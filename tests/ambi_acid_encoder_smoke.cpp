#include "s3g_ambi_acid_encoder.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>

namespace {

constexpr double kSampleRate = 48000.0;

bool parameterAndPatternProbe()
{
    s3g::AmbiAcidEncoder acid;
    acid.prepare(kSampleRate);
    auto params = acid.params();
    params.order = 99u;
    params.tempoBpm = std::numeric_limits<float>::quiet_NaN();
    params.cutoffHz = std::numeric_limits<float>::infinity();
    params.rootMidiNote = -200;
    params.patternLength = 0u;
    params.wakeMs = -1.0f;
    params.fieldListenMode = static_cast<s3g::AmbiFieldListenMode>(99u);
    acid.setParams(params);
    params = acid.params();
    if (params.order != 3u || params.tempoBpm != 126.0f
        || params.cutoffHz != 310.0f || params.rootMidiNote != 12
        || params.patternLength != 1u || params.wakeMs != 5.0f
        || params.fieldListenMode != s3g::AmbiFieldListenMode::Balance) {
        std::cerr << "acid parameter sanitization failed\n";
        return false;
    }

    acid.setStep(3u, { 200, true, true, true });
    const auto step = acid.step(3u);
    if (step.semitoneOffset != 36 || !step.gate
        || !step.accent || !step.slide) {
        std::cerr << "acid step sanitization failed\n";
        return false;
    }
    std::array<float, 20u> oversizedFrame {};
    oversizedFrame.fill(1.0f);
    acid.processFrame(oversizedFrame.data(),
        static_cast<uint32_t>(oversizedFrame.size()));
    for (uint32_t channel = s3g::kAmbiAcidChannels;
         channel < oversizedFrame.size(); ++channel) {
        if (oversizedFrame[channel] != 0.0f) {
            std::cerr << "acid did not clear channels beyond its HOA bus\n";
            return false;
        }
    }
    return true;
}

bool factoryPatternProbe()
{
    if (s3g::kAmbiAcidPatternPresets.size() < 10u) {
        std::cerr << "acid factory pattern bank is too small\n";
        return false;
    }
    for (uint32_t preset = 0u;
         preset < s3g::kAmbiAcidPatternPresets.size(); ++preset) {
        const auto& candidate = s3g::kAmbiAcidPatternPresets[preset];
        if (!candidate.name || candidate.name[0] == '\0') return false;
        uint32_t gates = 0u;
        for (const auto& step : candidate.steps) {
            if (step.semitoneOffset < -36 || step.semitoneOffset > 36) {
                std::cerr << "acid factory pattern note is out of range\n";
                return false;
            }
            gates += step.gate ? 1u : 0u;
        }
        if (gates == 0u) {
            std::cerr << "acid factory pattern contains no gates\n";
            return false;
        }
        for (uint32_t other = 0u; other < preset; ++other) {
            bool sameSteps = true;
            for (uint32_t step = 0u; step < candidate.steps.size(); ++step) {
                const auto& a = candidate.steps[step];
                const auto& b =
                    s3g::kAmbiAcidPatternPresets[other].steps[step];
                sameSteps = sameSteps
                    && a.semitoneOffset == b.semitoneOffset
                    && a.gate == b.gate
                    && a.accent == b.accent
                    && a.slide == b.slide;
            }
            if (std::strcmp(candidate.name,
                    s3g::kAmbiAcidPatternPresets[other].name) == 0
                || sameSteps) {
                std::cerr << "acid factory patterns are not unique\n";
                return false;
            }
        }
    }
    return true;
}

bool hostTransportClockProbe()
{
    s3g::AmbiAcidEncoder acid;
    acid.prepare(kSampleRate);
    auto params = acid.params();
    params.stepsPerBeat = 4u;
    params.patternLength = 16u;
    acid.setParams(params);
    std::array<float, s3g::kAmbiAcidChannels> frame {};

    acid.processFrameSynced(frame.data(), frame.size(), 2.25, true);
    if (acid.currentStep() != 9u) {
        std::cerr << "acid host clock did not select beat-position step 9\n";
        return false;
    }
    acid.processFrameSynced(frame.data(), frame.size(), 0.75, true);
    if (acid.currentStep() != 3u) {
        std::cerr << "acid host clock did not follow a backward seek\n";
        return false;
    }
    acid.processFrameSynced(frame.data(), frame.size(), -0.25, true);
    if (acid.currentStep() != 15u) {
        std::cerr << "acid host clock did not wrap negative pre-roll\n";
        return false;
    }
    acid.reset();
    acid.processFrameSynced(frame.data(), frame.size(), 4.0, false);
    double stoppedEnergy = 0.0;
    for (const float value : frame) {
        if (!std::isfinite(value)) return false;
        stoppedEnergy += static_cast<double>(value) * value;
    }
    acid.processFrameSynced(frame.data(), frame.size(), 4.0, true);
    if (acid.currentStep() != 0u || acid.completedSteps() != 1u
        || stoppedEnergy > 1.0e-10) {
        std::cerr << "acid host clock stop/restart contract failed\n";
        return false;
    }
    return true;
}

bool clockGateAndPitchProbe()
{
    s3g::AmbiAcidEncoder acid;
    acid.prepare(kSampleRate);
    auto params = acid.params();
    params.tempoBpm = 120.0f;
    params.stepsPerBeat = 4u;
    params.patternLength = 16u;
    params.outputGainDb = -14.0f;
    acid.setParams(params);

    std::array<float, s3g::kAmbiAcidChannels> frame {};
    float peak = 0.0f;
    double energy = 0.0;
    float restMinimumEnvelope = 1.0f;
    bool visitedRest = false;
    uint32_t transitions = 0u;
    uint32_t previousStep = 0u;
    // Include the sample at exactly one second so the half-open render range
    // has advanced into its ninth sixteenth-note cell.
    for (uint32_t sample = 0u; sample < 48001u; ++sample) {
        acid.processFrame(frame.data(), static_cast<uint32_t>(frame.size()));
        if (acid.currentStep() != previousStep) {
            ++transitions;
            previousStep = acid.currentStep();
        }
        if (acid.currentStep() == 7u) {
            visitedRest = true;
            restMinimumEnvelope = std::min(
                restMinimumEnvelope, acid.amplitudeEnvelope());
        }
        for (const float value : frame) {
            if (!std::isfinite(value)) {
                std::cerr << "acid voice produced a non-finite sample\n";
                return false;
            }
            peak = std::max(peak, std::fabs(value));
            energy += static_cast<double>(value) * value;
        }
    }
    if (transitions != 8u || acid.completedSteps() != 9u
        || !visitedRest || restMinimumEnvelope > 0.025f
        || peak < 0.01f || peak > 0.961f || energy < 0.01) {
        std::cerr << "acid clock/gate/level contract failed: transitions="
                  << transitions << " completed=" << acid.completedSteps()
                  << " rest-env=" << restMinimumEnvelope
                  << " peak=" << peak << " energy=" << energy << "\n";
        return false;
    }

    // The ninth begun step is pattern step 8: root C2, tied toward step 9.
    const float c2 = 65.4064f;
    if (std::fabs(acid.targetFrequencyHz() - c2) > 0.5f) {
        std::cerr << "acid pattern pitch was not rooted at C2: "
                  << acid.targetFrequencyHz() << " Hz\n";
        return false;
    }
    return true;
}

bool slideProbe()
{
    s3g::AmbiAcidEncoder acid;
    acid.prepare(kSampleRate);
    auto params = acid.params();
    params.tempoBpm = 60.0f;
    params.stepsPerBeat = 1u;
    params.patternLength = 2u;
    params.slideMs = 120.0f;
    acid.setParams(params);
    std::array<s3g::AmbiAcidStep, s3g::kAmbiAcidStepCount> pattern {};
    pattern[0] = { 0, true, false, true };
    pattern[1] = { 12, true, false, false };
    acid.setPattern(pattern);

    std::array<float, s3g::kAmbiAcidChannels> frame {};
    while (acid.currentStep() == 0u && acid.completedSteps() <= 1u) {
        acid.processFrame(frame.data(), static_cast<uint32_t>(frame.size()));
    }
    const float firstSlideFrequency = acid.currentFrequencyHz();
    for (uint32_t sample = 0u; sample < 4800u; ++sample) {
        acid.processFrame(frame.data(), static_cast<uint32_t>(frame.size()));
    }
    const float settledSlideFrequency = acid.currentFrequencyHz();
    if (firstSlideFrequency < 65.0f || firstSlideFrequency > 69.0f
        || settledSlideFrequency < 126.0f
        || settledSlideFrequency > 132.0f) {
        std::cerr << "acid slide did not continuously reach its target: "
                  << firstSlideFrequency << " -> "
                  << settledSlideFrequency << " Hz\n";
        return false;
    }
    return true;
}

struct SpatialMetrics {
    double wEnergy = 0.0;
    double directionalEnergy = 0.0;
    double highOrderEnergy = 0.0;
    float maximumWake = 0.0f;
    float listenerActivity = 0.0f;
    float peak = 0.0f;
    s3g::Vec3 target {};
};

SpatialMetrics renderSpatial(s3g::AmbiFieldListenMode mode,
    uint32_t order, float wakeAmount, uint32_t frames = 144000u)
{
    s3g::AmbiAcidEncoder acid;
    acid.prepare(kSampleRate);
    auto params = acid.params();
    params.order = order;
    params.fieldListenMode = mode;
    params.fieldListenAmount = 1.0f;
    params.wakeAmount = wakeAmount;
    params.outputGainDb = -14.0f;
    acid.setParams(params);
    std::array<float, s3g::kAmbiAcidChannels> frame {};
    SpatialMetrics metrics;
    for (uint32_t sample = 0u; sample < frames; ++sample) {
        acid.processFrame(frame.data(), static_cast<uint32_t>(frame.size()));
        metrics.wEnergy += static_cast<double>(frame[0]) * frame[0];
        for (uint32_t channel = 1u; channel < frame.size(); ++channel) {
            const double energy = static_cast<double>(frame[channel])
                * frame[channel];
            metrics.directionalEnergy += energy;
            if (channel >= 4u) metrics.highOrderEnergy += energy;
            metrics.peak = std::max(metrics.peak, std::fabs(frame[channel]));
        }
        metrics.maximumWake = std::max(
            metrics.maximumWake, acid.wakeEnergy());
        metrics.listenerActivity = std::max(
            metrics.listenerActivity, acid.fieldListenActivity());
        metrics.target = acid.targetDirection();
    }
    return metrics;
}

bool ambisonicWakeAndOrderProbe()
{
    const auto thirdOrder = renderSpatial(
        s3g::AmbiFieldListenMode::Off, 3u, 0.70f, 96000u);
    const auto firstOrder = renderSpatial(
        s3g::AmbiFieldListenMode::Off, 1u, 0.70f, 48000u);
    const auto noWake = renderSpatial(
        s3g::AmbiFieldListenMode::Off, 3u, 0.0f, 48000u);
    if (thirdOrder.wEnergy < 1.0e-6
        || thirdOrder.directionalEnergy / thirdOrder.wEnergy < 0.10
        || thirdOrder.highOrderEnergy < 1.0e-6
        || firstOrder.highOrderEnergy != 0.0
        || thirdOrder.maximumWake < 1.0e-5f
        || noWake.maximumWake != 0.0f
        || thirdOrder.peak > 0.961f) {
        std::cerr << "acid ambisonic/wake contract failed: dir/W="
                  << thirdOrder.directionalEnergy
                        / std::max(1.0e-12, thirdOrder.wEnergy)
                  << " high=" << thirdOrder.highOrderEnergy
                  << " first-high=" << firstOrder.highOrderEnergy
                  << " wake=" << thirdOrder.maximumWake
                  << " dry-wake=" << noWake.maximumWake
                  << " peak=" << thirdOrder.peak << "\n";
        return false;
    }
    return true;
}

float directionDistance(s3g::Vec3 a, s3g::Vec3 b)
{
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    const float dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

bool listenerProbe()
{
    const auto open = renderSpatial(
        s3g::AmbiFieldListenMode::Off, 3u, 0.52f);
    const auto follow = renderSpatial(
        s3g::AmbiFieldListenMode::Follow, 3u, 0.52f);
    const auto counter = renderSpatial(
        s3g::AmbiFieldListenMode::Counter, 3u, 0.52f);
    const auto balance = renderSpatial(
        s3g::AmbiFieldListenMode::Balance, 3u, 0.52f);
    const float followCounter = directionDistance(
        follow.target, counter.target);
    const float openBalance = directionDistance(open.target, balance.target);
    if (follow.listenerActivity < 0.25f
        || followCounter < 0.10f || openBalance < 0.05f) {
        std::cerr << "acid Listener Mode did not steer the next gesture: "
                  << "activity=" << follow.listenerActivity
                  << " follow/counter=" << followCounter
                  << " open/balance=" << openBalance << "\n";
        return false;
    }
    return true;
}

bool deterministicProbe()
{
    s3g::AmbiAcidEncoder a;
    s3g::AmbiAcidEncoder b;
    a.prepare(kSampleRate);
    b.prepare(kSampleRate);
    auto params = a.params();
    params.fieldListenMode = s3g::AmbiFieldListenMode::Balance;
    a.setParams(params);
    b.setParams(params);
    std::array<float, s3g::kAmbiAcidChannels> frameA {};
    std::array<float, s3g::kAmbiAcidChannels> frameB {};
    float maximumDifference = 0.0f;
    for (uint32_t sample = 0u; sample < 96000u; ++sample) {
        a.processFrame(frameA.data(), static_cast<uint32_t>(frameA.size()));
        b.processFrame(frameB.data(), static_cast<uint32_t>(frameB.size()));
        for (uint32_t channel = 0u; channel < frameA.size(); ++channel) {
            if (!std::isfinite(frameA[channel])
                || !std::isfinite(frameB[channel])) {
                std::cerr << "acid repeatability render became non-finite\n";
                return false;
            }
            maximumDifference = std::max(maximumDifference,
                std::fabs(frameA[channel] - frameB[channel]));
        }
    }
    if (maximumDifference > 1.0e-4f) {
        std::cerr << "acid listener feedback was not repeatable: max delta="
                  << maximumDifference << "\n";
        return false;
    }
    return true;
}

} // namespace

int main()
{
    if (!parameterAndPatternProbe()
        || !factoryPatternProbe()
        || !hostTransportClockProbe()
        || !clockGateAndPitchProbe()
        || !slideProbe()
        || !ambisonicWakeAndOrderProbe()
        || !listenerProbe()
        || !deterministicProbe()) {
        return 1;
    }
    std::cout << "ambi acid encoder smoke passed\n";
    return 0;
}
