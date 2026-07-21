#pragma once

#include "s3g_ambi_vot_encoder.h"

namespace s3g {

constexpr uint32_t kAmbiVoxMaxVoices = 16u;

enum class AmbiVoxOrchestration : uint32_t {
    Individual = 0,
    Unison = 1,
    Chorale = 2,
    Chorus = 3,
    Round = 4,
    Cluster = 5,
};

inline const char* ambiVoxOrchestrationName(AmbiVoxOrchestration orchestration)
{
    switch (orchestration) {
    case AmbiVoxOrchestration::Individual: return "INDIVIDUAL";
    case AmbiVoxOrchestration::Unison: return "UNISON";
    case AmbiVoxOrchestration::Chorale: return "CHORALE";
    case AmbiVoxOrchestration::Chorus: return "CHORUS";
    case AmbiVoxOrchestration::Round: return "ROUND";
    case AmbiVoxOrchestration::Cluster: return "CLUSTER";
    }
    return "INDIVIDUAL";
}

enum class AmbiVoxContourMode : uint32_t {
    Original = 0,
    Reduced = 1,
    Flat = 2,
    Quantized = 3,
};

inline const char* ambiVoxContourModeName(AmbiVoxContourMode mode)
{
    switch (mode) {
    case AmbiVoxContourMode::Original: return "ORIGINAL";
    case AmbiVoxContourMode::Reduced: return "REDUCED";
    case AmbiVoxContourMode::Flat: return "FLAT";
    case AmbiVoxContourMode::Quantized: return "QUANTIZED";
    }
    return "ORIGINAL";
}

inline float ambiVoxFitNoteToRange(float note, float minimum, float maximum)
{
    while (note < minimum) note += 12.0f;
    while (note > maximum) note -= 12.0f;
    return clamp(note, minimum, maximum);
}

inline float ambiVoxOrchestratedFreeNote(AmbiVoxOrchestration orchestration,
                                         float root,
                                         AmbiVotScale scale,
                                         uint32_t voice)
{
    switch (orchestration) {
    case AmbiVoxOrchestration::Unison:
        return root;
    case AmbiVoxOrchestration::Chorale: {
        // SATB tessitura is more useful than allowing every section to roam
        // across its complete theoretical range.
        constexpr int degrees[] { 7, 4, 2, -7 };
        constexpr float low[] { 60.0f, 53.0f, 48.0f, 40.0f };
        constexpr float high[] { 84.0f, 77.0f, 72.0f, 64.0f };
        const uint32_t part = voice % 4u;
        const float note = root + static_cast<float>(ambiVotScaleDegreeSemitones(scale, degrees[part]));
        return ambiVoxFitNoteToRange(note, low[part], high[part]);
    }
    case AmbiVoxOrchestration::Chorus: {
        constexpr float offsets[] { 0.0f, 0.0f, 12.0f, -12.0f };
        return ambiVoxFitNoteToRange(root + offsets[voice % 4u], 36.0f, 88.0f);
    }
    case AmbiVoxOrchestration::Round: {
        constexpr float offsets[] { 0.0f, 12.0f, -12.0f, 0.0f };
        return ambiVoxFitNoteToRange(root + offsets[voice % 4u], 36.0f, 88.0f);
    }
    case AmbiVoxOrchestration::Cluster: {
        if (voice == 0u) return root;
        const float distance = static_cast<float>((voice + 1u) / 2u);
        return root + ((voice & 1u) != 0u ? distance : -distance);
    }
    case AmbiVoxOrchestration::Individual:
    default:
        return root;
    }
}

inline float ambiVoxOrchestrationDetuneCents(AmbiVoxOrchestration orchestration,
                                              uint32_t voice)
{
    if (orchestration == AmbiVoxOrchestration::Individual) return 0.0f;
    uint32_t hash = (voice + 1u) * 0x9e3779b9u;
    hash ^= hash >> 16u;
    hash *= 0x7feb352du;
    hash ^= hash >> 15u;
    const float bipolar = static_cast<float>(hash & 0xffffu) / 32767.5f - 1.0f;
    float depth = 2.5f;
    if (orchestration == AmbiVoxOrchestration::Chorale) depth = 4.0f;
    else if (orchestration == AmbiVoxOrchestration::Chorus) depth = 10.0f;
    else if (orchestration == AmbiVoxOrchestration::Round) depth = 3.0f;
    else if (orchestration == AmbiVoxOrchestration::Cluster) depth = 2.0f;
    return bipolar * depth;
}

inline float ambiVoxOrchestrationDelayMs(AmbiVoxOrchestration orchestration,
                                          uint32_t voice)
{
    if (voice == 0u) return 0.0f;
    const float position = static_cast<float>((voice * 37u) % 101u) / 100.0f;
    if (orchestration == AmbiVoxOrchestration::Unison) return position * 8.0f;
    if (orchestration == AmbiVoxOrchestration::Chorale) return position * 18.0f;
    if (orchestration == AmbiVoxOrchestration::Chorus) return position * 32.0f;
    return 0.0f;
}

inline uint32_t ambiVoxPhraseGroupCount(AmbiVoxOrchestration orchestration,
                                        uint32_t voices)
{
    const uint32_t activeVoices = std::clamp<uint32_t>(voices, 1u, kAmbiVoxMaxVoices);
    return orchestration == AmbiVoxOrchestration::Round
        ? std::min<uint32_t>(activeVoices, 4u)
        : activeVoices;
}

inline float ambiVoxPhraseSpreadPhase(AmbiVoxOrchestration orchestration,
                                      uint32_t voice,
                                      uint32_t voices,
                                      float spread)
{
    const uint32_t groups = ambiVoxPhraseGroupCount(orchestration, voices);
    const uint32_t group = voice % groups;
    return clamp(spread, 0.0f, 1.0f)
        * static_cast<float>(group) / static_cast<float>(groups);
}

inline uint32_t ambiVoxPhraseSpreadIndex(AmbiVoxOrchestration orchestration,
                                         uint32_t voice,
                                         uint32_t voices,
                                         uint32_t phraseLength,
                                         float spread)
{
    if (phraseLength < 2u || spread <= 0.0f) return 0u;
    const float eventPosition = static_cast<float>(phraseLength)
        * ambiVoxPhraseSpreadPhase(orchestration, voice, voices, spread);
    return std::min<uint32_t>(phraseLength - 1u,
        static_cast<uint32_t>(std::lround(eventPosition)));
}

inline uint32_t ambiVoxRoundPhraseIndex(AmbiVoxOrchestration orchestration,
                                        uint32_t voice,
                                        uint32_t voices,
                                        uint32_t phraseLength)
{
    if (orchestration != AmbiVoxOrchestration::Round) return 0u;
    return ambiVoxPhraseSpreadIndex(orchestration, voice, voices, phraseLength, 1.0f);
}

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
