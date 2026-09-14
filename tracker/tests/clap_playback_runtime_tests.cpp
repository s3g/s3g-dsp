#include "s3g/tracker/clap_playback_runtime.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <new>
#include <thread>

namespace {
thread_local bool audioThread = false;
std::atomic<unsigned> audioAllocations {0}, audioDeletions {0};
int checks = 0, failures = 0;
void check(bool condition, const char* message)
{
    ++checks;
    if (!condition) { ++failures; std::cerr << message << '\n'; }
}
using namespace s3g::tracker;
ProjectDocument fixture(bool song = false)
{
    ProjectDocument d;
    d.patternBank.entries.clear();
    for (int p = 0; p < 3; ++p) {
        PatternBankEntry entry;
        entry.id = "P" + std::to_string(p);
        entry.pattern.visibleRows = 4;
        Track track;
        for (uint8_t row = 0; row < 4; ++row)
            track.notes.push_back(NoteCell::withNote(uint8_t(60 + 10*p + row)));
        track.velocities.assign(4, ValueCell::withValue(1.f));
        track.noteColumn.length = track.velocityColumn.length = 4;
        entry.pattern.tracks.push_back(track);
        d.patternBank.entries.push_back(std::move(entry));
    }
    d.patternBank.activePatternId = "P0";
    d.transport.bpm = 60;
    d.transport.ticksPerBeat = 1;
    d.transport.swing = .5;
    d.session.songPlaybackEnabled = song;
    d.song.ticksPerBeat = 1;
    SongRow a;
    a.patternId = "P0";
    a.durationTicks = 2;
    a.repeats = 2;
    d.song.rows.push_back(a);
    SongRow b;
    b.patternId = "P1";
    b.durationTicks = 2;
    b.repeats = 1;
    b.tempoMultiplier = 2;
    d.song.rows.push_back(b);
    return d;
}

void clockTests()
{
    MidiStepClock clock;
    int64_t offset = 0;
    std::size_t row = 0;
    check(!clock.nearestTarget(100, offset, row), "unarmed clock must be unavailable");
    clock.publish(100, 200, 7, 0);
    check(clock.nearestTarget(125, offset, row) && offset == 25 && row == 7,
        "late recorded key targets preceding row");
    check(clock.nearestTarget(175, offset, row) && offset == -25 && row == 0,
        "early recorded key targets following/loop row");
    check(clock.nearestTarget(150, offset, row) && offset == 50 && row == 7,
        "equidistant recording chooses preceding row");
    check(!clock.nearestTarget(99, offset, row), "pre-origin capture must not wrap");
    check(clock.nextTarget(190, offset, row) && offset == -10 && row == 0,
        "note-off following-row timing");
    check(clock.nextTarget(220, offset, row) && offset == 20 && row == 0,
        "late note-off following-row timing");
    clock.publish(100, 50, 3, 4);
    check(clock.nextTarget(101, offset, row) && offset == 1,
        "next frame is clamped to current frame");
    clock.clear();
    check(!clock.nextTarget(200, offset, row), "reset invalidates recording clock");
}

void patternTests()
{
    auto d = fixture();
    d.session.tempoScale = .5;
    PatternLaunchMailbox launch;
    VisualNoteHitMailboxes hits;
    MidiStepClock clock;
    auto rt = std::make_unique<ClapPlaybackRuntime>(d, 8000, &launch, &hits, &clock);
    check(rt->valid && !rt->songEnabled && rt->patternIds == std::vector<std::string>{"P0"},
        "pattern runtime compiles only the selected pattern");
    check(rt->hostClock(120, 44100).bpm == 60 &&
        rt->hostClock(120, 44100).sampleRate == 44100,
        "host tempo scale/sample rate policy");
    check(rt->hostClock(std::numeric_limits<double>::quiet_NaN(), 8000).bpm == 60,
        "invalid host BPM preserves document tempo");
    std::array<ScheduledEvent, 128> events;
    audioThread = true;
    const bool armed = rt->arm(0, 120, 8000, 1000);
    const auto count = rt->scheduler.process(24001, events.data(), events.size());
    audioThread = false;
    check(armed, "host-aligned pattern arm");
    unsigned notes = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const auto& e = events[i];
        if (e.kind != ScheduledEventKind::NoteOn) continue;
        check(e.note == 60 + notes && e.absoluteSampleTime == 8000*notes,
            "extracted runtime changed pattern note/sample timing");
        ++notes;
    }
    check(notes == 4, "pattern runtime note count");
    uint64_t sequence = 0;
    VisualNoteHitEvent hit;
    check(hits[0].readLatest(sequence, hit) && hit.row == 3 && hit.absoluteSampleTime == 24000,
        "visual hits retain audio timestamps without any GUI refresh");
    check(!hits[0].readLatest(sequence, hit), "visual hit consumed twice");
    int64_t offset = 0;
    std::size_t row = 0;
    check(clock.nearestTarget(25010, offset, row) && row == 3 && offset == 10,
        "MIDI recording clock retains absolute audio origin");
    check(rt->visualTickStartSample == 24000 && rt->visualTickEndSample == 32000,
        "visual subrow endpoints retain sample times");
    audioThread = true;
    rt->updateClock(180, 8000);
    audioThread = false;
    check(rt->scheduler.transport().bpm == 90, "host update preserves project rate");
    uint8_t channel = 99;
    rt->routeFor(16, channel);
    check(channel == 15, "one-based MIDI channel conversion");
    rt->routeFor(0, channel);
    check(channel == 0, "MIDI channel lower bound");
    auto bad = fixture();
    bad.patternBank.activePatternId = "missing";
    auto invalid = std::make_unique<ClapPlaybackRuntime>(bad, 8000);
    check(!invalid->valid && !invalid->arm(0, 60, 8000, 0),
        "missing pattern cannot arm an invalid runtime");
}

void songTests()
{
    auto d = fixture(true);
    PatternLaunchMailbox launch;
    auto rt = std::make_unique<ClapPlaybackRuntime>(d, 8000, &launch);
    check(rt->valid && rt->songEnabled && rt->patternIds ==
        std::vector<std::string>{"P0", "P1"} && rt->songPatternIndices ==
        std::vector<std::size_t>{0, 1}, "Song compiles only referenced patterns");
    std::array<ScheduledEvent, 128> events;
    audioThread = true;
    const bool armed = rt->arm(0, 60, 8000, 0);
    const auto count = rt->scheduler.process(60000, events.data(), events.size());
    audioThread = false;
    check(armed, "Song arm");
    const uint8_t expectedNotes[] = {60, 61, 62, 63, 70, 71};
    const uint64_t expectedFrames[] = {0, 8000, 16000, 24000, 28000, 32000};
    unsigned notes = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const auto& e = events[i];
        if (e.kind != ScheduledEventKind::NoteOn) continue;
        check(notes < 6 && e.note == expectedNotes[notes] &&
            e.absoluteSampleTime == expectedFrames[notes],
            "Song repeats/transition/host-tempo multiplier changed");
        ++notes;
    }
    check(notes == 6 && rt->songPlanner.isFinished() && rt->scheduler.isPlaying(),
        "finished Song must retain silent logical clock for queued relaunch");
    launch.quantization.store(uint32_t(PatternVariationLaunch::NextSongRow));
    launch.revision.store(1);
    audioThread = true;
    rt->scheduler.process(8000, events.data(), events.size());
    audioThread = false;
    check(launch.dueRevision == 1 && launch.consumedRevision == 1,
        "finished Song admits queued runtime at its next logical boundary");
    auto selected = std::make_unique<ClapPlaybackRuntime>(d, 8000);
    selected->initialSongRow = 1;
    check(selected->arm(0, 60, 8000, 0) && selected->initialSongRow == 0 &&
        selected->songPlanner.currentRowIndex() == 1 &&
        selected->scheduler.transport().bpm == 120,
        "queued Song row arm starts once at selected row/rate");
    SongRow settings;
    settings.tempoMultiplier = 1.5;
    settings.swing = .7;
    settings.patternLoop = SongPatternLoop{1, 3};
    const auto rowClock = selected->songRowClock(&settings, selected->hostClock(80, 48000));
    check(rowClock.bpm == 120 && rowClock.swing == .7 && rowClock.loopEnabled &&
        rowClock.loopStartRow == 1 && rowClock.loopEndRow == 3 && !rowClock.timingWarpEnabled,
        "Song row clock preserves swing/region and clears unselected warp");
}

void launchTests()
{
    auto d = fixture();
    d.transport.ticksPerBeat = 4;
    for (auto q : {PatternVariationLaunch::NextTick, PatternVariationLaunch::NextBeat,
             PatternVariationLaunch::NextPatternCycle, PatternVariationLaunch::NextSongRow}) {
        PatternLaunchMailbox mailbox;
        auto rt = std::make_unique<ClapPlaybackRuntime>(d, 8000, &mailbox);
        mailbox.quantization.store(uint32_t(q));
        mailbox.revision.store(7);
        const LogicalTickBoundary before {0, 0, 0}, boundary {3, 3, 6000};
        const auto first = ClapPlaybackRuntime::advancePatternLaunch(rt.get(), before);
        check(first == (q == PatternVariationLaunch::NextTick
            ? LogicalTickBoundaryAction::StopAfterBoundary : LogicalTickBoundaryAction::Continue),
            "quantized runtime did not wait for its boundary");
        const auto due = ClapPlaybackRuntime::advancePatternLaunch(rt.get(), boundary);
        check(mailbox.dueRevision == 7 && mailbox.consumedRevision == 7 &&
            (due == LogicalTickBoundaryAction::StopAfterBoundary || q == PatternVariationLaunch::NextTick),
            "quantized runtime handoff revision was lost");
        check(ClapPlaybackRuntime::advancePatternLaunch(rt.get(), boundary) ==
            LogicalTickBoundaryAction::Continue, "launch boundary consumed twice");
    }
}

void retirementTests()
{
    auto d = fixture();
    RuntimeRetirementQueue queue;
    check(!queue.full() && queue.retire(nullptr), "empty retirement queue");
    for (unsigned i = 0; i < RuntimeRetirementQueue::kCapacity; ++i) {
        auto* runtime = new ClapPlaybackRuntime(d, 8000);
        audioThread = true;
        const bool accepted = queue.retire(runtime);
        audioThread = false;
        check(accepted, "queue rejected capacity slot");
        if (!accepted) delete runtime;
    }
    auto extra = std::make_unique<ClapPlaybackRuntime>(d, 8000);
    audioThread = true;
    const bool full = queue.full(), accepted = queue.retire(extra.get());
    audioThread = false;
    check(full && !accepted, "full queue must defer swap without taking ownership");
    queue.drain();
    check(!queue.full(), "main-thread drain did not free queue capacity");
    // Producer and consumer run on different threads through repeated wraps.
    for (int batch = 0; batch < 3; ++batch) {
        std::array<ClapPlaybackRuntime*, 64> prepared;
        for (auto& p : prepared) p = new ClapPlaybackRuntime(d, 8000);
        std::atomic<bool> done {false};
        std::thread audio([&] {
            audioThread = true;
            for (auto* p : prepared) while (!queue.retire(p)) {}
            audioThread = false;
            done.store(true);
        });
        while (!done.load()) { queue.drain(); std::this_thread::yield(); }
        audio.join();
        queue.drain();
        check(!queue.full(), "concurrent retirement ring failed to wrap/drain");
    }
}
} // namespace

void* operator new(std::size_t n)
{
    if (audioThread) ++audioAllocations;
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept
{
    if (p && audioThread) ++audioDeletions;
    std::free(p);
}
void operator delete(void* p, std::size_t) noexcept { ::operator delete(p); }

int main()
{
    clockTests();
    patternTests();
    songTests();
    launchTests();
    retirementTests();
    check(audioAllocations == 0 && audioDeletions == 0,
        "runtime processing/retirement allocated or deleted on the audio thread");
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
