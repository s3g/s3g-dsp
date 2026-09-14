#include "s3g/tracker/clap_midi_engine.h"
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <new>
#include <thread>
#include <vector>

namespace {
using namespace s3g::tracker;
namespace m = s3g::tracker::midi;
thread_local bool inAudio = false;
std::atomic<unsigned> allocations {0}, deletions {0};
int checks = 0, failures = 0;
void check(bool ok, const char* message)
{
    ++checks;
    if (!ok) { ++failures; std::cerr << message << '\n'; }
}

ProjectDocument fixture(uint8_t base = 60)
{
    ProjectDocument d;
    d.patternBank.entries.clear();
    PatternBankEntry entry;
    entry.id = "P0";
    entry.pattern.visibleRows = 4;
    Track track;
    for (uint8_t i = 0; i < 4; ++i)
        track.notes.push_back(NoteCell::withNote(uint8_t(base + i)));
    track.velocities.assign(4, ValueCell::withValue(1.f));
    track.noteColumn.length = track.velocityColumn.length = 4;
    entry.pattern.tracks.push_back(track);
    d.patternBank.entries.push_back(entry);
    d.patternBank.activePatternId = entry.id;
    d.transport.bpm = 60;
    d.transport.ticksPerBeat = 1;
    d.transport.swing = .5;
    d.session.gateMilliseconds = 90;
    return d;
}

struct Message {
    uint64_t frame;
    uint8_t status, note, velocity;
};

struct Harness {
    m::Engine engine;
    std::array<Message, 65536> messages {};
    std::size_t count = 0;
    bool accept = true;
    unsigned callbacks = 0, requests = 0, dirty = 0;
    unsigned hostSequence = 0;
    m::MidiOutput output {this, [](const void* p, uint32_t offset,
            uint8_t status, uint8_t note, uint8_t velocity) noexcept {
        auto& h = *const_cast<Harness*>(static_cast<const Harness*>(p));
        if (!h.accept || h.count == h.messages.size()) return false;
        h.messages[h.count++] = {h.engine.processFrame + offset, status, note, velocity};
        return true;
    }};

    Harness()
    {
        engine.services.context = this;
        engine.services.requestProcess = [](const void* p) noexcept {
            auto& h = *const_cast<Harness*>(static_cast<const Harness*>(p));
            ++h.requests;
            h.hostSequence = 1;
        };
        engine.services.requestCallback = [](const void* p) noexcept {
            ++const_cast<Harness*>(static_cast<const Harness*>(p))->callbacks;
        };
        engine.services.markDirty = [](const void* p) noexcept {
            auto& h = *const_cast<Harness*>(static_cast<const Harness*>(p));
            ++h.dirty;
            h.hostSequence = h.hostSequence == 1 ? 2 : 99;
        };
        engine.document = fixture();
        check(m::initialize(engine), "initial runtime failed");
    }
    void run(uint32_t frames, bool playing = false, double bpm = 60,
        const std::vector<m::InputEvent>& input = {})
    {
        m::ProcessData data;
        data.frames_count = frames;
        data.transport = {playing, true, true,
            double(engine.processFrame) * bpm / (60 * engine.sampleRate), bpm, 0};
        data.in_events = {&input, uint32_t(input.size()),
            [](const void* p, uint32_t i, m::InputEvent& e) noexcept {
                const auto& list = *static_cast<const std::vector<m::InputEvent>*>(p);
                if (i >= list.size()) return false;
                e = list[i];
                return true;
            }};
        data.out_events = &output;
        inAudio = true;
        m::process(engine, data);
        inAudio = false;
    }
};

m::InputEvent input(uint32_t frame, uint8_t status, uint8_t note, uint8_t velocity)
{
    m::InputEvent event;
    event.time = frame;
    event.midi = {0, {status, note, velocity}};
    return event;
}

void timingTests()
{
    for (double rate : {44100., 48000., 96000.})
    for (double bpm : {97.5, 120., 145.5})
    for (uint32_t block : {64u, 127u, 512u, 8192u}) {
        auto h = std::make_unique<Harness>();
        check(m::activate(h->engine, rate), "activate valid sample rate");
        const double rowFrames = rate * 60 / bpm;
        // Finish inside the last row, not on a fractional ninth onset whose
        // nearest sample may legitimately fall inside the final buffer.
        const uint64_t end = uint64_t(std::ceil(7.5 * rowFrames));
        while (h->engine.processFrame < end)
            h->run(uint32_t(std::min<uint64_t>(block,
                end - h->engine.processFrame)), true, bpm);
        std::size_t onsets = 0;
        for (std::size_t i = 0; i < h->count; ++i) {
            const auto& e = h->messages[i];
            check(!i || e.frame >= h->messages[i-1].frame, "MIDI order across blocks");
            if ((e.status & 0xf0) != 0x90) continue;
            check(e.note == 60 + onsets % 4
                && std::abs(double(e.frame) - onsets * rowFrames) <= 1,
                "host-clock onset/loop changed");
            ++onsets;
        }
        check(onsets == 8, "eight-row playback note count");
        uint64_t revision = 0;
        VisualNoteHitEvent hit;
        check(h->engine.visualNoteHits[0].readLatest(revision, hit)
            && hit.row == 3
            && std::abs(double(hit.absoluteSampleTime) - 7 * rowFrames) <= 1,
            "visual hit no longer retains audio-clock timestamp");
        check(h->engine.visualPlaying && h->engine.notePlayheads[0] == 3,
            "shared playhead publication");
    }
}

void transportAndInputTests()
{
    auto h = std::make_unique<Harness>();
    check(m::activate(h->engine, 8000), "input fixture activate");
    m::InputEvent stop;
    stop.kind = m::InputEvent::Kind::Transport;
    stop.time = 100;
    stop.transport = {false, true, true, .0125, 60, 0};
    h->run(256, true, 60, {stop});
    check(h->count == 2 && h->messages[0].frame == 0
        && h->messages[1].frame == 100 && h->messages[1].status == 0x80,
        "in-block stop must release the sounding note at its exact offset");
    check(!h->engine.visualPlaying, "in-block stop visual state");

    h->count = 0;
    h->engine.midiStepRecordMode = uint8_t(MidiStepRecordMode::Step);
    h->engine.midiRecordTrack = 3;
    h->engine.midiMonitorChannel = 2;
    h->run(64, false, 60, {input(20, 0x90, 70, 100),
        input(30, 0x91, 70, 90), input(40, 0x80, 70, 0),
        input(50, 0x91, 70, 0)});
    check(h->count == 3 && h->messages[0].status == 0x92
        && h->messages[2].status == 0x82 && h->messages[2].frame == 306,
        "shared monitored pitch releases only after its last physical owner");
    MidiStepCapture capture;
    unsigned captured = 0;
    while (h->engine.midiStepCaptures.pop(capture)) {
        check(capture.targetTrack == 3 && capture.note == 70
            && !capture.timingKnown, "stopped step capture destination/timing");
        ++captured;
    }
    check(captured == 4, "capture all note-ons and velocity-zero note-offs");
    h->count = 0;
    h->run(64, false, 60, {input(1, 0x90, 72, 100)});
    h->engine.midiStepRecordMode = uint8_t(MidiStepRecordMode::Off);
    h->engine.midiMonitorChannel = 7;
    h->run(64, false, 60, {input(2, 0x80, 72, 0)});
    check(h->messages[h->count-1].status == 0x82,
        "disarming/changing routing must release the original channel");

    m::reset(h->engine);
    check(m::activate(h->engine, 8000), "live capture activate");
    h->engine.midiStepRecordMode = uint8_t(MidiStepRecordMode::LiveUnquantized);
    while (h->engine.midiStepCaptures.pop(capture)) {}
    h->run(512, true, 60, {input(400, 0x90, 75, 100)});
    check(h->engine.midiStepCaptures.pop(capture) && capture.timingKnown
        && capture.row == 0 && capture.offsetSamples == 400,
        "live recording uses exact MIDI event offset, not GUI time");
    h->engine.requestMidiMonitorRelease = true;
    h->run(64, true);
    check(!h->engine.monitoredInputNotes[75].active, "explicit monitor release");

    h->count = 0;
    h->engine.requestPanic = true;
    h->run(64);
    unsigned panic = 0;
    for (std::size_t i = 0; i < h->count; ++i)
        if ((h->messages[i].status & 0xf0) == 0xb0
            && h->messages[i].note == 123) ++panic;
    check(panic == 16, "panic sends all-notes-off on all MIDI channels");
    m::deactivate(h->engine);
    check(!h->engine.visualPlaying && !h->engine.runtimeArmed, "deactivate state");
    check(!m::activate(h->engine, 0)
        && !m::activate(h->engine, std::numeric_limits<double>::quiet_NaN()),
        "reject invalid sample rates");
}

void previewTests()
{
    for (uint32_t block : {127u, 8192u}) {
        auto h = std::make_unique<Harness>();
        check(m::activate(h->engine, 48000), "preview activate");
        const auto token = h->engine.pitchPreview.publish(
            {{2, 70, 100, 100}, {10, 72, 90, 100}},
            10, 120, 4, 16, true, true);
        const double rowFrames = 48000 * 60 / (97.5 * 4);
        const uint64_t end = uint64_t(std::ceil(16 * rowFrames * 16));
        while (h->engine.processFrame < end)
            h->run(uint32_t(std::min<uint64_t>(block,
                end - h->engine.processFrame)), false, 97.5);
        unsigned onsets = 0;
        for (std::size_t i = 0; i < h->count; ++i) {
            const auto& e = h->messages[i];
            check(!i || e.frame >= h->messages[i-1].frame,
                "preview note-offs/onsets must be time ordered");
            if ((e.status & 0xf0) != 0x90) continue;
            const double row = double(onsets / 2) * 16 + (onsets % 2 ? 10 : 2);
            check(e.status == 0x99 && std::abs(double(e.frame) - row * rowFrames) <= 1,
                "authoring LISTEN period/leading rest/host tempo changed");
            ++onsets;
        }
        check(onsets == 32, "sixteen complete preview loops without GUI ticks");
        h->engine.pitchPreview.cancel(token);
        h->count = 0;
        h->run(8192);
        check(h->count == 0, "cancelled preview must not restart");
    }
    auto h = std::make_unique<Harness>();
    check(m::activate(h->engine, 8000), "seam activate");
    h->engine.pitchPreview.publish({{0, 70, 100, 100}}, 1, 60, 1, 1, true, true);
    h->run(16001);
    check(h->count == 5 && h->messages[1].frame == 8000
        && h->messages[1].status == 0x80
        && h->messages[2].frame == 8000 && h->messages[2].status == 0x90,
        "same-frame preview loop seam must order off before on");
    h->engine.pitchPreview.cancel();
    h->run(1);
    check(h->messages[h->count-1].status == 0x80, "preview cancel releases held note");
    BurstDefinition burst;
    burst.eventCount = 2;
    burst.events[0] = {0, 76, 100, 50};
    burst.events[1] = {32768, 78, 90, 50};
    h->engine.burstPreviewMailbox.publish(burst, 3, 60, 1);
    h->count = 0;
    const uint64_t start = h->engine.processFrame;
    h->run(8001);
    check(h->count == 4 && h->messages[0].status == 0x92
        && h->messages[0].frame == start
        && h->messages[2].frame == start + 4000,
        "Burst preview offsets/routing/gates changed");
    h->engine.auditionNode = 1;
    h->engine.auditionData = 79 | (100u << 8);
    ++h->engine.auditionRevision;
    h->count = 0;
    h->run(721);
    check(h->count == 2 && h->messages[0].note == 79
        && h->messages[1].frame - h->messages[0].frame == 720,
        "one-note audition default gate changed");
}

void publicationTests()
{
    auto h = std::make_unique<Harness>();
    auto* original = h->engine.audioRuntime;
    m::storeDocumentWithoutRuntime(h->engine, fixture(64), true);
    check(h->engine.audioRuntime == original && !h->engine.pendingRuntime
        && h->dirty == 1 && h->hostSequence == 2,
        "document-only edit/request-process/dirty ordering");
    check(m::publishPreviewDocumentRuntime(h->engine, fixture(70)),
        "publish temporary preview runtime");
    check(h->engine.document.patternBank.entries[0].pattern.tracks[0].notes[0].note == 64,
        "preview publication must not overwrite saved document");
    h->run(1);
    check(h->engine.audioRuntime != original && h->callbacks == 1,
        "pending runtime audio handoff requests main-thread reclamation");
    check(m::publishStoredDocumentRuntime(h->engine), "restore stored runtime");
    h->run(1);
    m::drainRetiredRuntimes(h->engine);
    for (unsigned i = 0; i < RuntimeRetirementQueue::kCapacity + 4; ++i) {
        m::publishDocument(h->engine, fixture(uint8_t(60 + i % 8)), false);
        h->run(1);
    }
    check(h->engine.retiredRuntimes.full() && h->engine.pendingRuntime,
        "full retirement queue must defer, not delete/overwrite audio runtime");
    auto* pending = h->engine.pendingRuntime.load();
    m::drainRetiredRuntimes(h->engine);
    h->run(1);
    check(h->engine.audioRuntime == pending && !h->engine.pendingRuntime,
        "deferred runtime swaps after main-thread drain");
    check(m::queueVariationDocument(h->engine, fixture(80),
        PatternVariationLaunch::NextTick, true), "variation prepare");
    h->engine.patternLaunch.dueRevision = h->engine.patternLaunch.revision.load();
    h->run(1);
    check(!h->engine.queuedVariationRuntime && !h->engine.patternLaunch.revision,
        "due variation swaps and clears launch mailbox");
    check(m::queueVariationDocument(h->engine, fixture(),
        PatternVariationLaunch::NextBeat, false), "prepare cancellation");
    m::cancelQueuedVariation(h->engine);
    check(!h->engine.queuedVariationRuntime && !h->engine.patternLaunch.revision,
        "cancel queued variation");
    check(!m::queueSongDocument(h->engine, fixture(), 99,
        SongLaunchQuantization::NextTick), "reject unavailable Song row");
}

void outputFailureTests()
{
    auto h = std::make_unique<Harness>();
    h->accept = false;
    h->run(512, true);
    check(h->engine.sentEvents == 0 && h->engine.droppedEvents == 1,
        "host rejection accounted without allocation/retry");
    m::ProcessData data;
    data.frames_count = 1;
    inAudio = true;
    m::process(h->engine, data);
    inAudio = false;
    check(h->engine.droppedEvents == 2, "missing output handles note release safely");
}

void concurrentPublicationTests()
{
    auto h = std::make_unique<Harness>();
    std::atomic<bool> done {false};
    std::thread audio([&] {
        while (!done.load(std::memory_order_acquire)) {
            h->run(64);
            std::this_thread::yield();
        }
    });
    for (unsigned i = 0; i < 200; ++i) {
        m::publishDocument(h->engine, fixture(uint8_t(60 + i % 8)), false);
        m::drainRetiredRuntimes(h->engine);
    }
    done.store(true, std::memory_order_release);
    audio.join();
    m::drainRetiredRuntimes(h->engine);
    h->run(1);
    check(!h->engine.pendingRuntime && h->engine.runtimeBuildCount == 200,
        "concurrent main-thread publication/audio swaps lost final runtime");
}
} // namespace

void* operator new(std::size_t n)
{
    if (inAudio) ++allocations;
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept
{
    if (inAudio && p) ++deletions;
    std::free(p);
}
void operator delete(void* p, std::size_t) noexcept { ::operator delete(p); }

int main()
{
    timingTests();
    transportAndInputTests();
    previewTests();
    publicationTests();
    outputFailureTests();
    concurrentPublicationTests();
    check(allocations == 0 && deletions == 0,
        "MIDI process/runtime handoff allocated or freed on the audio thread");
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
