#include <clap/clap.h>
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
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr uint32_t kInputChannels = 16u;
constexpr uint32_t kOutputChannels = 64u;
constexpr uint32_t kFrames = 64u;
constexpr clap_id kLayoutParamId = 1u;
constexpr clap_id kOutputParamId = 10u;
constexpr clap_id kSelectedSourceParamId = 11u;
constexpr clap_id kSelectedAzimuthParamId = 12u;
constexpr clap_id kSelectedElevationParamId = 13u;
constexpr clap_id kSelectedDistanceParamId = 14u;
constexpr clap_id kSelectedGainParamId = 15u;
constexpr clap_id kActiveSourcesParamId = 16u;
constexpr clap_id kSourceParamBase = 100u;
constexpr clap_id kSourceParamStride = 6u;
constexpr clap_id kSourceGainOffset = 3u;
constexpr clap_id kLastSourceGainParamId =
    kSourceParamBase + 63u * kSourceParamStride + kSourceGainOffset;

std::atomic<uint32_t> gProcessRequests { 0u };

const void* hostGetExtension(const clap_host_t*, const char*) { return nullptr; }
void hostRequest(const clap_host_t*) {}
void hostRequestProcess(const clap_host_t*) { gProcessRequests.fetch_add(1u, std::memory_order_relaxed); }

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

struct EventList {
    std::vector<clap_event_param_value_t> storage;
    clap_input_events_t input {
        this,
        [](const clap_input_events_t* list) -> uint32_t {
            const auto* self = static_cast<const EventList*>(list->ctx);
            return self ? static_cast<uint32_t>(self->storage.size()) : 0u;
        },
        [](const clap_input_events_t* list, uint32_t index) -> const clap_event_header_t* {
            const auto* self = static_cast<const EventList*>(list->ctx);
            return self && index < self->storage.size() ? &self->storage[index].header : nullptr;
        },
    };

    void add(clap_id id, double value)
    {
        clap_event_param_value_t event {};
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
        storage.push_back(event);
    }
};

template <uint32_t Channels>
struct AudioBlock {
    std::array<std::array<float, kFrames>, Channels> samples {};
    std::array<float*, Channels> pointers {};
    clap_audio_buffer_t buffer {};

    AudioBlock()
    {
        for (uint32_t channel = 0u; channel < Channels; ++channel) pointers[channel] = samples[channel].data();
        buffer.data32 = pointers.data();
        buffer.channel_count = Channels;
    }

    void fillInput()
    {
        for (uint32_t channel = 0u; channel < Channels; ++channel) {
            for (uint32_t frame = 0u; frame < kFrames; ++frame) {
                samples[channel][frame] = 0.04f * std::sin(
                    0.17f * static_cast<float>(frame) + 0.11f * static_cast<float>(channel));
            }
            samples[channel][0] += 0.2f;
        }
    }
};

struct MemoryStream {
    std::vector<uint8_t> bytes;
    size_t offset = 0u;
};

int64_t stateWrite(const clap_ostream_t* stream, const void* buffer, uint64_t size)
{
    auto* memory = static_cast<MemoryStream*>(stream->ctx);
    if (!memory || (!buffer && size != 0u)) return -1;
    const auto* begin = static_cast<const uint8_t*>(buffer);
    memory->bytes.insert(memory->bytes.end(), begin, begin + size);
    return static_cast<int64_t>(size);
}

int64_t stateRead(const clap_istream_t* stream, void* buffer, uint64_t size)
{
    auto* memory = static_cast<MemoryStream*>(stream->ctx);
    if (!memory || size > memory->bytes.size() - std::min(memory->offset, memory->bytes.size())) return -1;
    std::memcpy(buffer, memory->bytes.data() + memory->offset, static_cast<size_t>(size));
    memory->offset += static_cast<size_t>(size);
    return static_cast<int64_t>(size);
}

struct Instance {
    const clap_plugin_t* plugin = nullptr;
    const clap_plugin_params_t* params = nullptr;
    const clap_plugin_state_t* state = nullptr;
    bool activated = false;
    bool processing = false;

    bool initialize(const clap_plugin_factory_t* factory, const clap_host_t* host,
        const char* pluginId, bool startAudio = true)
    {
        plugin = factory->create_plugin(factory, host, pluginId);
        if (!plugin || !plugin->init(plugin)) return false;
        params = static_cast<const clap_plugin_params_t*>(plugin->get_extension(plugin, CLAP_EXT_PARAMS));
        state = static_cast<const clap_plugin_state_t*>(plugin->get_extension(plugin, CLAP_EXT_STATE));
        if (!params || !state) return false;
        if (!startAudio) return true;
        if (!plugin->activate(plugin, 48000.0, 1u, kFrames)) return false;
        activated = true;
        plugin->reset(plugin);
        processing = plugin->start_processing(plugin);
        return processing;
    }

    void shutdown()
    {
        if (!plugin) return;
        if (processing) plugin->stop_processing(plugin);
        if (activated) plugin->deactivate(plugin);
        plugin->destroy(plugin);
        plugin = nullptr;
        activated = false;
        processing = false;
    }
};

bool processBlock(Instance& instance, AudioBlock<kInputChannels>& input,
    AudioBlock<kOutputChannels>& output, const clap_input_events_t* events)
{
    clap_process_t process {};
    process.frames_count = kFrames;
    process.audio_inputs = &input.buffer;
    process.audio_inputs_count = 1u;
    process.audio_outputs = &output.buffer;
    process.audio_outputs_count = 1u;
    process.in_events = events;
    return instance.plugin->process(instance.plugin, &process) != CLAP_PROCESS_ERROR;
}

double maxDifference(const AudioBlock<kOutputChannels>& left, const AudioBlock<kOutputChannels>& right)
{
    double result = 0.0;
    for (uint32_t channel = 0u; channel < kOutputChannels; ++channel) {
        for (uint32_t frame = 0u; frame < kFrames; ++frame) {
            result = std::max(result, std::fabs(static_cast<double>(left.samples[channel][frame])
                - static_cast<double>(right.samples[channel][frame])));
        }
    }
    return result;
}

bool getNear(const Instance& instance, clap_id id, double expected)
{
    double value = 0.0;
    return instance.params->get_value(instance.plugin, id, &value)
        && std::isfinite(value) && std::fabs(value - expected) <= 1.0e-9;
}

bool valuesMatch(const Instance& left, const Instance& right, clap_id id)
{
    double leftValue = 0.0;
    double rightValue = 0.0;
    return left.params->get_value(left.plugin, id, &leftValue)
        && right.params->get_value(right.plugin, id, &rightValue)
        && std::isfinite(leftValue) && std::isfinite(rightValue)
        && leftValue == rightValue;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 3) {
        std::cerr << "usage: panner_state_publication_clap_smoke <bundle-or-binary> <plugin-id>\n";
        return 2;
    }

    const auto binary = resolveBinary(argv[1]);
    if (binary.empty()) return 1;
    void* library = dlopen(binary.c_str(), RTLD_LOCAL | RTLD_NOW);
    if (!library) {
        std::cerr << dlerror() << "\n";
        return 1;
    }
    const auto* entry = static_cast<const clap_plugin_entry_t*>(dlsym(library, "clap_entry"));
    bool ok = entry && entry->init(binary.c_str());

    clap_host_t host {};
    host.clap_version = CLAP_VERSION_INIT;
    host.name = "Panner state publication smoke";
    host.vendor = "s3g";
    host.url = "https://github.com/s3g/s3g-dsp";
    host.version = "1";
    host.get_extension = hostGetExtension;
    host.request_restart = hostRequest;
    host.request_process = hostRequestProcess;
    host.request_callback = hostRequest;

    const auto* factory = ok ? static_cast<const clap_plugin_factory_t*>(
        entry->get_factory(CLAP_PLUGIN_FACTORY_ID)) : nullptr;
    Instance flushedInactive;
    Instance processedReference;
    Instance queued;
    Instance automated;
    Instance restored;
    Instance restoreReference;
    ok = ok && factory
        && flushedInactive.initialize(factory, &host, argv[2], false)
        && processedReference.initialize(factory, &host, argv[2]);

    // Match the validator's important edge case: all automatable parameters
    // arrive in ID order, Active Sources narrows the valid selection, and a
    // later per-source parameter addresses an inactive slot. The inactive
    // flush and active process paths must canonicalize to identical state.
    EventList reproducibilityEvents;
    reproducibilityEvents.add(kLayoutParamId, 4.0);
    reproducibilityEvents.add(kSelectedSourceParamId, 64.0);
    reproducibilityEvents.add(kSelectedAzimuthParamId, -145.3224);
    reproducibilityEvents.add(kSelectedElevationParamId, -7.4039);
    reproducibilityEvents.add(kSelectedDistanceParamId, 2.3916);
    reproducibilityEvents.add(kSelectedGainParamId, -2.8855);
    reproducibilityEvents.add(kActiveSourcesParamId, 9.0);
    reproducibilityEvents.add(kLastSourceGainParamId, -9.9072);
    if (ok) flushedInactive.params->flush(
        flushedInactive.plugin, &reproducibilityEvents.input, nullptr);
    AudioBlock<kInputChannels> reproducibilityInput;
    AudioBlock<kOutputChannels> reproducibilityOutput;
    reproducibilityInput.fillInput();
    if (ok) ok = processBlock(processedReference, reproducibilityInput,
        reproducibilityOutput, &reproducibilityEvents.input);
    static constexpr clap_id kAliasedParamIds[] {
        kSelectedSourceParamId,
        kSelectedAzimuthParamId,
        kSelectedElevationParamId,
        kSelectedDistanceParamId,
        kSelectedGainParamId,
    };
    for (const clap_id id : kAliasedParamIds) {
        if (ok) ok = valuesMatch(flushedInactive, processedReference, id);
    }
    ok = ok && getNear(flushedInactive, kSelectedSourceParamId, 9.0)
        && getNear(processedReference, kSelectedSourceParamId, 9.0);
    MemoryStream flushedState;
    MemoryStream processedState;
    clap_ostream_t flushedStream { &flushedState, stateWrite };
    clap_ostream_t processedStream { &processedState, stateWrite };
    if (ok) ok = flushedInactive.state->save(flushedInactive.plugin, &flushedStream)
        && processedReference.state->save(processedReference.plugin, &processedStream)
        && flushedState.bytes == processedState.bytes;

    ok = ok
        && queued.initialize(factory, &host, argv[2])
        && automated.initialize(factory, &host, argv[2]);

    EventList controlEvents;
    controlEvents.add(kOutputParamId, -18.0);
    controlEvents.add(kSelectedGainParamId, -3.0);
    if (ok) queued.params->flush(queued.plugin, &controlEvents.input, nullptr);
    ok = ok && getNear(queued, kOutputParamId, -18.0)
        && getNear(queued, kSelectedGainParamId, -3.0);

    AudioBlock<kInputChannels> queuedInput;
    AudioBlock<kInputChannels> automatedInput;
    AudioBlock<kOutputChannels> queuedOutput;
    AudioBlock<kOutputChannels> automatedOutput;
    queuedInput.fillInput();
    automatedInput.fillInput();
    if (ok) {
        ok = processBlock(queued, queuedInput, queuedOutput, nullptr)
            && processBlock(automated, automatedInput, automatedOutput, &controlEvents.input);
    }
    const double queuedVsAutomated = maxDifference(queuedOutput, automatedOutput);
    ok = ok && queuedVsAutomated <= 1.0e-7;

    EventList audioEvents;
    audioEvents.add(kOutputParamId, -9.0);
    audioEvents.add(kSelectedGainParamId, -12.0);
    AudioBlock<kInputChannels> nextInput;
    AudioBlock<kOutputChannels> nextOutput;
    nextInput.fillInput();
    if (ok) ok = processBlock(queued, nextInput, nextOutput, &audioEvents.input);
    ok = ok && getNear(queued, kOutputParamId, -9.0)
        && getNear(queued, kSelectedGainParamId, -12.0);

    MemoryStream saved;
    clap_ostream_t outputStream { &saved, stateWrite };
    if (ok) ok = queued.state->save(queued.plugin, &outputStream) && !saved.bytes.empty();
    if (ok) ok = restored.initialize(factory, &host, argv[2])
        && restoreReference.initialize(factory, &host, argv[2]);
    clap_istream_t inputStream { &saved, stateRead };
    if (ok) ok = restored.state->load(restored.plugin, &inputStream);
    ok = ok && getNear(restored, kOutputParamId, -9.0)
        && getNear(restored, kSelectedGainParamId, -12.0)
        && gProcessRequests.load(std::memory_order_relaxed) > 0u;

    AudioBlock<kInputChannels> restoredInput;
    AudioBlock<kInputChannels> referenceInput;
    AudioBlock<kOutputChannels> restoredOutput;
    AudioBlock<kOutputChannels> referenceOutput;
    restoredInput.fillInput();
    referenceInput.fillInput();
    if (ok) {
        ok = processBlock(restored, restoredInput, restoredOutput, nullptr)
            && processBlock(restoreReference, referenceInput, referenceOutput, &audioEvents.input);
    }
    const double restoredVsAutomated = maxDifference(restoredOutput, referenceOutput);
    ok = ok && restoredVsAutomated <= 1.0e-7;

    // Exercise audio publication concurrently with control-side snapshot readers.
    std::atomic<bool> readerOk { true };
    std::thread reader;
    if (ok) {
        reader = std::thread([&]() {
            for (uint32_t i = 0u; i < 1000u; ++i) {
                double value = 0.0;
                if (!queued.params->get_value(queued.plugin, kOutputParamId, &value)
                    || !std::isfinite(value) || value < -60.0 || value > 12.0) {
                    readerOk.store(false, std::memory_order_relaxed);
                    return;
                }
                if ((i & 63u) == 0u) {
                    MemoryStream snapshot;
                    clap_ostream_t stream { &snapshot, stateWrite };
                    if (!queued.state->save(queued.plugin, &stream) || snapshot.bytes.empty()) {
                        readerOk.store(false, std::memory_order_relaxed);
                        return;
                    }
                }
            }
        });
        for (uint32_t i = 0u; i < 250u && ok; ++i) {
            EventList events;
            events.add(kOutputParamId, -24.0 + static_cast<double>(i % 25u));
            AudioBlock<kInputChannels> input;
            AudioBlock<kOutputChannels> output;
            ok = processBlock(queued, input, output, &events.input);
        }
        reader.join();
        ok = ok && readerOk.load(std::memory_order_relaxed);
    }

    std::cout << argv[2] << " queued/automation max difference: " << queuedVsAutomated
        << ", restored/automation: " << restoredVsAutomated << "\n";
    restoreReference.shutdown();
    restored.shutdown();
    automated.shutdown();
    queued.shutdown();
    processedReference.shutdown();
    flushedInactive.shutdown();
    if (entry && entry->deinit) entry->deinit();
    dlclose(library);
    if (!ok) {
        std::cerr << "Panner state-publication regression failed\n";
        return 1;
    }
    std::cout << "Panner state-publication regression passed\n";
    return 0;
}
