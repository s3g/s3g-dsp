#pragma once

#include "s3g_loop_processor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>

namespace s3g {

constexpr uint32_t kMultiLoopMaxSources = 4;

enum class MultiLoopSourceRule : uint32_t {
    Order = 0,
    Interleave = 1,
    Random = 2,
    Morph = 3
};

struct MultiLoopCompositeOptions {
    MultiLoopSourceRule rule = MultiLoopSourceRule::Interleave;
    float sourceRateSpread = 0.0f;
    float sourceBlend = 1.0f;
    double maxSeconds = 600.0;
};

inline uint32_t multiLoopSourceChannelForLane(uint32_t lane, uint32_t sourceChannels)
{
    if (sourceChannels <= 1u) {
        return 0u;
    }
    if (sourceChannels <= kLoopProcessorChannels) {
        return lane % sourceChannels;
    }
    const double u = kLoopProcessorChannels > 1u
        ? static_cast<double>(lane) / static_cast<double>(kLoopProcessorChannels - 1u)
        : 0.0;
    return std::min<uint32_t>(
        sourceChannels - 1u,
        static_cast<uint32_t>(std::llround(u * static_cast<double>(sourceChannels - 1u))));
}

inline float multiLoopReadSourceLinear(const LoopProcessorSample& sample, uint32_t lane, double pos)
{
    if (sample.frames < 2u || sample.channels == 0u || sample.audio.empty()) {
        return 0.0f;
    }
    pos -= std::floor(pos / static_cast<double>(sample.frames)) * static_cast<double>(sample.frames);
    if (pos < 0.0) {
        pos += static_cast<double>(sample.frames);
    }
    const uint32_t i0 = static_cast<uint32_t>(std::floor(pos)) % sample.frames;
    const uint32_t i1 = (i0 + 1u) % sample.frames;
    const float frac = static_cast<float>(pos - std::floor(pos));
    const uint32_t srcCh = multiLoopSourceChannelForLane(lane, sample.channels);
    const float a = sample.audio[static_cast<size_t>(i0) * sample.channels + srcCh];
    const float b = sample.audio[static_cast<size_t>(i1) * sample.channels + srcCh];
    return a + (b - a) * frac;
}

inline float multiLoopReadSourceSeam(const LoopProcessorSample& sample, uint32_t lane, double pos)
{
    if (sample.frames < 4u || sample.channels == 0u || sample.audio.empty()) {
        return multiLoopReadSourceLinear(sample, lane, pos);
    }
    const double period = static_cast<double>(sample.frames);
    double phase = pos - std::floor(pos / period) * period;
    if (phase < 0.0) {
        phase += period;
    }

    const uint32_t fadeFrames = std::clamp<uint32_t>(
        static_cast<uint32_t>(std::round(sample.sampleRate * 0.020)),
        8u,
        std::max<uint32_t>(8u, std::min<uint32_t>(4096u, sample.frames / 4u)));
    if (fadeFrames <= 1u || sample.frames <= fadeFrames + 2u) {
        return multiLoopReadSourceLinear(sample, lane, phase);
    }

    float taper = 1.0f;
    if (phase < static_cast<double>(fadeFrames)) {
        const float u = static_cast<float>(phase / std::max(1.0, static_cast<double>(fadeFrames)));
        taper = 0.5f - 0.5f * std::cos(3.14159265359f * std::clamp(u, 0.0f, 1.0f));
    } else if (phase >= period - static_cast<double>(fadeFrames)) {
        const double seamPhase = phase - (period - static_cast<double>(fadeFrames));
        const float u = static_cast<float>(seamPhase / std::max(1.0, static_cast<double>(fadeFrames)));
        taper = 0.5f + 0.5f * std::cos(3.14159265359f * std::clamp(u, 0.0f, 1.0f));
    }
    return multiLoopReadSourceLinear(sample, lane, phase) * taper;
}

inline uint32_t multiLoopHashLane(uint32_t lane, uint32_t count)
{
    uint32_t x = lane * 747796405u + 2891336453u;
    x ^= x >> 16u;
    x *= 2246822519u;
    x ^= x >> 13u;
    return count == 0u ? 0u : x % count;
}

inline uint32_t multiLoopOrderedSourceForLane(uint32_t lane, uint32_t count)
{
    const uint32_t lanesPerSource = std::max<uint32_t>(1u, (kLoopProcessorChannels + count - 1u) / count);
    return std::min<uint32_t>(count - 1u, lane / lanesPerSource);
}

inline float multiLoopSourceRateForIndex(uint32_t index, uint32_t count, float spread)
{
    spread = std::clamp(spread, 0.0f, 1.0f);
    if (count <= 1u || spread <= 0.0001f) {
        return 1.0f;
    }
    const float u = static_cast<float>(index) / static_cast<float>(count - 1u);
    const float centered = u * 2.0f - 1.0f;
    return std::pow(2.0f, centered * spread);
}

inline float multiLoopMorphBlendForLane(float x, float width)
{
    width = std::clamp(width, 0.0f, 1.0f);
    if (width <= 0.0001f) {
        return (x - std::floor(x)) >= 0.5f ? 1.0f : 0.0f;
    }
    const float frac = x - std::floor(x);
    const float half = width * 0.5f;
    const float lo = 0.5f - half;
    const float hi = 0.5f + half;
    const float u = width >= 0.999f
        ? frac
        : std::clamp((frac - lo) / std::max(0.0001f, hi - lo), 0.0f, 1.0f);
    return u * u * (3.0f - 2.0f * u);
}

inline std::shared_ptr<LoopProcessorSample> buildMultiLoopComposite(
    const std::array<std::shared_ptr<const LoopProcessorSample>, kMultiLoopMaxSources>& sources,
    uint32_t sourceCount,
    const MultiLoopCompositeOptions& options)
{
    std::array<std::shared_ptr<const LoopProcessorSample>, kMultiLoopMaxSources> active {};
    uint32_t count = 0u;
    for (uint32_t i = 0; i < std::min<uint32_t>(sourceCount, kMultiLoopMaxSources); ++i) {
        if (sources[i] && sources[i]->frames >= 2u && sources[i]->channels > 0u && !sources[i]->audio.empty()) {
            active[count++] = sources[i];
        }
    }
    if (count == 0u) {
        return nullptr;
    }

    const double compositeRate = std::max(1.0, active[0]->sampleRate);
    uint32_t compositeFrames = 2u;
    for (uint32_t i = 0; i < count; ++i) {
        const double seconds = static_cast<double>(active[i]->frames) / std::max(1.0, active[i]->sampleRate);
        compositeFrames = std::max<uint32_t>(compositeFrames, static_cast<uint32_t>(std::ceil(seconds * compositeRate)));
    }
    compositeFrames = std::min<uint32_t>(
        compositeFrames,
        static_cast<uint32_t>(compositeRate * std::max(0.001, options.maxSeconds)));

    auto composite = std::make_shared<LoopProcessorSample>();
    composite->frames = compositeFrames;
    composite->channels = kLoopProcessorChannels;
    composite->sampleRate = compositeRate;
    composite->path = count == 1u ? active[0]->path : "multi-source composite";
    composite->audio.assign(static_cast<size_t>(compositeFrames) * kLoopProcessorChannels, 0.0f);

    const float sourceRateSpread = std::clamp(options.sourceRateSpread, 0.0f, 1.0f);
    const float sourceBlend = std::clamp(options.sourceBlend, 0.0f, 1.0f);
    for (uint32_t lane = 0; lane < kLoopProcessorChannels; ++lane) {
        uint32_t a = 0u;
        uint32_t b = 0u;
        float blend = 0.0f;
        if (options.rule == MultiLoopSourceRule::Order) {
            a = multiLoopOrderedSourceForLane(lane, count);
            b = a;
        } else if (options.rule == MultiLoopSourceRule::Random) {
            a = multiLoopHashLane(lane, count);
            b = a;
        } else if (options.rule == MultiLoopSourceRule::Morph && count > 1u) {
            const float u = kLoopProcessorChannels > 1u
                ? static_cast<float>(lane) / static_cast<float>(kLoopProcessorChannels - 1u)
                : 0.0f;
            const float x = u * static_cast<float>(count - 1u);
            a = static_cast<uint32_t>(std::floor(x));
            b = std::min<uint32_t>(count - 1u, a + 1u);
            blend = multiLoopMorphBlendForLane(x, sourceBlend);
            if (sourceBlend <= 0.0001f && blend >= 1.0f) {
                a = b;
                blend = 0.0f;
            }
        } else {
            a = lane % count;
            b = a;
        }

        const auto& sampleA = *active[a];
        const auto& sampleB = *active[b];
        const double rateA = static_cast<double>(multiLoopSourceRateForIndex(a, count, sourceRateSpread));
        const double rateB = static_cast<double>(multiLoopSourceRateForIndex(b, count, sourceRateSpread));
        for (uint32_t frame = 0; frame < compositeFrames; ++frame) {
            const double t = static_cast<double>(frame) / compositeRate;
            const float va = multiLoopReadSourceSeam(sampleA, lane, t * sampleA.sampleRate * rateA);
            const float vb = multiLoopReadSourceSeam(sampleB, lane, t * sampleB.sampleRate * rateB);
            composite->audio[static_cast<size_t>(frame) * kLoopProcessorChannels + lane] = va + (vb - va) * blend;
        }
    }

    return composite;
}

} // namespace s3g
