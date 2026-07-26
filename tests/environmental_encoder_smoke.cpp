#include "s3g_ambi_cryosphere_encoder.h"
#include "s3g_ambi_cryosphere_presets.h"
#include "s3g_ambi_pyrosphere_encoder.h"
#include "s3g_ambi_pyrosphere_presets.h"
#include "s3g_ambi_water_encoder.h"
#include "s3g_ambi_water_presets.h"
#include "s3g_ambi_wind_encoder.h"
#include "s3g_ambi_wind_presets.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <type_traits>

namespace {

template <typename Encoder, typename PresetFn>
float renderPresets(Encoder& encoder, uint32_t presetCount, PresetFn preset)
{
    constexpr uint32_t frames = 2048u;
    constexpr uint32_t channels = 16u;
    std::array<std::array<float, frames>, channels> storage {};
    std::array<float*, channels> outputs {};
    for (uint32_t channel = 0u; channel < channels; ++channel) {
        outputs[channel] = storage[channel].data();
    }
    float peak = 0.0f;
    for (uint32_t index = 0u; index < presetCount; ++index) {
        encoder.reset();
        encoder.setParams(preset(index));
        encoder.beginTransition();
        encoder.process(outputs.data(), channels, frames);
        for (const auto& channel : storage) {
            for (const float sample : channel) {
                if (!std::isfinite(sample)) return -1.0f;
                peak = std::max(peak, std::fabs(sample));
            }
        }
    }
    return peak;
}

template <typename Encoder, typename Params>
float renderDifference(const Params& first, const Params& second)
{
    constexpr uint32_t frames = 4096u;
    constexpr uint32_t channels = 16u;
    using Storage = std::array<std::array<float, frames>, channels>;
    Storage firstStorage {};
    Storage secondStorage {};
    std::array<float*, channels> firstOutputs {};
    std::array<float*, channels> secondOutputs {};
    for (uint32_t channel = 0u; channel < channels; ++channel) {
        firstOutputs[channel] = firstStorage[channel].data();
        secondOutputs[channel] = secondStorage[channel].data();
    }
    Encoder firstEncoder;
    Encoder secondEncoder;
    firstEncoder.prepare(48000.0);
    secondEncoder.prepare(48000.0);
    firstEncoder.setParams(first);
    secondEncoder.setParams(second);
    firstEncoder.process(firstOutputs.data(), channels, frames);
    secondEncoder.process(secondOutputs.data(), channels, frames);
    double difference = 0.0;
    for (uint32_t channel = 0u; channel < channels; ++channel) {
        for (uint32_t frame = 0u; frame < frames; ++frame) {
            difference += std::fabs(firstStorage[channel][frame]
                - secondStorage[channel][frame]);
        }
    }
    return static_cast<float>(difference / (channels * frames));
}

template <typename Encoder, typename Params>
float renderDurationDifference(const Params& first, const Params& second,
    float seconds)
{
    constexpr uint32_t blockFrames = 256u;
    constexpr uint32_t channels = 16u;
    using Storage = std::array<std::array<float, blockFrames>, channels>;
    Storage firstStorage {};
    Storage secondStorage {};
    std::array<float*, channels> firstOutputs {};
    std::array<float*, channels> secondOutputs {};
    for (uint32_t channel = 0u; channel < channels; ++channel) {
        firstOutputs[channel] = firstStorage[channel].data();
        secondOutputs[channel] = secondStorage[channel].data();
    }
    Encoder firstEncoder;
    Encoder secondEncoder;
    firstEncoder.prepare(48000.0);
    secondEncoder.prepare(48000.0);
    firstEncoder.setParams(first);
    secondEncoder.setParams(second);
    uint32_t remaining = static_cast<uint32_t>(seconds * 48000.0f);
    double difference = 0.0;
    uint64_t samples = 0u;
    while (remaining > 0u) {
        const uint32_t frames = std::min(remaining, blockFrames);
        firstEncoder.process(firstOutputs.data(), channels, frames);
        secondEncoder.process(secondOutputs.data(), channels, frames);
        for (uint32_t channel = 0u; channel < channels; ++channel) {
            for (uint32_t frame = 0u; frame < frames; ++frame) {
                difference += std::fabs(firstStorage[channel][frame]
                    - secondStorage[channel][frame]);
            }
        }
        samples += static_cast<uint64_t>(channels) * frames;
        remaining -= frames;
    }
    return samples > 0u ? static_cast<float>(difference / samples) : 0.0f;
}

template <typename FirstEncoder, typename SecondEncoder,
    typename FirstParams, typename SecondParams>
float renderInstrumentDifference(const FirstParams& firstParams,
    const SecondParams& secondParams)
{
    constexpr uint32_t frames = 4096u;
    constexpr uint32_t channels = 16u;
    using Storage = std::array<std::array<float, frames>, channels>;
    Storage firstStorage {};
    Storage secondStorage {};
    std::array<float*, channels> firstOutputs {};
    std::array<float*, channels> secondOutputs {};
    for (uint32_t channel = 0u; channel < channels; ++channel) {
        firstOutputs[channel] = firstStorage[channel].data();
        secondOutputs[channel] = secondStorage[channel].data();
    }
    FirstEncoder first;
    SecondEncoder second;
    first.prepare(48000.0);
    second.prepare(48000.0);
    first.setParams(firstParams);
    second.setParams(secondParams);
    first.process(firstOutputs.data(), channels, frames);
    second.process(secondOutputs.data(), channels, frames);
    double difference = 0.0;
    for (uint32_t channel = 0u; channel < channels; ++channel) {
        for (uint32_t frame = 0u; frame < frames; ++frame) {
            difference += std::fabs(firstStorage[channel][frame]
                - secondStorage[channel][frame]);
        }
    }
    return static_cast<float>(difference / (channels * frames));
}

template <typename Encoder, typename Params>
float renderPeriodicity(const Params& params)
{
    constexpr uint32_t frames = 65536u;
    std::array<float, frames> storage {};
    std::array<float*, 1> outputs { storage.data() };
    Encoder encoder;
    encoder.prepare(48000.0);
    encoder.setParams(params);
    encoder.process(outputs.data(), outputs.size(), frames);
    constexpr std::array<uint32_t, 12> lags {
        43u, 53u, 64u, 80u, 96u, 109u,
        137u, 160u, 218u, 320u, 480u, 800u,
    };
    float maximum = 0.0f;
    for (const uint32_t lag : lags) {
        double numerator = 0.0;
        double firstEnergy = 0.0;
        double secondEnergy = 0.0;
        for (uint32_t frame = 4096u + lag; frame < frames; ++frame) {
            const double first = storage[frame];
            const double second = storage[frame - lag];
            numerator += first * second;
            firstEnergy += first * first;
            secondEnergy += second * second;
        }
        const double denominator = std::sqrt(firstEnergy * secondEnergy);
        if (denominator > 1.0e-12) {
            maximum = std::max(maximum,
                static_cast<float>(std::fabs(numerator / denominator)));
        }
    }
    return maximum;
}

template <typename Encoder>
void renderDuration(Encoder& encoder, float seconds)
{
    constexpr uint32_t blockFrames = 256u;
    constexpr uint32_t channels = 16u;
    std::array<std::array<float, blockFrames>, channels> storage {};
    std::array<float*, channels> outputs {};
    for (uint32_t channel = 0u; channel < channels; ++channel) {
        outputs[channel] = storage[channel].data();
    }
    uint32_t remaining = static_cast<uint32_t>(seconds * 48000.0f);
    while (remaining > 0u) {
        const uint32_t frames = std::min(remaining, blockFrames);
        encoder.process(outputs.data(), channels, frames);
        remaining -= frames;
    }
}

template <typename Encoder, typename Meter>
float renderDurationPeakMeter(Encoder& encoder, float seconds, Meter meter)
{
    constexpr uint32_t blockFrames = 256u;
    constexpr uint32_t channels = 16u;
    std::array<std::array<float, blockFrames>, channels> storage {};
    std::array<float*, channels> outputs {};
    for (uint32_t channel = 0u; channel < channels; ++channel) {
        outputs[channel] = storage[channel].data();
    }
    float maximum = 0.0f;
    uint32_t remaining = static_cast<uint32_t>(seconds * 48000.0f);
    while (remaining > 0u) {
        const uint32_t frames = std::min(remaining, blockFrames);
        encoder.process(outputs.data(), channels, frames);
        maximum = std::max(maximum, meter(encoder));
        remaining -= frames;
    }
    return maximum;
}

template <typename Encoder, typename Params>
float renderLowFrequencyFraction(const Params& params)
{
    constexpr uint32_t frames = 65536u;
    std::array<float, frames> storage {};
    std::array<float*, 1> outputs { storage.data() };
    Encoder encoder;
    encoder.prepare(48000.0);
    encoder.setParams(params);
    encoder.process(outputs.data(), outputs.size(), frames);
    const float coefficient = 1.0f
        - std::exp(-2.0f * s3g::kPi * 120.0f / 48000.0f);
    float lowpass = 0.0f;
    double lowEnergy = 0.0;
    double totalEnergy = 0.0;
    for (uint32_t frame = 4096u; frame < frames; ++frame) {
        lowpass += (storage[frame] - lowpass) * coefficient;
        lowEnergy += static_cast<double>(lowpass) * lowpass;
        totalEnergy += static_cast<double>(storage[frame]) * storage[frame];
    }
    return static_cast<float>(lowEnergy / std::max(1.0e-12, totalEnergy));
}

} // namespace

int main()
{
    static_assert(!std::is_same_v<s3g::AmbiPyrosphereParams,
        s3g::AmbiWindParams>);
    static_assert(!std::is_same_v<s3g::AmbiCryosphereParams,
        s3g::AmbiWaterParams>);
    constexpr std::array<uint32_t, s3g::kAmbiPyrosphereFactoryPresetCount>
        expectedPyrosphereMaterials { 2u, 3u, 9u, 10u, 11u, 2u,
            4u, 5u, 6u, 12u, 6u, 11u, 11u, 13u };
    constexpr std::array<uint32_t, s3g::kAmbiCryosphereFactoryPresetCount>
        expectedCryosphereProcesses { 0u, 1u, 2u, 3u, 4u, 5u,
            6u, 7u, 8u, 9u, 10u, 12u, 0u, 13u };
    bool presetMappingsValid = true;
    for (uint32_t index = 0u; index < expectedPyrosphereMaterials.size();
        ++index) {
        presetMappingsValid = presetMappingsValid
            && s3g::ambiPyrosphereFactoryPreset(index).materialMode
                == expectedPyrosphereMaterials[index]
            && s3g::ambiCryosphereFactoryPreset(index).regime
                == expectedCryosphereProcesses[index];
    }
    s3g::AmbiPyrosphereEncoder pyrosphere;
    s3g::AmbiCryosphereEncoder cryosphere;
    s3g::AmbiWindEncoder wind;
    s3g::AmbiWaterEncoder water;
    pyrosphere.prepare(48000.0);
    cryosphere.prepare(48000.0);
    wind.prepare(48000.0);
    water.prepare(48000.0);
    pyrosphere.setParameterSurfaceGlideMs(240.0f);
    cryosphere.setParameterSurfaceGlideMs(240.0f);
    wind.setParameterSurfaceGlideMs(240.0f);
    water.setParameterSurfaceGlideMs(240.0f);

    const float pyrospherePeak = renderPresets(pyrosphere,
        s3g::kAmbiPyrosphereFactoryPresetCount,
        [](uint32_t index) { return s3g::ambiPyrosphereFactoryPreset(index); });
    const float cryospherePeak = renderPresets(cryosphere,
        s3g::kAmbiCryosphereFactoryPresetCount,
        [](uint32_t index) { return s3g::ambiCryosphereFactoryPreset(index); });

    auto expandedWind = s3g::ambiWindFactoryPreset(7u);
    expandedWind.materialMode = 10u;
    expandedWind.particles = 1.0f;
    expandedWind.vortex = 0.8f;
    expandedWind.pressure = 0.7f;
    const float windPeak = renderPresets(wind, 1u,
        [&](uint32_t) { return expandedWind; });
    auto baselineWind = expandedWind;
    baselineWind.materialMode = 0u;
    baselineWind.particles = 0.0f;
    baselineWind.vortex = 0.0f;
    baselineWind.pressure = 0.0f;
    const float windExtensionDifference = renderDifference<s3g::AmbiWindEncoder>(
        baselineWind, expandedWind);

    auto expandedWater = s3g::ambiWaterFactoryPreset(11u);
    expandedWater.regime = 8u;
    expandedWater.environment = 9u;
    expandedWater.foam = 1.0f;
    expandedWater.shore = 1.0f;
    const float waterPeak = renderPresets(water, 1u,
        [&](uint32_t) { return expandedWater; });
    auto baselineWater = expandedWater;
    baselineWater.regime = 3u;
    baselineWater.environment = 0u;
    baselineWater.foam = 0.0f;
    baselineWater.shore = 0.0f;
    const float waterExtensionDifference = renderDifference<s3g::AmbiWaterEncoder>(
        baselineWater, expandedWater);

    auto gas = s3g::ambiPyrosphereFactoryPreset(8u);
    gas.materialMode = 0u;
    auto timber = gas;
    timber.materialMode = 3u;
    timber.material = 0.82f;
    timber.grit = 0.72f;
    timber.particles = 0.68f;
    const float fuelDifference = renderDifference<s3g::AmbiPyrosphereEncoder>(
        gas, timber);
    const float pyrosphereVsWind = renderInstrumentDifference<
        s3g::AmbiPyrosphereEncoder, s3g::AmbiWindEncoder>(
            timber, expandedWind);

    auto frostCrack = s3g::ambiCryosphereFactoryPreset(0u);
    auto hail = frostCrack;
    hail.regime = 9u;
    hail.drops = 0.92f;
    hail.contact = 0.94f;
    hail.scale = 0.22f;
    const float iceRegimeDifference = renderDifference<
        s3g::AmbiCryosphereEncoder>(frostCrack, hail);
    const float cryosphereVsWater = renderInstrumentDifference<
        s3g::AmbiCryosphereEncoder, s3g::AmbiWaterEncoder>(
            frostCrack, expandedWater);

    auto rockSpall = s3g::ambiPyrosphereFactoryPreset(11u);
    rockSpall.materialMode = 12u;
    rockSpall.wind = 0.92f;
    rockSpall.material = 0.86f;
    rockSpall.q = 0.88f;
    rockSpall.grit = 0.72f;
    rockSpall.space = 0.0f;
    pyrosphere.reset();
    pyrosphere.setParams(rockSpall);
    renderDuration(pyrosphere, 2.0f);
    const float pyrospherePeriodicity = renderPeriodicity<
        s3g::AmbiPyrosphereEncoder>(rockSpall);

    auto calving = s3g::ambiCryosphereFactoryPreset(5u);
    calving.regime = 5u;
    calving.water = 0.78f;
    calving.flow = 0.82f;
    calving.splash = 1.0f;
    calving.density = 0.84f;
    calving.space = 0.0f;
    cryosphere.reset();
    cryosphere.setParams(calving);
    renderDuration(cryosphere, 3.0f);
    const float cryospherePeriodicity = renderPeriodicity<
        s3g::AmbiCryosphereEncoder>(calving);

    auto timberFall = s3g::ambiPyrosphereFactoryPreset(12u);
    auto timberFallDisabled = timberFall;
    timberFallDisabled.structuralLoad = 0.0f;
    timberFallDisabled.snap = 0.0f;
    timberFallDisabled.fall = 0.0f;
    const float timberFallDifference = renderDurationDifference<
        s3g::AmbiPyrosphereEncoder>(
            timberFall, timberFallDisabled, 4.0f);
    s3g::AmbiPyrosphereEncoder structuralPyrosphere;
    structuralPyrosphere.prepare(48000.0);
    structuralPyrosphere.setParams(timberFall);
    renderDuration(structuralPyrosphere, 10.0f);

    auto iceUnderFoot = s3g::ambiCryosphereFactoryPreset(12u);
    auto iceUnderFootDisabled = iceUnderFoot;
    iceUnderFootDisabled.surfaceLoad = 0.0f;
    iceUnderFootDisabled.snap = 0.0f;
    iceUnderFootDisabled.plateFailure = 0.0f;
    const float iceUnderFootDifference = renderDurationDifference<
        s3g::AmbiCryosphereEncoder>(
            iceUnderFoot, iceUnderFootDisabled, 4.0f);
    s3g::AmbiCryosphereEncoder structuralCryosphere;
    structuralCryosphere.prepare(48000.0);
    structuralCryosphere.setParams(iceUnderFoot);
    renderDuration(structuralCryosphere, 10.0f);

    auto sparseIgnition = s3g::ambiPyrosphereFactoryPreset(1u);
    sparseIgnition.voices = 24u;
    sparseIgnition.gustRate = s3g::kAmbiPyrosphereMinIgnitionRateHz;
    auto denseIgnition = sparseIgnition;
    denseIgnition.gustRate = s3g::kAmbiPyrosphereMaxIgnitionRateHz;
    s3g::AmbiPyrosphereEncoder sparsePyrosphere;
    s3g::AmbiPyrosphereEncoder densePyrosphere;
    sparsePyrosphere.prepare(48000.0);
    densePyrosphere.prepare(48000.0);
    sparsePyrosphere.setParams(sparseIgnition);
    densePyrosphere.setParams(denseIgnition);
    renderDuration(sparsePyrosphere, 4.0f);
    renderDuration(densePyrosphere, 4.0f);

    auto heldCryosphereParams = s3g::ambiCryosphereFactoryPreset(12u);
    heldCryosphereParams.flow = 0.0f;
    auto denseCryosphereParams = heldCryosphereParams;
    denseCryosphereParams.flow = 1.0f;
    s3g::AmbiCryosphereEncoder heldCryosphere;
    s3g::AmbiCryosphereEncoder denseCryosphere;
    heldCryosphere.prepare(48000.0);
    denseCryosphere.prepare(48000.0);
    heldCryosphere.setParams(heldCryosphereParams);
    denseCryosphere.setParams(denseCryosphereParams);
    renderDuration(heldCryosphere, 4.0f);
    renderDuration(denseCryosphere, 4.0f);
    const uint64_t heldCryosphereEvents = heldCryosphere.fractureEventCount()
        + heldCryosphere.slipEventCount()
        + heldCryosphere.calvingEventCount()
        + heldCryosphere.structuralSnapEventCount()
        + heldCryosphere.plateFailureEventCount();
    const uint64_t denseCryosphereEvents = denseCryosphere.fractureEventCount()
        + denseCryosphere.slipEventCount()
        + denseCryosphere.calvingEventCount()
        + denseCryosphere.structuralSnapEventCount()
        + denseCryosphere.plateFailureEventCount();

    auto flamethrower = s3g::ambiPyrosphereFactoryPreset(13u);
    auto flamethrowerWithoutJet = flamethrower;
    flamethrowerWithoutJet.materialMode = 0u;
    const float flamethrowerDifference = renderDifference<
        s3g::AmbiPyrosphereEncoder>(flamethrower, flamethrowerWithoutJet);
    s3g::AmbiPyrosphereEncoder flamethrowerEncoder;
    flamethrowerEncoder.prepare(48000.0);
    flamethrowerEncoder.setParams(flamethrower);
    renderDuration(flamethrowerEncoder, 2.0f);
    auto lightFlamethrowerBody = flamethrower;
    lightFlamethrowerBody.body = 0.08f;
    lightFlamethrowerBody.space = 0.0f;
    auto heavyFlamethrowerBody = lightFlamethrowerBody;
    heavyFlamethrowerBody.body = 0.96f;
    const float lightFlamethrowerLowFraction = renderLowFrequencyFraction<
        s3g::AmbiPyrosphereEncoder>(lightFlamethrowerBody);
    const float heavyFlamethrowerLowFraction = renderLowFrequencyFraction<
        s3g::AmbiPyrosphereEncoder>(heavyFlamethrowerBody);
    auto zeroWander = flamethrower;
    zeroWander.vectorRateHz = 0.0f;
    s3g::AmbiPyrosphereEncoder wanderSanitizer;
    wanderSanitizer.prepare(48000.0);
    wanderSanitizer.setParams(zeroWander);

    auto singingLake = s3g::ambiCryosphereFactoryPreset(13u);
    auto singingLakeWithoutPlateWaves = singingLake;
    singingLakeWithoutPlateWaves.regime = 0u;
    const float singingLakeDifference = renderDurationDifference<
        s3g::AmbiCryosphereEncoder>(singingLake,
            singingLakeWithoutPlateWaves, 4.0f);
    s3g::AmbiCryosphereEncoder singingLakeEncoder;
    singingLakeEncoder.prepare(48000.0);
    singingLakeEncoder.setParams(singingLake);
    const float singingLakePeakLayer = renderDurationPeakMeter(
        singingLakeEncoder, 12.0f,
        [](const s3g::AmbiCryosphereEncoder& encoder) {
            return encoder.singingIceLayerEnergy();
        });

    s3g::SingingIceModel flexuralPacket;
    flexuralPacket.prepare(48000.0, 0x51a91ce5u);
    flexuralPacket.excite(1.0f, 0.78f, 0.46f, 0.66f, 0.34f, 0.84f);
    float earlyFlexuralFrequency = 0.0f;
    float lateFlexuralFrequency = 0.0f;
    double flexuralEnergy = 0.0;
    for (uint32_t frame = 0u; frame < 96000u; ++frame) {
        const auto packet = flexuralPacket.process();
        flexuralEnergy += static_cast<double>(packet.sample) * packet.sample;
        if (frame == 2400u) {
            earlyFlexuralFrequency = flexuralPacket.dominantFrequencyHz();
        } else if (frame == 26400u) {
            lateFlexuralFrequency = flexuralPacket.dominantFrequencyHz();
        }
    }
    const bool flexuralPacketEnded = !flexuralPacket.active();

    std::cout << "pyrosphere/cryosphere/wind/water peaks: "
              << pyrospherePeak << " / "
              << cryospherePeak << " / " << windPeak << " / " << waterPeak
              << "\n";
    std::cout << "wind/water extension mean differences: "
              << windExtensionDifference << " / " << waterExtensionDifference
              << "\n";
    std::cout << "material/process mean differences: " << fuelDifference
              << " / " << iceRegimeDifference << "\n";
    std::cout << "preset material/process mappings: "
              << (presetMappingsValid ? "valid" : "INVALID") << "\n";
    std::cout << "pyrosphere-vs-wind/cryosphere-vs-water differences: "
              << pyrosphereVsWind << " / " << cryosphereVsWater << "\n";
    std::cout << "dedicated layer energies: "
              << pyrosphere.combustionLayerEnergy() << " / "
              << cryosphere.iceLayerEnergy() << "\n";
    std::cout << "geological event counts pyro fracture/spall/collapse: "
              << pyrosphere.geologicalEventCount() << " / "
              << pyrosphere.spallEventCount() << " / "
              << pyrosphere.collapseEventCount() << "\n";
    std::cout << "geological event counts cryo fracture/slip/calving/impact: "
              << cryosphere.fractureEventCount() << " / "
              << cryosphere.slipEventCount() << " / "
              << cryosphere.calvingEventCount() << " / "
              << cryosphere.impactEventCount() << "\n";
    std::cout << "maximum sampled periodicity pyro/cryo: "
              << pyrospherePeriodicity << " / "
              << cryospherePeriodicity << "\n";
    std::cout << "structural consequence differences pyro/cryo: "
              << timberFallDifference << " / "
              << iceUnderFootDifference << "\n";
    std::cout << "structural event counts pyro snap/fall: "
              << structuralPyrosphere.structuralSnapEventCount() << " / "
              << structuralPyrosphere.fallEventCount() << "\n";
    std::cout << "structural event counts cryo snap/plate failure: "
              << structuralCryosphere.structuralSnapEventCount() << " / "
              << structuralCryosphere.plateFailureEventCount() << "\n";
    std::cout << "ignition clock events min/max: "
              << sparsePyrosphere.ignitionEventCount() << " / "
              << densePyrosphere.ignitionEventCount() << "\n";
    std::cout << "cryosphere event clock counts hold/max: "
              << heldCryosphereEvents << " / " << denseCryosphereEvents
              << "\n";
    std::cout << "flamethrower/singing-lake model differences: "
              << flamethrowerDifference << " / " << singingLakeDifference
              << "\n";
    std::cout << "pressure-jet/singing-ice layer energies: "
              << flamethrowerEncoder.jetLayerEnergy() << " / "
              << singingLakePeakLayer << "\n";
    std::cout << "flamethrower low-band fraction light/heavy body: "
              << lightFlamethrowerLowFraction << " / "
              << heavyFlamethrowerLowFraction << "\n";
    std::cout << "sanitized plume-wander floor: "
              << wanderSanitizer.params().vectorRateHz << " Hz\n";
    std::cout << "singing-ice events and flexural descent: "
              << singingLakeEncoder.singingIceEventCount() << " / "
              << earlyFlexuralFrequency << " -> "
              << lateFlexuralFrequency << " Hz\n";
    return presetMappingsValid
            && pyrospherePeak > 0.0f && cryospherePeak > 0.0f
            && windPeak > 0.0f && waterPeak > 0.0f
            && windExtensionDifference > 1.0e-5f
            && waterExtensionDifference > 1.0e-5f
            && fuelDifference > 1.0e-4f
            && iceRegimeDifference > 1.0e-4f
            && pyrosphereVsWind > 1.0e-4f
            && cryosphereVsWater > 1.0e-4f
            && pyrosphere.combustionLayerEnergy() > 1.0e-8f
            && cryosphere.iceLayerEnergy() > 1.0e-8f
            && pyrosphere.geologicalEventCount() > 0u
            && pyrosphere.spallEventCount() > 0u
            && cryosphere.fractureEventCount() > 0u
            && cryosphere.slipEventCount() > 0u
            && cryosphere.calvingEventCount() > 0u
            && cryosphere.impactEventCount() > 0u
            && timberFallDifference > 1.0e-5f
            && iceUnderFootDifference > 1.0e-5f
            && structuralPyrosphere.structuralSnapEventCount() > 0u
            && structuralPyrosphere.fallEventCount() > 0u
            && structuralCryosphere.structuralSnapEventCount() > 0u
            && structuralCryosphere.plateFailureEventCount() > 0u
            && densePyrosphere.ignitionEventCount()
                > sparsePyrosphere.ignitionEventCount() + 500u
            && heldCryosphereEvents == 0u
            && denseCryosphereEvents > 50u
            && flamethrowerDifference > 1.0e-4f
            && flamethrowerEncoder.jetLayerEnergy() > 1.0e-8f
            && singingLakeDifference > 1.0e-5f
            && singingLakePeakLayer > 1.0e-9f
            && singingLakeEncoder.singingIceEventCount() > 0u
            && flexuralEnergy > 1.0e-6
            && earlyFlexuralFrequency > 300.0f
            && earlyFlexuralFrequency > lateFlexuralFrequency * 1.70f
            && lateFlexuralFrequency > 40.0f
            && flexuralPacketEnded
            && heavyFlamethrowerLowFraction
                > lightFlamethrowerLowFraction * 1.15f
            && wanderSanitizer.params().vectorRateHz
                >= s3g::kAmbiPyrosphereMinPlumeWanderHz
            && pyrospherePeriodicity < 0.72f
            && cryospherePeriodicity < 0.72f
        ? 0 : 1;
}
