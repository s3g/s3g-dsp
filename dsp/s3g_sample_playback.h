#pragma once

#include <cstdint>

namespace s3g::sample {

// Shared by Sample Player and Sample Slicer. Keeping these modes in one
// contract makes identical labels mean identical note/voice behavior across
// the Sample family.
enum class PitchMode : uint8_t {
    Rate = 0u,
    Stretch,
    RateBelowStretchAbove,
};

enum class SyncMode : uint8_t {
    Free = 0u,
    Host,
};

enum class TriggerMode : uint8_t {
    // One-shots ignore note-off while looped material releases from it.
    Auto = 0u,
    Gate,
    OneShot,
    Toggle,
};

enum class RetriggerMode : uint8_t {
    Layer = 0u,
    Restart,
    Ignore,
};

enum class VoiceMode : uint8_t {
    Poly = 0u,
    Mono,
    Legato,
};

} // namespace s3g::sample
