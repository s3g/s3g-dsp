#include "s3g_coreaudio_output.h"
#include "s3g_nim_midi_feedback.h"
#include "s3g_no_input_mixer_standalone_engine.h"

#include <AudioUnit/AudioUnit.h>
#include <CoreAudio/CoreAudio.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

extern "C" const clap_plugin_entry_t s3g_no_input_mixer_embedded_entry;
extern "C" const clap_plugin_entry_t s3g_nim_gesture_embedded_entry;

namespace {

using s3g::standalone::CoreAudioDeviceInfo;
using s3g::standalone::CoreAudioOutput;
using s3g::standalone::NoInputMixerStandaloneEngine;
using s3g::standalone::NoInputMixerStandaloneTelemetrySnapshot;
using s3g::standalone::NoInputOutputMode;
using s3g::standalone::RealtimeCallbackTelemetrySnapshot;
using s3g::standalone::outputChannelsForMode;

constexpr int kContractError = 2;
constexpr int kConfigurationError = 3;
constexpr int kOutputError = 4;
constexpr int kStrictRealtimeFailure = 5;
constexpr int kInterrupted = 130;
constexpr double kDefaultWarmupSeconds = 5.0;

volatile std::sig_atomic_t gInterrupted = 0;

void interruptHandler(int) { gInterrupted = 1; }

struct Options {
    bool help = false;
    bool listDevices = false;
    bool confirmLiveAudio = false;
    bool strict = false;
    bool deviceUidSpecified = false;
    bool durationSpecified = false;
    bool modeSpecified = false;
    bool warmupSpecified = false;
    std::string deviceUid;
    std::string jsonPath;
    double durationSeconds = 0.0;
    double warmupSeconds = kDefaultWarmupSeconds;
    NoInputOutputMode mode = NoInputOutputMode::StereoRing;
};

struct DiagnosticState {
    NoInputMixerStandaloneEngine engine;
    CoreAudioOutput audio;
    std::vector<float> hardwareScratch;
    std::vector<float*> hardwarePointers;
    std::atomic<bool> renderReady { false };
    std::atomic<uint64_t> renderFailureCount { 0u };
    std::atomic<uint64_t> outputLayoutErrorCount { 0u };
    std::atomic<uint64_t> nonzeroMutedCallbackCount { 0u };
};

void printUsage(std::ostream& output)
{
    output
        << "Usage:\n"
        << "  s3g_no_input_mixer_coreaudio_diagnostic --list-devices "
           "[--json PATH]\n"
        << "  s3g_no_input_mixer_coreaudio_diagnostic "
           "--device-uid UID --duration-seconds SECONDS "
           "--confirm-live-audio [options]\n\n"
        << "Live options:\n"
        << "  --mode stereo|quad|direct8  Output chain (default: stereo)\n"
        << "  --warmup-seconds SECONDS    Unmeasured warmup (default: 5)\n"
        << "  --json PATH                 Write JSON to PATH; '-' means stdout\n"
        << "  --strict                    Fail when realtime faults are observed\n\n"
        << "The live diagnostic never enables NIM monitoring and never changes "
           "the device sample rate, buffer size, or system default. Starting "
           "AUHAL can still awaken or claim the explicitly selected device.\n";
}

bool parseFiniteDouble(const char* text, double& value)
{
    if (!text || *text == '\0') return false;
    errno = 0;
    char* end = nullptr;
    const double parsed = std::strtod(text, &end);
    if (errno != 0 || end == text || *end != '\0' || !std::isfinite(parsed))
        return false;
    value = parsed;
    return true;
}

bool parseMode(const std::string& text, NoInputOutputMode& mode)
{
    if (text == "stereo") {
        mode = NoInputOutputMode::StereoRing;
        return true;
    }
    if (text == "quad") {
        mode = NoInputOutputMode::QuadRing;
        return true;
    }
    if (text == "direct8") {
        mode = NoInputOutputMode::DirectEight;
        return true;
    }
    return false;
}

bool requireValue(int argc, char** argv, int& index, const char* option,
    const char*& value, std::string& error)
{
    if (index + 1 >= argc) {
        error = std::string(option) + " requires a value";
        return false;
    }
    value = argv[++index];
    return true;
}

bool parseOptions(int argc, char** argv, Options& options, std::string& error)
{
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        const char* value = nullptr;
        if (argument == "--help" || argument == "-h") {
            options.help = true;
        } else if (argument == "--list-devices") {
            options.listDevices = true;
        } else if (argument == "--confirm-live-audio") {
            options.confirmLiveAudio = true;
        } else if (argument == "--strict") {
            options.strict = true;
        } else if (argument == "--device-uid") {
            if (!requireValue(argc, argv, index, "--device-uid", value,
                    error)) return false;
            options.deviceUid = value;
            options.deviceUidSpecified = true;
        } else if (argument == "--duration-seconds") {
            if (!requireValue(argc, argv, index, "--duration-seconds", value,
                    error)) return false;
            if (!parseFiniteDouble(value, options.durationSeconds)) {
                error = "--duration-seconds must be a finite number";
                return false;
            }
            options.durationSpecified = true;
        } else if (argument == "--warmup-seconds") {
            if (!requireValue(argc, argv, index, "--warmup-seconds", value,
                    error)) return false;
            if (!parseFiniteDouble(value, options.warmupSeconds)) {
                error = "--warmup-seconds must be a finite number";
                return false;
            }
            options.warmupSpecified = true;
        } else if (argument == "--mode") {
            if (!requireValue(argc, argv, index, "--mode", value, error))
                return false;
            if (!parseMode(value, options.mode)) {
                error = "--mode must be stereo, quad, or direct8";
                return false;
            }
            options.modeSpecified = true;
        } else if (argument == "--json") {
            if (!requireValue(argc, argv, index, "--json", value, error))
                return false;
            options.jsonPath = value;
        } else {
            error = "unknown argument: " + argument;
            return false;
        }
    }
    return true;
}

std::string jsonEscape(const std::string& text)
{
    std::ostringstream output;
    for (const unsigned char character : text) {
        switch (character) {
        case '"': output << "\\\""; break;
        case '\\': output << "\\\\"; break;
        case '\b': output << "\\b"; break;
        case '\f': output << "\\f"; break;
        case '\n': output << "\\n"; break;
        case '\r': output << "\\r"; break;
        case '\t': output << "\\t"; break;
        default:
            if (character < 0x20u) {
                output << "\\u" << std::hex << std::setw(4)
                       << std::setfill('0') << static_cast<unsigned>(character)
                       << std::dec << std::setfill(' ');
            } else {
                output << static_cast<char>(character);
            }
            break;
        }
    }
    return output.str();
}

const char* jsonBoolean(bool value) { return value ? "true" : "false"; }

const char* modeName(NoInputOutputMode mode)
{
    switch (mode) {
    case NoInputOutputMode::QuadRing: return "quad";
    case NoInputOutputMode::DirectEight: return "direct8";
    case NoInputOutputMode::StereoRing:
    default: return "stereo";
    }
}

bool emitJson(const std::string& json, const std::string& path,
    std::string& error)
{
    if (path.empty() || path == "-") {
        std::cout << json << '\n';
        return static_cast<bool>(std::cout);
    }
    std::ofstream output(path, std::ios::out | std::ios::trunc);
    if (!output) {
        error = "could not open JSON output: " + path;
        return false;
    }
    output << json << '\n';
    if (!output) {
        error = "could not write JSON output: " + path;
        return false;
    }
    return true;
}

template <typename Value>
bool readDeviceProperty(AudioDeviceID device,
    AudioObjectPropertySelector selector, AudioObjectPropertyScope scope,
    Value& value)
{
    AudioObjectPropertyAddress address { selector, scope,
        kAudioObjectPropertyElementMain };
    if (!AudioObjectHasProperty(device, &address)) return false;
    UInt32 size = sizeof(value);
    return AudioObjectGetPropertyData(device, &address, 0u, nullptr, &size,
        &value) == noErr && size == sizeof(value);
}

std::string deviceListJson(const std::vector<CoreAudioDeviceInfo>& devices)
{
    const AudioDeviceID defaultDevice = CoreAudioOutput::defaultOutputDevice();
    std::ostringstream output;
    output << std::setprecision(12)
           << "{\"schema\":\"s3g-coreaudio-device-list-v1\",\"devices\":[";
    for (size_t index = 0u; index < devices.size(); ++index) {
        const auto& device = devices[index];
        UInt32 bufferFrames = 0u;
        UInt32 maximumVariableFrames = 0u;
        UInt32 latencyFrames = 0u;
        UInt32 safetyOffsetFrames = 0u;
        Float64 actualSampleRate = 0.0;
        const bool hasBufferFrames = readDeviceProperty(device.id,
            kAudioDevicePropertyBufferFrameSize,
            kAudioObjectPropertyScopeGlobal, bufferFrames);
        const bool hasVariableFrames = readDeviceProperty(device.id,
            kAudioDevicePropertyUsesVariableBufferFrameSizes,
            kAudioObjectPropertyScopeGlobal, maximumVariableFrames);
        const bool hasLatency = readDeviceProperty(device.id,
            kAudioDevicePropertyLatency, kAudioDevicePropertyScopeOutput,
            latencyFrames);
        const bool hasSafetyOffset = readDeviceProperty(device.id,
            kAudioDevicePropertySafetyOffset,
            kAudioDevicePropertyScopeOutput, safetyOffsetFrames);
        const bool hasActualSampleRate = readDeviceProperty(device.id,
            kAudioDevicePropertyActualSampleRate,
            kAudioObjectPropertyScopeGlobal, actualSampleRate);
        if (index != 0u) output << ',';
        output << "{\"uid\":\"" << jsonEscape(device.uid)
               << "\",\"name\":\"" << jsonEscape(device.name)
               << "\",\"output_channels\":" << device.outputChannels
               << ",\"nominal_sample_rate\":" << device.sampleRate
               << ",\"actual_sample_rate\":";
        if (hasActualSampleRate) output << actualSampleRate;
        else output << "null";
        output << ",\"buffer_frame_size\":";
        if (hasBufferFrames) output << bufferFrames;
        else output << "null";
        output << ",\"uses_variable_buffer_frames\":";
        if (hasVariableFrames)
            output << jsonBoolean(maximumVariableFrames > 0u);
        else output << "null";
        output << ",\"maximum_variable_buffer_frames\":";
        if (hasVariableFrames) output << maximumVariableFrames;
        else output << "null";
        output << ",\"output_latency_frames\":";
        if (hasLatency) output << latencyFrames;
        else output << "null";
        output << ",\"output_safety_offset_frames\":";
        if (hasSafetyOffset) output << safetyOffsetFrames;
        else output << "null";
        output
               << ",\"is_default\":"
               << jsonBoolean(device.id == defaultDevice) << '}';
    }
    output << "]}";
    return output.str();
}

void clearAudioBufferList(AudioBufferList* output)
{
    if (!output) return;
    for (UInt32 index = 0u; index < output->mNumberBuffers; ++index) {
        auto& buffer = output->mBuffers[index];
        if (buffer.mData && buffer.mDataByteSize != 0u)
            std::memset(buffer.mData, 0, buffer.mDataByteSize);
    }
}

OSStatus renderAudio(void* context, AudioBufferList* output, uint32_t frames,
    uint64_t blockHostTime)
{
    auto* state = static_cast<DiagnosticState*>(context);
    if (!state || !output) {
        if (state && state->renderReady.load(std::memory_order_acquire))
            state->outputLayoutErrorCount.fetch_add(1u,
                std::memory_order_relaxed);
        clearAudioBufferList(output);
        return noErr;
    }
    if (!state->renderReady.load(std::memory_order_acquire)) {
        clearAudioBufferList(output);
        return noErr;
    }

    const uint32_t renderChannels = state->audio.config().renderChannels;
    if (frames == 0u || frames > state->engine.maximumFrames()
        || state->hardwarePointers.size() < renderChannels) {
        state->renderFailureCount.fetch_add(1u, std::memory_order_relaxed);
        clearAudioBufferList(output);
        return noErr;
    }

    if (!state->engine.render(state->hardwarePointers.data(), renderChannels,
            frames, blockHostTime)) {
        state->renderFailureCount.fetch_add(1u, std::memory_order_relaxed);
    }

    bool layoutError = false;
    bool nonzeroMutedOutput = false;
    uint32_t channelBase = 0u;
    for (UInt32 bufferIndex = 0u; bufferIndex < output->mNumberBuffers;
         ++bufferIndex) {
        auto& target = output->mBuffers[bufferIndex];
        auto* samples = static_cast<float*>(target.mData);
        const uint32_t bufferChannels = std::max<UInt32>(1u,
            target.mNumberChannels);
        const size_t capacity = target.mDataByteSize / sizeof(float);
        const size_t required = static_cast<size_t>(frames) * bufferChannels;
        if (!samples || capacity < required) layoutError = true;
        if (samples && capacity != 0u) {
            std::fill_n(samples, capacity, 0.0f);
            for (uint32_t frame = 0u; frame < frames; ++frame) {
                for (uint32_t channel = 0u; channel < bufferChannels;
                     ++channel) {
                    const size_t index = static_cast<size_t>(frame)
                        * bufferChannels + channel;
                    const uint32_t sourceChannel = channelBase + channel;
                    if (index >= capacity || sourceChannel >= renderChannels)
                        continue;
                    const float value =
                        state->hardwarePointers[sourceChannel][frame];
                    samples[index] = value;
                    nonzeroMutedOutput = nonzeroMutedOutput || value != 0.0f;
                }
            }
        }
        channelBase += bufferChannels;
    }
    if (channelBase < renderChannels) layoutError = true;
    if (layoutError) state->outputLayoutErrorCount.fetch_add(1u,
        std::memory_order_relaxed);
    if (nonzeroMutedOutput)
        state->nonzeroMutedCallbackCount.fetch_add(1u,
            std::memory_order_relaxed);
    return noErr;
}

void servicePlugins(DiagnosticState& state)
{
    state.engine.noInputPlugin().serviceMainThreadCallback();
    state.engine.gesturePlugin().serviceMainThreadCallback();
    uint8_t status = 0u;
    uint8_t dataOne = 0u;
    uint8_t dataTwo = 0u;
    while (state.engine.dequeueMidiOutput(status, dataOne, dataTwo)) {}
}

double runFor(DiagnosticState& state, double seconds)
{
    const auto start = std::chrono::steady_clock::now();
    while (!gInterrupted) {
        const auto now = std::chrono::steady_clock::now();
        const double elapsed = std::chrono::duration<double>(now - start)
            .count();
        if (elapsed >= seconds) return elapsed;
        servicePlugins(state);
        const double remaining = seconds - elapsed;
        const auto sleepDuration = std::chrono::duration<double>(
            std::min(0.01, std::max(0.0, remaining)));
        std::this_thread::sleep_for(sleepDuration);
    }
    return std::chrono::duration<double>(std::chrono::steady_clock::now()
        - start).count();
}

uint64_t difference(uint64_t finalValue, uint64_t baseline)
{
    return finalValue >= baseline ? finalValue - baseline : 0u;
}

NoInputMixerStandaloneTelemetrySnapshot difference(
    const NoInputMixerStandaloneTelemetrySnapshot& finalValue,
    const NoInputMixerStandaloneTelemetrySnapshot& baseline)
{
    NoInputMixerStandaloneTelemetrySnapshot result;
    result.gestureProcessErrorCount = difference(
        finalValue.gestureProcessErrorCount,
        baseline.gestureProcessErrorCount);
    result.noInputProcessErrorCount = difference(
        finalValue.noInputProcessErrorCount,
        baseline.noInputProcessErrorCount);
    result.nonFiniteOutputSampleCount = difference(
        finalValue.nonFiniteOutputSampleCount,
        baseline.nonFiniteOutputSampleCount);
    return result;
}

uint64_t realtimeFaultCount(const RealtimeCallbackTelemetrySnapshot& audio,
    const NoInputMixerStandaloneTelemetrySnapshot& engine,
    uint64_t renderFailures, uint64_t layoutErrors,
    uint64_t nonzeroMutedCallbacks, uint64_t midiDrops)
{
    // Callback-start gaps are useful scheduler diagnostics, but they are not
    // authoritative xruns: a delayed launch may still complete before the
    // hardware deadline. Keep lateCallbackCount advisory and reserve strict
    // failure for observed processing/timestamp/HAL/DSP faults.
    uint64_t faults = audio.deadlineMissCount
        + audio.timestampDiscontinuityCount + audio.timestampUnavailableCount
        + audio.sampleTimeDiscontinuityCount
        + audio.sampleTimeUnavailableCount
        + audio.oversizedCallbackCount + audio.processorOverloadCount
        + audio.abnormalStopCount
        + audio.deviceAliveChangeCount
        + audio.deviceConfigurationChangeCount + audio.callbackErrorCount
        + audio.renderActionErrorCount + engine.totalProcessErrorCount()
        + engine.nonFiniteOutputSampleCount
        + renderFailures + layoutErrors + nonzeroMutedCallbacks + midiDrops;
    if (audio.callbackCount == 0u) ++faults;
    return faults;
}

std::string reportJson(const Options& options,
    const CoreAudioDeviceInfo& device,
    const s3g::standalone::CoreAudioOutputConfig& config,
    double actualSampleRateBefore, bool hasActualSampleRateBefore,
    double actualSampleRateAfter, bool hasActualSampleRateAfter,
    double measuredSeconds, bool completed,
    const RealtimeCallbackTelemetrySnapshot& audio,
    const NoInputMixerStandaloneTelemetrySnapshot& warmupEngine,
    const NoInputMixerStandaloneTelemetrySnapshot& engine,
    uint64_t warmupRenderFailures, uint64_t renderFailures,
    uint64_t warmupLayoutErrors, uint64_t layoutErrors,
    uint64_t warmupNonzeroMutedCallbacks, uint64_t nonzeroMutedCallbacks,
    uint64_t warmupMidiDrops, uint64_t midiDrops, uint64_t faultCount)
{
    const char* status = !completed ? "interrupted"
        : faultCount == 0u ? "completed"
        : options.strict ? "failed_strict" : "realtime_faults_reported";
    std::ostringstream output;
    output << std::setprecision(12)
        << "{\"schema\":\"s3g-nim-coreaudio-diagnostic-v1\""
        << ",\"status\":\"" << status << '"'
        << ",\"completed\":" << jsonBoolean(completed)
        << ",\"strict\":" << jsonBoolean(options.strict)
        << ",\"monitor_muted\":true"
        << ",\"mode\":\"" << modeName(options.mode) << '"'
        << ",\"requested_duration_seconds\":"
        << options.durationSeconds
        << ",\"measured_duration_seconds\":" << measuredSeconds
        << ",\"warmup_seconds\":" << options.warmupSeconds
        << ",\"fault_count\":" << faultCount
        << ",\"device\":{\"uid\":\"" << jsonEscape(device.uid)
        << "\",\"name\":\"" << jsonEscape(device.name)
        << "\",\"output_channels\":" << device.outputChannels
        << ",\"required_output_channels\":"
        << outputChannelsForMode(options.mode)
        << ",\"nominal_sample_rate\":" << config.sampleRate
        << ",\"actual_sample_rate_before\":";
    if (hasActualSampleRateBefore) output << actualSampleRateBefore;
    else output << "null";
    output << ",\"actual_sample_rate_after\":";
    if (hasActualSampleRateAfter) output << actualSampleRateAfter;
    else output << "null";
    output
        << ",\"hardware_channels\":" << config.hardwareChannels
        << ",\"render_channels\":" << config.renderChannels
        << ",\"device_buffer_frames\":" << config.deviceBufferFrames
        << ",\"maximum_frames_per_slice\":" << config.maximumFrames
        << ",\"host_ticks_per_second\":" << config.hostTicksPerSecond
        << ",\"uses_variable_buffer_frames\":"
        << jsonBoolean(config.usesVariableBufferFrames)
        << ",\"maximum_variable_buffer_frames\":"
        << config.maximumVariableBufferFrames
        << ",\"latency_frames\":" << config.deviceLatencyFrames
        << ",\"safety_offset_frames\":"
        << config.deviceSafetyOffsetFrames
        << ",\"signals\":{\"processor_overload\":"
        << jsonBoolean(config.processorOverloadSignalAvailable)
        << ",\"abnormal_stop\":"
        << jsonBoolean(config.abnormalStopSignalAvailable)
        << ",\"render_error\":"
        << jsonBoolean(config.renderErrorSignalAvailable)
        << ",\"device_alive\":"
        << jsonBoolean(config.deviceAliveSignalAvailable)
        << ",\"device_configuration\":"
        << jsonBoolean(config.deviceConfigurationSignalAvailable)
        << "}}"
        << ",\"coreaudio\":{\"callback_count\":" << audio.callbackCount
        << ",\"rendered_frame_count\":" << audio.renderedFrameCount
        << ",\"callback_period_overrun_count\":"
        << audio.deadlineMissCount
        << ",\"late_callback_count\":" << audio.lateCallbackCount
        << ",\"timestamp_discontinuity_count\":"
        << audio.timestampDiscontinuityCount
        << ",\"timestamp_unavailable_count\":"
        << audio.timestampUnavailableCount
        << ",\"sample_time_discontinuity_count\":"
        << audio.sampleTimeDiscontinuityCount
        << ",\"sample_time_unavailable_count\":"
        << audio.sampleTimeUnavailableCount
        << ",\"oversized_callback_count\":"
        << audio.oversizedCallbackCount
        << ",\"processor_overload_count\":"
        << audio.processorOverloadCount
        << ",\"abnormal_stop_count\":" << audio.abnormalStopCount
        << ",\"device_alive_change_count\":"
        << audio.deviceAliveChangeCount
        << ",\"device_configuration_change_count\":"
        << audio.deviceConfigurationChangeCount
        << ",\"callback_error_count\":" << audio.callbackErrorCount
        << ",\"render_action_error_count\":"
        << audio.renderActionErrorCount
        << ",\"smoothed_load\":" << audio.smoothedLoad
        << ",\"peak_load\":" << audio.peakLoad
        << ",\"last_load\":" << audio.lastLoad
        << ",\"last_callback_milliseconds\":"
        << audio.lastCallbackMilliseconds
        << ",\"maximum_callback_milliseconds\":"
        << audio.maximumCallbackMilliseconds
        << ",\"maximum_lateness_milliseconds\":"
        << audio.maximumLatenessMilliseconds
        << ",\"maximum_timestamp_error_milliseconds\":"
        << audio.maximumTimestampErrorMilliseconds
        << ",\"maximum_sample_time_error_frames\":"
        << audio.maximumSampleTimeErrorFrames << '}'
        << ",\"engine\":{\"gesture_process_error_count\":"
        << engine.gestureProcessErrorCount
        << ",\"no_input_process_error_count\":"
        << engine.noInputProcessErrorCount
        << ",\"non_finite_output_sample_count\":"
        << engine.nonFiniteOutputSampleCount
        << ",\"render_failure_count\":" << renderFailures
        << ",\"output_layout_error_count\":" << layoutErrors
        << ",\"nonzero_muted_callback_count\":"
        << nonzeroMutedCallbacks
        << ",\"midi_input_drop_count\":" << midiDrops << '}'
        << ",\"warmup\":{\"gesture_process_error_count\":"
        << warmupEngine.gestureProcessErrorCount
        << ",\"no_input_process_error_count\":"
        << warmupEngine.noInputProcessErrorCount
        << ",\"non_finite_output_sample_count\":"
        << warmupEngine.nonFiniteOutputSampleCount
        << ",\"render_failure_count\":" << warmupRenderFailures
        << ",\"output_layout_error_count\":" << warmupLayoutErrors
        << ",\"nonzero_muted_callback_count\":"
        << warmupNonzeroMutedCallbacks
        << ",\"midi_input_drop_count\":" << warmupMidiDrops << "}}";
    return output.str();
}

int runLiveDiagnostic(const Options& options,
    const CoreAudioDeviceInfo& device)
{
    DiagnosticState state;
    if (!state.engine.create(&s3g_no_input_mixer_embedded_entry,
            &s3g_nim_gesture_embedded_entry)) {
        std::cerr << "configuration error: could not create embedded CLAP "
                     "processors\n";
        return kConfigurationError;
    }
    const auto* feedback = state.engine.noInputPlugin()
        .extension<s3g_nim_midi_feedback_t>(
            S3G_NIM_MIDI_FEEDBACK_EXTENSION_ID);
    if (feedback && feedback->set_enabled)
        feedback->set_enabled(state.engine.noInputPlugin().plugin(), false,
            false);

    std::string audioError;
    if (!state.audio.open(device.id, device.outputChannels, renderAudio,
            &state, &audioError)) {
        std::cerr << "configuration error: " << audioError << '\n';
        state.engine.destroy();
        return kConfigurationError;
    }
    const auto config = state.audio.config();
    if (!state.engine.prepare(config.sampleRate, config.maximumFrames)) {
        std::cerr << "configuration error: could not activate embedded CLAP "
                     "processors\n";
        state.audio.close();
        state.engine.destroy();
        return kConfigurationError;
    }
    state.engine.setOutputMode(options.mode);
    state.engine.setOutputChannelOffset(options.mode, 0u);
    state.engine.setAudioEnabled(false);
    state.engine.setHostTicksPerSecond(config.hostTicksPerSecond);
    state.hardwareScratch.assign(static_cast<size_t>(config.renderChannels)
            * config.maximumFrames,
        0.0f);
    state.hardwarePointers.assign(config.renderChannels, nullptr);
    for (uint32_t channel = 0u; channel < config.renderChannels; ++channel) {
        state.hardwarePointers[channel] = state.hardwareScratch.data()
            + static_cast<size_t>(channel) * config.maximumFrames;
    }

    Float64 actualSampleRateBefore = 0.0;
    const bool hasActualSampleRateBefore = readDeviceProperty(device.id,
        kAudioDevicePropertyActualSampleRate,
        kAudioObjectPropertyScopeGlobal, actualSampleRateBefore);
    state.audio.setTelemetryEnabled(false);
    state.renderReady.store(true, std::memory_order_release);
    if (!state.audio.start(&audioError)) {
        state.renderReady.store(false, std::memory_order_release);
        std::cerr << "configuration error: " << audioError << '\n';
        state.engine.release();
        state.audio.close();
        state.engine.destroy();
        return kConfigurationError;
    }

    runFor(state, options.warmupSeconds);
    const auto warmupEngine = state.engine.telemetry();
    const uint64_t warmupRenderFailures = state.renderFailureCount.load(
        std::memory_order_relaxed);
    const uint64_t warmupLayoutErrors = state.outputLayoutErrorCount.load(
        std::memory_order_relaxed);
    const uint64_t warmupNonzeroMutedCallbacks =
        state.nonzeroMutedCallbackCount.load(std::memory_order_relaxed);
    const uint64_t warmupMidiDrops = state.engine.midiInputDropCount();

    double measuredSeconds = 0.0;
    if (!gInterrupted) {
        state.audio.setTelemetryEnabled(true);
        measuredSeconds = runFor(state, options.durationSeconds);
    }
    const bool completed = !gInterrupted
        && measuredSeconds >= options.durationSeconds;
    state.renderReady.store(false, std::memory_order_release);
    state.audio.stop();

    const auto audioTelemetry = state.audio.telemetry();
    const auto engineTelemetry = difference(state.engine.telemetry(),
        warmupEngine);
    const uint64_t renderFailures = difference(
        state.renderFailureCount.load(std::memory_order_relaxed),
        warmupRenderFailures);
    const uint64_t layoutErrors = difference(
        state.outputLayoutErrorCount.load(std::memory_order_relaxed),
        warmupLayoutErrors);
    const uint64_t nonzeroMutedCallbacks = difference(
        state.nonzeroMutedCallbackCount.load(std::memory_order_relaxed),
        warmupNonzeroMutedCallbacks);
    const uint64_t midiDrops = difference(state.engine.midiInputDropCount(),
        warmupMidiDrops);
    Float64 actualSampleRateAfter = 0.0;
    const bool hasActualSampleRateAfter = readDeviceProperty(device.id,
        kAudioDevicePropertyActualSampleRate,
        kAudioObjectPropertyScopeGlobal, actualSampleRateAfter);
    const uint64_t faults = realtimeFaultCount(audioTelemetry,
        engineTelemetry, renderFailures, layoutErrors,
        nonzeroMutedCallbacks, midiDrops);

    const std::string json = reportJson(options, device, config,
        actualSampleRateBefore, hasActualSampleRateBefore,
        actualSampleRateAfter, hasActualSampleRateAfter, measuredSeconds,
        completed, audioTelemetry, warmupEngine, engineTelemetry,
        warmupRenderFailures, renderFailures, warmupLayoutErrors, layoutErrors,
        warmupNonzeroMutedCallbacks, nonzeroMutedCallbacks, warmupMidiDrops,
        midiDrops, faults);

    state.engine.release();
    state.audio.close();
    state.engine.destroy();

    std::string outputError;
    if (!emitJson(json, options.jsonPath, outputError)) {
        std::cerr << "output error: " << outputError << '\n';
        return kOutputError;
    }
    if (!completed) return kInterrupted;
    if (options.strict && faults != 0u) return kStrictRealtimeFailure;
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    Options options;
    std::string error;
    if (!parseOptions(argc, argv, options, error)) {
        std::cerr << "contract error: " << error << "\n\n";
        printUsage(std::cerr);
        return kContractError;
    }
    if (options.help) {
        printUsage(std::cout);
        return 0;
    }

    const bool liveArgumentPresent = options.deviceUidSpecified
        || options.durationSpecified || options.confirmLiveAudio
        || options.modeSpecified || options.warmupSpecified || options.strict;
    if (options.listDevices) {
        if (liveArgumentPresent) {
            std::cerr << "contract error: --list-devices cannot be combined "
                         "with live diagnostic options\n";
            return kContractError;
        }
        std::string outputError;
        if (!emitJson(deviceListJson(CoreAudioOutput::enumerateDevices()),
                options.jsonPath, outputError)) {
            std::cerr << "output error: " << outputError << '\n';
            return kOutputError;
        }
        return 0;
    }

    if (!options.deviceUidSpecified || options.deviceUid.empty()
        || !options.durationSpecified || !options.confirmLiveAudio) {
        std::cerr << "contract error: a live run requires --device-uid, "
                     "--duration-seconds, and --confirm-live-audio\n\n";
        printUsage(std::cerr);
        return kContractError;
    }
    if (!(options.durationSeconds > 0.0)
        || options.durationSeconds > 86400.0) {
        std::cerr << "contract error: --duration-seconds must be greater than "
                     "zero and no more than 86400\n";
        return kContractError;
    }
    if (options.warmupSeconds < 0.0 || options.warmupSeconds > 3600.0) {
        std::cerr << "contract error: --warmup-seconds must be between zero "
                     "and 3600\n";
        return kContractError;
    }

    const auto devices = CoreAudioOutput::enumerateDevices();
    const auto found = std::find_if(devices.begin(), devices.end(),
        [&](const CoreAudioDeviceInfo& device) {
            return device.uid == options.deviceUid;
        });
    if (found == devices.end()) {
        std::cerr << "configuration error: no output device has UID '"
                  << options.deviceUid << "'\n";
        return kConfigurationError;
    }
    const uint32_t requiredChannels = outputChannelsForMode(options.mode);
    if (found->outputChannels < requiredChannels) {
        std::cerr << "configuration error: mode " << modeName(options.mode)
                  << " requires " << requiredChannels << " channels, but '"
                  << found->name << "' exposes " << found->outputChannels
                  << "\n";
        return kConfigurationError;
    }

    gInterrupted = 0;
    std::signal(SIGINT, interruptHandler);
    std::signal(SIGTERM, interruptHandler);
    return runLiveDiagnostic(options, *found);
}
