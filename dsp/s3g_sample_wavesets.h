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
#include <memory>
#include <utility>
#include <vector>

namespace s3g::sample {

constexpr std::size_t kMaximumWavesetVoices = 16u;
constexpr std::size_t kMaximumWavesetOutputChannels = 32u;

struct WavesetVoiceCursor {
    float sourcePositionNormalized = -1.0f;
    float groupPositionNormalized = -1.0f;
    float transportPositionNormalized = -1.0f;
    float oscillatorPhase = 0.0f;
    uint64_t identity = 0u;
    uint32_t cycleOffset = 0u;
    uint32_t repeatIndex = 0u;
    uint32_t randomState = 0u;
    uint8_t key = 0u;
    uint8_t outputFirstChannel = 0u;
    uint8_t outputSecondChannel = 1u;
    uint8_t outputChannelCount = 2u;
    bool directionForward = true;
    bool enteredLoop = false;
    bool pendulumForward = true;
};

enum class WavesetPlayMode : uint8_t {
    Forward = 0u,
    ForwardLoop,
    Reverse,
    ReverseLoop,
    ForwardPingPong,
    ReversePingPong,
};

enum class WavesetSourceMode : uint8_t {
    Left = 0u,
    Right,
    SumMono,
    TrueChannels,
};

enum class WavesetAdvanceMode : uint8_t {
    Stretch = 0u,
    Preserve,
    Hold,
};

enum class WavesetDirection : uint8_t {
    Forward = 0u,
    Reverse,
    Pendulum,
    Shuffle,
};

enum class WavesetShape : uint8_t {
    Repeat = 0u,
    Omit,
    Replace,
    Envelope,
    Multiply,
    Average,
    Interpolate,
    Fractal,
    AdditiveHarmonic,
    GroupReverse,
    CycleReverse,
    Telescope,
};

enum class WavesetCrossingDetail : uint8_t {
    Raw = 0u,
    Hz8000,
    Hz4000,
    Hz1000,
    Hz250,
};

struct WavesetUnit {
    uint32_t startFrame = 0u;
    uint32_t middleFrame = 0u;
    uint32_t endFrame = 1u;
    double startPosition = 0.0;
    double endPosition = 1.0;
    float peak = 0.0f;
    float rms = 0.0f;

    double sampleLength() const noexcept
    {
        return endPosition > startPosition
            ? endPosition - startPosition : 1.0;
    }
};

// Analysis is stored per source lane plus a mono sum. Wavesets 2 exposes the
// first two lanes, but the map and engine remain channel-count agnostic so a
// later fixed-width edition can reuse the same timing and rendering contract.
struct WavesetMap {
    std::shared_ptr<const SampleAsset> asset;
    std::array<std::vector<WavesetUnit>, kMaximumAudioChannels> channelUnits;
    std::vector<WavesetUnit> sumUnits;
    WavesetCrossingDetail detail = WavesetCrossingDetail::Raw;

    bool valid() const noexcept
    {
        if (!asset || !asset->valid() || sumUnits.empty()) return false;
        for (uint8_t channel = 0u; channel < asset->channelCount; ++channel)
            if (channelUnits[channel].empty()) return false;
        return true;
    }

    std::size_t unitCount() const noexcept { return sumUnits.size(); }
};

inline double wavesetAnalysisCutoff(WavesetCrossingDetail detail) noexcept
{
    switch (detail) {
    case WavesetCrossingDetail::Hz8000: return 8000.0;
    case WavesetCrossingDetail::Hz4000: return 4000.0;
    case WavesetCrossingDetail::Hz1000: return 1000.0;
    case WavesetCrossingDetail::Hz250: return 250.0;
    case WavesetCrossingDetail::Raw:
    default: return 0.0;
    }
}

template <typename SampleAt>
inline std::vector<WavesetUnit> analyzeWavesetSignal(uint32_t frames,
    double sampleRate, WavesetCrossingDetail detail, SampleAt sampleAt)
{
    std::vector<WavesetUnit> result;
    if (frames < 8u || !(sampleRate > 0.0)) return result;
    struct Crossing { uint32_t frame; double position; };
    std::vector<Crossing> crossings;
    crossings.reserve(static_cast<std::size_t>(frames / 8u) + 2u);
    const double cutoff = wavesetAnalysisCutoff(detail);
    const double coefficient = cutoff > 0.0
        ? 1.0 - std::exp(-2.0 * 3.14159265358979323846 * cutoff
            / sampleRate)
        : 1.0;
    double filtered = sampleAt(0u);
    double previous = filtered;
    constexpr uint32_t kMinimumCycleFrames = 4u;
    uint32_t lastCrossing = 0u;
    for (uint32_t frame = 1u; frame < frames; ++frame) {
        filtered += coefficient
            * (static_cast<double>(sampleAt(frame)) - filtered);
        if (previous <= 0.0 && filtered > 0.0
            && (crossings.empty()
                || frame - lastCrossing >= kMinimumCycleFrames)) {
            const double denominator = filtered - previous;
            const double fraction = std::abs(denominator) > 1.0e-15
                ? std::clamp(-previous / denominator, 0.0, 1.0) : 0.0;
            crossings.push_back({ frame,
                static_cast<double>(frame - 1u) + fraction });
            lastCrossing = frame;
        }
        previous = filtered;
    }
    if (crossings.size() < 2u) return result;
    result.reserve(crossings.size() - 1u);
    for (std::size_t index = 0u; index + 1u < crossings.size(); ++index) {
        WavesetUnit unit;
        unit.startPosition = crossings[index].position;
        unit.endPosition = crossings[index + 1u].position;
        unit.startFrame = static_cast<uint32_t>(std::floor(
            unit.startPosition));
        unit.endFrame = std::min<uint32_t>(frames,
            static_cast<uint32_t>(std::ceil(unit.endPosition)) + 1u);
        if (unit.endFrame <= unit.startFrame + 2u) continue;
        unit.middleFrame = std::clamp(static_cast<uint32_t>(std::floor(
            (unit.startPosition + unit.endPosition) * 0.5)),
            unit.startFrame + 1u, unit.endFrame - 1u);
        double squareSum = 0.0;
        float peak = 0.0f;
        for (uint32_t frame = unit.startFrame; frame < unit.endFrame;
             ++frame) {
            const float value = sampleAt(frame);
            peak = std::max(peak, std::abs(value));
            squareSum += static_cast<double>(value) * value;
        }
        unit.peak = peak;
        unit.rms = static_cast<float>(std::sqrt(squareSum
            / static_cast<double>(unit.endFrame - unit.startFrame)));
        result.push_back(unit);
    }
    return result;
}

inline std::shared_ptr<const WavesetMap> analyzeWavesets(
    std::shared_ptr<const SampleAsset> asset,
    WavesetCrossingDetail detail = WavesetCrossingDetail::Raw)
{
    if (!asset || !asset->valid() || asset->frameCount() < 8u) return {};
    auto map = std::make_shared<WavesetMap>();
    map->asset = std::move(asset);
    map->detail = detail;
    const uint32_t frames = map->asset->frameCount();
    for (uint8_t channel = 0u; channel < map->asset->channelCount;
         ++channel) {
        const auto& samples = map->asset->channels[channel];
        map->channelUnits[channel] = analyzeWavesetSignal(frames,
            map->asset->sampleRate, detail,
            [&samples](uint32_t frame) { return samples[frame]; });
    }
    map->sumUnits = analyzeWavesetSignal(frames, map->asset->sampleRate,
        detail, [&asset = *map->asset](uint32_t frame) {
            double sum = 0.0;
            for (uint8_t channel = 0u; channel < asset.channelCount;
                 ++channel) sum += asset.channels[channel][frame];
            return static_cast<float>(sum / asset.channelCount);
        });
    return map->valid() ? map : std::shared_ptr<const WavesetMap> {};
}

struct WavesetSettings {
    WavesetPlayMode playMode = WavesetPlayMode::Forward;
    WavesetSourceMode sourceMode = WavesetSourceMode::TrueChannels;
    VoiceMode voiceMode = VoiceMode::Poly;
    TriggerMode triggerMode = TriggerMode::Auto;
    WavesetAdvanceMode advance = WavesetAdvanceMode::Stretch;
    WavesetDirection direction = WavesetDirection::Forward;
    WavesetShape shape = WavesetShape::Repeat;
    s3g::routing::VoiceOutputRouting outputRouting {};
    uint32_t activeOutputChannelCount = 2u;
    double start = 0.0;
    double end = 1.0;
    double loopStart = 0.0;
    double loopEnd = 1.0;
    float tuneSemitones = 0.0f;
    float fineTuneCents = 0.0f;
    uint8_t rootNote = 60u;
    float attackSeconds = 0.003f;
    float releaseSeconds = 0.012f;
    float velocitySensitivity = 1.0f;
    uint32_t groupSize = 8u;
    uint32_t repeats = 2u;
    uint32_t stride = 1u;
    float processAmount = 0.0f;
    float joinAmount = 1.0f;
    float outputGainDecibels = -6.0f;

    bool valid() const noexcept
    {
        return static_cast<uint8_t>(playMode)
                <= static_cast<uint8_t>(WavesetPlayMode::ReversePingPong)
            && static_cast<uint8_t>(sourceMode)
                <= static_cast<uint8_t>(WavesetSourceMode::TrueChannels)
            && static_cast<uint8_t>(voiceMode)
                <= static_cast<uint8_t>(VoiceMode::Legato)
            && static_cast<uint8_t>(triggerMode)
                <= static_cast<uint8_t>(TriggerMode::Toggle)
            && static_cast<uint8_t>(shape)
                <= static_cast<uint8_t>(WavesetShape::Telescope)
            && outputRouting.valid()
            && activeOutputChannelCount >= 2u
            && activeOutputChannelCount <= kMaximumWavesetOutputChannels
            && groupSize >= 1u && groupSize <= 32u
            && repeats >= 1u && repeats <= 16u
            && stride >= 1u && stride <= 16u
            && std::isfinite(start) && std::isfinite(end)
            && std::isfinite(loopStart) && std::isfinite(loopEnd)
            && start >= 0.0 && start <= 1.0 && end >= start && end <= 1.0
            && loopStart >= start && loopStart <= loopEnd
            && loopEnd <= end && rootNote < 128u
            && std::isfinite(tuneSemitones)
            && std::isfinite(fineTuneCents)
            && std::isfinite(attackSeconds) && attackSeconds >= 0.0f
            && attackSeconds <= 2.0f
            && std::isfinite(releaseSeconds) && releaseSeconds >= 0.0f
            && releaseSeconds <= 2.0f
            && std::isfinite(velocitySensitivity)
            && velocitySensitivity >= 0.0f && velocitySensitivity <= 1.0f
            && std::isfinite(processAmount)
            && std::isfinite(joinAmount)
            && std::isfinite(outputGainDecibels);
    }
};

enum class WavesetEventKind : uint8_t {
    NoteOn = 0u,
    NoteOff,
    Preview,
    StopAll,
};

struct WavesetRenderEvent {
    uint32_t frameOffset = 0u;
    WavesetEventKind kind = WavesetEventKind::NoteOn;
    uint64_t noteId = 0u;
    uint8_t key = 60u;
    float velocity = 1.0f;
    uint8_t midiChannel = 0u;
};

class SampleWavesetsEngine {
public:
    bool prepare(double outputSampleRate, uint32_t outputChannelCount) noexcept
    {
        if (!(outputSampleRate > 0.0) || !std::isfinite(outputSampleRate)
            || outputChannelCount == 0u
            || outputChannelCount > kMaximumWavesetOutputChannels)
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
        voiceCursors_ = {};
        voiceCursorCount_ = 0u;
        ageCounter_ = 0u;
        outputAllocator_.reset();
        outputPeak_ = 0.0f;
    }

    void setMap(const WavesetMap* map) noexcept
    {
        map_ = map && map->valid() ? map : nullptr;
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

    const std::array<WavesetVoiceCursor, kMaximumWavesetVoices>&
    voiceCursors() const noexcept { return voiceCursors_; }

    float primaryPositionNormalized() const noexcept
    {
        const Voice* newest = nullptr;
        for (const auto& voice : voices_)
            if (voice.active && (!newest || voice.age > newest->age))
                newest = &voice;
        return newest ? static_cast<float>(newest->sourcePosition) : -1.0f;
    }

    uint8_t primaryKey() const noexcept
    {
        const Voice* newest = nullptr;
        for (const auto& voice : voices_)
            if (voice.active && (!newest || voice.age > newest->age))
                newest = &voice;
        return newest ? newest->key : 0u;
    }

    bool primaryDirectionForward() const noexcept
    {
        const Voice* newest = nullptr;
        for (const auto& voice : voices_)
            if (voice.active && (!newest || voice.age > newest->age))
                newest = &voice;
        return !newest || newest->progressionForward;
    }

    bool primaryEnteredLoop() const noexcept
    {
        const Voice* newest = nullptr;
        for (const auto& voice : voices_)
            if (voice.active && (!newest || voice.age > newest->age))
                newest = &voice;
        return newest && newest->enteredLoop;
    }

    void render(const WavesetSettings& settings,
        const WavesetRenderEvent* events, std::size_t eventCount,
        float* const* outputs, uint32_t outputChannelCount,
        uint32_t frameCount) noexcept
    {
        voiceCursorCount_ = 0u;
        if (!outputs || outputChannelCount == 0u
            || outputChannelCount > outputChannelCount_) return;
        for (uint32_t channel = 0u; channel < outputChannelCount; ++channel) {
            if (!outputs[channel]) return;
            std::fill(outputs[channel], outputs[channel] + frameCount, 0.0f);
        }
        outputPeak_ = 0.0f;
        if (!prepared_ || !map_ || !settings.valid() || frameCount == 0u)
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
                const float voiceGain = voice.envelope
                    * voice.velocityGain * master;
                const uint32_t first = voice.output.firstChannel;
                if (first < outputChannelCount)
                    outputs[first][frame] += renderVoiceSample(voice,
                        settings, 0u) * voiceGain;
                if (voice.output.channelCount > 1u) {
                    const uint32_t second = voice.output.secondChannel;
                    if (second < outputChannelCount)
                        outputs[second][frame] += renderVoiceSample(voice,
                            settings, 1u) * voiceGain;
                }
                advanceVoice(voice, settings);
                advanceTransport(voice, settings);
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
        for (const auto& voice : voices_) {
            if (!voice.active || voiceCursorCount_ >= voiceCursors_.size())
                continue;
            voiceCursors_[voiceCursorCount_++] = {
                static_cast<float>(readPositionNormalized(voice, settings)),
                static_cast<float>(voice.sourcePosition),
                static_cast<float>(voice.transportPosition),
                static_cast<float>(voice.oscillatorPhase), voice.age,
                voice.cycleOffset, voice.repeatIndex, voice.randomState,
                voice.key, voice.output.firstChannel,
                voice.output.secondChannel, voice.output.channelCount,
                voice.progressionForward, voice.enteredLoop,
                voice.pendulumForward,
            };
        }
    }

private:
    static constexpr std::size_t kSumUnitIndex = kMaximumAudioChannels;

    struct Voice {
        bool active = false;
        bool releasing = false;
        bool progressionForward = true;
        bool enteredLoop = false;
        bool pendulumForward = true;
        uint64_t noteId = 0u;
        uint64_t age = 0u;
        uint8_t key = 60u;
        uint8_t midiChannel = 0u;
        float velocityGain = 1.0f;
        float envelope = 0.0f;
        double sourcePosition = 0.0;
        double transportPosition = 0.0;
        double oscillatorPhase = 0.0;
        uint32_t cycleOffset = 0u;
        uint32_t repeatIndex = 0u;
        uint32_t randomState = 0x12345678u;
        s3g::routing::VoiceOutputAssignment output {};
        std::array<std::size_t, kMaximumAudioChannels + 1u> baseUnits {};
    };

    static float decibelsToAmplitude(float decibels) noexcept
    {
        return decibels <= -59.99f ? 0.0f
            : std::pow(10.0f, decibels / 20.0f);
    }

    static bool isReverse(WavesetPlayMode mode) noexcept
    {
        return mode == WavesetPlayMode::Reverse
            || mode == WavesetPlayMode::ReverseLoop
            || mode == WavesetPlayMode::ReversePingPong;
    }

    static bool isLooping(WavesetPlayMode mode) noexcept
    {
        return mode != WavesetPlayMode::Forward
            && mode != WavesetPlayMode::Reverse;
    }

    static bool isPingPong(WavesetPlayMode mode) noexcept
    {
        return mode == WavesetPlayMode::ForwardPingPong
            || mode == WavesetPlayMode::ReversePingPong;
    }

    const std::vector<WavesetUnit>& unitsForIndex(std::size_t index) const
        noexcept
    {
        if (index == kSumUnitIndex) return map_->sumUnits;
        return map_->channelUnits[std::min<std::size_t>(index,
            map_->asset->channelCount - 1u)];
    }

    std::size_t referenceIndex(WavesetSourceMode mode,
        uint8_t outputChannelCount) const noexcept
    {
        if (mode == WavesetSourceMode::Right
            && map_->asset->channelCount > 1u) return 1u;
        if (mode == WavesetSourceMode::SumMono
            || (mode == WavesetSourceMode::TrueChannels
                && outputChannelCount == 1u)) return kSumUnitIndex;
        return 0u;
    }

    static std::size_t nearestUnit(const std::vector<WavesetUnit>& units,
        double normalized, uint32_t frameCount) noexcept
    {
        const double position = std::clamp(normalized, 0.0, 1.0)
            * static_cast<double>(frameCount);
        auto found = std::lower_bound(units.begin(), units.end(), position,
            [](const WavesetUnit& unit, double target) {
                return unit.startPosition < target;
            });
        if (found == units.end()) return units.size() - 1u;
        if (found != units.begin()) {
            const auto prior = found - 1;
            if (std::abs(prior->startPosition - position)
                <= std::abs(found->startPosition - position))
                return static_cast<std::size_t>(prior - units.begin());
        }
        return static_cast<std::size_t>(found - units.begin());
    }

    void setVoicePosition(Voice& voice, double normalized) noexcept
    {
        voice.sourcePosition = std::clamp(normalized, 0.0, 1.0);
        const uint32_t frames = map_->asset->frameCount();
        for (uint8_t channel = 0u; channel < map_->asset->channelCount;
             ++channel)
            voice.baseUnits[channel] = nearestUnit(
                map_->channelUnits[channel], voice.sourcePosition, frames);
        voice.baseUnits[kSumUnitIndex] = nearestUnit(map_->sumUnits,
            voice.sourcePosition, frames);
        voice.cycleOffset = 0u;
        voice.repeatIndex = 0u;
    }

    std::size_t resolvedUnit(const Voice& voice, std::size_t mapIndex,
        uint32_t offset, const WavesetSettings& settings) const noexcept
    {
        const auto& units = unitsForIndex(mapIndex);
        const uint32_t group = std::clamp(settings.groupSize, 1u, 32u);
        uint32_t ordered = offset % group;
        if (settings.direction == WavesetDirection::Reverse)
            ordered = group - 1u - ordered;
        else if (settings.direction == WavesetDirection::Pendulum
            && !voice.pendulumForward) ordered = group - 1u - ordered;
        else if (settings.direction == WavesetDirection::Shuffle) {
            uint32_t mixed = voice.randomState
                ^ (offset * 0x9e3779b9u + 0x7f4a7c15u);
            mixed ^= mixed >> 16u;
            ordered = mixed % group;
        }
        return (voice.baseUnits[mapIndex] + ordered) % units.size();
    }

    double readPositionNormalized(const Voice& voice,
        const WavesetSettings& settings) const noexcept
    {
        const std::size_t mapIndex = referenceIndex(settings.sourceMode,
            voice.output.channelCount);
        const auto& units = unitsForIndex(mapIndex);
        const auto& unit = units[resolvedUnit(voice, mapIndex,
            voice.cycleOffset, settings)];
        const double position = unit.startPosition
            + std::clamp(voice.oscillatorPhase, 0.0, 1.0)
                * unit.sampleLength();
        return std::clamp(position
            / static_cast<double>(map_->asset->frameCount()), 0.0, 1.0);
    }

    float sampleAt(std::size_t sourceIndex, double position) const noexcept
    {
        position = std::clamp(position, 0.0,
            static_cast<double>(map_->asset->frameCount() - 1u));
        const uint32_t frame = static_cast<uint32_t>(position);
        const uint32_t next = std::min<uint32_t>(frame + 1u,
            map_->asset->frameCount() - 1u);
        const float fraction = static_cast<float>(position - frame);
        const auto lane = [&](uint8_t channel) {
            const auto& samples = map_->asset->channels[channel];
            return samples[frame] + (samples[next] - samples[frame])
                * fraction;
        };
        if (sourceIndex != kSumUnitIndex)
            return lane(static_cast<uint8_t>(std::min<std::size_t>(
                sourceIndex, map_->asset->channelCount - 1u)));
        float sum = 0.0f;
        for (uint8_t channel = 0u; channel < map_->asset->channelCount;
             ++channel) sum += lane(channel);
        return sum / map_->asset->channelCount;
    }

    float interpolate(std::size_t sourceIndex, const WavesetUnit& unit,
        double phase, float join) const noexcept
    {
        phase -= std::floor(phase);
        const double position = unit.startPosition
            + phase * unit.sampleLength();
        const float raw = sampleAt(sourceIndex, position);
        const float start = sampleAt(sourceIndex, unit.startPosition);
        const float end = sampleAt(sourceIndex, unit.endPosition);
        const float corrected = raw - (start
            + (end - start) * static_cast<float>(phase));
        return raw + (corrected - raw) * std::clamp(join, 0.0f, 1.0f);
    }

    std::size_t outputMapIndex(WavesetSourceMode mode, uint32_t outputLane,
        uint8_t outputChannelCount) const noexcept
    {
        if (mode == WavesetSourceMode::SumMono) return kSumUnitIndex;
        if (mode == WavesetSourceMode::Right)
            return map_->asset->channelCount > 1u ? 1u : 0u;
        if (mode == WavesetSourceMode::TrueChannels) {
            if (outputChannelCount == 1u) return kSumUnitIndex;
            return std::min<std::size_t>(outputLane,
                map_->asset->channelCount - 1u);
        }
        return 0u;
    }

    std::size_t strongestUnit(const Voice& voice, std::size_t mapIndex,
        const WavesetSettings& settings) const noexcept
    {
        const auto& units = unitsForIndex(mapIndex);
        std::size_t strongest = resolvedUnit(voice, mapIndex, 0u, settings);
        for (uint32_t offset = 1u; offset < settings.groupSize; ++offset) {
            const std::size_t candidate = resolvedUnit(voice, mapIndex,
                offset, settings);
            if (units[candidate].peak > units[strongest].peak)
                strongest = candidate;
        }
        return strongest;
    }

    std::size_t adjacentUnit(const Voice& voice, std::size_t mapIndex,
        const WavesetSettings& settings) const noexcept
    {
        const auto& units = unitsForIndex(mapIndex);
        const std::size_t current = resolvedUnit(voice, mapIndex,
            voice.cycleOffset, settings);
        return voice.progressionForward
            ? (current + 1u) % units.size()
            : (current + units.size() - 1u) % units.size();
    }

    float groupAverageSample(const Voice& voice, std::size_t mapIndex,
        const WavesetSettings& settings, double phase) const noexcept
    {
        const auto& units = unitsForIndex(mapIndex);
        double sum = 0.0;
        for (uint32_t offset = 0u; offset < settings.groupSize; ++offset)
            sum += interpolate(mapIndex, units[resolvedUnit(voice, mapIndex,
                offset, settings)], phase, settings.joinAmount);
        return static_cast<float>(sum / std::max(1u, settings.groupSize));
    }

    float telescopeSample(const Voice& voice, std::size_t mapIndex,
        const WavesetSettings& settings, double phase) const noexcept
    {
        const auto& units = unitsForIndex(mapIndex);
        double sum = 0.0;
        for (uint32_t offset = 0u; offset < settings.groupSize; ++offset)
            sum += interpolate(mapIndex, units[resolvedUnit(voice, mapIndex,
                offset, settings)], phase, settings.joinAmount);
        return static_cast<float>(std::tanh(sum
            / std::sqrt(static_cast<double>(
                std::max(1u, settings.groupSize)))));
    }

    float renderVoiceSample(const Voice& voice,
        const WavesetSettings& settings, uint32_t outputLane) const noexcept
    {
        const std::size_t mapIndex = outputMapIndex(settings.sourceMode,
            outputLane, voice.output.channelCount);
        const auto& units = unitsForIndex(mapIndex);
        const WavesetUnit& unit = units[resolvedUnit(voice, mapIndex,
            voice.cycleOffset, settings)];
        const float raw = interpolate(mapIndex, unit,
            voice.oscillatorPhase, settings.joinAmount);
        const float amount = std::clamp(settings.processAmount, 0.0f, 1.0f);
        const auto blend = [raw, amount](float wet) noexcept {
            return raw + (wet - raw) * amount;
        };
        switch (settings.shape) {
        case WavesetShape::Omit: {
            uint32_t hash = static_cast<uint32_t>(voice.baseUnits[mapIndex])
                * 747796405u + 2891336453u;
            hash = ((hash >> ((hash >> 28u) + 4u)) ^ hash) * 277803737u;
            hash = (hash >> 22u) ^ hash;
            return static_cast<float>(hash & 0xffffu) / 65535.0f < amount
                ? raw * (1.0f - amount) : raw;
        }
        case WavesetShape::Replace:
            return blend(interpolate(mapIndex,
                units[strongestUnit(voice, mapIndex, settings)],
                voice.oscillatorPhase, settings.joinAmount));
        case WavesetShape::Envelope: {
            const double sine = std::sin(3.14159265358979323846
                * voice.oscillatorPhase);
            return blend(raw * static_cast<float>(sine * sine));
        }
        case WavesetShape::Multiply:
            return blend(interpolate(mapIndex, unit,
                voice.oscillatorPhase * (2.0 + std::lround(amount * 6.0f)),
                settings.joinAmount));
        case WavesetShape::Average:
            return blend(groupAverageSample(voice, mapIndex, settings,
                voice.oscillatorPhase));
        case WavesetShape::Interpolate: {
            const double morph = (static_cast<double>(voice.repeatIndex)
                + voice.oscillatorPhase)
                / static_cast<double>(std::max(1u, settings.repeats));
            const float target = interpolate(mapIndex,
                units[adjacentUnit(voice, mapIndex, settings)],
                voice.oscillatorPhase, settings.joinAmount);
            return blend(raw + (target - raw)
                * static_cast<float>(std::clamp(morph, 0.0, 1.0)));
        }
        case WavesetShape::Fractal: {
            double sum = raw;
            double normalization = 1.0;
            for (uint32_t level = 1u; level <= 4u; ++level) {
                const double weight = std::ldexp(1.0, -static_cast<int>(level));
                sum += interpolate(mapIndex, unit,
                    voice.oscillatorPhase * static_cast<double>(1u << level),
                    settings.joinAmount) * weight;
                normalization += weight;
            }
            return blend(static_cast<float>(sum / normalization));
        }
        case WavesetShape::AdditiveHarmonic: {
            const uint32_t highest = 2u + static_cast<uint32_t>(
                std::lround(amount * 6.0f));
            double sum = raw;
            double normalization = 1.0;
            for (uint32_t harmonic = 2u; harmonic <= highest; ++harmonic) {
                const double weight = 1.0 / static_cast<double>(harmonic);
                sum += interpolate(mapIndex, unit,
                    voice.oscillatorPhase * harmonic,
                    settings.joinAmount) * weight;
                normalization += weight;
            }
            return blend(static_cast<float>(sum / normalization));
        }
        case WavesetShape::GroupReverse: {
            const uint32_t reverseOffset = settings.groupSize - 1u
                - (voice.cycleOffset % settings.groupSize);
            const auto& reversed = units[resolvedUnit(voice, mapIndex,
                reverseOffset, settings)];
            return blend(interpolate(mapIndex, reversed,
                1.0 - voice.oscillatorPhase, settings.joinAmount));
        }
        case WavesetShape::CycleReverse:
            return blend(interpolate(mapIndex, unit,
                1.0 - voice.oscillatorPhase, settings.joinAmount));
        case WavesetShape::Telescope:
            return blend(telescopeSample(voice, mapIndex, settings,
                voice.oscillatorPhase));
        case WavesetShape::Repeat:
        default: return raw;
        }
    }

    double oscillatorIncrement(const Voice& voice,
        const WavesetSettings& settings) const noexcept
    {
        const std::size_t mapIndex = referenceIndex(settings.sourceMode,
            voice.output.channelCount);
        const auto& units = unitsForIndex(mapIndex);
        const auto& unit = units[resolvedUnit(voice, mapIndex,
            voice.cycleOffset, settings)];
        double length = unit.sampleLength();
        const double amount = std::clamp<double>(settings.processAmount,
            0.0, 1.0);
        if (settings.shape == WavesetShape::Interpolate) {
            const double morph = (static_cast<double>(voice.repeatIndex)
                + voice.oscillatorPhase)
                / static_cast<double>(std::max(1u, settings.repeats));
            const double targetLength = units[adjacentUnit(voice, mapIndex,
                settings)].sampleLength();
            length += (targetLength - length)
                * std::clamp(morph * amount, 0.0, 1.0);
        } else if (settings.shape == WavesetShape::GroupReverse) {
            const uint32_t reverseOffset = settings.groupSize - 1u
                - (voice.cycleOffset % settings.groupSize);
            const double targetLength = units[resolvedUnit(voice, mapIndex,
                reverseOffset, settings)].sampleLength();
            length += (targetLength - length) * amount;
        } else if (settings.shape == WavesetShape::Telescope) {
            double longest = length;
            for (uint32_t offset = 0u; offset < settings.groupSize; ++offset)
                longest = std::max(longest, units[resolvedUnit(voice,
                    mapIndex, offset, settings)].sampleLength());
            length += (longest - length) * amount;
        }
        const double semitones = static_cast<double>(voice.key)
            - settings.rootNote + settings.tuneSemitones
            + settings.fineTuneCents * 0.01;
        const double ratio = std::pow(2.0, semitones / 12.0);
        return map_->asset->sampleRate / outputSampleRate_ * ratio
            / std::max(1.0, length);
    }

    double transportIncrement(const Voice& voice,
        const WavesetSettings& settings) const noexcept
    {
        if (settings.advance == WavesetAdvanceMode::Hold
            || map_->asset->frameCount() == 0u) return 0.0;
        const double semitones = static_cast<double>(voice.key)
            - settings.rootNote + settings.tuneSemitones
            + settings.fineTuneCents * 0.01;
        const double pitchRatio = std::pow(2.0, semitones / 12.0);
        double traversal = static_cast<double>(settings.stride);
        if (settings.advance == WavesetAdvanceMode::Stretch)
            traversal /= std::max(1u, settings.repeats);
        return map_->asset->sampleRate / outputSampleRate_
            * pitchRatio * traversal
            / static_cast<double>(map_->asset->frameCount());
    }

    void advanceTransport(Voice& voice,
        const WavesetSettings& settings) noexcept
    {
        const double increment = transportIncrement(voice, settings);
        if (!(increment > 0.0) || !std::isfinite(increment)) return;
        voice.transportPosition += voice.progressionForward
            ? increment : -increment;
        if (!isLooping(settings.playMode)) {
            voice.transportPosition = std::clamp(voice.transportPosition,
                settings.start, settings.end);
            return;
        }
        if (!voice.enteredLoop) {
            voice.transportPosition = std::clamp(voice.transportPosition,
                settings.start, settings.end);
            return;
        }
        const double low = settings.loopStart;
        const double high = settings.loopEnd;
        const double span = high - low;
        if (!(span > 1.0e-12)) {
            voice.transportPosition = low;
            return;
        }
        if (isPingPong(settings.playMode)) {
            voice.transportPosition = std::clamp(voice.transportPosition,
                low, high);
            return;
        }
        while (voice.transportPosition > high)
            voice.transportPosition = low
                + (voice.transportPosition - high);
        while (voice.transportPosition < low)
            voice.transportPosition = high
                - (low - voice.transportPosition);
    }

    std::pair<std::size_t, std::size_t> unitBounds(
        const std::vector<WavesetUnit>& units, double start, double end) const
        noexcept
    {
        const uint32_t frames = map_->asset->frameCount();
        std::size_t low = nearestUnit(units, start, frames);
        std::size_t high = nearestUnit(units, end, frames);
        if (low > high) std::swap(low, high);
        return { low, high };
    }

    void beginRelease(Voice& voice, const WavesetSettings& settings) noexcept
    {
        if (settings.releaseSeconds <= 0.0f) voice.active = false;
        else voice.releasing = true;
    }

    void moveVoice(Voice& voice, const WavesetSettings& settings) noexcept
    {
        if (settings.advance == WavesetAdvanceMode::Hold) return;
        const std::size_t ref = referenceIndex(settings.sourceMode,
            voice.output.channelCount);
        const auto& units = unitsForIndex(ref);
        const auto active = unitBounds(units, settings.start, settings.end);
        const auto loop = unitBounds(units, settings.loopStart,
            settings.loopEnd);
        const uint64_t step = static_cast<uint64_t>(settings.groupSize)
            * settings.stride
            * (settings.advance == WavesetAdvanceMode::Preserve
                ? settings.repeats : 1u);
        const int64_t direction = voice.progressionForward ? 1 : -1;
        int64_t next = static_cast<int64_t>(voice.baseUnits[ref])
            + direction * static_cast<int64_t>(step);
        if (!isLooping(settings.playMode)) {
            if (next >= static_cast<int64_t>(active.first)
                && next <= static_cast<int64_t>(active.second)) {
                setVoicePosition(voice,
                    units[static_cast<std::size_t>(next)].startPosition
                        / map_->asset->frameCount());
                return;
            }
            beginRelease(voice, settings);
            return;
        }

        // A looped voice first traverses the lead-in from Start (or End).
        // Only after it reaches the matching loop boundary is its position
        // folded into the loop region.
        if (!voice.enteredLoop) {
            const bool stillInLeadIn = voice.progressionForward
                ? next < static_cast<int64_t>(loop.first)
                : next > static_cast<int64_t>(loop.second);
            if (stillInLeadIn) {
                next = std::clamp<int64_t>(next,
                    static_cast<int64_t>(active.first),
                    static_cast<int64_t>(active.second));
                setVoicePosition(voice,
                    units[static_cast<std::size_t>(next)].startPosition
                        / map_->asset->frameCount());
                return;
            }
            voice.enteredLoop = true;
        }

        const int64_t low = static_cast<int64_t>(loop.first);
        const int64_t high = static_cast<int64_t>(loop.second);
        if (low == high) {
            next = low;
        } else if (isPingPong(settings.playMode)) {
            const bool priorForward = voice.progressionForward;
            const int64_t width = high - low;
            const int64_t period = width * 2;
            const int64_t folded = ((next - low) % period + period) % period;
            const bool foldRunsForward = folded <= width;
            next = foldRunsForward ? low + folded
                : high - (folded - width);
            // The fold derivative flips the incoming direction each time a
            // boundary is crossed, including strides wider than one loop.
            voice.progressionForward = priorForward == foldRunsForward;
        } else {
            const int64_t span = high - low + 1;
            next = low + ((next - low) % span + span) % span;
        }
        next = std::clamp<int64_t>(next, 0,
            static_cast<int64_t>(units.size() - 1u));
        setVoicePosition(voice, units[static_cast<std::size_t>(next)]
            .startPosition / map_->asset->frameCount());
    }

    void advanceVoice(Voice& voice, const WavesetSettings& settings) noexcept
    {
        voice.oscillatorPhase += oscillatorIncrement(voice, settings);
        while (voice.active && voice.oscillatorPhase >= 1.0) {
            voice.oscillatorPhase -= 1.0;
            uint32_t cycleAdvance = 1u;
            if (settings.shape == WavesetShape::Telescope)
                cycleAdvance += static_cast<uint32_t>(std::lround(
                    std::clamp(settings.processAmount, 0.0f, 1.0f)
                        * static_cast<float>(settings.groupSize - 1u)));
            voice.cycleOffset += cycleAdvance;
            if (voice.cycleOffset < settings.groupSize) continue;
            voice.cycleOffset = 0u;
            if (++voice.repeatIndex < settings.repeats) continue;
            voice.repeatIndex = 0u;
            if (settings.direction == WavesetDirection::Pendulum)
                voice.pendulumForward = !voice.pendulumForward;
            voice.randomState = voice.randomState * 1664525u + 1013904223u;
            moveVoice(voice, settings);
        }
    }

    void updateEnvelope(Voice& voice,
        const WavesetSettings& settings) noexcept
    {
        const double seconds = voice.releasing ? settings.releaseSeconds
            : settings.attackSeconds;
        if (seconds <= 0.0) {
            voice.envelope = voice.releasing ? 0.0f : 1.0f;
        } else {
            const float coefficient = static_cast<float>(1.0
                - std::exp(-1.0 / (seconds * outputSampleRate_)));
            const float target = voice.releasing ? 0.0f : 1.0f;
            voice.envelope += coefficient * (target - voice.envelope);
        }
        if (voice.releasing && voice.envelope < 1.0e-5f)
            voice.active = false;
    }

    bool eventMatches(const WavesetRenderEvent& event,
        const Voice& voice) const noexcept
    {
        return event.noteId != 0u ? event.noteId == voice.noteId
            : event.key == voice.key && event.midiChannel == voice.midiChannel;
    }

    Voice* newestVoice() noexcept
    {
        Voice* newest = nullptr;
        for (auto& voice : voices_)
            if (voice.active && (!newest || voice.age > newest->age))
                newest = &voice;
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

    void startVoice(const WavesetRenderEvent& event,
        const WavesetSettings& settings) noexcept
    {
        Voice* voice = voiceToStart();
        *voice = {};
        voice->active = true;
        voice->progressionForward = !isReverse(settings.playMode);
        voice->noteId = event.noteId;
        voice->key = event.key;
        voice->midiChannel = event.midiChannel;
        voice->age = ++ageCounter_;
        voice->output = outputAllocator_.next(std::min(outputChannelCount_,
            settings.activeOutputChannelCount),
            settings.outputRouting);
        voice->randomState ^= static_cast<uint32_t>(event.key) * 0x9e3779b9u;
        const float velocity = std::clamp(event.velocity, 0.0f, 1.0f);
        voice->velocityGain = 1.0f + (velocity - 1.0f)
            * settings.velocitySensitivity;
        setVoicePosition(*voice, voice->progressionForward
            ? settings.start : settings.end);
        voice->transportPosition = voice->progressionForward
            ? settings.start : settings.end;
    }

    void noteOn(const WavesetRenderEvent& event,
        const WavesetSettings& settings) noexcept
    {
        if (settings.triggerMode == TriggerMode::Toggle) {
            bool stopped = false;
            for (auto& voice : voices_) {
                if (voice.active && eventMatches(event, voice)) {
                    beginRelease(voice, settings);
                    stopped = true;
                }
            }
            if (stopped) return;
        }
        if (settings.voiceMode == VoiceMode::Legato) {
            if (Voice* voice = newestVoice()) {
                voice->key = event.key;
                voice->noteId = event.noteId;
                voice->midiChannel = event.midiChannel;
                voice->output = outputAllocator_.next(
                    std::min(outputChannelCount_,
                        settings.activeOutputChannelCount),
                    settings.outputRouting);
                voice->velocityGain = 1.0f
                    + (std::clamp(event.velocity, 0.0f, 1.0f) - 1.0f)
                        * settings.velocitySensitivity;
                voice->releasing = false;
                voice->age = ++ageCounter_;
                return;
            }
        }
        if (settings.voiceMode != VoiceMode::Poly)
            for (auto& voice : voices_) voice.active = false;
        startVoice(event, settings);
    }

    void noteOff(const WavesetRenderEvent& event,
        const WavesetSettings& settings) noexcept
    {
        if (settings.triggerMode == TriggerMode::OneShot
            || settings.triggerMode == TriggerMode::Toggle) return;
        const bool releases = settings.triggerMode == TriggerMode::Gate
            || (settings.triggerMode == TriggerMode::Auto
                && isLooping(settings.playMode));
        if (!releases) return;
        for (auto& voice : voices_)
            if (voice.active && eventMatches(event, voice))
                beginRelease(voice, settings);
    }

    void applyEvent(const WavesetRenderEvent& event,
        const WavesetSettings& settings) noexcept
    {
        switch (event.kind) {
        case WavesetEventKind::NoteOn: noteOn(event, settings); break;
        case WavesetEventKind::NoteOff: noteOff(event, settings); break;
        case WavesetEventKind::Preview: {
            WavesetRenderEvent preview = event;
            preview.kind = WavesetEventKind::NoteOn;
            preview.key = settings.rootNote;
            preview.velocity = 1.0f;
            preview.noteId = std::numeric_limits<uint64_t>::max();
            noteOn(preview, settings);
            break;
        }
        case WavesetEventKind::StopAll:
            for (auto& voice : voices_) voice.active = false;
            break;
        }
    }

    bool prepared_ = false;
    double outputSampleRate_ = 48000.0;
    uint32_t outputChannelCount_ = 2u;
    const WavesetMap* map_ = nullptr;
    std::array<Voice, kMaximumWavesetVoices> voices_ {};
    std::array<WavesetVoiceCursor, kMaximumWavesetVoices> voiceCursors_ {};
    uint32_t voiceCursorCount_ = 0u;
    uint64_t ageCounter_ = 0u;
    s3g::routing::TriggerOutputAllocator<
        kMaximumWavesetOutputChannels> outputAllocator_ {};
    float outputPeak_ = 0.0f;
};

} // namespace s3g::sample
