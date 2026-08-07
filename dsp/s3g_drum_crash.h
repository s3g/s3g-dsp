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

enum class DrumCrashArticulation : uint8_t {
    Crash = 0u,
    Choke = 1u,
    Bell = 2u,
};

inline int drumCrashCanonicalMidiNote(DrumCrashArticulation articulation)
{
    switch (articulation) {
    case DrumCrashArticulation::Choke: return 50;
    case DrumCrashArticulation::Bell: return 53;
    case DrumCrashArticulation::Crash:
    default: return 49;
    }
}

// The first sixteen members define one synthesized cymbal voice and are
// latched per hit. Character, width, velocity response and output remain live.
struct DrumCrashParams {
    float tuneHz = 720.0f;
    float noteTracking = 0.25f;
    float size = 0.62f;
    float alloy = 0.58f;
    float spread = 0.66f;
    float density = 12.0f;
    float brightness = 0.68f;
    float attack = 0.72f;
    float decaySeconds = 1.30f;
    float damping = 0.44f;
    float wash = 0.74f;
    float washDecaySeconds = 1.45f;
    float bell = 0.20f;
    float bellTuneHz = 1680.0f;
    float chokeTimeMs = 14.0f;
    float texture = 0.58f;
    DrumCharacterParams character {};
    float stereoWidth = 0.68f;
    float velocitySensitivity = 0.90f;
    float outputGainDb = -11.0f;
};

inline DrumCrashParams drumSanitizeCrashParams(DrumCrashParams params)
{
    params.tuneHz = clamp(drumFiniteOr(params.tuneHz, 720.0f),
        180.0f, 2400.0f);
    params.noteTracking = clamp(
        drumFiniteOr(params.noteTracking, 0.25f), 0.0f, 1.0f);
    params.size = clamp(drumFiniteOr(params.size, 0.62f), 0.0f, 1.0f);
    params.alloy = clamp(drumFiniteOr(params.alloy, 0.58f), 0.0f, 1.0f);
    params.spread = clamp(drumFiniteOr(params.spread, 0.66f), 0.0f, 1.0f);
    params.density = clamp(drumFiniteOr(params.density, 12.0f), 4.0f, 16.0f);
    params.brightness = clamp(
        drumFiniteOr(params.brightness, 0.68f), 0.0f, 1.0f);
    params.attack = clamp(drumFiniteOr(params.attack, 0.72f), 0.0f, 1.0f);
    params.decaySeconds = clamp(
        drumFiniteOr(params.decaySeconds, 1.30f), 0.08f, 8.0f);
    params.damping = clamp(drumFiniteOr(params.damping, 0.44f), 0.0f, 1.0f);
    params.wash = clamp(drumFiniteOr(params.wash, 0.74f), 0.0f, 1.0f);
    params.washDecaySeconds = clamp(
        drumFiniteOr(params.washDecaySeconds, 1.45f), 0.08f, 8.0f);
    params.bell = clamp(drumFiniteOr(params.bell, 0.20f), 0.0f, 1.0f);
    params.bellTuneHz = clamp(
        drumFiniteOr(params.bellTuneHz, 1680.0f), 350.0f, 6000.0f);
    params.chokeTimeMs = clamp(
        drumFiniteOr(params.chokeTimeMs, 14.0f), 0.5f, 150.0f);
    params.texture = clamp(drumFiniteOr(params.texture, 0.58f), 0.0f, 1.0f);
    params.character.drive = clamp(
        drumFiniteOr(params.character.drive, 0.0f), 0.0f, 1.0f);
    params.character.bias = clamp(
        drumFiniteOr(params.character.bias, 0.0f), -1.0f, 1.0f);
    params.character.compression = clamp(
        drumFiniteOr(params.character.compression, 0.0f), 0.0f, 1.0f);
    params.character.sampleRateReduction = clamp(
        drumFiniteOr(params.character.sampleRateReduction, 0.0f), 0.0f, 1.0f);
    params.character.bitDepthReduction = clamp(
        drumFiniteOr(params.character.bitDepthReduction, 0.0f), 0.0f, 1.0f);
    params.character.reconstruction = clamp(
        drumFiniteOr(params.character.reconstruction, 0.0f), 0.0f, 1.0f);
    params.character.tone = clamp(
        drumFiniteOr(params.character.tone, 0.0f), -1.0f, 1.0f);
    params.stereoWidth = clamp(
        drumFiniteOr(params.stereoWidth, 0.68f), 0.0f, 1.0f);
    params.velocitySensitivity = clamp(
        drumFiniteOr(params.velocitySensitivity, 0.90f), 0.0f, 1.0f);
    params.outputGainDb = clamp(
        drumFiniteOr(params.outputGainDb, -11.0f), -36.0f, 12.0f);
    return params;
}

inline double drumCrashTailSeconds(const DrumCrashParams& source,
    DrumCrashArticulation articulation, double sampleRate = 48000.0)
{
    (void)sampleRate;
    const DrumCrashParams params = drumSanitizeCrashParams(source);
    const double articulationScale = articulation == DrumCrashArticulation::Choke
        ? 0.055 : articulation == DrumCrashArticulation::Bell ? 0.52 : 1.0;
    const double sizeScale = 0.78 + params.size * 0.48;
    const double modalTail = params.decaySeconds * articulationScale
        * sizeScale * 2.15;
    const double washTail = params.wash > 1.0e-6f
        ? params.washDecaySeconds * articulationScale
            * (0.82 + params.texture * 0.34) * 2.15 : 0.0;
    const double bellTail = params.bell > 1.0e-6f
            || articulation == DrumCrashArticulation::Bell
        ? params.decaySeconds * articulationScale * 1.35 * 2.15 : 0.0;
    return std::min(40.0, std::max({
        0.08, modalTail, washTail, bellTail,
        params.character.drive > 1.0e-6f ? 0.50 : 0.0,
    }));
}

class DrumCrash {
public:
    static constexpr uint32_t kVoiceCount = 20u;

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

    void setParams(DrumCrashParams params)
    {
        const bool wasInactive = activeVoiceCount_ == 0u
            && !character_.active();
        params_ = drumSanitizeCrashParams(params);
        outputGainTarget_ = dbToGain(params_.outputGainDb);
        character_.setParams(params_.character);
        if (wasInactive) {
            snapGlobalParameters();
            character_.reset();
        }
    }

    DrumCrashParams params() const { return params_; }

    void trigger(DrumCrashArticulation articulation,
        float velocity = 1.0f, int midiNote = -1)
    {
        velocity = clamp(drumFiniteOr(velocity, 1.0f), 0.0f, 1.0f);
        if (midiNote < 0) midiNote = drumCrashCanonicalMidiNote(articulation);
        midiNote = std::max(0, std::min(127, midiNote));
        if (lerp(1.0f, velocity,
                params_.velocitySensitivity) <= 1.0e-7f) {
            return;
        }
        if (articulation == DrumCrashArticulation::Choke) {
            chokeVoices();
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

    uint32_t activeVoiceCount() const { return activeVoiceCount_; }

private:
    static constexpr uint32_t kModeCount = 16u;

    struct Voice {
        bool active = false;
        bool choking = false;
        DrumCrashArticulation articulation = DrumCrashArticulation::Crash;
        float velocityGain = 1.0f;
        float velocityBrightness = 1.0f;
        float washLevel = 0.74f;
        float bellLevel = 0.20f;
        float texture = 0.58f;
        float contactPhase = 0.0f;
        float contactIncrement = 1.0f;
        float chokeGain = 1.0f;
        float chokeMultiplier = 1.0f;
        float washHighpassCoefficient = 0.04f;
        float washLowpassCoefficient = 0.4f;
        float washMidLow = 0.0f;
        float washMidBand = 0.0f;
        float washMidSmooth = 0.0f;
        float washSideLow = 0.0f;
        float washSideBand = 0.0f;
        float washSideSmooth = 0.0f;
        float bellPhase = 0.0f;
        float bellPhaseTwo = 0.0f;
        float bellFrequencyHz = 1680.0f;
        uint32_t modeCount = 12u;
        std::array<DrumModalResonator, kModeCount> modes {};
        std::array<float, kModeCount> modeWeights {};
        std::array<float, kModeCount> modePans {};
        DrumExponentialEnvelope metalEnvelope {};
        DrumExponentialEnvelope washEnvelope {};
        DrumExponentialEnvelope bellEnvelope {};
        DrumRandom random {};
        uint32_t ageSamples = 0u;
        uint32_t maximumAgeSamples = 1u;
    };

    void chokeVoices()
    {
        const float multiplier = drumDecayMultiplier(
            params_.chokeTimeMs * 0.001f,
            static_cast<float>(sampleRate_));
        for (Voice& voice : voices_) {
            if (!voice.active) continue;
            voice.choking = true;
            voice.chokeMultiplier = multiplier;
        }
    }

    void initialiseVoice(Voice& voice, uint32_t voiceIndex,
        uint64_t triggerIndex, DrumCrashArticulation articulation,
        float velocity, int midiNote)
    {
        const float sr = static_cast<float>(sampleRate_);
        voice.active = true;
        voice.articulation = articulation;
        voice.velocityGain = lerp(1.0f, velocity,
            params_.velocitySensitivity);
        voice.velocityBrightness = lerp(1.0f,
            0.34f + velocity * 0.66f, params_.velocitySensitivity);
        voice.washLevel = params_.wash;
        voice.bellLevel = params_.bell;
        voice.texture = params_.texture;
        voice.random.reset(drumMixedSeed(voiceIndex, triggerIndex,
            0x43524153u ^ static_cast<uint32_t>(articulation) * 0x9e3779b9u));

        const int canonical = drumCrashCanonicalMidiNote(articulation);
        const float trackedSemitones = static_cast<float>(midiNote - canonical)
            * params_.noteTracking;
        const float trackingRatio = std::exp2(trackedSemitones / 12.0f);
        const float sizePitch = lerp(1.32f, 0.76f, params_.size);
        const float baseFrequency = clamp(params_.tuneHz * trackingRatio
                * sizePitch, 120.0f, sr * 0.10f);
        const float articulationScale = articulation
                == DrumCrashArticulation::Choke ? 0.055f
            : articulation == DrumCrashArticulation::Bell ? 0.52f : 1.0f;
        const float sizeDecay = lerp(0.78f, 1.26f, params_.size);
        voice.modeCount = articulation == DrumCrashArticulation::Bell
            ? std::min(8u, static_cast<uint32_t>(std::lround(params_.density)))
            : articulation == DrumCrashArticulation::Choke ? 5u
            : static_cast<uint32_t>(std::lround(params_.density));
        voice.modeCount = std::max(4u, std::min(kModeCount, voice.modeCount));

        constexpr std::array<float, kModeCount> compactRatios {{
            1.00f, 1.34f, 1.71f, 2.10f,
            2.54f, 3.03f, 3.57f, 4.16f,
            4.81f, 5.53f, 6.31f, 7.16f,
            8.08f, 9.08f, 10.16f, 11.32f,
        }};
        constexpr std::array<float, kModeCount> scatteredRatios {{
            1.00f, 1.29f, 1.83f, 2.47f,
            3.19f, 4.07f, 5.12f, 6.39f,
            7.91f, 9.69f, 11.73f, 14.06f,
            16.71f, 19.69f, 23.04f, 26.77f,
        }};
        for (uint32_t mode = 0u; mode < kModeCount; ++mode) {
            const float normalized = static_cast<float>(mode)
                / static_cast<float>(kModeCount - 1u);
            float ratio = lerp(compactRatios[mode], scatteredRatios[mode],
                params_.spread);
            const float skew = (mode & 1u) != 0u ? 1.0f : -1.0f;
            ratio *= 1.0f + skew * params_.alloy
                * normalized * 0.055f;
            const float upperDamping = lerp(1.18f, 0.28f,
                normalized * lerp(0.36f, 1.0f, params_.damping));
            const float modeDecay = std::max(0.018f,
                params_.decaySeconds * articulationScale
                    * sizeDecay * upperDamping);
            voice.modes[mode].configure(
                std::min(sr * 0.44f, baseFrequency * ratio), modeDecay, sr);
            const bool enabled = mode < voice.modeCount;
            const float bellContour = articulation
                    == DrumCrashArticulation::Bell
                ? lerp(1.0f, 0.22f, normalized) : 1.0f;
            voice.modeWeights[mode] = enabled
                ? lerp(0.25f, 0.070f, normalized)
                    * lerp(0.65f, 1.26f,
                        params_.brightness * normalized)
                    * bellContour : 0.0f;
            voice.modePans[mode] = voice.random.bipolar()
                * lerp(0.08f, 0.96f, normalized);
            const float polarity = mode == 0u ? 1.0f
                : (voice.random.bipolar() >= 0.0f ? 1.0f : -1.0f);
            voice.modes[mode].strike(0.0f,
                -polarity * (0.62f + voice.random.unipolar() * 0.30f));
        }

        voice.metalEnvelope.configure(params_.decaySeconds
            * articulationScale * sizeDecay, sr);
        voice.washEnvelope.configure(params_.washDecaySeconds
            * articulationScale * lerp(0.82f, 1.16f, params_.texture), sr);
        voice.bellEnvelope.configure(params_.decaySeconds
            * articulationScale * 1.18f, sr);
        voice.metalEnvelope.trigger();
        voice.washEnvelope.trigger();
        voice.bellEnvelope.trigger();
        voice.bellFrequencyHz = std::min(sr * 0.38f,
            params_.bellTuneHz * trackingRatio
                * (articulation == DrumCrashArticulation::Bell ? 1.0f : 0.82f));

        const float hardness = params_.attack * voice.velocityBrightness;
        const float contactSeconds = articulation
                == DrumCrashArticulation::Choke
            ? lerp(0.010f, 0.0010f, hardness)
            : articulation == DrumCrashArticulation::Bell
                ? lerp(0.0040f, 0.00045f, hardness)
                : lerp(0.014f, 0.0012f, hardness);
        voice.contactIncrement = 1.0f
            / std::max(1.0f, contactSeconds * sr);

        const float highpassHz = lerp(750.0f, 6200.0f,
                params_.brightness)
            * lerp(0.76f, 1.12f, voice.velocityBrightness);
        const float lowpassHz = lerp(6500.0f, 22000.0f,
                params_.brightness)
            * lerp(0.82f, 1.08f, voice.velocityBrightness);
        voice.washHighpassCoefficient = drumOnePoleFrequencyCoefficient(
            highpassHz, sr);
        voice.washLowpassCoefficient = drumOnePoleFrequencyCoefficient(
            std::max(highpassHz * 1.3f, lowpassHz), sr);
        voice.maximumAgeSamples = drumLongestTailSamples(sampleRate_, {
            drumCrashTailSeconds(params_, articulation, sampleRate_),
        }, 0.08, 40.0);
    }

    static float voiceActivitySquared(const Voice& voice)
    {
        float modal = 0.0f;
        for (uint32_t mode = 0u; mode < kModeCount; ++mode) {
            modal += voice.modes[mode].magnitudeSquared()
                * voice.modeWeights[mode] * voice.modeWeights[mode];
        }
        const float metal = voice.metalEnvelope.value() * 0.32f;
        const float wash = voice.washEnvelope.value()
            * voice.washLevel * 0.52f;
        const float bell = voice.bellEnvelope.value()
            * voice.bellLevel * 0.38f;
        return (modal + metal * metal + wash * wash + bell * bell)
            * voice.velocityGain * voice.velocityGain
            * voice.chokeGain * voice.chokeGain;
    }

    void processVoice(Voice& voice, float& mid, float& side)
    {
        const float noiseMid = voice.random.bipolar();
        const float noiseSide = (voice.random.bipolar()
            - voice.random.bipolar()) * 0.5f;
        voice.washMidLow += (noiseMid - voice.washMidLow)
            * voice.washHighpassCoefficient;
        const float midHigh = noiseMid - voice.washMidLow;
        voice.washMidBand += (midHigh - voice.washMidBand)
            * voice.washLowpassCoefficient;
        voice.washMidSmooth += (voice.washMidBand - voice.washMidSmooth)
            * voice.washLowpassCoefficient;
        voice.washSideLow += (noiseSide - voice.washSideLow)
            * voice.washHighpassCoefficient;
        const float sideHigh = noiseSide - voice.washSideLow;
        voice.washSideBand += (sideHigh - voice.washSideBand)
            * voice.washLowpassCoefficient;
        voice.washSideSmooth += (voice.washSideBand - voice.washSideSmooth)
            * voice.washLowpassCoefficient;
        const float brightMid = lerp(voice.washMidSmooth,
            voice.washMidBand, voice.texture * 0.72f);
        const float brightSide = lerp(voice.washSideSmooth,
            voice.washSideBand, voice.texture * 0.72f);

        const float modalExcitation = brightMid
            * voice.washEnvelope.value() * voice.washLevel * 0.0045f;
        float modalMid = 0.0f;
        float modalSide = 0.0f;
        for (uint32_t mode = 0u; mode < kModeCount; ++mode) {
            const float value = voice.modes[mode].process(
                    modalExcitation * voice.modeWeights[mode])
                * voice.modeWeights[mode];
            modalMid += value;
            modalSide += value * voice.modePans[mode];
        }
        const float metalEnvelope = voice.metalEnvelope.process();
        const float articulationMetal = voice.articulation
                == DrumCrashArticulation::Bell ? 0.42f
            : voice.articulation == DrumCrashArticulation::Choke ? 0.48f : 1.0f;
        modalMid *= metalEnvelope * voice.velocityGain
            * articulationMetal * 0.42f;
        modalSide *= metalEnvelope * voice.velocityGain
            * articulationMetal * 0.34f;

        const float washEnvelope = voice.washEnvelope.process();
        const float articulationWash = voice.articulation
                == DrumCrashArticulation::Bell ? 0.12f
            : voice.articulation == DrumCrashArticulation::Choke ? 0.22f : 1.0f;
        const float washGain = washEnvelope * voice.washLevel
            * voice.velocityGain * voice.velocityBrightness
            * articulationWash * 0.62f;
        const float washMid = brightMid * washGain;
        const float washSide = brightSide * washGain * 0.88f;

        voice.bellPhase += voice.bellFrequencyHz
            / static_cast<float>(sampleRate_);
        voice.bellPhase -= std::floor(voice.bellPhase);
        voice.bellPhaseTwo += voice.bellFrequencyHz * 1.47f
            / static_cast<float>(sampleRate_);
        voice.bellPhaseTwo -= std::floor(voice.bellPhaseTwo);
        const float bellOscillator = std::sin(2.0f * kPi * voice.bellPhase)
            + std::sin(2.0f * kPi * voice.bellPhaseTwo) * 0.42f;
        const float articulationBell = voice.articulation
                == DrumCrashArticulation::Bell ? 1.0f
            : voice.articulation == DrumCrashArticulation::Choke ? 0.14f : 0.34f;
        const float bell = bellOscillator * voice.bellEnvelope.process()
            * lerp(voice.bellLevel, 1.0f,
                voice.articulation == DrumCrashArticulation::Bell ? 0.72f : 0.0f)
            * articulationBell * voice.velocityGain * 0.24f;

        float contactGain = 1.0f;
        if (voice.contactPhase < 1.0f) {
            const float phase = voice.contactPhase;
            contactGain = phase * phase * (3.0f - 2.0f * phase);
            voice.contactPhase = std::min(
                1.0f, phase + voice.contactIncrement);
        }
        const float gain = contactGain * voice.chokeGain;
        mid += (modalMid + washMid + bell) * gain;
        side += (modalSide + washSide) * gain;

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
        if (params_.stereoWidth == 0.0f) smoothedWidth_ = 0.0f;
        else smoothedWidth_ = flushDenormal(smoothedWidth_
            + (params_.stereoWidth - smoothedWidth_)
                * smoothingCoefficient_);
    }

    uint32_t countActiveVoices() const
    {
        uint32_t count = 0u;
        for (const Voice& voice : voices_) if (voice.active) ++count;
        return count;
    }

    double sampleRate_ = 48000.0;
    DrumCrashParams params_ {};
    DrumCharacter character_ {};
    std::array<Voice, kVoiceCount> voices_ {};
    uint64_t triggerCounter_ = 0u;
    uint32_t activeVoiceCount_ = 0u;
    float smoothingCoefficient_ = 0.004157998f;
    float outputGainTarget_ = 0.2818383f;
    float smoothedOutputGain_ = 0.2818383f;
    float smoothedWidth_ = 0.68f;
};

} // namespace s3g
