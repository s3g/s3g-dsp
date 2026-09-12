// Common services for literal terrain/map canvases. Included inside Editor.
std::string error;
std::vector<clap_id> gestures;
bool pointerEditing = false;
bool refreshActive = false;
void startRefresh() override { Canvas::startRefresh(); refreshActive = true; }
drawing::Rect bounds() const { return drawing::makeRect(0, 0, kGuiWidth, kGuiHeight); }
drawing::Point convertPoint_fromView(drawing::Point p, void*) { return p; }
void setNeedsDisplay(bool) { invalid(); }
void beep() { error = "HOST PARAMETER QUEUE BUSY"; invalid(); }
static uint32_t randomBits() {
  static thread_local std::mt19937 rng(std::random_device{}());
  return rng();
}
static uint32_t randomBounded(uint32_t upper) {
  if (!upper) return 0;
  const uint32_t threshold = uint32_t(-upper) % upper;
  uint32_t bits;
  do { bits = randomBits(); } while (bits < threshold);
  return bits % upper;
}
void markStateDirty() {
  if (!_plugin->host || !_plugin->host->get_extension) return;
  const auto* state = static_cast<const clap_host_state_t*>(
      _plugin->host->get_extension(_plugin->host, CLAP_EXT_STATE));
  if (state && state->mark_dirty) state->mark_dirty(_plugin->host);
}
void editBegin(clap_id id) {
  if (std::find(gestures.begin(), gestures.end(), id) != gestures.end()) return;
  if (_plugin->guiParamEvents.available() <= gestures.size() + 3u) { beep(); return; }
  if (queueGuiParamEvent(*_plugin, Kind::GestureBegin, id)) gestures.push_back(id);
}
void editEnd(clap_id id) {
  const auto found = std::find(gestures.begin(), gestures.end(), id);
  if (found != gestures.end() && queueGuiParamEvent(*_plugin, Kind::GestureEnd, id))
    gestures.erase(found);
}
void finishGestures() {
  for (size_t n = gestures.size(); n > 0; --n) editEnd(gestures[n - 1]);
}
bool canonicalValue(clap_id id, double& value) {
  if (!std::isfinite(value)) return false;
  for (uint32_t index = 0; index < std::size(kParams); ++index) {
    clap_param_info_t info{};
    if (paramsGetInfo(&_plugin->plugin, index, &info) && info.id == id) {
      value = std::clamp(value, info.min_value, info.max_value);
      if (info.flags & CLAP_PARAM_IS_STEPPED) value = std::round(value);
      return true;
    }
  }
  return false;
}
void editValue(clap_id id, double value) {
  if (!canonicalValue(id, value)) return;
  if (_plugin->guiParamEvents.available() <= gestures.size() + 3u) { beep(); return; }
  editBegin(id);
  if (std::find(gestures.begin(), gestures.end(), id) == gestures.end()) return;
  if (!queueGuiParamEvent(*_plugin, Kind::Value, id, value)) { beep(); return; }
#if S3G_MAP_KIND == 3
  _plugin->publishedParams[paramIndex(id)].store(value, std::memory_order_relaxed);
#endif
  if (!pointerEditing) editEnd(id);
  error.clear();
  markStateDirty();
}
void editSet(clap_id id, double value) {
  const bool editing = pointerEditing;
  pointerEditing = false;
  editValue(id, value);
  pointerEditing = editing;
}
#if S3G_MAP_KIND == 3
void resetLayout() {
  if (!queueGuiParamEvent(*_plugin, Kind::Value, kMapResetLayoutAction)) beep();
}
#endif
void service() override {
#if S3G_MAP_KIND == 1
  flushPendingDragValue();
  acceptSurfaceImage();
#elif S3G_MAP_KIND == 2
  if (!_plugin->active.load(std::memory_order_acquire)) snapshotGui(*_plugin);
#else
  _snapshot = readMapSnapshot(*_plugin);
  if (_viewRevision != _plugin->guiViewRevision.load(std::memory_order_acquire))
    loadViewState();
#endif
}
void stopRefresh() override {
  Canvas::stopRefresh();
  refreshActive = false;
  drawing::Event event{};
  mouseUp(&event);
#if S3G_MAP_KIND == 1
  flushPendingDragValue();
  _openMenu = 0;
#elif S3G_MAP_KIND == 2
  _openMenuParam = 0;
#else
  _openMenu = CLAP_INVALID_ID;
#endif
  finishGestures();
  pointerEditing = false;
  _hoverMenuItem = -1;
}
void paint() override {
  service();
  drawing::DrawingScope scope(*this, context, font, titleFont, &error);
  drawOriginal(bounds());
}
bool pointerDown(VSTGUI::MouseDownEvent& event) override {
  if (!event.buttonState.isLeft()) return false;
  // Reserve enough for a complete RANDOM/factory gesture batch, plus all
  // outstanding gesture ends. A host rejecting events cannot cause half a preset.
  if (_plugin->guiParamEvents.available() < std::size(kParams) * 3u + 16u) {
    beep(); return true;
  }
  finishGestures();
  service();
  pointerEditing = true;
  drawing::Event e{{event.mousePosition.x, event.mousePosition.y}, int(event.clickCount)};
  mouseDown(&e);
#if S3G_MAP_KIND == 3
  const bool dragging = _dragParam != CLAP_INVALID_ID || _dragListener || _dragSite >= 0;
#else
  const bool dragging = _dragParam != 0;
#endif
  if (!dragging) { finishGestures(); pointerEditing = false; }
  markStateDirty();
  return true;
}
void pointerMove(VSTGUI::MouseMoveEvent& event) override {
  drawing::Event e{{event.mousePosition.x, event.mousePosition.y}};
  if (event.buttonState.isLeft()) mouseDragged(&e); else mouseMoved(&e);
}
void pointerUp(VSTGUI::MouseUpEvent& event) override {
  drawing::Event e{{event.mousePosition.x, event.mousePosition.y}};
  mouseUp(&e);
  finishGestures();
  pointerEditing = false;
}
const clap_plugin_state_t& presetCodec() const {
#if S3G_MAP_KIND == 3
  return stateExtension;
#else
  return stateExt;
#endif
}
bool presetFile(const std::string& path, bool save) {
  bool success = false;
  if (save) {
    success = s3g::clap_gui::portable::saveStateFile(
        &_plugin->plugin, presetCodec(), path.c_str());
  } else {
    // Decode transactionally into an unactivated candidate before replacing
    // live state. Reuse the original versioned codec and preserve the user's OUT.
    auto candidate = std::make_unique<Plugin>();
    candidate->plugin = _plugin->plugin;
    candidate->plugin.plugin_data = candidate.get();
    if (!s3g::clap_gui::portable::loadStateFile(
            &candidate->plugin, presetCodec(), path.c_str())) return false;
    double output = 0.;
    if (!paramsGetValue(&_plugin->plugin, kOutputParamId, &output)) return false;
    applyParam(*candidate, kOutputParamId, output);
    struct Memory {
      std::vector<uint8_t> bytes;
      size_t offset = 0;
      clap_ostream_t out{this, [](const clap_ostream_t* stream, const void* data, uint64_t size) -> int64_t {
        auto& self = *static_cast<Memory*>(stream->ctx);
        const auto* bytes = static_cast<const uint8_t*>(data);
        self.bytes.insert(self.bytes.end(), bytes, bytes + size);
        return int64_t(size);
      }};
      clap_istream_t in{this, [](const clap_istream_t* stream, void* data, uint64_t size) -> int64_t {
        auto& self = *static_cast<Memory*>(stream->ctx);
        size = std::min<uint64_t>(size, self.bytes.size() - self.offset);
        std::memcpy(data, self.bytes.data() + self.offset, size);
        self.offset += size;
        return int64_t(size);
      }};
    } memory;
    success = presetCodec().save(&candidate->plugin, &memory.out) &&
        presetCodec().load(&_plugin->plugin, &memory.in);
  }
  if (success) {
    const auto label = drawing::uppercaseString(foundation::pathToUtf8(
        foundation::pathFromUtf8(path.c_str()).stem()));
#if S3G_MAP_KIND == 3
    std::snprintf(_presetName, sizeof(_presetName), "%s", label.c_str());
    _factoryPresetIndex = -1;
#else
    std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s", label.c_str());
#endif
    markStateDirty();
    service();
  }
  return success;
}
void presetDialog(bool save) {
  finishGestures();
  foundation::FileDialogOptions options;
  options.save = save;
  options.title = std::string(save ? "Save " : "Load ") + portablePresetDirectory + " preset";
  options.extensionDescription = "s3g preset";
  options.extension = "s3gpreset";
  options.defaultSaveName = "Custom.s3gpreset";
  options.initialDirectory = foundation::presetDirectory(portablePresetDirectory);
  const auto path = foundation::runFileDialog(getFrame(), options);
  if (path.empty()) return;
  error = presetFile(path, save) ? "" : "PRESET FILE COULD NOT BE READ / WRITTEN";
  invalid();
}
