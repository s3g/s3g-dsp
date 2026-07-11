#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace s3g {

constexpr uint32_t kAmbiGroupMatrix128Groups = 8;
constexpr uint32_t kAmbiGroupMatrix128GroupChannels = 16;
constexpr uint32_t kAmbiGroupMatrix128Channels = kAmbiGroupMatrix128Groups * kAmbiGroupMatrix128GroupChannels;

enum class AmbiGroupMatrix128FlowMode : uint32_t {
    Free = 0,
    Sync = 1,
};

struct AmbiGroupMatrix128Params {
    std::array<float, kAmbiGroupMatrix128Groups * kAmbiGroupMatrix128Groups> crosspointDb {};
    float flow = 0.0f;
    float spread = 0.0f;
    float vortex = 0.0f;
    float motion = 0.0f;
    AmbiGroupMatrix128FlowMode mode = AmbiGroupMatrix128FlowMode::Free;
    float rate = 0.15f;
    float divisionBeats = 16.0f;
    float phaseOffset = 0.0f;
    float smoothingMs = 35.0f;
    float outputGainDb = 0.0f;
};

inline float AmbiGroupMatrix128Clamp(float v, float lo, float hi)
{
    return std::max(lo, std::min(v, hi));
}

inline float AmbiGroupMatrix128DbToGain(float db)
{
    if (db <= -79.9f) {
        return 0.0f;
    }
    return std::pow(10.0f, db / 20.0f);
}

inline float AmbiGroupMatrix128GainToDb(float gain)
{
    return 20.0f * std::log10(std::max(0.000001f, gain));
}

inline uint32_t AmbiGroupMatrix128Index(uint32_t srcGroup, uint32_t dstGroup)
{
    return srcGroup * kAmbiGroupMatrix128Groups + dstGroup;
}

inline AmbiGroupMatrix128Params makeDefaultAmbiGroupMatrix128Params()
{
    AmbiGroupMatrix128Params p {};
    for (uint32_t src = 0; src < kAmbiGroupMatrix128Groups; ++src) {
        for (uint32_t dst = 0; dst < kAmbiGroupMatrix128Groups; ++dst) {
            p.crosspointDb[AmbiGroupMatrix128Index(src, dst)] = src == dst ? 0.0f : -80.0f;
        }
    }
    return p;
}

class AmbiGroupMatrix128 {
public:
    AmbiGroupMatrix128()
    {
        params_ = makeDefaultAmbiGroupMatrix128Params();
        currentGain_.fill(0.0f);
        targetGain_.fill(0.0f);
        rebuildTargets();
        currentGain_ = targetGain_;
    }

    void prepare(double sampleRate)
    {
        sampleRate_ = std::max(1000.0, sampleRate);
        reset();
    }

    void reset()
    {
        rebuildTargets();
        currentGain_ = targetGain_;
        freePhase_ = 0.0f;
    }

    void setParams(const AmbiGroupMatrix128Params& params)
    {
        params_ = sanitize(params);
        rebuildTargets();
    }

    const AmbiGroupMatrix128Params& params() const { return params_; }
    const std::array<float, kAmbiGroupMatrix128Groups * kAmbiGroupMatrix128Groups>& targetGain() const { return targetGain_; }
    std::array<float, kAmbiGroupMatrix128Groups * kAmbiGroupMatrix128Groups> generatedFlowPreview(float phase = 0.0f) const { return generatedFlowMatrix(phase); }
    float previewPhase() const { return wrapPhase((useExternalPhase_ ? externalPhase_ : freePhase_) + params_.phaseOffset); }

    void setExternalPhase(float phase)
    {
        externalPhase_ = wrapPhase(phase);
        useExternalPhase_ = true;
        rebuildTargets();
    }

    void useFreePhase()
    {
        useExternalPhase_ = false;
    }

    float freePhase() const { return freePhase_; }

    template <typename Sample>
    void process(Sample* const* inputs, uint32_t inputChannels, Sample* const* outputs, uint32_t outputChannels, uint32_t frames)
    {
        if (!inputs || !outputs || frames == 0) {
            return;
        }

        const uint32_t channels = std::min<uint32_t>(kAmbiGroupMatrix128Channels, outputChannels);
        for (uint32_t ch = 0; ch < channels; ++ch) {
            if (outputs[ch]) {
                std::fill(outputs[ch], outputs[ch] + frames, Sample { 0 });
            }
        }

        const float smoothing = smoothingCoeff();
        for (uint32_t frame = 0; frame < frames; ++frame) {
            if (!useExternalPhase_ && params_.motion > 0.0001f) {
                const float hz = 0.005f + params_.rate * params_.rate * 0.995f;
                freePhase_ = wrapPhase(freePhase_ + hz / static_cast<float>(sampleRate_));
                if ((frame & 31u) == 0u) {
                    rebuildTargets();
                }
            }
            for (uint32_t i = 0; i < currentGain_.size(); ++i) {
                currentGain_[i] += (targetGain_[i] - currentGain_[i]) * smoothing;
            }

            for (uint32_t src = 0; src < kAmbiGroupMatrix128Groups; ++src) {
                for (uint32_t dst = 0; dst < kAmbiGroupMatrix128Groups; ++dst) {
                    const float gain = currentGain_[AmbiGroupMatrix128Index(src, dst)];
                    if (gain <= 0.000001f) {
                        continue;
                    }

                    for (uint32_t lane = 0; lane < kAmbiGroupMatrix128GroupChannels; ++lane) {
                        const uint32_t inCh = src * kAmbiGroupMatrix128GroupChannels + lane;
                        const uint32_t outCh = dst * kAmbiGroupMatrix128GroupChannels + lane;
                        if (inCh < inputChannels && outCh < outputChannels && inputs[inCh] && outputs[outCh]) {
                            outputs[outCh][frame] += static_cast<Sample>(inputs[inCh][frame] * static_cast<Sample>(gain));
                        }
                    }
                }
            }
        }
    }

private:
    static AmbiGroupMatrix128Params sanitize(AmbiGroupMatrix128Params p)
    {
        for (float& db : p.crosspointDb) {
            db = AmbiGroupMatrix128Clamp(db, -80.0f, 12.0f);
        }
        p.flow = AmbiGroupMatrix128Clamp(p.flow, 0.0f, 1.0f);
        p.spread = AmbiGroupMatrix128Clamp(p.spread, 0.0f, 1.0f);
        p.vortex = AmbiGroupMatrix128Clamp(p.vortex, -1.0f, 1.0f);
        p.motion = AmbiGroupMatrix128Clamp(p.motion, 0.0f, 1.0f);
        p.mode = static_cast<AmbiGroupMatrix128FlowMode>(
            std::min<uint32_t>(static_cast<uint32_t>(p.mode), static_cast<uint32_t>(AmbiGroupMatrix128FlowMode::Sync)));
        p.rate = AmbiGroupMatrix128Clamp(p.rate, 0.0f, 1.0f);
        p.divisionBeats = AmbiGroupMatrix128Clamp(p.divisionBeats, 0.25f, 64.0f);
        p.phaseOffset = AmbiGroupMatrix128Clamp(p.phaseOffset, 0.0f, 1.0f);
        p.smoothingMs = AmbiGroupMatrix128Clamp(p.smoothingMs, 1.0f, 500.0f);
        p.outputGainDb = AmbiGroupMatrix128Clamp(p.outputGainDb, -60.0f, 12.0f);
        return p;
    }

    void rebuildTargets()
    {
        const float out = AmbiGroupMatrix128DbToGain(params_.outputGainDb);
        const float phase = wrapPhase((useExternalPhase_ ? externalPhase_ : freePhase_) + params_.phaseOffset);
        const auto generated = generatedFlowMatrix(phase);
        for (uint32_t src = 0; src < kAmbiGroupMatrix128Groups; ++src) {
            for (uint32_t dst = 0; dst < kAmbiGroupMatrix128Groups; ++dst) {
                const uint32_t idx = AmbiGroupMatrix128Index(src, dst);
                const float manual = AmbiGroupMatrix128DbToGain(params_.crosspointDb[idx]);
                const float flowShape = 1.0f - params_.motion + generated[idx] * params_.motion;
                targetGain_[idx] = manual * flowShape * out;
            }
        }
    }

    std::array<float, kAmbiGroupMatrix128Groups * kAmbiGroupMatrix128Groups> generatedFlowMatrix(float phase) const
    {
        std::array<float, kAmbiGroupMatrix128Groups * kAmbiGroupMatrix128Groups> g {};
        const float width = 0.08f + params_.spread * 1.55f + params_.flow * 0.72f;
        const float vortex = params_.vortex;
        const float angle = phase * 6.28318530718f;
        const float orbitX = std::cos(angle) * params_.flow;
        const float orbitY = std::sin(angle) * params_.flow;
        for (uint32_t src = 0; src < kAmbiGroupMatrix128Groups; ++src) {
            const float srcAngle = static_cast<float>(src) / static_cast<float>(kAmbiGroupMatrix128Groups) * 6.28318530718f;
            float cx = std::cos(srcAngle);
            float cy = std::sin(srcAngle);
            const float swirlX = -cy * vortex * params_.flow;
            const float swirlY = cx * vortex * params_.flow;
            const float sourcePhase = angle + srcAngle;
            cx = cx * (1.0f - 0.32f * params_.flow) + swirlX + orbitX * 0.46f + std::cos(sourcePhase) * params_.flow * 0.20f;
            cy = cy * (1.0f - 0.32f * params_.flow) + swirlY + orbitY * 0.46f + std::sin(sourcePhase) * params_.flow * 0.20f;

            float sum = 0.0f;
            for (uint32_t dst = 0; dst < kAmbiGroupMatrix128Groups; ++dst) {
                const float dstAngle = static_cast<float>(dst) / static_cast<float>(kAmbiGroupMatrix128Groups) * 6.28318530718f;
                const float dx = std::cos(dstAngle) - cx;
                const float dy = std::sin(dstAngle) - cy;
                const float d2 = dx * dx + dy * dy;
                const float w = std::exp(-d2 / std::max(0.001f, width * width));
                g[AmbiGroupMatrix128Index(src, dst)] = w;
                sum += w;
            }
            if (sum > 0.000001f) {
                for (uint32_t dst = 0; dst < kAmbiGroupMatrix128Groups; ++dst) {
                    g[AmbiGroupMatrix128Index(src, dst)] /= sum;
                }
            }
        }
        return g;
    }

    static float wrapPhase(float phase)
    {
        phase = std::fmod(phase, 1.0f);
        return phase < 0.0f ? phase + 1.0f : phase;
    }

    float smoothingCoeff() const
    {
        const float ms = std::max(1.0f, params_.smoothingMs);
        return 1.0f - std::exp(-1.0f / static_cast<float>(sampleRate_ * ms * 0.001));
    }

    double sampleRate_ = 48000.0;
    AmbiGroupMatrix128Params params_ {};
    std::array<float, kAmbiGroupMatrix128Groups * kAmbiGroupMatrix128Groups> currentGain_ {};
    std::array<float, kAmbiGroupMatrix128Groups * kAmbiGroupMatrix128Groups> targetGain_ {};
    float freePhase_ = 0.0f;
    float externalPhase_ = 0.0f;
    bool useExternalPhase_ = false;
};

} // namespace s3g
