#include <clap/clap.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

constexpr const char* kPluginId =
    "org.s3g.s3g-dsp.ambi-wrangler-encoder-64";
constexpr clap_id kFilterMorphParamId = 62u;
constexpr uint32_t kChannels = 64u;
constexpr uint32_t kBlockFrames = 256u;
constexpr uint32_t kRejectedStateVersion = 13u;

const void* hostGetExtension(const clap_host_t*, const char*)
{
    return nullptr;
}

void hostRequest(const clap_host_t*) {}

struct InputEventList {
    std::vector<const clap_event_header_t*> events;

    void set(const clap_event_header_t* event)
    {
        events.clear();
        if (event) events.push_back(event);
    }
};

uint32_t inputEventCount(const clap_input_events_t* events)
{
    const auto* input =
        static_cast<const InputEventList*>(events->ctx);
    return input
        ? static_cast<uint32_t>(input->events.size()) : 0u;
}

const clap_event_header_t* inputEventGet(
    const clap_input_events_t* events, uint32_t index)
{
    const auto* input =
        static_cast<const InputEventList*>(events->ctx);
    return input && index < input->events.size()
        ? input->events[index] : nullptr;
}

bool outputEventPush(
    const clap_output_events_t*, const clap_event_header_t*)
{
    return true;
}

struct MemoryInput {
    const uint8_t* bytes = nullptr;
    std::size_t size = 0u;
    std::size_t offset = 0u;
};

int64_t streamRead(
    const clap_istream_t* stream, void* destination,
    uint64_t requested)
{
    auto* input = static_cast<MemoryInput*>(stream->ctx);
    if (!input || input->offset >= input->size) return 0;
    const std::size_t count = std::min<std::size_t>(
        static_cast<std::size_t>(requested),
        input->size - input->offset);
    std::memcpy(
        destination, input->bytes + input->offset, count);
    input->offset += count;
    return static_cast<int64_t>(count);
}

std::filesystem::path resolveBinary(
    const std::filesystem::path& supplied)
{
    if (std::filesystem::is_regular_file(supplied)) return supplied;
#if defined(__APPLE__)
    const auto bundled = supplied / "Contents" / "MacOS"
        / "s3g_ambi_wrangler_encoder";
    if (std::filesystem::is_regular_file(bundled)) return bundled;
#endif
    return {};
}

clap_event_param_value_t makeParamEvent(
    uint32_t time, double value)
{
    clap_event_param_value_t event {};
    event.header.size = sizeof(event);
    event.header.time = time;
    event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    event.header.type = CLAP_EVENT_PARAM_VALUE;
    event.param_id = kFilterMorphParamId;
    event.note_id = -1;
    event.port_index = -1;
    event.channel = -1;
    event.key = -1;
    event.value = value;
    return event;
}

struct PluginInstance {
    const clap_plugin_t* plugin = nullptr;
    bool activated = false;
    bool started = false;
    int64_t steadyTime = 0;

    void close()
    {
        if (!plugin) return;
        if (started) plugin->stop_processing(plugin);
        if (activated) plugin->deactivate(plugin);
        plugin->destroy(plugin);
        plugin = nullptr;
        activated = false;
        started = false;
    }

    ~PluginInstance() { close(); }
};

bool createRenderInstance(
    const clap_plugin_factory_t* factory,
    const clap_host_t* host,
    PluginInstance& instance)
{
    instance.plugin = factory->create_plugin(
        factory, host, kPluginId);
    if (!instance.plugin || !instance.plugin->init(instance.plugin)) {
        return false;
    }
    instance.activated = instance.plugin->activate(
        instance.plugin, 48000.0, 1u, kBlockFrames);
    if (!instance.activated) return false;
    instance.plugin->reset(instance.plugin);
    instance.started =
        instance.plugin->start_processing(instance.plugin);
    return instance.started;
}

using AudioBlock =
    std::array<std::array<float, kBlockFrames>, kChannels>;

struct RenderResult {
    double energy = 0.0;
    bool finite = true;
    bool processed = false;
};

RenderResult renderBlock(
    PluginInstance& instance,
    AudioBlock& destination,
    uint32_t destinationOffset,
    uint32_t frames,
    const clap_event_header_t* event)
{
    RenderResult result;
    if (!instance.plugin || destinationOffset + frames > kBlockFrames) {
        return result;
    }

    std::array<float*, kChannels> pointers {};
    for (uint32_t channel = 0u; channel < kChannels; ++channel) {
        std::fill_n(
            destination[channel].data() + destinationOffset,
            frames, 0.0f);
        pointers[channel] =
            destination[channel].data() + destinationOffset;
    }
    clap_audio_buffer_t output {};
    output.data32 = pointers.data();
    output.channel_count = kChannels;

    InputEventList inputEventList;
    inputEventList.set(event);
    clap_input_events_t inputEvents {
        &inputEventList, inputEventCount, inputEventGet
    };
    clap_output_events_t outputEvents {
        nullptr, outputEventPush
    };
    clap_process_t process {};
    process.steady_time = instance.steadyTime;
    process.frames_count = frames;
    process.audio_outputs = &output;
    process.audio_outputs_count = 1u;
    process.in_events = &inputEvents;
    process.out_events = &outputEvents;
    result.processed =
        instance.plugin->process(instance.plugin, &process)
        != CLAP_PROCESS_ERROR;
    instance.steadyTime += frames;

    for (uint32_t channel = 0u; channel < kChannels; ++channel) {
        for (uint32_t frame = 0u; frame < frames; ++frame) {
            const float value =
                destination[channel][destinationOffset + frame];
            result.finite =
                result.finite && std::isfinite(value);
            result.energy +=
                static_cast<double>(value) * value;
        }
    }
    return result;
}

bool warmInstance(PluginInstance& instance)
{
    AudioBlock scratch {};
    double energy = 0.0;
    for (uint32_t block = 0u; block < 4u; ++block) {
        const auto rendered = renderBlock(
            instance, scratch, 0u, kBlockFrames, nullptr);
        if (!rendered.processed || !rendered.finite) return false;
        energy += rendered.energy;
    }
    return energy > 1.0e-8;
}

bool queryMorph(
    const PluginInstance& instance, double& value)
{
    const auto* params =
        static_cast<const clap_plugin_params_t*>(
            instance.plugin->get_extension(
                instance.plugin, CLAP_EXT_PARAMS));
    return params && params->get_value(
        instance.plugin, kFilterMorphParamId, &value);
}

bool testEventOffset(
    const clap_plugin_factory_t* factory,
    const clap_host_t* host,
    uint32_t eventFrame,
    double& earlyZeroDifference)
{
    PluginInstance timestamped;
    PluginInstance split;
    PluginInstance atZero;
    if (!createRenderInstance(factory, host, timestamped)
        || !createRenderInstance(factory, host, split)
        || !createRenderInstance(factory, host, atZero)
        || !warmInstance(timestamped)
        || !warmInstance(split)
        || !warmInstance(atZero)) {
        return false;
    }

    AudioBlock timestampedAudio {};
    AudioBlock splitAudio {};
    AudioBlock zeroAudio {};
    const auto timestampedEvent =
        makeParamEvent(eventFrame, 2.0);
    const auto zeroEvent =
        makeParamEvent(0u, 2.0);
    const auto whole = renderBlock(
        timestamped, timestampedAudio, 0u,
        kBlockFrames, &timestampedEvent.header);
    const auto firstSpan = renderBlock(
        split, splitAudio, 0u, eventFrame, nullptr);
    const auto secondSpan = renderBlock(
        split, splitAudio, eventFrame,
        kBlockFrames - eventFrame, &zeroEvent.header);
    const auto fromZero = renderBlock(
        atZero, zeroAudio, 0u,
        kBlockFrames, &zeroEvent.header);
    if (!whole.processed || !firstSpan.processed
        || !secondSpan.processed || !fromZero.processed
        || !whole.finite || !firstSpan.finite
        || !secondSpan.finite || !fromZero.finite
        || whole.energy <= 1.0e-8
        || firstSpan.energy + secondSpan.energy <= 1.0e-8
        || fromZero.energy <= 1.0e-8) {
        return false;
    }

    uint64_t mismatches = 0u;
    earlyZeroDifference = 0.0;
    for (uint32_t channel = 0u;
        channel < kChannels; ++channel) {
        for (uint32_t frame = 0u;
            frame < kBlockFrames; ++frame) {
            if (std::memcmp(
                    &timestampedAudio[channel][frame],
                    &splitAudio[channel][frame],
                    sizeof(float)) != 0) {
                ++mismatches;
            }
            if (frame < eventFrame) {
                earlyZeroDifference += std::fabs(
                    static_cast<double>(
                        timestampedAudio[channel][frame])
                    - zeroAudio[channel][frame]);
            }
        }
    }

    double timestampedValue = -1.0;
    double splitValue = -1.0;
    double zeroValue = -1.0;
    return mismatches == 0u
        && earlyZeroDifference > 0.0
        && queryMorph(timestamped, timestampedValue)
        && queryMorph(split, splitValue)
        && queryMorph(atZero, zeroValue)
        && timestampedValue == 2.0
        && splitValue == 2.0
        && zeroValue == 2.0;
}

bool testActiveFlush(
    const clap_plugin_factory_t* factory,
    const clap_host_t* host)
{
    PluginInstance instance;
    if (!createRenderInstance(factory, host, instance)
        || !warmInstance(instance)) {
        return false;
    }
    const auto* params =
        static_cast<const clap_plugin_params_t*>(
            instance.plugin->get_extension(
                instance.plugin, CLAP_EXT_PARAMS));
    if (!params || !params->flush) return false;

    const auto event = makeParamEvent(0u, 0.375);
    InputEventList inputEventList;
    inputEventList.set(&event.header);
    clap_input_events_t inputEvents {
        &inputEventList, inputEventCount, inputEventGet
    };
    clap_output_events_t outputEvents {
        nullptr, outputEventPush
    };
    params->flush(
        instance.plugin, &inputEvents, &outputEvents);
    AudioBlock audio {};
    const auto rendered = renderBlock(
        instance, audio, 0u, kBlockFrames, nullptr);
    double value = -1.0;
    return rendered.processed && rendered.finite
        && rendered.energy > 1.0e-8
        && queryMorph(instance, value)
        && value == 0.375;
}

bool testData64AndTrailingGuards(
    const clap_plugin_factory_t* factory,
    const clap_host_t* host)
{
    constexpr uint32_t kExtendedChannels = kChannels + 4u;
    constexpr uint32_t kFrames = 37u;
    constexpr uint32_t kGuardSamples = 4u;
    constexpr double kGuardValue = 918273.625;
    using GuardedChannel =
        std::array<double, kFrames + kGuardSamples * 2u>;

    PluginInstance instance;
    if (!createRenderInstance(factory, host, instance)
        || !warmInstance(instance)) {
        return false;
    }
    const auto* params =
        static_cast<const clap_plugin_params_t*>(
            instance.plugin->get_extension(
                instance.plugin, CLAP_EXT_PARAMS));
    if (!params || !params->flush) return false;
    auto orderEvent = makeParamEvent(0u, 7.0);
    orderEvent.param_id = 2u;
    InputEventList orderEventList;
    orderEventList.set(&orderEvent.header);
    clap_input_events_t orderInputEvents {
        &orderEventList, inputEventCount, inputEventGet
    };
    clap_output_events_t orderOutputEvents {
        nullptr, outputEventPush
    };
    params->flush(
        instance.plugin, &orderInputEvents,
        &orderOutputEvents);
    AudioBlock orderSettleAudio {};
    const auto orderSettle = renderBlock(
        instance, orderSettleAudio, 0u,
        kBlockFrames, nullptr);
    if (!orderSettle.processed || !orderSettle.finite
        || orderSettle.energy <= 1.0e-8) {
        return false;
    }

    std::array<GuardedChannel, kExtendedChannels> storage {};
    std::array<double*, kExtendedChannels> pointers {};
    for (uint32_t channel = 0u;
        channel < kExtendedChannels; ++channel) {
        storage[channel].fill(kGuardValue);
        pointers[channel] =
            storage[channel].data() + kGuardSamples;
    }
    clap_audio_buffer_t output {};
    output.data32 = nullptr;
    output.data64 = pointers.data();
    output.channel_count = kExtendedChannels;
    InputEventList inputEventList;
    clap_input_events_t inputEvents {
        &inputEventList, inputEventCount, inputEventGet
    };
    clap_output_events_t outputEvents {
        nullptr, outputEventPush
    };
    clap_process_t process {};
    process.steady_time = instance.steadyTime;
    process.frames_count = kFrames;
    process.audio_outputs = &output;
    process.audio_outputs_count = 1u;
    process.in_events = &inputEvents;
    process.out_events = &outputEvents;
    const bool processed =
        instance.plugin->process(instance.plugin, &process)
        != CLAP_PROCESS_ERROR;
    instance.steadyTime += kFrames;

    double energy = 0.0;
    double highestAmbiChannelEnergy = 0.0;
    bool finite = true;
    bool trailingZero = true;
    bool guardsIntact = true;
    for (uint32_t channel = 0u;
        channel < kExtendedChannels; ++channel) {
        for (uint32_t guard = 0u;
            guard < kGuardSamples; ++guard) {
            guardsIntact = guardsIntact
                && storage[channel][guard] == kGuardValue
                && storage[channel][
                    kGuardSamples + kFrames + guard]
                    == kGuardValue;
        }
        for (uint32_t frame = 0u;
            frame < kFrames; ++frame) {
            const double value =
                storage[channel][kGuardSamples + frame];
            finite = finite && std::isfinite(value);
            if (channel < kChannels) {
                energy += value * value;
                if (channel == kChannels - 1u) {
                    highestAmbiChannelEnergy += value * value;
                }
            } else {
                trailingZero =
                    trailingZero && value == 0.0;
            }
        }
    }
    return processed && finite && guardsIntact
        && trailingZero && energy > 1.0e-8
        && highestAmbiChannelEnergy > 1.0e-16;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr
            << "usage: s3g_ambi_wrangler_clap_smoke "
               "<Wrangler .clap bundle or plugin binary>\n";
        return 2;
    }

    const std::filesystem::path binary =
        resolveBinary(argv[1]);
    if (binary.empty()) {
        std::cerr << "Could not resolve Wrangler binary from "
                  << argv[1] << "\n";
        return 1;
    }

    void* library =
        dlopen(binary.c_str(), RTLD_LOCAL | RTLD_NOW);
    if (!library) {
        std::cerr << "Could not load Wrangler: "
                  << dlerror() << "\n";
        return 1;
    }
    const auto* entry =
        static_cast<const clap_plugin_entry_t*>(
            dlsym(library, "clap_entry"));
    if (!entry || !entry->init(binary.c_str())) {
        std::cerr
            << "Could not initialize Wrangler's CLAP entry\n";
        dlclose(library);
        return 1;
    }

    clap_host_t host {};
    host.clap_version = CLAP_VERSION_INIT;
    host.name = "Wrangler CLAP automation smoke";
    host.vendor = "s3g";
    host.url = "https://github.com/s3g/s3g-dsp";
    host.version = "1";
    host.get_extension = hostGetExtension;
    host.request_restart = hostRequest;
    host.request_process = hostRequest;
    host.request_callback = hostRequest;

    const auto* factory =
        static_cast<const clap_plugin_factory_t*>(
            entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    bool ok = factory
        && factory->get_plugin_count(factory) == 1u;
    if (!ok) {
        std::cerr << "Wrangler factory was unavailable\n";
    }

    const clap_plugin_t* probe = ok
        ? factory->create_plugin(factory, &host, kPluginId)
        : nullptr;
    if (!probe || !probe->init(probe)) {
        std::cerr << "Could not create Wrangler metadata probe\n";
        ok = false;
    }

    if (ok) {
        const auto* params =
            static_cast<const clap_plugin_params_t*>(
                probe->get_extension(probe, CLAP_EXT_PARAMS));
        clap_param_info_t morphInfo {};
        bool foundMorph = false;
        if (params) {
            for (uint32_t index = 0u;
                index < params->count(probe); ++index) {
                clap_param_info_t info {};
                if (params->get_info(probe, index, &info)
                    && info.id == kFilterMorphParamId) {
                    morphInfo = info;
                    foundMorph = true;
                    break;
                }
            }
        }
        char text[128] {};
        double roundTrip = -1.0;
        const bool continuous =
            foundMorph
            && (morphInfo.flags & CLAP_PARAM_IS_AUTOMATABLE) != 0u
            && (morphInfo.flags & CLAP_PARAM_IS_STEPPED) == 0u
            && morphInfo.min_value == 0.0
            && morphInfo.max_value == 2.0
            && params->value_to_text(
                probe, kFilterMorphParamId, 0.375,
                text, sizeof(text))
            && params->text_to_value(
                probe, kFilterMorphParamId, text, &roundTrip)
            && std::fabs(roundTrip - 0.375) <= 1.0e-9;
        if (!continuous) {
            std::cerr
                << "Param 62 was not continuous/automatable over 0..2: "
                << "found=" << foundMorph
                << " flags=" << morphInfo.flags
                << " range=" << morphInfo.min_value
                << ".." << morphInfo.max_value
                << " roundTrip=" << roundTrip << "\n";
            ok = false;
        }

        if (ok) {
            InputEventList inputEventList;
            const auto event = makeParamEvent(0u, 0.375);
            inputEventList.set(&event.header);
            clap_input_events_t inputEvents {
                &inputEventList, inputEventCount, inputEventGet
            };
            clap_output_events_t outputEvents {
                nullptr, outputEventPush
            };
            params->flush(probe, &inputEvents, &outputEvents);
            double value = -1.0;
            if (!params->get_value(
                    probe, kFilterMorphParamId, &value)
                || std::fabs(value - 0.375) > 1.0e-9) {
                std::cerr
                    << "Param 62 quantized an intermediate value: "
                    << value << "\n";
                ok = false;
            }
        }

        if (ok) {
            const auto* state =
                static_cast<const clap_plugin_state_t*>(
                    probe->get_extension(probe, CLAP_EXT_STATE));
            const uint32_t oldVersion = kRejectedStateVersion;
            MemoryInput memory {
                reinterpret_cast<const uint8_t*>(&oldVersion),
                sizeof(oldVersion), 0u
            };
            clap_istream_t stream { &memory, streamRead };
            double before = -1.0;
            double after = -1.0;
            params->get_value(
                probe, kFilterMorphParamId, &before);
            const bool rejected =
                state && !state->load(probe, &stream);
            params->get_value(
                probe, kFilterMorphParamId, &after);
            if (!rejected || before != after) {
                std::cerr
                    << "Wrangler did not reject state version "
                    << oldVersion << " without mutation\n";
                ok = false;
            }
        }
    }
    if (probe) probe->destroy(probe);

    static constexpr std::array<uint32_t, 6u>
        kEventOffsets { 1u, 7u, 15u, 17u, 127u, 128u };
    double minimumEarlyDifference =
        std::numeric_limits<double>::infinity();
    if (ok) {
        for (uint32_t eventOffset : kEventOffsets) {
            double earlyDifference = 0.0;
            if (!testEventOffset(
                    factory, &host, eventOffset,
                    earlyDifference)) {
                std::cerr
                    << "Wrangler collapsed or corrupted automation "
                    << "at frame " << eventOffset << "\n";
                ok = false;
                break;
            }
            minimumEarlyDifference = std::min(
                minimumEarlyDifference, earlyDifference);
        }
    }
    if (ok && !testActiveFlush(factory, &host)) {
        std::cerr
            << "Active audio-thread flush did not update the "
               "continuous morph safely\n";
        ok = false;
    }
    if (ok
        && !testData64AndTrailingGuards(factory, &host)) {
        std::cerr
            << "Wrangler data64-only output or >64-channel "
               "zero/sentinel guards failed\n";
        ok = false;
    }

    entry->deinit();
    dlclose(library);

    if (!ok) return 1;
    std::printf(
        "Ambi Wrangler CLAP smoke passed "
        "(offsets 1/7/15/17/127/128 matched split calls "
        "bit-for-bit; minimum event@0 prefix difference %.6g; "
        "data64 + 68-channel guards passed)\n",
        minimumEarlyDifference);
    return 0;
}
