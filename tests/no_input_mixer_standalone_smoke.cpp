#include "s3g_no_input_mixer_standalone_engine.h"
#include "s3g_nim_gesture_session.h"
#include "s3g_coreaudio_output.h"

#include <clap/clap.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <iostream>
#include <vector>

extern "C" const clap_plugin_entry_t s3g_no_input_mixer_embedded_entry;
extern "C" const clap_plugin_entry_t s3g_nim_gesture_embedded_entry;
extern "C" const clap_plugin_entry_t s3g_mc_to_stereo_autogain_embedded_entry;
extern "C" const clap_plugin_entry_t s3g_mc_to_quad_autogain_embedded_entry;

namespace {

constexpr uint32_t kFrames = 256u;
constexpr uint32_t kChannels = 16u;

int64_t writeMemory(const clap_ostream_t* stream, const void* source,
    uint64_t byteCount)
{
    if (!stream || !stream->ctx || (!source && byteCount != 0u)) return -1;
    if (byteCount == 0u) return 0;
    auto* bytes = static_cast<std::vector<uint8_t>*>(stream->ctx);
    const auto* begin = static_cast<const uint8_t*>(source);
    bytes->insert(bytes->end(), begin, begin + byteCount);
    return static_cast<int64_t>(byteCount);
}

struct MemoryReader {
    const std::vector<uint8_t>* bytes = nullptr;
    size_t offset = 0u;
};

int64_t readMemory(const clap_istream_t* stream, void* destination,
    uint64_t byteCount)
{
    if (!stream || !stream->ctx || (!destination && byteCount != 0u))
        return -1;
    auto* reader = static_cast<MemoryReader*>(stream->ctx);
    if (!reader->bytes) return -1;
    const size_t count = std::min<size_t>(static_cast<size_t>(byteCount),
        reader->bytes->size() - std::min(reader->offset,
            reader->bytes->size()));
    if (count != 0u) {
        std::memcpy(destination, reader->bytes->data() + reader->offset,
            count);
        reader->offset += count;
    }
    return static_cast<int64_t>(count);
}

bool renderMode(s3g::standalone::NoInputMixerStandaloneEngine& engine,
    s3g::standalone::NoInputOutputMode mode, uint32_t outputOffset)
{
    std::array<std::array<float, kFrames>, kChannels> storage {};
    std::array<float*, kChannels> pointers {};
    for (uint32_t channel = 0u; channel < kChannels; ++channel)
        pointers[channel] = storage[channel].data();
    engine.setOutputChannelOffset(mode, outputOffset);
    engine.setOutputMode(mode);
    engine.setAudioEnabled(true);
    const uint32_t active = s3g::standalone::outputChannelsForMode(mode);
    std::array<double, kChannels> energy {};
    for (uint32_t block = 0u; block < 240u; ++block) {
        if (!engine.render(pointers.data(), kChannels, kFrames)) {
            std::cerr << "Standalone processor returned an error\n";
            return false;
        }
        for (uint32_t channel = 0u; channel < kChannels; ++channel) {
            for (const float value : storage[channel]) {
                if (!std::isfinite(value)) {
                    std::cerr << "Standalone output became non-finite\n";
                    return false;
                }
                energy[channel] += static_cast<double>(value) * value;
            }
        }
    }
    for (uint32_t channel = 0u; channel < kChannels; ++channel) {
        const bool shouldBeActive = channel >= outputOffset
            && channel < outputOffset + active;
        if (shouldBeActive && !(energy[channel] > 1.0e-10)) {
            std::cerr << "Output mode " << static_cast<uint32_t>(mode)
                      << " left required channel " << channel + 1u
                      << " silent\n";
            return false;
        }
        if (!shouldBeActive && energy[channel] != 0.0) {
            std::cerr << "Output mode " << static_cast<uint32_t>(mode)
                      << " leaked into channel " << channel + 1u << "\n";
            return false;
        }
    }
    return true;
}

} // namespace

int main()
{
    s3g::standalone::RealtimeCallbackTelemetry callbackTelemetry;
    callbackTelemetry.recordCallbackNanoseconds(1000000u, 256u, 48000.0,
        256u);
    callbackTelemetry.recordCallbackNanoseconds(6000000u, 256u, 48000.0,
        256u);
    callbackTelemetry.recordCallbackNanoseconds(1000000u, 512u, 48000.0,
        256u, true);
    callbackTelemetry.recordProcessorOverload();
    callbackTelemetry.recordAbnormalStop();
    callbackTelemetry.recordDeviceAliveChange();
    callbackTelemetry.recordDeviceConfigurationChange();
    auto callbackSnapshot = callbackTelemetry.snapshot();
    bool ok = callbackSnapshot.callbackCount == 3u
        && callbackSnapshot.renderedFrameCount == 1024u
        && callbackSnapshot.deadlineMissCount == 1u
        && callbackSnapshot.oversizedCallbackCount == 1u
        && callbackSnapshot.processorOverloadCount == 1u
        && callbackSnapshot.abnormalStopCount == 1u
        && callbackSnapshot.deviceAliveChangeCount == 1u
        && callbackSnapshot.deviceConfigurationChangeCount == 1u
        && callbackSnapshot.callbackErrorCount == 1u
        && callbackSnapshot.smoothedLoad > 0.0
        && std::abs(callbackSnapshot.peakLoad - 1.125) < 1.0e-6
        && std::abs(callbackSnapshot.lastCallbackMilliseconds - 1.0)
            < 1.0e-6
        && std::abs(callbackSnapshot.maximumCallbackMilliseconds - 6.0)
            < 1.0e-6;
    s3g::standalone::CoreAudioOutput unopenedAudio;
    ok = ok && !unopenedAudio.telemetryEnabled();
    unopenedAudio.setTelemetryEnabled(true);
    ok = ok && unopenedAudio.telemetryEnabled();
    unopenedAudio.setTelemetryEnabled(false);
    ok = ok && !unopenedAudio.telemetryEnabled();
    callbackTelemetry.reset();
    callbackSnapshot = callbackTelemetry.snapshot();
    ok = ok && callbackSnapshot.callbackCount == 0u
        && callbackSnapshot.renderedFrameCount == 0u
        && callbackSnapshot.deadlineMissCount == 0u
        && callbackSnapshot.sampleTimeDiscontinuityCount == 0u
        && callbackSnapshot.sampleTimeUnavailableCount == 0u
        && callbackSnapshot.maximumSampleTimeErrorFrames == 0.0
        && callbackSnapshot.maximumCallbackMilliseconds == 0.0;

    s3g::standalone::RealtimeCallbackTelemetry cadenceTelemetry;
    constexpr uint64_t firstCallbackStart = 1000000000u;
    constexpr uint64_t firstAudioHostTime = 2000000000u;
    constexpr uint64_t normalPeriod = 5333333u;
    cadenceTelemetry.recordCallbackTiming(1000000u, 256u, 48000.0,
        256u, firstCallbackStart, firstAudioHostTime, 1000.0, 1.0e9,
        true, true);
    cadenceTelemetry.recordCallbackTiming(1000000u, 256u, 48000.0,
        256u, firstCallbackStart + normalPeriod,
        firstAudioHostTime + normalPeriod, 1258.0, 1.0e9, true, true);
    cadenceTelemetry.recordCallbackTiming(1000000u, 256u, 48000.0,
        256u, firstCallbackStart + normalPeriod + 8000000u,
        firstAudioHostTime + normalPeriod + 10000000u, 1544.0, 1.0e9,
        true, true, false, true);
    cadenceTelemetry.recordCallbackTiming(1000000u, 256u, 48000.0,
        256u, firstCallbackStart + normalPeriod + 8000000u + normalPeriod,
        0u, 0.0, 1.0e9, false, false);
    const auto cadenceSnapshot = cadenceTelemetry.snapshot();
    ok = ok && cadenceSnapshot.callbackCount == 4u
        && cadenceSnapshot.renderedFrameCount == 1024u
        && cadenceSnapshot.lateCallbackCount == 1u
        && cadenceSnapshot.timestampDiscontinuityCount == 1u
        && cadenceSnapshot.timestampUnavailableCount == 1u
        && cadenceSnapshot.sampleTimeDiscontinuityCount == 1u
        && cadenceSnapshot.sampleTimeUnavailableCount == 1u
        && cadenceSnapshot.renderActionErrorCount == 1u
        && cadenceSnapshot.maximumLatenessMilliseconds > 2.0
        && cadenceSnapshot.maximumTimestampErrorMilliseconds > 4.0
        && std::abs(cadenceSnapshot.maximumSampleTimeErrorFrames - 30.0)
            < 1.0e-6
        && std::abs(cadenceSnapshot.lastLoad - 0.1875) < 1.0e-6;

    // Sample-time continuity follows the preceding callback size, not the
    // current size. This is required for devices with variable buffer frames.
    s3g::standalone::RealtimeCallbackTelemetry variableFrameTelemetry;
    variableFrameTelemetry.recordCallbackTiming(100000u, 128u, 48000.0,
        256u, 1000000u, 0u, 500.0, 1.0e9, false, true);
    variableFrameTelemetry.recordCallbackTiming(100000u, 256u, 48000.0,
        256u, 3666667u, 0u, 628.0, 1.0e9, false, true);
    variableFrameTelemetry.recordCallbackTiming(100000u, 64u, 48000.0,
        256u, 9000000u, 0u, 884.0, 1.0e9, false, true);
    const auto variableFrameSnapshot = variableFrameTelemetry.snapshot();
    ok = ok && variableFrameSnapshot.sampleTimeDiscontinuityCount == 0u
        && variableFrameSnapshot.sampleTimeUnavailableCount == 0u
        && variableFrameSnapshot.maximumSampleTimeErrorFrames == 0.0;

    constexpr uint64_t blockHostTime = 1000000000u;
    ok = ok && s3g::standalone::NoInputMixerStandaloneEngine::
            midiFrameOffset(0u, blockHostTime, 1.0e9, 48000.0,
                kFrames) == 0u
        && s3g::standalone::NoInputMixerStandaloneEngine::midiFrameOffset(
            blockHostTime - 1u, blockHostTime, 1.0e9, 48000.0,
            kFrames) == 0u
        && s3g::standalone::NoInputMixerStandaloneEngine::midiFrameOffset(
            blockHostTime + 2500000u, blockHostTime, 1.0e9, 48000.0,
            kFrames) == 120u
        && s3g::standalone::NoInputMixerStandaloneEngine::midiFrameOffset(
            blockHostTime + 6000000u, blockHostTime, 1.0e9, 48000.0,
            kFrames) == kFrames;

    s3g::standalone::NoInputMixerStandaloneEngine engine;
    ok = ok && engine.create(&s3g_no_input_mixer_embedded_entry,
        &s3g_nim_gesture_embedded_entry,
        &s3g_mc_to_stereo_autogain_embedded_entry,
        &s3g_mc_to_quad_autogain_embedded_entry);
    ok = ok && engine.prepare(48000.0, kFrames);

    const auto* gestureSession = engine.gesturePlugin()
        .extension<s3g_nim_gesture_session_t>(
            S3G_NIM_GESTURE_SESSION_EXTENSION);
    const auto* gestureParams = engine.gesturePlugin()
        .extension<clap_plugin_params_t>(CLAP_EXT_PARAMS);
    double loopCount = -1.0;
    double recording = -1.0;
    double playing = -1.0;
    ok = ok && gestureSession && gestureSession->save
        && gestureSession->load && gestureSession->clear && gestureParams
        && gestureSession->clear(engine.gesturePlugin().plugin())
        && gestureParams->get_value(engine.gesturePlugin().plugin(), 6u,
            &loopCount)
        && gestureParams->get_value(engine.gesturePlugin().plugin(), 1u,
            &recording)
        && gestureParams->get_value(engine.gesturePlugin().plugin(), 2u,
            &playing)
        && loopCount == 0.0 && recording == 0.0 && playing == 0.0;

    std::array<std::array<float, kFrames>, kChannels> midiStorage {};
    std::array<float*, kChannels> midiPointers {};
    for (uint32_t channel = 0u; channel < kChannels; ++channel)
        midiPointers[channel] = midiStorage[channel].data();
    engine.enqueueMidi(0x9fu, 112u, 127u);
    engine.enqueueMidi(0xbfu, 99u, 0u);
    engine.enqueueMidi(0xbfu, 98u, 5u);
    engine.enqueueMidi(0xbfu, 6u, 64u);
    engine.enqueueMidi(0xbfu, 38u, 0u);
    ok = ok && engine.render(midiPointers.data(), kChannels, kFrames);
    ok = ok && engine.render(midiPointers.data(), kChannels, kFrames);
    engine.enqueueMidi(0x9fu, 112u, 127u);
    ok = ok && engine.render(midiPointers.data(), kChannels, kFrames);
    uint32_t e16FeedbackCount = 0u;
    uint32_t gridFeedbackCount = 0u;
    std::array<bool, 64u> gridFeedbackSeen {};
    uint8_t status = 0u;
    uint8_t dataOne = 0u;
    uint8_t dataTwo = 0u;
    while (engine.dequeueMidiOutput(status, dataOne, dataTwo)) {
        if (status == 0xbfu) {
            ok = ok && dataOne < 128u && dataTwo < 128u;
            ++e16FeedbackCount;
        } else if ((status & 0xf0u) == 0xa0u
            && (status & 0x0fu) < 4u) {
            ok = ok && dataOne < 16u && dataTwo < 128u;
            const uint32_t index = (status & 0x0fu) * 16u + dataOne;
            gridFeedbackSeen[index] = true;
            ++gridFeedbackCount;
        } else {
            ok = false;
        }
    }
    ok = ok && e16FeedbackCount >= 4u && gridFeedbackCount == 64u
        && std::all_of(gridFeedbackSeen.begin(), gridFeedbackSeen.end(),
            [](bool seen) { return seen; });

    ok = ok && renderMode(engine,
        s3g::standalone::NoInputOutputMode::StereoAutogain, 2u);
    ok = ok && renderMode(engine,
        s3g::standalone::NoInputOutputMode::QuadAutogain, 4u);
    ok = ok && renderMode(engine,
        s3g::standalone::NoInputOutputMode::DirectEight, 8u);

    // Standalone session I/O must not deactivate, reprepare, or alter the NIM
    // processor. Exercise the extension while the audio chain is active.
    std::vector<uint8_t> noInputBeforeGestureSession;
    std::vector<uint8_t> noInputAfterGestureSession;
    std::vector<uint8_t> portableSession;
    ok = ok && engine.isPrepared()
        && engine.noInputPlugin().saveState(noInputBeforeGestureSession);
    clap_ostream_t sessionOutput { &portableSession, writeMemory };
    ok = ok && gestureSession->save(engine.gesturePlugin().plugin(),
            &sessionOutput)
        && !portableSession.empty();
    ok = ok && gestureSession->clear(engine.gesturePlugin().plugin())
        && gestureParams->get_value(engine.gesturePlugin().plugin(), 6u,
            &loopCount)
        && gestureParams->get_value(engine.gesturePlugin().plugin(), 1u,
            &recording)
        && gestureParams->get_value(engine.gesturePlugin().plugin(), 2u,
            &playing)
        && loopCount == 0.0 && recording == 0.0 && playing == 0.0;
    MemoryReader sessionReader { &portableSession, 0u };
    clap_istream_t sessionInput { &sessionReader, readMemory };
    ok = ok && gestureSession->load(engine.gesturePlugin().plugin(),
            &sessionInput)
        && gestureParams->get_value(engine.gesturePlugin().plugin(), 6u,
            &loopCount)
        && gestureParams->get_value(engine.gesturePlugin().plugin(), 1u,
            &recording)
        && gestureParams->get_value(engine.gesturePlugin().plugin(), 2u,
            &playing)
        && loopCount == 1.0 && recording == 0.0 && playing == 0.0
        && engine.isPrepared()
        && engine.noInputPlugin().saveState(noInputAfterGestureSession)
        && noInputAfterGestureSession == noInputBeforeGestureSession;
    ok = ok && gestureSession->clear(engine.gesturePlugin().plugin())
        && gestureParams->get_value(engine.gesturePlugin().plugin(), 6u,
            &loopCount)
        && gestureParams->get_value(engine.gesturePlugin().plugin(), 1u,
            &recording)
        && gestureParams->get_value(engine.gesturePlugin().plugin(), 2u,
            &playing)
        && loopCount == 0.0 && recording == 0.0 && playing == 0.0
        && renderMode(engine,
            s3g::standalone::NoInputOutputMode::StereoAutogain, 0u);

    engine.setAudioEnabled(false);
    std::array<std::array<float, kFrames>, kChannels> mutedStorage {};
    std::array<float*, kChannels> mutedPointers {};
    for (uint32_t channel = 0u; channel < kChannels; ++channel)
        mutedPointers[channel] = mutedStorage[channel].data();
    for (uint32_t block = 0u; block < 8u; ++block)
        ok = ok && engine.render(mutedPointers.data(), kChannels, kFrames);
    for (const auto& channel : mutedStorage) {
        if (std::any_of(channel.begin(), channel.end(), [](float value) {
                return std::abs(value) > 1.0e-6f;
            })) {
            std::cerr << "Standalone safety mute did not close\n";
            ok = false;
            break;
        }
    }

    const auto engineTelemetry = engine.telemetry();
    ok = ok && engineTelemetry.totalProcessErrorCount() == 0u
        && engineTelemetry.nonFiniteOutputSampleCount == 0u;
    engine.release();
    for (auto& channel : mutedStorage) channel.fill(1.0f);
    ok = ok && !engine.render(mutedPointers.data(), kChannels, kFrames);
    for (const auto& channel : mutedStorage) {
        ok = ok && std::all_of(channel.begin(), channel.end(),
            [](float value) { return value == 0.0f; });
    }
    std::vector<uint8_t> noInputState;
    std::vector<uint8_t> stereoState;
    std::vector<uint8_t> quadState;
    ok = ok && engine.noInputPlugin().saveState(noInputState)
        && engine.stereoPlugin().saveState(stereoState)
        && engine.quadPlugin().saveState(quadState)
        && !noInputState.empty() && !stereoState.empty()
        && !quadState.empty();
    ok = ok && engine.noInputPlugin().loadState(noInputState)
        && engine.stereoPlugin().loadState(stereoState)
        && engine.quadPlugin().loadState(quadState);
    // A diagnostics reset must clear a prior MIDI overflow indication so the
    // GUI fault state does not remain latched until relaunch.
    for (uint32_t message = 0u; message < 4096u; ++message)
        engine.enqueueMidi(0x90u, 60u, 100u);
    ok = ok && engine.midiInputDropCount() > 0u;
    engine.resetMidiInputDropCount();
    ok = ok && engine.midiInputDropCount() == 0u;
    engine.destroy();
    if (!ok) return 1;
    std::cout << "No Input Mixer standalone chain passed\n";
    return 0;
}
