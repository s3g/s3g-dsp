#pragma once

#include "s3g_ambi_vot_encoder.h"

namespace s3g {

enum class AmbiVoxPreset : uint32_t {
    BlackMetal = 0,
    Throat = 1,
    Choir = 2,
    Animal = 3,
};

inline const char* ambiVoxPresetName(AmbiVoxPreset preset)
{
    switch (preset) {
    case AmbiVoxPreset::BlackMetal: return "Black Metal";
    case AmbiVoxPreset::Throat: return "Throat";
    case AmbiVoxPreset::Choir: return "Choir";
    case AmbiVoxPreset::Animal: return "Animal";
    default: return "Black Metal";
    }
}

inline float ambiVoxPulse(float phase, float width)
{
    phase = ambiVotFract(phase);
    width = clamp(width, 0.04f, 0.96f);
    return phase < width ? 1.0f : -1.0f;
}

inline float ambiVoxNoiseHash(uint32_t seed)
{
    seed ^= seed >> 16;
    seed *= 0x7feb352dU;
    seed ^= seed >> 15;
    seed *= 0x846ca68bU;
    seed ^= seed >> 16;
    return static_cast<float>(seed & 0x00ffffffU) / static_cast<float>(0x00800000U) - 1.0f;
}

inline float ambiVoxTable(AmbiVoxPreset preset, float u, float v, float phase)
{
    constexpr float tau = 2.0f * kPi;
    phase = ambiVotFract(phase);
    const float sine = std::sin(tau * phase);
    const float closure = clamp(u, 0.0f, 1.0f);
    const float darkness = clamp(v, 0.0f, 1.0f);
    float sample = sine;

    switch (preset) {
    case AmbiVoxPreset::Choir: {
        const float f1 = 2.0f + 5.0f * darkness;
        const float f2 = 5.0f + 10.0f * (1.0f - darkness);
        sample = 0.62f * sine
            + 0.22f * std::sin(tau * phase * f1 + closure * 0.4f)
            + 0.14f * std::sin(tau * phase * f2 + darkness * 1.3f);
        sample = lerp(sample, ambiVotTriangle(phase), 0.20f * closure);
        break;
    }
    case AmbiVoxPreset::Throat: {
        const float pulse = ambiVoxPulse(phase, 0.28f + 0.32f * closure);
        const float sub = std::sin(tau * phase * 0.5f + darkness * 1.7f);
        sample = 0.42f * sine + 0.28f * pulse + 0.26f * sub
            + 0.18f * std::sin(tau * phase * (3.0f + 5.0f * darkness));
        sample = softSat(sample * (1.0f + 2.2f * closure));
        break;
    }
    case AmbiVoxPreset::Animal: {
        const float fold = std::sin(tau * (phase + 0.13f * std::sin(tau * phase * (2.0f + 6.0f * closure))));
        const float growl = std::sin(tau * phase * (1.0f + 0.5f * darkness))
            * ambiVoxPulse(phase * (2.0f + 3.0f * closure), 0.34f);
        sample = softSat((0.48f * fold + 0.44f * growl + 0.16f * sine) * (1.2f + 1.8f * closure));
        break;
    }
    case AmbiVoxPreset::BlackMetal:
    default: {
        const float pulse = ambiVoxPulse(phase, 0.18f + 0.34f * closure);
        const float scrape = ambiVoxNoiseHash(static_cast<uint32_t>(phase * 8192.0f)
            + static_cast<uint32_t>(closure * 101.0f)
            + static_cast<uint32_t>(darkness * 509.0f));
        const float throat = std::sin(tau * phase * (2.0f + 5.0f * darkness)
            + 0.55f * std::sin(tau * phase * (3.0f + 9.0f * closure)));
        const float shriek = std::sin(tau * phase * (9.0f + 22.0f * closure) + darkness * 2.1f);
        const float body = 0.36f * sine + 0.28f * pulse + 0.22f * throat;
        sample = body + (0.10f + 0.22f * closure) * shriek + (0.06f + 0.18f * darkness) * scrape;
        sample = softSat(sample * (1.35f + 3.25f * closure));
        break;
    }
    }
    return sample;
}

inline AmbiVotTableBank ambiVoxPresetBank(AmbiVoxPreset preset)
{
    AmbiVotTableBank bank {};
    for (uint32_t row = 0; row < kAmbiVotGridSize; ++row) {
        const float v = static_cast<float>(row) / static_cast<float>(kAmbiVotGridSize - 1u);
        for (uint32_t column = 0; column < kAmbiVotGridSize; ++column) {
            const float u = static_cast<float>(column) / static_cast<float>(kAmbiVotGridSize - 1u);
            const uint32_t table = row * kAmbiVotGridSize + column;
            for (uint32_t i = 0; i < kAmbiVotTableSize; ++i) {
                const float phase = static_cast<float>(i) / static_cast<float>(kAmbiVotTableSize);
                bank.tables[table][i] = ambiVoxTable(preset, u, v, phase);
            }
        }
    }
    ambiVotNormalizeBank(bank);
    ambiVotBuildBandTables(bank);
    return bank;
}

} // namespace s3g
