#include "s3g_drum_mixer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>

namespace {

constexpr double kSampleRate = 48000.0;
using Frame = std::array<float, s3g::kDrumMixerChannelCount>;

bool close(float actual, float expected, float tolerance = 1.0e-5f)
{
    return std::abs(actual - expected) <= tolerance;
}

Frame processConstant(s3g::DrumMixer& mixer, const Frame& input,
    uint32_t frames = 2048u)
{
    Frame output {};
    for (uint32_t frame = 0u; frame < frames; ++frame) {
        mixer.processFrame(input, output);
    }
    return output;
}

float renderBandRms(s3g::DrumMixerParams params, float frequency)
{
    s3g::DrumMixer mixer;
    mixer.setParams(params);
    mixer.prepare(kSampleRate);
    Frame input {};
    Frame output {};
    double sum = 0.0;
    constexpr uint32_t frames = 24000u;
    for (uint32_t frame = 0u; frame < frames; ++frame) {
        const float value = 0.1f * std::sin(static_cast<float>(
            2.0 * s3g::kPi * frequency * frame / kSampleRate));
        input[0u] = value;
        input[1u] = value;
        mixer.processFrame(input, output);
        if (frame >= frames / 2u) {
            sum += static_cast<double>(output[0u]) * output[0u];
        }
    }
    return static_cast<float>(std::sqrt(sum / (frames / 2u)));
}

} // namespace

int main()
{
    s3g::DrumMixer mixer;
    mixer.prepare(kSampleRate);

    // SUM places only the stereo master on outputs 1/2.
    Frame input {};
    input[0u] = 0.20f;
    input[1u] = -0.10f;
    input[4u] = 0.05f;
    input[5u] = 0.15f;
    const Frame summed = processConstant(mixer, input, 1u);
    const float master = s3g::dbToGain(-6.0f);
    bool ok = close(summed[0u], 0.25f * master)
        && close(summed[1u], 0.05f * master);
    for (uint32_t channel = 2u;
         channel < s3g::kDrumMixerChannelCount; ++channel) {
        ok = ok && close(summed[channel], 0.0f);
    }
    if (!ok) {
        std::cerr << "SUM routing did not use only outputs 1/2\n";
        return 1;
    }

    // DIRECT returns post-strip pairs and deliberately ignores the master.
    s3g::DrumMixerParams directParams;
    directParams.outputMode = s3g::DrumMixerOutputMode::Direct;
    directParams.masterLevelDb = -60.0f;
    mixer.setParams(directParams);
    mixer.prepare(kSampleRate);
    const Frame direct = processConstant(mixer, input, 1u);
    for (uint32_t channel = 0u;
         channel < s3g::kDrumMixerChannelCount; ++channel) {
        if (!close(direct[channel], input[channel])) {
            std::cerr << "DIRECT routing changed channel " << channel << "\n";
            return 1;
        }
    }

    // Mute and solo operate on complete stereo lanes.
    auto laneParams = directParams;
    mixer.setParams(laneParams);
    mixer.prepare(kSampleRate);
    const Frame beforeMute = processConstant(mixer, input, 64u);
    laneParams.lanes[0u].mute = true;
    mixer.setParams(laneParams);
    Frame firstMuted {};
    mixer.processFrame(input, firstMuted);
    if (!(firstMuted[0u] > 0.0f
            && std::abs(firstMuted[0u] - beforeMute[0u]) < 0.01f)) {
        std::cerr << "Lane mute did not begin with a softened transition\n";
        return 1;
    }
    const Frame muted = processConstant(mixer, input, 4096u);
    if (!close(muted[0u], 0.0f) || !close(muted[1u], 0.0f)
        || close(muted[4u], 0.0f)) {
        std::cerr << "Lane mute did not isolate its stereo pair\n";
        return 1;
    }
    laneParams.lanes[0u].mute = false;
    mixer.setParams(laneParams);
    Frame firstUnmuted {};
    mixer.processFrame(input, firstUnmuted);
    if (!(firstUnmuted[0u] > 0.0f && firstUnmuted[0u] < 0.01f)) {
        std::cerr << "Lane unmute did not begin with a softened transition\n";
        return 1;
    }
    const Frame unmuted = processConstant(mixer, input, 4096u);
    laneParams.lanes[2u].solo = true;
    mixer.setParams(laneParams);
    Frame firstSoloed {};
    mixer.processFrame(input, firstSoloed);
    if (!(firstSoloed[0u] > 0.0f
            && std::abs(firstSoloed[0u] - unmuted[0u]) < 0.01f)) {
        std::cerr << "Lane solo did not begin with a softened transition\n";
        return 1;
    }
    const Frame soloed = processConstant(mixer, input, 4096u);
    if (!close(soloed[0u], 0.0f) || !close(soloed[1u], 0.0f)
        || close(soloed[4u], 0.0f) || close(soloed[5u], 0.0f)) {
        std::cerr << "Lane solo did not suppress non-solo lanes\n";
        return 1;
    }

    // Stereo pan is a balance control: hard right suppresses the left side.
    auto panParams = directParams;
    panParams.lanes[0u].pan = 1.0f;
    mixer.setParams(panParams);
    mixer.prepare(kSampleRate);
    Frame centeredInput {};
    centeredInput[0u] = 0.25f;
    centeredInput[1u] = 0.25f;
    const Frame panned = processConstant(mixer, centeredInput, 1u);
    if (!close(panned[0u], 0.0f) || !close(panned[1u], 0.25f)) {
        std::cerr << "Lane pan did not balance its stereo pair\n";
        return 1;
    }

    // The broad three-band EQ must have meaningful spectral selectivity.
    s3g::DrumMixerParams flat;
    flat.masterLevelDb = 0.0f;
    auto lowBoost = flat;
    lowBoost.lanes[0u].lowEqDb = 12.0f;
    const float flatLow = renderBandRms(flat, 60.0f);
    const float boostedLow = renderBandRms(lowBoost, 60.0f);
    const float flatHigh = renderBandRms(flat, 10000.0f);
    const float boostedHigh = renderBandRms(lowBoost, 10000.0f);
    if (!(boostedLow > flatLow * 2.5f
            && boostedHigh < flatHigh * 1.35f)) {
        std::cerr << "Three-band EQ did not isolate the low band\n";
        return 1;
    }

    // The mid bell has an independently tunable logarithmic center frequency.
    auto midBoost = flat;
    midBoost.lanes[0u].midEqDb = 12.0f;
    midBoost.lanes[0u].midFrequencyHz = 1000.0f;
    const float flatMid = renderBandRms(flat, 1000.0f);
    const float boostedMid = renderBandRms(midBoost, 1000.0f);
    const float boostedFar = renderBandRms(midBoost, 100.0f);
    auto shiftedMid = midBoost;
    shiftedMid.lanes[0u].midFrequencyHz = 250.0f;
    const float shiftedCenter = renderBandRms(shiftedMid, 250.0f);
    const float unshiftedAt250 = renderBandRms(midBoost, 250.0f);
    if (!(boostedMid > flatMid * 2.8f
            && boostedFar < renderBandRms(flat, 100.0f) * 1.7f
            && shiftedCenter > unshiftedAt250 * 1.5f)) {
        std::cerr << "Tunable mid EQ did not track its center frequency\n";
        return 1;
    }

    // AUX is a parallel, pre-master return and must change the SUM without
    // leaking into the direct pairs.
    s3g::DrumMixerParams busOff;
    busOff.masterLevelDb = 0.0f;
    busOff.lanes[0u].auxSend = 1.0f;
    mixer.setParams(busOff);
    mixer.prepare(kSampleRate);
    const Frame dry = processConstant(mixer, centeredInput, 4096u);
    auto busOn = busOff;
    busOn.busEnabled = true;
    busOn.busReturnDb = 0.0f;
    busOn.busDrive = 0.75f;
    busOn.busGlue = 0.70f;
    mixer.setParams(busOn);
    const Frame wet = processConstant(mixer, centeredInput, 4096u);
    if (close(wet[0u], dry[0u], 1.0e-3f)
        || mixer.busActivity() <= 0.0f) {
        std::cerr << "Enabled AUX drum bus produced no return\n";
        return 1;
    }
    busOn.outputMode = s3g::DrumMixerOutputMode::Direct;
    mixer.setParams(busOn);
    mixer.prepare(kSampleRate);
    const Frame wetDirect = processConstant(mixer, centeredInput, 4096u);
    if (!close(wetDirect[0u], centeredInput[0u], 1.0e-4f)
        || !close(wetDirect[1u], centeredInput[1u], 1.0e-4f)) {
        std::cerr << "AUX return leaked into DIRECT routing\n";
        return 1;
    }

    // Public setters sanitize hostile state and the audio boundary stays
    // finite even when a host supplies invalid samples.
    s3g::DrumMixerParams hostile;
    hostile.masterLevelDb = std::numeric_limits<float>::infinity();
    hostile.busRoom = std::numeric_limits<float>::quiet_NaN();
    hostile.lanes[0u].pan = 9.0f;
    hostile.lanes[0u].lowEqDb = -99.0f;
    hostile.lanes[0u].midFrequencyHz =
        std::numeric_limits<float>::infinity();
    mixer.setParams(hostile);
    const auto sanitized = mixer.params();
    if (!close(sanitized.masterLevelDb, -6.0f)
        || !close(sanitized.busRoom, 0.0f)
        || !close(sanitized.lanes[0u].pan, 1.0f)
        || !close(sanitized.lanes[0u].lowEqDb, -12.0f)
        || !close(sanitized.lanes[0u].midFrequencyHz, 900.0f)) {
        std::cerr << "Drum mixer parameter sanitation failed\n";
        return 1;
    }
    mixer.prepare(kSampleRate);
    Frame hostileInput {};
    hostileInput[0u] = std::numeric_limits<float>::quiet_NaN();
    hostileInput[1u] = std::numeric_limits<float>::infinity();
    Frame hostileOutput {};
    mixer.processFrame(hostileInput, hostileOutput);
    for (float value : hostileOutput) {
        if (!std::isfinite(value)) {
            std::cerr << "Drum mixer emitted non-finite audio\n";
            return 1;
        }
    }

    std::cout << "drum mixer smoke passed\n";
    return 0;
}
