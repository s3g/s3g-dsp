#include "s3g_ambi_wrangler_encoder.h"
#include "s3g_ambi_wrangler_presets.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
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

    const uint32_t engines = encoder.engineCount();
    std::array<uint32_t, s3g::kAmbiWranglerMaxVoices> representative {};
    representative.fill(params.voices);
    for (uint32_t node = 0u; node < params.voices; ++node) {
        const uint32_t engine = encoder.nodeEngine(node);
        if (engine < engines && representative[engine] == params.voices) {
            representative[engine] = node;
        }
    }
    std::array<uint32_t, s3g::kAmbiWranglerMaxVoices> previousRegister {};
    std::array<uint64_t, s3g::kAmbiWranglerMaxVoices> warmClockCount {};
    std::array<uint64_t, s3g::kAmbiWranglerMaxVoices> warmAdvanceCount {};
    std::array<uint64_t, s3g::kAmbiWranglerMaxVoices> warmHeldCount {};
    for (uint32_t engine = 0u; engine < engines; ++engine) {
        previousRegister[engine] = encoder.voiceRegister(representative[engine]);
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
            for (uint32_t engine = 0u; engine < engines; ++engine) {
                const uint32_t node = representative[engine];
                previousRegister[engine] = encoder.voiceRegister(node);
                if (block + 1u == warmupBlocks) {
                    warmClockCount[engine] =
                        encoder.voiceClockCount(node);
                    warmAdvanceCount[engine] =
                        encoder.voiceRegisterAdvanceCount(node);
                    warmHeldCount[engine] =
                        encoder.voiceHeldClockCount(node);
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

        for (uint32_t engine = 0u; engine < engines; ++engine) {
            const uint32_t node = representative[engine];
            const uint32_t current = encoder.voiceRegister(node) & mask;
            const uint32_t previous = previousRegister[engine] & mask;
            if (current != previous) {
                ++result.registerTransitions;
                const uint32_t outgoing = (previous >> (params.rungSize - 1u)) & 1u;
                const uint32_t incoming = current & 1u;
                if (incoming != outgoing) ++result.registerNovelty;
            }
            previousRegister[engine] = current;
            result.capture = std::max(
                result.capture, encoder.voiceListenerCapture(node));
            result.minimumEvolutionRate = std::min(
                result.minimumEvolutionRate,
                encoder.voiceListenerEvolutionRate(node));
        }
    }
    for (uint32_t engine = 0u; engine < engines; ++engine) {
        const uint32_t node = representative[engine];
        result.clockEvents +=
            encoder.voiceClockCount(node) - warmClockCount[engine];
        result.registerAdvances +=
            encoder.voiceRegisterAdvanceCount(node)
                - warmAdvanceCount[engine];
        result.heldClocks +=
            encoder.voiceHeldClockCount(node) - warmHeldCount[engine];
    }
    return result;
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
    static constexpr std::array<uint32_t, 4> kExpectedNodes {
        8u, 16u, 32u, 64u
    };
    static constexpr std::array<uint32_t, 4> kExpectedEngines {
        1u, 2u, 4u, 8u
    };

    for (uint32_t variant = 0u; variant < kExpectedNodes.size(); ++variant) {
        const auto params = s3g::ambiWranglerFactoryPreset(
            s3g::kAmbiWranglerCalmPresetStart + variant);
        if (params.circuitLaw != s3g::AmbiWranglerCircuitLaw::Bounded
            || params.voices != kExpectedNodes[variant]
            || params.engines != kExpectedEngines[variant]
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
        if (encoder.engineCount() != kExpectedEngines[variant]) {
            std::fprintf(stderr, "calm preset %u exposed the wrong engine count\n", variant);
            return false;
        }
        std::array<bool, s3g::kAmbiWranglerMaxVoices> seen {};
        for (uint32_t node = 0u; node < params.voices; ++node) {
            const uint32_t expected = std::min<uint32_t>(
                params.engines - 1u,
                node * params.engines / params.voices);
            const uint32_t mapped = encoder.nodeEngine(node);
            if (mapped != expected) {
                std::fprintf(stderr,
                    "node/engine mapping failed for preset %u at node %u: %u != %u\n",
                    variant, node, mapped, expected);
                return false;
            }
            seen[mapped] = true;
        }
        for (uint32_t engine = 0u; engine < params.engines; ++engine) {
            if (!seen[engine]) {
                std::fprintf(stderr, "calm preset %u left engine %u without nodes\n",
                    variant, engine);
                return false;
            }
        }

        const auto metrics = renderDynamics(params, 360u, 80u);
        std::printf(
            "Wrangler calm preset %u: RMS %.5g, roughness %.6g, late %.5g\n",
            variant, metrics.rms(), metrics.roughness(),
            metrics.lateMotion());
        if (!metrics.finite || metrics.peak > 1.001f
            || metrics.rms() <= 1.0e-5
            || metrics.roughness() >= 0.0005
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
        std::fprintf(stderr, "legacy law no longer keeps one circuit per field node\n");
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
    quietRegister.engines = 2u;
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

bool testEngineCurveAddressing()
{
    auto reference = s3g::ambiWranglerFactoryPreset(
        s3g::kAmbiWranglerCalmPresetStart + 2u);
    reference.voices = 32u;
    reference.engines = 4u;
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

    auto firstEngineMuted = reference;
    firstEngineMuted.bpAmp[0] = 0.0f;
    const auto comparison = renderPair(reference, firstEngineMuted);
    if (comparison.energy <= 1.0e-5 || comparison.difference < 1.0) {
        std::fprintf(stderr,
            "bounded curve slot E1 did not address engine 1: energy=%g difference=%g\n",
            comparison.energy, comparison.difference);
        return false;
    }
    return true;
}

bool testSettleHomeostasis()
{
    auto uncontrolled = s3g::ambiWranglerFactoryPreset(
        s3g::kAmbiWranglerCalmPresetStart + 2u);
    uncontrolled.voices = 16u;
    uncontrolled.engines = 4u;
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
    settled.engines = 4u;
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

    if (!testLegacyIdentity()
        || !testCalmPresetContract()
        || !testBoundedExactZero()
        || !testEngineCurveAddressing()
        || !testSettleHomeostasis()
        || !testSettleIsolation()) {
        return 1;
    }

    std::printf(
        "Ambi Wrangler listener smoke passed (field diff %.3f, return diff %.3f, activity %.3f)\n",
        writtenComparison.difference, returnComparison.difference,
        writtenComparison.activity);
    return 0;
}
