#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>

namespace s3g {

struct MusicalScaleDefinition {
    const char* name;
    std::array<int8_t, 12> semitones;
    uint8_t size;
};

// Stable scale IDs. New scales append here; menu presentation uses the
// independent kMusicalScaleMenuOrder permutation below.
inline constexpr uint32_t kMusicalScaleCount = 101u;
inline constexpr std::array<MusicalScaleDefinition, kMusicalScaleCount>
    kMusicalScales {{
    { "CHROMATIC", {{ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 }}, 12 },
    { "MAJOR", {{ 0, 2, 4, 5, 7, 9, 11 }}, 7 },
    { "MINOR", {{ 0, 2, 3, 5, 7, 8, 10 }}, 7 },
    { "PENTATONIC MAJOR", {{ 0, 2, 4, 7, 9 }}, 5 },
    { "WHOLE TONE", {{ 0, 2, 4, 6, 8, 10 }}, 6 },
    { "HARMONIC MINOR", {{ 0, 2, 3, 5, 7, 8, 11 }}, 7 },
    { "DORIAN", {{ 0, 2, 3, 5, 7, 9, 10 }}, 7 },
    { "PHRYGIAN", {{ 0, 1, 3, 5, 7, 8, 10 }}, 7 },
    { "LYDIAN", {{ 0, 2, 4, 6, 7, 9, 11 }}, 7 },
    { "MIXOLYDIAN", {{ 0, 2, 4, 5, 7, 9, 10 }}, 7 },
    { "LOCRIAN", {{ 0, 1, 3, 5, 6, 8, 10 }}, 7 },
    { "MELODIC MINOR", {{ 0, 2, 3, 5, 7, 9, 11 }}, 7 },
    { "HARMONIC MAJOR", {{ 0, 2, 4, 5, 7, 8, 11 }}, 7 },
    { "DORIAN B2", {{ 0, 1, 3, 5, 7, 9, 10 }}, 7 },
    { "LYDIAN AUGMENTED", {{ 0, 2, 4, 6, 8, 9, 11 }}, 7 },
    { "LYDIAN DOMINANT", {{ 0, 2, 4, 6, 7, 9, 10 }}, 7 },
    { "MIXOLYDIAN B6", {{ 0, 2, 4, 5, 7, 8, 10 }}, 7 },
    { "LOCRIAN #2", {{ 0, 2, 3, 5, 6, 8, 10 }}, 7 },
    { "ALTERED", {{ 0, 1, 3, 4, 6, 8, 10 }}, 7 },
    { "LOCRIAN NATURAL 6", {{ 0, 1, 3, 5, 6, 9, 10 }}, 7 },
    { "IONIAN AUGMENTED", {{ 0, 2, 4, 5, 8, 9, 11 }}, 7 },
    { "DORIAN #4", {{ 0, 2, 3, 6, 7, 9, 10 }}, 7 },
    { "PHRYGIAN DOMINANT", {{ 0, 1, 4, 5, 7, 8, 10 }}, 7 },
    { "LYDIAN #2", {{ 0, 3, 4, 6, 7, 9, 11 }}, 7 },
    { "ULTRA LOCRIAN", {{ 0, 1, 3, 4, 6, 8, 9 }}, 7 },
    { "DORIAN B5", {{ 0, 2, 3, 5, 6, 9, 10 }}, 7 },
    { "PHRYGIAN B4", {{ 0, 1, 3, 4, 7, 8, 10 }}, 7 },
    { "LYDIAN B3", {{ 0, 2, 3, 6, 7, 9, 11 }}, 7 },
    { "MIXOLYDIAN B2", {{ 0, 1, 4, 5, 7, 9, 10 }}, 7 },
    { "LYDIAN AUGMENTED #2", {{ 0, 3, 4, 6, 8, 9, 11 }}, 7 },
    { "LOCRIAN BB7", {{ 0, 1, 3, 5, 6, 8, 9 }}, 7 },
    { "PENTATONIC MINOR", {{ 0, 3, 5, 7, 10 }}, 5 },
    { "EGYPTIAN", {{ 0, 2, 5, 7, 10 }}, 5 },
    { "BLUES MINOR", {{ 0, 3, 5, 6, 7, 10 }}, 6 },
    { "BLUES MAJOR", {{ 0, 2, 3, 4, 7, 9 }}, 6 },
    { "HIRAJOSHI", {{ 0, 2, 3, 7, 8 }}, 5 },
    { "IN SEN", {{ 0, 1, 5, 7, 10 }}, 5 },
    { "IWATO", {{ 0, 1, 5, 6, 10 }}, 5 },
    { "KUMOI", {{ 0, 2, 3, 7, 9 }}, 5 },
    { "PELOG", {{ 0, 1, 3, 7, 8 }}, 5 },
    { "PROMETHEUS", {{ 0, 2, 4, 6, 9, 10 }}, 6 },
    { "AUGMENTED", {{ 0, 3, 4, 7, 8, 11 }}, 6 },
    { "TRITONE", {{ 0, 1, 4, 6, 7, 10 }}, 6 },
    { "SIX TONE SYMMETRIC", {{ 0, 1, 4, 5, 8, 9 }}, 6 },
    { "DIMINISHED WHOLE-HALF", {{ 0, 2, 3, 5, 6, 8, 9, 11 }}, 8 },
    { "DIMINISHED HALF-WHOLE", {{ 0, 1, 3, 4, 6, 7, 9, 10 }}, 8 },
    { "BEBOP MAJOR", {{ 0, 2, 4, 5, 7, 8, 9, 11 }}, 8 },
    { "BEBOP DOMINANT", {{ 0, 2, 4, 5, 7, 9, 10, 11 }}, 8 },
    { "BEBOP DORIAN", {{ 0, 2, 3, 4, 5, 7, 9, 10 }}, 8 },
    { "DOUBLE HARMONIC", {{ 0, 1, 4, 5, 7, 8, 11 }}, 7 },
    { "HUNGARIAN MINOR", {{ 0, 2, 3, 6, 7, 8, 11 }}, 7 },
    { "NEAPOLITAN MINOR", {{ 0, 1, 3, 5, 7, 8, 11 }}, 7 },
    { "NEAPOLITAN MAJOR", {{ 0, 1, 3, 5, 7, 9, 11 }}, 7 },
    { "ENIGMATIC", {{ 0, 1, 4, 6, 8, 10, 11 }}, 7 },
    { "PERSIAN", {{ 0, 1, 4, 5, 6, 8, 11 }}, 7 },
    { "MAJOR LOCRIAN", {{ 0, 2, 4, 5, 6, 8, 10 }}, 7 },
    { "LEADING WHOLE TONE", {{ 0, 2, 4, 6, 8, 10, 11 }}, 7 },
    { "JAPANESE", {{ 0, 1, 5, 7, 8 }}, 5 },
    { "YO", {{ 0, 2, 5, 7, 9 }}, 5 },
    { "HUNGARIAN MAJOR", {{ 0, 3, 4, 6, 7, 9, 10 }}, 7 },
    { "ORIENTAL", {{ 0, 1, 4, 5, 6, 9, 10 }}, 7 },
    { "PENTATONIC DOMINANT", {{ 0, 2, 4, 7, 10 }}, 5 },
    { "PENTATONIC MINOR 6", {{ 0, 3, 5, 7, 9 }}, 5 },
    { "MAN GONG", {{ 0, 3, 5, 8, 10 }}, 5 },
    { "MESSIAEN 3", {{ 0, 2, 3, 4, 6, 7, 8, 10, 11 }}, 9 },
    { "MESSIAEN 4", {{ 0, 1, 2, 5, 6, 7, 8, 11 }}, 8 },
    { "MESSIAEN 5", {{ 0, 1, 5, 6, 7, 11 }}, 6 },
    { "MESSIAEN 6", {{ 0, 2, 4, 5, 6, 8, 10, 11 }}, 8 },
    { "MESSIAEN 7", {{ 0, 1, 2, 3, 5, 6, 7, 8, 9, 11 }}, 10 },
    { "PENTATONIC IONIAN", {{ 0, 4, 5, 7, 11 }}, 5 },
    { "PENTATONIC MIXOLYDIAN", {{ 0, 4, 5, 7, 10 }}, 5 },
    { "RITUSEN", {{ 0, 2, 5, 7, 9 }}, 5 },
    { "PENTATONIC NEAP MAJOR", {{ 0, 4, 5, 6, 10 }}, 5 },
    { "VIETNAMESE 1", {{ 0, 3, 5, 7, 8 }}, 5 },
    { "PENTATONIC LYDIAN", {{ 0, 4, 6, 7, 11 }}, 5 },
    { "PENTATONIC LOCRIAN", {{ 0, 3, 5, 6, 10 }}, 5 },
    { "PENTATONIC FLAT 6", {{ 0, 2, 4, 7, 8 }}, 5 },
    { "SCRIABIN", {{ 0, 1, 4, 7, 9 }}, 5 },
    { "PENTATONIC WHOLE TONE", {{ 0, 4, 6, 8, 10 }}, 5 },
    { "PENTATONIC LYDIAN #5", {{ 0, 4, 6, 8, 11 }}, 5 },
    { "PENTATONIC LYD DOM", {{ 0, 4, 6, 7, 10 }}, 5 },
    { "PENTATONIC MIN-MAJ 7", {{ 0, 3, 5, 7, 11 }}, 5 },
    { "PENTATONIC SUPER LOC", {{ 0, 3, 4, 6, 10 }}, 5 },
    { "HEXATONIC MINOR", {{ 0, 2, 3, 5, 7, 11 }}, 6 },
    { "PIONGIO", {{ 0, 2, 5, 7, 9, 10 }}, 6 },
    { "PROMETHEUS NEAPOLITAN", {{ 0, 1, 4, 6, 9, 10 }}, 6 },
    { "MYSTERY 1", {{ 0, 1, 4, 6, 8, 10 }}, 6 },
    { "DOUBLE HARMONIC LYDIAN", {{ 0, 1, 4, 6, 7, 8, 11 }}, 7 },
    { "HEPTATONIC AUGMENTED", {{ 0, 3, 4, 5, 7, 8, 11 }}, 7 },
    { "LYDIAN DIMINISHED", {{ 0, 2, 3, 6, 7, 9, 11 }}, 7 },
    { "LYDIAN MINOR", {{ 0, 2, 4, 6, 7, 8, 10 }}, 7 },
    { "FLAMENCO", {{ 0, 1, 3, 4, 6, 7, 10 }}, 7 },
    { "TODI", {{ 0, 1, 3, 6, 7, 8, 11 }}, 7 },
    { "PURVI", {{ 0, 1, 4, 5, 6, 7, 8, 11 }}, 8 },
    { "SPANISH HEPTATONIC", {{ 0, 1, 3, 4, 5, 7, 8, 10 }}, 8 },
    { "BEBOP LOCRIAN", {{ 0, 1, 3, 5, 6, 7, 8, 10 }}, 8 },
    { "BEBOP MINOR", {{ 0, 2, 3, 5, 7, 8, 10, 11 }}, 8 },
    { "ICHIKOSUCHO", {{ 0, 2, 4, 5, 6, 7, 9, 11 }}, 8 },
    { "MINOR 6 DIMINISHED", {{ 0, 2, 3, 5, 7, 8, 9, 11 }}, 8 },
    { "KAFI", {{ 0, 3, 4, 5, 7, 9, 10, 11 }}, 8 },
    { "BLUES COMPOSITE", {{ 0, 2, 3, 4, 5, 6, 7, 9, 10 }}, 9 },
}};

// Canonical display order: core tonal scales, paired variants, modes,
// pentatonics, bebop collections, regional/global scales, then modern and
// limited-transposition collections.
inline constexpr std::array<uint8_t, kMusicalScaleCount>
    kMusicalScaleMenuOrder {{
    0, 1, 2, 12, 5, 11, 3, 31, 34, 33, 100, 4, 41, 44, 45, 43, 83, 88, 98,
    6, 7, 8, 9, 10,
    13, 14, 15, 16, 17, 18,
    22, 20, 19, 21, 23, 24, 25, 26, 27, 28, 29, 30, 49, 87, 55, 56, 89, 90,
    61, 62, 69, 70, 74, 75, 76, 78, 79, 80, 81, 82, 72,
    46, 96, 47, 48, 95,
    32, 35, 36, 37, 38, 57, 58, 71, 97, 39, 63, 84, 73, 54, 60, 91,
    52, 51, 59, 50, 92, 93, 99, 94,
    40, 85, 53, 77, 42, 86, 64, 65, 66, 67, 68,
}};

inline constexpr const MusicalScaleDefinition&
musicalScaleDefinition(uint32_t scale)
{
    return kMusicalScales[
        std::min<uint32_t>(scale, kMusicalScaleCount - 1u)];
}

inline constexpr uint32_t musicalScaleValueForMenuIndex(uint32_t menuIndex)
{
    return kMusicalScaleMenuOrder[
        std::min<uint32_t>(menuIndex, kMusicalScaleCount - 1u)];
}

inline constexpr uint32_t musicalScaleMenuIndexForValue(uint32_t scale)
{
    for (uint32_t index = 0u; index < kMusicalScaleCount; ++index) {
        if (kMusicalScaleMenuOrder[index] == scale) return index;
    }
    return 0u;
}

inline constexpr bool musicalScaleMenuOrderIsPermutation()
{
    std::array<bool, kMusicalScaleCount> seen {};
    for (const uint32_t scale : kMusicalScaleMenuOrder) {
        if (scale >= kMusicalScaleCount || seen[scale]) return false;
        seen[scale] = true;
    }
    return true;
}
static_assert(musicalScaleMenuOrderIsPermutation());

inline bool musicalScaleValueFromText(const char* text, uint32_t& scale)
{
    if (!text) return false;
    const auto equals = [](const char* left, const char* right) {
        if (!left || !right) return false;
        while (*left && *right) {
            if (std::toupper(static_cast<unsigned char>(*left))
                != std::toupper(static_cast<unsigned char>(*right))) {
                return false;
            }
            ++left;
            ++right;
        }
        return *left == '\0' && *right == '\0';
    };
    for (uint32_t index = 0u; index < kMusicalScaleCount; ++index) {
        if (equals(text, kMusicalScales[index].name)) {
            scale = index;
            return true;
        }
    }
    struct LegacyName {
        const char* name;
        uint8_t scale;
    };
    static constexpr LegacyName legacyNames[] {
        { "CHROM", 0u }, { "PENTA", 3u }, { "MAJOR PENTATONIC", 3u },
        { "MINOR PENTATONIC", 31u }, { "WHOLE", 4u },
        { "HARM MIN", 5u }, { "MEL MIN", 11u }, { "HARM MAJ", 12u },
        { "MINOR BLUES", 33u }, { "MAJOR BLUES", 34u },
    };
    for (const auto& legacy : legacyNames) {
        if (equals(text, legacy.name)) {
            scale = legacy.scale;
            return true;
        }
    }
    return false;
}

} // namespace s3g
