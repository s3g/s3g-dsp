#include "s3g_accelerometer_field_encoder.h"

#include <clap/clap.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>

#include <algorithm>
#include <array>
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
constexpr clap_id kPresetParamId = 1u;
constexpr clap_id kSubstrateParamId = 2u;
constexpr clap_id kSkinExtentParamId = 14u;
constexpr clap_id kSkinYParamId = 15u;
constexpr clap_id kSkinXParamId = 16u;
constexpr clap_id kBody8SkinXParamId = 76u;
constexpr clap_id kBody8SkinYParamId = 77u;
static_assert(static_cast<uint32_t>(
    s3g::AccelerometerSubstrate::TensionedSkin) == 20u);
static_assert(static_cast<uint32_t>(
    s3g::AccelerometerSubstrate::LoadedMembrane) == 21u);
static_assert(static_cast<uint32_t>(
    s3g::AccelerometerSubstrate::CoupledMembrane) == 22u);
static_assert(static_cast<uint32_t>(
    s3g::AccelerometerSubstrate::CavityMembrane) == 23u);
static_assert(static_cast<uint32_t>(
    s3g::AccelerometerSubstrate::LooseMembrane) == 24u);
static_assert(static_cast<uint32_t>(
    s3g::AccelerometerSubstrate::Count) == 25u);
static_assert(s3g::kAccelerometerFieldPresetCount == 25u);

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

template <typename Value>
void appendPrefix(MemoryState& state, const Value& value, size_t size)
{
    const auto* first = reinterpret_cast<const uint8_t*>(&value);
    state.bytes.insert(state.bytes.end(), first, first + size);
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
    ok = ok && params && state && params->count(plugin) == 77u;

    clap_param_info_t listenerInfo {};
    clap_param_info_t liftInfo {};
    clap_param_info_t presetInfo {};
    clap_param_info_t substrateInfo {};
    clap_param_info_t skinExtentInfo {};
    clap_param_info_t skinYInfo {};
    clap_param_info_t skinXInfo {};
    clap_param_info_t body8SkinXInfo {};
    clap_param_info_t body8SkinYInfo {};
    bool foundListener = false;
    bool foundLift = false;
    bool foundPreset = false;
    bool foundSubstrate = false;
    bool foundSkinExtent = false;
    bool foundSkinY = false;
    bool foundSkinX = false;
    bool foundBody8SkinX = false;
    bool foundBody8SkinY = false;
    if (ok) {
        for (uint32_t index = 0u; index < params->count(plugin); ++index) {
            clap_param_info_t info {};
            if (!params->get_info(plugin, index, &info)) continue;
            if (info.id == kPresetParamId) {
                presetInfo = info;
                foundPreset = true;
            } else if (info.id == kListenerPickupSetParamId) {
                listenerInfo = info;
                foundListener = true;
            } else if (info.id == kModalLiftParamId) {
                liftInfo = info;
                foundLift = true;
            } else if (info.id == kSubstrateParamId) {
                substrateInfo = info;
                foundSubstrate = true;
            } else if (info.id == kSkinExtentParamId) {
                skinExtentInfo = info;
                foundSkinExtent = true;
            } else if (info.id == kSkinYParamId) {
                skinYInfo = info;
                foundSkinY = true;
            } else if (info.id == kSkinXParamId) {
                skinXInfo = info;
                foundSkinX = true;
            } else if (info.id == kBody8SkinXParamId) {
                body8SkinXInfo = info;
                foundBody8SkinX = true;
            } else if (info.id == kBody8SkinYParamId) {
                body8SkinYInfo = info;
                foundBody8SkinY = true;
            }
        }
        char cubeText[32] {};
        char liftText[32] {};
        double tetraValue = -1.0;
        double parsedLift = -1.0;
        double initialLift = -1.0;
        char customText[32] {};
        ok = foundListener && foundLift && foundPreset && foundSubstrate
            && foundSkinExtent && foundSkinY && foundSkinX
            && foundBody8SkinX && foundBody8SkinY
            && std::strcmp(presetInfo.name, "Preset") == 0
            && presetInfo.min_value == 0.0
            && presetInfo.max_value == 25.0
            && presetInfo.default_value == 0.0
            && (presetInfo.flags & CLAP_PARAM_IS_STEPPED) != 0u
            && params->value_to_text(plugin, kPresetParamId, 25.0,
                customText, sizeof(customText))
            && std::strcmp(customText, "Custom") == 0
            && std::strcmp(substrateInfo.name, "Modal profile") == 0
            && std::strcmp(substrateInfo.module, "Modal Body") == 0
            && substrateInfo.min_value == 0.0
            && substrateInfo.max_value == 24.0
            && substrateInfo.default_value == 10.0
            && (substrateInfo.flags & CLAP_PARAM_IS_STEPPED) != 0u
            && (substrateInfo.flags & CLAP_PARAM_IS_AUTOMATABLE) != 0u
            && std::strcmp(skinExtentInfo.name, "Skin extent") == 0
            && std::strcmp(skinExtentInfo.module, "Distributed Skin") == 0
            && approximately(skinExtentInfo.default_value, 0.0)
            && std::strcmp(skinYInfo.name, "Body 1 skin Y") == 0
            && std::strcmp(skinYInfo.module, "Distributed Skin") == 0
            && approximately(skinYInfo.default_value, 0.50)
            && std::strcmp(skinXInfo.name, "Body 1 skin X") == 0
            && std::strcmp(skinXInfo.module, "Distributed Skin") == 0
            && approximately(skinXInfo.default_value, 0.43)
            && (skinExtentInfo.flags & CLAP_PARAM_IS_AUTOMATABLE) != 0u
            && (skinYInfo.flags & CLAP_PARAM_IS_AUTOMATABLE) != 0u
            && (skinXInfo.flags & CLAP_PARAM_IS_AUTOMATABLE) != 0u
            && std::strcmp(body8SkinXInfo.name, "Body 8 skin X") == 0
            && std::strcmp(body8SkinYInfo.name, "Body 8 skin Y") == 0
            && std::strcmp(body8SkinXInfo.module, "Distributed Skin") == 0
            && std::strcmp(body8SkinYInfo.module, "Distributed Skin") == 0
            && (body8SkinXInfo.flags & CLAP_PARAM_IS_AUTOMATABLE) != 0u
            && (body8SkinYInfo.flags & CLAP_PARAM_IS_AUTOMATABLE) != 0u
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
    constexpr std::array<double, 16u> substrateValues {{
        10.0, 2.0, 11.0, 12.0, 13.0, 14.0,
        15.0, 16.0, 17.0, 18.0, 19.0, 20.0,
        21.0, 22.0, 23.0, 24.0,
    }};
    constexpr std::array<const char*, substrateValues.size()>
        substrateNames {{
            "DEEP BRONZE", "TIERED BRONZE", "BROAD BRONZE",
            "BRIGHT BRONZE", "CARBON LAM.", "GLASS PLATE",
            "STEEL SHELL", "ALUM. PLATE", "PORCELAIN",
            "EARTHENWARE", "SPRUCE PLATE", "TENSIONED SKIN",
            "LOADED MEM.", "COUPLED MEM.", "CAVITY MEM.",
            "LOOSE MEM.",
        }};
    if (ok) {
        for (uint32_t index = 0u; index < substrateValues.size(); ++index) {
            char text[64] {};
            double parsed = -1.0;
            ok = params->value_to_text(plugin, kSubstrateParamId,
                    substrateValues[index], text, sizeof(text))
                && std::strcmp(text, substrateNames[index]) == 0
                && params->text_to_value(plugin, kSubstrateParamId,
                    substrateNames[index], &parsed)
                && parsed == substrateValues[index];
            if (!ok) break;
        }
    }
    constexpr std::array<const char*, 5u> membranePresetNames {{
        "Tension Veil", "Loaded Drift", "Coupled Current",
        "Cavity Breath", "Loose Horizon",
    }};
    if (ok) {
        for (uint32_t index = 0u;
             index < membranePresetNames.size(); ++index) {
            const double presetValue = static_cast<double>(20u + index);
            char text[64] {};
            double parsed = -1.0;
            ok = params->value_to_text(plugin, kPresetParamId,
                    presetValue, text, sizeof(text))
                && std::strcmp(text, membranePresetNames[index]) == 0
                && params->text_to_value(plugin, kPresetParamId,
                    membranePresetNames[index], &parsed)
                && parsed == presetValue;
            if (!ok) break;
        }
    }

    // New raw profile IDs leave the parameter aggregate layout unchanged.
    // Verify the final membrane value survives a complete current round trip.
    MemoryState materialState;
    double substrateValue = 0.0;
    if (ok) {
        OneParamEvent looseMembrane(kSubstrateParamId, 24.0);
        params->flush(plugin, &looseMembrane.events, nullptr);
        clap_ostream_t output { &materialState, stateWrite };
        ok = params->get_value(plugin, kSubstrateParamId, &substrateValue)
            && substrateValue == 24.0
            && state->save(plugin, &output);
    }
    if (ok) {
        OneParamEvent disturb(kSubstrateParamId, 10.0);
        params->flush(plugin, &disturb.events, nullptr);
        materialState.offset = 0u;
        clap_istream_t input { &materialState, stateRead };
        ok = state->load(plugin, &input)
            && params->get_value(plugin,
                kSubstrateParamId, &substrateValue)
            && substrateValue == 24.0;
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
        ok = header.version == 13u
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

    // Released v10 used preset index 13 as Custom. The material expansion now
    // uses 13 for Carbon Veil, so v13 must translate the historical sentinel
    // without disturbing the v10 Modal Lift value or parameter aggregate.
    auto releasedV10Params = s3g::accelerometerFieldFactoryPreset(10u);
    releasedV10Params.modalLift = 0.47f;
    releasedV10Params.substrate = s3g::AccelerometerSubstrate::BroadBronze;
    const SavedGuiStateV8 releasedV10Gui {
        2, 71.0f, -19.0f, 1.18f, 4u,
    };
    MemoryState releasedV10;
    append(releasedV10, StateHeader { 10u, 13u });
    constexpr size_t v12ParamsSize = offsetof(
        s3g::AccelerometerFieldParams, bodySkinX);
    appendPrefix(releasedV10, releasedV10Params, v12ParamsSize);
    append(releasedV10, releasedV10Gui);
    if (ok) {
        clap_istream_t input { &releasedV10, stateRead };
        ok = state->load(plugin, &input);
    }
    double migratedPreset = -1.0;
    double migratedV10Lift = -1.0;
    double migratedV10Substrate = -1.0;
    if (ok) {
        ok = params->get_value(plugin, kPresetParamId, &migratedPreset)
            && params->get_value(
                plugin, kModalLiftParamId, &migratedV10Lift)
            && params->get_value(
                plugin, kSubstrateParamId, &migratedV10Substrate)
            && migratedPreset == 25.0
            && approximately(migratedV10Lift, 0.47)
            && migratedV10Substrate == 11.0;
    }
    MemoryState migratedV10State;
    if (ok) {
        clap_ostream_t output { &migratedV10State, stateWrite };
        ok = state->save(plugin, &output)
            && migratedV10State.bytes.size() == expectedSize;
    }
    if (ok) {
        StateHeader header {};
        s3g::AccelerometerFieldParams savedParams {};
        std::memcpy(&header, migratedV10State.bytes.data(), sizeof(header));
        std::memcpy(&savedParams,
            migratedV10State.bytes.data() + sizeof(header),
            sizeof(savedParams));
        ok = header.version == 13u
            && header.presetIndex == 25u
            && approximately(savedParams.modalLift, 0.47)
            && savedParams.substrate
                == s3g::AccelerometerSubstrate::BroadBronze;
    }

    // Released v11 used preset index 20 as Custom. Membrane presets now begin
    // at that index, so v13 must move only the old sentinel to current Custom
    // while retaining its full parameter aggregate, including Modal Lift.
    auto releasedV11Params = s3g::accelerometerFieldFactoryPreset(19u);
    releasedV11Params.modalLift = 0.73f;
    releasedV11Params.substrate = s3g::AccelerometerSubstrate::SprucePlate;
    const SavedGuiStateV8 releasedV11Gui {
        1, -93.0f, 22.0f, 0.86f, 6u,
    };
    MemoryState releasedV11;
    append(releasedV11, StateHeader { 11u, 20u });
    appendPrefix(releasedV11, releasedV11Params, v12ParamsSize);
    append(releasedV11, releasedV11Gui);
    if (ok) {
        clap_istream_t input { &releasedV11, stateRead };
        ok = state->load(plugin, &input);
    }
    double migratedV11Preset = -1.0;
    double migratedV11Lift = -1.0;
    double migratedV11Substrate = -1.0;
    double migratedV11Body8SkinX = -1.0;
    double migratedV11Body8SkinY = -1.0;
    if (ok) {
        ok = params->get_value(plugin, kPresetParamId, &migratedV11Preset)
            && params->get_value(
                plugin, kModalLiftParamId, &migratedV11Lift)
            && params->get_value(
                plugin, kSubstrateParamId, &migratedV11Substrate)
            && params->get_value(
                plugin, kBody8SkinXParamId, &migratedV11Body8SkinX)
            && params->get_value(
                plugin, kBody8SkinYParamId, &migratedV11Body8SkinY)
            && migratedV11Preset == 25.0
            && approximately(migratedV11Lift, 0.73)
            && migratedV11Substrate == 19.0
            && approximately(migratedV11Body8SkinX,
                releasedV11Params.sourcePosition)
            && approximately(migratedV11Body8SkinY,
                releasedV11Params.contactDetail);
    }
    MemoryState migratedV11State;
    if (ok) {
        clap_ostream_t output { &migratedV11State, stateWrite };
        ok = state->save(plugin, &output)
            && migratedV11State.bytes.size() == expectedSize;
    }
    if (ok) {
        StateHeader header {};
        s3g::AccelerometerFieldParams savedParams {};
        SavedGuiStateV8 savedGui {};
        std::memcpy(&header, migratedV11State.bytes.data(), sizeof(header));
        std::memcpy(&savedParams,
            migratedV11State.bytes.data() + sizeof(header),
            sizeof(savedParams));
        std::memcpy(&savedGui,
            migratedV11State.bytes.data() + sizeof(header)
                + sizeof(savedParams),
            sizeof(savedGui));
        ok = header.version == 13u
            && header.presetIndex == 25u
            && approximately(savedParams.modalLift, 0.73)
            && savedParams.substrate
                == s3g::AccelerometerSubstrate::SprucePlate
            && savedGui.viewMode == releasedV11Gui.viewMode
            && approximately(savedGui.viewAzimuthDeg,
                releasedV11Gui.viewAzimuthDeg)
            && approximately(savedGui.viewElevationDeg,
                releasedV11Gui.viewElevationDeg)
            && approximately(savedGui.viewZoom, releasedV11Gui.viewZoom)
            && savedGui.selectedBody == releasedV11Gui.selectedBody;
    }

    // Released v12 stored one shared X/Y contact. Version 13 must replicate
    // that exact contact across all eight body skins.
    auto releasedV12Params = s3g::accelerometerFieldFactoryPreset(0u);
    releasedV12Params.sourcePosition = 0.27f;
    releasedV12Params.contactDetail = 0.64f;
    const SavedGuiStateV8 releasedV12Gui {
        2, -35.0f, 34.0f, 1.0f, 7u,
    };
    MemoryState releasedV12;
    append(releasedV12, StateHeader { 12u, 25u });
    appendPrefix(releasedV12, releasedV12Params, v12ParamsSize);
    append(releasedV12, releasedV12Gui);
    double migratedV12Body1SkinX = -1.0;
    double migratedV12Body1SkinY = -1.0;
    double migratedV12Body8SkinX = -1.0;
    double migratedV12Body8SkinY = -1.0;
    if (ok) {
        clap_istream_t input { &releasedV12, stateRead };
        ok = state->load(plugin, &input)
            && params->get_value(
                plugin, kSkinXParamId, &migratedV12Body1SkinX)
            && params->get_value(
                plugin, kSkinYParamId, &migratedV12Body1SkinY)
            && params->get_value(
                plugin, kBody8SkinXParamId, &migratedV12Body8SkinX)
            && params->get_value(
                plugin, kBody8SkinYParamId, &migratedV12Body8SkinY)
            && approximately(migratedV12Body1SkinX, 0.27)
            && approximately(migratedV12Body1SkinY, 0.64)
            && approximately(migratedV12Body8SkinX, 0.27)
            && approximately(migratedV12Body8SkinY, 0.64);
    }

    if (ok) {
        OneParamEvent tetra(kListenerPickupSetParamId, 0.0);
        params->flush(plugin, &tetra.events, nullptr);
        ok = params->get_value(plugin,
            kListenerPickupSetParamId, &listenerSet)
            && listenerSet == 0.0;
    }

    // Version 13 persists Modal Lift, membrane profile IDs, and independent
    // per-body skin contacts. Exercise the final IDs as well as the aggregate.
    if (ok) {
        OneParamEvent lift(kModalLiftParamId, 0.82);
        params->flush(plugin, &lift.events, nullptr);
        OneParamEvent membrane(kSubstrateParamId, 24.0);
        params->flush(plugin, &membrane.events, nullptr);
        OneParamEvent body8SkinX(kBody8SkinXParamId, 0.91);
        params->flush(plugin, &body8SkinX.events, nullptr);
        OneParamEvent body8SkinY(kBody8SkinYParamId, 0.17);
        params->flush(plugin, &body8SkinY.events, nullptr);
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
        ok = header.version == 13u
            && header.presetIndex == 25u
            && approximately(savedParams.modalLift, 0.82)
            && savedParams.substrate
                == s3g::AccelerometerSubstrate::LooseMembrane
            && savedParams.listenerPickupSet
                == s3g::AccelerometerFieldListenerPickupSet::Tetra4
            && approximately(savedParams.bodySkinX[7u], 0.91)
            && approximately(savedParams.bodySkinY[7u], 0.17);
    }
    if (ok) {
        OneParamEvent disturb(kModalLiftParamId, 0.13);
        params->flush(plugin, &disturb.events, nullptr);
        OneParamEvent disturbSkin(kBody8SkinXParamId, 0.22);
        params->flush(plugin, &disturbSkin.events, nullptr);
        current.offset = 0u;
        clap_istream_t input { &current, stateRead };
        ok = state->load(plugin, &input)
            && params->get_value(plugin, kModalLiftParamId, &modalLift)
            && params->get_value(
                plugin, kSubstrateParamId, &substrateValue)
            && params->get_value(
                plugin, kBody8SkinXParamId, &migratedV11Body8SkinX)
            && approximately(modalLift, 0.82)
            && substrateValue == 24.0
            && approximately(migratedV11Body8SkinX, 0.91);
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
        ok = header.version == 13u
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

    // IDs 64–77 occupy the second mailbox word. Exercise the active-plugin
    // automation/report path so those edits cannot silently disappear.
    bool activated = false;
    if (ok) {
        activated = plugin->activate(plugin, 48000.0, 1u, 64u);
        ok = activated;
    }
    double activeBody8SkinX = -1.0;
    double activeBody8SkinY = -1.0;
    if (ok) {
        OneParamEvent x(kBody8SkinXParamId, 0.38);
        params->flush(plugin, &x.events, nullptr);
        OneParamEvent y(kBody8SkinYParamId, 0.79);
        params->flush(plugin, &y.events, nullptr);
        ok = params->get_value(
                plugin, kBody8SkinXParamId, &activeBody8SkinX)
            && params->get_value(
                plugin, kBody8SkinYParamId, &activeBody8SkinY)
            && approximately(activeBody8SkinX, 0.38)
            && approximately(activeBody8SkinY, 0.79);
    }
    if (activated) plugin->deactivate(plugin);

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
