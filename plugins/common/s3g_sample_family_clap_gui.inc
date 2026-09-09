// Include inside the plug-in's anonymous namespace after
// makeSampleFamilyEditorConfig(Plugin&) has been defined.

bool portableGuiIsApiSupported(
    const clap_plugin_t*, const char* api, bool floating)
{
    return s3g::clap_gui::portable::isApiSupported(api, floating);
}

bool portableGuiGetPreferredApi(
    const clap_plugin_t*, const char** api, bool* floating)
{
    return s3g::clap_gui::portable::getPreferredApi(api, floating);
}

bool portableGuiCreate(
    const clap_plugin_t* plugin, const char* api, bool floating)
{
    auto& instance = *self(plugin);
    return s3g::clap_gui::portable::create(
        instance.portableGuiEditor, api, floating, [&instance]() {
            return s3g::portable_gui::createSampleFamilyEditor(
                makeSampleFamilyEditorConfig(instance),
                instance.portableGuiWidth, instance.portableGuiHeight);
        });
}

void destroyPortableGui(Plugin& instance)
{
    instance.portableGuiVisible = false;
    s3g::clap_gui::portable::destroy(instance.portableGuiEditor,
        s3g::portable_gui::destroySampleFamilyEditor);
}

void portableGuiDestroy(const clap_plugin_t* plugin)
{
    destroyPortableGui(*self(plugin));
}

bool portableGuiSetScale(const clap_plugin_t*, double) { return false; }

bool portableGuiGetSize(const clap_plugin_t* plugin,
    uint32_t* width, uint32_t* height)
{
    const auto* instance = self(plugin);
    return s3g::clap_gui::portable::getSize(instance->portableGuiWidth,
        instance->portableGuiHeight, width, height);
}

bool portableGuiCanResize(const clap_plugin_t*) { return true; }

bool portableGuiGetResizeHints(
    const clap_plugin_t*, clap_gui_resize_hints_t* hints)
{
    return s3g::clap_gui::portable::getResizeHints(
        kGuiWidth, kGuiHeight, hints);
}

bool portableGuiAdjustSize(
    const clap_plugin_t*, uint32_t* width, uint32_t* height)
{
    return s3g::clap_gui::portable::adjustSize(
        kGuiWidth, kGuiHeight, width, height);
}

bool portableGuiSetSize(
    const clap_plugin_t* plugin, uint32_t width, uint32_t height)
{
    auto& instance = *self(plugin);
    return s3g::clap_gui::portable::setSize(instance.portableGuiEditor,
        kGuiWidth, kGuiHeight,
        instance.portableGuiWidth, instance.portableGuiHeight,
        width, height, s3g::portable_gui::setSampleFamilyEditorSize);
}

bool portableGuiSetParent(
    const clap_plugin_t* plugin, const clap_window_t* window)
{
    return s3g::clap_gui::portable::setParent(
        self(plugin)->portableGuiEditor, window,
        s3g::portable_gui::setSampleFamilyEditorParent);
}

bool portableGuiSetTransient(
    const clap_plugin_t*, const clap_window_t*) { return false; }
void portableGuiSuggestTitle(const clap_plugin_t*, const char*) {}

bool portableGuiShow(const clap_plugin_t* plugin)
{
    auto& instance = *self(plugin);
    return s3g::clap_gui::portable::setVisible(instance.portableGuiEditor,
        instance.portableGuiVisible, true,
        s3g::portable_gui::setSampleFamilyEditorVisible);
}

bool portableGuiHide(const clap_plugin_t* plugin)
{
    auto& instance = *self(plugin);
    return s3g::clap_gui::portable::setVisible(instance.portableGuiEditor,
        instance.portableGuiVisible, false,
        s3g::portable_gui::setSampleFamilyEditorVisible);
}

const clap_plugin_gui_t portableGui {
    portableGuiIsApiSupported,
    portableGuiGetPreferredApi,
    portableGuiCreate,
    portableGuiDestroy,
    portableGuiSetScale,
    portableGuiGetSize,
    portableGuiCanResize,
    portableGuiGetResizeHints,
    portableGuiAdjustSize,
    portableGuiSetSize,
    portableGuiSetParent,
    portableGuiSetTransient,
    portableGuiSuggestTitle,
    portableGuiShow,
    portableGuiHide,
};
