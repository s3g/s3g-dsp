#include "s3g_ambi_wrangler_encoder.h"
#include "s3g_ambi_wrangler_presets.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <vector>

namespace {

constexpr uint32_t kChannels = s3g::kAmbiWranglerMaxChannels;
constexpr uint32_t kFrames = 128u;
constexpr uint32_t kBlocks = 620u;

struct RenderResult {
    double energy = 0.0;
    double difference = 0.0;
    float activity = 0.0f;
    float capture = 0.0f;
    uint64_t heldClocks = 0u;
    uint32_t registerWord = 0u;
};

struct DynamicsMetrics {
    double energy = 0.0;
    double differenceEnergy = 0.0;
    double lateDifference = 0.0;
    uint64_t samples = 0u;
    uint64_t lateComparisons = 0u;
    uint64_t registerTransitions = 0u;
    uint64_t registerNovelty = 0u;
    uint64_t clockEvents = 0u;
    uint64_t registerAdvances = 0u;
    uint64_t heldClocks = 0u;
    float peak = 0.0f;
    float capture = 0.0f;
    float minimumEvolutionRate = 1.0f;
    bool finite = true;

    double rms() const
    {
        return std::sqrt(energy / static_cast<double>(std::max<uint64_t>(1u, samples)));
    }

    double roughness() const
    {
        return differenceEnergy / std::max(1.0e-18, energy);
    }

    double lateMotion() const
    {
        return lateDifference
            / static_cast<double>(std::max<uint64_t>(1u, lateComparisons));
    }
};

struct TransitionMetrics {
    float baselineMaxStep = 0.0f;
    float boundaryStep = 0.0f;
    float transitionMaxStep = 0.0f;
    float peak = 0.0f;
    double energy = 0.0;
    bool finite = true;
};

struct DifferentialTransitionMetrics {
    float preTransitionMismatch = 0.0f;
    float boundaryResidual = 0.0f;
    float earlyResidualStep = 0.0f;
    float maximumResidual = 0.0f;
    float peak = 0.0f;
    double outputEnergy = 0.0;
    double residualEnergy = 0.0;
    bool finite = true;
    bool clockAligned = false;
};

struct AudioTrace {
    std::vector<float> samples;
    double energy = 0.0;
    float peak = 0.0f;
    bool finite = true;

    double rms() const
    {
        return std::sqrt(
            energy / static_cast<double>(
                std::max<std::size_t>(1u, samples.size())));
    }
};

DifferentialTransitionMetrics measureDifferentialTransition(
    s3g::AmbiWranglerParams before,
    s3g::AmbiWranglerParams after)
{
    constexpr uint32_t kProbeChannels = 4u;
    constexpr uint32_t kWarmBlocks = 128u;
    constexpr uint32_t kProbeBlocks = 24u;
    constexpr uint32_t kEarlySamples = 8u;

    s3g::AmbiWranglerEncoder reference;
    s3g::AmbiWranglerEncoder automated;
    reference.prepare(48000.0);
    automated.prepare(48000.0);
    reference.setParams(before);
    automated.setParams(before);
    reference.reset();
    automated.reset();

    std::array<std::vector<float>, kProbeChannels> referenceStorage;
    std::array<std::vector<float>, kProbeChannels> automatedStorage;
    std::array<float*, kProbeChannels> referenceOutputs {};
    std::array<float*, kProbeChannels> automatedOutputs {};
    for (uint32_t channel = 0u; channel < kProbeChannels; ++channel) {
        referenceStorage[channel].resize(kFrames);
        automatedStorage[channel].resize(kFrames);
        referenceOutputs[channel] = referenceStorage[channel].data();
        automatedOutputs[channel] = automatedStorage[channel].data();
    }

    DifferentialTransitionMetrics result;
    for (uint32_t block = 0u; block < kWarmBlocks; ++block) {
        reference.process(
            referenceOutputs.data(), kProbeChannels, kFrames);
        automated.process(
            automatedOutputs.data(), kProbeChannels, kFrames);
        for (uint32_t channel = 0u;
            channel < kProbeChannels; ++channel) {
            for (uint32_t frame = 0u; frame < kFrames; ++frame) {
                const float referenceValue =
                    referenceStorage[channel][frame];
                const float automatedValue =
                    automatedStorage[channel][frame];
                result.finite = result.finite
                    && std::isfinite(referenceValue)
                    && std::isfinite(automatedValue);
                result.preTransitionMismatch = std::max(
                    result.preTransitionMismatch,
                    std::fabs(referenceValue - automatedValue));
            }
        }
    }

    // Start every automation probe immediately after a genuine clock edge.
    // This prevents Rung Mode/Size from getting a coincidental first-sample
    // commit and gives all rows the same deterministic boundary condition.
    const uint64_t alignmentClock =
        reference.voiceClockCount(0u);
    for (uint32_t sample = 0u; sample < 262144u; ++sample) {
        reference.process(
            referenceOutputs.data(), kProbeChannels, 1u);
        automated.process(
            automatedOutputs.data(), kProbeChannels, 1u);
        for (uint32_t channel = 0u;
            channel < kProbeChannels; ++channel) {
            const float referenceValue =
                referenceStorage[channel][0];
            const float automatedValue =
                automatedStorage[channel][0];
            result.finite = result.finite
                && std::isfinite(referenceValue)
                && std::isfinite(automatedValue);
            result.preTransitionMismatch = std::max(
                result.preTransitionMismatch,
                std::fabs(referenceValue - automatedValue));
        }
        if (reference.voiceClockCount(0u) > alignmentClock) {
            result.clockAligned =
                automated.voiceClockCount(0u)
                    == reference.voiceClockCount(0u);
            break;
        }
    }

    automated.setParams(after);
    std::array<float, kProbeChannels> previousResidual {};
    for (uint32_t block = 0u; block < kProbeBlocks; ++block) {
        reference.process(
            referenceOutputs.data(), kProbeChannels, kFrames);
        automated.process(
            automatedOutputs.data(), kProbeChannels, kFrames);
        for (uint32_t channel = 0u;
            channel < kProbeChannels; ++channel) {
            for (uint32_t frame = 0u; frame < kFrames; ++frame) {
                const float referenceValue =
                    referenceStorage[channel][frame];
                const float automatedValue =
                    automatedStorage[channel][frame];
                const float residual =
                    automatedValue - referenceValue;
                result.finite = result.finite
                    && std::isfinite(referenceValue)
                    && std::isfinite(automatedValue)
                    && std::isfinite(residual);
                result.peak = std::max(
                    result.peak,
                    std::max(std::fabs(referenceValue),
                        std::fabs(automatedValue)));
                result.maximumResidual = std::max(
                    result.maximumResidual, std::fabs(residual));
                result.outputEnergy +=
                    static_cast<double>(referenceValue) * referenceValue
                    + static_cast<double>(automatedValue) * automatedValue;
                result.residualEnergy +=
                    static_cast<double>(residual) * residual;
                if (block == 0u && frame == 0u) {
                    result.boundaryResidual = std::max(
                        result.boundaryResidual,
                        std::fabs(residual));
                }
                const uint32_t elapsed =
                    block * kFrames + frame;
                if (elapsed < kEarlySamples) {
                    result.earlyResidualStep = std::max(
                        result.earlyResidualStep,
                        std::fabs(
                            residual - previousResidual[channel]));
                }
                previousResidual[channel] = residual;
            }
        }
    }
    return result;
}

void fillNeutralCurves(s3g::AmbiWranglerParams& params)
{
    params.bpRateA.fill(0.5f);
    params.bpRateB.fill(0.5f);
    params.bpFmAtoB.fill(0.5f);
    params.bpFmBtoA.fill(0.5f);
    params.bpRunglerA.fill(0.5f);
    params.bpRunglerB.fill(0.5f);
    params.bpFilter.fill(0.5f);
    params.bpThreshold.fill(0.5f);
    params.bpPwmA.fill(0.5f);
    params.bpPwmB.fill(0.5f);
    params.bpRampA.fill(0.5f);
    params.bpRampB.fill(0.5f);
    params.bpAmp.fill(1.0f);
    params.bpColorA.fill(0.0f);
    params.bpColorB.fill(0.0f);
    params.bpFilterFreqB.fill(0.5f);
    params.bpFilterRes.fill(0.5f);
    params.bpFilterComp.fill(0.0f);
    params.bpFilterType.fill(0.5f);
    params.bpCrossA.fill(0.0f);
    params.bpCrossB.fill(0.0f);
    params.bpCrossLpf.fill(0.25f);
    params.bpRungMode.fill(0.0f);
    params.bpRungSize.fill(0.5f);
}

AudioTrace renderAudioTrace(
    s3g::AmbiWranglerParams params,
    uint32_t blocks = 200u,
    uint32_t warmupBlocks = 64u)
{
    constexpr uint32_t kTraceChannels = 4u;
    s3g::AmbiWranglerEncoder encoder;
    encoder.prepare(48000.0);
    encoder.setParams(params);
    encoder.reset();

    std::array<std::vector<float>, kTraceChannels> storage;
    std::array<float*, kTraceChannels> outputs {};
    for (uint32_t channel = 0u; channel < kTraceChannels; ++channel) {
        storage[channel].resize(kFrames);
        outputs[channel] = storage[channel].data();
    }

    AudioTrace result;
    result.samples.reserve(
        static_cast<std::size_t>(blocks - warmupBlocks)
        * kFrames * kTraceChannels);
    for (uint32_t block = 0u; block < blocks; ++block) {
        encoder.process(outputs.data(), kTraceChannels, kFrames);
        if (block < warmupBlocks) continue;
        for (uint32_t channel = 0u; channel < kTraceChannels; ++channel) {
            for (float value : storage[channel]) {
                result.finite = result.finite && std::isfinite(value);
                result.peak = std::max(result.peak, std::fabs(value));
                result.energy += static_cast<double>(value) * value;
                result.samples.push_back(value);
            }
        }
    }
    return result;
}

double traceDifference(const AudioTrace& a, const AudioTrace& b)
{
    const std::size_t samples = std::min(a.samples.size(), b.samples.size());
    double difference = 0.0;
    for (std::size_t i = 0u; i < samples; ++i) {
        difference += std::fabs(
            static_cast<double>(a.samples[i]) - b.samples[i]);
    }
    return difference / static_cast<double>(
        std::max<std::size_t>(1u, samples));
}

struct PartitionTrace {
    std::array<std::vector<float>, kChannels> channels;
    double energy = 0.0;
    bool finite = true;
};

PartitionTrace renderPartitioned(
    const s3g::AmbiWranglerParams& params,
    uint32_t partitionFrames,
    uint32_t totalFrames,
    double sampleRate = 48000.0)
{
    s3g::AmbiWranglerEncoder encoder;
    encoder.prepare(sampleRate);
    encoder.setParams(params);
    encoder.reset();

    PartitionTrace result;
    for (auto& channel : result.channels) {
        channel.resize(totalFrames);
    }
    uint32_t offset = 0u;
    while (offset < totalFrames) {
        const uint32_t frames = std::min<uint32_t>(
            partitionFrames, totalFrames - offset);
        std::array<float*, kChannels> outputs {};
        for (uint32_t channel = 0u;
            channel < kChannels; ++channel) {
            outputs[channel] =
                result.channels[channel].data() + offset;
        }
        encoder.process(outputs.data(), kChannels, frames);
        offset += frames;
    }
    for (const auto& channel : result.channels) {
        for (float value : channel) {
            result.finite =
                result.finite && std::isfinite(value);
            result.energy +=
                static_cast<double>(value) * value;
        }
    }
    return result;
}

bool testCoreBlockPartitionInvariance()
{
    constexpr uint32_t kTotalFrames = 2056u;
    static constexpr std::array<uint32_t, 5u>
        kPartitions { 1u, 7u, 16u, 64u, 257u };
    static constexpr std::array<s3g::AmbiWranglerCircuitLaw, 2u>
        kLaws {
            s3g::AmbiWranglerCircuitLaw::Legacy,
            s3g::AmbiWranglerCircuitLaw::Bounded,
        };
    static constexpr std::array<double, 2u>
        kSampleRates { 48000.0, 96000.0 };

    for (uint32_t configuration = 0u;
        configuration < kSampleRates.size() * kLaws.size();
        ++configuration) {
        const double sampleRate =
            kSampleRates[configuration / kLaws.size()];
        const auto law =
            kLaws[configuration % kLaws.size()];
        auto params = s3g::ambiWranglerFactoryPreset(
            s3g::kAmbiWranglerCalmPresetStart + 4u);
        params.order = 7u;
        params.voices = 8u;
        params.engines = params.voices;
        params.circuitLaw = law;
        params.voiceBreakpointsEnabled = true;
        params.maskMode = 5u;
        params.maskDepth = 0.38f;
        params.filterMorph = 0.37f;
        params.fieldWrite = 0.24f;
        params.registerMotion = 0.16f;
        params.propagation = 0.31f;
        if (law == s3g::AmbiWranglerCircuitLaw::Legacy) {
            params.listenerResponse =
                s3g::AmbiWranglerListenerResponse::Write;
            params.returnBypass = 0u;
            params.fieldReturn = 0.12f;
        }
        fillNeutralCurves(params);
        for (uint32_t voice = 0u;
            voice < params.voices; ++voice) {
            const float lane = static_cast<float>(voice)
                / static_cast<float>(params.voices - 1u);
            params.bpColorA[voice] = 0.12f + lane * 0.38f;
            params.bpColorB[voice] = 0.42f - lane * 0.26f;
            params.bpFilterFreqB[voice] =
                0.18f + lane * 0.68f;
            params.bpFilterRes[voice] =
                0.22f + lane * 0.56f;
            params.bpFilterComp[voice] =
                0.08f + lane * 0.34f;
            params.bpFilterType[voice] =
                0.15f + lane * 0.70f;
            params.bpCrossA[voice] =
                0.05f + lane * 0.28f;
            params.bpCrossB[voice] =
                0.30f - lane * 0.20f;
            params.bpCrossLpf[voice] =
                0.10f + lane * 0.75f;
            params.bpRungMode[voice] =
                static_cast<float>(voice % 3u) * 0.5f;
            params.bpRungSize[voice] = lane;
        }

        const auto reference = renderPartitioned(
            params, kPartitions[0], kTotalFrames,
            sampleRate);
        if (!reference.finite || reference.energy <= 1.0e-8) {
            std::fprintf(stderr,
                "Wrangler partition reference was silent or invalid "
                "for law %u at %.0f Hz\n",
                static_cast<uint32_t>(law), sampleRate);
            return false;
        }
        for (uint32_t index = 1u;
            index < kPartitions.size(); ++index) {
            const auto candidate = renderPartitioned(
                params, kPartitions[index], kTotalFrames,
                sampleRate);
            uint64_t mismatches = 0u;
            double maximumDifference = 0.0;
            for (uint32_t channel = 0u;
                channel < kChannels; ++channel) {
                for (uint32_t frame = 0u;
                    frame < kTotalFrames; ++frame) {
                    const float expected =
                        reference.channels[channel][frame];
                    const float actual =
                        candidate.channels[channel][frame];
                    if (std::memcmp(
                            &expected, &actual,
                            sizeof(float)) != 0) {
                        ++mismatches;
                        maximumDifference = std::max(
                            maximumDifference,
                            std::fabs(
                                static_cast<double>(expected)
                                    - actual));
                    }
                }
            }
            if (!candidate.finite
                || candidate.energy <= 1.0e-8
                || mismatches != 0u) {
                std::fprintf(stderr,
                    "Wrangler core changed with block partition "
                    "%u vs 1 in law %u at %.0f Hz: "
                    "mismatches=%llu max=%g "
                    "energy=%g\n",
                    kPartitions[index],
                    static_cast<uint32_t>(law),
                    sampleRate,
                    static_cast<unsigned long long>(mismatches),
                    maximumDifference, candidate.energy);
                return false;
            }
        }
    }
    std::printf(
        "Wrangler core block partitions 1/7/16/64/257 "
        "matched bit-for-bit in both laws at 48/96 kHz\n");
    return true;
}

bool testMalformedInputsRemainFinite()
{
    auto params = s3g::ambiWranglerFactoryPreset(
        s3g::kAmbiWranglerCalmPresetStart);
    params.order = 3u;
    params.voices = 4u;
    params.engines = params.voices;
    params.topologyMotion = 0u;
    params.listeningEnabled = 0u;
    static constexpr std::array<float, 4u> kBadAzimuths {
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(),
        1.0e30f,
    };
    const std::array<double, 6u> badSampleRates {
        std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),
        -48000.0,
        0.0,
        1.0e30,
    };

    std::array<std::array<float, 64u>, 16u> storage {};
    std::array<float*, 16u> outputs {};
    for (uint32_t channel = 0u;
        channel < outputs.size(); ++channel) {
        outputs[channel] = storage[channel].data();
    }
    auto renderFinite = [&](double sampleRate, float azimuth) {
        s3g::AmbiWranglerEncoder encoder;
        encoder.prepare(sampleRate);
        auto malformed = params;
        malformed.centerAzimuthDeg = azimuth;
        encoder.setParams(malformed);
        encoder.reset();
        encoder.process(
            outputs.data(),
            static_cast<uint32_t>(outputs.size()), 64u);
        const auto sanitized = encoder.params();
        if (!std::isfinite(sanitized.centerAzimuthDeg)
            || sanitized.centerAzimuthDeg < -180.0f
            || sanitized.centerAzimuthDeg > 180.0f) {
            return false;
        }
        for (const auto& channel : storage) {
            for (float value : channel) {
                if (!std::isfinite(value)) return false;
            }
        }
        return true;
    };

    for (float azimuth : kBadAzimuths) {
        if (!renderFinite(48000.0, azimuth)) {
            std::fprintf(stderr,
                "Wrangler did not sanitize malformed azimuth %g\n",
                azimuth);
            return false;
        }
    }
    for (uint32_t index = 0u;
        index < badSampleRates.size(); ++index) {
        if (!renderFinite(
                badSampleRates[index],
                kBadAzimuths[
                    index % kBadAzimuths.size()])) {
            std::fprintf(stderr,
                "Wrangler failed finite rendering for malformed "
                "sample rate %g\n", badSampleRates[index]);
            return false;
        }
    }
    return true;
}

bool testStartupAndResetRamp()
{
    auto params = s3g::ambiWranglerFactoryPreset(
        s3g::kAmbiWranglerCalmPresetStart);
    params.order = 7u;
    params.voices = 1u;
    params.engines = 1u;
    params.topologyMotion = 0u;
    params.maskMode = 0u;
    params.maskDepth = 0.0f;
    params.listeningEnabled = 0u;
    params.outputGainDb = 6.0f;

    s3g::AmbiWranglerEncoder encoder;
    encoder.prepare(48000.0);
    encoder.setParams(params);
    encoder.reset();
    std::array<std::array<float, 256u>, kChannels> storage {};
    std::array<float*, kChannels> outputs {};
    for (uint32_t channel = 0u;
        channel < kChannels; ++channel) {
        outputs[channel] = storage[channel].data();
    }
    encoder.process(outputs.data(), kChannels, 1u);
    std::array<float, kChannels> firstSample {};
    float firstPeak = 0.0f;
    for (uint32_t channel = 0u;
        channel < kChannels; ++channel) {
        firstSample[channel] = storage[channel][0];
        if (!std::isfinite(firstSample[channel])) return false;
        firstPeak = std::max(
            firstPeak, std::fabs(firstSample[channel]));
    }

    for (uint32_t block = 0u; block < 20u; ++block) {
        encoder.process(outputs.data(), kChannels, 256u);
    }
    float settledPeak = 0.0f;
    double settledEnergy = 0.0;
    for (const auto& channel : storage) {
        for (float value : channel) {
            if (!std::isfinite(value)) return false;
            settledPeak =
                std::max(settledPeak, std::fabs(value));
            settledEnergy +=
                static_cast<double>(value) * value;
        }
    }

    encoder.reset();
    encoder.process(outputs.data(), kChannels, 1u);
    for (uint32_t channel = 0u;
        channel < kChannels; ++channel) {
        if (std::memcmp(
                &firstSample[channel],
                &storage[channel][0],
                sizeof(float)) != 0) {
            std::fprintf(stderr,
                "Wrangler reset did not reproduce its ramped "
                "first sample at channel %u\n", channel);
            return false;
        }
    }
    if (settledEnergy <= 1.0e-8
        || settledPeak <= 1.0e-5f
        || firstPeak > 0.01f
        || !(firstPeak * 8.0f < settledPeak)) {
        std::fprintf(stderr,
            "Wrangler startup/reset ramp was ineffective: "
            "first=%g settled=%g energy=%g\n",
            firstPeak, settledPeak, settledEnergy);
        return false;
    }
    return true;
}

float relativeCurveStorage(float globalCenter, float target)
{
    globalCenter = std::clamp(globalCenter, 0.0f, 1.0f);
    target = std::clamp(target, 0.0f, 1.0f);
    if (std::fabs(target - globalCenter) <= 1.0e-7f) return 0.5f;
    if (target < globalCenter) {
        return globalCenter <= 1.0e-7f
            ? 0.5f
            : 0.5f * target / globalCenter;
    }
    return globalCenter >= 1.0f - 1.0e-7f
        ? 0.5f
        : 0.5f + 0.5f
            * (target - globalCenter) / (1.0f - globalCenter);
}

RenderResult renderPair(
    s3g::AmbiWranglerParams aParams,
    s3g::AmbiWranglerParams bParams)
{
    s3g::AmbiWranglerEncoder a;
    s3g::AmbiWranglerEncoder b;
    a.prepare(48000.0);
    b.prepare(48000.0);
    a.setParams(aParams);
    b.setParams(bParams);

    std::array<std::vector<float>, kChannels> aStorage;
    std::array<std::vector<float>, kChannels> bStorage;
    std::array<float*, kChannels> aOutputs {};
    std::array<float*, kChannels> bOutputs {};
    for (uint32_t channel = 0u; channel < kChannels; ++channel) {
        aStorage[channel].resize(kFrames);
        bStorage[channel].resize(kFrames);
        aOutputs[channel] = aStorage[channel].data();
        bOutputs[channel] = bStorage[channel].data();
    }

    RenderResult result;
    for (uint32_t block = 0u; block < kBlocks; ++block) {
        a.process(aOutputs.data(), kChannels, kFrames);
        b.process(bOutputs.data(), kChannels, kFrames);
        for (uint32_t channel = 0u; channel < kChannels; ++channel) {
            for (uint32_t frame = 0u; frame < kFrames; ++frame) {
                const float av = aStorage[channel][frame];
                const float bv = bStorage[channel][frame];
                if (!std::isfinite(av) || !std::isfinite(bv)) {
                    std::fprintf(stderr, "non-finite Wrangler output\n");
                    std::exit(2);
                }
                result.energy += static_cast<double>(av) * av
                    + static_cast<double>(bv) * bv;
                result.difference += std::fabs(static_cast<double>(av) - bv);
            }
        }
    }
    result.activity = b.listenerActivity();
    result.capture = b.voiceListenerCapture(0u);
    result.heldClocks = b.voiceHeldClockCount(0u);
    result.registerWord = b.voiceRegister(0u);
    return result;
}

DynamicsMetrics renderDynamics(
    s3g::AmbiWranglerParams params,
    uint32_t blocks = 1500u,
    uint32_t warmupBlocks = 300u)
{
    constexpr uint32_t kMetricChannels = 16u;
    constexpr uint32_t kLateLag = 4096u;
    s3g::AmbiWranglerEncoder encoder;
    encoder.prepare(48000.0);
    encoder.setParams(params);
    encoder.reset();

    std::array<std::vector<float>, kMetricChannels> storage;
    std::array<float*, kMetricChannels> outputs {};
    for (uint32_t channel = 0u; channel < kMetricChannels; ++channel) {
        storage[channel].resize(kFrames);
        outputs[channel] = storage[channel].data();
    }

    const uint32_t voices = std::clamp<uint32_t>(
        params.voices, 1u, s3g::kAmbiWranglerMaxVoices);
    std::array<uint32_t, s3g::kAmbiWranglerMaxVoices> previousRegister {};
    std::array<uint64_t, s3g::kAmbiWranglerMaxVoices> warmClockCount {};
    std::array<uint64_t, s3g::kAmbiWranglerMaxVoices> warmAdvanceCount {};
    std::array<uint64_t, s3g::kAmbiWranglerMaxVoices> warmHeldCount {};
    for (uint32_t voice = 0u; voice < voices; ++voice) {
        previousRegister[voice] = encoder.voiceRegister(voice);
    }

    std::array<float, kLateLag> lag {};
    uint32_t lagPosition = 0u;
    uint64_t measuredSamples = 0u;
    float previousAudio = 0.0f;
    bool havePreviousAudio = false;
    DynamicsMetrics result;
    const uint32_t mask = (1u << params.rungSize) - 1u;

    for (uint32_t block = 0u; block < blocks; ++block) {
        encoder.process(outputs.data(), kMetricChannels, kFrames);
        for (uint32_t channel = 0u; channel < kMetricChannels; ++channel) {
            for (float value : storage[channel]) {
                result.finite = result.finite && std::isfinite(value);
                result.peak = std::max(result.peak, std::fabs(value));
            }
        }

        if (block < warmupBlocks) {
            for (uint32_t voice = 0u; voice < voices; ++voice) {
                previousRegister[voice] = encoder.voiceRegister(voice);
                if (block + 1u == warmupBlocks) {
                    warmClockCount[voice] =
                        encoder.voiceClockCount(voice);
                    warmAdvanceCount[voice] =
                        encoder.voiceRegisterAdvanceCount(voice);
                    warmHeldCount[voice] =
                        encoder.voiceHeldClockCount(voice);
                }
            }
            continue;
        }

        for (float value : storage[0]) {
            result.energy += static_cast<double>(value) * value;
            if (havePreviousAudio) {
                const double difference = static_cast<double>(value) - previousAudio;
                result.differenceEnergy += difference * difference;
            }
            previousAudio = value;
            havePreviousAudio = true;
            if (measuredSamples >= kLateLag) {
                result.lateDifference += std::fabs(
                    static_cast<double>(value) - lag[lagPosition]);
                ++result.lateComparisons;
            }
            lag[lagPosition] = value;
            lagPosition = (lagPosition + 1u) % kLateLag;
            ++measuredSamples;
            ++result.samples;
        }

        for (uint32_t voice = 0u; voice < voices; ++voice) {
            const uint32_t current = encoder.voiceRegister(voice) & mask;
            const uint32_t previous = previousRegister[voice] & mask;
            if (current != previous) {
                ++result.registerTransitions;
                const uint32_t outgoing = (previous >> (params.rungSize - 1u)) & 1u;
                const uint32_t incoming = current & 1u;
                if (incoming != outgoing) ++result.registerNovelty;
            }
            previousRegister[voice] = current;
            result.capture = std::max(
                result.capture, encoder.voiceListenerCapture(voice));
            result.minimumEvolutionRate = std::min(
                result.minimumEvolutionRate,
                encoder.voiceListenerEvolutionRate(voice));
        }
    }
    for (uint32_t voice = 0u; voice < voices; ++voice) {
        result.clockEvents +=
            encoder.voiceClockCount(voice) - warmClockCount[voice];
        result.registerAdvances +=
            encoder.voiceRegisterAdvanceCount(voice)
                - warmAdvanceCount[voice];
        result.heldClocks +=
            encoder.voiceHeldClockCount(voice) - warmHeldCount[voice];
    }
    return result;
}

TransitionMetrics measureTransition(
    s3g::AmbiWranglerParams before,
    s3g::AmbiWranglerParams after)
{
    constexpr uint32_t kTransitionChannels = 16u;
    constexpr uint32_t kWarmBlocks = 320u;
    constexpr uint32_t kBaselineBlocks = 24u;
    constexpr uint32_t kTransitionBlocks = 96u;

    s3g::AmbiWranglerEncoder encoder;
    encoder.prepare(48000.0);
    encoder.setParams(before);
    encoder.reset();

    std::array<std::vector<float>, kTransitionChannels> storage;
    std::array<float*, kTransitionChannels> outputs {};
    std::array<float, kTransitionChannels> previous {};
    std::array<bool, kTransitionChannels> havePrevious {};
    for (uint32_t channel = 0u; channel < kTransitionChannels; ++channel) {
        storage[channel].resize(kFrames);
        outputs[channel] = storage[channel].data();
    }

    TransitionMetrics result;
    for (uint32_t block = 0u;
        block < kWarmBlocks + kBaselineBlocks; ++block) {
        encoder.process(outputs.data(), kTransitionChannels, kFrames);
        const bool measure = block >= kWarmBlocks;
        for (uint32_t channel = 0u;
            channel < kTransitionChannels; ++channel) {
            for (float value : storage[channel]) {
                result.finite = result.finite && std::isfinite(value);
                result.peak = std::max(result.peak, std::fabs(value));
                if (measure && havePrevious[channel]) {
                    result.baselineMaxStep = std::max(
                        result.baselineMaxStep,
                        std::fabs(value - previous[channel]));
                }
                previous[channel] = value;
                havePrevious[channel] = true;
            }
        }
    }

    encoder.setParams(after);
    for (uint32_t block = 0u; block < kTransitionBlocks; ++block) {
        encoder.process(outputs.data(), kTransitionChannels, kFrames);
        for (uint32_t channel = 0u;
            channel < kTransitionChannels; ++channel) {
            for (uint32_t frame = 0u; frame < kFrames; ++frame) {
                const float value = storage[channel][frame];
                result.finite = result.finite && std::isfinite(value);
                result.peak = std::max(result.peak, std::fabs(value));
                const float step = std::fabs(value - previous[channel]);
                if (block == 0u && frame == 0u) {
                    result.boundaryStep =
                        std::max(result.boundaryStep, step);
                }
                result.transitionMaxStep =
                    std::max(result.transitionMaxStep, step);
                result.energy += static_cast<double>(value) * value;
                previous[channel] = value;
            }
        }
    }
    return result;
}

bool transitionIsClickFree(
    const char* label,
    const s3g::AmbiWranglerParams& before,
    const s3g::AmbiWranglerParams& after)
{
    const auto metrics = measureTransition(before, after);
    const float boundaryLimit = std::max(
        0.075f, metrics.baselineMaxStep * 7.0f + 0.008f);
    const float transitionLimit = std::max(
        0.14f, metrics.baselineMaxStep * 10.0f + 0.012f);
    std::printf(
        "Wrangler automation %s: baseline %.5g, boundary %.5g, "
        "transition %.5g, peak %.5g\n",
        label, metrics.baselineMaxStep, metrics.boundaryStep,
        metrics.transitionMaxStep, metrics.peak);
    if (!metrics.finite || metrics.energy <= 1.0e-8
        || metrics.peak > 1.001f
        || metrics.boundaryStep > boundaryLimit
        || metrics.transitionMaxStep > transitionLimit) {
        std::fprintf(stderr,
            "%s automation clicked or became invalid: baseline=%g "
            "boundary=%g/%g transition=%g/%g peak=%g energy=%g\n",
            label, metrics.baselineMaxStep,
            metrics.boundaryStep, boundaryLimit,
            metrics.transitionMaxStep, transitionLimit,
            metrics.peak, metrics.energy);
        return false;
    }
    return true;
}

bool testCurveDimensionContract()
{
    using Dimension = s3g::AmbiWranglerCurveDimension;
    static constexpr std::array<Dimension, 24u> kExpectedOrder {
        Dimension::RateA,
        Dimension::RungA,
        Dimension::FmBtoA,
        Dimension::ColorA,
        Dimension::RateB,
        Dimension::RungB,
        Dimension::FmAtoB,
        Dimension::ColorB,
        Dimension::FilterFreqA,
        Dimension::FilterFreqB,
        Dimension::FilterRes,
        Dimension::FilterComp,
        Dimension::FilterType,
        Dimension::CrossA,
        Dimension::CrossB,
        Dimension::CrossLpf,
        Dimension::RungMode,
        Dimension::RungThresh,
        Dimension::RungSize,
        Dimension::PwmA,
        Dimension::PwmB,
        Dimension::RampA,
        Dimension::RampB,
        Dimension::Amp,
    };

    if (s3g::kAmbiWranglerHistoricalCurveDimensionCount != 19u
        || s3g::kAmbiWranglerCurveDimensionCount
            != kExpectedOrder.size()) {
        std::fprintf(stderr,
            "Wrangler curve row count is not the frozen 19 + 5 contract\n");
        return false;
    }
    for (uint32_t row = 0u; row < kExpectedOrder.size(); ++row) {
        if (static_cast<uint32_t>(kExpectedOrder[row]) != row) {
            std::fprintf(stderr,
                "Wrangler curve row order changed at row %u\n", row);
            return false;
        }
    }
    return true;
}

bool testFactoryCurveInitialization()
{
    using Curve = std::array<
        float, s3g::kAmbiWranglerMaxVoices>;
    for (uint32_t preset = 0u;
        preset < s3g::kAmbiWranglerFactoryPresetCount; ++preset) {
        const auto params = s3g::ambiWranglerFactoryPreset(preset);
        const std::array<const Curve*, 24u> curves {
            &params.bpRateA,
            &params.bpRunglerA,
            &params.bpFmBtoA,
            &params.bpColorA,
            &params.bpRateB,
            &params.bpRunglerB,
            &params.bpFmAtoB,
            &params.bpColorB,
            &params.bpFilter,
            &params.bpFilterFreqB,
            &params.bpFilterRes,
            &params.bpFilterComp,
            &params.bpFilterType,
            &params.bpCrossA,
            &params.bpCrossB,
            &params.bpCrossLpf,
            &params.bpRungMode,
            &params.bpThreshold,
            &params.bpRungSize,
            &params.bpPwmA,
            &params.bpPwmB,
            &params.bpRampA,
            &params.bpRampB,
            &params.bpAmp,
        };
        for (uint32_t row = 0u; row < curves.size(); ++row) {
            for (uint32_t voice = 0u;
                voice < s3g::kAmbiWranglerMaxVoices; ++voice) {
                const float value = (*curves[row])[voice];
                if (!std::isfinite(value)
                    || value < 0.0f || value > 1.0f) {
                    std::fprintf(stderr,
                        "factory preset %u curve row %u voice %u "
                        "is outside [0, 1]: %g\n",
                        preset, row, voice, value);
                    return false;
                }
            }
        }
        for (uint32_t voice = 0u;
            voice < s3g::kAmbiWranglerMaxVoices; ++voice) {
            if (params.bpColorA[voice] != 0.0f
                || params.bpColorB[voice] != 0.0f
                || params.bpFilterFreqB[voice] != 0.5f
                || params.bpFilterRes[voice] != 0.5f
                || params.bpFilterComp[voice] != 0.0f
                || params.bpFilterType[voice] != 0.5f
                || params.bpCrossA[voice] != 0.0f
                || params.bpCrossB[voice] != 0.0f
                || params.bpCrossLpf[voice] != 0.25f
                || params.bpRungMode[voice] != 0.0f
                || params.bpRungSize[voice] != 0.5f) {
                std::fprintf(stderr,
                    "factory preset %u did not neutralize new "
                    "curve rows at voice %u\n",
                    preset, voice);
                return false;
            }
        }
    }
    return true;
}

bool testRelativeCurveReachability()
{
    using Dimension = s3g::AmbiWranglerCurveDimension;
    auto params = s3g::ambiWranglerFactoryPreset(
        s3g::kAmbiWranglerCalmPresetStart);
    params.voices = 1u;
    params.engines = params.voices;
    params.voiceBreakpointsEnabled = true;
    fillNeutralCurves(params);

    s3g::AmbiWranglerEncoder encoder;
    encoder.prepare(48000.0);
    if (s3g::AmbiWranglerEncoder::curveRungMode(0.0f)
            != s3g::AmbiWranglerRungMode::Common
        || s3g::AmbiWranglerEncoder::curveRungMode(0.5f)
            != s3g::AmbiWranglerRungMode::Split
        || s3g::AmbiWranglerEncoder::curveRungMode(1.0f)
            != s3g::AmbiWranglerRungMode::Swap) {
        std::fprintf(stderr,
            "Rung Mode curve is not COMMON/SPLIT/SWAP at 0/.5/1\n");
        return false;
    }
    static constexpr std::array<float, 5u>
        kRepresentativeGlobalMorphs { 0.0f, 0.23f, 0.5f, 0.77f, 1.0f };
    for (float globalMorph : kRepresentativeGlobalMorphs) {
        params.filterMorph = globalMorph;
        float previous = -1.0f;
        for (uint32_t step = 0u; step <= 100u; ++step) {
            const float target = static_cast<float>(step) / 100.0f;
            params.bpFilterType[0] =
                relativeCurveStorage(globalMorph, target);
            const float mapped =
                s3g::AmbiWranglerEncoder::curveFilterMorph(
                    params.bpFilterType[0], params.filterMorph);
            if (std::fabs(mapped - target) > 1.0e-6f
                || (step > 0u && !(mapped > previous))) {
                std::fprintf(stderr,
                    "Filter Morph mapping was quantized, non-monotonic, "
                    "or could not reach %g from global %g: "
                    "storage=%g mapped=%g previous=%g\n",
                    target, globalMorph, params.bpFilterType[0],
                    mapped, previous);
                return false;
            }
            encoder.setParams(params);
            const float actual = encoder.voiceCurveValue(
                0u, Dimension::FilterType);
            if (std::fabs(actual - target) > 1.0e-6f) {
                std::fprintf(stderr,
                    "Filter Morph curve did not remain continuous at %g "
                    "from global %g: storage=%g actual=%g\n",
                    target, globalMorph,
                    params.bpFilterType[0], actual);
                return false;
            }
            previous = mapped;
        }
    }

    static constexpr std::array<uint32_t, 3u>
        kRepresentativeGlobalSizes { 2u, 5u, 8u };
    for (uint32_t globalSize : kRepresentativeGlobalSizes) {
        params.rungSize = globalSize;
        const float global =
            static_cast<float>(globalSize - 2u) / 6.0f;
        for (uint32_t targetSize = 2u;
            targetSize <= 8u; ++targetSize) {
            const float target =
                static_cast<float>(targetSize - 2u) / 6.0f;
            params.bpRungSize[0] =
                relativeCurveStorage(global, target);
            if (s3g::AmbiWranglerEncoder::curveRungSize(
                    params.bpRungSize[0],
                    globalSize) != targetSize) {
                std::fprintf(stderr,
                    "Rung Size mapping API could not reach size %u "
                    "from global size %u\n",
                    targetSize, globalSize);
                return false;
            }
            encoder.setParams(params);
            encoder.reset();
            if (encoder.voiceRungSize(0u) != targetSize) {
                std::fprintf(stderr,
                    "Rung Size curve could not reach size %u "
                    "from global size %u: storage=%g actual=%u\n",
                    targetSize, globalSize,
                    params.bpRungSize[0],
                    encoder.voiceRungSize(0u));
                return false;
            }
        }
    }
    return true;
}

bool testNeutralCurveIdentity()
{
    auto legacy = s3g::ambiWranglerFactoryPreset(14u);
    legacy.voices = 1u;
    legacy.engines = legacy.voices;
    legacy.listeningEnabled = 0u;
    legacy.topologyMotion = 0u;
    fillNeutralCurves(legacy);
    legacy.voiceBreakpointsEnabled = false;
    auto legacyCurves = legacy;
    legacyCurves.voiceBreakpointsEnabled = true;
    const auto legacyReference = renderAudioTrace(legacy);
    const auto legacyCurveTrace = renderAudioTrace(legacyCurves);
    const double legacyDifference =
        traceDifference(legacyReference, legacyCurveTrace);
    if (!legacyReference.finite || !legacyCurveTrace.finite
        || legacyReference.energy <= 1.0e-6
        || legacyDifference != 0.0) {
        std::fprintf(stderr,
            "neutral extended curves changed Legacy samples: "
            "energy=%g difference=%g\n",
            legacyReference.energy, legacyDifference);
        return false;
    }

    auto bounded = s3g::ambiWranglerFactoryPreset(
        s3g::kAmbiWranglerCalmPresetStart);
    bounded.voices = 1u;
    bounded.engines = bounded.voices;
    bounded.listeningEnabled = 0u;
    bounded.topologyMotion = 0u;
    fillNeutralCurves(bounded);
    bounded.voiceBreakpointsEnabled = false;
    auto boundedCurves = bounded;
    boundedCurves.voiceBreakpointsEnabled = true;
    const auto boundedReference = renderAudioTrace(bounded);
    const auto boundedCurveTrace = renderAudioTrace(boundedCurves);
    const double boundedDifference =
        traceDifference(boundedReference, boundedCurveTrace);
    if (!boundedReference.finite || !boundedCurveTrace.finite
        || boundedReference.energy <= 1.0e-6
        || boundedDifference != 0.0) {
        std::fprintf(stderr,
            "neutral extended curves changed Bounded output: "
            "energy=%g difference=%g\n",
            boundedReference.energy, boundedDifference);
        return false;
    }
    return true;
}

bool testLegacyIdentity()
{
    auto reference = s3g::ambiWranglerFactoryPreset(14u);
    reference.circuitLaw = s3g::AmbiWranglerCircuitLaw::Legacy;
    reference.listeningEnabled = 0u;

    auto boundedFieldsAreInert = reference;
    boundedFieldsAreInert.engines = 1u;
    boundedFieldsAreInert.change = 0.0f;
    boundedFieldsAreInert.listenerResponse =
        s3g::AmbiWranglerListenerResponse::Settle;
    boundedFieldsAreInert.settleAmount = 1.0f;
    boundedFieldsAreInert.settleTarget = 0.0f;
    boundedFieldsAreInert.settleRecoverySeconds = 8.0f;

    const auto comparison = renderPair(reference, boundedFieldsAreInert);
    if (comparison.energy <= 1.0e-5 || comparison.difference != 0.0) {
        std::fprintf(stderr,
            "legacy law changed when bounded-only fields changed: energy=%g difference=%g\n",
            comparison.energy, comparison.difference);
        return false;
    }
    return true;
}

bool testCalmPresetContract()
{
    static constexpr std::array<uint32_t, 5> kExpectedVoices {
        8u, 16u, 32u, 64u, 8u
    };
    static constexpr std::array<float, 5>
        kExpectedFilterMorphs { 0.0f, 0.5f, 0.0f, 1.0f, 0.0f };

    for (uint32_t variant = 0u; variant < kExpectedVoices.size(); ++variant) {
        const auto params = s3g::ambiWranglerFactoryPreset(
            s3g::kAmbiWranglerCalmPresetStart + variant);
        if (params.circuitLaw != s3g::AmbiWranglerCircuitLaw::Bounded
            || params.voices != kExpectedVoices[variant]
            || params.engines != params.voices
            || params.filterMorph != kExpectedFilterMorphs[variant]
            || params.listeningEnabled != (variant == 4u ? 1u : 0u)
            || params.voiceBreakpointsEnabled
            || params.fmAtoB > 0.10f
            || params.fmBtoA > 0.10f
            || params.runglerA > 0.16f
            || params.runglerB > 0.12f
            || params.spread > 0.065f
            || params.saturation > 0.10f) {
            std::fprintf(stderr, "calm preset %u violates its bounded contract\n", variant);
            return false;
        }

        s3g::AmbiWranglerEncoder encoder;
        encoder.prepare(48000.0);
        encoder.setParams(params);
        encoder.reset();
        if (encoder.engineCount() != params.voices) {
            std::fprintf(stderr,
                "calm preset %u did not expose one synth per voice\n",
                variant);
            return false;
        }
        for (uint32_t voice = 0u; voice < params.voices; ++voice) {
            if (encoder.nodeEngine(voice) != voice) {
                std::fprintf(stderr,
                    "voice %u in preset %u does not own synth %u\n",
                    voice, variant, encoder.nodeEngine(voice));
                return false;
            }
        }

        const auto metrics = renderDynamics(params, 360u, 80u);
        std::printf(
            "Wrangler calm preset %u: RMS %.5g, roughness %.6g, late %.5g\n",
            variant, metrics.rms(), metrics.roughness(),
            metrics.lateMotion());
        const double roughnessLimit =
            params.filterMorph >= 0.75f ? 0.05 : 0.005;
        if (!metrics.finite || metrics.peak > 1.001f
            || metrics.rms() <= 1.0e-5
            || metrics.roughness() >= roughnessLimit
            || metrics.lateMotion() <= 1.0e-6) {
            std::fprintf(stderr,
                "calm preset %u was silent, static, or invalid: rms=%g roughness=%g late=%g peak=%g\n",
                variant, metrics.rms(), metrics.roughness(),
                metrics.lateMotion(), metrics.peak);
            return false;
        }
    }

    auto legacy = s3g::ambiWranglerFactoryPreset(0u);
    legacy.engines = 1u;
    s3g::AmbiWranglerEncoder legacyEncoder;
    legacyEncoder.prepare(48000.0);
    legacyEncoder.setParams(legacy);
    if (legacyEncoder.engineCount() != legacy.voices) {
        std::fprintf(stderr, "legacy law no longer keeps one synth per voice\n");
        return false;
    }

    const auto open = s3g::ambiWranglerFactoryPreset(
        s3g::kAmbiWranglerCalmPresetStart);
    auto settledDisabled = s3g::ambiWranglerFactoryPreset(
        s3g::kAmbiWranglerCalmPresetStart + 4u);
    settledDisabled.listeningEnabled = 0u;
    const auto matched = renderPair(open, settledDisabled);
    if (matched.difference != 0.0) {
        std::fprintf(stderr,
            "Deep Current OFF/SETTLE demonstration is not parameter matched: %g\n",
            matched.difference);
        return false;
    }
    return true;
}

bool testBoundedExactZero()
{
    auto quietRegister = s3g::ambiWranglerFactoryPreset(
        s3g::kAmbiWranglerCalmPresetStart);
    quietRegister.voices = 8u;
    quietRegister.engines = quietRegister.voices;
    quietRegister.fmAtoB = 0.0f;
    quietRegister.fmBtoA = 0.0f;
    quietRegister.runglerA = 0.0f;
    quietRegister.runglerB = 0.0f;
    quietRegister.filterRun = 0.0f;
    quietRegister.snap = 0.0f;
    quietRegister.registerMotion = 0.0f;
    quietRegister.listeningEnabled = 0u;

    auto busyButDisconnectedRegister = quietRegister;
    quietRegister.change = 0.0f;
    quietRegister.rungLoop = 1u;
    quietRegister.rungSize = 2u;
    busyButDisconnectedRegister.change = 1.0f;
    busyButDisconnectedRegister.rungLoop = 2u;
    busyButDisconnectedRegister.rungSize = 8u;

    const auto comparison = renderPair(
        quietRegister, busyButDisconnectedRegister);
    if (comparison.energy <= 1.0e-5 || comparison.difference != 0.0) {
        std::fprintf(stderr,
            "bounded zero-depth modulation leaked register state into audio: energy=%g difference=%g\n",
            comparison.energy, comparison.difference);
        return false;
    }
    return true;
}

bool testOneVoiceOnePointContract()
{
    auto params = s3g::ambiWranglerFactoryPreset(
        s3g::kAmbiWranglerCalmPresetStart);
    params.voices = 8u;
    // A stale v11 engine field must not reintroduce shared synthesis.
    params.engines = 1u;
    params.voiceBreakpointsEnabled = true;
    params.topologyMotion = 0u;
    for (uint32_t voice = 0u;
        voice < s3g::kAmbiWranglerMaxVoices; ++voice) {
        const float lane = static_cast<float>(voice)
            / static_cast<float>(s3g::kAmbiWranglerMaxVoices - 1u);
        params.bpRateA[voice] = 0.12f + lane * 0.76f;
        params.bpAmp[voice] = 1.0f;
    }

    s3g::AmbiWranglerEncoder encoder;
    encoder.prepare(48000.0);
    encoder.setParams(params);
    encoder.reset();

    std::array<std::vector<float>, 4u> storage;
    std::array<float*, 4u> outputs {};
    for (uint32_t channel = 0u; channel < outputs.size(); ++channel) {
        storage[channel].resize(kFrames);
        outputs[channel] = storage[channel].data();
    }
    for (uint32_t block = 0u; block < 240u; ++block) {
        encoder.process(outputs.data(),
            static_cast<uint32_t>(outputs.size()), kFrames);
    }

    uint32_t distinctPhases = 1u;
    uint32_t distinctPoints = 1u;
    const float phase0 = encoder.voicePhaseA(0u);
    const auto point0 = encoder.voicePoint(0u);
    if (encoder.engineCount() != params.voices) {
        std::fprintf(stderr,
            "bounded mode retained %u shared engines for %u voices\n",
            encoder.engineCount(), params.voices);
        return false;
    }
    for (uint32_t voice = 0u; voice < params.voices; ++voice) {
        if (encoder.nodeEngine(voice) != voice) {
            std::fprintf(stderr,
                "voice %u was mapped to synth %u\n",
                voice, encoder.nodeEngine(voice));
            return false;
        }
        if (voice > 0u
            && std::fabs(encoder.voicePhaseA(voice) - phase0) > 1.0e-4f) {
            ++distinctPhases;
        }
        if (voice > 0u) {
            const auto point = encoder.voicePoint(voice);
            if (std::fabs(point.azimuthDeg - point0.azimuthDeg) > 0.01f
                || std::fabs(point.elevationDeg - point0.elevationDeg)
                    > 0.01f) {
                ++distinctPoints;
            }
        }
    }
    if (distinctPhases < 4u || distinctPoints < 4u) {
        std::fprintf(stderr,
            "voices did not own independent synth/point state: "
            "phases=%u points=%u\n",
            distinctPhases, distinctPoints);
        return false;
    }
    return true;
}

bool testVoiceCurveAddressing()
{
    auto reference = s3g::ambiWranglerFactoryPreset(
        s3g::kAmbiWranglerCalmPresetStart + 2u);
    reference.voices = 32u;
    reference.engines = reference.voices;
    reference.voiceBreakpointsEnabled = true;
    reference.bpRateA.fill(0.5f);
    reference.bpRateB.fill(0.5f);
    reference.bpFmAtoB.fill(0.5f);
    reference.bpFmBtoA.fill(0.5f);
    reference.bpRunglerA.fill(0.5f);
    reference.bpRunglerB.fill(0.5f);
    reference.bpFilter.fill(0.5f);
    reference.bpThreshold.fill(0.5f);
    reference.bpPwmA.fill(0.5f);
    reference.bpPwmB.fill(0.5f);
    reference.bpRampA.fill(0.5f);
    reference.bpRampB.fill(0.5f);
    reference.bpAmp.fill(1.0f);

    auto firstVoiceMuted = reference;
    firstVoiceMuted.bpAmp[0] = 0.0f;
    const auto comparison = renderPair(reference, firstVoiceMuted);
    if (comparison.energy <= 1.0e-5 || comparison.difference < 1.0) {
        std::fprintf(stderr,
            "bounded curve slot V1 did not address voice 1: energy=%g difference=%g\n",
            comparison.energy, comparison.difference);
        return false;
    }
    return true;
}

bool testExtendedHistoricalCurveRows()
{
    auto base = s3g::ambiWranglerFactoryPreset(
        s3g::kAmbiWranglerCalmPresetStart + 1u);
    base.voices = 4u;
    base.engines = base.voices;
    base.voiceBreakpointsEnabled = true;
    base.topologyMotion = 0u;
    base.maskMode = 0u;
    base.maskDepth = 0.0f;
    base.listeningEnabled = 0u;
    base.rateA = 0.58f;
    base.rateB = 0.49f;
    base.rateModeA = 1u;
    base.rateModeB = 1u;
    base.fmAtoB = 0.36f;
    base.fmBtoA = 0.31f;
    base.runglerA = 0.68f;
    base.runglerB = 0.62f;
    base.rungSize = 4u;
    base.rungLoop = 0u;
    base.change = 1.0f;
    base.inputA = 0u;
    base.inputB = 0u;
    base.color = 0.46f;
    base.filter = 0.48f;
    base.resonance = 0.46f;
    base.filterRun = 0.58f;
    base.filterSweep = 0.22f;
    base.saturation = 0.12f;
    base.filterMorph = 0.25f;
    base.outputGainDb = -8.0f;
    fillNeutralCurves(base);
    base.bpAmp.fill(0.0f);
    base.bpAmp[0] = 1.0f;

    struct CurveCase {
        const char* label;
        void (*prepare)(s3g::AmbiWranglerParams&);
        void (*vary)(s3g::AmbiWranglerParams&);
    };
    static constexpr std::array<CurveCase, 11u> kCases {{
        { "Color A", nullptr,
            +[](s3g::AmbiWranglerParams& p) {
                p.bpColorA[0] = 1.0f;
            } },
        { "Color B", nullptr,
            +[](s3g::AmbiWranglerParams& p) {
                p.bpColorB[0] = 1.0f;
            } },
        { "Filter Freq B", nullptr,
            +[](s3g::AmbiWranglerParams& p) {
                p.bpFilterFreqB[0] = 1.0f;
            } },
        { "Filter Res", nullptr,
            +[](s3g::AmbiWranglerParams& p) {
                p.bpFilterRes[0] = 1.0f;
            } },
        { "Filter Comp", nullptr,
            +[](s3g::AmbiWranglerParams& p) {
                p.bpFilterComp[0] = 1.0f;
            } },
        { "Filter Morph", nullptr,
            +[](s3g::AmbiWranglerParams& p) {
                p.bpFilterType[0] = 1.0f;
            } },
        { "Cross A", nullptr,
            +[](s3g::AmbiWranglerParams& p) {
                p.bpCrossA[0] = 1.0f;
            } },
        { "Cross B", nullptr,
            +[](s3g::AmbiWranglerParams& p) {
                p.bpCrossB[0] = 1.0f;
            } },
        { "Cross LPF",
            +[](s3g::AmbiWranglerParams& p) {
                p.bpCrossA[0] = 0.85f;
                p.bpCrossB[0] = 0.75f;
            },
            +[](s3g::AmbiWranglerParams& p) {
                p.bpCrossLpf[0] = 1.0f;
            } },
        { "Rung Mode", nullptr,
            +[](s3g::AmbiWranglerParams& p) {
                p.bpRungMode[0] = 1.0f;
            } },
        { "Rung Size", nullptr,
            +[](s3g::AmbiWranglerParams& p) {
                p.bpRungSize[0] = 1.0f;
            } },
    }};

    const auto sharedReference = renderAudioTrace(base);
    for (const auto& curveCase : kCases) {
        auto referenceParams = base;
        if (curveCase.prepare) curveCase.prepare(referenceParams);
        auto variedParams = referenceParams;
        curveCase.vary(variedParams);
        AudioTrace preparedReference;
        const AudioTrace* reference = &sharedReference;
        if (curveCase.prepare) {
            preparedReference = renderAudioTrace(referenceParams);
            reference = &preparedReference;
        }
        const auto varied = renderAudioTrace(variedParams);
        const double difference =
            traceDifference(*reference, varied);
        std::printf(
            "Wrangler curve %-13s: rms %.5g -> %.5g, "
            "mean difference %.6g\n",
            curveCase.label, reference->rms(), varied.rms(),
            difference);
        if (!reference->finite || !varied.finite
            || reference->peak > 1.001f || varied.peak > 1.001f
            || reference->rms() <= 1.0e-6
            || varied.rms() <= 1.0e-6
            || difference <= 1.0e-7) {
            std::fprintf(stderr,
                "%s curve row was silent, invalid, or inaudible: "
                "rms=%g/%g peak=%g/%g difference=%g\n",
                curveCase.label, reference->rms(), varied.rms(),
                reference->peak, varied.peak, difference);
            return false;
        }
    }
    return true;
}

bool testHistoricalCurveTransitionSafety()
{
    auto base = s3g::ambiWranglerFactoryPreset(
        s3g::kAmbiWranglerCalmPresetStart);
    base.voices = 1u;
    base.engines = 1u;
    base.voiceBreakpointsEnabled = true;
    base.topologyMotion = 0u;
    base.maskMode = 0u;
    base.maskDepth = 0.0f;
    base.listeningEnabled = 0u;
    base.rateA = 0.58f;
    base.rateB = 0.47f;
    base.rateModeA = 1u;
    base.rateModeB = 1u;
    base.fmAtoB = 0.28f;
    base.fmBtoA = 0.24f;
    base.runglerA = 0.58f;
    base.runglerB = 0.52f;
    base.rungSize = 5u;
    base.rungLoop = 0u;
    base.change = 1.0f;
    base.inputA = 1u;
    base.inputB = 1u;
    base.color = 0.48f;
    base.filter = 0.44f;
    base.resonance = 0.42f;
    base.filterRun = 0.36f;
    base.filterSweep = 0.18f;
    base.saturation = 0.10f;
    base.filterMorph = 0.25f;
    base.outputGainDb = -10.0f;
    fillNeutralCurves(base);

    struct CurveCase {
        const char* label;
        void (*prepare)(s3g::AmbiWranglerParams&);
        void (*setLow)(s3g::AmbiWranglerParams&);
        void (*setHigh)(s3g::AmbiWranglerParams&);
        bool structural;
    };
    static constexpr std::array<CurveCase, 11u> kCases {{
        { "Color A", nullptr,
            +[](s3g::AmbiWranglerParams& p) {
                p.bpColorA[0] = 0.0f;
            },
            +[](s3g::AmbiWranglerParams& p) {
                p.bpColorA[0] = 1.0f;
            }, false },
        { "Color B", nullptr,
            +[](s3g::AmbiWranglerParams& p) {
                p.bpColorB[0] = 0.0f;
            },
            +[](s3g::AmbiWranglerParams& p) {
                p.bpColorB[0] = 1.0f;
            }, false },
        { "Filter Freq B", nullptr,
            +[](s3g::AmbiWranglerParams& p) {
                p.bpFilterFreqB[0] = 0.0f;
            },
            +[](s3g::AmbiWranglerParams& p) {
                p.bpFilterFreqB[0] = 1.0f;
            }, false },
        { "Filter Res", nullptr,
            +[](s3g::AmbiWranglerParams& p) {
                p.bpFilterRes[0] = 0.0f;
            },
            +[](s3g::AmbiWranglerParams& p) {
                p.bpFilterRes[0] = 1.0f;
            }, false },
        { "Filter Comp", nullptr,
            +[](s3g::AmbiWranglerParams& p) {
                p.bpFilterComp[0] = 0.0f;
            },
            +[](s3g::AmbiWranglerParams& p) {
                p.bpFilterComp[0] = 1.0f;
            }, false },
        { "Filter Morph", nullptr,
            +[](s3g::AmbiWranglerParams& p) {
                p.bpFilterType[0] = 0.0f;
            },
            +[](s3g::AmbiWranglerParams& p) {
                p.bpFilterType[0] = 1.0f;
            }, false },
        { "Cross A", nullptr,
            +[](s3g::AmbiWranglerParams& p) {
                p.bpCrossA[0] = 0.0f;
            },
            +[](s3g::AmbiWranglerParams& p) {
                p.bpCrossA[0] = 1.0f;
            }, false },
        { "Cross B", nullptr,
            +[](s3g::AmbiWranglerParams& p) {
                p.bpCrossB[0] = 0.0f;
            },
            +[](s3g::AmbiWranglerParams& p) {
                p.bpCrossB[0] = 1.0f;
            }, false },
        { "Cross LPF",
            +[](s3g::AmbiWranglerParams& p) {
                p.bpCrossA[0] = 0.75f;
                p.bpCrossB[0] = 0.70f;
            },
            +[](s3g::AmbiWranglerParams& p) {
                p.bpCrossLpf[0] = 0.0f;
            },
            +[](s3g::AmbiWranglerParams& p) {
                p.bpCrossLpf[0] = 1.0f;
            }, false },
        { "Rung Mode", nullptr,
            +[](s3g::AmbiWranglerParams& p) {
                p.bpRungMode[0] = 0.0f;
            },
            +[](s3g::AmbiWranglerParams& p) {
                p.bpRungMode[0] = 1.0f;
            }, true },
        { "Rung Size", nullptr,
            +[](s3g::AmbiWranglerParams& p) {
                p.bpRungSize[0] = 0.0f;
            },
            +[](s3g::AmbiWranglerParams& p) {
                p.bpRungSize[0] = 1.0f;
            }, true },
    }};

    static constexpr std::array<s3g::AmbiWranglerCircuitLaw, 2u>
        kLaws {
            s3g::AmbiWranglerCircuitLaw::Legacy,
            s3g::AmbiWranglerCircuitLaw::Bounded,
        };
    bool passed = true;
    for (const auto law : kLaws) {
        const char* lawName =
            law == s3g::AmbiWranglerCircuitLaw::Legacy
            ? "Legacy" : "Bounded";
        for (const auto& curveCase : kCases) {
            auto low = base;
            low.circuitLaw = law;
            if (curveCase.prepare) curveCase.prepare(low);
            curveCase.setLow(low);
            auto high = low;
            curveCase.setHigh(high);

            const auto forward =
                measureDifferentialTransition(low, high);
            const auto reverse =
                measureDifferentialTransition(high, low);
            const float boundaryLimit =
                curveCase.structural ? 1.0e-7f : 0.0015f;
            const float earlyStepLimit =
                curveCase.structural ? 0.004f : 0.006f;
            std::printf(
                "Wrangler differential %-7s %-13s: "
                "boundary %.6g/%.6g, early %.6g/%.6g, "
                "residual %.6g/%.6g\n",
                lawName, curveCase.label,
                forward.boundaryResidual, reverse.boundaryResidual,
                forward.earlyResidualStep, reverse.earlyResidualStep,
                forward.maximumResidual, reverse.maximumResidual);
            const auto invalid = [&](const auto& metrics) {
                return !metrics.finite || !metrics.clockAligned
                    || metrics.preTransitionMismatch != 0.0f
                    || metrics.outputEnergy <= 1.0e-9
                    // A structural edit is deliberately inaudible until a
                    // later genuine register clock. The dedicated clock
                    // test below proves both directions commit; this probe
                    // is concerned with silence before that commit and the
                    // absence of an artificial boundary correction.
                    || (!curveCase.structural
                        && metrics.residualEnergy <= 1.0e-16)
                    || metrics.peak > 1.001f
                    || metrics.boundaryResidual > boundaryLimit
                    || metrics.earlyResidualStep > earlyStepLimit;
            };
            if (invalid(forward) || invalid(reverse)) {
                std::fprintf(stderr,
                    "%s %s curve automation failed the twin-encoder "
                    "click probe: pre=%g/%g aligned=%u/%u "
                    "boundary=%g/%g (limit %g), early=%g/%g "
                    "(limit %g), peak=%g/%g residualEnergy=%g/%g\n",
                    lawName, curveCase.label,
                    forward.preTransitionMismatch,
                    reverse.preTransitionMismatch,
                    forward.clockAligned ? 1u : 0u,
                    reverse.clockAligned ? 1u : 0u,
                    forward.boundaryResidual,
                    reverse.boundaryResidual, boundaryLimit,
                    forward.earlyResidualStep,
                    reverse.earlyResidualStep, earlyStepLimit,
                    forward.peak, reverse.peak,
                    forward.residualEnergy,
                    reverse.residualEnergy);
                passed = false;
            }
        }
    }

    struct GlobalCase {
        const char* label;
        void (*setLow)(s3g::AmbiWranglerParams&);
        void (*setHigh)(s3g::AmbiWranglerParams&);
    };
    static constexpr std::array<GlobalCase, 4u> kGlobalCases {{
        { "Filter Morph",
            +[](s3g::AmbiWranglerParams& p) {
                p.filterMorph = 0.0f;
            },
            +[](s3g::AmbiWranglerParams& p) {
                p.filterMorph = 1.0f;
            } },
        { "Filter Freq",
            +[](s3g::AmbiWranglerParams& p) {
                p.filter = 0.08f;
            },
            +[](s3g::AmbiWranglerParams& p) {
                p.filter = 0.90f;
            } },
        { "Filter Res",
            +[](s3g::AmbiWranglerParams& p) {
                p.resonance = 0.05f;
            },
            +[](s3g::AmbiWranglerParams& p) {
                p.resonance = 0.95f;
            } },
        { "Color",
            +[](s3g::AmbiWranglerParams& p) {
                p.color = 0.0f;
            },
            +[](s3g::AmbiWranglerParams& p) {
                p.color = 1.0f;
            } },
    }};
    for (const auto law : kLaws) {
        const char* lawName =
            law == s3g::AmbiWranglerCircuitLaw::Legacy
            ? "Legacy" : "Bounded";
        for (const auto& globalCase : kGlobalCases) {
            auto low = base;
            low.circuitLaw = law;
            globalCase.setLow(low);
            auto high = low;
            globalCase.setHigh(high);
            const auto forward =
                measureDifferentialTransition(low, high);
            const auto reverse =
                measureDifferentialTransition(high, low);
            if (!forward.finite || !reverse.finite
                || !forward.clockAligned || !reverse.clockAligned
                || forward.preTransitionMismatch != 0.0f
                || reverse.preTransitionMismatch != 0.0f
                || forward.outputEnergy <= 1.0e-9
                || reverse.outputEnergy <= 1.0e-9
                || forward.residualEnergy <= 1.0e-16
                || reverse.residualEnergy <= 1.0e-16
                || forward.peak > 1.001f || reverse.peak > 1.001f
                || forward.boundaryResidual > 0.0015f
                || reverse.boundaryResidual > 0.0015f
                || forward.earlyResidualStep > 0.006f
                || reverse.earlyResidualStep > 0.006f) {
                std::fprintf(stderr,
                    "%s global %s automation failed the differential "
                    "click probe: boundary=%g/%g early=%g/%g "
                    "peak=%g/%g energy=%g/%g\n",
                    lawName, globalCase.label,
                    forward.boundaryResidual,
                    reverse.boundaryResidual,
                    forward.earlyResidualStep,
                    reverse.earlyResidualStep,
                    forward.peak, reverse.peak,
                    forward.residualEnergy,
                    reverse.residualEnergy);
                passed = false;
            }
        }
    }
    return passed;
}

bool testHistoricalCrossDirectionBindings()
{
    auto base = s3g::ambiWranglerFactoryPreset(
        s3g::kAmbiWranglerCalmPresetStart);
    base.voices = 1u;
    base.engines = 1u;
    base.circuitLaw = s3g::AmbiWranglerCircuitLaw::Bounded;
    base.voiceBreakpointsEnabled = true;
    base.topologyMotion = 0u;
    base.listeningEnabled = 0u;
    base.field = 0.0f;
    base.spread = 0.0f;
    base.deviation = 0.0f;
    base.fmAtoB = 0.0f;
    base.fmBtoA = 0.0f;
    base.runglerA = 0.0f;
    base.runglerB = 0.0f;
    base.rateA = 0.56f;
    base.rateB = 0.47f;
    base.filter = 0.58f;
    base.resonance = 0.40f;
    base.filterMorph = 0.5f;
    fillNeutralCurves(base);
    base.bpCrossLpf.fill(1.0f);

    auto finalPhases = [](const s3g::AmbiWranglerParams& params) {
        s3g::AmbiWranglerEncoder encoder;
        encoder.prepare(48000.0);
        encoder.setParams(params);
        encoder.reset();
        std::array<std::vector<float>, 4u> storage;
        std::array<float*, 4u> outputs {};
        for (uint32_t channel = 0u; channel < outputs.size(); ++channel) {
            storage[channel].resize(kFrames);
            outputs[channel] = storage[channel].data();
        }
        for (uint32_t block = 0u; block < 320u; ++block) {
            encoder.process(outputs.data(),
                static_cast<uint32_t>(outputs.size()), kFrames);
        }
        return std::array<float, 2u> {
            encoder.voicePhaseA(0u), encoder.voicePhaseB(0u)
        };
    };
    auto phaseDistance = [](float a, float b) {
        const float difference = std::fabs(a - b);
        return std::min(difference, 1.0f - difference);
    };

    const auto reference = finalPhases(base);
    auto crossA = base;
    crossA.bpCrossA[0] = 1.0f;
    const auto crossAPhases = finalPhases(crossA);
    auto crossB = base;
    crossB.bpCrossB[0] = 1.0f;
    const auto crossBPhases = finalPhases(crossB);

    const float crossAOnA =
        phaseDistance(reference[0], crossAPhases[0]);
    const float crossAOnB =
        phaseDistance(reference[1], crossAPhases[1]);
    const float crossBOnA =
        phaseDistance(reference[0], crossBPhases[0]);
    const float crossBOnB =
        phaseDistance(reference[1], crossBPhases[1]);
    if (crossAOnA != 0.0f || crossAOnB <= 1.0e-4f
        || crossBOnA <= 1.0e-4f || crossBOnB != 0.0f) {
        std::fprintf(stderr,
            "historical cross bindings were reversed or coupled: "
            "Cross A -> A/B %.7g/%.7g, Cross B -> A/B %.7g/%.7g\n",
            crossAOnA, crossAOnB, crossBOnA, crossBOnB);
        return false;
    }
    return true;
}

bool testPerVoiceRungStructure()
{
    auto params = s3g::ambiWranglerFactoryPreset(
        s3g::kAmbiWranglerCalmPresetStart);
    params.voices = 3u;
    params.engines = params.voices;
    params.voiceBreakpointsEnabled = true;
    params.topologyMotion = 0u;
    params.maskMode = 0u;
    params.maskDepth = 0.0f;
    params.listeningEnabled = 0u;
    params.rateA = 0.64f;
    params.rateB = 0.72f;
    params.rateModeA = 1u;
    params.rateModeB = 1u;
    params.runglerA = 0.72f;
    params.runglerB = 0.66f;
    params.rungSize = 5u;
    params.change = 1.0f;
    fillNeutralCurves(params);
    params.bpRungMode[0] = 0.0f;
    params.bpRungMode[1] = 0.5f;
    params.bpRungMode[2] = 1.0f;
    params.bpRungSize[0] = 0.0f;
    params.bpRungSize[1] = 0.5f;
    params.bpRungSize[2] = 1.0f;

    s3g::AmbiWranglerEncoder encoder;
    encoder.prepare(48000.0);
    encoder.setParams(params);
    encoder.reset();
    const uint32_t initialMode0 = encoder.voiceRungMode(0u);
    const uint32_t initialMode1 = encoder.voiceRungMode(1u);
    const uint32_t initialMode2 = encoder.voiceRungMode(2u);
    const uint32_t initialSize0 = encoder.voiceRungSize(0u);
    const uint32_t initialSize1 = encoder.voiceRungSize(1u);
    const uint32_t initialSize2 = encoder.voiceRungSize(2u);

    std::array<std::vector<float>, 4u> storage;
    std::array<float*, 4u> outputs {};
    for (uint32_t channel = 0u; channel < outputs.size(); ++channel) {
        storage[channel].resize(kFrames);
        outputs[channel] = storage[channel].data();
    }
    double energy = 0.0;
    bool finite = true;
    for (uint32_t block = 0u; block < 200u; ++block) {
        encoder.process(outputs.data(),
            static_cast<uint32_t>(outputs.size()), kFrames);
        for (const auto& channel : storage) {
            for (float value : channel) {
                finite = finite && std::isfinite(value);
                energy += static_cast<double>(value) * value;
            }
        }
    }

    std::printf(
        "Wrangler per-voice rung structure: mode %u/%u/%u, "
        "size %u/%u/%u, clocks %llu/%llu/%llu\n",
        encoder.voiceRungMode(0u), encoder.voiceRungMode(1u),
        encoder.voiceRungMode(2u),
        encoder.voiceRungSize(0u), encoder.voiceRungSize(1u),
        encoder.voiceRungSize(2u),
        static_cast<unsigned long long>(
            encoder.voiceClockCount(0u)),
        static_cast<unsigned long long>(
            encoder.voiceClockCount(1u)),
        static_cast<unsigned long long>(
            encoder.voiceClockCount(2u)));
    if (!finite || energy <= 1.0e-7
        || initialMode0 != 0u || initialMode1 != 1u
        || initialMode2 != 2u
        || initialSize0 != 2u || initialSize1 != 5u
        || initialSize2 != 8u
        || encoder.voiceRungMode(0u) != initialMode0
        || encoder.voiceRungMode(1u) != initialMode1
        || encoder.voiceRungMode(2u) != initialMode2
        || encoder.voiceRungSize(0u) != initialSize0
        || encoder.voiceRungSize(1u) != initialSize1
        || encoder.voiceRungSize(2u) != initialSize2
        || encoder.voiceClockCount(0u) == 0u
        || encoder.voiceClockCount(1u) == 0u
        || encoder.voiceClockCount(2u) == 0u
        || encoder.voiceRegisterAdvanceCount(0u) == 0u
        || encoder.voiceRegisterAdvanceCount(1u) == 0u
        || encoder.voiceRegisterAdvanceCount(2u) == 0u) {
        std::fprintf(stderr,
            "per-voice rung mode/size did not remain independent, "
            "stable, active, and nonzero\n");
        return false;
    }
    return true;
}

bool testClockAlignedRungStructureAutomation()
{
    static constexpr std::array<s3g::AmbiWranglerCircuitLaw, 2u>
        kLaws {
            s3g::AmbiWranglerCircuitLaw::Legacy,
            s3g::AmbiWranglerCircuitLaw::Bounded,
        };
    for (const auto law : kLaws) {
        auto low = s3g::ambiWranglerFactoryPreset(
            s3g::kAmbiWranglerCalmPresetStart);
        low.voices = 1u;
        low.engines = 1u;
        low.circuitLaw = law;
        low.voiceBreakpointsEnabled = true;
        low.topologyMotion = 0u;
        low.maskMode = 0u;
        low.maskDepth = 0.0f;
        low.listeningEnabled = 0u;
        low.rateA = 0.55f;
        low.rateB = 0.58f;
        low.rateModeA = 1u;
        low.rateModeB = 1u;
        low.inputB = 1u;
        low.rungSize = 5u;
        low.change = 1.0f;
        fillNeutralCurves(low);
        low.bpRungMode[0] = 0.0f;
        low.bpRungSize[0] = 0.0f;
        auto high = low;
        high.bpRungMode[0] = 1.0f;
        high.bpRungSize[0] = 1.0f;

        s3g::AmbiWranglerEncoder encoder;
        encoder.prepare(48000.0);
        encoder.setParams(low);
        encoder.reset();
        std::array<float, 1u> sample {};
        std::array<float*, 1u> output { sample.data() };

        const uint64_t initialClock =
            encoder.voiceClockCount(0u);
        bool aligned = false;
        for (uint32_t frame = 0u; frame < 262144u; ++frame) {
            encoder.process(output.data(), 1u, 1u);
            if (!std::isfinite(sample[0])) {
                std::fprintf(stderr,
                    "non-finite output while aligning rung structure\n");
                return false;
            }
            if (encoder.voiceClockCount(0u) > initialClock) {
                aligned = true;
                break;
            }
        }
        if (!aligned || encoder.voiceRungMode(0u) != 0u
            || encoder.voiceRungSize(0u) != 2u) {
            std::fprintf(stderr,
                "could not establish initial clock-aligned rung state "
                "for law %u\n", static_cast<uint32_t>(law));
            return false;
        }

        const auto waitForCommit =
            [&](const s3g::AmbiWranglerParams& target,
                uint32_t oldMode, uint32_t oldSize,
                uint32_t newMode, uint32_t newSize) {
                const uint64_t startingClock =
                    encoder.voiceClockCount(0u);
                encoder.setParams(target);
                if (encoder.voiceRungMode(0u) != oldMode
                    || encoder.voiceRungSize(0u) != oldSize) {
                    return false;
                }
                for (uint32_t frame = 0u;
                    frame < 262144u; ++frame) {
                    encoder.process(output.data(), 1u, 1u);
                    if (!std::isfinite(sample[0])) return false;
                    const uint64_t clock =
                        encoder.voiceClockCount(0u);
                    if (clock == startingClock) {
                        if (encoder.voiceRungMode(0u) != oldMode
                            || encoder.voiceRungSize(0u) != oldSize) {
                            return false;
                        }
                        continue;
                    }
                    return clock == startingClock + 1u
                        && encoder.voiceRungMode(0u) == newMode
                        && encoder.voiceRungSize(0u) == newSize;
                }
                return false;
            };

        if (!waitForCommit(high, 0u, 2u, 2u, 8u)
            || !waitForCommit(low, 2u, 8u, 0u, 2u)) {
            std::fprintf(stderr,
                "Rung Mode/Size did not commit exclusively on the "
                "next clock edge in law %u\n",
                static_cast<uint32_t>(law));
            return false;
        }
    }
    return true;
}

bool testContinuousFilterMorph()
{
    auto base = s3g::ambiWranglerFactoryPreset(
        s3g::kAmbiWranglerCalmPresetStart);
    base.voices = 1u;
    base.engines = 1u;
    base.circuitLaw = s3g::AmbiWranglerCircuitLaw::Bounded;
    base.voiceBreakpointsEnabled = false;
    base.topologyMotion = 0u;
    base.maskMode = 0u;
    base.maskDepth = 0.0f;
    base.listeningEnabled = 0u;
    base.filter = 0.38f;
    base.resonance = 0.42f;
    base.filterRun = 0.10f;
    base.filterSweep = 0.05f;
    base.color = 0.52f;
    base.saturation = 0.08f;
    base.change = 0.30f;
    base.outputGainDb = -8.0f;

    static constexpr std::array<float, 5u>
        kMorphs { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };
    std::array<AudioTrace, kMorphs.size()> globalTraces;
    std::array<AudioTrace, kMorphs.size()> curveTraces;
    std::array<double, kMorphs.size() - 1u> adjacentDifferences {};
    for (uint32_t index = 0u; index < kMorphs.size(); ++index) {
        auto global = base;
        global.filterMorph = kMorphs[index];
        globalTraces[index] = renderAudioTrace(global, 180u, 56u);

        auto curve = base;
        curve.filterMorph = 0.5f;
        curve.voiceBreakpointsEnabled = true;
        fillNeutralCurves(curve);
        curve.bpFilterType[0] = kMorphs[index];
        curveTraces[index] = renderAudioTrace(curve, 180u, 56u);
        const double mappingDifference =
            traceDifference(globalTraces[index], curveTraces[index]);
        if (!globalTraces[index].finite
            || !curveTraces[index].finite
            || globalTraces[index].peak > 1.001f
            || curveTraces[index].peak > 1.001f
            || globalTraces[index].rms() <= 1.0e-6
            || curveTraces[index].rms() <= 1.0e-6
            || mappingDifference != 0.0) {
            std::fprintf(stderr,
                "continuous Filter Morph position %g was silent, "
                "invalid, clipped, or differed between global and "
                "per-voice mapping: rms=%g/%g peak=%g/%g diff=%g\n",
                kMorphs[index], globalTraces[index].rms(),
                curveTraces[index].rms(), globalTraces[index].peak,
                curveTraces[index].peak, mappingDifference);
            return false;
        }
        if (index > 0u) {
            adjacentDifferences[index - 1u] =
                traceDifference(
                    globalTraces[index - 1u], globalTraces[index]);
            if (adjacentDifferences[index - 1u] <= 1.0e-5) {
                std::fprintf(stderr,
                    "continuous Filter Morph positions %g and %g "
                    "were not audibly distinct: difference=%g\n",
                    kMorphs[index - 1u], kMorphs[index],
                    adjacentDifferences[index - 1u]);
                return false;
            }
        }
    }
    std::printf(
        "Wrangler DJ filter LP/.25/OPEN/.75/HP rms "
        "%.5g/%.5g/%.5g/%.5g/%.5g; adjacent diff "
        "%.6g/%.6g/%.6g/%.6g\n",
        globalTraces[0].rms(), globalTraces[1].rms(),
        globalTraces[2].rms(), globalTraces[3].rms(),
        globalTraces[4].rms(), adjacentDifferences[0],
        adjacentDifferences[1], adjacentDifferences[2],
        adjacentDifferences[3]);

    // Filter Morph is a continuous audio-rate target, not a clock-latched
    // register structure. With both oscillators forced into their lowest
    // range it must become audible while the clock count remains unchanged.
    auto slowMorph = base;
    slowMorph.voiceBreakpointsEnabled = true;
    fillNeutralCurves(slowMorph);
    slowMorph.filterMorph = 0.5f;
    slowMorph.bpFilterType[0] = 0.25f;
    slowMorph.rateA = 0.0f;
    slowMorph.rateB = 0.0f;
    slowMorph.rateModeA = 0u;
    slowMorph.rateModeB = 0u;
    slowMorph.inputB = 1u;
    s3g::AmbiWranglerEncoder heldReference;
    s3g::AmbiWranglerEncoder movingMorph;
    heldReference.prepare(48000.0);
    movingMorph.prepare(48000.0);
    heldReference.setParams(slowMorph);
    movingMorph.setParams(slowMorph);
    heldReference.reset();
    movingMorph.reset();
    std::array<float, 1u> referenceSample {};
    std::array<float, 1u> movingSample {};
    std::array<float*, 1u> referenceOutput {
        referenceSample.data()
    };
    std::array<float*, 1u> movingOutput { movingSample.data() };
    for (uint32_t sample = 0u; sample < 4096u; ++sample) {
        heldReference.process(referenceOutput.data(), 1u, 1u);
        movingMorph.process(movingOutput.data(), 1u, 1u);
    }
    const uint64_t referenceClock =
        heldReference.voiceClockCount(0u);
    const uint64_t movingClock =
        movingMorph.voiceClockCount(0u);
    auto changedMorph = slowMorph;
    changedMorph.bpFilterType[0] = 0.75f;
    movingMorph.setParams(changedMorph);
    double unlockedDifference = 0.0;
    for (uint32_t sample = 0u; sample < 2048u; ++sample) {
        heldReference.process(referenceOutput.data(), 1u, 1u);
        movingMorph.process(movingOutput.data(), 1u, 1u);
        unlockedDifference += std::fabs(
            static_cast<double>(referenceSample[0])
                - movingSample[0]);
    }
    if (heldReference.voiceClockCount(0u) != referenceClock
        || movingMorph.voiceClockCount(0u) != movingClock
        || unlockedDifference <= 1.0e-7) {
        std::fprintf(stderr,
            "Filter Morph still behaved like a clock-latched structure: "
            "clocks=%llu->%llu/%llu->%llu difference=%g\n",
            static_cast<unsigned long long>(referenceClock),
            static_cast<unsigned long long>(
                heldReference.voiceClockCount(0u)),
            static_cast<unsigned long long>(movingClock),
            static_cast<unsigned long long>(
                movingMorph.voiceClockCount(0u)),
            unlockedDifference);
        return false;
    }

    // Bounded mode has no dry comparator outlet: changing cutoff must reshape
    // even the least-driven output rather than leaving a fixed dry component.
    auto closedLowPass = base;
    closedLowPass.filterMorph = 0.0f;
    closedLowPass.filter = 0.035f;
    closedLowPass.filterRun = 0.0f;
    closedLowPass.filterSweep = 0.0f;
    closedLowPass.resonance = 0.10f;
    closedLowPass.color = 0.0f;
    auto openLowPass = closedLowPass;
    openLowPass.filter = 0.82f;
    const auto cutoffComparison =
        renderPair(closedLowPass, openLowPass);
    const auto closedMetrics =
        renderDynamics(closedLowPass, 500u, 120u);
    const auto openMetrics =
        renderDynamics(openLowPass, 500u, 120u);
    std::printf(
        "Wrangler all-filtered cutoff proof: diff %.3f, rough %.6g -> %.6g\n",
        cutoffComparison.difference,
        closedMetrics.roughness(), openMetrics.roughness());
    if (cutoffComparison.difference < 0.5
        || !closedMetrics.finite || !openMetrics.finite
        || closedMetrics.rms() <= 1.0e-5
        || openMetrics.rms() <= 1.0e-5
        || !(closedMetrics.roughness()
            < openMetrics.roughness() * 0.90)) {
        std::fprintf(stderr,
            "bounded output did not behave as an all-filtered signal: "
            "difference=%g closed/open roughness=%g/%g\n",
            cutoffComparison.difference,
            closedMetrics.roughness(), openMetrics.roughness());
        return false;
    }
    return true;
}

bool testLiveAutomationSmoothing()
{
    auto base = s3g::ambiWranglerFactoryPreset(
        s3g::kAmbiWranglerCalmPresetStart);
    base.voices = 8u;
    base.engines = base.voices;
    base.filterMorph = 0.0f;
    base.topologyMotion = 0u;
    base.maskMode = 0u;
    base.maskDepth = 0.0f;
    base.listeningEnabled = 0u;
    base.snap = 0.0f;
    base.outputGainDb = -9.0f;
    base.filter = 0.36f;
    base.resonance = 0.34f;
    base.filterRun = 0.08f;
    base.filterSweep = 0.04f;
    base.saturation = 0.06f;

    auto sixtyFour = base;
    sixtyFour.voices = 64u;
    sixtyFour.engines = sixtyFour.voices;
    if (!transitionIsClickFree("voices 8 -> 64", base, sixtyFour)
        || !transitionIsClickFree("voices 64 -> 8", sixtyFour, base)) {
        return false;
    }

    auto narrowPwm = base;
    narrowPwm.pwmA = 0.18f;
    narrowPwm.pwmB = 0.82f;
    auto widePwm = base;
    widePwm.pwmA = 0.82f;
    widePwm.pwmB = 0.18f;
    if (!transitionIsClickFree(
            "pulse width A/B", narrowPwm, widePwm)) {
        return false;
    }

    auto earlyRamps = base;
    earlyRamps.rampA = 0.16f;
    earlyRamps.rampB = 0.84f;
    auto lateRamps = base;
    lateRamps.rampA = 0.84f;
    lateRamps.rampB = 0.16f;
    if (!transitionIsClickFree(
            "ramp shape A/B", earlyRamps, lateRamps)) {
        return false;
    }

    auto squareSources = base;
    squareSources.inputA = 0u;
    squareSources.inputB = 0u;
    auto triangleSources = base;
    triangleSources.inputA = 1u;
    triangleSources.inputB = 1u;
    if (!transitionIsClickFree(
            "osc source square -> triangle",
            squareSources, triangleSources)
        || !transitionIsClickFree(
            "osc source triangle -> square",
            triangleSources, squareSources)) {
        return false;
    }

    return true;
}

bool testSettleHomeostasis()
{
    auto uncontrolled = s3g::ambiWranglerFactoryPreset(
        s3g::kAmbiWranglerCalmPresetStart + 2u);
    uncontrolled.voices = 16u;
    uncontrolled.engines = uncontrolled.voices;
    uncontrolled.rateA = 0.50f;
    uncontrolled.rateB = 0.38f;
    uncontrolled.rateModeA = 1u;
    uncontrolled.rateModeB = 1u;
    uncontrolled.fmAtoB = 0.74f;
    uncontrolled.fmBtoA = 0.70f;
    uncontrolled.runglerA = 0.76f;
    uncontrolled.runglerB = 0.70f;
    uncontrolled.spread = 0.08f;
    uncontrolled.deviation = 0.035f;
    uncontrolled.change = 1.0f;
    uncontrolled.color = 0.52f;
    uncontrolled.filter = 0.48f;
    uncontrolled.resonance = 0.48f;
    uncontrolled.filterRun = 0.44f;
    uncontrolled.filterSweep = 0.32f;
    uncontrolled.saturation = 0.34f;
    uncontrolled.maskMode = 0u;
    uncontrolled.maskDepth = 0.0f;
    uncontrolled.topologyMotion = 0u;
    uncontrolled.snap = 0.14f;
    uncontrolled.listeningEnabled = 1u;
    uncontrolled.fieldWrite = 0.0f;
    uncontrolled.registerMotion = 0.0f;
    uncontrolled.fieldReturn = 0.0f;
    uncontrolled.returnBypass = 1u;
    uncontrolled.listenerResponse =
        s3g::AmbiWranglerListenerResponse::Write;

    auto settled = uncontrolled;
    settled.listenerResponse = s3g::AmbiWranglerListenerResponse::Settle;
    settled.settleAmount = 1.0f;
    settled.settleTarget = 0.08f;
    settled.settleRecoverySeconds = 1.5f;

    const auto openMetrics = renderDynamics(uncontrolled, 1800u, 375u);
    const auto settleMetrics = renderDynamics(settled, 1800u, 375u);
    const double rmsRatio = settleMetrics.rms()
        / std::max(1.0e-12, openMetrics.rms());

    std::printf(
        "Wrangler Settle metrics: roughness %.6g -> %.6g, novelty %llu -> %llu, "
        "RMS %.5g -> %.5g, clocks %llu/%llu, advances %llu/%llu, "
        "held %llu, capture %.3f, evolve %.3f\n",
        openMetrics.roughness(), settleMetrics.roughness(),
        static_cast<unsigned long long>(openMetrics.registerNovelty),
        static_cast<unsigned long long>(settleMetrics.registerNovelty),
        openMetrics.rms(), settleMetrics.rms(),
        static_cast<unsigned long long>(openMetrics.clockEvents),
        static_cast<unsigned long long>(settleMetrics.clockEvents),
        static_cast<unsigned long long>(openMetrics.registerAdvances),
        static_cast<unsigned long long>(settleMetrics.registerAdvances),
        static_cast<unsigned long long>(settleMetrics.heldClocks),
        settleMetrics.capture, settleMetrics.minimumEvolutionRate);

    if (!openMetrics.finite || !settleMetrics.finite
        || settleMetrics.peak > 1.001f
        || !(settleMetrics.roughness() < openMetrics.roughness() * 1.25)
        || !(settleMetrics.registerNovelty * 2u
            < openMetrics.registerNovelty)
        || settleMetrics.registerTransitions == 0u
        || openMetrics.heldClocks != 0u
        || settleMetrics.heldClocks == 0u
        || settleMetrics.clockEvents
            != settleMetrics.registerAdvances + settleMetrics.heldClocks
        || !(settleMetrics.registerAdvances * 2u
            < openMetrics.registerAdvances)
        || !(settleMetrics.registerAdvances * 2u
            < settleMetrics.clockEvents)
        || !(rmsRatio > 0.65 && rmsRatio < 1.35)
        || !(settleMetrics.rms() > 1.0e-5)
        || !(settleMetrics.lateMotion() > 1.0e-5)
        || !(settleMetrics.capture > 0.20f)
        || !(settleMetrics.minimumEvolutionRate < 0.45f)) {
        std::fprintf(stderr,
            "Settle failed to reduce activity without muting/freezing: "
            "roughness=%g/%g novelty=%llu/%llu transitions=%llu "
            "clocks=%llu advances=%llu held=%llu rmsRatio=%g late=%g "
            "capture=%g evolution=%g\n",
            settleMetrics.roughness(), openMetrics.roughness(),
            static_cast<unsigned long long>(settleMetrics.registerNovelty),
            static_cast<unsigned long long>(openMetrics.registerNovelty),
            static_cast<unsigned long long>(settleMetrics.registerTransitions),
            static_cast<unsigned long long>(settleMetrics.clockEvents),
            static_cast<unsigned long long>(settleMetrics.registerAdvances),
            static_cast<unsigned long long>(settleMetrics.heldClocks),
            rmsRatio, settleMetrics.lateMotion(), settleMetrics.capture,
            settleMetrics.minimumEvolutionRate);
        return false;
    }
    return true;
}

bool testSettleIsolation()
{
    auto settled = s3g::ambiWranglerFactoryPreset(
        s3g::kAmbiWranglerCalmPresetStart + 2u);
    settled.voices = 16u;
    settled.engines = settled.voices;
    settled.topologyMotion = 0u;
    settled.listeningEnabled = 1u;
    settled.listenMode = s3g::AmbiWranglerListenMode::Trace;
    settled.listenerResponse =
        s3g::AmbiWranglerListenerResponse::Settle;
    settled.settleAmount = 1.0f;
    settled.settleTarget = 0.04f;
    settled.fieldWrite = 0.0f;
    settled.fieldReturn = 0.0f;
    settled.returnBypass = 1u;
    settled.registerMotion = 0.0f;

    auto attemptedSteering = settled;
    attemptedSteering.registerMotion = 1.0f;
    const auto steeringComparison =
        renderPair(settled, attemptedSteering);
    if (steeringComparison.difference != 0.0) {
        std::fprintf(stderr,
            "Settle allowed register-addressed destination steering: %g\n",
            steeringComparison.difference);
        return false;
    }

    auto openTone = settled;
    openTone.listenerResponse =
        s3g::AmbiWranglerListenerResponse::Write;
    openTone.fmAtoB = 0.0f;
    openTone.fmBtoA = 0.0f;
    openTone.runglerA = 0.0f;
    openTone.runglerB = 0.0f;
    openTone.filterRun = 0.0f;
    openTone.snap = 0.0f;
    auto capturedTone = openTone;
    capturedTone.listenerResponse =
        s3g::AmbiWranglerListenerResponse::Settle;
    const auto toneComparison =
        renderPair(openTone, capturedTone);
    if (toneComparison.energy <= 1.0e-5
        || toneComparison.difference != 0.0
        || !(toneComparison.activity > 0.05f)
        || !(toneComparison.capture > 0.05f)
        || toneComparison.heldClocks == 0u) {
        std::fprintf(stderr,
            "Settle altered disconnected oscillator tone or failed to hold: "
            "energy=%g difference=%g activity=%g capture=%g held=%llu\n",
            toneComparison.energy, toneComparison.difference,
            toneComparison.activity, toneComparison.capture,
            static_cast<unsigned long long>(
                toneComparison.heldClocks));
        return false;
    }

    auto legacyOpen = s3g::ambiWranglerFactoryPreset(0u);
    legacyOpen.topologyMotion = 0u;
    legacyOpen.listeningEnabled = 1u;
    legacyOpen.listenMode = s3g::AmbiWranglerListenMode::Trace;
    legacyOpen.listenerResponse =
        s3g::AmbiWranglerListenerResponse::Write;
    legacyOpen.fieldWrite = 0.0f;
    legacyOpen.registerMotion = 0.0f;
    legacyOpen.fieldReturn = 0.0f;
    legacyOpen.returnBypass = 1u;
    auto legacyCaptured = legacyOpen;
    legacyCaptured.listenerResponse =
        s3g::AmbiWranglerListenerResponse::Settle;
    legacyCaptured.settleAmount = 1.0f;
    legacyCaptured.settleTarget = 0.02f;
    const auto legacyComparison =
        renderPair(legacyOpen, legacyCaptured);
    if (legacyComparison.difference < 1.0
        || !(legacyComparison.capture > 0.05f)
        || legacyComparison.heldClocks == 0u) {
        std::fprintf(stderr,
            "Legacy Settle did not apply event viscosity: "
            "difference=%g capture=%g held=%llu\n",
            legacyComparison.difference, legacyComparison.capture,
            static_cast<unsigned long long>(
                legacyComparison.heldClocks));
        return false;
    }
    return true;
}

} // namespace

int main()
{
    const auto openPreset = s3g::ambiWranglerFactoryPreset(
        s3g::kAmbiWranglerBasePresetCount);
    const auto writtenPreset = s3g::ambiWranglerFactoryPreset(
        s3g::kAmbiWranglerBasePresetCount + 1u);
    const auto returnedPreset = s3g::ambiWranglerFactoryPreset(
        s3g::kAmbiWranglerBasePresetCount + 2u);

    if (openPreset.listeningEnabled != 0u
        || writtenPreset.listeningEnabled != 1u
        || writtenPreset.returnBypass != 1u
        || returnedPreset.returnBypass != 0u
        || openPreset.circuitLaw != s3g::AmbiWranglerCircuitLaw::Legacy
        || writtenPreset.listenerResponse
            != s3g::AmbiWranglerListenerResponse::Write) {
        std::fprintf(stderr, "matched listener presets are not configured as expected\n");
        return 1;
    }

    auto inertControls = openPreset;
    inertControls.fieldWrite = 1.0f;
    inertControls.registerMotion = 1.0f;
    inertControls.fieldReturn = 1.0f;
    inertControls.propagation = 0.88f;
    const auto offComparison = renderPair(openPreset, inertControls);
    if (offComparison.energy <= 1.0e-5 || offComparison.difference != 0.0) {
        std::fprintf(stderr,
            "listener-off path changed: energy=%g difference=%g\n",
            offComparison.energy, offComparison.difference);
        return 1;
    }

    const auto writtenComparison = renderPair(openPreset, writtenPreset);
    if (writtenComparison.activity < 0.05f
        || writtenComparison.difference < 1.0
        || writtenComparison.registerWord == 0u) {
        std::fprintf(stderr,
            "field writing was not observably active: activity=%g difference=%g register=%u\n",
            writtenComparison.activity, writtenComparison.difference,
            writtenComparison.registerWord);
        return 1;
    }

    const auto returnComparison = renderPair(writtenPreset, returnedPreset);
    if (returnComparison.difference < 0.25) {
        std::fprintf(stderr,
            "directional audio return did not alter output: difference=%g\n",
            returnComparison.difference);
        return 1;
    }

    auto tetra = writtenPreset;
    tetra.pickupSet = s3g::AmbiWranglerPickupSet::Tetra4;
    s3g::AmbiWranglerEncoder pickupProbe;
    pickupProbe.prepare(48000.0);
    pickupProbe.setParams(tetra);
    if (pickupProbe.listenerPickupCount() != 4u) {
        std::fprintf(stderr, "tetra pickup selection did not configure four ears\n");
        return 1;
    }
    tetra.pickupSet = s3g::AmbiWranglerPickupSet::Cube8;
    pickupProbe.setParams(tetra);
    if (pickupProbe.listenerPickupCount() != 8u) {
        std::fprintf(stderr, "cube pickup selection did not configure eight ears\n");
        return 1;
    }

    // Do not short-circuit: a failed regression should not hide later DSP
    // diagnostics, especially the per-parameter discontinuity probes.
    bool allTestsPassed = true;
    allTestsPassed = testCurveDimensionContract() && allTestsPassed;
    allTestsPassed = testFactoryCurveInitialization() && allTestsPassed;
    allTestsPassed =
        testCoreBlockPartitionInvariance() && allTestsPassed;
    allTestsPassed =
        testMalformedInputsRemainFinite() && allTestsPassed;
    allTestsPassed = testStartupAndResetRamp() && allTestsPassed;
    allTestsPassed = testRelativeCurveReachability() && allTestsPassed;
    allTestsPassed = testNeutralCurveIdentity() && allTestsPassed;
    allTestsPassed = testLegacyIdentity() && allTestsPassed;
    allTestsPassed = testCalmPresetContract() && allTestsPassed;
    allTestsPassed = testBoundedExactZero() && allTestsPassed;
    allTestsPassed = testOneVoiceOnePointContract() && allTestsPassed;
    allTestsPassed = testVoiceCurveAddressing() && allTestsPassed;
    allTestsPassed = testExtendedHistoricalCurveRows() && allTestsPassed;
    allTestsPassed =
        testHistoricalCurveTransitionSafety() && allTestsPassed;
    allTestsPassed =
        testHistoricalCrossDirectionBindings() && allTestsPassed;
    allTestsPassed = testPerVoiceRungStructure() && allTestsPassed;
    allTestsPassed =
        testClockAlignedRungStructureAutomation() && allTestsPassed;
    allTestsPassed = testContinuousFilterMorph() && allTestsPassed;
    allTestsPassed = testLiveAutomationSmoothing() && allTestsPassed;
    allTestsPassed = testSettleHomeostasis() && allTestsPassed;
    allTestsPassed = testSettleIsolation() && allTestsPassed;
    if (!allTestsPassed) return 1;

    std::printf(
        "Ambi Wrangler listener smoke passed (field diff %.3f, return diff %.3f, activity %.3f)\n",
        writtenComparison.difference, returnComparison.difference,
        writtenComparison.activity);
    return 0;
}
