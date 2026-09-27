#pragma once

#include "s3g_sample_asset.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace s3g::sample {

// One planner is shared by the CHOP destination display and the committing
// edit. Explicit starts are consecutive and never wrap D8 back to A1.
constexpr unsigned kNeonChopAutoDestination = 32u;
enum class NeonChopDestinationError { None, Invalid, Occupied, PastLastPad, NotEnoughEmpty };
struct NeonChopDestinationPlan {
    std::array<std::size_t, 32u> pads {};
    unsigned count = 0u, blockedPad = 0u;
    bool replacesSource = false;
    NeonChopDestinationError error = NeonChopDestinationError::None;
    bool valid() const noexcept { return error == NeonChopDestinationError::None; }
};
inline NeonChopDestinationPlan neonChopDestinationPlan(uint32_t occupied, unsigned source,
    unsigned slices, bool stack, unsigned target = kNeonChopAutoDestination,
    bool replaceSourceAuto = false) noexcept
{
    NeonChopDestinationPlan plan;
    if (source >= 32u || slices == 0u || slices > 32u || target > kNeonChopAutoDestination) {
        plan.error = NeonChopDestinationError::Invalid; return plan;
    }
    const unsigned needed = stack ? 1u : slices;
    if (target < 32u) {
        if (target + needed > 32u) { plan.error = NeonChopDestinationError::PastLastPad; return plan; }
        for (unsigned pad = target; pad < target + needed; ++pad) {
            if (((occupied >> pad) & 1u || pad == source) && !(target == source && pad == source)) {
                plan.error = NeonChopDestinationError::Occupied; plan.blockedPad = pad; plan.count = 0u; return plan;
            }
            plan.pads[plan.count++] = pad;
        }
        plan.replacesSource = target == source;
    } else {
        if (replaceSourceAuto) { plan.pads[plan.count++] = source; plan.replacesSource = true; }
        for (unsigned pad = 0u; pad < 32u && plan.count < needed; ++pad)
            if (pad != source && !((occupied >> pad) & 1u)) plan.pads[plan.count++] = pad;
        if (plan.count != needed) { plan.error = NeonChopDestinationError::NotEnoughEmpty; plan.count = 0u; }
    }
    return plan;
}

// Display only: CombinedPeaks envelopes every channel without summing signed
// samples (opposite-polarity/spatial channels must not cancel on screen).
enum class SampleNeonWaveformDisplay : uint8_t { CombinedPeaks, FirstChannel, AllChannels };

constexpr unsigned sampleNeonWaveformRows(SampleNeonWaveformDisplay display, unsigned channels) noexcept
{
    return display == SampleNeonWaveformDisplay::AllChannels ? std::clamp(channels, 1u, 16u) : 1u;
}

// -1 requests the combined envelope; otherwise select exactly one channel.
constexpr int sampleNeonWaveformChannel(SampleNeonWaveformDisplay display, unsigned row) noexcept
{
    return display == SampleNeonWaveformDisplay::CombinedPeaks ? -1
        : display == SampleNeonWaveformDisplay::FirstChannel ? 0 : static_cast<int>(std::min(row, 15u));
}

// UI-only viewport: scrolling must never change a playback/edit cursor.
struct SampleNeonWaveformViewport {
    double start = 0.0;
    double zoom = 1.0;
    double lastCursor = -1.0;

    double width() const noexcept { return 1.0 / zoom; }
    void sync(double requestedZoom, double cursor) noexcept
    {
        requestedZoom = std::clamp(requestedZoom, 1.0, 32.0);
        const bool recenter = lastCursor < 0.0 || requestedZoom != zoom
            || (cursor != lastCursor && (cursor < start || cursor > start + width()));
        zoom = requestedZoom;
        if (recenter) start = std::clamp(cursor - width() * 0.5, 0.0, 1.0 - width());
        lastCursor = cursor;
    }
    void zoomAt(double requestedZoom, double fraction) noexcept
    {
        fraction = std::clamp(fraction, 0.0, 1.0);
        const double anchor = start + fraction * width();
        // Match the float storage used by the hardware zoom control exactly.
        zoom = static_cast<float>(std::clamp(requestedZoom, 1.0, 32.0));
        start = std::clamp(anchor - fraction * width(), 0.0, 1.0 - width());
    }
    void pan(double fraction) noexcept
    {
        start = std::clamp(start + fraction * width(), 0.0, 1.0 - width());
    }
};

// A boundary is BETWEEN frame-1 and frame. Never sum signed channels:
// opposite-polarity channels must not cancel the measured boundary energy.
// Prefer the nearest crossing shared by all channels; otherwise minimize the
// worst-channel endpoint level, then mean energy, then distance. Search is
// local (5 ms) and allocation-free, including from encoder/automation edits.
inline uint32_t sampleNeonBoundary(const SampleAsset& asset, uint32_t requested,
    uint32_t lower, uint32_t upper, uint32_t radius = 0u) noexcept
{
    const uint32_t frames = asset.frameCount();
    if (!frames || !asset.channelCount || lower > upper) return requested;
    upper = std::min(upper, frames);
    lower = std::min(lower, upper);
    requested = std::clamp(requested, lower, upper);
    if (!radius) radius = static_cast<uint32_t>(std::clamp(
        std::round(asset.sampleRate * 0.005), 1.0, 2048.0));
    const uint32_t begin = std::max(lower, requested > radius ? requested - radius : 0u);
    const uint32_t end = static_cast<uint32_t>(std::min<uint64_t>(
        upper, static_cast<uint64_t>(requested) + radius));
    uint32_t best = requested;
    bool bestShared = false;
    double bestWorst = std::numeric_limits<double>::infinity();
    double bestMean = bestWorst;
    uint32_t bestDistance = UINT32_MAX;
    for (uint32_t frame = begin; frame <= end; ++frame) {
        bool shared = true;
        double worst = 0.0, mean = 0.0;
        for (uint8_t channel = 0; channel < asset.channelCount; ++channel) {
            const float a = frame ? asset.channels[channel][frame - 1u] : 0.0f;
            const float b = frame < frames ? asset.channels[channel][frame] : 0.0f;
            // At a file edge, require the real endpoint to be quiet; padding
            // with virtual silence alone is not a meaningful zero crossing.
            const bool crosses = frame == 0u ? std::abs(b) <= 1.0e-6f
                : frame == frames ? std::abs(a) <= 1.0e-6f
                : (a <= 0.0f && b >= 0.0f) || (a >= 0.0f && b <= 0.0f);
            shared = shared && crosses;
            const double energy = static_cast<double>(a) * a + static_cast<double>(b) * b;
            worst = std::max(worst, energy);
            mean += energy;
        }
        const uint32_t distance = frame > requested ? frame - requested : requested - frame;
        const bool lowerEnergy = worst < bestWorst - 1.0e-12
            || (std::abs(worst - bestWorst) <= 1.0e-12
                && (mean < bestMean - 1.0e-12
                    || (std::abs(mean - bestMean) <= 1.0e-12 && distance < bestDistance)));
        const bool better = (shared && !bestShared) || (shared == bestShared
            && (shared ? distance < bestDistance
                || (distance == bestDistance && lowerEnergy) : lowerEnergy));
        if (better) {
            best = frame; bestShared = shared; bestWorst = worst;
            bestMean = mean; bestDistance = distance;
        }
    }
    return best;
}

// Main-thread destructive crop for review takes and committed slices. The
// source stays immutable; every channel uses the same integer-frame range.
inline SampleAsset sampleNeonCrop(const SampleAsset& asset, uint32_t start, uint32_t end)
{
    SampleAsset result;
    if (start >= end || end > asset.frameCount()) return result;
    result.sampleRate = asset.sampleRate;
    result.channelCount = asset.channelCount;
    for (uint8_t channel = 0u; channel < asset.channelCount; ++channel)
        result.channels[channel].assign(asset.channels[channel].begin() + start,
            asset.channels[channel].begin() + end);
    return result;
}

// Offline peak normalization of the whole source, not the rendered FX chain.
// A single positive scalar preserves channel ratios, polarity and ACN/SN3D.
constexpr double kSampleNeonNormalizePeak = 0.8912509381337456; // -1 dBFS
inline double sampleNeonNormalizeGain(const SampleAsset& asset) noexcept
{
    if (!asset.valid()) return 0.0;
    double peak = 0.0;
    for (uint8_t channel = 0u; channel < asset.channelCount; ++channel)
        for (float value : asset.channels[channel])
            peak = std::max(peak, std::abs(static_cast<double>(value)));
    return peak > 1.0e-12 ? kSampleNeonNormalizePeak / peak : 0.0;
}

inline SampleAsset sampleNeonNormalize(const SampleAsset& asset)
{
    const double gain = sampleNeonNormalizeGain(asset);
    if (gain == 0.0) return {};
    SampleAsset result = asset;
    for (uint8_t channel = 0u; channel < result.channelCount; ++channel)
        for (float& value : result.channels[channel])
            value = static_cast<float>(static_cast<double>(value) * gain);
    return result;
}

// Prepared off the audio thread. Recording only copies into fixed storage.
// The owner must hand off a finished recording before reading/copying it.
class SampleNeonRecorder {
public:
    static constexpr uint32_t kMaximumFrames = 48000u * 30u;
    static constexpr uint32_t kWaveBins = 4096u;
    struct WaveBin { std::atomic<float> minimum {0.0f}, maximum {0.0f}; };
    std::array<std::array<WaveBin, kWaveBins>, 16u> waveform {};
    std::atomic<uint32_t> waveCount {0u};
    std::atomic<uint32_t> waveChannels {0u};
    std::atomic<uint32_t> waveFrames {0u}, waveStride {1u};
    bool prepare(double rate) {
        if (!std::isfinite(rate) || rate <= 0.0) return false;
        rate_ = rate;
        capacity_ = static_cast<uint32_t>(std::min<double>(kMaximumFrames, rate * 30.0));
        try { for (auto& channel : channels_) channel.resize(capacity_); }
        catch (...) { return false; }
        count_ = 0u; width_ = 0u;
        waveStride.store((capacity_ + kWaveBins - 1u) / kWaveBins);
        return capacity_ > 0u;
    }
    void start(uint32_t width) noexcept {
        width_ = std::min<uint32_t>(16u, width); count_ = 0u;
        waveCount.store(0u); waveFrames.store(0u); waveChannels.store(width_);
    }
    bool append(float* const* source, uint32_t frames, uint32_t first) noexcept {
        const auto amount = std::min(frames, capacity_ - count_);
        for (uint32_t channel = 0u; channel < width_; ++channel)
            std::copy_n(source[first + channel], amount, channels_[channel].data() + count_);
        const uint32_t stride = (capacity_ + kWaveBins - 1u) / kWaveBins;
        for (uint32_t frame = 0u; frame < amount; ++frame) {
            const uint32_t absolute = count_ + frame, bin = absolute / stride;
            for (uint32_t channel = 0u; channel < width_; ++channel) {
                auto& peak = waveform[channel][bin];
                const float sample = source[first + channel][frame];
                peak.minimum.store(absolute % stride == 0u ? sample : std::min(sample, peak.minimum.load()), std::memory_order_relaxed);
                peak.maximum.store(absolute % stride == 0u ? sample : std::max(sample, peak.maximum.load()), std::memory_order_relaxed);
            }
        }
        count_ += amount;
        waveCount.store((count_ + stride - 1u) / stride, std::memory_order_release);
        waveFrames.store(count_, std::memory_order_release);
        return count_ == capacity_;
    }
    uint32_t count() const noexcept { return count_; }
    double maximumSeconds() const noexcept { return capacity_ / rate_; }
    SampleAsset finish() const {
        SampleAsset result;
        result.sampleRate = rate_;
        result.channelCount = static_cast<uint8_t>(width_);
        for (uint32_t channel = 0u; channel < width_; ++channel)
            result.channels[channel].assign(channels_[channel].begin(), channels_[channel].begin() + count_);
        return result;
    }
private:
    std::array<std::vector<float>, 16u> channels_;
    double rate_ = 48000.0;
    uint32_t capacity_ = 0u, count_ = 0u, width_ = 0u;
};

} // namespace s3g::sample
