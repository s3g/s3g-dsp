#pragma once

#include "s3g_math.h"
#include "s3g_realtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace s3g {

constexpr uint32_t kNoInputMixerChannels = 8u;
constexpr uint32_t kNoInputMixerInsertSlots = 3u;
constexpr uint32_t kNoInputMixerMatrixCells =
    kNoInputMixerChannels * kNoInputMixerChannels;

enum class NoInputDistortionType : uint32_t {
    Bypass = 0u,
    Muff,
    Rat,
    ZoneA,
    ZoneB,
    FuzzI,
    FuzzII,
    Diode,
    Ring,
    Count,
};

constexpr uint32_t kNoInputDistortionTypeCount =
    static_cast<uint32_t>(NoInputDistortionType::Count);

inline const char* noInputDistortionName(NoInputDistortionType type)
{
    switch (type) {
    case NoInputDistortionType::Bypass: return "BYPASS";
    case NoInputDistortionType::Muff: return "MUFF";
    case NoInputDistortionType::Rat: return "RAT";
    case NoInputDistortionType::ZoneA: return "ZONE A";
    case NoInputDistortionType::ZoneB: return "ZONE B";
    case NoInputDistortionType::FuzzI: return "FUZZ I";
    case NoInputDistortionType::FuzzII: return "FUZZ II";
    case NoInputDistortionType::Diode: return "DIODE";
    case NoInputDistortionType::Ring: return "RING";
    case NoInputDistortionType::Count: break;
    }
    return "BYPASS";
}

struct NoInputInsertParams {
    NoInputDistortionType type = NoInputDistortionType::Bypass;
    float gain = 0.35f;
    float tone = 0.50f;
    float bias = 0.0f;
    float levelDb = 0.0f;
    uint32_t bypass = 0u;
};

struct NoInputLaneParams {
    float body = 0.50f;
    float loss = 0.38f;
    float levelDb = -3.0f;
    uint32_t mute = 0u;
    float lowDb = 0.0f;
    float midFrequencyHz = 850.0f;
    float midGainDb = 0.0f;
    float highDb = 0.0f;
    std::array<NoInputInsertParams, kNoInputMixerInsertSlots> inserts {};
};

struct NoInputMixerParams {
    float outputGainDb = -18.0f;
    float ceilingDb = -1.0f;
    uint32_t limiterEnabled = 1u;
    uint32_t dcBlockEnabled = 1u;
    float feedback = 0.82f;
    float coupling = 0.42f;
    float phase = 0.34f;
    float drift = 0.18f;
    float formant = 0.30f;
    uint32_t quality = 1u;
    uint32_t seed = 0x5455444fu;
    std::array<float, kNoInputMixerMatrixCells> matrix {};
    std::array<NoInputLaneParams, kNoInputMixerChannels> lanes {};
};

inline NoInputMixerParams defaultNoInputMixerParams()
{
    NoInputMixerParams params;
    for (uint32_t destination = 0u;
         destination < kNoInputMixerChannels; ++destination) {
        for (uint32_t source = 0u;
             source < kNoInputMixerChannels; ++source) {
            params.matrix[destination * kNoInputMixerChannels + source] =
                destination == source ? 0.94f : 0.0f;
        }
        const uint32_t previous =
            (destination + kNoInputMixerChannels - 1u)
            % kNoInputMixerChannels;
        params.matrix[destination * kNoInputMixerChannels + previous] =
            (destination & 1u) == 0u ? 0.12f : -0.10f;

        auto& lane = params.lanes[destination];
        lane.body = 0.28f + 0.055f * static_cast<float>(destination);
        lane.loss = 0.31f + 0.025f * static_cast<float>(destination % 3u);
        lane.levelDb = -4.5f;
        lane.midFrequencyHz = 420.0f
            * std::pow(1.24f, static_cast<float>(destination));

        lane.inserts[0].type = NoInputDistortionType::Muff;
        lane.inserts[0].gain = 0.26f + 0.018f * static_cast<float>(destination);
        lane.inserts[0].tone = 0.38f + 0.045f * static_cast<float>(destination % 4u);
        lane.inserts[0].levelDb = -8.0f;
        lane.inserts[1].type = NoInputDistortionType::Rat;
        lane.inserts[1].gain = 0.24f;
        lane.inserts[1].tone = 0.58f;
        lane.inserts[1].levelDb = -5.0f;
        lane.inserts[1].bypass = 1u;
        lane.inserts[2].type = NoInputDistortionType::ZoneA;
        lane.inserts[2].gain = 0.20f;
        lane.inserts[2].tone = 0.52f;
        lane.inserts[2].levelDb = -6.0f;
        lane.inserts[2].bypass = 1u;
    }
    return params;
}

inline NoInputInsertParams sanitizeNoInputInsertParams(
    NoInputInsertParams params)
{
    const uint32_t type = std::min<uint32_t>(
        static_cast<uint32_t>(params.type),
        kNoInputDistortionTypeCount - 1u);
    params.type = static_cast<NoInputDistortionType>(type);
    params.gain = clamp(std::isfinite(params.gain) ? params.gain : 0.0f,
        0.0f, 1.0f);
    params.tone = clamp(std::isfinite(params.tone) ? params.tone : 0.5f,
        0.0f, 1.0f);
    params.bias = clamp(std::isfinite(params.bias) ? params.bias : 0.0f,
        -1.0f, 1.0f);
    params.levelDb = clamp(
        std::isfinite(params.levelDb) ? params.levelDb : 0.0f,
        -24.0f, 12.0f);
    params.bypass = params.bypass != 0u ? 1u : 0u;
    return params;
}

inline NoInputMixerParams sanitizeNoInputMixerParams(
    NoInputMixerParams params)
{
    params.outputGainDb = clamp(
        std::isfinite(params.outputGainDb) ? params.outputGainDb : -18.0f,
        -60.0f, 6.0f);
    params.ceilingDb = clamp(
        std::isfinite(params.ceilingDb) ? params.ceilingDb : -1.0f,
        -18.0f, 0.0f);
    params.limiterEnabled = params.limiterEnabled != 0u ? 1u : 0u;
    params.dcBlockEnabled = params.dcBlockEnabled != 0u ? 1u : 0u;
    params.feedback = clamp(
        std::isfinite(params.feedback) ? params.feedback : 0.82f,
        0.0f, 1.25f);
    params.coupling = clamp(
        std::isfinite(params.coupling) ? params.coupling : 0.42f,
        0.0f, 1.25f);
    params.phase = clamp(std::isfinite(params.phase) ? params.phase : 0.34f,
        0.0f, 1.0f);
    params.drift = clamp(std::isfinite(params.drift) ? params.drift : 0.18f,
        0.0f, 1.0f);
    params.formant = clamp(
        std::isfinite(params.formant) ? params.formant : 0.30f,
        0.0f, 1.0f);
    params.quality = std::min<uint32_t>(params.quality, 2u);
    if (params.seed == 0u) params.seed = 1u;
    for (float& value : params.matrix) {
        value = clamp(std::isfinite(value) ? value : 0.0f, -1.0f, 1.0f);
    }
    for (auto& lane : params.lanes) {
        lane.body = clamp(std::isfinite(lane.body) ? lane.body : 0.5f,
            0.0f, 1.0f);
        lane.loss = clamp(std::isfinite(lane.loss) ? lane.loss : 0.38f,
            0.0f, 1.0f);
        lane.levelDb = clamp(
            std::isfinite(lane.levelDb) ? lane.levelDb : -3.0f,
            -60.0f, 12.0f);
        lane.mute = lane.mute != 0u ? 1u : 0u;
        lane.lowDb = clamp(std::isfinite(lane.lowDb) ? lane.lowDb : 0.0f,
            -18.0f, 18.0f);
        lane.midFrequencyHz = clamp(
            std::isfinite(lane.midFrequencyHz)
                ? lane.midFrequencyHz : 850.0f,
            80.0f, 8000.0f);
        lane.midGainDb = clamp(
            std::isfinite(lane.midGainDb) ? lane.midGainDb : 0.0f,
            -18.0f, 18.0f);
        lane.highDb = clamp(
            std::isfinite(lane.highDb) ? lane.highDb : 0.0f,
            -18.0f, 18.0f);
        for (auto& insert : lane.inserts) {
            insert = sanitizeNoInputInsertParams(insert);
        }
    }
    return params;
}

enum class NoInputContainmentState : uint32_t {
    Quiet = 0u,
    Stable,
    Edge,
    Runaway,
};

inline const char* noInputContainmentName(NoInputContainmentState state)
{
    switch (state) {
    case NoInputContainmentState::Quiet: return "QUIET";
    case NoInputContainmentState::Stable: return "STABLE";
    case NoInputContainmentState::Edge: return "EDGE";
    case NoInputContainmentState::Runaway: return "RUNAWAY";
    }
    return "QUIET";
}

class NoInputMixer {
public:
    void prepare(double sampleRate)
    {
        sampleRate_ = std::max(1.0, sampleRate);
        dcPole_ = std::exp(-2.0f * kPi * 12.0f
            / static_cast<float>(sampleRate_));
        energyAttack_ = 1.0f - std::exp(-1.0f
            / static_cast<float>(sampleRate_ * 0.012));
        energyRelease_ = 1.0f - std::exp(-1.0f
            / static_cast<float>(sampleRate_ * 0.180));
        governorAttack_ = 1.0f - std::exp(-1.0f
            / static_cast<float>(sampleRate_ * 0.018));
        governorRelease_ = 1.0f - std::exp(-1.0f
            / static_cast<float>(sampleRate_ * 0.420));
        panicSamplesTotal_ = std::max<uint32_t>(
            1u, static_cast<uint32_t>(sampleRate_ * 0.008));
        setParams(params_);
        reset();
    }

    void setParams(NoInputMixerParams params)
    {
        params_ = sanitizeNoInputMixerParams(params);
        for (uint32_t lane = 0u; lane < kNoInputMixerChannels; ++lane) {
            updateEq(lane);
            updateBodyCoefficients(lane);
            for (uint32_t slot = 0u;
                 slot < kNoInputMixerInsertSlots; ++slot) {
                auto& runtime = laneState_[lane].inserts[slot];
                const auto& insert = params_.lanes[lane].inserts[slot];
                const NoInputDistortionType effective = insert.bypass != 0u
                    ? NoInputDistortionType::Bypass : insert.type;
                if (!runtime.initialized) {
                    runtime.currentType = effective;
                    runtime.previousType = effective;
                    runtime.crossfade = 1.0f;
                    runtime.initialized = true;
                } else if (runtime.currentType != effective) {
                    runtime.previousType = runtime.currentType;
                    runtime.currentType = effective;
                    runtime.crossfade = 0.0f;
                }
            }
        }
    }

    const NoInputMixerParams& params() const { return params_; }

    void reset()
    {
        clearSignalState();
        silenced_ = true;
        seedRemaining_ = 0u;
        panicRemaining_ = 0u;
        containmentState_ = NoInputContainmentState::Quiet;
    }

    void reseed(uint32_t seed, float amount = 0.45f)
    {
        clearSignalState();
        randomState_ = seed == 0u ? 1u : seed;
        seedAmount_ = clamp(amount, 0.01f, 1.0f);
        seedRemaining_ = std::max<uint32_t>(
            16u, static_cast<uint32_t>(sampleRate_ * 0.018));
        silenced_ = false;
        panicRemaining_ = 0u;
    }

    void triggerSeed(float amount = 0.35f)
    {
        seedAmount_ = clamp(amount, 0.01f, 1.0f);
        seedRemaining_ = std::max<uint32_t>(
            16u, static_cast<uint32_t>(sampleRate_ * 0.012));
        silenced_ = false;
    }

    void panic()
    {
        panicRemaining_ = panicSamplesTotal_;
    }

    void killLane(uint32_t lane)
    {
        if (lane >= kNoInputMixerChannels) return;
        laneState_[lane] = {};
        returns_[lane] = 0.0f;
        previousReturns_[lane] = 0.0f;
        lanePeak_[lane] = 0.0f;
        laneActivity_[lane] = 0.0f;
        for (uint32_t source = 0u; source < kNoInputMixerChannels;
             ++source) {
            phaseMemory_[lane * kNoInputMixerChannels + source] = 0.0f;
            phaseMemory_[source * kNoInputMixerChannels + lane] = 0.0f;
        }
        updateEq(lane);
        updateBodyCoefficients(lane);
        initializeInsertTypes(lane);
    }

    void processFrame(float* output)
    {
        if (!output) return;
        if (silenced_ && seedRemaining_ == 0u) {
            std::fill(output, output + kNoInputMixerChannels, 0.0f);
            return;
        }

        if ((controlCounter_++ & 31u) == 0u) updateSlowControl();
        previousReturns_ = returns_;

        std::array<float, kNoInputMixerChannels> matrixInput {};
        for (uint32_t destination = 0u;
             destination < kNoInputMixerChannels; ++destination) {
            float sum = seedForLane(destination);
            float weightSum = 0.0f;
            for (uint32_t source = 0u;
                 source < kNoInputMixerChannels; ++source) {
                const uint32_t index =
                    destination * kNoInputMixerChannels + source;
                const float matrixGain = params_.matrix[index];
                if (std::abs(matrixGain) < 1.0e-7f) continue;
                const float networkScale = source == destination
                    ? params_.feedback
                    : params_.feedback * params_.coupling;
                const float driftScale = 1.0f
                    + driftState_[index] * params_.drift * 0.14f;
                const float gain = matrixGain * networkScale * driftScale;
                const float coefficient = 0.08f + params_.phase
                    * (0.20f + 0.22f * hashUnit(index + 0x3157u));
                const float input = previousReturns_[source];
                const float allpass = -coefficient * input
                    + phaseMemory_[index];
                phaseMemory_[index] = flushDenormal(
                    input + coefficient * allpass);
                const float phased = lerp(input, allpass, params_.phase);
                sum += phased * gain;
                weightSum += std::abs(gain);
            }
            const float normalization = 1.0f
                / std::max(1.0f, 0.42f + weightSum * 0.68f);
            matrixInput[destination] = clamp(sum * normalization,
                -6.0f, 6.0f);
        }

        float networkPeak = 0.0f;
        for (uint32_t lane = 0u; lane < kNoInputMixerChannels; ++lane) {
            float value = processBody(lane, matrixInput[lane]);
            value = processFormant(lane, value);
            value = laneState_[lane].lowShelf.process(value);
            value = laneState_[lane].midPeak.process(value);
            value = laneState_[lane].highShelf.process(value);

            for (uint32_t slot = 0u;
                 slot < kNoInputMixerInsertSlots; ++slot) {
                const uint32_t ringSource =
                    (lane + slot + 1u) % kNoInputMixerChannels;
                value = processInsert(lane, slot, value,
                    previousReturns_[ringSource]);
            }

            if (!std::isfinite(value)) {
                killLane(lane);
                value = 0.0f;
            }
            value = clamp(value, -8.0f, 8.0f);

            auto& state = laneState_[lane];
            const float energyInput = value * value;
            const float energyCoefficient = energyInput > state.energy
                ? energyAttack_ : energyRelease_;
            state.energy += (energyInput - state.energy) * energyCoefficient;
            state.energy = std::max(0.0f, flushDenormal(state.energy));
            const float rms = std::sqrt(state.energy);
            const float excess = std::max(0.0f, rms - 0.42f);
            const float targetGovernor = 1.0f
                / (1.0f + excess * excess * 28.0f + excess * 3.5f);
            const float governorCoefficient = targetGovernor < state.governor
                ? governorAttack_ : governorRelease_;
            state.governor += (targetGovernor - state.governor)
                * governorCoefficient;
            state.governor = clamp(state.governor, 0.055f, 1.0f);

            float returned = value * state.governor;
            returned = dcBlock(returned, state.returnDcInput,
                state.returnDcOutput);
            returns_[lane] = flushDenormal(returned);
            networkPeak = std::max(networkPeak, std::abs(returned));

            float audition = value * dbToGain(params_.lanes[lane].levelDb)
                * dbToGain(params_.outputGainDb);
            if (params_.lanes[lane].mute != 0u) audition = 0.0f;
            audition = dcBlock(audition, state.outputDcInput,
                state.outputDcOutput);
            if (params_.limiterEnabled != 0u) {
                audition = softLimit(audition,
                    dbToGain(params_.ceilingDb));
            }
            if (panicRemaining_ > 0u) {
                audition *= static_cast<float>(panicRemaining_)
                    / static_cast<float>(panicSamplesTotal_);
            }
            output[lane] = std::isfinite(audition)
                ? flushDenormal(audition) : 0.0f;
            lanePeak_[lane] = std::max(
                lanePeak_[lane] * 0.9994f, std::abs(output[lane]));
            laneActivity_[lane] += (rms - laneActivity_[lane]) * 0.003f;
        }

        if (seedRemaining_ > 0u) --seedRemaining_;
        if (panicRemaining_ > 0u) {
            --panicRemaining_;
            if (panicRemaining_ == 0u) {
                clearSignalState();
                silenced_ = true;
                containmentState_ = NoInputContainmentState::Quiet;
                std::fill(output, output + kNoInputMixerChannels, 0.0f);
                return;
            }
        }

        networkActivity_ += (networkPeak - networkActivity_) * 0.0025f;
        const float minimumGovernor = minimumLaneGovernor();
        if (networkActivity_ < 1.0e-5f) {
            containmentState_ = NoInputContainmentState::Quiet;
        } else if (minimumGovernor > 0.72f) {
            containmentState_ = NoInputContainmentState::Stable;
        } else if (minimumGovernor > 0.28f) {
            containmentState_ = NoInputContainmentState::Edge;
        } else {
            containmentState_ = NoInputContainmentState::Runaway;
        }
    }

    float lanePeak(uint32_t lane) const
    {
        return lane < kNoInputMixerChannels ? lanePeak_[lane] : 0.0f;
    }

    float laneActivity(uint32_t lane) const
    {
        return lane < kNoInputMixerChannels ? laneActivity_[lane] : 0.0f;
    }

    float networkActivity() const { return networkActivity_; }
    float minimumGovernor() const { return minimumLaneGovernor(); }
    NoInputContainmentState containmentState() const
    {
        return containmentState_;
    }

private:
    struct Biquad {
        float b0 = 1.0f;
        float b1 = 0.0f;
        float b2 = 0.0f;
        float a1 = 0.0f;
        float a2 = 0.0f;
        float z1 = 0.0f;
        float z2 = 0.0f;

        float process(float input)
        {
            const float output = input * b0 + z1;
            z1 = flushDenormal(input * b1 - output * a1 + z2);
            z2 = flushDenormal(input * b2 - output * a2);
            return output;
        }

        void clear() { z1 = z2 = 0.0f; }
    };

    struct Resonator {
        float c1 = 0.0f;
        float c2 = 0.0f;
        float inputGain = 0.0f;
        float z1 = 0.0f;
        float z2 = 0.0f;

        float process(float input)
        {
            const float output = input * inputGain + c1 * z1 + c2 * z2;
            z2 = z1;
            z1 = flushDenormal(clamp(output, -12.0f, 12.0f));
            return z1;
        }

        void clear() { z1 = z2 = 0.0f; }
    };

    struct DistortionState {
        float low = 0.0f;
        float high = 0.0f;
        float memory = 0.0f;
        float envelope = 0.0f;
        float previous = 0.0f;
    };

    struct InsertRuntime {
        std::array<DistortionState, kNoInputDistortionTypeCount> states {};
        NoInputDistortionType currentType = NoInputDistortionType::Bypass;
        NoInputDistortionType previousType = NoInputDistortionType::Bypass;
        float crossfade = 1.0f;
        bool initialized = false;
    };

    struct LaneState {
        std::array<Resonator, 4u> resonators {};
        Biquad lowShelf {};
        Biquad midPeak {};
        Biquad highShelf {};
        std::array<InsertRuntime, kNoInputMixerInsertSlots> inserts {};
        float formantLow = 0.0f;
        float formantHigh = 0.0f;
        float returnDcInput = 0.0f;
        float returnDcOutput = 0.0f;
        float outputDcInput = 0.0f;
        float outputDcOutput = 0.0f;
        float energy = 0.0f;
        float governor = 1.0f;
    };

    static void setBiquad(Biquad& biquad,
        float b0, float b1, float b2, float a0, float a1, float a2)
    {
        const float inverseA0 = 1.0f / std::max(1.0e-9f, a0);
        biquad.b0 = b0 * inverseA0;
        biquad.b1 = b1 * inverseA0;
        biquad.b2 = b2 * inverseA0;
        biquad.a1 = a1 * inverseA0;
        biquad.a2 = a2 * inverseA0;
    }

    void setLowShelf(Biquad& biquad, float frequency, float gainDb)
    {
        const float a = std::pow(10.0f, gainDb / 40.0f);
        const float omega = 2.0f * kPi * clamp(frequency, 20.0f,
            static_cast<float>(sampleRate_ * 0.45))
            / static_cast<float>(sampleRate_);
        const float cosine = std::cos(omega);
        const float sine = std::sin(omega);
        const float alpha = sine * std::sqrt((a + 1.0f / a) * 2.0f);
        const float beta = 2.0f * std::sqrt(a) * alpha;
        setBiquad(biquad,
            a * ((a + 1.0f) - (a - 1.0f) * cosine + beta),
            2.0f * a * ((a - 1.0f) - (a + 1.0f) * cosine),
            a * ((a + 1.0f) - (a - 1.0f) * cosine - beta),
            (a + 1.0f) + (a - 1.0f) * cosine + beta,
            -2.0f * ((a - 1.0f) + (a + 1.0f) * cosine),
            (a + 1.0f) + (a - 1.0f) * cosine - beta);
    }

    void setHighShelf(Biquad& biquad, float frequency, float gainDb)
    {
        const float a = std::pow(10.0f, gainDb / 40.0f);
        const float omega = 2.0f * kPi * clamp(frequency, 20.0f,
            static_cast<float>(sampleRate_ * 0.45))
            / static_cast<float>(sampleRate_);
        const float cosine = std::cos(omega);
        const float sine = std::sin(omega);
        const float alpha = sine * std::sqrt((a + 1.0f / a) * 2.0f);
        const float beta = 2.0f * std::sqrt(a) * alpha;
        setBiquad(biquad,
            a * ((a + 1.0f) + (a - 1.0f) * cosine + beta),
            -2.0f * a * ((a - 1.0f) + (a + 1.0f) * cosine),
            a * ((a + 1.0f) + (a - 1.0f) * cosine - beta),
            (a + 1.0f) - (a - 1.0f) * cosine + beta,
            2.0f * ((a - 1.0f) - (a + 1.0f) * cosine),
            (a + 1.0f) - (a - 1.0f) * cosine - beta);
    }

    void setPeaking(Biquad& biquad, float frequency, float q, float gainDb)
    {
        const float a = std::pow(10.0f, gainDb / 40.0f);
        const float omega = 2.0f * kPi * clamp(frequency, 20.0f,
            static_cast<float>(sampleRate_ * 0.45))
            / static_cast<float>(sampleRate_);
        const float alpha = std::sin(omega) / (2.0f * std::max(0.1f, q));
        const float cosine = std::cos(omega);
        setBiquad(biquad,
            1.0f + alpha * a,
            -2.0f * cosine,
            1.0f - alpha * a,
            1.0f + alpha / a,
            -2.0f * cosine,
            1.0f - alpha / a);
    }

    void updateEq(uint32_t lane)
    {
        if (lane >= kNoInputMixerChannels) return;
        auto& state = laneState_[lane];
        const auto& params = params_.lanes[lane];
        setLowShelf(state.lowShelf, 180.0f, params.lowDb);
        setPeaking(state.midPeak, params.midFrequencyHz, 0.82f,
            params.midGainDb);
        setHighShelf(state.highShelf, 4200.0f, params.highDb);
    }

    void updateBodyCoefficients(uint32_t lane)
    {
        static constexpr std::array<float, 4u> ratios {{
            1.0f, 1.47f, 2.11f, 3.29f,
        }};
        if (lane >= kNoInputMixerChannels) return;
        const auto& params = params_.lanes[lane];
        const float laneSpread = 1.0f + (static_cast<float>(lane) - 3.5f)
            * 0.013f;
        const float base = 44.0f * std::pow(2.0f, params.body * 5.0f)
            * laneSpread;
        const float decaySeconds = lerp(2.8f, 0.055f, params.loss);
        const float radius = std::exp(-1.0f
            / std::max(1.0f,
                static_cast<float>(sampleRate_) * decaySeconds));
        for (uint32_t mode = 0u; mode < 4u; ++mode) {
            const float detune = 1.0f + driftState_[lane * 8u + mode]
                * params_.drift * 0.018f;
            const float frequency = clamp(base * ratios[mode] * detune,
                24.0f, static_cast<float>(sampleRate_ * 0.42));
            const float omega = 2.0f * kPi * frequency
                / static_cast<float>(sampleRate_);
            auto& resonator = laneState_[lane].resonators[mode];
            resonator.c1 = 2.0f * radius * std::cos(omega);
            resonator.c2 = -radius * radius;
            resonator.inputGain = std::max(0.00015f,
                (1.0f - radius) * (2.4f + 0.35f * mode));
        }
    }

    void initializeInsertTypes(uint32_t lane)
    {
        for (uint32_t slot = 0u;
             slot < kNoInputMixerInsertSlots; ++slot) {
            auto& runtime = laneState_[lane].inserts[slot];
            const auto& insert = params_.lanes[lane].inserts[slot];
            const auto type = insert.bypass != 0u
                ? NoInputDistortionType::Bypass : insert.type;
            runtime.currentType = type;
            runtime.previousType = type;
            runtime.crossfade = 1.0f;
            runtime.initialized = true;
        }
    }

    float processBody(uint32_t lane, float input)
    {
        auto& state = laneState_[lane];
        float body = 0.0f;
        for (uint32_t mode = 0u; mode < 4u; ++mode) {
            const float signedInput = (mode & 1u) == 0u ? input : -input;
            body += state.resonators[mode].process(signedInput);
        }
        body *= 0.31f;
        return std::tanh(input * 0.24f + body * 1.38f);
    }

    float processFormant(uint32_t lane, float input)
    {
        auto& state = laneState_[lane];
        const float body = params_.lanes[lane].body;
        const float lowHz = 120.0f + body * 780.0f;
        const float highHz = 1300.0f + body * 5600.0f;
        const float lowCoefficient = 1.0f - std::exp(-2.0f * kPi * lowHz
            / static_cast<float>(sampleRate_));
        const float highCoefficient = 1.0f - std::exp(-2.0f * kPi * highHz
            / static_cast<float>(sampleRate_));
        state.formantLow += (input - state.formantLow) * lowCoefficient;
        state.formantHigh += (input - state.formantHigh) * highCoefficient;
        state.formantLow = flushDenormal(state.formantLow);
        state.formantHigh = flushDenormal(state.formantHigh);
        const float highpass = input - state.formantLow;
        const float product = std::tanh(
            highpass * state.formantHigh * 5.5f);
        return lerp(input, product, params_.formant * 0.82f);
    }

    float processInsert(uint32_t lane, uint32_t slot,
        float input, float ringSource)
    {
        auto& runtime = laneState_[lane].inserts[slot];
        const auto& params = params_.lanes[lane].inserts[slot];
        const uint32_t substeps = 1u << params_.quality;
        const float step = 1.0f / static_cast<float>(substeps);
        float output = 0.0f;
        const float previousInput = runtime.states[
            static_cast<uint32_t>(runtime.currentType)].previous;
        for (uint32_t substep = 0u; substep < substeps; ++substep) {
            const float fraction = static_cast<float>(substep + 1u) * step;
            const float sample = lerp(previousInput, input, fraction);
            const float current = processDistortion(runtime, runtime.currentType,
                sample, ringSource, params);
            if (runtime.crossfade < 1.0f) {
                const float previous = processDistortion(runtime,
                    runtime.previousType, sample, ringSource, params);
                output += lerp(previous, current, runtime.crossfade);
            } else {
                output += current;
            }
        }
        runtime.states[static_cast<uint32_t>(runtime.currentType)].previous = input;
        output *= step;
        if (runtime.crossfade < 1.0f) {
            runtime.crossfade = std::min(1.0f,
                runtime.crossfade + static_cast<float>(substeps)
                    / static_cast<float>(sampleRate_ * 0.020));
        }
        return flushDenormal(output * dbToGain(params.levelDb));
    }

    float processDistortion(InsertRuntime& runtime,
        NoInputDistortionType type, float input, float ringSource,
        const NoInputInsertParams& params)
    {
        auto& state = runtime.states[static_cast<uint32_t>(type)];
        const float gain = params.gain;
        const float tone = params.tone;
        const float bias = params.bias * 0.22f;
        const float sr = static_cast<float>(sampleRate_)
            * static_cast<float>(1u << params_.quality);
        const auto onePole = [sr](float frequency) {
            return 1.0f - std::exp(-2.0f * kPi
                * std::min(frequency, sr * 0.45f) / sr);
        };
        switch (type) {
        case NoInputDistortionType::Bypass:
            return input;
        case NoInputDistortionType::Muff: {
            const float drive = 1.0f + gain * gain * 44.0f;
            const float first = std::tanh((input + bias) * drive);
            state.memory += (first - state.memory) * onePole(5200.0f);
            const float second = std::tanh(
                (state.memory - bias * 0.45f) * (2.2f + gain * 6.0f));
            state.low += (second - state.low)
                * onePole(lerp(360.0f, 2200.0f, tone));
            const float high = second - state.low;
            return std::tanh(lerp(state.low * 1.7f, high * 1.45f,
                tone) * 1.05f);
        }
        case NoInputDistortionType::Rat: {
            state.low += (input - state.low) * onePole(720.0f);
            const float pre = input - state.low * 0.78f;
            const float driven = pre * (2.0f + gain * gain * 78.0f) + bias;
            const float clipped = clamp(driven, -0.72f, 0.72f) / 0.72f;
            const float cutoff = 14000.0f
                * std::pow(420.0f / 14000.0f, tone);
            state.memory += (clipped - state.memory) * onePole(cutoff);
            return state.memory;
        }
        case NoInputDistortionType::ZoneA:
        case NoInputDistortionType::ZoneB: {
            const bool lower = type == NoInputDistortionType::ZoneB;
            const float lowCut = lower ? 120.0f : 260.0f;
            state.low += (input - state.low) * onePole(lowCut);
            const float upper = input - state.low;
            const float first = std::tanh((upper * (3.0f + gain * 54.0f)
                + bias) * (lower ? 0.72f : 1.0f));
            const float focusHz = lerp(lower ? 420.0f : 780.0f,
                lower ? 2600.0f : 5200.0f, tone);
            state.high += (first - state.high) * onePole(focusHz);
            const float focused = lower
                ? first + state.high * 0.48f
                : first + (first - state.high) * 0.72f;
            return std::tanh((focused - bias * 0.35f)
                * (2.2f + gain * 5.5f));
        }
        case NoInputDistortionType::FuzzI: {
            const float absolute = std::abs(input);
            state.envelope += (absolute - state.envelope)
                * onePole(lerp(4.0f, 30.0f, tone));
            const float sag = 1.0f
                / (1.0f + state.envelope * (1.0f + gain * 8.0f));
            const float drive = (2.0f + gain * gain * 64.0f) * sag;
            return std::tanh((input + bias) * drive)
                - std::tanh(bias * drive);
        }
        case NoInputDistortionType::FuzzII: {
            const float threshold = lerp(0.22f, 0.015f, gain);
            const float hold = state.memory;
            const float driven = input * (3.0f + gain * 72.0f) + bias;
            if (driven > threshold) state.memory = 1.0f;
            else if (driven < -threshold) state.memory = -1.0f;
            const float speed = onePole(lerp(900.0f, 9000.0f, tone));
            return lerp(hold, state.memory, speed);
        }
        case NoInputDistortionType::Diode: {
            const float drive = 1.0f + gain * gain * 36.0f;
            const float positive = std::tanh((input + bias) * drive);
            const float negative = std::tanh((input - bias)
                * drive * lerp(0.68f, 1.32f, tone));
            return (positive + negative) * 0.5f;
        }
        case NoInputDistortionType::Ring: {
            const float depth = gain;
            const float carrier = std::tanh(ringSource
                * (1.0f + tone * 8.0f));
            return lerp(input, input * carrier * 2.0f + bias, depth);
        }
        case NoInputDistortionType::Count:
            break;
        }
        return input;
    }

    float dcBlock(float input, float& previousInput, float& previousOutput)
    {
        if (params_.dcBlockEnabled == 0u) return input;
        const float output = input - previousInput + dcPole_ * previousOutput;
        previousInput = input;
        previousOutput = flushDenormal(output);
        return previousOutput;
    }

    static float softLimit(float input, float ceiling)
    {
        ceiling = std::max(0.001f, ceiling);
        const float absolute = std::abs(input);
        const float knee = ceiling * 0.72f;
        if (absolute <= knee) return input;
        const float span = std::max(1.0e-6f, ceiling - knee);
        const float limited = knee + span
            * (1.0f - std::exp(-(absolute - knee) / span));
        return std::copysign(std::min(ceiling, limited), input);
    }

    float seedForLane(uint32_t lane)
    {
        if (seedRemaining_ == 0u) return 0.0f;
        const float phase = static_cast<float>(seedRemaining_)
            / static_cast<float>(std::max<uint32_t>(1u,
                static_cast<uint32_t>(sampleRate_ * 0.018)));
        const float envelope = phase * phase;
        const float random = randomSigned();
        const float polarity = (lane & 1u) == 0u ? 1.0f : -1.0f;
        return random * envelope * seedAmount_ * 0.34f
            * polarity * (0.72f + 0.055f * static_cast<float>(lane));
    }

    void updateSlowControl()
    {
        for (uint32_t index = 0u;
             index < kNoInputMixerMatrixCells; ++index) {
            driftVelocity_[index] += randomSigned() * 0.014f;
            driftVelocity_[index] *= 0.972f;
            driftState_[index] += driftVelocity_[index] * 0.025f;
            driftState_[index] *= 0.9992f;
            if (driftState_[index] > 1.0f) {
                driftState_[index] = 1.0f;
                driftVelocity_[index] = -std::abs(driftVelocity_[index]);
            } else if (driftState_[index] < -1.0f) {
                driftState_[index] = -1.0f;
                driftVelocity_[index] = std::abs(driftVelocity_[index]);
            }
        }
        for (uint32_t lane = 0u; lane < kNoInputMixerChannels; ++lane) {
            updateBodyCoefficients(lane);
        }
    }

    void clearSignalState()
    {
        laneState_ = {};
        returns_.fill(0.0f);
        previousReturns_.fill(0.0f);
        phaseMemory_.fill(0.0f);
        driftState_.fill(0.0f);
        driftVelocity_.fill(0.0f);
        lanePeak_.fill(0.0f);
        laneActivity_.fill(0.0f);
        networkActivity_ = 0.0f;
        controlCounter_ = 0u;
        for (uint32_t lane = 0u; lane < kNoInputMixerChannels; ++lane) {
            updateEq(lane);
            updateBodyCoefficients(lane);
            initializeInsertTypes(lane);
        }
    }

    float minimumLaneGovernor() const
    {
        float result = 1.0f;
        for (const auto& lane : laneState_) {
            result = std::min(result, lane.governor);
        }
        return result;
    }

    uint32_t randomU32()
    {
        randomState_ += 0x9e3779b9u;
        uint32_t value = randomState_;
        value ^= value >> 16u;
        value *= 0x7feb352du;
        value ^= value >> 15u;
        value *= 0x846ca68bu;
        value ^= value >> 16u;
        return value;
    }

    float randomSigned()
    {
        return static_cast<float>(randomU32() & 0x00ffffffu)
            / static_cast<float>(0x00800000u) - 1.0f;
    }

    static float hashUnit(uint32_t value)
    {
        value ^= value >> 16u;
        value *= 0x7feb352du;
        value ^= value >> 15u;
        value *= 0x846ca68bu;
        value ^= value >> 16u;
        return static_cast<float>(value & 0x00ffffffu)
            / static_cast<float>(0x01000000u);
    }

    double sampleRate_ = 48000.0;
    NoInputMixerParams params_ = defaultNoInputMixerParams();
    std::array<LaneState, kNoInputMixerChannels> laneState_ {};
    std::array<float, kNoInputMixerChannels> returns_ {};
    std::array<float, kNoInputMixerChannels> previousReturns_ {};
    std::array<float, kNoInputMixerMatrixCells> phaseMemory_ {};
    std::array<float, kNoInputMixerMatrixCells> driftState_ {};
    std::array<float, kNoInputMixerMatrixCells> driftVelocity_ {};
    std::array<float, kNoInputMixerChannels> lanePeak_ {};
    std::array<float, kNoInputMixerChannels> laneActivity_ {};
    float networkActivity_ = 0.0f;
    float dcPole_ = 0.998f;
    float energyAttack_ = 0.002f;
    float energyRelease_ = 0.0001f;
    float governorAttack_ = 0.001f;
    float governorRelease_ = 0.00005f;
    float seedAmount_ = 0.45f;
    uint32_t seedRemaining_ = 0u;
    uint32_t randomState_ = 0x5455444fu;
    uint32_t controlCounter_ = 0u;
    uint32_t panicRemaining_ = 0u;
    uint32_t panicSamplesTotal_ = 384u;
    bool silenced_ = true;
    NoInputContainmentState containmentState_ =
        NoInputContainmentState::Quiet;
};

} // namespace s3g
