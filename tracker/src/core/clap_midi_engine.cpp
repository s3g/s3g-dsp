#include "s3g/tracker/clap_midi_engine.h"
#include "s3g/tracker/clap_document_controller.h"
#include "s3g/tracker/editor_state.h"
#include <limits>
#ifdef _WIN32
#include <memory>
#endif
#include <new>
#include <utility>

namespace s3g::tracker::midi {

using app::TrackerViewState;

ProjectDocument makeInitialDocument()
{
#ifdef _WIN32
    // This temporary editor state is large. Hosts can create an instance on
    // an already nested main-thread stack (for example during project recall).
    // Keep factory setup off that stack; this never runs in the audio callback.
    auto stateStorage = std::make_unique<TrackerViewState>();
    auto& state = *stateStorage;
#else
    TrackerViewState state;
#endif
    // Fresh instances open on one plain Superior Drummer bar. Keep this
    // factory pattern intentionally legible; the richer `demo` command
    // remains available from the Console when a user wants it.
    (void)CommandEngine::execute(state.session, "kit superior basic");
    state.session.pattern.tracks.resize(4u);
    state.session.laneDefaultNotes.resize(4u);
    for (auto alias = state.session.aliases.begin();
         alias != state.session.aliases.end();) {
        if (alias->second >= 4u) alias = state.session.aliases.erase(alias);
        else ++alias;
    }
    state.session.aliases["t"] = 2u;
    state.session.aliases["tom"] = 2u;
    state.session.pattern.name = "FOUR ON THE FLOOR";
    state.session.pattern.tracks[0u].name = "Kick";
    state.session.pattern.tracks[1u].name = "Snare";
    state.session.pattern.tracks[2u].name = "Tom";
    state.session.pattern.tracks[3u].name = "Hat";
    (void)CommandEngine::execute(
        state.session, "mask 1 x---x---x---x---");
    (void)CommandEngine::execute(
        state.session, "mask 2 ----x-------x---");
    (void)CommandEngine::execute(
        state.session, "mask 3 --------------xx");
    (void)CommandEngine::execute(
        state.session, "mask 4 x-x-x-x-x-x-x-x-");
    state.patternBank = s3g::tracker::makeDefaultPatternBank();
    (void)syncSessionToActivePattern(state);
    state.session.transport.sampleRate = 48000.0;
    state.session.transport.ticksPerBeat = 4u;
    state.session.transport.bpm = 120.0;
    state.session.transport.swing = 0.5;
    state.session.gateMilliseconds = 90.0;
    state.instrumentRack.midiRoutes[0u].channel = 1u;

    ProjectDocument document;
    document.patternBank = state.patternBank;
    document.burstBanks[0u].library = state.session.burstLibrary;
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

void captureMidiStep(Engine& plugin, const MidiMessage& event,
    uint32_t frameOffset) noexcept
{
    const auto mode = static_cast<MidiStepRecordMode>(
        plugin.midiStepRecordMode.load(std::memory_order_relaxed));
    if (mode == MidiStepRecordMode::Off || event.port_index != 0u) return;
    const uint8_t status = event.data[0];
    const uint8_t kind = status & 0xf0u;
    const bool noteOn = kind == 0x90u && event.data[2] != 0u;
    const bool noteOff = kind == 0x80u
        || (kind == 0x90u && event.data[2] == 0u);
    if (!noteOn && !noteOff) return;
    MidiStepCapture capture;
    capture.targetTrack = static_cast<std::size_t>(
        plugin.midiRecordTrack.load(std::memory_order_relaxed));
    capture.note = static_cast<uint8_t>(event.data[1] & 0x7fu);
    capture.velocity = static_cast<uint8_t>(event.data[2] & 0x7fu);
    capture.channel = static_cast<uint8_t>((status & 0x0fu) + 1u);
    capture.noteOn = noteOn;
    capture.mode = mode;
    const uint64_t absoluteFrame = frameOffset
            > std::numeric_limits<uint64_t>::max() - plugin.processFrame
        ? std::numeric_limits<uint64_t>::max()
        : plugin.processFrame + frameOffset;
    capture.rowKnown = plugin.midiStepClock.nearestTarget(
        absoluteFrame, capture.offsetSamples, capture.row);
    if (noteOff) {
        capture.followingRowKnown = plugin.midiStepClock.nextTarget(
            absoluteFrame, capture.followingOffsetSamples,
            capture.followingRow);
    }
    capture.timingKnown = capture.rowKnown;
    (void)plugin.midiStepCaptures.push(capture);
}

void markHostStateDirty(Engine& plugin)
{
    if (plugin.services.requestProcess)
        plugin.services.requestProcess(plugin.services.context);
    if (plugin.services.markDirty)
        plugin.services.markDirty(plugin.services.context);
}

bool retireQueueFull(const Engine& plugin) noexcept
{
    return plugin.retiredRuntimes.full();
}

bool retireRuntimeFromAudio(Engine& plugin, Runtime* runtime) noexcept
{
    return plugin.retiredRuntimes.retire(runtime);
}

void drainRetiredRuntimes(Engine& plugin)
{
    plugin.retiredRuntimes.drain();
}

void cancelQueuedVariation(Engine& plugin)
{
    plugin.songArrangementUpdatePending.store(false,
        std::memory_order_release);
    plugin.patternLaunch.revision.store(0u, std::memory_order_release);
    plugin.patternLaunch.dueRevision.store(0u, std::memory_order_release);
    plugin.patternLaunch.consumedRevision.store(0u,
        std::memory_order_relaxed);
    delete plugin.queuedVariationRuntime.exchange(nullptr,
        std::memory_order_acq_rel);
}

void storeDocumentWithoutRuntime(Engine& plugin, ProjectDocument document,
    bool markDirty)
{
    normalizeMidiOnlyDocument(document);
    {
        std::lock_guard<std::mutex> lock(plugin.documentMutex);
        plugin.document = std::move(document);
    }
    if (markDirty) markHostStateDirty(plugin);
}

bool queueRuntimeDocument(Engine& plugin, ProjectDocument document,
    PatternVariationLaunch quantization,
    std::optional<std::size_t> initialSongRow, bool markDirty)
{
    normalizeMidiOnlyDocument(document);
    auto* runtime = new (std::nothrow) Runtime(document,
        plugin.sampleRate, &plugin.patternLaunch, &plugin.visualNoteHits,
        &plugin.midiStepClock);
    if (!runtime || !runtime->valid
        || (initialSongRow && (!runtime->songEnabled
            || *initialSongRow >= runtime->songPatternIndices.size()))) {
        delete runtime;
        return false;
    }
    if (initialSongRow) runtime->initialSongRow = *initialSongRow;
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
    else if (plugin.services.requestProcess)
        plugin.services.requestProcess(plugin.services.context);
    return true;
}

bool queueVariationDocument(Engine& plugin, ProjectDocument document,
    PatternVariationLaunch quantization, bool markDirty)
{
    plugin.songArrangementUpdatePending.store(false,
        std::memory_order_release);
    return queueRuntimeDocument(plugin, std::move(document), quantization,
        std::nullopt, markDirty);
}

bool queueSongDocument(Engine& plugin, ProjectDocument document,
    std::size_t row, SongLaunchQuantization quantization,
    bool markDirty)
{
    PatternVariationLaunch boundary = PatternVariationLaunch::NextSongRow;
    switch (quantization) {
    case SongLaunchQuantization::NextTick:
        boundary = PatternVariationLaunch::NextTick;
        break;
    case SongLaunchQuantization::NextBeat:
        boundary = PatternVariationLaunch::NextBeat;
        break;
    case SongLaunchQuantization::NextPatternCycle:
        boundary = PatternVariationLaunch::NextPatternCycle;
        break;
    case SongLaunchQuantization::NextSongRow:
        break;
    }
    return queueRuntimeDocument(plugin, std::move(document), boundary,
        row, markDirty);
}

void publishDocument(Engine& plugin, ProjectDocument document,
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
    else if (plugin.services.requestProcess)
        plugin.services.requestProcess(plugin.services.context);
}

bool publishPreviewDocumentRuntime(Engine& plugin, ProjectDocument document)
{
    normalizeMidiOnlyDocument(document);
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
    if (plugin.services.requestProcess)
        plugin.services.requestProcess(plugin.services.context);
    return true;
}

bool publishStoredDocumentRuntime(Engine& plugin)
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
    if (plugin.services.requestProcess)
        plugin.services.requestProcess(plugin.services.context);
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

bool pushMidi(Engine& plugin, const MidiOutput* output,
    uint32_t frameOffset, uint8_t status, uint8_t data1,
    uint8_t data2) noexcept
{
    if (!output || !output->try_push
        || !output->try_push(output->context, frameOffset, status, data1, data2)) {
        plugin.droppedEvents.fetch_add(1u, std::memory_order_relaxed);
        return false;
    }
    plugin.sentEvents.fetch_add(1u, std::memory_order_relaxed);
    return true;
}
void emitActiveNoteOff(Engine& plugin, const MidiOutput* output,
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

void releaseMonitoredInputNote(Engine& plugin,
    const MidiOutput* output, uint32_t frameOffset,
    uint32_t inputIndex) noexcept
{
    auto& monitored = plugin.monitoredInputNotes[inputIndex];
    if (!monitored.active) return;
    const uint8_t note = static_cast<uint8_t>(inputIndex % kMidiNoteCount);
    const uint32_t outputIndex = activeNoteIndex(
        monitored.outputChannel, note);
    auto& owners = plugin.monitoredOutputCounts[outputIndex];
    if (owners > 0u) --owners;
    if (owners == 0u) {
        (void)pushMidi(plugin, output, frameOffset,
            static_cast<uint8_t>(0x80u | monitored.outputChannel),
            note, 0u);
    }
    monitored = {};
}

void releaseMonitoredInputNotes(Engine& plugin,
    const MidiOutput* output, uint32_t frameOffset) noexcept
{
    for (uint32_t index = 0u;
         index < plugin.monitoredInputNotes.size(); ++index) {
        releaseMonitoredInputNote(plugin, output, frameOffset, index);
    }
    plugin.monitoredOutputCounts.fill(0u);
}

void monitorMidiInput(Engine& plugin, const MidiMessage& event,
    const MidiOutput* output, uint32_t frameOffset) noexcept
{
    if (event.port_index != 0u) return;
    const uint8_t status = event.data[0];
    const uint8_t kind = status & 0xf0u;
    if (kind != 0x80u && kind != 0x90u) return;
    const uint8_t inputChannel = status & 0x0fu;
    const uint8_t note = event.data[1] & 0x7fu;
    const uint32_t inputIndex = activeNoteIndex(inputChannel, note);
    const bool noteOn = kind == 0x90u && event.data[2] != 0u;
    if (!noteOn) {
        // A key armed before a mode change must still release on its original
        // monitored channel even if recording has since been disarmed.
        releaseMonitoredInputNote(plugin, output, frameOffset, inputIndex);
        return;
    }
    const auto mode = static_cast<MidiStepRecordMode>(
        plugin.midiStepRecordMode.load(std::memory_order_relaxed));
    if (mode == MidiStepRecordMode::Off) return;

    if (plugin.monitoredInputNotes[inputIndex].active)
        releaseMonitoredInputNote(plugin, output, frameOffset, inputIndex);
    const uint8_t outputChannel = std::min<uint8_t>(
        plugin.midiMonitorChannel.load(std::memory_order_relaxed), 15u);
    const uint32_t outputIndex = activeNoteIndex(outputChannel, note);
    // A live key owns this channel/pitch until its physical note-off. Retire
    // an existing tracker gate first so its delayed off cannot cut the key.
    if (plugin.monitoredOutputCounts[outputIndex] == 0u
        && plugin.activeNotes[outputIndex].active) {
        emitActiveNoteOff(plugin, output, frameOffset, outputIndex);
    }
    plugin.monitoredInputNotes[inputIndex] = { outputChannel, true };
    ++plugin.monitoredOutputCounts[outputIndex];
    (void)pushMidi(plugin, output, frameOffset,
        static_cast<uint8_t>(0x90u | outputChannel), note,
        static_cast<uint8_t>(event.data[2] & 0x7fu));
}

void releaseActiveNotes(Engine& plugin, const MidiOutput* output,
    uint32_t frameOffset) noexcept
{
    for (uint32_t index = 0u; index < plugin.activeNotes.size(); ++index)
        emitActiveNoteOff(plugin, output, frameOffset, index);
}

void emitPanic(Engine& plugin, const MidiOutput* output,
    uint32_t frameOffset) noexcept
{
    plugin.pitchPreview.reset();
    releaseActiveNotes(plugin, output, frameOffset);
    releaseMonitoredInputNotes(plugin, output, frameOffset);
    for (uint8_t channel = 0u; channel < kMidiChannelCount; ++channel) {
        (void)pushMidi(plugin, output, frameOffset,
            static_cast<uint8_t>(0xb0u | channel), 123u, 0u);
    }
}

void emitScheduledEvent(Engine& plugin, Runtime& runtime,
    const MidiOutput* output, const ScheduledEvent& event,
    uint32_t blockOffset) noexcept
{
    if (event.kind == ScheduledEventKind::Parameter) return;
    uint8_t channel = 0u;
    runtime.routeFor(event.channel, channel);
    const uint32_t offset = blockOffset + event.frameOffset;
    if (event.kind == ScheduledEventKind::ControlChange) {
        (void)pushMidi(plugin, output, offset,
            static_cast<uint8_t>(0xb0u | channel),
            static_cast<uint8_t>(std::min<uint32_t>(
                event.parameterId, 127u)),
            s3g::tracker::midiValueFromNormalized(event.parameterValue));
        return;
    }
    const uint32_t index = activeNoteIndex(channel, event.note);
    // Do not let a sequenced retrigger/gate-off cut a physically held
    // monitored key sharing the same MIDI 1.0 channel and pitch.
    if (plugin.monitoredOutputCounts[index] != 0u) return;
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

std::size_t collectGateOffs(Engine& plugin, uint64_t segmentStart,
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

void handleAudition(Engine& plugin, Runtime& runtime,
    const MidiOutput* output, uint32_t blockOffset) noexcept
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

void handleBurstPreview(Engine& plugin, Runtime& runtime,
    const MidiOutput* output, bool transportPlaying,
    uint32_t blockOffset, uint32_t frameCount) noexcept
{
    const uint32_t revision = plugin.burstPreviewMailbox.revision.load(
        std::memory_order_acquire);
    auto& playback = plugin.burstPreviewPlayback;
    if ((revision & 1u) != 0u) return;
    if (transportPlaying) {
        // PREVIEW is deliberately a stopped-transport action. Consume a race
        // from the UI defensively so it cannot begin later after transport
        // stops, and let the normal transport transition release any tail.
        plugin.consumedBurstPreviewRevision = revision;
        playback.active = false;
        return;
    }

    if (revision != plugin.consumedBurstPreviewRevision) {
        BurstPreviewPlayback next;
        const uint32_t metadata = plugin.burstPreviewMailbox.metadata.load(
            std::memory_order_relaxed);
        next.eventCount = static_cast<uint8_t>(std::min<uint32_t>(
            metadata & 0xffu, s3g::tracker::kMaximumBurstEvents));
        next.midiChannel = static_cast<uint8_t>(std::clamp<uint32_t>(
            (metadata >> 8u) & 0xffu, 1u, 16u));
        const uint32_t ticksPerBeat = std::clamp<uint32_t>(
            (metadata >> 16u) & 0xffu, 1u, 96u);
        const double bpm = static_cast<double>(
            plugin.burstPreviewMailbox.bpmMilli.load(
                std::memory_order_relaxed)) / 1000.0;
        for (std::size_t index = 0u; index < next.eventCount; ++index) {
            const uint64_t packed = plugin.burstPreviewMailbox.events[index]
                .load(std::memory_order_relaxed);
            next.events[index] = {
                static_cast<uint16_t>(packed & 0xffffu),
                static_cast<uint8_t>((packed >> 16u) & 0x7fu),
                static_cast<uint8_t>((packed >> 24u) & 0x7fu),
                static_cast<uint8_t>((packed >> 32u) & 0x7fu),
            };
        }
        next.startFrame = plugin.processFrame + blockOffset;
        next.tickFrames = static_cast<uint64_t>(std::max(1.0,
            std::round(plugin.sampleRate * 60.0
                / (std::max(bpm, 1.0)
                    * static_cast<double>(ticksPerBeat)))));
        next.revision = revision;
        next.nextEvent = 0u;
        next.active = next.eventCount != 0u;
        if (revision != plugin.burstPreviewMailbox.revision.load(
                std::memory_order_acquire)) return;
        playback = next;
        plugin.consumedBurstPreviewRevision = revision;
    }

    const uint64_t segmentStart = plugin.processFrame + blockOffset;
    const uint64_t segmentEnd = segmentStart + frameCount;
    std::size_t eventCount = 0u;
    while (playback.active && playback.nextEvent < playback.eventCount) {
        const auto index = playback.nextEvent;
        const auto& authored = playback.events[index];
        const uint64_t onset = playback.startFrame
            + playback.tickFrames * static_cast<uint64_t>(authored.position)
                / 65536u;
        if (onset >= segmentEnd) break;
        ScheduledEvent event;
        event.absoluteSampleTime = onset;
        event.noteId = (static_cast<uint64_t>(playback.revision) << 32u)
            | static_cast<uint64_t>(index + 1u);
        event.durationSamples = std::max<uint64_t>(1u,
            playback.tickFrames
                * static_cast<uint64_t>(authored.gatePercent) / 100u);
        event.frameOffset = onset <= segmentStart ? 0u
            : static_cast<uint32_t>(onset - segmentStart);
        event.normalizedVelocity = static_cast<float>(authored.velocity)
            / 127.0f;
        event.note = authored.note;
        event.channel = playback.midiChannel;
        event.kind = ScheduledEventKind::NoteOn;
        event.destination = EventDestination::Midi;
        plugin.events[eventCount++] = event;
        ++playback.nextEvent;
        if (playback.nextEvent >= playback.eventCount)
            playback.active = false;
    }

    // With no Burst onsets, the Pitch/authoring merge below drains gate tails.
    // It must retain its own offs until they can be ordered against loop hits.
    if (eventCount == 0) return;
    std::size_t pendingGateCount = collectGateOffs(
        plugin, segmentStart, frameCount);
    const auto laterGate = [](const GateOff& left, const GateOff& right) {
        if (left.frameOffset != right.frameOffset)
            return left.frameOffset > right.frameOffset;
        return left.noteId > right.noteId;
    };
    std::size_t eventIndex = 0u;
    while (eventIndex < eventCount || pendingGateCount > 0u) {
        const bool useGate = pendingGateCount > 0u
            && (eventIndex >= eventCount
                || plugin.gateOffs.front().frameOffset
                    <= plugin.events[eventIndex].frameOffset);
        if (useGate) {
            std::pop_heap(plugin.gateOffs.begin(),
                plugin.gateOffs.begin() + pendingGateCount, laterGate);
            const auto gate = plugin.gateOffs[--pendingGateCount];
            const auto& active = plugin.activeNotes[gate.activeIndex];
            if (active.active && active.noteId == gate.noteId)
                emitActiveNoteOff(plugin, output,
                    blockOffset + gate.frameOffset, gate.activeIndex);
            continue;
        }
        const auto event = plugin.events[eventIndex++];
        emitScheduledEvent(plugin, runtime, output, event, blockOffset);
        const uint8_t channel = static_cast<uint8_t>(std::clamp<int>(
            event.channel, 1, 16) - 1);
        const uint32_t activeIndex = activeNoteIndex(channel, event.note);
        const auto& active = plugin.activeNotes[activeIndex];
        if (!active.active || active.dueFrame >= segmentEnd
            || pendingGateCount >= plugin.gateOffs.size()) continue;
        plugin.gateOffs[pendingGateCount++] = {
            activeIndex,
            active.dueFrame <= segmentStart ? 0u
                : static_cast<uint32_t>(active.dueFrame - segmentStart),
            active.noteId,
        };
        std::push_heap(plugin.gateOffs.begin(),
            plugin.gateOffs.begin() + pendingGateCount, laterGate);
    }
}

void releasePitchPreviewNotes(Engine& plugin,
    const MidiOutput* output, uint32_t offset) noexcept
{
    for (uint32_t i = 0; i < plugin.activeNotes.size(); ++i)
        if (plugin.activeNotes[i].noteId & (uint64_t{1} << 63))
            emitActiveNoteOff(plugin, output, offset, i);
}

void handlePitchPreview(Engine& plugin, Runtime& runtime,
    const MidiOutput* output, HostTransport transport,
    uint32_t blockOffset, uint32_t frameCount) noexcept
{
    auto& preview = plugin.pitchPreview;
    if (transport.playing) {
        preview.reset();
        releasePitchPreviewNotes(plugin, output, blockOffset);
        return;
    }
    const uint64_t segmentStart = plugin.processFrame + blockOffset;
    const uint64_t segmentEnd = segmentStart + frameCount;
    std::size_t pendingGateCount = collectGateOffs(
        plugin, segmentStart, frameCount);
    const auto laterGate = [](const GateOff& left, const GateOff& right) {
        if (left.frameOffset != right.frameOffset)
            return left.frameOffset > right.frameOffset;
        return left.noteId > right.noteId;
    };
    s3g::tracker::PreviewHit hit;
    bool hasHit = preview.next(hit);
    // Stream notes into the existing gate merge, with no per-block event cap
    // or audio-thread allocation. Offs at a seam precede the next note-on.
    while (hasHit || pendingGateCount > 0u) {
        if (pendingGateCount && (!hasHit
                || plugin.gateOffs.front().frameOffset <= hit.offset)) {
            std::pop_heap(plugin.gateOffs.begin(),
                plugin.gateOffs.begin() + pendingGateCount, laterGate);
            const auto gate = plugin.gateOffs[--pendingGateCount];
            const auto& active = plugin.activeNotes[gate.activeIndex];
            if (active.active && active.noteId == gate.noteId)
                emitActiveNoteOff(plugin, output,
                    blockOffset + gate.frameOffset, gate.activeIndex);
            continue;
        }
        ScheduledEvent event;
        event.absoluteSampleTime = segmentStart + hit.offset;
        event.noteId = (uint64_t{1} << 63) | ++plugin.pitchPreviewNoteId;
        event.durationSamples = hit.duration;
        event.frameOffset = hit.offset;
        event.normalizedVelocity = float(hit.event.velocity) / 127.f;
        event.note = hit.event.note;
        event.channel = hit.channel;
        event.kind = ScheduledEventKind::NoteOn;
        event.destination = EventDestination::Midi;
        emitScheduledEvent(plugin, runtime, output, event, blockOffset);
        const auto activeIndex = activeNoteIndex(hit.channel - 1, hit.event.note);
        const auto& active = plugin.activeNotes[activeIndex];
        if (active.active && active.dueFrame < segmentEnd
                && pendingGateCount < plugin.gateOffs.size()) {
            plugin.gateOffs[pendingGateCount++] = {
                activeIndex,
                active.dueFrame <= segmentStart ? 0u
                    : static_cast<uint32_t>(active.dueFrame - segmentStart),
                active.noteId,
            };
            std::push_heap(plugin.gateOffs.begin(),
                plugin.gateOffs.begin() + pendingGateCount, laterGate);
        }
        hasHit = preview.next(hit);
    }
    preview.endBlock();
}

void updateVisualState(Engine& plugin, Runtime& runtime) noexcept
{
    const uint64_t currentSample = runtime.scheduler.renderedFrameCount();
    const uint64_t tickDuration = runtime.visualTickEndSample
            > runtime.visualTickStartSample
        ? runtime.visualTickEndSample - runtime.visualTickStartSample : 1u;
    const double subrowPhase = currentSample <= runtime.visualTickStartSample
        ? 0.0 : static_cast<double>(currentSample
            - runtime.visualTickStartSample) / static_cast<double>(tickDuration);
    plugin.visualSubrowPhase.store(static_cast<float>(std::clamp(
        subrowPhase, 0.0, 1.0)), std::memory_order_relaxed);
    const uint64_t nextTick = runtime.scheduler.tickIndex();
    plugin.visualTimingWarpTick.store(
        nextTick == 0u ? 0u : nextTick - 1u,
        std::memory_order_relaxed);
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
            && !runtime.songPlanner.isFinished()
        ? runtime.songPlanner.currentRowIndex() : std::nullopt;
    plugin.visualSongRow.store(songRow
            ? static_cast<int32_t>(*songRow) : -1,
        std::memory_order_relaxed);
    auto pendingSongRow = runtime.songEnabled
        ? runtime.songPlanner.pendingRowIndex() : std::nullopt;
    auto pendingSongQuantization = runtime.songEnabled
        ? runtime.songPlanner.pendingQuantization() : std::nullopt;
    const uint32_t runtimeLaunchRevision = plugin.patternLaunch.revision.load(
        std::memory_order_acquire);
    if (runtime.songEnabled && runtimeLaunchRevision != 0u
        && !plugin.songArrangementUpdatePending.load(
            std::memory_order_acquire)
        && plugin.queuedVariationRuntime.load(std::memory_order_acquire)) {
        pendingSongRow = static_cast<std::size_t>(
            plugin.songLaunchRow.load(std::memory_order_relaxed));
        pendingSongQuantization = static_cast<SongLaunchQuantization>(
            std::min<uint32_t>(plugin.songLaunchQuantization.load(
                std::memory_order_relaxed), 3u));
    }
    plugin.visualPendingSongRow.store(pendingSongRow
            ? static_cast<int32_t>(*pendingSongRow) : -1,
        std::memory_order_relaxed);
    plugin.visualPendingSongQuantization.store(pendingSongQuantization
            ? static_cast<uint32_t>(*pendingSongQuantization) : 0u,
        std::memory_order_relaxed);
}

bool swapPendingRuntime(Engine& plugin,
    const MidiOutput* output) noexcept
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
    if (plugin.services.requestCallback)
        plugin.services.requestCallback(plugin.services.context);
    return true;
}

bool swapQueuedVariationRuntime(Engine& plugin,
    const MidiOutput* output) noexcept
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
    plugin.songArrangementUpdatePending.store(false,
        std::memory_order_release);
    if (plugin.services.requestCallback)
        plugin.services.requestCallback(plugin.services.context);
    return true;
}

void renderSegment(Engine& plugin, const MidiOutput* output,
    HostTransport transport, uint32_t blockOffset, uint32_t frameCount)
    noexcept
{
    plugin.visualHostTempo.store(transport.tempo,
        std::memory_order_relaxed);
    if (frameCount == 0u) return;
    if (plugin.requestMidiMonitorRelease.exchange(false,
            std::memory_order_acq_rel)) {
        releaseMonitoredInputNotes(plugin, output, blockOffset);
    }
    Runtime* runtime = plugin.audioRuntime;
    if (!runtime) return;
    runtime->scheduler.setFillActive(plugin.fillActive.load(
        std::memory_order_relaxed));
    if (plugin.requestPanic.exchange(false, std::memory_order_acq_rel))
        emitPanic(plugin, output, blockOffset);
    if (!transport.playing) {
        if (plugin.pitchPreview.beginBlock(frameCount, plugin.sampleRate,
                transport.hasTempo ? transport.tempo : 0, transport.tempoIncrement))
            releasePitchPreviewNotes(plugin, output, blockOffset);
        plugin.midiStepClock.clear();
        if (plugin.hostWasPlaying) {
            releaseActiveNotes(plugin, output, blockOffset);
            runtime->scheduler.stop();
        }
        handleAudition(plugin, *runtime, output, blockOffset);
        handleBurstPreview(plugin, *runtime, output, false,
            blockOffset, frameCount);
        handlePitchPreview(plugin, *runtime, output, transport,
            blockOffset, frameCount);
        plugin.hostWasPlaying = false;
        plugin.runtimeArmed = false;
        plugin.expectedBeatValid = false;
        plugin.visualPlaying.store(false, std::memory_order_relaxed);
        plugin.visualPendingSongRow.store(-1, std::memory_order_relaxed);
    } else {
        handleAudition(plugin, *runtime, output, blockOffset);
        handleBurstPreview(plugin, *runtime, output, true,
            blockOffset, frameCount);
        handlePitchPreview(plugin, *runtime, output, transport,
            blockOffset, frameCount);
        const bool restartRequested = plugin.requestRestart.exchange(false,
            std::memory_order_acq_rel);
        const uint32_t loopRevision = plugin.songLoopRevision.load(
            std::memory_order_acquire);
        if (loopRevision != plugin.consumedSongLoopRevision) {
            plugin.consumedSongLoopRevision = loopRevision;
            if (runtime->songEnabled) {
                runtime->songPlanner.setLoopEnabled(
                    plugin.songLoopEnabled.load(std::memory_order_relaxed));
            }
        }
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
                - runtime->effectiveBpm(transport.tempo)) > 1.0e-7;
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


Engine::Engine() : document(makeInitialDocument()) {}

Engine::~Engine()
{
    delete pendingRuntime.exchange(nullptr, std::memory_order_acq_rel);
    delete queuedVariationRuntime.exchange(nullptr, std::memory_order_acq_rel);
    delete audioRuntime;
    retiredRuntimes.drain();
}

bool initialize(Engine& engine)
{
    if (engine.audioRuntime) return engine.audioRuntime->valid;
    engine.audioRuntime = new (std::nothrow) Runtime(
        engine.document, engine.sampleRate, &engine.patternLaunch,
        &engine.visualNoteHits, &engine.midiStepClock);
    return engine.audioRuntime && engine.audioRuntime->valid;
}

bool activate(Engine& engine, double sampleRate)
{
    if (!std::isfinite(sampleRate) || sampleRate <= 0.0) return false;
    auto* instance = &engine;
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
    instance->burstPreviewPlayback = {};
    instance->consumedBurstPreviewRevision
        = instance->burstPreviewMailbox.revision.load(
            std::memory_order_relaxed);
    instance->pitchPreview.reset();
    for (auto& note : instance->activeNotes) note = {};
    for (auto& note : instance->monitoredInputNotes) note = {};
    instance->monitoredOutputCounts.fill(0u);
    instance->requestMidiMonitorRelease.store(false,
        std::memory_order_relaxed);
    return true;
}

void deactivate(Engine& engine) noexcept
{
    auto* instance = &engine;
    if (instance->audioRuntime) instance->audioRuntime->scheduler.stop();
    instance->visualPlaying.store(false, std::memory_order_relaxed);
    instance->hostWasPlaying = false;
    instance->runtimeArmed = false;
    instance->burstPreviewPlayback = {};
    instance->consumedBurstPreviewRevision
        = instance->burstPreviewMailbox.revision.load(
            std::memory_order_relaxed);
    instance->pitchPreview.reset();
    instance->midiStepClock.clear();
    for (auto& note : instance->monitoredInputNotes) note = {};
    instance->monitoredOutputCounts.fill(0u);
}

void reset(Engine& engine) noexcept
{
    auto* instance = &engine;
    if (instance->audioRuntime) instance->audioRuntime->scheduler.stop();
    for (auto& note : instance->activeNotes) note = {};
    for (auto& note : instance->monitoredInputNotes) note = {};
    instance->monitoredOutputCounts.fill(0u);
    instance->hostWasPlaying = false;
    instance->runtimeArmed = false;
    instance->expectedBeatValid = false;
    instance->burstPreviewPlayback = {};
    instance->consumedBurstPreviewRevision
        = instance->burstPreviewMailbox.revision.load(
            std::memory_order_relaxed);
    instance->pitchPreview.reset();
    instance->visualPlaying.store(false, std::memory_order_relaxed);
    instance->midiStepClock.clear();
}


void process(Engine& instance, const ProcessData& data) noexcept
{
    const auto* processData = &data;
    (void)swapQueuedVariationRuntime(instance, processData->out_events);
    (void)swapPendingRuntime(instance, processData->out_events);
    HostTransport transport = processData->transport;
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
        transport.tempo += transport.tempoIncrement * (end - cursor);
        cursor = end;
    };
    const auto& input = processData->in_events;
    for (uint32_t index = 0u; input.get && index < input.count; ++index) {
        InputEvent event;
        if (!input.get(input.context, index, event)) continue;
        const uint32_t time = std::min(event.time, processData->frames_count);
        if (event.kind == InputEvent::Kind::Transport) {
            renderTo(time);
            transport = event.transport;
            cursor = time;
        } else {
            renderTo(time);
            captureMidiStep(instance, event.midi, time);
            monitorMidiInput(instance, event.midi, processData->out_events, time);
        }
    }
    renderTo(processData->frames_count);
    instance.processFrame += processData->frames_count;
}

} // namespace s3g::tracker::midi
