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
    double sampleRate = 48000.0;
    // Core Audio and CoreMIDI timestamps share this host-clock frequency.
    double hostTicksPerSecond = 1.0e9;
};

struct RealtimeCallbackTelemetrySnapshot {
    uint64_t callbackCount = 0u;
    uint64_t deadlineMissCount = 0u;
    uint64_t oversizedCallbackCount = 0u;
    uint64_t processorOverloadCount = 0u;
    uint64_t callbackErrorCount = 0u;
    double smoothedLoad = 0.0;
    double peakLoad = 0.0;
    double lastCallbackMilliseconds = 0.0;
    double maximumCallbackMilliseconds = 0.0;
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
    void recordProcessorOverload();
    RealtimeCallbackTelemetrySnapshot snapshot() const;

private:
    static void updateMaximum(std::atomic<uint64_t>& destination,
        uint64_t value);

    std::atomic<uint64_t> callbackCount_ { 0u };
    std::atomic<uint64_t> deadlineMissCount_ { 0u };
    std::atomic<uint64_t> oversizedCallbackCount_ { 0u };
    std::atomic<uint64_t> processorOverloadCount_ { 0u };
    std::atomic<uint64_t> callbackErrorCount_ { 0u };
    std::atomic<uint64_t> smoothedLoadPartsPerMillion_ { 0u };
    std::atomic<uint64_t> peakLoadPartsPerMillion_ { 0u };
    std::atomic<uint64_t> lastCallbackNanoseconds_ { 0u };
    std::atomic<uint64_t> maximumCallbackNanoseconds_ { 0u };
};

class CoreAudioOutput {
public:
    using RenderCallback = OSStatus (*)(void* context,
        AudioBufferList* output, uint32_t frames, uint64_t hostTime);

    CoreAudioOutput() = default;
    ~CoreAudioOutput();
    CoreAudioOutput(const CoreAudioOutput&) = delete;
    CoreAudioOutput& operator=(const CoreAudioOutput&) = delete;

    static std::vector<CoreAudioDeviceInfo> enumerateDevices();
    static AudioDeviceID defaultOutputDevice();

    // Opens and initializes an AUHAL output without starting the render thread.
    bool open(AudioDeviceID device, uint32_t renderChannels,
        RenderCallback render, void* context, std::string* error = nullptr);
    bool start(std::string* error = nullptr);
    void stop();
    void close();

    bool isOpen() const { return unit_ != nullptr; }
    bool isRunning() const { return running_; }
    const CoreAudioOutputConfig& config() const { return config_; }
    RealtimeCallbackTelemetrySnapshot telemetry() const
    {
        return telemetry_.snapshot();
    }

private:
    static OSStatus renderThunk(void* context,
        AudioUnitRenderActionFlags* actionFlags,
        const AudioTimeStamp* timestamp, UInt32 bus, UInt32 frames,
        AudioBufferList* output);
    static OSStatus devicePropertyListener(AudioObjectID object,
        UInt32 addressCount, const AudioObjectPropertyAddress* addresses,
        void* context);

    AudioUnit unit_ = nullptr;
    RenderCallback render_ = nullptr;
    void* renderContext_ = nullptr;
    CoreAudioOutputConfig config_ {};
    RealtimeCallbackTelemetry telemetry_ {};
    uint32_t hostTimeNumerator_ = 1u;
    uint32_t hostTimeDenominator_ = 1u;
    bool overloadListenerInstalled_ = false;
    bool running_ = false;
};

} // namespace s3g::standalone

#endif
