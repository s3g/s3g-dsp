#include <clap/clap.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>
#include <clap/ext/note-ports.h>
#include <clap/ext/note-name.h>
#include "s3g_neon_midi.h"
#include "s3g_neon_midi_stream.h"
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
#include <string>
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
    void midi(uint32_t time, uint8_t status, uint8_t key, uint8_t value, uint16_t port = 0) {
        clap_event_midi_t e {};
        e.header = {sizeof(e), time, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_MIDI, 0u};
        e.data[0] = status; e.data[1] = key; e.data[2] = value;
        e.port_index = port;
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
    check(p.params && p.params->count(p.plugin) == 13u, "stable controls plus independent keyboard roles/channels");
    auto* ports = static_cast<const clap_plugin_note_ports_t*>(p.plugin->get_extension(p.plugin, CLAP_EXT_NOTE_PORTS));
    check(ports && ports->count(p.plugin, true) == 2u && ports->count(p.plugin, false) == 1u, "two addressable host inputs, one merged musical output");
    check(!p.plugin->get_extension(p.plugin, CLAP_EXT_AUDIO_PORTS), "MIDI only");
    for (uint32_t i = 0u; i < 13u; ++i) {
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
    // Pressure is a Sample Neon control envelope, never another note or raw
    // MIDI aftertouch. It follows the held cell even after a bank change.
    for (unsigned route = 0u; route < 2u; ++route) {
        p.set(4u, route);
        for (uint8_t bank = 0u; bank < 4u; ++bank) {
            p.set(1u, bank);
            in.clear(); out.clear(); in.midi(0, 0x97, 3, 32); p.run(in, out);
            p.set(1u, (bank + 1u) % 4u);
            for (uint8_t pressure : {0u, 1u, 64u, 127u}) {
                in.clear(); out.clear(); in.midi(11, 0xa7, 3, pressure); p.run(in, out);
                check(out.notes().empty(), "aftertouch emits no extra notes or raw MIDI");
                unsigned pressures = 0u;
                for (uint32_t i = 0u; i < out.count; ++i) {
                    const auto& event = out.events[i]; nm::BridgeMessage message;
                    if (event.type == CLAP_EVENT_MIDI_SYSEX
                        && nm::decodeBridge(event.sysex.buffer, event.sysex.size, message)
                        && message.kind == nm::BridgeKind::Pressure) {
                        ++pressures;
                        check(message.cell == bank * 8u + 3u && message.midi.data2 == pressure
                            && event.sysex.header.time == 11u,
                            "aftertouch retains its original cell, value and time across bank changes");
                    }
                }
                check(pressures == route, "only Tracker + Sample Neon forwards aftertouch");
                if (!route) check(out.count == 0u, "Notes Only suppresses pressure control envelopes");
            }
            in.clear(); out.clear(); in.midi(0, 0x87, 3, 0); p.run(in, out);
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
        // Editing pages are selected by buttons, never inferred from pad notes.
        const auto a = s3g::controller::reloop_neon::decode({0x97,key,100});
        const auto page = s3g::controller::reloop_neon::modeLedMessage(0,a.mode,a.layer);
        in.midi(0,page.status,page.data1,127);
        in.midi(0, 0x97, key, 100); in.midi(1, 0x87, key, 0);
    }
    in.midi(2,0x93,5,127);
    in.midi(2, 0x96, 0x0d, 127); in.midi(3, 0x97, 0, 100); in.midi(4, 0x87, 0, 0);
    in.midi(5, 0x86, 0x0d, 0); in.midi(6, 0x9b, 0x20, 127); in.midi(7, 0x90, 36, 100);
    p.run(in, out); check(out.count == 0u, "tools, shift, modifiers, lamps and unrelated MIDI are not recorded");
    // CHOP banks do not replace performance banks.
    in.clear(); out.clear(); p.set(1u, 2.);
    in.midi(0, 0x93, 0x06, 127); in.midi(1, 0x94, 1, 127);
    in.midi(2, 0x93, 5, 127); in.midi(3, 0x97, 0, 100); in.midi(4, 0x97, 0, 0);
    p.run(in, out); check(out.notes()[0].data[1] == 52u, "CHOP bank independent of sample bank");
    for (uint8_t layerBank=0;layerBank<4;++layerBank) {
        in.clear(); out.clear(); p.set(1u,2.);
        in.midi(0,0x93,7,127); in.midi(1,0x93+layerBank,0,127);
        in.midi(2,0x97,0x10,127); in.midi(3,0x87,0x10,0);
        p.run(in,out); check(out.notes().empty() && p.value(1u)==2, "STACK bank/gestures must not become Tracker notes or change its sample bank");
        in.clear();out.clear();in.midi(0,0x93,5,127);in.midi(1,0x97,0,100);in.midi(2,0x87,0,0);
        p.run(in,out);check(out.notes().size()==2 && out.notes()[0].data[1]==52,"return from STACK restores sample bank C");
    }
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
    State saved; check(state && state->save(p.plugin, &saved.out) && saved.data.size() == 20u, "v2 state partial writes");
    p.set(1u, 0.); check(state->load(p.plugin, &saved.in) && p.value(1u) == 3. && p.value(2u) == 42. && p.value(3u) == 6., "state roundtrip partial reads");
    for (size_t length = 0u; length < saved.data.size(); ++length) {
        State shortState; shortState.data.assign(saved.data.begin(), saved.data.begin() + static_cast<ptrdiff_t>(length));
        check(!state->load(p.plugin, &shortState.in) && p.value(1u) == 3., "truncated state transactional");
    }
    saved.cursor = 0; saved.data[5] = 127;
    check(!state->load(p.plugin, &saved.in) && p.value(1u) == 3., "invalid state transactional");
    p.set(2u, std::numeric_limits<double>::quiet_NaN()); check(p.value(2u) == 42., "invalid parameter ignored");
    State legacy; legacy.data = {'N','M','I','D',1,2,36,1,1};
    check(state->load(p.plugin, &legacy.in) && p.value(1) == 2 && p.value(6) == 0 && p.value(7) == 1,
        "old nine-byte state retains host MIDI and supplies independent second-bank default");
    p.set(6,1); p.set(7,3); p.set(8,1);
    State assignments; check(state->save(p.plugin,&assignments.out),"save dual settings");
    for (unsigned u=0;u<2;++u) for(unsigned n=0;n<4;++n)
        assignments.data[12u+4u*u+n] = static_cast<uint8_t>(uint32_t(u ? INT32_MAX : INT32_MIN) >> (8u*n));
    check(state->load(p.plugin,&assignments.in) && p.value(6)==1 && p.value(7)==3 && p.value(8)==1,"restore dual mode, bank and view");
    State assignmentsAgain; check(state->save(p.plugin,&assignmentsAgain.out)
        && assignmentsAgain.data==assignments.data,"signed USB assignments roundtrip without loss");
    State duplicate; duplicate.data=assignments.data;
    std::copy_n(duplicate.data.begin()+12,4,duplicate.data.begin()+16);
    check(!state->load(p.plugin,&duplicate.in),"cannot bind two units to one saved source");
    for (unsigned byte : {9u,10u,11u}) {
        State invalidDual; invalidDual.data=assignments.data; invalidDual.data[byte]=127;
        check(!state->load(p.plugin,&invalidDual.in),"out-of-range dual setting rejected");
    }
    State unchangedDual; check(state->save(p.plugin,&unchangedDual.out) && unchangedDual.data==assignments.data,"invalid dual restores are transactional");
    legacy.cursor=0; check(state->load(p.plugin,&legacy.in),"return to legacy after dual restore");
    p.set(1u, 0.); p.set(2u, 36.); p.set(3u, 1.); p.set(4u, 1.);
}

void testKeyboard(const char* path) {
    auto unit = std::make_unique<Module>(path); auto& p = *unit;
    p.set(4,0); p.set(6,1); p.set(11,1); p.set(12,48); p.set(13,2); p.activate();
    List in,out; in.midi(0,0x97,0,63,0); in.midi(1,0x97,0,97,1); p.run(in,out);
    auto notes=out.notes();
    check(notes.size()==2 && notes[0].data[0]==0x90 && notes[0].data[1]==36 && notes[0].data[2]==63
        && notes[1].data[0]==0x91 && notes[1].data[1]==56 && notes[1].data[2]==97,
        "unit 1 cells and unit 2 bank-B keyboard notes/velocity use independent channels");
    p.set(11,0); // Changing the role must not reinterpret an already-held key's pressure.
    in.clear();out.clear();in.midi(0,0xa7,0,51,1);p.run(in,out);notes=out.notes();
    check(notes.size()==1 && notes[0].data[0]==0xa1 && notes[0].data[1]==56 && notes[0].data[2]==51,
        "Keyboard aftertouch retains pitched note/channel");
    p.set(11,1);
    p.set(7,3);p.set(12,60);p.set(13,4);
    in.clear();out.clear();in.midi(0,0x87,0,0,1);p.run(in,out);notes=out.notes();
    check(notes.size()==1 && notes[0].data[0]==0x81 && notes[0].data[1]==56,
        "held keyboard release retains old bank, first note and channel");
    in.clear();out.clear();in.midi(0,0x9a,2,127,1);p.run(in,out);notes=out.notes();
    check(notes.size()==1 && notes[0].data[0]==0x93 && notes[0].data[1]==86,
        "new Keyboard press uses new range/channel");
    const auto* state=static_cast<const clap_plugin_state_t*>(p.plugin->get_extension(p.plugin,CLAP_EXT_STATE));
    State saved;check(state->save(p.plugin,&saved.out)&&saved.data[4]==4,"keyboard uses extended state");
    auto recalled=std::make_unique<Module>(path);
    const auto* restore=static_cast<const clap_plugin_state_t*>(recalled->plugin->get_extension(recalled->plugin,CLAP_EXT_STATE));
    check(restore->load(recalled->plugin,&saved.in)&&recalled->value(11)==1&&recalled->value(12)==60&&recalled->value(13)==4,
        "independent roles, first keys and channel survive recall");
    State roundtrip;check(restore->save(recalled->plugin,&roundtrip.out)&&roundtrip.data==saved.data,"keyboard state byte roundtrip");
    State invalid;invalid.data=saved.data;invalid.data.back()=17;
    check(!restore->load(recalled->plugin,&invalid.in)&&recalled->value(13)==4,"invalid channel recall is transactional");
    p.set(4,1);in.clear();out.clear();p.run(in,out);
    bool setup=false;
    for (unsigned i=0;i<out.count;++i) if(out.events[i].type==CLAP_EVENT_MIDI_SYSEX) {
        nm::BridgeMessage message;
        if(nm::decodeBridge(out.events[i].sysex.buffer,out.events[i].sysex.size,message))
            setup|=message.kind==nm::BridgeKind::KeyboardSetup&&message.unit==1&&message.channel==3&&message.midi.data1==1&&message.midi.data2==60;
    }
    check(setup,"Keyboard role/range follows addressed control bridge without creating musical notes");
}

void testBankModeInvariant(const char* path) {
    namespace neon = s3g::controller::reloop_neon;
    Module p(path); p.activate(); p.set(6,1); p.set(4,1);
    List in,out;
    for (unsigned unit=0;unit<2;++unit) for (unsigned page=0;page<8;++page) {
        const auto desired = neon::modeLedMessage(0,static_cast<neon::Mode>(page%4),
            page<4 ? neon::Layer::First : neon::Layer::Second);
        in.clear();out.clear();in.midi(0,desired.status,desired.data1,127,unit);p.run(in,out);
        for (unsigned bank=0;bank<4;++bank) for (unsigned stale=0;stale<8;++stale) {
            const auto recalled = neon::modeLedMessage(bank,static_cast<neon::Mode>(stale%4),
                stale<4 ? neon::Layer::First : neon::Layer::Second);
            const auto pad = neon::padSurfaceMessage(bank,static_cast<neon::Mode>(stale%4),
                stale<4 ? neon::Layer::First : neon::Layer::Second,0,127);
            in.clear();out.clear();
            // Exact captured bank + remembered mode-on/off packet, flattened by a host.
            in.midi(0,0x93+bank,1,127,unit);
            in.midi(0,recalled.status,recalled.data1,127,unit);
            in.midi(0,recalled.status,recalled.data1,0,unit);
            in.midi(1,0xb0|(pad.status&15),pad.data1,37,unit);
            in.midi(2,pad.status,pad.data1,127,unit);
            in.midi(3,0xa0|(pad.status&15),pad.data1,83,unit);
            in.midi(4,pad.status-0x10,pad.data1,0,unit);
            p.run(in,out);const auto notes=out.notes();
            check(notes.size()==(page==0 ? 2u : 0u),"bank recall and stale pads cannot change selected performance page/layer");
            if (page==0) check(notes[0].data[1]==36+bank*8 && notes[0].data[2]==37
                && notes[1].data[1]==notes[0].data[1],"stale deck addresses retain current bank, measured velocity and release");
            unsigned modes=0;
            for(unsigned n=0;n<out.count;++n) {
                const auto& e=out.events[n];nm::BridgeMessage b;
                if(e.type==CLAP_EVENT_MIDI_SYSEX && nm::decodeBridge(e.sysex.buffer,e.sysex.size,b)
                    && b.kind==nm::BridgeKind::Control)
                    modes+=neon::decode(b.midi).type==neon::ActionType::SelectMode;
            }
            check(modes==0,"automatic bank page recall never reaches downstream Sample Neon");
        }
    }
    // A real mode press immediately following the bank packet is still intentional.
    in.clear();out.clear();
    in.midi(0,0x94,1,127);in.midi(0,0x94,7,127);in.midi(0,0x94,7,0);
    in.midi(1,0x94,5,127);in.midi(2,0x94,0,127);
    in.midi(3,0x98,0x10,91);in.midi(4,0x88,0x10,0);
    p.run(in,out);check(out.notes().size()==2 && out.notes()[0].data[1]==44,
        "explicit mode selection after recall remains effective without timing lockout");
    // The other USB/host port keeps its own selected page.
    in.clear();out.clear();in.midi(0,0x97,0,91,1);in.midi(1,0x87,0,0,1);p.run(in,out);
    check(out.notes().empty(),"one controller's PLAY button cannot change the other controller's secondary RESAMPLE mode");
}

void testAddressedBridge() {
    nm::InputPacketDecoder first, second;
    unsigned delivered = 0, restored = 0;
    auto receive = [&](s3g::controller::reloop_neon::MidiMessage message, bool restore) {
        ++delivered; restored += restore;
        check(message.data1 < 128 && message.data2 < 128, "stream decoder emits only complete MIDI messages");
    };
    const uint8_t bankPacket[] = {0x94,0x01,0x7f,0x94,0x07,0x7f,0x94,0x07,0};
    second.packet(bankPacket, sizeof(bankPacket), receive);
    check(delivered == 3 && restored == 2, "captured bank-B automatic HOT CUE restore identified within its packet");
    const uint8_t explicitPage[] = {0x94,0x05,0x7f};
    second.packet(explicitPage, sizeof(explicitPage), receive);
    check(delivered == 4 && restored == 2, "later intentional page selection is never suppressed");
    const uint8_t partial[] = {0x97,0}; first.packet(partial,sizeof(partial),receive);
    const uint8_t other[] = {0x97,1,127}; second.packet(other,sizeof(other),receive);
    const uint8_t finish[] = {0xf8,127,0,0}; first.packet(finish,sizeof(finish),receive);
    check(delivered == 7, "per-unit partial and running-status messages survive interleaving and realtime bytes");
    const uint8_t sysex[] = {0xf0,0x0a,0x40,0xf7,0,127}; first.packet(sysex,sizeof(sysex),receive);
    check(delivered == 7, "SysEx and orphan data cannot fabricate a pad");
    first.packet(partial,sizeof(partial),receive); first.reset(); first.packet(finish,sizeof(finish),receive);
    check(delivered == 7, "disconnect reset discards partial/running status");
    for (int32_t uid : {int32_t(2116083961), int32_t(341027543), INT32_MIN, INT32_MAX, int32_t(0)}) {
        nm::BridgeMessage original {nm::BridgeKind::Control, 3, 36, 15, {0xb6, 4, 127}, 0, true, 1, uid};
        const auto packet = nm::encodeAddressedBridge(original);
        nm::BridgeMessage decoded;
        check(nm::decodeBridge(packet.data(), packet.size(), decoded) && decoded.addressed && decoded.unit == 1
            && decoded.destination == uid && decoded.midi.status == 0xb6 && decoded.bank == 3, "v2 exact signed UID and unit roundtrip");
        for (uint32_t n = 0; n < packet.size(); ++n) check(!nm::decodeBridge(packet.data(), n, decoded), "v2 truncation rejected");
        auto invalid = packet; invalid[14] = 2;
        check(!nm::decodeBridge(invalid.data(), invalid.size(), decoded), "invalid unit rejected");
        invalid = packet; invalid[19] = 16;
        check(!nm::decodeBridge(invalid.data(), invalid.size(), decoded), "overflowing destination UID rejected");
        invalid = packet; invalid[7] = 5;
        check(!nm::decodeBridge(invalid.data(), invalid.size(), decoded), "unknown bridge command rejected");
        const auto legacy = nm::encodeBridge(original);
        check(nm::decodeBridge(legacy.data(), legacy.size(), decoded) && !decoded.addressed
            && decoded.unit == 0 && decoded.destination == 0, "v1 clears reused v2 identity");
    }
}

void testFillShortcutRoute(const char* path) {
    Module p(path); p.activate();
    List in, out;
    for (unsigned inputMode : {0u, 1u}) for (unsigned route : {0u, 1u}) {
        p.set(6, inputMode); p.set(4, route);
        in.clear(); out.clear(); p.run(in, out);
        for (unsigned unit=0;unit<(inputMode ? 2u : 1u);++unit) {
            for (bool samplePress : {true, false}) for (unsigned release=0;release<3;++release) {
                const uint8_t pressStatus = samplePress ? 0x96 : 0x94;
                const uint8_t pressKey = samplePress ? 0x52 : 0x55;
                const uint8_t releaseStatus = release == 2 ? (samplePress ? 0x84 : 0x86)
                    : static_cast<uint8_t>(pressStatus-0x10);
                const uint8_t releaseKey = release == 0 ? pressKey : release == 1
                    ? (samplePress ? 0x0d : 0x10) : (samplePress ? 0x10 : 0x0d);
                in.clear(); out.clear();
                in.midi(0,pressStatus,pressKey,127,unit);
                in.midi(1,releaseStatus,releaseKey,0,unit);
                in.midi(2,0x97,0,73,unit); in.midi(3,0x87,0,0,unit);
                p.run(in,out);
                const auto notes = out.notes();
                check(notes.size()==2 && notes[0].data[2]==73,
                    "MODE/CENSOR release aliases never strand a modifier or swallow the next pad");
                unsigned controls = 0;
                for (unsigned n=0;n<out.count;++n) {
                    const auto& e = out.events[n]; nm::BridgeMessage b;
                    if (e.type != CLAP_EVENT_MIDI_SYSEX || !nm::decodeBridge(e.sysex.buffer,e.sysex.size,b)
                        || b.kind != nm::BridgeKind::Control) continue;
                    check(b.addressed == bool(inputMode) && b.unit == unit,
                        "fill shortcut retains USB-unit identity");
                    check(b.midi == (controls == 0
                        ? s3g::controller::reloop_neon::MidiMessage{pressStatus,pressKey,127}
                        : s3g::controller::reloop_neon::MidiMessage{releaseStatus,releaseKey,0}),
                        "fill shortcut press/release remain exact control messages");
                    ++controls;
                }
                check(controls == (route ? 2u : 0u),
                    "fill shortcuts forwarded only in Tracker + Sample Neon route");
            }
        }
    }
}

void testDualUtility(const char* path, const char* trackerPath, const char* neonPath) {
    Module p(path); p.set(6, 1); p.activate();
    List in, out;
    p.run(in, out); in.clear(); out.clear();
    in.midi(1, 0xb7, 0, 17, 0); in.midi(2, 0xb7, 0, 93, 1);
    in.midi(3, 0x97, 0, 127, 0); in.midi(3, 0x97, 0, 127, 1);
    in.midi(4, 0xa7, 0, 24, 0); in.midi(5, 0xa7, 0, 98, 1);
    in.midi(6, 0x95, 0, 127, 1); // U2 changes to C under held B1.
    in.midi(7, 0x97, 0, 0, 0); in.midi(8, 0x97, 0, 0, 1);
    p.run(in, out); auto notes = out.notes();
    check(notes.size() == 4 && notes[0].data[1] == 36 && notes[0].data[2] == 17
        && notes[1].data[1] == 44 && notes[1].data[2] == 93
        && notes[2].data[1] == 36 && notes[3].data[1] == 44, "dual CC velocity and original held releases never borrow the other unit's bank");
    unsigned pressureCount = 0;
    for (unsigned n = 0; n < out.count; ++n) if (out.events[n].type == CLAP_EVENT_MIDI_SYSEX) {
        const auto& e = out.events[n]; nm::BridgeMessage b;
        nm::PadNotes map;
        if (nm::decodeNoteMap(e.sysex.buffer,e.sysex.size,map)) { check(map == nm::sequentialNotes(),"shared default map on dual route"); continue; }
        check(nm::decodeBridge(e.sysex.buffer, e.sysex.size, b) && b.addressed, "dual route uses addressed envelopes");
        if (b.kind == nm::BridgeKind::Pressure) {
            ++pressureCount;
            check(b.cell == (b.unit ? 8 : 0) && b.midi.data2 == (b.unit ? 98 : 24), "dual aftertouch retains its unit and held cell");
        }
    }
    check(pressureCount == 2 && p.value(1) == 0 && p.value(7) == 2, "independent bank state and pressure streams");
    p.set(7, 0); in.clear(); out.clear();
    in.midi(0, 0x97, 2, 100, 0); in.midi(1, 0x97, 2, 110, 1);
    in.midi(2, 0x97, 2, 0, 0); p.run(in, out);
    check(out.notes().size() == 2, "first same-key release does not close the other unit's gate");
    in.clear(); out.clear(); in.midi(0, 0x97, 2, 0, 1); p.run(in, out);
    check(out.notes().size() == 1 && out.notes()[0].data[0] == 0x80, "final same-key release closes the gate once");
    p.set(7, 1);
    in.clear(); out.clear(); in.midi(0, 0x96, 0x0d, 127, 1); in.midi(1, 0x97, 0, 100, 0); in.midi(2, 0x97, 0, 100, 1); p.run(in, out);
    check(out.notes().size() == 1 && out.notes()[0].data[1] == 36, "U2 MODE modifier does not turn U1 performance into an edit");
    p.set(5, 1); in.clear(); out.clear(); p.run(in, out);
    check(out.notes().size() == 1 && out.notes()[0].data[0] == 0x80, "dual panic drains held musical notes");
    // Critical rejected release/bridge retries happen before a subsequent hit.
    in.clear(); out.clear(); in.midi(0, 0x97, 1, 100, 1); p.run(in, out);
    in.clear(); out.clear(); out.limit = 0; in.midi(0, 0x97, 1, 0, 1); p.run(in, out);
    in.clear(); out.clear(); p.run(in, out);
    check(out.notes().size() == 1 && out.notes()[0].data[0] == 0x80, "U2 rejected note-off retries");
    p.set(6, 3); in.clear(); out.clear(); in.midi(0, 0x97, 0, 127); p.run(in, out);
    check(out.notes().empty(), "direct USB input ignores duplicate host raw MIDI");
    p.set(6, 0); in.clear(); out.clear(); in.midi(0, 0x97, 0, 99, 1); in.midi(1, 0x97, 0, 99); in.midi(2, 0x97, 0, 0); p.run(in, out);
    check(out.notes().size() == 2 && out.notes()[0].data[1] == 36, "returning to legacy mode restores only original host port");
    if (!trackerPath || !neonPath) return;
    Module tracker(trackerPath), neon(neonPath);
    tracker.activate(); neon.set(5, 1); neon.set(7, 1); neon.activate(); p.set(6, 1);
    std::array<std::array<float, 256>, 32> samples {};
    std::array<float*, 32> channels {}; for (unsigned c=0;c<32;++c) channels[c]=samples[c].data();
    clap_audio_buffer_t audio {}; audio.channel_count=32; audio.data32=channels.data();
    List through, result;
    auto chain = [&] { out.clear(); through.clear(); result.clear(); p.run(in,out); tracker.run(out,through); neon.run(through,result,false,&audio); };
    in.clear(); chain();
    const auto triggerA = neon.value(1015), triggerB = neon.value(1271);
    in.clear();
    in.midi(0, 0x96, 0x0d, 127, 1); in.midi(1, 0x97, 0, 110, 1); in.midi(2, 0x97, 0, 0, 1);
    in.midi(3, 0x97, 0, 57, 0); in.midi(4, 0x97, 0, 0, 0); in.midi(5, 0x86, 0x0d, 0, 1);
    chain();
    check(neon.value(1015) == triggerA && neon.value(1271) != triggerB, "Tracker forwards unit identity: U2 modifier edits B1, not A1");
    check(through.notes().size() == 2 && through.notes()[0].data[1] == 36 && through.notes()[0].data[2] == 57,
        "simultaneous U1 performance passes Tracker once while U2 edits");
    const auto gainA = neon.value(1000), gainB = neon.value(1256);
    in.clear(); in.midi(0, 0xb6, 4, 1, 0); chain();
    check(neon.value(1000) > gainA && neon.value(1256) == gainB, "U1 encoder restores its selected A1 context after U2 controls");
    const auto newA = neon.value(1000);
    in.clear(); in.midi(0, 0xb6, 4, 1, 1); chain();
    check(neon.value(1000) == newA && neon.value(1256) > gainB, "U2 encoder retains independent B1 context");
    in.clear(); in.midi(0,0x97,3,100,1); in.midi(1,0x97,3,0,1); chain();
    in.clear(); in.midi(0,0x97,0,100,0); in.midi(1,0x97,0,0,0); chain();
    p.plugin->reset(p.plugin); in.clear(); chain();
    const auto gainB4 = neon.value(1352), retainedB1 = neon.value(1256);
    in.midi(0,0xb6,4,1,1); chain();
    check(neon.value(1352)>gainB4 && neon.value(1256)==retainedB1,
        "reset/reconnect sync preserves the inactive unit's selected editing cell");
    const auto* state = static_cast<const clap_plugin_state_t*>(neon.plugin->get_extension(neon.plugin,CLAP_EXT_STATE));
    const auto repeat = [&] {
        State saved; check(state && state->save(neon.plugin,&saved.out) && saved.data.size()>=12,
            "read fill settings from complete state");
        uint32_t version=0; std::memcpy(&version,saved.data.data()+4,4);
        if (version < 19) return 2.f; // Default fill settings omit the v19 extension.
        float value=0; std::memcpy(&value,saved.data.data()+saved.data.size()-8,4); return value;
    };
    for (unsigned unit=0;unit<2;++unit) {
        const clap_id gainId = unit ? 1352 : 1000;
        const auto beforeGain = neon.value(gainId);
        const auto beforeRepeat = repeat();
        in.clear(); in.midi(0,0x96,0x52,127,unit); chain();
        in.clear(); in.midi(0,0xb6,4,1,unit); chain();
        check(repeat()==beforeRepeat+1 && neon.value(gainId)==beforeGain,
            "primary SAMPLE Shift MODE reaches Fill Hold through Utility and Tracker on either unit");
        in.clear(); in.midi(0,0x84,0x10,0,unit); chain();
        in.clear(); in.midi(0,0xb6,4,1,unit); chain();
        check(repeat()==beforeRepeat+1 && neon.value(gainId)>beforeGain,
            "cross-page CENSOR release restores normal encoder editing through the chain");
    }
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
                && through.events[i].sysex.size == translated.events[i].sysex.size
                && !std::memcmp(through.events[i].bytes.data(), translated.events[i].bytes.data(), through.events[i].sysex.size), "transparent control and note-map envelopes");
    for (uint8_t velocity : {1u, 3u, 32u, 64u, 96u, 127u}) {
        raw.clear(); translated.clear(); through.clear(); result.clear();
        raw.midi(7, 0xb7, 1, velocity); raw.midi(8, 0x97, 1, 127); raw.midi(12, 0xa7, 1, 90);
        raw.midi(16, 0x87, 1, 0);
        utility.run(raw, translated); tracker.run(translated, through);
        const auto passed = through.notes();
        check(passed.size() == 2u && passed[0].data[1] == 45u && passed[0].data[2] == velocity
            && passed[0].header.time == 8u && passed[1].header.time == 16u,
            "Utility -> Tracker preserves soft/hard hit velocity independently of pressure");
        unsigned pressures = 0u;
        for (uint32_t i = 0u; i < through.count; ++i) {
            const auto& event = through.events[i]; nm::BridgeMessage message;
            if (event.type == CLAP_EVENT_MIDI_SYSEX
                && nm::decodeBridge(event.sysex.buffer, event.sysex.size, message)
                && message.kind == nm::BridgeKind::Pressure) {
                ++pressures;
                check(message.cell == 9u && message.midi.data2 == 90u
                    && event.sysex.header.time == 12u,
                    "actual Tracker forwards aftertouch unchanged to Sample Neon's B2");
            }
        }
        check(pressures == 1u, "exactly one pressure envelope survives the actual chain");
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
    // Once hardware has been returned to primary SAMPLE by its feedback
    // owner, every bank still authors notes. Bank changes under a held pad
    // must not retarget its pressure or note-off. Explicit EDIT remains tools.
    for (uint8_t bank = 0u; bank < 4u; ++bank) {
        const auto next = static_cast<uint8_t>((bank + 1u) % 4u);
        raw.clear(); translated.clear(); through.clear(); result.clear();
        raw.midi(0, 0x93u + bank, 0x05, 127); // Explicit primary SAMPLE.
        raw.midi(1, 0x93u + bank, 0x00, 127);
        raw.midi(2, 0x97, 3, 127); // Velocity OFF: no preceding CC.
        raw.midi(6, 0x93u + next, 0x00, 127);
        raw.midi(7, 0xa7, 3, 80);
        raw.midi(8, 0x87, 3, 0);
        raw.midi(9, 0xb7, 3, 3); // Velocity ON: next bank, same physical pad.
        raw.midi(10, 0x97, 3, 127); raw.midi(11, 0x87, 3, 0);
        raw.midi(20, 0x93u + next, 0x07, 127); // Explicit EDIT.
        raw.midi(21, 0x97u + next, 0x10, 127);
        raw.midi(22, 0x87u + next, 0x10, 0);
        utility.run(raw, translated); tracker.run(translated, through);
        neon.run(through, result, false, &audio);
        const auto passed = through.notes();
        check(passed.size() == 4u && passed[0].data[1] == 39u + bank * 8u
            && passed[0].data[2] == 127u && passed[0].header.time == 2u
            && passed[1].data[0] == 0x80u && passed[1].data[1] == passed[0].data[1]
            && passed[1].header.time == 8u && passed[2].data[1] == 39u + next * 8u
            && passed[2].data[2] == 3u && passed[3].data[0] == 0x80u
            && passed[3].data[1] == passed[2].data[1],
            "all banks retain performance notes/velocity and latched releases; explicit EDIT emits no notes");
        unsigned pressures = 0u;
        for (uint32_t i = 0u; i < through.count; ++i) {
            const auto& event = through.events[i]; nm::BridgeMessage message;
            if (event.type == CLAP_EVENT_MIDI_SYSEX
                && nm::decodeBridge(event.sysex.buffer, event.sysex.size, message)
                && message.kind == nm::BridgeKind::Pressure) {
                ++pressures;
                check(message.cell == bank * 8u + 3u && message.midi.data2 == 80u,
                    "aftertouch after a bank switch must address the original held cell");
            }
        }
        check(pressures == 1u, "bank switch lost or duplicated held pressure");
    }
}
void testNoteMap(const char* path, const char* trackerPath, const char* neonPath) {
    auto notes = nm::sequentialNotes();
    notes[0] = 0; notes[8] = 127; notes[31] = 12;
    check(nm::validNotes(notes) && nm::padForNote(notes,0)==0 && nm::padForNote(notes,127)==8
        && nm::padForNote(notes,12)==31 && nm::padForNote(notes,36)<0,"noncontiguous map and endpoints");
    nm::PadNotes parsed;
    std::string list;
    for (auto key : notes) list += std::to_string(key) + ",\t\n";
    check(nm::parseNotes(list,parsed) && parsed==notes,"bank-major clipboard list accepts comma/space/newline");
    for (auto invalid : {list+"1",std::string("-1,")+list,std::string("999999999999,"),std::string("1 1"),std::string("1.5")})
        check(!nm::parseNotes(invalid,parsed) && parsed==notes,"invalid clipboard list is transactional");
    auto packet=nm::encodeNoteMap(notes);
    check(nm::decodeNoteMap(packet.data(),packet.size(),parsed) && parsed==notes,"map wire roundtrip");
    for (unsigned n=0;n<packet.size();++n) check(!nm::decodeNoteMap(packet.data(),n,parsed),"truncated map rejected");
    packet[9]=packet[8]; check(!nm::decodeNoteMap(packet.data(),packet.size(),parsed),"duplicate wire note rejected");
    packet=nm::encodeNoteMap(notes);packet[8]=128;check(!nm::decodeNoteMap(packet.data(),packet.size(),parsed),"non-7bit note rejected");
    nm::PublishedNoteMap published;nm::NoteMap snapshot;snapshot.custom=true;snapshot.notes=notes;
    published.store(snapshot);nm::NoteMap read;
    check(published.read(read) && read.custom && read.notes==notes,"published map roundtrip");

    Module utility(path);
    const auto* state=static_cast<const clap_plugin_state_t*>(utility.plugin->get_extension(utility.plugin,CLAP_EXT_STATE));
    State original;check(state->save(utility.plugin,&original.out),"save default note map");
    State custom;custom.data=original.data;custom.data[4]=3;
    custom.data.insert(custom.data.end(),notes.begin(),notes.end());
    check(state->load(utility.plugin,&custom.in),"load v3 custom note map");
    State saved;check(state->save(utility.plugin,&saved.out) && saved.data==custom.data,"custom map recall stable");
    for (size_t size=20;size<custom.data.size();++size) {
        State shortState;shortState.data.assign(custom.data.begin(),custom.data.begin()+size);
        check(!state->load(utility.plugin,&shortState.in),"truncated custom map rejected");
    }
    State duplicate;duplicate.data=custom.data;duplicate.data[21]=duplicate.data[20];
    check(!state->load(utility.plugin,&duplicate.in),"duplicate custom state rejected");
    State unchanged;check(state->save(utility.plugin,&unchanged.out) && unchanged.data==custom.data,"invalid custom restores transactional");
    utility.activate();
    std::unique_ptr<Module> tracker,neon;
    std::array<std::array<float,256>,32> samples {};
    std::array<float*,32> channels {};for(unsigned c=0;c<32;++c) channels[c]=samples[c].data();
    clap_audio_buffer_t audio {};audio.channel_count=32;audio.data32=channels.data();
    if (trackerPath && neonPath) {
        tracker=std::make_unique<Module>(trackerPath);neon=std::make_unique<Module>(neonPath);
        tracker->activate();neon->set(5,0);neon->set(7,1);neon->activate();
    }
    List in,out,through,result;
    auto chain=[&] {
        out.clear();through.clear();result.clear();utility.run(in,out);
        if (tracker) { tracker->run(out,through);neon->run(through,result,false,&audio); }
    };
    for(unsigned bank=0;bank<4;++bank) {
        utility.set(1,bank);in.clear();
        for(unsigned pad=0;pad<8;++pad) {
            in.midi(pad*4,0xb7,pad,17+pad);in.midi(pad*4+1,0x97,pad,127);in.midi(pad*4+2,0x87,pad,0);
        }
        chain(); const auto emitted=out.notes();
        check(emitted.size()==16,"custom map emits all eight pads per bank");
        for(unsigned pad=0;pad<8;++pad) check(emitted[pad*2].data[1]==notes[bank*8+pad]
            && emitted[pad*2].data[2]==17+pad && emitted[pad*2+1].data[1]==notes[bank*8+pad],"custom keys retain velocity and releases");
        bool received=false;
        for(unsigned i=0;i<out.count;++i) if(out.events[i].type==CLAP_EVENT_MIDI_SYSEX
            && nm::decodeNoteMap(out.events[i].bytes.data(),out.events[i].sysex.size,parsed)) {
                check(parsed==notes,"shared custom map contains all banks");received=true;
            }
        check(received,"custom map sent before performance");
        if (neon) {
            const auto* names=static_cast<const clap_plugin_note_name_t*>(neon->plugin->get_extension(neon->plugin,CLAP_EXT_NOTE_NAME));
            for(unsigned cell=0;cell<32;++cell) { clap_note_name_t name {};check(names && names->get(neon->plugin,cell,&name)
                && name.key==notes[cell],"Tracker passes shared map with Neon ownership off"); }
        }
    }
    // Saving Neon captures a standalone fallback, without needing Utility at recall.
    if(neon) {
        const auto* ns=static_cast<const clap_plugin_state_t*>(neon->plugin->get_extension(neon->plugin,CLAP_EXT_STATE));
        State received;check(ns->save(neon->plugin,&received.out),"save received map in Neon");
        Module recalled(neonPath);const auto* rs=static_cast<const clap_plugin_state_t*>(recalled.plugin->get_extension(recalled.plugin,CLAP_EXT_STATE));
        check(rs->load(recalled.plugin,&received.in),"recall received map without Utility");
        const auto* names=static_cast<const clap_plugin_note_name_t*>(recalled.plugin->get_extension(recalled.plugin,CLAP_EXT_NOTE_NAME));
        for(unsigned cell=0;cell<32;++cell) { clap_note_name_t name {};check(names->get(recalled.plugin,cell,&name)
            && name.key==notes[cell],"standalone fallback retains custom keys"); }
        // Explicit local mode must survive a later upstream map and project recall.
        received.cursor=0;received.data[received.data.size()-33]=0;
        check(rs->load(recalled.plugin,&received.in),"restore local-only map source");
        recalled.activate();List localIn,localOut;
        const auto defaults=nm::encodeNoteMap(nm::sequentialNotes());
        clap_event_midi_sysex_t envelope {};
        envelope.header={sizeof(envelope),0,CLAP_CORE_EVENT_SPACE_ID,CLAP_EVENT_MIDI_SYSEX,0};
        envelope.buffer=defaults.data();envelope.size=static_cast<uint32_t>(defaults.size());
        check(List::push(&localIn.out,&envelope.header),"local map input");
        recalled.run(localIn,localOut,false,&audio);
        clap_note_name_t localName {};
        check(names->get(recalled.plugin,0,&localName) && localName.key==0,"local map ignores received defaults");
        State localSaved;check(rs->save(recalled.plugin,&localSaved.out) && localSaved.data==received.data,
            "local map setting and custom keys survive unchanged");
    }
    utility.set(1,0);in.clear();in.midi(0,0x97,0,61);chain();
    // Restore under a held finger: release must still use key zero, not 36.
    original.cursor=0;check(state->load(utility.plugin,&original.in),"restore default map under hold");
    in.clear();chain();check(out.notes().size()==1 && out.notes()[0].data[0]==0x80 && out.notes()[0].data[1]==0,"map apply releases original key");
    in.clear();in.midi(0,0x87,0,0);chain();check(out.notes().empty(),"late physical release does not author another note");
    utility.set(4,0);in.clear();in.midi(0,0x97,0,70);in.midi(1,0x87,0,0);chain();
    check(out.count==2 && out.notes()[0].data[1]==36,"Notes Only suppresses map envelopes too");
    utility.set(4,1);in.clear();out.clear();out.limit=0;in.midi(0,0x97,0,70);utility.run(in,out);
    check(out.count==0,"map rejection blocks new mapped note");
    in.clear();chain();check(out.notes().empty(),"rejected note never becomes a held release");
    in.clear();in.midi(2,0x97,0,71);in.midi(3,0x87,0,0);chain();
    check(out.notes().size()==2,"mapping retries recover after host backpressure");
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 2 && argc != 4) return 2;
    auto utility = std::make_unique<Module>(argv[1]);
    testUtility(*utility);
    if (argc == 4) testChain(*utility, argv[2], argv[3]);
    testAddressedBridge();
    testBankModeInvariant(argv[1]);
    testKeyboard(argv[1]);
    testFillShortcutRoute(argv[1]);
    testDualUtility(argv[1], argc == 4 ? argv[2] : nullptr, argc == 4 ? argv[3] : nullptr);
    testNoteMap(argv[1], argc == 4 ? argv[2] : nullptr, argc == 4 ? argv[3] : nullptr);
    std::printf("NEON MIDI: %u checks passed\n", checks);
    return 0;
}
