#pragma once

#include "s3g_sample_asset.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

namespace s3g::sample {

constexpr std::size_t kWavesetHeadCount = 2u;

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
    Harmonic,
};

enum class WavesetCrossfadeCurve : uint8_t {
    Cut = 0u,
    Sharp,
    Blend,
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

    uint32_t length() const noexcept
    {
        return endFrame > startFrame ? endFrame - startFrame : 1u;
    }

    double sampleLength() const noexcept
    {
        return endPosition > startPosition
            ? endPosition - startPosition : 1.0;
    }
};

struct WavesetMap {
    std::shared_ptr<const SampleAsset> asset;
    std::vector<WavesetUnit> units;
    WavesetCrossingDetail detail = WavesetCrossingDetail::Raw;
    uint8_t referenceChannel = 0u;

    bool valid() const noexcept
    {
        if (!asset || !asset->valid() || asset->channelCount > 2u
            || units.empty()) return false;
        return std::all_of(units.begin(), units.end(),
            [this](const WavesetUnit& unit) {
                return unit.startFrame < unit.middleFrame
                    && unit.middleFrame < unit.endFrame
                    && std::isfinite(unit.startPosition)
                    && std::isfinite(unit.endPosition)
                    && unit.startPosition >= 0.0
                    && unit.endPosition > unit.startPosition
                    && unit.endPosition
                        < static_cast<double>(asset->frameCount())
                    && unit.endFrame <= asset->frameCount();
            });
    }
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

inline std::shared_ptr<const WavesetMap> analyzeWavesets(
    std::shared_ptr<const SampleAsset> asset,
    WavesetCrossingDetail detail = WavesetCrossingDetail::Raw)
{
    if (!asset || !asset->valid() || asset->channelCount > 2u
        || asset->frameCount() < 8u) return {};

    auto map = std::make_shared<WavesetMap>();
    map->asset = std::move(asset);
    map->detail = detail;
    const uint32_t frames = map->asset->frameCount();

    // A single timing reference keeps every rendered source channel sample
    // locked. Choose the channel with the greatest energy; averaging stereo
    // channels can invent crossings through phase cancellation.
    double bestEnergy = -1.0;
    for (uint8_t channel = 0u; channel < map->asset->channelCount;
         ++channel) {
        double energy = 0.0;
        for (const float sample : map->asset->channels[channel])
            energy += static_cast<double>(sample) * sample;
        if (energy > bestEnergy) {
            bestEnergy = energy;
            map->referenceChannel = channel;
        }
    }

    const auto& source = map->asset->channels[map->referenceChannel];
    struct Crossing {
        uint32_t frame = 0u;
        double position = 0.0;
    };
    std::vector<Crossing> crossings;
    crossings.reserve(static_cast<std::size_t>(frames / 8u) + 2u);
    const double cutoff = wavesetAnalysisCutoff(detail);
    const double coefficient = cutoff > 0.0
        ? 1.0 - std::exp(-2.0 * 3.14159265358979323846 * cutoff
            / map->asset->sampleRate)
        : 1.0;
    double filtered = source[0u];
    double previous = filtered;
    constexpr uint32_t kMinimumCycleFrames = 4u;
    uint32_t lastCrossing = 0u;
    for (uint32_t frame = 1u; frame < frames; ++frame) {
        filtered += coefficient
            * (static_cast<double>(source[frame]) - filtered);
        // Graham Wakefield's waveset chopper uses rising crossings and
        // retains the fractional point between the two adjacent samples.
        // Doing the same here prevents a systematic one-sample phase error
        // while still using one shared timing map for both source channels.
        const bool rising = previous <= 0.0 && filtered > 0.0;
        if (rising && (crossings.empty()
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
    if (crossings.size() < 2u) return {};

    map->units.reserve(crossings.size() - 1u);
    for (std::size_t index = 0u; index + 1u < crossings.size(); ++index) {
        WavesetUnit unit;
        unit.startPosition = crossings[index].position;
        unit.endPosition = crossings[index + 1u].position;
        unit.startFrame = static_cast<uint32_t>(std::floor(
            unit.startPosition));
        unit.endFrame = std::min<uint32_t>(frames,
            static_cast<uint32_t>(std::ceil(unit.endPosition)) + 1u);
        const uint32_t midpoint = static_cast<uint32_t>(std::floor(
            (unit.startPosition + unit.endPosition) * 0.5));
        unit.middleFrame = std::clamp(midpoint, unit.startFrame + 1u,
            unit.endFrame - 1u);
        if (unit.endFrame <= unit.startFrame + 2u) continue;
        double squareSum = 0.0;
        float peak = 0.0f;
        for (uint32_t frame = unit.startFrame; frame < unit.endFrame;
             ++frame) {
            const float value = source[frame];
            peak = std::max(peak, std::abs(value));
            squareSum += static_cast<double>(value) * value;
        }
        unit.peak = peak;
        unit.rms = static_cast<float>(std::sqrt(squareSum
            / static_cast<double>(unit.endFrame - unit.startFrame)));
        map->units.push_back(unit);
    }
    return map->valid() ? map : std::shared_ptr<const WavesetMap> {};
}

struct WavesetSettings {
    WavesetAdvanceMode advance = WavesetAdvanceMode::Stretch;
    WavesetDirection direction = WavesetDirection::Forward;
    WavesetShape shape = WavesetShape::Repeat;
    WavesetCrossfadeCurve crossfadeCurve = WavesetCrossfadeCurve::Blend;
    uint32_t groupSize = 8u;
    uint32_t repeats = 2u;
    int32_t stride = 1;
    float processAmount = 0.0f;
    float joinAmount = 1.0f;
    float outputGainDecibels = -6.0f;
    double crossfader = -1.0;
    std::array<double, kWavesetHeadCount> positions {{
        0.0, 0.5,
    }};
    std::array<double, kWavesetHeadCount> scanRates {{
        1.0, 1.0,
    }};
    std::array<float, kWavesetHeadCount> levelsDecibels {{
        0.0f, 0.0f,
    }};

    bool valid() const noexcept
    {
        return groupSize >= 1u && groupSize <= 32u
            && repeats >= 1u && repeats <= 16u
            && stride >= -16 && stride <= 16
            && std::isfinite(processAmount)
            && std::isfinite(joinAmount)
            && std::isfinite(outputGainDecibels)
            && std::isfinite(crossfader)
            && crossfader >= -1.0 && crossfader <= 1.0
            && static_cast<uint8_t>(crossfadeCurve)
                <= static_cast<uint8_t>(WavesetCrossfadeCurve::Blend);
    }
};

enum class WavesetEventKind : uint8_t {
    PlayDeck = 0u,
    PauseDeck,
    StopDeck,
    RestartDeck,
    PlayBoth,
    PauseBoth,
    StopBoth,
    RestartBoth,
    SetPosition,
};

struct WavesetRenderEvent {
    uint32_t frameOffset = 0u;
    WavesetEventKind kind = WavesetEventKind::PlayDeck;
    uint8_t deck = 0u;
    float value = 1.0f;
};

class SampleWavesetsEngine {
public:
    void prepare(double outputSampleRate, uint32_t maximumBlockFrames)
    {
        prepared_ = outputSampleRate > 0.0
            && std::isfinite(outputSampleRate) && maximumBlockFrames > 0u;
        outputSampleRate_ = prepared_ ? outputSampleRate : 48000.0;
        reset();
    }

    void reset() noexcept
    {
        for (auto& head : heads) head = {};
        lastGroupSize_ = 1u;
        outputPeak_ = 0.0f;
        crossfadePosition_ = -1.0;
        crossfadeInitialized_ = false;
        settingsInitialized_ = false;
    }

    void setMap(const WavesetMap* map) noexcept
    {
        map_ = map && map->valid() ? map : nullptr;
        reset();
    }

    const WavesetMap* map() const noexcept { return map_; }
    float outputPeak() const noexcept { return outputPeak_; }

    float deckPositionNormalized(std::size_t index) const noexcept
    {
        if (!map_ || map_->units.empty() || index >= heads.size())
            return -1.0f;
        const Head& head = heads[index];
        const std::size_t unit = std::min(head.groupStart,
            map_->units.size() - 1u);
        return static_cast<float>(map_->units[unit].startPosition
            / static_cast<double>(map_->asset->frameCount()));
    }

    float deckOutputPhaseNormalized(std::size_t index) const noexcept
    {
        if (index >= heads.size()) return 0.0f;
        const Head& head = heads[index];
        return static_cast<float>(std::clamp(
            (static_cast<double>(head.cycleOffset)
                + head.oscillatorPhase)
                / static_cast<double>(std::max(1u, lastGroupSize_)),
            0.0, 1.0));
    }

    bool deckActive(std::size_t index) const noexcept
    {
        return index < heads.size()
            && (heads[index].playing || heads[index].envelope > 1.0e-5f);
    }

    uint8_t activeMask() const noexcept
    {
        return static_cast<uint8_t>((deckActive(0u) ? 1u : 0u)
            | (deckActive(1u) ? 2u : 0u));
    }

    bool deckPlaying(std::size_t index) const noexcept
    {
        return index < heads.size() && heads[index].playing;
    }

    void render(const WavesetSettings& settings,
        const WavesetRenderEvent* events, std::size_t eventCount,
        float* const* outputs, uint32_t outputChannelCount,
        uint32_t frameCount) noexcept
    {
        if (!outputs || outputChannelCount != 2u || !outputs[0u]
            || !outputs[1u]) return;
        std::fill(outputs[0u], outputs[0u] + frameCount, 0.0f);
        std::fill(outputs[1u], outputs[1u] + frameCount, 0.0f);
        outputPeak_ = 0.0f;
        if (!prepared_ || !map_ || !settings.valid()
            || frameCount == 0u) return;
        if (!events) eventCount = 0u;
        lastGroupSize_ = std::clamp(settings.groupSize, 1u, 32u);
        applyPositionSettings(settings);

        const float master = decibelsToAmplitude(
            settings.outputGainDecibels);
        if (!crossfadeInitialized_) {
            crossfadePosition_ = settings.crossfader;
            crossfadeInitialized_ = true;
        }
        const double crossfadeSmoothing = 1.0 - std::exp(
            -1.0 / (0.00025 * outputSampleRate_));
        std::size_t eventIndex = 0u;
        for (uint32_t frame = 0u; frame < frameCount; ++frame) {
            while (eventIndex < eventCount
                && events[eventIndex].frameOffset <= frame) {
                applyEvent(events[eventIndex], settings);
                ++eventIndex;
            }
            crossfadePosition_ += crossfadeSmoothing
                * (settings.crossfader - crossfadePosition_);
            float crossfadeA = 0.0f;
            float crossfadeB = 0.0f;
            crossfadeGains(settings.crossfadeCurve, crossfadePosition_,
                crossfadeA, crossfadeB);
            std::array<std::array<float, 2u>, kWavesetHeadCount>
                deckSamples {};
            for (std::size_t index = 0u; index < heads.size(); ++index) {
                Head& head = heads[index];
                updateEnvelope(head);
                if (head.envelope <= 1.0e-6f) continue;
                const float level = decibelsToAmplitude(
                    settings.levelsDecibels[index]);
                const float deckGain = level * head.envelope;
                for (uint32_t channel = 0u; channel < 2u; ++channel) {
                    const uint8_t sourceChannel = map_->asset->channelCount
                            == 1u ? 0u : static_cast<uint8_t>(channel);
                    deckSamples[index][channel] = renderHeadSample(
                        head, settings, sourceChannel) * deckGain;
                }
                // Let a paused deck finish its short release ramp before the
                // cursor is completely frozen, avoiding a click at pause.
                if (head.playing || head.envelope > 1.0e-5f)
                    advanceOscillator(head, settings, index);
            }
            for (uint32_t channel = 0u; channel < 2u; ++channel) {
                outputs[channel][frame] = master
                    * (deckSamples[0u][channel] * crossfadeA
                        + deckSamples[1u][channel] * crossfadeB);
            }
            outputPeak_ = std::max(outputPeak_, std::max(
                std::abs(outputs[0u][frame]),
                std::abs(outputs[1u][frame])));
        }
        while (eventIndex < eventCount
            && events[eventIndex].frameOffset <= frameCount) {
            applyEvent(events[eventIndex], settings);
            ++eventIndex;
        }
    }

private:
    struct Head {
        std::size_t groupStart = 0u;
        uint32_t cycleOffset = 0u;
        uint32_t repeatIndex = 0u;
        double oscillatorPhase = 0.0;
        double requestedPosition = -1.0;
        float envelope = 0.0f;
        bool pendulumForward = true;
        uint32_t randomState = 0x12345678u;
        bool playing = false;
    };

    static float decibelsToAmplitude(float decibels) noexcept
    {
        return decibels <= -59.99f ? 0.0f
            : std::pow(10.0f, decibels / 20.0f);
    }

    std::size_t wrapUnit(int64_t unit) const noexcept
    {
        const int64_t count = static_cast<int64_t>(map_->units.size());
        int64_t wrapped = unit % count;
        if (wrapped < 0) wrapped += count;
        return static_cast<std::size_t>(wrapped);
    }

    std::size_t resolvedUnit(const Head& head, uint32_t offset,
        WavesetDirection direction, uint32_t groupSize) const noexcept
    {
        uint32_t ordered = offset % std::max<uint32_t>(1u, groupSize);
        if (direction == WavesetDirection::Reverse)
            ordered = groupSize - 1u - ordered;
        else if (direction == WavesetDirection::Pendulum
            && !head.pendulumForward)
            ordered = groupSize - 1u - ordered;
        else if (direction == WavesetDirection::Shuffle) {
            uint32_t mixed = head.randomState
                ^ (offset * 0x9e3779b9u + 0x7f4a7c15u);
            mixed ^= mixed >> 16u;
            ordered = mixed % groupSize;
        }
        return wrapUnit(static_cast<int64_t>(head.groupStart)
            + static_cast<int64_t>(ordered));
    }

    float interpolate(uint8_t channel, const WavesetUnit& unit,
        double phase, float join) const noexcept
    {
        phase -= std::floor(phase);
        const auto& samples = map_->asset->channels[channel];
        const auto sampleAt = [&samples](double position) noexcept {
            position = std::clamp(position, 0.0,
                static_cast<double>(samples.size() - 1u));
            const uint32_t index = static_cast<uint32_t>(position);
            const uint32_t next = std::min<uint32_t>(index + 1u,
                static_cast<uint32_t>(samples.size() - 1u));
            const float fraction = static_cast<float>(position - index);
            return samples[index] + (samples[next] - samples[index])
                * fraction;
        };
        const double position = unit.startPosition
            + phase * unit.sampleLength();
        const float raw = sampleAt(position);
        const float start = sampleAt(unit.startPosition);
        const float end = sampleAt(unit.endPosition);
        const float corrected = raw - (start
            + (end - start) * static_cast<float>(phase));
        return raw + (corrected - raw) * std::clamp(join, 0.0f, 1.0f);
    }

    std::size_t strongestUnit(const Head& head,
        const WavesetSettings& settings) const noexcept
    {
        const uint32_t group = std::clamp(settings.groupSize, 1u, 32u);
        std::size_t strongest = resolvedUnit(head, 0u,
            settings.direction, group);
        for (uint32_t offset = 1u; offset < group; ++offset) {
            const std::size_t candidate = resolvedUnit(head, offset,
                settings.direction, group);
            if (map_->units[candidate].peak > map_->units[strongest].peak)
                strongest = candidate;
        }
        return strongest;
    }

    float renderHeadSample(const Head& head,
        const WavesetSettings& settings, uint8_t channel) const noexcept
    {
        const uint32_t group = std::clamp(settings.groupSize, 1u, 32u);
        const auto unitIndex = resolvedUnit(head, head.cycleOffset,
            settings.direction, group);
        const WavesetUnit& unit = map_->units[unitIndex];
        const float raw = interpolate(channel, unit,
            head.oscillatorPhase, settings.joinAmount);
        const float amount = std::clamp(settings.processAmount, 0.0f, 1.0f);
        switch (settings.shape) {
        case WavesetShape::Omit: {
            uint32_t hash = static_cast<uint32_t>(unitIndex)
                * 747796405u + 2891336453u;
            hash = ((hash >> ((hash >> 28u) + 4u)) ^ hash) * 277803737u;
            hash = (hash >> 22u) ^ hash;
            const float selector = static_cast<float>(hash & 0xffffu)
                / 65535.0f;
            return selector < amount ? raw * (1.0f - amount) : raw;
        }
        case WavesetShape::Replace: {
            const float replacement = interpolate(channel,
                map_->units[strongestUnit(head, settings)],
                head.oscillatorPhase, settings.joinAmount);
            return raw + (replacement - raw) * amount;
        }
        case WavesetShape::Envelope: {
            const double sine = std::sin(3.14159265358979323846
                * head.oscillatorPhase);
            const float shaped = raw * static_cast<float>(sine * sine);
            return raw + (shaped - raw) * amount;
        }
        case WavesetShape::Harmonic: {
            const double multiplier = 2.0
                + static_cast<double>(std::lround(amount * 6.0f));
            const float harmonic = interpolate(channel, unit,
                head.oscillatorPhase * multiplier, settings.joinAmount);
            return raw + (harmonic - raw) * amount;
        }
        case WavesetShape::Repeat:
        default:
            return raw;
        }
    }

    double oscillatorIncrement(const Head& head,
        const WavesetSettings& settings, std::size_t deck) const noexcept
    {
        const uint32_t group = std::clamp(settings.groupSize, 1u, 32u);
        const auto unitIndex = resolvedUnit(head, head.cycleOffset,
            settings.direction, group);
        const double speed = std::clamp(
            settings.scanRates[deck], 0.25, 4.0);
        const double sourceFramesPerOutput = map_->asset->sampleRate
            / outputSampleRate_ * speed;
        return sourceFramesPerOutput
            / map_->units[unitIndex].sampleLength();
    }

    void advanceOscillator(Head& head, const WavesetSettings& settings,
        std::size_t deck) noexcept
    {
        head.oscillatorPhase += oscillatorIncrement(head, settings, deck);
        while (head.oscillatorPhase >= 1.0) {
            head.oscillatorPhase -= 1.0;
            const uint32_t group = std::clamp(settings.groupSize, 1u, 32u);
            if (++head.cycleOffset < group) continue;
            head.cycleOffset = 0u;
            // Repeat the complete multi-cycle waveset, matching the Gen
            // chopper's segment-level repeat rather than repeating every
            // constituent cycle separately.
            if (++head.repeatIndex < settings.repeats) continue;
            head.repeatIndex = 0u;
            if (settings.direction == WavesetDirection::Pendulum)
                head.pendulumForward = !head.pendulumForward;
            head.randomState = head.randomState * 1664525u + 1013904223u;
            if (settings.advance != WavesetAdvanceMode::Hold) {
                const int64_t timeStep = settings.advance
                        == WavesetAdvanceMode::Preserve
                    ? static_cast<int64_t>(settings.repeats) : 1;
                moveHead(head, static_cast<int64_t>(settings.stride)
                    * static_cast<int64_t>(group) * timeStep);
            }
        }
    }

    void moveHead(Head& head, int64_t amount) noexcept
    {
        head.groupStart = wrapUnit(static_cast<int64_t>(head.groupStart)
            + amount);
        head.cycleOffset = 0u;
        head.repeatIndex = 0u;
    }

    void updateEnvelope(Head& head) noexcept
    {
        const double milliseconds = head.playing ? 3.0 : 12.0;
        const double seconds = std::max(0.0001, milliseconds * 0.001);
        const float coefficient = static_cast<float>(1.0
            - std::exp(-1.0 / (seconds * outputSampleRate_)));
        const float target = head.playing ? 1.0f : 0.0f;
        head.envelope += coefficient * (target - head.envelope);
        if (!head.playing && head.envelope < 1.0e-6f) head.envelope = 0.0f;
    }

    void applyEvent(const WavesetRenderEvent& event,
        const WavesetSettings& settings) noexcept
    {
        const auto play = [](Head& head) { head.playing = true; };
        const auto pause = [](Head& head) { head.playing = false; };
        const auto stop = [&](Head& head, std::size_t deck) {
            head.playing = false;
            head.envelope = 0.0f;
            setHeadPosition(head, settings.positions[deck]);
        };
        const auto restart = [&](Head& head, std::size_t deck) {
            setHeadPosition(head, settings.positions[deck]);
            head.playing = true;
        };
        switch (event.kind) {
        case WavesetEventKind::PlayDeck:
            if (event.deck < heads.size()) play(heads[event.deck]); break;
        case WavesetEventKind::PauseDeck:
            if (event.deck < heads.size()) pause(heads[event.deck]); break;
        case WavesetEventKind::StopDeck:
            if (event.deck < heads.size()) stop(heads[event.deck], event.deck);
            break;
        case WavesetEventKind::RestartDeck:
            if (event.deck < heads.size())
                restart(heads[event.deck], event.deck);
            break;
        case WavesetEventKind::PlayBoth:
            for (auto& head : heads) play(head); break;
        case WavesetEventKind::PauseBoth:
            for (auto& head : heads) pause(head); break;
        case WavesetEventKind::StopBoth:
            for (std::size_t deck = 0u; deck < heads.size(); ++deck)
                stop(heads[deck], deck);
            break;
        case WavesetEventKind::RestartBoth:
            for (std::size_t deck = 0u; deck < heads.size(); ++deck)
                restart(heads[deck], deck);
            break;
        case WavesetEventKind::SetPosition:
            if (event.deck < heads.size())
                setHeadPosition(heads[event.deck], event.value);
            break;
        }
    }

    void setHeadPosition(Head& head, double normalized) noexcept
    {
        normalized = std::clamp(normalized, 0.0, 1.0);
        const auto index = static_cast<std::size_t>(std::llround(normalized
            * static_cast<double>(map_->units.size() - 1u)));
        head.groupStart = std::min(index, map_->units.size() - 1u);
        head.cycleOffset = 0u;
        head.repeatIndex = 0u;
        head.requestedPosition = normalized;
    }

    void applyPositionSettings(const WavesetSettings& settings) noexcept
    {
        for (std::size_t index = 0u; index < heads.size(); ++index) {
            const double requested = std::clamp(settings.positions[index],
                0.0, 1.0);
            if (!settingsInitialized_
                || std::abs(requested - heads[index].requestedPosition)
                    > 1.0e-9)
                setHeadPosition(heads[index], requested);
        }
        settingsInitialized_ = true;
    }

    static void crossfadeGains(WavesetCrossfadeCurve curve,
        double crossfader, float& gainA, float& gainB) noexcept
    {
        constexpr double halfPi = 1.5707963267948966192313216916398;
        const double t = std::clamp((crossfader + 1.0) * 0.5, 0.0, 1.0);
        if (curve == WavesetCrossfadeCurve::Cut) {
            const double shaped = std::clamp((t - 0.48) / 0.04,
                0.0, 1.0);
            const double smooth = shaped * shaped * (3.0 - 2.0 * shaped);
            gainA = static_cast<float>(1.0 - smooth);
            gainB = static_cast<float>(smooth);
        } else if (curve == WavesetCrossfadeCurve::Sharp) {
            gainA = static_cast<float>(std::clamp(
                (0.60 - t) / 0.20, 0.0, 1.0));
            gainB = static_cast<float>(std::clamp(
                (t - 0.40) / 0.20, 0.0, 1.0));
        } else {
            gainA = static_cast<float>(std::cos(t * halfPi));
            gainB = static_cast<float>(std::sin(t * halfPi));
        }
    }

    bool prepared_ = false;
    bool settingsInitialized_ = false;
    double outputSampleRate_ = 48000.0;
    const WavesetMap* map_ = nullptr;
    std::array<Head, kWavesetHeadCount> heads {};
    uint32_t lastGroupSize_ = 1u;
    float outputPeak_ = 0.0f;
    double crossfadePosition_ = -1.0;
    bool crossfadeInitialized_ = false;
};

} // namespace s3g::sample
