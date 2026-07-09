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

constexpr bool k3OafxRackWetDirectDiagnostic = false;
constexpr bool k3OafxRackWetRoundTripDiagnostic = false;
constexpr bool k3OafxRackMaskDiagnostic = false;
constexpr bool k3OafxRackDelayOnlyDiagnostic = false;
constexpr bool k3OafxRackDelayDirectDiagnostic = false;
constexpr bool k3OafxRackCopyDiagnostic = false;
constexpr float k3OafxRackCopyDiagnosticGain = 4.0f;

enum class ThreeOafxRackMode : uint32_t {
    Parallel = 0,
    Series = 1
};

inline MacroDelayParams defaultThreeOafxRackDelayFx()
{
    MacroDelayParams params {};
    params.mix = 0.0f;
    return params;
}

inline MacroPitchParams defaultThreeOafxRackPitchFx()
{
    MacroPitchParams params {};
    params.mix = 0.0f;
    return params;
}

struct ThreeOafxSlotParams {
    AedMaskParams sendMask {};
    AedMaskParams returnMask {};
    float send = 1.0f;
    float returnGain = 1.0f;
    bool enabled = true;
    bool replace = true;
};

struct ThreeOafxRackParams {
    ThreeOafxRackMode mode = ThreeOafxRackMode::Parallel;
    float dry = 0.65f;
    // Kept for state compatibility. The rack uses per-effect MIX controls
    // instead of a global wet trim.
    float wet = 1.0f;
    float output = 0.90f;
    float maskContrast = 0.10f;
    float maskCeiling = 0.92f;
    float duckCurve = 0.0f;
    float wetLimiter = 0.15f;
    float attackLag = 0.15f;
    float releaseLag = 0.35f;
    bool insertDuck = true;
    bool directDry = true;
    ThreeOafxSlotParams delay {};
    ThreeOafxSlotParams pitch {};
    ThreeOafxSlotParams filter {};
    ThreeOafxSlotParams drive {};
    MacroDelayParams delayFx = defaultThreeOafxRackDelayFx();
    MacroPitchParams pitchFx = defaultThreeOafxRackPitchFx();
    float filterMix = 0.0f;
    float filterTone = 0.55f;
    float driveMix = 0.0f;
    float driveAmount = 0.35f;
};

class ThreeOafxRack {
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
        ambiDecoder_.prepare(sampleRate_);
        ambiDecoder_.setParams(decoderParams);
        updateRackEncodeMakeup();
        delay_.prepare(sampleRate_, k3OafxVirtualSpeakers, 2.5);
        pitch_.prepare(sampleRate_, k3OafxVirtualSpeakers);
        prepareRackDelay();
        prepareRackPitch();
        reset();
    }

    void reset()
    {
        delay_.reset();
        pitch_.reset();
        resetRackDelay();
        resetRackPitch();
        rackFilterState_.fill(0.0f);
        delaySendMask_ = {};
        delayReturnMask_ = {};
        pitchSendMask_ = {};
        pitchReturnMask_ = {};
        filterSendMask_ = {};
        filterReturnMask_ = {};
        driveSendMask_ = {};
        driveReturnMask_ = {};
        delayMixer_ = {};
        pitchMixer_ = {};
        rackMixer_ = {};
        rackWetActivity_ = 0.0f;
        rackWetGain_ = 1.0f;
        drySmoothed_ = params_.dry;
        wetSmoothed_ = 1.0f;
        outputSmoothed_ = params_.output;
    }

    void setParams(const ThreeOafxRackParams& params)
    {
        params_ = sanitize(params);
        delay_.setParams(params_.delayFx);
        pitch_.setParams(params_.pitchFx);
    }

    ThreeOafxRackParams params() const { return params_; }

    void processFrame(const float* input3Oa, float* output3Oa)
    {
        if (!input3Oa || !output3Oa) return;
        updateMixerSmoothing();
        updateRackWetActivity();

        if (effectMixSum() <= 0.00001f) {
            rackMixer_ = {};
            rackWetActivity_ = 0.0f;
            for (uint32_t ch = 0; ch < k3OaChannels; ++ch) {
                const float directDry = params_.directDry ? input3Oa[ch] * drySmoothed_ * outputSmoothed_ : 0.0f;
                output3Oa[ch] = transparentLimit(directDry);
            }
            return;
        }

        if constexpr (k3OafxRackWetDirectDiagnostic) {
            drySmoothed_ = params_.dry;
            wetSmoothed_ = 1.0f;
            outputSmoothed_ = params_.output;
            for (uint32_t ch = 0; ch < k3OaChannels; ++ch) {
                const float directDry = params_.directDry ? input3Oa[ch] * drySmoothed_ : 0.0f;
                const float directWet = input3Oa[ch] * wetSmoothed_;
                output3Oa[ch] = transparentLimit((directDry + directWet) * outputSmoothed_);
            }
            return;
        }

        float decoded[k3OafxVirtualSpeakers] {};
        float delaySend[k3OafxVirtualSpeakers] {};
        float delayWet[k3OafxVirtualSpeakers] {};
        float pitchSend[k3OafxVirtualSpeakers] {};
        float pitchWet[k3OafxVirtualSpeakers] {};
        float filterWet[k3OafxVirtualSpeakers] {};
        float driveWet[k3OafxVirtualSpeakers] {};
        float wetBus24[k3OafxVirtualSpeakers] {};
        float maskBus24[k3OafxVirtualSpeakers] {};
        float mixed24[k3OafxVirtualSpeakers] {};
        float output3OaRaw[k3OaChannels] {};
        bool virtualDryIncluded = false;

        decodeInputToVirtual24(input3Oa, decoded);

        if constexpr (k3OafxRackWetRoundTripDiagnostic) {
            for (uint32_t i = 0; i < k3OafxVirtualSpeakers; ++i) {
                mixed24[i] = decoded[i] * wetSmoothed_ * outputSmoothed_;
            }
        } else if constexpr (k3OafxRackMaskDiagnostic) {
            processSlotSend(decoded, params_.delay, delaySendMask_, delaySend);
            processSlotReturnAndMixer(decoded, delaySend, params_.delay, delayReturnMask_, delayMixer_, mixed24);
        } else if constexpr (k3OafxRackDelayDirectDiagnostic) {
            processSlotSend(decoded, params_.delay, delaySendMask_, delaySend);
            delay_.processWetFrame(delaySend, delayWet);
            conditionSlotWet(delayWet, 0.85f);
            for (uint32_t i = 0; i < k3OafxVirtualSpeakers; ++i) {
                mixed24[i] = delayWet[i] * wetSmoothed_ * outputSmoothed_;
            }
        } else if constexpr (k3OafxRackDelayOnlyDiagnostic) {
            processRackDelaySlot(decoded, delaySend, delayWet, mixed24);
            virtualDryIncluded = true;
        } else if constexpr (k3OafxRackCopyDiagnostic) {
            processRackCopyDiagnostic(decoded, mixed24);
            virtualDryIncluded = false;
        } else {
            processRackEffectChain(decoded, delaySend, delayWet, pitchSend, pitchWet, filterWet, driveWet);
            processRackReturnBuses(delayWet, pitchWet, filterWet, driveWet, wetBus24, maskBus24);
            processRackMixerBuses(decoded, wetBus24, maskBus24, rackMixer_, mixed24);
            virtualDryIncluded = true;
        }

        encodeVirtual24ToOutput(mixed24, output3OaRaw);

        for (uint32_t ch = 0; ch < k3OaChannels; ++ch) {
            const float directDry = (params_.directDry && !virtualDryIncluded) ? input3Oa[ch] * drySmoothed_ * outputSmoothed_ : 0.0f;
            output3Oa[ch] = transparentLimit(directDry + output3OaRaw[ch]);
        }
    }

private:
    void processRackEffectChain(const float* source24,
                                float* delaySend24,
                                float* delayWetOut24,
                                float* pitchSend24,
                                float* pitchWetOut24,
                                float* filterWetOut24,
                                float* driveWetOut24)
    {
        processSlotSend(source24, params_.delay, delaySendMask_, delaySend24);
        processRackDelayWet(delaySend24, delayWetOut24);

        if (params_.mode == ThreeOafxRackMode::Series) {
            float stage[k3OafxVirtualSpeakers] {};
            for (uint32_t i = 0; i < k3OafxVirtualSpeakers; ++i) stage[i] = source24[i] + delayWetOut24[i];
            processSlotSend(stage, params_.pitch, pitchSendMask_, pitchSend24);
            processRackPitchWet(pitchSend24, pitchWetOut24);
            for (uint32_t i = 0; i < k3OafxVirtualSpeakers; ++i) stage[i] = softSat(stage[i] + pitchWetOut24[i]);
            processRackFilterWet(stage, filterWetOut24);
            for (uint32_t i = 0; i < k3OafxVirtualSpeakers; ++i) stage[i] = softSat(stage[i] + filterWetOut24[i]);
            processRackDriveWet(stage, driveWetOut24);
            return;
        }

        processSlotSend(source24, params_.pitch, pitchSendMask_, pitchSend24);
        processRackPitchWet(pitchSend24, pitchWetOut24);
        processRackFilterWet(source24, filterWetOut24);
        processRackDriveWet(source24, driveWetOut24);
    }

    void addSlotWetToOutput(const float* effectWet24,
                            const ThreeOafxSlotParams& slot,
                            AedMaskState& returnState,
                            float* output24)
    {
        if (!slot.enabled) return;
        float returnMask[k3OafxVirtualSpeakers] {};
        computeMask(smoothDirection(returnState, slot.returnMask), slot.returnMask, returnMask);
        for (uint32_t i = 0; i < k3OafxVirtualSpeakers; ++i) {
            const float contribution = softSat(effectWet24[i] * returnMask[i] * slot.returnGain);
            output24[i] = softSat(output24[i] + contribution);
        }
    }

    void processRackDelaySlot(const float* source24, float* sendWet24, float* effectWet24, float* mixed24)
    {
        if (!params_.delay.enabled) {
            processBypassedSlot(source24, mixed24);
            std::fill(sendWet24, sendWet24 + k3OafxVirtualSpeakers, 0.0f);
            std::fill(effectWet24, effectWet24 + k3OafxVirtualSpeakers, 0.0f);
            return;
        }
        processSlotSend(source24, params_.delay, delaySendMask_, sendWet24);
        processRackDelayWet(sendWet24, effectWet24);
        conditionSlotWet(effectWet24, 1.0f);
        processSlotReturnWithDryDuck(source24, effectWet24, params_.delay, delayReturnMask_, delayMixer_, mixed24);
    }

    void processDelaySlot(const float* source24, float* sendWet24, float* effectWet24, float* mixed24, bool replaceOverride = true)
    {
        if (!params_.delay.enabled) {
            processBypassedSlot(source24, mixed24);
            std::fill(sendWet24, sendWet24 + k3OafxVirtualSpeakers, 0.0f);
            std::fill(effectWet24, effectWet24 + k3OafxVirtualSpeakers, 0.0f);
            return;
        }
        processSlotSend(source24, params_.delay, delaySendMask_, sendWet24);
        processRackDelayWet(sendWet24, effectWet24);
        conditionSlotWet(effectWet24, 1.0f);
        auto slot = params_.delay;
        slot.replace = replaceOverride ? slot.replace : false;
        processSlotReturnAndMixer(source24, effectWet24, slot, delayReturnMask_, delayMixer_, mixed24);
    }

    void processPitchSlot(const float* source24, float* sendWet24, float* effectWet24, float* mixed24)
    {
        if (!params_.pitch.enabled) {
            processBypassedSlot(source24, mixed24);
            std::fill(sendWet24, sendWet24 + k3OafxVirtualSpeakers, 0.0f);
            std::fill(effectWet24, effectWet24 + k3OafxVirtualSpeakers, 0.0f);
            return;
        }
        processSlotSend(source24, params_.pitch, pitchSendMask_, sendWet24);
        pitch_.processFrame(sendWet24, effectWet24);
        conditionSlotWet(effectWet24, 0.85f);
        processSlotReturnAndMixer(source24, effectWet24, params_.pitch, pitchReturnMask_, pitchMixer_, mixed24);
    }

    void processBypassedSlot(const float* source24, float* mixed24)
    {
        for (uint32_t i = 0; i < k3OafxVirtualSpeakers; ++i) {
            mixed24[i] = source24[i] * drySmoothed_ * outputSmoothed_;
        }
    }

    void processSlotSend(const float* dry24, const ThreeOafxSlotParams& slot, AedMaskState& state, float* wet24)
    {
        // In the rack, the send position is a linked diagnostic/control point.
        // The audible spatial placement happens at the return mask, matching the
        // useful part of the JSFX send/return workflow without double-gating.
        state.target = smoothDirection(state, slot.sendMask);
        for (uint32_t i = 0; i < k3OafxVirtualSpeakers; ++i) {
            wet24[i] = dry24[i] * slot.send;
        }
    }

    void conditionSlotWet(float* effectWet24, float slotTrim)
    {
        for (uint32_t i = 0; i < k3OafxVirtualSpeakers; ++i) {
            effectWet24[i] = flushDenormal(softSat(effectWet24[i] * slotTrim));
        }
    }

    void prepareRackDelay()
    {
        rackDelaySize_ = std::max<uint32_t>(8u, static_cast<uint32_t>(std::ceil(sampleRate_ * 2.5)) + 8u);
        for (auto& buffer : rackDelayBuffer_) {
            buffer.assign(rackDelaySize_, 0.0f);
        }
        resetRackDelay();
    }

    void resetRackDelay()
    {
        for (auto& buffer : rackDelayBuffer_) {
            std::fill(buffer.begin(), buffer.end(), 0.0f);
        }
        rackDelayWrite_ = 0;
        rackDelayCurrentMs_ = clamp(params_.delayFx.timeMs, 20.0f, 2000.0f);
        rackDelayPreviousMs_ = rackDelayCurrentMs_;
        rackDelayTargetMs_ = rackDelayCurrentMs_;
        rackDelayFade_ = 1.0f;
        rackDelayFeedback_ = params_.delayFx.feedback;
        rackDelayMix_ = clamp01f(params_.delayFx.mix);
    }

    void processRackDelayWet(const float* input24, float* output24)
    {
        if (rackDelaySize_ < 8u) {
            std::fill(output24, output24 + k3OafxVirtualSpeakers, 0.0f);
            return;
        }

        const float requestedMs = clamp(params_.delayFx.timeMs, 20.0f, 2000.0f);
        if (std::fabs(requestedMs - rackDelayTargetMs_) > 0.5f) {
            rackDelayPreviousMs_ = rackDelayCurrentMs_;
            rackDelayTargetMs_ = requestedMs;
            rackDelayFade_ = 0.0f;
        }
        if (rackDelayFade_ < 1.0f) {
            const float fadeSamples = static_cast<float>(std::max(1.0, sampleRate_ * 0.080));
            rackDelayFade_ = std::min(1.0f, rackDelayFade_ + 1.0f / fadeSamples);
            if (rackDelayFade_ >= 1.0f) {
                rackDelayCurrentMs_ = rackDelayTargetMs_;
                rackDelayPreviousMs_ = rackDelayCurrentMs_;
            }
        }
        rackDelayFeedback_ += (params_.delayFx.feedback - rackDelayFeedback_) * 0.0012f;
        const float requestedMix = clamp(params_.delayFx.mix, 0.0f, 1.0f);
        if (requestedMix <= 0.00001f) {
            rackDelayMix_ = 0.0f;
        } else {
            rackDelayMix_ += (requestedMix - rackDelayMix_) * 0.0015f;
        }

        const float feedback = clamp(rackDelayFeedback_, 0.0f, 0.55f);
        const float mix = clamp(rackDelayMix_, 0.0f, 1.0f);
        const float wetGain = std::sqrt(mix) / (1.0f + mix * 0.55f);
        const float previousSamples = delayMsToSamples(rackDelayPreviousMs_);
        const float targetSamples = delayMsToSamples(rackDelayTargetMs_);
        const float fade = smoothstep(rackDelayFade_);

        for (uint32_t ch = 0; ch < k3OafxVirtualSpeakers; ++ch) {
            auto& buffer = rackDelayBuffer_[ch];
            const float previous = readRackDelay(buffer, previousSamples);
            const float target = readRackDelay(buffer, targetSamples);
            const float delayed = previous + (target - previous) * fade;
            const float input = clamp(input24[ch], -2.0f, 2.0f);
            buffer[rackDelayWrite_] = flushDenormal(softSat(input + delayed * feedback));
            output24[ch] = flushDenormal(transparentLimit(delayed * wetGain * 1.35f));
        }
        rackDelayWrite_ = (rackDelayWrite_ + 1u) % rackDelaySize_;
    }

    void prepareRackPitch()
    {
        rackPitchSize_ = std::max<uint32_t>(256u, static_cast<uint32_t>(std::ceil(sampleRate_ * 0.22)) + 8u);
        for (auto& buffer : rackPitchBuffer_) {
            buffer.assign(rackPitchSize_, 0.0f);
        }
        resetRackPitch();
    }

    void resetRackPitch()
    {
        for (auto& buffer : rackPitchBuffer_) {
            std::fill(buffer.begin(), buffer.end(), 0.0f);
        }
        rackPitchWrite_ = 0;
        rackPitchRatio_ = 1.0f;
        for (uint32_t i = 0; i < k3OafxVirtualSpeakers; ++i) {
            rackPitchPhase_[i] = static_cast<float>(i) / static_cast<float>(k3OafxVirtualSpeakers);
        }
    }

    void processRackPitchWet(const float* input24, float* output24)
    {
        if (rackPitchSize_ < 16u) {
            std::fill(output24, output24 + k3OafxVirtualSpeakers, 0.0f);
            return;
        }
        const float mix = clamp(params_.pitchFx.mix, 0.0f, 1.0f);
        const float targetRatio = std::pow(2.0f, clamp(params_.pitchFx.pitchSemitones, -24.0f, 24.0f) / 12.0f);
        rackPitchRatio_ += (targetRatio - rackPitchRatio_) * 0.0008f;
        const float shift = std::fabs(rackPitchRatio_ - 1.0f);
        const float window = clamp(static_cast<float>(sampleRate_ * 0.085), 64.0f, static_cast<float>(rackPitchSize_ - 4u));
        const float step = clamp(shift / std::max(16.0f, window), 0.0f, 0.015f);

        for (uint32_t ch = 0; ch < k3OafxVirtualSpeakers; ++ch) {
            auto& buffer = rackPitchBuffer_[ch];
            buffer[rackPitchWrite_] = clamp(input24[ch], -2.0f, 2.0f);
            rackPitchPhase_[ch] += step;
            rackPitchPhase_[ch] -= std::floor(rackPitchPhase_[ch]);
            const float phaseA = rackPitchPhase_[ch];
            const float phaseB = phaseA + 0.5f - std::floor(phaseA + 0.5f);
            const float a = readRackPitch(buffer, phaseA, window, rackPitchRatio_);
            const float b = readRackPitch(buffer, phaseB, window, rackPitchRatio_);
            const float wa = 0.5f - 0.5f * std::cos(2.0f * kPi * phaseA);
            const float wb = 0.5f - 0.5f * std::cos(2.0f * kPi * phaseB);
            const float shifted = (a * wa + b * wb) / std::max(0.0001f, wa + wb);
            output24[ch] = flushDenormal(transparentLimit(shifted * mix * 1.55f));
        }
        rackPitchWrite_ = (rackPitchWrite_ + 1u) % rackPitchSize_;
    }

    float readRackPitch(const std::vector<float>& buffer, float phase, float window, float ratio) const
    {
        const float offset = ratio >= 1.0f ? window * (1.0f - phase) : window * phase;
        float read = static_cast<float>(rackPitchWrite_) - clamp(offset, 1.0f, static_cast<float>(rackPitchSize_ - 4u));
        while (read < 0.0f) read += static_cast<float>(rackPitchSize_);
        const uint32_t i0 = static_cast<uint32_t>(std::floor(read)) % rackPitchSize_;
        const uint32_t i1 = (i0 + 1u) % rackPitchSize_;
        const float frac = read - std::floor(read);
        return buffer[i0] + (buffer[i1] - buffer[i0]) * frac;
    }

    void processRackFilterWet(const float* input24, float* output24)
    {
        const float mix = clamp01f(params_.filterMix);
        const float tone = clamp01f(params_.filterTone);
        const float coeff = 0.006f + tone * tone * 0.42f;
        for (uint32_t ch = 0; ch < k3OafxVirtualSpeakers; ++ch) {
            rackFilterState_[ch] += (input24[ch] - rackFilterState_[ch]) * coeff;
            const float low = rackFilterState_[ch];
            const float high = input24[ch] - low;
            const float band = high - low * 0.45f;
            const float shaped = low * (1.0f - tone) + band * (tone * 1.65f);
            output24[ch] = flushDenormal(softSat(shaped * mix * 1.65f));
        }
    }

    void processRackDriveWet(const float* input24, float* output24) const
    {
        const float mix = clamp01f(params_.driveMix);
        const float drive = 1.0f + clamp01f(params_.driveAmount) * 8.0f;
        for (uint32_t ch = 0; ch < k3OafxVirtualSpeakers; ++ch) {
            const float shaped = std::tanh(input24[ch] * drive) / std::sqrt(std::max(1.0f, drive * 0.35f));
            output24[ch] = flushDenormal(softSat(shaped * mix * 1.35f));
        }
    }

    float delayMsToSamples(float delayMs) const
    {
        return clamp(delayMs * static_cast<float>(sampleRate_ * 0.001),
                     1.0f,
                     static_cast<float>(rackDelaySize_ - 4u));
    }

    float readRackDelay(const std::vector<float>& buffer, float delaySamples) const
    {
        float read = static_cast<float>(rackDelayWrite_) - delaySamples;
        while (read < 0.0f) read += static_cast<float>(rackDelaySize_);
        const uint32_t i0 = static_cast<uint32_t>(std::floor(read)) % rackDelaySize_;
        const uint32_t i1 = (i0 + 1u) % rackDelaySize_;
        const float frac = read - std::floor(read);
        return buffer[i0] + (buffer[i1] - buffer[i0]) * frac;
    }

    static float smoothstep(float value)
    {
        value = clamp(value, 0.0f, 1.0f);
        return value * value * (3.0f - 2.0f * value);
    }

    void processSlotReturnAndMixer(const float* dry24,
                                   const float* effectWet24,
                                   const ThreeOafxSlotParams& slot,
                                   AedMaskState& returnState,
                                   MixerState& mixerState,
                                   float* mixed24)
    {
        float returnMask[k3OafxVirtualSpeakers] {};
        computeMask(smoothDirection(returnState, slot.returnMask), slot.returnMask, returnMask);

        float wetSum = 0.0f;
        for (uint32_t i = 0; i < k3OafxVirtualSpeakers; ++i) {
            wetSum += effectWet24[i];
        }
        wetSum *= 1.0f / std::sqrt(static_cast<float>(k3OafxVirtualSpeakers));

        const float attack = 0.35f * std::pow(1.0f - clamp01f(params_.attackLag), 2.0f) + 0.0005f;
        const float release = 0.20f * std::pow(1.0f - clamp01f(params_.releaseLag), 2.0f) + 0.0002f;
        const float contrast = 1.0f + clamp01f(params_.maskContrast) * 2.5f;
        const float ceiling = clamp(params_.maskCeiling, 0.5f, 1.0f);
        const float duckPow = 1.0f - clamp01f(params_.duckCurve) * 0.4f;
        const float wetLimit = clamp01f(params_.wetLimiter);

        for (uint32_t i = 0; i < k3OafxVirtualSpeakers; ++i) {
            const float wetSrc = slot.replace ? wetSum : effectWet24[i];
            const float wet = wetSrc * returnMask[i] * slot.returnGain;
            const float raw = std::min(ceiling, safePow(returnMask[i], contrast));
            const float lag = raw > mixerState.maskSmooth[i] ? attack : release;
            mixerState.maskSmooth[i] += lag * (raw - mixerState.maskSmooth[i]);
            const float smoothedMask = safePow(clamp(mixerState.maskSmooth[i], 0.0f, ceiling), duckPow);
            const float activeDuck = smoothedMask * wetSmoothed_;
            const float dryGain = params_.insertDuck ? std::cos(activeDuck * kPi * 0.5f) : 1.0f;
            const float wetSumSafe = wet * wetSmoothed_;
            const float wetSafe = (1.0f - wetLimit) * wetSumSafe + wetLimit * softSat(wetSumSafe);
            const float dryTerm = params_.directDry ? 0.0f : dry24[i] * drySmoothed_ * dryGain;
            mixed24[i] = (dryTerm + wetSafe) * outputSmoothed_;
        }
    }

    void processSlotReturnWithDryDuck(const float* dry24,
                                      const float* effectWet24,
                                      const ThreeOafxSlotParams& slot,
                                      AedMaskState& returnState,
                                      MixerState& mixerState,
                                      float* mixed24)
    {
        float returnMask[k3OafxVirtualSpeakers] {};
        computeMask(smoothDirection(returnState, slot.returnMask), slot.returnMask, returnMask);

        const float attack = 0.35f * std::pow(1.0f - clamp01f(params_.attackLag), 2.0f) + 0.0005f;
        const float release = 0.20f * std::pow(1.0f - clamp01f(params_.releaseLag), 2.0f) + 0.0002f;
        const float contrast = 1.0f + clamp01f(params_.maskContrast) * 2.5f;
        const float ceiling = clamp(params_.maskCeiling, 0.5f, 1.0f);
        const float duckPow = 1.0f - clamp01f(params_.duckCurve) * 0.4f;
        const float wetLimit = clamp01f(params_.wetLimiter);

        for (uint32_t i = 0; i < k3OafxVirtualSpeakers; ++i) {
            const float raw = std::min(ceiling, safePow(returnMask[i], contrast));
            const float lag = raw > mixerState.maskSmooth[i] ? attack : release;
            mixerState.maskSmooth[i] += lag * (raw - mixerState.maskSmooth[i]);
            const float smoothedMask = safePow(clamp(mixerState.maskSmooth[i], 0.0f, ceiling), duckPow);
            const float activeDuck = smoothedMask * wetSmoothed_;
            const float dryGain = params_.insertDuck ? std::cos(activeDuck * kPi * 0.5f) : 1.0f;
            const float dryTerm = dry24[i] * drySmoothed_ * dryGain;
            const float wet = effectWet24[i] * smoothedMask * slot.returnGain * wetSmoothed_;
            const float wetSafe = (1.0f - wetLimit) * wet + wetLimit * softSat(wet);
            mixed24[i] = (dryTerm + wetSafe) * outputSmoothed_;
        }
    }

    void processRackReturnWithDryDuck(const float* dry24,
                                      const float* effectWet24,
                                      MixerState& mixerState,
                                      float* mixed24)
    {
        float delayMask[k3OafxVirtualSpeakers] {};
        float pitchMask[k3OafxVirtualSpeakers] {};
        float filterMask[k3OafxVirtualSpeakers] {};
        float driveMask[k3OafxVirtualSpeakers] {};
        computeMask(smoothDirection(delayReturnMask_, params_.delay.returnMask), params_.delay.returnMask, delayMask);
        computeMask(smoothDirection(pitchReturnMask_, params_.pitch.returnMask), params_.pitch.returnMask, pitchMask);
        computeMask(smoothDirection(filterReturnMask_, params_.filter.returnMask), params_.filter.returnMask, filterMask);
        computeMask(smoothDirection(driveReturnMask_, params_.drive.returnMask), params_.drive.returnMask, driveMask);

        const float attack = 0.35f * std::pow(1.0f - clamp01f(params_.attackLag), 2.0f) + 0.0005f;
        const float release = 0.20f * std::pow(1.0f - clamp01f(params_.releaseLag), 2.0f) + 0.0002f;
        const float contrast = 1.0f + clamp01f(params_.maskContrast) * 2.5f;
        const float ceiling = clamp(params_.maskCeiling, 0.5f, 1.0f);
        const float duckPow = 1.0f - clamp01f(params_.duckCurve) * 0.4f;
        const float wetLimit = clamp01f(params_.wetLimiter);

        const float delayAmt = clamp01f(params_.delayFx.mix);
        const float pitchAmt = clamp01f(params_.pitchFx.mix);
        const float filterAmt = clamp01f(params_.filterMix);
        const float driveAmt = clamp01f(params_.driveMix);
        if (delayAmt + pitchAmt + filterAmt + driveAmt <= 0.00001f) {
            std::fill(mixed24, mixed24 + k3OafxVirtualSpeakers, 0.0f);
            mixerState.maskSmooth.fill(0.0f);
            return;
        }

        for (uint32_t i = 0; i < k3OafxVirtualSpeakers; ++i) {
            const float unionMask = clamp01f(std::max(std::max(delayMask[i] * delayAmt, pitchMask[i] * pitchAmt),
                                                     std::max(filterMask[i] * filterAmt, driveMask[i] * driveAmt)));
            const float raw = std::min(ceiling, safePow(unionMask, contrast));
            const float lag = raw > mixerState.maskSmooth[i] ? attack : release;
            mixerState.maskSmooth[i] += lag * (raw - mixerState.maskSmooth[i]);
            const float smoothedMask = safePow(clamp(mixerState.maskSmooth[i], 0.0f, ceiling), duckPow);
            const float activeDuck = smoothedMask * wetSmoothed_;
            const float dryGain = params_.insertDuck ? std::cos(activeDuck * kPi * 0.5f) : 1.0f;
            const float dryTerm = dry24[i] * drySmoothed_ * dryGain;
            const float wet = softSat(effectWet24[i]) * wetSmoothed_;
            const float wetSafe = (1.0f - wetLimit) * wet + wetLimit * softSat(wet);
            mixed24[i] = transparentLimit((dryTerm + wetSafe) * outputSmoothed_);
        }
    }

    void processRackReturnBuses(const float* delayWet24,
                                const float* pitchWet24,
                                const float* filterWet24,
                                const float* driveWet24,
                                float* wetBus24,
                                float* maskBus24)
    {
        std::fill(wetBus24, wetBus24 + k3OafxVirtualSpeakers, 0.0f);
        std::fill(maskBus24, maskBus24 + k3OafxVirtualSpeakers, 0.0f);
        const float delayAmt = clamp01f(params_.delayFx.mix);
        const float pitchAmt = clamp01f(params_.pitchFx.mix);
        const float filterAmt = clamp01f(params_.filterMix);
        const float driveAmt = clamp01f(params_.driveMix);
        const float parallelComp = 1.0f;
        addRackReturnSlotToBuses(delayWet24, params_.delay, delayReturnMask_, delayAmt, 1.0f * parallelComp, wetBus24, maskBus24);
        addRackReturnSlotToBuses(pitchWet24, params_.pitch, pitchReturnMask_, pitchAmt, 1.0f * parallelComp, wetBus24, maskBus24);
        addRackReturnSlotToBuses(filterWet24, params_.filter, filterReturnMask_, filterAmt, 0.95f * parallelComp, wetBus24, maskBus24);
        addRackReturnSlotToBuses(driveWet24, params_.drive, driveReturnMask_, driveAmt, 0.85f * parallelComp, wetBus24, maskBus24);
    }

    void addRackReturnSlotToBuses(const float* effectWet24,
                                  const ThreeOafxSlotParams& slot,
                                  AedMaskState& returnState,
                                  float mixAmount,
                                  float slotTrim,
                                  float* wetBus24,
                                  float* maskBus24)
    {
        if (!slot.enabled || mixAmount <= 0.00001f) return;

        float wetGainMask[k3OafxVirtualSpeakers] {};
        float rawMask[k3OafxVirtualSpeakers] {};
        computeRackReturnMasks(returnState, slot.returnMask, wetGainMask, rawMask);

        float wetSum = 0.0f;
        for (uint32_t i = 0; i < k3OafxVirtualSpeakers; ++i) {
            wetSum += effectWet24[i];
        }

        for (uint32_t i = 0; i < k3OafxVirtualSpeakers; ++i) {
            const float wetSrc = slot.replace ? wetSum : effectWet24[i];
            const float returnedWet = wetSrc * wetGainMask[i] * slot.returnGain * slotTrim;
            wetBus24[i] = flushDenormal(wetBus24[i] + returnedWet);
            maskBus24[i] = clamp01f(std::max(maskBus24[i], rawMask[i] * mixAmount));
        }
    }

    void processRackMixerBuses(const float* dryBus24,
                               const float* wetBus24,
                               const float* maskBus24,
                               MixerState& mixerState,
                               float* mixed24)
    {
        const float attack = 0.35f * std::pow(1.0f - clamp01f(params_.attackLag), 2.0f) + 0.0005f;
        const float release = 0.20f * std::pow(1.0f - clamp01f(params_.releaseLag), 2.0f) + 0.0002f;
        const float contrast = 1.0f + clamp01f(params_.maskContrast) * 2.5f;
        const float ceiling = clamp(params_.maskCeiling, 0.5f, 1.0f);
        const float duckPow = 1.0f - clamp01f(params_.duckCurve) * 0.4f;
        const float wetLimit = clamp01f(params_.wetLimiter);

        float maskSum = 0.0f;
        for (uint32_t i = 0; i < k3OafxVirtualSpeakers; ++i) {
            maskSum += clamp01f(maskBus24[i]);
        }
        const float invN = 1.0f / static_cast<float>(k3OafxVirtualSpeakers);
        const float maskAvg = maskSum * invN;
        const float activeSum = std::min(4.0f, effectMixSum());
        const float multiComp = 1.0f / (1.0f + std::max(0.0f, activeSum - 1.0f) * 0.20f);
        const float targetWetGain = (1.10f + maskAvg * 0.90f) * multiComp;
        const float gainLag = 0.0008f;
        rackWetGain_ += (targetWetGain - rackWetGain_) * gainLag;

        for (uint32_t i = 0; i < k3OafxVirtualSpeakers; ++i) {
            const float maskIn = clamp01f(maskBus24[i]);
            const float raw = std::min(ceiling, safePow(maskIn, contrast));
            const float lag = raw > mixerState.maskSmooth[i] ? attack : release;
            mixerState.maskSmooth[i] += lag * (raw - mixerState.maskSmooth[i]);
            const float smoothedMask = safePow(clamp(mixerState.maskSmooth[i], 0.0f, ceiling), duckPow);
            const float activeDuck = smoothedMask * wetSmoothed_;
            const float dryGain = params_.insertDuck ? std::cos(activeDuck * kPi * 0.5f) : 1.0f;
            const float wet = wetBus24[i] * wetSmoothed_ * rackWetGain_;
            const float wetSafe = (1.0f - wetLimit) * wet + wetLimit * softSat(wet);
            const float drySafe = params_.directDry ? dryBus24[i] * drySmoothed_ * dryGain : 0.0f;
            mixed24[i] = transparentLimit((drySafe + wetSafe) * outputSmoothed_);
        }
    }

    void processRackSlotsReturnWithDryDuck(const float* dry24,
                                           const float* delayWet24,
                                           const float* pitchWet24,
                                           const float* filterWet24,
                                           const float* driveWet24,
                                           MixerState& mixerState,
                                           float* mixed24)
    {
        float delayMask[k3OafxVirtualSpeakers] {};
        float delayDuckMask[k3OafxVirtualSpeakers] {};
        float pitchMask[k3OafxVirtualSpeakers] {};
        float pitchDuckMask[k3OafxVirtualSpeakers] {};
        float filterMask[k3OafxVirtualSpeakers] {};
        float filterDuckMask[k3OafxVirtualSpeakers] {};
        float driveMask[k3OafxVirtualSpeakers] {};
        float driveDuckMask[k3OafxVirtualSpeakers] {};
        computeRackReturnMasks(delayReturnMask_, params_.delay.returnMask, delayMask, delayDuckMask);
        computeRackReturnMasks(pitchReturnMask_, params_.pitch.returnMask, pitchMask, pitchDuckMask);
        computeRackReturnMasks(filterReturnMask_, params_.filter.returnMask, filterMask, filterDuckMask);
        computeRackReturnMasks(driveReturnMask_, params_.drive.returnMask, driveMask, driveDuckMask);

        const float attack = 0.35f * std::pow(1.0f - clamp01f(params_.attackLag), 2.0f) + 0.0005f;
        const float release = 0.20f * std::pow(1.0f - clamp01f(params_.releaseLag), 2.0f) + 0.0002f;
        const float contrast = 1.0f + clamp01f(params_.maskContrast) * 2.5f;
        const float ceiling = clamp(params_.maskCeiling, 0.5f, 1.0f);
        const float duckPow = 1.0f - clamp01f(params_.duckCurve) * 0.4f;
        const float wetLimit = clamp01f(params_.wetLimiter);

        const float delayAmt = clamp01f(params_.delayFx.mix);
        const float pitchAmt = clamp01f(params_.pitchFx.mix);
        const float filterAmt = clamp01f(params_.filterMix);
        const float driveAmt = clamp01f(params_.driveMix);
        if (delayAmt + pitchAmt + filterAmt + driveAmt <= 0.00001f) {
            std::fill(mixed24, mixed24 + k3OafxVirtualSpeakers, 0.0f);
            mixerState.maskSmooth.fill(0.0f);
            return;
        }

        for (uint32_t i = 0; i < k3OafxVirtualSpeakers; ++i) {
            const float delayActive = delayDuckMask[i] * delayAmt;
            const float pitchActive = pitchDuckMask[i] * pitchAmt;
            const float filterActive = filterDuckMask[i] * filterAmt;
            const float driveActive = driveDuckMask[i] * driveAmt;

            const float maskEnergy = clamp01f((delayActive + pitchActive + filterActive + driveActive) * 0.25f);
            const float wetComp = 1.0f / (1.0f + maskEnergy * 0.55f);
            const float delayWet = delayWet24[i] * delayMask[i] * params_.delay.returnGain;
            const float pitchWet = pitchWet24[i] * pitchMask[i] * params_.pitch.returnGain;
            const float filterWet = softSat(filterWet24[i]) * filterMask[i] * params_.filter.returnGain * 0.55f;
            const float driveWet = softSat(driveWet24[i]) * driveMask[i] * params_.drive.returnGain * 0.45f;
            const float wet = (delayWet + pitchWet + filterWet + driveWet) * wetComp * wetSmoothed_;
            const float wetSafe = (1.0f - wetLimit) * wet + wetLimit * softSat(wet);
            const float raw = std::min(ceiling, safePow(maskEnergy, contrast));
            const float lag = raw > mixerState.maskSmooth[i] ? attack : release;
            mixerState.maskSmooth[i] += lag * (raw - mixerState.maskSmooth[i]);
            const float smoothedMask = safePow(clamp(mixerState.maskSmooth[i], 0.0f, ceiling), duckPow);
            const float activeDuck = smoothedMask * wetSmoothed_;
            const float dryGain = params_.insertDuck ? std::cos(activeDuck * kPi * 0.5f) : 1.0f;
            const float dryTerm = params_.directDry ? dry24[i] * drySmoothed_ * dryGain : 0.0f;
            mixed24[i] = transparentLimit((dryTerm + wetSafe) * outputSmoothed_);
        }
    }

    void processRackCopyDiagnostic(const float* source24, float* mixed24)
    {
        float delayMask[k3OafxVirtualSpeakers] {};
        float delayDuckMask[k3OafxVirtualSpeakers] {};
        float pitchMask[k3OafxVirtualSpeakers] {};
        float pitchDuckMask[k3OafxVirtualSpeakers] {};
        float filterMask[k3OafxVirtualSpeakers] {};
        float filterDuckMask[k3OafxVirtualSpeakers] {};
        float driveMask[k3OafxVirtualSpeakers] {};
        float driveDuckMask[k3OafxVirtualSpeakers] {};
        computeRackReturnMasks(delayReturnMask_, params_.delay.returnMask, delayMask, delayDuckMask);
        computeRackReturnMasks(pitchReturnMask_, params_.pitch.returnMask, pitchMask, pitchDuckMask);
        computeRackReturnMasks(filterReturnMask_, params_.filter.returnMask, filterMask, filterDuckMask);
        computeRackReturnMasks(driveReturnMask_, params_.drive.returnMask, driveMask, driveDuckMask);

        const float delayAmt = clamp01f(params_.delayFx.mix);
        const float pitchAmt = clamp01f(params_.pitchFx.mix);
        const float filterAmt = clamp01f(params_.filterMix);
        const float driveAmt = clamp01f(params_.driveMix);
        const float total = std::max(0.00001f, delayAmt + pitchAmt + filterAmt + driveAmt);

        for (uint32_t i = 0; i < k3OafxVirtualSpeakers; ++i) {
            const float weightedMask =
                delayMask[i] * delayAmt
                + pitchMask[i] * pitchAmt
                + filterMask[i] * filterAmt
                + driveMask[i] * driveAmt;
            const float mask = clamp01f(weightedMask / total);
            mixed24[i] = transparentLimit(source24[i] * mask * k3OafxRackCopyDiagnosticGain * outputSmoothed_);
        }
    }

    void computeRackReturnMasks(AedMaskState& state,
                                const AedMaskParams& params,
                                float* wetGainMask,
                                float* duckMask)
    {
        const Vec3 target = smoothDirection(state, params);
        float powSum = 0.0f;
        for (uint32_t i = 0; i < k3OafxVirtualSpeakers; ++i) {
            const float raw = computeMaskValue(k3OafxPoints[i], target, params);
            duckMask[i] = raw;
            powSum += raw * raw;
        }
        const float comp = (1.0f - clamp01f(params.energyComp))
            + clamp01f(params.energyComp) / std::max(1.0f, std::sqrt(powSum));
        for (uint32_t i = 0; i < k3OafxVirtualSpeakers; ++i) {
            wetGainMask[i] = clamp01f(safePow(duckMask[i], params.gamma) * comp * clamp01f(params.level));
        }
    }

    void updateMixerSmoothing()
    {
        drySmoothed_ += (params_.dry - drySmoothed_) * 0.0015f;
        wetSmoothed_ = 1.0f;
        outputSmoothed_ += (params_.output - outputSmoothed_) * 0.0015f;
    }

    void updateRackWetActivity()
    {
        const float target = clamp01f(effectMixSum());
        const float coeff = target > rackWetActivity_ ? 0.0018f : 0.0035f;
        rackWetActivity_ += (target - rackWetActivity_) * coeff;
    }

    float effectMixSum() const
    {
        return clamp01f(params_.delayFx.mix)
            + clamp01f(params_.pitchFx.mix)
            + clamp01f(params_.filterMix)
            + clamp01f(params_.driveMix);
    }

    void decodeInputToVirtual24(const float* input3Oa, float* output24) const
    {
        const auto& matrix = ambiDecoder_.matrix();
        for (uint32_t spk = 0; spk < k3OafxVirtualSpeakers; ++spk) {
            float value = 0.0f;
            for (uint32_t ch = 0; ch < k3OaChannels; ++ch) {
                value += input3Oa[ch] * matrix[spk][ch];
            }
            output24[spk] = flushDenormal(value);
        }
    }

    void encodeVirtual24ToOutput(const float* input24, float* output3Oa) const
    {
        // Canonical 3OAFX rack flow: AmbiDEC-like decode, JSFX-style
        // virtual-speaker mask/mixer, then AmbiENC-like 3OA encode.
        encodeVirtual24To3Oa(input24, output3Oa);
        for (uint32_t ch = 0; ch < k3OaChannels; ++ch) {
            output3Oa[ch] = flushDenormal(output3Oa[ch] * rackEncodeMakeup_);
        }
    }

    void updateRackEncodeMakeup()
    {
        float decodedW[k3OafxVirtualSpeakers] {};
        float encoded[k3OaChannels] {};
        const auto& matrix = ambiDecoder_.matrix();
        for (uint32_t spk = 0; spk < k3OafxVirtualSpeakers; ++spk) {
            decodedW[spk] = matrix[spk][0];
        }
        encodeVirtual24To3Oa(decodedW, encoded);
        rackEncodeMakeup_ = clamp(1.0f / std::max(0.0001f, std::abs(encoded[0])), 0.25f, 8.0f);
    }

    void rebuildMatchedEncoder()
    {
        for (auto& row : encodeMatrix_) row.fill(0.0f);
        const auto& decode = ambiDecoder_.matrix();
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

    static ThreeOafxSlotParams sanitizeSlot(ThreeOafxSlotParams slot)
    {
        slot.sendMask = sanitizeMask(slot.sendMask);
        slot.returnMask = sanitizeMask(slot.returnMask);
        slot.send = clamp01f(slot.send);
        slot.returnGain = clamp01f(slot.returnGain);
        return slot;
    }

    static AedMaskParams sanitizeMask(AedMaskParams mask)
    {
        mask.azimuthDeg = clamp(mask.azimuthDeg, -179.9f, 179.9f);
        mask.elevationDeg = clamp(mask.elevationDeg, -90.0f, 90.0f);
        mask.smoothing = clamp01f(mask.smoothing);
        mask.width = clamp01f(mask.width);
        mask.focus = clamp01f(mask.focus);
        mask.level = clamp01f(mask.level);
        mask.floor = clamp(mask.floor, 0.0f, 0.25f);
        mask.rearReject = clamp01f(mask.rearReject);
        mask.energyComp = clamp01f(mask.energyComp);
        mask.gamma = clamp(mask.gamma, 0.25f, 4.0f);
        return mask;
    }

    static ThreeOafxRackParams sanitize(ThreeOafxRackParams params)
    {
        params.dry = clamp01f(params.dry);
        params.wet = 1.0f;
        params.output = clamp01f(params.output);
        params.maskContrast = clamp01f(params.maskContrast);
        params.maskCeiling = clamp(params.maskCeiling, 0.5f, 1.0f);
        params.duckCurve = clamp01f(params.duckCurve);
        params.wetLimiter = clamp01f(params.wetLimiter);
        params.attackLag = clamp01f(params.attackLag);
        params.releaseLag = clamp01f(params.releaseLag);
        params.delay = sanitizeSlot(params.delay);
        params.pitch = sanitizeSlot(params.pitch);
        params.filter = sanitizeSlot(params.filter);
        params.drive = sanitizeSlot(params.drive);
        params.delay.returnMask = rackReturnMaskShape(params.delay.returnMask);
        params.pitch.returnMask = rackReturnMaskShape(params.pitch.returnMask);
        params.filter.returnMask = rackReturnMaskShape(params.filter.returnMask);
        params.drive.returnMask = rackReturnMaskShape(params.drive.returnMask);
        params.filterMix = clamp01f(params.filterMix);
        params.filterTone = clamp01f(params.filterTone);
        params.driveMix = clamp01f(params.driveMix);
        params.driveAmount = clamp01f(params.driveAmount);
        return params;
    }

    static AedMaskParams rackReturnMaskShape(AedMaskParams mask)
    {
        mask.focus = 0.0f;
        mask.floor = 0.03f;
        mask.rearReject = 1.0f;
        mask.energyComp = 0.50f;
        mask.gamma = 1.25f;
        return sanitizeMask(mask);
    }

    double sampleRate_ = 48000.0;
    ThreeOafxRackParams params_ {};
    AmbiSpeakerDecoder ambiDecoder_ {};
    std::array<std::array<float, k3OafxVirtualSpeakers>, k3OaChannels> encodeMatrix_ {};
    AedMaskState delaySendMask_ {};
    AedMaskState delayReturnMask_ {};
    AedMaskState pitchSendMask_ {};
    AedMaskState pitchReturnMask_ {};
    AedMaskState filterSendMask_ {};
    AedMaskState filterReturnMask_ {};
    AedMaskState driveSendMask_ {};
    AedMaskState driveReturnMask_ {};
    MixerState delayMixer_ {};
    MixerState pitchMixer_ {};
    MixerState rackMixer_ {};
    MacroDelay delay_ {};
    MacroPitch pitch_ {};
    std::array<std::vector<float>, k3OafxVirtualSpeakers> rackDelayBuffer_ {};
    uint32_t rackDelaySize_ = 0;
    uint32_t rackDelayWrite_ = 0;
    float rackDelayCurrentMs_ = 320.0f;
    float rackDelayPreviousMs_ = 320.0f;
    float rackDelayTargetMs_ = 320.0f;
    float rackDelayFade_ = 1.0f;
    float rackDelayFeedback_ = 0.22f;
    float rackDelayMix_ = 0.0f;
    float rackWetActivity_ = 0.0f;
    float rackWetGain_ = 1.0f;
    float rackEncodeMakeup_ = 1.0f;
    std::array<std::vector<float>, k3OafxVirtualSpeakers> rackPitchBuffer_ {};
    std::array<float, k3OafxVirtualSpeakers> rackPitchPhase_ {};
    uint32_t rackPitchSize_ = 0;
    uint32_t rackPitchWrite_ = 0;
    float rackPitchRatio_ = 1.0f;
    std::array<float, k3OafxVirtualSpeakers> rackFilterState_ {};
    float drySmoothed_ = 0.65f;
    float wetSmoothed_ = 1.0f;
    float outputSmoothed_ = 0.90f;
};

} // namespace s3g
