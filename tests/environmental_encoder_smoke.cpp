#include "s3g_ambi_cryosphere_encoder.h"
#include "s3g_ambi_cryosphere_presets.h"
#include "s3g_ambi_pyrosphere_encoder.h"
#include "s3g_ambi_pyrosphere_presets.h"
#include "s3g_ambi_water_encoder.h"
#include "s3g_ambi_water_presets.h"
#include "s3g_ambi_wind_encoder.h"
#include "s3g_ambi_wind_presets.h"
#include "s3g_planetary_modal_body.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string_view>
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
        float presetPeak = 0.0f;
        for (const auto& channel : storage) {
            for (const float sample : channel) {
                if (!std::isfinite(sample)) return -1.0f;
                presetPeak = std::max(presetPeak, std::fabs(sample));
            }
        }
        if (!(presetPeak > 0.0f)) {
            std::cerr << "factory preset produced no signal: "
                      << index << "\n";
            return -1.0f;
        }
        peak = std::max(peak, presetPeak);
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
    // Remove the newly modelled geological/combustion mass band before
    // looking for objectionable tonal repetition. Broad low-frequency
    // energy is deliberately measured by renderLowFrequencyFraction().
    constexpr float highpassCutoffHz = 160.0f;
    const float coefficient = 1.0f - std::exp(
        -2.0f * s3g::kPi * highpassCutoffHz / 48000.0f);
    float lowpass = 0.0f;
    for (float& sample : storage) {
        lowpass += (sample - lowpass) * coefficient;
        sample -= lowpass;
    }
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

template <typename Encoder, typename Params>
float renderBroadPeriodicity(const Params& params)
{
    constexpr uint32_t frames = 65536u;
    std::array<float, frames> storage {};
    std::array<float*, 1> outputs { storage.data() };
    Encoder encoder;
    encoder.prepare(48000.0);
    encoder.setParams(params);
    encoder.process(outputs.data(), outputs.size(), frames);
    const float dcCoefficient = 1.0f - std::exp(
        -2.0f * s3g::kPi * 10.0f / 48000.0f);
    float dc = 0.0f;
    for (float& sample : storage) {
        dc += (sample - dc) * dcCoefficient;
        sample -= dc;
    }
    float maximum = 0.0f;
    for (uint32_t lag = 120u; lag <= 1600u; lag += 16u) {
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

struct EnvironmentalScoreProbe {
    uint64_t arcs = 0u;
    uint64_t cascades = 0u;
    uint64_t consequences = 0u;
    uint32_t quietSteps = 0u;
    uint32_t maximumActive = 0u;
    uint64_t activeSum = 0u;
    float finalActivity = 0.0f;
};

EnvironmentalScoreProbe runEnvironmentalScoreProbe(
    s3g::EnvironmentalScoreParams params =
        { .64f, .08f, .72f, .58f, .88f },
    float rangeExpansion = 0.0f, uint32_t entities = 12u,
    uint32_t steps = 12000u)
{
    s3g::EnvironmentalScore score;
    score.prepare(48000.0, 0x5c0a7e21u);
    score.setParams(params);
    score.setRangeExpansion(rangeExpansion);
    score.reset(entities);
    for (uint32_t index = 0u; index < entities; ++index) {
        const float phase = static_cast<float>(index)
            / static_cast<float>(entities) * s3g::kPi * 2.0f;
        score.setEntityPosition(index,
            std::cos(phase), std::sin(phase),
            index & 1u ? 0.16f : -0.16f);
    }
    EnvironmentalScoreProbe result {};
    for (uint32_t step = 0u; step < steps; ++step) {
        score.update(0.01f, entities);
        const uint32_t active = score.activeEntityCount();
        if (active == 0u) ++result.quietSteps;
        result.maximumActive = std::max(result.maximumActive, active);
        result.activeSum += active;
    }
    result.arcs = score.arcCount();
    result.cascades = score.cascadeCount();
    result.consequences = score.consequenceCount();
    result.finalActivity = score.globalActivity();
    return result;
}

struct PlanetaryModalProbe {
    bool deterministic = true;
    bool finite = true;
    bool silentBeforeRelease = true;
    bool c1Onset = true;
    bool frequencyBounds = true;
    bool decayBounds = true;
    bool resetReplay = true;
    bool silentWithoutFlux = true;
    bool stationaryFrequencies = true;
    bool allProfilesEnergized = true;
    double earlyEnergy = 0.0;
    double lateEnergy = 0.0;
};

PlanetaryModalProbe runPlanetaryModalProbe()
{
    constexpr std::array<s3g::PlanetaryModalProfile, 9u> profiles {
        s3g::PlanetaryModalProfile::CryoTidal,
        s3g::PlanetaryModalProfile::CryoDune,
        s3g::PlanetaryModalProfile::CryoBrine,
        s3g::PlanetaryModalProfile::CryoQuasicrystal,
        s3g::PlanetaryModalProfile::PyroSilicate,
        s3g::PlanetaryModalProfile::PyroSupercritical,
        s3g::PlanetaryModalProfile::PyroVent,
        s3g::PlanetaryModalProfile::PyroLattice,
        s3g::PlanetaryModalProfile::CryoSingingLake,
    };
    static_assert(s3g::PlanetaryModalBody::kModeCount == 2u);
    PlanetaryModalProbe result {};
    for (uint32_t profile = 0u; profile < profiles.size(); ++profile) {
        const uint32_t seed = 0x91e10da5u + profile * 0x10204081u;
        s3g::PlanetaryModalBody first;
        s3g::PlanetaryModalBody second;
        first.prepare(48000.0, seed);
        second.prepare(48000.0, seed);
        s3g::PlanetaryModalParams params {};
        params.profile = profiles[profile];
        params.amount = 0.82f;
        params.scale = 0.57f;
        params.damping = 0.48f;
        params.irregularity = 0.79f;
        params.coupling = 0.74f;
        params.brightness = 0.68f;
        first.setParams(params);
        second.setParams(params);
        std::array<float, s3g::PlanetaryModalBody::kModeCount>
            initialFrequencies {};
        for (uint32_t mode = 0u;
            mode < s3g::PlanetaryModalBody::kModeCount; ++mode) {
            const float frequency = first.modeFrequencyHz(mode);
            initialFrequencies[mode] = frequency;
            const float decay = first.modeDecaySeconds(mode);
            result.frequencyBounds = result.frequencyBounds
                && std::isfinite(frequency) && frequency >= 180.0f
                && frequency <= 48000.0f * 0.40f;
            result.decayBounds = result.decayBounds
                && std::isfinite(decay) && decay >= 0.035f
                && decay <= 1.60f;
        }
        for (uint32_t frame = 0u; frame < 64u; ++frame) {
            const float flux = std::sin(static_cast<float>(frame) * 0.71f);
            const auto a = first.process(flux, 1.0f);
            const auto b = second.process(flux, 1.0f);
            result.silentBeforeRelease = result.silentBeforeRelease
                && a.sample == 0.0f && a.activity == 0.0f;
            result.deterministic = result.deterministic
                && a.sample == b.sample && a.activity == b.activity;
        }
        first.excite(0.94f, 0.86f, true);
        second.excite(0.94f, 0.86f, true);
        const auto firstOnset = first.process(0.83f, 1.0f);
        const auto secondOnset = second.process(0.83f, 1.0f);
        result.c1Onset = result.c1Onset
            && std::fabs(firstOnset.sample) <= 1.0e-6f;
        result.deterministic = result.deterministic
            && firstOnset.sample == secondOnset.sample
            && firstOnset.activity == secondOnset.activity;
        double profileEnergy = 0.0;
        for (uint32_t frame = 1u; frame < 192000u; ++frame) {
            const float phase = static_cast<float>(frame);
            const float flux = std::sin(phase * 0.371f)
                * (0.62f + 0.38f * std::sin(phase * 0.0137f));
            const auto a = first.process(flux, 1.0f);
            const auto b = second.process(flux, 1.0f);
            result.deterministic = result.deterministic
                && a.sample == b.sample && a.activity == b.activity;
            result.finite = result.finite && std::isfinite(a.sample)
                && std::isfinite(a.activity);
            const double energy = static_cast<double>(a.sample) * a.sample;
            profileEnergy += energy;
            if (frame < 48000u) result.earlyEnergy += energy;
            if (frame >= 144000u) result.lateEnergy += energy;
        }
        result.allProfilesEnergized = result.allProfilesEnergized
            && profileEnergy > 1.0e-10;
        for (uint32_t mode = 0u;
            mode < s3g::PlanetaryModalBody::kModeCount; ++mode) {
            result.stationaryFrequencies = result.stationaryFrequencies
                && first.modeFrequencyHz(mode) == initialFrequencies[mode];
        }

        s3g::PlanetaryModalBody zeroFlux;
        zeroFlux.prepare(48000.0, seed);
        zeroFlux.setParams(params);
        zeroFlux.excite(1.0f, 0.8f, true);
        for (uint32_t frame = 0u; frame < 2048u; ++frame) {
            const auto silent = zeroFlux.process(0.0f, 1.0f);
            result.silentWithoutFlux = result.silentWithoutFlux
                && silent.sample == 0.0f;
        }
        const uint64_t triggers = first.triggerCount();
        first.reset();
        first.setParams(params);
        const auto resetSilence = first.process(0.91f, 1.0f);
        result.resetReplay = result.resetReplay
            && triggers == 1u && first.triggerCount() == 0u
            && resetSilence.sample == 0.0f
            && resetSilence.activity == 0.0f;

        params.amount = std::numeric_limits<float>::infinity();
        params.scale = std::numeric_limits<float>::quiet_NaN();
        params.damping = -1000.0f;
        params.irregularity = 1000.0f;
        first.setParams(params);
        first.excite(std::numeric_limits<float>::infinity(),
            std::numeric_limits<float>::quiet_NaN(), true);
        const auto recovered = first.process(
            std::numeric_limits<float>::infinity(),
            std::numeric_limits<float>::quiet_NaN());
        result.finite = result.finite && std::isfinite(recovered.sample)
            && std::isfinite(recovered.activity);
    }
    return result;
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
            4u, 5u, 6u, 12u, 6u, 11u, 11u, 13u,
            14u, 15u, 16u, 17u };
    constexpr std::array<uint32_t, s3g::kAmbiCryosphereFactoryPresetCount>
        expectedCryosphereProcesses { 0u, 1u, 2u, 3u, 4u, 5u,
            6u, 7u, 8u, 9u, 10u, 12u, 0u, 13u,
            15u, 16u, 17u, 14u };
    constexpr std::array<std::string_view, 4u> expectedPyrosphereAlienNames {
        "Silicate Convection Sea",
        "Dense-Atmosphere Hydrocarbon Front",
        "Sulfur Vent Colony",
        "Mineral Lattice Collapse",
    };
    constexpr std::array<std::string_view, 4u> expectedCryosphereAlienNames {
        "Tidal Ocean-Moon Rift",
        "Methane-Ice Dune Creep",
        "Subsurface Brine Upwelling",
        "Quasicrystal Plate Bloom",
    };
    bool presetMappingsValid = true;
    for (uint32_t index = 0u; index < expectedPyrosphereMaterials.size();
        ++index) {
        presetMappingsValid = presetMappingsValid
            && s3g::ambiPyrosphereFactoryPreset(index).materialMode
                == expectedPyrosphereMaterials[index]
            && s3g::ambiCryosphereFactoryPreset(index).regime
                == expectedCryosphereProcesses[index];
    }
    bool alienPresetNamesValid = true;
    for (uint32_t offset = 0u; offset < expectedPyrosphereAlienNames.size();
        ++offset) {
        constexpr uint32_t firstAlienPreset = 14u;
        alienPresetNamesValid = alienPresetNamesValid
            && s3g::ambiPyrosphereFactoryPresetInfo(
                firstAlienPreset + offset).name
                == expectedPyrosphereAlienNames[offset]
            && s3g::ambiCryosphereFactoryPresetInfo(
                firstAlienPreset + offset).name
                == expectedCryosphereAlienNames[offset];
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
    const float fuelDifference = renderDurationDifference<
        s3g::AmbiPyrosphereEncoder>(gas, timber, 2.0f);
    const float pyrosphereVsWind = renderInstrumentDifference<
        s3g::AmbiPyrosphereEncoder, s3g::AmbiWindEncoder>(
            timber, expandedWind);

    auto frostCrack = s3g::ambiCryosphereFactoryPreset(0u);
    auto hail = frostCrack;
    hail.regime = 9u;
    hail.drops = 0.92f;
    hail.contact = 0.94f;
    hail.scale = 0.22f;
    const float iceRegimeDifference = renderDurationDifference<
        s3g::AmbiCryosphereEncoder>(frostCrack, hail, 2.0f);
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
    auto lightCryosphereMass = s3g::ambiCryosphereFactoryPreset(5u);
    lightCryosphereMass.space = 0.0f;
    lightCryosphereMass.scoreRest = 0.0f;
    lightCryosphereMass.scale = 0.08f;
    lightCryosphereMass.depth = 0.08f;
    lightCryosphereMass.eventSize = 0.52f;
    auto heavyCryosphereMass = lightCryosphereMass;
    heavyCryosphereMass.scale = 1.0f;
    heavyCryosphereMass.depth = 1.0f;
    heavyCryosphereMass.eventSize = 1.0f;
    const float lightCryosphereLowFraction = renderLowFrequencyFraction<
        s3g::AmbiCryosphereEncoder>(lightCryosphereMass);
    const float heavyCryosphereLowFraction = renderLowFrequencyFraction<
        s3g::AmbiCryosphereEncoder>(heavyCryosphereMass);
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
    const float singingLakeDeterminism = renderDifference<
        s3g::AmbiCryosphereEncoder>(singingLake, singingLake);
    auto singingLakeAnalysis = singingLake;
    singingLakeAnalysis.space = 0.0f;
    const float singingLakePeriodicity = renderBroadPeriodicity<
        s3g::AmbiCryosphereEncoder>(singingLakeAnalysis);
    const float singingLakeLowFraction = renderLowFrequencyFraction<
        s3g::AmbiCryosphereEncoder>(singingLakeAnalysis);
    s3g::AmbiCryosphereEncoder singingLakeEncoder;
    singingLakeEncoder.prepare(48000.0);
    singingLakeEncoder.setParams(singingLake);
    const float singingLakePeakLayer = renderDurationPeakMeter(
        singingLakeEncoder, 12.0f,
        [](const s3g::AmbiCryosphereEncoder& encoder) {
            return encoder.singingIceLayerEnergy();
        });

    auto quasicrystal = s3g::ambiCryosphereFactoryPreset(17u);
    quasicrystal.voices = 6u;
    quasicrystal.space = 0.0f;
    auto quasicrystalWithoutLattice = quasicrystal;
    quasicrystalWithoutLattice.regime = 0u;
    const float quasicrystalDifference = renderDurationDifference<
        s3g::AmbiCryosphereEncoder>(quasicrystal,
            quasicrystalWithoutLattice, 2.0f);
    const float quasicrystalPeriodicity = renderBroadPeriodicity<
        s3g::AmbiCryosphereEncoder>(quasicrystal);
    const float quasicrystalLowFraction = renderLowFrequencyFraction<
        s3g::AmbiCryosphereEncoder>(quasicrystal);
    s3g::AmbiCryosphereEncoder quasicrystalEncoder;
    quasicrystalEncoder.prepare(48000.0);
    quasicrystalEncoder.setParams(quasicrystal);
    const float quasicrystalPeakLayer = renderDurationPeakMeter(
        quasicrystalEncoder, 3.0f,
        [](const s3g::AmbiCryosphereEncoder& encoder) {
            return encoder.quasicrystalLayerEnergy();
        });
    const float quasicrystalModalLayer =
        quasicrystalEncoder.planetaryModalLayerEnergy();

    constexpr std::array<uint32_t, 4u> pyrosphereTerrestrialDonors {
        5u, 0u, 13u, 12u,
    };
    std::array<float, 4u> pyrosphereAlienDifferences {};
    std::array<float, 4u> pyrosphereAlienDeterminism {};
    std::array<float, 4u> pyrosphereAlienLayers {};
    std::array<float, 4u> pyrosphereAlienModalLayers {};
    std::array<float, 4u> pyrosphereAlienJetLayers {};
    std::array<uint64_t, 4u> pyrosphereAlienModalEvents {};
    std::array<uint64_t, 4u> pyrosphereAlienNamedEvents {};
    std::array<uint64_t, 4u> pyrosphereAlienAllPlanetaryEvents {};
    std::array<uint64_t, 4u> pyrosphereAlienLegacyEvents {};
    for (uint32_t mode = 0u; mode < 4u; ++mode) {
        auto alien = s3g::ambiPyrosphereFactoryPreset(14u + mode);
        auto donor = alien;
        donor.materialMode = pyrosphereTerrestrialDonors[mode];
        pyrosphereAlienDifferences[mode] = renderDifference<
            s3g::AmbiPyrosphereEncoder>(alien, donor);
        pyrosphereAlienDeterminism[mode] = renderDifference<
            s3g::AmbiPyrosphereEncoder>(alien, alien);
        alien.voices = 8u;
        alien.space = 0.0f;
        alien.scorePace = std::max(alien.scorePace, 0.72f);
        alien.scoreOccupancy = std::max(alien.scoreOccupancy, 0.72f);
        alien.scoreRest = std::min(alien.scoreRest, 0.24f);
        s3g::AmbiPyrosphereEncoder encoder;
        encoder.prepare(48000.0);
        encoder.setParams(alien);
        pyrosphereAlienLayers[mode] = renderDurationPeakMeter(
            encoder, 2.0f,
            [](const s3g::AmbiPyrosphereEncoder& rendered) {
                return rendered.planetaryLayerEnergy();
            });
        pyrosphereAlienModalLayers[mode] =
            encoder.planetaryModalLayerEnergy();
        pyrosphereAlienModalEvents[mode] =
            encoder.modalExcitationEventCount();
        const std::array<uint64_t, 4u> events {
            encoder.planetaryBlisterEventCount(),
            encoder.planetaryFrontEventCount(),
            encoder.planetaryVentEventCount(),
            encoder.planetaryLatticeEventCount(),
        };
        pyrosphereAlienNamedEvents[mode] = events[mode];
        for (const uint64_t count : events) {
            pyrosphereAlienAllPlanetaryEvents[mode] += count;
        }
        pyrosphereAlienLegacyEvents[mode] =
            encoder.geologicalEventCount() + encoder.spallEventCount()
            + encoder.collapseEventCount()
            + encoder.structuralSnapEventCount() + encoder.fallEventCount();
        pyrosphereAlienJetLayers[mode] = encoder.jetLayerEnergy();
    }

    constexpr std::array<uint32_t, 3u> cryosphereTerrestrialDonors {
        5u, 8u, 12u,
    };
    std::array<float, 3u> cryosphereAlienDifferences {};
    std::array<float, 3u> cryosphereAlienDeterminism {};
    std::array<float, 3u> cryosphereAlienLayers {};
    std::array<float, 3u> cryosphereAlienModalLayers {};
    std::array<float, 3u> cryosphereAlienSingingLayers {};
    std::array<uint64_t, 3u> cryosphereAlienModalEvents {};
    std::array<uint64_t, 3u> cryosphereAlienNamedEvents {};
    std::array<uint64_t, 3u> cryosphereAlienAllPlanetaryEvents {};
    std::array<uint64_t, 3u> cryosphereAlienLegacyEvents {};
    for (uint32_t mode = 0u; mode < 3u; ++mode) {
        auto alien = s3g::ambiCryosphereFactoryPreset(14u + mode);
        auto donor = alien;
        donor.regime = cryosphereTerrestrialDonors[mode];
        cryosphereAlienDifferences[mode] = renderDifference<
            s3g::AmbiCryosphereEncoder>(alien, donor);
        cryosphereAlienDeterminism[mode] = renderDifference<
            s3g::AmbiCryosphereEncoder>(alien, alien);
        alien.voices = 8u;
        alien.space = 0.0f;
        alien.flow = std::max(alien.flow, 0.68f);
        alien.scorePace = std::max(alien.scorePace, 0.72f);
        alien.scoreOccupancy = std::max(alien.scoreOccupancy, 0.72f);
        alien.scoreRest = std::min(alien.scoreRest, 0.24f);
        s3g::AmbiCryosphereEncoder encoder;
        encoder.prepare(48000.0);
        encoder.setParams(alien);
        cryosphereAlienLayers[mode] = renderDurationPeakMeter(
            encoder, 2.0f,
            [](const s3g::AmbiCryosphereEncoder& rendered) {
                return rendered.planetaryLayerEnergy();
            });
        cryosphereAlienModalLayers[mode] =
            encoder.planetaryModalLayerEnergy();
        cryosphereAlienModalEvents[mode] =
            encoder.modalExcitationEventCount();
        const std::array<uint64_t, 3u> events {
            encoder.tidalRiftEventCount(),
            encoder.hydrocarbonAvalancheEventCount(),
            encoder.brineBreakthroughEventCount(),
        };
        cryosphereAlienNamedEvents[mode] = events[mode];
        for (const uint64_t count : events) {
            cryosphereAlienAllPlanetaryEvents[mode] += count;
        }
        cryosphereAlienLegacyEvents[mode] =
            encoder.fractureEventCount() + encoder.slipEventCount()
            + encoder.calvingEventCount() + encoder.impactEventCount()
            + encoder.structuralSnapEventCount()
            + encoder.plateFailureEventCount()
            + encoder.singingIceEventCount();
        cryosphereAlienSingingLayers[mode] =
            encoder.singingIceLayerEnergy();
    }

    s3g::QuasicrystalIceParams quasicrystalModelParams {};
    quasicrystalModelParams.strainRate = 0.48f;
    quasicrystalModelParams.phaseMobility = 0.32f;
    quasicrystalModelParams.frontSpeed = 0.64f;
    quasicrystalModelParams.branching = 0.92f;
    quasicrystalModelParams.anisotropy = 0.78f;
    quasicrystalModelParams.heterogeneity = 0.84f;
    quasicrystalModelParams.scale = 0.62f;
    quasicrystalModelParams.brittleness = 0.76f;
    quasicrystalModelParams.damping = 0.58f;
    s3g::QuasicrystalIceModel firstQuasicrystalModel;
    s3g::QuasicrystalIceModel secondQuasicrystalModel;
    firstQuasicrystalModel.prepare(48000.0, 0xa93c5e1du);
    secondQuasicrystalModel.prepare(48000.0, 0xa93c5e1du);
    firstQuasicrystalModel.excite(1.0f, 0.94f);
    secondQuasicrystalModel.excite(1.0f, 0.94f);
    bool quasicrystalModelDeterministic = true;
    bool quasicrystalModelFinite = true;
    double quasicrystalModelEnergy = 0.0;
    uint32_t quasicrystalModelFronts = 0u;
    uint32_t quasicrystalModelAvalanches = 0u;
    for (uint32_t frame = 0u; frame < 48000u; ++frame) {
        if (frame == 18000u) {
            firstQuasicrystalModel.excite(0.72f, 0.86f);
            secondQuasicrystalModel.excite(0.72f, 0.86f);
        }
        const auto first = firstQuasicrystalModel.process(
            quasicrystalModelParams);
        const auto second = secondQuasicrystalModel.process(
            quasicrystalModelParams);
        quasicrystalModelDeterministic = quasicrystalModelDeterministic
            && first.sample == second.sample
            && first.activity == second.activity
            && first.frontAdvanced == second.frontAdvanced
            && first.avalancheAdvanced == second.avalancheAdvanced;
        quasicrystalModelFinite = quasicrystalModelFinite
            && std::isfinite(first.sample)
            && std::isfinite(first.activity);
        quasicrystalModelEnergy += static_cast<double>(first.sample)
            * first.sample;
        if (first.frontAdvanced) ++quasicrystalModelFronts;
        if (first.avalancheAdvanced) ++quasicrystalModelAvalanches;
    }
    auto hostileQuasicrystalParams = quasicrystalModelParams;
    hostileQuasicrystalParams.strainRate =
        std::numeric_limits<float>::quiet_NaN();
    hostileQuasicrystalParams.branching =
        std::numeric_limits<float>::infinity();
    const auto recoveredQuasicrystal = firstQuasicrystalModel.process(
        hostileQuasicrystalParams);
    quasicrystalModelFinite = quasicrystalModelFinite
        && std::isfinite(recoveredQuasicrystal.sample)
        && std::isfinite(recoveredQuasicrystal.activity);

    s3g::PlanetaryModalParams lakeModalParams {};
    lakeModalParams.profile =
        s3g::PlanetaryModalProfile::CryoSingingLake;
    lakeModalParams.amount = 0.66f;
    lakeModalParams.scale = 0.80f;
    lakeModalParams.damping = 0.34f;
    lakeModalParams.irregularity = 0.24f;
    lakeModalParams.coupling = 0.48f;
    lakeModalParams.brightness = 0.56f;
    float lakeModalMinimumHz = std::numeric_limits<float>::max();
    float lakeModalMaximumHz = 0.0f;
    for (uint32_t voice = 0u; voice < 14u; ++voice) {
        s3g::PlanetaryModalBody branch;
        branch.prepare(48000.0,
            0xc8a4f31du + voice * 0x94d049bbu);
        branch.setParams(lakeModalParams);
        for (uint32_t mode = 0u;
            mode < s3g::PlanetaryModalBody::kModeCount; ++mode) {
            const float frequency = branch.modeFrequencyHz(mode);
            lakeModalMinimumHz = std::min(lakeModalMinimumHz, frequency);
            lakeModalMaximumHz = std::max(lakeModalMaximumHz, frequency);
        }
    }
    s3g::PlanetaryModalBody lakeModalPacket;
    lakeModalPacket.prepare(48000.0, 0x51a91ce5u);
    lakeModalPacket.setParams(lakeModalParams);
    const std::array<float, 2u> lakeModalInitialFrequencies {
        lakeModalPacket.modeFrequencyHz(0u),
        lakeModalPacket.modeFrequencyHz(1u),
    };
    lakeModalPacket.excite(1.0f, 0.72f, true);
    double lakeModalPacketEnergy = 0.0;
    for (uint32_t frame = 0u; frame < 144000u; ++frame) {
        const float phase = static_cast<float>(frame);
        const float flux = std::sin(phase * 0.417f)
            * (0.62f + std::sin(phase * 0.073f) * 0.38f);
        const auto packet = lakeModalPacket.process(flux, 1.0f);
        lakeModalPacketEnergy += static_cast<double>(packet.sample)
            * packet.sample;
    }
    const bool lakeModalFrequenciesStationary =
        lakeModalPacket.modeFrequencyHz(0u)
            == lakeModalInitialFrequencies[0]
        && lakeModalPacket.modeFrequencyHz(1u)
            == lakeModalInitialFrequencies[1];
    const bool lakeModalPacketEnded = !lakeModalPacket.active();
    const auto scoreProbe = runEnvironmentalScoreProbe();
    const auto repeatedScoreProbe = runEnvironmentalScoreProbe();
    const auto expandedSparseScore = runEnvironmentalScoreProbe(
        { 0.0f, 0.0f, 0.0f, 1.0f, 1.0f }, 1.0f, 32u, 24000u);
    const auto expandedDenseScore = runEnvironmentalScoreProbe(
        { 1.0f, 1.0f, 1.0f, 0.0f, 0.0f }, 1.0f, 32u, 24000u);
    const auto expandedSlowPaceScore = runEnvironmentalScoreProbe(
        { 0.0f, .40f, 0.0f, .50f, .50f }, 1.0f, 32u, 24000u);
    const auto expandedFastPaceScore = runEnvironmentalScoreProbe(
        { 1.0f, .40f, 0.0f, .50f, .50f }, 1.0f, 32u, 24000u);
    const auto expandedZeroOccupancyScore = runEnvironmentalScoreProbe(
        { .65f, 0.0f, 0.0f, .50f, .50f }, 1.0f, 32u, 24000u);
    const auto expandedFullOccupancyScore = runEnvironmentalScoreProbe(
        { .65f, 1.0f, 0.0f, .50f, .50f }, 1.0f, 32u, 24000u);
    const auto expandedZeroCascadeScore = runEnvironmentalScoreProbe(
        { .65f, .35f, 0.0f, .50f, .50f }, 1.0f, 32u, 24000u);
    const auto expandedFullCascadeScore = runEnvironmentalScoreProbe(
        { .65f, .35f, 1.0f, .50f, .50f }, 1.0f, 32u, 24000u);
    const auto expandedShortRestScore = runEnvironmentalScoreProbe(
        { .35f, .20f, 0.0f, .50f, 0.0f }, 1.0f, 32u, 24000u);
    const auto expandedLongRestScore = runEnvironmentalScoreProbe(
        { .35f, .20f, 0.0f, .50f, 1.0f }, 1.0f, 32u, 24000u);
    const auto expandedNoMemoryScore = runEnvironmentalScoreProbe(
        { .65f, .35f, .40f, 0.0f, .50f }, 1.0f, 32u, 24000u);
    const auto expandedFullMemoryScore = runEnvironmentalScoreProbe(
        { .65f, .35f, .40f, 1.0f, .50f }, 1.0f, 32u, 24000u);
    const auto planetaryModalProbe = runPlanetaryModalProbe();
    const bool scoreDeterministic = scoreProbe.arcs == repeatedScoreProbe.arcs
        && scoreProbe.cascades == repeatedScoreProbe.cascades
        && scoreProbe.consequences == repeatedScoreProbe.consequences
        && scoreProbe.quietSteps == repeatedScoreProbe.quietSteps
        && scoreProbe.maximumActive == repeatedScoreProbe.maximumActive
        && scoreProbe.activeSum == repeatedScoreProbe.activeSum
        && std::fabs(scoreProbe.finalActivity
            - repeatedScoreProbe.finalActivity) < 1.0e-7f;

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
    std::cout << "alien-world preset names: "
              << (alienPresetNamesValid ? "valid" : "INVALID") << "\n";
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
    std::cout << "maximum sampled mid/high periodicity pyro/cryo: "
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
    std::cout << "pressure-jet/singing-lake layer energies: "
              << flamethrowerEncoder.jetLayerEnergy() << " / "
              << singingLakePeakLayer << "\n";
    std::cout << "flamethrower low-band fraction light/heavy body: "
              << lightFlamethrowerLowFraction << " / "
              << heavyFlamethrowerLowFraction << "\n";
    std::cout << "cryosphere low-band fraction light/heavy mass: "
              << lightCryosphereLowFraction << " / "
              << heavyCryosphereLowFraction << "\n";
    std::cout << "sanitized plume-wander floor: "
              << wanderSanitizer.params().vectorRateHz << " Hz\n";
    std::cout << "singing-lake events/modal count and fixed range: "
              << singingLakeEncoder.singingLakeEventCount() << " / "
              << singingLakeEncoder.modalExcitationEventCount() << " / "
              << lakeModalMinimumHz << ".." << lakeModalMaximumHz
              << " Hz\n";
    std::cout << "singing-lake periodicity/low fraction/determinism: "
              << singingLakePeriodicity << " / "
              << singingLakeLowFraction << " / "
              << singingLakeDeterminism << "\n";
    std::cout << "quasicrystal difference/periodicity/low fraction: "
              << quasicrystalDifference << " / "
              << quasicrystalPeriodicity << " / "
              << quasicrystalLowFraction << "\n";
    std::cout << "quasicrystal layer/fronts/avalanches/singing: "
              << quasicrystalPeakLayer << " / "
              << quasicrystalEncoder.quasicrystalFrontEventCount() << " / "
              << quasicrystalEncoder.quasicrystalAvalancheEventCount()
              << " / " << quasicrystalEncoder.singingIceEventCount()
              << "\n";
    std::cout << "quasicrystal modal layer/events: "
              << quasicrystalModalLayer << " / "
              << quasicrystalEncoder.modalExcitationEventCount() << "\n";
    std::cout << "pyrosphere planetary donor differences/layers/events: ";
    for (uint32_t mode = 0u; mode < 4u; ++mode) {
        std::cout << (mode == 0u ? "" : " | ")
                  << pyrosphereAlienDifferences[mode] << "/"
                  << pyrosphereAlienLayers[mode] << "/"
                  << pyrosphereAlienModalLayers[mode] << "/"
                  << pyrosphereAlienNamedEvents[mode] << "/"
                  << pyrosphereAlienModalEvents[mode];
    }
    std::cout << "\n";
    std::cout << "cryosphere planetary donor differences/layers/events: ";
    for (uint32_t mode = 0u; mode < 3u; ++mode) {
        std::cout << (mode == 0u ? "" : " | ")
                  << cryosphereAlienDifferences[mode] << "/"
                  << cryosphereAlienLayers[mode] << "/"
                  << cryosphereAlienModalLayers[mode] << "/"
                  << cryosphereAlienNamedEvents[mode] << "/"
                  << cryosphereAlienModalEvents[mode];
    }
    std::cout << "\n";
    std::cout << "quasicrystal model energy/fronts/avalanches: "
              << quasicrystalModelEnergy << " / "
              << quasicrystalModelFronts << " / "
              << quasicrystalModelAvalanches << "\n";
    std::cout << "planetary modal early/late energy and contracts: "
              << planetaryModalProbe.earlyEnergy << " / "
              << planetaryModalProbe.lateEnergy << " / "
              << planetaryModalProbe.deterministic << " / "
              << planetaryModalProbe.silentBeforeRelease << "\n";
    std::cout << "entity score arcs/cascades/consequences/quiet/max-active: "
              << scoreProbe.arcs << " / " << scoreProbe.cascades << " / "
              << scoreProbe.consequences << " / " << scoreProbe.quietSteps
              << " / " << scoreProbe.maximumActive << "\n";
    std::cout << "expanded score sparse quiet/max vs dense arcs/max: "
              << expandedSparseScore.quietSteps << " / "
              << expandedSparseScore.maximumActive << " / "
              << expandedDenseScore.arcs << " / "
              << expandedDenseScore.maximumActive << "\n";
    std::cout << "expanded score pace slow/fast quiet+arcs: "
              << expandedSlowPaceScore.quietSteps << " / "
              << expandedSlowPaceScore.arcs << " / "
              << expandedFastPaceScore.quietSteps << " / "
              << expandedFastPaceScore.arcs << "\n";
    std::cout << "expanded score occupancy mean low/high and cascade 0/1: "
              << static_cast<double>(expandedZeroOccupancyScore.activeSum)
                    / 24000.0 << " / "
              << static_cast<double>(expandedFullOccupancyScore.activeSum)
                    / 24000.0 << " / "
              << expandedZeroCascadeScore.cascades << " / "
              << expandedFullCascadeScore.cascades << "\n";
    std::cout << "expanded score rest arcs short/long and memory mean 0/1: "
              << expandedShortRestScore.arcs << " / "
              << expandedLongRestScore.arcs << " / "
              << static_cast<double>(expandedNoMemoryScore.activeSum)
                    / 24000.0 << " / "
              << static_cast<double>(expandedFullMemoryScore.activeSum)
                    / 24000.0 << "\n";
    std::cout << "instrument score arcs/cascades/consequences pyro: "
              << structuralPyrosphere.scoreArcCount() << " / "
              << structuralPyrosphere.scoreCascadeCount() << " / "
              << structuralPyrosphere.scoreConsequenceCount() << "\n";
    std::cout << "instrument score arcs/cascades/consequences cryo: "
              << structuralCryosphere.scoreArcCount() << " / "
              << structuralCryosphere.scoreCascadeCount() << " / "
              << structuralCryosphere.scoreConsequenceCount() << "\n";
    return presetMappingsValid && alienPresetNamesValid
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
            && timberFallDifference > 1.5e-3f
            && iceUnderFootDifference > 1.0e-5f
            && structuralPyrosphere.structuralSnapEventCount() > 0u
            && structuralPyrosphere.fallEventCount() > 0u
            && structuralCryosphere.structuralSnapEventCount() > 0u
            && structuralCryosphere.plateFailureEventCount() > 0u
            && densePyrosphere.ignitionEventCount()
                > sparsePyrosphere.ignitionEventCount() + 200u
            && heldCryosphereEvents == 0u
            && denseCryosphereEvents > 50u
            && flamethrowerDifference > 1.0e-4f
            && flamethrowerEncoder.jetLayerEnergy() > 1.0e-8f
            && singingLakeDifference > 1.0e-5f
            && singingLakePeakLayer > 1.0e-9f
            && singingLakeEncoder.singingIceEventCount() > 0u
            && singingLakeEncoder.singingLakeLayerEnergy()
                == singingLakeEncoder.singingIceLayerEnergy()
            && singingLakeEncoder.singingLakeEventCount()
                == singingLakeEncoder.singingIceEventCount()
            && singingLakeEncoder.modalExcitationEventCount()
                == singingLakeEncoder.singingLakeEventCount()
            && singingLakeEncoder.planetaryModalLayerEnergy() == 0.0f
            && singingLakeDeterminism == 0.0f
            && singingLakePeriodicity < 0.24f
            && singingLakeLowFraction < 0.32f
            && quasicrystalDifference > 1.0e-4f
            && quasicrystalPeriodicity < 0.20f
            && quasicrystalLowFraction < 0.32f
            && quasicrystalPeakLayer > 1.0e-8f
            && quasicrystalEncoder.quasicrystalFrontEventCount() > 5u
            && quasicrystalEncoder.quasicrystalAvalancheEventCount() > 5u
            && quasicrystalEncoder.singingIceEventCount() == 0u
            && quasicrystalEncoder.singingIceLayerEnergy() == 0.0f
            && quasicrystalModalLayer > 1.0e-14f
            && quasicrystalEncoder.modalExcitationEventCount()
                == quasicrystalEncoder.quasicrystalFrontEventCount()
                    + quasicrystalEncoder.quasicrystalAvalancheEventCount()
            && pyrosphere.planetaryModalLayerEnergy() == 0.0f
            && cryosphere.planetaryModalLayerEnergy() == 0.0f
            && std::all_of(pyrosphereAlienDifferences.begin(),
                pyrosphereAlienDifferences.end(),
                [](float value) { return value > 1.0e-6f; })
            && std::all_of(pyrosphereAlienDeterminism.begin(),
                pyrosphereAlienDeterminism.end(),
                [](float value) { return value == 0.0f; })
            && std::all_of(pyrosphereAlienLayers.begin(),
                pyrosphereAlienLayers.end(),
                [](float value) { return value > 1.0e-10f; })
            && std::all_of(pyrosphereAlienModalLayers.begin(),
                pyrosphereAlienModalLayers.end(),
                [](float value) { return value > 1.0e-14f; })
            && std::all_of(pyrosphereAlienNamedEvents.begin(),
                pyrosphereAlienNamedEvents.end(),
                [](uint64_t count) { return count > 0u; })
            && pyrosphereAlienNamedEvents
                == pyrosphereAlienAllPlanetaryEvents
            && pyrosphereAlienModalEvents
                == pyrosphereAlienNamedEvents
            && std::all_of(pyrosphereAlienLegacyEvents.begin(),
                pyrosphereAlienLegacyEvents.end(),
                [](uint64_t count) { return count == 0u; })
            && std::all_of(pyrosphereAlienJetLayers.begin(),
                pyrosphereAlienJetLayers.end(),
                [](float value) { return value == 0.0f; })
            && std::all_of(cryosphereAlienDifferences.begin(),
                cryosphereAlienDifferences.end(),
                [](float value) { return value > 1.0e-6f; })
            && std::all_of(cryosphereAlienDeterminism.begin(),
                cryosphereAlienDeterminism.end(),
                [](float value) { return value == 0.0f; })
            && std::all_of(cryosphereAlienLayers.begin(),
                cryosphereAlienLayers.end(),
                [](float value) { return value > 1.0e-10f; })
            && std::all_of(cryosphereAlienModalLayers.begin(),
                cryosphereAlienModalLayers.end(),
                [](float value) { return value > 1.0e-14f; })
            && std::all_of(cryosphereAlienNamedEvents.begin(),
                cryosphereAlienNamedEvents.end(),
                [](uint64_t count) { return count > 0u; })
            && cryosphereAlienNamedEvents
                == cryosphereAlienAllPlanetaryEvents
            && std::equal(cryosphereAlienModalEvents.begin(),
                cryosphereAlienModalEvents.end(),
                cryosphereAlienNamedEvents.begin(),
                [](uint64_t modal, uint64_t primary) {
                    return modal >= primary && primary > 0u;
                })
            && std::all_of(cryosphereAlienLegacyEvents.begin(),
                cryosphereAlienLegacyEvents.end(),
                [](uint64_t count) { return count == 0u; })
            && std::all_of(cryosphereAlienSingingLayers.begin(),
                cryosphereAlienSingingLayers.end(),
                [](float value) { return value == 0.0f; })
            && quasicrystalModelDeterministic
            && quasicrystalModelFinite
            && quasicrystalModelEnergy > 1.0e-6
            && quasicrystalModelFronts > 2u
            && quasicrystalModelAvalanches > 2u
            && planetaryModalProbe.deterministic
            && planetaryModalProbe.finite
            && planetaryModalProbe.silentBeforeRelease
            && planetaryModalProbe.c1Onset
            && planetaryModalProbe.frequencyBounds
            && planetaryModalProbe.decayBounds
            && planetaryModalProbe.resetReplay
            && planetaryModalProbe.silentWithoutFlux
            && planetaryModalProbe.stationaryFrequencies
            && planetaryModalProbe.allProfilesEnergized
            && planetaryModalProbe.earlyEnergy > 1.0e-8
            && planetaryModalProbe.lateEnergy
                < planetaryModalProbe.earlyEnergy * 0.05
            && lakeModalPacketEnergy > 1.0e-8
            && lakeModalMinimumHz >= 180.0f
            && lakeModalMaximumHz > lakeModalMinimumHz * 2.8f
            && lakeModalMaximumHz <= 48000.0f * 0.40f
            && lakeModalFrequenciesStationary
            && lakeModalPacketEnded
            && scoreDeterministic
            && scoreProbe.arcs > 8u
            && scoreProbe.cascades > 2u
            && scoreProbe.consequences > 2u
            && scoreProbe.quietSteps > 0u
            && scoreProbe.maximumActive < 12u
            && expandedSparseScore.quietSteps > 12000u
            && expandedSparseScore.maximumActive <= 1u
            && expandedDenseScore.quietSteps == 0u
            && expandedDenseScore.maximumActive == 32u
            && expandedDenseScore.arcs
                > expandedSparseScore.arcs + 1000u
            && expandedSlowPaceScore.quietSteps > 4800u
            && expandedFastPaceScore.quietSteps == 0u
            && expandedFastPaceScore.arcs
                > expandedSlowPaceScore.arcs * 50u
            && expandedZeroOccupancyScore.maximumActive <= 1u
            && expandedFullOccupancyScore.maximumActive == 32u
            && expandedFullOccupancyScore.activeSum > 22u * 24000u
            && expandedZeroCascadeScore.cascades == 0u
            && expandedFullCascadeScore.cascades > 1000u
            && expandedFullCascadeScore.maximumActive == 32u
            && expandedShortRestScore.arcs
                > expandedLongRestScore.arcs * 3u
            && expandedFullMemoryScore.activeSum
                > expandedNoMemoryScore.activeSum * 6u / 5u
            && structuralPyrosphere.scoreArcCount() > 0u
            && structuralPyrosphere.scoreConsequenceCount() > 0u
            && structuralCryosphere.scoreArcCount() > 0u
            && structuralCryosphere.scoreConsequenceCount() > 0u
            && heavyFlamethrowerLowFraction
                > lightFlamethrowerLowFraction * 1.15f
            && heavyFlamethrowerLowFraction > 0.22f
            && heavyCryosphereLowFraction
                > lightCryosphereLowFraction * 1.15f
            && heavyCryosphereLowFraction > 0.25f
            && heavyCryosphereLowFraction < 0.78f
            && wanderSanitizer.params().vectorRateHz
                >= s3g::kAmbiPyrosphereMinPlumeWanderHz
            && pyrospherePeriodicity < 0.72f
            && cryospherePeriodicity < 0.72f
        ? 0 : 1;
}
