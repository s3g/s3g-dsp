#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>

#include "s3g_format_upscale.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <iostream>
#include <vector>

namespace {

constexpr uint32_t kChannels = 64u;
constexpr uint32_t kFrames = 256u;

const void* hostGetExtension(const clap_host_t*, const char*) { return nullptr; }
void hostRequest(const clap_host_t*) {}

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
            const auto* events = static_cast<const Events*>(list->ctx);
            return index < events->values.size()
                ? &events->values[index].header : nullptr;
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
        values.push_back(event);
    }
};

struct MemoryState {
    std::vector<uint8_t> bytes;
    size_t offset = 0u;
};

struct FormatUpscaleSavedStateV3 {
    uint32_t version = 3u;
    s3g::FormatUpscaleParams params {};
    s3g::FormatUpscaleLayoutData customInput {};
    s3g::FormatUpscaleLayoutData customOutput {};
    uint32_t manualRoutesActive = 0u;
    std::array<uint8_t, kChannels * kChannels> manualRoutes {};
};

struct FormatUpscaleSavedStateV4 {
    uint32_t version = 4u;
    s3g::FormatUpscaleParams params {};
    s3g::FormatUpscaleLayoutData customInput {};
    s3g::FormatUpscaleLayoutData customOutput {};
    uint32_t manualRoutesActive = 0u;
    std::array<float, kChannels * kChannels> manualWeights {};
};

struct FormatUpscaleSavedStateV5 {
    uint32_t version = 5u;
    s3g::FormatUpscaleParams params {};
    s3g::FormatUpscaleLayoutData customInput {};
    s3g::FormatUpscaleLayoutData customOutput {};
    uint32_t manualRoutesActive = 0u;
    std::array<float, kChannels * kChannels> manualWeights {};
    uint32_t autoRowShape = 0u;
};

struct FormatUpscaleSavedStateV6 {
    uint32_t version = 6u;
    s3g::FormatUpscaleParams params {};
    s3g::FormatUpscaleLayoutData customInput {};
    s3g::FormatUpscaleLayoutData customOutput {};
    uint32_t manualRoutesActive = 0u;
    std::array<float, kChannels * kChannels> manualWeights {};
    uint32_t autoRowShape = 0u;
    uint32_t normalization = 2u;
};

int64_t stateWrite(const clap_ostream_t* stream,
    const void* source, uint64_t requested)
{
    auto* memory = static_cast<MemoryState*>(stream->ctx);
    const size_t count = std::min<size_t>(
        static_cast<size_t>(requested), 7u);
    const auto* bytes = static_cast<const uint8_t*>(source);
    memory->bytes.insert(memory->bytes.end(), bytes, bytes + count);
    return static_cast<int64_t>(count);
}

int64_t stateRead(const clap_istream_t* stream,
    void* destination, uint64_t requested)
{
    auto* memory = static_cast<MemoryState*>(stream->ctx);
    const size_t available = memory->offset < memory->bytes.size()
        ? memory->bytes.size() - memory->offset : 0u;
    const size_t count = std::min<size_t>({ available,
        static_cast<size_t>(requested), 5u });
    if (count > 0u) {
        std::memcpy(destination, memory->bytes.data() + memory->offset, count);
        memory->offset += count;
    }
    return static_cast<int64_t>(count);
}

bool processBlocks(const clap_plugin_t* plugin, Events* firstEvents,
    uint32_t blocks, uint32_t expectedActive,
    std::array<double, kChannels>& energy)
{
    std::array<std::array<float, kFrames>, kChannels> inputSamples {};
    std::array<std::array<float, kFrames>, kChannels> outputSamples {};
    std::array<float*, kChannels> inputPointers {};
    std::array<float*, kChannels> outputPointers {};
    for (uint32_t channel = 0u; channel < kChannels; ++channel) {
        inputPointers[channel] = inputSamples[channel].data();
        outputPointers[channel] = outputSamples[channel].data();
    }
    clap_audio_buffer_t input {};
    input.data32 = inputPointers.data();
    input.channel_count = kChannels;
    clap_audio_buffer_t output {};
    output.data32 = outputPointers.data();
    output.channel_count = kChannels;
    clap_process_t process {};
    process.frames_count = kFrames;
    process.audio_inputs = &input;
    process.audio_inputs_count = 1u;
    process.audio_outputs = &output;
    process.audio_outputs_count = 1u;
    energy.fill(0.0);

    for (uint32_t block = 0u; block < blocks; ++block) {
        for (auto& channel : outputSamples) channel.fill(0.0f);
        for (uint32_t frame = 0u; frame < kFrames; ++frame) {
            const uint32_t absolute = block * kFrames + frame;
            inputSamples[0][frame] = std::sin(
                static_cast<float>(absolute) * 0.031f) * 0.25f;
            inputSamples[1][frame] = std::cos(
                static_cast<float>(absolute) * 0.047f) * 0.20f;
        }
        process.in_events = block == 0u && firstEvents
            ? &firstEvents->input : nullptr;
        if (plugin->process(plugin, &process) != CLAP_PROCESS_CONTINUE)
            return false;
        for (uint32_t channel = 0u; channel < kChannels; ++channel) {
            for (float sample : outputSamples[channel]) {
                if (!std::isfinite(sample)) return false;
                if (channel >= expectedActive && sample != 0.0f) return false;
                energy[channel] += static_cast<double>(sample) * sample;
            }
        }
    }
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "usage: format_upscale_clap_smoke <bundle-or-binary>\n";
        return 2;
    }
    bool ok = true;
    const auto binary = resolveBinary(argv[1]);
    ok &= check(!binary.empty(), "could not resolve plug-in binary");
    void* library = binary.empty() ? nullptr
        : dlopen(binary.c_str(), RTLD_LOCAL | RTLD_NOW);
    if (!library) {
        const char* error = dlerror();
        if (error) std::cerr << error << '\n';
    }
    ok &= check(library != nullptr, "could not load plug-in binary");
    if (!library) return 1;

    const auto* entry = static_cast<const clap_plugin_entry_t*>(
        dlsym(library, "clap_entry"));
    ok &= check(entry && entry->init(binary.c_str()),
        "CLAP entry initialization failed");
    clap_host_t host {};
    host.clap_version = CLAP_VERSION_INIT;
    host.name = "Format Upscale smoke";
    host.vendor = "s3g";
    host.url = "https://github.com/s3g/s3g-dsp";
    host.version = "1";
    host.get_extension = hostGetExtension;
    host.request_restart = hostRequest;
    host.request_process = hostRequest;
    host.request_callback = hostRequest;

    const auto* factory = entry ? static_cast<const clap_plugin_factory_t*>(
        entry->get_factory(CLAP_PLUGIN_FACTORY_ID)) : nullptr;
    ok &= check(factory && factory->get_plugin_count(factory) == 1u,
        "plug-in factory is invalid");
    const clap_plugin_descriptor_t* descriptor = factory
        ? factory->get_plugin_descriptor(factory, 0u) : nullptr;
    ok &= check(descriptor && descriptor->name
            && std::strcmp(descriptor->name,
                "s3g Output Format Upscale 64") == 0,
        "host name is outside the Output family");
    const clap_plugin_t* plugin = factory ? factory->create_plugin(factory,
        &host, "org.s3g.s3g-dsp.format-upscale-64") : nullptr;
    ok &= check(plugin && plugin->init(plugin), "plug-in creation failed");
    if (!plugin) {
        entry->deinit();
        dlclose(library);
        return 1;
    }

    const auto* ports = static_cast<const clap_plugin_audio_ports_t*>(
        plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS));
    const auto* params = static_cast<const clap_plugin_params_t*>(
        plugin->get_extension(plugin, CLAP_EXT_PARAMS));
    const auto* state = static_cast<const clap_plugin_state_t*>(
        plugin->get_extension(plugin, CLAP_EXT_STATE));
    clap_audio_port_info_t inputPort {};
    clap_audio_port_info_t outputPort {};
    ok &= check(ports && params && state,
        "required CLAP extensions are missing");
    ok &= check(ports && ports->get(plugin, 0u, true, &inputPort)
            && ports->get(plugin, 0u, false, &outputPort)
            && inputPort.channel_count == kChannels
            && outputPort.channel_count == kChannels,
        "64-channel ports are incorrect");
    ok &= check(params && params->count(plugin) == 15u,
        "parameter count changed");
    bool foundInput = false;
    bool foundOutput = false;
    bool foundPlacement = false;
    bool foundAutoRowShape = false;
    bool foundNormalization = false;
    bool foundExpandedLayoutRange = false;
    for (uint32_t index = 0u; params && index < params->count(plugin); ++index) {
        clap_param_info_t info {};
        ok &= params->get_info(plugin, index, &info);
        if (info.id == 1u) foundInput = std::strcmp(info.name, "Input Format") == 0;
        if (info.id == 2u) {
            foundOutput = std::strcmp(info.name, "Output Format") == 0;
            foundExpandedLayoutRange = info.max_value
                == static_cast<double>(s3g::kFormatUpscaleLayoutCount - 1u);
        }
        if (info.id == 4u) {
            foundPlacement = std::strcmp(info.name, "Placement") == 0
                && info.max_value == 8.0;
        }
        if (info.id == 14u)
            foundAutoRowShape = std::strcmp(info.name, "Auto Row Shape") == 0
                && info.max_value == 3.0;
        if (info.id == 15u)
            foundNormalization = std::strcmp(
                info.name, "Matrix Normalization") == 0
                && info.max_value == 2.0 && info.default_value == 2.0;
    }
    ok &= check(foundInput && foundOutput && foundPlacement
            && foundAutoRowShape && foundNormalization
            && foundExpandedLayoutRange,
        "format parameters were not published");
    char midSideText[64] {};
    double parsedMidSide = -1.0;
    ok &= check(params->value_to_text(plugin, 4u, 8.0,
                midSideText, sizeof(midSideText))
            && std::strcmp(midSideText, "M/S spread") == 0
            && params->text_to_value(plugin, 4u, midSideText,
                &parsedMidSide)
            && parsedMidSide == 8.0,
        "M/S spread was not published as an automatable placement option");
    ok &= check(plugin->activate(plugin, 48000.0, 1u, kFrames)
            && plugin->start_processing(plugin),
        "plug-in activation failed");

    std::array<double, kChannels> energy {};
    ok &= check(processBlocks(plugin, nullptr, 4u, 4u, energy),
        "default stereo-to-quad processing failed");
    ok &= check(energy[0] > 0.01 && energy[1] > 0.01
            && energy[2] > 0.01 && energy[3] > 0.01,
        "default route did not energize all quad outputs");

    Events midSideChange;
    midSideChange.add(4u, 8.0);  // M/S spread.
    midSideChange.add(12u, 1.0); // Fast route smoothing for the smoke.
    ok &= check(processBlocks(plugin, &midSideChange, 2u, 4u, energy),
        "M/S spread processing failed");
    double midSidePlacement = -1.0;
    ok &= check(params->get_value(plugin, 4u, &midSidePlacement)
            && midSidePlacement == 8.0
            && energy[0] > 0.001 && energy[1] > 0.001
            && energy[2] > 0.001 && energy[3] > 0.001,
        "M/S spread did not remain selected or reach the quad outputs");

    Events change;
    change.add(2u, 6.0);  // Octophonic ring.
    change.add(4u, 4.0);  // Interleave.
    change.add(7u, 4.0);  // Four total copies.
    change.add(12u, 1.0); // Fast route smoothing for the smoke.
    change.add(14u, 1.0); // Center-weight every automatic row.
    change.add(15u, 1.0); // Normalize converging output columns.
    ok &= check(processBlocks(plugin, &change, 16u, 8u, energy),
        "octophonic interleave processing failed");
    uint32_t activeWithEnergy = 0u;
    for (uint32_t channel = 0u; channel < 8u; ++channel)
        if (energy[channel] > 0.001) ++activeWithEnergy;
    ok &= check(activeWithEnergy >= 6u,
        "interleave did not distribute into the octophonic output");

    MemoryState memory;
    clap_ostream_t outputState { &memory, stateWrite };
    ok &= check(state->save(plugin, &outputState)
            && memory.bytes.size() == sizeof(FormatUpscaleSavedStateV6),
        "state save failed");
    uint32_t savedVersion = 0u;
    if (memory.bytes.size() >= sizeof(savedVersion))
        std::memcpy(&savedVersion, memory.bytes.data(), sizeof(savedVersion));
    ok &= check(savedVersion == 6u,
        "normalization state did not use version 6");
    Events restoreChange;
    restoreChange.add(2u, 3.0);
    restoreChange.add(14u, 0.0);
    restoreChange.add(15u, 2.0);
    params->flush(plugin, &restoreChange.input, nullptr);
    double outputLayout = -1.0;
    ok &= check(params->get_value(plugin, 2u, &outputLayout)
            && outputLayout == 3.0, "parameter change before load failed");
    clap_istream_t inputState { &memory, stateRead };
    ok &= check(state->load(plugin, &inputState), "state load failed");
    ok &= check(params->get_value(plugin, 2u, &outputLayout)
            && outputLayout == 6.0, "state did not restore output format");
    double autoRowShape = -1.0;
    ok &= check(params->get_value(plugin, 14u, &autoRowShape)
            && autoRowShape == 1.0,
        "state did not restore automatic row shape");
    double normalization = -1.0;
    ok &= check(params->get_value(plugin, 15u, &normalization)
            && normalization == 1.0,
        "state did not restore column normalization");

    FormatUpscaleSavedStateV4 customState {};
    customState.params = {};
    customState.params.inputLayout = s3g::FormatUpscaleLayout::Custom;
    customState.params.outputLayout = s3g::FormatUpscaleLayout::Custom;
    customState.params.placement = s3g::FormatUpscalePlacement::Interleave;
    customState.params.copies = 3u;
    customState.params.smoothingMs = 1.0f;
    customState.customInput = s3g::formatUpscaleDefaultCustomInputLayout();
    customState.customOutput =
        s3g::formatUpscaleDefaultThreeTierOutputLayout();
    customState.manualRoutesActive = 1u;
    for (uint32_t input = 0u; input < 3u; ++input) {
        for (uint32_t tier = 0u; tier < 3u; ++tier)
            customState.manualWeights[input * kChannels + tier * 3u + input]
                = input == 0u && tier == 0u
                    ? -1.0f : (tier == 1u ? 0.5f : 1.0f);
    }
    MemoryState customMemory;
    customMemory.bytes.resize(sizeof(customState));
    std::memcpy(customMemory.bytes.data(), &customState, sizeof(customState));
    clap_istream_t customInputState { &customMemory, stateRead };
    ok &= check(state->load(plugin, &customInputState),
        "custom AED and drawn-route state load failed");
    ok &= check(params->get_value(plugin, 2u, &outputLayout)
            && outputLayout == static_cast<double>(
                static_cast<uint32_t>(s3g::FormatUpscaleLayout::Custom)),
        "custom output format was not restored");
    ok &= check(params->get_value(plugin, 14u, &autoRowShape)
            && autoRowShape == 0.0,
        "version-4 state did not default the new row shape safely");
    ok &= check(params->get_value(plugin, 15u, &normalization)
            && normalization == 0.0,
        "version-4 state did not retain row normalization compatibility");
    MemoryState signedRoundtripMemory;
    clap_ostream_t signedRoundtripOutput {
        &signedRoundtripMemory, stateWrite };
    ok &= check(state->save(plugin, &signedRoundtripOutput)
            && signedRoundtripMemory.bytes.size()
                == sizeof(FormatUpscaleSavedStateV6),
        "signed matrix state did not save");
    FormatUpscaleSavedStateV6 signedRoundtrip {};
    if (signedRoundtripMemory.bytes.size() == sizeof(signedRoundtrip))
        std::memcpy(&signedRoundtrip, signedRoundtripMemory.bytes.data(),
            sizeof(signedRoundtrip));
    ok &= check(signedRoundtrip.manualWeights[0u] == -1.0f,
        "state recall did not preserve negative matrix polarity");
    ok &= check(processBlocks(plugin, nullptr, 12u, 9u, energy),
        "state-restored 3-to-9 drawn-route processing failed");
    activeWithEnergy = 0u;
    for (uint32_t channel = 0u; channel < 9u; ++channel)
        if (energy[channel] > 0.001) ++activeWithEnergy;
    ok &= check(activeWithEnergy >= 6u,
        "state-restored custom layout did not reach its three tiers");

    FormatUpscaleSavedStateV3 legacyState {};
    legacyState.params = {};
    legacyState.params.smoothingMs = 1.0f;
    legacyState.customInput = s3g::formatUpscaleDefaultCustomInputLayout();
    legacyState.customOutput =
        s3g::formatUpscaleDefaultThreeTierOutputLayout();
    legacyState.manualRoutesActive = 1u;
    legacyState.manualRoutes[0u * kChannels + 0u] = 1u;
    legacyState.manualRoutes[0u * kChannels + 2u] = 1u;
    legacyState.manualRoutes[1u * kChannels + 1u] = 1u;
    legacyState.manualRoutes[1u * kChannels + 3u] = 1u;
    MemoryState legacyMemory;
    legacyMemory.bytes.resize(sizeof(legacyState));
    std::memcpy(legacyMemory.bytes.data(), &legacyState,
        sizeof(legacyState));
    clap_istream_t legacyInputState { &legacyMemory, stateRead };
    ok &= check(state->load(plugin, &legacyInputState),
        "version-3 binary matrix state did not migrate");
    ok &= check(processBlocks(plugin, nullptr, 12u, 4u, energy),
        "version-3 migrated matrix did not process");
    ok &= check(energy[0u] > 0.001 && energy[1u] > 0.001
            && energy[2u] > 0.001 && energy[3u] > 0.001,
        "version-3 routes were not converted to full matrix weights");

    plugin->stop_processing(plugin);
    plugin->deactivate(plugin);
    plugin->destroy(plugin);
    entry->deinit();
    dlclose(library);
    if (!ok) return 1;
    std::cout << "format upscale CLAP smoke passed\n";
    return 0;
}
