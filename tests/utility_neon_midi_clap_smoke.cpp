#include <clap/clap.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>
#include <clap/ext/note-ports.h>
#include "s3g_neon_midi.h"
#if defined(S3G_TEST_TRACKER_RECORDER)
#include "s3g/tracker/midi_step_recorder.h"
#endif
#include <cmath>
#include <dlfcn.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <vector>

namespace {
namespace nm = s3g::controller::neon_midi;
unsigned checks = 0u;
void check(bool value, const char* message) {
    ++checks;
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
struct List {
    struct Event {
        clap_event_midi_t midi {};
        clap_event_param_value_t param {};
        clap_event_midi_sysex_t sysex {};
        std::array<uint8_t, 256u> bytes {};
        uint16_t type = CLAP_EVENT_MIDI;
        const clap_event_header_t* header() const {
            return type == CLAP_EVENT_MIDI_SYSEX ? &sysex.header
                : type == CLAP_EVENT_PARAM_VALUE ? &param.header : &midi.header;
        }
    };
    std::array<Event, 512u> events {};
    uint32_t count = 0u, limit = 512u;
    clap_input_events_t in {this, size, get};
    clap_output_events_t out {this, push};
    static uint32_t size(const clap_input_events_t* p) { return static_cast<List*>(p->ctx)->count; }
    static const clap_event_header_t* get(const clap_input_events_t* p, uint32_t i) {
        const auto& list = *static_cast<List*>(p->ctx);
        return i < list.count ? list.events[i].header() : nullptr;
    }
    static bool push(const clap_output_events_t* p, const clap_event_header_t* h) {
        auto& list = *static_cast<List*>(p->ctx);
        if (list.count >= list.limit || list.count >= list.events.size()) return false;
        if (h->type != CLAP_EVENT_MIDI && h->type != CLAP_EVENT_MIDI_SYSEX
            && h->type != CLAP_EVENT_PARAM_VALUE) return true;
        auto& event = list.events[list.count++];
        event.type = h->type;
        if (h->type == CLAP_EVENT_MIDI) event.midi = *reinterpret_cast<const clap_event_midi_t*>(h);
        else if (h->type == CLAP_EVENT_PARAM_VALUE) event.param = *reinterpret_cast<const clap_event_param_value_t*>(h);
        else {
            event.sysex = *reinterpret_cast<const clap_event_midi_sysex_t*>(h);
            check(event.sysex.size <= event.bytes.size(), "bounded test SysEx");
            std::memcpy(event.bytes.data(), event.sysex.buffer, event.sysex.size);
            event.sysex.buffer = event.bytes.data();
        }
        return true;
    }
    void midi(uint32_t time, uint8_t status, uint8_t key, uint8_t value) {
        clap_event_midi_t e {};
        e.header = {sizeof(e), time, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_MIDI, 0u};
        e.data[0] = status; e.data[1] = key; e.data[2] = value;
        check(push(&out, &e.header), "test input capacity");
    }
    void param(clap_id id, double value, uint32_t time = 0u) {
        clap_event_param_value_t e {};
        e.header = {sizeof(e), time, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0u};
        e.param_id = id; e.value = value; e.note_id = -1; e.port_index = e.channel = e.key = -1;
        check(push(&out, &e.header), "parameter capacity");
    }
    std::vector<clap_event_midi_t> notes() const {
        std::vector<clap_event_midi_t> result;
        for (uint32_t i = 0u; i < count; ++i)
            if (events[i].type == CLAP_EVENT_MIDI) result.push_back(events[i].midi);
        return result;
    }
    void clear() { count = 0u; limit = 512u; }
};
struct Module {
    void* library = nullptr;
    const clap_plugin_entry_t* entry = nullptr;
    const clap_plugin_t* plugin = nullptr;
    clap_host_t host {};
    const clap_plugin_params_t* params = nullptr;
    bool active = false;
    Module(const char* path) {
        library = dlopen(path, RTLD_NOW | RTLD_LOCAL);
        if (!library) std::fprintf(stderr, "%s\n", dlerror());
        check(library != nullptr, "load CLAP binary");
        entry = static_cast<const clap_plugin_entry_t*>(dlsym(library, "clap_entry"));
        check(entry && entry->init(path), "CLAP entry");
        auto* factory = static_cast<const clap_plugin_factory_t*>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
        check(factory && factory->get_plugin_count(factory) == 1u, "factory count");
        host.clap_version = CLAP_VERSION; host.name = "NEON MIDI test"; host.vendor = "s3g";
        host.url = ""; host.version = "1";
        host.get_extension = [](const clap_host_t*, const char*) -> const void* { return nullptr; };
        host.request_restart = host.request_process = host.request_callback = [](const clap_host_t*) {};
        plugin = factory->create_plugin(factory, &host, factory->get_plugin_descriptor(factory, 0u)->id);
        check(plugin && plugin->init(plugin), "initialize plugin");
        params = static_cast<const clap_plugin_params_t*>(plugin->get_extension(plugin, CLAP_EXT_PARAMS));
    }
    void activate() { check(plugin->activate(plugin, 48000., 1u, 256u), "activate");
        check(plugin->start_processing(plugin), "start"); active = true; }
    void set(clap_id id, double value) { List in, out; in.param(id, value); params->flush(plugin, &in.in, &out.out); }
    double value(clap_id id) const { double v = -1.; check(params && params->get_value(plugin, id, &v), "get parameter"); return v; }
    void run(List& in, List& out, bool playing = false, clap_audio_buffer_t* audio = nullptr) {
        clap_event_transport_t transport {};
        transport.flags = CLAP_TRANSPORT_HAS_TEMPO | CLAP_TRANSPORT_HAS_BEATS_TIMELINE
            | (playing ? CLAP_TRANSPORT_IS_PLAYING : 0u);
        transport.tempo = 120.;
        clap_process_t data {}; data.frames_count = 256u; data.transport = &transport;
        data.in_events = &in.in; data.out_events = &out.out;
        data.audio_outputs = audio; data.audio_outputs_count = audio ? 1u : 0u;
        check(plugin->process(plugin, &data) != CLAP_PROCESS_ERROR, "process");
    }
    ~Module() { if (active) { plugin->stop_processing(plugin); plugin->deactivate(plugin); }
        if (plugin) plugin->destroy(plugin); if (entry) entry->deinit(); if (library) dlclose(library); }
};
struct State {
    std::vector<uint8_t> data;
    size_t cursor = 0u;
    clap_ostream_t out {this, [](const clap_ostream_t* stream, const void* bytes, uint64_t n) -> int64_t {
        auto& s = *static_cast<State*>(stream->ctx);
        const auto* b = static_cast<const uint8_t*>(bytes);
        n = std::min<uint64_t>(n, 3u); s.data.insert(s.data.end(), b, b + n); return static_cast<int64_t>(n);
    }};
    clap_istream_t in {this, [](const clap_istream_t* stream, void* bytes, uint64_t n) -> int64_t {
        auto& s = *static_cast<State*>(stream->ctx);
        n = std::min<uint64_t>({n, 2u, s.data.size() - s.cursor});
        if (n) std::memcpy(bytes, s.data.data() + s.cursor, static_cast<size_t>(n));
        s.cursor += static_cast<size_t>(n); return static_cast<int64_t>(n);
    }};
};

void testUtility(Module& p) {
    check(p.params && p.params->count(p.plugin) == 5u, "five utility controls");
    auto* ports = static_cast<const clap_plugin_note_ports_t*>(p.plugin->get_extension(p.plugin, CLAP_EXT_NOTE_PORTS));
    check(ports && ports->count(p.plugin, true) == 1u && ports->count(p.plugin, false) == 1u, "one MIDI input/output");
    check(!p.plugin->get_extension(p.plugin, CLAP_EXT_AUDIO_PORTS), "MIDI only");
    for (uint32_t i = 0u; i < 5u; ++i) {
        clap_param_info_t info {}; char text[128] {}; double value = -1.;
        check(p.params->get_info(p.plugin, i, &info), "parameter info");
        check(p.params->value_to_text(p.plugin, info.id, info.default_value, text, sizeof(text))
            && p.params->text_to_value(p.plugin, info.id, text, &value) && value == info.default_value, "text roundtrip");
    }
    p.set(4u, 0.); p.activate();
    List in, out;
    for (uint8_t bank = 0u; bank < 4u; ++bank) {
        in.clear(); out.clear(); in.midi(0, static_cast<uint8_t>(0x93u + bank), 0u, 127u);
        for (uint8_t pad = 0; pad < 8u; ++pad) {
            in.midi(2u + pad * 4u, 0x97, pad, static_cast<uint8_t>(40u + pad));
            in.midi(3u + pad * 4u, 0x87, pad, 0u);
        }
        p.run(in, out); const auto notes = out.notes();
        check(notes.size() == 16u, "eight notes and releases per bank");
        for (uint8_t pad = 0; pad < 8u; ++pad) {
            check(notes[pad * 2u].data[0] == 0x90 && notes[pad * 2u].data[1] == 36u + bank * 8u + pad
                && notes[pad * 2u].data[2] == 40u + pad && notes[pad * 2u].header.time == 2u + pad * 4u, "mapped bank, velocity, timing");
            check(notes[pad * 2u + 1u].data[0] == 0x80 && notes[pad * 2u + 1u].data[1] == notes[pad * 2u].data[1], "release mapping");
        }
        check(p.value(1u) == bank, "hardware bank reflected in parameter");
    }
    in.clear(); out.clear(); p.set(1u, 0.);
    for (unsigned route = 0u; route < 2u; ++route) {
        p.set(4u, route);
        for (uint8_t velocity = 1u; velocity < 128u; ++velocity) {
            in.clear(); out.clear();
            in.midi(4, 0x97, 0, velocity); in.midi(17, 0x97, 0, 0);
            p.run(in, out); const auto notes = out.notes();
            check(notes.size() == 2u && notes[0].data[2] == velocity
                && notes[1].data[0] == 0x80 && notes[1].data[2] == 0,
                "all 127 attack velocities preserved in both routes; zero releases");
        }
    }
    p.set(4u, 0.); in.clear(); out.clear();
    // Replay the measured factory sequence, not just ordinary keyboard notes.
    for (unsigned route = 0u; route < 2u; ++route) {
        p.set(4u, route);
        for (uint8_t bank = 0u; bank < 4u; ++bank) {
            p.set(1u, bank);
            for (uint8_t velocity = 1u; velocity < 128u; ++velocity) {
                const uint8_t pad = velocity % 8u;
                in.clear(); out.clear();
                in.midi(0, 0xb7, pad, velocity); in.midi(1, 0x97, pad, 127);
                in.midi(2, 0xa7, pad, 100); in.midi(3, 0x97, pad, 0);
                p.run(in, out); const auto hit = out.notes();
                check(hit.size() == 2u && hit[0].data[1] == 36u + bank * 8u + pad
                    && hit[0].data[2] == velocity && hit[0].header.time == 1u && hit[1].data[0] == 0x80,
                    "NEON CC + fixed-127 note uses measured velocity exactly once in both routes/all banks");
            }
        }
    }
    p.set(1u, 0.); p.set(4u, 0.);
    in.clear(); out.clear(); in.midi(255, 0xb7, 0, 3); p.run(in, out);
    check(out.count == 0u, "Velocity CC alone must not trigger music");
    in.clear(); out.clear(); in.midi(0, 0x97, 0, 127); in.midi(3, 0x97, 0, 0); p.run(in, out);
    check(out.notes().size() == 2u && out.notes()[0].data[2] == 3u, "Velocity CC crosses host block boundary");
    in.clear(); out.clear(); in.midi(0, 0xb7, 0, 3); p.run(in, out);
    in.clear(); out.clear(); for (unsigned i = 0; i < 4u; ++i) p.run(in, out);
    in.midi(0, 0x97, 0, 127); in.midi(1, 0x97, 0, 0); p.run(in, out);
    check(out.notes()[0].data[2] == 127u, "Old CC cannot supply the next fixed-velocity hit");
    in.clear(); out.clear(); in.midi(0, 0xb7, 0, 3); p.run(in, out);
    p.plugin->reset(p.plugin); in.clear(); out.clear();
    in.midi(0, 0x97, 0, 127); in.midi(1, 0x97, 0, 0); p.run(in, out);
    check(out.notes()[0].data[2] == 127u, "Host reset clears pending measured velocity");
    in.clear(); out.clear();
    in.midi(2, 0x97, 0, 127); in.midi(3, 0x94, 0, 127);
    in.param(2u, 48., 4u); in.param(3u, 2., 5u);
    in.midi(6, 0x99, 0x28, 0); // release changed page/channel/shift address
    p.run(in, out); auto notes = out.notes();
    check(notes.size() == 2u && notes[1].data[0] == 0x80 && notes[1].data[1] == 36u, "held release latches original note and channel");
    p.set(2u, 36.); p.set(3u, 1.);
    in.clear(); out.clear();
    for (uint8_t key : {uint8_t{8}, uint8_t{16}, uint8_t{24}, uint8_t{32}, uint8_t{96}, uint8_t{120}}) {
        in.midi(0, 0x97, key, 100); in.midi(1, 0x87, key, 0);
    }
    in.midi(2, 0x96, 0x0d, 127); in.midi(3, 0x97, 0, 100); in.midi(4, 0x87, 0, 0);
    in.midi(5, 0x86, 0x0d, 0); in.midi(6, 0x9b, 0x20, 127); in.midi(7, 0x90, 36, 100);
    p.run(in, out); check(out.count == 0u, "tools, shift, modifiers, lamps and unrelated MIDI are not recorded");
    // CHOP banks do not replace performance banks.
    in.clear(); out.clear(); p.set(1u, 2.);
    in.midi(0, 0x93, 0x06, 127); in.midi(1, 0x94, 1, 127);
    in.midi(2, 0x93, 5, 127); in.midi(3, 0x97, 0, 100); in.midi(4, 0x97, 0, 0);
    p.run(in, out); check(out.notes()[0].data[1] == 52u, "CHOP bank independent of sample bank");
    // Reject a release; it must be retried without a new note-on.
    in.clear(); out.clear(); in.midi(0, 0x97, 0, 100); p.run(in, out);
    in.clear(); out.clear(); out.limit = 0u; in.midi(3, 0x87, 0, 0); p.run(in, out);
    in.clear(); out.clear(); p.run(in, out);
    check(out.notes().size() == 1u && out.notes()[0].data[0] == 0x80, "release survives output rejection");
    in.clear(); out.clear(); in.midi(0, 0x97, 1, 100); p.run(in, out, true);
    in.clear(); out.clear(); p.run(in, out, false);
    check(out.notes().size() == 1u && out.notes()[0].data[0] == 0x80, "transport stop releases held input");
    in.clear(); out.clear(); in.midi(0, 0x97, 0, 100); in.midi(2, 0x97, 0, 110); p.run(in, out);
    notes = out.notes(); check(notes.size() == 3u && notes[1].data[0] == 0x80 && notes[2].data[0] == 0x90, "retrigger off before on");
    p.set(5u, 1.); in.clear(); out.clear(); p.run(in, out);
    check(out.notes().size() == 1u && p.value(5u) == 0., "momentary release held");
    in.clear(); out.clear(); in.midi(0, 0x97, 7, 100); p.run(in, out);
    p.plugin->reset(p.plugin); in.clear(); out.clear(); p.run(in, out);
    check(out.notes().size() == 1u && out.notes()[0].data[0] == 0x80, "reset drains held release");

    auto* state = static_cast<const clap_plugin_state_t*>(p.plugin->get_extension(p.plugin, CLAP_EXT_STATE));
    p.set(1u, 3.); p.set(2u, 42.); p.set(3u, 6.); p.set(4u, 1.);
    State saved; check(state && state->save(p.plugin, &saved.out) && saved.data.size() == 9u, "state partial writes");
    p.set(1u, 0.); check(state->load(p.plugin, &saved.in) && p.value(1u) == 3. && p.value(2u) == 42. && p.value(3u) == 6., "state roundtrip partial reads");
    for (size_t length = 0u; length < saved.data.size(); ++length) {
        State shortState; shortState.data.assign(saved.data.begin(), saved.data.begin() + static_cast<ptrdiff_t>(length));
        check(!state->load(p.plugin, &shortState.in) && p.value(1u) == 3., "truncated state transactional");
    }
    saved.cursor = 0; saved.data[5] = 127;
    check(!state->load(p.plugin, &saved.in) && p.value(1u) == 3., "invalid state transactional");
    p.set(2u, std::numeric_limits<double>::quiet_NaN()); check(p.value(2u) == 42., "invalid parameter ignored");
    p.set(1u, 0.); p.set(2u, 36.); p.set(3u, 1.); p.set(4u, 1.);
}

void testChain(Module& utility, const char* trackerPath, const char* neonPath) {
    Module tracker(trackerPath), neon(neonPath);
    tracker.activate(); neon.set(5u, 1.); neon.set(7u, 1.); neon.activate();
    std::array<std::array<float, 256u>, 32u> samples {};
    std::array<float*, 32u> channels {};
    for (size_t i = 0; i < channels.size(); ++i) channels[i] = samples[i].data();
    clap_audio_buffer_t audio {}; audio.channel_count = 32u; audio.data32 = channels.data();
    List raw, translated, through, result;
    raw.midi(0, 0x94, 0, 127); // bank B
    raw.midi(10, 0x97, 0, 100); raw.midi(20, 0x87, 0, 0);
    raw.midi(30, 0x96, 0x0d, 127); // MODE modifier + B1 changes trigger mode, not a note
    raw.midi(31, 0x97, 0, 100); raw.midi(32, 0x87, 0, 0); raw.midi(33, 0x86, 0x0d, 0);
    raw.midi(40, 0xb6, 0x04, 1); // LOOP encoder adjusts selected B1 gain
    utility.run(raw, translated); tracker.run(translated, through);
    const auto notes = through.notes();
    check(notes.size() == 2u && notes[0].data[0] == 0x90 && notes[0].data[1] == 44u
        && notes[0].data[2] == 100u
        && notes[0].header.time == 10u && notes[1].data[0] == 0x80 && notes[1].header.time == 20u,
        "actual Utility -> Tracker REC OFF forwards one note pair at original times");
    const clap_id triggerB1 = 1000u + 8u * 32u + 15u;
    const auto before = neon.value(triggerB1);
    const auto gainBefore = neon.value(1000u + 8u * 32u);
    neon.run(through, result, false, &audio);
    check(neon.value(triggerB1) != before && neon.value(1015u) == before,
        "bridged MODE+pad edits B1, not A1, through actual Tracker and Sample Neon binaries");
    check(neon.value(1000u + 8u * 32u) == gainBefore + .5, "encoder controls survive the serial chain");
    const auto count = translated.count;
    check(through.count == count, "Tracker preserved every musical/control message exactly once");
    for (uint32_t i = 0; i < count; ++i)
        if (translated.events[i].type == CLAP_EVENT_MIDI_SYSEX)
            check(through.events[i].type == CLAP_EVENT_MIDI_SYSEX
                && through.events[i].sysex.header.time == translated.events[i].sysex.header.time
                && through.events[i].sysex.size == 15u
                && !std::memcmp(through.events[i].bytes.data(), translated.events[i].bytes.data(), 15u), "transparent control envelope");
    for (uint8_t velocity : {1u, 3u, 32u, 64u, 96u, 127u}) {
        raw.clear(); translated.clear(); through.clear(); result.clear();
        raw.midi(7, 0xb7, 1, velocity); raw.midi(8, 0x97, 1, 127); raw.midi(12, 0xa7, 1, 90);
        raw.midi(16, 0x87, 1, 0);
        utility.run(raw, translated); tracker.run(translated, through);
        const auto passed = through.notes();
        check(passed.size() == 2u && passed[0].data[1] == 45u && passed[0].data[2] == velocity
            && passed[0].header.time == 8u && passed[1].header.time == 16u,
            "Utility -> Tracker preserves soft/hard hit velocity independently of pressure");
        neon.run(through, result, false, &audio);
#if defined(S3G_TEST_TRACKER_RECORDER)
        // Exercise the same recorder that Tracker's editor consumes, including
        // STEP chord mode and both live modes, not only the MIDI-thru output.
        using namespace s3g::tracker;
        for (auto mode : {MidiStepRecordMode::Step, MidiStepRecordMode::LiveQuantized,
                MidiStepRecordMode::LiveUnquantized}) {
            TrackerSession session;
            session.pattern.visibleRows = 8u; session.pattern.tracks.resize(1u);
            auto& track = session.pattern.tracks[0];
            track.notes.resize(8u, NoteCell::rest()); track.noteColumn.length = 8u;
            track.velocities.resize(8u, ValueCell::defaultValue()); track.velocityColumn.length = 8u;
            MidiLiveRecordState live;
            MidiStepCapture hit;
            hit.note = passed[0].data[1]; hit.velocity = passed[0].data[2]; hit.channel = 1u;
            hit.rowKnown = hit.timingKnown = true; hit.row = 0u;
            check(recordMidiStep(session, mode, hit, 48000., &live).recorded()
                && std::abs(track.velocities[0].valueVoice(0u) - float(velocity) / 127.f) < 1.e-6f,
                "Hardware sequence -> Utility -> Tracker -> recorded VOL equals measured velocity / 127");
        }
#endif
    }
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 2 && argc != 4) return 2;
    auto utility = std::make_unique<Module>(argv[1]);
    testUtility(*utility);
    if (argc == 4) testChain(*utility, argv[2], argv[3]);
    std::printf("NEON MIDI: %u checks passed\n", checks);
    return 0;
}
