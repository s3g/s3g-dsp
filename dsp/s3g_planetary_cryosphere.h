#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace s3g {

enum class PlanetaryCryosphereMode : uint32_t {
    TidalShell = 0u,
    HydrocarbonDune = 1u,
    ReactiveBrine = 2u,
};

struct PlanetaryCryosphereParams {
    PlanetaryCryosphereMode mode = PlanetaryCryosphereMode::TidalShell;

    // The score supplies causal energy. These are intentionally separate
    // from the material controls so a quiet entity remains physically quiet.
    float drive = 0.0f;
    float propagation = 0.0f;
    float consequence = 0.0f;
    float aftermath = 0.0f;

    float mass = 0.5f;
    float mobility = 0.5f;
    float branching = 0.5f;
    float pressure = 0.5f;
    float cohesion = 0.5f;
    float porosity = 0.5f;
    float brightness = 0.5f;
    float damping = 0.5f;
};

struct PlanetaryCryosphereOutput {
    float sample = 0.0f;
    float activity = 0.0f;
    bool primaryEvent = false;
    bool secondaryEvent = false;
};

// Three deliberately non-modal planetary-material force processes. Tidal Shell is
// a viscoelastic load / propagating-rift / decompression system; Hydrocarbon
// Dune is cohesive shear flow with avalanche packets and triboelectric
// microfracture; Reactive Brine is a pore-pressure / freeze-seal /
// breakthrough cycle. All audible energy is filtered stochastic flux: there
// are no oscillators, tuned delays, resonator banks, or impact bodies inside
// this source. The encoder may use completed releases to excite its separate,
// finite planetary modal layer.
//
// One model is held per Cryosphere voice. It is deterministic for a given
// seed, allocation-free, and resets its physical state when its mode changes.
class PlanetaryCryosphereModel {
public:
    void prepare(double sampleRate, uint32_t seed)
    {
        sampleRate_ = std::isfinite(sampleRate)
            ? std::clamp(sampleRate, 1000.0, 768000.0) : 48000.0;
        initialSeed_ = seed ? seed : 1u;
        reset();
    }

    void reset()
    {
        rng_ = initialSeed_ ? initialSeed_ : 1u;
        modeInitialized_ = false;
        clearPhysicalState();
    }

    void excite(PlanetaryCryosphereMode mode, float strength,
        float cascade, bool consequence = false)
    {
        ensureMode(sanitizeMode(mode));
        strength = finiteClamp(strength, 0.0f, 0.0f, 2.0f);
        cascade = finiteClamp(cascade, 0.5f, 0.0f, 1.0f);
        if (strength <= 1.0e-6f) return;

        const float consequenceGain = consequence ? 1.34f : 1.0f;
        primaryEnergy_ = std::min(4.0f, primaryEnergy_
            + strength * consequenceGain * (0.24f + cascade * 0.76f));
        secondaryEnergy_ = std::min(3.0f, secondaryEnergy_
            + strength * (0.04f + cascade * 0.24f));
        load_ = std::min(1.5f, load_ + strength
            * (consequence ? 0.28f : 0.10f));
        cascadeBias_ = std::max(cascadeBias_, cascade);
        if (primaryCountdown_ <= 0.0f) {
            primaryCountdown_ = static_cast<float>(sampleRate_)
                * (0.001f + randomUnit() * randomUnit() * 0.030f);
        }
        if (secondaryCountdown_ <= 0.0f) {
            secondaryCountdown_ = static_cast<float>(sampleRate_)
                * (0.0007f + randomUnit() * 0.018f);
        }
    }

    PlanetaryCryosphereOutput process(PlanetaryCryosphereParams params)
    {
        sanitize(params);
        ensureMode(params.mode);
        switch (params.mode) {
        case PlanetaryCryosphereMode::TidalShell:
            return processTidalShell(params);
        case PlanetaryCryosphereMode::HydrocarbonDune:
            return processHydrocarbonDune(params);
        case PlanetaryCryosphereMode::ReactiveBrine:
            return processReactiveBrine(params);
        }
        return {};
    }

    bool active() const
    {
        return activity_ > 1.0e-4f || primaryEnergy_ > 1.0e-5f
            || secondaryEnergy_ > 1.0e-5f || primaryEnvelope_ > 1.0e-5f
            || secondaryEnvelope_ > 1.0e-5f || bedEnvelope_ > 1.0e-5f;
    }

private:
    static float finiteClamp(float value, float fallback,
        float minimum, float maximum)
    {
        return std::clamp(std::isfinite(value) ? value : fallback,
            minimum, maximum);
    }

    static PlanetaryCryosphereMode sanitizeMode(
        PlanetaryCryosphereMode mode)
    {
        const auto value = static_cast<uint32_t>(mode);
        return value <= static_cast<uint32_t>(
                PlanetaryCryosphereMode::ReactiveBrine)
            ? mode : PlanetaryCryosphereMode::TidalShell;
    }

    static void sanitize(PlanetaryCryosphereParams& params)
    {
        params.mode = sanitizeMode(params.mode);
#define S3G_PLANETARY_CLAMP(member, fallback) \
        params.member = finiteClamp(params.member, fallback, 0.0f, 1.0f)
        S3G_PLANETARY_CLAMP(drive, 0.0f);
        S3G_PLANETARY_CLAMP(propagation, 0.0f);
        S3G_PLANETARY_CLAMP(consequence, 0.0f);
        S3G_PLANETARY_CLAMP(aftermath, 0.0f);
        S3G_PLANETARY_CLAMP(mass, 0.5f);
        S3G_PLANETARY_CLAMP(mobility, 0.5f);
        S3G_PLANETARY_CLAMP(branching, 0.5f);
        S3G_PLANETARY_CLAMP(pressure, 0.5f);
        S3G_PLANETARY_CLAMP(cohesion, 0.5f);
        S3G_PLANETARY_CLAMP(porosity, 0.5f);
        S3G_PLANETARY_CLAMP(brightness, 0.5f);
        S3G_PLANETARY_CLAMP(damping, 0.5f);
#undef S3G_PLANETARY_CLAMP
    }

    static float onePoleCoefficient(float cutoffHz, float sampleRate)
    {
        constexpr float kTwoPi = 6.28318530717958647692f;
        const float x = kTwoPi
            * std::clamp(cutoffHz, 0.001f, sampleRate * 0.44f)
            / sampleRate;
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

    void ensureMode(PlanetaryCryosphereMode mode)
    {
        if (modeInitialized_ && mode_ == mode) return;
        mode_ = mode;
        modeInitialized_ = true;
        clearPhysicalState();
        threshold_ = 0.08f + randomUnit() * 0.26f;
        seal_ = 0.18f + randomUnit() * 0.48f;
    }

    void clearPhysicalState()
    {
        load_ = 0.0f;
        threshold_ = 0.16f;
        seal_ = 0.32f;
        charge_ = 0.0f;
        primaryEnergy_ = 0.0f;
        secondaryEnergy_ = 0.0f;
        cascadeBias_ = 0.0f;
        primaryCountdown_ = 0.0f;
        secondaryCountdown_ = 0.0f;
        primaryEnvelope_ = 0.0f;
        secondaryEnvelope_ = 0.0f;
        bedEnvelope_ = 0.0f;
        lowA_ = midA_ = highA_ = 0.0f;
        lowB_ = midB_ = highB_ = 0.0f;
        drift_ = previousWhite_ = 0.0f;
        dcEstimate_ = activity_ = 0.0f;
    }

    void filterFlux(float whiteA, float whiteB, float lowHz,
        float midHz, float highHz)
    {
        const float sr = static_cast<float>(sampleRate_);
        lowA_ += (whiteA - lowA_) * onePoleCoefficient(lowHz, sr);
        midA_ += (whiteA - midA_) * onePoleCoefficient(midHz, sr);
        highA_ += (whiteA - highA_) * onePoleCoefficient(highHz, sr);
        lowB_ += (whiteB - lowB_)
            * onePoleCoefficient(lowHz * 1.37f, sr);
        midB_ += (whiteB - midB_)
            * onePoleCoefficient(midHz * 0.73f, sr);
        highB_ += (whiteB - highB_)
            * onePoleCoefficient(highHz * 1.21f, sr);
    }

    PlanetaryCryosphereOutput finish(float mixed, float instantActivity,
        bool primary, bool secondary)
    {
        const float sr = static_cast<float>(sampleRate_);
        const float bounded = mixed / (1.0f + std::fabs(mixed) * 0.82f);
        dcEstimate_ += (bounded - dcEstimate_)
            * onePoleCoefficient(12.0f, sr);
        const float sample = (bounded - dcEstimate_) * 0.68f;
        const float target = std::clamp(instantActivity, 0.0f, 1.0f);
        activity_ += (target - activity_)
            * onePoleCoefficient(target > activity_ ? 22.0f : 1.4f, sr);
        cascadeBias_ *= 1.0f - decayCoefficient(0.24f, sr);
        if (!std::isfinite(sample) || !std::isfinite(activity_)) {
            clearPhysicalState();
            return {};
        }
        return { sample, std::clamp(activity_, 0.0f, 1.0f),
            primary, secondary };
    }

    PlanetaryCryosphereOutput processTidalShell(
        const PlanetaryCryosphereParams& params)
    {
        const float sr = static_cast<float>(sampleRate_);
        const float dt = 1.0f / sr;
        const float whiteA = randomSigned();
        const float whiteB = randomSigned();
        filterFlux(whiteA, whiteB,
            13.0f + (1.0f - params.mass) * 48.0f,
            170.0f + params.brightness * 760.0f,
            1800.0f + params.brightness * 5600.0f);
        drift_ += (whiteA - drift_)
            * onePoleCoefficient(0.025f + params.mobility * 0.22f, sr);

        // Viscoelastic lag stores tidal work; it never drives a sinusoidal
        // body. A threshold crossing feeds an irregular spatial rift front.
        load_ += dt * params.drive
            * (0.020f + params.pressure * 0.12f)
            * (0.42f + params.mass * 0.78f)
            * (0.74f + std::fabs(drift_) * 1.8f);
        load_ = std::max(0.0f, load_ - dt
            * (0.001f + params.damping * 0.005f));
        if (load_ >= threshold_) {
            const float release = std::min(load_,
                0.10f + params.propagation * 0.44f);
            load_ -= release;
            primaryEnergy_ = std::min(4.0f, primaryEnergy_
                + release * (1.2f + params.branching * 2.4f));
            threshold_ = 0.055f + randomUnit()
                * (0.18f + params.mass * 0.38f);
        }

        bool riftAdvanced = false;
        primaryCountdown_ -= 1.0f;
        if (primaryEnergy_ > 0.001f && primaryCountdown_ <= 0.0f) {
            const float released = std::min(primaryEnergy_,
                0.025f + randomUnit() * randomUnit()
                    * (0.12f + params.propagation * 0.16f));
            primaryEnergy_ -= released;
            primaryEnvelope_ = std::min(2.0f, primaryEnvelope_
                + released * (2.8f + params.pressure * 2.0f));
            secondaryEnergy_ = std::min(3.0f, secondaryEnergy_
                + released * (0.24f + params.branching * 1.35f));
            riftAdvanced = true;
            primaryCountdown_ = sr * std::max(0.0012f,
                (0.006f + params.mass * 0.072f)
                * (1.22f - params.propagation * 0.78f)
                * (0.16f + randomUnit() * randomUnit() * 2.5f));
        }

        bool decompressed = false;
        secondaryCountdown_ -= 1.0f;
        if (secondaryEnergy_ > 0.0005f && secondaryCountdown_ <= 0.0f) {
            const float released = std::min(secondaryEnergy_,
                0.003f + randomUnit() * randomUnit()
                    * (0.025f + params.pressure * 0.045f));
            secondaryEnergy_ -= released;
            secondaryEnvelope_ = std::min(1.6f,
                secondaryEnvelope_ + released * 8.0f);
            decompressed = true;
            secondaryCountdown_ = sr * (0.0008f
                + randomUnit() * randomUnit() * 0.026f);
        }

        const float bedTarget = params.drive
            * (0.16f + params.aftermath * 0.28f)
            * (0.34f + params.pressure * 0.34f);
        bedEnvelope_ += (bedTarget - bedEnvelope_)
            * onePoleCoefficient(bedTarget > bedEnvelope_ ? 0.55f : 0.09f,
                sr);
        primaryEnvelope_ *= 1.0f - decayCoefficient(
            0.028f + params.mass * 0.20f, sr);
        secondaryEnvelope_ *= 1.0f - decayCoefficient(
            0.002f + params.pressure * 0.018f, sr);

        const float pressureFlux = (midA_ - lowA_) * bedEnvelope_;
        const float riftFlux = primaryEnvelope_
            * ((midB_ - lowB_) * 0.74f
                + (highA_ - midA_) * 0.24f);
        const float decompressionFlux = secondaryEnvelope_
            * ((whiteB - highB_) * 0.48f
                + (highA_ - midA_) * 0.34f);
        return finish(pressureFlux * 0.56f + riftFlux * 0.88f
                + decompressionFlux * 0.42f,
            std::max({ bedEnvelope_ * 0.42f, primaryEnvelope_,
                secondaryEnvelope_ * 0.72f,
                std::min(1.0f, primaryEnergy_ * 0.30f) }),
            riftAdvanced, decompressed);
    }

    PlanetaryCryosphereOutput processHydrocarbonDune(
        const PlanetaryCryosphereParams& params)
    {
        const float sr = static_cast<float>(sampleRate_);
        const float dt = 1.0f / sr;
        const float whiteA = randomSigned();
        const float whiteB = randomSigned();
        // The close low/mid bands form a dense-atmosphere granular veil;
        // charge detail remains a small aperiodic component, never a zap.
        filterFlux(whiteA, whiteB,
            42.0f + params.mass * 86.0f,
            310.0f + params.mobility * 930.0f,
            2600.0f + params.brightness * 5200.0f);
        drift_ += (whiteB - drift_)
            * onePoleCoefficient(0.12f + params.mobility * 1.1f, sr);

        load_ += dt * params.drive
            * (0.08f + params.mobility * 0.72f)
            * (0.44f + params.cohesion * 0.78f);
        load_ = std::max(0.0f, load_ - dt
            * (0.003f + params.damping * 0.012f));
        if (load_ >= threshold_) {
            const float release = std::min(load_,
                0.08f + params.mobility * 0.34f);
            load_ -= release;
            primaryEnergy_ = std::min(4.0f, primaryEnergy_
                + release * (1.1f + params.branching * 1.7f));
            charge_ = std::min(1.5f, charge_ + release
                * (0.32f + params.brightness * 1.2f));
            threshold_ = 0.035f + randomUnit()
                * (0.10f + params.cohesion * 0.34f);
        }

        bool packetAdvanced = false;
        primaryCountdown_ -= 1.0f;
        if (primaryEnergy_ > 0.001f && primaryCountdown_ <= 0.0f) {
            const float released = std::min(primaryEnergy_,
                0.010f + randomUnit() * randomUnit()
                    * (0.055f + params.porosity * 0.095f));
            primaryEnergy_ -= released;
            primaryEnvelope_ = std::min(1.8f, primaryEnvelope_
                + released * (4.2f + params.cohesion * 2.2f));
            charge_ = std::min(1.5f, charge_ + released
                * params.brightness * 0.72f);
            packetAdvanced = true;
            primaryCountdown_ = sr * (0.0015f
                + randomUnit() * randomUnit()
                    * (0.022f + params.cohesion * 0.085f));
        }

        bool chargeCrackle = false;
        secondaryCountdown_ -= 1.0f;
        if ((secondaryEnergy_ + charge_ * 0.16f) > 0.001f
            && secondaryCountdown_ <= 0.0f) {
            const float released = std::min(secondaryEnergy_,
                0.001f + randomUnit() * 0.012f);
            secondaryEnergy_ = std::max(0.0f,
                secondaryEnergy_ - released);
            secondaryEnvelope_ = std::min(1.2f, secondaryEnvelope_
                + released * 5.0f + charge_ * 0.012f);
            charge_ *= 0.92f + randomUnit() * 0.06f;
            chargeCrackle = true;
            secondaryCountdown_ = sr * (0.0006f
                + randomUnit() * randomUnit()
                    * (0.008f + (1.0f - params.brightness) * 0.018f));
        }
        charge_ = std::max(0.0f, charge_ - dt
            * (0.03f + params.damping * 0.18f));

        const float bedTarget = params.drive
            * (0.10f + params.aftermath * 0.34f)
            * (0.28f + params.mobility * 0.42f);
        bedEnvelope_ += (bedTarget - bedEnvelope_)
            * onePoleCoefficient(bedTarget > bedEnvelope_ ? 1.4f : 0.28f,
                sr);
        primaryEnvelope_ *= 1.0f - decayCoefficient(
            0.012f + params.cohesion * 0.12f
                + params.mass * 0.06f, sr);
        secondaryEnvelope_ *= 1.0f - decayCoefficient(
            0.0005f + (1.0f - params.brightness) * 0.003f, sr);

        const float veil = (midA_ - lowA_) * bedEnvelope_
            * (0.58f + std::fabs(drift_) * 0.72f);
        const float packet = primaryEnvelope_
            * ((midB_ - lowB_) * 0.74f
                + (highA_ - midA_) * 0.16f);
        const float staticCrackle = secondaryEnvelope_
            * ((whiteB - highB_) * 0.34f
                + (highA_ - midA_) * 0.24f);
        return finish(veil * 0.72f + packet * 0.80f
                + staticCrackle * (0.16f + params.brightness * 0.18f),
            std::max({ bedEnvelope_ * 0.48f, primaryEnvelope_,
                secondaryEnvelope_ * 0.52f,
                std::min(1.0f, primaryEnergy_ * 0.34f) }),
            packetAdvanced, chargeCrackle);
    }

    PlanetaryCryosphereOutput processReactiveBrine(
        const PlanetaryCryosphereParams& params)
    {
        const float sr = static_cast<float>(sampleRate_);
        const float dt = 1.0f / sr;
        const float whiteA = randomSigned();
        const float whiteB = randomSigned();
        filterFlux(whiteA, whiteB,
            24.0f + params.mass * 58.0f,
            230.0f + params.porosity * 1160.0f,
            2100.0f + params.brightness * 7200.0f);
        drift_ += (whiteA - drift_)
            * onePoleCoefficient(0.06f + params.mobility * 0.72f, sr);

        // Cold material continuously seals the pore network while fluid work
        // loads it. Breakthrough resets pressure without making an impact.
        seal_ += dt * (0.010f + params.cohesion * 0.075f)
            * (1.0f - seal_);
        seal_ = std::max(0.0f, seal_ - dt * params.drive
            * params.porosity * 0.045f);
        load_ += dt * params.drive
            * (0.10f + params.pressure * 0.68f)
            * (0.38f + seal_ * 0.88f)
            * (0.72f + std::fabs(drift_) * 1.4f);
        load_ = std::max(0.0f, load_ - dt
            * params.porosity * (0.008f + params.mobility * 0.032f));
        if (load_ >= threshold_ * (0.72f + seal_ * 0.72f)) {
            const float release = std::min(load_,
                0.10f + params.pressure * 0.48f);
            load_ -= release;
            seal_ *= 0.18f + randomUnit() * 0.24f;
            primaryEnergy_ = std::min(4.0f, primaryEnergy_
                + release * (1.2f + params.branching * 1.8f));
            secondaryEnergy_ = std::min(3.0f, secondaryEnergy_
                + release * (0.28f + params.porosity * 0.84f));
            threshold_ = 0.045f + randomUnit()
                * (0.12f + params.cohesion * 0.38f);
        }

        bool breakthrough = false;
        primaryCountdown_ -= 1.0f;
        if (primaryEnergy_ > 0.001f && primaryCountdown_ <= 0.0f) {
            const float released = std::min(primaryEnergy_,
                0.018f + randomUnit() * randomUnit()
                    * (0.075f + params.pressure * 0.13f));
            primaryEnergy_ -= released;
            primaryEnvelope_ = std::min(1.9f, primaryEnvelope_
                + released * (3.4f + params.pressure * 2.4f));
            secondaryEnergy_ = std::min(3.0f, secondaryEnergy_
                + released * (0.24f + params.branching * 0.78f));
            breakthrough = true;
            primaryCountdown_ = sr * (0.002f
                + randomUnit() * randomUnit()
                    * (0.024f + params.cohesion * 0.11f));
        }

        bool poreOpened = false;
        secondaryCountdown_ -= 1.0f;
        const float poreFlux = secondaryEnergy_
            + params.drive * params.porosity * 0.018f;
        if (poreFlux > 0.0008f && secondaryCountdown_ <= 0.0f) {
            const float released = std::min(secondaryEnergy_,
                0.002f + randomUnit() * randomUnit()
                    * (0.018f + params.porosity * 0.040f));
            secondaryEnergy_ = std::max(0.0f,
                secondaryEnergy_ - released);
            secondaryEnvelope_ = std::min(1.4f, secondaryEnvelope_
                + 0.018f + released * 9.0f);
            poreOpened = true;
            secondaryCountdown_ = sr * (0.0009f
                + randomUnit() * randomUnit()
                    * (0.016f + (1.0f - params.mobility) * 0.032f));
        }

        const float bedTarget = params.drive
            * (0.12f + params.aftermath * 0.38f)
            * (0.24f + params.pressure * 0.46f)
            * (0.72f + seal_ * 0.28f);
        bedEnvelope_ += (bedTarget - bedEnvelope_)
            * onePoleCoefficient(bedTarget > bedEnvelope_ ? 0.82f : 0.15f,
                sr);
        primaryEnvelope_ *= 1.0f - decayCoefficient(
            0.018f + params.mass * 0.12f
                + params.pressure * 0.08f, sr);
        secondaryEnvelope_ *= 1.0f - decayCoefficient(
            0.0012f + params.porosity * 0.011f, sr);

        const float fingers = bedEnvelope_
            * ((midA_ - lowA_) * 0.62f
                + (midB_ - lowB_) * 0.26f);
        const float opening = primaryEnvelope_
            * ((midB_ - lowB_) * 0.52f
                + (highA_ - midA_) * 0.38f);
        const float pores = secondaryEnvelope_
            * ((highB_ - midB_) * 0.48f
                + (whiteA - highA_) * 0.20f);
        return finish(fingers * 0.68f + opening * 0.86f + pores * 0.42f,
            std::max({ bedEnvelope_ * 0.46f, primaryEnvelope_,
                secondaryEnvelope_ * 0.64f,
                std::min(1.0f, primaryEnergy_ * 0.30f) }),
            breakthrough, poreOpened);
    }

    double sampleRate_ = 48000.0;
    uint32_t initialSeed_ = 1u;
    uint32_t rng_ = 1u;
    PlanetaryCryosphereMode mode_ = PlanetaryCryosphereMode::TidalShell;
    bool modeInitialized_ = false;

    float load_ = 0.0f;
    float threshold_ = 0.16f;
    float seal_ = 0.32f;
    float charge_ = 0.0f;
    float primaryEnergy_ = 0.0f;
    float secondaryEnergy_ = 0.0f;
    float cascadeBias_ = 0.0f;
    float primaryCountdown_ = 0.0f;
    float secondaryCountdown_ = 0.0f;
    float primaryEnvelope_ = 0.0f;
    float secondaryEnvelope_ = 0.0f;
    float bedEnvelope_ = 0.0f;

    float lowA_ = 0.0f;
    float midA_ = 0.0f;
    float highA_ = 0.0f;
    float lowB_ = 0.0f;
    float midB_ = 0.0f;
    float highB_ = 0.0f;
    float drift_ = 0.0f;
    float previousWhite_ = 0.0f;
    float dcEstimate_ = 0.0f;
    float activity_ = 0.0f;
};

} // namespace s3g
