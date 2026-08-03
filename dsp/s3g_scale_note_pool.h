#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

namespace s3g {

enum class ScaleRule : uint32_t {
    Major = 0u,
    NaturalMinor = 1u,
    Dorian = 2u,
    Mixolydian = 3u,
    MajorPentatonic = 4u,
    MinorPentatonic = 5u,
    HarmonicMinor = 6u,
    Chromatic = 7u,
};

struct ScaleRuleDefinition {
    std::array<int32_t, 12u> semitones {};
    uint32_t count = 0u;
};

inline constexpr std::array<ScaleRuleDefinition, 8u>
    kScaleRuleDefinitions {{
        { {{ 0, 2, 4, 5, 7, 9, 11 }}, 7u },
        { {{ 0, 2, 3, 5, 7, 8, 10 }}, 7u },
        { {{ 0, 2, 3, 5, 7, 9, 10 }}, 7u },
        { {{ 0, 2, 4, 5, 7, 9, 10 }}, 7u },
        { {{ 0, 2, 4, 7, 9 }}, 5u },
        { {{ 0, 3, 5, 7, 10 }}, 5u },
        { {{ 0, 2, 3, 5, 7, 8, 11 }}, 7u },
        { {{ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 }}, 12u },
    }};

inline int32_t scaleRuleSemitoneOffset(ScaleRule rule, int32_t degree)
{
    const auto& definition = kScaleRuleDefinitions[
        std::min<uint32_t>(static_cast<uint32_t>(rule), 7u)];
    const int32_t count = static_cast<int32_t>(definition.count);
    if (degree >= 0) {
        return 12 * (degree / count)
            + definition.semitones[static_cast<uint32_t>(degree % count)];
    }
    const int32_t octaves = (-degree + count - 1) / count;
    const int32_t wrappedDegree = degree + octaves * count;
    return definition.semitones[static_cast<uint32_t>(wrappedDegree)]
        - 12 * octaves;
}

inline int32_t dispersedScaleMidiNote(int32_t rootNote,
    uint32_t node, uint32_t noteCount, ScaleRule rule)
{
    noteCount = std::clamp<uint32_t>(noteCount, 1u, 8u);
    const uint32_t slot = node % noteCount;
    const int32_t degree = slot == 0u ? 0
        : ((slot & 1u) != 0u
            ? static_cast<int32_t>((slot + 1u) / 2u)
            : -static_cast<int32_t>(slot / 2u));
    return std::clamp(
        rootNote + scaleRuleSemitoneOffset(rule, degree), 0, 127);
}

} // namespace s3g
