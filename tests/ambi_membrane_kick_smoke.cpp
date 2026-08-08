#include "s3g_ambi_membrane_kick.h"
#include "s3g_ambi_membrane_kick_presets.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>

namespace {

constexpr double kSampleRate = 48000.0;

bool silenceAndFiniteProbe()
{
    s3g::AmbiMembraneKick kick;
    kick.prepare(kSampleRate);
    std::array<float, s3g::kAmbiMembraneKickChannels> frame {};
    for (uint32_t sample = 0u; sample < 512u; ++sample) {
        kick.processFrame(frame.data(), static_cast<uint32_t>(frame.size()));
        for (const float value : frame) {
            if (value != 0.0f || !std::isfinite(value)) {
                std::cerr << "membrane emitted sound before a strike\n";
                return false;
            }
        }
    }

    auto params = kick.params();
    params.tuneHz = std::numeric_limits<float>::quiet_NaN();
    params.decaySeconds = std::numeric_limits<float>::infinity();
    params.strikeX = 4.0f;
    params.strikeY = -4.0f;
    params.order = 99u;
    kick.setParams(params);
    params = kick.params();
    if (params.tuneHz != 43.0f || params.decaySeconds != 1.45f
        || params.order != s3g::kAmbiMembraneKickStereoDownmix
        || std::sqrt(params.strikeX * params.strikeX
            + params.strikeY * params.strikeY) > 0.981f) {
        std::cerr << "membrane parameter sanitization failed\n";
        return false;
    }
    return true;
}

bool deepBassAndSpatialProbe()
{
    s3g::AmbiMembraneKick kick;
    kick.prepare(kSampleRate);
    auto params = kick.params();
    params.shape = s3g::AmbiMembraneShape::Circle;
    params.shapeAmount = 0.0f;
    params.tuneHz = 43.0f;
    params.click = 0.0f;
    params.damping = 0.72f;
    params.spatialSpread = 0.78f;
    params.outputGainDb = -12.0f;
    kick.setParams(params);
    kick.trigger(1.0f, 36);

    std::array<float, s3g::kAmbiMembraneKickChannels> frame {};
    double wEnergy = 0.0;
    double directionalEnergy = 0.0;
    double low = 0.0;
    double previousLow = 0.0;
    uint32_t upwardCrossings = 0u;
    float peak = 0.0f;
    const uint32_t start = static_cast<uint32_t>(kSampleRate * 0.40);
    const uint32_t end = static_cast<uint32_t>(kSampleRate * 1.20);
    for (uint32_t sample = 0u; sample < end; ++sample) {
        kick.processFrame(frame.data(), static_cast<uint32_t>(frame.size()));
        for (const float value : frame) {
            if (!std::isfinite(value)) {
                std::cerr << "membrane produced a non-finite sample\n";
                return false;
            }
            peak = std::max(peak, std::fabs(value));
        }
        wEnergy += static_cast<double>(frame[0]) * frame[0];
        for (uint32_t channel = 1u; channel < frame.size(); ++channel) {
            directionalEnergy += static_cast<double>(frame[channel])
                * frame[channel];
        }
        // A simple low pass rejects the membrane overtones before counting
        // the settled body pitch.
        low += (static_cast<double>(frame[0]) - low) * 0.0105;
        if (sample >= start && previousLow <= 0.0 && low > 0.0) {
            ++upwardCrossings;
        }
        previousLow = low;
    }
    const double measuredHz = static_cast<double>(upwardCrossings)
        / (static_cast<double>(end - start) / kSampleRate);
    if (peak < 0.01f || peak > 0.98f || measuredHz < 38.0
        || measuredHz > 49.0) {
        std::cerr << "deep body pitch or level was out of range: "
                  << peak << " / " << measuredHz << " Hz\n";
        return false;
    }
    if (wEnergy <= 1.0e-8 || directionalEnergy / wEnergy < 0.025) {
        std::cerr << "membrane did not occupy higher-order space: "
                  << directionalEnergy / std::max(1.0e-12, wEnergy) << "\n";
        return false;
    }
    return true;
}

bool spatialAutomationContinuityProbe()
{
    s3g::AmbiMembraneKick settled;
    settled.prepare(kSampleRate);
    auto params = settled.params();
    params.tuneHz = 40.0f;
    params.pitchSweepSemitones = 0.0f;
    params.decaySeconds = 5.0f;
    params.click = 0.0f;
    params.drive = 0.10f;
    params.spatialSpread = 0.20f;
    params.membraneDepth = 0.15f;
    params.rotationDeg = -120.0f;
    params.outputGainDb = -8.0f;
    settled.setParams(params);
    settled.trigger(1.0f, 36);

    std::array<float, s3g::kAmbiMembraneKickChannels> frame {};
    for (uint32_t sample = 0u; sample < 4096u; ++sample) {
        settled.processFrame(frame.data(), static_cast<uint32_t>(frame.size()));
    }

    const auto verifyTransition = [&](const char* label,
                                      float spread,
                                      float depth,
                                      float rotation) {
        auto automated = settled;
        auto unchanged = settled;
        auto changedParams = automated.params();
        changedParams.spatialSpread = spread;
        changedParams.membraneDepth = depth;
        changedParams.rotationDeg = rotation;
        automated.setParams(changedParams);

        std::array<float, s3g::kAmbiMembraneKickChannels> automatedFrame {};
        std::array<float, s3g::kAmbiMembraneKickChannels> unchangedFrame {};
        automated.processFrame(automatedFrame.data(),
            static_cast<uint32_t>(automatedFrame.size()));
        unchanged.processFrame(unchangedFrame.data(),
            static_cast<uint32_t>(unchangedFrame.size()));
        float firstSampleDifference = 0.0f;
        for (uint32_t channel = 0u; channel < automatedFrame.size(); ++channel) {
            firstSampleDifference = std::max(firstSampleDifference,
                std::fabs(automatedFrame[channel] - unchangedFrame[channel]));
        }

        float settledDifference = 0.0f;
        for (uint32_t sample = 0u; sample < 4096u; ++sample) {
            automated.processFrame(automatedFrame.data(),
                static_cast<uint32_t>(automatedFrame.size()));
            unchanged.processFrame(unchangedFrame.data(),
                static_cast<uint32_t>(unchangedFrame.size()));
            if (sample >= 3584u) {
                for (uint32_t channel = 0u;
                     channel < automatedFrame.size(); ++channel) {
                    settledDifference = std::max(settledDifference,
                        std::fabs(automatedFrame[channel]
                            - unchangedFrame[channel]));
                }
            }
        }
        if (firstSampleDifference > 0.002f || settledDifference < 0.002f) {
            std::cerr << label << " spatial automation discontinuity: "
                      << firstSampleDifference << " / "
                      << settledDifference << "\n";
            return false;
        }
        return true;
    };

    return verifyTransition("rotation", 0.20f, 0.15f, 120.0f)
        && verifyTransition("spread", 1.0f, 0.15f, -120.0f)
        && verifyTransition("depth", 0.20f, 1.0f, -120.0f);
}

bool orderAndShapeProbe()
{
    s3g::AmbiMembraneKick kick;
    kick.prepare(kSampleRate);
    auto params = kick.params();
    params.order = 1u;
    params.click = 0.0f;
    params.shape = s3g::AmbiMembraneShape::Circle;
    kick.setParams(params);
    kick.trigger(0.8f, 36);

    std::array<float, s3g::kAmbiMembraneKickChannels> circle {};
    double silentHigherOrders = 0.0;
    for (uint32_t sample = 0u; sample < 4096u; ++sample) {
        kick.processFrame(circle.data(), static_cast<uint32_t>(circle.size()));
        for (uint32_t channel = 4u; channel < circle.size(); ++channel) {
            silentHigherOrders += std::fabs(circle[channel]);
        }
    }
    if (silentHigherOrders != 0.0) {
        std::cerr << "first-order mode leaked into higher-order channels\n";
        return false;
    }

    params.order = 3u;
    params.shape = s3g::AmbiMembraneShape::Triangle;
    params.shapeAmount = 1.0f;
    kick.setParams(params);
    const auto trianglePosition = kick.patchPosition(15u);
    params.shape = s3g::AmbiMembraneShape::Circle;
    kick.setParams(params);
    const auto circlePosition = kick.patchPosition(15u);
    const float positionDifference = std::fabs(
        trianglePosition[0] - circlePosition[0])
        + std::fabs(trianglePosition[1] - circlePosition[1]);
    if (positionDifference < 0.02f) {
        std::cerr << "shape selection did not reshape the membrane surface\n";
        return false;
    }

    params.shape = s3g::AmbiMembraneShape::Irregular;
    kick.setParams(params);
    const float irregularRatio = kick.modeRatio(1u);
    params.shape = s3g::AmbiMembraneShape::Circle;
    kick.setParams(params);
    if (std::fabs(irregularRatio - kick.modeRatio(1u)) < 0.05f) {
        std::cerr << "shape selection did not alter modal tuning\n";
        return false;
    }
    return true;
}

bool directPickupProbe()
{
    s3g::AmbiMembraneKick kick;
    kick.prepare(kSampleRate);
    auto params = kick.params();
    params.order = s3g::kAmbiMembraneKickDirectPickups;
    params.click = 0.02f;
    params.strikeX = 0.31f;
    params.strikeY = -0.17f;
    params.outputGainDb = -8.0f;
    kick.setParams(params);
    kick.trigger(0.9f, 36);

    std::array<double, s3g::kAmbiMembraneKickChannels> energy {};
    std::array<float, s3g::kAmbiMembraneKickChannels> frame {};
    double outerDifference = 0.0;
    float peak = 0.0f;
    for (uint32_t sample = 0u; sample < 8192u; ++sample) {
        kick.processFrame(frame.data(), static_cast<uint32_t>(frame.size()));
        for (uint32_t channel = 0u; channel < frame.size(); ++channel) {
            if (!std::isfinite(frame[channel])) {
                std::cerr << "direct membrane pickup was non-finite\n";
                return false;
            }
            energy[channel] += static_cast<double>(frame[channel])
                * frame[channel];
            peak = std::max(peak, std::fabs(frame[channel]));
        }
        outerDifference += std::fabs(frame[0u] - frame[15u]);
    }
    if (peak < 0.005f || peak > 0.98f || outerDifference < 0.01) {
        std::cerr << "direct membrane pickups lacked level or variation: "
                  << peak << " / " << outerDifference << "\n";
        return false;
    }
    for (uint32_t channel = 0u; channel < energy.size(); ++channel) {
        if (energy[channel] <= 1.0e-7) {
            std::cerr << "direct membrane pickup " << channel + 1u
                      << " was silent\n";
            return false;
        }
    }

    auto spatialEdit = kick;
    auto unchanged = kick;
    auto editedParams = spatialEdit.params();
    editedParams.spatialSpread = 0.0f;
    editedParams.membraneDepth = 1.0f;
    editedParams.rotationDeg = 177.0f;
    spatialEdit.setParams(editedParams);
    std::array<float, s3g::kAmbiMembraneKickChannels> editedFrame {};
    std::array<float, s3g::kAmbiMembraneKickChannels> unchangedFrame {};
    for (uint32_t sample = 0u; sample < 512u; ++sample) {
        spatialEdit.processFrame(
            editedFrame.data(), static_cast<uint32_t>(editedFrame.size()));
        unchanged.processFrame(unchangedFrame.data(),
            static_cast<uint32_t>(unchangedFrame.size()));
        if (editedFrame != unchangedFrame) {
            std::cerr << "ambisonic space controls altered direct pickups\n";
            return false;
        }
    }

    auto hoaTransition = kick;
    auto directReference = kick;
    auto hoaParams = hoaTransition.params();
    hoaParams.order = 3u;
    hoaTransition.setParams(hoaParams);
    hoaTransition.processFrame(
        editedFrame.data(), static_cast<uint32_t>(editedFrame.size()));
    directReference.processFrame(unchangedFrame.data(),
        static_cast<uint32_t>(unchangedFrame.size()));
    float firstFormatDifference = 0.0f;
    for (uint32_t channel = 0u; channel < editedFrame.size(); ++channel) {
        firstFormatDifference = std::max(firstFormatDifference,
            std::fabs(editedFrame[channel] - unchangedFrame[channel]));
    }
    if (firstFormatDifference > 0.002f) {
        std::cerr << "direct/ambisonic format switch clicked: "
                  << firstFormatDifference << "\n";
        return false;
    }
    return true;
}

bool stereoDownmixProbe()
{
    s3g::AmbiMembraneKick direct;
    s3g::AmbiMembraneKick stereo;
    direct.prepare(kSampleRate);
    stereo.prepare(kSampleRate);
    auto params = direct.params();
    params.shape = s3g::AmbiMembraneShape::Ellipse;
    params.shapeAmount = 0.85f;
    params.strikeX = 0.47f;
    params.strikeY = -0.21f;
    params.click = 0.03f;
    params.outputGainDb = -30.0f;
    params.order = s3g::kAmbiMembraneKickDirectPickups;
    direct.setParams(params);
    params.order = s3g::kAmbiMembraneKickStereoDownmix;
    stereo.setParams(params);
    direct.trigger(0.35f, 36);
    stereo.trigger(0.35f, 36);

    std::array<float, s3g::kAmbiMembraneKickChannels> directFrame {};
    std::array<float, s3g::kAmbiMembraneKickChannels> stereoFrame {};
    std::array<double, 2u> stereoEnergy {};
    double stereoDifference = 0.0;
    float maximumFoldError = 0.0f;
    double higherLaneEnergy = 0.0;
    constexpr float foldNormalization = 0.25f;
    constexpr float halfPi = 1.57079632679489661923f;
    for (uint32_t sample = 0u; sample < 4096u; ++sample) {
        direct.processFrame(
            directFrame.data(), static_cast<uint32_t>(directFrame.size()));
        stereo.processFrame(
            stereoFrame.data(), static_cast<uint32_t>(stereoFrame.size()));
        std::array<float, 2u> expected {};
        for (uint32_t patch = 0u;
             patch < s3g::kAmbiMembraneKickPatches; ++patch) {
            const float x = direct.patchPosition(patch)[0u];
            const float pan = std::clamp(x * 0.5f + 0.5f, 0.0f, 1.0f);
            expected[0u] += directFrame[patch] * std::cos(pan * halfPi)
                * foldNormalization;
            expected[1u] += directFrame[patch] * std::sin(pan * halfPi)
                * foldNormalization;
        }
        for (uint32_t channel = 0u; channel < 2u; ++channel) {
            if (!std::isfinite(stereoFrame[channel])) {
                std::cerr << "stereo membrane downmix was non-finite\n";
                return false;
            }
            maximumFoldError = std::max(maximumFoldError,
                std::fabs(stereoFrame[channel] - expected[channel]));
            stereoEnergy[channel] += static_cast<double>(
                stereoFrame[channel]) * stereoFrame[channel];
        }
        stereoDifference += std::fabs(
            stereoFrame[0u] - stereoFrame[1u]);
        for (uint32_t channel = 2u; channel < stereoFrame.size(); ++channel) {
            higherLaneEnergy += std::fabs(stereoFrame[channel]);
        }
    }
    if (stereoEnergy[0u] <= 1.0e-8 || stereoEnergy[1u] <= 1.0e-8
        || stereoDifference <= 1.0e-4 || higherLaneEnergy != 0.0
        || maximumFoldError > 1.0e-6f) {
        std::cerr << "stereo membrane downmix contract failed: "
                  << stereoEnergy[0u] << " / " << stereoEnergy[1u]
                  << " / " << stereoDifference << " / "
                  << higherLaneEnergy << " / " << maximumFoldError << "\n";
        return false;
    }

    auto spatialEdit = stereo;
    auto unchanged = stereo;
    auto editedParams = spatialEdit.params();
    editedParams.spatialSpread = 0.0f;
    editedParams.membraneDepth = 1.0f;
    editedParams.rotationDeg = 177.0f;
    spatialEdit.setParams(editedParams);
    for (uint32_t sample = 0u; sample < 512u; ++sample) {
        spatialEdit.processFrame(
            stereoFrame.data(), static_cast<uint32_t>(stereoFrame.size()));
        unchanged.processFrame(
            directFrame.data(), static_cast<uint32_t>(directFrame.size()));
        if (stereoFrame != directFrame) {
            std::cerr << "ambisonic space controls altered stereo downmix\n";
            return false;
        }
    }
    return true;
}

bool noteTrackingProbe()
{
    s3g::AmbiMembraneKick kick;
    kick.prepare(kSampleRate);
    auto params = kick.params();
    if (params.noteTracking != 1.0f) {
        std::cerr << "membrane did not default to chromatic MIDI tracking\n";
        return false;
    }
    params.pitchSweepSemitones = 0.0f;
    params.click = 0.0f;
    params.outputGainDb = -18.0f;
    kick.setParams(params);
    kick.trigger(1.0f, 48);

    std::array<float, s3g::kAmbiMembraneKickChannels> frame {};
    double low = 0.0;
    double previousLow = 0.0;
    uint32_t crossings = 0u;
    const uint32_t frames = static_cast<uint32_t>(kSampleRate * 0.5);
    for (uint32_t sample = 0u; sample < frames; ++sample) {
        kick.processFrame(frame.data(), static_cast<uint32_t>(frame.size()));
        low += (static_cast<double>(frame[0]) - low) * 0.0105;
        if (sample > 1000u && previousLow <= 0.0 && low > 0.0) ++crossings;
        previousLow = low;
    }
    const double frequency = static_cast<double>(crossings)
        / ((static_cast<double>(frames - 1001u)) / kSampleRate);
    if (frequency < 78.0 || frequency > 95.0) {
        std::cerr << "MIDI note tracking did not transpose the membrane: "
                  << frequency << " Hz\n";
        return false;
    }
    return true;
}

double velocityEnergy(float velocity)
{
    s3g::AmbiMembraneKick kick;
    kick.prepare(kSampleRate);
    auto params = kick.params();
    params.pitchSweepSemitones = 0.0f;
    params.click = 0.0f;
    params.drive = 0.0f;
    params.outputGainDb = -18.0f;
    kick.setParams(params);
    kick.trigger(velocity, 36);

    std::array<float, s3g::kAmbiMembraneKickChannels> frame {};
    double energy = 0.0;
    for (uint32_t sample = 0u; sample < 4096u; ++sample) {
        kick.processFrame(frame.data(), static_cast<uint32_t>(frame.size()));
        for (const float value : frame) {
            energy += static_cast<double>(value) * value;
        }
    }
    return energy;
}

bool velocityResponseProbe()
{
    const double soft = velocityEnergy(0.20f);
    const double hard = velocityEnergy(1.0f);
    if (soft <= 1.0e-9 || hard < soft * 8.0) {
        std::cerr << "MIDI velocity did not scale membrane strike energy: "
                  << soft << " / " << hard << "\n";
        return false;
    }
    return true;
}

bool strikePlacementProbe()
{
    s3g::AmbiMembraneKick kick;
    kick.prepare(kSampleRate);
    auto params = kick.params();
    params.strikeX = 0.35f;
    params.strikeY = -0.20f;
    params.strikeMode = s3g::AmbiMembraneStrikeMode::Fixed;
    kick.setParams(params);
    kick.trigger(1.0f, 36);
    const auto fixed = kick.actualStrikePosition();
    if (std::fabs(fixed[0u] - 0.35f) > 1.0e-6f
        || std::fabs(fixed[1u] + 0.20f) > 1.0e-6f) {
        std::cerr << "fixed strike automation did not reach the membrane\n";
        return false;
    }

    params.strikeMode = s3g::AmbiMembraneStrikeMode::RandomArea;
    kick.setParams(params);
    kick.trigger(1.0f, 36);
    const auto randomA = kick.actualStrikePosition();
    kick.trigger(1.0f, 36);
    const auto randomB = kick.actualStrikePosition();
    const auto radius = [](const std::array<float, 2u>& point) {
        return std::sqrt(point[0u] * point[0u] + point[1u] * point[1u]);
    };
    if (radius(randomA) > 0.941f || radius(randomB) > 0.941f
        || (std::fabs(randomA[0u] - randomB[0u]) < 1.0e-5f
            && std::fabs(randomA[1u] - randomB[1u]) < 1.0e-5f)) {
        std::cerr << "random-area strikes were not bounded and distinct\n";
        return false;
    }

    params.strikeMode = s3g::AmbiMembraneStrikeMode::RandomRim;
    kick.setParams(params);
    kick.trigger(1.0f, 36);
    const float rimRadius = radius(kick.actualStrikePosition());
    if (rimRadius < 0.679f || rimRadius > 0.961f) {
        std::cerr << "random-rim strike left its annular region\n";
        return false;
    }
    return true;
}

bool factoryPresetProbe()
{
    const auto quad = s3g::ambiMembraneKickFactoryPreset(1u);
    if (std::strcmp(s3g::ambiMembraneKickFactoryPresetInfo(1u).name,
            "QUAD BASS 43") != 0
        || quad.tuneHz != 43.0f
        || quad.pitchSweepSemitones < 36.0f
        || quad.velocitySensitivity != 1.0f
        || quad.noteTracking != 1.0f) {
        std::cerr << "quad-bass factory preset contract changed\n";
        return false;
    }
    if (s3g::ambiMembraneKickFactoryPresetIndex(quad) != 1) {
        std::cerr << "factory preset recognition failed\n";
        return false;
    }
    auto custom = quad;
    custom.drive += 0.01f;
    if (s3g::ambiMembraneKickFactoryPresetIndex(custom) != -1) {
        std::cerr << "custom membrane voice matched a factory preset\n";
        return false;
    }

    const auto foundation = s3g::ambiMembraneKickFactoryPreset(9u);
    const auto scoop = s3g::ambiMembraneKickFactoryPreset(10u);
    const auto version = s3g::ambiMembraneKickFactoryPreset(11u);
    const auto clash = s3g::ambiMembraneKickFactoryPreset(12u);
    const auto threeStack = s3g::ambiMembraneKickFactoryPreset(13u);
    if (std::strcmp(s3g::ambiMembraneKickFactoryPresetInfo(9u).name,
            "FOUNDATION 40") != 0
        || foundation.tuneHz != 40.0f
        || foundation.noteTracking != 0.25f
        || std::strcmp(s3g::ambiMembraneKickFactoryPresetInfo(10u).name,
            "SCOOP WARMTH 44") != 0
        || scoop.shape != s3g::AmbiMembraneShape::Ellipse
        || scoop.drive < 0.45f
        || std::strcmp(s3g::ambiMembraneKickFactoryPresetInfo(11u).name,
            "VERSION SPACE 37") != 0
        || version.tuneHz != 37.0f
        || version.noteTracking != 0.0f
        || version.decaySeconds < 3.9f
        || std::strcmp(s3g::ambiMembraneKickFactoryPresetInfo(12u).name,
            "CLASH CUT 50") != 0
        || clash.decaySeconds > 0.9f
        || clash.click < 0.15f
        || std::strcmp(s3g::ambiMembraneKickFactoryPresetInfo(13u).name,
            "THREE-STACK 42") != 0
        || threeStack.shape != s3g::AmbiMembraneShape::Triangle
        || threeStack.spatialSpread < 0.70f) {
        std::cerr << "sound-system factory preset contract changed\n";
        return false;
    }

    for (uint32_t index = 0u;
         index < s3g::kAmbiMembraneKickFactoryPresetCount; ++index) {
        const auto& info = s3g::ambiMembraneKickFactoryPresetInfo(index);
        if (!info.name || info.name[0] == '\0'
            || !info.description || info.description[0] == '\0') {
            std::cerr << "factory preset metadata was empty\n";
            return false;
        }
        for (uint32_t other = index + 1u;
             other < s3g::kAmbiMembraneKickFactoryPresetCount; ++other) {
            if (std::strcmp(info.name,
                    s3g::ambiMembraneKickFactoryPresetInfo(other).name) == 0) {
                std::cerr << "factory preset names were not unique\n";
                return false;
            }
        }

        s3g::AmbiMembraneKick kick;
        kick.prepare(kSampleRate);
        const auto preset = s3g::ambiMembraneKickFactoryPreset(index);
        if (s3g::ambiMembraneKickFactoryPresetIndex(preset)
            != static_cast<int>(index)) {
            std::cerr << info.name << " was not recognized after loading\n";
            return false;
        }
        kick.setParams(preset);
        kick.trigger(0.75f, 36);
        std::array<float, s3g::kAmbiMembraneKickChannels> frame {};
        double energy = 0.0;
        float peak = 0.0f;
        for (uint32_t sample = 0u; sample < 8192u; ++sample) {
            kick.processFrame(frame.data(),
                static_cast<uint32_t>(frame.size()));
            for (const float value : frame) {
                if (!std::isfinite(value)) {
                    std::cerr << info.name
                              << " produced a non-finite sample\n";
                    return false;
                }
                peak = std::max(peak, std::fabs(value));
                energy += static_cast<double>(value) * value;
            }
        }
        if (energy < 1.0e-6 || peak > 0.98f) {
            std::cerr << info.name << " had invalid level: "
                      << energy << " / " << peak << "\n";
            return false;
        }
    }
    return true;
}

} // namespace

int main()
{
    if (!silenceAndFiniteProbe()
        || !deepBassAndSpatialProbe()
        || !spatialAutomationContinuityProbe()
        || !orderAndShapeProbe()
        || !directPickupProbe()
        || !stereoDownmixProbe()
        || !noteTrackingProbe()
        || !velocityResponseProbe()
        || !strikePlacementProbe()
        || !factoryPresetProbe()) {
        return 1;
    }
    std::cout << "ambi membrane kick smoke passed\n";
    return 0;
}
