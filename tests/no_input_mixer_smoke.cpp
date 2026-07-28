#include "s3g_no_input_mixer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <iostream>
#include <limits>

namespace {

using Frame = std::array<float, s3g::kNoInputMixerChannels>;

struct RenderStats {
    std::array<double, s3g::kNoInputMixerChannels> sumSquares {};
    std::array<double, s3g::kNoInputMixerChannels> differences {};
    float peak = 0.0f;
    uint64_t samples = 0u;
    bool finite = true;
};

RenderStats render(s3g::NoInputMixer& mixer, uint32_t frames,
    uint32_t skip = 0u)
{
    RenderStats stats;
    Frame frame {};
    Frame previous {};
    for (uint32_t sample = 0u; sample < frames; ++sample) {
        mixer.processFrame(frame.data());
        for (uint32_t lane = 0u; lane < frame.size(); ++lane) {
            const float value = frame[lane];
            if (!std::isfinite(value)) stats.finite = false;
            stats.peak = std::max(stats.peak, std::abs(value));
            if (sample >= skip) {
                stats.sumSquares[lane] += static_cast<double>(value) * value;
                if (lane > 0u) {
                    stats.differences[lane] += std::abs(
                        static_cast<double>(value - frame[0]));
                }
            }
        }
        previous = frame;
    }
    stats.samples = frames > skip ? frames - skip : 0u;
    return stats;
}

bool testSilentReset()
{
    s3g::NoInputMixer mixer;
    mixer.prepare(48000.0);
    mixer.reset();
    const auto stats = render(mixer, 2048u);
    if (!stats.finite || stats.peak != 0.0f) {
        std::cerr << "No Input Mixer reset was not silent: "
                  << stats.peak << "\n";
        return false;
    }
    return true;
}

bool testDefaultEcology()
{
    s3g::NoInputMixer mixer;
    mixer.prepare(48000.0);
    const auto params = s3g::defaultNoInputMixerParams();
    mixer.setParams(params);
    mixer.reseed(params.seed, 0.58f);
    const auto stats = render(mixer, 96000u, 12000u);
    if (!stats.finite || !(stats.peak > 1.0e-5f) || stats.peak > 1.01f) {
        std::cerr << "No Input Mixer default ecology produced invalid peak: "
                  << stats.peak << "\n";
        return false;
    }
    for (uint32_t lane = 0u; lane < s3g::kNoInputMixerChannels; ++lane) {
        const double rms = std::sqrt(stats.sumSquares[lane]
            / static_cast<double>(stats.samples));
        if (!(rms > 1.0e-5) || rms > 1.01) {
            std::cerr << "No Input Mixer lane " << lane + 1u
                      << " had invalid RMS " << rms << "\n";
            return false;
        }
        if (lane > 0u && !(stats.differences[lane] > 0.01)) {
            std::cerr << "No Input Mixer lane " << lane + 1u
                      << " collapsed onto lane 1\n";
            return false;
        }
    }
    return true;
}

bool testDistortionFamilies()
{
    if (std::strcmp(s3g::noInputDistortionName(
            static_cast<s3g::NoInputDistortionType>(1u)), "WOOL") != 0
        || std::strcmp(s3g::noInputMixerFactoryPresetName(3u),
            "WOOL RING") != 0) {
        std::cerr << "No Input Mixer neutral processor names regressed\n";
        return false;
    }
    for (uint32_t type = 1u; type < s3g::kNoInputDistortionTypeCount;
         ++type) {
        auto params = s3g::defaultNoInputMixerParams();
        params.feedback = 0.94f;
        params.outputGainDb = -24.0f;
        for (auto& lane : params.lanes) {
            lane.inserts[0].type =
                static_cast<s3g::NoInputDistortionType>(type);
            lane.inserts[0].gain = 0.68f;
            lane.inserts[0].tone = 0.61f;
            lane.inserts[0].bias = -0.17f;
            lane.inserts[0].levelDb = -9.0f;
            lane.inserts[0].bypass = 0u;
            lane.inserts[1].bypass = 1u;
            lane.inserts[2].bypass = 1u;
        }
        s3g::NoInputMixer mixer;
        mixer.prepare(48000.0);
        mixer.setParams(params);
        mixer.reseed(params.seed + type * 17u, 0.72f);
        const auto stats = render(mixer, 24000u, 2000u);
        if (!stats.finite || !(stats.peak > 1.0e-7f)
            || stats.peak > 1.01f) {
            std::cerr << "No Input Mixer distortion "
                      << s3g::noInputDistortionName(
                             static_cast<s3g::NoInputDistortionType>(type))
                      << " failed: peak " << stats.peak << "\n";
            return false;
        }
    }
    return true;
}

bool testFactoryPresetsAndRandomization()
{
    for (uint32_t preset = 0u;
         preset < s3g::kNoInputMixerFactoryPresetCount; ++preset) {
        const auto params = s3g::noInputMixerFactoryPreset(preset);
        s3g::NoInputMixer mixer;
        mixer.prepare(48000.0);
        mixer.setParams(params);
        mixer.reseed(params.seed, 0.58f);
        const auto stats = render(mixer, 32000u, 3000u);
        if (!stats.finite || !(stats.peak > 1.0e-7f)
            || stats.peak > 1.01f) {
            std::cerr << "No Input Mixer factory preset "
                      << s3g::noInputMixerFactoryPresetName(preset)
                      << " failed: peak " << stats.peak << "\n";
            return false;
        }
    }

    const auto randomA = s3g::randomizedNoInputMixerParams(0x12345678u);
    const auto randomB = s3g::randomizedNoInputMixerParams(0x12345678u);
    const auto randomC = s3g::randomizedNoInputMixerParams(0x87654321u);
    if (randomA.seed != randomB.seed
        || randomA.feedback != randomB.feedback
        || randomA.matrix != randomB.matrix
        || randomA.lanes[3].body != randomB.lanes[3].body
        || (randomA.feedback == randomC.feedback
            && randomA.matrix == randomC.matrix)) {
        std::cerr << "No Input Mixer bounded randomization was not "
                     "deterministic and seed-sensitive\n";
        return false;
    }
    for (const uint32_t seed : { 0x13579bdfu, 0x2468ace0u, 0x10203040u }) {
        const auto params = s3g::randomizedNoInputMixerParams(seed);
        s3g::NoInputMixer mixer;
        mixer.prepare(48000.0);
        mixer.setParams(params);
        mixer.reseed(params.seed, 0.62f);
        const auto stats = render(mixer, 32000u, 3000u);
        if (!stats.finite || !(stats.peak > 1.0e-7f)
            || stats.peak > 1.01f) {
            std::cerr << "No Input Mixer randomized patch failed for seed "
                      << seed << ": peak " << stats.peak << "\n";
            return false;
        }
    }
    return true;
}

bool testSignedMatrixChangesState()
{
    auto positiveParams = s3g::defaultNoInputMixerParams();
    auto negativeParams = positiveParams;
    positiveParams.matrix[1u * s3g::kNoInputMixerChannels] = 0.74f;
    negativeParams.matrix[1u * s3g::kNoInputMixerChannels] = -0.74f;

    s3g::NoInputMixer positive;
    s3g::NoInputMixer negative;
    positive.prepare(48000.0);
    negative.prepare(48000.0);
    positive.setParams(positiveParams);
    negative.setParams(negativeParams);
    positive.reseed(0x51515151u, 0.55f);
    negative.reseed(0x51515151u, 0.55f);

    Frame positiveFrame {};
    Frame negativeFrame {};
    double difference = 0.0;
    float positiveRoutePeak = 0.0f;
    float negativeRoutePeak = 0.0f;
    constexpr uint32_t route = 1u * s3g::kNoInputMixerChannels;
    for (uint32_t sample = 0u; sample < 18000u; ++sample) {
        positive.processFrame(positiveFrame.data());
        negative.processFrame(negativeFrame.data());
        const float positiveRoute = positive.routeSignal(route);
        const float negativeRoute = negative.routeSignal(route);
        if (!std::isfinite(positiveFrame[1])
            || !std::isfinite(negativeFrame[1])
            || !std::isfinite(positiveRoute)
            || !std::isfinite(negativeRoute)) {
            std::cerr << "No Input Mixer signed matrix produced non-finite audio\n";
            return false;
        }
        positiveRoutePeak = std::max(
            positiveRoutePeak, std::abs(positiveRoute));
        negativeRoutePeak = std::max(
            negativeRoutePeak, std::abs(negativeRoute));
        difference += std::abs(static_cast<double>(
            positiveFrame[1] - negativeFrame[1]));
    }
    if (!(difference > 0.01)) {
        std::cerr << "No Input Mixer matrix polarity had no material effect\n";
        return false;
    }
    if (!(positiveRoutePeak > 1.0e-7f)
        || !(negativeRoutePeak > 1.0e-7f)) {
        std::cerr << "No Input Mixer routed-audio telemetry stayed silent\n";
        return false;
    }
    return true;
}

bool testHybridControlEcology()
{
    if (std::abs(s3g::noInputMixerMotionRateHz(0.0f) - 0.05f) > 1.0e-6f
        || std::abs(s3g::noInputMixerMotionRateHz(0.5f) - 0.5f) > 1.0e-5f
        || std::abs(s3g::noInputMixerMotionRateHz(1.0f) - 5.0f) > 1.0e-4f) {
        std::cerr << "No Input Mixer movement rate law is incorrect\n";
        return false;
    }
    const float focused = s3g::noInputMixerMotionGainScale(
        1.0f, 1.0f, 2u, 0.38f);
    const float background = s3g::noInputMixerMotionGainScale(
        0.1f, 1.0f, 2u, 0.38f);
    const float contrastDb = 20.0f * std::log10(background / focused);
    if (std::abs(focused - 1.0f) > 1.0e-6f || contrastDb > -9.0f
        || s3g::noInputMixerMotionGainScale(
            0.0f, 1.0f, 2u, 0.0f) != 1.0f) {
        std::cerr << "No Input Mixer movement contrast is not audible: "
                  << contrastDb << " dB\n";
        return false;
    }

    auto params = s3g::noInputMixerFactoryPreset(9u);
    const auto phaseA = s3g::noInputMixerMotionWeights(params, 0.10f);
    const auto phaseB = s3g::noInputMixerMotionWeights(params, 0.55f);
    if (phaseA == phaseB) {
        std::cerr << "No Input Mixer Matrix movement did not move\n";
        return false;
    }
    bool patchedGainMoved = false;
    for (float weight : phaseA) {
        if (!std::isfinite(weight) || weight < 0.0f || weight > 1.0001f) {
            std::cerr << "No Input Mixer Matrix movement escaped bounds\n";
            return false;
        }
    }
    std::array<float, s3g::kNoInputMixerChannels> peakA {};
    std::array<float, s3g::kNoInputMixerChannels> peakB {};
    std::array<uint32_t, s3g::kNoInputMixerChannels> routeCount {};
    for (uint32_t destination = 0u;
         destination < s3g::kNoInputMixerChannels; ++destination) {
        for (uint32_t source = 0u;
             source < s3g::kNoInputMixerChannels; ++source) {
            const uint32_t index = destination
                * s3g::kNoInputMixerChannels + source;
            if (std::abs(params.matrix[index]) <= 0.001f) continue;
            peakA[source] = std::max(peakA[source], phaseA[index]);
            peakB[source] = std::max(peakB[source], phaseB[index]);
            ++routeCount[source];
        }
    }
    for (uint32_t index = 0u; index < params.matrix.size(); ++index) {
        const float stored = std::abs(params.matrix[index]);
        if (stored <= 0.001f) continue;
        const uint32_t source = index % s3g::kNoInputMixerChannels;
        const float gainA = stored * s3g::noInputMixerMotionGainScale(
            phaseA[index], peakA[source], routeCount[source], params.motion);
        const float gainB = stored * s3g::noInputMixerMotionGainScale(
            phaseB[index], peakB[source], routeCount[source], params.motion);
        if (gainA > stored + 1.0e-5f || gainB > stored + 1.0e-5f) {
            std::cerr << "Matrix movement exceeded stored route gain\n";
            return false;
        }
        patchedGainMoved = patchedGainMoved
            || std::abs(gainA - gainB) > 1.0e-4f;
    }
    if (!patchedGainMoved) {
        std::cerr << "Matrix movement did not modulate patched route gain\n";
        return false;
    }

    const auto varied = s3g::variedNoInputMixerParams(
        params, 0x56415259u, 0.72f);
    for (uint32_t index = 0u; index < params.matrix.size(); ++index) {
        if (params.matrix[index] == 0.0f && varied.matrix[index] != 0.0f) {
            std::cerr << "Preset variance invented a matrix connection\n";
            return false;
        }
    }
    const auto forgotten = s3g::forgottenNoInputMixerParams(
        params, 0x464f5247u);
    bool routeChanged = false;
    for (uint32_t index = 0u; index < params.matrix.size(); ++index) {
        routeChanged = routeChanged
            || params.matrix[index] != forgotten.matrix[index];
    }
    if (!routeChanged
        || forgotten.lanes[2].inserts[0].type
            != params.lanes[2].inserts[0].type) {
        std::cerr << "FORGET did not remain a local routing operation\n";
        return false;
    }

    auto dryParams = params;
    auto auxParams = params;
    for (auto& lane : dryParams.lanes) lane.auxSend = { 0.0f, 0.0f };
    for (auto& lane : auxParams.lanes) lane.auxSend = { 0.72f, 0.58f };
    s3g::NoInputMixer dry;
    s3g::NoInputMixer wet;
    dry.prepare(48000.0);
    wet.prepare(48000.0);
    dry.setParams(dryParams);
    wet.setParams(auxParams);
    dry.reseed(params.seed, 0.58f);
    wet.reseed(params.seed, 0.58f);
    Frame dryFrame {};
    Frame wetFrame {};
    double difference = 0.0;
    for (uint32_t sample = 0u; sample < 24000u; ++sample) {
        dry.processFrame(dryFrame.data());
        wet.processFrame(wetFrame.data());
        for (uint32_t lane = 0u; lane < dryFrame.size(); ++lane) {
            if (!std::isfinite(dryFrame[lane])
                || !std::isfinite(wetFrame[lane])) return false;
            difference += std::abs(static_cast<double>(
                dryFrame[lane] - wetFrame[lane]));
        }
    }
    if (!(difference > 0.01) || !(wet.auxActivity(0u) > 0.0f)) {
        std::cerr << "Aux-return loops had no material DSP effect\n";
        return false;
    }
    return true;
}

bool testPanic()
{
    auto params = s3g::defaultNoInputMixerParams();
    params.feedback = 1.20f;
    s3g::NoInputMixer mixer;
    mixer.prepare(48000.0);
    mixer.setParams(params);
    mixer.reseed(0x50414e49u, 0.85f);
    render(mixer, 4096u);
    mixer.panic();
    const auto faded = render(mixer, 1024u);
    const auto silent = render(mixer, 1024u);
    if (!faded.finite || !silent.finite || silent.peak != 0.0f
        || mixer.containmentState() != s3g::NoInputContainmentState::Quiet) {
        std::cerr << "No Input Mixer PANIC did not clear the graph: faded finite "
                  << faded.finite << ", silent finite " << silent.finite
                  << ", silent peak " << silent.peak << ", state "
                  << static_cast<uint32_t>(mixer.containmentState()) << "\n";
        return false;
    }
    return true;
}

bool testSanitization()
{
    auto params = s3g::defaultNoInputMixerParams();
    params.feedback = std::numeric_limits<float>::infinity();
    params.matrix[0] = std::numeric_limits<float>::quiet_NaN();
    params.lanes[0].midFrequencyHz = -1000.0f;
    params.lanes[0].inserts[0].gain = 9.0f;
    params.lanes[0].inserts[0].type =
        static_cast<s3g::NoInputDistortionType>(999u);
    params.agency = -8.0f;
    params.motionShape = static_cast<s3g::MatrixFlowShape>(999u);
    params.aux[0].feedback = 7.0f;
    params.lanes[0].auxSend[0] = -2.0f;
    const auto clean = s3g::sanitizeNoInputMixerParams(params);
    if (!std::isfinite(clean.feedback) || clean.feedback != 0.82f
        || clean.matrix[0] != 0.0f
        || clean.lanes[0].midFrequencyHz != 80.0f
        || clean.lanes[0].inserts[0].gain != 1.0f
        || clean.lanes[0].inserts[0].type
            != s3g::NoInputDistortionType::Ring
        || clean.agency != 0.0f
        || clean.motionShape != s3g::MatrixFlowShape::Hold
        || clean.aux[0].feedback != 0.96f
        || clean.lanes[0].auxSend[0] != 0.0f) {
        std::cerr << "No Input Mixer parameter sanitization failed\n";
        return false;
    }
    return true;
}

} // namespace

int main()
{
    if (!testSilentReset()) return 1;
    if (!testDefaultEcology()) return 1;
    if (!testDistortionFamilies()) return 1;
    if (!testFactoryPresetsAndRandomization()) return 1;
    if (!testSignedMatrixChangesState()) return 1;
    if (!testHybridControlEcology()) return 1;
    if (!testPanic()) return 1;
    if (!testSanitization()) return 1;
    std::cout << "No Input Mixer smoke passed\n";
    return 0;
}
