#include "s3g_feedback_shift.h"
#include "s3g_feedback_shift_presets.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <string>

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
    ok &= check(std::abs(s3g::feedbackShiftFrequencyFromControlNorm(0.55f)
            - 0.111111f) < 0.0001f
        && std::abs(s3g::feedbackShiftFrequencyFromControlNorm(0.65f)
            - 1.0f) < 0.0001f,
        "frequency taper did not preserve fine control around zero");
    ok &= check(std::abs(s3g::feedbackShiftFrequencyFromControlNorm(0.0f)
            + 6000.0f) < 0.01f
        && std::abs(s3g::feedbackShiftFrequencyFromControlNorm(1.0f)
            - 6000.0f) < 0.01f,
        "frequency taper did not reach both edges");
    for (float frequency : { -6000.0f, -100.0f, -1.0f, -0.5f, -0.1f,
             -0.01f, 0.0f, 0.01f, 0.1f, 0.5f, 1.0f, 100.0f, 6000.0f }) {
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

    auto sourceParams = s3g::defaultFeedbackShiftParams();
    sourceParams.matrix.fill(0.0f);
    sourceParams.excite = 0.0f;
    sourceParams.auxGrainMix = 0.0f;
    sourceParams.outputGainDb = -6.0f;
    for (auto& node : sourceParams.nodes) {
        node.exciterSource = s3g::FeedbackExciterSource::Off;
        node.regeneration = 0.0f;
        node.levelDb = -60.0f;
        node.pedal = s3g::FeedbackPedalType::Bypass;
    }
    sourceParams.nodes[0u].exciterSource
        = s3g::FeedbackExciterSource::External;
    sourceParams.nodes[0u].regeneration = 1.0f;
    sourceParams.nodes[0u].levelDb = 0.0f;
    sourceParams.nodes[0u].frequencyHz = 0.0f;
    s3g::FeedbackShift externalSource;
    externalSource.setParams(sourceParams);
    externalSource.prepare(48000.0);
    std::array<float, s3g::kFeedbackShiftChannels> sourceInput {};
    std::array<float, s3g::kFeedbackShiftChannels> sourceOutput {};
    std::array<double, s3g::kFeedbackShiftChannels> sourceEnergy {};
    for (uint32_t frame = 0u; frame < 8192u; ++frame) {
        sourceInput.fill(0.0f);
        sourceInput[0u] = std::sin(static_cast<float>(frame)
            * 2.0f * 3.14159265358979323846f * 173.0f / 48000.0f) * 0.25f;
        externalSource.processFrame(sourceInput.data(), sourceOutput.data());
        for (uint32_t channel = 0u;
             channel < s3g::kFeedbackShiftChannels; ++channel) {
            sourceEnergy[channel] += static_cast<double>(sourceOutput[channel])
                * sourceOutput[channel];
        }
    }
    double sourceLeak = 0.0;
    for (uint32_t channel = 1u;
         channel < s3g::kFeedbackShiftChannels; ++channel) {
        sourceLeak += sourceEnergy[channel];
    }
    ok &= check(sourceEnergy[0u] > 1.0e-5 && sourceLeak < 1.0e-18,
        "discrete external excitation did not remain on its assigned node");

    auto toneParams = sourceParams;
    toneParams.nodes[0u].exciterSource = s3g::FeedbackExciterSource::Tone;
    toneParams.nodes[0u].frequencyHz = 110.0f;
    toneParams.excite = 0.75f;
    s3g::FeedbackShift staticTone;
    s3g::FeedbackShift movingTone;
    staticTone.setParams(toneParams);
    toneParams.motionRate = 0.76f;
    toneParams.nodes[0u].motionSource = s3g::FeedbackMotionSource::Lfo;
    toneParams.nodes[0u].motionTarget
        = s3g::FeedbackMotionTarget::Frequency;
    toneParams.nodes[0u].motionDepth = 0.85f;
    toneParams.nodes[0u].motionSlew = 0.0f;
    movingTone.setParams(toneParams);
    staticTone.prepare(48000.0);
    movingTone.prepare(48000.0);
    double motionDifference = 0.0;
    float motionPeak = 0.0f;
    std::array<float, s3g::kFeedbackShiftChannels> staticOutput {};
    std::array<float, s3g::kFeedbackShiftChannels> movingOutput {};
    for (uint32_t frame = 0u; frame < 24000u; ++frame) {
        staticTone.processFrame(staticOutput.data());
        movingTone.processFrame(movingOutput.data());
        motionDifference += std::abs(static_cast<double>(
            staticOutput[0u] - movingOutput[0u]));
        motionPeak = std::max(motionPeak,
            std::abs(movingTone.motionValue(0u)));
    }
    ok &= check(motionDifference > 0.01 && motionPeak > 0.10f,
        "assignable LFO motion did not reach the node frequency target");

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

    struct ShiftInstabilityProbe {
        double earlyEnergy = 0.0;
        double tailEnergy = 0.0;
        float minimumGovernor = 1.0f;
    };
    const auto probeShiftInstability = [&](float color) {
        s3g::FeedbackShift probe;
        auto params = s3g::defaultFeedbackShiftParams();
        params.matrix.fill(0.0f);
        params.excite = 0.0f;
        params.drift = 0.0f;
        params.outputGainDb = -12.0f;
        for (auto& node : params.nodes) {
            node.frequencyHz = 0.0f;
            node.regeneration = 0.0f;
            node.color = 0.0f;
            node.levelDb = -60.0f;
            node.pedal = s3g::FeedbackPedalType::Bypass;
        }
        params.nodes[0u].frequencyHz = 0.20f;
        params.nodes[0u].regeneration = 1.18f;
        params.nodes[0u].color = color;
        params.nodes[0u].levelDb = 0.0f;
        probe.setParams(params);
        probe.prepare(48000.0);
        probe.strike(0u, 1.0f);
        ShiftInstabilityProbe result;
        for (uint32_t frame = 0u; frame < 48000u; ++frame) {
            probe.processFrame(output.data());
            const double energy = static_cast<double>(output[0u])
                * output[0u];
            (frame < 36000u ? result.earlyEnergy : result.tailEnergy)
                += energy;
            result.minimumGovernor = std::min(result.minimumGovernor,
                probe.minimumGovernor());
        }
        return result;
    };
    const auto cleanShift = probeShiftInstability(0.0f);
    const auto coloredShift = probeShiftInstability(1.0f);
    const bool restoredShiftInstability = coloredShift.tailEnergy
            > cleanShift.tailEnergy * 4.0
        && coloredShift.minimumGovernor < cleanShift.minimumGovernor * 0.92f;
    if (!restoredShiftInstability) {
        std::cerr << "SHIFT instability probe: clean tail="
                  << cleanShift.tailEnergy << ", colored tail="
                  << coloredShift.tailEnergy << ", governors="
                  << cleanShift.minimumGovernor << " / "
                  << coloredShift.minimumGovernor << '\n';
    }
    ok &= check(restoredShiftInstability,
        "near-zero REGEN/COLOR no longer opens the local SHIFT feedback loop");

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

    ok &= check(s3g::feedbackPedalControlCount(
            s3g::FeedbackPedalType::Repeater) == 9u
        && s3g::feedbackPedalControlCount(
            s3g::FeedbackPedalType::TimeMangler) == 9u
        && s3g::feedbackPedalControlInfo(
            s3g::FeedbackPedalType::Repeater, 7u).storageSlot == 8u
        && std::string(s3g::feedbackPedalControlInfo(
            s3g::FeedbackPedalType::TimeMangler, 5u).label) == "XFADE",
        "temporal ecology control surface is incomplete");

    s3g::FeedbackShift linkedTemporal;
    auto linkedParams = s3g::defaultFeedbackShiftParams();
    linkedParams.matrix.fill(0.0f);
    linkedParams.excite = 0.0f;
    linkedParams.nodes[0u].pedal = s3g::FeedbackPedalType::Repeater;
    linkedParams.nodes[0u].frequencyHz = 0.0f;
    linkedParams.nodes[0u].regeneration = 1.0f;
    linkedParams.nodes[0u].pedalAmount = 0.0f;
    linkedParams.nodes[0u].pedalTone = 0.0f;
    linkedParams.nodes[0u].pedalMix = 1.0f;
    linkedParams.nodes[0u].pedalExtra[1u] = 1.0f; // sensitivity
    linkedParams.nodes[0u].pedalExtra[2u] = 0.65f; // crossfade
    linkedParams.nodes[0u].pedalExtra[3u] = 0.72f; // capture drift
    linkedParams.nodes[0u].pedalExtra[4u] = 1.0f; // link from node two
    linkedParams.nodes[1u].frequencyHz = 0.0f;
    linkedParams.nodes[1u].regeneration = 1.0f;
    linkedTemporal.setParams(linkedParams);
    linkedTemporal.prepare(48000.0);
    linkedTemporal.strike(1u, 1.0f);
    bool linkedCapture = false;
    bool linkedPlayback = false;
    for (uint32_t frame = 0u; frame < 4096u; ++frame) {
        linkedTemporal.processFrame(output.data());
        linkedCapture = linkedCapture
            || linkedTemporal.temporalPhase(0u) == 1u;
        linkedPlayback = linkedPlayback
            || linkedTemporal.temporalPhase(0u) == 2u;
        for (float sample : output) {
            ok &= check(std::isfinite(sample) && std::abs(sample) <= 1.0f,
                "linked temporal capture produced invalid audio");
        }
    }
    ok &= check(linkedCapture && linkedPlayback,
        "adjacent node did not trigger and seed the linked repeater");

    ok &= check(s3g::kFeedbackAuxControlCount == 14u
        && std::string(s3g::feedbackAuxControlInfo(4u).label) == "TILT"
        && std::string(s3g::feedbackAuxControlInfo(5u).label) == "RETURN"
        && std::string(s3g::feedbackAuxControlInfo(10u).label)
            == "COHERENCE"
        && s3g::feedbackAuxControlInfo(10u).storageSlot == 20u
        && std::string(s3g::feedbackAuxControlInfo(11u).label)
            == "LANE DRIFT"
        && s3g::feedbackAuxControlInfo(11u).storageSlot == 21u
        && std::string(s3g::feedbackAuxControlInfo(13u).label)
            == "GRAIN MIX"
        && s3g::feedbackAuxControlInfo(13u).storageSlot == 11u,
        "AUX wall/grain control surface is incomplete");

    auto noRouteDryParams = s3g::defaultFeedbackShiftParams();
    noRouteDryParams.matrix.fill(0.0f);
    noRouteDryParams.excite = 0.0f;
    for (auto& node : noRouteDryParams.nodes) {
        node.frequencyHz = 0.0f;
        node.regeneration = 1.0f;
        node.pedal = s3g::FeedbackPedalType::Bypass;
    }
    noRouteDryParams.auxGrainMix = 0.0f;
    auto noRouteWallParams = noRouteDryParams;
    noRouteWallParams.auxPress = 1.0f;
    noRouteWallParams.auxSaturation = 1.0f;
    noRouteWallParams.auxFold = 1.0f;
    noRouteWallParams.auxClip = 1.0f;
    noRouteWallParams.auxMix = 1.0f;
    auto noRouteGrainParams = noRouteDryParams;
    noRouteGrainParams.auxGrainSize = 0.12f;
    noRouteGrainParams.auxGrainDensity = 1.0f;
    noRouteGrainParams.auxGrainScatter = 1.0f;
    noRouteGrainParams.auxGrainMix = 1.0f;
    s3g::FeedbackShift noRouteDry;
    s3g::FeedbackShift noRouteWall;
    s3g::FeedbackShift noRouteGrain;
    noRouteDry.setParams(noRouteDryParams);
    noRouteWall.setParams(noRouteWallParams);
    noRouteGrain.setParams(noRouteGrainParams);
    noRouteDry.prepare(48000.0);
    noRouteWall.prepare(48000.0);
    noRouteGrain.prepare(48000.0);
    noRouteDry.strikeAll(1.0f);
    noRouteWall.strikeAll(1.0f);
    noRouteGrain.strikeAll(1.0f);
    double noRouteWallDifference = 0.0;
    double noRouteGrainDifference = 0.0;
    std::array<float, s3g::kFeedbackShiftChannels> noRouteDryOutput {};
    std::array<float, s3g::kFeedbackShiftChannels> noRouteWallOutput {};
    std::array<float, s3g::kFeedbackShiftChannels> noRouteGrainOutput {};
    for (uint32_t frame = 0u; frame < 16384u; ++frame) {
        noRouteDry.processFrame(noRouteDryOutput.data());
        noRouteWall.processFrame(noRouteWallOutput.data());
        noRouteGrain.processFrame(noRouteGrainOutput.data());
        for (uint32_t channel = 0u;
             channel < s3g::kFeedbackShiftChannels; ++channel) {
            noRouteWallDifference += std::abs(static_cast<double>(
                noRouteDryOutput[channel] - noRouteWallOutput[channel]));
            noRouteGrainDifference += std::abs(static_cast<double>(
                noRouteDryOutput[channel] - noRouteGrainOutput[channel]));
        }
    }
    ok &= check(noRouteWallDifference < 1.0e-12,
        "feedback AUX wall leaked into the audible dry output");
    ok &= check(noRouteGrainDifference > 0.01,
        "granulator did not move to the post-network output path");

    auto dryAuxParams = s3g::defaultFeedbackShiftParams();
    dryAuxParams.matrix.fill(0.0f);
    dryAuxParams.excite = 0.0f;
    dryAuxParams.outputGainDb = -6.0f;
    for (auto& node : dryAuxParams.nodes) {
        node.frequencyHz = 0.0f;
        node.regeneration = 0.0f;
        node.levelDb = -60.0f;
        node.pedal = s3g::FeedbackPedalType::Bypass;
    }
    dryAuxParams.nodes[0u].regeneration = 0.92f;
    dryAuxParams.nodes[0u].levelDb = 0.0f;
    dryAuxParams.matrix[0u] = 0.82f;
    dryAuxParams.auxGrainMix = 0.0f;
    auto granularParams = dryAuxParams;
    granularParams.auxGrainSize = 0.22f;
    granularParams.auxGrainDensity = 0.72f;
    granularParams.auxGrainScatter = 0.84f;
    granularParams.auxGrainPitch = 0.32f;
    granularParams.auxGrainEdge = 0.72f;
    granularParams.auxGrainCoherence = 1.0f;
    granularParams.auxGrainLaneDrift = 1.0f;
    granularParams.auxGrainMix = 1.0f;
    granularParams.auxMix = 0.0f;
    auto grainDryParams = granularParams;
    grainDryParams.auxGrainMix = 0.0f;
    auto sendMutedParams = granularParams;
    sendMutedParams.auxSend[0u] = 0.0f;
    auto independentParams = granularParams;
    independentParams.auxGrainCoherence = 0.0f;
    s3g::FeedbackShift dryAux;
    s3g::FeedbackShift granularAux;
    s3g::FeedbackShift grainDryAux;
    s3g::FeedbackShift sendMutedAux;
    s3g::FeedbackShift independentAux;
    dryAux.setParams(dryAuxParams);
    granularAux.setParams(granularParams);
    grainDryAux.setParams(grainDryParams);
    sendMutedAux.setParams(sendMutedParams);
    independentAux.setParams(independentParams);
    dryAux.prepare(48000.0);
    granularAux.prepare(48000.0);
    grainDryAux.prepare(48000.0);
    sendMutedAux.prepare(48000.0);
    independentAux.prepare(48000.0);
    dryAux.strike(0u, 1.0f);
    granularAux.strike(0u, 1.0f);
    grainDryAux.strike(0u, 1.0f);
    sendMutedAux.strike(0u, 1.0f);
    independentAux.strike(0u, 1.0f);
    std::array<float, s3g::kFeedbackShiftChannels> dryAuxOutput {};
    std::array<float, s3g::kFeedbackShiftChannels> granularOutput {};
    std::array<float, s3g::kFeedbackShiftChannels> grainDryOutput {};
    std::array<float, s3g::kFeedbackShiftChannels> sendMutedOutput {};
    std::array<float, s3g::kFeedbackShiftChannels> independentOutput {};
    double auxDifference = 0.0;
    double grainMixDifference = 0.0;
    double mutedSendDifference = 0.0;
    double coherenceDifference = 0.0;
    double auxLaneLeak = 0.0;
    bool activeGrainsObserved = false;
    for (uint32_t frame = 0u; frame < 16384u; ++frame) {
        dryAux.processFrame(dryAuxOutput.data());
        granularAux.processFrame(granularOutput.data());
        grainDryAux.processFrame(grainDryOutput.data());
        sendMutedAux.processFrame(sendMutedOutput.data());
        independentAux.processFrame(independentOutput.data());
        auxDifference += std::abs(static_cast<double>(
            dryAuxOutput[0u] - granularOutput[0u]));
        grainMixDifference += std::abs(static_cast<double>(
            grainDryOutput[0u] - granularOutput[0u]));
        mutedSendDifference += std::abs(static_cast<double>(
            granularOutput[0u] - sendMutedOutput[0u]));
        coherenceDifference += std::abs(static_cast<double>(
            granularOutput[0u] - independentOutput[0u]));
        activeGrainsObserved = activeGrainsObserved
            || granularAux.auxGrainActivity() > 0.05f;
        for (uint32_t channel = 1u;
             channel < s3g::kFeedbackShiftChannels; ++channel) {
            auxLaneLeak += static_cast<double>(granularOutput[channel])
                * granularOutput[channel];
        }
        for (float sample : granularOutput) {
            ok &= check(std::isfinite(sample) && std::abs(sample) <= 1.0f,
                "AUX wall/grain chain produced invalid audio");
        }
    }
    ok &= check(auxDifference > 0.01 && activeGrainsObserved,
        "post-network granulator did not transform the signal");
    ok &= check(grainMixDifference > 0.01,
        "GRAIN MIX did not independently blend the granulator");
    ok &= check(mutedSendDifference < 1.0e-12,
        "feedback AUX send unexpectedly changed the post granulator");
    ok &= check(coherenceDifference > 0.01,
        "COHERENCE and LANE DRIFT did not separate lane grain behavior");
    ok &= check(auxLaneLeak < 1.0e-18,
        "post granulator borrowed audio between discrete channel buffers");

    auto wallParams = dryAuxParams;
    wallParams.auxPress = 0.82f;
    wallParams.auxSaturation = 0.78f;
    wallParams.auxFold = 0.72f;
    wallParams.auxClip = 0.64f;
    wallParams.auxMix = 1.0f;
    auto wallMutedParams = wallParams;
    wallMutedParams.auxSend[0u] = 0.0f;
    s3g::FeedbackShift wallAux;
    s3g::FeedbackShift wallMutedAux;
    wallAux.setParams(wallParams);
    wallMutedAux.setParams(wallMutedParams);
    wallAux.prepare(48000.0);
    wallMutedAux.prepare(48000.0);
    wallAux.strike(0u, 1.0f);
    wallMutedAux.strike(0u, 1.0f);
    dryAux.panic();
    dryAux.strike(0u, 1.0f);
    double wallDifference = 0.0;
    double wallMutedDifference = 0.0;
    float wallActivityPeak = 0.0f;
    for (uint32_t frame = 0u; frame < 16384u; ++frame) {
        dryAux.processFrame(dryAuxOutput.data());
        wallAux.processFrame(granularOutput.data());
        wallMutedAux.processFrame(sendMutedOutput.data());
        wallDifference += std::abs(static_cast<double>(
            dryAuxOutput[0u] - granularOutput[0u]));
        wallMutedDifference += std::abs(static_cast<double>(
            dryAuxOutput[0u] - sendMutedOutput[0u]));
        wallActivityPeak = std::max(wallActivityPeak, wallAux.auxActivity());
    }
    ok &= check(wallDifference > 0.01 && wallActivityPeak > 1.0e-8f,
        "feedback AUX wall did not recirculate through the matrix source");
    ok &= check(wallMutedDifference < 1.0e-12,
        "zero lane send did not remove that lane from the feedback AUX");

    for (uint32_t preset = 0u;
         preset < s3g::kFeedbackShiftPresetCount; ++preset) {
        const auto presetParams = s3g::feedbackShiftPreset(preset);
        uint32_t movingNodes = 0u;
        for (const auto& node : presetParams.nodes) {
            movingNodes += node.motionSource != s3g::FeedbackMotionSource::Off
                ? 1u : 0u;
        }
        const auto sendBounds = std::minmax_element(
            presetParams.auxSend.begin(), presetParams.auxSend.end());
        ok &= check(presetParams.auxMix > 0.0f
                && presetParams.auxMix <= 0.20f
                && presetParams.auxGrainMix >= 0.0f
                && presetParams.auxGrainMix <= 1.0f
                && presetParams.auxGrainCoherence >= 0.0f
                && presetParams.auxGrainCoherence <= 1.0f
                && presetParams.auxGrainLaneDrift >= 0.0f
                && presetParams.auxGrainLaneDrift <= 1.0f
                && *sendBounds.first >= 0.0f
                && *sendBounds.second <= 1.0f
                && *sendBounds.second - *sendBounds.first > 0.05f
                && (preset == 0u || movingNodes >= 2u),
            "factory preset did not use a bounded AUX source return");
        s3g::FeedbackShift presetSynth;
        presetSynth.setParams(presetParams);
        presetSynth.prepare(48000.0);
        presetSynth.setTransport(120.0, true);
        presetSynth.strikeAll(0.8f);
        float presetAuxActivity = 0.0f;
        double presetEnergy = 0.0;
        for (uint32_t frame = 0u; frame < 16384u; ++frame) {
            presetSynth.processFrame(output.data());
            presetAuxActivity = std::max(presetAuxActivity,
                presetSynth.auxActivity());
            for (float sample : output) {
                ok &= check(std::isfinite(sample) && std::abs(sample) <= 1.0f,
                    "built-in preset produced invalid audio");
                presetEnergy += static_cast<double>(sample) * sample;
            }
        }
        ok &= check(presetAuxActivity > 1.0e-8f,
            "factory preset AUX return remained inactive");
        ok &= check(presetEnergy > 1.0e-10,
            "factory preset produced no audible energy");
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
        && randomA.matrix == randomB.matrix
        && randomA.auxSend == randomB.auxSend
        && randomA.auxMix >= 0.04f && randomA.auxMix <= 0.17f
        && randomA.auxGrainCoherence >= 0.08f
        && randomA.auxGrainCoherence <= 0.95f
        && randomA.auxGrainLaneDrift >= 0.20f
        && randomA.auxGrainLaneDrift <= 1.0f
        && randomA.auxGrainMix >= 0.10f
        && randomA.auxGrainMix <= 0.65f
        && randomA.motionRate >= 0.14f && randomA.motionRate <= 0.72f
        && static_cast<uint32_t>(randomA.nodes[0u].exciterSource)
            < s3g::kFeedbackExciterSourceCount
        && static_cast<uint32_t>(randomA.nodes[0u].motionSource)
            < s3g::kFeedbackMotionSourceCount
        && static_cast<uint32_t>(randomA.nodes[0u].motionTarget)
            < s3g::kFeedbackMotionTargetCount
        && std::abs(randomA.nodes[0u].motionDepth) <= 1.0f
        && randomA.nodes[0u].motionSlew >= 0.0f
        && randomA.nodes[0u].motionSlew <= 1.0f,
        "randomization was not reproducible or feedback-return aware");
    auto preservedOutput = s3g::defaultFeedbackShiftParams();
    preservedOutput.outputGainDb = -7.3f;
    preservedOutput.outputMode = s3g::FeedbackShiftOutputMode::StereoRing;
    preservedOutput.outputRotationDeg = 47.0f;
    const auto topologySafeRandom = s3g::randomFeedbackShiftParams(
        0x87654321u, preservedOutput);
    ok &= check(topologySafeRandom.outputGainDb
            == preservedOutput.outputGainDb
        && topologySafeRandom.outputMode == preservedOutput.outputMode
        && topologySafeRandom.outputRotationDeg
            == preservedOutput.outputRotationDeg,
        "RANDOM changed user-owned output level, mode, or rotation");
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
