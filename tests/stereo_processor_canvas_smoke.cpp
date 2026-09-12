#if defined(__APPLE__)
#import <Cocoa/Cocoa.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include S3G_STEREO_SOURCE
#include "vstgui/lib/coffscreencontext.h"
#include "vstgui/lib/platform/platformfactory.h"
#include <fstream>
#include <iostream>
#include <map>

namespace {
using namespace VSTGUI;
using namespace stereo_processor_canvas;
namespace F = s3g::portable_gui::foundation;
namespace D = s3g::portable_gui::stereo_drawing;
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
void down(Editor &v, CPoint p, int clicks = 1) {
  MouseDownEvent e;
  e.mousePosition = p;
  e.clickCount = clicks;
  e.buttonState.add(MouseButton::Left);
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
  if (const auto *dir = std::getenv("S3G_STEREO_CAPTURE_DIR")) {
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
  std::array<std::array<float, 128>, 2u> output{};
  std::array<float *, 2u> ptrs{};
  for (unsigned i = 0; i < 2u; ++i)
    ptrs[i] = output[i].data();
  clap_audio_buffer_t buffer{};
  buffer.channel_count = 2u;
  buffer.data32 = ptrs.data();
  clap_process_t proc{};
  proc.frames_count = 128;
  proc.audio_outputs_count = 1;
  proc.audio_outputs = &buffer;
  proc.out_events = &e.output;
  clap_event_transport_t transport{};
  transport.flags=CLAP_TRANSPORT_HAS_TEMPO|CLAP_TRANSPORT_HAS_BEATS_TIMELINE|CLAP_TRANSPORT_IS_PLAYING;
  transport.tempo=120.;proc.transport=&transport;
#if S3G_STEREO_KIND == 4
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
  const double before = value(p, id);
  down(v, from);
  move(v, to);
  up(v);
  flush(p, v, e);
  check(!near(before, value(p, id)), "slider drag changes correct parameter");
  down(v, from, 2);
  up(v);
  flush(p, v, e);
  clap_param_info_t info{};
  for (uint32_t n = 0; n < paramsCount(&p.plugin); ++n) {
    paramsGetInfo(&p.plugin, n, &info);
    if (info.id == id)
      break;
  }
  check(near(value(p, id), info.default_value), "slider double-click default");
}
void right(Editor& v,CPoint point) {
 MouseDownEvent event;event.mousePosition=point;event.clickCount=1;event.buttonState.add(MouseButton::Right);
 v.onMouseDownEvent(event);up(v);
}
void keyboard(Editor& v,char c,VirtualKey key=VirtualKey::None) {
 KeyboardEvent event;event.type=EventType::KeyDown;event.character=c;event.virt=key;v.onKeyboardEvent(event);
}
void run(Plugin& p,Editor& v,Events& e) {
 render(v,"first-open");
 int popupSelected=-1;
 v.openPopup({40,20,240,40},{"HEADING","CONTROL"},-1,
   [&](int index){popupSelected=index;},1,{40,40,240,80},20,{false,true});
 click(v,{60,50});check(popupSelected==-1,"disabled popup heading ignored");
 click(v,{60,70});check(popupSelected==1,"selectable popup row after heading");
 // All parameter identities can be edited, including controls on other pages.
 for(uint32_t i=0;i<paramsCount(&p.plugin);++i) {
  clap_param_info_t info{};check(paramsGetInfo(&p.plugin,i,&info),"parameter metadata");
  v.editValue(info.id,info.default_value);flush(p,v,e);
  check(near(value(p,info.id),info.default_value),"parameter default roundtrip");
 }
 e.balanced();
#if S3G_STEREO_KIND != 4
 const auto band=D::encoderTitleBand(kGuiWidth,kGuiHeight);
 const unsigned factories=
#if S3G_STEREO_KIND == 1
 s3g::kLowFrequencySynthFactoryPresetCount;
#elif S3G_STEREO_KIND == 2
 s3g::kLowformFactoryPresetCount;
#else
 s3g::kProcessorStackFactoryPresetCount;
#endif
 for(unsigned i=0;i<factories;++i) {
   click(v,center(D::cocoaRect(band.presetMenu)));auto menu=v.openMenuRect();
   click(v,{menu.origin.x+10,menu.origin.y+18*i+9});flush(p,v,e);
   check(v._factoryPresetIndex==int(i),"factory menu selection");
   render(v,"factory-"+std::to_string(i));
 }
 for(unsigned page=0;page<
#if S3G_STEREO_KIND == 1
 1u
#elif S3G_STEREO_KIND == 2
 2u
#else
 4u
#endif
 ;++page) {
#if S3G_STEREO_KIND == 2
  click(v,{941.+82.*page,52.});check(unsigned(v._page)==page,"Lowform page click");
#elif S3G_STEREO_KIND == 3
  click(v,center(stackPageButtonRect(page)));check(v._page==page,"Stack page click");
#endif
  render(v,"page-"+std::to_string(page));
  for(const auto& row:kUiRows) {
   double y=row.y;bool menu=false;
#if S3G_STEREO_KIND == 1
   menu=isUiMenuParam(row.id);
#elif S3G_STEREO_KIND == 2
   if(!uiRowVisible(row,v._page,p)) continue;y=uiRowY(row,p);menu=isUiMenuParam(p,row.id);
#else
   if(row.page!=page && row.page!=kAllPages) continue;
   if(isUiToggleParam(row.id)) {double old=value(p,row.id);click(v,{layout::processorControlX(row.panelX)+10,y});flush(p,v,e);check(old!=value(p,row.id),"toggle hit");continue;}
   menu=isUiMenuParam(row.id);
#endif
   double x=s3g::gui_layout::processorControlX(row.panelX);
   double width=s3g::gui_layout::processorTrackWidth(row.panelWidth);
   if(menu) {
    click(v,{x+8,y});check(v._openMenu==row.id,"menu anchor hit");
    auto box=v.openMenuRect();check(box.origin.y>=0 && D::maxY(box)<=kGuiHeight,"menu fits canvas");
    render(v,"menu-"+std::to_string(row.id));
    uint32_t index=v._menuItemCount-1,columns=1;
#if S3G_STEREO_KIND == 2
    if(isModTargetParam(row.id)) columns=kModTargetMenuColumns;
#endif
    uint32_t rows=D::multiColumnMenuRows(v._menuItemCount,columns);
    // Shared multicolumn menus are column-major, as in the original Cocoa.
    click(v,{box.origin.x+(index/rows+.5)*(box.size.width/columns),box.origin.y+(index%rows+.5)*18});
    flush(p,v,e);check(v._openMenu==CLAP_INVALID_ID,"menu closes after selection");
#if S3G_STEREO_KIND == 2
    check(near(value(p,row.id),uiMenuItemValue(p,row.id,index)),"Lowform final menu item");
#else
    check(near(value(p,row.id),paramDef(row.id)->minimum+index),"final menu item");
#endif
   } else slider(v,p,e,row.id,{x+width*.33,y},{x+width*.81,y});
  }
  e.balanced();
 }
#if S3G_STEREO_KIND == 2
 click(v,{941,52});
 for(unsigned engine=0;engine<8;++engine) {set(p,v,e,kBodyEngineParamId,engine);render(v,"engine-"+std::to_string(engine));}
 click(v,{1023,52});
 auto field=lowformArpPatternFieldRect();
 for(unsigned lane=0;lane<4;++lane) {
  click(v,center(lowformArpLaneButtonRect(lane)));check(unsigned(v._arpEditLane)==lane,"arp lane selection");
  down(v,{field.origin.x+4,field.origin.y+5});move(v,{D::maxX(field)-4,D::maxY(field)-5});up(v);flush(p,v,e);
  render(v,"arp-lane-"+std::to_string(lane));
  right(v,{field.origin.x+4,field.origin.y+4});flush(p,v,e);
  check(value(p,lowformArpPatternStepParam(0))==s3g::kProcessorStackArpRest,"right-click pitch REST from every lane");
 }
#elif S3G_STEREO_KIND == 3
 click(v,center(stackPageButtonRect(0)));
 for(unsigned player=0;player<2;++player) {
  auto field=stackPatternFieldRect(player);down(v,{field.origin.x+3,field.origin.y+4});
  move(v,{D::maxX(field)-3,D::maxY(field)-4});up(v);flush(p,v,e);
  right(v,{field.origin.x+4,field.origin.y+4});flush(p,v,e);
  check(value(p,stackPatternStepParam(player,0))==s3g::kProcessorStackArpRest,"Stack pattern REST");
 }
 click(v,center(stackPageButtonRect(3)));v.loadDocumentationScore();
 v._scoreSelectedPlayer=0;v._scoreSelectedString=0;v._scoreSelectedRow=0;
 keyboard(v,'1');keyboard(v,'2');check(p.scoreCells[0].load()==12 && v._scoreSelectedRow==1,"two-digit fret and advance");
 keyboard(v,'h');check(p.scoreCells[s3g::processorStackScoreCellIndex(0,1,0,0)].load()==s3g::kProcessorStackScoreHold,"score HOLD");
 keyboard(v,'r');check(p.scoreCells[s3g::processorStackScoreCellIndex(0,2,0,0)].load()==12,"score reattack skips holds");
 keyboard(v,'-');check(v._scoreSelectedRow==4,"score REST advance");
 auto copied=v.copyScoreText();v.pasteScoreText(copied);check(v.copyScoreText()==copied,"clipboard score roundtrip");
 v.selectScoreLock(1,2,0,s3g::ProcessorStackScoreLockControl::Feedback);
 auto lock=stackScoreLockCell(p,0,2,1,0);check(lock.control!=0,"row lock selection");
 auto cell=stackScoreLockRect(1,0,2);down(v,center(cell));move(v,{D::midX(cell),D::midY(cell)-40});up(v);
 check(stackScoreLockCell(p,0,2,1,0).normalized!=lock.normalized,"row lock vertical drag");
 down(v,center(cell),2);up(v);check(stackScoreLockCell(p,0,2,1,0).control==0,"row lock double-click clears");
 right(v,center(cell));render(v,"row-lock-menu");click(v,{1,1});
 render(v,"score-edited");
 for(unsigned action=0;action<kStackScoreRandomActionCount;++action) {click(v,center(stackScoreRandomActionButtonRect(action)));render(v,"score-random-"+std::to_string(action));}
#endif
#else
 const auto& family=kConduitFamilyLayout;
 const auto band=conduitTitleBand();
 for(unsigned i=0;i<s3g::kProcessorConduitFactoryPresetCount;++i) {
  click(v,center(D::cocoaRect(band.presetMenu)));auto anchor=D::cocoaRect(band.presetMenu);
  click(v,{anchor.origin.x+10,D::maxY(anchor)+2+18*i+9});flush(p,v,e);
  check(v._factoryPresetIndex==int(i),"Conduit factory selection");render(v,"factory-"+std::to_string(i));
 }
 for(unsigned material=0;material<s3g::kProcessorConduitMaterialCount;++material) {
  click(v,{s3g::gui_layout::processorControlX(family.engine.frame.x)+8,s3g::gui_layout::rowY(family.engine,0)});
  double width=s3g::gui_layout::processorMenuWidth(family.engine.frame.width);
  click(v,{s3g::gui_layout::processorControlX(family.engine.frame.x)+width*(material/13)+8,s3g::gui_layout::rowY(family.engine,0)+17+18*(material%13)+9});
  flush(p,v,e);check(value(p,kMaterialParamId)==material,"Conduit two-column material menu");
 }
 const s3g::gui_layout::Panel panels[]{family.output,family.engine,family.relationships,family.preview};
 const std::vector<std::vector<clap_id>> ids{{kOutputParamId,kMixParamId},{kInputParamId,kDriverParamId,kSizeParamId,kTensionParamId,kDampingParamId,kPickupParamId,kContactParamId,kFeedbackParamId},{kPedalMixParamId,kPedalDriveParamId,kPedalToneParamId},{kOctaveDownParamId,kOctaveDragParamId,kPaDriveParamId,kMicMotionParamId,kChamberParamId,kStereoWidthParamId}};
 for(unsigned panel=0;panel<4;++panel) for(unsigned i=0;i<ids[panel].size();++i) {
  double y=s3g::gui_layout::rowY(panels[panel],i+((panel==1||panel==2)?2:0));
  double x=s3g::gui_layout::processorControlX(panels[panel].frame.x),width=s3g::gui_layout::processorTrackWidth(panels[panel].frame.width);
  slider(v,p,e,ids[panel][i],{x+width*.32,y},{x+width*.77,y});
 }
 p.materialActivity.store(.7f);p.governorReduction.store(.3f);
 for(unsigned i=0;i<170;++i)v.service();check(v._activityHistory[0]>.8 && near(v._reductionHistory[0],.3),"Conduit original 8s history");
 render(v,"containment-live");click(v,center(D::cocoaRect(family.panicButton)));check(p.panicRequested.load(),"panic action");
#endif
 e.balanced();
 // Custom files use original state bytes, UTF-8 paths and OUT preservation.
 const std::string filename=F::pathToUtf8(std::filesystem::temp_directory_path()/(std::string("s3g-stereo-")+descriptor.id+"-é-音.s3gpreset"));
 check(v.presetFile(filename,true),"preset write");double preserved=-19.;set(p,v,e,kOutputParamId,preserved);
 check(v.presetFile(filename,false),"preset load");flush(p,v,e);check(near(value(p,kOutputParamId),preserved),"custom preset preserves OUT");
 // Backpressure must preserve the queued action, with no dangling gestures.
 e.reject=true;v.editValue(kOutputParamId,-17);paramsFlush(&p.plugin,nullptr,&e.output);
 check(p.guiParamEvents.available()<511,"host-rejected edits remain queued");
 e.reject=false;flush(p,v,e);check(near(value(p,kOutputParamId),-17),"retry delivers pending edit");e.balanced();
 Stream saved;check(stateSave(&p.plugin,&saved.output),"chunked state save");
 check(stateLoad(&p.plugin,&saved.input),"chunked state recall");v.service();
#if S3G_STEREO_KIND == 3
 set(p,v,e,kScoreEnableParamId,1);
#endif
 check(activate(&p.plugin,48000,1,128),"activate audio");audio(p,v,e,48,24);render(v,"live");
#if S3G_STEREO_KIND == 3
 check(v._presentedScoreRow>=0,"Stack live score playhead");
#endif
 deactivate(&p.plugin);v.stopRefresh();flush(p,v,e);e.balanced();
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
      CreateWindowExW(0, L"STATIC", L"s3g environmental GUI test",
                      WS_OVERLAPPEDWINDOW, 0, 0, kGuiWidth, kGuiHeight, nullptr,
                      nullptr, GetModuleHandleW(nullptr), nullptr);
#endif
  clap_host_t host{};
  host.clap_version = CLAP_VERSION_INIT;
  host.name = "Stereo processor parity";
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
      check(std::abs(double(w) / h - double(kGuiWidth) / kGuiHeight) < .002,
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
    std::cout << "Stereo processor canvas parity passed: " << descriptor.id
              << '\n';
  return ok ? 0 : 1;
}
