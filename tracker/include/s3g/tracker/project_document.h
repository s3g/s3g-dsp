#pragma once

#include "s3g/tracker/instrument_rack.h"
#include "s3g/tracker/pattern_bank.h"
#include "s3g/tracker/sequencer.h"
#include "s3g/tracker/song_playback_planner.h"

#include <cstdint>
#include <string>
#include <vector>

namespace s3g::tracker {

// This is the first native s3g Tracker project schema. It intentionally has
// no Max/pattr compatibility contract. A future incompatible representation
// gets a new schema version and an explicit migration rather than silently
// coercing musical data.
constexpr uint32_t kProjectSchemaVersion = 3u;
constexpr const char* kProjectFormatIdentifier = "s3g-tracker-project";
constexpr const char* kProjectFileExtension = ".s3gt";

struct ProjectSessionState {
    double gateMilliseconds = 90.0;
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
    ProjectSessionState session;
    InstrumentRackState instrumentRack = makeDefaultInstrumentRack();
    SongArrangement song;
};

} // namespace s3g::tracker
