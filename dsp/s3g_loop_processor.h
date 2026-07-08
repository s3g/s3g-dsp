#pragma once

#include "s3g_math.h"
#include "s3g_realtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace s3g {

constexpr uint32_t kLoopProcessorChannels = 8;

struct LoopProcessorSample {
    std::vector<float> audio;
    uint32_t frames = 0;
    uint32_t channels = 0;
    double sampleRate = 48000.0;
    std::string path;
};

struct LoopProcessorParams {
    float baseRate = 1.0f;
    float rateSpread = 0.08f;
    float driftAmount = 0.0f;
    float relationCenter = 0.5f;
    float relationGlideMs = 250.0f;
    float loopStart = 0.0f;
    float loopLength = 1.0f;
    float xfadePct = 0.08f;
    float seamDuck = 0.12f;
    float gainDb = -12.0f;
    uint32_t launchMode = 0u;
    uint32_t laneMask = 0xffu;
};

class LoopProcessorEngine {
public:
    void prepare(double sampleRate)
    {
        sampleRate_ = std::max(1.0, sampleRate);
        reset();
    }

    void reset()
    {
        for (uint32_t lane = 0; lane < kLoopProcessorChannels; ++lane) {
            const float u = static_cast<float>(lane) / static_cast<float>(kLoopProcessorChannels);
            lanes_[lane].pos = 0.0;
            lanes_[lane].rateOffset = u * 2.0f - 1.0f;
            lanes_[lane].phaseUnit = launchPhaseForLane(lane);
            lanes_[lane].driftPhase = 0.61803398875 * static_cast<double>(lane + 1u);
            lanes_[lane].driftHz = 0.019 + 0.007 * static_cast<double>((lane * 5u) % 7u);
            lanes_[lane].gain = 0.0f;
            lanes_[lane].period = 0.0;
            lanes_[lane].windowStart = 0.0;
            lanes_[lane].windowLength = 0.0;
            lanes_[lane].pendingWindowStart = 0.0;
            lanes_[lane].pendingWindowLength = 0.0;
            lanes_[lane].xfadeFrames = 0u;
            lanes_[lane].pendingXfadeFrames = 0u;
            lanes_[lane].windowInitialized = false;
        }
        xfadeFramesSmoothed_ = 0.0f;
        xfadeInitialized_ = false;
        relationSpreadSmoothed_ = params_.rateSpread;
        relationDriftSmoothed_ = params_.driftAmount;
        relationCenterSmoothed_ = params_.relationCenter;
    }

    void resync()
    {
        for (uint32_t lane = 0; lane < kLoopProcessorChannels; ++lane) {
            lanes_[lane].pos = 0.0;
            lanes_[lane].phaseUnit = 0.0f;
            lanes_[lane].driftPhase = 0.61803398875 * static_cast<double>(lane + 1u);
            lanes_[lane].windowInitialized = false;
        }
    }

    void setParams(const LoopProcessorParams& params)
    {
        params_ = params;
        params_.baseRate = clamp(params_.baseRate, 0.125f, 4.0f);
        params_.rateSpread = clamp(params_.rateSpread, -1.0f, 1.0f);
        params_.driftAmount = clamp(params_.driftAmount, -0.12f, 0.12f);
        params_.relationCenter = clamp(params_.relationCenter, 0.0f, 1.0f);
        params_.relationGlideMs = clamp(params_.relationGlideMs, 10.0f, 2000.0f);
        params_.loopStart = clamp(params_.loopStart, 0.0f, 0.999f);
        params_.loopLength = clamp(params_.loopLength, 0.01f, 1.0f);
        params_.xfadePct = clamp(params_.xfadePct, 0.0f, 0.3f);
        params_.seamDuck = clamp(params_.seamDuck, 0.0f, 0.75f);
        params_.gainDb = clamp(params_.gainDb, -60.0f, 6.0f);
        params_.launchMode = std::min<uint32_t>(params_.launchMode, 3u);
        params_.laneMask &= ((1u << kLoopProcessorChannels) - 1u);
    }

    void process(const std::shared_ptr<const LoopProcessorSample>& sample, float** outputs, uint32_t frames, bool playing = true)
    {
        if (!outputs || frames == 0) {
            return;
        }
        for (uint32_t ch = 0; ch < kLoopProcessorChannels; ++ch) {
            if (outputs[ch]) {
                std::fill(outputs[ch], outputs[ch] + frames, 0.0f);
            }
        }
        if (!sample || sample->frames < 2 || sample->channels == 0 || sample->audio.empty()) {
            return;
        }

        const uint32_t sourceFrames = sample->frames;
        const uint32_t sourceChannels = sample->channels;
        const double rateScale = sample->sampleRate / sampleRate_;
        updateRelationshipSmoothing(frames);
        const LoopRegion region = sanitizedRegion();
        const double fullPeriod = static_cast<double>(std::max<uint32_t>(2u, sourceFrames));
        const double requestedWindowStart = static_cast<double>(region.start) * static_cast<double>(std::max<uint32_t>(1u, sourceFrames - 1u));
        const double requestedWindowLength = std::clamp(
            static_cast<double>(sourceFrames) * static_cast<double>(region.length),
            2.0,
            fullPeriod);
        const LoopWindow targetWindow = zeroSnappedWindow(*sample, requestedWindowStart, requestedWindowLength, fullPeriod);
        const uint32_t targetXfadeFrames = xfadeFramesFor(targetWindow.length, params_.xfadePct);
        if (!xfadeInitialized_) {
            xfadeFramesSmoothed_ = static_cast<float>(targetXfadeFrames);
            xfadeInitialized_ = true;
        }
        const float xfadeCoeff = 1.0f - std::exp(-static_cast<float>(frames) / static_cast<float>(std::max(1.0, sampleRate_ * 0.350)));
        xfadeFramesSmoothed_ += (static_cast<float>(targetXfadeFrames) - xfadeFramesSmoothed_) * clamp(xfadeCoeff, 0.0005f, 0.10f);
        const float baseGain = dbToGain(params_.gainDb) / std::sqrt(std::max(1.0f, static_cast<float>(kLoopProcessorChannels) / 2.0f));

        for (uint32_t lane = 0; lane < kLoopProcessorChannels; ++lane) {
            if (!outputs[lane]) {
                continue;
            }
            auto& state = lanes_[lane];
            const float laneUnit = kLoopProcessorChannels > 1u
                ? static_cast<float>(lane) / static_cast<float>(kLoopProcessorChannels - 1u)
                : 0.5f;
            const float relation = clamp((laneUnit - relationCenterSmoothed_) * 2.0f, -1.0f, 1.0f);
            if (!state.windowInitialized) {
                state.windowStart = targetWindow.start;
                state.windowLength = targetWindow.length;
                state.pendingWindowStart = targetWindow.start;
                state.pendingWindowLength = targetWindow.length;
                state.xfadeFrames = static_cast<uint32_t>(std::max(0.0f, std::round(xfadeFramesSmoothed_)));
                state.pendingXfadeFrames = state.xfadeFrames;
                state.duck = params_.seamDuck;
                state.pendingDuck = state.duck;
                state.windowInitialized = true;
            } else {
                state.pendingWindowStart = targetWindow.start;
                state.pendingWindowLength = targetWindow.length;
                state.pendingXfadeFrames = static_cast<uint32_t>(std::max(0.0f, std::round(xfadeFramesSmoothed_)));
                state.pendingDuck = params_.seamDuck;
            }
            double period = std::clamp(state.windowLength, 2.0, fullPeriod);
            if (state.pos <= 0.0) {
                state.pos = state.phaseUnit * period;
            }
            state.period = period;
            const uint32_t srcCh = sourceChannelForLane(lane, sourceChannels);
            const bool laneEnabled = (params_.laneMask & (1u << lane)) != 0u;
            const float laneGainTarget = (playing && laneEnabled) ? baseGain : 0.0f;
            for (uint32_t i = 0; i < frames; ++i) {
                period = std::clamp(state.windowLength, 2.0, fullPeriod);
                state.period = period;
                const double drift = std::fabs(relationDriftSmoothed_) > 0.0001f
                    ? 1.0 + static_cast<double>(relationDriftSmoothed_) * std::sin(6.28318530718 * state.driftPhase)
                    : 1.0;
                const double rate = std::max(0.05, static_cast<double>(params_.baseRate * (1.0f + relation * relationSpreadSmoothed_))) * drift * rateScale;
                state.gain += (laneGainTarget - state.gain) * 0.0012f;
                const double normalLoopPos = wrapPosition(state.pos, period);
                const double windowStart = state.windowStart;
                const uint32_t activeXfadeFrames = std::min<uint32_t>(
                    static_cast<uint32_t>(std::max(2.0, period) / 2.0),
                    state.xfadeFrames);
                const double normalAbsolute = wrapPosition(windowStart + normalLoopPos, fullPeriod);
                const float value = readLoopSeam(*sample, srcCh, normalAbsolute, windowStart, period, fullPeriod, activeXfadeFrames, state.duck);
                outputs[lane][i] = flushDenormal(value * state.gain);
                if (playing) {
                    state.pos += rate;
                    if (state.pos >= period) {
                        state.pos -= period;
                        commitPendingWindow(state, fullPeriod);
                        period = std::clamp(state.windowLength, 2.0, fullPeriod);
                        state.period = period;
                    }
                    while (state.pos >= period) {
                        state.pos -= period;
                    }
                    state.driftPhase += state.driftHz / sampleRate_;
                    if (state.driftPhase >= 1.0) {
                        state.driftPhase -= std::floor(state.driftPhase);
                    }
                }
            }
        }
    }

    void lanePhases(float* phases, uint32_t count) const
    {
        if (!phases) {
            return;
        }
        const uint32_t n = std::min<uint32_t>(count, kLoopProcessorChannels);
        for (uint32_t lane = 0; lane < n; ++lane) {
            const auto& state = lanes_[lane];
            const double period = state.period > 1.0 ? state.period : 1.0;
            double phase = state.pos / period;
            phase -= std::floor(phase);
            phases[lane] = static_cast<float>(phase);
        }
        for (uint32_t lane = n; lane < count; ++lane) {
            phases[lane] = 0.0f;
        }
    }

private:
    struct LaneState {
        double pos = 0.0;
        float rateOffset = 0.0f;
        float phaseUnit = 0.0f;
        double driftPhase = 0.0;
        double driftHz = 0.02;
        double period = 0.0;
        float gain = 0.0f;
        double windowStart = 0.0;
        double windowLength = 0.0;
        double pendingWindowStart = 0.0;
        double pendingWindowLength = 0.0;
        uint32_t xfadeFrames = 0u;
        uint32_t pendingXfadeFrames = 0u;
        float duck = 0.0f;
        float pendingDuck = 0.0f;
        bool windowInitialized = false;
    };

    static uint32_t xfadeFramesFor(double loopLengthFrames, float xfadePct)
    {
        const double safeLength = std::max(2.0, loopLengthFrames);
        const uint32_t maxXfade = static_cast<uint32_t>(std::max(0.0, (safeLength - 2.0) * 0.5));
        return std::min<uint32_t>(
            maxXfade,
            static_cast<uint32_t>(std::max(0.0, std::round(safeLength * static_cast<double>(clamp(xfadePct, 0.0f, 0.3f))))));
    }

    static double wrapPosition(double pos, double period)
    {
        if (period <= 1.0) {
            return 0.0;
        }
        pos -= std::floor(pos / period) * period;
        return pos < 0.0 ? pos + period : pos;
    }

    static float readLinear(const LoopProcessorSample& sample, uint32_t ch, double pos)
    {
        if (sample.frames < 2 || sample.channels == 0) {
            return 0.0f;
        }
        const double wrapped = pos - std::floor(pos / static_cast<double>(sample.frames)) * static_cast<double>(sample.frames);
        const uint32_t i0 = static_cast<uint32_t>(wrapped) % sample.frames;
        const uint32_t i1 = (i0 + 1u) % sample.frames;
        const float frac = static_cast<float>(wrapped - std::floor(wrapped));
        const uint32_t srcCh = std::min<uint32_t>(ch, sample.channels - 1u);
        const float a = sample.audio[static_cast<size_t>(i0) * sample.channels + srcCh];
        const float b = sample.audio[static_cast<size_t>(i1) * sample.channels + srcCh];
        return a + (b - a) * frac;
    }

    static uint32_t sourceChannelForLane(uint32_t lane, uint32_t sourceChannels)
    {
        if (sourceChannels == 0u) {
            return 0u;
        }
        return lane % sourceChannels;
    }

    static float readSeam(const LoopProcessorSample& sample, uint32_t ch, double pos, uint32_t xfadeFrames, float seamDuck)
    {
        if (xfadeFrames <= 1u || sample.frames < 4u) {
            return readLinear(sample, ch, pos);
        }
        const double period = static_cast<double>(std::max<uint32_t>(2u, sample.frames));
        double phase = pos - std::floor(pos / period) * period;
        float value = readLinear(sample, ch, phase);
        if (phase >= period - static_cast<double>(xfadeFrames)) {
            const double seamPhase = phase - (period - static_cast<double>(xfadeFrames));
            const float u = static_cast<float>(seamPhase / std::max(1.0, static_cast<double>(xfadeFrames)));
            const float fadeIn = 0.5f - 0.5f * std::cos(3.14159265359f * u);
            const float fadeOut = 1.0f - fadeIn;
            const float duck = 1.0f - clamp(seamDuck, 0.0f, 0.75f) * std::sin(3.14159265359f * u);
            const float tail = readLinear(sample, ch, phase);
            const float head = readLinear(sample, ch, seamPhase - static_cast<double>(xfadeFrames));
            value = (tail * fadeOut + head * fadeIn) * duck;
        }
        return value;
    }

    static float readLoopSeam(const LoopProcessorSample& sample,
                              uint32_t ch,
                              double absolutePos,
                              double loopStart,
                              double loopLength,
                              double fullPeriod,
                              uint32_t xfadeFrames,
                              float seamDuck)
    {
        if (loopLength >= fullPeriod - 1.0) {
            return readSeam(sample, ch, absolutePos, xfadeFrames, seamDuck);
        }
        const uint32_t fadeFrames = std::min<uint32_t>(
            static_cast<uint32_t>(std::max(1.0, loopLength * 0.5)),
            std::max<uint32_t>(1u, xfadeFrames));
        if (fadeFrames <= 1u || sample.frames < 4u) {
            return readLinear(sample, ch, absolutePos);
        }

        const double loopPhase = wrapPosition(absolutePos - loopStart, fullPeriod);
        if (loopPhase < loopLength - static_cast<double>(fadeFrames)) {
            return readLinear(sample, ch, absolutePos);
        }

        const double seamPhase = loopPhase - (loopLength - static_cast<double>(fadeFrames));
        const float u = static_cast<float>(seamPhase / std::max(1.0, static_cast<double>(fadeFrames)));
        const float fadeIn = 0.5f - 0.5f * std::cos(3.14159265359f * clamp(u, 0.0f, 1.0f));
        const float fadeOut = 1.0f - fadeIn;
        const float duck = 1.0f - clamp(seamDuck, 0.0f, 0.75f) * std::sin(3.14159265359f * clamp(u, 0.0f, 1.0f));
        const float tail = readLinear(sample, ch, absolutePos);
        const float head = readLinear(sample, ch, wrapPosition(loopStart + seamPhase - static_cast<double>(fadeFrames), fullPeriod));
        return (tail * fadeOut + head * fadeIn) * duck;
    }

    float launchPhaseForLane(uint32_t lane) const
    {
        (void)lane;
        return 0.0f;
#if 0
        const float u = static_cast<float>(lane) / static_cast<float>(kLoopProcessorChannels);
        switch (params_.launchMode) {
        case 0u: return 0.0f;
        case 2u: return hashUnit(lane * 1103515245u + 12345u);
        case 3u: return wrapUnit(u + 0.08f * std::sin(static_cast<float>(lane + 1u) * 2.39996323f));
        default: return u;
        }
#endif
    }

    static float wrapUnit(float v)
    {
        v -= std::floor(v);
        return v < 0.0f ? v + 1.0f : v;
    }

    static float hashUnit(uint32_t x)
    {
        x ^= x >> 16;
        x *= 0x7feb352du;
        x ^= x >> 15;
        x *= 0x846ca68bu;
        x ^= x >> 16;
        return static_cast<float>(x & 0x00ffffffu) / static_cast<float>(0x01000000u);
    }

    void updateRelationshipSmoothing(uint32_t frames)
    {
        const float seconds = params_.relationGlideMs * 0.001f;
        const float coeff = 1.0f - std::exp(-static_cast<float>(frames) / static_cast<float>(std::max(1.0, sampleRate_ * seconds)));
        const float a = clamp(coeff, 0.0001f, 0.20f);
        relationSpreadSmoothed_ += (params_.rateSpread - relationSpreadSmoothed_) * a;
        relationDriftSmoothed_ += (params_.driftAmount - relationDriftSmoothed_) * a;
        relationCenterSmoothed_ += (params_.relationCenter - relationCenterSmoothed_) * a;
    }

    static void commitPendingWindow(LaneState& state, double fullPeriod)
    {
        state.windowStart = wrapPosition(state.pendingWindowStart, fullPeriod);
        state.windowLength = std::clamp(state.pendingWindowLength, 2.0, fullPeriod);
        state.xfadeFrames = state.pendingXfadeFrames;
        state.duck = state.pendingDuck;
    }

    struct LoopRegion {
        float start = 0.0f;
        float length = 1.0f;
    };

    struct LoopWindow {
        double start = 0.0;
        double length = 2.0;
    };

    LoopRegion sanitizedRegion() const
    {
        return LoopRegion {
            clamp(params_.loopStart, 0.0f, 0.999f),
            clamp(params_.loopLength, 0.01f, 1.0f)
        };
    }

    LoopWindow zeroSnappedWindow(const LoopProcessorSample& sample,
                                 double requestedStart,
                                 double requestedLength,
                                 double fullPeriod) const
    {
        if (sample.frames < 16u || requestedLength >= fullPeriod - 1.0) {
            return LoopWindow { wrapPosition(requestedStart, fullPeriod), std::clamp(requestedLength, 2.0, fullPeriod) };
        }

        const uint32_t searchRadius = std::clamp<uint32_t>(
            static_cast<uint32_t>(std::round(sample.sampleRate * 0.012)),
            16u,
            std::min<uint32_t>(2048u, std::max<uint32_t>(16u, sample.frames / 12u)));
        const double snappedStart = findLowEnergyCrossing(sample, requestedStart, searchRadius);
        const double requestedEnd = wrapPosition(requestedStart + requestedLength, fullPeriod);
        const double snappedEnd = findLowEnergyCrossing(sample, requestedEnd, searchRadius);
        double snappedLength = wrapPosition(snappedEnd - snappedStart, fullPeriod);
        if (snappedLength < 8.0 || snappedLength > fullPeriod - 1.0) {
            snappedLength = std::clamp(requestedLength, 2.0, fullPeriod);
        }
        return LoopWindow { snappedStart, snappedLength };
    }

    static double findLowEnergyCrossing(const LoopProcessorSample& sample, double framePos, uint32_t radius)
    {
        if (sample.frames < 2u || sample.channels == 0u) {
            return 0.0;
        }

        const int64_t center = static_cast<int64_t>(std::llround(framePos));
        const int64_t r = static_cast<int64_t>(radius);
        uint32_t bestFrame = static_cast<uint32_t>(wrapIndex(center, sample.frames));
        float bestScore = std::numeric_limits<float>::max();

        for (int64_t offset = -r; offset <= r; ++offset) {
            const uint32_t idx = static_cast<uint32_t>(wrapIndex(center + offset, sample.frames));
            const uint32_t prev = idx == 0u ? sample.frames - 1u : idx - 1u;
            float absSum = 0.0f;
            float deltaSum = 0.0f;
            float absMax = 0.0f;
            float deltaMax = 0.0f;
            float crossBonus = 0.0f;
            for (uint32_t ch = 0; ch < sample.channels; ++ch) {
                const float a = sample.audio[static_cast<size_t>(prev) * sample.channels + ch];
                const float b = sample.audio[static_cast<size_t>(idx) * sample.channels + ch];
                const float absB = std::fabs(b);
                const float delta = std::fabs(b - a);
                absSum += absB;
                deltaSum += delta;
                absMax = std::max(absMax, absB);
                deltaMax = std::max(deltaMax, delta);
                if ((a <= 0.0f && b >= 0.0f) || (a >= 0.0f && b <= 0.0f)) {
                    crossBonus += 1.0f;
                }
            }
            const float channelScale = 1.0f / static_cast<float>(sample.channels);
            const float distance = static_cast<float>(std::abs(offset)) / static_cast<float>(std::max<int64_t>(1, r));
            const float avgAbs = absSum * channelScale;
            const float avgDelta = deltaSum * channelScale;
            const float crossRatio = crossBonus * channelScale;
            const float score = absMax * 0.70f
                + avgAbs * 0.30f
                + deltaMax * 0.10f
                + avgDelta * 0.05f
                + distance * 0.003f
                - crossRatio * 0.002f;
            if (score < bestScore) {
                bestScore = score;
                bestFrame = idx;
            }
        }
        return static_cast<double>(bestFrame);
    }

    static int64_t wrapIndex(int64_t idx, uint32_t frames)
    {
        const int64_t period = static_cast<int64_t>(std::max<uint32_t>(1u, frames));
        idx %= period;
        return idx < 0 ? idx + period : idx;
    }

    double sampleRate_ = 48000.0;
    LoopProcessorParams params_ {};
    float xfadeFramesSmoothed_ = 0.0f;
    float relationSpreadSmoothed_ = 0.08f;
    float relationDriftSmoothed_ = 0.0f;
    float relationCenterSmoothed_ = 0.5f;
    bool xfadeInitialized_ = false;
    std::array<LaneState, kLoopProcessorChannels> lanes_ {};
};

} // namespace s3g
