#pragma once

#include "s3g_array_delay.h"
#include "s3g_array_hpf.h"
#include "s3g_array_trim.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace s3g {

constexpr uint32_t kArrayCalibrateMaxChannels = 64;

struct ArrayCalibrateParams {
    uint32_t activeChannels = 16;
    float outputGainDb = 0.0f;
    bool bypass = false;

    float cutoffHz = 90.0f;
    uint32_t poles = 2;
    bool hpfBypass = false;

    bool delayBypass = false;
    std::array<float, kArrayCalibrateMaxChannels> delayMs {};

    bool trimBypass = false;
    std::array<float, kArrayCalibrateMaxChannels> gainDb {};
    std::array<uint8_t, kArrayCalibrateMaxChannels> mute {};
    std::array<uint8_t, kArrayCalibrateMaxChannels> invert {};
};

// Fixed-order speaker calibration chain: HPF -> delay -> trim/output.
// The scratch buffers are allocated during prepare and reused by processBlock.
class ArrayCalibrate {
public:
    void prepare(double sampleRate, uint32_t maxFrames = 4096u)
    {
        maxFrames_ = std::max<uint32_t>(1u, maxFrames);
        for (uint32_t ch = 0; ch < kArrayCalibrateMaxChannels; ++ch) {
            hpfScratch_[ch].assign(maxFrames_, 0.0f);
            delayScratch_[ch].assign(maxFrames_, 0.0f);
            hpfPointers_[ch] = hpfScratch_[ch].data();
            delayPointers_[ch] = delayScratch_[ch].data();
        }
        hpf_.prepare(sampleRate);
        delay_.prepare(sampleRate);
        trim_.prepare(sampleRate);
        prepared_ = true;
        syncStages();
        reset();
    }

    void reset()
    {
        hpf_.reset();
        delay_.reset();
        trim_.reset();
    }

    void setParams(ArrayCalibrateParams params)
    {
        params.activeChannels = std::clamp<uint32_t>(params.activeChannels, 1u, kArrayCalibrateMaxChannels);
        params.outputGainDb = clamp(params.outputGainDb, -60.0f, 18.0f);
        params.cutoffHz = clamp(params.cutoffHz, 20.0f, 240.0f);
        params.poles = std::clamp<uint32_t>(params.poles, 1u, 4u);
        for (auto& ms : params.delayMs) ms = clamp(ms, 0.0f, kArrayDelayDefaultMaxMs);
        for (auto& gain : params.gainDb) gain = clamp(gain, -60.0f, 18.0f);
        for (auto& mute : params.mute) mute = mute ? 1u : 0u;
        for (auto& invert : params.invert) invert = invert ? 1u : 0u;
        params_ = params;
        if (prepared_) syncStages();
    }

    ArrayCalibrateParams params() const { return params_; }

    void processBlock(const float* const* in, float* const* out,
        uint32_t inputChannels, uint32_t outputChannels, uint32_t frames)
    {
        if (!out || frames == 0u) return;
        outputChannels = std::min<uint32_t>(outputChannels, kArrayCalibrateMaxChannels);
        for (uint32_t ch = 0; ch < outputChannels; ++ch) {
            if (out[ch]) std::fill(out[ch], out[ch] + frames, 0.0f);
        }
        if (!in || maxFrames_ == 0u) return;

        const uint32_t channels = std::min<uint32_t>({
            inputChannels, outputChannels, params_.activeChannels,
            kArrayCalibrateMaxChannels });
        uint32_t offset = 0u;
        while (offset < frames) {
            const uint32_t chunk = std::min<uint32_t>(maxFrames_, frames - offset);
            std::array<const float*, kArrayCalibrateMaxChannels> inputPointers {};
            std::array<float*, kArrayCalibrateMaxChannels> outputPointers {};
            for (uint32_t ch = 0; ch < channels; ++ch) {
                inputPointers[ch] = in[ch] ? in[ch] + offset : nullptr;
                outputPointers[ch] = out[ch] ? out[ch] + offset : nullptr;
            }
            processChunk(inputPointers.data(), outputPointers.data(), channels, chunk);
            offset += chunk;
        }
    }

private:
    void processChunk(const float* const* in, float* const* out,
        uint32_t channels, uint32_t frames)
    {
        hpf_.processBlock(in, hpfPointers_.data(), channels, channels, frames);
        std::array<const float*, kArrayCalibrateMaxChannels> hpfInput {};
        std::array<const float*, kArrayCalibrateMaxChannels> delayInput {};
        for (uint32_t ch = 0; ch < channels; ++ch) {
            hpfInput[ch] = hpfPointers_[ch];
            delayInput[ch] = delayPointers_[ch];
        }
        delay_.processBlock(hpfInput.data(), delayPointers_.data(), channels, channels, frames);
        trim_.processBlock(delayInput.data(), out, channels, channels, frames);
    }

    void syncStages()
    {
        ArrayHpfParams hpfParams {};
        hpfParams.activeChannels = params_.activeChannels;
        hpfParams.poles = params_.poles;
        hpfParams.cutoffHz = params_.cutoffHz;
        hpfParams.outputGainDb = 0.0f;
        hpfParams.bypass = params_.bypass || params_.hpfBypass;
        hpf_.setParams(hpfParams);

        ArrayDelayParams delayParams {};
        delayParams.activeChannels = params_.activeChannels;
        delayParams.maxDelayMs = kArrayDelayDefaultMaxMs;
        delayParams.outputGainDb = 0.0f;
        delayParams.bypass = params_.bypass || params_.delayBypass;
        delayParams.delayMs = params_.delayMs;
        delay_.setParams(delayParams);

        ArrayTrimParams trimParams {};
        trimParams.activeChannels = params_.activeChannels;
        trimParams.outputGainDb = params_.outputGainDb;
        trimParams.bypass = params_.bypass || params_.trimBypass;
        trimParams.gainDb = params_.gainDb;
        trimParams.mute = params_.mute;
        trimParams.invert = params_.invert;
        trim_.setParams(trimParams);
    }

    ArrayCalibrateParams params_ {};
    ArrayHpf hpf_ {};
    ArrayDelay delay_ {};
    ArrayTrim trim_ {};
    bool prepared_ = false;
    uint32_t maxFrames_ = 0u;
    std::array<std::vector<float>, kArrayCalibrateMaxChannels> hpfScratch_ {};
    std::array<std::vector<float>, kArrayCalibrateMaxChannels> delayScratch_ {};
    std::array<float*, kArrayCalibrateMaxChannels> hpfPointers_ {};
    std::array<float*, kArrayCalibrateMaxChannels> delayPointers_ {};
};

} // namespace s3g
