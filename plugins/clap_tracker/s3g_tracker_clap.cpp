#include "s3g_cocoa_gui.h"

#import "s3g_song_window.h"
#import "s3g_tracker_controls.h"
#import "s3g_tracker_help_window.h"
#import "s3g_tracker_workspace.h"
#include "s3g_tracker_workspace_layout.h"

#include "s3g/tracker/command.h"
#include "s3g/tracker/project_codec.h"
#include "s3g/tracker/timing_playback_scheduler.h"

#include <clap/clap.h>
#include <clap/ext/draft/transport-control.h>

#import <CoreText/CoreText.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <utility>
#include <vector>

@class S3GTrackerClapCoordinator;

namespace {

using s3g::tracker::CommandEffect;
using s3g::tracker::CommandEngine;
using s3g::tracker::EventDestination;
using s3g::tracker::LogicalTickBoundary;
using s3g::tracker::LogicalTickBoundaryAction;
using s3g::tracker::PatternBankEntry;
using s3g::tracker::ProjectDocument;
using s3g::tracker::ScheduledEvent;
using s3g::tracker::ScheduledEventKind;
using s3g::tracker::SongLaunchQuantization;
using s3g::tracker::SongPlaybackPlanner;
using s3g::tracker::TimingPlaybackScheduler;
using s3g::tracker::TransportSettings;
using s3g::tracker::app::TrackerViewState;
using s3g::tracker::app::WorkspaceCallbacks;

constexpr uint32_t kMidiBusCount = 8u;
constexpr uint32_t kMidiChannelCount = 16u;
constexpr uint32_t kMidiNoteCount = 128u;
constexpr uint32_t kActiveNoteCount =
    kMidiBusCount * kMidiChannelCount * kMidiNoteCount;
constexpr uint32_t kRetiredRuntimeCapacity = 64u;
constexpr uint32_t kNativeWidth = 1320u;
constexpr uint32_t kNativeHeight = 780u;
constexpr uint32_t kMinimumWidth = 760u;
constexpr uint32_t kMinimumHeight = 560u;

bool syncSessionToActivePattern(TrackerViewState& state)
{
    auto* entry = state.patternBank.findEntry(state.patternBank.activePatternId);
    if (!entry) return false;
    entry->pattern = state.session.pattern;
    entry->laneDefaultNotes = state.session.laneDefaultNotes;
    entry->aliases = state.session.aliases;
    return true;
}

bool loadActivePatternIntoSession(TrackerViewState& state)
{
    const auto* entry = state.patternBank.findEntry(
        state.patternBank.activePatternId);
    if (!entry) return false;
    state.session.pattern = entry->pattern;
    state.session.laneDefaultNotes = entry->laneDefaultNotes;
    state.session.aliases = entry->aliases;
    state.session.selectedTrack = std::min<std::size_t>(
        state.session.selectedTrack,
        state.session.pattern.tracks.empty()
            ? 0u : state.session.pattern.tracks.size() - 1u);
    state.session.selectedRow = std::min<std::size_t>(
        state.session.selectedRow,
        std::max<std::size_t>(state.session.pattern.visibleRows, 1u) - 1u);
    return true;
}

std::string nextPatternId(const s3g::tracker::PatternBank& bank)
{
    for (std::size_t index = 1u;
         index <= s3g::tracker::kMaximumPatternBankEntries; ++index) {
        std::string id = "A";
        if (index < 10u) id += "0";
        id += std::to_string(index);
        if (!bank.findEntry(id)) return id;
    }
    return {};
}

PatternBankEntry newPatternEntry(const PatternBankEntry& source,
    std::string id, bool duplicate)
{
    PatternBankEntry entry = source;
    entry.id = std::move(id);
    entry.pattern.name = duplicate
        ? source.pattern.name + " COPY" : "PATTERN " + entry.id;
    if (duplicate) return entry;
    for (auto& track : entry.pattern.tracks) {
        std::fill(track.notes.begin(), track.notes.end(),
            s3g::tracker::NoteCell::rest());
        std::fill(track.instruments.begin(), track.instruments.end(),
            s3g::tracker::InstrumentCell::empty());
        std::fill(track.velocities.begin(), track.velocities.end(),
            s3g::tracker::ValueCell::defaultValue());
        for (auto& pair : track.fxPairs) {
            std::fill(pair.actions.begin(), pair.actions.end(),
                s3g::tracker::FxActionCell::empty());
            std::fill(pair.values.begin(), pair.values.end(),
                s3g::tracker::FxValueCell::previous());
        }
    }
    return entry;
}

void normalizeMidiOnlyDocument(ProjectDocument& document)
{
    auto rack = s3g::tracker::makeDefaultInstrumentRack();
    for (auto& instrument : rack.instruments)
        instrument = s3g::tracker::RackInstrument {};
    for (std::size_t slot = 0u;
         slot < s3g::tracker::kMidiOutRackSlotCount; ++slot) {
        const auto node = s3g::tracker::midiOutNodeForRackSlot(slot);
        if (const auto* definition =
                s3g::tracker::defaultRackInstrument(node)) {
            rack.instruments[slot] = *definition;
        }
        rack.midiRoutes[slot].kind =
            s3g::tracker::MidiInstrumentRouteKind::VirtualSource;
        rack.midiRoutes[slot].destinationId = 0;
        rack.midiRoutes[slot].virtualSource =
            static_cast<uint8_t>(slot + 1u);
        rack.midiRoutes[slot].channel = 1u;
    }
    rack.selectedNode = s3g::tracker::midiOutNodeForRackSlot(0u);
    document.instrumentRack = rack;

    for (auto& entry : document.patternBank.entries) {
        for (std::size_t lane = 0u; lane < entry.pattern.tracks.size();
             ++lane) {
            auto& track = entry.pattern.tracks[lane];
            auto bus = s3g::tracker::midiOutRackSlotIndex(
                track.initialInstrumentNodeId);
            if (bus >= s3g::tracker::kMidiOutRackSlotCount)
                bus = lane % s3g::tracker::kMidiOutRackSlotCount;
            const auto node = s3g::tracker::midiOutNodeForRackSlot(bus);
            track.initialInstrumentNodeId = node;
            track.destination = EventDestination::Midi;
            track.midiChannel = static_cast<uint8_t>(std::clamp<int>(
                track.midiChannel, 1, 16));
            for (auto& cell : track.instruments) {
                if (cell.state != s3g::tracker::InstrumentCellState::Instrument)
                    continue;
                if (!s3g::tracker::isMidiOutInstrumentNode(cell.nodeId))
                    cell = s3g::tracker::InstrumentCell::withInstrument(node);
            }
        }
    }
}

ProjectDocument makeInitialDocument()
{
    TrackerViewState state;
    (void)CommandEngine::execute(state.session, "demo");
    (void)CommandEngine::execute(state.session, "kit gm basic");
    state.patternBank = s3g::tracker::makeDefaultPatternBank();
    (void)syncSessionToActivePattern(state);
    state.session.transport.sampleRate = 48000.0;
    state.session.transport.ticksPerBeat = 4u;
    state.session.gateMilliseconds = 90.0;
    state.instrumentRack.midiRoutes[0u].channel = 10u;

    ProjectDocument document;
    document.patternBank = state.patternBank;
    document.transport = state.session.transport;
    document.session.gateMilliseconds = state.session.gateMilliseconds;
    document.session.commandRngState = state.session.commandRngState;
    document.session.playbackSeed = state.session.playbackSeed;
    document.instrumentRack = state.instrumentRack;
    document.song.name = "SONG";
    document.song.ticksPerBeat = state.session.transport.ticksPerBeat;
    s3g::tracker::SongRow row;
    row.patternId = state.patternBank.activePatternId;
    row.durationTicks = static_cast<uint32_t>(std::max<std::size_t>(
        state.session.pattern.visibleRows, 1u));
    row.bpm = state.session.transport.bpm;
    row.swing = state.session.transport.swing;
    document.song.rows.push_back(row);
    document.song.rows.push_back(row);
    normalizeMidiOnlyDocument(document);
    return document;
}

void registerBundledFonts()
{
    NSBundle* bundle = [NSBundle bundleForClass:
        NSClassFromString(@"S3GTrackerWorkspaceController")];
    constexpr std::array<const char*, 3u> names {{
        "IBMPlexMono-Regular", "IBMPlexMono-Medium",
        "IBMPlexMono-SemiBold",
    }};
    for (const char* name : names) {
        NSURL* url = [bundle URLForResource:[NSString stringWithUTF8String:name]
            withExtension:@"ttf" subdirectory:@"Fonts"];
        if (!url) continue;
        CFErrorRef error = nullptr;
        (void)CTFontManagerRegisterFontsForURL((__bridge CFURLRef)url,
            kCTFontManagerScopeProcess, &error);
        if (error) CFRelease(error);
    }
}

struct HostTransport {
    bool playing = false;
    bool hasBeat = false;
    double beat = 0.0;
    double tempo = 120.0;
};

HostTransport readHostTransport(const clap_event_transport_t* source)
{
    HostTransport result;
    if (!source) return result;
    result.playing = (source->flags & CLAP_TRANSPORT_IS_PLAYING) != 0u;
    if ((source->flags & CLAP_TRANSPORT_HAS_TEMPO) != 0u
        && std::isfinite(source->tempo) && source->tempo > 0.0)
        result.tempo = source->tempo;
    if ((source->flags & CLAP_TRANSPORT_HAS_BEATS_TIMELINE) != 0u) {
        result.beat = static_cast<double>(source->song_pos_beats)
            / static_cast<double>(CLAP_BEATTIME_FACTOR);
        result.hasBeat = std::isfinite(result.beat);
    } else if ((source->flags & CLAP_TRANSPORT_HAS_SECONDS_TIMELINE) != 0u) {
        const double seconds = static_cast<double>(source->song_pos_seconds)
            / static_cast<double>(CLAP_SECTIME_FACTOR);
        result.beat = seconds * result.tempo / 60.0;
        result.hasBeat = std::isfinite(result.beat);
    }
    return result;
}

struct Runtime {
    TimingPlaybackScheduler scheduler;
    SongPlaybackPlanner songPlanner;
    TransportSettings projectTransport;
    std::vector<std::string> patternIds;
    std::vector<std::size_t> songPatternIndices;
    double gateMilliseconds = 90.0;
    bool songEnabled = false;
    bool valid = false;

    Runtime(const ProjectDocument& document, double sampleRate)
        : projectTransport(document.transport)
        , gateMilliseconds(document.session.gateMilliseconds)
    {
        if (document.patternBank.entries.empty()) return;
        std::vector<s3g::tracker::Pattern> patterns;
        patterns.reserve(document.patternBank.entries.size());
        patternIds.reserve(document.patternBank.entries.size());
        std::size_t initial = 0u;
        for (std::size_t index = 0u;
             index < document.patternBank.entries.size(); ++index) {
            const auto& entry = document.patternBank.entries[index];
            patterns.push_back(entry.pattern);
            patternIds.push_back(entry.id);
            if (entry.id == document.patternBank.activePatternId)
                initial = index;
        }

        songEnabled = document.session.songPlaybackEnabled
            && !document.song.rows.empty()
            && songPlanner.setArrangement(document.song).ok();
        if (songEnabled) {
            songPatternIndices.reserve(document.song.rows.size());
            for (const auto& row : document.song.rows) {
                const auto found = std::find(patternIds.begin(),
                    patternIds.end(), row.patternId);
                if (found == patternIds.end()) {
                    songEnabled = false;
                    songPatternIndices.clear();
                    break;
                }
                songPatternIndices.push_back(static_cast<std::size_t>(
                    std::distance(patternIds.begin(), found)));
            }
            if (songEnabled && !songPatternIndices.empty())
                initial = songPatternIndices.front();
        }
        projectTransport.sampleRate = sampleRate;
        valid = scheduler.preparePatternSet(std::move(patterns), initial);
        if (!valid) return;
        scheduler.setTransport(projectTransport);
        scheduler.setRandomSeed(document.session.playbackSeed);
        if (songEnabled)
            scheduler.setLogicalTickObserver(&Runtime::advanceSong, this);
    }

    TransportSettings hostClock(double tempo, double sampleRate) const
        noexcept
    {
        auto result = projectTransport;
        result.sampleRate = sampleRate;
        if (std::isfinite(tempo) && tempo > 0.0) result.bpm = tempo;
        return result;
    }

    bool arm(double hostBeat, double tempo, double sampleRate) noexcept
    {
        if (!valid) return false;
        scheduler.setTransport(hostClock(tempo, sampleRate));
        if (songEnabled) {
            songPlanner.reset();
            if (!songPlanner.start(0u) || songPatternIndices.empty())
                return false;
            (void)scheduler.activatePreparedPatternAtTickBoundary(
                songPatternIndices.front());
            const auto* row = songPlanner.currentRow();
            scheduler.setRuntimeTrackMuteMask(row ? row->mutedTracks : 0u);
            scheduler.setLogicalTickObserver(&Runtime::advanceSong, this);
        } else {
            scheduler.setRuntimeTrackMuteMask(0u);
            scheduler.setLogicalTickObserver(nullptr, nullptr);
        }
        const bool started = scheduler.startPreparedAtHostBeat(hostBeat);
        if (started && songEnabled)
            scheduler.relaunchColumnsAtTickBoundary(0u);
        return started;
    }

    void updateClock(double tempo, double sampleRate) noexcept
    {
        scheduler.setTransport(hostClock(tempo, sampleRate));
    }

    static LogicalTickBoundaryAction advanceSong(void* context,
        const LogicalTickBoundary&) noexcept
    {
        auto& runtime = *static_cast<Runtime*>(context);
        const auto result = runtime.songPlanner.advanceTick();
        if (result.transition) {
            const auto rowIndex = runtime.songPlanner.currentRowIndex();
            const auto* row = runtime.songPlanner.currentRow();
            if (rowIndex && *rowIndex < runtime.songPatternIndices.size()) {
                (void)runtime.scheduler.activatePreparedPatternAtTickBoundary(
                    runtime.songPatternIndices[*rowIndex]);
                runtime.scheduler.relaunchColumnsAtTickBoundary(0u);
                runtime.scheduler.setRuntimeTrackMuteMask(
                    row ? row->mutedTracks : 0u);
            }
        }
        return result.finished
            ? LogicalTickBoundaryAction::StopAfterBoundary
            : LogicalTickBoundaryAction::Continue;
    }

    void routeFor(uint32_t nodeId, uint8_t fallbackChannel,
        uint8_t& bus, uint8_t& channel) const noexcept
    {
        bus = 0u;
        channel = static_cast<uint8_t>(std::clamp<int>(
            fallbackChannel, 1, 16) - 1);
        const auto slot = s3g::tracker::midiOutRackSlotIndex(nodeId);
        if (slot < kMidiBusCount) bus = static_cast<uint8_t>(slot);
    }
};

struct ActiveNote {
    uint64_t noteId = 0u;
    uint64_t dueFrame = 0u;
    bool active = false;
};

struct GateOff {
    uint32_t activeIndex = 0u;
    uint32_t frameOffset = 0u;
    uint64_t noteId = 0u;
};

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    double sampleRate = 48000.0;
    std::mutex documentMutex;
    ProjectDocument document = makeInitialDocument();
    Runtime* audioRuntime = nullptr;
    std::atomic<Runtime*> pendingRuntime { nullptr };
    std::array<Runtime*, kRetiredRuntimeCapacity> retiredRuntimes {};
    std::atomic<uint32_t> retiredRead { 0u };
    std::atomic<uint32_t> retiredWrite { 0u };
    std::array<ScheduledEvent,
        s3g::tracker::kMaximumScheduledEventsPerBlock> events {};
    std::array<GateOff, kActiveNoteCount> gateOffs {};
    std::array<ActiveNote, kActiveNoteCount> activeNotes {};
    uint64_t processFrame = 0u;
    double expectedBeat = 0.0;
    bool expectedBeatValid = false;
    bool hostWasPlaying = false;
    bool runtimeArmed = false;
    std::atomic<bool> requestPanic { false };
    std::atomic<uint32_t> auditionNode { s3g::tracker::kInvalidInstrumentNode };
    std::atomic<uint32_t> auditionData { 0u };
    std::atomic<uint32_t> auditionRevision { 0u };
    uint32_t consumedAuditionRevision = 0u;
    std::atomic<uint32_t> songLaunchRow { 0u };
    std::atomic<uint32_t> songLaunchQuantization { 0u };
    std::atomic<uint32_t> songLaunchRevision { 0u };
    uint32_t consumedSongLaunchRevision = 0u;
    std::array<std::atomic<uint16_t>, s3g::tracker::kMaximumTrackCount>
        notePlayheads {};
    std::array<std::atomic<uint8_t>, s3g::tracker::kMaximumTrackCount>
        noteHits {};
    std::array<std::atomic<uint16_t>, s3g::tracker::kMaximumTrackCount>
        instrumentPlayheads {};
    std::array<std::atomic<uint16_t>, s3g::tracker::kMaximumTrackCount>
        velocityPlayheads {};
    std::array<std::array<std::atomic<uint16_t>, s3g::tracker::kFxPairCount>,
        s3g::tracker::kMaximumTrackCount> fxActionPlayheads {};
    std::array<std::array<std::atomic<uint16_t>, s3g::tracker::kFxPairCount>,
        s3g::tracker::kMaximumTrackCount> fxValuePlayheads {};
    std::atomic<bool> visualPlaying { false };
    std::atomic<int32_t> visualSongRow { -1 };
    std::atomic<uint64_t> sentEvents { 0u };
    std::atomic<uint64_t> droppedEvents { 0u };
    S3GTrackerClapCoordinator* coordinator = nil;
    void* guiView = nullptr;
    s3g::clap_gui::ResponsiveViewport guiViewport {};
};

Plugin* self(const clap_plugin_t* plugin)
{
    return static_cast<Plugin*>(plugin->plugin_data);
}

const clap_host_transport_control_t* hostTransportControl(Plugin& plugin)
{
    if (!plugin.host || !plugin.host->get_extension) return nullptr;
    return static_cast<const clap_host_transport_control_t*>(
        plugin.host->get_extension(plugin.host, CLAP_EXT_TRANSPORT_CONTROL));
}

void markHostStateDirty(Plugin& plugin)
{
    if (plugin.host && plugin.host->request_process)
        plugin.host->request_process(plugin.host);
    if (!plugin.host || !plugin.host->get_extension) return;
    const auto* state = static_cast<const clap_host_state_t*>(
        plugin.host->get_extension(plugin.host, CLAP_EXT_STATE));
    if (state && state->mark_dirty) state->mark_dirty(plugin.host);
}

bool retireQueueFull(const Plugin& plugin) noexcept
{
    const uint32_t write = plugin.retiredWrite.load(std::memory_order_relaxed);
    const uint32_t read = plugin.retiredRead.load(std::memory_order_acquire);
    return write - read >= kRetiredRuntimeCapacity;
}

bool retireRuntimeFromAudio(Plugin& plugin, Runtime* runtime) noexcept
{
    if (!runtime) return true;
    const uint32_t write = plugin.retiredWrite.load(std::memory_order_relaxed);
    const uint32_t read = plugin.retiredRead.load(std::memory_order_acquire);
    if (write - read >= kRetiredRuntimeCapacity) return false;
    plugin.retiredRuntimes[write % kRetiredRuntimeCapacity] = runtime;
    plugin.retiredWrite.store(write + 1u, std::memory_order_release);
    return true;
}

void drainRetiredRuntimes(Plugin& plugin)
{
    uint32_t read = plugin.retiredRead.load(std::memory_order_relaxed);
    const uint32_t write = plugin.retiredWrite.load(std::memory_order_acquire);
    while (read != write) {
        delete plugin.retiredRuntimes[read % kRetiredRuntimeCapacity];
        plugin.retiredRuntimes[read % kRetiredRuntimeCapacity] = nullptr;
        ++read;
    }
    plugin.retiredRead.store(read, std::memory_order_release);
}

void publishDocument(Plugin& plugin, ProjectDocument document,
    bool markDirty)
{
    normalizeMidiOnlyDocument(document);
    auto* runtime = new (std::nothrow) Runtime(document, plugin.sampleRate);
    if (!runtime || !runtime->valid) {
        delete runtime;
        return;
    }
    {
        std::lock_guard<std::mutex> lock(plugin.documentMutex);
        plugin.document = std::move(document);
    }
    Runtime* superseded = plugin.pendingRuntime.exchange(runtime,
        std::memory_order_acq_rel);
    delete superseded;
    if (markDirty) markHostStateDirty(plugin);
    else if (plugin.host && plugin.host->request_process)
        plugin.host->request_process(plugin.host);
}

uint32_t activeNoteIndex(uint8_t bus, uint8_t channel, uint8_t note) noexcept
{
    return (static_cast<uint32_t>(bus) * kMidiChannelCount
        + static_cast<uint32_t>(channel)) * kMidiNoteCount
        + static_cast<uint32_t>(note);
}

void decodeActiveNoteIndex(uint32_t index, uint8_t& bus,
    uint8_t& channel, uint8_t& note) noexcept
{
    note = static_cast<uint8_t>(index % kMidiNoteCount);
    index /= kMidiNoteCount;
    channel = static_cast<uint8_t>(index % kMidiChannelCount);
    bus = static_cast<uint8_t>(index / kMidiChannelCount);
}

bool pushMidi(Plugin& plugin, const clap_output_events_t* output,
    uint32_t frameOffset, uint8_t bus, uint8_t status,
    uint8_t data1, uint8_t data2) noexcept
{
    if (!output || !output->try_push) {
        plugin.droppedEvents.fetch_add(1u, std::memory_order_relaxed);
        return false;
    }
    clap_event_midi_t event {};
    event.header.size = sizeof(event);
    event.header.time = frameOffset;
    event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    event.header.type = CLAP_EVENT_MIDI;
    event.port_index = bus;
    event.data[0] = status;
    event.data[1] = data1;
    event.data[2] = data2;
    if (!output->try_push(output, &event.header)) {
        plugin.droppedEvents.fetch_add(1u, std::memory_order_relaxed);
        return false;
    }
    plugin.sentEvents.fetch_add(1u, std::memory_order_relaxed);
    return true;
}

void emitActiveNoteOff(Plugin& plugin, const clap_output_events_t* output,
    uint32_t frameOffset, uint32_t index) noexcept
{
    auto& active = plugin.activeNotes[index];
    if (!active.active) return;
    uint8_t bus = 0u;
    uint8_t channel = 0u;
    uint8_t note = 0u;
    decodeActiveNoteIndex(index, bus, channel, note);
    (void)pushMidi(plugin, output, frameOffset, bus,
        static_cast<uint8_t>(0x80u | channel), note, 0u);
    active = {};
}

void releaseActiveNotes(Plugin& plugin, const clap_output_events_t* output,
    uint32_t frameOffset) noexcept
{
    for (uint32_t index = 0u; index < plugin.activeNotes.size(); ++index)
        emitActiveNoteOff(plugin, output, frameOffset, index);
}

void emitPanic(Plugin& plugin, const clap_output_events_t* output,
    uint32_t frameOffset) noexcept
{
    releaseActiveNotes(plugin, output, frameOffset);
    for (uint8_t bus = 0u; bus < kMidiBusCount; ++bus) {
        for (uint8_t channel = 0u; channel < kMidiChannelCount; ++channel) {
            (void)pushMidi(plugin, output, frameOffset, bus,
                static_cast<uint8_t>(0xb0u | channel), 123u, 0u);
        }
    }
}

void emitScheduledEvent(Plugin& plugin, Runtime& runtime,
    const clap_output_events_t* output, const ScheduledEvent& event,
    uint32_t blockOffset) noexcept
{
    if (event.kind == ScheduledEventKind::Parameter) return;
    uint8_t bus = 0u;
    uint8_t channel = 0u;
    runtime.routeFor(event.targetNode, event.channel, bus, channel);
    const uint32_t index = activeNoteIndex(bus, channel, event.note);
    const uint32_t offset = blockOffset + event.frameOffset;
    auto& active = plugin.activeNotes[index];
    if (event.kind == ScheduledEventKind::NoteOff) {
        if (active.active && (event.noteId == 0u
                || event.noteId == active.noteId))
            emitActiveNoteOff(plugin, output, offset, index);
        return;
    }
    if (active.active) emitActiveNoteOff(plugin, output, offset, index);
    const uint8_t velocity = s3g::tracker::midiVelocityFromNormalized(
        event.normalizedVelocity);
    (void)pushMidi(plugin, output, offset, bus,
        static_cast<uint8_t>(0x90u | channel), event.note, velocity);
    const uint64_t gate = event.durationSamples != 0u
        ? event.durationSamples
        : static_cast<uint64_t>(std::max(1.0,
            std::round(plugin.sampleRate * runtime.gateMilliseconds / 1000.0)));
    active.active = true;
    active.noteId = event.noteId;
    const uint64_t onset = plugin.processFrame
        + static_cast<uint64_t>(blockOffset)
        + static_cast<uint64_t>(event.frameOffset);
    active.dueFrame = onset > std::numeric_limits<uint64_t>::max() - gate
        ? std::numeric_limits<uint64_t>::max() : onset + gate;
}

std::size_t collectGateOffs(Plugin& plugin, uint64_t segmentStart,
    uint32_t frameCount) noexcept
{
    const uint64_t segmentEnd = segmentStart + frameCount;
    std::size_t count = 0u;
    for (uint32_t index = 0u; index < plugin.activeNotes.size(); ++index) {
        const auto& active = plugin.activeNotes[index];
        if (!active.active || active.dueFrame < segmentStart
            || active.dueFrame >= segmentEnd) continue;
        plugin.gateOffs[count++] = { index,
            static_cast<uint32_t>(active.dueFrame - segmentStart),
            active.noteId };
    }
    std::sort(plugin.gateOffs.begin(), plugin.gateOffs.begin() + count,
        [](const GateOff& left, const GateOff& right) {
            return left.frameOffset < right.frameOffset;
        });
    return count;
}

void handleAudition(Plugin& plugin, Runtime& runtime,
    const clap_output_events_t* output, uint32_t blockOffset) noexcept
{
    const uint32_t revision = plugin.auditionRevision.load(
        std::memory_order_acquire);
    if (revision == plugin.consumedAuditionRevision) return;
    plugin.consumedAuditionRevision = revision;
    const uint32_t node = plugin.auditionNode.load(std::memory_order_relaxed);
    const uint32_t data = plugin.auditionData.load(std::memory_order_relaxed);
    const uint8_t note = static_cast<uint8_t>(data & 0x7fu);
    const uint8_t velocity = static_cast<uint8_t>((data >> 8u) & 0x7fu);
    uint8_t bus = 0u;
    uint8_t channel = 0u;
    runtime.routeFor(node, 1u, bus, channel);
    const uint32_t index = activeNoteIndex(bus, channel, note);
    if (plugin.activeNotes[index].active)
        emitActiveNoteOff(plugin, output, blockOffset, index);
    (void)pushMidi(plugin, output, blockOffset, bus,
        static_cast<uint8_t>(0x90u | channel), note,
        std::max<uint8_t>(velocity, 1u));
    auto& active = plugin.activeNotes[index];
    active.active = true;
    active.noteId = (static_cast<uint64_t>(revision) << 32u) | node;
    active.dueFrame = plugin.processFrame + blockOffset
        + static_cast<uint64_t>(std::max(1.0,
            std::round(plugin.sampleRate * runtime.gateMilliseconds / 1000.0)));
}

void updateVisualState(Plugin& plugin, Runtime& runtime) noexcept
{
    const auto trackCount = std::min<std::size_t>(
        runtime.scheduler.pattern().tracks.size(),
        s3g::tracker::kMaximumTrackCount);
    for (std::size_t track = 0u;
         track < s3g::tracker::kMaximumTrackCount; ++track) {
        if (track >= trackCount) {
            plugin.noteHits[track].store(0u, std::memory_order_relaxed);
            continue;
        }
        plugin.notePlayheads[track].store(static_cast<uint16_t>(
            runtime.scheduler.lastNotePosition(track)),
            std::memory_order_relaxed);
        plugin.noteHits[track].store(runtime.scheduler.lastNoteTriggered(track)
                ? 1u : 0u, std::memory_order_relaxed);
        plugin.instrumentPlayheads[track].store(static_cast<uint16_t>(
            runtime.scheduler.lastInstrumentPosition(track)),
            std::memory_order_relaxed);
        plugin.velocityPlayheads[track].store(static_cast<uint16_t>(
            runtime.scheduler.lastVelocityPosition(track)),
            std::memory_order_relaxed);
        for (std::size_t pair = 0u; pair < s3g::tracker::kFxPairCount; ++pair) {
            plugin.fxActionPlayheads[track][pair].store(
                static_cast<uint16_t>(runtime.scheduler.lastFxActionPosition(
                    track, pair)), std::memory_order_relaxed);
            plugin.fxValuePlayheads[track][pair].store(
                static_cast<uint16_t>(runtime.scheduler.lastFxValuePosition(
                    track, pair)), std::memory_order_relaxed);
        }
    }
    const auto songRow = runtime.songEnabled
        ? runtime.songPlanner.currentRowIndex() : std::nullopt;
    plugin.visualSongRow.store(songRow
            ? static_cast<int32_t>(*songRow) : -1,
        std::memory_order_relaxed);
}

bool swapPendingRuntime(Plugin& plugin,
    const clap_output_events_t* output) noexcept
{
    Runtime* pending = plugin.pendingRuntime.load(std::memory_order_acquire);
    if (!pending || (plugin.audioRuntime && retireQueueFull(plugin)))
        return false;
    pending = plugin.pendingRuntime.exchange(nullptr,
        std::memory_order_acq_rel);
    if (!pending) return false;
    releaseActiveNotes(plugin, output, 0u);
    if (plugin.audioRuntime)
        (void)retireRuntimeFromAudio(plugin, plugin.audioRuntime);
    plugin.audioRuntime = pending;
    plugin.runtimeArmed = false;
    plugin.expectedBeatValid = false;
    if (plugin.host && plugin.host->request_callback)
        plugin.host->request_callback(plugin.host);
    return true;
}

void renderSegment(Plugin& plugin, const clap_output_events_t* output,
    HostTransport transport, uint32_t blockOffset, uint32_t frameCount)
    noexcept
{
    Runtime* runtime = plugin.audioRuntime;
    if (!runtime || frameCount == 0u) return;
    if (plugin.requestPanic.exchange(false, std::memory_order_acq_rel))
        emitPanic(plugin, output, blockOffset);
    handleAudition(plugin, *runtime, output, blockOffset);

    if (!transport.playing) {
        if (plugin.hostWasPlaying) {
            releaseActiveNotes(plugin, output, blockOffset);
            runtime->scheduler.stop();
        }
        plugin.hostWasPlaying = false;
        plugin.runtimeArmed = false;
        plugin.expectedBeatValid = false;
        plugin.visualPlaying.store(false, std::memory_order_relaxed);
    } else {
        const uint32_t launchRevision = plugin.songLaunchRevision.load(
            std::memory_order_acquire);
        if (launchRevision != plugin.consumedSongLaunchRevision) {
            plugin.consumedSongLaunchRevision = launchRevision;
            if (runtime->songEnabled) {
                (void)runtime->songPlanner.queueRow(
                    plugin.songLaunchRow.load(std::memory_order_relaxed),
                    static_cast<SongLaunchQuantization>(std::min<uint32_t>(
                        plugin.songLaunchQuantization.load(
                            std::memory_order_relaxed), 3u)));
            }
        }
        const bool discontinuity = transport.hasBeat
            && plugin.expectedBeatValid
            && std::abs(transport.beat - plugin.expectedBeat) > 0.01;
        const bool tempoChanged = plugin.runtimeArmed
            && std::abs(runtime->scheduler.transport().bpm
                - transport.tempo) > 1.0e-7;
        if (!plugin.runtimeArmed || !plugin.hostWasPlaying || discontinuity
            || tempoChanged) {
            releaseActiveNotes(plugin, output, blockOffset);
            plugin.runtimeArmed = runtime->arm(
                transport.hasBeat ? transport.beat : 0.0,
                transport.tempo, plugin.sampleRate);
        } else {
            runtime->updateClock(transport.tempo, plugin.sampleRate);
        }
        plugin.hostWasPlaying = true;
        plugin.visualPlaying.store(plugin.runtimeArmed,
            std::memory_order_relaxed);

        if (plugin.runtimeArmed) {
            const std::size_t eventCount = runtime->scheduler.process(
                frameCount, plugin.events.data(), plugin.events.size());
            const uint64_t segmentStart = plugin.processFrame + blockOffset;
            const std::size_t gateCount = collectGateOffs(plugin,
                segmentStart, frameCount);
            std::size_t eventIndex = 0u;
            std::size_t gateIndex = 0u;
            while (eventIndex < eventCount || gateIndex < gateCount) {
                const bool useGate = gateIndex < gateCount
                    && (eventIndex >= eventCount
                        || plugin.gateOffs[gateIndex].frameOffset
                            <= plugin.events[eventIndex].frameOffset);
                if (useGate) {
                    const auto gate = plugin.gateOffs[gateIndex++];
                    const auto& active = plugin.activeNotes[gate.activeIndex];
                    if (active.active && active.noteId == gate.noteId)
                        emitActiveNoteOff(plugin, output,
                            blockOffset + gate.frameOffset,
                            gate.activeIndex);
                } else {
                    emitScheduledEvent(plugin, *runtime, output,
                        plugin.events[eventIndex++], blockOffset);
                }
            }
            updateVisualState(plugin, *runtime);
        }
    }

    if (transport.playing && transport.hasBeat) {
        plugin.expectedBeat = transport.beat + transport.tempo
            * static_cast<double>(frameCount)
                / (60.0 * plugin.sampleRate);
        plugin.expectedBeatValid = true;
    } else {
        plugin.expectedBeatValid = false;
    }
}

} // namespace

typedef NS_ENUM(NSInteger, S3GTrackerClapPage) {
    S3GTrackerClapPageTracker = 0,
    S3GTrackerClapPageSong,
    S3GTrackerClapPageGeometry,
    S3GTrackerClapPageWarps,
    S3GTrackerClapPageConsole,
    S3GTrackerClapPageHelp,
};

@interface S3GTrackerClapPageView : NSView
@property(nonatomic, copy) NSArray<NSView*>* pageViews;
@property(nonatomic, copy) NSArray<S3GTrackerActionButton*>* pageButtons;
@property(nonatomic) S3GTrackerClapPage selectedPage;
- (instancetype)initWithPages:(NSArray<NSView*>*)pages;
- (void)showPage:(S3GTrackerClapPage)page;
@end

@implementation S3GTrackerClapPageView

- (instancetype)initWithPages:(NSArray<NSView*>*)pages
{
    self = [super initWithFrame:NSMakeRect(0.0, 0.0,
        kNativeWidth, kNativeHeight)];
    if (!self) return nil;
    self.wantsLayer = YES;
    self.layer.backgroundColor = S3GTrackerThemeColor(
        S3GTrackerThemeRole::Canvas).CGColor;
    self.accessibilityElement = YES;
    self.accessibilityRole = NSAccessibilityGroupRole;
    self.accessibilityLabel = @"s3g Tracker REAPER page workspace";
    self.pageViews = pages;

    NSArray<NSString*>* titles = @[
        @"TRACKER", @"SONG", @"GEOMETRY", @"WARPS", @"CONSOLE", @"HELP",
    ];
    NSMutableArray<S3GTrackerActionButton*>* buttons =
        [[NSMutableArray alloc] initWithCapacity:titles.count];
    for (NSUInteger index = 0u; index < titles.count; ++index) {
        S3GTrackerActionButton* button = [[S3GTrackerActionButton alloc]
            initWithFrame:NSZeroRect];
        button.title = titles[index];
        button.font = S3GTrackerFont(10.5, NSFontWeightMedium);
        button.target = self;
        button.action = @selector(pagePressed:);
        button.identifier = [NSString stringWithFormat:@"%lu",
            static_cast<unsigned long>(index)];
        button.accessibilityLabel = [titles[index]
            stringByAppendingString:@" page"];
        [self addSubview:button];
        [buttons addObject:button];
    }
    self.pageButtons = buttons;

    for (NSView* page in self.pageViews) {
        [page removeFromSuperview];
        page.translatesAutoresizingMaskIntoConstraints = YES;
        page.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        [self addSubview:page positioned:NSWindowBelow relativeTo:nil];
    }
    [self showPage:S3GTrackerClapPageTracker];
    return self;
}

- (BOOL)isFlipped { return YES; }

- (void)layout
{
    [super layout];
    constexpr CGFloat navigationHeight = 40.0;
    CGFloat x = 12.0;
    const std::array<CGFloat, 6u> widths {{
        92.0, 68.0, 96.0, 72.0, 88.0, 64.0,
    }};
    for (NSUInteger index = 0u; index < self.pageButtons.count; ++index) {
        const CGFloat width = widths[std::min<std::size_t>(
            static_cast<std::size_t>(index), widths.size() - 1u)];
        self.pageButtons[index].frame = NSMakeRect(
            x, 6.0, width, navigationHeight - 12.0);
        x += width + 6.0;
    }
    const NSRect contentFrame = NSMakeRect(0.0, navigationHeight,
        NSWidth(self.bounds), std::max<CGFloat>(0.0,
            NSHeight(self.bounds) - navigationHeight));
    for (NSView* page in self.pageViews) page.frame = contentFrame;
}

- (void)pagePressed:(NSButton*)sender
{
    [self showPage:static_cast<S3GTrackerClapPage>(
        sender.identifier.integerValue)];
}

- (void)showPage:(S3GTrackerClapPage)page
{
    const NSInteger index = static_cast<NSInteger>(page);
    if (index < 0 || index >= static_cast<NSInteger>(self.pageViews.count))
        return;
    self.selectedPage = page;
    for (NSUInteger item = 0u; item < self.pageViews.count; ++item) {
        const BOOL selected = item == static_cast<NSUInteger>(index);
        self.pageViews[item].hidden = !selected;
        self.pageViews[item].accessibilityHidden = !selected;
        self.pageButtons[item].state = selected
            ? NSControlStateValueOn : NSControlStateValueOff;
        self.pageButtons[item].tag = selected ? 1 : 0;
        [self.pageButtons[item] setNeedsDisplay:YES];
    }
    [self setNeedsLayout:YES];
    NSAccessibilityPostNotification(
        self, NSAccessibilityLayoutChangedNotification);
}

@end

@interface S3GTrackerClapCoordinator : NSObject {
@private
    Plugin* _plugin;
    std::unique_ptr<TrackerViewState> _state;
    std::unique_ptr<WorkspaceCallbacks> _callbacks;
}
@property(nonatomic, strong) S3GTrackerWorkspaceController* workspace;
@property(nonatomic, strong) S3GTrackerSongWindowController* songWindow;
@property(nonatomic, strong) S3GTrackerConsoleHelpWindowController* helpWindow;
@property(nonatomic, strong) S3GTrackerClapPageView* pageView;
@property(nonatomic, strong) NSTimer* displayTimer;
- (instancetype)initWithPlugin:(Plugin*)plugin;
- (ProjectDocument)currentDocument;
- (void)applyDocument:(const ProjectDocument&)document;
- (void)startTimer;
- (void)stopTimer;
@end

@implementation S3GTrackerClapCoordinator

- (instancetype)initWithPlugin:(Plugin*)plugin
{
    self = [super init];
    if (!self) return nil;
    _plugin = plugin;
    registerBundledFonts();
    _state = std::make_unique<TrackerViewState>();
    _callbacks = std::make_unique<WorkspaceCallbacks>();

    __weak S3GTrackerClapCoordinator* weakSelf = self;
    _callbacks->togglePlayback = [weakSelf] {
        S3GTrackerClapCoordinator* owner = weakSelf;
        if (!owner) return;
        const auto* control = hostTransportControl(*owner->_plugin);
        if (control && control->request_continue)
            control->request_continue(owner->_plugin->host);
        else {
            owner->_state->status = "Use REAPER transport to play";
            [owner.workspace reloadModel];
        }
    };
    _callbacks->pausePlayback = [weakSelf] {
        S3GTrackerClapCoordinator* owner = weakSelf;
        if (!owner) return;
        const auto* control = hostTransportControl(*owner->_plugin);
        if (!control) return;
        if (owner->_state->playing && !owner->_state->paused) {
            if (control->request_pause) control->request_pause(owner->_plugin->host);
        } else if (control->request_continue) {
            control->request_continue(owner->_plugin->host);
        }
    };
    _callbacks->stopPlayback = [weakSelf] {
        S3GTrackerClapCoordinator* owner = weakSelf;
        if (!owner) return;
        const auto* control = hostTransportControl(*owner->_plugin);
        if (control && control->request_stop)
            control->request_stop(owner->_plugin->host);
        owner->_plugin->requestPanic.store(true, std::memory_order_release);
        if (owner->_plugin->host && owner->_plugin->host->request_process)
            owner->_plugin->host->request_process(owner->_plugin->host);
    };
    _callbacks->panic = [weakSelf] {
        S3GTrackerClapCoordinator* owner = weakSelf;
        if (!owner) return;
        owner->_plugin->requestPanic.store(true, std::memory_order_release);
        if (owner->_plugin->host && owner->_plugin->host->request_process)
            owner->_plugin->host->request_process(owner->_plugin->host);
        [owner.workspace appendConsoleMessage:
            "MIDI panic queued on all eight REAPER MIDI buses" error:NO];
    };
    _callbacks->showSongWindow = [weakSelf] {
        [weakSelf.pageView showPage:S3GTrackerClapPageSong];
    };
    _callbacks->showGeometryPage = [weakSelf] {
        [weakSelf.pageView showPage:S3GTrackerClapPageGeometry];
    };
    _callbacks->showWarpPage = [weakSelf] {
        [weakSelf.pageView showPage:S3GTrackerClapPageWarps];
    };
    _callbacks->showConsoleHelp = [weakSelf] {
        [weakSelf.pageView showPage:S3GTrackerClapPageHelp];
    };
    _callbacks->instrumentRackChanged = [weakSelf] {
        [weakSelf commitProject:YES];
    };
    _callbacks->instrumentRackReloaded = [weakSelf] {
        [weakSelf commitProject:NO];
    };
    _callbacks->reportError = [weakSelf](const std::string& message) {
        S3GTrackerClapCoordinator* owner = weakSelf;
        if (!owner) return;
        owner->_state->status = message;
        [owner.workspace appendConsoleMessage:message error:YES];
    };
    _callbacks->refreshMidiDestinations = [weakSelf] {
        [weakSelf configureHostDevices];
    };
    _callbacks->refreshAudioOutputDevices = [weakSelf] {
        [weakSelf configureHostDevices];
    };
    _callbacks->selectAudioOutputDevice = [weakSelf](uint32_t) {
        S3GTrackerClapCoordinator* owner = weakSelf;
        if (!owner) return;
        owner->_state->status = "Audio device is owned by REAPER";
        [owner.workspace reloadModel];
    };
    _callbacks->patternChanged = [weakSelf] {
        [weakSelf commitProject:YES];
    };
    _callbacks->selectPattern = [weakSelf](const std::string& id) {
        [weakSelf selectPattern:id];
    };
    _callbacks->addPattern = [weakSelf](bool duplicate) {
        [weakSelf addPattern:duplicate];
    };
    _callbacks->renamePattern = [weakSelf] {
        [weakSelf renamePattern];
    };
    _callbacks->deletePattern = [weakSelf] {
        [weakSelf deletePattern];
    };
    _callbacks->transportChanged = [weakSelf] {
        [weakSelf commitProject:YES];
    };
    _callbacks->outputChanged = [weakSelf] {
        [weakSelf commitProject:YES];
    };
    _callbacks->mainOutputGainChanged = [weakSelf](float) {
        [weakSelf commitProject:YES];
    };
    _callbacks->executeCommand = [weakSelf](const std::string& command) {
        [weakSelf executeCommand:command];
    };

    self.workspace = [[S3GTrackerWorkspaceController alloc]
        initWithState:_state.get() callbacks:_callbacks.get()];
    self.songWindow = [[S3GTrackerSongWindowController alloc] init];
    self.helpWindow = [[S3GTrackerConsoleHelpWindowController alloc] init];

    ProjectDocument initial;
    {
        std::lock_guard<std::mutex> lock(_plugin->documentMutex);
        initial = _plugin->document;
    }
    [self applyDocument:initial];

    self.pageView = [[S3GTrackerClapPageView alloc] initWithPages:@[
        self.workspace.view,
        self.songWindow.window.contentView,
        [self.workspace geometryPageView],
        [self.workspace warpPageView],
        [self.workspace consolePageView],
        self.helpWindow.window.contentView,
    ]];

    self.songWindow.changeHandler = ^(NSString* summary) {
        S3GTrackerClapCoordinator* owner = weakSelf;
        if (!owner) return;
        const char* text = summary.UTF8String;
        [owner.workspace appendConsoleMessage:text
                ? std::string("Song: ") + text : "Song updated"
            error:NO];
        [owner commitProject:YES];
    };
    self.songWindow.modeChangeHandler = ^(BOOL enabled) {
        S3GTrackerClapCoordinator* owner = weakSelf;
        if (!owner) return;
        owner->_state->songPlaybackEnabled = enabled;
        [owner commitProject:YES];
    };
    self.songWindow.launchHandler = ^(NSUInteger row, NSInteger quantization) {
        S3GTrackerClapCoordinator* owner = weakSelf;
        if (!owner) return;
        owner->_plugin->songLaunchRow.store(static_cast<uint32_t>(row),
            std::memory_order_relaxed);
        owner->_plugin->songLaunchQuantization.store(
            static_cast<uint32_t>(std::clamp<NSInteger>(quantization, 0, 3)),
            std::memory_order_relaxed);
        owner->_plugin->songLaunchRevision.fetch_add(1u,
            std::memory_order_release);
        if (owner->_plugin->host && owner->_plugin->host->request_process)
            owner->_plugin->host->request_process(owner->_plugin->host);
        owner->_state->status = "Queued quantized Song row "
            + std::to_string(row + 1u);
        [owner.workspace appendConsoleMessage:owner->_state->status error:NO];
    };
    [self configureHostDevices];
    return self;
}

- (void)dealloc
{
    [self stopTimer];
    [self.songWindow close];
    [self.helpWindow close];
}

- (void)configureHostDevices
{
    if (!_state || !self.workspace) return;
    _state->midiRoute = "8 REAPER MIDI BUSES";
    _state->audioOutputDevice = "REAPER HOST AUDIO";
    _state->audioAvailable = false;
    std::vector<s3g::tracker::MidiDestination> destinations;
    s3g::tracker::MidiOutputTarget target;
    target.kind = s3g::tracker::MidiOutputTargetKind::VirtualSource;
    target.virtualSource = 1u;
    target.name = "REAPER MIDI BUS 1";
    [self.workspace setMidiDestinations:destinations selectedTarget:target];
    std::vector<s3g::tracker::app::AudioOutputDevice> devices {
        { 1u, "REAPER HOST AUDIO", _plugin->sampleRate, 0u, true },
    };
    [self.workspace setAudioOutputDevices:devices selectedDeviceId:1u];
}

- (void)refreshSongPatterns
{
    NSMutableArray<NSString*>* ids = [[NSMutableArray alloc] init];
    for (const auto& entry : _state->patternBank.entries)
        [ids addObject:[NSString stringWithUTF8String:entry.id.c_str()]];
    NSString* active = [NSString stringWithUTF8String:
        _state->patternBank.activePatternId.c_str()];
    [self.songWindow setAvailablePatternIds:ids activePatternId:active];
}

- (ProjectDocument)currentDocument
{
    ProjectDocument document;
    if (!_state) return document;
    (void)syncSessionToActivePattern(*_state);
    document.patternBank = _state->patternBank;
    document.transport = _state->session.transport;
    document.session.gateMilliseconds = _state->session.gateMilliseconds;
    document.session.mainOutputGain = _state->mainOutputGain;
    document.session.mainOutputMuted = _state->mainOutputMuted;
    document.session.songPlaybackEnabled = _state->songPlaybackEnabled;
    document.session.commandRngState = _state->session.commandRngState;
    document.session.playbackSeed = _state->session.playbackSeed;
    document.instrumentRack = _state->instrumentRack;
    document.song = [self.songWindow songArrangement];
    normalizeMidiOnlyDocument(document);
    return document;
}

- (void)applyDocument:(const ProjectDocument&)document
{
    ProjectDocument midiDocument = document;
    normalizeMidiOnlyDocument(midiDocument);
    _state->patternBank = midiDocument.patternBank;
    (void)loadActivePatternIntoSession(*_state);
    _state->session.transport = midiDocument.transport;
    _state->session.gateMilliseconds = midiDocument.session.gateMilliseconds;
    _state->session.commandRngState = midiDocument.session.commandRngState;
    _state->session.playbackSeed = midiDocument.session.playbackSeed;
    _state->instrumentRack = midiDocument.instrumentRack;
    _state->selectedRackInstrument = _state->instrumentRack.selectedNode;
    _state->mainOutputGain = midiDocument.session.mainOutputGain;
    _state->mainOutputMuted = midiDocument.session.mainOutputMuted;
    _state->songPlaybackEnabled = midiDocument.session.songPlaybackEnabled;
    _state->status = "REAPER host sync • MIDI output ready";
    [self.songWindow setSongArrangement:midiDocument.song];
    self.songWindow.playbackEnabled = _state->songPlaybackEnabled;
    [self refreshSongPatterns];
    [self.workspace reloadModel];
}

- (void)commitProject:(BOOL)dirty
{
    if (!_state) return;
    ProjectDocument document = [self currentDocument];
    _state->patternBank = document.patternBank;
    _state->instrumentRack = document.instrumentRack;
    _state->selectedRackInstrument = document.instrumentRack.selectedNode;
    (void)loadActivePatternIntoSession(*_state);
    publishDocument(*_plugin, std::move(document), dirty);
    [self.workspace reloadModel];
}

- (void)selectPattern:(const std::string&)patternId
{
    if (patternId == _state->patternBank.activePatternId
        || !_state->patternBank.findEntry(patternId)) return;
    if (!syncSessionToActivePattern(*_state)
        || !_state->patternBank.selectPattern(patternId)
        || !loadActivePatternIntoSession(*_state)) return;
    _state->status = "Selected pattern " + patternId;
    [self refreshSongPatterns];
    [self commitProject:YES];
}

- (void)addPattern:(bool)duplicate
{
    if (_state->patternBank.entries.size()
        >= s3g::tracker::kMaximumPatternBankEntries) return;
    if (!syncSessionToActivePattern(*_state)) return;
    const auto* source = _state->patternBank.findEntry(
        _state->patternBank.activePatternId);
    const std::string id = nextPatternId(_state->patternBank);
    if (!source || id.empty()) return;
    _state->patternBank.entries.push_back(newPatternEntry(
        *source, id, duplicate));
    _state->patternBank.activePatternId = id;
    (void)loadActivePatternIntoSession(*_state);
    _state->session.selectedRow = 0u;
    _state->status = duplicate ? "Duplicated pattern " + id
                               : "Created pattern " + id;
    [self refreshSongPatterns];
    [self commitProject:YES];
}

- (void)renamePattern
{
    auto* entry = _state->patternBank.findEntry(
        _state->patternBank.activePatternId);
    if (!entry) return;
    NSAlert* alert = [[NSAlert alloc] init];
    alert.messageText = @"Rename Pattern";
    [alert addButtonWithTitle:@"Rename"];
    [alert addButtonWithTitle:@"Cancel"];
    NSTextField* field = [[NSTextField alloc]
        initWithFrame:NSMakeRect(0.0, 0.0, 320.0, 24.0)];
    field.stringValue = [NSString stringWithUTF8String:entry->pattern.name.c_str()];
    alert.accessoryView = field;
    if ([alert runModal] != NSAlertFirstButtonReturn) return;
    NSString* value = [field.stringValue stringByTrimmingCharactersInSet:
        NSCharacterSet.whitespaceAndNewlineCharacterSet];
    if (value.length == 0u) return;
    const char* text = value.UTF8String;
    entry->pattern.name = text ? text : entry->pattern.name;
    _state->session.pattern.name = entry->pattern.name;
    [self commitProject:YES];
}

- (void)deletePattern
{
    if (_state->patternBank.entries.size() <= 1u) return;
    const std::string id = _state->patternBank.activePatternId;
    const auto arrangement = [self.songWindow songArrangement];
    for (const auto& row : arrangement.rows) {
        if (row.patternId == id) {
            _state->status = "Cannot delete a pattern used by Song mode";
            [self.workspace appendConsoleMessage:_state->status error:YES];
            return;
        }
    }
    const auto found = std::find_if(_state->patternBank.entries.begin(),
        _state->patternBank.entries.end(), [&](const auto& entry) {
            return entry.id == id;
        });
    if (found == _state->patternBank.entries.end()) return;
    const auto index = static_cast<std::size_t>(std::distance(
        _state->patternBank.entries.begin(), found));
    _state->patternBank.entries.erase(found);
    _state->patternBank.activePatternId = _state->patternBank.entries[
        std::min(index, _state->patternBank.entries.size() - 1u)].id;
    (void)loadActivePatternIntoSession(*_state);
    [self refreshSongPatterns];
    [self commitProject:YES];
}

- (void)executeCommand:(const std::string&)command
{
    const auto result = CommandEngine::execute(_state->session, command);
    [self.workspace appendConsoleMessage:result.message error:!result.ok];
    if (!result.ok) return;
    if (result.hasEffect(CommandEffect::PatternChanged)
        || result.hasEffect(CommandEffect::TransportChanged)
        || result.hasEffect(CommandEffect::OutputChanged)
        || result.hasEffect(CommandEffect::RoutingChanged)
        || result.hasEffect(CommandEffect::ProjectChanged))
        [self commitProject:YES];
    if (result.hasEffect(CommandEffect::StartPlayback)) {
        const auto* control = hostTransportControl(*_plugin);
        if (control && control->request_continue)
            control->request_continue(_plugin->host);
    }
    if (result.hasEffect(CommandEffect::StopPlayback)) {
        const auto* control = hostTransportControl(*_plugin);
        if (control && control->request_stop)
            control->request_stop(_plugin->host);
    }
    if (result.hasEffect(CommandEffect::Panic))
        _plugin->requestPanic.store(true, std::memory_order_release);
    [self.workspace reloadModel];
}

- (void)pollDisplay:(NSTimer*)timer
{
    (void)timer;
    drainRetiredRuntimes(*_plugin);
    _state->playing = _plugin->visualPlaying.load(std::memory_order_relaxed);
    _state->paused = false;
    for (std::size_t track = 0u;
         track < s3g::tracker::kMaximumTrackCount; ++track) {
        _state->notePlayheads[track] = _plugin->notePlayheads[track].load(
            std::memory_order_relaxed);
        _state->noteHits[track] = _plugin->noteHits[track].load(
            std::memory_order_relaxed) != 0u;
        _state->instrumentPlayheads[track]
            = _plugin->instrumentPlayheads[track].load(
                std::memory_order_relaxed);
        _state->velocityPlayheads[track]
            = _plugin->velocityPlayheads[track].load(
                std::memory_order_relaxed);
        for (std::size_t pair = 0u; pair < s3g::tracker::kFxPairCount; ++pair) {
            _state->fxActionPlayheads[track][pair]
                = _plugin->fxActionPlayheads[track][pair].load(
                    std::memory_order_relaxed);
            _state->fxValuePlayheads[track][pair]
                = _plugin->fxValuePlayheads[track][pair].load(
                    std::memory_order_relaxed);
        }
    }
    const int32_t songRow = _plugin->visualSongRow.load(
        std::memory_order_relaxed);
    _state->songPlaybackActive = _state->playing
        && _state->songPlaybackEnabled && songRow >= 0;
    _state->songPlaybackRowValid = songRow >= 0;
    _state->songPlaybackRow = songRow >= 0
        ? static_cast<std::size_t>(songRow) : 0u;
    [self.songWindow setPlaybackRow:_state->songPlaybackRow
        valid:_state->songPlaybackRowValid];
    [self.songWindow setPlaybackLocked:_state->songPlaybackActive];
    _state->sentEventCount = _plugin->sentEvents.load(
        std::memory_order_relaxed);
    _state->droppedEventCount = _plugin->droppedEvents.load(
        std::memory_order_relaxed);
    _state->status = _state->playing
        ? "REAPER PLAYING • sample-accurate CLAP MIDI"
        : "REAPER STOPPED • MIDI output ready";
    _state->lastEvent = std::to_string(_state->sentEventCount)
        + " MIDI EVENTS";
    [self.workspace refreshPlaybackDisplay];
}

- (void)startTimer
{
    if (self.displayTimer) return;
    self.displayTimer = [NSTimer scheduledTimerWithTimeInterval:(1.0 / 30.0)
        target:self selector:@selector(pollDisplay:) userInfo:nil repeats:YES];
}

- (void)stopTimer
{
    [self.displayTimer invalidate];
    self.displayTimer = nil;
}

@end

namespace {

bool init(const clap_plugin_t*) { return true; }

void guiDestroy(const clap_plugin_t* plugin);

void destroy(const clap_plugin_t* plugin)
{
    auto* instance = self(plugin);
    guiDestroy(plugin);
    delete instance->pendingRuntime.exchange(nullptr,
        std::memory_order_acq_rel);
    delete instance->audioRuntime;
    instance->audioRuntime = nullptr;
    drainRetiredRuntimes(*instance);
    delete instance;
}

bool activate(const clap_plugin_t* plugin, double sampleRate,
    uint32_t, uint32_t)
{
    if (!std::isfinite(sampleRate) || sampleRate <= 0.0) return false;
    auto* instance = self(plugin);
    instance->sampleRate = sampleRate;
    ProjectDocument document;
    {
        std::lock_guard<std::mutex> lock(instance->documentMutex);
        document = instance->document;
    }
    auto* runtime = new (std::nothrow) Runtime(document, sampleRate);
    if (!runtime || !runtime->valid) {
        delete runtime;
        return false;
    }
    delete instance->pendingRuntime.exchange(nullptr,
        std::memory_order_acq_rel);
    delete instance->audioRuntime;
    instance->audioRuntime = runtime;
    instance->processFrame = 0u;
    instance->hostWasPlaying = false;
    instance->runtimeArmed = false;
    instance->expectedBeatValid = false;
    for (auto& note : instance->activeNotes) note = {};
    return true;
}

void deactivate(const clap_plugin_t* plugin)
{
    auto* instance = self(plugin);
    if (instance->audioRuntime) instance->audioRuntime->scheduler.stop();
    instance->visualPlaying.store(false, std::memory_order_relaxed);
    instance->hostWasPlaying = false;
    instance->runtimeArmed = false;
}

bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}

void reset(const clap_plugin_t* plugin)
{
    auto* instance = self(plugin);
    if (instance->audioRuntime) instance->audioRuntime->scheduler.stop();
    for (auto& note : instance->activeNotes) note = {};
    instance->hostWasPlaying = false;
    instance->runtimeArmed = false;
    instance->expectedBeatValid = false;
    instance->visualPlaying.store(false, std::memory_order_relaxed);
}

clap_process_status process(const clap_plugin_t* plugin,
    const clap_process_t* processData)
{
    if (!processData) return CLAP_PROCESS_ERROR;
    auto& instance = *self(plugin);
    (void)swapPendingRuntime(instance, processData->out_events);
    HostTransport transport = readHostTransport(processData->transport);
    uint32_t cursor = 0u;
    const auto renderTo = [&](uint32_t end) {
        if (end <= cursor) return;
        renderSegment(instance, processData->out_events, transport,
            cursor, end - cursor);
        if (transport.playing && transport.hasBeat) {
            transport.beat += transport.tempo
                * static_cast<double>(end - cursor)
                    / (60.0 * instance.sampleRate);
        }
        cursor = end;
    };
    const uint32_t count = processData->in_events
        ? processData->in_events->size(processData->in_events) : 0u;
    for (uint32_t index = 0u; index < count; ++index) {
        const clap_event_header_t* event = processData->in_events->get(
            processData->in_events, index);
        if (!event || event->space_id != CLAP_CORE_EVENT_SPACE_ID
            || event->type != CLAP_EVENT_TRANSPORT
            || event->size < sizeof(clap_event_transport_t)) continue;
        const uint32_t time = std::min(event->time,
            processData->frames_count);
        renderTo(time);
        transport = readHostTransport(reinterpret_cast<
            const clap_event_transport_t*>(event));
        cursor = time;
    }
    renderTo(processData->frames_count);
    instance.processFrame += processData->frames_count;
    return CLAP_PROCESS_CONTINUE;
}

void onMainThread(const clap_plugin_t* plugin)
{
    drainRetiredRuntimes(*self(plugin));
}

uint32_t notePortsCount(const clap_plugin_t*, bool isInput)
{
    return isInput ? 0u : kMidiBusCount;
}

bool notePortsGet(const clap_plugin_t*, uint32_t index, bool isInput,
    clap_note_port_info_t* info)
{
    if (!info || isInput || index >= kMidiBusCount) return false;
    *info = {};
    info->id = 100u + index;
    info->supported_dialects = CLAP_NOTE_DIALECT_MIDI;
    info->preferred_dialect = CLAP_NOTE_DIALECT_MIDI;
    std::snprintf(info->name, sizeof(info->name),
        "Tracker MIDI Bus %u", index + 1u);
    return true;
}

const clap_plugin_note_ports_t notePorts {
    notePortsCount,
    notePortsGet,
};

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    if (!stream || !stream->write) return false;
    auto* instance = self(plugin);
    ProjectDocument document;
    if (instance->coordinator)
        document = [instance->coordinator currentDocument];
    else {
        std::lock_guard<std::mutex> lock(instance->documentMutex);
        document = instance->document;
    }
    std::string json;
    const auto result = s3g::tracker::encodeProjectDocument(document, json);
    if (!result.ok()) return false;
    std::size_t written = 0u;
    while (written < json.size()) {
        const int64_t amount = stream->write(stream,
            json.data() + written, json.size() - written);
        if (amount <= 0 || static_cast<uint64_t>(amount)
                > json.size() - written) return false;
        written += static_cast<std::size_t>(amount);
    }
    return true;
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) return false;
    std::string json;
    std::array<char, 8192u> buffer {};
    for (;;) {
        const int64_t amount = stream->read(stream,
            buffer.data(), buffer.size());
        if (amount < 0 || static_cast<uint64_t>(amount) > buffer.size())
            return false;
        if (amount == 0) break;
        if (json.size() + static_cast<std::size_t>(amount)
            > s3g::tracker::kMaximumProjectDocumentBytes) return false;
        json.append(buffer.data(), static_cast<std::size_t>(amount));
    }
    ProjectDocument document;
    const auto result = s3g::tracker::decodeProjectDocument(json, document);
    if (!result.ok()) return false;
    auto* instance = self(plugin);
    publishDocument(*instance, document, false);
    if (instance->coordinator)
        [instance->coordinator applyDocument:document];
    return true;
}

const clap_plugin_state_t stateExtension {
    stateSave,
    stateLoad,
};

bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool floating)
{
    return !floating && api
        && std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0;
}

bool guiGetPreferredApi(const clap_plugin_t*, const char** api,
    bool* floating)
{
    if (!api || !floating) return false;
    *api = CLAP_WINDOW_API_COCOA;
    *floating = false;
    return true;
}

bool guiCreate(const clap_plugin_t* plugin, const char* api, bool floating)
{
    if (!guiIsApiSupported(plugin, api, floating)) return false;
    auto* instance = self(plugin);
    if (instance->guiView) return true;
    instance->coordinator = [[S3GTrackerClapCoordinator alloc]
        initWithPlugin:instance];
    if (!instance->coordinator) return false;
    NSView* view = instance->coordinator.pageView;
    view.frame = NSMakeRect(0.0, 0.0, kNativeWidth, kNativeHeight);
    instance->guiView = (__bridge_retained void*)view;
    if (!s3g::clap_gui::createResponsiveViewport(instance->guiViewport,
            view, kNativeWidth, kNativeHeight, kMinimumWidth,
            kMinimumHeight)) {
        CFBridgingRelease(instance->guiView);
        instance->guiView = nullptr;
        instance->coordinator = nil;
        return false;
    }
    view.frame = NSMakeRect(0.0, 0.0,
        instance->guiViewport.width, instance->guiViewport.height);
    return true;
}

void guiDestroy(const clap_plugin_t* plugin)
{
    auto* instance = self(plugin);
    if (!instance || !instance->guiView) return;
    [instance->coordinator stopTimer];
    s3g::clap_gui::destroyResponsiveViewport(instance->guiViewport,
        instance->guiView);
    instance->coordinator = nil;
}

bool guiSetScale(const clap_plugin_t*, double) { return true; }

bool guiGetSize(const clap_plugin_t* plugin, uint32_t* width,
    uint32_t* height)
{
    return s3g::clap_gui::getResponsiveViewportSize(
        self(plugin)->guiViewport, kNativeWidth, kNativeHeight,
        width, height, kMinimumWidth, kMinimumHeight);
}

bool guiCanResize(const clap_plugin_t*) { return true; }

bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t* hints)
{
    return s3g::clap_gui::getResponsiveResizeHints(hints);
}

bool guiAdjustSize(const clap_plugin_t* plugin, uint32_t* width,
    uint32_t* height)
{
    return s3g::clap_gui::adjustResponsiveViewportSize(
        self(plugin)->guiViewport, kNativeWidth, kNativeHeight,
        width, height, kMinimumWidth, kMinimumHeight);
}

bool guiSetSize(const clap_plugin_t* plugin, uint32_t width, uint32_t height)
{
    auto* instance = self(plugin);
    if (!s3g::clap_gui::setResponsiveViewportSize(
            instance->guiViewport, width, height)) return false;
    if (instance->coordinator)
        instance->coordinator.pageView.frame = NSMakeRect(
            0.0, 0.0, width, height);
    return true;
}

bool guiSetParent(const clap_plugin_t* plugin, const clap_window_t* window)
{
    if (!window || !window->api
        || std::strcmp(window->api, CLAP_WINDOW_API_COCOA) != 0
        || !window->cocoa) return false;
    auto* instance = self(plugin);
    return s3g::clap_gui::setResponsiveViewportParent(
        instance->guiViewport, (__bridge NSView*)window->cocoa,
        instance->host);
}

bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}

bool guiShow(const clap_plugin_t* plugin)
{
    auto* instance = self(plugin);
    if (!instance->guiView || !s3g::clap_gui::setResponsiveViewportHidden(
            instance->guiViewport, false)) return false;
    [instance->coordinator startTimer];
    [instance->coordinator.pageView showPage:S3GTrackerClapPageTracker];
    [instance->coordinator.workspace focusTracker];
    return true;
}

bool guiHide(const clap_plugin_t* plugin)
{
    auto* instance = self(plugin);
    if (!instance->guiView) return false;
    [instance->coordinator stopTimer];
    return s3g::clap_gui::setResponsiveViewportHidden(
        instance->guiViewport, true);
}

const clap_plugin_gui_t guiExtension {
    guiIsApiSupported,
    guiGetPreferredApi,
    guiCreate,
    guiDestroy,
    guiSetScale,
    guiGetSize,
    guiCanResize,
    guiGetResizeHints,
    guiAdjustSize,
    guiSetSize,
    guiSetParent,
    guiSetTransient,
    guiSuggestTitle,
    guiShow,
    guiHide,
};

const void* pluginGetExtension(const clap_plugin_t*, const char* id)
{
    if (!id) return nullptr;
    if (std::strcmp(id, CLAP_EXT_NOTE_PORTS) == 0) return &notePorts;
    if (std::strcmp(id, CLAP_EXT_STATE) == 0) return &stateExtension;
    if (std::strcmp(id, CLAP_EXT_GUI) == 0) return &guiExtension;
    return nullptr;
}

const char* const features[] {
    CLAP_PLUGIN_FEATURE_NOTE_EFFECT,
    CLAP_PLUGIN_FEATURE_UTILITY,
    nullptr,
};

const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-dsp.tracker",
    "s3g Tracker",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.3.0",
    "Polymetric tracker, song sequencer, and sample-accurate MIDI generator.",
    features,
};

const clap_plugin_t* createPlugin(const clap_plugin_factory*,
    const clap_host_t* host, const char* pluginId)
{
    if (!pluginId || std::strcmp(pluginId, descriptor.id) != 0) return nullptr;
    auto* instance = new (std::nothrow) Plugin();
    if (!instance) return nullptr;
    instance->host = host;
    instance->audioRuntime = new (std::nothrow) Runtime(
        instance->document, instance->sampleRate);
    if (!instance->audioRuntime || !instance->audioRuntime->valid) {
        delete instance->audioRuntime;
        delete instance;
        return nullptr;
    }
    instance->plugin.desc = &descriptor;
    instance->plugin.plugin_data = instance;
    instance->plugin.init = init;
    instance->plugin.destroy = destroy;
    instance->plugin.activate = activate;
    instance->plugin.deactivate = deactivate;
    instance->plugin.start_processing = startProcessing;
    instance->plugin.stop_processing = stopProcessing;
    instance->plugin.reset = reset;
    instance->plugin.process = process;
    instance->plugin.get_extension = pluginGetExtension;
    instance->plugin.on_main_thread = onMainThread;
    return &instance->plugin;
}

uint32_t factoryGetPluginCount(const clap_plugin_factory*) { return 1u; }

const clap_plugin_descriptor_t* factoryGetPluginDescriptor(
    const clap_plugin_factory*, uint32_t index)
{
    return index == 0u ? &descriptor : nullptr;
}

const clap_plugin_factory_t factory {
    factoryGetPluginCount,
    factoryGetPluginDescriptor,
    createPlugin,
};

bool entryInit(const char*) { return true; }
void entryDeinit() {}

const void* entryGetFactory(const char* factoryId)
{
    return factoryId && std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0
        ? &factory : nullptr;
}

} // namespace

extern "C" const clap_plugin_entry_t clap_entry {
    CLAP_VERSION_INIT,
    entryInit,
    entryDeinit,
    entryGetFactory,
};
