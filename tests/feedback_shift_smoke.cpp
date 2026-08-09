#include "s3g_feedback_shift.h"
#include "s3g_feedback_shift_presets.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>

namespace {

bool check(bool condition, const char* message)
{
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}

} // namespace

int main()
{
    bool ok = true;
    ok &= check(std::abs(s3g::feedbackShiftFrequencyFromControlNorm(0.5f))
            < 1.0e-7f,
        "frequency taper did not center exactly at zero");
    ok &= check(std::abs(s3g::feedbackShiftFrequencyFromControlNorm(0.51f))
            < 1.0f,
        "frequency taper did not preserve fine control around zero");
    ok &= check(std::abs(s3g::feedbackShiftFrequencyFromControlNorm(0.0f)
            + 6000.0f) < 0.01f
        && std::abs(s3g::feedbackShiftFrequencyFromControlNorm(1.0f)
            - 6000.0f) < 0.01f,
        "frequency taper did not reach both edges");
    for (float frequency : { -6000.0f, -100.0f, -1.0f, 0.0f,
             1.0f, 100.0f, 6000.0f }) {
        const float restored = s3g::feedbackShiftFrequencyFromControlNorm(
            s3g::feedbackShiftFrequencyControlNorm(frequency));
        ok &= check(std::abs(restored - frequency)
                < std::max(0.001f, std::abs(frequency) * 0.0001f),
            "frequency taper inverse was not stable");
    }

    s3g::FeedbackShift clockProbe;
    auto clockParams = clockProbe.params();
    clockParams.pulseSync = 1u;
    clockParams.pulseDivision = 4u;
    clockProbe.setParams(clockParams);
    clockProbe.prepare(48000.0);
    clockProbe.setTransport(123.0, true);
    std::array<float, s3g::kFeedbackShiftChannels> clockOutput {};
    for (uint32_t frame = 0u; frame < 48000u; ++frame) {
        clockProbe.processFrame(clockOutput.data());
    }
    ok &= check(std::abs(clockProbe.pulsePhase() - 0.05f) < 0.001f,
        "host-synchronized quarter-note pulse did not follow tempo");

    s3g::FeedbackShift synth;
    synth.prepare(48000.0);
    synth.setTransport(120.0, true);
    std::array<float, s3g::kFeedbackShiftChannels> output {};
    std::array<float, s3g::kFeedbackShiftChannels> peaks {};
    for (uint32_t frame = 0u; frame < 96000u; ++frame) {
        synth.processFrame(output.data());
        for (uint32_t channel = 0u;
             channel < s3g::kFeedbackShiftChannels; ++channel) {
            ok &= check(std::isfinite(output[channel])
                    && std::abs(output[channel]) <= 1.0f,
                "default matrix produced invalid or unbounded audio");
            peaks[channel] = std::max(peaks[channel],
                std::abs(output[channel]));
        }
    }
    for (float peak : peaks) {
        ok &= check(peak > 1.0e-7f,
            "one or more feedback matrix outputs were silent");
    }

    const auto verifyFold = [&](s3g::FeedbackShiftOutputMode mode,
                                uint32_t activeChannels) {
        s3g::FeedbackShift folded;
        auto params = s3g::defaultFeedbackShiftParams();
        params.outputMode = mode;
        params.outputRotationDeg = 41.0f;
        folded.setParams(params);
        folded.prepare(48000.0);
        folded.strikeAll(1.0f);
        std::array<double, s3g::kFeedbackShiftChannels> energy {};
        for (uint32_t frame = 0u; frame < 8192u; ++frame) {
            folded.processFrame(output.data());
            for (uint32_t channel = 0u;
                 channel < s3g::kFeedbackShiftChannels; ++channel) {
                energy[channel] += static_cast<double>(output[channel])
                    * output[channel];
            }
        }
        double active = 0.0;
        double silent = 0.0;
        for (uint32_t channel = 0u;
             channel < s3g::kFeedbackShiftChannels; ++channel) {
            (channel < activeChannels ? active : silent) += energy[channel];
        }
        return active > 1.0e-8 && silent < 1.0e-20;
    };
    ok &= check(verifyFold(s3g::FeedbackShiftOutputMode::QuadRing, 4u),
        "quad ring fold did not silence outputs five through eight");
    ok &= check(verifyFold(s3g::FeedbackShiftOutputMode::StereoRing, 2u),
        "stereo ring fold did not silence outputs three through eight");

    s3g::FeedbackShift ringZero;
    s3g::FeedbackShift ringRotated;
    auto ringParams = s3g::defaultFeedbackShiftParams();
    ringParams.outputMode = s3g::FeedbackShiftOutputMode::QuadRing;
    ringZero.setParams(ringParams);
    ringParams.outputRotationDeg = 90.0f;
    ringRotated.setParams(ringParams);
    ringZero.prepare(48000.0);
    ringRotated.prepare(48000.0);
    ringZero.strike(0u, 1.0f);
    ringRotated.strike(0u, 1.0f);
    double rotationDifference = 0.0;
    std::array<float, s3g::kFeedbackShiftChannels> zeroOutput {};
    std::array<float, s3g::kFeedbackShiftChannels> rotatedOutput {};
    for (uint32_t frame = 0u; frame < 8192u; ++frame) {
        ringZero.processFrame(zeroOutput.data());
        ringRotated.processFrame(rotatedOutput.data());
        for (uint32_t channel = 0u; channel < 4u; ++channel) {
            rotationDifference += std::abs(
                static_cast<double>(zeroOutput[channel]
                    - rotatedOutput[channel]));
        }
    }
    ok &= check(rotationDifference > 1.0e-5,
        "channel-ring rotation did not move the quad projection");

    for (uint32_t preset = 0u;
         preset < s3g::kFeedbackShiftPresetCount; ++preset) {
        synth.panic();
        synth.setParams(s3g::feedbackShiftPreset(preset));
        synth.strikeAll(0.8f);
        for (uint32_t frame = 0u; frame < 8192u; ++frame) {
            synth.processFrame(output.data());
            for (float sample : output) {
                ok &= check(std::isfinite(sample) && std::abs(sample) <= 1.0f,
                    "built-in preset produced invalid audio");
            }
        }
    }
    for (uint32_t pedal = 0u;
         pedal < s3g::kFeedbackPedalTypeCount; ++pedal) {
        auto pedalProbe = s3g::defaultFeedbackShiftParams();
        pedalProbe.nodes[0u].pedal = static_cast<s3g::FeedbackPedalType>(pedal);
        pedalProbe.nodes[0u].pedalAmount = 0.72f;
        pedalProbe.nodes[0u].pedalTone = 0.63f;
        pedalProbe.nodes[0u].pedalBias = 0.28f;
        pedalProbe.nodes[0u].pedalMix = 1.0f;
        synth.panic();
        synth.setParams(pedalProbe);
        synth.strike(0u, 1.0f);
        for (uint32_t frame = 0u; frame < 4096u; ++frame) {
            synth.processFrame(output.data());
            for (float sample : output) {
                ok &= check(std::isfinite(sample) && std::abs(sample) <= 1.0f,
                    "pedal-bank processor produced invalid audio");
            }
        }
    }
    const auto randomA = s3g::randomFeedbackShiftParams(0x12345678u);
    const auto randomB = s3g::randomFeedbackShiftParams(0x12345678u);
    ok &= check(randomA.nodes[0u].frequencyHz
            == randomB.nodes[0u].frequencyHz
        && randomA.matrix == randomB.matrix,
        "seeded randomization was not reproducible");
    synth.panic();
    synth.setParams(randomA);
    synth.strikeAll(1.0f);
    for (uint32_t frame = 0u; frame < 16384u; ++frame) {
        synth.processFrame(output.data());
        for (float sample : output) {
            ok &= check(std::isfinite(sample) && std::abs(sample) <= 1.0f,
                "constrained random patch escaped its safety ceiling");
        }
    }

    auto wild = synth.params();
    wild.excite = 1.0f;
    wild.pulseDepth = 1.0f;
    wild.pulseSync = 1u;
    wild.pulseDivision = 2u;
    wild.pulseShape = s3g::FeedbackPulseShape::Square;
    wild.outputGainDb = 6.0f;
    wild.matrix.fill(1.0f);
    for (uint32_t node = 0u; node < s3g::kFeedbackShiftChannels; ++node) {
        wild.nodes[node].frequencyHz = node < 4u ? -6000.0f : 6000.0f;
        wild.nodes[node].regeneration = 1.18f;
        wild.nodes[node].color = 1.0f;
        wild.nodes[node].pedal = static_cast<s3g::FeedbackPedalType>(
            node % s3g::kFeedbackPedalTypeCount);
        synth.strike(node, 1.0f);
    }
    synth.setParams(wild);
    float minimumGovernor = 1.0f;
    for (uint32_t frame = 0u; frame < 96000u; ++frame) {
        synth.processFrame(output.data());
        minimumGovernor = std::min(minimumGovernor,
            synth.minimumGovernor());
        for (float sample : output) {
            ok &= check(std::isfinite(sample) && std::abs(sample) <= 1.0f,
                "fully connected wild matrix escaped its safety ceiling");
        }
    }
    ok &= check(minimumGovernor < 0.85f,
        "continuous containment did not respond to a wild matrix");

    auto stopped = synth.params();
    stopped.run = false;
    synth.setParams(stopped);
    for (uint32_t frame = 0u; frame < 96000u; ++frame) {
        synth.processFrame(output.data());
    }
    for (float sample : output) {
        ok &= check(std::abs(sample) < 1.0e-5f,
            "RUN off did not fade every output to silence");
    }

    if (ok) std::cout << "feedback shift smoke tests passed\n";
    return ok ? 0 : 1;
}
