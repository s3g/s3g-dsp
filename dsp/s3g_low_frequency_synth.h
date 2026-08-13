#pragma once

#include "s3g_bass_amplifier.h"
#include "s3g_bass_shred.h"
#include "s3g_math.h"
#include "s3g_realtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace s3g {

enum class MotionClock : uint32_t {
    Free = 0u,
    Transport,
    Count,
};

inline constexpr uint32_t kMotionClockCount =
    static_cast<uint32_t>(MotionClock::Count);

inline const char* motionClockName(MotionClock clock)
{
    switch (clock) {
    case MotionClock::Free: return "FREE";
    case MotionClock::Transport: return "TRANSPORT";
    case MotionClock::Count: break;
    }
    return "FREE";
}

enum class AmplitudeMotionPosition : uint32_t {
    PreShred = 0u,
    PostShred,
    Count,
};

inline constexpr uint32_t kAmplitudeMotionPositionCount =
    static_cast<uint32_t>(AmplitudeMotionPosition::Count);

inline const char* amplitudeMotionPositionName(
    AmplitudeMotionPosition position)
{
    switch (position) {
    case AmplitudeMotionPosition::PreShred: return "PRE SHRED";
    case AmplitudeMotionPosition::PostShred: return "POST SHRED";
    case AmplitudeMotionPosition::Count: break;
    }
    return "POST SHRED";
}

struct MotionDivisionInfo {
    const char* name;
    float beats;
};

inline constexpr std::array<MotionDivisionInfo, 16u> kMotionDivisions {{
        { "4/1", 16.0f }, { "2/1", 8.0f }, { "1/1", 4.0f },
        { "1/2 D", 3.0f }, { "1/2", 2.0f },
        { "1/2 T", 4.0f / 3.0f }, { "1/4 D", 1.5f },
        { "1/4", 1.0f }, { "1/4 T", 2.0f / 3.0f },
        { "1/8 D", 0.75f }, { "1/8", 0.5f },
        { "1/8 T", 1.0f / 3.0f }, { "1/16 D", 0.375f },
        { "1/16", 0.25f }, { "1/16 T", 1.0f / 6.0f },
        { "1/32", 0.125f },
    }};

inline constexpr uint32_t kMotionDivisionCount =
    static_cast<uint32_t>(kMotionDivisions.size());

inline const MotionDivisionInfo& motionDivisionInfo(uint32_t index)
{
    return kMotionDivisions[std::min<uint32_t>(
        index, kMotionDivisionCount - 1u)];
}

// A keyboard adaptation of Ambi Membrane Kick's single shared body. Twelve
// struck modes are sampled at the same sixteen surface positions and locally
// saturated before stereo folding. A continuous modal-energy term makes that
// struck membrane sustain under ADSR and glide without introducing a separate
// oscillator bank. The first membrane mode is the protected bass foundation;
// the surface body can enter one stable topology-preserving low-pass. A final
// stereo Shred stage keeps a note-related clean low branch outside its folded
// feedback loops. No allocation or locking occurs while the voice is running.
struct LowFrequencySynthParams {
    float transposeSemitones = 0.0f;
    float fineCents = 0.0f;
    float fundamental = 0.96f;
    float body = 0.70f;
    float loading = 0.82f;
    float coupling = 0.50f;
    float tensionVariance = 0.08f;
    float excitationPosition = 0.25f;
    float damping = 0.28f;
    float nonlinearity = 0.24f;
    float attackSeconds = 0.008f;
    float decaySeconds = 0.22f;
    float sustain = 0.86f;
    float releaseSeconds = 0.35f;
    float glideMs = 35.0f;
    float pitchTransientSemitones = 0.0f;
    float pitchTransientMs = 45.0f;
    float stereoWidth = 0.12f;
    float velocitySensitivity = 0.72f;
    float pressureSensitivity = 0.45f;
    float outputGainDb = -8.0f;
    float upperModeLevel = 0.18f;
    float filterCutoffHz = 1200.0f;
    float filterResonance = 0.18f;
    float filterEnvelopeOctaves = 0.0f;
    float filterDecayMs = 160.0f;
    float membraneDrive = 0.28f;
    float wavefold = 0.0f;
    float driveFeedback = 0.0f;
    float processedMix = 0.36f;
    float amplitudeMotionRateHz = 2.0f;
    float amplitudeMotionDepth = 0.0f;
    float amplitudeMotionClock = 1.0f;
    float amplitudeMotionDivision = 7.0f;
    float amplitudeMotionPosition = 1.0f;
    float shred = 0.0f;
    float shredFeedback = 0.0f;
    float shredCircuit = 0.0f;
    float shredColor = 0.55f;
    float shredMix = 0.0f;
    float valvePreamp = 0.36f;
    float powerStage = 0.27f;
    float supplySag = 0.198f;
    float ampBass = 0.1008f;
    float ampMid = -0.0216f;
    float ampMidFrequency = 1.0f;
    float ampTreble = -0.036f;
    float cabinet = 0.2592f;
    float pedalCircuit = 0.0f;
    float pedalDrive = 0.0f;
    float pedalTone = 0.5f;
    float pedalCharacter = 0.5f;
    float pedalCrossoverHz = 240.0f;
    float pedalBlend = 0.0f;
};

class LowFrequencySynth {
public:
    static constexpr uint32_t kModeCount = 12u;
    static constexpr uint32_t kSurfacePatchCount = 16u;

    void prepare(double sampleRate)
    {
        sampleRate_ = std::isfinite(sampleRate)
            ? std::clamp(sampleRate, 8000.0, 768000.0) : 48000.0;
        initializeSurfacePickups();
        amplifier_.prepare(sampleRate_);
        bassShred_.prepare(sampleRate_);
        reset();
    }

    void reset()
    {
        modes_.fill({});
        currentFrequencyHz_ = 55.0f;
        targetFrequencyHz_ = 55.0f;
        envelope_ = 0.0f;
        releaseStep_ = 0.0f;
        pitchTransientEnvelope_ = 0.0f;
        pendingExcitation_ = 0.0f;
        declickGain_ = 0.0f;
        signalStateCleared_ = true;
        pressure_ = 0.0f;
        noteVelocity_ = 1.0f;
        sideLow_ = 0.0f;
        dcLowLeft_ = 0.0f;
        dcLowRight_ = 0.0f;
        limiterGain_ = 1.0f;
        filterStages_.fill(0.0f);
        sideFilterStages_.fill(0.0f);
        filterCutoffLog2_ = std::log2(params_.filterCutoffHz);
        effectiveFilterCutoffHz_ = params_.filterCutoffHz;
        resonanceSmoothed_ = params_.filterResonance;
        driveSmoothed_ = params_.membraneDrive;
        processedMixSmoothed_ = params_.processedMix;
        amplitudeMotionRateSmoothed_ = params_.amplitudeMotionRateHz;
        amplitudeMotionDepthSmoothed_ = params_.amplitudeMotionDepth;
        amplitudeMotionPhase_ = 0.0f;
        amplitudeMotionGain_ = 1.0f;
        amplitudeMotionPostMixSmoothed_ = params_.amplitudeMotionPosition;
        externalAmplitudeMotionActive_ = false;
        externalAmplitudeMotionPhase_ = 0.0f;
        previousFilterInput_ = 0.0f;
        previousFilterSideInput_ = 0.0f;
        smoothedOutputGain_ = dbToGain(params_.outputGainDb);
        stage_ = EnvelopeStage::Idle;
        gate_ = false;
        amplifier_.reset();
        bassShred_.setParams(bassShredParams());
        bassShred_.reset();
        updateModeCoefficients();
    }

    void setParams(LowFrequencySynthParams params)
    {
        params_ = sanitize(params);
        amplifier_.setParams(amplifierParams());
        bassShred_.setParams(bassShredParams());
        targetFrequencyHz_ = midiFrequency(currentMidiNote_);
        if (stage_ == EnvelopeStage::Idle) {
            currentFrequencyHz_ = targetFrequencyHz_;
            smoothedOutputGain_ = dbToGain(params_.outputGainDb);
            filterCutoffLog2_ = std::log2(params_.filterCutoffHz);
            effectiveFilterCutoffHz_ = params_.filterCutoffHz;
            resonanceSmoothed_ = params_.filterResonance;
            driveSmoothed_ = params_.membraneDrive;
            processedMixSmoothed_ = params_.processedMix;
            amplitudeMotionRateSmoothed_ = params_.amplitudeMotionRateHz;
            amplitudeMotionDepthSmoothed_ = params_.amplitudeMotionDepth;
            amplitudeMotionPostMixSmoothed_ =
                params_.amplitudeMotionPosition;
        }
    }

    LowFrequencySynthParams params() const { return params_; }

    void noteOn(int midiNote, float velocity = 1.0f, bool legato = false)
    {
        midiNote = std::clamp(midiNote, 0, 127);
        velocity = finiteClamp(velocity, 1.0f, 0.0f, 1.0f);
        const bool wasActive = stage_ != EnvelopeStage::Idle;
        currentMidiNote_ = midiNote;
        targetFrequencyHz_ = midiFrequency(midiNote);
        if (!wasActive || params_.glideMs <= 0.01f) {
            currentFrequencyHz_ = targetFrequencyHz_;
        }
        noteVelocity_ = velocity;
        gate_ = true;
        if (!legato || !wasActive) {
            stage_ = EnvelopeStage::Attack;
            pitchTransientEnvelope_ = 1.0f;
            pendingExcitation_ = std::max(pendingExcitation_,
                lerp(0.22f, velocity * velocity, 0.82f));
            if (!wasActive) {
                for (auto& mode : modes_) mode.phase = 0.0;
            }
            if (!externalAmplitudeMotionActive_) {
                amplitudeMotionPhase_ = 0.0f;
            }
        } else if (stage_ == EnvelopeStage::Release) {
            stage_ = EnvelopeStage::Attack;
        }
        signalStateCleared_ = false;
    }

    void noteOff()
    {
        if (!gate_ && stage_ == EnvelopeStage::Idle) return;
        gate_ = false;
        if (stage_ != EnvelopeStage::Idle) {
            stage_ = EnvelopeStage::Release;
            releaseStep_ = envelope_ / static_cast<float>(std::max(
                1.0, params_.releaseSeconds * sampleRate_));
        }
    }

    void setPressure(float pressure)
    {
        pressure_ = finiteClamp(pressure, 0.0f, 0.0f, 1.0f);
    }

    void setAmplitudeMotionTransportPhase(float phase, bool active)
    {
        externalAmplitudeMotionActive_ = active;
        if (active) {
            phase = std::isfinite(phase) ? phase : 0.0f;
            externalAmplitudeMotionPhase_ = phase - std::floor(phase);
            if (externalAmplitudeMotionPhase_ < 0.0f) {
                externalAmplitudeMotionPhase_ += 1.0f;
            }
            amplitudeMotionPhase_ = externalAmplitudeMotionPhase_;
        }
    }

    void processFrame(float& left, float& right)
    {
        updateEnvelope();
        const bool envelopeActive = stage_ != EnvelopeStage::Idle;
        const bool shredTailActive = params_.shredMix > 1.0e-5f
            && params_.shredFeedback > 1.0e-5f
            && bassShred_.feedbackActivity() > 2.0e-4f;
        const float declickTarget = envelopeActive || shredTailActive
            ? 1.0f : 0.0f;
        const float declickCoefficient = declickTarget > declickGain_
            ? declickAttackCoefficient_ : declickReleaseCoefficient_;
        declickGain_ += (declickTarget - declickGain_)
            * declickCoefficient;
        if (!envelopeActive && declickGain_ <= 1.0e-4f) {
            declickGain_ = 0.0f;
            if (!signalStateCleared_) {
                clearSignalState();
                signalStateCleared_ = true;
            }
            left = 0.0f;
            right = 0.0f;
            return;
        }
        signalStateCleared_ = false;
        updateFrequency();
        const float frequency = effectiveFundamentalFrequency();
        const float pressureInfluence = pressure_
            * params_.pressureSensitivity;
        const float driveEnergy = std::clamp(noteVelocity_
            + pressureInfluence * 0.55f, 0.0f, 1.35f);
        // Preserve Membrane Kick's one-point force law, but distribute the
        // strike over a short packet so a keyboard retrigger remains clean.
        const float excitation = pendingExcitation_
            * onsetExcitationCoefficient_;
        pendingExcitation_ = std::max(0.0f,
            pendingExcitation_ - excitation);
        if (pendingExcitation_ < 1.0e-7f) pendingExcitation_ = 0.0f;

        std::array<float, kModeCount> modalSamples {};
        for (uint32_t index = 0u; index < kModeCount; ++index) {
            auto& mode = modes_[index];
            const float ratio = modeRatio(index);
            const float modeFrequency = std::clamp(frequency * ratio,
                8.0f, static_cast<float>(sampleRate_ * 0.43));
            mode.phase += 2.0 * static_cast<double>(kPi)
                * static_cast<double>(modeFrequency) / sampleRate_;
            if (mode.phase >= 2.0 * static_cast<double>(kPi)) {
                mode.phase -= 2.0 * static_cast<double>(kPi)
                    * std::floor(mode.phase
                        / (2.0 * static_cast<double>(kPi)));
            }

            const float highMode = std::max(0.0f, ratio - 1.0f);
            const float shape = excitationShape(index,
                params_.excitationPosition);
            const float spectralFalloff = 1.0f / std::pow(
                std::max(1.0f, ratio), 1.12f + params_.damping * 0.72f);
            const float upperLevel = index == 0u ? 1.0f
                : lerp(0.14f, 1.0f, params_.upperModeLevel);
            const float centerWeight = index == 0u
                ? 1.0f + params_.fundamental * 0.86f
                : 0.42f + std::fabs(shape) * 0.72f;
            const float signedShape = index == 0u ? 1.0f : shape;
            mode.strikeAmplitude = std::clamp(mode.strikeAmplitude
                + excitation * spectralFalloff * centerWeight
                    * signedShape * upperLevel * 0.92f, -2.0f, 2.0f);

            const float decaySeconds = lerp(3.2f, 0.20f, params_.damping)
                / (1.0f + highMode
                    * (0.10f + params_.damping * 1.34f));
            mode.strikeAmplitude *= static_cast<float>(std::exp(-1.0
                / std::max(1.0, decaySeconds * sampleRate_)));
            if (std::fabs(mode.strikeAmplitude) < 1.0e-9f) {
                mode.strikeAmplitude = 0.0f;
            }

            const float sustainTarget = gate_
                ? driveEnergy * spectralFalloff * signedShape
                    * upperLevel * (index == 0u ? 0.74f : 0.52f)
                : 0.0f;
            const float sustainSeconds = std::fabs(sustainTarget)
                    > std::fabs(mode.sustainAmplitude)
                ? 0.018f : 0.095f;
            mode.sustainAmplitude += (sustainTarget
                - mode.sustainAmplitude) * onePoleCoefficient(sustainSeconds);
            const float amplitude = std::clamp(mode.strikeAmplitude
                + mode.sustainAmplitude, -2.2f, 2.2f);
            modalSamples[index] = std::sin(mode.phase) * amplitude;
        }

        // Fold the same sixteen physical surface pickups used by Membrane
        // Kick. Saturation is local to each patch, before stereo summation.
        float rawLeft = 0.0f;
        float rawRight = 0.0f;
        const float localDrive = 1.0f + driveSmoothed_ * 7.0f;
        const float driveNormalization = std::max(0.25f,
            std::tanh(localDrive));
        constexpr float pickupNormalization = 0.055f;
        for (uint32_t patch = 0u; patch < kSurfacePatchCount; ++patch) {
            float patchSample = modalSamples[0u] * 0.18f;
            for (uint32_t mode = 1u; mode < kModeCount; ++mode) {
                patchSample += modalSamples[mode]
                    * surfaceModeShapes_[patch][mode];
            }
            patchSample = std::tanh(patchSample * localDrive)
                / driveNormalization;
            rawLeft += patchSample * surfacePanGains_[patch][0u]
                * pickupNormalization;
            rawRight += patchSample * surfacePanGains_[patch][1u]
                * pickupNormalization;
        }

        const float bodyMid = (rawLeft + rawRight) * 0.5f
            * params_.body;
        const float bodySide = (rawLeft - rawRight) * 0.5f
            * params_.body;
        const float dryMid = bodyMid;
        float processedMid = dryMid;
        float processedSide = bodySide;
        processFilterAndDrive(processedMid, processedSide);
        const float mixedMid = lerp(dryMid, processedMid,
            processedMixSmoothed_);
        const float mixedSide = lerp(bodySide, processedSide,
            processedMixSmoothed_);
        sideLow_ += (mixedSide - sideLow_) * sideLowCoefficient_;
        const float protectedSide = mixedSide - sideLow_;
        const float velocityGain = lerp(1.0f,
            0.28f + noteVelocity_ * 0.72f, params_.velocitySensitivity);
        // The protected foundation is the first membrane mode itself. A small
        // waveform reinforcement follows that mode's phase and amplitude only
        // below the most audible bass range; there is no separate oscillator.
        const float bassSupport = std::clamp(
            std::log2(150.0f / std::max(20.0f, frequency)) * 0.55f,
            0.0f, 1.0f);
        const float membranePhase = static_cast<float>(modes_[0u].phase);
        const float membraneAmplitude = std::clamp(
            modes_[0u].strikeAmplitude + modes_[0u].sustainAmplitude,
            -2.2f, 2.2f);
        const float fundamentalWave = std::sin(membranePhase);
        const float secondHarmonic = std::sin(membranePhase * 2.0f);
        const float thirdHarmonic = std::sin(membranePhase * 3.0f);
        const float sub = params_.fundamental * membraneAmplitude
            * (fundamentalWave + bassSupport
                * (secondHarmonic * 0.11f
                    * partialBandLimit(frequency, 2.0f)
                    + thirdHarmonic * 0.040f
                    * partialBandLimit(frequency, 3.0f))) * 0.72f;
        const float side = protectedSide * params_.stereoWidth;
        float frameLeft = (sub + mixedMid + side)
            * envelope_ * velocityGain;
        float frameRight = (sub + mixedMid - side)
            * envelope_ * velocityGain;
        amplifier_.processStereo(frameLeft, frameRight);
        const float amplitudeGain = updateAmplitudeMotion();
        amplitudeMotionPostMixSmoothed_ +=
            (params_.amplitudeMotionPosition
                - amplitudeMotionPostMixSmoothed_)
            * onePoleCoefficient(0.020f);
        const float preGain = lerp(amplitudeGain, 1.0f,
            amplitudeMotionPostMixSmoothed_);
        frameLeft *= preGain;
        frameRight *= preGain;
        bassShred_.processStereo(frameLeft, frameRight, frequency);
        const float postGain = lerp(1.0f, amplitudeGain,
            amplitudeMotionPostMixSmoothed_);
        frameLeft *= postGain;
        frameRight *= postGain;

        // Remove numerical and asymmetric-saturation DC without filtering the
        // musical low band. The pole is below 8 Hz at ordinary sample rates.
        const float dcCoefficient = dcCoefficient_;
        dcLowLeft_ += (frameLeft - dcLowLeft_) * dcCoefficient;
        dcLowRight_ += (frameRight - dcLowRight_) * dcCoefficient;
        frameLeft -= dcLowLeft_;
        frameRight -= dcLowRight_;

        smoothedOutputGain_ += (dbToGain(params_.outputGainDb)
            - smoothedOutputGain_) * outputSmoothingCoefficient_;
        frameLeft *= smoothedOutputGain_;
        frameRight *= smoothedOutputGain_;
        frameLeft *= declickGain_;
        frameRight *= declickGain_;
        const float peak = std::max(std::fabs(frameLeft),
            std::fabs(frameRight));
        const float limiterTarget = peak > 0.94f ? 0.94f / peak : 1.0f;
        if (limiterTarget < limiterGain_) {
            // With no look-ahead available, catch the current linked stereo
            // peak immediately. A smoothed attack allowed the first onset
            // sample to hit the final hard clamp and audibly break apart.
            limiterGain_ = limiterTarget;
        } else {
            limiterGain_ += (limiterTarget - limiterGain_)
                * limiterReleaseCoefficient_;
        }
        left = safeOutput(frameLeft * limiterGain_);
        right = safeOutput(frameRight * limiterGain_);

    }

    void processBlock(float* left, float* right, uint32_t frames)
    {
        for (uint32_t frame = 0u; frame < frames; ++frame) {
            float frameLeft = 0.0f;
            float frameRight = 0.0f;
            processFrame(frameLeft, frameRight);
            if (left) left[frame] = frameLeft;
            if (right) right[frame] = frameRight;
        }
    }

    bool active() const
    {
        return stage_ != EnvelopeStage::Idle || declickGain_ > 1.0e-4f;
    }
    bool gate() const { return gate_; }
    float envelope() const { return envelope_; }
    float currentFrequencyHz() const { return currentFrequencyHz_; }
    float effectiveFilterCutoffHz() const
    {
        return effectiveFilterCutoffHz_;
    }

    float modeRatio(uint32_t index) const
    {
        index = std::min(index, kModeCount - 1u);
        constexpr std::array<float, kModeCount> circular {{
            1.000f, 1.593f, 1.593f, 2.135f, 2.295f, 2.295f,
            2.653f, 2.653f, 2.918f, 2.918f, 3.500f, 3.600f,
        }};
        // A compact musical approximation of radial composite loading: modal
        // pairs converge toward a harmonic family rather than being replaced
        // by an unrelated oscillator stack.
        constexpr std::array<float, kModeCount> loaded {{
            1.000f, 2.000f, 2.000f, 3.000f, 3.000f, 3.000f,
            4.000f, 4.000f, 5.000f, 5.000f, 6.000f, 7.000f,
        }};
        float ratio = lerp(circular[index], loaded[index],
            params_.loading);
        float pairSign = 0.0f;
        switch (index) {
        case 1u: case 4u: case 6u: case 8u: pairSign = -1.0f; break;
        case 2u: case 5u: case 7u: case 9u: pairSign = 1.0f; break;
        default: break;
        }
        if (pairSign != 0.0f) {
            const float splitCents = pairSign * params_.coupling
                * (3.5f + static_cast<float>(index) * 0.55f);
            ratio *= std::exp2(splitCents / 1200.0f);
        }
        return ratio;
    }

private:
    enum class EnvelopeStage : uint32_t { Idle, Attack, Decay, Sustain, Release };

    struct ModeState {
        double phase = 0.0;
        float strikeAmplitude = 0.0f;
        float sustainAmplitude = 0.0f;
    };

    static float finiteClamp(float value, float fallback,
        float minimum, float maximum)
    {
        return std::clamp(std::isfinite(value) ? value : fallback,
            minimum, maximum);
    }

    static float dbToGain(float db)
    {
        return db <= -120.0f ? 0.0f : std::pow(10.0f, db * 0.05f);
    }

    static LowFrequencySynthParams sanitize(LowFrequencySynthParams p)
    {
        p.transposeSemitones = finiteClamp(
            p.transposeSemitones, 0.0f, -36.0f, 24.0f);
        p.fineCents = 0.0f;
        p.fundamental = finiteClamp(p.fundamental, 0.96f, 0.0f, 1.0f);
        p.body = finiteClamp(p.body, 0.70f, 0.0f, 1.0f);
        p.loading = finiteClamp(p.loading, 0.82f, 0.0f, 1.0f);
        p.coupling = finiteClamp(p.coupling, 0.50f, 0.0f, 1.0f);
        p.tensionVariance = 0.08f;
        p.excitationPosition = finiteClamp(
            p.excitationPosition, 0.25f, 0.0f, 1.0f);
        p.damping = finiteClamp(p.damping, 0.28f, 0.0f, 1.0f);
        p.attackSeconds = finiteClamp(
            p.attackSeconds, 0.008f, 0.0005f, 2.0f);
        p.decaySeconds = finiteClamp(
            p.decaySeconds, 0.22f, 0.005f, 5.0f);
        p.sustain = finiteClamp(p.sustain, 0.86f, 0.0f, 1.0f);
        p.releaseSeconds = finiteClamp(
            p.releaseSeconds, 0.35f, 0.005f, 8.0f);
        p.glideMs = finiteClamp(p.glideMs, 35.0f, 0.0f, 2000.0f);
        p.pitchTransientSemitones = finiteClamp(
            p.pitchTransientSemitones, 0.0f, -12.0f, 36.0f);
        p.pitchTransientMs = 45.0f;
        p.stereoWidth = 0.12f;
        p.velocitySensitivity = 0.72f;
        p.pressureSensitivity = 0.45f;
        p.outputGainDb = finiteClamp(p.outputGainDb, -8.0f, -36.0f, 6.0f);
        p.upperModeLevel = finiteClamp(
            p.upperModeLevel, 0.18f, 0.0f, 1.0f);
        p.filterCutoffHz = finiteClamp(
            p.filterCutoffHz, 1200.0f, 30.0f, 12000.0f);
        p.filterResonance = finiteClamp(
            p.filterResonance, 0.18f, 0.0f, 1.0f);
        p.filterEnvelopeOctaves = 0.0f;
        p.filterDecayMs = 160.0f;
        p.membraneDrive = finiteClamp(
            p.membraneDrive, 0.28f, 0.0f, 1.0f);
        p.nonlinearity = 0.18f + p.membraneDrive * 0.22f;
        p.wavefold = 0.0f;
        p.driveFeedback = 0.0f;
        p.processedMix = finiteClamp(
            p.processedMix, 0.36f, 0.0f, 1.0f);
        p.amplitudeMotionRateHz = finiteClamp(
            p.amplitudeMotionRateHz, 2.0f, 0.05f, 20.0f);
        p.amplitudeMotionDepth = finiteClamp(
            p.amplitudeMotionDepth, 0.0f, 0.0f, 1.0f);
        p.amplitudeMotionClock = std::round(finiteClamp(
            p.amplitudeMotionClock, 1.0f, 0.0f,
            static_cast<float>(kMotionClockCount - 1u)));
        p.amplitudeMotionDivision = std::round(finiteClamp(
            p.amplitudeMotionDivision, 7.0f, 0.0f,
            static_cast<float>(kMotionDivisionCount - 1u)));
        p.amplitudeMotionPosition = std::round(finiteClamp(
            p.amplitudeMotionPosition, 1.0f, 0.0f,
            static_cast<float>(kAmplitudeMotionPositionCount - 1u)));
        p.shred = finiteClamp(p.shred, 0.0f, 0.0f, 1.0f);
        p.shredFeedback = finiteClamp(
            p.shredFeedback, 0.0f, 0.0f, 1.0f);
        p.shredCircuit = std::round(finiteClamp(
            p.shredCircuit, 0.0f, 0.0f,
            static_cast<float>(kBassShredCircuitCount - 1u)));
        p.shredColor = finiteClamp(
            p.shredColor, 0.55f, 0.0f, 1.0f);
        p.shredMix = finiteClamp(p.shredMix, 0.0f, 0.0f, 1.0f);
        p.valvePreamp = finiteClamp(p.valvePreamp, 0.36f, 0.0f, 1.0f);
        p.powerStage = p.valvePreamp * 0.75f;
        p.supplySag = p.valvePreamp * 0.55f;
        p.ampBass = p.valvePreamp * 0.28f;
        p.ampMid = p.valvePreamp * -0.06f;
        p.ampMidFrequency = 1.0f;
        p.ampTreble = p.valvePreamp * -0.10f;
        p.cabinet = p.valvePreamp * 0.72f;
        p.pedalCircuit = static_cast<float>(BassPedalCircuit::Bypass);
        p.pedalDrive = 0.0f;
        p.pedalTone = 0.5f;
        p.pedalCharacter = 0.5f;
        p.pedalCrossoverHz = 240.0f;
        p.pedalBlend = 0.0f;
        return p;
    }

    BassShredParams bassShredParams() const
    {
        BassShredParams p;
        p.shred = params_.shred;
        p.feedback = params_.shredFeedback;
        p.color = params_.shredColor;
        p.mix = params_.shredMix;
        p.circuit = static_cast<BassShredCircuit>(
            static_cast<uint32_t>(params_.shredCircuit));
        return p;
    }

    BassAmplifierParams amplifierParams() const
    {
        BassAmplifierParams p;
        const float tube = params_.valvePreamp;
        p.valvePreamp = tube;
        p.powerStage = tube * 0.75f;
        p.supplySag = tube * 0.55f;
        p.bassEq = tube * 0.28f;
        p.midEq = tube * -0.06f;
        p.midFrequency = 1u;
        p.trebleEq = tube * -0.10f;
        p.cabinet = tube * 0.72f;
        p.pedalCircuit = BassPedalCircuit::Bypass;
        p.pedalDrive = 0.0f;
        p.pedalTone = 0.5f;
        p.pedalCharacter = 0.5f;
        p.pedalCrossoverHz = 240.0f;
        p.pedalBlend = 0.0f;
        return p;
    }

    float midiFrequency(int midiNote) const
    {
        const float note = static_cast<float>(std::clamp(midiNote, 0, 127))
            + params_.transposeSemitones + params_.fineCents * 0.01f;
        return std::clamp(440.0f * std::exp2((note - 69.0f) / 12.0f),
            8.0f, static_cast<float>(sampleRate_ * 0.20));
    }

    float onePoleCoefficient(float seconds) const
    {
        return 1.0f - static_cast<float>(std::exp(-1.0 / std::max(
            1.0, static_cast<double>(seconds) * sampleRate_)));
    }

    void updateEnvelope()
    {
        switch (stage_) {
        case EnvelopeStage::Idle:
            envelope_ = 0.0f;
            break;
        case EnvelopeStage::Attack: {
            const float step = 1.0f / static_cast<float>(std::max(
                1.0, params_.attackSeconds * sampleRate_));
            envelope_ += step;
            if (envelope_ >= 1.0f) {
                envelope_ = 1.0f;
                stage_ = EnvelopeStage::Decay;
            }
            break;
        }
        case EnvelopeStage::Decay: {
            const float step = (1.0f - params_.sustain)
                / static_cast<float>(std::max(
                    1.0, params_.decaySeconds * sampleRate_));
            envelope_ -= step;
            if (envelope_ <= params_.sustain) {
                envelope_ = params_.sustain;
                stage_ = EnvelopeStage::Sustain;
            }
            break;
        }
        case EnvelopeStage::Sustain:
            envelope_ = params_.sustain;
            if (!gate_) stage_ = EnvelopeStage::Release;
            break;
        case EnvelopeStage::Release:
            envelope_ = std::max(0.0f, envelope_ - releaseStep_);
            if (envelope_ <= 1.0e-5f) {
                envelope_ = 0.0f;
                stage_ = EnvelopeStage::Idle;
            }
            break;
        }
    }

    void updateFrequency()
    {
        if (params_.glideMs <= 0.01f || stage_ == EnvelopeStage::Idle) {
            currentFrequencyHz_ = targetFrequencyHz_;
        } else {
            const float coefficient = 1.0f - static_cast<float>(std::exp(
                -1.0 / std::max(1.0,
                    params_.glideMs * 0.001 * sampleRate_)));
            currentFrequencyHz_ += (targetFrequencyHz_ - currentFrequencyHz_)
                * coefficient;
        }
        pitchTransientEnvelope_ *= static_cast<float>(std::exp(
            -1.0 / std::max(1.0,
                params_.pitchTransientMs * 0.001 * sampleRate_)));
        if (pitchTransientEnvelope_ < 1.0e-7f) {
            pitchTransientEnvelope_ = 0.0f;
        }
    }

    float effectiveFundamentalFrequency() const
    {
        return currentFrequencyHz_ * std::exp2(
            params_.pitchTransientSemitones
                * pitchTransientEnvelope_ / 12.0f);
    }

    void updateModeCoefficients()
    {
        sideLowCoefficient_ = 1.0f - std::exp(-2.0f * kPi * 92.0f
            / static_cast<float>(sampleRate_));
        dcCoefficient_ = 1.0f - std::exp(-2.0f * kPi * 7.0f
            / static_cast<float>(sampleRate_));
        outputSmoothingCoefficient_ = onePoleCoefficient(0.012f);
        limiterReleaseCoefficient_ = onePoleCoefficient(0.080f);
        onsetExcitationCoefficient_ = onePoleCoefficient(0.0015f);
        declickAttackCoefficient_ = onePoleCoefficient(0.0012f);
        declickReleaseCoefficient_ = onePoleCoefficient(0.0025f);
    }

    float partialBandLimit(float fundamentalHz, float multiple) const
    {
        const float partialHz = fundamentalHz * multiple;
        const float fadeStart = static_cast<float>(sampleRate_) * 0.36f;
        const float fadeEnd = static_cast<float>(sampleRate_) * 0.46f;
        const float x = std::clamp((fadeEnd - partialHz)
            / std::max(1.0f, fadeEnd - fadeStart), 0.0f, 1.0f);
        return x * x * (3.0f - 2.0f * x);
    }

    static float processTptSection(float input, float g, float damping,
        float& integratorOne, float& integratorTwo)
    {
        // Topology-preserving state-variable low-pass. Its two integrator
        // states remain continuous when cutoff or damping changes, avoiding
        // the moving feedback discontinuities of the earlier one-pole chain.
        const float a1 = 1.0f / (1.0f + g * (g + damping));
        const float a2 = g * a1;
        const float a3 = g * a2;
        const float delta = input - integratorTwo;
        const float band = a1 * integratorOne + a2 * delta;
        const float low = integratorTwo + a2 * integratorOne + a3 * delta;
        integratorOne = flushDenormal(2.0f * band - integratorOne);
        integratorTwo = flushDenormal(2.0f * low - integratorTwo);
        if (!std::isfinite(integratorOne)
            || !std::isfinite(integratorTwo)) {
            integratorOne = 0.0f;
            integratorTwo = 0.0f;
            return 0.0f;
        }
        integratorOne = std::clamp(integratorOne, -8.0f, 8.0f);
        integratorTwo = std::clamp(integratorTwo, -8.0f, 8.0f);
        return std::clamp(low, -8.0f, 8.0f);
    }

    float amplitudeMotionValue() const
    {
        const float phase = amplitudeMotionPhase_
            - std::floor(amplitudeMotionPhase_);
        return 0.5f - 0.5f * std::cos(phase * 2.0f * kPi);
    }

    float updateAmplitudeMotion()
    {
        const float smoothing = onePoleCoefficient(0.012f);
        amplitudeMotionRateSmoothed_ += (params_.amplitudeMotionRateHz
            - amplitudeMotionRateSmoothed_) * smoothing;
        amplitudeMotionDepthSmoothed_ += (params_.amplitudeMotionDepth
            - amplitudeMotionDepthSmoothed_) * smoothing;
        if (externalAmplitudeMotionActive_) {
            amplitudeMotionPhase_ = externalAmplitudeMotionPhase_;
        } else {
            amplitudeMotionPhase_ += amplitudeMotionRateSmoothed_
                / static_cast<float>(sampleRate_);
            if (amplitudeMotionPhase_ >= 1.0f) {
                amplitudeMotionPhase_ -= std::floor(amplitudeMotionPhase_);
            }
        }
        const float target = 1.0f - amplitudeMotionDepthSmoothed_
            * (1.0f - amplitudeMotionValue());
        amplitudeMotionGain_ += (target - amplitudeMotionGain_)
            * onePoleCoefficient(0.004f);
        return std::clamp(amplitudeMotionGain_, 0.0f, 1.0f);
    }

    void processFilterAndDrive(float& mid, float& side)
    {
        const float smoothing = onePoleCoefficient(0.006f);
        resonanceSmoothed_ += (params_.filterResonance
            - resonanceSmoothed_) * smoothing;
        driveSmoothed_ += (params_.membraneDrive
            - driveSmoothed_) * smoothing;
        processedMixSmoothed_ += (params_.processedMix
            - processedMixSmoothed_) * smoothing;
        const float targetCutoff = std::clamp(params_.filterCutoffHz, 25.0f,
            std::min(19000.0f,
                static_cast<float>(sampleRate_ * 0.41)));
        const float targetCutoffLog2 = std::log2(targetCutoff);
        const float cutoffSeconds = targetCutoffLog2 > filterCutoffLog2_
            ? 0.012f : 0.020f;
        filterCutoffLog2_ += (targetCutoffLog2 - filterCutoffLog2_)
            * onePoleCoefficient(cutoffSeconds);
        effectiveFilterCutoffHz_ = std::exp2(filterCutoffLog2_);

        const float filterInput = mid;

        const float substepRate = static_cast<float>(sampleRate_ * 2.0);
        const float g = std::tan(kPi * effectiveFilterCutoffHz_
            / substepRate);
        const float resonanceCurve = resonanceSmoothed_
            * resonanceSmoothed_;
        const float midQ = 0.70710678f + resonanceCurve * 2.55f;
        const float sideQ = 0.70710678f + resonanceCurve * 1.15f;
        float filteredMid = 0.0f;
        float filteredSide = 0.0f;
        for (uint32_t substep = 0u; substep < 2u; ++substep) {
            const float interpolation = static_cast<float>(substep + 1u)
                * 0.5f;
            const float interpolatedInput = lerp(
                previousFilterInput_, filterInput, interpolation);
            const float interpolatedSide = lerp(
                previousFilterSideInput_, side, interpolation);
            const float midLow = processTptSection(interpolatedInput, g,
                1.0f / midQ, filterStages_[0u], filterStages_[1u]);
            const float sideLow = processTptSection(interpolatedSide, g,
                1.0f / sideQ, sideFilterStages_[0u],
                sideFilterStages_[1u]);
            filteredMid += midLow * 0.5f;
            filteredSide += sideLow * 0.5f;
        }
        previousFilterInput_ = filterInput;
        previousFilterSideInput_ = side;
        mid = filteredMid;
        side = filteredSide;
    }

    static std::array<uint32_t, 3u> modeIdentity(uint32_t mode)
    {
        constexpr std::array<std::array<uint32_t, 3u>, kModeCount>
            identities {{
                {{ 0u, 1u, 0u }}, {{ 1u, 1u, 0u }}, {{ 1u, 1u, 1u }},
                {{ 0u, 2u, 0u }}, {{ 2u, 1u, 0u }}, {{ 2u, 1u, 1u }},
                {{ 1u, 2u, 0u }}, {{ 1u, 2u, 1u }},
                {{ 3u, 1u, 0u }}, {{ 3u, 1u, 1u }},
                {{ 0u, 3u, 0u }}, {{ 4u, 1u, 0u }},
            }};
        return identities[std::min(mode, kModeCount - 1u)];
    }

    static float membraneShape(uint32_t mode, float radius, float theta)
    {
        const auto identity = modeIdentity(mode);
        const uint32_t angular = identity[0u];
        const uint32_t radial = identity[1u];
        const bool sineVariant = identity[2u] != 0u;
        radius = std::clamp(radius, 0.0f, 0.98f);
        const float radialShape = radial == 1u
            ? std::cos(radius * kPi * 0.5f)
            : std::cos(radius * (static_cast<float>(radial) - 0.5f) * kPi);
        if (angular == 0u) return radialShape;
        const float angularShape = sineVariant
            ? std::sin(static_cast<float>(angular) * theta)
            : std::cos(static_cast<float>(angular) * theta);
        return radialShape * angularShape
            * std::pow(std::max(0.03f, radius),
                std::min(2.0f, static_cast<float>(angular) * 0.52f));
    }

    static float excitationShape(uint32_t mode, float position)
    {
        const float radius = lerp(0.04f, 0.94f,
            std::clamp(position, 0.0f, 1.0f));
        // Membrane Kick's default strike vector is approximately
        // (0.18, -0.08); retain that azimuth while exposing its radius.
        constexpr float strikeTheta = -0.41822433f;
        return membraneShape(mode, radius, strikeTheta);
    }

    void initializeSurfacePickups()
    {
        for (uint32_t patch = 0u; patch < kSurfacePatchCount; ++patch) {
            const uint32_t ring = patch / 4u;
            const uint32_t spoke = patch % 4u;
            const float radius = 0.18f + static_cast<float>(ring) * 0.22f;
            const float theta = static_cast<float>(spoke) * kPi * 0.5f
                + static_cast<float>(ring & 1u) * kPi * 0.25f;
            const float x = radius * std::cos(theta);
            const float y = radius * std::sin(theta);
            const float stereoPan = std::clamp(
                x * 0.5f + 0.5f, 0.0f, 1.0f) * kPi * 0.5f;
            surfacePanGains_[patch] = {
                std::cos(stereoPan), std::sin(stereoPan)
            };
            for (uint32_t mode = 0u; mode < kModeCount; ++mode) {
                surfaceModeShapes_[patch][mode] = membraneShape(
                    mode, radius, std::atan2(y, x));
            }
        }
    }

    static float safeOutput(float value)
    {
        if (!std::isfinite(value)) return 0.0f;
        return std::clamp(value, -1.0f, 1.0f);
    }

    void clearSignalState()
    {
        modes_.fill({});
        pendingExcitation_ = 0.0f;
        sideLow_ = 0.0f;
        dcLowLeft_ = 0.0f;
        dcLowRight_ = 0.0f;
        limiterGain_ = 1.0f;
        filterStages_.fill(0.0f);
        sideFilterStages_.fill(0.0f);
        previousFilterInput_ = 0.0f;
        previousFilterSideInput_ = 0.0f;
        amplifier_.reset();
        bassShred_.reset();
    }

    double sampleRate_ = 48000.0;
    LowFrequencySynthParams params_ {};
    std::array<ModeState, kModeCount> modes_ {};
    std::array<std::array<float, kModeCount>, kSurfacePatchCount>
        surfaceModeShapes_ {};
    std::array<std::array<float, 2u>, kSurfacePatchCount>
        surfacePanGains_ {};
    float currentFrequencyHz_ = 55.0f;
    float targetFrequencyHz_ = 55.0f;
    float envelope_ = 0.0f;
    float releaseStep_ = 0.0f;
    float pitchTransientEnvelope_ = 0.0f;
    float pendingExcitation_ = 0.0f;
    float onsetExcitationCoefficient_ = 0.014f;
    float declickGain_ = 0.0f;
    float declickAttackCoefficient_ = 0.017f;
    float declickReleaseCoefficient_ = 0.008f;
    float pressure_ = 0.0f;
    float noteVelocity_ = 1.0f;
    float sideLow_ = 0.0f;
    float sideLowCoefficient_ = 0.012f;
    float dcLowLeft_ = 0.0f;
    float dcLowRight_ = 0.0f;
    float dcCoefficient_ = 0.001f;
    float smoothedOutputGain_ = 0.4f;
    float outputSmoothingCoefficient_ = 0.002f;
    float limiterGain_ = 1.0f;
    float limiterReleaseCoefficient_ = 0.0002f;
    std::array<float, 2u> filterStages_ {};
    std::array<float, 2u> sideFilterStages_ {};
    float filterCutoffLog2_ = std::log2(1200.0f);
    float effectiveFilterCutoffHz_ = 1200.0f;
    float resonanceSmoothed_ = 0.18f;
    float driveSmoothed_ = 0.28f;
    float processedMixSmoothed_ = 0.36f;
    float amplitudeMotionRateSmoothed_ = 2.0f;
    float amplitudeMotionDepthSmoothed_ = 0.0f;
    float amplitudeMotionPhase_ = 0.0f;
    float amplitudeMotionGain_ = 1.0f;
    float amplitudeMotionPostMixSmoothed_ = 1.0f;
    bool externalAmplitudeMotionActive_ = false;
    float externalAmplitudeMotionPhase_ = 0.0f;
    float previousFilterInput_ = 0.0f;
    float previousFilterSideInput_ = 0.0f;
    BassAmplifierCircuit amplifier_ {};
    BassShredStereo bassShred_ {};
    int currentMidiNote_ = 33;
    EnvelopeStage stage_ = EnvelopeStage::Idle;
    bool gate_ = false;
    bool signalStateCleared_ = true;
};

} // namespace s3g
