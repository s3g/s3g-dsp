#include "s3g_coreaudio_output.h"

#if defined(__APPLE__)

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <mach/mach_time.h>
#include <thread>
#include <vector>

namespace s3g::standalone {
namespace {

static_assert(std::atomic<uint64_t>::is_always_lock_free,
    "Realtime telemetry requires lock-free 64-bit atomics");
static_assert(sizeof(double) == sizeof(uint64_t),
    "Sample-time telemetry requires 64-bit doubles");

uint64_t doubleBits(double value)
{
    uint64_t bits = 0u;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

double doubleFromBits(uint64_t bits)
{
    double value = 0.0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

template <typename Value>
bool deviceProperty(AudioDeviceID device,
    AudioObjectPropertySelector selector, AudioObjectPropertyScope scope,
    Value& value)
{
    AudioObjectPropertyAddress address {
        selector,
        scope,
        kAudioObjectPropertyElementMain,
    };
    if (!AudioObjectHasProperty(device, &address)) return false;
    UInt32 size = sizeof(value);
    return AudioObjectGetPropertyData(device, &address, 0u, nullptr, &size,
        &value) == noErr && size == sizeof(value);
}

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
    renderedFrameCount_.store(0u, std::memory_order_relaxed);
    deadlineMissCount_.store(0u, std::memory_order_relaxed);
    lateCallbackCount_.store(0u, std::memory_order_relaxed);
    timestampDiscontinuityCount_.store(0u, std::memory_order_relaxed);
    timestampUnavailableCount_.store(0u, std::memory_order_relaxed);
    sampleTimeDiscontinuityCount_.store(0u, std::memory_order_relaxed);
    sampleTimeUnavailableCount_.store(0u, std::memory_order_relaxed);
    oversizedCallbackCount_.store(0u, std::memory_order_relaxed);
    processorOverloadCount_.store(0u, std::memory_order_relaxed);
    abnormalStopCount_.store(0u, std::memory_order_relaxed);
    deviceAliveChangeCount_.store(0u, std::memory_order_relaxed);
    deviceConfigurationChangeCount_.store(0u, std::memory_order_relaxed);
    callbackErrorCount_.store(0u, std::memory_order_relaxed);
    renderActionErrorCount_.store(0u, std::memory_order_relaxed);
    smoothedLoadPartsPerMillion_.store(0u, std::memory_order_relaxed);
    peakLoadPartsPerMillion_.store(0u, std::memory_order_relaxed);
    lastLoadPartsPerMillion_.store(0u, std::memory_order_relaxed);
    lastCallbackNanoseconds_.store(0u, std::memory_order_relaxed);
    maximumCallbackNanoseconds_.store(0u, std::memory_order_relaxed);
    maximumLatenessNanoseconds_.store(0u, std::memory_order_relaxed);
    maximumTimestampErrorNanoseconds_.store(0u,
        std::memory_order_relaxed);
    maximumSampleTimeErrorMicroframes_.store(0u,
        std::memory_order_relaxed);
    clearTimingSequence();
}

void RealtimeCallbackTelemetry::clearTimingSequence()
{
    previousCallbackStartHostTime_.store(0u, std::memory_order_relaxed);
    previousAudioHostTime_.store(0u, std::memory_order_relaxed);
    previousAudioSampleTimeBits_.store(0u, std::memory_order_relaxed);
    previousAudioSampleTimeValid_.store(false, std::memory_order_relaxed);
    previousFrames_.store(0u, std::memory_order_relaxed);
}

void RealtimeCallbackTelemetry::recordCallbackNanoseconds(
    uint64_t durationNanoseconds, uint32_t frames, double sampleRate,
    uint32_t maximumFrames, bool callbackFailed)
{
    callbackCount_.fetch_add(1u, std::memory_order_relaxed);
    renderedFrameCount_.fetch_add(frames, std::memory_order_relaxed);
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
    lastLoadPartsPerMillion_.store(loadPartsPerMillion,
        std::memory_order_relaxed);
    const uint64_t previous = smoothedLoadPartsPerMillion_.load(
        std::memory_order_relaxed);
    const uint64_t smoothed = previous == 0u ? loadPartsPerMillion
        : (previous * 15u + loadPartsPerMillion) / 16u;
    smoothedLoadPartsPerMillion_.store(smoothed,
        std::memory_order_relaxed);
    updateMaximum(peakLoadPartsPerMillion_, loadPartsPerMillion);
}

void RealtimeCallbackTelemetry::recordCallbackTiming(
    uint64_t durationNanoseconds, uint32_t frames, double sampleRate,
    uint32_t maximumFrames, uint64_t callbackStartHostTime,
    uint64_t audioHostTime, double audioSampleTime,
    double hostTicksPerSecond, bool audioHostTimeValid,
    bool audioSampleTimeValid, bool callbackFailed, bool renderActionFailed)
{
    recordCallbackNanoseconds(durationNanoseconds, frames, sampleRate,
        maximumFrames, callbackFailed);
    if (renderActionFailed)
        renderActionErrorCount_.fetch_add(1u, std::memory_order_relaxed);

    const uint32_t previousFrames = previousFrames_.exchange(frames,
        std::memory_order_relaxed);
    const uint64_t previousCallbackStart =
        previousCallbackStartHostTime_.exchange(callbackStartHostTime,
            std::memory_order_relaxed);

    // The HAL sample position is a separate continuity signal from host-clock
    // cadence. A valid position must advance by the preceding callback's frame
    // count, including when the device uses variable buffer sizes.
    const bool sampleTimeUsable = audioSampleTimeValid
        && std::isfinite(audioSampleTime);
    if (!sampleTimeUsable) {
        sampleTimeUnavailableCount_.fetch_add(1u,
            std::memory_order_relaxed);
        previousAudioSampleTimeBits_.store(0u, std::memory_order_relaxed);
        previousAudioSampleTimeValid_.store(false,
            std::memory_order_relaxed);
    } else {
        const uint64_t previousSampleTimeBits =
            previousAudioSampleTimeBits_.exchange(doubleBits(audioSampleTime),
                std::memory_order_relaxed);
        const bool previousSampleTimeValid =
            previousAudioSampleTimeValid_.exchange(true,
                std::memory_order_relaxed);
        if (previousSampleTimeValid && previousFrames > 0u) {
            const long double previousSampleTime = doubleFromBits(
                previousSampleTimeBits);
            const long double actualDelta =
                static_cast<long double>(audioSampleTime)
                - previousSampleTime;
            const long double sampleTimeErrorFrames = std::abs(actualDelta
                - static_cast<long double>(previousFrames));
            const long double toleranceFrames = std::max<long double>(2.0L,
                static_cast<long double>(previousFrames) * 0.005L);
            if (sampleTimeErrorFrames > toleranceFrames) {
                sampleTimeDiscontinuityCount_.fetch_add(1u,
                    std::memory_order_relaxed);
                constexpr long double kMicroframesPerFrame = 1000000.0L;
                const long double scaledError = std::min<long double>(
                    sampleTimeErrorFrames * kMicroframesPerFrame,
                    std::numeric_limits<uint64_t>::max());
                updateMaximum(maximumSampleTimeErrorMicroframes_,
                    static_cast<uint64_t>(scaledError));
            }
        }
    }

    if (!(sampleRate > 0.0) || !(hostTicksPerSecond > 0.0)
        || frames == 0u) {
        previousAudioHostTime_.store(0u, std::memory_order_relaxed);
        return;
    }

    const long double nanosecondsPerTick = 1.0e9L
        / static_cast<long double>(hostTicksPerSecond);
    const long double previousPeriodNanoseconds = previousFrames > 0u
        ? static_cast<long double>(previousFrames) * 1.0e9L / sampleRate
        : 0.0L;
    if (previousCallbackStart != 0u && previousFrames > 0u
        && callbackStartHostTime > previousCallbackStart) {
        const long double actualIntervalNanoseconds =
            static_cast<long double>(callbackStartHostTime
                - previousCallbackStart) * nanosecondsPerTick;
        // IO-cycle launch has normal scheduler jitter. Count only a material
        // late start: more than ten percent of a period, with a 250 us floor.
        const long double toleranceNanoseconds = std::max<long double>(
            250000.0L, previousPeriodNanoseconds * 0.10L);
        if (actualIntervalNanoseconds
            > previousPeriodNanoseconds + toleranceNanoseconds) {
            lateCallbackCount_.fetch_add(1u, std::memory_order_relaxed);
            const long double lateness = actualIntervalNanoseconds
                - previousPeriodNanoseconds;
            updateMaximum(maximumLatenessNanoseconds_,
                static_cast<uint64_t>(std::min<long double>(lateness,
                    std::numeric_limits<uint64_t>::max())));
        }
    }

    if (!audioHostTimeValid || audioHostTime == 0u) {
        timestampUnavailableCount_.fetch_add(1u,
            std::memory_order_relaxed);
        previousAudioHostTime_.store(0u, std::memory_order_relaxed);
        return;
    }
    const uint64_t previousAudioHostTime = previousAudioHostTime_.exchange(
        audioHostTime, std::memory_order_relaxed);
    if (previousAudioHostTime == 0u || previousFrames == 0u) return;

    long double timestampErrorNanoseconds = 0.0L;
    if (audioHostTime > previousAudioHostTime) {
        const long double actualTimestampInterval =
            static_cast<long double>(audioHostTime - previousAudioHostTime)
            * nanosecondsPerTick;
        timestampErrorNanoseconds = std::abs(actualTimestampInterval
            - previousPeriodNanoseconds);
    } else {
        // A repeated or backwards HAL timestamp is always discontinuous.
        timestampErrorNanoseconds = previousPeriodNanoseconds;
    }
    // HAL timestamps normally advance exactly by the previous block size.
    // Allow two samples of rounding error (or 0.5% of a block) so aggregate
    // devices do not create noise in the report.
    const long double timestampToleranceNanoseconds =
        std::max<long double>(2.0e9L / sampleRate,
            previousPeriodNanoseconds * 0.005L);
    if (timestampErrorNanoseconds > timestampToleranceNanoseconds) {
        timestampDiscontinuityCount_.fetch_add(1u,
            std::memory_order_relaxed);
        updateMaximum(maximumTimestampErrorNanoseconds_,
            static_cast<uint64_t>(std::min<long double>(
                timestampErrorNanoseconds,
                std::numeric_limits<uint64_t>::max())));
    }
}

void RealtimeCallbackTelemetry::recordProcessorOverload()
{
    processorOverloadCount_.fetch_add(1u, std::memory_order_relaxed);
}

void RealtimeCallbackTelemetry::recordAbnormalStop()
{
    abnormalStopCount_.fetch_add(1u, std::memory_order_relaxed);
}

void RealtimeCallbackTelemetry::recordDeviceAliveChange()
{
    deviceAliveChangeCount_.fetch_add(1u, std::memory_order_relaxed);
}

void RealtimeCallbackTelemetry::recordDeviceConfigurationChange()
{
    deviceConfigurationChangeCount_.fetch_add(1u,
        std::memory_order_relaxed);
}

void RealtimeCallbackTelemetry::recordRenderActionError()
{
    renderActionErrorCount_.fetch_add(1u, std::memory_order_relaxed);
}

RealtimeCallbackTelemetrySnapshot RealtimeCallbackTelemetry::snapshot() const
{
    RealtimeCallbackTelemetrySnapshot result;
    result.callbackCount = callbackCount_.load(std::memory_order_relaxed);
    result.renderedFrameCount = renderedFrameCount_.load(
        std::memory_order_relaxed);
    result.deadlineMissCount = deadlineMissCount_.load(
        std::memory_order_relaxed);
    result.lateCallbackCount = lateCallbackCount_.load(
        std::memory_order_relaxed);
    result.timestampDiscontinuityCount = timestampDiscontinuityCount_.load(
        std::memory_order_relaxed);
    result.timestampUnavailableCount = timestampUnavailableCount_.load(
        std::memory_order_relaxed);
    result.sampleTimeDiscontinuityCount =
        sampleTimeDiscontinuityCount_.load(std::memory_order_relaxed);
    result.sampleTimeUnavailableCount = sampleTimeUnavailableCount_.load(
        std::memory_order_relaxed);
    result.oversizedCallbackCount = oversizedCallbackCount_.load(
        std::memory_order_relaxed);
    result.processorOverloadCount = processorOverloadCount_.load(
        std::memory_order_relaxed);
    result.abnormalStopCount = abnormalStopCount_.load(
        std::memory_order_relaxed);
    result.deviceAliveChangeCount = deviceAliveChangeCount_.load(
        std::memory_order_relaxed);
    result.deviceConfigurationChangeCount =
        deviceConfigurationChangeCount_.load(std::memory_order_relaxed);
    result.callbackErrorCount = callbackErrorCount_.load(
        std::memory_order_relaxed);
    result.renderActionErrorCount = renderActionErrorCount_.load(
        std::memory_order_relaxed);
    result.smoothedLoad = static_cast<double>(
        smoothedLoadPartsPerMillion_.load(std::memory_order_relaxed))
        / 1000000.0;
    result.peakLoad = static_cast<double>(
        peakLoadPartsPerMillion_.load(std::memory_order_relaxed))
        / 1000000.0;
    result.lastLoad = static_cast<double>(
        lastLoadPartsPerMillion_.load(std::memory_order_relaxed))
        / 1000000.0;
    result.lastCallbackMilliseconds = static_cast<double>(
        lastCallbackNanoseconds_.load(std::memory_order_relaxed)) / 1.0e6;
    result.maximumCallbackMilliseconds = static_cast<double>(
        maximumCallbackNanoseconds_.load(std::memory_order_relaxed)) / 1.0e6;
    result.maximumLatenessMilliseconds = static_cast<double>(
        maximumLatenessNanoseconds_.load(std::memory_order_relaxed)) / 1.0e6;
    result.maximumTimestampErrorMilliseconds = static_cast<double>(
        maximumTimestampErrorNanoseconds_.load(std::memory_order_relaxed))
        / 1.0e6;
    result.maximumSampleTimeErrorFrames = static_cast<double>(
        maximumSampleTimeErrorMicroframes_.load(std::memory_order_relaxed))
        / 1.0e6;
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
    resetTelemetry();
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
    renderErrorListenerInstalled_ = AudioUnitAddPropertyListener(unit_,
        kAudioUnitProperty_LastRenderError, renderErrorListener,
        this) == noErr;

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
    deviceProperty(device, kAudioDevicePropertyBufferFrameSize,
        kAudioObjectPropertyScopeGlobal, config_.deviceBufferFrames);
    deviceProperty(device, kAudioDevicePropertyLatency,
        kAudioDevicePropertyScopeOutput, config_.deviceLatencyFrames);
    deviceProperty(device, kAudioDevicePropertySafetyOffset,
        kAudioDevicePropertyScopeOutput, config_.deviceSafetyOffsetFrames);
    UInt32 maximumVariableFrames = 0u;
    if (deviceProperty(device,
            kAudioDevicePropertyUsesVariableBufferFrameSizes,
            kAudioObjectPropertyScopeGlobal, maximumVariableFrames)
        && maximumVariableFrames > 0u) {
        config_.usesVariableBufferFrames = true;
        config_.maximumVariableBufferFrames = maximumVariableFrames;
    }
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

    const auto installListener = [&](AudioObjectPropertySelector selector) {
        AudioObjectPropertyAddress address { selector,
            kAudioObjectPropertyScopeGlobal,
            kAudioObjectPropertyElementMain };
        return AudioObjectHasProperty(device, &address)
            && AudioObjectAddPropertyListener(device, &address,
                devicePropertyListener, this) == noErr;
    };
    overloadListenerInstalled_ = installListener(
        kAudioDeviceProcessorOverload);
    abnormalStopListenerInstalled_ = installListener(
        kAudioDevicePropertyIOStoppedAbnormally);
    deviceAliveListenerInstalled_ = installListener(
        kAudioDevicePropertyDeviceIsAlive);
    deviceConfigurationListenerInstalled_ = installListener(
        kAudioDevicePropertyDeviceHasChanged);
    config_.processorOverloadSignalAvailable = overloadListenerInstalled_;
    config_.abnormalStopSignalAvailable = abnormalStopListenerInstalled_;
    config_.renderErrorSignalAvailable = renderErrorListenerInstalled_;
    config_.deviceAliveSignalAvailable = deviceAliveListenerInstalled_;
    config_.deviceConfigurationSignalAvailable =
        deviceConfigurationListenerInstalled_;
    return true;
}

void CoreAudioOutput::setTelemetryEnabled(bool enabled)
{
    replaceTelemetryBank(enabled);
}

void CoreAudioOutput::resetTelemetry()
{
    replaceTelemetryBank(telemetryEnabled_.load(std::memory_order_acquire));
}

RealtimeCallbackTelemetrySnapshot CoreAudioOutput::telemetry() const
{
    for (;;) {
        const uint32_t bank = telemetryBank_.load(std::memory_order_acquire);
        const auto result = telemetry_[bank].snapshot();
        if (bank == telemetryBank_.load(std::memory_order_acquire))
            return result;
    }
}

void CoreAudioOutput::replaceTelemetryBank(bool enabled)
{
    telemetryEnabled_.store(false, std::memory_order_release);
    const uint32_t current = telemetryBank_.load(std::memory_order_relaxed);
    const uint32_t next = current ^ 1u;
    // The inactive bank can still have a writer from an earlier rapid reset.
    // Waiting happens only on the control thread, never in the audio callback.
    while (telemetryWriters_[next].load(std::memory_order_acquire) != 0u)
        std::this_thread::yield();
    telemetry_[next].reset();
    telemetryBank_.store(next, std::memory_order_release);
    telemetryEnabled_.store(enabled, std::memory_order_release);
}

bool CoreAudioOutput::beginTelemetryWrite(uint32_t& bank)
{
    if (!telemetryEnabled_.load(std::memory_order_acquire)) return false;
    bank = telemetryBank_.load(std::memory_order_acquire);
    telemetryWriters_[bank].fetch_add(1u, std::memory_order_acq_rel);
    if (!telemetryEnabled_.load(std::memory_order_acquire)
        || bank != telemetryBank_.load(std::memory_order_acquire)) {
        telemetryWriters_[bank].fetch_sub(1u, std::memory_order_release);
        return false;
    }
    return true;
}

void CoreAudioOutput::endTelemetryWrite(uint32_t bank)
{
    telemetryWriters_[bank].fetch_sub(1u, std::memory_order_release);
}

bool CoreAudioOutput::start(std::string* error)
{
    if (!unit_) {
        if (error) *error = "Core Audio output is not open";
        return false;
    }
    if (running_) return true;
    telemetry_[telemetryBank_.load(std::memory_order_acquire)]
        .clearTimingSequence();
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
    telemetry_[telemetryBank_.load(std::memory_order_acquire)]
        .clearTimingSequence();
}

void CoreAudioOutput::close()
{
    stop();
    const auto removeListener = [&](AudioObjectPropertySelector selector,
                                    bool installed) {
        if (!installed || config_.device == kAudioObjectUnknown) return;
        AudioObjectPropertyAddress address { selector,
            kAudioObjectPropertyScopeGlobal,
            kAudioObjectPropertyElementMain };
        AudioObjectRemovePropertyListener(config_.device, &address,
            devicePropertyListener, this);
    };
    removeListener(kAudioDeviceProcessorOverload,
        overloadListenerInstalled_);
    removeListener(kAudioDevicePropertyIOStoppedAbnormally,
        abnormalStopListenerInstalled_);
    removeListener(kAudioDevicePropertyDeviceIsAlive,
        deviceAliveListenerInstalled_);
    removeListener(kAudioDevicePropertyDeviceHasChanged,
        deviceConfigurationListenerInstalled_);
    overloadListenerInstalled_ = false;
    abnormalStopListenerInstalled_ = false;
    deviceAliveListenerInstalled_ = false;
    deviceConfigurationListenerInstalled_ = false;
    if (unit_ && renderErrorListenerInstalled_) {
        AudioUnitRemovePropertyListenerWithUserData(unit_,
            kAudioUnitProperty_LastRenderError, renderErrorListener, this);
    }
    renderErrorListenerInstalled_ = false;
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
    AudioUnitRenderActionFlags*,
    const AudioTimeStamp* timestamp, UInt32,
    UInt32 frames, AudioBufferList* output)
{
    auto* self = static_cast<CoreAudioOutput*>(context);
    if (!self || !self->render_) return noErr;
    const bool hostTimeValid = timestamp
        && (timestamp->mFlags & kAudioTimeStampHostTimeValid) != 0u;
    const bool sampleTimeValid = timestamp
        && (timestamp->mFlags & kAudioTimeStampSampleTimeValid) != 0u;
    const double sampleTime = sampleTimeValid ? timestamp->mSampleTime : 0.0;
    uint32_t telemetryBank = 0u;
    const bool telemetryEnabled = self->beginTelemetryWrite(telemetryBank);
    const uint64_t start = telemetryEnabled || !hostTimeValid
        ? mach_absolute_time() : 0u;
    const uint64_t blockHostTime = hostTimeValid
        ? timestamp->mHostTime : start;
    const OSStatus status = self->render_(self->renderContext_, output,
        frames, blockHostTime);
    if (!telemetryEnabled) return status;
    const uint64_t elapsedTicks = mach_absolute_time() - start;
    const long double elapsedNanoseconds = static_cast<long double>(
        elapsedTicks) * self->hostTimeNumerator_
        / self->hostTimeDenominator_;
    self->telemetry_[telemetryBank].recordCallbackTiming(static_cast<uint64_t>(
            std::min<long double>(elapsedNanoseconds,
                std::numeric_limits<uint64_t>::max())),
        frames, self->config_.sampleRate, self->config_.maximumFrames,
        start, blockHostTime, sampleTime, self->config_.hostTicksPerSecond,
        hostTimeValid, sampleTimeValid, status != noErr, false);
    self->endTelemetryWrite(telemetryBank);
    return status;
}

void CoreAudioOutput::renderErrorListener(void* context, AudioUnit,
    AudioUnitPropertyID property, AudioUnitScope, AudioUnitElement)
{
    auto* self = static_cast<CoreAudioOutput*>(context);
    if (!self || property != kAudioUnitProperty_LastRenderError) return;
    uint32_t telemetryBank = 0u;
    if (!self->beginTelemetryWrite(telemetryBank)) return;
    self->telemetry_[telemetryBank].recordRenderActionError();
    self->endTelemetryWrite(telemetryBank);
}

OSStatus CoreAudioOutput::devicePropertyListener(AudioObjectID,
    UInt32 addressCount, const AudioObjectPropertyAddress* addresses,
    void* context)
{
    auto* self = static_cast<CoreAudioOutput*>(context);
    if (!self || !addresses) return noErr;
    uint32_t telemetryBank = 0u;
    if (!self->beginTelemetryWrite(telemetryBank)) return noErr;
    for (UInt32 index = 0u; index < addressCount; ++index) {
        switch (addresses[index].mSelector) {
        case kAudioDeviceProcessorOverload:
            self->telemetry_[telemetryBank].recordProcessorOverload();
            break;
        case kAudioDevicePropertyIOStoppedAbnormally:
            self->telemetry_[telemetryBank].recordAbnormalStop();
            break;
        case kAudioDevicePropertyDeviceIsAlive:
            self->telemetry_[telemetryBank].recordDeviceAliveChange();
            break;
        case kAudioDevicePropertyDeviceHasChanged:
            self->telemetry_[telemetryBank].recordDeviceConfigurationChange();
            break;
        default:
            break;
        }
    }
    self->endTelemetryWrite(telemetryBank);
    return noErr;
}

} // namespace s3g::standalone

#endif
