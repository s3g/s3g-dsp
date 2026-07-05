#pragma once

#include "s3g_math.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace s3g {

constexpr uint32_t kTopologyShapeCount = 12;
constexpr uint32_t kTopologyMotionModeCount = 18;
constexpr uint32_t kTopologyVariantCount = 16;

inline const char* topologyShapeName(uint32_t shape)
{
    static constexpr const char* kNames[] = {
        "AED", "SHEAR", "FOLD", "VORTEX", "PINCH", "RUPTURE", "SCATTER", "MIRROR", "WAVE", "LINE", "PLANE", "FORSY"
    };
    return kNames[std::min<uint32_t>(shape, kTopologyShapeCount - 1u)];
}

inline const char* topologyMotionModeName(uint32_t mode)
{
    static constexpr const char* kNames[] = {
        "OFF", "FREE", "DRIFT", "PULSE", "ORBIT", "FOLD",
        "WEAVE", "GRID", "TRACE", "HOVER", "LEAP", "FIELD",
        "PAIR", "FLOW", "GROUP", "MARCH", "PATH", "SCAT"
    };
    return kNames[std::min<uint32_t>(mode, kTopologyMotionModeCount - 1u)];
}

inline const char* topologyVariantName(uint32_t variant)
{
    static constexpr const char* kNames[] = {
        "PRI", "ALT", "WIDE", "FOLD", "CAN", "SUSP", "BURST", "DRFT",
        "RIB", "GATE", "MIR", "SURG", "TETH", "VORT", "YLD", "STIL"
    };
    return kNames[std::min<uint32_t>(variant, kTopologyVariantCount - 1u)];
}

inline double laneNoise(uint32_t lane)
{
    uint32_t x = lane + 1u;
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return (static_cast<double>(x & 0xffffu) / 32767.5) - 1.0;
}

inline std::array<double, 3> baseTopologyPoint(uint32_t lane, uint32_t count)
{
    count = std::max<uint32_t>(1, count);
    lane = std::min<uint32_t>(lane, count - 1u);
    if (count == 1) {
        return { 0.0, 0.0, 0.0 };
    }
    if (count == 2) {
        return { lane == 0 ? -1.0 : 1.0, 0.0, 0.0 };
    }
    if (count == 4) {
        constexpr double k = 0.57735026919;
        const double coords[4][3] = {
            { k, k, k },
            { -k, -k, k },
            { -k, k, -k },
            { k, -k, -k }
        };
        return { coords[lane][0], coords[lane][1], coords[lane][2] };
    }
    if (count == 6) {
        const double coords[6][3] = {
            { 1.0, 0.0, 0.0 },
            { -1.0, 0.0, 0.0 },
            { 0.0, 1.0, 0.0 },
            { 0.0, -1.0, 0.0 },
            { 0.0, 0.0, 1.0 },
            { 0.0, 0.0, -1.0 }
        };
        return { coords[lane][0], coords[lane][1], coords[lane][2] };
    }
    if (count == 8) {
        constexpr double k = 0.57735026919;
        const double coords[8][3] = {
            { -k, -k, -k }, { k, -k, -k }, { k, k, -k }, { -k, k, -k },
            { -k, -k, k }, { k, -k, k }, { k, k, k }, { -k, k, k }
        };
        return { coords[lane][0], coords[lane][1], coords[lane][2] };
    }

    const double offset = 2.0 / static_cast<double>(count);
    const double y = 1.0 - (static_cast<double>(lane) + 0.5) * offset;
    const double r = std::sqrt(std::max(0.0, 1.0 - y * y));
    const double a = static_cast<double>(lane) * M_PI * (3.0 - std::sqrt(5.0));
    return { std::cos(a) * r, y, std::sin(a) * r };
}

inline std::array<double, 3> rotateAroundAxis(double x, double y, double z, double ax, double ay, double az, double angle)
{
    const double c = std::cos(angle);
    const double s = std::sin(angle);
    const double projection = x * ax + y * ay + z * az;
    const double crossX = ay * z - az * y;
    const double crossY = az * x - ax * z;
    const double crossZ = ax * y - ay * x;
    return {
        x * c + crossX * s + ax * projection * (1.0 - c),
        y * c + crossY * s + ay * projection * (1.0 - c),
        z * c + crossZ * s + az * projection * (1.0 - c)
    };
}

inline std::array<double, 3> cross3(double ax, double ay, double az, double bx, double by, double bz)
{
    return {
        ay * bz - az * by,
        az * bx - ax * bz,
        ax * by - ay * bx
    };
}

inline void normalize3(double& x, double& y, double& z)
{
    const double len = std::sqrt(x * x + y * y + z * z);
    if (len < 0.000001) {
        x = 1.0;
        y = 0.0;
        z = 0.0;
        return;
    }
    x /= len;
    y /= len;
    z /= len;
}

struct TopologyControls {
    double amount = 0.0;
    double jitter = 0.0;
    double collapse = 0.0;
    double dirX = 0.0;
    double dirY = 0.0;
    double dirZ = 1.0;
    double twist = 0.0;
    double flare = 0.0;
    double spinAngle = 0.0;
    double spinAmount = 0.0;
    uint32_t shape = 0;
};

struct TopologyState {
    double amount = 0.0;
    double jitter = 0.0;
    double collapse = 0.0;
    double dirX = 0.0;
    double dirY = 0.0;
    double dirZ = 1.0;
    double twist = 0.0;
    double flare = 0.0;
    uint32_t shape = 0;
    uint32_t motionMode = 0;
    uint32_t motionVariant = 0;
    double motionRateHz = 0.10;
    double motionDepth = 0.0;
    double motionPhase = 0.0;
    uint32_t neighborCount = 2;
    double neighborRadius = 0.65;
    double centroidAmount = 0.22;
};

struct TopologyPoint {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double lane = 0.0;
    double radius = 0.0;
    double noise = 0.0;
};

inline bool topologyMotionActive(const TopologyState& state)
{
    return state.motionMode > 0u && state.motionDepth > 0.0001 && state.motionRateHz > 0.0;
}

inline double triangleBipolar(double phase)
{
    const double p = phase - std::floor(phase);
    return 1.0 - 4.0 * std::fabs(p - 0.5);
}

inline double raisedSine(double phase)
{
    return 0.5 + 0.5 * std::sin(phase * M_PI * 2.0 - M_PI * 0.5);
}

inline TopologyControls topologyControlsFromState(const TopologyState& state)
{
    TopologyControls controls {};
    controls.amount = std::clamp(state.amount, 0.0, 1.0);
    controls.jitter = std::clamp(state.jitter, 0.0, 1.0);
    controls.collapse = std::clamp(state.collapse, 0.0, 1.0);
    controls.dirX = std::clamp(state.dirX, -1.0, 1.0);
    controls.dirY = std::clamp(state.dirY, -1.0, 1.0);
    controls.dirZ = std::clamp(state.dirZ, -1.0, 1.0);
    controls.twist = std::clamp(state.twist, -1.0, 1.0);
    controls.flare = std::clamp(state.flare, -1.0, 1.0);
    controls.shape = std::min<uint32_t>(state.shape, kTopologyShapeCount - 1u);
    if (!topologyMotionActive(state)) {
        return controls;
    }

    const double depth = std::clamp(state.motionDepth, 0.0, 1.0);
    const double phase = state.motionPhase - std::floor(state.motionPhase);
    const double a = phase * M_PI * 2.0;
    const double slowRise = raisedSine(phase);
    const double sineX = std::sin(a);
    const double sineY = std::sin(a + M_PI * 0.5);
    const double sineZ = std::sin(a + M_PI);
    const double tri = triangleBipolar(phase + 0.125);
    const double second = std::sin(a * 2.0 + M_PI * 0.25);
    const double third = std::sin(a * 3.0 + M_PI * 0.42);
    switch (std::min<uint32_t>(state.motionMode, kTopologyMotionModeCount - 1u)) {
    case 2: // DRIFT
        controls.amount = std::clamp(controls.amount + depth * (0.10 + 0.10 * slowRise), 0.0, 1.0);
        controls.dirX = std::clamp(controls.dirX + sineX * depth * 0.34 + second * depth * 0.12, -1.0, 1.0);
        controls.dirY = std::clamp(controls.dirY + sineY * depth * 0.24 + third * depth * 0.10, -1.0, 1.0);
        controls.dirZ = std::clamp(controls.dirZ + sineZ * depth * 0.28 + std::sin(a * 2.0 + M_PI) * depth * 0.10, -1.0, 1.0);
        controls.flare = std::clamp(controls.flare + third * depth * 0.16, -1.0, 1.0);
        break;
    case 3: // PULSE
        controls.amount = std::clamp(controls.amount + depth * 0.36 * slowRise, 0.0, 1.0);
        controls.collapse = std::clamp(controls.collapse + depth * 0.72 * slowRise, 0.0, 1.0);
        controls.flare = std::clamp(controls.flare + (slowRise * 2.0 - 1.0) * depth * 0.40, -1.0, 1.0);
        break;
    case 4: // ORBIT
        controls.amount = std::clamp(controls.amount + depth * 0.20, 0.0, 1.0);
        controls.spinAngle = a;
        controls.spinAmount = std::clamp(0.18 + depth * 0.82, 0.0, 1.0);
        controls.twist = std::clamp(controls.twist + depth * 0.16, -1.0, 1.0);
        break;
    case 5: // FOLD
        controls.amount = std::clamp(controls.amount + depth * 0.30 * slowRise, 0.0, 1.0);
        controls.collapse = std::clamp(controls.collapse + depth * 0.62 * (0.5 + 0.5 * tri), 0.0, 1.0);
        controls.twist = std::clamp(controls.twist + second * depth * 0.54, -1.0, 1.0);
        controls.flare = std::clamp(controls.flare - (0.35 + 0.65 * slowRise) * depth * 0.44, -1.0, 1.0);
        break;
    case 6: // WEAVE
        controls.amount = std::clamp(controls.amount + depth * 0.34, 0.0, 1.0);
        controls.dirX = std::clamp(controls.dirX + sineX * depth * 0.52, -1.0, 1.0);
        controls.dirY = std::clamp(controls.dirY + std::sin(a * 1.5 + M_PI * 0.5) * depth * 0.44, -1.0, 1.0);
        controls.twist = std::clamp(controls.twist + second * depth * 0.32, -1.0, 1.0);
        break;
    case 7: // GRID
        controls.amount = std::clamp(controls.amount + depth * 0.42, 0.0, 1.0);
        controls.flare = std::clamp(controls.flare + triangleBipolar(phase) * depth * 0.22, -1.0, 1.0);
        controls.twist = std::clamp(controls.twist + (triangleBipolar(phase + 0.25) * 0.5) * depth, -1.0, 1.0);
        break;
    case 8: // TRACE
        controls.amount = std::clamp(controls.amount + depth * (0.16 + slowRise * 0.26), 0.0, 1.0);
        controls.spinAngle = a * 0.72;
        controls.spinAmount = depth * 0.52;
        controls.flare = std::clamp(controls.flare + second * depth * 0.24, -1.0, 1.0);
        break;
    case 9: // HOVER
        controls.amount = std::clamp(controls.amount + depth * 0.16, 0.0, 1.0);
        controls.dirY = std::clamp(controls.dirY + std::sin(a * 0.5) * depth * 0.28, -1.0, 1.0);
        controls.collapse = std::clamp(controls.collapse + depth * 0.20, 0.0, 1.0);
        break;
    case 10: // LEAP
        controls.amount = std::clamp(controls.amount + depth * 0.44 * std::pow(slowRise, 3.0), 0.0, 1.0);
        controls.collapse = std::clamp(controls.collapse + depth * 0.38 * std::pow(1.0 - slowRise, 2.0), 0.0, 1.0);
        controls.flare = std::clamp(controls.flare + (slowRise * 2.0 - 1.0) * depth * 0.55, -1.0, 1.0);
        break;
    case 11: // FIELD
        controls.amount = std::clamp(controls.amount + depth * 0.36, 0.0, 1.0);
        controls.dirX = std::clamp(controls.dirX + sineX * depth * 0.28, -1.0, 1.0);
        controls.dirZ = std::clamp(controls.dirZ + second * depth * 0.36, -1.0, 1.0);
        controls.twist = std::clamp(controls.twist + third * depth * 0.28, -1.0, 1.0);
        break;
    case 12: // PAIR
        controls.amount = std::clamp(controls.amount + depth * 0.28, 0.0, 1.0);
        controls.collapse = std::clamp(controls.collapse + depth * 0.18, 0.0, 1.0);
        controls.flare = std::clamp(controls.flare + std::sin(a * 0.5) * depth * 0.36, -1.0, 1.0);
        break;
    case 13: // FLOW
        controls.amount = std::clamp(controls.amount + depth * 0.30, 0.0, 1.0);
        controls.spinAngle = a * 0.65 + std::sin(a) * 0.55;
        controls.spinAmount = depth * 0.66;
        controls.twist = std::clamp(controls.twist + std::sin(a * 0.72) * depth * 0.34, -1.0, 1.0);
        break;
    case 14: // GROUP
        controls.amount = std::clamp(controls.amount + depth * 0.26, 0.0, 1.0);
        controls.collapse = std::clamp(controls.collapse + depth * 0.24 * slowRise, 0.0, 1.0);
        controls.jitter = std::clamp(controls.jitter + depth * 0.16, 0.0, 1.0);
        break;
    case 15: // MARCH
        controls.amount = std::clamp(controls.amount + depth * 0.38, 0.0, 1.0);
        controls.dirX = std::clamp(controls.dirX + triangleBipolar(phase) * depth * 0.38, -1.0, 1.0);
        controls.dirY = std::clamp(controls.dirY + triangleBipolar(phase + 0.5) * depth * 0.18, -1.0, 1.0);
        break;
    case 16: // PATH
        controls.amount = std::clamp(controls.amount + depth * 0.40, 0.0, 1.0);
        controls.spinAngle = a * 0.5;
        controls.spinAmount = depth * 0.42;
        controls.collapse = std::clamp(controls.collapse + depth * 0.20 * raisedSine(phase + 0.25), 0.0, 1.0);
        break;
    case 17: // SCAT
        controls.amount = std::clamp(controls.amount + depth * 0.32, 0.0, 1.0);
        controls.jitter = std::clamp(controls.jitter + depth * 0.48, 0.0, 1.0);
        controls.flare = std::clamp(controls.flare + third * depth * 0.40, -1.0, 1.0);
        break;
    case 1: // FREE
    default:
        controls.amount = std::clamp(controls.amount + depth * 0.24 * slowRise, 0.0, 1.0);
        controls.collapse = std::clamp(controls.collapse + depth * 0.58 * (0.5 + 0.5 * tri), 0.0, 1.0);
        controls.dirX = std::clamp(controls.dirX + sineX * depth * 0.82, -1.0, 1.0);
        controls.dirY = std::clamp(controls.dirY + sineY * depth * 0.58, -1.0, 1.0);
        controls.dirZ = std::clamp(controls.dirZ + sineZ * depth * 0.72, -1.0, 1.0);
        controls.twist = std::clamp(controls.twist + second * depth * 0.46, -1.0, 1.0);
        controls.flare = std::clamp(controls.flare + std::cos(a) * depth * 0.38, -1.0, 1.0);
        break;
    }

    const uint32_t variant = std::min<uint32_t>(state.motionVariant, kTopologyVariantCount - 1u);
    if (variant == 2) { // WIDE
        controls.amount = std::clamp(controls.amount + 0.14 * depth, 0.0, 1.0);
        controls.flare = std::clamp(controls.flare + 0.18 * depth, -1.0, 1.0);
    } else if (variant == 3) { // FOLD
        controls.shape = 2;
        controls.collapse = std::clamp(controls.collapse + 0.20 * depth, 0.0, 1.0);
    } else if (variant == 4) { // CAN
        controls.twist = std::clamp(controls.twist + 0.22 * depth, -1.0, 1.0);
    } else if (variant == 5 || variant == 15) { // SUSP/STIL
        controls.collapse = std::clamp(controls.collapse + 0.32 * depth, 0.0, 1.0);
        controls.jitter *= 0.45;
    } else if (variant == 6) { // BURST
        controls.flare = std::clamp(controls.flare + std::pow(slowRise, 4.0) * depth * 0.72, -1.0, 1.0);
        controls.jitter = std::clamp(controls.jitter + depth * 0.22, 0.0, 1.0);
    } else if (variant == 7) { // DRFT
        controls.jitter = std::clamp(controls.jitter + depth * 0.18, 0.0, 1.0);
    } else if (variant == 8) { // RIB
        controls.flare = std::clamp(controls.flare - 0.24 * depth, -1.0, 1.0);
        controls.twist = std::clamp(controls.twist + 0.18 * depth, -1.0, 1.0);
    } else if (variant == 9) { // GATE
        controls.collapse = std::clamp(controls.collapse + (1.0 - slowRise) * depth * 0.55, 0.0, 1.0);
    } else if (variant == 10) { // MIR
        controls.shape = 7;
    } else if (variant == 11) { // SURG
        controls.flare = std::clamp(controls.flare + slowRise * depth * 0.58, -1.0, 1.0);
    } else if (variant == 12) { // TETH
        controls.collapse = std::clamp(controls.collapse + 0.28 * depth, 0.0, 1.0);
    } else if (variant == 13) { // VORT
        controls.spinAmount = std::clamp(controls.spinAmount + depth * 0.34, 0.0, 1.0);
        controls.twist = std::clamp(controls.twist + depth * 0.28, -1.0, 1.0);
    } else if (variant == 14) { // YLD
        controls.collapse = std::clamp(controls.collapse + slowRise * depth * 0.34, 0.0, 1.0);
        controls.flare = std::clamp(controls.flare - slowRise * depth * 0.22, -1.0, 1.0);
    }
    return controls;
}

inline TopologyPoint topologyPointForLane(uint32_t laneIndex, uint32_t count, const TopologyControls& controls)
{
    const auto base = baseTopologyPoint(laneIndex, count);
    const double amount = controls.amount;
    const double noise = laneNoise(laneIndex);
    double x = base[0];
    double y = base[1];
    double z = base[2];

    double dirX = controls.dirX;
    double dirY = controls.dirY;
    double dirZ = controls.dirZ;
    double dirLen = std::sqrt(dirX * dirX + dirY * dirY + dirZ * dirZ);
    if (dirLen < 0.001) {
        dirX = 0.0;
        dirY = 0.0;
        dirZ = 1.0;
        dirLen = 1.0;
    }
    dirX /= dirLen;
    dirY /= dirLen;
    dirZ /= dirLen;

    const uint32_t shape = std::min<uint32_t>(controls.shape, kTopologyShapeCount - 1u);
    double bx = std::fabs(dirY) < 0.82 ? 0.0 : 1.0;
    double by = std::fabs(dirY) < 0.82 ? 1.0 : 0.0;
    double bz = 0.0;
    auto u = cross3(dirX, dirY, dirZ, bx, by, bz);
    double ux = u[0];
    double uy = u[1];
    double uz = u[2];
    normalize3(ux, uy, uz);
    auto v = cross3(dirX, dirY, dirZ, ux, uy, uz);
    double vx = v[0];
    double vy = v[1];
    double vz = v[2];
    normalize3(vx, vy, vz);

    if (shape == 0) {
        if (amount > 0.0001) {
            auto az = rotateAroundAxis(x, y, z, 0.0, 1.0, 0.0, controls.dirX * amount * M_PI);
            auto el = rotateAroundAxis(az[0], az[1], az[2], 1.0, 0.0, 0.0, controls.dirY * amount * M_PI * 0.5);
            const double distance = std::max(0.15, 1.0 + controls.dirZ * amount * 0.85);
            x = el[0] * distance;
            y = el[1] * distance;
            z = el[2] * distance;
        }
    } else if (shape == 1) {
        x += y * amount * (0.85 + controls.dirX * 0.30);
    } else if (shape == 2) {
        y = y >= 0.0 ? y * (1.0 - amount * 0.80) : y - amount * 0.45;
    } else if (shape == 3) {
        const double twist = amount * (1.0 - std::min(1.0, std::fabs(y))) * M_PI * 1.35;
        auto r = rotateAroundAxis(x, y, z, dirX, dirY, dirZ, twist);
        x = r[0];
        y = r[1];
        z = r[2];
    } else if (shape == 4) {
        const double projection = x * dirX + y * dirY + z * dirZ;
        x = lerp(x, dirX * projection, amount * 0.75);
        y = lerp(y, dirY * projection, amount * 0.75);
        z = lerp(z, dirZ * projection, amount * 0.75);
    } else if (shape == 5) {
        const double band = static_cast<double>(static_cast<int>(laneIndex % 3u)) - 1.0;
        x += band * amount * 0.85;
        z -= band * amount * 0.35;
    } else if (shape == 6) {
        x += laneNoise(laneIndex + 17u) * amount * 0.95;
        y += laneNoise(laneIndex + 41u) * amount * 0.75;
        z += laneNoise(laneIndex + 73u) * amount * 0.95;
    } else if (shape == 7) {
        const double sign = x < 0.0 ? -1.0 : 1.0;
        x = lerp(x, sign * std::fabs(x), amount);
        z = lerp(z, -z, amount * 0.5);
    } else if (shape == 8) {
        const double az = std::atan2(x, z);
        y += std::sin(az * 2.0) * amount * 0.70;
    } else if (shape == 9) {
        const double lane = count <= 1 ? 0.0 : (static_cast<double>(laneIndex) / static_cast<double>(count - 1u)) * 2.0 - 1.0;
        const double fold = std::sin(static_cast<double>(laneIndex + 1u) * 1.61803398875) * 0.16 * (1.0 - amount);
        x = lerp(x, dirX * lane + ux * fold, amount);
        y = lerp(y, dirY * lane + uy * fold, amount);
        z = lerp(z, dirZ * lane + uz * fold, amount);
    } else if (shape == 10) {
        const double projection = x * dirX + y * dirY + z * dirZ;
        x -= dirX * projection * amount;
        y -= dirY * projection * amount;
        z -= dirZ * projection * amount;
    } else if (shape == 11) {
        const double phase = count <= 1 ? 0.0 : static_cast<double>(laneIndex) / static_cast<double>(count);
        const double a = phase * M_PI * 2.0;
        const double b = (phase * 3.0 + 0.125) * M_PI * 2.0;
        const double px = ux * std::sin(a) * 0.92 + vx * std::sin(b) * 0.48 + dirX * std::sin(a + b) * 0.36;
        const double py = uy * std::sin(a) * 0.92 + vy * std::sin(b) * 0.48 + dirY * std::sin(a + b) * 0.36;
        const double pz = uz * std::sin(a) * 0.92 + vz * std::sin(b) * 0.48 + dirZ * std::sin(a + b) * 0.36;
        x = lerp(x, px, amount);
        y = lerp(y, py, amount);
        z = lerp(z, pz, amount);
    }

    const double projection = x * dirX + y * dirY + z * dirZ;
    const double spinAmount = std::clamp(controls.spinAmount, 0.0, 1.0);
    if (spinAmount > 0.0001) {
        auto r = rotateAroundAxis(x, y, z, dirX, dirY, dirZ, controls.spinAngle);
        x = lerp(x, r[0], spinAmount);
        y = lerp(y, r[1], spinAmount);
        z = lerp(z, r[2], spinAmount);
    }
    if (std::fabs(controls.twist) > 0.0001) {
        auto r = rotateAroundAxis(x, y, z, dirX, dirY, dirZ, controls.twist * projection * M_PI * (1.0 + amount * 1.2));
        x = r[0];
        y = r[1];
        z = r[2];
    }
    const double flare = 1.0 + controls.flare * projection * (0.72 + amount * 0.55);
    x *= flare;
    y *= flare;
    z *= flare;

    const double collapse = std::clamp(controls.collapse, 0.0, 1.0);
    if (collapse > 0.0001) {
        const double pull = collapse * collapse * (3.0 - 2.0 * collapse);
        const double targetDistance = 0.08 + std::max(0.0, controls.dirZ) * 1.18;
        const double targetX = dirX * targetDistance;
        const double targetY = dirY * targetDistance;
        const double targetZ = dirZ * targetDistance;
        x = lerp(x, targetX, pull);
        y = lerp(y, targetY, pull);
        z = lerp(z, targetZ, pull);
    }

    const double seed = controls.jitter;
    const double seedScale = std::max(amount, collapse);
    x += laneNoise(laneIndex + 101u) * seed * seedScale * 0.34;
    y += laneNoise(laneIndex + 131u) * seed * seedScale * 0.26;
    z += laneNoise(laneIndex + 151u) * seed * seedScale * 0.34;

    const double radius = std::sqrt(x * x + y * y + z * z);
    const double lane = std::clamp(x * 0.78 + z * 0.52 + y * 0.32, -1.0, 1.0);
    return { x, y, z, lane, radius, noise };
}

inline double topologyAmount(double amount)
{
    return std::pow(std::clamp(amount, 0.0, 1.0), 0.62);
}

inline TopologyPoint topologyPointForLane(uint32_t laneIndex, uint32_t count, const TopologyState& state)
{
    return topologyPointForLane(laneIndex, count, topologyControlsFromState(state));
}

inline std::array<int, 3> nearestTopologyNeighbors(const TopologyState& state, uint32_t channel, uint32_t count)
{
    if (count < 2 || channel >= count) {
        const int ch = static_cast<int>(channel);
        return { ch, ch, ch };
    }

    const auto here = topologyPointForLane(channel, count, state);
    std::array<double, 3> best {
        std::numeric_limits<double>::max(),
        std::numeric_limits<double>::max(),
        std::numeric_limits<double>::max()
    };
    std::array<int, 3> neighbors {
        static_cast<int>((channel + count - 1u) % count),
        static_cast<int>((channel + 1u) % count),
        static_cast<int>((channel + count / 2u) % count)
    };
    for (uint32_t other = 0; other < count; ++other) {
        if (other == channel) {
            continue;
        }
        const auto there = topologyPointForLane(other, count, state);
        const double dx = here.x - there.x;
        const double dy = here.y - there.y;
        const double dz = here.z - there.z;
        const double d = dx * dx + dy * dy + dz * dz;
        for (uint32_t slot = 0; slot < 3; ++slot) {
            if (d < best[slot]) {
                for (uint32_t move = 2; move > slot; --move) {
                    best[move] = best[move - 1u];
                    neighbors[move] = neighbors[move - 1u];
                }
                best[slot] = d;
                neighbors[slot] = static_cast<int>(other);
                break;
            }
        }
    }

    const double radius = 0.18 + std::clamp(state.neighborRadius, 0.0, 1.0) * 3.20;
    const double radiusSq = radius * radius;
    const uint32_t requested = std::clamp<uint32_t>(state.neighborCount, 1u, 3u);
    for (uint32_t slot = 0; slot < 3; ++slot) {
        if (slot >= requested || (slot > 0 && best[slot] > radiusSq)) {
            neighbors[slot] = neighbors[0];
        }
    }
    return neighbors;
}

} // namespace s3g
