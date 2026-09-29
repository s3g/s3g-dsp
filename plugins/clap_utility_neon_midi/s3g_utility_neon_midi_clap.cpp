#include <clap/clap.h>
#include <clap/ext/gui.h>
#include <clap/ext/note-ports.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>

#include "s3g_neon_midi.h"
#include "../common/s3g_neon_usb_input.h"
#include "../common/s3g_clap_gui_param_queue.h"
#include "../common/s3g_clap_state_stream.h"
#if defined(S3G_ENABLE_VSTGUI_CANVAS_GUI)
#include "../common/s3g_vstgui_canvas.h"
#include "../common/s3g_neon_note_map_editor.h"
#include "../common/s3g_clap_vstgui.h"
#if defined(__APPLE__)
#include "../common/s3g_vstgui_readable_text_edit.h"
#endif
#endif

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

namespace {
namespace neon = s3g::controller::reloop_neon;
constexpr uint32_t kGuiWidth = 620u, kGuiHeight = 444u;
constexpr clap_id kBank = 1u, kBase = 2u, kChannel = 3u, kRoute = 4u, kPanic = 5u;
constexpr clap_id kInput = 6u, kBank2 = 7u, kUnit = 8u;
constexpr const char* kId = "org.s3g.s3g-dsp.utility-neon-midi";
constexpr const char* kName = "s3g Utility Neon MIDI";
constexpr const char* features[] = {CLAP_PLUGIN_FEATURE_NOTE_EFFECT, CLAP_PLUGIN_FEATURE_UTILITY, nullptr};
const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT, kId, kName, "s3g", "https://github.com/s3g/s3g-dsp", "", "", "0.3.1",
    "Bank-aware Reloop NEON performance notes for Tracker and MIDI instruments.", features,
};

struct ParamDef { const char* name; double min, max, initial; };
constexpr std::array<ParamDef, 8u> defs {{
    {"Bank", 0., 3., 0.}, {"Base Note", 0., 96., 36.},
    {"Output Channel", 1., 16., 1.}, {"Control Route", 0., 1., 1.},
    {"Release Held Notes", 0., 1., 0.},
    {"Input", 0., 3., 0.}, {"Unit 2 Bank", 0., 3., 1.}, {"Monitor Unit", 0., 1., 0.},
}};
struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    const clap_host_params_t* hostParams = nullptr;
    const clap_host_state_t* hostState = nullptr;
    std::array<std::atomic<double>, 8u> values {{0., 36., 1., 1., 0., 0., 1., 0.}};
    std::atomic<bool> panicRequested {false}, notifyRequested {false};
    s3g::clap_gui::ParamEventQueue<256u> guiParamEvents;
    std::array<s3g::controller::neon_midi::Mapper, 2> mapper;
    s3g::controller::neon_midi::UsbInput usb;
    std::array<std::atomic<uint32_t>, 2> heldCells {};
    std::atomic<uint32_t> rejected {0u};
    // Packed velocity / note / live input aftertouch, one byte each. Keep the
    // displayed strike stable while pressure changes. The cell/channel below
    // are process-owned and associate pressure/releases with that strike.
    std::array<std::atomic<uint32_t>, 2> lastHit {{UINT32_MAX, UINT32_MAX}};
    std::array<uint8_t, 2> lastHitCell {{255u, 255u}}, lastHitChannel {};
    std::array<std::atomic<uint8_t>, 2> page {};
    unsigned unit = 0, inputMode = 0;
    std::array<uint32_t, 2> usbGeneration {};
    std::array<int32_t, 2> destination {};
    std::array<bool, 2> disconnectPending {};
    double sampleRate = 48000.;
    bool wasPlaying = false;
    std::array<bool, 2> bridgeDirty {{true, true}};
    std::array<std::array<uint8_t, 4u>, 2> bridgeSettings {{{255u, 255u, 255u, 255u}, {255u, 255u, 255u, 255u}}};
    std::array<s3g::controller::neon_midi::AddressedBridgePacket, 8192u> bridgePackets {};
    uint32_t bridgeCount = 0u;
    s3g::controller::neon_midi::PublishedNoteMap noteMap;
    s3g::controller::neon_midi::NoteMap audioMap;
    s3g::controller::neon_midi::PadNotes mappedNotes = s3g::controller::neon_midi::sequentialNotes();
    std::array<s3g::controller::neon_midi::NoteMapPacket, 4096> mapPackets {};
    uint32_t mapPacketCount = 0;
    bool mapDirty = true, mapSent = false;
#if defined(S3G_ENABLE_VSTGUI_CANVAS_GUI)
    s3g::portable_gui::foundation::EditorHost* portableGuiEditor = nullptr;
    uint32_t portableGuiWidth = kGuiWidth, portableGuiHeight = kGuiHeight;
    bool portableGuiVisible = false;
#endif
};
Plugin* self(const clap_plugin_t* plugin) { return static_cast<Plugin*>(plugin->plugin_data); }
#if defined(S3G_ENABLE_VSTGUI_CANVAS_GUI)
void destroyPortableGui(Plugin&);
#endif

void notify(Plugin& p) {
    if (!p.notifyRequested.exchange(true) && p.host && p.host->request_callback)
        p.host->request_callback(p.host);
}
bool setValue(Plugin& p, clap_id id, double value) {
    if (id < kBank || id > kUnit || !std::isfinite(value)) return false;
    const auto& def = defs[id - 1u];
    value = std::round(std::clamp(value, def.min, def.max));
    if (id == kPanic) {
        if (value > .5) p.panicRequested.store(true);
    } else p.values[id - 1u].store(value);
    return true;
}
double getValue(const Plugin& p, clap_id id) {
    return id >= kBank && id <= kUnit ? p.values[id - 1u].load() : 0.;
}
void clearAftertouch(Plugin& p) {
    const auto last = p.lastHit[p.unit].load();
    if (last != UINT32_MAX) p.lastHit[p.unit].store(last & 0xffffu);
    p.lastHitCell[p.unit] = 255u;
}
void syncMapping(Plugin& p) {
    const bool panic = p.panicRequested.exchange(false);
    p.noteMap.read(p.audioMap);
    const auto notes = p.audioMap.resolved(static_cast<unsigned>(getValue(p, kBase)));
    if (notes != p.mappedNotes) { p.mappedNotes = notes; p.mapDirty = true; p.mapSent = false; }
    const auto previous = p.unit;
    for (p.unit = 0; p.unit < 2; ++p.unit) {
        auto& mapper = p.mapper[p.unit];
        mapper.setBank(static_cast<uint8_t>(getValue(p, p.unit ? kBank2 : kBank)));
        mapper.setBaseNote(static_cast<uint8_t>(getValue(p, kBase)));
        mapper.setNotes(p.mappedNotes);
        mapper.setChannel(static_cast<uint8_t>(getValue(p, kChannel) - 1.));
        if (panic) { mapper.panic(); clearAftertouch(p); if (p.inputMode) p.disconnectPending[p.unit] = true; }
        const std::array<uint8_t, 4u> settings {{mapper.bank(), mapper.baseNote(),
            mapper.channel(), static_cast<uint8_t>(getValue(p, kRoute))}};
        if (settings != p.bridgeSettings[p.unit]) {
            p.bridgeSettings[p.unit] = settings; p.bridgeDirty[p.unit] = true; p.mapDirty = true; p.mapSent = false;
        }
    }
    p.unit = previous;
}

bool pushNoteMap(Plugin& p, const clap_output_events_t* out, uint32_t time) {
    if (getValue(p, kRoute) < .5) return true;
    if (p.mapSent) return true;
    if (p.mapPacketCount >= p.mapPackets.size()) { p.rejected.fetch_add(1); p.mapDirty = true; return false; }
    auto& packet = p.mapPackets[p.mapPacketCount++];
    packet = s3g::controller::neon_midi::encodeNoteMap(p.mappedNotes);
    clap_event_midi_sysex_t event {};
    event.header = {sizeof(event), time, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_MIDI_SYSEX, CLAP_EVENT_IS_LIVE};
    event.port_index = 0; event.buffer = packet.data(); event.size = static_cast<uint32_t>(packet.size());
    const bool accepted = out && out->try_push && out->try_push(out, &event.header);
    p.mapDirty = !accepted; p.mapSent = accepted;
    if (!accepted) p.rejected.fetch_add(1);
    return accepted;
}

bool pushBridge(Plugin& p, const clap_output_events_t* out, uint32_t time,
    s3g::controller::neon_midi::BridgeKind kind,
    neon::MidiMessage message = {}, uint8_t cell = 0u) {
    if (getValue(p, kRoute) < .5) return true;
    if (p.bridgeCount >= p.bridgePackets.size()) { p.rejected.fetch_add(1u); return false; }
    auto& packet = p.bridgePackets[p.bridgeCount++];
    const auto& mapper = p.mapper[p.unit];
    if (p.inputMode && kind == s3g::controller::neon_midi::BridgeKind::Sync)
        message = {0u, static_cast<uint8_t>(mapper.mode()), static_cast<uint8_t>(mapper.layer())};
    const s3g::controller::neon_midi::BridgeMessage bridge {kind, mapper.bank(),
        mapper.baseNote(), mapper.channel(), message, cell, p.inputMode != 0, static_cast<uint8_t>(p.unit), p.destination[p.unit]};
    uint32_t size = 21u;
    if (p.inputMode || kind == s3g::controller::neon_midi::BridgeKind::Disconnect)
        packet = s3g::controller::neon_midi::encodeAddressedBridge(bridge);
    else { const auto legacy = s3g::controller::neon_midi::encodeBridge(bridge); std::copy(legacy.begin(), legacy.end(), packet.begin()); size = 15u; }
    clap_event_midi_sysex_t event {};
    event.header = {sizeof(event), time, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_MIDI_SYSEX, CLAP_EVENT_IS_LIVE};
    event.port_index = 0u; event.buffer = packet.data(); event.size = size;
    const bool accepted = out && out->try_push && out->try_push(out, &event.header);
    if (!accepted) p.rejected.fetch_add(1u);
    return accepted;
}
void syncBridge(Plugin& p, const clap_output_events_t* out, uint32_t time) {
    if (p.disconnectPending[p.unit]) {
        if (!pushBridge(p, out, time, s3g::controller::neon_midi::BridgeKind::Disconnect)) return;
        p.disconnectPending[p.unit] = false; p.bridgeDirty[p.unit] = true;
    }
    if (p.unit && p.inputMode != 1 && p.inputMode != 3) return;
    if (p.bridgeDirty[p.unit] && pushBridge(p, out, time, s3g::controller::neon_midi::BridgeKind::Sync))
        p.bridgeDirty[p.unit] = false;
}
void serviceGui(Plugin& p, const clap_output_events_t* out) {
    s3g::clap_gui::serviceParamEvents(p.guiParamEvents, out,
        [&p](clap_id id, double value) { if (setValue(p, id, value)) notify(p); });
}
void applyEvent(Plugin& p, const clap_event_header_t* header) {
    if (!header || header->space_id != CLAP_CORE_EVENT_SPACE_ID
        || header->type != CLAP_EVENT_PARAM_VALUE
        || header->size < sizeof(clap_event_param_value_t)) return;
    const auto* event = reinterpret_cast<const clap_event_param_value_t*>(header);
    (void)setValue(p, event->param_id, event->value);
}
void flushParams(const clap_plugin_t* plugin, const clap_input_events_t* in,
    const clap_output_events_t* out) {
    auto& p = *self(plugin);
    serviceGui(p, out);
    if (in && in->size && in->get)
        for (uint32_t i = 0u, n = in->size(in); i < n; ++i) applyEvent(p, in->get(in, i));
    if (p.panicRequested.load() && p.host && p.host->request_process)
        p.host->request_process(p.host);
}

clap_process_status process(const clap_plugin_t* plugin, const clap_process_t* block) {
    if (!block) return CLAP_PROCESS_ERROR;
    auto& p = *self(plugin);
    const auto* out = block->out_events;
    p.bridgeCount = 0u;
    p.mapPacketCount = 0; p.mapSent = false;
    auto sink = [&p, out](uint32_t time, neon::MidiMessage message) {
        const bool release = (message.status & 0xf0u) == 0x80u;
        // A new musical note must not outrun its map through Tracker. Releases
        // are never blocked by map backpressure and keep their latched key.
        if (!release && !pushNoteMap(p, out, time)) return false;
        // Two fingers on the same mapped key share its MIDI gate: either may
        // retrigger it, but only the final release closes it.
        if (release && p.mapper[1u-p.unit].holds(message.data1, message.status & 0x0fu)) {
            if (((p.lastHit[p.unit].load() >> 8u) & 0x7fu) == message.data1
                && p.lastHitChannel[p.unit] == (message.status & 0x0fu)) clearAftertouch(p);
            return true;
        }
        clap_event_midi_t event {};
        event.header = {sizeof(event), time, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_MIDI, CLAP_EVENT_IS_LIVE};
        event.port_index = 0u;
        event.data[0] = message.status; event.data[1] = message.data1; event.data[2] = message.data2;
        const bool accepted = out && out->try_push && out->try_push(out, &event.header);
        if (!accepted) p.rejected.fetch_add(1u);
        else if ((message.status & 0xf0u) == 0x90u) {
            p.lastHit[p.unit].store((static_cast<uint32_t>(message.data1) << 8u) | message.data2);
            p.lastHitChannel[p.unit] = message.status & 0x0fu;
            p.lastHitCell[p.unit] = 255u;
        } else if ((message.status & 0xf0u) == 0x80u
            && ((p.lastHit[p.unit].load() >> 8u) & 0x7fu) == message.data1
            && p.lastHitChannel[p.unit] == (message.status & 0x0fu)) {
            clearAftertouch(p);
        }
        return accepted;
    };
    serviceGui(p, out);
    syncMapping(p);
    const auto inputMode = static_cast<unsigned>(getValue(p, kInput));
    if (inputMode != p.inputMode) {
        for (p.unit = 0; p.unit < 2; ++p.unit) {
            p.mapper[p.unit].panic(); clearAftertouch(p);
            p.disconnectPending[p.unit] = true;
            p.destination[p.unit] = 0;
            p.bridgeDirty[p.unit] = true;
        }
        p.inputMode = inputMode;
    }
    p.usb.wanted.store(inputMode >= 2 ? inputMode - 1u : 0u);
    if (block->transport) {
        const bool playing = (block->transport->flags & CLAP_TRANSPORT_IS_PLAYING) != 0u;
        if (p.wasPlaying && !playing) for (p.unit = 0; p.unit < 2; ++p.unit) {
            p.mapper[p.unit].panic(); clearAftertouch(p);
            if (p.inputMode) p.disconnectPending[p.unit] = true;
        }
        p.wasPlaying = playing;
    }
    for (p.unit = 0; p.unit < 2; ++p.unit) {
        if (inputMode >= 2 && p.usbGeneration[p.unit] != p.usb.generation[p.unit].load()) {
            p.usbGeneration[p.unit] = p.usb.generation[p.unit].load();
            p.mapper[p.unit].panic(); clearAftertouch(p);
            p.disconnectPending[p.unit] = true;
            p.destination[p.unit] = p.usb.connected[p.unit].load() ? p.usb.destination[p.unit].load() : 0;
            notify(p);
        }
        p.mapper[p.unit].flush(0u, sink);
        if (!p.unit || inputMode == 1 || inputMode == 3 || p.disconnectPending[p.unit]) syncBridge(p, out, 0u);
    }
    if (p.mapDirty) pushNoteMap(p, out, 0u);
    auto rawInput = [&](unsigned unit, neon::MidiMessage raw, uint32_t time) {
        p.unit = unit;
        if (p.disconnectPending[unit]) return; // Retry critical release before accepting new gestures.
        auto& mapper = p.mapper[unit];
        const auto oldBank = mapper.bank();
        const auto result = mapper.process(raw, time, sink);
        using s3g::controller::neon_midi::BridgeKind;
        if (result.bridge && result.kind == BridgeKind::SelectCell) p.lastHitCell[unit] = result.cell;
        else if (result.bridge && result.kind == BridgeKind::Pressure && result.cell == p.lastHitCell[unit])
            p.lastHit[unit].store((p.lastHit[unit].load() & 0xffffu) | (static_cast<uint32_t>(raw.data2) << 16u));
        if (result.bridge && !pushBridge(p, out, time, result.kind, raw, result.cell)) {
            mapper.panic(); p.disconnectPending[unit] = p.inputMode != 0;
        }
        if (mapper.bank() != oldBank) { p.values[unit ? kBank2-1 : kBank-1].store(mapper.bank()); notify(p); }
    };
    const auto* in = block->in_events;
    const uint32_t n = in && in->size && in->get ? in->size(in) : 0;
    uint32_t i = 0, previousTime = 0;
    s3g::controller::neon_midi::UsbInput::Event usbEvent;
    const auto now = p.usb.clock();
    const double framesPerTick = p.sampleRate / p.usb.ticksPerSecond();
    auto hostEvent = [&](const clap_event_header_t* header) {
            if (!header || header->space_id != CLAP_CORE_EVENT_SPACE_ID || header->time >= block->frames_count) return;
            if (header->type == CLAP_EVENT_PARAM_VALUE) {
                applyEvent(p, header);
                syncMapping(p);
                for (p.unit = 0; p.unit < 2; ++p.unit) {
                    if (!p.unit || inputMode == 1 || inputMode == 3) syncBridge(p, out, header->time);
                    p.mapper[p.unit].flush(header->time, sink);
                }
                if (p.mapDirty) pushNoteMap(p, out, header->time);
            } else if (header->type == CLAP_EVENT_MIDI && header->size >= sizeof(clap_event_midi_t)) {
                const auto* event = reinterpret_cast<const clap_event_midi_t*>(header);
                if (inputMode < 2 && event->port_index < (inputMode == 1 ? 2u : 1u))
                    rawInput(event->port_index, {event->data[0], event->data[1], event->data[2]}, header->time);
            }
    };
    // Merge timestamped USB input and sample-offset host automation in order.
    for (unsigned packets = 0; packets < 4096 && p.usb.pop(usbEvent); ++packets) {
        if (inputMode < 2 || usbEvent.unit >= inputMode-1u
            || usbEvent.generation != p.usbGeneration[usbEvent.unit]) continue;
        // The hardware can restore an old deck page in the SAME bank-button
        // packet. Keep the current page; Sample Neon reasserts it to this unit.
        if (usbEvent.bankPageRestore) continue;
        const double age = usbEvent.timestamp < now ? double(now-usbEvent.timestamp) * framesPerTick : 0.;
        const auto time = std::max(previousTime, static_cast<uint32_t>(std::clamp(double(block->frames_count)-age, 0., double(block->frames_count ? block->frames_count-1u : 0u))));
        while (i < n) {
            const auto* header = in->get(in, i);
            if (header && header->time > time) break;
            hostEvent(header); ++i;
        }
        rawInput(usbEvent.unit, usbEvent.midi, time); previousTime = time;
    }
    while (i < n) hostEvent(in->get(in, i++));
    bool awake = inputMode >= 2 || getValue(p, kInput) != inputMode || (p.mapDirty && getValue(p,kRoute) >= .5);
    for (p.unit = 0; p.unit < 2; ++p.unit) {
        auto& mapper = p.mapper[p.unit]; mapper.advance(block->frames_count);
        p.heldCells[p.unit].store(mapper.heldCells());
        p.page[p.unit].store(static_cast<uint8_t>(static_cast<uint8_t>(mapper.mode()) + (mapper.layer() == neon::Layer::Second ? 4u : 0u)));
        awake |= mapper.heldCells() || mapper.pending() || p.disconnectPending[p.unit];
    }
    p.unit = 0;
    return awake ? CLAP_PROCESS_CONTINUE : CLAP_PROCESS_SLEEP;
}

uint32_t paramCount(const clap_plugin_t*) { return static_cast<uint32_t>(defs.size()); }
bool paramInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info) {
    if (!info || index >= defs.size()) return false;
    *info = {};
    info->id = index + 1u;
    info->flags = CLAP_PARAM_IS_STEPPED;
    if (index < 4u || index == kBank2-1) info->flags |= CLAP_PARAM_IS_AUTOMATABLE;
    std::snprintf(info->name, sizeof(info->name), "%s", defs[index].name);
    std::snprintf(info->module, sizeof(info->module), "NEON MIDI");
    info->min_value = defs[index].min; info->max_value = defs[index].max;
    info->default_value = defs[index].initial;
    return true;
}
bool paramValue(const clap_plugin_t* plugin, clap_id id, double* value) {
    if (!value || id < kBank || id > kUnit) return false;
    *value = getValue(*self(plugin), id); return true;
}
bool valueText(const clap_plugin_t*, clap_id id, double value, char* text, uint32_t capacity) {
    if (!text || !capacity || id < kBank || id > kUnit || !std::isfinite(value)) return false;
    const int v = static_cast<int>(std::round(std::clamp(value, defs[id - 1u].min, defs[id - 1u].max)));
    if (id == kBank || id == kBank2) std::snprintf(text, capacity, "%c", 'A' + v);
    else if (id == kInput) { const char* modes[] = {"Host MIDI", "Host Dual Ports", "USB One NEON", "USB Two NEONs"}; std::snprintf(text, capacity, "%s", modes[v]); }
    else if (id == kUnit) std::snprintf(text, capacity, "Unit %d", v+1);
    else if (id == kRoute) std::snprintf(text, capacity, "%s", v ? "Tracker + Sample Neon" : "Notes Only");
    else if (id == kPanic) std::snprintf(text, capacity, "%s", v ? "Release" : "Ready");
    else std::snprintf(text, capacity, "%d", v);
    return true;
}
bool textValue(const clap_plugin_t*, clap_id id, const char* text, double* value) {
    if (!text || !value || id < kBank || id > kUnit) return false;
    if ((id == kBank || id == kBank2) && text[0] && !text[1]
        && ((text[0] >= 'A' && text[0] <= 'D') || (text[0] >= 'a' && text[0] <= 'd'))) {
        *value = (text[0] >= 'a' ? text[0] - 'a' : text[0] - 'A'); return true;
    }
    if (id == kPanic && (!std::strcmp(text, "Release") || !std::strcmp(text, "Ready"))) {
        *value = !std::strcmp(text, "Release") ? 1. : 0.; return true;
    }
    if (id == kRoute && (!std::strcmp(text, "Tracker + Sample Neon") || !std::strcmp(text, "Notes Only"))) {
        *value = !std::strcmp(text, "Tracker + Sample Neon") ? 1. : 0.; return true;
    }
    if (id == kInput || id == kUnit) for (int n = 0; n <= defs[id-1].max; ++n) {
        char formatted[64] {}; valueText(nullptr, id, n, formatted, sizeof(formatted));
        if (!std::strcmp(text, formatted)) { *value = n; return true; }
    }
    char* end = nullptr;
    const double parsed = std::strtod(text, &end);
    if (end == text || *end || !std::isfinite(parsed)
        || parsed < defs[id - 1u].min || parsed > defs[id - 1u].max) return false;
    *value = std::round(parsed); return true;
}
const clap_plugin_params_t params {paramCount, paramInfo, paramValue, valueText, textValue, flushParams};

uint32_t portCount(const clap_plugin_t*, bool input) { return input ? 2u : 1u; }
bool portInfo(const clap_plugin_t*, uint32_t index, bool input, clap_note_port_info_t* info) {
    if (!info || index >= (input ? 2u : 1u)) return false;
    *info = {}; info->id = input ? index : 1u;
    info->supported_dialects = info->preferred_dialect = CLAP_NOTE_DIALECT_MIDI;
    std::snprintf(info->name, sizeof(info->name), "%s", input ? index ? "NEON Unit 2 In" : "NEON Hardware In" : "Notes + Controls Out");
    return true;
}
const clap_plugin_note_ports_t ports {portCount, portInfo};

bool saveState(const clap_plugin_t* plugin, const clap_ostream_t* stream) {
    const auto& p = *self(plugin);
    s3g::controller::neon_midi::NoteMap map;
    if (!p.noteMap.read(map)) return false;
    // Fixed byte layout: magic, version, bank, base, channel. No struct padding.
    std::array<uint8_t, 20u> data {{'N', 'M', 'I', 'D', 2u,
        static_cast<uint8_t>(getValue(p, kBank)), static_cast<uint8_t>(getValue(p, kBase)),
        static_cast<uint8_t>(getValue(p, kChannel)), static_cast<uint8_t>(getValue(p, kRoute)),
        static_cast<uint8_t>(getValue(p, kInput)), static_cast<uint8_t>(getValue(p, kBank2)), static_cast<uint8_t>(getValue(p, kUnit))}};
    for (unsigned u = 0; u < 2; ++u) for (unsigned n = 0; n < 4; ++n)
        data[12u+u*4u+n] = static_cast<uint8_t>(static_cast<uint32_t>(p.usb.source[u].load()) >> (8u*n));
    if (map.custom) data[4] = 3;
    return s3g::clap_state::writeAll(stream, data.data(), data.size())
        && (!map.custom || s3g::clap_state::writeAll(stream, map.notes.data(), map.notes.size()));
}
bool loadState(const clap_plugin_t* plugin, const clap_istream_t* stream) {
    std::array<uint8_t, 20u> data {};
    if (!s3g::clap_state::readAll(stream, data.data(), 9u)
        || std::memcmp(data.data(), "NMID", 4u) || (data[4] != 1u && data[4] != 2u && data[4] != 3u)
        || data[5] > 3u || data[6] > 96u || data[7] < 1u || data[7] > 16u || data[8] > 1u) return false;
    if (data[4] >= 2u && (!s3g::clap_state::readAll(stream, data.data()+9u, 11u)
        || data[9] > 3u || data[10] > 3u || data[11] > 1u)) return false;
    if (data[4] == 1u) data[10] = 1u;
    std::array<int32_t, 2> sources {};
    for (unsigned u = 0; u < 2; ++u) {
        uint32_t bits = 0; for (unsigned n = 0; n < 4; ++n) bits |= uint32_t(data[12u+u*4u+n]) << (8u*n);
        sources[u] = static_cast<int32_t>(bits);
    }
    if (sources[0] && sources[0] == sources[1]) return false;
    s3g::controller::neon_midi::NoteMap map;
    map.custom = data[4] == 3;
    if (map.custom && (!s3g::clap_state::readAll(stream,map.notes.data(),map.notes.size())
        || !s3g::controller::neon_midi::validNotes(map.notes))) return false;
    auto& p = *self(plugin);
    p.noteMap.store(map);
    for (uint32_t i = 0u; i < 4u; ++i) p.values[i].store(data[5u + i]);
    p.values[kInput-1].store(data[9]); p.values[kBank2-1].store(data[10]); p.values[kUnit-1].store(data[11]);
    for (unsigned u = 0; u < 2; ++u) p.usb.source[u].store(sources[u]);
    p.panicRequested.store(true); // Restore settings, never restore held fingers.
    notify(p);
    if (p.host && p.host->request_process) p.host->request_process(p.host);
    return true;
}
const clap_plugin_state_t state {saveState, loadState};

#if defined(S3G_ENABLE_VSTGUI_CANVAS_GUI)
#include "s3g_utility_neon_midi_vstgui.inc"
#include "../common/s3g_clap_canvas_gui.inc"
#endif

bool init(const clap_plugin_t* plugin) {
    auto& p = *self(plugin);
    if (p.host && p.host->get_extension) {
        p.hostParams = static_cast<const clap_host_params_t*>(p.host->get_extension(p.host, CLAP_EXT_PARAMS));
        p.hostState = static_cast<const clap_host_state_t*>(p.host->get_extension(p.host, CLAP_EXT_STATE));
    }
    p.usb.start();
    return true;
}
void destroy(const clap_plugin_t* plugin) {
    self(plugin)->usb.stop();
#if defined(S3G_ENABLE_VSTGUI_CANVAS_GUI)
    destroyPortableGui(*self(plugin));
#endif
    delete self(plugin);
}
bool activate(const clap_plugin_t* plugin, double rate, uint32_t minimum, uint32_t maximum) {
    if (!std::isfinite(rate) || rate <= 0. || minimum > maximum || !maximum) return false;
    auto& p = *self(plugin);
    p.wasPlaying = false; p.sampleRate = rate;
    p.mapDirty = true; p.mapSent = false;
    for (auto& mapper : p.mapper) mapper.prepare(rate);
    syncMapping(p); return true;
}
void deactivate(const clap_plugin_t* plugin) {
    auto& p = *self(plugin); p.usb.wanted.store(0); p.panicRequested.store(true);
    for (p.unit = 0; p.unit < 2; ++p.unit) { clearAftertouch(p); p.heldCells[p.unit].store(0); }
    p.unit = 0;
}
bool start(const clap_plugin_t*) { return true; }
void stop(const clap_plugin_t* plugin) {
    auto& p = *self(plugin); p.usb.wanted.store(0); p.panicRequested.store(true);
    for (auto& generation : p.usb.generation) generation.fetch_add(1);
}
void reset(const clap_plugin_t* plugin) {
    auto& p = *self(plugin);
    p.mapDirty = true; p.mapSent = false;
    for (p.unit = 0; p.unit < 2; ++p.unit) { p.mapper[p.unit].panic(); clearAftertouch(p); if (p.inputMode) p.disconnectPending[p.unit] = true; }
    p.unit = 0; p.wasPlaying = false;
}
void onMainThread(const clap_plugin_t* plugin) {
    auto& p = *self(plugin);
    if (!p.notifyRequested.exchange(false)) return;
    if (p.hostParams && p.hostParams->rescan) p.hostParams->rescan(p.host, CLAP_PARAM_RESCAN_VALUES);
    if (p.hostState && p.hostState->mark_dirty) p.hostState->mark_dirty(p.host);
}
const void* extension(const clap_plugin_t*, const char* id) {
    if (!id) return nullptr;
    if (!std::strcmp(id, CLAP_EXT_PARAMS)) return &params;
    if (!std::strcmp(id, CLAP_EXT_NOTE_PORTS)) return &ports;
    if (!std::strcmp(id, CLAP_EXT_STATE)) return &state;
#if defined(S3G_ENABLE_VSTGUI_CANVAS_GUI)
    if (!std::strcmp(id, CLAP_EXT_GUI)) return &portableGui;
#endif
    return nullptr;
}
const clap_plugin_t* create(const clap_plugin_factory_t*, const clap_host_t* host, const char* id) {
    if (!host || !clap_version_is_compatible(host->clap_version) || !id || std::strcmp(id, kId)) return nullptr;
    auto* p = new (std::nothrow) Plugin;
    if (!p) return nullptr;
    p->host = host;
    p->plugin = {&descriptor, p, init, destroy, activate, deactivate, start, stop, reset, process, extension, onMainThread};
    return &p->plugin;
}
uint32_t count(const clap_plugin_factory_t*) { return 1u; }
const clap_plugin_descriptor_t* describe(const clap_plugin_factory_t*, uint32_t i) { return i ? nullptr : &descriptor; }
const clap_plugin_factory_t factory {count, describe, create};
bool entryInit(const char*) { return true; }
void entryDeinit() {}
const void* getFactory(const char* id) { return id && !std::strcmp(id, CLAP_PLUGIN_FACTORY_ID) ? &factory : nullptr; }
} // namespace
extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry {CLAP_VERSION_INIT, entryInit, entryDeinit, getFactory};
