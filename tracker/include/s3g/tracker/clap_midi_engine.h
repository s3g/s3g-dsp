#pragma once

#include "s3g/tracker/clap_playback_runtime.h"
#include "s3g/tracker/midi_step_recorder.h"
#include "s3g/tracker/preview_sequencer.h"
#include <algorithm>
#include <cmath>
#include <mutex>
#include <optional>

namespace s3g::tracker::midi {

using Runtime = ClapPlaybackRuntime;

constexpr uint32_t kMidiChannelCount = 16u;
constexpr uint32_t kMidiNoteCount = 128u;
constexpr uint32_t kActiveNoteCount =
    kMidiChannelCount * kMidiNoteCount;
constexpr uint32_t kMaximumGateOffsPerBlock = kActiveNoteCount
    + s3g::tracker::kMaximumScheduledEventsPerBlock;

struct HostTransport {
    bool playing = false;
    bool hasTempo = false;
    bool hasBeat = false;
    double beat = 0.0;
    double tempo = 120.0;
    double tempoIncrement = 0.0;
};

// Main-thread Burst audition request. Every field read by the audio thread is
// atomic; an odd/even revision guards the fixed-size snapshot without locks
// or allocation in process().
struct BurstPreviewMailbox {
    std::array<std::atomic<uint64_t>,
        s3g::tracker::kMaximumBurstEvents> events;
    std::atomic<uint32_t> metadata { 0u };
    std::atomic<uint32_t> bpmMilli { 120000u };
    std::atomic<uint32_t> revision { 0u };

    BurstPreviewMailbox()
    {
        for (auto& event : events) event.store(0u);
    }

    void publish(const s3g::tracker::BurstDefinition& burst,
        uint8_t midiChannel, double bpm, uint32_t ticksPerBeat) noexcept
    {
        revision.fetch_add(1u, std::memory_order_acq_rel);
        const auto count = static_cast<uint8_t>(std::min<std::size_t>(
            burst.eventCount, s3g::tracker::kMaximumBurstEvents));
        for (std::size_t index = 0u; index < count; ++index) {
            const auto& event = burst.events[index];
            const uint64_t packed = static_cast<uint64_t>(event.position)
                | (static_cast<uint64_t>(event.note) << 16u)
                | (static_cast<uint64_t>(event.velocity) << 24u)
                | (static_cast<uint64_t>(event.gatePercent) << 32u);
            events[index].store(packed, std::memory_order_relaxed);
        }
        const uint32_t channel = static_cast<uint32_t>(
            std::clamp<int>(midiChannel, 1, 16));
        const uint32_t ticks = std::clamp<uint32_t>(ticksPerBeat, 1u, 96u);
        metadata.store(static_cast<uint32_t>(count)
                | (channel << 8u) | (ticks << 16u),
            std::memory_order_relaxed);
        const double safeBpm = std::clamp(
            std::isfinite(bpm) ? bpm : 120.0, 1.0, 1000.0);
        bpmMilli.store(static_cast<uint32_t>(
            std::lround(safeBpm * 1000.0)), std::memory_order_relaxed);
        revision.fetch_add(1u, std::memory_order_release);
    }
};

struct ActiveNote {
    uint64_t noteId = 0u;
    uint64_t dueFrame = 0u;
    bool active = false;
};

struct MonitoredInputNote {
    uint8_t outputChannel = 0u;
    bool active = false;
};

struct GateOff {
    uint32_t activeIndex = 0u;
    uint32_t frameOffset = 0u;
    uint64_t noteId = 0u;
};

struct BurstPreviewPlayback {
    std::array<s3g::tracker::BurstEvent,
        s3g::tracker::kMaximumBurstEvents> events {};
    uint64_t startFrame = 0u;
    uint64_t tickFrames = 1u;
    uint32_t revision = 0u;
    uint8_t eventCount = 0u;
    uint8_t nextEvent = 0u;
    uint8_t midiChannel = 1u;
    bool active = false;
};


// Borrowed, allocation-free views. The CLAP adapter owns format conversion;
// no platform SDK, CLAP header, UI timer or window handle enters the engine.
struct MidiMessage {
    uint16_t port_index = 0;
    uint8_t data[3] {};
};

struct InputEvent {
    enum class Kind { Midi, Transport };
    Kind kind = Kind::Midi;
    uint32_t time = 0;
    MidiMessage midi;
    HostTransport transport;
};

struct InputEvents {
    const void* context = nullptr;
    uint32_t count = 0;
    bool (*get)(const void*, uint32_t, InputEvent&) noexcept = nullptr;
};

struct MidiOutput {
    const void* context = nullptr;
    bool (*try_push)(const void*, uint32_t, uint8_t, uint8_t, uint8_t) noexcept = nullptr;
};

struct ProcessData {
    uint32_t frames_count = 0;
    HostTransport transport;
    InputEvents in_events;
    const MidiOutput* out_events = nullptr;
};

struct HostServices {
    const void* context = nullptr;
    void (*requestProcess)(const void*) noexcept = nullptr;
    void (*requestCallback)(const void*) noexcept = nullptr;
    void (*markDirty)(const void*) noexcept = nullptr;
};

// Stable-address owner shared by the native wrapper and portable host tests.
// Runtime construction/deletion is confined to the main thread; process()
// returns retired objects through the bounded SPSC queue.
struct Engine {
    Engine();
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
    HostServices services;
    double sampleRate = 48000.0;
    std::mutex documentMutex;
    ProjectDocument document;
    PatternLaunchMailbox patternLaunch;
    BurstPreviewMailbox burstPreviewMailbox;
    BurstPreviewPlayback burstPreviewPlayback;
    s3g::tracker::PreviewSequencer pitchPreview;
    uint64_t pitchPreviewNoteId = 0;
    VisualNoteHitMailboxes visualNoteHits;
    MidiStepCaptureQueue midiStepCaptures;
    MidiStepClock midiStepClock;
    std::atomic<uint8_t> midiStepRecordMode {
        static_cast<uint8_t>(MidiStepRecordMode::Off)
    };
    std::atomic<uint32_t> midiRecordTrack { 0u };
    std::atomic<uint8_t> midiMonitorChannel { 0u };
    std::atomic<bool> requestMidiMonitorRelease { false };
    Runtime* audioRuntime = nullptr;
    std::atomic<Runtime*> pendingRuntime { nullptr };
    std::atomic<Runtime*> queuedVariationRuntime { nullptr };
    s3g::tracker::RuntimeRetirementQueue retiredRuntimes;
    std::array<ScheduledEvent,
        s3g::tracker::kMaximumScheduledEventsPerBlock> events {};
    std::array<GateOff, kMaximumGateOffsPerBlock> gateOffs {};
    std::array<ActiveNote, kActiveNoteCount> activeNotes {};
    std::array<MonitoredInputNote, kActiveNoteCount> monitoredInputNotes {};
    std::array<uint8_t, kActiveNoteCount> monitoredOutputCounts {};
    uint64_t processFrame = 0u;
    double expectedBeat = 0.0;
    bool expectedBeatValid = false;
    bool hostWasPlaying = false;
    bool runtimeArmed = false;
    std::atomic<bool> requestPanic { false };
    std::atomic<bool> requestRestart { false };
    std::atomic<bool> fillActive { false };
    std::atomic<uint32_t> requestTrackResyncMask { 0u };
    std::atomic<uint32_t> auditionNode { s3g::tracker::kInvalidInstrumentNode };
    std::atomic<uint32_t> auditionData { 0u };
    std::atomic<uint32_t> auditionRevision { 0u };
    uint32_t consumedAuditionRevision = 0u;
    uint32_t consumedBurstPreviewRevision = 0u;
    std::atomic<uint32_t> songLaunchRow { 0u };
    std::atomic<uint32_t> songLaunchQuantization { 0u };
    std::atomic<uint32_t> songLaunchRevision { 0u };
    uint32_t consumedSongLaunchRevision = 0u;
    // Arrangement edits use the variation-runtime handoff for audio-thread
    // safety, but they are not an explicit Song-row performance queue.
    std::atomic<bool> songArrangementUpdatePending { false };
    std::atomic<bool> songLoopEnabled { false };
    std::atomic<uint32_t> songLoopRevision { 0u };
    uint32_t consumedSongLoopRevision = 0u;
    std::array<std::atomic<uint16_t>, s3g::tracker::kMaximumTrackCount>
        notePlayheads {};
    std::array<std::atomic<uint16_t>, s3g::tracker::kMaximumTrackCount>
        instrumentPlayheads {};
    std::array<std::atomic<uint16_t>, s3g::tracker::kMaximumTrackCount>
        velocityPlayheads {};
    std::array<std::array<std::atomic<uint16_t>, s3g::tracker::kFxPairCount>,
        s3g::tracker::kMaximumTrackCount> fxActionPlayheads {};
    std::array<std::array<std::atomic<uint16_t>, s3g::tracker::kFxPairCount>,
        s3g::tracker::kMaximumTrackCount> fxValuePlayheads {};
    std::atomic<bool> visualPlaying { false };
    std::atomic<double> visualHostTempo { 0.0 };
    std::atomic<float> visualSubrowPhase { 0.0f };
    std::atomic<uint64_t> visualTimingWarpTick { 0u };
    std::atomic<int32_t> visualSongRow { -1 };
    std::atomic<int32_t> visualPendingSongRow { -1 };
    std::atomic<uint32_t> visualPendingSongQuantization { 0u };
    std::atomic<uint64_t> sentEvents { 0u };
    std::atomic<uint64_t> droppedEvents { 0u };
    std::atomic<uint64_t> runtimeBuildCount { 0u };
};


ProjectDocument makeInitialDocument();

// Publication/initialization run on the main thread (activation while stopped).
// Only process() and reset() run on the audio thread. The engine must outlive
// every callback; destroy it only once processing has stopped.
bool initialize(Engine& engine);
bool activate(Engine& engine, double sampleRate);
void deactivate(Engine& engine) noexcept;
void reset(Engine& engine) noexcept;
void process(Engine& engine, const ProcessData& data) noexcept;
void markHostStateDirty(Engine& engine);
void drainRetiredRuntimes(Engine& engine);
void cancelQueuedVariation(Engine& engine);
void storeDocumentWithoutRuntime(Engine&, ProjectDocument, bool markDirty);
bool queueRuntimeDocument(Engine&, ProjectDocument, PatternVariationLaunch,
    std::optional<std::size_t> initialSongRow, bool markDirty);
bool queueVariationDocument(Engine&, ProjectDocument, PatternVariationLaunch,
    bool markDirty);
bool queueSongDocument(Engine&, ProjectDocument, std::size_t row,
    SongLaunchQuantization, bool markDirty = false);
void publishDocument(Engine&, ProjectDocument, bool markDirty);
bool publishPreviewDocumentRuntime(Engine&, ProjectDocument);
bool publishStoredDocumentRuntime(Engine&);

} // namespace s3g::tracker::midi

