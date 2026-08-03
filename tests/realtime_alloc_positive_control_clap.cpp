#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/params.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

namespace {

constexpr const char* kPluginId =
    "org.s3g.test.realtime-alloc-positive-control";
constexpr clap_id kControlParamId = 1u;
constexpr size_t kAllocationBytes = 37u;

struct Plugin {
    clap_plugin_t plugin {};
    double control = 0.0;
};

Plugin* self(const clap_plugin_t* plugin)
{
    return static_cast<Plugin*>(plugin->plugin_data);
}

// Prevent an optimized build from deleting the deliberate malloc/free pair.
// This CLAP exists only to prove that the opt-in realtime audit observes a
// known violation end to end.
__attribute__((noinline)) bool allocateOnce() noexcept
{
    void* memory = std::malloc(kAllocationBytes);
    if (!memory) return false;
    static volatile unsigned char observed = 0u;
    auto* bytes = static_cast<unsigned char*>(memory);
    bytes[0] = 0x5au;
    __asm__ volatile("" : "+r"(memory) : : "memory");
    observed = static_cast<unsigned char>(observed ^ bytes[0]);
    std::free(memory);
    return true;
}

void readParameterEvents(Plugin& instance,
    const clap_input_events_t* events)
{
    if (!events || !events->size || !events->get) return;
    const uint32_t count = events->size(events);
    for (uint32_t index = 0u; index < count; ++index) {
        const clap_event_header_t* header = events->get(events, index);
        if (!header || header->space_id != CLAP_CORE_EVENT_SPACE_ID
            || header->type != CLAP_EVENT_PARAM_VALUE
            || header->size < sizeof(clap_event_param_value_t)) continue;
        const auto* event = reinterpret_cast<
            const clap_event_param_value_t*>(header);
        if (event->param_id == kControlParamId)
            instance.control = std::clamp(event->value, 0.0, 1.0);
    }
}

bool pluginInit(const clap_plugin_t*) { return true; }
void pluginDestroy(const clap_plugin_t* plugin) { delete self(plugin); }
bool pluginActivate(const clap_plugin_t*, double sampleRate, uint32_t,
    uint32_t maximumFrames)
{
    return sampleRate > 0.0 && maximumFrames > 0u;
}
void pluginDeactivate(const clap_plugin_t*) {}
bool pluginStartProcessing(const clap_plugin_t*) { return true; }
void pluginStopProcessing(const clap_plugin_t*) {}
void pluginReset(const clap_plugin_t*) {}

clap_process_status pluginProcess(const clap_plugin_t* plugin,
    const clap_process_t* process)
{
    if (!plugin || !process || !allocateOnce()) return CLAP_PROCESS_ERROR;
    readParameterEvents(*self(plugin), process->in_events);

    if (process->audio_outputs_count == 0u || !process->audio_outputs)
        return CLAP_PROCESS_CONTINUE;
    const clap_audio_buffer_t* input = process->audio_inputs_count > 0u
            && process->audio_inputs
        ? &process->audio_inputs[0] : nullptr;
    clap_audio_buffer_t& output = process->audio_outputs[0];
    const uint32_t copiedChannels = input
        ? std::min(input->channel_count, output.channel_count) : 0u;

    for (uint32_t channel = 0u; channel < output.channel_count; ++channel) {
        if (output.data32 && output.data32[channel]) {
            if (channel < copiedChannels && input->data32
                && input->data32[channel]) {
                std::memcpy(output.data32[channel], input->data32[channel],
                    sizeof(float) * process->frames_count);
            } else {
                std::memset(output.data32[channel], 0,
                    sizeof(float) * process->frames_count);
            }
        } else if (output.data64 && output.data64[channel]) {
            if (channel < copiedChannels && input->data64
                && input->data64[channel]) {
                std::memcpy(output.data64[channel], input->data64[channel],
                    sizeof(double) * process->frames_count);
            } else {
                std::memset(output.data64[channel], 0,
                    sizeof(double) * process->frames_count);
            }
        }
    }
    return CLAP_PROCESS_CONTINUE;
}

uint32_t audioPortCount(const clap_plugin_t*, bool) { return 1u; }

bool audioPortGet(const clap_plugin_t*, uint32_t index, bool isInput,
    clap_audio_port_info_t* info)
{
    if (index != 0u || !info) return false;
    info->id = isInput ? 10u : 20u;
    std::snprintf(info->name, sizeof(info->name), "%s",
        isInput ? "Positive Control In" : "Positive Control Out");
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = 2u;
    info->port_type = CLAP_PORT_STEREO;
    info->in_place_pair = isInput ? 20u : 10u;
    return true;
}

const clap_plugin_audio_ports_t kAudioPorts {
    audioPortCount,
    audioPortGet,
};

uint32_t parameterCount(const clap_plugin_t*) { return 1u; }

bool parameterGetInfo(const clap_plugin_t*, uint32_t index,
    clap_param_info_t* info)
{
    if (index != 0u || !info) return false;
    info->id = kControlParamId;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    std::snprintf(info->name, sizeof(info->name), "%s", "Control");
    std::snprintf(info->module, sizeof(info->module), "%s", "Test");
    info->min_value = 0.0;
    info->max_value = 1.0;
    info->default_value = 0.0;
    return true;
}

bool parameterGetValue(const clap_plugin_t* plugin, clap_id id,
    double* value)
{
    if (id != kControlParamId || !value) return false;
    *value = self(plugin)->control;
    return true;
}

bool parameterValueToText(const clap_plugin_t*, clap_id id, double value,
    char* display, uint32_t size)
{
    if (id != kControlParamId || !display || size == 0u) return false;
    std::snprintf(display, size, "%.3f", value);
    return true;
}

bool parameterTextToValue(const clap_plugin_t*, clap_id id,
    const char* display, double* value)
{
    if (id != kControlParamId || !display || !value) return false;
    char* end = nullptr;
    const double parsed = std::strtod(display, &end);
    if (end == display || *end != '\0') return false;
    *value = std::clamp(parsed, 0.0, 1.0);
    return true;
}

void parameterFlush(const clap_plugin_t* plugin,
    const clap_input_events_t* input, const clap_output_events_t*)
{
    readParameterEvents(*self(plugin), input);
}

const clap_plugin_params_t kParameters {
    parameterCount,
    parameterGetInfo,
    parameterGetValue,
    parameterValueToText,
    parameterTextToValue,
    parameterFlush,
};

const void* pluginGetExtension(const clap_plugin_t*, const char* id)
{
    if (!id) return nullptr;
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &kAudioPorts;
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) return &kParameters;
    return nullptr;
}

void pluginOnMainThread(const clap_plugin_t*) {}

const char* const kFeatures[] {
    CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,
    CLAP_PLUGIN_FEATURE_STEREO,
    nullptr,
};

const clap_plugin_descriptor_t kDescriptor {
    CLAP_VERSION_INIT,
    kPluginId,
    "Realtime Allocation Positive Control",
    "s3g test",
    "",
    "",
    "",
    "1",
    "Test-only CLAP that deliberately allocates in process().",
    kFeatures,
};

const clap_plugin_t* factoryCreate(const clap_plugin_factory_t*,
    const clap_host_t* host, const char* pluginId)
{
    if (!host || !pluginId || std::strcmp(pluginId, kPluginId) != 0)
        return nullptr;
    auto* instance = new (std::nothrow) Plugin();
    if (!instance) return nullptr;
    instance->plugin.desc = &kDescriptor;
    instance->plugin.plugin_data = instance;
    instance->plugin.init = pluginInit;
    instance->plugin.destroy = pluginDestroy;
    instance->plugin.activate = pluginActivate;
    instance->plugin.deactivate = pluginDeactivate;
    instance->plugin.start_processing = pluginStartProcessing;
    instance->plugin.stop_processing = pluginStopProcessing;
    instance->plugin.reset = pluginReset;
    instance->plugin.process = pluginProcess;
    instance->plugin.get_extension = pluginGetExtension;
    instance->plugin.on_main_thread = pluginOnMainThread;
    return &instance->plugin;
}

uint32_t factoryPluginCount(const clap_plugin_factory_t*) { return 1u; }

const clap_plugin_descriptor_t* factoryDescriptor(
    const clap_plugin_factory_t*, uint32_t index)
{
    return index == 0u ? &kDescriptor : nullptr;
}

const clap_plugin_factory_t kFactory {
    factoryPluginCount,
    factoryDescriptor,
    factoryCreate,
};

bool entryInit(const char*) { return true; }
void entryDeinit() {}

const void* entryGetFactory(const char* factoryId)
{
    return factoryId && std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0
        ? &kFactory : nullptr;
}

} // namespace

extern "C" const clap_plugin_entry_t clap_entry {
    CLAP_VERSION_INIT,
    entryInit,
    entryDeinit,
    entryGetFactory,
};
