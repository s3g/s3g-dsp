#pragma once

#include "s3g_embedded_clap_host.h"

#include <clap/events.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace s3g::standalone {

enum class NoInputOutputMode : uint32_t {
    StereoRing = 0u,
    QuadRing = 1u,
    DirectEight = 2u,
};

inline uint32_t outputChannelsForMode(NoInputOutputMode mode)
{
    switch (mode) {
    case NoInputOutputMode::QuadRing: return 4u;
    case NoInputOutputMode::DirectEight: return 8u;
    case NoInputOutputMode::StereoRing:
    default: return 2u;
    }
}

// The standalone presents its hardware-oriented choices in 2/4/8 order,
// while the NIM CLAP parameter is published in 8/4/2 order.
inline uint32_t noInputOutputFormatForMode(NoInputOutputMode mode)
{
    switch (mode) {
    case NoInputOutputMode::DirectEight: return 0u;
    case NoInputOutputMode::QuadRing: return 1u;
    case NoInputOutputMode::StereoRing:
    default: return 2u;
    }
}

inline NoInputOutputMode outputModeForNoInputFormat(uint32_t format)
{
    switch (std::min<uint32_t>(format, 2u)) {
    case 0u: return NoInputOutputMode::DirectEight;
    case 1u: return NoInputOutputMode::QuadRing;
    case 2u:
    default: return NoInputOutputMode::StereoRing;
    }
}

struct NoInputMixerStandaloneTelemetrySnapshot {
    uint64_t gestureProcessErrorCount = 0u;
    uint64_t noInputProcessErrorCount = 0u;
    uint64_t nonFiniteOutputSampleCount = 0u;

    uint64_t totalProcessErrorCount() const
    {
        return gestureProcessErrorCount + noInputProcessErrorCount;
    }
};

class NoInputMixerStandaloneEngine {
public:
    bool create(const clap_plugin_entry_t* noInputEntry,
        const clap_plugin_entry_t* gestureEntry);
    void destroy();

    bool prepare(double sampleRate, uint32_t maximumFrames);
    void release();
    bool isPrepared() const { return prepared_; }

    void setOutputMode(NoInputOutputMode mode);
    NoInputOutputMode outputMode() const;
    // Refresh the hardware-routing cache after a change made in the embedded
    // NIM SAFETY page. Must be called from the main thread.
    bool synchronizeOutputModeFromPlugin();
    // Used by the one-time legacy standalone migration before activation.
    bool setOutputRotation(float degrees);
    void setOutputChannelOffset(NoInputOutputMode mode, uint32_t offset);
    uint32_t outputChannelOffset(NoInputOutputMode mode) const;
    uint32_t outputChannelOffset() const;
    void setAudioEnabled(bool enabled);
    bool audioEnabled() const;
    void setTempo(double bpm);
    double tempo() const;
    void setHostTicksPerSecond(double ticksPerSecond);
    double hostTicksPerSecond() const { return hostTicksPerSecond_; }
    void requestPanic();
    void setGestureFeedbackEnabled(bool enabled);
    bool gestureFeedbackEnabled() const
    {
        return gestureFeedbackEnabled_.load(std::memory_order_acquire);
    }
    void requestGestureFeedback();
    bool enqueueMidi(uint8_t status, uint8_t dataOne, uint8_t dataTwo,
        uint64_t hostTime = 0u);
    bool dequeueMidiOutput(uint8_t& status, uint8_t& dataOne,
        uint8_t& dataTwo);

    // Returns [0, frames - 1] for an event in or before this block, and
    // frames when the timestamp belongs to a future block. A zero timestamp
    // means "as soon as possible" and maps to frame zero.
    static uint32_t midiFrameOffset(uint64_t eventHostTime,
        uint64_t blockHostTime, double hostTicksPerSecond,
        double sampleRate, uint32_t frames);

    // Output is planar. Extra hardware channels are always zeroed.
    bool render(float* const* output, uint32_t outputChannels,
        uint32_t frames, uint64_t blockHostTime = 0u);
    NoInputMixerStandaloneTelemetrySnapshot telemetry() const;
    void resetTelemetry();

    EmbeddedClapPlugin& noInputPlugin() { return noInput_; }
    EmbeddedClapPlugin& gesturePlugin() { return gesture_; }
    const EmbeddedClapPlugin& noInputPlugin() const { return noInput_; }
    const EmbeddedClapPlugin& gesturePlugin() const { return gesture_; }

    double sampleRate() const { return sampleRate_; }
    uint32_t maximumFrames() const { return maximumFrames_; }
    float outputPeak(uint32_t channel) const;
    uint64_t midiInputDropCount() const
    {
        return midiInputDropCount_.load(std::memory_order_relaxed);
    }
    void resetMidiInputDropCount()
    {
        midiInputDropCount_.store(0u, std::memory_order_relaxed);
    }

private:
    struct MidiMessage {
        uint8_t status = 0u;
        uint8_t dataOne = 0u;
        uint8_t dataTwo = 0u;
        uint64_t hostTime = 0u;
        uint64_t sequence = 0u;
    };

    struct BlockMidiEvent {
        clap_event_midi_t event {};
        uint64_t sequence = 0u;
    };

    struct GestureFeedbackState {
        bool recording = false;
        bool playing = false;
        bool hasLastLoop = false;
        bool hasAnyLoop = false;

        bool operator==(const GestureFeedbackState& other) const
        {
            return recording == other.recording
                && playing == other.playing
                && hasLastLoop == other.hasLastLoop
                && hasAnyLoop == other.hasAnyLoop;
        }
    };

    static constexpr uint32_t kSourceChannels = 8u;
    static constexpr uint32_t kMidiQueueCapacity = 2048u;
    static constexpr uint32_t kMidiOutputQueueCapacity = 4096u;
    static constexpr uint32_t kMaximumEventsPerBlock = 512u;
    static constexpr uint32_t kMaximumGestureEventsPerBlock = 2048u;
    static constexpr uint32_t kNoPendingOutputFormat = 3u;
    static constexpr clap_id kOutputFormatParamId = 61u;
    static constexpr clap_id kOutputRotationParamId = 62u;

    bool dequeueMidi(MidiMessage& message);
    bool deferMidi(const MidiMessage& message);
    void appendMidiEvent(const MidiMessage& message, uint32_t frameOffset,
        uint32_t& eventCount);
    void prepareMidiEvents(uint32_t frames, uint64_t blockHostTime);
    bool enqueueMidiOutput(uint8_t status, uint8_t dataOne,
        uint8_t dataTwo);
    void serviceGestureFeedback();
    void clearOutput(float* const* output, uint32_t channels,
        uint32_t frames) const;

    EmbeddedClapPlugin noInput_;
    EmbeddedClapPlugin gesture_;

    std::vector<float> sourceStorage_;
    std::array<float*, kSourceChannels> sourcePointers_ {};
    std::array<BlockMidiEvent, kMaximumEventsPerBlock> midiEvents_ {};
    uint32_t midiEventCount_ = 0u;
    clap_input_events_t inputEvents_ {};
    std::array<clap_event_midi_t,
        kMaximumGestureEventsPerBlock> gestureMidiEvents_ {};
    uint32_t gestureMidiEventCount_ = 0u;
    clap_event_param_value_t outputFormatEvent_ {};
    bool outputFormatEventActive_ = false;
    clap_input_events_t noInputInputEvents_ {};
    clap_output_events_t gestureOutputEvents_ {};
    clap_output_events_t noInputOutputEvents_ {};
    std::array<MidiMessage, kMidiQueueCapacity> midiQueue_ {};
    std::atomic<uint32_t> midiWrite_ { 0u };
    std::atomic<uint32_t> midiRead_ { 0u };
    std::array<MidiMessage, kMidiQueueCapacity> pendingMidi_ {};
    uint32_t pendingMidiCount_ = 0u;
    std::atomic<uint64_t> midiSequence_ { 1u };
    std::atomic<uint64_t> midiInputDropCount_ { 0u };
    std::atomic<uint64_t> gestureProcessErrorCount_ { 0u };
    std::atomic<uint64_t> noInputProcessErrorCount_ { 0u };
    std::atomic<uint64_t> nonFiniteOutputSampleCount_ { 0u };
    std::array<MidiMessage, kMidiOutputQueueCapacity> midiOutputQueue_ {};
    std::atomic<uint32_t> midiOutputWrite_ { 0u };
    std::atomic<uint32_t> midiOutputRead_ { 0u };

    std::atomic<uint32_t> outputMode_ {
        static_cast<uint32_t>(NoInputOutputMode::StereoRing) };
    std::atomic<uint32_t> pendingOutputFormat_ { kNoPendingOutputFormat };
    std::atomic<bool> gestureFeedbackEnabled_ { false };
    std::atomic<bool> gestureFeedbackRequested_ { false };
    GestureFeedbackState gestureFeedbackState_ {};
    bool gestureFeedbackStateValid_ = false;
    uint64_t gestureFeedbackNextFrame_ = 0u;
    std::array<std::atomic<uint32_t>, 3u> outputChannelOffsets_ {};
    std::atomic<bool> audioEnabled_ { false };
    std::atomic<bool> panicRequested_ { false };
    std::atomic<double> tempoBpm_ { 120.0 };
    std::array<std::atomic<float>, kSourceChannels> outputPeaks_ {};
    double sampleRate_ = 48000.0;
    double hostTicksPerSecond_ = 1.0e9;
    uint32_t maximumFrames_ = 0u;
    int64_t steadyTime_ = 0;
    float monitorGain_ = 0.0f;
    bool prepared_ = false;
};

} // namespace s3g::standalone
