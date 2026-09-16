#pragma once

#include "s3g_sample_asset.h"
#include "s3g_sample_cursor_clock.h"

#include <cstdint>
#include <memory>

namespace s3g::portable_gui {

struct SampleFamilyParameterInfo {
    uint32_t id = 0u;
    char name[64] {};
    char module[64] {};
    double minimum = 0.0;
    double maximum = 1.0;
    double defaultValue = 0.0;
    bool stepped = false;
    bool readOnly = false;
};

struct SampleFamilyWaveMarker {
    uint32_t parameterId = 0u;
    const char* label = "";
};

struct SampleFamilyAction {
    uint32_t actionId = 0u;
    const char* label = "";
    bool hold = false;
};

enum class SampleFamilyVisualization : uint8_t {
    Waveform = 0u,
    Doubles,
    Wavesets,
    Motion,
    Lanes,
    Grains,
    Cutups,
    Rings,
    Circulator,
};

// A small, transport-safe drawing vocabulary used by plug-in bridges to
// publish distinctive visual state without coupling DSP code to VSTGUI.
// x/y/width/intensity are normalized. `kind` is visualization-specific.
struct SampleFamilyVisualPoint {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float intensity = 1.0f;
    uint32_t kind = 0u;
};

// Detailed read-head state for waveform cursors.  Plug-ins that have routed
// voices can publish the same note/output flags used by their Cocoa views.
struct SampleFamilyCursorState {
    float position = -1.0f;
    uint8_t key = 0u;
    uint8_t outputFirst = 0u;
    uint8_t outputSecond = 0u;
    uint8_t outputWidth = 0u;
    bool hasOutputRouting = false;
};

struct SampleFamilyWavesetsScopeState {
    uint32_t channelCount = 0u;
    uint32_t groupSize = 1u;
    uint32_t cycleOffset = 0u;
    uint32_t repeatIndex = 0u;
    uint32_t repeats = 1u;
    float processAmount = 0.0f;
    uint8_t key = 0u;
    uint8_t outputFirst = 0u;
    uint8_t outputSecond = 1u;
    uint8_t outputWidth = 2u;
    bool hasFocusedVoice = false;
    bool hasOutputRouting = false;
    char processName[32] {};
};

struct SampleFamilyMotionScopeState {
    uint32_t activeVoices = 0u;
    float cursorPhase = -1.0f;
};

struct SampleFamilyDoublesState {
    char tempoText[96] {};
    char storageText[320] {};
    uint8_t activeDecks = 0u;
    uint8_t cueMask = 0u;
    float cues[2] {-1.0f, -1.0f};
    bool playing = false;
    bool automaticTempo = false;
    uint32_t activeActions = 0u;
};

enum class SampleFamilyTransportState : uint8_t {
    Playing = 0u,
    Paused,
    Stopped,
};

// Rings publishes both sides of a live crossfade so the portable editor can
// reproduce the Cocoa effective-read panel without inferring routing state.
struct SampleFamilyRingsHeadState {
    uint32_t ringA = 0u;
    uint32_t ringB = 0u;
    uint32_t sourceSlotA = 0u;
    uint32_t sourceSlotB = 0u;
    uint32_t sourceChannelA = 0u;
    uint32_t sourceChannelB = 0u;
    uint32_t groupLeader = 0u;
    uint32_t groupSize = 1u;
    uint32_t manualRingParameterId = 0u;
    uint32_t manualPhaseParameterId = 0u;
    uint32_t manualRateParameterId = 0u;
    float phase = 0.0f;
    float phaseA = 0.0f;
    float phaseB = 0.0f;
    float rate = 0.0f;
    float mix = 0.0f;
    bool hasSource = false;
};

struct SampleFamilyEditorCallbacks {
    void* context = nullptr;
    uint32_t (*getParameterCount)(void*) = nullptr;
    bool (*getParameterInfo)(
        void*, uint32_t, SampleFamilyParameterInfo*) = nullptr;
    double (*getParam)(void*, uint32_t) = nullptr;
    bool (*getParamText)(void*, uint32_t, double, char*, uint32_t) = nullptr;
    void (*beginParamEdit)(void*, uint32_t) = nullptr;
    void (*setParam)(void*, uint32_t, double) = nullptr;
    void (*endParamEdit)(void*, uint32_t) = nullptr;
    double (*parameterToNormalized)(void*, uint32_t, double) = nullptr;
    double (*parameterFromNormalized)(void*, uint32_t, double) = nullptr;
    void (*resetToDefaults)(void*) = nullptr;
    uint32_t (*getFactoryPresetCount)(void*) = nullptr;
    const char* (*getFactoryPresetName)(void*, uint32_t) = nullptr;
    bool (*applyFactoryPreset)(void*, uint32_t) = nullptr;
    uint32_t (*getParameterMenuItemCount)(void*, uint32_t) = nullptr;
    bool (*getParameterMenuItem)(void*, uint32_t, uint32_t, double*, char*,
        uint32_t) = nullptr;
    int32_t (*getParameterMenuSelectedIndex)(void*, uint32_t) = nullptr;
    bool (*applyParameterMenuItem)(void*, uint32_t, uint32_t) = nullptr;

    uint32_t (*getSampleSlotCount)(void*) = nullptr;
    const s3g::sample::SampleAsset* (*getAsset)(void*, uint32_t) = nullptr;
    // Optional immutable ownership for per-editor drawing caches. Asset
    // contents must not mutate while this shared ownership is held.
    std::shared_ptr<const s3g::sample::SampleAsset> (*getOwnedAsset)(void*, uint32_t) = nullptr;
    const char* (*getSamplePath)(void*, uint32_t) = nullptr;
    const char* (*getSampleStatus)(void*, uint32_t) = nullptr;
    bool (*loadSample)(void*, uint32_t, const char*) = nullptr;
    bool (*clearSample)(void*, uint32_t) = nullptr;
    const char* (*getStorageModeName)(void*, uint32_t) = nullptr;
    bool (*cycleStorageMode)(void*, uint32_t) = nullptr;

    float (*getOutputPeak)(void*) = nullptr;
    uint32_t (*getOutputChannelCount)(void*) = nullptr;
    uint32_t (*getCursors)(void*, uint32_t, float*, uint8_t*, uint32_t)
        = nullptr;
    uint32_t (*getCursorStates)(void*, uint32_t, SampleFamilyCursorState*,
        uint32_t) = nullptr;
    uint32_t (*getCursorTrajectories)(void*, SampleCursorTrajectory*, uint32_t)
        = nullptr;
    bool (*getWavesetsScopeState)(void*, SampleFamilyWavesetsScopeState*)
        = nullptr;
    bool (*getMotionScopeState)(void*, SampleFamilyMotionScopeState*) = nullptr;
    bool (*getDoublesState)(void*, SampleFamilyDoublesState*) = nullptr;
    void (*placeDoublesCue)(void*, uint32_t, float) = nullptr;
    void (*applyTempoMultiplier)(void*, double, bool) = nullptr;
    uint32_t (*getVisualizationPoints)(void*, uint32_t,
        SampleFamilyVisualPoint*, uint32_t) = nullptr;
    SampleFamilyTransportState (*getTransportState)(void*) = nullptr;
    bool (*getRingsHeadState)(
        void*, uint32_t, SampleFamilyRingsHeadState*) = nullptr;
    void (*previewRingsHead)(void*, uint32_t, float, float) = nullptr;
    bool (*setVisualizationPoint)(void*, uint32_t, float, float) = nullptr;
    bool (*setVisualizationPointWithAlternate)(void*, uint32_t, float, float,
        bool) = nullptr;
    bool (*addVisualizationPoint)(void*, float, float) = nullptr;
    bool (*removeVisualizationPoint)(void*, uint32_t) = nullptr;
    void (*performAction)(void*, uint32_t, bool) = nullptr;
    void (*recalculateSample)(void*, uint32_t) = nullptr;
    void (*service)(void*) = nullptr;
    bool (*loadPreset)(void*, const char*) = nullptr;
    bool (*savePreset)(void*, const char*) = nullptr;
};

struct SampleFamilyEditorConfig {
    SampleFamilyEditorCallbacks callbacks {};
    const char* pluginName = "s3g Sample";
    const char* samplePanelName = "SAMPLE";
    const SampleFamilyWaveMarker* markers = nullptr;
    uint32_t markerCount = 0u;
    const SampleFamilyAction* actions = nullptr;
    uint32_t actionCount = 0u;
    uint32_t nativeWidth = 980u;
    uint32_t nativeHeight = 844u;
    uint32_t minimumColumns = 3u;
    SampleFamilyVisualization visualization
        = SampleFamilyVisualization::Waveform;
};

class SampleFamilyEditor;

SampleFamilyEditor* createSampleFamilyEditor(
    const SampleFamilyEditorConfig& config,
    uint32_t width,
    uint32_t height);
void destroySampleFamilyEditor(SampleFamilyEditor* editor);
bool setSampleFamilyEditorParent(
    SampleFamilyEditor* editor, void* nativeParent);
bool setSampleFamilyEditorSize(
    SampleFamilyEditor* editor, uint32_t width, uint32_t height);
bool setSampleFamilyEditorVisible(
    SampleFamilyEditor* editor, bool visible);

} // namespace s3g::portable_gui
