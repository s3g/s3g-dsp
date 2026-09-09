#pragma once

#include <cstdint>

namespace s3g::portable_gui {

enum class MacroEffectKind : uint8_t {
    Pitch,
    Shred,
    Fracture,
};

struct MacroEffectEditorCallbacks {
    void* context = nullptr;
    bool (*getParamValue)(void*, uint32_t, double*) = nullptr;
    float (*getOutputPeak)(void*) = nullptr;
    float (*getActivity)(void*) = nullptr;
    float (*getSecondaryActivity)(void*) = nullptr;
    float (*getFrequencyHz)(void*) = nullptr;
    void (*requestPanic)(void*) = nullptr;
    void (*beginParamEdit)(void*, uint32_t) = nullptr;
    void (*setParam)(void*, uint32_t, double) = nullptr;
    void (*endParamEdit)(void*, uint32_t) = nullptr;
    bool (*getDefaultValue)(void*, uint32_t, double*) = nullptr;
    void (*resetToDefaults)(void*) = nullptr;
    bool (*loadPreset)(void*, const char*) = nullptr;
    bool (*savePreset)(void*, const char*) = nullptr;
};

struct MacroEffectEditorConfig {
    MacroEffectEditorCallbacks callbacks {};
    MacroEffectKind kind = MacroEffectKind::Pitch;
    const char* pluginName = "s3g Macro Effect";
    uint32_t channelCount = 8u;
    uint32_t nativeWidth = 760u;
    uint32_t nativeHeight = 496u;
};

class MacroEffectEditor;

MacroEffectEditor* createMacroEffectEditor(
    const MacroEffectEditorConfig& config,
    uint32_t width,
    uint32_t height);
void destroyMacroEffectEditor(MacroEffectEditor* editor);

bool setMacroEffectEditorParent(MacroEffectEditor* editor, void* nativeParent);
bool setMacroEffectEditorSize(
    MacroEffectEditor* editor, uint32_t width, uint32_t height);
bool setMacroEffectEditorVisible(MacroEffectEditor* editor, bool visible);

} // namespace s3g::portable_gui
