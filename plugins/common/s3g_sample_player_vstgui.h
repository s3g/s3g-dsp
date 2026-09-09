#pragma once

#include "s3g_sample_player.h"

#include <cstddef>
#include <cstdint>

namespace s3g::portable_gui {

struct SamplePlayerEditorCallbacks {
    void* context = nullptr;
    double (*getParam)(void*, uint32_t) = nullptr;
    bool (*getParamText)(void*, uint32_t, double, char*, uint32_t) = nullptr;
    bool (*isParamExposed)(void*, uint32_t) = nullptr;
    const s3g::sample::SampleAsset* (*getAsset)(void*) = nullptr;
    const char* (*getStatus)(void*) = nullptr;
    const char* (*getSamplePath)(void*) = nullptr;
    const char* (*getStorageModeName)(void*) = nullptr;
    uint8_t (*getStorageMode)(void*) = nullptr;
    uint32_t (*getOutputChannelCount)(void*) = nullptr;
    float (*getOutputPeak)(void*) = nullptr;
    uint32_t (*getVoiceCursors)(
        void*, float*, uint8_t*, uint32_t) = nullptr;
    void (*beginParamEdit)(void*, uint32_t) = nullptr;
    void (*setParam)(void*, uint32_t, double) = nullptr;
    void (*endParamEdit)(void*, uint32_t) = nullptr;
    bool (*getDefaultValue)(void*, uint32_t, double*) = nullptr;
    void (*resetToDefaults)(void*) = nullptr;
    void (*killAll)(void*) = nullptr;
    bool (*loadSample)(void*, const char*) = nullptr;
    bool (*cycleStorageMode)(void*) = nullptr;
    void (*service)(void*) = nullptr;
    bool (*loadPreset)(void*, const char*) = nullptr;
    bool (*savePreset)(void*, const char*) = nullptr;
    bool (*loadDocumentationSample)(void*) = nullptr;
};

struct SamplePlayerEditorConfig {
    SamplePlayerEditorCallbacks callbacks {};
    const char* pluginName = "s3g Sample Player 2";
    uint32_t nativeWidth = 980u;
    uint32_t nativeHeight = 844u;
};

class SamplePlayerEditor;

SamplePlayerEditor* createSamplePlayerEditor(
    const SamplePlayerEditorConfig& config,
    uint32_t width,
    uint32_t height);
void destroySamplePlayerEditor(SamplePlayerEditor* editor);

bool setSamplePlayerEditorParent(
    SamplePlayerEditor* editor, void* nativeParent);
bool setSamplePlayerEditorSize(
    SamplePlayerEditor* editor, uint32_t width, uint32_t height);
bool setSamplePlayerEditorVisible(
    SamplePlayerEditor* editor, bool visible);

} // namespace s3g::portable_gui
