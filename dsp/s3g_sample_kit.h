#pragma once

#include "s3g_sample_player.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

namespace s3g::sample {

constexpr std::size_t kSampleKitPadCount = 16u;
constexpr std::size_t kSampleKitVariationCount = 8u;
constexpr uint32_t kSampleKitOutputChannels = 32u;
constexpr std::size_t kSampleKitMaximumBlockEvents = 2048u;
constexpr std::size_t kSampleKitMaximumEventsPerPad = 256u;

enum class SampleKitVariationMode : uint8_t {
    Cycle = 0u,
    Shuffle,
    Random,
    NoRepeat,
    Velocity,
};

struct SampleKitPadSettings {
    float gainDecibels = -6.0f;
    float pan = 0.0f;
    float tuneSemitones = 0.0f;
    double start = 0.0;
    double end = 1.0;
    float attackProportion = 0.0f;
    float decayProportion = 0.0f;
    float sustain = 1.0f;
    float releaseProportion = 0.005f;
    FilterType filterType = FilterType::Off;
    float filterCutoffHz = 20000.0f;
    float filterResonance = 0.0f;
    float velocitySensitivity = 1.0f;
    uint8_t chokeGroup = 0u;
    uint8_t outputPair = 0u;
    bool muted = false;
    bool soloed = false;
    PlayMode playMode = PlayMode::Forward;
    TriggerMode triggerMode = TriggerMode::OneShot;
    double loopCrossfade = 0.02;
    SampleKitVariationMode variationMode = SampleKitVariationMode::Cycle;
    bool naturalEnabled = false;
    float naturalGainDecibels = 0.0f;
    float naturalPitchCents = 0.0f;
    float naturalStartMilliseconds = 0.0f;
    float naturalTimingMilliseconds = 0.0f;
};

struct SampleKitSettings {
    std::array<SampleKitPadSettings, kSampleKitPadCount> pads {};
    uint8_t baseNote = 36u;
    uint8_t activeOutputPairs = 1u;
    float masterGainDecibels = -6.0f;
    float drive = 0.0f;
    uint8_t bitDepth = 24u;
    uint8_t rateReduction = 1u;
    bool naturalEnabled = true;
    uint32_t randomSeed = 1u;
};

class SampleKitEngine {
public:
    bool prepare(double sampleRate, uint32_t maximumFrames)
    {
        if (!(sampleRate > 0.0) || !std::isfinite(sampleRate)
            || maximumFrames == 0u) return false;
        sampleRate_ = sampleRate;
        maximumFrames_ = maximumFrames;
        try {
            for (auto& pad : padScratch_)
                for (auto& channel : pad)
                    channel.assign(maximumFrames, 0.0f);
            for (auto& channel : variationScratch_)
                channel.assign(maximumFrames, 0.0f);
            for (auto& pad : players_)
                for (auto& player : pad)
                    if (!player)
                        player = std::make_unique<SamplePlayerEngine>();
        } catch (...) {
            unprepare();
            return false;
        }
        for (std::size_t pad = 0u; pad < players_.size(); ++pad)
            for (std::size_t variation = 0u;
                 variation < players_[pad].size(); ++variation) {
                auto& player = players_[pad][variation];
                if (!player || !player->prepare(sampleRate, 2u)) {
                    unprepare();
                    return false;
                }
                player->setPreparedAsset(assets_[pad][variation]);
            }
        prepared_ = true;
        reset();
        return true;
    }

    void unprepare() noexcept
    {
        for (auto& pad : players_)
            for (auto& player : pad)
                if (player) player->unprepare();
        for (auto& pad : padScratch_)
            for (auto& channel : pad) channel.clear();
        for (auto& channel : variationScratch_) channel.clear();
        maximumFrames_ = 0u;
        prepared_ = false;
    }

    void reset() noexcept
    {
        for (auto& pad : players_)
            for (auto& player : pad)
                if (player) player->reset();
        heldSamples_.fill(0.0f);
        padPeaks_.fill(0.0f);
        lastSelectedVariations_.fill(0xffu);
        pendingEvents_ = {};
        reseed(randomSeed_);
        decimationPhase_ = 0u;
        outputPeak_ = 0.0f;
        previousPeak_ = 0.0f;
    }

    void killAll() noexcept
    {
        for (auto& pad : players_)
            for (auto& player : pad)
                if (player) player->killAll();
        pendingEvents_ = {};
    }

    bool setAsset(std::size_t pad, const SampleAsset* asset) noexcept
    {
        return setAsset(pad, 0u, asset);
    }

    bool setAsset(std::size_t pad, std::size_t variation,
        const SampleAsset* asset) noexcept
    {
        if (pad >= players_.size() || variation >= kSampleKitVariationCount
            || (asset && asset->channelCount > 2u)
            || (players_[pad][variation]
                && !players_[pad][variation]->setAsset(asset))) return false;
        assets_[pad][variation] = asset;
        return true;
    }

    void setPreparedAsset(std::size_t pad, const SampleAsset* asset) noexcept
    {
        setPreparedAsset(pad, 0u, asset);
    }

    void setPreparedAsset(std::size_t pad, std::size_t variation,
        const SampleAsset* asset) noexcept
    {
        if (pad >= players_.size() || variation >= kSampleKitVariationCount)
            return;
        assets_[pad][variation] = asset;
        if (players_[pad][variation])
            players_[pad][variation]->setPreparedAsset(asset);
    }

    float padPeak(std::size_t pad) const noexcept
    {
        return pad < padPeaks_.size() ? padPeaks_[pad] : 0.0f;
    }

    float outputPeak() const noexcept { return outputPeak_; }

    uint8_t lastSelectedVariation(std::size_t pad) const noexcept
    {
        return pad < lastSelectedVariations_.size()
            ? lastSelectedVariations_[pad] : 0xffu;
    }

    std::size_t activeVoiceCount() const noexcept
    {
        std::size_t result = 0u;
        for (const auto& pad : players_)
            for (const auto& player : pad)
                if (player) result += player->activeVoiceCount();
        return result;
    }

    void render(const SampleKitSettings& settings,
        const RenderEvent* events, std::size_t eventCount,
        float* const* outputs, uint32_t outputChannelCount,
        uint32_t frameCount) noexcept
    {
        if (!outputs || outputChannelCount != kSampleKitOutputChannels)
            return;
        for (uint32_t channel = 0u; channel < outputChannelCount; ++channel) {
            if (!outputs[channel]) return;
            std::fill(outputs[channel], outputs[channel] + frameCount, 0.0f);
        }
        if (!prepared_ || frameCount == 0u || frameCount > maximumFrames_)
            return;
        outputPeak_ = 0.0f;

        if (settings.randomSeed != randomSeed_)
            reseed(settings.randomSeed);
        EventCounts padEventCounts {};
        servicePendingEvents(padEventCounts, frameCount);
        const uint8_t baseNote = std::min<uint8_t>(settings.baseNote, 112u);
        eventCount = events
            ? std::min(eventCount, kSampleKitMaximumBlockEvents) : 0u;
        for (std::size_t index = 0u; index < eventCount; ++index) {
            const RenderEvent& source = events[index];
            if (source.key < baseNote
                || source.key >= baseNote + kSampleKitPadCount) continue;
            const std::size_t pad = source.key - baseNote;
            if (source.kind == EventKind::NoteOn) {
                const uint8_t group = settings.pads[pad].chokeGroup;
                if (group != 0u) {
                    for (std::size_t other = 0u;
                         other < kSampleKitPadCount; ++other) {
                        if (other == pad
                            || settings.pads[other].chokeGroup != group)
                            continue;
                        RenderEvent choke {
                            std::min(source.frameOffset, frameCount),
                            EventKind::Choke, 0u,
                            static_cast<uint8_t>(baseNote + other),
                            0.0f, 0u,
                        };
                        cancelPendingEvents(other, choke, true);
                        appendAllVariations(other, padEventCounts, choke);
                    }
                }
            }
            if (source.kind == EventKind::NoteOn)
                routeNoteOn(settings, pad, source, padEventCounts,
                    frameCount);
            else {
                cancelPendingEvents(pad, source,
                    source.kind == EventKind::Choke);
                appendAllVariations(pad, padEventCounts, source);
            }
        }

        const bool anySolo = std::any_of(settings.pads.begin(),
            settings.pads.end(), [](const SampleKitPadSettings& pad) {
                return pad.soloed;
            });
        const uint32_t activePairs = std::clamp<uint32_t>(
            settings.activeOutputPairs, 1u, 16u);
        for (std::size_t pad = 0u; pad < kSampleKitPadCount; ++pad) {
            float* stereo[] {
                padScratch_[pad][0u].data(),
                padScratch_[pad][1u].data(),
            };
            std::fill(stereo[0u], stereo[0u] + frameCount, 0.0f);
            std::fill(stereo[1u], stereo[1u] + frameCount, 0.0f);
            const PlayerSettings playerSettings = makePlayerSettings(
                settings.pads[pad], static_cast<uint8_t>(baseNote + pad));
            padPeaks_[pad] = 0.0f;
            for (std::size_t variation = 0u;
                 variation < kSampleKitVariationCount; ++variation) {
                float* variationOutput[] {
                    variationScratch_[0u].data(),
                    variationScratch_[1u].data(),
                };
                players_[pad][variation]->render(playerSettings,
                    padEvents_[pad][variation].data(),
                    padEventCounts[pad][variation], variationOutput, 2u,
                    frameCount);
                padPeaks_[pad] = std::max(padPeaks_[pad],
                    players_[pad][variation]->outputPeak());
                for (uint32_t frame = 0u; frame < frameCount; ++frame) {
                    stereo[0u][frame] += variationOutput[0u][frame];
                    stereo[1u][frame] += variationOutput[1u][frame];
                }
            }
            const auto& padSettings = settings.pads[pad];
            if (padSettings.muted || (anySolo && !padSettings.soloed))
                continue;
            const uint32_t pair = std::min<uint32_t>(
                padSettings.outputPair, 15u) % activePairs;
            const uint32_t left = pair * 2u;
            const uint32_t right = left + 1u;
            for (uint32_t frame = 0u; frame < frameCount; ++frame) {
                const float l = stereo[0u][frame];
                const float r = stereo[1u][frame];
                outputs[left][frame] += l;
                outputs[right][frame] += r;
            }
        }

        applyEffects(settings, outputs, outputChannelCount, frameCount,
            activePairs);
    }

private:
    using EventCounts = std::array<std::array<std::size_t,
        kSampleKitVariationCount>, kSampleKitPadCount>;

    struct SelectionState {
        uint64_t random = 1u;
        uint32_t cycle = 0u;
        std::array<uint8_t, kSampleKitVariationCount> shuffle {};
        uint8_t shufflePosition = 0u;
        uint8_t shuffleCount = 0u;
        uint8_t shuffleMask = 0u;
        uint8_t last = 0xffu;
    };

    struct PendingEvent {
        RenderEvent event {};
        uint32_t remainingFrames = 0u;
        uint8_t pad = 0u;
        uint8_t variation = 0u;
        bool active = false;
    };

    void appendEvent(std::size_t pad, std::size_t variation,
        EventCounts& counts, const RenderEvent& event) noexcept
    {
        if (pad >= kSampleKitPadCount
            || variation >= kSampleKitVariationCount
            || counts[pad][variation] >= kSampleKitMaximumEventsPerPad)
            return;
        auto& count = counts[pad][variation];
        auto& destination = padEvents_[pad][variation];
        std::size_t position = count;
        while (position != 0u
            && destination[position - 1u].frameOffset > event.frameOffset) {
            destination[position] = destination[position - 1u];
            --position;
        }
        destination[position] = event;
        ++count;
    }

    void appendAllVariations(std::size_t pad, EventCounts& counts,
        const RenderEvent& event) noexcept
    {
        for (std::size_t variation = 0u;
             variation < kSampleKitVariationCount; ++variation)
            appendEvent(pad, variation, counts, event);
    }

    static bool matchesNote(const RenderEvent& left,
        const RenderEvent& right) noexcept
    {
        if (left.noteId != 0u && right.noteId != 0u)
            return left.noteId == right.noteId;
        return left.key == right.key
            && left.midiChannel == right.midiChannel;
    }

    void cancelPendingEvents(std::size_t pad, const RenderEvent& event,
        bool all) noexcept
    {
        for (auto& pending : pendingEvents_)
            if (pending.active && pending.pad == pad
                && (all || matchesNote(pending.event, event)))
                pending.active = false;
    }

    void servicePendingEvents(EventCounts& counts,
        uint32_t frameCount) noexcept
    {
        for (auto& pending : pendingEvents_) {
            if (!pending.active) continue;
            if (pending.remainingFrames >= frameCount) {
                pending.remainingFrames -= frameCount;
                continue;
            }
            pending.event.frameOffset = pending.remainingFrames;
            appendEvent(pending.pad, pending.variation, counts,
                pending.event);
            pending.active = false;
        }
    }

    bool deferEvent(std::size_t pad, std::size_t variation,
        RenderEvent event, uint32_t remainingFrames) noexcept
    {
        for (auto& pending : pendingEvents_) {
            if (pending.active) continue;
            pending.event = event;
            pending.remainingFrames = remainingFrames;
            pending.pad = static_cast<uint8_t>(pad);
            pending.variation = static_cast<uint8_t>(variation);
            pending.active = true;
            return true;
        }
        return false;
    }

    static uint64_t nextRandom(uint64_t& state) noexcept
    {
        state += 0x9e3779b97f4a7c15ull;
        uint64_t value = state;
        value = (value ^ (value >> 30u)) * 0xbf58476d1ce4e5b9ull;
        value = (value ^ (value >> 27u)) * 0x94d049bb133111ebull;
        return value ^ (value >> 31u);
    }

    static double randomUnit(uint64_t& state) noexcept
    {
        return static_cast<double>(nextRandom(state) >> 11u)
            * (1.0 / 9007199254740992.0);
    }

    static double bellRandom(uint64_t& state) noexcept
    {
        return ((randomUnit(state) * 2.0 - 1.0)
            + (randomUnit(state) * 2.0 - 1.0)
            + (randomUnit(state) * 2.0 - 1.0)) / 3.0;
    }

    void reseed(uint32_t seed) noexcept
    {
        randomSeed_ = seed == 0u ? 1u : seed;
        for (std::size_t pad = 0u; pad < selectionStates_.size(); ++pad) {
            auto& state = selectionStates_[pad];
            state = {};
            state.random = static_cast<uint64_t>(randomSeed_)
                ^ (0xd1b54a32d192ed03ull
                    * static_cast<uint64_t>(pad + 1u));
            (void)nextRandom(state.random);
        }
    }

    std::size_t availableVariations(std::size_t pad,
        std::array<uint8_t, kSampleKitVariationCount>& available,
        uint8_t& mask) const noexcept
    {
        std::size_t count = 0u;
        mask = 0u;
        for (std::size_t variation = 0u;
             variation < kSampleKitVariationCount; ++variation) {
            if (!assets_[pad][variation]) continue;
            available[count++] = static_cast<uint8_t>(variation);
            mask |= static_cast<uint8_t>(1u << variation);
        }
        return count;
    }

    uint8_t chooseVariation(const SampleKitSettings& settings,
        std::size_t pad, const RenderEvent& event,
        bool natural) noexcept
    {
        std::array<uint8_t, kSampleKitVariationCount> available {};
        uint8_t mask = 0u;
        const std::size_t count = availableVariations(
            pad, available, mask);
        if (count == 0u) return 0xffu;
        if (event.variationIndex < kSampleKitVariationCount
            && assets_[pad][event.variationIndex])
            return event.variationIndex;
        if (!natural || count == 1u) return available[0u];

        auto& state = selectionStates_[pad];
        uint8_t selected = available[0u];
        switch (settings.pads[pad].variationMode) {
        case SampleKitVariationMode::Cycle:
            selected = available[state.cycle++ % count];
            break;
        case SampleKitVariationMode::Shuffle: {
            if (state.shuffleMask != mask
                || state.shufflePosition >= state.shuffleCount) {
                state.shuffleCount = static_cast<uint8_t>(count);
                state.shufflePosition = 0u;
                state.shuffleMask = mask;
                for (std::size_t index = 0u; index < count; ++index)
                    state.shuffle[index] = available[index];
                for (std::size_t index = count; index > 1u; --index) {
                    const std::size_t other = static_cast<std::size_t>(
                        nextRandom(state.random) % index);
                    std::swap(state.shuffle[index - 1u],
                        state.shuffle[other]);
                }
                if (count > 1u && state.shuffle[0u] == state.last)
                    std::swap(state.shuffle[0u], state.shuffle[1u]);
            }
            selected = state.shuffle[state.shufflePosition++];
            break;
        }
        case SampleKitVariationMode::Random:
            selected = available[static_cast<std::size_t>(
                nextRandom(state.random) % count)];
            break;
        case SampleKitVariationMode::NoRepeat: {
            std::array<uint8_t, kSampleKitVariationCount> candidates {};
            std::size_t candidateCount = 0u;
            for (std::size_t index = 0u; index < count; ++index)
                if (available[index] != state.last)
                    candidates[candidateCount++] = available[index];
            if (candidateCount == 0u) candidates[candidateCount++]
                = available[0u];
            selected = candidates[static_cast<std::size_t>(
                nextRandom(state.random) % candidateCount)];
            break;
        }
        case SampleKitVariationMode::Velocity:
            selected = available[std::min<std::size_t>(count - 1u,
                static_cast<std::size_t>(std::clamp(event.velocity,
                    0.0f, 0.999999f) * static_cast<float>(count)))];
            break;
        }
        state.last = selected;
        return selected;
    }

    void routeNoteOn(const SampleKitSettings& settings, std::size_t pad,
        const RenderEvent& source, EventCounts& counts,
        uint32_t frameCount) noexcept
    {
        const auto& padSettings = settings.pads[pad];
        const bool natural = settings.naturalEnabled
            && padSettings.naturalEnabled;
        const uint8_t variation = chooseVariation(
            settings, pad, source, natural);
        if (variation >= kSampleKitVariationCount) return;
        lastSelectedVariations_[pad] = variation;
        RenderEvent event = source;
        event.variationIndex = variation;
        uint32_t delayFrames = 0u;
        if (natural) {
            auto& random = selectionStates_[pad].random;
            event.gainOffsetDecibels = static_cast<float>(bellRandom(random)
                * std::clamp(padSettings.naturalGainDecibels,
                    0.0f, 6.0f));
            event.fineTuneOffsetCents = static_cast<float>(bellRandom(random)
                * std::clamp(padSettings.naturalPitchCents,
                    0.0f, 100.0f));
            const auto* asset = assets_[pad][variation];
            if (asset && asset->frameCount() != 0u) {
                const double offsetFrames = bellRandom(random)
                    * std::clamp<double>(
                        padSettings.naturalStartMilliseconds, 0.0, 20.0)
                    * asset->sampleRate * 0.001;
                event.startOffsetNormalized = offsetFrames
                    / static_cast<double>(asset->frameCount());
            }
            const double timingShape = std::abs(bellRandom(random));
            delayFrames = static_cast<uint32_t>(std::llround(
                timingShape * std::clamp<double>(
                    padSettings.naturalTimingMilliseconds, 0.0, 50.0)
                    * sampleRate_ * 0.001));
        }
        const uint64_t scheduled = static_cast<uint64_t>(source.frameOffset)
            + delayFrames;
        if (scheduled < frameCount) {
            event.frameOffset = static_cast<uint32_t>(scheduled);
            appendEvent(pad, variation, counts, event);
        } else if (!deferEvent(pad, variation, event,
                static_cast<uint32_t>(std::min<uint64_t>(
                    scheduled - frameCount,
                    std::numeric_limits<uint32_t>::max())))) {
            event.frameOffset = frameCount - 1u;
            appendEvent(pad, variation, counts, event);
        }
    }

    static PlayerSettings makePlayerSettings(
        const SampleKitPadSettings& pad, uint8_t rootNote) noexcept
    {
        PlayerSettings result;
        result.playMode = pad.playMode;
        result.pitchMode = PitchMode::Rate;
        result.syncMode = SyncMode::Free;
        result.triggerMode = pad.triggerMode;
        result.retriggerMode = RetriggerMode::Restart;
        result.voiceMode = VoiceMode::Poly;
        result.start = std::clamp(pad.start, 0.0, 0.999999);
        result.length = std::max(0.000001,
            std::clamp(pad.end, result.start + 0.000001, 1.0)
                - result.start);
        result.loopStart = result.start;
        result.loopEnd = result.start + result.length;
        result.loopCrossfade = std::clamp(pad.loopCrossfade, 0.0, 0.5);
        result.tuneSemitones = std::clamp(pad.tuneSemitones,
            -60.0f, 60.0f);
        result.rootNote = rootNote;
        float attack = std::clamp(pad.attackProportion, 0.0f, 1.0f);
        float decay = std::clamp(pad.decayProportion, 0.0f, 1.0f);
        float release = std::clamp(pad.releaseProportion, 0.0f, 1.0f);
        const float total = attack + decay + release;
        if (total > 1.0f) {
            attack /= total;
            decay /= total;
            release /= total;
        }
        result.attackProportion = attack;
        result.decayProportion = decay;
        result.sustain = std::clamp(pad.sustain, 0.0f, 1.0f);
        result.releaseProportion = release;
        result.gainDecibels = std::clamp(pad.gainDecibels,
            -60.0f, 12.0f);
        result.pan = std::clamp(pad.pan, -1.0f, 1.0f);
        result.velocitySensitivity = std::clamp(
            pad.velocitySensitivity, 0.0f, 1.0f);
        result.filterType = pad.filterType;
        result.filterCutoffHz = std::clamp(pad.filterCutoffHz,
            20.0f, 20000.0f);
        result.filterResonance = std::clamp(pad.filterResonance,
            0.0f, 1.0f);
        return result;
    }

    void applyEffects(const SampleKitSettings& settings,
        float* const* outputs, uint32_t outputChannelCount,
        uint32_t frameCount, uint32_t activePairs) noexcept
    {
        const uint32_t decimation = std::clamp<uint32_t>(
            settings.rateReduction, 1u, 32u);
        const uint32_t bitDepth = std::clamp<uint32_t>(
            settings.bitDepth, 4u, 24u);
        const float quantization = std::ldexp(1.0f,
            static_cast<int>(bitDepth - 1u));
        const float drive = std::clamp(settings.drive, 0.0f, 1.0f);
        const float driveGain = 1.0f + drive * 19.0f;
        const float driveNorm = 1.0f / std::tanh(driveGain);
        const float master = std::pow(10.0f,
            std::clamp(settings.masterGainDecibels,
                -60.0f, 12.0f) / 20.0f);
        for (uint32_t frame = 0u; frame < frameCount; ++frame) {
            const bool capture = decimationPhase_ == 0u;
            for (uint32_t channel = 0u;
                 channel < outputChannelCount; ++channel) {
                const bool active = channel < activePairs * 2u;
                if (!active) {
                    heldSamples_[channel] = 0.0f;
                    outputs[channel][frame] = 0.0f;
                    continue;
                }
                float value = outputs[channel][frame];
                if (capture) {
                    if (bitDepth < 24u)
                        value = std::round(std::clamp(value, -1.0f, 1.0f)
                            * quantization) / quantization;
                    heldSamples_[channel] = value;
                } else value = heldSamples_[channel];
                if (drive > 0.000001f)
                    value = std::tanh(value * driveGain) * driveNorm;
                outputs[channel][frame] = value * master;
                outputPeak_ = std::max(outputPeak_,
                    std::abs(outputs[channel][frame]));
            }
            decimationPhase_ = (decimationPhase_ + 1u) % decimation;
        }
        const float currentPeak = outputPeak_;
        outputPeak_ = std::max(currentPeak, previousPeak_ * 0.90f);
        previousPeak_ = outputPeak_;
    }

    double sampleRate_ = 48000.0;
    uint32_t maximumFrames_ = 0u;
    bool prepared_ = false;
    std::array<std::array<std::unique_ptr<SamplePlayerEngine>,
        kSampleKitVariationCount>,
        kSampleKitPadCount> players_ {};
    std::array<std::array<const SampleAsset*, kSampleKitVariationCount>,
        kSampleKitPadCount> assets_ {};
    std::array<std::array<std::vector<float>, 2u>,
        kSampleKitPadCount> padScratch_ {};
    std::array<std::vector<float>, 2u> variationScratch_ {};
    std::array<std::array<std::array<RenderEvent,
        kSampleKitMaximumEventsPerPad>, kSampleKitVariationCount>,
        kSampleKitPadCount> padEvents_ {};
    std::array<SelectionState, kSampleKitPadCount> selectionStates_ {};
    std::array<PendingEvent, kSampleKitMaximumBlockEvents> pendingEvents_ {};
    std::array<float, kSampleKitOutputChannels> heldSamples_ {};
    std::array<float, kSampleKitPadCount> padPeaks_ {};
    std::array<uint8_t, kSampleKitPadCount> lastSelectedVariations_ {};
    uint32_t randomSeed_ = 1u;
    uint32_t decimationPhase_ = 0u;
    float outputPeak_ = 0.0f;
    float previousPeak_ = 0.0f;
};

} // namespace s3g::sample
