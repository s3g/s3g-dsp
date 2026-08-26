#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/note-ports.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>

#include "../plugins/common/s3g_sample_storage.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <thread>
#include <vector>

namespace {

constexpr const char* kPluginId =
    "org.s3g.s3g-dsp.sample-rings-8";
constexpr uint32_t kChannelCount = 8u;
constexpr uint32_t kFrames = 256u;
constexpr uint32_t kParamCount = 91u;
constexpr uint32_t kStateMagic = 0x534c4d58u;
constexpr uint32_t kStateVersion = 6u;
constexpr uint32_t kPreviousStateVersion = 5u;
constexpr uint32_t kSourceCount = 4u;
constexpr uint32_t kHeadCount = 8u;

constexpr clap_id kPlaybackRateParamId = 1u;
constexpr clap_id kRelationshipAmountParamId = 4u;
constexpr clap_id kCenterParamId = 6u;
constexpr clap_id kOutputGainParamId = 13u;
constexpr clap_id kPlayingParamId = 16u;
constexpr clap_id kCaptureGateParamId = 18u;
constexpr clap_id kInputWidthParamId = 20u;
constexpr clap_id kMonitorParamId = 22u;
constexpr clap_id kRadialReverseParamId = 34u;
constexpr clap_id kAngularReverseParamId = 35u;
constexpr clap_id kManualPhaseParamBase = 220u;
constexpr clap_id kManualRateParamBase = 240u;

struct PreviousSavedState {
    uint32_t magic = kStateMagic;
    uint32_t version = kPreviousStateVersion;
    std::array<double, 35u> globals {};
    std::array<std::array<double, 8u>, kSourceCount> slots {};
    std::array<double, kHeadCount> manualRings {};
    std::array<double, kHeadCount> manualPhases {};
    std::array<double, kHeadCount> manualRates {};
    std::array<std::array<char, 1024u>, kSourceCount> paths {};
    std::array<uint8_t, kSourceCount> embedded {};
    std::array<uint8_t, kSourceCount> channels {};
    std::array<double, kSourceCount> sampleRates {};
    std::array<uint32_t, kSourceCount> frames {};
};

struct CurrentSavedState {
    uint32_t magic = kStateMagic;
    uint32_t version = kStateVersion;
    std::array<double, 35u> globals {};
    std::array<std::array<double, 8u>, kSourceCount> slots {};
    std::array<double, kHeadCount> manualRings {};
    std::array<double, kHeadCount> manualPhases {};
    std::array<double, kHeadCount> manualRates {};
    std::array<std::array<char, 1024u>, kSourceCount> paths {};
    std::array<uint8_t, kSourceCount> embedded {};
    std::array<uint8_t, kSourceCount> channels {};
    std::array<double, kSourceCount> sampleRates {};
    std::array<uint32_t, kSourceCount> frames {};
    uint8_t storageMode = 0u;
    std::array<uint8_t, 7u> reserved {};
};

static_assert(offsetof(CurrentSavedState, storageMode)
    == sizeof(PreviousSavedState));

struct HostContext {
    clap_host_t host {};
    s3g::sample_storage::ReaperHostBridge reaperBridge {};
    std::atomic<uint32_t> callbackRequests { 0u };
    uint32_t projectFileRegistrations = 0u;
    void* project = reinterpret_cast<void*>(uintptr_t { 0x1234u });
    void* fxDsp = reinterpret_cast<void*>(uintptr_t { 0x5678u });
    std::string projectFilePath;
    std::string mediaDirectory;
};

HostContext* activeHostContext = nullptr;

HostContext* hostContext(const clap_host_t* host)
{ return static_cast<HostContext*>(host->host_data); }

void* fakeGetReaperContext(const clap_host_t*, int selector)
{
    if (!activeHostContext) return nullptr;
    return selector == 3 ? activeHostContext->project
        : selector == 4 ? activeHostContext->fxDsp : nullptr;
}

void* fakeEnumProjects(int index, char* path, int capacity)
{
    if (!activeHostContext || index != 0) return nullptr;
    std::snprintf(path, static_cast<std::size_t>(capacity), "%s",
        activeHostContext->projectFilePath.c_str());
    return activeHostContext->project;
}

void fakeGetProjectPathEx(void* project, char* path, int capacity)
{
    if (!activeHostContext || project != activeHostContext->project) return;
    std::snprintf(path, static_cast<std::size_t>(capacity), "%s",
        activeHostContext->mediaDirectory.c_str());
}

int fakeRegisterObject(const char* name, void*)
{
    if (!activeHostContext || !name) return 0;
    if (std::strcmp(name, "file_in_project_ex2") == 0)
        ++activeHostContext->projectFileRegistrations;
    return 1;
}

void* fakeGetFunction(const char* name)
{
    if (!name) return nullptr;
    if (std::strcmp(name, "clap_get_reaper_context") == 0)
        return reinterpret_cast<void*>(&fakeGetReaperContext);
    if (std::strcmp(name, "EnumProjects") == 0)
        return reinterpret_cast<void*>(&fakeEnumProjects);
    if (std::strcmp(name, "GetProjectPathEx") == 0)
        return reinterpret_cast<void*>(&fakeGetProjectPathEx);
    if (std::strcmp(name, "plugin_register") == 0)
        return reinterpret_cast<void*>(&fakeRegisterObject);
    return nullptr;
}

const void* hostGetExtension(const clap_host_t* host, const char* id)
{
    if (!host || !id || std::strcmp(id, "cockos.reaper_extension") != 0)
        return nullptr;
    return &hostContext(host)->reaperBridge;
}
void hostRequestRestart(const clap_host_t*) {}
void hostRequestProcess(const clap_host_t*) {}
void hostRequestCallback(const clap_host_t* host)
{ hostContext(host)->callbackRequests.fetch_add(1u); }

bool writeTestWave(const std::filesystem::path& path)
{
    constexpr uint32_t sampleRate = 48000u;
    constexpr uint16_t channels = 2u;
    constexpr uint16_t bits = 16u;
    constexpr uint32_t frames = 512u;
    constexpr uint32_t dataBytes = frames * channels * (bits / 8u);
    std::ofstream output(path, std::ios::binary);
    if (!output) return false;
    const auto write16 = [&](uint16_t value) {
        const char bytes[] { static_cast<char>(value & 0xffu),
            static_cast<char>((value >> 8u) & 0xffu) };
        output.write(bytes, sizeof(bytes));
    };
    const auto write32 = [&](uint32_t value) {
        const char bytes[] { static_cast<char>(value & 0xffu),
            static_cast<char>((value >> 8u) & 0xffu),
            static_cast<char>((value >> 16u) & 0xffu),
            static_cast<char>((value >> 24u) & 0xffu) };
        output.write(bytes, sizeof(bytes));
    };
    output.write("RIFF", 4); write32(36u + dataBytes);
    output.write("WAVEfmt ", 8); write32(16u); write16(1u);
    write16(channels); write32(sampleRate);
    write32(sampleRate * channels * (bits / 8u));
    write16(channels * (bits / 8u)); write16(bits);
    output.write("data", 4); write32(dataBytes);
    for (uint32_t frame = 0u; frame < frames; ++frame) {
        const auto sample = static_cast<int16_t>(std::lround(12000.0
            * std::sin(6.28318530717958647692 * frame / 53.0)));
        write16(static_cast<uint16_t>(sample));
        write16(static_cast<uint16_t>(-sample));
    }
    return static_cast<bool>(output);
}

struct EventList {
    std::array<clap_event_param_value_t, 16u> parameters {};
    std::array<const clap_event_header_t*, 16u> events {};
    uint32_t count = 0u;
    clap_input_events_t input {
        this,
        [](const clap_input_events_t* list) -> uint32_t {
            return static_cast<const EventList*>(list->ctx)->count;
        },
        [](const clap_input_events_t* list, uint32_t index)
            -> const clap_event_header_t* {
            const auto* source = static_cast<const EventList*>(list->ctx);
            return index < source->count ? source->events[index] : nullptr;
        },
    };

    void add(clap_id id, double value)
    {
        auto& event = parameters[count];
        event = {};
        event.header.size = sizeof(event);
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_PARAM_VALUE;
        event.param_id = id;
        event.note_id = -1;
        event.port_index = -1;
        event.channel = -1;
        event.key = -1;
        event.value = value;
        events[count] = &event.header;
        ++count;
    }
};

struct MemoryState {
    std::vector<uint8_t> bytes;
    std::size_t position = 0u;
};

int64_t stateWrite(const clap_ostream_t* stream, const void* source,
    uint64_t requested)
{
    auto* state = static_cast<MemoryState*>(stream->ctx);
    const std::size_t count = std::min<std::size_t>(
        static_cast<std::size_t>(requested), 11u);
    const auto* bytes = static_cast<const uint8_t*>(source);
    state->bytes.insert(state->bytes.end(), bytes, bytes + count);
    return static_cast<int64_t>(count);
}

int64_t stateRead(const clap_istream_t* stream, void* destination,
    uint64_t requested)
{
    auto* state = static_cast<MemoryState*>(stream->ctx);
    const std::size_t available = state->position < state->bytes.size()
        ? state->bytes.size() - state->position : 0u;
    const std::size_t count = std::min<std::size_t>({ available,
        static_cast<std::size_t>(requested), 7u });
    if (count > 0u) {
        std::memcpy(destination, state->bytes.data() + state->position,
            count);
        state->position += count;
    }
    return static_cast<int64_t>(count);
}

std::filesystem::path resolveBinary(const std::filesystem::path& supplied)
{
    if (std::filesystem::is_regular_file(supplied)) return supplied;
#if defined(__APPLE__)
    if (std::filesystem::is_directory(supplied)) {
        const auto directory = supplied / "Contents" / "MacOS";
        for (const auto& entry : std::filesystem::directory_iterator(directory))
            if (entry.is_regular_file()) return entry.path();
    }
#endif
    return {};
}

bool check(bool condition, const char* message)
{
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}

bool hasFeature(const clap_plugin_descriptor_t* descriptor,
    const char* expected)
{
    if (!descriptor || !descriptor->features) return false;
    for (const char* const* feature = descriptor->features; *feature; ++feature)
        if (std::strcmp(*feature, expected) == 0) return true;
    return false;
}

double energy(const std::array<std::array<float, kFrames>, kChannelCount>& data)
{
    double result = 0.0;
    for (const auto& channel : data)
        for (float value : channel) result += static_cast<double>(value) * value;
    return result;
}

bool processBlock(const clap_plugin_t* plugin, EventList* eventList,
    bool feedInput,
    std::array<std::array<float, kFrames>, kChannelCount>& input,
    std::array<std::array<float, kFrames>, kChannelCount>& output,
    bool inPlace = false)
{
    constexpr double pi = 3.14159265358979323846;
    std::array<float*, kChannelCount> inputPointers {};
    std::array<float*, kChannelCount> outputPointers {};
    for (uint32_t channel = 0u; channel < kChannelCount; ++channel) {
        input[channel].fill(0.0f);
        output[channel].fill(0.0f);
        inputPointers[channel] = input[channel].data();
        outputPointers[channel] = inPlace ? input[channel].data()
            : output[channel].data();
    }
    if (feedInput) {
        for (uint32_t frame = 0u; frame < kFrames; ++frame) {
            input[0u][frame] = static_cast<float>(0.35 * std::sin(
                2.0 * pi * 173.0 * frame / 48000.0));
            input[1u][frame] = static_cast<float>(0.21 * std::sin(
                2.0 * pi * 257.0 * frame / 48000.0 + 0.3));
        }
    }
    clap_audio_buffer_t inputBuffer {};
    inputBuffer.data32 = inputPointers.data();
    inputBuffer.channel_count = kChannelCount;
    clap_audio_buffer_t outputBuffer {};
    outputBuffer.data32 = outputPointers.data();
    outputBuffer.channel_count = kChannelCount;
    clap_process_t process {};
    process.frames_count = kFrames;
    process.audio_inputs = &inputBuffer;
    process.audio_inputs_count = 1u;
    process.audio_outputs = &outputBuffer;
    process.audio_outputs_count = 1u;
    process.in_events = eventList ? &eventList->input : nullptr;
    if (plugin->process(plugin, &process) != CLAP_PROCESS_CONTINUE)
        return false;
    if (inPlace) output = input;
    for (const auto& channel : output)
        for (float value : channel)
            if (!std::isfinite(value)) return false;
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "usage: sample_rings_clap_smoke <bundle-or-binary>\n";
        return 2;
    }
    bool ok = true;
    const auto binary = resolveBinary(argv[1]);
    ok &= check(!binary.empty(), "could not resolve CLAP binary");
    void* handle = binary.empty() ? nullptr
        : dlopen(binary.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        const char* error = dlerror();
        if (error) std::cerr << "dlopen: " << error << '\n';
        return 1;
    }
    const auto* entry = static_cast<const clap_plugin_entry_t*>(
        dlsym(handle, "clap_entry"));
    ok &= check(entry && entry->init(nullptr), "CLAP entry init failed");
    const auto* factory = entry ? static_cast<const clap_plugin_factory_t*>(
        entry->get_factory(CLAP_PLUGIN_FACTORY_ID)) : nullptr;
    ok &= check(factory && factory->get_plugin_count(factory) == 1u,
        "factory contract failed");
    const auto* descriptor = factory
        ? factory->get_plugin_descriptor(factory, 0u) : nullptr;
    ok &= check(descriptor && std::strcmp(descriptor->id, kPluginId) == 0
        && std::strcmp(descriptor->name, "s3g Sample Rings 8") == 0,
        "identity contract failed");
    ok &= check(hasFeature(descriptor, CLAP_PLUGIN_FEATURE_AUDIO_EFFECT)
        && hasFeature(descriptor, CLAP_PLUGIN_FEATURE_INSTRUMENT)
        && hasFeature(descriptor, CLAP_PLUGIN_FEATURE_SAMPLER)
        && hasFeature(descriptor, CLAP_PLUGIN_FEATURE_SURROUND),
        "feature tags failed");

    const auto storageRoot = std::filesystem::temp_directory_path()
        / ("s3g-sample-rings-storage-" + std::to_string(
            std::chrono::high_resolution_clock::now()
                .time_since_epoch().count()));
    const auto mediaDirectory = storageRoot / "Media";
    const auto externalDirectory = storageRoot / "External";
    std::error_code filesystemError;
    std::filesystem::create_directories(mediaDirectory, filesystemError);
    std::filesystem::create_directories(externalDirectory, filesystemError);
    const auto externalWave = externalDirectory / "shared-source.wav";
    ok &= check(!filesystemError && writeTestWave(externalWave),
        "could not prepare project-storage source fixture");

    HostContext host;
    host.projectFilePath = (storageRoot / "matrix-project.rpp").string();
    host.mediaDirectory = mediaDirectory.string();
    host.reaperBridge.callerVersion = 1;
    host.reaperBridge.registerObject = fakeRegisterObject;
    host.reaperBridge.getFunction = fakeGetFunction;
    activeHostContext = &host;
    host.host.clap_version = CLAP_VERSION_INIT;
    host.host.host_data = &host;
    host.host.name = "s3g Sample Rings smoke";
    host.host.vendor = "s3g";
    host.host.url = "https://github.com/s3g/s3g-dsp";
    host.host.version = "1";
    host.host.get_extension = hostGetExtension;
    host.host.request_restart = hostRequestRestart;
    host.host.request_process = hostRequestProcess;
    host.host.request_callback = hostRequestCallback;
    const clap_plugin_t* plugin = factory
        ? factory->create_plugin(factory, &host.host, kPluginId) : nullptr;
    ok &= check(plugin && plugin->init(plugin), "plugin creation failed");
    if (!plugin) return 1;

    const auto* audioPorts = static_cast<const clap_plugin_audio_ports_t*>(
        plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS));
    clap_audio_port_info_t inputInfo {};
    clap_audio_port_info_t outputInfo {};
    ok &= check(audioPorts && audioPorts->count(plugin, true) == 1u
        && audioPorts->count(plugin, false) == 1u
        && audioPorts->get(plugin, 0u, true, &inputInfo)
        && audioPorts->get(plugin, 0u, false, &outputInfo)
        && inputInfo.channel_count == kChannelCount
        && outputInfo.channel_count == kChannelCount
        && inputInfo.in_place_pair == outputInfo.id
        && outputInfo.in_place_pair == inputInfo.id,
        "paired eight-channel audio port contract failed");

    const auto* notePorts = static_cast<const clap_plugin_note_ports_t*>(
        plugin->get_extension(plugin, CLAP_EXT_NOTE_PORTS));
    clap_note_port_info_t noteInfo {};
    ok &= check(notePorts && notePorts->count(plugin, true) == 1u
        && notePorts->count(plugin, false) == 0u
        && notePorts->get(plugin, 0u, true, &noteInfo),
        "note input contract failed");

    const auto* params = static_cast<const clap_plugin_params_t*>(
        plugin->get_extension(plugin, CLAP_EXT_PARAMS));
    ok &= check(params && params->count(plugin) == kParamCount,
        "parameter count failed");
    std::set<clap_id> ids;
    bool signedAmountMetadata = false;
    bool signedRateMetadata = false;
    bool headPivotMetadata = false;
    if (params) {
        for (uint32_t index = 0u; index < params->count(plugin); ++index) {
            clap_param_info_t info {};
            ok &= check(params->get_info(plugin, index, &info),
                "parameter metadata failed");
            ok &= check(ids.insert(info.id).second,
                "parameter IDs are not unique");
            if (info.id == kRelationshipAmountParamId)
                signedAmountMetadata = info.min_value == -1.0
                    && info.max_value == 1.0;
            if (info.id == kManualRateParamBase)
                signedRateMetadata = info.min_value == -4.0
                    && info.max_value == 4.0;
            if (info.id == kCenterParamId)
                headPivotMetadata = std::strcmp(info.name, "Head Pivot") == 0;
        }
    }
    ok &= check(signedAmountMetadata,
        "relationship amount is not bipolar");
    ok &= check(signedRateMetadata,
        "manual head rate is not signed");
    ok &= check(headPivotMetadata,
        "relationship center was not renamed Head Pivot");
    char display[64] {};
    double parsedPhase = 0.0;
    ok &= check(params
        && params->value_to_text(plugin, kManualPhaseParamBase, 0.75,
            display, sizeof(display))
        && std::strcmp(display, "-90.0 deg") == 0
        && params->text_to_value(plugin, kManualPhaseParamBase, "-90 deg",
            &parsedPhase)
        && std::abs(parsedPhase - 0.75) < 1.0e-9,
        "manual phase signed-degree conversion failed");
    ok &= check(params
        && params->value_to_text(plugin, kManualRateParamBase, -1.25,
            display, sizeof(display))
        && std::strcmp(display, "-1.250x") == 0,
        "manual rate signed display failed");

    const auto* state = static_cast<const clap_plugin_state_t*>(
        plugin->get_extension(plugin, CLAP_EXT_STATE));
    ok &= check(state != nullptr, "state extension missing");
    ok &= check(plugin->activate(plugin, 48000.0, 64u, kFrames),
        "activation failed");
    ok &= check(plugin->start_processing(plugin), "start processing failed");

    std::array<std::array<float, kFrames>, kChannelCount> input {};
    std::array<std::array<float, kFrames>, kChannelCount> output {};
    EventList captureStart;
    captureStart.add(kInputWidthParamId, 2.0);
    captureStart.add(kMonitorParamId, 1.0);
    captureStart.add(kCaptureGateParamId, 1.0);
    ok &= check(processBlock(plugin, &captureStart, true, input, output),
        "capture-start block failed");
    ok &= check(energy(output) > 0.0,
        "input monitor did not pass the upstream signal");
    for (uint32_t frame = 0u; frame < kFrames; ++frame) {
        ok &= check(std::abs(output[0u][frame] - input[0u][frame]) < 1.0e-6f
            && std::abs(output[1u][frame] - input[1u][frame]) < 1.0e-6f,
            "input monitor altered channel origin");
        if (!ok) break;
    }

    EventList captureStop;
    captureStop.add(kCaptureGateParamId, 0.0);
    captureStop.add(kMonitorParamId, 0.0);
    ok &= check(processBlock(plugin, &captureStop, false, input, output),
        "capture-stop block failed");
    ok &= check(host.callbackRequests.load() > 0u,
        "capture completion did not request a main-thread callback");
    plugin->on_main_thread(plugin);

    EventList inPlaceMonitor;
    inPlaceMonitor.add(kMonitorParamId, 1.0);
    inPlaceMonitor.add(kPlayingParamId, 0.0);
    ok &= check(processBlock(plugin, &inPlaceMonitor, true, input, output,
            true)
        && energy(output) > 0.0,
        "in-place input monitoring erased the upstream signal");
    EventList returnToLoop;
    returnToLoop.add(kMonitorParamId, 0.0);
    returnToLoop.add(kPlayingParamId, 1.0);
    ok &= check(processBlock(plugin, &returnToLoop, false, input, output),
        "return to loop monitoring failed");

    EventList loopPlayback;
    loopPlayback.add(kOutputGainParamId, 0.0);
    ok &= check(processBlock(plugin, &loopPlayback, false, input, output)
        && energy(output) > 0.0,
        "captured source did not play from the Sample Rings");

    EventList pause;
    pause.add(kPlayingParamId, 0.0);
    ok &= check(processBlock(plugin, &pause, false, input, output)
        && energy(output) == 0.0,
        "paused Sample Rings was not silent");

    EventList signedAngular;
    signedAngular.add(kRelationshipAmountParamId, -0.75);
    signedAngular.add(kManualRateParamBase, -1.25);
    ok &= check(processBlock(plugin, &signedAngular, false, input, output),
        "signed angular parameter block failed");

    MemoryState memory;
    clap_ostream_t outputStream { &memory, stateWrite };
    ok &= check(state && state->save(plugin, &outputStream)
        && memory.bytes.size() > 2048u,
        "embedded capture state save failed");
    CurrentSavedState stored {};
    if (memory.bytes.size() >= sizeof(stored))
        std::memcpy(&stored, memory.bytes.data(), sizeof(stored));
    ok &= check(stored.magic == kStateMagic
        && stored.version == kStateVersion && stored.storageMode == 0u
        && stored.embedded[0u] == 1u && stored.paths[0u][0u] == '\0',
        "new instance did not save as PROJECT with embedded live capture");
    EventList alter;
    alter.add(kPlaybackRateParamId, 4.0);
    alter.add(kPlayingParamId, 1.0);
    alter.add(kRadialReverseParamId, 1.0);
    alter.add(kAngularReverseParamId, 1.0);
    alter.add(kRelationshipAmountParamId, 0.5);
    alter.add(kManualRateParamBase, 1.0);
    ok &= check(processBlock(plugin, &alter, false, input, output),
        "parameter alteration block failed");
    clap_istream_t inputStream { &memory, stateRead };
    ok &= check(state && state->load(plugin, &inputStream),
        "embedded capture state load failed");
    double restoredRate = 0.0;
    double restoredPlaying = 1.0;
    double restoredRadialReverse = 1.0;
    double restoredAngularReverse = 1.0;
    double restoredRelationshipAmount = 0.0;
    double restoredManualRate = 0.0;
    ok &= check(params->get_value(plugin, kPlaybackRateParamId, &restoredRate)
        && params->get_value(plugin, kPlayingParamId, &restoredPlaying)
        && params->get_value(plugin, kRadialReverseParamId,
            &restoredRadialReverse)
        && params->get_value(plugin, kAngularReverseParamId,
            &restoredAngularReverse)
        && params->get_value(plugin, kRelationshipAmountParamId,
            &restoredRelationshipAmount)
        && params->get_value(plugin, kManualRateParamBase,
            &restoredManualRate)
        && std::abs(restoredRate - 1.0) < 1.0e-6
        && restoredPlaying == 0.0 && restoredRadialReverse == 0.0
        && restoredAngularReverse == 0.0
        && std::abs(restoredRelationshipAmount + 0.75) < 1.0e-6
        && std::abs(restoredManualRate + 1.25) < 1.0e-6,
        "parameter state did not round-trip");
    EventList resume;
    resume.add(kPlayingParamId, 1.0);
    ok &= check(processBlock(plugin, &resume, false, input, output)
        && energy(output) > 0.0,
        "restored embedded capture did not render");

    PreviousSavedState previous;
    std::memcpy(&previous, &stored, sizeof(previous));
    previous.version = kPreviousStateVersion;
    MemoryState previousMemory;
    const auto* previousBytes = reinterpret_cast<const uint8_t*>(&previous);
    previousMemory.bytes.assign(previousBytes,
        previousBytes + sizeof(previous));
    previousMemory.bytes.insert(previousMemory.bytes.end(),
        memory.bytes.begin() + static_cast<std::ptrdiff_t>(sizeof(stored)),
        memory.bytes.end());
    clap_istream_t previousInput { &previousMemory, stateRead };
    ok &= check(state && state->load(plugin, &previousInput),
        "version 5 capture state migration failed");
    MemoryState migratedMemory;
    clap_ostream_t migratedOutput { &migratedMemory, stateWrite };
    CurrentSavedState migrated {};
    ok &= check(state && state->save(plugin, &migratedOutput)
        && migratedMemory.bytes.size() >= sizeof(migrated),
        "migrated capture state save failed");
    if (migratedMemory.bytes.size() >= sizeof(migrated))
        std::memcpy(&migrated, migratedMemory.bytes.data(), sizeof(migrated));
    ok &= check(migrated.version == kStateVersion
        && migrated.storageMode == 0u && migrated.embedded[0u] == 1u,
        "version 5 pathless capture did not migrate to PROJECT safety embedding");

    PreviousSavedState linkedPrevious {};
    std::snprintf(linkedPrevious.paths[0u].data(),
        linkedPrevious.paths[0u].size(), "%s", "/tmp/offline-loop.wav");
    MemoryState linkedMemory;
    const auto* linkedBytes
        = reinterpret_cast<const uint8_t*>(&linkedPrevious);
    linkedMemory.bytes.assign(linkedBytes,
        linkedBytes + sizeof(linkedPrevious));
    clap_istream_t linkedInput { &linkedMemory, stateRead };
    ok &= check(state && state->load(plugin, &linkedInput),
        "version 5 linked state migration failed");
    MemoryState linkedRoundTrip;
    clap_ostream_t linkedOutput { &linkedRoundTrip, stateWrite };
    CurrentSavedState linkedCurrent {};
    ok &= check(state && state->save(plugin, &linkedOutput)
        && linkedRoundTrip.bytes.size() >= sizeof(linkedCurrent),
        "migrated linked state save failed");
    if (linkedRoundTrip.bytes.size() >= sizeof(linkedCurrent))
        std::memcpy(&linkedCurrent, linkedRoundTrip.bytes.data(),
            sizeof(linkedCurrent));
    ok &= check(linkedCurrent.storageMode == 1u
        && std::strcmp(linkedCurrent.paths[0u].data(),
            "/tmp/offline-loop.wav") == 0,
        "version 5 file reference did not migrate to LINK");

    CurrentSavedState projectSources = stored;
    projectSources.storageMode = 0u;
    projectSources.embedded.fill(0u);
    for (auto& path : projectSources.paths) path.fill('\0');
    std::snprintf(projectSources.paths[0u].data(),
        projectSources.paths[0u].size(), "%s", externalWave.c_str());
    std::snprintf(projectSources.paths[1u].data(),
        projectSources.paths[1u].size(), "%s", externalWave.c_str());
    MemoryState projectInputMemory;
    const auto* projectBytes
        = reinterpret_cast<const uint8_t*>(&projectSources);
    projectInputMemory.bytes.assign(projectBytes,
        projectBytes + sizeof(projectSources));
    clap_istream_t projectInput { &projectInputMemory, stateRead };
    ok &= check(state && state->load(plugin, &projectInput),
        "PROJECT file-backed state failed to load");
    const uint32_t registrationsBefore = host.projectFileRegistrations;
    const auto copyDeadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(3);
    while (host.projectFileRegistrations < registrationsBefore + 2u
        && std::chrono::steady_clock::now() < copyDeadline) {
        plugin->on_main_thread(plugin);
        std::this_thread::yield();
    }
    plugin->on_main_thread(plugin);
    MemoryState projectRoundTrip;
    clap_ostream_t projectOutput { &projectRoundTrip, stateWrite };
    CurrentSavedState copiedProject {};
    ok &= check(state && state->save(plugin, &projectOutput)
        && projectRoundTrip.bytes.size() >= sizeof(copiedProject),
        "PROJECT source state save failed");
    if (projectRoundTrip.bytes.size() >= sizeof(copiedProject))
        std::memcpy(&copiedProject, projectRoundTrip.bytes.data(),
            sizeof(copiedProject));
    std::size_t projectMediaFiles = 0u;
    const auto copiedDirectory = mediaDirectory / "s3g Samples";
    if (std::filesystem::is_directory(copiedDirectory))
        for (const auto& entry : std::filesystem::directory_iterator(
                 copiedDirectory))
            if (entry.is_regular_file()) ++projectMediaFiles;
    ok &= check(copiedProject.storageMode == 0u
        && copiedProject.embedded[0u] == 0u
        && copiedProject.embedded[1u] == 0u
        && copiedProject.paths[0u][0u] != '/'
        && std::strcmp(copiedProject.paths[0u].data(),
            copiedProject.paths[1u].data()) == 0
        && projectMediaFiles == 1u,
        "identical PROJECT sources did not reuse one relative media copy");

    plugin->stop_processing(plugin);
    plugin->deactivate(plugin);
    plugin->destroy(plugin);
    entry->deinit();
    dlclose(handle);
    activeHostContext = nullptr;
    std::filesystem::remove_all(storageRoot, filesystemError);
    if (!ok) return 1;
    std::cout << "Sample Rings CLAP smoke passed\n";
    return 0;
}
