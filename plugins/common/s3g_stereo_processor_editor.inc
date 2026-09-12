// Platform services shared by the four literal canvas adaptations.
std::string error;
bool cancelled=false;
uint32_t pointerModifiers=0;
std::vector<clap_id> gestures;
drawing::Rect bounds() const { return drawing::makeRect(0,0,kGuiWidth,kGuiHeight); }
drawing::Point convertPoint_fromView(drawing::Point p, void*) { return p; }
bool isHidden() const { return false; }
void setNeedsDisplay(bool) { invalid(); }
void setNeedsDisplayInRect(drawing::Rect) { invalid(); }
void focus() { if (getFrame()) getFrame()->setFocusView(this); }
bool wantsFocus() const override { return true; }
void beep() { if (!cancelled) error="ACTION UNAVAILABLE / HOST QUEUE BUSY"; cancelled=false; invalid(); }
const clap_host_params_t* hostParams() const {
  return _plugin->host && _plugin->host->get_extension
    ? static_cast<const clap_host_params_t*>(_plugin->host->get_extension(_plugin->host,CLAP_EXT_PARAMS)) : nullptr;
}
void request() { s3g::clap_gui::requestParamEventService(_plugin->host,hostParams()); }
void markStateDirty() {
  if (!_plugin->host || !_plugin->host->get_extension) return;
  auto* state=static_cast<const clap_host_state_t*>(_plugin->host->get_extension(_plugin->host,CLAP_EXT_STATE));
  if (state && state->mark_dirty) state->mark_dirty(_plugin->host);
}
bool beginEdit(clap_id id) {
  if (std::find(gestures.begin(),gestures.end(),id)!=gestures.end()) return true;
  if (_plugin->guiParamEvents.available()<=gestures.size()+4u) { beep(); return false; }
  if (!_plugin->guiParamEvents.push({s3g::clap_gui::ParamEventKind::GestureBegin,id,0.})) return false;
  gestures.push_back(id); request(); return true;
}
void endEdit(clap_id id) {
  auto found=std::find(gestures.begin(),gestures.end(),id);
  if (found==gestures.end()) return;
  if (_plugin->guiParamEvents.push({s3g::clap_gui::ParamEventKind::GestureEnd,id,0.})) {
    gestures.erase(found); request();
  }
}
void finishGestures() { while (!gestures.empty()) endEdit(gestures.back()); }
void editValue(clap_id id,double requested) {
  if (!std::isfinite(requested)) return;
  bool valid=false;
  for (uint32_t i=0;i<paramsCount(&_plugin->plugin);++i) {
    clap_param_info_t info{};
    if (paramsGetInfo(&_plugin->plugin,i,&info) && info.id==id) {
      requested=std::clamp(requested,info.min_value,info.max_value);
      if (info.flags&CLAP_PARAM_IS_STEPPED) requested=std::round(requested);
      valid=true; break;
    }
  }
  if (!valid || _plugin->guiParamEvents.available()<=gestures.size()+4u) { beep(); return; }
  const bool single=std::find(gestures.begin(),gestures.end(),id)==gestures.end();
  if (!beginEdit(id)) return;
  publishParam(*_plugin,id,requested);
  _plugin->guiParamEvents.push({s3g::clap_gui::ParamEventKind::Value,id,requested});
  request(); if(single) endEdit(id); error.clear(); markStateDirty();
}
// Keep original handler call sites while supplying queue capacity/gesture guards.
void queueGuiParamGestureBegin(Plugin&,clap_id id) { beginEdit(id); }
void queueGuiParamGestureEnd(Plugin&,clap_id id) { endEdit(id); }
void queueGuiParamValue(Plugin&,clap_id id,double v) { editValue(id,v); }
void queueGuiParamGesture(Plugin&,clap_id id,double v) { editValue(id,v); }
double refreshPeriodMilliseconds() const override {
#if S3G_STEREO_KIND == 3
  return 1000./60.;
#elif S3G_STEREO_KIND == 4
  return 50.;
#else
  return 1000./30.;
#endif
}
void startRefresh() override { service(); Canvas::startRefresh(); }
void stopRefresh() override {
  Canvas::stopRefresh(); drawing::Event e{}; mouseUp(&e); finishGestures();
  _openMenu=
#if S3G_STEREO_KIND == 4
    0;
#else
    CLAP_INVALID_ID;
#endif
}
~Editor() override { stopRefresh(); }
void paint() override {
  drawing::DrawingScope scope(*this,context,font,titleFont,&error);
  drawOriginal(bounds());
}
bool pointerDown(VSTGUI::MouseDownEvent& e) override {
  if (!e.buttonState.isLeft() && !e.buttonState.isRight()) return false;
  finishGestures();
  if (_plugin->guiParamEvents.available()<paramsCount(&_plugin->plugin)*3u+32u) { beep(); return true; }
  focus(); service(); cancelled=false;
  pointerModifiers=(e.modifiers.has(VSTGUI::ModifierKey::Shift)?1u:0u)
                 |(e.modifiers.has(VSTGUI::ModifierKey::Alt)?2u:0u);
  drawing::Event event{{e.mousePosition.x,e.mousePosition.y},int(e.clickCount)};
#if S3G_STEREO_KIND == 2 || S3G_STEREO_KIND == 3
  if (e.buttonState.isRight()) rightMouseDown(&event); else
#endif
  if (e.buttonState.isLeft()) mouseDown(&event);
  markStateDirty(); return true;
}
void pointerMove(VSTGUI::MouseMoveEvent& e) override {
  drawing::Event event{{e.mousePosition.x,e.mousePosition.y},1};
  if(e.buttonState.isLeft()) {
    if(_plugin->guiParamEvents.available()>gestures.size()+16u) mouseDragged(&event);
  }
#if S3G_STEREO_KIND != 4
  else mouseMoved(&event);
#endif
}
void pointerUp(VSTGUI::MouseUpEvent& e) override {
  drawing::Event event{{e.mousePosition.x,e.mousePosition.y},1};
  mouseUp(&event); finishGestures(); markStateDirty();
}
bool presetFile(const std::string& path,bool save) {
  if(save) return s3g::clap_gui::portable::saveStateFile(&_plugin->plugin,stateExt,path.c_str());
  if(_plugin->guiParamEvents.available()<paramsCount(&_plugin->plugin)*3u+32u) return false;
  auto staged=std::make_unique<Plugin>(); staged->plugin.plugin_data=staged.get();
  if(!s3g::clap_gui::portable::loadStateFile(&staged->plugin,stateExt,path.c_str())) return false;
  for(uint32_t i=0;i<paramsCount(&_plugin->plugin);++i) {
    clap_param_info_t info{}; double v=0.;
    if(paramsGetInfo(&_plugin->plugin,i,&info) && info.id!=kOutputParamId
       && paramsGetValue(&staged->plugin,info.id,&v)) editValue(info.id,v);
  }
#if S3G_STEREO_KIND == 3
  storeScoreProgram(*_plugin,scoreProgramSnapshot(*staged),true);
#endif
  const auto* params=hostParams(); if(params && params->rescan) params->rescan(_plugin->host,CLAP_PARAM_RESCAN_VALUES);
  markStateDirty(); service(); invalid(); return true;
}
bool presetDialog(bool save,drawing::String* name=nullptr) {
  finishGestures(); foundation::FileDialogOptions options;
  options.save=save; options.title=std::string(save?"Save ":"Load ")+portablePresetDirectory+" preset";
  options.extension="s3gpreset"; options.extensionDescription="s3g Preset";
  options.initialDirectory=foundation::presetDirectory(portablePresetDirectory);
  options.defaultSaveName="Preset.s3gpreset";
  auto path=foundation::runFileDialog(getFrame(),options);
  if(path.empty()) {cancelled=true;return false;}
  bool result=presetFile(path,save);
  error=result?"":"PRESET FILE COULD NOT BE READ / WRITTEN";
  if(result && name) *name=foundation::pathToUtf8(foundation::pathFromUtf8(path.c_str()).stem());
  invalid();return result;
}
#if S3G_STEREO_KIND == 4
bool handleProcessorTitleClick(drawing::Point point,const clap_plugin_t*,const char*,
    const s3g::gui_layout::EncoderTitleBand& band,char* name,size_t size,clap_id) {
  bool save=drawing::contains(point,drawing::cocoaRect(band.saveButton));
  bool load=drawing::contains(point,drawing::cocoaRect(band.loadButton));
  if(!save && !load) return false;
  drawing::String selected;
  if(presetDialog(save,&selected)) {
    std::snprintf(name,size,"%s",selected.c_str());_factoryPresetIndex=-1;
  } else beep();
  return true;
}
void editConduitParams(const s3g::ProcessorConduitParams& params) {
  for(const auto& def:kParamDefs) { double v=0.; conduitParamsValue(params,def.id,&v); editValue(def.id,v); }
  _snapshot=portableConduitSnapshot(*_plugin);
}
#endif
#if S3G_STEREO_KIND == 3
#include "s3g_stereo_processor_stack_keyboard.inc"
#endif
