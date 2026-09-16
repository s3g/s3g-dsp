#pragma once

#include "s3g_math.h"
#include <cmath>

namespace s3g {

// Render-span constants. Recomputed inside process(), under the same
// floating-point mode as the voices, so parameter/sample-rate changes need no
// persistent cache invalidation and sample-accurate CLAP spans stay independent.
struct PyrosphereCoefficients {
    float infra, sub, slow, mid, air;
    float fracture, spall, debris, fragment, pressure, force, mass;

    template<class Params, class Material>
    PyrosphereCoefficients(const Params& p, const Material& material, float sr)
    {
        const float dt = 1.0f / sr;
        const float infraHz = 2.4f + (1.0f - p.body) * 5.6f
            + (1.0f - material.collapse) * 2.0f;
        const float subHz = 14.0f + (1.0f - p.body) * 52.0f
            + material.collapse * 12.0f;
        const float slowHz = 48.0f + (1.0f - p.body) * 140.0f
            + material.collapse * 42.0f;
        const float midHz = 260.0f + p.sweep * 2100.0f
            + material.highColor * 680.0f;
        const float airHz = 2200.0f + p.shrill * 10800.0f + p.air * 2400.0f;
        infra = 1.0f - std::exp(-kPi * 2.0f * infraHz / sr);
        sub = 1.0f - std::exp(-kPi * 2.0f * subHz / sr);
        slow = 1.0f - std::exp(-kPi * 2.0f * slowHz / sr);
        mid = 1.0f - std::exp(-kPi * 2.0f * midHz / sr);
        air = 1.0f - std::exp(-kPi * 2.0f * airHz / sr);
        const float fractureTime = 0.0012f + p.q * 0.009f + material.damping * 0.004f;
        const float spallTime = 0.014f + p.body * 0.11f + material.spall * 0.08f;
        const float debrisTime = 0.06f + p.body * 0.56f + material.collapse * 0.42f;
        fracture = std::exp(-dt / fractureTime);
        spall = std::exp(-dt / spallTime);
        debris = std::exp(-dt / debrisTime);
        fragment = std::exp(-dt / (0.0014f + p.particles * 0.008f));
        pressure = std::exp(-dt / (0.055f + p.pressure * 0.46f));
        force = std::exp(-dt / (0.0011f + material.damping * 0.006f));
        mass = std::exp(-dt / (0.10f + p.body * 0.34f + material.collapse * 0.18f));
    }
};

} // namespace s3g
