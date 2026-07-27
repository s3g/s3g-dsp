#include "s3g_accelerometer_field_encoder.h"

#include <clap/clap.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <iostream>
#include <vector>

namespace {

constexpr clap_id kBodyCountParamId = 37u;
constexpr clap_id kBody1AzimuthParamId = 38u;
constexpr clap_id kListenerPickupSetParamId = 62u;
constexpr clap_id kModalLiftParamId = 63u;

const void* hostGetExtension(const clap_host_t*, const char*) { return nullptr; }
void hostRequest(const clap_host_t*) {}

struct MemoryState {
    std::vector<uint8_t> bytes;
    size_t offset = 0u;
};

int64_t stateWrite(
    const clap_ostream_t* stream, const void* source, uint64_t requested)
{
    auto* state = static_cast<MemoryState*>(stream->ctx);
    if (!state || (!source && requested > 0u)) return -1;
    const size_t count = std::min<size_t>(
        static_cast<size_t>(requested), 13u);
    const auto* first = static_cast<const uint8_t*>(source);
    state->bytes.insert(state->bytes.end(), first, first + count);
    return static_cast<int64_t>(count);
}

int64_t stateRead(
    const clap_istream_t* stream, void* destination, uint64_t requested)
{
    auto* state = static_cast<MemoryState*>(stream->ctx);
    if (!state || (!destination && requested > 0u)) return -1;
    const size_t available = state->offset < state->bytes.size()
        ? state->bytes.size() - state->offset : 0u;
    const size_t count = std::min<size_t>({
        available, static_cast<size_t>(requested), 11u,
    });
    if (count > 0u) {
        std::memcpy(destination,
            state->bytes.data() + state->offset, count);
        state->offset += count;
    }
    return static_cast<int64_t>(count);
}

template <typename Value>
void append(MemoryState& state, const Value& value)
{
    const auto* first = reinterpret_cast<const uint8_t*>(&value);
    state.bytes.insert(state.bytes.end(), first, first + sizeof(value));
}

struct StateHeader {
    uint32_t version = 0u;
    uint32_t presetIndex = 0u;
};

struct SavedGuiStateV8 {
    int32_t viewMode = 2;
    float viewAzimuthDeg = -35.0f;
    float viewElevationDeg = 34.0f;
    float viewZoom = 1.0f;
    uint32_t selectedBody = 0u;
};

std::filesystem::path resolveBinary(const std::filesystem::path& supplied)
{
    if (std::filesystem::is_regular_file(supplied)) return supplied;
#if defined(__APPLE__)
    if (std::filesystem::is_directory(supplied)) {
        const auto macOS = supplied / "Contents" / "MacOS";
        for (const auto& entry : std::filesystem::directory_iterator(macOS)) {
            if (entry.is_regular_file()) return entry.path();
        }
    }
#endif
    return {};
}

bool approximately(double first, double second)
{
    return std::fabs(first - second) <= 1.0e-6;
}

struct OneParamEvent {
    clap_event_param_value_t event {};
    clap_input_events_t events {
        this,
        [](const clap_input_events_t*) -> uint32_t { return 1u; },
        [](const clap_input_events_t* list, uint32_t index)
            -> const clap_event_header_t* {
            if (index != 0u) return nullptr;
            const auto* self = static_cast<const OneParamEvent*>(list->ctx);
            return self ? &self->event.header : nullptr;
        },
    };

    OneParamEvent(clap_id id, double value)
    {
        event.header.size = sizeof(event);
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_PARAM_VALUE;
        event.header.flags = CLAP_EVENT_IS_LIVE;
        event.param_id = id;
        event.note_id = -1;
        event.port_index = -1;
        event.channel = -1;
        event.key = -1;
        event.value = value;
    }
};

} // namespace

int main(int argc, char** argv)
{
    if (argc != 3) {
        std::cerr << "usage: s3g_accelerometer_field_encoder_clap_state_smoke "
                  << "<bundle-or-binary> <plugin-id>\n";
        return 2;
    }
    const auto binary = resolveBinary(argv[1]);
    if (binary.empty()) {
        std::cerr << "Could not resolve Modal Encoder binary\n";
        return 1;
    }
    void* library = dlopen(binary.c_str(), RTLD_LOCAL | RTLD_NOW);
    if (!library) {
        std::cerr << "Could not load Modal Encoder: " << dlerror() << "\n";
        return 1;
    }
    const auto* entry = static_cast<const clap_plugin_entry_t*>(
        dlsym(library, "clap_entry"));
    bool ok = entry && entry->init(binary.c_str());
    clap_host_t host {};
    host.clap_version = CLAP_VERSION_INIT;
    host.name = "Modal Encoder state smoke";
    host.vendor = "s3g";
    host.url = "https://github.com/s3g/s3g-dsp";
    host.version = "1";
    host.get_extension = hostGetExtension;
    host.request_restart = hostRequest;
    host.request_process = hostRequest;
    host.request_callback = hostRequest;
    const auto* factory = ok ? static_cast<const clap_plugin_factory_t*>(
        entry->get_factory(CLAP_PLUGIN_FACTORY_ID)) : nullptr;
    const clap_plugin_t* plugin = factory
        ? factory->create_plugin(factory, &host, argv[2]) : nullptr;
    ok = ok && plugin && plugin->init(plugin);
    const auto* params = ok ? static_cast<const clap_plugin_params_t*>(
        plugin->get_extension(plugin, CLAP_EXT_PARAMS)) : nullptr;
    const auto* state = ok ? static_cast<const clap_plugin_state_t*>(
        plugin->get_extension(plugin, CLAP_EXT_STATE)) : nullptr;
    ok = ok && params && state && params->count(plugin) == 63u;

    clap_param_info_t listenerInfo {};
    clap_param_info_t liftInfo {};
    bool foundListener = false;
    bool foundLift = false;
    if (ok) {
        for (uint32_t index = 0u; index < params->count(plugin); ++index) {
            clap_param_info_t info {};
            if (!params->get_info(plugin, index, &info)) continue;
            if (info.id == kListenerPickupSetParamId) {
                listenerInfo = info;
                foundListener = true;
            } else if (info.id == kModalLiftParamId) {
                liftInfo = info;
                foundLift = true;
            }
        }
        char cubeText[32] {};
        char liftText[32] {};
        double tetraValue = -1.0;
        double parsedLift = -1.0;
        double initialLift = -1.0;
        ok = foundListener && foundLift
            && std::strcmp(listenerInfo.name, "Listener pickups") == 0
            && std::strcmp(listenerInfo.module, "Listener / Actuator") == 0
            && listenerInfo.min_value == 0.0
            && listenerInfo.max_value == 1.0
            && listenerInfo.default_value == 1.0
            && (listenerInfo.flags & CLAP_PARAM_IS_STEPPED) != 0u
            && params->value_to_text(plugin,
                kListenerPickupSetParamId, 1.0,
                cubeText, sizeof(cubeText))
            && std::strcmp(cubeText, "CUBE 8") == 0
            && params->text_to_value(plugin,
                kListenerPickupSetParamId, "TETRA 4", &tetraValue)
            && tetraValue == 0.0
            && std::strcmp(liftInfo.name, "Modal lift") == 0
            && std::strcmp(liftInfo.module, "Output") == 0
            && liftInfo.min_value == 0.0
            && liftInfo.max_value == 1.0
            && approximately(liftInfo.default_value, 0.65)
            && (liftInfo.flags & CLAP_PARAM_IS_AUTOMATABLE) != 0u
            && (liftInfo.flags & CLAP_PARAM_IS_STEPPED) == 0u
            && params->get_value(plugin, kModalLiftParamId, &initialLift)
            && approximately(initialLift, 0.65)
            && params->value_to_text(plugin,
                kModalLiftParamId, 0.65, liftText, sizeof(liftText))
            && std::strcmp(liftText, "65 %") == 0
            && params->text_to_value(plugin,
                kModalLiftParamId, "41 %", &parsedLift)
            && approximately(parsedLift, 0.41);
    }
    double modalLift = 0.0;
    if (ok) {
        OneParamEvent lift(kModalLiftParamId, 0.82);
        params->flush(plugin, &lift.events, nullptr);
        ok = params->get_value(plugin, kModalLiftParamId, &modalLift)
            && approximately(modalLift, 0.82);
    }

    // Construct the exact v8 wire layout: header, the parameter prefix that
    // predated listenerPickupSet, then its persisted camera state.
    auto legacyParams = s3g::accelerometerFieldFactoryPreset(0u);
    legacyParams.bodyCount = 7u;
    legacyParams.outputGainDb = -17.25f;
    legacyParams.bodyAzimuthOffsetDeg[0] = 47.0f;
    const SavedGuiStateV8 legacyGui {
        0, 123.0f, -41.0f, 1.45f, 5u,
    };
    MemoryState v8;
    append(v8, StateHeader { 8u, 2u });
    const auto* legacyFirst = reinterpret_cast<const uint8_t*>(&legacyParams);
    constexpr size_t v8ParamsSize = offsetof(
        s3g::AccelerometerFieldParams, listenerPickupSet);
    v8.bytes.insert(v8.bytes.end(),
        legacyFirst, legacyFirst + v8ParamsSize);
    append(v8, legacyGui);
    if (ok) {
        clap_istream_t input { &v8, stateRead };
        ok = state->load(plugin, &input);
    }
    double bodyCount = 0.0;
    double bodyAzimuth = 0.0;
    double listenerSet = 0.0;
    if (ok) {
        ok = params->get_value(plugin, kBodyCountParamId, &bodyCount)
            && params->get_value(plugin,
                kBody1AzimuthParamId, &bodyAzimuth)
            && params->get_value(plugin,
                kListenerPickupSetParamId, &listenerSet)
            && bodyCount == 7.0
            && approximately(bodyAzimuth, 47.0)
            && listenerSet == 1.0;
    }

    MemoryState migrated;
    if (ok) {
        clap_ostream_t output { &migrated, stateWrite };
        ok = state->save(plugin, &output);
    }
    const size_t expectedSize = sizeof(StateHeader)
        + sizeof(s3g::AccelerometerFieldParams)
        + sizeof(SavedGuiStateV8);
    if (ok) {
        ok = migrated.bytes.size() == expectedSize;
    }
    if (ok) {
        StateHeader header {};
        s3g::AccelerometerFieldParams savedParams {};
        SavedGuiStateV8 savedGui {};
        std::memcpy(&header, migrated.bytes.data(), sizeof(header));
        std::memcpy(&savedParams,
            migrated.bytes.data() + sizeof(header), sizeof(savedParams));
        std::memcpy(&savedGui,
            migrated.bytes.data() + sizeof(header) + sizeof(savedParams),
            sizeof(savedGui));
        ok = header.version == 10u
            && header.presetIndex == 2u
            && savedParams.listenerPickupSet
                == s3g::AccelerometerFieldListenerPickupSet::Cube8
            && savedParams.bodyCount == 7u
            && approximately(savedParams.outputGainDb, -17.25)
            && savedGui.viewMode == legacyGui.viewMode
            && approximately(savedGui.viewAzimuthDeg,
                legacyGui.viewAzimuthDeg)
            && approximately(savedGui.viewElevationDeg,
                legacyGui.viewElevationDeg)
            && approximately(savedGui.viewZoom, legacyGui.viewZoom)
            && savedGui.selectedBody == legacyGui.selectedBody;
    }

    if (ok) {
        OneParamEvent tetra(kListenerPickupSetParamId, 0.0);
        params->flush(plugin, &tetra.events, nullptr);
        ok = params->get_value(plugin,
            kListenerPickupSetParamId, &listenerSet)
            && listenerSet == 0.0;
    }

    // Version 10 persists Modal Lift with the rest of the current parameter
    // aggregate. Exercise a non-default value, then disturb and reload it so
    // this checks both the wire bytes and the public parameter surface.
    if (ok) {
        OneParamEvent lift(kModalLiftParamId, 0.82);
        params->flush(plugin, &lift.events, nullptr);
    }
    MemoryState current;
    if (ok) {
        clap_ostream_t output { &current, stateWrite };
        ok = state->save(plugin, &output)
            && current.bytes.size() == expectedSize;
    }
    if (ok) {
        StateHeader header {};
        s3g::AccelerometerFieldParams savedParams {};
        std::memcpy(&header, current.bytes.data(), sizeof(header));
        std::memcpy(&savedParams,
            current.bytes.data() + sizeof(header), sizeof(savedParams));
        ok = header.version == 10u
            && approximately(savedParams.modalLift, 0.82)
            && savedParams.listenerPickupSet
                == s3g::AccelerometerFieldListenerPickupSet::Tetra4;
    }
    if (ok) {
        OneParamEvent disturb(kModalLiftParamId, 0.13);
        params->flush(plugin, &disturb.events, nullptr);
        current.offset = 0u;
        clap_istream_t input { &current, stateRead };
        ok = state->load(plugin, &input)
            && params->get_value(plugin, kModalLiftParamId, &modalLift)
            && approximately(modalLift, 0.82);
    }

    // Construct the exact v9 wire layout: its parameter aggregate ended
    // immediately before Modal Lift. Migration must opt old sessions out of
    // the new gain rider, while preserving the v9 listener and camera state.
    auto legacyV9Params = s3g::accelerometerFieldFactoryPreset(4u);
    legacyV9Params.bodyCount = 5u;
    legacyV9Params.outputGainDb = -14.75f;
    legacyV9Params.listenerPickupSet =
        s3g::AccelerometerFieldListenerPickupSet::Tetra4;
    legacyV9Params.bodyElevationOffsetDeg[2] = -33.0f;
    const SavedGuiStateV8 legacyV9Gui {
        1, -142.0f, 28.0f, 0.74f, 3u,
    };
    MemoryState v9;
    append(v9, StateHeader { 9u, 4u });
    const auto* legacyV9First =
        reinterpret_cast<const uint8_t*>(&legacyV9Params);
    constexpr size_t v9ParamsSize = offsetof(
        s3g::AccelerometerFieldParams, modalLift);
    v9.bytes.insert(v9.bytes.end(),
        legacyV9First, legacyV9First + v9ParamsSize);
    append(v9, legacyV9Gui);
    if (ok) {
        clap_istream_t input { &v9, stateRead };
        ok = state->load(plugin, &input);
    }
    double migratedElevation = 0.0;
    if (ok) {
        constexpr clap_id kBody3ElevationParamId = 45u;
        ok = params->get_value(plugin, kModalLiftParamId, &modalLift)
            && params->get_value(plugin,
                kBody3ElevationParamId, &migratedElevation)
            && approximately(modalLift, 0.0)
            && approximately(migratedElevation, -33.0);
    }
    MemoryState migratedV9;
    if (ok) {
        clap_ostream_t output { &migratedV9, stateWrite };
        ok = state->save(plugin, &output)
            && migratedV9.bytes.size() == expectedSize;
    }
    if (ok) {
        StateHeader header {};
        s3g::AccelerometerFieldParams savedParams {};
        SavedGuiStateV8 savedGui {};
        std::memcpy(&header, migratedV9.bytes.data(), sizeof(header));
        std::memcpy(&savedParams,
            migratedV9.bytes.data() + sizeof(header), sizeof(savedParams));
        std::memcpy(&savedGui,
            migratedV9.bytes.data() + sizeof(header) + sizeof(savedParams),
            sizeof(savedGui));
        ok = header.version == 10u
            && header.presetIndex == 4u
            && approximately(savedParams.modalLift, 0.0)
            && savedParams.bodyCount == 5u
            && approximately(savedParams.outputGainDb, -14.75)
            && savedParams.listenerPickupSet
                == s3g::AccelerometerFieldListenerPickupSet::Tetra4
            && savedGui.viewMode == legacyV9Gui.viewMode
            && approximately(savedGui.viewAzimuthDeg,
                legacyV9Gui.viewAzimuthDeg)
            && approximately(savedGui.viewElevationDeg,
                legacyV9Gui.viewElevationDeg)
            && approximately(savedGui.viewZoom, legacyV9Gui.viewZoom)
            && savedGui.selectedBody == legacyV9Gui.selectedBody;
    }

    if (plugin) plugin->destroy(plugin);
    if (entry) entry->deinit();
    dlclose(library);
    if (!ok) {
        std::cerr << "Modal Encoder CLAP state/parameter contract failed\n";
        return 1;
    }
    std::cout << "Modal Encoder CLAP state/parameter contract passed\n";
    return 0;
}
