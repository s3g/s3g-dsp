#include "s3g_no_input_mixer_standalone_engine.h"
#include "s3g_no_input_mixer.h"
#include "s3g_nim_gesture_midi.h"

#include <clap/ext/params.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace s3g::standalone {
namespace {

bool validProcessStatus(clap_process_status status) noexcept
{
    return status >= CLAP_PROCESS_CONTINUE && status <= CLAP_PROCESS_SLEEP;
}

} // namespace

bool NoInputMixerStandaloneEngine::create(
    const clap_plugin_entry_t* noInputEntry,
    const clap_plugin_entry_t* gestureEntry,
    const clap_plugin_entry_t* stereoEntry,
    const clap_plugin_entry_t* quadEntry)
{
    destroy();
    if (!noInput_.create(noInputEntry,
            "org.s3g.s3g-dsp.no-input-mixer-8ch",
            "s3g No Input Mixer Standalone")
        || !gesture_.create(gestureEntry,
            "org.s3g.s3g-dsp.nim-gesture",
            "s3g No Input Mixer Standalone")
        || !stereo_.create(stereoEntry,
            "org.s3g.s3g-dsp.mc-to-stereo-autogain",
            "s3g No Input Mixer Standalone")
        || !quad_.create(quadEntry,
            "org.s3g.s3g-dsp.mc-to-quad-autogain",
            "s3g No Input Mixer Standalone")) {
        destroy();
        return false;
    }

    inputEvents_.ctx = this;
    inputEvents_.size = [](const clap_input_events_t* events) -> uint32_t {
        const auto* self = static_cast<const NoInputMixerStandaloneEngine*>(
            events ? events->ctx : nullptr);
        return self ? self->midiEventCount_ : 0u;
    };
    inputEvents_.get = [](const clap_input_events_t* events,
        uint32_t index) -> const clap_event_header_t* {
        const auto* self = static_cast<const NoInputMixerStandaloneEngine*>(
            events ? events->ctx : nullptr);
        if (!self || index >= self->midiEventCount_) return nullptr;
        return &self->midiEvents_[index].event.header;
    };
    gestureInputEvents_.ctx = this;
    gestureInputEvents_.size = [](const clap_input_events_t* events) {
        const auto* self = static_cast<const NoInputMixerStandaloneEngine*>(
            events ? events->ctx : nullptr);
        return self ? self->gestureMidiEventCount_ : 0u;
    };
    gestureInputEvents_.get = [](const clap_input_events_t* events,
        uint32_t index) -> const clap_event_header_t* {
        const auto* self = static_cast<const NoInputMixerStandaloneEngine*>(
            events ? events->ctx : nullptr);
        return self && index < self->gestureMidiEventCount_
            ? &self->gestureMidiEvents_[index].header : nullptr;
    };
    gestureOutputEvents_.ctx = this;
    gestureOutputEvents_.try_push = [](const clap_output_events_t* events,
        const clap_event_header_t* event) {
        auto* self = static_cast<NoInputMixerStandaloneEngine*>(
            events ? events->ctx : nullptr);
        if (!self || !event) return false;
        if (event->space_id != CLAP_CORE_EVENT_SPACE_ID
            || event->type != CLAP_EVENT_MIDI
            || event->size < sizeof(clap_event_midi_t)) return true;
        if (self->gestureMidiEventCount_
            >= kMaximumGestureEventsPerBlock) return false;
        self->gestureMidiEvents_[self->gestureMidiEventCount_++] =
            *reinterpret_cast<const clap_event_midi_t*>(event);
        return true;
    };
    noInputOutputEvents_.ctx = this;
    noInputOutputEvents_.try_push = [](const clap_output_events_t* events,
        const clap_event_header_t* event) {
        auto* self = static_cast<NoInputMixerStandaloneEngine*>(
            events ? events->ctx : nullptr);
        if (!self || !event) return false;
        if (event->space_id != CLAP_CORE_EVENT_SPACE_ID
            || event->type != CLAP_EVENT_MIDI
            || event->size < sizeof(clap_event_midi_t)) return true;
        const auto* midi = reinterpret_cast<const clap_event_midi_t*>(event);
        const uint8_t status = midi->data[0];
        const uint8_t command = status & 0xf0u;
        const uint8_t channel = status & 0x0fu;
        const bool e16Feedback = command == 0xb0u && channel == 15u;
        const bool gridFeedback = command
                == s3g::kNoInputMatrixFeedbackCommand
            && channel < s3g::kNoInputMatrixGridChannels;
        if (!e16Feedback && !gridFeedback) return true;
        return self->enqueueMidiOutput(status, midi->data[1], midi->data[2]);
    };
    return true;
}

void NoInputMixerStandaloneEngine::destroy()
{
    release();
    quad_.destroy();
    stereo_.destroy();
    gesture_.destroy();
    noInput_.destroy();
}

bool NoInputMixerStandaloneEngine::prepare(double sampleRate,
    uint32_t maximumFrames)
{
    release();
    if (!noInput_.isCreated() || !gesture_.isCreated()
        || !stereo_.isCreated() || !quad_.isCreated()
        || sampleRate <= 0.0 || maximumFrames == 0u) return false;
    sampleRate_ = sampleRate;
    maximumFrames_ = maximumFrames;
    sourceStorage_.assign(static_cast<size_t>(kSourceChannels)
        * maximumFrames_, 0.0f);
    stereoStorage_.assign(static_cast<size_t>(2u) * maximumFrames_, 0.0f);
    quadStorage_.assign(static_cast<size_t>(4u) * maximumFrames_, 0.0f);
    for (uint32_t channel = 0u; channel < kSourceChannels; ++channel)
        sourcePointers_[channel] = sourceStorage_.data()
            + static_cast<size_t>(channel) * maximumFrames_;
    for (uint32_t channel = 0u; channel < 2u; ++channel)
        stereoPointers_[channel] = stereoStorage_.data()
            + static_cast<size_t>(channel) * maximumFrames_;
    for (uint32_t channel = 0u; channel < 4u; ++channel)
        quadPointers_[channel] = quadStorage_.data()
            + static_cast<size_t>(channel) * maximumFrames_;
    const uint32_t minimumFrames = 1u;
    if (!gesture_.activate(sampleRate_, minimumFrames, maximumFrames_)
        || !noInput_.activate(sampleRate_, minimumFrames, maximumFrames_)
        || !stereo_.activate(sampleRate_, minimumFrames, maximumFrames_)
        || !quad_.activate(sampleRate_, minimumFrames, maximumFrames_)) {
        release();
        return false;
    }
    steadyTime_ = 0;
    monitorGain_ = 0.0f;
    resetTelemetry();
    resetMidiInputDropCount();
    for (auto& peak : outputPeaks_) peak.store(0.0f,
        std::memory_order_relaxed);
    gestureFeedbackStateValid_ = false;
    gestureFeedbackNextFrame_ = 0u;
    if (gestureFeedbackEnabled()) requestGestureFeedback();
    prepared_ = true;
    return true;
}

void NoInputMixerStandaloneEngine::release()
{
    prepared_ = false;
    quad_.deactivate();
    stereo_.deactivate();
    noInput_.deactivate();
    gesture_.deactivate();
    sourceStorage_.clear();
    stereoStorage_.clear();
    quadStorage_.clear();
    sourcePointers_.fill(nullptr);
    stereoPointers_.fill(nullptr);
    quadPointers_.fill(nullptr);
    maximumFrames_ = 0u;
    midiEventCount_ = 0u;
    gestureMidiEventCount_ = 0u;
    pendingMidiCount_ = 0u;
    gestureFeedbackStateValid_ = false;
    gestureFeedbackNextFrame_ = 0u;
}

void NoInputMixerStandaloneEngine::setOutputMode(NoInputOutputMode mode)
{
    outputMode_.store(static_cast<uint32_t>(mode),
        std::memory_order_release);
}

NoInputOutputMode NoInputMixerStandaloneEngine::outputMode() const
{
    return static_cast<NoInputOutputMode>(std::min<uint32_t>(
        outputMode_.load(std::memory_order_acquire), 2u));
}

void NoInputMixerStandaloneEngine::setOutputChannelOffset(
    NoInputOutputMode mode, uint32_t offset)
{
    const uint32_t index = std::min<uint32_t>(
        static_cast<uint32_t>(mode), 2u);
    const auto normalizedMode = static_cast<NoInputOutputMode>(index);
    const uint32_t width = outputChannelsForMode(normalizedMode);
    outputChannelOffsets_[index].store((offset / width) * width,
        std::memory_order_release);
}

uint32_t NoInputMixerStandaloneEngine::outputChannelOffset(
    NoInputOutputMode mode) const
{
    const uint32_t index = std::min<uint32_t>(
        static_cast<uint32_t>(mode), 2u);
    return outputChannelOffsets_[index].load(std::memory_order_acquire);
}

uint32_t NoInputMixerStandaloneEngine::outputChannelOffset() const
{
    return outputChannelOffset(outputMode());
}

void NoInputMixerStandaloneEngine::setAudioEnabled(bool enabled)
{
    audioEnabled_.store(enabled, std::memory_order_release);
}

bool NoInputMixerStandaloneEngine::audioEnabled() const
{
    return audioEnabled_.load(std::memory_order_acquire);
}

void NoInputMixerStandaloneEngine::setTempo(double bpm)
{
    tempoBpm_.store(std::clamp(std::isfinite(bpm) ? bpm : 120.0,
        20.0, 300.0), std::memory_order_release);
}

double NoInputMixerStandaloneEngine::tempo() const
{
    return tempoBpm_.load(std::memory_order_acquire);
}

void NoInputMixerStandaloneEngine::setHostTicksPerSecond(
    double ticksPerSecond)
{
    if (std::isfinite(ticksPerSecond) && ticksPerSecond > 0.0)
        hostTicksPerSecond_ = ticksPerSecond;
}

void NoInputMixerStandaloneEngine::requestPanic()
{
    panicRequested_.store(true, std::memory_order_release);
}

void NoInputMixerStandaloneEngine::setGestureFeedbackEnabled(bool enabled)
{
    const bool wasEnabled = gestureFeedbackEnabled_.exchange(
        enabled, std::memory_order_acq_rel);
    if (enabled && !wasEnabled) requestGestureFeedback();
}

void NoInputMixerStandaloneEngine::requestGestureFeedback()
{
    gestureFeedbackRequested_.store(true, std::memory_order_release);
}

bool NoInputMixerStandaloneEngine::enqueueMidi(uint8_t status,
    uint8_t dataOne, uint8_t dataTwo, uint64_t hostTime)
{
    const uint32_t write = midiWrite_.load(std::memory_order_relaxed);
    const uint32_t next = (write + 1u) % kMidiQueueCapacity;
    if (next == midiRead_.load(std::memory_order_acquire)) {
        midiInputDropCount_.fetch_add(1u, std::memory_order_relaxed);
        return false;
    }
    midiQueue_[write] = { status, dataOne, dataTwo, hostTime,
        midiSequence_.fetch_add(1u, std::memory_order_relaxed) };
    midiWrite_.store(next, std::memory_order_release);
    return true;
}

bool NoInputMixerStandaloneEngine::deferMidi(const MidiMessage& message)
{
    if (pendingMidiCount_ >= pendingMidi_.size()) {
        midiInputDropCount_.fetch_add(1u, std::memory_order_relaxed);
        return false;
    }
    pendingMidi_[pendingMidiCount_++] = message;
    return true;
}

uint32_t NoInputMixerStandaloneEngine::midiFrameOffset(
    uint64_t eventHostTime, uint64_t blockHostTime,
    double hostTicksPerSecond, double sampleRate, uint32_t frames)
{
    if (frames == 0u) return 0u;
    if (eventHostTime == 0u || blockHostTime == 0u
        || eventHostTime <= blockHostTime
        || !(hostTicksPerSecond > 0.0) || !(sampleRate > 0.0)) return 0u;
    const long double deltaTicks = static_cast<long double>(eventHostTime
        - blockHostTime);
    const long double frame = deltaTicks * sampleRate / hostTicksPerSecond;
    if (frame >= frames) return frames;
    return static_cast<uint32_t>(frame);
}

void NoInputMixerStandaloneEngine::appendMidiEvent(
    const MidiMessage& message, uint32_t frameOffset, uint32_t& eventCount)
{
    if (eventCount >= midiEvents_.size()) return;
    auto& blockEvent = midiEvents_[eventCount++];
    blockEvent.event = {};
    blockEvent.event.header.size = sizeof(blockEvent.event);
    blockEvent.event.header.time = frameOffset;
    blockEvent.event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    blockEvent.event.header.type = CLAP_EVENT_MIDI;
    blockEvent.event.port_index = 0u;
    blockEvent.event.data[0] = message.status;
    blockEvent.event.data[1] = message.dataOne;
    blockEvent.event.data[2] = message.dataTwo;
    blockEvent.sequence = message.sequence;
}

void NoInputMixerStandaloneEngine::prepareMidiEvents(uint32_t frames,
    uint64_t blockHostTime)
{
    uint32_t eventCount = 0u;
    if (panicRequested_.exchange(false, std::memory_order_acq_rel)) {
        appendMidiEvent({ 0x9fu, 123u, 127u, 0u, 0u }, 0u,
            eventCount);
    }

    // Revisit timestamped events retained for a future callback. Compact the
    // fixed-capacity array in place; events which exceed this block's CLAP
    // budget remain pending and become frame-zero events on the next block.
    uint32_t retainedCount = 0u;
    for (uint32_t index = 0u; index < pendingMidiCount_; ++index) {
        MidiMessage message = pendingMidi_[index];
        const uint32_t offset = midiFrameOffset(message.hostTime,
            blockHostTime, hostTicksPerSecond_, sampleRate_, frames);
        if (offset < frames && eventCount < midiEvents_.size()) {
            appendMidiEvent(message, offset, eventCount);
        } else {
            if (offset < frames) message.hostTime = 0u;
            pendingMidi_[retainedCount++] = message;
        }
    }
    pendingMidiCount_ = retainedCount;

    MidiMessage message;
    while (dequeueMidi(message)) {
        const uint32_t offset = midiFrameOffset(message.hostTime,
            blockHostTime, hostTicksPerSecond_, sampleRate_, frames);
        if (offset < frames && eventCount < midiEvents_.size()) {
            appendMidiEvent(message, offset, eventCount);
        } else {
            if (offset < frames) message.hostTime = 0u;
            deferMidi(message);
        }
    }

    // CoreMIDI normally supplies monotonic packets, but several connected
    // sources may interleave. CLAP requires time-ordered events, with NRPN
    // byte ordering retained when timestamps are equal.
    std::sort(midiEvents_.begin(), midiEvents_.begin() + eventCount,
        [](const BlockMidiEvent& left, const BlockMidiEvent& right) {
            if (left.event.header.time != right.event.header.time)
                return left.event.header.time < right.event.header.time;
            return left.sequence < right.sequence;
        });
    midiEventCount_ = eventCount;
}

bool NoInputMixerStandaloneEngine::dequeueMidi(MidiMessage& message)
{
    const uint32_t read = midiRead_.load(std::memory_order_relaxed);
    if (read == midiWrite_.load(std::memory_order_acquire)) return false;
    message = midiQueue_[read];
    midiRead_.store((read + 1u) % kMidiQueueCapacity,
        std::memory_order_release);
    return true;
}

bool NoInputMixerStandaloneEngine::enqueueMidiOutput(uint8_t status,
    uint8_t dataOne, uint8_t dataTwo)
{
    const uint32_t write = midiOutputWrite_.load(std::memory_order_relaxed);
    const uint32_t next = (write + 1u) % kMidiOutputQueueCapacity;
    if (next == midiOutputRead_.load(std::memory_order_acquire)) return false;
    midiOutputQueue_[write] = { status, dataOne, dataTwo };
    midiOutputWrite_.store(next, std::memory_order_release);
    return true;
}

bool NoInputMixerStandaloneEngine::dequeueMidiOutput(uint8_t& status,
    uint8_t& dataOne, uint8_t& dataTwo)
{
    const uint32_t read = midiOutputRead_.load(std::memory_order_relaxed);
    if (read == midiOutputWrite_.load(std::memory_order_acquire)) return false;
    const MidiMessage message = midiOutputQueue_[read];
    midiOutputRead_.store((read + 1u) % kMidiOutputQueueCapacity,
        std::memory_order_release);
    status = message.status;
    dataOne = message.dataOne;
    dataTwo = message.dataTwo;
    return true;
}

void NoInputMixerStandaloneEngine::serviceGestureFeedback()
{
    if (!gestureFeedbackEnabled()) return;
    const bool requested = gestureFeedbackRequested_.exchange(
        false, std::memory_order_acq_rel);

    const auto* params = gesture_.extension<clap_plugin_params_t>(
        CLAP_EXT_PARAMS);
    if (!params || !params->get_value) {
        gestureFeedbackRequested_.store(true, std::memory_order_release);
        return;
    }

    double recording = 0.0;
    double playing = 0.0;
    double loopCount = 0.0;
    double lastLoopLength = 0.0;
    if (!params->get_value(gesture_.plugin(), 1u, &recording)
        || !params->get_value(gesture_.plugin(), 2u, &playing)
        || !params->get_value(gesture_.plugin(), 6u, &loopCount)
        || !params->get_value(gesture_.plugin(), 7u, &lastLoopLength)) {
        gestureFeedbackRequested_.store(true, std::memory_order_release);
        return;
    }

    const GestureFeedbackState current {
        recording >= 0.5,
        playing >= 0.5,
        lastLoopLength > 0.0,
        loopCount >= 0.5,
    };
    const bool heartbeatDue = !gestureFeedbackStateValid_
        || steadyTime_ >= gestureFeedbackNextFrame_;
    if (!requested && !heartbeatDue
        && current == gestureFeedbackState_) return;

    const auto sendState = [this](uint8_t note, bool active) {
        const uint8_t status = static_cast<uint8_t>(
            (active ? 0x90u : 0x80u)
            | s3g::nim_gesture_midi::kFeedbackChannel);
        return enqueueMidiOutput(status, note, active ? 127u : 0u);
    };
    bool sent = true;
    sent = sendState(s3g::nim_gesture_midi::kRecordNote,
        current.recording) && sent;
    sent = sendState(s3g::nim_gesture_midi::kPlayNote,
        current.playing) && sent;
    sent = sendState(s3g::nim_gesture_midi::kClearLastNote,
        current.hasLastLoop) && sent;
    sent = sendState(s3g::nim_gesture_midi::kClearAllNote,
        current.hasAnyLoop) && sent;
    sent = sendState(s3g::nim_gesture_midi::kCancelRecordNote,
        current.recording) && sent;

    if (!sent) {
        gestureFeedbackRequested_.store(true, std::memory_order_release);
        return;
    }
    gestureFeedbackState_ = current;
    gestureFeedbackStateValid_ = true;
    gestureFeedbackNextFrame_ = steadyTime_ + static_cast<uint64_t>(
        std::max(1.0, sampleRate_));
}

void NoInputMixerStandaloneEngine::clearOutput(float* const* output,
    uint32_t channels, uint32_t frames) const
{
    if (!output) return;
    for (uint32_t channel = 0u; channel < channels; ++channel) {
        if (output[channel]) std::fill_n(output[channel], frames, 0.0f);
    }
}

bool NoInputMixerStandaloneEngine::render(float* const* output,
    uint32_t outputChannels, uint32_t frames, uint64_t blockHostTime)
{
    if (!prepared_ || !output || frames == 0u || frames > maximumFrames_) {
        clearOutput(output, outputChannels, frames);
        return false;
    }
    const auto failBlock = [&](std::atomic<uint64_t>& stageErrors) {
        stageErrors.fetch_add(1u, std::memory_order_relaxed);
        clearOutput(output, outputChannels, frames);
        for (auto& peak : outputPeaks_) {
            peak.store(peak.load(std::memory_order_relaxed) * 0.90f,
                std::memory_order_relaxed);
        }
        steadyTime_ += frames;
        return false;
    };
    prepareMidiEvents(frames, blockHostTime);

    clap_event_transport_t transport {};
    transport.header.size = sizeof(transport);
    transport.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    transport.header.type = CLAP_EVENT_TRANSPORT;
    transport.flags = CLAP_TRANSPORT_HAS_TEMPO;
    transport.tempo = tempo();

    gestureMidiEventCount_ = 0u;
    clap_process_t gestureProcess {};
    gestureProcess.steady_time = steadyTime_;
    gestureProcess.frames_count = frames;
    gestureProcess.transport = &transport;
    gestureProcess.in_events = &inputEvents_;
    gestureProcess.out_events = &gestureOutputEvents_;
    if (!validProcessStatus(gesture_.process(gestureProcess))) {
        gestureMidiEventCount_ = 0u;
        return failBlock(gestureProcessErrorCount_);
    }
    serviceGestureFeedback();

    clap_audio_buffer_t sourceOutput {};
    sourceOutput.data32 = sourcePointers_.data();
    sourceOutput.channel_count = kSourceChannels;
    clap_process_t sourceProcess {};
    sourceProcess.steady_time = steadyTime_;
    sourceProcess.frames_count = frames;
    sourceProcess.transport = &transport;
    sourceProcess.audio_outputs = &sourceOutput;
    sourceProcess.audio_outputs_count = 1u;
    sourceProcess.in_events = &gestureInputEvents_;
    sourceProcess.out_events = &noInputOutputEvents_;
    if (!validProcessStatus(noInput_.process(sourceProcess)))
        return failBlock(noInputProcessErrorCount_);

    const NoInputOutputMode mode = outputMode();
    float* const* routed = sourcePointers_.data();
    uint32_t routedChannels = kSourceChannels;
    if (mode != NoInputOutputMode::DirectEight) {
        clap_audio_buffer_t foldInput {};
        foldInput.data32 = sourcePointers_.data();
        foldInput.channel_count = kSourceChannels;
        clap_audio_buffer_t foldOutput {};
        if (mode == NoInputOutputMode::QuadAutogain) {
            foldOutput.data32 = quadPointers_.data();
            foldOutput.channel_count = 4u;
            routed = quadPointers_.data();
            routedChannels = 4u;
        } else {
            foldOutput.data32 = stereoPointers_.data();
            foldOutput.channel_count = 2u;
            routed = stereoPointers_.data();
            routedChannels = 2u;
        }
        clap_process_t foldProcess {};
        foldProcess.steady_time = steadyTime_;
        foldProcess.frames_count = frames;
        foldProcess.audio_inputs = &foldInput;
        foldProcess.audio_inputs_count = 1u;
        foldProcess.audio_outputs = &foldOutput;
        foldProcess.audio_outputs_count = 1u;
        if (mode == NoInputOutputMode::QuadAutogain) {
            if (!validProcessStatus(quad_.process(foldProcess)))
                return failBlock(quadFoldProcessErrorCount_);
        } else if (!validProcessStatus(stereo_.process(foldProcess))) {
            return failBlock(stereoFoldProcessErrorCount_);
        }
    }

    clearOutput(output, outputChannels, frames);
    const uint32_t outputOffset = std::min(outputChannelOffset(),
        outputChannels);
    const float target = audioEnabled() ? 1.0f : 0.0f;
    const float fadeStep = 1.0f / std::max(1.0,
        sampleRate_ * 0.025);
    std::array<float, kSourceChannels> peaks {};
    uint64_t nonFiniteOutputSamples = 0u;
    for (uint32_t frame = 0u; frame < frames; ++frame) {
        if (monitorGain_ < target)
            monitorGain_ = std::min(target, monitorGain_ + fadeStep);
        else if (monitorGain_ > target)
            monitorGain_ = std::max(target, monitorGain_ - fadeStep);
        const uint32_t availableChannels = outputOffset < outputChannels
            ? outputChannels - outputOffset : 0u;
        const uint32_t channels = std::min(availableChannels,
            routedChannels);
        for (uint32_t channel = 0u; channel < channels; ++channel) {
            const float value = routed[channel][frame] * monitorGain_;
            const bool finite = std::isfinite(value);
            output[outputOffset + channel][frame] = finite ? value : 0.0f;
            if (!finite) ++nonFiniteOutputSamples;
            peaks[channel] = std::max(peaks[channel],
                std::abs(output[outputOffset + channel][frame]));
        }
    }
    for (uint32_t channel = 0u; channel < kSourceChannels; ++channel) {
        outputPeaks_[channel].store(std::max(
            outputPeaks_[channel].load(std::memory_order_relaxed) * 0.90f,
            peaks[channel]), std::memory_order_relaxed);
    }
    if (nonFiniteOutputSamples != 0u) {
        nonFiniteOutputSampleCount_.fetch_add(nonFiniteOutputSamples,
            std::memory_order_relaxed);
    }
    steadyTime_ += frames;
    return true;
}

NoInputMixerStandaloneTelemetrySnapshot
NoInputMixerStandaloneEngine::telemetry() const
{
    NoInputMixerStandaloneTelemetrySnapshot result;
    result.gestureProcessErrorCount = gestureProcessErrorCount_.load(
        std::memory_order_relaxed);
    result.noInputProcessErrorCount = noInputProcessErrorCount_.load(
        std::memory_order_relaxed);
    result.stereoFoldProcessErrorCount = stereoFoldProcessErrorCount_.load(
        std::memory_order_relaxed);
    result.quadFoldProcessErrorCount = quadFoldProcessErrorCount_.load(
        std::memory_order_relaxed);
    result.nonFiniteOutputSampleCount = nonFiniteOutputSampleCount_.load(
        std::memory_order_relaxed);
    return result;
}

void NoInputMixerStandaloneEngine::resetTelemetry()
{
    gestureProcessErrorCount_.store(0u, std::memory_order_relaxed);
    noInputProcessErrorCount_.store(0u, std::memory_order_relaxed);
    stereoFoldProcessErrorCount_.store(0u, std::memory_order_relaxed);
    quadFoldProcessErrorCount_.store(0u, std::memory_order_relaxed);
    nonFiniteOutputSampleCount_.store(0u, std::memory_order_relaxed);
}

float NoInputMixerStandaloneEngine::outputPeak(uint32_t channel) const
{
    return channel < outputPeaks_.size()
        ? outputPeaks_[channel].load(std::memory_order_relaxed) : 0.0f;
}

} // namespace s3g::standalone
