#pragma once

#include "s3g_sample_lanes.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace s3g::sample {

constexpr std::size_t kMaximumGrainEmitters = kMaximumLaneVoices;
constexpr std::size_t kMaximumActiveGrains = 128u;
constexpr std::size_t kMaximumPublishedGrainCursors = 32u;

struct SampleGrainCursor {
    float phase = 0.0f;
    float gain = 0.0f;
    float sourcePositionNormalized = 0.0f;
    float lanePositionNormalized = 0.0f;
    float pathClockPhase = 0.0f;
    std::array<float, kSampleLaneCount> laneSourcePositions {};
    std::array<float, kSampleLaneCount> laneSourceSpans {};
    std::array<float, kSampleLaneCount> laneWeights {};
    uint64_t identity = 0u;
};

enum class GrainSourceMode : uint8_t {
    Scan = 0u,
    Freeze,
    Cloud,
    Slice,
};

enum class GrainEnvelope : uint8_t {
    Parzen = 0u,
    Sine,
    Hann,
    Triangle,
    Gaussian,
};

enum class GrainTiming : uint8_t {
    Regular = 0u,
    Scatter,
};

enum class GrainPositionBias : uint8_t {
    Behind = 0u,
    Around,
    Ahead,
};

enum class GrainSourceAdvance : uint8_t {
    Scan = 0u,
    Grain,
};

enum class GrainMutate : uint8_t {
    Ordinary = 0u,
    Sorter,
    Stutter,
    Shrink,
    Doublets,
};

enum class GrainChannelMode : uint8_t {
    PreserveOrigins = 0u,
    MonoSum,
    Left,
    Right,
    Mid,
    Side,
};

enum class GrainStereoLink : uint8_t {
    Linked = 0u,
    Independent,
};

struct SampleGrainsSettings : SampleLanesSettings {
    GrainSourceMode grainSourceMode = GrainSourceMode::Scan;
    GrainEnvelope grainEnvelope = GrainEnvelope::Parzen;
    GrainTiming grainTiming = GrainTiming::Regular;
    GrainMutate grainMutate = GrainMutate::Ordinary;
    GrainPositionBias positionBias = GrainPositionBias::Around;
    GrainSourceAdvance sourceAdvance = GrainSourceAdvance::Scan;
    float grainDensityHz = 24.0f;
    float grainSizeMilliseconds = 90.0f;
    float sourcePosition = 0.0f;
    float positionSpray = 0.08f;
    float grainPitchSemitones = 0.0f;
    float pitchSpraySemitones = 0.0f;
    float reverseChance = 0.0f;
    float grainSizeVariation = 0.0f;
    float grainLevelVariation = 0.0f;
    float timingScatter = 1.0f;
    float envelopeSkew = 0.0f;
    float mutateAmount = 0.5f;
    uint32_t regionCount = 16u;
    bool sourceTimeSync = true;
    GrainChannelMode channelMode = GrainChannelMode::PreserveOrigins;
    GrainStereoLink stereoLink = GrainStereoLink::Linked;
    float monoSpread = 0.0f;

    bool valid() const noexcept
    {
        return SampleLanesSettings::valid()
            && static_cast<uint8_t>(grainSourceMode)
                <= static_cast<uint8_t>(GrainSourceMode::Slice)
            && static_cast<uint8_t>(grainEnvelope)
                <= static_cast<uint8_t>(GrainEnvelope::Gaussian)
            && static_cast<uint8_t>(grainTiming)
                <= static_cast<uint8_t>(GrainTiming::Scatter)
            && static_cast<uint8_t>(grainMutate)
                <= static_cast<uint8_t>(GrainMutate::Doublets)
            && static_cast<uint8_t>(positionBias)
                <= static_cast<uint8_t>(GrainPositionBias::Ahead)
            && static_cast<uint8_t>(sourceAdvance)
                <= static_cast<uint8_t>(GrainSourceAdvance::Grain)
            && std::isfinite(grainDensityHz) && grainDensityHz >= 0.1f
            && grainDensityHz <= 160.0f
            && std::isfinite(grainSizeMilliseconds)
            && grainSizeMilliseconds >= 8.0f
            && grainSizeMilliseconds <= 4000.0f
            && std::isfinite(sourcePosition) && sourcePosition >= 0.0f
            && sourcePosition <= 1.0f
            && std::isfinite(positionSpray) && positionSpray >= 0.0f
            && positionSpray <= 1.0f
            && std::isfinite(grainPitchSemitones)
            && grainPitchSemitones >= -48.0f
            && grainPitchSemitones <= 48.0f
            && std::isfinite(pitchSpraySemitones)
            && pitchSpraySemitones >= 0.0f
            && pitchSpraySemitones <= 24.0f
            && std::isfinite(reverseChance) && reverseChance >= 0.0f
            && reverseChance <= 1.0f
            && std::isfinite(grainSizeVariation)
            && grainSizeVariation >= 0.0f && grainSizeVariation <= 1.0f
            && std::isfinite(grainLevelVariation)
            && grainLevelVariation >= 0.0f && grainLevelVariation <= 1.0f
            && std::isfinite(timingScatter) && timingScatter >= 0.0f
            && timingScatter <= 1.0f
            && std::isfinite(envelopeSkew) && envelopeSkew >= -1.0f
            && envelopeSkew <= 1.0f
            && std::isfinite(mutateAmount) && mutateAmount >= 0.0f
            && mutateAmount <= 1.0f
            && regionCount >= 2u && regionCount <= 32u
            && static_cast<uint8_t>(channelMode)
                <= static_cast<uint8_t>(GrainChannelMode::Side)
            && static_cast<uint8_t>(stereoLink)
                <= static_cast<uint8_t>(GrainStereoLink::Independent)
            && std::isfinite(monoSpread) && monoSpread >= 0.0f
            && monoSpread <= 1.0f;
    }
};

class SampleGrainsEngine {
public:
    bool prepare(double outputSampleRate, uint32_t outputChannelCount) noexcept
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

    bool prepare(double outputSampleRate) noexcept
    { return prepare(outputSampleRate, 2u); }

    void reset() noexcept
    {
        emitters_ = {};
        grains_ = {};
        cursors_ = {};
        grainCursors_ = {};
        cursorCount_ = 0u;
        grainCursorCount_ = 0u;
        ageCounter_ = 0u;
        grainCounter_ = 0u;
        replacementIndex_ = 0u;
        randomState_ = 0x9e3779b9u;
        allocator_.reset();
        allocatorSeed_ = 0u;
        outputPeak_ = 0.0f;
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

    void setPreparedAsset(std::size_t lane, const SampleAsset* asset) noexcept
    { if (lane < assets_.size()) assets_[lane] = asset; }

    const std::array<const SampleAsset*, kSampleLaneCount>& assets()
        const noexcept { return assets_; }

    uint32_t activeVoiceCount() const noexcept
    {
        return static_cast<uint32_t>(std::count_if(emitters_.begin(),
            emitters_.end(), [](const Emitter& emitter) {
                return emitter.active;
            }));
    }

    uint32_t activeGrainCount() const noexcept
    {
        return static_cast<uint32_t>(std::count_if(grains_.begin(),
            grains_.end(), [](const Grain& grain) { return grain.active; }));
    }

    uint32_t voiceCursorCount() const noexcept { return cursorCount_; }
    uint32_t grainCursorCount() const noexcept { return grainCursorCount_; }
    float outputPeak() const noexcept { return outputPeak_; }
    const std::array<SampleLanesVoiceCursor, kMaximumLaneVoices>&
        voiceCursors() const noexcept { return cursors_; }
    const std::array<SampleGrainCursor, kMaximumPublishedGrainCursors>&
        grainCursors() const noexcept { return grainCursors_; }

    void render(const SampleGrainsSettings& settings,
        const LanesRenderEvent* events, std::size_t eventCount,
        float* left, float* right, uint32_t frameCount) noexcept
    {
        std::array<float*, 2u> outputs {{ left, right }};
        render(settings, events, eventCount, outputs.data(), 2u,
            frameCount);
    }

    void render(const SampleGrainsSettings& settings,
        const LanesRenderEvent* events, std::size_t eventCount,
        float* const* outputs, uint32_t outputChannelCount,
        uint32_t frameCount) noexcept
    {
        cursorCount_ = 0u;
        grainCursorCount_ = 0u;
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
            randomState_ = settings.seed != 0u ? settings.seed : 1u;
        }

        std::size_t eventIndex = 0u;
        for (uint32_t frame = 0u; frame < frameCount; ++frame) {
            while (eventIndex < eventCount
                && events[eventIndex].frameOffset <= frame) {
                applyEvent(events[eventIndex], settings);
                ++eventIndex;
            }
            for (auto& emitter : emitters_) {
                if (!emitter.active) continue;
                updateEmitterEnvelope(emitter, settings);
                if (!emitter.active) continue;
                if (emitter.pendingDoublet) {
                    emitter.pendingDoubletCountdown -= 1.0;
                    if (emitter.pendingDoubletCountdown <= 0.0) {
                        startGrain(emitter, settings,
                            emitter.pendingDoubletSource,
                            emitter.pendingDoubletPathClock, true);
                        emitter.pendingDoublet = false;
                    }
                }
                emitter.grainCountdown -= 1.0;
                if (emitter.grainCountdown <= 0.0) {
                    const double interval = nextInterval(settings);
                    startEvent(emitter, settings, interval);
                    emitter.grainCountdown += interval;
                }
                advanceEmitter(emitter, settings);
            }
            renderGrains(settings, outputs, outputChannelCount, frame);
        }
        while (eventIndex < eventCount
            && events[eventIndex].frameOffset <= frameCount) {
            applyEvent(events[eventIndex], settings);
            ++eventIndex;
        }
        publishCursors(settings);
        publishGrainCursors(settings);
    }

private:
    struct Emitter {
        bool active = false;
        bool releasing = false;
        uint64_t noteId = 0u;
        uint64_t age = 0u;
        uint8_t key = 60u;
        uint8_t midiChannel = 0u;
        float velocityGain = 1.0f;
        float envelope = 0.0f;
        float releaseDecrement = 0.0f;
        double scanPhase = 0.0;
        double grainCountdown = 0.0;
        double lastSourcePosition = 0.0;
        double lastLanePosition = 0.0;
        double lastPathPhase = 0.0;
        double stutterAnchor = 0.0;
        double pendingDoubletSource = 0.0;
        double pendingDoubletPathClock = 0.0;
        double pendingDoubletCountdown = 0.0;
        double oneShotDistance = 0.0;
        uint32_t patternIndex = 0u;
        uint32_t repeatIndex = 0u;
        bool directionForward = true;
        bool pendingDoublet = false;
    };

    struct Grain {
        bool active = false;
        uint32_t age = 0u;
        uint32_t duration = 0u;
        double sourcePosition = 0.0;
        double sourceIncrement = 1.0;
        double rightSourcePosition = 0.0;
        double rightSourceIncrement = 1.0;
        double pathClockPhase = 0.0;
        std::array<float, kSampleLaneCount> laneWeights {};
        std::array<double, kSampleLaneCount> lanePositions {};
        std::array<double, kSampleLaneCount> rightLanePositions {};
        float monoPan = 0.0f;
        float gain = 1.0f;
        uint64_t identity = 0u;
        s3g::routing::VoiceOutputAssignment outputAssignment {};
    };

    static double wrap(double value) noexcept
    {
        value -= std::floor(value);
        return value < 0.0 ? value + 1.0 : value;
    }

    static float dbToGain(float value) noexcept
    { return std::pow(10.0f, value / 20.0f); }

    uint32_t nextRandom() noexcept
    {
        uint32_t value = randomState_;
        value ^= value << 13u;
        value ^= value >> 17u;
        value ^= value << 5u;
        randomState_ = value != 0u ? value : 0x9e3779b9u;
        return randomState_;
    }

    float randomUnit() noexcept
    {
        return static_cast<float>((nextRandom() >> 8u) & 0x00ffffffu)
            / static_cast<float>(0x01000000u);
    }

    float randomBipolar() noexcept { return randomUnit() * 2.0f - 1.0f; }

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

    static float interpolateWrap(const SampleAsset& asset, uint8_t channel,
        double normalized, double start, double end) noexcept
    {
        if (channel >= asset.channelCount || asset.frameCount() < 2u)
            return 0.0f;
        const auto& samples = asset.channels[channel];
        const double lower = start * static_cast<double>(samples.size() - 1u);
        const double upper = end * static_cast<double>(samples.size() - 1u);
        const double width = std::max(1.0, upper - lower);
        double position = lower + wrap(normalized) * width;
        while (position < lower) position += width;
        while (position >= upper) position -= width;
        const auto first = static_cast<std::size_t>(std::floor(position));
        std::size_t second = first + 1u;
        if (second >= samples.size()
            || static_cast<double>(second) >= upper)
            second = static_cast<std::size_t>(std::floor(lower));
        const float fraction = static_cast<float>(position - first);
        return samples[first] + (samples[second] - samples[first]) * fraction;
    }

    static float monoSourceSample(const SampleAsset& asset,
        GrainChannelMode mode, double normalized, double start,
        double end) noexcept
    {
        const auto read = [&](uint8_t channel) noexcept {
            return interpolateWrap(asset,
                std::min<uint8_t>(channel, asset.channelCount - 1u),
                normalized, start, end);
        };
        const float left = read(0u);
        if (asset.channelCount == 1u)
            return mode == GrainChannelMode::Side ? 0.0f : left;
        const float right = read(1u);
        constexpr float inverseSqrtTwo = 0.70710678118654752440f;
        switch (mode) {
        case GrainChannelMode::Left: return left;
        case GrainChannelMode::Right: return right;
        case GrainChannelMode::Mid: return (left + right) * inverseSqrtTwo;
        case GrainChannelMode::Side: return (left - right) * inverseSqrtTwo;
        case GrainChannelMode::MonoSum:
        case GrainChannelMode::PreserveOrigins:
        default: return 0.5f * (left + right);
        }
    }

    static float window(GrainEnvelope envelope, float phase,
        float skew) noexcept
    {
        constexpr float pi = 3.14159265358979323846f;
        phase = std::clamp(phase, 0.0f, 1.0f);
        const float peak = 0.5f + 0.4f * std::clamp(skew, -1.0f, 1.0f);
        phase = phase <= peak
            ? 0.5f * phase / peak
            : 0.5f + 0.5f * (phase - peak) / (1.0f - peak);
        switch (envelope) {
        case GrainEnvelope::Sine:
            return std::sin(pi * phase);
        case GrainEnvelope::Hann:
            return 0.5f - 0.5f * std::cos(2.0f * pi * phase);
        case GrainEnvelope::Triangle:
            return 1.0f - std::abs(2.0f * phase - 1.0f);
        case GrainEnvelope::Gaussian: {
            const float value = (phase - 0.5f) / 0.18f;
            return std::exp(-0.5f * value * value);
        }
        case GrainEnvelope::Parzen:
        default: {
            const float triangle = 1.0f - std::abs(2.0f * phase - 1.0f);
            return triangle * triangle * (3.0f - 2.0f * triangle);
        }
        }
    }

    double positionOffset(const SampleGrainsSettings& settings) noexcept
    {
        switch (settings.positionBias) {
        case GrainPositionBias::Behind:
            return -static_cast<double>(randomUnit())
                * settings.positionSpray;
        case GrainPositionBias::Ahead:
            return static_cast<double>(randomUnit())
                * settings.positionSpray;
        case GrainPositionBias::Around:
        default:
            return static_cast<double>(randomBipolar())
                * settings.positionSpray;
        }
    }

    static double pathClockForEvent(const Emitter& emitter,
        const SampleGrainsSettings& settings) noexcept
    {
        if (settings.sourceAdvance == GrainSourceAdvance::Grain)
            return wrap((static_cast<double>(emitter.patternIndex) + 0.5)
                / 8.0);
        return std::clamp(emitter.scanPhase, 0.0, 1.0);
    }

    static double pathPhaseFromClock(double pathClock,
        const SampleGrainsSettings& settings) noexcept
    {
        return settings.path == LanePath::Manual
            ? std::clamp(pathClock, 0.0, 1.0)
            : wrap(pathClock * settings.pathCycles + settings.pathOffset);
    }

    static double grainLanePathUnit(double phase,
        const SampleGrainsSettings& settings) noexcept
    {
        if (settings.path == LanePath::Manual
            && settings.manualPathPointCount < 2u) return 0.5;
        return sampleLanePathUnit(phase, settings);
    }

    std::array<float, kSampleLaneCount> laneWeights(double lane,
        LaneBlend blend) const noexcept
    {
        std::array<float, kSampleLaneCount> result {};
        std::array<uint8_t, kSampleLaneCount> loaded {};
        std::size_t count = 0u;
        for (uint8_t index = 0u; index < assets_.size(); ++index)
            if (assets_[index]) loaded[count++] = index;
        if (count == 0u) return result;
        if (blend == LaneBlend::Jump || count == 1u) {
            uint8_t closest = loaded[0u];
            double distance = std::abs(lane - closest);
            for (std::size_t index = 1u; index < count; ++index) {
                const double candidate = std::abs(lane - loaded[index]);
                if (candidate < distance) {
                    closest = loaded[index];
                    distance = candidate;
                }
            }
            result[closest] = 1.0f;
            return result;
        }
        uint8_t lower = loaded[0u];
        uint8_t upper = loaded[count - 1u];
        for (std::size_t index = 0u; index < count; ++index) {
            if (loaded[index] <= lane) lower = loaded[index];
            if (loaded[index] >= lane) { upper = loaded[index]; break; }
        }
        if (lower == upper) result[lower] = 1.0f;
        else {
            const float mix = static_cast<float>((lane - lower)
                / static_cast<double>(upper - lower));
            constexpr float halfPi = 1.57079632679489661923f;
            result[lower] = std::cos(std::clamp(mix, 0.0f, 1.0f) * halfPi);
            result[upper] = std::sin(std::clamp(mix, 0.0f, 1.0f) * halfPi);
        }
        return result;
    }

    double sourceForEvent(Emitter& emitter,
        const SampleGrainsSettings& settings, double pathClock) noexcept
    {
        double source = emitter.scanPhase;
        switch (settings.grainSourceMode) {
        case GrainSourceMode::Freeze:
            source = settings.sourcePosition;
            break;
        case GrainSourceMode::Cloud:
            source = randomUnit();
            break;
        case GrainSourceMode::Slice:
            source = std::floor(emitter.scanPhase * settings.regionCount)
                / static_cast<double>(settings.regionCount);
            break;
        case GrainSourceMode::Scan:
        default:
            source = emitter.scanPhase;
            break;
        }
        source = wrap(source + positionOffset(settings));

        const uint32_t repeats = 1u + static_cast<uint32_t>(std::lround(
            settings.mutateAmount * 7.0f));
        switch (settings.grainMutate) {
        case GrainMutate::Sorter:
            source = sorterPosition(source,
                settings.sourceAdvance == GrainSourceAdvance::Grain
                    ? pathClock : source,
                settings);
            break;
        case GrainMutate::Stutter:
            if (emitter.repeatIndex == 0u) emitter.stutterAnchor = source;
            source = emitter.stutterAnchor;
            emitter.repeatIndex = (emitter.repeatIndex + 1u) % repeats;
            break;
        case GrainMutate::Shrink:
        case GrainMutate::Doublets:
        case GrainMutate::Ordinary:
        default:
            break;
        }
        return source;
    }

    double sorterPosition(double original, double pathClock,
        const SampleGrainsSettings& settings) const noexcept
    {
        const uint32_t count = settings.regionCount;
        const auto* asset = dominantAssetForPath(pathClock, settings);
        if (!asset || asset->frameCount() < count) return original;
        std::array<std::pair<float, uint8_t>, 32u> ranking {};
        for (uint32_t region = 0u; region < count; ++region) {
            float energy = 0.0f;
            for (uint32_t probe = 0u; probe < 8u; ++probe) {
                const double phase = (static_cast<double>(region)
                    + (static_cast<double>(probe) + 0.5) / 8.0)
                    / static_cast<double>(count);
                for (uint8_t channel = 0u; channel < asset->channelCount;
                     ++channel) {
                    const float sample = interpolateWrap(*asset, channel,
                        phase, settings.start, settings.end);
                    energy += sample * sample;
                }
            }
            ranking[region] = { energy, static_cast<uint8_t>(region) };
        }
        std::sort(ranking.begin(), ranking.begin() + count,
            [](const auto& left, const auto& right) {
                if (left.first == right.first)
                    return left.second < right.second;
                return left.first < right.first;
            });
        const uint32_t sequence = grainCounter_ % count;
        const double sorted = (static_cast<double>(ranking[sequence].second)
            + 0.5) / static_cast<double>(count);
        return wrap(original + (sorted - original) * settings.mutateAmount);
    }

    const SampleAsset* dominantAssetForPath(double pathClock,
        const SampleGrainsSettings& settings) const noexcept
    {
        const double pathPhase = pathPhaseFromClock(pathClock, settings);
        const double lane = 3.0 * grainLanePathUnit(pathPhase, settings);
        const auto weights = laneWeights(lane, settings.blend);
        std::size_t best = 0u;
        for (std::size_t index = 1u; index < weights.size(); ++index)
            if (weights[index] > weights[best]) best = index;
        return assets_[best];
    }

    void startEvent(Emitter& emitter,
        const SampleGrainsSettings& settings, double interval) noexcept
    {
        const double pathClock = pathClockForEvent(emitter, settings);
        const double source = sourceForEvent(emitter, settings, pathClock);
        startGrain(emitter, settings, source, pathClock, false);
        if (settings.grainMutate == GrainMutate::Doublets
            && (settings.mutateAmount >= 1.0f
                || (settings.mutateAmount > 0.0f
                    && randomUnit() < settings.mutateAmount))) {
            const double spacing = std::max(1.0, interval * 0.5);
            double second = source;
            if (settings.sourceTimeSync) {
                const auto* reference = referenceAsset();
                if (reference && reference->frameCount() > 0u)
                    second = wrap(source + spacing * reference->sampleRate
                        / outputSampleRate_
                        / static_cast<double>(reference->frameCount()));
            }
            emitter.pendingDoublet = true;
            emitter.pendingDoubletSource = second;
            emitter.pendingDoubletPathClock = pathClock;
            emitter.pendingDoubletCountdown = spacing;
        }
        ++emitter.patternIndex;
        ++grainCounter_;
    }

    void startGrain(Emitter& emitter, const SampleGrainsSettings& settings,
        double source, double pathClock, bool doublet) noexcept
    {
        Grain* slot = nullptr;
        for (auto& grain : grains_) if (!grain.active) {
            slot = &grain;
            break;
        }
        if (!slot) slot = &grains_[replacementIndex_++ % grains_.size()];

        const double pathPhase = pathPhaseFromClock(pathClock, settings);
        const double lane = 3.0 * grainLanePathUnit(pathPhase, settings);
        const float pitchSpray = randomBipolar()
            * settings.pitchSpraySemitones;
        double rate = std::pow(2.0,
            (settings.grainPitchSemitones + pitchSpray) / 12.0);
        if (randomUnit() < settings.reverseChance) rate = -rate;
        double rightSource = source;
        double rightRate = rate;
        if (outputChannelCount_ == 2u
            && settings.stereoLink == GrainStereoLink::Independent) {
            rightSource = wrap(source + positionOffset(settings));
            const float rightPitchSpray = randomBipolar()
                * settings.pitchSpraySemitones;
            rightRate = std::pow(2.0,
                (settings.grainPitchSemitones + rightPitchSpray) / 12.0);
            if (randomUnit() < settings.reverseChance) rightRate = -rightRate;
        }
        float size = settings.grainSizeMilliseconds;
        if (settings.grainSizeVariation > 0.0f)
            size *= std::max(0.0f, 1.0f
                + randomBipolar() * settings.grainSizeVariation);
        const float compensationSize = size;
        if (settings.grainMutate == GrainMutate::Shrink) {
            const uint32_t repeats = 1u + static_cast<uint32_t>(std::lround(
                settings.mutateAmount * 7.0f));
            const uint32_t step = emitter.repeatIndex++ % repeats;
            const double factor = std::pow(
                1.0 - 0.65 * settings.mutateAmount,
                static_cast<double>(step));
            size = static_cast<float>(std::max(8.0,
                static_cast<double>(size) * factor));
            rate /= std::max(0.1, factor);
            rightRate /= std::max(0.1, factor);
        }
        if (doublet) size *= 0.85f;

        *slot = {};
        slot->active = true;
        slot->duration = std::max<uint32_t>(8u,
            static_cast<uint32_t>(std::lround(size * 0.001
                * outputSampleRate_)));
        slot->sourcePosition = source;
        slot->sourceIncrement = rate;
        slot->rightSourcePosition = rightSource;
        slot->rightSourceIncrement = rightRate;
        slot->pathClockPhase = pathClock;
        slot->monoPan = std::clamp(settings.pan
            + randomBipolar() * settings.monoSpread, -1.0f, 1.0f);
        slot->laneWeights = laneWeights(lane, settings.blend);
        for (std::size_t index = 0u; index < slot->lanePositions.size();
             ++index) {
            slot->lanePositions[index] = source;
            slot->rightLanePositions[index] = rightSource;
        }
        const float overlap = settings.grainDensityHz
            * compensationSize * 0.001f;
        const float levelVariation = settings.grainLevelVariation > 0.0f
            ? 1.0f - settings.grainLevelVariation * randomUnit() : 1.0f;
        slot->gain = emitter.envelope * emitter.velocityGain
            * levelVariation * dbToGain(settings.outputGainDecibels)
            / std::sqrt(std::max(1.0f, overlap));
        slot->identity = ++ageCounter_;
        slot->outputAssignment = allocator_.next(
            settings.activeOutputChannels, settings.outputRouting);
        emitter.lastSourcePosition = source;
        emitter.lastLanePosition = lane;
        emitter.lastPathPhase = pathClock;
    }

    double nextInterval(const SampleGrainsSettings& settings) noexcept
    {
        const double mean = outputSampleRate_
            / static_cast<double>(settings.grainDensityHz);
        if (settings.grainTiming == GrainTiming::Regular
            || settings.timingScatter <= 0.0f) return mean;
        const double scattered = 0.2 + 1.8 * randomUnit();
        const double factor = 1.0 + settings.timingScatter
            * (scattered - 1.0);
        return std::max(1.0, mean * factor);
    }

    void renderGrains(const SampleGrainsSettings& settings,
        float* const* outputs, uint32_t outputChannelCount,
        uint32_t frame) noexcept
    {
        constexpr float halfPi = 1.57079632679489661923f;
        const float leftPan = std::cos((settings.pan + 1.0f) * 0.5f * halfPi);
        const float rightPan = std::sin((settings.pan + 1.0f) * 0.5f * halfPi);
        for (auto& grain : grains_) {
            if (!grain.active) continue;
            if (grain.age >= grain.duration) {
                grain.active = false;
                continue;
            }
            const float phase = static_cast<float>(grain.age)
                / static_cast<float>(std::max(1u, grain.duration - 1u));
            const float gain = grain.gain * window(settings.grainEnvelope,
                phase, settings.envelopeSkew);
            if (outputChannelCount == 2u) {
                float stereoLeft = 0.0f;
                float stereoRight = 0.0f;
                float mono = 0.0f;
                for (std::size_t lane = 0u; lane < assets_.size(); ++lane) {
                    const auto* asset = assets_[lane];
                    if (!asset || !(grain.laneWeights[lane] > 0.0f))
                        continue;
                    const double leftDelta = static_cast<double>(grain.age)
                        * grain.sourceIncrement * asset->sampleRate
                        / outputSampleRate_
                        / static_cast<double>(asset->frameCount());
                    const double rightDelta = static_cast<double>(grain.age)
                        * grain.rightSourceIncrement * asset->sampleRate
                        / outputSampleRate_
                        / static_cast<double>(asset->frameCount());
                    const float weight = grain.laneWeights[lane];
                    if (settings.channelMode
                            == GrainChannelMode::PreserveOrigins
                        && asset->channelCount > 1u) {
                        stereoLeft += weight * interpolateWrap(*asset, 0u,
                            grain.lanePositions[lane] + leftDelta,
                            settings.start, settings.end);
                        stereoRight += weight * interpolateWrap(*asset, 1u,
                            grain.rightLanePositions[lane] + rightDelta,
                            settings.start, settings.end);
                    } else {
                        mono += weight * monoSourceSample(*asset,
                            settings.channelMode,
                            grain.lanePositions[lane] + leftDelta,
                            settings.start, settings.end);
                    }
                }
                const float monoLeft = std::cos((grain.monoPan + 1.0f)
                    * 0.5f * halfPi);
                const float monoRight = std::sin((grain.monoPan + 1.0f)
                    * 0.5f * halfPi);
                outputs[0u][frame] += gain
                    * (stereoLeft * leftPan + mono * monoLeft);
                outputs[1u][frame] += gain
                    * (stereoRight * rightPan + mono * monoRight);
                outputPeak_ = std::max(outputPeak_, std::max(
                    std::abs(outputs[0u][frame]),
                    std::abs(outputs[1u][frame])));
                ++grain.age;
                if (grain.age >= grain.duration) grain.active = false;
                continue;
            }
            if (settings.outputMode == LaneOutputMode::Preserve) {
                for (uint32_t channel = 0u; channel < outputChannelCount;
                     ++channel) {
                    float sample = 0.0f;
                    for (std::size_t lane = 0u; lane < assets_.size(); ++lane) {
                        const auto* asset = assets_[lane];
                        if (!asset || !(grain.laneWeights[lane] > 0.0f))
                            continue;
                        const uint8_t sourceChannel = asset->channelCount == 1u
                                && outputChannelCount == 2u
                            ? 0u : static_cast<uint8_t>(channel);
                        if (sourceChannel >= asset->channelCount) continue;
                        const double delta = static_cast<double>(grain.age)
                            * grain.sourceIncrement * asset->sampleRate
                            / outputSampleRate_
                            / static_cast<double>(asset->frameCount());
                        sample += grain.laneWeights[lane] * interpolateWrap(
                            *asset, sourceChannel,
                            grain.lanePositions[lane] + delta,
                            settings.start, settings.end);
                    }
                    float channelGain = gain;
                    if (outputChannelCount == 2u)
                        channelGain *= channel == 0u ? leftPan : rightPan;
                    outputs[channel][frame] += sample * channelGain;
                    outputPeak_ = std::max(outputPeak_,
                        std::abs(outputs[channel][frame]));
                }
            } else {
                float left = 0.0f;
                float right = 0.0f;
                const bool stereo = grain.outputAssignment.channelCount > 1u;
                for (std::size_t lane = 0u; lane < assets_.size(); ++lane) {
                    const auto* asset = assets_[lane];
                    if (!asset || !(grain.laneWeights[lane] > 0.0f)) continue;
                    const double delta = static_cast<double>(grain.age)
                        * grain.sourceIncrement * asset->sampleRate
                        / outputSampleRate_
                        / static_cast<double>(asset->frameCount());
                    float laneLeft = 0.0f;
                    float laneRight = 0.0f;
                    if (!stereo || asset->channelCount == 1u) {
                        for (uint8_t channel = 0u; channel < asset->channelCount;
                             ++channel)
                            laneLeft += interpolateWrap(*asset, channel,
                                grain.lanePositions[lane] + delta,
                                settings.start, settings.end);
                        laneLeft /= static_cast<float>(asset->channelCount);
                        laneRight = laneLeft;
                    } else if (asset->channelCount == 2u) {
                        laneLeft = interpolateWrap(*asset, 0u,
                            grain.lanePositions[lane] + delta,
                            settings.start, settings.end);
                        laneRight = interpolateWrap(*asset, 1u,
                            grain.lanePositions[lane] + delta,
                            settings.start, settings.end);
                    } else {
                        uint32_t leftCount = 0u;
                        uint32_t rightCount = 0u;
                        for (uint8_t channel = 0u; channel < asset->channelCount;
                             ++channel) {
                            float& destination = channel % 2u == 0u
                                ? laneLeft : laneRight;
                            destination += interpolateWrap(*asset, channel,
                                grain.lanePositions[lane] + delta,
                                settings.start, settings.end);
                            channel % 2u == 0u ? ++leftCount : ++rightCount;
                        }
                        laneLeft /= static_cast<float>(std::max(1u, leftCount));
                        laneRight /= static_cast<float>(std::max(1u, rightCount));
                    }
                    left += grain.laneWeights[lane] * laneLeft;
                    right += grain.laneWeights[lane] * laneRight;
                }
                if (grain.outputAssignment.firstChannel < outputChannelCount) {
                    const uint32_t channel = grain.outputAssignment.firstChannel;
                    outputs[channel][frame] += left * gain * leftPan;
                    outputPeak_ = std::max(outputPeak_,
                        std::abs(outputs[channel][frame]));
                }
                if (grain.outputAssignment.channelCount > 1u
                    && grain.outputAssignment.secondChannel
                        < outputChannelCount) {
                    const uint32_t channel = grain.outputAssignment.secondChannel;
                    outputs[channel][frame] += right * gain * rightPan;
                    outputPeak_ = std::max(outputPeak_,
                        std::abs(outputs[channel][frame]));
                }
            }
            ++grain.age;
            if (grain.age >= grain.duration) grain.active = false;
        }
    }

    void applyEvent(const LanesRenderEvent& event,
        const SampleGrainsSettings& settings) noexcept
    {
        if ((event.kind == LaneEventKind::NoteOn
                || event.kind == LaneEventKind::Preview)
            && event.velocity > 0.0f)
            startEmitter(event, settings);
        else if (event.kind == LaneEventKind::StopAll) {
            for (auto& emitter : emitters_) emitter.active = false;
            for (auto& grain : grains_) grain.active = false;
        } else stopEmitter(event, settings);
    }

    void startEmitter(const LanesRenderEvent& event,
        const SampleGrainsSettings& settings) noexcept
    {
        const auto matches = [&event](const Emitter& emitter) {
            return emitter.active && (event.noteId != 0u
                ? emitter.noteId == event.noteId
                : emitter.key == event.key
                    && emitter.midiChannel == event.midiChannel);
        };
        if (settings.triggerMode == TriggerMode::Toggle) {
            bool stopped = false;
            for (auto& emitter : emitters_) if (matches(emitter)) {
                beginEmitterRelease(emitter, settings);
                stopped = true;
            }
            if (stopped) return;
        }
        Emitter* legato = nullptr;
        if (settings.voiceMode == VoiceMode::Legato) {
            for (auto& emitter : emitters_)
                if (emitter.active && (!legato || emitter.age > legato->age))
                    legato = &emitter;
        }
        if (settings.voiceMode == VoiceMode::Mono)
            for (auto& emitter : emitters_) emitter.active = false;
        Emitter* slot = legato;
        for (auto& emitter : emitters_) if (!emitter.active) {
            if (!slot) slot = &emitter;
            break;
        }
        if (!slot) slot = &*std::min_element(emitters_.begin(), emitters_.end(),
            [](const Emitter& left, const Emitter& right) {
                return left.age < right.age;
            });
        const double retainedPhase = slot->scanPhase;
        const float retainedEnvelope = slot->envelope;
        *slot = {};
        slot->active = true;
        slot->noteId = event.noteId;
        slot->key = event.key;
        slot->midiChannel = event.midiChannel;
        slot->age = ++ageCounter_;
        slot->velocityGain = 1.0f + (event.velocity - 1.0f)
            * settings.velocitySensitivity;
        slot->scanPhase = legato ? retainedPhase : settings.sourcePosition;
        slot->grainCountdown = 0.0;
        slot->envelope = legato ? retainedEnvelope
            : settings.attackSeconds <= 0.0f ? 1.0f : 0.0f;
        slot->directionForward = settings.transport != LaneTransport::Reverse;
    }

    void stopEmitter(const LanesRenderEvent& event,
        const SampleGrainsSettings& settings) noexcept
    {
        for (auto& emitter : emitters_) {
            if (!emitter.active) continue;
            const bool matches = event.noteId != 0u
                ? emitter.noteId == event.noteId
                : emitter.key == event.key
                    && emitter.midiChannel == event.midiChannel;
            if (!matches) continue;
            if (settings.triggerMode == TriggerMode::Auto
                || settings.triggerMode == TriggerMode::OneShot
                || settings.triggerMode == TriggerMode::Toggle) continue;
            beginEmitterRelease(emitter, settings);
        }
    }

    void beginEmitterRelease(Emitter& emitter,
        const SampleGrainsSettings& settings) noexcept
    {
        if (emitter.releasing) return;
        if (settings.releaseSeconds <= 0.0f) emitter.active = false;
        else {
            emitter.releasing = true;
            emitter.releaseDecrement = static_cast<float>(1.0
                / std::max(1.0,
                    settings.releaseSeconds * outputSampleRate_));
        }
    }

    void updateEmitterEnvelope(Emitter& emitter,
        const SampleGrainsSettings& settings) noexcept
    {
        if (emitter.releasing) {
            emitter.envelope -= emitter.releaseDecrement;
            if (!(emitter.envelope > 0.0f)) emitter.active = false;
        } else if (emitter.envelope < 1.0f) {
            emitter.envelope = std::min(1.0f, emitter.envelope
                + 1.0f / static_cast<float>(std::max(1.0,
                    settings.attackSeconds * outputSampleRate_)));
        }
    }

    void advanceEmitter(Emitter& emitter,
        const SampleGrainsSettings& settings) noexcept
    {
        const auto* reference = referenceAsset();
        if (!reference) return;
        double speed = settings.rateBasis == LaneRateBasis::Hertz
            ? settings.rate / outputSampleRate_
            : settings.rate * reference->sampleRate
                / outputSampleRate_
                / static_cast<double>(reference->frameCount())
                / (settings.end - settings.start);
        const double rateSemitones = static_cast<double>(emitter.key)
            - settings.rootNote + settings.tuneSemitones
            + settings.fineTuneCents * 0.01;
        speed *= std::pow(2.0, rateSemitones / 12.0);
        if (settings.transport == LaneTransport::PingPong) {
            emitter.scanPhase += emitter.directionForward ? speed : -speed;
            while (emitter.scanPhase > 1.0 || emitter.scanPhase < 0.0) {
                if (emitter.scanPhase > 1.0) {
                    emitter.scanPhase = 2.0 - emitter.scanPhase;
                    emitter.directionForward = false;
                } else {
                    emitter.scanPhase = -emitter.scanPhase;
                    emitter.directionForward = true;
                }
            }
        } else {
            if (settings.transport == LaneTransport::Reverse) speed = -speed;
            emitter.scanPhase = wrap(emitter.scanPhase + speed);
        }
        if (!emitter.releasing
            && (settings.triggerMode == TriggerMode::Auto
                || settings.triggerMode == TriggerMode::OneShot)) {
            emitter.oneShotDistance += std::abs(speed);
            if (emitter.oneShotDistance >= 1.0)
                beginEmitterRelease(emitter, settings);
        }
    }

    void publishCursors(const SampleGrainsSettings& settings) noexcept
    {
        for (const auto& emitter : emitters_) {
            if (!emitter.active || cursorCount_ >= cursors_.size()) continue;
            auto& cursor = cursors_[cursorCount_++];
            cursor = {};
            cursor.sourcePositionNormalized = static_cast<float>(
                emitter.lastSourcePosition);
            cursor.lanePositionNormalized = static_cast<float>(
                emitter.lastLanePosition / 3.0);
            cursor.pathPhase = static_cast<float>(emitter.lastPathPhase);
            cursor.key = emitter.key;
            cursor.identity = emitter.noteId;
            const auto weights = laneWeights(emitter.lastLanePosition,
                settings.blend);
            cursor.laneWeights = weights;
            for (std::size_t lane = 0u; lane < weights.size(); ++lane)
                cursor.laneSourcePositions[lane] = assets_[lane]
                    ? static_cast<float>(wrap(emitter.lastSourcePosition))
                    : -1.0f;
        }
    }

    void publishGrainCursors(const SampleGrainsSettings& settings) noexcept
    {
        const double loopSpan = settings.end - settings.start;
        for (const auto& grain : grains_) {
            if (!grain.active || grain.age >= grain.duration
                || grainCursorCount_ >= grainCursors_.size()) continue;
            auto& cursor = grainCursors_[grainCursorCount_++];
            cursor = {};
            cursor.phase = static_cast<float>(grain.age)
                / static_cast<float>(std::max(1u, grain.duration - 1u));
            cursor.gain = std::abs(grain.gain);
            cursor.sourcePositionNormalized = static_cast<float>(
                wrap(grain.sourcePosition));
            cursor.pathClockPhase = static_cast<float>(grain.pathClockPhase);
            cursor.identity = grain.identity;
            cursor.laneWeights = grain.laneWeights;
            float weightSum = 0.0f;
            float weightedLane = 0.0f;
            for (std::size_t lane = 0u; lane < assets_.size(); ++lane) {
                const float weight = grain.laneWeights[lane];
                weightSum += weight;
                weightedLane += weight * static_cast<float>(lane);
                const auto* asset = assets_[lane];
                if (!asset) {
                    cursor.laneSourcePositions[lane] = -1.0f;
                    continue;
                }
                const double sourcePhase = wrap(grain.lanePositions[lane]);
                cursor.laneSourcePositions[lane] = static_cast<float>(
                    settings.start + sourcePhase * loopSpan);
                cursor.laneSourceSpans[lane] = static_cast<float>(
                    static_cast<double>(grain.duration)
                    * grain.sourceIncrement * asset->sampleRate
                    / outputSampleRate_
                    / static_cast<double>(asset->frameCount()) * loopSpan);
            }
            cursor.lanePositionNormalized = weightSum > 0.0f
                ? weightedLane / (3.0f * weightSum) : 0.0f;
        }
    }

    double outputSampleRate_ = 48000.0;
    uint32_t outputChannelCount_ = 2u;
    bool prepared_ = false;
    std::array<const SampleAsset*, kSampleLaneCount> assets_ {};
    std::array<Emitter, kMaximumGrainEmitters> emitters_ {};
    std::array<Grain, kMaximumActiveGrains> grains_ {};
    std::array<SampleLanesVoiceCursor, kMaximumLaneVoices> cursors_ {};
    std::array<SampleGrainCursor, kMaximumPublishedGrainCursors>
        grainCursors_ {};
    uint32_t cursorCount_ = 0u;
    uint32_t grainCursorCount_ = 0u;
    uint64_t ageCounter_ = 0u;
    uint64_t grainCounter_ = 0u;
    std::size_t replacementIndex_ = 0u;
    uint32_t randomState_ = 0x9e3779b9u;
    uint32_t allocatorSeed_ = 0u;
    float outputPeak_ = 0.0f;
    s3g::routing::TriggerOutputAllocator<32u> allocator_ {};
};

} // namespace s3g::sample
