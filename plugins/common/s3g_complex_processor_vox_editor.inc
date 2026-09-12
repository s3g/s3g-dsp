// Shared platform boundary; canvas methods below retain the Cocoa hit maps.
std::string error;
bool cancelled = false;
std::vector<clap_id> gestures;
drawing::Rect bounds() const {
  return drawing::makeRect(0, 0, kGuiWidth, kGuiHeight);
}
drawing::Point convertPoint_fromView(drawing::Point p, void *) { return p; }
bool isHidden() const { return false; }
void setNeedsDisplay(bool) { invalid(); }
void focus() {
  if (getFrame())
    getFrame()->setFocusView(this);
}
bool wantsFocus() const override { return true; }
void beep() {
  if (!cancelled && error.empty())
    error = "ACTION UNAVAILABLE / HOST QUEUE BUSY";
  cancelled = false;
  invalid();
}
const clap_host_params_t *hostParams() const {
  return _plugin->host && _plugin->host->get_extension
             ? static_cast<const clap_host_params_t *>(
                   _plugin->host->get_extension(_plugin->host, CLAP_EXT_PARAMS))
             : nullptr;
}
void request() {
  s3g::clap_gui::requestParamEventService(_plugin->host, hostParams());
}
void markStateDirty() {
  if (!_plugin->host || !_plugin->host->get_extension)
    return;
  const auto *state = static_cast<const clap_host_state_t *>(
      _plugin->host->get_extension(_plugin->host, CLAP_EXT_STATE));
  if (state && state->mark_dirty)
    state->mark_dirty(_plugin->host);
}
bool beginEdit(clap_id id) {
  if (std::find(gestures.begin(), gestures.end(), id) != gestures.end())
    return true;
  if (_plugin->guiParamEvents.available() <= gestures.size() + 8u) {
    beep();
    return false;
  }
  if (!_plugin->guiParamEvents.push(
          {s3g::clap_gui::ParamEventKind::GestureBegin, id, 0.}))
    return false;
  gestures.push_back(id);
  request();
  return true;
}
bool endEdit(clap_id id) {
  auto it = std::find(gestures.begin(), gestures.end(), id);
  if (it == gestures.end())
    return true;
  if (!_plugin->guiParamEvents.push(
          {s3g::clap_gui::ParamEventKind::GestureEnd, id, 0.}))
    return false;
  gestures.erase(it);
  request();
  return true;
}
void finishGestures() {
  for (size_t n = gestures.size(); n > 0; --n)
    endEdit(gestures[n - 1]);
}
bool editValue(clap_id id, double v, bool continuous = false) {
  if (!std::isfinite(v))
    return false;
  bool valid = false;
  for (uint32_t n = 0; n < paramsCount(&_plugin->plugin); ++n) {
    clap_param_info_t info{};
    if (paramsGetInfo(&_plugin->plugin, n, &info) && info.id == id &&
        !(info.flags & CLAP_PARAM_IS_READONLY)) {
      v = std::clamp(v, info.min_value, info.max_value);
      if (info.flags & CLAP_PARAM_IS_STEPPED)
        v = std::round(v);
      valid = true;
      break;
    }
  }
  if (!valid || _plugin->guiParamEvents.available() <= gestures.size() + 8u) {
    beep();
    return false;
  }
  bool single = !continuous && std::find(gestures.begin(), gestures.end(),
                                         id) == gestures.end();
  if (!beginEdit(id))
    return false;
  _plugin->publishedParams[id].store(v, std::memory_order_release);
  _plugin->guiParamEvents.push({s3g::clap_gui::ParamEventKind::Value, id, v});
  if (single)
    endEdit(id);
  error.clear();
  request();
  markStateDirty();
  return true;
}
#include "s3g_complex_processor_vox_editor_platform.inc"
#include "s3g_complex_processor_vox_presets.inc"
