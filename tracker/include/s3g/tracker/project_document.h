#pragma once

#include "s3g/tracker/instrument_rack.h"
#include "s3g/tracker/pattern_bank.h"
#include "s3g/tracker/sequencer.h"
#include "s3g/tracker/song_playback_planner.h"

#include <cstdint>
#include <string>
#include <vector>

namespace s3g::tracker {

// Schema 9 adds reusable pattern-local Burst phrases and NOTE-cell references.
// Schema 8 adds the explicit Pattern timing-warp enable state. Schema 7 adds
// optional per-Song-row pattern loop ranges. Schema 6 adds MIDI CC actions and
// per-pair interpolation; schemas 5 through 7 remain readable, and schema 5
// defaults missing interpolation fields to STEP. The native
// format intentionally has no Max/pattr compatibility contract; incompatible
// representations get an explicit migration rather than silently coercing
// musical data.
constexpr uint32_t kProjectSchemaVersion = 9u;
constexpr uint32_t kOldestSupportedProjectSchemaVersion = 5u;
constexpr const char* kProjectFormatIdentifier = "s3g-tracker-project";
constexpr const char* kProjectFileExtension = ".s3gt";

struct ProjectSessionState {
    double gateMilliseconds = 90.0;
    // Musical rate applied to the host tempo by the CLAP tracker. Values are
    // normalized by the UI to the supported ratio menu.
    double tempoScale = 1.0;
    float mainOutputGain = 1.0f;
    bool mainOutputMuted = false;
    // Pattern transport remains the default. Enabling Song transport is an
    // explicit project choice so merely opening the Song editor never changes
    // the behavior of Play.
    bool songPlaybackEnabled = false;
    // Stored as a decimal string by the JSON codec so all 64 bits survive a
    // round trip through tools whose JSON number type is IEEE double.
    uint64_t commandRngState = 0x7333672d74726163ull;
    // Seed consumed by deterministic playback-time probability/generative
    // actions. This remains distinct from the transactional command RNG.
    uint32_t playbackSeed = 0x6d2b79f5u;
};

struct ProjectDocument {
    PatternBank patternBank = makeDefaultPatternBank();
    TransportSettings transport;
    TimingWarpLibrary warpLibrary;
    ProjectSessionState session;
    InstrumentRackState instrumentRack = makeDefaultInstrumentRack();
    SongArrangement song;
};

} // namespace s3g::tracker
