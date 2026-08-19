#pragma once

#include "s3g_sample_asset.h"
#include "s3g_sample_playback.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace s3g::sample {

constexpr std::size_t kMaximumVoices = 32u;
constexpr std::size_t kMidiNoteCount = 128u;
constexpr double kLivePitchSmoothingSeconds = 0.010;
constexpr double kPitchModeTransitionSeconds = 0.010;
constexpr double kLiveSustainSmoothingSeconds = 0.010;
constexpr double kLiveLoopTransitionSeconds = 0.005;

enum class PlayMode : uint8_t {
    Forward = 0u,
    ForwardLoop,
    Reverse,
    ReverseLoop,
    ForwardPingPong,
    ReversePingPong,
};

enum class FilterType : uint8_t {
    Off = 0u,
    LowPass,
    BandPass,
    HighPass,
    Notch,
};

enum class EventKind : uint8_t {
    NoteOn = 0u,
    NoteOff,
    Choke,
};

struct RenderEvent {
    uint32_t frameOffset = 0u;
    EventKind kind = EventKind::NoteOn;
    uint64_t noteId = 0u;
    uint8_t key = 60u;
    float velocity = 1.0f;
    uint8_t midiChannel = 0u;
};

struct VoiceCursor {
    float sourcePositionNormalized = -1.0f;
    uint8_t key = 0u;
};

// Start, Length, Loop Start, and Loop End are normalized against the source.
// Length is measured from Start. Loop points are absolute source positions and
// are clipped into the active Start/Length window at trigger time.
struct PlayerSettings {
    PlayMode playMode = PlayMode::Forward;
    PitchMode pitchMode = PitchMode::Rate;
    SyncMode syncMode = SyncMode::Free;
    TriggerMode triggerMode = TriggerMode::Auto;
    RetriggerMode retriggerMode = RetriggerMode::Layer;
    VoiceMode voiceMode = VoiceMode::Poly;
    double sourceTempoBpm = 120.0;
    double hostTempoBpm = 120.0;
    double glideSeconds = 0.0;
    double start = 0.0;
    double length = 1.0;
    double loopStart = 0.0;
    double loopEnd = 1.0;
    // Fraction of the loop span overlapped at a forward/reverse wrap. The
    // runtime bound is half the resolved loop so the playable body remains
    // non-empty. Ping-pong modes reflect continuously and do not need it.
    double loopCrossfade = 0.02;
    float tuneSemitones = 0.0f;
    float fineTuneCents = 0.0f;
    uint8_t rootNote = 60u;
    // A, D, and R are fractions of the pitch-adjusted Start/Length duration.
    // Their sum is bounded to one so non-looping voices always fit the full
    // envelope inside the selected sample window.
    float attackProportion = 0.001f;
    float decayProportion = 0.0f;
    float sustain = 1.0f;
    float releaseProportion = 0.005f;
    float gainDecibels = -6.0f;
    float pan = 0.0f;
    float velocitySensitivity = 1.0f;
    FilterType filterType = FilterType::Off;
    float filterCutoffHz = 20000.0f;
    float filterResonance = 0.0f;
    // Bipolar six-octave modulation from the proportional amp envelope.
    float filterEnvelopeAmount = 0.0f;

    bool valid() const noexcept
    {
        return static_cast<uint8_t>(playMode)
                <= static_cast<uint8_t>(PlayMode::ReversePingPong)
            && static_cast<uint8_t>(pitchMode)
                <= static_cast<uint8_t>(PitchMode::RateBelowStretchAbove)
            && static_cast<uint8_t>(syncMode)
                <= static_cast<uint8_t>(SyncMode::Host)
            && static_cast<uint8_t>(triggerMode)
                <= static_cast<uint8_t>(TriggerMode::Toggle)
            && static_cast<uint8_t>(retriggerMode)
                <= static_cast<uint8_t>(RetriggerMode::Ignore)
            && static_cast<uint8_t>(voiceMode)
                <= static_cast<uint8_t>(VoiceMode::Legato)
            && std::isfinite(sourceTempoBpm)
            && sourceTempoBpm >= 20.0 && sourceTempoBpm <= 999.0
            && std::isfinite(hostTempoBpm)
            && hostTempoBpm >= 1.0 && hostTempoBpm <= 999.0
            && std::isfinite(glideSeconds)
            && glideSeconds >= 0.0 && glideSeconds <= 2.0
            && std::isfinite(start) && start >= 0.0 && start <= 1.0
            && std::isfinite(length) && length >= 0.0 && length <= 1.0
            && std::isfinite(loopStart) && loopStart >= 0.0
            && loopStart <= 1.0 && std::isfinite(loopEnd)
            && loopEnd >= 0.0 && loopEnd <= 1.0
            && std::isfinite(loopCrossfade) && loopCrossfade >= 0.0
            && loopCrossfade <= 0.5
            && std::isfinite(tuneSemitones)
            && tuneSemitones >= -60.0f && tuneSemitones <= 60.0f
            && std::isfinite(fineTuneCents)
            && fineTuneCents >= -100.0f && fineTuneCents <= 100.0f
            && rootNote < kMidiNoteCount
            && std::isfinite(attackProportion)
            && attackProportion >= 0.0f && attackProportion <= 1.0f
            && std::isfinite(decayProportion)
            && decayProportion >= 0.0f && decayProportion <= 1.0f
            && std::isfinite(sustain) && sustain >= 0.0f && sustain <= 1.0f
            && std::isfinite(releaseProportion)
            && releaseProportion >= 0.0f && releaseProportion <= 1.0f
            && attackProportion + decayProportion + releaseProportion
                <= 1.000001f
            && std::isfinite(gainDecibels)
            && gainDecibels >= -60.0f && gainDecibels <= 12.0f
            && std::isfinite(pan) && pan >= -1.0f && pan <= 1.0f
            && std::isfinite(velocitySensitivity)
            && velocitySensitivity >= 0.0f
            && velocitySensitivity <= 1.0f
            && static_cast<uint8_t>(filterType)
                <= static_cast<uint8_t>(FilterType::Notch)
            && std::isfinite(filterCutoffHz)
            && filterCutoffHz >= 20.0f && filterCutoffHz <= 20000.0f
            && std::isfinite(filterResonance)
            && filterResonance >= 0.0f && filterResonance <= 1.0f
            && std::isfinite(filterEnvelopeAmount)
            && filterEnvelopeAmount >= -1.0f
            && filterEnvelopeAmount <= 1.0f;
    }
};

class SamplePlayerEngine {
public:
    bool prepare(double sampleRate, uint32_t outputChannelCount) noexcept
    {
        if (!(sampleRate > 0.0) || !std::isfinite(sampleRate)
            || outputChannelCount == 0u
            || outputChannelCount > kMaximumAudioChannels) return false;
        sampleRate_ = sampleRate;
        outputChannelCount_ = outputChannelCount;
        prepared_ = true;
        reset();
        return true;
    }

    void unprepare() noexcept
    {
        reset();
        asset_ = nullptr;
        prepared_ = false;
    }

    void reset() noexcept
    {
        voices_ = {};
        heldNotes_ = {};
        ageCounter_ = 0u;
        heldNoteAgeCounter_ = 0u;
        voiceCursors_ = {};
        voiceCursorCount_ = 0u;
        outputPeak_ = 0.0f;
    }

    void killAll() noexcept { reset(); }

    bool setAsset(const SampleAsset* asset) noexcept
    {
        if (asset && !asset->valid()) return false;
        asset_ = asset;
        return true;
    }

    std::size_t activeVoiceCount() const noexcept
    {
        return static_cast<std::size_t>(std::count_if(voices_.begin(),
            voices_.end(), [](const Voice& voice) { return voice.active; }));
    }

    const std::array<VoiceCursor, kMaximumVoices>& voiceCursors()
        const noexcept { return voiceCursors_; }
    uint32_t voiceCursorCount() const noexcept { return voiceCursorCount_; }
    float outputPeak() const noexcept { return outputPeak_; }

    void render(const PlayerSettings& settings, const RenderEvent* events,
        std::size_t eventCount, float* const* outputs,
        uint32_t outputChannelCount, uint32_t frameCount) noexcept
    {
        if (!outputs || outputChannelCount == 0u
            || outputChannelCount > kMaximumAudioChannels) return;
        for (uint32_t channel = 0u; channel < outputChannelCount; ++channel) {
            if (!outputs[channel]) return;
            std::fill(outputs[channel], outputs[channel] + frameCount, 0.0f);
        }
        voiceCursorCount_ = 0u;
        outputPeak_ = 0.0f;
        if (!prepared_ || !asset_ || !asset_->valid() || !settings.valid()
            || outputChannelCount != outputChannelCount_
            || asset_->channelCount > outputChannelCount || frameCount == 0u)
            return;
        if (!events) eventCount = 0u;

        const double nextSyncRatio = tempoRatio(settings);
        for (auto& voice : voices_) {
            if (!voice.active) continue;
            voice.syncRatio = nextSyncRatio;
            if (settings.pitchMode != voice.pitchModeSelection)
                configurePitchMode(voice, settings.pitchMode,
                    voice.key, voice.rootNote);
            updateLivePitchTarget(voice, settings);
            refreshVoiceRates(voice);
            updateLiveEnvelopeTargets(voice, settings);
            updateLiveLoopGeometry(voice, settings);
        }

        std::size_t eventIndex = 0u;
        for (uint32_t frame = 0u; frame < frameCount; ++frame) {
            while (eventIndex < eventCount
                && events[eventIndex].frameOffset <= frame) {
                handleEvent(events[eventIndex++], settings);
            }
            voiceCursorCount_ = 0u;
            for (auto& voice : voices_) {
                if (!voice.active || !voice.asset) continue;
                const float envelope = voice.envelopeLevel
                    * boundaryFade(voice);
                const float level = voice.velocityLevel * envelope;
                const uint32_t sourceChannels = voice.asset->channelCount;
                const FilterCoefficients filter = makeFilterCoefficients(
                    voice, settings);
                const StretchFrame stretch = makeStretchFrame(voice);
                for (uint32_t channel = 0u; channel < outputChannelCount;
                     ++channel) {
                    uint32_t sourceChannel = channel;
                    float channelLevel = level;
                    if (outputChannelCount_ == 2u
                        && sourceChannels == 1u && channel < 2u) {
                        sourceChannel = 0u;
                    } else if (channel >= sourceChannels) {
                        continue;
                    }
                    const float source = voice.pitchMode == PitchMode::Stretch
                        ? stretchSample(voice,
                            voice.asset->channels[sourceChannel], stretch)
                        : loopCrossfadedSample(voice,
                            voice.asset->channels[sourceChannel]);
                    float value = processFilter(
                        voice.filterStates[channel], source, filter)
                        * channelLevel;
                    if (voice.playbackTransitionFramesRemaining != 0u) {
                        const uint32_t total = std::max(1u,
                            voice.playbackTransitionTotalFrames);
                        const float linear = total == 1u ? 1.0f
                            : static_cast<float>(total
                                - voice.playbackTransitionFramesRemaining)
                                / static_cast<float>(total - 1u);
                        const float phase = linear * linear
                            * (3.0f - 2.0f * linear);
                        value = voice.playbackTransitionFromSamples[channel]
                            + (value
                                - voice.playbackTransitionFromSamples[channel])
                                * phase;
                    }
                    outputs[channel][frame] += value;
                    voice.lastOutputSamples[channel] = value;
                }
                if (voice.playbackTransitionFramesRemaining != 0u)
                    --voice.playbackTransitionFramesRemaining;
                voice.hasRenderedSample = true;
                if (voiceCursorCount_ < voiceCursors_.size()) {
                    const double sourceFrameCount = std::max(1.0,
                        static_cast<double>(voice.asset->frameCount()));
                    voiceCursors_[voiceCursorCount_++] = {
                        static_cast<float>(std::clamp(
                            voice.position / sourceFrameCount, 0.0, 1.0)),
                        voice.key,
                    };
                }
                voice.position += voice.increment;
                advanceStretchPhase(voice);
                advanceGlide(voice);
                advanceLiveSustain(voice);
                advanceEnvelope(voice);
                advancePosition(voice);
            }
        }
        applyFinalOutput(settings, outputs, outputChannelCount, frameCount);
    }

private:
    struct Voice {
        enum class EnvelopeStage : uint8_t {
            Attack = 0u,
            Decay,
            Sustain,
            Release,
        };

        struct FilterState {
            double integrator1 = 0.0;
            double integrator2 = 0.0;
        };

        const SampleAsset* asset = nullptr;
        uint64_t noteId = 0u;
        uint64_t age = 0u;
        double position = 0.0;
        double increment = 1.0;
        uint32_t playStartFrame = 0u;
        uint32_t playEndFrame = 0u;
        uint32_t loopStartFrame = 0u;
        uint32_t loopEndFrame = 0u;
        double loopCrossfadeFrames = 0.0;
        double sourceRatio = 1.0;
        double syncRatio = 1.0;
        double pitchRatio = 1.0;
        double targetPitchRatio = 1.0;
        double glideMultiplier = 1.0;
        uint32_t glideFramesRemaining = 0u;
        double readIncrementMagnitude = 1.0;
        double stretchWindowSourceFrames = 0.0;
        double stretchWindowOutputFrames = 0.0;
        double stretchPhase = 0.0;
        double stretchPhaseStep = 0.0;
        uint32_t attackFrames = 0u;
        uint32_t decayFrames = 0u;
        uint32_t releaseFrames = 0u;
        uint32_t envelopeReferenceFrames = 1u;
        uint32_t envelopeFrame = 0u;
        uint8_t key = 60u;
        uint8_t rootNote = 60u;
        uint8_t midiChannel = 0u;
        PlayMode playMode = PlayMode::Forward;
        PitchMode pitchModeSelection = PitchMode::Rate;
        PitchMode pitchMode = PitchMode::Rate;
        EnvelopeStage envelopeStage = EnvelopeStage::Sustain;
        float velocityLevel = 0.0f;
        float envelopeLevel = 1.0f;
        float releaseStartLevel = 0.0f;
        float sustainLevel = 1.0f;
        float targetSustainLevel = 1.0f;
        float sustainStep = 0.0f;
        uint32_t sustainFramesRemaining = 0u;
        std::array<FilterState, kMaximumAudioChannels> filterStates {};
        std::array<float, kMaximumAudioChannels> lastOutputSamples {};
        std::array<float, kMaximumAudioChannels>
            playbackTransitionFromSamples {};
        uint32_t playbackTransitionFramesRemaining = 0u;
        uint32_t playbackTransitionTotalFrames = 0u;
        bool active = false;
        bool hasLooped = false;
        bool hasRenderedSample = false;
    };

    struct HeldNote {
        uint64_t noteId = 0u;
        uint64_t age = 0u;
        uint8_t key = 60u;
        uint8_t midiChannel = 0u;
        float velocity = 1.0f;
        bool active = false;
    };

    struct FilterCoefficients {
        FilterType type = FilterType::Off;
        double a1 = 0.0;
        double a2 = 0.0;
        double a3 = 0.0;
        double damping = 0.0;
    };

    struct StretchFrame {
        double firstPosition = 0.0;
        double secondPosition = 0.0;
        float firstWeight = 1.0f;
        float secondWeight = 0.0f;
        float normalization = 1.0f;
        bool active = false;
    };

    struct LoopGeometry {
        uint32_t start = 0u;
        uint32_t end = 1u;
        double crossfadeFrames = 0.0;
    };

    static bool isReverse(PlayMode mode) noexcept
    {
        return mode == PlayMode::Reverse || mode == PlayMode::ReverseLoop
            || mode == PlayMode::ReversePingPong;
    }

    static bool isLooping(PlayMode mode) noexcept
    {
        return mode == PlayMode::ForwardLoop
            || mode == PlayMode::ReverseLoop
            || mode == PlayMode::ForwardPingPong
            || mode == PlayMode::ReversePingPong;
    }

    static bool isPingPong(PlayMode mode) noexcept
    {
        return mode == PlayMode::ForwardPingPong
            || mode == PlayMode::ReversePingPong;
    }

    static uint32_t proportionalFrames(float proportion,
        uint32_t totalFrames) noexcept
    {
        if (!(proportion > 0.0f) || totalFrames == 0u) return 0u;
        return static_cast<uint32_t>(std::clamp<double>(std::round(
            static_cast<double>(proportion) * totalFrames), 1.0,
            totalFrames));
    }

    static uint32_t normalizedFrame(double normalized,
        uint32_t frameCount) noexcept
    {
        if (frameCount == 0u) return 0u;
        return static_cast<uint32_t>(std::clamp<double>(std::floor(
            std::clamp(normalized, 0.0, 1.0)
                * static_cast<double>(frameCount)),
            0.0, static_cast<double>(frameCount)));
    }

    static double tempoRatio(const PlayerSettings& settings) noexcept
    {
        return settings.syncMode == SyncMode::Host
            ? std::clamp(settings.hostTempoBpm / settings.sourceTempoBpm,
                0.01, 32.0)
            : 1.0;
    }

    static double pitchRatioFor(uint8_t key, uint8_t rootNote,
        const PlayerSettings& settings) noexcept
    {
        const float semitones = static_cast<float>(
            static_cast<int>(key) - static_cast<int>(rootNote))
            + settings.tuneSemitones + settings.fineTuneCents * 0.01f;
        return std::pow(2.0, static_cast<double>(semitones) / 12.0);
    }

    static PitchMode resolvePitchModeForKey(PitchMode selection,
        uint8_t key, uint8_t rootNote) noexcept
    {
        if (selection != PitchMode::RateBelowStretchAbove)
            return selection;
        return key < rootNote ? PitchMode::Rate : PitchMode::Stretch;
    }

    void beginPlaybackTransition(Voice& voice, double seconds) const noexcept
    {
        if (!voice.hasRenderedSample) return;
        voice.playbackTransitionFromSamples = voice.lastOutputSamples;
        voice.playbackTransitionTotalFrames = static_cast<uint32_t>(
            std::max(16.0, std::round(sampleRate_ * seconds)));
        voice.playbackTransitionFramesRemaining
            = voice.playbackTransitionTotalFrames;
    }

    void configurePitchMode(Voice& voice, PitchMode selection,
        uint8_t key, uint8_t rootNote) const noexcept
    {
        voice.pitchModeSelection = selection;
        const PitchMode resolved = resolvePitchModeForKey(
            selection, key, rootNote);
        if (resolved == voice.pitchMode
            && (resolved != PitchMode::Stretch
                || voice.stretchWindowOutputFrames > 0.0)) {
            refreshVoiceRates(voice);
            return;
        }
        beginPlaybackTransition(voice, kPitchModeTransitionSeconds);
        voice.pitchMode = resolved;
        voice.stretchPhase = 0.0;
        if (resolved == PitchMode::Stretch) {
            constexpr double stretchWindowSeconds = 0.080;
            voice.stretchWindowOutputFrames = std::max(8.0,
                sampleRate_ * stretchWindowSeconds);
            voice.stretchWindowSourceFrames
                = voice.stretchWindowOutputFrames * voice.sourceRatio;
        } else {
            voice.stretchWindowOutputFrames = 0.0;
            voice.stretchWindowSourceFrames = 0.0;
            voice.stretchPhaseStep = 0.0;
        }
        refreshVoiceRates(voice);
    }

    void setPitchTarget(Voice& voice, double target,
        uint32_t transitionFrames) const noexcept
    {
        voice.targetPitchRatio = target;
        if (transitionFrames == 0u || !(voice.pitchRatio > 0.0)
            || !(target > 0.0)) {
            voice.pitchRatio = target;
            voice.glideFramesRemaining = 0u;
            voice.glideMultiplier = 1.0;
            refreshVoiceRates(voice);
            return;
        }
        voice.glideFramesRemaining = transitionFrames;
        voice.glideMultiplier = std::pow(target / voice.pitchRatio,
            1.0 / static_cast<double>(transitionFrames));
    }

    void updateLivePitchTarget(Voice& voice,
        const PlayerSettings& settings) const noexcept
    {
        const double target = pitchRatioFor(
            voice.key, voice.rootNote, settings);
        if (std::abs(target - voice.targetPitchRatio)
            <= std::max(1.0, target) * 1.0e-12) return;
        const uint32_t smoothingFrames = voice.glideFramesRemaining != 0u
            ? voice.glideFramesRemaining
            : static_cast<uint32_t>(std::max(1.0,
                std::round(sampleRate_ * kLivePitchSmoothingSeconds)));
        setPitchTarget(voice, target, smoothingFrames);
    }

    LoopGeometry resolveLoopGeometry(const Voice& voice,
        const PlayerSettings& settings) const noexcept
    {
        LoopGeometry geometry;
        if (!voice.asset || voice.playEndFrame <= voice.playStartFrame)
            return geometry;
        const uint32_t frames = voice.asset->frameCount();
        geometry.start = std::clamp(normalizedFrame(settings.loopStart,
            frames), voice.playStartFrame, voice.playEndFrame - 1u);
        geometry.end = std::clamp(normalizedFrame(settings.loopEnd, frames),
            geometry.start + 1u, voice.playEndFrame);
        if (geometry.end <= geometry.start) {
            geometry.start = voice.playStartFrame;
            geometry.end = voice.playEndFrame;
        }
        const double loopLength = static_cast<double>(
            geometry.end - geometry.start);
        if (!isPingPong(voice.playMode) && isLooping(voice.playMode)
            && settings.loopCrossfade > 0.0 && loopLength >= 2.0) {
            const double targetReadIncrement = voice.sourceRatio
                * (voice.pitchMode == PitchMode::Rate
                    ? voice.syncRatio * voice.targetPitchRatio
                    : std::max(voice.targetPitchRatio, voice.syncRatio));
            const double maximum = std::max(0.0,
                (loopLength - targetReadIncrement) * 0.5);
            if (maximum >= 1.0) {
                const double pitchSafeMinimum = std::min(maximum,
                    std::max(1.0, targetReadIncrement * 2.0));
                geometry.crossfadeFrames = std::clamp(
                    loopLength * settings.loopCrossfade,
                    pitchSafeMinimum, maximum);
            }
        }
        return geometry;
    }

    void updateLiveLoopGeometry(Voice& voice,
        const PlayerSettings& settings) const noexcept
    {
        if (!isLooping(voice.playMode)) return;
        const LoopGeometry geometry = resolveLoopGeometry(voice, settings);
        if (geometry.start == voice.loopStartFrame
            && geometry.end == voice.loopEndFrame
            && std::abs(geometry.crossfadeFrames
                - voice.loopCrossfadeFrames) <= 1.0e-9) return;
        beginPlaybackTransition(voice, kLiveLoopTransitionSeconds);
        voice.loopStartFrame = geometry.start;
        voice.loopEndFrame = geometry.end;
        voice.loopCrossfadeFrames = geometry.crossfadeFrames;
        if (!voice.hasLooped) return;
        const double start = static_cast<double>(geometry.start);
        const double end = static_cast<double>(geometry.end);
        if (isPingPong(voice.playMode)) {
            if (voice.position < start || voice.position >= end)
                voice.position = reflectPosition(voice.position, start, end);
        } else if (voice.position < start || voice.position >= end) {
            voice.position = wrapPosition(voice.position, start, end);
        }
    }

    uint32_t currentOutputLengthFrames(const Voice& voice) const noexcept
    {
        const double outputLength = std::ceil(static_cast<double>(
            voice.playEndFrame - voice.playStartFrame)
            / std::max(std::abs(voice.increment),
                std::numeric_limits<double>::min()));
        return static_cast<uint32_t>(std::clamp<double>(outputLength,
            1.0, std::numeric_limits<uint32_t>::max()));
    }

    void updateLiveEnvelopeTargets(Voice& voice,
        const PlayerSettings& settings) const noexcept
    {
        if (std::abs(settings.sustain - voice.targetSustainLevel)
            > 1.0e-7f) {
            voice.targetSustainLevel = settings.sustain;
            voice.sustainFramesRemaining = static_cast<uint32_t>(
                std::max(1.0, std::round(sampleRate_
                    * kLiveSustainSmoothingSeconds)));
            voice.sustainStep = (voice.targetSustainLevel
                - voice.sustainLevel)
                / static_cast<float>(voice.sustainFramesRemaining);
        }
        if (voice.envelopeStage != Voice::EnvelopeStage::Release) {
            voice.envelopeReferenceFrames
                = currentOutputLengthFrames(voice);
            voice.releaseFrames = proportionalFrames(
                settings.releaseProportion,
                voice.envelopeReferenceFrames);
        }
    }

    static void advanceLiveSustain(Voice& voice) noexcept
    {
        if (voice.sustainFramesRemaining == 0u) return;
        --voice.sustainFramesRemaining;
        if (voice.sustainFramesRemaining == 0u)
            voice.sustainLevel = voice.targetSustainLevel;
        else voice.sustainLevel += voice.sustainStep;
    }

    static void refreshVoiceRates(Voice& voice) noexcept
    {
        const double direction = voice.increment < 0.0 ? -1.0 : 1.0;
        const double transportRatio = voice.sourceRatio * voice.syncRatio;
        if (voice.pitchMode == PitchMode::Rate) {
            voice.increment = direction * transportRatio * voice.pitchRatio;
            voice.readIncrementMagnitude = std::abs(voice.increment);
            voice.stretchPhaseStep = 0.0;
        } else {
            voice.increment = direction * transportRatio;
            voice.readIncrementMagnitude = voice.sourceRatio
                * std::max(voice.pitchRatio, voice.syncRatio);
            voice.stretchPhaseStep = voice.stretchWindowOutputFrames > 0.0
                ? std::abs(voice.pitchRatio - voice.syncRatio)
                    / voice.stretchWindowOutputFrames
                : 0.0;
        }
    }

    static void advanceGlide(Voice& voice) noexcept
    {
        if (voice.glideFramesRemaining == 0u) return;
        --voice.glideFramesRemaining;
        if (voice.glideFramesRemaining == 0u)
            voice.pitchRatio = voice.targetPitchRatio;
        else voice.pitchRatio *= voice.glideMultiplier;
        refreshVoiceRates(voice);
    }

    static bool eventMatchesVoice(const RenderEvent& event,
        const Voice& voice) noexcept
    {
        if (event.noteId != 0u) return voice.noteId == event.noteId;
        return voice.key == event.key
            && (event.midiChannel == 0u
                || voice.midiChannel == event.midiChannel);
    }

    static bool eventRetriggersVoice(const RenderEvent& event,
        const Voice& voice) noexcept
    {
        return voice.key == event.key
            && (event.midiChannel == 0u
                || voice.midiChannel == event.midiChannel);
    }

    static bool eventMatchesHeldNote(const RenderEvent& event,
        const HeldNote& note) noexcept
    {
        if (event.noteId != 0u) return note.noteId == event.noteId;
        return note.key == event.key
            && (event.midiChannel == 0u
                || note.midiChannel == event.midiChannel);
    }

    void holdNote(const RenderEvent& event) noexcept
    {
        HeldNote* slot = nullptr;
        for (auto& note : heldNotes_) {
            if (!note.active) {
                slot = &note;
                break;
            }
        }
        if (!slot) {
            slot = &*std::min_element(heldNotes_.begin(), heldNotes_.end(),
                [](const HeldNote& left, const HeldNote& right) {
                    return left.age < right.age;
                });
        }
        *slot = { event.noteId, ++heldNoteAgeCounter_, event.key,
            event.midiChannel, event.velocity, true };
    }

    void releaseHeldNote(const RenderEvent& event) noexcept
    {
        for (auto& note : heldNotes_) {
            if (note.active && eventMatchesHeldNote(event, note))
                note.active = false;
        }
    }

    const HeldNote* newestHeldNote() const noexcept
    {
        const HeldNote* newest = nullptr;
        for (const auto& note : heldNotes_) {
            if (note.active && (!newest || note.age > newest->age))
                newest = &note;
        }
        return newest;
    }

    void handleEvent(const RenderEvent& event,
        const PlayerSettings& settings) noexcept
    {
        switch (event.kind) {
        case EventKind::NoteOn:
            holdNote(event);
            handleNoteOn(event, settings);
            break;
        case EventKind::NoteOff:
            releaseHeldNote(event);
            handleNoteOff(event, settings);
            break;
        case EventKind::Choke:
            releaseHeldNote(event);
            releaseMatching(event, true);
            break;
        }
    }

    void handleNoteOn(const RenderEvent& event,
        const PlayerSettings& settings) noexcept
    {
        bool matchingVoice = false;
        bool toggledVoice = false;
        for (auto& voice : voices_) {
            if (!voice.active || !eventRetriggersVoice(event, voice))
                continue;
            matchingVoice = true;
            if (settings.triggerMode == TriggerMode::Toggle
                && voice.envelopeStage != Voice::EnvelopeStage::Release) {
                beginRelease(voice, voice.releaseFrames);
                toggledVoice = true;
            }
        }
        if (settings.triggerMode == TriggerMode::Toggle && toggledVoice)
            return;
        if (matchingVoice
            && settings.retriggerMode == RetriggerMode::Ignore) return;
        if (matchingVoice
            && settings.retriggerMode == RetriggerMode::Restart) {
            for (auto& voice : voices_) {
                if (voice.active && eventRetriggersVoice(event, voice))
                    voice.active = false;
            }
        }

        if (settings.voiceMode == VoiceMode::Legato) {
            Voice* current = newestActiveVoice();
            if (current
                && current->envelopeStage != Voice::EnvelopeStage::Release
                && !(matchingVoice
                    && settings.retriggerMode == RetriggerMode::Restart)) {
                for (auto& voice : voices_) {
                    if (&voice != current) voice.active = false;
                }
                retargetVoice(*current, event, settings);
                return;
            }
        }
        if (settings.voiceMode != VoiceMode::Poly) {
            for (auto& voice : voices_) voice.active = false;
        }
        startVoice(event, settings);
    }

    void handleNoteOff(const RenderEvent& event,
        const PlayerSettings& settings) noexcept
    {
        if (settings.triggerMode == TriggerMode::OneShot
            || settings.triggerMode == TriggerMode::Toggle) return;
        Voice* current = nullptr;
        for (auto& voice : voices_) {
            if (voice.active && eventMatchesVoice(event, voice)
                && (!current || voice.age > current->age)) current = &voice;
        }
        if (!current) return;
        const bool shouldRelease = settings.triggerMode == TriggerMode::Gate
            || (settings.triggerMode == TriggerMode::Auto
                && isLooping(current->playMode));
        if (!shouldRelease) return;

        if (settings.voiceMode != VoiceMode::Poly) {
            if (const HeldNote* fallback = newestHeldNote()) {
                const RenderEvent fallbackEvent { event.frameOffset,
                    EventKind::NoteOn, fallback->noteId, fallback->key,
                    fallback->velocity, fallback->midiChannel };
                if (settings.voiceMode == VoiceMode::Legato) {
                    retargetVoice(*current, fallbackEvent, settings);
                    return;
                }
                for (auto& voice : voices_) voice.active = false;
                startVoice(fallbackEvent, settings);
                return;
            }
        }
        releaseMatching(event, false, settings.triggerMode);
    }

    Voice* newestActiveVoice() noexcept
    {
        Voice* newest = nullptr;
        for (auto& voice : voices_) {
            if (voice.active && (!newest || voice.age > newest->age))
                newest = &voice;
        }
        return newest;
    }

    Voice* voiceToStart() noexcept
    {
        for (auto& voice : voices_) if (!voice.active) return &voice;
        return &*std::min_element(voices_.begin(), voices_.end(),
            [](const Voice& left, const Voice& right) {
                return left.age < right.age;
            });
    }

    void startVoice(const RenderEvent& event,
        const PlayerSettings& settings) noexcept
    {
        if (!asset_ || !asset_->valid()) return;
        const uint32_t frames = asset_->frameCount();
        if (frames == 0u) return;
        const uint32_t start = std::min(normalizedFrame(settings.start,
            frames), frames - 1u);
        const uint32_t requestedLength = std::max(1u,
            normalizedFrame(settings.length, frames));
        const uint32_t end = std::min(frames, start + std::min(
            requestedLength, frames - start));
        if (end <= start) return;

        Voice& voice = *voiceToStart();
        voice = {};
        voice.asset = asset_;
        voice.noteId = event.noteId;
        voice.age = ++ageCounter_;
        voice.key = event.key;
        voice.rootNote = settings.rootNote;
        voice.midiChannel = event.midiChannel;
        voice.playMode = settings.playMode;
        voice.playStartFrame = start;
        voice.playEndFrame = end;
        const bool reverse = isReverse(settings.playMode);
        voice.position = reverse ? static_cast<double>(end - 1u)
                                 : static_cast<double>(start);
        const double sourceRatio = asset_->sampleRate / sampleRate_;
        voice.sourceRatio = sourceRatio;
        voice.syncRatio = tempoRatio(settings);
        voice.pitchRatio = pitchRatioFor(
            event.key, voice.rootNote, settings);
        voice.targetPitchRatio = voice.pitchRatio;
        voice.increment = sourceRatio * (reverse ? -1.0 : 1.0);
        configurePitchMode(voice, settings.pitchMode,
            event.key, voice.rootNote);
        const LoopGeometry loop = resolveLoopGeometry(voice, settings);
        voice.loopStartFrame = loop.start;
        voice.loopEndFrame = loop.end;
        voice.loopCrossfadeFrames = loop.crossfadeFrames;
        const float velocity = std::clamp(1.0f
            + (std::clamp(event.velocity, 0.0f, 1.0f) - 1.0f)
                * settings.velocitySensitivity, 0.0f, 1.0f);
        voice.velocityLevel = velocity;
        const uint32_t boundedLength = currentOutputLengthFrames(voice);
        voice.envelopeReferenceFrames = boundedLength;
        voice.attackFrames = proportionalFrames(
            settings.attackProportion, boundedLength);
        voice.decayFrames = proportionalFrames(
            settings.decayProportion, boundedLength);
        voice.releaseFrames = proportionalFrames(
            settings.releaseProportion, boundedLength);
        voice.sustainLevel = settings.sustain;
        voice.targetSustainLevel = settings.sustain;
        if (voice.attackFrames != 0u) {
            voice.envelopeStage = Voice::EnvelopeStage::Attack;
            voice.envelopeLevel = 0.0f;
        } else if (voice.decayFrames != 0u) {
            voice.envelopeStage = Voice::EnvelopeStage::Decay;
            voice.envelopeLevel = 1.0f;
        } else {
            voice.envelopeStage = Voice::EnvelopeStage::Sustain;
            voice.envelopeLevel = voice.sustainLevel;
        }
        voice.active = voice.velocityLevel > 0.0f;
    }

    void retargetVoice(Voice& voice, const RenderEvent& event,
        const PlayerSettings& settings) noexcept
    {
        voice.noteId = event.noteId;
        voice.age = ++ageCounter_;
        voice.key = event.key;
        voice.rootNote = settings.rootNote;
        voice.midiChannel = event.midiChannel;
        configurePitchMode(voice, settings.pitchMode,
            event.key, voice.rootNote);
        const double target = pitchRatioFor(
            event.key, voice.rootNote, settings);
        const uint32_t glideFrames = static_cast<uint32_t>(
            std::clamp(std::round(settings.glideSeconds * sampleRate_),
                0.0, static_cast<double>(
                    std::numeric_limits<uint32_t>::max())));
        setPitchTarget(voice, target, glideFrames);
    }

    void releaseMatching(const RenderEvent& event, bool choke,
        TriggerMode triggerMode = TriggerMode::Auto) noexcept
    {
        for (auto& voice : voices_) {
            if (!voice.active) continue;
            if (!eventMatchesVoice(event, voice)) continue;
            if (!choke && triggerMode == TriggerMode::Auto
                && !isLooping(voice.playMode)) continue;
            beginRelease(voice, choke ? 0u : voice.releaseFrames);
        }
    }

    static void beginRelease(Voice& voice, uint32_t frames) noexcept
    {
        if (frames == 0u || voice.envelopeLevel <= 0.0f) {
            voice.active = false;
            voice.envelopeLevel = 0.0f;
            return;
        }
        voice.releaseStartLevel = voice.envelopeLevel;
        voice.envelopeFrame = 0u;
        voice.releaseFrames = frames;
        voice.envelopeStage = Voice::EnvelopeStage::Release;
    }

    static void advanceEnvelope(Voice& voice) noexcept
    {
        switch (voice.envelopeStage) {
        case Voice::EnvelopeStage::Attack:
            ++voice.envelopeFrame;
            if (voice.envelopeFrame >= voice.attackFrames) {
                voice.envelopeFrame = 0u;
                voice.envelopeLevel = 1.0f;
                if (voice.decayFrames != 0u)
                    voice.envelopeStage = Voice::EnvelopeStage::Decay;
                else {
                    voice.envelopeStage = Voice::EnvelopeStage::Sustain;
                    voice.envelopeLevel = voice.sustainLevel;
                }
            } else {
                voice.envelopeLevel = static_cast<float>(voice.envelopeFrame)
                    / static_cast<float>(voice.attackFrames);
            }
            break;
        case Voice::EnvelopeStage::Decay:
            ++voice.envelopeFrame;
            if (voice.envelopeFrame >= voice.decayFrames) {
                voice.envelopeFrame = 0u;
                voice.envelopeLevel = voice.sustainLevel;
                voice.envelopeStage = Voice::EnvelopeStage::Sustain;
            } else {
                const float phase = static_cast<float>(voice.envelopeFrame)
                    / static_cast<float>(voice.decayFrames);
                voice.envelopeLevel = 1.0f
                    + (voice.sustainLevel - 1.0f) * phase;
            }
            break;
        case Voice::EnvelopeStage::Sustain:
            voice.envelopeLevel = voice.sustainLevel;
            break;
        case Voice::EnvelopeStage::Release:
            ++voice.envelopeFrame;
            if (voice.envelopeFrame >= voice.releaseFrames) {
                voice.envelopeLevel = 0.0f;
                voice.active = false;
            } else {
                voice.envelopeLevel = voice.releaseStartLevel
                    * (1.0f - static_cast<float>(voice.envelopeFrame)
                        / static_cast<float>(voice.releaseFrames));
            }
            break;
        }
    }

    static void advancePosition(Voice& voice) noexcept
    {
        if (!voice.active) return;
        if (!isLooping(voice.playMode)) {
            if (voice.position < static_cast<double>(voice.playStartFrame)
                || voice.position >= static_cast<double>(voice.playEndFrame))
                voice.active = false;
            return;
        }
        const double loopStart = static_cast<double>(voice.loopStartFrame);
        const double loopEnd = static_cast<double>(voice.loopEndFrame);
        const double loopLength = loopEnd - loopStart;
        if (!(loopLength > 0.0)) {
            voice.active = false;
            return;
        }
        if (isPingPong(voice.playMode)) {
            const double lower = loopStart;
            const double upper = loopEnd - 1.0;
            const double range = upper - lower;
            if (!(range > 0.0)) {
                voice.position = lower;
                return;
            }
            const double period = 2.0 * range;
            if (voice.increment > 0.0 && voice.position > upper) {
                double distance = std::fmod(voice.position - upper, period);
                if (distance < 0.0) distance += period;
                if (distance <= range) {
                    voice.position = upper - distance;
                    voice.increment = -std::abs(voice.increment);
                } else {
                    voice.position = lower + (distance - range);
                    voice.increment = std::abs(voice.increment);
                }
                voice.hasLooped = true;
            } else if (voice.increment < 0.0 && voice.position < lower) {
                double distance = std::fmod(lower - voice.position, period);
                if (distance < 0.0) distance += period;
                if (distance <= range) {
                    voice.position = lower + distance;
                    voice.increment = std::abs(voice.increment);
                } else {
                    voice.position = upper - (distance - range);
                    voice.increment = -std::abs(voice.increment);
                }
                voice.hasLooped = true;
            }
            return;
        }
        const double crossfade = std::clamp(voice.loopCrossfadeFrames,
            0.0, loopLength * 0.5);
        if (voice.playMode == PlayMode::ForwardLoop
            && voice.position >= loopEnd) {
            const double wrappedStart = loopStart + crossfade;
            const double wrappedLength = loopEnd - wrappedStart;
            if (!(wrappedLength > 0.0)) {
                voice.active = false;
                return;
            }
            double distance = std::fmod(
                voice.position - wrappedStart, wrappedLength);
            if (distance < 0.0) distance += wrappedLength;
            voice.position = wrappedStart + distance;
            voice.hasLooped = true;
        } else if (voice.playMode == PlayMode::ReverseLoop
            && voice.position < loopStart) {
            const double wrappedEnd = loopEnd - crossfade;
            const double wrappedLength = wrappedEnd - loopStart;
            if (!(wrappedLength > 0.0)) {
                voice.active = false;
                return;
            }
            const double distance = std::fmod(loopStart - voice.position,
                wrappedLength);
            voice.position = wrappedEnd - distance;
            if (voice.position >= wrappedEnd) voice.position = loopStart;
            voice.hasLooped = true;
        }
    }

    static void advanceStretchPhase(Voice& voice) noexcept
    {
        if (voice.pitchMode != PitchMode::Stretch
            || !(voice.stretchPhaseStep > 0.0)) return;
        voice.stretchPhase += voice.stretchPhaseStep;
        voice.stretchPhase -= std::floor(voice.stretchPhase);
    }

    static double wrapPosition(double position, double start,
        double end) noexcept
    {
        const double length = end - start;
        if (!(length > 0.0)) return start;
        position = start + std::fmod(position - start, length);
        if (position < start) position += length;
        return position;
    }

    static double reflectPosition(double position, double start,
        double end) noexcept
    {
        const double upper = end - 1.0;
        const double range = upper - start;
        if (!(range > 0.0)) return start;
        const double period = range * 2.0;
        double offset = std::fmod(position - start, period);
        if (offset < 0.0) offset += period;
        return offset <= range ? start + offset
                               : upper - (offset - range);
    }

    static double resolveStretchPosition(const Voice& voice,
        double position) noexcept
    {
        if (!isLooping(voice.playMode) || !voice.hasLooped)
            return std::clamp(position,
                static_cast<double>(voice.playStartFrame),
                static_cast<double>(voice.playEndFrame - 1u));
        const double start = static_cast<double>(voice.loopStartFrame);
        const double end = static_cast<double>(voice.loopEndFrame);
        return isPingPong(voice.playMode)
            ? reflectPosition(position, start, end)
            : wrapPosition(position, start, end);
    }

    static float grainWindow(double phase) noexcept
    {
        constexpr double twoPi = 6.283185307179586476925286766559;
        return static_cast<float>(0.5 - 0.5 * std::cos(twoPi
            * std::clamp(phase, 0.0, 1.0)));
    }

    static StretchFrame makeStretchFrame(const Voice& voice) noexcept
    {
        StretchFrame frame;
        if (!(voice.stretchPhaseStep > 1.0e-12)
            || !(voice.stretchWindowSourceFrames > 0.0))
            return frame;
        const double firstPhase = voice.stretchPhase;
        double secondPhase = firstPhase + 0.5;
        if (secondPhase >= 1.0) secondPhase -= 1.0;
        const double direction = voice.increment < 0.0 ? -1.0 : 1.0;
        const auto readerPosition = [&](double phase) noexcept {
            const double delay = voice.pitchRatio >= voice.syncRatio
                ? voice.stretchWindowSourceFrames * (1.0 - phase)
                : voice.stretchWindowSourceFrames * phase;
            return resolveStretchPosition(voice,
                voice.position - direction * delay);
        };
        frame.firstPosition = readerPosition(firstPhase);
        frame.secondPosition = readerPosition(secondPhase);
        frame.firstWeight = grainWindow(firstPhase);
        frame.secondWeight = grainWindow(secondPhase);
        frame.normalization = 1.0f / std::max(1.0e-6f,
            frame.firstWeight + frame.secondWeight);
        frame.active = true;
        return frame;
    }

    static float stretchSample(const Voice& voice,
        const std::vector<float>& samples,
        const StretchFrame& frame) noexcept
    {
        if (!frame.active) return loopCrossfadedSample(voice, samples);
        const float first = loopCrossfadedSampleAt(voice, samples,
            frame.firstPosition);
        const float second = loopCrossfadedSampleAt(voice, samples,
            frame.secondPosition);
        return (first * frame.firstWeight + second * frame.secondWeight)
            * frame.normalization;
    }

    static float loopCrossfadedSample(const Voice& voice,
        const std::vector<float>& samples) noexcept
    {
        return loopCrossfadedSampleAt(voice, samples, voice.position);
    }

    static float loopCrossfadedSampleAt(const Voice& voice,
        const std::vector<float>& samples, double position) noexcept
    {
        const float primary = interpolate(samples, position,
            voice.playStartFrame, voice.playEndFrame);
        const double crossfade = voice.loopCrossfadeFrames;
        if (!(crossfade > 0.0)) return primary;
        const double rate = voice.readIncrementMagnitude;
        const double denominator = std::max(
            crossfade - std::min(rate, crossfade - 1.0e-9), 1.0e-9);
        if (voice.playMode == PlayMode::ForwardLoop) {
            const double fadeStart = static_cast<double>(
                voice.loopEndFrame) - crossfade;
            if (position < fadeStart) return primary;
            const double phase = std::clamp(
                (position - fadeStart) / denominator, 0.0, 1.0);
            const double secondaryPosition = static_cast<double>(
                voice.loopStartFrame) + (position - fadeStart);
            const float secondary = interpolate(samples, secondaryPosition,
                voice.playStartFrame, voice.playEndFrame);
            return primary + (secondary - primary)
                * static_cast<float>(phase);
        }
        if (voice.playMode == PlayMode::ReverseLoop) {
            const double fadeEnd = static_cast<double>(
                voice.loopStartFrame) + crossfade;
            if (position > fadeEnd) return primary;
            const double phase = std::clamp(
                (fadeEnd - position) / denominator, 0.0, 1.0);
            const double secondaryPosition = static_cast<double>(
                voice.loopEndFrame) - crossfade
                + (position
                    - static_cast<double>(voice.loopStartFrame));
            const float secondary = interpolate(samples, secondaryPosition,
                voice.playStartFrame, voice.playEndFrame);
            return primary + (secondary - primary)
                * static_cast<float>(phase);
        }
        return primary;
    }

    FilterCoefficients makeFilterCoefficients(const Voice& voice,
        const PlayerSettings& settings) const noexcept
    {
        FilterCoefficients coefficients;
        coefficients.type = settings.filterType;
        if (settings.filterType == FilterType::Off) return coefficients;
        const double modulation = std::pow(2.0,
            static_cast<double>(settings.filterEnvelopeAmount)
                * static_cast<double>(voice.envelopeLevel) * 6.0);
        const double maximumCutoff = std::min(20000.0, sampleRate_ * 0.45);
        const double cutoff = std::clamp(
            static_cast<double>(settings.filterCutoffHz) * modulation,
            20.0, maximumCutoff);
        constexpr double pi = 3.1415926535897932384626433832795;
        const double g = std::tan(pi * cutoff / sampleRate_);
        const double resonance = std::clamp(
            static_cast<double>(settings.filterResonance), 0.0, 1.0);
        const double q = 0.5 + resonance * resonance * 19.5;
        coefficients.damping = 1.0 / q;
        coefficients.a1 = 1.0
            / (1.0 + g * (g + coefficients.damping));
        coefficients.a2 = g * coefficients.a1;
        coefficients.a3 = g * coefficients.a2;
        return coefficients;
    }

    static float processFilter(Voice::FilterState& state, float input,
        const FilterCoefficients& coefficients) noexcept
    {
        if (coefficients.type == FilterType::Off) return input;
        const double v3 = static_cast<double>(input) - state.integrator2;
        const double band = coefficients.a1 * state.integrator1
            + coefficients.a2 * v3;
        const double low = state.integrator2
            + coefficients.a2 * state.integrator1
            + coefficients.a3 * v3;
        state.integrator1 = 2.0 * band - state.integrator1;
        state.integrator2 = 2.0 * low - state.integrator2;
        const double high = static_cast<double>(input)
            - coefficients.damping * band - low;
        double output = input;
        switch (coefficients.type) {
        case FilterType::Off: output = input; break;
        case FilterType::LowPass: output = low; break;
        case FilterType::BandPass: output = band; break;
        case FilterType::HighPass: output = high; break;
        case FilterType::Notch: output = high + low; break;
        }
        if (std::isfinite(output) && std::isfinite(state.integrator1)
            && std::isfinite(state.integrator2))
            return static_cast<float>(output);
        state = {};
        return 0.0f;
    }

    void applyFinalOutput(const PlayerSettings& settings,
        float* const* outputs, uint32_t outputChannelCount,
        uint32_t frameCount) noexcept
    {
        const float gain = std::pow(10.0f,
            settings.gainDecibels * 0.05f);
        const float leftPan = outputChannelCount == 2u
            ? std::sqrt(std::max(0.0f, 1.0f
                - std::max(0.0f, settings.pan))) : 1.0f;
        const float rightPan = outputChannelCount == 2u
            ? std::sqrt(std::max(0.0f, 1.0f
                + std::min(0.0f, settings.pan))) : 1.0f;
        for (uint32_t channel = 0u; channel < outputChannelCount;
             ++channel) {
            const float channelGain = gain * (channel == 0u
                    ? leftPan : channel == 1u ? rightPan : 1.0f);
            for (uint32_t frame = 0u; frame < frameCount; ++frame) {
                outputs[channel][frame] *= channelGain;
                outputPeak_ = std::max(outputPeak_,
                    std::abs(outputs[channel][frame]));
            }
        }
    }

    static float boundaryFade(const Voice& voice) noexcept
    {
        if (isLooping(voice.playMode)) return 1.0f;
        const double rate = std::abs(voice.increment);
        if (!(rate > 0.0)) return 0.0f;
        const double distance = isReverse(voice.playMode)
            ? std::max(0.0, voice.position
                - static_cast<double>(voice.playStartFrame))
            : std::max(0.0, static_cast<double>(voice.playEndFrame)
                - voice.position);
        const uint64_t remaining = static_cast<uint64_t>(std::max(1.0,
            isReverse(voice.playMode) ? std::floor(distance / rate) + 1.0
                                      : std::ceil(distance / rate)));
        const uint32_t naturalFadeFrames = static_cast<uint32_t>(
            std::clamp<double>(std::ceil(16.0 / rate), 2.0,
                std::numeric_limits<uint32_t>::max()));
        const uint32_t fadeFrames = std::max(naturalFadeFrames,
            voice.releaseFrames);
        if (remaining >= fadeFrames) return 1.0f;
        return static_cast<float>(remaining - 1u)
            / static_cast<float>(fadeFrames - 1u);
    }

    static float interpolate(const std::vector<float>& samples,
        double position, uint32_t start, uint32_t end) noexcept
    {
        if (samples.empty() || end <= start) return 0.0f;
        position = std::clamp(position, static_cast<double>(start),
            static_cast<double>(end - 1u));
        const uint32_t first = static_cast<uint32_t>(std::floor(position));
        const uint32_t second = std::min(first + 1u, end - 1u);
        const float phase = static_cast<float>(position
            - static_cast<double>(first));
        return samples[first] + (samples[second] - samples[first]) * phase;
    }

    const SampleAsset* asset_ = nullptr;
    std::array<Voice, kMaximumVoices> voices_ {};
    std::array<HeldNote, kMidiNoteCount> heldNotes_ {};
    double sampleRate_ = 48000.0;
    uint64_t ageCounter_ = 0u;
    uint64_t heldNoteAgeCounter_ = 0u;
    uint32_t outputChannelCount_ = 2u;
    std::array<VoiceCursor, kMaximumVoices> voiceCursors_ {};
    uint32_t voiceCursorCount_ = 0u;
    float outputPeak_ = 0.0f;
    bool prepared_ = false;
};

} // namespace s3g::sample
