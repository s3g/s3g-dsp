#pragma once

#include <cstddef>
#include <limits>
#include <string_view>

namespace s3g::tracker::app {

inline bool parseGridInstrumentIndex(std::string_view text,
    std::size_t limitExclusive, std::size_t& result) noexcept
{
    if (text.empty() || limitExclusive == 0u) return false;
    std::size_t value = 0u;
    for (char character : text) {
        if (character < '0' || character > '9') return false;
        const auto digit = static_cast<std::size_t>(character - '0');
        if (value > (std::numeric_limits<std::size_t>::max() - digit) / 10u)
            return false;
        value = value * 10u + digit;
    }
    if (value >= limitExclusive) return false;
    result = value;
    return true;
}

inline bool parseGridNormalizedValue(std::string_view text,
    float& result) noexcept
{
    if (text.empty()) return false;
    bool sawDigit = false;
    bool afterDecimal = false;
    bool sawDecimal = false;
    long double value = 0.0L;
    long double place = 0.1L;
    for (char character : text) {
        if (character == '.') {
            if (sawDecimal) return false;
            sawDecimal = true;
            afterDecimal = true;
            continue;
        }
        if (character < '0' || character > '9') return false;
        sawDigit = true;
        const auto digit = static_cast<unsigned>(character - '0');
        if (afterDecimal) {
            value += static_cast<long double>(digit) * place;
            place *= 0.1L;
        } else {
            value = value * 10.0L + static_cast<long double>(digit);
        }
        if (value > 1.0L) return false;
    }
    if (!sawDigit || value < 0.0L || value > 1.0L) return false;
    result = static_cast<float>(value);
    return true;
}

} // namespace s3g::tracker::app
