#pragma once

#include <algorithm>
#include <cmath>

namespace s3g {

struct TurbulentFlameJetOutput {
    float sample = 0.0f;
    float activity = 0.0f;
};

// A continuous, pressurized non-premixed flame source. The correlated low
// bands represent the plenum/jet core, the decorrelated bands represent the
// shear layer and nozzle hiss, and the lagged source follows unsteady heat
// release downstream. This is intentionally separate from the event-driven
// material combustion path used by the other Pyrosphere materials.
class TurbulentFlameJetModel {
public:
    void prepare(double sampleRate)
    {
        sampleRate_ = std::max(1000.0, sampleRate);
        reset();
    }

    void reset()
    {
        sub_ = 0.0f;
        plenum_ = 0.0f;
        core_ = 0.0f;
        shear_ = 0.0f;
        heatRelease_ = 0.0f;
        previousHeatRelease_ = 0.0f;
        intermittency_ = 0.72f;
    }

    TurbulentFlameJetOutput process(float white, float pressure,
        float velocity, float turbulence, float oxygen, float hiss,
        float body, float brightness)
    {
        pressure = std::clamp(pressure, 0.0f, 1.0f);
        velocity = std::clamp(velocity, 0.0f, 1.0f);
        turbulence = std::clamp(turbulence, 0.0f, 1.0f);
        oxygen = std::clamp(oxygen, 0.0f, 1.0f);
        hiss = std::clamp(hiss, 0.0f, 1.0f);
        body = std::clamp(body, 0.0f, 1.0f);
        brightness = std::clamp(brightness, 0.0f, 1.0f);

        const float sr = static_cast<float>(sampleRate_);
        const auto coefficient = [sr](float hz) {
            constexpr float kTwoPi = 6.28318530717958647692f;
            return 1.0f - std::exp(-kTwoPi * hz / sr);
        };
        sub_ += (white - sub_)
            * coefficient(14.0f + (1.0f - body) * 42.0f
                + (1.0f - pressure) * 30.0f);
        plenum_ += (white - plenum_)
            * coefficient(42.0f + (1.0f - body) * 75.0f
                + (1.0f - pressure) * 35.0f);
        core_ += (white - core_)
            * coefficient(110.0f + (1.0f - body) * 140.0f
                + velocity * 310.0f + pressure * 170.0f);
        shear_ += (white - shear_)
            * coefficient(1250.0f + pressure * 2700.0f
                + brightness * 1900.0f);

        const float coreBand = core_ - plenum_;
        const float shearBand = shear_ - core_;
        const float nozzleBand = white - shear_;
        const float coherentTarget = std::clamp(0.62f
            + std::fabs(plenum_) * (0.78f + turbulence * 1.24f),
            0.56f, 1.34f);
        intermittency_ += (coherentTarget - intermittency_)
            * coefficient(5.0f + turbulence * 13.0f);

        const float heatTarget = std::tanh((plenum_ * 2.2f
                + coreBand * (0.72f + turbulence * 1.18f))
            * (0.78f + oxygen * 1.22f));
        heatRelease_ += (heatTarget - heatRelease_)
            * coefficient(34.0f + velocity * 76.0f);
        const float heatChange = (heatRelease_ - previousHeatRelease_)
            * (18.0f + velocity * 38.0f);
        previousHeatRelease_ = heatRelease_;

        const float drive = (0.18f + pressure * 0.82f)
            * (0.30f + velocity * 0.70f)
            * (0.34f + oxygen * 0.66f);
        const float jetCore = std::tanh(sub_
                * (2.4f + body * 4.6f + pressure * 1.2f)
            + plenum_ * (1.1f + body * 2.2f)
            + coreBand * (0.72f + turbulence * 1.58f));
        const float shearLayer = shearBand
            * (0.46f + turbulence * 1.44f)
            * (0.74f + std::fabs(coreBand) * 1.8f);
        const float nozzle = nozzleBand * hiss
            * pressure * pressure * (0.34f + brightness * 0.76f);
        const float combustion = std::tanh(heatRelease_ * 1.3f
                + heatChange)
            * (0.22f + oxygen * 0.58f)
            * (0.58f + turbulence * 0.42f);
        const float sample = (jetCore * (0.48f + body * 0.28f)
                + shearLayer * 0.42f + nozzle * 0.32f
                + combustion * 0.36f)
            * drive * intermittency_ * 0.54f;
        return { std::isfinite(sample) ? sample : 0.0f,
            std::clamp(std::fabs(sample) * 1.8f, 0.0f, 1.0f) };
    }

private:
    double sampleRate_ = 48000.0;
    float sub_ = 0.0f;
    float plenum_ = 0.0f;
    float core_ = 0.0f;
    float shear_ = 0.0f;
    float heatRelease_ = 0.0f;
    float previousHeatRelease_ = 0.0f;
    float intermittency_ = 0.72f;
};

} // namespace s3g
