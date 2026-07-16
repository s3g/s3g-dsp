#pragma once

#include "s3g_ambisonic_speaker_decoder.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace s3g {

struct AmbiAdaptiveDecoderParams {
    AmbiSpeakerDecoderParams decoder {};
    float focus = 0.75f;
    float diffuse = 0.35f;
    float confidence = 0.70f;
    float transient = 0.35f;
    float crossoverHz = 650.0f;
    float smoothingMs = 45.0f;
};

class AmbiAdaptiveDecoder {
public:
    void prepare(double sampleRate)
    {
        sampleRate_ = std::max(1.0, sampleRate);
        sharp_.prepare(sampleRate_);
        diffuse_.prepare(sampleRate_);
        reset();
        setParams(params_);
    }

    void reset()
    {
        lp_.fill({});
        env_ = 0.0f;
        prevEnv_ = 0.0f;
        focusState_ = 0.0f;
        confidenceMeter_ = 0.0f;
        transientMeter_ = 0.0f;
        focusMeter_ = 0.0f;
    }

    void setParams(AmbiAdaptiveDecoderParams params)
    {
        params.focus = clamp(params.focus, 0.0f, 1.0f);
        params.diffuse = clamp(params.diffuse, 0.0f, 1.0f);
        params.confidence = clamp(params.confidence, 0.0f, 1.0f);
        params.transient = clamp(params.transient, 0.0f, 1.0f);
        params.crossoverHz = clamp(params.crossoverHz, 20.0f, 5000.0f);
        params.smoothingMs = clamp(params.smoothingMs, 0.0f, 500.0f);
        params.decoder.outputGainDb = clamp(params.decoder.outputGainDb, -60.0f, 12.0f);
        params_ = params;

        auto sharpParams = params_.decoder;
        sharpParams.outputGainDb = 0.0f;
        sharp_.setParams(sharpParams);

        auto diffuseParams = params_.decoder;
        diffuseParams.mode = AmbiSpeakerDecoderMode::Basic;
        diffuseParams.weighting = AmbiSpeakerDecoderWeighting::InPhase;
        diffuseParams.width = std::min(diffuseParams.width, 0.72f);
        diffuseParams.energy = 1.0f;
        diffuseParams.outputGainDb = 0.0f;
        diffuse_.setParams(diffuseParams);

        params_.decoder = sharp_.params();
        params_.decoder.outputGainDb = params.decoder.outputGainDb;
    }

    AmbiAdaptiveDecoderParams params() const { return params_; }
    const AmbiSpeakerDecoder& fieldDecoder() const { return sharp_; }
    float confidenceMeter() const { return confidenceMeter_; }
    float transientMeter() const { return transientMeter_; }
    float focusMeter() const { return focusMeter_; }

    void processFrame(const float* input, float* output, uint32_t inputChannels, uint32_t outputChannels)
    {
        if (!output) return;
        outputChannels = std::min<uint32_t>(outputChannels, kAmbiSpeakerDecoderMaxSpeakers);
        for (uint32_t ch = 0; ch < outputChannels; ++ch) output[ch] = 0.0f;
        if (!input || inputChannels == 0u) return;

        std::array<float, kAmbiSpeakerDecoderMaxChannels> lowIn {};
        std::array<float, kAmbiSpeakerDecoderMaxChannels> highIn {};
        splitInput(input, inputChannels, lowIn, highIn);

        std::array<float, kAmbiSpeakerDecoderMaxSpeakers> lowDiffuse {};
        std::array<float, kAmbiSpeakerDecoderMaxSpeakers> highDiffuse {};
        std::array<float, kAmbiSpeakerDecoderMaxSpeakers> highSharp {};
        diffuse_.processFrame(lowIn.data(), lowDiffuse.data());
        diffuse_.processFrame(highIn.data(), highDiffuse.data());
        sharp_.processFrame(highIn.data(), highSharp.data());

        const float focus = smoothFocus(analyzeConfidence(highIn.data(), inputChannels),
            analyzeTransient(input, inputChannels));
        const float diffuseKeep = params_.diffuse;
        const float outGain = dbToGain(params_.decoder.outputGainDb);
        for (uint32_t spk = 0; spk < outputChannels; ++spk) {
            const float high = highDiffuse[spk] * (1.0f - focus) + highSharp[spk] * focus;
            const float low = lowDiffuse[spk] * (0.75f + 0.25f * diffuseKeep);
            output[spk] = flushDenormal((low + high) * outGain);
        }
    }

    template <typename Sample>
    void processBlock(const Sample* const* input, Sample* const* output, uint32_t inputChannels, uint32_t outputChannels, uint32_t frames)
    {
        if (!output || frames == 0u) return;
        outputChannels = std::min<uint32_t>(outputChannels, kAmbiSpeakerDecoderMaxSpeakers);
        for (uint32_t ch = 0; ch < outputChannels; ++ch) {
            if (output[ch]) std::fill(output[ch], output[ch] + frames, static_cast<Sample>(0));
        }
        if (!input) return;

        std::array<float, kAmbiSpeakerDecoderMaxChannels> frameIn {};
        std::array<float, kAmbiSpeakerDecoderMaxSpeakers> frameOut {};
        const uint32_t channels = std::min<uint32_t>(inputChannels, kAmbiSpeakerDecoderMaxChannels);
        for (uint32_t frame = 0; frame < frames; ++frame) {
            for (uint32_t ch = 0; ch < channels; ++ch) frameIn[ch] = input[ch] ? static_cast<float>(input[ch][frame]) : 0.0f;
            for (uint32_t ch = channels; ch < kAmbiSpeakerDecoderMaxChannels; ++ch) frameIn[ch] = 0.0f;
            processFrame(frameIn.data(), frameOut.data(), channels, outputChannels);
            for (uint32_t ch = 0; ch < outputChannels; ++ch) {
                if (output[ch]) output[ch][frame] = static_cast<Sample>(frameOut[ch]);
            }
        }
    }

private:
    struct LowpassState {
        float y = 0.0f;
    };

    void splitInput(const float* input, uint32_t inputChannels,
        std::array<float, kAmbiSpeakerDecoderMaxChannels>& lowIn,
        std::array<float, kAmbiSpeakerDecoderMaxChannels>& highIn)
    {
        const float alpha = 1.0f - std::exp(-2.0f * kPi * params_.crossoverHz / static_cast<float>(sampleRate_));
        const uint32_t channels = std::min<uint32_t>(inputChannels, kAmbiSpeakerDecoderMaxChannels);
        for (uint32_t ch = 0; ch < channels; ++ch) {
            lp_[ch].y += alpha * (input[ch] - lp_[ch].y);
            lowIn[ch] = lp_[ch].y;
            highIn[ch] = input[ch] - lp_[ch].y;
        }
    }

    float analyzeConfidence(const float* input, uint32_t inputChannels)
    {
        const float w = inputChannels > 0u ? input[0] : 0.0f;
        const float y = inputChannels > 1u ? input[1] : 0.0f;
        const float z = inputChannels > 2u ? input[2] : 0.0f;
        const float x = inputChannels > 3u ? input[3] : 0.0f;
        const float directional = std::sqrt(x * x + y * y + z * z);
        const float confidence = directional / std::max(0.000001f, directional + 0.70710678f * std::abs(w));
        confidenceMeter_ = confidenceMeter_ * 0.98f + confidence * 0.02f;
        return confidence;
    }

    float analyzeTransient(const float* input, uint32_t inputChannels)
    {
        const float w = inputChannels > 0u ? input[0] : 0.0f;
        const float target = std::abs(w);
        env_ += 0.08f * (target - env_);
        const float onset = std::max(0.0f, env_ - prevEnv_) * 12.0f;
        prevEnv_ = env_;
        const float transient = clamp(onset, 0.0f, 1.0f);
        transientMeter_ = transientMeter_ * 0.96f + transient * 0.04f;
        return transient;
    }

    float smoothFocus(float confidence, float transient)
    {
        const float target = clamp(params_.focus * (params_.confidence * confidence + params_.transient * transient), 0.0f, 1.0f);
        const float coef = params_.smoothingMs <= 0.0f
            ? 0.0f
            : std::exp(-1.0f / static_cast<float>(sampleRate_ * params_.smoothingMs * 0.001));
        focusState_ = target + (focusState_ - target) * coef;
        focusMeter_ = focusMeter_ * 0.96f + focusState_ * 0.04f;
        return focusState_;
    }

    double sampleRate_ = 48000.0;
    AmbiAdaptiveDecoderParams params_ {};
    AmbiSpeakerDecoder sharp_ {};
    AmbiSpeakerDecoder diffuse_ {};
    std::array<LowpassState, kAmbiSpeakerDecoderMaxChannels> lp_ {};
    float env_ = 0.0f;
    float prevEnv_ = 0.0f;
    float focusState_ = 0.0f;
    float confidenceMeter_ = 0.0f;
    float transientMeter_ = 0.0f;
    float focusMeter_ = 0.0f;
};

} // namespace s3g
