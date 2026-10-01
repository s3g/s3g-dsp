// White-box workflow tests plus a black-box pass over the exact CLAP bundle.
// The test-only inclusion has no GUI; the installable module is always tested
// separately with the shared native/VSTGUI editor harness.
#include "../plugins/clap_sample_decks/s3g_sample_decks_clap.cpp"
#include <dlfcn.h>
#include <iostream>
#include <thread>
#include <fstream>
#include "realtime_alloc_probe_api.h"
#include "../plugins/common/s3g_audio_file_export.h"
namespace {
unsigned checks=0;
void check(bool result,const char* why){++checks;if(!result)throw std::runtime_error(why);}
struct Bytes {
    std::vector<uint8_t> data;size_t at=0;
    clap_ostream_t out{this,[](const clap_ostream_t* s,const void* bytes,uint64_t n)->int64_t{
        auto& b=*static_cast<Bytes*>(s->ctx);auto* first=static_cast<const uint8_t*>(bytes);b.data.insert(b.data.end(),first,first+n);return n;}};
    clap_istream_t in{this,[](const clap_istream_t* s,void* bytes,uint64_t n)->int64_t{
        auto& b=*static_cast<Bytes*>(s->ctx);n=std::min<uint64_t>(n,b.data.size()-b.at);std::memcpy(bytes,b.data.data()+b.at,n);b.at+=n;return n;}};
};
void versionSeven(const Bytes& current,Bytes& old){old.data=current.data;old.at=0;
    if(old.data[4]>=8){old.data.resize(old.data.size()-16*sizeof(float));old.data[4]=7;}}
clap_host_t testHost{CLAP_VERSION_INIT,nullptr,"Sample Decks Test","s3g","","1",
    [](const clap_host_t*,const char*)->const void*{return nullptr;},[](const clap_host_t*){},[](const clap_host_t*){},[](const clap_host_t*){}};
std::shared_ptr<const SampleAsset> tone(unsigned channels,float amplitude=.2f){
    auto a=std::make_shared<SampleAsset>();a->sampleRate=48000;a->channelCount=channels;
    for(unsigned ch=0;ch<channels;++ch){a->channels[ch].resize(48000);
        for(unsigned n=0;n<48000;++n)a->channels[ch][n]=amplitude*std::sin(n*.061)*(ch%2?-1:1);}
    return a;
}
#include "sample_decks_graphics_checks.inc"
void loadFixture(Plugin& p,unsigned channels=2){auto doc=std::make_shared<Document>();
    for(unsigned d=0;d<2;++d)for(unsigned l=0;l<3;++l){auto& layer=doc->decks[d].layers[l];layer.asset=tone(channels,.2f+(.1f*l));layer.name="TEST "+std::to_string(l+1);analyzeLayer(layer);}
    publish(p,std::move(doc),3,false);setValue(p,Storage,2);
}
struct Audio {
    static constexpr unsigned frames=256;
    std::array<std::array<float,frames>,32> pcm{};std::array<float*,32> ptr{};
    clap_audio_buffer_t bus{};clap_process_t block{};
    std::array<clap_event_midi_t,32> messages{};unsigned messageCount=0;
    clap_input_events_t in{this,[](const clap_input_events_t* e)->uint32_t{return static_cast<Audio*>(e->ctx)->messageCount;},
        [](const clap_input_events_t* e,uint32_t i)->const clap_event_header_t*{auto& a=*static_cast<Audio*>(e->ctx);return i<a.messageCount?&a.messages[i].header:nullptr;}};
    std::array<clap_event_midi_t,512> feedback{};unsigned feedbackCount=0,feedbackLimit=512;
    clap_output_events_t out{this,[](const clap_output_events_t* out,const clap_event_header_t* e){
        auto& a=*static_cast<Audio*>(out->ctx);if(e->type!=CLAP_EVENT_MIDI)return true;
        if(a.feedbackCount>=a.feedbackLimit)return false;
        a.feedback[a.feedbackCount++]=*reinterpret_cast<const clap_event_midi_t*>(e);return true;}};
    Audio(){for(unsigned c=0;c<32;++c)ptr[c]=pcm[c].data();bus.data32=ptr.data();bus.channel_count=32;block.audio_outputs=&bus;block.audio_outputs_count=1;block.frames_count=frames;block.in_events=&in;block.out_events=&out;}
    void midi(unsigned status,unsigned key,unsigned velocity,unsigned at=0){auto& m=messages[messageCount++];m={};m.header={sizeof(m),at,CLAP_CORE_EVENT_SPACE_ID,CLAP_EVENT_MIDI,0};m.data[0]=status;m.data[1]=key;m.data[2]=velocity;}
    double run(const clap_plugin_t* p,unsigned n=1){double energy=0;
        using Begin=void(*)();using Read=int(*)(s3g_rt_alloc_probe_counts*,size_t);
        static auto begin=reinterpret_cast<Begin>(dlsym(RTLD_DEFAULT,"s3g_rt_alloc_probe_begin"));
        static auto end=reinterpret_cast<Begin>(dlsym(RTLD_DEFAULT,"s3g_rt_alloc_probe_end"));
        static auto read=reinterpret_cast<Read>(dlsym(RTLD_DEFAULT,"s3g_rt_alloc_probe_read"));
        for(unsigned b=0;b<n;++b){feedbackCount=0;if(begin)begin();const auto status=p->process(p,&block);if(end)end();
            check(status!=CLAP_PROCESS_ERROR,"process succeeds");
            if(read){s3g_rt_alloc_probe_counts counts{};check(read(&counts,sizeof(counts))!=0,"allocation probe");check(!counts.malloc_calls&&!counts.calloc_calls&&!counts.realloc_calls&&!counts.free_calls&&!counts.posix_memalign_calls&&!counts.aligned_alloc_calls,"audio thread allocates/frees nothing");}
            for(const auto& ch:pcm)for(float v:ch){check(std::isfinite(v),"finite output");energy+=double(v)*v;}
            messageCount=0;}
        return energy;
    }
};
#include "sample_decks_media_checks.inc"
#include "sample_decks_transition_checks.inc"
#include "sample_decks_transport_checks.inc"
void waitWork(Plugin& p){for(unsigned i=0;i<3000&&p.busy;++i){service(p);std::this_thread::sleep_for(std::chrono::milliseconds(1));}check(!p.busy,"background work completes");}
void platterModel(){
    for(double rate:{44100.,48000.,96000.}){
        DeckPlatter platter;platter.prepare(rate);check(platter.next()==1,"untouched platter is exactly neutral");
        platter.touch(true);float speed=1;
        for(unsigned n=0;n<unsigned(rate*.15);++n)speed=platter.next();
        check(std::abs(speed)<1.e-6,"stationary touched platter brakes to zero");
        double distance=0;float previous=speed;
        platter.move(5,false);
        for(unsigned n=0;n<unsigned(rate*.25);++n){speed=platter.next();distance+=speed/rate;
            check(std::abs(speed-previous)<.02,"jog rate never steps at a MIDI packet");previous=speed;}
        check(std::abs(distance-.020)<1.e-5,"tick travel is source seconds and sample-rate independent");
        platter.move(-5,false);double reverse=0;
        for(unsigned n=0;n<unsigned(rate*.25);++n)reverse+=platter.next()/rate;
        check(std::abs(reverse+.020)<1.e-5,"reverse travel preserves signed distance");
        platter.move(-30,false);for(unsigned n=0;n<unsigned(rate*.006);++n)speed=platter.next();
        const float beforeRelease=speed;
        platter.touch(false);speed=platter.next();
        check(std::abs(speed-beforeRelease)<.01,"releasing a moving platter is continuous");
        platter.touch(true);for(unsigned n=0;n<unsigned(rate*.4);++n)platter.next();
        platter.touch(false);check(platter.next()<.01,"motor release does not jump to full speed");
        for(unsigned n=0;n<unsigned(rate*.8);++n)speed=platter.next();
        check(std::abs(speed-1)<1.e-5,"motor settles to normal speed");
        platter.move(-63,true);for(unsigned n=0;n<unsigned(rate*.2);++n){speed=platter.next();check(speed>=.64&&speed<=1.36,"rim nudge remains bounded and forward");}
    }
}
void jogBlockContinuity(Plugin& p,const clap_plugin_t* plugin){
    auto render=[&](unsigned blockSize){reset(plugin);setValue(p,param(0,Playback),0);setValue(p,param(0,Position),0);
        setValue(p,param(0,Layer),0);setValue(p,param(0,Start),0);setValue(p,param(0,End),1);setValue(p,Crossfade,0);
        Audio a;a.run(plugin);std::vector<float> samples;
        struct Gesture{unsigned at,status,key,value;};
        const Gesture gestures[]{{0,0x90,0x60,127},{1024,0x90,0x63,127},{2048,0xb0,0x43,69},
            {2304,0xb0,0x43,59},{3072,0xb0,0x43,67},{4096,0xb0,0x43,62},{6144,0x80,0x63,0}};
        for(unsigned at=0;at<12000;){const unsigned size=std::min(blockSize,12000-at);a.block.frames_count=size;
            for(const auto& g:gestures)if(g.at>=at&&g.at<at+size)a.midi(g.status,g.key,g.value,g.at-at);
            a.run(plugin);samples.insert(samples.end(),a.pcm[0].begin(),a.pcm[0].begin()+size);at+=size;}
        float maximum=0;for(unsigned n=1;n<samples.size();++n)maximum=std::max(maximum,std::abs(samples[n]-samples[n-1]));
        check(maximum<.025f,"jog, reversal and release have no discontinuity spikes on a sine fixture");
        return samples;};
    const auto reference=render(256);
    for(unsigned blockSize:{64u,127u}){const auto candidate=render(blockSize);float error=0;
        for(unsigned n=0;n<reference.size();++n)error=std::max(error,std::abs(reference[n]-candidate[n]));
        check(error<1.e-5f,"sample-accurate jog is independent of host block size");}
}
void jogWorkflow(Plugin& p,const clap_plugin_t* plugin,Audio& a){
    for(unsigned method:{0u,6u}){
        reset(plugin);setValue(p,param(0,Playback),method);setValue(p,param(0,SourceMode),1);
        setValue(p,param(0,Layer),0);setValue(p,param(0,Position),0);setValue(p,param(0,Start),0);setValue(p,param(0,End),1);
        setValue(p,param(0,Slip),0);setValue(p,Crossfade,0);a.run(plugin);
        a.midi(0x90,0x60,127);a.run(plugin,30);
        a.midi(0x90,0x63,127);a.run(plugin,50);
        check(a.run(plugin)<1.e-9,"stationary touch is silent, without DC");
        const double stopped=currentPosition(p,0);a.run(plugin,8);
        check(std::abs(currentPosition(p,0)-stopped)<1.e-5,"held platter stays still between MIDI packets");
        const float positionParam=control(p,0,Position);
        a.midi(0xb0,0x43,69,127);double energy=a.run(plugin,30);
        check(energy>.001&&currentPosition(p,0)>stopped+.01,"forward jog scrubs audibly without retrigger");
        const double forward=currentPosition(p,0);
        a.midi(0xb0,0x43,59);a.run(plugin,40);
        check(currentPosition(p,0)<forward-.01,"reverse jog moves the read head backwards");
        check(control(p,0,Position)==positionParam,"jog does not write source-position automation or seek");
        check(p.playing[0].load(),"scrub preserves the playing voice");
        a.midi(0x80,0x63,0);a.run(plugin,120);const double resumed=currentPosition(p,0);a.run(plugin,4);
        check(std::abs(neonUnitPhase(currentPosition(p,0)-resumed)-4.*256/48000)<.003,"release recovers normal playback speed");
        a.midi(0xb0,0x43,65);a.run(plugin,30);check(a.run(plugin)>0,"rim nudge preserves audio");
    }
    // Every technique accepts the same gesture without a voice reset. Their
    // granular/spectral/slice/cycle textures intentionally remain distinct.
    for(unsigned method=1;method<9;++method){reset(plugin);setValue(p,param(0,Playback),method);a.run(plugin);
        a.midi(0x90,0x60,127);a.run(plugin,12);a.midi(0x90,0x63,127);a.midi(0xb0,0x43,67,80);a.run(plugin,8);
        check(p.playing[0].load(),"technique survives platter gesture");
        a.midi(0x80,0x63,0);check(a.run(plugin,40)>0,"technique survives release");}
    reset(plugin);setValue(p,param(0,Playback),0);a.run(plugin);
}
void stackJogWorkflow(Plugin& p,const clap_plugin_t* plugin,Audio& a){
    for(unsigned method=0;method<9;++method){reset(plugin);setValue(p,param(0,Playback),method);setValue(p,param(0,Layer),0);
        setValue(p,param(0,StackNavigation),0);setValue(p,param(0,SourceMode),1);setValue(p,param(0,Position),0);a.run(plugin);
        a.midi(0x90,0x60,127);a.run(plugin,20);
        a.midi(0x90,0x1c,127);a.midi(0x90,0x23,127);a.midi(0xb0,0x03,65);a.run(plugin,16);
        check(control(p,0,StackNavigation)==1,"Shift jog engages explicit manual stack mode");
        check(control(p,0,StackPosition)>0,"Shift jog moves stack position");
        check(!p.touching[0].load(),"shifted platter touch does not brake time navigation");
        check(control(p,0,Position)==0,"Shift jog leaves time position parameter untouched");
        if(method==0||method==3||method==8)check(control(p,0,Layer)==1,"discrete methods select the next layer");
        else check(p.stackPosition[0].load()>0,"continuous methods audibly follow manual stack position");
        a.midi(0x80,0x1c,0);a.midi(0x80,0x23,0);a.run(plugin);
        check(control(p,0,StackNavigation)==1,"manual stack holds its position after modifier release");
        setValue(p,param(0,StackNavigation),0);a.run(plugin);check(p.lastStackTarget[0]==-1,"Follow Source releases the override");
        NeonVisualSnapshot visual;check(p.playbackVisuals[0].read(visual)&&visual.count>0,"each method publishes its real voice windows");
        for(unsigned n=0;n<visual.count;++n)check(visual.cursors[n].sourceAsset&&std::isfinite(visual.cursors[n].sourcePositionNormalized),"telemetry uses real finite source heads");
    }
    reset(plugin);setValue(p,param(0,Playback),0);setValue(p,param(0,Layer),0);setValue(p,param(0,StackNavigation),0);a.run(plugin);
    setValue(p,param(0,PadMode),6);const double amount=control(p,0,Amount);
    a.midi(0xb2,0x40,65);a.run(plugin);check(control(p,0,StackNavigation)==1&&control(p,0,Amount)==amount,"shifted Jog FX wheel navigates stack instead of changing FX");
    setValue(p,param(0,PadMode),4);setValue(p,param(0,StackNavigation),0);
}
void keyboardWorkflow(Plugin& p,const clap_plugin_t* plugin,Audio& a){
    setValue(p,Crossfade,0);setValue(p,param(0,SamplerPads),1);
    setValue(p,param(0,PadBank),0);setValue(p,param(0,Layer),0);
    setValue(p,param(0,KeyboardVoices),8);
    for(unsigned method=0;method<9;++method){reset(plugin);setValue(p,param(0,Playback),method);setValue(p,param(0,SourceMode),1);a.run(plugin);
        a.midi(0x92,0x20,127);a.midi(0xb2,0x20,42,1);
        a.midi(0x92,0x24,127,32);a.midi(0x92,0x27,127,64);check(a.run(plugin,12)>0,"keyboard chord sounds in each method");
        check(p.engine.noteCount(0)==3,"three independent keyboard gestures for every playback method");
        check(control(p,0,Layer)==0&&control(p,0,SourceMode)==1,"keyboard does not change layer/source");
        check(p.padNotes[0][0].key==60&&p.padNotes[0][4].key==64&&p.padNotes[0][7].key==67,"chromatic triad");
        check(std::abs(p.padNotes[0][0].velocity-42.f/127)<1.e-6,"keyboard velocity pairing");
        if(method==0){const auto positions=[&]{std::array<double,3> at{};
            for(unsigned n=0;n<p.engine.cursorCount(0);++n){const auto& c=p.engine.cursors(0)[n];
                if(c.key==60)at[0]=c.sourcePositionNormalized;else if(c.key==64)at[1]=c.sourcePositionNormalized;else if(c.key==67)at[2]=c.sourcePositionNormalized;}return at;};
            const auto before=positions();a.run(plugin,4);const auto after=positions();const double root=after[0]-before[0];
            check(root>0&&std::abs((after[1]-before[1])/root-std::pow(2.,4./12))<.001
                &&std::abs((after[2]-before[2])/root-std::pow(2.,7./12))<.001,"keyboard pitches actually change the sample read rates");}
        const auto held=p.padNotes[0][4].id;
        setValue(p,param(0,PadBank),1);a.midi(0x82,0x20,0);a.run(plugin,60);
        check(p.engine.noteCount(0)==2&&p.padNotes[0][4].id==held,"bank switch releases original pitch only");
        a.midi(0x92,0x21,127);a.run(plugin,3);check(p.padNotes[0][1].key==69,"bank selects next eight pitches");
        a.midi(0x90,0x4a,127);a.midi(0x82,0x24,0);a.run(plugin,60);
        check(p.engine.noteCount(0)==2,"page switch preserves release ownership");
        a.midi(0x82,0x27,0);a.midi(0x82,0x21,0);a.run(plugin,100);
        check(!p.engine.active(0)&&p.engine.noteCount(0)==0,"keyboard releases leave no stuck voices");
        setValue(p,param(0,PadBank),0);
    }
    reset(plugin);setValue(p,param(0,Playback),0);setValue(p,param(0,KeyboardVoices),2);a.run(plugin);
    a.midi(0x92,0x20,127);a.midi(0x92,0x21,127);a.midi(0x92,0x22,127);a.run(plugin,3);
    check(p.engine.noteCount(0)==2,"bounded keyboard voice stealing");
    a.midi(0x82,0x20,0);a.run(plugin,60);check(p.engine.noteCount(0)==2,"stolen key release does not stop replacement voice");
    setValue(p,param(0,Layer),1);a.run(plugin,3);check(p.engine.noteCount(0)==2,"source change revoices held pitches within limit");
    setValue(p,param(1,SamplerPads),1);a.midi(0x93,0x20,127);a.midi(0x93,0x24,127);a.run(plugin,3);
    check(p.engine.noteCount(0)==2&&p.engine.noteCount(1)==2,"two independent keyboard decks");
    a.midi(0xb0,123,0);a.run(plugin,3);check(!p.engine.active(0)&&!p.engine.active(1),"panic stops all keyboard voices");
    // Fixed-pitch transport and layer launching remain mono when keyboard is off.
    for(unsigned d=0;d<2;++d){setValue(p,param(d,SamplerPads),0);setValue(p,param(d,KeyboardVoices),8);}
    setValue(p,param(0,Layer),0);a.midi(0x92,0x20,127);a.run(plugin,3);
    a.midi(0x92,0x21,127);a.run(plugin,3);check(p.engine.noteCount(0)==1&&control(p,0,Layer)==1,"ordinary layer launch stays mono");
    reset(plugin);setValue(p,param(0,Layer),0);a.run(plugin);
}
void ledWorkflow(Plugin& p,const clap_plugin_t* plugin,Audio& a){
    setValue(p,Controller,1);setValue(p,param(0,PadBank),0);setValue(p,param(0,Layer),0);
    p.ledInit=false;p.refreshLeds.store(false);a.run(plugin);
    check(a.feedbackCount==270,"all pad pages and both six-message jog rings initialized");
    for(unsigned d=0;d<2;++d)for(unsigned key=0;key<128;++key){const auto& m=a.feedback[d*128+key];
        check(m.data[0]==0x92+d&&m.data[1]==key&&m.port_index==0,"manufacturer pad LED addresses");
        check(m.data[2]==p.padLeds[d][key],"cache matches successfully sent LED");}
    check(p.padLeds[0][0x20]==35&&p.padLeds[0][0x60]==35&&p.padLeds[0][0x23]==0,"selected, Shift and empty-layer LEDs");
    a.run(plugin);check(a.feedbackCount==0,"no blind per-block MIDI feedback");
    // Roll-pad availability changes as its history fills, even with silence.
    // Let those genuine state changes settle before testing idle feedback.
    a.run(plugin,static_cast<unsigned>(std::ceil(p.sampleRate.load()*3/a.block.frames_count)));
    for(unsigned frame=0;frame<unsigned(p.sampleRate.load()*6);frame+=a.block.frames_count){
        a.run(plugin);check(a.feedbackCount==0,"unchanged LEDs stay silent beyond the former periodic refresh interval");}
    p.refreshLeds.store(true);a.run(plugin);check(a.feedbackCount==270,"Refresh LEDs explicitly resends pads and jog rings");
    a.run(plugin);check(a.feedbackCount==0,"manual refresh returns to change-only feedback");
    a.midi(0x92,0x20,127);a.run(plugin,12);check(p.padLeds[0][0x20]==127&&p.padLeds[0][0x60]==127,"held pad lit in both shift states");
    a.midi(0x82,0x20,0);a.run(plugin,12);check(p.padLeds[0][0x20]==35,"release returns loaded LED");
    a.feedbackLimit=1;p.refreshLeds.store(true);a.run(plugin);check(a.feedbackCount==1&&p.padLeds[0][1]==-1,"failed output push stays dirty");
    a.feedbackLimit=512;p.ledTick=4800;a.run(plugin);check(a.feedbackCount==269,"unsent pad and ring LEDs retry including off values");
    a.block.out_events=nullptr;a.run(plugin);check(!p.ledInit,"missing output invalidates LED initialization");
    a.block.out_events=&a.out;a.run(plugin);check(a.feedbackCount==270,"restoring host event output primes all LEDs");
    setValue(p,Controller,0);a.run(plugin);check(!p.ledInit&&a.feedbackCount==0,"Notes profile has no controller LED feedback");
    setValue(p,Controller,1);a.run(plugin);check(a.feedbackCount==270,"enabling Beatpad profile primes all LEDs");
    reset(plugin);a.run(plugin);
}
void controllerIndependentWorkflow(Plugin& p,const clap_plugin_t* plugin,Audio& a){
    reset(plugin);setValue(p,Controller,0);setValue(p,MidiChannel,2);setValue(p,param(0,Playback),0);
    setValue(p,param(0,PadMode),4);setValue(p,param(0,SamplerPads),1);setValue(p,param(0,PadBank),0);
    setValue(p,param(0,ScreenVelocity),.42);setValue(p,param(0,ScreenPressure),.63);setValue(p,param(0,ScreenLatch),1);a.run(plugin);
    command(p,{CommandKind::ScreenPad,0,0,1});command(p,{CommandKind::ScreenPad,0,0,0});
    command(p,{CommandKind::ScreenPad,0,4,1});command(p,{CommandKind::ScreenPad,0,4,0});a.run(plugin,3);
    check(p.engine.noteCount(0)==2&&p.screenHeld[0][0]&&p.screenHeld[0][4],"mouse latch makes a chord without a controller");
    check(std::abs(p.padNotes[0][0].velocity-.42)<1.e-6&&std::abs(p.pressure[0]-.63)<1.e-6,"screen velocity and pressure reach the shared engine");
    auto state=PadFeedback::unpack(p.padFeedback[0][0].load());
    check(state.held&&state.pulse&&state.matches(4,0,true)&&std::abs(state.velocity-.42)<.01&&std::abs(state.pressure-.63)<.01,"semantic feedback publishes mouse pressure/velocity");
    setValue(p,param(0,PadBank),1);check(!state.matches(4,1,true)&&!state.matches(4,0,false)&&!state.matches(3,0,false),"held feedback does not leak into another bank or role");
    command(p,{CommandKind::ScreenPad,0,0,-1});command(p,{CommandKind::ScreenPad,0,4,-1});a.run(plugin,60);
    check(!p.engine.active(0),"mouse releases original latched pitches after a bank change");
    a.midi(0x90,36,127);a.run(plugin);check(!p.engine.active(0),"generic notes adapter respects channel");
    a.midi(0x91,36,127);a.run(plugin);check(p.engine.active(0),"generic notes adapter starts the same deck engine");
    a.midi(0xb9,123,0);a.run(plugin);check(!p.engine.active(0)&&!PadFeedback::unpack(p.padFeedback[0][0].load()).held,"panic is profile/channel independent");
    setValue(p,Controller,1);setValue(p,MidiChannel,0);setValue(p,param(0,PadBank),0);setValue(p,param(0,ScreenLatch),0);
    a.midi(0x92,0x20,80);a.midi(0x82,0x20,0,1);a.run(plugin);
    state=PadFeedback::unpack(p.padFeedback[0][0].load());check(!state.held&&state.pulse&&state.matches(4,0,true),"sub-block hardware tap remains visible");
    a.run(plugin,30);check(!PadFeedback::unpack(p.padFeedback[0][0].load()).pulse,"brief feedback expires");
    a.midi(0x92,0x21,127);a.midi(0xb2,0x21,50,1);a.midi(0xa2,0x21,73,120);a.run(plugin);
    state=PadFeedback::unpack(p.padFeedback[0][1].load());check(state.held&&std::abs(state.velocity-50.f/127)<.001&&std::abs(state.pressure-73.f/127)<.001,"hardware telemetry preserves velocity and pressure");
    reset(plugin);check(p.padFeedback[0][1].load()==0,"reset clears published pad feedback immediately");
    setValue(p,param(0,SamplerPads),0);setValue(p,param(0,ScreenVelocity),1);setValue(p,param(0,ScreenPressure),0);a.run(plugin);
}
#include "sample_decks_pad_checks.inc"
#include "sample_decks_cue_checks.inc"
#include "sample_decks_stack_cue_checks.inc"
#include "sample_decks_headphone_checks.inc"
#include "sample_decks_shift_checks.inc"
#include "sample_decks_capture_format_checks.inc"
void sliceBoundaryWorkflow(){
    for(unsigned channels:{2u,4u,16u}){LayerAudio layer;layer.asset=tone(channels);layer.start=.23;layer.end=.71;
        auto slices=equalSampleNeonSliceLayout(1);check(addSampleNeonSliceMarker(slices,.37),"manual marker insertion");
        const double first=layer.start*48000,span=(layer.end-layer.start)*48000;
        const auto target=static_cast<unsigned>(first+span*.37);
        const auto expected=nearestSafeSampleBoundary(*layer.asset,target,target-240,target+240);
        snapDeckSliceMarker(layer,slices,1);
        check(std::abs(first+span*slices.boundaries[1]-expected)<1.e-7,"zero-cross snap is relative to trim and common to every channel");
        check(moveSampleNeonSliceMarker(slices,1,.8)&&slices.boundaries[1]==.8,"zero-cross override permits exact manual position");
        check(!removeDeckSliceMarker(slices,0)&&!removeDeckSliceMarker(slices,2),"slice endpoints cannot be removed");
        check(removeDeckSliceMarker(slices,1)&&slices.sliceCount==1&&slices.valid(),"remove returns a valid full-window slice");
        slices=equalSampleNeonSliceLayout(32);check(!addSampleNeonSliceMarker(slices,.01)&&slices.valid(),"manual slices obey 32-slice limit");
        moveSampleNeonSliceMarker(slices,1,1);snapDeckSliceMarker(layer,slices,1);check(slices.valid(),"drag cannot cross neighboring markers");
    }
}
void trackInputWorkflow(){
    const auto* plugin=create(&factory,&testHost,descriptor.id);check(plugin&&plugin->init(plugin),"create input recorder");auto& p=*self(plugin);
    check(plugin->activate(plugin,48000,1,256)&&plugin->start_processing(plugin),"input recorder activate");Audio a;
    std::array<std::array<float,256>,32> in{};std::array<float*,32> ptr{};
    std::array<std::array<double,256>,32> doubles{};std::array<double*,32> ptr64{};
    for(unsigned ch=0;ch<32;++ch){ptr[ch]=in[ch].data();ptr64[ch]=doubles[ch].data();
        for(unsigned n=0;n<256;++n)doubles[ch][n]=in[ch][n]=float(ch+1)*.001f+float(n)*.00001f;}
    clap_audio_buffer_t input{};input.data32=ptr.data();input.channel_count=32;
    a.block.audio_inputs=&input;a.block.audio_inputs_count=1;
    const auto finish=[&]{startRecord(p,0);a.run(plugin);service(p);waitWork(p);};
    setValue(p,RecordSource,1);setValue(p,RecordInputGroup,2);a.run(plugin);
    startRecord(p,0);check(p.capture.state.load()==1&&p.capture.to==0&&p.capture.input,"input can arm into an empty A deck");
    check(a.run(plugin,3)==0,"input capture is silent by default");finish();
    auto take=p.document->decks[0].layers[0].asset;
    check(take&&take->channelCount==2&&take->frameCount()==768,"input take saved to A, not B");
    for(unsigned ch=0;ch<2;++ch)for(unsigned n=0;n<768;++n)check(take->channels[ch][n]==in[ch+4][n%256],"selected stereo input pins recorded exactly");
    check(!p.document->decks[1].base(),"recording leaves other deck untouched");
    history(p,false);check(!p.document->decks[0].base(),"input take undo");history(p,true);check(p.document->decks[0].layers[0].asset==take,"input take redo retains PCM");
    setValue(p,RecordSource,2);setValue(p,RecordInputGroup,0);a.run(plugin);
    for(unsigned n=0;n<256;++n)check(a.pcm[0][n]==in[0][n]&&a.pcm[1][n]==in[1][n]&&a.pcm[2][n]==0,"optional thru adds selected input only to main bus");
    setValue(p,RecordSource,1);input.data32=nullptr;input.data64=ptr64.data();
    doubles[0][7]=std::numeric_limits<double>::quiet_NaN();doubles[1][8]=std::numeric_limits<double>::infinity();
    startRecord(p,1);a.run(plugin);finish();take=p.document->decks[1].layers[0].asset;
    check(take&&take->channels[0][7]==0&&take->channels[1][8]==0&&take->channels[0][6]==float(doubles[0][6]),"64-bit input sanitizes non-finite samples");
    // In-place host buffers must be copied before any channel is overwritten.
    input.data32=a.ptr.data();input.data64=nullptr;
    for(unsigned ch=0;ch<32;++ch)a.pcm[ch]=in[ch];
    startRecord(p,0);a.run(plugin);finish();take=p.document->decks[0].layers[1].asset;
    check(take&&take->channels[0][255]==in[0][255]&&take->channels[1][255]==in[1][255],"input/output aliasing does not erase the take");
    a.block.audio_inputs=nullptr;startRecord(p,0);a.run(plugin);service(p);
    check(!p.capture.state.load()&&p.document->decks[0].firstEmpty()==2&&p.message.find("NO TRACK INPUT")!=std::string::npos,"missing input rejects an empty take");
    a.block.audio_inputs=&input;input.data32=ptr.data();ptr[1]=nullptr;startRecord(p,0);a.run(plugin);service(p);
    check(!p.capture.state.load()&&p.document->decks[0].firstEmpty()==2,"partial input field is rejected");ptr[1]=in[1].data();
    startRecord(p,0);a.run(plugin);a.block.audio_inputs=nullptr;a.run(plugin);service(p);waitWork(p);
    check(p.document->decks[0].layers[2].asset->frameCount()==256&&p.message.find("INPUT LOST")!=std::string::npos,"input loss preserves completed audio without appending silence");
    for(unsigned format=0;format<=5;++format){
        auto empty=std::make_shared<Document>();publish(p,empty,3,false);setValue(p,param(1,Layer),0);setValue(p,Format,format);setValue(p,RecordInputGroup,1);
        input.data32=ptr.data();a.block.audio_inputs=&input;a.run(plugin);
        const unsigned width=sampleNeonBusWidth(decksLayout(format)),first=recordInputFirst(p,width);
        startRecord(p,1);a.run(plugin);finish();take=p.document->decks[1].layers[0].asset;
        check(take&&take->channelCount==width,"discrete/ACN-SN3D input width matches output format");
        for(unsigned ch=0;ch<width;++ch)for(unsigned n=0;n<256;++n)check(take->channels[ch][n]==in[first+ch][n],"spatial input channel order and gain preserved");
    }
    auto empty=std::make_shared<Document>();publish(p,empty,3,false);setValue(p,param(0,Layer),0);setValue(p,Format,0);setValue(p,RecordInputGroup,0);setValue(p,RecordSeconds,1);
    for(auto& ch:in)ch.fill(0);a.run(plugin);startRecord(p,0);a.run(plugin,190);service(p);waitWork(p);
    check(p.document->decks[0].layers[0].asset&&p.document->decks[0].layers[0].asset->frameCount()==48000,"connected silence records and maximum duration stops exactly");
    const auto* saved=p.document->decks[0].layers[0].asset.get();startRecord(p,0);a.run(plugin,2);setValue(p,Format,1);a.run(plugin);service(p);waitWork(p);
    check(p.document->decks[0].layers[0].asset.get()==saved&&p.document->decks[0].layers[1].asset->channelCount==2,"format change keeps the original take format and existing audio");
    Bytes state;check(saveState(plugin,&state.out),"input source settings save");setValue(p,RecordSource,0);state.at=0;check(loadState(plugin,&state.in)&&value(p,RecordSource)==1,"input source settings recall");
    Bytes old;versionSeven(state,old);old.data.erase(old.data.end()-202,old.data.end()-42);old.data[4]=4;check(loadState(plugin,&old.in)&&value(p,RecordSource)==0,"older states default to deck resampling");
    plugin->stop_processing(plugin);plugin->deactivate(plugin);plugin->destroy(plugin);
}
void whiteBox(Bytes& state){
    const auto* plugin=create(&factory,&testHost,descriptor.id);check(plugin&&plugin->init(plugin),"create whitebox");auto& p=*self(plugin);loadFixture(p);
    check(plugin->activate(plugin,48000,1,256),"activate");plugin->start_processing(plugin);Audio a;a.run(plugin);
    jogBlockContinuity(p,plugin);
    jogWorkflow(p,plugin,a);
    stackJogWorkflow(p,plugin,a);
    keyboardWorkflow(p,plugin,a);
    ledWorkflow(p,plugin,a);
    controllerIndependentWorkflow(p,plugin,a);sliceBoundaryWorkflow();
    padModeWorkflow(p,plugin,a);
    for(unsigned method=0;method<9;++method){reset(plugin);setValue(p,param(0,Playback),method);setValue(p,param(0,SourceMode),method==6?4:1);
        command(p,{CommandKind::Play,0});const double energy=a.run(plugin,40);std::cout<<"method "<<kMethods[method]<<" energy "<<energy<<'\n';check(energy>1.e-4,"each method produces audio");}
    reset(plugin);setValue(p,param(0,Playback),0);setValue(p,param(0,SourceMode),1);setValue(p,Crossfade,1);a.run(plugin,4);
    command(p,{CommandKind::Play,0});a.run(plugin,8);
    startRecord(p,0);check(p.capture.state.load()==1,"capture arms and latches");
    check(a.run(plugin,20)<1.e-7,"inaudible deck stays out of mix");check(p.capture.frames.load()==20*256,"capture advances without holding record");
    startRecord(p,0);a.run(plugin);service(p);waitWork(p);
    const auto take=p.document->decks[1].layers[3].asset;check(take&&take->frameCount()==20*256,"resample appends next empty layer");
    double sum=0;for(float v:take->channels[0])sum+=v*v;check(sum>1,"pre-crossfader capture contains source audio");
    check(p.document->decks[0].layers[0].asset!=take,"sender remains intact");
    history(p,false);check(!p.document->decks[1].layers[3].asset,"undo removes take");history(p,true);check(p.document->decks[1].layers[3].asset==take,"redo restores identical PCM");
    setValue(p,Crossfade,0);a.run(plugin);command(p,{CommandKind::Play,1});a.run(plugin,8);
    startRecord(p,1);a.run(plugin,10);startRecord(p,1);a.run(plugin);service(p);waitWork(p);
    const auto returned=p.document->decks[0].layers[3].asset;
    check(returned&&returned->frameCount()==10*256,"B to A handoff appends a new layer");
    check(p.document->decks[1].layers[3].asset==take,"B to A leaves sending take untouched");
    sum=0;for(float v:returned->channels[0])sum+=v*v;check(sum>0.1,"return handoff contains audio");
    history(p,false);check(!p.document->decks[0].layers[3].asset,"return handoff undo");
    setValue(p,param(0,family(NeonFamily::StackShape)),1);setValue(p,param(0,family(NeonFamily::PathCount)),8);
    check(settings(p,*p.document).slots[0].family.valid(),"automated path shape and count remain valid");
    check(control(p,0,family(NeonFamily::PathCount))==8&&control(p,0,family(NeonFamily::PathTime)+7)==1,"path count regenerates endpoints");
    setValue(p,param(0,family(NeonFamily::StackShape)),0);setValue(p,param(0,family(NeonFamily::PathCount)),4);
    check(settings(p,*p.document).slots[0].family.valid()&&control(p,0,family(NeonFamily::PathTime)+3)==1,"manual path count resamples existing curve");
    reset(plugin);setValue(p,Crossfade,0);setValue(p,param(0,PadBank),0);a.run(plugin);
    a.midi(0x92,0x20,127,0);a.midi(0xb2,0x20,39,1);a.run(plugin);
    check(std::abs(p.padVelocity[0][0]-39.f/127)<1.e-6,"separate velocity preserved");
    check(p.playing[0].load()&&p.padHeld[0][0],"velocity protocol triggers pad");
    check(p.padPressure[0][0]==0,"velocity CC does not become FX aftertouch");
    a.midi(0xa2,0x20,45);a.run(plugin);check(std::abs(p.padPressure[0][0]-45.f/127)<1.e-6,"poly pressure preserved");
    a.midi(0x82,0x20,0);a.run(plugin);check(!p.padHeld[0][0],"pad release");
    a.midi(0x92,0x21,127);a.run(plugin,2);check(p.playing[0].load()&&control(p,0,Layer)==1,"fixed velocity also triggers pad");
    a.midi(0x92,0x29,127);a.run(plugin);a.midi(0x90,0x4a,127);a.midi(0x82,0x29,0);a.run(plugin);check(!p.padHeld[0][1],"release follows original mode");
    a.midi(0xb4,0x45,0);a.run(plugin);check(value(p,Crossfade)==0,"crossfader A endpoint");
    a.midi(0xb4,0x45,127);a.run(plugin);check(value(p,Crossfade)==1,"crossfader B endpoint");
    a.midi(0xb0,123,0);a.run(plugin);check(!p.playing[0].load()&&!p.playing[1].load(),"all notes off");
    setValue(p,Crossfade,0);check(saveState(plugin,&state.out),"embedded state save");
    state.at=0;check(loadState(plugin,&state.in),"embedded state load");Bytes again;check(saveState(plugin,&again.out)&&again.data==state.data,"exact state roundtrip");
    // v1 audio/parameters remain loadable; v2 appends only display preferences.
    Bytes v7;versionSeven(state,v7);
    Bytes legacy;legacy.data=v7.data;legacy.data[4]=1;legacy.data.resize(legacy.data.size()-202);
    for(unsigned d=0;d<2;++d)for(unsigned key=SamplerPads;key<ControlCount;++key)
        std::fill_n(legacy.data.begin()+12+param(d,key)*sizeof(float),sizeof(float),uint8_t(0));
    check(loadState(plugin,&legacy.in),"version-one sets remain compatible");
    check(control(p,0,KeyboardRoot)==60&&control(p,0,KeyboardVoices)==8&&control(p,0,SamplerPads)==0,"reserved old-state slots migrate to keyboard defaults");
    Bytes versionTwo;versionTwo.data=legacy.data;versionTwo.data[4]=2;versionTwo.data.insert(versionTwo.data.end(),v7.data.end()-42,v7.data.end());
    check(loadState(plugin,&versionTwo.in),"version-two sets remain compatible");
    Bytes versionThree;versionThree.data=v7.data;versionThree.data.erase(versionThree.data.end()-202,versionThree.data.end()-42);versionThree.data[4]=3;
    for(unsigned d=0;d<2;++d)for(unsigned key=ScreenVelocity;key<ControlCount;++key)
        std::fill_n(versionThree.data.begin()+12+param(d,key)*sizeof(float),sizeof(float),uint8_t(0));
    check(loadState(plugin,&versionThree.in)&&control(p,0,ScreenVelocity)==1&&control(p,0,ScreenLatch)==0,"version-three sets migrate mouse controls without silent velocity");
    p.viewState.oppositeEdit={1,0};p.viewState.page={2,6};p.viewState.orientation={3,2};p.viewState.zoom={8,2};
    Bytes viewState;check(saveState(plugin,&viewState.out),"save workspace preferences");p.viewState={};
    check(loadState(plugin,&viewState.in)&&p.viewState.oppositeEdit[0]==1&&p.viewState.page[0]==2&&p.viewState.zoom[0]==8&&p.viewState.orientation[0]==3&&p.viewState.page[1]==6,"workspace preferences recalled");
    state.at=0;check(loadState(plugin,&state.in),"restore baseline after view test");
    Bytes malformed;malformed.data=state.data;malformed.data.resize(malformed.data.size()-13);check(!loadState(plugin,&malformed.in),"truncated state rejected");
    Bytes unchanged;check(saveState(plugin,&unchanged.out)&&unchanged.data==state.data,"failed state load is transactional");
    for(unsigned format=1;format<=5;++format){reset(plugin);const unsigned width=sampleNeonBusWidth(decksLayout(format));loadFixture(p,width);setValue(p,Format,format);setValue(p,param(0,Playback),0);setValue(p,param(0,Layer),0);setValue(p,Crossfade,0);a.run(plugin);
        command(p,{CommandKind::Play,0});check(a.run(plugin,8)>0,"multichannel output audible");
        a.midi(0x90,0x63,127);a.midi(0xb0,0x43,61,70);a.run(plugin,3);
        for(unsigned ch=1;ch<width;++ch)for(unsigned n=0;n<256;++n)check(std::abs(a.pcm[ch][n]-(ch%2?-a.pcm[0][n]:a.pcm[0][n]))<1.e-5,"preserve-field channel coherence");
        setValue(p,param(0,SamplerPads),1);a.midi(0x80,0x63,0);a.midi(0x92,0x20,127);a.midi(0x92,0x27,127);a.run(plugin,20);
        for(unsigned ch=1;ch<width;++ch)for(unsigned n=0;n<256;++n)check(std::abs(a.pcm[ch][n]-(ch%2?-a.pcm[0][n]:a.pcm[0][n]))<1.e-5,"keyboard chords preserve multichannel field coherence");
        for(unsigned ch=width;ch<32;++ch)for(float v:a.pcm[ch])check(v==0,"unused outputs silent");}
    plugin->stop_processing(plugin);plugin->deactivate(plugin);plugin->destroy(plugin);
}
void binary(const char* path,Bytes& state,Bytes& mixedState){void* image=dlopen(path,RTLD_NOW|RTLD_LOCAL);check(image!=nullptr,"open exact CLAP binary");
    const auto* entry=static_cast<const clap_plugin_entry_t*>(dlsym(image,"clap_entry"));check(entry&&entry->init(path),"entry init");
    const auto* f=static_cast<const clap_plugin_factory_t*>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));check(f&&f->get_plugin_count(f)==1,"factory count");
    const auto* plugin=f->create_plugin(f,&testHost,descriptor.id);check(plugin&&plugin->init(plugin),"binary init");
    const auto* stateExt=static_cast<const clap_plugin_state_t*>(plugin->get_extension(plugin,CLAP_EXT_STATE));
    state.at=0;check(stateExt&&stateExt->load(plugin,&state.in),"binary loads real PCM set");
    check(plugin->get_extension(plugin,CLAP_EXT_GUI)!=nullptr,"binary has editor extension");
    check(plugin->activate(plugin,48000,1,256)&&plugin->start_processing(plugin),"binary activate");Audio a;a.run(plugin);
    check(a.feedbackCount==270,"exact binary initializes Beatpad pads and jog rings");
    a.run(plugin,static_cast<unsigned>(std::ceil(48000.*3/a.block.frames_count)));
    for(unsigned frame=0;frame<48000*6;frame+=a.block.frames_count){
        a.run(plugin);check(a.feedbackCount==0,"exact binary sends no periodic idle LED refresh");}
    const auto* params=static_cast<const clap_plugin_params_t*>(plugin->get_extension(plugin,CLAP_EXT_PARAMS));check(params!=nullptr,"binary parameter extension");
    const auto* ports=static_cast<const clap_plugin_audio_ports_t*>(plugin->get_extension(plugin,CLAP_EXT_AUDIO_PORTS));clap_audio_port_info_t inputPort{},outputPort{};
    check(ports&&ports->count(plugin,true)==1&&ports->count(plugin,false)==1&&ports->get(plugin,0,true,&inputPort)&&ports->get(plugin,0,false,&outputPort)
        &&inputPort.channel_count==32&&outputPort.channel_count==32&&outputPort.id==0&&inputPort.id!=outputPort.id,"exact binary exposes input without changing output identity");
    for(unsigned method=0;method<9;++method){plugin->reset(plugin);
        clap_event_param_value_t setting{};setting.header={sizeof(setting),0,CLAP_CORE_EVENT_SPACE_ID,CLAP_EVENT_PARAM_VALUE,0};setting.param_id=param(0,Playback)+1;setting.value=method;
        clap_input_events_t input{&setting,[](const clap_input_events_t*)->uint32_t{return 1;},[](const clap_input_events_t* e,uint32_t)->const clap_event_header_t*{return &static_cast<clap_event_param_value_t*>(e->ctx)->header;}};
        params->flush(plugin,&input,nullptr);a.midi(0x90,0x60,127);check(a.run(plugin,40)>1.e-5,"each playback method in exact bundle produces audio");
    }
    binaryCueLayerWorkflow(plugin,params,a);
    binaryTransportResetWorkflow(plugin,params,a);
    headphoneBinaryWorkflow(plugin,params,a);
    beatpadShiftBinaryWorkflow(plugin,params,a);
    binaryCaptureDestinationWorkflow(plugin,params,a,mixedState);
    plugin->stop_processing(plugin);plugin->deactivate(plugin);plugin->destroy(plugin);entry->deinit();dlclose(image);
}
}
int main(int argc,char** argv){@autoreleasepool{try{
    waveformPeakChecks();headphoneMatrixChecks();headphoneWorkflow();beatpadShiftWorkflow();
    if(argc==3&&std::string(argv[1])=="--graphics-fixture"){graphicsFixture(argv[2]);return 0;}
    projectMediaSnapshotChecks();
    characterTransitionChecks();cueTransitionChecks();
    transportResetWorkflow();
    platterModel();
    const auto view=DeckViewport::make(8,0,.01,true);check(std::abs(view.fraction(.01)-.4)<1.e-12,"scrolling playhead stays fixed near source edges");
    const auto overview=DeckViewport::make(8,.3,.9,false);check(overview.pan(-2)>.3&&overview.pan(2)<.3,"horizontal wheel pans overview in both directions");
    check(overview.pan(100)==0&&overview.pan(-100)==.875&&DeckViewport{}.pan(-2)==0,"overview pan clamps to source edges and full-fit stays still");
    check(DeckViewState{}.target(0)==0,"left waveform targets A");DeckViewState ui;ui.oppositeEdit[0]=1;check(ui.target(0)==1,"left edit targets B");
    cueLayerWorkflow();stackCueWorkflow();layerClipboardWorkflow();jogLedWorkflow();Bytes mixedState;captureDestinationWorkflow(mixedState);trackInputWorkflow();capturePadWorkflow();rollFieldWorkflow();Bytes state;whiteBox(state);if(argc>1)binary(argv[1],state,mixedState);
    if(const char* fixture=std::getenv("S3G_DECKS_TEST_STATE")){std::ofstream file(fixture,std::ios::binary);file.write(reinterpret_cast<const char*>(state.data.data()),state.data.size());}
    std::cout<<"Sample Decks: "<<checks<<" checks passed\n";return 0;}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}}
