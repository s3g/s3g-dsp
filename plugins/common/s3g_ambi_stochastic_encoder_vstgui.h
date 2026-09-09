#pragma once

#include "s3g_ambi_stochastic_encoder.h"
#include "s3g_parameter_surface.h"

#include <cstdint>

namespace s3g::portable_gui {

constexpr uint32_t kStochasticGuiWaveSamples = 256u;

struct AmbiStochasticEditorSnapshot {
    s3g::AmbiStochasticParams params {};
    s3g::AmbiStochasticParams effectiveParams {};
    s3g::ParameterSurfaceState<s3g::AmbiStochasticParams> surface {};
    s3g::AmbiStochasticPoint points[s3g::kAmbiStochasticMaxVoices] {};
    s3g::Vec3 topology[s3g::kAmbiStochasticMaxVoices] {};
    float energy[s3g::kAmbiStochasticMaxVoices] {};
    float renderGain[s3g::kAmbiStochasticMaxVoices] {};
    float kinetic[s3g::kAmbiStochasticMaxVoices] {};
    float neighborInfluence[s3g::kAmbiStochasticMaxVoices] {};
    float selectionPulse[s3g::kAmbiStochasticMaxVoices] {};
    uint32_t neighbor[s3g::kAmbiStochasticMaxVoices] {};
    uint32_t secondaryNeighbor[s3g::kAmbiStochasticMaxVoices] {};
    uint32_t currentGenerator[s3g::kAmbiStochasticMaxVoices] {};
    uint32_t nextGenerator[s3g::kAmbiStochasticMaxVoices] {};
    uint32_t fieldActive[s3g::kAmbiStochasticMaxVoices] {};
    float frequency[s3g::kAmbiStochasticMaxVoices] {};
    uint32_t listenerPickup[s3g::kAmbiStochasticMaxVoices] {};
    uint32_t listenerSecondaryPickup[s3g::kAmbiStochasticMaxVoices] {};
    float listenerPickupMix[s3g::kAmbiStochasticMaxVoices] {};
    float listenerResponse[s3g::kAmbiStochasticMaxVoices] {};
    float listenerEnergy[s3g::kAmbiStochasticMaxVoices] {};
    float listenerSignal[s3g::kAmbiStochasticMaxVoices] {};
    float listenerCapture[s3g::kAmbiStochasticMaxVoices] {};
    float listenerMutationRate[s3g::kAmbiStochasticMaxVoices] {};
    float listenerEvolutionRate[s3g::kAmbiStochasticMaxVoices] {};
    float listenerFieldClockRate[s3g::kAmbiStochasticMaxVoices] {};
    float listenerCascadeRate[s3g::kAmbiStochasticMaxVoices] {};
    float listenerEnvelope[s3g::kAmbiFieldListenerMaxLobes] {};
    float currentWaveform[kStochasticGuiWaveSamples] {};
    float nextWaveform[kStochasticGuiWaveSamples] {};
    float breakpointPosition[s3g::kAmbiStochasticMaxBreakpoints] {};
    float breakpointAmplitude[s3g::kAmbiStochasticMaxBreakpoints] {};
    uint32_t history[s3g::kAmbiStochasticHistorySize] {};
    uint32_t voiceCount = 1u;
    uint32_t breakpointCount = 0u;
    uint32_t historyCursor = 0u;
    float amplitudeBarrier = 0.0f;
    float durationBarrier = 0.0f;
    float listenerActivity = 0.0f;
    float globalEnergy = 0.0f;
    float globalKinetic = 0.0f;
    float effectiveSurfaceX = 0.5f;
    float effectiveSurfaceY = 0.5f;
    float outputPeak = 0.0f;
    int32_t factoryPresetIndex = 0;
    char presetName[64] {};
    int32_t viewMode = 0;
    double viewAzimuthDeg = 90.0;
    double viewElevationDeg = 0.0;
    double viewZoom = 1.0;
};

enum class AmbiStochasticSurfaceAction : uint32_t {
    ToggleEnabled,
    Add,
    Remove,
    Capture,
    MoveCell,
    MoveCursor,
    SetFocus,
    SetGlide,
    SetCurve,
    SetCellPreset,
};

struct AmbiStochasticEditorCallbacks {
    void* context = nullptr;
    void (*getSnapshot)(void*, AmbiStochasticEditorSnapshot*) = nullptr;
    double (*getParam)(void*, uint32_t) = nullptr;
    double (*getEffectiveParam)(void*, uint32_t) = nullptr;
    bool (*getParamText)(void*, uint32_t, double, char*, uint32_t) = nullptr;
    bool (*getDefaultValue)(void*, uint32_t, double*) = nullptr;
    void (*beginParamEdit)(void*, uint32_t) = nullptr;
    void (*setParam)(void*, uint32_t, double) = nullptr;
    void (*endParamEdit)(void*, uint32_t) = nullptr;
    void (*setSelectedVoice)(void*, uint32_t) = nullptr;
    void (*setViewState)(void*, int32_t, double, double, double) = nullptr;
    void (*randomize)(void*) = nullptr;
    void (*applyFactoryPreset)(void*, uint32_t) = nullptr;
    bool (*surfaceAction)(void*, AmbiStochasticSurfaceAction, int32_t,
        double, double) = nullptr;
    bool (*loadPreset)(void*, const char*) = nullptr;
    bool (*savePreset)(void*, const char*) = nullptr;
};

struct AmbiStochasticEditorConfig {
    AmbiStochasticEditorCallbacks callbacks {};
    const char* pluginName = "s3g AMBI ENCODER STOCHASTIC";
    uint32_t nativeWidth = 1160u;
    uint32_t nativeHeight = 860u;
};

class AmbiStochasticEditor;

AmbiStochasticEditor* createAmbiStochasticEditor(
    const AmbiStochasticEditorConfig& config, uint32_t width,
    uint32_t height);
void destroyAmbiStochasticEditor(AmbiStochasticEditor* editor);
bool setAmbiStochasticEditorParent(
    AmbiStochasticEditor* editor, void* nativeParent);
bool setAmbiStochasticEditorSize(
    AmbiStochasticEditor* editor, uint32_t width, uint32_t height);
bool setAmbiStochasticEditorVisible(
    AmbiStochasticEditor* editor, bool visible);

} // namespace s3g::portable_gui
