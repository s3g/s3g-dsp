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

namespace s3g::sample {

constexpr std::size_t kSampleCutupsLaneCount = 4u;
constexpr std::size_t kMaximumCutupsRegions = 64u;
constexpr std::size_t kMaximumCutupsPatternSteps = kMaximumCutupsRegions;
constexpr std::size_t kMaximumCutupsVoices = 16u;

enum class CutClockBasis : uint8_t {
    Hertz = 0u,
    Host,
};

enum class CutDivision : uint8_t {
    Whole = 0u,
    Half,
    Quarter,
    Eighth,
    Sixteenth,
    ThirtySecond,
    EighthTriplet,
    SixteenthTriplet,
};

enum class CutRegionMode : uint8_t {
    Equal = 0u,
    Transient,
};

enum class CutFileOrder : uint8_t {
    Down = 0u,
    Up,
    Palindrome,
    Random,
    RandomCycle,
    Manual,
    Pairs,
    OutsideIn,
    Stagger,
    CenterOut,
};

enum class CutPolyPathMode : uint8_t {
    Together = 0u,
    StepOffset,
    QuarterSpread,
    MirrorPairs,
};

enum class CutSourceOrder : uint8_t {
    Timeline = 0u,
    Forward,
    Reverse,
    Palindrome,
    Random,
    Walk,
    Manual,
};

enum class CutOutputMode : uint8_t {
    Preserve = 0u,
    Distribute,
};

enum class CutAllocationCadence : uint8_t {
    Note = 0u,
    Cut,
    Pattern,
};

enum class CutupsEventKind : uint8_t {
    NoteOn = 0u,
    NoteOff,
    Preview,
    StopAll,
};

struct CutupsRenderEvent {
    uint32_t frameOffset = 0u;
    CutupsEventKind kind = CutupsEventKind::NoteOn;
    uint64_t noteId = 0u;
    uint8_t key = 60u;
    float velocity = 1.0f;
    uint8_t midiChannel = 0u;
};

struct CutRegionTable {
    // Region starts use normalized full-file positions. The final boundary is
    // implicit at 1.0; the table therefore stores at most 64 starts.
    std::array<float, kMaximumCutupsRegions> starts {};
    uint32_t count = 0u;

    bool valid() const noexcept
    {
        if (count == 0u || count > starts.size()) return false;
        float previous = -1.0f;
        for (uint32_t index = 0u; index < count; ++index) {
            const float value = starts[index];
            if (!std::isfinite(value) || value < 0.0f || value >= 1.0f
                || value <= previous) return false;
            previous = value;
        }
        return starts[0u] == 0.0f;
    }
};

struct CutupsLaneMetadata {
    CutRegionTable transientRegions {};
    double analyzedBpm = 0.0;
    float tempoConfidence = 0.0f;
    bool tempoValid = false;
    bool tempoOctaveAmbiguous = false;

    bool valid() const noexcept
    {
        return transientRegions.valid()
            && std::isfinite(analyzedBpm) && analyzedBpm >= 0.0
            && analyzedBpm <= 999.0
            && std::isfinite(tempoConfidence)
            && tempoConfidence >= 0.0f && tempoConfidence <= 1.0f
            && (!tempoValid || analyzedBpm > 0.0);
    }
};

struct CutPatternStep {
    uint8_t lane = 0u;
    float source = 0.0f;

    bool valid() const noexcept
    {
        return lane < kSampleCutupsLaneCount && std::isfinite(source)
            && source >= 0.0f && source <= 1.0f;
    }
};

inline std::array<CutPatternStep, kMaximumCutupsPatternSteps>
defaultCutupsPattern() noexcept
{
    std::array<CutPatternStep, kMaximumCutupsPatternSteps> result {};
    for (std::size_t index = 0u; index < result.size(); ++index) {
        result[index].lane = static_cast<uint8_t>(
            index % kSampleCutupsLaneCount);
        result[index].source = static_cast<float>(index)
            / static_cast<float>(result.size());
    }
    return result;
}

inline uint32_t cutupsFileOrderIndex(CutFileOrder order, uint32_t step,
    uint32_t count, uint32_t seed) noexcept
{
    count = std::min<uint32_t>(count,
        static_cast<uint32_t>(kSampleCutupsLaneCount));
    if (count <= 1u) return 0u;
    const uint32_t position = step % count;
    switch (order) {
    case CutFileOrder::Up:
        return count - 1u - position;
    case CutFileOrder::Palindrome: {
        const uint32_t period = (count - 1u) * 2u;
        const uint32_t phase = step % period;
        return phase < count ? phase : period - phase;
    }
    case CutFileOrder::Random: {
        uint32_t state = seed ^ (step + 1u) * 0x9e3779b9u;
        state ^= state << 13u;
        state ^= state >> 17u;
        state ^= state << 5u;
        return state % count;
    }
    case CutFileOrder::RandomCycle: {
        std::array<uint8_t, kSampleCutupsLaneCount> bag {{ 0u, 1u, 2u, 3u }};
        uint32_t state = seed ^ (step / count + 1u) * 0x85ebca6bu;
        for (uint32_t remaining = count; remaining > 1u; --remaining) {
            state ^= state << 13u;
            state ^= state >> 17u;
            state ^= state << 5u;
            std::swap(bag[remaining - 1u], bag[state % remaining]);
        }
        return bag[position];
    }
    case CutFileOrder::Pairs:
        return (step / 2u) % count;
    case CutFileOrder::OutsideIn:
        return (position & 1u) == 0u
            ? position / 2u : count - 1u - position / 2u;
    case CutFileOrder::CenterOut: {
        const uint32_t center = (count - 1u) / 2u;
        if ((count & 1u) == 0u)
            return (position & 1u) == 0u
                ? center - position / 2u
                : center + 1u + position / 2u;
        if (position == 0u) return center;
        return (position & 1u) != 0u
            ? center - (position + 1u) / 2u
            : center + position / 2u;
    }
    case CutFileOrder::Stagger: {
        constexpr std::array<uint8_t, 8u> pattern {{
            0u, 2u, 1u, 3u, 1u, 0u, 3u, 2u,
        }};
        return pattern[step % pattern.size()] % count;
    }
    case CutFileOrder::Manual:
    case CutFileOrder::Down:
    default:
        return position;
    }
}

inline uint32_t cutupsFileOrderPeriod(CutFileOrder order, uint32_t count,
    uint32_t manualLength) noexcept
{
    count = std::max(1u, count);
    switch (order) {
    case CutFileOrder::Palindrome:
        return count > 1u ? (count - 1u) * 2u : 1u;
    case CutFileOrder::Pairs:
        return count * 2u;
    case CutFileOrder::Stagger:
        return 8u;
    case CutFileOrder::Manual:
    case CutFileOrder::Random:
        return std::max(1u, manualLength);
    case CutFileOrder::Down:
    case CutFileOrder::Up:
    case CutFileOrder::RandomCycle:
    case CutFileOrder::OutsideIn:
    case CutFileOrder::CenterOut:
    default:
        return count;
    }
}

inline uint32_t cutupsRelatedFileOrderStep(CutPolyPathMode mode,
    uint32_t primaryStep, uint32_t voicePathIndex,
    uint32_t period) noexcept
{
    period = std::max(1u, period);
    const uint32_t path = voicePathIndex % kSampleCutupsLaneCount;
    const uint32_t phase = primaryStep % period;
    switch (mode) {
    case CutPolyPathMode::StepOffset:
        return primaryStep + path;
    case CutPolyPathMode::QuarterSpread: {
        const uint32_t spacing = std::max(1u, (period + 3u) / 4u);
        return primaryStep + path * spacing;
    }
    case CutPolyPathMode::MirrorPairs: {
        if (path == 0u) return primaryStep;
        const uint32_t half = std::max(1u, (period + 1u) / 2u);
        const uint32_t related = path >= 2u
            ? (phase + half) % period : phase;
        return (path & 1u) != 0u
            ? period - 1u - related : related;
    }
    case CutPolyPathMode::Together:
    default:
        return primaryStep;
    }
}

struct SampleCutupsSettings {
    CutClockBasis clockBasis = CutClockBasis::Host;
    CutDivision division = CutDivision::Sixteenth;
    CutRegionMode regionMode = CutRegionMode::Equal;
    CutFileOrder fileOrder = CutFileOrder::RandomCycle;
    CutSourceOrder sourceOrder = CutSourceOrder::Forward;
    CutPolyPathMode polyPathMode = CutPolyPathMode::Together;
    CutOutputMode outputMode = CutOutputMode::Preserve;
    CutAllocationCadence allocationCadence = CutAllocationCadence::Note;
    VoiceMode voiceMode = VoiceMode::Poly;
    TriggerMode triggerMode = TriggerMode::Gate;
    double start = 0.0;
    double end = 1.0;
    float cutRateHz = 8.0f;
    float swing = 0.0f;
    float timeVariation = 0.0f;
    float gate = 1.0f;
    float joinMilliseconds = 5.0f;
    uint32_t regionCount = 16u;
    uint32_t patternLength = 16u;
    uint32_t repeatCount = 1u;
    float reverseChance = 0.0f;
    float tuneSemitones = 0.0f;
    float fineTuneCents = 0.0f;
    float pitchVariationSemitones = 0.0f;
    float levelVariation = 0.0f;
    uint8_t rootNote = 60u;
    float attackSeconds = 0.003f;
    float releaseSeconds = 0.020f;
    float outputGainDecibels = -6.0f;
    float pan = 0.0f;
    float velocitySensitivity = 1.0f;
    bool tempoSync = true;
    std::array<float, kSampleCutupsLaneCount> laneBpm {{
        120.0f, 120.0f, 120.0f, 120.0f,
    }};
    uint32_t seed = 1u;
    uint32_t activeOutputChannels = 32u;
    s3g::routing::VoiceOutputRouting outputRouting {};
    std::array<CutPatternStep, kMaximumCutupsPatternSteps> manualPattern
        = defaultCutupsPattern();

    bool valid() const noexcept
    {
        return static_cast<uint8_t>(clockBasis)
                <= static_cast<uint8_t>(CutClockBasis::Host)
            && static_cast<uint8_t>(division)
                <= static_cast<uint8_t>(CutDivision::SixteenthTriplet)
            && static_cast<uint8_t>(regionMode)
                <= static_cast<uint8_t>(CutRegionMode::Transient)
            && static_cast<uint8_t>(fileOrder)
                <= static_cast<uint8_t>(CutFileOrder::CenterOut)
            && static_cast<uint8_t>(sourceOrder)
                <= static_cast<uint8_t>(CutSourceOrder::Manual)
            && static_cast<uint8_t>(polyPathMode)
                <= static_cast<uint8_t>(CutPolyPathMode::MirrorPairs)
            && static_cast<uint8_t>(outputMode)
                <= static_cast<uint8_t>(CutOutputMode::Distribute)
            && static_cast<uint8_t>(allocationCadence)
                <= static_cast<uint8_t>(CutAllocationCadence::Pattern)
            && static_cast<uint8_t>(voiceMode)
                <= static_cast<uint8_t>(VoiceMode::Legato)
            && static_cast<uint8_t>(triggerMode)
                <= static_cast<uint8_t>(TriggerMode::Toggle)
            && std::isfinite(start) && std::isfinite(end)
            && start >= 0.0 && start < end && end <= 1.0
            && std::isfinite(cutRateHz) && cutRateHz >= 0.1f
            && cutRateHz <= 80.0f
            && std::isfinite(swing) && swing >= 0.0f && swing <= 0.75f
            && std::isfinite(timeVariation) && timeVariation >= 0.0f
            && timeVariation <= 1.0f
            && std::isfinite(gate) && gate >= 0.05f && gate <= 1.0f
            && std::isfinite(joinMilliseconds)
            && joinMilliseconds >= 0.0f && joinMilliseconds <= 100.0f
            && regionCount >= 1u && regionCount <= kMaximumCutupsRegions
            && patternLength >= 1u
            && patternLength <= kMaximumCutupsPatternSteps
            && repeatCount >= 1u && repeatCount <= 16u
            && std::isfinite(reverseChance) && reverseChance >= 0.0f
            && reverseChance <= 1.0f
            && std::isfinite(tuneSemitones)
            && tuneSemitones >= -60.0f && tuneSemitones <= 60.0f
            && std::isfinite(fineTuneCents)
            && fineTuneCents >= -100.0f && fineTuneCents <= 100.0f
            && std::isfinite(pitchVariationSemitones)
            && pitchVariationSemitones >= 0.0f
            && pitchVariationSemitones <= 24.0f
            && std::isfinite(levelVariation) && levelVariation >= 0.0f
            && levelVariation <= 1.0f
            && std::isfinite(attackSeconds) && attackSeconds >= 0.0f
            && attackSeconds <= 2.0f
            && std::isfinite(releaseSeconds) && releaseSeconds >= 0.0f
            && releaseSeconds <= 2.0f
            && std::isfinite(outputGainDecibels)
            && outputGainDecibels >= -60.0f
            && outputGainDecibels <= 12.0f
            && std::isfinite(pan) && pan >= -1.0f && pan <= 1.0f
            && std::isfinite(velocitySensitivity)
            && velocitySensitivity >= 0.0f && velocitySensitivity <= 1.0f
            && std::all_of(laneBpm.begin(), laneBpm.end(), [](float bpm) {
                return std::isfinite(bpm) && bpm >= 20.0f && bpm <= 400.0f;
            })
            && activeOutputChannels >= 2u && activeOutputChannels <= 32u
            && outputRouting.valid()
            && std::all_of(manualPattern.begin(),
                manualPattern.begin() + patternLength,
                [](const CutPatternStep& step) { return step.valid(); });
    }
};

struct SampleCutupsVoiceCursor {
    float sourcePositionNormalized = -1.0f;
    float lanePositionNormalized = 0.0f;
    float pathPhase = 0.0f;
    std::array<float, kSampleCutupsLaneCount> laneSourcePositions {{
        -1.0f, -1.0f, -1.0f, -1.0f,
    }};
    std::array<float, kSampleCutupsLaneCount> laneWeights {};
    uint8_t lane = 0u;
    uint8_t region = 0u;
    uint8_t patternStep = 0u;
    uint8_t key = 0u;
    uint8_t outputFirstChannel = 0u;
    uint8_t outputChannelCount = 0u;
    uint64_t identity = 0u;
};

class SampleCutupsEngine {
public:
    bool prepare(double outputSampleRate,
        uint32_t outputChannelCount = 2u) noexcept
    {
        if (!(outputSampleRate > 0.0) || !std::isfinite(outputSampleRate)
            || outputChannelCount < 2u || outputChannelCount > 32u)
            return false;
        outputSampleRate_ = outputSampleRate;
        outputChannelCount_ = outputChannelCount;
        prepared_ = true;
        reset();
        return true;
    }

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
        metadata_ = {};
        prepared_ = false;
    }

    bool setAsset(std::size_t lane, const SampleAsset* asset) noexcept
    {
        if (lane >= assets_.size() || (asset && !asset->valid()))
            return false;
        assets_[lane] = asset;
        if (!asset) metadata_[lane] = nullptr;
        return true;
    }

    bool setAssets(const std::array<const SampleAsset*,
        kSampleCutupsLaneCount>& assets) noexcept
    {
        for (const auto* asset : assets)
            if (asset && !asset->valid()) return false;
        assets_ = assets;
        return true;
    }

    bool setMetadata(std::size_t lane,
        const CutupsLaneMetadata* metadata) noexcept
    {
        if (lane >= metadata_.size() || (metadata && !metadata->valid()))
            return false;
        metadata_[lane] = metadata;
        return true;
    }

    void setPreparedAsset(std::size_t lane,
        const SampleAsset* asset) noexcept
    {
        if (lane < assets_.size()) assets_[lane] = asset;
    }

    void setPreparedMetadata(std::size_t lane,
        const CutupsLaneMetadata* metadata) noexcept
    {
        if (lane < metadata_.size()) metadata_[lane] = metadata;
    }

    uint32_t activeVoiceCount() const noexcept
    {
        uint32_t count = 0u;
        for (const auto& voice : voices_) if (voice.active) ++count;
        return count;
    }

    uint32_t voiceCursorCount() const noexcept { return cursorCount_; }
    float outputPeak() const noexcept { return outputPeak_; }
    const std::array<SampleCutupsVoiceCursor, kMaximumCutupsVoices>&
        voiceCursors() const noexcept { return cursors_; }

    void render(const SampleCutupsSettings& settings,
        const CutupsRenderEvent* events, std::size_t eventCount,
        float* left, float* right, uint32_t frameCount,
        double hostTempoBpm = 120.0) noexcept
    {
        std::array<float*, 2u> outputs {{ left, right }};
        render(settings, events, eventCount, outputs.data(), 2u,
            frameCount, hostTempoBpm);
    }

    void render(const SampleCutupsSettings& settings,
        const CutupsRenderEvent* events, std::size_t eventCount,
        float* const* outputs, uint32_t outputChannelCount,
        uint32_t frameCount, double hostTempoBpm = 120.0) noexcept
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
        hostTempoBpm = std::isfinite(hostTempoBpm) && hostTempoBpm > 0.0
            ? std::clamp(hostTempoBpm, 1.0, 999.0) : 120.0;
        if (allocatorSeed_ != settings.seed) {
            allocator_.reset(settings.seed);
            allocatorSeed_ = settings.seed;
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
                applyEvent(events[eventIndex], settings, hostTempoBpm);
                ++eventIndex;
            }
            for (auto& voice : voices_) {
                if (!voice.active) continue;
                updateEnvelope(voice, settings);
                if (!voice.active) continue;
                if (voice.framesUntilCut <= 0.0)
                    beginCut(voice, settings, hostTempoBpm);
                renderVoiceFrame(voice, settings, outputs,
                    outputChannelCount, frame, master, leftPan, rightPan,
                    hostTempoBpm);
                voice.framesUntilCut -= 1.0;
            }
        }
        while (eventIndex < eventCount
            && events[eventIndex].frameOffset <= frameCount) {
            applyEvent(events[eventIndex], settings, hostTempoBpm);
            ++eventIndex;
        }
        publishCursors(settings.patternLength);
    }

private:
    struct Reader {
        bool active = false;
        bool forward = true;
        uint8_t lane = 0u;
        uint8_t region = 0u;
        double position = 0.0;
        double increment = 0.0;
        uint32_t ageFrames = 0u;
        uint32_t gateFrames = 1u;
        uint32_t joinFrames = 0u;
        uint32_t retirementFrame = 0u;
        uint32_t retirementFrames = 0u;
        float gain = 1.0f;
        s3g::routing::VoiceOutputAssignment output {};
    };

    struct Voice {
        bool active = false;
        bool releasing = false;
        bool patternComplete = false;
        uint64_t noteId = 0u;
        uint64_t age = 0u;
        uint8_t key = 60u;
        uint8_t midiChannel = 0u;
        uint8_t polyPathIndex = 0u;
        float velocityGain = 1.0f;
        float envelope = 0.0f;
        float releaseDecrement = 0.0f;
        double framesUntilCut = 0.0;
        double timelinePosition = 0.0;
        uint32_t cutIndex = 0u;
        uint32_t patternStep = 0u;
        uint32_t repeatIndex = 0u;
        uint32_t walkRegion = 0u;
        uint32_t randomState = 1u;
        Reader current {};
        Reader tail {};
        s3g::routing::VoiceOutputAssignment noteOutput {};
    };

    static float decibelsToAmplitude(float decibels) noexcept
    {
        return decibels <= -60.0f ? 0.0f
            : std::pow(10.0f, decibels / 20.0f);
    }

    static uint32_t nextRandom(uint32_t& state) noexcept
    {
        uint32_t value = state != 0u ? state : 0x6d2b79f5u;
        value ^= value << 13u;
        value ^= value >> 17u;
        value ^= value << 5u;
        state = value != 0u ? value : 0x6d2b79f5u;
        return state;
    }

    static float randomUnit(uint32_t& state) noexcept
    {
        return static_cast<float>(nextRandom(state) >> 8u)
            * (1.0f / 16777216.0f);
    }

    static float randomBipolar(uint32_t& state) noexcept
    { return randomUnit(state) * 2.0f - 1.0f; }

    bool anyAsset() const noexcept
    {
        return std::any_of(assets_.begin(), assets_.end(),
            [](const SampleAsset* asset) { return asset != nullptr; });
    }

    uint32_t loadedLanes(std::array<uint8_t,
        kSampleCutupsLaneCount>& result) const noexcept
    {
        uint32_t count = 0u;
        for (uint8_t lane = 0u; lane < assets_.size(); ++lane)
            if (assets_[lane]) result[count++] = lane;
        return count;
    }

    static uint32_t palindromeIndex(uint32_t index, uint32_t count) noexcept
    {
        if (count <= 1u) return 0u;
        const uint32_t period = (count - 1u) * 2u;
        const uint32_t phase = index % period;
        return phase < count ? phase : period - phase;
    }

    uint8_t chooseLane(Voice& voice,
        const SampleCutupsSettings& settings) noexcept
    {
        std::array<uint8_t, kSampleCutupsLaneCount> lanes {};
        const uint32_t count = loadedLanes(lanes);
        if (count == 0u) return 0u;
        const uint32_t period = cutupsFileOrderPeriod(settings.fileOrder,
            count, settings.patternLength);
        const uint32_t step = cutupsRelatedFileOrderStep(
            settings.voiceMode == VoiceMode::Poly
                ? settings.polyPathMode : CutPolyPathMode::Together,
            voice.patternStep, voice.polyPathIndex, period);
        if (settings.fileOrder == CutFileOrder::Manual) {
            const uint8_t requested = settings.manualPattern[
                step % settings.patternLength].lane;
            if (assets_[requested]) return requested;
            uint8_t nearest = lanes[0u];
            uint8_t distance = static_cast<uint8_t>(
                std::abs(static_cast<int>(nearest)
                    - static_cast<int>(requested)));
            for (uint32_t index = 1u; index < count; ++index) {
                const uint8_t candidateDistance = static_cast<uint8_t>(
                    std::abs(static_cast<int>(lanes[index])
                        - static_cast<int>(requested)));
                if (candidateDistance < distance) {
                    nearest = lanes[index];
                    distance = candidateDistance;
                }
            }
            return nearest;
        }
        return lanes[cutupsFileOrderIndex(settings.fileOrder, step,
            count, settings.seed)];
    }

    uint32_t availableRegionCount(uint8_t lane,
        const SampleCutupsSettings& settings) const noexcept
    {
        if (settings.regionMode != CutRegionMode::Transient
            || !metadata_[lane] || !metadata_[lane]->valid())
            return settings.regionCount;
        uint32_t count = 0u;
        const auto& regions = metadata_[lane]->transientRegions;
        for (uint32_t index = 0u; index < regions.count; ++index) {
            const double value = regions.starts[index];
            if (value >= settings.start && value < settings.end) ++count;
        }
        return std::max(1u, std::min(count, settings.regionCount));
    }

    double regionPosition(uint8_t lane, uint32_t ordinal,
        uint32_t count, const SampleCutupsSettings& settings) const noexcept
    {
        if (settings.regionMode == CutRegionMode::Transient
            && metadata_[lane] && metadata_[lane]->valid()) {
            const auto& regions = metadata_[lane]->transientRegions;
            uint32_t found = 0u;
            for (uint32_t index = 0u; index < regions.count; ++index) {
                const double value = regions.starts[index];
                if (value < settings.start || value >= settings.end)
                    continue;
                if (found == ordinal) return value;
                ++found;
                if (found >= count) break;
            }
        }
        return settings.start + (settings.end - settings.start)
            * static_cast<double>(ordinal % std::max(1u, count))
            / static_cast<double>(std::max(1u, count));
    }

    uint32_t chooseRegionOrdinal(Voice& voice, uint8_t lane,
        const SampleCutupsSettings& settings) noexcept
    {
        const uint32_t count = availableRegionCount(lane, settings);
        const uint32_t step = voice.patternStep;
        switch (settings.sourceOrder) {
        case CutSourceOrder::Reverse:
            return count - 1u - step % count;
        case CutSourceOrder::Palindrome:
            return palindromeIndex(step, count);
        case CutSourceOrder::Random:
            return nextRandom(voice.randomState) % count;
        case CutSourceOrder::Walk: {
            if (voice.cutIndex == 0u) voice.walkRegion = step % count;
            else {
                const int move = randomUnit(voice.randomState) < 0.5f
                    ? -1 : 1;
                voice.walkRegion = static_cast<uint32_t>((
                    static_cast<int>(voice.walkRegion) + move
                    + static_cast<int>(count)) % static_cast<int>(count));
            }
            return voice.walkRegion;
        }
        case CutSourceOrder::Manual: {
            const float source = settings.manualPattern[
                step % settings.patternLength].source;
            return std::min(count - 1u, static_cast<uint32_t>(
                source * static_cast<float>(count)));
        }
        case CutSourceOrder::Timeline:
            return std::min(count - 1u, static_cast<uint32_t>(
                std::clamp((voice.timelinePosition - settings.start)
                        / (settings.end - settings.start), 0.0, 0.999999)
                    * static_cast<double>(count)));
        case CutSourceOrder::Forward:
        default:
            return step % count;
        }
    }

    static double divisionBeats(CutDivision division) noexcept
    {
        switch (division) {
        case CutDivision::Whole: return 4.0;
        case CutDivision::Half: return 2.0;
        case CutDivision::Quarter: return 1.0;
        case CutDivision::Eighth: return 0.5;
        case CutDivision::Sixteenth: return 0.25;
        case CutDivision::ThirtySecond: return 0.125;
        case CutDivision::EighthTriplet: return 1.0 / 3.0;
        case CutDivision::SixteenthTriplet: return 1.0 / 6.0;
        }
        return 0.25;
    }

    double intervalFrames(Voice& voice,
        const SampleCutupsSettings& settings,
        double hostTempoBpm) noexcept
    {
        double seconds = settings.clockBasis == CutClockBasis::Host
            ? divisionBeats(settings.division) * 60.0 / hostTempoBpm
            : 1.0 / static_cast<double>(settings.cutRateHz);
        if ((voice.cutIndex & 1u) == 0u)
            seconds *= 1.0 + static_cast<double>(settings.swing);
        else seconds *= 1.0 - static_cast<double>(settings.swing);
        seconds *= std::max(0.1, 1.0
            + static_cast<double>(randomBipolar(voice.randomState))
                * settings.timeVariation * 0.45);
        return std::max(1.0, seconds * outputSampleRate_);
    }

    s3g::routing::VoiceOutputAssignment allocateOutput(
        const SampleCutupsSettings& settings) noexcept
    {
        return allocator_.next(std::min(settings.activeOutputChannels,
            outputChannelCount_), settings.outputRouting);
    }

    double readerIncrement(const Voice& voice, uint8_t lane,
        const SampleCutupsSettings& settings,
        double hostTempoBpm, float pitchVariation) const noexcept
    {
        const auto* asset = assets_[lane];
        if (!asset || asset->frameCount() < 2u) return 0.0;
        const double semitones = static_cast<double>(voice.key)
            - settings.rootNote + settings.tuneSemitones
            + settings.fineTuneCents * 0.01 + pitchVariation;
        double ratio = std::pow(2.0, semitones / 12.0);
        if (settings.tempoSync && settings.clockBasis == CutClockBasis::Host)
            ratio *= hostTempoBpm / settings.laneBpm[lane];
        return asset->sampleRate / outputSampleRate_
            / static_cast<double>(asset->frameCount() - 1u) * ratio;
    }

    void beginCut(Voice& voice, const SampleCutupsSettings& settings,
        double hostTempoBpm) noexcept
    {
        if (settings.triggerMode == TriggerMode::OneShot
            && voice.patternComplete) {
            voice.releasing = true;
            voice.framesUntilCut = std::numeric_limits<double>::max();
            return;
        }
        const uint32_t completedStep = voice.patternStep;
        const bool repeatingAddress = voice.cutIndex > 0u
            && voice.repeatIndex > 0u;
        const uint8_t lane = repeatingAddress
            ? voice.current.lane : chooseLane(voice, settings);
        const uint32_t regionCount = availableRegionCount(lane, settings);
        const uint32_t region = repeatingAddress
            ? std::min<uint32_t>(voice.current.region, regionCount - 1u)
            : chooseRegionOrdinal(voice, lane, settings);
        double position = regionPosition(lane, region, regionCount, settings);
        if (settings.sourceOrder == CutSourceOrder::Timeline)
            position = std::clamp(voice.timelinePosition,
                settings.start, std::nextafter(settings.end, settings.start));

        const double interval = intervalFrames(voice, settings,
            hostTempoBpm);
        const uint32_t join = static_cast<uint32_t>(std::min<double>(
            settings.joinMilliseconds * outputSampleRate_ * 0.001,
            interval * 0.5));
        if (voice.current.active && join > 0u) {
            voice.tail = voice.current;
            voice.tail.retirementFrame = 0u;
            voice.tail.retirementFrames = join;
        } else voice.tail = {};

        Reader reader;
        reader.active = true;
        reader.lane = lane;
        reader.region = static_cast<uint8_t>(std::min<uint32_t>(
            region, std::numeric_limits<uint8_t>::max()));
        reader.position = position;
        reader.forward = randomUnit(voice.randomState)
            >= settings.reverseChance;
        const float pitchVariation = randomBipolar(voice.randomState)
            * settings.pitchVariationSemitones;
        reader.increment = readerIncrement(voice, lane, settings,
            hostTempoBpm, pitchVariation);
        reader.gain = 1.0f - randomUnit(voice.randomState)
            * settings.levelVariation;
        reader.joinFrames = join;
        reader.gateFrames = std::max(1u, static_cast<uint32_t>(
            interval * settings.gate));
        const bool completesStep = voice.repeatIndex + 1u
            >= settings.repeatCount;
        const bool newPattern = completesStep
            && completedStep + 1u >= settings.patternLength;
        if (settings.outputMode == CutOutputMode::Distribute) {
            if (settings.allocationCadence == CutAllocationCadence::Cut) {
                reader.output = allocateOutput(settings);
            } else {
                const bool startsNewPattern = voice.cutIndex > 0u
                    && completedStep == 0u && voice.repeatIndex == 0u;
                if (settings.allocationCadence
                        == CutAllocationCadence::Pattern
                    && startsNewPattern)
                    voice.noteOutput = allocateOutput(settings);
                reader.output = voice.noteOutput;
            }
        }
        voice.current = reader;
        voice.framesUntilCut += interval;
        ++voice.cutIndex;
        ++voice.repeatIndex;
        if (voice.repeatIndex >= settings.repeatCount) {
            voice.repeatIndex = 0u;
            voice.patternStep = (voice.patternStep + 1u)
                % settings.patternLength;
        }
        if (settings.triggerMode == TriggerMode::OneShot && newPattern)
            voice.patternComplete = true;
    }

    static float interpolate(const SampleAsset& asset, uint8_t channel,
        double normalized) noexcept
    {
        if (channel >= asset.channelCount || asset.frameCount() == 0u)
            return 0.0f;
        normalized = std::clamp(normalized, 0.0, 1.0);
        const double frame = normalized
            * static_cast<double>(asset.frameCount() - 1u);
        const uint32_t first = static_cast<uint32_t>(frame);
        const uint32_t second = std::min(first + 1u,
            asset.frameCount() - 1u);
        const float fraction = static_cast<float>(frame - first);
        const auto& samples = asset.channels[channel];
        return samples[first] + (samples[second] - samples[first])
            * fraction;
    }

    static float readerEnvelope(const Reader& reader) noexcept
    {
        if (!reader.active || reader.ageFrames >= reader.gateFrames)
            return 0.0f;
        float gain = 1.0f;
        if (reader.joinFrames > 0u && reader.ageFrames < reader.joinFrames) {
            const float phase = static_cast<float>(reader.ageFrames + 1u)
                / static_cast<float>(reader.joinFrames);
            gain *= std::sin(std::min(1.0f, phase)
                * 1.57079632679f);
        }
        if (reader.joinFrames > 0u
            && reader.ageFrames + reader.joinFrames > reader.gateFrames) {
            const uint32_t remaining = reader.gateFrames - reader.ageFrames;
            const float phase = static_cast<float>(remaining)
                / static_cast<float>(reader.joinFrames);
            gain *= std::sin(std::min(1.0f, phase)
                * 1.57079632679f);
        }
        return gain;
    }

    static float tailEnvelope(const Reader& reader) noexcept
    {
        if (!reader.active || reader.retirementFrames == 0u
            || reader.retirementFrame >= reader.retirementFrames)
            return 0.0f;
        const float phase = static_cast<float>(reader.retirementFrame)
            / static_cast<float>(reader.retirementFrames);
        return std::cos(std::min(1.0f, phase) * 1.57079632679f);
    }

    static void advanceReader(Reader& reader,
        const SampleCutupsSettings& settings) noexcept
    {
        if (!reader.active) return;
        reader.position += reader.forward ? reader.increment
            : -reader.increment;
        const double width = settings.end - settings.start;
        while (reader.position >= settings.end) reader.position -= width;
        while (reader.position < settings.start) reader.position += width;
        ++reader.ageFrames;
        if (reader.retirementFrames > 0u) {
            ++reader.retirementFrame;
            if (reader.retirementFrame >= reader.retirementFrames)
                reader.active = false;
        } else if (reader.ageFrames >= reader.gateFrames)
            reader.active = false;
    }

    float objectSample(const Reader& reader, bool right,
        bool stereo) const noexcept
    {
        const auto* asset = assets_[reader.lane];
        if (!asset) return 0.0f;
        if (!stereo) {
            float sum = 0.0f;
            for (uint8_t channel = 0u; channel < asset->channelCount;
                 ++channel)
                sum += interpolate(*asset, channel, reader.position);
            return sum / std::sqrt(static_cast<float>(asset->channelCount));
        }
        if (asset->channelCount == 1u)
            return interpolate(*asset, 0u, reader.position);
        if (asset->channelCount == 2u)
            return interpolate(*asset, right ? 1u : 0u, reader.position);
        float sum = 0.0f;
        uint32_t count = 0u;
        for (uint8_t channel = right ? 1u : 0u;
             channel < asset->channelCount; channel += 2u) {
            sum += interpolate(*asset, channel, reader.position);
            ++count;
        }
        return count > 0u ? sum / std::sqrt(static_cast<float>(count))
            : 0.0f;
    }

    void renderReader(const Reader& reader, float readerGain,
        float voiceGain, const SampleCutupsSettings& settings,
        float* const* outputs, uint32_t outputChannelCount, uint32_t frame,
        float leftPan, float rightPan) noexcept
    {
        if (!reader.active || !(readerGain > 0.0f)) return;
        const auto* asset = assets_[reader.lane];
        if (!asset) return;
        const float gain = readerGain * reader.gain * voiceGain;
        if (settings.outputMode == CutOutputMode::Preserve) {
            for (uint32_t channel = 0u; channel < outputChannelCount;
                 ++channel) {
                const uint8_t sourceChannel = asset->channelCount == 1u
                        && outputChannelCount == 2u
                    ? 0u : static_cast<uint8_t>(channel);
                if (sourceChannel >= asset->channelCount) continue;
                float channelGain = gain;
                if (outputChannelCount == 2u)
                    channelGain *= channel == 0u ? leftPan : rightPan;
                outputs[channel][frame] += interpolate(*asset,
                    sourceChannel, reader.position) * channelGain;
                outputPeak_ = std::max(outputPeak_,
                    std::abs(outputs[channel][frame]));
            }
            return;
        }
        const bool stereo = reader.output.channelCount > 1u;
        const float left = objectSample(reader, false, stereo)
            * gain * leftPan;
        const uint32_t first = reader.output.firstChannel;
        if (first < outputChannelCount) {
            outputs[first][frame] += left;
            outputPeak_ = std::max(outputPeak_,
                std::abs(outputs[first][frame]));
        }
        if (stereo) {
            const uint32_t second = reader.output.secondChannel;
            if (second < outputChannelCount) {
                outputs[second][frame] += objectSample(reader, true, true)
                    * gain * rightPan;
                outputPeak_ = std::max(outputPeak_,
                    std::abs(outputs[second][frame]));
            }
        }
    }

    void renderVoiceFrame(Voice& voice,
        const SampleCutupsSettings& settings, float* const* outputs,
        uint32_t outputChannelCount, uint32_t frame, float master,
        float leftPan, float rightPan, double) noexcept
    {
        const float voiceGain = voice.envelope * voice.velocityGain * master;
        renderReader(voice.tail, tailEnvelope(voice.tail), voiceGain,
            settings, outputs, outputChannelCount, frame, leftPan, rightPan);
        renderReader(voice.current, readerEnvelope(voice.current), voiceGain,
            settings, outputs, outputChannelCount, frame, leftPan, rightPan);
        if (settings.sourceOrder == CutSourceOrder::Timeline
            && voice.current.active)
            voice.timelinePosition = voice.current.position;
        advanceReader(voice.tail, settings);
        advanceReader(voice.current, settings);
    }

    void updateEnvelope(Voice& voice,
        const SampleCutupsSettings& settings) noexcept
    {
        if (voice.releasing) {
            if (settings.releaseSeconds <= 0.0f) {
                voice = {};
                return;
            }
            if (!(voice.releaseDecrement > 0.0f))
                voice.releaseDecrement = voice.envelope
                    / static_cast<float>(std::max(1.0,
                        settings.releaseSeconds * outputSampleRate_));
            voice.envelope -= voice.releaseDecrement;
            if (voice.envelope <= 0.0f) voice = {};
        } else if (voice.envelope < 1.0f) {
            if (settings.attackSeconds <= 0.0f) voice.envelope = 1.0f;
            else voice.envelope = std::min(1.0f, voice.envelope
                + 1.0f / static_cast<float>(std::max(1.0,
                    settings.attackSeconds * outputSampleRate_)));
        }
    }

    Voice* newestVoice() noexcept
    {
        Voice* newest = nullptr;
        for (auto& voice : voices_)
            if (voice.active && (!newest || voice.age > newest->age))
                newest = &voice;
        return newest;
    }

    Voice* findVoice(const CutupsRenderEvent& event) noexcept
    {
        for (auto& voice : voices_)
            if (voice.active && voice.noteId == event.noteId
                && voice.key == event.key
                && voice.midiChannel == event.midiChannel) return &voice;
        return nullptr;
    }

    Voice* allocateVoice() noexcept
    {
        for (auto& voice : voices_) if (!voice.active) return &voice;
        return &*std::min_element(voices_.begin(), voices_.end(),
            [](const Voice& left, const Voice& right) {
                return left.age < right.age;
            });
    }

    uint8_t nextPolyPathIndex() const noexcept
    {
        std::array<bool, kSampleCutupsLaneCount> used {};
        uint32_t activeCount = 0u;
        for (const auto& voice : voices_) {
            if (!voice.active) continue;
            used[voice.polyPathIndex % used.size()] = true;
            ++activeCount;
        }
        for (uint8_t index = 0u; index < used.size(); ++index)
            if (!used[index]) return index;
        return static_cast<uint8_t>(activeCount % used.size());
    }

    void startVoice(const CutupsRenderEvent& event,
        const SampleCutupsSettings& settings) noexcept
    {
        if (settings.triggerMode == TriggerMode::Toggle) {
            if (auto* existing = findVoice(event)) {
                existing->releasing = true;
                return;
            }
        }
        Voice* voice = nullptr;
        if (settings.voiceMode == VoiceMode::Legato)
            voice = newestVoice();
        if (voice && settings.voiceMode == VoiceMode::Legato) {
            voice->noteId = event.noteId;
            voice->key = event.key;
            voice->midiChannel = event.midiChannel;
            voice->velocityGain = 1.0f + (event.velocity - 1.0f)
                * settings.velocitySensitivity;
            voice->releasing = false;
            voice->releaseDecrement = 0.0f;
            return;
        }
        if (settings.voiceMode == VoiceMode::Mono) {
            for (auto& active : voices_) active = {};
        }
        const uint8_t polyPathIndex = settings.voiceMode == VoiceMode::Poly
            ? nextPolyPathIndex() : 0u;
        voice = allocateVoice();
        *voice = {};
        voice->active = true;
        voice->noteId = event.noteId;
        voice->key = event.key;
        voice->midiChannel = event.midiChannel;
        voice->polyPathIndex = polyPathIndex;
        voice->age = ++ageCounter_;
        voice->velocityGain = 1.0f + (event.velocity - 1.0f)
            * settings.velocitySensitivity;
        voice->timelinePosition = settings.start;
        voice->randomState = settings.seed
            ^ static_cast<uint32_t>(event.noteId)
            ^ (static_cast<uint32_t>(event.key) << 16u)
            ^ static_cast<uint32_t>(voice->age * 0x9e3779b9u);
        if (voice->randomState == 0u) voice->randomState = 1u;
        if (settings.outputMode == CutOutputMode::Distribute
            && settings.allocationCadence != CutAllocationCadence::Cut)
            voice->noteOutput = allocateOutput(settings);
    }

    void releaseVoice(const CutupsRenderEvent& event,
        const SampleCutupsSettings& settings) noexcept
    {
        if (settings.triggerMode == TriggerMode::OneShot
            || settings.triggerMode == TriggerMode::Toggle) return;
        for (auto& voice : voices_)
            if (voice.active && voice.noteId == event.noteId
                && voice.key == event.key
                && voice.midiChannel == event.midiChannel)
                voice.releasing = true;
    }

    void applyEvent(const CutupsRenderEvent& event,
        const SampleCutupsSettings& settings, double) noexcept
    {
        switch (event.kind) {
        case CutupsEventKind::NoteOn:
        case CutupsEventKind::Preview:
            startVoice(event, settings);
            break;
        case CutupsEventKind::NoteOff:
            releaseVoice(event, settings);
            break;
        case CutupsEventKind::StopAll:
            for (auto& voice : voices_) voice = {};
            break;
        }
    }

    void publishCursors(uint32_t patternLength) noexcept
    {
        cursorCount_ = 0u;
        for (const auto& voice : voices_) {
            if (!voice.active || !voice.current.active
                || cursorCount_ >= cursors_.size()) continue;
            auto& cursor = cursors_[cursorCount_++];
            cursor.sourcePositionNormalized = static_cast<float>(
                voice.current.position);
            cursor.lanePositionNormalized = static_cast<float>(
                voice.current.lane) / 3.0f;
            cursor.pathPhase = static_cast<float>(voice.patternStep)
                / static_cast<float>(std::max(1u, patternLength));
            cursor.laneSourcePositions.fill(-1.0f);
            cursor.laneWeights.fill(0.0f);
            cursor.laneSourcePositions[voice.current.lane]
                = cursor.sourcePositionNormalized;
            cursor.laneWeights[voice.current.lane] = 1.0f;
            cursor.lane = voice.current.lane;
            cursor.region = voice.current.region;
            cursor.patternStep = static_cast<uint8_t>(voice.patternStep);
            cursor.key = voice.key;
            cursor.identity = voice.noteId;
            cursor.outputFirstChannel = voice.current.output.firstChannel;
            cursor.outputChannelCount = voice.current.output.channelCount;
        }
    }

    double outputSampleRate_ = 48000.0;
    uint32_t outputChannelCount_ = 2u;
    bool prepared_ = false;
    std::array<const SampleAsset*, kSampleCutupsLaneCount> assets_ {};
    std::array<const CutupsLaneMetadata*, kSampleCutupsLaneCount> metadata_ {};
    std::array<Voice, kMaximumCutupsVoices> voices_ {};
    std::array<SampleCutupsVoiceCursor, kMaximumCutupsVoices> cursors_ {};
    uint32_t cursorCount_ = 0u;
    uint64_t ageCounter_ = 0u;
    float outputPeak_ = 0.0f;
    s3g::routing::TriggerOutputAllocator<32u> allocator_ {};
    uint32_t allocatorSeed_ = 0u;
};

} // namespace s3g::sample
