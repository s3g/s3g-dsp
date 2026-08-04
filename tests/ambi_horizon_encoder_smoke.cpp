#include "s3g_ambi_horizon_encoder.h"
#include "s3g_ambi_horizon_presets.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {

constexpr uint32_t kFrames = 4096u;

using Buffer = std::array<std::vector<float>, s3g::kAmbiHorizonMaxChannels>;

Buffer render(s3g::AmbiHorizonEncoder& encoder, uint32_t channels,
              uint32_t frames = kFrames)
{
    Buffer buffer {};
    std::array<float*, s3g::kAmbiHorizonMaxChannels> pointers {};
    for (uint32_t channel = 0u; channel < channels; ++channel) {
        buffer[channel].resize(frames);
        pointers[channel] = buffer[channel].data();
    }
    encoder.processBlock(pointers.data(), channels, frames);
    return buffer;
}

float absoluteEnergy(const Buffer& buffer, uint32_t channels)
{
    float energy = 0.0f;
    for (uint32_t channel = 0u; channel < channels; ++channel) {
        for (const float sample : buffer[channel]) {
            assert(std::isfinite(sample));
            energy += std::abs(sample);
        }
    }
    return energy;
}

float absoluteDifference(const Buffer& a, const Buffer& b, uint32_t channels)
{
    float difference = 0.0f;
    for (uint32_t channel = 0u; channel < channels; ++channel) {
        assert(a[channel].size() == b[channel].size());
        for (size_t frame = 0u; frame < a[channel].size(); ++frame) {
            assert(std::isfinite(a[channel][frame]));
            assert(std::isfinite(b[channel][frame]));
            difference += std::abs(a[channel][frame] - b[channel][frame]);
        }
    }
    return difference;
}

float channelRms(const std::vector<float>& channel)
{
    double energy = 0.0;
    for (const float sample : channel) energy += static_cast<double>(sample) * sample;
    return channel.empty() ? 0.0f
        : static_cast<float>(std::sqrt(energy / static_cast<double>(channel.size())));
}

float channelPeak(const std::vector<float>& channel)
{
    float peak = 0.0f;
    for (const float sample : channel) peak = std::max(peak, std::abs(sample));
    return peak;
}

struct AudibilityMetrics {
    float rms = 0.0f;
    float peak = 0.0f;
    float quietestBlockRms = 0.0f;
};

AudibilityMetrics presetAudibility(
    const s3g::AmbiHorizonEncoderParams& sourceScene,
    uint32_t blocks = 48u)
{
    auto scene = sourceScene;
    scene.order = 1u;
    scene.outputGainDb = 0.0f;
    s3g::AmbiHorizonEncoder source;
    source.prepare(48000.0);
    source.setParams(scene);
    source.reset();

    AudibilityMetrics result {};
    result.quietestBlockRms = 1.0f;
    double energy = 0.0;
    uint64_t samples = 0u;
    for (uint32_t block = 0u; block < blocks; ++block) {
        const auto window = render(source, source.activeChannels());
        const float blockRms = channelRms(window[0]);
        result.quietestBlockRms = std::min(
            result.quietestBlockRms, blockRms);
        result.peak = std::max(result.peak, channelPeak(window[0]));
        for (float sample : window[0]) {
            energy += static_cast<double>(sample) * sample;
            ++samples;
        }
    }
    result.rms = samples == 0u ? 0.0f
        : static_cast<float>(std::sqrt(energy
            / static_cast<double>(samples)));
    return result;
}

struct TextureMetrics {
    float rms = 0.0f;
    float differenceRms = 0.0f;
    float peak = 0.0f;
    float maximumDifference = 0.0f;
};

struct TransientMetrics {
    float rms = 0.0f;
    float peak = 0.0f;
    float maximumStep = 0.0f;
    float maximumCurvature = 0.0f;
};

TextureMetrics textureMetrics(const std::vector<float>& channel)
{
    TextureMetrics result {};
    if (channel.empty()) return result;
    double energy = 0.0;
    double differenceEnergy = 0.0;
    for (size_t frame = 0u; frame < channel.size(); ++frame) {
        const float sample = channel[frame];
        energy += static_cast<double>(sample) * sample;
        result.peak = std::max(result.peak, std::abs(sample));
        if (frame > 0u) {
            const float difference = sample - channel[frame - 1u];
            differenceEnergy += static_cast<double>(difference) * difference;
            result.maximumDifference = std::max(
                result.maximumDifference, std::abs(difference));
        }
    }
    result.rms = static_cast<float>(std::sqrt(
        energy / static_cast<double>(channel.size())));
    result.differenceRms = channel.size() > 1u
        ? static_cast<float>(std::sqrt(differenceEnergy
            / static_cast<double>(channel.size() - 1u)))
        : 0.0f;
    return result;
}

TransientMetrics generatorTransientMetrics(
    const s3g::AmbiHorizonEncoderParams& sourceScene,
    float s3g::AmbiHorizonEncoderParams::*level,
    uint32_t blocks = 28u)
{
    auto scene = sourceScene;
    scene.entities = 8u;
    scene.activity = 1.0f;
    scene.occupancy = 1.0f;
    scene.signals = 1.0f;
    scene.horizonBed = 0.0f;
    scene.localFloor = 0.0f;
    scene.airNoise = 0.0f;
    scene.machines = 0.0f;
    scene.bells = 0.0f;
    scene.traffic = 0.0f;
    scene.aircraft = 0.0f;
    scene.foghorns = 0.0f;
    scene.surf = 0.0f;
    scene.*level = 1.0f;
    scene.outputGainDb = 0.0f;
    auto disabled = scene;
    disabled.*level = 0.0f;

    s3g::AmbiHorizonEncoder source;
    s3g::AmbiHorizonEncoder reference;
    source.prepare(48000.0);
    reference.prepare(48000.0);
    source.setParams(scene);
    reference.setParams(disabled);
    source.reset();
    reference.reset();

    TransientMetrics result {};
    double energy = 0.0;
    uint64_t samples = 0u;
    float previous = 0.0f;
    float previousStep = 0.0f;
    bool hasPrevious = false;
    for (uint32_t block = 0u; block < blocks; ++block) {
        const auto window = render(source, source.activeChannels());
        const auto referenceWindow = render(
            reference, reference.activeChannels());
        for (size_t frame = 0u; frame < window[0].size(); ++frame) {
            const float sample = window[0][frame]
                - referenceWindow[0][frame];
            assert(std::isfinite(sample));
            result.peak = std::max(result.peak, std::abs(sample));
            energy += static_cast<double>(sample) * sample;
            ++samples;
            if (hasPrevious) {
                const float step = sample - previous;
                result.maximumStep = std::max(
                    result.maximumStep, std::abs(step));
                result.maximumCurvature = std::max(
                    result.maximumCurvature,
                    std::abs(step - previousStep));
                previousStep = step;
            } else {
                hasPrevious = true;
            }
            previous = sample;
        }
    }
    result.rms = samples == 0u ? 0.0f
        : static_cast<float>(std::sqrt(energy / static_cast<double>(samples)));
    return result;
}

void assertSmoothedTransition(const s3g::AmbiHorizonEncoderParams& before,
                              const s3g::AmbiHorizonEncoderParams& after)
{
    s3g::AmbiHorizonEncoder changed;
    s3g::AmbiHorizonEncoder reference;
    changed.prepare(48000.0);
    reference.prepare(48000.0);
    changed.setParams(before);
    reference.setParams(before);
    changed.reset();
    reference.reset();
    (void)render(changed, 16u);
    (void)render(reference, 16u);

    changed.setParams(after);
    const auto transitioned = render(changed, 16u, 256u);
    const auto unchanged = render(reference, 16u, 256u);
    double firstDifference = 0.0;
    double maximumDifference = 0.0;
    for (uint32_t channel = 0u; channel < 16u; ++channel) {
        firstDifference = std::max(firstDifference,
            static_cast<double>(std::abs(
                transitioned[channel][0] - unchanged[channel][0])));
        for (uint32_t frame = 0u; frame < 256u; ++frame) {
            maximumDifference = std::max(maximumDifference,
                static_cast<double>(std::abs(
                    transitioned[channel][frame] - unchanged[channel][frame])));
        }
    }
    assert(maximumDifference > 1.0e-7);
    assert(firstDifference < maximumDifference * 0.10 + 1.0e-7);
}

void assertFamilyContribution(const s3g::AmbiHorizonEncoderParams& scene,
                              float s3g::AmbiHorizonEncoderParams::*level,
                              uint32_t blocks)
{
    auto enabled = scene;
    auto disabled = scene;
    enabled.*level = 1.0f;
    disabled.*level = 0.0f;
    s3g::AmbiHorizonEncoder withFamily;
    s3g::AmbiHorizonEncoder withoutFamily;
    withFamily.prepare(48000.0);
    withoutFamily.prepare(48000.0);
    withFamily.setParams(enabled);
    withoutFamily.setParams(disabled);
    withFamily.reset();
    withoutFamily.reset();
    float difference = 0.0f;
    for (uint32_t block = 0u; block < blocks; ++block) {
        const auto a = render(withFamily, withFamily.activeChannels());
        const auto b = render(withoutFamily, withoutFamily.activeChannels());
        difference += absoluteDifference(a, b, withFamily.activeChannels());
    }
    assert(difference > 0.001f);
}

} // namespace

int main()
{
    s3g::AmbiHorizonEncoder encoder;
    encoder.prepare(48000.0);

    auto params = s3g::ambiHorizonFactoryPreset(2u);
    params.order = 7u;
    params.outputGainDb = -12.0f;
    encoder.setParams(params);
    assert(encoder.activeChannels() == 64u);
    assert(encoder.activeEntities() == params.entities);

    const auto first = render(encoder, 64u);
    const float energy = absoluteEnergy(first, 64u);
    assert(energy > 0.001f);

    bool hasLocal = false;
    bool hasBed = false;
    bool hasSignal = false;
    for (uint32_t index = 0u; index < encoder.activeEntities(); ++index) {
        const auto point = encoder.entityTelemetry(index);
        assert(std::isfinite(point.azimuthDeg));
        assert(std::isfinite(point.elevationDeg));
        assert(point.rangeNorm >= 0.0f && point.rangeNorm <= 1.0f);
        hasLocal |= point.layer == s3g::AmbiHorizonLayer::LocalFloor;
        hasBed |= point.layer == s3g::AmbiHorizonLayer::HorizonBed;
        hasSignal |= point.layer == s3g::AmbiHorizonLayer::HorizonSignal;
    }
    assert(hasLocal && hasBed && hasSignal);

    // Reset must reproduce the same autonomous scene exactly.
    encoder.reset();
    const auto resetA = render(encoder, 64u);
    encoder.reset();
    const auto resetB = render(encoder, 64u);
    for (uint32_t channel = 0u; channel < 64u; ++channel) {
        assert(resetA[channel] == resetB[channel]);
    }

    // Parameter sanitation and factory-preset coverage.
    params.order = 99u;
    params.entities = 1u;
    params.rangeKm = 1000.0f;
    params.air = -2.0f;
    params.outputGainDb = 100.0f;
    encoder.setParams(params);
    const auto clean = encoder.params();
    assert(clean.order == 7u);
    assert(clean.entities == 4u);
    assert(clean.rangeKm == 20.0f);
    assert(clean.air == 0.0f);
    assert(clean.outputGainDb == 12.0f);

    auto transitionBase = s3g::ambiHorizonFactoryPreset(2u);
    transitionBase.order = 3u;
    transitionBase.outputGainDb = -18.0f;
    auto outputTransition = transitionBase;
    outputTransition.outputGainDb = 12.0f;
    assertSmoothedTransition(transitionBase, outputTransition);
    auto entityTransition = transitionBase;
    entityTransition.entities = 4u;
    assertSmoothedTransition(transitionBase, entityTransition);

    // Every synthetic family must have a real zero and use the same dezippered
    // automation path as the existing layer/output controls.
    assertFamilyContribution(s3g::ambiHorizonFactoryPreset(4u),
        &s3g::AmbiHorizonEncoderParams::machines, 8u);
    assertFamilyContribution(s3g::ambiHorizonFactoryPreset(0u),
        &s3g::AmbiHorizonEncoderParams::bells, 12u);
    assertFamilyContribution(s3g::ambiHorizonFactoryPreset(2u),
        &s3g::AmbiHorizonEncoderParams::traffic, 8u);
    assertFamilyContribution(s3g::ambiHorizonFactoryPreset(13u),
        &s3g::AmbiHorizonEncoderParams::aircraft, 8u);
    assertFamilyContribution(s3g::ambiHorizonFactoryPreset(14u),
        &s3g::AmbiHorizonEncoderParams::foghorns, 18u);
    assertFamilyContribution(s3g::ambiHorizonFactoryPreset(15u),
        &s3g::AmbiHorizonEncoderParams::surf, 8u);

    const std::array<std::pair<const char*, TransientMetrics>, 6u>
        generatorTransients {{
            { "machines", generatorTransientMetrics(
                s3g::ambiHorizonFactoryPreset(4u),
                &s3g::AmbiHorizonEncoderParams::machines) },
            { "bells", generatorTransientMetrics(
                s3g::ambiHorizonFactoryPreset(0u),
                &s3g::AmbiHorizonEncoderParams::bells) },
            { "traffic", generatorTransientMetrics(
                s3g::ambiHorizonFactoryPreset(2u),
                &s3g::AmbiHorizonEncoderParams::traffic) },
            { "aircraft", generatorTransientMetrics(
                s3g::ambiHorizonFactoryPreset(13u),
                &s3g::AmbiHorizonEncoderParams::aircraft) },
            { "foghorns", generatorTransientMetrics(
                s3g::ambiHorizonFactoryPreset(14u),
                &s3g::AmbiHorizonEncoderParams::foghorns) },
            { "surf", generatorTransientMetrics(
                s3g::ambiHorizonFactoryPreset(15u),
                &s3g::AmbiHorizonEncoderParams::surf) },
        }};
    constexpr std::array<float, 6u> kMaximumStepRatios {
        0.03f, 0.18f, 0.08f, 0.12f, 0.08f, 0.14f
    };
    constexpr std::array<float, 6u> kMaximumCurvatureRatios {
        0.01f, 0.04f, 0.06f, 0.09f, 0.02f, 0.12f
    };
    for (size_t index = 0u; index < generatorTransients.size(); ++index) {
        const auto& [name, metrics] = generatorTransients[index];
        const float normalizer = std::max(metrics.peak, 1.0e-12f);
        const float stepRatio = metrics.maximumStep / normalizer;
        const float curvatureRatio = metrics.maximumCurvature / normalizer;
        assert(metrics.peak > 1.0e-5f);
        assert(stepRatio < kMaximumStepRatios[index]);
        assert(curvatureRatio < kMaximumCurvatureRatios[index]);
        std::cout << "Generator " << name
                  << " step/peak=" << stepRatio
                  << " curvature/peak=" << curvatureRatio
                  << " peak=" << metrics.peak << '\n';
    }
    // Machines contain continuous motor/harmonic bodies only. Their absolute
    // ceiling prevents the family from becoming a foreground event; normal
    // oscillator slope is covered by the family limits above.
    assert(generatorTransients[0].second.peak < 0.020f);
    assert(generatorTransients[0].second.maximumStep
        < generatorTransients[0].second.peak * 0.12f);

    const auto assertFamilySmoothing = [](uint32_t preset, auto level) {
        auto before = s3g::ambiHorizonFactoryPreset(preset);
        before.*level = 1.0f;
        auto after = before;
        after.*level = 0.0f;
        assertSmoothedTransition(before, after);
    };
    assertFamilySmoothing(4u, &s3g::AmbiHorizonEncoderParams::machines);
    assertFamilySmoothing(0u, &s3g::AmbiHorizonEncoderParams::bells);
    assertFamilySmoothing(2u, &s3g::AmbiHorizonEncoderParams::traffic);
    assertFamilySmoothing(13u, &s3g::AmbiHorizonEncoderParams::aircraft);
    assertFamilySmoothing(14u, &s3g::AmbiHorizonEncoderParams::foghorns);
    assertFamilySmoothing(15u, &s3g::AmbiHorizonEncoderParams::surf);

    const auto assertModelSmoothing = [](uint32_t preset, auto control) {
        auto before = s3g::ambiHorizonFactoryPreset(preset);
        before.*control = 0.0f;
        auto after = before;
        after.*control = 1.0f;
        assertSmoothedTransition(before, after);
    };
    assertModelSmoothing(2u, &s3g::AmbiHorizonEncoderParams::trafficSpeed);
    assertModelSmoothing(2u, &s3g::AmbiHorizonEncoderParams::engineLoad);
    assertModelSmoothing(13u, &s3g::AmbiHorizonEncoderParams::aircraftFlight);
    assertModelSmoothing(13u, &s3g::AmbiHorizonEncoderParams::aircraftSpeed);
    assertModelSmoothing(13u, &s3g::AmbiHorizonEncoderParams::aircraftPower);
    assertModelSmoothing(13u, &s3g::AmbiHorizonEncoderParams::aircraftTone);
    assertModelSmoothing(14u, &s3g::AmbiHorizonEncoderParams::foghornPitch);
    assertModelSmoothing(14u, &s3g::AmbiHorizonEncoderParams::foghornPressure);
    assertModelSmoothing(14u, &s3g::AmbiHorizonEncoderParams::foghornLength);
    assertModelSmoothing(15u, &s3g::AmbiHorizonEncoderParams::waveRate);
    assertModelSmoothing(15u, &s3g::AmbiHorizonEncoderParams::waveBreak);
    assertModelSmoothing(4u, &s3g::AmbiHorizonEncoderParams::machineTone);
    assertModelSmoothing(0u, &s3g::AmbiHorizonEncoderParams::bellPitch);
    assertModelSmoothing(0u, &s3g::AmbiHorizonEncoderParams::bellDecay);

    auto listenerBefore = s3g::ambiHorizonFactoryPreset(4u);
    listenerBefore.outputGainDb = 0.0f;
    listenerBefore.fieldListenMode = s3g::AmbiFieldListenMode::Off;
    listenerBefore.fieldListenAmount = 1.0f;
    auto listenerAfter = listenerBefore;
    listenerAfter.fieldListenMode = s3g::AmbiFieldListenMode::Follow;
    listenerAfter.fieldListenResponse =
        s3g::AmbiFieldListenerResponse::Imprint;
    assertSmoothedTransition(listenerBefore, listenerAfter);

    // OFF is a true open-loop reference regardless of Amount. With listening
    // active, two research-cue mappings must evolve away from one another
    // using the generated pre-output HOA field and its landscape return.
    s3g::AmbiHorizonEncoder listenerOffA;
    s3g::AmbiHorizonEncoder listenerOffB;
    listenerOffA.prepare(48000.0);
    listenerOffB.prepare(48000.0);
    auto offAParams = listenerBefore;
    auto offBParams = listenerBefore;
    offAParams.fieldListenAmount = 0.0f;
    offBParams.fieldListenAmount = 1.0f;
    listenerOffA.setParams(offAParams);
    listenerOffB.setParams(offBParams);
    listenerOffA.reset();
    listenerOffB.reset();
    float offDifference = 0.0f;
    for (uint32_t block = 0u; block < 12u; ++block) {
        const auto a = render(listenerOffA, listenerOffA.activeChannels());
        const auto b = render(listenerOffB, listenerOffB.activeChannels());
        offDifference += absoluteDifference(
            a, b, listenerOffA.activeChannels());
    }
    assert(offDifference < 1.0e-6f);

    s3g::AmbiHorizonEncoder listenerReach;
    s3g::AmbiHorizonEncoder listenerDistance;
    listenerReach.prepare(48000.0);
    listenerDistance.prepare(48000.0);
    auto reachParams = listenerAfter;
    reachParams.fieldListenResponse =
        s3g::AmbiFieldListenerResponse::Legacy;
    auto distanceParams = listenerAfter;
    distanceParams.fieldListenResponse =
        s3g::AmbiFieldListenerResponse::Imprint;
    listenerReach.setParams(reachParams);
    listenerDistance.setParams(distanceParams);
    listenerReach.reset();
    listenerDistance.reset();
    float responseDifference = 0.0f;
    for (uint32_t block = 0u; block < 64u; ++block) {
        const auto reach = render(
            listenerReach, listenerReach.activeChannels());
        const auto distance = render(
            listenerDistance, listenerDistance.activeChannels());
        if (block >= 16u) {
            responseDifference += absoluteDifference(
                reach, distance, listenerReach.activeChannels());
        }
    }
    assert(listenerReach.fieldListenActivity() > 0.05f);
    assert(listenerReach.fieldListenReturnShare() > 0.001f);
    assert(listenerReach.fieldListenReturnShare() < 1.0f);
    assert(responseDifference > 0.01f);
    std::cout << "Horizon listener activity="
              << listenerReach.fieldListenActivity()
              << " return-share=" << listenerReach.fieldListenReturnShare()
              << " response-difference=" << responseDifference << '\n';

    // FLIGHT is a continuous landed-to-overhead scene variable, rather than
    // merely an aircraft timbre switch. It must produce a grounded, nearly
    // level path at zero and a pronounced overhead arc at one.
    const auto aircraftMaximumElevation = [](float flight) {
        auto aircraftScene = s3g::ambiHorizonFactoryPreset(13u);
        aircraftScene.aircraftFlight = flight;
        s3g::AmbiHorizonEncoder aircraftEncoder;
        aircraftEncoder.prepare(48000.0);
        aircraftEncoder.setParams(aircraftScene);
        aircraftEncoder.reset();
        (void)render(aircraftEncoder, aircraftEncoder.activeChannels(), 64u);
        float maximumElevation = -90.0f;
        for (uint32_t index = 4u;
             index < aircraftEncoder.activeEntities(); ++index) {
            const uint32_t familyIndex = index % 8u;
            if (familyIndex == 4u || familyIndex == 5u
                || familyIndex == 7u) {
                maximumElevation = std::max(maximumElevation,
                    aircraftEncoder.entityTelemetry(index).elevationDeg);
            }
        }
        return maximumElevation;
    };
    const float landedElevation = aircraftMaximumElevation(0.0f);
    const float overheadElevation = aircraftMaximumElevation(1.0f);
    assert(landedElevation < 3.0f);
    assert(overheadElevation > 25.0f);
    assert(overheadElevation > landedElevation + 20.0f);

    // Isolate the foghorn voices and verify that the pressurised horn has a
    // definite tonal call without depending on the broadband air or surf.
    auto foghornScene = s3g::ambiHorizonFactoryPreset(14u);
    foghornScene.airNoise = 0.0f;
    foghornScene.localFloor = 0.0f;
    foghornScene.horizonBed = 0.0f;
    foghornScene.signals = 1.0f;
    foghornScene.surf = 0.0f;
    foghornScene.foghorns = 1.0f;
    foghornScene.outputGainDb = 0.0f;
    encoder.setParams(foghornScene);
    encoder.reset();
    float foghornPeak = 0.0f;
    double foghornEnergy = 0.0;
    uint64_t foghornSamples = 0u;
    for (uint32_t block = 0u; block < 16u; ++block) {
        const auto foghornWindow = render(encoder, encoder.activeChannels());
        foghornPeak = std::max(foghornPeak, channelPeak(foghornWindow[0]));
        for (const float sample : foghornWindow[0]) {
            foghornEnergy += static_cast<double>(sample) * sample;
            ++foghornSamples;
        }
    }
    const float foghornRms = static_cast<float>(std::sqrt(
        foghornEnergy / static_cast<double>(foghornSamples)));
    assert(foghornPeak > 0.003f);
    assert(foghornRms > 0.0002f);
    assert(foghornPeak > foghornRms * 1.15f);
    std::cout << "Aircraft landed/overhead max elevation="
              << landedElevation << '/' << overheadElevation
              << "; isolated foghorn peak=" << foghornPeak
              << " RMS=" << foghornRms << '\n';

    // Coast and terrain returns must outlive the direct call. This guards the
    // outdoor reflection field against accidentally becoming a zero-latency
    // level boost or disappearing when the source family is muted.
    auto reflectedFoghorn = s3g::ambiHorizonFactoryPreset(14u);
    reflectedFoghorn.localFloor = 0.0f;
    reflectedFoghorn.horizonBed = 0.45f;
    reflectedFoghorn.signals = 1.0f;
    reflectedFoghorn.airNoise = 0.0f;
    reflectedFoghorn.surf = 0.0f;
    reflectedFoghorn.foghorns = 1.0f;
    reflectedFoghorn.outputGainDb = 0.0f;
    encoder.setParams(reflectedFoghorn);
    encoder.reset();
    for (uint32_t block = 0u; block < 24u; ++block) {
        (void)render(encoder, encoder.activeChannels());
    }
    auto silentHorizon = reflectedFoghorn;
    silentHorizon.signals = 0.0f;
    silentHorizon.horizonBed = 0.0f;
    silentHorizon.foghorns = 0.0f;
    encoder.setParams(silentHorizon);
    for (uint32_t block = 0u; block < 5u; ++block) {
        (void)render(encoder, encoder.activeChannels());
    }
    float landscapeTailEnergy = 0.0f;
    for (uint32_t block = 0u; block < 8u; ++block) {
        landscapeTailEnergy += absoluteEnergy(
            render(encoder, encoder.activeChannels()),
            encoder.activeChannels());
    }
    assert(landscapeTailEnergy > 0.001f);
    std::cout << "Coastal reflection tail energy="
              << landscapeTailEnergy << '\n';

    // The high-fidelity scene must remain a complete, audible horizon when
    // the synthesized broadband-air layer is explicitly disabled.
    auto highFidelity = s3g::ambiHorizonFactoryPreset(12u);
    assert(highFidelity.airNoise == 0.0f);
    encoder.setParams(highFidelity);
    encoder.reset();
    float highFidelityEnergy = 0.0f;
    for (uint32_t block = 0u; block < 6u; ++block) {
        highFidelityEnergy += absoluteEnergy(
            render(encoder, encoder.activeChannels()),
            encoder.activeChannels());
    }
    assert(highFidelityEnergy > 0.001f);

    auto localOnly = highFidelity;
    localOnly.entities = 4u;
    localOnly.signals = 0.0f;
    localOnly.horizonBed = 0.0f;
    localOnly.localFloor = 1.0f;
    localOnly.outputGainDb = 0.0f;
    encoder.setParams(localOnly);
    encoder.reset();
    for (uint32_t block = 0u; block < 8u; ++block) {
        (void)render(encoder, encoder.activeChannels());
    }
    const auto localTexture = textureMetrics(
        render(encoder, encoder.activeChannels())[0]);
    const float localRoughness = localTexture.differenceRms
        / std::max(localTexture.rms, 1.0e-12f);
    assert(localTexture.rms > 1.0e-6f);
    assert(localRoughness < 0.30f);

    // BED must remain a correlated low/mid environmental body even with all
    // named generators and synthesized AIR disabled. A broadband noise floor
    // has a first-difference ratio near or above unity; the horizon body is
    // required to remain more than an order of magnitude smoother.
    auto bedOnly = s3g::ambiHorizonFactoryPreset(12u);
    bedOnly.entities = 16u;
    bedOnly.signals = 0.0f;
    bedOnly.horizonBed = 1.0f;
    bedOnly.localFloor = 0.0f;
    bedOnly.airNoise = 0.0f;
    bedOnly.machines = 0.0f;
    bedOnly.bells = 0.0f;
    bedOnly.traffic = 0.0f;
    bedOnly.aircraft = 0.0f;
    bedOnly.foghorns = 0.0f;
    bedOnly.surf = 0.0f;
    bedOnly.outputGainDb = 0.0f;
    encoder.setParams(bedOnly);
    encoder.reset();
    for (uint32_t block = 0u; block < 10u; ++block) {
        (void)render(encoder, encoder.activeChannels());
    }
    const auto bedTexture = textureMetrics(
        render(encoder, encoder.activeChannels())[0]);
    const float bedRoughness = bedTexture.differenceRms
        / std::max(bedTexture.rms, 1.0e-12f);
    assert(bedTexture.rms > 1.0e-6f);
    assert(bedRoughness < 0.10f);
    std::cout << "Horizon-body roughness=" << bedRoughness << '\n';

    auto distantMachines = s3g::ambiHorizonFactoryPreset(4u);
    distantMachines.entities = 16u;
    distantMachines.activity = 1.0f;
    distantMachines.occupancy = 1.0f;
    distantMachines.pace = 1.0f;
    distantMachines.signals = 1.0f;
    distantMachines.horizonBed = 0.0f;
    distantMachines.localFloor = 0.0f;
    distantMachines.airNoise = 0.0f;
    distantMachines.outputGainDb = 0.0f;
    auto noMachines = distantMachines;
    noMachines.machines = 0.0f;
    s3g::AmbiHorizonEncoder machineSource;
    s3g::AmbiHorizonEncoder machineReference;
    machineSource.prepare(48000.0);
    machineReference.prepare(48000.0);
    machineSource.setParams(distantMachines);
    machineReference.setParams(noMachines);
    machineSource.reset();
    machineReference.reset();
    float machinePeak = 0.0f;
    float machineMaximumDifference = 0.0f;
    std::vector<float> machineBlockRms;
    machineBlockRms.reserve(240u);
    // More than twenty seconds covers the old pressure voice's fastest
    // recurrence, preventing a short startup window from hiding its return.
    for (uint32_t block = 0u; block < 240u; ++block) {
        const auto withMachines = render(
            machineSource, machineSource.activeChannels());
        const auto withoutMachines = render(
            machineReference, machineReference.activeChannels());
        std::vector<float> isolatedMachine(kFrames);
        for (uint32_t frame = 0u; frame < kFrames; ++frame) {
            isolatedMachine[frame] = withMachines[0][frame]
                - withoutMachines[0][frame];
        }
        const auto machineWindow = textureMetrics(isolatedMachine);
        machinePeak = std::max(machinePeak, machineWindow.peak);
        machineMaximumDifference = std::max(
            machineMaximumDifference, machineWindow.maximumDifference);
        machineBlockRms.push_back(machineWindow.rms);
    }
    std::sort(machineBlockRms.begin(), machineBlockRms.end());
    const float machineMedianRms = machineBlockRms[machineBlockRms.size() / 2u];
    const float machineMaximumRms = machineBlockRms.back();
    assert(machinePeak > 1.0e-5f);
    assert(machinePeak < 0.25f);
    assert(machineMaximumDifference < machinePeak * 0.02f);
    assert(machineMaximumRms < machineMedianRms * 2.2f);
    std::cout << "Local-floor roughness=" << localRoughness
              << "; long-run machine max slope="
              << machineMaximumDifference / machinePeak
              << " peak=" << machinePeak
              << " max/median block RMS="
              << machineMaximumRms / machineMedianRms << '\n';

    for (uint32_t preset = 0u;
         preset < s3g::kAmbiHorizonFactoryPresetCount; ++preset) {
        const auto metrics = presetAudibility(
            s3g::ambiHorizonFactoryPreset(preset));
        const auto toDb = [](float value) {
            return 20.0f * std::log10(std::max(value, 1.0e-12f));
        };
        std::cout << "Preset " << preset << " "
                  << s3g::kAmbiHorizonPresetInfo[preset].name
                  << " RMS=" << metrics.rms
                  << " (" << toDb(metrics.rms) << " dBFS)"
                  << " quietest=" << metrics.quietestBlockRms
                  << " (" << toDb(metrics.quietestBlockRms) << " dBFS)"
                  << " peak=" << metrics.peak << '\n';
        // At OUT = 0 dB, even the deliberately sparse/vanishing scenes must
        // maintain a useful distant-field body over a four-second listen.
        // -54 dBFS W-channel RMS is quiet, but does not require gain beyond
        // unity merely to establish that the preset is producing a scene.
        assert(metrics.rms >= 0.0020f);
        assert(metrics.peak >= 0.0060f);
    }

    for (uint32_t preset = 0u;
         preset < s3g::kAmbiHorizonFactoryPresetCount; ++preset) {
        const auto candidate = s3g::ambiHorizonFactoryPreset(preset);
        assert(candidate.entities >= 4u
            && candidate.entities <= s3g::kAmbiHorizonMaxEntities);
        assert(candidate.rangeKm >= 0.03f && candidate.rangeKm <= 20.0f);
        assert(candidate.horizonBed >= 0.0f && candidate.horizonBed <= 0.68f);
        assert(candidate.airNoise >= 0.0f && candidate.airNoise <= 1.0f);
        assert(candidate.machines >= 0.0f && candidate.machines <= 1.0f);
        assert(candidate.bells >= 0.0f && candidate.bells <= 1.0f);
        assert(candidate.traffic >= 0.0f && candidate.traffic <= 1.0f);
        assert(candidate.aircraft >= 0.0f && candidate.aircraft <= 1.0f);
        assert(candidate.foghorns >= 0.0f && candidate.foghorns <= 1.0f);
        assert(candidate.surf >= 0.0f && candidate.surf <= 1.0f);
        assert(candidate.trafficSpeed >= 0.0f && candidate.trafficSpeed <= 1.0f);
        assert(candidate.engineLoad >= 0.0f && candidate.engineLoad <= 1.0f);
        assert(candidate.aircraftFlight >= 0.0f && candidate.aircraftFlight <= 1.0f);
        assert(candidate.aircraftSpeed >= 0.0f && candidate.aircraftSpeed <= 1.0f);
        assert(candidate.aircraftPower >= 0.0f && candidate.aircraftPower <= 1.0f);
        assert(candidate.aircraftTone >= 0.0f && candidate.aircraftTone <= 1.0f);
        assert(candidate.foghornPitch >= 0.0f && candidate.foghornPitch <= 1.0f);
        assert(candidate.foghornPressure >= 0.0f && candidate.foghornPressure <= 1.0f);
        assert(candidate.foghornLength >= 0.0f && candidate.foghornLength <= 1.0f);
        assert(candidate.waveRate >= 0.0f && candidate.waveRate <= 1.0f);
        assert(candidate.waveBreak >= 0.0f && candidate.waveBreak <= 1.0f);
        assert(candidate.machineTone >= 0.0f && candidate.machineTone <= 1.0f);
        assert(candidate.bellPitch >= 0.0f && candidate.bellPitch <= 1.0f);
        assert(candidate.bellDecay >= 0.0f && candidate.bellDecay <= 1.0f);
        assert(s3g::kAmbiHorizonPresetInfo[preset].name[0] != '\0');
        encoder.setParams(candidate);
        encoder.reset();
        const auto scene = render(encoder, encoder.activeChannels());
        assert(absoluteEnergy(scene, encoder.activeChannels()) > 0.0001f);
        if (preset == 13u) {
            float maximumElevation = -90.0f;
            for (uint32_t index = 0u; index < encoder.activeEntities(); ++index) {
                maximumElevation = std::max(maximumElevation,
                    encoder.entityTelemetry(index).elevationDeg);
            }
            assert(maximumElevation > 25.0f);
        }
        if (preset == 14u) {
            assert(candidate.rangeKm >= 9.0f);
            assert(candidate.horizonBed <= 0.25f);
            assert(candidate.signals <= 0.85f);
        }
        if (preset == 0u) {
            const float startupRms = channelRms(scene[0]);
            assert(startupRms > 1.0e-5f);
            float bellPeak = 0.0f;
            double bellEnergy = 0.0;
            uint64_t bellSamples = 0u;
            for (uint32_t block = 0u; block < 10u; ++block) {
                const auto bellWindow = render(
                    encoder, encoder.activeChannels());
                bellPeak = std::max(bellPeak, channelPeak(bellWindow[0]));
                for (const float sample : bellWindow[0]) {
                    bellEnergy += static_cast<double>(sample) * sample;
                    ++bellSamples;
                }
            }
            const float bellRms = static_cast<float>(std::sqrt(
                bellEnergy / static_cast<double>(bellSamples)));
            assert(bellPeak > 0.004f);
            assert(bellPeak > bellRms * 3.0f);
            std::cout << "Horizon default W-channel RMS: "
                      << startupRms << "; Bell peak=" << bellPeak
                      << " crest=" << bellPeak / bellRms << '\n';
        }
    }

    return 0;
}
