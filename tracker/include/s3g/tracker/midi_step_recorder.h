#pragma once

#include "s3g/tracker/command.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace s3g::tracker {

enum class MidiStepRecordMode : uint8_t {
    Off,
    Step,
    LiveQuantized,
    LiveUnquantized,
};

struct MidiStepCapture {
    int64_t offsetSamples = 0;
    uint8_t note = 0u;
    uint8_t velocity = 0u;
    uint8_t channel = 1u;
    MidiStepRecordMode mode = MidiStepRecordMode::Off;
    std::size_t row = 0u;
    bool rowKnown = false;
    bool timingKnown = false;
};

// Single-producer (audio thread), single-consumer (main thread) queue. The
// producer writes a complete plain payload before publishing its write index;
// neither side allocates or locks.
class MidiStepCaptureQueue {
public:
    static constexpr uint32_t kCapacity = 128u;

    bool push(const MidiStepCapture& event) noexcept;
    bool pop(MidiStepCapture& event) noexcept;
    uint64_t droppedCount() const noexcept
    {
        return dropped_.load(std::memory_order_relaxed);
    }

private:
    std::array<MidiStepCapture, kCapacity> events_ {};
    std::atomic<uint32_t> read_ { 0u };
    std::atomic<uint32_t> write_ { 0u };
    std::atomic<uint64_t> dropped_ { 0u };
};

enum class MidiStepRecordCode : uint8_t {
    Recorded,
    Disabled,
    NoTrack,
    InvalidEvent,
    TimingUnavailable,
    FxUnavailable,
};

struct MidiStepRecordResult {
    MidiStepRecordCode code = MidiStepRecordCode::Disabled;
    std::size_t track = 0u;
    std::size_t row = 0u;
    std::size_t fxPair = kFxPairCount;
    float microTime = 0.5f;
    bool timingKnown = false;
    bool timingClamped = false;

    bool recorded() const noexcept
    {
        return code == MidiStepRecordCode::Recorded;
    }
};

// STEP writes note/velocity at the current cursor and advances one row. LIVE
// modes wrap the captured host row independently inside the selected NOTE and
// VOL column lengths without changing those lengths. The cursor follows the
// written NOTE row but does not auto-advance. Quantized recording removes
// authored MT at the target row; unquantized live recording writes MT into an
// existing MT pair or the first empty SEQ pair. The operation preflights the
// pair so a full row is left completely unchanged.
MidiStepRecordResult recordMidiStep(TrackerSession& session,
    MidiStepRecordMode mode, const MidiStepCapture& capture,
    double sampleRate);

} // namespace s3g::tracker
