#pragma once

#include "s3g_drum_character.h"
#include "s3g_drum_tom_voice.h"
#include "s3g_math.h"
#include "s3g_realtime.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace s3g {

// Members follow the standalone CLAP parameter order. The first sixteen are
// resolved and latched for every hit; character, width and gain remain live.
struct DrumFloorTomParams {
    float tuneHz = 72.0f;
    float noteTracking = 0.75f;
    float pitchDropSemitones = 8.0f;
    float pitchSweepMs = 45.0f;
    float shellSpread = 0.42f;
    float body = 0.70f;
    float ring = 0.42f;
    float bodyDecaySeconds = 0.70f;
    float punch = 0.72f;
    float damping = 0.35f;
    float rimLevel = 0.55f;
    float rimCharacter = 0.48f;
    float rimDecaySeconds = 0.070f;
    float stickLevel = 0.35f;
    float stickTone = 0.58f;
    float stickDecayMs = 4.5f;
    DrumCharacterParams character {};
    float stereoWidth = 0.0f;
    float velocitySensitivity = 0.90f;
    float outputGainDb = -6.0f;
};

inline DrumTomVoiceSettings drumFloorTomResolvedVoiceSettings(
    const DrumFloorTomParams& params, int midiNote = 43)
{
    midiNote = std::max(0, std::min(127, midiNote));
    const float noteTracking = clamp(
        drumFiniteOr(params.noteTracking, 0.75f), 0.0f, 1.0f);
    const float trackedSemitones = static_cast<float>(midiNote - 43)
        * noteTracking;
    DrumTomVoiceSettings settings;
    settings.frequencyHz = drumFiniteOr(params.tuneHz, 72.0f)
        * std::exp2(trackedSemitones / 12.0f);
    settings.pitchDropSemitones = params.pitchDropSemitones;
    settings.pitchSweepMs = params.pitchSweepMs;
    settings.shellSpread = params.shellSpread;
    settings.body = params.body;
    settings.ring = params.ring;
    settings.bodyDecaySeconds = params.bodyDecaySeconds;
    settings.punch = params.punch;
    settings.damping = params.damping;
    settings.rimLevel = params.rimLevel;
    settings.rimCharacter = params.rimCharacter;
    settings.rimDecaySeconds = params.rimDecaySeconds;
    settings.stickLevel = params.stickLevel;
    settings.stickTone = params.stickTone;
    settings.stickDecayMs = params.stickDecayMs;
    settings.stereoPosition = 0.0f;
    settings.velocitySensitivity = params.velocitySensitivity;
    settings.seedSalt = 0x464c4f52u; // "FLOR"
    return settings;
}

inline double drumFloorTomTailSeconds(const DrumFloorTomParams& params,
    double sampleRate = 48000.0)
{
    const DrumTomVoiceSettings settings = drumFloorTomResolvedVoiceSettings(
        params, 43);
    return std::min(40.0, std::max({
        drumTomVoiceTailSeconds(settings, DrumTomArticulation::Head,
            sampleRate),
        drumTomVoiceTailSeconds(settings, DrumTomArticulation::RimStick,
            sampleRate),
        params.character.drive > 1.0e-6f ? 0.50 : 0.0,
    }));
}

class DrumFloorTom {
public:
    static constexpr uint32_t kVoiceCount = 16u;

    void prepare(double sampleRate)
    {
        sampleRate_ = drumSafeSampleRate(sampleRate);
        voices_.prepare(sampleRate_);
        character_.prepare(sampleRate_);
        smoothingCoefficient_ = drumOnePoleTimeCoefficient(
            0.005f, static_cast<float>(sampleRate_));
        outputGainTarget_ = dbToGain(params_.outputGainDb);
        reset();
    }

    void reset()
    {
        voices_.reset();
        snapGlobalParameters();
        character_.reset();
    }

    void setParams(DrumFloorTomParams params)
    {
        const bool wasInactive = !active();
        params_ = sanitize(params);
        outputGainTarget_ = dbToGain(params_.outputGainDb);
        character_.setParams(params_.character);
        if (wasInactive) {
            snapGlobalParameters();
            character_.reset();
        }
    }

    DrumFloorTomParams params() const { return params_; }

    void trigger(DrumTomArticulation articulation, float velocity,
        int midiNote = 43)
    {
        midiNote = std::max(0, std::min(127, midiNote));
        // Accept the public octave-rim map directly as well as a wrapper's
        // already-normalized head-equivalent note.
        if (articulation == DrumTomArticulation::RimStick) {
            if (midiNote == 37) midiNote = 43;
            if (midiNote == 53 || midiNote == 55) midiNote -= 12;
        }
        const DrumTomVoiceSettings settings = drumFloorTomResolvedVoiceSettings(
            params_, midiNote);
        (void)voices_.trigger(settings, articulation, velocity);
    }

    void processFrame(float& left, float& right)
    {
        smoothGlobalParameters();
        float mid = 0.0f;
        float side = 0.0f;
        voices_.processFrame(mid, side);
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

    bool active() const { return voices_.active() || character_.active(); }

private:
    static DrumFloorTomParams sanitize(DrumFloorTomParams params)
    {
        params.tuneHz = clamp(drumFiniteOr(params.tuneHz, 72.0f),
            35.0f, 180.0f);
        params.noteTracking = clamp(
            drumFiniteOr(params.noteTracking, 0.75f), 0.0f, 1.0f);
        params.pitchDropSemitones = clamp(
            drumFiniteOr(params.pitchDropSemitones, 8.0f), -6.0f, 30.0f);
        params.pitchSweepMs = clamp(
            drumFiniteOr(params.pitchSweepMs, 45.0f), 1.0f, 300.0f);
        params.shellSpread = clamp(
            drumFiniteOr(params.shellSpread, 0.42f), 0.0f, 1.0f);
        params.body = clamp(drumFiniteOr(params.body, 0.70f), 0.0f, 1.0f);
        params.ring = clamp(drumFiniteOr(params.ring, 0.42f), 0.0f, 1.0f);
        params.bodyDecaySeconds = clamp(
            drumFiniteOr(params.bodyDecaySeconds, 0.70f), 0.04f, 4.0f);
        params.punch = clamp(
            drumFiniteOr(params.punch, 0.72f), 0.0f, 1.0f);
        params.damping = clamp(
            drumFiniteOr(params.damping, 0.35f), 0.0f, 1.0f);
        params.rimLevel = clamp(
            drumFiniteOr(params.rimLevel, 0.55f), 0.0f, 1.0f);
        params.rimCharacter = clamp(
            drumFiniteOr(params.rimCharacter, 0.48f), 0.0f, 1.0f);
        params.rimDecaySeconds = clamp(
            drumFiniteOr(params.rimDecaySeconds, 0.070f), 0.02f, 0.35f);
        params.stickLevel = clamp(
            drumFiniteOr(params.stickLevel, 0.35f), 0.0f, 1.0f);
        params.stickTone = clamp(
            drumFiniteOr(params.stickTone, 0.58f), 0.0f, 1.0f);
        params.stickDecayMs = clamp(
            drumFiniteOr(params.stickDecayMs, 4.5f), 0.5f, 40.0f);
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

    double sampleRate_ = 48000.0;
    DrumFloorTomParams params_ {};
    DrumTomVoiceBank<kVoiceCount> voices_ {};
    DrumCharacter character_ {};
    float smoothingCoefficient_ = 0.004f;
    float outputGainTarget_ = 0.501187f;
    float smoothedOutputGain_ = 0.501187f;
    float smoothedWidth_ = 0.0f;
};

} // namespace s3g
