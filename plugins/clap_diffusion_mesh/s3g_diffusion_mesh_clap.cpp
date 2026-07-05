#include "s3g_diffusion_mesh.h"
#include "s3g_lane_patch.h"
#include "s3g_math.h"

#include <clap/clap.h>
#if defined(__APPLE__)
#include <clap/ext/gui.h>
#import <Cocoa/Cocoa.h>
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <new>

namespace {

#ifndef S3G_DIFFUSION_MESH_DEFAULT_CHANNELS
#define S3G_DIFFUSION_MESH_DEFAULT_CHANNELS 2
#endif

#ifndef S3G_DIFFUSION_MESH_PLUGIN_ID
#define S3G_DIFFUSION_MESH_PLUGIN_ID "org.s3g.s3g-dsp.diffusion-mesh"
#endif

#ifndef S3G_DIFFUSION_MESH_PLUGIN_NAME
#define S3G_DIFFUSION_MESH_PLUGIN_NAME "s3g Diffusion Mesh"
#endif

#ifndef S3G_DIFFUSION_MESH_PLUGIN_DESCRIPTION
#define S3G_DIFFUSION_MESH_PLUGIN_DESCRIPTION "Scalable multichannel diffusion mesh with a 128-channel realtime cap."
#endif

#ifndef S3G_DIFFUSION_MESH_FIXED_PORTS
#define S3G_DIFFUSION_MESH_FIXED_PORTS 0
#endif

constexpr uint32_t kDefaultChannelCount = S3G_DIFFUSION_MESH_DEFAULT_CHANNELS;
constexpr uint32_t kMaxChannelCount = s3g::kMaxRealtimeChannels;
constexpr bool kFixedPorts = S3G_DIFFUSION_MESH_FIXED_PORTS != 0;
constexpr uint32_t kStateVersion = 6;
constexpr uint32_t kPreviousStateVersion = 5;
constexpr uint32_t kLegacyStateVersion = 4;
constexpr clap_id kAmountParamId = 1;
constexpr clap_id kFeedbackParamId = 2;
constexpr clap_id kMixParamId = 3;
constexpr clap_id kLaneModeParamId = 4;
constexpr clap_id kActiveChannelsParamId = 5;
constexpr clap_id kInputStartParamId = 6;
constexpr clap_id kOutputStartParamId = 7;
constexpr clap_id kClearUnusedParamId = 8;
constexpr clap_id kTopologySpreadParamId = 9;
constexpr clap_id kTopologySkewParamId = 10;
constexpr clap_id kTopologyJitterParamId = 11;
constexpr clap_id kDisplaceCollapseParamId = 12;
constexpr clap_id kDisplaceDirXParamId = 13;
constexpr clap_id kDisplaceDirYParamId = 14;
constexpr clap_id kDisplaceDirZParamId = 15;
constexpr clap_id kDisplaceTwistParamId = 16;
constexpr clap_id kDisplaceFlareParamId = 17;
constexpr clap_id kInputPortId = 10;
constexpr clap_id kOutputPortId = 20;

enum LaneMode : uint32_t {
    kLaneModeFull = 0,
    kLaneMode2ch = 1,
    kLaneMode4ch = 2,
    kLaneModeSix = 3,
    kLaneModeCustom = 4
};

constexpr uint32_t kLaneModeMax = static_cast<uint32_t>(kLaneModeCustom);
constexpr uint32_t kConfigCount = 1 + (kMaxChannelCount / 2);

uint32_t channelsForConfigIndex(uint32_t index)
{
    return index == 0 ? 1u : index * 2u;
}

clap_id configIdForChannels(uint32_t channels)
{
    return static_cast<clap_id>(channels);
}

bool isAdvertisedConfigChannelCount(uint32_t channels)
{
    return channels == 1 || (channels >= 2 && channels <= kMaxChannelCount && (channels % 2) == 0);
}

struct SavedState {
    uint32_t version = kStateVersion;
    uint32_t channelCount = kDefaultChannelCount;
    uint32_t activeChannels = kDefaultChannelCount;
    uint32_t inputStart = 1;
    uint32_t outputStart = 1;
    uint32_t clearUnused = 0;
    uint32_t laneMode = kLaneModeFull;
    uint64_t patchRows[s3g::kLanePatchMaxChannels] {};
    double amount = 0.35;
    double feedback = 0.12;
    double mix = 0.5;
    double topologySpread = 0.0;
    double topologySkew = 0.0;
    double topologyJitter = 0.0;
    double displaceCollapse = 0.0;
    double displaceDirX = 0.0;
    double displaceDirY = 0.0;
    double displaceDirZ = 1.0;
    double displaceTwist = 0.0;
    double displaceFlare = 0.0;
};

struct SavedStateV4 {
    uint32_t version = kLegacyStateVersion;
    uint32_t channelCount = kDefaultChannelCount;
    uint32_t activeChannels = kDefaultChannelCount;
    uint32_t inputStart = 1;
    uint32_t outputStart = 1;
    uint32_t clearUnused = 0;
    uint32_t laneMode = kLaneModeFull;
    uint64_t patchRows[s3g::kLanePatchMaxChannels] {};
    double amount = 0.35;
    double feedback = 0.12;
    double mix = 0.5;
};

struct SavedStateV5 {
    uint32_t version = kPreviousStateVersion;
    uint32_t channelCount = kDefaultChannelCount;
    uint32_t activeChannels = kDefaultChannelCount;
    uint32_t inputStart = 1;
    uint32_t outputStart = 1;
    uint32_t clearUnused = 0;
    uint32_t laneMode = kLaneModeFull;
    uint64_t patchRows[s3g::kLanePatchMaxChannels] {};
    double amount = 0.35;
    double feedback = 0.12;
    double mix = 0.5;
    double topologySpread = 0.0;
    double topologySkew = 0.0;
    double topologyJitter = 0.0;
};

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    double sampleRate = 48000.0;
    uint32_t maxFrames = 0;
    uint32_t channelCount = kDefaultChannelCount;
    double amount = 0.35;
    double feedback = 0.12;
    double mix = 0.5;
    double topologySpread = 0.0;
    double topologySkew = 0.0;
    double topologyJitter = 0.0;
    double displaceCollapse = 0.0;
    double displaceDirX = 0.0;
    double displaceDirY = 0.0;
    double displaceDirZ = 1.0;
    double displaceTwist = 0.0;
    double displaceFlare = 0.0;
    uint32_t activeChannels = kDefaultChannelCount;
    uint32_t inputStart = 1;
    uint32_t outputStart = 1;
    bool clearUnused = false;
    uint32_t laneMode = kLaneModeFull;
    s3g::LanePatch patch;
    s3g::DiffusionMesh mesh;
    std::array<float, kMaxChannelCount> resolvedMix {};
#if defined(__APPLE__)
    void* guiView = nullptr;
#endif
};

Plugin* self(const clap_plugin_t* plugin)
{
    return static_cast<Plugin*>(plugin->plugin_data);
}

#if defined(__APPLE__)
void guiDestroy(const clap_plugin_t* plugin);
#endif

double clamp01(double value)
{
    return std::clamp(value, 0.0, 1.0);
}

double clampFeedback(double value)
{
    return std::clamp(value, 0.0, 0.95);
}

double clampBipolar(double value)
{
    return std::clamp(value, -1.0, 1.0);
}

uint32_t clampChannelCount(uint32_t channels)
{
    return std::clamp(channels, 1u, kMaxChannelCount);
}

uint32_t roundedUint(double value)
{
    return static_cast<uint32_t>(std::max(0.0, std::floor(value + 0.5)));
}

bool textEquals(const char* a, const char* b)
{
    return std::strcmp(a, b) == 0;
}

const char* laneModeName(uint32_t mode)
{
    switch (mode) {
    case kLaneModeFull:
        return "Full";
    case kLaneMode2ch:
        return "2ch";
    case kLaneMode4ch:
        return "4ch";
    case kLaneModeSix:
        return "6ch";
    case kLaneModeCustom:
        return "Custom";
    default:
        return "Custom";
    }
}

bool laneModeValueFromText(const char* display, double* value)
{
    if (textEquals(display, "Full")) {
        *value = static_cast<double>(kLaneModeFull);
        return true;
    }
    if (textEquals(display, "2ch") || textEquals(display, "Stereo")) {
        *value = static_cast<double>(kLaneMode2ch);
        return true;
    }
    if (textEquals(display, "4ch") || textEquals(display, "Quad")) {
        *value = static_cast<double>(kLaneMode4ch);
        return true;
    }
    if (textEquals(display, "6ch")) {
        *value = static_cast<double>(kLaneModeSix);
        return true;
    }
    if (textEquals(display, "Custom")) {
        *value = static_cast<double>(kLaneModeCustom);
        return true;
    }

    *value = static_cast<double>(std::min(roundedUint(std::atof(display)), kLaneModeMax));
    return true;
}

const char* portTypeForChannels(uint32_t channels)
{
    if (channels == 1) {
        return CLAP_PORT_MONO;
    }
    if (channels == 2) {
        return CLAP_PORT_STEREO;
    }
    return nullptr;
}

uint32_t patchWidth(const Plugin& p)
{
    return std::min(p.channelCount, s3g::kLanePatchMaxChannels);
}

uint32_t meshChannelCount(const Plugin& p)
{
    return p.laneMode == kLaneModeCustom ? patchWidth(p) : p.activeChannels;
}

uint32_t topologyGeometryLaneCount(const Plugin& p)
{
    if (p.laneMode != kLaneModeCustom) {
        return std::clamp(p.activeChannels, 1u, patchWidth(p));
    }

    const uint32_t width = patchWidth(p);
    uint32_t activeRows = 0;
    for (uint32_t row = 0; row < width; ++row) {
        if (p.patch.rowMask(row) != 0) {
            ++activeRows;
        }
    }
    return activeRows > 0 ? activeRows : width;
}

double laneNoise(uint32_t channel)
{
    uint32_t x = channel + 1u;
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return (static_cast<double>(x & 0xffffu) / 32767.5) - 1.0;
}

double topologyLaneValue(const Plugin& p, uint32_t channel, uint32_t channels)
{
    const double position = channels <= 1
        ? 0.0
        : (static_cast<double>(channel) / static_cast<double>(channels - 1u)) * 2.0 - 1.0;
    const double skewed = std::clamp(position + p.topologySkew, -1.0, 1.0);
    const double jitter = laneNoise(channel) * p.topologyJitter;
    const double lane = std::clamp(skewed * (1.0 - p.topologyJitter) + jitter, -1.0, 1.0);
    const double directionBias = std::clamp((p.displaceDirX + p.displaceDirY + p.displaceDirZ) / 3.0, -1.0, 1.0);
    return s3g::lerp(lane, directionBias, p.displaceCollapse);
}

double resolveTopologyParam(double base, double spread, double laneValue, double scale, double minValue, double maxValue)
{
    return std::clamp(base + spread * laneValue * scale, minValue, maxValue);
}

void applyParamsToDsp(Plugin& p)
{
    const uint32_t channels = std::max(1u, meshChannelCount(p));
    p.mesh.setAmount(static_cast<float>(p.amount));
    p.mesh.setFeedback(static_cast<float>(p.feedback));
    p.resolvedMix.fill(static_cast<float>(p.mix));

    for (uint32_t ch = 0; ch < channels && ch < kMaxChannelCount; ++ch) {
        const double lane = topologyLaneValue(p, ch, channels);
        const double amount = resolveTopologyParam(p.amount, p.topologySpread, lane, 0.45, 0.0, 1.0);
        const double feedback = resolveTopologyParam(p.feedback, p.topologySpread, lane, 0.18, 0.0, 0.95);
        p.mesh.setChannelAmount(static_cast<int>(ch), static_cast<float>(amount));
        p.mesh.setChannelFeedback(static_cast<int>(ch), static_cast<float>(feedback));
        p.resolvedMix[ch] = static_cast<float>(p.mix);
    }
}

void normalizeLanePatch(Plugin& p)
{
    p.laneMode = std::min(p.laneMode, kLaneModeMax);
    p.patch.setWidth(patchWidth(p));
    if (p.laneMode != kLaneModeCustom) {
        p.inputStart = 1;
        p.outputStart = 1;
        p.clearUnused = p.laneMode != kLaneModeFull;
        switch (p.laneMode) {
        case kLaneMode2ch:
            p.activeChannels = 2;
            break;
        case kLaneMode4ch:
            p.activeChannels = 4;
            break;
        case kLaneModeSix:
            p.activeChannels = 6;
            break;
        case kLaneModeFull:
        default:
            p.activeChannels = p.channelCount;
            break;
        }
        p.patch.setIdentity(p.activeChannels);
    }

    p.inputStart = std::clamp(p.inputStart, 1u, p.channelCount);
    p.outputStart = std::clamp(p.outputStart, 1u, p.channelCount);

    const uint32_t inputCapacity = p.channelCount - p.inputStart + 1;
    const uint32_t outputCapacity = p.channelCount - p.outputStart + 1;
    const uint32_t maxActive = std::max(1u, std::min(inputCapacity, outputCapacity));
    p.activeChannels = std::clamp(p.activeChannels, 1u, maxActive);
}

void prepareMesh(Plugin& p)
{
    p.channelCount = kFixedPorts ? kDefaultChannelCount : clampChannelCount(p.channelCount);
    normalizeLanePatch(p);
    p.mesh.prepare(static_cast<int>(meshChannelCount(p)));
    applyParamsToDsp(p);
}

bool init(const clap_plugin_t*) { return true; }

void destroy(const clap_plugin_t* plugin)
{
#if defined(__APPLE__)
    guiDestroy(plugin);
#endif
    delete self(plugin);
}

bool activate(const clap_plugin_t* plugin, double sampleRate, uint32_t, uint32_t maxFrames)
{
    auto* p = self(plugin);
    p->sampleRate = sampleRate;
    p->maxFrames = maxFrames;
    prepareMesh(*p);
    return true;
}

void deactivate(const clap_plugin_t*) {}
bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}

void reset(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    prepareMesh(*p);
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
    case kLaneModeParamId:
        p.laneMode = std::min(roundedUint(value), kLaneModeMax);
        prepareMesh(p);
        return;
    case kActiveChannelsParamId:
        p.laneMode = kLaneModeCustom;
        p.activeChannels = std::max(1u, roundedUint(value));
        p.patch.setWindow(p.inputStart, p.outputStart, p.activeChannels);
        prepareMesh(p);
        return;
    case kInputStartParamId:
        p.laneMode = kLaneModeCustom;
        p.inputStart = std::max(1u, roundedUint(value));
        p.patch.setWindow(p.inputStart, p.outputStart, p.activeChannels);
        prepareMesh(p);
        return;
    case kOutputStartParamId:
        p.laneMode = kLaneModeCustom;
        p.outputStart = std::max(1u, roundedUint(value));
        p.patch.setWindow(p.inputStart, p.outputStart, p.activeChannels);
        prepareMesh(p);
        return;
    case kClearUnusedParamId:
        p.clearUnused = value >= 0.5;
        break;
    case kTopologySpreadParamId:
        p.topologySpread = clamp01(value);
        break;
    case kTopologySkewParamId:
        p.topologySkew = clampBipolar(value);
        break;
    case kTopologyJitterParamId:
        p.topologyJitter = clamp01(value);
        break;
    case kDisplaceCollapseParamId:
        p.displaceCollapse = clamp01(value);
        break;
    case kDisplaceDirXParamId:
        p.displaceDirX = clampBipolar(value);
        break;
    case kDisplaceDirYParamId:
        p.displaceDirY = clampBipolar(value);
        break;
    case kDisplaceDirZParamId:
        p.displaceDirZ = clampBipolar(value);
        break;
    case kDisplaceTwistParamId:
        p.displaceTwist = clampBipolar(value);
        break;
    case kDisplaceFlareParamId:
        p.displaceFlare = clampBipolar(value);
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

void copyAvailableChannels(const clap_audio_buffer_t& input, const clap_audio_buffer_t& output, uint32_t channels, uint32_t frames);

uint32_t runtimeActiveChannels(const Plugin& p, uint32_t inputChannels, uint32_t outputChannels)
{
    if (p.inputStart > inputChannels || p.outputStart > outputChannels) {
        return 0;
    }

    const uint32_t inputCapacity = inputChannels - p.inputStart + 1;
    const uint32_t outputCapacity = outputChannels - p.outputStart + 1;
    return std::min({ p.activeChannels, inputCapacity, outputCapacity, kMaxChannelCount });
}

void clearOutputs(const clap_audio_buffer_t& output, uint32_t channels, uint32_t frames)
{
    for (uint32_t ch = 0; ch < channels; ++ch) {
        if (output.data32 && output.data32[ch]) {
            std::fill(output.data32[ch], output.data32[ch] + frames, 0.0f);
        }
        if (output.data64 && output.data64[ch]) {
            std::fill(output.data64[ch], output.data64[ch] + frames, 0.0);
        }
    }
}

void renderPatchFloat(
    const Plugin& p,
    const std::array<float, kMaxChannelCount>& inFrame,
    const std::array<float, kMaxChannelCount>& wetFrame,
    const clap_audio_buffer_t& output,
    uint32_t outputChannels,
    uint32_t meshChannels,
    uint32_t frame)
{
    for (uint32_t outCh = 0; outCh < outputChannels; ++outCh) {
        float sum = 0.0f;
        bool hasConnection = false;
        const uint64_t outBit = uint64_t { 1 } << outCh;
        for (uint32_t inCh = 0; inCh < meshChannels; ++inCh) {
            if ((p.patch.rowMask(inCh) & outBit) == 0) {
                continue;
            }
            hasConnection = true;
            sum += s3g::lerp(inFrame[inCh], wetFrame[inCh], p.resolvedMix[inCh]);
        }
        if (hasConnection) {
            output.data32[outCh][frame] = sum;
        }
    }
}

void renderPatchDouble(
    const Plugin& p,
    const std::array<float, kMaxChannelCount>& inFrame,
    const std::array<float, kMaxChannelCount>& wetFrame,
    const clap_audio_buffer_t& output,
    uint32_t outputChannels,
    uint32_t meshChannels,
    uint32_t frame)
{
    for (uint32_t outCh = 0; outCh < outputChannels; ++outCh) {
        float sum = 0.0f;
        bool hasConnection = false;
        const uint64_t outBit = uint64_t { 1 } << outCh;
        for (uint32_t inCh = 0; inCh < meshChannels; ++inCh) {
            if ((p.patch.rowMask(inCh) & outBit) == 0) {
                continue;
            }
            hasConnection = true;
            sum += s3g::lerp(inFrame[inCh], wetFrame[inCh], p.resolvedMix[inCh]);
        }
        if (hasConnection) {
            output.data64[outCh][frame] = static_cast<double>(sum);
        }
    }
}

void processFloat(Plugin& p, const clap_audio_buffer_t& input, const clap_audio_buffer_t& output, uint32_t inputChannels, uint32_t outputChannels, uint32_t frames)
{
    std::array<float, kMaxChannelCount> inFrame {};
    std::array<float, kMaxChannelCount> wetFrame {};
    const bool usePatch = p.channelCount <= s3g::kLanePatchMaxChannels;
    const uint32_t activeChannels = usePatch ? meshChannelCount(p) : runtimeActiveChannels(p, inputChannels, outputChannels);

    if (p.clearUnused) {
        clearOutputs(output, outputChannels, frames);
    } else {
        copyAvailableChannels(input, output, std::min(inputChannels, outputChannels), frames);
    }

    if (activeChannels == 0) {
        return;
    }

    const uint32_t inputStart = usePatch ? 0 : p.inputStart - 1;
    const uint32_t outputStart = usePatch ? 0 : p.outputStart - 1;

    for (uint32_t i = 0; i < frames; ++i) {
        for (uint32_t ch = 0; ch < activeChannels; ++ch) {
            const uint32_t inputChannel = inputStart + ch;
            inFrame[ch] = inputChannel < inputChannels ? input.data32[inputChannel][i] : 0.0f;
        }

        p.mesh.processFrame(inFrame.data(), wetFrame.data());

        if (usePatch) {
            renderPatchFloat(p, inFrame, wetFrame, output, outputChannels, activeChannels, i);
        } else {
            for (uint32_t ch = 0; ch < activeChannels; ++ch) {
                output.data32[outputStart + ch][i] = s3g::lerp(inFrame[ch], wetFrame[ch], p.resolvedMix[ch]);
            }
        }
    }
}

void processDouble(Plugin& p, const clap_audio_buffer_t& input, const clap_audio_buffer_t& output, uint32_t inputChannels, uint32_t outputChannels, uint32_t frames)
{
    std::array<float, kMaxChannelCount> inFrame {};
    std::array<float, kMaxChannelCount> wetFrame {};
    const bool usePatch = p.channelCount <= s3g::kLanePatchMaxChannels;
    const uint32_t activeChannels = usePatch ? meshChannelCount(p) : runtimeActiveChannels(p, inputChannels, outputChannels);

    if (p.clearUnused) {
        clearOutputs(output, outputChannels, frames);
    } else {
        copyAvailableChannels(input, output, std::min(inputChannels, outputChannels), frames);
    }

    if (activeChannels == 0) {
        return;
    }

    const uint32_t inputStart = usePatch ? 0 : p.inputStart - 1;
    const uint32_t outputStart = usePatch ? 0 : p.outputStart - 1;

    for (uint32_t i = 0; i < frames; ++i) {
        for (uint32_t ch = 0; ch < activeChannels; ++ch) {
            const uint32_t inputChannel = inputStart + ch;
            inFrame[ch] = inputChannel < inputChannels ? static_cast<float>(input.data64[inputChannel][i]) : 0.0f;
        }

        p.mesh.processFrame(inFrame.data(), wetFrame.data());

        if (usePatch) {
            renderPatchDouble(p, inFrame, wetFrame, output, outputChannels, activeChannels, i);
        } else {
            for (uint32_t ch = 0; ch < activeChannels; ++ch) {
                const float mixed = s3g::lerp(inFrame[ch], wetFrame[ch], p.resolvedMix[ch]);
                output.data64[outputStart + ch][i] = static_cast<double>(mixed);
            }
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
    const uint32_t inputChannels = std::min(input.channel_count, kMaxChannelCount);
    const uint32_t outputChannels = std::min(output.channel_count, kMaxChannelCount);
    const uint32_t commonChannels = std::min(inputChannels, outputChannels);

    if (input.data32 && output.data32) {
        processFloat(*p, input, output, inputChannels, outputChannels, frames);
    } else if (input.data64 && output.data64) {
        processDouble(*p, input, output, inputChannels, outputChannels, frames);
    } else {
        copyAvailableChannels(input, output, commonChannels, frames);
    }

    clearExtraOutputs(output, commonChannels, frames);
    return CLAP_PROCESS_CONTINUE;
}

void onMainThread(const clap_plugin_t*) {}

uint32_t audioPortsCount(const clap_plugin_t*, bool) { return 1; }

void fillPortInfo(uint32_t channels, bool isInput, clap_audio_port_info_t* info)
{
    info->id = isInput ? kInputPortId : kOutputPortId;
    std::snprintf(info->name, sizeof(info->name), "%uch %s", channels, isInput ? "In" : "Out");
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = channels;
    info->port_type = portTypeForChannels(channels);
    info->in_place_pair = isInput ? kOutputPortId : kInputPortId;
}

bool audioPortsGet(const clap_plugin_t* plugin, uint32_t index, bool isInput, clap_audio_port_info_t* info)
{
    if (index != 0 || !info) {
        return false;
    }

    fillPortInfo(self(plugin)->channelCount, isInput, info);
    return true;
}

const clap_plugin_audio_ports_t audioPorts {
    audioPortsCount,
    audioPortsGet
};

uint32_t configCount(const clap_plugin_t*) { return kConfigCount; }

bool configGet(const clap_plugin_t*, uint32_t index, clap_audio_ports_config_t* config)
{
    if (!config || index >= kConfigCount) {
        return false;
    }

    const uint32_t channels = channelsForConfigIndex(index);
    config->id = configIdForChannels(channels);
    if (channels == 1) {
        std::strncpy(config->name, "Mono", sizeof(config->name));
    } else if (channels == 2) {
        std::strncpy(config->name, "Stereo", sizeof(config->name));
    } else {
        std::snprintf(config->name, sizeof(config->name), "%u channel", channels);
    }
    config->input_port_count = 1;
    config->output_port_count = 1;
    config->has_main_input = true;
    config->main_input_channel_count = channels;
    config->main_input_port_type = portTypeForChannels(channels);
    config->has_main_output = true;
    config->main_output_channel_count = channels;
    config->main_output_port_type = portTypeForChannels(channels);
    return true;
}

bool configSelect(const clap_plugin_t* plugin, clap_id configId)
{
    if (!isAdvertisedConfigChannelCount(configId)) {
        return false;
    }

    auto* p = self(plugin);
    p->channelCount = configId;
    prepareMesh(*p);
    return true;
}

const clap_plugin_audio_ports_config_t audioPortsConfig {
    configCount,
    configGet,
    configSelect
};

clap_id currentConfig(const clap_plugin_t* plugin)
{
    const uint32_t channels = self(plugin)->channelCount;
    if (isAdvertisedConfigChannelCount(channels)) {
        return configIdForChannels(channels);
    }
    return CLAP_INVALID_ID;
}

bool configInfoGet(const clap_plugin_t*, clap_id configId, uint32_t portIndex, bool isInput, clap_audio_port_info_t* info)
{
    if (portIndex != 0 || !info) {
        return false;
    }

    if (isAdvertisedConfigChannelCount(configId)) {
        fillPortInfo(configId, isInput, info);
        return true;
    }
    return false;
}

const clap_plugin_audio_ports_config_info_t audioPortsConfigInfo {
    currentConfig,
    configInfoGet
};

bool requestPairIsSupported(const clap_audio_port_configuration_request_t* requests, uint32_t requestCount, uint32_t* channelsOut)
{
    if (!requests || requestCount != 2 || !channelsOut) {
        return false;
    }

    const auto* in = requests[0].is_input ? &requests[0] : &requests[1];
    const auto* out = requests[0].is_input ? &requests[1] : &requests[0];
    if (!in->is_input || out->is_input || in->port_index != 0 || out->port_index != 0) {
        return false;
    }
    if (in->channel_count != out->channel_count || in->channel_count < 1 || in->channel_count > kMaxChannelCount) {
        return false;
    }

    *channelsOut = in->channel_count;
    return true;
}

bool canApplyConfiguration(const clap_plugin_t*, const clap_audio_port_configuration_request_t* requests, uint32_t requestCount)
{
    uint32_t channels = 0;
    return requestPairIsSupported(requests, requestCount, &channels);
}

bool applyConfiguration(const clap_plugin_t* plugin, const clap_audio_port_configuration_request_t* requests, uint32_t requestCount)
{
    uint32_t channels = 0;
    if (!requestPairIsSupported(requests, requestCount, &channels)) {
        return false;
    }

    auto* p = self(plugin);
    p->channelCount = channels;
    prepareMesh(*p);
    return true;
}

const clap_plugin_configurable_audio_ports_t configurableAudioPorts {
    canApplyConfiguration,
    applyConfiguration
};

uint32_t paramsCount(const clap_plugin_t*) { return 17; }

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
    case 3:
        info->id = kLaneModeParamId;
        info->flags = CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_AUTOMATABLE;
        std::strncpy(info->name, "Lane Mode", sizeof(info->name));
        std::strncpy(info->module, "Lane Patch", sizeof(info->module));
        info->min_value = static_cast<double>(kLaneModeFull);
        info->max_value = static_cast<double>(kLaneModeCustom);
        info->default_value = static_cast<double>(kLaneModeFull);
        return true;
    case 4:
        info->id = kActiveChannelsParamId;
        info->flags = CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_AUTOMATABLE;
        std::strncpy(info->name, "Active Channels", sizeof(info->name));
        std::strncpy(info->module, "Lane Patch", sizeof(info->module));
        info->min_value = 1.0;
        info->max_value = static_cast<double>(kMaxChannelCount);
        info->default_value = static_cast<double>(kDefaultChannelCount);
        return true;
    case 5:
        info->id = kInputStartParamId;
        info->flags = CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_AUTOMATABLE;
        std::strncpy(info->name, "Input Start", sizeof(info->name));
        std::strncpy(info->module, "Lane Patch", sizeof(info->module));
        info->min_value = 1.0;
        info->max_value = static_cast<double>(kMaxChannelCount);
        info->default_value = 1.0;
        return true;
    case 6:
        info->id = kOutputStartParamId;
        info->flags = CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_AUTOMATABLE;
        std::strncpy(info->name, "Output Start", sizeof(info->name));
        std::strncpy(info->module, "Lane Patch", sizeof(info->module));
        info->min_value = 1.0;
        info->max_value = static_cast<double>(kMaxChannelCount);
        info->default_value = 1.0;
        return true;
    case 7:
        info->id = kClearUnusedParamId;
        info->flags = CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_AUTOMATABLE;
        std::strncpy(info->name, "Clear Unused", sizeof(info->name));
        std::strncpy(info->module, "Lane Patch", sizeof(info->module));
        info->min_value = 0.0;
        info->max_value = 1.0;
        info->default_value = 0.0;
        return true;
    case 8:
        info->id = kTopologySpreadParamId;
        std::strncpy(info->name, "Topology Spread", sizeof(info->name));
        std::strncpy(info->module, "Topology", sizeof(info->module));
        info->min_value = 0.0;
        info->max_value = 1.0;
        info->default_value = 0.0;
        return true;
    case 9:
        info->id = kTopologySkewParamId;
        std::strncpy(info->name, "Topology Skew", sizeof(info->name));
        std::strncpy(info->module, "Topology", sizeof(info->module));
        info->min_value = -1.0;
        info->max_value = 1.0;
        info->default_value = 0.0;
        return true;
    case 10:
        info->id = kTopologyJitterParamId;
        std::strncpy(info->name, "Topology Jitter", sizeof(info->name));
        std::strncpy(info->module, "Topology", sizeof(info->module));
        info->min_value = 0.0;
        info->max_value = 1.0;
        info->default_value = 0.0;
        return true;
    case 11:
        info->id = kDisplaceCollapseParamId;
        std::strncpy(info->name, "Geometry Collapse", sizeof(info->name));
        std::strncpy(info->module, "Displacement", sizeof(info->module));
        info->min_value = 0.0;
        info->max_value = 1.0;
        info->default_value = 0.0;
        return true;
    case 12:
        info->id = kDisplaceDirXParamId;
        std::strncpy(info->name, "Collapse X", sizeof(info->name));
        std::strncpy(info->module, "Displacement", sizeof(info->module));
        info->min_value = -1.0;
        info->max_value = 1.0;
        info->default_value = 0.0;
        return true;
    case 13:
        info->id = kDisplaceDirYParamId;
        std::strncpy(info->name, "Collapse Y", sizeof(info->name));
        std::strncpy(info->module, "Displacement", sizeof(info->module));
        info->min_value = -1.0;
        info->max_value = 1.0;
        info->default_value = 0.0;
        return true;
    case 14:
        info->id = kDisplaceDirZParamId;
        std::strncpy(info->name, "Collapse Z", sizeof(info->name));
        std::strncpy(info->module, "Displacement", sizeof(info->module));
        info->min_value = -1.0;
        info->max_value = 1.0;
        info->default_value = 1.0;
        return true;
    case 15:
        info->id = kDisplaceTwistParamId;
        std::strncpy(info->name, "Geometry Twist", sizeof(info->name));
        std::strncpy(info->module, "Displacement", sizeof(info->module));
        info->min_value = -1.0;
        info->max_value = 1.0;
        info->default_value = 0.0;
        return true;
    case 16:
        info->id = kDisplaceFlareParamId;
        std::strncpy(info->name, "Geometry Flare", sizeof(info->name));
        std::strncpy(info->module, "Displacement", sizeof(info->module));
        info->min_value = -1.0;
        info->max_value = 1.0;
        info->default_value = 0.0;
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
    case kLaneModeParamId:
        *value = static_cast<double>(p->laneMode);
        return true;
    case kActiveChannelsParamId:
        *value = static_cast<double>(p->activeChannels);
        return true;
    case kInputStartParamId:
        *value = static_cast<double>(p->inputStart);
        return true;
    case kOutputStartParamId:
        *value = static_cast<double>(p->outputStart);
        return true;
    case kClearUnusedParamId:
        *value = p->clearUnused ? 1.0 : 0.0;
        return true;
    case kTopologySpreadParamId:
        *value = p->topologySpread;
        return true;
    case kTopologySkewParamId:
        *value = p->topologySkew;
        return true;
    case kTopologyJitterParamId:
        *value = p->topologyJitter;
        return true;
    case kDisplaceCollapseParamId:
        *value = p->displaceCollapse;
        return true;
    case kDisplaceDirXParamId:
        *value = p->displaceDirX;
        return true;
    case kDisplaceDirYParamId:
        *value = p->displaceDirY;
        return true;
    case kDisplaceDirZParamId:
        *value = p->displaceDirZ;
        return true;
    case kDisplaceTwistParamId:
        *value = p->displaceTwist;
        return true;
    case kDisplaceFlareParamId:
        *value = p->displaceFlare;
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
    case kTopologySpreadParamId:
    case kTopologyJitterParamId:
    case kDisplaceCollapseParamId:
        std::snprintf(display, size, "%.1f %%", value * 100.0);
        return true;
    case kTopologySkewParamId:
    case kDisplaceDirXParamId:
    case kDisplaceDirYParamId:
    case kDisplaceDirZParamId:
    case kDisplaceTwistParamId:
    case kDisplaceFlareParamId:
        std::snprintf(display, size, "%+.1f %%", value * 100.0);
        return true;
    case kLaneModeParamId:
        std::snprintf(display, size, "%s", laneModeName(std::min(roundedUint(value), kLaneModeMax)));
        return true;
    case kActiveChannelsParamId:
    case kInputStartParamId:
    case kOutputStartParamId:
        std::snprintf(display, size, "%u", roundedUint(value));
        return true;
    case kClearUnusedParamId:
        std::snprintf(display, size, "%u", value >= 0.5 ? 1u : 0u);
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
    case kTopologySpreadParamId:
    case kTopologyJitterParamId:
    case kDisplaceCollapseParamId:
        *value = std::atof(display);
        if (*value > 1.0) {
            *value *= 0.01;
        }
        return true;
    case kTopologySkewParamId:
    case kDisplaceDirXParamId:
    case kDisplaceDirYParamId:
    case kDisplaceDirZParamId:
    case kDisplaceTwistParamId:
    case kDisplaceFlareParamId:
        *value = std::atof(display);
        if (*value < -1.0 || *value > 1.0) {
            *value *= 0.01;
        }
        return true;
    case kLaneModeParamId:
        return laneModeValueFromText(display, value);
    case kActiveChannelsParamId:
    case kInputStartParamId:
    case kOutputStartParamId:
    case kClearUnusedParamId:
        *value = std::atof(display);
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
    SavedState state {};
    state.version = kStateVersion;
    state.channelCount = p->channelCount;
    state.activeChannels = p->activeChannels;
    state.inputStart = p->inputStart;
    state.outputStart = p->outputStart;
    state.clearUnused = p->clearUnused ? 1u : 0u;
    state.laneMode = p->laneMode;
    for (uint32_t row = 0; row < s3g::kLanePatchMaxChannels; ++row) {
        state.patchRows[row] = p->patch.rowMask(row);
    }
    state.amount = p->amount;
    state.feedback = p->feedback;
    state.mix = p->mix;
    state.topologySpread = p->topologySpread;
    state.topologySkew = p->topologySkew;
    state.topologyJitter = p->topologyJitter;
    state.displaceCollapse = p->displaceCollapse;
    state.displaceDirX = p->displaceDirX;
    state.displaceDirY = p->displaceDirY;
    state.displaceDirZ = p->displaceDirZ;
    state.displaceTwist = p->displaceTwist;
    state.displaceFlare = p->displaceFlare;
    return writeAll(stream, &state, sizeof(state));
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) {
        return false;
    }

    uint32_t version = 0;
    if (!readAll(stream, &version, sizeof(version))) {
        return false;
    }

    SavedState state {};
    state.version = version;
    if (version == kStateVersion) {
        auto* cursor = reinterpret_cast<uint8_t*>(&state) + sizeof(state.version);
        if (!readAll(stream, cursor, sizeof(state) - sizeof(state.version))) {
            return false;
        }
    } else if (version == kPreviousStateVersion) {
        SavedStateV5 oldState {};
        oldState.version = version;
        auto* cursor = reinterpret_cast<uint8_t*>(&oldState) + sizeof(oldState.version);
        if (!readAll(stream, cursor, sizeof(oldState) - sizeof(oldState.version))) {
            return false;
        }
        state.channelCount = oldState.channelCount;
        state.activeChannels = oldState.activeChannels;
        state.inputStart = oldState.inputStart;
        state.outputStart = oldState.outputStart;
        state.clearUnused = oldState.clearUnused;
        state.laneMode = oldState.laneMode;
        for (uint32_t row = 0; row < s3g::kLanePatchMaxChannels; ++row) {
            state.patchRows[row] = oldState.patchRows[row];
        }
        state.amount = oldState.amount;
        state.feedback = oldState.feedback;
        state.mix = oldState.mix;
        state.topologySpread = oldState.topologySpread;
        state.topologySkew = oldState.topologySkew;
        state.topologyJitter = oldState.topologyJitter;
    } else if (version == kLegacyStateVersion) {
        SavedStateV4 oldState {};
        oldState.version = version;
        auto* cursor = reinterpret_cast<uint8_t*>(&oldState) + sizeof(oldState.version);
        if (!readAll(stream, cursor, sizeof(oldState) - sizeof(oldState.version))) {
            return false;
        }
        state.channelCount = oldState.channelCount;
        state.activeChannels = oldState.activeChannels;
        state.inputStart = oldState.inputStart;
        state.outputStart = oldState.outputStart;
        state.clearUnused = oldState.clearUnused;
        state.laneMode = oldState.laneMode;
        for (uint32_t row = 0; row < s3g::kLanePatchMaxChannels; ++row) {
            state.patchRows[row] = oldState.patchRows[row];
        }
        state.amount = oldState.amount;
        state.feedback = oldState.feedback;
        state.mix = oldState.mix;
    } else {
        return false;
    }

    auto* p = self(plugin);
    p->channelCount = kFixedPorts ? kDefaultChannelCount : clampChannelCount(state.channelCount);
    p->activeChannels = std::max(1u, state.activeChannels);
    p->inputStart = std::max(1u, state.inputStart);
    p->outputStart = std::max(1u, state.outputStart);
    p->clearUnused = state.clearUnused != 0;
    p->laneMode = std::min(state.laneMode, kLaneModeMax);
    p->patch.setWidth(patchWidth(*p));
    for (uint32_t row = 0; row < s3g::kLanePatchMaxChannels; ++row) {
        p->patch.setRowMask(row, state.patchRows[row]);
    }
    p->amount = clamp01(state.amount);
    p->feedback = clampFeedback(state.feedback);
    p->mix = clamp01(state.mix);
    p->topologySpread = clamp01(state.topologySpread);
    p->topologySkew = clampBipolar(state.topologySkew);
    p->topologyJitter = clamp01(state.topologyJitter);
    p->displaceCollapse = clamp01(state.displaceCollapse);
    p->displaceDirX = clampBipolar(state.displaceDirX);
    p->displaceDirY = clampBipolar(state.displaceDirY);
    p->displaceDirZ = clampBipolar(state.displaceDirZ);
    p->displaceTwist = clampBipolar(state.displaceTwist);
    p->displaceFlare = clampBipolar(state.displaceFlare);
    prepareMesh(*p);
    return true;
}

const clap_plugin_state_t state {
    stateSave,
    stateLoad
};

#if defined(__APPLE__)

void setLaneModeFromGui(Plugin& p, uint32_t mode)
{
    p.laneMode = std::min(mode, kLaneModeMax);
    prepareMesh(p);
}

void togglePatchCellFromGui(Plugin& p, uint32_t input, uint32_t output)
{
    p.laneMode = kLaneModeCustom;
    p.clearUnused = true;
    p.patch.setWidth(patchWidth(p));
    p.patch.toggle(input, output);
    p.mesh.prepare(static_cast<int>(meshChannelCount(p)));
    applyParamsToDsp(p);
}

} // namespace

@interface S3GLanePatchView : NSView {
    void* _plugin;
    int _dragSlider;
    bool _dragTopologyView;
    NSPoint _lastDragPoint;
    double _viewYaw;
    double _viewPitch;
}
- (id)initWithPlugin:(void*)plugin;
- (void)updateSliderAtPoint:(NSPoint)pt;
@end

static NSColor* s3gColor(int rgb)
{
    return [NSColor colorWithCalibratedRed:((rgb >> 16) & 0xff) / 255.0
                                     green:((rgb >> 8) & 0xff) / 255.0
                                      blue:(rgb & 0xff) / 255.0
                                     alpha:1.0];
}

@implementation S3GLanePatchView

- (id)initWithPlugin:(void*)plugin
{
    self = [super initWithFrame:NSMakeRect(0, 0, 1000, 700)];
    if (self) {
        _plugin = plugin;
        _dragSlider = -1;
        _dragTopologyView = false;
        _lastDragPoint = NSMakePoint(0, 0);
        _viewYaw = -0.45;
        _viewPitch = 0.38;
    }
    return self;
}

- (BOOL)isFlipped
{
    return YES;
}

- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    auto* p = static_cast<Plugin*>(_plugin);
    NSColor* bg = s3gColor(0x0c0c0c);
    NSColor* strip = s3gColor(0x131313);
    NSColor* cellBg = s3gColor(0x1d1d1d);
    NSColor* grid = s3gColor(0x636363);
    NSColor* dim = s3gColor(0x9e9e9e);
    NSColor* text = s3gColor(0xf0f0f0);
    NSColor* accent = s3gColor(0xd1d1d1);
    NSColor* fillColor = s3gColor(0x8f8f8f);

    [bg setFill];
    NSRectFill([self bounds]);

    NSFont* mono = [NSFont fontWithName:@"Menlo" size:10.0] ?: [NSFont monospacedSystemFontOfSize:10.0 weight:NSFontWeightRegular];
    NSFont* monoBold = [NSFont fontWithName:@"Menlo-Bold" size:10.0] ?: [NSFont monospacedSystemFontOfSize:10.0 weight:NSFontWeightBold];
    NSFont* titleFont = [NSFont fontWithName:@"Menlo" size:10.5] ?: [NSFont monospacedSystemFontOfSize:10.5 weight:NSFontWeightRegular];
    NSDictionary* labelAttrs = @{
        NSForegroundColorAttributeName: text,
        NSFontAttributeName: monoBold
    };
    NSDictionary* smallAttrs = @{
        NSForegroundColorAttributeName: dim,
        NSFontAttributeName: mono
    };
    NSDictionary* sectionAttrs = @{
        NSForegroundColorAttributeName: accent,
        NSFontAttributeName: monoBold
    };
    NSDictionary* titleAttrs = @{
        NSForegroundColorAttributeName: text,
        NSFontAttributeName: titleFont
    };

    [@"s3g DIFFUSION MESH" drawAtPoint:NSMakePoint(18, 13) withAttributes:titleAttrs];
    [@"8CH" drawAtPoint:NSMakePoint(946, 14) withAttributes:smallAttrs];

    NSRect meshPanel = NSMakeRect(644, 34, 344, 88);
    [cellBg setFill];
    NSRectFill(meshPanel);
    [grid setStroke];
    NSFrameRect(meshPanel);
    [strip setFill];
    NSRectFill(NSMakeRect(644, 34, 344, 21));
    [accent setFill];
    NSRectFill(NSMakeRect(644, 34, 344, 2));
    [@"ENGINE" drawAtPoint:NSMakePoint(650, 39) withAttributes:sectionAttrs];

    const char* sliderNames[] = { "AMT", "FDBK", "MIX" };
    const double sliderValues[] = {
        p->amount,
        p->feedback / 0.95,
        p->mix
    };
    for (uint32_t i = 0; i < 3; ++i) {
        const CGFloat y = 64.0 + i * 18.0;
        NSString* name = [NSString stringWithUTF8String:sliderNames[i]];
        [name drawAtPoint:NSMakePoint(654, y - 2) withAttributes:smallAttrs];

        NSRect track = NSMakeRect(750, y + 1, 150, 9);
        [strip setFill];
        NSRectFill(track);
        [grid setStroke];
        NSFrameRect(track);

        const CGFloat norm = static_cast<CGFloat>(std::clamp(sliderValues[i], 0.0, 1.0));
        NSRect fill = NSInsetRect(track, 1.0, 1.0);
        fill.size.width = std::max<CGFloat>(1.0, fill.size.width * norm);
        [fillColor setFill];
        NSRectFill(fill);

        const CGFloat handleX = std::clamp(track.origin.x + track.size.width * norm - 1.5,
                                           track.origin.x + 1.0,
                                           track.origin.x + track.size.width - 4.0);
        [text setFill];
        NSRectFill(NSMakeRect(handleX, track.origin.y - 2.0, 3.0, track.size.height + 4.0));

        NSString* value = [NSString stringWithFormat:@"%3.0f%%", sliderValues[i] * 100.0];
        [value drawAtPoint:NSMakePoint(920, y - 2) withAttributes:smallAttrs];
    }

    NSRect topologyPanel = NSMakeRect(12, 34, 620, 654);
    [cellBg setFill];
    NSRectFill(topologyPanel);
    [grid setStroke];
    NSFrameRect(topologyPanel);
    [strip setFill];
    NSRectFill(NSMakeRect(12, 34, 620, 21));
    [accent setFill];
    NSRectFill(NSMakeRect(12, 34, 620, 2));
    [@"TOPOLOGY" drawAtPoint:NSMakePoint(18, 39) withAttributes:sectionAttrs];

    const uint32_t visualLanes = std::min<uint32_t>(8u, std::max(1u, topologyGeometryLaneCount(*p)));
    const CGFloat fieldX = 22.0;
    const CGFloat fieldY = 62.0;
    const CGFloat fieldW = 600.0;
    const CGFloat fieldH = 600.0;
    [strip setFill];
    NSRectFill(NSMakeRect(fieldX, fieldY, fieldW, fieldH));
    [grid setStroke];
    NSFrameRect(NSMakeRect(fieldX, fieldY, fieldW, fieldH));

    auto projectTopology = [&](double x, double y, double z) -> NSPoint {
        const double cy = std::cos(_viewYaw);
        const double sy = std::sin(_viewYaw);
        const double cp = std::cos(_viewPitch);
        const double sp = std::sin(_viewPitch);
        const double xr = x * cy - z * sy;
        const double zr = x * sy + z * cy;
        const double yr = y * cp - zr * sp;
        const double zz = y * sp + zr * cp;
        const double scale = 0.78 + zz * 0.08;
        return NSMakePoint(fieldX + fieldW * 0.5 + static_cast<CGFloat>(xr * fieldW * 0.29 * scale),
                           fieldY + fieldH * 0.52 - static_cast<CGFloat>(yr * fieldH * 0.40 * scale));
    };

    auto baseTopologyPoint = [&](uint32_t lane, uint32_t count) -> std::array<double, 3> {
        if (count == 1) {
            return { 0.0, 0.0, 0.0 };
        }
        if (count == 2) {
            return { lane == 0 ? -1.0 : 1.0, 0.0, 0.0 };
        }
        if (count == 4) {
            constexpr double k = 0.57735026919;
            const double coords[4][3] = {
                { k, k, k },
                { -k, -k, k },
                { -k, k, -k },
                { k, -k, -k }
            };
            return { coords[lane][0], coords[lane][1], coords[lane][2] };
        }
        if (count == 5) {
            if (lane == 0) {
                return { 0.0, 1.0, 0.0 };
            }
            if (lane == 1) {
                return { 0.0, -1.0, 0.0 };
            }
            const double a = (static_cast<double>(lane - 2u) / 3.0) * 2.0 * M_PI;
            return { std::cos(a), 0.0, std::sin(a) };
        }
        if (count == 6) {
            const double coords[6][3] = {
                { 1.0, 0.0, 0.0 },
                { -1.0, 0.0, 0.0 },
                { 0.0, 1.0, 0.0 },
                { 0.0, -1.0, 0.0 },
                { 0.0, 0.0, 1.0 },
                { 0.0, 0.0, -1.0 }
            };
            return { coords[lane][0], coords[lane][1], coords[lane][2] };
        }
        if (count == 7) {
            if (lane == 0) {
                return { 0.0, 1.0, 0.0 };
            }
            if (lane == 1) {
                return { 0.0, -1.0, 0.0 };
            }
            const double a = (static_cast<double>(lane - 2u) / 5.0) * 2.0 * M_PI;
            return { std::cos(a), 0.0, std::sin(a) };
        }
        if (count == 8) {
            constexpr double k = 0.57735026919;
            const double coords[8][3] = {
                { -k, -k, -k },
                { k, -k, -k },
                { k, k, -k },
                { -k, k, -k },
                { -k, -k, k },
                { k, -k, k },
                { k, k, k },
                { -k, k, k }
            };
            return { coords[lane][0], coords[lane][1], coords[lane][2] };
        }

        const double offset = 2.0 / static_cast<double>(count);
        const double y = 1.0 - (static_cast<double>(lane) + 0.5) * offset;
        const double r = std::sqrt(std::max(0.0, 1.0 - y * y));
        const double a = static_cast<double>(lane) * M_PI * (3.0 - std::sqrt(5.0));
        return { std::cos(a) * r, y, std::sin(a) * r };
    };

    std::array<NSPoint, 8> nodePoints {};
    std::array<std::array<double, 3>, 8> nodeXYZ {};
    double dirX = p->displaceDirX;
    double dirY = p->displaceDirY;
    double dirZ = p->displaceDirZ;
    double dirLen = std::sqrt(dirX * dirX + dirY * dirY + dirZ * dirZ);
    if (dirLen < 0.001) {
        dirX = 0.0;
        dirY = 0.0;
        dirZ = 1.0;
        dirLen = 1.0;
    }
    dirX /= dirLen;
    dirY /= dirLen;
    dirZ /= dirLen;

    auto deformPoint = [&](double x, double y, double z) -> std::array<double, 3> {
        const double projection = x * dirX + y * dirY + z * dirZ;
        const double angle = p->displaceTwist * projection * M_PI;
        const double c = std::cos(angle);
        const double s = std::sin(angle);
        const double crossX = dirY * z - dirZ * y;
        const double crossY = dirZ * x - dirX * z;
        const double crossZ = dirX * y - dirY * x;
        const double twistX = x * c + crossX * s + dirX * projection * (1.0 - c);
        const double twistY = y * c + crossY * s + dirY * projection * (1.0 - c);
        const double twistZ = z * c + crossZ * s + dirZ * projection * (1.0 - c);
        const double flare = 1.0 + p->displaceFlare * projection * 0.65;
        const double flaredX = twistX * flare;
        const double flaredY = twistY * flare;
        const double flaredZ = twistZ * flare;
        return {
            s3g::lerp(flaredX, dirX, p->displaceCollapse),
            s3g::lerp(flaredY, dirY, p->displaceCollapse),
            s3g::lerp(flaredZ, dirZ, p->displaceCollapse)
        };
    };

    for (uint32_t lane = 0; lane < visualLanes; ++lane) {
        const double topo = topologyLaneValue(*p, lane, visualLanes);
        const double amount = resolveTopologyParam(p->amount, p->topologySpread, topo, 0.45, 0.0, 1.0);
        const double feedback = resolveTopologyParam(p->feedback, p->topologySpread, topo, 0.18, 0.0, 0.95) / 0.95;
        const auto base = baseTopologyPoint(lane, visualLanes);
        const double radius = 0.70 + amount * 0.38;
        const double x = base[0] * radius;
        const double y = base[1] * radius + (feedback - 0.5) * 0.45;
        const double z = base[2] * radius;
        nodeXYZ[lane] = deformPoint(x, y, z);
        nodePoints[lane] = projectTopology(nodeXYZ[lane][0], nodeXYZ[lane][1], nodeXYZ[lane][2]);
    }

    [fillColor setStroke];
    auto strokeEdge = [&](uint32_t a, uint32_t b) {
        if (a < visualLanes && b < visualLanes) {
            [NSBezierPath strokeLineFromPoint:nodePoints[a] toPoint:nodePoints[b]];
        }
    };

    if (visualLanes == 8) {
        const uint32_t edges[][2] = {
            { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 },
            { 4, 5 }, { 5, 6 }, { 6, 7 }, { 7, 4 },
            { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 }
        };
        for (const auto& edge : edges) {
            strokeEdge(edge[0], edge[1]);
        }
    } else if (visualLanes == 6) {
        const uint32_t edges[][2] = {
            { 0, 2 }, { 0, 3 }, { 0, 4 }, { 0, 5 },
            { 1, 2 }, { 1, 3 }, { 1, 4 }, { 1, 5 },
            { 2, 4 }, { 4, 3 }, { 3, 5 }, { 5, 2 }
        };
        for (const auto& edge : edges) {
            strokeEdge(edge[0], edge[1]);
        }
    } else if (visualLanes == 4) {
        const uint32_t edges[][2] = {
            { 0, 1 }, { 0, 2 }, { 0, 3 },
            { 1, 2 }, { 1, 3 }, { 2, 3 }
        };
        for (const auto& edge : edges) {
            strokeEdge(edge[0], edge[1]);
        }
    } else {
        NSBezierPath* topologyPath = [NSBezierPath bezierPath];
        [topologyPath setLineWidth:1.3];
        for (uint32_t lane = 0; lane < visualLanes; ++lane) {
            if (lane == 0) {
                [topologyPath moveToPoint:nodePoints[lane]];
            } else {
                [topologyPath lineToPoint:nodePoints[lane]];
            }
        }
        if (visualLanes > 2) {
            [topologyPath closePath];
        }
        [topologyPath stroke];
    }

    for (uint32_t lane = 0; lane < visualLanes; ++lane) {
        const CGFloat r = 5.0;
        [accent setFill];
        NSRectFill(NSMakeRect(nodePoints[lane].x - r, nodePoints[lane].y - r, r * 2.0, r * 2.0));
    }
    [@"FIELD" drawAtPoint:NSMakePoint(22, fieldY - 1) withAttributes:smallAttrs];
    NSString* topologyName = visualLanes == 8 ? @"CUBE PARAM SPACE"
        : visualLanes == 6 ? @"OCTAHEDRON PARAM SPACE"
        : visualLanes == 4 ? @"TETRA PARAM SPACE"
        : @"SPHERE PARAM SPACE";
    [topologyName drawAtPoint:NSMakePoint(fieldX + fieldW - 188, fieldY + 10) withAttributes:smallAttrs];
    [@"R=AMT Y=FDBK" drawAtPoint:NSMakePoint(fieldX + fieldW - 188, fieldY + 25) withAttributes:smallAttrs];

    [@"META" drawAtPoint:NSMakePoint(650, 137) withAttributes:sectionAttrs];
    const char* topologyNames[] = { "SPRD", "SKEW", "JIT" };
    const double topologyValues[] = {
        p->topologySpread,
        (p->topologySkew + 1.0) * 0.5,
        p->topologyJitter
    };
    for (uint32_t i = 0; i < 3; ++i) {
        const CGFloat y = 166.0 + i * 18.0;
        NSString* name = [NSString stringWithUTF8String:topologyNames[i]];
        [name drawAtPoint:NSMakePoint(654, y - 2) withAttributes:smallAttrs];

        NSRect track = NSMakeRect(750, y + 1, 150, 9);
        [strip setFill];
        NSRectFill(track);
        [grid setStroke];
        NSFrameRect(track);

        const CGFloat norm = static_cast<CGFloat>(std::clamp(topologyValues[i], 0.0, 1.0));
        NSRect fill = NSInsetRect(track, 1.0, 1.0);
        fill.size.width = std::max<CGFloat>(1.0, fill.size.width * norm);
        [fillColor setFill];
        NSRectFill(fill);

        const CGFloat handleX = std::clamp(track.origin.x + track.size.width * norm - 1.5,
                                           track.origin.x + 1.0,
                                           track.origin.x + track.size.width - 4.0);
        [text setFill];
        NSRectFill(NSMakeRect(handleX, track.origin.y - 2.0, 3.0, track.size.height + 4.0));

        NSString* value = i == 1
            ? [NSString stringWithFormat:@"%+3.0f%%", p->topologySkew * 100.0]
            : [NSString stringWithFormat:@"%3.0f%%", topologyValues[i] * 100.0];
        [value drawAtPoint:NSMakePoint(920, y - 2) withAttributes:smallAttrs];
    }

    [@"DISPLACE" drawAtPoint:NSMakePoint(650, 224) withAttributes:sectionAttrs];
    const char* displaceNames[] = { "COLL", "DIRX", "DIRY", "DIRZ", "TWST", "FLAR" };
    const double displaceValues[] = {
        p->displaceCollapse,
        (p->displaceDirX + 1.0) * 0.5,
        (p->displaceDirY + 1.0) * 0.5,
        (p->displaceDirZ + 1.0) * 0.5,
        (p->displaceTwist + 1.0) * 0.5,
        (p->displaceFlare + 1.0) * 0.5
    };
    for (uint32_t i = 0; i < 6; ++i) {
        const CGFloat y = 247.0 + i * 18.0;
        NSString* name = [NSString stringWithUTF8String:displaceNames[i]];
        [name drawAtPoint:NSMakePoint(654, y - 2) withAttributes:smallAttrs];

        NSRect track = NSMakeRect(750, y + 1, 150, 9);
        [strip setFill];
        NSRectFill(track);
        [grid setStroke];
        NSFrameRect(track);

        const CGFloat norm = static_cast<CGFloat>(std::clamp(displaceValues[i], 0.0, 1.0));
        NSRect fill = NSInsetRect(track, 1.0, 1.0);
        fill.size.width = std::max<CGFloat>(1.0, fill.size.width * norm);
        [fillColor setFill];
        NSRectFill(fill);

        const CGFloat handleX = std::clamp(track.origin.x + track.size.width * norm - 1.5,
                                           track.origin.x + 1.0,
                                           track.origin.x + track.size.width - 4.0);
        [text setFill];
        NSRectFill(NSMakeRect(handleX, track.origin.y - 2.0, 3.0, track.size.height + 4.0));

        NSString* value = i == 0
            ? [NSString stringWithFormat:@"%3.0f%%", p->displaceCollapse * 100.0]
            : [NSString stringWithFormat:@"%+3.0f%%", (displaceValues[i] * 2.0 - 1.0) * 100.0];
        [value drawAtPoint:NSMakePoint(920, y - 2) withAttributes:smallAttrs];
    }

    const CGFloat left = 718.0;
    const CGFloat top = 404.0;
    const CGFloat cell = 24.0;
    NSRect matrixPanel = NSMakeRect(644, 366, 344, 284);
    [cellBg setFill];
    NSRectFill(matrixPanel);
    [grid setStroke];
    NSFrameRect(matrixPanel);
    [strip setFill];
    NSRectFill(NSMakeRect(644, 366, 344, 21));
    [accent setFill];
    NSRectFill(NSMakeRect(644, 366, 344, 2));
    [@"PATCH MATRIX" drawAtPoint:NSMakePoint(650, 371) withAttributes:sectionAttrs];

    for (uint32_t i = 0; i < 8; ++i) {
        NSString* outLabel = [NSString stringWithFormat:@"%u", i + 1];
        [outLabel drawAtPoint:NSMakePoint(left + i * cell + 8, top - 16) withAttributes:smallAttrs];
        NSString* inLabel = [NSString stringWithFormat:@"I%u", i + 1];
        [inLabel drawAtPoint:NSMakePoint(654, top + i * cell + 5) withAttributes:smallAttrs];
    }

    for (uint32_t in = 0; in < 8; ++in) {
        for (uint32_t out = 0; out < 8; ++out) {
            const bool connected = p->patch.connected(in, out);
            NSRect r = NSMakeRect(left + out * cell, top + in * cell, cell - 4, cell - 4);
            [strip setFill];
            NSRectFill(r);
            [grid setStroke];
            NSFrameRect(r);
            if (connected) {
                [accent setFill];
                NSRectFill(NSInsetRect(r, 5, 5));
            }
        }
    }

    NSString* clearText = p->clearUnused ? @"UNUSED: CLEAR" : @"UNUSED: PASS";
    [clearText drawAtPoint:NSMakePoint(650, 656) withAttributes:smallAttrs];
}

- (void)updateSliderAtPoint:(NSPoint)pt
{
    auto* p = static_cast<Plugin*>(_plugin);
    if (_dragSlider < 0 || _dragSlider > 11) {
        return;
    }
    const double norm = std::clamp((pt.x - 750.0) / 150.0, 0.0, 1.0);
    if (_dragSlider == 0) {
        p->amount = norm;
    } else if (_dragSlider == 1) {
        p->feedback = norm * 0.95;
    } else if (_dragSlider == 2) {
        p->mix = norm;
    } else if (_dragSlider == 3) {
        p->topologySpread = norm;
    } else if (_dragSlider == 4) {
        p->topologySkew = norm * 2.0 - 1.0;
    } else if (_dragSlider == 5) {
        p->topologyJitter = norm;
    } else if (_dragSlider == 6) {
        p->displaceCollapse = norm;
    } else if (_dragSlider == 7) {
        p->displaceDirX = norm * 2.0 - 1.0;
    } else if (_dragSlider == 8) {
        p->displaceDirY = norm * 2.0 - 1.0;
    } else if (_dragSlider == 9) {
        p->displaceDirZ = norm * 2.0 - 1.0;
    } else if (_dragSlider == 10) {
        p->displaceTwist = norm * 2.0 - 1.0;
    } else {
        p->displaceFlare = norm * 2.0 - 1.0;
    }
    applyParamsToDsp(*p);
    [self setNeedsDisplay:YES];
}

- (void)mouseDown:(NSEvent*)event
{
    NSPoint pt = [self convertPoint:[event locationInWindow] fromView:nil];

    for (uint32_t i = 0; i < 3; ++i) {
        NSRect r = NSMakeRect(744, 58.0 + i * 18.0, 164, 20);
        if (NSPointInRect(pt, r)) {
            _dragSlider = static_cast<int>(i);
            [self updateSliderAtPoint:pt];
            return;
        }
    }

    NSRect topologyView = NSMakeRect(22.0, 62.0, 600.0, 600.0);
    if (NSPointInRect(pt, topologyView)) {
        _dragTopologyView = true;
        _lastDragPoint = pt;
        return;
    }

    for (uint32_t i = 0; i < 3; ++i) {
        NSRect r = NSMakeRect(744, 160.0 + i * 18.0, 164, 20);
        if (NSPointInRect(pt, r)) {
            _dragSlider = static_cast<int>(i + 3);
            [self updateSliderAtPoint:pt];
            return;
        }
    }

    for (uint32_t i = 0; i < 6; ++i) {
        NSRect r = NSMakeRect(744, 241.0 + i * 18.0, 164, 20);
        if (NSPointInRect(pt, r)) {
            _dragSlider = static_cast<int>(i + 6);
            [self updateSliderAtPoint:pt];
            return;
        }
    }

    const CGFloat left = 718.0;
    const CGFloat top = 404.0;
    const CGFloat cell = 24.0;
    if (pt.x >= left && pt.y >= top && pt.x < left + cell * 8 && pt.y < top + cell * 8) {
        auto* p = static_cast<Plugin*>(_plugin);
        const uint32_t out = static_cast<uint32_t>((pt.x - left) / cell);
        const uint32_t in = static_cast<uint32_t>((pt.y - top) / cell);
        togglePatchCellFromGui(*p, in, out);
        [self setNeedsDisplay:YES];
    }
}

- (void)mouseDragged:(NSEvent*)event
{
    NSPoint pt = [self convertPoint:[event locationInWindow] fromView:nil];
    if (_dragTopologyView) {
        const CGFloat dx = pt.x - _lastDragPoint.x;
        const CGFloat dy = pt.y - _lastDragPoint.y;
        _viewYaw += dx * 0.015;
        _viewPitch = std::clamp(_viewPitch + dy * 0.012, -0.75, 0.95);
        _lastDragPoint = pt;
        [self setNeedsDisplay:YES];
        return;
    }
    [self updateSliderAtPoint:pt];
}

- (void)mouseUp:(NSEvent*)event
{
    (void)event;
    _dragSlider = -1;
    _dragTopologyView = false;
}

@end

namespace {

bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool isFloating)
{
    return !isFloating && std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0;
}

bool guiGetPreferredApi(const clap_plugin_t*, const char** api, bool* isFloating)
{
    if (!api || !isFloating) {
        return false;
    }
    *api = CLAP_WINDOW_API_COCOA;
    *isFloating = false;
    return true;
}

bool guiCreate(const clap_plugin_t* plugin, const char* api, bool isFloating)
{
    if (!guiIsApiSupported(plugin, api, isFloating)) {
        return false;
    }
    auto* p = self(plugin);
    if (p->guiView) {
        return true;
    }
    p->guiView = [[S3GLanePatchView alloc] initWithPlugin:p];
    return p->guiView != nullptr;
}

void guiDestroy(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (p->guiView) {
        NSView* view = static_cast<NSView*>(p->guiView);
        [view removeFromSuperview];
        [view release];
        p->guiView = nullptr;
    }
}

bool guiSetScale(const clap_plugin_t*, double) { return true; }

bool guiGetSize(const clap_plugin_t*, uint32_t* width, uint32_t* height)
{
    if (!width || !height) {
        return false;
    }
    *width = 1000;
    *height = 700;
    return true;
}

bool guiCanResize(const clap_plugin_t*) { return false; }
bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t*) { return false; }
bool guiAdjustSize(const clap_plugin_t*, uint32_t*, uint32_t*) { return false; }

bool guiSetSize(const clap_plugin_t* plugin, uint32_t width, uint32_t height)
{
    auto* p = self(plugin);
    if (!p->guiView) {
        return false;
    }
    NSView* view = static_cast<NSView*>(p->guiView);
    [view setFrameSize:NSMakeSize(width, height)];
    return true;
}

bool guiSetParent(const clap_plugin_t* plugin, const clap_window_t* window)
{
    if (!window || std::strcmp(window->api, CLAP_WINDOW_API_COCOA) != 0 || !window->cocoa) {
        return false;
    }
    auto* p = self(plugin);
    if (!p->guiView) {
        return false;
    }
    NSView* parent = static_cast<NSView*>(window->cocoa);
    NSView* view = static_cast<NSView*>(p->guiView);
    [parent addSubview:view];
    [view setFrame:NSMakeRect(0, 0, 1000, 700)];
    return true;
}

bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}

bool guiShow(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (!p->guiView) {
        return false;
    }
    [static_cast<NSView*>(p->guiView) setHidden:NO];
    return true;
}

bool guiHide(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (!p->guiView) {
        return false;
    }
    [static_cast<NSView*>(p->guiView) setHidden:YES];
    return true;
}

const clap_plugin_gui_t gui {
    guiIsApiSupported,
    guiGetPreferredApi,
    guiCreate,
    guiDestroy,
    guiSetScale,
    guiGetSize,
    guiCanResize,
    guiGetResizeHints,
    guiAdjustSize,
    guiSetSize,
    guiSetParent,
    guiSetTransient,
    guiSuggestTitle,
    guiShow,
    guiHide
};

#endif

const void* pluginGetExtension(const clap_plugin_t*, const char* id)
{
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) {
        return &audioPorts;
    }
    if (!kFixedPorts) {
        if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS_CONFIG) == 0) {
            return &audioPortsConfig;
        }
        if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS_CONFIG_INFO) == 0 ||
            std::strcmp(id, CLAP_EXT_AUDIO_PORTS_CONFIG_INFO_COMPAT) == 0) {
            return &audioPortsConfigInfo;
        }
        if (std::strcmp(id, CLAP_EXT_CONFIGURABLE_AUDIO_PORTS) == 0 ||
            std::strcmp(id, CLAP_EXT_CONFIGURABLE_AUDIO_PORTS_COMPAT) == 0) {
            return &configurableAudioPorts;
        }
    }
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) {
        return &params;
    }
    if (std::strcmp(id, CLAP_EXT_STATE) == 0) {
        return &state;
    }
#if defined(__APPLE__)
    if (std::strcmp(id, CLAP_EXT_GUI) == 0) {
        return &gui;
    }
#endif
    return nullptr;
}

const char* const features[] {
    CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,
    CLAP_PLUGIN_FEATURE_DELAY,
    CLAP_PLUGIN_FEATURE_STEREO,
    CLAP_PLUGIN_FEATURE_SURROUND,
    nullptr
};

const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    S3G_DIFFUSION_MESH_PLUGIN_ID,
    S3G_DIFFUSION_MESH_PLUGIN_NAME,
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.1.0",
    S3G_DIFFUSION_MESH_PLUGIN_DESCRIPTION,
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

    prepareMesh(*p);
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
