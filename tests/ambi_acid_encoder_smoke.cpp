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

bool musicalScaleProbe()
{
    if (s3g::ambiAcidQuantizeSemitoneOffset(1, 1u) != 0
        || s3g::ambiAcidQuantizeSemitoneOffset(6, 1u) != 5
        || s3g::ambiAcidQuantizeSemitoneOffset(-2, 1u) != -3
        || s3g::ambiAcidMoveScaleDegree(0, 1u, 1) != 2
        || s3g::ambiAcidMoveScaleDegree(0, 1u, -1) != -1) {
        std::cerr << "acid scale quantization contract failed\n";
        return false;
    }

    s3g::AmbiAcidEncoder acid;
    acid.prepare(kSampleRate);
    auto params = acid.params();
    params.rootMidiNote = 36;
    params.stepsPerBeat = 4u;
    params.patternLength = 2u;
    acid.setParams(params);
    acid.setStep(0u, { 1, true, false, false });
    acid.setStep(1u, { 6, true, false, false });
    acid.setScale(1u);
    std::array<float, s3g::kAmbiAcidChannels> frame {};
    acid.processFrameSynced(frame.data(), frame.size(), 0.0, true);
    const float c2 = 65.4064f;
    if (std::fabs(acid.targetFrequencyHz() - c2) > 0.5f) {
        std::cerr << "acid major scale did not quantize step 1 to root\n";
        return false;
    }
    acid.processFrameSynced(frame.data(), frame.size(), 0.25, true);
    const float f2 = 87.3071f;
    if (std::fabs(acid.targetFrequencyHz() - f2) > 0.5f) {
        std::cerr << "acid major scale did not quantize step 2 down to F\n";
        return false;
    }
    acid.setPerformanceRoot(48);
    const float f3 = 174.614f;
    if (!acid.performanceRootActive()
        || acid.effectiveRootMidiNote() != 48
        || std::fabs(acid.targetFrequencyHz() - f3) > 0.5f) {
        std::cerr << "acid performance root did not transpose the sequence\n";
        return false;
    }
    acid.clearPerformanceRoot();
    if (acid.performanceRootActive()
        || acid.effectiveRootMidiNote() != 36
        || std::fabs(acid.targetFrequencyHz() - f2) > 0.5f) {
        std::cerr << "acid performance root did not return to ROOT\n";
        return false;
    }
    acid.setScale(999u);
    if (acid.scale() != s3g::kMusicalScaleCount - 1u) {
        std::cerr << "acid scale selection was not sanitized\n";
        return false;
    }
    return true;
}

bool spatialStepPathProbe()
{
    s3g::AmbiAcidEncoder acid;
    acid.prepare(kSampleRate);
    auto params = acid.params();
    params.patternLength = 2u;
    params.centerAzimuthDeg = 0.0f;
    params.pathTurns = 1.0f;
    params.elevationSpreadDeg = 60.0f;
    params.spatialSpread = 1.0f;
    params.fieldListenMode = s3g::AmbiFieldListenMode::Off;
    acid.setParams(params);
    acid.setSpatialPoint(0u, { 0.0f, 1.0f, 0.0f });
    acid.setSpatialPoint(1u, { -1.0f, 0.0f, 0.5f });
    acid.setSpatialPoint(2u, {
        std::numeric_limits<float>::quiet_NaN(), 9.0f, -9.0f });
    const auto sanitized = acid.spatialPoint(2u);
    if (!std::isfinite(sanitized.x) || sanitized.y != 1.0f
        || sanitized.z != -1.0f) {
        std::cerr << "acid spatial point sanitization failed\n";
        return false;
    }

    std::array<float, s3g::kAmbiAcidChannels> frame {};
    acid.processFrame(frame.data(), static_cast<uint32_t>(frame.size()));
    const auto first = acid.targetDirection();
    if (std::fabs(first.x) > 0.001f || first.y < 0.999f
        || std::fabs(first.z) > 0.001f) {
        std::cerr << "acid step 1 did not use its authored path point\n";
        return false;
    }
    acid.processFrameSynced(frame.data(), frame.size(), 0.25, true);
    const auto second = acid.targetDirection();
    if (second.x > -0.85f || second.z < 0.45f) {
        std::cerr << "acid step 2 did not use its authored height: "
                  << second.x << "," << second.y << "," << second.z
                  << "\n";
        return false;
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

bool voiceExtensionProbe()
{
    if (s3g::kAmbiAcidDriveCircuitCount != 9u
        || std::strcmp(s3g::ambiAcidDriveCircuitName(
                s3g::AmbiAcidDriveCircuit::Shred), "SHRED") != 0
        || std::strcmp(s3g::ambiAcidDriveCircuitName(
                s3g::AmbiAcidDriveCircuit::Diode), "DIODE") != 0) {
        std::cerr << "acid drive circuit bank contract failed\n";
        return false;
    }

    s3g::AmbiAcidEncoder dry;
    s3g::AmbiAcidEncoder sub;
    dry.prepare(kSampleRate);
    sub.prepare(kSampleRate);
    sub.setSubOctave(-2);
    sub.setSubLevel(1.0f);
    std::array<float, s3g::kAmbiAcidChannels> dryFrame {};
    std::array<float, s3g::kAmbiAcidChannels> subFrame {};
    double subDifference = 0.0;
    for (uint32_t sample = 0u; sample < 12000u; ++sample) {
        dry.processFrame(dryFrame.data(), dryFrame.size());
        sub.processFrame(subFrame.data(), subFrame.size());
        subDifference += std::fabs(subFrame[0u] - dryFrame[0u]);
    }
    if (subDifference < 0.1 || sub.subOctave() != -2
        || sub.subLevel() != 1.0f) {
        std::cerr << "acid sub oscillator did not alter the voice\n";
        return false;
    }

    // The sub residual must remain the same when the main voice changes
    // distortion circuit. This proves the sub is recombined after the drive
    // stage instead of merely sounding comparatively clean for one circuit.
    s3g::AmbiAcidEncoder shredMain;
    s3g::AmbiAcidEncoder shredSub;
    s3g::AmbiAcidEncoder diodeMain;
    s3g::AmbiAcidEncoder diodeSub;
    auto configureDrivenVoice = [](s3g::AmbiAcidEncoder& acid,
                                    s3g::AmbiAcidDriveCircuit circuit,
                                    bool enableSub) {
        acid.prepare(kSampleRate);
        auto params = acid.params();
        params.drive = 0.92f;
        params.resonance = 0.68f;
        params.wakeAmount = 0.0f;
        params.outputGainDb = -36.0f;
        acid.setParams(params);
        acid.setDriveCircuit(circuit);
        acid.setDriveMix(1.0f);
        acid.setSubOctave(-2);
        acid.setSubLevel(enableSub ? 1.0f : 0.0f);
    };
    configureDrivenVoice(shredMain,
        s3g::AmbiAcidDriveCircuit::Shred, false);
    configureDrivenVoice(shredSub,
        s3g::AmbiAcidDriveCircuit::Shred, true);
    configureDrivenVoice(diodeMain,
        s3g::AmbiAcidDriveCircuit::Diode, false);
    configureDrivenVoice(diodeSub,
        s3g::AmbiAcidDriveCircuit::Diode, true);
    std::array<float, s3g::kAmbiAcidChannels> shredMainFrame {};
    std::array<float, s3g::kAmbiAcidChannels> shredSubFrame {};
    std::array<float, s3g::kAmbiAcidChannels> diodeMainFrame {};
    std::array<float, s3g::kAmbiAcidChannels> diodeSubFrame {};
    double cleanSubEnergy = 0.0;
    double cleanSubCircuitDelta = 0.0;
    for (uint32_t sample = 0u; sample < 12000u; ++sample) {
        shredMain.processFrame(shredMainFrame.data(), shredMainFrame.size());
        shredSub.processFrame(shredSubFrame.data(), shredSubFrame.size());
        diodeMain.processFrame(diodeMainFrame.data(), diodeMainFrame.size());
        diodeSub.processFrame(diodeSubFrame.data(), diodeSubFrame.size());
        if (sample < 2000u) continue;
        const double shredResidual = shredSubFrame[0u] - shredMainFrame[0u];
        const double diodeResidual = diodeSubFrame[0u] - diodeMainFrame[0u];
        cleanSubEnergy += std::fabs(shredResidual);
        cleanSubCircuitDelta += std::fabs(shredResidual - diodeResidual);
    }
    if (cleanSubEnergy < 0.01
        || cleanSubCircuitDelta > cleanSubEnergy * 0.001) {
        std::cerr << "acid sub oscillator entered the drive circuit: "
                  << cleanSubCircuitDelta << "/" << cleanSubEnergy << "\n";
        return false;
    }

    std::array<double, s3g::kAmbiAcidDriveCircuitCount> signatures {};
    for (uint32_t circuit = 0u;
         circuit < s3g::kAmbiAcidDriveCircuitCount; ++circuit) {
        s3g::AmbiAcidEncoder acid;
        acid.prepare(kSampleRate);
        auto params = acid.params();
        params.drive = 0.78f;
        params.outputGainDb = -16.0f;
        acid.setParams(params);
        acid.setDriveCircuit(
            static_cast<s3g::AmbiAcidDriveCircuit>(circuit));
        acid.setDriveMix(1.0f);
        std::array<float, s3g::kAmbiAcidChannels> frame {};
        for (uint32_t sample = 0u; sample < 10000u; ++sample) {
            acid.processFrame(frame.data(), frame.size());
            for (const float value : frame) {
                if (!std::isfinite(value)) {
                    std::cerr << "acid drive circuit became non-finite\n";
                    return false;
                }
                signatures[circuit] += std::fabs(value)
                    * static_cast<double>(1u + (sample % 17u));
            }
        }
    }
    uint32_t distinct = 0u;
    for (uint32_t circuit = 0u; circuit < signatures.size(); ++circuit) {
        bool unique = true;
        for (uint32_t prior = 0u; prior < circuit; ++prior) {
            unique = unique && std::fabs(
                signatures[circuit] - signatures[prior]) > 0.01;
        }
        distinct += unique ? 1u : 0u;
    }
    if (distinct < 6u) {
        std::cerr << "acid drive circuits were not sonically distinct\n";
        return false;
    }

    s3g::AmbiAcidEncoder mono;
    mono.prepare(kSampleRate);
    mono.setOutputMode(s3g::AmbiAcidOutputMode::DualMono);
    std::array<float, s3g::kAmbiAcidChannels> monoFrame {};
    double monoEnergy = 0.0;
    for (uint32_t sample = 0u; sample < 12000u; ++sample) {
        mono.processFrame(monoFrame.data(), monoFrame.size());
        if (std::fabs(monoFrame[0u] - monoFrame[1u]) > 1.0e-7f) {
            std::cerr << "acid dual-mono channels did not match\n";
            return false;
        }
        for (uint32_t channel = 2u; channel < monoFrame.size(); ++channel) {
            if (monoFrame[channel] != 0.0f) {
                std::cerr << "acid dual-mono mode leaked into HOA channels\n";
                return false;
            }
        }
        monoEnergy += static_cast<double>(monoFrame[0u]) * monoFrame[0u];
    }
    if (monoEnergy < 0.001) {
        std::cerr << "acid dual-mono mode produced no signal\n";
        return false;
    }
    return true;
}

bool lowCutoffDeclickProbe()
{
    s3g::AmbiAcidEncoder acid;
    acid.prepare(kSampleRate);
    auto params = acid.params();
    params.cutoffHz = 30.0f;
    params.resonance = 1.0f;
    params.filterEnvelopeOctaves = 7.0f;
    params.filterDecayMs = 24.0f;
    params.accentAmount = 1.0f;
    params.drive = 0.9f;
    params.gateLength = 1.0f;
    params.tempoBpm = 240.0f;
    params.stepsPerBeat = 4u;
    params.wakeAmount = 0.0f;
    params.fieldListenMode = s3g::AmbiFieldListenMode::Off;
    params.outputGainDb = -14.0f;
    acid.setParams(params);

    std::array<s3g::AmbiAcidStep, s3g::kAmbiAcidStepCount> pattern {};
    for (uint32_t step = 0u; step < pattern.size(); ++step) {
        pattern[step] = {
            step % 2u == 0u ? 0 : 12,
            true,
            step % 4u == 0u,
            false,
        };
    }
    acid.setPattern(pattern);

    std::array<float, s3g::kAmbiAcidChannels> frame {};
    float previousW = 0.0f;
    float previousCutoff = acid.effectiveCutoffHz();
    float maximumSampleDelta = 0.0f;
    float maximumLowCutoffDelta = 0.0f;
    float maximumCutoffStepOctaves = 0.0f;
    uint32_t maximumSampleDeltaIndex = 0u;
    uint32_t maximumCutoffStepIndex = 0u;
    for (uint32_t sample = 0u; sample < 72000u; ++sample) {
        if (sample == 24000u) {
            params.cutoffHz = 12000.0f;
            acid.setParams(params);
        } else if (sample == 36000u) {
            params.cutoffHz = 30.0f;
            acid.setParams(params);
        }
        acid.processFrame(frame.data(), static_cast<uint32_t>(frame.size()));
        if (!std::isfinite(frame[0u])
            || !std::isfinite(acid.effectiveCutoffHz())) {
            std::cerr << "acid low-cutoff render became non-finite\n";
            return false;
        }
        const float sampleDelta = std::fabs(frame[0u] - previousW);
        if (sampleDelta > maximumSampleDelta) {
            maximumSampleDelta = sampleDelta;
            maximumSampleDeltaIndex = sample;
        }
        if (sample < 24000u || sample >= 42000u) {
            maximumLowCutoffDelta = std::max(
                maximumLowCutoffDelta, sampleDelta);
        }
        const float cutoffStepOctaves = std::fabs(std::log2(
            acid.effectiveCutoffHz() / std::max(1.0f, previousCutoff)));
        if (cutoffStepOctaves > maximumCutoffStepOctaves) {
            maximumCutoffStepOctaves = cutoffStepOctaves;
            maximumCutoffStepIndex = sample;
        }
        previousW = frame[0u];
        previousCutoff = acid.effectiveCutoffHz();
    }
    if (maximumCutoffStepOctaves > 0.12f
        || maximumLowCutoffDelta > 0.12f) {
        std::cerr << "acid low-cutoff path was not de-clicked: cutoff-step="
                  << maximumCutoffStepOctaves
                  << " oct @" << maximumCutoffStepIndex
                  << ", sample-step=" << maximumSampleDelta
                  << " @" << maximumSampleDeltaIndex
                  << ", low-cutoff-step=" << maximumLowCutoffDelta << "\n";
        return false;
    }
    return true;
}

struct SpatialMetrics {
    double wEnergy = 0.0;
    double directionalEnergy = 0.0;
    double highOrderEnergy = 0.0;
    float maximumWake = 0.0f;
    float peak = 0.0f;
    s3g::Vec3 target {};
};

SpatialMetrics renderSpatial(
    uint32_t order, float wakeAmount, uint32_t frames = 144000u)
{
    s3g::AmbiAcidEncoder acid;
    acid.prepare(kSampleRate);
    auto params = acid.params();
    params.order = order;
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
        metrics.target = acid.targetDirection();
    }
    return metrics;
}

bool ambisonicWakeAndOrderProbe()
{
    const auto thirdOrder = renderSpatial(3u, 0.70f, 96000u);
    const auto firstOrder = renderSpatial(1u, 0.70f, 48000u);
    const auto noWake = renderSpatial(3u, 0.0f, 48000u);
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

bool listenerRemovalProbe()
{
    s3g::AmbiAcidEncoder off;
    s3g::AmbiAcidEncoder legacyListener;
    off.prepare(kSampleRate);
    legacyListener.prepare(kSampleRate);
    auto params = off.params();
    params.fieldListenMode = s3g::AmbiFieldListenMode::Off;
    off.setParams(params);
    params.fieldListenMode = s3g::AmbiFieldListenMode::Balance;
    params.fieldListenAmount = 1.0f;
    legacyListener.setParams(params);
    std::array<float, s3g::kAmbiAcidChannels> offFrame {};
    std::array<float, s3g::kAmbiAcidChannels> legacyFrame {};
    for (uint32_t sample = 0u; sample < 48000u; ++sample) {
        off.processFrame(offFrame.data(), offFrame.size());
        legacyListener.processFrame(legacyFrame.data(), legacyFrame.size());
        for (uint32_t channel = 0u; channel < offFrame.size(); ++channel) {
            if (offFrame[channel] != legacyFrame[channel]) {
                std::cerr << "acid legacy Listener parameter still affects DSP\n";
                return false;
            }
        }
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
        std::cerr << "acid render was not repeatable: max delta="
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
        || !musicalScaleProbe()
        || !spatialStepPathProbe()
        || !hostTransportClockProbe()
        || !clockGateAndPitchProbe()
        || !slideProbe()
        || !voiceExtensionProbe()
        || !lowCutoffDeclickProbe()
        || !ambisonicWakeAndOrderProbe()
        || !listenerRemovalProbe()
        || !deterministicProbe()) {
        return 1;
    }
    std::cout << "ambi acid encoder smoke passed\n";
    return 0;
}
