#pragma once

#include "s3g_break_bus.h"
#include "s3g_sample_asset.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <vector>

namespace s3g::breakbeat {

constexpr std::size_t kMaximumSampleSlots = 4u;
constexpr std::size_t kMaximumSlicesPerSlot = 128u;
constexpr std::size_t kMidiNoteCount = 128u;

inline constexpr std::size_t maximumSlicesForStartNote(
    uint8_t startNote) noexcept
{
    return startNote < kMidiNoteCount
        ? kMidiNoteCount - static_cast<std::size_t>(startNote) : 0u;
}
constexpr std::size_t kMaximumVoices = 32u;
constexpr std::size_t kMaximumAudioChannels
    = s3g::sample::kMaximumAudioChannels;
constexpr uint8_t kUnmappedIndex = 0xffu;

using SampleAsset = s3g::sample::SampleAsset;

enum class LaunchMode : uint8_t {
    OneShot = 0u,
    Gate,
    Thru,
    Loop,
    PingPong,
};

enum class Interpolation : uint8_t {
    Nearest = 0u,
    Linear,
};

constexpr std::size_t kInsertSlotsPerStrip = 2u;
constexpr std::size_t kInsertParameterCount = 4u;

enum class InsertType : uint8_t {
    Off = 0u,
    Filter,
    Degrade,
    Transient,
    Resonator,
    Erosion,
    Shifter,
    Wavefolder,
    Repeater,
    TimeMangler,
};

enum class FilterMode : uint8_t {
    LowPass = 0u,
    BandPass,
    HighPass,
    Notch,
};

// Insert values are normalized so the document/runtime snapshot stays small
// and stable while each processor owns its musically useful mapping. Mode is
// currently used by FILTER and retained for future stepped device options.
struct InsertSettings {
    InsertType type = InsertType::Off;
    FilterMode mode = FilterMode::LowPass;
    uint8_t variant = 0u;
    std::array<float, kInsertParameterCount> values {{
        0.5f, 0.5f, 0.0f, 1.0f,
    }};
    bool bypassed = false;

    bool valid() const noexcept
    {
        return static_cast<uint8_t>(type)
                <= static_cast<uint8_t>(InsertType::TimeMangler)
            && static_cast<uint8_t>(mode)
                <= static_cast<uint8_t>(FilterMode::Notch)
            && variant <= 2u
            && std::all_of(values.begin(), values.end(), [](float value) {
                return std::isfinite(value) && value >= 0.0f
                    && value <= 1.0f;
            });
    }
};

inline InsertSettings defaultInsertSettings(InsertType type) noexcept
{
    InsertSettings settings;
    settings.type = type;
    switch (type) {
    case InsertType::Filter:
        // 1 kHz low-pass, moderate resonance, clean drive, fully wet.
        settings.values = {{ 0.55f, 0.22f, 0.0f, 1.0f }};
        break;
    case InsertType::Degrade:
        // Audible but usable rate/bit reduction with no timing jitter.
        settings.values = {{ 0.30f, 0.58f, 0.0f, 1.0f }};
        break;
    case InsertType::Transient:
        // More attack, less sustain, gate disabled, fully wet.
        settings.values = {{ 0.66f, 0.38f, 0.0f, 1.0f }};
        break;
    case InsertType::Resonator:
        // Low-mid tuning, controlled feedback and a parallel wet balance.
        settings.values = {{ 0.38f, 0.52f, 0.48f, 0.48f }};
        break;
    case InsertType::Erosion:
        // Sine-modulated short delay with modest depth and regeneration.
        settings.values = {{ 0.45f, 0.35f, 0.20f, 0.60f }};
        break;
    case InsertType::Shifter:
        // A subtle upward frequency shift with restrained regeneration.
        settings.values = {{ 0.55f, 0.15f, 0.0f, 0.65f }};
        break;
    case InsertType::Wavefolder:
        // Two folds, centered symmetry, smooth triangle shape, parallel mix.
        settings.values = {{ 0.30f, 0.50f, 0.20f, 0.65f }};
        break;
    case InsertType::Repeater:
        // 64 ms transient capture, four repeats, slight pitch decay, wet.
        settings.values = {{ 0.43f, 0.20f, 0.12f, 1.0f }};
        break;
    case InsertType::TimeMangler:
        // 125 ms transient capture, original pitch, one-second freeze decay.
        settings.values = {{ 0.57f, 0.50f, 0.43f, 1.0f }};
        break;
    case InsertType::Off:
        settings.values = {{ 0.5f, 0.5f, 0.0f, 1.0f }};
        break;
    }
    return settings;
}

struct Envelope {
    // One envelope belongs to each break slot. A, D, and R are evaluated as
    // fractions of the triggered slice's rendered duration, keeping the break
    // coherent while preserving articulation across different slice lengths.
    float attackProportion = 0.001f;
    float decayProportion = 0.0f;
    float sustain = 1.0f;
    float releaseProportion = 0.005f;

    bool valid() const noexcept
    {
        return std::isfinite(attackProportion)
            && attackProportion >= 0.0f && attackProportion <= 1.0f
            && std::isfinite(decayProportion)
            && decayProportion >= 0.0f && decayProportion <= 1.0f
            && std::isfinite(sustain) && sustain >= 0.0f && sustain <= 1.0f
            && std::isfinite(releaseProportion)
            && releaseProportion >= 0.0f && releaseProportion <= 1.0f
            && attackProportion + decayProportion + releaseProportion
                <= 1.000001f;
    }
};

struct SamplePeak {
    float minimum = 0.0f;
    float maximum = 0.0f;
};

struct SampleTransient {
    uint32_t frame = 0u;
    float strength = 0.0f;
};

struct SampleAnalysis {
    uint32_t sourceFrameCount = 0u;
    uint32_t peakStrideFrames = 1u;
    std::vector<SamplePeak> peaks;
    std::vector<SampleTransient> transients;

    bool validFor(const SampleAsset& asset) const noexcept
    {
        if (!asset.valid() || sourceFrameCount != asset.frameCount()
            || peakStrideFrames == 0u || peaks.empty()) return false;
        const std::size_t expected = static_cast<std::size_t>(
            (static_cast<uint64_t>(sourceFrameCount) + peakStrideFrames - 1u)
                / peakStrideFrames);
        if (peaks.size() != expected) return false;
        for (const auto& peak : peaks) {
            if (!std::isfinite(peak.minimum)
                || !std::isfinite(peak.maximum)
                || peak.minimum > peak.maximum) return false;
        }
        uint32_t previous = 0u;
        bool first = true;
        for (const auto& transient : transients) {
            if (transient.frame >= sourceFrameCount
                || !std::isfinite(transient.strength)
                || transient.strength < 0.0f
                || (!first && transient.frame <= previous)) return false;
            previous = transient.frame;
            first = false;
        }
        return true;
    }
};

struct AnalysisSettings {
    std::size_t maximumPeakCount = 4096u;
    float minimumTransientLevel = 0.06f;
    float transientSensitivity = 2.25f;
    double minimumTransientSpacingSeconds = 0.025;
    double transientLookaheadSeconds = 0.002;
};

struct Slice {
    uint32_t startFrame = 0u;
    uint32_t endFrame = 0u; // exclusive
    uint32_t loopStartFrame = 0u;
    uint32_t loopEndFrame = 0u; // exclusive; zero selects the slice end
    float gain = 1.0f;
    float pan = 0.0f;
    float transposeSemitones = 0.0f;
    float fineTuneCents = 0.0f;
    uint8_t chokeGroup = 0u;
    LaunchMode launchMode = LaunchMode::OneShot;
    bool reverse = false;

    bool validFor(const SampleAsset& asset) const noexcept
    {
        if (startFrame >= endFrame || endFrame > asset.frameCount()
            || !std::isfinite(gain) || gain < 0.0f || gain > 4.0f
            || !std::isfinite(pan) || pan < -1.0f || pan > 1.0f
            || !std::isfinite(transposeSemitones)
            || transposeSemitones < -96.0f || transposeSemitones > 96.0f
            || !std::isfinite(fineTuneCents)
            || fineTuneCents < -100.0f || fineTuneCents > 100.0f
            || chokeGroup > 16u) return false;
        if (loopStartFrame == 0u && loopEndFrame == 0u) return true;
        return loopStartFrame >= startFrame && loopStartFrame < loopEndFrame
            && loopEndFrame <= endFrame;
    }
};

inline float multichannelMagnitude(const SampleAsset& asset,
    uint32_t frame) noexcept
{
    float magnitude = 0.0f;
    for (std::size_t channel = 0u; channel < asset.channelCount; ++channel)
        magnitude = std::max(magnitude,
            std::abs(asset.channels[channel][frame]));
    return magnitude;
}

inline SampleAnalysis analyzeSample(const SampleAsset& asset,
    const AnalysisSettings& settings = {})
{
    SampleAnalysis result;
    if (!asset.valid() || settings.maximumPeakCount == 0u
        || !std::isfinite(settings.minimumTransientLevel)
        || settings.minimumTransientLevel < 0.0f
        || !std::isfinite(settings.transientSensitivity)
        || settings.transientSensitivity < 1.0f
        || !std::isfinite(settings.minimumTransientSpacingSeconds)
        || settings.minimumTransientSpacingSeconds < 0.0
        || !std::isfinite(settings.transientLookaheadSeconds)
        || settings.transientLookaheadSeconds < 0.0) return result;

    const uint32_t frames = asset.frameCount();
    result.sourceFrameCount = frames;
    const uint64_t requestedPeaks = std::min<uint64_t>(
        settings.maximumPeakCount, frames);
    result.peakStrideFrames = static_cast<uint32_t>(std::max<uint64_t>(1u,
        (static_cast<uint64_t>(frames) + requestedPeaks - 1u)
            / requestedPeaks));
    const std::size_t peakCount = static_cast<std::size_t>(
        (static_cast<uint64_t>(frames) + result.peakStrideFrames - 1u)
            / result.peakStrideFrames);
    result.peaks.resize(peakCount);
    for (std::size_t index = 0u; index < peakCount; ++index) {
        const uint32_t start = static_cast<uint32_t>(
            static_cast<uint64_t>(index) * result.peakStrideFrames);
        const uint32_t end = static_cast<uint32_t>(std::min<uint64_t>(frames,
            static_cast<uint64_t>(start) + result.peakStrideFrames));
        float minimum = std::numeric_limits<float>::max();
        float maximum = std::numeric_limits<float>::lowest();
        for (uint32_t frame = start; frame < end; ++frame) {
            for (std::size_t channel = 0u;
                 channel < asset.channelCount; ++channel) {
                minimum = std::min(minimum, asset.channels[channel][frame]);
                maximum = std::max(maximum, asset.channels[channel][frame]);
            }
        }
        result.peaks[index] = { minimum, maximum };
    }

    const auto durationFrames = [&](double seconds, uint32_t minimum) {
        return static_cast<uint32_t>(std::clamp<double>(
            std::round(asset.sampleRate * seconds), minimum,
            std::numeric_limits<uint32_t>::max()));
    };
    const uint32_t spacing = durationFrames(
        settings.minimumTransientSpacingSeconds, 1u);
    const uint32_t lookahead = durationFrames(
        settings.transientLookaheadSeconds, 0u);
    const double attackCoefficient = 1.0 - std::exp(
        -1.0 / std::max(1.0, asset.sampleRate * 0.001));
    const double releaseCoefficient = 1.0 - std::exp(
        -1.0 / std::max(1.0, asset.sampleRate * 0.050));
    float envelope = 0.0f;
    float previous = 0.0f;
    uint64_t nextAllowed = 0u;
    for (uint32_t frame = 0u; frame < frames; ++frame) {
        const float magnitude = multichannelMagnitude(asset, frame);
        const float threshold = std::max(settings.minimumTransientLevel,
            envelope * settings.transientSensitivity);
        if (static_cast<uint64_t>(frame) >= nextAllowed
            && magnitude >= threshold && magnitude > previous) {
            const uint32_t searchEnd = static_cast<uint32_t>(
                std::min<uint64_t>(frames, static_cast<uint64_t>(frame)
                    + lookahead + 1u));
            uint32_t peakFrame = frame;
            float peakMagnitude = magnitude;
            for (uint32_t candidate = frame + 1u; candidate < searchEnd;
                 ++candidate) {
                const float candidateMagnitude = multichannelMagnitude(
                    asset, candidate);
                if (candidateMagnitude > peakMagnitude) {
                    peakMagnitude = candidateMagnitude;
                    peakFrame = candidate;
                }
            }
            result.transients.push_back({ peakFrame,
                std::min(1000000.0f, peakMagnitude
                    / std::max(0.000001f, threshold)) });
            envelope = std::max(envelope, peakMagnitude);
            nextAllowed = static_cast<uint64_t>(peakFrame) + spacing;
        }
        const double coefficient = magnitude > envelope
            ? attackCoefficient : releaseCoefficient;
        envelope += static_cast<float>((magnitude - envelope) * coefficient);
        previous = magnitude;
    }
    return result;
}

inline uint32_t nearestZeroFrame(const SampleAsset& asset, uint32_t frame,
    uint32_t searchRadiusFrames) noexcept
{
    const uint32_t frames = asset.frameCount();
    if (frames == 0u) return 0u;
    frame = std::min(frame, frames - 1u);
    // Markers must remain one shared frame across every channel. Use the
    // strongest channel at the requested marker as the zero-cross reference;
    // averaging can invent false crossings through inter-channel cancellation.
    std::size_t referenceChannel = 0u;
    float referenceMagnitude = 0.0f;
    for (std::size_t channel = 0u; channel < asset.channelCount; ++channel) {
        const float magnitude = std::abs(asset.channels[channel][frame]);
        if (magnitude > referenceMagnitude) {
            referenceMagnitude = magnitude;
            referenceChannel = channel;
        }
    }
    const auto reference = [&](uint32_t at) {
        return asset.channels[referenceChannel][at];
    };
    const auto crossing = [&](uint32_t at) {
        if (at == 0u) return std::abs(reference(at));
        const float previous = reference(at - 1u);
        const float current = reference(at);
        if ((previous <= 0.0f && current >= 0.0f)
            || (previous >= 0.0f && current <= 0.0f))
            return std::abs(previous) + std::abs(current);
        return std::numeric_limits<float>::infinity();
    };
    uint32_t best = frame;
    float bestScore = crossing(frame);
    uint32_t bestDistance = 0u;
    for (uint32_t distance = 1u; distance <= searchRadiusFrames; ++distance) {
        for (int direction : { -1, 1 }) {
            const int64_t candidate = static_cast<int64_t>(frame)
                + static_cast<int64_t>(direction) * distance;
            if (candidate < 0 || candidate >= frames) continue;
            const float score = crossing(static_cast<uint32_t>(candidate));
            if (score < bestScore || (score == bestScore
                && (bestDistance == 0u || distance < bestDistance))) {
                best = static_cast<uint32_t>(candidate);
                bestScore = score;
                bestDistance = distance;
            }
        }
        if (std::isfinite(bestScore) && bestDistance != 0u
            && distance > bestDistance) break;
    }
    return best;
}

inline std::vector<Slice> makeEqualSlices(const SampleAsset& asset,
    std::size_t count)
{
    std::vector<Slice> result;
    if (!asset.valid() || count == 0u) return result;
    const uint32_t frames = asset.frameCount();
    count = std::min<std::size_t>({ count, kMaximumSlicesPerSlot, frames });
    result.reserve(count);
    for (std::size_t index = 0u; index < count; ++index) {
        const uint32_t start = static_cast<uint32_t>(
            static_cast<uint64_t>(frames) * index / count);
        const uint32_t end = static_cast<uint32_t>(
            static_cast<uint64_t>(frames) * (index + 1u) / count);
        result.push_back({ start, std::max(start + 1u, end) });
    }
    return result;
}

inline std::vector<Slice> makeTransientSlices(const SampleAsset& asset,
    const SampleAnalysis& analysis,
    std::size_t maximumSliceCount = kMaximumSlicesPerSlot,
    uint32_t zeroCrossingRadiusFrames = 0u,
    uint32_t preTransientMicroseconds = 0u)
{
    std::vector<Slice> result;
    if (!analysis.validFor(asset) || maximumSliceCount == 0u) return result;
    maximumSliceCount = std::min(maximumSliceCount,
        kMaximumSlicesPerSlot);
    const uint32_t preTransientFrames = static_cast<uint32_t>(
        std::clamp<double>(std::round(asset.sampleRate
                * static_cast<double>(preTransientMicroseconds) * 1.0e-6),
            0.0, std::numeric_limits<uint32_t>::max()));
    std::vector<uint32_t> starts { 0u };
    starts.reserve(std::min(maximumSliceCount,
        analysis.transients.size() + 1u));
    for (const auto& transient : analysis.transients) {
        if (starts.size() >= maximumSliceCount) break;
        uint32_t marker = transient.frame > preTransientFrames
            ? transient.frame - preTransientFrames : 0u;
        if (zeroCrossingRadiusFrames != 0u)
            marker = nearestZeroFrame(asset, marker,
                zeroCrossingRadiusFrames);
        if (marker > 0u && marker < asset.frameCount())
            starts.push_back(marker);
    }
    std::sort(starts.begin() + 1, starts.end());
    starts.erase(std::unique(starts.begin(), starts.end()), starts.end());
    result.reserve(starts.size());
    for (std::size_t index = 0u; index < starts.size(); ++index) {
        const uint32_t end = index + 1u < starts.size()
            ? starts[index + 1u] : asset.frameCount();
        if (starts[index] < end)
            result.push_back({ starts[index], end });
    }
    return result;
}

inline bool addSliceMarker(Slice* slices, std::size_t& count,
    std::size_t capacity, uint32_t frame) noexcept
{
    if (!slices || count == 0u || count >= capacity) return false;
    std::size_t selected = count;
    for (std::size_t index = 0u; index < count; ++index) {
        if (frame > slices[index].startFrame
            && frame < slices[index].endFrame) {
            selected = index;
            break;
        }
    }
    if (selected == count) return false;
    for (std::size_t index = count; index > selected + 1u; --index)
        slices[index] = slices[index - 1u];
    Slice right = slices[selected];
    right.startFrame = frame;
    slices[selected].endFrame = frame;
    slices[selected + 1u] = right;
    ++count;
    return true;
}

inline bool moveSliceMarker(Slice* slices, std::size_t count,
    std::size_t markerIndex, uint32_t frame) noexcept
{
    if (!slices || markerIndex == 0u || markerIndex >= count) return false;
    auto& left = slices[markerIndex - 1u];
    auto& right = slices[markerIndex];
    if (frame <= left.startFrame || frame >= right.endFrame) return false;
    left.endFrame = frame;
    right.startFrame = frame;
    return true;
}

inline bool deleteSliceMarker(Slice* slices, std::size_t& count,
    std::size_t markerIndex) noexcept
{
    if (!slices || markerIndex == 0u || markerIndex >= count) return false;
    slices[markerIndex - 1u].endFrame = slices[markerIndex].endFrame;
    for (std::size_t index = markerIndex; index + 1u < count; ++index)
        slices[index] = slices[index + 1u];
    --count;
    slices[count] = {};
    return true;
}

struct SampleSlot {
    std::shared_ptr<const SampleAsset> asset;
    std::array<Slice, kMaximumSlicesPerSlot> slices {};
    uint16_t sliceCount = 0u;
    uint16_t mappedSliceCount = 0u;
    Envelope envelope {};
    float mixerGain = 1.0f;
    float mixerPan = 0.0f;
    float mixerLowEqDb = 0.0f;
    float mixerMidEqDb = 0.0f;
    float mixerHighEqDb = 0.0f;
    float mixerMidFrequencyHz = 900.0f;
    float mixerAuxSend = 0.0f;
    std::array<InsertSettings, kInsertSlotsPerStrip> inserts {};
    uint8_t rootNote = 48u;
    uint8_t mappedRootNote = 48u;
    uint8_t midiChannel = 0u; // zero is omni; 1-16 are explicit MIDI channels
    bool muted = false;
    bool solo = false;

    bool valid() const noexcept
    {
        if (!envelope.valid() || !std::isfinite(mixerGain)
            || mixerGain < 0.0f || mixerGain > 2.0f
            || !std::isfinite(mixerPan) || mixerPan < -1.0f
            || mixerPan > 1.0f
            || !std::isfinite(mixerLowEqDb) || mixerLowEqDb < -12.0f
            || mixerLowEqDb > 12.0f
            || !std::isfinite(mixerMidEqDb) || mixerMidEqDb < -12.0f
            || mixerMidEqDb > 12.0f
            || !std::isfinite(mixerHighEqDb) || mixerHighEqDb < -12.0f
            || mixerHighEqDb > 12.0f
            || !std::isfinite(mixerMidFrequencyHz)
            || mixerMidFrequencyHz < 120.0f
            || mixerMidFrequencyHz > 8000.0f
            || !std::isfinite(mixerAuxSend) || mixerAuxSend < 0.0f
            || mixerAuxSend > 1.0f || rootNote >= kMidiNoteCount
            || mappedRootNote >= kMidiNoteCount || midiChannel > 16u
            || sliceCount > slices.size()
            || sliceCount > maximumSlicesForStartNote(rootNote)
            || mappedSliceCount > sliceCount
            || static_cast<std::size_t>(mappedRootNote) + mappedSliceCount
                > kMidiNoteCount) return false;
        if (!std::all_of(inserts.begin(), inserts.end(),
                [](const InsertSettings& insert) {
                    return insert.valid();
                })) return false;
        if (!asset) return sliceCount == 0u && mappedSliceCount == 0u;
        if (!asset->valid() || sliceCount == 0u) return false;
        for (std::size_t index = 0u; index < sliceCount; ++index) {
            if (!slices[index].validFor(*asset)) return false;
        }
        return true;
    }
};

struct BankSnapshot {
    std::array<SampleSlot, kMaximumSampleSlots> slots {};
    Interpolation interpolation = Interpolation::Linear;
    float outputGain = 1.0f;
    bool auxEnabled = false;
    float auxPress = 0.42f;
    float auxSnap = 0.18f;
    float auxRecovery = 0.34f;
    float auxSaturation = 0.20f;
    float auxBite = 0.08f;
    float auxClip = 0.0f;
    float auxTilt = 0.0f;
    float auxReturnDb = -9.0f;
    s3g::BreakBusLinkMode auxLinkMode = s3g::BreakBusLinkMode::All;
    bool auxFieldSafe = false;

    bool valid() const noexcept
    {
        if (!std::isfinite(outputGain) || outputGain < 0.0f
            || outputGain > 4.0f
            || !std::isfinite(auxPress) || auxPress < 0.0f
            || auxPress > 1.0f
            || !std::isfinite(auxSnap) || auxSnap < -1.0f
            || auxSnap > 1.0f
            || !std::isfinite(auxRecovery) || auxRecovery < 0.0f
            || auxRecovery > 1.0f
            || !std::isfinite(auxSaturation) || auxSaturation < 0.0f
            || auxSaturation > 1.0f
            || !std::isfinite(auxBite) || auxBite < 0.0f
            || auxBite > 1.0f
            || !std::isfinite(auxClip) || auxClip < 0.0f
            || auxClip > 1.0f
            || !std::isfinite(auxTilt) || auxTilt < -1.0f
            || auxTilt > 1.0f
            || !std::isfinite(auxReturnDb) || auxReturnDb < -60.0f
            || auxReturnDb > 12.0f
            || static_cast<uint8_t>(auxLinkMode)
                > static_cast<uint8_t>(s3g::BreakBusLinkMode::Free))
            return false;
        for (const auto& slot : slots) {
            if (!slot.valid()) return false;
        }
        return true;
    }
};

// Runtime mixer state is published independently from sample/slice state.
// Keeping this POD snapshot separate lets a control edit land at a process
// boundary without replacing sample assets, ending voices, or clearing the
// post-playback filter and bus histories.
struct MixerStripSnapshot {
    float gain = 1.0f;
    float pan = 0.0f;
    float lowEqDb = 0.0f;
    float midEqDb = 0.0f;
    float highEqDb = 0.0f;
    float midFrequencyHz = 900.0f;
    float auxSend = 0.0f;
    std::array<InsertSettings, kInsertSlotsPerStrip> inserts {};
    bool muted = false;
    bool solo = false;

    bool valid() const noexcept
    {
        return std::isfinite(gain) && gain >= 0.0f && gain <= 2.0f
            && std::isfinite(pan) && pan >= -1.0f && pan <= 1.0f
            && std::isfinite(lowEqDb) && lowEqDb >= -12.0f
            && lowEqDb <= 12.0f
            && std::isfinite(midEqDb) && midEqDb >= -12.0f
            && midEqDb <= 12.0f
            && std::isfinite(highEqDb) && highEqDb >= -12.0f
            && highEqDb <= 12.0f
            && std::isfinite(midFrequencyHz) && midFrequencyHz >= 120.0f
            && midFrequencyHz <= 8000.0f
            && std::isfinite(auxSend) && auxSend >= 0.0f
            && auxSend <= 1.0f
            && std::all_of(inserts.begin(), inserts.end(),
                [](const InsertSettings& insert) {
                    return insert.valid();
                });
    }
};

struct MixerSnapshot {
    std::array<MixerStripSnapshot, kMaximumSampleSlots> strips {};
    float outputGain = 1.0f;
    bool auxEnabled = false;
    float auxPress = 0.42f;
    float auxSnap = 0.18f;
    float auxRecovery = 0.34f;
    float auxSaturation = 0.20f;
    float auxBite = 0.08f;
    float auxClip = 0.0f;
    float auxTilt = 0.0f;
    float auxReturnDb = -9.0f;
    s3g::BreakBusLinkMode auxLinkMode = s3g::BreakBusLinkMode::All;
    bool auxFieldSafe = false;

    bool valid() const noexcept
    {
        if (!std::isfinite(outputGain) || outputGain < 0.0f
            || outputGain > 4.0f
            || !std::isfinite(auxPress) || auxPress < 0.0f
            || auxPress > 1.0f
            || !std::isfinite(auxSnap) || auxSnap < -1.0f
            || auxSnap > 1.0f
            || !std::isfinite(auxRecovery) || auxRecovery < 0.0f
            || auxRecovery > 1.0f
            || !std::isfinite(auxSaturation) || auxSaturation < 0.0f
            || auxSaturation > 1.0f
            || !std::isfinite(auxBite) || auxBite < 0.0f
            || auxBite > 1.0f
            || !std::isfinite(auxClip) || auxClip < 0.0f
            || auxClip > 1.0f
            || !std::isfinite(auxTilt) || auxTilt < -1.0f
            || auxTilt > 1.0f
            || !std::isfinite(auxReturnDb) || auxReturnDb < -60.0f
            || auxReturnDb > 12.0f
            || static_cast<uint8_t>(auxLinkMode)
                > static_cast<uint8_t>(s3g::BreakBusLinkMode::Free))
            return false;
        return std::all_of(strips.begin(), strips.end(),
            [](const MixerStripSnapshot& strip) { return strip.valid(); });
    }
};

inline MixerSnapshot mixerSnapshotFromBank(
    const BankSnapshot& bank) noexcept
{
    MixerSnapshot mixer;
    mixer.outputGain = bank.outputGain;
    mixer.auxEnabled = bank.auxEnabled;
    mixer.auxPress = bank.auxPress;
    mixer.auxSnap = bank.auxSnap;
    mixer.auxRecovery = bank.auxRecovery;
    mixer.auxSaturation = bank.auxSaturation;
    mixer.auxBite = bank.auxBite;
    mixer.auxClip = bank.auxClip;
    mixer.auxTilt = bank.auxTilt;
    mixer.auxReturnDb = bank.auxReturnDb;
    mixer.auxLinkMode = bank.auxLinkMode;
    mixer.auxFieldSafe = bank.auxFieldSafe;
    for (std::size_t index = 0u; index < mixer.strips.size(); ++index) {
        const auto& source = bank.slots[index];
        auto& destination = mixer.strips[index];
        destination.gain = source.mixerGain;
        destination.pan = source.mixerPan;
        destination.lowEqDb = source.mixerLowEqDb;
        destination.midEqDb = source.mixerMidEqDb;
        destination.highEqDb = source.mixerHighEqDb;
        destination.midFrequencyHz = source.mixerMidFrequencyHz;
        destination.auxSend = source.mixerAuxSend;
        destination.inserts = source.inserts;
        destination.muted = source.muted;
        destination.solo = source.solo;
    }
    return mixer;
}

inline void initializeEmptyBank(BankSnapshot& bank) noexcept
{
    bank = {};
    for (std::size_t index = 0u; index < bank.slots.size(); ++index) {
        bank.slots[index].rootNote = 48u;
        bank.slots[index].mappedRootNote = bank.slots[index].rootNote;
        bank.slots[index].midiChannel = static_cast<uint8_t>(index + 1u);
    }
}

inline void clearMappingsForSlot(BankSnapshot& bank,
    uint8_t slotIndex) noexcept
{
    if (slotIndex >= bank.slots.size()) return;
    bank.slots[slotIndex].mappedSliceCount = 0u;
}

// Commits the slot's current slice table to consecutive MIDI notes. Mapping is
// per break, so separate MIDI channels may intentionally reuse note ranges.
inline bool mapSlotConsecutively(BankSnapshot& bank, uint8_t slotIndex,
    uint8_t baseNote, bool replace = false) noexcept
{
    (void)replace;
    if (slotIndex >= bank.slots.size()) return false;
    auto& slot = bank.slots[slotIndex];
    if (!slot.asset || slot.sliceCount == 0u
        || static_cast<std::size_t>(baseNote) + slot.sliceCount
            > kMidiNoteCount) return false;
    slot.rootNote = baseNote;
    slot.mappedRootNote = baseNote;
    slot.mappedSliceCount = slot.sliceCount;
    return true;
}

// Auto Map always retains the user-selected starting note. Slice creation is
// responsible for respecting maximumSlicesForStartNote(), so mapping never
// silently moves the keyboard range.
inline bool autoMapSlotConsecutively(BankSnapshot& bank, uint8_t slotIndex,
    bool replace = true) noexcept
{
    if (slotIndex >= bank.slots.size()) return false;
    const auto& slot = bank.slots[slotIndex];
    if (!slot.asset || slot.sliceCount == 0u
        || slot.sliceCount > maximumSlicesForStartNote(slot.rootNote))
        return false;
    return mapSlotConsecutively(bank, slotIndex, slot.rootNote, replace);
}

enum class EventKind : uint8_t {
    NoteOn = 0u,
    NoteOff,
    Choke,
};

struct RenderEvent {
    uint32_t frameOffset = 0u;
    EventKind kind = EventKind::NoteOn;
    uint64_t noteId = 0u;
    uint8_t key = 0u;
    uint8_t chokeGroup = 0u; // zero chokes every voice for Choke events
    float velocity = 1.0f;
    uint8_t midiChannel = 0u; // zero is unspecified/omni; 1-16 are explicit
};

class SlicerEngine {
public:
    bool prepare(double sampleRate,
        uint32_t maximumOutputChannels = kMaximumAudioChannels) noexcept
    {
        if (!(sampleRate > 0.0) || !std::isfinite(sampleRate)
            || maximumOutputChannels == 0u
            || maximumOutputChannels > kMaximumAudioChannels) return false;
        sampleRate_ = sampleRate;
        constexpr std::size_t insertStateCount = kMaximumSampleSlots
            * kInsertSlotsPerStrip;
        const std::size_t temporalSamples = insertStateCount
            * maximumOutputChannels * kTemporalBufferFrames;
        temporalBuffer_.reset(new (std::nothrow) float[temporalSamples]);
        if (!temporalBuffer_) return false;
        std::fill_n(temporalBuffer_.get(), temporalSamples, 0.0f);
        for (std::size_t slot = 0u; slot < insertStates_.size(); ++slot) {
            for (std::size_t insert = 0u;
                 insert < insertStates_[slot].size(); ++insert) {
                const std::size_t stateIndex = slot * kInsertSlotsPerStrip
                    + insert;
                insertStates_[slot][insert].temporalBuffer
                    = temporalBuffer_.get() + stateIndex
                        * maximumOutputChannels * kTemporalBufferFrames;
            }
        }
        const float sr = static_cast<float>(sampleRate_);
        lowCoefficient_ = frequencyCoefficient(170.0f, sr);
        highCoefficient_ = frequencyCoefficient(4200.0f, sr);
        temporalFastReleaseCoefficient_ = timeCoefficient(12.0f);
        temporalSlowAttackCoefficient_ = timeCoefficient(22.0f);
        temporalSlowReleaseCoefficient_ = timeCoefficient(140.0f);
        temporalParameterCoefficient_ = timeCoefficient(20.0f);
        temporalTransitionFrames_ = std::max<uint32_t>(2u,
            static_cast<uint32_t>(std::lround(sampleRate_ * 0.005)));
        shifterGovernorAttackCoefficient_ = timeCoefficient(1.5f);
        shifterGovernorReleaseCoefficient_ = timeCoefficient(140.0f);
        shifterGovernorRecoveryCoefficient_ = timeCoefficient(180.0f);
        shifterGovernorIntervalFrames_ = std::max<uint32_t>(2u,
            static_cast<uint32_t>(std::lround(sampleRate_ * 0.75)));
        shifterGovernorHoldFrames_ = std::max<uint32_t>(2u,
            static_cast<uint32_t>(std::lround(sampleRate_ * 0.030)));
        if (!auxProcessor_.prepare(sampleRate_)) return false;
        updateAuxProcessorParams();
        naturalFadeFrames_ = std::max<uint32_t>(2u,
            static_cast<uint32_t>(std::lround(sampleRate_ * 0.002)));
        chokeReleaseFrames_ = naturalFadeFrames_;
        prepared_ = true;
        reset();
        return true;
    }

    void unprepare() noexcept
    {
        prepared_ = false;
        bank_ = nullptr;
        mixer_ = nullptr;
        reset();
        for (auto& slot : insertStates_)
            for (auto& insert : slot) insert.temporalBuffer = nullptr;
        temporalBuffer_.reset();
    }

    // Convenience setter for non-realtime users and tests. Hosted publication
    // uses the independent prepared bank/mixer setters below.
    bool setBank(const BankSnapshot* bank) noexcept
    {
        if (bank && !bank->valid()) return false;
        bank_ = bank;
        ownedMixer_ = bank ? mixerSnapshotFromBank(*bank) : MixerSnapshot {};
        mixer_ = &ownedMixer_;
        updateAuxProcessorParams();
        reset();
        return true;
    }

    // Audio-thread adoption for a snapshot that was fully validated before
    // publication. This is constant-time and never scans sample memory.
    void setPreparedBank(const BankSnapshot* bank) noexcept
    {
        bank_ = bank;
    }

    bool setMixer(const MixerSnapshot* mixer) noexcept
    {
        if (mixer && !mixer->valid()) return false;
        mixer_ = mixer;
        updateAuxProcessorParams();
        return true;
    }

    void setPreparedMixer(const MixerSnapshot* mixer) noexcept
    {
        // Device topology changes clear only the affected insert history.
        // Parameter-only publication retains filter, detector, hold, and
        // resonator tails so active slices hear continuous automation.
        for (std::size_t slot = 0u; slot < insertStates_.size(); ++slot) {
            for (std::size_t insert = 0u;
                 insert < insertStates_[slot].size(); ++insert) {
                const InsertType next = mixer
                    ? mixer->strips[slot].inserts[insert].type
                    : InsertType::Off;
                auto& state = insertStates_[slot][insert];
                if (state.activeType == next) continue;
                // Large delay memory is touched only when its device is
                // selected. Other topology changes reset compact state and
                // avoid clearing unrelated buffers on the audio thread.
                resetInsertState(state, next == InsertType::Resonator,
                    next == InsertType::Erosion);
                state.activeType = next;
            }
        }
        mixer_ = mixer;
        updateAuxProcessorParams();
    }

    const BankSnapshot* bank() const noexcept { return bank_; }
    const MixerSnapshot* mixer() const noexcept { return mixer_; }

    void reset() noexcept
    {
        voices_ = {};
        mixerFilters_ = {};
        resetAllInsertStates();
        slotPeaks_.fill(0.0f);
        auxProcessor_.reset();
        auxActivity_ = 0.0f;
        auxGainReductionDb_ = 0.0f;
        auxProcessor_.beginBlock();
        voiceAge_ = 0u;
    }

    std::size_t activeVoiceCount() const noexcept
    {
        return static_cast<std::size_t>(std::count_if(
            voices_.begin(), voices_.end(),
            [](const Voice& voice) { return voice.active; }));
    }

    float slotPlayheadNormalized(uint8_t slotIndex) const noexcept
    {
        const Voice* newest = nullptr;
        for (const auto& voice : voices_) {
            if (!voice.active || !voice.asset || voice.slotIndex != slotIndex)
                continue;
            if (!newest || voice.age > newest->age) newest = &voice;
        }
        if (!newest || newest->asset->frameCount() <= 1u) return -1.0f;
        return std::clamp(static_cast<float>(newest->position
            / static_cast<double>(newest->asset->frameCount() - 1u)),
            0.0f, 1.0f);
    }

    float slotPeak(uint8_t slotIndex) const noexcept
    {
        return slotIndex < slotPeaks_.size() ? slotPeaks_[slotIndex] : 0.0f;
    }

    float auxActivity() const noexcept { return auxActivity_; }
    float auxGainReductionDb() const noexcept
    {
        return auxGainReductionDb_;
    }

    void render(const RenderEvent* events, std::size_t eventCount,
        float* const* outputs, uint32_t outputChannelCount,
        uint32_t frameCount) noexcept
    {
        if (!outputs || outputChannelCount == 0u
            || outputChannelCount > kMaximumAudioChannels) return;
        for (uint32_t channel = 0u; channel < outputChannelCount; ++channel) {
            if (!outputs[channel]) return;
            std::fill(outputs[channel], outputs[channel] + frameCount, 0.0f);
        }
        slotPeaks_.fill(0.0f);
        auxActivity_ = 0.0f;
        auxGainReductionDb_ = 0.0f;
        if (!prepared_ || !bank_ || !mixer_ || frameCount == 0u) return;
        if (!events) eventCount = 0u;

        struct StripRenderParams {
            float leftBalance = 1.0f;
            float rightBalance = 1.0f;
            float lowGain = 1.0f;
            float midGain = 1.0f;
            float highGain = 1.0f;
            float midG = 0.06f;
            float level = 1.0f;
            float auxSendSquared = 0.0f;
            std::array<InsertRenderParams, kInsertSlotsPerStrip> inserts {};
            bool stereoPan = true;
        };
        const bool anySolo = std::any_of(mixer_->strips.begin(),
            mixer_->strips.end(), [](const MixerStripSnapshot& strip) {
                return strip.solo;
            });
        std::array<StripRenderParams, kMaximumSampleSlots> stripParams {};
        for (std::size_t slotIndex = 0u;
             slotIndex < bank_->slots.size(); ++slotIndex) {
            const auto& slot = bank_->slots[slotIndex];
            const auto& strip = mixer_->strips[slotIndex];
            auto& params = stripParams[slotIndex];
            params.stereoPan = !slot.asset || slot.asset->channelCount <= 2u;
            if (params.stereoPan) {
                params.leftBalance = std::sqrt(std::max(0.0f, 1.0f
                    - std::max(0.0f, strip.pan)));
                params.rightBalance = std::sqrt(std::max(0.0f, 1.0f
                    + std::min(0.0f, strip.pan)));
            }
            params.lowGain = dbGain(strip.lowEqDb);
            params.midGain = dbGain(strip.midEqDb);
            params.highGain = dbGain(strip.highEqDb);
            params.midG = midFrequencyG(strip.midFrequencyHz);
            params.level = strip.gain
                * (!strip.muted && (!anySolo || strip.solo) ? 1.0f : 0.0f);
            params.auxSendSquared = strip.auxSend * strip.auxSend;
            for (std::size_t insert = 0u;
                 insert < params.inserts.size(); ++insert)
                params.inserts[insert] = insertRenderParams(
                    strip.inserts[insert]);
        }
        const float auxReturnGain = dbGain(mixer_->auxReturnDb);
        std::size_t eventIndex = 0u;
        for (uint32_t frame = 0u; frame < frameCount; ++frame) {
            while (eventIndex < eventCount
                && events[eventIndex].frameOffset <= frame) {
                handleEvent(events[eventIndex++]);
            }

            std::array<std::array<float, kMaximumAudioChannels>,
                kMaximumSampleSlots> slotMixed {};
            for (auto& voice : voices_) {
                if (!voice.active || !voice.asset) continue;
                const float boundary = boundaryEnvelope(voice);
                const float envelope = voice.envelopeLevel * boundary;
                const float level = voice.level * envelope;
                const uint32_t sourceChannels = voice.asset->channelCount;
                // Never emit a truncated spatial field. The host must select
                // an output configuration at least as wide as the source.
                if (sourceChannels <= outputChannelCount) {
                    for (uint32_t channel = 0u;
                         channel < outputChannelCount; ++channel) {
                        uint32_t sourceChannel = channel;
                        float channelLevel = level;
                        if (sourceChannels == 1u && outputChannelCount >= 2u
                            && channel < 2u) {
                            sourceChannel = 0u;
                            channelLevel *= channel == 0u
                                ? voice.leftPan : voice.rightPan;
                        } else if (channel >= sourceChannels) {
                            continue;
                        } else if (sourceChannels == 2u && channel < 2u) {
                            channelLevel *= channel == 0u
                                ? voice.leftPan : voice.rightPan;
                        }
                        // All channels read the exact same position before the
                        // voice clock advances once below. Slice, loop, reverse,
                        // pitch, and envelope transitions are sample locked.
                        const float rendered = interpolate(
                            voice.asset->channels[sourceChannel],
                            voice.position, voice.playStartFrame,
                            voice.playEndFrame, bank_->interpolation)
                            * channelLevel;
                        slotMixed[voice.slotIndex][channel] += rendered;
                    }
                }

                voice.position += voice.increment;
                advanceEnvelope(voice);
                advancePosition(voice);
            }

            std::array<float, kMaximumAudioChannels> mixed {};
            std::array<float, kMaximumAudioChannels> aux {};
            for (std::size_t slotIndex = 0u;
                 slotIndex < bank_->slots.size(); ++slotIndex) {
                const auto& params = stripParams[slotIndex];
                auto insertFrame = slotMixed[slotIndex];
                for (std::size_t insert = 0u;
                     insert < params.inserts.size(); ++insert)
                    processInsertFrame(insertFrame, outputChannelCount,
                        insertStates_[slotIndex][insert],
                        params.inserts[insert]);
                for (uint32_t channel = 0u; channel < outputChannelCount;
                     ++channel) {
                    float strip = processEq(insertFrame[channel],
                        mixerFilters_[slotIndex][channel], params.midG,
                        params.lowGain, params.midGain, params.highGain);
                    if (params.stereoPan && channel < 2u)
                        strip *= channel == 0u ? params.leftBalance
                                              : params.rightBalance;
                    strip *= params.level;
                    strip = finiteSample(strip);
                    mixed[channel] += strip;
                    aux[channel] += strip * params.auxSendSquared;
                    slotPeaks_[slotIndex] = std::max(slotPeaks_[slotIndex],
                        std::abs(strip));
                }
            }

            if (mixer_->auxEnabled) {
                auxProcessor_.processFrame(aux.data(), outputChannelCount);
                for (uint32_t channel = 0u; channel < outputChannelCount;
                     ++channel)
                    mixed[channel] += aux[channel] * auxReturnGain;
                auxActivity_ = auxProcessor_.activity();
                auxGainReductionDb_ = auxProcessor_.gainReductionDb();
            }
            for (uint32_t channel = 0u; channel < outputChannelCount;
                 ++channel) {
                // Floating-point plug-in audio deliberately keeps headroom
                // above 0 dBFS. Character clipping belongs in an explicit
                // sound mode, not invisibly inside the clean engine.
                outputs[channel][frame] = mixed[channel] * mixer_->outputGain;
            }
        }
    }

    void render(const RenderEvent* events, std::size_t eventCount,
        float* left, float* right, uint32_t frameCount) noexcept
    {
        float* outputs[] { left, right };
        render(events, eventCount, outputs, 2u, frameCount);
    }

private:
    static constexpr std::size_t kResonatorDelayFrames = 2048u;
    static constexpr std::size_t kErosionDelayFrames = 512u;
    static constexpr std::size_t kHilbertStages = 4u;
    static constexpr std::size_t kTemporalBufferFrames = 65536u;

    struct InsertRenderParams {
        InsertType type = InsertType::Off;
        FilterMode filterMode = FilterMode::LowPass;
        uint8_t variant = 0u;
        bool bypassed = false;
        float mix = 1.0f;

        float filterA1 = 1.0f;
        float filterA2 = 0.0f;
        float filterA3 = 0.0f;
        float filterK = 1.0f;
        float filterDrive = 1.0f;
        float filterDriveNorm = 1.0f;

        uint32_t degradePeriod = 1u;
        uint32_t degradeJitter = 0u;
        float degradeScale = 32768.0f;

        float transientAttack = 0.0f;
        float transientSustain = 0.0f;
        float transientGateThreshold = 0.0f;
        float transientFastAttack = 1.0f;
        float transientFastRelease = 1.0f;
        float transientSlowAttack = 1.0f;
        float transientSlowRelease = 1.0f;
        float transientGateRelease = 1.0f;

        uint32_t resonatorDelay = 1u;
        float resonatorFeedback = 0.0f;
        float resonatorDamping = 1.0f;

        float erosionPhaseIncrement = 0.0f;
        float erosionDepthFrames = 1.0f;
        float erosionFeedback = 0.0f;

        float shifterPhaseIncrement = 0.0f;
        float shifterFeedback = 0.0f;
        float shifterColor = 0.0f;

        float foldDrive = 1.0f;
        float foldBias = 0.0f;
        float foldShape = 0.0f;

        uint32_t temporalWindowFrames = 384u;
        uint32_t repeaterCount = 1u;
        float repeaterDecay = 0.0f;
        float timePitchRatio = 1.0f;
        float timeBrake = 0.0f;
        float timeFreezeDecayFrames = 48000.0f;
        float timeReverseReleaseFrames = 480.0f;
    };

    struct InsertFilterState {
        float ic1 = 0.0f;
        float ic2 = 0.0f;
    };

    struct InsertProcessorState {
        std::array<InsertFilterState, kMaximumAudioChannels> filters {};
        std::array<float, kMaximumAudioChannels> held {};
        std::array<float, kMaximumAudioChannels> resonatorDamped {};
        std::array<std::array<float, kResonatorDelayFrames>,
            kMaximumAudioChannels> resonatorBuffer {};
        std::array<std::array<float, kErosionDelayFrames>,
            kMaximumAudioChannels> erosionBuffer {};
        std::array<std::array<float, kHilbertStages>,
            kMaximumAudioChannels> shifterHilbertA {};
        std::array<std::array<float, kHilbertStages>,
            kMaximumAudioChannels> shifterHilbertB {};
        std::array<float, kMaximumAudioChannels> shifterPreviousWet {};
        std::array<float, kMaximumAudioChannels> foldPreviousInput {};
        std::array<float, kMaximumAudioChannels> foldDcInput {};
        std::array<float, kMaximumAudioChannels> foldDcOutput {};
        std::array<float, kMaximumAudioChannels> temporalPreviousWet {};
        uint32_t degradeRemaining = 0u;
        uint32_t random = 0x9e3779b9u;
        uint32_t resonatorWrite = 0u;
        uint32_t erosionWrite = 0u;
        uint32_t erosionRandom = 0x243f6a88u;
        uint32_t shifterGovernorRunFrames = 0u;
        uint32_t shifterGovernorHoldFrames = 0u;
        float erosionPhase = 0.0f;
        float erosionNoise = 0.0f;
        float erosionNoiseTarget = 0.0f;
        float shifterPhase = 0.0f;
        float shifterGovernorEnvelope = 0.0f;
        float shifterGovernorGain = 1.0f;
        float* temporalBuffer = nullptr;
        uint32_t temporalWrite = 0u;
        uint32_t temporalValidFrames = 0u;
        uint32_t temporalCaptureStart = 0u;
        uint32_t temporalCaptureFrames = 0u;
        uint32_t temporalCaptureTarget = 0u;
        uint32_t temporalCaptureWritten = 0u;
        uint32_t temporalPlaybackFrame = 0u;
        uint32_t temporalCaptureTransitionFrame
            = std::numeric_limits<uint32_t>::max();
        uint32_t temporalLatchedRepeaterCount = 1u;
        double temporalRead = 0.0;
        double temporalSpeed = 1.0;
        float temporalDetectorFast = 0.0f;
        float temporalDetectorSlow = 0.0f;
        float temporalSmoothedMix = 1.0f;
        float temporalSmoothedDecay = 0.0f;
        float temporalSmoothedPitchRatio = 1.0f;
        float temporalSmoothedBrake = 0.0f;
        float temporalSmoothedFreezeDecayFrames = 48000.0f;
        float temporalSmoothedReverseReleaseFrames = 480.0f;
        float temporalTapeRateScale = 1.0f;
        bool temporalCaptureReady = false;
        bool temporalDetectorArmed = true;
        bool temporalParametersInitialized = false;
        uint8_t temporalLatchedVariant = 0u;
        uint8_t temporalPhase = 0u; // listen, capture, playback
        float transientFast = 0.0f;
        float transientSlow = 0.0f;
        float transientGate = 1.0f;
        InsertType activeType = InsertType::Off;
    };

    struct Voice {
        enum class EnvelopeStage : uint8_t {
            Attack = 0u,
            Decay,
            Sustain,
            Release,
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
        uint32_t naturalFadeFrames = 2u;
        uint32_t tailReleaseFrames = 0u;
        uint32_t attackFrames = 0u;
        uint32_t decayFrames = 0u;
        uint32_t releaseFrames = 0u;
        uint32_t envelopeFrame = 0u;
        uint8_t slotIndex = 0u;
        uint8_t key = 0u;
        uint8_t midiChannel = 0u;
        uint8_t chokeGroup = 0u;
        LaunchMode launchMode = LaunchMode::OneShot;
        EnvelopeStage envelopeStage = EnvelopeStage::Sustain;
        float level = 0.0f;
        float leftPan = 1.0f;
        float rightPan = 1.0f;
        float envelopeLevel = 0.0f;
        float releaseStartLevel = 0.0f;
        float sustainLevel = 1.0f;
        bool active = false;
    };

    struct MixerFilterState {
        float low = 0.0f;
        float lowMid = 0.0f;
        float midIc1 = 0.0f;
        float midIc2 = 0.0f;
    };

    static float finiteSample(float value) noexcept
    {
        if (!std::isfinite(value)) return 0.0f;
        return std::abs(value) < 1.0e-20f ? 0.0f : value;
    }

    static float dbGain(float decibels) noexcept
    {
        return std::pow(10.0f, decibels * 0.05f);
    }

    static float frequencyCoefficient(float frequencyHz,
        float sampleRate) noexcept
    {
        constexpr float kTwoPi = 6.28318530717958647692f;
        const float frequency = std::clamp(frequencyHz, 1.0f,
            sampleRate * 0.45f);
        return 1.0f - std::exp(-kTwoPi * frequency / sampleRate);
    }

    float midFrequencyG(float frequencyHz) const noexcept
    {
        constexpr float kPi = 3.14159265358979323846f;
        const float normalized = std::clamp(frequencyHz, 120.0f, 8000.0f)
            / static_cast<float>(sampleRate_);
        return std::tan(kPi * std::min(normalized, 0.45f));
    }

    float processEq(float input, MixerFilterState& filter, float midG,
        float lowGain, float midGain, float highGain) const noexcept
    {
        constexpr float kMidQInverse = 1.25f;
        const float clean = finiteSample(input);
        filter.low = finiteSample(filter.low
            + (clean - filter.low) * lowCoefficient_);
        filter.lowMid = finiteSample(filter.lowMid
            + (clean - filter.lowMid) * highCoefficient_);
        const float low = filter.low;
        const float high = clean - filter.lowMid;
        const float a1 = 1.0f
            / (1.0f + midG * (midG + kMidQInverse));
        const float a2 = midG * a1;
        const float a3 = midG * a2;
        const float v3 = clean - filter.midIc2;
        const float v1 = a1 * filter.midIc1 + a2 * v3;
        const float v2 = filter.midIc2 + a2 * filter.midIc1 + a3 * v3;
        filter.midIc1 = finiteSample(2.0f * v1 - filter.midIc1);
        filter.midIc2 = finiteSample(2.0f * v2 - filter.midIc2);
        const float mid = kMidQInverse * v1;
        return finiteSample(clean + (lowGain - 1.0f) * low
            + (midGain - 1.0f) * mid + (highGain - 1.0f) * high);
    }

    float timeCoefficient(float milliseconds) const noexcept
    {
        const double frames = std::max(1.0,
            sampleRate_ * static_cast<double>(milliseconds) * 0.001);
        return static_cast<float>(1.0 - std::exp(-1.0 / frames));
    }

    uint32_t temporalWindowFrames(float normalized) const noexcept
    {
        constexpr std::array<double, 8u> milliseconds {{
            8.0, 16.0, 32.0, 64.0, 125.0, 250.0, 500.0, 1000.0,
        }};
        const std::size_t index = static_cast<std::size_t>(std::clamp(
            std::lround(normalized
                * static_cast<float>(milliseconds.size() - 1u)),
            0l, static_cast<long>(milliseconds.size() - 1u)));
        return static_cast<uint32_t>(std::clamp<double>(std::round(
            milliseconds[index] * sampleRate_ * 0.001), 2.0,
            static_cast<double>(kTemporalBufferFrames)));
    }

    InsertRenderParams insertRenderParams(
        const InsertSettings& settings) const noexcept
    {
        constexpr float kPi = 3.14159265358979323846f;
        InsertRenderParams result;
        result.type = settings.type;
        result.filterMode = settings.mode;
        result.variant = settings.variant;
        result.bypassed = settings.bypassed;
        result.mix = settings.values[3u];
        switch (settings.type) {
        case InsertType::Filter: {
            const float cutoff = 30.0f * std::pow(
                20000.0f / 30.0f, settings.values[0u]);
            const float normalized = std::min(0.45f,
                cutoff / static_cast<float>(sampleRate_));
            const float g = std::tan(kPi * normalized);
            const float q = 0.5f
                + 15.5f * settings.values[1u] * settings.values[1u];
            result.filterK = 1.0f / q;
            result.filterA1 = 1.0f
                / (1.0f + g * (g + result.filterK));
            result.filterA2 = g * result.filterA1;
            result.filterA3 = g * result.filterA2;
            result.filterDrive = 1.0f
                + 15.0f * settings.values[2u] * settings.values[2u];
            result.filterDriveNorm = result.filterDrive > 1.0001f
                ? 1.0f / std::tanh(result.filterDrive) : 1.0f;
            break;
        }
        case InsertType::Degrade: {
            result.degradePeriod = 1u + static_cast<uint32_t>(std::lround(
                settings.values[0u] * settings.values[0u] * 95.0f));
            result.degradeJitter = static_cast<uint32_t>(std::lround(
                static_cast<float>(result.degradePeriod)
                    * 0.75f * settings.values[2u]));
            const uint32_t bits = 4u + static_cast<uint32_t>(std::lround(
                settings.values[1u] * 12.0f));
            result.degradeScale = static_cast<float>(1u << (bits - 1u));
            break;
        }
        case InsertType::Transient:
            result.transientAttack = settings.values[0u] * 2.0f - 1.0f;
            result.transientSustain = settings.values[1u] * 2.0f - 1.0f;
            result.transientGateThreshold = settings.values[2u] <= 0.002f
                ? 0.0f : std::pow(10.0f,
                    (-72.0f + settings.values[2u] * 48.0f) * 0.05f);
            result.transientFastAttack = timeCoefficient(0.25f);
            result.transientFastRelease = timeCoefficient(18.0f);
            result.transientSlowAttack = timeCoefficient(12.0f);
            result.transientSlowRelease = timeCoefficient(180.0f);
            result.transientGateRelease = timeCoefficient(55.0f);
            break;
        case InsertType::Resonator: {
            const float frequency = 40.0f * std::pow(
                100.0f, settings.values[0u]);
            result.resonatorDelay = static_cast<uint32_t>(std::clamp<double>(
                std::round(sampleRate_ / frequency), 1.0,
                static_cast<double>(kResonatorDelayFrames - 1u)));
            result.resonatorFeedback = 0.94f * settings.values[1u];
            const float dampingCutoff = 250.0f
                + 17750.0f * std::pow(1.0f - settings.values[2u], 2.0f);
            result.resonatorDamping = frequencyCoefficient(dampingCutoff,
                static_cast<float>(sampleRate_));
            break;
        }
        case InsertType::Erosion: {
            const float modulationHz = 10.0f * std::pow(
                1600.0f, settings.values[0u]);
            result.erosionPhaseIncrement = 2.0f * kPi * modulationHz
                / static_cast<float>(sampleRate_);
            result.erosionDepthFrames = 1.0f
                + settings.values[1u] * settings.values[1u]
                    * static_cast<float>(kErosionDelayFrames - 3u);
            result.erosionFeedback = settings.values[2u] * 0.88f;
            break;
        }
        case InsertType::Shifter: {
            float frequency = 0.0f;
            if (settings.variant == 0u) {
                const float bipolar = settings.values[0u] * 2.0f - 1.0f;
                frequency = std::copysign(bipolar * bipolar * 4000.0f,
                    bipolar);
            } else {
                frequency = 20.0f * std::pow(1000.0f,
                    settings.values[0u]);
            }
            result.shifterPhaseIncrement = 2.0f * kPi * frequency
                / static_cast<float>(sampleRate_);
            result.shifterFeedback = settings.values[1u] * 0.88f;
            result.shifterColor = settings.values[2u];
            break;
        }
        case InsertType::Wavefolder:
            result.foldDrive = 1.0f
                + settings.values[0u] * settings.values[0u] * 31.0f;
            result.foldBias = settings.values[1u] * 2.0f - 1.0f;
            result.foldShape = settings.values[2u];
            break;
        case InsertType::Repeater:
            result.temporalWindowFrames = temporalWindowFrames(
                settings.values[0u]);
            result.repeaterCount = 1u + static_cast<uint32_t>(std::lround(
                settings.values[1u] * 15.0f));
            result.repeaterDecay = settings.values[2u];
            break;
        case InsertType::TimeMangler: {
            result.temporalWindowFrames = temporalWindowFrames(
                settings.values[0u]);
            const float semitones = (settings.values[1u] * 2.0f - 1.0f)
                * 24.0f;
            result.timePitchRatio = std::pow(2.0f, semitones / 12.0f);
            result.timeBrake = settings.values[2u];
            result.timeFreezeDecayFrames = static_cast<float>(sampleRate_
                * 0.125 * std::pow(128.0,
                    static_cast<double>(settings.values[2u])));
            result.timeReverseReleaseFrames = static_cast<float>(sampleRate_
                * 0.002 * std::pow(100.0,
                    static_cast<double>(settings.values[2u])));
            break;
        }
        case InsertType::Off: break;
        }
        return result;
    }

    static void resetInsertState(InsertProcessorState& state,
        bool clearResonator = true, bool clearErosion = true) noexcept
    {
        state.filters = {};
        state.held.fill(0.0f);
        if (clearResonator) {
            state.resonatorDamped.fill(0.0f);
            for (auto& channel : state.resonatorBuffer) channel.fill(0.0f);
            state.resonatorWrite = 0u;
        }
        if (clearErosion) {
            for (auto& channel : state.erosionBuffer) channel.fill(0.0f);
            state.erosionWrite = 0u;
        }
        state.shifterHilbertA = {};
        state.shifterHilbertB = {};
        state.shifterPreviousWet.fill(0.0f);
        state.foldPreviousInput.fill(0.0f);
        state.foldDcInput.fill(0.0f);
        state.foldDcOutput.fill(0.0f);
        state.temporalPreviousWet.fill(0.0f);
        state.degradeRemaining = 0u;
        state.random = 0x9e3779b9u;
        state.transientFast = 0.0f;
        state.transientSlow = 0.0f;
        state.transientGate = 1.0f;
        state.erosionRandom = 0x243f6a88u;
        state.erosionPhase = 0.0f;
        state.erosionNoise = 0.0f;
        state.erosionNoiseTarget = 0.0f;
        state.shifterPhase = 0.0f;
        state.shifterGovernorRunFrames = 0u;
        state.shifterGovernorHoldFrames = 0u;
        state.shifterGovernorEnvelope = 0.0f;
        state.shifterGovernorGain = 1.0f;
        state.temporalWrite = 0u;
        state.temporalValidFrames = 0u;
        state.temporalCaptureStart = 0u;
        state.temporalCaptureFrames = 0u;
        state.temporalCaptureTarget = 0u;
        state.temporalCaptureWritten = 0u;
        state.temporalPlaybackFrame = 0u;
        state.temporalCaptureTransitionFrame
            = std::numeric_limits<uint32_t>::max();
        state.temporalLatchedRepeaterCount = 1u;
        state.temporalRead = 0.0;
        state.temporalSpeed = 1.0;
        state.temporalDetectorFast = 0.0f;
        state.temporalDetectorSlow = 0.0f;
        state.temporalSmoothedMix = 1.0f;
        state.temporalSmoothedDecay = 0.0f;
        state.temporalSmoothedPitchRatio = 1.0f;
        state.temporalSmoothedBrake = 0.0f;
        state.temporalSmoothedFreezeDecayFrames = 48000.0f;
        state.temporalSmoothedReverseReleaseFrames = 480.0f;
        state.temporalTapeRateScale = 1.0f;
        state.temporalCaptureReady = false;
        state.temporalDetectorArmed = true;
        state.temporalParametersInitialized = false;
        state.temporalLatchedVariant = 0u;
        state.temporalPhase = 0u;
        state.activeType = InsertType::Off;
    }

    void resetAllInsertStates() noexcept
    {
        for (std::size_t slot = 0u; slot < insertStates_.size(); ++slot) {
            for (std::size_t insert = 0u;
                 insert < insertStates_[slot].size(); ++insert) {
                auto& state = insertStates_[slot][insert];
                resetInsertState(state);
                if (mixer_)
                    state.activeType = mixer_->strips[slot]
                        .inserts[insert].type;
            }
        }
    }

    static uint32_t advanceRandom(uint32_t& state) noexcept
    {
        state ^= state << 13u;
        state ^= state >> 17u;
        state ^= state << 5u;
        if (state == 0u) state = 0x9e3779b9u;
        return state;
    }

    static float allpassCascade(float input,
        std::array<float, kHilbertStages>& state,
        const std::array<float, kHilbertStages>& coefficients) noexcept
    {
        float value = input;
        for (std::size_t stage = 0u; stage < state.size(); ++stage) {
            const float output = coefficients[stage] * value + state[stage];
            state[stage] = finiteSample(value
                - coefficients[stage] * output);
            value = finiteSample(output);
        }
        return value;
    }

    static float foldShape(float input, uint8_t variant,
        float shape) noexcept
    {
        constexpr float kHalfPi = 1.57079632679489661923f;
        constexpr float kTwoOverPi = 0.63661977236758134308f;
        if (variant == 0u) {
            const float triangle = kTwoOverPi
                * std::asin(std::sin(kHalfPi * input));
            const float sineFold = std::sin(kHalfPi * input);
            return triangle + (sineFold - triangle) * shape;
        }
        const float softness = 1.0f + shape * 12.0f;
        const float soft = std::tanh(input * softness)
            / std::tanh(softness);
        const float hard = std::clamp(input, -1.0f, 1.0f);
        return soft + (hard - soft) * shape;
    }

    static float* temporalChannel(InsertProcessorState& state,
        uint32_t channel) noexcept
    {
        return state.temporalBuffer
            ? state.temporalBuffer
                + static_cast<std::size_t>(channel) * kTemporalBufferFrames
            : nullptr;
    }

    static const float* temporalChannel(const InsertProcessorState& state,
        uint32_t channel) noexcept
    {
        return state.temporalBuffer
            ? state.temporalBuffer
                + static_cast<std::size_t>(channel) * kTemporalBufferFrames
            : nullptr;
    }

    static void writeTemporalFrame(InsertProcessorState& state,
        const std::array<float, kMaximumAudioChannels>& input,
        uint32_t channelCount) noexcept
    {
        if (!state.temporalBuffer) return;
        for (uint32_t channel = 0u; channel < channelCount; ++channel)
            temporalChannel(state, channel)[state.temporalWrite]
                = finiteSample(input[channel]);
        state.temporalWrite = static_cast<uint32_t>(
            (state.temporalWrite + 1u) % kTemporalBufferFrames);
        state.temporalValidFrames = std::min<uint32_t>(
            static_cast<uint32_t>(kTemporalBufferFrames),
            state.temporalValidFrames + 1u);
    }

    bool detectTemporalOnset(InsertProcessorState& state,
        const std::array<float, kMaximumAudioChannels>& input,
        uint32_t channelCount) const noexcept
    {
        float peak = 0.0f;
        for (uint32_t channel = 0u; channel < channelCount; ++channel)
            peak = std::max(peak, std::abs(finiteSample(input[channel])));
        if (peak >= state.temporalDetectorFast)
            state.temporalDetectorFast = peak;
        else
            state.temporalDetectorFast += (peak
                - state.temporalDetectorFast)
                * temporalFastReleaseCoefficient_;
        const float slowCoefficient = peak >= state.temporalDetectorSlow
            ? temporalSlowAttackCoefficient_
            : temporalSlowReleaseCoefficient_;
        state.temporalDetectorSlow += (peak
            - state.temporalDetectorSlow) * slowCoefficient;
        const float novelty = std::max(0.0f,
            state.temporalDetectorFast - state.temporalDetectorSlow);
        if (!state.temporalDetectorArmed
            && novelty <= std::max(0.00025f,
                state.temporalDetectorSlow * 0.15f))
            state.temporalDetectorArmed = true;
        if (!state.temporalDetectorArmed || peak < 0.002f
            || novelty <= std::max(0.00075f,
                state.temporalDetectorSlow * 0.55f)) return false;
        state.temporalDetectorArmed = false;
        return true;
    }

    void smoothTemporalParameters(InsertProcessorState& state,
        const InsertRenderParams& params) const noexcept
    {
        const float mixTarget = params.bypassed ? 0.0f : params.mix;
        if (!state.temporalParametersInitialized) {
            state.temporalSmoothedMix = mixTarget;
            state.temporalSmoothedDecay = params.repeaterDecay;
            state.temporalSmoothedPitchRatio = params.timePitchRatio;
            state.temporalSmoothedBrake = params.timeBrake;
            state.temporalSmoothedFreezeDecayFrames
                = params.timeFreezeDecayFrames;
            state.temporalSmoothedReverseReleaseFrames
                = params.timeReverseReleaseFrames;
            state.temporalParametersInitialized = true;
            return;
        }
        const auto smooth = [this](float current, float target) noexcept {
            return current + (target - current)
                * temporalParameterCoefficient_;
        };
        state.temporalSmoothedMix = smooth(state.temporalSmoothedMix,
            mixTarget);
        state.temporalSmoothedDecay = smooth(state.temporalSmoothedDecay,
            params.repeaterDecay);
        state.temporalSmoothedPitchRatio = smooth(
            state.temporalSmoothedPitchRatio, params.timePitchRatio);
        state.temporalSmoothedBrake = smooth(state.temporalSmoothedBrake,
            params.timeBrake);
        state.temporalSmoothedFreezeDecayFrames = smooth(
            state.temporalSmoothedFreezeDecayFrames,
            params.timeFreezeDecayFrames);
        state.temporalSmoothedReverseReleaseFrames = smooth(
            state.temporalSmoothedReverseReleaseFrames,
            params.timeReverseReleaseFrames);
    }

    float temporalPlaybackEnvelope(uint32_t frame,
        uint32_t totalFrames) const noexcept
    {
        if (totalFrames == 0u) return 0.0f;
        const uint32_t fadeFrames = std::max<uint32_t>(2u,
            std::min<uint32_t>(temporalTransitionFrames_,
                std::max<uint32_t>(2u, totalFrames / 4u)));
        const float attack = std::min(1.0f,
            static_cast<float>(frame + 1u) / fadeFrames);
        const float release = std::min(1.0f,
            static_cast<float>(totalFrames - std::min(frame, totalFrames))
                / fadeFrames);
        return std::min(attack, release);
    }

    static void beginTemporalCapture(InsertProcessorState& state,
        uint32_t captureFrames, uint8_t variant,
        uint32_t repeaterCount) noexcept
    {
        const bool replacingPlayback = state.temporalPhase == 2u;
        state.temporalCaptureTarget = std::clamp<uint32_t>(captureFrames,
            2u, static_cast<uint32_t>(kTemporalBufferFrames));
        state.temporalCaptureStart = state.temporalWrite;
        state.temporalCaptureWritten = 0u;
        state.temporalCaptureFrames = 0u;
        state.temporalPlaybackFrame = 0u;
        state.temporalRead = 0.0;
        state.temporalSpeed = 1.0;
        state.temporalTapeRateScale = 1.0f;
        state.temporalCaptureReady = false;
        state.temporalCaptureTransitionFrame = replacingPlayback ? 0u
            : std::numeric_limits<uint32_t>::max();
        state.temporalLatchedVariant = variant;
        state.temporalLatchedRepeaterCount = std::max<uint32_t>(1u,
            repeaterCount);
        if (!replacingPlayback) state.temporalPreviousWet.fill(0.0f);
        state.temporalPhase = 1u;
    }

    void applyTemporalCaptureTransition(
        std::array<float, kMaximumAudioChannels>& samples,
        const std::array<float, kMaximumAudioChannels>& dry,
        uint32_t channelCount, InsertProcessorState& state) const noexcept
    {
        if (state.temporalCaptureTransitionFrame
            >= temporalTransitionFrames_) return;
        const float gain = 1.0f
            - static_cast<float>(state.temporalCaptureTransitionFrame + 1u)
                / static_cast<float>(temporalTransitionFrames_);
        for (uint32_t channel = 0u; channel < channelCount; ++channel)
            samples[channel] = finiteSample(dry[channel]
                + (state.temporalPreviousWet[channel] - dry[channel])
                    * state.temporalSmoothedMix
                    * std::clamp(gain, 0.0f, 1.0f));
        ++state.temporalCaptureTransitionFrame;
    }

    static bool advanceTemporalCapture(
        InsertProcessorState& state) noexcept
    {
        if (state.temporalPhase != 1u
            || state.temporalCaptureTarget < 2u) return false;
        ++state.temporalCaptureWritten;
        if (state.temporalCaptureWritten
            < state.temporalCaptureTarget) return false;
        state.temporalCaptureFrames = state.temporalCaptureTarget;
        state.temporalPlaybackFrame = 0u;
        state.temporalCaptureReady = true;
        state.temporalPhase = 2u;
        return true;
    }

    static float readTemporal(const InsertProcessorState& state,
        uint32_t channel, double offset) noexcept
    {
        if (!state.temporalBuffer || state.temporalCaptureFrames == 0u)
            return 0.0f;
        const double length = static_cast<double>(
            state.temporalCaptureFrames);
        offset = std::fmod(offset, length);
        if (offset < 0.0) offset += length;
        const uint32_t firstOffset = static_cast<uint32_t>(offset);
        const uint32_t secondOffset = static_cast<uint32_t>(
            (firstOffset + 1u) % state.temporalCaptureFrames);
        const float fraction = static_cast<float>(offset
            - static_cast<double>(firstOffset));
        const uint32_t first = static_cast<uint32_t>(
            (state.temporalCaptureStart + firstOffset)
                % kTemporalBufferFrames);
        const uint32_t second = static_cast<uint32_t>(
            (state.temporalCaptureStart + secondOffset)
                % kTemporalBufferFrames);
        const float* buffer = temporalChannel(state, channel);
        return finiteSample(buffer[first]
            + (buffer[second] - buffer[first]) * fraction);
    }

    float loopCrossfadedTemporal(
        const InsertProcessorState& state, uint32_t channel,
        double position, bool reverse) const noexcept
    {
        const uint32_t length = state.temporalCaptureFrames;
        if (length < 4u) return readTemporal(state, channel, position);
        const uint32_t fadeFrames = std::min<uint32_t>(
            temporalTransitionFrames_, length / 4u);
        float wet = readTemporal(state, channel, position);
        if (!reverse && position >= static_cast<double>(length - fadeFrames)) {
            const float blend = static_cast<float>((position
                - static_cast<double>(length - fadeFrames)) / fadeFrames);
            const float wrapped = readTemporal(state, channel,
                position - static_cast<double>(length - fadeFrames));
            wet += (wrapped - wet) * std::clamp(blend, 0.0f, 1.0f);
        } else if (reverse && position < static_cast<double>(fadeFrames)) {
            const float blend = 1.0f
                - static_cast<float>(position / fadeFrames);
            const float wrapped = readTemporal(state, channel,
                static_cast<double>(length - fadeFrames) + position);
            wet += (wrapped - wet) * std::clamp(blend, 0.0f, 1.0f);
        }
        return finiteSample(wet);
    }

    void processInsertFrame(
        std::array<float, kMaximumAudioChannels>& samples,
        uint32_t channelCount, InsertProcessorState& state,
        const InsertRenderParams& params) const noexcept
    {
        if (params.type == InsertType::Off || channelCount == 0u) return;
        const auto dry = samples;
        switch (params.type) {
        case InsertType::Filter:
            for (uint32_t channel = 0u; channel < channelCount; ++channel) {
                const float input = finiteSample(samples[channel]);
                const float driven = params.filterDrive > 1.0001f
                    ? std::tanh(input * params.filterDrive)
                        * params.filterDriveNorm : input;
                auto& filter = state.filters[channel];
                const float v3 = driven - filter.ic2;
                const float v1 = params.filterA1 * filter.ic1
                    + params.filterA2 * v3;
                const float v2 = filter.ic2
                    + params.filterA2 * filter.ic1
                    + params.filterA3 * v3;
                filter.ic1 = finiteSample(2.0f * v1 - filter.ic1);
                filter.ic2 = finiteSample(2.0f * v2 - filter.ic2);
                const float high = driven - params.filterK * v1 - v2;
                float wet = v2;
                switch (params.filterMode) {
                case FilterMode::LowPass: wet = v2; break;
                case FilterMode::BandPass: wet = v1; break;
                case FilterMode::HighPass: wet = high; break;
                case FilterMode::Notch: wet = high + v2; break;
                }
                samples[channel] = finiteSample(input
                    + (wet - input) * params.mix);
            }
            break;
        case InsertType::Degrade:
            if (state.degradeRemaining == 0u) {
                int32_t jitter = 0;
                if (params.degradeJitter != 0u) {
                    const uint32_t span = params.degradeJitter * 2u + 1u;
                    jitter = static_cast<int32_t>(advanceRandom(state.random)
                        % span) - static_cast<int32_t>(params.degradeJitter);
                }
                state.degradeRemaining = static_cast<uint32_t>(std::max(
                    1, static_cast<int32_t>(params.degradePeriod) + jitter));
                for (uint32_t channel = 0u; channel < channelCount;
                     ++channel)
                    state.held[channel] = finiteSample(std::round(
                        samples[channel] * params.degradeScale)
                        / params.degradeScale);
            }
            --state.degradeRemaining;
            for (uint32_t channel = 0u; channel < channelCount; ++channel)
                samples[channel] = finiteSample(samples[channel]
                    + (state.held[channel] - samples[channel]) * params.mix);
            break;
        case InsertType::Transient: {
            float detector = 0.0f;
            for (uint32_t channel = 0u; channel < channelCount; ++channel)
                detector = std::max(detector, std::abs(samples[channel]));
            const float fastCoefficient = detector > state.transientFast
                ? params.transientFastAttack : params.transientFastRelease;
            const float slowCoefficient = detector > state.transientSlow
                ? params.transientSlowAttack : params.transientSlowRelease;
            state.transientFast = finiteSample(state.transientFast
                + (detector - state.transientFast) * fastCoefficient);
            state.transientSlow = finiteSample(state.transientSlow
                + (detector - state.transientSlow) * slowCoefficient);
            const float onset = std::clamp((state.transientFast
                - state.transientSlow)
                / std::max(0.001f, state.transientFast), 0.0f, 1.0f);
            const float body = std::clamp(state.transientSlow * 3.0f,
                0.0f, 1.0f) * (1.0f - onset);
            const float shapedGain = dbGain(
                params.transientAttack * onset * 18.0f
                + params.transientSustain * body * 12.0f);
            const float gateTarget = params.transientGateThreshold <= 0.0f
                    || detector >= params.transientGateThreshold
                ? 1.0f : 0.0f;
            if (gateTarget >= state.transientGate)
                state.transientGate = gateTarget;
            else
                state.transientGate += (gateTarget - state.transientGate)
                    * params.transientGateRelease;
            const float gain = shapedGain * state.transientGate;
            for (uint32_t channel = 0u; channel < channelCount; ++channel) {
                const float wet = samples[channel] * gain;
                samples[channel] = finiteSample(samples[channel]
                    + (wet - samples[channel]) * params.mix);
            }
            break;
        }
        case InsertType::Resonator: {
            const uint32_t read = static_cast<uint32_t>(
                (state.resonatorWrite + kResonatorDelayFrames
                    - params.resonatorDelay) % kResonatorDelayFrames);
            for (uint32_t channel = 0u; channel < channelCount; ++channel) {
                const float delayed = state.resonatorBuffer[channel][read];
                state.resonatorDamped[channel] = finiteSample(
                    state.resonatorDamped[channel]
                    + (delayed - state.resonatorDamped[channel])
                        * params.resonatorDamping);
                const float wet = state.resonatorDamped[channel];
                state.resonatorBuffer[channel][state.resonatorWrite]
                    = finiteSample(samples[channel]
                        + wet * params.resonatorFeedback);
                // Parallel resonator amount: zero is dry, full preserves the
                // original slice while adding the tuned decay.
                samples[channel] = finiteSample(samples[channel]
                    + wet * params.mix);
            }
            state.resonatorWrite = static_cast<uint32_t>(
                (state.resonatorWrite + 1u) % kResonatorDelayFrames);
            break;
        }
        case InsertType::Erosion: {
            constexpr float kTwoPi = 6.28318530717958647692f;
            state.erosionPhase += params.erosionPhaseIncrement;
            bool wrapped = false;
            if (state.erosionPhase >= kTwoPi) {
                state.erosionPhase -= kTwoPi;
                wrapped = true;
            }
            if (params.variant != 0u && wrapped) {
                const uint32_t random = advanceRandom(state.erosionRandom);
                state.erosionNoiseTarget = static_cast<float>(
                    random & 0x00ffffffu) / 8388607.5f - 1.0f;
            }
            state.erosionNoise = finiteSample(state.erosionNoise
                + (state.erosionNoiseTarget - state.erosionNoise) * 0.08f);
            const float modulation = params.variant == 0u
                ? std::sin(state.erosionPhase) : state.erosionNoise;
            const float delay = 1.0f + params.erosionDepthFrames
                * (0.5f + modulation * 0.5f);
            float readPosition = static_cast<float>(state.erosionWrite)
                - delay;
            while (readPosition < 0.0f)
                readPosition += static_cast<float>(kErosionDelayFrames);
            const uint32_t first = static_cast<uint32_t>(readPosition)
                % static_cast<uint32_t>(kErosionDelayFrames);
            const uint32_t second = static_cast<uint32_t>(
                (first + 1u) % kErosionDelayFrames);
            const float fraction = readPosition
                - static_cast<float>(static_cast<uint32_t>(readPosition));
            for (uint32_t channel = 0u; channel < channelCount; ++channel) {
                const auto& buffer = state.erosionBuffer[channel];
                const float wet = finiteSample(buffer[first]
                    + (buffer[second] - buffer[first]) * fraction);
                state.erosionBuffer[channel][state.erosionWrite]
                    = finiteSample(samples[channel]
                        + wet * params.erosionFeedback);
                samples[channel] = finiteSample(samples[channel]
                    + (wet - samples[channel]) * params.mix);
            }
            state.erosionWrite = static_cast<uint32_t>(
                (state.erosionWrite + 1u) % kErosionDelayFrames);
            break;
        }
        case InsertType::Shifter: {
            constexpr float kTwoPi = 6.28318530717958647692f;
            constexpr std::array<float, kHilbertStages> branchA {{
                0.161758f, 0.733029f, 0.945350f, 0.990598f,
            }};
            constexpr std::array<float, kHilbertStages> branchB {{
                0.479401f, 0.876218f, 0.976599f, 0.997500f,
            }};
            float previousWetPeak = 0.0f;
            for (uint32_t channel = 0u; channel < channelCount; ++channel)
                previousWetPeak = std::max(previousWetPeak,
                    std::abs(finiteSample(
                        state.shifterPreviousWet[channel])));
            const float detectorCoefficient
                = previousWetPeak >= state.shifterGovernorEnvelope
                ? shifterGovernorAttackCoefficient_
                : shifterGovernorReleaseCoefficient_;
            state.shifterGovernorEnvelope += (previousWetPeak
                - state.shifterGovernorEnvelope) * detectorCoefficient;

            if (state.shifterGovernorHoldFrames > 0u) {
                --state.shifterGovernorHoldFrames;
                state.shifterGovernorRunFrames = 0u;
            } else {
                const bool sustainedRegeneration
                    = params.shifterFeedback >= 0.55f
                    && state.shifterGovernorEnvelope >= 0.05f
                    && state.shifterGovernorGain >= 0.85f;
                state.shifterGovernorRunFrames = sustainedRegeneration
                    ? std::min<uint32_t>(shifterGovernorIntervalFrames_,
                        state.shifterGovernorRunFrames + 1u)
                    : 0u;
                const bool emergency = state.shifterGovernorGain >= 0.85f
                    && (previousWetPeak >= 4.0f
                        || state.shifterGovernorEnvelope >= 1.75f);
                if (emergency || state.shifterGovernorRunFrames
                        >= shifterGovernorIntervalFrames_) {
                    state.shifterGovernorHoldFrames
                        = shifterGovernorHoldFrames_;
                    state.shifterGovernorRunFrames = 0u;
                }
            }
            const float governorTarget
                = state.shifterGovernorHoldFrames > 0u ? 0.0f : 1.0f;
            const float governorCoefficient = governorTarget
                    < state.shifterGovernorGain
                ? shifterGovernorAttackCoefficient_
                : shifterGovernorRecoveryCoefficient_;
            state.shifterGovernorGain += (governorTarget
                - state.shifterGovernorGain) * governorCoefficient;
            state.shifterGovernorGain = std::clamp(
                state.shifterGovernorGain, 0.0f, 1.0f);

            const float sine = std::sin(state.shifterPhase);
            const float cosine = std::cos(state.shifterPhase);
            for (uint32_t channel = 0u; channel < channelCount; ++channel) {
                const float feedback = state.shifterPreviousWet[channel]
                    * params.shifterFeedback * state.shifterGovernorGain;
                // The soft ceiling is a final guard against numerical bursts;
                // ordinary regeneration remains below this range unchanged.
                const float governedFeedback = 4.0f
                    * std::tanh(feedback * 0.25f);
                const float input = finiteSample(samples[channel]
                    + governedFeedback);
                float wet = 0.0f;
                if (params.variant == 0u) {
                    const float inPhase = allpassCascade(input,
                        state.shifterHilbertA[channel], branchA);
                    const float quadrature = allpassCascade(input,
                        state.shifterHilbertB[channel], branchB);
                    wet = inPhase * cosine - quadrature * sine;
                } else {
                    float carrier = sine;
                    if (params.shifterColor > 0.0f) {
                        const float drive = 1.0f
                            + params.shifterColor * 12.0f;
                        const float colored = std::tanh(carrier * drive)
                            / std::tanh(drive);
                        carrier += (colored - carrier)
                            * params.shifterColor;
                    }
                    wet = input * carrier;
                }
                if (params.variant == 0u && params.shifterColor > 0.0f) {
                    const float drive = 1.0f
                        + params.shifterColor * 8.0f;
                    const float colored = std::tanh(wet * drive)
                        / std::tanh(drive);
                    wet += (colored - wet) * params.shifterColor;
                }
                wet = finiteSample(wet);
                state.shifterPreviousWet[channel] = wet;
                samples[channel] = finiteSample(samples[channel]
                    + (wet - samples[channel]) * params.mix);
            }
            state.shifterPhase += params.shifterPhaseIncrement;
            while (state.shifterPhase >= kTwoPi)
                state.shifterPhase -= kTwoPi;
            while (state.shifterPhase < 0.0f)
                state.shifterPhase += kTwoPi;
            break;
        }
        case InsertType::Wavefolder:
            for (uint32_t channel = 0u; channel < channelCount; ++channel) {
                const float input = finiteSample(samples[channel]);
                float wet = 0.0f;
                // Four linearly interpolated substeps reduce fold/clip aliasing
                // while retaining fixed cost and no render-thread allocation.
                for (uint32_t substep = 1u; substep <= 4u; ++substep) {
                    const float fraction = static_cast<float>(substep) * 0.25f;
                    const float interpolated = state.foldPreviousInput[channel]
                        + (input - state.foldPreviousInput[channel]) * fraction;
                    wet += foldShape(interpolated * params.foldDrive
                        + params.foldBias, params.variant,
                        params.foldShape);
                }
                wet *= 0.25f;
                state.foldPreviousInput[channel] = input;
                const float dcBlocked = wet - state.foldDcInput[channel]
                    + 0.995f * state.foldDcOutput[channel];
                state.foldDcInput[channel] = wet;
                state.foldDcOutput[channel] = finiteSample(dcBlocked);
                samples[channel] = finiteSample(input
                    + (state.foldDcOutput[channel] - input) * params.mix);
            }
            break;
        case InsertType::Repeater: {
            smoothTemporalParameters(state, params);
            const bool onset = detectTemporalOnset(state, dry,
                channelCount);
            if (state.temporalPhase == 0u && onset)
                beginTemporalCapture(state, params.temporalWindowFrames,
                    params.variant, params.repeaterCount);
            if (state.temporalPhase == 2u
                && state.temporalCaptureReady) {
                const uint32_t length = state.temporalCaptureFrames;
                const uint32_t repeat = state.temporalPlaybackFrame / length;
                const uint32_t frameInRepeat = state.temporalPlaybackFrame
                    % length;
                const bool reverse = state.temporalLatchedVariant == 1u
                    || (state.temporalLatchedVariant == 2u
                        && (repeat & 1u) != 0u);
                const float repeatRate = std::pow(2.0f,
                    -state.temporalSmoothedDecay
                        * static_cast<float>(repeat));
                if (frameInRepeat == 0u)
                    state.temporalRead = reverse
                        ? static_cast<double>(length - 1u) : 0.0;
                state.temporalSpeed = reverse
                    ? -static_cast<double>(repeatRate)
                    : static_cast<double>(repeatRate);
                const double position = std::clamp(state.temporalRead, 0.0,
                    static_cast<double>(length - 1u));
                const float repeatGain = std::pow(std::max(0.05f,
                    1.0f - state.temporalSmoothedDecay * 0.12f),
                    static_cast<float>(repeat));
                const uint32_t totalFrames = length
                    * state.temporalLatchedRepeaterCount;
                const float playbackGain = temporalPlaybackEnvelope(
                    state.temporalPlaybackFrame, totalFrames);
                for (uint32_t channel = 0u; channel < channelCount;
                     ++channel) {
                    float wet = loopCrossfadedTemporal(state, channel,
                        position, reverse) * repeatGain;
                    const uint32_t boundaryFadeFrames
                        = std::max<uint32_t>(2u,
                            std::min<uint32_t>(temporalTransitionFrames_,
                                length / 4u));
                    if (repeat != 0u
                        && frameInRepeat < boundaryFadeFrames) {
                        const float blend = static_cast<float>(
                            frameInRepeat + 1u)
                            / static_cast<float>(boundaryFadeFrames);
                        wet = state.temporalPreviousWet[channel]
                            + (wet - state.temporalPreviousWet[channel])
                                * blend;
                    }
                    state.temporalPreviousWet[channel] = finiteSample(wet);
                    samples[channel] = finiteSample(dry[channel]
                        + (wet - dry[channel])
                            * state.temporalSmoothedMix * playbackGain);
                }
                state.temporalRead += state.temporalSpeed;
                ++state.temporalPlaybackFrame;
                if (state.temporalPlaybackFrame >= totalFrames) {
                    state.temporalPhase = 0u;
                    state.temporalCaptureReady = false;
                }
            } else {
                applyTemporalCaptureTransition(samples, dry, channelCount,
                    state);
                writeTemporalFrame(state, dry, channelCount);
                (void)advanceTemporalCapture(state);
            }
            break;
        }
        case InsertType::TimeMangler: {
            smoothTemporalParameters(state, params);
            const bool onset = detectTemporalOnset(state, dry,
                channelCount);
            if ((state.temporalPhase == 0u
                    || (state.temporalLatchedVariant == 1u
                        && state.temporalPhase == 2u))
                && onset)
                beginTemporalCapture(state, params.temporalWindowFrames,
                    params.variant, 1u);
            if (state.temporalPhase == 2u
                && state.temporalCaptureReady) {
                const uint8_t variant = state.temporalLatchedVariant;
                const bool reverse = variant == 0u;
                float playbackGain = std::min(1.0f,
                    static_cast<float>(state.temporalPlaybackFrame + 1u)
                        / static_cast<float>(temporalTransitionFrames_));
                if (variant == 0u) {
                    const double remainingFrames = state.temporalRead
                        / std::max(0.0001f,
                            state.temporalSmoothedPitchRatio);
                    playbackGain = std::min(playbackGain,
                        static_cast<float>(std::clamp(remainingFrames
                            / std::max(2.0f,
                                state.temporalSmoothedReverseReleaseFrames),
                            0.0, 1.0)));
                } else if (variant == 1u) {
                    const uint32_t decayFrames = std::max<uint32_t>(2u,
                        static_cast<uint32_t>(std::lround(
                            state.temporalSmoothedFreezeDecayFrames)));
                    playbackGain = std::min(playbackGain,
                        std::max(0.0f, 1.0f
                            - static_cast<float>(
                                state.temporalPlaybackFrame)
                                / static_cast<float>(decayFrames)));
                } else {
                    const uint32_t limit = state.temporalCaptureFrames * 4u;
                    playbackGain = std::min(playbackGain,
                        temporalPlaybackEnvelope(
                            state.temporalPlaybackFrame, limit));
                    const double remainingFrames = (static_cast<double>(
                        state.temporalCaptureFrames - 1u)
                            - state.temporalRead)
                        / std::max(0.0001, state.temporalSpeed);
                    playbackGain = std::min(playbackGain,
                        static_cast<float>(std::clamp(remainingFrames
                            / static_cast<double>(
                                temporalTransitionFrames_), 0.0, 1.0)));
                }
                for (uint32_t channel = 0u; channel < channelCount;
                     ++channel) {
                    const float wet = variant == 2u
                        ? readTemporal(state, channel, state.temporalRead)
                        : loopCrossfadedTemporal(state, channel,
                            state.temporalRead, reverse);
                    state.temporalPreviousWet[channel] = finiteSample(wet);
                    samples[channel] = finiteSample(dry[channel]
                        + (wet - dry[channel])
                            * state.temporalSmoothedMix * playbackGain);
                }
                if (variant == 2u) {
                    const double brake = std::exp(
                        -static_cast<double>(
                            state.temporalSmoothedBrake) * 5.0
                            / static_cast<double>(
                                state.temporalCaptureFrames));
                    state.temporalTapeRateScale *= static_cast<float>(brake);
                    state.temporalSpeed
                        = state.temporalSmoothedPitchRatio
                            * state.temporalTapeRateScale;
                    const double next = state.temporalRead
                        + state.temporalSpeed;
                    ++state.temporalPlaybackFrame;
                    if (next >= static_cast<double>(
                            state.temporalCaptureFrames - 1u)
                        || state.temporalPlaybackFrame
                            >= state.temporalCaptureFrames * 4u) {
                        state.temporalPhase = 0u;
                        state.temporalCaptureReady = false;
                    } else {
                        state.temporalRead = next;
                    }
                } else if (variant == 1u) {
                    state.temporalSpeed
                        = state.temporalSmoothedPitchRatio;
                    state.temporalRead += state.temporalSpeed;
                    const double length = static_cast<double>(
                        state.temporalCaptureFrames);
                    state.temporalRead = std::fmod(state.temporalRead,
                        length);
                    if (state.temporalRead < 0.0)
                        state.temporalRead += length;
                    ++state.temporalPlaybackFrame;
                    if (state.temporalPlaybackFrame >= static_cast<uint32_t>(
                            std::max(2.0f,
                                state.temporalSmoothedFreezeDecayFrames))) {
                        state.temporalPhase = 0u;
                        state.temporalCaptureReady = false;
                    }
                } else {
                    state.temporalSpeed = -static_cast<double>(
                        state.temporalSmoothedPitchRatio);
                    state.temporalRead += state.temporalSpeed;
                    ++state.temporalPlaybackFrame;
                    if (state.temporalRead < 0.0) {
                        state.temporalPhase = 0u;
                        state.temporalCaptureReady = false;
                    }
                }
            } else {
                applyTemporalCaptureTransition(samples, dry, channelCount,
                    state);
                writeTemporalFrame(state, dry, channelCount);
                if (advanceTemporalCapture(state)) {
                    state.temporalRead
                        = state.temporalLatchedVariant == 0u
                        ? static_cast<double>(
                            state.temporalCaptureFrames - 1u) : 0.0;
                    state.temporalSpeed
                        = state.temporalLatchedVariant == 0u
                        ? -static_cast<double>(
                            state.temporalSmoothedPitchRatio)
                        : static_cast<double>(
                            state.temporalSmoothedPitchRatio);
                    state.temporalTapeRateScale = 1.0f;
                }
            }
            break;
        }
        case InsertType::Off: break;
        }
        if (params.bypassed && params.type != InsertType::Repeater
            && params.type != InsertType::TimeMangler) {
            for (uint32_t channel = 0u; channel < channelCount; ++channel)
                samples[channel] = dry[channel];
        }
    }

    void updateAuxProcessorParams() noexcept
    {
        const MixerSnapshot defaults {};
        const auto& source = mixer_ ? *mixer_ : defaults;
        s3g::BreakBusParams params;
        params.press = source.auxPress;
        params.snap = source.auxSnap;
        params.recovery = source.auxRecovery;
        params.saturation = source.auxSaturation;
        params.bite = source.auxBite;
        params.clip = source.auxClip;
        params.tilt = source.auxTilt;
        params.linkMode = source.auxLinkMode;
        params.fieldSafe = source.auxFieldSafe;
        (void)auxProcessor_.setParams(params);
    }

    static uint32_t proportionalFrames(float proportion,
        uint32_t totalFrames) noexcept
    {
        if (!(proportion > 0.0f) || totalFrames == 0u) return 0u;
        return static_cast<uint32_t>(std::clamp<double>(
            std::round(static_cast<double>(proportion) * totalFrames), 1.0,
            totalFrames));
    }

    static float interpolate(const std::vector<float>& samples,
        double position, uint32_t start, uint32_t end,
        Interpolation interpolation) noexcept
    {
        if (samples.empty() || start >= end) return 0.0f;
        const double bounded = std::clamp(position,
            static_cast<double>(start), static_cast<double>(end - 1u));
        if (interpolation == Interpolation::Nearest) {
            const uint32_t frame = static_cast<uint32_t>(std::clamp<double>(
                std::floor(bounded + 0.5), start, end - 1u));
            return samples[frame];
        }
        const auto first = static_cast<uint32_t>(bounded);
        const auto second = std::min(first + 1u, end - 1u);
        const float fraction = static_cast<float>(
            bounded - static_cast<double>(first));
        return samples[first] + (samples[second] - samples[first]) * fraction;
    }

    void handleEvent(const RenderEvent& event) noexcept
    {
        switch (event.kind) {
        case EventKind::NoteOn: startVoice(event); break;
        case EventKind::NoteOff: releaseNote(event); break;
        case EventKind::Choke: choke(event.chokeGroup); break;
        }
    }

    void startVoice(const RenderEvent& event) noexcept
    {
        if (!bank_ || !mixer_ || event.key >= kMidiNoteCount) return;
        const bool anySolo = std::any_of(mixer_->strips.begin(),
            mixer_->strips.end(), [](const MixerStripSnapshot& strip) {
                return strip.solo;
            });
        for (std::size_t slotIndex = 0u;
             slotIndex < bank_->slots.size(); ++slotIndex) {
            const auto& slot = bank_->slots[slotIndex];
            const auto& strip = mixer_->strips[slotIndex];
            const bool channelMatches = slot.midiChannel == 0u
                || event.midiChannel == 0u
                || slot.midiChannel == event.midiChannel;
            if (!channelMatches || strip.muted || (anySolo && !strip.solo)
                || !slot.asset
                || slot.mappedSliceCount == 0u
                || event.key < slot.mappedRootNote) continue;
            const uint32_t sliceIndex = static_cast<uint32_t>(event.key
                - slot.mappedRootNote);
            if (sliceIndex >= slot.mappedSliceCount
                || sliceIndex >= slot.sliceCount) continue;
            startSlotVoice(event, static_cast<uint8_t>(slotIndex),
                static_cast<uint8_t>(sliceIndex));
        }
    }

    void startSlotVoice(const RenderEvent& event, uint8_t slotIndex,
        uint8_t sliceIndex) noexcept
    {
        const auto& slot = bank_->slots[slotIndex];
        const auto& slice = slot.slices[sliceIndex];
        if (slice.chokeGroup != 0u) choke(slice.chokeGroup);

        std::size_t selected = voices_.size();
        for (std::size_t index = 0u; index < voices_.size(); ++index) {
            if (!voices_[index].active) {
                selected = index;
                break;
            }
        }
        if (selected == voices_.size()) {
            selected = 0u;
            for (std::size_t index = 1u; index < voices_.size(); ++index) {
                if (voices_[index].age < voices_[selected].age)
                    selected = index;
            }
        }

        auto& voice = voices_[selected];
        voice = {};
        voice.asset = slot.asset.get();
        voice.noteId = event.noteId;
        voice.age = ++voiceAge_;
        voice.slotIndex = slotIndex;
        voice.key = event.key;
        voice.midiChannel = event.midiChannel;
        voice.chokeGroup = slice.chokeGroup;
        voice.launchMode = slice.launchMode;
        voice.playStartFrame = slice.startFrame;
        voice.playEndFrame = slice.endFrame;
        if (slice.launchMode == LaunchMode::Thru) {
            if (slice.reverse) voice.playStartFrame = 0u;
            else voice.playEndFrame = slot.asset->frameCount();
        }
        voice.loopStartFrame = slice.loopStartFrame == 0u
                && slice.loopEndFrame == 0u
            ? slice.startFrame : slice.loopStartFrame;
        voice.loopEndFrame = slice.loopStartFrame == 0u
                && slice.loopEndFrame == 0u
            ? slice.endFrame : slice.loopEndFrame;
        voice.position = slice.reverse
            ? static_cast<double>(voice.playEndFrame - 1u)
            : static_cast<double>(voice.playStartFrame);
        const double semitones = static_cast<double>(
            slice.transposeSemitones)
            + static_cast<double>(slice.fineTuneCents) / 100.0;
        const double ratio = std::pow(2.0, semitones / 12.0);
        const double increment = slot.asset->sampleRate / sampleRate_ * ratio;
        voice.increment = slice.reverse ? -increment : increment;
        voice.level = std::clamp(event.velocity, 0.0f, 1.0f)
            * slice.gain;
        constexpr double kHalfPi = 1.57079632679489661923;
        const float pan = std::clamp(slice.pan, -1.0f, 1.0f);
        const double angle = (static_cast<double>(pan) + 1.0)
            * 0.5 * kHalfPi;
        voice.leftPan = static_cast<float>(std::cos(angle));
        voice.rightPan = static_cast<float>(std::sin(angle));
        const double outputLength = std::ceil(
            static_cast<double>(voice.playEndFrame - voice.playStartFrame)
                / std::max(std::abs(increment),
                    std::numeric_limits<double>::min()));
        const auto boundedLength = static_cast<uint32_t>(std::clamp<double>(
            outputLength, 1.0, std::numeric_limits<uint32_t>::max()));
        voice.attackFrames = proportionalFrames(
            slot.envelope.attackProportion, boundedLength);
        voice.decayFrames = proportionalFrames(
            slot.envelope.decayProportion, boundedLength);
        voice.releaseFrames = proportionalFrames(
            slot.envelope.releaseProportion, boundedLength);
        voice.tailReleaseFrames = slice.launchMode == LaunchMode::OneShot
            ? voice.releaseFrames : 0u;
        voice.sustainLevel = slot.envelope.sustain;
        voice.naturalFadeFrames = std::min(naturalFadeFrames_,
            std::max<uint32_t>(2u, boundedLength / 2u));
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
        voice.active = voice.level > 0.0f && increment > 0.0;
    }

    void releaseNote(const RenderEvent& event) noexcept
    {
        for (auto& voice : voices_) {
            if (!voice.active) continue;
            const bool matches = event.noteId != 0u
                ? voice.noteId == event.noteId : voice.key == event.key;
            const bool channelMatches = event.midiChannel == 0u
                || voice.midiChannel == 0u
                || voice.midiChannel == event.midiChannel;
            if (!matches || !channelMatches
                || (voice.launchMode == LaunchMode::OneShot)
                || voice.launchMode == LaunchMode::Thru) continue;
            beginRelease(voice, voice.releaseFrames);
        }
    }

    void choke(uint8_t group) noexcept
    {
        for (auto& voice : voices_) {
            if (!voice.active || (group != 0u
                && voice.chokeGroup != group)) continue;
            beginRelease(voice, chokeReleaseFrames_);
        }
    }

    static void beginRelease(Voice& voice, uint32_t frames) noexcept
    {
        if (voice.envelopeStage == Voice::EnvelopeStage::Release) return;
        if (frames == 0u || voice.envelopeLevel <= 0.0f) {
            voice.active = false;
            voice.envelopeLevel = 0.0f;
            return;
        }
        voice.releaseFrames = frames;
        voice.releaseStartLevel = voice.envelopeLevel;
        voice.envelopeFrame = 0u;
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
                voice.envelopeStage = voice.decayFrames != 0u
                    ? Voice::EnvelopeStage::Decay
                    : Voice::EnvelopeStage::Sustain;
                if (voice.decayFrames == 0u)
                    voice.envelopeLevel = voice.sustainLevel;
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

    static bool loops(const Voice& voice) noexcept
    {
        return voice.launchMode == LaunchMode::Loop
            || voice.launchMode == LaunchMode::PingPong;
    }

    static void advancePosition(Voice& voice) noexcept
    {
        if (!voice.active) return;
        if (!loops(voice)) {
            if (voice.position < static_cast<double>(voice.playStartFrame)
                || voice.position >= static_cast<double>(voice.playEndFrame))
                voice.active = false;
            return;
        }

        const double loopStart = static_cast<double>(voice.loopStartFrame);
        const double loopEnd = static_cast<double>(voice.loopEndFrame);
        const double span = loopEnd - loopStart;
        if (!(span > 0.0)) {
            voice.active = false;
            return;
        }
        if (voice.launchMode == LaunchMode::PingPong) {
            if (voice.increment > 0.0 && voice.position >= loopEnd) {
                voice.position = loopEnd
                    - std::fmod(voice.position - loopEnd, span) - 1.0;
                voice.increment = -voice.increment;
            } else if (voice.increment < 0.0 && voice.position < loopStart) {
                voice.position = loopStart
                    + std::fmod(loopStart - voice.position, span);
                voice.increment = -voice.increment;
            }
        } else if (voice.increment > 0.0 && voice.position >= loopEnd) {
            voice.position = loopStart
                + std::fmod(voice.position - loopEnd, span);
        } else if (voice.increment < 0.0 && voice.position < loopStart) {
            voice.position = loopEnd
                - std::fmod(loopStart - voice.position, span) - 1.0;
        }
    }

    float boundaryEnvelope(const Voice& voice) const noexcept
    {
        if (loops(voice)) return 1.0f;
        const double rate = std::abs(voice.increment);
        if (!(rate > 0.0)) return 0.0f;
        const double distance = voice.increment > 0.0
            ? std::max(0.0, static_cast<double>(voice.playEndFrame)
                - voice.position)
            : std::max(0.0, voice.position
                - static_cast<double>(voice.playStartFrame));
        const uint64_t remaining = static_cast<uint64_t>(std::max(1.0,
            voice.increment > 0.0 ? std::ceil(distance / rate)
                                  : std::floor(distance / rate) + 1.0));
        const uint32_t fadeFrames = std::max(voice.naturalFadeFrames,
            voice.tailReleaseFrames);
        if (remaining >= fadeFrames) return 1.0f;
        return static_cast<float>(remaining - 1u)
            / static_cast<float>(fadeFrames - 1u);
    }

    const BankSnapshot* bank_ = nullptr;
    const MixerSnapshot* mixer_ = nullptr;
    MixerSnapshot ownedMixer_ {};
    std::array<Voice, kMaximumVoices> voices_ {};
    std::array<float, kMaximumSampleSlots> slotPeaks_ {};
    std::array<std::array<MixerFilterState, kMaximumAudioChannels>,
        kMaximumSampleSlots> mixerFilters_ {};
    std::array<std::array<InsertProcessorState, kInsertSlotsPerStrip>,
        kMaximumSampleSlots> insertStates_ {};
    std::unique_ptr<float[]> temporalBuffer_;
    s3g::BreakBus auxProcessor_ {};
    double sampleRate_ = 48000.0;
    uint64_t voiceAge_ = 0u;
    uint32_t naturalFadeFrames_ = 96u;
    uint32_t chokeReleaseFrames_ = 96u;
    float lowCoefficient_ = 0.02f;
    float highCoefficient_ = 0.35f;
    float temporalFastReleaseCoefficient_ = 0.002f;
    float temporalSlowAttackCoefficient_ = 0.001f;
    float temporalSlowReleaseCoefficient_ = 0.0001f;
    float temporalParameterCoefficient_ = 0.001f;
    uint32_t temporalTransitionFrames_ = 240u;
    float shifterGovernorAttackCoefficient_ = 0.01f;
    float shifterGovernorReleaseCoefficient_ = 0.0001f;
    float shifterGovernorRecoveryCoefficient_ = 0.0001f;
    uint32_t shifterGovernorIntervalFrames_ = 36000u;
    uint32_t shifterGovernorHoldFrames_ = 1440u;
    float auxActivity_ = 0.0f;
    float auxGainReductionDb_ = 0.0f;
    bool prepared_ = false;
};

} // namespace s3g::breakbeat
