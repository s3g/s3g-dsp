#pragma once

#include "s3g_math.h"
#include "s3g_realtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace s3g {

// Continuous articulator coordinates. These are anatomical control values,
// not recorded area-function or acoustic data.
struct ArticulatoryGesture {
    float tonguePosition = 0.48f;     // back 0 .. front 1
    float tongueConstriction = 0.25f; // open 0 .. constricted 1
    float jawOpen = 0.50f;
    float lipRound = 0.05f;
    float oralClosure = 0.0f;
    float closurePosition = 0.78f;    // glottis 0 .. lips 1
    float velumOpen = 0.05f;
    float tractScale = 1.0f;
    float coarticulation = 0.68f;
};

struct ArticulatoryTractFrame {
    float oral = 0.0f;
    float nasal = 0.0f;
};

inline float articulatoryFiniteOr(float value, float fallback)
{
    return std::isfinite(value) ? value : fallback;
}

inline ArticulatoryGesture sanitizeArticulatoryGesture(
    ArticulatoryGesture gesture)
{
    gesture.tonguePosition = clamp(articulatoryFiniteOr(
        gesture.tonguePosition, 0.48f), 0.0f, 1.0f);
    gesture.tongueConstriction = clamp(articulatoryFiniteOr(
        gesture.tongueConstriction, 0.25f), 0.0f, 1.0f);
    gesture.jawOpen = clamp(articulatoryFiniteOr(
        gesture.jawOpen, 0.50f), 0.0f, 1.0f);
    gesture.lipRound = clamp(articulatoryFiniteOr(
        gesture.lipRound, 0.05f), 0.0f, 1.0f);
    gesture.oralClosure = clamp(articulatoryFiniteOr(
        gesture.oralClosure, 0.0f), 0.0f, 1.0f);
    gesture.closurePosition = clamp(articulatoryFiniteOr(
        gesture.closurePosition, 0.78f), 0.05f, 0.99f);
    gesture.velumOpen = clamp(articulatoryFiniteOr(
        gesture.velumOpen, 0.05f), 0.0f, 1.0f);
    gesture.tractScale = clamp(articulatoryFiniteOr(
        gesture.tractScale, 1.0f), 0.70f, 1.35f);
    gesture.coarticulation = clamp(articulatoryFiniteOr(
        gesture.coarticulation, 0.68f), 0.0f, 1.0f);
    return gesture;
}

// Kelly-Lochbaum-style bidirectional tube model. Section count follows sample
// rate up to 96 kHz, preserving approximate tract length without allocation.
// A smaller nasal tube is excited from a delayed pressure tap at the velum.
class ArticulatoryWaveguide {
public:
    void prepare(double sampleRate)
    {
        sampleRate_ = std::isfinite(sampleRate)
            ? clamp(static_cast<float>(sampleRate), 8000.0f, 192000.0f)
            : 48000.0f;
        const float modelRate = std::min(sampleRate_, 96000.0f);
        oralSections_ = std::max<uint32_t>(6u,
            std::min<uint32_t>(kMaxOralSections,
                static_cast<uint32_t>(std::lround(
                    24.0f * modelRate / 48000.0f))));
        nasalSections_ = std::max<uint32_t>(4u,
            std::min<uint32_t>(kMaxNasalSections,
                static_cast<uint32_t>(std::lround(
                    12.0f * modelRate / 48000.0f))));
        inputCoefficient_ = 1.0f - std::exp(
            -2.0f * kPi * std::min(9000.0f, sampleRate_ * 0.38f)
                / sampleRate_);
        reset();
    }

    void reset()
    {
        oralRight_.fill(0.0f);
        oralLeft_.fill(0.0f);
        oralNextRight_.fill(0.0f);
        oralNextLeft_.fill(0.0f);
        nasalRight_.fill(0.0f);
        nasalLeft_.fill(0.0f);
        nasalNextRight_.fill(0.0f);
        nasalNextLeft_.fill(0.0f);
        oralReflection_.fill(0.0f);
        nasalReflection_.fill(0.0f);
        currentOralArea_.fill(1.0f);
        targetOralArea_.fill(1.0f);
        currentNasalArea_.fill(0.72f);
        targetNasalArea_.fill(0.72f);
        mouthInput_ = 0.0f;
        mouthRadiation_ = 0.0f;
        noseInput_ = 0.0f;
        noseRadiation_ = 0.0f;
        inputLowpass_ = 0.0f;
        velumPressure_ = 0.0f;
        controlCounter_ = 0u;
        gesture_ = sanitizeArticulatoryGesture(gesture_);
        buildTargetAreas();
        currentOralArea_ = targetOralArea_;
        currentNasalArea_ = targetNasalArea_;
        updateReflections();
    }

    void setGesture(ArticulatoryGesture gesture)
    {
        gesture_ = sanitizeArticulatoryGesture(gesture);
        buildTargetAreas();
    }

    const ArticulatoryGesture& gesture() const { return gesture_; }

    ArticulatoryTractFrame processFrame(float excitation)
    {
        excitation = std::isfinite(excitation) ? excitation : 0.0f;
        if ((controlCounter_++ & 7u) == 0u) updateGeometry();
        inputLowpass_ += (excitation - inputLowpass_) * inputCoefficient_;

        const uint32_t branch = std::min<uint32_t>(oralSections_ - 1u,
            std::max<uint32_t>(1u,
                static_cast<uint32_t>(0.38f * oralSections_)));
        velumPressure_ += ((oralRight_[branch] + oralLeft_[branch])
            - velumPressure_) * 0.22f;
        const float velum = gesture_.velumOpen;
        const float nasalInput = velumPressure_ * velum * 0.32f;
        const float oralLoss = 1.0f - velum * 0.012f;

        const float oral = propagate(oralRight_, oralLeft_, oralNextRight_,
            oralNextLeft_, oralReflection_, oralSections_,
            inputLowpass_ * 0.24f, -0.82f, 0.9965f * oralLoss,
            mouthInput_, mouthRadiation_);
        const float nasal = propagate(nasalRight_, nasalLeft_, nasalNextRight_,
            nasalNextLeft_, nasalReflection_, nasalSections_,
            nasalInput, -0.76f, 0.9945f, noseInput_, noseRadiation_);

        ArticulatoryTractFrame frame {
            std::tanh(oral * 2.8f) * 0.46f,
            std::tanh(nasal * 3.2f) * 0.38f * velum,
        };
        if (!std::isfinite(frame.oral) || !std::isfinite(frame.nasal)) {
            reset();
            return {};
        }
        return frame;
    }

private:
    static constexpr uint32_t kMaxOralSections = 48u;
    static constexpr uint32_t kMaxNasalSections = 24u;

    static float gaussian(float value, float center, float width)
    {
        const float offset = (value - center) / std::max(0.02f, width);
        return std::exp(-offset * offset);
    }

    void buildTargetAreas()
    {
        const float tongueCenter = 0.30f
            + gesture_.tonguePosition * 0.53f;
        const float tongueWidth = 0.22f
            - gesture_.tongueConstriction * 0.10f;
        for (uint32_t index = 0u; index < oralSections_; ++index) {
            const float x = oralSections_ > 1u
                ? static_cast<float>(index)
                    / static_cast<float>(oralSections_ - 1u)
                : 0.0f;
            float area = 0.78f + x * 1.40f
                + gesture_.jawOpen * (0.30f + x * 1.25f);
            area += (1.0f - gesture_.tonguePosition) * 0.42f
                * gaussian(x, 0.72f, 0.25f);
            area -= gesture_.tongueConstriction
                * (0.78f + gesture_.jawOpen * 0.24f)
                * gaussian(x, tongueCenter, tongueWidth);
            const float lipRegion = clamp((x - 0.78f) / 0.22f,
                0.0f, 1.0f);
            area *= 1.0f - gesture_.lipRound * lipRegion * 0.76f;
            area += gesture_.jawOpen * lipRegion * 0.58f;
            area *= 1.0f - gesture_.oralClosure * 0.985f
                * gaussian(x, gesture_.closurePosition, 0.055f);
            // A longer tract is slightly narrower toward the lips and wider
            // in the pharynx, complementing the section-count length model.
            const float scaleWarp = gesture_.tractScale - 1.0f;
            area *= 1.0f + scaleWarp * (0.38f - x * 0.58f);
            targetOralArea_[index] = clamp(area, 0.025f, 6.5f);
        }

        for (uint32_t index = 0u; index < nasalSections_; ++index) {
            const float x = nasalSections_ > 1u
                ? static_cast<float>(index)
                    / static_cast<float>(nasalSections_ - 1u)
                : 0.0f;
            float area = 0.34f + x * 0.88f
                + 0.24f * gaussian(x, 0.46f, 0.22f);
            area *= 1.0f - 0.28f * gaussian(x, 0.78f, 0.10f);
            targetNasalArea_[index] = clamp(area, 0.08f, 2.0f);
        }
    }

    void updateGeometry()
    {
        const float milliseconds = lerp(48.0f, 7.0f,
            gesture_.coarticulation);
        const float coefficient = 1.0f - std::exp(-8.0f
            / std::max(1.0f, milliseconds * 0.001f * sampleRate_));
        for (uint32_t index = 0u; index < oralSections_; ++index) {
            currentOralArea_[index] += (targetOralArea_[index]
                - currentOralArea_[index]) * coefficient;
        }
        for (uint32_t index = 0u; index < nasalSections_; ++index) {
            currentNasalArea_[index] += (targetNasalArea_[index]
                - currentNasalArea_[index]) * coefficient;
        }
        updateReflections();
    }

    void updateReflections()
    {
        for (uint32_t index = 0u; index + 1u < oralSections_; ++index) {
            const float left = currentOralArea_[index];
            const float right = currentOralArea_[index + 1u];
            oralReflection_[index] = clamp((left - right)
                / std::max(0.001f, left + right), -0.985f, 0.985f);
        }
        for (uint32_t index = 0u; index + 1u < nasalSections_; ++index) {
            const float left = currentNasalArea_[index];
            const float right = currentNasalArea_[index + 1u];
            nasalReflection_[index] = clamp((left - right)
                / std::max(0.001f, left + right), -0.96f, 0.96f);
        }
    }

    template <size_t Size>
    static float propagate(std::array<float, Size>& right,
        std::array<float, Size>& left,
        std::array<float, Size>& nextRight,
        std::array<float, Size>& nextLeft,
        const std::array<float, Size>& reflection, uint32_t sections,
        float source, float lipReflection, float damping,
        float& previousMouthInput, float& radiationState)
    {
        const float lipInput = right[sections - 1u];
        nextRight[0u] = (source + left[0u] * 0.68f) * damping;
        nextLeft[sections - 1u] = lipInput * lipReflection * damping;
        for (uint32_t index = 0u; index + 1u < sections; ++index) {
            const float incomingRight = right[index];
            const float incomingLeft = left[index + 1u];
            const float coefficient = reflection[index];
            nextLeft[index] = (coefficient * incomingRight
                + (1.0f - coefficient) * incomingLeft) * damping;
            nextRight[index + 1u] = ((1.0f + coefficient) * incomingRight
                - coefficient * incomingLeft) * damping;
        }
        for (uint32_t index = 0u; index < sections; ++index) {
            right[index] = flushDenormal(nextRight[index]);
            left[index] = flushDenormal(nextLeft[index]);
        }
        const float radiation = lipInput - previousMouthInput
            + 0.972f * radiationState;
        previousMouthInput = lipInput;
        radiationState = flushDenormal(radiation);
        return radiationState;
    }

    float sampleRate_ = 48000.0f;
    uint32_t oralSections_ = 24u;
    uint32_t nasalSections_ = 12u;
    ArticulatoryGesture gesture_ {};
    std::array<float, kMaxOralSections> oralRight_ {};
    std::array<float, kMaxOralSections> oralLeft_ {};
    std::array<float, kMaxOralSections> oralNextRight_ {};
    std::array<float, kMaxOralSections> oralNextLeft_ {};
    std::array<float, kMaxOralSections> oralReflection_ {};
    std::array<float, kMaxOralSections> currentOralArea_ {};
    std::array<float, kMaxOralSections> targetOralArea_ {};
    std::array<float, kMaxNasalSections> nasalRight_ {};
    std::array<float, kMaxNasalSections> nasalLeft_ {};
    std::array<float, kMaxNasalSections> nasalNextRight_ {};
    std::array<float, kMaxNasalSections> nasalNextLeft_ {};
    std::array<float, kMaxNasalSections> nasalReflection_ {};
    std::array<float, kMaxNasalSections> currentNasalArea_ {};
    std::array<float, kMaxNasalSections> targetNasalArea_ {};
    float inputCoefficient_ = 0.5f;
    float inputLowpass_ = 0.0f;
    float velumPressure_ = 0.0f;
    float mouthInput_ = 0.0f;
    float mouthRadiation_ = 0.0f;
    float noseInput_ = 0.0f;
    float noseRadiation_ = 0.0f;
    uint32_t controlCounter_ = 0u;
};

} // namespace s3g
