#pragma once
#include <array>

namespace s3g::sample_neon_gui {

// Display vocabulary only. Menu order continues to match the saved numeric
// choices; the internal Scan/Selected/Manual enum names remain unchanged.
struct Labels {
    static constexpr char stackPath[] = "STACK PATH";
    static constexpr char stackCycle[] = "STACK CYCLE";
    static constexpr char sourcePath[] = "SOURCE PATH";
    static constexpr char sourceCycle[] = "SOURCE CYCLE";
    static constexpr char editLayer[] = "EDIT LAYER";
    static constexpr char pitchSpray[] = "PITCH SPRAY";
    static constexpr char reverseChance[] = "REV CHANCE";
    static constexpr std::array<const char*, 5> layerSources {{
        "PRIMARY LAYER", editLayer, "VELOCITY", "RANDOM / TRIGGER", stackPath
    }};
    static constexpr std::array<const char*, 2> lanesNavigation {{"MANUAL", stackPath}};
    static constexpr std::array<const char*, 5> editViews {{
        "SOURCE / STACK", "PLAYBACK", stackPath, "CHARACTER FX", "ROUTING"
    }};
};

} // namespace s3g::sample_neon_gui
