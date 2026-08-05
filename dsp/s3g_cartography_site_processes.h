#pragma once

#include "s3g_math.h"
#include "s3g_realtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace s3g {

constexpr uint32_t kCartographySiteProcessLanes = 24u;

struct CartographySiteProcessParams {
    float macro = 0.50f;
    float color = 0.55f;
    float memory = 0.28f;
    float spread = 0.35f;
    float deviation = 0.12f;
    float skew = 0.0f;
    float center = 0.50f;
};

inline CartographySiteProcessParams sanitizeCartographySiteProcessParams(
    CartographySiteProcessParams params)
{
    const auto unit = [](float value, float fallback) {
        return clamp(std::isfinite(value) ? value : fallback, 0.0f, 1.0f);
    };
    params.macro = unit(params.macro, 0.50f);
    params.color = unit(params.color, 0.55f);
    params.memory = unit(params.memory, 0.28f);
    params.spread = unit(params.spread, 0.35f);
    params.deviation = unit(params.deviation, 0.12f);
    params.skew = clamp(
        std::isfinite(params.skew) ? params.skew : 0.0f, -1.0f, 1.0f);
    params.center = unit(params.center, 0.50f);
    return params;
}

namespace cartography_site_process_detail {

inline double sanitizeSampleRate(double sampleRate)
{
    return std::clamp(
        std::isfinite(sampleRate) ? sampleRate : 48000.0,
        1000.0, 768000.0);
}

inline float finiteInput(float sample)
{
    return std::isfinite(sample) ? sample : 0.0f;
}

inline float boundedOutput(float sample)
{
    return std::isfinite(sample) ? clamp(sample, -8.0f, 8.0f) : 0.0f;
}

// Close bounded approximation of tanh for the speaker nonlinearity. The
// maximum error over [-3, 3] is small, while avoiding dozens of libm calls per
// sample when all 24 sites are active.
inline float speakerSaturate(float sample)
{
    const float x = clamp(std::isfinite(sample) ? sample : 0.0f,
        -3.0f, 3.0f);
    const float square = x * x;
    if (square >= 9.0f) return std::copysign(1.0f, x);
    return x * (27.0f + square) / (27.0f + 9.0f * square);
}

inline float onePoleFrequency(float frequencyHz, float sampleRate)
{
    const float frequency = clamp(frequencyHz, 1.0f, sampleRate * 0.45f);
    return 1.0f - std::exp(-2.0f * kPi * frequency / sampleRate);
}

inline float onePoleTime(float seconds, float sampleRate)
{
    return 1.0f - std::exp(-1.0f
        / std::max(1.0f, seconds * sampleRate));
}

inline float smoothStep(float value)
{
    const float x = clamp(value, 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}

inline float laneUnit(uint32_t lane, uint32_t lanes)
{
    return lanes > 1u
        ? static_cast<float>(lane) / static_cast<float>(lanes - 1u)
        : 0.5f;
}

inline float laneCentered(float unit, float center, uint32_t lanes)
{
    return lanes > 1u
        ? clamp((unit - center) * 2.0f, -1.0f, 1.0f)
        : 0.0f;
}

inline float laneRandom(uint32_t lane, uint32_t salt)
{
    uint32_t value = lane * 747796405u + 2891336453u + salt * 277803737u;
    value = ((value >> ((value >> 28u) + 4u)) ^ value) * 277803737u;
    value = (value >> 22u) ^ value;
    return static_cast<float>(value & 0xffffu) / 32767.5f - 1.0f;
}

class ParameterSmoother {
public:
    void prepare(double sampleRate)
    {
        const double rate = sanitizeSampleRate(sampleRate);
        coefficient_ = 1.0f - std::exp(-1.0f
            / static_cast<float>(rate * 0.020));
        reset();
    }

    void reset() { current_ = target_; }

    void setTarget(CartographySiteProcessParams params)
    {
        target_ = sanitizeCartographySiteProcessParams(params);
    }

    const CartographySiteProcessParams& target() const { return target_; }

    const CartographySiteProcessParams& advance()
    {
        smooth(current_.macro, target_.macro);
        smooth(current_.color, target_.color);
        smooth(current_.memory, target_.memory);
        smooth(current_.spread, target_.spread);
        smooth(current_.deviation, target_.deviation);
        smooth(current_.skew, target_.skew);
        smooth(current_.center, target_.center);
        return current_;
    }

private:
    void smooth(float& value, float target) const
    {
        value += (target - value) * coefficient_;
    }

    CartographySiteProcessParams target_ {};
    CartographySiteProcessParams current_ {};
    float coefficient_ = 1.0f;
};

struct SvfOutputs {
    float low = 0.0f;
    // The band output includes damping, so low + band + high reconstructs
    // the input. That makes band replacement safe at zero process depth.
    float band = 0.0f;
    float high = 0.0f;
};

struct CachedSvf {
    float integrator1 = 0.0f;
    float integrator2 = 0.0f;
    float a1 = 1.0f;
    float a2 = 0.0f;
    float a3 = 0.0f;
    float damping = 1.41421356f;
    float lastCutoffHz = 0.0f;
    float lastResonance = 0.0f;
    uint32_t coefficientPhase = 0u;

    void reset()
    {
        integrator1 = 0.0f;
        integrator2 = 0.0f;
        a1 = 1.0f;
        a2 = 0.0f;
        a3 = 0.0f;
        damping = 1.41421356f;
        lastCutoffHz = 0.0f;
        lastResonance = 0.0f;
        coefficientPhase = 0u;
    }

    bool healthy() const
    {
        return std::isfinite(integrator1) && std::isfinite(integrator2)
            && std::isfinite(a1) && std::isfinite(a2)
            && std::isfinite(a3) && std::isfinite(damping)
            && std::fabs(integrator1) < 64.0f
            && std::fabs(integrator2) < 64.0f;
    }

    SvfOutputs process(float input, float cutoffHz, float resonance,
        float sampleRate)
    {
        if (!std::isfinite(input) || !std::isfinite(cutoffHz)
            || !std::isfinite(resonance) || !healthy()) {
            reset();
            return {};
        }

        const float hz = clamp(cutoffHz, 8.0f, sampleRate * 0.45f);
        const float boundedResonance = clamp(resonance, 0.0f, 0.92f);
        const float cutoffThreshold = std::max(10.0f, lastCutoffHz * 0.02f);
        const bool refresh = coefficientPhase == 0u || lastCutoffHz <= 0.0f
            || std::fabs(hz - lastCutoffHz) > cutoffThreshold
            || std::fabs(boundedResonance - lastResonance) > 0.02f;
        coefficientPhase = (coefficientPhase + 1u) & 15u;
        if (refresh) {
            const float g = std::tan(kPi * hz / sampleRate);
            damping = 2.0f - boundedResonance * 1.82f;
            a1 = 1.0f / (1.0f + g * (g + damping));
            a2 = g * a1;
            a3 = g * a2;
            lastCutoffHz = hz;
            lastResonance = boundedResonance;
        }

        const float v3 = input - integrator2;
        const float v1 = a1 * integrator1 + a2 * v3;
        const float v2 = integrator2 + a2 * integrator1 + a3 * v3;
        integrator1 = flushDenormal(2.0f * v1 - integrator1);
        integrator2 = flushDenormal(2.0f * v2 - integrator2);
        const float normalizedBand = damping * v1;
        const float high = input - normalizedBand - v2;
        if (!healthy() || !std::isfinite(high)) {
            reset();
            return {};
        }
        return { flushDenormal(v2), flushDenormal(normalizedBand),
            flushDenormal(high) };
    }
};

} // namespace cartography_site_process_detail

// A compact loudspeaker path: drive enters a horn/cabinet band limit, excites
// a lane-voiced body resonance, and heats a slow compressor. The thermal state
// only attenuates; all nonlinear stages are bounded.
class CartographySpeakerBody {
public:
    void prepare(double sampleRate, uint32_t channels)
    {
        sampleRate_ = cartography_site_process_detail::sanitizeSampleRate(
            sampleRate);
        channels_ = std::min<uint32_t>(
            channels, kCartographySiteProcessLanes);
        lanePositionCoefficient_ =
            cartography_site_process_detail::onePoleTime(
                0.030f, static_cast<float>(sampleRate_));
        smoother_.prepare(sampleRate_);
        reset();
    }

    void reset()
    {
        smoother_.reset();
        lanes_ = {};
        controlPhase_ = 0u;
    }

    void setParams(CartographySiteProcessParams params)
    {
        smoother_.setTarget(params);
    }

    void setActiveChannels(uint32_t channels)
    {
        const uint32_t active = std::min<uint32_t>(
            channels, kCartographySiteProcessLanes);
        if (active == channels_) return;
        if (active > channels_) {
            for (uint32_t lane = channels_; lane < active; ++lane) {
                lanes_[lane] = {};
            }
        } else {
            for (uint32_t lane = active; lane < channels_; ++lane) {
                lanes_[lane] = {};
            }
        }
        channels_ = active;
        controlPhase_ = 0u;
    }

    CartographySiteProcessParams params() const { return smoother_.target(); }

    void processFrame(const float* input, float* output,
        const float* laneUnits = nullptr)
    {
        if (!output || channels_ == 0u) return;
        using namespace cartography_site_process_detail;
        const auto& params = smoother_.advance();
        const float sampleRate = static_cast<float>(sampleRate_);
        const bool refreshDerived = controlPhase_ == 0u;
        controlPhase_ = (controlPhase_ + 1u) & 7u;
        if (refreshDerived) {
            envelopeAttack_ = onePoleTime(
                0.003f + (1.0f - params.color) * 0.010f, sampleRate);
            envelopeRelease_ = onePoleTime(
                0.080f + params.memory * 0.420f, sampleRate);
            heatAttack_ = onePoleTime(
                0.030f + (1.0f - params.macro) * 0.120f, sampleRate);
            heatRelease_ = onePoleTime(
                0.180f + params.memory * 1.820f, sampleRate);
        }

        for (uint32_t lane = 0u; lane < channels_; ++lane) {
            auto& state = lanes_[lane];
            const float source = finiteInput(input ? input[lane] : 0.0f);
            const float targetUnit = laneUnits
                ? clamp(laneUnits[lane], 0.0f, 1.0f)
                : laneUnit(lane, channels_);
            if (!state.laneUnitPrimed) {
                state.smoothedLaneUnit = targetUnit;
                state.laneUnitPrimed = true;
            } else {
                state.smoothedLaneUnit = flushDenormal(
                    state.smoothedLaneUnit
                        + (targetUnit - state.smoothedLaneUnit)
                            * lanePositionCoefficient_);
            }
            if (refreshDerived) {
                const float unit = state.smoothedLaneUnit;
                const float centered = laneCentered(
                    unit, params.center, channels_);
                const float random = laneRandom(lane, 0x424f4459u);
                const float octaveShift = centered * params.spread * 1.20f
                    + random * params.deviation * 0.72f
                    + params.skew * (unit - 0.5f) * 0.90f;
                state.lowCutoff = clamp(
                    52.0f * std::pow(2.0f,
                        params.color * 1.55f + octaveShift * 0.12f),
                    32.0f, sampleRate * 0.18f);
                const float highCutoffLimit = sampleRate * 0.43f;
                state.highCutoff = clamp(
                    2800.0f * std::pow(2.0f,
                        params.color * 1.95f + octaveShift * 0.34f),
                    std::min(900.0f, highCutoffLimit), highCutoffLimit);
                state.bodyCutoff = clamp(
                    145.0f * std::pow(2.0f,
                        params.color * 2.65f + octaveShift),
                    70.0f, sampleRate * 0.32f);
                state.bodyResonance = clamp(0.18f
                    + params.memory * 0.56f + params.macro * 0.10f,
                    0.0f, 0.88f);
            }
            updateBandLimit(state, state.lowCutoff,
                state.highCutoff, sampleRate);

            const float drive = 1.0f + params.macro
                * (2.2f + params.color * 2.8f);
            const float driven = speakerSaturate(source * drive);
            state.highpassLow = flushDenormal(state.highpassLow
                + (driven - state.highpassLow)
                    * state.highpassCoefficient);
            const float highpassed = driven - state.highpassLow;
            state.lowpass1 = flushDenormal(state.lowpass1
                + (highpassed - state.lowpass1)
                    * state.lowpassCoefficient);
            state.lowpass2 = flushDenormal(state.lowpass2
                + (state.lowpass1 - state.lowpass2)
                    * state.lowpassCoefficient);
            const float cabinet = flushDenormal(state.lowpass2);

            const auto body = state.body.process(
                cabinet, state.bodyCutoff,
                state.bodyResonance, sampleRate);
            const float voiced = cabinet
                + body.band * (0.22f + params.macro * 1.08f);

            const float magnitude = std::fabs(voiced);
            state.envelope = flushDenormal(state.envelope
                + (magnitude - state.envelope)
                * (magnitude > state.envelope
                    ? envelopeAttack_ : envelopeRelease_));
            const float heatThreshold = 0.26f
                - params.macro * 0.13f + params.color * 0.025f;
            const float heatTarget = std::max(
                0.0f, state.envelope - heatThreshold);
            state.heat += (heatTarget - state.heat)
                * (heatTarget > state.heat ? heatAttack_ : heatRelease_);
            state.heat = flushDenormal(clamp(state.heat, 0.0f, 4.0f));
            const float thermalGain = 1.0f / (1.0f + state.heat
                * (2.0f + params.macro * 8.0f + params.memory * 2.0f));
            const float postDrive = 1.0f + params.macro * 1.65f;
            const float modeled = speakerSaturate(
                voiced * thermalGain * postDrive)
                * (1.0f - params.macro * 0.12f);
            output[lane] = boundedOutput(
                source + params.macro * (modeled - source));
        }
    }

private:
    struct LaneState {
        cartography_site_process_detail::CachedSvf body {};
        float highpassLow = 0.0f;
        float lowpass1 = 0.0f;
        float lowpass2 = 0.0f;
        float envelope = 0.0f;
        float heat = 0.0f;
        float highpassCoefficient = 0.01f;
        float lowpassCoefficient = 0.5f;
        float lastLowCutoff = 0.0f;
        float lastHighCutoff = 0.0f;
        float lowCutoff = 52.0f;
        float highCutoff = 2800.0f;
        float bodyCutoff = 145.0f;
        float bodyResonance = 0.18f;
        float smoothedLaneUnit = 0.5f;
        bool laneUnitPrimed = false;
        uint32_t coefficientPhase = 0u;
    };

    static void updateBandLimit(LaneState& state, float lowCutoff,
        float highCutoff, float sampleRate)
    {
        using namespace cartography_site_process_detail;
        const bool refresh = state.coefficientPhase == 0u
            || state.lastLowCutoff <= 0.0f
            || std::fabs(lowCutoff - state.lastLowCutoff)
                > std::max(4.0f, state.lastLowCutoff * 0.02f)
            || std::fabs(highCutoff - state.lastHighCutoff)
                > std::max(12.0f, state.lastHighCutoff * 0.02f);
        state.coefficientPhase = (state.coefficientPhase + 1u) & 15u;
        if (!refresh) return;
        state.highpassCoefficient = onePoleFrequency(lowCutoff, sampleRate);
        state.lowpassCoefficient = onePoleFrequency(highCutoff, sampleRate);
        state.lastLowCutoff = lowCutoff;
        state.lastHighCutoff = highCutoff;
    }

    double sampleRate_ = 48000.0;
    uint32_t channels_ = 0u;
    uint32_t controlPhase_ = 0u;
    float lanePositionCoefficient_ = 1.0f;
    float envelopeAttack_ = 1.0f;
    float envelopeRelease_ = 1.0f;
    float heatAttack_ = 1.0f;
    float heatRelease_ = 1.0f;
    cartography_site_process_detail::ParameterSmoother smoother_ {};
    std::array<LaneState, kCartographySiteProcessLanes> lanes_ {};
};

// Each lane is decomposed into complementary low/band/high components. The
// normalized band is replaced by bands from relationship-selected neighbors;
// a sub-unity recurrence gives MEMORY a spectral trail without an FFT or an
// unbounded feedback path.
class CartographySpectralRelay {
public:
    void prepare(double sampleRate, uint32_t channels)
    {
        sampleRate_ = cartography_site_process_detail::sanitizeSampleRate(
            sampleRate);
        channels_ = std::min<uint32_t>(
            channels, kCartographySiteProcessLanes);
        smoother_.prepare(sampleRate_);
        reset();
    }

    void reset()
    {
        smoother_.reset();
        filters_ = {};
        memory_.fill(0.0f);
    }

    void setParams(CartographySiteProcessParams params)
    {
        smoother_.setTarget(params);
    }

    void setActiveChannels(uint32_t channels)
    {
        const uint32_t active = std::min<uint32_t>(
            channels, kCartographySiteProcessLanes);
        if (active == channels_) return;
        if (active > channels_) {
            for (uint32_t lane = channels_; lane < active; ++lane) {
                filters_[lane] = {};
                memory_[lane] = 0.0f;
            }
        } else {
            for (uint32_t lane = active; lane < channels_; ++lane) {
                filters_[lane] = {};
                memory_[lane] = 0.0f;
            }
        }
        channels_ = active;
    }

    CartographySiteProcessParams params() const { return smoother_.target(); }

    void processFrame(const float* input, float* output)
    {
        if (!output || channels_ == 0u) return;
        using namespace cartography_site_process_detail;
        const auto& params = smoother_.advance();
        const float sampleRate = static_cast<float>(sampleRate_);
        std::array<float, kCartographySiteProcessLanes> source {};
        std::array<float, kCartographySiteProcessLanes> low {};
        std::array<float, kCartographySiteProcessLanes> band {};
        std::array<float, kCartographySiteProcessLanes> nextMemory {};

        const float baseCutoff = 70.0f * std::pow(128.0f, params.color);
        const float recurrence = params.memory * params.memory * 0.48f;
        for (uint32_t lane = 0u; lane < channels_; ++lane) {
            source[lane] = finiteInput(input ? input[lane] : 0.0f);
            const float unit = laneUnit(lane, channels_);
            const float centered = laneCentered(
                unit, params.center, channels_);
            const float random = laneRandom(lane, 0x53504543u);
            const float octaveShift = centered * params.spread * 2.45f
                + random * params.deviation * 1.35f
                + params.skew * (unit - 0.5f) * 1.65f;
            const float cutoff = clamp(
                baseCutoff * std::pow(2.0f, octaveShift),
                24.0f, sampleRate * 0.42f);
            const float resonance = clamp(0.10f
                + params.memory * 0.44f + params.spread * 0.18f,
                0.0f, 0.84f);
            const auto components = filters_[lane].process(
                source[lane], cutoff, resonance, sampleRate);
            low[lane] = components.low;
            band[lane] = components.band;
            nextMemory[lane] = flushDenormal(clamp(
                band[lane] + memory_[lane] * recurrence, -2.0f, 2.0f));
        }

        const float direction = params.skew * 0.5f + 0.5f;
        for (uint32_t lane = 0u; lane < channels_; ++lane) {
            const uint32_t distance = relayDistance(lane, params);
            const uint32_t forward = (lane + distance) % channels_;
            const uint32_t backward = (lane + channels_ - distance) % channels_;
            const float relayedBand = lerp(
                nextMemory[backward], nextMemory[forward], direction);
            const float relayedLow = lerp(
                low[backward], low[forward], direction);
            const float bandReplacement = relayedBand - band[lane];
            const float lowReplacement = relayedLow - low[lane];
            const float value = source[lane]
                + params.macro * bandReplacement
                + params.macro * params.spread * 0.18f * lowReplacement;
            output[lane] = boundedOutput(value);
        }
        memory_ = nextMemory;
    }

private:
    uint32_t relayDistance(uint32_t lane,
        const CartographySiteProcessParams& params) const
    {
        if (channels_ <= 1u) return 0u;
        const float maximum = static_cast<float>(channels_ - 1u);
        const float base = 1.0f
            + params.spread * static_cast<float>(channels_ - 2u);
        const float jitter = cartography_site_process_detail::laneRandom(
            lane, 0x52454c59u) * params.deviation * maximum * 0.28f;
        const long rounded = std::lround(base + jitter);
        return static_cast<uint32_t>(std::clamp<long>(
            rounded, 1l, static_cast<long>(channels_ - 1u)));
    }

    double sampleRate_ = 48000.0;
    uint32_t channels_ = 0u;
    cartography_site_process_detail::ParameterSmoother smoother_ {};
    std::array<cartography_site_process_detail::CachedSvf,
        kCartographySiteProcessLanes> filters_ {};
    std::array<float, kCartographySiteProcessLanes> memory_ {};
};

// A per-destination circular recorder alternates between capturing a source
// lane and replaying the frozen window. Repeat edges return briefly to the live
// lane through a smoothstep window, avoiding clicks without filtering the body
// of the captured sound.
class CartographyRelayBuffer {
public:
    static constexpr float kMaximumCaptureSeconds = 1.20f;

    void prepare(double sampleRate, uint32_t channels)
    {
        sampleRate_ = cartography_site_process_detail::sanitizeSampleRate(
            sampleRate);
        channels_ = std::min<uint32_t>(
            channels, kCartographySiteProcessLanes);
        smoother_.prepare(sampleRate_);
        laneStride_ = std::max<uint32_t>(64u,
            static_cast<uint32_t>(std::ceil(
                sampleRate_ * static_cast<double>(kMaximumCaptureSeconds)))
                + 8u);
        buffer_.assign(
            static_cast<size_t>(laneStride_)
                * kCartographySiteProcessLanes,
            0.0f);
        prepared_ = true;
        reset();
    }

    void reset()
    {
        smoother_.reset();
        states_ = {};
        hasProcessed_ = false;
        if (!prepared_ || laneStride_ < 8u) return;
        const auto params = smoother_.target();
        for (uint32_t lane = 0u; lane < channels_; ++lane) {
            configureCapture(lane, states_[lane], params);
        }
    }

    void setParams(CartographySiteProcessParams params)
    {
        smoother_.setTarget(params);
    }

    void setActiveChannels(uint32_t channels)
    {
        const uint32_t active = std::min<uint32_t>(
            channels, kCartographySiteProcessLanes);
        if (active == channels_) return;
        const uint32_t previous = channels_;
        channels_ = active;
        if (!prepared_ || laneStride_ < 8u) return;
        const auto params = smoother_.target();
        if (!hasProcessed_) {
            states_ = {};
            for (uint32_t lane = 0u; lane < channels_; ++lane) {
                configureCapture(lane, states_[lane], params);
            }
        } else if (active > previous) {
            for (uint32_t lane = previous; lane < active; ++lane) {
                states_[lane] = {};
                configureCapture(lane, states_[lane], params);
            }
        } else {
            for (uint32_t lane = active; lane < previous; ++lane) {
                states_[lane] = {};
            }
        }
    }

    CartographySiteProcessParams params() const { return smoother_.target(); }

    void processFrame(const float* input, float* output)
    {
        if (!output || channels_ == 0u) return;
        using namespace cartography_site_process_detail;
        const auto& params = smoother_.advance();
        std::array<float, kCartographySiteProcessLanes> frame {};
        for (uint32_t lane = 0u; lane < channels_; ++lane) {
            frame[lane] = finiteInput(input ? input[lane] : 0.0f);
        }
        if (!prepared_ || buffer_.empty() || laneStride_ < 8u) {
            for (uint32_t lane = 0u; lane < channels_; ++lane) {
                output[lane] = frame[lane];
            }
            return;
        }

        for (uint32_t lane = 0u; lane < channels_; ++lane) {
            auto& state = states_[lane];
            const float live = frame[lane];
            float value = live;
            if (!state.repeating) {
                const size_t write = bufferIndex(lane, state.writePosition);
                buffer_[write] = frame[state.sourceLane % channels_];
                state.writePosition = (state.writePosition + 1u) % laneStride_;
                ++state.capturedSamples;
                if (state.capturedSamples >= state.windowSamples) {
                    state.segmentEnd = state.writePosition;
                    state.repeating = true;
                    state.playPosition = 0u;
                    state.completedLoops = 0u;
                    state.loopGain = 1.0f;
                }
            } else {
                const uint32_t position = std::min(
                    state.playPosition, state.windowSamples - 1u);
                const uint32_t start = (state.segmentEnd + laneStride_
                    - state.windowSamples) % laneStride_;
                const uint32_t relative = state.reverse
                    ? state.windowSamples - 1u - position : position;
                const uint32_t readPosition = (start + relative) % laneStride_;
                const float repeated = buffer_[bufferIndex(lane, readPosition)]
                    * state.loopGain;
                const uint32_t edgeSamples = std::min<uint32_t>(
                    std::max<uint32_t>(1u,
                        static_cast<uint32_t>(sampleRate_ * 0.008)),
                    std::max<uint32_t>(1u, state.windowSamples / 4u));
                const float edgeIn = static_cast<float>(position)
                    / static_cast<float>(edgeSamples);
                const float edgeOut = static_cast<float>(
                    state.windowSamples - 1u - position)
                    / static_cast<float>(edgeSamples);
                const float window = smoothStep(std::min(edgeIn, edgeOut));
                const float depth = params.macro * window;
                value = live + (repeated - live) * depth;

                ++state.playPosition;
                if (state.playPosition >= state.windowSamples) {
                    state.playPosition = 0u;
                    ++state.completedLoops;
                    state.loopGain *= 0.72f + params.memory * 0.27f;
                    if (state.completedLoops >= state.repeatLoops) {
                        configureCapture(lane, state, params);
                    }
                }
            }
            output[lane] = boundedOutput(value);
        }
        hasProcessed_ = true;
    }

private:
    struct LaneState {
        uint32_t writePosition = 0u;
        uint32_t capturedSamples = 0u;
        uint32_t windowSamples = 2u;
        uint32_t segmentEnd = 0u;
        uint32_t playPosition = 0u;
        uint32_t completedLoops = 0u;
        uint32_t repeatLoops = 1u;
        uint32_t sourceLane = 0u;
        float loopGain = 1.0f;
        bool repeating = false;
        bool reverse = false;
    };

    size_t bufferIndex(uint32_t lane, uint32_t position) const
    {
        return static_cast<size_t>(lane) * laneStride_
            + static_cast<size_t>(position);
    }

    void configureCapture(uint32_t lane, LaneState& state,
        const CartographySiteProcessParams& params)
    {
        using namespace cartography_site_process_detail;
        const float unit = laneUnit(lane, channels_);
        const float centered = laneCentered(unit, params.center, channels_);
        const float random = laneRandom(lane, 0x42554646u);
        const float octaveShift = centered * params.spread * 1.25f
            + random * params.deviation * 0.88f
            + params.skew * (unit - 0.5f) * 0.72f;
        const float baseSeconds = 0.018f * std::pow(64.0f, params.color);
        const float seconds = clamp(
            baseSeconds * std::pow(2.0f, octaveShift),
            0.012f, kMaximumCaptureSeconds);
        const uint32_t maximum = laneStride_ > 4u ? laneStride_ - 4u : 2u;
        state.windowSamples = std::clamp<uint32_t>(
            static_cast<uint32_t>(std::lround(seconds * sampleRate_)),
            2u, maximum);
        state.capturedSamples = 0u;
        state.playPosition = 0u;
        state.completedLoops = 0u;
        state.repeatLoops = 1u + static_cast<uint32_t>(
            std::lround(params.memory * params.memory * 7.0f));
        state.sourceLane = captureSource(lane, params);
        const float reverseScore = params.skew
            + random * params.deviation * 0.60f
            + centered * params.spread * 0.20f;
        state.reverse = reverseScore < -0.08f;
        state.loopGain = 1.0f;
        state.repeating = false;
    }

    uint32_t captureSource(uint32_t lane,
        const CartographySiteProcessParams& params) const
    {
        if (channels_ <= 1u) return 0u;
        const float relationship = std::max({
            params.spread, params.deviation, std::fabs(params.skew) });
        if (relationship < 1.0e-4f) return lane;
        const float maximum = static_cast<float>(channels_ - 1u);
        const float random = cartography_site_process_detail::laneRandom(
            lane, 0x534f5552u);
        const float base = 1.0f
            + params.spread * static_cast<float>(channels_ - 2u);
        const long rounded = std::lround(
            base + random * params.deviation * maximum * 0.28f);
        const uint32_t distance = static_cast<uint32_t>(std::clamp<long>(
            rounded, 1l, static_cast<long>(channels_ - 1u)));
        int direction = random < 0.0f ? -1 : 1;
        if (params.skew < -0.05f) direction = -1;
        if (params.skew > 0.05f) direction = 1;
        const int count = static_cast<int>(channels_);
        const int destination = (static_cast<int>(lane)
            + direction * static_cast<int>(distance) + count) % count;
        return static_cast<uint32_t>(destination);
    }

    double sampleRate_ = 48000.0;
    uint32_t channels_ = 0u;
    uint32_t laneStride_ = 0u;
    bool prepared_ = false;
    bool hasProcessed_ = false;
    cartography_site_process_detail::ParameterSmoother smoother_ {};
    std::array<LaneState, kCartographySiteProcessLanes> states_ {};
    std::vector<float> buffer_ {};
};

} // namespace s3g
