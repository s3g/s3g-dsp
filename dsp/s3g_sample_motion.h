#pragma once

#include "s3g_sample_asset.h"
#include "s3g_sample_playback.h"
#include "s3g_voice_output_allocator.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

namespace s3g::sample {

constexpr std::size_t kMaximumMotionVoices = 16u;
constexpr std::size_t kMaximumMotionSegmentLayers = 4u;
constexpr uint32_t kMaximumMotionOutputChannels = 32u;

enum class MotionMode : uint8_t {
    Hover = 0u,
    Mirror,
    Drunk,
    Zigzag,
    Forward,
    Reverse,
    MovingLoop,
    RoundTrip,
};

enum class MotionArticulation : uint8_t {
    Continuous = 0u,
    Motor,
    Packets,
};

enum class MotionRateBasis : uint8_t {
    Normal = 0u,
    Hertz,
};

enum class MotorEnvelopeShape : uint8_t {
    Linear = 0u,
    Rounded,
    Exponential,
    Plateau,
};

enum class SegmentModel : uint8_t {
    Off = 0u,
    Freeze,
    Iterate,
    Pulser,
    Doublets,
    Bounce,
    RoutedIterate,
};

enum class SegmentTrigger : uint8_t {
    Clock = 0u,
    Packet,
    Turn,
};

enum class SegmentOverlap : uint8_t {
    Cut = 0u,
    Layer,
};

enum class OutputAssignmentEvent : uint8_t {
    Note = 0u,
    Turn,
    Segment,
};

inline float motorEnvelopeLevel(float phase, float symmetry,
    MotorEnvelopeShape shape) noexcept
{
    phase = std::clamp(phase, 0.0f, 1.0f);
    symmetry = std::clamp(symmetry, 0.05f, 0.95f);
    const float linear = std::clamp(phase <= symmetry
            ? phase / symmetry : (1.0f - phase) / (1.0f - symmetry),
        0.0f, 1.0f);
    switch (shape) {
    case MotorEnvelopeShape::Rounded:
        return linear * linear * (3.0f - 2.0f * linear);
    case MotorEnvelopeShape::Exponential:
        return linear * linear;
    case MotorEnvelopeShape::Plateau:
        return std::min(1.0f, linear * 3.0f);
    case MotorEnvelopeShape::Linear:
    default:
        return linear;
    }
}

enum class MotionEventKind : uint8_t {
    NoteOn = 0u,
    NoteOff,
    Preview,
    StopAll,
};

struct MotionRenderEvent {
    uint32_t frameOffset = 0u;
    MotionEventKind kind = MotionEventKind::NoteOn;
    uint64_t noteId = 0u;
    uint8_t key = 60u;
    float velocity = 1.0f;
    uint8_t midiChannel = 0u;
};

struct MotionSettings {
    MotionMode motion = MotionMode::Hover;
    MotionArticulation articulation = MotionArticulation::Continuous;
    MotionRateBasis rateBasis = MotionRateBasis::Normal;
    MotorEnvelopeShape motorEnvelope = MotorEnvelopeShape::Rounded;
    SegmentModel segmentModel = SegmentModel::Off;
    SegmentTrigger segmentTrigger = SegmentTrigger::Clock;
    SegmentOverlap segmentOverlap = SegmentOverlap::Cut;
    OutputAssignmentEvent outputAssignmentEvent = OutputAssignmentEvent::Note;
    s3g::routing::VoiceOutputRouting outputRouting {};
    uint32_t activeOutputChannelCount = 2u;
    VoiceMode voiceMode = VoiceMode::Poly;
    TriggerMode triggerMode = TriggerMode::Gate;
    double start = 0.0;
    double end = 1.0;
    double locus = 0.5;
    double field = 0.25;
    float motionRate = 1.0f;
    float travel = 0.35f;
    float jitter = 0.0f;
    float innerRateHz = 12.0f;
    float outerRateHz = 1.0f;
    float packetDuty = 0.65f;
    float symmetry = 0.5f;
    float joinAmount = 1.0f;
    float eventRateHz = 4.0f;
    uint8_t eventRepeats = 2u;
    float eventStep = 0.05f;
    float eventPitchScatterSemitones = 0.0f;
    float eventLevelVariation = 0.15f;
    float eventIntervalCurve = 0.0f;
    float tuneSemitones = 0.0f;
    float fineTuneCents = 0.0f;
    uint8_t rootNote = 60u;
    float attackSeconds = 0.003f;
    float releaseSeconds = 0.020f;
    float velocitySensitivity = 1.0f;
    float outputGainDecibels = -6.0f;
    uint32_t seed = 1u;

    bool valid() const noexcept
    {
        return static_cast<uint8_t>(motion)
                <= static_cast<uint8_t>(MotionMode::RoundTrip)
            && static_cast<uint8_t>(articulation)
                <= static_cast<uint8_t>(MotionArticulation::Packets)
            && static_cast<uint8_t>(rateBasis)
                <= static_cast<uint8_t>(MotionRateBasis::Hertz)
            && static_cast<uint8_t>(motorEnvelope)
                <= static_cast<uint8_t>(MotorEnvelopeShape::Plateau)
            && static_cast<uint8_t>(segmentModel)
                <= static_cast<uint8_t>(SegmentModel::RoutedIterate)
            && static_cast<uint8_t>(segmentTrigger)
                <= static_cast<uint8_t>(SegmentTrigger::Turn)
            && static_cast<uint8_t>(segmentOverlap)
                <= static_cast<uint8_t>(SegmentOverlap::Layer)
            && static_cast<uint8_t>(outputAssignmentEvent)
                <= static_cast<uint8_t>(OutputAssignmentEvent::Segment)
            && outputRouting.valid()
            && activeOutputChannelCount >= 2u
            && activeOutputChannelCount <= kMaximumMotionOutputChannels
            && static_cast<uint8_t>(voiceMode)
                <= static_cast<uint8_t>(VoiceMode::Legato)
            && static_cast<uint8_t>(triggerMode)
                <= static_cast<uint8_t>(TriggerMode::Toggle)
            && std::isfinite(start) && std::isfinite(end)
            && start >= 0.0 && start < end && end <= 1.0
            && std::isfinite(locus) && locus >= 0.0 && locus <= 1.0
            && std::isfinite(field) && field >= 0.00001 && field <= 1.0
            && std::isfinite(motionRate)
            && motionRate >= 0.01f && motionRate <= 80.0f
            && std::isfinite(travel) && travel >= 0.0f && travel <= 1.0f
            && std::isfinite(jitter) && jitter >= 0.0f && jitter <= 1.0f
            && std::isfinite(innerRateHz)
            && innerRateHz >= 0.25f && innerRateHz <= 200.0f
            && std::isfinite(outerRateHz)
            && outerRateHz >= 0.05f && outerRateHz <= 20.0f
            && std::isfinite(packetDuty)
            && packetDuty >= 0.02f && packetDuty <= 1.0f
            && std::isfinite(symmetry)
            && symmetry >= 0.05f && symmetry <= 0.95f
            && std::isfinite(joinAmount)
            && joinAmount >= 0.0f && joinAmount <= 1.0f
            && std::isfinite(eventRateHz)
            && eventRateHz >= 0.25f && eventRateHz <= 80.0f
            && eventRepeats >= 1u && eventRepeats <= 16u
            && std::isfinite(eventStep)
            && eventStep >= 0.0f && eventStep <= 1.0f
            && std::isfinite(eventPitchScatterSemitones)
            && eventPitchScatterSemitones >= 0.0f
            && eventPitchScatterSemitones <= 12.0f
            && std::isfinite(eventLevelVariation)
            && eventLevelVariation >= 0.0f
            && eventLevelVariation <= 1.0f
            && std::isfinite(eventIntervalCurve)
            && eventIntervalCurve >= -1.0f
            && eventIntervalCurve <= 1.0f
            && std::isfinite(tuneSemitones)
            && std::isfinite(fineTuneCents) && rootNote < 128u
            && std::isfinite(attackSeconds)
            && attackSeconds >= 0.0f && attackSeconds <= 2.0f
            && std::isfinite(releaseSeconds)
            && releaseSeconds >= 0.0f && releaseSeconds <= 2.0f
            && std::isfinite(velocitySensitivity)
            && velocitySensitivity >= 0.0f && velocitySensitivity <= 1.0f
            && std::isfinite(outputGainDecibels) && seed != 0u;
    }
};

struct MotionVoiceCursor {
    float sourcePositionNormalized = -1.0f;
    float fieldLowNormalized = 0.0f;
    float fieldHighNormalized = 1.0f;
    float motionPhase = 0.0f;
    float innerPhase = 0.0f;
    float outerPhase = 0.0f;
    uint64_t identity = 0u;
    uint8_t key = 0u;
    bool directionForward = true;
    bool motorPacketActive = false;
    uint8_t outputFirstChannel = 0u;
    uint8_t outputSecondChannel = 0u;
    uint8_t outputChannelCount = 2u;
    uint8_t activeSegmentCount = 0u;
    uint32_t segmentEventIndex = 0u;
};

class SampleMotionEngine {
public:
    bool prepare(double outputSampleRate,
        uint32_t outputChannelCount = 2u) noexcept
    {
        if (!(outputSampleRate > 0.0) || !std::isfinite(outputSampleRate)
            || outputChannelCount < 2u
            || outputChannelCount > kMaximumMotionOutputChannels)
            return false;
        outputSampleRate_ = outputSampleRate;
        outputChannelCount_ = outputChannelCount;
        prepared_ = true;
        reset();
        return true;
    }

    void reset() noexcept
    {
        for (auto& voice : voices_) voice = {};
        for (auto& tail : tails_) tail = {};
        voiceCursors_ = {};
        voiceCursorCount_ = 0u;
        ageCounter_ = 0u;
        outputAllocator_.reset();
        outputPeak_ = 0.0f;
        segmentEventCount_ = 0u;
    }

    void setAsset(const SampleAsset* asset) noexcept
    {
        asset_ = asset && asset->valid() ? asset : nullptr;
        reset();
    }

    float outputPeak() const noexcept { return outputPeak_; }

    uint32_t activeVoiceCount() const noexcept
    {
        uint32_t count = 0u;
        for (const auto& voice : voices_) if (voice.active) ++count;
        return count;
    }

    uint32_t voiceCursorCount() const noexcept { return voiceCursorCount_; }

    uint64_t segmentEventCount() const noexcept { return segmentEventCount_; }

    const std::array<MotionVoiceCursor, kMaximumMotionVoices>&
    voiceCursors() const noexcept { return voiceCursors_; }

    float primaryPositionNormalized() const noexcept
    {
        const Voice* newest = newestVoice();
        return newest ? static_cast<float>(newest->sourcePosition) : -1.0f;
    }

    void render(const MotionSettings& settings,
        const MotionRenderEvent* events, std::size_t eventCount,
        float* left, float* right, uint32_t frameCount) noexcept
    {
        float* outputs[] { left, right };
        render(settings, events, eventCount, outputs, 2u, frameCount);
    }

    void render(const MotionSettings& settings,
        const MotionRenderEvent* events, std::size_t eventCount,
        float* const* outputs, uint32_t outputChannelCount,
        uint32_t frameCount) noexcept
    {
        voiceCursorCount_ = 0u;
        if (!outputs || outputChannelCount < 2u
            || outputChannelCount > outputChannelCount_) return;
        for (uint32_t channel = 0u; channel < outputChannelCount; ++channel) {
            if (!outputs[channel]) return;
            std::fill(outputs[channel], outputs[channel] + frameCount, 0.0f);
        }
        outputPeak_ = 0.0f;
        if (!prepared_ || !asset_ || !settings.valid() || frameCount == 0u)
            return;
        if (!events) eventCount = 0u;

        const float master = decibelsToAmplitude(settings.outputGainDecibels);
        std::size_t eventIndex = 0u;
        for (uint32_t frame = 0u; frame < frameCount; ++frame) {
            while (eventIndex < eventCount
                && events[eventIndex].frameOffset <= frame) {
                applyEvent(events[eventIndex], settings);
                ++eventIndex;
            }

            for (auto& voice : voices_) {
                if (!voice.active) continue;
                updateEnvelope(voice, settings);
                if (!voice.active) continue;
                auto bounds = fieldBounds(settings, voice);
                adaptVoiceModes(voice, settings, bounds.first, bounds.second);
                bounds = fieldBounds(settings, voice);
                voice.sourcePosition = trajectoryPosition(voice, settings,
                    bounds.first, bounds.second);
                if (settings.segmentModel == SegmentModel::Off) {
                    const float articulation = articulationGain(voice,
                        settings);
                    const float gain = voice.envelope * voice.velocityGain
                        * articulation * master;
                    const float polarity = mirrorGain(voice, settings);
                    const float leftSample = trajectorySample(0u, voice,
                        settings, bounds.first, bounds.second) * polarity;
                    const float rightSample = trajectorySample(1u, voice,
                        settings, bounds.first, bounds.second) * polarity;
                    voice.lastLeft = leftSample * gain;
                    voice.lastRight = rightSample * gain;
                    routeCurrentVoice(voice, outputs, outputChannelCount,
                        frame);
                } else {
                    serviceSegmentEvents(voice, settings, bounds.first,
                        bounds.second);
                    renderSegmentVoice(voice, settings, master, outputs,
                        outputChannelCount, frame);
                }
                advanceVoice(voice, settings, bounds.first, bounds.second);
            }

            for (auto& tail : tails_) {
                if (tail.framesRemaining == 0u) continue;
                const float fade = static_cast<float>(tail.framesRemaining)
                    / static_cast<float>(tail.frameCount);
                routeVoiceSample(tail.output, tail.left * fade,
                    tail.right * fade, outputs, outputChannelCount, frame);
                if (--tail.framesRemaining == 0u) tail = {};
            }
            for (uint32_t channel = 0u; channel < outputChannelCount;
                 ++channel)
                outputPeak_ = std::max(outputPeak_,
                    std::abs(outputs[channel][frame]));
        }
        while (eventIndex < eventCount
            && events[eventIndex].frameOffset <= frameCount) {
            applyEvent(events[eventIndex], settings);
            ++eventIndex;
        }
        publishCursors(settings);
    }

private:
    struct SegmentLane {
        bool active = false;
        bool directionForward = true;
        uint64_t age = 0u;
        double low = 0.0;
        double high = 1.0;
        double phase = 0.0;
        float gain = 1.0f;
        float pitchRatio = 1.0f;
        float lastLeft = 0.0f;
        float lastRight = 0.0f;
        s3g::routing::VoiceOutputAssignment output {};
    };

    struct Voice {
        bool active = false;
        bool releasing = false;
        bool directionForward = true;
        bool packetActive = false;
        uint64_t noteId = 0u;
        uint64_t age = 0u;
        uint8_t key = 60u;
        uint8_t midiChannel = 0u;
        float velocityGain = 1.0f;
        float envelope = 0.0f;
        float releaseDecrement = 0.0f;
        float motionRateScale = 1.0f;
        float innerRateScale = 1.0f;
        float eventRateScale = 1.0f;
        float lastLeft = 0.0f;
        float lastRight = 0.0f;
        double sourcePosition = 0.5;
        double motionPhase = 0.0;
        double innerPhase = 0.0;
        double outerPhase = 0.0;
        double drunkA = 0.5;
        double drunkB = 0.5;
        uint32_t randomState = 1u;
        bool zigForward = true;
        bool eventStarted = false;
        bool pendingPacketEvent = false;
        bool pendingTurnEvent = false;
        double eventPhase = 0.0;
        double eventBasePosition = 0.5;
        double movingLoopCenter = 0.5;
        uint32_t eventIndex = 0u;
        uint8_t eventRepeatIndex = 0u;
        MotionMode motion = MotionMode::Hover;
        MotionArticulation articulation = MotionArticulation::Continuous;
        SegmentModel segmentModel = SegmentModel::Off;
        s3g::routing::VoiceOutputAssignment output {};
        s3g::routing::VoiceOutputAssignment previousOutput {};
        uint32_t routeFadeRemaining = 0u;
        uint32_t routeFadeFrameCount = 0u;
        std::array<SegmentLane, kMaximumMotionSegmentLayers> segments {};
    };

    struct Tail {
        float left = 0.0f;
        float right = 0.0f;
        uint32_t framesRemaining = 0u;
        uint32_t frameCount = 0u;
        s3g::routing::VoiceOutputAssignment output {};
    };

    static constexpr double kPi = 3.14159265358979323846;

    static float decibelsToAmplitude(float decibels) noexcept
    {
        return std::pow(10.0f, decibels / 20.0f);
    }

    static double wrapPhase(double phase) noexcept
    {
        phase -= std::floor(phase);
        return phase < 0.0 ? phase + 1.0 : phase;
    }

    static uint32_t nextRandom(uint32_t& state) noexcept
    {
        if (state == 0u) state = 0x9e3779b9u;
        state ^= state << 13u;
        state ^= state >> 17u;
        state ^= state << 5u;
        return state;
    }

    static float randomBipolar(uint32_t& state) noexcept
    {
        return static_cast<float>((nextRandom(state) >> 8u)
            * (1.0 / 16777215.0) * 2.0 - 1.0);
    }

    static float randomUnit(uint32_t& state) noexcept
    {
        return static_cast<float>((nextRandom(state) >> 8u)
            * (1.0 / 16777215.0));
    }

    std::pair<double, double> fieldBounds(
        const MotionSettings& settings) const noexcept
    {
        const double length = settings.end - settings.start;
        const double half = std::max(0.5 / std::max<double>(
            1.0, asset_->frameCount() - 1.0), settings.field * 0.5);
        const double center = std::clamp(settings.locus,
            settings.start, settings.end);
        double low = std::max(settings.start, center - half);
        double high = std::min(settings.end, center + half);
        if (high <= low) {
            low = settings.start;
            high = settings.start + length;
        }
        return { low, high };
    }

    std::pair<double, double> fieldBounds(const MotionSettings& settings,
        const Voice& voice) const noexcept
    {
        if (settings.motion != MotionMode::MovingLoop)
            return fieldBounds(settings);
        const double length = settings.end - settings.start;
        const double width = std::min(length, std::max(
            1.0 / std::max<double>(1.0, asset_->frameCount() - 1.0),
            settings.field));
        const double minimumCenter = settings.start + width * 0.5;
        const double maximumCenter = settings.end - width * 0.5;
        const double center = std::clamp(voice.movingLoopCenter,
            minimumCenter, maximumCenter);
        return { center - width * 0.5, center + width * 0.5 };
    }

    static double wrapRange(double value, double low, double high) noexcept
    {
        const double width = high - low;
        if (!(width > 0.0)) return low;
        value = std::fmod(value - low, width);
        if (value < 0.0) value += width;
        return low + value;
    }

    float sourceSample(uint8_t outputChannel, double normalized) const noexcept
    {
        if (!asset_ || asset_->frameCount() == 0u) return 0.0f;
        const uint8_t sourceChannel = asset_->channelCount == 1u
            ? 0u : std::min<uint8_t>(outputChannel,
                static_cast<uint8_t>(asset_->channelCount - 1u));
        const auto& samples = asset_->channels[sourceChannel];
        const double position = std::clamp(normalized, 0.0, 1.0)
            * static_cast<double>(samples.size() - 1u);
        const auto first = static_cast<std::size_t>(std::floor(position));
        const auto second = std::min(first + 1u, samples.size() - 1u);
        const float fraction = static_cast<float>(position - first);
        return samples[first] + (samples[second] - samples[first]) * fraction;
    }

    float trajectorySample(uint8_t sourceChannel, const Voice& voice,
        const MotionSettings& settings, double low, double high) const noexcept
    {
        const float primary = sourceSample(sourceChannel,
            voice.sourcePosition);
        if ((settings.motion != MotionMode::Forward
                && settings.motion != MotionMode::Reverse
                && settings.motion != MotionMode::MovingLoop)
            || settings.joinAmount <= 0.0f) return primary;
        const double width = std::clamp(
            static_cast<double>(settings.joinAmount) * 0.08, 0.0, 0.08);
        if (width <= 0.0 || voice.motionPhase < 1.0 - width)
            return primary;
        const double local = voice.motionPhase - (1.0 - width);
        const double alternate = settings.motion != MotionMode::Reverse
            ? low + local * (high - low)
            : high - local * (high - low);
        const float mix = static_cast<float>(std::clamp(local / width,
            0.0, 1.0));
        const float smooth = mix * mix * (3.0f - 2.0f * mix);
        return primary + (sourceSample(sourceChannel, alternate) - primary)
            * smooth;
    }

    static void routeVoiceSample(
        const s3g::routing::VoiceOutputAssignment& output,
        float left, float right, float* const* outputs,
        uint32_t outputChannelCount, uint32_t frame) noexcept
    {
        if (output.channelCount <= 1u) {
            if (output.firstChannel < outputChannelCount)
                outputs[output.firstChannel][frame] += 0.5f * (left + right);
            return;
        }
        if (output.firstChannel < outputChannelCount)
            outputs[output.firstChannel][frame] += left;
        if (output.secondChannel < outputChannelCount)
            outputs[output.secondChannel][frame] += right;
    }

    void routeCurrentVoice(Voice& voice, float* const* outputs,
        uint32_t outputChannelCount, uint32_t frame) noexcept
    {
        if (voice.routeFadeRemaining == 0u) {
            routeVoiceSample(voice.output, voice.lastLeft, voice.lastRight,
                outputs, outputChannelCount, frame);
            return;
        }
        const float oldGain = static_cast<float>(voice.routeFadeRemaining)
            / static_cast<float>(voice.routeFadeFrameCount);
        const float newGain = 1.0f - oldGain;
        routeVoiceSample(voice.previousOutput, voice.lastLeft * oldGain,
            voice.lastRight * oldGain, outputs, outputChannelCount, frame);
        routeVoiceSample(voice.output, voice.lastLeft * newGain,
            voice.lastRight * newGain, outputs, outputChannelCount, frame);
        --voice.routeFadeRemaining;
    }

    s3g::routing::VoiceOutputAssignment nextOutput(
        const MotionSettings& settings) noexcept
    {
        return outputAllocator_.next(
            std::min(outputChannelCount_, settings.activeOutputChannelCount),
            settings.outputRouting);
    }

    s3g::routing::VoiceOutputAssignment initialOutput(
        const MotionSettings& settings) noexcept
    {
        const bool segmentAssigned = settings.outputAssignmentEvent
                == OutputAssignmentEvent::Segment
            || settings.segmentModel == SegmentModel::RoutedIterate;
        if (!segmentAssigned) return nextOutput(settings);
        s3g::routing::VoiceOutputAssignment output;
        const uint32_t channels = std::min(outputChannelCount_,
            settings.activeOutputChannelCount);
        const bool stereo = settings.outputRouting.width
                == s3g::routing::OutputVoiceWidth::Stereo
            && channels >= 2u;
        output.channelCount = stereo ? 2u : 1u;
        output.firstChannel = 0u;
        output.secondChannel = stereo
            && settings.outputRouting.pairLayout
                == s3g::routing::StereoPairLayout::SplitBanks
            ? static_cast<uint8_t>(channels / 2u)
            : static_cast<uint8_t>(stereo ? 1u : 0u);
        return output;
    }

    void reassignVoiceOutput(Voice& voice,
        const MotionSettings& settings) noexcept
    {
        const auto next = nextOutput(settings);
        if (next.firstChannel == voice.output.firstChannel
            && next.secondChannel == voice.output.secondChannel
            && next.channelCount == voice.output.channelCount) return;
        voice.previousOutput = voice.output;
        voice.output = next;
        const uint32_t frames = static_cast<uint32_t>(std::lround(
            outputSampleRate_ * 0.010 * settings.joinAmount));
        voice.routeFadeRemaining = frames;
        voice.routeFadeFrameCount = std::max(1u, frames);
    }

    std::pair<double, double> centeredSegment(double center, double width,
        const MotionSettings& settings) const noexcept
    {
        const double minimumWidth = 1.0 / std::max<double>(1.0,
            asset_->frameCount() - 1.0);
        width = std::clamp(width, minimumWidth,
            settings.end - settings.start);
        center = std::clamp(center, settings.start, settings.end);
        double low = center - width * 0.5;
        double high = center + width * 0.5;
        if (low < settings.start) {
            high += settings.start - low;
            low = settings.start;
        }
        if (high > settings.end) {
            low -= high - settings.end;
            high = settings.end;
        }
        return { std::max(settings.start, low),
            std::min(settings.end, high) };
    }

    double sourceRateNormalizedPerSecond(const Voice& voice,
        const MotionSettings& settings, double segmentWidth,
        float pitchRatio = 1.0f) const noexcept
    {
        double normalized = 0.0;
        if (settings.rateBasis == MotionRateBasis::Normal) {
            normalized = asset_->sampleRate
                / std::max<double>(1.0, asset_->frameCount() - 1.0)
                * settings.motionRate;
        } else normalized = segmentWidth * settings.motionRate;
        return normalized * noteRatio(voice, settings) * pitchRatio;
    }

    double effectiveEventRate(const Voice& voice,
        const MotionSettings& settings) const noexcept
    {
        const double curve = std::clamp<double>(
            settings.eventIntervalCurve * voice.eventRepeatIndex,
            -3.0, 3.0);
        return std::clamp<double>(settings.eventRateHz
                * voice.eventRateScale * std::pow(2.0, curve),
            0.03125, 640.0);
    }

    uint8_t activeSegmentCount(const Voice& voice) const noexcept
    {
        uint8_t count = 0u;
        for (const auto& lane : voice.segments) if (lane.active) ++count;
        return count;
    }

    SegmentLane* allocateSegmentLane(Voice& voice,
        SegmentOverlap overlap) noexcept
    {
        if (overlap == SegmentOverlap::Cut) {
            for (auto& lane : voice.segments) {
                if (lane.active) retireSegmentLane(lane);
                lane = {};
            }
        }
        for (auto& lane : voice.segments) if (!lane.active) return &lane;
        SegmentLane* oldest = &*std::min_element(voice.segments.begin(),
            voice.segments.end(), [](const SegmentLane& left,
                const SegmentLane& right) { return left.age < right.age; });
        retireSegmentLane(*oldest);
        return oldest;
    }

    void startSegmentEvent(Voice& voice, const MotionSettings& settings,
        double fieldLow, double fieldHigh) noexcept
    {
        const double sourceWidth = settings.end - settings.start;
        const double fieldWidth = std::max(fieldHigh - fieldLow,
            1.0 / std::max<double>(1.0, asset_->frameCount() - 1.0));
        double center = std::clamp(voice.eventBasePosition,
            settings.start, settings.end);
        double width = fieldWidth;
        switch (settings.segmentModel) {
        case SegmentModel::Freeze:
            center = std::clamp(settings.locus,
                settings.start, settings.end);
            break;
        case SegmentModel::Bounce: {
            center = std::clamp(settings.locus,
                settings.start, settings.end);
            const float progress = settings.eventRepeats > 1u
                ? static_cast<float>(voice.eventRepeatIndex)
                    / static_cast<float>(settings.eventRepeats - 1u)
                : 0.0f;
            const float minimum = std::clamp(settings.packetDuty,
                0.02f, 1.0f);
            width *= 1.0 - static_cast<double>(progress * (1.0f - minimum));
            break;
        }
        case SegmentModel::Iterate:
        case SegmentModel::RoutedIterate:
            if (settings.eventStep <= 0.0f)
                center = voice.sourcePosition;
            break;
        case SegmentModel::Pulser: {
            center = fieldLow + randomUnit(voice.randomState) * fieldWidth;
            const double seconds = settings.packetDuty
                / std::max<double>(0.25, effectiveEventRate(voice, settings));
            width = std::min(fieldWidth,
                sourceRateNormalizedPerSecond(voice, settings, fieldWidth)
                    * seconds);
            break;
        }
        case SegmentModel::Doublets:
            break;
        case SegmentModel::Off:
        default:
            return;
        }
        const auto range = centeredSegment(center, width, settings);
        SegmentLane* lane = allocateSegmentLane(voice,
            settings.segmentOverlap);
        if (!lane) return;
        *lane = {};
        lane->active = true;
        lane->age = ++segmentEventCount_;
        lane->low = range.first;
        lane->high = range.second;
        lane->directionForward = trajectoryForward(voice);
        lane->gain = settings.segmentModel == SegmentModel::Bounce
            ? static_cast<float>(std::pow(
                std::max(0.0f, 1.0f - settings.eventLevelVariation),
                voice.eventRepeatIndex))
            : 1.0f - randomUnit(voice.randomState)
                * settings.eventLevelVariation;
        lane->pitchRatio = std::pow(2.0f,
            randomBipolar(voice.randomState)
                * settings.eventPitchScatterSemitones / 12.0f);
        const bool routeSegment = settings.outputAssignmentEvent
                == OutputAssignmentEvent::Segment
            || settings.segmentModel == SegmentModel::RoutedIterate;
        if (routeSegment) {
            lane->output = nextOutput(settings);
            voice.output = lane->output;
        } else lane->output = voice.output;

        ++voice.eventIndex;
        ++voice.eventRepeatIndex;
        if (voice.eventRepeatIndex >= settings.eventRepeats)
            voice.eventRepeatIndex = 0u;
        const double step = settings.eventStep * sourceWidth;
        if (settings.segmentModel == SegmentModel::Doublets) {
            if (voice.eventRepeatIndex == 0u)
                voice.eventBasePosition = wrapRange(
                    voice.eventBasePosition + step,
                    settings.start, settings.end);
        } else if ((settings.segmentModel == SegmentModel::Iterate
                || settings.segmentModel == SegmentModel::RoutedIterate)
            && step > 0.0) {
            voice.eventBasePosition = wrapRange(
                voice.eventBasePosition + step,
                settings.start, settings.end);
        }
    }

    void serviceSegmentEvents(Voice& voice, const MotionSettings& settings,
        double fieldLow, double fieldHigh) noexcept
    {
        bool trigger = !voice.eventStarted;
        if (settings.segmentTrigger == SegmentTrigger::Clock) {
            if (settings.segmentModel == SegmentModel::Doublets)
                trigger = trigger || activeSegmentCount(voice) == 0u;
            else trigger = trigger || voice.eventPhase >= 1.0;
        }
        else if (settings.segmentTrigger == SegmentTrigger::Packet)
            trigger = trigger || voice.pendingPacketEvent;
        else trigger = trigger || voice.pendingTurnEvent;
        voice.pendingPacketEvent = false;
        voice.pendingTurnEvent = false;
        if (!trigger) return;
        voice.eventPhase = wrapPhase(voice.eventPhase);
        voice.eventStarted = true;
        startSegmentEvent(voice, settings, fieldLow, fieldHigh);
        voice.eventRateScale = std::max(0.10f, 1.0f
            + randomBipolar(voice.randomState) * settings.jitter * 0.75f);
    }

    void renderSegmentVoice(Voice& voice, const MotionSettings& settings,
        float master, float* const* outputs, uint32_t outputChannelCount,
        uint32_t frame) noexcept
    {
        voice.lastLeft = 0.0f;
        voice.lastRight = 0.0f;
        SegmentLane* newest = nullptr;
        for (auto& lane : voice.segments) {
            if (!lane.active) continue;
            const double width = std::max(lane.high - lane.low,
                1.0 / std::max<double>(1.0,
                    asset_->frameCount() - 1.0));
            const double position = lane.directionForward
                ? lane.low + lane.phase * width
                : lane.high - lane.phase * width;
            const float shaped = motorEnvelopeLevel(
                static_cast<float>(lane.phase), settings.symmetry,
                settings.motorEnvelope);
            const float eventEnvelope = 1.0f
                + (shaped - 1.0f) * settings.joinAmount;
            const float gain = voice.envelope * voice.velocityGain * master
                * lane.gain * eventEnvelope;
            const float left = sourceSample(0u, position) * gain;
            const float right = sourceSample(1u, position) * gain;
            lane.lastLeft = left;
            lane.lastRight = right;
            routeVoiceSample(lane.output, left, right, outputs,
                outputChannelCount, frame);
            voice.lastLeft += left;
            voice.lastRight += right;
            if (!newest || lane.age > newest->age) newest = &lane;
            lane.phase += sourceRateNormalizedPerSecond(voice, settings,
                width, lane.pitchRatio) / width / outputSampleRate_;
            if (lane.phase >= 1.0) lane.active = false;
        }
        voice.packetActive = activeSegmentCount(voice) != 0u;
        if (newest) {
            const double width = newest->high - newest->low;
            voice.sourcePosition = newest->directionForward
                ? newest->low + std::min(1.0, newest->phase) * width
                : newest->high - std::min(1.0, newest->phase) * width;
        }
    }

    double trajectoryPosition(const Voice& voice,
        const MotionSettings& settings, double low, double high) const noexcept
    {
        const double center = 0.5 * (low + high);
        const double span = 0.5 * (high - low);
        switch (settings.motion) {
        case MotionMode::Mirror:
        case MotionMode::RoundTrip: {
            const double triangle = voice.motionPhase < 0.5
                ? voice.motionPhase * 2.0
                : 2.0 - voice.motionPhase * 2.0;
            return low + triangle * (high - low);
        }
        case MotionMode::Drunk:
        case MotionMode::Zigzag: {
            const double linear = voice.motionPhase;
            const double smooth = linear * linear * (3.0 - 2.0 * linear);
            const double amount = linear
                + (smooth - linear) * settings.joinAmount;
            return std::clamp(voice.drunkA
                + (voice.drunkB - voice.drunkA) * amount, low, high);
        }
        case MotionMode::Forward:
        case MotionMode::MovingLoop:
            return low + voice.motionPhase * (high - low);
        case MotionMode::Reverse:
            return high - voice.motionPhase * (high - low);
        case MotionMode::Hover:
        default:
            return center + span * std::sin(2.0 * kPi * voice.motionPhase);
        }
    }

    static float mirrorGain(const Voice& voice,
        const MotionSettings& settings) noexcept
    {
        if (settings.motion != MotionMode::Mirror) return 1.0f;
        const float phase = static_cast<float>(voice.motionPhase);
        const float polarity = phase < 0.5f ? 1.0f : -1.0f;
        const float transitionWidth = settings.joinAmount * 0.08f;
        if (transitionWidth <= 0.0f) return polarity;
        const float distance = std::min({ std::abs(phase - 0.5f),
            phase, 1.0f - phase });
        if (distance >= transitionWidth) return polarity;
        const float shaped = std::sin(static_cast<float>(0.5 * kPi)
            * distance / transitionWidth);
        return polarity * shaped;
    }

    static float articulationGain(Voice& voice,
        const MotionSettings& settings) noexcept
    {
        if (settings.articulation == MotionArticulation::Continuous) {
            voice.packetActive = true;
            return 1.0f;
        }
        const float inner = static_cast<float>(voice.innerPhase);
        voice.packetActive = inner < settings.packetDuty;
        if (!voice.packetActive) return 0.0f;
        const float packetPhase = std::clamp(inner / settings.packetDuty,
            0.0f, 1.0f);
        const float hann = 0.5f - 0.5f
            * std::cos(static_cast<float>(2.0 * kPi) * packetPhase);
        const float packet = 1.0f + (hann - 1.0f) * settings.joinAmount;
        if (settings.articulation == MotionArticulation::Packets)
            return packet;
        return packet * motorEnvelopeLevel(
            static_cast<float>(voice.outerPhase), settings.symmetry,
            settings.motorEnvelope);
    }

    double noteRatio(const Voice& voice,
        const MotionSettings& settings) const noexcept
    {
        const double semitones = static_cast<double>(voice.key)
            - settings.rootNote + settings.tuneSemitones
            + settings.fineTuneCents / 100.0;
        return std::pow(2.0, semitones / 12.0);
    }

    void chooseDrunkTarget(Voice& voice, const MotionSettings& settings,
        double low, double high, bool forward) noexcept
    {
        const double width = high - low;
        const double step = width * settings.travel;
        const double origin = forward ? voice.drunkA : voice.drunkB;
        const double candidate = std::clamp(origin
            + randomBipolar(voice.randomState) * step, low, high);
        if (forward) voice.drunkB = candidate;
        else voice.drunkA = candidate;
        voice.motionRateScale = std::max(0.05f, 1.0f
            + randomBipolar(voice.randomState) * settings.jitter * 0.75f);
    }

    void chooseZigTarget(Voice& voice, const MotionSettings& settings,
        double low, double high, bool targetIsB) noexcept
    {
        const double width = high - low;
        const double maximum = width * settings.travel;
        const double unit = static_cast<double>(nextRandom(voice.randomState)
            >> 8u) / 16777215.0;
        const double distance = maximum
            * (1.0 - unit * settings.jitter * 0.80);
        const double origin = targetIsB ? voice.drunkA : voice.drunkB;
        const auto candidateFor = [&](bool directionAB) {
            const double sign = targetIsB
                ? (directionAB ? 1.0 : -1.0)
                : (directionAB ? -1.0 : 1.0);
            return std::clamp(origin + sign * distance, low, high);
        };
        double candidate = candidateFor(voice.zigForward);
        if (distance > 0.0 && std::abs(candidate - origin) < 1.0e-12) {
            voice.zigForward = !voice.zigForward;
            candidate = candidateFor(voice.zigForward);
        }
        if (targetIsB) voice.drunkB = candidate;
        else voice.drunkA = candidate;
        updateMotionRateScale(voice, settings);
    }

    void updateMotionRateScale(Voice& voice,
        const MotionSettings& settings) noexcept
    {
        voice.motionRateScale = std::max(0.05f, 1.0f
            + randomBipolar(voice.randomState) * settings.jitter * 0.75f);
    }

    double motionPhaseRateHz(const Voice& voice,
        const MotionSettings& settings, double low, double high) const noexcept
    {
        if (settings.rateBasis == MotionRateBasis::Hertz)
            return settings.motionRate;
        const double frameSpan = std::max<double>(1.0,
            asset_->frameCount() - 1.0);
        const double normalizedPerSecond = asset_->sampleRate / frameSpan;
        const double fieldWidth = std::max(high - low, 1.0 / frameSpan);
        double pathLength = 2.0 * fieldWidth;
        if (settings.motion == MotionMode::Forward
            || settings.motion == MotionMode::Reverse
            || settings.motion == MotionMode::MovingLoop)
            pathLength = fieldWidth;
        else if (settings.motion == MotionMode::Drunk
            || settings.motion == MotionMode::Zigzag) {
            if (settings.travel <= 0.0f) return 0.0;
            pathLength = std::max(std::abs(voice.drunkB - voice.drunkA),
                1.0 / frameSpan);
        }
        return static_cast<double>(settings.motionRate)
            * normalizedPerSecond / pathLength;
    }

    void advanceVoice(Voice& voice, const MotionSettings& settings,
        double low, double high) noexcept
    {
        bool forward = true;
        if (settings.motion != MotionMode::Forward
            && settings.motion != MotionMode::Reverse
            && settings.motion != MotionMode::MovingLoop
            && settings.articulation == MotionArticulation::Motor)
            forward = voice.outerPhase <= settings.symmetry;
        const bool intrinsicForward = trajectoryForward(voice);
        voice.directionForward = forward
            ? intrinsicForward : !intrinsicForward;
        const double increment = motionPhaseRateHz(voice, settings,
                low, high)
            * voice.motionRateScale * noteRatio(voice, settings)
            / outputSampleRate_ * (forward ? 1.0 : -1.0);
        const double previousMotion = voice.motionPhase;
        voice.motionPhase += increment;
        bool turnBoundary = false;
        bool wrappedMotion = false;
        if (voice.motionPhase >= 1.0) {
            voice.motionPhase -= 1.0;
            wrappedMotion = true;
            turnBoundary = settings.motion != MotionMode::Hover;
            if (settings.motion == MotionMode::Drunk) {
                voice.drunkA = voice.drunkB;
                chooseDrunkTarget(voice, settings, low, high, true);
            } else if (settings.motion == MotionMode::Zigzag) {
                voice.drunkA = voice.drunkB;
                voice.zigForward = !voice.zigForward;
                chooseZigTarget(voice, settings, low, high, true);
            } else {
                if (settings.motion == MotionMode::MovingLoop) {
                    const double width = high - low;
                    const double minimum = settings.start + width * 0.5;
                    const double maximum = settings.end - width * 0.5;
                    voice.movingLoopCenter = wrapRange(
                        voice.movingLoopCenter
                            + settings.eventStep
                                * (settings.end - settings.start),
                        minimum, std::nextafter(maximum, minimum));
                }
                updateMotionRateScale(voice, settings);
            }
        } else if (voice.motionPhase < 0.0) {
            voice.motionPhase += 1.0;
            wrappedMotion = true;
            turnBoundary = settings.motion != MotionMode::Hover;
            if (settings.motion == MotionMode::Drunk) {
                voice.drunkB = voice.drunkA;
                chooseDrunkTarget(voice, settings, low, high, false);
            } else if (settings.motion == MotionMode::Zigzag) {
                voice.drunkB = voice.drunkA;
                voice.zigForward = !voice.zigForward;
                chooseZigTarget(voice, settings, low, high, false);
            } else updateMotionRateScale(voice, settings);
        }
        if (!wrappedMotion && (settings.motion == MotionMode::Mirror
                || settings.motion == MotionMode::RoundTrip)
            && ((previousMotion < 0.5 && voice.motionPhase >= 0.5)
                || (previousMotion >= 0.5 && voice.motionPhase < 0.5)))
            turnBoundary = true;
        if (!wrappedMotion && settings.motion == MotionMode::Hover) {
            if (increment >= 0.0) {
                turnBoundary = (previousMotion < 0.25
                        && voice.motionPhase >= 0.25)
                    || (previousMotion < 0.75
                        && voice.motionPhase >= 0.75);
            } else {
                turnBoundary = (previousMotion > 0.75
                        && voice.motionPhase <= 0.75)
                    || (previousMotion > 0.25
                        && voice.motionPhase <= 0.25);
            }
        }
        if (turnBoundary) {
            voice.pendingTurnEvent = true;
            if (settings.outputAssignmentEvent
                    == OutputAssignmentEvent::Turn)
                reassignVoiceOutput(voice, settings);
        }

        const bool needsPacketClock = settings.articulation
                != MotionArticulation::Continuous
            || (settings.segmentModel != SegmentModel::Off
                && settings.segmentTrigger == SegmentTrigger::Packet);
        if (needsPacketClock) {
            const double previousInner = voice.innerPhase;
            voice.innerPhase = wrapPhase(voice.innerPhase
                + settings.innerRateHz * voice.innerRateScale
                    / outputSampleRate_);
            if (voice.innerPhase < previousInner) {
                voice.pendingPacketEvent = true;
                voice.innerRateScale = std::max(0.05f, 1.0f
                    + randomBipolar(voice.randomState)
                        * settings.jitter * 0.5f);
            }
        }
        if (settings.articulation == MotionArticulation::Motor)
            voice.outerPhase = wrapPhase(voice.outerPhase
                + settings.outerRateHz / outputSampleRate_);
        if (settings.segmentModel != SegmentModel::Off
            && settings.segmentTrigger == SegmentTrigger::Clock)
            voice.eventPhase += effectiveEventRate(voice, settings)
                / outputSampleRate_;
    }

    void updateEnvelope(Voice& voice,
        const MotionSettings& settings) noexcept
    {
        if (voice.releasing) {
            if (settings.releaseSeconds <= 0.0f) {
                retireVoice(voice);
                return;
            }
            if (!(voice.releaseDecrement > 0.0f))
                voice.releaseDecrement = voice.envelope
                    / static_cast<float>(std::max(1.0,
                        settings.releaseSeconds * outputSampleRate_));
            voice.envelope -= voice.releaseDecrement;
            if (voice.envelope <= 0.0f) retireVoice(voice);
        } else if (voice.envelope < 1.0f) {
            if (settings.attackSeconds <= 0.0f) voice.envelope = 1.0f;
            else voice.envelope = std::min(1.0f, voice.envelope
                + 1.0f / static_cast<float>(std::max(1.0,
                    settings.attackSeconds * outputSampleRate_)));
        }
    }

    void retireSegmentLane(const SegmentLane& lane) noexcept
    {
        if (!lane.active
            || (lane.lastLeft == 0.0f && lane.lastRight == 0.0f)) return;
        Tail* selected = nullptr;
        for (auto& tail : tails_) {
            if (tail.framesRemaining == 0u) {
                selected = &tail;
                break;
            }
        }
        if (!selected) selected = &tails_[0u];
        const uint32_t frames = std::max<uint32_t>(1u,
            static_cast<uint32_t>(std::lround(outputSampleRate_ * 0.003)));
        *selected = { lane.lastLeft, lane.lastRight, frames, frames,
            lane.output };
    }

    void retireVoice(Voice& voice) noexcept
    {
        if (!voice.active) return;
        if (voice.segmentModel != SegmentModel::Off) {
            for (const auto& lane : voice.segments)
                if (lane.active) retireSegmentLane(lane);
            voice = {};
            return;
        }
        Tail* selected = nullptr;
        for (auto& tail : tails_) {
            if (tail.framesRemaining == 0u) {
                selected = &tail;
                break;
            }
        }
        if (!selected) selected = &tails_[0u];
        const uint32_t frames = std::max<uint32_t>(1u,
            static_cast<uint32_t>(std::lround(outputSampleRate_ * 0.003)));
        *selected = { voice.lastLeft, voice.lastRight, frames, frames,
            voice.output };
        voice = {};
    }

    Voice* newestVoice() noexcept
    {
        Voice* newest = nullptr;
        for (auto& voice : voices_)
            if (voice.active && (!newest || voice.age > newest->age))
                newest = &voice;
        return newest;
    }

    const Voice* newestVoice() const noexcept
    {
        const Voice* newest = nullptr;
        for (const auto& voice : voices_)
            if (voice.active && (!newest || voice.age > newest->age))
                newest = &voice;
        return newest;
    }

    Voice* findMatching(const MotionRenderEvent& event) noexcept
    {
        Voice* newest = nullptr;
        for (auto& voice : voices_) {
            if (!voice.active || voice.key != event.key
                || voice.midiChannel != event.midiChannel) continue;
            if (event.noteId != 0u && voice.noteId != event.noteId) continue;
            if (!newest || voice.age > newest->age) newest = &voice;
        }
        return newest;
    }

    static bool eventMatches(const MotionRenderEvent& event,
        const Voice& voice) noexcept
    {
        return voice.active && voice.key == event.key
            && voice.midiChannel == event.midiChannel
            && (event.noteId == 0u || voice.noteId == event.noteId);
    }

    void beginRelease(Voice& voice,
        const MotionSettings& settings) noexcept
    {
        if (!voice.active || voice.releasing) return;
        if (settings.releaseSeconds <= 0.0f) {
            retireVoice(voice);
            return;
        }
        voice.releasing = true;
        voice.releaseDecrement = voice.envelope
            / static_cast<float>(std::max(1.0,
                settings.releaseSeconds * outputSampleRate_));
    }

    static bool trajectoryForward(const Voice& voice) noexcept
    {
        if (voice.motion == MotionMode::Forward
            || voice.motion == MotionMode::MovingLoop) return true;
        if (voice.motion == MotionMode::Reverse) return false;
        if (voice.motion == MotionMode::Mirror
            || voice.motion == MotionMode::RoundTrip)
            return voice.motionPhase < 0.5;
        if (voice.motion == MotionMode::Hover)
            return voice.motionPhase < 0.25 || voice.motionPhase > 0.75;
        return voice.drunkB >= voice.drunkA;
    }

    double phaseForPosition(MotionMode motion, double position,
        double low, double high, bool forward) const noexcept
    {
        const double width = std::max(high - low,
            1.0 / std::max<double>(1.0, asset_->frameCount() - 1.0));
        const double normalized = std::clamp((position - low) / width,
            0.0, 1.0);
        if (motion == MotionMode::Mirror || motion == MotionMode::RoundTrip)
            return forward ? normalized * 0.5 : 1.0 - normalized * 0.5;
        if (motion == MotionMode::Hover) {
            const double sine = normalized * 2.0 - 1.0;
            const double angle = std::asin(std::clamp(sine, -1.0, 1.0));
            return wrapPhase(forward ? angle / (2.0 * kPi)
                : (kPi - angle) / (2.0 * kPi));
        }
        if (motion == MotionMode::Forward || motion == MotionMode::MovingLoop)
            return normalized;
        if (motion == MotionMode::Reverse) return 1.0 - normalized;
        return 0.0;
    }

    void adaptVoiceModes(Voice& voice, const MotionSettings& settings,
        double low, double high) noexcept
    {
        if (voice.motion != settings.motion) {
            const bool forward = trajectoryForward(voice);
            const double position = std::clamp(voice.sourcePosition,
                low, high);
            voice.motion = settings.motion;
            if (settings.motion == MotionMode::MovingLoop)
                voice.movingLoopCenter = std::clamp(position,
                    settings.start, settings.end);
            if (settings.motion == MotionMode::Drunk
                || settings.motion == MotionMode::Zigzag) {
                voice.motionPhase = 0.0;
                voice.drunkA = position;
                voice.drunkB = position;
                voice.zigForward = true;
                if (settings.motion == MotionMode::Zigzag)
                    chooseZigTarget(voice, settings, low, high, true);
                else chooseDrunkTarget(voice, settings, low, high, true);
            } else {
                voice.motionPhase = phaseForPosition(settings.motion,
                    position, low, high, forward);
            }
        }
        if (voice.articulation != settings.articulation) {
            voice.articulation = settings.articulation;
            if (settings.articulation != MotionArticulation::Continuous) {
                voice.innerPhase = settings.packetDuty * 0.5;
                voice.packetActive = true;
            }
            if (settings.articulation == MotionArticulation::Motor)
                voice.outerPhase = settings.symmetry;
        }
        if (voice.segmentModel != settings.segmentModel) {
            voice.segmentModel = settings.segmentModel;
            voice.eventStarted = false;
            voice.eventPhase = 0.0;
            voice.eventIndex = 0u;
            voice.eventRepeatIndex = 0u;
            voice.eventRateScale = 1.0f;
            voice.eventBasePosition = std::clamp(settings.locus,
                settings.start, settings.end);
            for (auto& lane : voice.segments) lane = {};
        }
    }

    Voice* allocateVoice() noexcept
    {
        for (auto& voice : voices_) if (!voice.active) return &voice;
        return &*std::min_element(voices_.begin(), voices_.end(),
            [](const Voice& left, const Voice& right) {
                return left.age < right.age;
            });
    }

    void startVoice(Voice& voice, const MotionRenderEvent& event,
        const MotionSettings& settings, bool preserveMotion) noexcept
    {
        const Voice oldVoice = voice;
        const double oldMotion = voice.motionPhase;
        const double oldInner = voice.innerPhase;
        const double oldOuter = voice.outerPhase;
        const double oldA = voice.drunkA;
        const double oldB = voice.drunkB;
        const bool oldZigForward = voice.zigForward;
        const float oldEnvelope = voice.envelope;
        const auto oldOutput = voice.output;
        if (voice.active && !preserveMotion) retireVoice(voice);
        voice = {};
        voice.active = true;
        voice.noteId = event.noteId;
        voice.age = ++ageCounter_;
        voice.key = event.key;
        voice.midiChannel = event.midiChannel;
        voice.velocityGain = 1.0f + (std::clamp(event.velocity, 0.0f, 1.0f)
            - 1.0f) * settings.velocitySensitivity;
        voice.envelope = preserveMotion ? oldEnvelope : 0.0f;
        voice.randomState = settings.seed
            ^ static_cast<uint32_t>(voice.age * 0x9e3779b9u)
            ^ static_cast<uint32_t>(event.key * 0x85ebca6bu);
        if (voice.randomState == 0u) voice.randomState = 1u;
        const auto bounds = fieldBounds(settings);
        voice.motion = settings.motion;
        voice.articulation = settings.articulation;
        voice.segmentModel = settings.segmentModel;
        voice.output = preserveMotion ? oldOutput : initialOutput(settings);
        if (preserveMotion) {
            voice.motionPhase = oldMotion;
            voice.innerPhase = oldInner;
            voice.outerPhase = oldOuter;
            voice.eventRateScale = oldVoice.eventRateScale;
            voice.drunkA = oldA;
            voice.drunkB = oldB;
            voice.zigForward = oldZigForward;
            voice.eventStarted = oldVoice.eventStarted;
            voice.pendingPacketEvent = oldVoice.pendingPacketEvent;
            voice.pendingTurnEvent = oldVoice.pendingTurnEvent;
            voice.eventPhase = oldVoice.eventPhase;
            voice.eventBasePosition = oldVoice.eventBasePosition;
            voice.movingLoopCenter = oldVoice.movingLoopCenter;
            voice.eventIndex = oldVoice.eventIndex;
            voice.eventRepeatIndex = oldVoice.eventRepeatIndex;
            voice.previousOutput = oldVoice.previousOutput;
            voice.routeFadeRemaining = oldVoice.routeFadeRemaining;
            voice.routeFadeFrameCount = oldVoice.routeFadeFrameCount;
            voice.segments = oldVoice.segments;
        } else {
            const double origin = std::clamp(settings.locus,
                bounds.first, bounds.second);
            voice.motionPhase = phaseForPosition(settings.motion, origin,
                bounds.first, bounds.second, true);
            voice.innerPhase = settings.articulation
                    != MotionArticulation::Continuous
                ? settings.packetDuty * 0.5 : 0.0;
            voice.outerPhase = settings.articulation
                    == MotionArticulation::Motor
                ? settings.symmetry : 0.0;
            voice.drunkA = std::clamp(settings.locus,
                bounds.first, bounds.second);
            voice.drunkB = voice.drunkA;
            voice.zigForward = true;
            voice.eventBasePosition = std::clamp(settings.locus,
                settings.start, settings.end);
            voice.movingLoopCenter = voice.eventBasePosition;
            if (settings.motion == MotionMode::Zigzag)
                chooseZigTarget(voice, settings,
                    bounds.first, bounds.second, true);
            else chooseDrunkTarget(voice, settings,
                    bounds.first, bounds.second, true);
        }
        voice.sourcePosition = trajectoryPosition(voice, settings,
            bounds.first, bounds.second);
    }

    void applyEvent(const MotionRenderEvent& event,
        const MotionSettings& settings) noexcept
    {
        switch (event.kind) {
        case MotionEventKind::StopAll:
            for (auto& voice : voices_) retireVoice(voice);
            return;
        case MotionEventKind::NoteOff: {
            if (settings.triggerMode == TriggerMode::OneShot
                || settings.triggerMode == TriggerMode::Toggle) return;
            for (auto& voice : voices_)
                if (eventMatches(event, voice)) beginRelease(voice, settings);
            return;
        }
        case MotionEventKind::Preview:
        case MotionEventKind::NoteOn:
            break;
        }

        if (settings.triggerMode == TriggerMode::Toggle) {
            bool stopped = false;
            for (auto& voice : voices_) if (eventMatches(event, voice)) {
                beginRelease(voice, settings);
                stopped = true;
            }
            if (stopped) return;
        }

        if (settings.voiceMode == VoiceMode::Legato) {
            if (Voice* active = newestVoice()) {
                startVoice(*active, event, settings, true);
                return;
            }
        } else if (settings.voiceMode == VoiceMode::Mono) {
            for (auto& voice : voices_)
                if (voice.active) retireVoice(voice);
        }
        Voice* voice = allocateVoice();
        startVoice(*voice, event, settings, false);
    }

    void publishCursors(const MotionSettings& settings) noexcept
    {
        for (const auto& voice : voices_) {
            if (!voice.active || voiceCursorCount_ >= voiceCursors_.size())
                continue;
            const auto bounds = fieldBounds(settings, voice);
            voiceCursors_[voiceCursorCount_++] = {
                static_cast<float>(voice.sourcePosition),
                static_cast<float>(bounds.first),
                static_cast<float>(bounds.second),
                static_cast<float>(voice.motionPhase),
                static_cast<float>(voice.innerPhase),
                static_cast<float>(voice.outerPhase),
                voice.age, voice.key, voice.directionForward,
                voice.packetActive, voice.output.firstChannel,
                voice.output.secondChannel, voice.output.channelCount,
                activeSegmentCount(voice), voice.eventIndex,
            };
        }
    }

    const SampleAsset* asset_ = nullptr;
    double outputSampleRate_ = 48000.0;
    uint32_t outputChannelCount_ = 2u;
    bool prepared_ = false;
    std::array<Voice, kMaximumMotionVoices> voices_ {};
    std::array<Tail, kMaximumMotionVoices> tails_ {};
    std::array<MotionVoiceCursor, kMaximumMotionVoices> voiceCursors_ {};
    uint32_t voiceCursorCount_ = 0u;
    uint64_t ageCounter_ = 0u;
    uint64_t segmentEventCount_ = 0u;
    s3g::routing::TriggerOutputAllocator<kMaximumMotionOutputChannels>
        outputAllocator_ {};
    float outputPeak_ = 0.0f;
};

} // namespace s3g::sample
