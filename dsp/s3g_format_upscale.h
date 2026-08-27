#pragma once

#include "s3g_ambisonic_geometry.h"
#include "s3g_layout_panner.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace s3g {

constexpr uint32_t kFormatUpscaleMaxChannels = 64u;
constexpr uint32_t kFormatUpscaleLayoutCount = 42u;
constexpr float kFormatUpscaleMaxDelayMs = 50.0f;
constexpr float kFormatUpscaleTierHeightTolerance = 0.015f;

enum class FormatUpscaleLayout : uint32_t {
    Mono = 0u,
    Stereo,
    Lcr,
    Quad,
    FiveZero,
    SevenZero,
    OctophonicRing,
    FiveZeroFour,
    SevenZeroFour,
    Ring12,
    Ring16,
    Ring24,
    Ring32,
    Ring48,
    Ring64,
    DoubleRing16,
    DoubleRing20,
    DoubleRing24,
    DoubleRing32,
    DoubleRing48,
    DoubleRing64,
    Cube8,
    Dodeca12,
    Dome24,
    SixZero,
    QuadOverhead6,
    FiveZeroTwo,
    SevenZeroTwo,
    NineZero,
    NineZeroTwo,
    NineZeroFour,
    NineZeroSix,
    SevenZeroSix,
    ElevenZeroEight,
    Cube17,
    Cube41,
    Lpac41,
    Srst25,
    Dome25,
    Icosahedron20,
    Sphere24,
    Custom,
};

enum class FormatUpscaleBasis : uint32_t {
    Direct = 0u,
    Mid,
    Side,
};

enum class FormatUpscalePlacement : uint32_t {
    Match = 0u,
    SameSide,
    Cross,
    Rotate,
    Interleave,
    Nearest,
    Span,
    TierFill,
    MidSideSpread,
};

enum class FormatUpscaleRowShape : uint32_t {
    Flat = 0u,
    Center,
    Edges,
    Taper,
};

enum class FormatUpscaleNormalization : uint32_t {
    Row = 0u,
    Column,
    DualLimit,
    Exact,
};

enum class FormatUpscaleOrigin : uint32_t {
    Keep = 0u,
    Share,
    Remap,
};

enum class FormatUpscaleRole : uint32_t {
    Generic = 0u,
    Mono,
    Left,
    Right,
    Center,
    LeftSurround,
    RightSurround,
    LeftRear,
    RightRear,
    TopLeftFront,
    TopRightFront,
    TopLeftRear,
    TopRightRear,
};

struct FormatUpscaleSpeaker {
    float azimuthDeg = 0.0f;
    float elevationDeg = 0.0f;
    float distance = 1.0f;
    FormatUpscaleRole role = FormatUpscaleRole::Generic;
};

struct FormatUpscaleLayoutData {
    std::array<FormatUpscaleSpeaker, kFormatUpscaleMaxChannels> speakers {};
    uint32_t count = 2u;
};

struct FormatUpscaleParams {
    FormatUpscaleLayout inputLayout = FormatUpscaleLayout::Stereo;
    FormatUpscaleLayout outputLayout = FormatUpscaleLayout::Quad;
    FormatUpscaleBasis basis = FormatUpscaleBasis::Direct;
    FormatUpscalePlacement placement = FormatUpscalePlacement::SameSide;
    FormatUpscaleOrigin origin = FormatUpscaleOrigin::Share;
    float amountPercent = 100.0f;
    uint32_t copies = 2u;
    float rotationDegrees = 90.0f;
    float spreadPercent = 35.0f;
    float delayMs = 0.0f;
    float decorrelationPercent = 0.0f;
    float smoothingMs = 35.0f;
    float outputGainDb = 0.0f;
};

inline float formatUpscaleClamp(float value, float low, float high)
{
    return std::max(low, std::min(value, high));
}

inline float formatUpscaleWrapDegrees(float value)
{
    if (!std::isfinite(value)) return 0.0f;
    value = std::fmod(value, 360.0f);
    if (value > 180.0f) value -= 360.0f;
    if (value <= -180.0f) value += 360.0f;
    return value;
}

inline float formatUpscaleAngularDistance(float a, float b)
{
    return std::abs(formatUpscaleWrapDegrees(a - b));
}

inline float formatUpscaleDbToGain(float value)
{
    return std::pow(10.0f, value / 20.0f);
}

inline float formatUpscaleSpeakerHeight(const FormatUpscaleSpeaker& speaker)
{
    return std::sin(speaker.elevationDeg
        * 3.14159265358979323846f / 180.0f) * speaker.distance;
}

inline const char* formatUpscaleLayoutName(FormatUpscaleLayout layout)
{
    switch (layout) {
    case FormatUpscaleLayout::Mono: return "Mono";
    case FormatUpscaleLayout::Stereo: return "Stereo";
    case FormatUpscaleLayout::Lcr: return "LCR";
    case FormatUpscaleLayout::Quad: return "Quad";
    case FormatUpscaleLayout::FiveZero: return "5.0";
    case FormatUpscaleLayout::SevenZero: return "7.0";
    case FormatUpscaleLayout::OctophonicRing: return "Octophonic ring";
    case FormatUpscaleLayout::FiveZeroFour: return "5.0.4";
    case FormatUpscaleLayout::SevenZeroFour: return "7.0.4";
    case FormatUpscaleLayout::Ring12: return "Ring 12";
    case FormatUpscaleLayout::Ring16: return "Ring 16";
    case FormatUpscaleLayout::Ring24: return "Ring 24";
    case FormatUpscaleLayout::Ring32: return "Ring 32";
    case FormatUpscaleLayout::Ring48: return "Ring 48";
    case FormatUpscaleLayout::Ring64: return "Ring 64";
    case FormatUpscaleLayout::DoubleRing16: return "Double ring 16";
    case FormatUpscaleLayout::DoubleRing20: return "Double ring 20";
    case FormatUpscaleLayout::DoubleRing24: return "Double ring 24";
    case FormatUpscaleLayout::DoubleRing32: return "Double ring 32";
    case FormatUpscaleLayout::DoubleRing48: return "Double ring 48";
    case FormatUpscaleLayout::DoubleRing64: return "Double ring 64";
    case FormatUpscaleLayout::Cube8: return "Cube 8";
    case FormatUpscaleLayout::Dodeca12: return "Dodeca 12";
    case FormatUpscaleLayout::Dome24: return "Dome 24";
    case FormatUpscaleLayout::SixZero: return "6.0";
    case FormatUpscaleLayout::QuadOverhead6: return "Quad + overhead";
    case FormatUpscaleLayout::FiveZeroTwo: return "5.0.2";
    case FormatUpscaleLayout::SevenZeroTwo: return "7.0.2";
    case FormatUpscaleLayout::NineZero: return "9.0";
    case FormatUpscaleLayout::NineZeroTwo: return "9.0.2";
    case FormatUpscaleLayout::NineZeroFour: return "9.0.4";
    case FormatUpscaleLayout::NineZeroSix: return "9.0.6";
    case FormatUpscaleLayout::SevenZeroSix: return "7.0.6";
    case FormatUpscaleLayout::ElevenZeroEight: return "11.0.8";
    case FormatUpscaleLayout::Cube17: return "Cube 17";
    case FormatUpscaleLayout::Cube41: return "Cube 41";
    case FormatUpscaleLayout::Lpac41: return "LPAC 41";
    case FormatUpscaleLayout::Srst25: return "SRST 25";
    case FormatUpscaleLayout::Dome25: return "Dome 25";
    case FormatUpscaleLayout::Icosahedron20: return "Icosahedron 20";
    case FormatUpscaleLayout::Sphere24: return "Sphere 24";
    case FormatUpscaleLayout::Custom: return "Custom AED";
    default: return "Stereo";
    }
}

inline const char* formatUpscaleBasisName(FormatUpscaleBasis basis)
{
    switch (basis) {
    case FormatUpscaleBasis::Mid: return "Mid";
    case FormatUpscaleBasis::Side: return "Side";
    case FormatUpscaleBasis::Direct:
    default: return "Direct";
    }
}

inline const char* formatUpscalePlacementName(FormatUpscalePlacement placement)
{
    switch (placement) {
    case FormatUpscalePlacement::Match: return "Match";
    case FormatUpscalePlacement::SameSide: return "Same side";
    case FormatUpscalePlacement::Cross: return "Cross";
    case FormatUpscalePlacement::Rotate: return "Rotate";
    case FormatUpscalePlacement::Interleave: return "Interleave";
    case FormatUpscalePlacement::Nearest: return "Nearest";
    case FormatUpscalePlacement::Span: return "Span";
    case FormatUpscalePlacement::TierFill: return "Tier fill";
    case FormatUpscalePlacement::MidSideSpread: return "Tier fill";
    default: return "Same side";
    }
}

inline const char* formatUpscaleRowShapeName(FormatUpscaleRowShape shape)
{
    switch (shape) {
    case FormatUpscaleRowShape::Center: return "Center";
    case FormatUpscaleRowShape::Edges: return "Edges";
    case FormatUpscaleRowShape::Taper: return "Taper";
    case FormatUpscaleRowShape::Flat:
    default: return "Flat";
    }
}

inline const char* formatUpscaleNormalizationName(
    FormatUpscaleNormalization normalization)
{
    switch (normalization) {
    case FormatUpscaleNormalization::Column: return "Column";
    case FormatUpscaleNormalization::DualLimit: return "Dual limit";
    case FormatUpscaleNormalization::Exact: return "Exact";
    case FormatUpscaleNormalization::Row:
    default: return "Row";
    }
}

inline const char* formatUpscaleOriginName(FormatUpscaleOrigin origin)
{
    switch (origin) {
    case FormatUpscaleOrigin::Keep: return "Keep";
    case FormatUpscaleOrigin::Remap: return "Remap";
    case FormatUpscaleOrigin::Share:
    default: return "Share";
    }
}

inline const char* formatUpscaleRoleName(FormatUpscaleRole role)
{
    switch (role) {
    case FormatUpscaleRole::Mono: return "M";
    case FormatUpscaleRole::Left: return "L";
    case FormatUpscaleRole::Right: return "R";
    case FormatUpscaleRole::Center: return "C";
    case FormatUpscaleRole::LeftSurround: return "LS";
    case FormatUpscaleRole::RightSurround: return "RS";
    case FormatUpscaleRole::LeftRear: return "LR";
    case FormatUpscaleRole::RightRear: return "RR";
    case FormatUpscaleRole::TopLeftFront: return "TLF";
    case FormatUpscaleRole::TopRightFront: return "TRF";
    case FormatUpscaleRole::TopLeftRear: return "TLR";
    case FormatUpscaleRole::TopRightRear: return "TRR";
    case FormatUpscaleRole::Generic:
    default: return "";
    }
}

inline FormatUpscaleRole formatUpscaleRoleForAed(
    float azimuthDeg, float elevationDeg)
{
    const float azimuth = formatUpscaleWrapDegrees(azimuthDeg);
    const float absoluteAzimuth = std::abs(azimuth);
    if (elevationDeg > 25.0f) {
        if (azimuth >= 0.0f)
            return absoluteAzimuth < 90.0f
                ? FormatUpscaleRole::TopLeftFront
                : FormatUpscaleRole::TopLeftRear;
        return absoluteAzimuth < 90.0f
            ? FormatUpscaleRole::TopRightFront
            : FormatUpscaleRole::TopRightRear;
    }
    if (absoluteAzimuth < 5.0f) return FormatUpscaleRole::Center;
    if (azimuth > 0.0f) {
        if (absoluteAzimuth < 60.0f) return FormatUpscaleRole::Left;
        if (absoluteAzimuth < 135.0f) return FormatUpscaleRole::LeftSurround;
        return FormatUpscaleRole::LeftRear;
    }
    if (absoluteAzimuth < 60.0f) return FormatUpscaleRole::Right;
    if (absoluteAzimuth < 135.0f) return FormatUpscaleRole::RightSurround;
    return FormatUpscaleRole::RightRear;
}

inline bool formatUpscalePannerPreset(FormatUpscaleLayout layout,
    LayoutPannerPreset& preset)
{
    switch (layout) {
    case FormatUpscaleLayout::Quad: preset = LayoutPannerPreset::Quad; break;
    case FormatUpscaleLayout::FiveZero: preset = LayoutPannerPreset::FiveZero; break;
    case FormatUpscaleLayout::SixZero: preset = LayoutPannerPreset::SixZero; break;
    case FormatUpscaleLayout::SevenZero: preset = LayoutPannerPreset::SevenZero; break;
    case FormatUpscaleLayout::OctophonicRing:
        preset = LayoutPannerPreset::OctophonicRing; break;
    case FormatUpscaleLayout::QuadOverhead6:
        preset = LayoutPannerPreset::QuadOverhead6; break;
    case FormatUpscaleLayout::FiveZeroTwo:
        preset = LayoutPannerPreset::FiveZeroTwo; break;
    case FormatUpscaleLayout::SevenZeroTwo:
        preset = LayoutPannerPreset::SevenZeroTwo; break;
    case FormatUpscaleLayout::FiveZeroFour:
        preset = LayoutPannerPreset::FiveZeroFour; break;
    case FormatUpscaleLayout::SevenZeroFour:
        preset = LayoutPannerPreset::SevenZeroFour; break;
    case FormatUpscaleLayout::NineZero: preset = LayoutPannerPreset::NineZero; break;
    case FormatUpscaleLayout::NineZeroTwo:
        preset = LayoutPannerPreset::NineZeroTwo; break;
    case FormatUpscaleLayout::NineZeroFour:
        preset = LayoutPannerPreset::NineZeroFour; break;
    case FormatUpscaleLayout::NineZeroSix:
        preset = LayoutPannerPreset::NineZeroSix; break;
    case FormatUpscaleLayout::SevenZeroSix:
        preset = LayoutPannerPreset::SevenZeroSix; break;
    case FormatUpscaleLayout::ElevenZeroEight:
        preset = LayoutPannerPreset::ElevenZeroEight; break;
    case FormatUpscaleLayout::Ring12: preset = LayoutPannerPreset::Ring12; break;
    case FormatUpscaleLayout::Ring16: preset = LayoutPannerPreset::Ring16; break;
    case FormatUpscaleLayout::DoubleRing16:
        preset = LayoutPannerPreset::DoubleRing16; break;
    case FormatUpscaleLayout::DoubleRing20:
        preset = LayoutPannerPreset::DoubleRing20; break;
    case FormatUpscaleLayout::Cube8: preset = LayoutPannerPreset::Cube8; break;
    case FormatUpscaleLayout::Cube17: preset = LayoutPannerPreset::Cube17; break;
    case FormatUpscaleLayout::Cube41: preset = LayoutPannerPreset::Cube41; break;
    case FormatUpscaleLayout::Lpac41: preset = LayoutPannerPreset::Lpac41; break;
    case FormatUpscaleLayout::Dodeca12:
        preset = LayoutPannerPreset::Dodeca12; break;
    case FormatUpscaleLayout::Icosahedron20:
        preset = LayoutPannerPreset::Icosahedron20; break;
    case FormatUpscaleLayout::Dome24:
        preset = LayoutPannerPreset::Dome24NoOverhead; break;
    case FormatUpscaleLayout::Dome25: preset = LayoutPannerPreset::Dome25; break;
    case FormatUpscaleLayout::Srst25: preset = LayoutPannerPreset::Srst25; break;
    default: return false;
    }
    return true;
}

inline void formatUpscaleSetSpeaker(FormatUpscaleLayoutData& data,
    uint32_t index, float azimuthDeg, float elevationDeg,
    FormatUpscaleRole role = FormatUpscaleRole::Generic,
    float distance = 1.0f)
{
    if (index >= data.speakers.size()) return;
    data.speakers[index] = {
        formatUpscaleWrapDegrees(azimuthDeg),
        formatUpscaleClamp(elevationDeg, -90.0f, 90.0f),
        formatUpscaleClamp(distance, 0.1f, 3.0f), role };
}

inline void formatUpscaleSetRing(FormatUpscaleLayoutData& data,
    uint32_t base, uint32_t count, float startAzimuthDeg, float elevationDeg)
{
    if (count == 0u) return;
    const float step = 360.0f / static_cast<float>(count);
    for (uint32_t index = 0u; index < count; ++index)
        formatUpscaleSetSpeaker(data, base + index,
            startAzimuthDeg - step * static_cast<float>(index), elevationDeg);
}

inline FormatUpscaleLayoutData formatUpscaleLayoutData(
    FormatUpscaleLayout layout)
{
    FormatUpscaleLayoutData data {};
    LayoutPannerPreset policyPreset {};
    if (formatUpscalePannerPreset(layout, policyPreset)) {
        LayoutPanner policy;
        policy.prepare(48000.0);
        auto params = policy.params();
        params.layout = policyPreset;
        policy.setParams(params);
        data.count = policy.activeSpeakers();
        for (uint32_t index = 0u; index < data.count; ++index) {
            const auto& speaker = policy.speakers()[index];
            formatUpscaleSetSpeaker(data, index, speaker.azimuthDeg,
                speaker.elevationDeg,
                formatUpscaleRoleForAed(
                    speaker.azimuthDeg, speaker.elevationDeg),
                speaker.distance);
        }
        return data;
    }
    switch (layout) {
    case FormatUpscaleLayout::Mono:
        data.count = 1u;
        formatUpscaleSetSpeaker(data, 0u, 0.0f, 0.0f,
            FormatUpscaleRole::Mono);
        break;
    case FormatUpscaleLayout::Stereo:
        data.count = 2u;
        formatUpscaleSetSpeaker(data, 0u, -30.0f, 0.0f,
            FormatUpscaleRole::Right);
        formatUpscaleSetSpeaker(data, 1u, 30.0f, 0.0f,
            FormatUpscaleRole::Left);
        break;
    case FormatUpscaleLayout::Lcr:
        data.count = 3u;
        formatUpscaleSetSpeaker(data, 0u, -30.0f, 0.0f,
            FormatUpscaleRole::Right);
        formatUpscaleSetSpeaker(data, 1u, 30.0f, 0.0f,
            FormatUpscaleRole::Left);
        formatUpscaleSetSpeaker(data, 2u, 0.0f, 0.0f,
            FormatUpscaleRole::Center);
        break;
    case FormatUpscaleLayout::Ring24:
        data.count = 24u;
        formatUpscaleSetRing(data, 0u, 24u, -30.0f, 0.0f);
        break;
    case FormatUpscaleLayout::Ring32:
        data.count = 32u;
        formatUpscaleSetRing(data, 0u, 32u, -30.0f, 0.0f);
        break;
    case FormatUpscaleLayout::Ring48:
        data.count = 48u;
        formatUpscaleSetRing(data, 0u, 48u, -30.0f, 0.0f);
        break;
    case FormatUpscaleLayout::Ring64:
        data.count = 64u;
        formatUpscaleSetRing(data, 0u, 64u, -30.0f, 0.0f);
        break;
    case FormatUpscaleLayout::DoubleRing24:
        data.count = 24u;
        formatUpscaleSetRing(data, 0u, 12u, -30.0f, 0.0f);
        formatUpscaleSetRing(data, 12u, 12u, -15.0f, 45.0f);
        break;
    case FormatUpscaleLayout::DoubleRing32:
        data.count = 32u;
        formatUpscaleSetRing(data, 0u, 16u, -22.5f, 0.0f);
        formatUpscaleSetRing(data, 16u, 16u, -11.25f, 45.0f);
        break;
    case FormatUpscaleLayout::DoubleRing48:
        data.count = 48u;
        formatUpscaleSetRing(data, 0u, 24u, -15.0f, 0.0f);
        formatUpscaleSetRing(data, 24u, 24u, -7.5f, 45.0f);
        break;
    case FormatUpscaleLayout::DoubleRing64:
        data.count = 64u;
        formatUpscaleSetRing(data, 0u, 32u, -11.25f, 0.0f);
        formatUpscaleSetRing(data, 32u, 32u, -5.625f, 45.0f);
        break;
    case FormatUpscaleLayout::Sphere24:
        data.count = kAmbisonicSphere24PointCount;
        for (uint32_t index = 0u; index < data.count; ++index) {
            const auto& direction = kAmbisonicSphere24Points[index];
            const float azimuth = std::atan2(direction.y, direction.x)
                * 180.0f / kPi;
            const float elevation = std::asin(formatUpscaleClamp(
                direction.z, -1.0f, 1.0f)) * 180.0f / kPi;
            formatUpscaleSetSpeaker(data, index, azimuth, elevation,
                formatUpscaleRoleForAed(azimuth, elevation));
        }
        break;
    case FormatUpscaleLayout::Custom:
        data.count = 3u;
        formatUpscaleSetSpeaker(data, 0u, -30.0f, 0.0f,
            FormatUpscaleRole::Right);
        formatUpscaleSetSpeaker(data, 1u, 30.0f, 0.0f,
            FormatUpscaleRole::Left);
        formatUpscaleSetSpeaker(data, 2u, 0.0f, 0.0f,
            FormatUpscaleRole::Center);
        break;
    default:
        break;
    }
    return data;
}

inline uint32_t formatUpscaleLayoutChannels(FormatUpscaleLayout layout)
{
    LayoutPannerPreset preset {};
    if (formatUpscalePannerPreset(layout, preset))
        return layoutPannerPresetSpeakerCount(preset);
    switch (layout) {
    case FormatUpscaleLayout::Mono: return 1u;
    case FormatUpscaleLayout::Stereo: return 2u;
    case FormatUpscaleLayout::Lcr: return 3u;
    case FormatUpscaleLayout::Ring24:
    case FormatUpscaleLayout::DoubleRing24:
    case FormatUpscaleLayout::Sphere24: return 24u;
    case FormatUpscaleLayout::Ring32:
    case FormatUpscaleLayout::DoubleRing32: return 32u;
    case FormatUpscaleLayout::Ring48:
    case FormatUpscaleLayout::DoubleRing48: return 48u;
    case FormatUpscaleLayout::Ring64:
    case FormatUpscaleLayout::DoubleRing64: return 64u;
    case FormatUpscaleLayout::Custom: return 3u;
    default: return 2u;
    }
}

inline FormatUpscaleLayoutData sanitizeFormatUpscaleLayoutData(
    FormatUpscaleLayoutData data, uint32_t minimumCount = 1u)
{
    data.count = std::clamp<uint32_t>(data.count, minimumCount,
        kFormatUpscaleMaxChannels);
    for (uint32_t index = 0u; index < data.count; ++index) {
        auto& speaker = data.speakers[index];
        speaker.azimuthDeg = formatUpscaleWrapDegrees(speaker.azimuthDeg);
        speaker.elevationDeg = formatUpscaleClamp(
            speaker.elevationDeg, -90.0f, 90.0f);
        speaker.distance = formatUpscaleClamp(speaker.distance, 0.1f, 3.0f);
        speaker.role = formatUpscaleRoleForAed(
            speaker.azimuthDeg, speaker.elevationDeg);
    }
    return data;
}

inline FormatUpscaleLayoutData formatUpscaleDefaultCustomInputLayout()
{
    FormatUpscaleLayoutData data {};
    data.count = 3u;
    formatUpscaleSetSpeaker(data, 0u, -30.0f, 0.0f,
        FormatUpscaleRole::Right);
    formatUpscaleSetSpeaker(data, 1u, 30.0f, 0.0f,
        FormatUpscaleRole::Left);
    formatUpscaleSetSpeaker(data, 2u, 0.0f, 0.0f,
        FormatUpscaleRole::Center);
    return data;
}

inline FormatUpscaleLayoutData formatUpscaleDefaultThreeTierOutputLayout()
{
    FormatUpscaleLayoutData data {};
    data.count = 9u;
    constexpr float elevations[3] { -35.0f, 0.0f, 45.0f };
    constexpr float distances[3] { 1.15f, 1.0f, 0.9f };
    for (uint32_t tier = 0u; tier < 3u; ++tier) {
        for (uint32_t speaker = 0u; speaker < 3u; ++speaker) {
            const float azimuth = formatUpscaleWrapDegrees(
                -45.0f - static_cast<float>(speaker) * 120.0f);
            formatUpscaleSetSpeaker(data, tier * 3u + speaker,
                azimuth, elevations[tier],
                formatUpscaleRoleForAed(azimuth, elevations[tier]),
                distances[tier]);
        }
    }
    return data;
}

inline FormatUpscaleParams sanitizeFormatUpscaleParams(FormatUpscaleParams p)
{
    p.inputLayout = static_cast<FormatUpscaleLayout>(std::min<uint32_t>(
        static_cast<uint32_t>(p.inputLayout), kFormatUpscaleLayoutCount - 1u));
    p.outputLayout = static_cast<FormatUpscaleLayout>(std::min<uint32_t>(
        static_cast<uint32_t>(p.outputLayout), kFormatUpscaleLayoutCount - 1u));
    if (p.inputLayout != FormatUpscaleLayout::Custom
        && p.outputLayout != FormatUpscaleLayout::Custom
        && formatUpscaleLayoutChannels(p.outputLayout)
            < formatUpscaleLayoutChannels(p.inputLayout))
        p.outputLayout = p.inputLayout;
    // Matrix Upmix is deliberately a positive-gain direct matrix. Retain the
    // historical enum values for state compatibility, but fold them into the
    // direct model when parameters are sanitized.
    p.basis = FormatUpscaleBasis::Direct;
    p.placement = static_cast<FormatUpscalePlacement>(std::min<uint32_t>(
        static_cast<uint32_t>(p.placement),
        static_cast<uint32_t>(FormatUpscalePlacement::TierFill)));
    p.origin = static_cast<FormatUpscaleOrigin>(std::min<uint32_t>(
        static_cast<uint32_t>(p.origin),
        static_cast<uint32_t>(FormatUpscaleOrigin::Remap)));
    p.amountPercent = formatUpscaleClamp(p.amountPercent, 0.0f, 100.0f);
    p.copies = std::clamp<uint32_t>(p.copies, 1u,
        kFormatUpscaleMaxChannels);
    p.rotationDegrees = formatUpscaleClamp(p.rotationDegrees, -180.0f, 180.0f);
    p.spreadPercent = formatUpscaleClamp(p.spreadPercent, 0.0f, 100.0f);
    p.delayMs = formatUpscaleClamp(p.delayMs, 0.0f, kFormatUpscaleMaxDelayMs);
    p.decorrelationPercent = formatUpscaleClamp(
        p.decorrelationPercent, 0.0f, 100.0f);
    p.smoothingMs = formatUpscaleClamp(p.smoothingMs, 1.0f, 500.0f);
    p.outputGainDb = formatUpscaleClamp(p.outputGainDb, -24.0f, 12.0f);
    return p;
}

class FormatUpscale {
public:
    FormatUpscale()
    {
        customInputLayout_ = formatUpscaleDefaultCustomInputLayout();
        customOutputLayout_ = formatUpscaleDefaultThreeTierOutputLayout();
        layoutPolicy_.prepare(48000.0);
        rebuildTargets();
        currentAnchor_ = targetAnchor_;
        currentExtension_ = targetExtension_;
        rebuildActiveRoutes();
    }

    void prepare(double sampleRate)
    {
        sampleRate_ = std::max(1000.0, sampleRate);
        layoutPolicy_.prepare(sampleRate_);
        delayLength_ = std::max<uint32_t>(2u,
            static_cast<uint32_t>(std::ceil(sampleRate_
                * static_cast<double>(kFormatUpscaleMaxDelayMs) * 0.001)) + 2u);
        delayBuffer_.assign(
            static_cast<size_t>(delayLength_) * kFormatUpscaleMaxChannels,
            0.0f);
        reset();
    }

    void reset()
    {
        std::fill(delayBuffer_.begin(), delayBuffer_.end(), 0.0f);
        allpassState_.fill(0.0f);
        currentDelaySamples_.fill(0.0f);
        delayWriteIndex_ = 0u;
        currentAnchor_ = targetAnchor_;
        currentExtension_ = targetExtension_;
        rebuildActiveRoutes();
    }

    void setParams(const FormatUpscaleParams& params)
    {
        const auto sanitized = sanitizeFormatUpscaleParams(params);
        if (paramsEqual(params_, sanitized)) return;
        params_ = sanitized;
        rebuildTargets();
    }

    const FormatUpscaleParams& params() const { return params_; }
    FormatUpscaleRowShape autoRowShape() const { return autoRowShape_; }
    FormatUpscaleNormalization normalization() const { return normalization_; }

    void setAutoRowShape(FormatUpscaleRowShape shape)
    {
        const auto sanitized = static_cast<FormatUpscaleRowShape>(
            std::min<uint32_t>(static_cast<uint32_t>(shape),
                static_cast<uint32_t>(FormatUpscaleRowShape::Taper)));
        if (autoRowShape_ == sanitized) return;
        autoRowShape_ = sanitized;
        if (!manualRoutesActive_) rebuildTargets();
    }

    void setNormalization(FormatUpscaleNormalization normalization)
    {
        const auto sanitized = static_cast<FormatUpscaleNormalization>(
            std::min<uint32_t>(static_cast<uint32_t>(normalization),
                static_cast<uint32_t>(FormatUpscaleNormalization::Exact)));
        if (normalization_ == sanitized) return;
        normalization_ = sanitized;
        rebuildTargets();
    }
    const FormatUpscaleLayoutData& inputLayout() const { return inputLayout_; }
    const FormatUpscaleLayoutData& outputLayout() const { return outputLayout_; }
    const FormatUpscaleLayoutData& customInputLayout() const
        { return customInputLayout_; }
    const FormatUpscaleLayoutData& customOutputLayout() const
        { return customOutputLayout_; }
    uint32_t activeInputs() const { return inputLayout_.count; }
    uint32_t activeOutputs() const { return outputLayout_.count; }
    bool manualRoutesActive() const { return manualRoutesActive_; }

    bool manualRoute(uint32_t input, uint32_t output) const
    {
        return input < kFormatUpscaleMaxChannels
                && output < kFormatUpscaleMaxChannels
            ? std::abs(manualWeights_[matrixIndex(input, output)])
                > 0.000001f : false;
    }

    float manualWeight(uint32_t input, uint32_t output) const
    {
        return input < kFormatUpscaleMaxChannels
                && output < kFormatUpscaleMaxChannels
            ? manualWeights_[matrixIndex(input, output)] : 0.0f;
    }

    const std::array<uint8_t,
        kFormatUpscaleMaxChannels * kFormatUpscaleMaxChannels>&
        manualRoutes() const { return manualRoutes_; }

    const std::array<float,
        kFormatUpscaleMaxChannels * kFormatUpscaleMaxChannels>&
        manualWeights() const { return manualWeights_; }

    void setManualRoutes(const std::array<uint8_t,
            kFormatUpscaleMaxChannels * kFormatUpscaleMaxChannels>& routes,
        bool active = true)
    {
        for (uint32_t index = 0u; index < manualRoutes_.size(); ++index) {
            manualRoutes_[index] = routes[index] != 0u ? 1u : 0u;
            manualWeights_[index] = routes[index] != 0u ? 1.0f : 0.0f;
        }
        manualRoutesActive_ = active;
        rebuildTargets();
    }

    void setManualWeights(const std::array<float,
            kFormatUpscaleMaxChannels * kFormatUpscaleMaxChannels>& weights,
        bool active = true)
    {
        for (uint32_t index = 0u; index < manualWeights_.size(); ++index) {
            const float weight = std::isfinite(weights[index])
                ? formatUpscaleClamp(weights[index], -1.0f, 1.0f) : 0.0f;
            manualWeights_[index] = weight;
            manualRoutes_[index] = std::abs(weight) > 0.000001f ? 1u : 0u;
        }
        manualRoutesActive_ = active;
        rebuildTargets();
    }

    void beginManualRoutesFromCurrent()
    {
        if (manualRoutesActive_) return;
        for (uint32_t input = 0u; input < kFormatUpscaleMaxChannels; ++input) {
            for (uint32_t output = 0u; output < kFormatUpscaleMaxChannels;
                 ++output) {
                const uint32_t index = matrixIndex(input, output);
                const float weight = targetAnchor_[index]
                    + targetExtension_[index];
                manualWeights_[index] = formatUpscaleClamp(
                    weight, -1.0f, 1.0f);
                manualRoutes_[index] = std::abs(weight) > 0.000001f
                    ? 1u : 0u;
            }
        }
        manualRoutesActive_ = true;
        rebuildTargets();
    }

    void setManualRoute(uint32_t input, uint32_t output, bool enabled)
    {
        if (input >= kFormatUpscaleMaxChannels
            || output >= kFormatUpscaleMaxChannels) return;
        manualRoutesActive_ = true;
        manualRoutes_[matrixIndex(input, output)] = enabled ? 1u : 0u;
        manualWeights_[matrixIndex(input, output)] = enabled ? 1.0f : 0.0f;
        rebuildTargets();
    }

    void setManualWeight(uint32_t input, uint32_t output, float weight)
    {
        if (input >= kFormatUpscaleMaxChannels
            || output >= kFormatUpscaleMaxChannels) return;
        manualRoutesActive_ = true;
        const uint32_t index = matrixIndex(input, output);
        manualWeights_[index] = std::isfinite(weight)
            ? formatUpscaleClamp(weight, -1.0f, 1.0f) : 0.0f;
        manualRoutes_[index] = std::abs(manualWeights_[index]) > 0.000001f
            ? 1u : 0u;
        rebuildTargets();
    }

    void clearManualRoutes()
    {
        manualRoutes_.fill(0u);
        manualWeights_.fill(0.0f);
        manualRoutesActive_ = true;
        rebuildTargets();
    }

    void useAutomaticRoutes()
    {
        manualRoutesActive_ = false;
        rebuildTargets();
    }

    void setCustomInputLayout(const FormatUpscaleLayoutData& layout)
    {
        customInputLayout_ = sanitizeFormatUpscaleLayoutData(layout);
        if (params_.inputLayout == FormatUpscaleLayout::Custom)
            rebuildTargets();
    }

    void setCustomOutputLayout(const FormatUpscaleLayoutData& layout)
    {
        customOutputLayout_ = sanitizeFormatUpscaleLayoutData(layout);
        if (params_.outputLayout == FormatUpscaleLayout::Custom)
            rebuildTargets();
    }

    float targetAnchorGain(uint32_t input, uint32_t output) const
    {
        return input < kFormatUpscaleMaxChannels
                && output < kFormatUpscaleMaxChannels
            ? targetAnchor_[matrixIndex(input, output)] : 0.0f;
    }

    float targetExtensionGain(uint32_t input, uint32_t output) const
    {
        return input < kFormatUpscaleMaxChannels
                && output < kFormatUpscaleMaxChannels
            ? targetExtension_[matrixIndex(input, output)] : 0.0f;
    }

    void processFrame(const float* input, uint32_t availableInputs,
        float* output, uint32_t availableOutputs)
    {
        if (!output) return;
        const uint32_t outputChannels = std::min<uint32_t>(
            availableOutputs, kFormatUpscaleMaxChannels);
        for (uint32_t channel = 0u; channel < outputChannels; ++channel)
            output[channel] = 0.0f;

        const uint32_t inputChannels = std::min<uint32_t>({
            availableInputs, inputLayout_.count, kFormatUpscaleMaxChannels });
        const uint32_t activeOutputChannels = std::min<uint32_t>(
            outputChannels, outputLayout_.count);
        const float smooth = smoothingCoefficient();
        const float outputGain = formatUpscaleDbToGain(params_.outputGainDb);
        std::array<float, kFormatUpscaleMaxChannels> extensionSums {};

        for (uint32_t route = 0u; route < activeRouteCount_; ++route) {
            const uint32_t index = activeRouteIndices_[route];
            const uint32_t source = index / kFormatUpscaleMaxChannels;
            const uint32_t destination = index % kFormatUpscaleMaxChannels;
            currentAnchor_[index] +=
                (targetAnchor_[index] - currentAnchor_[index]) * smooth;
            currentExtension_[index] +=
                (targetExtension_[index] - currentExtension_[index]) * smooth;
            if (source >= inputChannels || destination >= activeOutputChannels)
                continue;
            const float sample = input ? input[source] : 0.0f;
            output[destination] += sample * currentAnchor_[index];
            extensionSums[destination] += sample * currentExtension_[index];
        }
        for (uint32_t destination = 0u;
             destination < activeOutputChannels; ++destination) {
            const float extension = processExtension(
                destination, extensionSums[destination], smooth);
            output[destination] = (output[destination] + extension) * outputGain;
        }
        if (delayLength_ > 0u)
            delayWriteIndex_ = (delayWriteIndex_ + 1u) % delayLength_;
    }

private:
    static constexpr uint32_t matrixIndex(uint32_t input, uint32_t output)
    {
        return input * kFormatUpscaleMaxChannels + output;
    }

    static bool paramsEqual(const FormatUpscaleParams& a,
        const FormatUpscaleParams& b)
    {
        return a.inputLayout == b.inputLayout
            && a.outputLayout == b.outputLayout
            && a.basis == b.basis
            && a.placement == b.placement
            && a.origin == b.origin
            && a.copies == b.copies
            && std::abs(a.amountPercent - b.amountPercent) < 0.0001f
            && std::abs(a.rotationDegrees - b.rotationDegrees) < 0.0001f
            && std::abs(a.spreadPercent - b.spreadPercent) < 0.0001f
            && std::abs(a.delayMs - b.delayMs) < 0.0001f
            && std::abs(a.decorrelationPercent - b.decorrelationPercent)
                < 0.0001f
            && std::abs(a.smoothingMs - b.smoothingMs) < 0.0001f
            && std::abs(a.outputGainDb - b.outputGainDb) < 0.0001f;
    }

    static int speakerSide(const FormatUpscaleSpeaker& speaker)
    {
        if (speaker.azimuthDeg < -5.0f) return -1;
        if (speaker.azimuthDeg > 5.0f) return 1;
        return 0;
    }

    static float speakerDistance(const FormatUpscaleSpeaker& a,
        const FormatUpscaleSpeaker& b)
    {
        return formatUpscaleAngularDistance(a.azimuthDeg, b.azimuthDeg)
            + std::abs(a.elevationDeg - b.elevationDeg) * 1.35f
            + std::abs(a.distance - b.distance) * 20.0f;
    }

    FormatUpscaleLayoutData resolveLayout(
        FormatUpscaleLayout layout, bool input)
    {
        if (layout == FormatUpscaleLayout::Custom)
            return input ? customInputLayout_ : customOutputLayout_;
        LayoutPannerPreset policyPreset {};
        if (!formatUpscalePannerPreset(layout, policyPreset))
            return formatUpscaleLayoutData(layout);

        auto policyParams = layoutPolicy_.params();
        policyParams.layout = policyPreset;
        layoutPolicy_.setParams(policyParams);
        FormatUpscaleLayoutData data {};
        data.count = layoutPolicy_.activeSpeakers();
        for (uint32_t index = 0u; index < data.count; ++index) {
            const auto& speaker = layoutPolicy_.speakers()[index];
            formatUpscaleSetSpeaker(data, index, speaker.azimuthDeg,
                speaker.elevationDeg,
                formatUpscaleRoleForAed(
                    speaker.azimuthDeg, speaker.elevationDeg),
                speaker.distance);
        }
        return data;
    }

    uint32_t findAnchor(uint32_t source,
        const std::array<bool, kFormatUpscaleMaxChannels>& alreadyUsed) const
    {
        const auto& inputSpeaker = inputLayout_.speakers[source];
        if (inputSpeaker.role != FormatUpscaleRole::Generic) {
            for (uint32_t output = 0u; output < outputLayout_.count; ++output) {
                if (!alreadyUsed[output]
                    && outputLayout_.speakers[output].role == inputSpeaker.role)
                    return output;
            }
        }
        if (params_.inputLayout == params_.outputLayout
            && source < outputLayout_.count && !alreadyUsed[source])
            return source;

        uint32_t best = kFormatUpscaleMaxChannels;
        float bestScore = 1000000.0f;
        for (uint32_t output = 0u; output < outputLayout_.count; ++output) {
            if (alreadyUsed[output]) continue;
            const float score = speakerDistance(
                inputSpeaker, outputLayout_.speakers[output]);
            if (score < bestScore) {
                bestScore = score;
                best = output;
            }
        }
        return best;
    }

    float candidateScore(uint32_t source, uint32_t output,
        uint32_t copyIndex) const
    {
        const auto& inputSpeaker = inputLayout_.speakers[source];
        const auto& outputSpeaker = outputLayout_.speakers[output];
        float score = speakerDistance(inputSpeaker, outputSpeaker);
        const int inputSide = speakerSide(inputSpeaker);
        const int outputSide = speakerSide(outputSpeaker);
        switch (params_.placement) {
        case FormatUpscalePlacement::SameSide:
            if (inputSide == 0) {
                if (outputSide != 0) score += 1000.0f;
            } else if (outputSide != inputSide) {
                score += 1000.0f;
            }
            break;
        case FormatUpscalePlacement::Cross:
            if (inputSide == 0) {
                if (outputSide != 0) score += 1000.0f;
            } else if (outputSide != -inputSide) {
                score += 1000.0f;
            }
            break;
        case FormatUpscalePlacement::Rotate: {
            const float targetAzimuth = formatUpscaleWrapDegrees(
                inputSpeaker.azimuthDeg + params_.rotationDegrees
                    * static_cast<float>(copyIndex + 1u));
            score = formatUpscaleAngularDistance(
                    outputSpeaker.azimuthDeg, targetAzimuth)
                + std::abs(outputSpeaker.elevationDeg
                    - inputSpeaker.elevationDeg) * 0.7f;
            break;
        }
        case FormatUpscalePlacement::Interleave:
            score = output % std::max<uint32_t>(1u, inputLayout_.count)
                    == source % std::max<uint32_t>(1u, inputLayout_.count)
                ? static_cast<float>(output) : 1000.0f + static_cast<float>(output);
            break;
        case FormatUpscalePlacement::Nearest:
        case FormatUpscalePlacement::Span:
        case FormatUpscalePlacement::TierFill:
        case FormatUpscalePlacement::MidSideSpread:
        case FormatUpscalePlacement::Match:
        default:
            break;
        }
        return score;
    }

    void buildPlacement(std::array<float,
        kFormatUpscaleMaxChannels * kFormatUpscaleMaxChannels>& placement,
        const std::array<bool, kFormatUpscaleMaxChannels>& anchorOutputs,
        const std::array<uint32_t,
            kFormatUpscaleMaxChannels>& anchors) const
    {
        placement.fill(0.0f);

        std::array<float, kFormatUpscaleMaxChannels> tierHeights {};
        std::array<uint32_t, kFormatUpscaleMaxChannels> outputTiers {};
        uint32_t tierCount = 0u;
        for (uint32_t output = 0u; output < outputLayout_.count; ++output) {
            const float height = formatUpscaleSpeakerHeight(
                outputLayout_.speakers[output]);
            uint32_t tier = 0u;
            while (tier < tierCount
                && std::abs(tierHeights[tier] - height)
                    >= kFormatUpscaleTierHeightTolerance)
                ++tier;
            if (tier == tierCount) {
                tierHeights[tier] = height;
                ++tierCount;
            }
            outputTiers[output] = tier;
        }
        const bool tierFill = params_.placement
            == FormatUpscalePlacement::TierFill;
        const bool fillTiers = tierFill && tierCount > 1u;
        // Ordinary recipes must honor Copies exactly. Tier seeding used to
        // raise that count silently whenever every tier had enough speakers,
        // which made Repeat's Outputs/Input control appear unpredictable.
        // Complete height and array coverage now belongs only to Tier Fill.
        const uint32_t repeatMaximum = std::max<uint32_t>(1u,
            outputLayout_.count
                / std::max<uint32_t>(1u, inputLayout_.count));
        const uint32_t effectiveCopies = params_.placement
                == FormatUpscalePlacement::Interleave
            ? std::min(params_.copies, repeatMaximum)
            : params_.copies;
        const uint32_t baseRequested =
            params_.placement == FormatUpscalePlacement::Match
                ? 0u : (effectiveCopies > 0u ? effectiveCopies - 1u : 0u);
        const uint32_t tierRequested = fillTiers ? tierCount - 1u : 0u;
        const uint32_t requested = std::max(baseRequested, tierRequested);
        if (requested == 0u && !tierFill) return;

        // Column load is the first candidate rank, followed by the selected
        // spatial rule. This prevents independently attractive candidates
        // from piling up while an equally eligible output column is unused.
        std::array<uint32_t, kFormatUpscaleMaxChannels> extensionLoads {};
        std::array<uint32_t, kFormatUpscaleMaxChannels> outputLoads {};
        for (uint32_t output = 0u; output < outputLayout_.count; ++output)
            outputLoads[output] = anchorOutputs[output] ? 1u : 0u;
        std::array<bool, kFormatUpscaleMaxChannels> tierAssigned =
            anchorOutputs;
        std::array<uint32_t, kFormatUpscaleMaxChannels> selectedCounts {};
        auto selected = [&](uint32_t source, uint32_t output) {
            return placement[matrixIndex(source, output)] > 0.000001f;
        };
        auto addCandidate = [&](uint32_t source, uint32_t output,
                                bool tierSeed = false) {
            float gain = 1.0f;
            if (params_.placement == FormatUpscalePlacement::Span) {
                const float distance = speakerDistance(
                    inputLayout_.speakers[source],
                    outputLayout_.speakers[output]);
                const float focus = 1.0f
                    + (100.0f - params_.spreadPercent) * 0.045f;
                const float cosine = std::max(0.001f,
                    std::cos(formatUpscaleClamp(distance, 0.0f, 180.0f)
                        * 3.14159265358979323846f / 360.0f));
                gain = std::pow(cosine, focus);
                if (tierSeed) gain = std::max(gain, 0.25f);
            }
            placement[matrixIndex(source, output)] = gain;
            ++selectedCounts[source];
            ++extensionLoads[output];
            ++outputLoads[output];
        };

        // Seed every source onto every elevation tier. Where a tier has room
        // for all sources, destinations remain unique. Tier Fill deliberately
        // permits sharing on sparse tiers and balances that sharing by column
        // occupancy before considering AED distance.
        if (fillTiers) {
            for (uint32_t source = 0u; source < inputLayout_.count; ++source) {
                if (anchors[source] >= outputLayout_.count) continue;
                const uint32_t anchorTier = outputTiers[anchors[source]];
                for (uint32_t tier = 0u;
                     tier < tierCount
                        && selectedCounts[source] < requested; ++tier) {
                    if (tier == anchorTier) continue;
                    uint32_t best = kFormatUpscaleMaxChannels;
                    uint32_t bestLoad = UINT32_MAX;
                    float bestScore = 1000000.0f;
                    for (uint32_t output = 0u; output < outputLayout_.count;
                         ++output) {
                        if (outputTiers[output] != tier
                            || selected(source, output)
                            || (!tierFill && tierAssigned[output])
                            || (tierFill && anchors[source] == output))
                            continue;
                        const uint32_t load = tierFill
                            ? outputLoads[output] : extensionLoads[output];
                        const float score = candidateScore(
                            source, output, selectedCounts[source]);
                        if (load < bestLoad
                            || (load == bestLoad && score < bestScore)) {
                            bestLoad = load;
                            bestScore = score;
                            best = output;
                        }
                    }
                    // Tier coverage is the stronger promise here. If the
                    // preferred side/index is already reserved, use the best
                    // remaining speaker on this tier rather than leave a
                    // source or tier incomplete.
                    if (best < outputLayout_.count) {
                        addCandidate(source, best, true);
                        if (!tierFill) tierAssigned[best] = true;
                    }
                }
            }
        }

        // Add the base copy count in rounds so early input rows cannot consume
        // all of the least-loaded choices before later rows are considered.
        bool progress = true;
        while (progress) {
            progress = false;
            for (uint32_t source = 0u; source < inputLayout_.count; ++source) {
                if (selectedCounts[source] >= requested) continue;
                uint32_t best = kFormatUpscaleMaxChannels;
                uint32_t bestLoad = UINT32_MAX;
                float bestScore = 1000000.0f;
                for (uint32_t output = 0u; output < outputLayout_.count;
                     ++output) {
                    if (selected(source, output)) continue;
                    if (tierFill) {
                        if (anchors[source] == output) continue;
                    } else if (anchorOutputs[output]) {
                        continue;
                    }
                    const float score = candidateScore(
                        source, output, selectedCounts[source]);
                    if (!tierFill && score >= 999.0f) continue;
                    const uint32_t load = tierFill
                        ? outputLoads[output] : extensionLoads[output];
                    if (load < bestLoad
                        || (load == bestLoad && score < bestScore)) {
                        bestLoad = load;
                        bestScore = score;
                        best = output;
                    }
                }
                if (best >= outputLayout_.count) continue;
                addCandidate(source, best);
                progress = true;
            }
        }

        // Tier Fill is the exhaustive alternative: after tier coverage, add
        // only as many routes as needed to touch every otherwise-unused output
        // column. Rows are kept as even as possible, then AED distance decides.
        if (tierFill) {
            for (uint32_t output = 0u; output < outputLayout_.count; ++output) {
                if (outputLoads[output] != 0u) continue;
                uint32_t bestSource = kFormatUpscaleMaxChannels;
                uint32_t bestRowLoad = UINT32_MAX;
                float bestScore = 1000000.0f;
                for (uint32_t source = 0u; source < inputLayout_.count;
                     ++source) {
                    if (anchors[source] == output
                        || selected(source, output)) continue;
                    const float score = candidateScore(
                        source, output, selectedCounts[source]);
                    if (selectedCounts[source] < bestRowLoad
                        || (selectedCounts[source] == bestRowLoad
                            && score < bestScore)) {
                        bestSource = source;
                        bestRowLoad = selectedCounts[source];
                        bestScore = score;
                    }
                }
                if (bestSource < inputLayout_.count)
                    addCandidate(bestSource, output);
            }
        }
    }

    void buildBasis(std::array<float,
        kFormatUpscaleMaxChannels * kFormatUpscaleMaxChannels>& basis) const
    {
        basis.fill(0.0f);
        for (uint32_t lane = 0u; lane < inputLayout_.count; ++lane)
            basis[matrixIndex(lane, lane)] = 1.0f;
    }

    float automaticShapeWeight(float distance, float minimumDistance,
        float maximumDistance) const
    {
        const float distanceRange = maximumDistance - minimumDistance;
        const float position = distanceRange > 0.0001f
            ? (distance - minimumDistance) / distanceRange : 0.0f;
        if (autoRowShape_ == FormatUpscaleRowShape::Center)
            return 1.0f - 0.5f * position;
        if (autoRowShape_ == FormatUpscaleRowShape::Edges)
            return 0.5f + 0.5f * position;
        if (autoRowShape_ == FormatUpscaleRowShape::Taper)
            return std::max(0.25f, std::cos(
                formatUpscaleClamp(distance, 0.0f, 180.0f)
                * 3.14159265358979323846f / 360.0f));
        return 1.0f;
    }

    void applyAutomaticRowShape(uint32_t source)
    {
        if (autoRowShape_ == FormatUpscaleRowShape::Flat
            || source >= inputLayout_.count) return;
        float minimumDistance = 1000000.0f;
        float maximumDistance = 0.0f;
        for (uint32_t output = 0u; output < outputLayout_.count; ++output) {
            const uint32_t index = matrixIndex(source, output);
            if (std::abs(targetAnchor_[index]) < 0.000001f
                && std::abs(targetExtension_[index]) < 0.000001f)
                continue;
            const float distance = speakerDistance(
                inputLayout_.speakers[source],
                outputLayout_.speakers[output]);
            minimumDistance = std::min(minimumDistance, distance);
            maximumDistance = std::max(maximumDistance, distance);
        }
        for (uint32_t output = 0u; output < outputLayout_.count; ++output) {
            const uint32_t index = matrixIndex(source, output);
            if (std::abs(targetAnchor_[index]) < 0.000001f
                && std::abs(targetExtension_[index]) < 0.000001f)
                continue;
            const float distance = speakerDistance(
                inputLayout_.speakers[source],
                outputLayout_.speakers[output]);
            const float weight = automaticShapeWeight(
                distance, minimumDistance, maximumDistance);
            targetAnchor_[index] *= weight;
            targetExtension_[index] *= weight;
        }
    }

    void normalizeInputRows()
    {
        for (uint32_t input = 0u; input < inputLayout_.count; ++input) {
            float power = 0.0f;
            for (uint32_t output = 0u; output < outputLayout_.count;
                 ++output) {
                const uint32_t index = matrixIndex(input, output);
                const float gain = targetAnchor_[index]
                    + targetExtension_[index];
                power += gain * gain;
            }
            if (power < 0.000001f) continue;
            const float scale = 1.0f / std::sqrt(power);
            for (uint32_t output = 0u; output < outputLayout_.count;
                 ++output) {
                const uint32_t index = matrixIndex(input, output);
                targetAnchor_[index] *= scale;
                targetExtension_[index] *= scale;
            }
        }
    }

    void applyNormalization()
    {
        if (normalization_ == FormatUpscaleNormalization::Row
            || normalization_ == FormatUpscaleNormalization::DualLimit)
            normalizeInputRows();
        if (normalization_ == FormatUpscaleNormalization::Column)
            normalizeOutputColumns(false);
        else if (normalization_ == FormatUpscaleNormalization::DualLimit)
            normalizeOutputColumns(true);
    }

    void rebuildTargets()
    {
        inputLayout_ = resolveLayout(params_.inputLayout, true);
        outputLayout_ = resolveLayout(params_.outputLayout, false);
        targetAnchor_.fill(0.0f);
        targetExtension_.fill(0.0f);

        if (manualRoutesActive_) {
            for (uint32_t input = 0u; input < inputLayout_.count; ++input) {
                for (uint32_t output = 0u; output < outputLayout_.count;
                     ++output) {
                    const uint32_t index = matrixIndex(input, output);
                    if (std::abs(manualWeights_[index]) > 0.000001f)
                        targetExtension_[index] = manualWeights_[index];
                }
            }
            applyNormalization();
            rebuildActiveRoutes();
            return;
        }

        std::array<bool, kFormatUpscaleMaxChannels> anchorOutputs {};
        std::array<uint32_t, kFormatUpscaleMaxChannels> anchors {};
        anchors.fill(kFormatUpscaleMaxChannels);
        for (uint32_t source = 0u; source < inputLayout_.count; ++source) {
            // Repeat is an ordinal channel-cycle operation: I1 starts at O1,
            // I2 at O2, and later rounds advance by the input count. Spatial
            // role matching here could move an anchor out of its cycle and
            // make a requested round impossible.
            const uint32_t output = params_.placement
                    == FormatUpscalePlacement::Interleave
                && source < outputLayout_.count
                ? source : findAnchor(source, anchorOutputs);
            if (output < outputLayout_.count) {
                anchors[source] = output;
                anchorOutputs[output] = true;
            }
        }

        std::array<float,
            kFormatUpscaleMaxChannels * kFormatUpscaleMaxChannels> placement {};
        std::array<float,
            kFormatUpscaleMaxChannels * kFormatUpscaleMaxChannels> basis {};
        buildPlacement(placement, anchorOutputs, anchors);
        buildBasis(basis);

        std::array<float,
            kFormatUpscaleMaxChannels * kFormatUpscaleMaxChannels> extensionRaw {};
        for (uint32_t lane = 0u; lane < inputLayout_.count; ++lane) {
            for (uint32_t output = 0u; output < outputLayout_.count; ++output) {
                const float route = placement[matrixIndex(lane, output)];
                if (std::abs(route) < 0.000001f) continue;
                for (uint32_t source = 0u; source < inputLayout_.count; ++source)
                    extensionRaw[matrixIndex(source, output)] += route
                        * basis[matrixIndex(source, lane)];
            }
        }

        const float amount = params_.amountPercent * 0.01f;
        for (uint32_t source = 0u; source < inputLayout_.count; ++source) {
            float extensionPower = 0.0f;
            for (uint32_t output = 0u; output < outputLayout_.count; ++output) {
                const float gain = extensionRaw[matrixIndex(source, output)];
                extensionPower += gain * gain;
            }
            const bool hasExtension = extensionPower > 0.000001f;
            float anchorScale = 1.0f;
            float extensionScale = amount;
            if (params_.origin == FormatUpscaleOrigin::Remap && hasExtension)
                anchorScale = 1.0f - amount;
            if (anchors[source] < outputLayout_.count)
                targetAnchor_[matrixIndex(source, anchors[source])] = anchorScale;
            for (uint32_t output = 0u; output < outputLayout_.count; ++output)
                targetExtension_[matrixIndex(source, output)] =
                    extensionRaw[matrixIndex(source, output)] * extensionScale;

            applyAutomaticRowShape(source);

            if (params_.origin != FormatUpscaleOrigin::Keep) {
                float power = 0.0f;
                for (uint32_t output = 0u; output < outputLayout_.count;
                     ++output) {
                    const uint32_t index = matrixIndex(source, output);
                    power += targetAnchor_[index] * targetAnchor_[index]
                        + targetExtension_[index] * targetExtension_[index];
                }
                if (power > 0.000001f) {
                    const float scale = 1.0f / std::sqrt(power);
                    for (uint32_t output = 0u; output < outputLayout_.count;
                         ++output) {
                        const uint32_t index = matrixIndex(source, output);
                        targetAnchor_[index] *= scale;
                        targetExtension_[index] *= scale;
                    }
                }
            }
        }
        if (normalization_ == FormatUpscaleNormalization::Column)
            normalizeOutputColumns(false);
        else if (normalization_ == FormatUpscaleNormalization::DualLimit)
            normalizeOutputColumns(true);
        rebuildActiveRoutes();
    }

    void normalizeOutputColumns(bool limitOnly)
    {
        for (uint32_t output = 0u; output < outputLayout_.count; ++output) {
            float power = 0.0f;
            for (uint32_t input = 0u; input < inputLayout_.count; ++input) {
                const uint32_t index = matrixIndex(input, output);
                const float gain = targetAnchor_[index]
                    + targetExtension_[index];
                power += gain * gain;
            }
            if (power < 0.000001f || (limitOnly && power <= 1.0f)) continue;
            const float scale = 1.0f / std::sqrt(power);
            for (uint32_t input = 0u; input < inputLayout_.count; ++input) {
                const uint32_t index = matrixIndex(input, output);
                targetAnchor_[index] *= scale;
                targetExtension_[index] *= scale;
            }
        }
    }

    void rebuildActiveRoutes()
    {
        activeRouteCount_ = 0u;
        for (uint32_t index = 0u;
             index < kFormatUpscaleMaxChannels * kFormatUpscaleMaxChannels;
             ++index) {
            if (std::abs(targetAnchor_[index]) < 0.000001f
                && std::abs(targetExtension_[index]) < 0.000001f
                && std::abs(currentAnchor_[index]) < 0.000001f
                && std::abs(currentExtension_[index]) < 0.000001f)
                continue;
            activeRouteIndices_[activeRouteCount_++] =
                static_cast<uint16_t>(index);
        }
    }

    float smoothingCoefficient() const
    {
        return 1.0f - std::exp(-1000.0f
            / (std::max(1.0f, params_.smoothingMs)
                * static_cast<float>(sampleRate_)));
    }

    static float channelHash(uint32_t channel)
    {
        uint32_t value = channel + 1u;
        value ^= value << 13u;
        value ^= value >> 17u;
        value ^= value << 5u;
        return static_cast<float>(value & 0xffffu) / 65535.0f;
    }

    float processExtension(uint32_t output, float input, float smooth)
    {
        if (delayLength_ == 0u || delayBuffer_.empty()) return input;
        const float delayTarget = params_.delayMs * 0.001f
            * static_cast<float>(sampleRate_)
            * (0.35f + channelHash(output) * 0.65f);
        currentDelaySamples_[output] +=
            (delayTarget - currentDelaySamples_[output]) * smooth;
        const float delay = formatUpscaleClamp(currentDelaySamples_[output],
            0.0f, static_cast<float>(delayLength_ - 2u));
        const uint32_t whole = static_cast<uint32_t>(delay);
        const float fraction = delay - static_cast<float>(whole);
        const uint32_t readA = (delayWriteIndex_ + delayLength_ - whole)
            % delayLength_;
        const uint32_t readB = (readA + delayLength_ - 1u) % delayLength_;
        const size_t base = static_cast<size_t>(output) * delayLength_;
        delayBuffer_[base + delayWriteIndex_] = input;
        float delayed = delayBuffer_[base + readA] * (1.0f - fraction)
            + delayBuffer_[base + readB] * fraction;

        const float decor = params_.decorrelationPercent * 0.01f;
        if (decor <= 0.00001f) return delayed;
        const float coefficient = decor
            * (0.42f + channelHash(output + 97u) * 0.28f);
        const float result = -coefficient * delayed + allpassState_[output];
        allpassState_[output] = delayed + coefficient * result;
        return result;
    }

    FormatUpscaleParams params_ {};
    FormatUpscaleRowShape autoRowShape_ = FormatUpscaleRowShape::Flat;
    FormatUpscaleNormalization normalization_ =
        FormatUpscaleNormalization::Row;
    LayoutPanner layoutPolicy_ {};
    FormatUpscaleLayoutData customInputLayout_ {};
    FormatUpscaleLayoutData customOutputLayout_ {};
    FormatUpscaleLayoutData inputLayout_ {};
    FormatUpscaleLayoutData outputLayout_ {};
    std::array<uint8_t,
        kFormatUpscaleMaxChannels * kFormatUpscaleMaxChannels>
        manualRoutes_ {};
    std::array<float,
        kFormatUpscaleMaxChannels * kFormatUpscaleMaxChannels>
        manualWeights_ {};
    bool manualRoutesActive_ = false;
    double sampleRate_ = 48000.0;
    std::array<float,
        kFormatUpscaleMaxChannels * kFormatUpscaleMaxChannels> targetAnchor_ {};
    std::array<float,
        kFormatUpscaleMaxChannels * kFormatUpscaleMaxChannels> targetExtension_ {};
    std::array<float,
        kFormatUpscaleMaxChannels * kFormatUpscaleMaxChannels> currentAnchor_ {};
    std::array<float,
        kFormatUpscaleMaxChannels * kFormatUpscaleMaxChannels> currentExtension_ {};
    std::array<uint16_t,
        kFormatUpscaleMaxChannels * kFormatUpscaleMaxChannels>
        activeRouteIndices_ {};
    uint32_t activeRouteCount_ = 0u;
    std::vector<float> delayBuffer_;
    std::array<float, kFormatUpscaleMaxChannels> currentDelaySamples_ {};
    std::array<float, kFormatUpscaleMaxChannels> allpassState_ {};
    uint32_t delayLength_ = 0u;
    uint32_t delayWriteIndex_ = 0u;
};

} // namespace s3g
