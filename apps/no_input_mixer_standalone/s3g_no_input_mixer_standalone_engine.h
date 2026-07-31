#pragma once

#include "s3g_embedded_clap_host.h"

#include <clap/events.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace s3g::standalone {

enum class NoInputOutputMode : uint32_t {
    StereoAutogain = 0u,
    QuadAutogain = 1u,
    DirectEight = 2u,
};

inline uint32_t outputChannelsForMode(NoInputOutputMode mode)
{
    switch (mode) {
    case NoInputOutputMode::QuadAutogain: return 4u;
    case NoInputOutputMode::DirectEight: return 8u;
    case NoInputOutputMode::StereoAutogain:
    default: return 2u;
    }
}

class NoInputMixerStandaloneEngine {
public:
    bool create(const clap_plugin_entry_t* noInputEntry,
        const clap_plugin_entry_t* gestureEntry,
        const clap_plugin_entry_t* stereoEntry,
        const clap_plugin_entry_t* quadEntry);
    void destroy();

    bool prepare(double sampleRate, uint32_t maximumFrames);
    void release();
    bool isPrepared() const { return prepared_; }

    void setOutputMode(NoInputOutputMode mode);
    NoInputOutputMode outputMode() const;
    void setOutputChannelOffset(NoInputOutputMode mode, uint32_t offset);
    uint32_t outputChannelOffset(NoInputOutputMode mode) const;
    uint32_t outputChannelOffset() const;
    void setAudioEnabled(bool enabled);
    bool audioEnabled() const;
    void setTempo(double bpm);
    double tempo() const;
    void requestPanic();
    bool enqueueMidi(uint8_t status, uint8_t dataOne, uint8_t dataTwo);
    bool dequeueMidiOutput(uint8_t& status, uint8_t& dataOne,
        uint8_t& dataTwo);

    // Output is planar. Extra hardware channels are always zeroed.
    void render(float* const* output, uint32_t outputChannels,
        uint32_t frames);

    EmbeddedClapPlugin& noInputPlugin() { return noInput_; }
    EmbeddedClapPlugin& gesturePlugin() { return gesture_; }
    EmbeddedClapPlugin& stereoPlugin() { return stereo_; }
    EmbeddedClapPlugin& quadPlugin() { return quad_; }
    const EmbeddedClapPlugin& noInputPlugin() const { return noInput_; }
    const EmbeddedClapPlugin& gesturePlugin() const { return gesture_; }
    const EmbeddedClapPlugin& stereoPlugin() const { return stereo_; }
    const EmbeddedClapPlugin& quadPlugin() const { return quad_; }

    double sampleRate() const { return sampleRate_; }
    uint32_t maximumFrames() const { return maximumFrames_; }
    float outputPeak(uint32_t channel) const;

private:
    struct MidiMessage {
        uint8_t status = 0u;
        uint8_t dataOne = 0u;
        uint8_t dataTwo = 0u;
    };

    static constexpr uint32_t kSourceChannels = 8u;
    static constexpr uint32_t kMidiQueueCapacity = 2048u;
    static constexpr uint32_t kMidiOutputQueueCapacity = 4096u;
    static constexpr uint32_t kMaximumEventsPerBlock = 512u;
    static constexpr uint32_t kMaximumGestureEventsPerBlock = 2048u;

    bool dequeueMidi(MidiMessage& message);
    bool enqueueMidiOutput(uint8_t status, uint8_t dataOne,
        uint8_t dataTwo);
    void clearOutput(float* const* output, uint32_t channels,
        uint32_t frames) const;

    EmbeddedClapPlugin noInput_;
    EmbeddedClapPlugin gesture_;
    EmbeddedClapPlugin stereo_;
    EmbeddedClapPlugin quad_;

    std::vector<float> sourceStorage_;
    std::vector<float> stereoStorage_;
    std::vector<float> quadStorage_;
    std::array<float*, kSourceChannels> sourcePointers_ {};
    std::array<float*, 2u> stereoPointers_ {};
    std::array<float*, 4u> quadPointers_ {};
    std::array<clap_event_midi_t, kMaximumEventsPerBlock> midiEvents_ {};
    clap_input_events_t inputEvents_ {};
    std::array<clap_event_midi_t,
        kMaximumGestureEventsPerBlock> gestureMidiEvents_ {};
    uint32_t gestureMidiEventCount_ = 0u;
    clap_input_events_t gestureInputEvents_ {};
    clap_output_events_t gestureOutputEvents_ {};
    clap_output_events_t noInputOutputEvents_ {};
    std::array<MidiMessage, kMidiQueueCapacity> midiQueue_ {};
    std::atomic<uint32_t> midiWrite_ { 0u };
    std::atomic<uint32_t> midiRead_ { 0u };
    std::array<MidiMessage, kMidiOutputQueueCapacity> midiOutputQueue_ {};
    std::atomic<uint32_t> midiOutputWrite_ { 0u };
    std::atomic<uint32_t> midiOutputRead_ { 0u };

    std::atomic<uint32_t> outputMode_ {
        static_cast<uint32_t>(NoInputOutputMode::StereoAutogain) };
    std::array<std::atomic<uint32_t>, 3u> outputChannelOffsets_ {};
    std::atomic<bool> audioEnabled_ { false };
    std::atomic<bool> panicRequested_ { false };
    std::atomic<double> tempoBpm_ { 120.0 };
    std::array<std::atomic<float>, kSourceChannels> outputPeaks_ {};
    double sampleRate_ = 48000.0;
    uint32_t maximumFrames_ = 0u;
    int64_t steadyTime_ = 0;
    float monitorGain_ = 0.0f;
    bool prepared_ = false;
};

} // namespace s3g::standalone
