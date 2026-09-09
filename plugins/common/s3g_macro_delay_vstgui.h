#pragma once

#include "s3g_macro_delay.h"

#include <cstddef>
#include <cstdint>

namespace s3g::portable_gui {

struct MacroDelayEditorCallbacks {
    void* context = nullptr;
    s3g::MacroDelayParams (*getParams)(void*) = nullptr;
    float (*getOutputPeak)(void*) = nullptr;
    void (*beginParamEdit)(void*, uint32_t) = nullptr;
    void (*setParam)(void*, uint32_t, double) = nullptr;
    void (*endParamEdit)(void*, uint32_t) = nullptr;
    bool (*getDefaultValue)(void*, uint32_t, double*) = nullptr;
    void (*resetToDefaults)(void*) = nullptr;
    bool (*loadPreset)(void*, const char*) = nullptr;
    bool (*savePreset)(void*, const char*) = nullptr;
};

struct MacroDelayEditorConfig {
    MacroDelayEditorCallbacks callbacks {};
    const char* pluginName = "s3g Macro Delay";
    uint32_t channelCount = 8u;
    uint32_t nativeWidth = 760u;
    uint32_t nativeHeight = 496u;
};

class MacroDelayEditor;

MacroDelayEditor* createMacroDelayEditor(
    const MacroDelayEditorConfig& config,
    uint32_t width,
    uint32_t height);
void destroyMacroDelayEditor(MacroDelayEditor* editor);

bool setMacroDelayEditorParent(MacroDelayEditor* editor, void* nativeParent);
bool setMacroDelayEditorSize(
    MacroDelayEditor* editor, uint32_t width, uint32_t height);
bool setMacroDelayEditorVisible(MacroDelayEditor* editor, bool visible);

} // namespace s3g::portable_gui
