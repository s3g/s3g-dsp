#include "s3g_processor_stack.h"
#include "s3g_processor_stack_presets.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <vector>

namespace {

struct RenderStats {
    double energy = 0.0;
    double onsetEnergy = 0.0;
    double onsetDifferenceEnergy = 0.0;
    double leadingEdgeEnergy = 0.0;
    double leadingEdgeDifferenceEnergy = 0.0;
    double earlyEnergy = 0.0;
    double tailEnergy = 0.0;
    double differenceEnergy = 0.0;
    double sideEnergy = 0.0;
    double leftEnergy = 0.0;
    double rightEnergy = 0.0;
    double channelCrossEnergy = 0.0;
    double feedbackBodyEnergy = 0.0;
    double feedbackStabEnergy = 0.0;
    double targetGlitchEnergy = 0.0;
    float peak = 0.0f;
    float onsetPeak = 0.0f;
    float onsetMaximumDelta = 0.0f;
    float leadingEdgePeak = 0.0f;
    float maximumDelta = 0.0f;
    float maximumSpeakerProtection = 0.0f;
    float maximumSpeakerModePreLimit = 0.0f;
    float maximumLimiterGainStep = 0.0f;
    float maximumPairSideActivity = 0.0f;
    uint32_t onsetMaximumDeltaFrame = 0u;
    float maximumOverloadMask = 0.0f;
    float finalActivity = 0.0f;
    float sag = 0.0f;
    uint64_t targetGlitchTriggers = 0u;
    uint64_t speakerSoftLimitCount = 0u;
    uint64_t micSoftLimitCount = 0u;
    uint64_t limiterAttackEvents = 0u;
    bool finite = true;
    bool active = false;
};

RenderStats render(s3g::ProcessorStackParams params,
    const std::vector<std::pair<uint32_t, int>>& noteOns,
    const std::vector<std::pair<uint32_t, int>>& noteOffs,
    uint32_t frames = 144000u, double sampleRate = 48000.0,
    float pressure = 0.0f, float bend = 0.0f, float tempo = 120.0f)
{
    s3g::ProcessorStack stack;
    stack.prepare(sampleRate);
    stack.setParams(params);
    stack.setPressure(pressure);
    stack.setPitchBendSemitones(bend);
    stack.setTempoBpm(tempo);
    RenderStats stats;
    size_t onIndex = 0u;
    size_t offIndex = 0u;
    float previousMono = 0.0f;
    const uint32_t earlyEnd = std::max<uint32_t>(1u, frames / 4u);
    const uint32_t onsetEnd = std::max<uint32_t>(1u,
        static_cast<uint32_t>(sampleRate * 0.006));
    const uint32_t leadingEdgeEnd = std::max<uint32_t>(1u,
        static_cast<uint32_t>(sampleRate * 0.002));
    const uint32_t tailStart = frames * 3u / 4u;
    for (uint32_t frame = 0u; frame < frames; ++frame) {
        while (onIndex < noteOns.size() && noteOns[onIndex].first == frame) {
            stack.noteOn(noteOns[onIndex].second,
                onIndex == 0u ? 0.92f : 0.74f);
            ++onIndex;
        }
        while (offIndex < noteOffs.size()
            && noteOffs[offIndex].first == frame) {
            stack.noteOff(noteOffs[offIndex].second);
            ++offIndex;
        }
        float left = 0.0f;
        float right = 0.0f;
        stack.processFrame(left, right);
        if (!std::isfinite(left) || !std::isfinite(right)) {
            stats.finite = false;
            break;
        }
        const float framePeak = std::max(std::abs(left), std::abs(right));
        const double frameEnergy = static_cast<double>(left) * left
            + static_cast<double>(right) * right;
        stats.energy += frameEnergy;
        stats.leftEnergy += static_cast<double>(left) * left;
        stats.rightEnergy += static_cast<double>(right) * right;
        stats.channelCrossEnergy += static_cast<double>(left) * right;
        const float mono = (left + right) * 0.5f;
        const float difference = mono - previousMono;
        stats.maximumDelta = std::max(stats.maximumDelta,
            std::abs(difference));
        stats.differenceEnergy += static_cast<double>(difference) * difference;
        const float side = (left - right) * 0.5f;
        stats.sideEnergy += static_cast<double>(side) * side;
        if (frame < onsetEnd) {
            stats.onsetEnergy += frameEnergy;
            stats.onsetDifferenceEnergy += static_cast<double>(difference)
                * difference;
            stats.onsetPeak = std::max(stats.onsetPeak, framePeak);
            if (std::abs(difference) > stats.onsetMaximumDelta) {
                stats.onsetMaximumDelta = std::abs(difference);
                stats.onsetMaximumDeltaFrame = frame;
            }
        }
        if (frame < leadingEdgeEnd) {
            stats.leadingEdgeEnergy += frameEnergy;
            stats.leadingEdgeDifferenceEnergy += static_cast<double>(difference)
                * difference;
            stats.leadingEdgePeak = std::max(stats.leadingEdgePeak, framePeak);
        }
        previousMono = mono;
        stats.feedbackBodyEnergy += static_cast<double>(
            stack.feedbackBodyActivity()) * stack.feedbackBodyActivity();
        stats.feedbackStabEnergy += static_cast<double>(
            stack.feedbackStabActivity()) * stack.feedbackStabActivity();
        stats.targetGlitchEnergy += static_cast<double>(
            stack.targetGlitchActivity()) * stack.targetGlitchActivity();
        stats.maximumOverloadMask = std::max(stats.maximumOverloadMask,
            stack.overloadMaskActivity());
        stats.sag = std::max(stats.sag, stack.sagEnvelope());
        stats.maximumSpeakerProtection = std::max(
            stats.maximumSpeakerProtection,
            stack.speakerProtectionActivity());
        stats.maximumSpeakerModePreLimit = std::max(
            stats.maximumSpeakerModePreLimit,
            stack.speakerModePreLimitPeak());
        stats.maximumLimiterGainStep = std::max(
            stats.maximumLimiterGainStep,
            stack.maximumLimiterGainStep());
        stats.maximumPairSideActivity = std::max(
            stats.maximumPairSideActivity, stack.pairSideActivity());
        stats.speakerSoftLimitCount = std::max(
            stats.speakerSoftLimitCount, stack.speakerSoftLimitCount());
        stats.micSoftLimitCount = std::max(stats.micSoftLimitCount,
            stack.micSoftLimitCount());
        stats.limiterAttackEvents = std::max(stats.limiterAttackEvents,
            stack.limiterAttackEventCount());
        if (frame < earlyEnd) stats.earlyEnergy += frameEnergy;
        if (frame >= tailStart) stats.tailEnergy += frameEnergy;
        stats.peak = std::max(stats.peak, framePeak);
    }
    stats.finalActivity = stack.feedbackActivity();
    stats.targetGlitchTriggers = stack.targetGlitchTriggerCount();
    stats.active = stack.active();
    return stats;
}

std::vector<float> renderSignature(s3g::ProcessorStackParams params)
{
    s3g::ProcessorStack stack;
    stack.prepare(48000.0);
    stack.setParams(params);
    stack.noteOn(43, 0.84f);
    std::vector<float> result(8192u);
    for (uint32_t frame = 0u; frame < result.size(); ++frame) {
        if (frame == 4096u) stack.noteOff(43);
        float left = 0.0f;
        float right = 0.0f;
        stack.processFrame(left, right);
        result[frame] = left + right * 0.37f;
    }
    return result;
}

struct StereoDifference {
    double left = 0.0;
    double right = 0.0;
};

StereoDifference compareStereoSignatures(
    s3g::ProcessorStackParams firstParams,
    s3g::ProcessorStackParams secondParams)
{
    s3g::ProcessorStack first;
    s3g::ProcessorStack second;
    first.prepare(48000.0);
    second.prepare(48000.0);
    first.setParams(firstParams);
    second.setParams(secondParams);
    first.noteOn(43, 0.84f);
    second.noteOn(43, 0.84f);
    StereoDifference difference;
    for (uint32_t frame = 0u; frame < 12000u; ++frame) {
        float firstLeft = 0.0f;
        float firstRight = 0.0f;
        float secondLeft = 0.0f;
        float secondRight = 0.0f;
        first.processFrame(firstLeft, firstRight);
        second.processFrame(secondLeft, secondRight);
        const double leftDelta = static_cast<double>(
            firstLeft - secondLeft);
        const double rightDelta = static_cast<double>(
            firstRight - secondRight);
        difference.left += leftDelta * leftDelta;
        difference.right += rightDelta * rightDelta;
    }
    return difference;
}

bool near(float first, float second, float tolerance = 1.0e-6f)
{
    return std::abs(first - second) <= tolerance;
}

} // namespace

int main()
{
    s3g::ProcessorStack idle;
    idle.prepare(48000.0);
    for (uint32_t frame = 0u; frame < 4096u; ++frame) {
        float left = 1.0f;
        float right = 1.0f;
        idle.processFrame(left, right);
        if (left != 0.0f || right != 0.0f
            || !std::isfinite(left) || !std::isfinite(right)) {
            std::cerr << "idle Processor Stack was not finite silence\n";
            return 1;
        }
    }

    s3g::ProcessorStackParams invalid;
    invalid.mode = static_cast<s3g::ProcessorStackMode>(99u);
    invalid.circuit = static_cast<s3g::ProcessorStackCircuit>(99u);
    invalid.shape = -4.0f;
    invalid.wire = 3.0f;
    invalid.pick = std::numeric_limits<float>::quiet_NaN();
    invalid.glideMs = 9000.0f;
    invalid.attackMs = -4.0f;
    invalid.decayMs = 90000.0f;
    invalid.sustain = -3.0f;
    invalid.releaseMs = 90000.0f;
    invalid.pairAmount = 9.0f;
    invalid.pairRelation =
        static_cast<s3g::ProcessorStackPairRelation>(99u);
    invalid.pairLoose = -2.0f;
    invalid.pairSpread = std::numeric_limits<float>::quiet_NaN();
    invalid.neckA = static_cast<s3g::ProcessorStackNeckMaterial>(99u);
    invalid.bodyA = static_cast<s3g::ProcessorStackBodyMaterial>(99u);
    invalid.neckB = static_cast<s3g::ProcessorStackNeckMaterial>(99u);
    invalid.bodyB = static_cast<s3g::ProcessorStackBodyMaterial>(99u);
    invalid.feedback = 5.0f;
    invalid.polarity = -5.0f;
    invalid.arpPattern = static_cast<s3g::ProcessorStackArpPattern>(99u);
    invalid.scale = static_cast<s3g::ProcessorStackScale>(99u);
    invalid.arpRate = static_cast<s3g::ProcessorStackArpRate>(99u);
    invalid.arpOctaves = 99u;
    invalid.arpGate = -1.0f;
    invalid.customPatternLength = 99u;
    invalid.customPattern[0u] = -99;
    invalid.customPattern[1u] = 99;
    invalid.arpBRelation = static_cast<s3g::ProcessorStackArpRelation>(99u);
    invalid.arpPatternB = static_cast<s3g::ProcessorStackArpPattern>(99u);
    invalid.scaleB = static_cast<s3g::ProcessorStackScale>(99u);
    invalid.arpRateB = static_cast<s3g::ProcessorStackArpRate>(99u);
    invalid.arpOctavesB = 99u;
    invalid.arpGateB = -1.0f;
    invalid.arpPhaseB = 4.0f;
    invalid.customPatternLengthB = 99u;
    invalid.customPatternB[0u] = -99;
    invalid.customPatternB[1u] = 99;
    invalid.circuitB = static_cast<s3g::ProcessorStackCircuit>(99u);
    invalid.biteB = -2.0f;
    invalid.stackB = 7.0f;
    invalid.feedbackB = 4.0f;
    invalid.polarityB = -3.0f;
    invalid.overloadMaskB = 8.0f;
    invalid.pierce = -3.0f;
    invalid.selfListen = 4.0f;
    invalid.targetGlitch = 8.0f;
    invalid.glitchRatchet = -2.0f;
    invalid.overloadMask = 9.0f;
    invalid.outputGainDb = 80.0f;
    const auto sanitized = s3g::sanitizeProcessorStackParams(invalid);
    if (sanitized.mode != s3g::ProcessorStackMode::Lead
        || sanitized.circuit != s3g::ProcessorStackCircuit::Diode
        || sanitized.shape != 0.0f || sanitized.wire != 1.0f
        || sanitized.pick != 0.72f || sanitized.glideMs != 2000.0f
        || sanitized.attackMs != 0.0f || sanitized.decayMs != 8000.0f
        || sanitized.sustain != 0.0f || sanitized.releaseMs != 20000.0f
        || sanitized.pairAmount != 1.0f
        || sanitized.pairRelation != s3g::ProcessorStackPairRelation::Contrary
        || sanitized.pairLoose != 0.0f || sanitized.pairSpread != 0.72f
        || sanitized.neckA != s3g::ProcessorStackNeckMaterial::Composite
        || sanitized.bodyA != s3g::ProcessorStackBodyMaterial::Aluminum
        || sanitized.neckB != s3g::ProcessorStackNeckMaterial::Composite
        || sanitized.bodyB != s3g::ProcessorStackBodyMaterial::Aluminum
        || sanitized.feedback != 1.0f || sanitized.polarity != 0.0f
        || sanitized.arpPattern != s3g::ProcessorStackArpPattern::Custom
        || sanitized.scale != s3g::ProcessorStackScale::Tritone
        || sanitized.arpRate != s3g::ProcessorStackArpRate::Whole
        || sanitized.arpOctaves != 4u || sanitized.arpGate != 0.05f
        || sanitized.customPatternLength != 8u
        || sanitized.customPattern[0u] != -8
        || sanitized.customPattern[1u] != 15
        || sanitized.arpBRelation != s3g::ProcessorStackArpRelation::Free
        || sanitized.arpPatternB != s3g::ProcessorStackArpPattern::Custom
        || sanitized.scaleB != s3g::ProcessorStackScale::Tritone
        || sanitized.arpRateB != s3g::ProcessorStackArpRate::Whole
        || sanitized.arpOctavesB != 4u || sanitized.arpGateB != 0.05f
        || sanitized.arpPhaseB != 1.0f
        || sanitized.customPatternLengthB != 8u
        || sanitized.customPatternB[0u] != -8
        || sanitized.customPatternB[1u] != 15
        || sanitized.circuitB != s3g::ProcessorStackCircuit::Diode
        || sanitized.biteB != 0.0f || sanitized.stackB != 1.0f
        || sanitized.feedbackB != 1.0f || sanitized.polarityB != 0.0f
        || sanitized.overloadMaskB != 1.0f
        || sanitized.pierce != 0.0f || sanitized.selfListen != 1.0f
        || sanitized.targetGlitch != 1.0f
        || sanitized.glitchRatchet != 0.0f
        || sanitized.overloadMask != 1.0f
        || sanitized.outputGainDb != 6.0f) {
        std::cerr << "Processor Stack parameter sanitation failed\n";
        return 1;
    }

    s3g::ProcessorStackParams reference;
    reference.outputGainDb = -12.0f;
    const auto referenceStats = render(reference, {{ 0u, 43 }},
        {{ 24000u, 43 }});
    if (!referenceStats.finite || referenceStats.energy < 0.001
        || referenceStats.peak < 0.001f || referenceStats.peak > 1.0f) {
        std::cerr << "Processor Stack reference render failed: energy="
                  << referenceStats.energy << " peak=" << referenceStats.peak
                  << " finite=" << referenceStats.finite << "\n";
        return 1;
    }

    auto dry = reference;
    dry.feedback = 0.0f;
    dry.spill = 0.0f;
    dry.wire = 0.0f;
    const auto dryStats = render(dry, {{ 0u, 43 }}, {{ 12000u, 43 }});
    auto regenerated = dry;
    regenerated.feedback = 0.92f;
    regenerated.spill = 0.82f;
    regenerated.cone = 0.88f;
    regenerated.proximity = 0.76f;
    const auto regeneratedStats = render(regenerated,
        {{ 0u, 43 }}, {{ 12000u, 43 }});
    if (!regeneratedStats.finite
        || regeneratedStats.tailEnergy <= dryStats.tailEnergy * 1.5
        || regeneratedStats.peak > 1.0f) {
        std::cerr << "speaker feedback did not create a bounded longer tail: "
                  << dryStats.tailEnergy << " -> "
                  << regeneratedStats.tailEnergy
                  << " peak=" << regeneratedStats.peak << "\n";
        return 1;
    }

    auto bodyFeedback = reference;
    bodyFeedback.mode = s3g::ProcessorStackMode::Lead;
    bodyFeedback.feedback = 0.86f;
    bodyFeedback.proximity = 0.82f;
    bodyFeedback.harmonic = 0.74f;
    bodyFeedback.tracking = 0.94f;
    bodyFeedback.pierce = 0.0f;
    bodyFeedback.selfListen = 0.0f;
    const auto bodyFeedbackStats = render(bodyFeedback,
        {{ 0u, 52 }}, {}, 96000u);
    auto piercingFeedback = bodyFeedback;
    piercingFeedback.pierce = 1.0f;
    piercingFeedback.selfListen = 1.0f;
    const auto piercingFeedbackStats = render(piercingFeedback,
        {{ 0u, 52 }}, {}, 96000u);
    const double bodyStabRatio = bodyFeedbackStats.feedbackStabEnergy
        / std::max(1.0e-12, bodyFeedbackStats.feedbackBodyEnergy);
    const double piercingStabRatio = piercingFeedbackStats.feedbackStabEnergy
        / std::max(1.0e-12, piercingFeedbackStats.feedbackBodyEnergy);
    const double bodyBrightness = bodyFeedbackStats.differenceEnergy
        / std::max(1.0e-12, bodyFeedbackStats.energy);
    const double piercingBrightness = piercingFeedbackStats.differenceEnergy
        / std::max(1.0e-12, piercingFeedbackStats.energy);
    if (!piercingFeedbackStats.finite
        || piercingStabRatio <= bodyStabRatio * 1.20
        || piercingBrightness <= bodyBrightness * 1.02) {
        std::cerr << "self-listening stab lane did not overtake body mud: ratio="
                  << bodyStabRatio << " -> " << piercingStabRatio
                  << " brightness=" << bodyBrightness << " -> "
                  << piercingBrightness << "\n";
        return 1;
    }

    s3g::ProcessorStack feedbackSweep;
    feedbackSweep.prepare(48000.0);
    auto feedbackSweepParams = piercingFeedback;
    feedbackSweepParams.feedback = 0.0f;
    feedbackSweepParams.pierce = 0.82f;
    feedbackSweepParams.selfListen = 0.90f;
    feedbackSweepParams.targetGlitch = 0.0f;
    feedbackSweepParams.chaos = 0.12f;
    feedbackSweepParams.overloadMask = 1.0f;
    feedbackSweep.setParams(feedbackSweepParams);
    feedbackSweep.noteOn(52, 0.86f);
    float feedbackSweepPrevious = 0.0f;
    float feedbackSweepPeak = 0.0f;
    float feedbackSweepMaximumDelta = 0.0f;
    double feedbackSweepEnergy = 0.0;
    double feedbackSweepDifferenceEnergy = 0.0;
    bool feedbackSweepFinite = true;
    for (uint32_t frame = 0u; frame < 96000u; ++frame) {
        if (frame < 48000u && (frame % 64u) == 0u) {
            feedbackSweepParams.feedback = static_cast<float>(frame)
                / 48000.0f;
            feedbackSweep.setParams(feedbackSweepParams);
        }
        if (frame == 72000u) feedbackSweep.noteOff(52);
        float left = 0.0f;
        float right = 0.0f;
        feedbackSweep.processFrame(left, right);
        if (!std::isfinite(left) || !std::isfinite(right)) {
            feedbackSweepFinite = false;
            break;
        }
        const float mono = (left + right) * 0.5f;
        const float difference = mono - feedbackSweepPrevious;
        feedbackSweepPrevious = mono;
        feedbackSweepPeak = std::max(feedbackSweepPeak,
            std::max(std::abs(left), std::abs(right)));
        feedbackSweepMaximumDelta = std::max(feedbackSweepMaximumDelta,
            std::abs(difference));
        feedbackSweepEnergy += static_cast<double>(left) * left
            + static_cast<double>(right) * right;
        feedbackSweepDifferenceEnergy += static_cast<double>(difference)
            * difference;
    }
    const double feedbackSweepRoughness = feedbackSweepDifferenceEnergy
        / std::max(1.0e-12, feedbackSweepEnergy);
    if (!feedbackSweepFinite || feedbackSweepPeak > 1.0f
        || feedbackSweepMaximumDelta > 0.30f
        || feedbackSweepRoughness > 0.025) {
        std::cerr << "microphone feedback sweep developed DSP-like breakup: "
                  << "peak=" << feedbackSweepPeak << " delta="
                  << feedbackSweepMaximumDelta << " roughness="
                  << feedbackSweepRoughness << "\n";
        return 1;
    }

    auto targetGlitch = piercingFeedback;
    targetGlitch.targetGlitch = 1.0f;
    targetGlitch.glitchRatchet = 0.82f;
    const auto targetGlitchStats = render(targetGlitch,
        {{ 0u, 52 }, { 18000u, 55 }, { 36000u, 58 }}, {}, 96000u);
    if (!targetGlitchStats.finite
        || targetGlitchStats.targetGlitchTriggers < 2u
        || targetGlitchStats.targetGlitchEnergy < 1.0e-8
        || targetGlitchStats.maximumDelta > 0.45f) {
        std::cerr << "targeted STAB glitch did not produce bounded, click-safe "
                     "captured repeats: triggers="
                  << targetGlitchStats.targetGlitchTriggers
                  << " energy=" << targetGlitchStats.targetGlitchEnergy
                  << " delta=" << targetGlitchStats.maximumDelta << "\n";
        return 1;
    }

    auto unmaskedOverload = targetGlitch;
    unmaskedOverload.mode = s3g::ProcessorStackMode::Power;
    unmaskedOverload.shape = 1.0f;
    unmaskedOverload.bite = 1.0f;
    unmaskedOverload.stack = 1.0f;
    unmaskedOverload.sag = 0.88f;
    unmaskedOverload.cone = 1.0f;
    unmaskedOverload.feedback = 1.0f;
    unmaskedOverload.proximity = 1.0f;
    unmaskedOverload.chaos = 1.0f;
    unmaskedOverload.targetGlitch = 1.0f;
    unmaskedOverload.glitchRatchet = 1.0f;
    unmaskedOverload.overloadMask = 0.0f;
    const auto unmaskedOverloadStats = render(unmaskedOverload,
        {{ 0u, 31 }, { 14000u, 32 }, { 28000u, 37 }}, {}, 96000u,
        48000.0, 1.0f);
    auto maskedOverload = unmaskedOverload;
    maskedOverload.overloadMask = 1.0f;
    const auto maskedOverloadStats = render(maskedOverload,
        {{ 0u, 31 }, { 14000u, 32 }, { 28000u, 37 }}, {}, 96000u,
        48000.0, 1.0f);
    if (!maskedOverloadStats.finite
        || maskedOverloadStats.maximumOverloadMask < 0.05f
        || maskedOverloadStats.maximumSpeakerProtection < 0.50f
        || maskedOverloadStats.speakerSoftLimitCount > 64u
        || maskedOverloadStats.speakerSoftLimitCount
            > unmaskedOverloadStats.speakerSoftLimitCount
        || maskedOverloadStats.micSoftLimitCount != 0u
        || maskedOverloadStats.differenceEnergy
            >= unmaskedOverloadStats.differenceEnergy * 0.94
        || maskedOverloadStats.maximumDelta > 0.45f) {
        std::cerr << "overload masker did not suppress dense cracking energy: "
                  << "mask=" << maskedOverloadStats.maximumOverloadMask
                  << " difference=" << unmaskedOverloadStats.differenceEnergy
                  << " -> " << maskedOverloadStats.differenceEnergy
                  << " delta=" << maskedOverloadStats.maximumDelta
                  << " protection="
                  << unmaskedOverloadStats.maximumSpeakerProtection << " -> "
                  << maskedOverloadStats.maximumSpeakerProtection
                  << " speaker-soft="
                  << unmaskedOverloadStats.speakerSoftLimitCount << "/"
                  << maskedOverloadStats.speakerSoftLimitCount
                  << " mic-soft=" << unmaskedOverloadStats.micSoftLimitCount
                  << "/" << maskedOverloadStats.micSoftLimitCount
                  << " limiter-step="
                  << maskedOverloadStats.maximumLimiterGainStep << "\n";
        return 1;
    }

    auto noString = reference;
    noString.wire = 0.0f;
    noString.feedback = 0.0f;
    noString.spill = 0.0f;
    noString.damping = 0.10f;
    noString.releaseMs = 20000.0f;
    const auto noStringStats = render(noString,
        {{ 0u, 45 }}, {{ 3000u, 45 }}, 48000u);
    auto pluckedString = noString;
    pluckedString.wire = 1.0f;
    const auto pluckedStringStats = render(pluckedString,
        {{ 0u, 45 }}, {{ 3000u, 45 }}, 48000u);
    if (!pluckedStringStats.finite
        || pluckedStringStats.tailEnergy
            <= noStringStats.tailEnergy + 1.0e-8) {
        std::cerr << "plucked-string waveguide did not outlast the pick packet: "
                  << noStringStats.tailEnergy << " -> "
                  << pluckedStringStats.tailEnergy << "\n";
        return 1;
    }

    auto oneGuitar = reference;
    oneGuitar.mode = s3g::ProcessorStackMode::Lead;
    oneGuitar.feedback = 0.0f;
    oneGuitar.targetGlitch = 0.0f;
    oneGuitar.outputGainDb = -18.0f;
    oneGuitar.pairAmount = 0.0f;
    oneGuitar.neckA = s3g::ProcessorStackNeckMaterial::Maple;
    oneGuitar.bodyA = s3g::ProcessorStackBodyMaterial::SolidWood;
    auto twoGuitars = oneGuitar;
    twoGuitars.pairAmount = 1.0f;
    twoGuitars.pairRelation = s3g::ProcessorStackPairRelation::FifthUp;
    twoGuitars.pairLoose = 0.48f;
    twoGuitars.pairSpread = 1.0f;
    twoGuitars.neckB = s3g::ProcessorStackNeckMaterial::Aluminum;
    twoGuitars.bodyB = s3g::ProcessorStackBodyMaterial::HollowWood;
    auto collapsedTwoGuitars = twoGuitars;
    collapsedTwoGuitars.pairSpread = 0.0f;
    const auto collapsedTwoGuitarStats = render(collapsedTwoGuitars,
        {{ 0u, 45 }}, {{ 18000u, 45 }}, 48000u);
    const auto twoGuitarStats = render(twoGuitars,
        {{ 0u, 45 }}, {{ 18000u, 45 }}, 48000u);
    const double channelCorrelation = std::abs(
        twoGuitarStats.channelCrossEnergy)
        / std::sqrt(std::max(1.0e-18,
            twoGuitarStats.leftEnergy * twoGuitarStats.rightEnergy));
    if (!twoGuitarStats.finite
        || twoGuitarStats.maximumPairSideActivity < 1.0e-4f
        || twoGuitarStats.sideEnergy
            <= collapsedTwoGuitarStats.sideEnergy * 4.0 + 1.0e-8
        || twoGuitarStats.sideEnergy <= twoGuitarStats.energy * 0.08
        || channelCorrelation >= 0.96) {
        std::cerr << "two independent rigs did not create a true stereo image: "
                  << collapsedTwoGuitarStats.sideEnergy << " -> "
                  << twoGuitarStats.sideEnergy << " activity="
                  << twoGuitarStats.maximumPairSideActivity
                  << " ratio="
                  << twoGuitarStats.sideEnergy / twoGuitarStats.energy
                  << " corr=" << channelCorrelation << "\n";
        return 1;
    }

    auto alternatePartnerMaterial = twoGuitars;
    alternatePartnerMaterial.neckB =
        s3g::ProcessorStackNeckMaterial::Mahogany;
    alternatePartnerMaterial.bodyB =
        s3g::ProcessorStackBodyMaterial::SolidWood;
    const auto partnerMaterialDifference = compareStereoSignatures(
        twoGuitars, alternatePartnerMaterial);
    if (partnerMaterialDifference.right
            <= partnerMaterialDifference.left * 1.5 + 1.0e-12) {
        std::cerr << "guitar B material did not remain localized to its rig: "
                  << partnerMaterialDifference.left << " / "
                  << partnerMaterialDifference.right << "\n";
        return 1;
    }

    auto splitRigA = twoGuitars;
    splitRigA.linkPedal = false;
    splitRigA.linkAmplifier = false;
    splitRigA.linkFeedback = false;
    splitRigA.circuitB = s3g::ProcessorStackCircuit::ZoneA;
    splitRigA.stackB = 0.28f;
    splitRigA.sagB = 0.18f;
    splitRigA.feedbackB = 0.16f;
    splitRigA.pierceB = 0.34f;
    auto splitRigB = splitRigA;
    splitRigB.circuitB = s3g::ProcessorStackCircuit::Shred;
    splitRigB.stackB = 0.94f;
    splitRigB.sagB = 0.82f;
    splitRigB.feedbackB = 0.78f;
    splitRigB.pierceB = 0.96f;
    const auto partnerRigDifference = compareStereoSignatures(
        splitRigA, splitRigB);
    if (partnerRigDifference.right
            <= partnerRigDifference.left * 1.5 + 1.0e-12) {
        std::cerr << "guitar B amp/effects did not remain localized: "
                  << partnerRigDifference.left << " / "
                  << partnerRigDifference.right << "\n";
        return 1;
    }

    auto metalHollow = oneGuitar;
    metalHollow.neckA = s3g::ProcessorStackNeckMaterial::Aluminum;
    metalHollow.bodyA = s3g::ProcessorStackBodyMaterial::HollowWood;
    const auto woodSignature = renderSignature(oneGuitar);
    const auto metalHollowSignature = renderSignature(metalHollow);
    if (woodSignature == metalHollowSignature) {
        std::cerr << "neck/body construction did not alter the guitar voice\n";
        return 1;
    }

    s3g::ProcessorStack relatedGuitars;
    relatedGuitars.prepare(48000.0);
    relatedGuitars.setParams(twoGuitars);
    relatedGuitars.noteOn(40, 0.86f);
    if (relatedGuitars.partnerRootNote() != 47) {
        std::cerr << "fifth-related guitar did not select the upper fifth\n";
        return 1;
    }
    auto contraryGuitars = twoGuitars;
    contraryGuitars.pairRelation =
        s3g::ProcessorStackPairRelation::Contrary;
    relatedGuitars.reset();
    relatedGuitars.setParams(contraryGuitars);
    relatedGuitars.noteOn(40, 0.86f);
    const int firstCounterNote = relatedGuitars.partnerRootNote();
    relatedGuitars.noteOn(43, 0.86f);
    if (firstCounterNote != 47
        || relatedGuitars.partnerRootNote() != 44) {
        std::cerr << "contrary guitar did not invert primary motion: "
                  << firstCounterNote << " -> "
                  << relatedGuitars.partnerRootNote() << "\n";
        return 1;
    }

    auto drain = regenerated;
    drain.feedback = 0.78f;
    drain.spill = 0.58f;
    const auto drainStats = render(drain, {{ 0u, 43 }},
        {{ 12000u, 43 }}, 768000u);
    if (!drainStats.finite || drainStats.active
        || drainStats.finalActivity > 1.0e-5f
        || drainStats.tailEnergy > 1.0e-5) {
        std::cerr << "governed feedback did not drain to silence: active="
                  << drainStats.active << " activity="
                  << drainStats.finalActivity << " tail="
                  << drainStats.tailEnergy << "\n";
        return 1;
    }

    auto power = reference;
    power.mode = s3g::ProcessorStackMode::Power;
    power.shape = 0.76f;
    // Keep this voicing comparison independent of the intentionally chaotic
    // microphone return; feedback behavior has dedicated tests above.
    power.feedback = 0.0f;
    power.spill = 0.0f;
    const auto powerStats = render(power, {{ 0u, 40 }}, {{ 30000u, 40 }},
        96000u);
    auto lead = power;
    lead.mode = s3g::ProcessorStackMode::Lead;
    const auto leadStats = render(lead, {{ 0u, 40 }}, {{ 30000u, 40 }},
        96000u);
    if (!powerStats.finite || !leadStats.finite
        || std::abs(powerStats.energy - leadStats.energy)
            < std::max(0.001, leadStats.energy * 0.01)
        || std::abs(powerStats.sag - leadStats.sag) < 1.0e-4f) {
        std::cerr << "POWER voicing did not audibly load the shared stack: "
                  << powerStats.energy << "/" << leadStats.energy
                  << " sag=" << powerStats.sag << "/" << leadStats.sag
                  << " protection=" << powerStats.maximumSpeakerProtection
                  << "/" << leadStats.maximumSpeakerProtection
                  << " limiter-events=" << powerStats.limiterAttackEvents
                  << "/" << leadStats.limiterAttackEvents
                  << " limiter-step=" << powerStats.maximumLimiterGainStep
                  << "/" << leadStats.maximumLimiterGainStep
                  << "\n";
        return 1;
    }

    auto lowShape = reference;
    lowShape.mode = s3g::ProcessorStackMode::Power;
    lowShape.shape = 0.0f;
    lowShape.feedback = 0.0f;
    lowShape.targetGlitch = 0.0f;
    lowShape.pick = 0.92f;
    lowShape.wire = 0.72f;
    auto highShape = lowShape;
    highShape.shape = 1.0f;
    const auto lowShapeStats = render(lowShape,
        {{ 0u, 40 }}, {{ 6000u, 40 }}, 12000u);
    const auto highShapeStats = render(highShape,
        {{ 0u, 40 }}, {{ 6000u, 40 }}, 12000u);
    const double lowOnsetRoughness = lowShapeStats.leadingEdgeDifferenceEnergy
        / std::max(1.0e-12, lowShapeStats.leadingEdgeEnergy);
    const double highOnsetRoughness = highShapeStats.leadingEdgeDifferenceEnergy
        / std::max(1.0e-12, highShapeStats.leadingEdgeEnergy);
    if (!lowShapeStats.finite || !highShapeStats.finite
        || highShapeStats.onsetMaximumDelta
            > lowShapeStats.onsetMaximumDelta * 1.24f
        || highShapeStats.leadingEdgePeak
            > lowShapeStats.leadingEdgePeak * 1.24f
        || std::abs(highShapeStats.energy - lowShapeStats.energy) < 0.001) {
        std::cerr << "SHAPE changed pick harshness instead of only voicing: "
                  << "roughness=" << lowOnsetRoughness << " -> "
                  << highOnsetRoughness << " peak="
                  << lowShapeStats.leadingEdgePeak << " -> "
                  << highShapeStats.leadingEdgePeak << " delta="
                  << lowShapeStats.onsetMaximumDelta << " -> "
                  << highShapeStats.onsetMaximumDelta << " frame="
                  << lowShapeStats.onsetMaximumDeltaFrame << " -> "
                  << highShapeStats.onsetMaximumDeltaFrame << "\n";
        return 1;
    }

    s3g::ProcessorStack shapeSweep;
    shapeSweep.prepare(48000.0);
    shapeSweep.setParams(lowShape);
    shapeSweep.noteOn(40, 0.88f);
    float shapePrevious = 0.0f;
    float shapeBoundaryDelta = 0.0f;
    for (uint32_t frame = 0u; frame < 8192u; ++frame) {
        if (frame == 4096u) shapeSweep.setParams(highShape);
        float left = 0.0f;
        float right = 0.0f;
        shapeSweep.processFrame(left, right);
        const float mono = (left + right) * 0.5f;
        if (frame >= 4096u && frame < 4352u) {
            shapeBoundaryDelta = std::max(shapeBoundaryDelta,
                std::abs(mono - shapePrevious));
        }
        shapePrevious = mono;
    }
    if (shapeBoundaryDelta > 0.20f) {
        std::cerr << "SHAPE automation retriggered or clicked: delta="
                  << shapeBoundaryDelta << "\n";
        return 1;
    }

    auto zeroPlay = reference;
    zeroPlay.mode = s3g::ProcessorStackMode::Power;
    zeroPlay.shape = 0.0f;
    zeroPlay.wire = 0.0f;
    zeroPlay.pick = 0.0f;
    zeroPlay.damping = 0.0f;
    zeroPlay.glideMs = 0.0f;
    zeroPlay.crooked = 0.0f;
    zeroPlay.spill = 0.0f;
    zeroPlay.feedback = 0.0f;
    zeroPlay.targetGlitch = 0.0f;
    const auto zeroPlayStats = render(zeroPlay,
        {{ 0u, 40 }}, {{ 6000u, 40 }}, 12000u);
    auto hardPick = zeroPlay;
    hardPick.pick = 0.92f;
    const auto hardPickStats = render(hardPick,
        {{ 0u, 40 }}, {{ 6000u, 40 }}, 12000u);
    const double zeroPlayRoughness = zeroPlayStats.onsetDifferenceEnergy
        / std::max(1.0e-12, zeroPlayStats.onsetEnergy);
    const double hardPickRoughness = hardPickStats.onsetDifferenceEnergy
        / std::max(1.0e-12, hardPickStats.onsetEnergy);
    if (!zeroPlayStats.finite || !hardPickStats.finite
        || zeroPlayStats.onsetEnergy < 1.0e-8
        || zeroPlayRoughness >= hardPickRoughness * 0.72
        || zeroPlayStats.onsetMaximumDelta > 0.09f) {
        std::cerr << "zeroed PLAY toolbox retained a discontinuous excitation: "
                  << "roughness=" << zeroPlayRoughness << " vs PICK "
                  << hardPickRoughness << " delta="
                  << zeroPlayStats.onsetMaximumDelta << " onset="
                  << zeroPlayStats.onsetEnergy << " frame="
                  << zeroPlayStats.onsetMaximumDeltaFrame
                  << " protection="
                  << zeroPlayStats.maximumSpeakerProtection
                  << " limiter-step="
                  << zeroPlayStats.maximumLimiterGainStep << "\n";
        return 1;
    }

    auto midPick = reference;
    midPick.mode = s3g::ProcessorStackMode::Lead;
    midPick.feedback = 0.0f;
    midPick.targetGlitch = 0.0f;
    midPick.wire = 0.18f;
    midPick.pick = 0.62f;
    auto maximumPick = midPick;
    maximumPick.pick = 1.0f;
    const auto midPickStats = render(midPick,
        {{ 0u, 52 }}, {{ 6000u, 52 }}, 12000u);
    const auto maximumPickStats = render(maximumPick,
        {{ 0u, 52 }}, {{ 6000u, 52 }}, 12000u);
    const double midPickRoughness = midPickStats.onsetDifferenceEnergy
        / std::max(1.0e-12, midPickStats.onsetEnergy);
    const double maximumPickRoughness =
        maximumPickStats.onsetDifferenceEnergy
        / std::max(1.0e-12, maximumPickStats.onsetEnergy);
    if (!midPickStats.finite || !maximumPickStats.finite
        || maximumPickStats.onsetEnergy <= midPickStats.onsetEnergy * 0.80
        || maximumPickRoughness > 0.012
        || maximumPickRoughness > midPickRoughness * 2.2
        || maximumPickStats.onsetMaximumDelta > 0.25f) {
        std::cerr << "maximum PICK became splattery instead of precise: "
                  << "energy=" << midPickStats.onsetEnergy << " -> "
                  << maximumPickStats.onsetEnergy << " roughness="
                  << midPickRoughness << " -> " << maximumPickRoughness
                  << " delta=" << maximumPickStats.onsetMaximumDelta
                  << "\n";
        return 1;
    }

    auto immediateEnvelope = reference;
    immediateEnvelope.mode = s3g::ProcessorStackMode::Lead;
    immediateEnvelope.feedback = 0.0f;
    immediateEnvelope.targetGlitch = 0.0f;
    immediateEnvelope.wire = 0.46f;
    immediateEnvelope.pick = 0.56f;
    immediateEnvelope.attackMs = 0.0f;
    immediateEnvelope.decayMs = 60.0f;
    immediateEnvelope.sustain = 1.0f;
    immediateEnvelope.releaseMs = 5.0f;
    auto slowEnvelope = immediateEnvelope;
    slowEnvelope.attackMs = 350.0f;
    const auto immediateEnvelopeStats = render(immediateEnvelope,
        {{ 0u, 45 }}, {{ 10000u, 45 }}, 16000u);
    const auto slowEnvelopeStats = render(slowEnvelope,
        {{ 0u, 45 }}, {{ 10000u, 45 }}, 16000u);
    if (!immediateEnvelopeStats.finite || !slowEnvelopeStats.finite
        || slowEnvelopeStats.onsetEnergy
            >= immediateEnvelopeStats.onsetEnergy * 0.32
        || slowEnvelopeStats.energy < 1.0e-6) {
        std::cerr << "ADSR ATTACK did not create a playable source swell: "
                  << immediateEnvelopeStats.onsetEnergy << " -> "
                  << slowEnvelopeStats.onsetEnergy << " total="
                  << slowEnvelopeStats.energy << " protection="
                  << immediateEnvelopeStats.maximumSpeakerProtection << "/"
                  << slowEnvelopeStats.maximumSpeakerProtection << " mask="
                  << immediateEnvelopeStats.maximumOverloadMask << "/"
                  << slowEnvelopeStats.maximumOverloadMask << "\n";
        return 1;
    }

    auto fullSustain = immediateEnvelope;
    fullSustain.decayMs = 45.0f;
    fullSustain.sustain = 1.0f;
    auto mutedSustain = fullSustain;
    mutedSustain.sustain = 0.0f;
    const auto fullSustainStats = render(fullSustain,
        {{ 0u, 45 }}, {}, 48000u);
    const auto mutedSustainStats = render(mutedSustain,
        {{ 0u, 45 }}, {}, 48000u);
    if (!fullSustainStats.finite || !mutedSustainStats.finite
        || mutedSustainStats.tailEnergy
            >= fullSustainStats.tailEnergy * 0.12) {
        std::cerr << "ADSR DECAY/SUSTAIN did not create palm-muted hold: "
                  << fullSustainStats.tailEnergy << " -> "
                  << mutedSustainStats.tailEnergy << "\n";
        return 1;
    }

    auto longRelease = fullSustain;
    longRelease.releaseMs = 2200.0f;
    const auto shortReleaseStats = render(fullSustain,
        {{ 0u, 45 }}, {{ 6000u, 45 }}, 48000u);
    const auto longReleaseStats = render(longRelease,
        {{ 0u, 45 }}, {{ 6000u, 45 }}, 48000u);
    if (!shortReleaseStats.finite || !longReleaseStats.finite
        || longReleaseStats.tailEnergy
            <= shortReleaseStats.tailEnergy * 8.0 + 1.0e-8) {
        std::cerr << "ADSR RELEASE did not extend source energy before SPILL: "
                  << shortReleaseStats.tailEnergy << " -> "
                  << longReleaseStats.tailEnergy << "\n";
        return 1;
    }

    auto denseArpPick = reference;
    denseArpPick.mode = s3g::ProcessorStackMode::Lead;
    denseArpPick.feedback = 0.0f;
    denseArpPick.targetGlitch = 0.0f;
    denseArpPick.wire = 0.42f;
    denseArpPick.pick = 0.62f;
    denseArpPick.damping = 0.64f;
    denseArpPick.attackMs = 0.0f;
    denseArpPick.decayMs = 42.0f;
    denseArpPick.sustain = 0.16f;
    denseArpPick.releaseMs = 18.0f;
    denseArpPick.arpPattern = s3g::ProcessorStackArpPattern::Up;
    denseArpPick.scale = s3g::ProcessorStackScale::Phrygian;
    denseArpPick.arpRate = s3g::ProcessorStackArpRate::SixtyFourth;
    denseArpPick.arpGate = 0.42f;
    auto denseArpMaximumPick = denseArpPick;
    denseArpMaximumPick.pick = 1.0f;
    const auto denseArpPickStats = render(denseArpPick,
        {{ 0u, 40 }}, {}, 48000u, 48000.0, 0.0f, 0.0f, 300.0f);
    const auto denseArpMaximumPickStats = render(denseArpMaximumPick,
        {{ 0u, 40 }}, {}, 48000u, 48000.0, 0.0f, 0.0f, 300.0f);
    const double denseArpRoughness = denseArpPickStats.differenceEnergy
        / std::max(1.0e-12, denseArpPickStats.energy);
    const double denseArpMaximumRoughness =
        denseArpMaximumPickStats.differenceEnergy
        / std::max(1.0e-12, denseArpMaximumPickStats.energy);
    if (!denseArpPickStats.finite || !denseArpMaximumPickStats.finite
        || denseArpMaximumPickStats.energy < 1.0e-5
        || denseArpMaximumRoughness > denseArpRoughness * 1.85
        || denseArpMaximumPickStats.maximumDelta > 0.28f) {
        std::cerr << "dense arpeggiator PICK retriggers became phasey or harsh: "
                  << "roughness=" << denseArpRoughness << " -> "
                  << denseArpMaximumRoughness << " delta="
                  << denseArpMaximumPickStats.maximumDelta << " energy="
                  << denseArpMaximumPickStats.energy << "\n";
        return 1;
    }

    auto hand = reference;
    hand.mode = s3g::ProcessorStackMode::Hand;
    const auto handStats = render(hand,
        {{ 0u, 40 }, { 3200u, 47 }, { 6400u, 52 }},
        {{ 26000u, 40 }, { 28000u, 47 }, { 30000u, 52 }}, 96000u);
    if (!handStats.finite || handStats.energy < 0.001
        || handStats.peak > 1.0f) {
        std::cerr << "HAND voicing was not bounded and audible\n";
        return 1;
    }

    auto polyGlitch = s3g::processorStackFactoryPreset(20u);
    const auto polyGlitchStats = render(polyGlitch,
        {{ 0u, 40 }, { 0u, 47 }, { 0u, 51 }, { 0u, 58 }},
        {{ 30000u, 40 }, { 30000u, 47 }, { 30000u, 51 }, { 30000u, 58 }},
        72000u);
    if (!polyGlitchStats.finite || polyGlitchStats.energy < 0.001
        || polyGlitchStats.targetGlitchTriggers == 0u
        || polyGlitchStats.maximumDelta > 0.45f) {
        std::cerr << "polyphonic targeted feedback was not bounded and active: "
                  << polyGlitchStats.energy << " triggers="
                  << polyGlitchStats.targetGlitchTriggers << " delta="
                  << polyGlitchStats.maximumDelta << "\n";
        return 1;
    }

    auto straight = reference;
    straight.mode = s3g::ProcessorStackMode::Lead;
    straight.crooked = 0.0f;
    auto crooked = straight;
    crooked.crooked = 1.0f;
    const auto straightStats = render(straight,
        {{ 0u, 48 }, { 6000u, 49 }, { 12000u, 55 }, { 18000u, 43 }},
        {{ 5800u, 48 }, { 11800u, 49 }, { 17800u, 55 }, { 30000u, 43 }},
        72000u);
    const auto crookedStats = render(crooked,
        {{ 0u, 48 }, { 6000u, 49 }, { 12000u, 55 }, { 18000u, 43 }},
        {{ 5800u, 48 }, { 11800u, 49 }, { 17800u, 55 }, { 30000u, 43 }},
        72000u);
    if (!straightStats.finite || !crookedStats.finite
        || std::abs(straightStats.energy - crookedStats.energy)
            < std::max(0.001, straightStats.energy * 0.01)) {
        std::cerr << "CROOKED interval response was not expressed\n";
        return 1;
    }

    s3g::ProcessorStack arpeggiator;
    arpeggiator.prepare(48000.0);
    auto arpParams = reference;
    arpParams.mode = s3g::ProcessorStackMode::Lead;
    arpParams.arpPattern = s3g::ProcessorStackArpPattern::Up;
    arpParams.scale = s3g::ProcessorStackScale::Phrygian;
    arpParams.arpRate = s3g::ProcessorStackArpRate::Sixteenth;
    arpParams.arpOctaves = 2u;
    arpParams.arpGate = 0.5f;
    arpeggiator.setParams(arpParams);
    arpeggiator.setTempoBpm(120.0f);
    arpeggiator.noteOn(40, 0.9f);
    if (arpeggiator.arpCurrentNote() != 40
        || arpeggiator.arpStepCount() != 1u) {
        std::cerr << "arpeggiator did not attack its scale root immediately\n";
        return 1;
    }
    for (uint32_t frame = 0u; frame < 6000u; ++frame) {
        float left = 0.0f;
        float right = 0.0f;
        arpeggiator.processFrame(left, right);
    }
    if (arpeggiator.arpCurrentNote() != 41
        || arpeggiator.arpStepCount() != 2u) {
        std::cerr << "tempo-synced Phrygian rule missed its flat second\n";
        return 1;
    }
    for (uint32_t frame = 0u; frame < 6000u; ++frame) {
        float left = 0.0f;
        float right = 0.0f;
        arpeggiator.processFrame(left, right);
    }
    if (arpeggiator.arpCurrentNote() != 43
        || arpeggiator.arpStepCount() != 3u) {
        std::cerr << "tempo-synced Phrygian rule missed its minor third\n";
        return 1;
    }

    s3g::ProcessorStack dualArpeggiator;
    dualArpeggiator.prepare(48000.0);
    auto dualArpParams = arpParams;
    dualArpParams.pairAmount = 1.0f;
    dualArpParams.pairSpread = 1.0f;
    dualArpParams.arpBRelation = s3g::ProcessorStackArpRelation::Free;
    dualArpParams.arpPatternB = s3g::ProcessorStackArpPattern::Custom;
    dualArpParams.scaleB = s3g::ProcessorStackScale::Phrygian;
    dualArpParams.arpRateB = s3g::ProcessorStackArpRate::Eighth;
    dualArpParams.arpOctavesB = 1u;
    dualArpParams.arpGateB = 0.68f;
    dualArpParams.arpPhaseB = 0.0f;
    dualArpParams.customPatternLengthB = 2u;
    dualArpParams.customPatternB = {{ 0, 4, 0, 0, 0, 0, 0, 0 }};
    dualArpeggiator.setParams(dualArpParams);
    dualArpeggiator.setTempoBpm(120.0f);
    dualArpeggiator.noteOn(40, 0.9f);
    if (dualArpeggiator.arpStepCount() != 1u
        || dualArpeggiator.partnerArpStepCount() != 1u
        || dualArpeggiator.partnerArpCurrentNote() != 40) {
        std::cerr << "independent B arpeggiator did not attack immediately\n";
        return 1;
    }
    for (uint32_t frame = 0u; frame < 12000u; ++frame) {
        float left = 0.0f;
        float right = 0.0f;
        dualArpeggiator.processFrame(left, right);
    }
    if (dualArpeggiator.arpStepCount() != 3u
        || dualArpeggiator.partnerArpStepCount() != 2u
        || dualArpeggiator.partnerArpCurrentNote() != 47) {
        std::cerr << "A/B arpeggiator clocks or custom B rule were not independent: "
                  << dualArpeggiator.arpStepCount() << " / "
                  << dualArpeggiator.partnerArpStepCount() << " note="
                  << dualArpeggiator.partnerArpCurrentNote() << "\n";
        return 1;
    }
    auto followArpParams = dualArpParams;
    followArpParams.arpBRelation = s3g::ProcessorStackArpRelation::Follow;
    s3g::ProcessorStack followingArpeggiator;
    followingArpeggiator.prepare(48000.0);
    followingArpeggiator.setParams(followArpParams);
    followingArpeggiator.noteOn(40, 0.9f);
    if (followingArpeggiator.partnerArpStepCount() != 0u
        || followingArpeggiator.partnerRootNote() != 40
        || std::strcmp(s3g::processorStackArpRelationName(
                s3g::ProcessorStackArpRelation::Counter), "COUNTER") != 0) {
        std::cerr << "FOLLOW arpeggiator did not defer to A's clock\n";
        return 1;
    }

    s3g::ProcessorStack slowArpeggiator;
    slowArpeggiator.prepare(48000.0);
    auto slowParams = arpParams;
    slowParams.arpRate = s3g::ProcessorStackArpRate::Whole;
    slowArpeggiator.setParams(slowParams);
    slowArpeggiator.setTempoBpm(120.0f);
    slowArpeggiator.noteOn(36, 0.8f);
    for (uint32_t frame = 0u; frame < 95999u; ++frame) {
        float left = 0.0f;
        float right = 0.0f;
        slowArpeggiator.processFrame(left, right);
    }
    if (slowArpeggiator.arpStepCount() != 1u) {
        std::cerr << "whole-note arpeggiator advanced before four beats\n";
        return 1;
    }
    float slowLeft = 0.0f;
    float slowRight = 0.0f;
    slowArpeggiator.processFrame(slowLeft, slowRight);
    if (slowArpeggiator.arpStepCount() != 2u
        || std::strcmp(s3g::processorStackArpRateName(
                s3g::ProcessorStackArpRate::Whole), "1/1") != 0) {
        std::cerr << "whole-note arpeggiator missed its four-beat boundary\n";
        return 1;
    }

    s3g::ProcessorStack customArp;
    customArp.prepare(48000.0);
    auto customParams = reference;
    customParams.mode = s3g::ProcessorStackMode::Lead;
    customParams.wire = 0.82f;
    customParams.feedback = 0.0f;
    customParams.crooked = 1.0f;
    customParams.outputGainDb = -18.0f;
    customParams.arpPattern = s3g::ProcessorStackArpPattern::Custom;
    customParams.scale = s3g::ProcessorStackScale::Phrygian;
    customParams.arpRate = s3g::ProcessorStackArpRate::Sixteenth;
    customParams.arpGate = 0.42f;
    customParams.customPatternLength = 4u;
    customParams.customPattern = {{ 0, 3, -1, 6, 0, 0, 0, 0 }};
    customArp.setParams(customParams);
    customArp.setTempoBpm(120.0f);
    customArp.noteOn(40, 0.9f);
    const std::array<int, 4u> expectedCustomNotes {{ 40, 45, 38, 50 }};
    float previousLeft = 0.0f;
    float previousRight = 0.0f;
    float maximumBoundaryDelta = 0.0f;
    for (uint32_t step = 0u; step < expectedCustomNotes.size(); ++step) {
        if (customArp.arpCurrentNote() != expectedCustomNotes[step]) {
            std::cerr << "custom scale-degree pattern produced note "
                      << customArp.arpCurrentNote() << " instead of "
                      << expectedCustomNotes[step] << " at step " << step
                      << "\n";
            return 1;
        }
        for (uint32_t frame = 0u; frame < 6000u; ++frame) {
            float left = 0.0f;
            float right = 0.0f;
            customArp.processFrame(left, right);
            if ((frame < 96u && step > 0u) || frame >= 5904u) {
                maximumBoundaryDelta = std::max(maximumBoundaryDelta,
                    std::max(std::abs(left - previousLeft),
                        std::abs(right - previousRight)));
            }
            previousLeft = left;
            previousRight = right;
        }
    }
    if (maximumBoundaryDelta > 0.35f) {
        std::cerr << "custom arpeggiator boundary was click-like: delta="
                  << maximumBoundaryDelta << "\n";
        return 1;
    }

    for (uint32_t circuit = 0u;
         circuit < s3g::kProcessorStackCircuitCount; ++circuit) {
        auto circuitParams = reference;
        circuitParams.circuit = static_cast<s3g::ProcessorStackCircuit>(circuit);
        circuitParams.bite = 0.78f;
        const auto circuitStats = render(circuitParams,
            {{ 0u, 50 }}, {{ 10000u, 50 }}, 36000u);
        if (!circuitStats.finite || circuitStats.energy < 1.0e-7
            || circuitStats.peak > 1.0f) {
            std::cerr << "pedal circuit " << circuit
                      << " failed: energy=" << circuitStats.energy
                      << " peak=" << circuitStats.peak << "\n";
            return 1;
        }
    }

    for (uint32_t preset = 0u;
         preset < s3g::kProcessorStackFactoryPresetCount; ++preset) {
        const auto presetParams = s3g::processorStackFactoryPreset(preset);
        if (s3g::processorStackFactoryPresetIndex(presetParams)
            != static_cast<int>(preset)) {
            std::cerr << "factory preset " << preset
                      << " did not round-trip through preset matching\n";
            return 1;
        }
        const auto presetStats = render(presetParams,
            {{ 0u, 43 }}, {{ 12000u, 43 }}, 36000u);
        if (!presetStats.finite || presetStats.energy < 1.0e-7
            || presetStats.peak > 1.0f) {
            std::cerr << "factory preset " << preset
                      << " failed: energy=" << presetStats.energy
                      << " peak=" << presetStats.peak << "\n";
            return 1;
        }
    }

    const auto firstSignature = renderSignature(reference);
    const auto secondSignature = renderSignature(reference);
    if (firstSignature != secondSignature) {
        std::cerr << "Processor Stack reset was not deterministic\n";
        return 1;
    }

    for (const double sampleRate : { 8000.0, 44100.0, 96000.0, 192000.0,
                                     768000.0 }) {
        auto stress = reference;
        stress.feedback = 1.0f;
        stress.proximity = 1.0f;
        stress.cone = 1.0f;
        stress.stack = 1.0f;
        stress.bite = 1.0f;
        stress.chaos = 1.0f;
        stress.outputGainDb = 6.0f;
        const uint32_t frames = static_cast<uint32_t>(sampleRate * 0.24);
        const auto stressStats = render(stress, {{ 0u, 24 }},
            {{ frames / 2u, 24 }}, frames, sampleRate, 1.0f, 12.0f);
        const float allowedLimiterStep = 0.75f * (1.0f - std::exp(
            -1.0f / static_cast<float>(sampleRate * 0.00075)));
        if (!stressStats.finite || stressStats.peak > 0.996f
            || stressStats.limiterAttackEvents == 0u
            || stressStats.maximumLimiterGainStep > allowedLimiterStep) {
            std::cerr << "Processor Stack stress failed at " << sampleRate
                      << " Hz, peak=" << stressStats.peak
                      << " delta=" << stressStats.maximumDelta
                      << " limiter-events="
                      << stressStats.limiterAttackEvents
                      << " limiter-step="
                      << stressStats.maximumLimiterGainStep
                      << " protection="
                      << stressStats.maximumSpeakerProtection << "\n";
            return 1;
        }
    }

    s3g::ProcessorStack noteMemory;
    noteMemory.prepare(48000.0);
    noteMemory.noteOn(40, 0.8f);
    noteMemory.noteOn(47, 0.7f);
    if (noteMemory.heldNoteCount() != 2u) {
        std::cerr << "held-note memory did not retain two keys\n";
        return 1;
    }
    noteMemory.noteOff(47);
    if (noteMemory.heldNoteCount() != 1u) {
        std::cerr << "held-note fallback did not restore the prior key\n";
        return 1;
    }
    noteMemory.allNotesOff();
    if (noteMemory.heldNoteCount() != 0u) {
        std::cerr << "all-notes-off did not clear held-note memory\n";
        return 1;
    }

    if (!near(s3g::sanitizeProcessorStackParams(reference).outputGainDb,
            reference.outputGainDb)) {
        std::cerr << "valid Processor Stack parameters changed during sanitation\n";
        return 1;
    }

    std::cout << "Processor Stack smoke test passed\n";
    return 0;
}
