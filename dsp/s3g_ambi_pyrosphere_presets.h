#pragma once

#include "s3g_ambi_pyrosphere_encoder.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace s3g {

struct AmbiPyrospherePresetInfo {
    const char* name;
    const char* description;
};

inline constexpr uint32_t kAmbiPyrosphereFactoryPresetCount = 14u;
inline constexpr std::array<AmbiPyrospherePresetInfo,
    kAmbiPyrosphereFactoryPresetCount> kAmbiPyrospherePresetInfo {{
        { "Duff Smoulder", "A damp porous fuel bed with muted combustion and internal material failure." },
        { "Timber Pyrolysis", "Heated timber releasing gas, checking across the grain, and dropping char." },
        { "Resinous Brush", "Fast volatile combustion with branching twig fracture and lofted fragments." },
        { "Grass Front", "A broad, rapidly advancing low-mass combustion front." },
        { "Forest Combustion Front", "Layered canopy heat release, timber fracture, and falling debris." },
        { "Root Burn", "Subsurface heat moving through roots, duff, and damp mineral soil." },
        { "Coal Seam", "Dense low combustion with pressure pockets and restrained fracture." },
        { "Oil Pool", "Broad turbulent liquid-fuel combustion with little solid fracture." },
        { "Masonry Thermal Spall", "Fire-heated masonry shedding plates and weakened fragments." },
        { "Rock Talus Spall", "Thermal mismatch and pore moisture fracturing an exposed rock field." },
        { "Structural Collapse", "Heat-weakened masonry and metal releasing heavy debris cascades." },
        { "Firestorm Debris Field", "A large rotating combustion front driving fracture, pressure, and debris." },
        { "Burned Timber Fall", "A standing fire-weakened tree loading, snapping through branch generations, hinging, and striking the ground." },
        { "Flamethrower Jet", "A sustained pressurized fuel jet with a dense nozzle core, turbulent shear roar, hiss, and downstream heat-release modulation." },
    }};

inline AmbiPyrospherePresetInfo ambiPyrosphereFactoryPresetInfo(
    uint32_t index)
{
    return kAmbiPyrospherePresetInfo[std::min<uint32_t>(
        index, kAmbiPyrosphereFactoryPresetCount - 1u)];
}

inline AmbiPyrosphereParams ambiPyrosphereFactoryPreset(uint32_t index)
{
    AmbiPyrosphereParams p {};
    p.order = 3u;
    p.outputGainDb = -8.0f;
    p.centerElevationDeg = 4.0f;
    p.motionUpdraft = 0.38f;
    p.motionFlow = 0.38f;
    p.motionShear = 0.24f;
    p.motionCurl = 0.28f;
    p.field = 0.58f;
    p.body = 0.46f;
    p.breath = 0.48f;
    p.surfaceX = p.surfaceY = 0.5f;
    const uint32_t safeIndex = std::min<uint32_t>(index,
        kAmbiPyrosphereFactoryPresetCount - 1u);
    switch (safeIndex) {
    case 0u:
        p.voices=22; p.materialMode=2u; p.wind=.28f; p.gustRate=.015f; p.gustDepth=.24f; p.turbulence=.20f; p.flutter=.14f;
        p.material=.82f; p.body=.76f; p.breath=.24f; p.air=.18f; p.hiss=.12f; p.q=.48f; p.grit=.38f; p.particles=.12f;
        p.pressure=.18f; p.spread=.42f; p.motionUpdraft=.12f; p.centerElevationDeg=-8.0f; p.space=.18f; break;
    case 1u:
        p.voices=30; p.materialMode=3u; p.wind=.52f; p.gustRate=.080f; p.gustDepth=.44f; p.turbulence=.42f; p.flutter=.28f;
        p.material=.88f; p.body=.72f; p.breath=.52f; p.air=.30f; p.hiss=.22f; p.q=.68f; p.grit=.70f; p.particles=.46f;
        p.center=.62f; p.spread=.54f; p.motionUpdraft=.40f; p.space=.22f;
        p.structuralLoad=.62f; p.snap=.78f; p.fall=.32f; break;
    case 2u:
        p.voices=38; p.materialMode=9u; p.wind=.64f; p.gustRate=.75f; p.gustDepth=.62f; p.turbulence=.58f; p.flutter=.72f;
        p.material=.72f; p.body=.42f; p.breath=.62f; p.air=.62f; p.hiss=.48f; p.q=.78f; p.grit=.86f; p.particles=.74f;
        p.spread=.68f; p.motionRateHz=.18f; p.motionUpdraft=.72f; p.sweep=.66f; p.shrill=.52f; break;
    case 3u:
        p.voices=48; p.materialMode=10u; p.wind=.68f; p.gustRate=1.60f; p.gustDepth=.48f; p.turbulence=.54f; p.flutter=.62f;
        p.material=.62f; p.body=.28f; p.breath=.58f; p.air=.64f; p.hiss=.52f; p.q=.62f; p.grit=.72f; p.particles=.58f;
        p.spread=.94f; p.motionRateHz=.24f; p.motionFlow=.76f; p.motionUpdraft=.64f; p.centerDistance=1.24f; break;
    case 4u:
        p.order=4u; p.voices=58; p.materialMode=11u; p.wind=.76f; p.gustRate=.22f; p.gustDepth=.78f; p.turbulence=.78f; p.flutter=.46f;
        p.material=.82f; p.body=.78f; p.breath=.64f; p.air=.54f; p.hiss=.40f; p.q=.72f; p.grit=.82f; p.particles=.88f;
        p.spread=.96f; p.motionRateHz=.12f; p.motionUpdraft=.88f; p.pressure=.48f; p.centerDistance=1.38f; p.outputGainDb=-10.0f;
        p.structuralLoad=.78f; p.snap=.82f; p.fall=.58f; break;
    case 5u:
        p.voices=26; p.materialMode=2u; p.wind=.38f; p.gustRate=.035f; p.gustDepth=.34f; p.turbulence=.28f; p.flutter=.18f;
        p.material=.92f; p.body=.86f; p.breath=.28f; p.air=.12f; p.hiss=.08f; p.q=.72f; p.grit=.62f; p.particles=.16f;
        p.pressure=.34f; p.spread=.52f; p.motionUpdraft=.06f; p.centerElevationDeg=-14.0f; p.place=5u; p.space=.28f; break;
    case 6u:
        p.voices=24; p.materialMode=4u; p.wind=.42f; p.gustRate=.018f; p.gustDepth=.30f; p.turbulence=.26f; p.flutter=.12f;
        p.material=.86f; p.body=.96f; p.breath=.46f; p.air=.12f; p.hiss=.08f; p.q=.56f; p.grit=.48f; p.particles=.38f;
        p.pressure=.66f; p.center=.72f; p.spread=.38f; p.motionUpdraft=.18f; p.space=.24f; break;
    case 7u:
        p.voices=34; p.materialMode=5u; p.wind=.78f; p.gustRate=.12f; p.gustDepth=.66f; p.turbulence=.84f; p.flutter=.32f;
        p.material=.58f; p.body=.64f; p.breath=.78f; p.air=.72f; p.hiss=.62f; p.q=.18f; p.grit=.08f; p.particles=.04f;
        p.pressure=.52f; p.center=.84f; p.spread=.62f; p.motionUpdraft=.78f; p.sweep=.72f; break;
    case 8u:
        p.voices=32; p.materialMode=6u; p.wind=.90f; p.gustRate=.030f; p.gustDepth=.42f; p.turbulence=.24f; p.flutter=.10f;
        p.material=.88f; p.body=.64f; p.breath=.42f; p.air=.10f; p.hiss=.06f; p.q=.90f; p.grit=.72f; p.particles=.44f;
        p.pressure=.54f; p.spread=.58f; p.motionUpdraft=.16f; p.place=3u; p.space=.38f; p.environmentDamping=.68f; break;
    case 9u:
        p.voices=36; p.materialMode=12u; p.wind=.96f; p.gustRate=.020f; p.gustDepth=.38f; p.turbulence=.18f; p.flutter=.08f;
        p.material=.92f; p.body=.72f; p.breath=.36f; p.air=.08f; p.hiss=.04f; p.q=.94f; p.grit=.78f; p.particles=.54f;
        p.pressure=.62f; p.spread=.86f; p.motionUpdraft=.10f; p.place=5u; p.space=.30f; p.centerDistance=1.26f; break;
    case 10u:
        p.order=4u; p.voices=46; p.materialMode=6u; p.wind=.88f; p.gustRate=.10f; p.gustDepth=.72f; p.turbulence=.48f; p.flutter=.28f;
        p.material=.96f; p.body=.92f; p.breath=.52f; p.air=.18f; p.hiss=.12f; p.q=.88f; p.grit=.94f; p.particles=.82f;
        p.pressure=.86f; p.spread=.78f; p.motionShear=.68f; p.motionUpdraft=.42f; p.place=4u; p.space=.58f; p.outputGainDb=-10.0f; break;
    case 11u:
        p.order=4u; p.voices=64; p.materialMode=11u; p.wind=.96f; p.gustRate=1.60f; p.gustDepth=.94f; p.turbulence=.94f; p.flutter=.72f;
        p.material=.94f; p.body=.88f; p.breath=.72f; p.air=.68f; p.hiss=.54f; p.q=.86f; p.grit=1.0f; p.particles=1.0f;
        p.pressure=.96f; p.vortex=1.0f; p.spread=1.0f; p.motionRateHz=.28f; p.motionCurl=1.0f; p.motionUpdraft=1.0f;
        p.centerDistance=1.22f; p.outputGainDb=-11.0f;
        p.structuralLoad=.84f; p.snap=.88f; p.fall=.66f; break;
    case 12u:
        p.order=4u; p.voices=52; p.materialMode=11u; p.wind=.82f; p.gustRate=12.0f; p.gustDepth=.72f; p.turbulence=.66f; p.flutter=.34f;
        p.material=.98f; p.body=.88f; p.breath=.56f; p.air=.34f; p.hiss=.22f; p.q=.88f; p.grit=.90f; p.particles=.72f;
        p.pressure=.52f; p.vortex=.54f; p.spread=.88f; p.motionRateHz=.072f; p.motionFlow=.84f; p.motionShear=.78f; p.motionCurl=.38f; p.motionUpdraft=.48f;
        p.structuralLoad=1.0f; p.snap=1.0f; p.fall=1.0f; p.centerDistance=1.16f; p.space=.16f; p.outputGainDb=-11.0f; break;
    default:
        p.voices=12; p.materialMode=13u; p.wind=1.0f; p.gustRate=.42f; p.gustDepth=.18f; p.turbulence=.94f; p.flutter=.22f;
        p.material=.94f; p.body=.88f; p.breath=1.0f; p.air=.88f; p.hiss=.82f; p.q=.08f; p.grit=.04f; p.particles=.02f;
        p.center=.92f; p.sweep=.62f; p.shrill=.48f; p.pressure=1.0f; p.vortex=.74f; p.spread=.24f; p.deviation=.05f;
        p.motionRateHz=.15f; p.motionFlow=1.0f; p.motionShear=.88f; p.motionCurl=.18f; p.motionUpdraft=.24f;
        p.structuralLoad=0.0f; p.snap=.12f; p.fall=0.0f; p.centerDistance=.86f; p.space=.12f; p.outputGainDb=-11.0f; break;
    }
    struct ScoreProfile {
        float pace;
        float occupancy;
        float cascade;
        float memory;
        float rest;
    };
    static constexpr std::array<ScoreProfile,
        kAmbiPyrosphereFactoryPresetCount> scoreProfiles {{
        { .18f, .16f, .24f, .88f, .90f }, // duff smoulder
        { .28f, .24f, .50f, .84f, .78f }, // timber pyrolysis
        { .68f, .42f, .82f, .55f, .52f }, // resinous brush
        { .78f, .68f, .88f, .42f, .30f }, // grass front
        { .34f, .32f, .78f, .82f, .68f }, // forest front
        { .16f, .16f, .36f, .90f, .90f }, // root burn
        { .14f, .12f, .28f, .92f, .92f }, // coal seam
        { .42f, .38f, .36f, .70f, .48f }, // oil pool
        { .24f, .22f, .50f, .82f, .82f }, // masonry spall
        { .18f, .18f, .62f, .90f, .88f }, // rock talus
        { .30f, .28f, .74f, .86f, .78f }, // structural collapse
        { .82f, .80f, .95f, .38f, .16f }, // firestorm
        { .22f, .12f, .72f, .92f, .92f }, // burned timber fall
        { .78f, .95f, .10f, .85f, .05f }, // pressure jet
    }};
    const auto score = scoreProfiles[safeIndex];
    p.scorePace = score.pace;
    p.scoreOccupancy = score.occupancy;
    p.scoreCascade = score.cascade;
    p.scoreMemory = score.memory;
    p.scoreRest = score.rest;
    return p;
}

} // namespace s3g
