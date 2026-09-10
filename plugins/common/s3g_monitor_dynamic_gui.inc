// Include inside the plug-in's anonymous namespace after
// makePortableCanvas(Plugin&) has been defined. Plugin owns an EditorHost*.

bool portableGuiIsApiSupported(const clap_plugin_t *, const char *api,
                               bool floating) {
  return s3g::clap_gui::portable::isApiSupported(api, floating);
}

bool portableGuiGetPreferredApi(const clap_plugin_t *, const char **api,
                                bool *floating) {
  return s3g::clap_gui::portable::getPreferredApi(api, floating);
}

bool portableGuiCreate(const clap_plugin_t *plugin, const char *api,
                       bool floating) {
  auto &instance = *self(plugin);
  return s3g::clap_gui::portable::create(
      instance.portableGuiEditor, api, floating, [&instance]() {
        auto *editor =
            new (std::nothrow) s3g::portable_gui::foundation::EditorHost(
                kGuiWidth, kGuiHeight, instance.portableGuiWidth,
                instance.portableGuiHeight);
        if (!editor)
          return editor;
        if (!editor->ready() || !editor->attach(makePortableCanvas(instance))) {
          delete editor;
          return static_cast<s3g::portable_gui::foundation::EditorHost *>(
              nullptr);
        }
        return editor;
      });
}

void destroyPortableGui(Plugin &instance) {
  instance.portableGuiVisible = false;
  s3g::clap_gui::portable::destroy(instance.portableGuiEditor,
                                   [](auto *editor) { delete editor; });
}

void portableGuiDestroy(const clap_plugin_t *plugin) {
  destroyPortableGui(*self(plugin));
}

bool portableGuiSetScale(const clap_plugin_t *, double) { return false; }

bool portableGuiGetSize(const clap_plugin_t *plugin, uint32_t *width,
                        uint32_t *height) {
  const auto *instance = self(plugin);
  return s3g::clap_gui::portable::getSize(
      instance->portableGuiWidth, instance->portableGuiHeight, width, height);
}

bool portableGuiCanResize(const clap_plugin_t *) { return true; }

bool portableGuiGetResizeHints(const clap_plugin_t *,
                               clap_gui_resize_hints_t *hints) {
  if (!hints)
    return false;
  hints->can_resize_horizontally = hints->can_resize_vertically = true;
  hints->preserve_aspect_ratio = false;
  hints->aspect_ratio_width = hints->aspect_ratio_height = 0;
  return true;
}

bool portableGuiAdjustSize(const clap_plugin_t *, uint32_t *width,
                           uint32_t *height) {
  if (!width || !height)
    return false;
  // Retain the analyzer's independent axes and original minimum extents.
  *width = std::clamp(*width, 720u, kGuiWidth * 2u);
  *height = std::clamp(*height, 430u, kGuiHeight * 2u);
  return true;
}

bool portableGuiSetSize(const clap_plugin_t *plugin, uint32_t width,
                        uint32_t height) {
  auto &instance = *self(plugin);
  if (!portableGuiAdjustSize(plugin, &width, &height))
    return false;
  if (instance.portableGuiEditor &&
      !instance.portableGuiEditor->setSize(width, height))
    return false;
  instance.portableGuiWidth = width;
  instance.portableGuiHeight = height;
  return true;
}

bool portableGuiSetParent(const clap_plugin_t *plugin,
                          const clap_window_t *window) {
  return s3g::clap_gui::portable::setParent(
      self(plugin)->portableGuiEditor, window,
      [](auto *editor, void *parent) { return editor->setParent(parent); });
}

bool portableGuiSetTransient(const clap_plugin_t *, const clap_window_t *) {
  return false;
}
void portableGuiSuggestTitle(const clap_plugin_t *, const char *) {}

bool portableGuiShow(const clap_plugin_t *plugin) {
  auto &instance = *self(plugin);
  return s3g::clap_gui::portable::setVisible(
      instance.portableGuiEditor, instance.portableGuiVisible, true,
      [](auto *editor, bool visible) { return editor->setVisible(visible); });
}

bool portableGuiHide(const clap_plugin_t *plugin) {
  auto &instance = *self(plugin);
  return s3g::clap_gui::portable::setVisible(
      instance.portableGuiEditor, instance.portableGuiVisible, false,
      [](auto *editor, bool visible) { return editor->setVisible(visible); });
}

const clap_plugin_gui_t portableGui{
    portableGuiIsApiSupported, portableGuiGetPreferredApi,
    portableGuiCreate,         portableGuiDestroy,
    portableGuiSetScale,       portableGuiGetSize,
    portableGuiCanResize,      portableGuiGetResizeHints,
    portableGuiAdjustSize,     portableGuiSetSize,
    portableGuiSetParent,      portableGuiSetTransient,
    portableGuiSuggestTitle,   portableGuiShow,
    portableGuiHide,
};
