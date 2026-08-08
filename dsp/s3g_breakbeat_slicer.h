#pragma once

#include "s3g_drum_overload.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <vector>

namespace s3g::breakbeat {

constexpr std::size_t kMaximumSampleSlots = 4u;
constexpr std::size_t kMaximumSlicesPerSlot = 128u;
constexpr std::size_t kMidiNoteCount = 128u;
constexpr std::size_t kMaximumVoices = 32u;
constexpr std::size_t kMaximumAudioChannels = 16u;
constexpr uint8_t kUnmappedIndex = 0xffu;

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

struct Envelope {
    // A, D, and R are fractions of this slice's rendered duration. This
    // keeps one-shot articulation stable when markers or pitch change.
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

// Decoding and validation happen off the audio thread. Every active channel
// has exactly the same frame count: a voice therefore owns one playback clock
// shared by all channels, including quad, octal, and 3OA material.
struct SampleAsset {
    double sampleRate = 48000.0;
    std::array<std::vector<float>, kMaximumAudioChannels> channels {};
    uint8_t channelCount = 0u;

    bool valid() const noexcept
    {
        if (!(sampleRate > 0.0) || !std::isfinite(sampleRate)
            || channelCount == 0u || channelCount > channels.size()
            || channels[0u].empty()
            || channels[0u].size() > std::numeric_limits<uint32_t>::max())
            return false;
        const auto finite = [](const std::vector<float>& channel) {
            return std::all_of(channel.begin(), channel.end(),
                [](float value) { return std::isfinite(value); });
        };
        const std::size_t frames = channels[0u].size();
        for (std::size_t index = 0u; index < channels.size(); ++index) {
            if (index < channelCount) {
                if (channels[index].size() != frames
                    || !finite(channels[index])) return false;
            } else if (!channels[index].empty()) return false;
        }
        return true;
    }

    uint32_t frameCount() const noexcept
    {
        return channelCount != 0u
                && channels[0u].size()
                    <= std::numeric_limits<uint32_t>::max()
            ? static_cast<uint32_t>(channels[0u].size()) : 0u;
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
    Envelope envelope {};
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
            || chokeGroup > 16u || !envelope.valid()) return false;
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
    uint32_t zeroCrossingRadiusFrames = 0u)
{
    std::vector<Slice> result;
    if (!analysis.validFor(asset) || maximumSliceCount == 0u) return result;
    maximumSliceCount = std::min(maximumSliceCount,
        kMaximumSlicesPerSlot);
    std::vector<uint32_t> starts { 0u };
    starts.reserve(std::min(maximumSliceCount,
        analysis.transients.size() + 1u));
    for (const auto& transient : analysis.transients) {
        if (starts.size() >= maximumSliceCount) break;
        uint32_t marker = transient.frame;
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
    float mixerGain = 1.0f;
    float mixerPan = 0.0f;
    float mixerLowEqDb = 0.0f;
    float mixerMidEqDb = 0.0f;
    float mixerHighEqDb = 0.0f;
    float mixerMidFrequencyHz = 900.0f;
    float mixerAuxSend = 0.0f;
    uint8_t rootNote = 36u;
    uint8_t mappedRootNote = 36u;
    uint8_t midiChannel = 0u; // zero is omni; 1-16 are explicit MIDI channels
    bool muted = false;
    bool solo = false;

    bool valid() const noexcept
    {
        if (!std::isfinite(mixerGain)
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
            || mappedSliceCount > sliceCount
            || static_cast<std::size_t>(mappedRootNote) + mappedSliceCount
                > kMidiNoteCount) return false;
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
    float auxDrive = 0.38f;
    float auxGlue = 0.34f;
    float auxRoom = 0.0f;
    float auxWeight = 0.68f;
    float auxTone = 0.0f;
    float auxReturnDb = -9.0f;

    bool valid() const noexcept
    {
        if (!std::isfinite(outputGain) || outputGain < 0.0f
            || outputGain > 4.0f
            || !std::isfinite(auxDrive) || auxDrive < 0.0f
            || auxDrive > 1.0f
            || !std::isfinite(auxGlue) || auxGlue < 0.0f
            || auxGlue > 1.0f
            || !std::isfinite(auxRoom) || auxRoom < -1.0f
            || auxRoom > 1.0f
            || !std::isfinite(auxWeight) || auxWeight < 0.0f
            || auxWeight > 1.0f
            || !std::isfinite(auxTone) || auxTone < -1.0f
            || auxTone > 1.0f
            || !std::isfinite(auxReturnDb) || auxReturnDb < -60.0f
            || auxReturnDb > 12.0f) return false;
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
            && auxSend <= 1.0f;
    }
};

struct MixerSnapshot {
    std::array<MixerStripSnapshot, kMaximumSampleSlots> strips {};
    float outputGain = 1.0f;
    bool auxEnabled = false;
    float auxDrive = 0.38f;
    float auxGlue = 0.34f;
    float auxRoom = 0.0f;
    float auxWeight = 0.68f;
    float auxTone = 0.0f;
    float auxReturnDb = -9.0f;

    bool valid() const noexcept
    {
        if (!std::isfinite(outputGain) || outputGain < 0.0f
            || outputGain > 4.0f
            || !std::isfinite(auxDrive) || auxDrive < 0.0f
            || auxDrive > 1.0f
            || !std::isfinite(auxGlue) || auxGlue < 0.0f
            || auxGlue > 1.0f
            || !std::isfinite(auxRoom) || auxRoom < -1.0f
            || auxRoom > 1.0f
            || !std::isfinite(auxWeight) || auxWeight < 0.0f
            || auxWeight > 1.0f
            || !std::isfinite(auxTone) || auxTone < -1.0f
            || auxTone > 1.0f
            || !std::isfinite(auxReturnDb) || auxReturnDb < -60.0f
            || auxReturnDb > 12.0f) return false;
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
    mixer.auxDrive = bank.auxDrive;
    mixer.auxGlue = bank.auxGlue;
    mixer.auxRoom = bank.auxRoom;
    mixer.auxWeight = bank.auxWeight;
    mixer.auxTone = bank.auxTone;
    mixer.auxReturnDb = bank.auxReturnDb;
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
        destination.muted = source.muted;
        destination.solo = source.solo;
    }
    return mixer;
}

inline void initializeEmptyBank(BankSnapshot& bank) noexcept
{
    bank = {};
    for (std::size_t index = 0u; index < bank.slots.size(); ++index) {
        bank.slots[index].rootNote = static_cast<uint8_t>(36u + index * 16u);
        bank.slots[index].mappedRootNote = bank.slots[index].rootNote;
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
    bool prepare(double sampleRate) noexcept
    {
        if (!(sampleRate > 0.0) || !std::isfinite(sampleRate)) return false;
        sampleRate_ = sampleRate;
        const float sr = static_cast<float>(sampleRate_);
        lowCoefficient_ = frequencyCoefficient(170.0f, sr);
        highCoefficient_ = frequencyCoefficient(4200.0f, sr);
        fastAttackCoefficient_ = onePoleCoefficient(0.18f, sr);
        fastReleaseCoefficient_ = onePoleCoefficient(24.0f, sr);
        slowAttackCoefficient_ = onePoleCoefficient(14.0f, sr);
        slowReleaseCoefficient_ = onePoleCoefficient(260.0f, sr);
        for (auto& processor : auxProcessors_) processor.prepare(sampleRate_);
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
        mixer_ = mixer;
        updateAuxProcessorParams();
    }

    const BankSnapshot* bank() const noexcept { return bank_; }
    const MixerSnapshot* mixer() const noexcept { return mixer_; }

    void reset() noexcept
    {
        voices_ = {};
        mixerFilters_ = {};
        slotPeaks_.fill(0.0f);
        auxFastEnvelopes_.fill(0.0f);
        auxSlowEnvelopes_.fill(0.0f);
        auxRoomGains_.fill(1.0f);
        for (auto& processor : auxProcessors_) processor.reset();
        auxActivity_ = 0.0f;
        auxGainReductionDb_ = 0.0f;
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
                for (uint32_t channel = 0u; channel < outputChannelCount;
                     ++channel) {
                    float strip = processEq(slotMixed[slotIndex][channel],
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
                float frameActivity = 0.0f;
                float frameReduction = 0.0f;
                for (uint32_t pair = 0u; pair < outputChannelCount / 2u;
                     ++pair) {
                    float left = aux[pair * 2u];
                    float right = aux[pair * 2u + 1u];
                    processAuxRoom(pair, left, right);
                    auxProcessors_[pair].processFrame(left, right);
                    mixed[pair * 2u] += left * auxReturnGain;
                    mixed[pair * 2u + 1u] += right * auxReturnGain;
                    frameActivity = std::max(frameActivity,
                        std::max(std::abs(left), std::abs(right)));
                    frameReduction = std::min(frameReduction,
                        auxProcessors_[pair].gainReductionDb());
                }
                auxActivity_ = std::max(auxActivity_, frameActivity);
                auxGainReductionDb_ = std::min(auxGainReductionDb_,
                    frameReduction);
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

    static float onePoleCoefficient(float timeMilliseconds,
        float sampleRate) noexcept
    {
        const float seconds = std::max(0.000001f,
            timeMilliseconds * 0.001f);
        return 1.0f - std::exp(-1.0f / (seconds * sampleRate));
    }

    static float frequencyCoefficient(float frequencyHz,
        float sampleRate) noexcept
    {
        constexpr float kTwoPi = 6.28318530717958647692f;
        const float frequency = std::clamp(frequencyHz, 1.0f,
            sampleRate * 0.45f);
        return 1.0f - std::exp(-kTwoPi * frequency / sampleRate);
    }

    static float followEnvelope(float current, float input,
        float attackCoefficient, float releaseCoefficient) noexcept
    {
        const float coefficient = input > current
            ? attackCoefficient : releaseCoefficient;
        return finiteSample(current + (input - current) * coefficient);
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

    void updateAuxProcessorParams() noexcept
    {
        const MixerSnapshot defaults {};
        const auto& source = mixer_ ? *mixer_ : defaults;
        s3g::DrumOverloadParams params;
        params.circuit = s3g::DrumOverloadCircuit::Console;
        params.inputGainDb = source.auxDrive * 12.0f;
        params.overload = 0.18f + source.auxDrive * 0.72f;
        params.density = source.auxGlue;
        params.punch = 0.08f + source.auxGlue * 0.20f;
        params.bias = 0.08f;
        params.breakup = source.auxGlue * 0.18f;
        params.weight = source.auxWeight;
        params.tone = source.auxTone;
        params.stereoLink = 0.92f;
        params.mix = 1.0f;
        params.outputGainDb = -3.0f;
        params.bypass = false;
        for (auto& processor : auxProcessors_) processor.setParams(params);
    }

    void processAuxRoom(uint32_t pair, float& left, float& right) noexcept
    {
        if (!mixer_ || pair >= auxRoomGains_.size()) return;
        const float magnitude = std::max(std::abs(left), std::abs(right));
        auxFastEnvelopes_[pair] = followEnvelope(auxFastEnvelopes_[pair],
            magnitude, fastAttackCoefficient_, fastReleaseCoefficient_);
        auxSlowEnvelopes_[pair] = followEnvelope(auxSlowEnvelopes_[pair],
            magnitude, slowAttackCoefficient_, slowReleaseCoefficient_);
        const float transient = std::clamp(
            (auxFastEnvelopes_[pair] - auxSlowEnvelopes_[pair])
                / (auxSlowEnvelopes_[pair] + 0.025f), 0.0f, 1.0f);
        const float body = 1.0f - transient;
        const float targetGain = mixer_->auxRoom < 0.0f
            ? 1.0f - (-mixer_->auxRoom) * body * 0.94f
            : 1.0f + mixer_->auxRoom * body * 1.45f;
        auxRoomGains_[pair] = finiteSample(auxRoomGains_[pair]
            + (targetGain - auxRoomGains_[pair])
                * fastReleaseCoefficient_);
        left = finiteSample(left * auxRoomGains_[pair]);
        right = finiteSample(right * auxRoomGains_[pair]);
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
            slice.envelope.attackProportion, boundedLength);
        voice.decayFrames = proportionalFrames(
            slice.envelope.decayProportion, boundedLength);
        voice.releaseFrames = proportionalFrames(
            slice.envelope.releaseProportion, boundedLength);
        voice.tailReleaseFrames = slice.launchMode == LaunchMode::OneShot
            ? voice.releaseFrames : 0u;
        voice.sustainLevel = slice.envelope.sustain;
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
    std::array<s3g::DrumOverload, kMaximumAudioChannels / 2u>
        auxProcessors_ {};
    std::array<float, kMaximumAudioChannels / 2u> auxFastEnvelopes_ {};
    std::array<float, kMaximumAudioChannels / 2u> auxSlowEnvelopes_ {};
    std::array<float, kMaximumAudioChannels / 2u> auxRoomGains_ {};
    double sampleRate_ = 48000.0;
    uint64_t voiceAge_ = 0u;
    uint32_t naturalFadeFrames_ = 96u;
    uint32_t chokeReleaseFrames_ = 96u;
    float lowCoefficient_ = 0.02f;
    float highCoefficient_ = 0.35f;
    float fastAttackCoefficient_ = 0.10f;
    float fastReleaseCoefficient_ = 0.001f;
    float slowAttackCoefficient_ = 0.001f;
    float slowReleaseCoefficient_ = 0.0001f;
    float auxActivity_ = 0.0f;
    float auxGainReductionDb_ = 0.0f;
    bool prepared_ = false;
};

} // namespace s3g::breakbeat
