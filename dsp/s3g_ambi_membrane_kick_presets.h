#pragma once

#include "s3g_ambi_membrane_kick.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace s3g {

struct AmbiMembraneKickFactoryPresetInfo {
    const char* name;
    const char* description;
};

constexpr uint32_t kAmbiMembraneKickFactoryPresetCount = 14u;

inline const AmbiMembraneKickFactoryPresetInfo&
ambiMembraneKickFactoryPresetInfo(uint32_t index)
{
    static constexpr std::array<AmbiMembraneKickFactoryPresetInfo,
        kAmbiMembraneKickFactoryPresetCount> info {{
        { "DEEP CITY", "Balanced deep membrane kick with a broad spatial body." },
        { "QUAD BASS 43", "Fast classic bass drop into a focused 43 Hz fundamental." },
        { "TRUNK PRESSURE 38", "Low, driven 38 Hz body with a heavier lingering push." },
        { "MIAMI LONG TAIL", "Clean sustained sub tail with a slower downward glide." },
        { "HARD FLOOR", "Short aggressive impact with extra beater and drive." },
        { "WIDE ELLIPSE", "Off-centre elliptical membrane spread across the field." },
        { "FOUR CORNERS", "Square boundary with a rotating, strongly distributed hit." },
        { "TRIANGLE RUMBLE", "Very low triangular body with a long dark decay." },
        { "BROKEN BOOM", "Irregular driven membrane with rattling upper modes." },
        { "FOUNDATION 40", "Dark stable 40 Hz foundation with restrained upper modes." },
        { "SCOOP WARMTH 44", "Rounded scoop-inspired weight with warm local drive." },
        { "VERSION SPACE 37", "Sparse sustained 37 Hz source with room for downstream dub processing." },
        { "CLASH CUT 50", "Short hard 50 Hz impact designed to remain legible in a dense system." },
        { "THREE-STACK 42", "Threefold membrane geometry with broad direct spatial diffusion." },
    }};
    return info[std::min<uint32_t>(
        index, kAmbiMembraneKickFactoryPresetCount - 1u)];
}

inline AmbiMembraneKickParams ambiMembraneKickFactoryPreset(uint32_t index)
{
    AmbiMembraneKickParams params;
    switch (std::min<uint32_t>(
        index, kAmbiMembraneKickFactoryPresetCount - 1u)) {
    case 1u: // QUAD BASS 43
        params.shape = AmbiMembraneShape::Circle;
        params.tuneHz = 43.0f;
        params.pitchSweepSemitones = 38.0f;
        params.pitchSweepMs = 38.0f;
        params.decaySeconds = 1.25f;
        params.damping = 0.52f;
        params.punch = 0.95f;
        params.click = 0.08f;
        params.drive = 0.24f;
        params.strikeX = 0.05f;
        params.strikeY = -0.02f;
        params.spatialSpread = 0.64f;
        params.membraneDepth = 0.35f;
        params.shapeAmount = 0.0f;
        break;
    case 2u: // TRUNK PRESSURE 38
        params.shape = AmbiMembraneShape::Ellipse;
        params.tuneHz = 38.0f;
        params.pitchSweepSemitones = 32.0f;
        params.pitchSweepMs = 56.0f;
        params.decaySeconds = 2.40f;
        params.damping = 0.48f;
        params.punch = 0.88f;
        params.click = 0.04f;
        params.drive = 0.52f;
        params.strikeX = 0.12f;
        params.strikeY = -0.05f;
        params.spatialSpread = 0.52f;
        params.membraneDepth = 0.72f;
        params.rotationDeg = -15.0f;
        params.shapeAmount = 0.45f;
        break;
    case 3u: // MIAMI LONG TAIL
        params.shape = AmbiMembraneShape::Circle;
        params.tuneHz = 46.0f;
        params.pitchSweepSemitones = 26.0f;
        params.pitchSweepMs = 78.0f;
        params.decaySeconds = 3.60f;
        params.damping = 0.62f;
        params.punch = 0.74f;
        params.click = 0.03f;
        params.drive = 0.18f;
        params.strikeX = 0.02f;
        params.strikeY = 0.01f;
        params.spatialSpread = 0.44f;
        params.membraneDepth = 0.28f;
        params.shapeAmount = 0.0f;
        break;
    case 4u: // HARD FLOOR
        params.shape = AmbiMembraneShape::Circle;
        params.tuneHz = 52.0f;
        params.pitchSweepSemitones = 44.0f;
        params.pitchSweepMs = 21.0f;
        params.decaySeconds = 0.72f;
        params.damping = 0.30f;
        params.punch = 1.0f;
        params.click = 0.38f;
        params.drive = 0.68f;
        params.strikeX = 0.26f;
        params.strikeY = -0.12f;
        params.spatialSpread = 0.74f;
        params.membraneDepth = 0.36f;
        params.shapeAmount = 0.0f;
        break;
    case 5u: // WIDE ELLIPSE
        params.shape = AmbiMembraneShape::Ellipse;
        params.tuneHz = 41.0f;
        params.pitchSweepSemitones = 34.0f;
        params.pitchSweepMs = 46.0f;
        params.decaySeconds = 1.65f;
        params.damping = 0.33f;
        params.punch = 0.82f;
        params.click = 0.12f;
        params.drive = 0.30f;
        params.strikeX = 0.42f;
        params.strikeY = -0.18f;
        params.spatialSpread = 1.0f;
        params.membraneDepth = 0.58f;
        params.rotationDeg = -28.0f;
        params.shapeAmount = 0.88f;
        break;
    case 6u: // FOUR CORNERS
        params.shape = AmbiMembraneShape::Square;
        params.tuneHz = 44.0f;
        params.pitchSweepSemitones = 30.0f;
        params.pitchSweepMs = 34.0f;
        params.decaySeconds = 1.30f;
        params.damping = 0.24f;
        params.punch = 0.78f;
        params.click = 0.18f;
        params.drive = 0.42f;
        params.strikeX = 0.34f;
        params.strikeY = 0.31f;
        params.strikeMode = AmbiMembraneStrikeMode::RandomArea;
        params.spatialSpread = 0.92f;
        params.membraneDepth = 0.50f;
        params.rotationDeg = 45.0f;
        params.shapeAmount = 0.92f;
        break;
    case 7u: // TRIANGLE RUMBLE
        params.shape = AmbiMembraneShape::Triangle;
        params.tuneHz = 35.0f;
        params.pitchSweepSemitones = 28.0f;
        params.pitchSweepMs = 70.0f;
        params.decaySeconds = 4.20f;
        params.damping = 0.68f;
        params.punch = 0.84f;
        params.click = 0.02f;
        params.drive = 0.35f;
        params.strikeX = -0.22f;
        params.strikeY = 0.30f;
        params.spatialSpread = 0.82f;
        params.membraneDepth = 0.84f;
        params.rotationDeg = 120.0f;
        params.shapeAmount = 0.86f;
        break;
    case 8u: // BROKEN BOOM
        params.shape = AmbiMembraneShape::Irregular;
        params.tuneHz = 48.0f;
        params.pitchSweepSemitones = 20.0f;
        params.pitchSweepMs = 30.0f;
        params.decaySeconds = 1.10f;
        params.damping = 0.10f;
        params.punch = 0.70f;
        params.click = 0.28f;
        params.drive = 0.75f;
        params.strikeX = 0.55f;
        params.strikeY = -0.33f;
        params.strikeMode = AmbiMembraneStrikeMode::RandomRim;
        params.spatialSpread = 1.0f;
        params.membraneDepth = 0.76f;
        params.rotationDeg = 72.0f;
        params.shapeAmount = 0.94f;
        break;
    case 9u: // FOUNDATION 40
        params.shape = AmbiMembraneShape::Circle;
        params.tuneHz = 40.0f;
        params.pitchSweepSemitones = 22.0f;
        params.pitchSweepMs = 65.0f;
        params.decaySeconds = 2.80f;
        params.damping = 0.74f;
        params.punch = 0.92f;
        params.click = 0.02f;
        params.drive = 0.30f;
        params.strikeX = 0.04f;
        params.strikeY = -0.02f;
        params.spatialSpread = 0.30f;
        params.membraneDepth = 0.50f;
        params.shapeAmount = 0.0f;
        params.noteTracking = 0.25f;
        break;
    case 10u: // SCOOP WARMTH 44
        params.shape = AmbiMembraneShape::Ellipse;
        params.tuneHz = 44.0f;
        params.pitchSweepSemitones = 18.0f;
        params.pitchSweepMs = 78.0f;
        params.decaySeconds = 2.10f;
        params.damping = 0.58f;
        params.punch = 0.80f;
        params.click = 0.03f;
        params.drive = 0.48f;
        params.strikeX = 0.16f;
        params.strikeY = -0.06f;
        params.spatialSpread = 0.38f;
        params.membraneDepth = 0.66f;
        params.shapeAmount = 0.38f;
        params.noteTracking = 0.50f;
        break;
    case 11u: // VERSION SPACE 37
        params.shape = AmbiMembraneShape::Circle;
        params.tuneHz = 37.0f;
        params.pitchSweepSemitones = 14.0f;
        params.pitchSweepMs = 105.0f;
        params.decaySeconds = 4.0f;
        params.damping = 0.82f;
        params.punch = 0.86f;
        params.click = 0.0f;
        params.drive = 0.20f;
        params.strikeX = 0.02f;
        params.strikeY = 0.0f;
        params.spatialSpread = 0.24f;
        params.membraneDepth = 0.72f;
        params.shapeAmount = 0.0f;
        params.noteTracking = 0.0f;
        break;
    case 12u: // CLASH CUT 50
        params.shape = AmbiMembraneShape::Square;
        params.tuneHz = 50.0f;
        params.pitchSweepSemitones = 42.0f;
        params.pitchSweepMs = 24.0f;
        params.decaySeconds = 0.82f;
        params.damping = 0.42f;
        params.punch = 1.0f;
        params.click = 0.16f;
        params.drive = 0.65f;
        params.strikeX = 0.20f;
        params.strikeY = -0.10f;
        params.spatialSpread = 0.46f;
        params.membraneDepth = 0.28f;
        params.shapeAmount = 0.22f;
        params.noteTracking = 1.0f;
        break;
    case 13u: // THREE-STACK 42
        params.shape = AmbiMembraneShape::Triangle;
        params.tuneHz = 42.0f;
        params.pitchSweepSemitones = 26.0f;
        params.pitchSweepMs = 48.0f;
        params.decaySeconds = 1.65f;
        params.damping = 0.62f;
        params.punch = 0.88f;
        params.click = 0.04f;
        params.drive = 0.36f;
        params.strikeX = 0.03f;
        params.strikeY = 0.02f;
        params.spatialSpread = 0.72f;
        params.membraneDepth = 0.20f;
        params.shapeAmount = 0.38f;
        params.noteTracking = 0.50f;
        break;
    case 0u: // DEEP CITY
    default:
        break;
    }
    return params;
}

inline int32_t ambiMembraneKickFactoryPresetIndex(
    const AmbiMembraneKickParams& params)
{
    const auto close = [](float left, float right) {
        return std::fabs(left - right) <= 1.0e-4f;
    };
    for (uint32_t index = 0u;
         index < kAmbiMembraneKickFactoryPresetCount; ++index) {
        const auto preset = ambiMembraneKickFactoryPreset(index);
        if (params.shape == preset.shape
            && params.strikeMode == preset.strikeMode
            && close(params.tuneHz, preset.tuneHz)
            && close(params.pitchSweepSemitones,
                preset.pitchSweepSemitones)
            && close(params.pitchSweepMs, preset.pitchSweepMs)
            && close(params.decaySeconds, preset.decaySeconds)
            && close(params.damping, preset.damping)
            && close(params.punch, preset.punch)
            && close(params.click, preset.click)
            && close(params.drive, preset.drive)
            && close(params.strikeX, preset.strikeX)
            && close(params.strikeY, preset.strikeY)
            && close(params.spatialSpread, preset.spatialSpread)
            && close(params.membraneDepth, preset.membraneDepth)
            && close(params.rotationDeg, preset.rotationDeg)
            && close(params.shapeAmount, preset.shapeAmount)
            && close(params.velocitySensitivity,
                preset.velocitySensitivity)
            && close(params.noteTracking, preset.noteTracking)) {
            return static_cast<int32_t>(index);
        }
    }
    return -1;
}

} // namespace s3g
