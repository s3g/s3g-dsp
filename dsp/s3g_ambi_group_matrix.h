#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include "s3g_matrix_flow_shapes.h"

namespace s3g {

constexpr uint32_t kAmbiGroupMatrixGroups = 4;
constexpr uint32_t kAmbiGroupMatrixGroupChannels = 16;
constexpr uint32_t kAmbiGroupMatrixChannels = kAmbiGroupMatrixGroups * kAmbiGroupMatrixGroupChannels;

enum class AmbiGroupMatrixFlowMode : uint32_t {
    Free = 0,
    Sync = 1,
};

struct AmbiGroupMatrixParams {
    std::array<float, kAmbiGroupMatrixGroups * kAmbiGroupMatrixGroups> crosspointDb {};
    float flow = 0.0f;
    float spread = 0.0f;
    float vortex = 0.0f;
    float motion = 0.0f;
    MatrixFlowShape shape = MatrixFlowShape::Flow;
    AmbiGroupMatrixFlowMode mode = AmbiGroupMatrixFlowMode::Free;
    float rate = 0.15f;
    float divisionBeats = 16.0f;
    float phaseOffset = 0.0f;
    float smoothingMs = 35.0f;
    float outputGainDb = 0.0f;
};

inline float ambiGroupMatrixClamp(float v, float lo, float hi)
{
    return std::max(lo, std::min(v, hi));
}

inline float ambiGroupMatrixDbToGain(float db)
{
    if (db <= -79.9f) {
        return 0.0f;
    }
    return std::pow(10.0f, db / 20.0f);
}

inline float ambiGroupMatrixGainToDb(float gain)
{
    return 20.0f * std::log10(std::max(0.000001f, gain));
}

inline uint32_t ambiGroupMatrixIndex(uint32_t srcGroup, uint32_t dstGroup)
{
    return srcGroup * kAmbiGroupMatrixGroups + dstGroup;
}

inline AmbiGroupMatrixParams makeDefaultAmbiGroupMatrixParams()
{
    AmbiGroupMatrixParams p {};
    for (uint32_t src = 0; src < kAmbiGroupMatrixGroups; ++src) {
        for (uint32_t dst = 0; dst < kAmbiGroupMatrixGroups; ++dst) {
            p.crosspointDb[ambiGroupMatrixIndex(src, dst)] = src == dst ? 0.0f : -80.0f;
        }
    }
    return p;
}

class AmbiGroupMatrix {
public:
    AmbiGroupMatrix()
    {
        params_ = makeDefaultAmbiGroupMatrixParams();
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

    void setParams(const AmbiGroupMatrixParams& params)
    {
        params_ = sanitize(params);
        rebuildTargets();
    }

    const AmbiGroupMatrixParams& params() const { return params_; }
    const std::array<float, kAmbiGroupMatrixGroups * kAmbiGroupMatrixGroups>& targetGain() const { return targetGain_; }
    std::array<float, kAmbiGroupMatrixGroups * kAmbiGroupMatrixGroups> generatedFlowPreview(float phase = 0.0f) const { return generatedFlowMatrix(phase); }
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

        const uint32_t channels = std::min<uint32_t>(kAmbiGroupMatrixChannels, outputChannels);
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

            for (uint32_t src = 0; src < kAmbiGroupMatrixGroups; ++src) {
                for (uint32_t dst = 0; dst < kAmbiGroupMatrixGroups; ++dst) {
                    const float gain = currentGain_[ambiGroupMatrixIndex(src, dst)];
                    if (gain <= 0.000001f) {
                        continue;
                    }

                    for (uint32_t lane = 0; lane < kAmbiGroupMatrixGroupChannels; ++lane) {
                        const uint32_t inCh = src * kAmbiGroupMatrixGroupChannels + lane;
                        const uint32_t outCh = dst * kAmbiGroupMatrixGroupChannels + lane;
                        if (inCh < inputChannels && outCh < outputChannels && inputs[inCh] && outputs[outCh]) {
                            outputs[outCh][frame] += static_cast<Sample>(inputs[inCh][frame] * static_cast<Sample>(gain));
                        }
                    }
                }
            }
        }
    }

private:
    static AmbiGroupMatrixParams sanitize(AmbiGroupMatrixParams p)
    {
        for (float& db : p.crosspointDb) {
            db = ambiGroupMatrixClamp(db, -80.0f, 12.0f);
        }
        p.flow = ambiGroupMatrixClamp(p.flow, 0.0f, 1.0f);
        p.spread = ambiGroupMatrixClamp(p.spread, 0.0f, 1.0f);
        p.vortex = ambiGroupMatrixClamp(p.vortex, -1.0f, 1.0f);
        p.motion = ambiGroupMatrixClamp(p.motion, 0.0f, 1.0f);
        p.shape = matrixFlowShapeFromIndex(static_cast<uint32_t>(p.shape));
        p.mode = static_cast<AmbiGroupMatrixFlowMode>(
            std::min<uint32_t>(static_cast<uint32_t>(p.mode), static_cast<uint32_t>(AmbiGroupMatrixFlowMode::Sync)));
        p.rate = ambiGroupMatrixClamp(p.rate, 0.0f, 1.0f);
        p.divisionBeats = ambiGroupMatrixClamp(p.divisionBeats, 0.25f, 64.0f);
        p.phaseOffset = ambiGroupMatrixClamp(p.phaseOffset, 0.0f, 1.0f);
        p.smoothingMs = ambiGroupMatrixClamp(p.smoothingMs, 1.0f, 500.0f);
        p.outputGainDb = ambiGroupMatrixClamp(p.outputGainDb, -60.0f, 12.0f);
        return p;
    }

    void rebuildTargets()
    {
        const float out = ambiGroupMatrixDbToGain(params_.outputGainDb);
        const float phase = wrapPhase((useExternalPhase_ ? externalPhase_ : freePhase_) + params_.phaseOffset);
        const auto generated = generatedFlowMatrix(phase);
        for (uint32_t src = 0; src < kAmbiGroupMatrixGroups; ++src) {
            for (uint32_t dst = 0; dst < kAmbiGroupMatrixGroups; ++dst) {
                const uint32_t idx = ambiGroupMatrixIndex(src, dst);
                const float manual = ambiGroupMatrixDbToGain(params_.crosspointDb[idx]);
                const float flowShape = 1.0f - params_.motion + generated[idx] * params_.motion;
                targetGain_[idx] = manual * flowShape * out;
            }
        }
    }

    std::array<float, kAmbiGroupMatrixGroups * kAmbiGroupMatrixGroups> generatedFlowMatrix(float phase) const
    {
        std::array<float, kAmbiGroupMatrixGroups * kAmbiGroupMatrixGroups> g {};
        const float phaseForShape = params_.shape == MatrixFlowShape::Hold ? 0.0f : phase;
        const float width = 0.08f + params_.spread * 1.55f + params_.flow * 0.72f;
        const float angle = phaseForShape * 6.28318530718f;
        const float coords[kAmbiGroupMatrixGroups][2] {
            { -1.0f, 1.0f },
            { 1.0f, 1.0f },
            { -1.0f, -1.0f },
            { 1.0f, -1.0f },
        };

        if (params_.shape == MatrixFlowShape::Pulse) {
            for (uint32_t src = 0; src < kAmbiGroupMatrixGroups; ++src) {
                for (uint32_t dst = 0; dst < kAmbiGroupMatrixGroups; ++dst) {
                    const float offset = static_cast<float>(src + dst) * 0.125f * params_.spread;
                    g[ambiGroupMatrixIndex(src, dst)] = matrixFlowPulse(phaseForShape + offset);
                }
            }
            return g;
        }

        if (params_.shape == MatrixFlowShape::Chase) {
            const float chaseWidth = 0.35f + params_.spread * 2.4f;
            const float pos = phaseForShape * static_cast<float>(kAmbiGroupMatrixGroups);
            for (uint32_t src = 0; src < kAmbiGroupMatrixGroups; ++src) {
                float sum = 0.0f;
                const float center = std::fmod(static_cast<float>(src) + pos, static_cast<float>(kAmbiGroupMatrixGroups));
                for (uint32_t dst = 0; dst < kAmbiGroupMatrixGroups; ++dst) {
                    const float w = matrixFlowRingWeight(kAmbiGroupMatrixGroups, dst, center, chaseWidth);
                    g[ambiGroupMatrixIndex(src, dst)] = w;
                    sum += w;
                }
                if (sum > 0.000001f) {
                    for (uint32_t dst = 0; dst < kAmbiGroupMatrixGroups; ++dst) {
                        g[ambiGroupMatrixIndex(src, dst)] /= sum;
                    }
                }
            }
            return g;
        }

        if (params_.shape == MatrixFlowShape::Scatter) {
            const float step = phaseForShape * 12.0f;
            const uint32_t seedA = static_cast<uint32_t>(std::floor(step));
            const uint32_t seedB = seedA + 1u;
            const float t = 0.5f - 0.5f * std::cos((step - std::floor(step)) * 3.14159265359f);
            const float threshold = 0.74f - params_.spread * 0.42f;
            for (uint32_t src = 0; src < kAmbiGroupMatrixGroups; ++src) {
                float sum = 0.0f;
                for (uint32_t dst = 0; dst < kAmbiGroupMatrixGroups; ++dst) {
                    const float a = matrixFlowHash01(src, dst, seedA);
                    const float b = matrixFlowHash01(src, dst, seedB);
                    const float h = a + (b - a) * t;
                    const float w = std::max(0.0f, (h - threshold) / std::max(0.05f, 1.0f - threshold));
                    g[ambiGroupMatrixIndex(src, dst)] = 0.04f + w;
                    sum += g[ambiGroupMatrixIndex(src, dst)];
                }
                if (sum > 0.000001f) {
                    for (uint32_t dst = 0; dst < kAmbiGroupMatrixGroups; ++dst) {
                        g[ambiGroupMatrixIndex(src, dst)] /= sum;
                    }
                }
            }
            return g;
        }

        const bool swirlShape = params_.shape == MatrixFlowShape::Swirl;
        const float vortex = params_.vortex + (swirlShape ? 0.75f : 0.0f);
        const float orbitX = std::cos(angle) * params_.flow * (swirlShape ? 0.74f : 1.0f);
        const float orbitY = std::sin(angle) * params_.flow * (swirlShape ? 0.74f : 1.0f);

        for (uint32_t src = 0; src < kAmbiGroupMatrixGroups; ++src) {
            float cx = coords[src][0];
            float cy = coords[src][1];
            const float swirlX = -cy * vortex * params_.flow;
            const float swirlY = cx * vortex * params_.flow;
            const float sourcePhase = angle + static_cast<float>(src) * 1.57079632679f;
            const float swirlAmp = swirlShape ? 0.36f : 0.20f;
            cx = cx * (1.0f - 0.32f * params_.flow) + swirlX + orbitX * 0.46f + std::cos(sourcePhase) * params_.flow * swirlAmp;
            cy = cy * (1.0f - 0.32f * params_.flow) + swirlY + orbitY * 0.46f + std::sin(sourcePhase) * params_.flow * swirlAmp;

            float sum = 0.0f;
            for (uint32_t dst = 0; dst < kAmbiGroupMatrixGroups; ++dst) {
                const float dx = coords[dst][0] - cx;
                const float dy = coords[dst][1] - cy;
                const float d2 = dx * dx + dy * dy;
                const float w = std::exp(-d2 / std::max(0.001f, width * width));
                g[ambiGroupMatrixIndex(src, dst)] = w;
                sum += w;
            }
            if (sum > 0.000001f) {
                for (uint32_t dst = 0; dst < kAmbiGroupMatrixGroups; ++dst) {
                    g[ambiGroupMatrixIndex(src, dst)] /= sum;
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
    AmbiGroupMatrixParams params_ {};
    std::array<float, kAmbiGroupMatrixGroups * kAmbiGroupMatrixGroups> currentGain_ {};
    std::array<float, kAmbiGroupMatrixGroups * kAmbiGroupMatrixGroups> targetGain_ {};
    float freePhase_ = 0.0f;
    float externalPhase_ = 0.0f;
    bool useExternalPhase_ = false;
};

} // namespace s3g
