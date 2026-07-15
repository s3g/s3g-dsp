#pragma once

#include "s3g_24ch_layout.h"
#include "s3g_3oafx.h"
#include "s3g_math.h"
#include "s3g_realtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace s3g {

constexpr uint32_t kAmbiSpeakerDecoderMaxSpeakers = 64;
constexpr uint32_t kAmbiSpeakerDecoderMaxOrder = 7;
constexpr uint32_t kAmbiSpeakerDecoderMaxChannels = 64;

enum class AmbiSpeakerDecoderMode : uint32_t {
    Basic = 0,
    Epad = 1,
    Mmd = 2,
};

enum class AmbiSpeakerLayoutPreset : uint32_t {
    Custom = 0,
    Quad = 1,
    Cube8 = 2,
    Cube17 = 3,
    Dome24 = 4,
    Dome25 = 5,
    QuadOverhead6 = 6,
    Sphere24 = 7,
};

enum class AmbiSpeakerDecoderWeighting : uint32_t {
    None = 0,
    MaxRe = 1,
    InPhase = 2,
};

enum class AmbiSpeakerCustomField : uint32_t {
    FullSphere = 0,
    Hemisphere = 1,
};

struct AmbiSpeaker {
    float azimuthDeg = 0.0f;
    float elevationDeg = 0.0f;
    float distance = 1.0f;
    float gain = 1.0f;
    bool enabled = true;
    bool solo = false;
};

struct AmbiSpeakerDecoderParams {
    uint32_t activeSpeakers = 24;
    uint32_t selectedSpeaker = 0;
    uint32_t order = 3;
    AmbiSpeakerDecoderMode mode = AmbiSpeakerDecoderMode::Epad;
    AmbiSpeakerLayoutPreset layout = AmbiSpeakerLayoutPreset::Sphere24;
    AmbiSpeakerDecoderWeighting weighting = AmbiSpeakerDecoderWeighting::MaxRe;
    AmbiSpeakerCustomField customField = AmbiSpeakerCustomField::FullSphere;
    float regularization = 0.018f;
    float width = 1.0f;
    float energy = 1.0f;
    float outputGainDb = -6.0f;
    float selectedAzimuthDeg = 0.0f;
    float selectedElevationDeg = 0.0f;
    float selectedDistance = 1.0f;
    float selectedGain = 1.0f;
    bool selectedEnabled = true;
};

inline uint32_t ambiChannelsForOrder(uint32_t order)
{
    order = std::clamp<uint32_t>(order, 1u, kAmbiSpeakerDecoderMaxOrder);
    return (order + 1u) * (order + 1u);
}

inline float factorialRatio(uint32_t lo, uint32_t hi)
{
    float value = 1.0f;
    for (uint32_t i = lo + 1u; i <= hi; ++i) {
        value /= static_cast<float>(i);
    }
    return value;
}

inline std::array<float, kAmbiSpeakerDecoderMaxChannels> acnSn3dBasis7(Vec3 p)
{
    p = normalize(p);
    std::array<float, kAmbiSpeakerDecoderMaxChannels> out {};
    const auto basis3 = acnSn3dBasis(p);
    for (uint32_t i = 0; i < k3OaChannels; ++i) {
        out[i] = basis3[i];
    }

    const float az = std::atan2(p.y, p.x);
    const float z = clamp(p.z, -1.0f, 1.0f);
    const float rxy = std::sqrt(std::max(0.0f, 1.0f - z * z));
    float legendre[kAmbiSpeakerDecoderMaxOrder + 1u][kAmbiSpeakerDecoderMaxOrder + 1u] {};
    legendre[0][0] = 1.0f;
    for (uint32_t m = 1; m <= kAmbiSpeakerDecoderMaxOrder; ++m) {
        legendre[m][m] = static_cast<float>(2u * m - 1u) * rxy * legendre[m - 1u][m - 1u];
    }
    for (uint32_t m = 0; m < kAmbiSpeakerDecoderMaxOrder; ++m) {
        legendre[m + 1u][m] = static_cast<float>(2u * m + 1u) * z * legendre[m][m];
    }
    for (uint32_t m = 0; m <= kAmbiSpeakerDecoderMaxOrder; ++m) {
        for (uint32_t n = m + 2u; n <= kAmbiSpeakerDecoderMaxOrder; ++n) {
            legendre[n][m] = (static_cast<float>(2u * n - 1u) * z * legendre[n - 1u][m]
                - static_cast<float>(n + m - 1u) * legendre[n - 2u][m]) / static_cast<float>(n - m);
        }
    }

    for (uint32_t n = 4; n <= kAmbiSpeakerDecoderMaxOrder; ++n) {
        const uint32_t base = n * n;
        for (int m = -static_cast<int>(n); m <= static_cast<int>(n); ++m) {
            const uint32_t absM = static_cast<uint32_t>(std::abs(m));
            const float sn3d = std::sqrt((absM == 0u ? 1.0f : 2.0f) * factorialRatio(n - absM, n + absM));
            const float angle = static_cast<float>(absM) * az;
            const float trig = m < 0 ? std::sin(angle) : (m > 0 ? std::cos(angle) : 1.0f);
            out[base + static_cast<uint32_t>(m + static_cast<int>(n))] = sn3d * legendre[n][absM] * trig;
        }
    }
    return out;
}

class AmbiSpeakerDecoder {
public:
    void prepare(double sampleRate)
    {
        sampleRate_ = std::max(1.0, sampleRate);
        applyLayout(AmbiSpeakerLayoutPreset::Sphere24);
        setParams(params_);
    }

    void setParams(AmbiSpeakerDecoderParams params)
    {
        params.activeSpeakers = std::clamp<uint32_t>(params.activeSpeakers, 2u, kAmbiSpeakerDecoderMaxSpeakers);
        params.selectedSpeaker = std::min<uint32_t>(params.selectedSpeaker, params.activeSpeakers - 1u);
        params.order = std::clamp<uint32_t>(params.order, 1u, kAmbiSpeakerDecoderMaxOrder);
        params.weighting = static_cast<AmbiSpeakerDecoderWeighting>(
            std::clamp<uint32_t>(static_cast<uint32_t>(params.weighting), 0u, 2u));
        params.customField = static_cast<AmbiSpeakerCustomField>(
            std::clamp<uint32_t>(static_cast<uint32_t>(params.customField), 0u, 1u));
        params.regularization = clamp(params.regularization, 0.0f, 0.20f);
        params.width = clamp(params.width, 0.0f, 1.50f);
        params.energy = clamp(params.energy, 0.0f, 1.50f);
        params.outputGainDb = clamp(params.outputGainDb, -60.0f, 12.0f);
        params.selectedAzimuthDeg = wrapSignedDeg(params.selectedAzimuthDeg);
        params.selectedElevationDeg = clamp(params.selectedElevationDeg, -90.0f, 90.0f);
        params.selectedDistance = clamp(params.selectedDistance, 0.15f, 2.0f);
        params.selectedGain = clamp(params.selectedGain, 0.0f, 2.0f);
        const bool layoutChanged = params.layout != params_.layout;
        const bool selectedChanged = params.selectedSpeaker != params_.selectedSpeaker;
        const bool activeChanged = params.activeSpeakers != params_.activeSpeakers;
        const bool customFieldChanged = params.customField != params_.customField;
        params_ = params;
        if (params_.layout != AmbiSpeakerLayoutPreset::Custom && (layoutChanged || activeChanged)) {
            applyLayout(params_.layout);
            params_.layout = params.layout;
        } else if (params_.layout == AmbiSpeakerLayoutPreset::Custom && (layoutChanged || activeChanged || customFieldChanged)) {
            generateCustomLayout(params_.customField, params_.activeSpeakers);
        } else if (selectedChanged || params_.layout != AmbiSpeakerLayoutPreset::Custom) {
            syncSelectedFromSpeaker();
        } else {
            auto& speaker = speakers_[params_.selectedSpeaker];
            speaker.azimuthDeg = params_.selectedAzimuthDeg;
            speaker.elevationDeg = params_.selectedElevationDeg;
            speaker.distance = params_.selectedDistance;
            speaker.gain = params_.selectedGain;
        }
        params_.activeSpeakers = std::min<uint32_t>(params_.activeSpeakers, kAmbiSpeakerDecoderMaxSpeakers);
        syncSelectedFromSpeaker();
        rebuildMatrix();
    }

    AmbiSpeakerDecoderParams params() const { return params_; }
    const std::array<AmbiSpeaker, kAmbiSpeakerDecoderMaxSpeakers>& speakers() const { return speakers_; }
    const std::array<std::array<float, kAmbiSpeakerDecoderMaxChannels>, kAmbiSpeakerDecoderMaxSpeakers>& matrix() const { return matrix_; }

    AmbiSpeaker speaker(uint32_t index) const
    {
        return speakers_[std::min<uint32_t>(index, kAmbiSpeakerDecoderMaxSpeakers - 1u)];
    }

    void setSpeaker(uint32_t index, AmbiSpeaker speaker)
    {
        if (index >= kAmbiSpeakerDecoderMaxSpeakers) return;
        speaker.azimuthDeg = wrapSignedDeg(speaker.azimuthDeg);
        speaker.elevationDeg = clamp(speaker.elevationDeg, -90.0f, 90.0f);
        speaker.distance = clamp(speaker.distance, 0.15f, 2.0f);
        speaker.gain = clamp(speaker.gain, 0.0f, 2.0f);
        speakers_[index] = speaker;
        params_.layout = AmbiSpeakerLayoutPreset::Custom;
        if (index == params_.selectedSpeaker) {
            syncSelectedFromSpeaker();
        }
        rebuildMatrix();
    }

    void setSpeakerGain(uint32_t index, float gain)
    {
        if (index >= kAmbiSpeakerDecoderMaxSpeakers) return;
        speakers_[index].gain = clamp(gain, 0.0f, 2.0f);
        params_.selectedSpeaker = std::min<uint32_t>(index, std::max<uint32_t>(1u, params_.activeSpeakers) - 1u);
        syncSelectedFromSpeaker();
        rebuildMatrix();
    }

    void setSpeakerEnabled(uint32_t index, bool enabled)
    {
        if (index >= kAmbiSpeakerDecoderMaxSpeakers) return;
        speakers_[index].enabled = enabled;
        params_.selectedSpeaker = std::min<uint32_t>(index, std::max<uint32_t>(1u, params_.activeSpeakers) - 1u);
        syncSelectedFromSpeaker();
    }

    void setSpeakerSolo(uint32_t index, bool solo)
    {
        if (index >= kAmbiSpeakerDecoderMaxSpeakers) return;
        speakers_[index].solo = solo;
        params_.selectedSpeaker = std::min<uint32_t>(index, std::max<uint32_t>(1u, params_.activeSpeakers) - 1u);
        syncSelectedFromSpeaker();
    }

    void setSpeakers(std::array<AmbiSpeaker, kAmbiSpeakerDecoderMaxSpeakers> speakers)
    {
        for (auto& speaker : speakers) {
            speaker.azimuthDeg = wrapSignedDeg(speaker.azimuthDeg);
            speaker.elevationDeg = clamp(speaker.elevationDeg, -90.0f, 90.0f);
            speaker.distance = clamp(speaker.distance, 0.15f, 2.0f);
            speaker.gain = clamp(speaker.gain, 0.0f, 2.0f);
        }
        speakers_ = speakers;
        syncSelectedFromSpeaker();
        rebuildMatrix();
    }

    void processFrame(const float* input3Oa, float* output64) const
    {
        std::fill(output64, output64 + kAmbiSpeakerDecoderMaxSpeakers, 0.0f);
        if (!input3Oa) return;
        const uint32_t n = std::min<uint32_t>(params_.activeSpeakers, kAmbiSpeakerDecoderMaxSpeakers);
        const uint32_t channels = ambiChannelsForOrder(params_.order);
        const float outGain = dbToGain(params_.outputGainDb);
        bool anySolo = false;
        for (uint32_t spk = 0; spk < n; ++spk) anySolo = anySolo || speakers_[spk].solo;
        for (uint32_t spk = 0; spk < n; ++spk) {
            float value = 0.0f;
            for (uint32_t ch = 0; ch < channels; ++ch) {
                value += input3Oa[ch] * matrix_[spk][ch];
            }
            const bool audible = speakers_[spk].enabled && (!anySolo || speakers_[spk].solo);
            output64[spk] = audible ? flushDenormal(value * speakers_[spk].gain * outGain) : 0.0f;
        }
    }

    template <typename Sample>
    void processBlock(const Sample* const* input3Oa, Sample* const* output64, uint32_t inputChannels, uint32_t outputChannels, uint32_t frames) const
    {
        if (!output64 || frames == 0u) return;
        outputChannels = std::min<uint32_t>(outputChannels, kAmbiSpeakerDecoderMaxSpeakers);
        for (uint32_t spk = 0; spk < outputChannels; ++spk) {
            if (output64[spk]) std::fill(output64[spk], output64[spk] + frames, static_cast<Sample>(0));
        }
        if (!input3Oa) return;

        const uint32_t n = std::min<uint32_t>({ params_.activeSpeakers, outputChannels, kAmbiSpeakerDecoderMaxSpeakers });
        const uint32_t channels = std::min<uint32_t>(ambiChannelsForOrder(params_.order), inputChannels);
        const float outGain = dbToGain(params_.outputGainDb);
        bool anySolo = false;
        for (uint32_t spk = 0; spk < n; ++spk) anySolo = anySolo || speakers_[spk].solo;

        for (uint32_t spk = 0; spk < n; ++spk) {
            Sample* out = output64[spk];
            if (!out) continue;
            const bool audible = speakers_[spk].enabled && (!anySolo || speakers_[spk].solo);
            if (!audible) continue;
            const float speakerGain = speakers_[spk].gain * outGain;
            for (uint32_t frame = 0; frame < frames; ++frame) {
                float value = 0.0f;
                for (uint32_t ch = 0; ch < channels; ++ch) {
                    const Sample* in = input3Oa[ch];
                    value += (in ? static_cast<float>(in[frame]) : 0.0f) * matrix_[spk][ch];
                }
                out[frame] = static_cast<Sample>(flushDenormal(value * speakerGain));
            }
        }
    }

private:
    static float wrapSignedDeg(float value)
    {
        while (value > 180.0f) value -= 360.0f;
        while (value <= -180.0f) value += 360.0f;
        return value;
    }

    static AmbiSpeaker fromXyz(float x, float y, float z)
    {
        const float r = std::sqrt(std::max(0.000001f, x * x + y * y + z * z));
        return {
            wrapSignedDeg(std::atan2(y, x) * 180.0f / kPi),
            clamp(std::asin(clamp(z / r, -1.0f, 1.0f)) * 180.0f / kPi, -90.0f, 90.0f),
            std::max(0.15f, r),
            1.0f,
            true,
            false
        };
    }

    void clearSpeakers()
    {
        for (auto& speaker : speakers_) {
            speaker = {};
            speaker.enabled = false;
        }
    }

    void addSpeaker(uint32_t index, float azimuthDeg, float elevationDeg, float distance = 1.0f)
    {
        if (index >= kAmbiSpeakerDecoderMaxSpeakers) return;
        speakers_[index] = {
            wrapSignedDeg(azimuthDeg),
            clamp(elevationDeg, -90.0f, 90.0f),
            clamp(distance, 0.15f, 2.0f),
            1.0f,
            true,
            false
        };
    }

    void addRingClockwise(uint32_t outBase, uint32_t count, float rightAzimuthDeg, float elevationDeg)
    {
        const float step = 360.0f / static_cast<float>(std::max<uint32_t>(1u, count));
        for (uint32_t i = 0; i < count; ++i) {
            addSpeaker(outBase + i, rightAzimuthDeg - step * static_cast<float>(i), elevationDeg);
        }
    }

    void generateCustomLayout(AmbiSpeakerCustomField field, uint32_t count)
    {
        clearSpeakers();
        params_.layout = AmbiSpeakerLayoutPreset::Custom;
        params_.customField = field;
        params_.activeSpeakers = std::clamp<uint32_t>(count, 2u, kAmbiSpeakerDecoderMaxSpeakers);
        const float golden = kPi * (3.0f - std::sqrt(5.0f));
        const float denom = static_cast<float>(std::max<uint32_t>(1u, params_.activeSpeakers - 1u));
        for (uint32_t i = 0; i < params_.activeSpeakers; ++i) {
            float z = 0.0f;
            if (field == AmbiSpeakerCustomField::Hemisphere) {
                z = static_cast<float>(i) / denom;
            } else {
                z = 1.0f - 2.0f * (static_cast<float>(i) + 0.5f) / static_cast<float>(params_.activeSpeakers);
            }
            const float radius = std::sqrt(std::max(0.0f, 1.0f - z * z));
            const float angle = -45.0f * kPi / 180.0f - golden * static_cast<float>(i);
            const float x = std::cos(angle) * radius;
            const float y = std::sin(angle) * radius;
            speakers_[i] = fromXyz(x, y, z);
            speakers_[i].distance = 1.0f;
        }
        params_.selectedSpeaker = std::min<uint32_t>(params_.selectedSpeaker, params_.activeSpeakers - 1u);
        syncSelectedFromSpeaker();
    }

    void applyLayout(AmbiSpeakerLayoutPreset preset)
    {
        clearSpeakers();
        switch (preset) {
        case AmbiSpeakerLayoutPreset::Quad:
            params_.activeSpeakers = 4;
            addSpeaker(0, -45.0f, 0.0f);
            addSpeaker(1, -135.0f, 0.0f);
            addSpeaker(2, 135.0f, 0.0f);
            addSpeaker(3, 45.0f, 0.0f);
            break;
        case AmbiSpeakerLayoutPreset::Cube8: {
            params_.activeSpeakers = 8;
            const float pts[8][3] = {
                { 1, -1, -1 }, { -1, -1, -1 }, { -1, 1, -1 }, { 1, 1, -1 },
                { 1, -1, 1 }, { -1, -1, 1 }, { -1, 1, 1 }, { 1, 1, 1 },
            };
            for (uint32_t i = 0; i < 8; ++i) speakers_[i] = fromXyz(pts[i][0], pts[i][1], pts[i][2]);
            break;
        }
        case AmbiSpeakerLayoutPreset::Cube17: {
            params_.activeSpeakers = 17;
            const float pts[17][3] = {
                { 1, -1, -1 }, { -1, -1, -1 }, { -1, 1, -1 }, { 1, 1, -1 },
                { 1, -1, 0 }, { 0, -1, 0 }, { -1, -1, 0 }, { -1, 0, 0 },
                { -1, 1, 0 }, { 0, 1, 0 }, { 1, 1, 0 }, { 1, 0, 0 },
                { 1, -1, 1 }, { -1, -1, 1 }, { -1, 1, 1 }, { 1, 1, 1 },
                { 0, 0, 1 },
            };
            for (uint32_t i = 0; i < 17; ++i) speakers_[i] = fromXyz(pts[i][0], pts[i][1], pts[i][2]);
            break;
        }
        case AmbiSpeakerLayoutPreset::Dome24:
            params_.activeSpeakers = 24;
            addRingClockwise(0, 12, -30.0f, 0.0f);
            addRingClockwise(12, 8, -45.0f, 32.0f);
            addRingClockwise(20, 4, -90.0f, 66.6f);
            break;
        case AmbiSpeakerLayoutPreset::Dome25:
            params_.activeSpeakers = 25;
            addRingClockwise(0, 12, -30.0f, 0.0f);
            addRingClockwise(12, 8, -45.0f, 32.0f);
            addRingClockwise(20, 4, -90.0f, 66.6f);
            addSpeaker(24, 0.0f, 90.0f);
            break;
        case AmbiSpeakerLayoutPreset::QuadOverhead6:
            params_.activeSpeakers = 6;
            addSpeaker(0, -45.0f, 0.0f);
            addSpeaker(1, -135.0f, 0.0f);
            addSpeaker(2, 135.0f, 0.0f);
            addSpeaker(3, 45.0f, 0.0f);
            addSpeaker(4, -90.0f, 60.0f);
            addSpeaker(5, 90.0f, 60.0f);
            break;
        case AmbiSpeakerLayoutPreset::Sphere24:
        default:
            params_.activeSpeakers = 24;
            for (uint32_t i = 0; i < 24; ++i) {
                const auto& v = k3OafxPoints[i];
                speakers_[i] = fromXyz(v.x, v.y, v.z);
            }
            break;
        }
        params_.selectedSpeaker = std::min<uint32_t>(params_.selectedSpeaker, params_.activeSpeakers - 1u);
        syncSelectedFromSpeaker();
    }

    void syncSelectedFromSpeaker()
    {
        params_.selectedSpeaker = std::min<uint32_t>(params_.selectedSpeaker, std::max<uint32_t>(1u, params_.activeSpeakers) - 1u);
        const auto& speaker = speakers_[params_.selectedSpeaker];
        params_.selectedAzimuthDeg = speaker.azimuthDeg;
        params_.selectedElevationDeg = speaker.elevationDeg;
        params_.selectedDistance = speaker.distance;
        params_.selectedGain = speaker.gain;
        params_.selectedEnabled = speaker.enabled;
    }

    static bool invertMatrix(std::array<std::array<float, kAmbiSpeakerDecoderMaxChannels * 2>, kAmbiSpeakerDecoderMaxChannels>& aug, uint32_t n)
    {
        for (uint32_t col = 0; col < n; ++col) {
            uint32_t pivot = col;
            float best = std::fabs(aug[col][col]);
            for (uint32_t row = col + 1; row < n; ++row) {
                const float value = std::fabs(aug[row][col]);
                if (value > best) {
                    best = value;
                    pivot = row;
                }
            }
            if (best < 0.000001f) return false;
            if (pivot != col) std::swap(aug[pivot], aug[col]);
            const float invPivot = 1.0f / aug[col][col];
            for (uint32_t c = 0; c < n * 2u; ++c) aug[col][c] *= invPivot;
            for (uint32_t row = 0; row < n; ++row) {
                if (row == col) continue;
                const float f = aug[row][col];
                if (std::fabs(f) < 0.0000001f) continue;
                for (uint32_t c = 0; c < n * 2u; ++c) aug[row][c] -= f * aug[col][c];
            }
        }
        return true;
    }

    static float legendreP(uint32_t n, float x)
    {
        if (n == 0u) return 1.0f;
        if (n == 1u) return x;
        float p0 = 1.0f;
        float p1 = x;
        for (uint32_t order = 2; order <= n; ++order) {
            const float p = ((2.0f * static_cast<float>(order) - 1.0f) * x * p1
                - (static_cast<float>(order) - 1.0f) * p0) / static_cast<float>(order);
            p0 = p1;
            p1 = p;
        }
        return p1;
    }

    static float orderWeight(AmbiSpeakerDecoderWeighting weighting, uint32_t order, uint32_t maxOrder)
    {
        if (order == 0u || weighting == AmbiSpeakerDecoderWeighting::None) return 1.0f;
        maxOrder = std::max<uint32_t>(1u, maxOrder);
        if (weighting == AmbiSpeakerDecoderWeighting::MaxRe) {
            const float angle = (137.9f * kPi / 180.0f) / (static_cast<float>(maxOrder) + 1.51f);
            return clamp(legendreP(order, std::cos(angle)), 0.0f, 1.0f);
        }
        if (weighting == AmbiSpeakerDecoderWeighting::InPhase) {
            float value = 1.0f;
            for (uint32_t k = 0; k < order; ++k) {
                value *= static_cast<float>(maxOrder - k) / static_cast<float>(maxOrder + k + 2u);
            }
            return clamp(value, 0.0f, 1.0f);
        }
        return 1.0f;
    }

    void rebuildMatrix()
    {
        for (auto& row : matrix_) row.fill(0.0f);
        const uint32_t nSpeakers = std::min<uint32_t>(params_.activeSpeakers, kAmbiSpeakerDecoderMaxSpeakers);
        const uint32_t nChannels = ambiChannelsForOrder(params_.order);
        std::array<std::array<float, kAmbiSpeakerDecoderMaxChannels>, kAmbiSpeakerDecoderMaxSpeakers> basis {};
        for (uint32_t spk = 0; spk < nSpeakers; ++spk) {
            basis[spk] = acnSn3dBasis7(directionFromAed(speakers_[spk].azimuthDeg, speakers_[spk].elevationDeg));
        }

        if (params_.mode == AmbiSpeakerDecoderMode::Basic) {
            const float scale = params_.width / std::sqrt(std::max(1.0f, static_cast<float>(nSpeakers)));
            for (uint32_t spk = 0; spk < nSpeakers; ++spk) {
                float norm = 0.0f;
                for (uint32_t ch = 0; ch < nChannels; ++ch) norm += basis[spk][ch] * basis[spk][ch];
                norm = 1.0f / std::sqrt(std::max(0.000001f, norm));
                for (uint32_t ch = 0; ch < nChannels; ++ch) {
                    const uint32_t order = static_cast<uint32_t>(std::sqrt(static_cast<float>(ch)));
                    matrix_[spk][ch] = basis[spk][ch] * norm * scale * orderWeight(params_.weighting, order, params_.order);
                }
            }
            return;
        }

        std::array<std::array<float, kAmbiSpeakerDecoderMaxChannels * 2>, kAmbiSpeakerDecoderMaxChannels> aug {};
        for (uint32_t r = 0; r < nChannels; ++r) {
            for (uint32_t c = 0; c < nChannels; ++c) {
                float sum = 0.0f;
                for (uint32_t spk = 0; spk < nSpeakers; ++spk) {
                    sum += basis[spk][r] * basis[spk][c];
                }
                aug[r][c] = sum + (r == c ? params_.regularization : 0.0f);
            }
            aug[r][nChannels + r] = 1.0f;
        }
        if (!invertMatrix(aug, nChannels)) {
            params_.mode = AmbiSpeakerDecoderMode::Basic;
            rebuildMatrix();
            return;
        }

        const float densityScale = std::sqrt(std::max(1.0f, static_cast<float>(nChannels)) / std::max(1.0f, static_cast<float>(nSpeakers)));
        const float mmdBlend = params_.mode == AmbiSpeakerDecoderMode::Mmd ? 0.45f : 0.0f;
        for (uint32_t spk = 0; spk < nSpeakers; ++spk) {
            float rowEnergy = 0.0f;
            std::array<float, kAmbiSpeakerDecoderMaxChannels> epadRow {};
            for (uint32_t ch = 0; ch < nChannels; ++ch) {
                float value = 0.0f;
                for (uint32_t k = 0; k < nChannels; ++k) {
                    value += basis[spk][k] * aug[k][nChannels + ch];
                }
                epadRow[ch] = value;
                rowEnergy += value * value;
            }
            const float energyNorm = 1.0f / std::sqrt(std::max(0.000001f, rowEnergy));
            for (uint32_t ch = 0; ch < nChannels; ++ch) {
                const uint32_t order = static_cast<uint32_t>(std::sqrt(static_cast<float>(ch)));
                const float weight = orderWeight(params_.weighting, order, params_.order);
                const float epad = epadRow[ch] * lerp(1.0f, energyNorm, clamp(params_.energy, 0.0f, 1.0f)) * densityScale;
                const float basic = basis[spk][ch] * densityScale / std::sqrt(std::max(1.0f, static_cast<float>(nChannels)));
                matrix_[spk][ch] = lerp(epad, basic, mmdBlend) * params_.width * weight;
            }
        }
    }

    double sampleRate_ = 48000.0;
    AmbiSpeakerDecoderParams params_ {};
    std::array<AmbiSpeaker, kAmbiSpeakerDecoderMaxSpeakers> speakers_ {};
    std::array<std::array<float, kAmbiSpeakerDecoderMaxChannels>, kAmbiSpeakerDecoderMaxSpeakers> matrix_ {};
};

} // namespace s3g
