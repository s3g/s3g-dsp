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

// The first sixteen members are the snare voice vocabulary.  They are copied
// into a voice at trigger time, giving tracker parameter locks a per-hit scope.
// Character, width and output are shared/live performance controls.
struct DrumSnareParams {
    float tuneHz = 180.0f;
    float noteTracking = 0.75f;
    float pitchDropSemitones = 7.0f;
    float pitchSweepMs = 22.0f;
    float shellSpread = 0.40f;
    float body = 0.58f;
    float ring = 0.32f;
    float bodyDecaySeconds = 0.42f;
    float punch = 0.78f;
    float wires = 0.70f;
    float wireTone = 0.56f;
    float wireTension = 0.48f;
    float wireDecaySeconds = 0.34f;
    float rattle = 0.18f;
    float click = 0.10f;
    float clickTone = 0.60f;
    DrumCharacterParams character {};
    float stereoWidth = 0.0f;
    float velocitySensitivity = 0.90f;
    float outputGainDb = -6.0f;
};

class DrumSnare {
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

    void setParams(DrumSnareParams params)
    {
        const bool wasInactive = activeVoiceCount_ == 0u
            && !character_.active();
        params_ = sanitize(params);
        outputGainTarget_ = dbToGain(params_.outputGainDb);
        character_.setParams(params_.character);
        if (wasInactive) {
            snapGlobalParameters();
            character_.reset();
        }
    }

    DrumSnareParams params() const { return params_; }

    void trigger(float velocity = 1.0f, int midiNote = 38)
    {
        velocity = clamp(drumFiniteOr(velocity, 1.0f), 0.0f, 1.0f);
        midiNote = std::max(0, std::min(127, midiNote));
        // A tracker may emit a zero-velocity hit as a compact mute.  When
        // sensitivity makes it exactly silent, do not allocate or steal a
        // voice whose resonators would otherwise keep the host awake.
        if (lerp(1.0f, velocity, params_.velocitySensitivity) <= 1.0e-7f) {
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
        initialiseVoice(voice, selected, ++triggerCounter_, velocity,
            midiNote);

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
        uint32_t voicesRemaining = 0u;
        for (Voice& voice : voices_) {
            if (!voice.active) continue;
            processVoice(voice, mid, side);
            if (voice.active) ++voicesRemaining;
        }
        activeVoiceCount_ = voicesRemaining;

        // Only the wire path writes to side.  The shell, impact and click stay
        // centered even when width is fully open.
        float frameLeft = mid + side * smoothedWidth_;
        float frameRight = mid - side * smoothedWidth_;
        character_.processFrame(frameLeft, frameRight);
        if (params_.stereoWidth == 0.0f) {
            // Character has independent channel histories.  Collapsing after
            // it makes a live wide-to-mono change exact immediately too.
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
    static constexpr uint32_t kShellModeCount = 4u;

    struct Voice {
        bool active = false;
        float velocityGain = 1.0f;
        float velocityBrightness = 1.0f;
        float finalFrequencyHz = 180.0f;
        float pitchDropSemitones = 7.0f;
        float bodyLevel = 0.58f;
        float ringLevel = 0.32f;
        float punchLevel = 0.78f;
        float wireLevel = 0.70f;
        float wireTone = 0.56f;
        float wireTension = 0.48f;
        float rattleLevel = 0.18f;
        float clickLevel = 0.10f;
        float clickTone = 0.60f;
        std::array<DrumModalResonator, kShellModeCount> shellModes {};
        std::array<float, kShellModeCount> shellWeights {};
        DrumExponentialEnvelope pitchEnvelope {};
        DrumExponentialEnvelope impactEnvelope {};
        DrumExponentialEnvelope wireEnvelope {};
        DrumExponentialEnvelope rattleEnvelope {};
        DrumExponentialEnvelope clickEnvelope {};
        DrumRandom random {};
        float impactPhase = 0.0f;
        float clickPhase = 0.0f;
        float wirePhase = 0.0f;
        float clickFrequencyHz = 3600.0f;
        float wireFrequencyHz = 4200.0f;
        float wireHighpassCoefficient = 0.03f;
        float wireLowpassCoefficient = 0.4f;
        float clickHighpassCoefficient = 0.12f;
        float clickLowpassCoefficient = 0.5f;
        float rattleSmoothingCoefficient = 0.01f;
        float wireMidLow = 0.0f;
        float wireMidBand = 0.0f;
        float wireMidDamped = 0.0f;
        float wireMidDampedTwo = 0.0f;
        float wireSideLow = 0.0f;
        float wireSideBand = 0.0f;
        float wireSideDamped = 0.0f;
        float wireSideDampedTwo = 0.0f;
        float clickLow = 0.0f;
        float clickBand = 0.0f;
        float rattleSmooth = 0.0f;
        uint32_t ageSamples = 0u;
        uint32_t maximumAgeSamples = 1u;
    };

    static DrumSnareParams sanitize(DrumSnareParams params)
    {
        params.tuneHz = clamp(drumFiniteOr(params.tuneHz, 180.0f),
            70.0f, 420.0f);
        params.noteTracking = clamp(
            drumFiniteOr(params.noteTracking, 0.75f), 0.0f, 1.0f);
        params.pitchDropSemitones = clamp(
            drumFiniteOr(params.pitchDropSemitones, 7.0f), -12.0f, 36.0f);
        params.pitchSweepMs = clamp(
            drumFiniteOr(params.pitchSweepMs, 22.0f), 1.0f, 250.0f);
        params.shellSpread = clamp(
            drumFiniteOr(params.shellSpread, 0.40f), 0.0f, 1.0f);
        params.body = clamp(drumFiniteOr(params.body, 0.58f), 0.0f, 1.0f);
        params.ring = clamp(drumFiniteOr(params.ring, 0.32f), 0.0f, 1.0f);
        params.bodyDecaySeconds = clamp(
            drumFiniteOr(params.bodyDecaySeconds, 0.42f), 0.02f, 3.0f);
        params.punch = clamp(drumFiniteOr(params.punch, 0.78f), 0.0f, 1.0f);
        params.wires = clamp(drumFiniteOr(params.wires, 0.70f), 0.0f, 1.0f);
        params.wireTone = clamp(
            drumFiniteOr(params.wireTone, 0.56f), 0.0f, 1.0f);
        params.wireTension = clamp(
            drumFiniteOr(params.wireTension, 0.48f), 0.0f, 1.0f);
        params.wireDecaySeconds = clamp(
            drumFiniteOr(params.wireDecaySeconds, 0.34f), 0.01f, 4.0f);
        params.rattle = clamp(drumFiniteOr(params.rattle, 0.18f), 0.0f, 1.0f);
        params.click = clamp(drumFiniteOr(params.click, 0.10f), 0.0f, 1.0f);
        params.clickTone = clamp(
            drumFiniteOr(params.clickTone, 0.60f), 0.0f, 1.0f);
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
            drumFiniteOr(params.character.reconstruction, 0.0f),
            0.0f, 1.0f);
        params.character.tone = clamp(
            drumFiniteOr(params.character.tone, 0.0f), -1.0f, 1.0f);
        params.stereoWidth = clamp(
            drumFiniteOr(params.stereoWidth, 0.0f), 0.0f, 1.0f);
        params.velocitySensitivity = clamp(
            drumFiniteOr(params.velocitySensitivity, 0.90f), 0.0f, 1.0f);
        params.outputGainDb = clamp(
            drumFiniteOr(params.outputGainDb, -6.0f), -36.0f, 12.0f);
        return params;
    }

    void initialiseVoice(Voice& voice, uint32_t voiceIndex,
        uint64_t triggerIndex, float velocity, int midiNote)
    {
        const float sr = static_cast<float>(sampleRate_);
        const float trackedSemitones = static_cast<float>(midiNote - 38)
            * params_.noteTracking;
        const float noteRatio = std::exp2(trackedSemitones / 12.0f);

        voice.active = true;
        voice.velocityGain = lerp(1.0f, velocity,
            params_.velocitySensitivity);
        voice.velocityBrightness = lerp(1.0f, 0.35f + velocity * 0.65f,
            params_.velocitySensitivity);
        voice.finalFrequencyHz = clamp(params_.tuneHz * noteRatio,
            35.0f, sr * 0.16f);
        voice.pitchDropSemitones = params_.pitchDropSemitones;
        voice.bodyLevel = params_.body;
        voice.ringLevel = params_.ring;
        voice.punchLevel = params_.punch;
        voice.wireLevel = params_.wires;
        voice.wireTone = params_.wireTone;
        voice.wireTension = params_.wireTension;
        voice.rattleLevel = params_.rattle;
        voice.clickLevel = params_.click;
        voice.clickTone = params_.clickTone;
        voice.random.reset(drumMixedSeed(voiceIndex, triggerIndex,
            0x534e4152u));

        const float spread = params_.shellSpread;
        const std::array<float, kShellModeCount> ratios {{
            1.0f,
            1.43f + spread * 0.17f,
            1.95f + spread * 0.38f,
            2.61f + spread * 0.71f,
        }};
        const std::array<float, kShellModeCount> decays {{
            params_.bodyDecaySeconds * lerp(0.72f, 1.28f, params_.body),
            params_.bodyDecaySeconds * lerp(0.50f, 1.55f, params_.ring),
            params_.bodyDecaySeconds * lerp(0.34f, 1.95f, params_.ring),
            params_.bodyDecaySeconds * lerp(0.24f, 1.55f, params_.ring),
        }};
        voice.shellWeights = {{
            0.36f + params_.body * 0.38f,
            0.20f + params_.body * 0.12f + params_.ring * 0.06f,
            params_.ring * (0.13f + spread * 0.09f),
            params_.ring * (0.07f + spread * 0.12f),
        }};
        float longestModeSeconds = 0.0f;
        for (uint32_t mode = 0u; mode < kShellModeCount; ++mode) {
            voice.shellModes[mode].configure(
                std::min(sr * 0.45f, voice.finalFrequencyHz * ratios[mode]),
                decays[mode], sr);
            const float polarity = mode == 0u ? 1.0f
                : (voice.random.bipolar() >= 0.0f ? 1.0f : -1.0f);
            const float strike = polarity
                * (0.88f + voice.random.unipolar() * 0.12f);
            voice.shellModes[mode].strike(strike,
                strike * voice.random.bipolar() * 0.24f);
            longestModeSeconds = std::max(longestModeSeconds, decays[mode]);
        }

        const float pitchSeconds = params_.pitchSweepMs * 0.001f;
        voice.pitchEnvelope.configure(pitchSeconds, sr, 0.01f);
        voice.pitchEnvelope.trigger();
        const float impactSeconds = std::max(pitchSeconds * 0.75f,
            lerp(0.032f, 0.005f, params_.punch));
        voice.impactEnvelope.configure(impactSeconds, sr);
        voice.impactEnvelope.trigger();

        voice.wireEnvelope.configure(params_.wireDecaySeconds, sr);
        voice.wireEnvelope.trigger();
        const float rattleSeconds = params_.wireDecaySeconds
            * lerp(1.05f, 2.80f, params_.rattle);
        voice.rattleEnvelope.configure(rattleSeconds, sr);
        voice.rattleEnvelope.trigger();

        const float clickSeconds = lerp(0.010f, 0.0015f,
            params_.clickTone);
        voice.clickEnvelope.configure(clickSeconds, sr);
        voice.clickEnvelope.trigger();

        const float wireHighpassHz = 160.0f
            * std::pow(11.0f, params_.wireTension)
            * lerp(0.82f, 1.18f, voice.velocityBrightness);
        const float wireToneCurve = params_.wireTone;
        const float wireLowpassHz = 1965.0f
            * std::exp(0.99f * wireToneCurve
                + 1.14f * wireToneCurve * wireToneCurve)
            * lerp(0.78f, 1.10f, voice.velocityBrightness);
        voice.wireHighpassCoefficient = drumOnePoleFrequencyCoefficient(
            wireHighpassHz, sr);
        voice.wireLowpassCoefficient = drumOnePoleFrequencyCoefficient(
            std::max(wireHighpassHz * 1.35f, wireLowpassHz), sr);
        voice.wireFrequencyHz = std::min(sr * 0.42f,
            voice.finalFrequencyHz * lerp(7.0f, 19.0f,
                params_.wireTension)
                + lerp(350.0f, 2100.0f, params_.wireTone));
        voice.rattleSmoothingCoefficient = drumOnePoleFrequencyCoefficient(
            lerp(35.0f, 280.0f, params_.wireTension), sr);

        const float clickHighpassHz = lerp(650.0f, 5200.0f,
            params_.clickTone);
        const float clickLowpassHz = lerp(3600.0f, 16000.0f,
            params_.clickTone);
        voice.clickHighpassCoefficient = drumOnePoleFrequencyCoefficient(
            clickHighpassHz, sr);
        voice.clickLowpassCoefficient = drumOnePoleFrequencyCoefficient(
            clickLowpassHz, sr);
        voice.clickFrequencyHz = std::min(sr * 0.42f,
            1000.0f * std::pow(9.0f, params_.clickTone));

        // Every exponential primitive reaches -60 dB at its named time.  A
        // factor above two carries the longest selected component through its
        // -120 dB point, while the state-based early exit usually ends first.
        const double modeTail = static_cast<double>(longestModeSeconds) * 2.15;
        const double impactTail = static_cast<double>(impactSeconds) * 2.15;
        const double wireTail = params_.wires > 1.0e-6f
            ? static_cast<double>(params_.wireDecaySeconds) * 2.15 : 0.0;
        const double rattleTail = params_.wires > 1.0e-6f
                && params_.rattle > 1.0e-6f
            ? static_cast<double>(rattleSeconds) * 2.15 : 0.0;
        const double clickTail = params_.click > 1.0e-6f
            ? static_cast<double>(clickSeconds) * 2.15 : 0.0;
        voice.maximumAgeSamples = drumLongestTailSamples(sampleRate_, {
            modeTail, impactTail, wireTail, rattleTail, clickTail,
        }, 0.10, 40.0);
    }

    static float voiceActivitySquared(const Voice& voice)
    {
        float modal = 0.0f;
        const float modalGain = voice.velocityGain
            * (0.42f + voice.bodyLevel * 0.48f);
        for (uint32_t mode = 0u; mode < kShellModeCount; ++mode) {
            modal += voice.shellModes[mode].magnitudeSquared()
                * voice.shellWeights[mode] * voice.shellWeights[mode];
        }
        const float impact = voice.impactEnvelope.value()
            * voice.velocityGain
            * (0.08f + voice.punchLevel * 0.30f);
        const float wire = voice.wireEnvelope.value()
            * voice.wireLevel * voice.velocityGain
            * voice.velocityBrightness * 0.52f;
        const float rattle = voice.rattleEnvelope.value()
            * voice.wireLevel * voice.velocityGain
            * voice.velocityBrightness * voice.rattleLevel
            * (0.16f + voice.rattleSmooth * 1.7f);
        const float click = voice.clickEnvelope.value()
            * voice.clickLevel * voice.velocityGain
            * voice.velocityBrightness * 0.72f;
        return modal * modalGain * modalGain
            + impact * impact + wire * wire
            + rattle * rattle + click * click;
    }

    void processVoice(Voice& voice, float& mid, float& side)
    {
        const float sr = static_cast<float>(sampleRate_);
        const float noiseMid = voice.random.bipolar();
        const float noiseSide = (voice.random.bipolar()
            - voice.random.bipolar()) * 0.5f;
        const float noiseClick = voice.random.bipolar();
        const float noiseRattle = voice.random.bipolar();

        float shell = 0.0f;
        for (uint32_t mode = 0u; mode < kShellModeCount; ++mode) {
            shell += voice.shellModes[mode].process()
                * voice.shellWeights[mode];
        }
        shell *= voice.velocityGain
            * (0.42f + voice.bodyLevel * 0.48f);

        const float pitchEnvelope = voice.pitchEnvelope.process();
        const float impactFrequency = clamp(voice.finalFrequencyHz
                * std::exp2(voice.pitchDropSemitones
                    * pitchEnvelope / 12.0f),
            25.0f, sr * 0.44f);
        voice.impactPhase += impactFrequency / sr;
        voice.impactPhase -= std::floor(voice.impactPhase);
        const float impactEnvelope = voice.impactEnvelope.process();
        const float impactOscillator = std::sin(
            2.0f * kPi * voice.impactPhase);
        const float impact = (impactOscillator * 0.78f
                + noiseMid * 0.22f * voice.velocityBrightness)
            * impactEnvelope * voice.velocityGain
            * (0.08f + voice.punchLevel * 0.30f);

        voice.wireMidLow += (noiseMid - voice.wireMidLow)
            * voice.wireHighpassCoefficient;
        const float wireMidHigh = noiseMid - voice.wireMidLow;
        voice.wireMidBand += (wireMidHigh - voice.wireMidBand)
            * voice.wireLowpassCoefficient;
        voice.wireMidDamped += (voice.wireMidBand - voice.wireMidDamped)
            * voice.wireLowpassCoefficient;
        voice.wireMidDampedTwo +=
            (voice.wireMidDamped - voice.wireMidDampedTwo)
            * voice.wireLowpassCoefficient;

        voice.wireSideLow += (noiseSide - voice.wireSideLow)
            * voice.wireHighpassCoefficient;
        const float wireSideHigh = noiseSide - voice.wireSideLow;
        voice.wireSideBand += (wireSideHigh - voice.wireSideBand)
            * voice.wireLowpassCoefficient;
        voice.wireSideDamped +=
            (voice.wireSideBand - voice.wireSideDamped)
            * voice.wireLowpassCoefficient;
        voice.wireSideDampedTwo +=
            (voice.wireSideDamped - voice.wireSideDampedTwo)
            * voice.wireLowpassCoefficient;

        voice.wirePhase += voice.wireFrequencyHz / sr;
        voice.wirePhase -= std::floor(voice.wirePhase);
        const float wireCarrier = std::sin(2.0f * kPi * voice.wirePhase);
        const float tensionColor = voice.wireTension * 0.34f;
        const float coloredWireMid = voice.wireMidDampedTwo
            * (1.0f + wireCarrier * tensionColor);
        const float coloredWireSide = voice.wireSideDampedTwo
            * (1.0f - wireCarrier * tensionColor);

        const float wireEnvelope = voice.wireEnvelope.process();
        const float rattleEnvelope = voice.rattleEnvelope.process();
        const float rattleImpulse = std::max(0.0f,
            std::abs(noiseRattle)
                - lerp(0.96f, 0.72f, voice.rattleLevel))
            * lerp(5.0f, 2.0f, voice.rattleLevel);
        voice.rattleSmooth += (rattleImpulse - voice.rattleSmooth)
            * voice.rattleSmoothingCoefficient;
        const float rattleGain = voice.rattleLevel * rattleEnvelope
            * (0.16f + voice.rattleSmooth * 1.7f);
        const float wireAmplitude = voice.wireLevel * voice.velocityGain
            * voice.velocityBrightness
            * (wireEnvelope * 0.52f + rattleGain);
        const float shellToWire = impact * voice.wireLevel
            * (0.08f + voice.wireTension * 0.08f);
        const float wireMid = coloredWireMid * wireAmplitude + shellToWire;
        const float wireSide = coloredWireSide * wireAmplitude * 0.76f;

        voice.clickLow += (noiseClick - voice.clickLow)
            * voice.clickHighpassCoefficient;
        const float clickHigh = noiseClick - voice.clickLow;
        voice.clickBand += (clickHigh - voice.clickBand)
            * voice.clickLowpassCoefficient;
        voice.clickPhase += voice.clickFrequencyHz / sr;
        voice.clickPhase -= std::floor(voice.clickPhase);
        const float clickOscillator = std::cos(
            2.0f * kPi * voice.clickPhase);
        const float clickEnvelope = voice.clickEnvelope.process();
        const float click = (voice.clickBand * 0.64f
                + clickOscillator * 0.36f)
            * clickEnvelope * voice.clickLevel * voice.velocityGain
            * voice.velocityBrightness * 0.72f;

        mid += shell + impact + wireMid + click;
        side += wireSide;

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
        // A zero-width setting is a hard format contract, not an asymptote.
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
    DrumSnareParams params_ {};
    DrumCharacter character_ {};
    std::array<Voice, kVoiceCount> voices_ {};
    uint64_t triggerCounter_ = 0u;
    uint32_t activeVoiceCount_ = 0u;
    float smoothingCoefficient_ = 0.004157998f;
    float outputGainTarget_ = 0.5011872f;
    float smoothedOutputGain_ = 0.5011872f;
    float smoothedWidth_ = 0.0f;
};

} // namespace s3g
