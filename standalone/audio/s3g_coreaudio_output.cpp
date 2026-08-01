#include "s3g_coreaudio_output.h"

#if defined(__APPLE__)

#include <algorithm>
#include <cmath>
#include <limits>
#include <mach/mach_time.h>
#include <vector>

namespace s3g::standalone {
namespace {

uint32_t outputChannelCount(AudioDeviceID device)
{
    AudioObjectPropertyAddress address {
        kAudioDevicePropertyStreamConfiguration,
        kAudioDevicePropertyScopeOutput,
        kAudioObjectPropertyElementMain,
    };
    UInt32 size = 0u;
    if (AudioObjectGetPropertyDataSize(device, &address, 0u, nullptr,
            &size) != noErr || size == 0u) return 0u;
    std::vector<uint8_t> storage(size);
    auto* buffers = reinterpret_cast<AudioBufferList*>(storage.data());
    if (AudioObjectGetPropertyData(device, &address, 0u, nullptr, &size,
            buffers) != noErr) return 0u;
    uint32_t channels = 0u;
    for (UInt32 index = 0u; index < buffers->mNumberBuffers; ++index)
        channels += buffers->mBuffers[index].mNumberChannels;
    return channels;
}

double nominalSampleRate(AudioDeviceID device)
{
    Float64 sampleRate = 48000.0;
    UInt32 size = sizeof(sampleRate);
    AudioObjectPropertyAddress address {
        kAudioDevicePropertyNominalSampleRate,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain,
    };
    if (AudioObjectGetPropertyData(device, &address, 0u, nullptr, &size,
            &sampleRate) != noErr || sampleRate <= 0.0) return 48000.0;
    return sampleRate;
}

std::string stringProperty(AudioDeviceID device, AudioObjectPropertySelector selector)
{
    AudioObjectPropertyAddress address {
        selector,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain,
    };
    CFStringRef value = nullptr;
    UInt32 size = sizeof(value);
    if (AudioObjectGetPropertyData(device, &address, 0u, nullptr, &size,
            &value) != noErr || !value) return {};
    char text[512] {};
    CFStringGetCString(value, text, sizeof(text), kCFStringEncodingUTF8);
    CFRelease(value);
    return text;
}

void setError(std::string* error, const char* operation, OSStatus status)
{
    if (!error) return;
    *error = std::string(operation) + " failed (OSStatus "
        + std::to_string(static_cast<int32_t>(status)) + ")";
}

} // namespace

void RealtimeCallbackTelemetry::updateMaximum(
    std::atomic<uint64_t>& destination, uint64_t value)
{
    uint64_t current = destination.load(std::memory_order_relaxed);
    while (current < value && !destination.compare_exchange_weak(current,
        value, std::memory_order_relaxed, std::memory_order_relaxed)) {}
}

void RealtimeCallbackTelemetry::reset()
{
    callbackCount_.store(0u, std::memory_order_relaxed);
    deadlineMissCount_.store(0u, std::memory_order_relaxed);
    oversizedCallbackCount_.store(0u, std::memory_order_relaxed);
    processorOverloadCount_.store(0u, std::memory_order_relaxed);
    callbackErrorCount_.store(0u, std::memory_order_relaxed);
    smoothedLoadPartsPerMillion_.store(0u, std::memory_order_relaxed);
    peakLoadPartsPerMillion_.store(0u, std::memory_order_relaxed);
    lastCallbackNanoseconds_.store(0u, std::memory_order_relaxed);
    maximumCallbackNanoseconds_.store(0u, std::memory_order_relaxed);
}

void RealtimeCallbackTelemetry::recordCallbackNanoseconds(
    uint64_t durationNanoseconds, uint32_t frames, double sampleRate,
    uint32_t maximumFrames, bool callbackFailed)
{
    callbackCount_.fetch_add(1u, std::memory_order_relaxed);
    lastCallbackNanoseconds_.store(durationNanoseconds,
        std::memory_order_relaxed);
    updateMaximum(maximumCallbackNanoseconds_, durationNanoseconds);
    if (maximumFrames > 0u && frames > maximumFrames)
        oversizedCallbackCount_.fetch_add(1u, std::memory_order_relaxed);
    if (callbackFailed)
        callbackErrorCount_.fetch_add(1u, std::memory_order_relaxed);
    if (!(sampleRate > 0.0) || frames == 0u) return;

    const double deadlineNanoseconds = static_cast<double>(frames)
        * 1.0e9 / sampleRate;
    if (static_cast<double>(durationNanoseconds) > deadlineNanoseconds)
        deadlineMissCount_.fetch_add(1u, std::memory_order_relaxed);
    const double load = static_cast<double>(durationNanoseconds)
        / deadlineNanoseconds;
    // A pathological stalled callback should not overflow the fixed-point
    // EMA. Capping display load at 1000x still leaves ample diagnostic range.
    const uint64_t loadPartsPerMillion = static_cast<uint64_t>(std::llround(
        std::min(load, 1000.0) * 1000000.0));
    const uint64_t previous = smoothedLoadPartsPerMillion_.load(
        std::memory_order_relaxed);
    const uint64_t smoothed = previous == 0u ? loadPartsPerMillion
        : (previous * 15u + loadPartsPerMillion) / 16u;
    smoothedLoadPartsPerMillion_.store(smoothed,
        std::memory_order_relaxed);
    updateMaximum(peakLoadPartsPerMillion_, loadPartsPerMillion);
}

void RealtimeCallbackTelemetry::recordProcessorOverload()
{
    processorOverloadCount_.fetch_add(1u, std::memory_order_relaxed);
}

RealtimeCallbackTelemetrySnapshot RealtimeCallbackTelemetry::snapshot() const
{
    RealtimeCallbackTelemetrySnapshot result;
    result.callbackCount = callbackCount_.load(std::memory_order_relaxed);
    result.deadlineMissCount = deadlineMissCount_.load(
        std::memory_order_relaxed);
    result.oversizedCallbackCount = oversizedCallbackCount_.load(
        std::memory_order_relaxed);
    result.processorOverloadCount = processorOverloadCount_.load(
        std::memory_order_relaxed);
    result.callbackErrorCount = callbackErrorCount_.load(
        std::memory_order_relaxed);
    result.smoothedLoad = static_cast<double>(
        smoothedLoadPartsPerMillion_.load(std::memory_order_relaxed))
        / 1000000.0;
    result.peakLoad = static_cast<double>(
        peakLoadPartsPerMillion_.load(std::memory_order_relaxed))
        / 1000000.0;
    result.lastCallbackMilliseconds = static_cast<double>(
        lastCallbackNanoseconds_.load(std::memory_order_relaxed)) / 1.0e6;
    result.maximumCallbackMilliseconds = static_cast<double>(
        maximumCallbackNanoseconds_.load(std::memory_order_relaxed)) / 1.0e6;
    return result;
}

CoreAudioOutput::~CoreAudioOutput() { close(); }

std::vector<CoreAudioDeviceInfo> CoreAudioOutput::enumerateDevices()
{
    AudioObjectPropertyAddress address {
        kAudioHardwarePropertyDevices,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain,
    };
    UInt32 size = 0u;
    std::vector<CoreAudioDeviceInfo> result;
    if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &address,
            0u, nullptr, &size) != noErr || size == 0u) return result;
    std::vector<AudioDeviceID> ids(size / sizeof(AudioDeviceID));
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, 0u,
            nullptr, &size, ids.data()) != noErr) return result;
    for (const AudioDeviceID id : ids) {
        const uint32_t channels = outputChannelCount(id);
        if (channels == 0u) continue;
        CoreAudioDeviceInfo info;
        info.id = id;
        info.uid = stringProperty(id, kAudioDevicePropertyDeviceUID);
        info.name = stringProperty(id, kAudioObjectPropertyName);
        info.outputChannels = channels;
        info.sampleRate = nominalSampleRate(id);
        if (info.name.empty()) info.name = "Output Device";
        result.push_back(std::move(info));
    }
    std::sort(result.begin(), result.end(),
        [](const CoreAudioDeviceInfo& left, const CoreAudioDeviceInfo& right) {
            if (left.outputChannels != right.outputChannels)
                return left.outputChannels > right.outputChannels;
            return left.name < right.name;
        });
    return result;
}

AudioDeviceID CoreAudioOutput::defaultOutputDevice()
{
    AudioDeviceID device = kAudioObjectUnknown;
    UInt32 size = sizeof(device);
    AudioObjectPropertyAddress address {
        kAudioHardwarePropertyDefaultOutputDevice,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain,
    };
    AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, 0u,
        nullptr, &size, &device);
    return device;
}

bool CoreAudioOutput::open(AudioDeviceID device, uint32_t renderChannels,
    RenderCallback render, void* context, std::string* error)
{
    close();
    telemetry_.reset();
    if (device == kAudioObjectUnknown || !render || renderChannels == 0u) {
        if (error) *error = "Invalid Core Audio output configuration";
        return false;
    }
    const uint32_t hardwareChannels = outputChannelCount(device);
    if (hardwareChannels < renderChannels) {
        if (error) *error = "Selected device does not have enough output channels";
        return false;
    }

    AudioComponentDescription description {};
    description.componentType = kAudioUnitType_Output;
    description.componentSubType = kAudioUnitSubType_HALOutput;
    description.componentManufacturer = kAudioUnitManufacturer_Apple;
    AudioComponent component = AudioComponentFindNext(nullptr, &description);
    if (!component) {
        if (error) *error = "Core Audio HAL output component is unavailable";
        return false;
    }
    OSStatus status = AudioComponentInstanceNew(component, &unit_);
    if (status != noErr || !unit_) {
        setError(error, "Creating HAL output", status);
        unit_ = nullptr;
        return false;
    }

    UInt32 enabled = 1u;
    status = AudioUnitSetProperty(unit_,
        kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Output, 0u,
        &enabled, sizeof(enabled));
    UInt32 disabled = 0u;
    if (status == noErr) {
        status = AudioUnitSetProperty(unit_,
            kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Input, 1u,
            &disabled, sizeof(disabled));
    }
    if (status == noErr) {
        status = AudioUnitSetProperty(unit_,
            kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global,
            0u, &device, sizeof(device));
    }

    const double sampleRate = nominalSampleRate(device);
    AudioStreamBasicDescription format {};
    format.mSampleRate = sampleRate;
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = kAudioFormatFlagIsFloat
        | kAudioFormatFlagIsPacked | kAudioFormatFlagIsNonInterleaved
        | kAudioFormatFlagsNativeEndian;
    format.mBitsPerChannel = 32u;
    format.mChannelsPerFrame = renderChannels;
    format.mFramesPerPacket = 1u;
    format.mBytesPerFrame = sizeof(float);
    format.mBytesPerPacket = sizeof(float);
    if (status == noErr) {
        status = AudioUnitSetProperty(unit_, kAudioUnitProperty_StreamFormat,
            kAudioUnitScope_Input, 0u, &format, sizeof(format));
    }

    render_ = render;
    renderContext_ = context;
    AURenderCallbackStruct callback { renderThunk, this };
    if (status == noErr) {
        status = AudioUnitSetProperty(unit_,
            kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0u,
            &callback, sizeof(callback));
    }
    if (status == noErr) status = AudioUnitInitialize(unit_);
    if (status != noErr) {
        setError(error, "Initializing HAL output", status);
        close();
        return false;
    }

    UInt32 maximumFrames = 0u;
    UInt32 maximumFramesSize = sizeof(maximumFrames);
    if (AudioUnitGetProperty(unit_, kAudioUnitProperty_MaximumFramesPerSlice,
            kAudioUnitScope_Global, 0u, &maximumFrames,
            &maximumFramesSize) != noErr || maximumFrames == 0u) {
        maximumFrames = 4096u;
    }
    config_.device = device;
    config_.hardwareChannels = hardwareChannels;
    config_.renderChannels = renderChannels;
    config_.maximumFrames = maximumFrames;
    config_.sampleRate = sampleRate;
    mach_timebase_info_data_t timebase {};
    if (mach_timebase_info(&timebase) == KERN_SUCCESS
        && timebase.numer != 0u && timebase.denom != 0u) {
        hostTimeNumerator_ = timebase.numer;
        hostTimeDenominator_ = timebase.denom;
    } else {
        hostTimeNumerator_ = 1u;
        hostTimeDenominator_ = 1u;
    }
    config_.hostTicksPerSecond = 1.0e9
        * static_cast<double>(hostTimeDenominator_)
        / static_cast<double>(hostTimeNumerator_);

    AudioObjectPropertyAddress overloadAddress {
        kAudioDeviceProcessorOverload,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain,
    };
    overloadListenerInstalled_ = AudioObjectHasProperty(device,
        &overloadAddress) && AudioObjectAddPropertyListener(device,
        &overloadAddress, devicePropertyListener, this) == noErr;
    return true;
}

bool CoreAudioOutput::start(std::string* error)
{
    if (!unit_) {
        if (error) *error = "Core Audio output is not open";
        return false;
    }
    if (running_) return true;
    const OSStatus status = AudioOutputUnitStart(unit_);
    if (status != noErr) {
        setError(error, "Starting HAL output", status);
        return false;
    }
    running_ = true;
    return true;
}

void CoreAudioOutput::stop()
{
    if (unit_ && running_) AudioOutputUnitStop(unit_);
    running_ = false;
}

void CoreAudioOutput::close()
{
    stop();
    if (overloadListenerInstalled_
        && config_.device != kAudioObjectUnknown) {
        AudioObjectPropertyAddress overloadAddress {
            kAudioDeviceProcessorOverload,
            kAudioObjectPropertyScopeGlobal,
            kAudioObjectPropertyElementMain,
        };
        AudioObjectRemovePropertyListener(config_.device, &overloadAddress,
            devicePropertyListener, this);
    }
    overloadListenerInstalled_ = false;
    if (unit_) {
        AudioUnitUninitialize(unit_);
        AudioComponentInstanceDispose(unit_);
    }
    unit_ = nullptr;
    render_ = nullptr;
    renderContext_ = nullptr;
    config_ = {};
}

OSStatus CoreAudioOutput::renderThunk(void* context,
    AudioUnitRenderActionFlags*, const AudioTimeStamp* timestamp, UInt32,
    UInt32 frames, AudioBufferList* output)
{
    auto* self = static_cast<CoreAudioOutput*>(context);
    if (!self || !self->render_) return noErr;
    const uint64_t start = mach_absolute_time();
    const uint64_t blockHostTime = timestamp
        && (timestamp->mFlags & kAudioTimeStampHostTimeValid) != 0u
        ? timestamp->mHostTime : start;
    const OSStatus status = self->render_(self->renderContext_, output,
        frames, blockHostTime);
    const uint64_t elapsedTicks = mach_absolute_time() - start;
    const long double elapsedNanoseconds = static_cast<long double>(
        elapsedTicks) * self->hostTimeNumerator_
        / self->hostTimeDenominator_;
    self->telemetry_.recordCallbackNanoseconds(static_cast<uint64_t>(
        std::min<long double>(elapsedNanoseconds,
            std::numeric_limits<uint64_t>::max())), frames,
        self->config_.sampleRate, self->config_.maximumFrames,
        status != noErr);
    return status;
}

OSStatus CoreAudioOutput::devicePropertyListener(AudioObjectID,
    UInt32 addressCount, const AudioObjectPropertyAddress* addresses,
    void* context)
{
    auto* self = static_cast<CoreAudioOutput*>(context);
    if (!self || !addresses) return noErr;
    for (UInt32 index = 0u; index < addressCount; ++index) {
        if (addresses[index].mSelector != kAudioDeviceProcessorOverload)
            continue;
        self->telemetry_.recordProcessorOverload();
    }
    return noErr;
}

} // namespace s3g::standalone

#endif
