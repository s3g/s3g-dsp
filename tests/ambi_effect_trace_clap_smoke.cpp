#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/latency.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>
#include <clap/ext/tail.h>

#include "s3g_ambi_effect_partial_trace.h"

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

constexpr uint32_t kChannels = 64u;
constexpr uint32_t kFrames = 128u;
constexpr double kSampleRate = 48000.0;
constexpr clap_id kBody = 2u;
constexpr clap_id kEnabled = 4u;
constexpr clap_id kCapture = 5u;
constexpr clap_id kClear = 6u;
constexpr clap_id kCaptureSeconds = 7u;
constexpr clap_id kTranspose = 14u;
constexpr clap_id kFreeze = 27u;
constexpr clap_id kSmear = 28u;

struct HostContext {
    clap_host_t host {};
    clap_host_tail_t tail {};
    uint32_t tailChanges = 0u;
};

HostContext* hostContext(const clap_host_t* host)
{
    return static_cast<HostContext*>(host->host_data);
}
const void* hostGetExtension(const clap_host_t* host, const char* id)
{
    return id && std::strcmp(id, CLAP_EXT_TAIL) == 0
        ? &hostContext(host)->tail : nullptr;
}
void hostRequest(const clap_host_t*) {}
void hostTailChanged(const clap_host_t* host)
{
    ++hostContext(host)->tailChanges;
}

struct Events {
    std::vector<clap_event_param_value_t> values;
    clap_input_events_t input {
        this,
        [](const clap_input_events_t* list) -> uint32_t {
            return static_cast<uint32_t>(
                static_cast<const Events*>(list->ctx)->values.size());
        },
        [](const clap_input_events_t* list, uint32_t index)
            -> const clap_event_header_t* {
            const auto& values = static_cast<const Events*>(list->ctx)->values;
            return index < values.size() ? &values[index].header : nullptr;
        },
    };
    void add(clap_id id, double value)
    {
        clap_event_param_value_t event {};
        event.header.size = sizeof(event);
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_PARAM_VALUE;
        event.param_id = id;
        event.note_id = -1;
        event.port_index = -1;
        event.channel = -1;
        event.key = -1;
        event.value = value;
        values.push_back(event);
    }
};

struct MemoryState {
    std::vector<uint8_t> bytes;
    size_t position = 0u;
};
int64_t writeState(const clap_ostream_t* stream, const void* source,
    uint64_t requested)
{
    auto* state = static_cast<MemoryState*>(stream->ctx);
    const size_t count = std::min<size_t>(requested, 19u);
    const auto* bytes = static_cast<const uint8_t*>(source);
    state->bytes.insert(state->bytes.end(), bytes, bytes + count);
    return static_cast<int64_t>(count);
}
int64_t readState(const clap_istream_t* stream, void* destination,
    uint64_t requested)
{
    auto* state = static_cast<MemoryState*>(stream->ctx);
    const size_t available = state->position < state->bytes.size()
        ? state->bytes.size() - state->position : 0u;
    const size_t count = std::min<size_t>({ available,
        static_cast<size_t>(requested), 17u });
    if (count > 0u) {
        std::memcpy(destination, state->bytes.data() + state->position, count);
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
        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            if (entry.is_regular_file()) return entry.path();
        }
    }
#endif
    return {};
}

struct Audio {
    std::array<std::array<float, kFrames>, kChannels> input {};
    std::array<std::array<float, kFrames>, kChannels> output {};
    std::array<float*, kChannels> inPointers {};
    std::array<float*, kChannels> outPointers {};
    clap_audio_buffer_t inBuffer {};
    clap_audio_buffer_t outBuffer {};
    Audio()
    {
        for (uint32_t channel = 0u; channel < kChannels; ++channel) {
            inPointers[channel] = input[channel].data();
            outPointers[channel] = output[channel].data();
        }
        inBuffer.data32 = inPointers.data();
        inBuffer.channel_count = kChannels;
        outBuffer.data32 = outPointers.data();
        outBuffer.channel_count = kChannels;
    }
    void clear()
    {
        for (auto& channel : input) channel.fill(0.0f);
        for (auto& channel : output) channel.fill(0.0f);
    }
};

bool run(const clap_plugin_t* plugin, Audio& audio, const Events* events = nullptr)
{
    clap_process_t process {};
    process.frames_count = kFrames;
    process.audio_inputs = &audio.inBuffer;
    process.audio_outputs = &audio.outBuffer;
    process.audio_inputs_count = 1u;
    process.audio_outputs_count = 1u;
    process.in_events = events ? &events->input : nullptr;
    return plugin->process(plugin, &process) == CLAP_PROCESS_CONTINUE;
}

bool finitePeak(const Audio& audio, float& peak)
{
    for (const auto& channel : audio.output) {
        for (float value : channel) {
            if (!std::isfinite(value)) return false;
            peak = std::max(peak, std::abs(value));
        }
    }
    return peak <= 0.89126f;
}

void flushValue(const clap_plugin_t* plugin,
    const clap_plugin_params_t* params, clap_id id, double value)
{
    Events events;
    events.add(id, value);
    params->flush(plugin, &events.input, nullptr);
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 3) {
        std::cerr << "usage: s3g_ambi_effect_trace_clap_smoke <bundle> <id>\n";
        return 2;
    }
    const bool partial = std::strstr(argv[2], "partial-trace") != nullptr;
    const auto binary = resolveBinary(argv[1]);
    void* library = binary.empty() ? nullptr
        : dlopen(binary.c_str(), RTLD_LOCAL | RTLD_NOW);
    const auto* entry = library ? static_cast<const clap_plugin_entry_t*>(
        dlsym(library, "clap_entry")) : nullptr;
    bool ok = entry && entry->init(binary.c_str());
    HostContext context {};
    context.tail.changed = hostTailChanged;
    context.host.clap_version = CLAP_VERSION_INIT;
    context.host.host_data = &context;
    context.host.name = "Ambi Effect Trace smoke";
    context.host.vendor = "s3g";
    context.host.url = "https://github.com/s3g/s3g-dsp";
    context.host.version = "1";
    context.host.get_extension = hostGetExtension;
    context.host.request_restart = hostRequest;
    context.host.request_process = hostRequest;
    context.host.request_callback = hostRequest;
    const auto* factory = ok ? static_cast<const clap_plugin_factory_t*>(
        entry->get_factory(CLAP_PLUGIN_FACTORY_ID)) : nullptr;
    const clap_plugin_t* plugin = factory
        ? factory->create_plugin(factory, &context.host, argv[2]) : nullptr;
    ok = ok && plugin && plugin->init(plugin);
    const auto* ports = ok ? static_cast<const clap_plugin_audio_ports_t*>(
        plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS)) : nullptr;
    const auto* params = ok ? static_cast<const clap_plugin_params_t*>(
        plugin->get_extension(plugin, CLAP_EXT_PARAMS)) : nullptr;
    const auto* state = ok ? static_cast<const clap_plugin_state_t*>(
        plugin->get_extension(plugin, CLAP_EXT_STATE)) : nullptr;
    const auto* latency = ok ? static_cast<const clap_plugin_latency_t*>(
        plugin->get_extension(plugin, CLAP_EXT_LATENCY)) : nullptr;
    const auto* tail = ok ? static_cast<const clap_plugin_tail_t*>(
        plugin->get_extension(plugin, CLAP_EXT_TAIL)) : nullptr;
    clap_audio_port_info_t port {};
    ok = ok && ports && params && state && latency && tail
        && ports->get(plugin, 0u, true, &port) && port.channel_count == kChannels
        && params->count(plugin) == (partial ? 24u : 19u)
        && latency->get(plugin) == (partial ? 0u : 1024u)
        && plugin->activate(plugin, kSampleRate, kFrames, kFrames)
        && plugin->start_processing(plugin);

    Audio audio;
    float peak = 0.0f;
    if (ok && partial) {
        flushValue(plugin, params, kTranspose, 7.0);
        flushValue(plugin, params, kSmear, 0.8);
        float phase = 0.0f;
        for (uint32_t block = 0u; ok && block < 160u; ++block) {
            audio.clear();
            for (uint32_t frame = 0u; frame < kFrames; ++frame) {
                audio.input[0][frame] = 0.12f * std::sin(phase);
                phase += 2.0f * 3.14159265358979323846f * 220.0f / 48000.0f;
            }
            ok = run(plugin, audio) && finitePeak(audio, peak);
        }
        flushValue(plugin, params, kFreeze, 1.0);
        float frozenPeak = 0.0f;
        for (uint32_t block = 0u; ok && block < 160u; ++block) {
            audio.clear();
            ok = run(plugin, audio) && finitePeak(audio, frozenPeak);
        }
        ok = ok && peak > 0.01f && frozenPeak > 0.001f
            && tail->get(plugin) == 0u;
        MemoryState partialState;
        clap_ostream_t partialOutput { &partialState, writeState };
        ok = ok && state->save(plugin, &partialOutput);
        flushValue(plugin, params, kFreeze, 0.0);
        flushValue(plugin, params, kSmear, 0.0);
        partialState.position = 0u;
        clap_istream_t partialInput { &partialState, readState };
        double frozenValue = 0.0;
        double smearValue = 0.0;
        ok = ok && state->load(plugin, &partialInput)
            && params->get_value(plugin, kFreeze, &frozenValue)
            && params->get_value(plugin, kSmear, &smearValue)
            && frozenValue > 0.5 && smearValue > 0.79;
        float restoredPeak = 0.0f;
        if (ok) {
            audio.clear();
            ok = run(plugin, audio) && finitePeak(audio, restoredPeak)
                && restoredPeak > 0.001f;
        }
        if (ok) {
            plugin->reset(plugin);
            audio.clear();
            restoredPeak = 0.0f;
            ok = run(plugin, audio) && finitePeak(audio, restoredPeak)
                && restoredPeak > 0.001f;
        }
        if (ok) {
            plugin->stop_processing(plugin);
            plugin->deactivate(plugin);
            ok = plugin->activate(plugin, kSampleRate, kFrames, kFrames)
                && plugin->start_processing(plugin);
            audio.clear();
            restoredPeak = 0.0f;
            ok = ok && run(plugin, audio) && finitePeak(audio, restoredPeak)
                && restoredPeak > 0.001f;
        }

        MemoryState legacyPartial;
        if (ok) {
            constexpr size_t headerBytes = sizeof(uint32_t) * 2u;
            constexpr size_t viewBytes = sizeof(int32_t) + sizeof(float) * 2u;
            constexpr size_t previousParamsSize = offsetof(
                s3g::AmbiEffectPartialTraceParams, freeze);
            constexpr size_t currentParamsSize = sizeof(
                s3g::AmbiEffectPartialTraceParams);
            constexpr size_t viewOffset = headerBytes + currentParamsSize;
            ok = partialState.bytes.size() >= viewOffset + viewBytes;
            if (ok) {
                legacyPartial.bytes.assign(partialState.bytes.begin(),
                    partialState.bytes.begin() + headerBytes
                        + previousParamsSize);
                const uint32_t legacyVersion = 1u;
                std::memcpy(legacyPartial.bytes.data() + sizeof(uint32_t),
                    &legacyVersion, sizeof(legacyVersion));
                legacyPartial.bytes.insert(legacyPartial.bytes.end(),
                    partialState.bytes.begin() + viewOffset,
                    partialState.bytes.begin() + viewOffset + viewBytes);
                clap_istream_t legacyInput { &legacyPartial, readState };
                ok = state->load(plugin, &legacyInput)
                    && params->get_value(plugin, kFreeze, &frozenValue)
                    && params->get_value(plugin, kSmear, &smearValue)
                    && frozenValue < 0.5 && smearValue < 0.001;
            }
        }
    } else if (ok) {
        flushValue(plugin, params, kBody, 1.0);
        flushValue(plugin, params, kCaptureSeconds, 0.05);
        flushValue(plugin, params, kCapture, 1.0);
        flushValue(plugin, params, kCapture, 0.0);
        uint32_t frameCounter = 0u;
        for (uint32_t block = 0u; ok && block < 20u; ++block) {
            audio.clear();
            for (uint32_t frame = 0u; frame < kFrames; ++frame, ++frameCounter) {
                if (frameCounter == 0u) {
                    audio.input[0][frame] = 0.35f;
                    audio.input[3][frame] = 0.45f;
                }
                if (frameCounter == 300u) {
                    audio.input[0][frame] = -0.12f;
                    audio.input[3][frame] = -0.20f;
                }
            }
            ok = run(plugin, audio);
        }
        for (uint32_t block = 0u; ok && block < 8u; ++block) {
            audio.clear();
            ok = run(plugin, audio);
        }
        flushValue(plugin, params, kEnabled, 1.0);
        ok = ok && tail->get(plugin) > 1024u;

        MemoryState memory;
        clap_ostream_t outputStream { &memory, writeState };
        ok = ok && state->save(plugin, &outputStream)
            && memory.bytes.size() > 30000u;
        flushValue(plugin, params, kClear, 1.0);
        flushValue(plugin, params, kClear, 0.0);
        memory.position = 0u;
        clap_istream_t inputStream { &memory, readState };
        ok = ok && state->load(plugin, &inputStream)
            && tail->get(plugin) > 1024u;

        MemoryState legacy;
        if (ok) {
            constexpr uint32_t expectedPickups = 4u;
            constexpr uint32_t expectedFrames = 2400u;
            constexpr size_t metadataBytes = sizeof(uint32_t) * 3u
                + sizeof(double);
            const size_t kernelBytes = static_cast<size_t>(expectedPickups)
                * expectedFrames * sizeof(float);
            ok = memory.bytes.size() > metadataBytes + kernelBytes;
            const size_t metadataOffset = ok
                ? memory.bytes.size() - metadataBytes - kernelBytes : 0u;
            uint32_t storedBody = 0u;
            uint32_t storedPickups = 0u;
            uint32_t storedFrames = 0u;
            double storedRate = 0.0;
            if (ok) {
                std::memcpy(&storedBody,
                    memory.bytes.data() + metadataOffset, sizeof(storedBody));
                std::memcpy(&storedPickups,
                    memory.bytes.data() + metadataOffset + sizeof(uint32_t),
                    sizeof(storedPickups));
                std::memcpy(&storedFrames,
                    memory.bytes.data() + metadataOffset + sizeof(uint32_t) * 2u,
                    sizeof(storedFrames));
                std::memcpy(&storedRate,
                    memory.bytes.data() + metadataOffset + sizeof(uint32_t) * 3u,
                    sizeof(storedRate));
                ok = storedBody == 1u && storedPickups == expectedPickups
                    && storedFrames == expectedFrames
                    && storedRate == kSampleRate;
            }
            if (ok) {
                legacy.bytes.assign(memory.bytes.begin(),
                    memory.bytes.begin() + metadataOffset);
                const uint32_t legacyVersion = 1u;
                std::memcpy(legacy.bytes.data() + sizeof(uint32_t),
                    &legacyVersion, sizeof(legacyVersion));
                const auto append = [&legacy](const void* source, size_t bytes) {
                    const auto* first = static_cast<const uint8_t*>(source);
                    legacy.bytes.insert(legacy.bytes.end(), first, first + bytes);
                };
                append(&storedFrames, sizeof(storedFrames));
                append(&storedRate, sizeof(storedRate));
                append(memory.bytes.data() + metadataOffset + metadataBytes,
                    static_cast<size_t>(storedFrames) * sizeof(float));
                flushValue(plugin, params, kClear, 1.0);
                flushValue(plugin, params, kClear, 0.0);
                clap_istream_t legacyStream { &legacy, readState };
                ok = state->load(plugin, &legacyStream)
                    && tail->get(plugin) > 1024u;
            }
        }
        if (ok) {
            plugin->stop_processing(plugin);
            plugin->deactivate(plugin);
            ok = plugin->activate(plugin, kSampleRate, kFrames, kFrames)
                && plugin->start_processing(plugin)
                && tail->get(plugin) > 1024u;
        }
        for (uint32_t block = 0u; ok && block < 45u; ++block) {
            audio.clear();
            if (block == 0u) audio.input[0][0] = 0.4f;
            ok = run(plugin, audio) && finitePeak(audio, peak);
        }
        ok = ok && peak > 0.00001f;
    }

    if (plugin) {
        plugin->stop_processing(plugin);
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
    }
    if (entry) entry->deinit();
    if (library) dlclose(library);
    if (!ok) {
        std::cerr << "Ambi Effect Trace CLAP smoke failed\n";
        return 1;
    }
    std::cout << "Ambi Effect " << (partial ? "Partial" : "Response")
        << " Trace CLAP smoke passed: peak=" << peak
        << " tail changes=" << context.tailChanges << "\n";
    return 0;
}
