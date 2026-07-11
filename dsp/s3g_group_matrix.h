#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include "s3g_matrix_flow_shapes.h"

namespace s3g {

constexpr uint32_t kGroupMatrixChannels = 64;
constexpr uint32_t kGroupMatrixMaxGroups = 16;
constexpr uint32_t kGroupMatrixMaxCrosspoints = kGroupMatrixMaxGroups * kGroupMatrixMaxGroups;

enum class GroupMatrixSize : uint32_t {
    Ch4 = 0,
    Ch8 = 1,
    Ch16 = 2,
};

enum class GroupMatrixFlowMode : uint32_t {
    Free = 0,
    Sync = 1,
};

struct GroupMatrixParams {
    std::array<float, kGroupMatrixMaxCrosspoints> crosspointDb {};
    GroupMatrixSize groupSize = GroupMatrixSize::Ch8;
    float flow = 0.0f;
    float spread = 0.0f;
    float vortex = 0.0f;
    float motion = 0.0f;
    MatrixFlowShape shape = MatrixFlowShape::Flow;
    GroupMatrixFlowMode mode = GroupMatrixFlowMode::Free;
    float rate = 0.15f;
    float divisionBeats = 16.0f;
    float phaseOffset = 0.0f;
    float smoothingMs = 35.0f;
    float outputGainDb = 0.0f;
};

inline float groupMatrixClamp(float v, float lo, float hi)
{
    return std::max(lo, std::min(v, hi));
}

inline float groupMatrixDbToGain(float db)
{
    if (db <= -79.9f) {
        return 0.0f;
    }
    return std::pow(10.0f, db / 20.0f);
}

inline uint32_t groupMatrixGroupChannels(GroupMatrixSize size)
{
    switch (size) {
    case GroupMatrixSize::Ch4: return 4;
    case GroupMatrixSize::Ch8: return 8;
    case GroupMatrixSize::Ch16: return 16;
    default: return 8;
    }
}

inline const char* groupMatrixSizeName(GroupMatrixSize size)
{
    switch (size) {
    case GroupMatrixSize::Ch4: return "4CH";
    case GroupMatrixSize::Ch8: return "8CH";
    case GroupMatrixSize::Ch16: return "16CH";
    default: return "8CH";
    }
}

inline uint32_t groupMatrixActiveGroups(GroupMatrixSize size)
{
    return kGroupMatrixChannels / groupMatrixGroupChannels(size);
}

inline uint32_t groupMatrixIndex(uint32_t srcGroup, uint32_t dstGroup)
{
    return srcGroup * kGroupMatrixMaxGroups + dstGroup;
}

inline GroupMatrixParams makeDefaultGroupMatrixParams()
{
    GroupMatrixParams p {};
    for (uint32_t src = 0; src < kGroupMatrixMaxGroups; ++src) {
        for (uint32_t dst = 0; dst < kGroupMatrixMaxGroups; ++dst) {
            p.crosspointDb[groupMatrixIndex(src, dst)] = src == dst ? 0.0f : -80.0f;
        }
    }
    return p;
}

class GroupMatrix {
public:
    GroupMatrix()
    {
        params_ = makeDefaultGroupMatrixParams();
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

    void setParams(const GroupMatrixParams& params)
    {
        params_ = sanitize(params);
        rebuildTargets();
    }

    const GroupMatrixParams& params() const { return params_; }
    uint32_t activeGroups() const { return groupMatrixActiveGroups(params_.groupSize); }
    uint32_t groupChannels() const { return groupMatrixGroupChannels(params_.groupSize); }
    const std::array<float, kGroupMatrixMaxCrosspoints>& targetGain() const { return targetGain_; }
    std::array<float, kGroupMatrixMaxCrosspoints> generatedFlowPreview(float phase = 0.0f) const { return generatedFlowMatrix(phase); }
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

    template <typename Sample>
    void process(Sample* const* inputs, uint32_t inputChannels, Sample* const* outputs, uint32_t outputChannels, uint32_t frames)
    {
        if (!inputs || !outputs || frames == 0) {
            return;
        }

        const uint32_t channels = std::min<uint32_t>(kGroupMatrixChannels, outputChannels);
        for (uint32_t ch = 0; ch < channels; ++ch) {
            if (outputs[ch]) {
                std::fill(outputs[ch], outputs[ch] + frames, Sample { 0 });
            }
        }

        const float smoothing = smoothingCoeff();
        const uint32_t groups = activeGroups();
        const uint32_t width = groupChannels();
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

            for (uint32_t src = 0; src < groups; ++src) {
                for (uint32_t dst = 0; dst < groups; ++dst) {
                    const float gain = currentGain_[groupMatrixIndex(src, dst)];
                    if (gain <= 0.000001f) {
                        continue;
                    }
                    for (uint32_t lane = 0; lane < width; ++lane) {
                        const uint32_t inCh = src * width + lane;
                        const uint32_t outCh = dst * width + lane;
                        if (inCh < inputChannels && outCh < outputChannels && inputs[inCh] && outputs[outCh]) {
                            outputs[outCh][frame] += static_cast<Sample>(inputs[inCh][frame] * static_cast<Sample>(gain));
                        }
                    }
                }
            }
        }
    }

private:
    static GroupMatrixParams sanitize(GroupMatrixParams p)
    {
        for (float& db : p.crosspointDb) {
            db = groupMatrixClamp(db, -80.0f, 12.0f);
        }
        p.groupSize = static_cast<GroupMatrixSize>(
            std::min<uint32_t>(static_cast<uint32_t>(p.groupSize), static_cast<uint32_t>(GroupMatrixSize::Ch16)));
        p.flow = groupMatrixClamp(p.flow, 0.0f, 1.0f);
        p.spread = groupMatrixClamp(p.spread, 0.0f, 1.0f);
        p.vortex = groupMatrixClamp(p.vortex, -1.0f, 1.0f);
        p.motion = groupMatrixClamp(p.motion, 0.0f, 1.0f);
        p.shape = matrixFlowShapeFromIndex(static_cast<uint32_t>(p.shape));
        p.mode = static_cast<GroupMatrixFlowMode>(
            std::min<uint32_t>(static_cast<uint32_t>(p.mode), static_cast<uint32_t>(GroupMatrixFlowMode::Sync)));
        p.rate = groupMatrixClamp(p.rate, 0.0f, 1.0f);
        p.divisionBeats = groupMatrixClamp(p.divisionBeats, 0.25f, 64.0f);
        p.phaseOffset = groupMatrixClamp(p.phaseOffset, 0.0f, 1.0f);
        p.smoothingMs = groupMatrixClamp(p.smoothingMs, 1.0f, 500.0f);
        p.outputGainDb = groupMatrixClamp(p.outputGainDb, -60.0f, 12.0f);
        return p;
    }

    void rebuildTargets()
    {
        targetGain_.fill(0.0f);
        const float out = groupMatrixDbToGain(params_.outputGainDb);
        const float phase = wrapPhase((useExternalPhase_ ? externalPhase_ : freePhase_) + params_.phaseOffset);
        const auto generated = generatedFlowMatrix(phase);
        const uint32_t groups = activeGroups();
        for (uint32_t src = 0; src < groups; ++src) {
            for (uint32_t dst = 0; dst < groups; ++dst) {
                const uint32_t idx = groupMatrixIndex(src, dst);
                const float manual = groupMatrixDbToGain(params_.crosspointDb[idx]);
                const float flowShape = 1.0f - params_.motion + generated[idx] * params_.motion;
                targetGain_[idx] = manual * flowShape * out;
            }
        }
    }

    std::array<float, kGroupMatrixMaxCrosspoints> generatedFlowMatrix(float phase) const
    {
        std::array<float, kGroupMatrixMaxCrosspoints> g {};
        const uint32_t groups = activeGroups();
        const float phaseForShape = params_.shape == MatrixFlowShape::Hold ? 0.0f : phase;
        const bool swirlShape = params_.shape == MatrixFlowShape::Swirl;
        const float flowAmt = swirlShape ? std::max(0.24f, params_.flow) : params_.flow;
        const float width = 0.045f + params_.spread * 1.25f + flowAmt * 0.58f;
        const float angle = phaseForShape * 6.28318530718f;

        if (params_.shape == MatrixFlowShape::Pulse) {
            for (uint32_t src = 0; src < groups; ++src) {
                for (uint32_t dst = 0; dst < groups; ++dst) {
                    const float offset = (static_cast<float>(src + dst) / static_cast<float>(std::max<uint32_t>(1u, groups))) * params_.spread;
                    g[groupMatrixIndex(src, dst)] = matrixFlowPulse(phaseForShape + offset);
                }
            }
            return g;
        }

        if (params_.shape == MatrixFlowShape::Chase) {
            const float chaseWidth = 0.35f + params_.spread * 2.4f;
            const float pos = phaseForShape * static_cast<float>(groups);
            for (uint32_t src = 0; src < groups; ++src) {
                float sum = 0.0f;
                const float center = std::fmod(static_cast<float>(src) + pos, static_cast<float>(groups));
                for (uint32_t dst = 0; dst < groups; ++dst) {
                    const float w = matrixFlowRingWeight(groups, dst, center, chaseWidth);
                    g[groupMatrixIndex(src, dst)] = w;
                    sum += w;
                }
                if (sum > 0.000001f) {
                    for (uint32_t dst = 0; dst < groups; ++dst) {
                        g[groupMatrixIndex(src, dst)] /= sum;
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
            for (uint32_t src = 0; src < groups; ++src) {
                float sum = 0.0f;
                for (uint32_t dst = 0; dst < groups; ++dst) {
                    const float a = matrixFlowHash01(src, dst, seedA);
                    const float b = matrixFlowHash01(src, dst, seedB);
                    const float h = a + (b - a) * t;
                    const float w = std::max(0.0f, (h - threshold) / std::max(0.05f, 1.0f - threshold));
                    g[groupMatrixIndex(src, dst)] = 0.04f + w;
                    sum += g[groupMatrixIndex(src, dst)];
                }
                if (sum > 0.000001f) {
                    for (uint32_t dst = 0; dst < groups; ++dst) {
                        g[groupMatrixIndex(src, dst)] /= sum;
                    }
                }
            }
            return g;
        }

        const float vortex = params_.vortex + (swirlShape ? 1.35f : 0.0f);
        const float orbitX = std::cos(angle) * flowAmt * (swirlShape ? 1.12f : 1.0f);
        const float orbitY = std::sin(angle) * flowAmt * (swirlShape ? 1.12f : 1.0f);

        for (uint32_t src = 0; src < groups; ++src) {
            const float srcAngle = groups <= 1u ? 0.0f : (static_cast<float>(src) / static_cast<float>(groups)) * 6.28318530718f;
            float cx = std::cos(srcAngle);
            float cy = std::sin(srcAngle);
            const float swirlX = -cy * vortex * flowAmt;
            const float swirlY = cx * vortex * flowAmt;
            const float sourcePhase = angle + srcAngle;
            const float swirlAmp = swirlShape ? 0.54f : 0.20f;
            cx = cx * (1.0f - 0.32f * flowAmt) + swirlX + orbitX * 0.46f + std::cos(sourcePhase) * flowAmt * swirlAmp;
            cy = cy * (1.0f - 0.32f * flowAmt) + swirlY + orbitY * 0.46f + std::sin(sourcePhase) * flowAmt * swirlAmp;

            float sum = 0.0f;
            for (uint32_t dst = 0; dst < groups; ++dst) {
                const float dstAngle = groups <= 1u ? 0.0f : (static_cast<float>(dst) / static_cast<float>(groups)) * 6.28318530718f;
                const float dx = std::cos(dstAngle) - cx;
                const float dy = std::sin(dstAngle) - cy;
                const float d2 = dx * dx + dy * dy;
                const float w = std::exp(-d2 / std::max(0.001f, width * width));
                g[groupMatrixIndex(src, dst)] = w;
                sum += w;
            }
            if (sum > 0.000001f) {
                for (uint32_t dst = 0; dst < groups; ++dst) {
                    g[groupMatrixIndex(src, dst)] /= sum;
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
    GroupMatrixParams params_ {};
    std::array<float, kGroupMatrixMaxCrosspoints> currentGain_ {};
    std::array<float, kGroupMatrixMaxCrosspoints> targetGain_ {};
    float freePhase_ = 0.0f;
    float externalPhase_ = 0.0f;
    bool useExternalPhase_ = false;
};

} // namespace s3g
