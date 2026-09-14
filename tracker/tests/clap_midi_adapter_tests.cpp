#include "s3g_tracker_clap_adapter.h"
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>

namespace {
namespace a = s3g::tracker::clap_adapter;
namespace m = s3g::tracker::midi;
int checks = 0, failures = 0;
void check(bool ok, const char* message)
{
    ++checks;
    if (!ok) { ++failures; std::cerr << message << '\n'; }
}
struct Host {
    clap_host_t host {};
    clap_host_state_t state {};
    unsigned requests = 0, callbacks = 0, dirty = 0, sequence = 0;
    Host()
    {
        host.clap_version = CLAP_VERSION;
        host.host_data = this;
        host.request_process = request;
        host.request_callback = callback;
        host.get_extension = extension;
        state.mark_dirty = mark;
    }
    static Host& self(const clap_host_t* h) { return *static_cast<Host*>(h->host_data); }
    static void CLAP_ABI request(const clap_host_t* h) { ++self(h).requests; self(h).sequence = 1; }
    static void CLAP_ABI callback(const clap_host_t* h) { ++self(h).callbacks; }
    static void CLAP_ABI mark(const clap_host_t* h) {
        ++self(h).dirty; self(h).sequence = self(h).sequence == 1 ? 2 : 99;
    }
    static const void* CLAP_ABI extension(const clap_host_t* h, const char* id) {
        return std::strcmp(id, CLAP_EXT_STATE) == 0 ? &self(h).state : nullptr;
    }
};
struct Events {
    std::array<const clap_event_header_t*, 8> input {};
    uint32_t count = 0;
    clap_input_events_t in {this, size, get};
    std::array<clap_event_midi_t, 64> output {};
    uint32_t written = 0;
    bool accept = true, validHeaders = true;
    clap_output_events_t out {this, push};
    static uint32_t CLAP_ABI size(const clap_input_events_t* list) {
        return static_cast<const Events*>(list->ctx)->count;
    }
    static const clap_event_header_t* CLAP_ABI get(const clap_input_events_t* list, uint32_t i) {
        const auto& e = *static_cast<const Events*>(list->ctx);
        return i < e.count ? e.input[i] : nullptr;
    }
    static bool CLAP_ABI push(const clap_output_events_t* list, const clap_event_header_t* event) {
        auto& e = *static_cast<Events*>(list->ctx);
        e.validHeaders = e.validHeaders && event->size == sizeof(clap_event_midi_t)
            && event->space_id == CLAP_CORE_EVENT_SPACE_ID
            && event->type == CLAP_EVENT_MIDI && event->flags == 0;
        if (!e.validHeaders || !e.accept || e.written == e.output.size()) return false;
        e.output[e.written++] = *reinterpret_cast<const clap_event_midi_t*>(event);
        return true;
    }
};
}

int main()
{
    clap_event_transport_t transport {};
    check(!a::readHostTransport(nullptr).playing
        && a::readHostTransport(nullptr).tempo == 120, "absent transport defaults");
    transport.flags = CLAP_TRANSPORT_IS_PLAYING | CLAP_TRANSPORT_HAS_TEMPO
        | CLAP_TRANSPORT_HAS_BEATS_TIMELINE;
    transport.tempo = 97.5;
    transport.tempo_inc = .001;
    transport.song_pos_beats = -2 * CLAP_BEATTIME_FACTOR;
    auto t = a::readHostTransport(&transport);
    check(t.playing && t.hasTempo && t.hasBeat && t.beat == -2
        && t.tempo == 97.5 && t.tempoIncrement == .001, "beat/tempo/tempo-ramp decode");
    transport.flags = CLAP_TRANSPORT_HAS_TEMPO | CLAP_TRANSPORT_HAS_SECONDS_TIMELINE;
    transport.tempo = 90;
    transport.song_pos_seconds = 2 * CLAP_SECTIME_FACTOR;
    check(a::readHostTransport(&transport).beat == 3, "seconds timeline fallback");
    transport.tempo = std::numeric_limits<double>::quiet_NaN();
    transport.tempo_inc = std::numeric_limits<double>::infinity();
    t = a::readHostTransport(&transport);
    check(!t.hasTempo && t.tempo == 120 && t.tempoIncrement == 0 && t.beat == 4,
        "invalid host tempo fallback");

    auto engine = std::make_unique<m::Engine>();
    check(m::initialize(*engine) && m::activate(*engine, 48000), "initialize adapter engine");
    Host host;
    engine->services = a::hostServices(&host.host);
    m::markHostStateDirty(*engine);
    check(host.requests == 1 && host.dirty == 1 && host.sequence == 2,
        "CLAP request-process then state mark_dirty");
    m::publishDocument(*engine, engine->document, false);
    Events events;
    clap_process_t data {};
    data.frames_count = 256;
    data.in_events = &events.in;
    data.out_events = &events.out;
    check(a::process(*engine, nullptr) == CLAP_PROCESS_ERROR, "null process data");
    check(a::process(*engine, &data) == CLAP_PROCESS_CONTINUE
        && host.callbacks == 1, "CLAP runtime handoff callback");
    m::drainRetiredRuntimes(*engine);

    engine->midiStepRecordMode = uint8_t(s3g::tracker::MidiStepRecordMode::Step);
    engine->midiMonitorChannel = 9;
    clap_event_midi_t note {};
    note.header = {sizeof(note), 20, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_MIDI, 0};
    note.data[0] = 0x91; note.data[1] = 70; note.data[2] = 100;
    auto off = note;
    off.header.time = 80; off.data[2] = 0;
    auto shortEvent = note;
    shortEvent.header.size = sizeof(clap_event_header_t);
    auto foreign = note;
    foreign.header.space_id = 42;
    auto wrongPort = note;
    wrongPort.port_index = 1;
    events.input = {nullptr, &shortEvent.header, &foreign.header,
        &wrongPort.header, &note.header, &off.header};
    events.count = 6;
    check(a::process(*engine, &data) == CLAP_PROCESS_CONTINUE, "CLAP MIDI input processing");
    check(events.written == 2 && events.validHeaders
        && events.output[0].header.time == 20
        && events.output[0].port_index == 0
        && events.output[0].data[0] == 0x99 && events.output[0].data[1] == 70
        && events.output[1].header.time == 80 && events.output[1].data[0] == 0x89,
        "MIDI byte/offset/channel conversion and malformed/foreign/other-port filtering");
    events.accept = false;
    events.input[0] = &note.header;
    events.count = 1;
    a::process(*engine, &data);
    check(engine->droppedEvents == 1, "CLAP output rejection accounting");
    events.count = 0;
    engine->requestMidiMonitorRelease = true;
    data.out_events = nullptr;
    a::process(*engine, &data);
    check(engine->droppedEvents == 2, "null CLAP output accounting");

    // A transport event splits one host buffer, not an entire UI frame.
    m::reset(*engine);
    events.accept = true;
    events.written = 0;
    data.out_events = &events.out;
    transport.flags = CLAP_TRANSPORT_IS_PLAYING | CLAP_TRANSPORT_HAS_TEMPO
        | CLAP_TRANSPORT_HAS_BEATS_TIMELINE;
    transport.tempo = 120; transport.tempo_inc = 0; transport.song_pos_beats = 0;
    data.transport = &transport;
    auto stop = transport;
    stop.header = {sizeof(stop), 100, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_TRANSPORT, 0};
    stop.flags &= ~CLAP_TRANSPORT_IS_PLAYING;
    events.input[0] = &stop.header;
    events.count = 1;
    a::process(*engine, &data);
    check(events.written > 0 && !engine->visualPlaying, "mid-buffer CLAP transport event");
    for (uint32_t i = 0; i < events.written; ++i)
        check(events.output[i].header.time == ((events.output[i].data[0] & 0xf0) == 0x80 ? 100u : 0u),
            "transport event MIDI offset changed");

    engine->services = a::hostServices(nullptr);
    m::markHostStateDirty(*engine);
    clap_input_events_t empty {};
    data.in_events = &empty;
    data.transport = nullptr;
    check(a::process(*engine, &data) == CLAP_PROCESS_CONTINUE, "optional host hooks absent");
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
