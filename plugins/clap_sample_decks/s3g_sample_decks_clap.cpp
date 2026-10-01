#include "s3g_sample_decks_document.h"
#include "s3g_sample_decks_view.h"
#include "s3g_sample_decks_controls.h"
#include "s3g_sample_decks_beatpad_state.h"
#include "s3g_sample_decks_headphones.h"
#include "s3g_sample_decks_media.h"
#include "../clap_sample_neon/s3g_sample_neon_visual_state.h"
#include "../common/s3g_clap_gui_param_queue.h"
#include "../common/s3g_clap_state_stream.h"
#include "../common/s3g_sample_storage.h"
#include "../common/s3g_sample_file_decode.h"
#include <clap/clap.h>
#if defined(S3G_ENABLE_VSTGUI_CANVAS_GUI)
#include "../common/s3g_clap_vstgui.h"
#include "../common/s3g_vstgui_canvas.h"
#if defined(__APPLE__)
#include "../common/s3g_vstgui_readable_text_edit.h"
#endif
#endif
#if defined(__APPLE__)
#import <AVFoundation/AVFoundation.h>
#endif
#include <atomic>
#include <chrono>
#include <cstring>
#include <deque>
#include <future>
#include <memory>
#include <string>
#include <vector>

namespace {
using namespace s3g::sample;
using namespace s3g::decks;
namespace media=s3g::sample_storage;
constexpr unsigned kGuiWidth=1440,kGuiHeight=810;
constexpr uint64_t kAudioBudget=512ull*1024*1024;
struct PendingPad { bool active=false; unsigned mode=0,remaining=0; float velocity=1; };
struct PadNote { uint64_t id=0; uint8_t key=60; float velocity=1; };
struct WorkResult { unsigned deck=0,first=0; uint64_t revision=0; std::vector<LayerAudio> layers; std::string error,action; int captureFrom=-1; };
struct BookmarkEdit {unsigned deck=0,index=0;uint64_t revision=0;DeckBookmark cue;};
struct DeckPerformance {
    bool window=false,loop=false,raw=false,heldSlice=false,released=false;
    unsigned pad=0,bank=0,layer=0;double start=0,end=1;
    uint64_t note=0,remaining=UINT64_MAX;
    std::array<uint64_t,8> fxOrder{},rideOrder{};
    std::array<float,8> fxPressure{},rideTarget{};
    std::array<bool,8> fxToggle{};
    float rideFine=0;
    float ridePosition=0;unsigned rideReturnLayer=0;bool riding=false;
    int rollPad=-1;
    int auditionReceiver=-1;uint64_t auditionNote=0;
};
struct Capture {
    // 0 idle, 1 armed, 2 recording, 3 complete. Only audio writes PCM while 1/2.
    std::atomic<unsigned> state {0},frames {0}; std::atomic<bool> stop {false};
    std::atomic<unsigned> notice {0}; // 1 missing input, 2 changed output format
    unsigned from=0,to=1,target=0,width=2,format=0,capacity=0; double rate=48000;
    bool input=false;unsigned inputFirst=0;
    std::array<std::vector<float>,16> audio;
    std::array<std::atomic<float>,512> peaks {};
};
struct Plugin {
    clap_plugin_t plugin {}; const clap_host_t* host=nullptr;
    const clap_host_params_t* hostParams=nullptr;
    const clap_host_state_t* hostState=nullptr;
    const clap_host_timer_support_t* hostTimer=nullptr;
    clap_id timerId=CLAP_INVALID_ID;
    std::array<Definition,kParamCount> defs;
    std::vector<unsigned> ids;
    std::array<std::atomic<float>,kParamCount> values {};
    s3g::clap_gui::ParamEventQueue<4096> guiParams;
    s3g::clap_gui::SpscEventQueue<Command,1024> commands;
    s3g::clap_gui::SpscEventQueue<BookmarkEdit,64> bookmarkEdits;
    std::shared_ptr<Document> document=std::make_shared<Document>();
    std::shared_ptr<const LayerAudio> layerClipboard;
    std::deque<std::shared_ptr<Document>> undo,redo;
    std::vector<std::shared_ptr<Document>> retired;
    std::atomic<const Document*> published {nullptr},hazard {nullptr};
    std::array<uint64_t,2> audioRevision {{UINT64_MAX,UINT64_MAX}};
    std::array<const SampleAsset*,2> audioSource {};
    uint64_t revision=0;
    uint64_t loadedAudioBytes=0; // Main-thread display; no per-paint document walk.
    std::future<WorkResult> worker;
    std::future<DeckMediaResult> mediaWorker;
    std::array<std::array<DeckProjectMedia,32>,2> projectMedia;
    // Weak asset keys retain verified locators for internal undo/redo without
    // retaining extra PCM. Files themselves remain for host undo/project recall.
    std::vector<DeckProjectMedia> mediaHistory;
    std::chrono::steady_clock::time_point mediaQueryAfter{};
    bool busy=false;
    std::string message="DROP AUDIO ON EITHER DECK";
    SampleDecksEngine engine;
    DeckHeadphones headphones;
    DeckViewState viewState;
    std::array<NeonVisualPublication,2> playbackVisuals;
    std::array<std::atomic<int>,2> waveformLayer {{-1,-1}};
    std::array<std::atomic<float>,2> scanPosition {};
    std::array<float,2> lastStackTarget {{-2,-2}};
    std::array<bool,2> stackTouch {};
    std::array<std::vector<float>,32> scratch;
    Capture capture;
    std::array<std::vector<float>,16> inputScratch;
    std::atomic<int> recordRequest {-1};
    std::atomic<unsigned> forceStop {0};
    std::array<std::atomic<bool>,2> playing {},touching {};
    std::array<std::atomic<float>,2> cursor {},stackPosition {},pathPhase {},peaks {};
    std::array<std::atomic<unsigned>,2> soundingLayer {};
    std::array<bool,2> held {},shift {},cueOn {};
    std::array<double,2> cue {},resume {},jogTarget {};
    std::array<DeckPlatter,2> platters;
    BeatpadState beatpad;
    std::array<std::array<bool,2>,2> bendHeld{};
    int lastController=-1;
    std::array<std::array<float,64>,2> platterRates {},scanPositions {};
    std::array<float,2> scanMix {};
    std::array<std::array<float,8>,2> padPressure {};
    std::array<std::array<bool,8>,2> padHeld {};
    std::array<std::array<unsigned,8>,2> padOwner {};
    std::array<std::array<unsigned,8>,2> padOwnerBank {},padPulse {};
    std::array<std::array<bool,8>,2> padOwnerKeyboard {},screenHeld {};
    std::array<std::array<bool,8>,2> screenLatched {};
    std::array<std::array<std::atomic<uint32_t>,8>,2> padFeedback {};
    std::array<std::array<float,8>,2> padVelocity {};
    std::array<std::array<PendingPad,8>,2> pendingPads {};
    std::array<std::array<PadNote,8>,2> padNotes {};
    std::array<std::array<uint64_t,8>,2> padGestures {};
    std::array<DeckPerformance,2> performance {};
    std::array<NeonStack,2> performanceStacks;
    std::array<std::atomic<unsigned>,2> rollHistory {},performanceLights {};
    std::array<std::atomic<int>,2> lastTake {{-1,-1}};
    std::atomic<double> recordTempo {120};
    int nextTake=-1;
    uint64_t noteSerial=16;
    std::array<float,2> pressure {},lastPosition {{-1,-1}};
    std::array<unsigned,2> lastMethod {{99,99}},lastLayer {{99,99}};
    std::atomic<float> outputPeak {0};
    std::atomic<double> sampleRate {48000}; unsigned maximumFrames=0;
    std::atomic<bool> activated {false};
    bool hostPlaying=false; double tempo=120,beat=0; bool beatValid=false;
    std::atomic<bool> dirty {false};
    unsigned ledTick=0,callbackFrames=0;
    std::array<std::array<int16_t,128>,2> padLeds {};
    std::array<std::array<int16_t,6>,2> jogLeds {};
    std::array<int16_t,2> headphoneLeds {};
    bool ledInit=false; std::atomic<bool> refreshLeds {false};
    std::array<std::array<media::ProjectFileRegistration,32>,2> registrations;
#if defined(S3G_ENABLE_VSTGUI_CANVAS_GUI)
    s3g::portable_gui::foundation::EditorHost* portableGuiEditor=nullptr;
    uint32_t portableGuiWidth=kGuiWidth,portableGuiHeight=kGuiHeight; bool portableGuiVisible=false;
#endif
    Plugin() {
        for(unsigned i=0;i<kParamCount;++i) { defs[i]=definition(i); values[i].store(static_cast<float>(defs[i].initial)); if(!defs[i].name.empty())ids.push_back(i); }
        published.store(document.get()); cursor[0].store(-1);cursor[1].store(-1);
    }
};
Plugin* self(const clap_plugin_t* plugin) { return static_cast<Plugin*>(plugin->plugin_data); }
float value(const Plugin& p,unsigned i) noexcept { return i<kParamCount?p.values[i].load(std::memory_order_relaxed):0; }
float control(const Plugin& p,unsigned d,unsigned key) noexcept { return value(p,param(d,key)); }
bool keyboardPads(const Plugin& p,unsigned d) noexcept {return control(p,d,PadMode)==4&&control(p,d,SamplerPads)!=0;}
int keyboardNote(const Plugin& p,unsigned d,unsigned pad) noexcept {
    return int(control(p,d,KeyboardRoot))+12*int(control(p,d,KeyboardOctave))+8*int(control(p,d,PadBank))+int(pad);
}
double currentPosition(const Plugin& p,unsigned d) noexcept {
    return std::clamp((std::max(0.f,p.cursor[d].load())-control(p,d,Start))/std::max(1.e-7f,control(p,d,End)-control(p,d,Start)),0.f,1.f);
}
void request(Plugin& p) { if(p.host&&p.host->request_process)p.host->request_process(p.host); }
bool setValue(Plugin& p,unsigned i,double v) noexcept {
    if(i>=kParamCount||p.defs[i].name.empty()||!std::isfinite(v))return false;
    const auto& def=p.defs[i]; v=std::clamp(v,def.lo,def.hi); if(def.integer)v=std::round(v);
    if(i>=kDeckBase){const unsigned d=(i-kDeckBase)/kDeckStride,key=(i-kDeckBase)%kDeckStride;
        if(key==family(NeonFamily::StackShape)||key==family(NeonFamily::PathCount)){
            NeonFamilySettings f;for(unsigned n=0;n<kNeonFamilyCount;++n)f.values[n]=control(p,d,kFamily+n);
            const auto shape=static_cast<NeonStackShape>(key==family(NeonFamily::StackShape)?v:f[NeonFamily::StackShape]);
            const unsigned count=std::clamp(static_cast<unsigned>(key==family(NeonFamily::PathCount)?v:f[NeonFamily::PathCount]),neonStackShapeMinimum(shape),32u);
            if(shape==NeonStackShape::Manual&&key==family(NeonFamily::PathCount)){
                const auto old=f;for(unsigned n=0;n<count;++n){const float t=n/float(count-1);f.values[neonFamilyIndex(NeonFamily::PathTime)+n]=t;f.values[neonFamilyIndex(NeonFamily::PathValue)+n]=static_cast<float>(neonStackPath(old,t));}f[NeonFamily::PathCount]=count;
            }else neonSetStackShape(f,shape,count,d);
            for(unsigned n=0;n<kNeonFamilyCount;++n)p.values[param(d,kFamily+n)].store(f.values[n]);
            v=key==family(NeonFamily::PathCount)?f[NeonFamily::PathCount]:f[NeonFamily::StackShape];
        }
    }
    p.values[i].store(static_cast<float>(v)); p.dirty.store(true); return true;
}
void guiValue(Plugin& p,unsigned i,double v) {
    if(!setValue(p,i,v))return;
    s3g::clap_gui::enqueueParamEvent(p.guiParams,p.host,p.hostParams,s3g::clap_gui::ParamEventKind::Value,i+1,v);
    request(p);
}
void command(Plugin& p,Command c) { if(!p.commands.push(c))p.message="CONTROL QUEUE FULL";request(p); }
void clearPadFeedback(Plugin& p,unsigned d) noexcept {
    p.screenHeld[d]={};p.screenLatched[d]={};p.padPulse[d]={};p.padPressure[d]={};
    for(auto& state:p.padFeedback[d])state.store(0,std::memory_order_relaxed);
}
void notify(Plugin& p) {
    if(p.hostParams&&p.hostParams->rescan)p.hostParams->rescan(p.host,CLAP_PARAM_RESCAN_VALUES);
    if(p.hostState&&p.hostState->mark_dirty)p.hostState->mark_dirty(p.host);
    request(p);
}
uint64_t audioBytes(const Document& doc) {
    std::vector<const SampleAsset*> seen; uint64_t bytes=0;
    for(const auto& d:doc.decks)for(const auto& l:d.layers)if(l.asset&&std::find(seen.begin(),seen.end(),l.asset.get())==seen.end()) {
        seen.push_back(l.asset.get());bytes+=uint64_t(l.asset->frameCount())*l.asset->channelCount*sizeof(float);
    }
    return bytes;
}
void publish(Plugin& p,std::shared_ptr<Document> doc,unsigned mask,bool history=true) {
    if(history) {auto old=std::make_shared<Document>(*p.document);for(unsigned i=0;i<kParamCount;++i)old->savedControls[i]=value(p,i);
        p.undo.push_back(std::move(old));if(p.undo.size()>32)p.undo.pop_front();p.redo.clear();}
    for(unsigned d=0;d<2;++d)if(mask&(1u<<d)){doc->decks[d].revision=++p.revision;doc->decks[d].rebuild();}
    p.retired.push_back(p.document);p.document=std::move(doc);p.loadedAudioBytes=audioBytes(*p.document);p.published.store(p.document.get());p.mediaQueryAfter={};
    // seq_cst hazard publication protects acquisition; a modified deck stops
    // before old sources can be dereferenced by its renderer again.
    const auto* used=p.hazard.load();
    p.retired.erase(std::remove_if(p.retired.begin(),p.retired.end(),[&](const auto& old){return old.get()!=used;}),p.retired.end());
    // Bound history's retained PCM as well as its entry count.
    while(p.undo.size()>1) {
        uint64_t total=0;for(const auto& item:p.undo)total+=audioBytes(*item);
        if(total<=kAudioBudget)break;p.undo.pop_front();
    }
    notify(p);
}
void history(Plugin& p,bool forward) {
    if(p.busy||p.capture.state.load()!=0){p.message="FINISH RECORDING / LOADING FIRST";return;}
    auto& from=forward?p.redo:p.undo;auto& to=forward?p.undo:p.redo;if(from.empty())return;
    auto next=std::make_shared<Document>(*from.back());from.pop_back();auto current=std::make_shared<Document>(*p.document);
    for(unsigned i=0;i<kParamCount;++i)current->savedControls[i]=value(p,i);to.push_back(std::move(current));
    const auto action=forward?next->action:p.document->action;
    for(unsigned i=0;i<kParamCount;++i)p.values[i].store(next->savedControls[i]);
    publish(p,std::move(next),3,false);p.message=forward?"REDO":"UNDO";p.message+=" — "+action;
}
enum class LayerEdit { Copy, Paste, Clear };
bool editStackLayer(Plugin& p,unsigned d,unsigned n,LayerEdit action) {
    if(d>=2||n>=32||p.busy||p.capture.state.load()){p.message="FINISH RECORDING / LOADING FIRST";return false;}
    const auto& source=p.document->decks[d].layers[n];
    if(action==LayerEdit::Copy){if(!source.asset)return false;
        p.layerClipboard=std::make_shared<LayerAudio>(source);p.message="LAYER COPIED";return true;}
    if(action==LayerEdit::Paste&&!p.layerClipboard)return false;
    if(action==LayerEdit::Clear&&!source.asset)return false;
    auto doc=std::make_shared<Document>(*p.document);
    auto& deck=doc->decks[d];deck.layers[n]=action==LayerEdit::Paste?*p.layerClipboard:LayerAudio{};
    for(auto& cue:deck.bookmarks)if(cue.set&&cue.layer==n)cue={};
    if(audioBytes(*doc)>kAudioBudget){p.message="512 MIB AUDIO BUDGET EXCEEDED";return false;}
    doc->action=action==LayerEdit::Paste?"PASTE LAYER":"CLEAR LAYER";
    publish(p,std::move(doc),1u<<d);
    if(unsigned(control(p,d,Layer))==n){const auto& layer=p.document->decks[d].layers[n];
        guiValue(p,param(d,Start),layer.start);guiValue(p,param(d,End),layer.end);}
    p.message=p.document->action;return true;
}
bool decode(const std::string& path,std::shared_ptr<const SampleAsset>& result,std::string& error,uint64_t budget=kAudioBudget) {
#if defined(__APPLE__)
    @autoreleasepool {
        NSError* e=nil;NSString* name=[NSString stringWithUTF8String:path.c_str()];
        AVAudioFile* file=name?[[AVAudioFile alloc]initForReading:[NSURL fileURLWithPath:name] error:&e]:nil;
        if(!file){error="COULD NOT OPEN AUDIO";return false;}
        const auto format=file.processingFormat;const auto n=file.length;const unsigned ch=format.channelCount;
        if(!sampleNeonChannelCountSupported(ch)||n<1){error="INVALID CHANNEL COUNT OR EMPTY AUDIO";return false;}
        if(uint64_t(n)>budget/(ch*sizeof(float))){error="AUDIO EXCEEDS REMAINING "+std::to_string(budget/(1024*1024))+" MIB / 512 MIB TOTAL";return false;}
        // Decode in bounded chunks instead of holding a second full floating-
        // point copy of a multichannel file beside the final source vectors.
        constexpr AVAudioFrameCount chunk=65536;
        AVAudioPCMBuffer* b=[[AVAudioPCMBuffer alloc]initWithPCMFormat:format frameCapacity:chunk];
        if(!b){error="AUDIO DECODE BUFFER FAILED";return false;}
        auto a=std::make_shared<SampleAsset>();a->sampleRate=format.sampleRate;a->channelCount=static_cast<uint8_t>(ch);
        for(unsigned c=0;c<ch;++c)a->channels[c].resize(static_cast<size_t>(n));
        unsigned at=0;
        while(at<static_cast<unsigned>(n)){
            const auto requested=std::min(chunk,static_cast<unsigned>(n)-at);
            if(![file readIntoBuffer:b frameCount:requested error:&e]||!b.floatChannelData||!b.frameLength){error="AUDIO DECODE FAILED / TRUNCATED FILE";return false;}
            for(unsigned c=0;c<ch;++c)std::copy_n(b.floatChannelData[c],b.frameLength,a->channels[c].data()+at);
            at+=b.frameLength;
        }
        if(!a->valid()){error="INVALID AUDIO DATA";return false;}result=std::move(a);return true;
    }
#else
    if(!s3g::sample_file::decodeWaveFile(path,result,error))return false;
    if(uint64_t(result->frameCount())*result->channelCount*sizeof(float)>budget){result.reset();error="512 MIB AUDIO BUDGET EXCEEDED";return false;}return true;
#endif
}
void loadFiles(Plugin& p,unsigned deck,const std::vector<std::string>& paths) {
    if(deck>=2||paths.empty()||p.busy||p.capture.state.load()){p.message="FINISH CURRENT TAKE / LOAD FIRST";return;}
    const unsigned first=p.document->decks[deck].firstEmpty();
    if(first+paths.size()>32){p.message="STACK FULL";return;}
    for(unsigned n=first;n<first+paths.size();++n)if(p.document->decks[deck].layers[n].asset){p.message="NEED CONSECUTIVE EMPTY LAYERS";return;}
    p.busy=true;p.message="LOADING / ANALYZING AUDIO";const auto revision=p.document->decks[deck].revision;
    const uint64_t remaining=kAudioBudget-audioBytes(*p.document);
    p.worker=std::async(std::launch::async,[deck,first,paths,revision,remaining]{
        WorkResult r;r.deck=deck;r.first=first;r.revision=revision;r.action="LOAD AUDIO";
        uint64_t loaded=0;
        try {for(const auto& path:paths){LayerAudio l;l.path=path;l.name=std::filesystem::u8path(path).filename().u8string();
            if(!decode(path,l.asset,r.error,remaining-loaded))return r;loaded+=uint64_t(l.asset->frameCount())*l.asset->channelCount*sizeof(float);
            if(loaded>remaining){r.error="512 MIB AUDIO BUDGET EXCEEDED";return r;}analyzeLayer(l);r.layers.push_back(std::move(l));}}
        catch(...){r.error="AUDIO LOAD RAN OUT OF MEMORY";}return r;
    });
}
unsigned recordInputFirst(const Plugin& p,unsigned width) noexcept {
    return std::min(static_cast<unsigned>(value(p,RecordInputGroup)),32/width-1)*width;
}
void startRecord(Plugin& p,unsigned from,unsigned beats=0) {
    const auto state=p.capture.state.load();
    if(state==1||state==2){p.capture.stop.store(true);request(p);return;}
    if(state||p.busy||from>=2||!p.activated.load()){p.message="AUDIO MUST BE ACTIVE; FINISH CURRENT TAKE / LOAD";return;}
    const bool input=value(p,RecordSource)!=0;
    const unsigned to=input?from:1-from,target=p.document->decks[to].recordTarget(static_cast<unsigned>(control(p,to,Layer)));
    const auto format=static_cast<unsigned>(value(p,Format));const unsigned width=sampleNeonBusWidth(decksLayout(format));
    const auto* source=p.document->decks[from].playbackBase(static_cast<unsigned>(control(p,from,Layer)));
    if(!input&&!source){p.message="SOURCE DECK IS EMPTY";return;}
    if(target>=32){p.message="RECEIVING DECK HAS NO EMPTY LAYERS";return;}
    const bool distribute=control(p,from,family(NeonFamily::RoutingMode))!=0;
    if(!input&&!sampleNeonRouteCompatible(source->asset->channelCount,format>=3?SampleNeonSourceFormat::Ambisonic:SampleNeonSourceFormat::Discrete,decksLayout(format),from,distribute)){
        p.message="SOURCE CANNOT PLAY IN THIS OUTPUT FORMAT — CHECK FORMAT / ROUTING";return;}
    auto& c=p.capture;c.rate=p.sampleRate.load();c.width=width;c.from=from;c.to=to;c.target=target;c.format=format;
    c.input=input;c.inputFirst=recordInputFirst(p,width);c.notice.store(0);
    const double duration=beats?beats*60./p.recordTempo.load():value(p,RecordSeconds);
    c.capacity=static_cast<unsigned>(c.rate*duration);
    if(audioBytes(*p.document)+uint64_t(c.capacity)*width*sizeof(float)>kAudioBudget){p.message="TAKE EXCEEDS 512 MIB AUDIO BUDGET";return;}
    try{for(unsigned ch=0;ch<16;++ch)c.audio[ch].assign(ch<width?c.capacity:0,0.f);}catch(...){p.message="RECORD BUFFER ALLOCATION FAILED";return;}
    for(auto& peak:c.peaks)peak.store(0);c.frames.store(0);c.stop.store(false);c.state.store(1);
    p.message=input?"RECORDING TRACK INPUT → NEW "+std::string(to?"B":"A")+" LAYER":from==0?"RECORDING A → NEW B LAYER":"RECORDING B → NEW A LAYER";request(p);
}
// Hashing and copying are worker-only. In particular, REAPER requests state
// snapshots for ordinary mixer edits; state.save must never collect the files.
const DeckProjectMedia* preparedMedia(const Plugin& p,unsigned deck,unsigned layer,const media::ProjectLocation& project) {
    const auto& audio=p.document->decks[deck].layers[layer];
    const auto& current=p.projectMedia[deck][layer];
    if(current.matches(audio,project)&&current.copy.success)return &current;
    for(const auto& entry:p.mediaHistory)if(entry.matches(audio,project)&&entry.copy.success)return &entry;
    return nullptr;
}
void serviceProjectMedia(Plugin& p) {
    const auto now=std::chrono::steady_clock::now();
    const bool finished=p.mediaWorker.valid()&&p.mediaWorker.wait_for(std::chrono::seconds(0))==std::future_status::ready;
    if(!finished&&now<p.mediaQueryAfter)return;
    p.mediaQueryAfter=now+std::chrono::seconds(1);
    const auto context=media::reaperContext(p.host);media::ProjectLocation project;
    const bool projectReady=media::queryProjectLocation(context,project);
    if(finished){
        try{auto results=p.mediaWorker.get();bool changed=false;
            for(auto& job:results){const auto& layer=p.document->decks[job.deck].layers[job.layer];
                // Even a superseded result may belong to an undo version.
                if(job.media.copy.success)p.mediaHistory.push_back(job.media);
                if(!projectReady||!job.media.matches(layer,project))continue;
                auto& cached=p.projectMedia[job.deck][job.layer];cached=std::move(job.media);
                if(cached.copy.success){changed=true;
                    if(value(p,Storage)==0)(void)p.registrations[job.deck][job.layer].reset(context,cached.copy.absolutePath,nullptr,nullptr,"s3g Sample Decks 32");}
                else {cached.retryAfter=now+std::chrono::seconds(30);p.message="PROJECT MEDIA COPY FAILED — AUDIO WILL BE EMBEDDED; RETRY PENDING";}
            }
            if(changed&&value(p,Storage)==0)p.dirty.store(true);
        }catch(...){p.message="PROJECT MEDIA COPY FAILED — AUDIO WILL BE EMBEDDED";}
    }
    if(p.mediaWorker.valid()||!projectReady||value(p,Storage)!=0)return;
    p.mediaHistory.erase(std::remove_if(p.mediaHistory.begin(),p.mediaHistory.end(),
        [](const auto& entry){return entry.asset.expired();}),p.mediaHistory.end());
    DeckMediaResult jobs;
    for(unsigned d=0;d<2;++d)for(unsigned n=0;n<32;++n){const auto& l=p.document->decks[d].layers[n];auto& cached=p.projectMedia[d][n];
        if(!l.asset){cached={};p.registrations[d][n].clear();continue;}
        if(cached.matches(l,project)&&(cached.copy.success||now<cached.retryAfter))continue;
        if(const auto* previous=preparedMedia(p,d,n,project)){
            cached=*previous;(void)p.registrations[d][n].reset(context,cached.copy.absolutePath,nullptr,nullptr,"s3g Sample Decks 32");continue;}
        p.registrations[d][n].clear();cached={};cached.asset=l.asset;cached.source=l.path;cached.project=project;cached.attempted=true;
        cached.retryAfter=now+std::chrono::seconds(30); // Background retry, never a gesture/save loop.
        jobs.push_back({d,n,cached,l.asset,l.name});
    }
    if(jobs.empty())return;
    try{p.mediaWorker=std::async(std::launch::async,[jobs=std::move(jobs)]() mutable {
        for(auto& job:jobs){try{collectDeckMedia(job);}
            catch(...){job.media.copy.success=false;}}
        return jobs;
    });}catch(...){p.message="PROJECT MEDIA WORKER FAILED — AUDIO WILL BE EMBEDDED";}
}
void service(Plugin& p) {
    if(p.dirty.exchange(false)&&p.hostState&&p.hostState->mark_dirty)p.hostState->mark_dirty(p.host);
    BookmarkEdit edit;
    while(p.bookmarkEdits.peek(edit)){
        if(edit.revision==p.document->decks[edit.deck].revision){auto doc=std::make_shared<Document>(*p.document);
            doc->decks[edit.deck].bookmarks[edit.index]=edit.cue;doc->action=edit.cue.set?"STORE BOOKMARK":"CLEAR BOOKMARK";
            publish(p,std::move(doc),0);p.message=p.document->action;}
        p.bookmarkEdits.pop();
    }
    const int rec=p.recordRequest.exchange(-1);
    if(rec>=0){const unsigned from=unsigned(rec)&1,action=unsigned(rec)>>1;
        if(action==5&&(p.capture.state.load()==1||p.capture.state.load()==2)){
            p.nextTake=p.capture.from;p.capture.stop.store(true);request(p);
        }else startRecord(p,from,action>=1&&action<=4?1u<<(action-1):0);}
    if(p.capture.state.load()==3&&!p.busy) {
        auto& c=p.capture;const unsigned frames=c.frames.load();
        if(!frames){c.state.store(0);p.nextTake=-1;p.message=c.notice.load()==1?"NO TRACK INPUT PINS / CHECK REAPER INPUT ROUTING":"EMPTY TAKE DISCARDED";}
        else {
            auto a=std::make_shared<SampleAsset>();a->sampleRate=c.rate;a->channelCount=static_cast<uint8_t>(c.width);
            for(unsigned ch=0;ch<c.width;++ch){a->channels[ch]=std::move(c.audio[ch]);a->channels[ch].resize(frames);}
            const unsigned deck=c.to,first=c.target;const auto revision=p.document->decks[deck].revision;
            p.busy=true;p.message="FINISHING TAKE";
            p.worker=std::async(std::launch::async,[deck,first,revision,a,input=c.input,notice=c.notice.load(),from=c.from]{WorkResult r;r.deck=deck;r.first=first;r.revision=revision;r.captureFrom=static_cast<int>(from);r.action=input?"RECORD TRACK INPUT":"RESAMPLE";
                if(notice)r.action+=notice==1?" / INPUT LOST; TAKE KEPT":" / FORMAT CHANGED; TAKE KEPT";
                try{LayerAudio l;l.asset=a;l.name=input?"TRACK INPUT TO "+std::string(deck?"B":"A"):"RESAMPLE "+std::string(deck?"A TO B":"B TO A");analyzeLayer(l);r.layers.push_back(std::move(l));}
                catch(...){r.error="TAKE ANALYSIS FAILED";}return r;});
            c.state.store(0);
        }
    }
    if(p.busy&&p.worker.valid()&&p.worker.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
        auto r=p.worker.get();p.busy=false;
        if(!r.error.empty())p.message=r.error;
        else if(r.revision!=p.document->decks[r.deck].revision)p.message="STALE LOAD DISCARDED";
        else {
            auto next=std::make_shared<Document>(*p.document);auto& d=next->decks[r.deck];bool valid=true;
            for(unsigned i=0;i<r.layers.size();++i){valid&=r.layers[i].asset&&sampleNeonChannelCountSupported(r.layers[i].asset->channelCount);d.layers[r.first+i]=std::move(r.layers[i]);}
            if(!valid)p.message="UNSUPPORTED SOURCE CHANNEL COUNT";
            else if(audioBytes(*next)>kAudioBudget)p.message="512 MIB AUDIO BUDGET EXCEEDED";
            else {next->action=r.action;publish(p,std::move(next),1u<<r.deck);guiValue(p,param(r.deck,Layer),r.first);guiValue(p,param(r.deck,Start),0);guiValue(p,param(r.deck,End),1);p.message=r.action+" READY — AUDITION WITH PLAY";
                if(r.captureFrom>=0){p.lastTake[r.captureFrom].store(static_cast<int>(r.deck*32+r.first));
                    guiValue(p,param(r.deck,SourceMode),1);guiValue(p,param(r.deck,StackNavigation),0);guiValue(p,param(r.deck,Position),0);}}
        }
        if(p.nextTake>=0){const unsigned from=static_cast<unsigned>(p.nextTake);p.nextTake=-1;startRecord(p,from);}
    }
    serviceProjectMedia(p);
}

#include "s3g_sample_decks_performance.inc"
#include "s3g_sample_decks_cues.inc"

SampleNeonSettings settings(Plugin& p,const Document& doc,unsigned frames=0) {
    SampleNeonSettings s;s.outputLayout=decksLayout(static_cast<unsigned>(value(p,Format)));s.masterGainDecibels=0;
    s.hostTempoBpm=static_cast<float>(p.tempo);s.hostBeatPosition=p.beat;s.hostBeatValid=p.beatValid;s.transportPlaying=p.hostPlaying;
    for(unsigned d=0;d<2;++d){auto& c=s.slots[d];const auto& source=doc.decks[d];const auto get=[&](unsigned key){return control(p,d,key);};
        c.stack=&source.playbackStack(static_cast<unsigned>(get(Layer)));c.selectedLayer=static_cast<uint8_t>(get(Layer));c.sourceMode=static_cast<NeonSourceMode>(get(SourceMode));
        c.deckWavesetSources=true;c.deckTransport=true;
        c.playback=static_cast<SampleNeonPlayback>(get(Playback));c.outputBus=static_cast<uint8_t>(d);c.gainDecibels=0;
        c.sourceFormat=value(p,Format)>=3?SampleNeonSourceFormat::Ambisonic:SampleNeonSourceFormat::Discrete;
        c.start=get(Start);c.end=std::max(c.start+1.e-7,double(get(End)));c.end=std::min(1.,c.end);
        const auto* base=source.playbackBase(c.selectedLayer);
        c.sourceDurationSeconds=base?base->asset->frameCount()/base->asset->sampleRate:0;
        c.tuneSemitones=get(Tune);const float rate=get(Rate);
        if(c.playback==SampleNeonPlayback::Sample||c.playback==SampleNeonPlayback::Wavesets)c.tuneSemitones+=12*std::log2(rate);
        c.direction=static_cast<uint8_t>(get(Direction));c.repeat=get(Repeat)!=0;c.triggerMode=TriggerMode::Gate;
        c.velocityEnabled=true;
        c.rootNote=static_cast<uint8_t>(get(KeyboardRoot));c.noteVoiceLimit=static_cast<uint8_t>(get(KeyboardVoices));
        c.retriggerMode=RetriggerMode::Restart;c.clock=static_cast<SampleNeonClock>(get(Clock));
        c.motionCycleSeconds=get(Cycle)/rate;c.motionCycleBeats=get(CycleBeats)/rate;c.stackCycleSeconds=get(StackCycle);c.stackCycleBeats=get(StackBeats);
        c.motionPath=static_cast<SampleNeonMotionPath>(get(MotionPath));
        c.launchPosition=get(Position);c.grainPosition=get(Position);c.sourceTempoBpm=get(SourceBpm);c.grainSizeMs=get(GrainSize);c.grainDensityHz=get(Density)*rate;
        c.grainSpray=get(Spray);c.grainPitchSpraySemitones=get(PitchSpray);c.grainReverseChance=get(ReverseChance);
        c.techniqueAttackSeconds=get(Attack);c.techniqueReleaseSeconds=get(Release);c.attackProportion=.005f;c.releaseProportion=.02f;
        c.character=static_cast<SampleNeonMangleCharacter>(get(Character));c.mangle=get(Amount);c.pressureDepth=get(PressureDepth);
        for(unsigned i=0;i<kNeonFamilyCount;++i)c.family.values[i]=get(kFamily+i);
        if(!c.family.valid())c.family=NeonFamilySettings{};
        if(get(StackNavigation)!=0&&(c.playback==SampleNeonPlayback::Sample||c.playback==SampleNeonPlayback::SliceSequence||c.playback==SampleNeonPlayback::Cutups)){
            c.sourceMode=NeonSourceMode::Selected;
            if(c.playback==SampleNeonPlayback::Cutups){c.family[NeonFamily::CutFileOrder]=5;
                for(unsigned n=0;n<kMaximumCutupsPatternSteps;++n)c.family.values[neonFamilyIndex(NeonFamily::CutPatternLane)+n]=get(Layer);}}
        c.family[NeonFamily::CutRate]=std::clamp(c.family[NeonFamily::CutRate]*rate,.1f,80.f);
        c.family[NeonFamily::LaneRate]=std::clamp(c.family[NeonFamily::LaneRate]*rate,.25f,4.f);
        for(unsigned i=0;i<4;++i)c.technique[i]=get(Technique0+i);
        for(unsigned i=0;i<kNeonCharacterControls;++i)c.fx[i]=get(kFx+static_cast<unsigned>(c.character)*kNeonCharacterControls+i);
        c.sliceLayout=source.layers[c.selectedLayer].slices;c.sliceCount=c.sliceLayout.sliceCount;
        if(c.sourceMode==NeonSourceMode::Primary&&c.stack->skipEmptyLayers&&!c.stack->layers[0].asset){
            unsigned first=0;while(first<32&&!c.stack->layers[first].asset)++first;
            if(first<32){c.sourceMode=NeonSourceMode::Selected;c.selectedLayer=first;c.start=source.layers[first].start;c.end=source.layers[first].end;
                c.sliceLayout=source.layers[first].slices;c.sliceCount=c.sliceLayout.sliceCount;}}
        performanceSettings(p,doc,d,c,frames);
    }return s;
}

void preparePlatters(Plugin& p,SampleNeonSettings& s,unsigned frames) noexcept {
    const double sampleRate=p.sampleRate.load();
    for(unsigned d=0;d<2;++d){auto& c=s.slots[d];
        const unsigned layer=std::min(31u,p.soundingLayer[d].load());
        const auto& source=c.stack->layers[layer];
        const double duration=source.asset ? source.asset->frameCount()/source.asset->sampleRate
            *std::max(1.e-7,layer==c.selectedLayer?c.end-c.start:source.end-source.start) : 1.;
        for(unsigned n=0;n<frames;++n){const float speed=p.platters[d].next();p.platterRates[d][n]=speed;
            p.scanPositions[d][n]=static_cast<float>(p.jogTarget[d]);
            p.jogTarget[d]=neonUnitPhase(p.jogTarget[d]+speed*control(p,d,Rate)
                *(control(p,d,Direction)==1?-1.:1.)/(sampleRate*std::max(.001,duration)));
        }
        c.deckPlatterRates=p.platterRates[d].data();
        if(p.scanMix[d]>0){c.deckScanPositions=p.scanPositions[d].data();c.deckScanMix=p.scanMix[d];
            c.sourceScrub=p.scanPositions[d][0];}
        if(!p.touching[d].load()&&!p.bendHeld[d][0]&&!p.bendHeld[d][1])p.scanMix[d]=std::max(0.f,p.scanMix[d]-static_cast<float>(frames/(sampleRate*.12)));
    }
}

void audioCommand(Plugin& p,const Command& c,std::array<SampleNeonEvent,64>& events,unsigned& count) noexcept {
    const unsigned d=std::min(1u,c.deck);auto add=[&](SampleNeonEventKind kind,float v=1.f){if(count>=events.size())return;
        auto& e=events[count++];e={};e.slot=static_cast<uint8_t>(d);e.kind=kind;e.noteId=d+1;e.value=v;};
    auto clearNotes=[&]{p.padNotes[d]={};p.pendingPads[d]={};p.padHeld[d]={};p.padPressure[d]={};p.screenHeld[d]={};p.padPulse[d]={};
        p.performance[d]={};p.padGestures[d]={};p.engine.roll(d).release();
        for(auto& state:p.padFeedback[d])state.store(0,std::memory_order_relaxed);
        for(unsigned n=0;n<count;)if(events[n].slot==d){for(unsigned k=n+1;k<count;++k)events[k-1]=events[k];--count;}else ++n;};
    auto start=[&]{clearNotes();p.engine.stop(d);const unsigned before=count;add(SampleNeonEventKind::Trigger);p.playing[d].store(count>before);return count>before;};
    switch(c.kind){
    case CommandKind::Play:if(p.playing[d].load()){clearNotes();p.engine.stop(d);p.playing[d].store(false);p.platters[d].reset();p.bendHeld[d]={};p.touching[d].store(false);p.scanMix[d]=0;}else start();break;
    case CommandKind::Stop:clearNotes();p.engine.stop(d);p.playing[d].store(false);p.touching[d].store(false);p.platters[d].reset();p.bendHeld[d]={};p.beatpad.faderStart[d]=false;p.scanMix[d]=0;break;
    case CommandKind::Bend:
        // Non-vinyl techniques use the same temporary source scan as rim jog;
        // keep it engaged while held, then blend back to their native path.
        if(c.value>0){if(p.scanMix[d]==0)p.jogTarget[d]=currentPosition(p,d);p.scanMix[d]=1;}
        p.bendHeld[d][c.index%2]=c.value>0;
        p.platters[d].bend(.08f*(int(p.bendHeld[d][1])-int(p.bendHeld[d][0])));break;
    case CommandKind::Seek: {
        const double pos=std::clamp(c.value,0.,1.);setValue(p,param(d,Position),pos);p.jogTarget[d]=pos;p.engine.seek(d,pos);break;}
    case CommandKind::Touch:
        if(c.value>0&&!p.touching[d].load()){p.resume[d]=currentPosition(p,d);p.jogTarget[d]=p.resume[d];p.touching[d].store(true);p.platters[d].touch(true);p.scanMix[d]=1;}
        else if(c.value<=0&&p.touching[d].load()){p.touching[d].store(false);p.platters[d].touch(false);
            if(control(p,d,Slip)!=0){p.engine.seek(d,p.resume[d]);p.jogTarget[d]=p.resume[d];}}
        break;
    case CommandKind::Jog:
        if(c.value!=0){if(p.scanMix[d]==0)p.jogTarget[d]=currentPosition(p,d);p.scanMix[d]=1;
            p.platters[d].move(static_cast<int>(c.value),c.index!=0);}
        break;
    case CommandKind::StackJog: {
        const auto* doc=p.hazard.load();if(!doc||c.value==0)break;
        const unsigned layers=doc->decks[d].stack.count;if(layers<2)break;
        if(newest(p.performance[d].rideOrder)>=0){p.performance[d].rideFine+=static_cast<float>(c.value*.08/(layers-1));break;}
        const unsigned method=static_cast<unsigned>(control(p,d,Playback));
        const bool discrete=method==0||method==3||method==8;
        const double base=control(p,d,StackNavigation)!=0?control(p,d,StackPosition)
            :discrete?control(p,d,Layer)/(layers-1):p.stackPosition[d].load();
        setValue(p,param(d,StackPosition),std::clamp(base+c.value*(discrete?1.:.08)/(layers-1),0.,1.));
        setValue(p,param(d,StackNavigation),1);break;}
    case CommandKind::Cue:case CommandKind::CueStop:{
        const auto* doc=p.hazard.load();const int layer=static_cast<int>(control(p,d,CueLayer));
        bool blend=false;
        if(layer>=0){if(!doc||!doc->decks[d].layers[layer].asset){
                if(c.kind==CommandKind::CueStop)audioCommand(p,{CommandKind::Stop,d},events,count);
                break;
            }blend=selectCueLayer(p,*doc,d,static_cast<unsigned>(layer),control(p,d,CueStackPosition));}
        setValue(p,param(d,Position),control(p,d,CuePosition));
        if(c.kind==CommandKind::CueStop){audioCommand(p,{CommandKind::Stop,d},events,count);p.engine.seek(d,control(p,d,Position));break;}
        if(start()&&layer>=0){events[count-1].selectedSource=!blend;events[count-1].snapStackPosition=blend;}break;}
    case CommandKind::SetCue:{
        const auto* doc=p.hazard.load();if(!doc)break;const auto cue=cueForStore(p,*doc,d,events,count);if(!cue.set)break;
        const auto& source=doc->decks[d].layers[cue.layer];
        setValue(p,param(d,CueLayer),cue.layer);
        setValue(p,param(d,CueStackPosition),cue.stackPosition);
        setValue(p,param(d,CuePosition),(cue.position-source.start)/std::max(1.e-7,source.end-source.start));break;}
    case CommandKind::Sync:setValue(p,param(d,Rate),p.tempo/control(p,d,SourceBpm));setValue(p,param(d,Position),0);start();break;
    case CommandKind::FollowSource:
        setValue(p,param(d,StackNavigation),0);
        p.performance[d].riding=false;p.performance[d].rideOrder={};p.performance[d].rideFine=0;
        // Force a resume event even if the menu already reads Follow Source;
        // a single-layer cue can still have an isolated source voice.
        p.lastStackTarget[d]=-2;break;
    case CommandKind::Record:p.recordRequest.store(static_cast<int>(d));if(p.host&&p.host->request_callback)p.host->request_callback(p.host);break;
    case CommandKind::TimedCapture:case CommandKind::NextTake:
        p.recordRequest.store(static_cast<int>(d+2*(c.kind==CommandKind::NextTake?5:1+std::min(3u,c.index))));
        if(p.host&&p.host->request_callback)p.host->request_callback(p.host);break;
    case CommandKind::StoreBookmark:case CommandKind::ClearBookmark:{
        const auto* doc=p.hazard.load();if(!doc)break;
        const DeckBookmark cue=c.kind==CommandKind::StoreBookmark?cueForStore(p,*doc,d,events,count):DeckBookmark{};
        if(c.kind==CommandKind::StoreBookmark&&!cue.set)break;
        (void)p.bookmarkEdits.push({d,c.index%8,doc->decks[d].revision,cue});
        if(p.host&&p.host->request_callback)p.host->request_callback(p.host);break;}
    case CommandKind::AuditionTake:case CommandKind::LaunchTake:{
        if(c.kind==CommandKind::AuditionTake&&c.value<=0){auto& perf=p.performance[d];
            if(perf.auditionReceiver>=0&&count<events.size()){auto& e=events[count++];e={};e.slot=perf.auditionReceiver;e.kind=SampleNeonEventKind::Release;e.noteId=perf.auditionNote;}
            perf.auditionReceiver=-1;break;}
        const int take=p.lastTake[d].load();const auto* doc=p.hazard.load();if(take<0||!doc||!doc->decks[take/32].layers[take%32].asset)break;
        const unsigned receiver=static_cast<unsigned>(take)/32,layer=static_cast<unsigned>(take)%32;
        setValue(p,param(receiver,Layer),layer);setValue(p,param(receiver,SourceMode),1);setValue(p,param(receiver,Position),0);
        audioCommand(p,{CommandKind::Stop,receiver},events,count);
        if(count<events.size()){auto& e=events[count++];e={};e.slot=receiver;e.noteId=++p.noteSerial;p.playing[receiver].store(true);
            if(c.kind==CommandKind::AuditionTake){p.performance[d].auditionReceiver=receiver;p.performance[d].auditionNote=e.noteId;}}break;}
    case CommandKind::PadPressure:p.padPressure[d][c.index%8]=static_cast<float>(std::clamp(c.value,0.,1.));
        if(p.performance[d].fxOrder[c.index%8])p.performance[d].fxPressure[c.index%8]=p.padPressure[d][c.index%8];break;
    case CommandKind::ScreenPad: {
        const unsigned pad=c.index%8;
        // A latch is a mouse convenience, not a different voice engine. An
        // explicit negative value releases it (editor close / latch disabled).
        if(c.value==0&&p.screenLatched[d][pad])break;
        if(c.value<=0&&!p.screenHeld[d][pad])break;
        const unsigned mode=static_cast<unsigned>(control(p,d,PadMode));
        const bool hold=keyboardPads(p,d)||(mode==2&&control(p,d,FxLatch)==0)||(mode==3&&control(p,d,SliceHold)!=0)||mode==5||mode==6||(mode==7&&pad==6);
        const bool latch=hold&&control(p,d,ScreenLatch)!=0;
        const bool on=c.value>0&&!(p.screenLatched[d][pad]&&p.screenHeld[d][pad]);
        audioCommand(p,{CommandKind::Pad,d,pad,on?control(p,d,ScreenVelocity):0.},events,count);
        p.screenHeld[d][pad]=on&&p.padHeld[d][pad];p.screenLatched[d][pad]=p.screenHeld[d][pad]&&latch;break;}
    case CommandKind::Pad:{
        const unsigned pad=c.index%8,bank=static_cast<unsigned>(control(p,d,PadBank)),mode=static_cast<unsigned>(control(p,d,PadMode));
        auto& perf=p.performance[d];
        if(c.value<=0){if(!p.padHeld[d][pad])break;p.padHeld[d][pad]=false;p.screenHeld[d][pad]=false;p.padPressure[d][pad]=0;
            auto& note=p.padNotes[d][pad];
            if(note.id){if(count>=events.size())break;add(SampleNeonEventKind::Release);events[count-1].noteId=note.id;events[count-1].key=note.key;note={};break;}
            const auto owner=p.padOwner[d][pad];
            if(owner==3&&perf.window&&perf.heldSlice&&perf.note==p.padGestures[d][pad]&&!perf.released){add(SampleNeonEventKind::Release);events[count-1].noteId=perf.note;perf.released=true;}
            if(owner==2&&!perf.fxToggle[pad])perf.fxOrder[pad]=0;
            if(owner==5&&perf.rollPad==int(pad)){p.engine.roll(d).release();perf.rollPad=-1;}
            if(owner==6)perf.rideOrder[pad]=0;
            if(owner==7&&pad==6)audioCommand(p,{CommandKind::AuditionTake,d,0,0},events,count);
            break;}
        if(count+2>events.size())break;
        const auto* doc=p.hazard.load();if(!doc||!padAvailable(p,*doc,d,mode,bank,pad))break;
        if(p.padHeld[d][pad])audioCommand(p,{CommandKind::Pad,d,pad,0},events,count);
        auto& note=p.padNotes[d][pad];
        if(note.id){add(SampleNeonEventKind::Release);events[count-1].noteId=note.id;events[count-1].key=note.key;note={};}
        p.padHeld[d][pad]=true;p.screenHeld[d][pad]=false;p.padOwner[d][pad]=mode;
        p.padGestures[d][pad]=++p.noteSerial;
        if(keyboardPads(p,d)){
            if(perf.window){perf.window=false;p.engine.stopVoices(d);p.padNotes[d]={};}
            const int key=keyboardNote(p,d,pad);if(key<0||key>127){p.padHeld[d][pad]=false;break;}
            note={++p.noteSerial,static_cast<uint8_t>(key),static_cast<float>(c.value)};
            add(SampleNeonEventKind::Trigger,note.velocity);events[count-1].key=note.key;events[count-1].noteId=note.id;p.playing[d].store(true);
        }
        else if(mode==4){setValue(p,param(d,Layer),bank*8+pad);setValue(p,param(d,SourceMode),1);start();p.padHeld[d][pad]=true;p.padOwner[d][pad]=mode;events[count-1].value=static_cast<float>(c.value);events[count-1].selectedSource=true;}
        else if(mode==0){
            const unsigned action=static_cast<unsigned>(control(p,d,CueAction));
            if(action){audioCommand(p,{action==1?CommandKind::StoreBookmark:CommandKind::ClearBookmark,d,pad},events,count);setValue(p,param(d,CueAction),0);}
            else {const auto cue=doc->decks[d].bookmarks[pad];const auto& layer=doc->decks[d].layers[cue.layer];
                const bool blend=selectCueLayer(p,*doc,d,cue.layer,cue.stackPosition);
                setValue(p,param(d,Position),std::clamp((cue.position-layer.start)/std::max(1.e-7,layer.end-layer.start),0.,1.));
                perf.window=false;p.padNotes[d]={};p.engine.stopVoices(d);add(SampleNeonEventKind::Trigger,static_cast<float>(c.value));events[count-1].selectedSource=!blend;events[count-1].snapStackPosition=blend;p.playing[d].store(true);}
        }
        else if(mode==2){const bool toggle=control(p,d,FxLatch)!=0;
            perf.fxOrder[pad]=toggle&&perf.fxOrder[pad]?0:p.padGestures[d][pad];perf.fxToggle[pad]=toggle;perf.fxPressure[pad]=static_cast<float>(c.value);}
        else if(mode==6){const unsigned layers=doc->decks[d].stack.count;
            if(!perf.riding){perf.rideReturnLayer=std::min(31u,p.soundingLayer[d].load());perf.ridePosition=layers>1?float(perf.rideReturnLayer)/(layers-1):0;perf.riding=true;}
            perf.rideOrder[pad]=p.padGestures[d][pad];perf.rideTarget[pad]=layers>1?float(bank*8+pad)/(layers-1):0;perf.rideFine=0;}
        else if(mode==5){if(p.engine.roll(d).start(std::ldexp(1.,int(pad)-5)*60./p.tempo))perf.rollPad=static_cast<int>(pad);else p.padHeld[d][pad]=false;}
        else if(mode==7){
            const auto kind=pad<4?CommandKind::TimedCapture:pad==4?CommandKind::Record:pad==5?CommandKind::NextTake:pad==6?CommandKind::AuditionTake:CommandKind::LaunchTake;
            audioCommand(p,{kind,d,pad,1},events,count);p.padHeld[d][pad]=true;p.padOwner[d][pad]=mode;}
        else if(mode==1||mode==3){
            if(mode==1&&perf.window&&perf.loop&&perf.pad==pad){
                const double absolute=std::max(0.f,p.cursor[d].load());perf.window=false;
                setValue(p,param(d,Position),(absolute-control(p,d,Start))/std::max(1.e-7f,control(p,d,End)-control(p,d,Start)));
                p.engine.stopVoices(d);add(SampleNeonEventKind::Trigger,static_cast<float>(c.value));
            }else{
                const unsigned selected=static_cast<unsigned>(control(p,d,Layer));
                const unsigned sounding=std::min(31u,p.soundingLayer[d].load());
                const unsigned layer=mode==1&&p.playing[d].load()&&doc->decks[d].layers[sounding].asset?sounding:selected;const auto& source=doc->decks[d].layers[layer];
                const double lo=layer==selected?double(control(p,d,Start)):source.start,hi=layer==selected?double(control(p,d,End)):source.end,duration=source.asset->frameCount()/source.asset->sampleRate;
                double first=0,last=1;
                if(mode==3){first=lo+(hi-lo)*source.slices.boundaries[bank*8+pad];last=lo+(hi-lo)*source.slices.boundaries[bank*8+pad+1];}
                else {const double length=std::min(hi-lo,std::ldexp(1.,int(pad)-4)*60./control(p,d,SourceBpm)/duration);
                    const double cursor=p.playing[d].load()?double(p.cursor[d].load()):lo+(hi-lo)*control(p,d,Position);
                    first=std::clamp(cursor,lo,hi-length);last=first+length;}
                perf.window=true;perf.loop=mode==1;perf.heldSlice=mode==3&&control(p,d,SliceHold)!=0;
                perf.raw=mode==3&&control(p,d,SliceRaw)!=0;perf.pad=pad;perf.bank=bank;perf.layer=layer;perf.start=first;perf.end=last;
                perf.note=p.padGestures[d][pad];perf.released=false;
                perf.remaining=perf.loop||perf.heldSlice?UINT64_MAX:std::max(uint64_t(1),uint64_t((last-first)*duration/control(p,d,Rate)*p.sampleRate.load()));
                p.engine.stopVoices(d);p.padNotes[d]={};add(SampleNeonEventKind::Trigger,static_cast<float>(c.value));events[count-1].noteId=perf.note;
            }p.playing[d].store(true);
        }
        if(p.padHeld[d][pad]){p.padOwnerBank[d][pad]=bank;p.padOwnerKeyboard[d][pad]=keyboardPads(p,d);
            p.padVelocity[d][pad]=static_cast<float>(std::clamp(c.value,0.,1.));p.padPulse[d][pad]=static_cast<unsigned>(p.sampleRate.load()*.12);}
        break;}
    }
}

#include "s3g_sample_decks_midi.inc"
#include "s3g_sample_decks_state.inc"

bool pluginInit(const clap_plugin_t* plugin){auto& p=*self(plugin);
    if(p.host&&p.host->get_extension){p.hostParams=static_cast<const clap_host_params_t*>(p.host->get_extension(p.host,CLAP_EXT_PARAMS));p.hostState=static_cast<const clap_host_state_t*>(p.host->get_extension(p.host,CLAP_EXT_STATE));
        p.hostTimer=static_cast<const clap_host_timer_support_t*>(p.host->get_extension(p.host,CLAP_EXT_TIMER_SUPPORT));
        if(p.hostTimer&&p.hostTimer->register_timer)p.hostTimer->register_timer(p.host,16,&p.timerId);
    }return true;}
bool activate(const clap_plugin_t* plugin,double rate,uint32_t,uint32_t maximum){auto& p=*self(plugin);if(!std::isfinite(rate)||rate<8000||rate>384000||!maximum||maximum>65536)return false;
    try{if(!p.engine.prepare(rate,maximum))return false;p.headphones.prepare(rate);for(auto& c:p.scratch)c.assign(maximum,0);for(auto& c:p.inputScratch)c.assign(maximum,0);}catch(...){return false;}
    for(auto& platter:p.platters)platter.prepare(rate);p.scanMix={};for(auto& held:p.touching)held.store(false);p.lastStackTarget.fill(-2);p.stackTouch={};p.padNotes={};p.padHeld={};p.pendingPads={};
    p.beatpad.resetGestures();p.bendHeld={};p.shift={};p.lastController=-1;
    for(unsigned d=0;d<2;++d)clearPadFeedback(p,d);
    p.performance={};p.padGestures={};for(auto& light:p.performanceLights)light.store(0);for(auto& history:p.rollHistory)history.store(0);
    p.sampleRate.store(rate);p.maximumFrames=maximum;p.audioRevision.fill(UINT64_MAX);p.ledInit=false;p.activated.store(true);return true;}
void deactivate(const clap_plugin_t* plugin){auto& p=*self(plugin);p.engine.reset();p.playing[0].store(false);p.playing[1].store(false);p.hazard.store(nullptr);p.activated.store(false);
    for(unsigned d=0;d<2;++d)clearPadFeedback(p,d);
    if(p.capture.state.load()==1||p.capture.state.load()==2)p.capture.state.store(3);}
bool startProcessing(const clap_plugin_t*){return true;}
void stopProcessing(const clap_plugin_t*){}
void reset(const clap_plugin_t* plugin){auto& p=*self(plugin);p.engine.reset();p.headphones.reset();for(unsigned d=0;d<2;++d){p.playing[d].store(false);p.touching[d].store(false);p.platters[d].reset();p.playbackVisuals[d].clear();clearPadFeedback(p,d);p.rollHistory[d].store(0);p.performanceLights[d].store(0);}p.performance={};p.padGestures={};p.lastStackTarget.fill(-2);p.stackTouch={};p.scanMix={};p.padHeld={};p.pendingPads={};p.padNotes={};p.padPressure={};p.pressure={};p.beatpad.resetGestures();p.bendHeld={};p.shift={};p.refreshLeds.store(true);}

// Read the complete input block before output writes (hosts may alias buffers).
// Missing channels are not the same as connected, silent channels. Never record
// partially missing spatial fields or duplicate stereo into a wider source.
bool prepareTrackInput(Plugin& p,const clap_process_t& block,unsigned& width) noexcept {
    const auto state=p.capture.state.load();const bool recording=(state==1||state==2)&&p.capture.input;
    width=recording?p.capture.width:sampleNeonBusWidth(decksLayout(static_cast<unsigned>(value(p,Format))));
    const unsigned first=recording?p.capture.inputFirst:recordInputFirst(p,width);
    const auto* bus=block.audio_inputs&&block.audio_inputs_count?&block.audio_inputs[0]:nullptr;
    bool connected=bus&&bus->channel_count>=first+width&&(bus->data32||bus->data64);
    if(connected)for(unsigned ch=0;ch<width;++ch)if(!(bus->data32&&bus->data32[first+ch])&&!(bus->data64&&bus->data64[first+ch]))connected=false;
    for(unsigned ch=0;ch<width;++ch)for(unsigned n=0;n<block.frames_count;++n){
        const double v=!connected?0:bus->data32&&bus->data32[first+ch]?bus->data32[first+ch][n]:bus->data64[first+ch][n];
        const float safe=std::isfinite(v)?static_cast<float>(std::clamp(v,-1.e6,1.e6)):0;
        p.inputScratch[ch][n]=safe;}
    return connected;
}
clap_process_status process(const clap_plugin_t* plugin,const clap_process_t* block){
    auto& p=*self(plugin);if(!block||block->frames_count>p.maximumFrames||!block->audio_outputs||block->audio_outputs_count<1)return CLAP_PROCESS_ERROR;
    auto& out=block->audio_outputs[0];if(out.channel_count!=32||(!out.data32&&!out.data64))return CLAP_PROCESS_ERROR;
    for(unsigned ch=0;ch<32;++ch)if((out.data32&&!out.data32[ch])||(out.data64&&!out.data64[ch]))return CLAP_PROCESS_ERROR;
    unsigned inputWidth=0;const bool inputConnected=prepareTrackInput(p,*block,inputWidth);
    const Document* doc=nullptr;do{doc=p.published.load();p.hazard.store(doc);}while(doc!=p.published.load());
    if(p.lastController!=int(value(p,Controller))){p.lastController=int(value(p,Controller));p.beatpad.resetGestures();p.shift={};p.bendHeld={};
        for(auto& platter:p.platters)platter.bend(0);}
    for(unsigned d=0;d<2;++d)if(p.audioRevision[d]!=doc->decks[d].revision){p.engine.stop(d);p.padNotes[d]={};p.padHeld[d]={};p.pendingPads[d]={};p.playing[d].store(false);p.platters[d].reset();p.touching[d].store(false);p.scanMix[d]=0;p.audioRevision[d]=doc->decks[d].revision;
        p.performance[d]={};p.bendHeld[d]={};p.beatpad.storingCue[d]={};p.beatpad.faderStart[d]=false;p.rollHistory[d].store(0);p.performanceLights[d].store(0);
        clearPadFeedback(p,d);
        const auto* base=doc->decks[d].playbackBase(static_cast<unsigned>(control(p,d,Layer)));p.audioSource[d]=base?base->asset.get():nullptr;
        p.engine.setSource(d,p.audioSource[d],base?base->wavesets.get():nullptr);}
    if(const unsigned mask=p.forceStop.exchange(0))for(unsigned d=0;d<2;++d)if(mask&(1u<<d)){p.engine.stop(d);p.performance[d]={};p.padNotes[d]={};p.padHeld[d]={};p.pendingPads[d]={};p.playing[d].store(false);p.platters[d].reset();p.bendHeld[d]={};p.beatpad.storingCue[d]={};p.beatpad.faderStart[d]=false;p.touching[d].store(false);p.scanMix[d]=0;clearPadFeedback(p,d);}
    if(block->transport){const auto& t=*block->transport;p.hostPlaying=t.flags&CLAP_TRANSPORT_IS_PLAYING;p.beatValid=t.flags&CLAP_TRANSPORT_HAS_BEATS_TIMELINE;
        if((t.flags&CLAP_TRANSPORT_HAS_TEMPO)&&std::isfinite(t.tempo))p.tempo=std::clamp(t.tempo,20.,400.);if(p.beatValid)p.beat=double(t.song_pos_beats)/CLAP_BEATTIME_FACTOR;}
    else {p.hostPlaying=false;p.beatValid=false;}
    p.recordTempo.store(p.tempo);
    // GUI values are already atomic. Re-applying queued history would undo a
    // later Undo/Load operation before the host's next audio callback.
    s3g::clap_gui::serviceParamEvents(p.guiParams,block->out_events,[](clap_id,double){});
    const unsigned total=block->in_events&&block->in_events->size?block->in_events->size(block->in_events):0;unsigned event=0,at=0;float peak=0;
    while(at<block->frames_count){std::array<SampleNeonEvent,64> events{};unsigned count=0;
        if(at==0){Command c;while(p.commands.peek(c)){audioCommand(p,c,events,count);p.commands.pop();}}
        while(event<total){const auto* e=block->in_events->get(block->in_events,event);if(e&&e->time>at)break;++event;if(!e||e->space_id!=CLAP_CORE_EVENT_SPACE_ID)continue;
            if(e->type==CLAP_EVENT_PARAM_VALUE&&e->size>=sizeof(clap_event_param_value_t)){const auto& v=*reinterpret_cast<const clap_event_param_value_t*>(e);if(v.param_id)setValue(p,v.param_id-1,v.value);}
            else if(e->type==CLAP_EVENT_MIDI&&e->size>=sizeof(clap_event_midi_t)){const auto& m=*reinterpret_cast<const clap_event_midi_t*>(e);if(m.port_index==0)midi(p,m.data,events,count);}
            else if((e->type==CLAP_EVENT_NOTE_ON||e->type==CLAP_EVENT_NOTE_OFF||e->type==CLAP_EVENT_NOTE_CHOKE)&&e->size>=sizeof(clap_event_note_t)){
                const auto& n=*reinterpret_cast<const clap_event_note_t*>(e);if(n.port_index!=0&&n.port_index!=-1)continue;
                if(value(p,MidiChannel)!=0&&n.channel>=0&&n.channel!=int(value(p,MidiChannel))-1)continue;
                if(n.key==36||n.key==37)audioCommand(p,{e->type==CLAP_EVENT_NOTE_ON?CommandKind::Cue:CommandKind::Stop,static_cast<unsigned>(n.key-36)},events,count);}
        }
        for(unsigned d=0;d<2;++d)for(unsigned pad=0;pad<8;++pad)if(p.pendingPads[d][pad].active&&!p.pendingPads[d][pad].remaining)commitPad(p,d,pad,events,count);
        for(unsigned d=0;d<2;++d){auto& perf=p.performance[d];
            if(perf.window&&!perf.released&&!perf.remaining&&count<events.size()){
                auto& e=events[count++];e={};e.slot=d;e.kind=SampleNeonEventKind::Release;e.noteId=perf.note;perf.released=true;}}
        unsigned next=std::min(block->frames_count,at+64u);if(event<total){const auto* e=block->in_events->get(block->in_events,event);if(e&&e->time>at)next=std::min(next,e->time);}
        for(const auto& deck:p.pendingPads)for(const auto& pending:deck)if(pending.active)next=std::min(next,at+pending.remaining);
        for(const auto& perf:p.performance)if(perf.window&&!perf.released&&perf.remaining!=UINT64_MAX&&perf.remaining)next=std::min(next,at+static_cast<unsigned>(std::min<uint64_t>(64,perf.remaining)));
        const unsigned frames=next-at;
        for(unsigned d=0;d<2;++d){const unsigned method=static_cast<unsigned>(control(p,d,Playback));
            const bool manual=control(p,d,StackNavigation)!=0;
            const float target=performanceStackTarget(p,d);
            if(manual&&newest(p.performance[d].rideOrder)<0&&!p.performance[d].window&&(method==0||method==3||method==8)){
                const unsigned layerCount=doc->decks[d].stack.count;
                if(layerCount){unsigned chosen=static_cast<unsigned>(std::lround(target*(layerCount-1)));
                    if(!doc->decks[d].layers[chosen].asset){unsigned best=32;for(unsigned n=0;n<layerCount;++n)if(doc->decks[d].layers[n].asset
                        &&(best==32||std::abs(int(n)-int(chosen))<std::abs(int(best)-int(chosen))))best=n;if(best<32)chosen=best;}
                    if(control(p,d,Layer)!=chosen)setValue(p,param(d,Layer),chosen);}}
            const unsigned layer=static_cast<unsigned>(control(p,d,Layer));
            const auto* source=doc->decks[d].playbackBase(layer);const auto* asset=source?source->asset.get():nullptr;
            if(asset!=p.audioSource[d]){p.audioSource[d]=asset;p.engine.setSource(d,asset,source?source->wavesets.get():nullptr);}
            if(p.lastLayer[d]!=99&&layer!=p.lastLayer[d]){setValue(p,param(d,Start),doc->decks[d].layers[layer].start);setValue(p,param(d,End),doc->decks[d].layers[layer].end);}
            if((p.lastMethod[d]!=99&&method!=p.lastMethod[d])||(p.lastLayer[d]!=99&&layer!=p.lastLayer[d])){
                if(p.performance[d].window&&p.performance[d].layer!=layer)p.performance[d].window=false;
                const bool was=p.playing[d].load();p.engine.stop(d);
                const bool triggering=std::any_of(events.begin(),events.begin()+count,[d](const auto& e){return e.slot==d&&e.kind==SampleNeonEventKind::Trigger;});
                bool keyboardHeld=false;
                for(const auto& note:p.padNotes[d])if(note.id){keyboardHeld=true;
                    const bool queued=std::any_of(events.begin(),events.begin()+count,[&](const auto& e){return e.slot==d&&e.kind==SampleNeonEventKind::Trigger&&e.noteId==note.id;});
                    if(!queued&&count<events.size()){auto& e=events[count++];e={};e.slot=static_cast<uint8_t>(d);e.noteId=note.id;e.key=note.key;e.value=note.velocity;}}
                if(was&&!keyboardHeld&&!triggering&&count<events.size()){events[count]={};events[count].slot=static_cast<uint8_t>(d);events[count].noteId=d+1;++count;}}
            p.lastMethod[d]=method;p.lastLayer[d]=layer;
            const bool triggering=std::any_of(events.begin(),events.begin()+count,[d](const auto& e){return e.slot==d&&e.kind==SampleNeonEventKind::Trigger;});
            if((target!=p.lastStackTarget[d]||triggering)&&count<events.size()){
                const bool isolated=std::any_of(events.begin(),events.begin()+count,[d](const auto& e){return e.slot==d&&e.kind==SampleNeonEventKind::Trigger&&e.selectedSource;});
                const bool snap=std::any_of(events.begin(),events.begin()+count,[d](const auto& e){return e.slot==d&&e.kind==SampleNeonEventKind::Trigger&&e.snapStackPosition;});
                auto& e=events[count++];e={};e.slot=static_cast<uint8_t>(d);e.kind=SampleNeonEventKind::StackPosition;e.value=target;e.resumeNavigation=!isolated;
                e.snapStackPosition=snap;p.lastStackTarget[d]=target;}
            const float pos=control(p,d,Position);if(pos!=p.lastPosition[d]){p.engine.seek(d,pos);p.lastPosition[d]=pos;}
            if(p.touching[d].load()&&control(p,d,Slip)!=0){const auto* a=doc->decks[d].base();if(a)p.resume[d]=neonUnitPhase(p.resume[d]+frames/p.sampleRate.load()*control(p,d,Rate)/(a->frameCount()/a->sampleRate));}
            float pressure=0;for(unsigned i=0;i<8;++i)if(p.padHeld[d][i])pressure=std::max(pressure,std::max(p.padPressure[d][i],p.screenHeld[d][i]?control(p,d,ScreenPressure):0.f));
            if(pressure!=p.pressure[d]&&count<events.size()){auto& e=events[count++];e.slot=static_cast<uint8_t>(d);e.kind=SampleNeonEventKind::Pressure;e.value=pressure;p.pressure[d]=pressure;}
        }
        auto s=settings(p,*doc,frames);preparePlatters(p,s,frames);SampleDecksMix mix;mix.crossfade=value(p,Crossfade);mix.gainDecibels=value(p,Output);mix.curve=static_cast<DeckFadeCurve>(value(p,Curve));mix.stems=value(p,Stems)!=0;
        for(unsigned d=0;d<2;++d){mix.levels[d]=control(p,d,Level);mix.trimDb[d]=control(p,d,Gain);
            mix.lowDb[d]=control(p,d,Low);mix.midDb[d]=control(p,d,Mid);mix.highDb[d]=control(p,d,High);mix.filter[d]=control(p,d,Filter);}
        std::array<float*,32> audio{};for(unsigned ch=0;ch<32;++ch)audio[ch]=p.scratch[ch].data();
        p.engine.render(s,events.data(),count,mix,audio.data(),frames);
        auto& c=p.capture;unsigned state=c.state.load();
        if(state==1||state==2){const bool missing=c.input&&!inputConnected,changed=c.format!=unsigned(value(p,Format));
            if(c.stop.load()||changed||missing){if(missing||changed)c.notice.store(missing?1:2);c.state.store(3);}
            else {c.state.store(2);unsigned written=c.frames.load();const unsigned n=std::min(frames,c.capacity-written);
                for(unsigned i=0;i<n;++i){float pk=0;for(unsigned ch=0;ch<c.width;++ch){const float v=c.input?p.inputScratch[ch][at+i]:p.engine.deckAudio(c.from,ch)[i];c.audio[ch][written+i]=v;pk=std::max(pk,std::abs(v));}
                    const unsigned bin=std::min(511u,static_cast<unsigned>(uint64_t(written+i)*512/c.capacity));c.peaks[bin].store(std::max(c.peaks[bin].load(),pk));}
                c.frames.store(written+n);if(written+n==c.capacity)c.state.store(3);}
            if(c.state.load()==3&&p.host&&p.host->request_callback)p.host->request_callback(p.host);}
        const bool monitorInput=value(p,RecordSource)==2&&inputConnected;
        if(monitorInput)for(unsigned ch=0;ch<inputWidth;++ch)for(unsigned i=0;i<frames;++i)audio[ch][i]+=p.inputScratch[ch][at+i];
        DeckHeadphoneSettings hp;hp.output=static_cast<unsigned>(value(p,HeadphoneOutput));hp.format=static_cast<unsigned>(value(p,Format));
        hp.fold=static_cast<unsigned>(value(p,HeadphoneFold));hp.stems=mix.stems;hp.selected={value(p,HeadphoneA)!=0,value(p,HeadphoneB)!=0};
        hp.mix=value(p,HeadphoneMix);hp.gainDb=value(p,HeadphoneLevel);
        std::array<const float*,16> cueA{},cueB{};const unsigned width=sampleNeonBusWidth(s.outputLayout);
        for(unsigned ch=0;ch<width;++ch){cueA[ch]=p.engine.deckAudio(0,ch);cueB[ch]=p.engine.deckAudio(1,ch);}
        p.headphones.render(hp,width,cueA.data(),cueB.data(),audio.data(),frames);
        for(unsigned ch=0;ch<32;++ch)for(unsigned i=0;i<frames;++i){const float v=audio[ch][i];
            peak=std::max(peak,std::abs(v));if(out.data32)out.data32[ch][at+i]=v;else out.data64[ch][at+i]=v;}
        for(unsigned d=0;d<2;++d){p.playing[d].store(p.engine.active(d));const unsigned n=p.engine.cursorCount(d);p.cursor[d].store(n?p.engine.cursors(d)[n-1].sourcePositionNormalized:-1);
            if(p.performance[d].window){auto cursors=p.engine.cursors(d);for(unsigned i=0;i<n;++i)cursors[i].layer=p.performance[d].layer;
                p.playbackVisuals[d].publish(cursors,n,p.engine.motionVisual(d,s));}
            else p.playbackVisuals[d].publish(p.engine.cursors(d),n,p.engine.motionVisual(d,s));
            p.waveformLayer[d].store(p.engine.waveformLayer(d,s));p.scanPosition[d].store(static_cast<float>(p.engine.scanPosition(d,s)));
            if(n)p.soundingLayer[d].store(p.performance[d].window?p.performance[d].layer:p.engine.cursors(d)[n-1].layer);p.stackPosition[d].store(p.engine.stackPosition(d));p.pathPhase[d].store(p.engine.stackPhase(d,s));
            const unsigned method=static_cast<unsigned>(control(p,d,Playback));
            if(n&&(method==0||method==3||method==8)){const unsigned layer=p.soundingLayer[d].load();p.waveformLayer[d].store(layer);
                p.stackPosition[d].store(doc->decks[d].stack.count>1?float(layer)/(doc->decks[d].stack.count-1):0);}
            if(p.performance[d].window){const auto& window=p.performance[d];p.waveformLayer[d].store(window.layer);
                if(method==1||method==2||method==4){const bool selected=window.layer==static_cast<unsigned>(control(p,d,Layer));
                    const double first=selected?control(p,d,Start):doc->decks[d].layers[window.layer].start,last=selected?control(p,d,End):doc->decks[d].layers[window.layer].end;
                    p.scanPosition[d].store(static_cast<float>((window.start+(window.end-window.start)*p.scanPosition[d].load()-first)/std::max(1.e-7,last-first)));}}
            auto& perf=p.performance[d];if(perf.window&&!perf.released&&perf.remaining!=UINT64_MAX)perf.remaining-=std::min<uint64_t>(frames,perf.remaining);
            p.rollHistory[d].store(p.engine.roll(d).historyFrames());
            unsigned lights=0;if(perf.window&&perf.loop)lights|=1u<<perf.pad;
            for(unsigned i=0;i<8;++i)if(perf.fxOrder[i])lights|=1u<<(8+i);
            if(perf.rollPad>=0)lights|=1u<<(16+perf.rollPad);p.performanceLights[d].store(lights);}
        for(auto& deck:p.pendingPads)for(auto& pending:deck)if(pending.active)pending.remaining-=std::min(frames,pending.remaining);
        for(unsigned d=0;d<2;++d)for(unsigned n=0;n<8;++n){
            const PadFeedback padState{p.padHeld[d][n],p.padPulse[d][n]!=0,p.padOwnerKeyboard[d][n],p.padOwner[d][n],p.padOwnerBank[d][n],
                p.padVelocity[d][n],std::max(p.padPressure[d][n],p.screenHeld[d][n]?control(p,d,ScreenPressure):0.f)};
            p.padFeedback[d][n].store(padState.packed(),std::memory_order_relaxed);
            p.padPulse[d][n]-=std::min(frames,p.padPulse[d][n]);}
        if(p.beatValid)p.beat+=frames/p.sampleRate.load()*p.tempo/60.;at=next;
    }
    p.outputPeak.store(peak);out.constant_mask=0;feedback(p,block->out_events,block->frames_count);p.hazard.store(nullptr);
    p.callbackFrames+=block->frames_count;if(p.callbackFrames>=p.sampleRate.load()/20){p.callbackFrames=0;if(p.host&&p.host->request_callback)p.host->request_callback(p.host);}
    return CLAP_PROCESS_CONTINUE;
}

uint32_t paramCount(const clap_plugin_t* p){return static_cast<uint32_t>(self(p)->ids.size());}
bool paramInfo(const clap_plugin_t* plugin,uint32_t index,clap_param_info_t* out){auto& p=*self(plugin);if(!out||index>=p.ids.size())return false;const unsigned i=p.ids[index];const auto& d=p.defs[i];*out={};
    out->id=i+1;out->flags=CLAP_PARAM_IS_AUTOMATABLE|(d.integer?CLAP_PARAM_IS_STEPPED:0);out->min_value=d.lo;out->max_value=d.hi;out->default_value=d.initial;
    std::snprintf(out->name,sizeof(out->name),"%s",d.name.c_str());std::snprintf(out->module,sizeof(out->module),"%s",i<kDeckBase?"Mixer":(i-kDeckBase)/kDeckStride?"Deck B":"Deck A");return true;}
bool paramValue(const clap_plugin_t* p,clap_id id,double* out){if(!out||!id||id>kParamCount||self(p)->defs[id-1].name.empty())return false;*out=value(*self(p),id-1);return true;}
bool valueText(const clap_plugin_t* plugin,clap_id id,double v,char* out,uint32_t size){if(!out||!size||!id||id>kParamCount||!std::isfinite(v))return false;const auto& d=self(plugin)->defs[id-1];if(d.name.empty())return false;
    if(!d.choices.empty())std::snprintf(out,size,"%s",d.choices[static_cast<unsigned>(std::clamp(std::round(v),d.lo,d.hi))].c_str());else std::snprintf(out,size,d.integer?"%.0f":"%.3f",v);return true;}
bool textValue(const clap_plugin_t* plugin,clap_id id,const char* text,double* out){if(!out||!text||!id||id>kParamCount)return false;const auto& d=self(plugin)->defs[id-1];if(d.name.empty())return false;
    for(unsigned n=0;n<d.choices.size();++n)if(d.choices[n]==text){*out=n;return true;}
    char* end=nullptr;const double v=std::strtod(text,&end);if(end==text||*end||!std::isfinite(v))return false;*out=std::clamp(d.integer?std::round(v):v,d.lo,d.hi);return true;}
void flush(const clap_plugin_t* plugin,const clap_input_events_t* in,const clap_output_events_t* out){auto& p=*self(plugin);
    if(in&&in->size&&in->get)for(unsigned i=0;i<in->size(in);++i){const auto* e=in->get(in,i);if(e&&e->space_id==CLAP_CORE_EVENT_SPACE_ID&&e->type==CLAP_EVENT_PARAM_VALUE&&e->size>=sizeof(clap_event_param_value_t)){const auto& v=*reinterpret_cast<const clap_event_param_value_t*>(e);if(v.param_id)setValue(p,v.param_id-1,v.value);}}
    s3g::clap_gui::serviceParamEvents(p.guiParams,out,[](clap_id,double){});}
const clap_plugin_params_t paramsExtension{paramCount,paramInfo,paramValue,valueText,textValue,flush};
const clap_plugin_audio_ports_t audioPortsExtension{
    [](const clap_plugin_t*,bool)->uint32_t{return 1;},
    [](const clap_plugin_t*,uint32_t index,bool input,clap_audio_port_info_t* out){if(index||!out)return false;*out={};out->id=input?1:0;std::strcpy(out->name,input?"Track Input / Record":"Decks Mix / Stems");out->flags=CLAP_AUDIO_PORT_IS_MAIN|CLAP_AUDIO_PORT_SUPPORTS_64BITS;out->channel_count=32;out->in_place_pair=CLAP_INVALID_ID;return true;}};
const clap_plugin_note_ports_t notePortsExtension{
    [](const clap_plugin_t*,bool)->uint32_t{return 1;},
    [](const clap_plugin_t*,uint32_t index,bool input,clap_note_port_info_t* out){if(index||!out)return false;*out={};out->id=0;out->supported_dialects=CLAP_NOTE_DIALECT_MIDI|(input?CLAP_NOTE_DIALECT_CLAP:0);out->preferred_dialect=CLAP_NOTE_DIALECT_MIDI;std::strcpy(out->name,input?"Beatpad 2 / Deck Commands":"Beatpad 2 LED Feedback");return true;}};
const clap_plugin_timer_support_t timerExtension{[](const clap_plugin_t* plugin,clap_id id){auto& p=*self(plugin);if(id==p.timerId)service(p);}};
#if defined(S3G_ENABLE_VSTGUI_CANVAS_GUI)
#include "s3g_sample_decks_vstgui.inc"
#include "../common/s3g_clap_canvas_gui.inc"
#endif
const void* extension(const clap_plugin_t*,const char* id){if(!id)return nullptr;
    if(!std::strcmp(id,CLAP_EXT_AUDIO_PORTS))return &audioPortsExtension;if(!std::strcmp(id,CLAP_EXT_NOTE_PORTS))return &notePortsExtension;
    if(!std::strcmp(id,CLAP_EXT_PARAMS))return &paramsExtension;if(!std::strcmp(id,CLAP_EXT_STATE))return &stateExtension;
    if(!std::strcmp(id,CLAP_EXT_TIMER_SUPPORT))return &timerExtension;
#if defined(S3G_ENABLE_VSTGUI_CANVAS_GUI)
    if(!std::strcmp(id,CLAP_EXT_GUI))return &portableGui;
#endif
    return nullptr;}
void destroy(const clap_plugin_t* plugin){auto* p=self(plugin);
    if(p->hostTimer&&p->timerId!=CLAP_INVALID_ID)p->hostTimer->unregister_timer(p->host,p->timerId);
#if defined(S3G_ENABLE_VSTGUI_CANVAS_GUI)
    destroyPortableGui(*p);
#endif
    if(p->worker.valid())p->worker.wait();if(p->mediaWorker.valid())p->mediaWorker.wait();delete p;}
const char* const features[]{CLAP_PLUGIN_FEATURE_INSTRUMENT,CLAP_PLUGIN_FEATURE_SAMPLER,CLAP_PLUGIN_FEATURE_SURROUND,nullptr};
const clap_plugin_descriptor_t descriptor{CLAP_VERSION_INIT,"org.s3g.s3g-dsp.sample-decks","s3g Sample Decks 32","s3g","https://github.com/s3g/s3g-dsp","","","0.1.0",
    "Two multichannel sample stacks, nine playback methods and reversible deck-to-deck resampling.",features};
const clap_plugin_t* create(const clap_plugin_factory_t*,const clap_host_t* host,const char* id){if(!host||!clap_version_is_compatible(host->clap_version)||!id||std::strcmp(id,descriptor.id))return nullptr;
    try{auto* p=new Plugin;p->host=host;p->plugin={&descriptor,p,pluginInit,destroy,activate,deactivate,startProcessing,stopProcessing,reset,process,extension,
        [](const clap_plugin_t* plugin){service(*self(plugin));}};return &p->plugin;}catch(...){return nullptr;}}
const clap_plugin_factory_t factory{[](const clap_plugin_factory_t*)->uint32_t{return 1;},[](const clap_plugin_factory_t*,uint32_t i)->const clap_plugin_descriptor_t*{return i?nullptr:&descriptor;},create};
} // namespace
extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry{CLAP_VERSION_INIT,[](const char*){return true;},[]{},[](const char* id)->const void*{return id&&!std::strcmp(id,CLAP_PLUGIN_FACTORY_ID)?&factory:nullptr;}};
