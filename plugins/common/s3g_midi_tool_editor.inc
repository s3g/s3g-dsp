// Shared platform boundary; canvas methods below retain the Cocoa hit maps.
std::string error;
bool cancelled = false;
std::vector<clap_id> gestures;
drawing::Rect bounds() const { return drawing::makeRect(0,0,kGuiWidth,kGuiHeight); }
drawing::Point convertPoint_fromView(drawing::Point p, void*) { return p; }
bool isHidden() const { return false; }
void setNeedsDisplay(bool) { invalid(); }
void focus() { if(getFrame()) getFrame()->setFocusView(this); }
bool wantsFocus() const override { return true; }
void beep() { if(!cancelled && error.empty()) error="ACTION UNAVAILABLE / HOST QUEUE BUSY"; cancelled=false; invalid(); }
const clap_host_params_t* hostParams() const {
  return _plugin->host && _plugin->host->get_extension ? static_cast<const clap_host_params_t*>(
      _plugin->host->get_extension(_plugin->host,CLAP_EXT_PARAMS)) : nullptr;
}
void request() { s3g::clap_gui::requestParamEventService(_plugin->host,hostParams()); }
void markStateDirty() {
  if(!_plugin->host || !_plugin->host->get_extension) return;
  const auto* state=static_cast<const clap_host_state_t*>(_plugin->host->get_extension(_plugin->host,CLAP_EXT_STATE));
  if(state && state->mark_dirty) state->mark_dirty(_plugin->host);
}
bool beginEdit(clap_id id) {
  if(std::find(gestures.begin(),gestures.end(),id)!=gestures.end()) return true;
  if(_plugin->guiParamEvents.available()<=gestures.size()+8u) {beep();return false;}
  if(!_plugin->guiParamEvents.push({s3g::clap_gui::ParamEventKind::GestureBegin,id,0.})) return false;
  gestures.push_back(id);request();return true;
}
bool endEdit(clap_id id) {
  auto it=std::find(gestures.begin(),gestures.end(),id);
  if(it==gestures.end())return true;
  if(!_plugin->guiParamEvents.push({s3g::clap_gui::ParamEventKind::GestureEnd,id,0.}))return false;
  gestures.erase(it);request();return true;
}
void finishGestures() { for(size_t n=gestures.size();n>0;--n)endEdit(gestures[n-1]); }
bool editValue(clap_id id,double v,bool continuous=false) {
  if(!std::isfinite(v))return false;
  bool valid=false;
  for(uint32_t n=0;n<paramsCount(&_plugin->plugin);++n) {
    clap_param_info_t info{};
    if(paramsGetInfo(&_plugin->plugin,n,&info)&&info.id==id && !(info.flags&CLAP_PARAM_IS_READONLY)) {
      v=std::clamp(v,info.min_value,info.max_value);
      if(info.flags&CLAP_PARAM_IS_STEPPED)v=std::round(v);
      valid=true;break;
    }
  }
  if(!valid || _plugin->guiParamEvents.available()<=gestures.size()+8u){beep();return false;}
  bool single=!continuous && std::find(gestures.begin(),gestures.end(),id)==gestures.end();
  if(!beginEdit(id))return false;
  publishPortableMidiParam(*_plugin,id,v);
  _plugin->guiParamEvents.push({s3g::clap_gui::ParamEventKind::Value,id,v});
  if(single)endEdit(id);
  error.clear();request();markStateDirty();return true;
}
#if S3G_MIDI_TOOL_KIND == 1
void requestGuiCommand(Plugin& p,uint32_t command) {
  if(command==kGuiToggleRecord) {editValue(kRecordParamId,p.uiRecording.load()?0.:1.);return;}
  if(command==kGuiTogglePlay) {editValue(kPlayParamId,p.uiPlaying.load()?0.:1.);return;}
  if(command==kGuiSetTakeover) {editValue(kTakeoverParamId,p.guiTakeoverMs.load(),_draggingTakeover);return;}
  if(command==kGuiClearAll) {editValue(kClearAllParamId,1.);editValue(kClearAllParamId,0.);return;}
  const bool cancel=command==kGuiCancelRecord;
  const clap_id hostId=cancel?kRecordParamId:kClearLastParamId;
  if(p.guiParamEvents.available()<=gestures.size()+8u || !beginEdit(hostId)){beep();return;}
  p.guiParamEvents.push({s3g::clap_gui::ParamEventKind::Value,
      cancel?kPortableCancelRecord:kPortableClearSelected,
      cancel?0.:double(p.uiSelectedIndex.load(std::memory_order_relaxed))});
  if(!cancel)p.guiParamEvents.push({s3g::clap_gui::ParamEventKind::Value,kClearLastParamId,0.});
  endEdit(hostId);request();markStateDirty();
}
void mouseMoved(drawing::Event*) {}
#else
void queueGuiBegin(Plugin&,clap_id id){beginEdit(id);}
void queueGuiValue(Plugin&,clap_id id,double v){editValue(id,v);}
void queueGuiEnd(Plugin&,clap_id id){endEdit(id);}
bool presetFile(const std::string& path,bool save) {
  if(save)return s3g::clap_gui::portable::saveStateFile(&_plugin->plugin,stateExtension,path.c_str());
  auto staged=std::make_unique<Plugin>();staged->plugin.plugin_data=staged.get();
  if(!s3g::clap_gui::portable::loadStateFile(&staged->plugin,stateExtension,path.c_str()))return false;
  return queuePortableMidiPreset(*_plugin,*staged);
}
bool presetDialog(bool save,drawing::String* name) {
  finishGestures(); foundation::FileDialogOptions options;
  options.save=save;options.title=save?"Save Relay preset":"Load Relay preset";
  options.extension="s3gpreset";options.extensionDescription="s3g Preset";
  options.initialDirectory=foundation::presetDirectory("Relay");options.defaultSaveName="Preset.s3gpreset";
  auto path=foundation::runFileDialog(getFrame(),options);
  if(path.empty()){cancelled=true;return false;}
  const bool ok=presetFile(path,save);
  if(!ok)error="PRESET FILE COULD NOT BE READ / WRITTEN";
  else {error.clear();markStateDirty();if(name)*name=foundation::pathToUtf8(foundation::pathFromUtf8(path.c_str()).stem());}
  invalid();return ok;
}
#endif
double refreshPeriodMilliseconds()const override{return 1000./30.;}
void startRefresh()override{service();Canvas::startRefresh();}
void stopRefresh()override {
  Canvas::stopRefresh();drawing::Event event{};mouseUp(&event);finishGestures();
#if S3G_MIDI_TOOL_KIND == 2
  _openMenu=-1;_hoverMenuItem=-1;_menuItemCount=0;
#endif
}
~Editor()override{stopRefresh();}
void paint()override{drawing::DrawingScope scope(*this,context,font,titleFont,&error);drawOriginal(bounds());}
bool pointerDown(VSTGUI::MouseDownEvent& e)override {
  if(!e.buttonState.isLeft())return false;
  finishGestures();
  if(_plugin->guiParamEvents.available()<paramsCount(&_plugin->plugin)*3u+32u){beep();return true;}
  focus();service();cancelled=false;
  drawing::Event event{{e.mousePosition.x,e.mousePosition.y},int(e.clickCount)};
  mouseDown(&event);markStateDirty();return true;
}
void pointerMove(VSTGUI::MouseMoveEvent& e)override {
  drawing::Event event{{e.mousePosition.x,e.mousePosition.y},1};
  if(e.buttonState.isLeft()){if(_plugin->guiParamEvents.available()>gestures.size()+8u)mouseDragged(&event);}
  else mouseMoved(&event);
}
void pointerUp(VSTGUI::MouseUpEvent& e)override {
  drawing::Event event{{e.mousePosition.x,e.mousePosition.y},1};mouseUp(&event);finishGestures();markStateDirty();
}
