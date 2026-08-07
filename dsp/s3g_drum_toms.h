#pragma once

#include "s3g_drum_character.h"
#include "s3g_drum_tom_voice.h"
#include "s3g_math.h"
#include "s3g_realtime.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace s3g {

// Shared Low/Mid/High rack controls in standalone CLAP parameter order. The
// three explicit tune controls remain independently editable; safe RANDOM
// generates them as an ordered, correlated family.
struct DrumTomsParams {
    float lowTuneHz = 82.0f;
    float noteTracking = 0.75f;
    float midTuneHz = 127.0f;
    float highTuneHz = 182.0f;
    float pitchDropSemitones = 7.0f;
    float pitchSweepMs = 32.0f;
    float shellSpread = 0.40f;
    float body = 0.62f;
    float ring = 0.38f;
    float bodyDecaySeconds = 0.65f;
    float decaySpread = 0.25f;
    float punch = 0.74f;
    float rimLevel = 0.52f;
    float rimCharacter = 0.52f;
    float rimDecaySeconds = 0.065f;
    float stickTone = 0.62f;
    DrumCharacterParams character {};
    float stereoWidth = 0.0f;
    float velocitySensitivity = 0.90f;
    float outputGainDb = -6.0f;
};

inline int drumTomSlotCanonicalMidiNote(DrumTomSlot slot)
{
    switch (slot) {
    case DrumTomSlot::Low: return 45;
    case DrumTomSlot::Mid: return 48;
    case DrumTomSlot::High: return 50;
    }
    return 48;
}

inline DrumTomVoiceSettings drumTomsResolvedVoiceSettings(
    const DrumTomsParams& params, DrumTomSlot slot, int midiNote)
{
    midiNote = std::max(0, std::min(127, midiNote));
    const float tune = slot == DrumTomSlot::Low ? params.lowTuneHz
        : (slot == DrumTomSlot::Mid ? params.midTuneHz
            : params.highTuneHz);
    const int canonicalNote = drumTomSlotCanonicalMidiNote(slot);
    const float noteTracking = clamp(
        drumFiniteOr(params.noteTracking, 0.75f), 0.0f, 1.0f);
    const float trackedSemitones = static_cast<float>(
        midiNote - canonicalNote) * noteTracking;
    const float spreadPosition = slot == DrumTomSlot::Low ? 1.0f
        : (slot == DrumTomSlot::High ? -1.0f : 0.0f);
    const float decaySpread = clamp(
        drumFiniteOr(params.decaySpread, 0.25f), -1.0f, 1.0f);

    DrumTomVoiceSettings settings;
    settings.frequencyHz = drumFiniteOr(tune, 127.0f)
        * std::exp2(trackedSemitones / 12.0f);
    settings.pitchDropSemitones = params.pitchDropSemitones;
    settings.pitchSweepMs = params.pitchSweepMs;
    settings.shellSpread = params.shellSpread;
    settings.body = params.body;
    settings.ring = params.ring;
    settings.bodyDecaySeconds = params.bodyDecaySeconds
        * std::exp2(spreadPosition * decaySpread * 0.75f);
    settings.punch = params.punch;
    settings.damping = clamp(0.25f + static_cast<float>(
            static_cast<uint8_t>(slot)) * 0.075f
            + (1.0f - params.body) * 0.12f - params.ring * 0.07f,
        0.08f, 0.72f);
    settings.rimLevel = params.rimLevel;
    settings.rimCharacter = params.rimCharacter;
    settings.rimDecaySeconds = params.rimDecaySeconds
        * std::exp2(spreadPosition * decaySpread * 0.20f);
    settings.stickLevel = clamp(0.16f + params.rimLevel * 0.30f
            + params.punch * 0.22f,
        0.0f, 0.72f);
    settings.stickTone = params.stickTone;
    settings.stickDecayMs = lerp(8.0f, 2.0f,
        clamp(drumFiniteOr(params.stickTone, 0.62f), 0.0f, 1.0f));
    settings.stereoPosition = slot == DrumTomSlot::Low ? -0.58f
        : (slot == DrumTomSlot::High ? 0.58f : 0.0f);
    settings.velocitySensitivity = params.velocitySensitivity;
    settings.seedSalt = 0x544f4d30u
        + static_cast<uint32_t>(static_cast<uint8_t>(slot));
    return settings;
}

inline double drumTomsTailSeconds(const DrumTomsParams& params,
    double sampleRate = 48000.0)
{
    double longest = params.character.drive > 1.0e-6f ? 0.50 : 0.0;
    for (DrumTomSlot slot : {
            DrumTomSlot::Low, DrumTomSlot::Mid, DrumTomSlot::High }) {
        const DrumTomVoiceSettings settings = drumTomsResolvedVoiceSettings(
            params, slot, drumTomSlotCanonicalMidiNote(slot));
        longest = std::max(longest, drumTomVoiceTailSeconds(settings,
            DrumTomArticulation::Head, sampleRate));
        longest = std::max(longest, drumTomVoiceTailSeconds(settings,
            DrumTomArticulation::RimStick, sampleRate));
    }
    return std::min(40.0, longest);
}

class DrumToms {
public:
    static constexpr uint32_t kVoiceCount = 24u;

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

    void setParams(DrumTomsParams params)
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

    DrumTomsParams params() const { return params_; }

    void trigger(DrumTomSlot slot, DrumTomArticulation articulation,
        float velocity, int midiNote)
    {
        midiNote = std::max(0, std::min(127, midiNote));
        // Rim notes occupy the octave above their head equivalents. Accepting
        // that public map here keeps direct core users and normalized wrappers
        // pitch-identical.
        if (articulation == DrumTomArticulation::RimStick) {
            if (midiNote == 37) {
                midiNote = drumTomSlotCanonicalMidiNote(slot);
            } else if (midiNote == 57 || midiNote == 59
                || midiNote == 60 || midiNote == 62) {
                midiNote -= 12;
            }
        }
        const DrumTomVoiceSettings settings = drumTomsResolvedVoiceSettings(
            params_, slot, midiNote);
        (void)voices_.trigger(settings, articulation, velocity);
    }

    bool triggerMidi(float velocity, int midiNote)
    {
        DrumTomSlot slot = DrumTomSlot::Mid;
        DrumTomArticulation articulation = DrumTomArticulation::Head;
        int pitchNote = midiNote;
        switch (midiNote) {
        case 45: slot = DrumTomSlot::Low; break;
        case 47:
        case 48: slot = DrumTomSlot::Mid; break;
        case 50: slot = DrumTomSlot::High; break;
        case 37:
            slot = DrumTomSlot::Mid;
            articulation = DrumTomArticulation::RimStick;
            pitchNote = 48;
            break;
        case 57:
            slot = DrumTomSlot::Low;
            articulation = DrumTomArticulation::RimStick;
            pitchNote = 45;
            break;
        case 59:
            slot = DrumTomSlot::Mid;
            articulation = DrumTomArticulation::RimStick;
            pitchNote = 47;
            break;
        case 60:
            slot = DrumTomSlot::Mid;
            articulation = DrumTomArticulation::RimStick;
            pitchNote = 48;
            break;
        case 62:
            slot = DrumTomSlot::High;
            articulation = DrumTomArticulation::RimStick;
            pitchNote = 50;
            break;
        default: return false;
        }
        trigger(slot, articulation, velocity, pitchNote);
        return true;
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
    static DrumTomsParams sanitize(DrumTomsParams params)
    {
        params.lowTuneHz = clamp(
            drumFiniteOr(params.lowTuneHz, 82.0f), 40.0f, 180.0f);
        params.noteTracking = clamp(
            drumFiniteOr(params.noteTracking, 0.75f), 0.0f, 1.0f);
        params.midTuneHz = clamp(
            drumFiniteOr(params.midTuneHz, 127.0f), 55.0f, 280.0f);
        params.highTuneHz = clamp(
            drumFiniteOr(params.highTuneHz, 182.0f), 70.0f, 420.0f);
        params.pitchDropSemitones = clamp(
            drumFiniteOr(params.pitchDropSemitones, 7.0f), -6.0f, 30.0f);
        params.pitchSweepMs = clamp(
            drumFiniteOr(params.pitchSweepMs, 32.0f), 1.0f, 250.0f);
        params.shellSpread = clamp(
            drumFiniteOr(params.shellSpread, 0.40f), 0.0f, 1.0f);
        params.body = clamp(drumFiniteOr(params.body, 0.62f), 0.0f, 1.0f);
        params.ring = clamp(drumFiniteOr(params.ring, 0.38f), 0.0f, 1.0f);
        params.bodyDecaySeconds = clamp(
            drumFiniteOr(params.bodyDecaySeconds, 0.65f), 0.03f, 3.0f);
        params.decaySpread = clamp(
            drumFiniteOr(params.decaySpread, 0.25f), -1.0f, 1.0f);
        params.punch = clamp(
            drumFiniteOr(params.punch, 0.74f), 0.0f, 1.0f);
        params.rimLevel = clamp(
            drumFiniteOr(params.rimLevel, 0.52f), 0.0f, 1.0f);
        params.rimCharacter = clamp(
            drumFiniteOr(params.rimCharacter, 0.52f), 0.0f, 1.0f);
        params.rimDecaySeconds = clamp(
            drumFiniteOr(params.rimDecaySeconds, 0.065f), 0.02f, 0.30f);
        params.stickTone = clamp(
            drumFiniteOr(params.stickTone, 0.62f), 0.0f, 1.0f);
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
    DrumTomsParams params_ {};
    DrumTomVoiceBank<kVoiceCount> voices_ {};
    DrumCharacter character_ {};
    float smoothingCoefficient_ = 0.004f;
    float outputGainTarget_ = 0.501187f;
    float smoothedOutputGain_ = 0.501187f;
    float smoothedWidth_ = 0.0f;
};

} // namespace s3g
