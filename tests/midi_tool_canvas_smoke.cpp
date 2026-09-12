#if defined(__APPLE__)
#import <Cocoa/Cocoa.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include S3G_MIDI_SOURCE
#include "vstgui/lib/coffscreencontext.h"
#include "vstgui/lib/platform/platformfactory.h"
#include <fstream>
#include <iostream>
#include <map>
#include <thread>
#include "../plugins/common/s3g_clap_atomic_pod.h"

namespace {
using namespace VSTGUI;
using namespace midi_tool_canvas;
namespace F = s3g::portable_gui::foundation;
namespace D = s3g::portable_gui::midi_drawing;
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
  uint64_t midiCount=0;
  std::array<uint8_t,3> lastMidi{};
  clap_output_events_t output{
      this, [](const clap_output_events_t *o, const clap_event_header_t *h) {
        auto &e = *static_cast<Events *>(o->ctx);
        if (e.reject)
          return false;
        if(h->type==CLAP_EVENT_MIDI) {
          const auto* m=reinterpret_cast<const clap_event_midi_t*>(h);
          ++e.midiCount;std::copy(m->data,m->data+3,e.lastMidi.begin());
        }
        if (h->type == CLAP_EVENT_PARAM_VALUE) {
          const auto *v = reinterpret_cast<const clap_event_param_value_t *>(h);
          e.events.push_back({Kind::Value, v->param_id, v->value});
        } else if(h->type==CLAP_EVENT_PARAM_GESTURE_BEGIN || h->type==CLAP_EVENT_PARAM_GESTURE_END) {
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
  if (const auto *dir = std::getenv("S3G_MIDI_CAPTURE_DIR")) {
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

struct MidiInput {
  std::vector<clap_event_midi_t> events;
  clap_input_events_t input{this,
    [](const clap_input_events_t* i)->uint32_t{return static_cast<MidiInput*>(i->ctx)->events.size();},
    [](const clap_input_events_t* i,uint32_t n)->const clap_event_header_t*{return &static_cast<MidiInput*>(i->ctx)->events.at(n).header;}};
  void midi(uint8_t a,uint8_t b,uint8_t c,uint32_t time=0) {
    clap_event_midi_t e{};e.header={sizeof(e),time,CLAP_CORE_EVENT_SPACE_ID,CLAP_EVENT_MIDI,0};
    e.data[0]=a;e.data[1]=b;e.data[2]=c;events.push_back(e);
  }
  void nrpn(uint16_t id,uint16_t value) {
    midi(0xbf,99,id>>7);midi(0xbf,98,id&127);midi(0xbf,6,value>>7);midi(0xbf,38,value&127);
  }
};
uint64_t timeline=0;
void tick(Plugin& p,Editor& v,Events& e,MidiInput* input=nullptr,unsigned blocks=1) {
  clap_event_transport_t transport{};
  transport.flags=CLAP_TRANSPORT_HAS_TEMPO|CLAP_TRANSPORT_HAS_BEATS_TIMELINE|CLAP_TRANSPORT_IS_PLAYING;
  transport.tempo=120.;
  for(unsigned i=0;i<blocks;++i) {
    clap_process_t proc{};proc.frames_count=128;proc.steady_time=timeline;
    transport.song_pos_beats=clap_beattime(double(timeline)/48000.*2.*CLAP_BEATTIME_FACTOR);
    proc.transport=&transport;proc.in_events=i==0&&input?&input->input:nullptr;proc.out_events=&e.output;
    check(process(&p.plugin,&proc)!=CLAP_PROCESS_ERROR,"MIDI-only process");
    timeline+=128;v.service();
  }
}
void run(Plugin& p,Editor& v,Events& e) {
  render(v,"first-open");
  check(activate(&p.plugin,48000.,1,128),"activate MIDI plugin");
  check(startProcessing(&p.plugin),"start MIDI processing");
#if S3G_MIDI_TOOL_KIND == 1
  // Every card retains the hardware's page order, hit map and informational
  // action glyphs. Parameter cells select; they do not synthesize knob edits.
  for(uint32_t page=0;page<guiPages().size();++page)for(uint32_t slot=0;slot<16;++slot) {
    const auto& control=guiPages()[page].controls[slot];
    const int32_t before=p.uiSelectedIndex.load();
    click(v,center(guiControlCellRect(page,slot)));
    const int32_t index=parameterIndex(control.parameter);
    check(p.uiSelectedIndex.load()==(control.action||index<0?before:index),"NIM hardware cell selection");
  }
  auto track=v.takeoverTrackRect();
  down(v,{track.origin.x, D::midY(track)});move(v,{D::maxX(track),D::midY(track)});up(v);flush(p,v,e);
  check(near(value(p,kTakeoverParamId),5000),"NIM smooth takeover drag");
  down(v,center(track),2);up(v);flush(p,v,e);
  check(near(value(p,kTakeoverParamId),650),"NIM takeover default reset");
  // Two clicks before the host services the queue must not collapse into one.
  click(v,center(v.buttonRect(1)));click(v,center(v.buttonRect(1)));flush(p,v,e);
  check(value(p,kPlayParamId)==0,"NIM ordered rapid Play toggles");
  click(v,center(v.buttonRect(0)));flush(p,v,e);
  check(value(p,kRecordParamId)==1,"NIM record button");
  MidiInput first;first.nrpn(5,2000);first.nrpn(6,4000);tick(p,v,e,&first,8);
  MidiInput next;next.nrpn(5,12000);next.nrpn(6,14000);tick(p,v,e,&next,8);
  render(v,"recording-rings");
  click(v,center(v.buttonRect(0)));flush(p,v,e);
  check(value(p,kLoopCountParamId)==2,"NIM commits independent loops");
  set(p,v,e,kPlayParamId,1);const auto before=e.midiCount;tick(p,v,e,nullptr,100);
  check(e.midiCount>before,"NIM loops emit MIDI");render(v,"playing-loops");
  // Clear-selected captures the selected ID, even if selection changes before flush.
  p.uiSelectedIndex.store(parameterIndex(5));click(v,center(v.buttonRect(2)));
  p.uiSelectedIndex.store(parameterIndex(6));flush(p,v,e);
  check(value(p,kLoopCountParamId)==1 && !(p.uiFlags[parameterIndex(5)].load()&kUiLoopActive)
      && (p.uiFlags[parameterIndex(6)].load()&kUiLoopActive),"NIM exact clear-selected target");
  click(v,center(v.buttonRect(0)));flush(p,v,e);MidiInput take;take.nrpn(5,5000);tick(p,v,e,&take,4);
  click(v,center(v.buttonRect(4)));flush(p,v,e);
  check(value(p,kRecordParamId)==0 && value(p,kLoopCountParamId)==1,"NIM cancel discards take only");
  Stream saved;check(stateSave(&p.plugin,&saved.output),"NIM state save");
  click(v,center(v.buttonRect(3)));flush(p,v,e);check(value(p,kLoopCountParamId)==0,"NIM clear all");
  check(stateLoad(&p.plugin,&saved.input),"NIM state recall");
  check(value(p,kLoopCountParamId)==1 && value(p,kPlayParamId)==1,"NIM loops and Play restore");
  bool clearedCell=false;
  for(uint32_t page=0;page<guiPages().size()&&!clearedCell;++page)for(uint32_t slot=0;slot<16&&!clearedCell;++slot) {
    const auto& control=guiPages()[page].controls[slot];
    if(control.parameter==6&&!control.action) {
      down(v,center(guiControlCellRect(page,slot)),2);up(v);flush(p,v,e);clearedCell=true;
    }
  }
  check(clearedCell&&value(p,kLoopCountParamId)==0,"NIM double-click clears the selected cell's loop");
  saved.pos=0;check(stateLoad(&p.plugin,&saved.input),"NIM restore after cell clear");
  down(v,center(track));v.stopRefresh();flush(p,v,e);
  check(!v._draggingTakeover && value(p,kPlayParamId)==1,"NIM close ends gesture, not playback");
  e.balanced();
  e.reject=true;set(p,v,e,kTakeoverParamId,1200);check(p.guiParamEvents.available()<1023,"NIM backpressure retains queue");
  e.reject=false;flush(p,v,e);check(near(value(p,kTakeoverParamId),1200),"NIM backpressure retry");e.balanced();
#else
  for(uint32_t index=0;index<kParamCount;++index) {
    // Visit the actual page and relay owning each parameter, then use its original hit area.
    if(index<kGlobalParamCount)v.selectVisualPage(index>=kMidiInputMode&&index<=kMidiInputDecay?4:0);
    else {
      v._selectedRelay=(index-kGlobalParamCount)/kRelayParamCount;
      const auto local=(index-kGlobalParamCount)%kRelayParamCount;
      v.selectRelayPage(local==kRelayCcA||local==kRelayCcB||local>=kRelayCcASource?1:0);
    }
    const auto spec=paramSpec(index);
    if(parameterIsMenu(index)) {
      click(v,center(parameterMenuBoxRect(index)));
      check(v._openMenu==int(index),"Relay menu hit map");
      const auto count=v._menuItemCount;const auto columns=parameterMenuColumns(index);
      const auto rows=D::multiColumnMenuRows(count,columns);const auto menu=v.openMenuRect();
      if(index==kScale||index==kMidiInputChannel)render(v,"menu-"+std::to_string(index));
      const uint32_t item=count-1;
      click(v,{menu.origin.x+(item/rows+.5)*menu.size.width/columns,menu.origin.y+(item%rows+.5)*18});
      flush(p,v,e);check(near(value(p,spec.id),parameterMenuValue(index,item)),"Relay last menu item, including multi-column");
    } else if(parameterIsBinary(index)) {
      const double before=value(p,spec.id);click(v,center(parameterInteractionRect(index)));flush(p,v,e);
      check(value(p,spec.id)!=(before>=.5?1.:0.),"Relay binary toggle");
    } else {
      auto track=parameterSliderTrackRect(index);
      down(v,{track.origin.x, D::midY(track)});move(v,{D::maxX(track),D::midY(track)});up(v);flush(p,v,e);
      if(!near(value(p,spec.id),spec.maximum)){std::cerr<<"maximum: "<<spec.name<<'\n';ok=false;}
      down(v,center(track),2);up(v);flush(p,v,e);
      if(!near(value(p,spec.id),spec.defaultValue)){std::cerr<<"default: "<<spec.name<<'\n';ok=false;}
    }
    e.balanced();
  }
  std::array<double,kParamCount> assignments{};
  for(uint32_t i=0;i<kParamCount;++i)assignments[i]=publishedValue(p,i);
  for(uint32_t preset=0;preset<kFactoryPresetCount;++preset) {
    check(v.applyFactoryPreset(preset),"Relay factory preset queue");flush(p,v,e);
    const auto config=factoryPresetConfig(preset);
    for(uint32_t i=0;i<kParamCount;++i) {
      const auto local=i>=kGlobalParamCount?(i-kGlobalParamCount)%kRelayParamCount:kRelayParamCount;
      const bool kept=local==kRelayChannel||local==kRelayNote||local==kRelayCcA||local==kRelayCcB;
      check(near(publishedValue(p,i),kept?assignments[i]:configValue(config,i)),"Relay complete factory preset with routing preserved");
    }
    e.balanced();
  }
  check(v.applySafeRandom(),"Relay safe random");flush(p,v,e);e.balanced();
  for(uint32_t i=kGlobalParamCount;i<kParamCount;++i) {
    const auto local=(i-kGlobalParamCount)%kRelayParamCount;
    if(local==kRelayChannel||local==kRelayNote||local==kRelayCcA||local==kRelayCcB)
      check(near(publishedValue(p,i),assignments[i]),"Relay random preserves MIDI assignments");
  }
  v.applyFactoryPreset(1);flush(p,v,e);tick(p,v,e,nullptr,300);
  check(e.midiCount>0,"Relay produces MIDI");
  for(uint32_t page=0;page<5;++page) {
    click(v,center(visualPageTabRect(page)));check(v._visualPage==int(page),"Relay five visual tabs");
    render(v,"page-"+std::to_string(page));
  }
  v.selectVisualPage(1);
  for(int mode=0;mode<2;++mode){click(v,center(learningModeRect(mode)));check(v._matrixMode==mode,"Relay learning mode");}
  click(v,center(learningCellRect(2,3)));check(v._selectedMatrixCell==11,"Relay matrix cell selection");
  render(v,"learning-selected");click(v,center(learningCellRect(2,3)));check(v._selectedMatrixCell==-1,"Relay matrix deselect");
  v.selectVisualPage(2);
  for(uint32_t depth=0;depth<3;++depth) {
    set(p,v,e,paramIdForIndex(kLatticeDepth),depth);tick(p,v,e);
    for(uint32_t plane=0;plane<=s3g::relay::latticePlaneCount(depth);++plane) {
      click(v,center(formPlaneButtonRect(plane)));check(v._formPlane==int(plane)-1,"Relay form plane inspection");
    }
    render(v,"form-depth-"+std::to_string(depth));
  }
  v.selectVisualPage(3);
  for(int filter=0;filter<3;++filter){click(v,center(consoleFilterRect(filter)));check(v._consoleFilter==filter,"Relay console filter");render(v,"console-"+std::to_string(filter));}
  click(v,center(consoleClearRect()));check(v._consoleFloor==p.midiTraceWrite.load(),"Relay console clear floor");
  for(uint32_t relay=0;relay<8;++relay){click(v,center(relayTabRect(relay)));check(v._selectedRelay==int(relay),"Relay eight voice tabs");}
  v.selectRelayPage(1);render(v,"cc-routing");
  set(p,v,e,paramIdForIndex(kMemory),.42);v.selectVisualPage(2);tick(p,v,e);
  click(v,center(crystallizeRect()));flush(p,v,e);
  const double held=p.heldFormBeat.load();
  check(p.formHold.load() && near(p.thawMemory.load(),.42) && value(p,paramIdForIndex(kFreeze))==1,"Relay crystallize");
  const auto file=std::filesystem::temp_directory_path()/F::pathFromUtf8(("s3g-midi-préset-音-"+std::to_string(D::randomBits())+".s3gpreset").c_str());
  check(v.presetFile(F::pathToUtf8(file),true),"Relay Unicode preset save");
  click(v,center(crystallizeRect()));flush(p,v,e);
  check(!p.formHold.load()&&near(value(p,paramIdForIndex(kMemory)),.42),"Relay thaw memory");
  set(p,v,e,paramIdForIndex(kMemory),.2);
  check(v.presetFile(F::pathToUtf8(file),false),"Relay staged preset load");flush(p,v,e);
  check(p.formHold.load()&&near(p.thawMemory.load(),.42)&&near(p.heldFormBeat.load(),held),"Relay preset retains held form and thaw state");
  {std::ofstream corrupt(file,std::ios::binary|std::ios::trunc);corrupt<<"not a preset";}
  check(!v.presetFile(F::pathToUtf8(file),false)&&p.formHold.load()&&near(p.thawMemory.load(),.42),"Relay corrupt preset rejected transactionally");
  std::filesystem::remove(file);
  check(!v.presetFile(F::pathToUtf8(file),false),"Relay missing file rejected");
  click(v,center(crystallizeRect()));flush(p,v,e);
  check(near(value(p,paramIdForIndex(kMemory)),.42),"Relay thaw after preset recall");
  e.balanced();
  e.reject=true;set(p,v,e,paramIdForIndex(kMemory),.7);check(p.guiParamEvents.available()<kGuiParamQueueCapacity-1,"Relay queue backpressure");
  e.reject=false;flush(p,v,e);check(near(value(p,paramIdForIndex(kMemory)),.7),"Relay queue retry");e.balanced();
  v.selectVisualPage(0);down(v,center(parameterSliderTrackRect(kActivity)));v.stopRefresh();flush(p,v,e);e.balanced();
#endif
  stopProcessing(&p.plugin);deactivate(&p.plugin);v.stopRefresh();flush(p,v,e);e.balanced();
}
} // namespace
int main() {
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
      CreateWindowExW(0, L"STATIC", L"s3g MIDI tool GUI test",
                      WS_OVERLAPPEDWINDOW, 0, 0, kGuiWidth, kGuiHeight, nullptr,
                      nullptr, GetModuleHandleW(nullptr), nullptr);
#endif
  clap_host_t host{};
  host.clap_version = CLAP_VERSION_INIT;
  host.name = "MIDI tool parity";
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
    std::cout << "MIDI tool canvas parity passed: " << descriptor.id
              << '\n';
  return ok ? 0 : 1;
}
