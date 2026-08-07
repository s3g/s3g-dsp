#pragma once

#include "s3g/tracker/audio/audio_node.h"
#include "s3g/tracker/sequencer.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace s3g::tracker::audio {

struct TrackerAudioTelemetry {
    uint64_t processErrorCount = 0u;
    uint64_t droppedEventCount = 0u;
    uint64_t reorderedEventCount = 0u;
    uint64_t oversizedBlockCount = 0u;
    uint64_t nonFiniteOutputSampleCount = 0u;
    uint64_t unknownNodeEventCount = 0u;
};

// Bounded arrays of independent embedded membrane kicks and DaisySP drum
// voices feed an ACN/SN3D third-order master. Stereo samplers join the decoded
// output through a dedicated stereo bus.
// MIDI OUT is a rack instrument at the scheduler boundary and never enters
// this graph.
class TrackerAudioEngine {
public:
    TrackerAudioEngine();
    ~TrackerAudioEngine();

    TrackerAudioEngine(const TrackerAudioEngine&) = delete;
    TrackerAudioEngine& operator=(const TrackerAudioEngine&) = delete;

    bool create();
    void destroy();
    bool prepare(const AudioRenderSpec& spec, AudioLayout masterLayout);
    void release();
    bool startProcessing() noexcept;
    void stopProcessing() noexcept;
    void reset() noexcept;

    bool isCreated() const noexcept;
    bool isPrepared() const noexcept;
    bool isProcessing() const noexcept;
    AudioLayout masterLayout() const noexcept;
    uint32_t outputChannelCount() const noexcept;

    // The caller owns sequencing and supplies block-relative, time-ordered
    // canonical events. Internal-only and Both events feed the instrument;
    // MIDI-only events are ignored. Extra hardware output channels are zeroed.
    bool render(const ScheduledEvent* events, std::size_t eventCount,
        const PlanarAudioBlock& output) noexcept;

    // Realtime-safe post-decode MAIN OUT gain. This is the first true audio
    // fader in the mixer; per-track controls remain event-stage velocity trim
    // until the graph exposes separate lane buses.
    void setMainOutputGain(float normalized) noexcept;
    float mainOutputGain() const noexcept;

    // Realtime-safe control mailboxes. These methods never call into CLAP;
    // the render thread emits dirty base values at frame zero before authored
    // FX, and consumes at most one coalesced audition onset per internal node.
    bool setMembraneBaseParameter(uint32_t targetNode,
        uint32_t parameterId, float normalized) noexcept;
    bool setDaisyDrumBaseParameter(uint32_t targetNode,
        uint32_t parameterId, float normalized) noexcept;
    bool setInstrumentRackState(const InstrumentRackState& rack) noexcept;
    bool auditionInstrument(uint32_t targetNode, uint8_t note,
        float normalizedVelocity) noexcept;

    bool saveMembraneState(uint32_t targetNode,
        std::vector<uint8_t>& destination) const;
    bool loadMembraneState(uint32_t targetNode,
        const std::vector<uint8_t>& source);
    TrackerAudioTelemetry telemetry() const noexcept;
    void resetTelemetry() noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace s3g::tracker::audio
