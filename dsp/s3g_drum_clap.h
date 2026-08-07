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

enum class DrumClapArticulation : uint8_t {
    Clap = 0u,
    Flam = 1u,
    Tight = 2u,
};

inline int drumClapCanonicalMidiNote(DrumClapArticulation articulation)
{
    switch (articulation) {
    case DrumClapArticulation::Flam: return 40;
    case DrumClapArticulation::Tight: return 41;
    case DrumClapArticulation::Clap:
    default: return 39;
    }
}

// The first sixteen members define one synthesized clap and are latched per
// hit. Character, width, velocity response and output remain live.
struct DrumClapParams {
    float toneHz = 3900.0f;
    float noteTracking = 0.20f;
    float hands = 4.0f;
    float spreadMs = 28.0f;
    float scatter = 0.45f;
    float attack = 0.65f;
    float bandwidth = 0.72f;
    float air = 0.42f;
    float burstDecaySeconds = 0.030f;
    float tailDecaySeconds = 0.18f;
    float tail = 0.48f;
    float body = 0.30f;
    float bodyTuneHz = 940.0f;
    float bodyDecaySeconds = 0.075f;
    float flamTimeMs = 34.0f;
    float texture = 0.56f;
    DrumCharacterParams character {};
    float stereoWidth = 0.36f;
    float velocitySensitivity = 0.90f;
    float outputGainDb = -7.5f;
};

inline DrumClapParams drumSanitizeClapParams(DrumClapParams params)
{
    params.toneHz = clamp(drumFiniteOr(params.toneHz, 3900.0f),
        700.0f, 10000.0f);
    params.noteTracking = clamp(
        drumFiniteOr(params.noteTracking, 0.20f), 0.0f, 1.0f);
    params.hands = clamp(drumFiniteOr(params.hands, 4.0f), 1.0f, 8.0f);
    params.spreadMs = clamp(
        drumFiniteOr(params.spreadMs, 28.0f), 0.0f, 65.0f);
    params.scatter = clamp(drumFiniteOr(params.scatter, 0.45f), 0.0f, 1.0f);
    params.attack = clamp(drumFiniteOr(params.attack, 0.65f), 0.0f, 1.0f);
    params.bandwidth = clamp(
        drumFiniteOr(params.bandwidth, 0.72f), 0.0f, 1.0f);
    params.air = clamp(drumFiniteOr(params.air, 0.42f), 0.0f, 1.0f);
    params.burstDecaySeconds = clamp(
        drumFiniteOr(params.burstDecaySeconds, 0.030f), 0.006f, 0.18f);
    params.tailDecaySeconds = clamp(
        drumFiniteOr(params.tailDecaySeconds, 0.18f), 0.025f, 2.0f);
    params.tail = clamp(drumFiniteOr(params.tail, 0.48f), 0.0f, 1.0f);
    params.body = clamp(drumFiniteOr(params.body, 0.30f), 0.0f, 1.0f);
    params.bodyTuneHz = clamp(
        drumFiniteOr(params.bodyTuneHz, 940.0f), 280.0f, 3200.0f);
    params.bodyDecaySeconds = clamp(
        drumFiniteOr(params.bodyDecaySeconds, 0.075f), 0.012f, 0.60f);
    params.flamTimeMs = clamp(
        drumFiniteOr(params.flamTimeMs, 34.0f), 5.0f, 120.0f);
    params.texture = clamp(drumFiniteOr(params.texture, 0.56f), 0.0f, 1.0f);
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
        drumFiniteOr(params.stereoWidth, 0.36f), 0.0f, 1.0f);
    params.velocitySensitivity = clamp(
        drumFiniteOr(params.velocitySensitivity, 0.90f), 0.0f, 1.0f);
    params.outputGainDb = clamp(
        drumFiniteOr(params.outputGainDb, -7.5f), -36.0f, 12.0f);
    return params;
}

inline double drumClapTailSeconds(const DrumClapParams& source,
    DrumClapArticulation articulation, double sampleRate = 48000.0)
{
    (void)sampleRate;
    const DrumClapParams params = drumSanitizeClapParams(source);
    const double handSpread = articulation == DrumClapArticulation::Flam
        ? (params.flamTimeMs + params.spreadMs * 0.38) * 0.001
        : articulation == DrumClapArticulation::Tight
            ? std::min(8.0f, params.spreadMs * 0.28f) * 0.001
            : params.spreadMs * 0.001;
    const double articulationTail = articulation == DrumClapArticulation::Tight
        ? 0.48 : 1.0;
    const double noiseTail = params.tail > 1.0e-6f
        ? params.tailDecaySeconds * articulationTail * 2.15 : 0.0;
    const double burstTail = params.burstDecaySeconds
        * (articulation == DrumClapArticulation::Tight ? 0.68 : 1.0)
        * 2.15;
    const double bodyTail = params.body > 1.0e-6f
        ? params.bodyDecaySeconds
            * (articulation == DrumClapArticulation::Tight ? 0.60 : 1.0)
            * 2.15 : 0.0;
    return std::min(40.0, std::max({
        0.06,
        handSpread + noiseTail,
        handSpread + burstTail,
        handSpread + bodyTail,
        params.character.drive > 1.0e-6f ? 0.50 : 0.0,
    }));
}

class DrumClap {
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

    void setParams(DrumClapParams params)
    {
        const bool wasInactive = activeVoiceCount_ == 0u
            && !character_.active();
        params_ = drumSanitizeClapParams(params);
        outputGainTarget_ = dbToGain(params_.outputGainDb);
        character_.setParams(params_.character);
        if (wasInactive) {
            snapGlobalParameters();
            character_.reset();
        }
    }

    DrumClapParams params() const { return params_; }

    void trigger(DrumClapArticulation articulation,
        float velocity = 1.0f, int midiNote = -1)
    {
        velocity = clamp(drumFiniteOr(velocity, 1.0f), 0.0f, 1.0f);
        if (midiNote < 0) midiNote = drumClapCanonicalMidiNote(articulation);
        midiNote = std::max(0, std::min(127, midiNote));
        if (lerp(1.0f, velocity,
                params_.velocitySensitivity) <= 1.0e-7f) {
            return;
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
    static constexpr uint32_t kMaximumHands = 8u;
    static constexpr uint32_t kBodyModeCount = 3u;

    struct Hand {
        bool started = false;
        uint32_t offsetSamples = 0u;
        float gain = 1.0f;
        float pan = 0.0f;
        float contactPhase = 0.0f;
        float contactIncrement = 1.0f;
        float noiseHighpassCoefficient = 0.08f;
        float noiseLowpassCoefficient = 0.4f;
        float airHighpassCoefficient = 0.4f;
        float noiseLow = 0.0f;
        float noiseBand = 0.0f;
        float noiseSmooth = 0.0f;
        float airLow = 0.0f;
        DrumExponentialEnvelope burstEnvelope {};
        DrumExponentialEnvelope tailEnvelope {};
        std::array<DrumModalResonator, kBodyModeCount> bodyModes {};
        std::array<float, kBodyModeCount> bodyWeights {};
    };

    struct Voice {
        bool active = false;
        DrumClapArticulation articulation = DrumClapArticulation::Clap;
        float velocityGain = 1.0f;
        float velocityBrightness = 1.0f;
        float tailLevel = 0.48f;
        float bodyLevel = 0.30f;
        float airLevel = 0.42f;
        float texture = 0.56f;
        uint32_t handCount = 0u;
        uint32_t ageSamples = 0u;
        uint32_t maximumAgeSamples = 1u;
        std::array<Hand, kMaximumHands> hands {};
        DrumRandom random {};
    };

    void initialiseVoice(Voice& voice, uint32_t voiceIndex,
        uint64_t triggerIndex, DrumClapArticulation articulation,
        float velocity, int midiNote)
    {
        const float sr = static_cast<float>(sampleRate_);
        voice.active = true;
        voice.articulation = articulation;
        voice.velocityGain = lerp(1.0f, velocity,
            params_.velocitySensitivity);
        voice.velocityBrightness = lerp(1.0f,
            0.42f + velocity * 0.58f, params_.velocitySensitivity);
        voice.tailLevel = params_.tail;
        voice.bodyLevel = params_.body;
        voice.airLevel = params_.air;
        voice.texture = params_.texture;
        voice.random.reset(drumMixedSeed(voiceIndex, triggerIndex,
            0x434c4150u ^ static_cast<uint32_t>(articulation)
                * 0x9e3779b9u));

        uint32_t handCount = static_cast<uint32_t>(std::lround(params_.hands));
        handCount = std::max(1u, std::min(kMaximumHands, handCount));
        if (articulation == DrumClapArticulation::Flam) {
            handCount = std::min(kMaximumHands,
                std::max(4u, handCount + 2u));
        } else if (articulation == DrumClapArticulation::Tight) {
            handCount = std::min(4u, std::max(2u, handCount));
        }
        voice.handCount = handCount;

        const int canonicalNote = drumClapCanonicalMidiNote(articulation);
        const float trackedSemitones = static_cast<float>(
            midiNote - canonicalNote) * params_.noteTracking;
        const float trackingRatio = std::exp2(trackedSemitones / 12.0f);
        const float toneHz = clamp(params_.toneHz * trackingRatio,
            420.0f, sr * 0.34f);
        const float bodyTuneHz = clamp(params_.bodyTuneHz * trackingRatio,
            180.0f, sr * 0.18f);

        const float spreadSamples = params_.spreadMs * 0.001f * sr;
        uint32_t previousOffset = 0u;
        for (uint32_t index = 0u; index < handCount; ++index) {
            Hand& hand = voice.hands[index];
            float offset = 0.0f;
            if (articulation == DrumClapArticulation::Flam) {
                const uint32_t firstCluster = handCount / 2u;
                const bool second = index >= firstCluster;
                const uint32_t localIndex = second
                    ? index - firstCluster : index;
                const uint32_t localCount = second
                    ? handCount - firstCluster : firstCluster;
                const float localSpan = spreadSamples * 0.38f;
                const float localPhase = localCount > 1u
                    ? static_cast<float>(localIndex)
                        / static_cast<float>(localCount - 1u) : 0.0f;
                offset = localPhase * localSpan;
                if (second) {
                    offset += std::max(params_.flamTimeMs * 0.001f * sr,
                        localSpan + sr * 0.002f);
                }
            } else {
                const float span = articulation == DrumClapArticulation::Tight
                    ? std::min(spreadSamples * 0.28f, sr * 0.008f)
                    : spreadSamples;
                offset = handCount > 1u
                    ? span * static_cast<float>(index)
                        / static_cast<float>(handCount - 1u) : 0.0f;
            }

            if (index != 0u) {
                const float nominalSpacing = std::max(1.0f,
                    offset - static_cast<float>(previousOffset));
                offset += voice.random.bipolar() * params_.scatter
                    * nominalSpacing * 0.46f;
                offset = std::max(offset,
                    static_cast<float>(previousOffset + 1u));
            }
            hand.offsetSamples = static_cast<uint32_t>(
                std::max(0.0f, std::round(offset)));
            previousOffset = hand.offsetSamples;
            hand.gain = (0.82f + voice.random.unipolar() * 0.30f)
                / std::sqrt(static_cast<float>(handCount));
            hand.pan = voice.random.bipolar()
                * lerp(0.18f, 0.92f, params_.scatter);

            const float hardness = params_.attack
                * voice.velocityBrightness;
            float attackSeconds = lerp(0.0038f, 0.00055f, hardness);
            if (articulation == DrumClapArticulation::Tight) {
                attackSeconds *= 0.72f;
            }
            hand.contactPhase = 0.0f;
            hand.contactIncrement = 1.0f
                / std::max(1.0f, attackSeconds * sr);

            const float lowHz = clamp(toneHz
                    * lerp(0.72f, 0.18f, params_.bandwidth),
                180.0f, sr * 0.30f);
            const float highHz = clamp(toneHz
                    * lerp(1.24f, 3.15f, params_.bandwidth),
                lowHz * 1.15f, sr * 0.45f);
            hand.noiseHighpassCoefficient =
                drumOnePoleFrequencyCoefficient(lowHz, sr);
            hand.noiseLowpassCoefficient =
                drumOnePoleFrequencyCoefficient(highHz, sr);
            hand.airHighpassCoefficient = drumOnePoleFrequencyCoefficient(
                lerp(5200.0f, 14500.0f, params_.air)
                    * lerp(0.84f, 1.08f, voice.velocityBrightness), sr);

            const float articulationDecay = articulation
                    == DrumClapArticulation::Tight ? 0.68f : 1.0f;
            hand.burstEnvelope.configure(params_.burstDecaySeconds
                * articulationDecay, sr);
            hand.tailEnvelope.configure(params_.tailDecaySeconds
                * (articulation == DrumClapArticulation::Tight
                    ? 0.48f : 1.0f), sr);

            constexpr std::array<float, kBodyModeCount> bodyRatios {{
                1.0f, 1.67f, 2.46f,
            }};
            for (uint32_t mode = 0u; mode < kBodyModeCount; ++mode) {
                const float normalized = static_cast<float>(mode)
                    / static_cast<float>(kBodyModeCount - 1u);
                hand.bodyModes[mode].configure(
                    std::min(sr * 0.42f, bodyTuneHz * bodyRatios[mode]),
                    params_.bodyDecaySeconds
                        * lerp(1.0f, 0.58f, normalized)
                        * (articulation == DrumClapArticulation::Tight
                            ? 0.60f : 1.0f), sr);
                hand.bodyWeights[mode] = lerp(0.46f, 0.18f, normalized);
            }
        }

        voice.maximumAgeSamples = drumLongestTailSamples(sampleRate_, {
            drumClapTailSeconds(params_, articulation, sampleRate_),
        }, 0.06, 40.0);
    }

    void startHand(Hand& hand, Voice& voice)
    {
        hand.started = true;
        hand.burstEnvelope.trigger();
        hand.tailEnvelope.trigger();
        for (uint32_t mode = 0u; mode < kBodyModeCount; ++mode) {
            const float polarity = mode == 0u ? 1.0f
                : (voice.random.bipolar() >= 0.0f ? 1.0f : -1.0f);
            hand.bodyModes[mode].strike(0.0f,
                -polarity * (0.72f + voice.random.unipolar() * 0.22f));
        }
    }

    static float handActivitySquared(const Hand& hand)
    {
        if (!hand.started) return 1.0f;
        float modes = 0.0f;
        for (uint32_t mode = 0u; mode < kBodyModeCount; ++mode) {
            modes += hand.bodyModes[mode].magnitudeSquared()
                * hand.bodyWeights[mode] * hand.bodyWeights[mode];
        }
        return hand.gain * hand.gain * (
            hand.burstEnvelope.value() * hand.burstEnvelope.value()
            + hand.tailEnvelope.value() * hand.tailEnvelope.value()
            + modes);
    }

    static float voiceActivitySquared(const Voice& voice)
    {
        float result = 0.0f;
        for (uint32_t index = 0u; index < voice.handCount; ++index) {
            result += handActivitySquared(voice.hands[index]);
        }
        return result * voice.velocityGain * voice.velocityGain;
    }

    void processVoice(Voice& voice, float& mid, float& side)
    {
        float voiceMid = 0.0f;
        float voiceSide = 0.0f;
        for (uint32_t index = 0u; index < voice.handCount; ++index) {
            Hand& hand = voice.hands[index];
            if (!hand.started && voice.ageSamples >= hand.offsetSamples) {
                startHand(hand, voice);
            }
            if (!hand.started) continue;

            const float primaryNoise = voice.random.bipolar();
            const float secondaryNoise = (voice.random.bipolar()
                + voice.random.bipolar()) * 0.5f;
            const float sparse = voice.random.unipolar()
                    > lerp(0.985f, 0.88f, voice.texture)
                ? voice.random.bipolar() : 0.0f;
            const float noise = lerp(secondaryNoise, primaryNoise,
                    voice.texture * 0.72f)
                + sparse * voice.texture * 0.30f;

            hand.noiseLow += (noise - hand.noiseLow)
                * hand.noiseHighpassCoefficient;
            const float highpassed = noise - hand.noiseLow;
            hand.noiseBand += (highpassed - hand.noiseBand)
                * hand.noiseLowpassCoefficient;
            hand.noiseSmooth += (hand.noiseBand - hand.noiseSmooth)
                * hand.noiseLowpassCoefficient;
            const float coloredNoise = lerp(hand.noiseSmooth,
                hand.noiseBand, 0.28f + voice.texture * 0.58f);
            hand.airLow += (noise - hand.airLow)
                * hand.airHighpassCoefficient;
            const float airNoise = noise - hand.airLow;

            const float transientEnvelope = hand.burstEnvelope.process();
            const float tailEnvelope = hand.tailEnvelope.process();
            float body = 0.0f;
            for (uint32_t mode = 0u; mode < kBodyModeCount; ++mode) {
                body += hand.bodyModes[mode].process(
                        coloredNoise * transientEnvelope * 0.0008f)
                    * hand.bodyWeights[mode];
            }

            float contactGain = 1.0f;
            if (hand.contactPhase < 1.0f) {
                const float phase = hand.contactPhase;
                contactGain = phase * phase * (3.0f - 2.0f * phase);
                hand.contactPhase = std::min(
                    1.0f, phase + hand.contactIncrement);
            }
            const float transient = coloredNoise * transientEnvelope * 0.72f;
            const float tail = (coloredNoise * 0.78f
                    + airNoise * voice.airLevel * 0.46f)
                * tailEnvelope * voice.tailLevel * 0.44f;
            const float bodyOutput = body * voice.bodyLevel * 0.22f;
            const float output = (transient + tail + bodyOutput)
                * contactGain * hand.gain * voice.velocityGain
                * lerp(0.78f, 1.0f, voice.velocityBrightness);
            voiceMid += output;
            voiceSide += output * hand.pan;
        }

        const float articulationLevel = voice.articulation
                == DrumClapArticulation::Flam ? 0.88f
            : voice.articulation == DrumClapArticulation::Tight
                ? 1.08f : 1.0f;
        mid += voiceMid * articulationLevel;
        side += voiceSide * articulationLevel * 0.72f;

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
    DrumClapParams params_ {};
    DrumCharacter character_ {};
    std::array<Voice, kVoiceCount> voices_ {};
    uint64_t triggerCounter_ = 0u;
    uint32_t activeVoiceCount_ = 0u;
    float smoothingCoefficient_ = 0.004157998f;
    float outputGainTarget_ = 0.4216965f;
    float smoothedOutputGain_ = 0.4216965f;
    float smoothedWidth_ = 0.36f;
};

} // namespace s3g
