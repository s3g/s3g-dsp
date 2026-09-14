#pragma once

#include "s3g/tracker/pitch_map.h"
#include <atomic>
#include <memory>
#include <vector>

namespace s3g::tracker {

struct PreviewHit {
    PitchPreviewEvent event;
    uint32_t offset = 0;
    uint64_t duration = 1;
    uint8_t channel = 1;
};

// One main-thread publisher and one audio-thread reader. Immutable plans are
// reclaimed ONLY on the main thread, after the audio thread acknowledges a
// newer plan. Neither MIDI scheduling nor loop boundaries involve a GUI timer.
class PreviewSequencer {
public:
    // Main thread. durationRows == 0 retains the legacy one-shot Pitch Map API.
    uint32_t publish(std::vector<PitchPreviewEvent>, uint8_t channel,
        double bpm, uint32_t ticksPerBeat, uint32_t durationRows = 0,
        bool loop = false, bool followHostTempo = false);
    void cancel(uint32_t token = 0);
    void setLoop(uint32_t token, bool enabled);
    int64_t position(uint32_t token); // -1: finished/superseded; 0: queued

    // Audio thread (or while audio is deactivated). reset consumes pending work
    // so host start/panic/reset cannot resurrect a stopped loop later.
    void reset() noexcept;
    bool beginBlock(uint32_t frames, double sampleRate, double hostBpm,
        double tempoIncrement = 0) noexcept; // true: new plan/cancellation
    bool next(PreviewHit&) noexcept;
    void endBlock() noexcept;

private:
    struct Plan {
        std::vector<PitchPreviewEvent> events;
        uint32_t token = 0, rows = 0, ticks = 4;
        uint8_t channel = 1;
        double bpm = 120;
        bool followHostTempo = false;
    };
    void reclaim();
    void report(int64_t row) noexcept;
    long double rowAt(uint32_t offset) const noexcept;
    std::vector<std::unique_ptr<Plan>> plans_; // main thread only
    std::atomic<const Plan*> pending_ {nullptr};
    std::atomic<uint32_t> acknowledged_ {0};
    std::atomic<uint64_t> progress_ {0}; // token : (row + 1), one snapshot
    std::atomic<uint32_t> loopingToken_ {0};
    uint32_t requested_ = 0; // main thread only
    const Plan* active_ = nullptr; // remaining state: audio thread only
    uint32_t consumed_ = 0, frames_ = 0;
    std::size_t event_ = 0;
    uint64_t cycle_ = 0;
    long double row_ = 0, step_ = 0, acceleration_ = 0;
    long double endRow_ = 0;
    bool looping_ = false;
};
} // namespace s3g::tracker
