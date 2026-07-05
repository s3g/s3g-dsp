#include "s3g_diffusion_mesh.h"
#include "s3g_math.h"

#include <clap/clap.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <new>

namespace {

constexpr uint32_t kChannelCount = 24;
constexpr uint32_t kStateVersion = 1;
constexpr clap_id kAmountParamId = 1;
constexpr clap_id kFeedbackParamId = 2;
constexpr clap_id kMixParamId = 3;

struct SavedState {
    uint32_t version = kStateVersion;
    double amount = 0.35;
    double feedback = 0.12;
    double mix = 0.5;
};

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    double sampleRate = 48000.0;
    uint32_t maxFrames = 0;
    double amount = 0.35;
    double feedback = 0.12;
    double mix = 0.5;
    s3g::DiffusionMesh24 mesh;
};

Plugin* self(const clap_plugin_t* plugin)
{
    return static_cast<Plugin*>(plugin->plugin_data);
}

double clamp01(double value)
{
    return std::clamp(value, 0.0, 1.0);
}

double clampFeedback(double value)
{
    return std::clamp(value, 0.0, 0.95);
}

void applyParamsToDsp(Plugin& p)
{
    p.mesh.setAmount(static_cast<float>(p.amount));
    p.mesh.setFeedback(static_cast<float>(p.feedback));
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
    p->mesh.reset();
    applyParamsToDsp(*p);
    return true;
}

void deactivate(const clap_plugin_t*) {}
bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}

void reset(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    p->mesh.reset();
    applyParamsToDsp(*p);
}

void setParam(Plugin& p, clap_id paramId, double value)
{
    switch (paramId) {
    case kAmountParamId:
        p.amount = clamp01(value);
        break;
    case kFeedbackParamId:
        p.feedback = clampFeedback(value);
        break;
    case kMixParamId:
        p.mix = clamp01(value);
        break;
    default:
        return;
    }
    applyParamsToDsp(p);
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
            setParam(p, param->param_id, param->value);
        }
    }
}

void clearExtraOutputs(const clap_audio_buffer_t& output, uint32_t channels, uint32_t frames)
{
    for (uint32_t ch = channels; ch < output.channel_count; ++ch) {
        if (output.data32 && output.data32[ch]) {
            std::fill(output.data32[ch], output.data32[ch] + frames, 0.0f);
        }
        if (output.data64 && output.data64[ch]) {
            std::fill(output.data64[ch], output.data64[ch] + frames, 0.0);
        }
    }
}

void processFloat(Plugin& p, const clap_audio_buffer_t& input, const clap_audio_buffer_t& output, uint32_t frames)
{
    std::array<float, kChannelCount> inFrame {};
    std::array<float, kChannelCount> wetFrame {};

    for (uint32_t i = 0; i < frames; ++i) {
        for (uint32_t ch = 0; ch < kChannelCount; ++ch) {
            inFrame[ch] = input.data32[ch][i];
        }

        p.mesh.processFrame(inFrame.data(), wetFrame.data());

        for (uint32_t ch = 0; ch < kChannelCount; ++ch) {
            output.data32[ch][i] = s3g::lerp(inFrame[ch], wetFrame[ch], static_cast<float>(p.mix));
        }
    }
}

void processDouble(Plugin& p, const clap_audio_buffer_t& input, const clap_audio_buffer_t& output, uint32_t frames)
{
    std::array<float, kChannelCount> inFrame {};
    std::array<float, kChannelCount> wetFrame {};

    for (uint32_t i = 0; i < frames; ++i) {
        for (uint32_t ch = 0; ch < kChannelCount; ++ch) {
            inFrame[ch] = static_cast<float>(input.data64[ch][i]);
        }

        p.mesh.processFrame(inFrame.data(), wetFrame.data());

        for (uint32_t ch = 0; ch < kChannelCount; ++ch) {
            const float mixed = s3g::lerp(inFrame[ch], wetFrame[ch], static_cast<float>(p.mix));
            output.data64[ch][i] = static_cast<double>(mixed);
        }
    }
}

void copyAvailableChannels(const clap_audio_buffer_t& input, const clap_audio_buffer_t& output, uint32_t channels, uint32_t frames)
{
    for (uint32_t ch = 0; ch < channels; ++ch) {
        if (input.data32 && output.data32 && input.data32[ch] && output.data32[ch]) {
            std::memcpy(output.data32[ch], input.data32[ch], sizeof(float) * frames);
        } else if (input.data64 && output.data64 && input.data64[ch] && output.data64[ch]) {
            std::memcpy(output.data64[ch], input.data64[ch], sizeof(double) * frames);
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

    if (channels == kChannelCount && input.data32 && output.data32) {
        processFloat(*p, input, output, frames);
    } else if (channels == kChannelCount && input.data64 && output.data64) {
        processDouble(*p, input, output, frames);
    } else {
        copyAvailableChannels(input, output, channels, frames);
    }

    clearExtraOutputs(output, channels, frames);
    return CLAP_PROCESS_CONTINUE;
}

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

uint32_t paramsCount(const clap_plugin_t*) { return 3; }

bool paramsGetInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info)
{
    if (!info) {
        return false;
    }

    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    std::strncpy(info->module, "Mesh", sizeof(info->module));

    switch (index) {
    case 0:
        info->id = kAmountParamId;
        std::strncpy(info->name, "Amount", sizeof(info->name));
        info->min_value = 0.0;
        info->max_value = 1.0;
        info->default_value = 0.35;
        return true;
    case 1:
        info->id = kFeedbackParamId;
        std::strncpy(info->name, "Feedback", sizeof(info->name));
        info->min_value = 0.0;
        info->max_value = 0.95;
        info->default_value = 0.12;
        return true;
    case 2:
        info->id = kMixParamId;
        std::strncpy(info->name, "Mix", sizeof(info->name));
        info->min_value = 0.0;
        info->max_value = 1.0;
        info->default_value = 0.5;
        return true;
    default:
        return false;
    }
}

bool paramsGetValue(const clap_plugin_t* plugin, clap_id paramId, double* value)
{
    if (!value) {
        return false;
    }

    const auto* p = self(plugin);
    switch (paramId) {
    case kAmountParamId:
        *value = p->amount;
        return true;
    case kFeedbackParamId:
        *value = p->feedback;
        return true;
    case kMixParamId:
        *value = p->mix;
        return true;
    default:
        return false;
    }
}

bool paramsValueToText(const clap_plugin_t*, clap_id paramId, double value, char* display, uint32_t size)
{
    if (!display || size == 0) {
        return false;
    }

    switch (paramId) {
    case kAmountParamId:
    case kFeedbackParamId:
    case kMixParamId:
        std::snprintf(display, size, "%.1f %%", value * 100.0);
        return true;
    default:
        return false;
    }
}

bool paramsTextToValue(const clap_plugin_t*, clap_id paramId, const char* display, double* value)
{
    if (!display || !value) {
        return false;
    }

    switch (paramId) {
    case kAmountParamId:
    case kFeedbackParamId:
    case kMixParamId:
        *value = std::atof(display);
        if (*value > 1.0) {
            *value *= 0.01;
        }
        return true;
    default:
        return false;
    }
}

void paramsFlush(const clap_plugin_t* plugin, const clap_input_events_t* in, const clap_output_events_t*)
{
    readParamEvents(*self(plugin), in);
}

const clap_plugin_params_t params {
    paramsCount,
    paramsGetInfo,
    paramsGetValue,
    paramsValueToText,
    paramsTextToValue,
    paramsFlush
};

bool writeAll(const clap_ostream_t* stream, const void* data, uint64_t size)
{
    auto* cursor = static_cast<const uint8_t*>(data);
    uint64_t remaining = size;

    while (remaining > 0) {
        const int64_t written = stream->write(stream, cursor, remaining);
        if (written <= 0) {
            return false;
        }
        cursor += written;
        remaining -= static_cast<uint64_t>(written);
    }
    return true;
}

bool readAll(const clap_istream_t* stream, void* data, uint64_t size)
{
    auto* cursor = static_cast<uint8_t*>(data);
    uint64_t remaining = size;

    while (remaining > 0) {
        const int64_t count = stream->read(stream, cursor, remaining);
        if (count <= 0) {
            return false;
        }
        cursor += count;
        remaining -= static_cast<uint64_t>(count);
    }
    return true;
}

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    if (!stream || !stream->write) {
        return false;
    }

    const auto* p = self(plugin);
    const SavedState state { kStateVersion, p->amount, p->feedback, p->mix };
    return writeAll(stream, &state, sizeof(state));
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) {
        return false;
    }

    SavedState state {};
    if (!readAll(stream, &state, sizeof(state))) {
        return false;
    }
    if (state.version != kStateVersion) {
        return false;
    }

    auto* p = self(plugin);
    p->amount = clamp01(state.amount);
    p->feedback = clampFeedback(state.feedback);
    p->mix = clamp01(state.mix);
    applyParamsToDsp(*p);
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
    CLAP_PLUGIN_FEATURE_DELAY,
    CLAP_PLUGIN_FEATURE_SURROUND,
    nullptr
};

const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-dsp.24ch-diffusion-mesh",
    "s3g 24ch Diffusion Mesh",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.1.0",
    "24-channel diffusion mesh for s3g-mc 3OAFX virtual speaker inserts.",
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

    applyParamsToDsp(*p);
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
