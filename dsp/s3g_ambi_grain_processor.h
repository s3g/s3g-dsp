#pragma once

#include "s3g_math.h"
#include "s3g_realtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace s3g {

constexpr uint32_t kAmbiGrainMaxOrder = 3;
constexpr uint32_t kAmbiGrainChannels = 16;
constexpr float kAmbiGrainMaxDensity = 160.0f;
constexpr float kAmbiGrainMaxGrainMs = 4000.0f;
constexpr float kAmbiGrainMaxOverlap = 32.0f;

enum class AmbiGrainMode : uint32_t {
    Scan = 0,
    Cloud = 1,
    Freeze = 2,
    Jump = 3,
};

enum class AmbiGrainEnvelope : uint32_t {
    Parzen = 0,
    Sine = 1,
    Hann = 2,
    Triangle = 3,
    Gauss = 4,
};

struct AmbiGrainSample {
    std::vector<float> audio;
    uint32_t frames = 0;
    uint32_t channels = 0;
    double sampleRate = 48000.0;
    std::string path;
};

struct AmbiGrainParams {
    uint32_t order = 3;
    AmbiGrainMode mode = AmbiGrainMode::Scan;
    float density = 28.0f;
    float grainMs = 90.0f;
    float sourcePosition = 0.0f;
    float scanSpeed = 1.0f;
    float positionJitter = 0.12f;
    float rate = 1.0f;
    float rateJitterOct = 0.04f;
    float reverseChance = 0.0f;
    float freezePosition = 0.5f;
    uint32_t jumpSteps = 8;
    bool sync = true;
    AmbiGrainEnvelope envelope = AmbiGrainEnvelope::Parzen;
    float outputGainDb = -12.0f;
};

inline uint32_t ambiGrainChannelsForOrder(uint32_t order)
{
    order = std::clamp<uint32_t>(order, 1u, kAmbiGrainMaxOrder);
    return (order + 1u) * (order + 1u);
}

inline uint32_t ambiGrainOrderForChannels(uint32_t channels)
{
    if (channels >= 16u) return 3u;
    if (channels >= 9u) return 2u;
    return 1u;
}

inline float ambiGrainDensityLimitForGrainMs(float grainMs)
{
    const float grainSec = std::max(0.008f, grainMs * 0.001f);
    return std::min(kAmbiGrainMaxDensity, kAmbiGrainMaxOverlap / grainSec);
}

inline AmbiGrainParams sanitizeAmbiGrainParams(AmbiGrainParams params)
{
    params.order = std::clamp<uint32_t>(params.order, 1u, kAmbiGrainMaxOrder);
    params.mode = static_cast<AmbiGrainMode>(std::min<uint32_t>(static_cast<uint32_t>(params.mode), 3u));
    params.grainMs = clamp(params.grainMs, 8.0f, kAmbiGrainMaxGrainMs);
    params.density = clamp(params.density, 0.1f, ambiGrainDensityLimitForGrainMs(params.grainMs));
    params.sourcePosition = clamp(params.sourcePosition, 0.0f, 1.0f);
    params.scanSpeed = clamp(params.scanSpeed, 0.0f, 4.0f);
    params.positionJitter = clamp(params.positionJitter, 0.0f, 1.0f);
    params.rate = clamp(params.rate, 0.125f, 4.0f);
    params.rateJitterOct = clamp(params.rateJitterOct, 0.0f, 1.0f);
    params.reverseChance = clamp(params.reverseChance, 0.0f, 1.0f);
    params.freezePosition = clamp(params.freezePosition, 0.0f, 1.0f);
    params.jumpSteps = std::clamp<uint32_t>(params.jumpSteps, 2u, 64u);
    params.envelope = static_cast<AmbiGrainEnvelope>(std::min<uint32_t>(static_cast<uint32_t>(params.envelope), 4u));
    params.outputGainDb = clamp(params.outputGainDb, -60.0f, 6.0f);
    return params;
}

class AmbiGrainProcessor {
public:
    void prepare(double sampleRate)
    {
        sampleRate_ = std::max(1.0, sampleRate);
        maxGrainFrames_ = std::max<uint32_t>(65536u, static_cast<uint32_t>(std::ceil(sampleRate_ * 8.0)));
        reset();
    }

    void reset()
    {
        for (auto& grain : grains_) grain.active = false;
        densityPhase_ = 0.0;
        asyncCountdown_ = 0.0;
        scanPhase_ = 0.0;
        smoothGain_ = 0.0f;
        rng_ = 0x9e3779b9u;
        oldestGrain_ = 0u;
    }

    void setParams(AmbiGrainParams params)
    {
        params_ = sanitizeAmbiGrainParams(params);
    }

    const AmbiGrainParams& params() const { return params_; }

    void process(const std::shared_ptr<const AmbiGrainSample>& sample, float** outputs, uint32_t outChannels, uint32_t frames, bool playing = true)
    {
        if (!outputs || frames == 0u) return;
        for (uint32_t ch = 0; ch < outChannels; ++ch) {
            if (outputs[ch]) std::fill(outputs[ch], outputs[ch] + frames, 0.0f);
        }
        if (!sample || sample->frames < 8u || sample->channels == 0u || sample->audio.empty()) return;
        const uint32_t activeChannels = std::min<uint32_t>({ outChannels, kAmbiGrainChannels, ambiGrainChannelsForOrder(params_.order), sample->channels });
        if (activeChannels == 0u) return;

        const float gainTarget = playing ? dbToGain(params_.outputGainDb) * densityCompensation() : 0.0f;
        const double densityStep = static_cast<double>(params_.density) / sampleRate_;
        const uint32_t grainFrames = static_cast<uint32_t>(std::round(params_.grainMs * static_cast<float>(sampleRate_) * 0.001f));

        for (uint32_t i = 0; i < frames; ++i) {
            smoothGain_ += (gainTarget - smoothGain_) * 0.0012f;
            if (playing) {
                if (params_.sync) {
                    densityPhase_ += densityStep;
                    while (densityPhase_ >= 1.0) {
                        densityPhase_ -= 1.0;
                        startGrain(*sample, activeChannels, std::max<uint32_t>(8u, grainFrames));
                    }
                } else {
                    asyncCountdown_ -= 1.0;
                    while (asyncCountdown_ <= 0.0) {
                        startGrain(*sample, activeChannels, std::max<uint32_t>(8u, grainFrames));
                        const double mean = sampleRate_ / std::max(0.1, static_cast<double>(params_.density));
                        asyncCountdown_ += std::max(1.0, mean * (0.20 + static_cast<double>(randUnit()) * 1.80));
                    }
                }
                scanPhase_ += static_cast<double>(params_.scanSpeed) / std::max(1.0, static_cast<double>(sample->frames));
                if (scanPhase_ >= 1.0) scanPhase_ -= std::floor(scanPhase_);
            }

            std::array<float, kAmbiGrainChannels> frame {};
            renderActiveGrains(*sample, activeChannels, frame);
            for (uint32_t ch = 0; ch < activeChannels; ++ch) {
                if (outputs[ch]) outputs[ch][i] = flushDenormal(frame[ch] * smoothGain_);
            }
        }
    }

private:
    struct ActiveGrain {
        double sourcePos = 0.0;
        float rate = 1.0f;
        uint32_t age = 0u;
        uint32_t duration = 0u;
        uint32_t channels = 0u;
        bool active = false;
    };

    float densityCompensation() const
    {
        const float overlap = std::min(kAmbiGrainMaxOverlap, params_.density * params_.grainMs * 0.001f);
        return 1.0f / std::sqrt(std::max(1.0f, overlap));
    }

    static float triangle(float phase, float split = 0.5f)
    {
        phase = clamp(phase, 0.0f, 1.0f);
        split = clamp(split, 0.0001f, 0.9999f);
        return phase < split ? phase / split : 1.0f - (phase - split) / (1.0f - split);
    }

    static float parzen(float phase)
    {
        phase = clamp(phase, 0.0f, 1.0f);
        return phase > 0.5f
            ? 1.0f - ((1.0f - phase) * (1.0f - phase) * phase * 6.0f)
            : phase * phase * phase * 2.0f;
    }

    float envelopeAt(float phase) const
    {
        phase = clamp(phase, 0.0f, 1.0f);
        switch (params_.envelope) {
        case AmbiGrainEnvelope::Sine:
            return std::sin(kPi * phase);
        case AmbiGrainEnvelope::Hann:
            return 0.5f - 0.5f * std::cos(kPi * 2.0f * phase);
        case AmbiGrainEnvelope::Triangle:
            return triangle(phase, 0.5f);
        case AmbiGrainEnvelope::Gauss: {
            const float x = (phase - 0.5f) / 0.18f;
            return std::exp(-0.5f * x * x);
        }
        case AmbiGrainEnvelope::Parzen:
        default:
            return parzen(triangle(phase, 0.5f));
        }
    }

    uint32_t nextRand()
    {
        rng_ = rng_ * 1664525u + 1013904223u;
        return rng_;
    }

    float randUnit()
    {
        return static_cast<float>((nextRand() >> 8u) & 0x00ffffffu) / static_cast<float>(0x01000000u);
    }

    float randBi()
    {
        return randUnit() * 2.0f - 1.0f;
    }

    void startGrain(const AmbiGrainSample& sample, uint32_t activeChannels, uint32_t grainFrames)
    {
        if (activeChannels == 0u) return;
        grainFrames = std::min<uint32_t>(grainFrames, std::max<uint32_t>(8u, maxGrainFrames_ / 2u));

        const uint32_t maxStartFrame = sample.frames > grainFrames + 2u ? sample.frames - grainFrames - 2u : 1u;
        float u = 0.0f;
        switch (params_.mode) {
        case AmbiGrainMode::Cloud: u = randUnit(); break;
        case AmbiGrainMode::Freeze: u = params_.freezePosition; break;
        case AmbiGrainMode::Jump: {
            const float stepped = std::floor(params_.sourcePosition * static_cast<float>(params_.jumpSteps))
                / static_cast<float>(std::max<uint32_t>(1u, params_.jumpSteps - 1u));
            u = stepped;
            break;
        }
        case AmbiGrainMode::Scan:
        default: u = params_.sourcePosition > 0.0001f ? params_.sourcePosition : static_cast<float>(scanPhase_); break;
        }
        u = clamp(u + randBi() * params_.positionJitter, 0.0f, 1.0f);

        double sourcePos = static_cast<double>(u) * static_cast<double>(maxStartFrame);
        const float duration = static_cast<float>(grainFrames) * (0.75f + randUnit() * 0.5f);
        const float jitter = std::pow(2.0f, randBi() * params_.rateJitterOct);
        const float reverse = randUnit() < params_.reverseChance ? -1.0f : 1.0f;
        const float rate = params_.rate * jitter * reverse;
        if (rate < 0.0f) {
            sourcePos = std::min<double>(static_cast<double>(sample.frames - 1u), sourcePos + static_cast<double>(grainFrames));
        }

        ActiveGrain* slot = nullptr;
        for (auto& grain : grains_) {
            if (!grain.active) {
                slot = &grain;
                break;
            }
        }
        if (!slot) {
            slot = &grains_[oldestGrain_++ % grains_.size()];
        }
        slot->sourcePos = sourcePos;
        slot->rate = rate;
        slot->age = 0u;
        slot->duration = static_cast<uint32_t>(std::max(2.0f, duration));
        slot->channels = activeChannels;
        slot->active = true;
    }

    void renderActiveGrains(const AmbiGrainSample& sample, uint32_t activeChannels, std::array<float, kAmbiGrainChannels>& frame)
    {
        for (auto& grain : grains_) {
            if (!grain.active) continue;
            if (grain.age >= grain.duration) {
                grain.active = false;
                continue;
            }
            const float phase = static_cast<float>(grain.age) / static_cast<float>(std::max(1u, grain.duration - 1u));
            const float env = envelopeAt(phase);
            const double read = grain.sourcePos
                + static_cast<double>(grain.age) * static_cast<double>(grain.rate) * sample.sampleRate / sampleRate_;
            const uint32_t grainChannels = std::min<uint32_t>(activeChannels, grain.channels);
            for (uint32_t ch = 0; ch < grainChannels; ++ch) {
                frame[ch] = flushDenormal(frame[ch] + readLinearWrap(sample, ch, read) * env);
            }
            ++grain.age;
            if (grain.age >= grain.duration) grain.active = false;
        }
    }

    static float readLinear(const AmbiGrainSample& sample, uint32_t ch, double pos)
    {
        if (sample.frames < 2u || sample.channels == 0u) return 0.0f;
        pos = std::clamp(pos, 0.0, static_cast<double>(sample.frames - 1u));
        const uint32_t i0 = static_cast<uint32_t>(std::floor(pos));
        const uint32_t i1 = std::min<uint32_t>(sample.frames - 1u, i0 + 1u);
        const float frac = static_cast<float>(pos - std::floor(pos));
        const uint32_t srcCh = std::min<uint32_t>(ch, sample.channels - 1u);
        const float a = sample.audio[static_cast<size_t>(i0) * sample.channels + srcCh];
        const float b = sample.audio[static_cast<size_t>(i1) * sample.channels + srcCh];
        return a + (b - a) * frac;
    }

    static float readLinearWrap(const AmbiGrainSample& sample, uint32_t ch, double pos)
    {
        if (sample.frames < 2u || sample.channels == 0u) return 0.0f;
        const double frames = static_cast<double>(sample.frames);
        pos = std::fmod(pos, frames);
        if (pos < 0.0) pos += frames;
        const uint32_t i0 = static_cast<uint32_t>(std::floor(pos));
        const uint32_t i1 = (i0 + 1u) % sample.frames;
        const float frac = static_cast<float>(pos - std::floor(pos));
        const uint32_t srcCh = std::min<uint32_t>(ch, sample.channels - 1u);
        const float a = sample.audio[static_cast<size_t>(i0) * sample.channels + srcCh];
        const float b = sample.audio[static_cast<size_t>(i1) * sample.channels + srcCh];
        return a + (b - a) * frac;
    }

    double sampleRate_ = 48000.0;
    AmbiGrainParams params_ {};
    uint32_t maxGrainFrames_ = 65536u;
    double densityPhase_ = 0.0;
    double asyncCountdown_ = 0.0;
    double scanPhase_ = 0.0;
    float smoothGain_ = 0.0f;
    uint32_t rng_ = 0x9e3779b9u;
    std::array<ActiveGrain, 96> grains_ {};
    size_t oldestGrain_ = 0u;
};

} // namespace s3g
