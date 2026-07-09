#pragma once

#include "s3g_3oafx.h"
#include "s3g_ambisonic_speaker_decoder.h"
#include "s3g_macro_delay.h"
#include "s3g_macro_pitch.h"
#include "s3g_math.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace s3g {

enum class ThreeOafxEffectKind : uint32_t {
    Delay = 0,
    Pitch = 1,
    Filter = 2,
    Gain = 3
};

struct ThreeOafxSingleEffectParams {
    ThreeOafxEffectKind kind = ThreeOafxEffectKind::Delay;
    AedMaskParams mask {};
    float dry = 0.65f;
    float output = 0.90f;
    float mix = 0.0f;
    float delayTimeMs = 320.0f;
    float delayFeedback = 0.22f;
    float pitchSemitones = 0.0f;
    float filterTone = 0.55f;
    float gain = 1.0f;
    float maskContrast = 0.10f;
    float maskCeiling = 0.92f;
    float duckCurve = 0.0f;
    float wetLimiter = 0.15f;
    float attackLag = 0.15f;
    float releaseLag = 0.35f;
    bool insertDuck = true;
};

class ThreeOafxSingleEffect {
public:
    void prepare(double sampleRate)
    {
        sampleRate_ = std::max(1.0, sampleRate);
        AmbiSpeakerDecoderParams decoderParams {};
        decoderParams.activeSpeakers = k3OafxVirtualSpeakers;
        decoderParams.order = 3;
        decoderParams.mode = AmbiSpeakerDecoderMode::Mmd;
        decoderParams.layout = AmbiSpeakerLayoutPreset::Sphere24;
        decoderParams.weighting = AmbiSpeakerDecoderWeighting::MaxRe;
        decoderParams.regularization = 0.018f;
        decoderParams.width = 1.0f;
        decoderParams.energy = 1.0f;
        decoderParams.outputGainDb = 0.0f;
        decoder_.prepare(sampleRate_);
        decoder_.setParams(decoderParams);
        rebuildMatchedEncoder();
        updateEncodeMakeup();
        delay_.prepare(sampleRate_, k3OafxVirtualSpeakers, 2.5);
        pitch_.prepare(sampleRate_, k3OafxVirtualSpeakers);
        prepareDelaySlot();
        reset();
    }

    void reset()
    {
        delay_.reset();
        pitch_.reset();
        resetDelaySlot();
        maskState_ = {};
        mixer_ = {};
        filterState_.fill(0.0f);
        drySmoothed_ = params_.dry;
        outputSmoothed_ = params_.output;
        wetGainSmoothed_ = 1.0f;
        gainSmoothed_ = params_.gain;
    }

    void setParams(const ThreeOafxSingleEffectParams& params)
    {
        params_ = sanitize(params);

        MacroDelayParams delayParams {};
        delayParams.timeMs = params_.delayTimeMs;
        delayParams.feedback = params_.delayFeedback;
        delayParams.tone = 0.62f;
        delayParams.character = 0.24f;
        delayParams.smear = 0.0f;
        delayParams.mix = 1.0f;
        delay_.setParams(delayParams);

        MacroPitchParams pitchParams {};
        pitchParams.pitchSemitones = params_.pitchSemitones;
        pitchParams.windowMs = 90.0f;
        pitchParams.mix = 1.0f;
        pitch_.setParams(pitchParams);
    }

    ThreeOafxSingleEffectParams params() const { return params_; }

    void processFrame(const float* input3Oa, float* output3Oa)
    {
        if (!input3Oa || !output3Oa) return;
        updateSmoothing();

        const bool isGain = params_.kind == ThreeOafxEffectKind::Gain;
        const bool gainActive = isGain && std::abs(params_.gain - params_.dry) > 0.0005f;
        if (isGain && !gainActive) {
            mixer_ = {};
            for (uint32_t ch = 0; ch < k3OaChannels; ++ch) {
                output3Oa[ch] = transparentLimit(input3Oa[ch] * drySmoothed_ * outputSmoothed_);
            }
            return;
        }

        float dryBus[k3OafxVirtualSpeakers] {};
        float sendBus[k3OafxVirtualSpeakers] {};
        float effectWet[k3OafxVirtualSpeakers] {};
        float wetBus[k3OafxVirtualSpeakers] {};
        float maskBus[k3OafxVirtualSpeakers] {};
        float wetGainMask[k3OafxVirtualSpeakers] {};
        float rawMask[k3OafxVirtualSpeakers] {};
        float mixed24[k3OafxVirtualSpeakers] {};

        decodeInput(input3Oa, dryBus);
        computeReturnMasks(wetGainMask, rawMask);
        for (uint32_t i = 0; i < k3OafxVirtualSpeakers; ++i) {
            // The working JSFX 3OAFX path uses the return mask for placement.
            // Send masking is diagnostic only, so effects receive the full 24-point field.
            sendBus[i] = dryBus[i];
        }

        if (params_.kind == ThreeOafxEffectKind::Gain) {
            processGainMixer(dryBus, rawMask, mixed24);
        } else {
            processEffectWet(sendBus, effectWet);
            processReturn(effectWet, wetGainMask, rawMask, wetBus, maskBus);
            processMixer(dryBus, wetBus, maskBus, mixed24);
        }

        encodeOutput(mixed24, output3Oa);
    }

private:
    static ThreeOafxSingleEffectParams sanitize(ThreeOafxSingleEffectParams params)
    {
        params.mask = sanitizeMask(params.mask);
        params.dry = clamp01f(params.dry);
        params.output = clamp01f(params.output);
        params.mix = clamp01f(params.mix);
        params.delayTimeMs = clamp(params.delayTimeMs, 20.0f, 2000.0f);
        params.delayFeedback = clamp(params.delayFeedback, 0.0f, 0.62f);
        params.pitchSemitones = clamp(params.pitchSemitones, -24.0f, 24.0f);
        params.filterTone = clamp01f(params.filterTone);
        params.gain = clamp(params.gain, 0.0f, 2.0f);
        params.maskContrast = clamp01f(params.maskContrast);
        params.maskCeiling = clamp(params.maskCeiling, 0.5f, 1.0f);
        params.duckCurve = clamp01f(params.duckCurve);
        params.wetLimiter = clamp01f(params.wetLimiter);
        params.attackLag = clamp01f(params.attackLag);
        params.releaseLag = clamp01f(params.releaseLag);
        return params;
    }

    static AedMaskParams sanitizeMask(AedMaskParams mask)
    {
        mask.azimuthDeg = clamp(mask.azimuthDeg, -179.9f, 179.9f);
        mask.elevationDeg = clamp(mask.elevationDeg, -90.0f, 90.0f);
        mask.smoothing = clamp01f(mask.smoothing);
        mask.width = clamp01f(mask.width);
        mask.focus = 0.0f;
        mask.level = clamp01f(mask.level);
        mask.floor = 0.03f;
        mask.rearReject = 1.0f;
        mask.energyComp = 0.50f;
        mask.gamma = 1.25f;
        return mask;
    }

    void updateSmoothing()
    {
        drySmoothed_ += (params_.dry - drySmoothed_) * 0.0015f;
        outputSmoothed_ += (params_.output - outputSmoothed_) * 0.0015f;
        gainSmoothed_ += (params_.gain - gainSmoothed_) * 0.0015f;
    }

    void decodeInput(const float* input3Oa, float* output24) const
    {
        const auto& matrix = decoder_.matrix();
        for (uint32_t spk = 0; spk < k3OafxVirtualSpeakers; ++spk) {
            float value = 0.0f;
            for (uint32_t ch = 0; ch < k3OaChannels; ++ch) {
                value += input3Oa[ch] * matrix[spk][ch];
            }
            output24[spk] = flushDenormal(value);
        }
    }

    void processEffectWet(const float* input24, float* output24)
    {
        switch (params_.kind) {
        case ThreeOafxEffectKind::Delay:
            processDelayWet(input24, output24);
            break;
        case ThreeOafxEffectKind::Pitch:
            pitch_.processFrame(input24, output24);
            break;
        case ThreeOafxEffectKind::Filter:
            processFilterWet(input24, output24);
            break;
        case ThreeOafxEffectKind::Gain:
            std::fill(output24, output24 + k3OafxVirtualSpeakers, 0.0f);
            break;
        }
        for (uint32_t i = 0; i < k3OafxVirtualSpeakers; ++i) {
            output24[i] = flushDenormal(softSat(output24[i]));
        }
    }

    void processFilterWet(const float* input24, float* output24)
    {
        const float tone = clamp01f(params_.filterTone);
        const float coeff = 0.006f + tone * tone * 0.42f;
        for (uint32_t ch = 0; ch < k3OafxVirtualSpeakers; ++ch) {
            filterState_[ch] += (input24[ch] - filterState_[ch]) * coeff;
            const float low = filterState_[ch];
            const float high = input24[ch] - low;
            const float band = high - low * 0.45f;
            const float shaped = low * (1.0f - tone) + band * (tone * 1.65f);
            output24[ch] = flushDenormal(softSat(shaped * 1.65f));
        }
    }

    void prepareDelaySlot()
    {
        delaySize_ = std::max<uint32_t>(256u, static_cast<uint32_t>(std::ceil(sampleRate_ * 2.5)) + 8u);
        for (auto& buffer : delayBuffer_) {
            buffer.assign(delaySize_, 0.0f);
        }
        resetDelaySlot();
    }

    void resetDelaySlot()
    {
        for (auto& buffer : delayBuffer_) {
            std::fill(buffer.begin(), buffer.end(), 0.0f);
        }
        delayWrite_ = 0u;
        delayCurrentMs_ = clamp(params_.delayTimeMs, 20.0f, 2000.0f);
        delayPreviousMs_ = delayCurrentMs_;
        delayTargetMs_ = delayCurrentMs_;
        delayFade_ = 1.0f;
        delayFeedback_ = clamp(params_.delayFeedback, 0.0f, 0.48f);
    }

    void processDelayWet(const float* input24, float* output24)
    {
        if (delaySize_ < 8u) {
            std::fill(output24, output24 + k3OafxVirtualSpeakers, 0.0f);
            return;
        }

        const float requestedMs = clamp(params_.delayTimeMs, 20.0f, 2000.0f);
        if (std::fabs(requestedMs - delayTargetMs_) > 0.5f) {
            delayPreviousMs_ = delayCurrentMs_;
            delayTargetMs_ = requestedMs;
            delayFade_ = 0.0f;
        }
        if (delayFade_ < 1.0f) {
            const float fadeSamples = static_cast<float>(std::max(1.0, sampleRate_ * 0.080));
            delayFade_ = std::min(1.0f, delayFade_ + 1.0f / fadeSamples);
            if (delayFade_ >= 1.0f) {
                delayCurrentMs_ = delayTargetMs_;
                delayPreviousMs_ = delayCurrentMs_;
            }
        }

        delayFeedback_ += (clamp(params_.delayFeedback, 0.0f, 0.48f) - delayFeedback_) * 0.0012f;
        const float previousSamples = delayMsToSamples(delayPreviousMs_);
        const float targetSamples = delayMsToSamples(delayTargetMs_);
        const float fade = smoothstep(delayFade_);
        const float wetTrim = 1.35f / std::sqrt(static_cast<float>(k3OafxVirtualSpeakers));

        for (uint32_t ch = 0; ch < k3OafxVirtualSpeakers; ++ch) {
            auto& buffer = delayBuffer_[ch];
            const float previous = readDelay(buffer, previousSamples);
            const float target = readDelay(buffer, targetSamples);
            const float delayed = previous + (target - previous) * fade;
            const float input = clamp(input24[ch], -2.0f, 2.0f);
            buffer[delayWrite_] = flushDenormal(softSat(input + delayed * delayFeedback_));
            output24[ch] = flushDenormal(transparentLimit(delayed * wetTrim));
        }
        delayWrite_ = (delayWrite_ + 1u) % delaySize_;
    }

    float delayMsToSamples(float delayMs) const
    {
        return clamp(delayMs * static_cast<float>(sampleRate_ * 0.001),
                     1.0f,
                     static_cast<float>(delaySize_ - 4u));
    }

    float readDelay(const std::vector<float>& buffer, float delaySamples) const
    {
        float read = static_cast<float>(delayWrite_) - delaySamples;
        while (read < 0.0f) read += static_cast<float>(delaySize_);
        const uint32_t i0 = static_cast<uint32_t>(std::floor(read)) % delaySize_;
        const uint32_t i1 = (i0 + 1u) % delaySize_;
        const float frac = read - std::floor(read);
        return buffer[i0] + (buffer[i1] - buffer[i0]) * frac;
    }

    static float smoothstep(float value)
    {
        value = clamp(value, 0.0f, 1.0f);
        return value * value * (3.0f - 2.0f * value);
    }

    void processReturn(const float* effectWet24,
                       const float* wetGainMask,
                       const float* rawMask,
                       float* wetBus24,
                       float* maskBus24)
    {
        float wetSum = 0.0f;
        for (uint32_t i = 0; i < k3OafxVirtualSpeakers; ++i) {
            wetSum += effectWet24[i];
        }
        if (params_.kind == ThreeOafxEffectKind::Delay) {
            wetSum *= 1.0f / std::sqrt(static_cast<float>(k3OafxVirtualSpeakers));
        }

        for (uint32_t i = 0; i < k3OafxVirtualSpeakers; ++i) {
            wetBus24[i] = flushDenormal(wetSum * wetGainMask[i]);
            maskBus24[i] = clamp01f(rawMask[i] * params_.mix);
        }
    }

    void processMixer(const float* dryBus24, const float* wetBus24, const float* maskBus24, float* mixed24)
    {
        const float wetTarget = 1.10f + averageMask(maskBus24) * 0.90f;
        wetGainSmoothed_ += (wetTarget - wetGainSmoothed_) * 0.0008f;

        const float attack = 0.35f * std::pow(1.0f - clamp01f(params_.attackLag), 2.0f) + 0.0005f;
        const float release = 0.20f * std::pow(1.0f - clamp01f(params_.releaseLag), 2.0f) + 0.0002f;
        const float contrast = 1.0f + clamp01f(params_.maskContrast) * 2.5f;
        const float ceiling = clamp(params_.maskCeiling, 0.5f, 1.0f);
        const float duckPow = 1.0f - clamp01f(params_.duckCurve) * 0.4f;
        const float wetLimit = clamp01f(params_.wetLimiter);

        for (uint32_t i = 0; i < k3OafxVirtualSpeakers; ++i) {
            const float maskIn = clamp01f(maskBus24[i]);
            const float raw = std::min(ceiling, safePow(maskIn, contrast));
            const float lag = raw > mixer_.maskSmooth[i] ? attack : release;
            mixer_.maskSmooth[i] += lag * (raw - mixer_.maskSmooth[i]);
            const float m = safePow(clamp(mixer_.maskSmooth[i], 0.0f, ceiling), duckPow);
            const float dryGain = params_.insertDuck ? std::cos(m * kPi * 0.5f) : 1.0f;
            const float wet = wetBus24[i] * params_.mix * wetGainSmoothed_;
            const float wetSafe = (1.0f - wetLimit) * wet + wetLimit * softSat(wet);
            mixed24[i] = transparentLimit((dryBus24[i] * drySmoothed_ * dryGain + wetSafe) * outputSmoothed_);
        }
    }

    void processGainMixer(const float* dryBus24, const float* rawMask, float* mixed24)
    {
        const float attack = 0.35f * std::pow(1.0f - clamp01f(params_.attackLag), 2.0f) + 0.0005f;
        const float release = 0.20f * std::pow(1.0f - clamp01f(params_.releaseLag), 2.0f) + 0.0002f;
        const float contrast = 1.0f + clamp01f(params_.maskContrast) * 2.5f;
        const float ceiling = clamp(params_.maskCeiling, 0.5f, 1.0f);
        const float duckPow = 1.0f - clamp01f(params_.duckCurve) * 0.4f;

        for (uint32_t i = 0; i < k3OafxVirtualSpeakers; ++i) {
            const float raw = std::min(ceiling, safePow(rawMask[i], contrast));
            const float lag = raw > mixer_.maskSmooth[i] ? attack : release;
            mixer_.maskSmooth[i] += lag * (raw - mixer_.maskSmooth[i]);
            const float m = safePow(clamp(mixer_.maskSmooth[i], 0.0f, ceiling), duckPow);
            const float localGain = clamp(drySmoothed_ + m * (gainSmoothed_ - drySmoothed_), 0.0f, 2.0f);
            mixed24[i] = transparentLimit(dryBus24[i] * localGain * outputSmoothed_);
        }
    }

    void computeReturnMasks(float* wetGainMask, float* rawMask)
    {
        const Vec3 target = smoothDirection(maskState_, params_.mask);
        float powSum = 0.0f;
        for (uint32_t i = 0; i < k3OafxVirtualSpeakers; ++i) {
            const float raw = computeMaskValue(k3OafxPoints[i], target, params_.mask);
            rawMask[i] = raw;
            powSum += raw * raw;
        }
        const float comp = (1.0f - clamp01f(params_.mask.energyComp))
            + clamp01f(params_.mask.energyComp) / std::max(1.0f, std::sqrt(powSum));
        for (uint32_t i = 0; i < k3OafxVirtualSpeakers; ++i) {
            wetGainMask[i] = clamp01f(safePow(rawMask[i], params_.mask.gamma) * comp * clamp01f(params_.mask.level));
        }
    }

    static float averageMask(const float* maskBus24)
    {
        float sum = 0.0f;
        for (uint32_t i = 0; i < k3OafxVirtualSpeakers; ++i) {
            sum += clamp01f(maskBus24[i]);
        }
        return sum / static_cast<float>(k3OafxVirtualSpeakers);
    }

    void encodeOutput(const float* input24, float* output3Oa) const
    {
        for (uint32_t ch = 0; ch < k3OaChannels; ++ch) {
            float value = 0.0f;
            for (uint32_t spk = 0; spk < k3OafxVirtualSpeakers; ++spk) {
                value += encodeMatrix_[ch][spk] * input24[spk];
            }
            output3Oa[ch] = value;
        }
        for (uint32_t ch = 0; ch < k3OaChannels; ++ch) {
            output3Oa[ch] = flushDenormal(output3Oa[ch] * encodeMakeup_);
        }
    }

    void updateEncodeMakeup()
    {
        float decodedW[k3OafxVirtualSpeakers] {};
        float encoded[k3OaChannels] {};
        const auto& matrix = decoder_.matrix();
        for (uint32_t spk = 0; spk < k3OafxVirtualSpeakers; ++spk) {
            decodedW[spk] = matrix[spk][0];
        }
        for (uint32_t ch = 0; ch < k3OaChannels; ++ch) {
            for (uint32_t spk = 0; spk < k3OafxVirtualSpeakers; ++spk) {
                encoded[ch] += encodeMatrix_[ch][spk] * decodedW[spk];
            }
        }
        encodeMakeup_ = clamp(1.0f / std::max(0.0001f, std::abs(encoded[0])), 0.25f, 8.0f);
    }

    void rebuildMatchedEncoder()
    {
        for (auto& row : encodeMatrix_) row.fill(0.0f);
        const auto& decode = decoder_.matrix();
        std::array<std::array<double, k3OaChannels * 2u>, k3OaChannels> aug {};
        for (uint32_t row = 0; row < k3OaChannels; ++row) {
            for (uint32_t col = 0; col < k3OaChannels; ++col) {
                double sum = 0.0;
                for (uint32_t spk = 0; spk < k3OafxVirtualSpeakers; ++spk) {
                    sum += static_cast<double>(decode[spk][row]) * static_cast<double>(decode[spk][col]);
                }
                aug[row][col] = sum;
            }
            aug[row][k3OaChannels + row] = 1.0;
        }
        if (!invert16(aug)) {
            return;
        }
        for (uint32_t row = 0; row < k3OaChannels; ++row) {
            for (uint32_t spk = 0; spk < k3OafxVirtualSpeakers; ++spk) {
                double value = 0.0;
                for (uint32_t k = 0; k < k3OaChannels; ++k) {
                    value += aug[row][k3OaChannels + k] * static_cast<double>(decode[spk][k]);
                }
                encodeMatrix_[row][spk] = static_cast<float>(value);
            }
        }
    }

    static bool invert16(std::array<std::array<double, k3OaChannels * 2u>, k3OaChannels>& aug)
    {
        for (uint32_t col = 0; col < k3OaChannels; ++col) {
            uint32_t pivot = col;
            double best = std::fabs(aug[col][col]);
            for (uint32_t row = col + 1u; row < k3OaChannels; ++row) {
                const double value = std::fabs(aug[row][col]);
                if (value > best) {
                    best = value;
                    pivot = row;
                }
            }
            if (best < 0.000000000001) return false;
            if (pivot != col) std::swap(aug[pivot], aug[col]);
            const double invPivot = 1.0 / aug[col][col];
            for (uint32_t c = 0; c < k3OaChannels * 2u; ++c) aug[col][c] *= invPivot;
            for (uint32_t row = 0; row < k3OaChannels; ++row) {
                if (row == col) continue;
                const double f = aug[row][col];
                if (std::fabs(f) < 0.0000000000001) continue;
                for (uint32_t c = 0; c < k3OaChannels * 2u; ++c) aug[row][c] -= f * aug[col][c];
            }
        }
        return true;
    }

    static float transparentLimit(float value)
    {
        if (!std::isfinite(value)) return 0.0f;
        constexpr float knee = 1.0f;
        if (std::abs(value) <= knee) return value;
        const float sign = value < 0.0f ? -1.0f : 1.0f;
        const float over = std::min(std::abs(value) - knee, 7.0f);
        return sign * (knee + over / (1.0f + over));
    }

    double sampleRate_ = 48000.0;
    ThreeOafxSingleEffectParams params_ {};
    AmbiSpeakerDecoder decoder_ {};
    AedMaskState maskState_ {};
    MixerState mixer_ {};
    MacroDelay delay_ {};
    MacroPitch pitch_ {};
    std::array<std::array<float, k3OafxVirtualSpeakers>, k3OaChannels> encodeMatrix_ {};
    std::array<float, k3OafxVirtualSpeakers> filterState_ {};
    std::array<std::vector<float>, k3OafxVirtualSpeakers> delayBuffer_ {};
    uint32_t delaySize_ = 0u;
    uint32_t delayWrite_ = 0u;
    float delayCurrentMs_ = 320.0f;
    float delayPreviousMs_ = 320.0f;
    float delayTargetMs_ = 320.0f;
    float delayFade_ = 1.0f;
    float delayFeedback_ = 0.22f;
    float drySmoothed_ = 0.65f;
    float outputSmoothed_ = 0.90f;
    float wetGainSmoothed_ = 1.0f;
    float gainSmoothed_ = 1.0f;
    float encodeMakeup_ = 1.0f;
};

} // namespace s3g
