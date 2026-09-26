#include <clap/clap.h>
#include <clap/ext/gui.h>
#include <clap/ext/note-ports.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>

#include "s3g_neon_midi.h"
#include "../common/s3g_clap_gui_param_queue.h"
#include "../common/s3g_clap_state_stream.h"
#if defined(S3G_ENABLE_VSTGUI_CANVAS_GUI)
#include "../common/s3g_vstgui_canvas.h"
#include "../common/s3g_clap_vstgui.h"
#endif

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

namespace {
namespace neon = s3g::controller::reloop_neon;
constexpr uint32_t kGuiWidth = 620u, kGuiHeight = 344u;
constexpr clap_id kBank = 1u, kBase = 2u, kChannel = 3u, kRoute = 4u, kPanic = 5u;
constexpr const char* kId = "org.s3g.s3g-dsp.utility-neon-midi";
constexpr const char* kName = "s3g Utility Neon MIDI";
constexpr const char* features[] = {CLAP_PLUGIN_FEATURE_NOTE_EFFECT, CLAP_PLUGIN_FEATURE_UTILITY, nullptr};
const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT, kId, kName, "s3g", "https://github.com/s3g/s3g-dsp", "", "", "0.1.1",
    "Bank-aware Reloop NEON performance notes for Tracker and MIDI instruments.", features,
};

struct ParamDef { const char* name; double min, max, initial; };
constexpr std::array<ParamDef, 5u> defs {{
    {"Bank", 0., 3., 0.}, {"Base Note", 0., 96., 36.},
    {"Output Channel", 1., 16., 1.}, {"Control Route", 0., 1., 1.},
    {"Release Held Notes", 0., 1., 0.},
}};
struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    const clap_host_params_t* hostParams = nullptr;
    const clap_host_state_t* hostState = nullptr;
    std::array<std::atomic<double>, 4u> values {{0., 36., 1., 1.}};
    std::atomic<bool> panicRequested {false}, notifyRequested {false};
    s3g::clap_gui::ParamEventQueue<256u> guiParamEvents;
    s3g::controller::neon_midi::Mapper mapper;
    std::atomic<uint32_t> heldCells {0u}, rejected {0u};
    // One publication keeps the note and its attack velocity consistent.
    std::atomic<uint32_t> lastHit {UINT32_MAX};
    std::atomic<uint8_t> page {0u};
    bool wasPlaying = false;
    bool bridgeDirty = true;
    std::array<uint8_t, 4u> bridgeSettings {{255u, 255u, 255u, 255u}};
    std::array<s3g::controller::neon_midi::BridgePacket, 2048u> bridgePackets {};
    uint32_t bridgeCount = 0u;
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
    if (id < kBank || id > kPanic || !std::isfinite(value)) return false;
    const auto& def = defs[id - 1u];
    value = std::round(std::clamp(value, def.min, def.max));
    if (id == kPanic) {
        if (value > .5) p.panicRequested.store(true);
    } else p.values[id - 1u].store(value);
    return true;
}
double getValue(const Plugin& p, clap_id id) {
    return id >= kBank && id <= kRoute ? p.values[id - 1u].load() : 0.;
}
void syncMapping(Plugin& p) {
    p.mapper.setBank(static_cast<uint8_t>(getValue(p, kBank)));
    p.mapper.setBaseNote(static_cast<uint8_t>(getValue(p, kBase)));
    p.mapper.setChannel(static_cast<uint8_t>(getValue(p, kChannel) - 1.));
    if (p.panicRequested.exchange(false)) p.mapper.panic();
    const std::array<uint8_t, 4u> settings {{p.mapper.bank(), p.mapper.baseNote(),
        p.mapper.channel(), static_cast<uint8_t>(getValue(p, kRoute))}};
    if (settings != p.bridgeSettings) { p.bridgeSettings = settings; p.bridgeDirty = true; }
}

bool pushBridge(Plugin& p, const clap_output_events_t* out, uint32_t time,
    s3g::controller::neon_midi::BridgeKind kind,
    neon::MidiMessage message = {}, uint8_t cell = 0u) {
    if (getValue(p, kRoute) < .5) return true;
    if (p.bridgeCount >= p.bridgePackets.size()) { p.rejected.fetch_add(1u); return false; }
    auto& packet = p.bridgePackets[p.bridgeCount++];
    packet = s3g::controller::neon_midi::encodeBridge({kind, p.mapper.bank(),
        p.mapper.baseNote(), p.mapper.channel(), message, cell});
    clap_event_midi_sysex_t event {};
    event.header = {sizeof(event), time, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_MIDI_SYSEX, CLAP_EVENT_IS_LIVE};
    event.port_index = 0u; event.buffer = packet.data(); event.size = static_cast<uint32_t>(packet.size());
    const bool accepted = out && out->try_push && out->try_push(out, &event.header);
    if (!accepted) p.rejected.fetch_add(1u);
    return accepted;
}
void syncBridge(Plugin& p, const clap_output_events_t* out, uint32_t time) {
    if (p.bridgeDirty && pushBridge(p, out, time, s3g::controller::neon_midi::BridgeKind::Sync))
        p.bridgeDirty = false;
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
    auto sink = [&p, out](uint32_t time, neon::MidiMessage message) {
        clap_event_midi_t event {};
        event.header = {sizeof(event), time, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_MIDI, CLAP_EVENT_IS_LIVE};
        event.port_index = 0u;
        event.data[0] = message.status; event.data[1] = message.data1; event.data[2] = message.data2;
        const bool accepted = out && out->try_push && out->try_push(out, &event.header);
        if (!accepted) p.rejected.fetch_add(1u);
        else if ((message.status & 0xf0u) == 0x90u)
            p.lastHit.store((static_cast<uint32_t>(message.data1) << 8u) | message.data2);
        return accepted;
    };
    serviceGui(p, out);
    syncMapping(p);
    syncBridge(p, out, 0u);
    if (block->transport) {
        const bool playing = (block->transport->flags & CLAP_TRANSPORT_IS_PLAYING) != 0u;
        if (p.wasPlaying && !playing) p.mapper.panic();
        p.wasPlaying = playing;
    }
    p.mapper.flush(0u, sink);
    const auto* in = block->in_events;
    if (in && in->size && in->get) {
        for (uint32_t i = 0u, n = in->size(in); i < n; ++i) {
            const auto* header = in->get(in, i);
            if (!header || header->space_id != CLAP_CORE_EVENT_SPACE_ID
                || header->time >= block->frames_count) continue;
            if (header->type == CLAP_EVENT_PARAM_VALUE) {
                applyEvent(p, header);
                syncMapping(p);
                syncBridge(p, out, header->time);
                p.mapper.flush(header->time, sink);
            } else if (header->type == CLAP_EVENT_MIDI && header->size >= sizeof(clap_event_midi_t)) {
                const auto* event = reinterpret_cast<const clap_event_midi_t*>(header);
                if (event->port_index != 0u) continue;
                const auto oldBank = p.mapper.bank();
                const neon::MidiMessage raw {event->data[0], event->data[1], event->data[2]};
                const auto result = p.mapper.process(raw, header->time, sink);
                if (result.bridge) (void)pushBridge(p, out, header->time, result.kind, raw, result.cell);
                if (p.mapper.bank() != oldBank) {
                    p.values[0].store(p.mapper.bank());
                    notify(p);
                }
            }
        }
    }
    p.mapper.advance(block->frames_count);
    p.heldCells.store(p.mapper.heldCells());
    p.page.store(static_cast<uint8_t>(static_cast<uint8_t>(p.mapper.mode())
        + (p.mapper.layer() == neon::Layer::Second ? 4u : 0u)));
    // Also stay awake until an unmatched velocity CC expires across blocks.
    return p.mapper.heldCells() || p.mapper.pending() ? CLAP_PROCESS_CONTINUE : CLAP_PROCESS_SLEEP;
}

uint32_t paramCount(const clap_plugin_t*) { return static_cast<uint32_t>(defs.size()); }
bool paramInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info) {
    if (!info || index >= defs.size()) return false;
    *info = {};
    info->id = index + 1u;
    info->flags = CLAP_PARAM_IS_STEPPED;
    if (index < 4u) info->flags |= CLAP_PARAM_IS_AUTOMATABLE;
    std::snprintf(info->name, sizeof(info->name), "%s", defs[index].name);
    std::snprintf(info->module, sizeof(info->module), "NEON MIDI");
    info->min_value = defs[index].min; info->max_value = defs[index].max;
    info->default_value = defs[index].initial;
    return true;
}
bool paramValue(const clap_plugin_t* plugin, clap_id id, double* value) {
    if (!value || id < kBank || id > kPanic) return false;
    *value = getValue(*self(plugin), id); return true;
}
bool valueText(const clap_plugin_t*, clap_id id, double value, char* text, uint32_t capacity) {
    if (!text || !capacity || id < kBank || id > kPanic || !std::isfinite(value)) return false;
    const int v = static_cast<int>(std::round(std::clamp(value, defs[id - 1u].min, defs[id - 1u].max)));
    if (id == kBank) std::snprintf(text, capacity, "%c", 'A' + v);
    else if (id == kRoute) std::snprintf(text, capacity, "%s", v ? "Tracker + Sample Neon" : "Notes Only");
    else if (id == kPanic) std::snprintf(text, capacity, "%s", v ? "Release" : "Ready");
    else std::snprintf(text, capacity, "%d", v);
    return true;
}
bool textValue(const clap_plugin_t*, clap_id id, const char* text, double* value) {
    if (!text || !value || id < kBank || id > kPanic) return false;
    if (id == kBank && text[0] && !text[1]
        && ((text[0] >= 'A' && text[0] <= 'D') || (text[0] >= 'a' && text[0] <= 'd'))) {
        *value = (text[0] >= 'a' ? text[0] - 'a' : text[0] - 'A'); return true;
    }
    if (id == kPanic && (!std::strcmp(text, "Release") || !std::strcmp(text, "Ready"))) {
        *value = !std::strcmp(text, "Release") ? 1. : 0.; return true;
    }
    if (id == kRoute && (!std::strcmp(text, "Tracker + Sample Neon") || !std::strcmp(text, "Notes Only"))) {
        *value = !std::strcmp(text, "Tracker + Sample Neon") ? 1. : 0.; return true;
    }
    char* end = nullptr;
    const double parsed = std::strtod(text, &end);
    if (end == text || *end || !std::isfinite(parsed)
        || parsed < defs[id - 1u].min || parsed > defs[id - 1u].max) return false;
    *value = std::round(parsed); return true;
}
const clap_plugin_params_t params {paramCount, paramInfo, paramValue, valueText, textValue, flushParams};

uint32_t portCount(const clap_plugin_t*, bool) { return 1u; }
bool portInfo(const clap_plugin_t*, uint32_t index, bool input, clap_note_port_info_t* info) {
    if (!info || index) return false;
    *info = {}; info->id = input ? 0u : 1u;
    info->supported_dialects = info->preferred_dialect = CLAP_NOTE_DIALECT_MIDI;
    std::snprintf(info->name, sizeof(info->name), "%s", input ? "NEON Hardware In" : "Notes + Controls Out");
    return true;
}
const clap_plugin_note_ports_t ports {portCount, portInfo};

bool saveState(const clap_plugin_t* plugin, const clap_ostream_t* stream) {
    const auto& p = *self(plugin);
    // Fixed byte layout: magic, version, bank, base, channel. No struct padding.
    const std::array<uint8_t, 9u> data {{'N', 'M', 'I', 'D', 1u,
        static_cast<uint8_t>(getValue(p, kBank)), static_cast<uint8_t>(getValue(p, kBase)),
        static_cast<uint8_t>(getValue(p, kChannel)), static_cast<uint8_t>(getValue(p, kRoute))}};
    return s3g::clap_state::writeAll(stream, data.data(), data.size());
}
bool loadState(const clap_plugin_t* plugin, const clap_istream_t* stream) {
    std::array<uint8_t, 9u> data {};
    if (!s3g::clap_state::readAll(stream, data.data(), data.size())
        || std::memcmp(data.data(), "NMID", 4u) || data[4] != 1u
        || data[5] > 3u || data[6] > 96u || data[7] < 1u || data[7] > 16u || data[8] > 1u) return false;
    auto& p = *self(plugin);
    for (uint32_t i = 0u; i < 4u; ++i) p.values[i].store(data[5u + i]);
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
    return true;
}
void destroy(const clap_plugin_t* plugin) {
#if defined(S3G_ENABLE_VSTGUI_CANVAS_GUI)
    destroyPortableGui(*self(plugin));
#endif
    delete self(plugin);
}
bool activate(const clap_plugin_t* plugin, double rate, uint32_t minimum, uint32_t maximum) {
    if (!std::isfinite(rate) || rate <= 0. || minimum > maximum || !maximum) return false;
    auto& p = *self(plugin);
    p.wasPlaying = false; p.mapper.prepare(rate); syncMapping(p); return true;
}
void deactivate(const clap_plugin_t* plugin) { self(plugin)->panicRequested.store(true); }
bool start(const clap_plugin_t*) { return true; }
void stop(const clap_plugin_t*) {}
void reset(const clap_plugin_t* plugin) {
    auto& p = *self(plugin); p.mapper.panic(); p.wasPlaying = false;
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
