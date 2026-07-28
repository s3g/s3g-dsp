#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace s3g {

enum class PlanetaryModalProfile : uint32_t {
    CryoTidal = 0u,
    CryoDune,
    CryoBrine,
    CryoQuasicrystal,
    PyroSilicate,
    PyroSupercritical,
    PyroVent,
    PyroLattice,
    // Appended so every existing planetary profile keeps its ordinal.
    CryoSingingLake,
};

struct PlanetaryModalParams {
    PlanetaryModalProfile profile = PlanetaryModalProfile::CryoTidal;
    float amount = 0.0f;
    float scale = 0.5f;
    float damping = 0.5f;
    float irregularity = 0.5f;
    float coupling = 0.5f;
    float brightness = 0.5f;
};

struct PlanetaryModalOutput {
    float sample = 0.0f;
    float activity = 0.0f;
};

// A deliberately small event-excited body shared by the speculative frozen
// and thermal materials. Two modes per spatial entity keep the resonator
// budget fixed while seeded entity identity spreads the larger field into an
// alien modal lattice. There is no autonomous exciter: completed physical
// releases merely open a finite force window onto the source model's flux.
class PlanetaryModalBody {
public:
    static constexpr uint32_t kModeCount = 2u;

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
        identityA_ = randomSigned();
        identityB_ = randomSigned();
        identityC_ = randomSigned();
        for (auto& mode : modes_) mode = {};
        forceLow_ = 0.0f;
        forceDc_ = 0.0f;
        gatePhase_ = 1.0f;
        gateIncrement_ = 1.0f;
        gateStrength_ = 0.0f;
        consequenceBias_ = 0.0f;
        charge_ = 0.0f;
        activity_ = 0.0f;
        triggerCount_ = 0u;
        profileInitialized_ = false;
        updateTargets(true);
    }

    void setParams(PlanetaryModalParams params)
    {
        sanitize(params);
        const bool profileChanged = profileInitialized_
            && params.profile != params_.profile;
        params_ = params;
        if (profileChanged) clearSignalState();
        profileInitialized_ = true;
        updateTargets(!modesInitialized_ || profileChanged);
    }

    void excite(float strength, float cascade, bool consequence = false)
    {
        strength = finiteClamp(strength, 0.0f, 0.0f, 1.5f);
        cascade = finiteClamp(cascade, 0.5f, 0.0f, 1.0f);
        if (strength <= 1.0e-6f || params_.amount <= 1.0e-6f) return;

        const float available = std::sqrt(std::max(0.0f, 1.0f - charge_));
        const float accepted = strength * available;
        if (accepted <= 1.0e-6f) return;
        charge_ = std::min(1.0f,
            charge_ + accepted * accepted * (0.34f + cascade * 0.28f));

        const auto& profile = profileData(params_.profile);
        const float duration = std::clamp(profile.pulseSeconds
                * (0.82f + cascade * 0.32f)
                * (consequence ? 1.18f : 1.0f),
            0.002f, 0.014f);
        if (gatePhase_ >= 1.0f) {
            gatePhase_ = 0.0f;
            gateStrength_ = accepted;
        } else {
            // Preserve the current C1 window rather than restarting it with a
            // discontinuity when a defect cascade arrives during a release.
            gateStrength_ = std::min(1.5f,
                gateStrength_ + accepted * (0.34f + cascade * 0.28f));
        }
        gateIncrement_ = 1.0f
            / std::max(1.0f, duration * static_cast<float>(sampleRate_));
        consequenceBias_ = std::max(consequenceBias_,
            consequence ? 1.0f : cascade * 0.46f);
        ++triggerCount_;
    }

    PlanetaryModalOutput process(float physicalFlux, float sourceActivity)
    {
        physicalFlux = finiteClamp(physicalFlux, 0.0f, -4.0f, 4.0f);
        sourceActivity = finiteClamp(sourceActivity, 0.0f, 0.0f, 1.0f);
        const auto& profile = profileData(params_.profile);
        const float sr = static_cast<float>(sampleRate_);

        charge_ *= std::exp(-1.0f
            / std::max(1.0f, profile.recoverySeconds * sr));

        const float forceCutoff = std::clamp(profile.forceCutoffHz
                * (0.72f + params_.brightness * 0.72f),
            90.0f, sr * 0.42f);
        const float forceCoefficient = onePoleCoefficient(forceCutoff, sr);
        forceLow_ += (physicalFlux - forceLow_) * forceCoefficient;
        forceDc_ += (forceLow_ - forceDc_)
            * onePoleCoefficient(18.0f, sr);

        float force = 0.0f;
        if (gatePhase_ < 1.0f) {
            const float phase = std::clamp(gatePhase_, 0.0f, 1.0f);
            const float complement = 1.0f - phase;
            const float window = 16.0f * phase * phase
                * complement * complement;
            force = (forceLow_ - forceDc_) * window * gateStrength_
                * (0.34f + sourceActivity * 0.66f);
            gatePhase_ += gateIncrement_;
            if (gatePhase_ >= 1.0f) {
                gatePhase_ = 1.0f;
                gateStrength_ = 0.0f;
            }
        }

        const float smoothing = 1.0f - std::exp(-1.0f
            / std::max(1.0f, sr * 0.018f));
        std::array<double, kModeCount> velocity {};
        for (uint32_t index = 0u; index < kModeCount; ++index) {
            auto& mode = modes_[index];
            mode.frequencyHz += (mode.targetFrequencyHz
                - mode.frequencyHz) * smoothing;
            mode.decaySeconds += (mode.targetDecaySeconds
                - mode.decaySeconds) * smoothing;

            const double frequency = std::clamp(mode.frequencyHz,
                180.0, sampleRate_ * 0.40);
            const double decay = std::clamp(mode.decaySeconds,
                0.035, 1.60);
            const double theta = 6.28318530717958647692
                * frequency / sampleRate_;
            double radius = std::exp(-6.907755278982137052
                / (decay * sampleRate_));
            const double stateMagnitude = std::fabs(mode.state1)
                + std::fabs(mode.state2);
            radius *= std::exp(-std::min(0.018,
                stateMagnitude * 0.00045));
            radius = std::clamp(radius, 0.0, 0.9999997);
            const double coefficient = 2.0 * radius * std::cos(theta);
            const double normalizer = std::sqrt(
                std::max(1.0e-10, 1.0 - radius * radius));
            const float consequenceTilt = consequenceBias_;
            const float drive = index == 0u
                ? profile.lowerDrive
                    * (1.0f - consequenceTilt * 0.30f)
                : profile.upperDrive
                    * (0.72f + params_.brightness * 0.28f
                        + consequenceTilt * 0.34f);
            double next = coefficient * mode.state1
                - radius * radius * mode.state2
                + static_cast<double>(force * drive) * normalizer;
            if (!std::isfinite(next)) next = 0.0;
            next = safetyKnee(next);
            const double sine = std::max(0.035, std::fabs(std::sin(theta)));
            velocity[index] = (next - mode.state1) / sine;
            mode.state2 = mode.state1;
            mode.state1 = next;
        }
        consequenceBias_ *= std::exp(-1.0f / std::max(1.0f, sr * 0.08f));

        double sample = velocity[0] * profile.lowerMix
            + velocity[1] * profile.upperMix;
        sample *= static_cast<double>(params_.amount) * 0.62;
        sample = safetyKnee(sample);
        if (!std::isfinite(sample)) {
            clearSignalState();
            sample = 0.0;
        }
        const float output = static_cast<float>(sample);
        const float targetActivity = std::clamp(std::fabs(output) * 2.8f
                + (gatePhase_ < 1.0f ? gateStrength_ * 0.18f : 0.0f),
            0.0f, 1.0f);
        activity_ += (targetActivity - activity_)
            * (targetActivity > activity_ ? 0.12f : 0.0016f);
        if (activity_ < 1.0e-12f && gatePhase_ >= 1.0f
            && std::fabs(output) < 1.0e-12f) {
            activity_ = 0.0f;
        }
        return { output, activity_ };
    }

    float modeFrequencyHz(uint32_t index) const
    {
        return static_cast<float>(modes_[std::min<uint32_t>(
            index, kModeCount - 1u)].targetFrequencyHz);
    }

    float modeDecaySeconds(uint32_t index) const
    {
        return static_cast<float>(modes_[std::min<uint32_t>(
            index, kModeCount - 1u)].targetDecaySeconds);
    }

    uint64_t triggerCount() const { return triggerCount_; }
    float activity() const { return activity_; }

    bool active() const
    {
        if (gatePhase_ < 1.0f || activity_ > 1.0e-7f) return true;
        for (const auto& mode : modes_) {
            if (std::fabs(mode.state1) + std::fabs(mode.state2)
                    > 1.0e-8) {
                return true;
            }
        }
        return false;
    }

private:
    struct ProfileData {
        float baseHz;
        float ratio;
        float ratioSpread;
        float lowerDecay;
        float upperDecay;
        float pulseSeconds;
        float recoverySeconds;
        float forceCutoffHz;
        float lowerDrive;
        float upperDrive;
        float lowerMix;
        float upperMix;
        float identitySpread;
    };

    struct Mode {
        double state1 = 0.0;
        double state2 = 0.0;
        double frequencyHz = 300.0;
        double targetFrequencyHz = 300.0;
        double decaySeconds = 0.3;
        double targetDecaySeconds = 0.3;
    };

    static constexpr std::array<ProfileData, 9u> kProfiles {{
        { 300.0f, 1.57f, 0.11f, 0.82f, 0.46f, 0.0080f,
            0.82f, 1100.0f, 0.54f, 0.90f, 0.38f, 0.86f, 0.22f },
        { 520.0f, 1.83f, 0.18f, 0.14f, 0.08f, 0.0035f,
            0.34f, 2600.0f, 0.34f, 1.00f, 0.26f, 0.94f, 0.31f },
        { 390.0f, 1.64f, 0.22f, 0.34f, 0.20f, 0.0060f,
            0.58f, 1800.0f, 0.48f, 0.92f, 0.40f, 0.88f, 0.27f },
        { 440.0f, 1.47f, 0.19f, 0.52f, 0.26f, 0.0030f,
            0.76f, 4300.0f, 0.30f, 1.00f, 0.25f, 0.98f, 0.37f },
        { 430.0f, 1.39f, 0.17f, 0.62f, 0.30f, 0.0080f,
            0.50f, 1100.0f, 0.52f, 0.84f, 0.46f, 0.80f, 0.28f },
        { 520.0f, 1.82f, 0.28f, 0.11f, 0.065f, 0.0120f,
            0.30f, 760.0f, 0.82f, 0.52f, 0.72f, 0.48f, 0.36f },
        { 610.0f, 1.105f, 0.035f, 0.20f, 0.13f, 0.0050f,
            0.55f, 2200.0f, 0.58f, 0.90f, 0.48f, 0.88f, 0.30f },
        { 760.0f, 1.618f, 0.085f, 0.72f, 0.40f, 0.0025f,
            1.10f, 5200.0f, 0.36f, 1.00f, 0.30f, 1.00f, 0.42f },
        // Singing Lake: a close, opposed pair suppresses the hard pitched
        // onset, then blooms as the two stationary modes separate in phase.
        // There is deliberately no event-time frequency sweep.
        { 360.0f, 1.118f, 0.030f, 0.44f, 0.26f, 0.0105f,
            0.85f, 2400.0f, 0.46f, 0.74f, 0.56f, -0.35f, 0.13f },
    }};

    static float finiteClamp(float value, float fallback,
        float minimum, float maximum)
    {
        return std::clamp(std::isfinite(value) ? value : fallback,
            minimum, maximum);
    }

    static PlanetaryModalProfile sanitizeProfile(
        PlanetaryModalProfile profile)
    {
        return static_cast<uint32_t>(profile) < kProfiles.size()
            ? profile : PlanetaryModalProfile::CryoTidal;
    }

    static void sanitize(PlanetaryModalParams& params)
    {
        params.profile = sanitizeProfile(params.profile);
        params.amount = finiteClamp(params.amount, 0.0f, 0.0f, 1.0f);
        params.scale = finiteClamp(params.scale, 0.5f, 0.0f, 1.0f);
        params.damping = finiteClamp(params.damping, 0.5f, 0.0f, 1.0f);
        params.irregularity = finiteClamp(
            params.irregularity, 0.5f, 0.0f, 1.0f);
        params.coupling = finiteClamp(params.coupling, 0.5f, 0.0f, 1.0f);
        params.brightness = finiteClamp(
            params.brightness, 0.5f, 0.0f, 1.0f);
    }

    static const ProfileData& profileData(PlanetaryModalProfile profile)
    {
        return kProfiles[static_cast<uint32_t>(sanitizeProfile(profile))];
    }

    static float onePoleCoefficient(float cutoffHz, float sampleRate)
    {
        constexpr float kTwoPi = 6.28318530717958647692f;
        const float x = kTwoPi
            * std::clamp(cutoffHz, 0.001f, sampleRate * 0.44f)
            / sampleRate;
        return x / (1.0f + x);
    }

    static double safetyKnee(double value)
    {
        constexpr double threshold = 4.0;
        constexpr double range = 4.0;
        const double magnitude = std::fabs(value);
        if (magnitude <= threshold) return value;
        const double excess = magnitude - threshold;
        const double shaped = threshold + excess / (1.0 + excess / range);
        return std::copysign(shaped, value);
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

    static uint32_t hashIdentity(uint32_t value)
    {
        value ^= value >> 16u;
        value *= 0x7feb352du;
        value ^= value >> 15u;
        value *= 0x846ca68bu;
        value ^= value >> 16u;
        return value;
    }

    void updateTargets(bool immediate)
    {
        const auto& profile = profileData(params_.profile);
        const float transpose = std::exp2((0.5f - params_.scale) * 1.40f
            + (params_.brightness - 0.5f) * 0.18f);
        const float spread = profile.identitySpread
            * (0.36f + params_.irregularity * 0.64f);
        float base = profile.baseHz * transpose
            * std::exp2(identityA_ * spread);
        if (params_.profile == PlanetaryModalProfile::CryoSingingLake) {
            // A pure seed hash selects one flexural branch without consuming
            // RNG state, leaving all eight established profiles bit-stable.
            constexpr std::array<float, 7u> kFlexuralBranches {
                1.000f, 1.337f, 1.782f, 2.316f,
                2.903f, 3.571f, 4.279f,
            };
            const uint32_t branch = hashIdentity(
                initialSeed_ ^ 0x51a91ce5u) % kFlexuralBranches.size();
            base *= kFlexuralBranches[branch];
        }
        const float ratio = std::max(1.035f, profile.ratio
            + identityB_ * profile.ratioSpread
                * (0.30f + params_.irregularity * 0.70f)
            + (params_.coupling - 0.5f) * profile.ratioSpread * 0.22f);
        const float maximum = static_cast<float>(sampleRate_) * 0.40f;
        modes_[0].targetFrequencyHz = std::clamp<double>(base,
            180.0, maximum);
        modes_[1].targetFrequencyHz = std::clamp<double>(base * ratio,
            180.0, maximum);

        const float decayScale = std::exp2(
                (0.5f - params_.damping) * 1.70f)
            * (0.82f + params_.scale * 0.36f);
        const float scatterA = std::exp2(identityB_
            * params_.irregularity * 0.22f);
        const float scatterB = std::exp2(identityC_
            * params_.irregularity * 0.28f);
        modes_[0].targetDecaySeconds = std::clamp<double>(
            profile.lowerDecay * decayScale * scatterA, 0.035, 1.60);
        modes_[1].targetDecaySeconds = std::clamp<double>(
            profile.upperDecay * decayScale * scatterB, 0.035, 1.60);
        if (immediate) {
            for (auto& mode : modes_) {
                mode.frequencyHz = mode.targetFrequencyHz;
                mode.decaySeconds = mode.targetDecaySeconds;
            }
        }
        modesInitialized_ = true;
    }

    void clearSignalState()
    {
        for (auto& mode : modes_) {
            mode.state1 = 0.0;
            mode.state2 = 0.0;
        }
        forceLow_ = 0.0f;
        forceDc_ = 0.0f;
        gatePhase_ = 1.0f;
        gateStrength_ = 0.0f;
        consequenceBias_ = 0.0f;
        charge_ = 0.0f;
        activity_ = 0.0f;
    }

    PlanetaryModalParams params_ {};
    std::array<Mode, kModeCount> modes_ {};
    double sampleRate_ = 48000.0;
    uint32_t initialSeed_ = 1u;
    uint32_t rng_ = 1u;
    float identityA_ = 0.0f;
    float identityB_ = 0.0f;
    float identityC_ = 0.0f;
    float forceLow_ = 0.0f;
    float forceDc_ = 0.0f;
    float gatePhase_ = 1.0f;
    float gateIncrement_ = 1.0f;
    float gateStrength_ = 0.0f;
    float consequenceBias_ = 0.0f;
    float charge_ = 0.0f;
    float activity_ = 0.0f;
    uint64_t triggerCount_ = 0u;
    bool profileInitialized_ = false;
    bool modesInitialized_ = false;
};

} // namespace s3g
