#pragma once

#include "s3g_cascade_taps.h"
#include "s3g_fixed_bus_ring_renderer.h"
#include "s3g_iterate_delay.h"
#include "s3g_orbit_delay.h"
#include "s3g_shard_scatter.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace s3g {

enum class DelayFieldModel : uint32_t {
    Shard = 0u,
    Orbit,
    Cascade,
    Iterate,
    Count,
};

constexpr uint32_t kDelayFieldModelCount =
    static_cast<uint32_t>(DelayFieldModel::Count);
constexpr uint32_t kDelayFieldHostChannels = 16u;

inline DelayFieldModel sanitizeDelayFieldModel(uint32_t value)
{
    return static_cast<DelayFieldModel>(std::min<uint32_t>(value,
        kDelayFieldModelCount - 1u));
}

inline const char* delayFieldModelName(DelayFieldModel model)
{
    switch (model) {
    case DelayFieldModel::Shard: return "SHARD";
    case DelayFieldModel::Orbit: return "ORBIT";
    case DelayFieldModel::Cascade: return "CASCADE";
    case DelayFieldModel::Iterate: return "ITERATE";
    case DelayFieldModel::Count: break;
    }
    return "SHARD";
}

struct DelayFieldParams {
    DelayFieldModel model = DelayFieldModel::Shard;
    FixedBusRingFormat format = FixedBusRingFormat::Direct16;
    float outputRotationDegrees = 0.0f;
    ShardScatterParams shard {};
    OrbitDelayParams orbit {};
    CascadeTapsParams cascade {};
    IterateDelayParams iterate {};
};

class DelayField {
public:
    bool prepare(double sampleRate, uint32_t maxBlockFrames)
    {
        maxBlockFrames_ = std::max<uint32_t>(1u, maxBlockFrames);
        if (!shard_.prepare(sampleRate, maxBlockFrames_)
            || !orbit_.prepare(sampleRate, maxBlockFrames_)
            || !cascade_.prepare(sampleRate, maxBlockFrames_)
            || !iterate_.prepare(sampleRate, maxBlockFrames_)) return false;
        try {
            for (auto& channel : currentScratch_)
                channel.assign(maxBlockFrames_, 0.0f);
            for (auto& channel : previousScratch_)
                channel.assign(maxBlockFrames_, 0.0f);
        } catch (...) {
            return false;
        }
        for (uint32_t channel = 0u; channel < kDelayFieldHostChannels;
             ++channel) {
            currentPointers_[channel] = currentScratch_[channel].data();
            previousPointers_[channel] = previousScratch_[channel].data();
        }
        sampleRate_ = sampleRate;
        currentModel_ = sanitizeDelayFieldModel(
            static_cast<uint32_t>(params_.model));
        previousModel_ = currentModel_;
        transitionRemaining_ = 0u;
        ready_ = true;
        setParams(params_);
        reset();
        return true;
    }

    void reset()
    {
        shard_.reset();
        orbit_.reset();
        cascade_.reset();
        iterate_.reset();
        currentModel_ = sanitizeDelayFieldModel(
            static_cast<uint32_t>(params_.model));
        previousModel_ = currentModel_;
        transitionRemaining_ = 0u;
    }

    void setParams(const DelayFieldParams& params)
    {
        params_ = params;
        params_.model = sanitizeDelayFieldModel(
            static_cast<uint32_t>(params_.model));
        params_.format = sanitizeFixedBusRingFormat(
            static_cast<uint32_t>(params_.format));
        params_.outputRotationDegrees = sanitizeFixedBusRingRotation(
            params_.outputRotationDegrees);
        auto shardParams = params_.shard;
        auto cascadeParams = params_.cascade;
        auto iterateParams = params_.iterate;
        shardParams.dry = 0.0f;
        cascadeParams.dry = 0.0f;
        iterateParams.dry = 0.0f;
        shard_.setParams(shardParams);
        orbit_.setParams(params_.orbit);
        orbit_.setExternalDirect(true);
        cascade_.setParams(cascadeParams);
        iterate_.setParams(iterateParams);
        // Format is a monitoring/projection decision. Keep every model on its
        // complete sixteen-lane delay field so Stereo, Quad, and 8ch modes are
        // true folds rather than reduced versions of the algorithms.
        shard_.setOutputChannels(kDelayFieldHostChannels);
        orbit_.setOutputChannels(kDelayFieldHostChannels);
        cascade_.setOutputChannels(kDelayFieldHostChannels);
        iterate_.setOutputChannels(kDelayFieldHostChannels);
        renderer_.configure(params_.format, params_.outputRotationDegrees);
    }

    const DelayFieldParams& params() const { return params_; }
    uint32_t activeChannels() const { return renderer_.activeChannels(); }
    const std::array<float, kDelayFieldHostChannels>& sourcePeaks() const
    {
        return sourcePeaks_;
    }

    void process(const float* left, const float* right, float* const* output,
                 uint32_t frames)
    {
        if (!ready_ || !output || frames == 0u) return;
        frames = std::min(frames, maxBlockFrames_);
        if (params_.model != currentModel_) {
            previousModel_ = currentModel_;
            currentModel_ = params_.model;
            resetModel(currentModel_);
            transitionRemaining_ = transitionFrames();
        }
        clearScratch(currentScratch_, frames);
        processModel(currentModel_, left, right, currentPointers_.data(), frames);
        const bool transitioning = transitionRemaining_ > 0u
            && previousModel_ != currentModel_;
        if (transitioning) {
            clearScratch(previousScratch_, frames);
            processModel(previousModel_, left, right, previousPointers_.data(),
                frames);
        }

        constexpr float kHalfPi = 1.57079632679489661923f;
        std::array<float, kDelayFieldHostChannels> activeFrame {};
        std::array<float, kDelayFieldHostChannels> renderedFrame {};
        sourcePeaks_.fill(0.0f);
        for (uint32_t frame = 0u; frame < frames; ++frame) {
            float currentGain = 1.0f;
            float previousGain = 0.0f;
            if (transitioning && transitionRemaining_ > 0u) {
                const float progress = 1.0f - static_cast<float>(
                    transitionRemaining_) / transitionFrames();
                currentGain = std::sin(progress * kHalfPi);
                previousGain = std::cos(progress * kHalfPi);
                --transitionRemaining_;
            }
            for (uint32_t channel = 0u; channel < kDelayFieldHostChannels;
                 ++channel) {
                activeFrame[channel] = currentScratch_[channel][frame]
                    * currentGain + (transitioning
                        ? previousScratch_[channel][frame] * previousGain
                        : 0.0f);
                sourcePeaks_[channel] = std::max(sourcePeaks_[channel],
                    std::abs(activeFrame[channel]));
            }
            renderer_.processFrame(activeFrame.data(), renderedFrame.data());
            const float directGain = directLevel(currentModel_) * currentGain
                + (transitioning
                    ? directLevel(previousModel_) * previousGain : 0.0f);
            const float rawLeft = left ? left[frame] : 0.0f;
            const float inputLeft = std::isfinite(rawLeft) ? rawLeft : 0.0f;
            const float rawRight = right ? right[frame] : inputLeft;
            const float inputRight = std::isfinite(rawRight)
                ? rawRight : inputLeft;
            addDirectFrame(inputLeft, inputRight, directGain,
                renderedFrame.data());
            for (uint32_t channel = 0u; channel < kDelayFieldHostChannels;
                 ++channel)
                output[channel][frame] = renderedFrame[channel];
        }
    }

private:
    using Scratch = std::array<std::vector<float>, kDelayFieldHostChannels>;

    uint32_t transitionFrames() const
    {
        return std::max<uint32_t>(1u, static_cast<uint32_t>(
            std::round(sampleRate_ * 0.050)));
    }

    static void clearScratch(Scratch& scratch, uint32_t frames)
    {
        for (auto& channel : scratch)
            std::fill_n(channel.data(), frames, 0.0f);
    }

    float directLevel(DelayFieldModel model) const
    {
        float level = 0.0f;
        switch (model) {
        case DelayFieldModel::Shard: level = params_.shard.dry; break;
        case DelayFieldModel::Orbit: level = 1.0f - params_.orbit.wet; break;
        case DelayFieldModel::Cascade: level = params_.cascade.dry; break;
        case DelayFieldModel::Iterate: level = params_.iterate.dry; break;
        case DelayFieldModel::Count: break;
        }
        return std::clamp(std::isfinite(level) ? level : 0.0f, 0.0f, 1.0f);
    }

    void addDirectFrame(float left, float right, float gain,
                        float* output) const
    {
        if (!output || gain <= 0.0f) return;
        const uint32_t channels = renderer_.activeChannels();
        const float rotation = params_.outputRotationDegrees / 360.0f
            * static_cast<float>(channels);
        addRingSource(left * gain, rotation, channels, output);
        addRingSource(right * gain, rotation + 0.5f * channels,
            channels, output);
    }

    static void addRingSource(float value, float position, uint32_t channels,
                              float* output)
    {
        if (!output || channels < 2u || value == 0.0f) return;
        position -= std::floor(position / static_cast<float>(channels))
            * static_cast<float>(channels);
        const uint32_t first = static_cast<uint32_t>(std::floor(position))
            % channels;
        const uint32_t second = (first + 1u) % channels;
        const float fraction = position - std::floor(position);
        constexpr float kHalfPi = 1.57079632679489661923f;
        output[first] += value * std::cos(fraction * kHalfPi);
        output[second] += value * std::sin(fraction * kHalfPi);
    }

    void processModel(DelayFieldModel model, const float* left,
                      const float* right, float* const* output,
                      uint32_t frames)
    {
        switch (model) {
        case DelayFieldModel::Shard:
            shard_.process(left, right, output, frames);
            break;
        case DelayFieldModel::Orbit:
            orbit_.process(left, right, output, frames);
            break;
        case DelayFieldModel::Cascade:
            cascade_.process(left, right, output, frames);
            break;
        case DelayFieldModel::Iterate:
            iterate_.process(left, right, output, frames);
            break;
        case DelayFieldModel::Count:
            break;
        }
    }

    void resetModel(DelayFieldModel model)
    {
        switch (model) {
        case DelayFieldModel::Shard: shard_.reset(); break;
        case DelayFieldModel::Orbit: orbit_.reset(); break;
        case DelayFieldModel::Cascade: cascade_.reset(); break;
        case DelayFieldModel::Iterate: iterate_.reset(); break;
        case DelayFieldModel::Count: break;
        }
    }

    double sampleRate_ = 48000.0;
    uint32_t maxBlockFrames_ = 0u;
    bool ready_ = false;
    DelayFieldModel currentModel_ = DelayFieldModel::Shard;
    DelayFieldModel previousModel_ = DelayFieldModel::Shard;
    uint32_t transitionRemaining_ = 0u;
    DelayFieldParams params_ {};
    ShardScatter shard_;
    OrbitDelay orbit_;
    CascadeTaps cascade_;
    IterateDelay iterate_;
    FixedBusRingRenderer<> renderer_;
    Scratch currentScratch_;
    Scratch previousScratch_;
    std::array<float*, kDelayFieldHostChannels> currentPointers_ {};
    std::array<float*, kDelayFieldHostChannels> previousPointers_ {};
    std::array<float, kDelayFieldHostChannels> sourcePeaks_ {};
};

} // namespace s3g
