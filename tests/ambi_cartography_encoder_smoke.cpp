#include "s3g_ambi_cartography_encoder.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {

constexpr uint32_t kSampleRate = 48000u;
constexpr uint32_t kFrames = 7200u;
constexpr uint32_t kChannels = 4u;

using Audio = std::array<std::vector<float>, kChannels>;

Audio renderImpulse(s3g::AmbiCartographyEncoder& encoder,
    uint32_t frames = kFrames)
{
    std::vector<float> left(frames, 0.0f);
    std::vector<float> right(frames, 0.0f);
    left[0] = 1.0f;
    right[0] = 1.0f;
    std::array<const float*, 2u> input { left.data(), right.data() };
    Audio output;
    std::array<float*, kChannels> outputPointers {};
    for (uint32_t channel = 0u; channel < kChannels; ++channel) {
        output[channel].assign(frames, 0.0f);
        outputPointers[channel] = output[channel].data();
    }
    encoder.processBlock(input.data(), outputPointers.data(), 2u,
        kChannels, frames);
    return output;
}

uint32_t onset(const std::vector<float>& signal, float threshold = 0.01f)
{
    for (uint32_t frame = 0u; frame < signal.size(); ++frame) {
        if (std::fabs(signal[frame]) >= threshold) return frame;
    }
    return static_cast<uint32_t>(signal.size());
}

double energy(const Audio& audio)
{
    double result = 0.0;
    for (const auto& channel : audio) {
        for (float sample : channel) result += sample * sample;
    }
    return result;
}

bool finiteAndBounded(const Audio& audio)
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

    s3g::AmbiCartographyEncoder encoder;
    encoder.prepare(kSampleRate);

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

    for (uint32_t engine = 0u; engine <= 4u; ++engine) {
        params.macroEngine = static_cast<s3g::AmbiCartographyMacroEngine>(engine);
        params.listenMode = s3g::AmbiFieldListenMode::Follow;
        params.listenerAmount = 0.65f;
        params.processMix = engine == 0u ? 0.0f : 0.65f;
        encoder.setParams(params);
        encoder.reset();
        const auto result = renderImpulse(encoder, 2048u);
        if (!finiteAndBounded(result) || energy(result) < 0.000001) {
            std::cerr << "Macro engine stability failed for engine "
                      << engine << " energy=" << energy(result) << "\n";
            return 1;
        }
    }

    std::cout << "Cartography arrival onsets absolute/relative/network: "
              << absoluteOnset << "/" << relativeOnset << "/"
              << networkOnset << " frames\n";
    std::cout << "Cartography macro engines, HOA output, and Listener OFF contract passed\n";
    return 0;
}
