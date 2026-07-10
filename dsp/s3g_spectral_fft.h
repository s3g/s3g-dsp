#pragma once

#include "s3g_math.h"
#include "s3g_realtime.h"

#if defined(__APPLE__)
#define S3G_HAS_ACCELERATE_FFT 1
#include <Accelerate/Accelerate.h>
#else
#define S3G_HAS_ACCELERATE_FFT 0
#endif

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace s3g {

constexpr uint32_t kSpectralFftMinSize = 64;
constexpr uint32_t kSpectralFftMaxSize = 16384;

inline bool isPowerOfTwo(uint32_t value)
{
    return value != 0u && (value & (value - 1u)) == 0u;
}

inline uint32_t log2u(uint32_t value)
{
    uint32_t result = 0u;
    while (value > 1u) {
        value >>= 1u;
        ++result;
    }
    return result;
}

struct SpectralFrameView {
    float* real = nullptr;
    float* imag = nullptr;
    uint32_t bins = 0;
    uint32_t fftSize = 0;
    uint32_t channel = 0;
};

class SpectralFftProcessor {
public:
    ~SpectralFftProcessor()
    {
        releaseSetup();
    }

    SpectralFftProcessor() = default;
    SpectralFftProcessor(const SpectralFftProcessor&) = delete;
    SpectralFftProcessor& operator=(const SpectralFftProcessor&) = delete;

    bool prepare(uint32_t channels, uint32_t fftSize, uint32_t overlap)
    {
        if (channels == 0u || !isPowerOfTwo(fftSize) || fftSize < kSpectralFftMinSize || fftSize > kSpectralFftMaxSize) {
            resetState();
            return false;
        }
        if (overlap == 0u || (fftSize % overlap) != 0u) {
            resetState();
            return false;
        }

#if !S3G_HAS_ACCELERATE_FFT
        (void)channels;
        (void)fftSize;
        (void)overlap;
        resetState();
        return false;
#else
        const uint32_t setupLog2 = log2u(fftSize);
        FFTSetup nextSetup = vDSP_create_fftsetup(setupLog2, kFFTRadix2);
        if (!nextSetup) {
            resetState();
            return false;
        }

        releaseSetup();
        fftSetup_ = nextSetup;
        channels_ = channels;
        fftSize_ = fftSize;
        halfSize_ = fftSize / 2u;
        bins_ = halfSize_ + 1u;
        overlap_ = overlap;
        hopSize_ = fftSize / overlap;
        fftLog2_ = setupLog2;
        writePos_ = 0u;
        ready_ = true;

        window_.assign(fftSize_, 0.0f);
        for (uint32_t i = 0; i < fftSize_; ++i) {
            window_[i] = 0.5f - 0.5f * std::cos((2.0f * kPi * static_cast<float>(i)) / static_cast<float>(fftSize_));
        }
        // vDSP real inverse FFT plus Hann/Hann OLA reconstructs unity with this gain.
        windowScale_ = overlap_ > 0u ? (4.0f / (3.0f * static_cast<float>(overlap_))) : 1.0f;

        frame_.assign(fftSize_, 0.0f);
        splitReal_.assign(halfSize_, 0.0f);
        splitImag_.assign(halfSize_, 0.0f);
        binReal_.assign(bins_, 0.0f);
        binImag_.assign(bins_, 0.0f);

        states_.assign(channels_, ChannelState {});
        for (auto& state : states_) {
            state.input.assign(fftSize_, 0.0f);
            state.output.assign(fftSize_, 0.0f);
        }
        return true;
#endif
    }

    void reset()
    {
        writePos_ = 0u;
        for (auto& state : states_) {
            std::fill(state.input.begin(), state.input.end(), 0.0f);
            std::fill(state.output.begin(), state.output.end(), 0.0f);
        }
    }

    uint32_t channels() const { return channels_; }
    uint32_t fftSize() const { return fftSize_; }
    uint32_t hopSize() const { return hopSize_; }
    uint32_t overlap() const { return overlap_; }
    uint32_t bins() const { return bins_; }
    bool ready() const { return ready_; }

    template <typename Kernel>
    void process(const float* const* input, float* const* output, uint32_t frames, Kernel&& kernel)
    {
        if (!ready_ || channels_ == 0u || frames == 0u) {
            return;
        }

        for (uint32_t frame = 0; frame < frames; ++frame) {
            for (uint32_t ch = 0; ch < channels_; ++ch) {
                const float sample = input && input[ch] ? input[ch][frame] : 0.0f;
                states_[ch].input[writePos_] = flushDenormal(sample);
                output[ch][frame] = flushDenormal(states_[ch].output[writePos_]);
                states_[ch].output[writePos_] = 0.0f;
            }

            ++writePos_;
            if (writePos_ >= fftSize_) {
                writePos_ = 0u;
            }

            if ((writePos_ % hopSize_) == 0u) {
                processHop(kernel);
            }
        }
    }

private:
    struct ChannelState {
        std::vector<float> input;
        std::vector<float> output;
    };

    void resetState()
    {
        releaseSetup();
        channels_ = 0u;
        fftSize_ = 0u;
        halfSize_ = 0u;
        bins_ = 0u;
        overlap_ = 0u;
        hopSize_ = 0u;
        fftLog2_ = 0u;
        writePos_ = 0u;
        windowScale_ = 1.0f;
        ready_ = false;
        window_.clear();
        frame_.clear();
        splitReal_.clear();
        splitImag_.clear();
        binReal_.clear();
        binImag_.clear();
        states_.clear();
    }

    void releaseSetup()
    {
#if S3G_HAS_ACCELERATE_FFT
        if (fftSetup_) {
            vDSP_destroy_fftsetup(fftSetup_);
            fftSetup_ = nullptr;
        }
#endif
    }

    template <typename Kernel>
    void processHop(Kernel& kernel)
    {
#if S3G_HAS_ACCELERATE_FFT
        for (uint32_t ch = 0; ch < channels_; ++ch) {
            auto& state = states_[ch];
            for (uint32_t i = 0; i < fftSize_; ++i) {
                const uint32_t index = (writePos_ + i) % fftSize_;
                frame_[i] = state.input[index] * window_[i];
            }

            DSPSplitComplex split { splitReal_.data(), splitImag_.data() };
            vDSP_ctoz(reinterpret_cast<const DSPComplex*>(frame_.data()), 2, &split, 1, halfSize_);
            vDSP_fft_zrip(fftSetup_, &split, 1, fftLog2_, FFT_FORWARD);

            binReal_[0] = splitReal_[0];
            binImag_[0] = 0.0f;
            binReal_[halfSize_] = splitImag_[0];
            binImag_[halfSize_] = 0.0f;
            for (uint32_t bin = 1u; bin < halfSize_; ++bin) {
                binReal_[bin] = splitReal_[bin];
                binImag_[bin] = splitImag_[bin];
            }

            SpectralFrameView view { binReal_.data(), binImag_.data(), bins_, fftSize_, ch };
            kernel(view);

            splitReal_[0] = binReal_[0];
            splitImag_[0] = binReal_[halfSize_];
            for (uint32_t bin = 1u; bin < halfSize_; ++bin) {
                splitReal_[bin] = binReal_[bin];
                splitImag_[bin] = binImag_[bin];
            }

            vDSP_fft_zrip(fftSetup_, &split, 1, fftLog2_, FFT_INVERSE);
            vDSP_ztoc(&split, 1, reinterpret_cast<DSPComplex*>(frame_.data()), 2, halfSize_);

            const float scale = windowScale_ / static_cast<float>(fftSize_);
            for (uint32_t i = 0; i < fftSize_; ++i) {
                const uint32_t index = (writePos_ + i) % fftSize_;
                state.output[index] += flushDenormal(frame_[i] * window_[i] * scale);
            }
        }
#else
        (void)kernel;
#endif
    }

#if S3G_HAS_ACCELERATE_FFT
    FFTSetup fftSetup_ = nullptr;
#endif
    uint32_t channels_ = 0u;
    uint32_t fftSize_ = 0u;
    uint32_t halfSize_ = 0u;
    uint32_t bins_ = 0u;
    uint32_t overlap_ = 0u;
    uint32_t hopSize_ = 0u;
    uint32_t fftLog2_ = 0u;
    uint32_t writePos_ = 0u;
    float windowScale_ = 1.0f;
    bool ready_ = false;

    std::vector<float> window_;
    std::vector<float> frame_;
    std::vector<float> splitReal_;
    std::vector<float> splitImag_;
    std::vector<float> binReal_;
    std::vector<float> binImag_;
    std::vector<ChannelState> states_;
};

} // namespace s3g
