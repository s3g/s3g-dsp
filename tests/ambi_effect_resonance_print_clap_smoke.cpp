#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>
#include <clap/ext/tail.h>

#include "s3g_ambi_effect_resonance_print.h"

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
constexpr uint32_t kFrames = 256u;
constexpr double kSampleRate = 48000.0;
constexpr clap_id kCaptureParam = 4u;
constexpr clap_id kClearParam = 5u;
constexpr clap_id kCaptureSecondsParam = 6u;
constexpr clap_id kTransposeParam = 9u;
constexpr clap_id kOutputParam = 20u;
constexpr clap_id kBodyParam = 2u;
constexpr clap_id kTopologyParam = 3u;
constexpr clap_id kMaskDryParam = 26u;
constexpr clap_id kApplyPrintParam = 27u;

static_assert(offsetof(s3g::AmbiEffectResonancePrintParams, printEnabled)
        + sizeof(uint32_t)
    == sizeof(s3g::AmbiEffectResonancePrintParams));

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
    if (id && std::strcmp(id, CLAP_EXT_TAIL) == 0) {
        return &hostContext(host)->tail;
    }
    return nullptr;
}

void hostRequest(const clap_host_t*) {}

void hostTailChanged(const clap_host_t* host)
{
    ++hostContext(host)->tailChanges;
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

    void add(clap_id id, double value)
    {
        clap_event_param_value_t event {};
        event.header.size = sizeof(event);
        event.header.time = 0u;
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

struct MemoryState {
    std::vector<uint8_t> bytes;
    size_t offset = 0u;
};

int64_t stateWrite(const clap_ostream_t* stream,
    const void* source, uint64_t requested)
{
    auto* state = static_cast<MemoryState*>(stream->ctx);
    if (!state || (!source && requested > 0u)) return -1;
    const size_t count = std::min<size_t>(static_cast<size_t>(requested), 17u);
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

struct AudioBlock {
    std::array<std::array<float, kFrames>, kChannels> input {};
    std::array<std::array<float, kFrames>, kChannels> output {};
    std::array<float*, kChannels> inputPointers {};
    std::array<float*, kChannels> outputPointers {};
    clap_audio_buffer_t inputBuffer {};
    clap_audio_buffer_t outputBuffer {};

    AudioBlock()
    {
        for (uint32_t channel = 0u; channel < kChannels; ++channel) {
            inputPointers[channel] = input[channel].data();
            outputPointers[channel] = output[channel].data();
        }
        inputBuffer.data32 = inputPointers.data();
        inputBuffer.channel_count = kChannels;
        outputBuffer.data32 = outputPointers.data();
        outputBuffer.channel_count = kChannels;
    }

    void clear()
    {
        for (auto& channel : input) channel.fill(0.0f);
        for (auto& channel : output) channel.fill(0.0f);
    }
};

clap_process_status runBlock(const clap_plugin_t* plugin,
    AudioBlock& audio, const clap_input_events_t* events)
{
    clap_process_t process {};
    process.frames_count = kFrames;
    process.audio_inputs = &audio.inputBuffer;
    process.audio_outputs = &audio.outputBuffer;
    process.audio_inputs_count = 1u;
    process.audio_outputs_count = 1u;
    process.in_events = events;
    return plugin->process(plugin, &process);
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 3) {
        std::cerr << "usage: s3g_ambi_effect_resonance_print_clap_smoke "
            << "<bundle-or-binary> <plugin-id>\n";
        return 2;
    }
    const auto binary = resolveBinary(argv[1]);
    if (binary.empty()) {
        std::cerr << "Could not resolve Resonance Print binary\n";
        return 1;
    }
    void* library = dlopen(binary.c_str(), RTLD_LOCAL | RTLD_NOW);
    if (!library) {
        std::cerr << "Could not load Resonance Print: " << dlerror() << "\n";
        return 1;
    }
    const auto* entry = static_cast<const clap_plugin_entry_t*>(
        dlsym(library, "clap_entry"));
    bool ok = entry && entry->init(binary.c_str());
    HostContext hostContext {};
    hostContext.tail.changed = hostTailChanged;
    hostContext.host.clap_version = CLAP_VERSION_INIT;
    hostContext.host.host_data = &hostContext;
    hostContext.host.name = "Resonance Print smoke";
    hostContext.host.vendor = "s3g";
    hostContext.host.url = "https://github.com/s3g/s3g-dsp";
    hostContext.host.version = "1";
    hostContext.host.get_extension = hostGetExtension;
    hostContext.host.request_restart = hostRequest;
    hostContext.host.request_process = hostRequest;
    hostContext.host.request_callback = hostRequest;
    const auto* factory = ok ? static_cast<const clap_plugin_factory_t*>(
        entry->get_factory(CLAP_PLUGIN_FACTORY_ID)) : nullptr;
    const clap_plugin_t* plugin = factory
        ? factory->create_plugin(factory, &hostContext.host, argv[2]) : nullptr;
    ok = ok && plugin && plugin->init(plugin);
    const auto* ports = ok ? static_cast<const clap_plugin_audio_ports_t*>(
        plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS)) : nullptr;
    const auto* params = ok ? static_cast<const clap_plugin_params_t*>(
        plugin->get_extension(plugin, CLAP_EXT_PARAMS)) : nullptr;
    const auto* state = ok ? static_cast<const clap_plugin_state_t*>(
        plugin->get_extension(plugin, CLAP_EXT_STATE)) : nullptr;
    const auto* tail = ok ? static_cast<const clap_plugin_tail_t*>(
        plugin->get_extension(plugin, CLAP_EXT_TAIL)) : nullptr;
    clap_audio_port_info_t port {};
    ok = ok && ports && params && state && tail
        && ports->count(plugin, true) == 1u
        && ports->get(plugin, 0u, true, &port)
        && port.channel_count == kChannels
        && params->count(plugin) == 75u
        && plugin->activate(plugin, kSampleRate, kFrames, kFrames)
        && plugin->start_processing(plugin);

    if (ok) {
        char bodyText[64] {};
        char topologyText[64] {};
        char dryText[64] {};
        char applyText[64] {};
        double bodyValue = -1.0;
        double topologyValue = -1.0;
        double dryValue = 0.0;
        double applyValue = -1.0;
        ok = params->value_to_text(plugin, kBodyParam, 4.0,
                bodyText, sizeof(bodyText))
            && params->text_to_value(plugin, kBodyParam, bodyText, &bodyValue)
            && bodyValue == 4.0
            && params->value_to_text(plugin, kTopologyParam, 3.0,
                topologyText, sizeof(topologyText))
            && params->text_to_value(plugin, kTopologyParam,
                topologyText, &topologyValue)
            && topologyValue == 3.0
            && params->value_to_text(plugin, kMaskDryParam, -1.0,
                dryText, sizeof(dryText))
            && params->text_to_value(plugin, kMaskDryParam, dryText, &dryValue)
            && dryValue == -1.0
            && params->value_to_text(plugin, kApplyPrintParam, 1.0,
                applyText, sizeof(applyText))
            && params->text_to_value(plugin, kApplyPrintParam,
                applyText, &applyValue)
            && applyValue == 1.0;
    }

    AudioBlock audio;
    uint64_t sample = 0u;
    for (uint32_t block = 0u; ok && block < 49u; ++block) {
        audio.clear();
        for (uint32_t frame = 0u; frame < kFrames; ++frame, ++sample) {
            const double time = static_cast<double>(sample) / kSampleRate;
            audio.input[0][frame] = static_cast<float>(
                0.22 * std::sin(2.0 * 3.14159265358979323846 * 220.0 * time)
                + 0.10 * std::sin(2.0 * 3.14159265358979323846 * 440.0 * time));
        }
        EventList capture;
        if (block == 0u) {
            capture.add(kCaptureSecondsParam, 0.25);
            capture.add(kCaptureParam, 1.0);
        } else if (block == 1u) {
            capture.add(kCaptureParam, 0.0);
        } else if (block == 10u) {
            capture.add(kApplyPrintParam, 1.0);
        }
        const auto* events = capture.storage.empty() ? nullptr : &capture.input;
        ok = runBlock(plugin, audio, events) == CLAP_PROCESS_CONTINUE;
    }
    uint32_t capturedTail = 0u;
    double applyValue = -1.0;
    ok = ok && tail->get(plugin) == 0u
        && params->get_value(plugin, kApplyPrintParam, &applyValue)
        && applyValue == 0.0
        && hostContext.tailChanges > 0u;
    if (ok) {
        audio.clear();
        EventList apply;
        apply.add(kApplyPrintParam, 1.0);
        ok = runBlock(plugin, audio, &apply.input) == CLAP_PROCESS_CONTINUE;
        capturedTail = tail->get(plugin);
        ok = ok && capturedTail > 1000u
            && params->get_value(plugin, kApplyPrintParam, &applyValue)
            && applyValue == 1.0;
    }
    if (ok) {
        audio.clear();
        EventList bypass;
        bypass.add(kApplyPrintParam, 0.0);
        ok = runBlock(plugin, audio, &bypass.input) == CLAP_PROCESS_CONTINUE
            && tail->get(plugin) == 0u
            && params->get_value(plugin, kApplyPrintParam, &applyValue)
            && applyValue == 0.0;
    }
    if (ok) {
        audio.clear();
        EventList reapply;
        reapply.add(kApplyPrintParam, 1.0);
        ok = runBlock(plugin, audio, &reapply.input) == CLAP_PROCESS_CONTINUE
            && tail->get(plugin) == capturedTail
            && params->get_value(plugin, kApplyPrintParam, &applyValue)
            && applyValue == 1.0;
    }

    MemoryState saved;
    if (ok) {
        clap_ostream_t output { &saved, stateWrite };
        ok = state->save(plugin, &output) && saved.bytes.size() > 3500u;
    }
    if (ok) {
        audio.clear();
        EventList clear;
        clear.add(kClearParam, 1.0);
        ok = runBlock(plugin, audio, &clear.input) == CLAP_PROCESS_CONTINUE
            && tail->get(plugin) == 0u;
    }
    if (ok) {
        MemoryState input = saved;
        clap_istream_t stream { &input, stateRead };
        ok = state->load(plugin, &stream)
            && tail->get(plugin) == capturedTail;
    }
    if (ok) {
        MemoryState legacy = saved;
        const uint32_t versionOne = 1u;
        std::memcpy(legacy.bytes.data() + sizeof(uint32_t),
            &versionOne, sizeof(versionOne));
        const size_t printEnabledOffset = sizeof(uint32_t) * 2u
            + offsetof(s3g::AmbiEffectResonancePrintParams, printEnabled);
        legacy.bytes.erase(legacy.bytes.begin() + printEnabledOffset,
            legacy.bytes.begin() + printEnabledOffset + sizeof(uint32_t));
        clap_istream_t stream { &legacy, stateRead };
        ok = state->load(plugin, &stream)
            && tail->get(plugin) == 0u
            && params->get_value(plugin, kApplyPrintParam, &applyValue)
            && applyValue == 0.0;
    }
    if (ok) {
        audio.clear();
        EventList applyLegacyPrint;
        applyLegacyPrint.add(kApplyPrintParam, 1.0);
        ok = runBlock(plugin, audio, &applyLegacyPrint.input)
                == CLAP_PROCESS_CONTINUE
            && tail->get(plugin) == capturedTail;
    }

    double transposePhase = 0.0;
    float transposePeak = 0.0f;
    float transposeMaximumStep = 0.0f;
    float previousTransposeSample = 0.0f;
    if (ok) {
        for (uint32_t block = 0u; block < 120u; ++block) {
            audio.clear();
            for (uint32_t frame = 0u; frame < kFrames; ++frame) {
                audio.input[0][frame] = static_cast<float>(
                    0.18 * std::sin(transposePhase)
                    + 0.04 * std::sin(transposePhase * 2.01));
                transposePhase += 2.0 * 3.14159265358979323846
                    * 220.0 / kSampleRate;
            }
            EventList transpose;
            if (block == 0u) transpose.add(kOutputParam, -6.0);
            if (block % 4u == 0u) {
                transpose.add(kTransposeParam,
                    (block / 4u) % 2u == 0u ? 24.0 : -24.0);
            }
            ok = runBlock(plugin, audio,
                transpose.storage.empty() ? nullptr : &transpose.input)
                == CLAP_PROCESS_CONTINUE;
            for (uint32_t frame = 0u; frame < kFrames; ++frame) {
                const float value = audio.output[0][frame];
                ok = ok && std::isfinite(value);
                transposePeak = std::max(transposePeak, std::abs(value));
                transposeMaximumStep = std::max(transposeMaximumStep,
                    std::abs(value - previousTransposeSample));
                previousTransposeSample = value;
            }
        }
        ok = ok && transposePeak < 0.7f && transposeMaximumStep < 0.15f;
    }

    float bypassGuardPeak = 0.0f;
    if (ok) {
        for (uint32_t block = 0u; block < 8u; ++block) {
            audio.clear();
            audio.input[0].fill(0.5f);
            EventList outputGuard;
            if (block == 0u) {
                outputGuard.add(kApplyPrintParam, 0.0);
                outputGuard.add(kOutputParam, 12.0);
            }
            ok = runBlock(plugin, audio,
                outputGuard.storage.empty() ? nullptr : &outputGuard.input)
                == CLAP_PROCESS_CONTINUE;
            for (uint32_t channel = 0u; channel < kChannels; ++channel) {
                for (float value : audio.output[channel]) {
                    ok = ok && std::isfinite(value);
                    bypassGuardPeak = std::max(bypassGuardPeak,
                        std::abs(value));
                }
            }
        }
        ok = ok && tail->get(plugin) == 0u
            && bypassGuardPeak <= 0.8915f;
    }
    if (ok) {
        audio.clear();
        EventList restore;
        restore.add(kOutputParam, 0.0);
        restore.add(kTransposeParam, 0.0);
        restore.add(kApplyPrintParam, 1.0);
        ok = runBlock(plugin, audio, &restore.input) == CLAP_PROCESS_CONTINUE
            && tail->get(plugin) == capturedTail;
    }

    double energy = 0.0;
    if (ok) {
        plugin->reset(plugin);
        for (uint32_t block = 0u; block < 48u; ++block) {
            audio.clear();
            if (block == 0u) audio.input[0][0] = 1.0f;
            ok = runBlock(plugin, audio, nullptr) == CLAP_PROCESS_CONTINUE;
            for (uint32_t channel = 0u; channel < 4u; ++channel) {
                for (uint32_t frame = 0u; frame < kFrames; ++frame) {
                    const float value = audio.output[channel][frame];
                    ok = ok && std::isfinite(value);
                    if (block > 0u) energy += static_cast<double>(value) * value;
                }
            }
        }
        ok = ok && energy > 1.0e-14;
    }

    if (plugin) {
        plugin->stop_processing(plugin);
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
    }
    if (entry) entry->deinit();
    dlclose(library);
    if (!ok) {
        std::cerr << "Ambi Effect Resonance Print CLAP smoke failed"
            << " tail=" << capturedTail
            << " tail changes=" << hostContext.tailChanges
            << " transpose peak=" << transposePeak
            << " transpose step=" << transposeMaximumStep
            << " bypass guard=" << bypassGuardPeak
            << " energy=" << energy << "\n";
        return 1;
    }
    std::cout << "Ambi Effect Resonance Print CLAP smoke passed: tail="
        << capturedTail << " state=" << saved.bytes.size()
        << " bytes transpose-step=" << transposeMaximumStep
        << " bypass-guard=" << bypassGuardPeak
        << " energy=" << energy << "\n";
    return 0;
}
