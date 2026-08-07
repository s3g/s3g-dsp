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

enum class DrumHiHatArticulation : uint8_t {
    Closed = 0u,
    Open = 1u,
    Pedal = 2u,
};

inline int drumHiHatCanonicalMidiNote(DrumHiHatArticulation articulation)
{
    switch (articulation) {
    case DrumHiHatArticulation::Open: return 46;
    case DrumHiHatArticulation::Pedal: return 44;
    case DrumHiHatArticulation::Closed:
    default: return 42;
    }
}

// The first sixteen members describe one synthesized hat voice. They are
// latched per hit; character, width, velocity response and output remain live.
struct DrumHiHatParams {
    float tuneHz = 1180.0f;
    float noteTracking = 0.35f;
    float alloy = 0.62f;
    float spread = 0.58f;
    float density = 0.72f;
    float tone = 0.62f;
    float air = 0.55f;
    float attack = 0.66f;
    float closedDecaySeconds = 0.18f;
    float openDecaySeconds = 1.35f;
    float pedalDecaySeconds = 0.30f;
    float wash = 0.58f;
    float chick = 0.52f;
    float chickTone = 0.56f;
    float sizzle = 0.28f;
    float chokeTimeMs = 7.0f;
    DrumCharacterParams character {};
    float stereoWidth = 0.18f;
    float velocitySensitivity = 0.90f;
    float outputGainDb = -8.0f;
};

inline DrumHiHatParams drumSanitizeHiHatParams(DrumHiHatParams params)
{
    params.tuneHz = clamp(drumFiniteOr(params.tuneHz, 1180.0f),
        320.0f, 3200.0f);
    params.noteTracking = clamp(
        drumFiniteOr(params.noteTracking, 0.35f), 0.0f, 1.0f);
    params.alloy = clamp(drumFiniteOr(params.alloy, 0.62f), 0.0f, 1.0f);
    params.spread = clamp(drumFiniteOr(params.spread, 0.58f), 0.0f, 1.0f);
    params.density = clamp(
        drumFiniteOr(params.density, 0.72f), 0.0f, 1.0f);
    params.tone = clamp(drumFiniteOr(params.tone, 0.62f), 0.0f, 1.0f);
    params.air = clamp(drumFiniteOr(params.air, 0.55f), 0.0f, 1.0f);
    params.attack = clamp(drumFiniteOr(params.attack, 0.66f), 0.0f, 1.0f);
    params.closedDecaySeconds = clamp(
        drumFiniteOr(params.closedDecaySeconds, 0.18f), 0.03f, 1.0f);
    params.openDecaySeconds = clamp(
        drumFiniteOr(params.openDecaySeconds, 1.35f), 0.12f, 6.0f);
    params.pedalDecaySeconds = clamp(
        drumFiniteOr(params.pedalDecaySeconds, 0.30f), 0.03f, 1.5f);
    params.wash = clamp(drumFiniteOr(params.wash, 0.58f), 0.0f, 1.0f);
    params.chick = clamp(drumFiniteOr(params.chick, 0.52f), 0.0f, 1.0f);
    params.chickTone = clamp(
        drumFiniteOr(params.chickTone, 0.56f), 0.0f, 1.0f);
    params.sizzle = clamp(drumFiniteOr(params.sizzle, 0.28f), 0.0f, 1.0f);
    params.chokeTimeMs = clamp(
        drumFiniteOr(params.chokeTimeMs, 7.0f), 0.3f, 80.0f);
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
        drumFiniteOr(params.stereoWidth, 0.18f), 0.0f, 1.0f);
    params.velocitySensitivity = clamp(
        drumFiniteOr(params.velocitySensitivity, 0.90f), 0.0f, 1.0f);
    params.outputGainDb = clamp(
        drumFiniteOr(params.outputGainDb, -8.0f), -36.0f, 12.0f);
    return params;
}

inline double drumHiHatTailSeconds(const DrumHiHatParams& source,
    DrumHiHatArticulation articulation, double sampleRate = 48000.0)
{
    (void)sampleRate;
    const DrumHiHatParams params = drumSanitizeHiHatParams(source);
    const double articulationDecay = articulation
            == DrumHiHatArticulation::Open
        ? params.openDecaySeconds
        : articulation == DrumHiHatArticulation::Pedal
            ? params.pedalDecaySeconds : params.closedDecaySeconds;
    const double modalDecay = articulationDecay
        * (0.86 + params.density * 0.30 + params.sizzle * 0.22);
    const double washDecay = articulationDecay
        * (0.72 + params.wash * 0.48 + params.sizzle * 0.22);
    const double chickDecay = params.chick > 1.0e-6f
        ? 0.025 + (0.120 - 0.025) * (1.0 - params.chickTone) : 0.0;
    return std::min(40.0, std::max({
        0.08,
        modalDecay * 2.15,
        params.wash > 1.0e-6f ? washDecay * 2.15 : 0.0,
        chickDecay * 2.15,
        params.character.drive > 1.0e-6f ? 0.50 : 0.0,
    }));
}

class DrumHiHat {
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

    void setParams(DrumHiHatParams params)
    {
        const bool wasInactive = activeVoiceCount_ == 0u
            && !character_.active();
        params_ = drumSanitizeHiHatParams(params);
        outputGainTarget_ = dbToGain(params_.outputGainDb);
        character_.setParams(params_.character);
        if (wasInactive) {
            snapGlobalParameters();
            character_.reset();
        }
    }

    DrumHiHatParams params() const { return params_; }

    void trigger(DrumHiHatArticulation articulation,
        float velocity = 1.0f, int midiNote = -1)
    {
        velocity = clamp(drumFiniteOr(velocity, 1.0f), 0.0f, 1.0f);
        if (midiNote < 0) midiNote = drumHiHatCanonicalMidiNote(articulation);
        midiNote = std::max(0, std::min(127, midiNote));
        if (lerp(1.0f, velocity,
                params_.velocitySensitivity) <= 1.0e-7f) {
            return;
        }

        if (articulation != DrumHiHatArticulation::Open) {
            chokeOpenVoices();
        }

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
        if (foundInactive) {
            ++activeVoiceCount_;
        } else {
            activeVoiceCount_ = countActiveVoices();
        }
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

    uint32_t activeVoiceCount() const { return activeVoiceCount_; }

private:
    static constexpr uint32_t kModeCount = 8u;

    struct Voice {
        bool active = false;
        bool choking = false;
        DrumHiHatArticulation articulation = DrumHiHatArticulation::Closed;
        float velocityGain = 1.0f;
        float velocityBrightness = 1.0f;
        float metalLevel = 0.62f;
        float washLevel = 0.58f;
        float chickLevel = 0.52f;
        float sizzleLevel = 0.28f;
        float contactPhase = 1.0f;
        float contactIncrement = 1.0f;
        float chokeGain = 1.0f;
        float chokeMultiplier = 1.0f;
        float washHighpassCoefficient = 0.04f;
        float washLowpassCoefficient = 0.4f;
        float chickHighpassCoefficient = 0.08f;
        float chickLowpassCoefficient = 0.4f;
        float washMidLow = 0.0f;
        float washMidBand = 0.0f;
        float washMidSmooth = 0.0f;
        float washSideLow = 0.0f;
        float washSideBand = 0.0f;
        float washSideSmooth = 0.0f;
        float chickLow = 0.0f;
        float chickBand = 0.0f;
        float chickSmooth = 0.0f;
        float chickPhase = 0.0f;
        float chickFrequencyHz = 4200.0f;
        std::array<DrumModalResonator, kModeCount> modes {};
        std::array<float, kModeCount> modeWeights {};
        std::array<float, kModeCount> modePans {};
        DrumExponentialEnvelope metalEnvelope {};
        DrumExponentialEnvelope washEnvelope {};
        DrumExponentialEnvelope chickEnvelope {};
        DrumRandom random {};
        uint32_t ageSamples = 0u;
        uint32_t maximumAgeSamples = 1u;
    };

    void chokeOpenVoices()
    {
        const float multiplier = drumDecayMultiplier(
            params_.chokeTimeMs * 0.001f,
            static_cast<float>(sampleRate_));
        for (Voice& voice : voices_) {
            if (!voice.active
                || voice.articulation != DrumHiHatArticulation::Open) {
                continue;
            }
            voice.choking = true;
            voice.chokeMultiplier = multiplier;
        }
    }

    void initialiseVoice(Voice& voice, uint32_t voiceIndex,
        uint64_t triggerIndex, DrumHiHatArticulation articulation,
        float velocity, int midiNote)
    {
        const float sr = static_cast<float>(sampleRate_);
        voice.active = true;
        voice.articulation = articulation;
        voice.velocityGain = lerp(1.0f, velocity,
            params_.velocitySensitivity);
        voice.velocityBrightness = lerp(1.0f,
            0.38f + velocity * 0.62f, params_.velocitySensitivity);
        voice.metalLevel = params_.alloy;
        voice.washLevel = params_.wash;
        voice.chickLevel = params_.chick;
        voice.sizzleLevel = params_.sizzle;
        voice.random.reset(drumMixedSeed(voiceIndex, triggerIndex,
            0x48415453u ^ static_cast<uint32_t>(articulation) * 0x9e3779b9u));

        const int canonicalNote = drumHiHatCanonicalMidiNote(articulation);
        const float trackedSemitones = static_cast<float>(
            midiNote - canonicalNote) * params_.noteTracking;
        const float baseFrequency = clamp(params_.tuneHz
                * std::exp2(trackedSemitones / 12.0f),
            180.0f, sr * 0.18f);
        const float articulationDecay = articulation
                == DrumHiHatArticulation::Open
            ? params_.openDecaySeconds
            : articulation == DrumHiHatArticulation::Pedal
                ? params_.pedalDecaySeconds : params_.closedDecaySeconds;
        const float articulationLevel = articulation
                == DrumHiHatArticulation::Open ? 0.84f
            : articulation == DrumHiHatArticulation::Pedal ? 0.72f : 1.0f;

        constexpr std::array<float, kModeCount> compactRatios {{
            1.00f, 1.46f, 1.91f, 2.43f,
            3.02f, 3.72f, 4.54f, 5.48f,
        }};
        constexpr std::array<float, kModeCount> scatteredRatios {{
            1.00f, 1.31f, 1.79f, 2.57f,
            3.43f, 4.61f, 6.02f, 7.83f,
        }};
        for (uint32_t mode = 0u; mode < kModeCount; ++mode) {
            const float normalized = static_cast<float>(mode)
                / static_cast<float>(kModeCount - 1u);
            float ratio = lerp(compactRatios[mode], scatteredRatios[mode],
                params_.spread);
            const float alloySkew = (mode & 1u) != 0u ? 1.0f : -1.0f;
            ratio *= 1.0f + alloySkew * params_.alloy
                * normalized * 0.045f;
            const float decayScale = lerp(1.18f, 0.62f, normalized)
                * lerp(0.88f, 1.20f,
                    params_.sizzle * normalized);
            voice.modes[mode].configure(
                std::min(sr * 0.45f, baseFrequency * ratio),
                std::max(0.012f, articulationDecay * decayScale), sr);
            const float densityWeight = mode < 2u ? 1.0f
                : lerp(0.12f, 1.0f, std::pow(params_.density,
                    0.55f + normalized * 0.70f));
            voice.modeWeights[mode] = articulationLevel
                * densityWeight * lerp(0.42f, 0.18f, normalized);
            voice.modePans[mode] = voice.random.bipolar()
                * lerp(0.08f, 0.72f, normalized);
            const float polarity = mode == 0u ? 1.0f
                : (voice.random.bipolar() >= 0.0f ? 1.0f : -1.0f);
            const float strike = polarity
                * (0.74f + voice.random.unipolar() * 0.24f);
            // A quadrature strike and whole-voice contact ramp guarantee that
            // the dense mode bank never appears as a sample-zero edge.
            voice.modes[mode].strike(0.0f, -strike);
        }

        const float modalDecay = articulationDecay
            * lerp(0.86f, 1.16f, params_.density)
            * lerp(0.94f, 1.18f, params_.sizzle);
        voice.metalEnvelope.configure(modalDecay, sr);
        voice.metalEnvelope.trigger();
        const float washDecay = articulationDecay
            * lerp(0.72f, 1.20f, params_.wash)
            * lerp(0.96f, 1.22f, params_.sizzle);
        voice.washEnvelope.configure(washDecay, sr);
        voice.washEnvelope.trigger();
        const float chickSeconds = lerp(0.120f, 0.025f,
            params_.chickTone);
        voice.chickEnvelope.configure(chickSeconds, sr);
        voice.chickEnvelope.trigger();

        const float hardness = params_.attack * voice.velocityBrightness;
        const float contactSeconds = articulation
                == DrumHiHatArticulation::Pedal
            ? lerp(0.020f, 0.0040f, hardness)
            : articulation == DrumHiHatArticulation::Open
                ? lerp(0.0060f, 0.0012f, hardness)
                : lerp(0.0035f, 0.0007f, hardness);
        voice.contactPhase = 0.0f;
        voice.contactIncrement = 1.0f
            / std::max(1.0f, contactSeconds * sr);

        const float washHighpassHz = lerp(1500.0f, 6800.0f,
                params_.tone)
            * lerp(0.82f, 1.10f, voice.velocityBrightness);
        const float washLowpassHz = lerp(6800.0f, 20500.0f,
                params_.air)
            * lerp(0.78f, 1.08f, voice.velocityBrightness);
        voice.washHighpassCoefficient = drumOnePoleFrequencyCoefficient(
            washHighpassHz, sr);
        voice.washLowpassCoefficient = drumOnePoleFrequencyCoefficient(
            std::max(washHighpassHz * 1.25f, washLowpassHz), sr);

        const float chickHighpassHz = lerp(850.0f, 5200.0f,
            params_.chickTone);
        const float chickLowpassHz = lerp(4200.0f, 18000.0f,
            params_.chickTone);
        voice.chickHighpassCoefficient = drumOnePoleFrequencyCoefficient(
            chickHighpassHz, sr);
        voice.chickLowpassCoefficient = drumOnePoleFrequencyCoefficient(
            std::max(chickHighpassHz * 1.30f, chickLowpassHz), sr);
        voice.chickFrequencyHz = std::min(sr * 0.42f,
            lerp(1700.0f, 7600.0f, params_.chickTone));

        voice.maximumAgeSamples = drumLongestTailSamples(sampleRate_, {
            drumHiHatTailSeconds(params_, articulation, sampleRate_),
        }, 0.08, 40.0);
    }

    static float voiceActivitySquared(const Voice& voice)
    {
        float modal = 0.0f;
        for (uint32_t mode = 0u; mode < kModeCount; ++mode) {
            modal += voice.modes[mode].magnitudeSquared()
                * voice.modeWeights[mode] * voice.modeWeights[mode];
        }
        const float metal = voice.metalEnvelope.value()
            * voice.velocityGain * (0.18f + voice.metalLevel * 0.48f);
        const float wash = voice.washEnvelope.value()
            * voice.velocityGain * voice.velocityBrightness
            * voice.washLevel * 0.52f;
        const float chick = voice.chickEnvelope.value()
            * voice.velocityGain * voice.velocityBrightness
            * voice.chickLevel * 0.40f;
        return (modal + metal * metal + wash * wash + chick * chick)
            * voice.chokeGain * voice.chokeGain;
    }

    void processVoice(Voice& voice, float& mid, float& side)
    {
        const float noiseMid = voice.random.bipolar();
        const float noiseSide = (voice.random.bipolar()
            - voice.random.bipolar()) * 0.5f;
        const float noiseChick = voice.random.bipolar();

        voice.washMidLow += (noiseMid - voice.washMidLow)
            * voice.washHighpassCoefficient;
        const float washMidHigh = noiseMid - voice.washMidLow;
        voice.washMidBand += (washMidHigh - voice.washMidBand)
            * voice.washLowpassCoefficient;
        voice.washMidSmooth += (voice.washMidBand
                - voice.washMidSmooth)
            * voice.washLowpassCoefficient;
        voice.washSideLow += (noiseSide - voice.washSideLow)
            * voice.washHighpassCoefficient;
        const float washSideHigh = noiseSide - voice.washSideLow;
        voice.washSideBand += (washSideHigh - voice.washSideBand)
            * voice.washLowpassCoefficient;
        voice.washSideSmooth += (voice.washSideBand
                - voice.washSideSmooth)
            * voice.washLowpassCoefficient;

        const float brightWashMid = lerp(voice.washMidSmooth,
            voice.washMidBand, voice.sizzleLevel * 0.58f);
        const float brightWashSide = lerp(voice.washSideSmooth,
            voice.washSideBand, voice.sizzleLevel * 0.58f);
        const float modalExcitation = brightWashMid
            * voice.washEnvelope.value() * voice.washLevel * 0.0035f;

        float metalMid = 0.0f;
        float metalSide = 0.0f;
        for (uint32_t mode = 0u; mode < kModeCount; ++mode) {
            const float value = voice.modes[mode].process(
                    modalExcitation * voice.modeWeights[mode])
                * voice.modeWeights[mode];
            metalMid += value;
            metalSide += value * voice.modePans[mode];
        }
        const float metalEnvelope = voice.metalEnvelope.process();
        const float metalGain = voice.velocityGain
            * (0.12f + voice.metalLevel * 0.22f);
        metalMid *= metalEnvelope * metalGain;
        metalSide *= metalEnvelope * metalGain * 0.54f;

        const float washEnvelope = voice.washEnvelope.process();
        const float articulationWash = voice.articulation
                == DrumHiHatArticulation::Open ? 1.0f
            : voice.articulation == DrumHiHatArticulation::Pedal
                ? 0.34f : 0.62f;
        const float washGain = washEnvelope * voice.washLevel
            * voice.velocityGain * voice.velocityBrightness
            * articulationWash * 0.52f;
        const float washMid = brightWashMid * washGain;
        const float washSide = brightWashSide * washGain * 0.76f;

        voice.chickLow += (noiseChick - voice.chickLow)
            * voice.chickHighpassCoefficient;
        const float chickHigh = noiseChick - voice.chickLow;
        voice.chickBand += (chickHigh - voice.chickBand)
            * voice.chickLowpassCoefficient;
        voice.chickSmooth += (voice.chickBand - voice.chickSmooth)
            * voice.chickLowpassCoefficient;
        voice.chickPhase += voice.chickFrequencyHz
            / static_cast<float>(sampleRate_);
        voice.chickPhase -= std::floor(voice.chickPhase);
        const float chickOscillator = std::sin(
            2.0f * kPi * voice.chickPhase);
        const float chickEnvelope = voice.chickEnvelope.process();
        const float articulationChick = voice.articulation
                == DrumHiHatArticulation::Pedal ? 1.0f
            : voice.articulation == DrumHiHatArticulation::Closed
                ? 0.30f : 0.08f;
        const float chick = (voice.chickSmooth * 0.72f
                + chickOscillator * 0.28f)
            * chickEnvelope * voice.chickLevel * articulationChick
            * voice.velocityGain * voice.velocityBrightness * 0.58f;

        float contactGain = 1.0f;
        if (voice.contactPhase < 1.0f) {
            const float phase = voice.contactPhase;
            contactGain = phase * phase * (3.0f - 2.0f * phase);
            voice.contactPhase = std::min(
                1.0f, phase + voice.contactIncrement);
        }
        const float outputGain = contactGain * voice.chokeGain;
        mid += (metalMid + washMid + chick) * outputGain;
        side += (metalSide + washSide) * outputGain;

        if (voice.choking) {
            voice.chokeGain = flushDenormal(
                voice.chokeGain * voice.chokeMultiplier);
        }
        ++voice.ageSamples;
        if (voiceActivitySquared(voice) < 1.0e-12f
            || voice.ageSamples >= voice.maximumAgeSamples) {
            voice.active = false;
        }
    }

    void snapGlobalParameters()
    {
        outputGainTarget_ = dbToGain(params_.outputGainDb);
        smoothedOutputGain_ = outputGainTarget_;
        smoothedWidth_ = params_.stereoWidth;
    }

    void smoothGlobalParameters()
    {
        smoothedOutputGain_ = flushDenormal(smoothedOutputGain_
            + (outputGainTarget_ - smoothedOutputGain_)
                * smoothingCoefficient_);
        if (params_.stereoWidth == 0.0f) {
            smoothedWidth_ = 0.0f;
        } else {
            smoothedWidth_ = flushDenormal(smoothedWidth_
                + (params_.stereoWidth - smoothedWidth_)
                    * smoothingCoefficient_);
        }
    }

    uint32_t countActiveVoices() const
    {
        uint32_t count = 0u;
        for (const Voice& voice : voices_) {
            if (voice.active) ++count;
        }
        return count;
    }

    double sampleRate_ = 48000.0;
    DrumHiHatParams params_ {};
    DrumCharacter character_ {};
    std::array<Voice, kVoiceCount> voices_ {};
    uint64_t triggerCounter_ = 0u;
    uint32_t activeVoiceCount_ = 0u;
    float smoothingCoefficient_ = 0.004157998f;
    float outputGainTarget_ = 0.3981072f;
    float smoothedOutputGain_ = 0.3981072f;
    float smoothedWidth_ = 0.18f;
};

} // namespace s3g
