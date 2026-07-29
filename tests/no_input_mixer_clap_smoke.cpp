#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/note-ports.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <iostream>
#include <thread>
#include <vector>

namespace {

constexpr uint32_t kChannels = 8u;
constexpr uint32_t kFrames = 256u;
constexpr uint32_t kParamCount = 400u;
constexpr clap_id kOutputParam = 1u;
constexpr clap_id kFeedbackParam = 5u;
constexpr clap_id kQualityParam = 10u;
constexpr clap_id kFirstMatrixParam = 100u;
constexpr clap_id kMotionShapeParam = 20u;
constexpr clap_id kAuxATypeParam = 23u;
constexpr clap_id kAuxAMuteParam = 33u;
constexpr clap_id kAuxBMuteParam = 34u;
constexpr clap_id kBehaviorParam = 35u;
constexpr clap_id kEventRateParam = 36u;
constexpr clap_id kEventChokeParam = 41u;
constexpr clap_id kAuxABiasParam = 42u;
constexpr clap_id kAuxBBiasParam = 43u;
constexpr clap_id kReactModeParam = 44u;
constexpr clap_id kReactDepthParam = 45u;
constexpr clap_id kClockSyncParam = 52u;
constexpr clap_id kFieldDivisionParam = 53u;
constexpr clap_id kSurfaceXParam = 55u;
constexpr clap_id kLaneOneMuteParam = 1003u;
constexpr clap_id kLaneOneMidFrequencyParam = 1005u;
constexpr clap_id kLaneOneAuxAParam = 1008u;
constexpr clap_id kLaneOneTuneParam = 1010u;
constexpr clap_id kLaneOnePitchLockParam = 1012u;
constexpr clap_id kLaneOneAuxTapAParam = 1013u;
constexpr clap_id kLaneOneAuxReturnAParam = 1015u;

struct LegacyInsert {
    uint32_t type = 0u;
    float gain = 0.35f;
    float tone = 0.50f;
    float bias = 0.0f;
    float levelDb = 0.0f;
    uint32_t bypass = 0u;
};

struct LegacyLane {
    float body = 0.50f;
    float loss = 0.38f;
    float levelDb = -3.0f;
    uint32_t mute = 0u;
    float lowDb = 0.0f;
    float midFrequencyHz = 850.0f;
    float midGainDb = 0.0f;
    float highDb = 0.0f;
    std::array<LegacyInsert, 3u> inserts {};
};

struct LegacyParams {
    float outputGainDb = -18.0f;
    float ceilingDb = -1.0f;
    uint32_t limiterEnabled = 1u;
    uint32_t dcBlockEnabled = 1u;
    float feedback = 0.82f;
    float coupling = 0.42f;
    float phase = 0.34f;
    float drift = 0.18f;
    float formant = 0.30f;
    uint32_t quality = 1u;
    uint32_t seed = 0x5455444fu;
    std::array<float, 64u> matrix {};
    std::array<LegacyLane, 8u> lanes {};
};

struct LegacyState {
    uint32_t version = 1u;
    LegacyParams params {};
    uint32_t selectedLane = 2u;
    uint32_t selectedSlot = 0u;
    uint32_t selectedSource = 2u;
    uint32_t selectedDestination = 2u;
    uint32_t guiPage = 0u;
};

struct Version4Aux {
    LegacyInsert effect {};
    float feedback = 0.24f;
    float returnGain = 0.18f;
};

struct Version4Lane {
    float body = 0.50f;
    float loss = 0.38f;
    float levelDb = -3.0f;
    uint32_t mute = 0u;
    float lowDb = 0.0f;
    float midFrequencyHz = 850.0f;
    float midGainDb = 0.0f;
    float highDb = 0.0f;
    std::array<float, 2u> auxSend {{ 0.0f, 0.0f }};
    std::array<LegacyInsert, 3u> inserts {};
};

struct Version4Params {
    float outputGainDb = -18.0f;
    float ceilingDb = -1.0f;
    uint32_t limiterEnabled = 1u;
    uint32_t dcBlockEnabled = 1u;
    float feedback = 0.82f;
    float coupling = 0.42f;
    float phase = 0.34f;
    float drift = 0.18f;
    float formant = 0.30f;
    float agency = 0.28f;
    float space = 0.10f;
    float variance = 0.12f;
    float internalTone = 0.0f;
    float houseTone = -0.08f;
    float flow = 0.42f;
    float spread = 0.36f;
    float vortex = 0.0f;
    float motion = 0.0f;
    uint32_t motionShape = 0u;
    float motionRate = 0.15f;
    float motionPhase = 0.0f;
    uint32_t quality = 1u;
    uint32_t seed = 0x5455444fu;
    std::array<Version4Aux, 2u> aux {};
    std::array<float, 64u> matrix {};
    std::array<Version4Lane, 8u> lanes {};
};

struct Version4Behavior {
    uint32_t behavior = 0u;
    float eventRate = 0.42f;
    float length = 0.32f;
    float density = 0.55f;
    float chaos = 0.34f;
    float slew = 0.22f;
    float choke = 0.0f;
};

struct Version4State {
    uint32_t version = 4u;
    Version4Params params {};
    uint32_t selectedLane = 2u;
    uint32_t selectedSlot = 0u;
    uint32_t selectedSource = 2u;
    uint32_t selectedDestination = 2u;
    uint32_t guiPage = 0u;
    std::array<uint32_t, 2u> auxMute {};
    Version4Behavior behavior {};
};

const void* hostGetExtension(const clap_host_t*, const char*) { return nullptr; }
void hostRequest(const clap_host_t*) {}

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

struct EventList {
    std::vector<clap_event_param_value_t> storage;
    clap_input_events_t input {
        this,
        [](const clap_input_events_t* list) -> uint32_t {
            const auto* self = static_cast<const EventList*>(list->ctx);
            return self ? static_cast<uint32_t>(self->storage.size()) : 0u;
        },
        [](const clap_input_events_t* list, uint32_t index)
            -> const clap_event_header_t* {
            const auto* self = static_cast<const EventList*>(list->ctx);
            return self && index < self->storage.size()
                ? &self->storage[index].header : nullptr;
        },
    };

    void add(clap_id id, double value, uint32_t time = 0u)
    {
        clap_event_param_value_t event {};
        event.header.size = sizeof(event);
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_PARAM_VALUE;
        event.header.time = time;
        event.param_id = id;
        event.note_id = -1;
        event.port_index = -1;
        event.channel = -1;
        event.key = -1;
        event.value = value;
        storage.push_back(event);
    }
};

struct MidiEventList {
    std::vector<clap_event_midi_t> storage;
    clap_input_events_t input {
        this,
        [](const clap_input_events_t* list) -> uint32_t {
            const auto* self = static_cast<const MidiEventList*>(list->ctx);
            return self ? static_cast<uint32_t>(self->storage.size()) : 0u;
        },
        [](const clap_input_events_t* list, uint32_t index)
            -> const clap_event_header_t* {
            const auto* self = static_cast<const MidiEventList*>(list->ctx);
            return self && index < self->storage.size()
                ? &self->storage[index].header : nullptr;
        },
    };

    void add(uint8_t status, uint8_t dataOne, uint8_t dataTwo,
        uint32_t time = 0u)
    {
        clap_event_midi_t event {};
        event.header.size = sizeof(event);
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_MIDI;
        event.header.time = time;
        event.port_index = 0u;
        event.data[0] = status;
        event.data[1] = dataOne;
        event.data[2] = dataTwo;
        storage.push_back(event);
    }

    void addNrpn(clap_id id, uint16_t value, uint8_t channel = 15u)
    {
        const uint8_t status = static_cast<uint8_t>(0xb0u | channel);
        add(status, 99u, static_cast<uint8_t>((id >> 7u) & 0x7fu));
        add(status, 98u, static_cast<uint8_t>(id & 0x7fu));
        add(status, 6u, static_cast<uint8_t>((value >> 7u) & 0x7fu));
        add(status, 38u, static_cast<uint8_t>(value & 0x7fu));
    }
};

struct OutputEventList {
    std::vector<clap_event_param_value_t> params;
    clap_output_events_t output {
        this,
        [](const clap_output_events_t* list,
            const clap_event_header_t* event) -> bool {
            auto* self = static_cast<OutputEventList*>(list->ctx);
            if (!self || !event) return false;
            if (event->space_id == CLAP_CORE_EVENT_SPACE_ID
                && event->type == CLAP_EVENT_PARAM_VALUE
                && event->size >= sizeof(clap_event_param_value_t)) {
                self->params.push_back(
                    *reinterpret_cast<const clap_event_param_value_t*>(event));
            }
            return true;
        },
    };

    void clear() { params.clear(); }

    const clap_event_param_value_t* last(clap_id id) const
    {
        for (auto it = params.rbegin(); it != params.rend(); ++it) {
            if (it->param_id == id) return &*it;
        }
        return nullptr;
    }
};

struct MemoryState {
    std::vector<uint8_t> bytes;
    size_t offset = 0u;
};

int64_t stateWrite(const clap_ostream_t* stream,
    const void* source, uint64_t requested)
{
    auto* state = static_cast<MemoryState*>(stream->ctx);
    if (!state || (!source && requested > 0u)) return -1;
    const size_t count = std::min<size_t>(requested, 17u);
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
        static_cast<size_t>(requested), 13u });
    if (count > 0u) {
        std::memcpy(destination, state->bytes.data() + state->offset, count);
        state->offset += count;
    }
    return static_cast<int64_t>(count);
}

struct AudioBlock {
    std::array<std::array<float, kFrames>, kChannels> output {};
    std::array<float*, kChannels> pointers {};
    clap_audio_buffer_t buffer {};

    AudioBlock()
    {
        for (uint32_t channel = 0u; channel < kChannels; ++channel) {
            pointers[channel] = output[channel].data();
        }
        buffer.data32 = pointers.data();
        buffer.channel_count = kChannels;
    }

    void clear()
    {
        for (auto& channel : output) channel.fill(0.0f);
    }
};

} // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "usage: s3g_no_input_mixer_clap_smoke "
                     "<bundle-or-binary>\n";
        return 2;
    }
    const auto binary = resolveBinary(argv[1]);
    if (binary.empty()) {
        std::cerr << "Could not resolve No Input Mixer binary\n";
        return 1;
    }
    void* library = dlopen(binary.c_str(), RTLD_LOCAL | RTLD_NOW);
    if (!library) {
        std::cerr << "Could not load No Input Mixer: " << dlerror() << "\n";
        return 1;
    }
    const auto* entry = static_cast<const clap_plugin_entry_t*>(
        dlsym(library, "clap_entry"));
    bool ok = entry && entry->init(binary.c_str());

    clap_host_t host {};
    host.clap_version = CLAP_VERSION_INIT;
    host.name = "No Input Mixer smoke";
    host.vendor = "s3g";
    host.url = "https://github.com/s3g/s3g-dsp";
    host.version = "1";
    host.get_extension = hostGetExtension;
    host.request_restart = hostRequest;
    host.request_process = hostRequest;
    host.request_callback = hostRequest;

    const auto* factory = ok ? static_cast<const clap_plugin_factory_t*>(
        entry->get_factory(CLAP_PLUGIN_FACTORY_ID)) : nullptr;
    const clap_plugin_t* plugin = factory ? factory->create_plugin(
        factory, &host, "org.s3g.s3g-dsp.no-input-mixer-8ch") : nullptr;
    ok = ok && plugin && plugin->init(plugin);
    const auto* ports = ok ? static_cast<const clap_plugin_audio_ports_t*>(
        plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS)) : nullptr;
    const auto* notePorts = ok ? static_cast<const clap_plugin_note_ports_t*>(
        plugin->get_extension(plugin, CLAP_EXT_NOTE_PORTS)) : nullptr;
    const auto* params = ok ? static_cast<const clap_plugin_params_t*>(
        plugin->get_extension(plugin, CLAP_EXT_PARAMS)) : nullptr;
    const auto* state = ok ? static_cast<const clap_plugin_state_t*>(
        plugin->get_extension(plugin, CLAP_EXT_STATE)) : nullptr;
    clap_audio_port_info_t outputPort {};
    clap_note_port_info_t midiInputPort {};
    ok = ok && ports && notePorts && params && state
        && ports->count(plugin, true) == 0u
        && ports->count(plugin, false) == 1u
        && ports->get(plugin, 0u, false, &outputPort)
        && outputPort.channel_count == kChannels
        && notePorts->count(plugin, true) == 1u
        && notePorts->count(plugin, false) == 0u
        && notePorts->get(plugin, 0u, true, &midiInputPort)
        && (midiInputPort.supported_dialects & CLAP_NOTE_DIALECT_MIDI) != 0u
        && midiInputPort.preferred_dialect == CLAP_NOTE_DIALECT_MIDI
        && params->count(plugin) == kParamCount
        && plugin->activate(plugin, 48000.0, kFrames, kFrames)
        && plugin->start_processing(plugin);

    clap_param_info_t reactDepthInfo {};
    clap_param_info_t reactModeInfo {};
    bool foundReactDepth = false;
    bool foundReactMode = false;
    for (uint32_t index = 0u; ok && index < params->count(plugin); ++index) {
        clap_param_info_t info {};
        if (!params->get_info(plugin, index, &info)) {
            ok = false;
            break;
        }
        if (info.id == kReactDepthParam) {
            reactDepthInfo = info;
            foundReactDepth = true;
        }
        if (info.id == kReactModeParam) {
            reactModeInfo = info;
            foundReactMode = true;
        }
    }
    ok = ok && foundReactDepth && foundReactMode
        && (reactDepthInfo.flags & CLAP_PARAM_IS_MODULATABLE) != 0u
        && (reactDepthInfo.flags & CLAP_PARAM_IS_STEPPED) == 0u
        && (reactModeInfo.flags & CLAP_PARAM_IS_STEPPED) != 0u
        && (reactModeInfo.flags & CLAP_PARAM_IS_MODULATABLE) == 0u;
    if (!ok) std::cerr << "failed: setup/parameter metadata\n";

    for (uint32_t index = 0u; ok && index < params->count(plugin); ++index) {
        clap_param_info_t info {};
        if (!params->get_info(plugin, index, &info)) {
            ok = false;
            break;
        }
        const std::array<double, 3u> probes {{
            info.min_value, info.default_value, info.max_value,
        }};
        for (const double probe : probes) {
            char firstText[128] {};
            char secondText[128] {};
            double parsed = 0.0;
            if (!params->value_to_text(plugin, info.id, probe,
                    firstText, sizeof(firstText))
                || !params->text_to_value(plugin, info.id, firstText,
                    &parsed)
                || !params->value_to_text(plugin, info.id, parsed,
                    secondText, sizeof(secondText))
                || std::strcmp(firstText, secondText) != 0) {
                std::cerr << "failed: parameter text round-trip for id "
                    << info.id << " ('" << info.name << "'): '"
                    << firstText << "' -> " << parsed << " -> '"
                    << secondText << "'\n";
                ok = false;
                break;
            }
        }
    }

    if (ok) {
        std::atomic<bool> resetReturned { false };
        std::thread audioThread([&]() {
            plugin->reset(plugin);
            resetReturned.store(true, std::memory_order_release);
        });
        audioThread.join();
        ok = resetReturned.load(std::memory_order_acquire);
    }

    MemoryState saved;
    clap_ostream_t outputStream { &saved, stateWrite };
    ok = ok && state->save(plugin, &outputStream) && !saved.bytes.empty();
    if (!ok) std::cerr << "failed: current state save\n";

    Version4State versionFour;
    versionFour.params.feedback = 0.71f;
    versionFour.params.matrix[0] = 0.88f;
    versionFour.params.lanes[0].body = 0.67f;
    versionFour.params.lanes[0].auxSend[0] = 0.62f;
    versionFour.auxMute[0] = 1u;
    versionFour.behavior.behavior = 4u;
    MemoryState versionFourMemory;
    const auto* versionFourBytes = reinterpret_cast<const uint8_t*>(
        &versionFour);
    versionFourMemory.bytes.assign(versionFourBytes,
        versionFourBytes + sizeof(versionFour));
    clap_istream_t versionFourStream { &versionFourMemory, stateRead };
    double versionFourValue = 0.0;
    ok = ok && state->load(plugin, &versionFourStream)
        && params->get_value(plugin, 5u, &versionFourValue)
        && std::abs(versionFourValue - 0.71) < 1.0e-6
        && params->get_value(plugin, 100u, &versionFourValue)
        && std::abs(versionFourValue - 0.88) < 1.0e-6
        && params->get_value(plugin, kLaneOneAuxAParam, &versionFourValue)
        && std::abs(versionFourValue - 0.62) < 1.0e-6
        && params->get_value(plugin, kBehaviorParam, &versionFourValue)
        && versionFourValue == 4.0
        && params->get_value(plugin, kReactModeParam, &versionFourValue)
        && versionFourValue == 0.0
        && params->get_value(plugin, kLaneOneTuneParam, &versionFourValue)
        && std::abs(versionFourValue - 45.0) < 1.0e-6
        && params->get_value(plugin, kLaneOneAuxReturnAParam,
            &versionFourValue)
        && std::abs(versionFourValue - 0.42) < 1.0e-6;
    if (!ok) std::cerr << "failed: v4 state migration\n";
    saved.offset = 0u;
    clap_istream_t postV4Restore { &saved, stateRead };
    ok = ok && state->load(plugin, &postV4Restore);

    EventList matrixChange;
    matrixChange.add(kFirstMatrixParam, 0.0);
    if (ok) params->flush(plugin, &matrixChange.input, nullptr);
    double matrixValue = -1.0;
    ok = ok && params->get_value(plugin, kFirstMatrixParam, &matrixValue)
        && matrixValue == 0.0;
    saved.offset = 0u;
    clap_istream_t inputStream { &saved, stateRead };
    ok = ok && state->load(plugin, &inputStream)
        && params->get_value(plugin, kFirstMatrixParam, &matrixValue)
        && std::abs(matrixValue - 0.94) < 1.0e-6;
    if (!ok) std::cerr << "failed: current state restore\n";

    LegacyState legacy;
    legacy.params.feedback = 0.73f;
    legacy.params.matrix[0] = -0.44f;
    legacy.params.lanes[0].body = 0.61f;
    MemoryState legacyMemory;
    const auto* legacyBytes = reinterpret_cast<const uint8_t*>(&legacy);
    legacyMemory.bytes.assign(legacyBytes, legacyBytes + sizeof(legacy));
    clap_istream_t legacyStream { &legacyMemory, stateRead };
    double migrated = 0.0;
    ok = ok && state->load(plugin, &legacyStream)
        && params->get_value(plugin, 5u, &migrated)
        && std::abs(migrated - 0.73) < 1.0e-6
        && params->get_value(plugin, 100u, &migrated)
        && std::abs(migrated + 0.44) < 1.0e-6
        && params->get_value(plugin, 1000u, &migrated)
        && std::abs(migrated - 0.61) < 1.0e-6
        && params->get_value(plugin, 11u, &migrated)
        && std::abs(migrated - 0.28) < 1.0e-6
        && params->get_value(plugin, kLaneOneAuxAParam, &migrated)
        && std::abs(migrated - 0.08) < 1.0e-6;
    if (!ok) std::cerr << "failed: v1 state migration\n";
    saved.offset = 0u;
    clap_istream_t restoredStream { &saved, stateRead };
    ok = ok && state->load(plugin, &restoredStream);

    EventList muteChange;
    muteChange.add(kAuxAMuteParam, 1.0);
    muteChange.add(kAuxBMuteParam, 1.0);
    if (ok) params->flush(plugin, &muteChange.input, nullptr);
    double muteValue = 0.0;
    ok = ok && params->get_value(plugin, kAuxAMuteParam, &muteValue)
        && muteValue == 1.0
        && params->get_value(plugin, kAuxBMuteParam, &muteValue)
        && muteValue == 1.0;
    if (!ok) std::cerr << "failed: aux mute state\n";
    MemoryState mutedState;
    clap_ostream_t mutedOutput { &mutedState, stateWrite };
    ok = ok && state->save(plugin, &mutedOutput);
    EventList unmuteChange;
    unmuteChange.add(kAuxAMuteParam, 0.0);
    unmuteChange.add(kAuxBMuteParam, 0.0);
    if (ok) params->flush(plugin, &unmuteChange.input, nullptr);
    clap_istream_t mutedInput { &mutedState, stateRead };
    ok = ok && state->load(plugin, &mutedInput)
        && params->get_value(plugin, kAuxAMuteParam, &muteValue)
        && muteValue == 1.0
        && params->get_value(plugin, kAuxBMuteParam, &muteValue)
        && muteValue == 1.0;

    EventList hybridChange;
    hybridChange.add(kMotionShapeParam, 3.0);
    hybridChange.add(kAuxATypeParam, 1.0);
    hybridChange.add(kLaneOneAuxAParam, 0.72);
    hybridChange.add(kBehaviorParam, 3.0);
    hybridChange.add(kEventRateParam, 0.82);
    hybridChange.add(kEventChokeParam, 1.0);
    hybridChange.add(kAuxABiasParam, -0.63);
    hybridChange.add(kAuxBBiasParam, 0.41);
    hybridChange.add(kReactModeParam, 1.0);
    hybridChange.add(kReactDepthParam, 0.78);
    hybridChange.add(kClockSyncParam, 1.0);
    hybridChange.add(kFieldDivisionParam, 8.0);
    hybridChange.add(kSurfaceXParam, 0.19);
    hybridChange.add(kLaneOneTuneParam, 57.25);
    hybridChange.add(kLaneOnePitchLockParam, 1.0);
    hybridChange.add(kLaneOneAuxTapAParam, 3.0);
    hybridChange.add(kLaneOneAuxReturnAParam, -0.62);
    if (ok) params->flush(plugin, &hybridChange.input, nullptr);
    double hybridValue = 0.0;
    ok = ok && params->get_value(plugin, kMotionShapeParam, &hybridValue)
        && hybridValue == 3.0
        && params->get_value(plugin, kAuxATypeParam, &hybridValue)
        && hybridValue == 1.0
        && params->get_value(plugin, kLaneOneAuxAParam, &hybridValue)
        && std::abs(hybridValue - 0.72) < 1.0e-6
        && params->get_value(plugin, kBehaviorParam, &hybridValue)
        && hybridValue == 3.0
        && params->get_value(plugin, kEventRateParam, &hybridValue)
        && std::abs(hybridValue - 0.82) < 1.0e-6
        && params->get_value(plugin, kEventChokeParam, &hybridValue)
        && hybridValue == 1.0
        && params->get_value(plugin, kAuxABiasParam, &hybridValue)
        && std::abs(hybridValue + 0.63) < 1.0e-6
        && params->get_value(plugin, kAuxBBiasParam, &hybridValue)
        && std::abs(hybridValue - 0.41) < 1.0e-6
        && params->get_value(plugin, kReactModeParam, &hybridValue)
        && hybridValue == 1.0
        && params->get_value(plugin, kReactDepthParam, &hybridValue)
        && std::abs(hybridValue - 0.78) < 1.0e-6
        && params->get_value(plugin, kClockSyncParam, &hybridValue)
        && hybridValue == 1.0
        && params->get_value(plugin, kFieldDivisionParam, &hybridValue)
        && hybridValue == 8.0
        && params->get_value(plugin, kSurfaceXParam, &hybridValue)
        && std::abs(hybridValue - 0.19) < 1.0e-6
        && params->get_value(plugin, kLaneOneTuneParam, &hybridValue)
        && std::abs(hybridValue - 57.25) < 1.0e-6
        && params->get_value(plugin, kLaneOnePitchLockParam, &hybridValue)
        && hybridValue == 1.0
        && params->get_value(plugin, kLaneOneAuxTapAParam, &hybridValue)
        && hybridValue == 3.0
        && params->get_value(plugin, kLaneOneAuxReturnAParam, &hybridValue)
        && std::abs(hybridValue + 0.62) < 1.0e-6;
    if (!ok) std::cerr << "failed: new parameter values\n";
    char processorName[32] {};
    ok = ok && params->value_to_text(plugin, kAuxATypeParam, 1.0,
            processorName, sizeof(processorName))
        && std::strcmp(processorName, "WOOL") == 0;
    ok = ok && params->value_to_text(plugin, kAuxATypeParam, 14.0,
            processorName, sizeof(processorName))
        && std::strcmp(processorName, "VOID") == 0
        && params->value_to_text(plugin, kAuxATypeParam, 22.0,
            processorName, sizeof(processorName))
        && std::strcmp(processorName, "OCT STACK") == 0
        && params->value_to_text(plugin, kBehaviorParam, 3.0,
            processorName, sizeof(processorName))
        && std::strcmp(processorName, "BURST") == 0
        && params->value_to_text(plugin, kAuxABiasParam, -0.63,
            processorName, sizeof(processorName))
        && std::strcmp(processorName, "-0.63") == 0
        && params->value_to_text(plugin, kReactModeParam, 1.0,
            processorName, sizeof(processorName))
        && std::strcmp(processorName, "FOLLOW") == 0
        && params->value_to_text(plugin, kLaneOneAuxTapAParam, 3.0,
            processorName, sizeof(processorName))
        && std::strcmp(processorName, "POST INSERT") == 0;
    if (!ok) std::cerr << "failed: parameter text\n";

    AudioBlock audio;
    std::array<double, kChannels> energy {};
    std::array<double, kChannels> difference {};
    clap_process_t process {};
    process.frames_count = kFrames;
    process.audio_outputs = &audio.buffer;
    process.audio_outputs_count = 1u;

    OutputEventList midiOutput;
    MidiEventList feedbackNrpn;
    constexpr uint16_t kHalf14Bit = 8192u;
    feedbackNrpn.addNrpn(kFeedbackParam, kHalf14Bit);
    process.in_events = &feedbackNrpn.input;
    process.out_events = &midiOutput.output;
    audio.clear();
    ok = ok && plugin->process(plugin, &process) == CLAP_PROCESS_CONTINUE;
    const double expectedFeedback = 1.25
        * static_cast<double>(kHalf14Bit) / 16383.0;
    double midiValue = 0.0;
    const auto* feedbackOutput = midiOutput.last(kFeedbackParam);
    ok = ok && params->get_value(plugin, kFeedbackParam, &midiValue)
        && std::abs(midiValue - expectedFeedback) < 1.0e-6
        && feedbackOutput
        && std::abs(feedbackOutput->value - expectedFeedback) < 1.0e-6
        && (feedbackOutput->header.flags & CLAP_EVENT_IS_LIVE) != 0u
        && (feedbackOutput->header.flags & CLAP_EVENT_DONT_RECORD) != 0u;
    if (!ok) std::cerr << "failed: MIDI NRPN parameter mapping\n";

    midiOutput.clear();
    MidiEventList wrongChannelNrpn;
    wrongChannelNrpn.addNrpn(kFeedbackParam, 16383u, 14u);
    process.in_events = &wrongChannelNrpn.input;
    audio.clear();
    ok = ok && plugin->process(plugin, &process) == CLAP_PROCESS_CONTINUE
        && params->get_value(plugin, kFeedbackParam, &midiValue)
        && std::abs(midiValue - expectedFeedback) < 1.0e-6
        && midiOutput.params.empty();
    if (!ok) std::cerr << "failed: MIDI control channel isolation\n";

    midiOutput.clear();
    MidiEventList scaledNrpn;
    scaledNrpn.addNrpn(kQualityParam, 16383u);
    scaledNrpn.addNrpn(kLaneOneMidFrequencyParam, kHalf14Bit);
    process.in_events = &scaledNrpn.input;
    audio.clear();
    ok = ok && plugin->process(plugin, &process) == CLAP_PROCESS_CONTINUE;
    const double expectedMidFrequency = 80.0 * std::pow(
        8000.0 / 80.0,
        static_cast<double>(kHalf14Bit) / 16383.0);
    ok = ok && params->get_value(plugin, kQualityParam, &midiValue)
        && midiValue == 2.0
        && params->get_value(plugin, kLaneOneMidFrequencyParam, &midiValue)
        && std::abs(midiValue - expectedMidFrequency) < 1.0e-3;
    if (!ok) std::cerr << "failed: MIDI stepped/logarithmic scaling\n";

    double muteBefore = 0.0;
    ok = ok && params->get_value(plugin, kLaneOneMuteParam, &muteBefore);
    midiOutput.clear();
    MidiEventList muteCommand;
    muteCommand.add(0x9fu, 32u, 127u);
    process.in_events = &muteCommand.input;
    audio.clear();
    ok = ok && plugin->process(plugin, &process) == CLAP_PROCESS_CONTINUE;
    const auto* muteOutput = midiOutput.last(kLaneOneMuteParam);
    ok = ok && params->get_value(plugin, kLaneOneMuteParam, &midiValue)
        && midiValue == (muteBefore >= 0.5 ? 0.0 : 1.0)
        && muteOutput && muteOutput->value == midiValue;
    if (!ok) std::cerr << "failed: MIDI push command mapping\n";

    double pitchLockBefore = 0.0;
    ok = ok && params->get_value(
        plugin, kLaneOnePitchLockParam, &pitchLockBefore);
    midiOutput.clear();
    MidiEventList pitchLockCommand;
    pitchLockCommand.add(0x9fu, 80u, 127u);
    process.in_events = &pitchLockCommand.input;
    audio.clear();
    ok = ok && plugin->process(plugin, &process) == CLAP_PROCESS_CONTINUE;
    const auto* pitchLockOutput = midiOutput.last(kLaneOnePitchLockParam);
    ok = ok && params->get_value(
        plugin, kLaneOnePitchLockParam, &midiValue)
        && midiValue == (pitchLockBefore >= 0.5 ? 0.0 : 1.0)
        && pitchLockOutput && pitchLockOutput->value == midiValue;
    if (!ok) std::cerr << "failed: MIDI pitch-lock command mapping\n";

    midiOutput.clear();
    MidiEventList randomEnergyCommands;
    randomEnergyCommands.add(0x9fu, 125u, 127u);
    randomEnergyCommands.add(0x9fu, 122u, 127u);
    randomEnergyCommands.add(0x9fu, 126u, 127u);
    process.in_events = &randomEnergyCommands.input;
    audio.clear();
    ok = ok && plugin->process(plugin, &process) == CLAP_PROCESS_CONTINUE
        && midiOutput.params.size() == 3u * kParamCount;
    if (!ok) std::cerr << "failed: MIDI random-energy command mapping\n";

    saved.offset = 0u;
    clap_istream_t postMidiRestore { &saved, stateRead };
    ok = ok && state->load(plugin, &postMidiRestore);
    process.in_events = nullptr;
    process.out_events = nullptr;

    for (uint32_t block = 0u; ok && block < 220u; ++block) {
        audio.clear();
        ok = plugin->process(plugin, &process) == CLAP_PROCESS_CONTINUE;
        if (block < 24u) continue;
        for (uint32_t channel = 0u; channel < kChannels; ++channel) {
            for (uint32_t frame = 0u; frame < kFrames; ++frame) {
                const float value = audio.output[channel][frame];
                ok = ok && std::isfinite(value) && std::abs(value) <= 1.01f;
                energy[channel] += static_cast<double>(value) * value;
                if (channel > 0u) {
                    difference[channel] += std::abs(static_cast<double>(
                        value - audio.output[0u][frame]));
                }
            }
        }
    }
    for (uint32_t channel = 0u; ok && channel < kChannels; ++channel) {
        ok = energy[channel] > 1.0e-8;
        if (channel > 0u) ok = ok && difference[channel] > 1.0e-4;
    }
    if (!ok) std::cerr << "failed: audio generation\n";

    saved.offset = 0u;
    clap_istream_t timedRestore { &saved, stateRead };
    ok = ok && state->load(plugin, &timedRestore);
    process.in_events = nullptr;
    for (uint32_t block = 0u; ok && block < 40u; ++block) {
        audio.clear();
        ok = plugin->process(plugin, &process) == CLAP_PROCESS_CONTINUE;
    }
    EventList timedOutput;
    timedOutput.add(kOutputParam, -60.0, 0u);
    timedOutput.add(kOutputParam, 6.0, kFrames / 2u);
    process.in_events = &timedOutput.input;
    audio.clear();
    ok = ok && plugin->process(plugin, &process) == CLAP_PROCESS_CONTINUE;
    double firstHalf = 0.0;
    double secondHalf = 0.0;
    for (uint32_t channel = 0u; channel < kChannels; ++channel) {
        for (uint32_t frame = 0u; frame < kFrames / 2u; ++frame)
            firstHalf += std::abs(audio.output[channel][frame]);
        for (uint32_t frame = kFrames / 2u; frame < kFrames; ++frame)
            secondHalf += std::abs(audio.output[channel][frame]);
    }
    double finalOutputValue = -999.0;
    ok = ok && params->get_value(plugin, kOutputParam, &finalOutputValue)
        && std::abs(finalOutputValue - 6.0) < 1.0e-9
        && secondHalf > firstHalf * 20.0;
    if (!ok) std::cerr << "failed: sample-accurate event ratio "
        << firstHalf << " / " << secondHalf << " final "
        << finalOutputValue << "\n";
    process.in_events = nullptr;

    if (plugin) {
        plugin->stop_processing(plugin);
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
    }
    if (entry) entry->deinit();
    dlclose(library);

    if (!ok) {
        std::cerr << "No Input Mixer CLAP smoke failed\n";
        return 1;
    }
    std::cout << "No Input Mixer CLAP smoke passed\n";
    return 0;
}
