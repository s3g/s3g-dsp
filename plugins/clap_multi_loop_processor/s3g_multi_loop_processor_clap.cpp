#include "s3g_loop_processor.h"
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

constexpr uint32_t kChannelCount = s3g::kLoopProcessorChannels;
constexpr uint32_t kStateVersion = 7;

constexpr clap_id kRateParamId = 1;
constexpr clap_id kSpreadParamId = 2;
constexpr clap_id kDriftParamId = 3;
constexpr clap_id kXfadeParamId = 4;
constexpr clap_id kDuckParamId = 5;
constexpr clap_id kGainParamId = 6;
constexpr clap_id kCenterParamId = 7;
constexpr clap_id kGlideParamId = 8;
constexpr clap_id kLoopStartParamId = 9;
constexpr clap_id kLoopLengthParamId = 10;
constexpr clap_id kLaunchParamId = 13;
constexpr clap_id kLaneMaskParamId = 14;
constexpr clap_id kRuleParamId = 15;
constexpr clap_id kSourceRateParamId = 16;
constexpr clap_id kSourceBlendParamId = 17;

constexpr double kGuiW = 920.0;
constexpr double kGuiH = 640.0;
constexpr double kSliderLabelX = 606.0;
constexpr double kSliderTrackX = 704.0;
constexpr double kSliderValueX = 858.0;
constexpr double kSliderTrackW = 150.0;
constexpr double kToolboxX = 596.0;
constexpr double kToolboxW = 306.0;
constexpr uint32_t kMaxSources = 4;

struct SavedState {
    uint32_t version = kStateVersion;
    double baseRate = 1.0;
    double rateSpread = 0.08;
    double driftAmount = 0.0;
    double relationCenter = 0.5;
    double relationGlideMs = 250.0;
    double loopStart = 0.0;
    double loopLength = 1.0;
    double xfadePct = 0.08;
    double seamDuck = 0.12;
    double gainDb = -12.0;
    double sourceRateSpread = 0.0;
    double sourceBlend = 1.0;
    uint32_t launchMode = 0;
    uint32_t laneMask = 0xffu;
    uint32_t rule = 1;
    uint32_t playing = 1;
    char samplePaths[kMaxSources][1024] {};
};

struct ParameterTargets {
    std::atomic<float> baseRate { 1.0f };
    std::atomic<float> rateSpread { 0.08f };
    std::atomic<float> driftAmount { 0.0f };
    std::atomic<float> relationCenter { 0.5f };
    std::atomic<float> relationGlideMs { 250.0f };
    std::atomic<float> loopStart { 0.0f };
    std::atomic<float> loopLength { 1.0f };
    std::atomic<float> xfadePct { 0.08f };
    std::atomic<float> seamDuck { 0.12f };
    std::atomic<float> gainDb { -12.0f };
    std::atomic<float> sourceRateSpread { 0.0f };
    std::atomic<float> sourceBlend { 1.0f };
    std::atomic<uint32_t> launchMode { 0u };
    std::atomic<uint32_t> laneMask { 0xffu };
    std::atomic<uint32_t> rule { 1u };
};

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    double sampleRate = 48000.0;
    uint32_t maxFrames = 0;
    ParameterTargets targets {};
    s3g::LoopProcessorEngine engine;
    std::shared_ptr<const s3g::LoopProcessorSample> sample;
    std::array<std::shared_ptr<const s3g::LoopProcessorSample>, kMaxSources> sources {};
    std::array<std::string, kMaxSources> samplePaths {};
    std::atomic<uint32_t> sourceCount { 0u };
    std::atomic<bool> playing { true };
    std::atomic<uint32_t> resetAfterStopFrames { 0u };
    bool audioPlaying = true;
    std::atomic<float> outputPeak { 0.0f };
    std::array<std::atomic<float>, kChannelCount> lanePhases {};
#if defined(__APPLE__)
    void* guiView = nullptr;
    std::atomic<bool> guiVisible { false };
#endif
};

Plugin* self(const clap_plugin_t* plugin) { return static_cast<Plugin*>(plugin->plugin_data); }

double clamp01(double v) { return std::clamp(v, 0.0, 1.0); }

s3g::LoopProcessorParams snapshotParams(const Plugin& p)
{
    s3g::LoopProcessorParams params {};
    params.baseRate = p.targets.baseRate.load(std::memory_order_acquire);
    params.rateSpread = p.targets.rateSpread.load(std::memory_order_acquire);
    params.driftAmount = p.targets.driftAmount.load(std::memory_order_acquire);
    params.relationCenter = p.targets.relationCenter.load(std::memory_order_acquire);
    params.relationGlideMs = p.targets.relationGlideMs.load(std::memory_order_acquire);
    params.loopStart = p.targets.loopStart.load(std::memory_order_acquire);
    params.loopLength = p.targets.loopLength.load(std::memory_order_acquire);
    params.xfadePct = p.targets.xfadePct.load(std::memory_order_acquire);
    params.seamDuck = p.targets.seamDuck.load(std::memory_order_acquire);
    params.gainDb = p.targets.gainDb.load(std::memory_order_acquire);
    params.launchMode = p.targets.launchMode.load(std::memory_order_acquire);
    params.laneMask = p.targets.laneMask.load(std::memory_order_acquire);
    return params;
}

const char* ruleName(uint32_t rule)
{
    switch (std::min<uint32_t>(rule, 3u)) {
    case 0: return "ORDER";
    case 2: return "RND";
    case 3: return "MORPH";
    default: return "INTER";
    }
}

void setParam(Plugin& p, clap_id id, double value)
{
    switch (id) {
    case kRateParamId: p.targets.baseRate.store(static_cast<float>(std::clamp(value, 0.125, 4.0)), std::memory_order_release); break;
    case kSpreadParamId: p.targets.rateSpread.store(static_cast<float>(std::clamp(value, -1.0, 1.0)), std::memory_order_release); break;
    case kDriftParamId: p.targets.driftAmount.store(static_cast<float>(std::clamp(value, -0.12, 0.12)), std::memory_order_release); break;
    case kCenterParamId: p.targets.relationCenter.store(static_cast<float>(clamp01(value)), std::memory_order_release); break;
    case kGlideParamId: p.targets.relationGlideMs.store(static_cast<float>(std::clamp(value, 10.0, 2000.0)), std::memory_order_release); break;
    case kLoopStartParamId: p.targets.loopStart.store(static_cast<float>(std::clamp(value, 0.0, 0.999)), std::memory_order_release); break;
    case kLoopLengthParamId: p.targets.loopLength.store(static_cast<float>(std::clamp(value, 0.01, 1.0)), std::memory_order_release); break;
    case kLaunchParamId: p.targets.launchMode.store(0u, std::memory_order_release); break;
    case kLaneMaskParamId: p.targets.laneMask.store(static_cast<uint32_t>(std::clamp(std::floor(value + 0.5), 0.0, 255.0)), std::memory_order_release); break;
    case kRuleParamId: p.targets.rule.store(static_cast<uint32_t>(std::clamp(std::floor(value + 0.5), 0.0, 3.0)), std::memory_order_release); break;
    case kSourceRateParamId: p.targets.sourceRateSpread.store(static_cast<float>(std::clamp(value, 0.0, 1.0)), std::memory_order_release); break;
    case kSourceBlendParamId: p.targets.sourceBlend.store(static_cast<float>(std::clamp(value, 0.0, 1.0)), std::memory_order_release); break;
    case kXfadeParamId: p.targets.xfadePct.store(static_cast<float>(std::clamp(value, 0.0, 0.3)), std::memory_order_release); break;
    case kDuckParamId: p.targets.seamDuck.store(static_cast<float>(std::clamp(value, 0.0, 0.75)), std::memory_order_release); break;
    case kGainParamId: p.targets.gainDb.store(static_cast<float>(std::clamp(value, -60.0, 6.0)), std::memory_order_release); break;
    default: break;
    }
}

#if defined(__APPLE__)
void resetControlsForNewSample(Plugin& p);

std::shared_ptr<s3g::LoopProcessorSample> readSampleFromPath(const std::string& path)
{
    if (path.empty()) {
        return nullptr;
    }
    @autoreleasepool {
        NSString* nsPath = [NSString stringWithUTF8String:path.c_str()];
        NSURL* url = [NSURL fileURLWithPath:nsPath];
        NSError* error = nil;
        AVAudioFile* file = [[AVAudioFile alloc] initForReading:url error:&error];
        if (!file) {
            return nullptr;
        }
        AVAudioFormat* format = [file processingFormat];
        const AVAudioFrameCount frames = static_cast<AVAudioFrameCount>(std::min<int64_t>([file length], 0x7fffffff));
        AVAudioPCMBuffer* buffer = [[AVAudioPCMBuffer alloc] initWithPCMFormat:format frameCapacity:frames];
        if (!buffer) {
            [file release];
            return nullptr;
        }
        BOOL ok = [file readIntoBuffer:buffer error:&error];
        if (!ok || buffer.frameLength < 2 || !buffer.floatChannelData) {
            [buffer release];
            [file release];
            return nullptr;
        }
        auto sample = std::make_shared<s3g::LoopProcessorSample>();
        sample->frames = buffer.frameLength;
        sample->channels = std::max<uint32_t>(1u, buffer.format.channelCount);
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

uint32_t sourceChannelForLane(uint32_t lane, uint32_t sourceChannels)
{
    if (sourceChannels <= 1u) {
        return 0u;
    }
    if (sourceChannels <= kChannelCount) {
        return lane % sourceChannels;
    }
    const double u = kChannelCount > 1u
        ? static_cast<double>(lane) / static_cast<double>(kChannelCount - 1u)
        : 0.0;
    return std::min<uint32_t>(
        sourceChannels - 1u,
        static_cast<uint32_t>(std::llround(u * static_cast<double>(sourceChannels - 1u))));
}

float readSourceLinear(const s3g::LoopProcessorSample& sample, uint32_t lane, double pos)
{
    if (sample.frames < 2u || sample.channels == 0u) {
        return 0.0f;
    }
    pos -= std::floor(pos / static_cast<double>(sample.frames)) * static_cast<double>(sample.frames);
    if (pos < 0.0) pos += static_cast<double>(sample.frames);
    const uint32_t i0 = static_cast<uint32_t>(std::floor(pos)) % sample.frames;
    const uint32_t i1 = (i0 + 1u) % sample.frames;
    const float frac = static_cast<float>(pos - std::floor(pos));
    const uint32_t srcCh = sourceChannelForLane(lane, sample.channels);
    const float a = sample.audio[static_cast<size_t>(i0) * sample.channels + srcCh];
    const float b = sample.audio[static_cast<size_t>(i1) * sample.channels + srcCh];
    return a + (b - a) * frac;
}

uint32_t hashLane(uint32_t lane, uint32_t count)
{
    uint32_t x = lane * 747796405u + 2891336453u;
    x ^= x >> 16u;
    x *= 2246822519u;
    x ^= x >> 13u;
    return count == 0u ? 0u : x % count;
}

uint32_t orderedSourceForLane(uint32_t lane, uint32_t count)
{
    const uint32_t lanesPerSource = std::max<uint32_t>(1u, (kChannelCount + count - 1u) / count);
    return std::min<uint32_t>(count - 1u, lane / lanesPerSource);
}

float sourceRateForIndex(uint32_t index, uint32_t count, float spread)
{
    spread = std::clamp(spread, 0.0f, 1.0f);
    if (count <= 1u || spread <= 0.0001f) {
        return 1.0f;
    }
    const float u = static_cast<float>(index) / static_cast<float>(count - 1u);
    const float centered = u * 2.0f - 1.0f;
    return std::pow(2.0f, centered * spread);
}

float morphBlendForLane(float x, float width)
{
    width = std::clamp(width, 0.0f, 1.0f);
    if (width <= 0.0001f) {
        return (x - std::floor(x)) >= 0.5f ? 1.0f : 0.0f;
    }
    const float frac = x - std::floor(x);
    const float half = width * 0.5f;
    const float lo = 0.5f - half;
    const float hi = 0.5f + half;
    const float u = width >= 0.999f ? frac : std::clamp((frac - lo) / std::max(0.0001f, hi - lo), 0.0f, 1.0f);
    return u * u * (3.0f - 2.0f * u);
}

void rebuildCompositeSample(Plugin& p, bool resetControls)
{
    std::array<std::shared_ptr<const s3g::LoopProcessorSample>, kMaxSources> active {};
    uint32_t count = 0;
    for (uint32_t i = 0; i < kMaxSources; ++i) {
        if (p.sources[i] && p.sources[i]->frames >= 2u && p.sources[i]->channels > 0u) {
            active[count++] = p.sources[i];
        }
    }
    p.sourceCount.store(count, std::memory_order_release);
    if (count == 0u) {
        std::shared_ptr<const s3g::LoopProcessorSample> empty;
        std::atomic_store_explicit(&p.sample, empty, std::memory_order_release);
        return;
    }

    const double compositeRate = std::max(1.0, active[0]->sampleRate);
    uint32_t compositeFrames = 2u;
    for (uint32_t i = 0; i < count; ++i) {
        const double seconds = static_cast<double>(active[i]->frames) / std::max(1.0, active[i]->sampleRate);
        compositeFrames = std::max<uint32_t>(compositeFrames, static_cast<uint32_t>(std::ceil(seconds * compositeRate)));
    }
    compositeFrames = std::min<uint32_t>(compositeFrames, static_cast<uint32_t>(compositeRate * 600.0));

    auto composite = std::make_shared<s3g::LoopProcessorSample>();
    composite->frames = compositeFrames;
    composite->channels = kChannelCount;
    composite->sampleRate = compositeRate;
    composite->path = count == 1u ? active[0]->path : "multi-source composite";
    composite->audio.assign(static_cast<size_t>(compositeFrames) * kChannelCount, 0.0f);

    const uint32_t rule = p.targets.rule.load(std::memory_order_acquire);
    const float sourceRateSpread = p.targets.sourceRateSpread.load(std::memory_order_acquire);
    const float sourceBlend = p.targets.sourceBlend.load(std::memory_order_acquire);
    for (uint32_t lane = 0; lane < kChannelCount; ++lane) {
        uint32_t a = 0;
        uint32_t b = 0;
        float blend = 0.0f;
        if (rule == 0u) {
            a = orderedSourceForLane(lane, count);
            b = a;
        } else if (rule == 2u) {
            a = hashLane(lane, count);
            b = a;
        } else if (rule == 3u && count > 1u) {
            const float u = kChannelCount > 1u ? static_cast<float>(lane) / static_cast<float>(kChannelCount - 1u) : 0.0f;
            const float x = u * static_cast<float>(count - 1u);
            a = static_cast<uint32_t>(std::floor(x));
            b = std::min<uint32_t>(count - 1u, a + 1u);
            blend = morphBlendForLane(x, sourceBlend);
            if (sourceBlend <= 0.0001f && blend >= 1.0f) {
                a = b;
                blend = 0.0f;
            }
        } else {
            a = lane % count;
            b = a;
        }
        const auto& sampleA = *active[a];
        const auto& sampleB = *active[b];
        const double rateA = static_cast<double>(sourceRateForIndex(a, count, sourceRateSpread));
        const double rateB = static_cast<double>(sourceRateForIndex(b, count, sourceRateSpread));
        for (uint32_t frame = 0; frame < compositeFrames; ++frame) {
            const double t = static_cast<double>(frame) / compositeRate;
            const float va = readSourceLinear(sampleA, lane, t * sampleA.sampleRate * rateA);
            const float vb = readSourceLinear(sampleB, lane, t * sampleB.sampleRate * rateB);
            composite->audio[static_cast<size_t>(frame) * kChannelCount + lane] = va + (vb - va) * blend;
        }
    }

    std::shared_ptr<const s3g::LoopProcessorSample> immutable = composite;
    std::atomic_store_explicit(&p.sample, immutable, std::memory_order_release);
    if (resetControls) {
        resetControlsForNewSample(p);
    }
    p.engine.reset();
}

void resetControlsForNewSample(Plugin& p)
{
    const uint32_t laneMask = p.targets.laneMask.load(std::memory_order_acquire);
    s3g::LoopProcessorParams defaults {};
    defaults.laneMask = laneMask;
    setParam(p, kRateParamId, defaults.baseRate);
    setParam(p, kSpreadParamId, defaults.rateSpread);
    setParam(p, kDriftParamId, defaults.driftAmount);
    setParam(p, kCenterParamId, defaults.relationCenter);
    setParam(p, kGlideParamId, defaults.relationGlideMs);
    setParam(p, kLoopStartParamId, defaults.loopStart);
    setParam(p, kLoopLengthParamId, defaults.loopLength);
    setParam(p, kXfadeParamId, defaults.xfadePct);
    setParam(p, kDuckParamId, defaults.seamDuck);
    setParam(p, kGainParamId, defaults.gainDb);
    setParam(p, kLaneMaskParamId, defaults.laneMask);
}

bool loadSampleFromPath(Plugin& p, const std::string& path, bool resetControls)
{
    auto sample = readSampleFromPath(path);
    if (!sample) {
        return false;
    }
    p.sources = {};
    p.samplePaths = {};
    p.sources[0] = sample;
    p.samplePaths[0] = path;
    rebuildCompositeSample(p, resetControls);
    return true;
}

void chooseSampleFromFinder(Plugin& p)
{
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    [panel setCanChooseFiles:YES];
    [panel setCanChooseDirectories:NO];
    [panel setAllowsMultipleSelection:YES];
    [panel setAllowedFileTypes:@[@"wav", @"aif", @"aiff", @"caf", @"mp3", @"m4a"]];
    if ([panel runModal] == NSModalResponseOK) {
        NSArray<NSURL*>* urls = [panel URLs];
        p.sources = {};
        p.samplePaths = {};
        uint32_t loaded = 0;
        for (NSURL* url in urls) {
            if (loaded >= kMaxSources) break;
            char path[4096] {};
            if (url && [[url path] getFileSystemRepresentation:path maxLength:sizeof(path)]) {
                auto sample = readSampleFromPath(path);
                if (sample) {
                    p.sources[loaded] = sample;
                    p.samplePaths[loaded] = path;
                    ++loaded;
                }
            }
        }
        rebuildCompositeSample(p, true);
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

clap_process_status process(const clap_plugin_t* plugin, const clap_process_t* proc)
{
    auto* p = self(plugin);
    readEvents(*p, proc->in_events);
    if (proc->audio_outputs_count == 0) {
        return CLAP_PROCESS_CONTINUE;
    }
    const auto& out = proc->audio_outputs[0];
    const uint32_t frames = proc->frames_count;
    if (out.data32) {
        for (uint32_t ch = 0; ch < std::min<uint32_t>(out.channel_count, kChannelCount); ++ch) {
            if (out.data32[ch]) {
                std::fill(out.data32[ch], out.data32[ch] + frames, 0.0f);
            }
        }
        s3g::clearAudioBufferFromChannel(out, std::min<uint32_t>(out.channel_count, kChannelCount), frames);
        float* laneOut[kChannelCount] {};
        for (uint32_t ch = 0; ch < std::min<uint32_t>(out.channel_count, kChannelCount); ++ch) {
            laneOut[ch] = out.data32[ch];
        }
        auto sample = std::atomic_load_explicit(&p->sample, std::memory_order_acquire);
        const s3g::LoopProcessorParams params = snapshotParams(*p);
        p->engine.setParams(params);
        const bool requestedPlaying = p->playing.load(std::memory_order_acquire);
        p->audioPlaying = requestedPlaying;
        p->engine.process(sample, laneOut, frames, requestedPlaying);
        if (requestedPlaying) {
            p->resetAfterStopFrames.store(0u, std::memory_order_release);
        } else {
            uint32_t resetFrames = p->resetAfterStopFrames.load(std::memory_order_acquire);
            if (resetFrames > 0u) {
                if (resetFrames <= frames) {
                    p->engine.resync();
                    p->resetAfterStopFrames.store(0u, std::memory_order_release);
                    for (uint32_t ch = 0; ch < kChannelCount; ++ch) {
                        p->lanePhases[ch].store(0.0f, std::memory_order_relaxed);
                    }
                } else {
                    p->resetAfterStopFrames.store(resetFrames - frames, std::memory_order_release);
                }
            }
        }
        float phases[kChannelCount] {};
        p->engine.lanePhases(phases, kChannelCount);
        for (uint32_t ch = 0; ch < kChannelCount; ++ch) {
            p->lanePhases[ch].store(phases[ch], std::memory_order_relaxed);
        }
        float peak = 0.0f;
        for (uint32_t ch = 0; ch < std::min<uint32_t>(out.channel_count, kChannelCount); ++ch) {
            if (!out.data32[ch]) continue;
            for (uint32_t i = 0; i < frames; ++i) peak = std::max(peak, std::fabs(out.data32[ch][i]));
        }
        p->outputPeak.store(std::max(p->outputPeak.load(std::memory_order_relaxed) * 0.90f, peak), std::memory_order_relaxed);
    } else if (out.data64) {
        for (uint32_t ch = 0; ch < out.channel_count; ++ch) {
            if (out.data64[ch]) {
                std::fill(out.data64[ch], out.data64[ch] + frames, 0.0);
            }
        }
    }
    return CLAP_PROCESS_CONTINUE;
}

void onMainThread(const clap_plugin_t*) {}

uint32_t audioPortsCount(const clap_plugin_t*, bool isInput) { return isInput ? 0u : 1u; }

bool audioPortsGet(const clap_plugin_t*, uint32_t index, bool isInput, clap_audio_port_info_t* info)
{
    if (isInput || index != 0 || !info) return false;
    info->id = 20;
    std::strncpy(info->name, "8ch Out", sizeof(info->name));
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = kChannelCount;
    info->port_type = CLAP_PORT_SURROUND;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}
const clap_plugin_audio_ports_t audioPorts { audioPortsCount, audioPortsGet };

struct ParamDef { clap_id id; const char* name; double min; double max; double def; };
constexpr ParamDef kParamDefs[] {
    { kRateParamId, "Base Rate", 0.125, 4.0, 1.0 },
    { kSpreadParamId, "Rate Spread", -1.0, 1.0, 0.08 },
    { kDriftParamId, "Rate Drift", -0.12, 0.12, 0.0 },
    { kCenterParamId, "Relationship Center", 0.0, 1.0, 0.5 },
    { kGlideParamId, "Relationship Glide", 10.0, 2000.0, 250.0 },
    { kLoopStartParamId, "Loop Start", 0.0, 0.999, 0.0 },
    { kLoopLengthParamId, "Loop Length", 0.01, 1.0, 1.0 },
    { kLaneMaskParamId, "Lane Mask", 0.0, 255.0, 255.0 },
    { kRuleParamId, "Lane Rule", 0.0, 3.0, 1.0 },
    { kSourceRateParamId, "Source Rate Spread", 0.0, 1.0, 0.0 },
    { kSourceBlendParamId, "Source Crossblend", 0.0, 1.0, 1.0 },
    { kXfadeParamId, "Loop Crossfade", 0.0, 0.3, 0.08 },
    { kDuckParamId, "Seam Duck", 0.0, 0.75, 0.12 },
    { kGainParamId, "Output Gain", -60.0, 6.0, -12.0 },
};

uint32_t paramsCount(const clap_plugin_t*) { return static_cast<uint32_t>(sizeof(kParamDefs) / sizeof(kParamDefs[0])); }

bool paramsGetInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info)
{
    if (!info || index >= paramsCount(nullptr)) return false;
    const auto& def = kParamDefs[index];
    info->id = def.id;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    if (def.id == kLaneMaskParamId || def.id == kRuleParamId) {
        info->flags |= CLAP_PARAM_IS_STEPPED;
    }
    if (def.id == kRuleParamId || def.id == kSourceRateParamId || def.id == kSourceBlendParamId) {
        info->flags &= ~CLAP_PARAM_IS_AUTOMATABLE;
    }
    std::strncpy(info->name, def.name, sizeof(info->name));
    std::strncpy(info->module, "Multi Loop Processor", sizeof(info->module));
    info->min_value = def.min;
    info->max_value = def.max;
    info->default_value = def.def;
    return true;
}

bool paramsGetValue(const clap_plugin_t* plugin, clap_id id, double* value)
{
    if (!value) return false;
    const auto* p = self(plugin);
    const s3g::LoopProcessorParams params = snapshotParams(*p);
    switch (id) {
    case kRateParamId: *value = params.baseRate; return true;
    case kSpreadParamId: *value = params.rateSpread; return true;
    case kDriftParamId: *value = params.driftAmount; return true;
    case kCenterParamId: *value = params.relationCenter; return true;
    case kGlideParamId: *value = params.relationGlideMs; return true;
    case kLoopStartParamId: *value = params.loopStart; return true;
    case kLoopLengthParamId: *value = params.loopLength; return true;
    case kLaunchParamId: *value = params.launchMode; return true;
    case kLaneMaskParamId: *value = params.laneMask; return true;
    case kRuleParamId: *value = p->targets.rule.load(std::memory_order_acquire); return true;
    case kSourceRateParamId: *value = p->targets.sourceRateSpread.load(std::memory_order_acquire); return true;
    case kSourceBlendParamId: *value = p->targets.sourceBlend.load(std::memory_order_acquire); return true;
    case kXfadeParamId: *value = params.xfadePct; return true;
    case kDuckParamId: *value = params.seamDuck; return true;
    case kGainParamId: *value = params.gainDb; return true;
    default: return false;
    }
}

bool paramsValueToText(const clap_plugin_t*, clap_id id, double value, char* display, uint32_t size)
{
    if (!display || size == 0) return false;
    if (id == kGainParamId) std::snprintf(display, size, "%+.1f dB", value);
    else if (id == kGlideParamId) std::snprintf(display, size, "%.1f ms", value);
    else if (id == kSpreadParamId) std::snprintf(display, size, "%+.0f %%", value * 100.0);
    else if (id == kDriftParamId) std::snprintf(display, size, "%+.3f", value);
    else if (id == kLoopStartParamId || id == kLoopLengthParamId || id == kXfadeParamId || id == kSourceRateParamId || id == kSourceBlendParamId) std::snprintf(display, size, "%.0f %%", value * 100.0);
    else if (id == kRuleParamId) std::snprintf(display, size, "%s", ruleName(static_cast<uint32_t>(std::clamp(std::floor(value + 0.5), 0.0, 3.0))));
    else if (id == kLaneMaskParamId) std::snprintf(display, size, "0x%02X", static_cast<uint32_t>(std::clamp(std::floor(value + 0.5), 0.0, 255.0)));
    else std::snprintf(display, size, "%.3f", value);
    return true;
}

bool paramsTextToValue(const clap_plugin_t*, clap_id id, const char* display, double* value)
{
    if (!display || !value) return false;
    double v = std::atof(display);
    if ((id == kXfadeParamId || id == kLoopStartParamId || id == kLoopLengthParamId) && v > 1.0) {
        v *= 0.01;
    } else if ((id == kSourceRateParamId || id == kSourceBlendParamId) && v > 1.0) {
        v *= 0.01;
    } else if (id == kSpreadParamId && (v > 1.0 || v < -1.0)) {
        v *= 0.01;
    }
    *value = v;
    return true;
}

void paramsFlush(const clap_plugin_t* plugin, const clap_input_events_t* in, const clap_output_events_t*) { readEvents(*self(plugin), in); }
const clap_plugin_params_t paramsExt { paramsCount, paramsGetInfo, paramsGetValue, paramsValueToText, paramsTextToValue, paramsFlush };

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    if (!stream || !stream->write) return false;
    const auto* p = self(plugin);
    const s3g::LoopProcessorParams params = snapshotParams(*p);
    SavedState s {};
    s.baseRate = params.baseRate;
    s.rateSpread = params.rateSpread;
    s.driftAmount = params.driftAmount;
    s.relationCenter = params.relationCenter;
    s.relationGlideMs = params.relationGlideMs;
    s.loopStart = params.loopStart;
    s.loopLength = params.loopLength;
    s.xfadePct = params.xfadePct;
    s.seamDuck = params.seamDuck;
    s.gainDb = params.gainDb;
    s.sourceRateSpread = p->targets.sourceRateSpread.load(std::memory_order_acquire);
    s.sourceBlend = p->targets.sourceBlend.load(std::memory_order_acquire);
    s.launchMode = 0u;
    s.laneMask = params.laneMask;
    s.rule = p->targets.rule.load(std::memory_order_acquire);
    s.playing = p->playing.load(std::memory_order_acquire) ? 1u : 0u;
    for (uint32_t i = 0; i < kMaxSources; ++i) {
        std::strncpy(s.samplePaths[i], p->samplePaths[i].c_str(), sizeof(s.samplePaths[i]) - 1u);
    }
    return stream->write(stream, &s, sizeof(s)) == static_cast<int64_t>(sizeof(s));
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) return false;
    SavedState s {};
    if (stream->read(stream, &s, sizeof(s)) != static_cast<int64_t>(sizeof(s)) || s.version != kStateVersion) return false;
    auto* p = self(plugin);
    setParam(*p, kRateParamId, s.baseRate);
    setParam(*p, kSpreadParamId, s.rateSpread);
    setParam(*p, kDriftParamId, s.driftAmount);
    setParam(*p, kCenterParamId, s.relationCenter);
    setParam(*p, kGlideParamId, s.relationGlideMs);
    setParam(*p, kLoopStartParamId, s.loopStart);
    setParam(*p, kLoopLengthParamId, s.loopLength);
    setParam(*p, kLaunchParamId, 0.0);
    setParam(*p, kLaneMaskParamId, s.laneMask);
    setParam(*p, kRuleParamId, s.rule);
    setParam(*p, kSourceRateParamId, s.sourceRateSpread);
    setParam(*p, kSourceBlendParamId, s.sourceBlend);
    setParam(*p, kXfadeParamId, s.xfadePct);
    setParam(*p, kDuckParamId, s.seamDuck);
    setParam(*p, kGainParamId, s.gainDb);
    p->playing.store(s.playing != 0u, std::memory_order_release);
#if defined(__APPLE__)
    p->sources = {};
    p->samplePaths = {};
    for (uint32_t i = 0; i < kMaxSources; ++i) {
        if (s.samplePaths[i][0] == '\0') continue;
        auto sample = readSampleFromPath(s.samplePaths[i]);
        if (sample) {
            p->sources[i] = sample;
            p->samplePaths[i] = s.samplePaths[i];
        }
    }
    rebuildCompositeSample(*p, false);
#endif
    return true;
}
const clap_plugin_state_t stateExt { stateSave, stateLoad };

} // namespace

#if defined(__APPLE__)
@interface S3GMultiLoopProcessorView : NSView {
    void* _plugin;
    int _dragSlider;
    NSTimer* _timer;
    BOOL _engineOpen;
    BOOL _relationshipsOpen;
    int _openMenu;
    NSPoint _menuOrigin;
    uint32_t _menuItemCount;
    CGFloat _waveZoom;
    CGFloat _waveScroll;
}
- (id)initWithPlugin:(void*)plugin;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
- (void)drawSlider:(NSString*)name value:(NSString*)val norm:(CGFloat)n y:(CGFloat)y attrs:(NSDictionary*)attrs small:(NSDictionary*)small;
- (void)drawMenuControl:(NSString*)name value:(NSString*)value y:(CGFloat)y attrs:(NSDictionary*)attrs small:(NSDictionary*)small;
- (void)drawSectionHeader:(NSString*)title open:(BOOL)open y:(CGFloat)y attrs:(NSDictionary*)attrs;
- (void)drawText:(NSString*)text centeredInRect:(NSRect)rect attrs:(NSDictionary*)attrs;
- (void)drawTransportButton:(NSRect)rect kind:(int)kind active:(BOOL)active;
- (void)drawWaveform:(const std::shared_ptr<const s3g::LoopProcessorSample>&)sample rect:(NSRect)rect attrs:(NSDictionary*)attrs;
- (void)drawRelationshipPreview:(NSRect)rect attrs:(NSDictionary*)attrs;
- (void)drawLaneStrip:(NSRect)rect attrs:(NSDictionary*)attrs;
- (void)updateSlider:(NSPoint)pt;
@end

static NSColor* c(int rgb) { return s3g::clap_gui::color(rgb); }

static CGFloat wrapUnitCGFloat(CGFloat value)
{
    value -= std::floor(value);
    return value < 0.0 ? value + 1.0 : value;
}

static uint32_t energeticChannelForDisplay(const s3g::LoopProcessorSample& sample)
{
    if (sample.frames < 2u || sample.channels == 0u || sample.audio.empty()) {
        return 0u;
    }
    uint32_t bestChannel = 0u;
    float bestScore = -1.0f;
    const uint32_t scoreSteps = std::min<uint32_t>(4096u, sample.frames);
    const uint32_t scoreStride = std::max<uint32_t>(1u, sample.frames / scoreSteps);
    for (uint32_t ch = 0; ch < sample.channels; ++ch) {
        float peak = 0.0f;
        float sum = 0.0f;
        uint32_t count = 0u;
        for (uint32_t i = 0; i < sample.frames; i += scoreStride) {
            const float value = std::fabs(sample.audio[static_cast<size_t>(i) * sample.channels + ch]);
            peak = std::max(peak, value);
            sum += value;
            ++count;
        }
        const float score = peak + (count > 0u ? sum / static_cast<float>(count) : 0.0f);
        if (score > bestScore) {
            bestScore = score;
            bestChannel = ch;
        }
    }
    return bestChannel;
}

static void drawMiniWaveform(const s3g::LoopProcessorSample* sample, NSRect rect, uint32_t channel)
{
    [c(0x0d0d0d) setFill];
    NSRectFill(rect);
    [c(0x3f3f3f) setStroke];
    NSFrameRect(rect);
    [c(0x252525) setStroke];
    [NSBezierPath strokeLineFromPoint:NSMakePoint(rect.origin.x + 1.0, NSMidY(rect))
                              toPoint:NSMakePoint(NSMaxX(rect) - 1.0, NSMidY(rect))];
    if (!sample || sample->frames < 2u || sample->channels == 0u || sample->audio.empty()) {
        return;
    }
    channel = std::min<uint32_t>(channel, sample->channels - 1u);
    const CGFloat waveX = rect.origin.x + 2.0;
    const CGFloat waveW = std::max<CGFloat>(1.0, rect.size.width - 4.0);
    const CGFloat midY = NSMidY(rect);
    const CGFloat scaleY = rect.size.height * 0.43;
    const uint32_t columns = static_cast<uint32_t>(std::max<CGFloat>(16.0, std::floor(waveW)));
    NSBezierPath* wave = [NSBezierPath bezierPath];
    [wave setLineWidth:0.75];
    for (uint32_t x = 0; x < columns; ++x) {
        const double ua = static_cast<double>(x) / static_cast<double>(std::max<uint32_t>(1u, columns));
        const double ub = static_cast<double>(x + 1u) / static_cast<double>(std::max<uint32_t>(1u, columns));
        const uint32_t a = std::min<uint32_t>(sample->frames - 1u, static_cast<uint32_t>(std::floor(ua * sample->frames)));
        const uint32_t b = std::max<uint32_t>(a + 1u, std::min<uint32_t>(sample->frames, static_cast<uint32_t>(std::ceil(ub * sample->frames))));
        const uint32_t stride = std::max<uint32_t>(1u, (b - a) / 16u);
        float lo = 0.0f;
        float hi = 0.0f;
        for (uint32_t i = a; i < b; i += stride) {
            const float value = sample->audio[static_cast<size_t>(i) * sample->channels + channel];
            lo = std::min(lo, value);
            hi = std::max(hi, value);
        }
        const CGFloat px = waveX + static_cast<CGFloat>(x);
        const CGFloat yA = std::clamp<CGFloat>(midY - static_cast<CGFloat>(hi) * scaleY, rect.origin.y + 2.0, NSMaxY(rect) - 2.0);
        const CGFloat yB = std::clamp<CGFloat>(midY - static_cast<CGFloat>(lo) * scaleY, rect.origin.y + 2.0, NSMaxY(rect) - 2.0);
        [wave moveToPoint:NSMakePoint(px, yA)];
        [wave lineToPoint:NSMakePoint(px, yB)];
    }
    [c(0x8a8a8a) setStroke];
    [wave stroke];
}

@implementation S3GMultiLoopProcessorView
- (id)initWithPlugin:(void*)plugin
{
    self = [super initWithFrame:NSMakeRect(0,0,kGuiW,kGuiH)];
    if (self) {
        _plugin = plugin;
        _dragSlider = -1;
        _timer = nil;
        _engineOpen = YES;
        _relationshipsOpen = YES;
        _openMenu = 0;
        _menuOrigin = NSMakePoint(0, 0);
        _menuItemCount = 0;
        _waveZoom = 1.0;
        _waveScroll = 0.0;
    }
    return self;
}
- (BOOL)isFlipped { return YES; }
- (void)dealloc { [self stopRefreshTimer]; [super dealloc]; }
- (void)startRefreshTimer { if (_timer) return; _timer = [NSTimer timerWithTimeInterval:1.0/20.0 target:self selector:@selector(refresh:) userInfo:nil repeats:YES]; [[NSRunLoop mainRunLoop] addTimer:_timer forMode:NSRunLoopCommonModes]; }
- (void)stopRefreshTimer { if (_timer) { [_timer invalidate]; _timer = nil; } }
- (void)refresh:(NSTimer*)timer { (void)timer; if (![self isHidden] && _plugin && s3g::clap_support::hostAppIsActive()) [self setNeedsDisplay:YES]; }
- (void)drawSlider:(NSString*)name value:(NSString*)val norm:(CGFloat)n y:(CGFloat)y attrs:(NSDictionary*)attrs small:(NSDictionary*)small
{
    s3g::clap_gui::Style style;
    s3g::clap_gui::drawSlider(name, val, n, y, attrs, small, style, kSliderLabelX, kSliderTrackX, kSliderValueX);
}
- (void)drawMenuControl:(NSString*)name value:(NSString*)value y:(CGFloat)y attrs:(NSDictionary*)attrs small:(NSDictionary*)small
{
    s3g::clap_gui::Style style;
    s3g::clap_gui::drawMenu(name, value, y, attrs, small, style, kSliderLabelX, kSliderTrackX);
}
- (void)drawSectionHeader:(NSString*)title open:(BOOL)open y:(CGFloat)y attrs:(NSDictionary*)attrs
{
    s3g::clap_gui::Style style;
    s3g::clap_gui::drawPanelHeader(title, open, kToolboxX, y, kToolboxW, 20.0, attrs, style);
}
- (void)drawText:(NSString*)text centeredInRect:(NSRect)rect attrs:(NSDictionary*)attrs
{
    const NSSize size = [text sizeWithAttributes:attrs];
    const CGFloat x = rect.origin.x + (rect.size.width - size.width) * 0.5;
    const CGFloat y = rect.origin.y + (rect.size.height - size.height) * 0.5 - 0.5;
    [text drawAtPoint:NSMakePoint(x, y) withAttributes:attrs];
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
- (void)drawWaveform:(const std::shared_ptr<const s3g::LoopProcessorSample>&)sample rect:(NSRect)rect attrs:(NSDictionary*)attrs
{
    auto* p = static_cast<Plugin*>(_plugin);
    const s3g::LoopProcessorParams params = snapshotParams(*p);
    [c(0x111111) setFill];
    NSRectFill(rect);
    [c(0x444444) setStroke];
    NSFrameRect(rect);
    const CGFloat midY = NSMidY(rect);
    [c(0x2a2a2a) setStroke];
    NSBezierPath* center = [NSBezierPath bezierPath];
    [center moveToPoint:NSMakePoint(rect.origin.x + 1.0, midY)];
    [center lineToPoint:NSMakePoint(NSMaxX(rect) - 1.0, midY)];
    [center stroke];
    if (!sample || sample->frames < 2u || sample->channels == 0u || sample->audio.empty()) {
        [@"NO WAVEFORM" drawAtPoint:NSMakePoint(rect.origin.x + 10.0, rect.origin.y + 10.0) withAttributes:attrs];
        return;
    }

    uint32_t bestChannel = 0u;
    float bestScore = -1.0f;
    const uint32_t scoreSteps = std::min<uint32_t>(4096u, sample->frames);
    const uint32_t scoreStride = std::max<uint32_t>(1u, sample->frames / scoreSteps);
    for (uint32_t ch = 0; ch < sample->channels; ++ch) {
        float peak = 0.0f;
        float sum = 0.0f;
        uint32_t count = 0u;
        for (uint32_t i = 0; i < sample->frames; i += scoreStride) {
            const float value = std::fabs(sample->audio[static_cast<size_t>(i) * sample->channels + ch]);
            peak = std::max(peak, value);
            sum += value;
            ++count;
        }
        const float score = peak + (count > 0u ? sum / static_cast<float>(count) : 0.0f);
        if (score > bestScore) {
            bestScore = score;
            bestChannel = ch;
        }
    }

    const CGFloat waveX = rect.origin.x + 4.0;
    const CGFloat waveW = rect.size.width - 8.0;
    const CGFloat loopStartUnit = std::clamp<CGFloat>(params.loopStart, 0.0, 1.0);
    const CGFloat loopEndUnit = std::clamp<CGFloat>(params.loopStart + params.loopLength, 0.0, 1.0);
    const CGFloat zoom = std::clamp<CGFloat>(_waveZoom, 1.0, 16.0);
    const CGFloat viewSpan = 1.0 / zoom;
    _waveScroll = zoom <= 1.0001 ? 0.0 : std::clamp<CGFloat>(_waveScroll, 0.0, 1.0);
    const CGFloat viewStart = (1.0 - viewSpan) * _waveScroll;
    auto unitToX = [&](CGFloat unit) -> CGFloat {
        return waveX + ((unit - viewStart) / viewSpan) * waveW;
    };
    auto screenToUnit = [&](CGFloat x) -> CGFloat {
        return std::clamp<CGFloat>(viewStart + ((x - waveX) / waveW) * viewSpan, 0.0, 1.0);
    };

    const uint32_t columns = static_cast<uint32_t>(std::max<CGFloat>(32.0, std::floor(waveW)));
    const CGFloat usableH = rect.size.height - 18.0;
    const CGFloat scaleY = usableH * 0.48;
    NSBezierPath* wave = [NSBezierPath bezierPath];
    [wave setLineWidth:0.75];
    [NSGraphicsContext saveGraphicsState];
    NSRectClip(NSInsetRect(rect, 1.0, 1.0));
    for (uint32_t x = 0; x < columns; ++x) {
        const CGFloat unitA = screenToUnit(waveX + static_cast<CGFloat>(x));
        const CGFloat unitB = screenToUnit(waveX + static_cast<CGFloat>(x + 1u));
        const uint32_t a = static_cast<uint32_t>(std::floor(unitA * static_cast<CGFloat>(sample->frames)));
        const uint32_t b = std::max<uint32_t>(a + 1u, static_cast<uint32_t>(std::ceil(unitB * static_cast<CGFloat>(sample->frames))));
        const uint32_t stride = std::max<uint32_t>(1u, (b - a) / 32u);
        float lo = 0.0f;
        float hi = 0.0f;
        for (uint32_t i = a; i < std::min<uint32_t>(b, sample->frames); i += stride) {
            const float value = sample->audio[static_cast<size_t>(i) * sample->channels + bestChannel];
            lo = std::min(lo, value);
            hi = std::max(hi, value);
        }
        const CGFloat px = waveX + static_cast<CGFloat>(x);
        const CGFloat yA = midY - static_cast<CGFloat>(hi) * scaleY;
        const CGFloat yB = midY - static_cast<CGFloat>(lo) * scaleY;
        [wave moveToPoint:NSMakePoint(px, std::clamp(yA, rect.origin.y + 4.0, NSMaxY(rect) - 14.0))];
        [wave lineToPoint:NSMakePoint(px, std::clamp(yB, rect.origin.y + 4.0, NSMaxY(rect) - 14.0))];
    }
    [c(0x747474) setStroke];
    [wave stroke];
    const CGFloat labelBandH = 18.0;
    const CGFloat markerTop = rect.origin.y + 4.0;
    const CGFloat markerBottom = NSMaxY(rect) - labelBandH - 2.0;
    const CGFloat sx = unitToX(loopStartUnit);
    const CGFloat ex = unitToX(loopEndUnit);
    [c(0x7f9aa0) setStroke];
    [NSBezierPath strokeLineFromPoint:NSMakePoint(sx, markerTop) toPoint:NSMakePoint(sx, markerBottom)];
    [NSBezierPath strokeLineFromPoint:NSMakePoint(ex, markerTop) toPoint:NSMakePoint(ex, markerBottom)];
    [c(0x7f9aa0) setFill];
    NSRectFill(NSMakeRect(sx - 2.0, markerTop, 4.0, 2.0));
    NSRectFill(NSMakeRect(ex - 2.0, markerTop, 4.0, 2.0));
    NSRectFill(NSMakeRect(sx - 2.0, markerBottom - 2.0, 4.0, 2.0));
    NSRectFill(NSMakeRect(ex - 2.0, markerBottom - 2.0, 4.0, 2.0));
    const CGFloat loopX = sx;
    const CGFloat loopW = std::max<CGFloat>(1.0, ex - sx);
    for (uint32_t lane = 0; lane < s3g::kLoopProcessorChannels; ++lane) {
        if ((params.laneMask & (1u << lane)) == 0u) {
            continue;
        }
        const CGFloat phase = wrapUnitCGFloat(std::clamp<CGFloat>(p->lanePhases[lane].load(std::memory_order_relaxed), 0.0, 1.0));
        const CGFloat px = loopX + phase * loopW;
        const CGFloat laneStep = (markerBottom - markerTop) / static_cast<CGFloat>(s3g::kLoopProcessorChannels);
        const CGFloat tickY = markerTop + static_cast<CGFloat>(lane) * laneStep + laneStep * 0.5;
        const CGFloat tickH = std::max<CGFloat>(5.0, laneStep * 0.55);
        [c(0x676767) setStroke];
        [NSBezierPath strokeLineFromPoint:NSMakePoint(px, markerTop) toPoint:NSMakePoint(px, markerBottom)];
        [c(0x9a9a9a) setFill];
        NSRectFill(NSMakeRect(px - 0.5, tickY - tickH * 0.5, 1.0, tickH));
        [c(0xf0f0f0) setFill];
        NSRectFill(NSMakeRect(px - 0.5, markerTop, 1.0, markerBottom - markerTop));
        NSRectFill(NSMakeRect(px - 2.5, tickY - 1.5, 5.0, 3.0));
    }
    [NSGraphicsContext restoreGraphicsState];
    if (zoom > 1.0001) {
        const CGFloat scrollX = waveX;
        const CGFloat scrollW = waveW;
        const CGFloat scrollY = NSMaxY(rect) + 6.0;
        const CGFloat thumbW = std::max<CGFloat>(24.0, scrollW * viewSpan);
        const CGFloat thumbX = scrollX + (scrollW - thumbW) * _waveScroll;
        [c(0x262626) setFill];
        NSRectFill(NSMakeRect(scrollX, scrollY, scrollW, 3.0));
        [c(0xb0b0b0) setFill];
        NSRectFill(NSMakeRect(thumbX, scrollY - 1.0, thumbW, 5.0));
    }
    [[NSString stringWithFormat:@"WAVE CH%u  ZM %.1fx  GLD %.0f", bestChannel + 1u, zoom, params.relationGlideMs] drawAtPoint:NSMakePoint(rect.origin.x + 10.0, NSMaxY(rect) - 16.0) withAttributes:attrs];
}
- (void)drawLaneStrip:(NSRect)rect attrs:(NSDictionary*)attrs
{
    auto* p = static_cast<Plugin*>(_plugin);
    const s3g::LoopProcessorParams params = snapshotParams(*p);
    [@"OUT" drawAtPoint:NSMakePoint(rect.origin.x + 12.0, rect.origin.y + 4.0) withAttributes:attrs];
    const CGFloat cell = 30.0;
    for (uint32_t lane = 0; lane < s3g::kLoopProcessorChannels; ++lane) {
        const bool on = (params.laneMask & (1u << lane)) != 0u;
        NSRect r = NSMakeRect(rect.origin.x + 44.0 + static_cast<CGFloat>(lane) * cell, rect.origin.y, 20.0, 18.0);
        [c(on ? 0xd1d1d1 : 0x141414) setFill];
        NSRectFill(r);
        [c(0xd1d1d1) setStroke];
        NSFrameRect(r);
        NSDictionary* a = on ? @{ NSForegroundColorAttributeName:c(0x0c0c0c), NSFontAttributeName:[attrs objectForKey:NSFontAttributeName] } : attrs;
        [[NSString stringWithFormat:@"%u", lane + 1u] drawAtPoint:NSMakePoint(r.origin.x + 6.0, r.origin.y + 2.0) withAttributes:a];
    }
}
- (void)drawRelationshipPreview:(NSRect)rect attrs:(NSDictionary*)attrs
{
    auto* p = static_cast<Plugin*>(_plugin);
    const s3g::LoopProcessorParams params = snapshotParams(*p);
    [c(0x111111) setFill];
    NSRectFill(rect);
    [c(0x444444) setStroke];
    NSFrameRect(rect);
    [@"LANE RATE REL" drawAtPoint:NSMakePoint(rect.origin.x + 10.0, rect.origin.y + 8.0) withAttributes:attrs];

    const CGFloat labelX = rect.origin.x + 10.0;
    const CGFloat midX = rect.origin.x + rect.size.width * 0.54;
    const CGFloat barMaxW = rect.size.width * 0.34;
    const CGFloat driftMaxW = rect.size.width * 0.14;
    const CGFloat y0 = rect.origin.y + 32.0;
    const CGFloat step = (rect.size.height - 50.0) / static_cast<CGFloat>(s3g::kLoopProcessorChannels - 1u);
    const float spread = std::clamp(params.rateSpread, -1.0f, 1.0f);
    const float drift = std::clamp(params.driftAmount / 0.12f, -1.0f, 1.0f);
    [c(0x2f2f2f) setStroke];
    NSBezierPath* axis = [NSBezierPath bezierPath];
    [axis moveToPoint:NSMakePoint(midX, y0 - 6.0)];
    [axis lineToPoint:NSMakePoint(midX, y0 + step * static_cast<CGFloat>(s3g::kLoopProcessorChannels - 1u) + 6.0)];
    [axis stroke];
    for (uint32_t lane = 0; lane < s3g::kLoopProcessorChannels; ++lane) {
        const float u = s3g::kLoopProcessorChannels > 1u
            ? static_cast<float>(lane) / static_cast<float>(s3g::kLoopProcessorChannels - 1u)
            : 0.5f;
        const float rel = std::clamp((u - params.relationCenter) * 2.0f, -1.0f, 1.0f);
        const float rate = params.baseRate * (1.0f + rel * params.rateSpread);
        const CGFloat y = y0 + static_cast<CGFloat>(lane) * step;
        [[NSString stringWithFormat:@"L%u", lane + 1u] drawAtPoint:NSMakePoint(labelX, y - 5.0) withAttributes:attrs];
        const CGFloat signedRel = static_cast<CGFloat>(rel * spread);
        const CGFloat w = std::clamp<CGFloat>(std::fabs(signedRel), 0.0, 1.0) * barMaxW;
        const NSRect bar = signedRel >= 0.0
            ? NSMakeRect(midX, y - 2.0, w, 5.0)
            : NSMakeRect(midX - w, y - 2.0, w, 5.0);
        [c(0xd1d1d1) setFill];
        NSRectFill(bar);
        if (std::fabs(drift) > 0.0001f) {
            const CGFloat driftPhase = static_cast<CGFloat>(((lane * 5u) % 7u) + 1u) / 8.0;
            const CGFloat driftW = std::fabs(drift) * (0.35 + 0.65 * driftPhase) * driftMaxW;
            const CGFloat driftX = drift >= 0.0f ? NSMaxX(rect) - 98.0 : NSMaxX(rect) - 98.0 - driftW;
            [c(0x7f7f7f) setFill];
            NSRectFill(NSMakeRect(driftX, y - 1.0, driftW, 3.0));
        }
        [[NSString stringWithFormat:@"%.2f", rate] drawAtPoint:NSMakePoint(NSMaxX(rect) - 50.0, y - 5.0) withAttributes:attrs];
    }
}
- (void)drawRect:(NSRect)dirty
{
    (void)dirty;
    auto* p = static_cast<Plugin*>(_plugin);
    const s3g::LoopProcessorParams params = snapshotParams(*p);
    [c(0x0c0c0c) setFill]; NSRectFill([self bounds]);
    NSFont* mono = [NSFont fontWithName:@"Menlo" size:10] ?: [NSFont monospacedSystemFontOfSize:10 weight:NSFontWeightRegular];
    NSFont* titleFont = [NSFont fontWithName:@"Menlo" size:10.5] ?: [NSFont monospacedSystemFontOfSize:10.5 weight:NSFontWeightRegular];
    NSDictionary* lab = @{ NSForegroundColorAttributeName:c(0xf0f0f0), NSFontAttributeName:mono };
    NSDictionary* section = @{ NSForegroundColorAttributeName:c(0xd1d1d1), NSFontAttributeName:mono };
    NSDictionary* small = @{ NSForegroundColorAttributeName:c(0xa0a0a0), NSFontAttributeName:mono };
    NSDictionary* titleAttrs = @{ NSForegroundColorAttributeName:c(0xf0f0f0), NSFontAttributeName:titleFont };
    [@"s3g MULTI LOOP PROCESSOR" drawAtPoint:NSMakePoint(18,13) withAttributes:titleAttrs];
    const float pk = p->outputPeak.load(std::memory_order_relaxed);
    [[NSString stringWithFormat:@"PK %+4.1f", 20.0 * std::log10(std::max(0.000001f, pk))] drawAtPoint:NSMakePoint(720,14) withAttributes:small];
    [@"8CH" drawAtPoint:NSMakePoint(866,14) withAttributes:small];
    NSRect samplePanel = NSMakeRect(18,42,560,572);
    [c(0x1d1d1d) setFill]; NSRectFill(samplePanel);
    [c(0x626262) setStroke]; NSFrameRect(samplePanel);
    [c(0x141414) setFill];
    NSRectFill(NSMakeRect(samplePanel.origin.x, samplePanel.origin.y, samplePanel.size.width, 21.0));
    [c(0xd1d1d1) setFill];
    NSRectFill(NSMakeRect(samplePanel.origin.x, samplePanel.origin.y, samplePanel.size.width, 2.0));
    [@"SAMPLE" drawAtPoint:NSMakePoint(28,47) withAttributes:section];
    [@"ZM" drawAtPoint:NSMakePoint(494,47) withAttributes:small];
    NSRect zoomOut = NSMakeRect(516,46,18,15);
    NSRect zoomIn = NSMakeRect(540,46,18,15);
    [c(0x141414) setFill]; NSRectFill(zoomOut); NSRectFill(zoomIn);
    [c(0xd1d1d1) setStroke]; NSFrameRect(zoomOut); NSFrameRect(zoomIn);
    [@"-" drawAtPoint:NSMakePoint(522,47) withAttributes:small];
    [@"+" drawAtPoint:NSMakePoint(546,47) withAttributes:small];
    NSRect load = NSMakeRect(28,82,72,24);
    [c(0x141414) setFill]; NSRectFill(load);
    [c(0xd1d1d1) setStroke]; NSFrameRect(load);
    [self drawText:@"LOAD" centeredInRect:load attrs:lab];
    const bool isPlaying = p->playing.load(std::memory_order_acquire);
    [self drawTransportButton:NSMakeRect(116,83,26,22) kind:0 active:isPlaying];
    [self drawTransportButton:NSMakeRect(150,83,26,22) kind:1 active:!isPlaying];
    [self drawTransportButton:NSMakeRect(184,83,26,22) kind:2 active:NO];
    NSRect sync = NSMakeRect(226,83,54,22);
    [c(0x141414) setFill]; NSRectFill(sync);
    [c(0xd1d1d1) setStroke]; NSFrameRect(sync);
    [self drawText:@"SYNC" centeredInRect:sync attrs:lab];
    auto sample = std::atomic_load_explicit(&p->sample, std::memory_order_acquire);
    const uint32_t sourceCount = p->sourceCount.load(std::memory_order_acquire);
    NSString* name = sample ? [NSString stringWithUTF8String:sample->path.c_str()] : @"NO SAMPLE LOADED";
    NSString* last = sourceCount > 1u ? [NSString stringWithFormat:@"%u FILES -> 8 LANE COMPOSITE", sourceCount] : [name lastPathComponent];
    [last drawAtPoint:NSMakePoint(28,124) withAttributes:lab];
    if (sample) {
        [[NSString stringWithFormat:@"%u FR / %u CH / %.0f Hz", sample->frames, sample->channels, sample->sampleRate] drawAtPoint:NSMakePoint(28,144) withAttributes:small];
        [[NSString stringWithFormat:@"%s / %u SOURCE%s / SRAT %.0f / XBLD %.0f",
            ruleName(p->targets.rule.load(std::memory_order_acquire)),
            sourceCount,
            sourceCount == 1u ? "" : "S",
            p->targets.sourceRateSpread.load(std::memory_order_acquire) * 100.0f,
            p->targets.sourceBlend.load(std::memory_order_acquire) * 100.0f] drawAtPoint:NSMakePoint(28,162) withAttributes:small];
    }
    [self drawLaneStrip:NSMakeRect(292,85,276,18) attrs:small];
    NSRect sourceBox = NSMakeRect(28,184,540,164);
    [c(0x111111) setFill]; NSRectFill(sourceBox);
    [c(0x444444) setStroke]; NSFrameRect(sourceBox);
    [@"SOURCES" drawAtPoint:NSMakePoint(sourceBox.origin.x + 10.0, sourceBox.origin.y + 7.0) withAttributes:small];
    [@"ENERGETIC CH DISPLAY / LANE MAP" drawAtPoint:NSMakePoint(sourceBox.origin.x + 316.0, sourceBox.origin.y + 7.0) withAttributes:small];
    for (uint32_t slot = 0; slot < kMaxSources; ++slot) {
        const CGFloat rowY = sourceBox.origin.y + 26.0 + static_cast<CGFloat>(slot) * 33.0;
        const bool active = p->sources[slot] != nullptr;
        NSDictionary* rowAttrs = active ? lab : small;
        NSString* nameText = nil;
        NSString* infoText = nil;
        uint32_t displayChannel = 0u;
        if (active) {
            displayChannel = energeticChannelForDisplay(*p->sources[slot]);
            NSString* path = [NSString stringWithUTF8String:p->samplePaths[slot].c_str()];
            NSString* base = [path lastPathComponent];
            nameText = [NSString stringWithFormat:@"%u  %@", slot + 1u, base];
            infoText = [NSString stringWithFormat:@"%uCH %.0f  SHOW %u", p->sources[slot]->channels, p->sources[slot]->sampleRate, displayChannel + 1u];
        } else {
            nameText = [NSString stringWithFormat:@"%u  --", slot + 1u];
            infoText = @"";
        }
        [nameText drawInRect:NSMakeRect(sourceBox.origin.x + 12.0, rowY, 196.0, 13.0) withAttributes:rowAttrs];
        [infoText drawInRect:NSMakeRect(sourceBox.origin.x + 12.0, rowY + 14.0, 196.0, 13.0) withAttributes:small];
        drawMiniWaveform(active ? p->sources[slot].get() : nullptr, NSMakeRect(sourceBox.origin.x + 220.0, rowY - 1.0, 306.0, 27.0), displayChannel);
    }
    [self drawWaveform:sample rect:NSMakeRect(28,362,540,232) attrs:small];

    const CGFloat panelX = kToolboxX;
    const CGFloat panelW = kToolboxW;
    const CGFloat headerH = 20.0;
    const CGFloat gap = 10.0;
    auto panelFrame = [&](CGFloat y, CGFloat h) {
        s3g::clap_gui::Style style;
        s3g::clap_gui::drawPanelFrame(panelX, y, panelW, h, style);
    };
    CGFloat y = 42.0;
    const CGFloat engineH = _engineOpen ? 246.0 : headerH;
    panelFrame(y, engineH);
    [self drawSectionHeader:@"ENGINE" open:_engineOpen y:y attrs:section];
    if (_engineOpen) {
        [self drawMenuControl:@"RULE" value:[NSString stringWithUTF8String:ruleName(p->targets.rule.load(std::memory_order_acquire))] y:y + 28 attrs:small small:small];
        [self drawSlider:@"SRAT" value:[NSString stringWithFormat:@"%.0f%%", p->targets.sourceRateSpread.load(std::memory_order_acquire) * 100.0f] norm:p->targets.sourceRateSpread.load(std::memory_order_acquire) y:y + 52 attrs:small small:small];
        [self drawSlider:@"XBLD" value:[NSString stringWithFormat:@"%.0f%%", p->targets.sourceBlend.load(std::memory_order_acquire) * 100.0f] norm:p->targets.sourceBlend.load(std::memory_order_acquire) y:y + 76 attrs:small small:small];
        [self drawSlider:@"RATE" value:[NSString stringWithFormat:@"%.3f", params.baseRate] norm:(params.baseRate - 0.125) / (4.0 - 0.125) y:y + 100 attrs:small small:small];
        [self drawSlider:@"XFD" value:[NSString stringWithFormat:@"%.0f%%", params.xfadePct * 100.0f] norm:params.xfadePct / 0.3f y:y + 124 attrs:small small:small];
        [self drawSlider:@"DUCK" value:[NSString stringWithFormat:@"%.2f", params.seamDuck] norm:params.seamDuck / 0.75f y:y + 148 attrs:small small:small];
        [self drawSlider:@"STRT" value:[NSString stringWithFormat:@"%.0f%%", params.loopStart * 100.0f] norm:params.loopStart / 0.999f y:y + 172 attrs:small small:small];
        [self drawSlider:@"LEN" value:[NSString stringWithFormat:@"%.0f%%", params.loopLength * 100.0f] norm:(params.loopLength - 0.01f) / 0.99f y:y + 196 attrs:small small:small];
        [self drawSlider:@"OUT" value:[NSString stringWithFormat:@"%+.1f", params.gainDb] norm:(params.gainDb + 60.0f) / 66.0f y:y + 220 attrs:small small:small];
    }
    y += engineH + gap;
    const CGFloat relH = _relationshipsOpen ? 260.0 : headerH;
    panelFrame(y, relH);
    [self drawSectionHeader:@"RELATIONSHIPS" open:_relationshipsOpen y:y attrs:section];
    if (_relationshipsOpen) {
        [self drawSlider:@"SPRD" value:[NSString stringWithFormat:@"%+.0f%%", params.rateSpread * 100.0f] norm:(params.rateSpread + 1.0f) * 0.5f y:y + 28 attrs:small small:small];
        [self drawSlider:@"DRFT" value:[NSString stringWithFormat:@"%+.3f", params.driftAmount] norm:(params.driftAmount + 0.12f) / 0.24f y:y + 52 attrs:small small:small];
        [self drawSlider:@"CTR" value:[NSString stringWithFormat:@"%.2f", params.relationCenter] norm:params.relationCenter y:y + 76 attrs:small small:small];
        [self drawSlider:@"GLD" value:[NSString stringWithFormat:@"%.0f", params.relationGlideMs] norm:(params.relationGlideMs - 10.0f) / 1990.0f y:y + 100 attrs:small small:small];
        [self drawRelationshipPreview:NSMakeRect(606, y + 126, 286, 122) attrs:small];
    }
    if (_openMenu == 1) {
        NSString* items[] = { @"ORDER", @"INTER", @"RND", @"MORPH" };
        s3g::clap_gui::Style style;
        const int selected = static_cast<int>(p->targets.rule.load(std::memory_order_acquire));
        s3g::clap_gui::drawDropdownMenu(NSMakeRect(_menuOrigin.x, _menuOrigin.y, 150, 18.0 * 4.0), 18.0, items, 4, selected, -1, small, style);
    }
}
- (void)updateSlider:(NSPoint)pt
{
    auto* p = static_cast<Plugin*>(_plugin);
    const double n = std::clamp((pt.x - kSliderTrackX) / kSliderTrackW, 0.0, 1.0);
    switch (_dragSlider) {
    case 1: setParam(*p, kRateParamId, 0.125 + n * (4.0 - 0.125)); break;
    case 2: setParam(*p, kXfadeParamId, n * 0.3); break;
    case 3: setParam(*p, kDuckParamId, n * 0.75); break;
    case 4: setParam(*p, kGainParamId, -60.0 + n * 66.0); break;
    case 5: setParam(*p, kSpreadParamId, -1.0 + n * 2.0); break;
    case 6: setParam(*p, kDriftParamId, -0.12 + n * 0.24); break;
    case 7: setParam(*p, kCenterParamId, n); break;
    case 8: setParam(*p, kGlideParamId, 10.0 + n * 1990.0); break;
    case 9: setParam(*p, kLoopStartParamId, n * 0.999); break;
    case 10: setParam(*p, kLoopLengthParamId, 0.01 + n * 0.99); break;
    case 11: setParam(*p, kSourceRateParamId, n); break;
    case 12: setParam(*p, kSourceBlendParamId, n); break;
    default: break;
    }
    [self setNeedsDisplay:YES];
}
- (void)mouseDown:(NSEvent*)event
{
    NSPoint pt = [self convertPoint:[event locationInWindow] fromView:nil];
    auto* p = static_cast<Plugin*>(_plugin);
    if (_openMenu == 1) {
        const int hit = s3g::clap_gui::dropdownHitIndex(pt, NSMakeRect(_menuOrigin.x, _menuOrigin.y, 150, 18.0 * 4.0), 18.0, 4);
        if (hit >= 0) {
            setParam(*p, kRuleParamId, hit);
#if defined(__APPLE__)
            rebuildCompositeSample(*p, false);
#endif
        }
        _openMenu = 0;
        _menuItemCount = 0;
        [self setNeedsDisplay:YES];
        return;
    }
    _openMenu = 0;
    _menuItemCount = 0;
    if (NSPointInRect(pt, NSMakeRect(516,46,18,15))) {
        _waveZoom = std::max<CGFloat>(1.0, _waveZoom * 0.5);
        if (_waveZoom <= 1.0001) {
            _waveScroll = 0.0;
        }
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(pt, NSMakeRect(540,46,18,15))) {
        _waveZoom = std::min<CGFloat>(16.0, _waveZoom * 2.0);
        _waveScroll = std::clamp<CGFloat>(_waveScroll, 0.0, 1.0);
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(pt, NSMakeRect(28,82,72,24))) {
        chooseSampleFromFinder(*p);
        _waveZoom = 1.0;
        _waveScroll = 0.0;
        [self setNeedsDisplay:YES];
        return;
    }
    const NSRect waveRect = NSMakeRect(28,362,540,232);
    const CGFloat zoom = std::clamp<CGFloat>(_waveZoom, 1.0, 16.0);
    if (zoom > 1.0001 && NSPointInRect(pt, NSMakeRect(waveRect.origin.x + 4.0, NSMaxY(waveRect) + 1.0, waveRect.size.width - 8.0, 14.0))) {
        const CGFloat scrollX = waveRect.origin.x + 4.0;
        const CGFloat scrollW = waveRect.size.width - 8.0;
        const CGFloat viewSpan = 1.0 / zoom;
        const CGFloat thumbW = std::max<CGFloat>(24.0, scrollW * viewSpan);
        _waveScroll = std::clamp<CGFloat>((pt.x - scrollX - thumbW * 0.5) / std::max<CGFloat>(1.0, scrollW - thumbW), 0.0, 1.0);
        _dragSlider = -2;
        [self setNeedsDisplay:YES];
        return;
    }
    for (uint32_t lane = 0; lane < s3g::kLoopProcessorChannels; ++lane) {
        NSRect laneRect = NSMakeRect(336.0 + static_cast<CGFloat>(lane) * 30.0, 85.0, 20.0, 18.0);
        if (NSPointInRect(pt, laneRect)) {
            uint32_t laneMask = p->targets.laneMask.load(std::memory_order_acquire);
            laneMask ^= (1u << lane);
            if ((laneMask & 0xffu) == 0u) {
                laneMask = (1u << lane);
            }
            setParam(*p, kLaneMaskParamId, laneMask);
            [self setNeedsDisplay:YES];
            return;
        }
    }
    if (NSPointInRect(pt, NSMakeRect(116,83,26,22))) {
        p->resetAfterStopFrames.store(0u, std::memory_order_release);
        p->playing.store(true, std::memory_order_release);
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(pt, NSMakeRect(150,83,26,22))) {
        const bool next = !p->playing.load(std::memory_order_acquire);
        if (next) {
            p->resetAfterStopFrames.store(0u, std::memory_order_release);
        }
        p->playing.store(next, std::memory_order_release);
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(pt, NSMakeRect(184,83,26,22))) {
        p->playing.store(false, std::memory_order_release);
        const uint32_t resetDelay = static_cast<uint32_t>(std::max(1.0, p->sampleRate * 0.140));
        p->resetAfterStopFrames.store(resetDelay, std::memory_order_release);
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(pt, NSMakeRect(226,83,54,22))) {
        p->engine.resync();
        for (uint32_t lane = 0; lane < s3g::kLoopProcessorChannels; ++lane) {
            p->lanePhases[lane].store(0.0f, std::memory_order_relaxed);
        }
        [self setNeedsDisplay:YES];
        return;
    }
    const CGFloat headerH = 20.0;
    const CGFloat gap = 10.0;
    CGFloat engineY = 42.0;
    CGFloat engineH = _engineOpen ? 246.0 : headerH;
    CGFloat relY = engineY + engineH + gap;
    CGFloat relH = _relationshipsOpen ? 260.0 : headerH;
    if (NSPointInRect(pt, NSMakeRect(kToolboxX, engineY, kToolboxW, 20))) {
        _engineOpen = !_engineOpen;
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(pt, NSMakeRect(kToolboxX, relY, kToolboxW, 20))) {
        _relationshipsOpen = !_relationshipsOpen;
        [self setNeedsDisplay:YES];
        return;
    }
    if (_engineOpen && NSPointInRect(pt, NSMakeRect(kSliderLabelX - 4.0, engineY + 28 - 8, 296, 24))) {
        _openMenu = 1;
        _menuItemCount = 4;
        _menuOrigin = NSMakePoint(kSliderTrackX, engineY + 45);
        [self setNeedsDisplay:YES];
        return;
    }
    struct RowHit { CGFloat y; int slider; BOOL open; };
    const RowHit rows[] = {
        { engineY + 52, 11, _engineOpen },
        { engineY + 76, 12, _engineOpen },
        { engineY + 100, 1, _engineOpen },
        { engineY + 124, 2, _engineOpen },
        { engineY + 148, 3, _engineOpen },
        { engineY + 172, 9, _engineOpen },
        { engineY + 196, 10, _engineOpen },
        { engineY + 220, 4, _engineOpen },
        { relY + 28, 5, _relationshipsOpen },
        { relY + 52, 6, _relationshipsOpen },
        { relY + 76, 7, _relationshipsOpen },
        { relY + 100, 8, _relationshipsOpen },
    };
    for (const auto& row : rows) {
        if (row.open && NSPointInRect(pt, NSMakeRect(kSliderLabelX - 4.0, row.y - 8, 296, 24))) {
            _dragSlider = row.slider;
            [self updateSlider:pt];
            return;
        }
    }
}
- (void)mouseDragged:(NSEvent*)event
{
    NSPoint pt = [self convertPoint:[event locationInWindow] fromView:nil];
    if (_dragSlider > 0) {
        [self updateSlider:pt];
    } else if (_dragSlider == -2) {
        const NSRect waveRect = NSMakeRect(28,362,540,232);
        const CGFloat zoom = std::clamp<CGFloat>(_waveZoom, 1.0, 16.0);
        const CGFloat scrollX = waveRect.origin.x + 4.0;
        const CGFloat scrollW = waveRect.size.width - 8.0;
        const CGFloat viewSpan = 1.0 / zoom;
        const CGFloat thumbW = std::max<CGFloat>(24.0, scrollW * viewSpan);
        _waveScroll = std::clamp<CGFloat>((pt.x - scrollX - thumbW * 0.5) / std::max<CGFloat>(1.0, scrollW - thumbW), 0.0, 1.0);
        [self setNeedsDisplay:YES];
    }
}
- (void)mouseUp:(NSEvent*)event
{
    (void)event;
    auto* p = static_cast<Plugin*>(_plugin);
    if (_dragSlider == 11 || _dragSlider == 12) {
        rebuildCompositeSample(*p, false);
    }
    _dragSlider = -1;
}
@end

namespace {

bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool isFloating) { return !isFloating && std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0; }
bool guiGetPreferredApi(const clap_plugin_t*, const char** api, bool* isFloating) { if (!api || !isFloating) return false; *api = CLAP_WINDOW_API_COCOA; *isFloating = false; return true; }
bool guiCreate(const clap_plugin_t* plugin, const char* api, bool isFloating) { if (!guiIsApiSupported(plugin, api, isFloating)) return false; auto* p = self(plugin); if (p->guiView) return true; p->guiView = [[S3GMultiLoopProcessorView alloc] initWithPlugin:p]; return p->guiView != nullptr; }
void guiDestroy(const clap_plugin_t* plugin) { auto* p = self(plugin); if (p->guiView) { p->guiVisible.store(false); auto* v = static_cast<S3GMultiLoopProcessorView*>(p->guiView); [v stopRefreshTimer]; [v removeFromSuperview]; [v release]; p->guiView = nullptr; } }
bool guiSetScale(const clap_plugin_t*, double) { return true; }
bool guiGetSize(const clap_plugin_t*, uint32_t* w, uint32_t* h) { if (!w || !h) return false; *w = static_cast<uint32_t>(kGuiW); *h = static_cast<uint32_t>(kGuiH); return true; }
bool guiCanResize(const clap_plugin_t*) { return false; }
bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t*) { return false; }
bool guiAdjustSize(const clap_plugin_t*, uint32_t*, uint32_t*) { return false; }
bool guiSetSize(const clap_plugin_t* plugin, uint32_t w, uint32_t h) { auto* p = self(plugin); if (!p->guiView) return false; [static_cast<NSView*>(p->guiView) setFrameSize:NSMakeSize(w,h)]; return true; }
bool guiSetParent(const clap_plugin_t* plugin, const clap_window_t* win) { if (!win || std::strcmp(win->api, CLAP_WINDOW_API_COCOA) != 0 || !win->cocoa) return false; auto* p = self(plugin); if (!p->guiView) return false; NSView* parent = static_cast<NSView*>(win->cocoa); NSView* v = static_cast<NSView*>(p->guiView); [parent addSubview:v]; [v setFrame:NSMakeRect(0,0,kGuiW,kGuiH)]; return true; }
bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}
bool guiShow(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView) return false; p->guiVisible.store(true); [static_cast<NSView*>(p->guiView) setHidden:NO]; [static_cast<S3GMultiLoopProcessorView*>(p->guiView) startRefreshTimer]; return true; }
bool guiHide(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView) return false; p->guiVisible.store(false); [static_cast<S3GMultiLoopProcessorView*>(p->guiView) stopRefreshTimer]; [static_cast<NSView*>(p->guiView) setHidden:YES]; return true; }
const clap_plugin_gui_t guiExt { guiIsApiSupported, guiGetPreferredApi, guiCreate, guiDestroy, guiSetScale, guiGetSize, guiCanResize, guiGetResizeHints, guiAdjustSize, guiSetSize, guiSetParent, guiSetTransient, guiSuggestTitle, guiShow, guiHide };
#endif

const void* pluginGetExtension(const clap_plugin_t*, const char* id)
{
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &audioPorts;
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) return &paramsExt;
    if (std::strcmp(id, CLAP_EXT_STATE) == 0) return &stateExt;
#if defined(__APPLE__)
    if (std::strcmp(id, CLAP_EXT_GUI) == 0) return &guiExt;
#endif
    return nullptr;
}

const char* const features[] { CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_SAMPLER, CLAP_PLUGIN_FEATURE_SURROUND, nullptr };
const clap_plugin_descriptor_t descriptor { CLAP_VERSION_INIT, "org.s3g.s3g-dsp.multi-loop-processor-8ch", "s3g Multi Loop Processor 8ch", "s3g", "https://github.com/s3g/s3g-dsp", "", "", "0.1.0", "8-channel multi-file sample-loop processor with lane assignment rules.", features };

const clap_plugin_t* createPlugin(const clap_plugin_factory*, const clap_host_t* host, const char* pluginId)
{
    if (std::strcmp(pluginId, descriptor.id) != 0) return nullptr;
    auto* p = new (std::nothrow) Plugin();
    if (!p) return nullptr;
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
const clap_plugin_descriptor_t* factoryGetPluginDescriptor(const clap_plugin_factory*, uint32_t index) { return index == 0 ? &descriptor : nullptr; }
const clap_plugin_factory_t factory { factoryGetPluginCount, factoryGetPluginDescriptor, createPlugin };
bool entryInit(const char*) { return true; }
void entryDeinit() {}
const void* entryGetFactory(const char* factoryId) { return std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0 ? &factory : nullptr; }

} // namespace

extern "C" const clap_plugin_entry_t clap_entry { CLAP_VERSION_INIT, entryInit, entryDeinit, entryGetFactory };
