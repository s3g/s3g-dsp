#include "s3g_ambi_cartography_encoder.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

namespace {

constexpr uint32_t kSampleRate = 48000u;
constexpr uint32_t kFrames = 7200u;
constexpr uint32_t kChannels = 4u;

template <size_t Channels>
using MultiAudio = std::array<std::vector<float>, Channels>;

using Audio = MultiAudio<kChannels>;

template <size_t Channels>
MultiAudio<Channels> renderStereo(s3g::AmbiCartographyEncoder& encoder,
    const std::vector<float>& left, const std::vector<float>& right,
    bool segmented = false)
{
    const uint32_t frames = static_cast<uint32_t>(
        std::min(left.size(), right.size()));
    MultiAudio<Channels> output;
    for (auto& channel : output) channel.assign(frames, 0.0f);

    constexpr std::array<uint32_t, 7u> chunks {
        7u, 31u, 113u, 5u, 64u, 19u, 151u };
    uint32_t offset = 0u;
    uint32_t chunkIndex = 0u;
    while (offset < frames) {
        const uint32_t requested = segmented
            ? chunks[chunkIndex++ % chunks.size()] : frames;
        const uint32_t count = std::min(requested, frames - offset);
        std::array<const float*, 2u> input {
            left.data() + offset, right.data() + offset
        };
        std::array<float*, Channels> outputPointers {};
        for (size_t channel = 0u; channel < Channels; ++channel) {
            outputPointers[channel] = output[channel].data() + offset;
        }
        encoder.processBlock(input.data(), outputPointers.data(), 2u,
            static_cast<uint32_t>(Channels), count);
        offset += count;
    }
    return output;
}

Audio renderImpulse(s3g::AmbiCartographyEncoder& encoder,
    uint32_t frames = kFrames)
{
    std::vector<float> left(frames, 0.0f);
    std::vector<float> right(frames, 0.0f);
    left[0] = 1.0f;
    right[0] = 1.0f;
    return renderStereo<kChannels>(encoder, left, right);
}

uint32_t onset(const std::vector<float>& signal, float threshold = 0.01f)
{
    for (uint32_t frame = 0u; frame < signal.size(); ++frame) {
        if (std::fabs(signal[frame]) >= threshold) return frame;
    }
    return static_cast<uint32_t>(signal.size());
}

template <size_t Channels>
double energy(const MultiAudio<Channels>& audio, uint32_t first = 0u,
    uint32_t last = UINT32_MAX)
{
    double result = 0.0;
    for (const auto& channel : audio) {
        const uint32_t end = std::min<uint32_t>(
            last, static_cast<uint32_t>(channel.size()));
        for (uint32_t frame = std::min(first, end); frame < end; ++frame) {
            result += static_cast<double>(channel[frame]) * channel[frame];
        }
    }
    return result;
}

template <size_t Channels>
bool finiteAndBounded(const MultiAudio<Channels>& audio)
{
    for (const auto& channel : audio) {
        for (float sample : channel) {
            if (!std::isfinite(sample) || std::fabs(sample) > 8.0f) {
                return false;
            }
        }
    }
    return true;
}

template <size_t Channels>
double maximumDifference(const MultiAudio<Channels>& first,
    const MultiAudio<Channels>& second)
{
    double difference = 0.0;
    for (size_t channel = 0u; channel < Channels; ++channel) {
        const size_t frames = std::min(
            first[channel].size(), second[channel].size());
        for (size_t frame = 0u; frame < frames; ++frame) {
            difference = std::max(difference,
                static_cast<double>(std::fabs(
                    first[channel][frame] - second[channel][frame])));
        }
    }
    return difference;
}

std::array<std::vector<float>, 2u> deterministicStereo(uint32_t frames)
{
    std::array<std::vector<float>, 2u> signal;
    signal[0].resize(frames);
    signal[1].resize(frames);
    uint32_t state = 0x51f15e5du;
    for (uint32_t frame = 0u; frame < frames; ++frame) {
        state = state * 1664525u + 1013904223u;
        const float first = static_cast<float>((state >> 8u) & 0xffffu)
            / 32767.5f - 1.0f;
        state = state * 1664525u + 1013904223u;
        const float second = static_cast<float>((state >> 8u) & 0xffffu)
            / 32767.5f - 1.0f;
        signal[0][frame] = first * 0.24f;
        signal[1][frame] = second * 0.24f;
    }
    return signal;
}

std::array<std::vector<float>, 2u> stereoTone(uint32_t frames,
    float frequency)
{
    std::array<std::vector<float>, 2u> signal;
    signal[0].resize(frames);
    signal[1].resize(frames);
    for (uint32_t frame = 0u; frame < frames; ++frame) {
        const float sample = 0.30f * std::sin(2.0f * s3g::kPi
            * frequency * static_cast<float>(frame)
            / static_cast<float>(kSampleRate));
        signal[0][frame] = sample;
        signal[1][frame] = sample;
    }
    return signal;
}

void configureLandscapeFixture(s3g::AmbiCartographyEncoder& encoder,
    uint32_t sites = 8u,
    s3g::AmbiCartographyLayout layout = s3g::AmbiCartographyLayout::Grid)
{
    auto params = encoder.params();
    params.activeSites = sites;
    params.selectedSite = 0u;
    params.order = 1u;
    params.layout = layout;
    params.stereoMap = s3g::AmbiCartographyStereoMap::Mono;
    params.timeReference = s3g::AmbiCartographyTimeReference::Relative;
    params.mapScaleMeters = 160.0f;
    params.listenerX = 0.0f;
    params.listenerY = 0.0f;
    params.listenerZ = 0.0f;
    params.networkSpreadMs = 0.0f;
    params.propagationScale = 0.0f;
    params.air = 0.0f;
    params.distanceLoss = 0.0f;
    params.carry = 0.0f;
    params.turbulence = 0.0f;
    params.macroEngine = s3g::AmbiCartographyMacroEngine::Clean;
    params.processMix = 0.0f;
    params.color = 0.62f;
    params.memory = 0.18f;
    params.spread = 0.66f;
    params.skew = 0.0f;
    params.listenMode = s3g::AmbiFieldListenMode::Off;
    params.listenerAmount = 0.0f;
    params.outputGainDb = 0.0f;
    encoder.setParams(params);
    encoder.setLandscapeParams({});
}

void configureSingleSite(s3g::AmbiCartographyEncoder& encoder,
    s3g::AmbiCartographyTimeReference reference,
    float networkTrimMs)
{
    auto params = encoder.params();
    params.activeSites = 1u;
    params.order = 1u;
    params.mapScaleMeters = 34.3f;
    params.propagationScale = 1.0f;
    params.networkSpreadMs = 0.0f;
    params.timeReference = reference;
    params.stereoMap = s3g::AmbiCartographyStereoMap::Mono;
    params.macroEngine = s3g::AmbiCartographyMacroEngine::Clean;
    params.processMix = 0.0f;
    params.air = 0.0f;
    params.distanceLoss = 0.0f;
    params.turbulence = 0.0f;
    params.listenMode = s3g::AmbiFieldListenMode::Off;
    params.listenerAmount = 0.0f;
    params.outputGainDb = 0.0f;
    encoder.setParams(params);

    auto site = encoder.site(0u);
    site.x = 0.0f;
    site.y = 1.0f;
    site.z = 0.0f;
    site.gain = 1.0f;
    site.networkPosition = 0.0f;
    site.networkTrimMs = networkTrimMs;
    site.enabled = true;
    encoder.setSite(0u, site);
}

} // namespace

int main()
{
    s3g::AmbiCartographyEncoderParams extreme {};
    extreme.activeSites = 999u;
    extreme.selectedSite = 999u;
    extreme.order = 99u;
    extreme.mapScaleMeters = 99999.0f;
    extreme.networkSpreadMs = -1.0f;
    extreme.propagationScale = 5.0f;
    extreme.listenerAmount = -2.0f;
    const auto sanitized =
        s3g::sanitizeAmbiCartographyEncoderParams(extreme);
    if (sanitized.activeSites != s3g::kAmbiCartographyMaxSites
        || sanitized.selectedSite != s3g::kAmbiCartographyMaxSites - 1u
        || sanitized.order != s3g::kAmbiCartographyMaxOrder
        || sanitized.mapScaleMeters != 2000.0f
        || sanitized.networkSpreadMs != 0.0f
        || sanitized.propagationScale != 1.0f
        || sanitized.listenerAmount != 0.0f) {
        std::cerr << "Cartography parameter sanitation failed\n";
        return 1;
    }

    s3g::AmbiCartographyLandscapeParams extremeLandscape {};
    extremeLandscape.multipath = 2.0f;
    extremeLandscape.occlusion = -1.0f;
    extremeLandscape.networkWeather = 4.0f;
    extremeLandscape.refraction = -3.0f;
    extremeLandscape.motion = 3.0f;
    extremeLandscape.ecology = -2.0f;
    extremeLandscape.horizon = 8.0f;
    const auto sanitizedLandscape =
        s3g::sanitizeAmbiCartographyLandscapeParams(extremeLandscape);
    if (sanitizedLandscape.multipath != 1.0f
        || sanitizedLandscape.occlusion != 0.0f
        || sanitizedLandscape.networkWeather != 1.0f
        || sanitizedLandscape.refraction != -1.0f
        || sanitizedLandscape.motion != 1.0f
        || sanitizedLandscape.ecology != 0.0f
        || sanitizedLandscape.horizon != 1.0f) {
        std::cerr << "Cartography landscape sanitation failed\n";
        return 1;
    }

    s3g::AmbiCartographySiteProcessOptions extremeSiteProcess {};
    extremeSiteProcess.shredCircuit =
        static_cast<s3g::MacroShredCircuit>(999u);
    extremeSiteProcess.fractureProcessor =
        static_cast<s3g::FractureProcessor>(999u);
    const auto sanitizedSiteProcess =
        s3g::sanitizeAmbiCartographySiteProcessOptions(extremeSiteProcess);
    if (sanitizedSiteProcess.shredCircuit
            != s3g::MacroShredCircuit::Diode
        || sanitizedSiteProcess.fractureProcessor
            != s3g::FractureProcessor::OctStack) {
        std::cerr << "Cartography site-process sanitation failed\n";
        return 1;
    }

    s3g::AmbiCartographyEncoder encoder;
    encoder.prepare(kSampleRate);

    // The map editor uses conventional plan axes (+X screen-right,
    // +Y toward the top/front). The encoder must convert those into the
    // repository-wide ambisonic frame (+X front, +Y listener-left, +Z up).
    {
        s3g::AmbiCartographyEncoder coordinateEncoder;
        coordinateEncoder.prepare(kSampleRate);
        configureSingleSite(coordinateEncoder,
            s3g::AmbiCartographyTimeReference::Relative, 0.0f);
        coordinateEncoder.reset();
        (void)renderImpulse(coordinateEncoder, 32u);
        const auto front = coordinateEncoder.siteDirection(0u);

        auto site = coordinateEncoder.site(0u);
        site.x = 1.0f;
        site.y = 0.0f;
        coordinateEncoder.setSite(0u, site);
        coordinateEncoder.reset();
        (void)renderImpulse(coordinateEncoder, 32u);
        const auto right = coordinateEncoder.siteDirection(0u);

        site.x = 0.0f;
        site.z = 1.0f;
        coordinateEncoder.setSite(0u, site);
        coordinateEncoder.reset();
        (void)renderImpulse(coordinateEncoder, 32u);
        const auto up = coordinateEncoder.siteDirection(0u);
        if (front.x < 0.999f || std::fabs(front.y) > 0.001f
            || right.y > -0.999f || std::fabs(right.x) > 0.001f
            || up.z < 0.999f) {
            std::cerr << "Cartography map-to-ambisonic coordinate conversion failed\n";
            return 1;
        }
    }

    configureSingleSite(encoder,
        s3g::AmbiCartographyTimeReference::Absolute, 0.0f);
    encoder.reset();
    const auto absolute = renderImpulse(encoder);
    const uint32_t absoluteOnset = onset(absolute[0]);
    if (absoluteOnset < 4798u || absoluteOnset > 4802u
        || !finiteAndBounded(absolute) || energy(absolute) < 0.5) {
        std::cerr << "Absolute acoustic arrival failed: onset="
                  << absoluteOnset << " energy=" << energy(absolute) << "\n";
        return 1;
    }
    if (std::fabs(encoder.siteArrivalSeconds(0u) - 0.1f) > 0.0002f) {
        std::cerr << "Absolute arrival telemetry failed: "
                  << encoder.siteArrivalSeconds(0u) << "\n";
        return 1;
    }

    configureSingleSite(encoder,
        s3g::AmbiCartographyTimeReference::Relative, 0.0f);
    encoder.reset();
    const auto relative = renderImpulse(encoder);
    const uint32_t relativeOnset = onset(relative[0]);
    if (relativeOnset != 0u || energy(relative) < 0.5) {
        std::cerr << "Relative time reference failed: onset="
                  << relativeOnset << " energy=" << energy(relative) << "\n";
        return 1;
    }

    configureSingleSite(encoder,
        s3g::AmbiCartographyTimeReference::Relative, 100.0f);
    encoder.reset();
    const auto network = renderImpulse(encoder);
    const uint32_t networkOnset = onset(network[0]);
    if (networkOnset < 4798u || networkOnset > 4802u
        || std::fabs(encoder.siteArrivalSeconds(0u) - 0.2f) > 0.0002f) {
        std::cerr << "Infrastructure delay failed: onset=" << networkOnset
                  << " arrival=" << encoder.siteArrivalSeconds(0u) << "\n";
        return 1;
    }

    auto params = encoder.params();
    params.activeSites = 8u;
    params.layout = s3g::AmbiCartographyLayout::Waterfront;
    params.order = 1u;
    params.mapScaleMeters = 120.0f;
    params.propagationScale = 0.0f;
    params.networkSpreadMs = 0.0f;
    params.timeReference = s3g::AmbiCartographyTimeReference::Relative;
    params.macroEngine = s3g::AmbiCartographyMacroEngine::Delay;
    params.processMix = 0.55f;
    params.macro = 0.20f;
    params.memory = 0.25f;
    params.air = 0.0f;
    params.distanceLoss = 0.0f;
    params.turbulence = 0.0f;
    params.listenMode = s3g::AmbiFieldListenMode::Off;
    params.listenerAmount = 0.0f;
    params.outputGainDb = -6.0f;
    encoder.setParams(params);
    params = encoder.params();
    encoder.reset();
    const auto listenerReference = renderImpulse(encoder, 4096u);

    params.listenerAmount = 1.0f;
    encoder.setParams(params);
    encoder.reset();
    const auto listenerOff = renderImpulse(encoder, 4096u);
    double difference = 0.0;
    for (uint32_t channel = 0u; channel < kChannels; ++channel) {
        for (uint32_t frame = 0u; frame < 4096u; ++frame) {
            difference += std::fabs(listenerReference[channel][frame]
                - listenerOff[channel][frame]);
        }
    }
    if (difference > 0.00001) {
        std::cerr << "LISTEN OFF changed the open-loop render: "
                  << difference << "\n";
        return 1;
    }

    for (uint32_t engine = 0u; engine <= 7u; ++engine) {
        params.macroEngine = static_cast<s3g::AmbiCartographyMacroEngine>(engine);
        params.listenMode = s3g::AmbiFieldListenMode::Follow;
        params.listenerAmount = 0.65f;
        params.processMix = engine == 0u ? 0.0f : 0.65f;
        encoder.setParams(params);
        encoder.reset();
        const auto result = renderImpulse(encoder, 8192u);
        if (!finiteAndBounded(result) || energy(result) < 0.000001) {
            std::cerr << "Macro engine stability failed for engine "
                      << engine << " energy=" << energy(result) << "\n";
            return 1;
        }
    }

    // The three cartography-specific processors must each create a distinct,
    // bounded site treatment while continuing to use the common macro axes.
    {
        const auto signal = deterministicStereo(16384u);
        s3g::AmbiCartographyEncoder processEncoder;
        processEncoder.prepare(kSampleRate);
        configureLandscapeFixture(processEncoder, 6u,
            s3g::AmbiCartographyLayout::Corridor);
        auto processParams = processEncoder.params();
        processParams.macro = 0.78f;
        processParams.color = 0.64f;
        processParams.memory = 0.56f;
        processParams.spread = 0.71f;
        processParams.deviation = 0.31f;
        processParams.skew = -0.24f;
        processParams.center = 0.62f;
        processParams.processMix = 0.88f;
        processParams.outputGainDb = -9.0f;

        std::array<Audio, 3u> renders;
        for (uint32_t engine = 5u; engine <= 7u; ++engine) {
            processParams.macroEngine =
                static_cast<s3g::AmbiCartographyMacroEngine>(engine);
            processEncoder.setParams(processParams);
            processEncoder.reset();
            renders[engine - 5u] = renderStereo<kChannels>(processEncoder,
                signal[0], signal[1], true);
            processEncoder.reset();
            const auto contiguous = renderStereo<kChannels>(processEncoder,
                signal[0], signal[1]);
            if (!finiteAndBounded(renders[engine - 5u])
                || energy(renders[engine - 5u]) < 0.000001
                || maximumDifference(
                    renders[engine - 5u], contiguous) > 0.000001) {
                std::cerr << "Cartography site processor failed for engine "
                          << engine << "\n";
                return 1;
            }
        }
        if (maximumDifference(renders[0], renders[1]) < 0.00001
            || maximumDifference(renders[0], renders[2]) < 0.00001
            || maximumDifference(renders[1], renders[2]) < 0.00001) {
            std::cerr << "Cartography site processors were not distinct\n";
            return 1;
        }

        processParams.macroEngine =
            s3g::AmbiCartographyMacroEngine::Clean;
        processParams.processMix = 0.0f;
        processEncoder.setParams(processParams);
        processEncoder.reset();
        const auto dryReference = renderStereo<kChannels>(processEncoder,
            signal[0], signal[1], true);
        for (uint32_t engine = 5u; engine <= 7u; ++engine) {
            processParams.macroEngine =
                static_cast<s3g::AmbiCartographyMacroEngine>(engine);
            processEncoder.setParams(processParams);
            processEncoder.reset();
            const auto bypassed = renderStereo<kChannels>(processEncoder,
                signal[0], signal[1], true);
            if (maximumDifference(dryReference, bypassed) != 0.0) {
                std::cerr << "Site Process Mix zero bypass failed for engine "
                          << engine << "\n";
                return 1;
            }
        }
    }

    // Body history belongs to a physical site, while RANGE only voices that
    // site's controls. Crossing two range ranks must not exchange their
    // resonator states at the geometry update boundary.
    {
        s3g::AmbiCartographyEncoder movedEncoder;
        s3g::AmbiCartographyEncoder controlEncoder;
        movedEncoder.prepare(kSampleRate);
        controlEncoder.prepare(kSampleRate);
        const auto configureBodyRange = [](auto& target) {
            auto params = target.params();
            params.activeSites = 2u;
            params.order = 1u;
            params.layout = s3g::AmbiCartographyLayout::Radial;
            params.stereoMap = s3g::AmbiCartographyStereoMap::Alternate;
            params.timeReference =
                s3g::AmbiCartographyTimeReference::Relative;
            params.listenerX = -0.02f;
            params.listenerY = 0.0f;
            params.listenerZ = 0.0f;
            params.networkSpreadMs = 0.0f;
            params.propagationScale = 0.0f;
            params.air = 0.0f;
            params.distanceLoss = 0.0f;
            params.turbulence = 0.0f;
            params.macroEngine =
                s3g::AmbiCartographyMacroEngine::SpeakerBody;
            params.macroMetric =
                s3g::AmbiCartographyMacroMetric::Range;
            params.macro = 0.78f;
            params.color = 0.68f;
            params.memory = 0.48f;
            params.spread = 1.0f;
            params.deviation = 0.10f;
            params.skew = 0.16f;
            params.center = 0.62f;
            params.processMix = 1.0f;
            params.listenMode = s3g::AmbiFieldListenMode::Off;
            params.listenerAmount = 0.0f;
            params.outputGainDb = 0.0f;
            target.setParams(params);
            auto leftSite = target.site(0u);
            leftSite.x = -0.50f;
            leftSite.y = 0.0f;
            leftSite.z = 0.0f;
            leftSite.gain = 1.0f;
            leftSite.networkPosition = 0.0f;
            leftSite.networkTrimMs = 0.0f;
            leftSite.enabled = true;
            target.setSite(0u, leftSite);
            auto rightSite = target.site(1u);
            rightSite.x = 0.50f;
            rightSite.y = 0.0f;
            rightSite.z = 0.0f;
            rightSite.gain = 1.0f;
            rightSite.networkPosition = 1.0f;
            rightSite.networkTrimMs = 0.0f;
            rightSite.enabled = true;
            target.setSite(1u, rightSite);
            target.setLandscapeParams({});
            target.reset();
        };
        configureBodyRange(movedEncoder);
        configureBodyRange(controlEncoder);

        constexpr uint32_t warmFrames = 96000u;
        constexpr uint32_t inspectFrames = 2048u;
        std::vector<float> warmLeft(warmFrames);
        std::vector<float> warmRight(warmFrames);
        std::vector<float> inspectLeft(inspectFrames);
        std::vector<float> inspectRight(inspectFrames);
        const auto fillSignal = [](auto& left, auto& right,
                                   uint32_t startFrame) {
            for (uint32_t frame = 0u; frame < left.size(); ++frame) {
                const float absolute = static_cast<float>(startFrame + frame);
                left[frame] = 0.23f * std::sin(2.0f * s3g::kPi
                    * 997.0f * absolute / static_cast<float>(kSampleRate));
                right[frame] = 0.19f * std::sin(2.0f * s3g::kPi
                    * 233.0f * absolute / static_cast<float>(kSampleRate)
                    + 0.37f);
            }
        };
        fillSignal(warmLeft, warmRight, 0u);
        fillSignal(inspectLeft, inspectRight, warmFrames);
        renderStereo<kChannels>(
            movedEncoder, warmLeft, warmRight, true);
        renderStereo<kChannels>(
            controlEncoder, warmLeft, warmRight, true);
        auto movedParams = movedEncoder.params();
        movedParams.listenerX = 0.02f;
        movedEncoder.setParams(movedParams);
        const auto moved = renderStereo<kChannels>(
            movedEncoder, inspectLeft, inspectRight, true);
        const auto control = renderStereo<kChannels>(
            controlEncoder, inspectLeft, inspectRight, true);
        const double boundaryDifference = std::fabs(
            moved[0].front() - control[0].front());
        double wDifference = 0.0;
        for (uint32_t frame = 0u; frame < inspectFrames; ++frame) {
            wDifference = std::max(wDifference,
                static_cast<double>(std::fabs(
                    moved[0][frame] - control[0][frame])));
        }
        if (!finiteAndBounded(moved) || !finiteAndBounded(control)
            || boundaryDifference > 0.0005
            || wDifference < 0.00001) {
            std::cerr << "Body site-identity continuity failed: boundary="
                      << boundaryDifference << " effect="
                      << wDifference << "\n";
            return 1;
        }
    }

    // Cross-lane processors must compact the enabled sites rather than route
    // through the empty gaps in the legacy 24-lane ordinal map. Site zero is
    // the only driven source but has zero output gain; all rendered energy
    // therefore proves that the second site received the transfer.
    {
        constexpr uint32_t sparseFrames = 8192u;
        std::vector<float> left(sparseFrames, 0.0f);
        std::vector<float> right(sparseFrames, 0.0f);
        for (uint32_t frame = 0u; frame < sparseFrames; ++frame) {
            left[frame] = 0.30f * std::sin(2.0f * s3g::kPi * 440.0f
                * static_cast<float>(frame)
                / static_cast<float>(kSampleRate));
        }

        for (const auto engine : {
                s3g::AmbiCartographyMacroEngine::SpectralRelay,
                s3g::AmbiCartographyMacroEngine::RelayBuffer }) {
            s3g::AmbiCartographyEncoder sparseEncoder;
            sparseEncoder.prepare(kSampleRate);
            auto sparseParams = sparseEncoder.params();
            sparseParams.activeSites = 2u;
            sparseParams.order = 1u;
            sparseParams.layout = s3g::AmbiCartographyLayout::Corridor;
            sparseParams.stereoMap =
                s3g::AmbiCartographyStereoMap::Alternate;
            sparseParams.timeReference =
                s3g::AmbiCartographyTimeReference::Relative;
            sparseParams.networkSpreadMs = 0.0f;
            sparseParams.propagationScale = 0.0f;
            sparseParams.air = 0.0f;
            sparseParams.distanceLoss = 0.0f;
            sparseParams.turbulence = 0.0f;
            sparseParams.macroEngine = engine;
            sparseParams.macro = 1.0f;
            sparseParams.color = engine
                    == s3g::AmbiCartographyMacroEngine::RelayBuffer
                ? 0.0f : 0.62f;
            sparseParams.memory = 1.0f;
            sparseParams.spread = 0.0f;
            sparseParams.deviation = 0.0f;
            sparseParams.skew = -1.0f;
            sparseParams.center = 0.5f;
            sparseParams.processMix = 1.0f;
            sparseParams.listenMode = s3g::AmbiFieldListenMode::Off;
            sparseParams.listenerAmount = 0.0f;
            sparseParams.outputGainDb = 0.0f;
            sparseEncoder.setParams(sparseParams);
            auto source = sparseEncoder.site(0u);
            source.gain = 0.0f;
            source.enabled = true;
            source.networkPosition = 0.0f;
            sparseEncoder.setSite(0u, source);
            auto destination = sparseEncoder.site(1u);
            destination.gain = 1.0f;
            destination.enabled = true;
            destination.networkPosition = 1.0f;
            sparseEncoder.setSite(1u, destination);
            sparseEncoder.reset();
            const auto transferred = renderStereo<kChannels>(
                sparseEncoder, left, right, true);
            if (!finiteAndBounded(transferred)
                || energy(transferred, 1024u) < 0.001) {
                std::cerr << "Sparse cross-lane transfer failed for engine "
                          << static_cast<uint32_t>(engine) << ": energy="
                          << energy(transferred, 1024u) << "\n";
                return 1;
            }
        }
    }

    // Removing a zero-gain member still changes the compact relationship
    // topology. Stateful mapped processors must hold their own last audible
    // sample at that boundary, then release toward the new topology without
    // creating a second edge when the 30 ms correction completes.
    {
        constexpr uint32_t warmFrames = 96000u;
        constexpr uint32_t inspectFrames = 2048u;
        constexpr uint32_t releaseInspectFirst = 1360u;
        constexpr uint32_t releaseInspectLast = 1520u;
        constexpr double boundaryLimit = 0.00001;
        constexpr double stepTolerance = 0.0005;

        std::vector<float> warmLeft(warmFrames);
        std::vector<float> warmRight(warmFrames);
        std::vector<float> inspectLeft(inspectFrames);
        std::vector<float> inspectRight(inspectFrames);
        const auto fillSignal = [](auto& left, auto& right,
                                   uint32_t startFrame) {
            for (uint32_t frame = 0u; frame < left.size(); ++frame) {
                const float absolute = static_cast<float>(startFrame + frame);
                left[frame] = 0.23f * std::sin(2.0f * s3g::kPi
                    * 997.0f * absolute / static_cast<float>(kSampleRate));
                right[frame] = 0.19f * std::sin(2.0f * s3g::kPi
                    * 233.0f * absolute / static_cast<float>(kSampleRate)
                    + 0.37f);
            }
        };
        fillSignal(warmLeft, warmRight, 0u);
        fillSignal(inspectLeft, inspectRight, warmFrames);

        for (const auto engine : {
                s3g::AmbiCartographyMacroEngine::SpectralRelay,
                s3g::AmbiCartographyMacroEngine::RelayBuffer }) {
            s3g::AmbiCartographyEncoder changedEncoder;
            s3g::AmbiCartographyEncoder controlEncoder;
            changedEncoder.prepare(kSampleRate);
            controlEncoder.prepare(kSampleRate);
            const auto configure = [engine](auto& target) {
                auto params = target.params();
                params.activeSites = 3u;
                params.order = 1u;
                params.layout = s3g::AmbiCartographyLayout::Radial;
                params.stereoMap =
                    s3g::AmbiCartographyStereoMap::Alternate;
                params.timeReference =
                    s3g::AmbiCartographyTimeReference::Relative;
                params.mapScaleMeters = 1.0f;
                params.networkSpreadMs = 0.0f;
                params.propagationScale = 0.0f;
                params.air = 0.0f;
                params.distanceLoss = 0.0f;
                params.carry = 0.0f;
                params.turbulence = 0.0f;
                params.macroEngine = engine;
                params.macroMetric =
                    s3g::AmbiCartographyMacroMetric::Network;
                params.macro = 0.78f;
                params.color = 0.68f;
                params.memory = 0.48f;
                params.spread = 0.66f;
                params.deviation = 0.10f;
                params.skew = 0.16f;
                params.center = 0.62f;
                params.processMix = 1.0f;
                params.listenMode = s3g::AmbiFieldListenMode::Off;
                params.listenerAmount = 0.0f;
                params.outputGainDb = 0.0f;
                target.setParams(params);
                target.setLandscapeParams({});
                for (uint32_t siteIndex = 0u;
                    siteIndex < 3u; ++siteIndex) {
                    auto site = target.site(siteIndex);
                    site.x = -0.50f
                        + static_cast<float>(siteIndex) * 0.50f;
                    site.y = 0.0f;
                    site.z = 0.0f;
                    site.gain = siteIndex == 2u ? 0.0f : 1.0f;
                    site.networkPosition =
                        static_cast<float>(siteIndex) * 0.50f;
                    site.networkTrimMs = 0.0f;
                    site.enabled = true;
                    target.setSite(siteIndex, site);
                }
                target.reset();
            };
            configure(changedEncoder);
            configure(controlEncoder);

            const auto changedWarm = renderStereo<kChannels>(
                changedEncoder, warmLeft, warmRight, true);
            const auto controlWarm = renderStereo<kChannels>(
                controlEncoder, warmLeft, warmRight, true);
            auto silentSite = changedEncoder.site(2u);
            silentSite.enabled = false;
            changedEncoder.setSite(2u, silentSite);
            const auto changed = renderStereo<kChannels>(
                changedEncoder, inspectLeft, inspectRight, true);
            const auto control = renderStereo<kChannels>(
                controlEncoder, inspectLeft, inspectRight, true);

            double boundaryStep = 0.0;
            double maximumStep = 0.0;
            double controlMaximumStep = 0.0;
            double releaseMaximumStep = 0.0;
            double releaseControlMaximumStep = 0.0;
            for (uint32_t channel = 0u; channel < kChannels; ++channel) {
                float previousChanged = changedWarm[channel].back();
                float previousControl = controlWarm[channel].back();
                for (uint32_t frame = 0u;
                    frame < inspectFrames; ++frame) {
                    const double changedStep = std::fabs(
                        changed[channel][frame] - previousChanged);
                    const double controlStep = std::fabs(
                        control[channel][frame] - previousControl);
                    if (frame == 0u) {
                        boundaryStep = std::max(
                            boundaryStep, changedStep);
                    }
                    maximumStep = std::max(maximumStep, changedStep);
                    controlMaximumStep = std::max(
                        controlMaximumStep, controlStep);
                    if (frame >= releaseInspectFirst
                        && frame <= releaseInspectLast) {
                        releaseMaximumStep = std::max(
                            releaseMaximumStep, changedStep);
                        releaseControlMaximumStep = std::max(
                            releaseControlMaximumStep, controlStep);
                    }
                    previousChanged = changed[channel][frame];
                    previousControl = control[channel][frame];
                }
            }
            const double topologyDifference = maximumDifference(
                changed, control);
            if (!finiteAndBounded(changed) || !finiteAndBounded(control)
                || maximumDifference(changedWarm, controlWarm) > 0.000001
                || boundaryStep > boundaryLimit
                || maximumStep > controlMaximumStep + stepTolerance
                || releaseMaximumStep
                    > releaseControlMaximumStep + stepTolerance
                || topologyDifference < 0.0001) {
                std::cerr << "Mapped topology continuity failed for engine "
                          << static_cast<uint32_t>(engine)
                          << ": boundary=" << boundaryStep
                          << " step=" << maximumStep << "/"
                          << controlMaximumStep << " release="
                          << releaseMaximumStep << "/"
                          << releaseControlMaximumStep << " effect="
                          << topologyDifference << "\n";
                return 1;
            }
        }
    }

    // A live engine change must leave the previous wet signal in place at the
    // boundary and crossfade it toward the new engine. A settled Body fed DC
    // is near silence, while Clean is the original constant feed, making an
    // immediate wet-to-dry switch unambiguously visible as a large step.
    {
        s3g::AmbiCartographyEncoder switchEncoder;
        switchEncoder.prepare(kSampleRate);
        configureLandscapeFixture(switchEncoder, 1u,
            s3g::AmbiCartographyLayout::Radial);
        auto switchParams = switchEncoder.params();
        switchParams.macroEngine =
            s3g::AmbiCartographyMacroEngine::SpeakerBody;
        switchParams.macro = 1.0f;
        switchParams.color = 0.35f;
        switchParams.memory = 0.70f;
        switchParams.processMix = 1.0f;
        switchParams.outputGainDb = 0.0f;
        switchEncoder.setParams(switchParams);
        switchEncoder.reset();
        std::vector<float> constant(4096u, 0.25f);
        const auto before = renderStereo<kChannels>(
            switchEncoder, constant, constant);
        switchParams.macroEngine =
            s3g::AmbiCartographyMacroEngine::Clean;
        switchEncoder.setParams(switchParams);
        std::vector<float> afterInput(2048u, 0.25f);
        const auto after = renderStereo<kChannels>(
            switchEncoder, afterInput, afterInput);
        const double boundaryStep = std::fabs(
            after[0].front() - before[0].back());
        double maximumStep = boundaryStep;
        for (size_t frame = 1u; frame < after[0].size(); ++frame) {
            maximumStep = std::max(maximumStep,
                static_cast<double>(std::fabs(
                    after[0][frame] - after[0][frame - 1u])));
        }
        if (!finiteAndBounded(after) || maximumStep > 0.02
            || std::fabs(after[0].back()) < 0.20f) {
            std::cerr << "Live Site Process crossfade failed: step="
                      << maximumStep << " settled="
                      << after[0].back() << "\n";
            return 1;
        }
    }

    // Retargeting at either side of the transition midpoint must not collapse
    // the current blend to one of its endpoints. The newest target is queued,
    // so Body -> Clean completes before the return to Body begins.
    {
        constexpr uint32_t transitionFrames = 1440u;
        constexpr std::array<uint32_t, 4u> retargetFrames {
            360u, 706u, 734u, 1080u };
        for (const uint32_t retargetFrame : retargetFrames) {
            s3g::AmbiCartographyEncoder retargetEncoder;
            retargetEncoder.prepare(kSampleRate);
            configureLandscapeFixture(retargetEncoder, 1u,
                s3g::AmbiCartographyLayout::Radial);
            auto retargetParams = retargetEncoder.params();
            retargetParams.macroEngine =
                s3g::AmbiCartographyMacroEngine::SpeakerBody;
            retargetParams.macro = 1.0f;
            retargetParams.color = 0.35f;
            retargetParams.memory = 0.70f;
            retargetParams.processMix = 1.0f;
            retargetParams.outputGainDb = 0.0f;
            retargetEncoder.setParams(retargetParams);
            retargetEncoder.reset();

            std::vector<float> settleInput(4096u, 0.25f);
            const auto settled = renderStereo<kChannels>(
                retargetEncoder, settleInput, settleInput);
            retargetParams.macroEngine =
                s3g::AmbiCartographyMacroEngine::Clean;
            retargetEncoder.setParams(retargetParams);
            std::vector<float> firstInput(retargetFrame, 0.25f);
            const auto first = renderStereo<kChannels>(
                retargetEncoder, firstInput, firstInput);

            retargetParams.macroEngine =
                s3g::AmbiCartographyMacroEngine::SpeakerBody;
            retargetEncoder.setParams(retargetParams);
            const uint32_t secondFrames = transitionFrames - retargetFrame
                + transitionFrames + 256u;
            std::vector<float> secondInput(secondFrames, 0.25f);
            const auto second = renderStereo<kChannels>(
                retargetEncoder, secondInput, secondInput);

            double maximumStep = 0.0;
            double maximumLevel = 0.0;
            float previous = settled[0].back();
            const auto inspect = [&](const Audio& audio) {
                for (const float sample : audio[0]) {
                    maximumStep = std::max(maximumStep,
                        static_cast<double>(std::fabs(sample - previous)));
                    maximumLevel = std::max(maximumLevel,
                        static_cast<double>(std::fabs(sample)));
                    previous = sample;
                }
            };
            inspect(first);
            inspect(second);
            const double returnError = std::fabs(
                second[0].back() - settled[0].back());
            if (!finiteAndBounded(first) || !finiteAndBounded(second)
                || maximumStep > 0.02 || maximumLevel < 0.20
                || returnError > 0.05) {
                std::cerr << "Queued Site Process retarget failed at frame "
                          << retargetFrame << ": step=" << maximumStep
                          << " peak=" << maximumLevel
                          << " return=" << returnError << "\n";
                return 1;
            }
        }
    }

    // Multiple host events can be published before the next audio frame. An
    // unseen intermediate engine at blend zero must be replaced by the latest
    // target, including cancellation back to the still-audible endpoint.
    {
        constexpr std::array<s3g::AmbiCartographyMacroEngine, 2u>
            immediateTargets {
                s3g::AmbiCartographyMacroEngine::Delay,
                s3g::AmbiCartographyMacroEngine::SpeakerBody
            };
        for (const auto immediateTarget : immediateTargets) {
            s3g::AmbiCartographyEncoder immediateEncoder;
            s3g::AmbiCartographyEncoder directEncoder;
            immediateEncoder.prepare(kSampleRate);
            directEncoder.prepare(kSampleRate);
            configureLandscapeFixture(immediateEncoder, 1u,
                s3g::AmbiCartographyLayout::Radial);
            configureLandscapeFixture(directEncoder, 1u,
                s3g::AmbiCartographyLayout::Radial);
            auto bodyParams = immediateEncoder.params();
            bodyParams.macroEngine =
                s3g::AmbiCartographyMacroEngine::SpeakerBody;
            bodyParams.macro = 1.0f;
            bodyParams.color = 0.35f;
            bodyParams.memory = 0.70f;
            bodyParams.processMix = 1.0f;
            bodyParams.outputGainDb = 0.0f;
            immediateEncoder.setParams(bodyParams);
            directEncoder.setParams(bodyParams);
            immediateEncoder.reset();
            directEncoder.reset();

            std::vector<float> settleInput(4096u, 0.25f);
            renderStereo<kChannels>(
                immediateEncoder, settleInput, settleInput);
            renderStereo<kChannels>(directEncoder, settleInput, settleInput);

            auto eventParams = immediateEncoder.params();
            eventParams.macroEngine =
                s3g::AmbiCartographyMacroEngine::Clean;
            immediateEncoder.setParams(eventParams);
            eventParams.macroEngine = immediateTarget;
            immediateEncoder.setParams(eventParams);
            auto directParams = directEncoder.params();
            directParams.macroEngine = immediateTarget;
            directEncoder.setParams(directParams);

            std::vector<float> renderInput(2048u, 0.25f);
            const auto immediate = renderStereo<kChannels>(
                immediateEncoder, renderInput, renderInput);
            const auto direct = renderStereo<kChannels>(
                directEncoder, renderInput, renderInput);
            const double difference = maximumDifference(immediate, direct);
            if (!finiteAndBounded(immediate) || !finiteAndBounded(direct)
                || difference > 1.0e-6) {
                std::cerr << "Unrendered Site Process target was retained for "
                          << static_cast<uint32_t>(immediateTarget)
                          << ": difference=" << difference << "\n";
                return 1;
            }
        }
    }

    // Every Shred circuit is selectable in the embedded site processor and
    // changes its deterministic broadband response.
    {
        const auto signal = deterministicStereo(12288u);
        s3g::AmbiCartographyEncoder shredEncoder;
        shredEncoder.prepare(kSampleRate);
        configureLandscapeFixture(shredEncoder, 4u);
        auto shredParams = shredEncoder.params();
        shredParams.macroEngine = s3g::AmbiCartographyMacroEngine::Shred;
        shredParams.macro = 0.82f;
        shredParams.color = 0.67f;
        shredParams.memory = 0.38f;
        shredParams.spread = 0.74f;
        shredParams.deviation = 0.29f;
        shredParams.processMix = 0.90f;
        shredParams.outputGainDb = -12.0f;
        shredEncoder.setParams(shredParams);

        Audio reference;
        for (uint32_t circuit = 0u;
            circuit < s3g::kMacroShredCircuitCount; ++circuit) {
            auto options = shredEncoder.siteProcessOptions();
            options.shredCircuit =
                static_cast<s3g::MacroShredCircuit>(circuit);
            shredEncoder.setSiteProcessOptions(options);
            shredEncoder.reset();
            const auto rendered = renderStereo<kChannels>(shredEncoder,
                signal[0], signal[1], true);
            if (!finiteAndBounded(rendered) || energy(rendered) < 0.000001) {
                std::cerr << "Embedded Shred circuit failed: "
                          << circuit << "\n";
                return 1;
            }
            if (circuit == 0u) {
                reference = rendered;
            } else if (maximumDifference(reference, rendered) < 0.00001) {
                std::cerr << "Embedded Shred circuit was not distinct: "
                          << circuit << "\n";
                return 1;
            }
        }
    }

    // Every Fracture processor is selectable and remains finite under the
    // same common macro-control fixture.
    {
        const auto signal = deterministicStereo(12288u);
        s3g::AmbiCartographyEncoder fractureEncoder;
        fractureEncoder.prepare(kSampleRate);
        configureLandscapeFixture(fractureEncoder, 4u);
        auto fractureParams = fractureEncoder.params();
        fractureParams.macroEngine =
            s3g::AmbiCartographyMacroEngine::Fracture;
        fractureParams.macro = 0.79f;
        fractureParams.color = 0.61f;
        fractureParams.memory = 0.34f;
        fractureParams.spread = 0.76f;
        fractureParams.deviation = 0.36f;
        fractureParams.skew = 0.27f;
        fractureParams.processMix = 0.90f;
        fractureParams.outputGainDb = -12.0f;
        fractureEncoder.setParams(fractureParams);

        Audio reference;
        for (uint32_t processor = 0u;
            processor < s3g::kFractureProcessorCount; ++processor) {
            auto options = fractureEncoder.siteProcessOptions();
            options.fractureProcessor =
                static_cast<s3g::FractureProcessor>(processor);
            fractureEncoder.setSiteProcessOptions(options);
            fractureEncoder.reset();
            const auto rendered = renderStereo<kChannels>(fractureEncoder,
                signal[0], signal[1], true);
            if (!finiteAndBounded(rendered) || energy(rendered) < 0.000001) {
                std::cerr << "Embedded Fracture processor failed: "
                          << processor << "\n";
                return 1;
            }
            if (processor == 0u) {
                reference = rendered;
            } else if (maximumDifference(reference, rendered) < 0.00001) {
                std::cerr << "Embedded Fracture processor was not distinct: "
                          << processor << "\n";
                return 1;
            }
        }
    }

    // Fracture Logic receives its second operand from the next enabled site
    // in relationship order. Cartography deliberately spreads a small site
    // population across the complete 24-lane macro range, so looking at the
    // numerically adjacent lane would read an empty phantom lane instead.
    // Make only site zero audible, then remove its next ranked operand. With
    // correct routing its operand changes from the inverted right input to
    // the correlated left input; phantom-lane routing would render both
    // configurations identically.
    {
        constexpr uint32_t frames = 24000u;
        constexpr uint32_t inspectFirst = 4096u;
        std::vector<float> left(frames);
        std::vector<float> right(frames);
        for (uint32_t frame = 0u; frame < frames; ++frame) {
            const float phase = 2.0f * s3g::kPi * 181.0f
                * static_cast<float>(frame)
                / static_cast<float>(kSampleRate);
            left[frame] = 0.24f * std::sin(phase);
            right[frame] = -left[frame];
        }

        const auto configureLogic = [](auto& encoder,
                                        bool middleEnabled,
                                        bool monoInput) {
            encoder.prepare(kSampleRate);
            configureLandscapeFixture(
                encoder, 3u, s3g::AmbiCartographyLayout::Radial);
            auto params = encoder.params();
            params.stereoMap = monoInput
                ? s3g::AmbiCartographyStereoMap::Mono
                : s3g::AmbiCartographyStereoMap::Alternate;
            params.macroEngine =
                s3g::AmbiCartographyMacroEngine::Fracture;
            params.macroMetric =
                s3g::AmbiCartographyMacroMetric::Network;
            params.macro = 1.0f;
            params.color = 0.58f;
            params.memory = 0.0f;
            params.spread = 0.0f;
            params.deviation = 0.0f;
            params.skew = 0.0f;
            params.center = 0.5f;
            params.processMix = 1.0f;
            params.listenMode = s3g::AmbiFieldListenMode::Off;
            params.listenerAmount = 0.0f;
            params.outputGainDb = 0.0f;
            encoder.setParams(params);
            auto options = encoder.siteProcessOptions();
            options.fractureProcessor = s3g::FractureProcessor::Logic;
            encoder.setSiteProcessOptions(options);
            for (uint32_t siteIndex = 0u; siteIndex < 3u; ++siteIndex) {
                auto site = encoder.site(siteIndex);
                site.x = -0.6f + static_cast<float>(siteIndex) * 0.6f;
                site.y = 0.8f;
                site.z = 0.0f;
                site.gain = siteIndex == 0u ? 1.0f : 0.0f;
                site.networkPosition =
                    static_cast<float>(siteIndex) * 0.5f;
                site.networkTrimMs = 0.0f;
                site.enabled = siteIndex != 1u || middleEnabled;
                encoder.setSite(siteIndex, site);
            }
            encoder.setLandscapeParams({});
            encoder.reset();
        };

        auto nextRightEncoder =
            std::make_unique<s3g::AmbiCartographyEncoder>();
        auto nextLeftEncoder =
            std::make_unique<s3g::AmbiCartographyEncoder>();
        configureLogic(*nextRightEncoder, true, false);
        configureLogic(*nextLeftEncoder, false, false);
        const auto nextRight = renderStereo<kChannels>(
            *nextRightEncoder, left, right, true);
        const auto nextLeft = renderStereo<kChannels>(
            *nextLeftEncoder, left, right, true);
        double routingDifference = 0.0;
        for (uint32_t channel = 0u; channel < kChannels; ++channel) {
            for (uint32_t frame = inspectFirst; frame < frames; ++frame) {
                routingDifference = std::max(routingDifference,
                    static_cast<double>(std::fabs(
                        nextRight[channel][frame]
                        - nextLeft[channel][frame])));
            }
        }
        if (!finiteAndBounded(nextRight) || !finiteAndBounded(nextLeft)
            || energy(nextRight, inspectFirst) < 0.0001
            || energy(nextLeft, inspectFirst) < 0.0001
            || routingDifference < 0.005) {
            std::cerr << "Cartography Fracture Logic relationship routing "
                      << "failed: difference=" << routingDifference
                      << " energy=" << energy(nextRight, inspectFirst)
                      << "/" << energy(nextLeft, inspectFirst) << "\n";
            return 1;
        }

        // Copied mono feeds used to leave every XOR pair in the same state,
        // reducing a full-wet Logic path to DC-blocked silence. At Memory zero
        // and Amount/Mix one there is no dry or recurrent path that can hide
        // that failure. Carrier-assisted bipolar bursts must remain active,
        // audibly distinct from Clean, finite, and free of hard edges.
        auto monoLogicEncoder =
            std::make_unique<s3g::AmbiCartographyEncoder>();
        auto monoCleanEncoder =
            std::make_unique<s3g::AmbiCartographyEncoder>();
        configureLogic(*monoLogicEncoder, true, true);
        configureLogic(*monoCleanEncoder, true, true);
        auto cleanParams = monoCleanEncoder->params();
        cleanParams.macroEngine =
            s3g::AmbiCartographyMacroEngine::Clean;
        monoCleanEncoder->setParams(cleanParams);
        monoCleanEncoder->reset();
        const auto mono = stereoTone(frames, 181.0f);
        const auto monoLogic = renderStereo<kChannels>(
            *monoLogicEncoder, mono[0], mono[1], true);
        const auto monoClean = renderStereo<kChannels>(
            *monoCleanEncoder, mono[0], mono[1], true);
        double maximumStep = 0.0;
        double monoDifference = 0.0;
        for (uint32_t channel = 0u; channel < kChannels; ++channel) {
            float previous = monoLogic[channel][inspectFirst];
            for (uint32_t frame = inspectFirst + 1u;
                frame < frames; ++frame) {
                maximumStep = std::max(maximumStep,
                    static_cast<double>(std::fabs(
                        monoLogic[channel][frame] - previous)));
                monoDifference = std::max(monoDifference,
                    static_cast<double>(std::fabs(
                        monoLogic[channel][frame]
                        - monoClean[channel][frame])));
                previous = monoLogic[channel][frame];
            }
        }
        const double monoLogicEnergy = energy(
            monoLogic, inspectFirst);
        if (!finiteAndBounded(monoLogic)
            || monoLogicEnergy < 0.0001
            || monoDifference < 0.005
            || maximumStep > 0.05) {
            std::cerr << "Cartography Fracture Logic mono activity failed: "
                      << "energy=" << monoLogicEnergy
                      << " difference=" << monoDifference
                      << " step=" << maximumStep << "\n";
            return 1;
        }
    }

    // Hidden selector values belong only to their matching engines. They are
    // deliberately retained while other engines run, but must not leak into
    // those engines' audio paths.
    {
        const auto signal = deterministicStereo(4096u);
        s3g::AmbiCartographyEncoder isolationEncoder;
        isolationEncoder.prepare(kSampleRate);
        configureLandscapeFixture(isolationEncoder, 4u);
        auto isolationParams = isolationEncoder.params();
        isolationParams.macro = 0.76f;
        isolationParams.color = 0.63f;
        isolationParams.memory = 0.41f;
        isolationParams.spread = 0.72f;
        isolationParams.deviation = 0.24f;
        isolationParams.processMix = 0.86f;
        isolationParams.outputGainDb = -9.0f;
        const auto render = [&](s3g::AmbiCartographyMacroEngine engine,
                                s3g::MacroShredCircuit shred,
                                s3g::FractureProcessor fracture) {
            isolationParams.macroEngine = engine;
            isolationEncoder.setParams(isolationParams);
            s3g::AmbiCartographySiteProcessOptions options {};
            options.shredCircuit = shred;
            options.fractureProcessor = fracture;
            isolationEncoder.setSiteProcessOptions(options);
            isolationEncoder.reset();
            return renderStereo<kChannels>(isolationEncoder,
                signal[0], signal[1], true);
        };
        for (uint32_t engine = 3u; engine <= 7u; ++engine) {
            const auto selected =
                static_cast<s3g::AmbiCartographyMacroEngine>(engine);
            const auto reference = render(selected,
                s3g::MacroShredCircuit::Shred,
                s3g::FractureProcessor::Relay);
            const auto unrelated = engine == 3u
                ? render(selected, s3g::MacroShredCircuit::Shred,
                    s3g::FractureProcessor::OctStack)
                : engine == 4u
                    ? render(selected, s3g::MacroShredCircuit::Diode,
                        s3g::FractureProcessor::Relay)
                    : render(selected, s3g::MacroShredCircuit::Diode,
                        s3g::FractureProcessor::OctStack);
            if (maximumDifference(reference, unrelated) != 0.0) {
                std::cerr << "Inactive selector leaked into engine "
                          << engine << "\n";
                return 1;
            }
        }
    }

    // The released signal path is the exact bypass for every landscape
    // amount at zero. Compare an untouched encoder with explicit per-effect
    // zero assignments using a broadband, non-symmetric stereo fixture.
    {
        const auto signal = deterministicStereo(8192u);
        s3g::AmbiCartographyEncoder referenceEncoder;
        s3g::AmbiCartographyEncoder bypassEncoder;
        referenceEncoder.prepare(kSampleRate);
        bypassEncoder.prepare(kSampleRate);
        configureLandscapeFixture(referenceEncoder);
        configureLandscapeFixture(bypassEncoder);
        referenceEncoder.reset();
        const auto reference = renderStereo<kChannels>(referenceEncoder,
            signal[0], signal[1]);
        for (uint32_t effect = 0u; effect < 7u; ++effect) {
            s3g::AmbiCartographyLandscapeParams bypass {};
            switch (effect) {
            case 0u: bypass.multipath = 0.0f; break;
            case 1u: bypass.occlusion = 0.0f; break;
            case 2u: bypass.networkWeather = 0.0f; break;
            case 3u: bypass.refraction = 0.0f; break;
            case 4u: bypass.motion = 0.0f; break;
            case 5u: bypass.ecology = 0.0f; break;
            default: bypass.horizon = 0.0f; break;
            }
            bypassEncoder.setLandscapeParams(bypass);
            bypassEncoder.reset();
            const auto rendered = renderStereo<kChannels>(bypassEncoder,
                signal[0], signal[1]);
            const double difference = maximumDifference(reference, rendered);
            if (difference != 0.0) {
                std::cerr << "Landscape zero bypass failed for effect "
                          << effect << ": " << difference << "\n";
                return 1;
            }
        }
    }

    // Multipath must create delayed ground/facade energy rather than merely
    // rescaling the direct path.
    {
        s3g::AmbiCartographyEncoder multipathEncoder;
        multipathEncoder.prepare(kSampleRate);
        configureSingleSite(multipathEncoder,
            s3g::AmbiCartographyTimeReference::Relative, 0.0f);
        multipathEncoder.setLandscapeParams({});
        multipathEncoder.reset();
        const auto dry = renderImpulse(multipathEncoder, 12000u);
        auto landscape = multipathEncoder.landscapeParams();
        landscape.multipath = 1.0f;
        multipathEncoder.setLandscapeParams(landscape);
        multipathEncoder.reset();
        const auto reflected = renderImpulse(multipathEncoder, 12000u);
        const double dryTail = energy(dry, 8u);
        const double reflectedTail = energy(reflected, 8u);
        if (!finiteAndBounded(reflected)
            || reflectedTail < dryTail + 0.001) {
            std::cerr << "Landscape multipath did not create delayed paths: "
                      << dryTail << "/" << reflectedTail << "\n";
            return 1;
        }
    }

    // A deliberately obstructed Ridge path should publish strong occlusion
    // and measurably remove high-frequency energy after its smoothing time.
    {
        const auto tone = stereoTone(72000u, 8000.0f);
        s3g::AmbiCartographyEncoder occlusionEncoder;
        occlusionEncoder.prepare(kSampleRate);
        configureLandscapeFixture(occlusionEncoder, 1u,
            s3g::AmbiCartographyLayout::Ridge);
        auto site = occlusionEncoder.site(0u);
        site.x = 0.0f;
        site.y = 0.68f;
        site.z = -0.20f;
        occlusionEncoder.setSite(0u, site);
        occlusionEncoder.reset();
        const auto clear = renderStereo<kChannels>(occlusionEncoder,
            tone[0], tone[1]);
        auto landscape = occlusionEncoder.landscapeParams();
        landscape.occlusion = 1.0f;
        occlusionEncoder.setLandscapeParams(landscape);
        occlusionEncoder.reset();
        const auto blocked = renderStereo<kChannels>(occlusionEncoder,
            tone[0], tone[1]);
        const double clearLate = energy(clear, 48000u);
        const double blockedLate = energy(blocked, 48000u);
        if (occlusionEncoder.siteOcclusion(0u) < 0.90f
            || blockedLate >= clearLate * 0.35
            || !finiteAndBounded(blocked)) {
            std::cerr << "Landscape occlusion contract failed: mask="
                      << occlusionEncoder.siteOcclusion(0u)
                      << " energy=" << clearLate << "/" << blockedLate
                      << "\n";
            return 1;
        }
    }

    // Network weather is deterministic and sample-clock based: irregular
    // host blocks must produce the same render as one large block.
    {
        const auto signal = deterministicStereo(16384u);
        s3g::AmbiCartographyEncoder weatherEncoder;
        s3g::AmbiCartographyEncoder segmentedEncoder;
        weatherEncoder.prepare(kSampleRate);
        segmentedEncoder.prepare(kSampleRate);
        configureLandscapeFixture(weatherEncoder);
        configureLandscapeFixture(segmentedEncoder);
        auto weatherParams = weatherEncoder.params();
        weatherParams.memory = 1.0f;
        weatherParams.turbulence = 1.0f;
        weatherEncoder.setParams(weatherParams);
        segmentedEncoder.setParams(weatherParams);
        weatherEncoder.reset();
        const auto calm = renderStereo<kChannels>(weatherEncoder,
            signal[0], signal[1]);
        s3g::AmbiCartographyLandscapeParams landscape {};
        landscape.networkWeather = 1.0f;
        weatherEncoder.setLandscapeParams(landscape);
        segmentedEncoder.setLandscapeParams(landscape);
        weatherEncoder.reset();
        segmentedEncoder.reset();
        const auto weather = renderStereo<kChannels>(weatherEncoder,
            signal[0], signal[1]);
        const auto segmented = renderStereo<kChannels>(segmentedEncoder,
            signal[0], signal[1], true);
        const double deterministicDifference =
            maximumDifference(weather, segmented);
        if (deterministicDifference > 0.000001
            || maximumDifference(calm, weather) < 0.0001
            || !finiteAndBounded(weather)) {
            std::cerr << "Landscape network weather contract failed: split="
                      << deterministicDifference << " effect="
                      << maximumDifference(calm, weather) << "\n";
            return 1;
        }
    }

    // Weather oscillator wraps and turbulence value-noise bucket changes must
    // remain continuous. With DC input, any appreciable adjacent-sample jump
    // is an internally generated click rather than source material.
    {
        s3g::AmbiCartographyEncoder continuityEncoder;
        s3g::AmbiCartographyEncoder calmEncoder;
        continuityEncoder.prepare(kSampleRate);
        calmEncoder.prepare(kSampleRate);
        configureLandscapeFixture(continuityEncoder,
            s3g::kAmbiCartographyMaxSites,
            s3g::AmbiCartographyLayout::Grid);
        configureLandscapeFixture(calmEncoder,
            s3g::kAmbiCartographyMaxSites,
            s3g::AmbiCartographyLayout::Grid);
        auto continuityParams = continuityEncoder.params();
        continuityParams.memory = 0.0f;
        continuityParams.turbulence = 1.0f;
        continuityParams.macroEngine =
            s3g::AmbiCartographyMacroEngine::Clean;
        continuityParams.processMix = 0.0f;
        continuityParams.outputGainDb = 0.0f;
        continuityEncoder.setParams(continuityParams);
        auto calmParams = continuityParams;
        calmParams.turbulence = 0.0f;
        calmEncoder.setParams(calmParams);
        s3g::AmbiCartographyLandscapeParams landscape {};
        landscape.networkWeather = 1.0f;
        continuityEncoder.setLandscapeParams(landscape);
        calmEncoder.setLandscapeParams(landscape);
        continuityEncoder.reset();
        calmEncoder.reset();
        std::vector<float> settle(kSampleRate * 2u, 0.25f);
        const auto settled = renderStereo<kChannels>(
            continuityEncoder, settle, settle, true);
        renderStereo<kChannels>(calmEncoder, settle, settle, true);
        std::vector<float> measure(kSampleRate * 6u, 0.25f);
        const auto continuous = renderStereo<kChannels>(
            continuityEncoder, measure, measure, true);
        const auto calm = renderStereo<kChannels>(
            calmEncoder, measure, measure, true);
        double maximumStep = 0.0;
        for (uint32_t channel = 0u; channel < 4u; ++channel) {
            maximumStep = std::max(maximumStep,
                static_cast<double>(std::fabs(
                    continuous[channel].front()
                        - settled[channel].back())));
            for (size_t frame = 1u;
                frame < continuous[channel].size(); ++frame) {
                maximumStep = std::max(maximumStep,
                    static_cast<double>(std::fabs(
                        continuous[channel][frame]
                            - continuous[channel][frame - 1u])));
            }
        }
        const double modulationDifference = maximumDifference(
            continuous, calm);
        if (!finiteAndBounded(continuous) || !finiteAndBounded(calm)
            || maximumStep > 0.00035
            || modulationDifference < 0.0001) {
            std::cerr << "Landscape modulation continuity failed: step="
                      << maximumStep << " effect="
                      << modulationDifference << "\n";
            return 1;
        }
    }

    // Refraction aligned with the site bearing increases effective sound
    // speed, so a physically delayed impulse must arrive materially earlier.
    {
        s3g::AmbiCartographyEncoder refractionEncoder;
        refractionEncoder.prepare(kSampleRate);
        configureSingleSite(refractionEncoder,
            s3g::AmbiCartographyTimeReference::Absolute, 0.0f);
        auto refractionParams = refractionEncoder.params();
        refractionParams.carry = 1.0f;
        refractionParams.skew = 0.0f;
        refractionEncoder.setParams(refractionParams);
        refractionEncoder.setLandscapeParams({});
        refractionEncoder.reset();
        const auto stillAir = renderImpulse(refractionEncoder, 6000u);
        s3g::AmbiCartographyLandscapeParams landscape {};
        landscape.refraction = 1.0f;
        refractionEncoder.setLandscapeParams(landscape);
        refractionEncoder.reset();
        const auto refracted = renderImpulse(refractionEncoder, 6000u);
        const uint32_t stillOnset = onset(stillAir[0]);
        const uint32_t refractedOnset = onset(refracted[0]);
        if (stillOnset < 4798u || stillOnset > 4802u
            || refractedOnset + 200u >= stillOnset
            || refractionEncoder.siteArrivalSeconds(0u) >= 0.095f
            || !finiteAndBounded(refracted)) {
            std::cerr << "Landscape refraction timing failed: "
                      << stillOnset << "/" << refractedOnset
                      << " arrival="
                      << refractionEncoder.siteArrivalSeconds(0u) << "\n";
            return 1;
        }
    }

    // Moving cartographies use the same internal 32-sample control clock for
    // large and segmented host blocks and visibly displace the rendered map.
    {
        const auto signal = deterministicStereo(16384u);
        s3g::AmbiCartographyEncoder motionEncoder;
        s3g::AmbiCartographyEncoder segmentedEncoder;
        motionEncoder.prepare(kSampleRate);
        segmentedEncoder.prepare(kSampleRate);
        configureLandscapeFixture(motionEncoder, 8u,
            s3g::AmbiCartographyLayout::Radial);
        configureLandscapeFixture(segmentedEncoder, 8u,
            s3g::AmbiCartographyLayout::Radial);
        auto motionParams = motionEncoder.params();
        motionParams.memory = 0.0f;
        motionParams.propagationScale = 0.45f;
        motionEncoder.setParams(motionParams);
        segmentedEncoder.setParams(motionParams);
        motionEncoder.reset();
        const auto stationary = renderStereo<kChannels>(motionEncoder,
            signal[0], signal[1]);
        s3g::AmbiCartographyLandscapeParams landscape {};
        landscape.motion = 1.0f;
        motionEncoder.setLandscapeParams(landscape);
        segmentedEncoder.setLandscapeParams(landscape);
        motionEncoder.reset();
        segmentedEncoder.reset();
        const auto moving = renderStereo<kChannels>(motionEncoder,
            signal[0], signal[1]);
        const auto segmented = renderStereo<kChannels>(segmentedEncoder,
            signal[0], signal[1], true);
        const auto listener = motionEncoder.renderedListenerPosition();
        const float displacement = std::sqrt(listener.x * listener.x
            + listener.y * listener.y + listener.z * listener.z);
        const double deterministicDifference =
            maximumDifference(moving, segmented);
        if (deterministicDifference > 0.000001
            || maximumDifference(stationary, moving) < 0.0001
            || displacement < 0.40f
            || !finiteAndBounded(moving)) {
            std::cerr << "Landscape motion contract failed: split="
                      << deterministicDifference << " effect="
                      << maximumDifference(stationary, moving)
                      << " displacement=" << displacement << "\n";
            return 1;
        }
    }

    // Feedback Ecology must create an audible return while its bounded graph
    // decays under the most persistent legal settings.
    {
        s3g::AmbiCartographyEncoder ecologyEncoder;
        ecologyEncoder.prepare(kSampleRate);
        configureLandscapeFixture(ecologyEncoder, 2u,
            s3g::AmbiCartographyLayout::Grid);
        auto ecologyParams = ecologyEncoder.params();
        ecologyParams.memory = 1.0f;
        ecologyParams.spread = 1.0f;
        ecologyEncoder.setParams(ecologyParams);
        s3g::AmbiCartographyLandscapeParams landscape {};
        landscape.ecology = 1.0f;
        ecologyEncoder.setLandscapeParams(landscape);
        ecologyEncoder.reset();
        constexpr uint32_t ecologyFrames = kSampleRate * 5u;
        const auto ecology = renderImpulse(ecologyEncoder, ecologyFrames);
        const double earlyTail = energy(ecology, 24000u, 120000u);
        const double lateTail = energy(ecology, 192000u, ecologyFrames);
        if (!finiteAndBounded(ecology) || earlyTail < 0.000001
            || lateTail >= earlyTail) {
            std::cerr << "Landscape feedback ecology stability failed: "
                      << earlyTail << "/" << lateTail << "\n";
            return 1;
        }
    }

    // At maximum horizon strength a near site remains present while a site
    // beyond the map's audibility boundary fades deeply after smoothing.
    {
        const auto tone = stereoTone(72000u, 997.0f);
        const auto renderAtDistance = [&](float normalizedDistance) {
            s3g::AmbiCartographyEncoder horizonEncoder;
            horizonEncoder.prepare(kSampleRate);
            configureLandscapeFixture(horizonEncoder, 1u,
                s3g::AmbiCartographyLayout::Radial);
            auto site = horizonEncoder.site(0u);
            site.x = 0.0f;
            site.y = normalizedDistance;
            site.z = 0.0f;
            horizonEncoder.setSite(0u, site);
            s3g::AmbiCartographyLandscapeParams landscape {};
            landscape.horizon = 1.0f;
            horizonEncoder.setLandscapeParams(landscape);
            horizonEncoder.reset();
            return renderStereo<kChannels>(horizonEncoder,
                tone[0], tone[1]);
        };
        const auto near = renderAtDistance(0.20f);
        const auto far = renderAtDistance(1.20f);
        const double nearLate = energy(near, 48000u);
        const double farLate = energy(far, 48000u);
        if (!finiteAndBounded(near) || !finiteAndBounded(far)
            || nearLate < 1.0 || farLate >= nearLate * 0.05) {
            std::cerr << "Landscape audibility horizon failed: "
                      << nearLate << "/" << farLate << "\n";
            return 1;
        }
    }

    // Exercise the complete landscape with the maximum site population and
    // all 64 7OA channels to catch cross-effect instability.
    {
        const auto signal = deterministicStereo(12000u);
        for (double sampleRate : { 48000.0, 96000.0 }) {
            s3g::AmbiCartographyEncoder stressEncoder;
            stressEncoder.prepare(sampleRate);
            configureLandscapeFixture(stressEncoder,
                s3g::kAmbiCartographyMaxSites,
                s3g::AmbiCartographyLayout::Grid);
            auto stressParams = stressEncoder.params();
            stressParams.order = s3g::kAmbiCartographyMaxOrder;
            stressParams.mapScaleMeters = 700.0f;
            stressParams.networkSpreadMs = 80.0f;
            stressParams.propagationScale = 0.25f;
            stressParams.air = 0.80f;
            stressParams.distanceLoss = 0.70f;
            stressParams.carry = 0.70f;
            stressParams.turbulence = 0.80f;
            stressParams.macro = 0.82f;
            stressParams.color = 0.70f;
            stressParams.memory = 0.20f;
            stressParams.spread = 0.85f;
            stressParams.deviation = 0.42f;
            stressParams.processMix = 0.80f;
            stressParams.outputGainDb = -12.0f;
            s3g::AmbiCartographyLandscapeParams landscape {};
            landscape.multipath = 1.0f;
            landscape.occlusion = 1.0f;
            landscape.networkWeather = 1.0f;
            landscape.refraction = 0.80f;
            landscape.motion = 1.0f;
            landscape.ecology = 1.0f;
            landscape.horizon = 1.0f;
            stressEncoder.setLandscapeParams(landscape);
            for (uint32_t engine = 4u; engine <= 7u; ++engine) {
                stressParams.macroEngine =
                    static_cast<s3g::AmbiCartographyMacroEngine>(engine);
                stressEncoder.setParams(stressParams);
                stressEncoder.reset();
                const auto stress =
                    renderStereo<s3g::kAmbiCartographyMaxChannels>(
                        stressEncoder, signal[0], signal[1], true);
                if (!finiteAndBounded(stress)
                    || energy(stress) < 0.000001) {
                    std::cerr
                        << "Combined landscape/site-process stress failed at "
                        << sampleRate << " Hz for engine " << engine
                        << ": energy=" << energy(stress) << "\n";
                    return 1;
                }
            }
        }
    }

    std::cout << "Cartography arrival onsets absolute/relative/network: "
              << absoluteOnset << "/" << relativeOnset << "/"
              << networkOnset << " frames\n";
    std::cout << "Cartography macro engines, HOA output, and Listener OFF contract passed\n";
    std::cout << "Cartography landscape effects, bypass, deterministic motion/weather, and ecology stability passed\n";
    return 0;
}
