#include "s3g_ambi_pulsar_encoder.h"
#include "s3g_ambi_pulsar_presets.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>

namespace {

constexpr uint32_t kFrames = 512u;
using Buffer = std::array<std::array<float, kFrames>, s3g::kAmbiPulsarMaxChannels>;

float renderPeak(s3g::AmbiPulsarEncoder& encoder, Buffer& buffer, uint32_t blocks = 96u)
{
    std::array<float*, s3g::kAmbiPulsarMaxChannels> outputs {};
    for (uint32_t channel = 0u; channel < outputs.size(); ++channel) outputs[channel] = buffer[channel].data();
    float peak = 0.0f;
    for (uint32_t block = 0u; block < blocks; ++block) {
        encoder.process(outputs.data(), static_cast<uint32_t>(outputs.size()), kFrames);
        for (const auto& channel : buffer) {
            for (float sample : channel) {
                if (!std::isfinite(sample)) return -1.0f;
                peak = std::max(peak, std::fabs(sample));
            }
        }
    }
    return peak;
}

bool testFactoryPresets()
{
    for (uint32_t preset = 0u; preset < s3g::kAmbiPulsarFactoryPresetCount; ++preset) {
        s3g::AmbiPulsarEncoder encoder;
        encoder.prepare(48000.0);
        encoder.setParams(s3g::ambiPulsarFactoryPreset(preset));
        encoder.reset();
        Buffer buffer {};
        const float peak = renderPeak(encoder, buffer, 80u);
        if (!(peak > 1.0e-5f) || peak > 4.0f) {
            std::cerr << "Pulsar preset " << preset << " peak outside expected range: " << peak << "\n";
            return false;
        }
    }
    return true;
}

bool testOrderAndMasking()
{
    s3g::AmbiPulsarEncoder encoder;
    encoder.prepare(48000.0);
    auto params = s3g::ambiPulsarFactoryPreset(0u);
    params.order = 3u;
    encoder.setParams(params);
    encoder.reset();

    Buffer buffer {};
    for (auto& channel : buffer) channel.fill(0.25f);
    const float peak = renderPeak(encoder, buffer, 20u);
    if (!(peak > 1.0e-5f)) {
        std::cerr << "Pulsar encoder produced no output\n";
        return false;
    }
    for (uint32_t channel = 16u; channel < s3g::kAmbiPulsarMaxChannels; ++channel) {
        for (float sample : buffer[channel]) {
            if (sample != 0.0f) {
                std::cerr << "Pulsar encoder did not clear channels above selected order\n";
                return false;
            }
        }
    }

    params.probability = 0.0f;
    encoder.setParams(params);
    encoder.reset();
    Buffer muted {};
    if (renderPeak(encoder, muted, 12u) != 0.0f) {
        std::cerr << "Pulsar probability mask did not mute events\n";
        return false;
    }
    return true;
}

bool testEventRate()
{
    auto countForRate = [](float rate) {
        s3g::AmbiPulsarEncoder encoder;
        encoder.prepare(48000.0);
        auto params = s3g::ambiPulsarFactoryPreset(0u);
        params.emissionHz = rate;
        params.emissionModDepth = 0.0f;
        params.probability = 1.0f;
        params.burstOff = 0u;
        params.sieveModulo = 1u;
        encoder.setParams(params);
        encoder.reset();
        Buffer buffer {};
        renderPeak(encoder, buffer, 94u); // ~1.003 seconds
        return encoder.emittedEventCount();
    };

    const uint64_t slow = countForRate(7.0f);
    const uint64_t fast = countForRate(70.0f);
    if (slow < 6u || slow > 8u || fast < 68u || fast > 72u || fast <= slow * 8u) {
        std::cerr << "Pulsar emission clock inaccurate: slow=" << slow << " fast=" << fast << "\n";
        return false;
    }
    return true;
}

bool testValidAmbisonicEncoding()
{
    s3g::AmbiPulsarEncoder encoder;
    encoder.prepare(48000.0);
    auto params = s3g::ambiPulsarFactoryPreset(0u);
    params.order = 7u;
    params.centerAzimuthDeg = 37.0f;
    params.centerElevationDeg = 21.0f;
    params.centerDistance = 1.0f;
    params.spatialWidth = 0.0f;
    params.spatialScatter = 0.0f;
    params.orbitDepth = 0.0f;
    params.orbitRateHz = 0.0f;
    params.air = 0.0f;
    params.doppler = 0.0f;
    params.probability = 1.0f;
    encoder.setParams(params);
    encoder.reset();

    Buffer buffer {};
    renderPeak(encoder, buffer, 24u);
    const auto basis = s3g::acnSn3dBasis7(s3g::directionFromAed(37.0f, 21.0f));
    bool foundSignal = false;
    for (uint32_t frame = 0u; frame < kFrames; ++frame) {
        const float omni = buffer[0][frame];
        if (std::fabs(omni) < 1.0e-4f) continue;
        foundSignal = true;
        for (uint32_t channel = 1u; channel < s3g::kAmbiPulsarMaxChannels; ++channel) {
            const float expected = omni * basis[channel];
            if (std::fabs(buffer[channel][frame] - expected) > 3.0e-4f) {
                std::cerr << "Pulsar HOA vector lost directional coherence at channel " << channel << "\n";
                return false;
            }
        }
    }
    if (!foundSignal) {
        std::cerr << "Pulsar coherence test found no signal\n";
        return false;
    }
    return true;
}

bool testSanitizationAndQuality()
{
    s3g::AmbiPulsarParams unsafe;
    unsafe.order = 99u;
    unsafe.emissionHz = -1.0f;
    unsafe.sieveModulo = 0u;
    unsafe.sieveResidue = 99u;
    unsafe.lanes[0].formantHz = 1.0e9f;
    unsafe.lanes[0].overlap = 99.0f;
    unsafe.advancedLanes[0].carrierRatio = 1000.0f;
    unsafe.advancedLanes[0].fmRatio = 1000.0f;
    unsafe.advancedLanes[0].fmIndex = 1000.0f;
    unsafe.advancedLanes[0].windowSkew = -1.0f;
    unsafe.advancedLanes[0].tuneMode = static_cast<s3g::AmbiPulsarTuneMode>(99u);
    unsafe.advancedLanes[0].retriggerMode = static_cast<s3g::AmbiPulsarRetriggerMode>(99u);
    unsafe.centerElevationDeg = 180.0f;
    unsafe.points = 99u;
    unsafe.motionMode = static_cast<s3g::AmbiPulsarMotionMode>(99u);
    const auto safe = s3g::sanitizeAmbiPulsarParams(unsafe);
    if (safe.order != 7u || safe.emissionHz != 0.05f || safe.sieveModulo != 1u
        || safe.sieveResidue != 0u || safe.lanes[0].overlap != 8.0f
        || safe.advancedLanes[0].carrierRatio != 128.0f
        || safe.advancedLanes[0].fmRatio != 64.0f || safe.advancedLanes[0].fmIndex != 20.0f
        || safe.advancedLanes[0].windowSkew != 0.02f
        || safe.advancedLanes[0].tuneMode != s3g::AmbiPulsarTuneMode::Subharmonic
        || safe.advancedLanes[0].retriggerMode != s3g::AmbiPulsarRetriggerMode::IdleOnly
        || safe.centerElevationDeg != 89.0f || safe.points != 32u
        || safe.motionMode != s3g::AmbiPulsarMotionMode::Forsy) {
        std::cerr << "Pulsar parameter sanitization failed\n";
        return false;
    }

    s3g::AmbiPulsarEncoder encoder;
    encoder.prepare(96000.0);
    auto params = s3g::ambiPulsarFactoryPreset(4u);
    params.order = 7u;
    params.quality = s3g::AmbiPulsarQuality::Ultra;
    encoder.setParams(params);
    encoder.reset();
    Buffer buffer {};
    const float peak = renderPeak(encoder, buffer, 40u);
    if (!(peak > 1.0e-5f) || peak > 4.0f) {
        std::cerr << "Pulsar ultra quality render failed: " << peak << "\n";
        return false;
    }
    bool highOrderEnergy = false;
    for (uint32_t channel = 49u; channel < 64u; ++channel) {
        for (float sample : buffer[channel]) highOrderEnergy = highOrderEnergy || std::fabs(sample) > 1.0e-6f;
    }
    if (!highOrderEnergy) {
        std::cerr << "Pulsar 7OA render produced no seventh-order energy\n";
        return false;
    }
    return true;
}

bool testAdvancedPulsaretModes()
{
    auto base = s3g::ambiPulsarFactoryPreset(0u);
    base.emissionHz = 80.0f;
    base.emissionModDepth = 0.0f;
    base.formantModDepthSemitones = 0.0f;
    base.formantScatterSemitones = 0.0f;
    base.phaseScatter = 0.0f;
    base.probability = 1.0f;
    base.burstOff = 0u;
    base.sieveModulo = 1u;
    for (auto& lane : base.lanes) {
        lane.triggerOffset = 0.0f;
        lane.overlap = 2.0f;
    }

    s3g::AmbiPulsarEncoder encoder;
    encoder.prepare(48000.0);
    Buffer buffer {};
    base.advancedLanes[0].carrierRatio = 4.0f;
    base.advancedLanes[0].tuneMode = s3g::AmbiPulsarTuneMode::Ratio;
    encoder.setParams(base);
    encoder.reset();
    renderPeak(encoder, buffer, 2u);
    if (std::fabs(encoder.lastTriggeredCarrierHz(0u) - 320.0f) > 0.1f) {
        std::cerr << "Pulsar ratio tuning did not follow the scheduler\n";
        return false;
    }

    base.advancedLanes[0].tuneMode = s3g::AmbiPulsarTuneMode::Subharmonic;
    encoder.setParams(base);
    encoder.reset();
    renderPeak(encoder, buffer, 2u);
    if (std::fabs(encoder.lastTriggeredCarrierHz(0u) - 20.0f) > 0.1f) {
        std::cerr << "Pulsar subharmonic tuning did not divide the scheduler\n";
        return false;
    }

    base.emissionHz = 200.0f;
    base.advancedLanes[0].tuneMode = s3g::AmbiPulsarTuneMode::Hertz;
    base.advancedLanes[0].retriggerMode = s3g::AmbiPulsarRetriggerMode::IdleOnly;
    base.lanes[0].overlap = 8.0f;
    encoder.setParams(base);
    encoder.reset();
    renderPeak(encoder, buffer, 1u);
    if (encoder.activeGrainCount(0u) != 1u) {
        std::cerr << "Pulsar idle-only policy allowed overlapping lane voices\n";
        return false;
    }
    base.advancedLanes[0].retriggerMode = s3g::AmbiPulsarRetriggerMode::Retrigger;
    encoder.setParams(base);
    encoder.reset();
    renderPeak(encoder, buffer, 1u);
    if (encoder.activeGrainCount(0u) < 2u) {
        std::cerr << "Pulsar retrigger policy failed to overlap lane voices\n";
        return false;
    }

    for (auto& lane : base.advancedLanes) {
        lane.retriggerMode = s3g::AmbiPulsarRetriggerMode::Free;
        lane.fmRatio = 7.5f;
        lane.fmIndex = 12.0f;
        lane.windowSkew = 0.08f;
    }
    base.quality = s3g::AmbiPulsarQuality::Ultra;
    encoder.setParams(base);
    encoder.reset();
    const float peak = renderPeak(encoder, buffer, 12u);
    if (!(peak > 1.0e-5f) || peak > 4.0f) {
        std::cerr << "Pulsar free-running phase FM/skew render failed: " << peak << "\n";
        return false;
    }
    return true;
}

bool testEventParameterLatching()
{
    auto params = s3g::ambiPulsarFactoryPreset(0u);
    params.emissionHz = 180.0f;
    params.emissionModDepth = 0.0f;
    params.probability = 1.0f;
    params.burstOff = 0u;
    params.sieveModulo = 1u;
    for (auto& lane : params.lanes) {
        lane.triggerOffset = 0.0f;
        lane.overlap = 8.0f;
    }

    s3g::AmbiPulsarEncoder changed;
    s3g::AmbiPulsarEncoder control;
    changed.prepare(48000.0);
    control.prepare(48000.0);
    changed.setParams(params);
    control.setParams(params);
    changed.reset();
    control.reset();
    Buffer changedBuffer {};
    Buffer controlBuffer {};
    renderPeak(changed, changedBuffer, 1u);
    renderPeak(control, controlBuffer, 1u);

    auto altered = params;
    altered.probability = 0.0f;
    altered.envelope = s3g::AmbiPulsarEnvelope::Reverse;
    altered.envelopeEdge = 0.99f;
    altered.quality = s3g::AmbiPulsarQuality::Eco;
    altered.neuralPulsaretMix = 1.0f;
    altered.neuralEnvelopeMix = 1.0f;
    altered.neuralFmDepthSemitones = 24.0f;
    for (auto& lane : altered.advancedLanes) {
        lane.fmIndex = 20.0f;
        lane.windowSkew = 0.02f;
    }
    auto noNewEvents = params;
    noNewEvents.probability = 0.0f;
    changed.setParams(altered);
    control.setParams(noNewEvents);
    renderPeak(changed, changedBuffer, 1u);
    renderPeak(control, controlBuffer, 1u);
    for (uint32_t channel = 0u; channel < 16u; ++channel) {
        for (uint32_t frame = 0u; frame < kFrames; ++frame) {
            if (std::fabs(changedBuffer[channel][frame] - controlBuffer[channel][frame]) > 1.0e-6f) {
                std::cerr << "Active pulsaret changed after event-latched parameters moved\n";
                return false;
            }
        }
    }
    return true;
}

bool testPointField()
{
    s3g::AmbiPulsarEncoder encoder;
    encoder.prepare(48000.0);
    auto params = s3g::ambiPulsarFactoryPreset(0u);
    params.points = 32u;
    params.emissionHz = 80.0f;
    params.emissionModDepth = 0.0f;
    params.probability = 1.0f;
    params.burstOff = 0u;
    params.sieveModulo = 1u;
    params.pointRandomness = 0.0f;
    params.spatialWidth = 0.82f;
    params.spatialScatter = 0.0f;
    params.orbitDepth = 0.0f;
    params.orbitRateHz = 0.0f;
    params.neuralLevel = 0.0f;
    for (auto& lane : params.lanes) {
        lane.overlap = 8.0f;
        lane.level = 0.55f;
        lane.triggerOffset = 0.0f;
    }
    encoder.setParams(params);
    encoder.reset();

    Buffer buffer {};
    std::array<float*, s3g::kAmbiPulsarMaxChannels> outputs {};
    for (uint32_t channel = 0u; channel < outputs.size(); ++channel) outputs[channel] = buffer[channel].data();
    std::array<std::array<bool, s3g::kAmbiPulsarLanes>, s3g::kAmbiPulsarMaxPoints> observedLanes {};
    float peak = 0.0f;
    for (uint32_t block = 0u; block < 48u; ++block) {
        encoder.process(outputs.data(), static_cast<uint32_t>(outputs.size()), kFrames);
        for (const auto& channel : buffer) {
            for (float sample : channel) {
                if (!std::isfinite(sample)) return false;
                peak = std::max(peak, std::fabs(sample));
            }
        }
        for (uint32_t point = 0u; point < params.points; ++point) {
            for (uint32_t lane = 0u; lane < s3g::kAmbiPulsarLanes; ++lane) {
                observedLanes[point][lane] = observedLanes[point][lane]
                    || encoder.activeGrainCountAtPoint(point, lane) > 0u;
            }
        }
    }
    if (!(peak > 1.0e-5f) || encoder.pointCount() != 32u) {
        std::cerr << "Pulsar thirty-two-point field produced no valid output\n";
        return false;
    }
    for (uint32_t point = 0u; point < 32u; ++point) {
        const auto position = encoder.point(point);
        if (!std::isfinite(position.azimuthDeg) || !std::isfinite(position.elevationDeg)
            || !std::isfinite(position.distance) || encoder.pointEnergy(point) <= 0.0f) {
            std::cerr << "Pulsar point " << point << " has invalid position or energy\n";
            return false;
        }
        for (uint32_t lane = 0u; lane < s3g::kAmbiPulsarLanes; ++lane) {
            if (!observedLanes[point][lane]) {
                std::cerr << "Pulsar point " << point << " is missing formant lane " << lane << "\n";
                return false;
            }
        }
        const auto next = encoder.point((point + 1u) % 32u);
        const float separation = std::fabs(position.azimuthDeg - next.azimuthDeg)
            + std::fabs(position.elevationDeg - next.elevationDeg)
            + std::fabs(position.distance - next.distance);
        if (separation < 0.01f) {
            std::cerr << "Pulsar point cloud collapsed adjacent positions\n";
            return false;
        }
    }

    params.points = 4u;
    encoder.setParams(params);
    if (encoder.pointCount() != 4u) {
        std::cerr << "Pulsar point-count reduction failed\n";
        return false;
    }
    return true;
}

bool testMotionModesAndOrigin()
{
    for (const auto mode : { s3g::AmbiPulsarMotionMode::Orbit, s3g::AmbiPulsarMotionMode::Sway,
             s3g::AmbiPulsarMotionMode::FigureEight }) {
        s3g::AmbiPulsarEncoder encoder;
        encoder.prepare(48000.0);
        auto params = s3g::ambiPulsarFactoryPreset(0u);
        params.points = 6u;
        params.centerAzimuthDeg = 17.0f;
        params.centerElevationDeg = 9.0f;
        params.centerDistance = 1.4f;
        params.spatialWidth = 0.8f;
        params.spatialScatter = 0.0f;
        params.orbitRateHz = 1.0f;
        params.orbitDepth = 1.0f;
        params.motionMode = mode;
        encoder.setParams(params);
        encoder.reset();
        const auto origin = encoder.point(0u);
        if (std::fabs(origin.azimuthDeg - params.centerAzimuthDeg) > 1.0e-3f
            || std::fabs(origin.elevationDeg - params.centerElevationDeg) > 1.0e-3f
            || std::fabs(origin.distance - params.centerDistance) > 1.0e-3f) {
            std::cerr << "Pulsar motion mode did not start point 1 at the field origin\n";
            return false;
        }

        Buffer buffer {};
        renderPeak(encoder, buffer, 24u);
        const float right = encoder.point(0u).azimuthDeg - params.centerAzimuthDeg;
        renderPeak(encoder, buffer, 47u);
        const float left = encoder.point(0u).azimuthDeg - params.centerAzimuthDeg;
        if (!(right > 8.0f && left < -8.0f)) {
            std::cerr << "Pulsar motion remained biased to one side: right=" << right
                      << " left=" << left << "\n";
            return false;
        }
    }

    s3g::AmbiPulsarEncoder freeEncoder;
    freeEncoder.prepare(48000.0);
    auto freeParams = s3g::ambiPulsarFactoryPreset(0u);
    freeParams.points = 32u;
    freeParams.centerAzimuthDeg = 0.0f;
    freeParams.centerElevationDeg = 0.0f;
    freeParams.spatialWidth = 0.82f;
    freeParams.spatialScatter = 0.0f;
    freeParams.orbitRateHz = 1.0f;
    freeParams.orbitDepth = 1.0f;
    freeParams.motionMode = s3g::AmbiPulsarMotionMode::Free;
    freeParams.probability = 0.0f;
    freeParams.neuralLevel = 0.0f;
    freeEncoder.setParams(freeParams);
    freeEncoder.reset();
    float initialMinAzimuth = 180.0f;
    float initialMaxAzimuth = -180.0f;
    float initialMinElevation = 90.0f;
    float initialMaxElevation = -90.0f;
    for (uint32_t index = 1u; index < freeParams.points; ++index) {
        const auto point = freeEncoder.point(index);
        initialMinAzimuth = std::min(initialMinAzimuth, point.azimuthDeg);
        initialMaxAzimuth = std::max(initialMaxAzimuth, point.azimuthDeg);
        initialMinElevation = std::min(initialMinElevation, point.elevationDeg);
        initialMaxElevation = std::max(initialMaxElevation, point.elevationDeg);
    }
    if (initialMinAzimuth > -165.0f || initialMaxAzimuth < 165.0f
        || initialMinElevation > -70.0f || initialMaxElevation < 70.0f) {
        std::cerr << "Pulsar FREE anchors remain confined to a sphere sector: azimuth=["
                  << initialMinAzimuth << ", " << initialMaxAzimuth << "] elevation=["
                  << initialMinElevation << ", " << initialMaxElevation << "]\n";
        return false;
    }
    std::array<std::array<float, kFrames>, 4u> motionBuffer {};
    std::array<float*, 4u> motionOutputs {};
    for (uint32_t channel = 0u; channel < motionOutputs.size(); ++channel) {
        motionOutputs[channel] = motionBuffer[channel].data();
    }
    float maxTravel = 0.0f;
    float minAzimuth = 180.0f;
    float maxAzimuth = -180.0f;
    for (uint32_t block = 0u; block < 360u; ++block) {
        freeEncoder.process(motionOutputs.data(), static_cast<uint32_t>(motionOutputs.size()), kFrames);
        const auto point = freeEncoder.point(0u);
        maxTravel = std::max(maxTravel, std::fabs(point.azimuthDeg) + std::fabs(point.elevationDeg));
        for (uint32_t index = 0u; index < freeParams.points; ++index) {
            const float azimuth = freeEncoder.point(index).azimuthDeg;
            minAzimuth = std::min(minAzimuth, azimuth);
            maxAzimuth = std::max(maxAzimuth, azimuth);
        }
    }
    if (maxTravel < 12.0f || minAzimuth > -55.0f || maxAzimuth < 55.0f) {
        std::cerr << "Pulsar FREE motion did not explore the full field: origin=" << maxTravel
                  << " azimuth=[" << minAzimuth << ", " << maxAzimuth << "]\n";
        return false;
    }

    s3g::AmbiPulsarEncoder freeReference;
    s3g::AmbiPulsarEncoder forsyEncoder;
    freeReference.prepare(48000.0);
    forsyEncoder.prepare(48000.0);
    freeReference.setParams(freeParams);
    auto forsyParams = freeParams;
    forsyParams.motionMode = s3g::AmbiPulsarMotionMode::Forsy;
    forsyEncoder.setParams(forsyParams);
    freeReference.reset();
    forsyEncoder.reset();
    std::array<s3g::AmbiPulsarPoint, s3g::kAmbiPulsarMaxPoints> forsyInitial {};
    float formationDifference = 0.0f;
    for (uint32_t index = 0u; index < forsyParams.points; ++index) {
        forsyInitial[index] = forsyEncoder.point(index);
        const auto freePoint = freeReference.point(index);
        formationDifference += std::fabs(s3g::ambiPulsarWrapSignedDeg(
                forsyInitial[index].azimuthDeg - freePoint.azimuthDeg))
            + std::fabs(forsyInitial[index].elevationDeg - freePoint.elevationDeg);
    }
    float forsyTravel = 0.0f;
    for (uint32_t block = 0u; block < 180u; ++block) {
        forsyEncoder.process(motionOutputs.data(), static_cast<uint32_t>(motionOutputs.size()), kFrames);
        for (uint32_t index = 0u; index < forsyParams.points; ++index) {
            const auto point = forsyEncoder.point(index);
            if (!std::isfinite(point.azimuthDeg) || !std::isfinite(point.elevationDeg)
                || !std::isfinite(point.distance)) {
                std::cerr << "Pulsar FORSY produced an invalid point\n";
                return false;
            }
            forsyTravel = std::max(forsyTravel,
                std::fabs(s3g::ambiPulsarWrapSignedDeg(point.azimuthDeg
                    - forsyInitial[index].azimuthDeg))
                + std::fabs(point.elevationDeg - forsyInitial[index].elevationDeg));
        }
    }
    if (formationDifference < 120.0f || forsyTravel < 20.0f) {
        std::cerr << "Pulsar FORSY did not retain its distinct animated topology: difference="
                  << formationDifference << " travel=" << forsyTravel << "\n";
        return false;
    }

    if (std::fabs(s3g::ambiPulsarPalindromePhase(0.25) - 0.25f) > 1.0e-6f
        || std::fabs(s3g::ambiPulsarPalindromePhase(0.75) - 0.75f) > 1.0e-6f
        || std::fabs(s3g::ambiPulsarPalindromePhase(1.25) - 0.75f) > 1.0e-6f
        || std::fabs(s3g::ambiPulsarPalindromePhase(1.75) - 0.25f) > 1.0e-6f
        || std::fabs(s3g::ambiPulsarPalindromePhase(2.0)) > 1.0e-6f
        || std::fabs(s3g::ambiPulsarPalindromePhase(-0.25) - 0.25f) > 1.0e-6f) {
        std::cerr << "Pulsar FORSY phase did not reflect as a palindrome\n";
        return false;
    }

    s3g::AmbiPulsarEncoder continuousForsy;
    continuousForsy.prepare(48000.0);
    forsyParams.orbitRateHz = 4.0f;
    forsyParams.spatialFollow = 0.0f;
    continuousForsy.setParams(forsyParams);
    continuousForsy.reset();
    constexpr uint32_t kContinuityFrames = 16u;
    std::array<std::array<float, kContinuityFrames>, 4u> continuityBuffer {};
    std::array<float*, 4u> continuityOutputs {};
    for (uint32_t channel = 0u; channel < continuityOutputs.size(); ++channel) {
        continuityOutputs[channel] = continuityBuffer[channel].data();
    }
    std::array<s3g::AmbiPulsarPoint, s3g::kAmbiPulsarMaxPoints> previousPoints {};
    for (uint32_t index = 0u; index < forsyParams.points; ++index) {
        previousPoints[index] = continuousForsy.point(index);
    }
    float maximumForsyStep = 0.0f;
    for (uint32_t block = 0u; block < 1800u; ++block) {
        continuousForsy.process(continuityOutputs.data(),
            static_cast<uint32_t>(continuityOutputs.size()), kContinuityFrames);
        for (uint32_t index = 0u; index < forsyParams.points; ++index) {
            const auto point = continuousForsy.point(index);
            const float angularStep = std::fabs(s3g::ambiPulsarWrapSignedDeg(
                    point.azimuthDeg - previousPoints[index].azimuthDeg))
                + std::fabs(point.elevationDeg - previousPoints[index].elevationDeg);
            maximumForsyStep = std::max(maximumForsyStep, angularStep);
            previousPoints[index] = point;
        }
    }
    if (maximumForsyStep > 2.0f) {
        std::cerr << "Pulsar FORSY point teleported: angular step="
                  << maximumForsyStep << " degrees per spatial update\n";
        return false;
    }
    return true;
}

bool testContinuityRepairs()
{
    using SmallBuffer = std::array<std::array<float, kFrames>, 4u>;
    auto processBlock = [](s3g::AmbiPulsarEncoder& encoder, SmallBuffer& buffer) {
        std::array<float*, 4u> outputs {};
        for (uint32_t channel = 0u; channel < outputs.size(); ++channel) outputs[channel] = buffer[channel].data();
        encoder.process(outputs.data(), static_cast<uint32_t>(outputs.size()), kFrames);
    };
    auto neuralParams = [] {
        auto params = s3g::ambiPulsarFactoryPreset(8u);
        params.order = 1u;
        params.points = 8u;
        params.probability = 0.0f;
        for (auto& lane : params.lanes) lane.level = 0.0f;
        params.neuralLevel = 1.0f;
        params.centerDistance = 1.0f;
        params.spatialWidth = 0.0f;
        params.spatialScatter = 0.0f;
        params.orbitDepth = 0.0f;
        params.orbitRateHz = 0.0f;
        params.air = 0.0f;
        params.doppler = 0.0f;
        params.outputGainDb = 0.0f;
        return params;
    };
    auto transitionIsSmooth = [&](const char* name, s3g::AmbiPulsarParams before, auto mutate) {
        s3g::AmbiPulsarEncoder encoder;
        encoder.prepare(48000.0);
        encoder.setParams(before);
        encoder.reset();
        SmallBuffer buffer {};
        for (uint32_t block = 0u; block < 188u; ++block) processBlock(encoder, buffer);
        double squaredDelta = 0.0;
        for (uint32_t frame = 1u; frame < kFrames; ++frame) {
            const double delta = buffer[0][frame] - buffer[0][frame - 1u];
            squaredDelta += delta * delta;
        }
        const float baselineRms = static_cast<float>(std::sqrt(squaredDelta / (kFrames - 1u)));
        const float previous = buffer[0][kFrames - 1u];
        mutate(before);
        encoder.setParams(before);
        processBlock(encoder, buffer);
        const float jump = std::fabs(buffer[0][0] - previous);
        const float limit = std::max(0.01f, baselineRms * 8.0f);
        if (!std::isfinite(jump) || jump > limit) {
            std::cerr << "Pulsar " << name << " transition clicked: jump=" << jump
                      << " baseline RMS delta=" << baselineRms << "\n";
            return false;
        }
        return true;
    };

    if (!transitionIsSmooth("neural level", neuralParams(), [](auto& p) { p.neuralLevel = 0.0f; })) return false;
    auto far = neuralParams();
    far.centerDistance = 8.0f;
    if (!transitionIsSmooth("distance", far, [](auto& p) { p.centerDistance = 0.1f; })) return false;
    if (!transitionIsSmooth("Doppler", neuralParams(), [](auto& p) { p.doppler = 1.0f; })) return false;
    far.air = 0.0f;
    if (!transitionIsSmooth("air", far, [](auto& p) { p.air = 1.0f; })) return false;
    if (!transitionIsSmooth("point count", neuralParams(), [](auto& p) { p.points = 4u; })) return false;

    s3g::AmbiPulsarEncoder startup;
    startup.prepare(48000.0);
    startup.setParams(neuralParams());
    startup.reset();
    SmallBuffer startupBuffer {};
    processBlock(startup, startupBuffer);
    if (std::fabs(startupBuffer[0][0]) > 0.002f) {
        std::cerr << "Pulsar startup safety ramp exposed a discontinuity\n";
        return false;
    }
    return true;
}

bool testNeuralCircuit()
{
    s3g::NeuralSynthesisParams params;
    params.drive = 2.8f;
    params.feedback = 0.96f;
    params.coupling = 0.68f;
    params.hierarchy = 0.72f;
    params.phaseShift = 0.58f;
    params.brownian = 0.16f;
    params.drift = 0.24f;
    params.selfModulation = 0.66f;
    params.audioFeedback = 0.28f;
    params.seed = 0x1234abcdu;

    s3g::NeuralSynthesisNetwork network;
    s3g::NeuralSynthesisNetwork duplicate;
    network.prepare(48000.0);
    duplicate.prepare(48000.0);
    network.setParams(params);
    duplicate.setParams(params);
    network.reset();
    duplicate.reset();

    std::array<double, s3g::kNeuralSynthesisClusters> energy {};
    std::array<uint32_t, s3g::kNeuralSynthesisClusters> crossings {};
    std::array<float, s3g::kNeuralSynthesisClusters> previous {};
    constexpr uint32_t totalFrames = 48000u * 6u;
    constexpr uint32_t warmupFrames = 48000u;
    for (uint32_t frame = 0u; frame < totalFrames; ++frame) {
        const float external = std::sin(static_cast<float>(frame) * 0.00131f) * 0.08f;
        const auto a = network.process(external);
        const auto b = duplicate.process(external);
        for (uint32_t node = 0u; node < s3g::kNeuralSynthesisNodes; ++node) {
            if (!std::isfinite(a.nodes[node]) || std::fabs(a.nodes[node]) > 1.01f
                || std::fabs(a.nodes[node] - b.nodes[node]) > 1.0e-7f) {
                std::cerr << "Neural circuit lost bounded deterministic state at node " << node << "\n";
                return false;
            }
        }
        if (frame < warmupFrames) continue;
        for (uint32_t cluster = 0u; cluster < s3g::kNeuralSynthesisClusters; ++cluster) {
            const float value = a.nodes[cluster * s3g::kNeuralNodesPerCluster];
            energy[cluster] += static_cast<double>(value) * value;
            if ((value >= 0.0f) != (previous[cluster] >= 0.0f)) ++crossings[cluster];
            previous[cluster] = value;
        }
    }

    for (uint32_t cluster = 0u; cluster < s3g::kNeuralSynthesisClusters; ++cluster) {
        const double rms = std::sqrt(energy[cluster] / static_cast<double>(totalFrames - warmupFrames));
        if (!(rms > 1.0e-4) || crossings[cluster] == 0u) {
            std::cerr << "Neural cluster " << cluster << " did not sustain activity: rms=" << rms
                      << " crossings=" << crossings[cluster] << "\n";
            return false;
        }
    }
    if (!(crossings[3] > crossings[0] && crossings[2] >= crossings[0])) {
        std::cerr << "Neural RC bands did not separate from slow to fast: "
                  << crossings[0] << ", " << crossings[1] << ", " << crossings[2] << ", " << crossings[3] << "\n";
        return false;
    }

    s3g::NeuralSynthesisNetwork uncoupled;
    uncoupled.prepare(48000.0);
    auto uncoupledParams = params;
    uncoupledParams.coupling = 0.0f;
    uncoupled.setParams(uncoupledParams);
    uncoupled.reset();
    network.reset();
    double difference = 0.0;
    for (uint32_t frame = 0u; frame < 96000u; ++frame) {
        const auto coupledFrame = network.process();
        const auto uncoupledFrame = uncoupled.process();
        difference += std::fabs(coupledFrame.nodes[14] - uncoupledFrame.nodes[14]);
    }
    if (difference < 10.0) {
        std::cerr << "Neural signed matrix had no material cross-cluster influence\n";
        return false;
    }
    return true;
}

bool testNeuralEncoderIntegration()
{
    s3g::AmbiPulsarEncoder encoder;
    encoder.prepare(48000.0);
    auto params = s3g::ambiPulsarFactoryPreset(8u);
    params.order = 3u;
    params.probability = 0.0f;
    for (auto& lane : params.lanes) lane.level = 0.0f;
    params.neuralLevel = 0.9f;
    params.neural.audioFeedback = 0.42f;
    encoder.setParams(params);
    encoder.reset();

    Buffer buffer {};
    const float peak = renderPeak(encoder, buffer, 80u); // enough to arm and capture the 0.5 s table window
    if (!(peak > 1.0e-5f) || peak > 4.0f || encoder.neuralCaptureGeneration() == 0u) {
        std::cerr << "Neural direct/capture integration failed: peak=" << peak
                  << " captures=" << encoder.neuralCaptureGeneration() << "\n";
        return false;
    }

    params.probability = 1.0f;
    params.neuralLevel = 0.0f;
    params.neuralPulsaretMix = 1.0f;
    params.neuralEnvelopeMix = 1.0f;
    params.neuralFmDepthSemitones = 8.0f;
    for (auto& lane : params.lanes) lane.level = 0.55f;
    ++params.neuralCapture;
    encoder.setParams(params);
    const uint32_t before = encoder.neuralCaptureGeneration();
    const float capturedPeak = renderPeak(encoder, buffer, 24u);
    if (!(capturedPeak > 1.0e-5f) || capturedPeak > 4.0f || encoder.neuralCaptureGeneration() <= before) {
        std::cerr << "Neural pulsaret/envelope/FM capture paths failed: peak=" << capturedPeak << "\n";
        return false;
    }
    encoder.reset();
    if (encoder.neuralCaptureGeneration() != 0u) {
        std::cerr << "Neural capture status survived a table reset\n";
        return false;
    }
    return true;
}

} // namespace

int main()
{
    if (!testFactoryPresets()) return 1;
    if (!testOrderAndMasking()) return 1;
    if (!testEventRate()) return 1;
    if (!testValidAmbisonicEncoding()) return 1;
    if (!testSanitizationAndQuality()) return 1;
    if (!testAdvancedPulsaretModes()) return 1;
    if (!testEventParameterLatching()) return 1;
    if (!testPointField()) return 1;
    if (!testMotionModesAndOrigin()) return 1;
    if (!testContinuityRepairs()) return 1;
    if (!testNeuralCircuit()) return 1;
    if (!testNeuralEncoderIntegration()) return 1;
    std::cout << "s3g Pulsar Encoder smoke test passed\n";
    return 0;
}
