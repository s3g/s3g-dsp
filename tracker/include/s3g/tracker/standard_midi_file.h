#pragma once

#include "s3g/tracker/project_codec.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace s3g::tracker {

enum class StandardMidiFileScope : uint8_t {
    ActivePattern,
    Song,
};

struct StandardMidiFileOptions {
    StandardMidiFileScope scope = StandardMidiFileScope::ActivePattern;
    // Type-1 SMF metrical division. 960 retains useful resolution for
    // Tracker microtiming while remaining widely supported.
    uint16_t ticksPerQuarterNote = 960u;
    // Offline rendering uses a fixed clock; musical positions are then
    // converted to the metrical SMF timeline with a tempo map.
    double sampleRate = 48000.0;
};

struct StandardMidiFileSummary {
    std::size_t laneTrackCount = 0u;
    std::size_t midiEventCount = 0u;
    uint64_t durationTicks = 0u;
    bool renderedSong = false;
};

// Render the selected scope through Tracker's production playback scheduler
// and encode a Standard MIDI File type 1. The result contains one tempo/meta
// track followed by one track per Tracker lane. Sequencing actions,
// probability, timing warps, swing, microtiming and Song transitions are
// materialized with the saved playback seed. Parameter actions are not MIDI;
// MIDI CC actions are exported.
ProjectResult encodeStandardMidiFile(const ProjectDocument& document,
    const StandardMidiFileOptions& options, std::string& destination,
    StandardMidiFileSummary* summary = nullptr);

} // namespace s3g::tracker
