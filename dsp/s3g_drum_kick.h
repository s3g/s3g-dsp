#pragma once

#include "s3g_drum_character.h"
#include "s3g_math.h"
#include "s3g_realtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace s3g {

// Parameter vocabulary shared by the standalone CLAP and future tracker voice.
// The synthesis core owns no host state and performs no allocation while running.
struct DrumKickParams {
    float tuneHz = 48.0f;
    float noteTracking = 1.0f;
    float pitchDropSemitones = 24.0f;
    float pitchSweepMs = 45.0f;
    float pitchSettle = 0.25f;
    float body = 0.25f;
    float harmonics = 0.10f;
    float decaySeconds = 0.85f;
    float tail = 0.22f;
    float punch = 0.72f;
    float click = 0.16f;
    float clickTone = 0.55f;
    float clickDecayMs = 6.5f;
    float texture = 0.04f;
    float textureTone = 0.45f;
    float textureDecaySeconds = 0.12f;
    DrumCharacterParams character {};
    float stereoWidth = 0.0f;
    float velocitySensitivity = 0.90f;
    float outputGainDb = -6.0f;
};

class DrumKick {
public:
    static constexpr uint32_t kVoiceCount = 12u;

    void prepare(double sampleRate)
    {
        sampleRate_ = std::isfinite(sampleRate)
            ? std::max(8000.0, std::min(768000.0, sampleRate))
            : 48000.0;
        character_.prepare(sampleRate_);
        updateDerived();
        reset();
    }

    void reset()
    {
        voices_.fill({});
        triggerCounter_ = 0u;
        activeVoiceCount_ = 0u;
        snapSmoothedParameters();
        character_.reset();
    }

    void setParams(DrumKickParams params)
    {
        const bool wasInactive = activeVoiceCount_ == 0u
            && !character_.active();
        const DrumKickParams previous = params_;
        params_ = sanitize(params);
        character_.setParams(params_.character);

        if (params_.pitchSweepMs != previous.pitchSweepMs
            || params_.pitchSettle != previous.pitchSettle) {
            updatePitchEnvelopeDerived();
        }
        if (params_.decaySeconds != previous.decaySeconds
            || params_.tail != previous.tail) {
            updateBodyEnvelopeDerived();
        }
        if (params_.clickDecayMs != previous.clickDecayMs) {
            updateClickEnvelopeDerived();
        }
        if (params_.textureDecaySeconds != previous.textureDecaySeconds) {
            updateTextureEnvelopeDerived();
        }
        if (params_.punch != previous.punch) updateAttackDerived();
        if (params_.clickTone != previous.clickTone) updateClickToneDerived();
        if (params_.textureTone != previous.textureTone) {
            updateTextureToneDerived();
        }
        if (params_.outputGainDb != previous.outputGainDb) {
            outputGainTarget_ = dbToGain(params_.outputGainDb);
        }
        const bool lifetimeChanged = params_.decaySeconds
                != previous.decaySeconds
            || params_.tail != previous.tail
            || params_.clickDecayMs != previous.clickDecayMs
            || params_.textureDecaySeconds != previous.textureDecaySeconds;
        if (lifetimeChanged) {
            updateMaximumVoiceAge();
            for (Voice& voice : voices_) {
                if (voice.active) {
                    voice.maximumAgeSamples = std::max(
                        voice.maximumAgeSamples, maximumVoiceAgeSamples_);
                }
            }
        }
        if (wasInactive) {
            // Hosts may update a sleeping instrument without calling process().
            // Snap silent state so the next transient starts at the requested
            // settings; sounding voices retain the short live slew below.
            snapSmoothedParameters();
            character_.reset();
        }
    }

    DrumKickParams params() const { return params_; }

    void trigger(float velocity = 1.0f, int midiNote = 36)
    {
        velocity = finiteOr(velocity, 1.0f);
        velocity = clamp(velocity, 0.0f, 1.0f);
        midiNote = std::max(0, std::min(127, midiNote));

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
            const float activity = candidate.ampFast
                + candidate.ampSlow * params_.tail * 0.5f
                + candidate.clickEnvelope * params_.click * 0.15f
                + candidate.textureEnvelope * params_.texture * 0.1f;
            if (activity < quietest) {
                quietest = activity;
                selected = index;
            }
        }

        Voice& voice = voices_[selected];
        // Voice stealing is intentionally immediate: at drum rates the old
        // transient is already the quietest, while avoiding a hidden tail
        // allocator keeps this core suitable for tracker embedding.
        // Reused inactive voices also carry filter memories. Resetting the
        // complete slot makes every hit independent and reset-deterministic.
        voice = {};

        const float trackedSemitones = static_cast<float>(midiNote - 36)
            * params_.noteTracking;
        voice.active = true;
        voice.noteRatio = std::exp2(trackedSemitones / 12.0f);
        voice.velocityGain = lerp(1.0f, velocity, params_.velocitySensitivity);
        voice.velocityBrightness = lerp(1.0f, 0.35f + velocity * 0.65f,
            params_.velocitySensitivity);
        voice.phase = 0.0f;
        voice.auxiliaryPhase = 0.173f;
        voice.clickPhase = 0.0f;
        voice.pitchFast = 1.0f;
        voice.pitchSlow = 1.0f;
        voice.ampFast = 1.0f;
        voice.ampSlow = 1.0f;
        voice.clickEnvelope = 1.0f;
        voice.textureEnvelope = 1.0f;
        voice.attackEnvelope = 0.0f;
        voice.ageSamples = 0u;
        voice.maximumAgeSamples = maximumVoiceAgeSamples_;
        voice.randomState = seedFor(selected, ++triggerCounter_);

        if (!foundInactive) {
            // The stolen voice was already included in the count.
            activeVoiceCount_ = countActiveVoices();
        } else {
            ++activeVoiceCount_;
        }
    }

    void processFrame(float& left, float& right)
    {
        smoothGlobalParameters();

        float outputMid = 0.0f;
        float outputSide = 0.0f;
        uint32_t voicesRemaining = 0u;
        for (Voice& voice : voices_) {
            if (!voice.active) continue;
            processVoice(voice, outputMid, outputSide);
            if (voice.active) ++voicesRemaining;
        }
        activeVoiceCount_ = voicesRemaining;

        float frameLeft = outputMid + outputSide * smoothedWidth_;
        float frameRight = outputMid - outputSide * smoothedWidth_;

        character_.processFrame(frameLeft, frameRight);
        frameLeft *= smoothedOutputGain_;
        frameRight *= smoothedOutputGain_;

        left = safeOutput(frameLeft);
        right = safeOutput(frameRight);
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
    struct Voice {
        bool active = false;
        float noteRatio = 1.0f;
        float velocityGain = 1.0f;
        float velocityBrightness = 1.0f;
        float phase = 0.0f;
        float auxiliaryPhase = 0.0f;
        float clickPhase = 0.0f;
        float pitchFast = 0.0f;
        float pitchSlow = 0.0f;
        float ampFast = 0.0f;
        float ampSlow = 0.0f;
        float clickEnvelope = 0.0f;
        float textureEnvelope = 0.0f;
        float attackEnvelope = 0.0f;
        float clickNoiseLow = 0.0f;
        float clickNoiseSmooth = 0.0f;
        float textureMidLow = 0.0f;
        float textureMidDc = 0.0f;
        float textureSideLow = 0.0f;
        float textureSideDc = 0.0f;
        uint32_t randomState = 1u;
        uint32_t ageSamples = 0u;
        uint32_t maximumAgeSamples = 1u;
    };

    static float finiteOr(float value, float fallback)
    {
        return std::isfinite(value) ? value : fallback;
    }

    static DrumKickParams sanitize(DrumKickParams params)
    {
        params.tuneHz = clamp(finiteOr(params.tuneHz, 48.0f), 20.0f, 180.0f);
        params.noteTracking = clamp(finiteOr(params.noteTracking, 1.0f), 0.0f, 1.0f);
        params.pitchDropSemitones = clamp(
            finiteOr(params.pitchDropSemitones, 24.0f), -12.0f, 60.0f);
        params.pitchSweepMs = clamp(finiteOr(params.pitchSweepMs, 45.0f), 1.0f, 500.0f);
        params.pitchSettle = clamp(finiteOr(params.pitchSettle, 0.25f), 0.0f, 1.0f);
        params.body = clamp(finiteOr(params.body, 0.25f), 0.0f, 1.0f);
        params.harmonics = clamp(finiteOr(params.harmonics, 0.10f), 0.0f, 1.0f);
        params.decaySeconds = clamp(finiteOr(params.decaySeconds, 0.85f), 0.02f, 8.0f);
        params.tail = clamp(finiteOr(params.tail, 0.22f), 0.0f, 1.0f);
        params.punch = clamp(finiteOr(params.punch, 0.72f), 0.0f, 1.0f);
        params.click = clamp(finiteOr(params.click, 0.16f), 0.0f, 1.0f);
        params.clickTone = clamp(finiteOr(params.clickTone, 0.55f), 0.0f, 1.0f);
        params.clickDecayMs = clamp(finiteOr(params.clickDecayMs, 6.5f), 0.25f, 40.0f);
        params.texture = clamp(finiteOr(params.texture, 0.04f), 0.0f, 1.0f);
        params.textureTone = clamp(finiteOr(params.textureTone, 0.45f), 0.0f, 1.0f);
        params.textureDecaySeconds = clamp(
            finiteOr(params.textureDecaySeconds, 0.12f), 0.01f, 4.0f);
        params.character.drive = clamp(finiteOr(params.character.drive, 0.0f), 0.0f, 1.0f);
        params.character.bias = clamp(finiteOr(params.character.bias, 0.0f), -1.0f, 1.0f);
        params.character.compression = clamp(
            finiteOr(params.character.compression, 0.0f), 0.0f, 1.0f);
        params.character.sampleRateReduction = clamp(
            finiteOr(params.character.sampleRateReduction, 0.0f), 0.0f, 1.0f);
        params.character.bitDepthReduction = clamp(
            finiteOr(params.character.bitDepthReduction, 0.0f), 0.0f, 1.0f);
        params.character.reconstruction = clamp(
            finiteOr(params.character.reconstruction, 0.0f), 0.0f, 1.0f);
        params.character.tone = clamp(finiteOr(params.character.tone, 0.0f), -1.0f, 1.0f);
        params.stereoWidth = clamp(finiteOr(params.stereoWidth, 0.0f), 0.0f, 1.0f);
        params.velocitySensitivity = clamp(
            finiteOr(params.velocitySensitivity, 0.90f), 0.0f, 1.0f);
        params.outputGainDb = clamp(finiteOr(params.outputGainDb, -6.0f), -36.0f, 12.0f);
        return params;
    }

    static uint32_t seedFor(uint32_t voice, uint64_t trigger)
    {
        uint32_t seed = 0x9e3779b9u
            ^ static_cast<uint32_t>(trigger)
            ^ static_cast<uint32_t>(trigger >> 32u)
            ^ ((voice + 1u) * 0x85ebca6bu);
        seed ^= seed >> 16u;
        seed *= 0x7feb352du;
        seed ^= seed >> 15u;
        return seed != 0u ? seed : 1u;
    }

    static float randomBipolar(Voice& voice)
    {
        uint32_t state = voice.randomState;
        state ^= state << 13u;
        state ^= state >> 17u;
        state ^= state << 5u;
        voice.randomState = state;
        return static_cast<float>(state >> 8u) * (1.0f / 8388607.5f) - 1.0f;
    }

    static float decayMultiplier(float timeSeconds, float sampleRate,
        float terminal = 0.001f)
    {
        const float samples = std::max(1.0f, timeSeconds * sampleRate);
        return std::exp(std::log(terminal) / samples);
    }

    static float frequencyCoefficient(float frequencyHz, float sampleRate)
    {
        frequencyHz = clamp(frequencyHz, 2.0f, sampleRate * 0.45f);
        return 1.0f - std::exp(-2.0f * kPi * frequencyHz / sampleRate);
    }

    void updateDerived()
    {
        const float sr = static_cast<float>(sampleRate_);
        smoothingCoefficient_ = 1.0f
            - std::exp(-1.0f / std::max(1.0f, sr * 0.005f));

        updatePitchEnvelopeDerived();
        updateBodyEnvelopeDerived();
        updateClickEnvelopeDerived();
        updateTextureEnvelopeDerived();
        updateAttackDerived();
        updateClickToneDerived();
        updateTextureToneDerived();
        updateMaximumVoiceAge();
        outputGainTarget_ = dbToGain(params_.outputGainDb);
    }

    void updatePitchEnvelopeDerived()
    {
        const float sr = static_cast<float>(sampleRate_);

        const float pitchSeconds = params_.pitchSweepMs * 0.001f;
        pitchFastMultiplier_ = decayMultiplier(pitchSeconds, sr, 0.01f);
        const float settleScale = lerp(2.0f, 18.0f, params_.pitchSettle);
        pitchSlowMultiplier_ = decayMultiplier(
            pitchSeconds * settleScale, sr, 0.01f);
    }

    void updateBodyEnvelopeDerived()
    {
        const float sr = static_cast<float>(sampleRate_);
        ampFastMultiplier_ = decayMultiplier(params_.decaySeconds, sr);
        const float tailSeconds = params_.decaySeconds
            * lerp(1.15f, 4.0f, params_.tail);
        ampSlowMultiplier_ = decayMultiplier(tailSeconds, sr);
    }

    void updateClickEnvelopeDerived()
    {
        clickMultiplier_ = decayMultiplier(params_.clickDecayMs * 0.001f,
            static_cast<float>(sampleRate_));
    }

    void updateTextureEnvelopeDerived()
    {
        textureMultiplier_ = decayMultiplier(params_.textureDecaySeconds,
            static_cast<float>(sampleRate_));
    }

    void updateAttackDerived()
    {
        const float sr = static_cast<float>(sampleRate_);
        const float attackMs = lerp(3.2f, 0.08f, params_.punch);
        attackCoefficient_ = 1.0f - std::exp(
            -1.0f / std::max(1.0f, attackMs * 0.001f * sr));
    }

    void updateClickToneDerived()
    {
        const float sr = static_cast<float>(sampleRate_);
        const float clickCutoff = 350.0f
            * std::pow(15000.0f / 350.0f, params_.clickTone);
        clickHighpassCoefficient_ = frequencyCoefficient(
            std::min(2200.0f, clickCutoff * 0.28f), sr);
        clickSmoothCoefficient_ = frequencyCoefficient(clickCutoff, sr);
        clickFrequencyHz_ = 650.0f * std::pow(16.0f, params_.clickTone);
    }

    void updateTextureToneDerived()
    {
        const float sr = static_cast<float>(sampleRate_);
        const float textureCutoff = 180.0f
            * std::pow(15000.0f / 180.0f, params_.textureTone);
        textureLowpassCoefficient_ = frequencyCoefficient(textureCutoff, sr);
        textureDcCoefficient_ = frequencyCoefficient(
            lerp(55.0f, 850.0f, params_.textureTone), sr);
    }

    void updateMaximumVoiceAge()
    {
        const double bodySeconds = static_cast<double>(params_.decaySeconds)
            * static_cast<double>(lerp(1.15f, 4.0f, params_.tail)) * 1.75;
        const double clickSeconds = static_cast<double>(params_.clickDecayMs)
            * 0.001 * 1.75;
        const double textureSeconds =
            static_cast<double>(params_.textureDecaySeconds) * 1.75;
        const double maximumSeconds = std::min(40.0, std::max({
            2.0, bodySeconds, clickSeconds, textureSeconds }));
        maximumVoiceAgeSamples_ = static_cast<uint32_t>(std::min(
            maximumSeconds * sampleRate_,
            static_cast<double>(std::numeric_limits<uint32_t>::max())));
    }

    void snapSmoothedParameters()
    {
        smoothedTuneHz_ = params_.tuneHz;
        smoothedOutputGain_ = outputGainTarget_;
        smoothedWidth_ = params_.stereoWidth;
        smoothedPitchDrop_ = params_.pitchDropSemitones;
        smoothedPitchSettle_ = params_.pitchSettle;
        smoothedBody_ = params_.body;
        smoothedHarmonics_ = params_.harmonics;
        smoothedTail_ = params_.tail;
        smoothedPunch_ = params_.punch;
        smoothedClick_ = params_.click;
        smoothedTexture_ = params_.texture;
        smoothedTextureTone_ = params_.textureTone;
        smoothedClickFrequencyHz_ = clickFrequencyHz_;
    }

    void smoothGlobalParameters()
    {
        const auto smooth = [this](float& value, float target) {
            value = flushDenormal(value
                + (target - value) * smoothingCoefficient_);
        };
        smoothedTuneHz_ = flushDenormal(smoothedTuneHz_
            + (params_.tuneHz - smoothedTuneHz_) * smoothingCoefficient_);
        smoothedOutputGain_ = flushDenormal(smoothedOutputGain_
            + (outputGainTarget_ - smoothedOutputGain_)
                * smoothingCoefficient_);
        smoothedWidth_ = flushDenormal(smoothedWidth_
            + (params_.stereoWidth - smoothedWidth_) * smoothingCoefficient_);
        smooth(smoothedPitchDrop_, params_.pitchDropSemitones);
        smooth(smoothedPitchSettle_, params_.pitchSettle);
        smooth(smoothedBody_, params_.body);
        smooth(smoothedHarmonics_, params_.harmonics);
        smooth(smoothedTail_, params_.tail);
        smooth(smoothedPunch_, params_.punch);
        smooth(smoothedClick_, params_.click);
        smooth(smoothedTexture_, params_.texture);
        smooth(smoothedTextureTone_, params_.textureTone);
        smooth(smoothedClickFrequencyHz_, clickFrequencyHz_);
    }

    void processVoice(Voice& voice, float& mid, float& side)
    {
        const float sr = static_cast<float>(sampleRate_);
        const float settleShare = smoothedPitchSettle_ * 0.55f;
        const float pitchEnvelope = voice.pitchFast * (1.0f - settleShare)
            + voice.pitchSlow * settleShare;
        const float frequency = clamp(smoothedTuneHz_ * voice.noteRatio
                * std::exp2(smoothedPitchDrop_ * pitchEnvelope / 12.0f),
            15.0f, sr * 0.45f);

        const float phaseIncrement = frequency / sr;
        voice.phase += phaseIncrement;
        voice.phase -= std::floor(voice.phase);
        const float auxiliaryRatio = 1.43f + smoothedBody_ * 0.42f
            + pitchEnvelope * 0.18f;
        voice.auxiliaryPhase += std::min(
            phaseIncrement * auxiliaryRatio, 0.45f);
        voice.auxiliaryPhase -= std::floor(voice.auxiliaryPhase);

        const float auxiliary = std::sin(2.0f * kPi * voice.auxiliaryPhase);
        const float warpedPhase = voice.phase
            + auxiliary * smoothedBody_
                * (0.010f + smoothedHarmonics_ * 0.035f);
        const float fundamental = std::sin(2.0f * kPi * warpedPhase);
        const float second = std::sin(4.0f * kPi * voice.phase + 0.35f * auxiliary);
        const float third = std::sin(6.0f * kPi * voice.phase);
        const float harmonicAmount = smoothedHarmonics_
            * voice.velocityBrightness;
        const float secondBandlimit = clamp(
            (sr * 0.45f - frequency) / (sr * 0.23f), 0.0f, 1.0f);
        const float thirdBandlimit = clamp(
            (sr * 0.32f - frequency) / (sr * 0.18f), 0.0f, 1.0f);
        float bodySignal = fundamental
            + second * harmonicAmount * 0.34f * secondBandlimit
            + third * harmonicAmount * harmonicAmount * 0.15f
                * thirdBandlimit
            + auxiliary * smoothedBody_ * 0.11f
                * (0.25f + pitchEnvelope * 0.75f);

        voice.attackEnvelope += (1.0f - voice.attackEnvelope)
            * attackCoefficient_;
        const float tailShare = smoothedTail_ * 0.72f;
        const float bodyEnvelope = voice.attackEnvelope
            * (voice.ampFast * (1.0f - tailShare)
                + voice.ampSlow * tailShare);
        const float punchGain = 1.0f + smoothedPunch_
            * voice.pitchFast * 0.32f;
        bodySignal *= bodyEnvelope * voice.velocityGain * punchGain * 0.82f;

        const float noiseMid = randomBipolar(voice);
        const float noiseLeft = randomBipolar(voice);
        const float noiseRight = randomBipolar(voice);
        const float noiseSide = (noiseLeft - noiseRight) * 0.5f;

        voice.clickNoiseLow += (noiseMid - voice.clickNoiseLow)
            * clickHighpassCoefficient_;
        const float clickHigh = noiseMid - voice.clickNoiseLow;
        voice.clickNoiseSmooth += (clickHigh - voice.clickNoiseSmooth)
            * clickSmoothCoefficient_;
        const float clickFrequency = std::min(sr * 0.45f,
            smoothedClickFrequencyHz_
            * (0.72f + 0.28f * voice.velocityBrightness));
        voice.clickPhase += clickFrequency / sr;
        voice.clickPhase -= std::floor(voice.clickPhase);
        const float clickOscillator = std::cos(2.0f * kPi * voice.clickPhase);
        const float clickSignal = (clickOscillator * 0.46f
                + voice.clickNoiseSmooth * 0.78f)
            * voice.clickEnvelope * smoothedClick_
            * voice.velocityGain * voice.velocityBrightness;

        voice.textureMidLow += (noiseMid - voice.textureMidLow)
            * textureLowpassCoefficient_;
        voice.textureMidDc += (voice.textureMidLow - voice.textureMidDc)
            * textureDcCoefficient_;
        const float textureMid = voice.textureMidLow
            - voice.textureMidDc
                * lerp(0.25f, 0.92f, smoothedTextureTone_);

        voice.textureSideLow += (noiseSide - voice.textureSideLow)
            * textureLowpassCoefficient_;
        voice.textureSideDc += (voice.textureSideLow - voice.textureSideDc)
            * textureDcCoefficient_;
        const float textureSide = voice.textureSideLow - voice.textureSideDc;
        const float textureEnvelope = voice.textureEnvelope * smoothedTexture_
            * voice.velocityGain * voice.velocityBrightness;

        mid += bodySignal + clickSignal + textureMid * textureEnvelope * 0.56f;
        side += (textureSide * textureEnvelope * 0.72f
            + noiseSide * voice.clickEnvelope * smoothedClick_ * 0.10f
                * voice.velocityGain * voice.velocityBrightness);

        voice.pitchFast = flushDenormal(voice.pitchFast * pitchFastMultiplier_);
        voice.pitchSlow = flushDenormal(voice.pitchSlow * pitchSlowMultiplier_);
        voice.ampFast = flushDenormal(voice.ampFast * ampFastMultiplier_);
        voice.ampSlow = flushDenormal(voice.ampSlow * ampSlowMultiplier_);
        voice.clickEnvelope = flushDenormal(
            voice.clickEnvelope * clickMultiplier_);
        voice.textureEnvelope = flushDenormal(
            voice.textureEnvelope * textureMultiplier_);
        ++voice.ageSamples;

        const float remaining = voice.ampFast + voice.ampSlow * tailShare
            + voice.clickEnvelope * smoothedClick_
            + voice.textureEnvelope * smoothedTexture_;
        if (remaining < 1.0e-5f
            || voice.ageSamples >= voice.maximumAgeSamples) {
            voice.active = false;
        }
    }

    static float safeOutput(float value)
    {
        if (!std::isfinite(value)) return 0.0f;
        value = flushDenormal(value);
        const float magnitude = std::abs(value);
        if (magnitude <= 1.5f) return value;
        return std::copysign(1.5f
                + std::tanh((magnitude - 1.5f) * 0.65f) * 1.45f,
            value);
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
    DrumKickParams params_ {};
    DrumCharacter character_ {};
    std::array<Voice, kVoiceCount> voices_ {};
    uint64_t triggerCounter_ = 0u;
    uint32_t activeVoiceCount_ = 0u;
    uint32_t maximumVoiceAgeSamples_ = 96000u;

    float smoothedTuneHz_ = 48.0f;
    float smoothedOutputGain_ = 0.5011872f;
    float outputGainTarget_ = 0.5011872f;
    float smoothedWidth_ = 0.0f;
    // Voice-shaping controls use a short live slew. This is intentional for
    // CLAP automation and gives the future tracker a defined parameter-lock
    // policy without discontinuously rewriting an already sounding hit.
    float smoothedPitchDrop_ = 24.0f;
    float smoothedPitchSettle_ = 0.25f;
    float smoothedBody_ = 0.25f;
    float smoothedHarmonics_ = 0.10f;
    float smoothedTail_ = 0.22f;
    float smoothedPunch_ = 0.72f;
    float smoothedClick_ = 0.16f;
    float smoothedTexture_ = 0.04f;
    float smoothedTextureTone_ = 0.45f;
    float clickFrequencyHz_ = 2986.2f;
    float smoothedClickFrequencyHz_ = 2986.2f;
    float smoothingCoefficient_ = 0.004157998f;
    float pitchFastMultiplier_ = 0.99787f;
    float pitchSlowMultiplier_ = 0.99965f;
    float ampFastMultiplier_ = 0.99983f;
    float ampSlowMultiplier_ = 0.99994f;
    float clickMultiplier_ = 0.9781f;
    float textureMultiplier_ = 0.99880f;
    float attackCoefficient_ = 0.01f;
    float clickHighpassCoefficient_ = 0.1f;
    float clickSmoothCoefficient_ = 0.4f;
    float textureLowpassCoefficient_ = 0.2f;
    float textureDcCoefficient_ = 0.01f;
};

} // namespace s3g
