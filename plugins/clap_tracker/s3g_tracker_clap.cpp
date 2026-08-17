#include "s3g_cocoa_gui.h"

#import "s3g_song_window.h"
#import "s3g_tracker_controls.h"
#import "s3g_tracker_help_window.h"
#import "s3g_tracker_workspace.h"
#include "s3g_tracker_workspace_layout.h"

#include "s3g/tracker/atomic_project_store.h"
#include "s3g/tracker/command.h"
#include "s3g/tracker/fx_catalog.h"
#include "s3g/tracker/midi_step_recorder.h"
#include "s3g/tracker/project_codec.h"
#include "s3g/tracker/project_history.h"
#include "s3g/tracker/runtime_pattern_plan.h"
#include "s3g/tracker/timing_playback_scheduler.h"
#include "s3g/tracker/visual_note_hit_mailbox.h"

#include <clap/clap.h>
#include <clap/ext/draft/transport-control.h>

#import <CoreText/CoreText.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

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
#include <sstream>
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
using s3g::tracker::MidiStepCapture;
using s3g::tracker::MidiStepCaptureQueue;
using s3g::tracker::MidiStepRecordCode;
using s3g::tracker::MidiStepRecordMode;
using s3g::tracker::PatternBankEntry;
using s3g::tracker::PatternVariationLaunch;
using s3g::tracker::ProjectDocument;
using s3g::tracker::ProjectHistory;
using s3g::tracker::ScheduledEvent;
using s3g::tracker::ScheduledEventKind;
using s3g::tracker::SongLaunchQuantization;
using s3g::tracker::SongPlaybackPlanner;
using s3g::tracker::TimingPlaybackScheduler;
using s3g::tracker::TransportSettings;
using s3g::tracker::VisualNoteHitEvent;
using s3g::tracker::VisualNoteHitMailbox;
using s3g::tracker::app::TrackerViewState;
using s3g::tracker::app::WorkspaceCallbacks;

constexpr uint32_t kMidiChannelCount = 16u;
constexpr uint32_t kMidiNoteCount = 128u;
constexpr uint32_t kActiveNoteCount =
    kMidiChannelCount * kMidiNoteCount;
constexpr uint32_t kMaximumGateOffsPerBlock = kActiveNoteCount
    + s3g::tracker::kMaximumScheduledEventsPerBlock;
constexpr uint32_t kRetiredRuntimeCapacity = 64u;
constexpr uint32_t kNativeWidth = 1320u;
constexpr uint32_t kNativeHeight = 780u;
constexpr uint32_t kMinimumWidth = 760u;
constexpr uint32_t kMinimumHeight = 560u;

double normalizedTempoScale(double value) noexcept
{
    constexpr std::array<double, 7u> choices {
        0.25, 0.5, 2.0 / 3.0, 1.0, 1.5, 2.0, 4.0,
    };
    if (!std::isfinite(value)) return 1.0;
    double best = 1.0;
    double distance = std::numeric_limits<double>::infinity();
    for (const double choice : choices) {
        const double candidate = std::abs(value - choice);
        if (candidate < distance) { distance = candidate; best = choice; }
    }
    return best;
}

std::vector<std::string> commandWords(std::string_view command)
{
    std::istringstream stream { std::string(command) };
    std::vector<std::string> words;
    std::string word;
    while (stream >> word) {
        for (char& character : word) {
            if (character >= 'A' && character <= 'Z')
                character = static_cast<char>(character - 'A' + 'a');
        }
        words.push_back(std::move(word));
    }
    return words;
}

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

PatternBankEntry variationPatternEntry(const PatternBankEntry& source,
    std::string id,
    const s3g::tracker::PatternVariationRequest& variation)
{
    PatternBankEntry entry = source;
    entry.id = std::move(id);
    entry.pattern = variation.generatedSession.pattern;
    entry.laneDefaultNotes = variation.generatedSession.laneDefaultNotes;
    entry.aliases = variation.generatedSession.aliases;
    entry.pattern.name = source.pattern.name.empty()
        ? "VAR " + entry.id
        : source.pattern.name + " VAR " + entry.id;
    return entry;
}

void normalizeMidiOnlyDocument(ProjectDocument& document)
{
    document.session.tempoScale = normalizedTempoScale(
        document.session.tempoScale);
    auto rack = s3g::tracker::makeDefaultInstrumentRack();
    for (auto& instrument : rack.instruments)
        instrument = s3g::tracker::RackInstrument {};
    const auto midiNode = s3g::tracker::midiOutNodeForRackSlot(0u);
    if (const auto* definition =
            s3g::tracker::defaultRackInstrument(midiNode)) {
        rack.instruments[0u] = *definition;
    }
    rack.midiRoutes[0u].kind =
        s3g::tracker::MidiInstrumentRouteKind::VirtualSource;
    rack.midiRoutes[0u].destinationId = 0;
    rack.midiRoutes[0u].virtualSource = 1u;
    rack.midiRoutes[0u].channel = 1u;
    rack.selectedNode = midiNode;
    document.instrumentRack = rack;
    for (auto& row : document.song.rows) row.bpm.reset();

    for (auto& entry : document.patternBank.entries) {
        for (std::size_t lane = 0u; lane < entry.pattern.tracks.size();
             ++lane) {
            auto& track = entry.pattern.tracks[lane];
            track.initialInstrumentNodeId = midiNode;
            track.destination = EventDestination::Midi;
            track.midiChannel = static_cast<uint8_t>(std::clamp<int>(
                track.midiChannel, 1, 16));
            // Routing is owned by the lane's channel header. Older bus and INS
            // assignments collapse onto the plug-in's single CLAP note port.
            std::fill(track.instruments.begin(), track.instruments.end(),
                s3g::tracker::InstrumentCell::empty());
            for (auto& pair : track.fxPairs) {
                for (auto& action : pair.actions) {
                    if (action.state
                        == s3g::tracker::FxActionCellState::Parameter) {
                        action = s3g::tracker::FxActionCell::empty();
                    }
                }
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
    document.warpLibrary = state.session.warpLibrary;
    document.session.gateMilliseconds = state.session.gateMilliseconds;
    document.session.tempoScale = 1.0;
    document.session.commandRngState = state.session.commandRngState;
    document.session.playbackSeed = state.session.playbackSeed;
    document.instrumentRack = state.instrumentRack;
    document.song.name = "SONG";
    document.song.ticksPerBeat = state.session.transport.ticksPerBeat;
    s3g::tracker::SongRow row;
    row.patternId = state.patternBank.activePatternId;
    row.durationTicks = static_cast<uint32_t>(std::max<std::size_t>(
        state.session.pattern.visibleRows, 1u));
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

struct PatternLaunchMailbox {
    std::atomic<uint32_t> revision { 0u };
    std::atomic<uint32_t> consumedRevision { 0u };
    std::atomic<uint32_t> dueRevision { 0u };
    std::atomic<uint32_t> quantization { 0u };
};

struct MidiStepClock {
    void clear() noexcept
    {
        sequence.fetch_add(1u, std::memory_order_acq_rel);
        valid.store(false, std::memory_order_relaxed);
        sequence.fetch_add(1u, std::memory_order_release);
    }

    void publish(uint64_t current, uint64_t next) noexcept
    {
        sequence.fetch_add(1u, std::memory_order_acq_rel);
        currentFrame.store(current, std::memory_order_relaxed);
        nextFrame.store(std::max(current, next), std::memory_order_relaxed);
        valid.store(true, std::memory_order_relaxed);
        sequence.fetch_add(1u, std::memory_order_release);
    }

    bool nearestOffset(uint64_t frame, int64_t& offset) const noexcept
    {
        for (unsigned attempt = 0u; attempt < 4u; ++attempt) {
            const uint64_t before = sequence.load(std::memory_order_acquire);
            if ((before & 1u) != 0u) continue;
            const bool available = valid.load(std::memory_order_relaxed);
            const uint64_t current = currentFrame.load(
                std::memory_order_relaxed);
            const uint64_t next = nextFrame.load(std::memory_order_relaxed);
            const uint64_t after = sequence.load(std::memory_order_acquire);
            if (before != after || (after & 1u) != 0u) continue;
            if (!available || frame < current) return false;
            const uint64_t currentDistance = frame - current;
            const uint64_t nextDistance = next >= frame
                ? next - frame : std::numeric_limits<uint64_t>::max();
            const bool chooseNext = next > current
                && nextDistance < currentDistance;
            const uint64_t magnitude = chooseNext
                ? nextDistance : currentDistance;
            const uint64_t bounded = std::min<uint64_t>(magnitude,
                static_cast<uint64_t>(std::numeric_limits<int64_t>::max()));
            offset = chooseNext ? -static_cast<int64_t>(bounded)
                                : static_cast<int64_t>(bounded);
            return true;
        }
        return false;
    }

    std::atomic<uint64_t> sequence { 0u };
    std::atomic<uint64_t> currentFrame { 0u };
    std::atomic<uint64_t> nextFrame { 0u };
    std::atomic<bool> valid { false };
};

using VisualNoteHitMailboxes = std::array<VisualNoteHitMailbox,
    s3g::tracker::kMaximumTrackCount>;

struct Runtime {
    TimingPlaybackScheduler scheduler;
    SongPlaybackPlanner songPlanner;
    TransportSettings projectTransport;
    std::vector<std::string> patternIds;
    std::vector<std::size_t> songPatternIndices;
    double gateMilliseconds = 90.0;
    double tempoScale = 1.0;
    bool songEnabled = false;
    bool valid = false;
    PatternLaunchMailbox* patternLaunch = nullptr;
    VisualNoteHitMailboxes* visualNoteHits = nullptr;
    MidiStepClock* midiStepClock = nullptr;
    uint64_t absoluteFrameOrigin = 0u;

    Runtime(const ProjectDocument& document, double sampleRate,
        PatternLaunchMailbox* launchMailbox = nullptr,
        VisualNoteHitMailboxes* hitMailboxes = nullptr,
        MidiStepClock* stepClock = nullptr)
        : projectTransport(document.transport)
        , gateMilliseconds(document.session.gateMilliseconds)
        , tempoScale(std::clamp(document.session.tempoScale, 0.25, 4.0))
        , patternLaunch(launchMailbox)
        , visualNoteHits(hitMailboxes)
        , midiStepClock(stepClock)
    {
        s3g::tracker::RuntimePatternPlan plan;
        if (!s3g::tracker::makeRuntimePatternPlan(document, plan)) return;
        std::vector<s3g::tracker::Pattern> patterns;
        patterns.reserve(plan.documentPatternIndices.size());
        patternIds.reserve(plan.documentPatternIndices.size());
        for (const auto index : plan.documentPatternIndices) {
            const auto& entry = document.patternBank.entries[index];
            patterns.push_back(entry.pattern);
            patternIds.push_back(entry.id);
        }

        songEnabled = plan.songEnabled
            && songPlanner.setArrangement(document.song).ok();
        if (songEnabled) {
            songPatternIndices = plan.songPatternIndices;
        }
        projectTransport.sampleRate = sampleRate;
        valid = scheduler.preparePatternSet(std::move(patterns),
            plan.initialPatternIndex);
        if (!valid) return;
        scheduler.setTimingWarpLibrary(document.warpLibrary);
        scheduler.setTransport(projectTransport);
        scheduler.setRandomSeed(document.session.playbackSeed);
        scheduler.setLogicalTickObserver(&Runtime::advanceLogicalTick, this);
    }

    TransportSettings hostClock(double tempo, double sampleRate) const
        noexcept
    {
        auto result = projectTransport;
        result.sampleRate = sampleRate;
        if (std::isfinite(tempo) && tempo > 0.0)
            result.bpm = tempo * tempoScale;
        return result;
    }

    bool arm(double hostBeat, double tempo, double sampleRate,
        uint64_t absoluteStartFrame, bool forceAllRows = false) noexcept
    {
        if (!valid) return false;
        absoluteFrameOrigin = absoluteStartFrame;
        if (midiStepClock) midiStepClock->clear();
        scheduler.setTransport(hostClock(tempo, sampleRate));
        if (songEnabled) {
            songPlanner.reset();
            if (!songPlanner.start(0u) || songPatternIndices.empty())
                return false;
            (void)scheduler.activatePreparedPatternAtTickBoundary(
                songPatternIndices.front());
            const auto* row = songPlanner.currentRow();
            scheduler.setRuntimeTrackMuteMask(row ? row->mutedTracks : 0u);
        } else {
            scheduler.setRuntimeTrackMuteMask(0u);
        }
        scheduler.setLogicalTickObserver(&Runtime::advanceLogicalTick, this);
        const bool started = scheduler.startPreparedAtHostBeat(hostBeat);
        if (started && forceAllRows)
            scheduler.resyncAllTrackColumnsAtTickBoundary(0u);
        else if (started && songEnabled)
            scheduler.relaunchColumnsAtTickBoundary(0u);
        return started;
    }

    void updateClock(double tempo, double sampleRate) noexcept
    {
        // Host clock refreshes happen every process segment. Preserve a WRP
        // composition recalled by the scheduler and update only the fields
        // that the host actually owns.
        auto current = scheduler.transport();
        current.sampleRate = sampleRate;
        if (std::isfinite(tempo) && tempo > 0.0)
            current.bpm = tempo * tempoScale;
        scheduler.setTransport(std::move(current));
    }

    void publishVisualNoteHits(const LogicalTickBoundary& boundary) noexcept
    {
        if (midiStepClock) {
            const auto addOrigin = [&](uint64_t frame) {
                return frame > std::numeric_limits<uint64_t>::max()
                        - absoluteFrameOrigin
                    ? std::numeric_limits<uint64_t>::max()
                    : absoluteFrameOrigin + frame;
            };
            midiStepClock->publish(addOrigin(boundary.absoluteSampleTime),
                addOrigin(scheduler.nextTickSampleFrame()));
        }
        if (!visualNoteHits) return;
        const auto trackCount = std::min<std::size_t>(
            scheduler.pattern().tracks.size(), visualNoteHits->size());
        for (std::size_t track = 0u; track < trackCount; ++track) {
            if (!scheduler.lastNoteTriggered(track)) continue;
            (*visualNoteHits)[track].publish(
                scheduler.lastNotePosition(track),
                boundary.absoluteSampleTime);
        }
    }

    static LogicalTickBoundaryAction advanceLogicalTick(void* context,
        const LogicalTickBoundary& boundary) noexcept
    {
        auto& runtime = *static_cast<Runtime*>(context);
        runtime.publishVisualNoteHits(boundary);
        if (!runtime.songEnabled)
            return advancePatternLaunch(context, boundary);
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

    static LogicalTickBoundaryAction advancePatternLaunch(void* context,
        const LogicalTickBoundary& boundary) noexcept
    {
        auto& runtime = *static_cast<Runtime*>(context);
        if (!runtime.patternLaunch)
            return LogicalTickBoundaryAction::Continue;
        auto& mailbox = *runtime.patternLaunch;
        const uint32_t revision = mailbox.revision.load(
            std::memory_order_acquire);
        if (revision == 0u || revision == mailbox.consumedRevision.load(
                std::memory_order_relaxed))
            return LogicalTickBoundaryAction::Continue;

        const auto quantization = static_cast<PatternVariationLaunch>(
            mailbox.quantization.load(std::memory_order_relaxed));
        const bool due = s3g::tracker::patternVariationLaunchIsDue(
            quantization, boundary.completedTickIndex,
            boundary.completedTransportRow,
            runtime.scheduler.transport().ticksPerBeat,
            runtime.scheduler.pattern().visibleRows);
        if (!due) return LogicalTickBoundaryAction::Continue;
        mailbox.consumedRevision.store(revision, std::memory_order_relaxed);
        mailbox.dueRevision.store(revision, std::memory_order_release);
        return LogicalTickBoundaryAction::StopAfterBoundary;
    }

    void routeFor(uint8_t fallbackChannel, uint8_t& channel) const noexcept
    {
        channel = static_cast<uint8_t>(std::clamp<int>(
            fallbackChannel, 1, 16) - 1);
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
    PatternLaunchMailbox patternLaunch;
    VisualNoteHitMailboxes visualNoteHits;
    MidiStepCaptureQueue midiStepCaptures;
    MidiStepClock midiStepClock;
    std::atomic<uint8_t> midiStepRecordMode {
        static_cast<uint8_t>(MidiStepRecordMode::Off)
    };
    Runtime* audioRuntime = nullptr;
    std::atomic<Runtime*> pendingRuntime { nullptr };
    std::atomic<Runtime*> queuedVariationRuntime { nullptr };
    std::array<Runtime*, kRetiredRuntimeCapacity> retiredRuntimes {};
    std::atomic<uint32_t> retiredRead { 0u };
    std::atomic<uint32_t> retiredWrite { 0u };
    std::array<ScheduledEvent,
        s3g::tracker::kMaximumScheduledEventsPerBlock> events {};
    std::array<GateOff, kMaximumGateOffsPerBlock> gateOffs {};
    std::array<ActiveNote, kActiveNoteCount> activeNotes {};
    uint64_t processFrame = 0u;
    double expectedBeat = 0.0;
    bool expectedBeatValid = false;
    bool hostWasPlaying = false;
    bool runtimeArmed = false;
    std::atomic<bool> requestPanic { false };
    std::atomic<bool> requestRestart { false };
    std::atomic<uint32_t> requestTrackResyncMask { 0u };
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
    std::atomic<int32_t> visualSongRow { -1 };
    std::atomic<int32_t> visualPendingSongRow { -1 };
    std::atomic<uint32_t> visualPendingSongQuantization { 0u };
    std::atomic<uint64_t> sentEvents { 0u };
    std::atomic<uint64_t> droppedEvents { 0u };
    std::atomic<uint64_t> runtimeBuildCount { 0u };
    S3GTrackerClapCoordinator* coordinator = nil;
    void* guiView = nullptr;
    s3g::clap_gui::ResponsiveViewport guiViewport {};
};

// The audio callback commonly renders a block shortly before the downstream
// instrument presents it. Keep one 60 Hz GUI frame in hand so the playhead is
// not painted from the newly rendered block ahead of the audible onset.
struct VisualPlaybackFrame {
    std::array<std::size_t, s3g::tracker::kMaximumTrackCount>
        notePlayheads {};
    std::array<bool, s3g::tracker::kMaximumTrackCount> noteHits {};
    std::array<std::size_t, s3g::tracker::kMaximumTrackCount> noteHitRows {};
    std::array<uint64_t, s3g::tracker::kMaximumTrackCount>
        noteHitSampleTimes {};
    std::array<std::size_t, s3g::tracker::kMaximumTrackCount>
        instrumentPlayheads {};
    std::array<std::size_t, s3g::tracker::kMaximumTrackCount>
        velocityPlayheads {};
    std::array<std::array<std::size_t, s3g::tracker::kFxPairCount>,
        s3g::tracker::kMaximumTrackCount> fxActionPlayheads {};
    std::array<std::array<std::size_t, s3g::tracker::kFxPairCount>,
        s3g::tracker::kMaximumTrackCount> fxValuePlayheads {};
    int32_t songRow = -1;
    int32_t pendingSongRow = -1;
    uint32_t pendingSongQuantization = 0u;
};

Plugin* self(const clap_plugin_t* plugin)
{
    return static_cast<Plugin*>(plugin->plugin_data);
}

void captureMidiStep(Plugin& plugin, const clap_event_midi_t& event,
    uint32_t frameOffset) noexcept
{
    const auto mode = static_cast<MidiStepRecordMode>(
        plugin.midiStepRecordMode.load(std::memory_order_relaxed));
    if (mode == MidiStepRecordMode::Off || event.port_index != 0u) return;
    const uint8_t status = event.data[0];
    if ((status & 0xf0u) != 0x90u || event.data[2] == 0u) return;
    MidiStepCapture capture;
    capture.note = static_cast<uint8_t>(event.data[1] & 0x7fu);
    capture.velocity = static_cast<uint8_t>(event.data[2] & 0x7fu);
    capture.channel = static_cast<uint8_t>((status & 0x0fu) + 1u);
    capture.mode = mode;
    const uint64_t absoluteFrame = frameOffset
            > std::numeric_limits<uint64_t>::max() - plugin.processFrame
        ? std::numeric_limits<uint64_t>::max()
        : plugin.processFrame + frameOffset;
    capture.timingKnown = plugin.midiStepClock.nearestOffset(
        absoluteFrame, capture.offsetSamples);
    (void)plugin.midiStepCaptures.push(capture);
}

const clap_host_transport_control_t* hostTransportControl(Plugin& plugin)
{
    if (!plugin.host || !plugin.host->get_extension) return nullptr;
    return static_cast<const clap_host_transport_control_t*>(
        plugin.host->get_extension(plugin.host, CLAP_EXT_TRANSPORT_CONTROL));
}

// REAPER exposes this prefix of reaper_plugin_info_t to CLAP plug-ins through
// "cockos.reaper_extension". Keep the bridge local and minimal so Tracker can
// use the host's documented transport buttons without taking on the complete
// REAPER extension SDK.
struct ReaperHostBridge {
    int callerVersion;
    void* mainWindow;
    int (*registerObject)(const char*, void*);
    void* (*getFunction)(const char*);
};

const ReaperHostBridge* reaperHostBridge(Plugin& plugin)
{
    if (!plugin.host || !plugin.host->get_extension) return nullptr;
    return static_cast<const ReaperHostBridge*>(plugin.host->get_extension(
        plugin.host, "cockos.reaper_extension"));
}

using ReaperTransportButton = void (*)();

ReaperTransportButton reaperTransportButton(Plugin& plugin,
    const char* name)
{
    const auto* bridge = reaperHostBridge(plugin);
    if (!bridge || !bridge->getFunction) return nullptr;
    return reinterpret_cast<ReaperTransportButton>(
        bridge->getFunction(name));
}

bool requestHostContinue(Plugin& plugin)
{
    const auto* control = hostTransportControl(plugin);
    if (control && control->request_continue) {
        control->request_continue(plugin.host);
        return true;
    }
    if (const auto play = reaperTransportButton(plugin, "OnPlayButton")) {
        play();
        return true;
    }
    return false;
}

bool requestHostStop(Plugin& plugin)
{
    const auto* control = hostTransportControl(plugin);
    if (control && control->request_stop) {
        control->request_stop(plugin.host);
        return true;
    }
    if (const auto stop = reaperTransportButton(plugin, "OnStopButton")) {
        stop();
        return true;
    }
    return false;
}

bool requestHostTogglePlayback(Plugin& plugin)
{
    const auto* control = hostTransportControl(plugin);
    if (control && control->request_toggle_play) {
        control->request_toggle_play(plugin.host);
        return true;
    }
    const bool playing = plugin.visualPlaying.load(std::memory_order_relaxed);
    if (control) {
        if (playing && control->request_pause) {
            control->request_pause(plugin.host);
            return true;
        }
        if (!playing && control->request_continue) {
            control->request_continue(plugin.host);
            return true;
        }
    }
    const auto button = reaperTransportButton(plugin,
        playing ? "OnPauseButton" : "OnPlayButton");
    if (!button) return false;
    button();
    return true;
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

void cancelQueuedVariation(Plugin& plugin)
{
    plugin.patternLaunch.revision.store(0u, std::memory_order_release);
    plugin.patternLaunch.dueRevision.store(0u, std::memory_order_release);
    plugin.patternLaunch.consumedRevision.store(0u,
        std::memory_order_relaxed);
    delete plugin.queuedVariationRuntime.exchange(nullptr,
        std::memory_order_acq_rel);
}

void storeDocumentWithoutRuntime(Plugin& plugin, ProjectDocument document,
    bool markDirty)
{
    normalizeMidiOnlyDocument(document);
    {
        std::lock_guard<std::mutex> lock(plugin.documentMutex);
        plugin.document = std::move(document);
    }
    if (markDirty) markHostStateDirty(plugin);
}

bool queueVariationDocument(Plugin& plugin, ProjectDocument document,
    PatternVariationLaunch quantization, bool markDirty)
{
    normalizeMidiOnlyDocument(document);
    auto* runtime = new (std::nothrow) Runtime(document,
        plugin.sampleRate, &plugin.patternLaunch, &plugin.visualNoteHits,
        &plugin.midiStepClock);
    if (!runtime || !runtime->valid) {
        delete runtime;
        return false;
    }
    plugin.runtimeBuildCount.fetch_add(1u, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(plugin.documentMutex);
        plugin.document = std::move(document);
    }
    delete plugin.pendingRuntime.exchange(nullptr,
        std::memory_order_acq_rel);
    Runtime* superseded = plugin.queuedVariationRuntime.exchange(runtime,
        std::memory_order_acq_rel);
    delete superseded;
    plugin.patternLaunch.quantization.store(
        static_cast<uint32_t>(quantization), std::memory_order_relaxed);
    plugin.patternLaunch.dueRevision.store(0u, std::memory_order_relaxed);
    auto revision = plugin.patternLaunch.revision.load(
        std::memory_order_relaxed) + 1u;
    if (revision == 0u) revision = 1u;
    plugin.patternLaunch.revision.store(revision,
        std::memory_order_release);
    if (markDirty) markHostStateDirty(plugin);
    else if (plugin.host && plugin.host->request_process)
        plugin.host->request_process(plugin.host);
    return true;
}

void publishDocument(Plugin& plugin, ProjectDocument document,
    bool markDirty)
{
    normalizeMidiOnlyDocument(document);
    auto* runtime = new (std::nothrow) Runtime(document, plugin.sampleRate,
        &plugin.patternLaunch, &plugin.visualNoteHits,
        &plugin.midiStepClock);
    if (!runtime || !runtime->valid) {
        delete runtime;
        return;
    }
    plugin.runtimeBuildCount.fetch_add(1u, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(plugin.documentMutex);
        plugin.document = std::move(document);
    }
    cancelQueuedVariation(plugin);
    Runtime* superseded = plugin.pendingRuntime.exchange(runtime,
        std::memory_order_acq_rel);
    delete superseded;
    if (markDirty) markHostStateDirty(plugin);
    else if (plugin.host && plugin.host->request_process)
        plugin.host->request_process(plugin.host);
}

bool publishStoredDocumentRuntime(Plugin& plugin)
{
    ProjectDocument document;
    {
        std::lock_guard<std::mutex> lock(plugin.documentMutex);
        document = plugin.document;
    }
    auto* runtime = new (std::nothrow) Runtime(document, plugin.sampleRate,
        &plugin.patternLaunch, &plugin.visualNoteHits,
        &plugin.midiStepClock);
    if (!runtime || !runtime->valid) {
        delete runtime;
        return false;
    }
    plugin.runtimeBuildCount.fetch_add(1u, std::memory_order_relaxed);
    cancelQueuedVariation(plugin);
    Runtime* superseded = plugin.pendingRuntime.exchange(runtime,
        std::memory_order_acq_rel);
    delete superseded;
    if (plugin.host && plugin.host->request_process)
        plugin.host->request_process(plugin.host);
    return true;
}

uint32_t activeNoteIndex(uint8_t channel, uint8_t note) noexcept
{
    return static_cast<uint32_t>(channel) * kMidiNoteCount
        + static_cast<uint32_t>(note);
}

void decodeActiveNoteIndex(uint32_t index, uint8_t& channel,
    uint8_t& note) noexcept
{
    note = static_cast<uint8_t>(index % kMidiNoteCount);
    index /= kMidiNoteCount;
    channel = static_cast<uint8_t>(index % kMidiChannelCount);
}

bool pushMidi(Plugin& plugin, const clap_output_events_t* output,
    uint32_t frameOffset, uint8_t status, uint8_t data1,
    uint8_t data2) noexcept
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
    event.port_index = 0u;
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
    uint8_t channel = 0u;
    uint8_t note = 0u;
    decodeActiveNoteIndex(index, channel, note);
    (void)pushMidi(plugin, output, frameOffset,
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
    for (uint8_t channel = 0u; channel < kMidiChannelCount; ++channel) {
        (void)pushMidi(plugin, output, frameOffset,
            static_cast<uint8_t>(0xb0u | channel), 123u, 0u);
    }
}

void emitScheduledEvent(Plugin& plugin, Runtime& runtime,
    const clap_output_events_t* output, const ScheduledEvent& event,
    uint32_t blockOffset) noexcept
{
    if (event.kind == ScheduledEventKind::Parameter) return;
    uint8_t channel = 0u;
    runtime.routeFor(event.channel, channel);
    const uint32_t index = activeNoteIndex(channel, event.note);
    const uint32_t offset = blockOffset + event.frameOffset;
    auto& active = plugin.activeNotes[index];
    if (event.kind == ScheduledEventKind::NoteOff) {
        if (active.active && (event.noteId == 0u
                || event.noteId == active.noteId))
            emitActiveNoteOff(plugin, output, offset, index);
        return;
    }
    // MIDI 1.0 has no note identity beyond channel and pitch. Use a
    // deterministic latest-note-wins policy: retrigger the pitch explicitly,
    // replace its noteId/dueFrame below, and let the noteId check suppress the
    // superseded gate-off so it cannot cut the new onset short.
    if (active.active) emitActiveNoteOff(plugin, output, offset, index);
    const uint8_t velocity = s3g::tracker::midiVelocityFromNormalized(
        event.normalizedVelocity);
    (void)pushMidi(plugin, output, offset,
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
        if (!active.active || active.dueFrame >= segmentEnd) continue;
        plugin.gateOffs[count++] = { index,
            active.dueFrame <= segmentStart ? 0u
                : static_cast<uint32_t>(active.dueFrame - segmentStart),
            active.noteId };
    }
    const auto later = [](const GateOff& left, const GateOff& right) {
        if (left.frameOffset != right.frameOffset)
            return left.frameOffset > right.frameOffset;
        return left.noteId > right.noteId;
    };
    std::make_heap(plugin.gateOffs.begin(),
        plugin.gateOffs.begin() + count, later);
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
    uint8_t channel = 0u;
    runtime.routeFor(1u, channel);
    const uint32_t index = activeNoteIndex(channel, note);
    if (plugin.activeNotes[index].active)
        emitActiveNoteOff(plugin, output, blockOffset, index);
    (void)pushMidi(plugin, output, blockOffset,
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
        if (track >= trackCount) continue;
        plugin.notePlayheads[track].store(static_cast<uint16_t>(
            runtime.scheduler.lastNotePosition(track)),
            std::memory_order_relaxed);
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
    const auto pendingSongRow = runtime.songEnabled
        ? runtime.songPlanner.pendingRowIndex() : std::nullopt;
    const auto pendingSongQuantization = runtime.songEnabled
        ? runtime.songPlanner.pendingQuantization() : std::nullopt;
    plugin.visualPendingSongRow.store(pendingSongRow
            ? static_cast<int32_t>(*pendingSongRow) : -1,
        std::memory_order_relaxed);
    plugin.visualPendingSongQuantization.store(pendingSongQuantization
            ? static_cast<uint32_t>(*pendingSongQuantization) : 0u,
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

bool swapQueuedVariationRuntime(Plugin& plugin,
    const clap_output_events_t* output) noexcept
{
    const uint32_t due = plugin.patternLaunch.dueRevision.load(
        std::memory_order_acquire);
    if (due == 0u) return false;
    Runtime* queued = plugin.queuedVariationRuntime.load(
        std::memory_order_acquire);
    if (!queued) {
        plugin.patternLaunch.dueRevision.store(0u,
            std::memory_order_release);
        plugin.runtimeArmed = false;
        return false;
    }
    if (plugin.audioRuntime && retireQueueFull(plugin)) return false;
    queued = plugin.queuedVariationRuntime.exchange(nullptr,
        std::memory_order_acq_rel);
    if (!queued) return false;
    releaseActiveNotes(plugin, output, 0u);
    if (plugin.audioRuntime)
        (void)retireRuntimeFromAudio(plugin, plugin.audioRuntime);
    plugin.audioRuntime = queued;
    plugin.runtimeArmed = false;
    plugin.expectedBeatValid = false;
    plugin.patternLaunch.dueRevision.store(0u, std::memory_order_release);
    plugin.patternLaunch.revision.store(0u, std::memory_order_release);
    plugin.patternLaunch.consumedRevision.store(0u,
        std::memory_order_relaxed);
    if (plugin.host && plugin.host->request_callback)
        plugin.host->request_callback(plugin.host);
    return true;
}

void renderSegment(Plugin& plugin, const clap_output_events_t* output,
    HostTransport transport, uint32_t blockOffset, uint32_t frameCount)
    noexcept
{
    plugin.visualHostTempo.store(transport.tempo,
        std::memory_order_relaxed);
    Runtime* runtime = plugin.audioRuntime;
    if (!runtime || frameCount == 0u) return;
    if (plugin.requestPanic.exchange(false, std::memory_order_acq_rel))
        emitPanic(plugin, output, blockOffset);
    handleAudition(plugin, *runtime, output, blockOffset);

    if (!transport.playing) {
        plugin.midiStepClock.clear();
        if (plugin.hostWasPlaying) {
            releaseActiveNotes(plugin, output, blockOffset);
            runtime->scheduler.stop();
        }
        plugin.hostWasPlaying = false;
        plugin.runtimeArmed = false;
        plugin.expectedBeatValid = false;
        plugin.visualPlaying.store(false, std::memory_order_relaxed);
        plugin.visualPendingSongRow.store(-1, std::memory_order_relaxed);
    } else {
        const bool restartRequested = plugin.requestRestart.exchange(false,
            std::memory_order_acq_rel);
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
                - transport.tempo * runtime->tempoScale) > 1.0e-7;
        if (restartRequested) {
            releaseActiveNotes(plugin, output, blockOffset);
            plugin.runtimeArmed = runtime->arm(
                0.0, transport.tempo, plugin.sampleRate,
                plugin.processFrame + blockOffset, true);
            plugin.expectedBeatValid = false;
        } else if (!plugin.runtimeArmed || !plugin.hostWasPlaying
            || discontinuity || tempoChanged) {
            releaseActiveNotes(plugin, output, blockOffset);
            plugin.runtimeArmed = runtime->arm(
                transport.hasBeat ? transport.beat : 0.0,
                transport.tempo, plugin.sampleRate,
                plugin.processFrame + blockOffset);
        } else {
            runtime->updateClock(transport.tempo, plugin.sampleRate);
        }
        plugin.hostWasPlaying = true;
        plugin.visualPlaying.store(plugin.runtimeArmed,
            std::memory_order_relaxed);

        if (plugin.runtimeArmed) {
            const uint32_t resyncMask = plugin.requestTrackResyncMask.exchange(
                0u, std::memory_order_acq_rel);
            for (std::size_t track = 0u;
                 track < s3g::tracker::kMaximumTrackCount; ++track) {
                if ((resyncMask & (uint32_t { 1u } << track)) != 0u)
                    (void)runtime->scheduler
                        .resyncTrackColumnsAtTickBoundary(track, 0u);
            }
            const std::size_t eventCount = runtime->scheduler.process(
                frameCount, plugin.events.data(), plugin.events.size());
            const uint64_t segmentStart = plugin.processFrame + blockOffset;
            const std::size_t gateCount = collectGateOffs(plugin,
                segmentStart, frameCount);
            std::size_t eventIndex = 0u;
            std::size_t pendingGateCount = gateCount;
            const auto laterGate = [](const GateOff& left,
                                       const GateOff& right) {
                if (left.frameOffset != right.frameOffset)
                    return left.frameOffset > right.frameOffset;
                return left.noteId > right.noteId;
            };
            while (eventIndex < eventCount || pendingGateCount > 0u) {
                const bool useGate = pendingGateCount > 0u
                    && (eventIndex >= eventCount
                        || plugin.gateOffs.front().frameOffset
                            <= plugin.events[eventIndex].frameOffset);
                if (useGate) {
                    std::pop_heap(plugin.gateOffs.begin(),
                        plugin.gateOffs.begin() + pendingGateCount,
                        laterGate);
                    const auto gate = plugin.gateOffs[--pendingGateCount];
                    const auto& active = plugin.activeNotes[gate.activeIndex];
                    if (active.active && active.noteId == gate.noteId)
                        emitActiveNoteOff(plugin, output,
                            blockOffset + gate.frameOffset,
                            gate.activeIndex);
                } else {
                    const auto event = plugin.events[eventIndex++];
                    emitScheduledEvent(plugin, *runtime, output, event,
                        blockOffset);
                    if (event.kind != ScheduledEventKind::NoteOn) continue;
                    uint8_t channel = 0u;
                    runtime->routeFor(event.channel, channel);
                    const uint32_t activeIndex = activeNoteIndex(
                        channel, event.note);
                    const auto& active = plugin.activeNotes[activeIndex];
                    const uint64_t segmentEnd = segmentStart + frameCount;
                    if (!active.active || active.dueFrame >= segmentEnd
                        || pendingGateCount >= plugin.gateOffs.size())
                        continue;
                    plugin.gateOffs[pendingGateCount++] = {
                        activeIndex,
                        active.dueFrame <= segmentStart ? 0u
                            : static_cast<uint32_t>(
                                active.dueFrame - segmentStart),
                        active.noteId,
                    };
                    std::push_heap(plugin.gateOffs.begin(),
                        plugin.gateOffs.begin() + pendingGateCount,
                        laterGate);
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

@interface S3GTrackerClapPageView : NSView <NSWindowDelegate>
@property(nonatomic, copy) NSArray<NSView*>* pageViews;
@property(nonatomic, copy) NSArray<NSView*>* pageHosts;
@property(nonatomic, copy) NSArray<NSTextField*>* detachedPlaceholders;
@property(nonatomic, copy) NSArray<S3GTrackerActionButton*>* pageButtons;
@property(nonatomic, strong) S3GTrackerActionButton* popoutButton;
@property(nonatomic, strong) NSMutableDictionary<NSNumber*, NSWindow*>*
    detachedWindows;
@property(nonatomic) S3GTrackerClapPage selectedPage;
- (instancetype)initWithPages:(NSArray<NSView*>*)pages;
- (void)showPage:(S3GTrackerClapPage)page;
- (BOOL)pageCanDetach:(S3GTrackerClapPage)page;
- (void)toggleDetachPage:(S3GTrackerClapPage)page;
- (void)reattachPage:(S3GTrackerClapPage)page closeWindow:(BOOL)closeWindow;
- (void)detachFromPlugin;
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
    self.detachedWindows = [[NSMutableDictionary alloc] init];

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

    NSMutableArray<NSView*>* hosts = [[NSMutableArray alloc]
        initWithCapacity:self.pageViews.count];
    NSMutableArray<NSTextField*>* placeholders = [[NSMutableArray alloc]
        initWithCapacity:self.pageViews.count];
    for (NSUInteger index = 0u; index < self.pageViews.count; ++index) {
        NSView* host = [[NSView alloc] initWithFrame:NSZeroRect];
        host.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        host.wantsLayer = YES;
        host.layer.backgroundColor = S3GTrackerThemeColor(
            S3GTrackerThemeRole::Canvas).CGColor;
        [self addSubview:host positioned:NSWindowBelow relativeTo:nil];
        NSTextField* placeholder = [NSTextField labelWithString:
            @"THIS PAGE IS OPEN IN A DETACHED WINDOW"];
        placeholder.font = S3GTrackerFont(11.0, NSFontWeightMedium);
        placeholder.textColor = S3GTrackerThemeColor(
            S3GTrackerThemeRole::TextMuted);
        placeholder.alignment = NSTextAlignmentCenter;
        placeholder.hidden = YES;
        [host addSubview:placeholder];
        [hosts addObject:host];
        [placeholders addObject:placeholder];

        NSView* page = self.pageViews[index];
        [page removeFromSuperview];
        page.translatesAutoresizingMaskIntoConstraints = YES;
        page.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        [host addSubview:page];
    }
    self.pageHosts = hosts;
    self.detachedPlaceholders = placeholders;

    self.popoutButton = [[S3GTrackerActionButton alloc]
        initWithFrame:NSZeroRect];
    self.popoutButton.title = @"↗";
    self.popoutButton.font = S3GTrackerFont(13.0, NSFontWeightMedium);
    self.popoutButton.target = self;
    self.popoutButton.action = @selector(popoutPressed:);
    self.popoutButton.accessibilityLabel = @"Detach selected tool page";
    [self addSubview:self.popoutButton];
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
    self.popoutButton.frame = NSMakeRect(
        std::max<CGFloat>(12.0, NSWidth(self.bounds) - 48.0),
        6.0, 36.0, navigationHeight - 12.0);
    const NSRect contentFrame = NSMakeRect(0.0, navigationHeight,
        NSWidth(self.bounds), std::max<CGFloat>(0.0,
            NSHeight(self.bounds) - navigationHeight));
    for (NSUInteger index = 0u; index < self.pageHosts.count; ++index) {
        NSView* host = self.pageHosts[index];
        host.frame = contentFrame;
        self.detachedPlaceholders[index].frame = NSMakeRect(20.0,
            std::max<CGFloat>(20.0, NSMidY(host.bounds) - 10.0),
            std::max<CGFloat>(1.0, NSWidth(host.bounds) - 40.0), 20.0);
        if (!self.detachedWindows[@(index)])
            self.pageViews[index].frame = host.bounds;
    }
}

- (void)pagePressed:(NSButton*)sender
{
    const auto page = static_cast<S3GTrackerClapPage>(
        sender.identifier.integerValue);
    [self showPage:page];
    if (NSApp.currentEvent.clickCount >= 2 && [self pageCanDetach:page]) {
        [self toggleDetachPage:page];
    }
}

- (BOOL)pageCanDetach:(S3GTrackerClapPage)page
{
    return page == S3GTrackerClapPageGeometry
        || page == S3GTrackerClapPageWarps
        || page == S3GTrackerClapPageConsole
        || page == S3GTrackerClapPageHelp;
}

- (void)popoutPressed:(id)sender
{
    (void)sender;
    [self toggleDetachPage:self.selectedPage];
}

- (void)toggleDetachPage:(S3GTrackerClapPage)page
{
    if (![self pageCanDetach:page]) return;
    NSNumber* key = @(static_cast<NSInteger>(page));
    NSWindow* existing = self.detachedWindows[key];
    if (existing) {
        [self reattachPage:page closeWindow:YES];
        return;
    }
    const NSInteger index = static_cast<NSInteger>(page);
    if (index < 0 || index >= static_cast<NSInteger>(self.pageViews.count))
        return;
    NSWindow* parentWindow = self.window;
    NSView* content = self.pageViews[static_cast<NSUInteger>(index)];
    [content removeFromSuperview];
    NSWindow* window = [[NSWindow alloc] initWithContentRect:
        NSMakeRect(0.0, 0.0, 920.0, 660.0)
        styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
            | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable)
        backing:NSBackingStoreBuffered defer:NO];
    NSArray<NSString*>* names = @[
        @"Tracker", @"Song", @"Rhythm Geometry", @"Timing Warps",
        @"Console", @"Help",
    ];
    window.title = [@"s3g Tracker — " stringByAppendingString:
        names[static_cast<NSUInteger>(index)]];
    window.minSize = NSMakeSize(480.0, 360.0);
    window.backgroundColor = S3GTrackerThemeColor(
        S3GTrackerThemeRole::Canvas);
    window.appearance = [NSAppearance appearanceNamed:
        NSAppearanceNameDarkAqua];
    window.releasedWhenClosed = NO;
    window.tabbingMode = NSWindowTabbingModeDisallowed;
    window.hidesOnDeactivate = YES;
    window.collectionBehavior |=
        NSWindowCollectionBehaviorFullScreenAuxiliary;
    window.delegate = self;
    content.frame = window.contentView.bounds;
    content.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    window.contentView = content;
    self.detachedWindows[key] = window;
    self.detachedPlaceholders[static_cast<NSUInteger>(index)].hidden = NO;
    if (parentWindow)
        [parentWindow addChildWindow:window ordered:NSWindowAbove];
    window.level = parentWindow
        ? std::max<NSInteger>(
            NSFloatingWindowLevel, parentWindow.level + 1)
        : NSFloatingWindowLevel;
    [window center];
    [window makeKeyAndOrderFront:nil];
    [self showPage:page];
}

- (void)reattachPage:(S3GTrackerClapPage)page closeWindow:(BOOL)closeWindow
{
    NSNumber* key = @(static_cast<NSInteger>(page));
    NSWindow* window = self.detachedWindows[key];
    if (!window) return;
    const NSUInteger index = static_cast<NSUInteger>(page);
    NSView* content = self.pageViews[index];
    window.delegate = nil;
    if (window.parentWindow)
        [window.parentWindow removeChildWindow:window];
    window.contentView = [[NSView alloc] initWithFrame:NSZeroRect];
    [content removeFromSuperview];
    [self.pageHosts[index] addSubview:content];
    content.frame = self.pageHosts[index].bounds;
    self.detachedPlaceholders[index].hidden = YES;
    [self.detachedWindows removeObjectForKey:key];
    if (closeWindow) [window close];
    else [window orderOut:nil];
    [self showPage:page];
}

- (BOOL)windowShouldClose:(NSWindow*)sender
{
    for (NSNumber* key in self.detachedWindows.allKeys) {
        if (self.detachedWindows[key] != sender) continue;
        [self reattachPage:static_cast<S3GTrackerClapPage>(
            key.integerValue) closeWindow:NO];
        return NO;
    }
    return YES;
}

- (void)detachFromPlugin
{
    for (NSNumber* key in self.detachedWindows.allKeys.copy)
        [self reattachPage:static_cast<S3GTrackerClapPage>(
            key.integerValue) closeWindow:YES];
}

- (void)showPage:(S3GTrackerClapPage)page
{
    const NSInteger index = static_cast<NSInteger>(page);
    if (index < 0 || index >= static_cast<NSInteger>(self.pageViews.count))
        return;
    self.selectedPage = page;
    for (NSUInteger item = 0u; item < self.pageHosts.count; ++item) {
        const BOOL selected = item == static_cast<NSUInteger>(index);
        self.pageHosts[item].hidden = !selected;
        self.pageHosts[item].accessibilityHidden = !selected;
        self.pageButtons[item].state = selected
            ? NSControlStateValueOn : NSControlStateValueOff;
        self.pageButtons[item].tag = selected ? 1 : 0;
        [self.pageButtons[item] setNeedsDisplay:YES];
    }
    const BOOL detachable = [self pageCanDetach:page];
    self.popoutButton.hidden = !detachable;
    const BOOL detached = self.detachedWindows[@(index)] != nil;
    self.popoutButton.title = detached ? @"↙" : @"↗";
    self.popoutButton.toolTip = detached
        ? @"Return this tool to the plug-in window"
        : @"Open this tool in its own window";
    if (detached)
        [self.detachedWindows[@(index)] makeKeyAndOrderFront:nil];
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
    ProjectHistory _history;
    std::array<uint64_t, s3g::tracker::kMaximumTrackCount>
        _consumedNoteHitSequences;
    VisualPlaybackFrame _pendingVisualFrame;
    bool _visualFramePrimed;
    uint64_t _reportedStepRecordDrops;
    bool _runtimePublicationPending;
}
@property(nonatomic, strong) S3GTrackerWorkspaceController* workspace;
@property(nonatomic, strong) S3GTrackerSongWindowController* songWindow;
@property(nonatomic, strong) S3GTrackerConsoleHelpWindowController* helpWindow;
@property(nonatomic, strong) S3GTrackerClapPageView* pageView;
@property(nonatomic, strong) NSTimer* displayTimer;
@property(nonatomic, strong) NSTimer* runtimePublicationTimer;
- (instancetype)initWithPlugin:(Plugin*)plugin;
- (ProjectDocument)currentDocument;
- (void)applyDocument:(const ProjectDocument&)document;
- (void)updateHistoryAvailability;
- (void)resetHistory:(const ProjectDocument&)document;
- (void)recordHistory:(const ProjectDocument&)document;
- (void)undoProject;
- (void)redoProject;
- (void)consumeMidiStepCaptures;
- (void)scheduleRuntimePublication;
- (void)cancelRuntimePublication;
- (void)flushRuntimePublication;
- (void)disarmMidiStepRecording;
- (void)commitProjectWithoutRuntime:(BOOL)dirty;
- (void)presentSaveSongProject;
- (void)presentLoadSongProject;
- (BOOL)installPatternVariation:
    (const s3g::tracker::PatternVariationRequest&)variation;
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
    _consumedNoteHitSequences.fill(0u);
    _visualFramePrimed = false;
    _reportedStepRecordDrops = 0u;
    _runtimePublicationPending = false;
    _state->midiStepInputAvailable = true;
    _state->midiStepRecordMode = MidiStepRecordMode::Off;
    _plugin->midiStepRecordMode.store(
        static_cast<uint8_t>(MidiStepRecordMode::Off),
        std::memory_order_relaxed);

    __weak S3GTrackerClapCoordinator* weakSelf = self;
    _callbacks->togglePlayback = [weakSelf] {
        S3GTrackerClapCoordinator* owner = weakSelf;
        if (!owner) return;
        if (!requestHostTogglePlayback(*owner->_plugin)) {
            owner->_state->status = "Use REAPER transport controls";
            [owner.workspace reloadModel];
        }
    };
    _callbacks->restartPlayback = [weakSelf] {
        S3GTrackerClapCoordinator* owner = weakSelf;
        if (!owner) return;
        owner->_plugin->requestRestart.store(true, std::memory_order_release);
        if (owner->_plugin->host && owner->_plugin->host->request_process)
            owner->_plugin->host->request_process(owner->_plugin->host);
        [owner.workspace appendConsoleMessage:
            "SYNC ALL queued for row 1; phase offsets ignored and REAPER transport unchanged"
            error:NO];
    };
    _callbacks->resyncTrack = [weakSelf](std::size_t track) {
        S3GTrackerClapCoordinator* owner = weakSelf;
        if (!owner || track >= s3g::tracker::kMaximumTrackCount) return;
        owner->_plugin->requestTrackResyncMask.fetch_or(
            uint32_t { 1u } << track, std::memory_order_release);
        if (owner->_plugin->host && owner->_plugin->host->request_process)
            owner->_plugin->host->request_process(owner->_plugin->host);
        [owner.workspace appendConsoleMessage:[NSString stringWithFormat:
            @"Lane %lu SYNC queued for next tick; REAPER transport unchanged",
            static_cast<unsigned long>(track + 1u)].UTF8String
            error:NO];
    };
    _callbacks->panic = [weakSelf] {
        S3GTrackerClapCoordinator* owner = weakSelf;
        if (!owner) return;
        owner->_plugin->requestPanic.store(true, std::memory_order_release);
        if (owner->_plugin->host && owner->_plugin->host->request_process)
            owner->_plugin->host->request_process(owner->_plugin->host);
        [owner.workspace appendConsoleMessage:
            "MIDI panic queued on channels 1–16" error:NO];
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
        [weakSelf commitProjectWithoutRuntime:YES];
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
        [weakSelf commitProjectWithoutRuntime:YES];
    };
    _callbacks->midiStepRecordModeChanged = [weakSelf](
        MidiStepRecordMode mode) {
        S3GTrackerClapCoordinator* owner = weakSelf;
        if (!owner) return;
        owner->_plugin->midiStepRecordMode.store(
            static_cast<uint8_t>(mode), std::memory_order_release);
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
    [self resetHistory:[self currentDocument]];

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
    self.songWindow.saveProjectHandler = ^{
        [weakSelf presentSaveSongProject];
    };
    self.songWindow.loadProjectHandler = ^{
        [weakSelf presentLoadSongProject];
    };
    [self configureHostDevices];
    return self;
}

- (void)dealloc
{
    _plugin->midiStepRecordMode.store(
        static_cast<uint8_t>(MidiStepRecordMode::Off),
        std::memory_order_release);
    [self stopTimer];
    [self.pageView detachFromPlugin];
    [self.songWindow close];
    [self.helpWindow close];
}

- (void)consumeMidiStepCaptures
{
    if (!_state) return;
    MidiStepCapture capture;
    bool changed = false;
    while (_plugin->midiStepCaptures.pop(capture)) {
        const auto result = s3g::tracker::recordMidiStep(_state->session,
            capture.mode, capture, _plugin->sampleRate);
        if (result.recorded()) {
            changed = true;
            std::ostringstream message;
            message << "STEP REC CH" << static_cast<unsigned>(capture.channel)
                    << " note " << static_cast<unsigned>(capture.note)
                    << " → lane " << (result.track + 1u)
                    << ", row " << (result.row + 1u);
            if (capture.mode == MidiStepRecordMode::Unquantized) {
                message << ", MT " << std::lround(
                    static_cast<double>(result.microTime) * 100.0) << '%';
                if (!result.timingKnown) message << " (no host tick; centered)";
                else if (result.timingClamped) message << " (clamped)";
            }
            [self.workspace appendConsoleMessage:message.str() error:NO];
        } else if (result.code == MidiStepRecordCode::FxUnavailable) {
            [self.workspace appendConsoleMessage:
                "MICRO step record needs an empty SEQ1 or SEQ2 cell on the cursor row"
                error:YES];
        } else if (result.code == MidiStepRecordCode::TimingUnavailable) {
            [self.workspace appendConsoleMessage:
                "MICRO step record requires a nonzero MicroTime range"
                error:YES];
        }
    }
    const uint64_t dropped = _plugin->midiStepCaptures.droppedCount();
    if (dropped != _reportedStepRecordDrops) {
        _reportedStepRecordDrops = dropped;
        [self.workspace appendConsoleMessage:
            "MIDI step-record input overflowed; reduce controller density"
            error:YES];
    }
    if (changed) [self commitProject:YES];
}

- (void)disarmMidiStepRecording
{
    if (!_state) return;
    _state->midiStepRecordMode = MidiStepRecordMode::Off;
    _plugin->midiStepRecordMode.store(
        static_cast<uint8_t>(MidiStepRecordMode::Off),
        std::memory_order_release);
    [self.workspace reloadModel];
}

- (void)configureHostDevices
{
    if (!_state || !self.workspace) return;
    _state->midiRoute = "1 OUT • 1 STEP IN • CH 1–16";
    _state->audioOutputDevice = "REAPER HOST AUDIO";
    _state->audioAvailable = false;
    std::vector<s3g::tracker::MidiDestination> destinations;
    s3g::tracker::MidiOutputTarget target;
    target.kind = s3g::tracker::MidiOutputTargetKind::VirtualSource;
    target.virtualSource = 1u;
    target.name = "TRACKER MIDI OUTPUT";
    [self.workspace setMidiDestinations:destinations selectedTarget:target];
    std::vector<s3g::tracker::app::AudioOutputDevice> devices {
        { 1u, "REAPER HOST AUDIO", _plugin->sampleRate, 0u, true },
    };
    [self.workspace setAudioOutputDevices:devices selectedDeviceId:1u];
    [self.workspace reloadModel];
}

- (void)refreshSongPatterns
{
    NSMutableArray<NSString*>* ids = [[NSMutableArray alloc] init];
    NSMutableArray<NSString*>* names = [[NSMutableArray alloc] init];
    for (const auto& entry : _state->patternBank.entries) {
        [ids addObject:[NSString stringWithUTF8String:entry.id.c_str()]];
        [names addObject:[NSString stringWithUTF8String:
            entry.pattern.name.c_str()]];
    }
    NSString* active = [NSString stringWithUTF8String:
        _state->patternBank.activePatternId.c_str()];
    [self.songWindow setAvailablePatternIds:ids patternNames:names
        activePatternId:active];
}

- (void)presentSaveSongProject
{
    NSSavePanel* panel = [NSSavePanel savePanel];
    panel.title = @"Save Tracker Song + Patterns";
    panel.prompt = @"Save";
    panel.canCreateDirectories = YES;
    panel.nameFieldStringValue = @"Tracker Song.s3gt";
    if (UTType* type = [UTType typeWithFilenameExtension:@"s3gt"])
        panel.allowedContentTypes = @[ type ];
    __weak S3GTrackerClapCoordinator* weakSelf = self;
    void (^completion)(NSModalResponse) = ^(NSModalResponse response) {
        S3GTrackerClapCoordinator* owner = weakSelf;
        if (!owner || response != NSModalResponseOK || !panel.URL) return;
        const char* path = panel.URL.fileSystemRepresentation;
        if (!path) return;
        const auto result = s3g::tracker::saveProjectDocumentAtomically(
            [owner currentDocument], path);
        if (!result.ok()) {
            const std::string message = "Could not save Song + Patterns: "
                + result.message;
            owner->_state->status = message;
            [owner.workspace appendConsoleMessage:message error:YES];
        } else {
            owner->_state->status = "Saved Song + Patterns";
            [owner.workspace appendConsoleMessage:
                std::string("Saved Song + Patterns to ") + path error:NO];
        }
        [owner.workspace reloadModel];
    };
    if (self.pageView.window)
        [panel beginSheetModalForWindow:self.pageView.window
            completionHandler:completion];
    else
        [panel beginWithCompletionHandler:completion];
}

- (void)presentLoadSongProject
{
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.title = @"Load Tracker Song + Patterns";
    panel.prompt = @"Load";
    panel.canChooseFiles = YES;
    panel.canChooseDirectories = NO;
    panel.allowsMultipleSelection = NO;
    if (UTType* type = [UTType typeWithFilenameExtension:@"s3gt"])
        panel.allowedContentTypes = @[ type ];
    __weak S3GTrackerClapCoordinator* weakSelf = self;
    void (^completion)(NSModalResponse) = ^(NSModalResponse response) {
        S3GTrackerClapCoordinator* owner = weakSelf;
        if (!owner || response != NSModalResponseOK || !panel.URL) return;
        const char* path = panel.URL.fileSystemRepresentation;
        if (!path) return;
        ProjectDocument document;
        const auto result = s3g::tracker::loadProjectDocument(path, document);
        if (!result.ok()) {
            const std::string message = "Could not load Song + Patterns: "
                + result.message;
            owner->_state->status = message;
            [owner.workspace appendConsoleMessage:message error:YES];
            [owner.workspace reloadModel];
            return;
        }
        [owner applyDocument:document];
        [owner commitProject:YES];
        owner->_state->status = "Loaded Song + Patterns";
        [owner.workspace appendConsoleMessage:
            std::string("Loaded Song + Patterns from ") + path error:NO];
        [owner.workspace reloadModel];
    };
    if (self.pageView.window)
        [panel beginSheetModalForWindow:self.pageView.window
            completionHandler:completion];
    else
        [panel beginWithCompletionHandler:completion];
}

- (ProjectDocument)currentDocument
{
    ProjectDocument document;
    if (!_state) return document;
    (void)syncSessionToActivePattern(*_state);
    document.patternBank = _state->patternBank;
    document.transport = _state->session.transport;
    document.warpLibrary = _state->session.warpLibrary;
    document.session.gateMilliseconds = _state->session.gateMilliseconds;
    document.session.tempoScale = _state->tempoScale;
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
    _state->session.warpLibrary = midiDocument.warpLibrary;
    _state->session.gateMilliseconds = midiDocument.session.gateMilliseconds;
    _state->tempoScale = midiDocument.session.tempoScale;
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

- (void)updateHistoryAvailability
{
    if (!_state) return;
    _state->canUndo = _history.canUndo();
    _state->canRedo = _history.canRedo();
}

- (void)resetHistory:(const ProjectDocument&)document
{
    ProjectDocument normalized = document;
    normalizeMidiOnlyDocument(normalized);
    const auto result = _history.reset(normalized);
    if (!result.ok()) {
        [self.workspace appendConsoleMessage:
            "Could not initialize Tracker edit history: " + result.message
            error:YES];
    }
    [self updateHistoryAvailability];
    [self.workspace reloadModel];
}

- (void)recordHistory:(const ProjectDocument&)document
{
    const auto result = _history.record(document);
    if (!result.ok()) {
        [self.workspace appendConsoleMessage:
            "Could not record Tracker edit history: " + result.message
            error:YES];
    }
    [self updateHistoryAvailability];
}

- (void)undoProject
{
    ProjectDocument document;
    const auto result = _history.undo(document);
    if (!result.ok()) {
        [self.workspace appendConsoleMessage:result.message error:YES];
        [self updateHistoryAvailability];
        [self.workspace reloadModel];
        return;
    }
    [self cancelRuntimePublication];
    [self applyDocument:document];
    publishDocument(*_plugin, std::move(document), true);
    [self updateHistoryAvailability];
    _state->status = "Undid Tracker edit";
    [self.workspace appendConsoleMessage:_state->status error:NO];
    [self.workspace reloadModel];
}

- (void)redoProject
{
    ProjectDocument document;
    const auto result = _history.redo(document);
    if (!result.ok()) {
        [self.workspace appendConsoleMessage:result.message error:YES];
        [self updateHistoryAvailability];
        [self.workspace reloadModel];
        return;
    }
    [self cancelRuntimePublication];
    [self applyDocument:document];
    publishDocument(*_plugin, std::move(document), true);
    [self updateHistoryAvailability];
    _state->status = "Redid Tracker edit";
    [self.workspace appendConsoleMessage:_state->status error:NO];
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
    if (dirty) [self recordHistory:document];
    storeDocumentWithoutRuntime(*_plugin, std::move(document), dirty);
    [self scheduleRuntimePublication];
    [self.workspace reloadModel];
}

- (void)commitProjectWithoutRuntime:(BOOL)dirty
{
    if (!_state) return;
    ProjectDocument document = [self currentDocument];
    _state->patternBank = document.patternBank;
    _state->instrumentRack = document.instrumentRack;
    _state->selectedRackInstrument = document.instrumentRack.selectedNode;
    (void)loadActivePatternIntoSession(*_state);
    if (dirty) [self recordHistory:document];
    storeDocumentWithoutRuntime(*_plugin, std::move(document), dirty);
    [self.workspace reloadModel];
}

- (void)scheduleRuntimePublication
{
    _runtimePublicationPending = true;
    [self.runtimePublicationTimer invalidate];
    __weak S3GTrackerClapCoordinator* weakSelf = self;
    self.runtimePublicationTimer = [NSTimer timerWithTimeInterval:(1.0 / 60.0)
        repeats:NO block:^(NSTimer*) {
            [weakSelf flushRuntimePublication];
        }];
    [[NSRunLoop mainRunLoop] addTimer:self.runtimePublicationTimer
        forMode:NSRunLoopCommonModes];
}

- (void)cancelRuntimePublication
{
    [self.runtimePublicationTimer invalidate];
    self.runtimePublicationTimer = nil;
    _runtimePublicationPending = false;
}

- (void)flushRuntimePublication
{
    [self.runtimePublicationTimer invalidate];
    self.runtimePublicationTimer = nil;
    if (!_runtimePublicationPending) return;
    _runtimePublicationPending = false;
    if (!publishStoredDocumentRuntime(*_plugin)) {
        [self.workspace appendConsoleMessage:
            "Could not prepare the edited Tracker playback runtime"
            error:YES];
    }
}

- (BOOL)installPatternVariation:
    (const s3g::tracker::PatternVariationRequest&)variation
{
    [self cancelRuntimePublication];
    if (_state->patternBank.entries.size()
            >= s3g::tracker::kMaximumPatternBankEntries
        || !syncSessionToActivePattern(*_state)) return NO;
    const std::string sourceId = _state->patternBank.activePatternId;
    const auto* source = _state->patternBank.findEntry(sourceId);
    const std::string id = nextPatternId(_state->patternBank);
    if (!source || id.empty()) return NO;
    _state->patternBank.entries.push_back(variationPatternEntry(
        *source, id, variation));

    if (variation.launch == PatternVariationLaunch::None) {
        _state->status = "Created variation " + id
            + "; active pattern remains " + sourceId;
        [self refreshSongPatterns];
        [self commitProjectWithoutRuntime:YES];
        [self.workspace appendConsoleMessage:_state->status error:NO];
        return YES;
    }

    _state->patternBank.activePatternId = id;
    if (!loadActivePatternIntoSession(*_state)) return NO;
    _state->session.selectedRow = 0u;
    ProjectDocument document = [self currentDocument];
    _state->patternBank = document.patternBank;
    (void)loadActivePatternIntoSession(*_state);
    const ProjectDocument historyDocument = document;
    const bool playing = _plugin->visualPlaying.load(
        std::memory_order_acquire);
    bool installed = true;
    if (playing) {
        installed = queueVariationDocument(*_plugin, std::move(document),
            variation.launch, true);
    } else {
        publishDocument(*_plugin, std::move(document), true);
    }
    if (!installed) {
        _state->patternBank.entries.pop_back();
        _state->patternBank.activePatternId = sourceId;
        (void)loadActivePatternIntoSession(*_state);
        _state->status = "Could not prepare variation runtime";
        [self refreshSongPatterns];
        [self.workspace reloadModel];
        return NO;
    }
    [self recordHistory:historyDocument];
    _state->status = playing
        ? "Queued variation " + id + " for quantized launch"
        : "Selected variation " + id;
    [self refreshSongPatterns];
    [self.workspace reloadModel];
    [self.workspace appendConsoleMessage:_state->status error:NO];
    return YES;
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
    [self refreshSongPatterns];
    [self commitProjectWithoutRuntime:YES];
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
    const auto words = commandWords(command);
    if (!words.empty()) {
        const auto& verb = words.front();
        const bool variationCommand = verb == "variation" || verb == "vary";
        const bool quantizedVariationLaunch = variationCommand
            && words.size() >= 4u
            && words[words.size() - 2u] == "launch"
            && (words.back() == "tick" || words.back() == "beat"
                || words.back() == "cycle" || words.back() == "pattern");
        if (variationCommand && _state->patternBank.entries.size()
                >= s3g::tracker::kMaximumPatternBankEntries) {
            [self.workspace appendConsoleMessage:
                "Pattern bank is full; delete a pattern before creating a variation."
                error:YES];
            return;
        }
        if (quantizedVariationLaunch && _state->songPlaybackEnabled) {
            [self.workspace appendConsoleMessage:
                "Quantized bank variation launch is unavailable while Song playback owns pattern transitions."
                error:YES];
            return;
        }
        if (verb == "help" || verb == "?") {
            [self.pageView showPage:S3GTrackerClapPageHelp];
            [self.workspace appendConsoleMessage:
                "Opened the MIDI tracker command reference" error:NO];
            return;
        }
        if (verb == "bpm") {
            [self.workspace appendConsoleMessage:
                "Tempo follows REAPER. Use the RATE menu for musical multiples."
                error:YES];
            return;
        }
        if (verb == "instrument" || verb == "inst") {
            [self.workspace appendConsoleMessage:
                "The MIDI tracker has no INS column; set CH01–CH16 in the lane header."
                error:YES];
            return;
        }
        if (verb == "actions") {
            std::string message = "SEQUENCER ACTIONS";
            for (std::size_t index = 0u;
                 index < s3g::tracker::sequencerActionCount(); ++index) {
                const auto* action = s3g::tracker::sequencerAction(index);
                if (!action) continue;
                message += index == 0u ? "  " : " · ";
                message += action->mnemonic;
                message += " ";
                message += action->displayName;
            }
            [self.workspace appendConsoleMessage:message error:NO];
            return;
        }
        const bool columnCommand = verb == "len" || verb == "length"
            || verb == "stride" || verb == "speed" || verb == "spd"
            || verb == "phase" || verb == "ph" || verb == "dir"
            || verb == "mode" || verb == "mute";
        if (columnCommand && std::find_if(words.begin(), words.end(),
                [](const std::string& word) {
                    return word == "ins" || word == "instrument";
                }) != words.end()) {
            [self.workspace appendConsoleMessage:
                "INS is not a column in the MIDI tracker." error:YES];
            return;
        }
        std::string action;
        if (verb == "fx" && words.size() >= 5u) action = words[4u];
        else if ((verb == "fx1" || verb == "f1" || verb == "fx2"
                || verb == "f2") && words.size() >= 3u) action = words[2u];
        if (!action.empty() && action != "clear" && action != "previous"
            && action != "prv"
            && !s3g::tracker::findSequencerAction(action)) {
            [self.workspace appendConsoleMessage:
                "SEQ columns accept sequencing actions only; type actions to list them."
                error:YES];
            return;
        }
    }
    const auto commandRngBefore = _state->session.commandRngState;
    const auto result = CommandEngine::execute(_state->session, command);
    if (!result.ok) {
        [self.workspace appendConsoleMessage:result.message error:YES];
        return;
    }
    if (result.hasEffect(CommandEffect::UndoRequested)) {
        [self undoProject];
        return;
    }
    if (result.hasEffect(CommandEffect::RedoRequested)) {
        [self redoProject];
        return;
    }
    if (result.patternVariation) {
        [self.workspace appendConsoleMessage:result.message error:NO];
        if (![self installPatternVariation:*result.patternVariation]) {
            _state->session.commandRngState = commandRngBefore;
            [self.workspace appendConsoleMessage:
                "Pattern variation was not installed." error:YES];
        }
        return;
    }
    [self.workspace appendConsoleMessage:result.message error:NO];
    const bool runtimeChanged = result.hasEffect(CommandEffect::PatternChanged)
        || result.hasEffect(CommandEffect::TransportChanged)
        || result.hasEffect(CommandEffect::OutputChanged)
        || result.hasEffect(CommandEffect::RoutingChanged);
    if (runtimeChanged)
        [self commitProject:YES];
    else if (result.hasEffect(CommandEffect::ProjectChanged))
        [self commitProjectWithoutRuntime:YES];
    if (result.hasEffect(CommandEffect::StartPlayback)) {
        if (!requestHostContinue(*_plugin))
            [self.workspace appendConsoleMessage:
                "The host does not expose a plug-in transport-start request."
                error:YES];
    }
    if (result.hasEffect(CommandEffect::StopPlayback)) {
        if (!requestHostStop(*_plugin))
            [self.workspace appendConsoleMessage:
                "The host does not expose a plug-in transport-stop request."
                error:YES];
    }
    if (result.hasEffect(CommandEffect::Panic))
        _plugin->requestPanic.store(true, std::memory_order_release);
    [self.workspace reloadModel];
}

- (void)pollDisplay:(NSTimer*)timer
{
    (void)timer;
    drainRetiredRuntimes(*_plugin);
    [self consumeMidiStepCaptures];
    const bool playing = _plugin->visualPlaying.load(
        std::memory_order_relaxed);
    _state->playing = playing;
    _state->hostBpm = _plugin->visualHostTempo.load(
        std::memory_order_relaxed);
    _state->paused = false;
    VisualPlaybackFrame captured;
    for (std::size_t track = 0u;
         track < s3g::tracker::kMaximumTrackCount; ++track) {
        captured.notePlayheads[track] = _plugin->notePlayheads[track].load(
            std::memory_order_relaxed);
        VisualNoteHitEvent hit;
        const bool newHit = _plugin->visualNoteHits[track].readLatest(
            _consumedNoteHitSequences[track], hit);
        captured.noteHits[track] = playing && newHit;
        if (newHit) {
            captured.noteHitRows[track] = hit.row;
            captured.noteHitSampleTimes[track] = hit.absoluteSampleTime;
        }
        captured.instrumentPlayheads[track]
            = _plugin->instrumentPlayheads[track].load(
                std::memory_order_relaxed);
        captured.velocityPlayheads[track]
            = _plugin->velocityPlayheads[track].load(
                std::memory_order_relaxed);
        for (std::size_t pair = 0u; pair < s3g::tracker::kFxPairCount; ++pair) {
            captured.fxActionPlayheads[track][pair]
                = _plugin->fxActionPlayheads[track][pair].load(
                    std::memory_order_relaxed);
            captured.fxValuePlayheads[track][pair]
                = _plugin->fxValuePlayheads[track][pair].load(
                    std::memory_order_relaxed);
        }
    }
    captured.songRow = _plugin->visualSongRow.load(
        std::memory_order_relaxed);
    captured.pendingSongRow = _plugin->visualPendingSongRow.load(
        std::memory_order_relaxed);
    captured.pendingSongQuantization
        = _plugin->visualPendingSongQuantization.load(
            std::memory_order_relaxed);
    if (playing && _visualFramePrimed) {
        _state->notePlayheads = _pendingVisualFrame.notePlayheads;
        _state->noteHits = _pendingVisualFrame.noteHits;
        _state->noteHitRows = _pendingVisualFrame.noteHitRows;
        _state->noteHitSampleTimes
            = _pendingVisualFrame.noteHitSampleTimes;
        _state->instrumentPlayheads
            = _pendingVisualFrame.instrumentPlayheads;
        _state->velocityPlayheads = _pendingVisualFrame.velocityPlayheads;
        _state->fxActionPlayheads = _pendingVisualFrame.fxActionPlayheads;
        _state->fxValuePlayheads = _pendingVisualFrame.fxValuePlayheads;
    } else {
        _state->noteHits.fill(false);
    }
    const int32_t songRow = playing && _visualFramePrimed
        ? _pendingVisualFrame.songRow : captured.songRow;
    const int32_t pendingSongRow = playing && _visualFramePrimed
        ? _pendingVisualFrame.pendingSongRow : captured.pendingSongRow;
    const uint32_t pendingSongQuantization = playing && _visualFramePrimed
        ? _pendingVisualFrame.pendingSongQuantization
        : captured.pendingSongQuantization;
    _pendingVisualFrame = std::move(captured);
    _visualFramePrimed = playing;
    _state->songPlaybackActive = _state->playing
        && _state->songPlaybackEnabled && songRow >= 0;
    _state->songPlaybackRowValid = songRow >= 0;
    _state->songPlaybackRow = songRow >= 0
        ? static_cast<std::size_t>(songRow) : 0u;
    _state->songPlaybackPatternId.clear();
    _state->songPlaybackMutedTracks = 0u;
    if (_state->songPlaybackActive) {
        const auto arrangement = [self.songWindow songArrangement];
        if (_state->songPlaybackRow < arrangement.rows.size()) {
            const auto& activeRow = arrangement.rows[
                _state->songPlaybackRow];
            _state->songPlaybackPatternId = activeRow.patternId;
            _state->songPlaybackMutedTracks = activeRow.mutedTracks;
        }
    }
    [self.songWindow setPlaybackRow:_state->songPlaybackRow
        valid:_state->songPlaybackRowValid];
    [self.songWindow setPendingPlaybackRow:pendingSongRow >= 0
            ? static_cast<NSUInteger>(pendingSongRow) : 0u
        valid:pendingSongRow >= 0
        quantization:static_cast<NSInteger>(std::min<uint32_t>(
            pendingSongQuantization, 3u))];
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
    self.displayTimer = [NSTimer timerWithTimeInterval:(1.0 / 60.0)
        target:self selector:@selector(pollDisplay:) userInfo:nil repeats:YES];
    self.displayTimer.tolerance = 1.0 / 240.0;
    [[NSRunLoop mainRunLoop] addTimer:self.displayTimer
        forMode:NSRunLoopCommonModes];
}

- (void)stopTimer
{
    [self consumeMidiStepCaptures];
    [self flushRuntimePublication];
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
    delete instance->queuedVariationRuntime.exchange(nullptr,
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
    auto* runtime = new (std::nothrow) Runtime(document, sampleRate,
        &instance->patternLaunch, &instance->visualNoteHits,
        &instance->midiStepClock);
    if (!runtime || !runtime->valid) {
        delete runtime;
        return false;
    }
    delete instance->pendingRuntime.exchange(nullptr,
        std::memory_order_acq_rel);
    cancelQueuedVariation(*instance);
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
    instance->midiStepClock.clear();
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
    instance->midiStepClock.clear();
}

clap_process_status process(const clap_plugin_t* plugin,
    const clap_process_t* processData)
{
    if (!processData) return CLAP_PROCESS_ERROR;
    auto& instance = *self(plugin);
    (void)swapQueuedVariationRuntime(instance, processData->out_events);
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
        if (!event || event->space_id != CLAP_CORE_EVENT_SPACE_ID) continue;
        const uint32_t time = std::min(event->time,
            processData->frames_count);
        if (event->type == CLAP_EVENT_TRANSPORT
            && event->size >= sizeof(clap_event_transport_t)) {
            renderTo(time);
            transport = readHostTransport(reinterpret_cast<
                const clap_event_transport_t*>(event));
            cursor = time;
        } else if (event->type == CLAP_EVENT_MIDI
            && event->size >= sizeof(clap_event_midi_t)) {
            renderTo(time);
            captureMidiStep(instance, *reinterpret_cast<
                const clap_event_midi_t*>(event), time);
        }
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
    (void)isInput;
    return 1u;
}

bool notePortsGet(const clap_plugin_t*, uint32_t index, bool isInput,
    clap_note_port_info_t* info)
{
    if (!info || index != 0u) return false;
    *info = {};
    info->id = isInput ? 50u : 100u;
    info->supported_dialects = CLAP_NOTE_DIALECT_MIDI;
    info->preferred_dialect = CLAP_NOTE_DIALECT_MIDI;
    std::snprintf(info->name, sizeof(info->name), "%s",
        isInput ? "Step Record MIDI Input" : "Tracker MIDI Output");
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
    if (instance->coordinator)
        [instance->coordinator cancelRuntimePublication];
    publishDocument(*instance, document, false);
    if (instance->coordinator) {
        [instance->coordinator applyDocument:document];
        [instance->coordinator resetHistory:document];
    }
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
    [instance->coordinator disarmMidiStepRecording];
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
    "0.4.0",
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
        instance->document, instance->sampleRate,
        &instance->patternLaunch, &instance->visualNoteHits,
        &instance->midiStepClock);
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
