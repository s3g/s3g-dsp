#include "s3g_no_input_mixer.h"

#include <algorithm>
#include <array>
#include <cmath>
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
    for (uint32_t sample = 0u; sample < 18000u; ++sample) {
        positive.processFrame(positiveFrame.data());
        negative.processFrame(negativeFrame.data());
        if (!std::isfinite(positiveFrame[1])
            || !std::isfinite(negativeFrame[1])) {
            std::cerr << "No Input Mixer signed matrix produced non-finite audio\n";
            return false;
        }
        difference += std::abs(static_cast<double>(
            positiveFrame[1] - negativeFrame[1]));
    }
    if (!(difference > 0.01)) {
        std::cerr << "No Input Mixer matrix polarity had no material effect\n";
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
    const auto clean = s3g::sanitizeNoInputMixerParams(params);
    if (!std::isfinite(clean.feedback) || clean.feedback != 0.82f
        || clean.matrix[0] != 0.0f
        || clean.lanes[0].midFrequencyHz != 80.0f
        || clean.lanes[0].inserts[0].gain != 1.0f
        || clean.lanes[0].inserts[0].type
            != s3g::NoInputDistortionType::Ring) {
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
    if (!testSignedMatrixChangesState()) return 1;
    if (!testPanic()) return 1;
    if (!testSanitization()) return 1;
    std::cout << "No Input Mixer smoke passed\n";
    return 0;
}
