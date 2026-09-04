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
    // The audio thread snapshots the explicitly armed tracker lane with the
    // MIDI event. Editing selection may change before the UI consumes the
    // queue, but the recorded destination must not.
    std::size_t targetTrack = 0u;
    uint8_t note = 0u;
    uint8_t velocity = 0u;
    uint8_t channel = 1u;
    bool noteOn = true;
    MidiStepRecordMode mode = MidiStepRecordMode::Off;
    std::size_t row = 0u;
    int64_t followingOffsetSamples = 0;
    std::size_t followingRow = 0u;
    bool rowKnown = false;
    bool followingRowKnown = false;
    bool timingKnown = false;
};

// UI-thread chord state links each admitted onset to its physical MIDI
// note-off without adding mutable recording state to the realtime sequencer.
struct MidiLiveRecordState {
    bool active = false;
    uint8_t note = 0u;
    uint8_t channel = 1u;
    std::array<uint8_t, kMaximumNoteVoices> notes {};
    std::array<uint8_t, kMaximumNoteVoices> velocities {};
    // Normalized MT values are kept in the same pitch-sorted voice order as
    // notes and velocities so a live chord can retain per-note onset timing.
    std::array<float, kMaximumNoteVoices> microTimes {};
    std::array<uint8_t, kMaximumNoteVoices> channels {};
    std::array<bool, kMaximumNoteVoices> held {};
    uint8_t voiceCount = 0u;
    std::size_t track = 0u;
    std::size_t onsetRow = 0u;

    void clear() noexcept { *this = {}; }
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
    bool release = false;
    std::size_t holdRows = 0u;

    bool recorded() const noexcept
    {
        return code == MidiStepRecordCode::Recorded;
    }
};

// STEP writes note/velocity at the current cursor and advances by the View
// JUMP interval. LIVE
// modes wrap the captured host row independently inside the armed NOTE and
// VOL column lengths without changing those lengths. The cursor follows the
// written row but lane/field selection remains independent. Quantized recording removes
// authored MT at the target row; unquantized live recording writes a
// pitch-aligned MT stack into an existing MT pair or the first empty SEQ pair.
// The operation preflights the pair so a full row is left completely unchanged.
MidiStepRecordResult recordMidiStep(TrackerSession& session,
    MidiStepRecordMode mode, const MidiStepCapture& capture,
    double sampleRate, MidiLiveRecordState* liveState = nullptr,
    std::size_t stepAdvance = 1u);

} // namespace s3g::tracker
