#include "s3g_loop_processor.h"
#include "s3g_multi_loop_processor.h"
#include "s3g_cocoa_gui.h"

#import <AVFoundation/AVFoundation.h>
#import <AudioUnit/AudioUnit.h>
#import <Cocoa/Cocoa.h>
#import <CoreAudio/CoreAudio.h>
#import <CoreMIDI/CoreMIDI.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kChannels = s3g::kLoopProcessorChannels;
constexpr uint32_t kMaxSources = s3g::kMultiLoopMaxSources;
constexpr uint32_t kMaxRenderFrames = 4096;
constexpr CGFloat kGuiW = 920.0;
constexpr CGFloat kGuiH = 640.0;
constexpr CGFloat kPanelX = 596.0;
constexpr CGFloat kPanelW = 306.0;
constexpr CGFloat kLabelX = 606.0;
constexpr CGFloat kTrackX = 704.0;
constexpr CGFloat kValueX = 858.0;
constexpr CGFloat kTrackW = 150.0;

struct OutputDeviceInfo {
    AudioDeviceID id = kAudioObjectUnknown;
    std::string name;
    uint32_t channels = 0u;
};

struct AppState {
    AVAudioEngine* audioEngine = nil;
    AVAudioSourceNode* sourceNode = nil;
    MIDIClientRef midiClient = 0;
    MIDIPortRef midiPort = 0;
    std::vector<OutputDeviceInfo> outputDevices;
    std::atomic<uint32_t> selectedDeviceIndex { 0u };
    std::atomic<uint32_t> outputChannelCount { 0u };

    s3g::LoopProcessorEngine engine;
    std::shared_ptr<const s3g::LoopProcessorSample> sample;
    std::array<std::shared_ptr<const s3g::LoopProcessorSample>, kMaxSources> sources {};
    std::array<std::string, kMaxSources> samplePaths {};
    std::atomic<uint32_t> sourceCount { 0u };

    std::atomic<float> baseRate { 1.0f };
    std::atomic<float> rateSpread { 0.08f };
    std::atomic<float> driftAmount { 0.0f };
    std::atomic<float> center { 0.5f };
    std::atomic<float> glideMs { 250.0f };
    std::atomic<float> loopStart { 0.0f };
    std::atomic<float> loopLength { 1.0f };
    std::atomic<float> xfadePct { 0.12f };
    std::atomic<float> seamDuck { 0.18f };
    std::atomic<float> gainDb { -12.0f };
    std::atomic<uint32_t> rule { 1u };
    std::atomic<float> sourceRateSpread { 0.0f };
    std::atomic<float> sourceBlend { 1.0f };

    std::atomic<bool> playing { false };
    std::atomic<bool> resyncRequested { false };
    std::atomic<bool> rebuildRequested { false };
    std::atomic<int32_t> lastCc { -1 };
    std::atomic<int32_t> lastCcValue { 0 };
    std::atomic<float> outputPeak { 0.0f };
    std::array<std::atomic<float>, kChannels> lanePhases {};

    std::array<std::array<float, kMaxRenderFrames>, kChannels> scratch {};
    std::mutex rebuildMutex;
};

static NSColor* c(int rgb, double alpha = 1.0) { return s3g::clap_gui::color(rgb, alpha); }

static CGFloat wrapUnitCGFloat(CGFloat value)
{
    value -= std::floor(value);
    return value < 0.0 ? value + 1.0 : value;
}

uint32_t energeticChannelForDisplay(const s3g::LoopProcessorSample& sample)
{
    if (sample.frames < 2u || sample.channels == 0u || sample.audio.empty()) return 0u;
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

s3g::LoopProcessorParams snapshotParams(const AppState& state)
{
    s3g::LoopProcessorParams params {};
    params.baseRate = state.baseRate.load(std::memory_order_acquire);
    params.rateSpread = state.rateSpread.load(std::memory_order_acquire);
    params.driftAmount = state.driftAmount.load(std::memory_order_acquire);
    params.relationCenter = state.center.load(std::memory_order_acquire);
    params.relationGlideMs = state.glideMs.load(std::memory_order_acquire);
    params.loopStart = state.loopStart.load(std::memory_order_acquire);
    params.loopLength = state.loopLength.load(std::memory_order_acquire);
    params.xfadePct = state.xfadePct.load(std::memory_order_acquire);
    params.seamDuck = state.seamDuck.load(std::memory_order_acquire);
    params.gainDb = state.gainDb.load(std::memory_order_acquire);
    params.laneMask = 0xffu;
    params.baseRate = std::clamp(params.baseRate, 0.125f, 4.0f);
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

uint32_t outputChannelCountForDevice(AudioDeviceID device)
{
    AudioObjectPropertyAddress address {
        kAudioDevicePropertyStreamConfiguration,
        kAudioDevicePropertyScopeOutput,
        kAudioObjectPropertyElementMain
    };
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(device, &address, 0, nullptr, &size) != noErr || size == 0) return 0u;
    std::vector<uint8_t> storage(size);
    auto* list = reinterpret_cast<AudioBufferList*>(storage.data());
    if (AudioObjectGetPropertyData(device, &address, 0, nullptr, &size, list) != noErr) return 0u;
    uint32_t channels = 0u;
    for (UInt32 i = 0; i < list->mNumberBuffers; ++i) channels += list->mBuffers[i].mNumberChannels;
    return channels;
}

std::string nameForDevice(AudioDeviceID device)
{
    AudioObjectPropertyAddress address {
        kAudioObjectPropertyName,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };
    CFStringRef cfName = nullptr;
    UInt32 size = sizeof(cfName);
    if (AudioObjectGetPropertyData(device, &address, 0, nullptr, &size, &cfName) != noErr || !cfName) {
        return "Output Device";
    }
    char name[256] {};
    CFStringGetCString(cfName, name, sizeof(name), kCFStringEncodingUTF8);
    CFRelease(cfName);
    return name[0] ? std::string(name) : std::string("Output Device");
}

AudioDeviceID currentDefaultOutputDevice()
{
    AudioDeviceID device = kAudioObjectUnknown;
    UInt32 size = sizeof(device);
    AudioObjectPropertyAddress address {
        kAudioHardwarePropertyDefaultOutputDevice,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };
    AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, 0, nullptr, &size, &device);
    return device;
}

std::vector<OutputDeviceInfo> enumerateOutputDevices()
{
    AudioObjectPropertyAddress address {
        kAudioHardwarePropertyDevices,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };
    UInt32 size = 0;
    std::vector<OutputDeviceInfo> devices;
    if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &address, 0, nullptr, &size) != noErr || size == 0) {
        return devices;
    }
    const uint32_t count = size / sizeof(AudioDeviceID);
    std::vector<AudioDeviceID> ids(count);
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, 0, nullptr, &size, ids.data()) != noErr) {
        return devices;
    }
    for (AudioDeviceID id : ids) {
        const uint32_t channels = outputChannelCountForDevice(id);
        if (channels == 0u) continue;
        devices.push_back(OutputDeviceInfo { id, nameForDevice(id), channels });
    }
    std::sort(devices.begin(), devices.end(), [](const OutputDeviceInfo& a, const OutputDeviceInfo& b) {
        if (a.channels != b.channels) return a.channels > b.channels;
        return a.name < b.name;
    });
    return devices;
}

void refreshOutputDevices(AppState& state)
{
    state.outputDevices = enumerateOutputDevices();
    const AudioDeviceID current = currentDefaultOutputDevice();
    uint32_t selected = 0u;
    for (uint32_t i = 0; i < state.outputDevices.size(); ++i) {
        if (state.outputDevices[i].id == current) {
            selected = i;
            break;
        }
    }
    if (!state.outputDevices.empty()) {
        state.selectedDeviceIndex.store(selected, std::memory_order_release);
        state.outputChannelCount.store(state.outputDevices[selected].channels, std::memory_order_release);
    }
}

bool configureAudioGraph(AppState& state)
{
    if (!state.audioEngine || !state.sourceNode) return false;
    NSError* error = nil;
    const bool wasRunning = [state.audioEngine isRunning];
    if (wasRunning) [state.audioEngine stop];
    [state.audioEngine disconnectNodeOutput:state.sourceNode];
    AVAudioFormat* format = [[state.audioEngine outputNode] outputFormatForBus:0];
    [state.audioEngine connect:state.sourceNode to:[state.audioEngine mainMixerNode] format:format];
    [state.audioEngine prepare];
    const bool ok = [state.audioEngine startAndReturnError:&error];
    if (ok) {
        state.outputChannelCount.store([format channelCount], std::memory_order_release);
        state.engine.prepare([format sampleRate]);
        state.resyncRequested.store(true, std::memory_order_release);
    }
    return ok;
}

bool setSystemDefaultOutputDevice(AudioDeviceID device)
{
    if (device == kAudioObjectUnknown) return false;
    AudioObjectPropertyAddress address {
        kAudioHardwarePropertyDefaultOutputDevice,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };
    AudioDeviceID value = device;
    return AudioObjectSetPropertyData(kAudioObjectSystemObject, &address, 0, nullptr, sizeof(value), &value) == noErr;
}

bool selectOutputDevice(AppState& state, uint32_t index)
{
    if (!state.audioEngine || index >= state.outputDevices.size()) return false;
    if (!setSystemDefaultOutputDevice(state.outputDevices[index].id)) return false;
    refreshOutputDevices(state);
    state.selectedDeviceIndex.store(index, std::memory_order_release);
    return configureAudioGraph(state);
}

std::shared_ptr<s3g::LoopProcessorSample> readSampleFromPath(const std::string& path)
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

void rebuildComposite(AppState& state)
{
    std::lock_guard<std::mutex> lock(state.rebuildMutex);
    std::array<std::shared_ptr<const s3g::LoopProcessorSample>, kMaxSources> active {};
    uint32_t count = 0u;
    for (uint32_t i = 0; i < kMaxSources; ++i) {
        if (state.sources[i] && state.sources[i]->frames >= 2u) active[count++] = state.sources[i];
    }
    state.sourceCount.store(count, std::memory_order_release);
    if (count == 0u) {
        std::shared_ptr<const s3g::LoopProcessorSample> empty;
        std::atomic_store_explicit(&state.sample, empty, std::memory_order_release);
        return;
    }
    s3g::MultiLoopCompositeOptions options {};
    options.rule = static_cast<s3g::MultiLoopSourceRule>(std::min<uint32_t>(state.rule.load(std::memory_order_acquire), 3u));
    options.sourceRateSpread = state.sourceRateSpread.load(std::memory_order_acquire);
    options.sourceBlend = state.sourceBlend.load(std::memory_order_acquire);
    auto composite = s3g::buildMultiLoopComposite(active, count, options);
    std::shared_ptr<const s3g::LoopProcessorSample> immutable = composite;
    std::atomic_store_explicit(&state.sample, immutable, std::memory_order_release);
    state.resyncRequested.store(true, std::memory_order_release);
}

void setCc(AppState& state, uint8_t cc, uint8_t value)
{
    const float n = static_cast<float>(value) / 127.0f;
    const bool pressed = value > 0u;
    state.lastCc.store(cc, std::memory_order_release);
    state.lastCcValue.store(value, std::memory_order_release);
    switch (cc) {
    case 0: state.baseRate.store(0.125f + n * (4.0f - 0.125f), std::memory_order_release); break;
    case 1: state.rateSpread.store(-1.0f + n * 2.0f, std::memory_order_release); break;
    case 2: state.driftAmount.store(-0.12f + n * 0.24f, std::memory_order_release); break;
    case 3: state.center.store(n, std::memory_order_release); break;
    case 4: state.glideMs.store(10.0f + n * 1990.0f, std::memory_order_release); break;
    case 5: state.loopStart.store(n * 0.999f, std::memory_order_release); break;
    case 6: state.loopLength.store(0.01f + n * 0.99f, std::memory_order_release); break;
    case 7: state.gainDb.store(-60.0f + n * 66.0f, std::memory_order_release); break;
    case 16: state.xfadePct.store(n * 0.3f, std::memory_order_release); break;
    case 17: state.seamDuck.store(n * 0.75f, std::memory_order_release); break;
    case 32: if (pressed) { state.rule.store(0u, std::memory_order_release); state.rebuildRequested.store(true, std::memory_order_release); } break;
    case 33: if (pressed) { state.rule.store(1u, std::memory_order_release); state.rebuildRequested.store(true, std::memory_order_release); } break;
    case 34: if (pressed) { state.rule.store(2u, std::memory_order_release); state.rebuildRequested.store(true, std::memory_order_release); } break;
    case 35: if (pressed) { state.rule.store(3u, std::memory_order_release); state.rebuildRequested.store(true, std::memory_order_release); } break;
    case 41: if (pressed) state.playing.store(true, std::memory_order_release); break;
    case 42: if (pressed) { state.playing.store(false, std::memory_order_release); state.resyncRequested.store(true, std::memory_order_release); } break;
    case 43: if (pressed) state.resyncRequested.store(true, std::memory_order_release); break;
    case 44: if (pressed) {
        const uint32_t next = (state.rule.load(std::memory_order_acquire) + 1u) & 3u;
        state.rule.store(next, std::memory_order_release);
        state.rebuildRequested.store(true, std::memory_order_release);
    } break;
    case 45: if (pressed) state.rebuildRequested.store(true, std::memory_order_release); break;
    case 46: if (pressed) {
        const bool nowPlaying = state.playing.load(std::memory_order_acquire);
        state.playing.store(!nowPlaying, std::memory_order_release);
        if (nowPlaying) state.resyncRequested.store(true, std::memory_order_release);
    } break;
    default: break;
    }
}

void handleMidiPacketList(const MIDIPacketList* packetList, void* refCon)
{
    auto* state = static_cast<AppState*>(refCon);
    if (!state || !packetList) return;
    const MIDIPacket* packet = &packetList->packet[0];
    for (uint32_t p = 0; p < packetList->numPackets; ++p) {
        uint16_t i = 0;
        while (i + 2u < packet->length) {
            const uint8_t status = packet->data[i] & 0xf0u;
            const uint8_t d1 = packet->data[i + 1u];
            const uint8_t d2 = packet->data[i + 2u];
            if (status == 0xb0u) {
                setCc(*state, d1, d2);
                i += 3u;
            } else if (status == 0x80u || status == 0x90u) {
                i += 3u;
            } else {
                ++i;
            }
        }
        packet = MIDIPacketNext(packet);
    }
}

void midiReadProc(const MIDIPacketList* packetList, void* refCon, void* connRefCon)
{
    (void)connRefCon;
    handleMidiPacketList(packetList, refCon);
}

} // namespace

@interface S3GStandaloneView : NSView {
    AppState* _state;
    NSTimer* _timer;
    int _dragSlider;
    int _openMenu;
}
- (id)initWithState:(AppState*)state;
- (void)drawWaveform:(const std::shared_ptr<const s3g::LoopProcessorSample>&)sample rect:(NSRect)rect attrs:(NSDictionary*)attrs;
@end

static void drawMiniWaveform(const s3g::LoopProcessorSample* sample, NSRect rect, uint32_t channel)
{
    [c(0x0d0d0d) setFill];
    NSRectFill(rect);
    [c(0x3f3f3f) setStroke];
    NSFrameRect(rect);
    [c(0x252525) setStroke];
    [NSBezierPath strokeLineFromPoint:NSMakePoint(rect.origin.x + 1.0, NSMidY(rect))
                              toPoint:NSMakePoint(NSMaxX(rect) - 1.0, NSMidY(rect))];
    if (!sample || sample->frames < 2u || sample->channels == 0u || sample->audio.empty()) return;
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

@implementation S3GStandaloneView
- (id)initWithState:(AppState*)state
{
    if ((self = [super initWithFrame:NSMakeRect(0, 0, kGuiW, kGuiH)])) {
        _state = state;
        _dragSlider = -1;
        _openMenu = 0;
        _timer = [NSTimer timerWithTimeInterval:1.0 / 20.0 target:self selector:@selector(tick:) userInfo:nil repeats:YES];
        [[NSRunLoop mainRunLoop] addTimer:_timer forMode:NSRunLoopCommonModes];
    }
    return self;
}
- (BOOL)isFlipped { return YES; }
- (void)dealloc { [_timer invalidate]; [super dealloc]; }
- (void)tick:(NSTimer*)timer
{
    (void)timer;
    if (_state->rebuildRequested.exchange(false, std::memory_order_acq_rel)) rebuildComposite(*_state);
    [self setNeedsDisplay:YES];
}
- (void)drawButton:(NSString*)label rect:(NSRect)rect active:(BOOL)active attrs:(NSDictionary*)attrs
{
    [c(active ? 0xd1d1d1 : 0x141414) setFill];
    NSRectFill(rect);
    [c(0xd1d1d1) setStroke];
    NSFrameRect(rect);
    NSMutableDictionary* a = [attrs mutableCopy];
    a[NSForegroundColorAttributeName] = active ? c(0x0c0c0c) : c(0xd1d1d1);
    NSSize size = [label sizeWithAttributes:a];
    [label drawAtPoint:NSMakePoint(NSMidX(rect) - size.width * 0.5, NSMidY(rect) - size.height * 0.5 - 1.0) withAttributes:a];
    [a release];
}
- (void)drawSlider:(NSString*)name value:(NSString*)value norm:(CGFloat)norm y:(CGFloat)y attrs:(NSDictionary*)attrs small:(NSDictionary*)small
{
    s3g::clap_gui::Style style;
    s3g::clap_gui::drawSlider(name, value, std::clamp(norm, static_cast<CGFloat>(0), static_cast<CGFloat>(1)), y, attrs, small, style, kLabelX, kTrackX, kValueX, kTrackW);
}
- (void)drawWaveform:(const std::shared_ptr<const s3g::LoopProcessorSample>&)sample rect:(NSRect)rect attrs:(NSDictionary*)attrs
{
    const s3g::LoopProcessorParams params = snapshotParams(*_state);
    [c(0x111111) setFill];
    NSRectFill(rect);
    [c(0x444444) setStroke];
    NSFrameRect(rect);
    const CGFloat midY = NSMidY(rect);
    [c(0x2a2a2a) setStroke];
    [NSBezierPath strokeLineFromPoint:NSMakePoint(rect.origin.x + 1.0, midY)
                              toPoint:NSMakePoint(NSMaxX(rect) - 1.0, midY)];
    if (!sample || sample->frames < 2u || sample->channels == 0u || sample->audio.empty()) {
        [@"NO WAVEFORM" drawAtPoint:NSMakePoint(rect.origin.x + 10.0, rect.origin.y + 10.0) withAttributes:attrs];
        return;
    }

    const uint32_t bestChannel = energeticChannelForDisplay(*sample);
    const CGFloat waveX = rect.origin.x + 4.0;
    const CGFloat waveW = rect.size.width - 8.0;
    const CGFloat loopStartUnit = std::clamp<CGFloat>(params.loopStart, 0.0, 1.0);
    const CGFloat loopEndUnit = std::clamp<CGFloat>(params.loopStart + params.loopLength, 0.0, 1.0);
    const uint32_t columns = static_cast<uint32_t>(std::max<CGFloat>(32.0, std::floor(waveW)));
    const CGFloat usableH = rect.size.height - 18.0;
    const CGFloat scaleY = usableH * 0.48;
    NSBezierPath* wave = [NSBezierPath bezierPath];
    [wave setLineWidth:0.75];
    [NSGraphicsContext saveGraphicsState];
    NSRectClip(NSInsetRect(rect, 1.0, 1.0));
    for (uint32_t x = 0; x < columns; ++x) {
        const double ua = static_cast<double>(x) / static_cast<double>(std::max<uint32_t>(1u, columns));
        const double ub = static_cast<double>(x + 1u) / static_cast<double>(std::max<uint32_t>(1u, columns));
        const uint32_t a = std::min<uint32_t>(sample->frames - 1u, static_cast<uint32_t>(std::floor(ua * sample->frames)));
        const uint32_t b = std::max<uint32_t>(a + 1u, std::min<uint32_t>(sample->frames, static_cast<uint32_t>(std::ceil(ub * sample->frames))));
        const uint32_t stride = std::max<uint32_t>(1u, (b - a) / 32u);
        float lo = 0.0f;
        float hi = 0.0f;
        for (uint32_t i = a; i < b; i += stride) {
            const float value = sample->audio[static_cast<size_t>(i) * sample->channels + bestChannel];
            lo = std::min(lo, value);
            hi = std::max(hi, value);
        }
        const CGFloat px = waveX + static_cast<CGFloat>(x);
        const CGFloat yA = std::clamp<CGFloat>(midY - static_cast<CGFloat>(hi) * scaleY, rect.origin.y + 4.0, NSMaxY(rect) - 14.0);
        const CGFloat yB = std::clamp<CGFloat>(midY - static_cast<CGFloat>(lo) * scaleY, rect.origin.y + 4.0, NSMaxY(rect) - 14.0);
        [wave moveToPoint:NSMakePoint(px, yA)];
        [wave lineToPoint:NSMakePoint(px, yB)];
    }
    [c(0x747474) setStroke];
    [wave stroke];

    const CGFloat markerTop = rect.origin.y + 4.0;
    const CGFloat markerBottom = NSMaxY(rect) - 18.0;
    const CGFloat sx = waveX + loopStartUnit * waveW;
    const CGFloat ex = waveX + loopEndUnit * waveW;
    [c(0x7f9aa0) setStroke];
    [NSBezierPath strokeLineFromPoint:NSMakePoint(sx, markerTop) toPoint:NSMakePoint(sx, markerBottom)];
    [NSBezierPath strokeLineFromPoint:NSMakePoint(ex, markerTop) toPoint:NSMakePoint(ex, markerBottom)];
    const CGFloat loopW = std::max<CGFloat>(1.0, ex - sx);
    for (uint32_t lane = 0; lane < kChannels; ++lane) {
        const CGFloat phase = wrapUnitCGFloat(std::clamp<CGFloat>(_state->lanePhases[lane].load(std::memory_order_relaxed), 0.0, 1.0));
        const CGFloat px = sx + phase * loopW;
        [c(0xf0f0f0) setFill];
        NSRectFill(NSMakeRect(px - 0.5, markerTop, 1.0, markerBottom - markerTop));
        const CGFloat laneStep = (markerBottom - markerTop) / static_cast<CGFloat>(kChannels);
        const CGFloat tickY = markerTop + static_cast<CGFloat>(lane) * laneStep + laneStep * 0.5;
        NSRectFill(NSMakeRect(px - 2.0, tickY - 1.0, 4.0, 2.0));
    }
    [NSGraphicsContext restoreGraphicsState];
    [[NSString stringWithFormat:@"WAVE CH%u  XFD %.0f%%  DUCK %.0f%%", bestChannel + 1u, params.xfadePct * 100.0f, params.seamDuck * 100.0f]
        drawAtPoint:NSMakePoint(rect.origin.x + 10.0, NSMaxY(rect) - 16.0)
      withAttributes:attrs];
}
- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    s3g::clap_gui::Style style;
    [style.bg setFill];
    NSRectFill(self.bounds);
    NSDictionary* small = @{ NSFontAttributeName:[NSFont fontWithName:@"Menlo" size:10] ?: [NSFont monospacedSystemFontOfSize:10 weight:NSFontWeightRegular], NSForegroundColorAttributeName:style.text };
    NSDictionary* title = @{ NSFontAttributeName:[NSFont fontWithName:@"Menlo" size:11] ?: [NSFont monospacedSystemFontOfSize:11 weight:NSFontWeightRegular], NSForegroundColorAttributeName:style.text };

    [@"s3g Multi Loop Processor" drawAtPoint:NSMakePoint(18, 16) withAttributes:title];
    [@"standalone prototype / AVAudioEngine / CoreMIDI" drawAtPoint:NSMakePoint(596, 16) withAttributes:small];
    NSRect samplePanel = NSMakeRect(18, 48, 560, 566);
    [c(0x1d1d1d) setFill];
    NSRectFill(samplePanel);
    [c(0x626262) setStroke];
    NSFrameRect(samplePanel);
    [c(0x141414) setFill];
    NSRectFill(NSMakeRect(samplePanel.origin.x, samplePanel.origin.y, samplePanel.size.width, 21.0));
    [c(0xd1d1d1) setFill];
    NSRectFill(NSMakeRect(samplePanel.origin.x, samplePanel.origin.y, samplePanel.size.width, 2.0));
    [@"SAMPLE" drawAtPoint:NSMakePoint(28, 53) withAttributes:small];

    [self drawButton:@"LOAD" rect:NSMakeRect(28, 88, 72, 24) active:NO attrs:small];
    [self drawButton:@"PLAY" rect:NSMakeRect(116, 89, 54, 22) active:_state->playing.load() attrs:small];
    [self drawButton:@"STOP" rect:NSMakeRect(178, 89, 54, 22) active:NO attrs:small];
    [self drawButton:@"SYNC" rect:NSMakeRect(244, 89, 54, 22) active:NO attrs:small];

    NSString* deviceText = @"SYSTEM OUTPUT";
    const uint32_t selectedDevice = _state->selectedDeviceIndex.load(std::memory_order_acquire);
    if (selectedDevice < _state->outputDevices.size()) {
        const auto& device = _state->outputDevices[selectedDevice];
        deviceText = [NSString stringWithFormat:@"%s / %uCH", device.name.c_str(), device.channels];
    }
    [[NSString stringWithFormat:@"SYS %@", deviceText] drawInRect:NSMakeRect(316, 92, 242, 18) withAttributes:small];

    auto sample = std::atomic_load_explicit(&_state->sample, std::memory_order_acquire);
    NSString* sourceText = sample ? [NSString stringWithFormat:@"%u files -> %u frames / %u lanes", _state->sourceCount.load(), sample->frames, sample->channels] : @"no sources loaded";
    [sourceText drawAtPoint:NSMakePoint(28, 130) withAttributes:small];
    const int32_t lastCc = _state->lastCc.load(std::memory_order_acquire);
    NSString* ccText = lastCc >= 0
        ? [NSString stringWithFormat:@"PK %.3f   CC %d %d", _state->outputPeak.load(), lastCc, _state->lastCcValue.load(std::memory_order_acquire)]
        : [NSString stringWithFormat:@"PK %.3f   CC --", _state->outputPeak.load()];
    [ccText drawAtPoint:NSMakePoint(28, 150) withAttributes:small];
    if (sample) {
        [[NSString stringWithFormat:@"%s / %.0f Hz / OUT %uCH",
            ruleName(_state->rule.load(std::memory_order_acquire)),
            sample->sampleRate,
            _state->outputChannelCount.load(std::memory_order_acquire)]
            drawAtPoint:NSMakePoint(28, 168)
            withAttributes:small];
    }

    NSRect sourceBox = NSMakeRect(28, 190, 540, 164);
    [c(0x111111) setFill];
    NSRectFill(sourceBox);
    [c(0x444444) setStroke];
    NSFrameRect(sourceBox);
    [@"SOURCES" drawAtPoint:NSMakePoint(sourceBox.origin.x + 10.0, sourceBox.origin.y + 7.0) withAttributes:small];
    [@"ENERGETIC CH DISPLAY / LANE MAP" drawAtPoint:NSMakePoint(sourceBox.origin.x + 316.0, sourceBox.origin.y + 7.0) withAttributes:small];
    for (uint32_t slot = 0; slot < kMaxSources; ++slot) {
        const CGFloat rowY = sourceBox.origin.y + 26.0 + static_cast<CGFloat>(slot) * 33.0;
        const bool active = _state->sources[slot] != nullptr;
        NSDictionary* rowAttrs = active ? title : small;
        NSString* nameText = nil;
        NSString* infoText = nil;
        uint32_t displayChannel = 0u;
        if (active) {
            displayChannel = energeticChannelForDisplay(*_state->sources[slot]);
            NSString* path = [NSString stringWithUTF8String:_state->samplePaths[slot].c_str()];
            NSString* base = [path lastPathComponent];
            nameText = [NSString stringWithFormat:@"%u  %@", slot + 1u, base];
            infoText = [NSString stringWithFormat:@"%uCH %.0f  SHOW %u", _state->sources[slot]->channels, _state->sources[slot]->sampleRate, displayChannel + 1u];
        } else {
            nameText = [NSString stringWithFormat:@"%u  --", slot + 1u];
            infoText = @"";
        }
        [nameText drawInRect:NSMakeRect(sourceBox.origin.x + 12.0, rowY, 196.0, 13.0) withAttributes:rowAttrs];
        [infoText drawInRect:NSMakeRect(sourceBox.origin.x + 12.0, rowY + 14.0, 196.0, 13.0) withAttributes:small];
        drawMiniWaveform(active ? _state->sources[slot].get() : nullptr, NSMakeRect(sourceBox.origin.x + 220.0, rowY - 1.0, 306.0, 27.0), displayChannel);
    }
    [self drawWaveform:sample rect:NSMakeRect(28, 368, 540, 226) attrs:small];

    s3g::clap_gui::drawPanelFrame(kPanelX, 48, kPanelW, 386, style);
    s3g::clap_gui::drawPanelHeader(@"ENGINE", true, kPanelX, 48, kPanelW, 20, small, style);
    s3g::clap_gui::drawMenu(@"RULE", [NSString stringWithUTF8String:ruleName(_state->rule.load())], 82, small, small, style, kLabelX, kTrackX);
    [self drawSlider:@"RATE" value:[NSString stringWithFormat:@"%.3f", _state->baseRate.load()] norm:(_state->baseRate.load() - 0.125f) / (4.0f - 0.125f) y:112 attrs:small small:small];
    [self drawSlider:@"SPRD" value:[NSString stringWithFormat:@"%+.0f%%", _state->rateSpread.load() * 100.0f] norm:(_state->rateSpread.load() + 1.0f) * 0.5f y:142 attrs:small small:small];
    [self drawSlider:@"DRFT" value:[NSString stringWithFormat:@"%+.3f", _state->driftAmount.load()] norm:(_state->driftAmount.load() + 0.12f) / 0.24f y:172 attrs:small small:small];
    [self drawSlider:@"CTR" value:[NSString stringWithFormat:@"%.2f", _state->center.load()] norm:_state->center.load() y:202 attrs:small small:small];
    [self drawSlider:@"GLD" value:[NSString stringWithFormat:@"%.0f", _state->glideMs.load()] norm:(_state->glideMs.load() - 10.0f) / 1990.0f y:232 attrs:small small:small];
    [self drawSlider:@"STRT" value:[NSString stringWithFormat:@"%.0f%%", _state->loopStart.load() * 100.0f] norm:_state->loopStart.load() / 0.999f y:262 attrs:small small:small];
    [self drawSlider:@"LEN" value:[NSString stringWithFormat:@"%.0f%%", _state->loopLength.load() * 100.0f] norm:(_state->loopLength.load() - 0.01f) / 0.99f y:292 attrs:small small:small];
    [self drawSlider:@"XFD" value:[NSString stringWithFormat:@"%.0f%%", _state->xfadePct.load() * 100.0f] norm:_state->xfadePct.load() / 0.3f y:322 attrs:small small:small];
    [self drawSlider:@"DUCK" value:[NSString stringWithFormat:@"%.0f%%", _state->seamDuck.load() * 100.0f] norm:_state->seamDuck.load() / 0.75f y:352 attrs:small small:small];
    [self drawSlider:@"OUT" value:[NSString stringWithFormat:@"%+.1f", _state->gainDb.load()] norm:(_state->gainDb.load() + 60.0f) / 66.0f y:382 attrs:small small:small];

    s3g::clap_gui::drawPanelFrame(kPanelX, 448, kPanelW, 146, style);
    s3g::clap_gui::drawPanelHeader(@"nanoKONTROL2", true, kPanelX, 448, kPanelW, 20, small, style);
    NSArray* rows = @[
        @"SLIDERS 0-7: ENGINE",
        @"KNOBS 16-17: XFD DUCK",
        @"SOLO 32-35: RULES",
        @"TRANSPORT 41-46: PLAY/SYNC"
    ];
    for (NSUInteger i = 0; i < [rows count]; ++i) {
        [rows[i] drawAtPoint:NSMakePoint(kLabelX, 482 + static_cast<CGFloat>(i) * 24.0) withAttributes:small];
    }

    if (_openMenu == 1) {
        NSString* items[] = { @"ORDER", @"INTER", @"RND", @"MORPH" };
        s3g::clap_gui::drawDropdownMenu(NSMakeRect(kTrackX, 99, 178, 72), 18.0, items, 4, static_cast<int>(_state->rule.load()), -1, small, style);
    }
}
- (void)setSlider:(int)slider norm:(double)n
{
    n = std::clamp(n, 0.0, 1.0);
    switch (slider) {
    case 1: _state->baseRate.store(0.125f + static_cast<float>(n) * (4.0f - 0.125f)); break;
    case 2: _state->rateSpread.store(-1.0f + static_cast<float>(n) * 2.0f); break;
    case 3: _state->driftAmount.store(-0.12f + static_cast<float>(n) * 0.24f); break;
    case 4: _state->center.store(static_cast<float>(n)); break;
    case 5: _state->glideMs.store(10.0f + static_cast<float>(n) * 1990.0f); break;
    case 6: _state->loopStart.store(static_cast<float>(n) * 0.999f); break;
    case 7: _state->loopLength.store(0.01f + static_cast<float>(n) * 0.99f); break;
    case 8: _state->xfadePct.store(static_cast<float>(n) * 0.3f); break;
    case 9: _state->seamDuck.store(static_cast<float>(n) * 0.75f); break;
    case 10: _state->gainDb.store(-60.0f + static_cast<float>(n) * 66.0f); break;
    default: break;
    }
    [self setNeedsDisplay:YES];
}
- (void)mouseDown:(NSEvent*)event
{
    NSPoint pt = [self convertPoint:event.locationInWindow fromView:nil];
    if (_openMenu == 1) {
        const int hit = s3g::clap_gui::dropdownHitIndex(pt, NSMakeRect(kTrackX, 99, 178, 72), 18, 4);
        if (hit >= 0) {
            _state->rule.store(static_cast<uint32_t>(hit));
            rebuildComposite(*_state);
        }
        _openMenu = 0;
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(pt, NSMakeRect(28, 88, 72, 24))) {
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        [panel setAllowsMultipleSelection:YES];
        [panel setCanChooseDirectories:NO];
        [panel setAllowedFileTypes:@[@"wav", @"aif", @"aiff", @"caf", @"mp3", @"m4a"]];
        if ([panel runModal] == NSModalResponseOK) {
            _state->sources = {};
            _state->samplePaths = {};
            NSArray<NSURL*>* urls = [panel URLs];
            const NSUInteger count = std::min<NSUInteger>([urls count], kMaxSources);
            for (NSUInteger i = 0; i < count; ++i) {
                char path[4096] {};
                if ([[urls[i] path] getFileSystemRepresentation:path maxLength:sizeof(path)]) {
                    auto sample = readSampleFromPath(path);
                    if (sample) {
                        _state->sources[i] = sample;
                        _state->samplePaths[i] = path;
                    }
                }
            }
            _state->loopStart.store(0.0f, std::memory_order_release);
            _state->loopLength.store(1.0f, std::memory_order_release);
            _state->xfadePct.store(0.12f, std::memory_order_release);
            _state->seamDuck.store(0.18f, std::memory_order_release);
            rebuildComposite(*_state);
        }
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(pt, NSMakeRect(116, 89, 54, 22))) { _state->playing.store(true); [self setNeedsDisplay:YES]; return; }
    if (NSPointInRect(pt, NSMakeRect(178, 89, 54, 22))) { _state->playing.store(false); _state->resyncRequested.store(true); [self setNeedsDisplay:YES]; return; }
    if (NSPointInRect(pt, NSMakeRect(244, 89, 54, 22))) { _state->resyncRequested.store(true); [self setNeedsDisplay:YES]; return; }
    if (NSPointInRect(pt, NSMakeRect(kTrackX, 81, 178, 18))) { _openMenu = 1; [self setNeedsDisplay:YES]; return; }
    const CGFloat ys[] = {112, 142, 172, 202, 232, 262, 292, 322, 352, 382};
    for (int i = 0; i < 10; ++i) {
        if (pt.y >= ys[i] - 7 && pt.y <= ys[i] + 19 && pt.x >= kTrackX - 8 && pt.x <= kTrackX + kTrackW + 8) {
            _dragSlider = i + 1;
            [self setSlider:_dragSlider norm:(pt.x - kTrackX) / kTrackW];
            return;
        }
    }
}
- (void)mouseDragged:(NSEvent*)event
{
    if (_dragSlider > 0) {
        NSPoint pt = [self convertPoint:event.locationInWindow fromView:nil];
        [self setSlider:_dragSlider norm:(pt.x - kTrackX) / kTrackW];
    }
}
- (void)mouseUp:(NSEvent*)event { (void)event; _dragSlider = -1; }
@end

@interface S3GAppDelegate : NSObject <NSApplicationDelegate> {
    AppState* _state;
    NSWindow* _window;
}
@end

@implementation S3GAppDelegate
- (void)installApplicationMenu
{
    NSMenu* menubar = [[[NSMenu alloc] initWithTitle:@""] autorelease];
    NSMenuItem* appMenuItem = [[[NSMenuItem alloc] initWithTitle:@"" action:nil keyEquivalent:@""] autorelease];
    [menubar addItem:appMenuItem];
    [NSApp setMainMenu:menubar];

    NSMenu* appMenu = [[[NSMenu alloc] initWithTitle:@"s3g Multi Loop Processor"] autorelease];
    NSString* quitTitle = @"Quit s3g Multi Loop Processor";
    NSMenuItem* quit = [[[NSMenuItem alloc] initWithTitle:quitTitle
                                                   action:@selector(terminate:)
                                            keyEquivalent:@"q"] autorelease];
    [appMenu addItem:quit];
    [appMenuItem setSubmenu:appMenu];
}
- (void)applicationDidFinishLaunching:(NSNotification*)notification
{
    (void)notification;
    [self installApplicationMenu];
    _state = new AppState();
    _state->engine.prepare(48000.0);
    _state->audioEngine = [[AVAudioEngine alloc] init];
    refreshOutputDevices(*_state);
    AVAudioFormat* outputFormat = [[_state->audioEngine outputNode] outputFormatForBus:0];
    const double sampleRate = outputFormat ? [outputFormat sampleRate] : 48000.0;
    _state->engine.prepare(sampleRate);
    AVAudioSourceNode* source = [[AVAudioSourceNode alloc] initWithRenderBlock:^OSStatus(BOOL* isSilence, const AudioTimeStamp*, AVAudioFrameCount frameCount, AudioBufferList* outputData) {
        if (isSilence) *isSilence = NO;
        if (frameCount > kMaxRenderFrames) {
            for (uint32_t b = 0; b < outputData->mNumberBuffers; ++b) {
                const size_t samples = outputData->mBuffers[b].mDataByteSize / sizeof(float);
                if (outputData->mBuffers[b].mData && samples > 0u) {
                    std::fill_n(static_cast<float*>(outputData->mBuffers[b].mData), samples, 0.0f);
                }
            }
            return noErr;
        }
        float* lanes[kChannels] {};
        for (uint32_t ch = 0; ch < kChannels; ++ch) {
            lanes[ch] = _state->scratch[ch].data();
            std::fill_n(lanes[ch], frameCount, 0.0f);
        }
        auto sample = std::atomic_load_explicit(&_state->sample, std::memory_order_acquire);
        const bool playing = _state->playing.load(std::memory_order_acquire);
        _state->engine.setParams(snapshotParams(*_state));
        if (_state->resyncRequested.exchange(false, std::memory_order_acq_rel)) _state->engine.resync();
        _state->engine.process(sample, lanes, frameCount, playing);
        float phases[kChannels] {};
        _state->engine.lanePhases(phases, kChannels);
        for (uint32_t ch = 0; ch < kChannels; ++ch) _state->lanePhases[ch].store(phases[ch], std::memory_order_relaxed);

        float peak = 0.0f;
        const uint32_t outBuffers = std::max<uint32_t>(1u, outputData->mNumberBuffers);
        for (uint32_t b = 0; b < outputData->mNumberBuffers; ++b) {
            float* out = static_cast<float*>(outputData->mBuffers[b].mData);
            const size_t writableSamples = outputData->mBuffers[b].mDataByteSize / sizeof(float);
            if (!out || writableSamples == 0u) continue;
            std::fill_n(out, writableSamples, 0.0f);
            const uint32_t bufferChannels = std::max<uint32_t>(
                1u,
                static_cast<uint32_t>(writableSamples / std::max<AVAudioFrameCount>(1u, frameCount)));
            for (uint32_t i = 0; i < frameCount; ++i) {
                for (uint32_t c = 0; c < bufferChannels; ++c) {
                    const size_t index = static_cast<size_t>(i) * bufferChannels + c;
                    if (index >= writableSamples) continue;
                    const uint32_t outCh = b + c;
                    float value = 0.0f;
                    if (outBuffers >= kChannels) {
                        value = outCh < kChannels ? lanes[outCh][i] : 0.0f;
                    } else {
                        float sum = 0.0f;
                        uint32_t count = 0u;
                        for (uint32_t lane = outCh; lane < kChannels; lane += outBuffers) {
                            sum += lanes[lane][i];
                            ++count;
                        }
                        value = count > 0u ? sum / static_cast<float>(count) : 0.0f;
                    }
                    out[index] = value;
                    peak = std::max(peak, std::fabs(value));
                }
            }
        }
        _state->outputPeak.store(std::max(_state->outputPeak.load(std::memory_order_relaxed) * 0.92f, peak), std::memory_order_relaxed);
        return noErr;
    }];
    _state->sourceNode = [source retain];
    [_state->audioEngine attachNode:_state->sourceNode];
    configureAudioGraph(*_state);

    MIDIClientCreate(CFSTR("s3g Multi Loop Processor"), nullptr, nullptr, &_state->midiClient);
    MIDIInputPortCreate(_state->midiClient, CFSTR("Input"), midiReadProc, _state, &_state->midiPort);
    const ItemCount sources = MIDIGetNumberOfSources();
    for (ItemCount i = 0; i < sources; ++i) {
        MIDIEndpointRef src = MIDIGetSource(i);
        if (src) MIDIPortConnectSource(_state->midiPort, src, nullptr);
    }

    _window = [[NSWindow alloc] initWithContentRect:NSMakeRect(120, 120, kGuiW, kGuiH)
                                         styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable
                                           backing:NSBackingStoreBuffered
                                             defer:NO];
    [_window setTitle:@"s3g Multi Loop Processor"];
    [_window setContentView:[[[S3GStandaloneView alloc] initWithState:_state] autorelease]];
    [_window makeKeyAndOrderFront:nil];
}
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender { (void)sender; return YES; }
- (void)applicationWillTerminate:(NSNotification*)notification
{
    (void)notification;
    if (_state) {
        if (_state->midiPort) MIDIPortDispose(_state->midiPort);
        if (_state->midiClient) MIDIClientDispose(_state->midiClient);
        [_state->audioEngine stop];
        [_state->sourceNode release];
        [_state->audioEngine release];
        delete _state;
        _state = nullptr;
    }
}
@end

int main(int argc, const char* argv[])
{
    (void)argc;
    (void)argv;
    @autoreleasepool {
        NSApplication* app = [NSApplication sharedApplication];
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];
        S3GAppDelegate* delegate = [[S3GAppDelegate alloc] init];
        [app setDelegate:delegate];
        [app run];
        [delegate release];
    }
    return 0;
}
