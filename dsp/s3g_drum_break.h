#pragma once

#include "s3g_drum_character.h"
#include "s3g_drum_primitives.h"
#include "s3g_math.h"
#include "s3g_realtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace s3g {

enum class DrumBreakArticulation : uint8_t {
    Kick = 0u,
    Snare = 1u,
    Tom = 2u,
    HiHat = 3u,
};

inline int drumBreakCanonicalMidiNote(DrumBreakArticulation articulation)
{
    switch (articulation) {
    case DrumBreakArticulation::Snare: return 38;
    case DrumBreakArticulation::Tom: return 45;
    case DrumBreakArticulation::HiHat: return 42;
    case DrumBreakArticulation::Kick:
    default: return 36;
    }
}

inline DrumBreakArticulation drumBreakArticulationForMidiNote(int note)
{
    // Conventional GM-family placement keeps one tracker lane immediately
    // playable while reducing every incoming note to one synthesis family.
    if (note <= 36) return DrumBreakArticulation::Kick;
    if (note <= 40) return DrumBreakArticulation::Snare;
    switch (note) {
    case 41: case 43: case 45: case 47: case 48: case 50:
        return DrumBreakArticulation::Tom;
    case 42: case 44: case 46:
        return DrumBreakArticulation::HiHat;
    default:
        return DrumBreakArticulation::HiHat;
    }
}

// The first sixteen fields are latched per hit. Character, width and output
// remain live, matching the rest of the s3g Drum family.
struct DrumBreakParams {
    float lowTuneHz = 52.0f;
    float noteTracking = 0.20f;
    float lowDropSemitones = 14.0f;
    float lowDecaySeconds = 0.42f;
    float lowWeight = 0.78f;
    float kickLevelDb = 0.0f;
    float kickBandHz = 140.0f;
    float midTuneHz = 175.0f;
    float midBody = 0.56f;
    float midCrack = 0.72f;
    float midDecaySeconds = 0.32f;
    float snareLevelDb = 0.0f;
    float snareBandHz = 1800.0f;
    float highTone = 0.56f;
    float highTexture = 0.66f;
    float highDecaySeconds = 0.12f;
    float hiHatLevelDb = 0.0f;
    float hiHatBandHz = 8200.0f;
    float tomTuneHz = 118.0f;
    float tomDecaySeconds = 0.52f;
    float tomLevelDb = 0.0f;
    float tomBandHz = 320.0f;
    float transient = 0.62f;
    float bleed = 0.20f;
    float room = 0.28f;
    float age = 0.25f;
    DrumCharacterParams character {};
    float stereoWidth = 0.38f;
    float velocitySensitivity = 0.90f;
    float outputGainDb = -8.0f;
};

inline DrumBreakParams drumSanitizeBreakParams(DrumBreakParams params)
{
    params.lowTuneHz = clamp(drumFiniteOr(params.lowTuneHz, 52.0f),
        28.0f, 96.0f);
    params.noteTracking = clamp(
        drumFiniteOr(params.noteTracking, 0.20f), 0.0f, 1.0f);
    params.lowDropSemitones = clamp(
        drumFiniteOr(params.lowDropSemitones, 14.0f), 0.0f, 42.0f);
    params.lowDecaySeconds = clamp(
        drumFiniteOr(params.lowDecaySeconds, 0.42f), 0.05f, 2.0f);
    params.lowWeight = clamp(
        drumFiniteOr(params.lowWeight, 0.78f), 0.0f, 1.0f);
    params.kickLevelDb = clamp(
        drumFiniteOr(params.kickLevelDb, 0.0f), -24.0f, 12.0f);
    params.kickBandHz = clamp(
        drumFiniteOr(params.kickBandHz, 140.0f), 55.0f, 900.0f);
    params.midTuneHz = clamp(drumFiniteOr(params.midTuneHz, 175.0f),
        90.0f, 380.0f);
    params.midBody = clamp(
        drumFiniteOr(params.midBody, 0.56f), 0.0f, 1.0f);
    params.midCrack = clamp(
        drumFiniteOr(params.midCrack, 0.72f), 0.0f, 1.0f);
    params.midDecaySeconds = clamp(
        drumFiniteOr(params.midDecaySeconds, 0.32f), 0.04f, 1.8f);
    params.snareLevelDb = clamp(
        drumFiniteOr(params.snareLevelDb, 0.0f), -24.0f, 12.0f);
    params.snareBandHz = clamp(
        drumFiniteOr(params.snareBandHz, 1800.0f), 300.0f, 6000.0f);
    params.highTone = clamp(
        drumFiniteOr(params.highTone, 0.56f), 0.0f, 1.0f);
    params.highTexture = clamp(
        drumFiniteOr(params.highTexture, 0.66f), 0.0f, 1.0f);
    params.highDecaySeconds = clamp(
        drumFiniteOr(params.highDecaySeconds, 0.12f), 0.018f, 1.2f);
    params.hiHatLevelDb = clamp(
        drumFiniteOr(params.hiHatLevelDb, 0.0f), -24.0f, 12.0f);
    params.hiHatBandHz = clamp(
        drumFiniteOr(params.hiHatBandHz, 8200.0f), 2200.0f, 14000.0f);
    params.tomTuneHz = clamp(drumFiniteOr(params.tomTuneHz, 118.0f),
        58.0f, 260.0f);
    params.tomDecaySeconds = clamp(
        drumFiniteOr(params.tomDecaySeconds, 0.52f), 0.08f, 2.4f);
    params.tomLevelDb = clamp(
        drumFiniteOr(params.tomLevelDb, 0.0f), -24.0f, 12.0f);
    params.tomBandHz = clamp(
        drumFiniteOr(params.tomBandHz, 320.0f), 80.0f, 1800.0f);
    params.transient = clamp(
        drumFiniteOr(params.transient, 0.62f), 0.0f, 1.0f);
    params.bleed = clamp(drumFiniteOr(params.bleed, 0.20f), 0.0f, 1.0f);
    params.room = clamp(drumFiniteOr(params.room, 0.28f), 0.0f, 1.0f);
    params.age = clamp(drumFiniteOr(params.age, 0.25f), 0.0f, 1.0f);
    params.character.drive = clamp(
        drumFiniteOr(params.character.drive, 0.0f), 0.0f, 1.0f);
    params.character.bias = clamp(
        drumFiniteOr(params.character.bias, 0.0f), -1.0f, 1.0f);
    params.character.compression = clamp(
        drumFiniteOr(params.character.compression, 0.0f), 0.0f, 1.0f);
    params.character.sampleRateReduction = clamp(
        drumFiniteOr(params.character.sampleRateReduction, 0.0f),
        0.0f, 1.0f);
    params.character.bitDepthReduction = clamp(
        drumFiniteOr(params.character.bitDepthReduction, 0.0f),
        0.0f, 1.0f);
    params.character.reconstruction = clamp(
        drumFiniteOr(params.character.reconstruction, 0.0f), 0.0f, 1.0f);
    params.character.tone = clamp(
        drumFiniteOr(params.character.tone, 0.0f), -1.0f, 1.0f);
    params.stereoWidth = clamp(
        drumFiniteOr(params.stereoWidth, 0.38f), 0.0f, 1.0f);
    params.velocitySensitivity = clamp(
        drumFiniteOr(params.velocitySensitivity, 0.90f), 0.0f, 1.0f);
    params.outputGainDb = clamp(
        drumFiniteOr(params.outputGainDb, -8.0f), -36.0f, 12.0f);
    return params;
}

inline double drumBreakTailSeconds(const DrumBreakParams& source,
    DrumBreakArticulation articulation, double sampleRate = 48000.0)
{
    (void)sampleRate;
    const DrumBreakParams params = drumSanitizeBreakParams(source);
    double principal = params.lowDecaySeconds;
    switch (articulation) {
    case DrumBreakArticulation::Snare:
        principal = params.midDecaySeconds;
        break;
    case DrumBreakArticulation::Tom:
        principal = params.tomDecaySeconds;
        break;
    case DrumBreakArticulation::HiHat:
        principal = params.highDecaySeconds;
        break;
    case DrumBreakArticulation::Kick:
    default:
        break;
    }
    const double roomTail = params.room > 1.0e-6f
        ? (0.12 + params.room * 0.88) * (0.72 + params.age * 0.34)
        : 0.0;
    return std::min(40.0, std::max({
        0.08,
        principal * 2.20,
        roomTail * 2.20,
        params.character.drive > 1.0e-6f ? 0.50 : 0.0,
    }));
}

class DrumBreak {
public:
    static constexpr uint32_t kVoiceCount = 24u;

    void prepare(double sampleRate)
    {
        sampleRate_ = drumSafeSampleRate(sampleRate);
        character_.prepare(sampleRate_);
        smoothingCoefficient_ = drumOnePoleTimeCoefficient(
            0.005f, static_cast<float>(sampleRate_));
        outputGainTarget_ = dbToGain(params_.outputGainDb);
        reset();
    }

    void reset()
    {
        voices_.fill({});
        triggerCounter_ = 0u;
        activeVoiceCount_ = 0u;
        snapGlobalParameters();
        character_.reset();
    }

    void setParams(DrumBreakParams params)
    {
        const bool wasInactive = activeVoiceCount_ == 0u
            && !character_.active();
        params_ = drumSanitizeBreakParams(params);
        outputGainTarget_ = dbToGain(params_.outputGainDb);
        character_.setParams(params_.character);
        if (wasInactive) {
            snapGlobalParameters();
            character_.reset();
        }
    }

    DrumBreakParams params() const { return params_; }

    void trigger(DrumBreakArticulation articulation,
        float velocity = 1.0f, int midiNote = -1)
    {
        velocity = clamp(drumFiniteOr(velocity, 1.0f), 0.0f, 1.0f);
        if (midiNote < 0) midiNote = drumBreakCanonicalMidiNote(articulation);
        midiNote = std::max(0, std::min(127, midiNote));
        if (lerp(1.0f, velocity,
                params_.velocitySensitivity) <= 1.0e-7f) return;

        uint32_t selected = 0u;
        float quietest = std::numeric_limits<float>::max();
        bool foundInactive = false;
        for (uint32_t index = 0u; index < voices_.size(); ++index) {
            const Voice& candidate = voices_[index];
            if (!candidate.active) {
                selected = index;
                foundInactive = true;
                break;
            }
            const float activity = voiceActivitySquared(candidate);
            if (activity < quietest) {
                quietest = activity;
                selected = index;
            }
        }

        Voice& voice = voices_[selected];
        voice = {};
        initialiseVoice(voice, selected, ++triggerCounter_, articulation,
            velocity, midiNote);
        if (foundInactive) ++activeVoiceCount_;
        else activeVoiceCount_ = countActiveVoices();
    }

    void processFrame(float& left, float& right)
    {
        smoothGlobalParameters();
        float mid = 0.0f;
        float side = 0.0f;
        uint32_t remaining = 0u;
        for (Voice& voice : voices_) {
            if (!voice.active) continue;
            processVoice(voice, mid, side);
            if (voice.active) ++remaining;
        }
        activeVoiceCount_ = remaining;

        float frameLeft = mid + side * smoothedWidth_;
        float frameRight = mid - side * smoothedWidth_;
        character_.processFrame(frameLeft, frameRight);
        if (params_.stereoWidth == 0.0f) {
            const float mono = (frameLeft + frameRight) * 0.5f;
            frameLeft = mono;
            frameRight = mono;
        }
        frameLeft *= smoothedOutputGain_;
        frameRight *= smoothedOutputGain_;
        left = drumSafeOutput(frameLeft);
        right = drumSafeOutput(frameRight);
    }

    void processBlock(float* left, float* right, uint32_t frames)
    {
        for (uint32_t frame = 0u; frame < frames; ++frame) {
            float frameLeft = 0.0f;
            float frameRight = 0.0f;
            processFrame(frameLeft, frameRight);
            if (left && right) {
                left[frame] = frameLeft;
                right[frame] = frameRight;
            } else if (left) {
                left[frame] = (frameLeft + frameRight) * 0.5f;
            } else if (right) {
                right[frame] = (frameLeft + frameRight) * 0.5f;
            }
        }
    }

    bool active() const
    {
        return activeVoiceCount_ != 0u || character_.active();
    }

private:
    static constexpr uint32_t kModeCount = 5u;

    struct Voice {
        bool active = false;
        DrumBreakArticulation articulation = DrumBreakArticulation::Kick;
        float velocityGain = 1.0f;
        float brightness = 1.0f;
        float fundamentalHz = 52.0f;
        float pitchDropSemitones = 0.0f;
        float lowLevel = 0.0f;
        float midLevel = 0.0f;
        float highLevel = 0.0f;
        float transientLevel = 0.0f;
        float roomLevel = 0.0f;
        float voiceGain = 1.0f;
        float age = 0.0f;
        std::array<DrumModalResonator, kModeCount> modes {};
        std::array<float, kModeCount> modeWeights {};
        DrumExponentialEnvelope pitchEnvelope {};
        DrumExponentialEnvelope lowEnvelope {};
        DrumExponentialEnvelope midEnvelope {};
        DrumExponentialEnvelope highEnvelope {};
        DrumExponentialEnvelope transientEnvelope {};
        DrumExponentialEnvelope roomEnvelope {};
        DrumRandom random {};
        float lowPhase = 0.0f;
        float lowNoiseState = 0.0f;
        float midLowState = 0.0f;
        float midBandState = 0.0f;
        float highLowState = 0.0f;
        float highBandState = 0.0f;
        float sideLowState = 0.0f;
        float sideBandState = 0.0f;
        float roomMidState = 0.0f;
        float roomSideState = 0.0f;
        float outputLowState = 0.0f;
        float outputBandState = 0.0f;
        float sideOutputLowState = 0.0f;
        float sideOutputBandState = 0.0f;
        float lowCoefficient = 0.02f;
        float midHighpassCoefficient = 0.02f;
        float midLowpassCoefficient = 0.2f;
        float highHighpassCoefficient = 0.12f;
        float highLowpassCoefficient = 0.5f;
        float roomCoefficient = 0.02f;
        float outputHighpassCoefficient = 0.01f;
        float outputLowpassCoefficient = 0.2f;
        uint32_t ageSamples = 0u;
        uint32_t attackSamples = 1u;
        uint32_t maximumAgeSamples = 1u;
    };

    void initialiseVoice(Voice& voice, uint32_t voiceIndex,
        uint64_t triggerIndex, DrumBreakArticulation articulation,
        float velocity, int midiNote)
    {
        const float sr = static_cast<float>(sampleRate_);
        const int canonical = drumBreakCanonicalMidiNote(articulation);
        const float noteRatio = std::exp2(
            static_cast<float>(midiNote - canonical)
            * params_.noteTracking / 12.0f);
        voice.active = true;
        voice.articulation = articulation;
        voice.velocityGain = lerp(1.0f, velocity,
            params_.velocitySensitivity);
        voice.brightness = lerp(1.0f, 0.42f + velocity * 0.58f,
            params_.velocitySensitivity);
        voice.pitchDropSemitones = articulation
                == DrumBreakArticulation::Kick
            ? params_.lowDropSemitones
            : articulation == DrumBreakArticulation::Tom
                ? params_.lowDropSemitones * 0.18f
                : articulation == DrumBreakArticulation::Snare
                    ? params_.lowDropSemitones * 0.10f : 0.0f;
        float fundamental = params_.lowTuneHz;
        switch (articulation) {
        case DrumBreakArticulation::Snare:
            fundamental = params_.midTuneHz;
            break;
        case DrumBreakArticulation::Tom:
            fundamental = params_.tomTuneHz;
            break;
        case DrumBreakArticulation::HiHat:
            fundamental = lerp(420.0f, 820.0f, params_.highTone);
            break;
        case DrumBreakArticulation::Kick:
        default:
            break;
        }
        voice.fundamentalHz = clamp(fundamental * noteRatio,
            24.0f, sr * 0.20f);
        voice.age = params_.age;
        voice.random.reset(drumMixedSeed(voiceIndex, triggerIndex,
            0x4252454bu));

        float principalDecay = params_.lowDecaySeconds;
        if (articulation == DrumBreakArticulation::Kick) {
            principalDecay = params_.lowDecaySeconds;
            voice.lowLevel = 0.36f + params_.lowWeight * 0.48f;
            // Kick bleed is deliberately restrained. The post-voice band-pass
            // can isolate it further without making global cohesion useless to
            // snare, tom and hi-hat hits.
            voice.midLevel = params_.bleed * 0.035f;
            voice.highLevel = params_.bleed * 0.012f;
            voice.modeWeights = {{
                0.25f + params_.lowWeight * 0.25f,
                params_.lowWeight * 0.11f,
                params_.bleed * 0.012f,
                params_.bleed * 0.006f,
                0.0f,
            }};
        } else if (articulation == DrumBreakArticulation::Snare) {
            principalDecay = params_.midDecaySeconds;
            voice.lowLevel = params_.bleed * 0.095f;
            voice.midLevel = 0.28f + params_.midBody * 0.34f;
            voice.highLevel = 0.12f + params_.midCrack * 0.52f;
            voice.modeWeights = {{
                0.20f + params_.midBody * 0.28f,
                0.14f + params_.midBody * 0.12f,
                0.08f + params_.midBody * 0.08f,
                params_.midCrack * 0.055f,
                params_.midCrack * 0.030f,
            }};
        } else if (articulation == DrumBreakArticulation::Tom) {
            principalDecay = params_.tomDecaySeconds;
            voice.lowLevel = 0.22f + params_.lowWeight * 0.20f;
            voice.midLevel = 0.18f + params_.midBody * 0.28f;
            voice.highLevel = params_.bleed * 0.070f;
            voice.modeWeights = {{
                0.27f + params_.midBody * 0.18f,
                0.18f + params_.midBody * 0.10f,
                0.11f + params_.lowWeight * 0.055f,
                0.070f + params_.bleed * 0.030f,
                0.035f,
            }};
        } else {
            principalDecay = params_.highDecaySeconds;
            voice.lowLevel = params_.bleed * 0.015f;
            voice.midLevel = params_.bleed * 0.10f;
            voice.highLevel = 0.22f + params_.highTexture * 0.46f;
            voice.modeWeights = {{
                params_.highTexture * 0.070f,
                params_.highTexture * 0.060f,
                params_.highTexture * 0.052f,
                params_.highTexture * 0.044f,
                params_.highTexture * 0.036f,
            }};
        }
        float transientScale = 0.34f;
        float roomScale = 0.16f;
        switch (articulation) {
        case DrumBreakArticulation::Snare:
            roomScale = 0.28f;
            break;
        case DrumBreakArticulation::Tom:
            transientScale = 0.28f;
            roomScale = 0.24f;
            break;
        case DrumBreakArticulation::HiHat:
            transientScale = 0.42f;
            roomScale = 0.22f;
            break;
        case DrumBreakArticulation::Kick:
        default:
            break;
        }
        voice.transientLevel = params_.transient * transientScale;
        voice.roomLevel = params_.room * roomScale;

        float modalAnchor = voice.fundamentalHz;
        std::array<float, kModeCount> ratios {{
            1.0f, 1.58f, 2.31f, 3.17f, 4.21f
        }};
        float modalDecayScale = 1.0f;
        switch (articulation) {
        case DrumBreakArticulation::Snare:
            ratios = {{1.0f, 1.47f, 2.08f, 2.79f, 3.73f}};
            break;
        case DrumBreakArticulation::Tom:
            ratios = {{1.0f, 1.52f, 2.13f, 2.83f, 3.62f}};
            break;
        case DrumBreakArticulation::HiHat:
            modalAnchor = lerp(1800.0f, 5600.0f,
                params_.highTone) * noteRatio;
            ratios = {{1.0f, 1.39f, 1.93f, 2.71f, 3.62f}};
            modalDecayScale = 0.45f;
            break;
        case DrumBreakArticulation::Kick:
        default:
            break;
        }
        for (uint32_t mode = 0u; mode < kModeCount; ++mode) {
            const float decay = principalDecay * modalDecayScale
                * (1.0f - static_cast<float>(mode) * 0.105f);
            voice.modes[mode].configure(
                std::min(sr * 0.44f, modalAnchor * ratios[mode]),
                std::max(0.012f, decay), sr);
            const float strike = (mode == 0u ? 1.0f
                : (voice.random.bipolar() >= 0.0f ? 1.0f : -1.0f))
                * (0.84f + voice.random.unipolar() * 0.16f);
            voice.modes[mode].strike(strike,
                strike * voice.random.bipolar() * 0.18f);
        }

        const float pitchSeconds = articulation
                == DrumBreakArticulation::Kick
            ? lerp(0.020f, 0.072f, params_.lowDecaySeconds / 2.0f)
            : articulation == DrumBreakArticulation::Tom ? 0.028f : 0.012f;
        voice.pitchEnvelope.configure(pitchSeconds, sr, 0.01f);
        voice.pitchEnvelope.trigger();
        const float lowDecay = articulation == DrumBreakArticulation::Kick
            ? params_.lowDecaySeconds
            : articulation == DrumBreakArticulation::Tom
                ? params_.tomDecaySeconds * 0.85f
                : std::max(0.04f, params_.midDecaySeconds * 0.62f);
        voice.lowEnvelope.configure(lowDecay, sr);
        voice.lowEnvelope.trigger();
        const float midDecay = articulation == DrumBreakArticulation::Snare
            ? params_.midDecaySeconds
            : articulation == DrumBreakArticulation::Tom
                ? params_.tomDecaySeconds * 0.72f
                : std::max(0.028f, principalDecay * 0.48f);
        voice.midEnvelope.configure(midDecay, sr);
        voice.midEnvelope.trigger();
        float highDecay = std::max(0.018f,
            params_.lowDecaySeconds * 0.16f);
        if (articulation == DrumBreakArticulation::Snare) {
            highDecay = params_.midDecaySeconds * 0.82f;
        } else if (articulation == DrumBreakArticulation::Tom) {
            highDecay = params_.tomDecaySeconds * 0.28f;
        } else if (articulation == DrumBreakArticulation::HiHat) {
            highDecay = params_.highDecaySeconds;
        }
        voice.highEnvelope.configure(highDecay, sr);
        voice.highEnvelope.trigger();
        const float transientSeconds = lerp(0.0035f, 0.014f,
            1.0f - params_.transient);
        voice.transientEnvelope.configure(transientSeconds, sr);
        voice.transientEnvelope.trigger();
        const float roomSeconds = (0.12f + params_.room * 0.88f)
            * (0.72f + params_.age * 0.34f);
        voice.roomEnvelope.configure(roomSeconds, sr);
        voice.roomEnvelope.trigger();

        const float ageBandwidth = lerp(1.0f, 0.48f, params_.age);
        voice.lowCoefficient = drumOnePoleFrequencyCoefficient(
            lerp(105.0f, 230.0f, params_.lowWeight), sr);
        voice.midHighpassCoefficient = drumOnePoleFrequencyCoefficient(
            lerp(220.0f, 720.0f, params_.midCrack), sr);
        voice.midLowpassCoefficient = drumOnePoleFrequencyCoefficient(
            lerp(3100.0f, 8200.0f, params_.midCrack)
                * ageBandwidth * voice.brightness, sr);
        const float highHighpass = lerp(
            1700.0f, 6300.0f, params_.highTone);
        const float highLowpass = lerp(
            7600.0f, 18500.0f, params_.highTone);
        voice.highHighpassCoefficient = drumOnePoleFrequencyCoefficient(
            highHighpass, sr);
        voice.highLowpassCoefficient = drumOnePoleFrequencyCoefficient(
            highLowpass * ageBandwidth * voice.brightness, sr);
        voice.roomCoefficient = drumOnePoleFrequencyCoefficient(
            lerp(900.0f, 4300.0f, 1.0f - params_.age), sr);

        float bandCenterHz = params_.kickBandHz;
        float bandLowerRatio = 0.18f;
        float bandUpperRatio = 4.5f;
        float levelDb = params_.kickLevelDb;
        switch (articulation) {
        case DrumBreakArticulation::Snare:
            bandCenterHz = params_.snareBandHz;
            bandLowerRatio = 0.18f;
            bandUpperRatio = 3.2f;
            levelDb = params_.snareLevelDb;
            break;
        case DrumBreakArticulation::Tom:
            bandCenterHz = params_.tomBandHz;
            bandLowerRatio = 0.20f;
            bandUpperRatio = 4.0f;
            levelDb = params_.tomLevelDb;
            break;
        case DrumBreakArticulation::HiHat:
            bandCenterHz = params_.hiHatBandHz;
            bandLowerRatio = 0.32f;
            bandUpperRatio = 2.3f;
            levelDb = params_.hiHatLevelDb;
            break;
        case DrumBreakArticulation::Kick:
        default:
            break;
        }
        const float bandLowerHz = clamp(bandCenterHz * bandLowerRatio,
            18.0f, sr * 0.40f);
        const float bandUpperHz = clamp(bandCenterHz * bandUpperRatio,
            bandLowerHz * 1.25f, sr * 0.44f);
        voice.outputHighpassCoefficient =
            drumOnePoleFrequencyCoefficient(bandLowerHz, sr);
        voice.outputLowpassCoefficient =
            drumOnePoleFrequencyCoefficient(bandUpperHz, sr);
        voice.voiceGain = dbToGain(levelDb);

        // A short raised-cosine onset prevents the discontinuous click that a
        // block-started noise burst or already-struck resonator can produce.
        voice.attackSamples = std::max(1u, static_cast<uint32_t>(
            sr * lerp(0.00055f, 0.00020f, params_.transient)));
        voice.maximumAgeSamples = drumLongestTailSamples(sampleRate_, {
            static_cast<double>(principalDecay) * 2.20,
            static_cast<double>(roomSeconds) * 2.20,
            static_cast<double>(transientSeconds) * 2.20,
        }, 0.08, 40.0);
    }

    static float voiceActivitySquared(const Voice& voice)
    {
        float modal = 0.0f;
        for (uint32_t mode = 0u; mode < kModeCount; ++mode) {
            modal += voice.modes[mode].magnitudeSquared()
                * voice.modeWeights[mode] * voice.modeWeights[mode];
        }
        const float envelopes = voice.lowEnvelope.value() * voice.lowLevel
            + voice.midEnvelope.value() * voice.midLevel
            + voice.highEnvelope.value() * voice.highLevel
            + voice.roomEnvelope.value() * voice.roomLevel;
        return modal + envelopes * envelopes;
    }

    void processVoice(Voice& voice, float& mid, float& side)
    {
        const float sr = static_cast<float>(sampleRate_);
        const float randomMid = voice.random.bipolar();
        const float randomSide = (voice.random.bipolar()
            - voice.random.bipolar()) * 0.5f;

        float modal = 0.0f;
        for (uint32_t mode = 0u; mode < kModeCount; ++mode) {
            modal += voice.modes[mode].process() * voice.modeWeights[mode];
        }

        const float pitch = voice.pitchEnvelope.process();
        const float lowFrequency = clamp(voice.fundamentalHz
                * std::exp2(voice.pitchDropSemitones * pitch / 12.0f),
            22.0f, sr * 0.42f);
        voice.lowPhase += lowFrequency / sr;
        voice.lowPhase -= std::floor(voice.lowPhase);
        const float lowOscillator = std::sin(2.0f * kPi * voice.lowPhase);
        voice.lowNoiseState += (randomMid - voice.lowNoiseState)
            * voice.lowCoefficient;
        const float low = (lowOscillator * 0.88f
                + voice.lowNoiseState * 0.12f)
            * voice.lowEnvelope.process() * voice.lowLevel;

        voice.midLowState += (randomMid - voice.midLowState)
            * voice.midHighpassCoefficient;
        const float midHigh = randomMid - voice.midLowState;
        voice.midBandState += (midHigh - voice.midBandState)
            * voice.midLowpassCoefficient;
        const float midNoise = voice.midBandState
            * voice.midEnvelope.process() * voice.midLevel;

        voice.highLowState += (randomMid - voice.highLowState)
            * voice.highHighpassCoefficient;
        const float highPass = randomMid - voice.highLowState;
        voice.highBandState += (highPass - voice.highBandState)
            * voice.highLowpassCoefficient;
        voice.sideLowState += (randomSide - voice.sideLowState)
            * voice.highHighpassCoefficient;
        const float sideHigh = randomSide - voice.sideLowState;
        voice.sideBandState += (sideHigh - voice.sideBandState)
            * voice.highLowpassCoefficient;
        const float highEnvelope = voice.highEnvelope.process();
        const float highMid = voice.highBandState * highEnvelope
            * voice.highLevel;
        const float highSide = voice.sideBandState * highEnvelope
            * voice.highLevel * 0.72f;

        const float transientEnvelope = voice.transientEnvelope.process();
        const float transientHit = (midHigh * 0.72f + highPass * 0.28f)
            * transientEnvelope * voice.transientLevel;

        voice.roomMidState += (randomMid - voice.roomMidState)
            * voice.roomCoefficient;
        voice.roomSideState += (randomSide - voice.roomSideState)
            * voice.roomCoefficient;
        const float roomEnvelope = voice.roomEnvelope.process();
        const float roomBloom = std::min(1.0f,
            static_cast<float>(voice.ageSamples)
                / std::max(1.0f, sr * 0.008f));
        const float roomMid = voice.roomMidState * roomEnvelope
            * voice.roomLevel * roomBloom;
        const float roomSide = voice.roomSideState * roomEnvelope
            * voice.roomLevel * roomBloom;

        const float onsetPhase = std::min(1.0f,
            static_cast<float>(voice.ageSamples) / voice.attackSamples);
        const float onset = 0.5f - 0.5f * std::cos(kPi * onsetPhase);
        const float ageFlutter = 1.0f - voice.age * 0.035f
            * std::max(0.0f, voice.random.bipolar() - 0.80f) * 5.0f;
        const float velocity = voice.velocityGain;
        const float rawMid = low + midNoise + modal + highMid
            + transientHit + roomMid;
        const float rawSide = highSide + roomSide;
        voice.outputLowState += (rawMid - voice.outputLowState)
            * voice.outputHighpassCoefficient;
        const float outputHigh = rawMid - voice.outputLowState;
        voice.outputBandState += (outputHigh - voice.outputBandState)
            * voice.outputLowpassCoefficient;
        voice.sideOutputLowState += (rawSide - voice.sideOutputLowState)
            * voice.outputHighpassCoefficient;
        const float sideOutputHigh = rawSide - voice.sideOutputLowState;
        voice.sideOutputBandState +=
            (sideOutputHigh - voice.sideOutputBandState)
            * voice.outputLowpassCoefficient;
        const float voiceScale = voice.voiceGain * velocity * onset
            * ageFlutter;
        mid += voice.outputBandState * voiceScale;
        side += voice.sideOutputBandState * voiceScale;

        ++voice.ageSamples;
        const bool elapsed = voice.ageSamples >= voice.maximumAgeSamples;
        const bool quiet = voice.ageSamples > voice.attackSamples
            && voiceActivitySquared(voice) < 1.0e-12f;
        if (elapsed || quiet) voice.active = false;
    }

    uint32_t countActiveVoices() const
    {
        uint32_t count = 0u;
        for (const Voice& voice : voices_) if (voice.active) ++count;
        return count;
    }

    void snapGlobalParameters()
    {
        smoothedWidth_ = params_.stereoWidth;
        smoothedOutputGain_ = outputGainTarget_;
    }

    void smoothGlobalParameters()
    {
        smoothedWidth_ += (params_.stereoWidth - smoothedWidth_)
            * smoothingCoefficient_;
        smoothedOutputGain_ += (outputGainTarget_ - smoothedOutputGain_)
            * smoothingCoefficient_;
        smoothedWidth_ = flushDenormal(smoothedWidth_);
        smoothedOutputGain_ = flushDenormal(smoothedOutputGain_);
    }

    double sampleRate_ = 48000.0;
    DrumBreakParams params_ {};
    std::array<Voice, kVoiceCount> voices_ {};
    DrumCharacter character_ {};
    uint64_t triggerCounter_ = 0u;
    uint32_t activeVoiceCount_ = 0u;
    float smoothingCoefficient_ = 0.004f;
    float outputGainTarget_ = 0.398f;
    float smoothedOutputGain_ = 0.398f;
    float smoothedWidth_ = 0.38f;
};

} // namespace s3g
