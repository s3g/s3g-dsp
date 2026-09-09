#pragma once

#include "s3g_ambisonic_point_encoder.h"

#include <cstdint>

namespace s3g::portable_gui {

struct AmbiPointEditorSnapshot {
    s3g::AmbiPointEncoderParams params {};
    s3g::AmbiPoint animatedPoints[s3g::kAmbiPointEncoderMaxPoints] {};
    s3g::AmbiPoint editPoints[s3g::kAmbiPointEncoderMaxPoints] {};
    float collisionEnergy[s3g::kAmbiPointEncoderMaxPoints] {};
    float bondRelease[s3g::kAmbiPointEncoderMaxPoints] {};
    s3g::Vec3 perturbationSource {};
    s3g::Vec3 previousPerturbationSource {};
    float outputPeak = 0.0f;
    int32_t viewMode = 0;
    double viewAzimuthDeg = 90.0;
    double viewElevationDeg = 0.0;
    double viewZoom = 1.0;
};

struct AmbiPointEditorCallbacks {
    void* context = nullptr;
    void (*getSnapshot)(void*, AmbiPointEditorSnapshot*) = nullptr;
    double (*getParam)(void*, uint32_t) = nullptr;
    bool (*getParamText)(void*, uint32_t, double, char*, uint32_t) = nullptr;
    bool (*getDefaultValue)(void*, uint32_t, double*) = nullptr;
    void (*beginParamEdit)(void*, uint32_t) = nullptr;
    void (*setParam)(void*, uint32_t, double) = nullptr;
    void (*endParamEdit)(void*, uint32_t) = nullptr;
    void (*setPointParam)(void*, uint32_t, uint32_t, double) = nullptr;
    void (*setViewState)(void*, int32_t, double, double, double) = nullptr;
    void (*resetToDefaults)(void*) = nullptr;
    void (*randomize)(void*) = nullptr;
    bool (*loadPreset)(void*, const char*) = nullptr;
    bool (*savePreset)(void*, const char*) = nullptr;
};

struct AmbiPointEditorConfig {
    AmbiPointEditorCallbacks callbacks {};
    const char* pluginName = "s3g AMBI ENCODER POINT";
    uint32_t nativeWidth = 900u;
    uint32_t nativeHeight = 716u;
};

class AmbiPointEditor;

AmbiPointEditor* createAmbiPointEditor(
    const AmbiPointEditorConfig& config, uint32_t width, uint32_t height);
void destroyAmbiPointEditor(AmbiPointEditor* editor);
bool setAmbiPointEditorParent(AmbiPointEditor* editor, void* nativeParent);
bool setAmbiPointEditorSize(
    AmbiPointEditor* editor, uint32_t width, uint32_t height);
bool setAmbiPointEditorVisible(AmbiPointEditor* editor, bool visible);

} // namespace s3g::portable_gui
