#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace s3g {

enum class PlanetaryThermalMode : uint32_t {
    SilicateCells = 0u,
    SupercriticalFront,
    SulfurSlugVents,
    ThermoelasticLattice,
};

struct PlanetaryThermalParams {
    PlanetaryThermalMode mode = PlanetaryThermalMode::SilicateCells;
    float heat = 0.0f;
    float flux = 0.5f;
    float activity = 0.0f;
    float drive = 0.0f;
    float propagation = 0.0f;
    float consequence = 0.0f;
    float instability = 0.5f;
    float pressure = 0.5f;
    float density = 0.5f;
    float brittleness = 0.5f;
    float damping = 0.5f;
    float branching = 0.5f;
    float scale = 0.5f;
    float detail = 0.5f;
};

struct PlanetaryThermalOutput {
    float sample = 0.0f;
    float activity = 0.0f;
    float overturn = 0.0f;
    float reactionFront = 0.0f;
    float ventFlow = 0.0f;
    float latticeRelease = 0.0f;
    bool blisterReleased = false;
    bool frontAdvanced = false;
    bool slugReleased = false;
    bool latticeFrontAdvanced = false;
    bool avalancheAdvanced = false;
};

// Four speculative planetary material processes sharing one small, fixed-size
// state. The modes model thermal reservoirs, pressure-density lag, two-phase
// slug flow, and thermoelastic defect propagation. Their audible output is
// made entirely from filtered stochastic flux and aperiodic release envelopes:
// there are no oscillator banks, tuned delays, or impact-body sinusoids inside
// this source. The encoder may use completed releases to excite its separate,
// finite planetary modal layer.
class PlanetaryThermalModel {
public:
    void prepare(double sampleRate, uint32_t seed)
    {
        sampleRate_ = std::isfinite(sampleRate)
            ? std::clamp(sampleRate, 1000.0, 768000.0) : 48000.0;
        reset(seed);
    }

    void reset()
    {
        reset(initialSeed_);
    }

    void reset(uint32_t seed)
    {
        initialSeed_ = seed ? seed : 1u;
        rng_ = initialSeed_;
        modeInitialized_ = false;
        clearPhysicalPath();
        infraA_ = lowA_ = bodyA_ = presenceA_ = airA_ = 0.0f;
        infraB_ = lowB_ = bodyB_ = presenceB_ = airB_ = 0.0f;
        drift_ = previousWhite_ = dcEstimate_ = activity_ = 0.0f;
    }

    // Excitations are causal score/material gestures, not audio impulses. A
    // traversal or pressure-build delay always precedes an audible release.
    void excite(PlanetaryThermalMode mode, float strength, float cascade,
        bool consequence = false)
    {
        ensureMode(mode);
        strength = finiteUnit(strength, 0.0f);
        cascade = finiteUnit(cascade, 0.5f);
        if (strength <= 1.0e-5f) return;

        switch (mode) {
        case PlanetaryThermalMode::SilicateCells:
            storedEnergy_ = std::min(3.0f, storedEnergy_
                + strength * (0.18f + cascade * 0.34f));
            secondaryEnergy_ = std::min(2.0f, secondaryEnergy_
                + strength * (consequence ? 0.34f : 0.06f));
            break;
        case PlanetaryThermalMode::SupercriticalFront:
            storedEnergy_ = std::min(3.0f, storedEnergy_
                + strength * (0.24f + cascade * 0.48f));
            secondaryEnergy_ = std::min(2.0f, secondaryEnergy_
                + strength * (0.08f + (consequence ? 0.42f : 0.0f)));
            break;
        case PlanetaryThermalMode::SulfurSlugVents:
            reservoir_ = std::min(2.2f, reservoir_
                + strength * (0.10f + cascade * 0.18f));
            storedEnergy_ = std::min(3.0f, storedEnergy_
                + strength * (0.28f + (consequence ? 0.52f : 0.0f)));
            break;
        case PlanetaryThermalMode::ThermoelasticLattice:
            storedEnergy_ = std::min(4.0f, storedEnergy_
                + strength * (0.46f + cascade * 0.86f));
            secondaryEnergy_ = std::min(3.0f, secondaryEnergy_
                + strength * cascade * (consequence ? 0.72f : 0.22f));
            break;
        }
        cascadeBias_ = std::max(cascadeBias_, cascade);
        if (primaryCountdown_ <= 0.0f) {
            primaryCountdown_ = static_cast<float>(sampleRate_)
                * (0.0015f + randomUnit() * randomUnit() * 0.038f);
        }
    }

    PlanetaryThermalOutput process(PlanetaryThermalParams params)
    {
        sanitize(params);
        ensureMode(params.mode);
        PlanetaryThermalOutput output {};
        const float sr = static_cast<float>(sampleRate_);
        const float dt = 1.0f / sr;
        const float whiteA = randomSigned();
        const float whiteB = randomSigned();

        float infraHz = 2.0f;
        float lowHz = 54.0f;
        float bodyHz = 420.0f;
        float presenceHz = 2400.0f;
        float airHz = 8200.0f;
        switch (params.mode) {
        case PlanetaryThermalMode::SilicateCells:
            infraHz = 0.42f + (1.0f - params.scale) * 1.8f;
            lowHz = 18.0f + (1.0f - params.density) * 46.0f;
            bodyHz = 92.0f + params.instability * 430.0f;
            presenceHz = 680.0f + params.brittleness * 2600.0f;
            airHz = 3600.0f + params.detail * 7200.0f;
            break;
        case PlanetaryThermalMode::SupercriticalFront:
            infraHz = 1.4f + params.instability * 2.8f;
            lowHz = 34.0f + (1.0f - params.density) * 92.0f;
            bodyHz = 180.0f + params.instability * 720.0f;
            presenceHz = 760.0f + (1.0f - params.damping) * 2800.0f;
            airHz = 2600.0f + (1.0f - params.density) * 6800.0f;
            break;
        case PlanetaryThermalMode::SulfurSlugVents:
            infraHz = 2.2f + params.pressure * 3.8f;
            lowHz = 48.0f + (1.0f - params.scale) * 130.0f;
            bodyHz = 260.0f + params.pressure * 940.0f;
            presenceHz = 1300.0f + params.instability * 3900.0f;
            airHz = 4200.0f + params.detail * 8200.0f;
            break;
        case PlanetaryThermalMode::ThermoelasticLattice:
            infraHz = 3.4f + params.scale * 4.0f;
            lowHz = 86.0f + (1.0f - params.scale) * 220.0f;
            bodyHz = 430.0f + params.brittleness * 1450.0f;
            presenceHz = 1900.0f + params.brittleness * 5200.0f;
            airHz = 6200.0f + params.detail * 10800.0f;
            break;
        }
        updateNoiseChain(whiteA, infraHz, lowHz, bodyHz, presenceHz,
            airHz, infraA_, lowA_, bodyA_, presenceA_, airA_, sr);
        updateNoiseChain(whiteB, infraHz * 1.23f, lowHz * 1.31f,
            bodyHz * 0.79f, presenceHz * 1.43f, airHz * 0.88f,
            infraB_, lowB_, bodyB_, presenceB_, airB_, sr);
        drift_ += (whiteA - drift_) * onePoleCoefficient(
            0.09f + params.instability * 2.6f, sr);

        const float pressureA = (lowA_ - infraA_) * 5.8f;
        const float pressureB = (lowB_ - infraB_) * 5.2f;
        const float bodyA = bodyA_ - lowA_;
        const float bodyB = bodyB_ - lowB_;
        const float shearA = presenceA_ - bodyA_;
        const float shearB = presenceB_ - bodyB_;
        const float fineA = whiteA - airA_;
        const float fineB = whiteB - airB_;
        const float derivative = whiteA - previousWhite_;
        previousWhite_ = whiteA;

        primaryCountdown_ -= 1.0f;
        secondaryCountdown_ -= 1.0f;
        switch (params.mode) {
        case PlanetaryThermalMode::SilicateCells:
            processSilicate(params, output, pressureA, pressureB, bodyA,
                bodyB, shearA, fineA, derivative, dt, sr);
            break;
        case PlanetaryThermalMode::SupercriticalFront:
            processSupercritical(params, output, pressureA, pressureB,
                bodyA, bodyB, shearA, shearB, fineA, dt, sr);
            break;
        case PlanetaryThermalMode::SulfurSlugVents:
            processSulfur(params, output, pressureA, pressureB, bodyA,
                bodyB, shearA, fineA, fineB, dt, sr);
            break;
        case PlanetaryThermalMode::ThermoelasticLattice:
            processLattice(params, output, bodyA, bodyB, shearA, shearB,
                fineA, fineB, derivative, dt, sr);
            break;
        }

        const float mixed = output.overturn + output.reactionFront
            + output.ventFlow + output.latticeRelease;
        const float bounded = mixed / (1.0f + std::fabs(mixed) * 0.78f);
        dcEstimate_ += (bounded - dcEstimate_)
            * onePoleCoefficient(11.0f, sr);
        const float sample = (bounded - dcEstimate_) * 0.72f;
        const float instantActivity = std::clamp(std::max({
            std::fabs(reservoir_) * 0.28f,
            std::fabs(primaryEnvelope_), std::fabs(secondaryEnvelope_),
            std::min(1.0f, storedEnergy_ * 0.34f),
            std::min(1.0f, secondaryEnergy_ * 0.42f) }), 0.0f, 1.0f);
        const float activityHz = instantActivity > activity_ ? 22.0f : 1.4f;
        activity_ += (instantActivity - activity_)
            * onePoleCoefficient(activityHz, sr);
        cascadeBias_ *= 1.0f - decayCoefficient(1.8f, sr);

        if (!std::isfinite(sample) || !std::isfinite(activity_)) {
            clearPhysicalPath();
            dcEstimate_ = activity_ = 0.0f;
            return {};
        }
        output.sample = sample;
        output.activity = std::clamp(activity_, 0.0f, 1.0f);
        return output;
    }

private:
    static float finiteUnit(float value, float fallback)
    {
        return std::clamp(std::isfinite(value) ? value : fallback,
            0.0f, 1.0f);
    }

    static void sanitize(PlanetaryThermalParams& params)
    {
        const auto rawMode = std::min<uint32_t>(
            static_cast<uint32_t>(params.mode), 3u);
        params.mode = static_cast<PlanetaryThermalMode>(rawMode);
        params.heat = finiteUnit(params.heat, 0.0f);
        params.flux = finiteUnit(params.flux, 0.5f);
        params.activity = finiteUnit(params.activity, 0.0f);
        params.drive = finiteUnit(params.drive, 0.0f);
        params.propagation = finiteUnit(params.propagation, 0.0f);
        params.consequence = finiteUnit(params.consequence, 0.0f);
        params.instability = finiteUnit(params.instability, 0.5f);
        params.pressure = finiteUnit(params.pressure, 0.5f);
        params.density = finiteUnit(params.density, 0.5f);
        params.brittleness = finiteUnit(params.brittleness, 0.5f);
        params.damping = finiteUnit(params.damping, 0.5f);
        params.branching = finiteUnit(params.branching, 0.5f);
        params.scale = finiteUnit(params.scale, 0.5f);
        params.detail = finiteUnit(params.detail, 0.5f);
    }

    void processSilicate(const PlanetaryThermalParams& p,
        PlanetaryThermalOutput& out, float pressureA, float pressureB,
        float bodyA, float bodyB, float shear, float fine, float derivative,
        float dt, float sr)
    {
        const float target = p.heat * (0.24f + p.flux * 0.76f)
            * (0.20f + p.activity * 0.80f);
        reservoir_ += (target - reservoir_)
            * onePoleCoefficient(0.16f + p.instability * 0.72f, sr);
        lag_ += (reservoir_ - lag_)
            * onePoleCoefficient(0.025f + (1.0f - p.density) * 0.11f, sr);
        const float mismatch = std::max(0.0f, reservoir_ - lag_);
        storedEnergy_ = std::min(3.0f, storedEnergy_ + dt * mismatch
            * (0.08f + p.pressure * 0.46f + p.instability * 0.22f));
        if (storedEnergy_ > 0.018f && primaryCountdown_ <= 0.0f) {
            const float released = std::min(storedEnergy_,
                0.014f + randomUnit() * (0.075f + p.pressure * 0.13f));
            storedEnergy_ -= released;
            primaryEnvelope_ = std::min(1.8f, primaryEnvelope_
                + released * (2.4f + p.pressure * 4.2f));
            secondaryEnergy_ = std::min(2.0f, secondaryEnergy_
                + released * p.brittleness * 0.72f);
            out.blisterReleased = true;
            primaryCountdown_ = sr * (0.018f + p.scale * 0.32f)
                * (0.18f + randomUnit() * randomUnit() * 2.4f);
        }
        if (secondaryEnergy_ > 0.006f && secondaryCountdown_ <= 0.0f) {
            const float released = std::min(secondaryEnergy_,
                0.004f + randomUnit() * 0.032f);
            secondaryEnergy_ -= released;
            secondaryEnvelope_ = std::min(1.5f,
                secondaryEnvelope_ + released * (4.0f + p.brittleness * 5.0f));
            secondaryCountdown_ = sr * (0.002f
                + randomUnit() * randomUnit() * 0.036f);
        }
        const float overturn = std::tanh(pressureA
                * (0.54f + p.density * 1.24f)
            + pressureB * 0.28f + bodyA * (0.32f + p.instability * 0.56f))
            * reservoir_ * (0.28f + p.flux * 0.42f);
        const float blister = primaryEnvelope_
            * (bodyB * 0.72f + shear * 0.24f + pressureB * 0.18f);
        const float skin = secondaryEnvelope_
            * (fine * (0.34f + p.brittleness * 0.42f)
                + derivative * 0.12f);
        out.overturn = overturn + blister * 0.74f + skin * 0.38f;
        primaryEnvelope_ *= 1.0f - decayCoefficient(
            0.045f + p.scale * 0.18f, sr);
        secondaryEnvelope_ *= 1.0f - decayCoefficient(
            0.0012f + (1.0f - p.brittleness) * 0.012f, sr);
    }

    void processSupercritical(const PlanetaryThermalParams& p,
        PlanetaryThermalOutput& out, float pressureA, float pressureB,
        float bodyA, float bodyB, float shearA, float shearB, float fine,
        float dt, float sr)
    {
        const float target = p.heat * (0.16f + p.drive * 0.84f)
            * (0.36f + p.flux * 0.64f);
        reservoir_ += (target - reservoir_)
            * onePoleCoefficient(0.72f + p.instability * 5.4f, sr);
        lag_ += (reservoir_ - lag_)
            * onePoleCoefficient(0.08f + (1.0f - p.density) * 0.68f, sr);
        const float folding = std::fabs(reservoir_ - lag_)
            + std::fabs(drift_) * (0.12f + p.instability * 0.66f);
        storedEnergy_ = std::min(3.0f, storedEnergy_ + dt * folding
            * (0.20f + p.propagation * 0.82f));
        if (storedEnergy_ > 0.012f && primaryCountdown_ <= 0.0f) {
            const float released = std::min(storedEnergy_,
                0.010f + randomUnit() * (0.045f + p.instability * 0.085f));
            storedEnergy_ -= released;
            primaryEnvelope_ = std::min(1.6f, primaryEnvelope_
                + released * (3.6f + p.pressure * 3.4f));
            secondaryEnergy_ = std::min(2.0f, secondaryEnergy_
                + released * (0.26f + p.branching * 0.88f));
            out.frontAdvanced = true;
            primaryCountdown_ = sr * (0.009f + p.scale * 0.085f)
                * (0.16f + randomUnit() * randomUnit() * 2.8f);
        }
        if (secondaryEnergy_ > 0.004f && secondaryCountdown_ <= 0.0f) {
            const float released = std::min(secondaryEnergy_,
                0.003f + randomUnit() * 0.018f);
            secondaryEnergy_ -= released;
            secondaryEnvelope_ = std::min(1.4f,
                secondaryEnvelope_ + released * 7.2f);
            secondaryCountdown_ = sr * (0.0015f
                + randomUnit() * randomUnit() * 0.022f);
        }
        const float densitySheet = std::tanh(pressureA
                * (0.72f + p.density * 1.52f)
            + pressureB * 0.34f + bodyA * (0.44f + p.instability * 0.62f))
            * reservoir_;
        // Dense fluids absorb short wavelengths as reaction density rises.
        const float absorbedFold = bodyB * (1.0f - p.density * 0.72f)
            + shearA * (1.0f - p.damping) * 0.34f;
        const float cellular = primaryEnvelope_
            * (absorbedFold * 0.76f + pressureB * 0.32f)
            + secondaryEnvelope_ * (shearB * 0.34f
                + fine * (1.0f - p.density) * 0.16f);
        out.reactionFront = densitySheet * 0.62f + cellular * 0.72f;
        primaryEnvelope_ *= 1.0f - decayCoefficient(
            0.018f + p.density * 0.085f, sr);
        secondaryEnvelope_ *= 1.0f - decayCoefficient(
            0.003f + p.damping * 0.014f, sr);
    }

    void processSulfur(const PlanetaryThermalParams& p,
        PlanetaryThermalOutput& out, float pressureA, float pressureB,
        float bodyA, float bodyB, float shear, float fineA, float fineB,
        float dt, float sr)
    {
        const float charging = p.pressure * (0.16f + p.flux * 0.84f)
            * (0.08f + p.drive * 0.72f + p.activity * 0.20f);
        reservoir_ = std::min(2.2f, reservoir_ + charging * dt
            * (0.14f + p.density * 0.48f));
        reservoir_ = std::max(0.0f, reservoir_ - dt
            * (0.008f + (1.0f - p.density) * 0.026f));
        lag_ += ((drift_ > 0.0f ? 1.0f : 0.0f) - lag_)
            * onePoleCoefficient(0.18f + p.instability * 1.8f, sr);
        const float releaseThreshold = 0.075f + p.scale * 0.18f
            + lag_ * 0.08f;
        if (reservoir_ + storedEnergy_ > releaseThreshold
            && primaryCountdown_ <= 0.0f) {
            const float available = reservoir_ + storedEnergy_;
            const float released = std::min(available,
                0.045f + randomUnit() * (0.12f + p.pressure * 0.22f));
            const float fromStored = std::min(storedEnergy_, released);
            storedEnergy_ -= fromStored;
            reservoir_ = std::max(0.0f,
                reservoir_ - (released - fromStored));
            primaryEnvelope_ = std::min(1.9f, primaryEnvelope_
                + released * (2.8f + p.pressure * 4.4f));
            secondaryEnergy_ = std::min(2.4f, secondaryEnergy_
                + released * (0.46f + p.instability * 0.86f));
            out.slugReleased = true;
            primaryCountdown_ = sr * (0.028f + p.scale * 0.28f)
                * (0.18f + randomUnit() * randomUnit() * 2.9f);
        }
        if (secondaryEnergy_ > 0.003f && secondaryCountdown_ <= 0.0f) {
            const float released = std::min(secondaryEnergy_,
                0.002f + randomUnit() * 0.020f);
            secondaryEnergy_ -= released;
            secondaryEnvelope_ = std::min(1.5f,
                secondaryEnvelope_ + released * 8.4f);
            secondaryCountdown_ = sr * (0.0007f
                + randomUnit() * randomUnit() * 0.013f);
        }
        const float preRelease = std::tanh(pressureA
                * (0.38f + reservoir_ * 1.8f) + pressureB * 0.22f
            + bodyA * reservoir_ * 0.74f)
            * (0.12f + reservoir_ * 0.62f);
        const float slug = primaryEnvelope_
            * (pressureB * 0.52f + bodyB * 0.64f + shear * 0.18f);
        const float foam = secondaryEnvelope_
            * (fineA * 0.46f + fineB * 0.34f + shear * 0.24f);
        out.ventFlow = preRelease + slug * 0.82f + foam * 0.42f;
        primaryEnvelope_ *= 1.0f - decayCoefficient(
            0.026f + p.density * 0.16f, sr);
        secondaryEnvelope_ *= 1.0f - decayCoefficient(
            0.001f + (1.0f - p.instability) * 0.009f, sr);
    }

    void processLattice(const PlanetaryThermalParams& p,
        PlanetaryThermalOutput& out, float bodyA, float bodyB,
        float shearA, float shearB, float fineA, float fineB,
        float derivative, float dt, float sr)
    {
        const float target = p.heat * (0.18f + p.drive * 0.82f);
        reservoir_ += (target - reservoir_)
            * onePoleCoefficient(0.10f + p.instability * 0.84f, sr);
        lag_ += (reservoir_ - lag_)
            * onePoleCoefficient(0.018f + p.damping * 0.12f, sr);
        const float thermoelastic = std::fabs(reservoir_ - lag_)
            * (0.24f + p.brittleness * 0.96f);
        storedEnergy_ = std::min(4.0f, storedEnergy_ + dt * thermoelastic
            * (0.18f + p.propagation * 0.92f));
        if (storedEnergy_ > 0.010f && primaryCountdown_ <= 0.0f) {
            const float released = std::min(storedEnergy_,
                0.008f + randomUnit() * randomUnit()
                    * (0.055f + p.brittleness * 0.13f));
            storedEnergy_ -= released;
            primaryEnvelope_ = std::min(1.7f, primaryEnvelope_
                + released * (3.0f + p.brittleness * 5.2f));
            secondaryEnergy_ = std::min(3.0f, secondaryEnergy_
                + released * p.branching
                    * (0.62f + cascadeBias_ * 1.42f));
            out.latticeFrontAdvanced = true;
            primaryCountdown_ = sr * (0.003f + p.scale * 0.052f)
                * (0.10f + randomUnit() * randomUnit() * 2.8f);
        }
        if (secondaryEnergy_ > 0.001f && secondaryCountdown_ <= 0.0f) {
            const float released = std::min(secondaryEnergy_,
                0.0015f + randomUnit() * randomUnit()
                    * (0.014f + p.branching * 0.034f));
            secondaryEnergy_ -= released;
            secondaryEnvelope_ = std::min(1.7f, secondaryEnvelope_
                + released * (7.0f + p.brittleness * 7.0f));
            out.avalancheAdvanced = true;
            secondaryCountdown_ = sr * (0.00035f
                + randomUnit() * randomUnit() * 0.011f)
                * (1.22f - p.branching * 0.78f);
        }
        const float domainCreep = (bodyA * 0.58f + bodyB * 0.34f)
            * reservoir_ * (0.08f + p.activity * 0.24f);
        const float front = primaryEnvelope_
            * (shearA * (0.54f + p.brittleness * 0.28f)
                + shearB * 0.28f + fineA * 0.12f);
        const float defects = secondaryEnvelope_
            * (fineB * (0.58f + p.brittleness * 0.34f)
                + derivative * (0.08f + p.branching * 0.18f));
        out.latticeRelease = domainCreep + front * 0.72f + defects * 0.44f;
        primaryEnvelope_ *= 1.0f - decayCoefficient(
            0.005f + p.scale * 0.052f, sr);
        secondaryEnvelope_ *= 1.0f - decayCoefficient(
            0.00045f + (1.0f - p.brittleness) * 0.0068f, sr);
    }

    static void updateNoiseChain(float white, float infraHz, float lowHz,
        float bodyHz, float presenceHz, float airHz, float& infra,
        float& low, float& body, float& presence, float& air, float sr)
    {
        infra += (white - infra) * onePoleCoefficient(infraHz, sr);
        low += (white - low) * onePoleCoefficient(lowHz, sr);
        body += (white - body) * onePoleCoefficient(bodyHz, sr);
        presence += (white - presence)
            * onePoleCoefficient(presenceHz, sr);
        air += (white - air) * onePoleCoefficient(airHz, sr);
    }

    static float onePoleCoefficient(float cutoffHz, float sampleRate)
    {
        constexpr float kTwoPi = 6.28318530717958647692f;
        const float x = kTwoPi * std::clamp(
            cutoffHz, 0.001f, sampleRate * 0.44f) / sampleRate;
        return x / (1.0f + x);
    }

    static float decayCoefficient(float seconds, float sampleRate)
    {
        return std::clamp(1.0f
            / (std::max(0.00005f, seconds) * sampleRate), 0.0f, 1.0f);
    }

    void ensureMode(PlanetaryThermalMode mode)
    {
        if (modeInitialized_ && mode == mode_) return;
        mode_ = mode;
        modeInitialized_ = true;
        clearPhysicalPath();
    }

    void clearPhysicalPath()
    {
        reservoir_ = lag_ = storedEnergy_ = secondaryEnergy_ = 0.0f;
        primaryEnvelope_ = secondaryEnvelope_ = cascadeBias_ = 0.0f;
        primaryCountdown_ = secondaryCountdown_ = 0.0f;
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

    double sampleRate_ = 48000.0;
    uint32_t initialSeed_ = 1u;
    uint32_t rng_ = 1u;
    PlanetaryThermalMode mode_ = PlanetaryThermalMode::SilicateCells;
    bool modeInitialized_ = false;

    float infraA_ = 0.0f;
    float lowA_ = 0.0f;
    float bodyA_ = 0.0f;
    float presenceA_ = 0.0f;
    float airA_ = 0.0f;
    float infraB_ = 0.0f;
    float lowB_ = 0.0f;
    float bodyB_ = 0.0f;
    float presenceB_ = 0.0f;
    float airB_ = 0.0f;
    float drift_ = 0.0f;
    float previousWhite_ = 0.0f;
    float reservoir_ = 0.0f;
    float lag_ = 0.0f;
    float storedEnergy_ = 0.0f;
    float secondaryEnergy_ = 0.0f;
    float primaryEnvelope_ = 0.0f;
    float secondaryEnvelope_ = 0.0f;
    float cascadeBias_ = 0.0f;
    float primaryCountdown_ = 0.0f;
    float secondaryCountdown_ = 0.0f;
    float dcEstimate_ = 0.0f;
    float activity_ = 0.0f;
};

} // namespace s3g
