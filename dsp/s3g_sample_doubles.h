#pragma once

#include "s3g_sample_asset.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace s3g::sample {

// Sample Doubles is intentionally a two-read-head instrument rather than a
// polyphonic sampler. Both decks reference one immutable SampleAsset and keep
// independent positions in that shared stereo source.
enum class DoublesCrossfadeCurve : uint8_t {
    Cut = 0u,
    Sharp,
    Blend,
};

enum class DoublesEventKind : uint8_t {
    Restart = 0u,
    Stop,
    Play,
    SyncDeckB,
    PhaseStepBackward,
    PhaseStepForward,
    PunchAOn,
    PunchAOff,
    PunchBOn,
    PunchBOff,
    SelectOffset,
    ToggleDeckA,
    ToggleDeckB,
    DragAOn,
    DragAOff,
    DragBOn,
    DragBOff,
};

struct DoublesRenderEvent {
    uint32_t frameOffset = 0u;
    DoublesEventKind kind = DoublesEventKind::Restart;
    uint64_t noteId = 0u;
    // Punch depth is MIDI velocity normalized to 0..1. SelectOffset carries
    // its signed beat displacement here; other events ignore the value.
    float value = 1.0f;
};

struct DoublesSettings {
    double sourceTempoBpm = 120.0;
    // Authentic deck/tape varispeed: pitch and duration remain coupled.
    double speedSemitones = -7.0;
    // A small independent speed error on Deck B provides gradual tape-style
    // phasing. Zero retains the fixed two-copy DJ relationship.
    double phaseCents = 0.0;
    double offsetBeats = 1.0;
    double phaseStepBeats = 0.25;
    // A realtime phase displacement relative to the last applied value.
    // Restart/Sync also include it in Deck B's absolute source-beat offset.
    double livePhaseBeats = 0.0;
    double start = 0.0;
    double end = 1.0;
    bool loop = false;
    // -1 is Deck A, +1 is Deck B.
    double crossfader = -1.0;
    DoublesCrossfadeCurve crossfadeCurve = DoublesCrossfadeCurve::Cut;
    float deckALevelDecibels = 0.0f;
    float deckBLevelDecibels = 0.0f;
    float gainDecibels = -6.0f;
    bool linkDecks = true;

    bool valid() const noexcept
    {
        return std::isfinite(sourceTempoBpm)
            && sourceTempoBpm >= 20.0 && sourceTempoBpm <= 999.0
            && std::isfinite(speedSemitones)
            && speedSemitones >= -24.0 && speedSemitones <= 12.0
            && std::isfinite(phaseCents)
            && phaseCents >= -100.0 && phaseCents <= 100.0
            && std::isfinite(offsetBeats)
            && offsetBeats >= -8.0 && offsetBeats <= 8.0
            && std::isfinite(phaseStepBeats)
            && phaseStepBeats >= 0.015625 && phaseStepBeats <= 4.0
            && std::isfinite(livePhaseBeats)
            && livePhaseBeats >= -1.0 && livePhaseBeats <= 1.0
            && std::isfinite(start) && start >= 0.0 && start <= 1.0
            && std::isfinite(end) && end >= 0.0 && end <= 1.0
            && start < end
            && std::isfinite(crossfader)
            && crossfader >= -1.0 && crossfader <= 1.0
            && static_cast<uint8_t>(crossfadeCurve)
                <= static_cast<uint8_t>(DoublesCrossfadeCurve::Blend)
            && std::isfinite(deckALevelDecibels)
            && deckALevelDecibels >= -60.0f
            && deckALevelDecibels <= 12.0f
            && std::isfinite(deckBLevelDecibels)
            && deckBLevelDecibels >= -60.0f
            && deckBLevelDecibels <= 12.0f
            && std::isfinite(gainDecibels)
            && gainDecibels >= -60.0f && gainDecibels <= 12.0f;
    }
};

// Tracker-facing MIDI command keyboard. The compact map leaves the remainder
// of the keyboard free for future gestures while making every important chop
// operation expressible as an ordinary gated note.
constexpr uint8_t kDoublesMidiRestart = 36u;
constexpr uint8_t kDoublesMidiStop = 37u;
constexpr uint8_t kDoublesMidiSyncDeckB = 38u;
constexpr uint8_t kDoublesMidiPhaseStepBackward = 39u;
constexpr uint8_t kDoublesMidiPunchA = 40u;
constexpr uint8_t kDoublesMidiPunchB = 41u;
constexpr uint8_t kDoublesMidiPhaseStepForward = 42u;
constexpr uint8_t kDoublesMidiPlay = 43u;
constexpr uint8_t kDoublesMidiToggleDeckA = 44u;
constexpr uint8_t kDoublesMidiToggleDeckB = 45u;
constexpr uint8_t kDoublesMidiDragA = 46u;
constexpr uint8_t kDoublesMidiDragB = 47u;
constexpr uint8_t kDoublesMidiFirstOffset = 48u;
constexpr uint8_t kDoublesMidiLastOffset = 60u;

constexpr uint8_t kDoublesMidiCcCrossfader = 16u;
constexpr uint8_t kDoublesMidiCcDeckALevel = 17u;
constexpr uint8_t kDoublesMidiCcDeckBLevel = 18u;
constexpr uint8_t kDoublesMidiCcLivePhase = 19u;

inline bool doublesOffsetForMidiNote(uint8_t note,
    double& offsetBeats) noexcept
{
    constexpr std::array<double, 13u> offsets {{
        -4.0, -2.0, -1.0, -0.5, -0.25, -0.125, 0.0,
        0.125, 0.25, 0.5, 1.0, 2.0, 4.0,
    }};
    if (note < kDoublesMidiFirstOffset || note > kDoublesMidiLastOffset)
        return false;
    offsetBeats = offsets[static_cast<std::size_t>(
        note - kDoublesMidiFirstOffset)];
    return true;
}

class SampleDoublesEngine {
public:
    bool prepare(double sampleRate) noexcept
    {
        if (!(sampleRate > 0.0) || !std::isfinite(sampleRate)) return false;
        outputSampleRate_ = sampleRate;
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
        deckA_ = {};
        deckB_ = {};
        playing_ = false;
        punchA_ = {};
        punchB_ = {};
        gestureAge_ = 0u;
        crossfadePosition_ = 0.0;
        crossfadeInitialized_ = false;
        deckGainA_ = 1.0;
        deckGainB_ = 1.0;
        deckGainInitialized_ = false;
        livePhaseBeats_ = 0.0;
        livePhaseInitialized_ = false;
        outputPeak_ = 0.0f;
    }

    void killAll() noexcept
    {
        deckA_.active = false;
        deckB_.active = false;
        playing_ = false;
        punchA_ = {};
        punchB_ = {};
        deckA_.dragHeld = false;
        deckA_.dragNoteId = 0u;
        deckB_.dragHeld = false;
        deckB_.dragNoteId = 0u;
    }

    bool setAsset(const SampleAsset* asset) noexcept
    {
        if (asset && (!asset->valid() || asset->channelCount > 2u))
            return false;
        asset_ = asset;
        reset();
        return true;
    }

    // Audio-thread adoption for an immutable asset validated before
    // publication. This mirrors the Sample Player family contract.
    void setPreparedAsset(const SampleAsset* asset) noexcept
    {
        asset_ = asset;
        reset();
    }

    bool playing() const noexcept { return playing_; }
    bool deckAActive() const noexcept { return deckA_.active; }
    bool deckBActive() const noexcept { return deckB_.active; }
    bool punchAHeld() const noexcept { return punchA_.held; }
    bool punchBHeld() const noexcept { return punchB_.held; }
    bool dragAHeld() const noexcept { return deckA_.dragHeld; }
    bool dragBHeld() const noexcept { return deckB_.dragHeld; }
    double deckARateScale() const noexcept { return deckA_.dragScale; }
    double deckBRateScale() const noexcept { return deckB_.dragScale; }
    float outputPeak() const noexcept { return outputPeak_; }

    float deckAPositionNormalized() const noexcept
    {
        return normalizedPosition(deckA_);
    }

    float deckBPositionNormalized() const noexcept
    {
        return normalizedPosition(deckB_);
    }

    void render(const DoublesSettings& settings,
        const DoublesRenderEvent* events, std::size_t eventCount,
        float* const* outputs, uint32_t outputChannelCount,
        uint32_t frameCount) noexcept
    {
        if (!outputs || outputChannelCount != 2u) return;
        if (!outputs[0u] || !outputs[1u]) return;
        std::fill(outputs[0u], outputs[0u] + frameCount, 0.0f);
        std::fill(outputs[1u], outputs[1u] + frameCount, 0.0f);
        outputPeak_ = 0.0f;
        if (!prepared_ || !asset_ || !settings.valid()
            || asset_->channelCount == 0u || asset_->channelCount > 2u
            || asset_->frameCount() == 0u || frameCount == 0u) return;
        if (!events) eventCount = 0u;

        const Bounds bounds = resolvedBounds(settings);
        constrainDeck(deckA_, settings, bounds);
        constrainDeck(deckB_, settings, bounds);
        applyLivePhase(settings, bounds);
        if (!crossfadeInitialized_) {
            crossfadePosition_ = crossfadeTarget(settings);
            crossfadeInitialized_ = true;
        }

        const double sourceRateRatio = asset_->sampleRate
            / outputSampleRate_;
        const double speedRatio = std::pow(2.0,
            settings.speedSemitones / 12.0);
        const double incrementA = sourceRateRatio * speedRatio;
        const double incrementB = incrementA * std::pow(2.0,
            settings.phaseCents / 1200.0);
        const float outputGain = decibelsToAmplitude(
            settings.gainDecibels);
        const double crossfadeSmoothing = 1.0 - std::exp(-1.0
            / (outputSampleRate_ * kCrossfadeSmoothingSeconds));
        const double gainSmoothing = 1.0 - std::exp(-1.0
            / (outputSampleRate_ * kDeckGainSmoothingSeconds));
        const float gainTargetA = decibelsToAmplitude(
            settings.deckALevelDecibels);
        const float gainTargetB = decibelsToAmplitude(
            settings.deckBLevelDecibels);
        if (!deckGainInitialized_) {
            deckGainA_ = gainTargetA;
            deckGainB_ = gainTargetB;
            deckGainInitialized_ = true;
        }

        std::size_t eventIndex = 0u;
        for (uint32_t frame = 0u; frame < frameCount; ++frame) {
            while (eventIndex < eventCount
                && events[eventIndex].frameOffset <= frame) {
                applyEvent(events[eventIndex], settings, bounds);
                ++eventIndex;
            }

            const double target = crossfadeTarget(settings);
            crossfadePosition_ += crossfadeSmoothing
                * (target - crossfadePosition_);
            deckGainA_ += static_cast<float>(gainSmoothing)
                * (gainTargetA - deckGainA_);
            deckGainB_ += static_cast<float>(gainSmoothing)
                * (gainTargetB - deckGainB_);
            updateDrag(deckA_);
            updateDrag(deckB_);
            float gainA = 0.0f;
            float gainB = 0.0f;
            crossfadeGains(settings.crossfadeCurve,
                crossfadePosition_, gainA, gainB);

            for (uint32_t channel = 0u; channel < 2u; ++channel) {
                const uint8_t sourceChannel = asset_->channelCount == 1u
                    ? 0u : static_cast<uint8_t>(channel);
                const float a = deckA_.active
                    ? readSample(sourceChannel, deckA_.position,
                        bounds, settings.loop) : 0.0f;
                const float b = deckB_.active
                    ? readSample(sourceChannel, deckB_.position,
                        bounds, settings.loop) : 0.0f;
                const float sample = (a * gainA * deckGainA_
                    + b * gainB * deckGainB_) * outputGain;
                outputs[channel][frame] = sample;
                outputPeak_ = std::max(outputPeak_, std::abs(sample));
            }

            advanceDeck(deckA_, incrementA * deckA_.dragScale,
                settings, bounds);
            advanceDeck(deckB_, incrementB * deckB_.dragScale,
                settings, bounds);
            if (!deckA_.active && !deckB_.active) playing_ = false;
        }

        // Events exactly at the block boundary establish state for the next
        // block instead of being silently discarded.
        while (eventIndex < eventCount
            && events[eventIndex].frameOffset <= frameCount) {
            applyEvent(events[eventIndex], settings, bounds);
            ++eventIndex;
        }
    }

private:
    static constexpr double kCrossfadeSmoothingSeconds = 0.00025;
    static constexpr double kDeckGainSmoothingSeconds = 0.005;
    static constexpr double kDragDownSeconds = 0.030;
    static constexpr double kMotorRecoverySeconds = 0.220;
    static constexpr double kHeldDragScale = 0.16;

    struct Deck {
        double position = 0.0;
        bool active = false;
        bool dragHeld = false;
        uint64_t dragNoteId = 0u;
        double dragScale = 1.0;
    };

    struct Punch {
        bool held = false;
        float depth = 1.0f;
        uint64_t noteId = 0u;
        uint64_t age = 0u;
    };

    struct Bounds {
        uint32_t start = 0u;
        uint32_t end = 1u;
    };

    float normalizedPosition(const Deck& deck) const noexcept
    {
        if (!asset_ || asset_->frameCount() == 0u) return -1.0f;
        return static_cast<float>(std::clamp(deck.position
            / static_cast<double>(asset_->frameCount()), 0.0, 1.0));
    }

    Bounds resolvedBounds(const DoublesSettings& settings) const noexcept
    {
        const uint32_t frames = asset_->frameCount();
        const auto roundedStart = static_cast<uint32_t>(std::llround(
            settings.start * static_cast<double>(frames)));
        const auto roundedEnd = static_cast<uint32_t>(std::llround(
            settings.end * static_cast<double>(frames)));
        Bounds bounds;
        bounds.start = std::min(roundedStart, frames - 1u);
        bounds.end = std::clamp(roundedEnd, bounds.start + 1u, frames);
        return bounds;
    }

    static double wrapPosition(double position,
        const Bounds& bounds) noexcept
    {
        const double start = static_cast<double>(bounds.start);
        const double length = static_cast<double>(bounds.end - bounds.start);
        double wrapped = std::fmod(position - start, length);
        if (wrapped < 0.0) wrapped += length;
        return start + wrapped;
    }

    static void constrainDeck(Deck& deck, const DoublesSettings& settings,
        const Bounds& bounds) noexcept
    {
        if (settings.loop) {
            deck.position = wrapPosition(deck.position, bounds);
            return;
        }
        if (deck.position < static_cast<double>(bounds.start))
            deck.position = static_cast<double>(bounds.start);
        if (deck.position >= static_cast<double>(bounds.end)) {
            deck.position = static_cast<double>(bounds.end - 1u);
            deck.active = false;
        }
    }

    double beatFrames(const DoublesSettings& settings) const noexcept
    {
        return asset_->sampleRate * 60.0 / settings.sourceTempoBpm;
    }

    void applyLivePhase(const DoublesSettings& settings,
        const Bounds& bounds) noexcept
    {
        if (!livePhaseInitialized_) {
            livePhaseBeats_ = settings.livePhaseBeats;
            livePhaseInitialized_ = true;
            return;
        }
        const double delta = settings.livePhaseBeats - livePhaseBeats_;
        livePhaseBeats_ = settings.livePhaseBeats;
        if (std::abs(delta) < 1.0e-12) return;
        deckB_.position += beatFrames(settings) * delta;
        constrainDeck(deckB_, settings, bounds);
    }

    void restart(const DoublesSettings& settings,
        const Bounds& bounds) noexcept
    {
        deckA_.position = static_cast<double>(bounds.start);
        deckA_.active = true;
        playing_ = true;
        syncDeckB(settings, bounds, settings.offsetBeats);
    }

    void play(const DoublesSettings& settings, const Bounds& bounds) noexcept
    {
        constrainDeck(deckA_, settings, bounds);
        constrainDeck(deckB_, settings, bounds);
        if (deckA_.position >= static_cast<double>(bounds.end - 1u))
            deckA_.position = static_cast<double>(bounds.start);
        if (deckB_.position >= static_cast<double>(bounds.end - 1u))
            syncDeckB(settings, bounds, settings.offsetBeats);
        deckA_.active = true;
        deckB_.active = true;
        playing_ = true;
    }

    void syncDeckB(const DoublesSettings& settings, const Bounds& bounds,
        double offset) noexcept
    {
        double position = deckA_.position + beatFrames(settings)
            * (offset + settings.livePhaseBeats);
        if (settings.loop) position = wrapPosition(position, bounds);
        else position = std::clamp(position,
            static_cast<double>(bounds.start),
            static_cast<double>(bounds.end - 1u));
        deckB_.position = position;
        deckB_.active = playing_;
    }

    void toggleDeck(bool deckB, const DoublesSettings& settings,
        const Bounds& bounds) noexcept
    {
        if (settings.linkDecks) {
            if (deckA_.active || deckB_.active) {
                deckA_.active = false;
                deckB_.active = false;
                playing_ = false;
            } else play(settings, bounds);
            return;
        }
        Deck& deck = deckB ? deckB_ : deckA_;
        if (deck.active) {
            deck.active = false;
        } else {
            constrainDeck(deck, settings, bounds);
            if (deck.position >= static_cast<double>(bounds.end - 1u)) {
                if (deckB) syncDeckB(settings, bounds,
                    settings.offsetBeats);
                else deck.position = static_cast<double>(bounds.start);
            }
            deck.active = true;
        }
        playing_ = deckA_.active || deckB_.active;
    }

    static void setDrag(Deck& deck, bool held, uint64_t noteId) noexcept
    {
        if (held) {
            deck.dragHeld = true;
            deck.dragNoteId = noteId;
        } else if (noteId == 0u || deck.dragNoteId == 0u
            || deck.dragNoteId == noteId) {
            deck.dragHeld = false;
            deck.dragNoteId = 0u;
        }
    }

    void updateDrag(Deck& deck) const noexcept
    {
        const double target = deck.dragHeld ? kHeldDragScale : 1.0;
        const double seconds = deck.dragHeld
            ? kDragDownSeconds : kMotorRecoverySeconds;
        const double smoothing = 1.0 - std::exp(-1.0
            / (outputSampleRate_ * seconds));
        deck.dragScale += smoothing * (target - deck.dragScale);
        deck.dragScale = std::clamp(deck.dragScale,
            kHeldDragScale, 1.0);
    }

    void phaseStep(const DoublesSettings& settings, const Bounds& bounds,
        double direction) noexcept
    {
        deckB_.position += direction * beatFrames(settings)
            * settings.phaseStepBeats;
        if (settings.loop)
            deckB_.position = wrapPosition(deckB_.position, bounds);
        else {
            deckB_.position = std::clamp(deckB_.position,
                static_cast<double>(bounds.start),
                static_cast<double>(bounds.end - 1u));
        }
    }

    void applyEvent(const DoublesRenderEvent& event,
        const DoublesSettings& settings, const Bounds& bounds) noexcept
    {
        switch (event.kind) {
        case DoublesEventKind::Restart:
            restart(settings, bounds);
            break;
        case DoublesEventKind::Stop:
            deckA_.active = false;
            deckB_.active = false;
            playing_ = false;
            punchA_ = {};
            punchB_ = {};
            deckA_.dragHeld = false;
            deckA_.dragNoteId = 0u;
            deckB_.dragHeld = false;
            deckB_.dragNoteId = 0u;
            break;
        case DoublesEventKind::Play:
            play(settings, bounds);
            break;
        case DoublesEventKind::SyncDeckB:
            syncDeckB(settings, bounds, settings.offsetBeats);
            break;
        case DoublesEventKind::PhaseStepBackward:
            phaseStep(settings, bounds, -1.0);
            break;
        case DoublesEventKind::PhaseStepForward:
            phaseStep(settings, bounds, 1.0);
            break;
        case DoublesEventKind::PunchAOn:
            punchA_.held = true;
            punchA_.depth = std::clamp(event.value, 0.0f, 1.0f);
            punchA_.noteId = event.noteId;
            punchA_.age = ++gestureAge_;
            break;
        case DoublesEventKind::PunchAOff:
            if (event.noteId == 0u || punchA_.noteId == 0u
                || punchA_.noteId == event.noteId) punchA_.held = false;
            break;
        case DoublesEventKind::PunchBOn:
            punchB_.held = true;
            punchB_.depth = std::clamp(event.value, 0.0f, 1.0f);
            punchB_.noteId = event.noteId;
            punchB_.age = ++gestureAge_;
            break;
        case DoublesEventKind::PunchBOff:
            if (event.noteId == 0u || punchB_.noteId == 0u
                || punchB_.noteId == event.noteId) punchB_.held = false;
            break;
        case DoublesEventKind::SelectOffset:
            syncDeckB(settings, bounds, std::clamp(
                static_cast<double>(event.value), -8.0, 8.0));
            break;
        case DoublesEventKind::ToggleDeckA:
            toggleDeck(false, settings, bounds);
            break;
        case DoublesEventKind::ToggleDeckB:
            toggleDeck(true, settings, bounds);
            break;
        case DoublesEventKind::DragAOn:
            setDrag(deckA_, true, event.noteId);
            break;
        case DoublesEventKind::DragAOff:
            setDrag(deckA_, false, event.noteId);
            break;
        case DoublesEventKind::DragBOn:
            setDrag(deckB_, true, event.noteId);
            break;
        case DoublesEventKind::DragBOff:
            setDrag(deckB_, false, event.noteId);
            break;
        }
    }

    double crossfadeTarget(const DoublesSettings& settings) const noexcept
    {
        const double base = settings.crossfader;
        const Punch* punch = nullptr;
        double side = base;
        if (punchA_.held && (!punchB_.held
                || punchA_.age > punchB_.age)) {
            punch = &punchA_;
            side = -1.0;
        } else if (punchB_.held) {
            punch = &punchB_;
            side = 1.0;
        }
        if (!punch) return base;
        return base + (side - base) * static_cast<double>(punch->depth);
    }

    static void crossfadeGains(DoublesCrossfadeCurve curve,
        double crossfader, float& gainA, float& gainB) noexcept
    {
        constexpr double halfPi = 1.5707963267948966192313216916398;
        const double t = std::clamp((crossfader + 1.0) * 0.5, 0.0, 1.0);
        if (curve == DoublesCrossfadeCurve::Cut) {
            const double shaped = std::clamp((t - 0.48) / 0.04,
                0.0, 1.0);
            const double smooth = shaped * shaped * (3.0 - 2.0 * shaped);
            gainA = static_cast<float>(1.0 - smooth);
            gainB = static_cast<float>(smooth);
        } else if (curve == DoublesCrossfadeCurve::Sharp) {
            gainA = static_cast<float>(std::clamp(
                (0.60 - t) / 0.20, 0.0, 1.0));
            gainB = static_cast<float>(std::clamp(
                (t - 0.40) / 0.20, 0.0, 1.0));
        } else {
            gainA = static_cast<float>(std::cos(t * halfPi));
            gainB = static_cast<float>(std::sin(t * halfPi));
        }
    }

    float readSample(uint8_t channel, double position,
        const Bounds& bounds, bool loop) const noexcept
    {
        position = std::clamp(position,
            static_cast<double>(bounds.start),
            static_cast<double>(bounds.end - 1u));
        const uint32_t first = static_cast<uint32_t>(std::floor(position));
        uint32_t second = first + 1u;
        if (second >= bounds.end)
            second = loop ? bounds.start : bounds.end - 1u;
        const float fraction = static_cast<float>(position
            - static_cast<double>(first));
        const auto& samples = asset_->channels[channel];
        return samples[first] + (samples[second] - samples[first])
            * fraction;
    }

    static void advanceDeck(Deck& deck, double increment,
        const DoublesSettings& settings, const Bounds& bounds) noexcept
    {
        if (!deck.active) return;
        deck.position += increment;
        if (deck.position < static_cast<double>(bounds.end)) return;
        if (settings.loop) deck.position = wrapPosition(deck.position, bounds);
        else {
            deck.position = static_cast<double>(bounds.end - 1u);
            deck.active = false;
        }
    }

    static float decibelsToAmplitude(float decibels) noexcept
    {
        return std::pow(10.0f, decibels * 0.05f);
    }

    double outputSampleRate_ = 48000.0;
    const SampleAsset* asset_ = nullptr;
    Deck deckA_ {};
    Deck deckB_ {};
    Punch punchA_ {};
    Punch punchB_ {};
    uint64_t gestureAge_ = 0u;
    double crossfadePosition_ = 0.0;
    bool crossfadeInitialized_ = false;
    float deckGainA_ = 1.0f;
    float deckGainB_ = 1.0f;
    bool deckGainInitialized_ = false;
    double livePhaseBeats_ = 0.0;
    bool livePhaseInitialized_ = false;
    bool prepared_ = false;
    bool playing_ = false;
    float outputPeak_ = 0.0f;
};

} // namespace s3g::sample
