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

enum class DrumCowbellArticulation : uint8_t {
    Cowbell = 0u,
    Muted = 1u,
    High = 2u,
};

inline int drumCowbellCanonicalMidiNote(DrumCowbellArticulation articulation)
{
    switch (articulation) {
    case DrumCowbellArticulation::Muted: return 57;
    case DrumCowbellArticulation::High: return 58;
    case DrumCowbellArticulation::Cowbell:
    default: return 56;
    }
}

// The first sixteen members define a synthesized cowbell voice and are
// latched per hit. Character, width, velocity response and output remain live.
struct DrumCowbellParams {
    float tuneHz = 560.0f;
    float noteTracking = 0.65f;
    float intervalRatio = 1.52f;
    float detune = 0.08f;
    float shape = 0.68f;
    float attack = 0.62f;
    float decaySeconds = 0.24f;
    float damping = 0.46f;
    float body = 0.62f;
    float bodyDecaySeconds = 0.18f;
    float brightness = 0.58f;
    float noise = 0.16f;
    float noiseDecaySeconds = 0.035f;
    float bendSemitones = 0.0f;
    float bendDecaySeconds = 0.030f;
    float strikeTone = 0.56f;
    DrumCharacterParams character {};
    float stereoWidth = 0.18f;
    float velocitySensitivity = 0.90f;
    float outputGainDb = -8.0f;
};

inline DrumCowbellParams drumSanitizeCowbellParams(DrumCowbellParams params)
{
    params.tuneHz = clamp(drumFiniteOr(params.tuneHz, 560.0f),
        180.0f, 2400.0f);
    params.noteTracking = clamp(
        drumFiniteOr(params.noteTracking, 0.65f), 0.0f, 1.0f);
    params.intervalRatio = clamp(
        drumFiniteOr(params.intervalRatio, 1.52f), 1.08f, 2.60f);
    params.detune = clamp(drumFiniteOr(params.detune, 0.08f), 0.0f, 1.0f);
    params.shape = clamp(drumFiniteOr(params.shape, 0.68f), 0.0f, 1.0f);
    params.attack = clamp(drumFiniteOr(params.attack, 0.62f), 0.0f, 1.0f);
    params.decaySeconds = clamp(
        drumFiniteOr(params.decaySeconds, 0.24f), 0.025f, 2.0f);
    params.damping = clamp(drumFiniteOr(params.damping, 0.46f), 0.0f, 1.0f);
    params.body = clamp(drumFiniteOr(params.body, 0.62f), 0.0f, 1.0f);
    params.bodyDecaySeconds = clamp(
        drumFiniteOr(params.bodyDecaySeconds, 0.18f), 0.02f, 1.5f);
    params.brightness = clamp(
        drumFiniteOr(params.brightness, 0.58f), 0.0f, 1.0f);
    params.noise = clamp(drumFiniteOr(params.noise, 0.16f), 0.0f, 1.0f);
    params.noiseDecaySeconds = clamp(
        drumFiniteOr(params.noiseDecaySeconds, 0.035f), 0.004f, 0.40f);
    params.bendSemitones = clamp(
        drumFiniteOr(params.bendSemitones, 0.0f), -24.0f, 24.0f);
    params.bendDecaySeconds = clamp(
        drumFiniteOr(params.bendDecaySeconds, 0.030f), 0.003f, 0.50f);
    params.strikeTone = clamp(
        drumFiniteOr(params.strikeTone, 0.56f), 0.0f, 1.0f);
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
        drumFiniteOr(params.stereoWidth, 0.18f), 0.0f, 1.0f);
    params.velocitySensitivity = clamp(
        drumFiniteOr(params.velocitySensitivity, 0.90f), 0.0f, 1.0f);
    params.outputGainDb = clamp(
        drumFiniteOr(params.outputGainDb, -8.0f), -36.0f, 12.0f);
    return params;
}

inline double drumCowbellTailSeconds(const DrumCowbellParams& source,
    DrumCowbellArticulation articulation, double sampleRate = 48000.0)
{
    (void)sampleRate;
    const DrumCowbellParams params = drumSanitizeCowbellParams(source);
    const double scale = articulation == DrumCowbellArticulation::Muted
        ? 0.30 : articulation == DrumCowbellArticulation::High ? 0.76 : 1.0;
    const double toneTail = params.decaySeconds * scale * 2.15;
    const double bodyTail = params.body > 1.0e-6f
        ? params.bodyDecaySeconds * scale * 2.15 : 0.0;
    const double noiseTail = params.noise > 1.0e-6f
        ? params.noiseDecaySeconds * scale * 2.15 : 0.0;
    return std::min(40.0, std::max({
        0.05, toneTail, bodyTail, noiseTail,
        params.character.drive > 1.0e-6f ? 0.50 : 0.0,
    }));
}

class DrumCowbell {
public:
    static constexpr uint32_t kVoiceCount = 16u;

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

    void setParams(DrumCowbellParams params)
    {
        const bool wasInactive = activeVoiceCount_ == 0u
            && !character_.active();
        params_ = drumSanitizeCowbellParams(params);
        outputGainTarget_ = dbToGain(params_.outputGainDb);
        character_.setParams(params_.character);
        if (wasInactive) {
            snapGlobalParameters();
            character_.reset();
        }
    }

    DrumCowbellParams params() const { return params_; }

    void trigger(DrumCowbellArticulation articulation,
        float velocity = 1.0f, int midiNote = -1)
    {
        velocity = clamp(drumFiniteOr(velocity, 1.0f), 0.0f, 1.0f);
        if (midiNote < 0) midiNote = drumCowbellCanonicalMidiNote(articulation);
        midiNote = std::max(0, std::min(127, midiNote));
        if (lerp(1.0f, velocity,
                params_.velocitySensitivity) <= 1.0e-7f) {
            return;
        }
        if (articulation == DrumCowbellArticulation::Muted) {
            chokeVoices(0.008f);
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
    static constexpr uint32_t kModeCount = 5u;

    struct Voice {
        bool active = false;
        bool choking = false;
        DrumCowbellArticulation articulation = DrumCowbellArticulation::Cowbell;
        float velocityGain = 1.0f;
        float velocityBrightness = 1.0f;
        float shape = 0.68f;
        float bodyLevel = 0.62f;
        float noiseLevel = 0.16f;
        float baseFrequency = 560.0f;
        float intervalRatio = 1.52f;
        float detuneRatio = 1.0f;
        float bendSemitones = 0.0f;
        float phaseOne = 0.0f;
        float phaseTwo = 0.0f;
        float contactPhase = 0.0f;
        float contactIncrement = 1.0f;
        float chokeGain = 1.0f;
        float chokeMultiplier = 1.0f;
        float noiseHighpassCoefficient = 0.04f;
        float noiseLowpassCoefficient = 0.4f;
        float noiseLow = 0.0f;
        float noiseBand = 0.0f;
        float noiseSmooth = 0.0f;
        std::array<DrumModalResonator, kModeCount> modes {};
        std::array<float, kModeCount> modeWeights {};
        std::array<float, kModeCount> modePans {};
        DrumExponentialEnvelope toneEnvelope {};
        DrumExponentialEnvelope bodyEnvelope {};
        DrumExponentialEnvelope noiseEnvelope {};
        DrumExponentialEnvelope bendEnvelope {};
        DrumRandom random {};
        uint32_t ageSamples = 0u;
        uint32_t maximumAgeSamples = 1u;
    };

    void chokeVoices(float seconds)
    {
        const float multiplier = drumDecayMultiplier(
            seconds, static_cast<float>(sampleRate_));
        for (Voice& voice : voices_) {
            if (!voice.active) continue;
            voice.choking = true;
            voice.chokeMultiplier = multiplier;
        }
    }

    void initialiseVoice(Voice& voice, uint32_t voiceIndex,
        uint64_t triggerIndex, DrumCowbellArticulation articulation,
        float velocity, int midiNote)
    {
        const float sr = static_cast<float>(sampleRate_);
        voice.active = true;
        voice.articulation = articulation;
        voice.velocityGain = lerp(1.0f, velocity,
            params_.velocitySensitivity);
        voice.velocityBrightness = lerp(1.0f,
            0.45f + velocity * 0.55f, params_.velocitySensitivity);
        voice.shape = params_.shape;
        voice.bodyLevel = params_.body;
        voice.noiseLevel = params_.noise;
        voice.intervalRatio = params_.intervalRatio;
        voice.detuneRatio = std::exp2(
            params_.detune * 0.18f / 12.0f);
        voice.bendSemitones = params_.bendSemitones;
        voice.random.reset(drumMixedSeed(voiceIndex, triggerIndex,
            0x434f5742u ^ static_cast<uint32_t>(articulation) * 0x9e3779b9u));

        const int canonical = drumCowbellCanonicalMidiNote(articulation);
        const float trackedSemitones = static_cast<float>(midiNote - canonical)
            * params_.noteTracking;
        const float articulationPitch = articulation
                == DrumCowbellArticulation::High ? 1.38f : 1.0f;
        voice.baseFrequency = clamp(params_.tuneHz
                * std::exp2(trackedSemitones / 12.0f) * articulationPitch,
            100.0f, sr * 0.16f);
        const float articulationDecay = articulation
                == DrumCowbellArticulation::Muted ? 0.30f
            : articulation == DrumCowbellArticulation::High ? 0.76f : 1.0f;

        voice.toneEnvelope.configure(
            params_.decaySeconds * articulationDecay, sr);
        voice.bodyEnvelope.configure(
            params_.bodyDecaySeconds * articulationDecay, sr);
        voice.noiseEnvelope.configure(
            params_.noiseDecaySeconds * articulationDecay, sr);
        voice.bendEnvelope.configure(params_.bendDecaySeconds, sr);
        voice.toneEnvelope.trigger();
        voice.bodyEnvelope.trigger();
        voice.noiseEnvelope.trigger();
        voice.bendEnvelope.trigger();

        constexpr std::array<float, kModeCount> ratios {{
            0.53f, 1.0f, 1.52f, 2.18f, 3.07f,
        }};
        for (uint32_t mode = 0u; mode < kModeCount; ++mode) {
            const float normalized = static_cast<float>(mode)
                / static_cast<float>(kModeCount - 1u);
            const float ratio = mode == 2u ? params_.intervalRatio
                : ratios[mode] * lerp(0.97f, 1.04f,
                    params_.detune * normalized);
            const float decayScale = lerp(1.16f, 0.34f,
                normalized * lerp(0.45f, 1.0f, params_.damping));
            voice.modes[mode].configure(
                std::min(sr * 0.42f, voice.baseFrequency * ratio),
                std::max(0.012f, params_.bodyDecaySeconds
                    * articulationDecay * decayScale), sr);
            voice.modeWeights[mode] = lerp(0.42f, 0.10f, normalized)
                * lerp(0.72f, 1.18f,
                    params_.brightness * normalized);
            voice.modePans[mode] = voice.random.bipolar()
                * lerp(0.04f, 0.58f, normalized);
            const float polarity = mode == 0u ? 1.0f
                : (voice.random.bipolar() >= 0.0f ? 1.0f : -1.0f);
            voice.modes[mode].strike(0.0f,
                -polarity * (0.68f + voice.random.unipolar() * 0.24f));
        }

        const float hardness = params_.attack * voice.velocityBrightness;
        const float contactSeconds = articulation
                == DrumCowbellArticulation::Muted
            ? lerp(0.0030f, 0.00045f, hardness)
            : lerp(0.0045f, 0.00065f, hardness);
        voice.contactIncrement = 1.0f
            / std::max(1.0f, contactSeconds * sr);

        const float highpassHz = lerp(220.0f, 2100.0f,
            params_.strikeTone) * voice.velocityBrightness;
        const float lowpassHz = lerp(2800.0f, 16000.0f,
            params_.strikeTone) * voice.velocityBrightness;
        voice.noiseHighpassCoefficient = drumOnePoleFrequencyCoefficient(
            std::max(100.0f, highpassHz), sr);
        voice.noiseLowpassCoefficient = drumOnePoleFrequencyCoefficient(
            std::max(highpassHz * 1.4f, lowpassHz), sr);
        voice.maximumAgeSamples = drumLongestTailSamples(sampleRate_, {
            drumCowbellTailSeconds(params_, articulation, sampleRate_),
        }, 0.05, 40.0);
    }

    static float shapedOscillator(float phase, float shape)
    {
        const float sine = std::sin(2.0f * kPi * phase);
        const float square = std::tanh(sine * 4.2f) * 0.82f;
        return lerp(sine, square, shape);
    }

    static float voiceActivitySquared(const Voice& voice)
    {
        float modes = 0.0f;
        for (uint32_t mode = 0u; mode < kModeCount; ++mode) {
            modes += voice.modes[mode].magnitudeSquared()
                * voice.modeWeights[mode] * voice.modeWeights[mode];
        }
        const float tone = voice.toneEnvelope.value() * 0.55f;
        const float body = voice.bodyEnvelope.value()
            * voice.bodyLevel * 0.55f;
        const float noise = voice.noiseEnvelope.value()
            * voice.noiseLevel * 0.45f;
        return (modes + tone * tone + body * body + noise * noise)
            * voice.velocityGain * voice.velocityGain
            * voice.chokeGain * voice.chokeGain;
    }

    void processVoice(Voice& voice, float& mid, float& side)
    {
        const float sr = static_cast<float>(sampleRate_);
        const float bend = voice.bendEnvelope.process();
        const float bendRatio = std::exp2(
            voice.bendSemitones * bend / 12.0f);
        voice.phaseOne += voice.baseFrequency * bendRatio / sr;
        voice.phaseTwo += voice.baseFrequency * voice.intervalRatio
            * voice.detuneRatio / sr;
        voice.phaseOne -= std::floor(voice.phaseOne);
        voice.phaseTwo -= std::floor(voice.phaseTwo);
        const float oscillator = shapedOscillator(
                voice.phaseOne, voice.shape)
            + shapedOscillator(voice.phaseTwo, voice.shape) * 0.78f;
        const float tone = oscillator * voice.toneEnvelope.process()
            * voice.velocityGain * 0.28f;

        const float noiseInput = voice.random.bipolar();
        voice.noiseLow += (noiseInput - voice.noiseLow)
            * voice.noiseHighpassCoefficient;
        const float noiseHigh = noiseInput - voice.noiseLow;
        voice.noiseBand += (noiseHigh - voice.noiseBand)
            * voice.noiseLowpassCoefficient;
        voice.noiseSmooth += (voice.noiseBand - voice.noiseSmooth)
            * voice.noiseLowpassCoefficient;
        const float noiseEnvelope = voice.noiseEnvelope.process();
        const float noise = voice.noiseSmooth * noiseEnvelope
            * voice.noiseLevel * voice.velocityGain
            * voice.velocityBrightness * 0.48f;

        float modalMid = 0.0f;
        float modalSide = 0.0f;
        for (uint32_t mode = 0u; mode < kModeCount; ++mode) {
            const float value = voice.modes[mode].process()
                * voice.modeWeights[mode];
            modalMid += value;
            modalSide += value * voice.modePans[mode];
        }
        const float bodyEnvelope = voice.bodyEnvelope.process();
        const float modalGain = bodyEnvelope * voice.bodyLevel
            * voice.velocityGain * 0.24f;
        modalMid *= modalGain;
        modalSide *= modalGain * 0.66f;

        float contactGain = 1.0f;
        if (voice.contactPhase < 1.0f) {
            const float phase = voice.contactPhase;
            contactGain = phase * phase * (3.0f - 2.0f * phase);
            voice.contactPhase = std::min(
                1.0f, phase + voice.contactIncrement);
        }
        const float gain = contactGain * voice.chokeGain;
        mid += (tone + modalMid + noise) * gain;
        side += (modalSide + noise * 0.22f
            * voice.random.bipolar()) * gain;

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
    DrumCowbellParams params_ {};
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
