#pragma once

#include "s3g_math.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace s3g {

constexpr uint32_t k3OaChannels = 16;
constexpr uint32_t kAmbisonicSphere24PointCount = 24;

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

// AED uses the ambisonic axis convention: +Y is listener-left, so +90
// degrees is left and -90 degrees is right when viewed from above.  A
// horizontal position control therefore runs from positive azimuth on its
// left to negative azimuth on its right.
inline float aedAzimuthSliderNorm(float azimuthDeg)
{
    return clamp((180.0f - azimuthDeg) / 360.0f, 0.0f, 1.0f);
}

inline float aedAzimuthFromSliderNorm(float norm)
{
    return 180.0f - clamp(norm, 0.0f, 1.0f) * 360.0f;
}

struct AedViewProjection {
    float horizontal = 0.0f;
    float vertical = 0.0f;
    float depth = 0.0f;
};

inline AedViewProjection projectAedDirection(
    Vec3 direction, float viewAzimuthDeg, float viewElevationDeg)
{
    const float yaw = viewAzimuthDeg * kPi / 180.0f;
    const float elevation = viewElevationDeg * kPi / 180.0f;
    const float rotatedX = direction.x * std::cos(yaw)
        - direction.y * std::sin(yaw);
    const float rotatedY = direction.x * std::sin(yaw)
        + direction.y * std::cos(yaw);
    return {
        rotatedX,
        rotatedY * std::cos(elevation) - direction.z * std::sin(elevation),
        rotatedY * std::sin(elevation) + direction.z * std::cos(elevation),
    };
}

inline constexpr std::array<Vec3, kAmbisonicSphere24PointCount> kAmbisonicSphere24Points {{
    { 0.285652275f, 0.000000000f, 0.958333333f },
    { -0.356977173f, 0.327020333f, 0.875000000f },
    { 0.053413032f, -0.608613947f, 0.791666667f },
    { 0.429483666f, 0.560185389f, 0.708333333f },
    { -0.768691718f, -0.135970741f, 0.625000000f },
    { 0.709255111f, -0.451170045f, 0.541666667f },
    { -0.230731212f, 0.858308606f, 0.458333333f },
    { -0.427272247f, -0.822686712f, 0.375000000f },
    { 0.898479629f, 0.328123319f, 0.291666667f },
    { -0.904063458f, 0.373184253f, 0.208333333f },
    { 0.420521661f, -0.898630365f, 0.125000000f },
    { 0.299023957f, 0.953335493f, 0.041666667f },
    { -0.864459832f, -0.500972143f, -0.041666667f },
    { 0.969015453f, -0.213035329f, -0.125000000f },
    { -0.562509872f, 0.800112409f, -0.208333333f },
    { -0.122923048f, -0.948588678f, -0.291666667f },
    { 0.708848590f, 0.597418342f, -0.375000000f },
    { -0.888021405f, 0.036722473f, -0.458333333f },
    { 0.595837310f, -0.592937705f, -0.541666667f },
    { -0.036058186f, 0.779791515f, -0.625000000f },
    { -0.452262552f, -0.541961689f, -0.708333333f },
    { 0.605497091f, 0.081468777f, -0.791666667f },
    { -0.397396334f, 0.276498018f, -0.875000000f },
    { 0.062695352f, -0.278687127f, -0.958333333f },
}};

inline Vec3 directionFromAed(float azimuthDeg, float elevationDeg)
{
    const float az = azimuthDeg * kPi / 180.0f;
    const float el = elevationDeg * kPi / 180.0f;
    const float ce = std::cos(el);
    return { ce * std::cos(az), ce * std::sin(az), std::sin(el) };
}

inline Vec3 normalize(Vec3 v)
{
    const float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (len < 0.000001f) {
        return { 1.0f, 0.0f, 0.0f };
    }
    return { v.x / len, v.y / len, v.z / len };
}

inline std::array<float, k3OaChannels> acnSn3dBasis(Vec3 p)
{
    p = normalize(p);
    const float x = p.x;
    const float y = p.y;
    const float z = p.z;
    return {
        1.0f,
        y,
        z,
        x,
        1.224744871f * x * y,
        1.224744871f * y * z,
        1.5f * z * z - 0.5f,
        1.224744871f * x * z,
        0.612372435f * (x * x - y * y),
        0.790569415f * y * (3.0f * x * x - y * y),
        2.371708245f * x * y * z,
        0.790569415f * y * (5.0f * z * z - 1.0f),
        0.5f * z * (5.0f * z * z - 3.0f),
        0.790569415f * x * (5.0f * z * z - 1.0f),
        1.185854122f * z * (x * x - y * y),
        0.790569415f * x * (x * x - 3.0f * y * y),
    };
}

inline float softSat(float value)
{
    return value / std::sqrt(1.0f + value * value);
}

} // namespace s3g
