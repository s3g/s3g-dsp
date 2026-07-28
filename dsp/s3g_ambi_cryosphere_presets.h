#pragma once

#include "s3g_ambi_cryosphere_encoder.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace s3g {

struct AmbiCryospherePresetInfo {
    const char* name;
    const char* description;
};

inline constexpr uint32_t kAmbiCryosphereFactoryPresetCount = 18u;
inline constexpr std::array<AmbiCryospherePresetInfo,
    kAmbiCryosphereFactoryPresetCount> kAmbiCryospherePresetInfo {{
        { "Alpine Frost Crack", "Moist bedrock accumulating freezing stress and releasing sparse cracks." },
        { "Ice Segregation Gneiss", "Sustained ice-lens growth driving microfracture through hard rock." },
        { "Permafrost Heave", "Frozen moraine lifting, settling, and breaking along pore-water fronts." },
        { "Basal Glacier Stick-Slip", "A glacier bed loading against rough rock and releasing shear events." },
        { "Pressure Ridge", "Converging sea ice crushing, rafting, and grinding across a broad field." },
        { "Calving Front", "Fracture cascades separating large ice masses from a glacier face." },
        { "Iceberg Water Impact", "Calving blocks followed by delayed water impact and cavitation." },
        { "Avalanche Release", "Snow and ice detaching into a descending granular mass." },
        { "Snowpack Creep", "A soft snow body settling and slipping over an inclined substrate." },
        { "Hail on Metal", "Dense hard frozen impacts across a metal surface." },
        { "Sleet on Glass", "Mixed granular and wet frozen contacts against glass." },
        { "Meltwater Under Ice", "Basal water, brine pressure, sliding, and grinding beneath glacier ice." },
        { "Ice Under Foot", "A loaded surface plate flexing, opening radial cracks, snapping through, and collapsing locally under weight." },
        { "Singing Lake Ice", "Sparse fractures illuminate short, spatially scattered inharmonic lake modes from their own stochastic crack flux." },
        { "Tidal Ocean-Moon Rift", "Slow tidal loading tears a submerged ice shell into deep, propagating plate failures." },
        { "Methane-Ice Dune Creep", "Brittle hydrocarbon frost migrates in muted granular sheets through a shallow channel." },
        { "Subsurface Brine Upwelling", "Pressurized brine circulates upward through a damp ice cavern, opening and resealing fissures." },
        { "Quasicrystal Plate Bloom", "Mobile phase boundaries seed broadband fracture fronts and defect avalanches through an alien aperiodic lattice." },
    }};

inline AmbiCryospherePresetInfo ambiCryosphereFactoryPresetInfo(
    uint32_t index)
{
    return kAmbiCryospherePresetInfo[std::min<uint32_t>(
        index, kAmbiCryosphereFactoryPresetCount - 1u)];
}

inline AmbiCryosphereParams ambiCryosphereFactoryPreset(uint32_t index)
{
    AmbiCryosphereParams p {};
    p.order = 3u;
    p.outputGainDb = -8.0f;
    p.eventDecay = 0.34f;
    p.resonance = 0.18f;
    p.damping = 0.62f;
    p.environment = 1u;
    p.surfaceX = p.surfaceY = 0.5f;
    const uint32_t safeIndex = std::min<uint32_t>(index,
        kAmbiCryosphereFactoryPresetCount - 1u);
    switch (safeIndex) {
    case 0u:
        p.voices=24; p.regime=0u; p.environment=1u; p.water=.82f; p.flow=.48f; p.scale=.56f; p.turbulence=.32f;
        p.bubbles=.56f; p.density=.42f; p.eventSize=.38f; p.contact=.78f; p.brightness=.48f; p.current=.08f;
        p.convergence=.16f; p.spread=.72f; p.motionRateHz=.018f; p.space=.18f;
        p.surfaceLoad=.72f; p.snap=.88f; p.plateFailure=.34f; break;
    case 1u:
        p.voices=30; p.regime=1u; p.environment=1u; p.water=.94f; p.flow=.72f; p.scale=.72f; p.turbulence=.72f;
        p.bubbles=.82f; p.density=.68f; p.eventSize=.46f; p.eventDecay=.42f; p.contact=.92f; p.brightness=.42f;
        p.current=.06f; p.convergence=.38f; p.spread=.76f; p.motionRateHz=.012f; p.space=.20f; break;
    case 2u:
        p.voices=34; p.regime=2u; p.environment=3u; p.water=.86f; p.flow=.62f; p.scale=.88f; p.turbulence=.48f;
        p.bubbles=.72f; p.density=.54f; p.eventSize=.72f; p.eventDecay=.58f; p.contact=.76f; p.depth=.72f;
        p.current=.14f; p.convergence=.72f; p.slope=.24f; p.shore=.42f; p.spread=.82f; p.space=.24f; break;
    case 3u:
        p.voices=38; p.regime=3u; p.environment=9u; p.water=.62f; p.flow=.78f; p.scale=.92f; p.turbulence=.46f;
        p.density=.48f; p.eventSize=.82f; p.eventDecay=.44f; p.contact=.88f; p.depth=.86f; p.current=.92f;
        p.convergence=.72f; p.shore=.94f; p.eddy=.42f; p.spread=.84f; p.motionRateHz=.08f; p.space=.28f; break;
    case 4u:
        p.voices=42; p.regime=4u; p.environment=0u; p.water=.72f; p.flow=.64f; p.scale=.94f; p.turbulence=.62f;
        p.density=.58f; p.eventSize=.84f; p.eventDecay=.50f; p.contact=.90f; p.depth=.78f; p.current=.72f;
        p.convergence=.98f; p.shore=1.0f; p.eddy=.58f; p.width=.82f; p.spread=.86f; p.space=.20f;
        p.surfaceLoad=.88f; p.snap=.86f; p.plateFailure=.72f; break;
    case 5u:
        p.order=4u; p.voices=48; p.regime=5u; p.environment=9u; p.water=.76f; p.flow=.84f; p.scale=1.0f; p.turbulence=.92f;
        p.aeration=.28f; p.splash=1.0f; p.density=.86f; p.eventSize=1.0f; p.eventDecay=.58f; p.contact=.96f;
        p.depth=.94f; p.current=.64f; p.slope=-.72f; p.width=1.0f; p.spread=.94f; p.space=.34f; p.outputGainDb=-10.0f;
        p.surfaceLoad=.82f; p.snap=.92f; p.plateFailure=.84f; break;
    case 6u:
        p.order=4u; p.voices=44; p.regime=6u; p.environment=0u; p.place=1u; p.water=.68f; p.flow=.72f; p.scale=.96f;
        p.turbulence=.86f; p.splash=1.0f; p.bubbles=.82f; p.density=.78f; p.eventSize=.96f; p.eventDecay=.66f;
        p.contact=.88f; p.depth=1.0f; p.current=.52f; p.slope=-.88f; p.spread=.92f; p.space=.46f; p.outputGainDb=-10.0f; break;
    case 7u:
        p.order=4u; p.voices=60; p.regime=7u; p.environment=3u; p.water=.42f; p.flow=.76f; p.scale=.72f; p.turbulence=.92f;
        p.aeration=.88f; p.splash=.46f; p.density=.86f; p.eventSize=.74f; p.eventDecay=.46f; p.contact=.68f;
        p.current=.96f; p.slope=-1.0f; p.eddy=.82f; p.foam=.92f; p.shore=.82f; p.spread=1.0f; p.outputGainDb=-10.0f; break;
    case 8u:
        p.voices=52; p.regime=8u; p.environment=2u; p.water=.34f; p.flow=.48f; p.scale=.54f; p.turbulence=.34f;
        p.aeration=.66f; p.density=.28f; p.eventSize=.24f; p.eventDecay=.32f; p.contact=.28f; p.damping=.92f;
        p.current=.58f; p.slope=-.42f; p.eddy=.36f; p.foam=1.0f; p.shore=.42f; p.spread=.92f; p.space=.16f; break;
    case 9u:
        p.voices=48; p.regime=9u; p.environment=5u; p.water=.14f; p.flow=.16f; p.scale=.18f; p.turbulence=.56f;
        p.aeration=.72f; p.drops=1.0f; p.density=.92f; p.eventSize=.24f; p.eventDecay=.12f; p.contact=1.0f;
        p.brightness=.94f; p.damping=.34f; p.current=.54f; p.slope=-.74f; p.spread=.92f; p.space=.20f; break;
    case 10u:
        p.voices=44; p.regime=10u; p.environment=6u; p.water=.30f; p.flow=.22f; p.scale=.24f; p.turbulence=.48f;
        p.aeration=.78f; p.drops=.92f; p.bubbles=.28f; p.density=.82f; p.eventSize=.28f; p.eventDecay=.18f;
        p.contact=.88f; p.brightness=.86f; p.damping=.56f; p.current=.62f; p.slope=-.64f; p.spread=.84f; p.space=.24f; break;
    case 11u:
        p.voices=36; p.regime=12u; p.environment=9u; p.place=1u; p.water=.54f; p.flow=.72f; p.scale=.78f; p.turbulence=.42f;
        p.aeration=.24f; p.splash=.24f; p.bubbles=.92f; p.density=.44f; p.eventSize=.52f; p.eventDecay=.42f;
        p.contact=.72f; p.depth=.86f; p.current=.88f; p.convergence=.58f; p.shore=.92f; p.spread=.72f; p.space=.36f;
        p.surfaceLoad=.42f; p.snap=.58f; p.plateFailure=.22f; break;
    case 12u:
        p.voices=28; p.regime=0u; p.environment=0u; p.water=.68f; p.flow=1.0f; p.scale=.46f; p.turbulence=.84f;
        p.aeration=.08f; p.drops=.06f; p.splash=.04f; p.bubbles=.24f; p.density=.62f; p.eventSize=.46f; p.eventDecay=.28f;
        p.contact=.98f; p.brightness=.88f; p.resonance=.08f; p.damping=.46f; p.current=.16f; p.convergence=.36f;
        p.surfaceLoad=1.0f; p.snap=1.0f; p.plateFailure=.82f; p.spread=.66f; p.motionRateHz=.20f; p.space=.12f; p.outputGainDb=-9.0f; break;
    case 13u:
        p.voices=8; p.regime=13u; p.environment=0u; p.water=.74f; p.flow=.04f; p.scale=.78f; p.turbulence=.18f;
        p.aeration=0.0f; p.drops=.01f; p.splash=0.0f; p.bubbles=.04f; p.density=.14f; p.eventSize=.78f; p.eventDecay=.32f;
        p.contact=.86f; p.depth=.84f; p.brightness=.46f; p.resonance=.66f; p.damping=.34f; p.current=.02f; p.convergence=.08f;
        p.surfaceLoad=.20f; p.snap=.72f; p.plateFailure=.02f; p.spread=.82f; p.deviation=.04f; p.motionRateHz=.012f;
        p.width=.88f; p.centerDistance=1.08f; p.space=.18f; p.environmentDecay=.42f; p.environmentDamping=.46f; p.outputGainDb=-11.0f; break;
    case 14u:
        p.order=4u; p.voices=32; p.regime=kAmbiCryosphereTidalShellRegime; p.environment=0u; p.place=1u; p.water=.94f; p.flow=.36f; p.scale=1.0f;
        p.turbulence=.74f; p.aeration=.02f; p.drops=.01f; p.splash=.42f; p.bubbles=.68f; p.density=.48f; p.eventSize=1.0f;
        p.eventDecay=.82f; p.contact=.76f; p.depth=1.0f; p.brightness=.20f; p.resonance=.46f; p.damping=.66f;
        p.current=.32f; p.slope=-.08f; p.eddy=.38f; p.convergence=.94f; p.width=1.0f; p.spread=1.0f; p.deviation=.18f;
        p.motionRateHz=.006f; p.centerDistance=1.42f; p.space=.56f; p.environmentSize=.94f; p.environmentDecay=.84f;
        p.environmentDamping=.72f; p.surfaceLoad=.96f; p.snap=.62f; p.plateFailure=.90f; p.outputGainDb=-12.0f; break;
    case 15u:
        p.voices=56; p.regime=kAmbiCryosphereHydrocarbonDuneRegime; p.environment=7u; p.place=4u; p.water=.46f; p.flow=.44f; p.scale=.28f;
        p.turbulence=.26f; p.aeration=.64f; p.drops=.10f; p.splash=.01f; p.bubbles=.06f; p.density=.36f; p.eventSize=.22f;
        p.eventDecay=.34f; p.contact=.44f; p.depth=.58f; p.brightness=.30f; p.resonance=.16f; p.damping=.82f;
        p.current=.62f; p.slope=-.48f; p.eddy=.24f; p.convergence=.16f; p.width=.92f; p.spread=.94f; p.deviation=.28f;
        p.motionRateHz=.026f; p.centerElevationDeg=-12.0f; p.centerDistance=1.20f; p.space=.32f; p.environmentSize=.72f;
        p.environmentDecay=.38f; p.environmentDamping=.84f; p.foam=.72f; p.surfaceLoad=.24f; p.snap=.26f;
        p.plateFailure=.08f; p.outputGainDb=-9.0f; break;
    case 16u:
        p.voices=40; p.regime=kAmbiCryosphereReactiveBrineRegime; p.environment=8u; p.place=2u; p.water=.72f; p.flow=.58f; p.scale=.66f;
        p.turbulence=.48f; p.aeration=.12f; p.drops=.03f; p.splash=.06f; p.bubbles=.98f; p.density=.46f; p.eventSize=.54f;
        p.eventDecay=.68f; p.contact=.42f; p.depth=.98f; p.brightness=.24f; p.resonance=.40f; p.damping=.70f;
        p.current=.88f; p.slope=.72f; p.eddy=.90f; p.convergence=.42f; p.shore=.46f; p.width=.72f; p.spread=.82f;
        p.deviation=.38f; p.motionRateHz=.042f; p.centerElevationDeg=-18.0f; p.centerDistance=1.28f; p.space=.64f;
        p.environmentSize=.80f; p.environmentDecay=.76f; p.environmentDamping=.74f; p.surfaceLoad=.38f; p.snap=.40f;
        p.plateFailure=.18f; p.outputGainDb=-10.0f; break;
    default:
        p.order=4u; p.voices=24; p.regime=kAmbiCryosphereAperiodicLatticeRegime; p.environment=1u; p.place=2u;
        p.water=.78f; p.flow=.46f; p.scale=.58f; p.turbulence=.92f; p.aeration=0.0f; p.drops=0.0f; p.splash=0.0f;
        p.bubbles=.12f; p.density=.74f; p.eventSize=.66f; p.eventDecay=.54f; p.contact=.82f; p.depth=.64f;
        p.brightness=.52f; p.resonance=.18f; p.damping=.58f; p.current=.30f; p.slope=.18f; p.eddy=.72f;
        p.convergence=.86f; p.width=.96f; p.spread=1.0f; p.deviation=.12f; p.motionRateHz=.006f;
        p.centerElevationDeg=14.0f; p.centerDistance=1.22f; p.space=.38f; p.environmentSize=.82f;
        p.environmentDecay=.62f; p.environmentDamping=.74f; p.surfaceLoad=.72f; p.snap=.84f; p.plateFailure=.44f;
        p.outputGainDb=-9.0f; break;
    }
    struct ScoreProfile {
        float pace;
        float occupancy;
        float cascade;
        float memory;
        float rest;
    };
    static constexpr std::array<ScoreProfile,
        kAmbiCryosphereFactoryPresetCount> scoreProfiles {{
        { .18f, .12f, .74f, .92f, .94f }, // alpine frost crack
        { .20f, .18f, .68f, .90f, .88f }, // ice segregation
        { .16f, .18f, .62f, .92f, .90f }, // permafrost heave
        { .28f, .22f, .74f, .86f, .80f }, // basal stick-slip
        { .34f, .38f, .84f, .78f, .66f }, // pressure ridge
        { .20f, .18f, .92f, .88f, .86f }, // calving front
        { .18f, .12f, .86f, .90f, .90f }, // iceberg impact
        { .62f, .72f, .82f, .48f, .30f }, // avalanche
        { .14f, .18f, .34f, .90f, .88f }, // snowpack creep
        { .82f, .82f, .10f, .28f, .18f }, // hail shower
        { .72f, .72f, .16f, .36f, .26f }, // sleet shower
        { .58f, .58f, .34f, .56f, .42f }, // meltwater under ice
        { .34f, .10f, .94f, .90f, .88f }, // moving load under foot
        { .12f, .08f, .46f, .96f, .98f }, // singing lake
        { .10f, .14f, .98f, .98f, .94f }, // tidal ocean-moon rift
        { .30f, .52f, .32f, .90f, .66f }, // methane-ice dune creep
        { .42f, .42f, .62f, .88f, .62f }, // subsurface brine upwelling
        { .12f, .03f, 1.0f, .20f, .86f }, // quasicrystal plate bloom
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
