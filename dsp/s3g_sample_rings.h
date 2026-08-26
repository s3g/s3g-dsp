#pragma once

#include "s3g_sample_asset.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace s3g::sample {

constexpr std::size_t kSampleRingsSourceCount = 4u;
constexpr std::size_t kSampleRingsHeadCount = 8u;
constexpr std::size_t kSampleRingsMaximumRings = 32u;
constexpr std::size_t kSampleRingsMaximumChannelsPerSource = 8u;
constexpr double kSampleRingsRadialReferenceSeconds = 8.0;

enum class SampleRingsRingPath : uint8_t {
    Fixed = 0u,
    Outward,
    Inward,
    Bounce,
    Sine,
    Steps,
    Random,
    Manual,
};

enum class SampleRingsRelationship : uint8_t {
    Unison = 0u,
    Canon,
    Fan,
    Ratio,
    Shear,
    Orbit,
    Drift,
    Manual,
};

enum class SampleRingsHeadFormation : uint8_t {
    Free = 0u,
    Pairs,
    Quads,
    Field8,
};

struct SampleRingsSlotSettings {
    float start = 0.0f;
    float end = 1.0f;
    float speed = 1.0f;
    float stretch = 1.0f;
    float pitchSemitones = 0.0f;
    float nudge = 0.0f;
    float gainDecibels = -6.0f;
    bool reverse = false;

    bool valid() const noexcept
    {
        return std::isfinite(start) && std::isfinite(end)
            && start >= 0.0f && start < end && end <= 1.0f
            && std::isfinite(speed) && speed >= 0.125f && speed <= 4.0f
            && std::isfinite(stretch) && stretch >= 0.25f
            && stretch <= 4.0f
            && std::isfinite(pitchSemitones)
            && pitchSemitones >= -48.0f && pitchSemitones <= 48.0f
            && std::isfinite(nudge) && nudge >= -0.5f && nudge <= 0.5f
            && std::isfinite(gainDecibels)
            && gainDecibels >= -60.0f && gainDecibels <= 12.0f;
    }
};

struct SampleRingsSettings {
    SampleRingsRingPath ringPath = SampleRingsRingPath::Bounce;
    SampleRingsRelationship relationship = SampleRingsRelationship::Canon;
    SampleRingsHeadFormation formation = SampleRingsHeadFormation::Field8;
    std::array<SampleRingsSlotSettings, kSampleRingsSourceCount> slots {};
    std::array<float, kSampleRingsHeadCount> manualRings {{
        0.0f, 1.0f / 7.0f, 2.0f / 7.0f, 3.0f / 7.0f,
        4.0f / 7.0f, 5.0f / 7.0f, 6.0f / 7.0f, 1.0f,
    }};
    std::array<float, kSampleRingsHeadCount> manualPhases {{
        0.0f, 0.125f, 0.25f, 0.375f,
        0.5f, 0.625f, 0.75f, 0.875f,
    }};
    std::array<float, kSampleRingsHeadCount> manualRates {{
        1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
    }};
    float playbackRate = 1.0f;
    float relationshipAmount = 0.5f;
    float relationshipCenter = 0.5f;
    float relationshipGlideMilliseconds = 180.0f;
    float driftAmount = 0.0f;
    float ringPosition = 0.5f;
    float radialRatio = 1.0f;
    float pathDepth = 1.0f;
    float pathOffset = 0.0f;
    float pathSpread = 0.0f;
    float pathSlewMilliseconds = 80.0f;
    bool reverseRadialPath = false;
    bool reverseAngularMotion = false;
    float ringBlend = 1.0f;
    float loopJoin = 0.04f;
    float seamDuck = 0.0f;
    float outputGainDecibels = -9.0f;
    uint32_t headMask = 0xffu;
    uint32_t seed = 1u;

    bool valid() const noexcept
    {
        return static_cast<uint8_t>(ringPath)
                <= static_cast<uint8_t>(SampleRingsRingPath::Manual)
            && static_cast<uint8_t>(relationship)
                <= static_cast<uint8_t>(SampleRingsRelationship::Manual)
            && static_cast<uint8_t>(formation)
                <= static_cast<uint8_t>(SampleRingsHeadFormation::Field8)
            && std::all_of(slots.begin(), slots.end(),
                [](const SampleRingsSlotSettings& slot) {
                    return slot.valid();
                })
            && std::all_of(manualRings.begin(), manualRings.end(),
                [](float ring) {
                    return std::isfinite(ring) && ring >= 0.0f
                        && ring <= 1.0f;
                })
            && std::all_of(manualPhases.begin(), manualPhases.end(),
                [](float phase) {
                    return std::isfinite(phase) && phase >= 0.0f
                        && phase <= 1.0f;
                })
            && std::all_of(manualRates.begin(), manualRates.end(),
                [](float rate) {
                    return std::isfinite(rate) && rate >= -4.0f
                        && rate <= 4.0f;
                })
            && std::isfinite(playbackRate) && playbackRate >= 0.125f
            && playbackRate <= 4.0f
            && std::isfinite(relationshipAmount)
            && relationshipAmount >= -1.0f && relationshipAmount <= 1.0f
            && std::isfinite(relationshipCenter)
            && relationshipCenter >= 0.0f && relationshipCenter <= 1.0f
            && std::isfinite(relationshipGlideMilliseconds)
            && relationshipGlideMilliseconds >= 0.0f
            && relationshipGlideMilliseconds <= 2000.0f
            && std::isfinite(driftAmount) && driftAmount >= 0.0f
            && driftAmount <= 1.0f
            && std::isfinite(ringPosition) && ringPosition >= 0.0f
            && ringPosition <= 1.0f
            && std::isfinite(radialRatio) && radialRatio >= 0.0f
            && radialRatio <= 4.0f
            && std::isfinite(pathDepth) && pathDepth >= 0.0f
            && pathDepth <= 1.0f
            && std::isfinite(pathOffset) && pathOffset >= 0.0f
            && pathOffset <= 1.0f
            && std::isfinite(pathSpread) && pathSpread >= 0.0f
            && pathSpread <= 1.0f
            && std::isfinite(pathSlewMilliseconds)
            && pathSlewMilliseconds >= 0.0f
            && pathSlewMilliseconds <= 2000.0f
            && std::isfinite(ringBlend) && ringBlend >= 0.0f
            && ringBlend <= 1.0f
            && std::isfinite(loopJoin) && loopJoin >= 0.0f
            && loopJoin <= 0.5f
            && std::isfinite(seamDuck) && seamDuck >= 0.0f
            && seamDuck <= 0.75f
            && std::isfinite(outputGainDecibels)
            && outputGainDecibels >= -60.0f
            && outputGainDecibels <= 12.0f
            && (headMask & ~0xffu) == 0u && seed != 0u;
    }
};

struct SampleRingsHeadCursor {
    float phase = 0.0f;
    float phaseA = 0.0f;
    float phaseB = 0.0f;
    float rate = 1.0f;
    float radialPosition = 0.0f;
    float pathPhase = 0.0f;
    uint8_t ringA = 0u;
    uint8_t ringB = 0u;
    uint8_t sourceA = 0u;
    uint8_t sourceB = 0u;
    uint8_t channelA = 0u;
    uint8_t channelB = 0u;
    float sourceMix = 0.0f;
    uint8_t formationLeader = 0u;
    bool active = false;
};

class SampleRingsEngine {
public:
    bool prepare(double outputSampleRate) noexcept
    {
        if (!(outputSampleRate > 0.0) || !std::isfinite(outputSampleRate))
            return false;
        outputSampleRate_ = outputSampleRate;
        prepared_ = true;
        reset();
        return true;
    }

    void unprepare() noexcept
    {
        reset();
        assets_ = {};
        prepared_ = false;
    }

    void reset() noexcept
    {
        states_ = {};
        cursors_ = {};
        radialPositions_ = {};
        radialInitialized_ = {};
        relationshipAmount_ = 0.5f;
        relationshipCenter_ = 0.5f;
        outputPeak_ = 0.0f;
        orbitPhase_ = 0.0;
        ringPathPhase_ = 0.0;
        launchSignature_ = 0u;
    }

    bool setAsset(std::size_t slot, const SampleAsset* asset) noexcept
    {
        if (slot >= assets_.size() || (asset && !asset->valid()))
            return false;
        assets_[slot] = asset;
        for (auto& head : states_) head[slot] = {};
        radialInitialized_ = {};
        return true;
    }

    void setPreparedAsset(std::size_t slot, const SampleAsset* asset) noexcept
    {
        if (slot >= assets_.size()) return;
        if (assets_[slot] != asset) {
            assets_[slot] = asset;
            for (auto& head : states_) head[slot] = {};
            radialInitialized_ = {};
        }
    }

    const std::array<const SampleAsset*, kSampleRingsSourceCount>& assets()
        const noexcept { return assets_; }
    const std::array<SampleRingsHeadCursor, kSampleRingsHeadCount>& cursors()
        const noexcept { return cursors_; }
    float outputPeak() const noexcept { return outputPeak_; }

    void resync(const SampleRingsSettings& settings) noexcept
    {
        launchSignature_ = settingsSignature(settings);
        for (std::size_t head = 0u; head < states_.size(); ++head) {
            const double phase = launchPhase(head, settings);
            for (auto& state : states_[head]) {
                state.phase = phase;
                state.grainPhase = 0.0;
                state.appliedNudge = 0.0;
                state.initialized = true;
            }
        }
        radialInitialized_ = {};
        orbitPhase_ = 0.0;
        ringPathPhase_ = 0.0;
        refreshCursors(settings);
    }

    void render(const SampleRingsSettings& settings,
        float* const* outputs, uint32_t outputChannelCount,
        uint32_t frameCount, bool playing = true) noexcept
    {
        if (!outputs || outputChannelCount != kSampleRingsHeadCount) return;
        for (uint32_t channel = 0u; channel < outputChannelCount; ++channel) {
            if (!outputs[channel]) return;
            std::fill(outputs[channel], outputs[channel] + frameCount, 0.0f);
        }
        outputPeak_ = 0.0f;
        if (!prepared_ || !settings.valid() || !anyAsset()
            || frameCount == 0u) {
            cursors_ = {};
            return;
        }
        if (launchSignature_ != settingsSignature(settings))
            resync(settings);
        if (!playing) return;
        cursors_ = {};
        smoothRelationships(settings, frameCount);
        const float master = decibelsToAmplitude(
            settings.outputGainDecibels);
        const auto active = activeSources();
        const auto rings = ringCatalog();
        if (active.count == 0u || rings.count == 0u) return;

        for (uint32_t frame = 0u; frame < frameCount; ++frame) {
            updateRadialPositions(settings);
            for (std::size_t head = 0u; head < kSampleRingsHeadCount;
                 ++head) {
                if ((settings.headMask & (1u << head)) == 0u) continue;
                const uint8_t leader = formationLeader(head,
                    settings.formation);
                const ReadChoice choice = settings.formation
                        == SampleRingsHeadFormation::Free
                    ? freeChoice(radialPositions_[leader], rings,
                        settings.ringBlend)
                    : formationChoice(head, radialPositions_[leader], active,
                        rings, settings.ringBlend);
                const ReadResult first = readChannel(head, choice.a.slot,
                    choice.a.channel, settings);
                float value = first.value;
                float cursorPhase = first.phase;
                float cursorRate = first.rate;
                if ((choice.b.slot != choice.a.slot
                        || choice.b.channel != choice.a.channel)
                    && choice.mix > 0.0f) {
                    const ReadResult second = readChannel(head,
                        choice.b.slot, choice.b.channel, settings);
                    constexpr double halfPi = 1.57079632679489661923;
                    const float fadeA = static_cast<float>(std::cos(
                        choice.mix * halfPi));
                    const float fadeB = static_cast<float>(std::sin(
                        choice.mix * halfPi));
                    value = value * fadeA + second.value * fadeB;
                    cursorPhase += (second.phase - cursorPhase) * choice.mix;
                    cursorRate += (second.rate - cursorRate) * choice.mix;
                }
                outputs[head][frame] = value * master;
                outputPeak_ = std::max(outputPeak_,
                    std::abs(outputs[head][frame]));
                auto& cursor = cursors_[head];
                cursor.phase = cursorPhase;
                cursor.phaseA = first.phase;
                cursor.phaseB = first.phase;
                cursor.rate = cursorRate;
                cursor.radialPosition = radialPositions_[leader];
                cursor.pathPhase = static_cast<float>(wrap(ringPathPhase_
                    + settings.pathOffset));
                cursor.ringA = choice.a.ring;
                cursor.ringB = choice.b.ring;
                cursor.sourceA = choice.a.slot;
                cursor.sourceB = choice.b.slot;
                cursor.channelA = choice.a.channel;
                cursor.channelB = choice.b.channel;
                cursor.sourceMix = choice.mix;
                if ((choice.b.slot != choice.a.slot
                        || choice.b.channel != choice.a.channel)
                    && choice.mix > 0.0f) {
                    const auto& secondState = states_[head][choice.b.slot];
                    cursor.phaseB = static_cast<float>(visiblePhase(
                        secondState.phase, settings.slots[choice.b.slot]));
                }
                cursor.formationLeader = leader;
                cursor.active = true;
            }
            advance(settings);
        }
    }

private:
    struct HeadSourceState {
        double phase = 0.0;
        double grainPhase = 0.0;
        double driftPhase = 0.0;
        double appliedNudge = 0.0;
        float stretchMix = 0.0f;
        bool initialized = false;
    };

    struct ActiveSources {
        std::array<uint8_t, kSampleRingsSourceCount> indices {};
        std::size_t count = 0u;
    };

    struct RingRef {
        uint8_t slot = 0u;
        uint8_t channel = 0u;
        uint8_t ring = 0u;
    };

    struct RingCatalog {
        std::array<RingRef, kSampleRingsMaximumRings> entries {};
        std::size_t count = 0u;
    };

    struct ReadChoice {
        RingRef a {};
        RingRef b {};
        float mix = 0.0f;
    };

    struct ReadResult {
        float value = 0.0f;
        float phase = 0.0f;
        float rate = 0.0f;
    };

    static double wrap(double value) noexcept
    {
        value -= std::floor(value);
        return value < 0.0 ? value + 1.0 : value;
    }

    static float decibelsToAmplitude(float value) noexcept
    { return std::pow(10.0f, value / 20.0f); }

    bool anyAsset() const noexcept
    {
        return std::any_of(assets_.begin(), assets_.end(),
            [](const SampleAsset* asset) { return asset != nullptr; });
    }

    ActiveSources activeSources() const noexcept
    {
        ActiveSources result;
        for (uint8_t slot = 0u; slot < assets_.size(); ++slot)
            if (assets_[slot]) result.indices[result.count++] = slot;
        return result;
    }

    RingCatalog ringCatalog() const noexcept
    {
        RingCatalog result;
        for (uint8_t slot = 0u; slot < assets_.size(); ++slot) {
            const auto* asset = assets_[slot];
            if (!asset) continue;
            const uint8_t width = std::min<uint8_t>(asset->channelCount,
                static_cast<uint8_t>(kSampleRingsMaximumChannelsPerSource));
            for (uint8_t channel = 0u; channel < width
                 && result.count < result.entries.size(); ++channel) {
                result.entries[result.count] = { slot, channel,
                    static_cast<uint8_t>(result.count) };
                ++result.count;
            }
        }
        return result;
    }

    static RingRef findRing(const RingCatalog& rings, uint8_t slot,
        uint8_t channel) noexcept
    {
        for (std::size_t index = 0u; index < rings.count; ++index)
            if (rings.entries[index].slot == slot
                && rings.entries[index].channel == channel)
                return rings.entries[index];
        return rings.entries[0u];
    }

    static uint32_t hash(uint32_t value) noexcept
    {
        value ^= value >> 16u;
        value *= 0x7feb352du;
        value ^= value >> 15u;
        value *= 0x846ca68bu;
        value ^= value >> 16u;
        return value;
    }

    static uint32_t settingsSignature(
        const SampleRingsSettings& settings) noexcept
    {
        uint32_t result = static_cast<uint32_t>(settings.relationship);
        if (settings.relationship == SampleRingsRelationship::Canon
            || settings.relationship == SampleRingsRelationship::Shear)
            result |= static_cast<uint32_t>(std::lround(
                (settings.relationshipAmount + 1.0f) * 1000.0f)) << 4u;
        if (settings.relationship == SampleRingsRelationship::Drift
            || settings.driftAmount > 0.0f)
            result ^= settings.seed * 0x9e3779b9u;
        if (settings.relationship == SampleRingsRelationship::Manual) {
            for (std::size_t head = 0u; head < settings.manualPhases.size();
                 ++head)
                result ^= hash(static_cast<uint32_t>(std::lround(
                    settings.manualPhases[head] * 10000.0f))
                    + static_cast<uint32_t>(head * 97u));
        }
        return result;
    }

    static double launchPhase(std::size_t head,
        const SampleRingsSettings& settings) noexcept
    {
        const double unit = static_cast<double>(head)
            / static_cast<double>(kSampleRingsHeadCount);
        switch (settings.relationship) {
        case SampleRingsRelationship::Canon:
            return wrap(unit * settings.relationshipAmount);
        case SampleRingsRelationship::Shear:
            return wrap(unit * settings.relationshipAmount * 0.5);
        case SampleRingsRelationship::Orbit:
            return unit;
        case SampleRingsRelationship::Manual:
            return settings.manualPhases[head];
        default: return 0.0;
        }
    }

    static uint8_t formationSize(SampleRingsHeadFormation formation) noexcept
    {
        switch (formation) {
        case SampleRingsHeadFormation::Pairs: return 2u;
        case SampleRingsHeadFormation::Quads: return 4u;
        case SampleRingsHeadFormation::Field8: return 8u;
        case SampleRingsHeadFormation::Free:
        default: return 1u;
        }
    }

    static uint8_t formationLeader(std::size_t head,
        SampleRingsHeadFormation formation) noexcept
    {
        const uint8_t size = formationSize(formation);
        return static_cast<uint8_t>(head - head % size);
    }

    static float blendedFraction(double fraction, float blend) noexcept
    {
        const float smooth = static_cast<float>(fraction * fraction
            * (3.0 - 2.0 * fraction));
        const float hard = fraction >= 0.5 ? 1.0f : 0.0f;
        return hard + (smooth - hard) * blend;
    }

    static ReadChoice freeChoice(float radialPosition,
        const RingCatalog& rings, float blend) noexcept
    {
        if (rings.count == 1u)
            return { rings.entries[0u], rings.entries[0u], 0.0f };
        const double position = std::clamp<double>(radialPosition, 0.0, 1.0)
            * static_cast<double>(rings.count - 1u);
        const std::size_t first = std::min(rings.count - 1u,
            static_cast<std::size_t>(std::floor(position)));
        const std::size_t second = std::min(rings.count - 1u, first + 1u);
        const float mix = blendedFraction(position - std::floor(position),
            blend);
        return { rings.entries[first], rings.entries[second], mix };
    }

    ReadChoice formationChoice(std::size_t head, float radialPosition,
        const ActiveSources& active, const RingCatalog& rings,
        float blend) const noexcept
    {
        if (active.count == 1u) {
            const uint8_t slot = active.indices[0u];
            const uint8_t width = std::min<uint8_t>(assets_[slot]->channelCount,
                static_cast<uint8_t>(kSampleRingsMaximumChannelsPerSource));
            const uint8_t channel = static_cast<uint8_t>(head % width);
            const RingRef ring = findRing(rings, slot, channel);
            return { ring, ring, 0.0f };
        }
        const double position = std::clamp<double>(radialPosition, 0.0, 1.0)
            * static_cast<double>(active.count - 1u);
        const std::size_t first = std::min(active.count - 1u,
            static_cast<std::size_t>(std::floor(position)));
        const std::size_t second = std::min(active.count - 1u, first + 1u);
        const uint8_t slotA = active.indices[first];
        const uint8_t slotB = active.indices[second];
        const uint8_t widthA = std::min<uint8_t>(assets_[slotA]->channelCount,
            static_cast<uint8_t>(kSampleRingsMaximumChannelsPerSource));
        const uint8_t widthB = std::min<uint8_t>(assets_[slotB]->channelCount,
            static_cast<uint8_t>(kSampleRingsMaximumChannelsPerSource));
        const RingRef ringA = findRing(rings, slotA,
            static_cast<uint8_t>(head % widthA));
        const RingRef ringB = findRing(rings, slotB,
            static_cast<uint8_t>(head % widthB));
        const float mix = blendedFraction(position - std::floor(position),
            blend);
        return { ringA, ringB, mix };
    }

    float pathTarget(uint8_t leader,
        const SampleRingsSettings& settings) const noexcept
    {
        const uint8_t size = formationSize(settings.formation);
        const uint8_t group = static_cast<uint8_t>(leader / size);
        const uint8_t groups = static_cast<uint8_t>(kSampleRingsHeadCount
            / size);
        const double groupUnit = groups > 1u
            ? static_cast<double>(group) / static_cast<double>(groups - 1u)
            : 0.5;
        if (settings.ringPath == SampleRingsRingPath::Manual)
            return settings.manualRings[leader];
        if (settings.ringPath == SampleRingsRingPath::Fixed)
            return std::clamp(settings.ringPosition
                + static_cast<float>((groupUnit - 0.5)
                    * settings.pathSpread), 0.0f, 1.0f);

        const double phase = wrap(ringPathPhase_ + settings.pathOffset
            + (groupUnit - 0.5) * settings.pathSpread);
        double shape = phase;
        switch (settings.ringPath) {
        case SampleRingsRingPath::Inward: shape = 1.0 - phase; break;
        case SampleRingsRingPath::Bounce:
            shape = phase < 0.5 ? phase * 2.0 : 2.0 - phase * 2.0;
            break;
        case SampleRingsRingPath::Sine:
            shape = 0.5 - 0.5 * std::cos(
                phase * 6.28318530717958647692);
            break;
        case SampleRingsRingPath::Steps:
            shape = std::floor(phase * 4.0) / 3.0;
            break;
        case SampleRingsRingPath::Random: {
            const uint32_t step = static_cast<uint32_t>(
                std::floor(phase * 16.0));
            shape = static_cast<double>(hash(settings.seed
                ^ static_cast<uint32_t>(group * 0x9e3779b9u) ^ step))
                / static_cast<double>(0xffffffffu);
            break;
        }
        case SampleRingsRingPath::Outward:
        case SampleRingsRingPath::Fixed:
        case SampleRingsRingPath::Manual:
        default: break;
        }
        return std::clamp(settings.ringPosition
            + static_cast<float>((shape - 0.5) * settings.pathDepth),
            0.0f, 1.0f);
    }

    void updateRadialPositions(const SampleRingsSettings& settings)
        noexcept
    {
        const uint8_t size = formationSize(settings.formation);
        const float coefficient = settings.pathSlewMilliseconds <= 0.0f
            ? 1.0f : static_cast<float>(1.0 - std::exp(-1.0
                / (outputSampleRate_ * settings.pathSlewMilliseconds
                    * 0.001)));
        for (uint8_t leader = 0u; leader < kSampleRingsHeadCount;
             leader = static_cast<uint8_t>(leader + size)) {
            const float target = pathTarget(leader, settings);
            if (!radialInitialized_[leader]) {
                radialPositions_[leader] = target;
                radialInitialized_[leader] = true;
            } else {
                radialPositions_[leader] += coefficient
                    * (target - radialPositions_[leader]);
            }
            for (uint8_t member = 1u; member < size; ++member)
                radialPositions_[leader + member] = radialPositions_[leader];
        }
    }

    static float interpolate(const SampleAsset& asset, uint8_t channel,
        double normalized) noexcept
    {
        if (channel >= asset.channelCount || asset.frameCount() == 0u)
            return 0.0f;
        const auto& samples = asset.channels[channel];
        const double position = std::clamp(normalized, 0.0, 1.0)
            * static_cast<double>(samples.size() - 1u);
        const std::size_t first = static_cast<std::size_t>(
            std::floor(position));
        const std::size_t second = std::min(first + 1u,
            samples.size() - 1u);
        const float mix = static_cast<float>(position - std::floor(position));
        return samples[first] + (samples[second] - samples[first]) * mix;
    }

    static float seamRead(const SampleAsset& asset, uint8_t channel,
        double phase, const SampleRingsSlotSettings& slot, float join,
        float duck) noexcept
    {
        phase = visiblePhase(phase, slot);
        const double span = slot.end - slot.start;
        const double normalized = slot.start + phase * span;
        const float primary = interpolate(asset, channel, normalized);
        const double width = std::min(0.5,
            static_cast<double>(std::clamp(join, 0.0f, 0.5f)));
        if (!(width > 0.0) || phase < 1.0 - width) return primary;
        const double local = (phase - (1.0 - width)) / width;
        const double headPhase = phase - (1.0 - width);
        double secondaryNormalized = slot.start + headPhase * span;
        if (slot.reverse) secondaryNormalized = slot.end - headPhase * span;
        const float secondary = interpolate(asset, channel,
            secondaryNormalized);
        const float smooth = static_cast<float>(local * local
            * (3.0 - 2.0 * local));
        const float attenuation = 1.0f - std::clamp(duck, 0.0f, 0.75f)
            * static_cast<float>(std::sin(3.14159265358979323846 * local));
        return (primary + (secondary - primary) * smooth) * attenuation;
    }

    static double visiblePhase(double phase,
        const SampleRingsSlotSettings& slot) noexcept
    {
        phase = wrap(phase + slot.nudge);
        return slot.reverse ? 1.0 - phase : phase;
    }

    static float grainWindow(double phase) noexcept
    {
        return static_cast<float>(0.5 - 0.5 * std::cos(
            6.28318530717958647692 * std::clamp(phase, 0.0, 1.0)));
    }

    ReadResult readChannel(std::size_t stateHead, std::size_t slotIndex,
        uint8_t channel, const SampleRingsSettings& settings) noexcept
    {
        const auto* asset = assets_[slotIndex];
        if (!asset || channel >= asset->channelCount) return {};
        auto& state = states_[stateHead][slotIndex];
        if (!state.initialized) {
            state.phase = launchPhase(stateHead, settings);
            state.driftPhase = static_cast<double>(hash(static_cast<uint32_t>(
                stateHead * 31u + slotIndex * 131u + settings.seed)))
                / static_cast<double>(0xffffffffu);
            state.initialized = true;
        }
        const auto& slot = settings.slots[slotIndex];
        const double spanFrames = std::max(2.0,
            static_cast<double>(asset->frameCount() - 1u)
                * static_cast<double>(slot.end - slot.start));
        const double nativeIncrement = asset->sampleRate
            / outputSampleRate_ / spanFrames;
        const double relationship = relationshipRate(stateHead, settings,
            state);
        const double angularDirection = settings.reverseAngularMotion
            ? -1.0 : 1.0;
        const double pitch = std::pow(2.0,
            static_cast<double>(slot.pitchSemitones) / 12.0);
        const double readIncrement = angularDirection * nativeIncrement
            * settings.playbackRate * slot.speed * relationship * pitch;
        const double transportIncrement = angularDirection * nativeIncrement
            * settings.playbackRate * slot.speed * relationship / slot.stretch;
        const double window = std::clamp(asset->sampleRate * 0.080
            / spanFrames, 1.0 / spanFrames, 0.5);
        const float direct = seamRead(*asset, channel, state.phase, slot,
            settings.loopJoin, settings.seamDuck);
        float value = direct;
        if (std::abs(readIncrement - transportIncrement) > 1.0e-12
            && window > 0.0) {
            const double secondPhase = wrap(state.grainPhase + 0.5);
            const auto grainRead = [&](double grainPhase) noexcept {
                const double delay = readIncrement >= transportIncrement
                    ? window * (1.0 - grainPhase) : window * grainPhase;
                return seamRead(*asset, channel, wrap(state.phase - delay),
                    slot, settings.loopJoin, settings.seamDuck);
            };
            const float firstWeight = grainWindow(state.grainPhase);
            const float secondWeight = grainWindow(secondPhase);
            const float normalizer = 1.0f / std::max(1.0e-6f,
                firstWeight + secondWeight);
            const float shifted = (grainRead(state.grainPhase) * firstWeight
                + grainRead(secondPhase) * secondWeight) * normalizer;
            value = direct + (shifted - direct) * state.stretchMix;
        }
        value *= decibelsToAmplitude(slot.gainDecibels);
        const double visibleDirection = slot.reverse
            ? -angularDirection : angularDirection;
        return { value, static_cast<float>(visiblePhase(state.phase, slot)),
            static_cast<float>(visibleDirection * settings.playbackRate
                * slot.speed * relationship / slot.stretch) };
    }

    void refreshCursors(const SampleRingsSettings& settings) noexcept
    {
        cursors_ = {};
        if (!prepared_ || !settings.valid() || !anyAsset()) return;
        const auto active = activeSources();
        const auto rings = ringCatalog();
        if (active.count == 0u || rings.count == 0u) return;
        updateRadialPositions(settings);
        for (std::size_t head = 0u; head < kSampleRingsHeadCount; ++head) {
            if ((settings.headMask & (1u << head)) == 0u) continue;
            const uint8_t leader = formationLeader(head,
                settings.formation);
            const ReadChoice choice = settings.formation
                    == SampleRingsHeadFormation::Free
                ? freeChoice(radialPositions_[leader], rings,
                    settings.ringBlend)
                : formationChoice(head, radialPositions_[leader], active,
                    rings, settings.ringBlend);
            const ReadResult first = readChannel(head, choice.a.slot,
                choice.a.channel, settings);
            float cursorPhase = first.phase;
            float cursorRate = first.rate;
            float secondPhase = first.phase;
            if ((choice.b.slot != choice.a.slot
                    || choice.b.channel != choice.a.channel)
                && choice.mix > 0.0f) {
                const ReadResult second = readChannel(head, choice.b.slot,
                    choice.b.channel, settings);
                secondPhase = second.phase;
                cursorPhase += (second.phase - cursorPhase) * choice.mix;
                cursorRate += (second.rate - cursorRate) * choice.mix;
            }
            auto& cursor = cursors_[head];
            cursor.phase = cursorPhase;
            cursor.phaseA = first.phase;
            cursor.phaseB = secondPhase;
            cursor.rate = cursorRate;
            cursor.radialPosition = radialPositions_[leader];
            cursor.pathPhase = static_cast<float>(wrap(ringPathPhase_
                + settings.pathOffset));
            cursor.ringA = choice.a.ring;
            cursor.ringB = choice.b.ring;
            cursor.sourceA = choice.a.slot;
            cursor.sourceB = choice.b.slot;
            cursor.channelA = choice.a.channel;
            cursor.channelB = choice.b.channel;
            cursor.sourceMix = choice.mix;
            cursor.formationLeader = leader;
            cursor.active = true;
        }
    }

    double relationshipRate(std::size_t head,
        const SampleRingsSettings& settings,
        const HeadSourceState& state) const noexcept
    {
        const double unit = static_cast<double>(head)
            / static_cast<double>(kSampleRingsHeadCount - 1u);
        const double center = relationshipCenter_;
        const double relation = std::clamp((unit - center) * 2.0,
            -1.0, 1.0);
        const double amount = relationshipAmount_;
        double result = 1.0;
        switch (settings.relationship) {
        case SampleRingsRelationship::Fan:
            result = 1.0 + relation * amount;
            break;
        case SampleRingsRelationship::Ratio: {
            constexpr std::array<double, kSampleRingsHeadCount> ratios {{
                0.5, 2.0 / 3.0, 0.75, 1.0,
                4.0 / 3.0, 1.5, 2.0, 3.0,
            }};
            const std::size_t ratioHead = amount < 0.0
                ? kSampleRingsHeadCount - 1u - head : head;
            result = 1.0 + (ratios[ratioHead] - 1.0)
                * std::abs(amount);
            break;
        }
        case SampleRingsRelationship::Shear:
            result = 1.0 + relation * amount * 0.6;
            break;
        case SampleRingsRelationship::Orbit:
            result = 1.0 + amount * 0.22 * std::sin(orbitPhase_
                + unit * 6.28318530717958647692);
            break;
        case SampleRingsRelationship::Drift:
            result = 1.0 + amount * 0.15 * std::sin(
                state.driftPhase * 6.28318530717958647692);
            break;
        case SampleRingsRelationship::Manual:
            result = settings.manualRates[head];
            break;
        case SampleRingsRelationship::Unison:
        case SampleRingsRelationship::Canon:
        default: break;
        }
        const double drift = settings.driftAmount > 0.0f
            ? 1.0 + settings.driftAmount * 0.08 * std::sin(
                state.driftPhase * 6.28318530717958647692) : 1.0;
        if (settings.relationship == SampleRingsRelationship::Manual)
            return std::clamp(result * drift, -4.0, 4.0);
        return std::clamp(result * drift, 0.05, 4.0);
    }

    double pathIncrement(const SampleRingsSettings& settings) const
        noexcept
    {
        const double direction = settings.reverseRadialPath ? -1.0 : 1.0;
        return direction * settings.playbackRate * settings.radialRatio
            / (outputSampleRate_ * kSampleRingsRadialReferenceSeconds);
    }

    void advance(const SampleRingsSettings& settings) noexcept
    {
        for (std::size_t head = 0u; head < states_.size(); ++head) {
            for (std::size_t slotIndex = 0u; slotIndex < assets_.size();
                 ++slotIndex) {
                const auto* asset = assets_[slotIndex];
                if (!asset) continue;
                auto& state = states_[head][slotIndex];
                if (!state.initialized) {
                    state.phase = launchPhase(head, settings);
                    state.driftPhase = static_cast<double>(hash(
                        static_cast<uint32_t>(head * 31u
                            + slotIndex * 131u + settings.seed)))
                        / static_cast<double>(0xffffffffu);
                    state.initialized = true;
                }
                const auto& slot = settings.slots[slotIndex];
                const double spanFrames = std::max(2.0,
                    static_cast<double>(asset->frameCount() - 1u)
                        * static_cast<double>(slot.end - slot.start));
                const double nativeIncrement = asset->sampleRate
                    / outputSampleRate_ / spanFrames;
                const double relationship = relationshipRate(head, settings,
                    state);
                const double angularDirection = settings.reverseAngularMotion
                    ? -1.0 : 1.0;
                const double pitch = std::pow(2.0,
                    static_cast<double>(slot.pitchSemitones) / 12.0);
                const double readIncrement = angularDirection * nativeIncrement
                    * settings.playbackRate * slot.speed * relationship * pitch;
                const double transportIncrement = angularDirection
                    * nativeIncrement
                    * settings.playbackRate * slot.speed * relationship
                    / slot.stretch;
                const double window = std::clamp(asset->sampleRate * 0.080
                    / spanFrames, 1.0 / spanFrames, 0.5);
                if (window > 0.0)
                    state.grainPhase = wrap(state.grainPhase
                        + std::abs(readIncrement - transportIncrement)
                            / window);
                state.phase = wrap(state.phase + transportIncrement);
                state.driftPhase = wrap(state.driftPhase
                    + (0.006 + 0.003 * static_cast<double>((head
                        + slotIndex * 3u) % 5u)) / outputSampleRate_);
                const float target = std::abs(
                    readIncrement - transportIncrement) > 1.0e-12
                    ? 1.0f : 0.0f;
                const float coefficient = static_cast<float>(1.0
                    - std::exp(-1.0 / (outputSampleRate_ * 0.010)));
                state.stretchMix += coefficient
                    * (target - state.stretchMix);
            }
        }
        orbitPhase_ = wrap(orbitPhase_ + (0.02
            + std::abs(settings.relationshipAmount) * 0.18)
            / outputSampleRate_);
        if (settings.ringPath != SampleRingsRingPath::Fixed
            && settings.ringPath != SampleRingsRingPath::Manual)
            ringPathPhase_ = wrap(ringPathPhase_ + pathIncrement(settings));
    }

    void smoothRelationships(const SampleRingsSettings& settings,
        uint32_t frameCount) noexcept
    {
        const double seconds = settings.relationshipGlideMilliseconds
            * 0.001;
        const float coefficient = seconds <= 0.0 ? 1.0f
            : static_cast<float>(1.0 - std::exp(
                -static_cast<double>(frameCount)
                    / (outputSampleRate_ * seconds)));
        relationshipAmount_ += coefficient
            * (settings.relationshipAmount - relationshipAmount_);
        relationshipCenter_ += coefficient
            * (settings.relationshipCenter - relationshipCenter_);
    }

    double outputSampleRate_ = 48000.0;
    bool prepared_ = false;
    std::array<const SampleAsset*, kSampleRingsSourceCount> assets_ {};
    std::array<std::array<HeadSourceState, kSampleRingsSourceCount>,
        kSampleRingsHeadCount> states_ {};
    std::array<SampleRingsHeadCursor, kSampleRingsHeadCount> cursors_ {};
    std::array<float, kSampleRingsHeadCount> radialPositions_ {};
    std::array<bool, kSampleRingsHeadCount> radialInitialized_ {};
    float relationshipAmount_ = 0.5f;
    float relationshipCenter_ = 0.5f;
    float outputPeak_ = 0.0f;
    double orbitPhase_ = 0.0;
    double ringPathPhase_ = 0.0;
    uint32_t launchSignature_ = 0u;
};

} // namespace s3g::sample
