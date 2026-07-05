#pragma once

#include <array>

namespace s3g {

struct SpeakerPoint {
    float azimuthDeg;
    float elevationDeg;
    float distance;
};

constexpr int kVirtualSpeakerCount = 24;

// 24-point virtual dome layout used by the 3OAFX insert layer.
// Azimuth convention follows the s3g-mc REAPER tools: -90 is right, +90 is left.
inline constexpr std::array<SpeakerPoint, kVirtualSpeakerCount> kVirtualDome24 = {{
    {  45.0f,   0.0f, 1.0f}, {   0.0f,   0.0f, 1.0f}, { -45.0f,   0.0f, 1.0f},
    { -90.0f,   0.0f, 1.0f}, {-135.0f,   0.0f, 1.0f}, { 180.0f,   0.0f, 1.0f},
    { 135.0f,   0.0f, 1.0f}, {  90.0f,   0.0f, 1.0f},
    {  45.0f,  35.0f, 1.0f}, {   0.0f,  35.0f, 1.0f}, { -45.0f,  35.0f, 1.0f},
    { -90.0f,  35.0f, 1.0f}, {-135.0f,  35.0f, 1.0f}, { 180.0f,  35.0f, 1.0f},
    { 135.0f,  35.0f, 1.0f}, {  90.0f,  35.0f, 1.0f},
    {  45.0f,  60.0f, 1.0f}, {   0.0f,  60.0f, 1.0f}, { -45.0f,  60.0f, 1.0f},
    { -90.0f,  60.0f, 1.0f}, {-135.0f,  60.0f, 1.0f}, { 180.0f,  60.0f, 1.0f},
    { 135.0f,  60.0f, 1.0f}, {  90.0f,  60.0f, 1.0f}
}};

} // namespace s3g
