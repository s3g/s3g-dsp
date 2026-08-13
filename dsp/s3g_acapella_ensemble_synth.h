#pragma once

#include "s3g_acapella_source_synth.h"
#include "s3g_acapella_resonator_bank.h"
#include "s3g_math.h"
#include "s3g_realtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace s3g {

constexpr uint32_t kAcapellaMaxPolyphony = 8u;

struct AcapellaEnsembleParams {
    uint32_t polyphony = 4u;
    float doubleAmount = 0.18f;
    float doubleDetuneCents = 7.0f;
    float doubleTimingMs = 18.0f;
    float doubleDirt = 0.0f;
    float doubleWidth = 0.82f;
};

struct AcapellaEnsembleNote {
    AcapellaSyllable syllable {};
    int32_t noteId = -1;
    int16_t channel = -1;
    int16_t key = -1;
};

struct AcapellaEnsembleFrame {
    float left = 0.0f;
    float right = 0.0f;
};

inline AcapellaEnsembleParams sanitizeAcapellaEnsembleParams(
    AcapellaEnsembleParams params)
{
    params.polyphony = std::max<uint32_t>(1u,
        std::min<uint32_t>(kAcapellaMaxPolyphony, params.polyphony));
    params.doubleAmount = clamp(acapellaFiniteOr(params.doubleAmount, 0.18f),
        0.0f, 1.0f);
    params.doubleDetuneCents = clamp(acapellaFiniteOr(
        params.doubleDetuneCents, 7.0f), 0.0f, 30.0f);
    params.doubleTimingMs = clamp(acapellaFiniteOr(
        params.doubleTimingMs, 18.0f), 0.0f, 45.0f);
    params.doubleDirt = clamp(acapellaFiniteOr(params.doubleDirt, 0.0f),
        0.0f, 1.0f);
    params.doubleWidth = clamp(acapellaFiniteOr(params.doubleWidth, 0.82f),
        0.0f, 1.0f);
    return params;
}

inline AcapellaEnsembleParams acapellaEnsemblePreset(
    AcapellaSourcePreset preset)
{
    AcapellaEnsembleParams params;
    switch (preset) {
    case AcapellaSourcePreset::RhythmicRap:
        params.doubleAmount = 0.24f;
        params.doubleDetuneCents = 6.0f;
        params.doubleTimingMs = 14.0f;
        params.doubleDirt = 0.08f;
        break;
    case AcapellaSourcePreset::AirySung:
        params.polyphony = 6u;
        params.doubleAmount = 0.30f;
        params.doubleDetuneCents = 9.0f;
        params.doubleTimingMs = 24.0f;
        params.doubleWidth = 0.94f;
        break;
    case AcapellaSourcePreset::PressedLead:
        params.doubleAmount = 0.34f;
        params.doubleDetuneCents = 8.0f;
        params.doubleTimingMs = 17.0f;
        params.doubleDirt = 0.18f;
        break;
    case AcapellaSourcePreset::HarshScream:
        params.doubleAmount = 0.50f;
        params.doubleDetuneCents = 13.0f;
        params.doubleTimingMs = 25.0f;
        params.doubleDirt = 0.52f;
        params.doubleWidth = 1.0f;
        break;
    case AcapellaSourcePreset::DeathGrowl:
        params.doubleAmount = 0.62f;
        params.doubleDetuneCents = 11.0f;
        params.doubleTimingMs = 31.0f;
        params.doubleDirt = 0.68f;
        params.doubleWidth = 1.0f;
        break;
    case AcapellaSourcePreset::NeutralSung:
    default:
        break;
    }
    return sanitizeAcapellaEnsembleParams(params);
}

// Fixed-capacity polyphony plus two independently synthesized, quieter double
// takes per note. No voice is cloned from buffered audio: each layer owns its
// own glottis, random sequence, tract filters, envelope, and consonant state.
class AcapellaEnsembleSynth {
public:
    void prepare(double sampleRate)
    {
        sampleRate_ = std::isfinite(sampleRate)
            ? clamp(static_cast<float>(sampleRate), 8000.0f, 192000.0f)
            : 48000.0f;
        sourceParams_ = sanitizeAcapellaSourceParams(sourceParams_);
        params_ = sanitizeAcapellaEnsembleParams(params_);
        smoothingCoefficient_ = timeCoefficient(20.0f);
        trackAttackCoefficient_ = timeCoefficient(0.7f);
        trackReleaseCoefficient_ = timeCoefficient(55.0f);
        trackGainAttackCoefficient_ = timeCoefficient(1.0f);
        trackGainReleaseCoefficient_ = timeCoefficient(70.0f);
        for (auto& voice : voices_) {
            voice.lead.prepare(sampleRate_);
            voice.doubleLeft.prepare(sampleRate_);
            voice.doubleRight.prepare(sampleRate_);
        }
        reset();
    }

    void reset()
    {
        ageCounter_ = 0u;
        textWordCursor_ = 0u;
        smoothedDoubleAmount_ = params_.doubleAmount;
        smoothedDoubleDirt_ = params_.doubleDirt;
        smoothedDoubleWidth_ = params_.doubleWidth;
        for (auto& voice : voices_) resetVoice(voice);
        updateVoiceParams();
    }

    void setSourceParams(AcapellaSourceParams params)
    {
        params = sanitizeAcapellaSourceParams(params);
        if (sameSourceParams(sourceParams_, params)) return;
        if (sourceParams_.gestureSequence != params.gestureSequence) {
            textWordCursor_ = 0u;
        }
        sourceParams_ = params;
        updateVoiceParams();
    }

    void setParams(AcapellaEnsembleParams params)
    {
        params_ = sanitizeAcapellaEnsembleParams(params);
    }

    void setTextGestureProgram(const AcapellaGestureProgram& program)
    {
        if (program.revision == textProgram_.revision
            && program.count == textProgram_.count) return;
        textProgram_ = program;
        textWordCursor_ = 0u;
        updateVoiceParams();
    }

    void setGestureTransport(double tempoBpm, double songBeat,
        bool tempoValid, bool beatValid, bool playing = true)
    {
        transportTempoBpm_ = tempoValid && std::isfinite(tempoBpm)
            ? std::clamp(tempoBpm, 20.0, 400.0) : 120.0;
        transportBeat_ = beatValid && std::isfinite(songBeat)
            ? songBeat : transportBeat_;
        transportTempoValid_ = tempoValid;
        transportBeatValid_ = beatValid;
        transportPlaying_ = playing;
        for (auto& voice : voices_) {
            setVoiceTransport(voice.lead);
            setVoiceTransport(voice.doubleLeft);
            setVoiceTransport(voice.doubleRight);
        }
    }

    const AcapellaEnsembleParams& params() const { return params_; }

    bool trigger(AcapellaEnsembleNote note)
    {
        Voice& voice = allocateVoice();
        const bool stealing = voiceActive(voice);
        const uint32_t slot = static_cast<uint32_t>(&voice - voices_.data());
        voice.noteId = note.noteId;
        voice.channel = note.channel;
        voice.key = note.key;
        voice.age = ++ageCounter_;
        voice.held = true;
        voice.pendingLeft = false;
        voice.pendingRight = false;
        if (!stealing) {
            voice.leadEnvelope = {};
            voice.leftEnvelope = {};
            voice.rightEnvelope = {};
        } else {
            voice.doubleLeft.release();
            voice.doubleRight.release();
        }

        const bool wordStepping = sourceParams_.gestureSequence
                == AcapellaGestureSequence::Text
            && textProgram_.wordCount > 0u;
        voice.textWordStep = wordStepping;
        voice.textWordIndex = wordStepping
            ? textWordCursor_ % textProgram_.wordCount : 0u;
        setVoiceTextProgram(voice);

        setVoiceTransport(voice.lead);
        if (!voice.lead.trigger(note.syllable)) return false;
        if (wordStepping) {
            textWordCursor_ = (voice.textWordIndex + 1u)
                % textProgram_.wordCount;
        }
        if (params_.doubleAmount > 1.0e-4f) {
            const float detuneRatio = std::exp2(
                params_.doubleDetuneCents / 1200.0f);
            voice.leftSyllable = note.syllable;
            voice.rightSyllable = note.syllable;
            voice.leftSyllable.frequencyHz /= detuneRatio;
            voice.rightSyllable.frequencyHz *= detuneRatio;
            const float slotSkew = static_cast<float>((slot * 37u) % 11u)
                * 0.018f - 0.09f;
            const float timingSamples = params_.doubleTimingMs * 0.001f
                * sampleRate_;
            voice.leftDelay = static_cast<uint32_t>(std::max(0.0f,
                timingSamples * (0.70f + slotSkew)));
            voice.rightDelay = static_cast<uint32_t>(std::max(0.0f,
                timingSamples * (1.18f - slotSkew)));
            voice.pendingLeft = true;
            voice.pendingRight = true;
        }
        return true;
    }

    void release(int32_t noteId, int16_t channel, int16_t key)
    {
        for (auto& voice : voices_) {
            if (!voiceActive(voice) || !matches(voice, noteId, channel, key)) {
                continue;
            }
            releaseVoice(voice);
        }
    }

    void releaseAll()
    {
        for (auto& voice : voices_) {
            if (voiceActive(voice)) releaseVoice(voice);
        }
    }

    bool active() const
    {
        for (const auto& voice : voices_) {
            if (voiceActive(voice)) return true;
        }
        return false;
    }

    uint32_t activeVoiceCount() const
    {
        uint32_t count = 0u;
        for (const auto& voice : voices_) {
            if (voiceActive(voice)) ++count;
        }
        return count;
    }

    AcapellaResonatorGesture resonatorGesture() const
    {
        const Voice* dominant = nullptr;
        for (const auto& voice : voices_) {
            if (!voiceActive(voice)
                || voice.lead.carrierNoteGain() <= 1.0e-5f) {
                continue;
            }
            if (!dominant || voice.age > dominant->age) dominant = &voice;
        }
        if (!dominant) return {};
        const auto& source = dominant->lead;
        AcapellaResonatorGesture gesture;
        gesture.phoneme = source.activePhoneme();
        gesture.frequencyHz = source.currentFrequencyHz();
        gesture.stepProgress = source.gestureStepProgress();
        gesture.stepIndex = source.gestureStepIndex();
        gesture.voiceInstance = dominant->age;
        gesture.stress = source.activePhonemeStress();
        gesture.flags = source.activePhonemeFlags();
        gesture.active = true;

        // The dominant voice supplies phoneme/word/rest metadata, while the
        // carrier follows every held MIDI voice independently of a text
        // articulation gate. This keeps oscillator phase and chord identity
        // intact through scored rests; the analysis envelope still closes the
        // vocoder audibly.
        uint32_t soundingVoices = 0u;
        for (const auto& voice : voices_) {
            if (voiceActive(voice)
                && voice.lead.carrierNoteGain() > 1.0e-5f) {
                ++soundingVoices;
            }
        }
        gesture.voiceCount = std::min<uint32_t>(
            soundingVoices, kAcapellaMaxPolyphony);
        const float chordNormalization = gesture.voiceCount > 0u
            ? 1.0f / std::sqrt(static_cast<float>(gesture.voiceCount))
            : 0.0f;
        uint32_t gestureVoice = 0u;
        for (const auto& voice : voices_) {
            if (!voiceActive(voice)
                || voice.lead.carrierNoteGain() <= 1.0e-5f
                || gestureVoice >= gesture.voiceCount) {
                continue;
            }
            gesture.voiceFrequencyHz[gestureVoice]
                = voice.lead.currentFrequencyHz();
            gesture.voiceGain[gestureVoice]
                = voice.lead.carrierNoteGain() * chordNormalization;
            gesture.voiceInstanceIds[gestureVoice] = voice.age;
            ++gestureVoice;
        }
        return gesture;
    }

    // Carrier-only gesture for an external microphone modulator. It retains
    // MIDI pitch, velocity, release, and stable voice IDs, but deliberately
    // excludes the internal text engine's rests and consonant metadata.
    AcapellaResonatorGesture midiCarrierGesture() const
    {
        const Voice* dominant = nullptr;
        for (const auto& voice : voices_) {
            if (!voiceActive(voice)
                || voice.lead.carrierNoteGain() <= 1.0e-5f) {
                continue;
            }
            if (!dominant || voice.age > dominant->age) dominant = &voice;
        }
        if (!dominant) return {};

        AcapellaResonatorGesture gesture;
        gesture.phoneme = AcapellaPhoneme::AX;
        gesture.carrierOnly = true;
        gesture.frequencyHz = dominant->lead.currentFrequencyHz();
        gesture.voiceInstance = dominant->age;
        gesture.active = true;

        uint32_t soundingVoices = 0u;
        for (const auto& voice : voices_) {
            if (voiceActive(voice)
                && voice.lead.carrierNoteGain() > 1.0e-5f) {
                ++soundingVoices;
            }
        }
        gesture.voiceCount = std::min<uint32_t>(
            soundingVoices, kAcapellaMaxPolyphony);
        const float chordNormalization = gesture.voiceCount > 0u
            ? 1.0f / std::sqrt(static_cast<float>(gesture.voiceCount))
            : 0.0f;
        uint32_t gestureVoice = 0u;
        for (const auto& voice : voices_) {
            if (!voiceActive(voice)
                || voice.lead.carrierNoteGain() <= 1.0e-5f
                || gestureVoice >= gesture.voiceCount) {
                continue;
            }
            gesture.voiceFrequencyHz[gestureVoice]
                = voice.lead.currentFrequencyHz();
            gesture.voiceGain[gestureVoice]
                = voice.lead.carrierNoteGain() * chordNormalization;
            gesture.voiceInstanceIds[gestureVoice] = voice.age;
            ++gestureVoice;
        }
        return gesture;
    }

    AcapellaEnsembleFrame processFrame()
    {
        smoothedDoubleAmount_ += (params_.doubleAmount
            - smoothedDoubleAmount_) * smoothingCoefficient_;
        smoothedDoubleDirt_ += (params_.doubleDirt
            - smoothedDoubleDirt_) * smoothingCoefficient_;
        smoothedDoubleWidth_ += (params_.doubleWidth
            - smoothedDoubleWidth_) * smoothingCoefficient_;

        float left = 0.0f;
        float right = 0.0f;
        uint32_t sounding = 0u;
        for (auto& voice : voices_) {
            if (!voiceActive(voice)) continue;
            ++sounding;
            startPendingDouble(voice.doubleLeft, voice.leftSyllable,
                voice.leftDelay, voice.pendingLeft, voice.held);
            startPendingDouble(voice.doubleRight, voice.rightSyllable,
                voice.rightDelay, voice.pendingRight, voice.held);

            const float lead = compressTrack(voice.lead.processFrame(),
                voice.leadEnvelope);
            float doubleLeft = compressTrack(voice.doubleLeft.processFrame(),
                voice.leftEnvelope);
            float doubleRight = compressTrack(voice.doubleRight.processFrame(),
                voice.rightEnvelope);
            doubleLeft = dirtyDouble(doubleLeft);
            doubleRight = dirtyDouble(doubleRight);

            const float doubleLevel = smoothedDoubleAmount_ * 0.58f;
            const float nearGain = 0.5f + 0.5f * smoothedDoubleWidth_;
            const float farGain = 0.5f - 0.5f * smoothedDoubleWidth_;
            left += lead * 0.78f + doubleLevel
                * (doubleLeft * nearGain + doubleRight * farGain);
            right += lead * 0.78f + doubleLevel
                * (doubleRight * nearGain + doubleLeft * farGain);

            if (!voiceActive(voice)) clearIdentity(voice);
        }
        advanceTransportClock();
        if (sounding == 0u) return {};
        const float normalization = 1.0f / std::sqrt(
            static_cast<float>(sounding));
        left = std::tanh(left * normalization * 1.15f) * 0.88f;
        right = std::tanh(right * normalization * 1.15f) * 0.88f;
        if (!std::isfinite(left) || !std::isfinite(right)) {
            reset();
            return {};
        }
        return { left, right };
    }

private:
    struct TrackEnvelope {
        float detector = 0.0f;
        float gain = 1.0f;
    };

    struct Voice {
        AcapellaSourceSynth lead {};
        AcapellaSourceSynth doubleLeft {};
        AcapellaSourceSynth doubleRight {};
        AcapellaSyllable leftSyllable {};
        AcapellaSyllable rightSyllable {};
        TrackEnvelope leadEnvelope {};
        TrackEnvelope leftEnvelope {};
        TrackEnvelope rightEnvelope {};
        int32_t noteId = -1;
        int16_t channel = -1;
        int16_t key = -1;
        uint64_t age = 0u;
        uint32_t leftDelay = 0u;
        uint32_t rightDelay = 0u;
        bool pendingLeft = false;
        bool pendingRight = false;
        bool held = false;
        uint32_t textWordIndex = 0u;
        bool textWordStep = false;
    };

    float timeCoefficient(float milliseconds) const
    {
        const float samples = std::max(1.0f,
            milliseconds * 0.001f * sampleRate_);
        return 1.0f - std::exp(-1.0f / samples);
    }

    static bool voiceActive(const Voice& voice)
    {
        return voice.lead.active() || voice.doubleLeft.active()
            || voice.doubleRight.active() || voice.pendingLeft
            || voice.pendingRight;
    }

    static bool sameSourceParams(const AcapellaSourceParams& a,
        const AcapellaSourceParams& b)
    {
        return a.delivery == b.delivery
            && a.voice.tractScale == b.voice.tractScale
            && a.voice.breath == b.voice.breath
            && a.voice.roughness == b.voice.roughness
            && a.voice.brightness == b.voice.brightness
            && a.voice.chest == b.voice.chest
            && a.voice.nasal == b.voice.nasal
            && a.voice.openQuotient == b.voice.openQuotient
            && a.voice.harshness == b.voice.harshness
            && a.voice.falseFold == b.voice.falseFold
            && a.voice.throat == b.voice.throat
            && a.articulation == b.articulation
            && a.consonantStrength == b.consonantStrength
            && a.intensity == b.intensity
            && a.vibratoRateHz == b.vibratoRateHz
            && a.vibratoDepthCents == b.vibratoDepthCents
            && a.pitchDriftCents == b.pitchDriftCents
            && a.glideMs == b.glideMs
            && a.attackMs == b.attackMs
            && a.releaseMs == b.releaseMs
            && a.hybridBlend == b.hybridBlend
            && a.onsetGuardMs == b.onsetGuardMs
            && a.waveguideBlend == b.waveguideBlend
            && a.coarticulation == b.coarticulation
            && a.intelligibility == b.intelligibility
            && a.gestureSequence == b.gestureSequence
            && a.gestureRateHz == b.gestureRateHz
            && a.gestureDepth == b.gestureDepth
            && a.gestureLoop == b.gestureLoop
            && a.gestureSync == b.gestureSync
            && a.gestureDivision == b.gestureDivision
            && a.retriggerMs == b.retriggerMs
            && a.onsetScoopSemitones == b.onsetScoopSemitones
            && a.rapDeclinationSemitones == b.rapDeclinationSemitones
            && a.randomSeed == b.randomSeed;
    }

    static void clearIdentity(Voice& voice)
    {
        voice.noteId = -1;
        voice.channel = -1;
        voice.key = -1;
        voice.held = false;
    }

    static bool matches(const Voice& voice, int32_t noteId,
        int16_t channel, int16_t key)
    {
        if (noteId >= 0 && voice.noteId >= 0) return voice.noteId == noteId;
        const bool channelMatches = channel < 0 || voice.channel < 0
            || voice.channel == channel;
        return channelMatches && (key < 0 || voice.key == key);
    }

    void resetVoice(Voice& voice)
    {
        voice.lead.reset();
        voice.doubleLeft.reset();
        voice.doubleRight.reset();
        voice.pendingLeft = false;
        voice.pendingRight = false;
        voice.leadEnvelope = {};
        voice.leftEnvelope = {};
        voice.rightEnvelope = {};
        voice.textWordIndex = 0u;
        voice.textWordStep = false;
        clearIdentity(voice);
    }

    void releaseVoice(Voice& voice)
    {
        voice.held = false;
        voice.pendingLeft = false;
        voice.pendingRight = false;
        voice.lead.release();
        voice.doubleLeft.release();
        voice.doubleRight.release();
    }

    Voice& allocateVoice()
    {
        const uint32_t limit = params_.polyphony;
        for (uint32_t index = 0u; index < limit; ++index) {
            if (!voiceActive(voices_[index])) return voices_[index];
        }
        uint32_t oldest = 0u;
        for (uint32_t index = 1u; index < limit; ++index) {
            if ((!voices_[index].held && voices_[oldest].held)
                || (voices_[index].held == voices_[oldest].held
                    && voices_[index].age < voices_[oldest].age)) {
                oldest = index;
            }
        }
        voices_[oldest].pendingLeft = false;
        voices_[oldest].pendingRight = false;
        return voices_[oldest];
    }

    void startPendingDouble(AcapellaSourceSynth& synth,
        const AcapellaSyllable& syllable, uint32_t& delay, bool& pending,
        bool held)
    {
        if (!pending) return;
        if (!held) {
            pending = false;
            return;
        }
        if (delay > 0u) {
            --delay;
            return;
        }
        setVoiceTransport(synth);
        synth.trigger(syllable);
        pending = false;
    }

    void setVoiceTransport(AcapellaSourceSynth& synth) const
    {
        synth.setGestureTransport(transportTempoBpm_, transportBeat_,
            transportTempoValid_, transportBeatValid_, transportPlaying_);
    }

    void advanceTransportClock()
    {
        if (transportTempoValid_ && transportPlaying_) {
            transportBeat_ += transportTempoBpm_
                / (60.0 * static_cast<double>(sampleRate_));
        }
    }

    float compressTrack(float sample, TrackEnvelope& state)
    {
        const float level = std::abs(sample);
        state.detector += (level - state.detector)
            * (level > state.detector ? trackAttackCoefficient_
                                      : trackReleaseCoefficient_);
        constexpr float threshold = 0.15f;
        const float desired = state.detector > threshold
            ? (threshold + (state.detector - threshold) / 7.0f)
                / state.detector
            : 1.0f;
        state.gain += (desired - state.gain)
            * (desired < state.gain ? trackGainAttackCoefficient_
                                    : trackGainReleaseCoefficient_);
        return sample * state.gain * 1.12f;
    }

    float dirtyDouble(float sample) const
    {
        const float driven = std::tanh(sample
            * (1.8f + smoothedDoubleDirt_ * 7.2f)) * 0.64f;
        return lerp(sample, driven, smoothedDoubleDirt_);
    }

    void updateVoiceParams()
    {
        for (uint32_t index = 0u; index < voices_.size(); ++index) {
            auto lead = sourceParams_;
            const bool wordSteppedText = lead.gestureSequence
                    == AcapellaGestureSequence::Text
                && textProgram_.wordCount > 0u;
            if (wordSteppedText) lead.gestureLoop = false;
            lead.randomSeed ^= 0x9e3779b9u * (index + 1u);
            auto left = sourceParams_;
            if (wordSteppedText) left.gestureLoop = false;
            left.randomSeed ^= 0x85ebca6bu * (index + 3u);
            left.voice.tractScale = clamp(left.voice.tractScale
                * (0.986f - static_cast<float>(index % 3u) * 0.002f),
                0.70f, 1.35f);
            left.voice.roughness = clamp(left.voice.roughness + 0.035f,
                0.0f, 1.0f);
            auto right = sourceParams_;
            if (wordSteppedText) right.gestureLoop = false;
            right.randomSeed ^= 0xc2b2ae35u * (index + 5u);
            right.voice.tractScale = clamp(right.voice.tractScale
                * (1.014f + static_cast<float>(index % 3u) * 0.002f),
                0.70f, 1.35f);
            right.voice.breath = clamp(right.voice.breath + 0.025f,
                0.0f, 1.0f);
            voices_[index].lead.setParams(lead);
            voices_[index].doubleLeft.setParams(left);
            voices_[index].doubleRight.setParams(right);
            setVoiceTextProgram(voices_[index]);
        }
    }

    void setVoiceTextProgram(Voice& voice)
    {
        AcapellaGestureProgram program = textProgram_;
        if (voice.textWordStep
            && sourceParams_.gestureSequence == AcapellaGestureSequence::Text
            && textProgram_.wordCount > 0u) {
            const uint32_t word = voice.textWordIndex
                % textProgram_.wordCount;
            if (!acapellaGestureWordProgram(textProgram_, word, program)) {
                voice.textWordStep = false;
                program = textProgram_;
            }
        }
        voice.lead.setTextGestureProgram(program);
        voice.doubleLeft.setTextGestureProgram(program);
        voice.doubleRight.setTextGestureProgram(program);
    }

    float sampleRate_ = 48000.0f;
    AcapellaSourceParams sourceParams_ {};
    AcapellaGestureProgram textProgram_ {};
    AcapellaEnsembleParams params_ {};
    std::array<Voice, kAcapellaMaxPolyphony> voices_ {};
    uint64_t ageCounter_ = 0u;
    uint32_t textWordCursor_ = 0u;
    float smoothingCoefficient_ = 0.001f;
    float trackAttackCoefficient_ = 0.02f;
    float trackReleaseCoefficient_ = 0.0004f;
    float trackGainAttackCoefficient_ = 0.02f;
    float trackGainReleaseCoefficient_ = 0.0003f;
    float smoothedDoubleAmount_ = 0.18f;
    float smoothedDoubleDirt_ = 0.0f;
    float smoothedDoubleWidth_ = 0.82f;
    double transportTempoBpm_ = 120.0;
    double transportBeat_ = 0.0;
    bool transportTempoValid_ = false;
    bool transportBeatValid_ = false;
    bool transportPlaying_ = false;
};

} // namespace s3g
