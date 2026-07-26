#pragma once

#include "s3g_math.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace s3g {

constexpr uint32_t kParameterSurfaceLegacyMaxCells = 8u;
constexpr uint32_t kParameterSurfaceMaxCells = 24u;
constexpr uint32_t kParameterSurfaceStateVersion = 3u;
constexpr float kParameterSurfaceDefaultGlideMs = 160.0f;

enum class ParameterSurfaceCurve : uint32_t {
    Soft = 0u,
    Linear = 1u,
    Smooth = 2u,
    Tight = 3u,
};

constexpr uint32_t kParameterSurfaceCurveCount = 4u;

inline const char* parameterSurfaceCurveName(ParameterSurfaceCurve curve)
{
    switch (curve) {
    case ParameterSurfaceCurve::Soft: return "SOFT";
    case ParameterSurfaceCurve::Smooth: return "SMOOTH";
    case ParameterSurfaceCurve::Tight: return "TIGHT";
    case ParameterSurfaceCurve::Linear:
    default: return "LINEAR";
    }
}

template <typename Params>
struct ParameterSurfaceCell {
    uint32_t active = 0u;
    int32_t presetIndex = -1;
    float x = 0.5f;
    float y = 0.5f;
    char name[32] {};
    Params params {};
};

template <typename Params>
struct ParameterSurfaceState {
    uint32_t version = kParameterSurfaceStateVersion;
    uint32_t enabled = 0u;
    uint32_t cellCount = 0u;
    float focus = 2.0f;
    ParameterSurfaceCurve curve = ParameterSurfaceCurve::Linear;
    float glideMs = kParameterSurfaceDefaultGlideMs;
    std::array<ParameterSurfaceCell<Params>, kParameterSurfaceMaxCells> cells {};
};

// Version 2 expanded the original surface to 24 cells. Keep its exact binary
// shape so plugin state readers can add interpolation settings without losing
// existing cell assignments.
template <typename Params>
struct ParameterSurfaceStateV2 {
    uint32_t version = 2u;
    uint32_t enabled = 0u;
    uint32_t cellCount = 0u;
    float focus = 2.0f;
    std::array<ParameterSurfaceCell<Params>, kParameterSurfaceMaxCells> cells {};
};

// Version 1 was the initial eight-cell layout. Keep its exact binary shape so
// enclosing plugin state formats can migrate sessions written by that release.
template <typename Params>
struct ParameterSurfaceStateV1 {
    uint32_t version = 1u;
    uint32_t enabled = 0u;
    uint32_t cellCount = 0u;
    float focus = 2.0f;
    std::array<ParameterSurfaceCell<Params>,
        kParameterSurfaceLegacyMaxCells> cells {};
};

struct ParameterSurfaceWeights {
    std::array<float, kParameterSurfaceMaxCells> values {};
    float sum = 0.0f;
    uint32_t nearest = 0u;
    uint32_t activeCount = 0u;
    bool exact = false;
};

inline float parameterSurfaceHalton(uint32_t index, uint32_t base)
{
    float result = 0.0f;
    float fraction = 1.0f;
    while (index > 0u) {
        fraction /= static_cast<float>(base);
        result += fraction * static_cast<float>(index % base);
        index /= base;
    }
    return result;
}

inline std::array<float, 2> parameterSurfaceDefaultPosition(uint32_t index)
{
    static constexpr std::array<std::array<float, 2>,
        kParameterSurfaceLegacyMaxCells> legacyPositions {{
        {{ 0.18f, 0.18f }}, {{ 0.82f, 0.18f }},
        {{ 0.82f, 0.82f }}, {{ 0.18f, 0.82f }},
        {{ 0.50f, 0.18f }}, {{ 0.82f, 0.50f }},
        {{ 0.50f, 0.82f }}, {{ 0.18f, 0.50f }},
    }};
    if (index < legacyPositions.size()) return legacyPositions[index];
    // A low-discrepancy sequence avoids piling later cells onto a rigid grid.
    // Offset past the preserved first-generation sites and leave a small edge
    // margin so new cells are immediately easy to select and drag.
    const uint32_t sequence = index + 9u;
    return {{
        0.08f + 0.84f * parameterSurfaceHalton(sequence, 2u),
        0.08f + 0.84f * parameterSurfaceHalton(sequence, 3u),
    }};
}

template <typename Params>
inline void sanitizeParameterSurface(ParameterSurfaceState<Params>& surface)
{
    surface.version = kParameterSurfaceStateVersion;
    surface.enabled = surface.enabled ? 1u : 0u;
    surface.cellCount = std::min<uint32_t>(
        surface.cellCount, kParameterSurfaceMaxCells);
    surface.focus = clamp(
        std::isfinite(surface.focus) ? surface.focus : 2.0f,
        0.25f, 8.0f);
    surface.curve = static_cast<ParameterSurfaceCurve>(
        std::min<uint32_t>(static_cast<uint32_t>(surface.curve),
            kParameterSurfaceCurveCount - 1u));
    surface.glideMs = clamp(
        std::isfinite(surface.glideMs) ? surface.glideMs
            : kParameterSurfaceDefaultGlideMs,
        0.0f, 2000.0f);
    uint32_t packed = 0u;
    for (uint32_t index = 0u; index < kParameterSurfaceMaxCells; ++index) {
        auto cell = surface.cells[index];
        if (index >= surface.cellCount || cell.active == 0u) continue;
        cell.active = 1u;
        cell.x = clamp(std::isfinite(cell.x) ? cell.x : 0.5f, 0.0f, 1.0f);
        cell.y = clamp(std::isfinite(cell.y) ? cell.y : 0.5f, 0.0f, 1.0f);
        cell.name[sizeof(cell.name) - 1u] = '\0';
        surface.cells[packed++] = cell;
    }
    surface.cellCount = packed;
    for (uint32_t index = packed;
        index < kParameterSurfaceMaxCells; ++index) {
        surface.cells[index] = {};
    }
    if (surface.cellCount < 2u) surface.enabled = 0u;
}

template <typename Params>
inline bool addParameterSurfaceCell(
    ParameterSurfaceState<Params>& surface, const Params& params,
    int32_t presetIndex, const char* name)
{
    sanitizeParameterSurface(surface);
    if (surface.cellCount >= kParameterSurfaceMaxCells) return false;
    const uint32_t index = surface.cellCount++;
    auto& cell = surface.cells[index];
    cell = {};
    cell.active = 1u;
    cell.presetIndex = presetIndex;
    const auto position = parameterSurfaceDefaultPosition(index);
    cell.x = position[0];
    cell.y = position[1];
    cell.params = params;
    std::snprintf(cell.name, sizeof(cell.name), "%s",
        name && name[0] ? name : "CELL");
    return true;
}

template <typename Params>
inline ParameterSurfaceState<Params> migrateParameterSurfaceV1(
    const ParameterSurfaceStateV1<Params>& legacy)
{
    ParameterSurfaceState<Params> surface {};
    surface.enabled = legacy.enabled;
    surface.cellCount = std::min<uint32_t>(legacy.cellCount,
        kParameterSurfaceLegacyMaxCells);
    surface.focus = legacy.focus;
    surface.curve = ParameterSurfaceCurve::Linear;
    surface.glideMs = kParameterSurfaceDefaultGlideMs;
    for (uint32_t index = 0u; index < surface.cellCount; ++index) {
        surface.cells[index] = legacy.cells[index];
    }
    sanitizeParameterSurface(surface);
    return surface;
}

template <typename Params>
inline ParameterSurfaceState<Params> migrateParameterSurfaceV2(
    const ParameterSurfaceStateV2<Params>& legacy)
{
    ParameterSurfaceState<Params> surface {};
    surface.enabled = legacy.enabled;
    surface.cellCount = std::min<uint32_t>(legacy.cellCount,
        kParameterSurfaceMaxCells);
    surface.focus = legacy.focus;
    surface.curve = ParameterSurfaceCurve::Linear;
    surface.glideMs = kParameterSurfaceDefaultGlideMs;
    for (uint32_t index = 0u; index < surface.cellCount; ++index) {
        surface.cells[index] = legacy.cells[index];
    }
    sanitizeParameterSurface(surface);
    return surface;
}

inline float parameterSurfaceGlideCoefficient(
    float glideMs, float deltaSeconds)
{
    if (!std::isfinite(glideMs) || glideMs <= 0.0f) return 1.0f;
    if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.0f) return 0.0f;
    // The displayed time is the approximately 99% settling time, which makes
    // the setting easier to compare to an envelope or automation glide.
    constexpr float kNinetyNinePercent = 4.605170186f;
    const float seconds = std::max(0.001f, glideMs * 0.001f);
    return clamp(1.0f - std::exp(
        -kNinetyNinePercent * deltaSeconds / seconds), 0.0f, 1.0f);
}

inline float parameterSurfaceSteppedGlide(float glideMs, int direction)
{
    static constexpr std::array<float, 10u> values {{
        0.0f, 40.0f, 80.0f, 160.0f, 240.0f,
        400.0f, 640.0f, 1000.0f, 1500.0f, 2000.0f,
    }};
    glideMs = clamp(std::isfinite(glideMs) ? glideMs :
        kParameterSurfaceDefaultGlideMs, 0.0f, 2000.0f);
    if (direction < 0) {
        for (auto it = values.rbegin(); it != values.rend(); ++it) {
            if (*it < glideMs - 0.5f) return *it;
        }
        return values.front();
    }
    for (const float value : values) {
        if (value > glideMs + 0.5f) return value;
    }
    return values.back();
}

template <typename Params>
inline bool removeParameterSurfaceCell(
    ParameterSurfaceState<Params>& surface, uint32_t index)
{
    sanitizeParameterSurface(surface);
    if (index >= surface.cellCount) return false;
    for (uint32_t item = index + 1u;
        item < surface.cellCount; ++item) {
        surface.cells[item - 1u] = surface.cells[item];
    }
    surface.cells[--surface.cellCount] = {};
    if (surface.cellCount < 2u) surface.enabled = 0u;
    return true;
}

template <typename Params>
inline ParameterSurfaceWeights parameterSurfaceWeights(
    const ParameterSurfaceState<Params>& surface,
    float cursorX, float cursorY)
{
    ParameterSurfaceWeights result {};
    cursorX = clamp(std::isfinite(cursorX) ? cursorX : 0.5f, 0.0f, 1.0f);
    cursorY = clamp(std::isfinite(cursorY) ? cursorY : 0.5f, 0.0f, 1.0f);
    const uint32_t count = std::min<uint32_t>(
        surface.cellCount, kParameterSurfaceMaxCells);
    float nearestDistance = 1.0e30f;
    for (uint32_t index = 0u; index < count; ++index) {
        const auto& cell = surface.cells[index];
        if (!cell.active) continue;
        ++result.activeCount;
        const float dx = cursorX - cell.x;
        const float dy = cursorY - cell.y;
        const float distanceSquared = dx * dx + dy * dy;
        if (distanceSquared < nearestDistance) {
            nearestDistance = distanceSquared;
            result.nearest = index;
        }
        if (distanceSquared < 1.0e-10f) {
            result.values.fill(0.0f);
            result.values[index] = 1.0f;
            result.sum = 1.0f;
            result.nearest = index;
            result.exact = true;
            return result;
        }
        const float distance = std::sqrt(std::max(1.0e-10f, distanceSquared));
        const float weight = 1.0f / std::pow(distance,
            clamp(surface.focus, 0.25f, 8.0f));
        result.values[index] = weight;
        result.sum += weight;
    }
    if (result.sum <= 0.0f
        || surface.curve == ParameterSurfaceCurve::Linear) {
        return result;
    }
    float shapedSum = 0.0f;
    for (uint32_t index = 0u; index < count; ++index) {
        if (result.values[index] <= 0.0f) continue;
        const float normalized = result.values[index] / result.sum;
        float shaped = normalized;
        switch (surface.curve) {
        case ParameterSurfaceCurve::Soft:
            shaped = std::sqrt(normalized);
            break;
        case ParameterSurfaceCurve::Smooth:
            shaped = normalized * normalized
                * (3.0f - 2.0f * normalized);
            break;
        case ParameterSurfaceCurve::Tight:
            shaped = normalized * normalized;
            break;
        case ParameterSurfaceCurve::Linear:
        default:
            break;
        }
        result.values[index] = shaped;
        shapedSum += shaped;
    }
    result.sum = shapedSum;
    return result;
}

template <typename Params, typename Getter>
inline float parameterSurfaceBlend(
    const ParameterSurfaceState<Params>& surface,
    const ParameterSurfaceWeights& weights, Getter getter,
    float fallback)
{
    if (weights.activeCount == 0u || weights.sum <= 0.0f) return fallback;
    float total = 0.0f;
    for (uint32_t index = 0u;
        index < std::min<uint32_t>(surface.cellCount,
            kParameterSurfaceMaxCells); ++index) {
        if (!surface.cells[index].active || weights.values[index] <= 0.0f) continue;
        total += getter(surface.cells[index].params) * weights.values[index];
    }
    return total / weights.sum;
}

template <typename Params, typename Getter>
inline float parameterSurfaceBlendAngleDegrees(
    const ParameterSurfaceState<Params>& surface,
    const ParameterSurfaceWeights& weights, Getter getter,
    float fallback)
{
    if (weights.activeCount == 0u || weights.sum <= 0.0f) return fallback;
    float sine = 0.0f;
    float cosine = 0.0f;
    for (uint32_t index = 0u;
        index < std::min<uint32_t>(surface.cellCount,
            kParameterSurfaceMaxCells); ++index) {
        if (!surface.cells[index].active || weights.values[index] <= 0.0f) continue;
        const float radians = getter(surface.cells[index].params) * kPi / 180.0f;
        sine += std::sin(radians) * weights.values[index];
        cosine += std::cos(radians) * weights.values[index];
    }
    if (std::fabs(sine) + std::fabs(cosine) < 1.0e-8f) return fallback;
    return std::atan2(sine, cosine) * 180.0f / kPi;
}

template <typename Params>
inline const Params& parameterSurfaceNearestParams(
    const ParameterSurfaceState<Params>& surface,
    const ParameterSurfaceWeights& weights, const Params& fallback)
{
    if (weights.activeCount == 0u
        || weights.nearest >= surface.cellCount
        || !surface.cells[weights.nearest].active) {
        return fallback;
    }
    return surface.cells[weights.nearest].params;
}

} // namespace s3g
