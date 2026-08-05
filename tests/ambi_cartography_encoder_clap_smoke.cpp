#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>
#include <clap/ext/tail.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <iostream>
#include <limits>
#include <vector>

namespace {

constexpr clap_id kSitesParamId = 1u;
constexpr clap_id kOrderParamId = 3u;
constexpr clap_id kLayoutParamId = 4u;
constexpr clap_id kTimeReferenceParamId = 6u;
constexpr clap_id kNetworkSpreadParamId = 11u;
constexpr clap_id kPropagationParamId = 12u;
constexpr clap_id kAirParamId = 13u;
constexpr clap_id kDistanceLossParamId = 14u;
constexpr clap_id kCarryParamId = 15u;
constexpr clap_id kTurbulenceParamId = 16u;
constexpr clap_id kMacroEngineParamId = 17u;
constexpr clap_id kMacroMetricParamId = 18u;
constexpr clap_id kMacroParamId = 19u;
constexpr clap_id kMemoryParamId = 21u;
constexpr clap_id kProcessMixParamId = 26u;
constexpr clap_id kListenModeParamId = 27u;
constexpr clap_id kOutputParamId = 29u;
constexpr clap_id kSiteXParamId = 30u;
constexpr clap_id kSiteNetworkTrimParamId = 34u;
constexpr clap_id kSiteEnabledParamId = 35u;
constexpr clap_id kMultipathParamId = 36u;
constexpr clap_id kOcclusionParamId = 37u;
constexpr clap_id kNetworkWeatherParamId = 38u;
constexpr clap_id kRefractionParamId = 39u;
constexpr clap_id kMotionParamId = 40u;
constexpr clap_id kEcologyParamId = 41u;
constexpr clap_id kHorizonParamId = 42u;
constexpr clap_id kShredCircuitParamId = 43u;
constexpr clap_id kFractureProcessorParamId = 44u;
constexpr uint32_t kFrames = 256u;
constexpr size_t kLegacyStateBytes = 816u;
constexpr size_t kLandscapeV1StateBytes = 852u;
constexpr size_t kSiteProcessV1StateBytes = 868u;
constexpr size_t kCurrentStateBytes = 892u;
constexpr uint32_t kCameraStateMagic = 0x43414d52u;
constexpr uint32_t kLegacyCameraStateVersion = 1u;
constexpr uint32_t kCameraStateVersion = 2u;

struct CameraStateFixture {
    uint32_t magic = kCameraStateMagic;
    uint32_t version = kCameraStateVersion;
    int32_t viewMode = 2;
    float azimuthDeg = 38.0f;
    float elevationDeg = 32.0f;
    float zoom = 1.0f;
};

static_assert(sizeof(CameraStateFixture) == 24u);

struct MemoryState {
    std::vector<uint8_t> bytes;
    size_t offset = 0u;
};

int64_t stateWrite(const clap_ostream_t* stream,
    const void* source, uint64_t requested)
{
    auto* state = static_cast<MemoryState*>(stream->ctx);
    if (!state || (!source && requested > 0u)) return -1;
    const size_t count = std::min<size_t>(
        static_cast<size_t>(requested), 13u);
    const auto* bytes = static_cast<const uint8_t*>(source);
    state->bytes.insert(state->bytes.end(), bytes, bytes + count);
    return static_cast<int64_t>(count);
}

int64_t stateRead(const clap_istream_t* stream,
    void* destination, uint64_t requested)
{
    auto* state = static_cast<MemoryState*>(stream->ctx);
    if (!state || (!destination && requested > 0u)) return -1;
    const size_t available = state->offset < state->bytes.size()
        ? state->bytes.size() - state->offset : 0u;
    const size_t count = std::min<size_t>({ available,
        static_cast<size_t>(requested), 9u });
    if (count > 0u) {
        std::memcpy(destination, state->bytes.data() + state->offset, count);
        state->offset += count;
    }
    return static_cast<int64_t>(count);
}

struct ParamEvent {
    clap_event_param_value_t event {};
    clap_input_events_t input {
        this,
        [](const clap_input_events_t*) -> uint32_t { return 1u; },
        [](const clap_input_events_t* list,
            uint32_t index) -> const clap_event_header_t* {
            if (index != 0u) return nullptr;
            return &static_cast<const ParamEvent*>(
                list->ctx)->event.header;
        },
    };

    ParamEvent(clap_id id, double value)
    {
        event.header.size = sizeof(event);
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_PARAM_VALUE;
        event.param_id = id;
        event.note_id = -1;
        event.port_index = -1;
        event.channel = -1;
        event.key = -1;
        event.value = value;
    }
};

struct TransportEvent {
    clap_event_transport_t event {};
    clap_input_events_t input {
        this,
        [](const clap_input_events_t*) -> uint32_t { return 1u; },
        [](const clap_input_events_t* list,
            uint32_t index) -> const clap_event_header_t* {
            if (index != 0u) return nullptr;
            return &static_cast<const TransportEvent*>(
                list->ctx)->event.header;
        },
    };

    TransportEvent(uint32_t time, bool playing)
    {
        event.header.size = sizeof(event);
        event.header.time = time;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_TRANSPORT;
        event.flags = playing ? CLAP_TRANSPORT_IS_PLAYING : 0u;
    }

    TransportEvent(uint32_t time, bool playing, double seconds)
        : TransportEvent(time, playing)
    {
        event.flags |= CLAP_TRANSPORT_HAS_SECONDS_TIMELINE;
        event.song_pos_seconds = static_cast<clap_sectime>(std::llround(
            seconds * static_cast<double>(CLAP_SECTIME_FACTOR)));
    }
};

std::filesystem::path resolveBinary(const std::filesystem::path& supplied)
{
    if (std::filesystem::is_regular_file(supplied)) return supplied;
#if defined(__APPLE__)
    if (std::filesystem::is_directory(supplied)) {
        const auto directory = supplied / "Contents" / "MacOS";
        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            if (entry.is_regular_file()) return entry.path();
        }
    }
#endif
    return {};
}

const void* hostGetExtension(const clap_host_t*, const char*) { return nullptr; }
void hostRequest(const clap_host_t*) {}

bool setParam(const clap_plugin_t* plugin,
    const clap_plugin_params_t* params, clap_id id, double value)
{
    ParamEvent event(id, value);
    params->flush(plugin, &event.input, nullptr);
    double actual = 0.0;
    return params->get_value(plugin, id, &actual)
        && std::fabs(actual - value) <= 1.0e-6;
}

bool getParamInfo(const clap_plugin_t* plugin,
    const clap_plugin_params_t* params, clap_id id, clap_param_info_t& info)
{
    const uint32_t count = params->count(plugin);
    for (uint32_t index = 0u; index < count; ++index) {
        clap_param_info_t candidate {};
        if (params->get_info(plugin, index, &candidate)
            && candidate.id == id) {
            info = candidate;
            return true;
        }
    }
    return false;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 3) {
        std::cerr << "usage: s3g_ambi_cartography_encoder_clap_smoke "
                  << "<bundle-or-binary> <plugin-id>\n";
        return 2;
    }
    const auto binary = resolveBinary(argv[1]);
    if (binary.empty()) {
        std::cerr << "Could not resolve Cartography binary\n";
        return 1;
    }
    void* library = dlopen(binary.c_str(), RTLD_LOCAL | RTLD_NOW);
    if (!library) {
        std::cerr << "Could not load Cartography: " << dlerror() << "\n";
        return 1;
    }
    const auto* entry = static_cast<const clap_plugin_entry_t*>(
        dlsym(library, "clap_entry"));
    bool ok = entry && entry->init(binary.c_str());

    clap_host_t host {};
    host.clap_version = CLAP_VERSION_INIT;
    host.name = "Cartography smoke";
    host.vendor = "s3g";
    host.url = "https://github.com/s3g/s3g-dsp";
    host.version = "1";
    host.get_extension = hostGetExtension;
    host.request_restart = hostRequest;
    host.request_process = hostRequest;
    host.request_callback = hostRequest;

    const auto* factory = ok
        ? static_cast<const clap_plugin_factory_t*>(
            entry->get_factory(CLAP_PLUGIN_FACTORY_ID)) : nullptr;
    const clap_plugin_t* plugin = factory
        ? factory->create_plugin(factory, &host, argv[2]) : nullptr;
    ok = ok && plugin && plugin->init(plugin);
    const auto* params = ok
        ? static_cast<const clap_plugin_params_t*>(
            plugin->get_extension(plugin, CLAP_EXT_PARAMS)) : nullptr;
    const auto* state = ok
        ? static_cast<const clap_plugin_state_t*>(
            plugin->get_extension(plugin, CLAP_EXT_STATE)) : nullptr;
    const auto* ports = ok
        ? static_cast<const clap_plugin_audio_ports_t*>(
            plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS)) : nullptr;
    const auto* tail = ok
        ? static_cast<const clap_plugin_tail_t*>(
            plugin->get_extension(plugin, CLAP_EXT_TAIL)) : nullptr;
    ok = ok && params && state && ports && tail
        && params->count(plugin) == 44u
        && tail->get(plugin) >= 48000u * 60u;

    if (ok) {
        constexpr const char* expectedNames[7] {
            "Multipath", "Occlusion", "Network Weather", "Refraction",
            "Motion", "Feedback Ecology", "Audibility Horizon"
        };
        for (uint32_t index = 0u; ok && index < 7u; ++index) {
            clap_param_info_t info {};
            const bool bipolar = index == 3u;
            ok = params->get_info(plugin, 35u + index, &info)
                && info.id == kMultipathParamId + index
                && std::strcmp(info.name, expectedNames[index]) == 0
                && std::strcmp(info.module,
                    index < 4u ? "Landscape Path" : "Landscape Field") == 0
                && info.min_value == (bipolar ? -1.0 : 0.0)
                && info.max_value == 1.0
                && info.default_value == 0.0
                && (info.flags & CLAP_PARAM_IS_AUTOMATABLE) != 0u
                && (info.flags & CLAP_PARAM_IS_STEPPED) == 0u;
        }
    }

    if (ok) {
        clap_param_info_t engine {};
        clap_param_info_t shred {};
        clap_param_info_t fracture {};
        ok = getParamInfo(plugin, params, kMacroEngineParamId, engine)
            && std::strcmp(engine.name, "Process") == 0
            && std::strcmp(engine.module, "Site Process") == 0
            && engine.min_value == 0.0 && engine.max_value == 7.0
            && engine.default_value == 1.0
            && (engine.flags & CLAP_PARAM_IS_AUTOMATABLE) != 0u
            && (engine.flags & CLAP_PARAM_IS_STEPPED) != 0u
            && getParamInfo(plugin, params, kShredCircuitParamId, shred)
            && std::strcmp(shred.name, "Shred Circuit") == 0
            && std::strcmp(shred.module, "Site Process") == 0
            && shred.min_value == 0.0 && shred.max_value == 7.0
            && shred.default_value == 0.0
            && (shred.flags & CLAP_PARAM_IS_AUTOMATABLE) != 0u
            && (shred.flags & CLAP_PARAM_IS_STEPPED) != 0u
            && getParamInfo(plugin, params,
                kFractureProcessorParamId, fracture)
            && std::strcmp(fracture.name, "Fracture Processor") == 0
            && std::strcmp(fracture.module, "Site Process") == 0
            && fracture.min_value == 0.0 && fracture.max_value == 9.0
            && fracture.default_value == 0.0
            && (fracture.flags & CLAP_PARAM_IS_AUTOMATABLE) != 0u
            && (fracture.flags & CLAP_PARAM_IS_STEPPED) != 0u;
    }

    if (ok) {
        clap_audio_port_info_t input {};
        clap_audio_port_info_t output {};
        ok = ports->count(plugin, true) == 1u
            && ports->count(plugin, false) == 1u
            && ports->get(plugin, 0u, true, &input)
            && ports->get(plugin, 0u, false, &output)
            && input.channel_count == 2u
            && output.channel_count == 64u
            && std::strcmp(input.port_type, CLAP_PORT_STEREO) == 0;
    }

    if (ok) {
        char text[32] {};
        double parsed = 0.0;
        ok = params->value_to_text(plugin, kLayoutParamId, 4.0,
                text, sizeof(text))
            && std::strcmp(text, "WATERFRONT") == 0
            && params->value_to_text(plugin, kMacroEngineParamId, 4.0,
                text, sizeof(text))
            && std::strcmp(text, "FRACTURE") == 0
            && params->value_to_text(plugin, kListenModeParamId, 3.0,
                text, sizeof(text))
            && std::strcmp(text, "BALANCE") == 0
            && params->text_to_value(plugin, kPropagationParamId,
                "35%", &parsed)
            && std::fabs(parsed - 0.35) < 0.000001
            && params->value_to_text(plugin, kRefractionParamId, -0.25,
                text, sizeof(text))
            && std::strcmp(text, "-25%") == 0
            && params->text_to_value(plugin, kRefractionParamId,
                "+45%", &parsed)
            && std::fabs(parsed - 0.45) < 0.000001
            && params->value_to_text(plugin, kSiteEnabledParamId, 1.0,
                text, sizeof(text))
            && std::strcmp(text, "ON") == 0
            && params->text_to_value(plugin, kSiteEnabledParamId,
                "ON", &parsed)
            && parsed == 1.0
            && params->text_to_value(plugin, kSiteEnabledParamId,
                "OFF", &parsed)
            && parsed == 0.0;
    }

    if (ok) {
        constexpr std::array<const char*, 8u> engineNames {
            "CLEAN", "DELAY", "PITCH", "SHRED", "FRACTURE", "BODY",
            "SPECTRAL RELAY", "RELAY BUFFER"
        };
        constexpr std::array<const char*, 8u> shredNames {
            "SHRED", "WOOL", "RAT", "ZONE A", "ZONE B", "FUZZ I",
            "FUZZ II", "DIODE"
        };
        constexpr std::array<const char*, 10u> fractureNames {
            "RELAY", "CRUSH", "SPLICE", "LOGIC", "VOID", "THROAT",
            "ROBOT", "OCT DOWN", "OCT UP", "OCT STACK"
        };
        const auto verifyNames = [&](clap_id id, const auto& names) {
            for (uint32_t index = 0u; index < names.size(); ++index) {
                char text[32] {};
                double parsed = -1.0;
                if (!params->value_to_text(plugin, id,
                        static_cast<double>(index), text, sizeof(text))
                    || std::strcmp(text, names[index]) != 0
                    || !params->text_to_value(plugin, id,
                        names[index], &parsed)
                    || parsed != static_cast<double>(index)) {
                    return false;
                }
            }
            return true;
        };
        ok = verifyNames(kMacroEngineParamId, engineNames)
            && verifyNames(kShredCircuitParamId, shredNames)
            && verifyNames(kFractureProcessorParamId, fractureNames);
    }

    if (ok) {
        ok = setParam(plugin, params, kSitesParamId, 4.0)
            && setParam(plugin, params, kOrderParamId, 3.0)
            && setParam(plugin, params, kLayoutParamId, 3.0)
            && setParam(plugin, params, kMacroMetricParamId, 2.0)
            && setParam(plugin, params, kSiteXParamId, 0.33)
            && setParam(plugin, params, kMultipathParamId, 0.44)
            && setParam(plugin, params, kRefractionParamId, -0.35)
            && setParam(plugin, params, kHorizonParamId, 0.72)
            && setParam(plugin, params, kShredCircuitParamId, 6.0)
            && setParam(plugin, params, kFractureProcessorParamId, 9.0)
            && setParam(plugin, params, kOutputParamId, -3.0);
    }

    MemoryState saved;
    clap_ostream_t outputState { &saved, stateWrite };
    if (ok) ok = state->save(plugin, &outputState)
        && saved.bytes.size() == kCurrentStateBytes;
    if (ok) {
        CameraStateFixture camera {};
        std::memcpy(&camera,
            saved.bytes.data() + kSiteProcessV1StateBytes,
            sizeof(camera));
        ok = camera.magic == kCameraStateMagic
            && camera.version == kCameraStateVersion
            && camera.viewMode == 2
            && std::fabs(camera.azimuthDeg - 38.0f) < 0.0001f
            && std::fabs(camera.elevationDeg - 32.0f) < 0.0001f
            && std::fabs(camera.zoom - 1.0f) < 0.0001f;
    }
    if (ok) {
        ok = setParam(plugin, params, kLayoutParamId, 0.0)
            && setParam(plugin, params, kSiteXParamId, -0.75)
            && setParam(plugin, params, kShredCircuitParamId, 1.0)
            && setParam(plugin, params, kFractureProcessorParamId, 2.0);
        saved.offset = 0u;
        clap_istream_t inputState { &saved, stateRead };
        ok = ok && state->load(plugin, &inputState);
        double layout = 0.0;
        double siteX = 0.0;
        double multipath = 0.0;
        double refraction = 0.0;
        double horizon = 0.0;
        double shredCircuit = 0.0;
        double fractureProcessor = 0.0;
        ok = ok && params->get_value(plugin, kLayoutParamId, &layout)
            && params->get_value(plugin, kSiteXParamId, &siteX)
            && params->get_value(plugin, kMultipathParamId, &multipath)
            && params->get_value(plugin, kRefractionParamId, &refraction)
            && params->get_value(plugin, kHorizonParamId, &horizon)
            && params->get_value(plugin,
                kShredCircuitParamId, &shredCircuit)
            && params->get_value(plugin,
                kFractureProcessorParamId, &fractureProcessor)
            && layout == 3.0 && std::fabs(siteX - 0.33) < 0.0001
            && std::fabs(multipath - 0.44) < 0.0001
            && std::fabs(refraction + 0.35) < 0.0001
            && std::fabs(horizon - 0.72) < 0.0001
            && shredCircuit == 6.0 && fractureProcessor == 9.0;
    }

    const auto roundTripCamera = [&](const CameraStateFixture& authored,
                                     CameraStateFixture& restored) {
        MemoryState edited;
        edited.bytes = saved.bytes;
        std::memcpy(edited.bytes.data() + kSiteProcessV1StateBytes,
            &authored, sizeof(authored));
        clap_istream_t editedInput { &edited, stateRead };
        if (!state->load(plugin, &editedInput)) return false;
        MemoryState reSaved;
        clap_ostream_t reSavedOutput { &reSaved, stateWrite };
        if (!state->save(plugin, &reSavedOutput)
            || reSaved.bytes.size() != kCurrentStateBytes) {
            return false;
        }
        std::memcpy(&restored,
            reSaved.bytes.data() + kSiteProcessV1StateBytes,
            sizeof(restored));
        return true;
    };

    if (ok) {
        CameraStateFixture authored {};
        authored.viewMode = -1;
        authored.azimuthDeg = -73.25f;
        authored.elevationDeg = 22.5f;
        authored.zoom = 1.65f;
        CameraStateFixture restored {};
        ok = roundTripCamera(authored, restored)
            && restored.magic == authored.magic
            && restored.version == authored.version
            && restored.viewMode == authored.viewMode
            && std::fabs(restored.azimuthDeg - authored.azimuthDeg) < 0.0001f
            && std::fabs(restored.elevationDeg
                - authored.elevationDeg) < 0.0001f
            && std::fabs(restored.zoom - authored.zoom) < 0.0001f;
    }

    // SIDE is exactly edge-on at +90 degrees. It must not be narrowed to the
    // free-orbit +/-85 degree limit when a session is restored.
    if (ok) {
        CameraStateFixture side {};
        side.viewMode = 1;
        side.azimuthDeg = 0.0f;
        side.elevationDeg = 90.0f;
        side.zoom = 1.15f;
        CameraStateFixture restored {};
        ok = roundTripCamera(side, restored)
            && restored.viewMode == 1
            && std::fabs(restored.azimuthDeg) < 0.0001f
            && std::fabs(restored.elevationDeg - 90.0f) < 0.0001f
            && std::fabs(restored.zoom - 1.15f) < 0.0001f;
    }

    // Camera-v1 shipped briefly with the inverse pitch convention. Fixed
    // presets migrate to their upright v2 definitions, while a free camera
    // preserves its prior screen orientation by reversing elevation.
    if (ok) {
        CameraStateFixture legacyThreeQuarter {};
        legacyThreeQuarter.version = kLegacyCameraStateVersion;
        legacyThreeQuarter.viewMode = 2;
        legacyThreeQuarter.azimuthDeg = 38.0f;
        legacyThreeQuarter.elevationDeg = 32.0f;
        legacyThreeQuarter.zoom = 1.30f;
        CameraStateFixture restored {};
        ok = roundTripCamera(legacyThreeQuarter, restored)
            && restored.version == kCameraStateVersion
            && restored.viewMode == 2
            && std::fabs(restored.azimuthDeg - 38.0f) < 0.0001f
            && std::fabs(restored.elevationDeg - 32.0f) < 0.0001f
            && std::fabs(restored.zoom - 1.30f) < 0.0001f;
    }
    if (ok) {
        CameraStateFixture legacySide {};
        legacySide.version = kLegacyCameraStateVersion;
        legacySide.viewMode = 1;
        legacySide.elevationDeg = -90.0f;
        CameraStateFixture restored {};
        ok = roundTripCamera(legacySide, restored)
            && restored.version == kCameraStateVersion
            && restored.viewMode == 1
            && std::fabs(restored.azimuthDeg) < 0.0001f
            && std::fabs(restored.elevationDeg - 90.0f) < 0.0001f;
    }
    if (ok) {
        CameraStateFixture legacyFree {};
        legacyFree.version = kLegacyCameraStateVersion;
        legacyFree.viewMode = -1;
        legacyFree.azimuthDeg = -73.25f;
        legacyFree.elevationDeg = 22.5f;
        CameraStateFixture restored {};
        ok = roundTripCamera(legacyFree, restored)
            && restored.version == kCameraStateVersion
            && restored.viewMode == -1
            && std::fabs(restored.azimuthDeg + 73.25f) < 0.0001f
            && std::fabs(restored.elevationDeg + 22.5f) < 0.0001f;
    }

    if (ok) {
        CameraStateFixture outsideRange {};
        outsideRange.viewMode = 99;
        outsideRange.azimuthDeg = 250.0f;
        outsideRange.elevationDeg = -120.0f;
        outsideRange.zoom = 8.0f;
        CameraStateFixture restored {};
        ok = roundTripCamera(outsideRange, restored)
            && restored.viewMode == 2
            && std::fabs(restored.azimuthDeg - 180.0f) < 0.0001f
            && std::fabs(restored.elevationDeg + 90.0f) < 0.0001f
            && std::fabs(restored.zoom - 2.20f) < 0.0001f;
    }

    if (ok) {
        CameraStateFixture nonFinite {};
        nonFinite.viewMode = -1;
        nonFinite.azimuthDeg = std::numeric_limits<float>::quiet_NaN();
        nonFinite.elevationDeg = std::numeric_limits<float>::infinity();
        nonFinite.zoom = std::numeric_limits<float>::quiet_NaN();
        CameraStateFixture restored {};
        ok = roundTripCamera(nonFinite, restored)
            && restored.viewMode == -1
            && std::isfinite(restored.azimuthDeg)
            && std::fabs(restored.azimuthDeg - 38.0f) < 0.0001f
            && std::isfinite(restored.elevationDeg)
            && std::fabs(restored.elevationDeg - 32.0f) < 0.0001f
            && std::isfinite(restored.zoom)
            && std::fabs(restored.zoom - 1.0f) < 0.0001f;
    }

    // The immediately previous state generation ended after the process
    // selector suffix. Its fixed 2-D map migrates to TOP at unity zoom.
    if (ok) {
        MemoryState siteProcessV1;
        siteProcessV1.bytes.assign(saved.bytes.begin(),
            saved.bytes.begin() + kSiteProcessV1StateBytes);
        clap_istream_t oldInput { &siteProcessV1, stateRead };
        ok = state->load(plugin, &oldInput);
        MemoryState migrated;
        clap_ostream_t migratedOutput { &migrated, stateWrite };
        ok = ok && state->save(plugin, &migratedOutput)
            && migrated.bytes.size() == kCurrentStateBytes;
        CameraStateFixture camera {};
        if (ok) {
            std::memcpy(&camera,
                migrated.bytes.data() + kSiteProcessV1StateBytes,
                sizeof(camera));
            ok = camera.viewMode == 0
                && std::fabs(camera.azimuthDeg) < 0.0001f
                && std::fabs(camera.elevationDeg) < 0.0001f
                && std::fabs(camera.zoom - 1.0f) < 0.0001f;
        }
    }

    // Landscape-v1 states contain the 816-byte base and 36-byte landscape
    // suffix. Loading one restores that generation while defaulting the new
    // process selectors.
    if (ok) {
        MemoryState landscapeV1;
        landscapeV1.bytes.assign(saved.bytes.begin(),
            saved.bytes.begin() + kLandscapeV1StateBytes);
        ok = setParam(plugin, params, kMultipathParamId, 0.02)
            && setParam(plugin, params, kRefractionParamId, 0.03)
            && setParam(plugin, params, kHorizonParamId, 0.04)
            && setParam(plugin, params, kShredCircuitParamId, 7.0)
            && setParam(plugin, params, kFractureProcessorParamId, 8.0);
        landscapeV1.offset = 0u;
        clap_istream_t landscapeV1Input { &landscapeV1, stateRead };
        ok = ok && state->load(plugin, &landscapeV1Input);
        double multipath = 0.0;
        double refraction = 0.0;
        double horizon = 0.0;
        double shredCircuit = -1.0;
        double fractureProcessor = -1.0;
        ok = ok
            && params->get_value(plugin, kMultipathParamId, &multipath)
            && params->get_value(plugin, kRefractionParamId, &refraction)
            && params->get_value(plugin, kHorizonParamId, &horizon)
            && params->get_value(plugin,
                kShredCircuitParamId, &shredCircuit)
            && params->get_value(plugin,
                kFractureProcessorParamId, &fractureProcessor)
            && std::fabs(multipath - 0.44) < 0.0001
            && std::fabs(refraction + 0.35) < 0.0001
            && std::fabs(horizon - 0.72) < 0.0001
            && shredCircuit == 0.0 && fractureProcessor == 0.0;
    }

    if (ok) {
        MemoryState legacy;
        legacy.bytes.assign(saved.bytes.begin(),
            saved.bytes.begin() + kLegacyStateBytes);
        ok = setParam(plugin, params, kMultipathParamId, 0.9)
            && setParam(plugin, params, kNetworkWeatherParamId, 0.9)
            && setParam(plugin, params, kEcologyParamId, 0.9)
            && setParam(plugin, params, kShredCircuitParamId, 5.0)
            && setParam(plugin, params, kFractureProcessorParamId, 7.0);
        legacy.offset = 0u;
        clap_istream_t legacyInput { &legacy, stateRead };
        ok = ok && state->load(plugin, &legacyInput);
        for (clap_id id = kMultipathParamId;
            ok && id <= kHorizonParamId; ++id) {
            double value = 1.0;
            ok = params->get_value(plugin, id, &value)
                && std::fabs(value) < 0.000001;
        }
        double shredCircuit = -1.0;
        double fractureProcessor = -1.0;
        ok = ok
            && params->get_value(plugin,
                kShredCircuitParamId, &shredCircuit)
            && params->get_value(plugin,
                kFractureProcessorParamId, &fractureProcessor)
            && shredCircuit == 0.0 && fractureProcessor == 0.0;
    }

    if (ok) {
        MemoryState truncated;
        truncated.bytes.assign(saved.bytes.begin(),
            saved.bytes.begin() + kLegacyStateBytes + 5u);
        ok = setParam(plugin, params, kMultipathParamId, 0.61);
        truncated.offset = 0u;
        clap_istream_t truncatedInput { &truncated, stateRead };
        const bool rejected = !state->load(plugin, &truncatedInput);
        double multipath = 0.0;
        ok = ok && rejected
            && params->get_value(plugin, kMultipathParamId, &multipath)
            && std::fabs(multipath - 0.61) < 0.0001;
    }

    if (ok) {
        MemoryState truncatedSiteProcess;
        truncatedSiteProcess.bytes.assign(saved.bytes.begin(),
            saved.bytes.begin() + kLandscapeV1StateBytes + 5u);
        ok = setParam(plugin, params, kMultipathParamId, 0.37)
            && setParam(plugin, params, kShredCircuitParamId, 4.0)
            && setParam(plugin, params, kFractureProcessorParamId, 6.0);
        truncatedSiteProcess.offset = 0u;
        clap_istream_t truncatedInput { &truncatedSiteProcess, stateRead };
        const bool rejected = !state->load(plugin, &truncatedInput);
        double multipath = 0.0;
        double shredCircuit = 0.0;
        double fractureProcessor = 0.0;
        ok = ok && rejected
            && params->get_value(plugin, kMultipathParamId, &multipath)
            && params->get_value(plugin,
                kShredCircuitParamId, &shredCircuit)
            && params->get_value(plugin,
                kFractureProcessorParamId, &fractureProcessor)
            && std::fabs(multipath - 0.37) < 0.0001
            && shredCircuit == 4.0 && fractureProcessor == 6.0;
    }

    if (ok) {
        CameraStateFixture preserved {};
        preserved.viewMode = -1;
        preserved.azimuthDeg = 61.0f;
        preserved.elevationDeg = -24.0f;
        preserved.zoom = 1.45f;
        CameraStateFixture roundTripped {};
        ok = roundTripCamera(preserved, roundTripped)
            && setParam(plugin, params, kMultipathParamId, 0.53);

        MemoryState truncatedCamera;
        truncatedCamera.bytes.assign(saved.bytes.begin(),
            saved.bytes.begin() + kSiteProcessV1StateBytes + 5u);
        clap_istream_t truncatedInput { &truncatedCamera, stateRead };
        const bool rejected = !state->load(plugin, &truncatedInput);
        double multipath = 0.0;
        MemoryState afterReject;
        clap_ostream_t afterRejectOutput { &afterReject, stateWrite };
        ok = ok && rejected
            && params->get_value(plugin, kMultipathParamId, &multipath)
            && std::fabs(multipath - 0.53) < 0.0001
            && state->save(plugin, &afterRejectOutput)
            && afterReject.bytes.size() == kCurrentStateBytes;
        CameraStateFixture camera {};
        if (ok) {
            std::memcpy(&camera,
                afterReject.bytes.data() + kSiteProcessV1StateBytes,
                sizeof(camera));
            ok = camera.viewMode == preserved.viewMode
                && std::fabs(camera.azimuthDeg
                    - preserved.azimuthDeg) < 0.0001f
                && std::fabs(camera.elevationDeg
                    - preserved.elevationDeg) < 0.0001f
                && std::fabs(camera.zoom - preserved.zoom) < 0.0001f;
        }
    }

    if (ok) {
        MemoryState invalidCamera;
        invalidCamera.bytes = saved.bytes;
        CameraStateFixture invalid {};
        invalid.magic = 0u;
        std::memcpy(invalidCamera.bytes.data() + kSiteProcessV1StateBytes,
            &invalid, sizeof(invalid));
        clap_istream_t invalidInput { &invalidCamera, stateRead };
        ok = !state->load(plugin, &invalidInput);
    }
    if (ok) {
        MemoryState invalidCamera;
        invalidCamera.bytes = saved.bytes;
        CameraStateFixture invalid {};
        invalid.version = 99u;
        std::memcpy(invalidCamera.bytes.data() + kSiteProcessV1StateBytes,
            &invalid, sizeof(invalid));
        clap_istream_t invalidInput { &invalidCamera, stateRead };
        ok = !state->load(plugin, &invalidInput);
    }

    if (ok) {
        ok = setParam(plugin, params, kNetworkSpreadParamId, 0.0)
            && setParam(plugin, params, kPropagationParamId, 0.0)
            && setParam(plugin, params, kMacroEngineParamId, 0.0)
            && setParam(plugin, params, kListenModeParamId, 0.0)
            && setParam(plugin, params, kMultipathParamId, 0.0)
            && setParam(plugin, params, kOcclusionParamId, 0.0)
            && setParam(plugin, params, kNetworkWeatherParamId, 0.0)
            && setParam(plugin, params, kRefractionParamId, 0.0)
            && setParam(plugin, params, kMotionParamId, 0.0)
            && setParam(plugin, params, kEcologyParamId, 0.0)
            && setParam(plugin, params, kHorizonParamId, 0.0)
            && setParam(plugin, params, kShredCircuitParamId, 0.0)
            && setParam(plugin, params, kFractureProcessorParamId, 0.0)
            && setParam(plugin, params, kOutputParamId, 0.0)
            && plugin->activate(plugin, 48000.0, 1u, 31u)
            && plugin->start_processing(plugin);
    }

    std::array<std::array<float, kFrames>, 2u> inputStorage {};
    inputStorage[0][0] = 1.0f;
    inputStorage[1][0] = 1.0f;
    std::array<float*, 2u> inputPointers {
        inputStorage[0].data(), inputStorage[1].data()
    };
    std::array<std::array<float, kFrames>, 64u> outputStorage {};
    std::array<float*, 64u> outputPointers {};
    for (uint32_t channel = 0u; channel < 64u; ++channel) {
        outputPointers[channel] = outputStorage[channel].data();
    }
    clap_audio_buffer_t inputBuffer {};
    inputBuffer.data32 = inputPointers.data();
    inputBuffer.channel_count = 2u;
    clap_audio_buffer_t outputBuffer {};
    outputBuffer.data32 = outputPointers.data();
    outputBuffer.channel_count = 64u;
    clap_process_t process {};
    process.frames_count = kFrames;
    process.audio_inputs = &inputBuffer;
    process.audio_inputs_count = 1u;
    process.audio_outputs = &outputBuffer;
    process.audio_outputs_count = 1u;
    if (ok) {
        ok = plugin->process(plugin, &process) == CLAP_PROCESS_CONTINUE;
        double activeEnergy = 0.0;
        double inactiveEnergy = 0.0;
        for (uint32_t channel = 0u; channel < 64u; ++channel) {
            for (float sample : outputStorage[channel]) {
                if (!std::isfinite(sample)) ok = false;
                if (channel < 16u) activeEnergy += sample * sample;
                else inactiveEnergy += sample * sample;
            }
        }
        ok = ok && activeEnergy > 0.001 && inactiveEnergy == 0.0;
    }

    // Transport boundaries must enter the spatial delay network as short
    // continuous ramps; otherwise every site replays the same click later.
    // Lifecycle restart and reset also begin from host silence without
    // discarding a parked tail at full amplitude.
    if (ok) {
        ok = setParam(plugin, params, kSitesParamId, 1.0)
            && setParam(plugin, params, kOrderParamId, 1.0)
            && setParam(plugin, params, kNetworkSpreadParamId, 0.0)
            && setParam(plugin, params, kPropagationParamId, 0.0)
            && setParam(plugin, params, kAirParamId, 0.0)
            && setParam(plugin, params, kDistanceLossParamId, 0.0)
            && setParam(plugin, params, kCarryParamId, 0.0)
            && setParam(plugin, params, kTurbulenceParamId, 0.0)
            && setParam(plugin, params, kMacroEngineParamId, 0.0)
            && setParam(plugin, params, kProcessMixParamId, 0.0)
            && setParam(plugin, params, kListenModeParamId, 0.0)
            && setParam(plugin, params, kMultipathParamId, 0.0)
            && setParam(plugin, params, kOcclusionParamId, 0.0)
            && setParam(plugin, params, kNetworkWeatherParamId, 0.0)
            && setParam(plugin, params, kRefractionParamId, 0.0)
            && setParam(plugin, params, kMotionParamId, 0.0)
            && setParam(plugin, params, kEcologyParamId, 0.0)
            && setParam(plugin, params, kHorizonParamId, 0.0)
            && setParam(plugin, params, kOutputParamId, 0.0);
        plugin->reset(plugin);

        clap_event_transport_t playingTransport {};
        playingTransport.header.size = sizeof(playingTransport);
        playingTransport.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        playingTransport.header.type = CLAP_EVENT_TRANSPORT;
        playingTransport.flags = CLAP_TRANSPORT_IS_PLAYING;
        clap_event_transport_t stoppedTransport = playingTransport;
        stoppedTransport.flags = 0u;

        const auto runBlock = [&](uint32_t count, float inputValue,
                                  const clap_event_transport_t* transport) {
            for (auto& channel : inputStorage) {
                std::fill(channel.begin(), channel.end(), 0.0f);
                std::fill_n(channel.begin(), count, inputValue);
            }
            for (auto& channel : outputStorage) channel.fill(0.0f);
            process.frames_count = count;
            process.transport = transport;
            return plugin->process(plugin, &process)
                == CLAP_PROCESS_CONTINUE;
        };

        for (uint32_t block = 0u; ok && block < 16u; ++block) {
            ok = runBlock(kFrames, 0.25f, &playingTransport);
        }
        double settledPeak = 0.0;
        double settledEnergy = 0.0;
        std::array<float, 4u> settledLast {};
        for (uint32_t channel = 0u; channel < 4u; ++channel) {
            settledLast[channel] = outputStorage[channel][kFrames - 1u];
            for (float sample : outputStorage[channel]) {
                settledPeak = std::max(settledPeak,
                    static_cast<double>(std::fabs(sample)));
                settledEnergy += static_cast<double>(sample) * sample;
            }
        }
        const double settledRms = std::sqrt(
            settledEnergy / static_cast<double>(4u * kFrames));

        struct BoundaryMetrics {
            bool finite = true;
            double firstPeak = 0.0;
            double peak = 0.0;
            double maximumStep = 0.0;
            double postBridgeRms = 0.0;
            double lateRms = 0.0;
            std::array<float, 4u> last {};
        };
        const auto renderBoundary = [&](float inputValue,
                                        const clap_event_transport_t* transport,
                                        std::array<float, 4u> previous,
                                        uint32_t totalFrames) {
            BoundaryMetrics metrics {};
            const uint32_t lateFrames = std::min(256u, totalFrames);
            constexpr std::array<uint32_t, 5u> chunks {
                1u, 7u, 31u, 113u, 256u };
            double postBridgeEnergy = 0.0;
            uint32_t postBridgeSamples = 0u;
            double lateEnergy = 0.0;
            uint32_t lateSamples = 0u;
            uint32_t rendered = 0u;
            uint32_t chunkIndex = 0u;
            while (rendered < totalFrames) {
                const uint32_t count = std::min(
                    chunks[chunkIndex++ % chunks.size()],
                    totalFrames - rendered);
                if (!runBlock(count, inputValue, transport)) {
                    metrics.finite = false;
                    break;
                }
                for (uint32_t frame = 0u; frame < count; ++frame) {
                    const uint32_t absoluteFrame = rendered + frame;
                    for (uint32_t channel = 0u; channel < 4u; ++channel) {
                        const float sample = outputStorage[channel][frame];
                        metrics.finite = metrics.finite
                            && std::isfinite(sample);
                        if (absoluteFrame == 0u) {
                            metrics.firstPeak = std::max(
                                metrics.firstPeak,
                                static_cast<double>(std::fabs(sample)));
                        }
                        metrics.peak = std::max(metrics.peak,
                            static_cast<double>(std::fabs(sample)));
                        metrics.maximumStep = std::max(
                            metrics.maximumStep,
                            static_cast<double>(std::fabs(
                                sample - previous[channel])));
                        previous[channel] = sample;
                        if (absoluteFrame >= 1024u
                            && absoluteFrame < std::min(8192u, totalFrames)) {
                            postBridgeEnergy +=
                                static_cast<double>(sample) * sample;
                            ++postBridgeSamples;
                        }
                        if (absoluteFrame >= totalFrames - lateFrames) {
                            lateEnergy += static_cast<double>(sample) * sample;
                            ++lateSamples;
                        }
                    }
                }
                rendered += count;
            }
            metrics.last = previous;
            metrics.postBridgeRms = postBridgeSamples > 0u
                ? std::sqrt(postBridgeEnergy
                    / static_cast<double>(postBridgeSamples))
                : 0.0;
            metrics.lateRms = lateSamples > 0u
                ? std::sqrt(lateEnergy / static_cast<double>(lateSamples))
                : 0.0;
            return metrics;
        };

        const double stepLimit = std::max(0.0005, settledPeak * 0.03);
        const auto stopped = renderBoundary(
            0.0f, &stoppedTransport, settledLast, 1536u);
        const auto restarted = renderBoundary(
            0.25f, &playingTransport, stopped.last, 1536u);
        const bool directBoundaryOk = settledPeak > 0.10
            && settledRms > 0.04
            && stopped.finite && restarted.finite
            && stopped.maximumStep <= stepLimit
            && restarted.maximumStep <= stepLimit
            && stopped.lateRms < settledRms * 0.10
            && restarted.lateRms >= settledRms * 0.80;
        if (!directBoundaryOk) {
            std::cerr << "Direct transport boundary failed: settled="
                      << settledPeak << "/" << settledRms
                      << " step=" << stopped.maximumStep << "/"
                      << restarted.maximumStep << " limit=" << stepLimit
                      << " late=" << stopped.lateRms << "/"
                      << restarted.lateRms << "\n";
        }
        ok = ok && directBoundaryOk;

        const auto stoppedForEvent = renderBoundary(
            0.0f, &stoppedTransport, restarted.last, 1536u);
        TransportEvent midBlockStart(128u, true);
        for (auto& channel : inputStorage) {
            channel.fill(0.0f);
            std::fill(channel.begin() + 128u, channel.end(), 0.25f);
        }
        for (auto& channel : outputStorage) channel.fill(0.0f);
        process.frames_count = kFrames;
        process.transport = &stoppedTransport;
        process.in_events = &midBlockStart.input;
        const bool eventProcessed = plugin->process(plugin, &process)
            == CLAP_PROCESS_CONTINUE;
        process.in_events = nullptr;
        double eventMaximumStep = 0.0;
        double eventFinalPeak = 0.0;
        auto eventPrevious = stoppedForEvent.last;
        for (uint32_t frame = 0u; frame < kFrames; ++frame) {
            for (uint32_t channel = 0u; channel < 4u; ++channel) {
                const float sample = outputStorage[channel][frame];
                eventMaximumStep = std::max(eventMaximumStep,
                    static_cast<double>(std::fabs(
                        sample - eventPrevious[channel])));
                eventPrevious[channel] = sample;
                if (frame >= kFrames - 16u) {
                    eventFinalPeak = std::max(eventFinalPeak,
                        static_cast<double>(std::fabs(sample)));
                }
            }
        }
        const bool eventBoundaryOk = stoppedForEvent.finite
            && eventProcessed && eventMaximumStep <= stepLimit
            && eventFinalPeak > 0.02;
        if (!eventBoundaryOk) {
            std::cerr << "Sample-offset transport boundary failed: step="
                      << eventMaximumStep << " limit=" << stepLimit
                      << " final=" << eventFinalPeak << "\n";
        }
        ok = ok && eventBoundaryOk;

        // A loop or seek can jump the source while PLAYING remains set. Use
        // an exact timeline event at frame 128 and require that discontinuity
        // to enter the same pre-network correction as a play/stop edge.
        if (ok) {
            plugin->reset(plugin);
            clap_event_transport_t timelineTransport = playingTransport;
            timelineTransport.flags |=
                CLAP_TRANSPORT_HAS_SECONDS_TIMELINE;
            double timelineSeconds = 3.0;
            for (uint32_t block = 0u; ok && block < 16u; ++block) {
                timelineTransport.song_pos_seconds =
                    static_cast<clap_sectime>(std::llround(
                        timelineSeconds
                            * static_cast<double>(CLAP_SECTIME_FACTOR)));
                ok = runBlock(kFrames, 0.25f, &timelineTransport);
                timelineSeconds += static_cast<double>(kFrames) / 48000.0;
            }
            std::array<float, 4u> loopPrevious {};
            for (uint32_t channel = 0u; channel < 4u; ++channel) {
                loopPrevious[channel] =
                    outputStorage[channel][kFrames - 1u];
            }
            timelineTransport.song_pos_seconds =
                static_cast<clap_sectime>(std::llround(
                    timelineSeconds
                        * static_cast<double>(CLAP_SECTIME_FACTOR)));
            constexpr double loopDestinationSeconds = 1.0;
            TransportEvent loopJump(
                128u, true, loopDestinationSeconds);
            for (auto& channel : inputStorage) {
                std::fill(channel.begin(), channel.begin() + 128u, 0.25f);
                std::fill(channel.begin() + 128u, channel.end(), -0.25f);
            }
            for (auto& channel : outputStorage) channel.fill(0.0f);
            process.frames_count = kFrames;
            process.transport = &timelineTransport;
            process.in_events = &loopJump.input;
            const bool loopProcessed = plugin->process(plugin, &process)
                == CLAP_PROCESS_CONTINUE;
            process.in_events = nullptr;
            process.transport = nullptr;
            double loopMaximumStep = 0.0;
            for (uint32_t frame = 0u; frame < kFrames; ++frame) {
                for (uint32_t channel = 0u; channel < 4u; ++channel) {
                    const float sample = outputStorage[channel][frame];
                    loopMaximumStep = std::max(loopMaximumStep,
                        static_cast<double>(std::fabs(
                            sample - loopPrevious[channel])));
                    loopPrevious[channel] = sample;
                }
            }
            const auto loopContinuation = renderBoundary(
                -0.25f, nullptr, loopPrevious, 1536u);
            loopMaximumStep = std::max(
                loopMaximumStep, loopContinuation.maximumStep);
            const bool loopBoundaryOk = loopProcessed
                && loopContinuation.finite
                && loopMaximumStep <= stepLimit
                && loopContinuation.last[0] < -0.10f;
            if (!loopBoundaryOk) {
                std::cerr << "Seek/loop transport boundary failed: step="
                          << loopMaximumStep << " limit=" << stepLimit
                          << " final=" << loopContinuation.last[0] << "\n";
            }
            ok = ok && loopBoundaryOk;
        }

        // The correction must happen before the network ring, not merely at
        // the output. An 80 ms path places the returned edge well beyond the
        // 10 ms lifecycle bridge and exposes any click that entered history.
        if (ok) {
            ok = setParam(plugin, params, kTimeReferenceParamId, 1.0)
                && setParam(plugin, params, kSiteNetworkTrimParamId, 80.0);
            plugin->reset(plugin);
            for (uint32_t block = 0u; ok && block < 32u; ++block) {
                ok = runBlock(kFrames, 0.25f, &playingTransport);
            }
            double delayedPeak = 0.0;
            double delayedEnergy = 0.0;
            std::array<float, 4u> delayedLast {};
            for (uint32_t channel = 0u; channel < 4u; ++channel) {
                delayedLast[channel] = outputStorage[channel][kFrames - 1u];
                for (float sample : outputStorage[channel]) {
                    delayedPeak = std::max(delayedPeak,
                        static_cast<double>(std::fabs(sample)));
                    delayedEnergy += static_cast<double>(sample) * sample;
                }
            }
            const double delayedRms = std::sqrt(delayedEnergy
                / static_cast<double>(4u * kFrames));
            const double delayedStepLimit = std::max(
                0.0005, delayedPeak * 0.03);
            const auto delayedStop = renderBoundary(
                0.0f, &stoppedTransport, delayedLast, 12000u);
            const auto delayedStart = renderBoundary(
                0.25f, &playingTransport, delayedStop.last, 12000u);
            const bool delayedBoundaryOk = delayedPeak > 0.10
                && delayedRms > 0.04
                && delayedStop.finite && delayedStart.finite
                && delayedStop.maximumStep <= delayedStepLimit
                && delayedStart.maximumStep <= delayedStepLimit
                && delayedStop.lateRms < delayedRms * 0.10
                && delayedStart.lateRms >= delayedRms * 0.80;
            if (!delayedBoundaryOk) {
                std::cerr << "Delayed transport boundary failed: settled="
                          << delayedPeak << "/" << delayedRms
                          << " step=" << delayedStop.maximumStep << "/"
                          << delayedStart.maximumStep << " limit="
                          << delayedStepLimit << " late="
                          << delayedStop.lateRms << "/"
                          << delayedStart.lateRms << "\n";
            }
            ok = ok && delayedBoundaryOk;

            ok = ok
                && setParam(plugin, params, kTimeReferenceParamId, 0.0)
                && setParam(plugin, params, kSiteNetworkTrimParamId, 0.0);
            plugin->reset(plugin);
            for (uint32_t block = 0u; ok && block < 16u; ++block) {
                ok = runBlock(kFrames, 0.25f, &playingTransport);
            }
        }

        const auto verifyLifecycleRestart = [&](bool callReset,
                                                bool reactivate) {
            plugin->stop_processing(plugin);
            if (callReset) plugin->reset(plugin);
            if (reactivate) {
                plugin->deactivate(plugin);
                if (!plugin->activate(plugin, 48000.0, 1u, kFrames)) {
                    return false;
                }
            }
            if (!plugin->start_processing(plugin)) return false;
            const auto boundary = renderBoundary(
                0.25f, &playingTransport, {}, 1536u);
            return boundary.finite
                && boundary.firstPeak <= std::max(0.001,
                    settledPeak * 0.02)
                && boundary.maximumStep <= stepLimit
                && boundary.lateRms >= settledRms * 0.80;
        };
        const bool stopStartOk = verifyLifecycleRestart(false, false);
        const bool resetRestartOk = stopStartOk
            && verifyLifecycleRestart(true, false);
        const bool reactivateRestartOk = resetRestartOk
            && verifyLifecycleRestart(false, true);
        if (!stopStartOk || !resetRestartOk || !reactivateRestartOk) {
            std::cerr << "Lifecycle restart failed: stop/start="
                      << stopStartOk << " reset=" << resetRestartOk
                      << " reactivate=" << reactivateRestartOk << "\n";
        }
        ok = ok && stopStartOk && resetRestartOk && reactivateRestartOk;

        // A short input ramp is continuous but Body's horn high-pass can
        // differentiate it into a large pulse. Its transport correction is
        // therefore process-aware and long enough to keep that pulse quiet
        // before it can enter propagation and feedback memories.
        if (ok) {
            ok = setParam(plugin, params, kMacroEngineParamId, 5.0)
                && setParam(plugin, params, kMacroParamId, 1.0)
                && setParam(plugin, params, kMemoryParamId, 0.70)
                && setParam(plugin, params, kProcessMixParamId, 1.0);
            plugin->reset(plugin);
            for (uint32_t block = 0u; ok && block < 400u; ++block) {
                ok = runBlock(kFrames, 0.25f, &playingTransport);
            }
            std::array<float, 4u> bodyLast {};
            double bodySettledPeak = 0.0;
            for (uint32_t channel = 0u; channel < 4u; ++channel) {
                bodyLast[channel] = outputStorage[channel][kFrames - 1u];
                for (float sample : outputStorage[channel]) {
                    bodySettledPeak = std::max(bodySettledPeak,
                        static_cast<double>(std::fabs(sample)));
                }
            }
            const auto bodyStop = renderBoundary(
                0.0f, &stoppedTransport, bodyLast, 9600u);
            const auto bodyStart = renderBoundary(
                0.25f, &playingTransport, bodyStop.last, 9600u);
            const bool bodyTransportOk = bodyStop.finite
                && bodyStart.finite && bodySettledPeak < 0.01
                && bodyStop.peak <= 0.0625
                && bodyStart.peak <= 0.0625
                && bodyStop.maximumStep <= 0.0075
                && bodyStart.maximumStep <= 0.0075;
            if (!bodyTransportOk) {
                std::cerr << "Body transport transient failed: settled="
                          << bodySettledPeak << " peak="
                          << bodyStop.peak << "/" << bodyStart.peak
                          << " step=" << bodyStop.maximumStep << "/"
                          << bodyStart.maximumStep << "\n";
            }
            std::cout << "Cartography Body transport peak stop/start: "
                      << bodyStop.peak << "/" << bodyStart.peak << "\n";
            ok = ok && bodyTransportOk;
        }

        // A real feedback delay remains parked across a host sleep. Restart
        // onto silent input: the lifecycle bridge should make the first edge
        // quiet while energy after that bridge proves the tail was retained.
        if (ok) {
            ok = setParam(plugin, params, kMacroEngineParamId, 1.0)
                && setParam(plugin, params, kMacroParamId, 0.35)
                && setParam(plugin, params, kMemoryParamId, 1.0)
                && setParam(plugin, params, kProcessMixParamId, 1.0);
            plugin->reset(plugin);
            for (uint32_t block = 0u; ok && block < 192u; ++block) {
                ok = runBlock(kFrames, 0.25f, &playingTransport);
            }
            double tailSettledPeak = 0.0;
            double tailSettledEnergy = 0.0;
            for (uint32_t channel = 0u; channel < 4u; ++channel) {
                for (float sample : outputStorage[channel]) {
                    tailSettledPeak = std::max(tailSettledPeak,
                        static_cast<double>(std::fabs(sample)));
                    tailSettledEnergy += static_cast<double>(sample) * sample;
                }
            }
            const double tailSettledRms = std::sqrt(tailSettledEnergy
                / static_cast<double>(4u * kFrames));
            plugin->stop_processing(plugin);
            ok = ok && plugin->start_processing(plugin);
            const auto parkedTail = renderBoundary(
                0.0f, &stoppedTransport, {}, 48000u);
            const double tailStepLimit = std::max(
                0.0005, tailSettledPeak * 0.03);
            const bool parkedTailOk = tailSettledPeak > 0.02
                && tailSettledRms > 0.01
                && parkedTail.finite
                && parkedTail.firstPeak <= std::max(
                    0.001, tailSettledPeak * 0.02)
                && parkedTail.maximumStep <= tailStepLimit
                && parkedTail.postBridgeRms >= tailSettledRms * 0.10
                && parkedTail.lateRms < parkedTail.postBridgeRms * 0.50;
            if (!parkedTailOk) {
                std::cerr << "Parked delay tail failed: settled="
                          << tailSettledPeak << "/" << tailSettledRms
                          << " first=" << parkedTail.firstPeak
                          << " step=" << parkedTail.maximumStep
                          << " limit=" << tailStepLimit << " post="
                          << parkedTail.postBridgeRms << " late="
                          << parkedTail.lateRms << "\n";
            }
            ok = ok && parkedTailOk;
        }
    }

    if (plugin) {
        plugin->stop_processing(plugin);
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
    }
    if (entry && entry->deinit) entry->deinit();
    dlclose(library);
    if (!ok) {
        std::cerr << "Cartography CLAP parameter/state/audio smoke failed\n";
        return 1;
    }
    std::cout << "Cartography CLAP parameter/state/audio smoke passed for "
              << argv[2] << "\n";
    return 0;
}
