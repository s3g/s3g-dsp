#include "s3g/tracker/clap_midi_engine.h"
#include "s3g/tracker/clap_document_controller.h"
#include "s3g/tracker/editor_state.h"
#include "s3g/tracker/project_codec.h"

#include <pthread.h>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>

namespace {

using namespace s3g::tracker;

struct Probe {
    bool passed = false;
};

void* createAndRestore(void* context)
{
    auto* probe = static_cast<Probe*>(context);
    auto engine = std::make_unique<midi::Engine>();
    std::string state;
    auto restored = std::make_unique<ProjectDocument>();
    probe->passed = encodeProjectDocument(engine->document, state).ok()
        && decodeProjectDocument(state, *restored).ok()
        && restored->patternBank.activePatternId
            == engine->document.patternBank.activePatternId;
    return nullptr;
}

bool defaultPatternMatchesPriorFactory()
{
    auto engine = std::make_unique<midi::Engine>();
    auto state = std::make_unique<app::TrackerViewState>();
    (void)CommandEngine::execute(state->session, "kit superior basic");
    state->session.pattern.tracks.resize(4u);
    state->session.laneDefaultNotes.resize(4u);
    for (auto alias = state->session.aliases.begin();
         alias != state->session.aliases.end();) {
        if (alias->second >= 4u) alias = state->session.aliases.erase(alias);
        else ++alias;
    }
    state->session.aliases["t"] = 2u;
    state->session.aliases["tom"] = 2u;
    state->session.pattern.name = "FOUR ON THE FLOOR";
    state->session.pattern.tracks[0u].name = "Kick";
    state->session.pattern.tracks[1u].name = "Snare";
    state->session.pattern.tracks[2u].name = "Tom";
    state->session.pattern.tracks[3u].name = "Hat";
    (void)CommandEngine::execute(state->session, "mask 1 x---x---x---x---");
    (void)CommandEngine::execute(state->session, "mask 2 ----x-------x---");
    (void)CommandEngine::execute(state->session, "mask 3 --------------xx");
    (void)CommandEngine::execute(state->session, "mask 4 x-x-x-x-x-x-x-x-");
    state->patternBank = makeDefaultPatternBank();
    (void)syncSessionToActivePattern(*state);
    state->session.transport.sampleRate = 48000.0;
    state->session.transport.ticksPerBeat = 4u;
    state->session.transport.bpm = 120.0;
    state->session.transport.swing = 0.5;
    state->session.gateMilliseconds = 90.0;
    state->instrumentRack.midiRoutes[0u].channel = 1u;

    auto expected = std::make_unique<ProjectDocument>();
    expected->patternBank = state->patternBank;
    expected->burstBanks[0u].library = state->session.burstLibrary;
    expected->transport = state->session.transport;
    expected->warpLibrary = state->session.warpLibrary;
    expected->session.gateMilliseconds = state->session.gateMilliseconds;
    expected->session.tempoScale = 1.0;
    expected->session.commandRngState = state->session.commandRngState;
    expected->session.playbackSeed = state->session.playbackSeed;
    expected->instrumentRack = state->instrumentRack;
    expected->song.name = "SONG";
    expected->song.ticksPerBeat = state->session.transport.ticksPerBeat;
    SongRow row;
    row.patternId = state->patternBank.activePatternId;
    row.durationTicks = static_cast<uint32_t>(std::max<std::size_t>(
        state->session.pattern.visibleRows, 1u));
    row.swing = state->session.transport.swing;
    expected->song.rows.push_back(row);
    expected->song.rows.push_back(row);
    normalizeMidiOnlyDocument(*expected);

    std::string prior, current;
    return encodeProjectDocument(*expected, prior).ok()
        && encodeProjectDocument(engine->document, current).ok()
        && prior == current;
}

} // namespace

int main()
{
    if (!defaultPatternMatchesPriorFactory()) {
        std::fprintf(stderr, "Tracker factory state changed\n");
        return 1;
    }
    pthread_attr_t attributes;
    if (pthread_attr_init(&attributes) != 0) return 1;
    constexpr std::size_t kLiveAudioWorkerStack = 544u * 1024u;
    const int stackResult = pthread_attr_setstacksize(
        &attributes, kLiveAudioWorkerStack);
    Probe probe;
    pthread_t worker {};
    const int createResult = stackResult == 0
        ? pthread_create(&worker, &attributes, createAndRestore, &probe)
        : stackResult;
    pthread_attr_destroy(&attributes);
    if (createResult != 0 || pthread_join(worker, nullptr) != 0
        || !probe.passed) {
        std::fprintf(stderr, "Tracker failed on a 544 KB host worker stack\n");
        return 1;
    }
    std::puts("Tracker factory/recall on 544 KB stack: ok");
    return 0;
}
