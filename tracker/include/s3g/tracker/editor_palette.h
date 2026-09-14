#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
namespace s3g::tracker::editor {
enum class ThemeRole : std::uint8_t {
    Canvas,
    Workspace,
    Panel,
    Raised,
    Control,
    ControlHover,
    Selection,
    GridPlayback,
    GridPlaybackAccent,
    GridSelection,
    GridCursor,
    Grid,
    Border,
    BorderStrong,
    TextPrimary,
    TextSecondary,
    TextMuted,
    TextFaint,
    Focus,
    Note,
    Instrument,
    Value,
    Live,
    Success,
    Warning,
    Danger,
};
inline std::uint32_t interpolateRGB(std::uint32_t left, std::uint32_t right, double amount)
{
    const auto channel = [amount](std::uint32_t a, std::uint32_t b) {
        return static_cast<std::uint32_t>(std::lround(
            static_cast<double>(a) + (static_cast<double>(b) - static_cast<double>(a)) * amount));
    };
    const auto red = channel((left >> 16u) & 0xffu, (right >> 16u) & 0xffu);
    const auto green = channel((left >> 8u) & 0xffu, (right >> 8u) & 0xffu);
    const auto blue = channel(left & 0xffu, right & 0xffu);
    return (red << 16u) | (green << 8u) | blue;
}

inline std::uint32_t nightNeutral(std::uint8_t level)
{
    struct Stop {
        std::uint8_t level;
        std::uint32_t rgb;
    };
    constexpr std::array<Stop, 14u> stops { {
        { 0x00, 0x050505 },
        { 0x0c, 0x090909 },
        { 0x13, 0x101010 },
        { 0x1d, 0x181818 },
        { 0x24, 0x222222 },
        { 0x30, 0x2d2d2d },
        { 0x40, 0x3b3b3b },
        { 0x56, 0x505050 },
        { 0x70, 0x6a6a6a },
        { 0x8f, 0x898989 },
        { 0xa8, 0xa5a5a5 },
        { 0xb8, 0xbcbcbc },
        { 0xd0, 0xd4d4d4 },
        { 0xff, 0xf2f2f0 },
    } };
    for (std::size_t index = 1u; index < stops.size(); ++index) {
        if (level > stops[index].level)
            continue;
        const auto& left = stops[index - 1u];
        const auto& right = stops[index];
        const double span = static_cast<double>(right.level - left.level);
        const double amount = span > 0.0 ? static_cast<double>(level - left.level) / span : 0.0;
        return interpolateRGB(left.rgb, right.rgb, amount);
    }
    return stops.back().rgb;
}

inline std::uint32_t themeRGB(ThemeRole role)
{
    switch (role) {
    case ThemeRole::Canvas:
        return 0x060606;
    case ThemeRole::Workspace:
        return 0x0a0a0a;
    case ThemeRole::Panel:
        return 0x101010;
    case ThemeRole::Raised:
        return 0x181818;
    case ThemeRole::Control:
        return 0x262626;
    case ThemeRole::ControlHover:
        return 0x353535;
    case ThemeRole::Selection:
        return 0x424242;
    // Restored from the v8 tracker as grid-only semantic states. Keeping
    // these separate from Live/Selection prevents transport and native
    // controls from acquiring decorative tracker-cell color.
    case ThemeRole::GridPlayback:
        return 0x2e412e;
    case ThemeRole::GridPlaybackAccent:
        return 0x69826b;
    case ThemeRole::GridSelection:
        return 0x303854;
    case ThemeRole::GridCursor:
        return 0x4d4d6b;
    case ThemeRole::Grid:
        return 0x303030;
    case ThemeRole::Border:
        return 0x4c4c4c;
    case ThemeRole::BorderStrong:
        return 0x6a6a6a;
    case ThemeRole::TextPrimary:
        return 0xdededa;
    case ThemeRole::TextSecondary:
        return 0xbababa;
    case ThemeRole::TextMuted:
        return 0x878787;
    case ThemeRole::TextFaint:
        return 0x656565;
    case ThemeRole::Focus:
        return 0xc0c0bc;
    case ThemeRole::Note:
        return 0x85cbd3;
    case ThemeRole::Instrument:
        return 0xb5b5b1;
    case ThemeRole::Value:
        return 0xe8d47d;
    case ThemeRole::Live:
        return 0x7fd7e8;
    case ThemeRole::Success:
        return 0x72d68c;
    case ThemeRole::Warning:
        return 0xf0ad6d;
    case ThemeRole::Danger:
        return 0xf06a72;
    }
    return 0xff00ff;
}

}
