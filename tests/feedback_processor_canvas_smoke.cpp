#if defined(__APPLE__)
#import <Cocoa/Cocoa.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include S3G_FEEDBACK_SOURCE
#include "vstgui/lib/coffscreencontext.h"
#include "vstgui/lib/platform/platformfactory.h"
#include <fstream>
#include <iostream>
#include <map>
#include <thread>
#include "../plugins/common/s3g_clap_atomic_pod.h"

namespace {
using namespace VSTGUI;
using namespace feedback_processor_canvas;
namespace F = s3g::portable_gui::foundation;
namespace D = s3g::portable_gui::feedback_drawing;
using Kind=s3g::clap_gui::ParamEventKind;
bool ok = true;
void check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << message << '\n';
    ok = false;
  }
}
bool near(double a, double b) { return std::abs(a - b) < 1.e-4; }
struct Events {
  std::vector<s3g::clap_gui::ParamEvent> events;
  bool reject = false;
  clap_output_events_t output{
      this, [](const clap_output_events_t *o, const clap_event_header_t *h) {
        auto &e = *static_cast<Events *>(o->ctx);
        if (e.reject)
          return false;
        if (h->type == CLAP_EVENT_PARAM_VALUE) {
          const auto *v = reinterpret_cast<const clap_event_param_value_t *>(h);
          e.events.push_back({Kind::Value, v->param_id, v->value});
        } else {
          const auto *v =
              reinterpret_cast<const clap_event_param_gesture_t *>(h);
          e.events.push_back({h->type == CLAP_EVENT_PARAM_GESTURE_BEGIN
                                  ? Kind::GestureBegin
                                  : Kind::GestureEnd,
                              v->param_id, 0.});
        }
        return true;
      }};
  void balanced() {
    std::map<clap_id, int> depth;
    for (auto e : events) {
      if (e.kind == Kind::GestureBegin)
        check(++depth[e.paramId] == 1, "nested edit gesture");
      else if (e.kind == Kind::GestureEnd)
        check(--depth[e.paramId] == 0, "unbalanced edit end");
      else
        check(depth[e.paramId] == 1, "value without edit gesture");
    }
    for (auto e : depth)
      check(e.second == 0, "unterminated edit gesture");
    events.clear();
  }
};
struct Stream {
  std::vector<uint8_t> bytes;
  size_t pos = 0;
  clap_ostream_t output{
      this, [](const clap_ostream_t *o, const void *p, uint64_t n) -> int64_t {
        auto &s = *static_cast<Stream *>(o->ctx);
        n = std::min<uint64_t>(n, 23);
        const auto *b = static_cast<const uint8_t *>(p);
        s.bytes.insert(s.bytes.end(), b, b + n);
        return n;
      }};
  clap_istream_t input{
      this, [](const clap_istream_t *i, void *p, uint64_t n) -> int64_t {
        auto &s = *static_cast<Stream *>(i->ctx);
        n = std::min<uint64_t>({n, 17, s.bytes.size() - s.pos});
        std::memcpy(p, s.bytes.data() + s.pos, n);
        s.pos += n;
        return n;
      }};
};
CPoint center(D::Rect r) { return {D::midX(r), D::midY(r)}; }
void down(Editor &v, CPoint p, int clicks = 1, bool alternate = false) {
  MouseDownEvent e;
  e.mousePosition = p;
  e.clickCount = clicks;
  e.buttonState.add(MouseButton::Left);
  if (alternate) e.modifiers.add(ModifierKey::Alt);
  v.onMouseDownEvent(e);
}
void move(Editor &v, CPoint p) {
  MouseMoveEvent e;
  e.mousePosition = p;
  e.buttonState.add(MouseButton::Left);
  v.onMouseMoveEvent(e);
}
void up(Editor &v) {
  MouseUpEvent e;
  v.onMouseUpEvent(e);
}
void click(Editor &v, CPoint p) {
  down(v, p);
  up(v);
}
void flush(Plugin &p, Editor &v, Events &e) {
  paramsFlush(&p.plugin, nullptr, &e.output);
  v.service();
}
double value(Plugin &p, clap_id id) {
  double x = 0;
  check(paramsGetValue(&p.plugin, id, &x), "parameter read");
  return x;
}
void render(Editor &v, const std::string &suffix) {
  auto c = COffscreenContext::create(
      {v.getViewSize().getWidth(), v.getViewSize().getHeight()});
  check(bool(c), "offscreen context");
  if (!c)
    return;
  c->beginDraw();
  v.draw(c);
  c->endDraw();
  if (const auto *dir = std::getenv("S3G_FEEDBACK_CAPTURE_DIR")) {
    const auto folder = F::pathFromUtf8(dir);
    std::filesystem::create_directories(folder);
    auto png = getPlatformFactory().createBitmapMemoryPNGRepresentation(
        c->getBitmap()->getPlatformBitmap());
    std::ofstream file(folder /
                           (std::string(descriptor.id) + "-" + suffix + ".png"),
                       std::ios::binary);
    file.write(reinterpret_cast<const char *>(png.data()), png.size());
    check(bool(file), "PNG capture");
  }
}
void set(Plugin &p, Editor &v, Events &e, clap_id id, double x) {
  v.editValue(id, x);
  flush(p, v, e);
}
void audio(Plugin &p, Editor &v, Events &e, int key = -1,
           unsigned blocks = 32) {
  std::array<std::array<float, 128>, 8u> output{};
  std::array<float *, 8u> ptrs{};
  for (unsigned i = 0; i < 8u; ++i)
    ptrs[i] = output[i].data();
  clap_audio_buffer_t buffer{};
  buffer.channel_count = S3G_FEEDBACK_KIND == 1 ? 2u : 8u;
  buffer.data32 = ptrs.data();
  clap_process_t proc{};
  proc.frames_count = 128;
  proc.audio_outputs_count = 1;
  proc.audio_outputs = &buffer;
  proc.out_events = &e.output;
  clap_event_transport_t transport{};
  transport.flags=CLAP_TRANSPORT_HAS_TEMPO|CLAP_TRANSPORT_HAS_BEATS_TIMELINE|CLAP_TRANSPORT_IS_PLAYING;
  transport.tempo=120.;proc.transport=&transport;
#if S3G_FEEDBACK_KIND == 2 || S3G_FEEDBACK_KIND == 3
 std::array<float,128> input{}; for(unsigned n=0;n<128;++n) input[n]=.1f*std::sin(n*.17f);
 float* ins[]{input.data(),input.data()}; clap_audio_buffer_t ib{};ib.data32=ins;ib.channel_count=2;
 proc.audio_inputs_count=1;proc.audio_inputs=&ib;
#endif
  clap_event_note_t note{};
  note.header = {sizeof(note), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_NOTE_ON,
                 0};
  note.note_id = -1;
  note.port_index = 0;
  note.channel = 0;
  note.key = key;
  note.velocity = .8;
  clap_input_events_t notes{
      &note, [](const clap_input_events_t *) -> uint32_t { return 1; },
      [](const clap_input_events_t *x,
         uint32_t) -> const clap_event_header_t * {
        return &static_cast<clap_event_note_t *>(x->ctx)->header;
      }};
  for (unsigned b = 0; b < blocks; ++b) {
    proc.steady_time=int64_t(b)*128;
    transport.song_pos_beats=clap_beattime(double(b)*128./48000.*2.*double(CLAP_BEATTIME_FACTOR));
    proc.in_events = key >= 0 && b == 0 ? &notes : nullptr;
    check(process(&p.plugin, &proc) != CLAP_PROCESS_ERROR, "audio process");
    for (const auto &ch : output)
      for (float sample : ch)
        check(std::isfinite(sample), "finite audio");
    v.service();
  }
}
void slider(Editor &v, Plugin &p, Events &e, clap_id id, CPoint from,
            CPoint to) {
  clap_param_info_t info{};
  for (uint32_t n = 0; n < paramsCount(&p.plugin); ++n) {
    paramsGetInfo(&p.plugin, n, &info);
    if (info.id == id) break;
  }
  set(p, v, e, id, info.min_value);
  const double before = value(p, id);
  down(v, from);
  move(v, to);
  up(v);
  flush(p, v, e);
  check(!near(before, value(p, id)), "slider drag changes correct parameter");
  down(v, from, 2);
  up(v);
  flush(p, v, e);
  check(near(value(p, id), info.default_value), "slider double-click default");
}
void right(Editor& v,CPoint point) {
 MouseDownEvent event;event.mousePosition=point;event.clickCount=1;event.buttonState.add(MouseButton::Right);
 v.onMouseDownEvent(event);up(v);
}
void keyboard(Editor& v,char c,VirtualKey key=VirtualKey::None) {
 KeyboardEvent event;event.type=EventType::KeyDown;event.character=c;event.virt=key;v.onKeyboardEvent(event);
}
void run(Plugin& p, Editor& v, Events& e) {
  render(v, "first-open");
  for (uint32_t n = 0; n < paramsCount(&p.plugin); ++n) {
    clap_param_info_t info{}; paramsGetInfo(&p.plugin, n, &info);
#if S3G_FEEDBACK_KIND == 1
    if (info.id == kTriggerParamId) continue;
#elif S3G_FEEDBACK_KIND == 3
    bool persistent=false;
    for(uint32_t i=0;i<kPersistentParamCount;++i) if(persistentParamIdAt(i)==info.id) persistent=true;
    if(!persistent) continue;
#elif S3G_FEEDBACK_KIND == 4
    if(info.id==kPresetParamId || info.id==kRandomizeFieldParamId || info.id==kRandomizePatchParamId || info.id==kMutateParamId || info.id==kUndoParamId) continue;
#endif
    set(p,v,e,info.id,info.default_value);
    if(!near(value(p,info.id),info.default_value)) {std::cerr<<"default: "<<info.name<<'\n';ok=false;}
  }
  e.balanced();
#if S3G_FEEDBACK_KIND == 1
  for(const auto& row:kUiRows) {
    const double x=s3g::gui_layout::processorControlX(row.panel->frame.x);
    const double width=s3g::gui_layout::processorTrackWidth(row.panel->frame.width);
    const double y=s3g::gui_layout::rowY(*row.panel,row.row);
    if(row.id==kMidiReceiveParamId) {
      click(v,{x+10,y});check(v._midiReceiveMenuOpen,"Errant MIDI menu opens");render(v,"midi-menu");
      auto menu=v.midiReceiveMenuRect();click(v,{D::maxX(menu)-10,menu.origin.y+7*18+9});
      flush(p,v,e);check(value(p,row.id)==16,"Errant MIDI menu second column");set(p,v,e,row.id,0);
    } else slider(v,p,e,row.id,{x+width*.2,y},{x+width*.8,y});
  }
  click(v,{kAncestryFrame.x+100,kAncestryFrame.y+168});flush(p,v,e);e.balanced();
#elif S3G_FEEDBACK_KIND == 2
  for(uint32_t page=0;page<kFeedbackPageCount;++page) {
    click(v,center(feedbackPageButtonRect(page)));check(v._page==page,"Feedback Shift page selection");render(v,"page-"+std::to_string(page));
  }
  for(uint32_t preset=0;preset<s3g::kFeedbackShiftPresetCount;++preset) {
    v.openMenu_control_rect(kFeedbackMenuPreset,0,D::makeRect(300,20,260,15));
    auto menu=v.openMenuRect();click(v,{menu.origin.x+10,menu.origin.y+18*(preset+.5)});flush(p,v,e);
    check(v._factoryPresetIndex==int(preset),"Feedback Shift factory menu");render(v,"factory-"+std::to_string(preset));
  }
  click(v,center(feedbackPageButtonRect(0)));
  const auto id=matrixParamId(false,2,3);set(p,v,e,id,0);
  click(v,center(feedbackMatrixCellRect(2,3)));flush(p,v,e);check(value(p,id)>0,"Feedback positive route");
  click(v,center(feedbackMatrixCellRect(2,3)));flush(p,v,e);check(near(value(p,id),0),"Feedback erase route");
  down(v,center(feedbackMatrixCellRect(2,3)),1,true);up(v);flush(p,v,e);check(value(p,id)<0,"Feedback Option/Alt negative route");
  click(v,center(feedbackCrosspointActionRect(0)));flush(p,v,e);check(value(p,id)>0,"Feedback flip route sign");
  render(v,"matrix-edited");
  click(v,center(feedbackSceneCopyButtonRect(true)));flush(p,v,e);
  check(near(value(p,matrixParamId(true,2,3)),value(p,id)),"Feedback scene A copied to B");
  click(v,center(feedbackPageButtonRect(1)));
  auto selectMenu=[&](int menu,uint32_t item,uint32_t control=0) {
    v.openMenu_control_rect(menu,control,D::makeRect(300,250,300,18));
    const auto rect=v.openMenuRect();
    check(item<v.openMenuItemCount(),"Feedback menu item exists");
    click(v,{rect.origin.x+8,rect.origin.y+18*(item+.5)});flush(p,v,e);
  };
  for(uint32_t category=0;category<s3g::kFeedbackInsertCategoryCount;++category) {
    selectMenu(kFeedbackMenuInsertCategory,category);
    const auto cat=static_cast<s3g::FeedbackInsertCategory>(category);
    for(uint32_t effect=0;effect<s3g::feedbackInsertEffectCount(cat);++effect) {
      selectMenu(kFeedbackMenuInsertEffect,effect);
      check(value(p,nodeParamId(v._selectedNode,kNodePedalOffset))==double(s3g::feedbackInsertEffectPedal(cat,effect)),"Feedback insert effect selection");
      render(v,"insert-"+std::to_string(category)+"-"+std::to_string(effect));
    }
  }
  for(uint32_t node=0;node<8;++node) {
    click(v,center(feedbackNodeButtonRect(1,node)));check(v._selectedNode==node,"Feedback insert node selection");
    click(v,center(feedbackNodeRandomButtonRect(node)));flush(p,v,e);
  }
#elif S3G_FEEDBACK_KIND == 3
  for(uint32_t preset=0;preset<kFactoryPresetCount;++preset) {
    click(v,center(D::cocoaRect(D::encoderTitleBand(kGuiWidth,kGuiHeight).presetMenu)));
    check(v._presetMenuOpen,"Fissure preset opens");
    auto r=presetDropdownRect();const auto rows=D::multiColumnMenuRows(kFactoryPresetCount,kPresetMenuColumns);
    click(v,{r.origin.x+r.size.width/kPresetMenuColumns*(preset/rows)+8,r.origin.y+kPresetMenuItemHeight*(preset%rows+.5)});
    flush(p,v,e);check(value(p,kPresetParamId)==preset,"Fissure two-column preset menu");render(v,"factory-"+std::to_string(preset));
  }
  const auto id=kMatrixParamBase+2*8+3;set(p,v,e,id,0);
  click(v,center(matrixCellRect(2,3)));flush(p,v,e);check(value(p,id)>0,"Fissure positive paint");
  click(v,center(matrixCellRect(2,3)));flush(p,v,e);check(value(p,id)<0,"Fissure negative paint");
  click(v,center(matrixCellRect(2,3)));flush(p,v,e);check(near(value(p,id),0),"Fissure erase paint");
  for(uint32_t pad=0;pad<2;++pad) {
    set(p,v,e,kFractureLatchParamIds[pad],0);
    auto r=fracturePadRect(pad);down(v,{r.origin.x+10,D::maxY(r)-10});move(v,{D::maxX(r)-10,r.origin.y+10});
    flush(p,v,e);check(value(p,kFractureXParamIds[pad])>.8,"Fissure puck X");check(value(p,kFractureYParamIds[pad])>.8,"Fissure puck Y");
    render(v,"pad-"+std::to_string(pad));up(v);flush(p,v,e);check(near(value(p,kFractureXParamIds[pad]),0),"Fissure spring return");
    click(v,center(fractureLatchButtonRect(pad)));flush(p,v,e);
    down(v,{D::maxX(r)-10,r.origin.y+10});up(v);flush(p,v,e);check(value(p,kFractureXParamIds[pad])>.8,"Fissure latch position");
    click(v,center(fractureLatchButtonRect(pad)));flush(p,v,e);check(near(value(p,kFractureXParamIds[pad]),0),"Fissure unlatch return");
  }
  for(uint32_t cell=0;cell<8;++cell) {click(v,center(cellButtonRect(cell)));flush(p,v,e);check(value(p,kSelectedCellParamId)==cell+1,"Fissure select and strike");}
  for(const auto& row:kUiRows) {
    const double x=s3g::gui_layout::processorControlX(row.panel->frame.x),width=s3g::gui_layout::processorTrackWidth(row.panel->frame.width),y=s3g::gui_layout::rowY(*row.panel,row.row);
    slider(v,p,e,row.id,{x+width*.2,y},{x+width*.8,y});
  }
  check(activate(&p.plugin,48000,1,128),"activate Fissure scene actions");
  for(uint32_t scene=0;scene<4;++scene) {
    click(v,center(sceneButtonRect(scene)));flush(p,v,e);
    set(p,v,e,kPressureParamId,.1+.2*scene);
    click(v,center(storeButtonRect()));flush(p,v,e);audio(p,v,e,-1,1);
  }
  for(uint32_t scene=0;scene<4;++scene) {
    click(v,center(sceneButtonRect(scene)));flush(p,v,e);
    check(near(value(p,kPressureParamId),.1+.2*scene),"Fissure store/recall scene");
  }
  auto morph=sceneMorphRect();down(v,{morph.origin.x,morph.origin.y+12});move(v,{D::midX(morph),D::midY(morph)});up(v);flush(p,v,e);
  check(near(value(p,kPressureParamId),.4),"Fissure continuous scene interpolation");
  deactivate(&p.plugin);
  // A live edit after morph must survive file recall without triggering morph.
  set(p,v,e,kPressureParamId,.73);
  for(uint32_t cell=0;cell<8;++cell) {
    const auto maskId=kCutMaskParamBase+cell;const auto before=value(p,maskId);
    click(v,center(cutMaskButtonRect(cell)));flush(p,v,e);check(value(p,maskId)!=before,"Fissure cut mask");
  }
#elif S3G_FEEDBACK_KIND == 4
  for(int page=0;page<3;++page) {
    click(v,page==0?CPoint{1100,240}:page==1?CPoint{1280,240}:CPoint{1190,240});
    check(v._editorPage==page,"Fault pages");render(v,"page-"+std::to_string(page));
  }
  const auto control=[](double x){return s3g::gui_layout::processorControlX(x);};
  auto menu=[&](int page,int index,CPoint button,CPoint origin,uint32_t count,clap_id id,bool curated=false) {
    click(v,page==0?CPoint{1100,240}:page==1?CPoint{1280,240}:CPoint{1190,240});
    for(uint32_t item=0;item<count;++item) {
      click(v,button);check(v._openMenu==index,"Fault menu opens from visible control");
      if(item==count-1)render(v,"menu-"+std::to_string(index));
      click(v,{origin.x+8,origin.y+18*(item+.5)});flush(p,v,e);
      if(!curated)check(near(value(p,id),item),"Fault menu selects correct parameter/item");
      check(v._openMenu==0,"Fault menu closes");
    }
  };
  menu(0,1,{kLeftControlX+10,kPresetRowY+3},{kLeftControlX,315},14,kPresetParamId);
  menu(0,2,{kLeftControlX+10,455},{kLeftControlX,489},5,kChannelSchemeParamId);
  menu(0,3,{kRightControlX+10,270},{kRightControlX,281},kCodecModeCount,kCodecModeParamId);
  menu(0,4,{kModControlX+10,270},{kModControlX,281},s3g::kPsdRawFieldModAlgorithmCount,kModAlgorithmParamId,true);
  menu(1,4,{control(kLabChartX)+10,270},{control(kLabChartX),281},s3g::kPsdRawFieldModAlgorithmCount,kModAlgorithmParamId);
  const clap_id sourceIds[]={kModSourceParamId,kModSource2ParamId,kModSource3ParamId};
  const clap_id targetIds[]={kModTargetParamId,kModTarget2ParamId,kModTarget3ParamId};
  const clap_id envIds[]={kModEnvelope1ParamId,kModEnvelope2ParamId,kModEnvelope3ParamId};
  for(uint32_t op=0;op<3;++op) {
    const double x=control(labCardX(op));
    menu(1,5+2*op,{x+10,kLabSourceRowY+3},{x,kLabSourceRowY+14},s3g::kPsdRawFieldModSourceCount,sourceIds[op]);
    menu(1,6+2*op,{x+10,kLabTargetRowY+3},{x,kLabTargetRowY+14},s3g::kPsdRawFieldModTargetCount,targetIds[op]);
    menu(1,11+op,{x+10,kLabEnvelopeRowY+3},{x,kLabEnvelopeRowY+14},2,envIds[op]);
  }
  const double bassX=control(kBassReceiverPanelX);
  menu(2,14,{bassX+10,290},{bassX,281},4,kBassReceiverParamId);
  menu(2,15,{bassX+10,316},{bassX,307},3,kBassPitchTrackingParamId);
  menu(2,16,{bassX+10,342},{bassX,333},3,kBassOctaveParamId);
  menu(0,17,{control(kMidiReceivePanelX)+10,kPerformanceRowY},{midiReceiveDropdownRect().origin.x,midiReceiveDropdownRect().origin.y},17,kMidiReceiveParamId);
  menu(0,18,{control(kOutputPanelX)+10,kOutputFormatRowY},{outputFormatDropdownRect().origin.x,outputFormatDropdownRect().origin.y},3,kOutputFormatParamId);
  auto graph=envelopeGraphGeometry(v._readout);
  slider(v,p,e,kAttackParamId,{graph.attack.x,graph.attack.y},{kEnvelopeX+170,graph.attack.y});
  // Generate an actual PCM WAVE with a Unicode filename; no fixture resources
  // or file-format assumptions are hidden behind the import test.
  const auto file=std::filesystem::temp_directory_path()/"s3g-feedback-é-音.wav";
  std::vector<uint8_t> bytes(44+2048*2);
  auto u16=[&](size_t n,uint16_t x){bytes[n]=uint8_t(x);bytes[n+1]=uint8_t(x>>8);};
  auto u32=[&](size_t n,uint32_t x){u16(n,uint16_t(x));u16(n+2,uint16_t(x>>16));};
  std::memcpy(bytes.data(),"RIFF",4);u32(4,uint32_t(bytes.size()-8));std::memcpy(bytes.data()+8,"WAVEfmt ",8);
  u32(16,16);u16(20,1);u16(22,1);u32(24,48000);u32(28,96000);u16(32,2);u16(34,16);
  std::memcpy(bytes.data()+36,"data",4);u32(40,uint32_t(bytes.size()-44));
  for(size_t n=0;n<2048;++n)u16(44+n*2,uint16_t(int16_t(std::sin(n*.13)*16000)));
  {std::ofstream f(file,std::ios::binary);f.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());}
  check(v.importFaultSource(F::pathToUtf8(file),true),"Fault Unicode WAVE import queues");flush(p,v,e);
  check(p.rawSource && p.rawSource->waveform && p.rawSource->sourceChannelCount==1,"Fault WAVE decoded");render(v,"wave-import");
  Stream waveState;check(stateSave(&p.plugin,&waveState.output),"Fault saves WAVE path and interpretation");
  check(v.importFaultSource(F::pathToUtf8(file),false),"Fault raw import queues");flush(p,v,e);
  check(p.rawSource && !p.rawSource->waveform,"Fault raw bytes option");
  check(stateLoad(&p.plugin,&waveState.input),"Fault WAVE source project recall");v.service();
  check(p.rawSource && p.rawSource->waveform,"Fault project recalls WAVE rather than raw interpretation");
  char publishedStatus[512]{};faultSourceStatus(p,publishedStatus,sizeof(publishedStatus));
  check(sourceStatusText(p)==publishedStatus,"Fault published source status matches Cocoa");
  v.applyParam(v._readout,kRandomizeFieldParamId,.37);flush(p,v,e);check(!p.rawSource,"Fault generate field replaces source");
  v.applyParam(v._readout,kUndoParamId,1);flush(p,v,e);check(bool(p.rawSource),"Fault undo restores imported source");
#endif
  e.balanced();
  const clap_id outputId=
#if S3G_FEEDBACK_KIND == 4
    kGainParamId;
#else
    kOutputParamId;
#endif
  const auto filename=F::pathToUtf8(std::filesystem::temp_directory_path()/(std::string("s3g-feedback-")+descriptor.id+"-é-音.s3gpreset"));
#if S3G_FEEDBACK_KIND == 3
  std::map<clap_id, double> liveBefore;
  for (uint32_t i=0; i<kPersistentParamCount; ++i) {
    const auto liveId=persistentParamIdAt(i); if(liveId!=outputId) liveBefore[liveId]=value(p,liveId);
  }
  const auto scenesBefore=p.scenes;
#endif
  check(v.presetFile(filename,true),"preset save");set(p,v,e,outputId,-19);
  check(v.presetFile(filename,false,outputId),"preset load");flush(p,v,e);check(near(value(p,outputId),-19),"preset preserves output");e.balanced();
#if S3G_FEEDBACK_KIND == 3
  for(const auto& item:liveBefore) {
    if(!near(value(p,item.first),item.second)) {
      std::cerr<<"Fissure preset changed live parameter "<<item.first<<" from "<<item.second<<" to "<<value(p,item.first)<<'\n';ok=false;
    }
  }
  for(size_t scene=0;scene<4;++scene) {
    std::array<double,kSceneValueCount> before{},after{};
    writeSceneValues(scenesBefore[scene],before.data());writeSceneValues(p.scenes[scene],after.data());
    check(before==after,"Fissure preset preserves all four scene tables");
  }
#endif
  e.reject=true;v.editValue(outputId,-17);paramsFlush(&p.plugin,nullptr,&e.output);
  check(p.guiParamEvents.available()<8191,"rejected output event retained");
  e.reject=false;flush(p,v,e);check(near(value(p,outputId),-17),"backpressure retry");e.balanced();
  Stream saved;check(stateSave(&p.plugin,&saved.output),"chunked save");check(stateLoad(&p.plugin,&saved.input),"chunked load");
  check(activate(&p.plugin,48000,1,128),"activate");audio(p,v,e,48,64);render(v,"live");
#if S3G_FEEDBACK_KIND == 3
  click(v,center(grabButtonRect()));audio(p,v,e,-1,40);click(v,center(grabButtonRect()));audio(p,v,e,-1,1);
  check(p.grabbed.load(),"Fissure Grab phrase captured");
  down(v,center(repeatButtonRect()));audio(p,v,e,-1,20);check(p.repeatMix.load()>0,"Fissure Repeat held");render(v,"repeat-live");
  up(v);audio(p,v,e,-1,32);check(value(p,kRepeatParamId)==0,"Fissure Repeat releases");
  down(v,center(repeatButtonRect()));audio(p,v,e,-1,2);v.stopRefresh();flush(p,v,e);
  check(value(p,kRepeatParamId)==0,"Fissure closing GUI releases held Repeat");
#endif
  deactivate(&p.plugin);v.stopRefresh();flush(p,v,e);e.balanced();
}
} // namespace
int main() {
  // Publication is bounded and race-free even while the owner replaces a
  // multiword GUI/state snapshot. No mixed-generation payload may escape.
  s3g::clap_gui::AtomicPod<std::array<uint64_t,64>> snapshot;
  std::atomic<bool> done{false};
  std::thread writer([&]{for(uint64_t n=1;n<10000;++n){std::array<uint64_t,64> values;values.fill(n);snapshot.store(values);}done.store(true,std::memory_order_release);});
  do {
    std::array<uint64_t,64> values{};
    if(snapshot.load(values))for(const auto item:values)check(item==values[0],"coherent atomic snapshot publication");
  } while(!done.load(std::memory_order_acquire));
  writer.join();
#if defined(__APPLE__)
  [NSApplication sharedApplication];
  NSWindow *window = [[NSWindow alloc]
      initWithContentRect:NSMakeRect(0, 0, kGuiWidth, kGuiHeight)
                styleMask:NSWindowStyleMaskTitled
                  backing:NSBackingStoreBuffered
                    defer:NO];
  [window setReleasedWhenClosed:NO];
#elif defined(_WIN32)
  HWND window =
      CreateWindowExW(0, L"STATIC", L"s3g environmental GUI test",
                      WS_OVERLAPPEDWINDOW, 0, 0, kGuiWidth, kGuiHeight, nullptr,
                      nullptr, GetModuleHandleW(nullptr), nullptr);
#endif
  clap_host_t host{};
  host.clap_version = CLAP_VERSION_INIT;
  host.name = "Feedback processor parity";
  host.vendor = "s3g";
  host.version = "1";
  host.url = "";
  host.get_extension = [](const clap_host_t *, const char *) -> const void * {
    return nullptr;
  };
  host.request_process = host.request_restart =
      host.request_callback = [](const clap_host_t *) {};
  auto *plugin=factory.create_plugin(&factory,&host,descriptor.id);
  check(plugin && plugin->init(plugin), "create plugin");
  if (!plugin)
    return 1;
  auto &p = *self(plugin);
  Events events;
  const auto *gui = static_cast<const clap_plugin_gui_t *>(
      plugin->get_extension(plugin, CLAP_EXT_GUI));
  check(gui && gui->create(plugin, s3g::clap_gui::portable::windowApi(), false),
        "portable GUI create");
  if (p.portableGuiEditor) {
    auto &editor = *static_cast<Editor *>(p.portableGuiEditor->contentView());
    clap_window_t parent{};
#if defined(__APPLE__)
    parent.api = CLAP_WINDOW_API_COCOA;
    parent.cocoa = [window contentView];
#elif defined(_WIN32)
    parent.api = CLAP_WINDOW_API_WIN32;
    parent.win32 = window;
#endif
    check(gui->set_parent(plugin, &parent) && gui->show(plugin),
          "native parent/show");
    for (double scale : {.65, 1., 1.5, 2.}) {
      uint32_t w = std::lround(kGuiWidth * scale),
               h = std::lround(kGuiHeight * scale);
      check(gui->adjust_size(plugin, &w, &h) && gui->set_size(plugin, w, h),
            "65-200 percent resizing");
      check(std::abs(double(w) - double(h) * kGuiWidth / kGuiHeight) <=
                .5 * (1. + double(kGuiWidth) / kGuiHeight),
            "proportional resize");
    }
    gui->set_size(plugin, kGuiWidth, kGuiHeight);
    run(p, editor, events);
    check(gui->hide(plugin), "GUI hide");
    gui->destroy(plugin);
    gui->destroy(plugin);
  }
  plugin->destroy(plugin);
#if defined(__APPLE__)
  [window close];
  [window release];
#elif defined(_WIN32)
  DestroyWindow(window);
#endif
  if (ok)
    std::cout << "Feedback processor canvas parity passed: " << descriptor.id
              << '\n';
  return ok ? 0 : 1;
}
