#include <clap/clap.h>
#include <clap/ext/audio-ports.h>

#include <algorithm>
#include <cstring>
#include <new>

#ifndef S3G_NULL_SUPPORTS_64
#error "S3G_NULL_SUPPORTS_64 must be defined"
#endif

namespace {

constexpr uint32_t kChannelCount = 36u;
constexpr bool kSupports64 = S3G_NULL_SUPPORTS_64 != 0;
constexpr const char* kPluginId = kSupports64
    ? "org.s3g.s3g-dsp.ambi-effect-null-64"
    : "org.s3g.s3g-dsp.ambi-effect-null-32";
constexpr const char* kPluginName = kSupports64
    ? "s3g Ambi Effect Null 64"
    : "s3g Ambi Effect Null 32";

struct Plugin {
    clap_plugin_t plugin {};
};

Plugin* self(const clap_plugin_t* plugin)
{
    return static_cast<Plugin*>(plugin->plugin_data);
}

bool init(const clap_plugin_t*) { return true; }
void destroy(const clap_plugin_t* plugin) { delete self(plugin); }
bool activate(const clap_plugin_t*, double sampleRate, uint32_t minimumFrames,
    uint32_t maximumFrames)
{
    return sampleRate > 0.0 && minimumFrames > 0u
        && maximumFrames >= minimumFrames;
}
void deactivate(const clap_plugin_t*) {}
bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}
void reset(const clap_plugin_t*) {}

template <typename Sample>
void copyChannels(Sample** input, Sample** output, uint32_t inputChannels,
    uint32_t outputChannels, uint32_t frames)
{
    for (uint32_t channel = 0u; channel < outputChannels; ++channel) {
        Sample* destination = output ? output[channel] : nullptr;
        if (!destination) continue;
        const Sample* source = input && channel < inputChannels
            ? input[channel] : nullptr;
        if (source)
            std::memcpy(destination, source, sizeof(Sample) * frames);
        else
            std::fill(destination, destination + frames, Sample {});
    }
}

clap_process_status process(const clap_plugin_t*, const clap_process_t* data)
{
    if (!data || data->audio_inputs_count == 0u
        || data->audio_outputs_count == 0u)
        return CLAP_PROCESS_CONTINUE;

    const auto& input = data->audio_inputs[0];
    const auto& output = data->audio_outputs[0];
    const uint32_t inputChannels = std::min(input.channel_count, kChannelCount);
    const uint32_t outputChannels = std::min(output.channel_count, kChannelCount);
    if (input.data64 && output.data64) {
        copyChannels(input.data64, output.data64, inputChannels,
            outputChannels, data->frames_count);
    } else if (input.data32 && output.data32) {
        copyChannels(input.data32, output.data32, inputChannels,
            outputChannels, data->frames_count);
    } else if (output.data64) {
        copyChannels<double>(nullptr, output.data64, 0u, outputChannels,
            data->frames_count);
    } else if (output.data32) {
        copyChannels<float>(nullptr, output.data32, 0u, outputChannels,
            data->frames_count);
    }
    return CLAP_PROCESS_CONTINUE;
}

uint32_t audioPortsCount(const clap_plugin_t*, bool) { return 1u; }
bool audioPortsGet(const clap_plugin_t*, uint32_t index, bool isInput,
    clap_audio_port_info_t* info)
{
    if (index != 0u || !info) return false;
    info->id = isInput ? 10u : 20u;
    std::strncpy(info->name,
        isInput ? "5OA ACN/SN3D In" : "5OA ACN/SN3D Out",
        sizeof(info->name));
    info->flags = CLAP_AUDIO_PORT_IS_MAIN
        | (kSupports64 ? CLAP_AUDIO_PORT_SUPPORTS_64BITS : 0u);
    info->channel_count = kChannelCount;
    info->port_type = CLAP_PORT_AMBISONIC;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}

const clap_plugin_audio_ports_t audioPorts {
    audioPortsCount,
    audioPortsGet,
};

const void* getExtension(const clap_plugin_t*, const char* id)
{
    return id && std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0
        ? &audioPorts : nullptr;
}
void onMainThread(const clap_plugin_t*) {}

const char* const features[] {
    CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,
    CLAP_PLUGIN_FEATURE_SURROUND,
    nullptr,
};

const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    kPluginId,
    kPluginName,
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.0.0-test",
    "Test-only 36-channel null processor for Max/CLAP bridge benchmarks.",
    features,
};

const clap_plugin_t* createPlugin(const clap_plugin_factory_t*,
    const clap_host_t*, const char* pluginId)
{
    if (!pluginId || std::strcmp(pluginId, descriptor.id) != 0) return nullptr;
    auto* instance = new (std::nothrow) Plugin();
    if (!instance) return nullptr;
    instance->plugin.desc = &descriptor;
    instance->plugin.plugin_data = instance;
    instance->plugin.init = init;
    instance->plugin.destroy = destroy;
    instance->plugin.activate = activate;
    instance->plugin.deactivate = deactivate;
    instance->plugin.start_processing = startProcessing;
    instance->plugin.stop_processing = stopProcessing;
    instance->plugin.reset = reset;
    instance->plugin.process = process;
    instance->plugin.get_extension = getExtension;
    instance->plugin.on_main_thread = onMainThread;
    return &instance->plugin;
}

uint32_t factoryGetPluginCount(const clap_plugin_factory_t*) { return 1u; }
const clap_plugin_descriptor_t* factoryGetPluginDescriptor(
    const clap_plugin_factory_t*, uint32_t index)
{
    return index == 0u ? &descriptor : nullptr;
}

const clap_plugin_factory_t factory {
    factoryGetPluginCount,
    factoryGetPluginDescriptor,
    createPlugin,
};

bool entryInit(const char*) { return true; }
void entryDeinit() {}
const void* entryGetFactory(const char* factoryId)
{
    return factoryId && std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0
        ? &factory : nullptr;
}

} // namespace

extern "C" const clap_plugin_entry_t clap_entry {
    CLAP_VERSION_INIT,
    entryInit,
    entryDeinit,
    entryGetFactory,
};
