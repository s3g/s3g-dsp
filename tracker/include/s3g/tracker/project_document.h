#pragma once

#include "s3g/tracker/instrument_rack.h"
#include "s3g/tracker/pattern_bank.h"
#include "s3g/tracker/phrase_library.h"
#include "s3g/tracker/sequencer.h"
#include "s3g/tracker/song_playback_planner.h"

#include <cstdint>
#include <string>
#include <vector>

namespace s3g::tracker {

// The native file is a MIDI-composition document, not a snapshot of the
// retired internal instrument rack. Version 2 is intentionally a breaking
// boundary: the decoder accepts this exact format/version pair and performs no
// migration from the former hybrid tracker/instrument schemas.
constexpr uint32_t kProjectFormatVersion = 2u;
constexpr const char* kProjectFormatIdentifier
    = "s3g-tracker-midi-composition";
constexpr const char* kProjectFileExtension = ".s3gt";

struct ProjectSessionState {
    double gateMilliseconds = 90.0;
    // Musical rate applied to the host tempo by the CLAP tracker. Values are
    // normalized by the UI to the supported ratio menu.
    double tempoScale = 1.0;
    // Pattern transport remains the default. Enabling Song transport is an
    // explicit project choice so merely opening the Song editor never changes
    // the behavior of Play.
    bool songPlaybackEnabled = false;
    // This is presentation state rather than musical data, but it belongs to
    // the native composition so reopening it restores the author's working
    // view.
    bool showMidiNoteValues = true;
    // Project-scoped Tracker navigation preference. Up/Down move this many
    // rows, making sparse entry (for example every third row) immediate.
    uint32_t trackerRowJump = 1u;
    // Stored as a decimal string by the JSON codec so all 64 bits survive a
    // round trip through tools whose JSON number type is IEEE double.
    uint64_t commandRngState = 0x7333672d74726163ull;
    // Seed consumed by deterministic playback-time probability/generative
    // actions. This remains distinct from the transactional command RNG.
    uint32_t playbackSeed = 0x6d2b79f5u;
};

struct ProjectDocument {
    PatternBank patternBank = makeDefaultPatternBank();
    BurstLibrary burstLibrary;
    PhraseLibrary phraseLibrary;
    TransportSettings transport;
    TimingWarpLibrary warpLibrary;
    ProjectSessionState session;
    InstrumentRackState instrumentRack = makeDefaultInstrumentRack();
    SongArrangement song;
};

} // namespace s3g::tracker
