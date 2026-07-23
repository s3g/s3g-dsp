#pragma once

#include "s3g_ambi_field_listener.h"
#include "s3g_ambisonic_speaker_decoder.h"
#include "s3g_math.h"
#include "s3g_realtime.h"

#if defined(__APPLE__)
#define S3G_HAS_AMBI_IMPRINT_FFT 1
#include <Accelerate/Accelerate.h>
#else
#define S3G_HAS_AMBI_IMPRINT_FFT 0
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace s3g {

constexpr uint32_t kAmbiImprintFormatVersion = 1u;
constexpr uint32_t kAmbiImprintMaxProfiles = 8u;
constexpr uint32_t kAmbiImprintBands = 8u;
constexpr uint32_t kAmbiImprintChannels = 64u;
constexpr uint32_t kAmbiImprintPartitionSize = 1024u;
constexpr float kAmbiImprintMaxKernelSeconds = 4.0f;
constexpr float kAmbiImprintWetHighpassHz = 25.0f;
constexpr float kAmbiImprintSparseTransferCeiling = 2.0f;
constexpr float kAmbiImprintSafetyCeiling = 0.95f;

struct AmbiImprintReflection {
    float delayMs = 0.0f;
    float gain = 0.0f;
    float azimuthDeg = 0.0f;
    float elevationDeg = 0.0f;
};

struct AmbiImprintLateProfile {
    float startMs = 40.0f;
    float durationSeconds = 2.0f;
    float level = 0.15f;
    float diffusion = 0.5f;
    float spreadDeg = 45.0f;
    float highFrequencyDamping = 0.4f;
    std::array<float, kAmbiImprintBands> absorptionByBand { 0.2f, 0.2f, 0.2f, 0.2f, 0.2f, 0.2f, 0.2f, 0.2f };
    std::array<float, kAmbiImprintBands> rt60SecondsByBand { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
    uint32_t seed = 1u;
};

struct AmbiImprintProfile {
    float azimuthDeg = 0.0f;
    float elevationDeg = 0.0f;
    float sourceXMetres = 0.0f;
    float sourceYMetres = 0.0f;
    float sourceZMetres = 0.0f;
    float weight = 0.125f;
    float directDelayMs = 0.0f;
    float directGain = 1.0f;
    std::vector<AmbiImprintReflection> earlyReflections;
    AmbiImprintLateProfile late {};
};

struct AmbiImprintRoom {
    float widthMetres = 8.0f;
    float depthMetres = 10.0f;
    float heightMetres = 3.0f;
    std::vector<std::array<float, 2>> polygon;
};

struct AmbiImprintDescriptor {
    uint32_t version = kAmbiImprintFormatVersion;
    uint32_t referenceOrder = 3u;
    float durationSeconds = 2.0f;
    AmbiImprintRoom room {};
    std::array<float, 3> listenerPositionMetres { 0.0f, 0.0f, 0.0f };
    std::vector<AmbiImprintProfile> profiles;
};

struct AmbiImprintParams {
    uint32_t order = 7u;
    float mix = 0.5f;
    float focus = 1.0f;
    float width = 1.0f;
    float outputGainDb = 0.0f;
    bool bypass = false;
    AmbiFieldListenMode fieldListenMode = AmbiFieldListenMode::Off;
};

inline AmbiImprintParams sanitizeAmbiImprintParams(AmbiImprintParams params)
{
    params.order = std::clamp<uint32_t>(params.order, 1u, 7u);
    params.mix = clamp(params.mix, 0.0f, 1.0f);
    params.focus = clamp(params.focus, 0.0f, 1.0f);
    params.width = clamp(params.width, 0.0f, 1.5f);
    params.outputGainDb = clamp(params.outputGainDb, -60.0f, 12.0f);
    params.fieldListenMode = sanitizeAmbiFieldListenMode(params.fieldListenMode);
    return params;
}

inline AmbiImprintDescriptor sanitizeAmbiImprintDescriptor(AmbiImprintDescriptor descriptor)
{
    descriptor.version = kAmbiImprintFormatVersion;
    descriptor.referenceOrder = std::clamp<uint32_t>(descriptor.referenceOrder, 1u, 7u);
    descriptor.durationSeconds = clamp(descriptor.durationSeconds, 0.05f, 12.0f);
    descriptor.room.widthMetres = clamp(descriptor.room.widthMetres, 0.5f, 1000.0f);
    descriptor.room.depthMetres = clamp(descriptor.room.depthMetres, 0.5f, 1000.0f);
    descriptor.room.heightMetres = clamp(descriptor.room.heightMetres, 0.5f, 1000.0f);
    if (descriptor.room.polygon.size() > 64u) descriptor.room.polygon.resize(64u);
    if (descriptor.profiles.size() > kAmbiImprintMaxProfiles) descriptor.profiles.resize(kAmbiImprintMaxProfiles);
    const float fallbackWeight = descriptor.profiles.empty() ? 1.0f : 1.0f / static_cast<float>(descriptor.profiles.size());
    for (auto& value : descriptor.listenerPositionMetres) {
        if (!std::isfinite(value)) value = 0.0f;
        value = clamp(value, -1000.0f, 1000.0f);
    }
    for (auto& profile : descriptor.profiles) {
        profile.azimuthDeg = std::remainder(profile.azimuthDeg, 360.0f);
        profile.elevationDeg = clamp(profile.elevationDeg, -90.0f, 90.0f);
        if (!std::isfinite(profile.sourceXMetres)) profile.sourceXMetres = 0.0f;
        if (!std::isfinite(profile.sourceYMetres)) profile.sourceYMetres = 0.0f;
        if (!std::isfinite(profile.sourceZMetres)) profile.sourceZMetres = 0.0f;
        profile.sourceXMetres = clamp(profile.sourceXMetres, -1000.0f, 1000.0f);
        profile.sourceYMetres = clamp(profile.sourceYMetres, -1000.0f, 1000.0f);
        profile.sourceZMetres = clamp(profile.sourceZMetres, -1000.0f, 1000.0f);
        profile.weight = clamp(profile.weight, 0.0f, 1.0f);
        if (profile.weight <= 0.0f) profile.weight = fallbackWeight;
        profile.directDelayMs = clamp(profile.directDelayMs, 0.0f, 12000.0f);
        profile.directGain = clamp(profile.directGain, -8.0f, 8.0f);
        if (profile.earlyReflections.size() > 128u) profile.earlyReflections.resize(128u);
        for (auto& event : profile.earlyReflections) {
            event.delayMs = clamp(event.delayMs, 0.0f, 12000.0f);
            event.gain = clamp(event.gain, -8.0f, 8.0f);
            event.azimuthDeg = std::remainder(event.azimuthDeg, 360.0f);
            event.elevationDeg = clamp(event.elevationDeg, -90.0f, 90.0f);
        }
        profile.late.startMs = clamp(profile.late.startMs, 0.0f, 12000.0f);
        profile.late.durationSeconds = clamp(profile.late.durationSeconds, 0.05f, 12.0f);
        profile.late.level = clamp(profile.late.level, 0.0f, 2.0f);
        profile.late.diffusion = clamp(profile.late.diffusion, 0.0f, 1.0f);
        profile.late.spreadDeg = clamp(profile.late.spreadDeg, 0.0f, 180.0f);
        profile.late.highFrequencyDamping = clamp(profile.late.highFrequencyDamping, 0.0f, 1.0f);
        for (auto& value : profile.late.absorptionByBand) value = clamp(value, 0.0f, 0.999f);
        for (auto& value : profile.late.rt60SecondsByBand) value = clamp(value, 0.05f, 12.0f);
        if (profile.late.seed == 0u) profile.late.seed = 1u;
    }
    return descriptor;
}

namespace ambi_imprint_detail {

inline uint32_t xorshift32(uint32_t& state)
{
    if (state == 0u) state = 0x6d2b79f5u;
    state ^= state << 13u;
    state ^= state >> 17u;
    state ^= state << 5u;
    return state;
}

inline float bipolarNoise(uint32_t& state)
{
    return static_cast<float>(xorshift32(state) >> 8u) * (1.0f / 8388607.5f) - 1.0f;
}

struct Biquad {
    float b0 = 1.0f;
    float b1 = 0.0f;
    float b2 = 0.0f;
    float a1 = 0.0f;
    float a2 = 0.0f;
    float z1 = 0.0f;
    float z2 = 0.0f;

    void reset()
    {
        z1 = 0.0f;
        z2 = 0.0f;
    }

    void prepareHighpass(double sampleRate, float frequency, float q = 0.70710678f)
    {
        const float safeRate = static_cast<float>(std::max(1.0, sampleRate));
        frequency = clamp(frequency, 2.0f, safeRate * 0.44f);
        q = clamp(q, 0.35f, 4.0f);
        const float omega = 2.0f * kPi * frequency / safeRate;
        const float cosine = std::cos(omega);
        const float alpha = std::sin(omega) / (2.0f * q);
        const float a0 = 1.0f + alpha;
        b0 = 0.5f * (1.0f + cosine) / a0;
        b1 = -(1.0f + cosine) / a0;
        b2 = b0;
        a1 = -2.0f * cosine / a0;
        a2 = (1.0f - alpha) / a0;
        reset();
    }

    void prepareBandpass(double sampleRate, float frequency, float q)
    {
        const float safeRate = static_cast<float>(std::max(1.0, sampleRate));
        frequency = clamp(frequency, 20.0f, safeRate * 0.44f);
        q = clamp(q, 0.35f, 4.0f);
        const float omega = 2.0f * kPi * frequency / safeRate;
        const float alpha = std::sin(omega) / (2.0f * q);
        const float a0 = 1.0f + alpha;
        b0 = alpha / a0;
        b1 = 0.0f;
        b2 = -alpha / a0;
        a1 = -2.0f * std::cos(omega) / a0;
        a2 = (1.0f - alpha) / a0;
    }

    float process(float input)
    {
        const float output = b0 * input + z1;
        z1 = b1 * input - a1 * output + z2;
        z2 = b2 * input - a2 * output;
        return flushDenormal(output);
    }
};

inline std::vector<float> buildKernel(const AmbiImprintProfile& source, double sampleRate, float maximumSeconds, bool includeDiscrete = true)
{
    AmbiImprintDescriptor temporary;
    temporary.durationSeconds = maximumSeconds;
    temporary.profiles = { source };
    const AmbiImprintProfile profile = sanitizeAmbiImprintDescriptor(std::move(temporary)).profiles.front();
    const float duration = std::min({ maximumSeconds, profile.late.durationSeconds, kAmbiImprintMaxKernelSeconds });
    const uint32_t frames = std::max<uint32_t>(1u, static_cast<uint32_t>(std::ceil(duration * sampleRate)));
    std::vector<float> kernel(frames, 0.0f);
    auto addTap = [&](float delayMs, float gain) {
        const uint32_t index = static_cast<uint32_t>(std::llround(static_cast<double>(delayMs) * 0.001 * sampleRate));
        if (index < kernel.size()) kernel[index] += gain;
    };
    if (includeDiscrete) {
        addTap(profile.directDelayMs, profile.directGain);
        for (const auto& reflection : profile.earlyReflections) addTap(reflection.delayMs, reflection.gain);
    }

    const uint32_t lateStart = std::min<uint32_t>(frames, static_cast<uint32_t>(std::llround(profile.late.startMs * 0.001 * sampleRate)));
    std::vector<float> tail(frames, 0.0f);
    constexpr std::array<float, kAmbiImprintBands> frequencies { 125.0f, 250.0f, 500.0f, 1000.0f, 2000.0f, 4000.0f, 8000.0f, 16000.0f };
    const float q = 0.62f + (1.0f - profile.late.diffusion) * 0.92f;
    for (uint32_t band = 0; band < kAmbiImprintBands; ++band) {
        Biquad filter;
        filter.prepareBandpass(sampleRate, frequencies[band], q);
        uint32_t random = profile.late.seed ^ (0x9e3779b9u * (band + 1u));
        const float rt60 = profile.late.rt60SecondsByBand[band];
        const float high = static_cast<float>(band) / static_cast<float>(kAmbiImprintBands - 1u);
        const float spectral = std::sqrt(std::max(0.001f, 1.0f - profile.late.absorptionByBand[band]))
            * (1.0f - profile.late.highFrequencyDamping * high * 0.58f);
        for (uint32_t i = lateStart; i < frames; ++i) {
            const float t = static_cast<float>(i - lateStart) / static_cast<float>(sampleRate);
            const float envelope = std::exp(-6.907755f * t / rt60);
            tail[i] += filter.process(bipolarNoise(random)) * envelope * spectral;
        }
    }

    double tailEnergy = 0.0;
    for (uint32_t i = lateStart; i < frames; ++i) tailEnergy += static_cast<double>(tail[i]) * static_cast<double>(tail[i]);
    const float targetTailEnergy = clamp(profile.late.level * 3.0f, 0.0f, 1.5f);
    const float tailScale = tailEnergy > 1.0e-12 ? targetTailEnergy / static_cast<float>(std::sqrt(tailEnergy)) : 0.0f;
    for (uint32_t i = lateStart; i < frames; ++i) kernel[i] += tail[i] * tailScale;

    float peak = 0.0f;
    double energy = 0.0;
    for (float value : kernel) {
        peak = std::max(peak, std::abs(value));
        energy += static_cast<double>(value) * static_cast<double>(value);
    }
    float scale = peak > 1.0f ? 1.0f / peak : 1.0f;
    if (energy > 4.0) scale = std::min(scale, 2.0f / static_cast<float>(std::sqrt(energy)));
    if (scale < 1.0f) for (auto& value : kernel) value *= scale;
    return kernel;
}

} // namespace ambi_imprint_detail

class AmbiImprintPartitionedConvolver {
public:
    AmbiImprintPartitionedConvolver() = default;
    ~AmbiImprintPartitionedConvolver() { release(); }
    AmbiImprintPartitionedConvolver(const AmbiImprintPartitionedConvolver&) = delete;
    AmbiImprintPartitionedConvolver& operator=(const AmbiImprintPartitionedConvolver&) = delete;

    bool prepare(const std::vector<float>& kernel, uint32_t partitionSize = kAmbiImprintPartitionSize)
    {
#if !S3G_HAS_AMBI_IMPRINT_FFT
        (void)kernel;
        (void)partitionSize;
        return false;
#else
        if (kernel.empty() || partitionSize < 64u || (partitionSize & (partitionSize - 1u)) != 0u) return false;
        release();
        partitionSize_ = partitionSize;
        fftSize_ = partitionSize_ * 2u;
        halfSize_ = fftSize_ / 2u;
        uint32_t value = fftSize_;
        while (value > 1u) { value >>= 1u; ++log2Size_; }
        setup_ = vDSP_create_fftsetup(log2Size_, kFFTRadix2);
        if (!setup_) { release(); return false; }
        partitions_ = static_cast<uint32_t>((kernel.size() + partitionSize_ - 1u) / partitionSize_);
        overlap_.assign(partitionSize_, 0.0f);
        inputBlock_.assign(partitionSize_, 0.0f);
        outputBlock_.assign(partitionSize_, 0.0f);
        frame_.assign(fftSize_, 0.0f);
        splitReal_.assign(halfSize_, 0.0f);
        splitImag_.assign(halfSize_, 0.0f);
        accumulatorReal_.assign(halfSize_, 0.0f);
        accumulatorImag_.assign(halfSize_, 0.0f);
        kernelReal_.assign(static_cast<size_t>(partitions_) * halfSize_, 0.0f);
        kernelImag_.assign(static_cast<size_t>(partitions_) * halfSize_, 0.0f);
        historyReal_.assign(static_cast<size_t>(partitions_) * halfSize_, 0.0f);
        historyImag_.assign(static_cast<size_t>(partitions_) * halfSize_, 0.0f);

        for (uint32_t partition = 0; partition < partitions_; ++partition) {
            std::fill(frame_.begin(), frame_.end(), 0.0f);
            const uint32_t offset = partition * partitionSize_;
            const uint32_t count = std::min<uint32_t>(partitionSize_, static_cast<uint32_t>(kernel.size()) - offset);
            std::copy_n(kernel.data() + offset, count, frame_.data());
            forward(frame_.data(), splitReal_.data(), splitImag_.data());
            std::copy(splitReal_.begin(), splitReal_.end(), kernelReal_.begin() + static_cast<size_t>(partition) * halfSize_);
            std::copy(splitImag_.begin(), splitImag_.end(), kernelImag_.begin() + static_cast<size_t>(partition) * halfSize_);
        }
        reset();
        return true;
#endif
    }

    void reset()
    {
        inputPosition_ = 0u;
        historyPosition_ = 0u;
        std::fill(overlap_.begin(), overlap_.end(), 0.0f);
        std::fill(inputBlock_.begin(), inputBlock_.end(), 0.0f);
        std::fill(outputBlock_.begin(), outputBlock_.end(), 0.0f);
        std::fill(historyReal_.begin(), historyReal_.end(), 0.0f);
        std::fill(historyImag_.begin(), historyImag_.end(), 0.0f);
    }

    float processSample(float input)
    {
        if (!ready()) return 0.0f;
        const float output = outputBlock_[inputPosition_];
        inputBlock_[inputPosition_] = flushDenormal(input);
        ++inputPosition_;
        if (inputPosition_ >= partitionSize_) {
            processBlock();
            inputPosition_ = 0u;
        }
        return flushDenormal(output);
    }

    bool ready() const
    {
#if S3G_HAS_AMBI_IMPRINT_FFT
        return setup_ != nullptr && partitions_ > 0u;
#else
        return false;
#endif
    }
    uint32_t latencyFrames() const { return partitionSize_; }

private:
    void release()
    {
#if S3G_HAS_AMBI_IMPRINT_FFT
        if (setup_) vDSP_destroy_fftsetup(setup_);
        setup_ = nullptr;
#endif
        partitionSize_ = 0u;
        fftSize_ = 0u;
        halfSize_ = 0u;
        partitions_ = 0u;
        log2Size_ = 0u;
        inputPosition_ = 0u;
        historyPosition_ = 0u;
        overlap_.clear();
        inputBlock_.clear();
        outputBlock_.clear();
        frame_.clear();
        splitReal_.clear();
        splitImag_.clear();
        accumulatorReal_.clear();
        accumulatorImag_.clear();
        kernelReal_.clear();
        kernelImag_.clear();
        historyReal_.clear();
        historyImag_.clear();
    }

#if S3G_HAS_AMBI_IMPRINT_FFT
    void forward(float* time, float* real, float* imag)
    {
        DSPSplitComplex split { real, imag };
        vDSP_ctoz(reinterpret_cast<const DSPComplex*>(time), 2, &split, 1, halfSize_);
        vDSP_fft_zrip(setup_, &split, 1, log2Size_, FFT_FORWARD);
    }

    void inverse(float* real, float* imag, float* time)
    {
        DSPSplitComplex split { real, imag };
        vDSP_fft_zrip(setup_, &split, 1, log2Size_, FFT_INVERSE);
        vDSP_ztoc(&split, 1, reinterpret_cast<DSPComplex*>(time), 2, halfSize_);
    }

    void processBlock()
    {
        std::copy(overlap_.begin(), overlap_.end(), frame_.begin());
        std::copy(inputBlock_.begin(), inputBlock_.end(), frame_.begin() + partitionSize_);
        std::copy(inputBlock_.begin(), inputBlock_.end(), overlap_.begin());

        float* historyReal = historyReal_.data() + static_cast<size_t>(historyPosition_) * halfSize_;
        float* historyImag = historyImag_.data() + static_cast<size_t>(historyPosition_) * halfSize_;
        forward(frame_.data(), historyReal, historyImag);
        std::fill(accumulatorReal_.begin(), accumulatorReal_.end(), 0.0f);
        std::fill(accumulatorImag_.begin(), accumulatorImag_.end(), 0.0f);

        for (uint32_t partition = 0; partition < partitions_; ++partition) {
            const uint32_t historyIndex = (historyPosition_ + partitions_ - partition) % partitions_;
            float* xr = historyReal_.data() + static_cast<size_t>(historyIndex) * halfSize_;
            float* xi = historyImag_.data() + static_cast<size_t>(historyIndex) * halfSize_;
            float* hr = kernelReal_.data() + static_cast<size_t>(partition) * halfSize_;
            float* hi = kernelImag_.data() + static_cast<size_t>(partition) * halfSize_;
            accumulatorReal_[0] += xr[0] * hr[0];
            accumulatorImag_[0] += xi[0] * hi[0];
            if (halfSize_ > 1u) {
                DSPSplitComplex x { xr + 1u, xi + 1u };
                DSPSplitComplex h { hr + 1u, hi + 1u };
                DSPSplitComplex accumulator { accumulatorReal_.data() + 1u, accumulatorImag_.data() + 1u };
                vDSP_zvma(&x, 1, &h, 1, &accumulator, 1, &accumulator, 1, halfSize_ - 1u);
            }
        }

        inverse(accumulatorReal_.data(), accumulatorImag_.data(), frame_.data());
        const float scale = 1.0f / static_cast<float>(4u * fftSize_);
        for (uint32_t i = 0; i < partitionSize_; ++i) outputBlock_[i] = flushDenormal(frame_[partitionSize_ + i] * scale);
        historyPosition_ = (historyPosition_ + 1u) % partitions_;
    }

    FFTSetup setup_ = nullptr;
#endif
    uint32_t partitionSize_ = 0u;
    uint32_t fftSize_ = 0u;
    uint32_t halfSize_ = 0u;
    uint32_t partitions_ = 0u;
    uint32_t log2Size_ = 0u;
    uint32_t inputPosition_ = 0u;
    uint32_t historyPosition_ = 0u;
    std::vector<float> overlap_;
    std::vector<float> inputBlock_;
    std::vector<float> outputBlock_;
    std::vector<float> frame_;
    std::vector<float> splitReal_;
    std::vector<float> splitImag_;
    std::vector<float> accumulatorReal_;
    std::vector<float> accumulatorImag_;
    std::vector<float> kernelReal_;
    std::vector<float> kernelImag_;
    std::vector<float> historyReal_;
    std::vector<float> historyImag_;
};

class AmbiImprintProcessor {
public:
    bool prepare(double sampleRate, AmbiImprintDescriptor descriptor)
    {
        sampleRate_ = std::max(1.0, sampleRate);
        descriptor_ = sanitizeAmbiImprintDescriptor(std::move(descriptor));
        float directTransfer = 0.0f;
        float reflectionTransfer = 0.0f;
        for (const auto& profile : descriptor_.profiles) {
            directTransfer += profile.weight * std::abs(profile.directGain);
            for (const auto& reflection : profile.earlyReflections) {
                reflectionTransfer += profile.weight * std::abs(reflection.gain);
            }
        }
        directTapScale_ = directTransfer > kAmbiImprintSparseTransferCeiling
            ? kAmbiImprintSparseTransferCeiling / directTransfer
            : 1.0f;
        const float directAfterScale = directTransfer * directTapScale_;
        const float reflectionBudget = std::max(0.0f, kAmbiImprintSparseTransferCeiling - directAfterScale);
        reflectionTapScale_ = reflectionTransfer > reflectionBudget && reflectionTransfer > 1.0e-9f
            ? reflectionBudget / reflectionTransfer
            : 1.0f;
        profiles_.clear();
        for (const auto& profile : descriptor_.profiles) {
            RuntimeProfile runtime;
            runtime.weight = profile.weight;
            const Vec3 direction = directionFromAed(profile.azimuthDeg, profile.elevationDeg);
            runtime.direction = direction;
            runtime.basis = acnSn3dBasis7(direction);
            const auto kernel = ambi_imprint_detail::buildKernel(profile, sampleRate_, descriptor_.durationSeconds, false);
            runtime.convolver = std::make_unique<AmbiImprintPartitionedConvolver>();
            if (!runtime.convolver->prepare(kernel)) return false;
            profiles_.push_back(std::move(runtime));
        }
        std::array<Vec3, kAmbiImprintMaxProfiles> listenerDirections {};
        for (uint32_t profile = 0u; profile < profiles_.size(); ++profile) {
            listenerDirections[profile] = profiles_[profile].direction;
        }
        fieldListener_.prepare(sampleRate_);
        fieldListener_.setMemorySeconds(0.42f);
        fieldListener_.setDirections(
            listenerDirections.data(), static_cast<uint32_t>(profiles_.size()));
        const uint32_t maximumResponseFrames = static_cast<uint32_t>(std::ceil(
            std::min(descriptor_.durationSeconds, kAmbiImprintMaxKernelSeconds) * sampleRate_));
        for (uint32_t sourceIndex = 0; sourceIndex < profiles_.size(); ++sourceIndex) {
            auto& runtime = profiles_[sourceIndex];
            const auto& source = descriptor_.profiles[sourceIndex];
            uint32_t maximumDelay = kAmbiImprintPartitionSize;
            auto appendTap = [&](float delayMs, float gain, uint32_t outputProfile) {
                const uint32_t physicalDelay = static_cast<uint32_t>(std::llround(delayMs * 0.001 * sampleRate_));
                if (physicalDelay > maximumResponseFrames || std::abs(gain) < 1.0e-9f) return;
                const uint32_t delay = kAmbiImprintPartitionSize + physicalDelay;
                runtime.sparseTaps.push_back({ delay, outputProfile, gain });
                maximumDelay = std::max(maximumDelay, delay);
            };
            appendTap(source.directDelayMs, source.directGain * directTapScale_, sourceIndex);
            for (const auto& reflection : source.earlyReflections) {
                const Vec3 eventDirection = directionFromAed(reflection.azimuthDeg, reflection.elevationDeg);
                uint32_t first = 0u;
                uint32_t second = profiles_.size() > 1u ? 1u : 0u;
                float firstDot = -2.0f;
                float secondDot = -2.0f;
                for (uint32_t outputProfile = 0; outputProfile < profiles_.size(); ++outputProfile) {
                    const Vec3 candidate = profiles_[outputProfile].direction;
                    const float dot = eventDirection.x * candidate.x + eventDirection.y * candidate.y + eventDirection.z * candidate.z;
                    if (dot > firstDot) {
                        second = first;
                        secondDot = firstDot;
                        first = outputProfile;
                        firstDot = dot;
                    } else if (dot > secondDot) {
                        second = outputProfile;
                        secondDot = dot;
                    }
                }
                if (profiles_.size() == 1u) {
                    appendTap(reflection.delayMs, reflection.gain * reflectionTapScale_, first);
                } else {
                    const float firstWeight = std::pow(std::max(0.001f, 1.0f + firstDot), 4.0f);
                    const float secondWeight = std::pow(std::max(0.001f, 1.0f + secondDot), 4.0f);
                    const float inverse = 1.0f / std::max(1.0e-9f, firstWeight + secondWeight);
                    appendTap(reflection.delayMs, reflection.gain * reflectionTapScale_ * firstWeight * inverse, first);
                    appendTap(reflection.delayMs, reflection.gain * reflectionTapScale_ * secondWeight * inverse, second);
                }
            }
            runtime.sparseDelay.assign(static_cast<size_t>(maximumDelay) + 1u, 0.0f);
            runtime.sparsePosition = 0u;
        }
        dryDelay_.assign(kAmbiImprintChannels, std::vector<float>(kAmbiImprintPartitionSize, 0.0f));
        dryPosition_ = 0u;
        for (auto& filter : inputHighpass_) filter.prepareHighpass(sampleRate_, kAmbiImprintWetHighpassHz);
        inputSafetyGain_ = 1.0f;
        wetSafetyGain_ = 1.0f;
        inputSafetyRelease_ = 1.0f - std::exp(-1.0f / static_cast<float>(sampleRate_ * 0.080));
        wetSafetyRelease_ = 1.0f - std::exp(-1.0f / static_cast<float>(sampleRate_ * 0.120));
        listenWeightCoefficient_ = 1.0f - std::exp(
            -1.0f / static_cast<float>(sampleRate_ * 0.52));
        listenModeCoefficient_ = 1.0f - std::exp(
            -1.0f / static_cast<float>(sampleRate_ * 0.045));
        params_ = sanitizeAmbiImprintParams(params_);
        currentMix_ = params_.mix;
        currentFocus_ = params_.focus;
        currentWidth_ = params_.width;
        currentOutput_ = dbToGain(params_.outputGainDb);
        currentListenMix_ =
            params_.fieldListenMode == AmbiFieldListenMode::Off ? 0.0f : 1.0f;
        currentProfileWeight_.fill(1.0f);
        targetProfileWeight_.fill(1.0f);
        return S3G_HAS_AMBI_IMPRINT_FFT != 0;
    }

    void setParams(AmbiImprintParams params) { params_ = sanitizeAmbiImprintParams(params); }
    const AmbiImprintParams& params() const { return params_; }
    const AmbiImprintDescriptor& descriptor() const { return descriptor_; }
    uint32_t profileCount() const { return static_cast<uint32_t>(profiles_.size()); }
    uint32_t latencyFrames() const { return kAmbiImprintPartitionSize; }
    uint32_t tailFrames() const { return static_cast<uint32_t>(std::ceil(std::min(descriptor_.durationSeconds, kAmbiImprintMaxKernelSeconds) * sampleRate_)) + latencyFrames(); }
    float directTapScale() const { return directTapScale_; }
    float reflectionTapScale() const { return reflectionTapScale_; }
    float fieldListenEnvelope(uint32_t profile) const
    {
        return profile < profiles_.size() ? fieldListener_.envelope(profile) : 0.0f;
    }
    float fieldListenWeight(uint32_t profile) const
    {
        return profile < profiles_.size()
            ? lerp(1.0f, currentProfileWeight_[profile], currentListenMix_)
            : 1.0f;
    }

    void reset()
    {
        for (auto& profile : profiles_) {
            profile.convolver->reset();
            std::fill(profile.sparseDelay.begin(), profile.sparseDelay.end(), 0.0f);
            profile.sparsePosition = 0u;
        }
        for (auto& channel : dryDelay_) std::fill(channel.begin(), channel.end(), 0.0f);
        dryPosition_ = 0u;
        for (auto& filter : inputHighpass_) filter.reset();
        fieldListener_.reset();
        inputSafetyGain_ = 1.0f;
        wetSafetyGain_ = 1.0f;
        currentMix_ = params_.mix;
        currentFocus_ = params_.focus;
        currentWidth_ = params_.width;
        currentOutput_ = dbToGain(params_.outputGainDb);
        currentListenMix_ =
            params_.fieldListenMode == AmbiFieldListenMode::Off ? 0.0f : 1.0f;
        currentProfileWeight_.fill(1.0f);
        targetProfileWeight_.fill(1.0f);
    }

    template <typename Sample>
    void process(Sample** input, uint32_t inputChannels, Sample** output, uint32_t outputChannels, uint32_t frames)
    {
        if (!output || outputChannels == 0u || frames == 0u) return;
        const uint32_t activeChannels = std::min<uint32_t>({ inputChannels, outputChannels, (params_.order + 1u) * (params_.order + 1u), kAmbiImprintChannels });
        const bool hasProfiles = !profiles_.empty();
        for (uint32_t frame = 0; frame < frames; ++frame) {
            currentMix_ += ((params_.bypass || !hasProfiles ? 0.0f : params_.mix) - currentMix_) * 0.0018f;
            currentFocus_ += (params_.focus - currentFocus_) * 0.0018f;
            currentWidth_ += (params_.width - currentWidth_) * 0.0018f;
            currentOutput_ += (dbToGain(params_.outputGainDb) - currentOutput_) * 0.0018f;
            const float listenTarget =
                params_.fieldListenMode == AmbiFieldListenMode::Off ? 0.0f : 1.0f;
            currentListenMix_ +=
                (listenTarget - currentListenMix_) * listenModeCoefficient_;
            for (uint32_t profile = 0u; profile < profiles_.size(); ++profile) {
                currentProfileWeight_[profile] +=
                    (targetProfileWeight_[profile] - currentProfileWeight_[profile])
                    * listenWeightCoefficient_;
            }
            wet_.fill(0.0f);
            wetBins_.fill(0.0f);

            std::array<float, 4u> excitation {};
            float inputPeak = 0.0f;
            for (uint32_t channel = 0u; channel < excitation.size(); ++channel) {
                float value = inputChannels > channel && input && input[channel]
                    ? static_cast<float>(input[channel][frame])
                    : 0.0f;
                if (!std::isfinite(value)) value = 0.0f;
                excitation[channel] = inputHighpass_[channel].process(value);
                inputPeak = std::max(inputPeak, std::abs(excitation[channel]));
            }
            const float inputSafetyTarget = inputPeak > kAmbiImprintSafetyCeiling
                ? kAmbiImprintSafetyCeiling / inputPeak
                : 1.0f;
            if (inputSafetyTarget < inputSafetyGain_) inputSafetyGain_ = inputSafetyTarget;
            else inputSafetyGain_ += (inputSafetyTarget - inputSafetyGain_) * inputSafetyRelease_;
            for (auto& value : excitation) value *= inputSafetyGain_;
            const float w = excitation[0];
            const float y = excitation[1];
            const float z = excitation[2];
            const float x = excitation[3];
            for (uint32_t sourceIndex = 0; sourceIndex < profiles_.size(); ++sourceIndex) {
                auto& profile = profiles_[sourceIndex];
                const float beam = w + currentFocus_ * 3.0f * (y * profile.basis[1] + z * profile.basis[2] + x * profile.basis[3]);
                if (!profile.sparseDelay.empty()) {
                    profile.sparseDelay[profile.sparsePosition] = beam;
                    for (const auto& tap : profile.sparseTaps) {
                        const uint32_t size = static_cast<uint32_t>(profile.sparseDelay.size());
                        const uint32_t readPosition = (profile.sparsePosition + size - (tap.delayFrames % size)) % size;
                        wetBins_[tap.outputProfile] += profile.sparseDelay[readPosition] * tap.gain * profile.weight;
                    }
                    profile.sparsePosition = (profile.sparsePosition + 1u) % static_cast<uint32_t>(profile.sparseDelay.size());
                }
                wetBins_[sourceIndex] += profile.convolver->processSample(beam) * profile.weight;
            }
            for (uint32_t outputProfile = 0; outputProfile < profiles_.size(); ++outputProfile) {
                const float listenWeight = lerp(
                    1.0f, currentProfileWeight_[outputProfile], currentListenMix_);
                const float response = wetBins_[outputProfile] * listenWeight;
                for (uint32_t channel = 0; channel < activeChannels; ++channel) wet_[channel] += response * profiles_[outputProfile].basis[channel];
            }

            float wetPeak = 0.0f;
            for (uint32_t channel = 0u; channel < activeChannels; ++channel) {
                const uint32_t order = static_cast<uint32_t>(std::sqrt(static_cast<float>(channel)));
                const float width = order == 0u ? 1.0f : std::pow(currentWidth_, static_cast<float>(order) / static_cast<float>(std::max<uint32_t>(1u, params_.order)));
                float value = wet_[channel] * width;
                if (!std::isfinite(value)) value = 0.0f;
                wetShaped_[channel] = value;
                wetPeak = std::max(wetPeak, std::abs(value));
            }
            fieldListener_.processFrame(wetShaped_.data(), activeChannels);
            updateFieldListenTargets();
            const float wetSafetyTarget = wetPeak > kAmbiImprintSafetyCeiling
                ? kAmbiImprintSafetyCeiling / wetPeak
                : 1.0f;
            if (wetSafetyTarget < wetSafetyGain_) wetSafetyGain_ = wetSafetyTarget;
            else wetSafetyGain_ += (wetSafetyTarget - wetSafetyGain_) * wetSafetyRelease_;

            for (uint32_t channel = 0; channel < outputChannels; ++channel) {
                if (!output[channel]) continue;
                float in = channel < inputChannels && input && input[channel] ? static_cast<float>(input[channel][frame]) : 0.0f;
                if (!std::isfinite(in)) in = 0.0f;
                float dry = in;
                if (channel < kAmbiImprintChannels && !dryDelay_.empty()) {
                    dry = dryDelay_[channel][dryPosition_];
                    dryDelay_[channel][dryPosition_] = in;
                }
                if (channel < activeChannels) {
                    float value = lerp(dry, wetShaped_[channel] * wetSafetyGain_, currentMix_) * currentOutput_;
                    if (!std::isfinite(value)) value = 0.0f;
                    output[channel][frame] = static_cast<Sample>(std::clamp(value, -8.0f, 8.0f));
                } else {
                    output[channel][frame] = Sample {};
                }
            }
            dryPosition_ = (dryPosition_ + 1u) % kAmbiImprintPartitionSize;
        }
    }

private:
    void updateFieldListenTargets()
    {
        const uint32_t count = static_cast<uint32_t>(profiles_.size());
        if (params_.fieldListenMode == AmbiFieldListenMode::Off || count == 0u) {
            targetProfileWeight_.fill(1.0f);
            return;
        }

        std::array<float, kAmbiImprintMaxProfiles> energy {};
        float mean = 0.0f;
        for (uint32_t profile = 0u; profile < count; ++profile) {
            energy[profile] = std::max(0.0f, fieldListener_.envelope(profile));
            mean += energy[profile];
        }
        mean /= static_cast<float>(count);
        if (mean < 1.0e-7f) {
            targetProfileWeight_.fill(1.0f);
            return;
        }

        std::array<float, kAmbiImprintMaxProfiles> raw {};
        float baseTotal = 0.0f;
        float weightedTotal = 0.0f;
        for (uint32_t profile = 0u; profile < count; ++profile) {
            float observed = energy[profile];
            float polarity = 1.0f;
            float strength = 0.64f;
            if (params_.fieldListenMode == AmbiFieldListenMode::Counter) {
                float opposing = 0.0f;
                float opposingWeight = 0.0f;
                const Vec3 direction = profiles_[profile].direction;
                for (uint32_t other = 0u; other < count; ++other) {
                    const Vec3 candidate = profiles_[other].direction;
                    const float antipodal = std::max(0.0f,
                        -(direction.x * candidate.x
                            + direction.y * candidate.y
                            + direction.z * candidate.z));
                    const float kernel = antipodal * antipodal * antipodal * antipodal;
                    opposing += energy[other] * kernel;
                    opposingWeight += kernel;
                }
                observed = opposingWeight > 1.0e-4f
                    ? opposing / opposingWeight
                    : std::max(0.0f, mean * 2.0f - energy[profile]);
                strength = 0.72f;
            } else if (params_.fieldListenMode == AmbiFieldListenMode::Balance) {
                polarity = -1.0f;
                strength = 0.54f;
            }
            const float contrast = clamp(
                (observed - mean) / std::max(1.0e-6f, mean), -1.0f, 1.0f);
            raw[profile] = std::exp(clamp(
                polarity * strength * contrast, -0.58f, 0.58f));
            baseTotal += profiles_[profile].weight;
            weightedTotal += profiles_[profile].weight * raw[profile];
        }
        const float normalization = baseTotal
            / std::max(1.0e-6f, weightedTotal);
        for (uint32_t profile = 0u; profile < count; ++profile) {
            targetProfileWeight_[profile] =
                clamp(raw[profile] * normalization, 0.40f, 2.50f);
        }
        for (uint32_t profile = count; profile < kAmbiImprintMaxProfiles; ++profile) {
            targetProfileWeight_[profile] = 1.0f;
        }
    }

    struct SparseTap {
        uint32_t delayFrames = 0u;
        uint32_t outputProfile = 0u;
        float gain = 0.0f;
    };

    struct RuntimeProfile {
        float weight = 0.125f;
        Vec3 direction {};
        std::array<float, kAmbiImprintChannels> basis {};
        std::unique_ptr<AmbiImprintPartitionedConvolver> convolver;
        std::vector<SparseTap> sparseTaps;
        std::vector<float> sparseDelay;
        uint32_t sparsePosition = 0u;
    };

    double sampleRate_ = 48000.0;
    AmbiImprintDescriptor descriptor_ {};
    AmbiImprintParams params_ {};
    std::vector<RuntimeProfile> profiles_;
    std::vector<std::vector<float>> dryDelay_;
    uint32_t dryPosition_ = 0u;
    std::array<float, kAmbiImprintChannels> wet_ {};
    std::array<float, kAmbiImprintChannels> wetShaped_ {};
    std::array<float, kAmbiImprintMaxProfiles> wetBins_ {};
    AmbiFieldListener fieldListener_ {};
    std::array<float, kAmbiImprintMaxProfiles> currentProfileWeight_ {};
    std::array<float, kAmbiImprintMaxProfiles> targetProfileWeight_ {};
    std::array<ambi_imprint_detail::Biquad, 4u> inputHighpass_ {};
    float directTapScale_ = 1.0f;
    float reflectionTapScale_ = 1.0f;
    float inputSafetyGain_ = 1.0f;
    float wetSafetyGain_ = 1.0f;
    float inputSafetyRelease_ = 0.001f;
    float wetSafetyRelease_ = 0.001f;
    float currentMix_ = 0.5f;
    float currentFocus_ = 1.0f;
    float currentWidth_ = 1.0f;
    float currentOutput_ = 1.0f;
    float currentListenMix_ = 0.0f;
    float listenWeightCoefficient_ = 0.00004f;
    float listenModeCoefficient_ = 0.0005f;
};

} // namespace s3g
