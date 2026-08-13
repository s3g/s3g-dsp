#include "s3g_low_frequency_synth.h"
#include "s3g_low_frequency_synth_presets.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>

namespace {

constexpr double kSampleRate = 48000.0;

bool silenceAndSanitizationProbe()
{
    s3g::LowFrequencySynth synth;
    synth.prepare(std::numeric_limits<double>::quiet_NaN());
    float left = 1.0f;
    float right = 1.0f;
    for (uint32_t sample = 0u; sample < 4096u; ++sample) {
        synth.processFrame(left, right);
        if (!std::isfinite(left) || !std::isfinite(right)
            || left != 0.0f || right != 0.0f) {
            std::cerr << "idle LF Synth was not finite silence\n";
            return false;
        }
    }

    s3g::LowFrequencySynthParams invalid;
    invalid.fundamental = std::numeric_limits<float>::infinity();
    invalid.body = -4.0f;
    invalid.releaseSeconds = 200.0f;
    invalid.outputGainDb = std::numeric_limits<float>::quiet_NaN();
    invalid.amplitudeMotionRateHz = std::numeric_limits<float>::infinity();
    invalid.amplitudeMotionDepth = 20.0f;
    invalid.amplitudeMotionClock = std::numeric_limits<float>::infinity();
    invalid.amplitudeMotionDivision = 99.0f;
    invalid.amplitudeMotionPosition =
        std::numeric_limits<float>::quiet_NaN();
    invalid.shredCircuit = 99.0f;
    invalid.shredColor = std::numeric_limits<float>::quiet_NaN();
    synth.setParams(invalid);
    const auto sanitized = synth.params();
    return sanitized.fundamental == 0.96f
        && sanitized.body == 0.0f
        && sanitized.releaseSeconds == 8.0f
        && sanitized.outputGainDb == -8.0f
        && sanitized.amplitudeMotionRateHz == 2.0f
        && sanitized.amplitudeMotionDepth == 1.0f
        && sanitized.amplitudeMotionClock == 1.0f
        && sanitized.amplitudeMotionDivision == 15.0f
        && sanitized.amplitudeMotionPosition == 1.0f
        && sanitized.shred == 0.0f
        && sanitized.shredFeedback == 0.0f
        && sanitized.shredCircuit == 7.0f
        && sanitized.shredColor == 0.55f
        && sanitized.shredMix == 0.0f;
}

bool sustainedPitchAndEnvelopeProbe()
{
    s3g::LowFrequencySynth synth;
    synth.prepare(kSampleRate);
    auto params = synth.params();
    params.fundamental = 1.0f;
    params.body = 0.0f;
    params.upperModeLevel = 0.0f;
    params.processedMix = 0.0f;
    params.valvePreamp = 0.0f;
    params.attackSeconds = 0.002f;
    params.decaySeconds = 0.01f;
    params.sustain = 1.0f;
    params.releaseSeconds = 0.08f;
    params.glideMs = 0.0f;
    params.pitchTransientSemitones = 0.0f;
    params.outputGainDb = -12.0f;
    synth.setParams(params);
    synth.noteOn(33, 1.0f, false); // A1 = 55 Hz.

    uint32_t crossings = 0u;
    float previous = 0.0f;
    float peak = 0.0f;
    constexpr uint32_t start = 12000u;
    constexpr uint32_t end = 60000u;
    for (uint32_t sample = 0u; sample < end; ++sample) {
        float left = 0.0f;
        float right = 0.0f;
        synth.processFrame(left, right);
        if (!std::isfinite(left) || !std::isfinite(right)) {
            std::cerr << "LF Synth produced non-finite fundamental\n";
            return false;
        }
        peak = std::max(peak, std::max(std::fabs(left), std::fabs(right)));
        if (sample >= start && previous <= 0.0f && left > 0.0f) {
            ++crossings;
        }
        previous = left;
    }
    const double measured = static_cast<double>(crossings)
        / (static_cast<double>(end - start) / kSampleRate);
    if (measured < 54.7 || measured > 55.3 || peak < 0.08f
        || peak > 0.40f) {
        std::cerr << "fundamental pitch/level mismatch: " << measured
                  << " Hz / " << peak << "\n";
        return false;
    }

    synth.noteOff();
    for (uint32_t sample = 0u; sample < 5000u; ++sample) {
        float left = 0.0f;
        float right = 0.0f;
        synth.processFrame(left, right);
    }
    if (synth.active() || synth.envelope() != 0.0f) {
        std::cerr << "LF Synth release did not become inactive\n";
        return false;
    }
    float idleLeft = 1.0f;
    float idleRight = 1.0f;
    synth.processFrame(idleLeft, idleRight);
    if (idleLeft != 0.0f || idleRight != 0.0f) {
        std::cerr << "LF Synth retained signal state after host sleep\n";
        return false;
    }
    return true;
}

bool researchMappingProbe()
{
    s3g::LowFrequencySynth synth;
    synth.prepare(kSampleRate);
    auto params = synth.params();
    params.loading = 0.0f;
    params.tensionVariance = 0.0f;
    params.coupling = 0.0f;
    synth.setParams(params);
    const float circular = synth.modeRatio(1u);

    params.loading = 1.0f;
    synth.setParams(params);
    const float loaded = synth.modeRatio(1u);
    if (loaded < circular + 0.25f || std::fabs(loaded - 2.0f) > 0.15f) {
        std::cerr << "composite loading did not harmonicize modal ratios\n";
        return false;
    }

    params.coupling = 0.78f;
    synth.setParams(params);
    const float lowerPair = synth.modeRatio(4u);
    const float upperPair = synth.modeRatio(5u);
    if (!(lowerPair < upperPair
            && std::fabs(upperPair - lowerPair) > 0.005f)) {
        std::cerr << "membrane pair coupling did not split modes\n";
        return false;
    }
    return true;
}

bool perceptualBassWeightProbe()
{
    s3g::LowFrequencySynth synth;
    synth.prepare(kSampleRate);
    auto params = synth.params();
    params.attackSeconds = 0.001f;
    params.decaySeconds = 0.005f;
    params.sustain = 1.0f;
    params.outputGainDb = 0.0f;
    params.pedalCircuit = static_cast<float>(
        s3g::BassPedalCircuit::Bypass);
    synth.setParams(params);
    synth.noteOn(33, 1.0f, false);

    constexpr uint32_t start = 12000u;
    constexpr uint32_t end = 60000u;
    double energy = 0.0;
    double sine[3u] {};
    double cosine[3u] {};
    float peak = 0.0f;
    for (uint32_t sample = 0u; sample < end; ++sample) {
        float left = 0.0f;
        float right = 0.0f;
        synth.processFrame(left, right);
        if (!std::isfinite(left) || !std::isfinite(right)) return false;
        peak = std::max(peak,
            std::max(std::fabs(left), std::fabs(right)));
        if (sample < start) continue;
        const double mono = 0.5 * static_cast<double>(left + right);
        energy += mono * mono;
        for (uint32_t harmonic = 0u; harmonic < 3u; ++harmonic) {
            const double phase = 2.0 * 3.14159265358979323846 * 55.0
                * static_cast<double>(harmonic + 1u)
                * static_cast<double>(sample - start) / kSampleRate;
            sine[harmonic] += mono * std::sin(phase);
            cosine[harmonic] += mono * std::cos(phase);
        }
    }
    const double count = static_cast<double>(end - start);
    const double rms = std::sqrt(energy / count);
    std::array<double, 3u> amplitude {};
    for (uint32_t harmonic = 0u; harmonic < amplitude.size(); ++harmonic) {
        amplitude[harmonic] = 2.0 * std::hypot(
            sine[harmonic], cosine[harmonic]) / count;
    }
    const double support = std::hypot(amplitude[1u], amplitude[2u]);
    if (rms < 0.34 || amplitude[0u] < 0.28 || support < 0.055
        || peak > 0.941f) {
        std::cerr << "unity bass weight mismatch: rms " << rms
                  << ", harmonics " << amplitude[0u] << " / "
                  << amplitude[1u] << " / " << amplitude[2u]
                  << ", peak " << peak << "\n";
        return false;
    }
    return true;
}

bool membraneKickAdaptationProbe()
{
    s3g::LowFrequencySynth ratioProbe;
    ratioProbe.prepare(kSampleRate);
    auto ratioParams = ratioProbe.params();
    ratioParams.loading = 0.0f;
    ratioParams.coupling = 0.0f;
    ratioProbe.setParams(ratioParams);
    constexpr std::array<float, 4u> kickRatios {{
        1.000f, 1.593f, 2.135f, 2.295f
    }};
    constexpr std::array<uint32_t, 4u> kickIndices {{ 0u, 1u, 3u, 4u }};
    for (uint32_t index = 0u; index < kickRatios.size(); ++index) {
        if (std::fabs(ratioProbe.modeRatio(kickIndices[index])
                - kickRatios[index]) > 1.0e-4f) {
            std::cerr << "bass membrane diverged from Membrane Kick ratios\n";
            return false;
        }
    }

    s3g::LowFrequencySynth dark;
    dark.prepare(kSampleRate);
    auto params = dark.params();
    params.fundamental = 0.0f;
    params.body = 1.0f;
    params.loading = 0.0f;
    params.coupling = 0.0f;
    params.excitationPosition = 0.72f;
    params.upperModeLevel = 0.0f;
    params.membraneDrive = 0.0f;
    params.processedMix = 0.0f;
    params.valvePreamp = 0.0f;
    params.attackSeconds = 0.001f;
    params.sustain = 1.0f;
    params.outputGainDb = -6.0f;
    dark.setParams(params);
    auto open = dark;
    params.upperModeLevel = 1.0f;
    open.setParams(params);
    dark.noteOn(45, 1.0f, false);
    open.noteOn(45, 1.0f, false);

    double darkEnergy = 0.0;
    double openEnergy = 0.0;
    double difference = 0.0;
    double darkOnsetDifferenceEnergy = 0.0;
    double openOnsetDifferenceEnergy = 0.0;
    float previousDark = 0.0f;
    float previousOpen = 0.0f;
    for (uint32_t sample = 0u; sample < 48000u; ++sample) {
        float darkLeft = 0.0f;
        float darkRight = 0.0f;
        float openLeft = 0.0f;
        float openRight = 0.0f;
        dark.processFrame(darkLeft, darkRight);
        open.processFrame(openLeft, openRight);
        if (sample < 2400u) {
            const double darkDelta = darkLeft - previousDark;
            const double openDelta = openLeft - previousOpen;
            darkOnsetDifferenceEnergy += darkDelta * darkDelta;
            openOnsetDifferenceEnergy += openDelta * openDelta;
        }
        previousDark = darkLeft;
        previousOpen = openLeft;
        if (sample < 12000u) continue;
        darkEnergy += static_cast<double>(darkLeft) * darkLeft;
        openEnergy += static_cast<double>(openLeft) * openLeft;
        difference += std::fabs(static_cast<double>(openLeft) - darkLeft);
    }
    if (darkEnergy < 1.0e-5 || openEnergy < 1.0e-5
        || std::fabs(std::log(openEnergy / darkEnergy)) < 0.05
        || difference < 1.0
        || openOnsetDifferenceEnergy
            < darkOnsetDifferenceEnergy * 1.5) {
        std::cerr << "upper-mode control did not expose the struck membrane: "
                  << darkEnergy << " / " << openEnergy << " / "
                  << difference << " / onset "
                  << darkOnsetDifferenceEnergy << " / "
                  << openOnsetDifferenceEnergy << "\n";
        return false;
    }
    return true;
}

bool drivenBodyAndProtectedStereoProbe()
{
    s3g::LowFrequencySynth synth;
    synth.prepare(kSampleRate);
    auto params = synth.params();
    params.fundamental = 0.0f;
    params.body = 1.0f;
    params.loading = 0.82f;
    params.coupling = 0.82f;
    params.tensionVariance = 0.52f;
    params.excitationPosition = 0.68f;
    params.upperModeLevel = 1.0f;
    params.damping = 0.34f;
    params.nonlinearity = 0.62f;
    params.attackSeconds = 0.003f;
    params.decaySeconds = 0.02f;
    params.sustain = 1.0f;
    params.stereoWidth = 1.0f;
    params.processedMix = 0.0f;
    params.valvePreamp = 0.0f;
    params.outputGainDb = -9.0f;
    synth.setParams(params);
    synth.noteOn(33, 0.9f, false);

    double midEnergy = 0.0;
    double sideEnergy = 0.0;
    double lowMidEnergy = 0.0;
    double lowSideEnergy = 0.0;
    double lowMid = 0.0;
    double lowSide = 0.0;
    float peak = 0.0f;
    for (uint32_t sample = 0u; sample < 48000u; ++sample) {
        float left = 0.0f;
        float right = 0.0f;
        synth.processFrame(left, right);
        peak = std::max(peak, std::max(std::fabs(left), std::fabs(right)));
        if (sample < 8000u) continue;
        const double mid = (static_cast<double>(left) + right) * 0.5;
        const double side = (static_cast<double>(left) - right) * 0.5;
        midEnergy += mid * mid;
        sideEnergy += side * side;
        lowMid += (mid - lowMid) * 0.007;
        lowSide += (side - lowSide) * 0.007;
        lowMidEnergy += lowMid * lowMid;
        lowSideEnergy += lowSide * lowSide;
    }
    if (peak < 0.01f || peak > 1.0f || midEnergy < 1.0e-4
        || sideEnergy / midEnergy < 1.0e-5) {
        std::cerr << "driven membrane body lacked level or stereo detail: "
                  << peak << " / " << sideEnergy / std::max(1.0e-12,
                      midEnergy) << "\n";
        return false;
    }
    if (lowSideEnergy / std::max(1.0e-12, lowMidEnergy) > 0.12) {
        std::cerr << "protected low band carried excessive stereo energy: "
                  << lowSideEnergy / lowMidEnergy << "\n";
        return false;
    }
    return true;
}

bool glideAndPressureProbe()
{
    s3g::LowFrequencySynth synth;
    synth.prepare(kSampleRate);
    auto params = synth.params();
    params.glideMs = 300.0f;
    params.attackSeconds = 0.001f;
    params.sustain = 1.0f;
    params.pressureSensitivity = 1.0f;
    synth.setParams(params);
    synth.noteOn(33, 0.7f, false);
    for (uint32_t sample = 0u; sample < 24000u; ++sample) {
        float left = 0.0f;
        float right = 0.0f;
        synth.processFrame(left, right);
    }
    synth.noteOn(45, 0.7f, true); // A2 = 110 Hz.
    for (uint32_t sample = 0u; sample < 960u; ++sample) {
        float left = 0.0f;
        float right = 0.0f;
        synth.processFrame(left, right);
    }
    const float early = synth.currentFrequencyHz();
    if (early <= 55.0f || early >= 85.0f) {
        std::cerr << "legato glide jumped or failed: " << early << " Hz\n";
        return false;
    }
    for (uint32_t sample = 0u; sample < 96000u; ++sample) {
        float left = 0.0f;
        float right = 0.0f;
        synth.processFrame(left, right);
    }
    if (std::fabs(synth.currentFrequencyHz() - 110.0f) > 0.25f) {
        std::cerr << "glide did not settle to target pitch\n";
        return false;
    }

    auto unpressured = synth;
    auto pressured = synth;
    pressured.setPressure(1.0f);
    double difference = 0.0;
    for (uint32_t sample = 0u; sample < 12000u; ++sample) {
        float aL = 0.0f;
        float aR = 0.0f;
        float bL = 0.0f;
        float bR = 0.0f;
        unpressured.processFrame(aL, aR);
        pressured.processFrame(bL, bR);
        difference += std::fabs(static_cast<double>(aL) - bL)
            + std::fabs(static_cast<double>(aR) - bR);
    }
    if (difference < 0.01) {
        std::cerr << "pressure did not animate the membrane body\n";
        return false;
    }
    return true;
}

bool filterAndDriveProbe()
{
    s3g::LowFrequencySynth dry;
    dry.prepare(kSampleRate);
    auto dryParams = dry.params();
    dryParams.fundamental = 0.0f;
    dryParams.body = 1.0f;
    dryParams.excitationPosition = 0.68f;
    dryParams.upperModeLevel = 1.0f;
    dryParams.processedMix = 0.0f;
    dryParams.attackSeconds = 0.001f;
    dryParams.sustain = 1.0f;
    dryParams.outputGainDb = -18.0f;
    dry.setParams(dryParams);
    auto processed = dry;
    auto processedParams = dryParams;
    processedParams.filterCutoffHz = 3200.0f;
    processedParams.filterResonance = 0.74f;
    processedParams.filterEnvelopeOctaves = 0.0f;
    processedParams.membraneDrive = 0.92f;
    processedParams.wavefold = 0.82f;
    processedParams.driveFeedback = 0.68f;
    processedParams.processedMix = 1.0f;
    processed.setParams(processedParams);
    dry.noteOn(33, 1.0f, false);
    processed.noteOn(33, 1.0f, false);

    double dryEnergy = 0.0;
    double processedEnergy = 0.0;
    double difference = 0.0;
    double processedVariation = 0.0;
    float previous = 0.0f;
    float peak = 0.0f;
    for (uint32_t sample = 0u; sample < 48000u; ++sample) {
        float dryLeft = 0.0f;
        float dryRight = 0.0f;
        float wetLeft = 0.0f;
        float wetRight = 0.0f;
        dry.processFrame(dryLeft, dryRight);
        processed.processFrame(wetLeft, wetRight);
        if (!std::isfinite(wetLeft) || !std::isfinite(wetRight)) {
            std::cerr << "filter/drive path produced non-finite output\n";
            return false;
        }
        if (sample < 2000u) continue;
        dryEnergy += static_cast<double>(dryLeft) * dryLeft;
        processedEnergy += static_cast<double>(wetLeft) * wetLeft;
        difference += std::fabs(static_cast<double>(wetLeft) - dryLeft);
        processedVariation += std::fabs(
            static_cast<double>(wetLeft) - previous);
        previous = wetLeft;
        peak = std::max(peak, std::fabs(wetLeft));
    }
    if (dryEnergy < 0.001 || processedEnergy < 0.001
        || difference < 1.0 || processedVariation < 1.0
        || peak > 1.0f) {
        std::cerr << "filter/drive path lacked bounded spectral change: "
                  << dryEnergy << " / " << processedEnergy << " / "
                  << difference << " / " << peak << "\n";
        return false;
    }

    // The motion engine must be a VCA only. It may not move the tone filter,
    // even when obsolete hidden filter-envelope values are supplied.
    s3g::LowFrequencySynth motion;
    motion.prepare(kSampleRate);
    auto motionParams = motion.params();
    motionParams.fundamental = 1.0f;
    motionParams.body = 0.0f;
    motionParams.upperModeLevel = 0.0f;
    motionParams.processedMix = 0.0f;
    motionParams.valvePreamp = 0.0f;
    motionParams.attackSeconds = 0.001f;
    motionParams.sustain = 1.0f;
    motionParams.filterCutoffHz = 900.0f;
    motionParams.filterEnvelopeOctaves = 6.0f;
    motionParams.amplitudeMotionRateHz = 4.0f;
    motionParams.amplitudeMotionDepth = 1.0f;
    motionParams.outputGainDb = -18.0f;
    motion.setParams(motionParams);
    motion.noteOn(33, 1.0f, false);

    motion.setAmplitudeMotionTransportPhase(0.0f, true);
    double closedEnergy = 0.0;
    for (uint32_t sample = 0u; sample < 12000u; ++sample) {
        float left = 0.0f;
        float right = 0.0f;
        motion.processFrame(left, right);
        if (sample >= 6000u) closedEnergy += left * left;
    }
    const float closedCutoff = motion.effectiveFilterCutoffHz();
    motion.setAmplitudeMotionTransportPhase(0.5f, true);
    double openEnergy = 0.0;
    for (uint32_t sample = 0u; sample < 12000u; ++sample) {
        float left = 0.0f;
        float right = 0.0f;
        motion.processFrame(left, right);
        if (sample >= 6000u) openEnergy += left * left;
    }
    const float openCutoff = motion.effectiveFilterCutoffHz();
    if (closedEnergy > openEnergy * 0.001
        || openEnergy < 0.001
        || std::fabs(closedCutoff - 900.0f) > 0.5f
        || std::fabs(openCutoff - 900.0f) > 0.5f) {
        std::cerr << "amplitude LFO changed tone or lacked VCA depth: "
                  << closedEnergy << " / " << openEnergy << " / "
                  << closedCutoff << " / " << openCutoff << "\n";
        return false;
    }

    s3g::LowFrequencySynth protectedDry;
    protectedDry.prepare(kSampleRate);
    auto protectedParams = protectedDry.params();
    protectedParams.fundamental = 1.0f;
    protectedParams.body = 0.0f;
    protectedParams.upperModeLevel = 0.0f;
    protectedParams.processedMix = 0.0f;
    protectedParams.attackSeconds = 0.001f;
    protectedParams.sustain = 1.0f;
    protectedParams.outputGainDb = -18.0f;
    protectedDry.setParams(protectedParams);
    auto protectedWet = protectedDry;
    protectedParams.filterCutoffHz = 30.0f;
    protectedParams.filterResonance = 1.0f;
    protectedParams.membraneDrive = 1.0f;
    protectedParams.wavefold = 1.0f;
    protectedParams.driveFeedback = 1.0f;
    protectedParams.processedMix = 1.0f;
    protectedWet.setParams(protectedParams);
    protectedDry.noteOn(33, 1.0f, false);
    protectedWet.noteOn(33, 1.0f, false);
    double subDifference = 0.0;
    for (uint32_t sample = 0u; sample < 12000u; ++sample) {
        float dryLeft = 0.0f;
        float dryRight = 0.0f;
        float wetLeft = 0.0f;
        float wetRight = 0.0f;
        protectedDry.processFrame(dryLeft, dryRight);
        protectedWet.processFrame(wetLeft, wetRight);
        subDifference += std::fabs(static_cast<double>(dryLeft) - wetLeft)
            + std::fabs(static_cast<double>(dryRight) - wetRight);
    }
    if (subDifference > 1.0e-5) {
        std::cerr << "filter/drive path modified the protected fundamental\n";
        return false;
    }
    return true;
}

bool smoothFilterStressProbe()
{
    s3g::LowFrequencySynth synth;
    synth.prepare(kSampleRate);
    auto params = s3g::lowFrequencySynthFactoryPreset(15u);
    params.fundamental = 0.0f;
    params.attackSeconds = 0.001f;
    params.decaySeconds = 0.005f;
    params.sustain = 1.0f;
    params.processedMix = 1.0f;
    params.filterEnvelopeOctaves = 0.0f;
    params.shredFeedback = 0.0f;
    params.shredMix = 0.0f;
    params.outputGainDb = -11.0f;
    synth.setParams(params);
    synth.noteOn(33, 1.0f, false);

    float previous = 0.0f;
    float previousDelta = 0.0f;
    float maximumStep = 0.0f;
    float maximumCurvature = 0.0f;
    double totalStep = 0.0;
    uint32_t largeSteps = 0u;
    uint32_t clampedSamples = 0u;
    for (uint32_t sample = 0u; sample < 48000u; ++sample) {
        if ((sample % 96u) == 0u) {
            params.filterCutoffHz = ((sample / 96u) & 1u) == 0u
                ? 30.0f : 12000.0f;
            synth.setParams(params);
        }
        float left = 0.0f;
        float right = 0.0f;
        synth.processFrame(left, right);
        if (!std::isfinite(left) || !std::isfinite(right)) {
            std::cerr << "filter stress produced non-finite output\n";
            return false;
        }
        if (sample > 1024u) {
            const float delta = left - previous;
            maximumStep = std::max(maximumStep, std::fabs(delta));
            totalStep += std::fabs(delta);
            if (std::fabs(delta) > 0.20f) ++largeSteps;
            maximumCurvature = std::max(maximumCurvature,
                std::fabs(delta - previousDelta));
            previousDelta = delta;
            if (std::fabs(left) >= 0.94f) ++clampedSamples;
        }
        previous = left;
    }
    if (maximumStep > 0.28f || maximumCurvature > 0.21f
        || largeSteps > 60u || clampedSamples != 0u) {
        std::cerr << "filter stress continuity mismatch: step "
                  << maximumStep << ", curvature " << maximumCurvature
                  << ", mean " << totalStep / (48000.0 - 1025.0)
                  << ", large " << largeSteps << ", clamped "
                  << clampedSamples << "\n";
        return false;
    }
    return true;
}

bool directSurfaceThicknessProbe()
{
    auto params = s3g::lowFrequencySynthFactoryPreset(15u);
    if (params.fundamental < 0.95f || params.body < 0.95f
        || params.loading < 0.95f || params.upperModeLevel > 0.60f
        || params.filterResonance > 0.50f || params.membraneDrive > 0.85f
        || params.wavefold != 0.0f || params.driveFeedback != 0.0f
        || params.pedalBlend != 0.0f) {
        std::cerr << "direct surface exceeded clarity limits\n";
        return false;
    }

    s3g::LowFrequencySynth synth;
    synth.prepare(kSampleRate);
    params.attackSeconds = 0.001f;
    params.sustain = 1.0f;
    params.outputGainDb = -8.0f;
    synth.setParams(params);
    synth.noteOn(33, 1.0f, false);
    double sine = 0.0;
    double cosine = 0.0;
    double energy = 0.0;
    float peak = 0.0f;
    constexpr uint32_t start = 12000u;
    constexpr uint32_t end = 60000u;
    for (uint32_t sample = 0u; sample < end; ++sample) {
        float left = 0.0f;
        float right = 0.0f;
        synth.processFrame(left, right);
        if (!std::isfinite(left) || !std::isfinite(right)) return false;
        peak = std::max(peak,
            std::max(std::fabs(left), std::fabs(right)));
        if (sample >= start) {
            const double phase = 2.0 * 3.14159265358979323846 * 55.0
                * static_cast<double>(sample - start) / kSampleRate;
            sine += left * std::sin(phase);
            cosine += left * std::cos(phase);
            energy += static_cast<double>(left) * left;
        }
    }
    const double count = static_cast<double>(end - start);
    const double fundamentalAmplitude = 2.0 * std::hypot(sine, cosine)
        / count;
    const double rms = std::sqrt(energy / count);
    if (fundamentalAmplitude < 0.015 || rms < 0.01
        || fundamentalAmplitude < rms * 0.12 || peak > 0.941f) {
        std::cerr << "maximum density lost the bass foundation: "
                  << fundamentalAmplitude << " / " << rms << " / "
                  << peak << "\n";
        return false;
    }
    return true;
}

bool tubeAudibilityProbe()
{
    s3g::LowFrequencySynth clean;
    clean.prepare(kSampleRate);
    auto params = clean.params();
    params.attackSeconds = 0.001f;
    params.sustain = 1.0f;
    params.valvePreamp = 0.0f;
    params.outputGainDb = -12.0f;
    clean.setParams(params);
    auto tube = clean;
    params.valvePreamp = 1.0f;
    tube.setParams(params);
    clean.noteOn(33, 1.0f, false);
    tube.noteOn(33, 1.0f, false);
    double difference = 0.0;
    double cleanEnergy = 0.0;
    double tubeEnergy = 0.0;
    for (uint32_t sample = 0u; sample < 36000u; ++sample) {
        float cleanLeft = 0.0f;
        float cleanRight = 0.0f;
        float tubeLeft = 0.0f;
        float tubeRight = 0.0f;
        clean.processFrame(cleanLeft, cleanRight);
        tube.processFrame(tubeLeft, tubeRight);
        if (!std::isfinite(tubeLeft) || !std::isfinite(tubeRight)) {
            std::cerr << "tube path produced non-finite output\n";
            return false;
        }
        if (sample < 4000u) continue;
        difference += std::fabs(static_cast<double>(tubeLeft) - cleanLeft);
        cleanEnergy += static_cast<double>(cleanLeft) * cleanLeft;
        tubeEnergy += static_cast<double>(tubeLeft) * tubeLeft;
    }
    if (difference < 50.0 || cleanEnergy < 0.01 || tubeEnergy < 0.01
        || std::fabs(std::log(tubeEnergy / cleanEnergy)) < 0.08) {
        std::cerr << "Tube did not create an evident coordinated response: "
                  << difference << " / " << cleanEnergy << " / "
                  << tubeEnergy << "\n";
        return false;
    }
    return true;
}

bool tubeAutomationContinuityProbe()
{
    s3g::LowFrequencySynth synth;
    synth.prepare(kSampleRate);
    auto params = synth.params();
    params.fundamental = 1.0f;
    params.body = 0.18f;
    params.upperModeLevel = 0.08f;
    params.processedMix = 0.12f;
    params.valvePreamp = 1.0f;
    params.shredMix = 0.0f;
    params.amplitudeMotionDepth = 0.0f;
    params.attackSeconds = 0.001f;
    params.decaySeconds = 0.005f;
    params.sustain = 1.0f;
    params.outputGainDb = -18.0f;
    synth.setParams(params);
    synth.noteOn(33, 1.0f, false);

    float previous = 0.0f;
    float right = 0.0f;
    for (uint32_t sample = 0u; sample < 48000u; ++sample) {
        synth.processFrame(previous, right);
    }
    float steadyStep = 0.0f;
    for (uint32_t sample = 0u; sample < 12000u; ++sample) {
        float left = 0.0f;
        synth.processFrame(left, right);
        steadyStep = std::max(steadyStep, std::fabs(left - previous));
        previous = left;
    }

    params.valvePreamp = 0.0f;
    synth.setParams(params);
    float transitionStep = 0.0f;
    uint32_t transitionSample = 0u;
    for (uint32_t sample = 0u; sample < 24000u; ++sample) {
        float left = 0.0f;
        synth.processFrame(left, right);
        const float step = std::fabs(left - previous);
        if (step > transitionStep) {
            transitionStep = step;
            transitionSample = sample;
        }
        previous = left;
    }
    if (transitionStep > steadyStep * 1.08f + 0.002f) {
        std::cerr << "Tube automation introduced a discontinuity: "
                  << transitionStep << " / steady " << steadyStep
                  << " at sample " << transitionSample << "\n";
        return false;
    }
    return true;
}

bool stereoShredProbe()
{
    s3g::LowFrequencySynth dry;
    dry.prepare(kSampleRate);
    auto params = dry.params();
    params.fundamental = 1.0f;
    params.body = 0.0f;
    params.upperModeLevel = 0.0f;
    params.processedMix = 0.0f;
    params.valvePreamp = 0.0f;
    params.attackSeconds = 0.001f;
    params.decaySeconds = 0.005f;
    params.sustain = 1.0f;
    params.releaseSeconds = 0.005f;
    params.outputGainDb = -18.0f;
    dry.setParams(params);

    auto zeroMix = dry;
    auto zeroMixParams = params;
    zeroMixParams.shred = 1.0f;
    zeroMixParams.shredFeedback = 1.0f;
    zeroMixParams.shredCircuit = static_cast<float>(
        s3g::BassShredCircuit::FuzzII);
    zeroMixParams.shredColor = 1.0f;
    zeroMixParams.shredMix = 0.0f;
    zeroMix.setParams(zeroMixParams);
    dry.noteOn(33, 1.0f, false);
    zeroMix.noteOn(33, 1.0f, false);
    for (uint32_t sample = 0u; sample < 24000u; ++sample) {
        float dryLeft = 0.0f;
        float dryRight = 0.0f;
        float zeroLeft = 0.0f;
        float zeroRight = 0.0f;
        dry.processFrame(dryLeft, dryRight);
        zeroMix.processFrame(zeroLeft, zeroRight);
        if (dryLeft != zeroLeft || dryRight != zeroRight) {
            std::cerr << "zero Shred Mix did not preserve exact dry audio\n";
            return false;
        }
    }

    s3g::LowFrequencySynth clean;
    clean.prepare(kSampleRate);
    clean.setParams(params);
    s3g::LowFrequencySynth wet;
    wet.prepare(kSampleRate);
    auto wetParams = params;
    wetParams.shred = 0.74f;
    wetParams.shredFeedback = 0.72f;
    wetParams.shredCircuit = static_cast<float>(
        s3g::BassShredCircuit::Shred);
    wetParams.shredColor = 0.55f;
    wetParams.shredMix = 1.0f;
    wet.setParams(wetParams);
    clean.noteOn(33, 1.0f, false);
    wet.noteOn(33, 1.0f, false);

    constexpr uint32_t start = 12000u;
    constexpr uint32_t end = 60000u;
    double difference = 0.0;
    double wetSideEnergy = 0.0;
    double cleanSine = 0.0;
    double cleanCosine = 0.0;
    double wetSine = 0.0;
    double wetCosine = 0.0;
    for (uint32_t sample = 0u; sample < end; ++sample) {
        float cleanLeft = 0.0f;
        float cleanRight = 0.0f;
        float wetLeft = 0.0f;
        float wetRight = 0.0f;
        clean.processFrame(cleanLeft, cleanRight);
        wet.processFrame(wetLeft, wetRight);
        if (!std::isfinite(wetLeft) || !std::isfinite(wetRight)) {
            std::cerr << "stereo Shred produced non-finite output\n";
            return false;
        }
        if (sample < start) continue;
        const double phase = 2.0 * 3.14159265358979323846 * 55.0
            * static_cast<double>(sample - start) / kSampleRate;
        const double cleanMid = 0.5
            * static_cast<double>(cleanLeft + cleanRight);
        const double wetMid = 0.5
            * static_cast<double>(wetLeft + wetRight);
        const double wetSide = 0.5
            * static_cast<double>(wetLeft - wetRight);
        difference += std::fabs(wetLeft - cleanLeft)
            + std::fabs(wetRight - cleanRight);
        wetSideEnergy += wetSide * wetSide;
        cleanSine += cleanMid * std::sin(phase);
        cleanCosine += cleanMid * std::cos(phase);
        wetSine += wetMid * std::sin(phase);
        wetCosine += wetMid * std::cos(phase);
    }
    const double count = static_cast<double>(end - start);
    const double cleanFundamental = 2.0
        * std::hypot(cleanSine, cleanCosine) / count;
    const double wetFundamental = 2.0
        * std::hypot(wetSine, wetCosine) / count;
    if (difference < 10.0 || wetSideEnergy < 1.0e-6
        || wetFundamental < cleanFundamental * 0.78) {
        std::cerr << "bass Shred lacked stereo effect or low anchor: "
                  << difference << " / " << wetSideEnergy << " / "
                  << cleanFundamental << " / " << wetFundamental << "\n";
        return false;
    }

    wet.noteOff();
    bool regeneratedPastEnvelope = false;
    double tailEnergy = 0.0;
    for (uint32_t sample = 0u; sample < 96000u; ++sample) {
        float left = 0.0f;
        float right = 0.0f;
        wet.processFrame(left, right);
        if (sample > 600u && wet.active()) {
            regeneratedPastEnvelope = true;
            tailEnergy += static_cast<double>(left) * left
                + static_cast<double>(right) * right;
        }
    }
    if (!regeneratedPastEnvelope || tailEnergy < 1.0e-8
        || wet.active()) {
        std::cerr << "governed Shred feedback did not drain as a tail: "
                  << regeneratedPastEnvelope << " / " << tailEnergy
                  << " / " << wet.active() << "\n";
        return false;
    }

    std::array<double, 5u> circuitSignatures {};
    constexpr std::array<s3g::BassShredCircuit, 5u> circuits {{
        s3g::BassShredCircuit::Shred,
        s3g::BassShredCircuit::Wool,
        s3g::BassShredCircuit::Rat,
        s3g::BassShredCircuit::ZoneA,
        s3g::BassShredCircuit::ZoneB,
    }};
    for (uint32_t index = 0u; index < circuits.size(); ++index) {
        s3g::LowFrequencySynth circuitSynth;
        circuitSynth.prepare(kSampleRate);
        auto circuitParams = params;
        circuitParams.body = 0.78f;
        circuitParams.upperModeLevel = 0.42f;
        circuitParams.shred = 0.68f;
        circuitParams.shredFeedback = 0.32f;
        circuitParams.shredCircuit = static_cast<float>(circuits[index]);
        circuitParams.shredColor = 0.56f;
        circuitParams.shredMix = 0.72f;
        circuitSynth.setParams(circuitParams);
        circuitSynth.noteOn(33, 1.0f, false);
        float previous = 0.0f;
        for (uint32_t sample = 0u; sample < 24000u; ++sample) {
            float left = 0.0f;
            float right = 0.0f;
            circuitSynth.processFrame(left, right);
            if (!std::isfinite(left) || !std::isfinite(right)) {
                std::cerr << "selectable Shred circuit was not finite\n";
                return false;
            }
            if (sample >= 6000u) {
                circuitSignatures[index] += std::fabs(left - previous)
                    + 0.25 * std::fabs(left - right);
            }
            previous = left;
        }
    }
    for (uint32_t index = 1u; index < circuitSignatures.size(); ++index) {
        if (std::fabs(circuitSignatures[index]
                - circuitSignatures[index - 1u]) < 0.05) {
            std::cerr << "adjacent Shred circuits were not distinct: "
                      << circuitSignatures[index - 1u] << " / "
                      << circuitSignatures[index] << "\n";
            return false;
        }
    }
    return true;
}

bool amplitudeMotionPositionProbe()
{
    s3g::LowFrequencySynth pre;
    s3g::LowFrequencySynth post;
    pre.prepare(kSampleRate);
    post.prepare(kSampleRate);
    auto params = pre.params();
    params.fundamental = 0.82f;
    params.body = 0.86f;
    params.upperModeLevel = 0.42f;
    params.processedMix = 0.44f;
    params.valvePreamp = 0.28f;
    params.attackSeconds = 0.001f;
    params.decaySeconds = 0.005f;
    params.sustain = 1.0f;
    params.releaseSeconds = 0.20f;
    params.amplitudeMotionDepth = 1.0f;
    params.shred = 0.78f;
    params.shredFeedback = 0.82f;
    params.shredMix = 1.0f;
    params.shredCircuit = static_cast<float>(s3g::BassShredCircuit::Rat);
    params.shredColor = 0.58f;
    params.outputGainDb = -18.0f;
    params.amplitudeMotionPosition = static_cast<float>(
        s3g::AmplitudeMotionPosition::PreShred);
    pre.setParams(params);
    params.amplitudeMotionPosition = static_cast<float>(
        s3g::AmplitudeMotionPosition::PostShred);
    post.setParams(params);
    pre.noteOn(33, 1.0f, false);
    post.noteOn(33, 1.0f, false);
    pre.setAmplitudeMotionTransportPhase(0.5f, true);
    post.setAmplitudeMotionTransportPhase(0.5f, true);
    double openDifference = 0.0;
    for (uint32_t sample = 0u; sample < 24000u; ++sample) {
        float preLeft = 0.0f;
        float preRight = 0.0f;
        float postLeft = 0.0f;
        float postRight = 0.0f;
        pre.processFrame(preLeft, preRight);
        post.processFrame(postLeft, postRight);
        if (!std::isfinite(preLeft) || !std::isfinite(preRight)
            || !std::isfinite(postLeft) || !std::isfinite(postRight)) {
            std::cerr << "AM position path produced non-finite output\n";
            return false;
        }
        if (sample >= 12000u) {
            openDifference += std::fabs(preLeft - postLeft)
                + std::fabs(preRight - postRight);
        }
    }
    pre.setAmplitudeMotionTransportPhase(0.0f, true);
    post.setAmplitudeMotionTransportPhase(0.0f, true);
    double preClosedEnergy = 0.0;
    double postClosedEnergy = 0.0;
    for (uint32_t sample = 0u; sample < 6000u; ++sample) {
        float preLeft = 0.0f;
        float preRight = 0.0f;
        float postLeft = 0.0f;
        float postRight = 0.0f;
        pre.processFrame(preLeft, preRight);
        post.processFrame(postLeft, postRight);
        if (sample >= 400u) {
            preClosedEnergy += static_cast<double>(preLeft) * preLeft
                + static_cast<double>(preRight) * preRight;
            postClosedEnergy += static_cast<double>(postLeft) * postLeft
                + static_cast<double>(postRight) * postRight;
        }
    }
    if (openDifference > 1.0e-5
        || preClosedEnergy < postClosedEnergy * 1.25 + 1.0e-8) {
        std::cerr << "AM pre/post Shred routing was not distinct: "
                  << openDifference << " / " << preClosedEnergy << " / "
                  << postClosedEnergy << "\n";
        return false;
    }

    pre.setAmplitudeMotionTransportPhase(0.25f, true);
    float previous = 0.0f;
    float right = 0.0f;
    for (uint32_t sample = 0u; sample < 2400u; ++sample) {
        pre.processFrame(previous, right);
    }
    float steadyStep = 0.0f;
    for (uint32_t sample = 0u; sample < 2400u; ++sample) {
        float left = 0.0f;
        pre.processFrame(left, right);
        steadyStep = std::max(steadyStep, std::fabs(left - previous));
        previous = left;
    }
    auto switchedParams = pre.params();
    switchedParams.amplitudeMotionPosition = static_cast<float>(
        s3g::AmplitudeMotionPosition::PostShred);
    pre.setParams(switchedParams);
    float maximumStep = 0.0f;
    for (uint32_t sample = 0u; sample < 2400u; ++sample) {
        float left = 0.0f;
        float right = 0.0f;
        pre.processFrame(left, right);
        maximumStep = std::max(maximumStep, std::fabs(left - previous));
        previous = left;
    }
    if (maximumStep > steadyStep * 1.20f + 0.01f) {
        std::cerr << "AM position switch was not declicked: "
                  << maximumStep << " / steady " << steadyStep << "\n";
        return false;
    }
    return true;
}

bool selectableShredStressProbe()
{
    for (uint32_t circuit = 0u;
         circuit < s3g::kBassShredCircuitCount; ++circuit) {
        s3g::LowFrequencySynth synth;
        synth.prepare(kSampleRate);
        auto params = synth.params();
        params.attackSeconds = 0.0005f;
        params.decaySeconds = 0.005f;
        params.sustain = 1.0f;
        params.releaseSeconds = 0.005f;
        params.shred = 1.0f;
        params.shredFeedback = 1.0f;
        params.shredCircuit = static_cast<float>(circuit);
        params.shredColor = (circuit & 1u) == 0u ? 0.0f : 1.0f;
        params.shredMix = 1.0f;
        params.outputGainDb = -12.0f;
        synth.setParams(params);
        synth.noteOn(33, 1.0f, false);
        float peak = 0.0f;
        for (uint32_t sample = 0u; sample < 24000u; ++sample) {
            float left = 0.0f;
            float right = 0.0f;
            synth.processFrame(left, right);
            if (!std::isfinite(left) || !std::isfinite(right)) {
                std::cerr << "extreme Shred circuit was not finite: "
                          << circuit << "\n";
                return false;
            }
            peak = std::max(peak,
                std::max(std::fabs(left), std::fabs(right)));
        }
        synth.noteOff();
        for (uint32_t sample = 0u; sample < 96000u; ++sample) {
            float left = 0.0f;
            float right = 0.0f;
            synth.processFrame(left, right);
            if (!std::isfinite(left) || !std::isfinite(right)) return false;
        }
        if (peak < 0.01f || peak > 0.941f || synth.active()) {
            std::cerr << "extreme Shred circuit containment mismatch: "
                      << circuit << " / " << peak << " / "
                      << synth.active() << "\n";
            return false;
        }
    }
    return true;
}

bool onsetReleaseAndHeadroomProbe()
{
    s3g::LowFrequencySynth smooth;
    smooth.prepare(kSampleRate);
    auto params = smooth.params();
    params.fundamental = 1.0f;
    params.body = 0.0f;
    params.upperModeLevel = 0.0f;
    params.attackSeconds = 0.0005f;
    params.decaySeconds = 0.005f;
    params.sustain = 1.0f;
    params.releaseSeconds = 0.005f;
    params.valvePreamp = 1.0f;
    params.powerStage = 1.0f;
    params.supplySag = 1.0f;
    params.cabinet = 1.0f;
    params.outputGainDb = 6.0f;
    smooth.setParams(params);
    smooth.noteOn(33, 1.0f, false);

    float previous = 0.0f;
    float onsetStep = 0.0f;
    float steadyStep = 0.0f;
    for (uint32_t sample = 0u; sample < 4800u; ++sample) {
        float left = 0.0f;
        float right = 0.0f;
        smooth.processFrame(left, right);
        if (sample < 512u) {
            onsetStep = std::max(onsetStep, std::fabs(left - previous));
        } else if (sample >= 2400u) {
            steadyStep = std::max(steadyStep, std::fabs(left - previous));
        }
        previous = left;
    }
    if (onsetStep > steadyStep * 1.20f + 0.01f) {
        std::cerr << "de-clicked onset changed too abruptly: "
                  << onsetStep << " / steady " << steadyStep << "\n";
        return false;
    }

    smooth.noteOff();
    float releaseStep = 0.0f;
    float terminalStep = 1.0f;
    bool reachedIdle = false;
    for (uint32_t sample = 0u; sample < 4800u; ++sample) {
        const bool wasActive = smooth.active();
        float left = 0.0f;
        float right = 0.0f;
        smooth.processFrame(left, right);
        releaseStep = std::max(releaseStep, std::fabs(left - previous));
        if (wasActive && !smooth.active()) {
            terminalStep = std::fabs(left - previous);
            reachedIdle = true;
        }
        previous = left;
    }
    if (!reachedIdle || releaseStep > steadyStep * 1.20f + 0.01f
        || terminalStep > 0.002f) {
        std::cerr << "de-clicked release was abrupt or did not drain: "
                  << releaseStep << " / " << terminalStep << "\n";
        return false;
    }

    s3g::LowFrequencySynth aggressive;
    aggressive.prepare(kSampleRate);
    params = aggressive.params();
    params.fundamental = 1.0f;
    params.body = 1.0f;
    params.upperModeLevel = 1.0f;
    params.attackSeconds = 0.0005f;
    params.sustain = 1.0f;
    params.filterCutoffHz = 12000.0f;
    params.filterResonance = 1.0f;
    params.membraneDrive = 1.0f;
    params.wavefold = 1.0f;
    params.driveFeedback = 1.0f;
    params.processedMix = 1.0f;
    params.valvePreamp = 1.0f;
    params.powerStage = 1.0f;
    params.supplySag = 1.0f;
    params.cabinet = 1.0f;
    params.pedalCircuit = static_cast<float>(
        s3g::BassPedalCircuit::DualAsymmetry);
    params.pedalDrive = 1.0f;
    params.pedalBlend = 1.0f;
    params.outputGainDb = 6.0f;
    aggressive.setParams(params);
    aggressive.noteOn(33, 1.0f, false);
    float peak = 0.0f;
    uint32_t clampedSamples = 0u;
    for (uint32_t sample = 0u; sample < 24000u; ++sample) {
        float left = 0.0f;
        float right = 0.0f;
        aggressive.processFrame(left, right);
        peak = std::max(peak,
            std::max(std::fabs(left), std::fabs(right)));
        if (std::fabs(left) >= 0.999f || std::fabs(right) >= 0.999f) {
            ++clampedSamples;
        }
    }
    if (peak < 0.1f || peak > 0.941f || clampedSamples != 0u) {
        std::cerr << "linked headroom guard failed: " << peak << " / "
                  << clampedSamples << " clamped samples\n";
        return false;
    }
    return true;
}

bool factoryPresetProbe()
{
    for (uint32_t index = 0u;
         index < s3g::kLowFrequencySynthFactoryPresetCount; ++index) {
        const auto params = s3g::lowFrequencySynthFactoryPreset(index);
        if (s3g::lowFrequencySynthFactoryPresetIndex(params)
                != static_cast<int32_t>(index)) {
            std::cerr << "factory preset identity mismatch at " << index
                      << "\n";
            return false;
        }
        auto hiddenMutation = params;
        hiddenMutation.fineCents = 47.0f;
        hiddenMutation.tensionVariance = 0.83f;
        hiddenMutation.nonlinearity = 0.89f;
        hiddenMutation.pitchTransientMs = 311.0f;
        hiddenMutation.stereoWidth = 0.94f;
        hiddenMutation.velocitySensitivity = 0.03f;
        hiddenMutation.pressureSensitivity = 0.96f;
        hiddenMutation.filterEnvelopeOctaves = 6.0f;
        hiddenMutation.filterDecayMs = 901.0f;
        hiddenMutation.wavefold = 0.95f;
        hiddenMutation.driveFeedback = 0.92f;
        hiddenMutation.shred = params.shred;
        hiddenMutation.shredFeedback = params.shredFeedback;
        hiddenMutation.shredCircuit = params.shredCircuit;
        hiddenMutation.shredColor = params.shredColor;
        hiddenMutation.shredMix = params.shredMix;
        hiddenMutation.powerStage = 0.88f;
        hiddenMutation.supplySag = 0.86f;
        hiddenMutation.ampBass = -0.70f;
        hiddenMutation.ampMid = -0.60f;
        hiddenMutation.ampMidFrequency = 4.0f;
        hiddenMutation.ampTreble = 0.80f;
        hiddenMutation.cabinet = 0.84f;
        hiddenMutation.pedalDrive = 0.82f;
        hiddenMutation.pedalCharacter = 0.20f;
        hiddenMutation.pedalCrossoverHz = 900.0f;
        hiddenMutation.pedalBlend = 0.81f;
        const auto rebuilt =
            s3g::lowFrequencySynthFromExposedControls(hiddenMutation);
        if (!s3g::lowFrequencySynthParamsEqual(params, rebuilt)) {
            std::cerr << "factory preset depends on a hidden control at "
                      << index << "\n";
            return false;
        }
        s3g::LowFrequencySynth synth;
        synth.prepare(kSampleRate);
        synth.setParams(params);
        synth.noteOn(33, 1.0f, false);
        float peak = 0.0f;
        for (uint32_t sample = 0u; sample < 12000u; ++sample) {
            float left = 0.0f;
            float right = 0.0f;
            synth.processFrame(left, right);
            if (!std::isfinite(left) || !std::isfinite(right)) {
                std::cerr << "factory preset produced non-finite output at "
                          << index << "\n";
                return false;
            }
            peak = std::max(peak,
                std::max(std::fabs(left), std::fabs(right)));
        }
        if (peak < 0.001f || peak > 1.0f) {
            std::cerr << "factory preset level mismatch at " << index
                      << ": " << peak << "\n";
            return false;
        }
    }
    return true;
}

} // namespace

int main()
{
    if (!silenceAndSanitizationProbe()
        || !sustainedPitchAndEnvelopeProbe()
        || !researchMappingProbe()
        || !perceptualBassWeightProbe()
        || !membraneKickAdaptationProbe()
        || !drivenBodyAndProtectedStereoProbe()
        || !glideAndPressureProbe()
        || !filterAndDriveProbe()
        || !smoothFilterStressProbe()
        || !directSurfaceThicknessProbe()
        || !tubeAudibilityProbe()
        || !tubeAutomationContinuityProbe()
        || !stereoShredProbe()
        || !amplitudeMotionPositionProbe()
        || !selectableShredStressProbe()
        || !onsetReleaseAndHeadroomProbe()
        || !factoryPresetProbe()) {
        return 1;
    }
    std::cout << "low frequency synth smoke passed\n";
    return 0;
}
