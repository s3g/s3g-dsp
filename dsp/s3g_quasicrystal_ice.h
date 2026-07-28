#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace s3g {

struct QuasicrystalIceParams {
    // Continuous deformation at the boundary between unlike solid phases.
    float strainRate = 0.0f;
    float phaseMobility = 0.5f;

    // Propagation of an explicitly excited fracture front.
    float frontSpeed = 0.5f;
    float branching = 0.5f;

    // Contrasts between differently oriented domains and local defects.
    float anisotropy = 0.5f;
    float heterogeneity = 0.5f;

    // Larger domains move the texture toward slower, broader events.
    float scale = 0.5f;
    float brittleness = 0.5f;
    float damping = 0.5f;
};

struct QuasicrystalIceOutput {
    float sample = 0.0f;
    float activity = 0.0f;
    float phaseBoundary = 0.0f;
    float fractureFront = 0.0f;
    float latticeAvalanche = 0.0f;
    bool frontAdvanced = false;
    bool avalancheAdvanced = false;
};

// A non-modal planetary-ice force texture for one spatial voice. The model treats
// a quasicrystalline plate as differently oriented solid domains separated by
// mobile phase boundaries. Stored strain advances an irregular fracture
// front; released energy then branches through defects as a lattice
// avalanche. Every audible layer is made from filtered stochastic flux. There
// are deliberately no oscillators, resonator banks, tuned delays, or ringing
// impact bodies here. The encoder can use completed lattice releases to excite
// its separate, finite planetary modal layer.
//
// One instance is intended per Cryosphere voice. It allocates no memory and
// uses two xorshift samples, simple one-pole differences, and two irregular
// event clocks per process call, keeping 64-voice rendering practical.
class QuasicrystalIceModel {
public:
    void prepare(double sampleRate, uint32_t seed)
    {
        sampleRate_ = std::isfinite(sampleRate)
            ? std::clamp(sampleRate, 1000.0, 768000.0) : 48000.0;
        reset(seed);
    }

    // A parameterless reset deliberately replays the prepared seed. This is
    // useful for deterministic renders and differs from merely clearing the
    // filters while allowing an RNG stream to continue.
    void reset()
    {
        reset(initialSeed_);
    }

    void reset(uint32_t seed)
    {
        initialSeed_ = seed ? seed : 1u;
        rng_ = initialSeed_;
        frontEnergy_ = 0.0f;
        avalancheEnergy_ = 0.0f;
        cascadeBias_ = 0.0f;
        frontCountdownSamples_ = 0.0f;
        avalancheCountdownSamples_ = 0.0f;
        phaseEnvelope_ = 0.0f;
        frontEnvelope_ = 0.0f;
        avalancheEnvelope_ = 0.0f;
        axisALow_ = 0.0f;
        axisAHigh_ = 0.0f;
        axisBLow_ = 0.0f;
        axisBHigh_ = 0.0f;
        detailLow_ = 0.0f;
        drift_ = 0.0f;
        previousWhite_ = 0.0f;
        dcEstimate_ = 0.0f;
        activity_ = 0.0f;
        orientation_ = 0.0f;
        orientationTarget_ = randomSigned();
        orientationCountdownSamples_ = static_cast<float>(sampleRate_)
            * (0.12f + randomUnit() * 0.71f);
    }

    // Inject energy from a scored entity. `cascade` is a per-entity gesture,
    // not an audio-rate control: low values make a short wandering front;
    // high values distribute the release across many asynchronous defects.
    void excite(float strength, float cascade = 0.5f)
    {
        strength = finiteClamp(strength, 0.0f, 0.0f, 2.0f);
        cascade = finiteClamp(cascade, 0.5f, 0.0f, 1.0f);
        if (strength <= 1.0e-5f) return;

        frontEnergy_ = std::min(3.5f, frontEnergy_
            + strength * (0.42f + cascade * 0.88f));
        avalancheEnergy_ = std::min(2.5f, avalancheEnergy_
            + strength * cascade * 0.055f);
        cascadeBias_ = std::max(cascadeBias_, cascade);

        // Do not turn the excitation itself into an impact. Its first audible
        // change arrives after a seed-dependent traversal delay.
        if (frontCountdownSamples_ <= 0.0f) {
            frontCountdownSamples_ = static_cast<float>(sampleRate_)
                * (0.0008f + randomUnit() * randomUnit() * 0.021f);
        }
        if (avalancheCountdownSamples_ <= 0.0f) {
            avalancheCountdownSamples_ = static_cast<float>(sampleRate_)
                * (0.0012f + randomUnit() * 0.012f);
        }
    }

    QuasicrystalIceOutput process(QuasicrystalIceParams params)
    {
        sanitize(params);
        QuasicrystalIceOutput output {};
        const float sr = static_cast<float>(sampleRate_);

        // Two decorrelated stochastic fluxes encounter domains oriented on
        // different axes. Anisotropy separates their filter bandwidths rather
        // than introducing a periodic spatial rotation or a tuned frequency.
        const float whiteA = randomSigned();
        const float whiteB = randomSigned();
        const float domainCenter = 190.0f + params.brittleness * 760.0f
            + (1.0f - params.scale) * 430.0f;
        const float separation = params.anisotropy;
        const float aLowHz = domainCenter
            * (0.24f - separation * 0.08f);
        const float aHighHz = domainCenter
            * (1.05f + separation * 0.38f);
        const float bLowHz = domainCenter
            * (0.24f + separation * 0.22f);
        const float bHighHz = domainCenter
            * (1.05f + separation * 1.72f);
        axisALow_ += (whiteA - axisALow_)
            * onePoleCoefficient(aLowHz, sr);
        axisAHigh_ += (whiteA - axisAHigh_)
            * onePoleCoefficient(aHighHz, sr);
        axisBLow_ += (whiteB - axisBLow_)
            * onePoleCoefficient(bLowHz, sr);
        axisBHigh_ += (whiteB - axisBHigh_)
            * onePoleCoefficient(bHighHz, sr);
        detailLow_ += (whiteB - detailLow_)
            * onePoleCoefficient(2100.0f + params.brittleness * 7800.0f,
                sr);
        drift_ += (whiteA - drift_)
            * onePoleCoefficient(0.13f + params.phaseMobility * 1.9f, sr);

        orientationCountdownSamples_ -= 1.0f;
        if (orientationCountdownSamples_ <= 0.0f) {
            orientationTarget_ = randomSigned();
            const float dwell = (0.075f + params.scale * 0.82f)
                * (0.28f + randomUnit() * 1.72f);
            orientationCountdownSamples_ = sr * dwell;
        }
        orientation_ += (orientationTarget_ - orientation_)
            * onePoleCoefficient(0.32f + params.heterogeneity * 2.4f, sr);

        const float phaseFlux = params.strainRate * params.phaseMobility;
        const float phaseTarget = phaseFlux
            * (0.62f + std::fabs(drift_) * 1.8f)
            * (1.0f - std::min(0.48f, frontEnergy_ * 0.12f));
        const float phaseRate = phaseTarget > phaseEnvelope_
            ? 0.38f + params.phaseMobility * 2.2f
            : 0.09f + params.damping * 0.62f;
        phaseEnvelope_ += (phaseTarget - phaseEnvelope_)
            * onePoleCoefficient(phaseRate, sr);

        const float orientationAmount = std::clamp(0.5f
            + orientation_ * params.anisotropy * 0.44f, 0.04f, 0.96f);
        const float axisA = axisAHigh_ - axisALow_;
        const float axisB = axisBHigh_ - axisBLow_;
        const float orientedBoundary = axisA * (1.0f - orientationAmount)
            + axisB * orientationAmount;
        output.phaseBoundary = orientedBoundary * phaseEnvelope_
            * (0.26f + params.heterogeneity * 0.28f);

        frontCountdownSamples_ -= 1.0f;
        if (frontEnergy_ > 0.002f && frontCountdownSamples_ <= 0.0f) {
            const float irregular = randomUnit();
            const float releaseLimit = (0.035f + irregular * 0.15f)
                * (0.72f + params.frontSpeed * 0.58f)
                * (0.72f + params.heterogeneity * 0.46f);
            const float released = std::min(frontEnergy_, releaseLimit);
            frontEnergy_ = std::max(0.0f, frontEnergy_ - released);
            frontEnvelope_ = std::min(1.8f, frontEnvelope_
                + released * (1.5f + params.brittleness * 2.2f));
            avalancheEnergy_ = std::min(2.5f, avalancheEnergy_
                + released * params.branching
                    * (0.58f + cascadeBias_ * 1.28f));
            output.frontAdvanced = true;

            // The squared random term prevents a recognisable event cadence;
            // domain scale and front speed change the statistical range only.
            const float jitter = 0.12f + randomUnit() * randomUnit() * 2.6f;
            const float interval = (0.0035f + params.scale * 0.038f)
                * (1.32f - params.frontSpeed * 0.92f) * jitter;
            frontCountdownSamples_ = sr * std::max(0.0007f, interval);
        }

        avalancheCountdownSamples_ -= 1.0f;
        if (avalancheEnergy_ > 0.001f
            && avalancheCountdownSamples_ <= 0.0f) {
            const float defect = randomUnit();
            const float released = std::min(avalancheEnergy_,
                0.004f + defect * defect
                    * (0.025f + params.heterogeneity * 0.052f));
            avalancheEnergy_ = std::max(0.0f,
                avalancheEnergy_ - released);
            avalancheEnvelope_ = std::min(1.6f, avalancheEnvelope_
                + released * (4.0f + params.brittleness * 6.2f));
            output.avalancheAdvanced = true;

            const float branchDrive = std::clamp(params.branching
                * (0.46f + cascadeBias_ * 0.72f), 0.0f, 1.0f);
            const float interval = (0.00045f
                + randomUnit() * randomUnit() * 0.010f)
                * (1.22f - branchDrive * 0.82f)
                * (0.72f + params.scale * 0.74f);
            avalancheCountdownSamples_ = sr
                * std::max(0.00025f, interval);
        }

        const float frontDecaySeconds = (0.006f + params.scale * 0.046f)
            * (1.18f - params.damping * 0.62f);
        const float avalancheDecaySeconds = (0.00055f
            + (1.0f - params.brittleness) * 0.0058f)
            * (1.12f - params.damping * 0.54f);
        frontEnvelope_ *= 1.0f
            - decayCoefficient(frontDecaySeconds, sr);
        avalancheEnvelope_ *= 1.0f
            - decayCoefficient(avalancheDecaySeconds, sr);
        cascadeBias_ *= 1.0f
            - decayCoefficient(0.18f + params.scale * 1.4f, sr);

        const float fine = whiteB - detailLow_;
        const float derivative = whiteA - previousWhite_;
        previousWhite_ = whiteA;
        output.fractureFront = frontEnvelope_
            * (axisB * (0.54f + params.anisotropy * 0.32f)
                + fine * (0.10f + params.brittleness * 0.30f));
        output.latticeAvalanche = avalancheEnvelope_
            * (fine * (0.62f + params.brittleness * 0.34f)
                + derivative * (0.08f + params.heterogeneity * 0.20f));

        const float mixed = output.phaseBoundary
            + output.fractureFront * 0.70f
            + output.latticeAvalanche * 0.42f;
        const float bounded = mixed
            / (1.0f + std::fabs(mixed) * 0.72f);
        dcEstimate_ += (bounded - dcEstimate_)
            * onePoleCoefficient(14.0f, sr);
        const float sample = (bounded - dcEstimate_) * 0.62f;

        const float instantActivity = std::clamp(std::max({
            phaseEnvelope_ * 0.34f, frontEnvelope_, avalancheEnvelope_,
            std::min(1.0f, frontEnergy_ * 0.24f),
            std::min(1.0f, avalancheEnergy_ * 0.42f) }), 0.0f, 1.0f);
        const float activityRate = instantActivity > activity_
            ? 24.0f : 1.8f;
        activity_ += (instantActivity - activity_)
            * onePoleCoefficient(activityRate, sr);

        if (!std::isfinite(sample) || !std::isfinite(activity_)) {
            clearSignalPath();
            return {};
        }
        output.sample = sample;
        output.activity = std::clamp(activity_, 0.0f, 1.0f);
        return output;
    }

    bool active() const
    {
        return activity_ > 1.0e-4f || frontEnergy_ > 1.0e-4f
            || avalancheEnergy_ > 1.0e-4f;
    }

private:
    static float finiteClamp(float value, float fallback,
        float minimum, float maximum)
    {
        return std::clamp(std::isfinite(value) ? value : fallback,
            minimum, maximum);
    }

    static void sanitize(QuasicrystalIceParams& params)
    {
        params.strainRate = finiteClamp(params.strainRate, 0.0f, 0.0f, 1.0f);
        params.phaseMobility = finiteClamp(
            params.phaseMobility, 0.5f, 0.0f, 1.0f);
        params.frontSpeed = finiteClamp(params.frontSpeed, 0.5f, 0.0f, 1.0f);
        params.branching = finiteClamp(params.branching, 0.5f, 0.0f, 1.0f);
        params.anisotropy = finiteClamp(params.anisotropy, 0.5f, 0.0f, 1.0f);
        params.heterogeneity = finiteClamp(
            params.heterogeneity, 0.5f, 0.0f, 1.0f);
        params.scale = finiteClamp(params.scale, 0.5f, 0.0f, 1.0f);
        params.brittleness = finiteClamp(
            params.brittleness, 0.5f, 0.0f, 1.0f);
        params.damping = finiteClamp(params.damping, 0.5f, 0.0f, 1.0f);
    }

    static float onePoleCoefficient(float cutoffHz, float sampleRate)
    {
        constexpr float kTwoPi = 6.28318530717958647692f;
        const float x = kTwoPi
            * std::clamp(cutoffHz, 0.001f, sampleRate * 0.44f)
            / sampleRate;
        // A stable bilinear-like approximation avoids transcendental work in
        // each of 64 voices while remaining monotonic over the useful range.
        return x / (1.0f + x);
    }

    static float decayCoefficient(float seconds, float sampleRate)
    {
        return std::clamp(1.0f
            / (std::max(0.00005f, seconds) * sampleRate), 0.0f, 1.0f);
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

    void clearSignalPath()
    {
        frontEnergy_ = 0.0f;
        avalancheEnergy_ = 0.0f;
        cascadeBias_ = 0.0f;
        phaseEnvelope_ = 0.0f;
        frontEnvelope_ = 0.0f;
        avalancheEnvelope_ = 0.0f;
        axisALow_ = axisAHigh_ = 0.0f;
        axisBLow_ = axisBHigh_ = 0.0f;
        detailLow_ = drift_ = previousWhite_ = 0.0f;
        dcEstimate_ = activity_ = 0.0f;
    }

    double sampleRate_ = 48000.0;
    uint32_t initialSeed_ = 1u;
    uint32_t rng_ = 1u;

    float frontEnergy_ = 0.0f;
    float avalancheEnergy_ = 0.0f;
    float cascadeBias_ = 0.0f;
    float frontCountdownSamples_ = 0.0f;
    float avalancheCountdownSamples_ = 0.0f;
    float phaseEnvelope_ = 0.0f;
    float frontEnvelope_ = 0.0f;
    float avalancheEnvelope_ = 0.0f;

    float axisALow_ = 0.0f;
    float axisAHigh_ = 0.0f;
    float axisBLow_ = 0.0f;
    float axisBHigh_ = 0.0f;
    float detailLow_ = 0.0f;
    float drift_ = 0.0f;
    float previousWhite_ = 0.0f;
    float dcEstimate_ = 0.0f;
    float activity_ = 0.0f;
    float orientation_ = 0.0f;
    float orientationTarget_ = 0.0f;
    float orientationCountdownSamples_ = 0.0f;
};

} // namespace s3g
