#include "s3g_gain.h"

#include <clap/clap.h>
#include "s3g_realtime.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <new>

namespace {

constexpr clap_id kGainParamId = 1;
constexpr uint32_t kChannelCount = 24;
constexpr uint32_t kStateVersion = 1;

struct SavedState {
    uint32_t version = kStateVersion;
    double gainDb = 0.0;
};

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    double sampleRate = 48000.0;
    uint32_t maxFrames = 0;
    double gainDb = 0.0;
    s3g::MultichannelGain gain;
};

double clampedGain(double value)
{
    return std::clamp(value, -60.0, 12.0);
}

Plugin* self(const clap_plugin_t* plugin)
{
    return static_cast<Plugin*>(plugin->plugin_data);
}

bool init(const clap_plugin_t*) { return true; }

void destroy(const clap_plugin_t* plugin)
{
    delete self(plugin);
}

bool activate(const clap_plugin_t* plugin, double sampleRate, uint32_t, uint32_t maxFrames)
{
    auto* p = self(plugin);
    p->sampleRate = sampleRate;
    p->maxFrames = maxFrames;
    p->gain.prepare(static_cast<float>(sampleRate), static_cast<int>(kChannelCount), 5.0f);
    p->gain.setGainDb(static_cast<float>(p->gainDb));
    return true;
}

void deactivate(const clap_plugin_t*) {}
bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}
void reset(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    p->gain.prepare(static_cast<float>(p->sampleRate), static_cast<int>(kChannelCount), 5.0f);
    p->gain.setGainDb(static_cast<float>(p->gainDb));
}

void readParamEvents(Plugin& p, const clap_input_events_t* in)
{
    if (!in) {
        return;
    }

    const uint32_t n = in->size(in);
    for (uint32_t i = 0; i < n; ++i) {
        const clap_event_header_t* ev = in->get(in, i);
        if (ev && ev->space_id == CLAP_CORE_EVENT_SPACE_ID && ev->type == CLAP_EVENT_PARAM_VALUE) {
            const auto* param = reinterpret_cast<const clap_event_param_value_t*>(ev);
            if (param->param_id == kGainParamId) {
                p.gainDb = clampedGain(param->value);
            }
        }
    }
}

clap_process_status process(const clap_plugin_t* plugin, const clap_process_t* process)
{
    auto* p = self(plugin);
    const uint32_t frames = process->frames_count;
    readParamEvents(*p, process->in_events);

    if (process->audio_inputs_count == 0 || process->audio_outputs_count == 0) {
        return CLAP_PROCESS_CONTINUE;
    }

    const auto& input = process->audio_inputs[0];
    const auto& output = process->audio_outputs[0];
    const uint32_t channels = std::min({ input.channel_count, output.channel_count, kChannelCount });

    for (uint32_t ch = 0; ch < channels; ++ch) {
        if (input.data32 && output.data32 && input.data32[ch] && output.data32[ch]) {
            std::memcpy(output.data32[ch], input.data32[ch], sizeof(float) * frames);
        } else if (input.data64 && output.data64 && input.data64[ch] && output.data64[ch]) {
            std::memcpy(output.data64[ch], input.data64[ch], sizeof(double) * frames);
        }
    }

    s3g::clearAudioBufferFromChannel(output, channels, frames);

    p->gain.setGainDb(static_cast<float>(p->gainDb));
    if (output.data32 && channels == kChannelCount) {
        p->gain.process(output.data32, static_cast<int>(frames));
    } else if (output.data32 && channels > 0) {
        s3g::MultichannelGain partialGain;
        partialGain.prepare(static_cast<float>(p->sampleRate), static_cast<int>(channels), 0.0f);
        partialGain.setGainDb(static_cast<float>(p->gainDb));
        partialGain.process(output.data32, static_cast<int>(frames));
    }

    if (output.data64 && channels > 0) {
        s3g::MultichannelGain doubleGain;
        doubleGain.prepare(static_cast<float>(p->sampleRate), static_cast<int>(channels), 0.0f);
        doubleGain.setGainDb(static_cast<float>(p->gainDb));
        doubleGain.process(output.data64, static_cast<int>(frames));
    }

    return CLAP_PROCESS_CONTINUE;
}

const void* getExtension(const clap_plugin_t*, const char*) { return nullptr; }
void onMainThread(const clap_plugin_t*) {}

uint32_t audioPortsCount(const clap_plugin_t*, bool) { return 1; }

bool audioPortsGet(const clap_plugin_t*, uint32_t index, bool isInput, clap_audio_port_info_t* info)
{
    if (index != 0 || !info) {
        return false;
    }
    info->id = isInput ? 10 : 20;
    std::strncpy(info->name, isInput ? "24ch In" : "24ch Out", sizeof(info->name));
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = kChannelCount;
    info->port_type = CLAP_PORT_SURROUND;
    info->in_place_pair = isInput ? 20 : 10;
    return true;
}

const clap_plugin_audio_ports_t audioPorts {
    audioPortsCount,
    audioPortsGet
};

uint32_t paramsCount(const clap_plugin_t*) { return 1; }

bool paramsGetInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info)
{
    if (index != 0 || !info) {
        return false;
    }
    info->id = kGainParamId;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    std::strncpy(info->name, "Gain", sizeof(info->name));
    std::strncpy(info->module, "Main", sizeof(info->module));
    info->min_value = -60.0;
    info->max_value = 12.0;
    info->default_value = 0.0;
    return true;
}

bool paramsGetValue(const clap_plugin_t* plugin, clap_id paramId, double* value)
{
    if (paramId != kGainParamId || !value) {
        return false;
    }
    *value = self(plugin)->gainDb;
    return true;
}

bool paramsValueToText(const clap_plugin_t*, clap_id paramId, double value, char* display, uint32_t size)
{
    if (paramId != kGainParamId || !display || size == 0) {
        return false;
    }
    std::snprintf(display, size, "%.2f dB", value);
    return true;
}

bool paramsTextToValue(const clap_plugin_t*, clap_id paramId, const char* display, double* value)
{
    if (paramId != kGainParamId || !display || !value) {
        return false;
    }
    *value = std::atof(display);
    return true;
}

void paramsFlush(const clap_plugin_t* plugin, const clap_input_events_t* in, const clap_output_events_t*)
{
    auto* p = self(plugin);
    if (!in) {
        return;
    }

    const uint32_t n = in->size(in);
    for (uint32_t i = 0; i < n; ++i) {
        const clap_event_header_t* ev = in->get(in, i);
        if (ev && ev->space_id == CLAP_CORE_EVENT_SPACE_ID && ev->type == CLAP_EVENT_PARAM_VALUE) {
            const auto* param = reinterpret_cast<const clap_event_param_value_t*>(ev);
            if (param->param_id == kGainParamId) {
                p->gainDb = clampedGain(param->value);
            }
        }
    }
}

const clap_plugin_params_t params {
    paramsCount,
    paramsGetInfo,
    paramsGetValue,
    paramsValueToText,
    paramsTextToValue,
    paramsFlush
};

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    if (!stream || !stream->write) {
        return false;
    }

    const SavedState state { kStateVersion, self(plugin)->gainDb };
    return stream->write(stream, &state, sizeof(state)) == static_cast<int64_t>(sizeof(state));
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) {
        return false;
    }

    SavedState state {};
    if (stream->read(stream, &state, sizeof(state)) != static_cast<int64_t>(sizeof(state))) {
        return false;
    }
    if (state.version != kStateVersion) {
        return false;
    }

    auto* p = self(plugin);
    p->gainDb = clampedGain(state.gainDb);
    p->gain.setGainDb(static_cast<float>(p->gainDb));
    return true;
}

const clap_plugin_state_t state {
    stateSave,
    stateLoad
};

const void* pluginGetExtension(const clap_plugin_t*, const char* id)
{
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) {
        return &audioPorts;
    }
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) {
        return &params;
    }
    if (std::strcmp(id, CLAP_EXT_STATE) == 0) {
        return &state;
    }
    return nullptr;
}

const char* const features[] {
    CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,
    CLAP_PLUGIN_FEATURE_SURROUND,
    nullptr
};

const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-dsp.24ch-passthrough-test",
    "s3g Passthrough Test 24ch",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.1.0",
    "24-channel passthrough/gain test for s3g-mc 3OAFX insert workflows.",
    features
};

const clap_plugin_t* createPlugin(const clap_plugin_factory*, const clap_host_t* host, const char* pluginId)
{
    if (std::strcmp(pluginId, descriptor.id) != 0) {
        return nullptr;
    }

    auto* p = new (std::nothrow) Plugin();
    if (!p) {
        return nullptr;
    }

    p->host = host;
    p->plugin.desc = &descriptor;
    p->plugin.plugin_data = p;
    p->plugin.init = init;
    p->plugin.destroy = destroy;
    p->plugin.activate = activate;
    p->plugin.deactivate = deactivate;
    p->plugin.start_processing = startProcessing;
    p->plugin.stop_processing = stopProcessing;
    p->plugin.reset = reset;
    p->plugin.process = process;
    p->plugin.get_extension = pluginGetExtension;
    p->plugin.on_main_thread = onMainThread;
    return &p->plugin;
}

uint32_t factoryGetPluginCount(const clap_plugin_factory*) { return 1; }

const clap_plugin_descriptor_t* factoryGetPluginDescriptor(const clap_plugin_factory*, uint32_t index)
{
    return index == 0 ? &descriptor : nullptr;
}

const clap_plugin_factory_t factory {
    factoryGetPluginCount,
    factoryGetPluginDescriptor,
    createPlugin
};

bool entryInit(const char*) { return true; }
void entryDeinit() {}

const void* entryGetFactory(const char* factoryId)
{
    if (std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0) {
        return &factory;
    }
    return nullptr;
}

} // namespace

extern "C" const clap_plugin_entry_t clap_entry {
    CLAP_VERSION_INIT,
    entryInit,
    entryDeinit,
    entryGetFactory
};
