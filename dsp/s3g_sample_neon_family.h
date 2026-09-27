#pragma once
#include "s3g_sample_windows.h"
#include <array>
#include <cstddef>

namespace s3g::sample {
enum class NeonStackShape : unsigned { Manual, RampUp, RampDown, Triangle, Sine, Square, Wander };
constexpr std::array<const char*, 7> kNeonStackShapeNames {{
    "MANUAL", "RAMP UP", "RAMP DOWN", "TRIANGLE", "SINE", "SQUARE", "WANDER"
}};
inline unsigned neonStackShapeMinimum(NeonStackShape shape) noexcept {
    return shape == NeonStackShape::Sine ? 9u : shape == NeonStackShape::Square ? 4u
        : shape == NeonStackShape::Triangle || shape == NeonStackShape::Wander ? 3u : 2u;
}
inline float neonStackShapeTime(NeonStackShape shape, unsigned count, unsigned point) noexcept {
    if (shape == NeonStackShape::Triangle || shape == NeonStackShape::Sine) {
        const unsigned middle = count / 2;
        return point <= middle ? .5f * point / middle : .5f + .5f * (point - middle) / (count - 1 - middle);
    }
    if (shape == NeonStackShape::Square) {
        const unsigned middle = count / 2;
        return point < middle ? .4999f * point / (middle - 1) : .5f + .5f * (point - middle) / (count - 1 - middle);
    }
    return static_cast<float>(point) / (count - 1);
}
inline float neonStackShapeValue(NeonStackShape shape, float time, unsigned pad = 0) noexcept {
    switch (shape) {
    case NeonStackShape::RampDown: return 1 - time;
    case NeonStackShape::Triangle: return time <= .5f ? time * 2 : 2 - time * 2;
    case NeonStackShape::Sine: return static_cast<float>(.5 - .5 * std::cos(time * 6.283185307179586));
    case NeonStackShape::Square: return time < .5f ? 0 : 1;
    case NeonStackShape::Wander: return static_cast<float>(std::clamp(.5
        + std::sin((time + pad * .071) * 6.28318530718) * .31
        + std::sin((time * 2.71 + pad * .113) * 6.28318530718) * .19, 0.0, 1.0));
    default: return time;
    }
}
inline NeonStackShape neonLegacyStackShape(unsigned path) noexcept {
    constexpr std::array<NeonStackShape, 4> shapes {{NeonStackShape::RampUp,
        NeonStackShape::RampDown, NeonStackShape::Triangle, NeonStackShape::Wander}};
    return shapes[std::min(3u, path)];
}
// Append-only sound controls. The bridge stores this bounded, allocation-free
// array per pad; all coordinates are normalized, independent of stack length.
enum class NeonFamily : unsigned {
    StackShape, StackJump, StackOffset, StackCurve, StackAdvance, PathCount,
    MotionSound, PacketRate, PacketDuty, MotorRate, MotorSymmetry, MotorShape,
    MotionLocus, MotionField, MotionJitter, MotionWindow, MotionModel,
    EventRate, EventRepeats, EventStep, EventPitch, EventLevel, EventCurve,
    MotionTrajectory, MotionTravel,
    GrainSource, GrainProcess, GrainAmount, GrainRegions, GrainWindow,
    GrainSkew, GrainSizeVariation, GrainLevelVariation, GrainScatter,
    GrainBias, GrainPitch, GrainTimeSync, GrainSizeScale, GrainDensityScale,
    PathTime, // 32 time coordinates, then 32 layer coordinates
    PathValue = PathTime + 32u,
    LanePosition = PathValue + 32u, LaneAuto, LaneRate, LaneSlew, LaneJoin,
    Count
};
constexpr unsigned neonFamilyIndex(NeonFamily key) noexcept { return static_cast<unsigned>(key); }
constexpr unsigned kNeonFamilyCount = neonFamilyIndex(NeonFamily::Count);
constexpr unsigned kNeonFamilyV17Count = neonFamilyIndex(NeonFamily::LanePosition);
struct NeonFamilyDef { float minimum, maximum, initial; bool stepped = false; };
inline NeonFamilyDef neonFamilyDef(unsigned i) noexcept
{
    using F = NeonFamily;
    if (i >= neonFamilyIndex(F::PathValue) && i < kNeonFamilyV17Count) return {0, 1, neonStackShapeValue(NeonStackShape::Triangle,
        neonStackShapeTime(NeonStackShape::Triangle, 32, i - neonFamilyIndex(F::PathValue)))};
    if (i >= neonFamilyIndex(F::PathTime) && i < neonFamilyIndex(F::PathValue)) return {0, 1, neonStackShapeTime(NeonStackShape::Triangle, 32, i - neonFamilyIndex(F::PathTime))};
    switch (static_cast<F>(i)) {
    case F::LaneAuto: return {0, 1, 0, true};
    case F::LaneRate: return {.25f, 4, 1};
    case F::LaneSlew: return {.001f, .1f, .01f};
    case F::LaneJoin: return {0, .25f, .02f};
    case F::PathCount: return {2, 32, 32, true};
    case F::StackShape: return {0, 6, 3, true};
    case F::StackJump: case F::StackAdvance: return {0, 1, 0, true};
    case F::StackCurve: case F::GrainSkew: case F::EventCurve: return {-1, 1, 0};
    case F::MotionSound: return {0, 2, 0, true};
    case F::PacketRate: return {.1f, 160, 12};
    case F::PacketDuty: return {.05f, 1, .65f};
    case F::MotorRate: return {.05f, 20, 1};
    case F::MotorSymmetry: return {.05f, .95f, .5f};
    case F::MotorShape: return {0, 3, 1, true};
    case F::MotionLocus: return {0, 1, .5f};
    case F::MotionField: return {0, 1, 1};
    case F::MotionWindow: return {5, 500, 40};
    case F::MotionModel: return {0, 4, 0, true};
    case F::EventRate: return {.1f, 80, 4};
    case F::EventRepeats: return {1, 16, 2, true};
    case F::EventStep: return {-.5f, .5f, .05f};
    case F::EventPitch: return {0, 24, 0};
    case F::MotionTrajectory: return {0, 4, 0, true};
    case F::MotionTravel: return {0, 1, .35f};
    case F::GrainSource: return {0, 3, 0, true}; // Freeze (legacy), Scan, Cloud, Slice
    case F::GrainProcess: return {0, 4, 0, true}; // Ordinary, Sorter, Stutter, Shrink, Doublets
    case F::GrainAmount: return {0, 1, .5f};
    case F::GrainRegions: return {2, 32, 16, true};
    case F::GrainWindow: return {0, 5, 0, true}; // Legacy ADSR, then shared Grains windows
    case F::GrainBias: return {0, 2, 1, true};
    case F::GrainPitch: return {-48, 48, 0};
    case F::GrainTimeSync: return {0, 1, 1, true};
    case F::GrainSizeScale: return {1, 8, 1};
    case F::GrainDensityScale: return {.1f, 2, 1};
    default: return {0, 1, 0};
    }
}
struct NeonFamilySettings {
    std::array<float, kNeonFamilyCount> values {};
    NeonFamilySettings() noexcept {
        for (unsigned i = 0; i < values.size(); ++i) values[i] = neonFamilyDef(i).initial;
    }
    float operator[](NeonFamily key) const noexcept { return values[neonFamilyIndex(key)]; }
    float& operator[](NeonFamily key) noexcept { return values[neonFamilyIndex(key)]; }
    bool valid() const noexcept {
        for (unsigned i = 0; i < values.size(); ++i) {
            const auto d = neonFamilyDef(i); const float v = values[i];
            if (!std::isfinite(v) || v < d.minimum || v > d.maximum || (d.stepped && v != std::round(v))) return false;
        }
        const unsigned n = static_cast<unsigned>((*this)[NeonFamily::PathCount]);
        if (n < neonStackShapeMinimum(static_cast<NeonStackShape>((*this)[NeonFamily::StackShape]))) return false;
        const auto t = neonFamilyIndex(NeonFamily::PathTime);
        if (values[t] != 0 || values[t + n - 1] != 1) return false;
        for (unsigned i = 1; i < n; ++i) if (values[t + i] <= values[t + i - 1]) return false;
        return true;
    }
};
// Generate source points once on the control thread. There is no separate
// analytic preset engine: every shape renders through neonStackPath below.
inline void neonSetStackShape(NeonFamilySettings& f, NeonStackShape shape, unsigned count, unsigned pad = 0) noexcept {
    f[NeonFamily::StackShape] = static_cast<float>(shape);
    if (shape == NeonStackShape::Manual) return; // Unlock the current curve intact.
    count = std::clamp(count, neonStackShapeMinimum(shape), 32u);
    f[NeonFamily::PathCount] = static_cast<float>(count);
    f[NeonFamily::StackCurve] = 0;
    const auto t = neonFamilyIndex(NeonFamily::PathTime), v = neonFamilyIndex(NeonFamily::PathValue);
    for (unsigned i = 0; i < 32; ++i) {
        f.values[t + i] = i < count ? neonStackShapeTime(shape, count, i) : neonFamilyDef(t + i).initial;
        f.values[v + i] = i < count ? neonStackShapeValue(shape, f.values[t + i], pad) : neonFamilyDef(v + i).initial;
    }
}
inline double neonUnitPhase(double phase) noexcept { return phase - std::floor(phase); }
inline double neonStackPath(const NeonFamilySettings& f, double phase) noexcept
{
    const unsigned n = std::clamp<unsigned>(static_cast<unsigned>(f[NeonFamily::PathCount]), 2, 32);
    const auto t = neonFamilyIndex(NeonFamily::PathTime), v = neonFamilyIndex(NeonFamily::PathValue);
    phase = neonUnitPhase(phase + f[NeonFamily::StackOffset]);
    for (unsigned i = 1; i < n; ++i) {
        const double end = i + 1 == n ? 1.0 : f.values[t + i];
        if (phase <= end || i + 1 == n) {
            const double begin = i == 1 ? 0.0 : f.values[t + i - 1];
            double mix = std::clamp((phase - begin) / std::max(.00001, end - begin), 0.0, 1.0);
            const float curve = f[NeonFamily::StackCurve];
            if (curve != 0) mix = std::pow(mix, std::pow(4.0, curve));
            return std::clamp(f.values[v + i - 1] + mix * (f.values[v + i] - f.values[v + i - 1]), 0.0, 1.0);
        }
    }
    return 0;
}
inline float neonMotionArticulation(const NeonFamilySettings& f, double seconds) noexcept
{
    if (f[NeonFamily::MotionSound] == 0) return 1;
    const double rate = f[NeonFamily::PacketRate];
    const double phase = neonUnitPhase(seconds * rate);
    const double duty = f[NeonFamily::PacketDuty];
    // A short, rate-independent join at both edges avoids hard gate clicks.
    const double edge = std::min(duty * .25, rate * .002);
    float value = static_cast<float>(std::clamp(std::min(phase, duty - phase) / std::max(.00001, edge), 0.0, 1.0));
    if (f[NeonFamily::MotionSound] == 2)
        value *= motorEnvelopeLevel(static_cast<float>(neonUnitPhase(seconds * f[NeonFamily::MotorRate])),
            f[NeonFamily::MotorSymmetry], static_cast<MotorEnvelopeShape>(f[NeonFamily::MotorShape]));
    return value;
}
}
