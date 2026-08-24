#pragma once

#include "s3g_sample_asset.h"
#include "s3g_sample_playback.h"
#include "s3g_voice_output_allocator.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace s3g::sample {

constexpr std::size_t kSampleLaneCount = 4u;
constexpr std::size_t kMaximumLaneVoices = 32u;
constexpr std::size_t kMaximumLanePathPoints = 16u;

// Nudge rotates source content around the selected loop. These paired
// mappings are shared by the audio engine and GUI so the displayed waveform
// and its timeline-space playhead describe the same circular offset.
inline double wrapLanePhase(double value) noexcept
{
    value -= std::floor(value);
    return value < 0.0 ? value + 1.0 : value;
}

inline double laneSourcePhase(double timelinePhase, double nudge) noexcept
{
    return wrapLanePhase(timelinePhase + nudge);
}

inline double laneTimelinePhase(double sourcePhase, double nudge) noexcept
{
    return wrapLanePhase(sourcePhase - nudge);
}

struct LanePathPoint {
    float phase = 0.0f;
    float lane = 0.0f;
};

enum class LaneTransport : uint8_t {
    Forward = 0u,
    Reverse,
    PingPong,
};

enum class LaneRateBasis : uint8_t {
    Normal = 0u,
    Hertz,
};

enum class LanePath : uint8_t {
    Down = 0u,
    Up,
    Triangle,
    Sine,
    StepsDown,
    StepsUp,
    Random,
    Manual,
};

enum class LaneBlend : uint8_t {
    Crossfade = 0u,
    Jump,
};

enum class LanePathShape : uint8_t {
    Linear = 0u,
    Smooth,
    Exponential,
    Plateau,
};

enum class LaneOutputMode : uint8_t {
    Preserve = 0u,
    Distribute,
};

enum class LaneAllocationCadence : uint8_t {
    Note = 0u,
    Lane,
    Turn,
};

enum class LaneEventKind : uint8_t {
    NoteOn = 0u,
    NoteOff,
    Preview,
    StopAll,
};

struct LanesRenderEvent {
    uint32_t frameOffset = 0u;
    LaneEventKind kind = LaneEventKind::NoteOn;
    uint64_t noteId = 0u;
    uint8_t key = 60u;
    float velocity = 1.0f;
    uint8_t midiChannel = 0u;
};

struct SampleLanesSettings {
    LaneTransport transport = LaneTransport::Forward;
    LaneRateBasis rateBasis = LaneRateBasis::Normal;
    LanePath path = LanePath::Down;
    LaneBlend blend = LaneBlend::Crossfade;
    LanePathShape pathShape = LanePathShape::Linear;
    VoiceMode voiceMode = VoiceMode::Poly;
    TriggerMode triggerMode = TriggerMode::Gate;
    double start = 0.0;
    double end = 1.0;
    // Fraction of the selected loop overlapped at a wrap.
    double loopCrossfade = 0.02;
    // Native-rate multiplier in Normal mode; complete loops per second in Hz.
    float rate = 1.0f;
    float pathCycles = 1.0f;
    float pathOffset = 0.0f;
    float pathSkew = 0.5f;
    float pathCurve = 0.0f;
    float manualLane = 0.0f;
    // Manual paths are piecewise-linear breakpoint curves. A count below two
    // retains the legacy constant Manual Lane behavior for older sessions.
    std::array<LanePathPoint, kMaximumLanePathPoints> manualPathPoints {};
    uint32_t manualPathPointCount = 0u;
    float laneSlewSeconds = 0.005f;
    // Per-lane transport. Speed changes read rate and pitch; Stretch changes
    // duration independently through a two-grain overlap reader. Nudge is a
    // signed fraction of the selected loop and wraps at its endpoints.
    std::array<float, kSampleLaneCount> laneSpeed {{
        1.0f, 1.0f, 1.0f, 1.0f,
    }};
    std::array<float, kSampleLaneCount> laneStretch {{
        1.0f, 1.0f, 1.0f, 1.0f,
    }};
    std::array<float, kSampleLaneCount> laneNudge {{
        0.0f, 0.0f, 0.0f, 0.0f,
    }};
    float tuneSemitones = 0.0f;
    float fineTuneCents = 0.0f;
    uint8_t rootNote = 60u;
    float attackSeconds = 0.003f;
    float releaseSeconds = 0.020f;
    float outputGainDecibels = -6.0f;
    float pan = 0.0f;
    float velocitySensitivity = 1.0f;
    uint32_t seed = 1u;
    LaneOutputMode outputMode = LaneOutputMode::Preserve;
    LaneAllocationCadence allocationCadence = LaneAllocationCadence::Note;
    uint32_t activeOutputChannels = 32u;
    s3g::routing::VoiceOutputRouting outputRouting {};

    bool valid() const noexcept
    {
        return static_cast<uint8_t>(transport)
                <= static_cast<uint8_t>(LaneTransport::PingPong)
            && static_cast<uint8_t>(rateBasis)
                <= static_cast<uint8_t>(LaneRateBasis::Hertz)
            && static_cast<uint8_t>(path)
                <= static_cast<uint8_t>(LanePath::Manual)
            && static_cast<uint8_t>(blend)
                <= static_cast<uint8_t>(LaneBlend::Jump)
            && static_cast<uint8_t>(pathShape)
                <= static_cast<uint8_t>(LanePathShape::Plateau)
            && static_cast<uint8_t>(voiceMode)
                <= static_cast<uint8_t>(VoiceMode::Legato)
            && static_cast<uint8_t>(triggerMode)
                <= static_cast<uint8_t>(TriggerMode::Toggle)
            && std::isfinite(start) && std::isfinite(end)
            && start >= 0.0 && start < end && end <= 1.0
            && std::isfinite(loopCrossfade) && loopCrossfade >= 0.0
            && loopCrossfade <= 0.5
            && std::isfinite(rate) && rate >= 0.01f && rate <= 80.0f
            && std::isfinite(pathCycles)
            && pathCycles >= 0.125f && pathCycles <= 16.0f
            && std::isfinite(pathOffset)
            && pathOffset >= 0.0f && pathOffset <= 1.0f
            && std::isfinite(pathSkew)
            && pathSkew >= 0.05f && pathSkew <= 0.95f
            && std::isfinite(pathCurve)
            && pathCurve >= -1.0f && pathCurve <= 1.0f
            && std::isfinite(manualLane)
            && manualLane >= 0.0f && manualLane <= 1.0f
            && manualPathPointCount <= manualPathPoints.size()
            && (manualPathPointCount < 2u
                || (manualPathPoints[0u].phase == 0.0f
                    && manualPathPoints[manualPathPointCount - 1u].phase
                        == 1.0f))
            && std::all_of(manualPathPoints.begin(),
                manualPathPoints.begin() + manualPathPointCount,
                [](const LanePathPoint& point) {
                    return std::isfinite(point.phase)
                        && point.phase >= 0.0f && point.phase <= 1.0f
                        && std::isfinite(point.lane)
                        && point.lane >= 0.0f && point.lane <= 1.0f;
                })
            && (manualPathPointCount < 2u
                || std::is_sorted(manualPathPoints.begin(),
                    manualPathPoints.begin() + manualPathPointCount,
                    [](const LanePathPoint& first,
                       const LanePathPoint& second) {
                        return first.phase < second.phase;
                    }))
            && std::isfinite(laneSlewSeconds)
            && laneSlewSeconds >= 0.0f && laneSlewSeconds <= 0.1f
            && std::all_of(laneSpeed.begin(), laneSpeed.end(),
                [](float value) {
                    return std::isfinite(value)
                        && value >= 0.25f && value <= 4.0f;
                })
            && std::all_of(laneStretch.begin(), laneStretch.end(),
                [](float value) {
                    return std::isfinite(value)
                        && value >= 0.25f && value <= 4.0f;
                })
            && std::all_of(laneNudge.begin(), laneNudge.end(),
                [](float value) {
                    return std::isfinite(value)
                        && value >= -0.5f && value <= 0.5f;
                })
            && std::isfinite(tuneSemitones)
            && tuneSemitones >= -60.0f && tuneSemitones <= 60.0f
            && std::isfinite(fineTuneCents)
            && fineTuneCents >= -100.0f && fineTuneCents <= 100.0f
            && rootNote < 128u
            && std::isfinite(attackSeconds)
            && attackSeconds >= 0.0f && attackSeconds <= 2.0f
            && std::isfinite(releaseSeconds)
            && releaseSeconds >= 0.0f && releaseSeconds <= 2.0f
            && std::isfinite(outputGainDecibels)
            && outputGainDecibels >= -60.0f
            && outputGainDecibels <= 12.0f
            && std::isfinite(pan) && pan >= -1.0f && pan <= 1.0f
            && std::isfinite(velocitySensitivity)
            && velocitySensitivity >= 0.0f
            && velocitySensitivity <= 1.0f
            && static_cast<uint8_t>(outputMode)
                <= static_cast<uint8_t>(LaneOutputMode::Distribute)
            && static_cast<uint8_t>(allocationCadence)
                <= static_cast<uint8_t>(LaneAllocationCadence::Turn)
            && activeOutputChannels >= 2u && activeOutputChannels <= 32u
            && outputRouting.valid()
            && seed != 0u;
    }
};

inline uint32_t lanePathRandomValue(uint32_t value) noexcept
{
    if (value == 0u) value = 0x9e3779b9u;
    value ^= value << 13u;
    value ^= value >> 17u;
    value ^= value << 5u;
    return value;
}

inline double randomLanePathUnit(double phase, uint32_t seed) noexcept
{
    phase = wrapLanePhase(phase);
    const uint32_t section = std::min<uint32_t>(7u,
        static_cast<uint32_t>(std::floor(phase * 8.0)));
    const uint32_t randomValue = lanePathRandomValue(
        seed ^ (section * 0x9e3779b9u));
    return static_cast<double>(randomValue % kSampleLaneCount)
        / static_cast<double>(kSampleLaneCount - 1u);
}

inline double manualLanePathUnit(double phase,
    const SampleLanesSettings& settings) noexcept
{
    if (settings.manualPathPointCount < 2u)
        return settings.manualLane;
    phase = phase >= 0.0 && phase <= 1.0
        ? phase : wrapLanePhase(phase);
    const auto* points = settings.manualPathPoints.data();
    for (uint32_t index = 1u;
         index < settings.manualPathPointCount; ++index) {
        if (phase > points[index].phase) continue;
        const double width = static_cast<double>(points[index].phase)
            - points[index - 1u].phase;
        if (!(width > 0.0)) return points[index].lane;
        const double mix = std::clamp((phase - points[index - 1u].phase)
            / width, 0.0, 1.0);
        return points[index - 1u].lane
            + (points[index].lane - points[index - 1u].lane) * mix;
    }
    return points[settings.manualPathPointCount - 1u].lane;
}

inline double shapeLanePathUnit(double value,
    const SampleLanesSettings& settings) noexcept
{
    value = std::clamp(value, 0.0, 1.0);
    const double skew = settings.pathSkew;
    value = value <= skew
        ? 0.5 * value / skew
        : 0.5 + 0.5 * (value - skew) / (1.0 - skew);
    switch (settings.pathShape) {
    case LanePathShape::Smooth:
        return value * value * (3.0 - 2.0 * value);
    case LanePathShape::Exponential: {
        const double exponent = std::pow(2.0,
            3.0 * static_cast<double>(settings.pathCurve));
        return std::pow(value, exponent);
    }
    case LanePathShape::Plateau: {
        const double width = 0.18 + 0.12
            * std::abs(static_cast<double>(settings.pathCurve));
        const double local = std::clamp(
            (value - width) / (1.0 - 2.0 * width), 0.0, 1.0);
        return local * local * (3.0 - 2.0 * local);
    }
    case LanePathShape::Linear:
    default:
        if (settings.pathCurve == 0.0f) return value;
        return settings.pathCurve > 0.0f
            ? std::pow(value, 1.0 + 3.0 * settings.pathCurve)
            : 1.0 - std::pow(1.0 - value,
                1.0 - 3.0 * settings.pathCurve);
    }
}

inline double sampleLanePathUnit(double phase,
    const SampleLanesSettings& settings) noexcept
{
    if (settings.path == LanePath::Manual)
        return manualLanePathUnit(phase, settings);
    phase = wrapLanePhase(phase);
    if (settings.path == LanePath::Random)
        return randomLanePathUnit(phase, settings.seed);
    double unit = phase;
    if (settings.path == LanePath::Up
        || settings.path == LanePath::StepsUp) unit = 1.0 - phase;
    else if (settings.path == LanePath::Triangle)
        unit = 1.0 - std::abs(2.0 * phase - 1.0);
    else if (settings.path == LanePath::Sine)
        unit = 0.5 - 0.5 * std::cos(6.28318530717958647692 * phase);
    unit = shapeLanePathUnit(unit, settings);
    if (settings.path == LanePath::StepsDown
        || settings.path == LanePath::StepsUp)
        unit = std::min(3.0, std::floor(unit * 4.0)) / 3.0;
    return std::clamp(unit, 0.0, 1.0);
}

struct SampleLanesVoiceCursor {
    float sourcePositionNormalized = -1.0f;
    float lanePositionNormalized = 0.0f;
    float pathPhase = 0.0f;
    std::array<float, kSampleLaneCount> laneSourcePositions {{
        -1.0f, -1.0f, -1.0f, -1.0f,
    }};
    std::array<float, kSampleLaneCount> laneWeights {};
    uint64_t identity = 0u;
    uint8_t key = 0u;
    uint8_t outputFirstChannel = 0u;
    uint8_t outputChannelCount = 0u;
    bool directionForward = true;
};

// Four independently sized audio documents share a normalized playback and
// lane trajectory. No source is resampled into a common document: each lane
// resolves the same 0..1 loop phase against its own frame count at render time.
class SampleLanesEngine {
public:
    bool prepare(double outputSampleRate, uint32_t outputChannelCount) noexcept
    {
        if (!(outputSampleRate > 0.0) || !std::isfinite(outputSampleRate))
            return false;
        if (outputChannelCount < 2u || outputChannelCount > 32u)
            return false;
        outputSampleRate_ = outputSampleRate;
        outputChannelCount_ = outputChannelCount;
        prepared_ = true;
        reset();
        return true;
    }

    bool prepare(double outputSampleRate) noexcept
    { return prepare(outputSampleRate, 2u); }

    void reset() noexcept
    {
        voices_ = {};
        cursors_ = {};
        cursorCount_ = 0u;
        ageCounter_ = 0u;
        outputPeak_ = 0.0f;
        allocator_.reset();
        allocatorSeed_ = 0u;
    }

    void unprepare() noexcept
    {
        reset();
        assets_ = {};
        prepared_ = false;
    }

    bool setAsset(std::size_t lane, const SampleAsset* asset) noexcept
    {
        if (lane >= assets_.size() || (asset && !asset->valid()))
            return false;
        assets_[lane] = asset;
        return true;
    }

    bool setAssets(const std::array<const SampleAsset*, kSampleLaneCount>&
        assets) noexcept
    {
        for (const auto* asset : assets)
            if (asset && !asset->valid()) return false;
        assets_ = assets;
        return true;
    }

    // Constant-time adoption for immutable assets validated off the audio
    // thread. Retention remains the owner's responsibility.
    void setPreparedAsset(std::size_t lane, const SampleAsset* asset) noexcept
    {
        if (lane < assets_.size()) assets_[lane] = asset;
    }

    const std::array<const SampleAsset*, kSampleLaneCount>& assets()
        const noexcept { return assets_; }
    uint32_t activeVoiceCount() const noexcept
    {
        uint32_t count = 0u;
        for (const auto& voice : voices_) if (voice.active) ++count;
        return count;
    }
    uint32_t voiceCursorCount() const noexcept { return cursorCount_; }
    float outputPeak() const noexcept { return outputPeak_; }
    const std::array<SampleLanesVoiceCursor, kMaximumLaneVoices>&
        voiceCursors() const noexcept { return cursors_; }

    void render(const SampleLanesSettings& settings,
        const LanesRenderEvent* events, std::size_t eventCount,
        float* left, float* right, uint32_t frameCount) noexcept
    {
        std::array<float*, 2u> outputs {{ left, right }};
        render(settings, events, eventCount, outputs.data(), 2u,
            frameCount);
    }

    void render(const SampleLanesSettings& settings,
        const LanesRenderEvent* events, std::size_t eventCount,
        float* const* outputs, uint32_t outputChannelCount,
        uint32_t frameCount) noexcept
    {
        cursorCount_ = 0u;
        if (!outputs || outputChannelCount < 2u
            || outputChannelCount > outputChannelCount_) return;
        for (uint32_t channel = 0u; channel < outputChannelCount; ++channel) {
            if (!outputs[channel]) return;
            std::fill(outputs[channel], outputs[channel] + frameCount, 0.0f);
        }
        outputPeak_ = 0.0f;
        if (!prepared_ || !anyAsset() || !settings.valid()
            || frameCount == 0u) return;
        if (!events) eventCount = 0u;

        if (allocatorSeed_ != settings.seed) {
            allocator_.reset(settings.seed);
            allocatorSeed_ = settings.seed;
            for (auto& voice : voices_) voice.outputSignature = 0u;
        }

        const float master = decibelsToAmplitude(
            settings.outputGainDecibels);
        constexpr double halfPi = 1.57079632679489661923;
        const float leftPan = static_cast<float>(std::cos(
            (static_cast<double>(settings.pan) + 1.0) * 0.5 * halfPi));
        const float rightPan = static_cast<float>(std::sin(
            (static_cast<double>(settings.pan) + 1.0) * 0.5 * halfPi));
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
                adaptLiveModes(voice, settings);
                adaptLiveNudges(voice, settings);
                const double source = sourcePosition(voice, settings);
                const double pathPhase = makePathPhase(voice, settings);
                const double lane = lanePosition(settings, pathPhase);
                const auto targets = laneTargets(lane, settings.blend);
                smoothWeights(voice, targets, settings.laneSlewSeconds);
                updateOutputAssignment(voice, settings, targets, lane);
                std::array<double, kSampleLaneCount> laneSources {};
                for (std::size_t index = 0u; index < assets_.size(); ++index) {
                    laneSources[index] = sourcePositionForLane(
                        voice, index, settings);
                }
                const float gain = voice.envelope * voice.velocityGain
                    * master;
                if (settings.outputMode == LaneOutputMode::Preserve) {
                    for (uint32_t channel = 0u;
                         channel < outputChannelCount; ++channel) {
                        float sample = 0.0f;
                        for (std::size_t index = 0u; index < assets_.size();
                             ++index) {
                            const auto* asset = assets_[index];
                            const float weight = voice.laneWeights[index];
                            if (!(weight > 0.0f) || !asset) continue;
                            const uint8_t sourceChannel
                                = asset->channelCount == 1u
                                    && outputChannelCount == 2u
                                ? 0u : static_cast<uint8_t>(channel);
                            if (sourceChannel >= asset->channelCount) continue;
                            sample += weight * laneSample(index,
                                sourceChannel, laneSources[index], voice,
                                settings);
                        }
                        float channelGain = gain;
                        if (outputChannelCount == 2u)
                            channelGain *= channel == 0u ? leftPan : rightPan;
                        outputs[channel][frame] += sample * channelGain;
                        outputPeak_ = std::max(outputPeak_,
                            std::abs(outputs[channel][frame]));
                    }
                } else {
                    float sampleLeft = 0.0f;
                    float sampleRight = 0.0f;
                    const bool stereo = settings.outputRouting.width
                            == s3g::routing::OutputVoiceWidth::Stereo
                        && settings.activeOutputChannels >= 2u;
                    for (std::size_t index = 0u; index < assets_.size();
                         ++index) {
                        const float weight = voice.laneWeights[index];
                        if (!(weight > 0.0f) || !assets_[index]) continue;
                        sampleLeft += weight * laneObjectSample(index, false,
                            stereo, laneSources[index], voice, settings);
                        sampleRight += weight * laneObjectSample(index, true,
                            stereo, laneSources[index], voice, settings);
                    }
                    const auto assignment = voice.outputAssignment;
                    const uint32_t first = assignment.firstChannel;
                    if (first < outputChannelCount) {
                        outputs[first][frame] += sampleLeft * gain * leftPan;
                        outputPeak_ = std::max(outputPeak_,
                            std::abs(outputs[first][frame]));
                    }
                    if (assignment.channelCount > 1u) {
                        const uint32_t second = assignment.secondChannel;
                        if (second < outputChannelCount) {
                            outputs[second][frame]
                                += sampleRight * gain * rightPan;
                            outputPeak_ = std::max(outputPeak_,
                                std::abs(outputs[second][frame]));
                        }
                    }
                }
                voice.lastSourcePosition = source;
                voice.lastLanePosition = lane;
                voice.lastPathPhase = pathPhase;
                voice.lastLaneSourcePositions = laneSources;
                advanceVoice(voice, settings);
            }
        }
        while (eventIndex < eventCount
            && events[eventIndex].frameOffset <= frameCount) {
            applyEvent(events[eventIndex], settings);
            ++eventIndex;
        }
        publishCursors();
    }

private:
    struct Voice {
        bool active = false;
        bool releasing = false;
        bool directionForward = true;
        bool weightsInitialized = false;
        uint64_t noteId = 0u;
        uint64_t age = 0u;
        uint8_t key = 60u;
        uint8_t midiChannel = 0u;
        float velocityGain = 1.0f;
        float envelope = 0.0f;
        float releaseDecrement = 0.0f;
        double phase = 0.0;
        double lastSourcePosition = 0.0;
        double lastLanePosition = 0.0;
        double lastPathPhase = 0.0;
        std::array<double, kSampleLaneCount> lanePhases {};
        std::array<double, kSampleLaneCount> stretchPhases {};
        std::array<double, kSampleLaneCount> appliedNudges {};
        std::array<float, kSampleLaneCount> stretchMix {};
        std::array<double, kSampleLaneCount> lastLaneSourcePositions {};
        std::array<bool, kSampleLaneCount> laneDirections {{
            true, true, true, true,
        }};
        LaneTransport transport = LaneTransport::Forward;
        std::array<float, kSampleLaneCount> laneWeights {};
        s3g::routing::VoiceOutputAssignment outputAssignment {};
        uint32_t outputSignature = 0u;
        int8_t dominantLane = -1;
        float lastPathDelta = 0.0f;
    };

    static double wrap(double value) noexcept
    {
        return wrapLanePhase(value);
    }

    static float decibelsToAmplitude(float value) noexcept
    {
        return std::pow(10.0f, value / 20.0f);
    }

    bool anyAsset() const noexcept
    {
        return std::any_of(assets_.begin(), assets_.end(),
            [](const SampleAsset* asset) { return asset != nullptr; });
    }

    const SampleAsset* referenceAsset() const noexcept
    {
        for (const auto* asset : assets_) if (asset) return asset;
        return nullptr;
    }

    static float interpolate(const std::vector<float>& samples,
        double position) noexcept
    {
        if (samples.empty()) return 0.0f;
        position = std::clamp(position, 0.0,
            static_cast<double>(samples.size() - 1u));
        const auto first = static_cast<std::size_t>(std::floor(position));
        const auto second = std::min(first + 1u, samples.size() - 1u);
        const float fraction = static_cast<float>(position - first);
        return samples[first] + (samples[second] - samples[first]) * fraction;
    }

    static double makePathPhase(const Voice& voice,
        const SampleLanesSettings& settings) noexcept
    {
        if (settings.path == LanePath::Manual)
            return std::clamp(voice.phase, 0.0, 1.0);
        return wrap(voice.phase * settings.pathCycles
            + settings.pathOffset);
    }

    static double lanePosition(const SampleLanesSettings& settings,
        double pathPhase) noexcept
    {
        return 3.0 * sampleLanePathUnit(pathPhase, settings);
    }

    std::array<float, kSampleLaneCount> laneTargets(double position,
        LaneBlend blend) const noexcept
    {
        std::array<float, kSampleLaneCount> weights {};
        std::array<uint8_t, kSampleLaneCount> loaded {};
        std::size_t count = 0u;
        for (uint8_t lane = 0u; lane < assets_.size(); ++lane)
            if (assets_[lane]) loaded[count++] = lane;
        if (count == 0u) return weights;
        if (blend == LaneBlend::Jump || count == 1u) {
            uint8_t closest = loaded[0u];
            double distance = std::abs(position - closest);
            for (std::size_t index = 1u; index < count; ++index) {
                const double nextDistance = std::abs(position - loaded[index]);
                if (nextDistance < distance) {
                    closest = loaded[index];
                    distance = nextDistance;
                }
            }
            weights[closest] = 1.0f;
            return weights;
        }
        uint8_t lower = loaded[0u];
        uint8_t upper = loaded[count - 1u];
        for (std::size_t index = 0u; index < count; ++index) {
            if (loaded[index] <= position) lower = loaded[index];
            if (loaded[index] >= position) {
                upper = loaded[index];
                break;
            }
        }
        if (lower == upper) {
            weights[lower] = 1.0f;
            return weights;
        }
        const float mix = static_cast<float>((position - lower)
            / static_cast<double>(upper - lower));
        // Equal-power interpolation avoids the central level dip that a
        // linear crossfade produces for decorrelated files.
        constexpr double halfPi = 1.57079632679489661923;
        weights[lower] = static_cast<float>(std::cos(mix * halfPi));
        weights[upper] = static_cast<float>(std::sin(mix * halfPi));
        return weights;
    }

    void smoothWeights(Voice& voice,
        const std::array<float, kSampleLaneCount>& target,
        float seconds) const noexcept
    {
        if (!voice.weightsInitialized || !(seconds > 0.0f)) {
            voice.laneWeights = target;
            voice.weightsInitialized = true;
            return;
        }
        const float coefficient = static_cast<float>(1.0
            - std::exp(-1.0 / std::max(1.0,
                outputSampleRate_ * static_cast<double>(seconds))));
        float energy = 0.0f;
        for (std::size_t lane = 0u; lane < target.size(); ++lane) {
            voice.laneWeights[lane] += coefficient
                * (target[lane] - voice.laneWeights[lane]);
            energy += voice.laneWeights[lane] * voice.laneWeights[lane];
        }
        if (energy > 1.0e-12f) {
            const float normalizer = 1.0f / std::sqrt(energy);
            for (auto& weight : voice.laneWeights) weight *= normalizer;
        }
    }

    static double sourcePosition(const Voice& voice,
        const SampleLanesSettings& settings) noexcept
    {
        const double span = settings.end - settings.start;
        return settings.start + voice.phase * span;
    }

    static double sourcePositionForLane(const Voice& voice, std::size_t lane,
        const SampleLanesSettings& settings) noexcept
    {
        return settings.start + voice.lanePhases[lane]
            * (settings.end - settings.start);
    }

    static float grainWindow(double phase) noexcept
    {
        constexpr double twoPi = 6.28318530717958647692;
        return static_cast<float>(0.5 - 0.5 * std::cos(twoPi
            * std::clamp(phase, 0.0, 1.0)));
    }

    double stretchWindowPhase(std::size_t lane,
        const SampleLanesSettings& settings) const noexcept
    {
        const auto* asset = assets_[lane];
        if (!asset || asset->frameCount() < 2u) return 0.0;
        constexpr double windowSeconds = 0.080;
        const double selectedFrames = static_cast<double>(
            asset->frameCount() - 1u) * (settings.end - settings.start);
        return std::clamp(asset->sampleRate * windowSeconds
            / std::max(1.0, selectedFrames), 1.0 / selectedFrames, 0.5);
    }

    static double resolveStretchReaderPhase(double phase,
        bool directionForward, const SampleLanesSettings& settings) noexcept
    {
        if (settings.transport == LaneTransport::PingPong) {
            phase = std::fmod(phase, 2.0);
            if (phase < 0.0) phase += 2.0;
            return phase <= 1.0 ? phase : 2.0 - phase;
        }
        const double crossfade = settings.loopCrossfade;
        if (!(crossfade > 0.0)) return wrap(phase);
        if (directionForward) {
            const double length = 1.0 - crossfade;
            return crossfade + wrap((phase - crossfade) / length) * length;
        }
        const double length = 1.0 - crossfade;
        return wrap(phase / length) * length;
    }

    float laneSampleAt(std::size_t lane, uint8_t outputChannel,
        double normalized, bool directionForward,
        const SampleLanesSettings& settings) const noexcept
    {
        const auto* asset = assets_[lane];
        if (!asset || asset->frameCount() == 0u) return 0.0f;
        const uint8_t channel = asset->channelCount == 1u ? 0u
            : std::min<uint8_t>(outputChannel, asset->channelCount - 1u);
        const auto& samples = asset->channels[channel];
        const auto read = [&](double at) noexcept {
            return interpolate(samples, std::clamp(at, 0.0, 1.0)
                * static_cast<double>(samples.size() - 1u));
        };
        const float primary = read(normalized);
        if (settings.transport == LaneTransport::PingPong
            || !(settings.loopCrossfade > 0.0)) return primary;
        const double span = settings.end - settings.start;
        const double width = std::min(span * 0.5,
            span * settings.loopCrossfade);
        if (!(width > 0.0)) return primary;
        if (directionForward) {
            const double fadeStart = settings.end - width;
            if (normalized < fadeStart) return primary;
            const double mix = std::clamp(
                (normalized - fadeStart) / width, 0.0, 1.0);
            const float secondary = read(settings.start
                + (normalized - fadeStart));
            const float smooth = static_cast<float>(mix * mix
                * (3.0 - 2.0 * mix));
            return primary + (secondary - primary) * smooth;
        }
        const double fadeEnd = settings.start + width;
        if (normalized > fadeEnd) return primary;
        const double mix = std::clamp(
            (fadeEnd - normalized) / width, 0.0, 1.0);
        const float secondary = read(settings.end - width
            + (normalized - settings.start));
        const float smooth = static_cast<float>(mix * mix
            * (3.0 - 2.0 * mix));
        return primary + (secondary - primary) * smooth;
    }

    float laneSample(std::size_t lane, uint8_t outputChannel,
        double normalized, const Voice& voice,
        const SampleLanesSettings& settings) const noexcept
    {
        const bool direction = voice.laneDirections[lane];
        const double stretch = settings.laneStretch[lane];
        const double window = stretchWindowPhase(lane, settings);
        const double baseIncrement = cyclesPerSecond(voice, settings)
            * settings.laneSpeed[lane] / outputSampleRate_;
        const double transportIncrement = baseIncrement / stretch;
        const double difference = std::abs(
            baseIncrement - transportIncrement);
        const float direct = laneSampleAt(lane, outputChannel, normalized,
            direction, settings);
        if (std::abs(stretch - 1.0) < 1.0e-5 || !(window > 0.0)
            || !(difference > 1.0e-12)) return direct;

        const double firstPhase = voice.stretchPhases[lane];
        double secondPhase = firstPhase + 0.5;
        if (secondPhase >= 1.0) secondPhase -= 1.0;
        const auto readerPosition = [&](double phase) noexcept {
            const double delay = baseIncrement >= transportIncrement
                ? window * (1.0 - phase) : window * phase;
            const double signedDelay = direction ? delay : -delay;
            const double local = resolveStretchReaderPhase(
                voice.lanePhases[lane] - signedDelay, direction, settings);
            return settings.start + local
                * (settings.end - settings.start);
        };
        const float firstWeight = grainWindow(firstPhase);
        const float secondWeight = grainWindow(secondPhase);
        const float normalizer = 1.0f / std::max(1.0e-6f,
            firstWeight + secondWeight);
        const float granular = (laneSampleAt(lane, outputChannel,
                    readerPosition(firstPhase), direction, settings)
                * firstWeight
            + laneSampleAt(lane, outputChannel,
                    readerPosition(secondPhase), direction, settings)
                * secondWeight) * normalizer;
        return direct + (granular - direct) * voice.stretchMix[lane];
    }

    float laneObjectSample(std::size_t lane, bool right, bool stereo,
        double normalized, const Voice& voice,
        const SampleLanesSettings& settings) const noexcept
    {
        const auto* asset = assets_[lane];
        if (!asset || asset->channelCount == 0u) return 0.0f;
        if (!stereo) {
            float sum = 0.0f;
            for (uint8_t channel = 0u; channel < asset->channelCount;
                 ++channel)
                sum += laneSample(lane, channel, normalized, voice, settings);
            return sum / std::sqrt(static_cast<float>(asset->channelCount));
        }
        if (asset->channelCount == 1u)
            return laneSample(lane, 0u, normalized, voice, settings);
        if (asset->channelCount == 2u)
            return laneSample(lane, right ? 1u : 0u, normalized, voice,
                settings);
        float sum = 0.0f;
        uint32_t count = 0u;
        for (uint8_t channel = right ? 1u : 0u;
             channel < asset->channelCount; channel += 2u) {
            sum += laneSample(lane, channel, normalized, voice, settings);
            ++count;
        }
        return count > 0u ? sum / std::sqrt(static_cast<float>(count)) : 0.0f;
    }

    static uint32_t outputSignature(
        const SampleLanesSettings& settings) noexcept
    {
        return settings.activeOutputChannels
            | (static_cast<uint32_t>(settings.outputRouting.traversal) << 8u)
            | (static_cast<uint32_t>(settings.outputRouting.width) << 12u)
            | (static_cast<uint32_t>(settings.outputRouting.pairLayout)
                << 16u)
            | (static_cast<uint32_t>(settings.outputRouting.avoidAdjacent)
                << 20u);
    }

    void allocateOutput(Voice& voice,
        const SampleLanesSettings& settings) noexcept
    {
        voice.outputAssignment = allocator_.next(std::min(
            settings.activeOutputChannels, outputChannelCount_),
            settings.outputRouting);
        voice.outputSignature = outputSignature(settings);
    }

    void updateOutputAssignment(Voice& voice,
        const SampleLanesSettings& settings,
        const std::array<float, kSampleLaneCount>& targets,
        double lanePositionValue) noexcept
    {
        if (settings.outputMode != LaneOutputMode::Distribute) return;
        bool shouldAllocate = voice.outputSignature != outputSignature(settings);
        int8_t dominant = 0;
        for (int8_t lane = 1; lane < static_cast<int8_t>(targets.size());
             ++lane)
            if (targets[static_cast<std::size_t>(lane)]
                    > targets[static_cast<std::size_t>(dominant)])
                dominant = lane;
        if (settings.allocationCadence == LaneAllocationCadence::Lane
            && voice.dominantLane >= 0 && dominant != voice.dominantLane)
            shouldAllocate = true;
        const float delta = static_cast<float>(lanePositionValue
            - voice.lastLanePosition);
        if (settings.allocationCadence == LaneAllocationCadence::Turn
            && std::abs(delta) > 1.0e-5f
            && std::abs(voice.lastPathDelta) > 1.0e-5f
            && (delta > 0.0f) != (voice.lastPathDelta > 0.0f))
            shouldAllocate = true;
        if (std::abs(delta) > 1.0e-5f) voice.lastPathDelta = delta;
        voice.dominantLane = dominant;
        if (shouldAllocate) allocateOutput(voice, settings);
    }

    double cyclesPerSecond(const Voice& voice,
        const SampleLanesSettings& settings) const noexcept
    {
        double cycles = settings.rate;
        if (settings.rateBasis == LaneRateBasis::Normal) {
            const auto* asset = referenceAsset();
            if (!asset) return 0.0;
            const double frames = std::max(1.0,
                static_cast<double>(asset->frameCount() - 1u));
            cycles = asset->sampleRate / frames
                / (settings.end - settings.start) * settings.rate;
        }
        const double semitones = static_cast<double>(voice.key)
            - settings.rootNote + settings.tuneSemitones
            + settings.fineTuneCents * 0.01;
        return cycles * std::pow(2.0, semitones / 12.0);
    }

    static void advanceLoopPhase(double& phase, bool& directionForward,
        double increment, LaneTransport transport,
        double crossfade) noexcept
    {
        if (transport == LaneTransport::PingPong) {
            phase += directionForward ? increment : -increment;
            while (phase > 1.0 || phase < 0.0) {
                if (phase > 1.0) {
                    phase = 2.0 - phase;
                    directionForward = false;
                } else if (phase < 0.0) {
                    phase = -phase;
                    directionForward = true;
                }
            }
            phase = std::clamp(phase, 0.0, 1.0);
            return;
        }
        if (directionForward) {
            phase += increment;
            if (phase >= 1.0) phase = wrap(phase
                - 1.0 + crossfade);
        } else {
            phase -= increment;
            if (phase <= 0.0) phase = 1.0 - crossfade
                - wrap(-phase);
            if (phase < 0.0) phase = wrap(phase);
        }
    }

    void advanceVoice(Voice& voice,
        const SampleLanesSettings& settings) const noexcept
    {
        const double baseIncrement = cyclesPerSecond(voice, settings)
            / outputSampleRate_;
        advanceLoopPhase(voice.phase, voice.directionForward, baseIncrement,
            settings.transport, settings.loopCrossfade);
        for (std::size_t lane = 0u; lane < kSampleLaneCount; ++lane) {
            const double speedIncrement = baseIncrement
                * settings.laneSpeed[lane];
            const double transportIncrement = speedIncrement
                / settings.laneStretch[lane];
            const double window = stretchWindowPhase(lane, settings);
            if (window > 0.0) {
                voice.stretchPhases[lane] += std::abs(speedIncrement
                    - transportIncrement) / window;
                voice.stretchPhases[lane] = wrap(
                    voice.stretchPhases[lane]);
            }
            advanceLoopPhase(voice.lanePhases[lane],
                voice.laneDirections[lane], transportIncrement,
                settings.transport, settings.loopCrossfade);
        }
    }

    void updateEnvelope(Voice& voice,
        const SampleLanesSettings& settings) const noexcept
    {
        if (voice.releasing) {
            if (settings.releaseSeconds <= 0.0f) {
                voice.active = false;
                return;
            }
            if (!(voice.releaseDecrement > 0.0f))
                voice.releaseDecrement = static_cast<float>(voice.envelope
                    / std::max(1.0,
                        outputSampleRate_ * settings.releaseSeconds));
            voice.envelope -= voice.releaseDecrement;
            if (!(voice.envelope > 1.0e-6f)) voice.active = false;
            return;
        }
        if (settings.attackSeconds <= 0.0f) voice.envelope = 1.0f;
        else voice.envelope = std::min(1.0f, voice.envelope
            + static_cast<float>(1.0
                / (outputSampleRate_ * settings.attackSeconds)));
    }

    void beginRelease(Voice& voice,
        const SampleLanesSettings& settings) const noexcept
    {
        if (!voice.active || voice.releasing) return;
        if (settings.releaseSeconds <= 0.0f) {
            voice.active = false;
            return;
        }
        voice.releasing = true;
        voice.releaseDecrement = static_cast<float>(voice.envelope
            / std::max(1.0,
                outputSampleRate_ * settings.releaseSeconds));
    }

    void adaptLiveModes(Voice& voice,
        const SampleLanesSettings& settings) const noexcept
    {
        if (voice.transport == settings.transport) return;
        voice.transport = settings.transport;
        if (settings.transport == LaneTransport::Forward) {
            voice.directionForward = true;
            voice.laneDirections.fill(true);
        } else if (settings.transport == LaneTransport::Reverse) {
            voice.directionForward = false;
            voice.laneDirections.fill(false);
        }
    }

    void adaptLiveNudges(Voice& voice,
        const SampleLanesSettings& settings) const noexcept
    {
        constexpr double nudgeSmoothingSeconds = 0.020;
        const double maximumNudgeStep = 1.0 / std::max(1.0,
            outputSampleRate_ * nudgeSmoothingSeconds);
        constexpr double stretchSmoothingSeconds = 0.010;
        const float stretchCoefficient = static_cast<float>(1.0
            - std::exp(-1.0 / std::max(1.0,
                outputSampleRate_ * stretchSmoothingSeconds)));
        for (std::size_t lane = 0u; lane < kSampleLaneCount; ++lane) {
            const double next = settings.laneNudge[lane];
            double delta = next - voice.appliedNudges[lane];
            if (delta > 0.5) delta -= 1.0;
            else if (delta < -0.5) delta += 1.0;
            const double step = std::clamp(delta,
                -maximumNudgeStep, maximumNudgeStep);
            if (std::abs(step) > 1.0e-12) {
                voice.lanePhases[lane] = wrap(
                    voice.lanePhases[lane] + step);
                voice.appliedNudges[lane] += step;
                if (voice.appliedNudges[lane] > 0.5)
                    voice.appliedNudges[lane] -= 1.0;
                else if (voice.appliedNudges[lane] < -0.5)
                    voice.appliedNudges[lane] += 1.0;
            }
            const float stretchTarget
                = std::abs(settings.laneStretch[lane] - 1.0f) > 1.0e-5f
                ? 1.0f : 0.0f;
            voice.stretchMix[lane] += stretchCoefficient
                * (stretchTarget - voice.stretchMix[lane]);
        }
    }

    static bool matches(const LanesRenderEvent& event,
        const Voice& voice) noexcept
    {
        if (!voice.active) return false;
        if (event.noteId != 0u) return event.noteId == voice.noteId;
        return event.key == voice.key && event.midiChannel == voice.midiChannel;
    }

    Voice* newestVoice() noexcept
    {
        Voice* result = nullptr;
        for (auto& voice : voices_)
            if (voice.active && (!result || voice.age > result->age))
                result = &voice;
        return result;
    }

    Voice* allocateVoice() noexcept
    {
        for (auto& voice : voices_) if (!voice.active) return &voice;
        return &*std::min_element(voices_.begin(), voices_.end(),
            [](const Voice& left, const Voice& right) {
                return left.age < right.age;
            });
    }

    void startVoice(Voice& voice, const LanesRenderEvent& event,
        const SampleLanesSettings& settings, bool preservePosition) noexcept
    {
        const double oldPhase = voice.phase;
        const bool oldDirection = voice.directionForward;
        const auto oldWeights = voice.laneWeights;
        const bool oldWeightsInitialized = voice.weightsInitialized;
        const float oldEnvelope = voice.envelope;
        const auto oldLanePhases = voice.lanePhases;
        const auto oldStretchPhases = voice.stretchPhases;
        const auto oldAppliedNudges = voice.appliedNudges;
        const auto oldStretchMix = voice.stretchMix;
        const auto oldLaneDirections = voice.laneDirections;
        voice = {};
        voice.active = true;
        voice.noteId = event.noteId;
        voice.age = ++ageCounter_;
        voice.key = event.key;
        voice.midiChannel = event.midiChannel;
        voice.velocityGain = 1.0f + (std::clamp(event.velocity, 0.0f, 1.0f)
            - 1.0f) * settings.velocitySensitivity;
        voice.transport = settings.transport;
        voice.directionForward = settings.transport != LaneTransport::Reverse;
        voice.phase = settings.transport == LaneTransport::Reverse ? 1.0 : 0.0;
        voice.laneDirections.fill(voice.directionForward);
        voice.lanePhases.fill(voice.phase);
        for (std::size_t lane = 0u; lane < kSampleLaneCount; ++lane) {
            voice.appliedNudges[lane] = settings.laneNudge[lane];
            if (settings.laneNudge[lane] != 0.0f)
                voice.lanePhases[lane] = wrap(voice.lanePhases[lane]
                    + settings.laneNudge[lane]);
        }
        voice.envelope = 0.0f;
        voice.dominantLane = -1;
        allocateOutput(voice, settings);
        if (preservePosition) {
            voice.phase = oldPhase;
            voice.directionForward = oldDirection;
            voice.laneWeights = oldWeights;
            voice.weightsInitialized = oldWeightsInitialized;
            voice.envelope = oldEnvelope;
            voice.lanePhases = oldLanePhases;
            voice.stretchPhases = oldStretchPhases;
            voice.appliedNudges = oldAppliedNudges;
            voice.stretchMix = oldStretchMix;
            voice.laneDirections = oldLaneDirections;
        }
    }

    void applyEvent(const LanesRenderEvent& event,
        const SampleLanesSettings& settings) noexcept
    {
        if (event.kind == LaneEventKind::StopAll) {
            for (auto& voice : voices_) voice = {};
            return;
        }
        if (event.kind == LaneEventKind::NoteOff) {
            if (settings.triggerMode == TriggerMode::OneShot
                || settings.triggerMode == TriggerMode::Toggle) return;
            for (auto& voice : voices_)
                if (matches(event, voice)) beginRelease(voice, settings);
            return;
        }
        if (settings.triggerMode == TriggerMode::Toggle) {
            bool stopped = false;
            for (auto& voice : voices_) if (matches(event, voice)) {
                beginRelease(voice, settings);
                stopped = true;
            }
            if (stopped) return;
        }
        if (settings.voiceMode == VoiceMode::Legato) {
            if (Voice* voice = newestVoice()) {
                startVoice(*voice, event, settings, true);
                return;
            }
        } else if (settings.voiceMode == VoiceMode::Mono) {
            for (auto& voice : voices_) voice = {};
        }
        startVoice(*allocateVoice(), event, settings, false);
    }

    void publishCursors() noexcept
    {
        for (const auto& voice : voices_) {
            if (!voice.active || cursorCount_ >= cursors_.size()) continue;
            cursors_[cursorCount_++] = {
                static_cast<float>(voice.lastSourcePosition),
                static_cast<float>(voice.lastLanePosition / 3.0),
                static_cast<float>(voice.lastPathPhase),
                {
                    static_cast<float>(voice.lastLaneSourcePositions[0u]),
                    static_cast<float>(voice.lastLaneSourcePositions[1u]),
                    static_cast<float>(voice.lastLaneSourcePositions[2u]),
                    static_cast<float>(voice.lastLaneSourcePositions[3u]),
                },
                voice.laneWeights, voice.age, voice.key,
                voice.outputAssignment.firstChannel,
                voice.outputAssignment.channelCount,
                voice.directionForward,
            };
        }
    }

    std::array<const SampleAsset*, kSampleLaneCount> assets_ {};
    std::array<Voice, kMaximumLaneVoices> voices_ {};
    std::array<SampleLanesVoiceCursor, kMaximumLaneVoices> cursors_ {};
    double outputSampleRate_ = 48000.0;
    uint32_t outputChannelCount_ = 2u;
    s3g::routing::TriggerOutputAllocator<32u> allocator_ {};
    uint32_t allocatorSeed_ = 0u;
    uint64_t ageCounter_ = 0u;
    uint32_t cursorCount_ = 0u;
    float outputPeak_ = 0.0f;
    bool prepared_ = false;
};

} // namespace s3g::sample
