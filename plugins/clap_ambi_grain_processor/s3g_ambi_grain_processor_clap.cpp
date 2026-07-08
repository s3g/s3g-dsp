#include "s3g_ambi_grain_processor.h"
#include "s3g_realtime.h"

#include <clap/clap.h>
#include <clap/ext/gui.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>

#if defined(__APPLE__)
#import <AVFoundation/AVFoundation.h>
#import <Cocoa/Cocoa.h>
#include "../common/s3g_clap_macos.h"
#include "../common/s3g_cocoa_gui.h"
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <string>

namespace {

constexpr uint32_t kChannelCount = s3g::kAmbiGrainChannels;
constexpr uint32_t kStateVersion = 1;

constexpr clap_id kOrderParamId = 1;
constexpr clap_id kModeParamId = 2;
constexpr clap_id kDensityParamId = 3;
constexpr clap_id kGrainMsParamId = 4;
constexpr clap_id kSourcePosParamId = 5;
constexpr clap_id kJitterParamId = 6;
constexpr clap_id kRateParamId = 7;
constexpr clap_id kRateJitterParamId = 8;
constexpr clap_id kReverseParamId = 9;
constexpr clap_id kFreezeParamId = 10;
constexpr clap_id kJumpStepsParamId = 11;
constexpr clap_id kGainParamId = 12;
constexpr clap_id kSyncParamId = 13;
constexpr clap_id kEnvelopeParamId = 14;
constexpr clap_id kScanSpeedParamId = 15;

constexpr double kGuiW = 920.0;
constexpr double kGuiH = 640.0;
constexpr double kToolboxX = 596.0;
constexpr double kToolboxW = 306.0;
constexpr double kSliderLabelX = 606.0;
constexpr double kSliderTrackX = 704.0;
constexpr double kSliderValueX = 858.0;
constexpr double kSliderTrackW = 150.0;

struct SavedState {
    uint32_t version = kStateVersion;
    double order = 3.0;
    double mode = 0.0;
    double density = 28.0;
    double grainMs = 90.0;
    double sourcePosition = 0.0;
    double scanSpeed = 1.0;
    double positionJitter = 0.12;
    double rate = 1.0;
    double rateJitter = 0.04;
    double reverse = 0.0;
    double freeze = 0.5;
    double jumpSteps = 8.0;
    double gainDb = -12.0;
    double sync = 1.0;
    double envelope = 0.0;
    uint32_t playing = 1;
    char samplePath[1024] {};
};

struct Targets {
    std::atomic<float> order { 3.0f };
    std::atomic<float> mode { 0.0f };
    std::atomic<float> density { 28.0f };
    std::atomic<float> grainMs { 90.0f };
    std::atomic<float> sourcePosition { 0.0f };
    std::atomic<float> scanSpeed { 1.0f };
    std::atomic<float> positionJitter { 0.12f };
    std::atomic<float> rate { 1.0f };
    std::atomic<float> rateJitter { 0.04f };
    std::atomic<float> reverse { 0.0f };
    std::atomic<float> freeze { 0.5f };
    std::atomic<float> jumpSteps { 8.0f };
    std::atomic<float> gainDb { -12.0f };
    std::atomic<float> sync { 1.0f };
    std::atomic<float> envelope { 0.0f };
};

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    double sampleRate = 48000.0;
    uint32_t maxFrames = 0;
    Targets targets {};
    s3g::AmbiGrainProcessor engine;
    std::shared_ptr<const s3g::AmbiGrainSample> sample;
    std::string samplePath;
    std::atomic<bool> playing { true };
    std::atomic<bool> resetRequested { false };
    std::atomic<float> outputPeak { 0.0f };
    std::atomic<float> visualPhase { 0.0f };
    std::atomic<float> visualPulse { 0.0f };
#if defined(__APPLE__)
    void* guiView = nullptr;
    std::atomic<bool> guiVisible { false };
#endif
};

Plugin* self(const clap_plugin_t* plugin) { return static_cast<Plugin*>(plugin->plugin_data); }

const char* modeName(uint32_t mode)
{
    switch (std::min<uint32_t>(mode, 3u)) {
    case 1: return "CLOUD";
    case 2: return "FREEZE";
    case 3: return "JUMP";
    default: return "SCAN";
    }
}

const char* envelopeName(uint32_t envelope)
{
    switch (std::min<uint32_t>(envelope, 4u)) {
    case 1: return "SIN";
    case 2: return "HANN";
    case 3: return "TRI";
    case 4: return "GAUS";
    default: return "PARZ";
    }
}

s3g::AmbiGrainParams snapshotParams(const Plugin& p)
{
    s3g::AmbiGrainParams params {};
    params.order = static_cast<uint32_t>(std::clamp(std::round(p.targets.order.load(std::memory_order_acquire)), 1.0f, 3.0f));
    params.mode = static_cast<s3g::AmbiGrainMode>(static_cast<uint32_t>(std::clamp(std::round(p.targets.mode.load(std::memory_order_acquire)), 0.0f, 3.0f)));
    params.density = p.targets.density.load(std::memory_order_acquire);
    params.grainMs = p.targets.grainMs.load(std::memory_order_acquire);
    params.sourcePosition = p.targets.sourcePosition.load(std::memory_order_acquire);
    params.scanSpeed = p.targets.scanSpeed.load(std::memory_order_acquire);
    params.positionJitter = p.targets.positionJitter.load(std::memory_order_acquire);
    params.rate = p.targets.rate.load(std::memory_order_acquire);
    params.rateJitterOct = p.targets.rateJitter.load(std::memory_order_acquire);
    params.reverseChance = p.targets.reverse.load(std::memory_order_acquire);
    params.freezePosition = p.targets.freeze.load(std::memory_order_acquire);
    params.jumpSteps = static_cast<uint32_t>(std::clamp(std::round(p.targets.jumpSteps.load(std::memory_order_acquire)), 2.0f, 64.0f));
    params.sync = p.targets.sync.load(std::memory_order_acquire) >= 0.5f;
    params.envelope = static_cast<s3g::AmbiGrainEnvelope>(static_cast<uint32_t>(std::clamp(std::round(p.targets.envelope.load(std::memory_order_acquire)), 0.0f, 4.0f)));
    params.outputGainDb = p.targets.gainDb.load(std::memory_order_acquire);
    return s3g::sanitizeAmbiGrainParams(params);
}

float safeDensityForGrainMs(float density, float grainMs)
{
    return s3g::clamp(density, 0.1f, s3g::ambiGrainDensityLimitForGrainMs(grainMs));
}

void setParam(Plugin& p, clap_id id, double value)
{
    switch (id) {
    case kOrderParamId: p.targets.order.store(static_cast<float>(std::clamp(std::round(value), 1.0, 3.0)), std::memory_order_release); break;
    case kModeParamId: p.targets.mode.store(static_cast<float>(std::clamp(std::round(value), 0.0, 3.0)), std::memory_order_release); break;
    case kDensityParamId: {
        const float grainMs = p.targets.grainMs.load(std::memory_order_acquire);
        p.targets.density.store(safeDensityForGrainMs(static_cast<float>(value), grainMs), std::memory_order_release);
        break;
    }
    case kGrainMsParamId: {
        const float grainMs = static_cast<float>(std::clamp(value, 8.0, static_cast<double>(s3g::kAmbiGrainMaxGrainMs)));
        p.targets.grainMs.store(grainMs, std::memory_order_release);
        const float density = p.targets.density.load(std::memory_order_acquire);
        p.targets.density.store(safeDensityForGrainMs(density, grainMs), std::memory_order_release);
        break;
    }
    case kSourcePosParamId: p.targets.sourcePosition.store(static_cast<float>(std::clamp(value, 0.0, 1.0)), std::memory_order_release); break;
    case kScanSpeedParamId: p.targets.scanSpeed.store(static_cast<float>(std::clamp(value, 0.0, 4.0)), std::memory_order_release); break;
    case kJitterParamId: p.targets.positionJitter.store(static_cast<float>(std::clamp(value, 0.0, 1.0)), std::memory_order_release); break;
    case kRateParamId: p.targets.rate.store(static_cast<float>(std::clamp(value, 0.125, 4.0)), std::memory_order_release); break;
    case kRateJitterParamId: p.targets.rateJitter.store(static_cast<float>(std::clamp(value, 0.0, 1.0)), std::memory_order_release); break;
    case kReverseParamId: p.targets.reverse.store(static_cast<float>(std::clamp(value, 0.0, 1.0)), std::memory_order_release); break;
    case kFreezeParamId: p.targets.freeze.store(static_cast<float>(std::clamp(value, 0.0, 1.0)), std::memory_order_release); break;
    case kJumpStepsParamId: p.targets.jumpSteps.store(static_cast<float>(std::clamp(std::round(value), 2.0, 64.0)), std::memory_order_release); break;
    case kGainParamId: p.targets.gainDb.store(static_cast<float>(std::clamp(value, -60.0, 6.0)), std::memory_order_release); break;
    case kSyncParamId: p.targets.sync.store(static_cast<float>(std::clamp(std::round(value), 0.0, 1.0)), std::memory_order_release); break;
    case kEnvelopeParamId: p.targets.envelope.store(static_cast<float>(std::clamp(std::round(value), 0.0, 4.0)), std::memory_order_release); break;
    default: break;
    }
}

#if defined(__APPLE__)
std::shared_ptr<s3g::AmbiGrainSample> readSampleFromPath(const std::string& path)
{
    if (path.empty()) return nullptr;
    @autoreleasepool {
        NSString* nsPath = [NSString stringWithUTF8String:path.c_str()];
        NSURL* url = [NSURL fileURLWithPath:nsPath];
        NSError* error = nil;
        AVAudioFile* file = [[AVAudioFile alloc] initForReading:url error:&error];
        if (!file) return nullptr;
        AVAudioFormat* format = [file processingFormat];
        const AVAudioFrameCount frames = static_cast<AVAudioFrameCount>(std::min<int64_t>([file length], 0x7fffffff));
        AVAudioPCMBuffer* buffer = [[AVAudioPCMBuffer alloc] initWithPCMFormat:format frameCapacity:frames];
        if (!buffer) { [file release]; return nullptr; }
        BOOL ok = [file readIntoBuffer:buffer error:&error];
        if (!ok || buffer.frameLength < 8 || !buffer.floatChannelData) {
            [buffer release];
            [file release];
            return nullptr;
        }
        auto sample = std::make_shared<s3g::AmbiGrainSample>();
        sample->frames = buffer.frameLength;
        sample->channels = std::min<uint32_t>(s3g::kAmbiGrainChannels, std::max<uint32_t>(1u, buffer.format.channelCount));
        sample->sampleRate = buffer.format.sampleRate;
        sample->path = path;
        sample->audio.assign(static_cast<size_t>(sample->frames) * sample->channels, 0.0f);
        for (uint32_t ch = 0; ch < sample->channels; ++ch) {
            const float* src = buffer.floatChannelData[ch];
            if (!src) continue;
            for (uint32_t i = 0; i < sample->frames; ++i) {
                sample->audio[static_cast<size_t>(i) * sample->channels + ch] = src[i];
            }
        }
        [buffer release];
        [file release];
        return sample;
    }
}

bool loadSampleFromPath(Plugin& p, const std::string& path)
{
    auto sample = readSampleFromPath(path);
    if (!sample) return false;
    p.samplePath = path;
    std::shared_ptr<const s3g::AmbiGrainSample> immutable = sample;
    std::atomic_store_explicit(&p.sample, immutable, std::memory_order_release);
    setParam(p, kOrderParamId, s3g::ambiGrainOrderForChannels(sample->channels));
    p.engine.reset();
    return true;
}

void chooseSampleFromFinder(Plugin& p)
{
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    [panel setCanChooseFiles:YES];
    [panel setCanChooseDirectories:NO];
    [panel setAllowsMultipleSelection:NO];
    [panel setAllowedFileTypes:@[@"wav", @"aif", @"aiff", @"caf", @"mp3", @"m4a"]];
    if ([panel runModal] == NSModalResponseOK) {
        NSURL* url = [panel URL];
        char path[4096] {};
        if (url && [[url path] getFileSystemRepresentation:path maxLength:sizeof(path)]) {
            loadSampleFromPath(p, path);
        }
    }
}

void guiDestroy(const clap_plugin_t* plugin);
#endif

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
    p->engine.prepare(sampleRate);
    p->engine.setParams(snapshotParams(*p));
    return true;
}

void deactivate(const clap_plugin_t*) {}
bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}
void reset(const clap_plugin_t* plugin) { self(plugin)->engine.reset(); }

void readEvents(Plugin& p, const clap_input_events_t* in)
{
    if (!in) return;
    const uint32_t n = in->size(in);
    for (uint32_t i = 0; i < n; ++i) {
        const clap_event_header_t* ev = in->get(in, i);
        if (ev && ev->space_id == CLAP_CORE_EVENT_SPACE_ID && ev->type == CLAP_EVENT_PARAM_VALUE) {
            const auto* param = reinterpret_cast<const clap_event_param_value_t*>(ev);
            setParam(p, param->param_id, param->value);
        }
    }
}

clap_process_status process(const clap_plugin_t* plugin, const clap_process_t* proc)
{
    auto* p = self(plugin);
    readEvents(*p, proc->in_events);
    if (proc->audio_outputs_count == 0) return CLAP_PROCESS_CONTINUE;
    const auto& out = proc->audio_outputs[0];
    const uint32_t frames = proc->frames_count;
    if (out.data32) {
        float* laneOut[kChannelCount] {};
        for (uint32_t ch = 0; ch < std::min<uint32_t>(out.channel_count, kChannelCount); ++ch) laneOut[ch] = out.data32[ch];
        auto sample = std::atomic_load_explicit(&p->sample, std::memory_order_acquire);
        const s3g::AmbiGrainParams params = snapshotParams(*p);
        const bool playing = p->playing.load(std::memory_order_acquire);
        if (p->resetRequested.exchange(false, std::memory_order_acq_rel)) {
            p->engine.reset();
            p->visualPhase.store(0.0f, std::memory_order_relaxed);
            p->visualPulse.store(0.0f, std::memory_order_relaxed);
            p->outputPeak.store(0.0f, std::memory_order_relaxed);
        }
        p->engine.setParams(params);
        p->engine.process(sample, laneOut, std::min<uint32_t>(out.channel_count, kChannelCount), frames, playing);
        if (playing && sample) {
            const float advance = static_cast<float>(static_cast<double>(frames) / std::max(1.0, p->sampleRate));
            const float densityStep = params.density * advance;
            float phaseAdvance = advance / std::max(0.25f, params.grainMs * 0.001f);
            if (params.mode == s3g::AmbiGrainMode::Scan && params.sourcePosition <= 0.0001f) {
                const double duration = static_cast<double>(sample->frames) / std::max(1.0, sample->sampleRate);
                phaseAdvance = static_cast<float>(advance * params.scanSpeed / std::max(0.001, duration));
            }
            float phase = p->visualPhase.load(std::memory_order_relaxed) + phaseAdvance;
            phase -= std::floor(phase);
            p->visualPhase.store(phase, std::memory_order_relaxed);
            const float pulse = std::max(p->visualPulse.load(std::memory_order_relaxed) * 0.88f,
                std::min(1.0f, densityStep));
            p->visualPulse.store(pulse, std::memory_order_relaxed);
        } else {
            p->visualPulse.store(p->visualPulse.load(std::memory_order_relaxed) * 0.90f, std::memory_order_relaxed);
        }
        s3g::clearAudioBufferFromChannel(out, std::min<uint32_t>(out.channel_count, kChannelCount), frames);
        float peak = 0.0f;
        for (uint32_t ch = 0; ch < std::min<uint32_t>(out.channel_count, kChannelCount); ++ch) {
            if (!out.data32[ch]) continue;
            for (uint32_t i = 0; i < frames; ++i) peak = std::max(peak, std::fabs(out.data32[ch][i]));
        }
        p->outputPeak.store(std::max(p->outputPeak.load(std::memory_order_relaxed) * 0.92f, peak), std::memory_order_relaxed);
    } else if (out.data64) {
        for (uint32_t ch = 0; ch < out.channel_count; ++ch) if (out.data64[ch]) std::fill(out.data64[ch], out.data64[ch] + frames, 0.0);
    }
    return CLAP_PROCESS_CONTINUE;
}

void onMainThread(const clap_plugin_t*) {}

uint32_t audioPortsCount(const clap_plugin_t*, bool isInput) { return isInput ? 0u : 1u; }

bool audioPortsGet(const clap_plugin_t*, uint32_t index, bool isInput, clap_audio_port_info_t* info)
{
    if (isInput || index != 0 || !info) return false;
    info->id = 20;
    std::strncpy(info->name, "Ambi 3OA Out", sizeof(info->name));
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = kChannelCount;
    info->port_type = CLAP_PORT_AMBISONIC;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}
const clap_plugin_audio_ports_t audioPorts { audioPortsCount, audioPortsGet };

struct ParamDef { clap_id id; const char* name; double min; double max; double def; };
constexpr ParamDef kParamDefs[] {
    { kOrderParamId, "Order", 1.0, 3.0, 3.0 },
    { kModeParamId, "Mode", 0.0, 3.0, 0.0 },
    { kDensityParamId, "Density", 0.1, s3g::kAmbiGrainMaxDensity, 28.0 },
    { kGrainMsParamId, "Grain Size", 8.0, s3g::kAmbiGrainMaxGrainMs, 90.0 },
    { kSourcePosParamId, "Source Position", 0.0, 1.0, 0.0 },
    { kScanSpeedParamId, "Scan Speed", 0.0, 4.0, 1.0 },
    { kJitterParamId, "Position Jitter", 0.0, 1.0, 0.12 },
    { kRateParamId, "Rate", 0.125, 4.0, 1.0 },
    { kRateJitterParamId, "Rate Jitter", 0.0, 1.0, 0.04 },
    { kReverseParamId, "Reverse Chance", 0.0, 1.0, 0.0 },
    { kFreezeParamId, "Freeze Position", 0.0, 1.0, 0.5 },
    { kJumpStepsParamId, "Jump Steps", 2.0, 64.0, 8.0 },
    { kSyncParamId, "Sync", 0.0, 1.0, 1.0 },
    { kEnvelopeParamId, "Envelope", 0.0, 4.0, 0.0 },
    { kGainParamId, "Output Gain", -60.0, 6.0, -12.0 },
};

uint32_t paramsCount(const clap_plugin_t*) { return static_cast<uint32_t>(sizeof(kParamDefs) / sizeof(kParamDefs[0])); }

bool paramsGetInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info)
{
    if (!info || index >= paramsCount(nullptr)) return false;
    const auto& def = kParamDefs[index];
    info->id = def.id;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    if (def.id == kOrderParamId || def.id == kModeParamId || def.id == kJumpStepsParamId || def.id == kSyncParamId || def.id == kEnvelopeParamId) info->flags |= CLAP_PARAM_IS_STEPPED;
    std::strncpy(info->name, def.name, sizeof(info->name));
    std::strncpy(info->module, "Ambi Grain Processor", sizeof(info->module));
    info->min_value = def.min;
    info->max_value = def.max;
    info->default_value = def.def;
    return true;
}

bool paramsGetValue(const clap_plugin_t* plugin, clap_id id, double* value)
{
    if (!value) return false;
    const auto* p = self(plugin);
    switch (id) {
    case kOrderParamId: *value = p->targets.order.load(std::memory_order_acquire); return true;
    case kModeParamId: *value = p->targets.mode.load(std::memory_order_acquire); return true;
    case kDensityParamId: *value = p->targets.density.load(std::memory_order_acquire); return true;
    case kGrainMsParamId: *value = p->targets.grainMs.load(std::memory_order_acquire); return true;
    case kSourcePosParamId: *value = p->targets.sourcePosition.load(std::memory_order_acquire); return true;
    case kScanSpeedParamId: *value = p->targets.scanSpeed.load(std::memory_order_acquire); return true;
    case kJitterParamId: *value = p->targets.positionJitter.load(std::memory_order_acquire); return true;
    case kRateParamId: *value = p->targets.rate.load(std::memory_order_acquire); return true;
    case kRateJitterParamId: *value = p->targets.rateJitter.load(std::memory_order_acquire); return true;
    case kReverseParamId: *value = p->targets.reverse.load(std::memory_order_acquire); return true;
    case kFreezeParamId: *value = p->targets.freeze.load(std::memory_order_acquire); return true;
    case kJumpStepsParamId: *value = p->targets.jumpSteps.load(std::memory_order_acquire); return true;
    case kSyncParamId: *value = p->targets.sync.load(std::memory_order_acquire); return true;
    case kEnvelopeParamId: *value = p->targets.envelope.load(std::memory_order_acquire); return true;
    case kGainParamId: *value = p->targets.gainDb.load(std::memory_order_acquire); return true;
    default: return false;
    }
}

bool paramsValueToText(const clap_plugin_t*, clap_id id, double value, char* display, uint32_t size)
{
    if (!display || size == 0) return false;
    if (id == kModeParamId) std::snprintf(display, size, "%s", modeName(static_cast<uint32_t>(std::round(value))));
    else if (id == kEnvelopeParamId) std::snprintf(display, size, "%s", envelopeName(static_cast<uint32_t>(std::round(value))));
    else if (id == kSyncParamId) std::snprintf(display, size, "%s", value >= 0.5 ? "SYNC" : "ASYNC");
    else if (id == kOrderParamId) std::snprintf(display, size, "%.0fOA", value);
    else if (id == kGainParamId) std::snprintf(display, size, "%+.1f dB", value);
    else if (id == kGrainMsParamId) std::snprintf(display, size, "%.1f ms", value);
    else if (id == kDensityParamId) std::snprintf(display, size, "%.1f/s", value);
    else if (id == kRateParamId || id == kScanSpeedParamId) std::snprintf(display, size, "%.3fx", value);
    else if (id == kSourcePosParamId || id == kJitterParamId || id == kRateJitterParamId || id == kReverseParamId || id == kFreezeParamId) std::snprintf(display, size, "%.0f %%", value * 100.0);
    else std::snprintf(display, size, "%.3f", value);
    return true;
}

bool paramsTextToValue(const clap_plugin_t*, clap_id id, const char* display, double* value)
{
    if (!display || !value) return false;
    double v = std::atof(display);
    if (id == kSyncParamId) {
        if (std::strstr(display, "ASY") || std::strstr(display, "asy")) v = 0.0;
        else if (std::strstr(display, "SYN") || std::strstr(display, "syn")) v = 1.0;
    } else if (id == kEnvelopeParamId) {
        if (std::strstr(display, "SIN") || std::strstr(display, "sin")) v = 1.0;
        else if (std::strstr(display, "HAN") || std::strstr(display, "han")) v = 2.0;
        else if (std::strstr(display, "TRI") || std::strstr(display, "tri")) v = 3.0;
        else if (std::strstr(display, "GAU") || std::strstr(display, "gau")) v = 4.0;
        else v = 0.0;
    } else if ((id == kSourcePosParamId || id == kJitterParamId || id == kRateJitterParamId || id == kReverseParamId || id == kFreezeParamId) && v > 1.0) v *= 0.01;
    *value = v;
    return true;
}

void paramsFlush(const clap_plugin_t* plugin, const clap_input_events_t* in, const clap_output_events_t*) { readEvents(*self(plugin), in); }
const clap_plugin_params_t paramsExt { paramsCount, paramsGetInfo, paramsGetValue, paramsValueToText, paramsTextToValue, paramsFlush };

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    if (!stream || !stream->write) return false;
    const auto* p = self(plugin);
    SavedState s {};
    paramsGetValue(plugin, kOrderParamId, &s.order);
    paramsGetValue(plugin, kModeParamId, &s.mode);
    paramsGetValue(plugin, kDensityParamId, &s.density);
    paramsGetValue(plugin, kGrainMsParamId, &s.grainMs);
    paramsGetValue(plugin, kSourcePosParamId, &s.sourcePosition);
    paramsGetValue(plugin, kScanSpeedParamId, &s.scanSpeed);
    paramsGetValue(plugin, kJitterParamId, &s.positionJitter);
    paramsGetValue(plugin, kRateParamId, &s.rate);
    paramsGetValue(plugin, kRateJitterParamId, &s.rateJitter);
    paramsGetValue(plugin, kReverseParamId, &s.reverse);
    paramsGetValue(plugin, kFreezeParamId, &s.freeze);
    paramsGetValue(plugin, kJumpStepsParamId, &s.jumpSteps);
    paramsGetValue(plugin, kSyncParamId, &s.sync);
    paramsGetValue(plugin, kEnvelopeParamId, &s.envelope);
    paramsGetValue(plugin, kGainParamId, &s.gainDb);
    s.playing = p->playing.load(std::memory_order_acquire) ? 1u : 0u;
    std::strncpy(s.samplePath, p->samplePath.c_str(), sizeof(s.samplePath) - 1u);
    return stream->write(stream, &s, sizeof(s)) == static_cast<int64_t>(sizeof(s));
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) return false;
    SavedState s {};
    if (stream->read(stream, &s, sizeof(s)) != static_cast<int64_t>(sizeof(s)) || s.version != kStateVersion) return false;
    auto* p = self(plugin);
    setParam(*p, kOrderParamId, s.order);
    setParam(*p, kModeParamId, s.mode);
    setParam(*p, kDensityParamId, s.density);
    setParam(*p, kGrainMsParamId, s.grainMs);
    setParam(*p, kSourcePosParamId, s.sourcePosition);
    setParam(*p, kScanSpeedParamId, s.scanSpeed);
    setParam(*p, kJitterParamId, s.positionJitter);
    setParam(*p, kRateParamId, s.rate);
    setParam(*p, kRateJitterParamId, s.rateJitter);
    setParam(*p, kReverseParamId, s.reverse);
    setParam(*p, kFreezeParamId, s.freeze);
    setParam(*p, kJumpStepsParamId, s.jumpSteps);
    setParam(*p, kSyncParamId, s.sync);
    setParam(*p, kEnvelopeParamId, s.envelope);
    setParam(*p, kGainParamId, s.gainDb);
    p->playing.store(s.playing != 0u, std::memory_order_release);
#if defined(__APPLE__)
    if (s.samplePath[0] != '\0') loadSampleFromPath(*p, s.samplePath);
#endif
    return true;
}
const clap_plugin_state_t stateExt { stateSave, stateLoad };

} // namespace

#if defined(__APPLE__)
@interface S3GAmbiGrainProcessorView : NSView {
    void* _plugin;
    NSTimer* _timer;
    int _dragSlider;
    int _openMenu;
    int _hoverMenuIndex;
}
- (id)initWithPlugin:(void*)plugin;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
- (void)drawTransportButton:(NSRect)rect kind:(int)kind active:(BOOL)active;
- (void)drawSlider:(NSString*)label param:(clap_id)param y:(CGFloat)y min:(double)min max:(double)max attrs:(NSDictionary*)attrs small:(NSDictionary*)small;
- (void)drawSlider:(NSString*)label param:(clap_id)param y:(CGFloat)y min:(double)min max:(double)max attrs:(NSDictionary*)attrs small:(NSDictionary*)small active:(BOOL)active;
- (void)drawMenuControl:(NSString*)label param:(clap_id)param y:(CGFloat)y attrs:(NSDictionary*)attrs small:(NSDictionary*)small;
- (void)drawWaveform:(const std::shared_ptr<const s3g::AmbiGrainSample>&)sample rect:(NSRect)rect attrs:(NSDictionary*)attrs;
@end

static NSColor* c(int rgb) { return s3g::clap_gui::color(rgb); }

static double normalizedSliderValue(clap_id param, double value, double min, double max)
{
    if (param == kGrainMsParamId) {
        const double lo = std::max(0.001, min);
        const double hi = std::max(lo + 0.001, max);
        return std::log(std::max(lo, value) / lo) / std::log(hi / lo);
    }
    return (value - min) / std::max(0.0001, max - min);
}

static double valueForNormalizedSlider(clap_id param, double normalized, double min, double max)
{
    normalized = std::clamp(normalized, 0.0, 1.0);
    if (param == kGrainMsParamId) {
        const double lo = std::max(0.001, min);
        const double hi = std::max(lo + 0.001, max);
        return lo * std::pow(hi / lo, normalized);
    }
    return min + (max - min) * normalized;
}

@implementation S3GAmbiGrainProcessorView
- (id)initWithPlugin:(void*)plugin { if ((self = [super initWithFrame:NSMakeRect(0,0,kGuiW,kGuiH)])) { _plugin = plugin; _dragSlider = 0; _openMenu = 0; _hoverMenuIndex = -1; } return self; }
- (BOOL)isFlipped { return YES; }
- (void)dealloc { [self stopRefreshTimer]; [super dealloc]; }
- (void)startRefreshTimer { if (!_timer) { _timer = [NSTimer timerWithTimeInterval:1.0/20.0 target:self selector:@selector(tick:) userInfo:nil repeats:YES]; [[NSRunLoop mainRunLoop] addTimer:_timer forMode:NSRunLoopCommonModes]; } }
- (void)stopRefreshTimer { [_timer invalidate]; _timer = nil; }
- (void)tick:(NSTimer*)timer { (void)timer; if (![self isHidden] && _plugin && s3g::clap_support::hostAppIsActive()) [self setNeedsDisplay:YES]; }
- (void)drawButton:(NSString*)label rect:(NSRect)rect active:(BOOL)active attrs:(NSDictionary*)attrs
{
    [c(active ? 0xd1d1d1 : 0x141414) setFill]; NSRectFill(rect);
    [c(0xd1d1d1) setStroke]; NSFrameRect(rect);
    NSMutableDictionary* a = [attrs mutableCopy]; a[NSForegroundColorAttributeName] = active ? c(0x050505) : c(0xdadada);
    const NSSize size = [label sizeWithAttributes:a];
    [label drawAtPoint:NSMakePoint(rect.origin.x + (rect.size.width - size.width) * 0.5,
                                   rect.origin.y + (rect.size.height - size.height) * 0.5 - 1.0)
        withAttributes:a];
    [a release];
}
- (void)drawTransportButton:(NSRect)rect kind:(int)kind active:(BOOL)active
{
    [c(active ? 0xd1d1d1 : 0x141414) setFill];
    NSRectFill(rect);
    [c(0xd1d1d1) setStroke];
    NSFrameRect(rect);
    [c(active ? 0x0c0c0c : 0xd1d1d1) setFill];
    const CGFloat cx = NSMidX(rect);
    const CGFloat cy = NSMidY(rect);
    if (kind == 0) {
        NSBezierPath* tri = [NSBezierPath bezierPath];
        [tri moveToPoint:NSMakePoint(cx - 3.0, cy - 5.5)];
        [tri lineToPoint:NSMakePoint(cx - 3.0, cy + 5.5)];
        [tri lineToPoint:NSMakePoint(cx + 6.0, cy)];
        [tri closePath];
        [tri fill];
    } else if (kind == 1) {
        NSRectFill(NSMakeRect(cx - 5.5, cy - 5.5, 3.5, 11.0));
        NSRectFill(NSMakeRect(cx + 2.0, cy - 5.5, 3.5, 11.0));
    } else {
        NSRectFill(NSMakeRect(cx - 5.5, cy - 5.5, 11.0, 11.0));
    }
}
- (void)drawSlider:(NSString*)label param:(clap_id)param y:(CGFloat)y min:(double)min max:(double)max attrs:(NSDictionary*)attrs small:(NSDictionary*)small
{
    [self drawSlider:label param:param y:y min:min max:max attrs:attrs small:small active:YES];
}
- (void)drawSlider:(NSString*)label param:(clap_id)param y:(CGFloat)y min:(double)min max:(double)max attrs:(NSDictionary*)attrs small:(NSDictionary*)small active:(BOOL)active
{
    auto* p = static_cast<Plugin*>(_plugin);
    double value = 0.0; paramsGetValue(&p->plugin, param, &value);
    char text[64] {}; paramsValueToText(&p->plugin, param, value, text, sizeof(text));
    const CGFloat n = static_cast<CGFloat>(normalizedSliderValue(param, value, min, max));
    s3g::clap_gui::Style style;
    NSDictionary* labelAttrs = attrs;
    NSDictionary* valueAttrs = small;
    if (!active) {
        NSMutableDictionary* mutedLabel = [attrs mutableCopy];
        NSMutableDictionary* mutedValue = [small mutableCopy];
        mutedLabel[NSForegroundColorAttributeName] = c(0x686868);
        mutedValue[NSForegroundColorAttributeName] = c(0x686868);
        labelAttrs = mutedLabel;
        valueAttrs = mutedValue;
        style.fill = c(0x3f3f3f);
        style.grid = c(0x333333);
    }
    s3g::clap_gui::drawSlider(label, [NSString stringWithUTF8String:text], n, y, labelAttrs, valueAttrs, style,
        kSliderLabelX, kSliderTrackX, kSliderValueX, kSliderTrackW);
    if (!active) {
        [(id)labelAttrs release];
        [(id)valueAttrs release];
    }
}
- (void)drawMenuControl:(NSString*)label param:(clap_id)param y:(CGFloat)y attrs:(NSDictionary*)attrs small:(NSDictionary*)small
{
    auto* p = static_cast<Plugin*>(_plugin);
    double value = 0.0; paramsGetValue(&p->plugin, param, &value);
    char text[64] {}; paramsValueToText(&p->plugin, param, value, text, sizeof(text));
    s3g::clap_gui::Style style;
    s3g::clap_gui::drawMenu(label, [NSString stringWithUTF8String:text], y, attrs, small, style,
        kSliderLabelX, kSliderTrackX, 178.0);
}
- (void)drawWaveform:(const std::shared_ptr<const s3g::AmbiGrainSample>&)sample rect:(NSRect)rect attrs:(NSDictionary*)attrs
{
    auto* p = static_cast<Plugin*>(_plugin);
    const s3g::AmbiGrainParams prm = snapshotParams(*p);
    [c(0x111111) setFill]; NSRectFill(rect);
    [c(0x444444) setStroke]; NSFrameRect(rect);
    const CGFloat midY = NSMidY(rect);
    [c(0x2a2a2a) setStroke];
    [NSBezierPath strokeLineFromPoint:NSMakePoint(rect.origin.x + 1.0, midY)
                              toPoint:NSMakePoint(NSMaxX(rect) - 1.0, midY)];
    if (!sample || sample->frames < 2u || sample->channels == 0u || sample->audio.empty()) {
        [@"LOAD ACN/SN3D AMBISONIC FILE" drawAtPoint:NSMakePoint(rect.origin.x + 12.0, rect.origin.y + 14.0) withAttributes:attrs];
        return;
    }

    const CGFloat waveX = rect.origin.x + 6.0;
    const CGFloat waveW = rect.size.width - 12.0;
    const CGFloat usableH = rect.size.height - 28.0;
    const CGFloat scaleY = usableH * 0.46;
    const uint32_t columns = static_cast<uint32_t>(std::max<CGFloat>(32.0, std::floor(waveW)));
    NSBezierPath* wave = [NSBezierPath bezierPath];
    [wave setLineWidth:0.75];
    [NSGraphicsContext saveGraphicsState];
    NSRectClip(NSInsetRect(rect, 1.0, 1.0));
    for (uint32_t x = 0; x < columns; ++x) {
        const uint32_t a = static_cast<uint32_t>((static_cast<uint64_t>(x) * sample->frames) / columns);
        const uint32_t b = std::max<uint32_t>(a + 1u, static_cast<uint32_t>((static_cast<uint64_t>(x + 1u) * sample->frames) / columns));
        const uint32_t stride = std::max<uint32_t>(1u, (b - a) / 32u);
        float lo = 0.0f;
        float hi = 0.0f;
        for (uint32_t i = a; i < std::min<uint32_t>(b, sample->frames); i += stride) {
            const float value = sample->audio[static_cast<size_t>(i) * sample->channels];
            lo = std::min(lo, value);
            hi = std::max(hi, value);
        }
        const CGFloat px = waveX + static_cast<CGFloat>(x);
        [wave moveToPoint:NSMakePoint(px, std::clamp(midY - static_cast<CGFloat>(hi) * scaleY, rect.origin.y + 6.0, NSMaxY(rect) - 22.0))];
        [wave lineToPoint:NSMakePoint(px, std::clamp(midY - static_cast<CGFloat>(lo) * scaleY, rect.origin.y + 6.0, NSMaxY(rect) - 22.0))];
    }
    [c(0x747474) setStroke]; [wave stroke];

    CGFloat centerUnit = prm.sourcePosition;
    const bool autoScan = prm.mode == s3g::AmbiGrainMode::Scan && centerUnit <= 0.0001f;
    if (autoScan) {
        centerUnit = p->visualPhase.load(std::memory_order_relaxed);
    } else if (prm.mode == s3g::AmbiGrainMode::Cloud) {
        centerUnit = p->visualPhase.load(std::memory_order_relaxed);
    } else if (prm.mode == s3g::AmbiGrainMode::Freeze) {
        centerUnit = prm.freezePosition;
    } else if (prm.mode == s3g::AmbiGrainMode::Jump) {
        centerUnit = std::floor(prm.sourcePosition * static_cast<float>(prm.jumpSteps))
            / static_cast<float>(std::max<uint32_t>(1u, prm.jumpSteps - 1u));
    }
    centerUnit = std::clamp<CGFloat>(centerUnit, 0.0, 1.0);
    const CGFloat sourceX = waveX + centerUnit * waveW;
    const CGFloat jitterW = prm.positionJitter * waveW;

    const CGFloat grainUnitW = std::clamp<CGFloat>((prm.grainMs * 0.001f) / std::max(0.25, static_cast<double>(sample->frames) / sample->sampleRate), 0.002, 1.0);
    const CGFloat grainW = grainUnitW * waveW;
    const CGFloat pulse = std::clamp<CGFloat>(p->visualPulse.load(std::memory_order_relaxed), 0.0, 1.0);
    const uint32_t grainCount = std::clamp<uint32_t>(
        static_cast<uint32_t>(std::ceil(std::max(1.0f, prm.density * prm.grainMs * 0.001f))),
        1u,
        32u);
    const CGFloat grainTop = rect.origin.y + 12.0;
    const CGFloat swarmH = 34.0;
    const CGFloat grainBottom = NSMaxY(rect) - 32.0 - swarmH;
    const CGFloat grainH = grainBottom - grainTop;
    const NSRect scanRect = NSMakeRect(std::max(waveX, sourceX - grainW * 0.5),
        rect.origin.y + 8.0,
        std::min(NSMaxX(rect) - 6.0, sourceX + grainW * 0.5) - std::max(waveX, sourceX - grainW * 0.5),
        grainBottom - rect.origin.y - 8.0);
    [[NSColor colorWithCalibratedRed:0.15 green:0.76 blue:0.86 alpha:0.09 + pulse * 0.06] setFill];
    NSRectFill(scanRect);
    [[NSColor colorWithCalibratedRed:0.30 green:0.88 blue:0.95 alpha:0.38] setStroke];
    NSFrameRect(scanRect);
    const CGFloat jitterLeft = autoScan ? waveX : std::max(waveX, sourceX - jitterW);
    const CGFloat jitterRight = autoScan ? NSMaxX(rect) - 6.0 : std::min(NSMaxX(rect) - 6.0, sourceX + jitterW);
    [[NSColor colorWithCalibratedRed:0.42 green:0.82 blue:0.86 alpha:0.34] setStroke];
    [NSBezierPath strokeLineFromPoint:NSMakePoint(jitterLeft, rect.origin.y + 8.0)
                              toPoint:NSMakePoint(jitterLeft, grainBottom)];
    [NSBezierPath strokeLineFromPoint:NSMakePoint(jitterRight, rect.origin.y + 8.0)
                              toPoint:NSMakePoint(jitterRight, grainBottom)];
    [[NSColor colorWithCalibratedRed:0.42 green:0.82 blue:0.86 alpha:0.46] setFill];
    NSRectFill(NSMakeRect(jitterLeft - 2.0, rect.origin.y + 8.0, 4.0, 3.0));
    NSRectFill(NSMakeRect(jitterRight - 2.0, rect.origin.y + 8.0, 4.0, 3.0));
    NSRectFill(NSMakeRect(jitterLeft - 2.0, grainBottom - 3.0, 4.0, 3.0));
    NSRectFill(NSMakeRect(jitterRight - 2.0, grainBottom - 3.0, 4.0, 3.0));
    auto hashUnit = [](uint32_t seed) -> CGFloat {
        seed ^= seed >> 16u;
        seed *= 0x7feb352du;
        seed ^= seed >> 15u;
        seed *= 0x846ca68bu;
        seed ^= seed >> 16u;
        return static_cast<CGFloat>((seed & 0x00ffffffu) / static_cast<double>(0x01000000u));
    };
    auto wrapUnit = [](CGFloat value) -> CGFloat {
        value -= std::floor(value);
        return value < 0.0 ? value + 1.0 : value;
    };
    for (uint32_t i = 0; i < grainCount; ++i) {
        const CGFloat spread = prm.positionJitter;
        CGFloat unit = centerUnit;
        if (prm.mode == s3g::AmbiGrainMode::Cloud) {
            unit = hashUnit(i * 97u + static_cast<uint32_t>(p->visualPhase.load(std::memory_order_relaxed) * 4096.0f));
        } else if (spread > 0.0001) {
            unit += (hashUnit(i * 131u + 17u) * 2.0 - 1.0) * spread;
        } else {
            unit += (static_cast<CGFloat>(i) / static_cast<CGFloat>(std::max(1u, grainCount)) - 0.5) * grainUnitW;
        }
        if (prm.mode != s3g::AmbiGrainMode::Freeze) {
            unit = wrapUnit(unit);
        }
        unit = std::clamp<CGFloat>(unit, 0.0, 1.0);
        const CGFloat gx = waveX + unit * waveW;
        const CGFloat age = wrapUnit(p->visualPhase.load(std::memory_order_relaxed) + static_cast<CGFloat>(i) / static_cast<CGFloat>(grainCount));
        const CGFloat y0 = grainTop + age * grainH;
        const CGFloat markerW = std::clamp<CGFloat>(grainW, 5.0, 68.0);
        const CGFloat alpha = 0.16 + pulse * 0.16 + (1.0 - age) * 0.10;
        [[NSColor colorWithCalibratedRed:0.42 green:0.90 blue:0.98 alpha:alpha * 0.36] setFill];
        NSRectFill(NSMakeRect(std::max(waveX, gx - markerW * 0.5), std::max(grainTop, y0 - 1.0),
            std::min(NSMaxX(rect) - 6.0, gx + markerW * 0.5) - std::max(waveX, gx - markerW * 0.5), 2.0));
        [[NSColor colorWithCalibratedRed:0.55 green:0.94 blue:1.0 alpha:alpha] setFill];
        NSRectFill(NSMakeRect(gx - 1.5, std::max(grainTop, y0 - 1.5), 3.0, 3.0));
        [[NSColor colorWithCalibratedRed:0.44 green:0.82 blue:0.90 alpha:0.18 + pulse * 0.12] setStroke];
        [NSBezierPath strokeLineFromPoint:NSMakePoint(gx, std::max(grainTop, y0 - 5.0))
                                  toPoint:NSMakePoint(gx, std::min(grainBottom, y0 + 5.0))];
        const CGFloat stripY = grainBottom + 12.0 + hashUnit(i * 43u + 9u) * (swarmH - 16.0);
        [[NSColor colorWithCalibratedRed:0.50 green:0.90 blue:0.96 alpha:0.20 + pulse * 0.16] setFill];
        NSRectFill(NSMakeRect(gx - 1.0, stripY, 2.0, 8.0));
    }
    [[NSColor colorWithCalibratedWhite:0.20 alpha:1.0] setStroke];
    [NSBezierPath strokeLineFromPoint:NSMakePoint(waveX, grainBottom + 6.0)
                              toPoint:NSMakePoint(NSMaxX(rect) - 6.0, grainBottom + 6.0)];
    [c(0xf0f0f0) setStroke];
    [NSBezierPath strokeLineFromPoint:NSMakePoint(sourceX, rect.origin.y + 6.0)
                              toPoint:NSMakePoint(sourceX, grainBottom)];
    [[NSString stringWithFormat:@"W  %.1f sec", static_cast<double>(sample->frames) / sample->sampleRate]
        drawAtPoint:NSMakePoint(rect.origin.x + 10.0, NSMaxY(rect) - 17.0) withAttributes:attrs];
    [[NSString stringWithFormat:@"%u grains", grainCount]
        drawAtPoint:NSMakePoint(NSMaxX(rect) - 82.0, NSMaxY(rect) - 17.0) withAttributes:attrs];
    [NSGraphicsContext restoreGraphicsState];
}
- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    auto* p = static_cast<Plugin*>(_plugin);
    s3g::clap_gui::Style style;
    [style.bg setFill]; NSRectFill(self.bounds);
    NSDictionary* small = @{ NSFontAttributeName:[NSFont fontWithName:@"Menlo" size:10] ?: [NSFont monospacedSystemFontOfSize:10 weight:NSFontWeightRegular], NSForegroundColorAttributeName:style.text };
    NSDictionary* text = small;
    NSDictionary* title = @{ NSFontAttributeName:[NSFont fontWithName:@"Menlo" size:11] ?: [NSFont monospacedSystemFontOfSize:11 weight:NSFontWeightRegular], NSForegroundColorAttributeName:c(0xf0f0f0) };
    const s3g::AmbiGrainParams prm = snapshotParams(*p);
    const BOOL isScan = prm.mode == s3g::AmbiGrainMode::Scan;
    const BOOL isFreeze = prm.mode == s3g::AmbiGrainMode::Freeze;
    const BOOL isJump = prm.mode == s3g::AmbiGrainMode::Jump;
    const BOOL isPlaying = p->playing.load(std::memory_order_acquire);
    [@"s3g Ambi Grain Processor" drawAtPoint:NSMakePoint(18, 16) withAttributes:title];
    [self drawButton:@"LOAD" rect:NSMakeRect(18, 48, 70, 24) active:NO attrs:text];
    [self drawTransportButton:NSMakeRect(96, 49, 26, 22) kind:0 active:isPlaying];
    [self drawTransportButton:NSMakeRect(130, 49, 26, 22) kind:1 active:!isPlaying];
    [self drawTransportButton:NSMakeRect(164, 49, 26, 22) kind:2 active:NO];
    auto sample = std::atomic_load_explicit(&p->sample, std::memory_order_acquire);
    NSString* info = sample ? [NSString stringWithFormat:@"%u ch / %.1f sec / %.0f Hz", sample->channels, static_cast<double>(sample->frames) / sample->sampleRate, sample->sampleRate] : @"no ambisonic file loaded";
    [info drawAtPoint:NSMakePoint(202, 53) withAttributes:text];
    const float peak = p->outputPeak.load(std::memory_order_relaxed);
    [[NSString stringWithFormat:@"PK %.2f", peak] drawAtPoint:NSMakePoint(858, 16) withAttributes:text];

    s3g::clap_gui::drawPanelFrame(18, 92, 552, 458, style);
    s3g::clap_gui::drawPanelHeader(@"WAVEFORM", true, 18, 92, 552, 20, text, style);
    [self drawWaveform:sample rect:NSMakeRect(28, 126, 532, 396) attrs:text];
    [@"W channel source view: cursor, jitter band, and grain window" drawAtPoint:NSMakePoint(28, 558) withAttributes:text];

    s3g::clap_gui::drawPanelFrame(kToolboxX, 48, kToolboxW, 184, style);
    s3g::clap_gui::drawPanelHeader(@"ENGINE", true, kToolboxX, 48, kToolboxW, 20, text, style);
    [self drawMenuControl:@"ORD" param:kOrderParamId y:82 attrs:text small:small];
    [self drawMenuControl:@"MODE" param:kModeParamId y:108 attrs:text small:small];
    [self drawSlider:@"DENS" param:kDensityParamId y:134 min:0.1 max:s3g::kAmbiGrainMaxDensity attrs:text small:small];
    [self drawSlider:@"GMS" param:kGrainMsParamId y:160 min:8 max:s3g::kAmbiGrainMaxGrainMs attrs:text small:small];
    [self drawSlider:@"RATE" param:kRateParamId y:186 min:0.125 max:4 attrs:text small:small];

    s3g::clap_gui::drawPanelFrame(kToolboxX, 246, kToolboxW, 210, style);
    s3g::clap_gui::drawPanelHeader(@"SOURCE", true, kToolboxX, 246, kToolboxW, 20, text, style);
    NSString* sourceLabel = isScan ? @"S-SRC" : (isJump ? @"J-SRC" : @"SRC");
    [self drawSlider:sourceLabel param:kSourcePosParamId y:280 min:0 max:1 attrs:text small:small active:(isScan || isJump)];
    [self drawSlider:@"S-SPD" param:kScanSpeedParamId y:306 min:0 max:4 attrs:text small:small active:isScan];
    [self drawSlider:@"JIT" param:kJitterParamId y:332 min:0 max:1 attrs:text small:small];
    [self drawSlider:@"RJIT" param:kRateJitterParamId y:358 min:0 max:1 attrs:text small:small];
    [self drawSlider:@"REV" param:kReverseParamId y:384 min:0 max:1 attrs:text small:small];
    [self drawSlider:@"F-SRC" param:kFreezeParamId y:410 min:0 max:1 attrs:text small:small active:isFreeze];
    [self drawSlider:@"J-STP" param:kJumpStepsParamId y:436 min:2 max:64 attrs:text small:small active:isJump];

    s3g::clap_gui::drawPanelFrame(kToolboxX, 470, kToolboxW, 118, style);
    s3g::clap_gui::drawPanelHeader(@"WINDOW", true, kToolboxX, 470, kToolboxW, 20, text, style);
    [self drawMenuControl:@"SYNC" param:kSyncParamId y:504 attrs:text small:small];
    [self drawMenuControl:@"ENV" param:kEnvelopeParamId y:530 attrs:text small:small];
    [self drawSlider:@"OUT" param:kGainParamId y:556 min:-60 max:6 attrs:text small:small];

    if (_openMenu > 0) {
        NSString* orderItems[] = {@"1OA", @"2OA", @"3OA"};
        NSString* modeItems[] = {@"SCAN", @"CLOUD", @"FREEZE", @"JUMP"};
        NSString* syncItems[] = {@"ASYNC", @"SYNC"};
        NSString* envItems[] = {@"PARZ", @"SIN", @"HANN", @"TRI", @"GAUS"};
        NSString** items = nullptr;
        uint32_t count = 0u;
        int selected = 0;
        NSRect menuRect = NSMakeRect(kSliderTrackX, 0, 178, 18);
        if (_openMenu == 1) { items = orderItems; count = 3u; selected = static_cast<int>(std::round(p->targets.order.load())) - 1; menuRect.origin.y = 100; }
        else if (_openMenu == 2) { items = modeItems; count = 4u; selected = static_cast<int>(std::round(p->targets.mode.load())); menuRect.origin.y = 126; }
        else if (_openMenu == 3) { items = syncItems; count = 2u; selected = static_cast<int>(std::round(p->targets.sync.load())); menuRect.origin.y = 522; }
        else if (_openMenu == 4) { items = envItems; count = 5u; selected = static_cast<int>(std::round(p->targets.envelope.load())); menuRect.origin.y = 548; }
        menuRect.size.height = 18.0 * count;
        if (items) s3g::clap_gui::drawDropdownMenu(menuRect, 18.0, items, count, selected, _hoverMenuIndex, text, style);
    }
}
- (void)updateSlider:(NSPoint)pt
{
    auto* p = static_cast<Plugin*>(_plugin);
    struct Row { clap_id id; double min; double max; CGFloat y; };
    const Row rows[] = {
        {kDensityParamId,0.1,s3g::kAmbiGrainMaxDensity,134},{kGrainMsParamId,8,s3g::kAmbiGrainMaxGrainMs,160},
        {kRateParamId,0.125,4,186},{kSourcePosParamId,0,1,280},{kScanSpeedParamId,0,4,306},
        {kJitterParamId,0,1,332},{kRateJitterParamId,0,1,358},
        {kReverseParamId,0,1,384},{kFreezeParamId,0,1,410},{kJumpStepsParamId,2,64,436},
        {kGainParamId,-60,6,556}
    };
    if (_dragSlider <= 0) return;
    const Row& row = rows[_dragSlider - 1];
    const double n = std::clamp((pt.x - kSliderTrackX) / kSliderTrackW, 0.0, 1.0);
    setParam(*p, row.id, valueForNormalizedSlider(row.id, n, row.min, row.max));
    [self setNeedsDisplay:YES];
}
- (void)mouseDown:(NSEvent*)event
{
    auto* p = static_cast<Plugin*>(_plugin);
    NSPoint pt = [self convertPoint:event.locationInWindow fromView:nil];
    if (_openMenu > 0) {
        uint32_t count = 0u;
        NSRect menuRect = NSMakeRect(kSliderTrackX, 0, 178, 18);
        if (_openMenu == 1) { count = 3u; menuRect.origin.y = 100; }
        else if (_openMenu == 2) { count = 4u; menuRect.origin.y = 126; }
        else if (_openMenu == 3) { count = 2u; menuRect.origin.y = 522; }
        else if (_openMenu == 4) { count = 5u; menuRect.origin.y = 548; }
        menuRect.size.height = 18.0 * count;
        const int hit = s3g::clap_gui::dropdownHitIndex(pt, menuRect, 18.0, count);
        if (hit >= 0) {
            if (_openMenu == 1) setParam(*p, kOrderParamId, hit + 1);
            else if (_openMenu == 2) setParam(*p, kModeParamId, hit);
            else if (_openMenu == 3) setParam(*p, kSyncParamId, hit);
            else if (_openMenu == 4) setParam(*p, kEnvelopeParamId, hit);
            _openMenu = 0;
            _hoverMenuIndex = -1;
            [self setNeedsDisplay:YES];
            return;
        }
        _openMenu = 0;
        _hoverMenuIndex = -1;
    }
    if (NSPointInRect(pt, NSMakeRect(18,48,70,24))) { chooseSampleFromFinder(*p); [self setNeedsDisplay:YES]; return; }
    if (NSPointInRect(pt, NSMakeRect(96,49,26,22))) {
        p->playing.store(true, std::memory_order_release);
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(pt, NSMakeRect(130,49,26,22))) {
        p->playing.store(!p->playing.load(std::memory_order_acquire), std::memory_order_release);
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(pt, NSMakeRect(164,49,26,22))) {
        p->playing.store(false, std::memory_order_release);
        p->resetRequested.store(true, std::memory_order_release);
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(pt, NSMakeRect(kSliderTrackX, 81, 178, 16))) { _openMenu = 1; [self setNeedsDisplay:YES]; return; }
    if (NSPointInRect(pt, NSMakeRect(kSliderTrackX, 107, 178, 16))) { _openMenu = 2; [self setNeedsDisplay:YES]; return; }
    if (NSPointInRect(pt, NSMakeRect(kSliderTrackX, 503, 178, 16))) { _openMenu = 3; [self setNeedsDisplay:YES]; return; }
    if (NSPointInRect(pt, NSMakeRect(kSliderTrackX, 529, 178, 16))) { _openMenu = 4; [self setNeedsDisplay:YES]; return; }
    const CGFloat ys[] = {134,160,186,280,306,332,358,384,410,436,556};
    for (int i = 0; i < 11; ++i) {
        if (pt.y >= ys[i] - 5 && pt.y <= ys[i] + 18 && pt.x >= kSliderTrackX - 10 && pt.x <= kSliderTrackX + kSliderTrackW + 10) {
            _dragSlider = i + 1;
            [self updateSlider:pt];
            return;
        }
    }
}
- (void)mouseDragged:(NSEvent*)event { [self updateSlider:[self convertPoint:event.locationInWindow fromView:nil]]; }
- (void)mouseUp:(NSEvent*)event { (void)event; _dragSlider = 0; }
- (void)mouseMoved:(NSEvent*)event
{
    if (_openMenu <= 0) return;
    NSPoint pt = [self convertPoint:event.locationInWindow fromView:nil];
    uint32_t count = 0u;
    NSRect menuRect = NSMakeRect(kSliderTrackX, 0, 178, 18);
    if (_openMenu == 1) { count = 3u; menuRect.origin.y = 100; }
    else if (_openMenu == 2) { count = 4u; menuRect.origin.y = 126; }
    else if (_openMenu == 3) { count = 2u; menuRect.origin.y = 522; }
    else if (_openMenu == 4) { count = 5u; menuRect.origin.y = 548; }
    menuRect.size.height = 18.0 * count;
    const int hit = s3g::clap_gui::dropdownHitIndex(pt, menuRect, 18.0, count);
    if (hit != _hoverMenuIndex) {
        _hoverMenuIndex = hit;
        [self setNeedsDisplay:YES];
    }
}
@end

namespace {

bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool isFloating) { return !isFloating && api && std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0; }
bool guiGetPreferredApi(const clap_plugin_t*, const char** api, bool* isFloating) { if (!api || !isFloating) return false; *api = CLAP_WINDOW_API_COCOA; *isFloating = false; return true; }
bool guiCreate(const clap_plugin_t* plugin, const char* api, bool isFloating) { if (!guiIsApiSupported(plugin, api, isFloating)) return false; auto* p = self(plugin); if (p->guiView) return true; p->guiView = [[S3GAmbiGrainProcessorView alloc] initWithPlugin:p]; return p->guiView != nullptr; }
void guiDestroy(const clap_plugin_t* plugin) { auto* p = self(plugin); if (p->guiView) { p->guiVisible.store(false); auto* v = static_cast<S3GAmbiGrainProcessorView*>(p->guiView); [v stopRefreshTimer]; [v removeFromSuperview]; [v release]; p->guiView = nullptr; } }
bool guiSetScale(const clap_plugin_t*, double) { return true; }
bool guiGetSize(const clap_plugin_t*, uint32_t* width, uint32_t* height) { if (!width || !height) return false; *width = static_cast<uint32_t>(kGuiW); *height = static_cast<uint32_t>(kGuiH); return true; }
bool guiCanResize(const clap_plugin_t*) { return false; }
bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t*) { return false; }
bool guiAdjustSize(const clap_plugin_t*, uint32_t*, uint32_t*) { return true; }
bool guiSetSize(const clap_plugin_t* plugin, uint32_t width, uint32_t height) { auto* p = self(plugin); if (p->guiView) [static_cast<NSView*>(p->guiView) setFrameSize:NSMakeSize(width, height)]; return true; }
bool guiSetParent(const clap_plugin_t* plugin, const clap_window_t* window) { auto* p = self(plugin); if (!p->guiView || !window || !window->cocoa) return false; NSView* parent = static_cast<NSView*>(window->cocoa); [parent addSubview:static_cast<NSView*>(p->guiView)]; return true; }
bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}
bool guiShow(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView) return false; p->guiVisible.store(true); [static_cast<NSView*>(p->guiView) setHidden:NO]; [static_cast<S3GAmbiGrainProcessorView*>(p->guiView) startRefreshTimer]; return true; }
bool guiHide(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView) return false; p->guiVisible.store(false); [static_cast<S3GAmbiGrainProcessorView*>(p->guiView) stopRefreshTimer]; [static_cast<NSView*>(p->guiView) setHidden:YES]; return true; }
const clap_plugin_gui_t guiExt { guiIsApiSupported, guiGetPreferredApi, guiCreate, guiDestroy, guiSetScale, guiGetSize, guiCanResize, guiGetResizeHints, guiAdjustSize, guiSetSize, guiSetParent, guiSetTransient, guiSuggestTitle, guiShow, guiHide };
#endif

const void* getExtension(const clap_plugin_t*, const char* id)
{
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &audioPorts;
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) return &paramsExt;
    if (std::strcmp(id, CLAP_EXT_STATE) == 0) return &stateExt;
#if defined(__APPLE__)
    if (std::strcmp(id, CLAP_EXT_GUI) == 0) return &guiExt;
#endif
    return nullptr;
}

void hostTail(const clap_plugin_t*) {}

const clap_plugin_t pluginClass {
    nullptr, nullptr, init, destroy, activate, deactivate, startProcessing, stopProcessing, reset, process, getExtension, onMainThread
};

constexpr const char* features[] = { CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_AMBISONIC, nullptr };
const clap_plugin_descriptor_t descriptor { CLAP_VERSION_INIT, "org.s3g.s3g-dsp.ambi-grain-processor", "s3g Ambi Grain Processor", "s3g", "https://github.com/s3g/s3g-dsp", "", "", "0.1.0", "Coherent grain processor for loaded ACN/SN3D ambisonic media.", features };

const clap_plugin_t* createPlugin(const clap_host_t* host, const char* pluginId)
{
    if (std::strcmp(pluginId, descriptor.id) != 0) return nullptr;
    auto* p = new (std::nothrow) Plugin();
    if (!p) return nullptr;
    p->plugin = pluginClass;
    p->plugin.desc = &descriptor;
    p->plugin.plugin_data = p;
    p->host = host;
    p->engine.setParams(snapshotParams(*p));
    return &p->plugin;
}

uint32_t factoryGetPluginCount(const clap_plugin_factory*) { return 1; }
const clap_plugin_descriptor_t* factoryGetPluginDescriptor(const clap_plugin_factory*, uint32_t index) { return index == 0 ? &descriptor : nullptr; }
const clap_plugin_t* factoryCreatePlugin(const clap_plugin_factory*, const clap_host_t* host, const char* pluginId) { return createPlugin(host, pluginId); }
const clap_plugin_factory factory { factoryGetPluginCount, factoryGetPluginDescriptor, factoryCreatePlugin };
bool entryInit(const char*) { return true; }
void entryDeinit() {}
const void* entryGetFactory(const char* factoryId) { return std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0 ? &factory : nullptr; }

} // namespace

extern "C" const clap_plugin_entry_t clap_entry { CLAP_VERSION_INIT, entryInit, entryDeinit, entryGetFactory };
