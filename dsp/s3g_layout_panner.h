#pragma once

#include "s3g_3oafx.h"
#include "s3g_cube41_layout.h"
#include "s3g_math.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace s3g {

constexpr uint32_t kLayoutPannerSources = 64;
constexpr uint32_t kLayoutPannerMaxSpeakers = 64;
constexpr uint32_t kLayoutPannerMaxSolverSpeakers = 96;
constexpr uint32_t kLayoutPannerMaxVbapPairs = 128;
constexpr uint32_t kLayoutPannerMaxVbapFacets = 256;
constexpr uint32_t kLayoutPannerTargetUpdateSamples = 32;
constexpr float kLayoutPannerDefaultFirstAzimuthDeg = -30.0f;

enum class LayoutPannerPreset : uint32_t {
    Custom = 0,
    Cube8 = 1,
    Cube17 = 2,
    Dodeca12 = 3,
    Dome24NoOverhead = 4,
    Dome25 = 5,
    DoubleRing16 = 6,
    DoubleRing20 = 7,
    OctophonicRing = 8,
    Quad = 9,
    QuadOverhead6 = 10,
    Ring12 = 11,
    Ring16 = 12,
    FiveZero = 13,
    SixZero = 14,
    SevenZero = 15,
    FiveZeroTwo = 16,
    SevenZeroTwo = 17,
    FiveZeroFour = 18,
    SevenZeroFour = 19,
    NineZero = 20,
    NineZeroTwo = 21,
    NineZeroFour = 22,
    NineZeroSix = 23,
    SevenZeroSix = 24,
    ElevenZeroEight = 25,
    Icosahedron20 = 26,
    Cube41 = 27,
    Lpac41 = 28,
    Srst25 = 29,
};

enum class LayoutPannerMethod : uint32_t {
    Distance = 0,
    Cosine = 1,
    Dbap = 2,
    Lbap = 3,
    Vbap = 4,
};

enum class LayoutPannerInsideMode : uint32_t {
    Hold = 0,
    Attenuate = 1,
    Diffuse = 2,
    CenterBlend = 3,
};

enum class LayoutPannerCustomShape : uint32_t {
    Auto = 0,
    Ring = 1,
    Dome = 2,
    Tetra = 3,
    Octa = 4,
    Cube = 5,
    Icosa = 6,
    Dodeca = 7,
    Geo = 8,
    Stack = 9,
};

struct LayoutPannerSpeaker {
    float azimuthDeg = 0.0f;
    float elevationDeg = 0.0f;
    float distance = 1.0f;
};

struct LayoutPannerSolverSpeaker {
    Vec3 dir {};
    float azimuthDeg = 0.0f;
    float elevationDeg = 0.0f;
    uint32_t realIndex = 0u;
    bool real = true;
    bool foldToReal = true;
};

struct LayoutPannerVbapPair {
    uint32_t a = 0u;
    uint32_t b = 0u;
};

struct LayoutPannerVbapFacet {
    uint32_t a = 0u;
    uint32_t b = 0u;
    uint32_t c = 0u;
};

struct LayoutPannerVbapTopology {
    std::array<LayoutPannerVbapPair, kLayoutPannerMaxVbapPairs> pairs {};
    std::array<LayoutPannerVbapFacet, kLayoutPannerMaxVbapFacets> facets {};
    uint32_t pairCount = 0u;
    uint32_t facetCount = 0u;
};

struct LayoutPannerSource {
    float azimuthDeg = 0.0f;
    float elevationDeg = 0.0f;
    float distance = 1.0f;
    float x = 1.0f;
    float y = 0.0f;
    float z = 0.0f;
    float gainDb = 0.0f;
    bool muted = false;
    bool solo = false;
};

struct LayoutPannerParams {
    LayoutPannerPreset layout = LayoutPannerPreset::Dome24NoOverhead;
    LayoutPannerMethod method = LayoutPannerMethod::Distance;
    uint32_t activeSources = kLayoutPannerSources;
    uint32_t selectedSource = 0;
    uint32_t activeSpeakers = 24;
    uint32_t selectedSpeaker = 0;
    LayoutPannerCustomShape customShape = LayoutPannerCustomShape::Auto;
    float focus = 1.0f;
    float distanceRolloffDb = 6.0f;
    float smoothingMs = 35.0f;
    float globalAzimuthDeg = 0.0f;
    float globalElevationDeg = 0.0f;
    float globalDistanceOffset = 0.0f;
    float distanceDiffusion = 0.35f;
    LayoutPannerInsideMode insideMode = LayoutPannerInsideMode::Hold;
    float outputGainDb = -6.0f;
};

inline float layoutPannerWrapDeg(float value)
{
    while (value > 180.0f) value -= 360.0f;
    while (value <= -180.0f) value += 360.0f;
    return value;
}

inline const char* layoutPannerPresetName(LayoutPannerPreset preset)
{
    switch (preset) {
    case LayoutPannerPreset::Custom: return "CUSTOM";
    case LayoutPannerPreset::Cube8: return "CUBE 8";
    case LayoutPannerPreset::Cube17: return "CUBE 17";
    case LayoutPannerPreset::Cube41: return "CUBE 41";
    case LayoutPannerPreset::Dodeca12: return "DODECA 12";
    case LayoutPannerPreset::Dome24NoOverhead: return "DOME 24";
    case LayoutPannerPreset::Dome25: return "DOME 25";
    case LayoutPannerPreset::DoubleRing16: return "DBL RING 16";
    case LayoutPannerPreset::DoubleRing20: return "DBL RING 20";
    case LayoutPannerPreset::OctophonicRing: return "OCTO RING";
    case LayoutPannerPreset::Quad: return "QUAD";
    case LayoutPannerPreset::QuadOverhead6: return "QUAD+OH";
    case LayoutPannerPreset::Ring12: return "RING 12";
    case LayoutPannerPreset::Ring16: return "RING 16";
    case LayoutPannerPreset::FiveZero: return "5.0";
    case LayoutPannerPreset::SixZero: return "6.0";
    case LayoutPannerPreset::SevenZero: return "7.0";
    case LayoutPannerPreset::FiveZeroTwo: return "5.0.2";
    case LayoutPannerPreset::SevenZeroTwo: return "7.0.2";
    case LayoutPannerPreset::FiveZeroFour: return "5.0.4";
    case LayoutPannerPreset::SevenZeroFour: return "7.0.4";
    case LayoutPannerPreset::NineZero: return "9.0";
    case LayoutPannerPreset::NineZeroTwo: return "9.0.2";
    case LayoutPannerPreset::NineZeroFour: return "9.0.4";
    case LayoutPannerPreset::NineZeroSix: return "9.0.6";
    case LayoutPannerPreset::SevenZeroSix: return "7.0.6";
    case LayoutPannerPreset::ElevenZeroEight: return "11.0.8";
    case LayoutPannerPreset::Icosahedron20: return "ICOSAHEDRON 20";
    case LayoutPannerPreset::Lpac41: return "LPAC 41";
    case LayoutPannerPreset::Srst25: return "SRST 25";
    default: return "DOME 24";
    }
}

inline uint32_t layoutPannerPresetSpeakerCount(LayoutPannerPreset preset, uint32_t customFallback = 0u)
{
    switch (preset) {
    case LayoutPannerPreset::Custom: return customFallback;
    case LayoutPannerPreset::Cube8: return 8u;
    case LayoutPannerPreset::Cube17: return 17u;
    case LayoutPannerPreset::Cube41: return kCube41SpeakerCount;
    case LayoutPannerPreset::Lpac41: return kCube41SpeakerCount;
    case LayoutPannerPreset::Dodeca12: return 12u;
    case LayoutPannerPreset::Dome24NoOverhead: return 24u;
    case LayoutPannerPreset::Dome25: return 25u;
    case LayoutPannerPreset::Srst25: return kSrst25SpeakerCount;
    case LayoutPannerPreset::DoubleRing16: return 16u;
    case LayoutPannerPreset::DoubleRing20: return 20u;
    case LayoutPannerPreset::OctophonicRing: return 8u;
    case LayoutPannerPreset::Quad: return 4u;
    case LayoutPannerPreset::QuadOverhead6: return 6u;
    case LayoutPannerPreset::Ring12: return 12u;
    case LayoutPannerPreset::Ring16: return 16u;
    case LayoutPannerPreset::FiveZero: return 5u;
    case LayoutPannerPreset::SixZero: return 6u;
    case LayoutPannerPreset::SevenZero: return 7u;
    case LayoutPannerPreset::FiveZeroTwo: return 7u;
    case LayoutPannerPreset::SevenZeroTwo: return 9u;
    case LayoutPannerPreset::FiveZeroFour: return 9u;
    case LayoutPannerPreset::SevenZeroFour: return 11u;
    case LayoutPannerPreset::NineZero: return 9u;
    case LayoutPannerPreset::NineZeroTwo: return 11u;
    case LayoutPannerPreset::NineZeroFour: return 13u;
    case LayoutPannerPreset::NineZeroSix: return 15u;
    case LayoutPannerPreset::SevenZeroSix: return 13u;
    case LayoutPannerPreset::ElevenZeroEight: return 19u;
    case LayoutPannerPreset::Icosahedron20: return 20u;
    default: return customFallback;
    }
}

inline const char* layoutPannerMethodName(LayoutPannerMethod method)
{
    switch (method) {
    case LayoutPannerMethod::Cosine: return "COS";
    case LayoutPannerMethod::Dbap: return "DBAP";
    case LayoutPannerMethod::Lbap: return "LBAP";
    case LayoutPannerMethod::Vbap: return "VBAP";
    case LayoutPannerMethod::Distance:
    default: return "DIST";
    }
}

inline const char* layoutPannerInsideModeName(LayoutPannerInsideMode mode)
{
    switch (mode) {
    case LayoutPannerInsideMode::Attenuate: return "ATTENUATE";
    case LayoutPannerInsideMode::Diffuse: return "DIFFUSE";
    case LayoutPannerInsideMode::CenterBlend: return "CENTER";
    case LayoutPannerInsideMode::Hold:
    default: return "HOLD";
    }
}

inline const char* layoutPannerCustomShapeName(LayoutPannerCustomShape shape)
{
    switch (shape) {
    case LayoutPannerCustomShape::Auto: return "AUTO";
    case LayoutPannerCustomShape::Ring: return "RING";
    case LayoutPannerCustomShape::Dome: return "DOME";
    case LayoutPannerCustomShape::Tetra: return "TETRA";
    case LayoutPannerCustomShape::Octa: return "OCTA";
    case LayoutPannerCustomShape::Cube: return "CUBE";
    case LayoutPannerCustomShape::Icosa: return "ICO";
    case LayoutPannerCustomShape::Dodeca: return "DODECA";
    case LayoutPannerCustomShape::Geo: return "GEO";
    case LayoutPannerCustomShape::Stack: return "STACK";
    default: return "AUTO";
    }
}

inline Vec3 layoutPannerSourcePosition(const LayoutPannerSource& source)
{
    return { source.x, source.y, source.z };
}

inline void layoutPannerSyncSourcePositionFromAed(LayoutPannerSource& source)
{
    source.azimuthDeg = layoutPannerWrapDeg(source.azimuthDeg);
    source.elevationDeg = clamp(source.elevationDeg, -90.0f, 90.0f);
    source.distance = clamp(source.distance, 0.1f, 3.0f);
    const Vec3 dir = directionFromAed(source.azimuthDeg, source.elevationDeg);
    source.x = dir.x * source.distance;
    source.y = dir.y * source.distance;
    source.z = dir.z * source.distance;
}

inline void layoutPannerSyncSourceAedFromPosition(LayoutPannerSource& source)
{
    const float distance = std::sqrt(source.x * source.x + source.y * source.y + source.z * source.z);
    source.distance = clamp(distance, 0.1f, 3.0f);
    const float inv = 1.0f / std::max(0.000001f, distance);
    source.azimuthDeg = layoutPannerWrapDeg(std::atan2(source.y, source.x) * 180.0f / kPi);
    source.elevationDeg = clamp(std::asin(clamp(source.z * inv, -1.0f, 1.0f)) * 180.0f / kPi, -90.0f, 90.0f);
}

inline LayoutPannerSource layoutPannerEffectiveSource(const LayoutPannerSource& source, const LayoutPannerParams& params)
{
    LayoutPannerSource effective = source;
    effective.azimuthDeg = layoutPannerWrapDeg(source.azimuthDeg + params.globalAzimuthDeg);
    effective.elevationDeg = clamp(source.elevationDeg + params.globalElevationDeg, -90.0f, 90.0f);
    effective.distance = std::max(0.1f, source.distance + params.globalDistanceOffset);
    const Vec3 dir = directionFromAed(effective.azimuthDeg, effective.elevationDeg);
    effective.x = dir.x * effective.distance;
    effective.y = dir.y * effective.distance;
    effective.z = dir.z * effective.distance;
    return effective;
}

inline Vec3 layoutPannerEffectiveSourcePosition(const LayoutPannerSource& source, const LayoutPannerParams& params)
{
    return layoutPannerSourcePosition(layoutPannerEffectiveSource(source, params));
}

inline Vec3 layoutPannerRawSourcePositionFromEffectivePosition(Vec3 effectivePosition, const LayoutPannerParams& params)
{
    const float effectiveDistance = std::sqrt(
        effectivePosition.x * effectivePosition.x
        + effectivePosition.y * effectivePosition.y
        + effectivePosition.z * effectivePosition.z);
    const float inv = 1.0f / std::max(0.000001f, effectiveDistance);
    const float effectiveAzimuth = layoutPannerWrapDeg(std::atan2(effectivePosition.y, effectivePosition.x) * 180.0f / kPi);
    const float effectiveElevation = clamp(
        std::asin(clamp(effectivePosition.z * inv, -1.0f, 1.0f)) * 180.0f / kPi,
        -90.0f,
        90.0f);
    const float rawAzimuth = layoutPannerWrapDeg(effectiveAzimuth - params.globalAzimuthDeg);
    const float rawElevation = clamp(effectiveElevation - params.globalElevationDeg, -90.0f, 90.0f);
    const float rawDistance = clamp(effectiveDistance - params.globalDistanceOffset, 0.1f, 3.0f);
    const Vec3 rawDir = directionFromAed(rawAzimuth, rawElevation);
    return { rawDir.x * rawDistance, rawDir.y * rawDistance, rawDir.z * rawDistance };
}

class LayoutPanner {
public:
    void prepare(double sampleRate)
    {
        sampleRate_ = std::max(1.0, sampleRate);
        resetSources();
        applyLayout(params_.layout);
        updateCachedCoefficients();
        clearGains();
    }

    void resetSources()
    {
        for (uint32_t i = 0; i < kLayoutPannerSources; ++i) {
            sources_[i].azimuthDeg = layoutPannerWrapDeg(-30.0f - static_cast<float>(i) * 360.0f / static_cast<float>(kLayoutPannerSources));
            sources_[i].elevationDeg = 0.0f;
            sources_[i].distance = 1.0f;
            layoutPannerSyncSourcePositionFromAed(sources_[i]);
            sources_[i].gainDb = 0.0f;
            sources_[i].muted = false;
            sources_[i].solo = false;
            smoothedDistance_[i] = 1.0f;
        }
    }

    void setParams(LayoutPannerParams params)
    {
        params.activeSources = std::clamp<uint32_t>(params.activeSources, 1u, kLayoutPannerSources);
        params.selectedSource = std::min<uint32_t>(params.selectedSource, params.activeSources - 1u);
        params.activeSpeakers = std::clamp<uint32_t>(params.activeSpeakers, 2u, kLayoutPannerMaxSpeakers);
        params.selectedSpeaker = std::min<uint32_t>(params.selectedSpeaker, params.activeSpeakers - 1u);
        params.method = static_cast<LayoutPannerMethod>(
            std::clamp<uint32_t>(static_cast<uint32_t>(params.method), 0u, 4u));
        params.customShape = static_cast<LayoutPannerCustomShape>(
            std::clamp<uint32_t>(static_cast<uint32_t>(params.customShape), 0u, 9u));
        params.focus = clamp(params.focus, 0.25f, 4.0f);
        params.distanceRolloffDb = clamp(params.distanceRolloffDb, 0.0f, 48.0f);
        params.smoothingMs = clamp(params.smoothingMs, 1.0f, 250.0f);
        params.globalAzimuthDeg = layoutPannerWrapDeg(params.globalAzimuthDeg);
        params.globalElevationDeg = clamp(params.globalElevationDeg, -90.0f, 90.0f);
        params.globalDistanceOffset = clamp(params.globalDistanceOffset, -3.0f, 3.0f);
        params.distanceDiffusion = clamp(params.distanceDiffusion, 0.0f, 1.0f);
        params.insideMode = static_cast<LayoutPannerInsideMode>(
            std::clamp<uint32_t>(static_cast<uint32_t>(params.insideMode), 0u, 3u));
        params.outputGainDb = clamp(params.outputGainDb, -60.0f, 12.0f);
        const bool layoutChanged = params.layout != params_.layout;
        const bool activeChanged = params.activeSpeakers != params_.activeSpeakers;
        const bool customShapeChanged = params.customShape != params_.customShape;
        params_ = params;
        if (params_.layout == LayoutPannerPreset::Custom && (layoutChanged || activeChanged || customShapeChanged)) {
            generateCustomLayout(params_.customShape, params_.activeSpeakers);
            clearGains();
        } else if (params_.layout != LayoutPannerPreset::Custom && layoutChanged) {
            applyLayout(params_.layout);
            clearGains();
        }
        params_.activeSpeakers = activeSpeakers_;
        params_.activeSources = std::clamp<uint32_t>(params_.activeSources, 1u, kLayoutPannerSources);
        params_.selectedSource = std::min<uint32_t>(params_.selectedSource, params_.activeSources - 1u);
        params_.selectedSpeaker = std::min<uint32_t>(params_.selectedSpeaker, std::max<uint32_t>(1u, activeSpeakers_) - 1u);
        updateCachedCoefficients();
        markTargetsDirty();
    }

    LayoutPannerParams params() const { return params_; }
    uint32_t activeSpeakers() const { return activeSpeakers_; }
    const std::array<LayoutPannerSpeaker, kLayoutPannerMaxSpeakers>& speakers() const { return speakers_; }
    const std::array<LayoutPannerSource, kLayoutPannerSources>& sources() const { return sources_; }

    uint32_t vbapSolverSpeakers(std::array<LayoutPannerSolverSpeaker, kLayoutPannerMaxSolverSpeakers>& solver) const
    {
        return buildSolverSpeakers(solver);
    }

    LayoutPannerVbapTopology vbapTopology(const std::array<LayoutPannerSolverSpeaker, kLayoutPannerMaxSolverSpeakers>& solver,
        uint32_t solverCount) const
    {
        return buildVbapTopology(solver, solverCount);
    }

    LayoutPannerSource source(uint32_t index) const
    {
        return sources_[std::min<uint32_t>(index, kLayoutPannerSources - 1u)];
    }

    LayoutPannerSpeaker speaker(uint32_t index) const
    {
        return speakers_[std::min<uint32_t>(index, kLayoutPannerMaxSpeakers - 1u)];
    }

    void setSpeaker(uint32_t index, LayoutPannerSpeaker speaker)
    {
        if (index >= kLayoutPannerMaxSpeakers) return;
        speaker.azimuthDeg = layoutPannerWrapDeg(speaker.azimuthDeg);
        speaker.elevationDeg = clamp(speaker.elevationDeg, -90.0f, 90.0f);
        speaker.distance = clamp(speaker.distance, 0.1f, 3.0f);
        speakers_[index] = speaker;
        params_.layout = LayoutPannerPreset::Custom;
        params_.selectedSpeaker = std::min<uint32_t>(index, std::max<uint32_t>(1u, activeSpeakers_) - 1u);
        params_.activeSpeakers = activeSpeakers_;
        clearGains();
    }

    void setSpeakerPosition(uint32_t index, Vec3 position)
    {
        if (index >= kLayoutPannerMaxSpeakers) return;
        const float distance = std::sqrt(position.x * position.x + position.y * position.y + position.z * position.z);
        if (distance < 0.000001f) {
            setSpeaker(index, { 0.0f, 0.0f, 1.0f });
            return;
        }
        const float inv = 1.0f / distance;
        setSpeaker(index, {
            layoutPannerWrapDeg(std::atan2(position.y, position.x) * 180.0f / kPi),
            clamp(std::asin(clamp(position.z * inv, -1.0f, 1.0f)) * 180.0f / kPi, -90.0f, 90.0f),
            clamp(distance, 0.1f, 3.0f)
        });
    }

    void setActiveSpeakers(uint32_t count, LayoutPannerCustomShape shape)
    {
        generateCustomLayout(shape, count);
        clearGains();
    }

    void setSpeakers(std::array<LayoutPannerSpeaker, kLayoutPannerMaxSpeakers> speakers, uint32_t count)
    {
        activeSpeakers_ = std::clamp<uint32_t>(count, 2u, kLayoutPannerMaxSpeakers);
        for (uint32_t i = 0; i < kLayoutPannerMaxSpeakers; ++i) {
            auto speaker = speakers[i];
            speaker.azimuthDeg = layoutPannerWrapDeg(speaker.azimuthDeg);
            speaker.elevationDeg = clamp(speaker.elevationDeg, -90.0f, 90.0f);
            speaker.distance = clamp(speaker.distance, 0.1f, 3.0f);
            speakers_[i] = speaker;
        }
        params_.layout = LayoutPannerPreset::Custom;
        params_.activeSpeakers = activeSpeakers_;
        params_.selectedSpeaker = std::min<uint32_t>(params_.selectedSpeaker, activeSpeakers_ - 1u);
        clearGains();
    }

    void copyCurrentLayoutToCustom()
    {
        params_.layout = LayoutPannerPreset::Custom;
        params_.activeSpeakers = activeSpeakers_;
        params_.selectedSpeaker = std::min<uint32_t>(params_.selectedSpeaker, activeSpeakers_ - 1u);
        clearGains();
    }

    void setCustomShapeMetadata(LayoutPannerCustomShape shape)
    {
        params_.customShape = static_cast<LayoutPannerCustomShape>(
            std::clamp<uint32_t>(static_cast<uint32_t>(shape), 0u, 9u));
    }

    void setSource(uint32_t index, LayoutPannerSource source)
    {
        if (index >= kLayoutPannerSources) return;
        source.azimuthDeg = layoutPannerWrapDeg(source.azimuthDeg);
        source.elevationDeg = clamp(source.elevationDeg, -90.0f, 90.0f);
        source.distance = clamp(source.distance, 0.1f, 3.0f);
        layoutPannerSyncSourcePositionFromAed(source);
        source.gainDb = clamp(source.gainDb, -60.0f, 24.0f);
        sources_[index] = source;
        params_.selectedSource = index;
        markTargetsDirty();
    }

    void setSourcePosition(uint32_t index, Vec3 position)
    {
        if (index >= kLayoutPannerSources) return;
        const float distance = std::sqrt(position.x * position.x + position.y * position.y + position.z * position.z);
        if (distance > 3.0f) {
            const float scale = 3.0f / std::max(0.000001f, distance);
            position.x *= scale;
            position.y *= scale;
            position.z *= scale;
        } else if (distance < 0.1f) {
            const float scale = 0.1f / std::max(0.000001f, distance);
            position.x = distance < 0.000001f ? 0.1f : position.x * scale;
            position.y = distance < 0.000001f ? 0.0f : position.y * scale;
            position.z = distance < 0.000001f ? 0.0f : position.z * scale;
        }
        sources_[index].x = position.x;
        sources_[index].y = position.y;
        sources_[index].z = position.z;
        layoutPannerSyncSourceAedFromPosition(sources_[index]);
        params_.selectedSource = index;
        markTargetsDirty();
    }

    void setSourceGain(uint32_t index, float gainDb)
    {
        if (index >= kLayoutPannerSources) return;
        sources_[index].gainDb = clamp(gainDb, -60.0f, 24.0f);
        params_.selectedSource = index;
        markTargetsDirty();
    }

    void setSourceMute(uint32_t index, bool muted)
    {
        if (index >= kLayoutPannerSources) return;
        sources_[index].muted = muted;
        params_.selectedSource = index;
        markTargetsDirty();
    }

    void setSourceSolo(uint32_t index, bool solo)
    {
        if (index >= kLayoutPannerSources) return;
        sources_[index].solo = solo;
        params_.selectedSource = index;
        markTargetsDirty();
    }

    void processFrame(const float* in, float* out, uint32_t inputChannels, uint32_t outputChannels = kLayoutPannerMaxSpeakers)
    {
        if (!out) return;
        outputChannels = std::min<uint32_t>(outputChannels, kLayoutPannerMaxSpeakers);
        const uint32_t activeOutputs = std::min<uint32_t>(outputChannels, activeSpeakers_);
        for (uint32_t spk = 0; spk < outputChannels; ++spk) out[spk] = 0.0f;
        const uint32_t activeSourceCount = std::clamp<uint32_t>(params_.activeSources, 1u, kLayoutPannerSources);
        const uint32_t activeInputs = std::min<uint32_t>(inputChannels, activeSourceCount);
        bool anySolo = false;
        for (uint32_t src = 0; src < activeSourceCount; ++src) anySolo = anySolo || sources_[src].solo;

        const float gainCoef = cachedGainCoef_;
        if (targetUpdateCountdown_ == 0u) {
            updateGainTargets(activeInputs, activeSourceCount, anySolo, kLayoutPannerTargetUpdateSamples);
            targetUpdateCountdown_ = kLayoutPannerTargetUpdateSamples;
        } else {
            --targetUpdateCountdown_;
        }
        for (uint32_t src = activeInputs; src < activeSourceCount; ++src) smoothSourceToSilence(src, gainCoef);
        for (uint32_t src = 0; src < activeInputs; ++src) {
            const float input = in ? in[src] : 0.0f;
            if (!targetActive_[src] || std::abs(input) < 0.0000001f) {
                smoothSourceToSilence(src, gainCoef);
                continue;
            }

            const float weighted = input * sourceAmps_[src];
            for (uint32_t spk = 0; spk < activeOutputs; ++spk) {
                const uint32_t gi = src * kLayoutPannerMaxSpeakers + spk;
                const float target = targetGains_[gi];
                smoothedGains_[gi] = target + (smoothedGains_[gi] - target) * gainCoef;
                out[spk] += weighted * smoothedGains_[gi];
            }
        }
        for (uint32_t spk = 0; spk < activeOutputs; ++spk) {
            out[spk] = clamp(out[spk] * cachedOutputGain_, -4.0f, 4.0f);
        }
    }

    template <typename Sample>
    void processBlock(const Sample* const* in, Sample* const* out, uint32_t inputChannels, uint32_t outputChannels, uint32_t sampleFrames)
    {
        if (!out || sampleFrames == 0u) return;
        const uint32_t activeSourceCount = std::clamp<uint32_t>(params_.activeSources, 1u, kLayoutPannerSources);
        const uint32_t activeInputs = std::min<uint32_t>(inputChannels, activeSourceCount);
        const uint32_t activeOutputs = std::min<uint32_t>(outputChannels, activeSpeakers_);
        bool anySolo = false;
        for (uint32_t src = 0; src < activeSourceCount; ++src) anySolo = anySolo || sources_[src].solo;

        updateGainTargets(activeInputs, activeSourceCount, anySolo, sampleFrames);
        targetUpdateCountdown_ = kLayoutPannerTargetUpdateSamples;

        const float gainCoef = smoothingCoeff(params_.smoothingMs);
        const float outputGain = dbToGain(params_.outputGainDb);
        for (uint32_t frame = 0; frame < sampleFrames; ++frame) {
            for (uint32_t spk = 0; spk < outputChannels; ++spk) {
                if (out[spk]) out[spk][frame] = static_cast<Sample>(0);
            }
            for (uint32_t src = activeInputs; src < activeSourceCount; ++src) smoothSourceToSilence(src, gainCoef);
            for (uint32_t src = 0; src < activeInputs; ++src) {
                const float input = (in && in[src]) ? static_cast<float>(in[src][frame]) : 0.0f;
                if (!targetActive_[src] || std::abs(input) < 0.0000001f) {
                    smoothSourceToSilence(src, gainCoef);
                    continue;
                }
                const float weighted = input * sourceAmps_[src];
                for (uint32_t spk = 0; spk < activeOutputs; ++spk) {
                    const uint32_t gi = src * kLayoutPannerMaxSpeakers + spk;
                    smoothedGains_[gi] = targetGains_[gi] + (smoothedGains_[gi] - targetGains_[gi]) * gainCoef;
                    if (out[spk]) {
                        out[spk][frame] = static_cast<Sample>(out[spk][frame] + static_cast<Sample>(weighted * smoothedGains_[gi]));
                    }
                }
            }
            for (uint32_t spk = 0; spk < activeOutputs; ++spk) {
                if (out[spk]) out[spk][frame] = static_cast<Sample>(clamp(static_cast<float>(out[spk][frame]) * outputGain, -4.0f, 4.0f));
            }
        }
    }

    void beginVector(uint32_t inputChannels, uint32_t outputChannels, uint32_t sampleFrames)
    {
        vectorActiveSourceCount_ = std::clamp<uint32_t>(params_.activeSources, 1u, kLayoutPannerSources);
        vectorActiveInputs_ = std::min<uint32_t>(inputChannels, vectorActiveSourceCount_);
        vectorOutputChannels_ = std::min<uint32_t>(outputChannels, kLayoutPannerMaxSpeakers);
        vectorActiveOutputs_ = std::min<uint32_t>(vectorOutputChannels_, activeSpeakers_);

        bool anySolo = false;
        for (uint32_t src = 0; src < vectorActiveSourceCount_; ++src) anySolo = anySolo || sources_[src].solo;
        updateGainTargets(vectorActiveInputs_, vectorActiveSourceCount_, anySolo, sampleFrames);
        targetUpdateCountdown_ = kLayoutPannerTargetUpdateSamples;
    }

    void processVectorFrame(const float* in, float* out)
    {
        if (!out) return;
        for (uint32_t spk = 0; spk < vectorOutputChannels_; ++spk) out[spk] = 0.0f;
        const float gainCoef = cachedGainCoef_;
        for (uint32_t src = vectorActiveInputs_; src < vectorActiveSourceCount_; ++src) smoothSourceToSilence(src, gainCoef);
        for (uint32_t src = 0; src < vectorActiveInputs_; ++src) {
            const float input = in ? in[src] : 0.0f;
            if (!targetActive_[src] || std::abs(input) < 0.0000001f) {
                smoothSourceToSilence(src, gainCoef);
                continue;
            }
            const float weighted = input * sourceAmps_[src];
            for (uint32_t spk = 0; spk < vectorActiveOutputs_; ++spk) {
                const uint32_t gi = src * kLayoutPannerMaxSpeakers + spk;
                const float target = targetGains_[gi];
                smoothedGains_[gi] = target + (smoothedGains_[gi] - target) * gainCoef;
                out[spk] += weighted * smoothedGains_[gi];
            }
        }
        for (uint32_t spk = 0; spk < vectorActiveOutputs_; ++spk) {
            out[spk] = clamp(out[spk] * cachedOutputGain_, -4.0f, 4.0f);
        }
    }

    template <typename Sample>
    void processVectorFrameChannels(const Sample* const* in, Sample* const* out, uint32_t frame)
    {
        if (!out) return;
        for (uint32_t spk = 0; spk < vectorOutputChannels_; ++spk) {
            if (out[spk]) out[spk][frame] = static_cast<Sample>(0);
        }

        const float gainCoef = cachedGainCoef_;
        for (uint32_t src = vectorActiveInputs_; src < vectorActiveSourceCount_; ++src) smoothSourceToSilence(src, gainCoef);
        for (uint32_t src = 0; src < vectorActiveInputs_; ++src) {
            const float input = (in && in[src]) ? static_cast<float>(in[src][frame]) : 0.0f;
            if (!targetActive_[src] || std::abs(input) < 0.0000001f) {
                smoothSourceToSilence(src, gainCoef);
                continue;
            }
            const float weighted = input * sourceAmps_[src];
            for (uint32_t spk = 0; spk < vectorActiveOutputs_; ++spk) {
                const uint32_t gi = src * kLayoutPannerMaxSpeakers + spk;
                const float target = targetGains_[gi];
                smoothedGains_[gi] = target + (smoothedGains_[gi] - target) * gainCoef;
                if (out[spk]) out[spk][frame] = static_cast<Sample>(out[spk][frame] + static_cast<Sample>(weighted * smoothedGains_[gi]));
            }
        }

        for (uint32_t spk = 0; spk < vectorActiveOutputs_; ++spk) {
            if (out[spk]) out[spk][frame] = static_cast<Sample>(clamp(static_cast<float>(out[spk][frame]) * cachedOutputGain_, -4.0f, 4.0f));
        }
    }

    bool canProcessQuadOverhead2x6(uint32_t inputChannels, uint32_t outputChannels) const
    {
        return inputChannels == 2u
            && outputChannels == 6u
            && params_.layout == LayoutPannerPreset::QuadOverhead6
            && params_.activeSources == 2u
            && activeSpeakers_ == 6u;
    }

    bool canProcessPresetLayoutKernel(uint32_t inputChannels, uint32_t outputChannels) const
    {
        return params_.layout != LayoutPannerPreset::Custom
            && inputChannels == params_.activeSources
            && outputChannels == activeSpeakers_
            && outputChannels <= kLayoutPannerMaxSpeakers
            && params_.activeSources <= kLayoutPannerSources;
    }

    template <typename Sample>
    void processPresetLayoutBlock(const Sample* const* in, Sample* const* out, uint32_t inputChannels, uint32_t outputChannels, uint32_t sampleFrames)
    {
        if (!out || sampleFrames == 0u) return;
        inputChannels = std::min<uint32_t>(inputChannels, kLayoutPannerSources);
        outputChannels = std::min<uint32_t>(outputChannels, kLayoutPannerMaxSpeakers);
        const uint32_t activeSourceCount = std::min<uint32_t>(params_.activeSources, inputChannels);
        const uint32_t activeOutputs = std::min<uint32_t>(activeSpeakers_, outputChannels);

        bool anySolo = false;
        for (uint32_t src = 0; src < activeSourceCount; ++src) anySolo = anySolo || sources_[src].solo;
        updateGainTargets(activeSourceCount, activeSourceCount, anySolo, sampleFrames);
        targetUpdateCountdown_ = kLayoutPannerTargetUpdateSamples;

        const float gainCoef = cachedGainCoef_;
        const float outputGain = cachedOutputGain_;
        for (uint32_t spk = 0; spk < outputChannels; ++spk) {
            if (out[spk]) {
                std::fill(out[spk], out[spk] + sampleFrames, static_cast<Sample>(0));
            }
        }

        for (uint32_t frame = 0; frame < sampleFrames; ++frame) {
            for (uint32_t src = 0; src < activeSourceCount; ++src) {
                const float input = (in && in[src]) ? static_cast<float>(in[src][frame]) : 0.0f;
                if (!targetActive_[src] || std::abs(input) < 0.0000001f) {
                    smoothSourceToSilence(src, gainCoef);
                    continue;
                }
                const float weighted = input * sourceAmps_[src];
                float* g = smoothedGains_.data() + src * kLayoutPannerMaxSpeakers;
                const float* t = targetGains_.data() + src * kLayoutPannerMaxSpeakers;
                for (uint32_t spk = 0; spk < activeOutputs; ++spk) {
                    g[spk] = t[spk] + (g[spk] - t[spk]) * gainCoef;
                    if (out[spk]) out[spk][frame] = static_cast<Sample>(out[spk][frame] + static_cast<Sample>(weighted * g[spk]));
                }
            }

            for (uint32_t spk = 0; spk < activeOutputs; ++spk) {
                if (out[spk]) out[spk][frame] = static_cast<Sample>(clamp(static_cast<float>(out[spk][frame]) * outputGain, -4.0f, 4.0f));
            }
        }
    }

    template <typename Sample>
    void processQuadOverhead2x6Block(const Sample* const* in, Sample* const* out, uint32_t sampleFrames)
    {
        if (!out || sampleFrames == 0u) return;

        bool anySolo = false;
        for (uint32_t src = 0; src < 2u; ++src) anySolo = anySolo || sources_[src].solo;
        updateGainTargets(2u, 2u, anySolo, sampleFrames);
        targetUpdateCountdown_ = kLayoutPannerTargetUpdateSamples;

        const bool src0Active = targetActive_[0];
        const bool src1Active = targetActive_[1];
        const float src0Amp = sourceAmps_[0];
        const float src1Amp = sourceAmps_[1];
        const float gainCoef = cachedGainCoef_;
        const float outputGain = cachedOutputGain_;

        Sample* out0 = out[0];
        Sample* out1 = out[1];
        Sample* out2 = out[2];
        Sample* out3 = out[3];
        Sample* out4 = out[4];
        Sample* out5 = out[5];
        const Sample* in0 = in ? in[0] : nullptr;
        const Sample* in1 = in ? in[1] : nullptr;

        for (uint32_t frame = 0; frame < sampleFrames; ++frame) {
            const float s0 = (src0Active && in0) ? static_cast<float>(*in0++) * src0Amp : 0.0f;
            const float s1 = (src1Active && in1) ? static_cast<float>(*in1++) * src1Amp : 0.0f;
            float y0 = 0.0f;
            float y1 = 0.0f;
            float y2 = 0.0f;
            float y3 = 0.0f;
            float y4 = 0.0f;
            float y5 = 0.0f;

            if (std::abs(s0) >= 0.0000001f) {
                float* g = smoothedGains_.data();
                const float* t = targetGains_.data();
                g[0] = t[0] + (g[0] - t[0]) * gainCoef;
                g[1] = t[1] + (g[1] - t[1]) * gainCoef;
                g[2] = t[2] + (g[2] - t[2]) * gainCoef;
                g[3] = t[3] + (g[3] - t[3]) * gainCoef;
                g[4] = t[4] + (g[4] - t[4]) * gainCoef;
                g[5] = t[5] + (g[5] - t[5]) * gainCoef;
                y0 += s0 * g[0];
                y1 += s0 * g[1];
                y2 += s0 * g[2];
                y3 += s0 * g[3];
                y4 += s0 * g[4];
                y5 += s0 * g[5];
            } else {
                smoothSourceToSilence(0u, gainCoef);
            }

            if (std::abs(s1) >= 0.0000001f) {
                float* g = smoothedGains_.data() + kLayoutPannerMaxSpeakers;
                const float* t = targetGains_.data() + kLayoutPannerMaxSpeakers;
                g[0] = t[0] + (g[0] - t[0]) * gainCoef;
                g[1] = t[1] + (g[1] - t[1]) * gainCoef;
                g[2] = t[2] + (g[2] - t[2]) * gainCoef;
                g[3] = t[3] + (g[3] - t[3]) * gainCoef;
                g[4] = t[4] + (g[4] - t[4]) * gainCoef;
                g[5] = t[5] + (g[5] - t[5]) * gainCoef;
                y0 += s1 * g[0];
                y1 += s1 * g[1];
                y2 += s1 * g[2];
                y3 += s1 * g[3];
                y4 += s1 * g[4];
                y5 += s1 * g[5];
            } else {
                smoothSourceToSilence(1u, gainCoef);
            }

            if (out0) *out0++ = static_cast<Sample>(clamp(y0 * outputGain, -4.0f, 4.0f));
            if (out1) *out1++ = static_cast<Sample>(clamp(y1 * outputGain, -4.0f, 4.0f));
            if (out2) *out2++ = static_cast<Sample>(clamp(y2 * outputGain, -4.0f, 4.0f));
            if (out3) *out3++ = static_cast<Sample>(clamp(y3 * outputGain, -4.0f, 4.0f));
            if (out4) *out4++ = static_cast<Sample>(clamp(y4 * outputGain, -4.0f, 4.0f));
            if (out5) *out5++ = static_cast<Sample>(clamp(y5 * outputGain, -4.0f, 4.0f));
        }
    }

private:
    void setSpeaker(uint32_t index, float azimuthDeg, float elevationDeg, float distance = 1.0f)
    {
        if (index >= kLayoutPannerMaxSpeakers) return;
        speakers_[index].azimuthDeg = layoutPannerWrapDeg(azimuthDeg);
        speakers_[index].elevationDeg = clamp(elevationDeg, -90.0f, 90.0f);
        speakers_[index].distance = clamp(distance, 0.1f, 3.0f);
    }

    void setSpeakerFromXyz(uint32_t index, float x, float y, float z)
    {
        const float distance = std::sqrt(x * x + y * y + z * z);
        if (distance < 0.000001f) {
            setSpeaker(index, 0.0f, 0.0f, 1.0f);
            return;
        }
        const float inv = 1.0f / distance;
        const float az = std::atan2(y, x) * 180.0f / kPi;
        const float el = std::asin(clamp(z * inv, -1.0f, 1.0f)) * 180.0f / kPi;
        setSpeaker(index, az, el, distance);
    }

    void setSpeakerFromRoomXyz(uint32_t index, float x, float y, float z)
    {
        const float distance = std::sqrt(x * x + y * y + z * z);
        if (distance < 0.000001f) {
            setSpeaker(index, 0.0f, 0.0f, 1.0f);
            return;
        }
        const float inv = 1.0f / distance;
        const float az = -std::atan2(x, y) * 180.0f / kPi;
        const float el = std::asin(clamp(z * inv, -1.0f, 1.0f)) * 180.0f / kPi;
        setSpeaker(index, az, el, distance);
    }

    void setRing(uint32_t base, uint32_t count, float startAzimuthDeg, float elevationDeg)
    {
        const float step = 360.0f / static_cast<float>(count);
        for (uint32_t i = 0; i < count; ++i) {
            setSpeaker(base + i, startAzimuthDeg - step * static_cast<float>(i), elevationDeg);
        }
    }

    void setSurroundBase(uint32_t count)
    {
        if (count == 5u) {
            setSpeaker(0, -30.0f, 0.0f);
            setSpeaker(1, -110.0f, 0.0f);
            setSpeaker(2, 110.0f, 0.0f);
            setSpeaker(3, 30.0f, 0.0f);
            setSpeaker(4, 0.0f, 0.0f);
        } else if (count == 6u) {
            setSpeaker(0, -30.0f, 0.0f);
            setSpeaker(1, -110.0f, 0.0f);
            setSpeaker(2, 180.0f, 0.0f);
            setSpeaker(3, 110.0f, 0.0f);
            setSpeaker(4, 30.0f, 0.0f);
            setSpeaker(5, 0.0f, 0.0f);
        } else if (count == 7u) {
            setSpeaker(0, -30.0f, 0.0f);
            setSpeaker(1, -110.0f, 0.0f);
            setSpeaker(2, -150.0f, 0.0f);
            setSpeaker(3, 150.0f, 0.0f);
            setSpeaker(4, 110.0f, 0.0f);
            setSpeaker(5, 30.0f, 0.0f);
            setSpeaker(6, 0.0f, 0.0f);
        } else if (count == 9u) {
            setSpeaker(0, -30.0f, 0.0f);
            setSpeaker(1, -60.0f, 0.0f);
            setSpeaker(2, -110.0f, 0.0f);
            setSpeaker(3, -150.0f, 0.0f);
            setSpeaker(4, 150.0f, 0.0f);
            setSpeaker(5, 110.0f, 0.0f);
            setSpeaker(6, 60.0f, 0.0f);
            setSpeaker(7, 30.0f, 0.0f);
            setSpeaker(8, 0.0f, 0.0f);
        } else if (count == 11u) {
            setSpeaker(0, -30.0f, 0.0f);
            setSpeaker(1, -50.0f, 0.0f);
            setSpeaker(2, -70.0f, 0.0f);
            setSpeaker(3, -110.0f, 0.0f);
            setSpeaker(4, -150.0f, 0.0f);
            setSpeaker(5, 180.0f, 0.0f);
            setSpeaker(6, 150.0f, 0.0f);
            setSpeaker(7, 110.0f, 0.0f);
            setSpeaker(8, 70.0f, 0.0f);
            setSpeaker(9, 30.0f, 0.0f);
            setSpeaker(10, 0.0f, 0.0f);
        }
    }

    void setOverheadPair(uint32_t base)
    {
        setSpeaker(base, -45.0f, 60.0f, 0.88f);
        setSpeaker(base + 1u, 45.0f, 60.0f, 0.88f);
    }

    void setOverheadQuad(uint32_t base)
    {
        setSpeaker(base, -45.0f, 55.0f, 0.9f);
        setSpeaker(base + 1u, -135.0f, 55.0f, 0.9f);
        setSpeaker(base + 2u, 135.0f, 55.0f, 0.9f);
        setSpeaker(base + 3u, 45.0f, 55.0f, 0.9f);
    }

    void setOverheadRing(uint32_t base, uint32_t count)
    {
        if (count == 2u) {
            setOverheadPair(base);
        } else if (count == 4u) {
            setOverheadQuad(base);
        } else if (count == 6u) {
            static constexpr float az[6] { -45.0f, -90.0f, -135.0f, 135.0f, 90.0f, 45.0f };
            for (uint32_t i = 0; i < 6u; ++i) setSpeaker(base + i, az[i], 55.0f, 0.9f);
        } else if (count == 8u) {
            static constexpr float az[8] { -30.0f, -75.0f, -120.0f, -165.0f, 165.0f, 120.0f, 75.0f, 30.0f };
            for (uint32_t i = 0; i < 8u; ++i) setSpeaker(base + i, az[i], 55.0f, 0.9f);
        }
    }

    void setSrstDome25(bool includeZenith)
    {
        static constexpr float lower[12] {
            -30.0f, -60.0f, -90.0f, -120.0f, -150.0f, 180.0f,
            150.0f, 120.0f, 90.0f, 60.0f, 30.0f, 0.0f
        };
        static constexpr float middle[8] {
            -45.0f, -90.0f, -135.0f, 180.0f, 135.0f, 90.0f, 45.0f, 0.0f
        };
        static constexpr float upper[4] {
            -90.0f, 180.0f, 90.0f, 0.0f
        };
        activeSpeakers_ = includeZenith ? 25u : 24u;
        for (uint32_t i = 0; i < 12u; ++i) setSpeaker(i, lower[i], 0.0f);
        for (uint32_t i = 0; i < 8u; ++i) setSpeaker(12u + i, middle[i], 32.0f);
        for (uint32_t i = 0; i < 4u; ++i) setSpeaker(20u + i, upper[i], 66.6f);
        if (includeZenith) setSpeaker(24u, 0.0f, 90.0f);
    }

    static Vec3 normalized(Vec3 v)
    {
        const float d = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
        if (d <= 0.000001f) return {};
        const float inv = 1.0f / d;
        return { v.x * inv, v.y * inv, v.z * inv };
    }

    static float distanceSquared(Vec3 a, Vec3 b)
    {
        const float dx = a.x - b.x;
        const float dy = a.y - b.y;
        const float dz = a.z - b.z;
        return dx * dx + dy * dy + dz * dz;
    }

    static float vecAzimuthDeg(Vec3 v)
    {
        return layoutPannerWrapDeg(std::atan2(v.y, v.x) * 180.0f / kPi);
    }

    static float vecElevationDeg(Vec3 v)
    {
        const float h = std::sqrt(v.x * v.x + v.y * v.y);
        return std::atan2(v.z, h) * 180.0f / kPi;
    }

    void setIcosahedronFaceCenterLayout()
    {
        constexpr float phi = 1.61803398875f;
        const std::array<Vec3, 12> vertices {{
            { 0.0f, 1.0f, phi }, { 0.0f, -1.0f, phi }, { 0.0f, 1.0f, -phi }, { 0.0f, -1.0f, -phi },
            { 1.0f, phi, 0.0f }, { -1.0f, phi, 0.0f }, { 1.0f, -phi, 0.0f }, { -1.0f, -phi, 0.0f },
            { phi, 0.0f, 1.0f }, { -phi, 0.0f, 1.0f }, { phi, 0.0f, -1.0f }, { -phi, 0.0f, -1.0f },
        }};
        std::array<Vec3, 12> verts {};
        for (uint32_t i = 0; i < vertices.size(); ++i) verts[i] = normalized(vertices[i]);

        float minD2 = 999999.0f;
        for (uint32_t a = 0; a < verts.size(); ++a) {
            for (uint32_t b = a + 1; b < verts.size(); ++b) {
                const float d2 = distanceSquared(verts[a], verts[b]);
                if (d2 > 0.0001f && d2 < minD2) minD2 = d2;
            }
        }

        std::array<Vec3, 20> centers {};
        uint32_t count = 0;
        const float maxD2 = minD2 * 1.08f;
        for (uint32_t a = 0; a < verts.size(); ++a) {
            for (uint32_t b = a + 1; b < verts.size(); ++b) {
                if (distanceSquared(verts[a], verts[b]) > maxD2) continue;
                for (uint32_t c = b + 1; c < verts.size(); ++c) {
                    if (distanceSquared(verts[a], verts[c]) > maxD2 || distanceSquared(verts[b], verts[c]) > maxD2) continue;
                    if (count < centers.size()) {
                        centers[count++] = normalized({ verts[a].x + verts[b].x + verts[c].x,
                            verts[a].y + verts[b].y + verts[c].y,
                            verts[a].z + verts[b].z + verts[c].z });
                    }
                }
            }
        }

        std::sort(centers.begin(), centers.begin() + static_cast<std::ptrdiff_t>(count), [](Vec3 a, Vec3 b) {
            const float ea = vecElevationDeg(a);
            const float eb = vecElevationDeg(b);
            if (std::fabs(ea - eb) > 0.001f) return ea < eb;
            return vecAzimuthDeg(a) < vecAzimuthDeg(b);
        });

        activeSpeakers_ = std::min<uint32_t>(20u, count);
        for (uint32_t i = 0; i < activeSpeakers_; ++i) {
            setSpeakerFromXyz(i, centers[i].x, centers[i].y, centers[i].z);
            speakers_[i].distance = 1.0f;
        }
    }

    void addCanonicalPolyhedron(LayoutPannerCustomShape shape)
    {
        constexpr float phi = 1.61803398875f;
        constexpr float invPhi = 0.61803398875f;
        switch (shape) {
        case LayoutPannerCustomShape::Tetra: {
            activeSpeakers_ = 4;
            static constexpr float pts[4][3] {
                { 1, 1, 1 }, { 1, -1, -1 }, { -1, 1, -1 }, { -1, -1, 1 },
            };
            for (uint32_t i = 0; i < 4; ++i) setSpeakerFromXyz(i, pts[i][0], pts[i][1], pts[i][2]);
            break;
        }
        case LayoutPannerCustomShape::Octa: {
            activeSpeakers_ = 6;
            static constexpr float pts[6][3] {
                { 1, 0, 0 }, { 0, -1, 0 }, { -1, 0, 0 }, { 0, 1, 0 },
                { 0, 0, 1 }, { 0, 0, -1 },
            };
            for (uint32_t i = 0; i < 6; ++i) setSpeakerFromXyz(i, pts[i][0], pts[i][1], pts[i][2]);
            break;
        }
        case LayoutPannerCustomShape::Cube: {
            activeSpeakers_ = 8;
            static constexpr float pts[8][3] {
                { 1, -1, -1 }, { -1, -1, -1 }, { -1, 1, -1 }, { 1, 1, -1 },
                { 1, -1, 1 }, { -1, -1, 1 }, { -1, 1, 1 }, { 1, 1, 1 },
            };
            for (uint32_t i = 0; i < 8; ++i) setSpeakerFromXyz(i, pts[i][0], pts[i][1], pts[i][2]);
            break;
        }
        case LayoutPannerCustomShape::Icosa: {
            activeSpeakers_ = 12;
            const float pts[12][3] {
                { 0, 1, phi }, { 0, -1, phi }, { 0, 1, -phi }, { 0, -1, -phi },
                { 1, phi, 0 }, { -1, phi, 0 }, { 1, -phi, 0 }, { -1, -phi, 0 },
                { phi, 0, 1 }, { -phi, 0, 1 }, { phi, 0, -1 }, { -phi, 0, -1 },
            };
            for (uint32_t i = 0; i < 12; ++i) setSpeakerFromXyz(i, pts[i][0], pts[i][1], pts[i][2]);
            break;
        }
        case LayoutPannerCustomShape::Dodeca: {
            activeSpeakers_ = 20;
            const float pts[20][3] {
                { 1, 1, 1 }, { 1, 1, -1 }, { 1, -1, 1 }, { 1, -1, -1 },
                { -1, 1, 1 }, { -1, 1, -1 }, { -1, -1, 1 }, { -1, -1, -1 },
                { 0, invPhi, phi }, { 0, invPhi, -phi }, { 0, -invPhi, phi }, { 0, -invPhi, -phi },
                { invPhi, phi, 0 }, { invPhi, -phi, 0 }, { -invPhi, phi, 0 }, { -invPhi, -phi, 0 },
                { phi, 0, invPhi }, { phi, 0, -invPhi }, { -phi, 0, invPhi }, { -phi, 0, -invPhi },
            };
            for (uint32_t i = 0; i < 20; ++i) setSpeakerFromXyz(i, pts[i][0], pts[i][1], pts[i][2]);
            break;
        }
        default:
            break;
        }
    }

    static LayoutPannerCustomShape autoShapeForCount(uint32_t count)
    {
        if (count == 4u) return LayoutPannerCustomShape::Tetra;
        if (count == 6u) return LayoutPannerCustomShape::Octa;
        if (count == 8u) return LayoutPannerCustomShape::Cube;
        if (count == 12u) return LayoutPannerCustomShape::Icosa;
        if (count == 20u) return LayoutPannerCustomShape::Dodeca;
        if (count <= 16u) return LayoutPannerCustomShape::Geo;
        return LayoutPannerCustomShape::Dome;
    }

    void generateRingLayout(uint32_t count)
    {
        activeSpeakers_ = std::clamp<uint32_t>(count, 2u, kLayoutPannerMaxSpeakers);
        setRing(0, activeSpeakers_, kLayoutPannerDefaultFirstAzimuthDeg, 0.0f);
    }

    void generateStackLayout(uint32_t count)
    {
        activeSpeakers_ = std::clamp<uint32_t>(count, 2u, kLayoutPannerMaxSpeakers);
        const uint32_t lower = (activeSpeakers_ + 1u) / 2u;
        const uint32_t upper = activeSpeakers_ - lower;
        setRing(0, lower, kLayoutPannerDefaultFirstAzimuthDeg, 0.0f);
        if (upper > 0u) setRing(lower, upper, kLayoutPannerDefaultFirstAzimuthDeg, 45.0f);
    }

    void generateDomeLayout(uint32_t count)
    {
        activeSpeakers_ = std::clamp<uint32_t>(count, 2u, kLayoutPannerMaxSpeakers);
        const uint32_t lower = activeSpeakers_ <= 8u ? activeSpeakers_ : std::max<uint32_t>(4u, activeSpeakers_ / 2u);
        const uint32_t mid = activeSpeakers_ <= 8u ? 0u : (activeSpeakers_ - lower) * 2u / 3u;
        const uint32_t top = activeSpeakers_ - lower - mid;
        setRing(0, lower, kLayoutPannerDefaultFirstAzimuthDeg, 0.0f);
        if (mid > 0u) setRing(lower, mid, kLayoutPannerDefaultFirstAzimuthDeg, 36.0f);
        if (top == 1u) setSpeaker(lower + mid, 0.0f, 90.0f);
        else if (top > 1u) setRing(lower + mid, top, kLayoutPannerDefaultFirstAzimuthDeg, 68.0f);
    }

    void generateGeoLayout(uint32_t count, bool hemisphere)
    {
        activeSpeakers_ = std::clamp<uint32_t>(count, 2u, kLayoutPannerMaxSpeakers);
        const float golden = kPi * (3.0f - std::sqrt(5.0f));
        const float denom = static_cast<float>(std::max<uint32_t>(1u, activeSpeakers_ - 1u));
        for (uint32_t i = 0; i < activeSpeakers_; ++i) {
            const float z = hemisphere
                ? static_cast<float>(i) / denom
                : 1.0f - 2.0f * (static_cast<float>(i) + 0.5f) / static_cast<float>(activeSpeakers_);
            const float radius = std::sqrt(std::max(0.0f, 1.0f - z * z));
            const float angle = -45.0f * kPi / 180.0f - golden * static_cast<float>(i);
            setSpeakerFromXyz(i, std::cos(angle) * radius, std::sin(angle) * radius, z);
            speakers_[i].distance = 1.0f;
        }
    }

    void generateCustomLayout(LayoutPannerCustomShape shape, uint32_t count)
    {
        for (auto& speaker : speakers_) speaker = {};
        params_.layout = LayoutPannerPreset::Custom;
        params_.customShape = shape;
        const LayoutPannerCustomShape resolved = shape == LayoutPannerCustomShape::Auto
            ? autoShapeForCount(std::clamp<uint32_t>(count, 2u, kLayoutPannerMaxSpeakers))
            : shape;
        switch (resolved) {
        case LayoutPannerCustomShape::Ring: generateRingLayout(count); break;
        case LayoutPannerCustomShape::Dome: generateDomeLayout(count); break;
        case LayoutPannerCustomShape::Tetra:
        case LayoutPannerCustomShape::Octa:
        case LayoutPannerCustomShape::Cube:
        case LayoutPannerCustomShape::Icosa:
        case LayoutPannerCustomShape::Dodeca:
            addCanonicalPolyhedron(resolved);
            break;
        case LayoutPannerCustomShape::Stack: generateStackLayout(count); break;
        case LayoutPannerCustomShape::Geo:
        case LayoutPannerCustomShape::Auto:
        default:
            generateGeoLayout(count, false);
            break;
        }
        params_.activeSpeakers = activeSpeakers_;
        params_.selectedSpeaker = std::min<uint32_t>(params_.selectedSpeaker, activeSpeakers_ - 1u);
    }

    void applyLayout(LayoutPannerPreset layout)
    {
        for (auto& speaker : speakers_) speaker = {};
        switch (layout) {
        case LayoutPannerPreset::Custom:
            generateCustomLayout(params_.customShape, params_.activeSpeakers);
            return;
        case LayoutPannerPreset::Cube8:
            activeSpeakers_ = 8;
            setSpeaker(0, -45.0f, -35.2644f, 1.0f);
            setSpeaker(1, -135.0f, -35.2644f, 1.0f);
            setSpeaker(2, 135.0f, -35.2644f, 1.0f);
            setSpeaker(3, 45.0f, -35.2644f, 1.0f);
            setSpeaker(4, -45.0f, 35.2644f, 1.0f);
            setSpeaker(5, -135.0f, 35.2644f, 1.0f);
            setSpeaker(6, 135.0f, 35.2644f, 1.0f);
            setSpeaker(7, 45.0f, 35.2644f, 1.0f);
            break;
        case LayoutPannerPreset::Cube17: {
            activeSpeakers_ = 17;
            static constexpr float pts[17][3] {
                { 1, -1, -1 }, { -1, -1, -1 }, { -1, 1, -1 }, { 1, 1, -1 },
                { 1, -1, 0 }, { 0, -1, 0 }, { -1, -1, 0 }, { -1, 0, 0 },
                { -1, 1, 0 }, { 0, 1, 0 }, { 1, 1, 0 }, { 1, 0, 0 },
                { 1, -1, 1 }, { -1, -1, 1 }, { -1, 1, 1 }, { 1, 1, 1 },
                { 0, 0, 1 },
            };
            for (uint32_t i = 0; i < 17; ++i) setSpeakerFromXyz(i, pts[i][0], pts[i][1], pts[i][2]);
            break;
        }
        case LayoutPannerPreset::Cube41:
            activeSpeakers_ = kCube41SpeakerCount;
            for (uint32_t i = 0; i < kCube41SpeakerCount; ++i) {
                const auto& p = kCube41Points[i];
                setSpeakerFromXyz(i, p.x, p.y, p.z);
            }
            break;
        case LayoutPannerPreset::Lpac41:
            activeSpeakers_ = kCube41SpeakerCount;
            for (uint32_t i = 0; i < kCube41SpeakerCount; ++i) {
                const auto& p = kLpac41Points[i];
                setSpeakerFromXyz(i, p.x, p.y, p.z);
            }
            break;
        case LayoutPannerPreset::Dodeca12:
            activeSpeakers_ = 12;
            setSpeaker(0, -31.717474f, 0.0f);
            setSpeaker(1, -90.0f, -31.717474f);
            setSpeaker(2, -90.0f, 31.717474f);
            setSpeaker(3, -148.282526f, 0.0f);
            setSpeaker(4, 180.0f, -58.282526f);
            setSpeaker(5, 180.0f, 58.282526f);
            setSpeaker(6, 148.282526f, 0.0f);
            setSpeaker(7, 90.0f, -31.717474f);
            setSpeaker(8, 90.0f, 31.717474f);
            setSpeaker(9, 31.717474f, 0.0f);
            setSpeaker(10, 0.0f, -58.282526f);
            setSpeaker(11, 0.0f, 58.282526f);
            break;
        case LayoutPannerPreset::Icosahedron20:
            setIcosahedronFaceCenterLayout();
            break;
        case LayoutPannerPreset::Dome24NoOverhead:
            setSrstDome25(false);
            break;
        case LayoutPannerPreset::Dome25:
            setSrstDome25(true);
            break;
        case LayoutPannerPreset::Srst25:
            activeSpeakers_ = kSrst25SpeakerCount;
            for (uint32_t i = 0; i < kSrst25SpeakerCount; ++i) {
                const auto& p = kSrst25Points[i];
                setSpeakerFromRoomXyz(i, p.x, p.y, p.z);
                speakers_[i].distance = 1.0f;
            }
            break;
        case LayoutPannerPreset::DoubleRing16:
            activeSpeakers_ = 16;
            setRing(0, 8, kLayoutPannerDefaultFirstAzimuthDeg, 0.0f);
            setRing(8, 8, kLayoutPannerDefaultFirstAzimuthDeg, 45.0f);
            break;
        case LayoutPannerPreset::DoubleRing20:
            activeSpeakers_ = 20;
            setRing(0, 12, kLayoutPannerDefaultFirstAzimuthDeg, 0.0f);
            setRing(12, 8, kLayoutPannerDefaultFirstAzimuthDeg, 45.0f);
            break;
        case LayoutPannerPreset::OctophonicRing:
            activeSpeakers_ = 8;
            setRing(0, 8, -45.0f, 0.0f);
            break;
        case LayoutPannerPreset::Quad:
            activeSpeakers_ = 4;
            setSpeaker(0, 45.0f, 0.0f);
            setSpeaker(1, -45.0f, 0.0f);
            setSpeaker(2, -135.0f, 0.0f);
            setSpeaker(3, 135.0f, 0.0f);
            break;
        case LayoutPannerPreset::QuadOverhead6:
            activeSpeakers_ = 6;
            setSpeaker(0, 45.0f, 0.0f);
            setSpeaker(1, -45.0f, 0.0f);
            setSpeaker(2, -135.0f, 0.0f);
            setSpeaker(3, 135.0f, 0.0f);
            setSpeaker(4, 90.0f, 60.0f);
            setSpeaker(5, -90.0f, 60.0f);
            break;
        case LayoutPannerPreset::Ring12:
            activeSpeakers_ = 12;
            setRing(0, 12, kLayoutPannerDefaultFirstAzimuthDeg, 0.0f);
            break;
        case LayoutPannerPreset::Ring16:
            activeSpeakers_ = 16;
            setRing(0, 16, kLayoutPannerDefaultFirstAzimuthDeg, 0.0f);
            break;
        case LayoutPannerPreset::FiveZero:
            activeSpeakers_ = 5;
            setSurroundBase(5);
            break;
        case LayoutPannerPreset::SixZero:
            activeSpeakers_ = 6;
            setSurroundBase(6);
            break;
        case LayoutPannerPreset::SevenZero:
            activeSpeakers_ = 7;
            setSurroundBase(7);
            break;
        case LayoutPannerPreset::FiveZeroTwo:
            activeSpeakers_ = 7;
            setSurroundBase(5);
            setOverheadPair(5);
            break;
        case LayoutPannerPreset::SevenZeroTwo:
            activeSpeakers_ = 9;
            setSurroundBase(7);
            setOverheadPair(7);
            break;
        case LayoutPannerPreset::FiveZeroFour:
            activeSpeakers_ = 9;
            setSurroundBase(5);
            setOverheadQuad(5);
            break;
        case LayoutPannerPreset::SevenZeroFour:
            activeSpeakers_ = 11;
            setSurroundBase(7);
            setOverheadRing(7, 4);
            break;
        case LayoutPannerPreset::NineZero:
            activeSpeakers_ = 9;
            setSurroundBase(9);
            break;
        case LayoutPannerPreset::NineZeroTwo:
            activeSpeakers_ = 11;
            setSurroundBase(9);
            setOverheadRing(9, 2);
            break;
        case LayoutPannerPreset::NineZeroFour:
            activeSpeakers_ = 13;
            setSurroundBase(9);
            setOverheadRing(9, 4);
            break;
        case LayoutPannerPreset::NineZeroSix:
            activeSpeakers_ = 15;
            setSurroundBase(9);
            setOverheadRing(9, 6);
            break;
        case LayoutPannerPreset::SevenZeroSix:
            activeSpeakers_ = 13;
            setSurroundBase(7);
            setOverheadRing(7, 6);
            break;
        case LayoutPannerPreset::ElevenZeroEight:
        default:
            activeSpeakers_ = 19;
            setSurroundBase(11);
            setOverheadRing(11, 8);
            break;
        }
        params_.activeSpeakers = activeSpeakers_;
        params_.selectedSpeaker = std::min<uint32_t>(params_.selectedSpeaker, std::max<uint32_t>(1u, activeSpeakers_) - 1u);
    }

    float smoothingCoeff(float ms) const
    {
        return std::exp(-1000.0f / (std::max(1.0f, ms) * static_cast<float>(sampleRate_)));
    }

    float distanceAmp(float distance) const
    {
        if (distance <= 1.0f && params_.insideMode == LayoutPannerInsideMode::Attenuate) {
            return clamp(distance, 0.0f, 1.0f);
        }
        if (distance <= 1.0f) return 1.0f;
        const float exponent = params_.distanceRolloffDb / 6.0f;
        return 1.0f / std::pow(distance, std::max(0.0f, exponent));
    }

    float distanceDiffusionFactor(float distance) const
    {
        if (distance < 1.0f && params_.insideMode == LayoutPannerInsideMode::Diffuse) {
            return 1.0f + (1.0f - distance) * params_.distanceDiffusion * 2.0f;
        }
        return 1.0f + std::max(0.0f, distance - 1.0f) * params_.distanceDiffusion;
    }

    uint32_t buildSolverSpeakers(std::array<LayoutPannerSolverSpeaker, kLayoutPannerMaxSolverSpeakers>& solver) const
    {
        uint32_t count = 0u;
        float minElev = 90.0f;
        float maxElev = -90.0f;
        for (uint32_t i = 0; i < activeSpeakers_ && count < solver.size(); ++i) {
            const auto& speaker = speakers_[i];
            solver[count++] = {
                directionFromAed(speaker.azimuthDeg, speaker.elevationDeg),
                speaker.azimuthDeg,
                speaker.elevationDeg,
                i,
                true
            };
            minElev = std::min(minElev, speaker.elevationDeg);
            maxElev = std::max(maxElev, speaker.elevationDeg);
        }

        if (layoutIsPlanar2d()) {
            if (count < solver.size()) solver[count++] = { directionFromAed(0.0f, 90.0f), 0.0f, 90.0f, 0u, false, false };
            if (count < solver.size()) solver[count++] = { directionFromAed(0.0f, -90.0f), 0.0f, -90.0f, 0u, false, false };
            return count;
        }
        if (maxElev < 5.0f || minElev < -10.0f) return count;

        if (count < solver.size()) {
            solver[count++] = { directionFromAed(0.0f, -90.0f), 0.0f, -90.0f, 0u, false, false };
        }
        return count;

        std::array<uint32_t, kLayoutPannerMaxSpeakers> lowerIds {};
        uint32_t lowerCount = 0u;
        for (uint32_t i = 0; i < activeSpeakers_; ++i) {
            if (std::fabs(speakers_[i].elevationDeg - minElev) <= 8.0f && lowerCount < lowerIds.size()) {
                lowerIds[lowerCount++] = i;
            }
        }
        if (lowerCount == 0u) {
            static constexpr float fallbackAz[8] { -45.0f, -90.0f, -135.0f, 180.0f, 135.0f, 90.0f, 45.0f, 0.0f };
            for (float az : fallbackAz) {
                if (count >= solver.size()) break;
                solver[count++] = { directionFromAed(az, -60.0f), az, -60.0f, 0u, false };
            }
            return count;
        }

        std::sort(lowerIds.begin(), lowerIds.begin() + lowerCount, [&](uint32_t a, uint32_t b) {
            float aa = layoutPannerWrapDeg(speakers_[a].azimuthDeg);
            float bb = layoutPannerWrapDeg(speakers_[b].azimuthDeg);
            if (aa < 0.0f) aa += 360.0f;
            if (bb < 0.0f) bb += 360.0f;
            return aa < bb;
        });
        const uint32_t stride = std::max<uint32_t>(1u, (lowerCount + 15u) / 16u);
        for (uint32_t i = 0; i < lowerCount && count < solver.size(); i += stride) {
            const float az = speakers_[lowerIds[i]].azimuthDeg;
            solver[count++] = { directionFromAed(az, -60.0f), az, -60.0f, 0u, false };
        }
        return count;
    }

    float computeGain(uint32_t speakerIndex, Vec3 sourcePosition, float sourceDistance,
        const LayoutPannerSolverSpeaker* solver = nullptr, uint32_t solverCount = 0u) const
    {
        const auto& speaker = speakers_[speakerIndex];
        const Vec3 dir = directionFromAed(speaker.azimuthDeg, speaker.elevationDeg);
        const Vec3 sp { dir.x * speaker.distance, dir.y * speaker.distance, dir.z * speaker.distance };
        if (params_.method == LayoutPannerMethod::Cosine) {
            const Vec3 srcDir = normalize(sourcePosition);
            const float dot = srcDir.x * dir.x + srcDir.y * dir.y + srcDir.z * dir.z;
            return std::pow(std::max(0.0f, dot), std::max(0.25f, params_.focus));
        }
        const float dx = sourcePosition.x - sp.x;
        const float dy = sourcePosition.y - sp.y;
        const float dz = sourcePosition.z - sp.z;
        if (params_.method == LayoutPannerMethod::Dbap) {
            const float blur = 0.015f + params_.distanceDiffusion * 0.65f;
            const float dist = std::sqrt(dx * dx + dy * dy + dz * dz + blur * blur);
            const float rolloffExponent = std::max(0.05f, params_.distanceRolloffDb / 6.0f);
            const float focus = std::max(0.25f, params_.focus);
            return 1.0f / std::pow(std::max(0.001f, dist), rolloffExponent * focus);
        }
        if (params_.method == LayoutPannerMethod::Lbap) {
            const float sourceAz = vecAzimuthDeg(sourcePosition);
            float sourceEl = vecElevationDeg(sourcePosition);
            if (missingLowerHemisphereSupport() && sourceEl < 0.0f) sourceEl = 0.0f;
            float azStep = 180.0f;
            float elStep = 90.0f;
            if (solver && solverCount > 0u) {
                for (uint32_t i = 0; i < solverCount; ++i) {
                    const auto& other = solver[i];
                    if (other.real && other.realIndex == speakerIndex) continue;
                    const float elDiff = std::fabs(other.elevationDeg - speaker.elevationDeg);
                    if (elDiff < 1.0f) {
                        const float azDiff = std::fabs(layoutPannerWrapDeg(other.azimuthDeg - speaker.azimuthDeg));
                        if (azDiff > 0.1f) azStep = std::min(azStep, azDiff);
                    } else {
                        elStep = std::min(elStep, elDiff);
                    }
                }
            } else {
                for (uint32_t i = 0; i < activeSpeakers_; ++i) {
                    if (i == speakerIndex) continue;
                    const auto& other = speakers_[i];
                    const float elDiff = std::fabs(other.elevationDeg - speaker.elevationDeg);
                    if (elDiff < 1.0f) {
                        const float azDiff = std::fabs(layoutPannerWrapDeg(other.azimuthDeg - speaker.azimuthDeg));
                        if (azDiff > 0.1f) azStep = std::min(azStep, azDiff);
                    } else {
                        elStep = std::min(elStep, elDiff);
                    }
                }
            }
            if (std::fabs(speaker.elevationDeg) > 88.0f) azStep = 180.0f;
            azStep = std::clamp(azStep, 15.0f, 180.0f);
            elStep = std::clamp(elStep, 15.0f, 90.0f);

            const float azDiff = std::fabs(layoutPannerWrapDeg(sourceAz - speaker.azimuthDeg));
            const float elDiff = std::fabs(sourceEl - speaker.elevationDeg);
            const float azWeight = azDiff >= azStep ? 0.0f : std::cos((azDiff / azStep) * (kPi * 0.5f));
            const float elWeight = elDiff >= elStep ? 0.0f : std::cos((elDiff / elStep) * (kPi * 0.5f));
            const float focus = std::max(0.25f, params_.focus);
            const float local = std::pow(std::max(0.0f, azWeight * elWeight), focus);
            const float insideBlend = std::clamp(1.0f - sourceDistance, 0.0f, 1.0f);
            return local * (1.0f - insideBlend) + insideBlend;
        }
        const float dist = std::sqrt(dx * dx + dy * dy + dz * dz) + 0.04f;
        const float focus = std::max(0.25f, params_.focus / distanceDiffusionFactor(sourceDistance));
        return 1.0f / std::pow(dist, focus);
    }

    Vec3 speakerUnit(uint32_t index) const
    {
        const auto& speaker = speakers_[index];
        return directionFromAed(speaker.azimuthDeg, speaker.elevationDeg);
    }

    static Vec3 solverUnit(const std::array<LayoutPannerSolverSpeaker, kLayoutPannerMaxSolverSpeakers>& solver, uint32_t index)
    {
        return solver[index].dir;
    }

    bool layoutIsPlanar2d() const
    {
        if (activeSpeakers_ < 2u) return false;
        const float ref = speakers_[0].elevationDeg;
        for (uint32_t i = 1; i < activeSpeakers_; ++i) {
            if (std::fabs(speakers_[i].elevationDeg - ref) > 1.0f) return false;
        }
        return std::fabs(ref) < 5.0f;
    }

    bool missingLowerHemisphereSupport() const
    {
        if (layoutIsPlanar2d()) return false;
        float minElev = 90.0f;
        float maxElev = -90.0f;
        for (uint32_t spk = 0; spk < activeSpeakers_; ++spk) {
            minElev = std::min(minElev, speakers_[spk].elevationDeg);
            maxElev = std::max(maxElev, speakers_[spk].elevationDeg);
        }
        return maxElev >= 5.0f && minElev >= -10.0f;
    }

    static float positiveAzimuth(float azimuthDeg)
    {
        float az = layoutPannerWrapDeg(azimuthDeg);
        return az < 0.0f ? az + 360.0f : az;
    }

    static float angularDistanceDeg(float a, float b)
    {
        return std::fabs(layoutPannerWrapDeg(a - b));
    }

    static void addVbapPair(LayoutPannerVbapTopology& topology, uint32_t a, uint32_t b)
    {
        if (a == b || topology.pairCount >= topology.pairs.size()) return;
        const uint32_t lo = std::min(a, b);
        const uint32_t hi = std::max(a, b);
        for (uint32_t i = 0; i < topology.pairCount; ++i) {
            if (topology.pairs[i].a == lo && topology.pairs[i].b == hi) return;
        }
        topology.pairs[topology.pairCount++] = { lo, hi };
    }

    static void addVbapFacet(LayoutPannerVbapTopology& topology, uint32_t a, uint32_t b, uint32_t c)
    {
        if (a == b || a == c || b == c || topology.facetCount >= topology.facets.size()) return;
        std::array<uint32_t, 3> sorted { a, b, c };
        std::sort(sorted.begin(), sorted.end());
        for (uint32_t i = 0; i < topology.facetCount; ++i) {
            const auto& f = topology.facets[i];
            if (f.a == sorted[0] && f.b == sorted[1] && f.c == sorted[2]) return;
        }
        topology.facets[topology.facetCount++] = { sorted[0], sorted[1], sorted[2] };
    }

    uint32_t nearestInBandByAzimuth(const LayoutPannerSolverSpeaker* solver,
        const uint32_t* ids, uint32_t count, float azimuthDeg) const
    {
        uint32_t best = ids[0];
        float bestD = 999999.0f;
        for (uint32_t i = 0; i < count; ++i) {
            const uint32_t candidate = ids[i];
            const float d = angularDistanceDeg(solver[candidate].azimuthDeg, azimuthDeg);
            if (d < bestD) {
                bestD = d;
                best = candidate;
            }
        }
        return best;
    }

    uint32_t nextInSortedBand(const uint32_t* ids, uint32_t count, uint32_t id, int direction) const
    {
        if (count < 2u) return id;
        for (uint32_t i = 0; i < count; ++i) {
            if (ids[i] == id) {
                const int next = (static_cast<int>(i) + direction + static_cast<int>(count)) % static_cast<int>(count);
                return ids[static_cast<uint32_t>(next)];
            }
        }
        return ids[0];
    }

    LayoutPannerVbapTopology buildVbapTopology(
        const std::array<LayoutPannerSolverSpeaker, kLayoutPannerMaxSolverSpeakers>& solver,
        uint32_t solverCount) const
    {
        LayoutPannerVbapTopology topology {};
        if (solverCount < 2u) return topology;

        struct Band {
            float elevationDeg = 0.0f;
            std::array<uint32_t, kLayoutPannerMaxSolverSpeakers> ids {};
            uint32_t count = 0u;
        };
        std::array<Band, 12> bands {};
        uint32_t bandCount = 0u;
        for (uint32_t i = 0; i < solverCount; ++i) {
            uint32_t band = bandCount;
            for (uint32_t b = 0; b < bandCount; ++b) {
                if (std::fabs(bands[b].elevationDeg - solver[i].elevationDeg) <= 8.0f) {
                    band = b;
                    break;
                }
            }
            if (band == bandCount && bandCount < bands.size()) {
                bands[bandCount].elevationDeg = solver[i].elevationDeg;
                band = bandCount++;
            }
            if (band < bandCount && bands[band].count < bands[band].ids.size()) {
                bands[band].ids[bands[band].count++] = i;
            }
        }
        std::sort(bands.begin(), bands.begin() + bandCount, [](const Band& a, const Band& b) {
            return a.elevationDeg < b.elevationDeg;
        });
        for (uint32_t b = 0; b < bandCount; ++b) {
            std::sort(bands[b].ids.begin(), bands[b].ids.begin() + bands[b].count, [&](uint32_t a, uint32_t bIndex) {
                return positiveAzimuth(solver[a].azimuthDeg) < positiveAzimuth(solver[bIndex].azimuthDeg);
            });
            if (bands[b].count > 1u) {
                for (uint32_t i = 0; i < bands[b].count; ++i) {
                    addVbapPair(topology, bands[b].ids[i], bands[b].ids[(i + 1u) % bands[b].count]);
                }
            }
        }

        if (layoutIsPlanar2d()) return topology;

        for (uint32_t b = 0; b + 1u < bandCount; ++b) {
            const auto& lower = bands[b];
            const auto& upper = bands[b + 1u];
            if (lower.count == 0u || upper.count == 0u) continue;
            if (lower.count == 1u) {
                for (uint32_t ui = 0; ui < upper.count; ++ui) {
                    addVbapFacet(topology, lower.ids[0], upper.ids[ui], upper.ids[(ui + 1u) % upper.count]);
                }
                continue;
            }
            if (upper.count == 1u) {
                for (uint32_t li = 0; li < lower.count; ++li) {
                    addVbapFacet(topology, upper.ids[0], lower.ids[li], lower.ids[(li + 1u) % lower.count]);
                }
                continue;
            }
            for (uint32_t ui = 0; ui < upper.count; ++ui) {
                const uint32_t u = upper.ids[ui];
                const uint32_t l0 = nearestInBandByAzimuth(solver.data(), lower.ids.data(), lower.count, solver[u].azimuthDeg);
                const uint32_t lPrev = nextInSortedBand(lower.ids.data(), lower.count, l0, -1);
                const uint32_t lNext = nextInSortedBand(lower.ids.data(), lower.count, l0, 1);
                if (lPrev != l0) addVbapFacet(topology, u, lPrev, l0);
                if (lNext != l0) addVbapFacet(topology, u, l0, lNext);
            }
            for (uint32_t li = 0; li < lower.count; ++li) {
                const uint32_t l = lower.ids[li];
                const uint32_t u0 = nearestInBandByAzimuth(solver.data(), upper.ids.data(), upper.count, solver[l].azimuthDeg);
                const uint32_t uPrev = nextInSortedBand(upper.ids.data(), upper.count, u0, -1);
                const uint32_t uNext = nextInSortedBand(upper.ids.data(), upper.count, u0, 1);
                if (uPrev != u0) addVbapFacet(topology, l, uPrev, u0);
                if (uNext != u0) addVbapFacet(topology, l, u0, uNext);
            }
        }
        return topology;
    }

    static float det3(Vec3 a, Vec3 b, Vec3 c)
    {
        return a.x * (b.y * c.z - b.z * c.y)
             - b.x * (a.y * c.z - a.z * c.y)
             + c.x * (a.y * b.z - a.z * b.y);
    }

    bool solveVbap2d(Vec3 dir, const std::array<LayoutPannerSolverSpeaker, kLayoutPannerMaxSolverSpeakers>& solver,
        uint32_t solverCount, const LayoutPannerVbapTopology& topology, uint32_t& a, uint32_t& b, float& ga, float& gb) const
    {
        const float sourceAz = vecAzimuthDeg(dir);
        float bestScore = 999999.0f;
        bool found = false;
        for (uint32_t pairIndex = 0; pairIndex < topology.pairCount; ++pairIndex) {
            const uint32_t i = topology.pairs[pairIndex].a;
            const uint32_t j = topology.pairs[pairIndex].b;
            if (i >= solverCount || j >= solverCount) continue;
            const Vec3 vi = solverUnit(solver, i);
            const Vec3 vj = solverUnit(solver, j);
            const float det = vi.x * vj.y - vj.x * vi.y;
            if (std::fabs(det) < 0.0001f) continue;
            const float gi = (dir.x * vj.y - vj.x * dir.y) / det;
            const float gj = (vi.x * dir.y - dir.x * vi.y) / det;
            if (gi < -0.0001f || gj < -0.0001f) continue;
            const float mid = layoutPannerWrapDeg((solver[i].azimuthDeg + solver[j].azimuthDeg) * 0.5f);
            const float span = std::fabs(layoutPannerWrapDeg(solver[i].azimuthDeg - solver[j].azimuthDeg));
            const float score = std::fabs(layoutPannerWrapDeg(sourceAz - mid)) + span * 0.001f;
            if (score < bestScore) {
                bestScore = score;
                a = i;
                b = j;
                ga = std::max(0.0f, gi);
                gb = std::max(0.0f, gj);
                found = true;
            }
        }
        if (found) return true;
        float bestA = 999999.0f;
        float bestB = 999999.0f;
        a = 0u;
        b = solverCount > 1u ? 1u : 0u;
        for (uint32_t i = 0; i < solverCount; ++i) {
            const float d = std::fabs(layoutPannerWrapDeg(sourceAz - solver[i].azimuthDeg));
            if (d < bestA) {
                bestB = bestA;
                b = a;
                bestA = d;
                a = i;
            } else if (d < bestB) {
                bestB = d;
                b = i;
            }
        }
        ga = 1.0f;
        gb = 0.0f;
        return solverCount > 0u;
    }

    bool solveVbap3d(Vec3 dir, const std::array<LayoutPannerSolverSpeaker, kLayoutPannerMaxSolverSpeakers>& solver,
        uint32_t solverCount, const LayoutPannerVbapTopology& topology,
        uint32_t& a, uint32_t& b, uint32_t& c, float& ga, float& gb, float& gc) const
    {
        float bestScore = 999999.0f;
        bool found = false;
        for (uint32_t facetIndex = 0; facetIndex < topology.facetCount; ++facetIndex) {
            const uint32_t i = topology.facets[facetIndex].a;
            const uint32_t j = topology.facets[facetIndex].b;
            const uint32_t k = topology.facets[facetIndex].c;
            if (i >= solverCount || j >= solverCount || k >= solverCount) continue;
            const Vec3 vi = solverUnit(solver, i);
            const Vec3 vj = solverUnit(solver, j);
            const Vec3 vk = solverUnit(solver, k);
            const float det = det3(vi, vj, vk);
            if (std::fabs(det) < 0.0001f) continue;
            const float gi = det3(dir, vj, vk) / det;
            const float gj = det3(vi, dir, vk) / det;
            const float gk = det3(vi, vj, dir) / det;
            if (gi < -0.0001f || gj < -0.0001f || gk < -0.0001f) continue;
            const float balance = std::max({ gi, gj, gk }) - std::min({ gi, gj, gk });
            const float score = balance + std::fabs(gi + gj + gk - 1.0f) * 0.01f;
            if (score < bestScore) {
                bestScore = score;
                a = i;
                b = j;
                c = k;
                ga = std::max(0.0f, gi);
                gb = std::max(0.0f, gj);
                gc = std::max(0.0f, gk);
                found = true;
            }
        }
        return found;
    }

    void addSolverGainToRealTarget(uint32_t sourceIndex,
        const std::array<LayoutPannerSolverSpeaker, kLayoutPannerMaxSolverSpeakers>& solver,
        uint32_t solverIndex, float gain)
    {
        if (gain <= 0.0f) return;
        const uint32_t base = sourceIndex * kLayoutPannerMaxSpeakers;
        const auto& point = solver[solverIndex];
        if (point.real) {
            targetGains_[base + point.realIndex] += gain;
            return;
        }
        if (!point.foldToReal) return;

        uint32_t best = 0u;
        uint32_t second = activeSpeakers_ > 1u ? 1u : 0u;
        float bestD = 999999.0f;
        float secondD = 999999.0f;
        for (uint32_t spk = 0; spk < activeSpeakers_; ++spk) {
            const float d = distanceSquared(speakerUnit(spk), point.dir);
            if (d < bestD) {
                secondD = bestD;
                second = best;
                bestD = d;
                best = spk;
            } else if (d < secondD) {
                secondD = d;
                second = spk;
            }
        }
        const float wBest = 1.0f / std::max(0.0001f, bestD);
        const float wSecond = second != best ? 1.0f / std::max(0.0001f, secondD) : 0.0f;
        const float inv = 1.0f / std::max(0.0001f, wBest + wSecond);
        targetGains_[base + best] += gain * wBest * inv;
        if (second != best) targetGains_[base + second] += gain * wSecond * inv;
    }

    void addSolverGainToRealBuffer(float* gains,
        const std::array<LayoutPannerSolverSpeaker, kLayoutPannerMaxSolverSpeakers>& solver,
        uint32_t solverIndex, float gain) const
    {
        if (!gains || gain <= 0.0f) return;
        const auto& point = solver[solverIndex];
        if (point.real) {
            gains[point.realIndex] += gain;
            return;
        }
        if (!point.foldToReal) return;

        uint32_t best = 0u;
        uint32_t second = activeSpeakers_ > 1u ? 1u : 0u;
        float bestD = 999999.0f;
        float secondD = 999999.0f;
        for (uint32_t spk = 0; spk < activeSpeakers_; ++spk) {
            const float d = distanceSquared(speakerUnit(spk), point.dir);
            if (d < bestD) {
                secondD = bestD;
                second = best;
                bestD = d;
                best = spk;
            } else if (d < secondD) {
                secondD = d;
                second = spk;
            }
        }
        const float wBest = 1.0f / std::max(0.0001f, bestD);
        const float wSecond = second != best ? 1.0f / std::max(0.0001f, secondD) : 0.0f;
        const float inv = 1.0f / std::max(0.0001f, wBest + wSecond);
        gains[best] += gain * wBest * inv;
        if (second != best) gains[second] += gain * wSecond * inv;
    }

    void normalizeGainBuffer(float* gains) const
    {
        if (!gains) return;
        float sumSquares = 0.0f;
        for (uint32_t spk = 0; spk < activeSpeakers_; ++spk) {
            sumSquares += gains[spk] * gains[spk];
        }
        const float norm = sumSquares > 0.000001f ? std::sqrt(sumSquares) : 1.0f;
        const float inv = norm > 0.000001f ? 1.0f / norm : 1.0f;
        for (uint32_t spk = 0; spk < activeSpeakers_; ++spk) gains[spk] *= inv;
    }

    void applyInsideTargetMode(float* gains, float sourceDistance) const
    {
        if (!gains || sourceDistance >= 1.0f || params_.insideMode == LayoutPannerInsideMode::Hold
            || params_.insideMode == LayoutPannerInsideMode::Attenuate || activeSpeakers_ == 0u) {
            return;
        }

        const float inside = clamp(1.0f - sourceDistance, 0.0f, 1.0f);
        const float blend = params_.insideMode == LayoutPannerInsideMode::CenterBlend
            ? inside
            : inside * params_.distanceDiffusion;
        if (blend <= 0.000001f) return;

        const float uniform = 1.0f / std::sqrt(static_cast<float>(activeSpeakers_));
        for (uint32_t spk = 0; spk < activeSpeakers_; ++spk) {
            gains[spk] = gains[spk] * (1.0f - blend) + uniform * blend;
        }
        normalizeGainBuffer(gains);
    }

    float vbapPoleBlend(Vec3 dir, bool& upperPole) const
    {
        const float elevation = vecElevationDeg(dir);
        upperPole = elevation >= 0.0f;
        const float absElevation = std::fabs(elevation);
        if (absElevation <= 75.0f) return 0.0f;
        const float t = clamp((absElevation - 75.0f) / 15.0f, 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

    float planarElevationAttenuation(Vec3 dir) const
    {
        if (!layoutIsPlanar2d()) return 1.0f;
        const float elevationRad = std::fabs(vecElevationDeg(dir)) * kPi / 180.0f;
        return clamp(std::cos(elevationRad), 0.0f, 1.0f);
    }

    float missingLowerHemisphereAttenuation(Vec3 dir) const
    {
        if (!missingLowerHemisphereSupport()) return 1.0f;
        const float elevation = vecElevationDeg(dir);
        if (elevation >= 0.0f) return 1.0f;
        const float t = clamp(-elevation / 90.0f, 0.0f, 1.0f);
        return std::cos(t * (kPi * 0.5f));
    }

    void writeVbapCapGains(float* gains,
        const std::array<LayoutPannerSolverSpeaker, kLayoutPannerMaxSolverSpeakers>& solver,
        uint32_t solverCount, bool upperPole) const
    {
        if (!gains || solverCount == 0u) return;
        for (uint32_t spk = 0; spk < activeSpeakers_; ++spk) gains[spk] = 0.0f;

        const float targetElevation = upperPole ? 90.0f : -90.0f;
        float bestDiff = 999999.0f;
        for (uint32_t i = 0; i < solverCount; ++i) {
            const float diff = std::fabs(solver[i].elevationDeg - targetElevation);
            bestDiff = std::min(bestDiff, diff);
        }
        if (bestDiff > 999.0f) return;

        uint32_t capCount = 0u;
        for (uint32_t i = 0; i < solverCount; ++i) {
            if (std::fabs(std::fabs(solver[i].elevationDeg - targetElevation) - bestDiff) <= 2.0f) {
                ++capCount;
            }
        }
        if (capCount == 0u) return;

        const float each = 1.0f / std::sqrt(static_cast<float>(capCount));
        for (uint32_t i = 0; i < solverCount; ++i) {
            if (std::fabs(std::fabs(solver[i].elevationDeg - targetElevation) - bestDiff) <= 2.0f) {
                addSolverGainToRealBuffer(gains, solver, i, each);
            }
        }
        normalizeGainBuffer(gains);
    }

    void applyPoleCapBlend(float* gains, Vec3 dir,
        const std::array<LayoutPannerSolverSpeaker, kLayoutPannerMaxSolverSpeakers>& solver,
        uint32_t solverCount) const
    {
        if (!gains) return;
        bool upperPole = true;
        const float poleBlend = vbapPoleBlend(dir, upperPole);
        if (poleBlend <= 0.0f) return;

        std::array<float, kLayoutPannerMaxSpeakers> capGains {};
        writeVbapCapGains(capGains.data(), solver, solverCount, upperPole);
        float capEnergy = 0.0f;
        for (uint32_t spk = 0; spk < activeSpeakers_; ++spk) capEnergy += capGains[spk] * capGains[spk];
        const float invNormal = 1.0f - poleBlend;
        for (uint32_t spk = 0; spk < activeSpeakers_; ++spk) {
            gains[spk] = gains[spk] * invNormal + capGains[spk] * poleBlend;
        }
        if (capEnergy > 0.000001f) normalizeGainBuffer(gains);
    }

    void writeVbapGains(uint32_t sourceIndex, Vec3 sourcePosition,
        const std::array<LayoutPannerSolverSpeaker, kLayoutPannerMaxSolverSpeakers>& solver, uint32_t solverCount,
        const LayoutPannerVbapTopology& topology)
    {
        for (uint32_t spk = 0; spk < activeSpeakers_; ++spk) {
            targetGains_[sourceIndex * kLayoutPannerMaxSpeakers + spk] = 0.0f;
        }
        const Vec3 dir = normalize(sourcePosition);
        uint32_t a = 0u;
        uint32_t b = 0u;
        uint32_t c = 0u;
        float ga = 0.0f;
        float gb = 0.0f;
        float gc = 0.0f;
        if (layoutIsPlanar2d()) {
            if (solveVbap2d(dir, solver, solverCount, topology, a, b, ga, gb)) {
                addSolverGainToRealTarget(sourceIndex, solver, a, ga);
                addSolverGainToRealTarget(sourceIndex, solver, b, gb);
            }
        } else if (solveVbap3d(dir, solver, solverCount, topology, a, b, c, ga, gb, gc)) {
            addSolverGainToRealTarget(sourceIndex, solver, a, ga);
            addSolverGainToRealTarget(sourceIndex, solver, b, gb);
            addSolverGainToRealTarget(sourceIndex, solver, c, gc);
        } else {
            float best = 999999.0f;
            uint32_t bestIndex = 0u;
            for (uint32_t spk = 0; spk < activeSpeakers_; ++spk) {
                const float d = distanceSquared(speakerUnit(spk), dir);
                if (d < best) {
                    best = d;
                    bestIndex = spk;
                }
            }
            targetGains_[sourceIndex * kLayoutPannerMaxSpeakers + bestIndex] = 1.0f;
        }

        float sumSquares = 0.0f;
        for (uint32_t spk = 0; spk < activeSpeakers_; ++spk) {
            const float g = targetGains_[sourceIndex * kLayoutPannerMaxSpeakers + spk];
            sumSquares += g * g;
        }
        const float norm = sumSquares > 0.000001f ? std::sqrt(sumSquares) : 1.0f;
        const float inv = norm > 0.000001f ? 1.0f / norm : 1.0f;
        for (uint32_t spk = 0; spk < activeSpeakers_; ++spk) {
            targetGains_[sourceIndex * kLayoutPannerMaxSpeakers + spk] *= inv;
        }

        float* sourceGains = targetGains_.data() + sourceIndex * kLayoutPannerMaxSpeakers;
        applyPoleCapBlend(sourceGains, dir, solver, solverCount);
        const float lowerAmp = missingLowerHemisphereAttenuation(dir);
        if (lowerAmp < 0.999999f) {
            for (uint32_t spk = 0; spk < activeSpeakers_; ++spk) sourceGains[spk] *= lowerAmp;
        }
        applyInsideTargetMode(sourceGains,
            std::sqrt(sourcePosition.x * sourcePosition.x + sourcePosition.y * sourcePosition.y + sourcePosition.z * sourcePosition.z));
        const float elevationAmp = planarElevationAttenuation(dir);
        if (elevationAmp < 0.999999f) {
            for (uint32_t spk = 0; spk < activeSpeakers_; ++spk) {
                targetGains_[sourceIndex * kLayoutPannerMaxSpeakers + spk] *= elevationAmp;
            }
        }
    }

    void updateCachedCoefficients()
    {
        cachedGainCoef_ = smoothingCoeff(params_.smoothingMs);
        cachedOutputGain_ = dbToGain(params_.outputGainDb);
    }

    void updateGainTargets(uint32_t activeInputs, uint32_t activeSourceCount, bool anySolo, uint32_t sampleStep)
    {
        const float distCoef = std::pow(cachedGainCoef_, static_cast<float>(std::max<uint32_t>(1u, sampleStep)));
        std::array<LayoutPannerSolverSpeaker, kLayoutPannerMaxSolverSpeakers> solver {};
        const uint32_t solverCount = (params_.method == LayoutPannerMethod::Lbap || params_.method == LayoutPannerMethod::Vbap)
            ? buildSolverSpeakers(solver)
            : 0u;
        const LayoutPannerVbapTopology vbapTopology = params_.method == LayoutPannerMethod::Vbap
            ? buildVbapTopology(solver, solverCount)
            : LayoutPannerVbapTopology {};
        for (uint32_t src = 0; src < activeSourceCount; ++src) {
            const auto& source = sources_[src];
            const bool active = src < activeInputs && !source.muted && (!anySolo || source.solo);
            targetActive_[src] = active;
            if (!active) {
                sourceAmps_[src] = 0.0f;
                for (uint32_t spk = 0; spk < activeSpeakers_; ++spk) targetGains_[src * kLayoutPannerMaxSpeakers + spk] = 0.0f;
                continue;
            }

            const float targetDistance = std::max(0.1f, source.distance + params_.globalDistanceOffset);
            smoothedDistance_[src] = targetDistance + (smoothedDistance_[src] - targetDistance) * distCoef;
            const float sourceDistance = smoothedDistance_[src];
            const float sourceAz = layoutPannerWrapDeg(source.azimuthDeg + params_.globalAzimuthDeg);
            const float sourceEl = clamp(source.elevationDeg + params_.globalElevationDeg, -90.0f, 90.0f);
            const Vec3 dir = directionFromAed(sourceAz, sourceEl);
            const Vec3 pos { dir.x * sourceDistance, dir.y * sourceDistance, dir.z * sourceDistance };

            if (params_.method == LayoutPannerMethod::Vbap) {
                writeVbapGains(src, pos, solver, solverCount, vbapTopology);
            } else {
                float sumSquares = 0.0f;
                for (uint32_t spk = 0; spk < activeSpeakers_; ++spk) {
                    const float gain = computeGain(spk, pos, sourceDistance, solver.data(), solverCount);
                    targetGains_[src * kLayoutPannerMaxSpeakers + spk] = gain;
                    sumSquares += gain * gain;
                }
                const float norm = sumSquares > 0.000001f ? std::sqrt(sumSquares) : 1.0f;
                for (uint32_t spk = 0; spk < activeSpeakers_; ++spk) {
                    const uint32_t gi = src * kLayoutPannerMaxSpeakers + spk;
                    targetGains_[gi] /= norm;
                }
                float* sourceGains = targetGains_.data() + src * kLayoutPannerMaxSpeakers;
                if (params_.method == LayoutPannerMethod::Lbap) {
                    applyPoleCapBlend(sourceGains, dir, solver, solverCount);
                }
                const float lowerAmp = missingLowerHemisphereAttenuation(dir);
                if (lowerAmp < 0.999999f) {
                    for (uint32_t spk = 0; spk < activeSpeakers_; ++spk) sourceGains[spk] *= lowerAmp;
                }
                applyInsideTargetMode(sourceGains, sourceDistance);
                const float elevationAmp = params_.method == LayoutPannerMethod::Lbap ? planarElevationAttenuation(dir) : 1.0f;
                if (elevationAmp < 0.999999f) {
                    for (uint32_t spk = 0; spk < activeSpeakers_; ++spk) {
                        const uint32_t gi = src * kLayoutPannerMaxSpeakers + spk;
                        targetGains_[gi] *= elevationAmp;
                    }
                }
            }
            sourceAmps_[src] = dbToGain(source.gainDb) * distanceAmp(sourceDistance);
        }
    }

    void markTargetsDirty()
    {
        targetUpdateCountdown_ = 0;
    }

    void smoothSourceToSilence(uint32_t sourceIndex, float gainCoef)
    {
        for (uint32_t spk = 0; spk < activeSpeakers_; ++spk) {
            const uint32_t gi = sourceIndex * kLayoutPannerMaxSpeakers + spk;
            smoothedGains_[gi] *= gainCoef;
        }
    }

    void clearGains()
    {
        smoothedGains_.fill(0.0f);
        targetGains_.fill(0.0f);
        sourceAmps_.fill(0.0f);
        targetActive_.fill(false);
        markTargetsDirty();
    }

    double sampleRate_ = 48000.0;
    uint32_t activeSpeakers_ = 24;
    LayoutPannerParams params_ {};
    std::array<LayoutPannerSpeaker, kLayoutPannerMaxSpeakers> speakers_ {};
    std::array<LayoutPannerSource, kLayoutPannerSources> sources_ {};
    std::array<float, kLayoutPannerSources> smoothedDistance_ {};
    std::array<float, kLayoutPannerSources * kLayoutPannerMaxSpeakers> smoothedGains_ {};
    std::array<float, kLayoutPannerSources * kLayoutPannerMaxSpeakers> targetGains_ {};
    std::array<float, kLayoutPannerSources> sourceAmps_ {};
    std::array<bool, kLayoutPannerSources> targetActive_ {};
    uint32_t targetUpdateCountdown_ = 0;
    float cachedGainCoef_ = 0.0f;
    float cachedOutputGain_ = 1.0f;
    uint32_t vectorActiveSourceCount_ = 1;
    uint32_t vectorActiveInputs_ = 1;
    uint32_t vectorOutputChannels_ = kLayoutPannerMaxSpeakers;
    uint32_t vectorActiveOutputs_ = 1;
};

} // namespace s3g
