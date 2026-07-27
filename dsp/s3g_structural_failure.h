#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace s3g {

enum class StructuralConsequence : uint32_t {
    HingeFall = 0u,
    PlateRupture = 1u,
};

struct StructuralFailureParams {
    float drive = 0.0f;
    float motion = 0.0f;
    float hierarchy = 0.5f;
    float stiffness = 0.5f;
    float toughness = 0.5f;
    float damage = 0.0f;
    float highDetail = 0.5f;
    float consequence = 0.0f;
    float mass = 0.5f;
    StructuralConsequence mode = StructuralConsequence::HingeFall;
};

struct StructuralFailureOutput {
    float flex = 0.0f;
    float crack = 0.0f;
    float snap = 0.0f;
    float rupture = 0.0f;
    float fall = 0.0f;
    float impact = 0.0f;
    float activity = 0.0f;
    bool crackTriggered = false;
    bool snapTriggered = false;
    bool consequenceTriggered = false;
};

// A three-generation structural failure model. Large members, secondary
// limbs/plates, and fine branches/cracks accumulate load independently. A
// Strouhal-derived forcing clock drives tree structures; plate structures use
// irregular pressure steps. Failures excite aperiodic wavelets rather than
// free-running audio oscillators.
class StructuralFailureModel {
public:
    void prepare(double sampleRate, uint32_t seed)
    {
        sampleRate_ = std::max(1000.0, sampleRate);
        reset(seed);
    }

    void reset(uint32_t seed)
    {
        rng_ = seed ? seed : 1u;
        loads_.fill(0.0f);
        fatigue_.fill(0.0f);
        for (uint32_t generation = 0u; generation < loads_.size();
            ++generation) {
            thresholds_[generation] = baseThreshold(generation)
                * (0.78f + randomUnit() * 0.54f);
        }
        forcingTimer_ = 0.02f + randomUnit() * 0.32f;
        forcingPulse_ = 0.0f;
        flexEnvelope_ = 0.0f;
        crackEnvelope_ = 0.0f;
        snapEnvelope_ = 0.0f;
        ruptureEnvelope_ = 0.0f;
        impactEnvelope_ = 0.0f;
        consequenceActive_ = false;
        consequenceProgress_ = 0.0f;
        consequenceDuration_ = 1.0f;
        secondaryTimer_ = 0.0f;
        cascadeTimer_ = 0.0f;
        cascadeRemaining_ = 0u;
        lowNoise_ = 0.0f;
        presenceNoise_ = 0.0f;
        highNoise_ = 0.0f;
        previousWhite_ = 0.0f;
        forcedConsequencePending_ = false;
    }

    // Applies an external physical impulse from a higher-level environmental
    // entity (footfall, crown failure, slab release). The normal load/fatigue
    // path still produces the sound; a scored consequence merely guarantees
    // that the primary member reaches its failure threshold on the next
    // process step.
    void excite(float strength, bool forceConsequence = false)
    {
        strength = clampUnit(strength);
        if (strength <= 0.0001f) return;
        forcingPulse_ = std::max(forcingPulse_, strength);
        flexEnvelope_ = std::max(flexEnvelope_, strength * 0.72f);
        loads_[2] += strength * 0.34f;
        loads_[1] += strength * 0.26f;
        loads_[0] += strength * 0.18f;
        if (forceConsequence) {
            loads_[0] = std::max(loads_[0],
                thresholds_[0] * (1.02f + strength * 0.24f));
            forcedConsequencePending_ = true;
        }
    }

    StructuralFailureOutput process(StructuralFailureParams params)
    {
        sanitize(params);
        const float sr = static_cast<float>(sampleRate_);
        const float dt = 1.0f / sr;
        StructuralFailureOutput output {};

        const float white = randomSigned();
        const float lowCutoff = 70.0f + params.mass * 240.0f;
        const float presenceCutoff = 1050.0f
            + params.highDetail * 4600.0f;
        const float highCutoff = 3600.0f
            + params.highDetail * 9800.0f;
        lowNoise_ += (white - lowNoise_)
            * onePoleCoefficient(lowCutoff, sr);
        presenceNoise_ += (white - presenceNoise_)
            * onePoleCoefficient(presenceCutoff, sr);
        highNoise_ += (white - highNoise_)
            * onePoleCoefficient(highCutoff, sr);
        const float presenceBand = presenceNoise_ - lowNoise_;
        const float highBand = white - highNoise_;
        const float derivative = white - previousWhite_;
        previousWhite_ = white;

        forcingTimer_ -= dt;
        if (forcingTimer_ <= 0.0f && params.drive > 0.0001f) {
            const bool plate = params.mode == StructuralConsequence::PlateRupture;
            float forcingRate = 0.0f;
            if (plate) {
                // Irregular pressure or footfall cadence.
                forcingRate = 0.32f + params.motion * 2.8f
                    + params.drive * 0.74f;
            } else {
                // Karman vortex shedding: f = St * velocity / diameter.
                const float velocity = 0.22f + params.motion * 3.4f;
                const float diameter = 0.035f
                    + (1.0f - params.hierarchy) * 0.24f
                    + params.mass * 0.16f;
                forcingRate = std::clamp(
                    0.20f * velocity / diameter, 0.18f, 18.0f);
            }
            forcingTimer_ = (0.62f + randomUnit() * 0.86f)
                / std::max(0.08f, forcingRate);
            const float impulse = params.drive
                * (0.32f + randomUnit() * 0.68f);
            forcingPulse_ = std::max(forcingPulse_, impulse);
            flexEnvelope_ = std::max(flexEnvelope_,
                impulse * (0.32f + params.mass * 0.68f));
            for (uint32_t generation = 0u; generation < loads_.size();
                ++generation) {
                const float generationGain = 0.58f
                    + static_cast<float>(generation) * 0.34f;
                loads_[generation] += impulse * generationGain
                    * (plate ? 0.030f : 0.018f);
            }
        }

        for (uint32_t generation = 0u; generation < loads_.size();
            ++generation) {
            const float generationGain = 0.72f
                + static_cast<float>(generation) * 0.46f;
            const float applied = params.drive
                * (0.22f + params.motion * 0.78f)
                * generationGain
                * (0.36f + params.damage * 0.92f)
                + forcingPulse_ * (0.18f + generationGain * 0.12f);
            loads_[generation] += applied * dt * 0.19f;
            loads_[generation] = std::max(0.0f,
                loads_[generation] - loads_[generation] * dt
                    * (0.10f + params.stiffness * 0.34f));
            const float fatigueDrive = std::max(0.0f,
                loads_[generation] - thresholds_[generation] * 0.24f);
            fatigue_[generation] += fatigueDrive * dt
                * (0.05f + params.damage * 0.32f);
            fatigue_[generation] = std::max(0.0f,
                fatigue_[generation] - dt
                    * (0.0008f + params.toughness * 0.0032f));
            const float plateConsequenceThreshold =
                params.mode == StructuralConsequence::PlateRupture
                    && generation == 0u
                ? 1.0f - params.consequence * 0.36f : 1.0f;
            if (loads_[generation] + fatigue_[generation]
                    >= thresholds_[generation]
                        * plateConsequenceThreshold) {
                triggerFailure(generation, params, output, false);
            }
        }

        if (cascadeRemaining_ > 0u) {
            cascadeTimer_ -= dt;
            if (cascadeTimer_ <= 0.0f) {
                const uint32_t generation = randomUnit() < 0.72f ? 2u : 1u;
                triggerFailure(generation, params, output, true);
                --cascadeRemaining_;
                cascadeTimer_ = 0.00045f + randomUnit()
                    * (0.0035f + (1.0f - params.highDetail) * 0.012f);
            }
        }

        float consequenceRamp = 0.0f;
        if (consequenceActive_) {
            consequenceProgress_ += dt
                / std::max(0.01f, consequenceDuration_);
            consequenceRamp = std::clamp(consequenceProgress_, 0.0f, 1.0f);
            secondaryTimer_ -= dt;
            if (secondaryTimer_ <= 0.0f && consequenceProgress_ < 0.94f) {
                const uint32_t generation = randomUnit() < 0.58f ? 1u : 2u;
                triggerFailure(generation, params, output, true);
                secondaryTimer_ = 0.012f + randomUnit()
                    * (params.mode == StructuralConsequence::HingeFall
                            ? 0.16f : 0.035f);
            }
            if (consequenceProgress_ >= 1.0f) {
                consequenceActive_ = false;
                consequenceProgress_ = 0.0f;
                impactEnvelope_ = std::max(impactEnvelope_,
                    (0.38f + params.mass * 0.92f)
                        * (0.36f + params.consequence * 0.76f));
            }
        }

        forcingPulse_ *= std::exp(-dt
            / (0.018f + params.mass * 0.11f));
        flexEnvelope_ *= std::exp(-dt
            / (0.028f + params.mass * 0.24f));
        crackEnvelope_ *= std::exp(-dt
            / (0.00032f + params.highDetail * 0.0028f));
        snapEnvelope_ *= std::exp(-dt
            / (0.0018f + params.mass * 0.014f));
        ruptureEnvelope_ *= std::exp(-dt
            / (0.012f + params.mass * 0.16f));
        impactEnvelope_ *= std::exp(-dt
            / (0.018f + params.mass * 0.24f));

        output.flex = flexEnvelope_
            * (lowNoise_ * 0.72f + presenceBand * 0.28f);
        output.crack = crackEnvelope_
            * (highBand * (0.72f + params.highDetail * 0.42f)
                + derivative * 0.38f);
        output.snap = snapEnvelope_
            * (presenceBand * 0.46f + highBand * 0.48f
                + derivative * 0.24f);
        output.rupture = ruptureEnvelope_
            * (presenceBand * 0.74f + lowNoise_ * 0.46f
                + highBand * 0.18f);
        if (consequenceActive_) {
            if (params.mode == StructuralConsequence::HingeFall) {
                output.fall = consequenceRamp
                    * (lowNoise_ * (0.46f + params.mass * 0.72f)
                        + presenceBand * (0.18f + consequenceRamp * 0.46f));
            } else {
                output.fall = (1.0f - consequenceRamp * 0.42f)
                    * (presenceBand * 0.62f + highBand * 0.28f)
                    * (0.38f + params.mass * 0.62f);
            }
        }
        output.impact = impactEnvelope_
            * (lowNoise_ * 1.26f + presenceBand * 0.38f
                + forcingPulse_ * randomSigned() * 0.52f);
        output.activity = std::max({ flexEnvelope_, crackEnvelope_,
            snapEnvelope_, ruptureEnvelope_, impactEnvelope_,
            consequenceActive_ ? 0.35f + consequenceRamp * 0.65f : 0.0f });
        return output;
    }

private:
    static float clampUnit(float value)
    {
        return std::clamp(std::isfinite(value) ? value : 0.0f, 0.0f, 1.0f);
    }

    static void sanitize(StructuralFailureParams& params)
    {
        params.drive = clampUnit(params.drive);
        params.motion = clampUnit(params.motion);
        params.hierarchy = clampUnit(params.hierarchy);
        params.stiffness = clampUnit(params.stiffness);
        params.toughness = clampUnit(params.toughness);
        params.damage = clampUnit(params.damage);
        params.highDetail = clampUnit(params.highDetail);
        params.consequence = clampUnit(params.consequence);
        params.mass = clampUnit(params.mass);
    }

    static float baseThreshold(uint32_t generation)
    {
        constexpr std::array<float, 3> values { 0.38f, 0.20f, 0.085f };
        return values[std::min<uint32_t>(generation, 2u)];
    }

    float failureThreshold(uint32_t generation,
        const StructuralFailureParams& params)
    {
        const float hierarchyScale = generation == 0u
            ? 1.0f + params.mass * 0.42f
            : 1.0f - params.hierarchy
                * (generation == 2u ? 0.34f : 0.18f);
        return baseThreshold(generation)
            * hierarchyScale
            * (0.64f + params.toughness * 0.84f)
            * (0.76f + randomUnit() * 0.52f);
    }

    void triggerFailure(uint32_t generation,
        const StructuralFailureParams& params,
        StructuralFailureOutput& output, bool cascade)
    {
        const float threshold = std::max(0.001f, thresholds_[generation]);
        const float overload = (loads_[generation]
            + fatigue_[generation]) / threshold;
        const float strength = std::clamp(
            (0.26f + randomUnit() * 0.74f)
                * (0.58f + overload * 0.52f)
                * (0.42f + params.damage * 0.48f
                    + params.highDetail * 0.34f)
                * (cascade ? 0.68f : 1.0f),
            0.04f, 1.8f);
        loads_[generation] *= 0.04f + randomUnit() * 0.12f;
        fatigue_[generation] *= 0.06f + randomUnit() * 0.16f;
        thresholds_[generation] = failureThreshold(generation, params);

        if (generation == 2u) {
            crackEnvelope_ = std::max(crackEnvelope_, strength);
            output.crackTriggered = true;
            return;
        }
        if (generation == 1u) {
            snapEnvelope_ = std::max(snapEnvelope_, strength);
            crackEnvelope_ = std::max(crackEnvelope_, strength * 0.52f);
            output.snapTriggered = true;
            if (!cascade) {
                cascadeRemaining_ += 1u + static_cast<uint32_t>(
                    randomUnit() * (2.0f + params.hierarchy * 4.0f));
                cascadeTimer_ = 0.0007f + randomUnit() * 0.006f;
            }
            return;
        }

        ruptureEnvelope_ = std::max(ruptureEnvelope_, strength);
        snapEnvelope_ = std::max(snapEnvelope_, strength * 0.68f);
        output.snapTriggered = true;
        cascadeRemaining_ += 2u + static_cast<uint32_t>(
            randomUnit() * (3.0f + params.hierarchy * 8.0f));
        cascadeTimer_ = 0.0005f + randomUnit() * 0.004f;
        const float consequenceChance = params.consequence
            * (0.18f + params.damage * 0.64f);
        const bool forceConsequence = forcedConsequencePending_;
        forcedConsequencePending_ = false;
        if (!consequenceActive_
            && (forceConsequence || randomUnit() < consequenceChance)) {
            consequenceActive_ = true;
            consequenceProgress_ = 0.0f;
            consequenceDuration_ = params.mode
                    == StructuralConsequence::HingeFall
                ? 0.24f + params.mass * 1.42f
                    + randomUnit() * 0.36f
                : 0.025f + params.mass * 0.16f
                    + randomUnit() * 0.055f;
            secondaryTimer_ = 0.006f + randomUnit() * 0.045f;
            output.consequenceTriggered = true;
        }
    }

    uint32_t nextRandom()
    {
        rng_ ^= rng_ << 13u;
        rng_ ^= rng_ >> 17u;
        rng_ ^= rng_ << 5u;
        return rng_;
    }

    float randomUnit()
    {
        return static_cast<float>(nextRandom() & 0x00ffffffu)
            / static_cast<float>(0x01000000u);
    }

    float randomSigned() { return randomUnit() * 2.0f - 1.0f; }

    static float onePoleCoefficient(float cutoff, float sampleRate)
    {
        constexpr float kTwoPi = 6.28318530717958647692f;
        return 1.0f - std::exp(-kTwoPi
            * std::clamp(cutoff, 1.0f, sampleRate * 0.44f) / sampleRate);
    }

    double sampleRate_ = 48000.0;
    uint32_t rng_ = 1u;
    std::array<float, 3> loads_ {};
    std::array<float, 3> fatigue_ {};
    std::array<float, 3> thresholds_ {};
    float forcingTimer_ = 0.0f;
    float forcingPulse_ = 0.0f;
    float flexEnvelope_ = 0.0f;
    float crackEnvelope_ = 0.0f;
    float snapEnvelope_ = 0.0f;
    float ruptureEnvelope_ = 0.0f;
    float impactEnvelope_ = 0.0f;
    bool consequenceActive_ = false;
    float consequenceProgress_ = 0.0f;
    float consequenceDuration_ = 1.0f;
    float secondaryTimer_ = 0.0f;
    float cascadeTimer_ = 0.0f;
    uint32_t cascadeRemaining_ = 0u;
    float lowNoise_ = 0.0f;
    float presenceNoise_ = 0.0f;
    float highNoise_ = 0.0f;
    float previousWhite_ = 0.0f;
    bool forcedConsequencePending_ = false;
};

} // namespace s3g
