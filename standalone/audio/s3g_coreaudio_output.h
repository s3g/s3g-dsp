#pragma once

#if defined(__APPLE__)

#include <AudioUnit/AudioUnit.h>
#include <CoreAudio/CoreAudio.h>

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
};

class CoreAudioOutput {
public:
    using RenderCallback = OSStatus (*)(void* context,
        AudioBufferList* output, uint32_t frames);

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

private:
    static OSStatus renderThunk(void* context,
        AudioUnitRenderActionFlags* actionFlags,
        const AudioTimeStamp* timestamp, UInt32 bus, UInt32 frames,
        AudioBufferList* output);

    AudioUnit unit_ = nullptr;
    RenderCallback render_ = nullptr;
    void* renderContext_ = nullptr;
    CoreAudioOutputConfig config_ {};
    bool running_ = false;
};

} // namespace s3g::standalone

#endif
