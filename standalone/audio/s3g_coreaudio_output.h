#pragma once

#if defined(__APPLE__)

#include <AudioUnit/AudioUnit.h>
#include <CoreAudio/CoreAudio.h>

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace s3g::standalone {

struct CoreAudioDeviceInfo {
    AudioDeviceID id = kAudioObjectUnknown;
    std::string uid;
    std::string name;
    uint32_t outputChannels = 0u;
    double sampleRate = 48000.0;
};

struct CoreAudioOutputConfig {
    AudioDeviceID device = kAudioObjectUnknown;
    uint32_t hardwareChannels = 0u;
    uint32_t renderChannels = 0u;
    uint32_t maximumFrames = 0u;
    uint32_t deviceBufferFrames = 0u;
    uint32_t maximumVariableBufferFrames = 0u;
    uint32_t deviceLatencyFrames = 0u;
    uint32_t deviceSafetyOffsetFrames = 0u;
    double sampleRate = 48000.0;
    // Core Audio and CoreMIDI timestamps share this host-clock frequency.
    double hostTicksPerSecond = 1.0e9;
    bool usesVariableBufferFrames = false;
    bool processorOverloadSignalAvailable = false;
    bool abnormalStopSignalAvailable = false;
    bool renderErrorSignalAvailable = false;
    bool deviceAliveSignalAvailable = false;
    bool deviceConfigurationSignalAvailable = false;
};

// Raw AUHAL timing delivered with a render callback. Clients that fan one
// musical clock out to other timestamped systems must validate these flags
// before rendering or publishing the host timestamp; telemetry is recorded
// after the callback and is therefore intentionally not a preflight signal.
struct CoreAudioRenderTiming {
    uint64_t hostTime = 0u;
    double sampleTime = 0.0;
    bool hostTimeValid = false;
    bool sampleTimeValid = false;
};

struct RealtimeCallbackTelemetrySnapshot {
    uint64_t callbackCount = 0u;
    uint64_t renderedFrameCount = 0u;
    // A render overrun is a callback whose own execution exceeded the audio
    // period. Keep deadlineMissCount as the source-compatible field name.
    uint64_t deadlineMissCount = 0u;
    uint64_t lateCallbackCount = 0u;
    uint64_t timestampDiscontinuityCount = 0u;
    uint64_t timestampUnavailableCount = 0u;
    uint64_t sampleTimeDiscontinuityCount = 0u;
    uint64_t sampleTimeUnavailableCount = 0u;
    uint64_t oversizedCallbackCount = 0u;
    uint64_t processorOverloadCount = 0u;
    uint64_t abnormalStopCount = 0u;
    uint64_t deviceAliveChangeCount = 0u;
    uint64_t deviceConfigurationChangeCount = 0u;
    uint64_t callbackErrorCount = 0u;
    uint64_t renderActionErrorCount = 0u;
    double smoothedLoad = 0.0;
    double peakLoad = 0.0;
    double lastLoad = 0.0;
    double lastCallbackMilliseconds = 0.0;
    double maximumCallbackMilliseconds = 0.0;
    double maximumLatenessMilliseconds = 0.0;
    double maximumTimestampErrorMilliseconds = 0.0;
    double maximumSampleTimeErrorFrames = 0.0;
};

// Lock-free counters shared by the Core Audio render thread and the UI.
// recordCallbackNanoseconds() is public so its deadline math can be tested
// without opening an audio device.
class RealtimeCallbackTelemetry {
public:
    void reset();
    void recordCallbackNanoseconds(uint64_t durationNanoseconds,
        uint32_t frames, double sampleRate, uint32_t maximumFrames,
        bool callbackFailed = false);
    // Adds stream-cadence diagnostics to the duration counters above.
    // Host timestamps are raw Core Audio ticks. The callback start comes from
    // mach_absolute_time(); audioHostTime and audioSampleTime are the timeline
    // positions supplied by AUHAL for the rendered block.
    void recordCallbackTiming(uint64_t durationNanoseconds,
        uint32_t frames, double sampleRate, uint32_t maximumFrames,
        uint64_t callbackStartHostTime, uint64_t audioHostTime,
        double audioSampleTime, double hostTicksPerSecond,
        bool audioHostTimeValid, bool audioSampleTimeValid,
        bool callbackFailed = false, bool renderActionFailed = false);
    void clearTimingSequence();
    void recordProcessorOverload();
    void recordAbnormalStop();
    void recordDeviceAliveChange();
    void recordDeviceConfigurationChange();
    void recordRenderActionError();
    RealtimeCallbackTelemetrySnapshot snapshot() const;

private:
    static void updateMaximum(std::atomic<uint64_t>& destination,
        uint64_t value);

    std::atomic<uint64_t> callbackCount_ { 0u };
    std::atomic<uint64_t> renderedFrameCount_ { 0u };
    std::atomic<uint64_t> deadlineMissCount_ { 0u };
    std::atomic<uint64_t> lateCallbackCount_ { 0u };
    std::atomic<uint64_t> timestampDiscontinuityCount_ { 0u };
    std::atomic<uint64_t> timestampUnavailableCount_ { 0u };
    std::atomic<uint64_t> sampleTimeDiscontinuityCount_ { 0u };
    std::atomic<uint64_t> sampleTimeUnavailableCount_ { 0u };
    std::atomic<uint64_t> oversizedCallbackCount_ { 0u };
    std::atomic<uint64_t> processorOverloadCount_ { 0u };
    std::atomic<uint64_t> abnormalStopCount_ { 0u };
    std::atomic<uint64_t> deviceAliveChangeCount_ { 0u };
    std::atomic<uint64_t> deviceConfigurationChangeCount_ { 0u };
    std::atomic<uint64_t> callbackErrorCount_ { 0u };
    std::atomic<uint64_t> renderActionErrorCount_ { 0u };
    std::atomic<uint64_t> smoothedLoadPartsPerMillion_ { 0u };
    std::atomic<uint64_t> peakLoadPartsPerMillion_ { 0u };
    std::atomic<uint64_t> lastLoadPartsPerMillion_ { 0u };
    std::atomic<uint64_t> lastCallbackNanoseconds_ { 0u };
    std::atomic<uint64_t> maximumCallbackNanoseconds_ { 0u };
    std::atomic<uint64_t> maximumLatenessNanoseconds_ { 0u };
    std::atomic<uint64_t> maximumTimestampErrorNanoseconds_ { 0u };
    std::atomic<uint64_t> maximumSampleTimeErrorMicroframes_ { 0u };
    std::atomic<uint64_t> previousCallbackStartHostTime_ { 0u };
    std::atomic<uint64_t> previousAudioHostTime_ { 0u };
    std::atomic<uint64_t> previousAudioSampleTimeBits_ { 0u };
    std::atomic<bool> previousAudioSampleTimeValid_ { false };
    std::atomic<uint32_t> previousFrames_ { 0u };
};

class CoreAudioOutput {
public:
    using RenderCallback = OSStatus (*)(void* context,
        AudioBufferList* output, uint32_t frames, uint64_t hostTime);
    using TimedRenderCallback = OSStatus (*)(void* context,
        AudioBufferList* output, uint32_t frames,
        const CoreAudioRenderTiming& timing);

    CoreAudioOutput() = default;
    ~CoreAudioOutput();
    CoreAudioOutput(const CoreAudioOutput&) = delete;
    CoreAudioOutput& operator=(const CoreAudioOutput&) = delete;

    static std::vector<CoreAudioDeviceInfo> enumerateDevices();
    static AudioDeviceID defaultOutputDevice();

    // Opens and initializes an AUHAL output without starting the render thread.
    // When timedRender is supplied it receives raw AUHAL validity metadata and
    // takes precedence over render; render may then be null.
    bool open(AudioDeviceID device, uint32_t renderChannels,
        RenderCallback render, void* context, std::string* error = nullptr,
        TimedRenderCallback timedRender = nullptr);
    bool start(std::string* error = nullptr);
    void stop();
    void close();

    bool isOpen() const { return unit_ != nullptr; }
    bool isRunning() const { return running_; }
    void setTelemetryEnabled(bool enabled);
    void resetTelemetry();
    bool telemetryEnabled() const
    {
        return telemetryEnabled_.load(std::memory_order_acquire);
    }
    const CoreAudioOutputConfig& config() const { return config_; }
    RealtimeCallbackTelemetrySnapshot telemetry() const;

private:
    static OSStatus renderThunk(void* context,
        AudioUnitRenderActionFlags* actionFlags,
        const AudioTimeStamp* timestamp, UInt32 bus, UInt32 frames,
        AudioBufferList* output);
    static void renderErrorListener(void* context, AudioUnit unit,
        AudioUnitPropertyID property, AudioUnitScope scope,
        AudioUnitElement element);
    static OSStatus devicePropertyListener(AudioObjectID object,
        UInt32 addressCount, const AudioObjectPropertyAddress* addresses,
        void* context);
    void replaceTelemetryBank(bool enabled);
    bool beginTelemetryWrite(uint32_t& bank);
    void endTelemetryWrite(uint32_t bank);

    AudioUnit unit_ = nullptr;
    RenderCallback render_ = nullptr;
    TimedRenderCallback timedRender_ = nullptr;
    void* renderContext_ = nullptr;
    CoreAudioOutputConfig config_ {};
    // Reset swaps to a clean inactive bank. A callback already in flight may
    // finish writing the old bank without leaking pre-reset data into the new
    // observation window.
    RealtimeCallbackTelemetry telemetry_[2] {};
    std::atomic<uint32_t> telemetryBank_ { 0u };
    std::atomic<uint32_t> telemetryWriters_[2] {};
    uint32_t hostTimeNumerator_ = 1u;
    uint32_t hostTimeDenominator_ = 1u;
    bool overloadListenerInstalled_ = false;
    bool abnormalStopListenerInstalled_ = false;
    bool deviceAliveListenerInstalled_ = false;
    bool deviceConfigurationListenerInstalled_ = false;
    bool renderErrorListenerInstalled_ = false;
    bool running_ = false;
    std::atomic<bool> telemetryEnabled_ { false };
};

} // namespace s3g::standalone

#endif
