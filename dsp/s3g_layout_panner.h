#pragma once

#include "s3g_3oafx.h"
#include "s3g_math.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace s3g {

constexpr uint32_t kLayoutPannerSources = 16;
constexpr uint32_t kLayoutPannerMaxSpeakers = 64;

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
};

enum class LayoutPannerMethod : uint32_t {
    Distance = 0,
    Cosine = 1,
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
    default: return "DOME 24";
    }
}

inline const char* layoutPannerMethodName(LayoutPannerMethod method)
{
    return method == LayoutPannerMethod::Cosine ? "COS" : "DIST";
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

class LayoutPanner {
public:
    void prepare(double sampleRate)
    {
        sampleRate_ = std::max(1.0, sampleRate);
        resetSources();
        applyLayout(params_.layout);
        clearGains();
    }

    void resetSources()
    {
        static constexpr float kDefaultAz[kLayoutPannerSources] {
            -30.0f, -52.5f, -75.0f, -97.5f,
            -120.0f, -142.5f, -165.0f, 172.5f,
            150.0f, 127.5f, 105.0f, 82.5f,
            60.0f, 37.5f, 15.0f, -7.5f
        };
        for (uint32_t i = 0; i < kLayoutPannerSources; ++i) {
            sources_[i].azimuthDeg = kDefaultAz[i];
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
        params.selectedSource = std::min<uint32_t>(params.selectedSource, kLayoutPannerSources - 1u);
        params.activeSpeakers = std::clamp<uint32_t>(params.activeSpeakers, 2u, kLayoutPannerMaxSpeakers);
        params.selectedSpeaker = std::min<uint32_t>(params.selectedSpeaker, params.activeSpeakers - 1u);
        params.customShape = static_cast<LayoutPannerCustomShape>(
            std::clamp<uint32_t>(static_cast<uint32_t>(params.customShape), 0u, 9u));
        params.focus = clamp(params.focus, 0.25f, 4.0f);
        params.distanceRolloffDb = clamp(params.distanceRolloffDb, 0.0f, 48.0f);
        params.smoothingMs = clamp(params.smoothingMs, 1.0f, 250.0f);
        params.globalAzimuthDeg = layoutPannerWrapDeg(params.globalAzimuthDeg);
        params.globalElevationDeg = clamp(params.globalElevationDeg, -90.0f, 90.0f);
        params.globalDistanceOffset = clamp(params.globalDistanceOffset, -3.0f, 3.0f);
        params.distanceDiffusion = clamp(params.distanceDiffusion, 0.0f, 1.0f);
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
        params_.selectedSpeaker = std::min<uint32_t>(params_.selectedSpeaker, std::max<uint32_t>(1u, activeSpeakers_) - 1u);
    }

    LayoutPannerParams params() const { return params_; }
    uint32_t activeSpeakers() const { return activeSpeakers_; }
    const std::array<LayoutPannerSpeaker, kLayoutPannerMaxSpeakers>& speakers() const { return speakers_; }
    const std::array<LayoutPannerSource, kLayoutPannerSources>& sources() const { return sources_; }

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
    }

    void setSourceGain(uint32_t index, float gainDb)
    {
        if (index >= kLayoutPannerSources) return;
        sources_[index].gainDb = clamp(gainDb, -60.0f, 24.0f);
        params_.selectedSource = index;
    }

    void setSourceMute(uint32_t index, bool muted)
    {
        if (index >= kLayoutPannerSources) return;
        sources_[index].muted = muted;
        params_.selectedSource = index;
    }

    void setSourceSolo(uint32_t index, bool solo)
    {
        if (index >= kLayoutPannerSources) return;
        sources_[index].solo = solo;
        params_.selectedSource = index;
    }

    void processFrame(const float* in, float* out, uint32_t inputChannels)
    {
        if (!out) return;
        for (uint32_t spk = 0; spk < kLayoutPannerMaxSpeakers; ++spk) out[spk] = 0.0f;
        const uint32_t activeInputs = std::min<uint32_t>(inputChannels, kLayoutPannerSources);
        bool anySolo = false;
        for (uint32_t src = 0; src < kLayoutPannerSources; ++src) anySolo = anySolo || sources_[src].solo;

        const float distCoef = smoothingCoeff(params_.smoothingMs);
        const float gainCoef = distCoef;
        for (uint32_t src = 0; src < activeInputs; ++src) {
            const auto& source = sources_[src];
            const bool active = !source.muted && (!anySolo || source.solo);
            const float input = in ? in[src] : 0.0f;
            if (!active || std::abs(input) < 0.0000001f) {
                smoothSourceToSilence(src, gainCoef);
                continue;
            }
            const float targetDistance = std::max(0.1f, source.distance + params_.globalDistanceOffset);
            smoothedDistance_[src] = targetDistance + (smoothedDistance_[src] - targetDistance) * distCoef;
            const float sourceDistance = smoothedDistance_[src];
            const float sourceAz = layoutPannerWrapDeg(source.azimuthDeg + params_.globalAzimuthDeg);
            const float sourceEl = clamp(source.elevationDeg + params_.globalElevationDeg, -90.0f, 90.0f);
            const Vec3 dir = directionFromAed(sourceAz, sourceEl);
            const Vec3 pos { dir.x * sourceDistance, dir.y * sourceDistance, dir.z * sourceDistance };

            float sumSquares = 0.0f;
            for (uint32_t spk = 0; spk < activeSpeakers_; ++spk) {
                targetGains_[spk] = computeGain(spk, pos, sourceDistance);
                sumSquares += targetGains_[spk] * targetGains_[spk];
            }
            const float norm = sumSquares > 0.000001f ? std::sqrt(sumSquares) : 1.0f;
            const float weighted = input * dbToGain(source.gainDb) * distanceAmp(sourceDistance);
            for (uint32_t spk = 0; spk < activeSpeakers_; ++spk) {
                const uint32_t gi = src * kLayoutPannerMaxSpeakers + spk;
                const float target = targetGains_[spk] / norm;
                smoothedGains_[gi] = target + (smoothedGains_[gi] - target) * gainCoef;
                out[spk] += weighted * smoothedGains_[gi];
            }
        }
        const float outputGain = dbToGain(params_.outputGainDb);
        for (uint32_t spk = 0; spk < activeSpeakers_; ++spk) {
            out[spk] = clamp(out[spk] * outputGain, -4.0f, 4.0f);
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

    void setRing(uint32_t base, uint32_t count, float startAzimuthDeg, float elevationDeg)
    {
        const float step = 360.0f / static_cast<float>(count);
        for (uint32_t i = 0; i < count; ++i) {
            setSpeaker(base + i, startAzimuthDeg - step * static_cast<float>(i), elevationDeg);
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
        setRing(0, activeSpeakers_, -30.0f, 0.0f);
    }

    void generateStackLayout(uint32_t count)
    {
        activeSpeakers_ = std::clamp<uint32_t>(count, 2u, kLayoutPannerMaxSpeakers);
        const uint32_t lower = (activeSpeakers_ + 1u) / 2u;
        const uint32_t upper = activeSpeakers_ - lower;
        setRing(0, lower, -30.0f, 0.0f);
        if (upper > 0u) setRing(lower, upper, -30.0f, 45.0f);
    }

    void generateDomeLayout(uint32_t count)
    {
        activeSpeakers_ = std::clamp<uint32_t>(count, 2u, kLayoutPannerMaxSpeakers);
        const uint32_t lower = activeSpeakers_ <= 8u ? activeSpeakers_ : std::max<uint32_t>(4u, activeSpeakers_ / 2u);
        const uint32_t mid = activeSpeakers_ <= 8u ? 0u : (activeSpeakers_ - lower) * 2u / 3u;
        const uint32_t top = activeSpeakers_ - lower - mid;
        setRing(0, lower, -30.0f, 0.0f);
        if (mid > 0u) setRing(lower, mid, -45.0f, 36.0f);
        if (top == 1u) setSpeaker(lower + mid, 0.0f, 90.0f);
        else if (top > 1u) setRing(lower + mid, top, -90.0f, 68.0f);
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
        case LayoutPannerPreset::Dodeca12:
            activeSpeakers_ = 12;
            setSpeaker(0, -31.717474f, 0.0f);
            setSpeaker(1, 31.717474f, 0.0f);
            setSpeaker(2, -148.282526f, 0.0f);
            setSpeaker(3, 148.282526f, 0.0f);
            setSpeaker(4, 180.0f, 58.282526f);
            setSpeaker(5, 0.0f, 58.282526f);
            setSpeaker(6, 180.0f, -58.282526f);
            setSpeaker(7, 0.0f, -58.282526f);
            setSpeaker(8, 90.0f, -31.717474f);
            setSpeaker(9, 90.0f, 31.717474f);
            setSpeaker(10, -90.0f, -31.717474f);
            setSpeaker(11, -90.0f, 31.717474f);
            break;
        case LayoutPannerPreset::Dome24NoOverhead:
            activeSpeakers_ = 24;
            setRing(0, 12, -30.0f, 0.0f);
            setRing(12, 8, -45.0f, 32.0f);
            setRing(20, 4, -90.0f, 66.6f);
            break;
        case LayoutPannerPreset::Dome25:
            activeSpeakers_ = 25;
            setRing(0, 12, -30.0f, 0.0f);
            setRing(12, 8, -45.0f, 32.0f);
            setRing(20, 4, -90.0f, 66.6f);
            setSpeaker(24, 0.0f, 90.0f);
            break;
        case LayoutPannerPreset::DoubleRing16:
            activeSpeakers_ = 16;
            setRing(0, 8, -30.0f, 0.0f);
            setRing(8, 8, -30.0f, 45.0f);
            break;
        case LayoutPannerPreset::DoubleRing20:
            activeSpeakers_ = 20;
            setRing(0, 12, -30.0f, 0.0f);
            setRing(12, 8, -45.0f, 45.0f);
            break;
        case LayoutPannerPreset::OctophonicRing:
            activeSpeakers_ = 8;
            setRing(0, 8, -30.0f, 0.0f);
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
            setRing(0, 12, -30.0f, 0.0f);
            break;
        case LayoutPannerPreset::Ring16:
        default:
            activeSpeakers_ = 16;
            setRing(0, 16, -30.0f, 0.0f);
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
        if (distance <= 1.0f) return 1.0f;
        const float exponent = params_.distanceRolloffDb / 6.0f;
        return 1.0f / std::pow(distance, std::max(0.0f, exponent));
    }

    float distanceDiffusionFactor(float distance) const
    {
        return 1.0f + std::max(0.0f, distance - 1.0f) * params_.distanceDiffusion;
    }

    float computeGain(uint32_t speakerIndex, Vec3 sourcePosition, float sourceDistance) const
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
        const float dist = std::sqrt(dx * dx + dy * dy + dz * dz) + 0.04f;
        const float focus = std::max(0.25f, params_.focus / distanceDiffusionFactor(sourceDistance));
        return 1.0f / std::pow(dist, focus);
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
    }

    double sampleRate_ = 48000.0;
    uint32_t activeSpeakers_ = 24;
    LayoutPannerParams params_ {};
    std::array<LayoutPannerSpeaker, kLayoutPannerMaxSpeakers> speakers_ {};
    std::array<LayoutPannerSource, kLayoutPannerSources> sources_ {};
    std::array<float, kLayoutPannerSources> smoothedDistance_ {};
    std::array<float, kLayoutPannerSources * kLayoutPannerMaxSpeakers> smoothedGains_ {};
    std::array<float, kLayoutPannerMaxSpeakers> targetGains_ {};
};

} // namespace s3g
