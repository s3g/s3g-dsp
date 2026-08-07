#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace s3g::drum_midi {

constexpr int kOmni = 0;
constexpr int kLastChannel = 16;

inline int receiveChannel(double value)
{
    if (!std::isfinite(value)) return kOmni;
    return std::clamp(static_cast<int>(std::lround(value)),
        kOmni, kLastChannel);
}

// CLAP note-event channels and the channel nibble in raw MIDI are zero-based;
// the user-facing receive parameter is one-based, with zero reserved for OMNI.
inline bool accepts(double receiveValue, int eventChannel)
{
    const int receive = receiveChannel(receiveValue);
    return receive == kOmni
        || (eventChannel >= 0 && eventChannel < kLastChannel
            && eventChannel == receive - 1);
}

inline void valueToText(double value, char* display, unsigned int size)
{
    if (!display || size == 0u) return;
    const int receive = receiveChannel(value);
    if (receive == kOmni) {
        std::snprintf(display, size, "OMNI");
    } else {
        std::snprintf(display, size, "CH %d", receive);
    }
}

inline bool asciiEqualsIgnoreCase(const char* first, const char* second)
{
    if (!first || !second) return false;
    while (*first != '\0' && *second != '\0') {
        const auto a = static_cast<unsigned char>(*first++);
        const auto b = static_cast<unsigned char>(*second++);
        if (std::tolower(a) != std::tolower(b)) return false;
    }
    return *first == '\0' && *second == '\0';
}

inline bool textToValue(const char* display, double* value)
{
    if (!display || !value) return false;
    while (std::isspace(static_cast<unsigned char>(*display)) != 0) {
        ++display;
    }
    if (asciiEqualsIgnoreCase(display, "OMNI")) {
        *value = static_cast<double>(kOmni);
        return true;
    }
    if ((display[0] == 'C' || display[0] == 'c')
        && (display[1] == 'H' || display[1] == 'h')) {
        display += 2;
        while (std::isspace(static_cast<unsigned char>(*display)) != 0) {
            ++display;
        }
    }

    char* end = nullptr;
    const double parsed = std::strtod(display, &end);
    if (end == display || !std::isfinite(parsed)
        || std::floor(parsed) != parsed) {
        return false;
    }
    while (std::isspace(static_cast<unsigned char>(*end)) != 0) ++end;
    if (*end != '\0' || parsed < kOmni || parsed > kLastChannel) {
        return false;
    }
    *value = parsed;
    return true;
}

} // namespace s3g::drum_midi
