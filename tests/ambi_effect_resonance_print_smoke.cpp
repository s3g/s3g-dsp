#include "s3g_ambi_effect_resonance_print.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

namespace {

constexpr double kSampleRate = 48000.0;
constexpr uint32_t kChannels = s3g::kAmbiEffectDjFilterMaxChannels;
constexpr uint32_t kBlockSize = 256u;

struct AudioBlock {
    std::array<std::array<float, kBlockSize>, kChannels> input {};
    std::array<std::array<float, kBlockSize>, kChannels> output {};
    std::array<float*, kChannels> inputPointers {};
    std::array<float*, kChannels> outputPointers {};

    AudioBlock()
    {
        for (uint32_t channel = 0u; channel < kChannels; ++channel) {
            inputPointers[channel] = input[channel].data();
            outputPointers[channel] = output[channel].data();
        }
    }

    void clear()
    {
        for (auto& channel : input) channel.fill(0.0f);
        for (auto& channel : output) channel.fill(0.0f);
    }
};

bool noPrintIdentityCheck()
{
    auto processor = std::make_unique<s3g::AmbiEffectResonancePrint>();
    if (!processor->prepare(kSampleRate)) return false;
    s3g::AmbiEffectResonancePrintParams params {};
    params.order = 1u;
    params.body = s3g::AmbiEffectBody::Icosa12;
    params.topology = s3g::AmbiEffectTopology::Cross;
    params.topologyAmount = 1.0f;
    params.maskAmount = 1.0f;
    params.maskDry = 0.0f;
    params.mix = 1.0f;
    params.printEnabled = 1u;
    processor->setParams(params);

    AudioBlock block;
    for (uint32_t channel = 0u; channel < 4u; ++channel) {
        for (uint32_t frame = 0u; frame < kBlockSize; ++frame) {
            block.input[channel][frame] = 0.1f * std::sin(
                static_cast<float>((channel + 1u) * (frame + 3u)) * 0.031f);
        }
    }
    block.input[0][0] = 0.75f;
    processor->process(block.inputPointers.data(), block.outputPointers.data(),
        kChannels, kChannels, kBlockSize);
    for (uint32_t channel = 0u; channel < kChannels; ++channel) {
        for (uint32_t frame = 0u; frame < kBlockSize; ++frame) {
            const float expected = channel < 4u
                ? block.input[channel][frame] : 0.0f;
            if (block.output[channel][frame] != expected) return false;
        }
    }
    if (processor->tailFrames() != 0u) return false;

    params.outputGainDb = 12.0f;
    processor->setParams(params);
    processor->reset();
    block.clear();
    block.input[0].fill(0.5f);
    processor->process(block.inputPointers.data(), block.outputPointers.data(),
        kChannels, kChannels, kBlockSize);
    float guardedPeak = 0.0f;
    for (uint32_t channel = 0u; channel < kChannels; ++channel) {
        for (float value : block.output[channel]) {
            guardedPeak = std::max(guardedPeak, std::abs(value));
        }
    }
    return guardedPeak <= 0.8915f && processor->safetyGain() < 0.99f;
}

bool captureAndResynthesisCheck()
{
    auto processor = std::make_unique<s3g::AmbiEffectResonancePrint>();
    if (!processor->prepare(kSampleRate)) return false;
    s3g::AmbiEffectResonancePrintParams params {};
    params.order = 1u;
    params.body = s3g::AmbiEffectBody::Icosa12;
    params.captureSeconds = 0.25f;
    params.sensitivity = 0.9f;
    params.modalCount = 8u;
    params.decaySeconds = 0.45f;
    params.drive = 0.0f;
    params.topologyAmount = 0.0f;
    params.mix = 1.0f;
    processor->setParams(params);
    processor->beginCapture();
    params.printEnabled = 1u;
    processor->setParams(params);
    if (processor->params().printEnabled != 0u
        || processor->printApplied()) return false;

    AudioBlock block;
    uint64_t sample = 0u;
    while (processor->isCapturing()) {
        block.clear();
        for (uint32_t frame = 0u; frame < kBlockSize; ++frame, ++sample) {
            const double time = static_cast<double>(sample) / kSampleRate;
            block.input[0][frame] = 0.22f * std::sin(
                2.0 * s3g::kPi * 220.0 * time)
                + 0.11f * std::sin(2.0 * s3g::kPi * 440.0 * time)
                + 0.055f * std::sin(2.0 * s3g::kPi * 660.0 * time);
            block.input[1][frame] = 0.04f * std::sin(
                2.0 * s3g::kPi * 220.0 * time + 0.3);
        }
        processor->process(block.inputPointers.data(), block.outputPointers.data(),
            kChannels, kChannels, kBlockSize);
        if (sample > 48000u) return false;
    }

    const auto captured = processor->printData();
    bool foundFundamentalPartial = false;
    uint32_t totalModes = 0u;
    for (uint32_t node = 0u; node < captured.pickupCount; ++node) {
        totalModes += captured.modeCount[node];
        for (uint32_t mode = 0u; mode < captured.modeCount[node]; ++mode) {
            foundFundamentalPartial = foundFundamentalPartial
                || std::abs(captured.modes[node][mode].frequencyHz - 220.0f) < 18.0f;
        }
    }
    const bool printLooksValid = processor->hasPrint()
        && captured.version == s3g::kResonancePrintVersion
        && captured.pickupCount == 12u
        && totalModes >= captured.pickupCount
        && foundFundamentalPartial
        && !processor->printApplied()
        && processor->tailFrames() == 0u;

    params.printEnabled = 1u;
    processor->setParams(params);
    const bool applyWorked = processor->printApplied()
        && processor->tailFrames() > 1000u;

    processor->clearPrint();
    const bool clearWorked = !processor->hasPrint()
        && processor->tailFrames() == 0u
        && !processor->printApplied()
        && processor->params().printEnabled == 0u
        && processor->printedModeCount(0u) == 0u;
    processor->setPrint(captured);
    params.printEnabled = 1u;
    processor->setParams(params);
    processor->reset();

    double tailEnergy = 0.0;
    float peak = 0.0f;
    bool finite = true;
    for (uint32_t blockIndex = 0u; blockIndex < 80u; ++blockIndex) {
        block.clear();
        if (blockIndex == 0u) block.input[0][0] = 1.0f;
        processor->process(block.inputPointers.data(), block.outputPointers.data(),
            kChannels, kChannels, kBlockSize);
        for (uint32_t channel = 0u; channel < 4u; ++channel) {
            for (uint32_t frame = 0u; frame < kBlockSize; ++frame) {
                const float value = block.output[channel][frame];
                finite = finite && std::isfinite(value);
                peak = std::max(peak, std::abs(value));
                if (blockIndex > 0u) tailEnergy += static_cast<double>(value) * value;
            }
        }
    }

    params.printEnabled = 0u;
    processor->setParams(params);
    block.clear();
    for (uint32_t channel = 0u; channel < 4u; ++channel) {
        for (uint32_t frame = 0u; frame < kBlockSize; ++frame) {
            block.input[channel][frame] = 0.07f * std::sin(
                static_cast<float>((channel + 2u) * (frame + 5u)) * 0.019f);
        }
    }
    processor->process(block.inputPointers.data(), block.outputPointers.data(),
        kChannels, kChannels, kBlockSize);
    bool bypassIdentity = processor->hasPrint() && !processor->printApplied()
        && processor->tailFrames() == 0u;
    for (uint32_t channel = 0u; channel < kChannels; ++channel) {
        for (uint32_t frame = 0u; frame < kBlockSize; ++frame) {
            const float expected = channel < 4u
                ? block.input[channel][frame] : 0.0f;
            bypassIdentity = bypassIdentity
                && block.output[channel][frame] == expected;
        }
    }
    params.printEnabled = 1u;
    processor->setParams(params);
    const bool reapplyWorked = processor->hasPrint() && processor->printApplied()
        && processor->tailFrames() > 1000u;

    params.outputGainDb = -6.0f;
    params.drive = 0.35f;
    processor->setParams(params);
    processor->reset();
    double transposePhase = 0.0;
    float previousSample = 0.0f;
    float transposePeak = 0.0f;
    float transposeMaximumStep = 0.0f;
    bool transposeFinite = true;
    for (uint32_t blockIndex = 0u; blockIndex < 120u; ++blockIndex) {
        if (blockIndex % 4u == 0u) {
            params.transposeSemitones = (blockIndex / 4u) % 2u == 0u
                ? 24.0f : -24.0f;
            processor->setParams(params);
        }
        block.clear();
        for (uint32_t frame = 0u; frame < kBlockSize; ++frame) {
            block.input[0][frame] = 0.18f * std::sin(transposePhase)
                + 0.04f * std::sin(transposePhase * 2.01);
            transposePhase += 2.0 * s3g::kPi * 220.0 / kSampleRate;
        }
        processor->process(block.inputPointers.data(), block.outputPointers.data(),
            kChannels, kChannels, kBlockSize);
        for (uint32_t frame = 0u; frame < kBlockSize; ++frame) {
            const float value = block.output[0][frame];
            transposeFinite = transposeFinite && std::isfinite(value);
            transposePeak = std::max(transposePeak, std::abs(value));
            transposeMaximumStep = std::max(transposeMaximumStep,
                std::abs(value - previousSample));
            previousSample = value;
        }
    }
    const bool transposeWorked = transposeFinite
        && transposePeak < 0.5f
        && transposeMaximumStep < 0.15f
        && std::isfinite(processor->maximumResonatorState())
        && processor->maximumResonatorState() <= 64.001f;
    std::printf(
        "Ambi Effect Resonance Print TRANS stress: peak=%.6g max-step=%.6g state=%.6g\n",
        transposePeak, transposeMaximumStep, processor->maximumResonatorState());

    std::printf(
        "Ambi Effect Resonance Print: modes=%u f0=%.2f confidence=%.3f tail=%u energy=%.9g peak=%.6g\n",
        totalModes, captured.fundamentalHz, captured.pitchConfidence,
        processor->tailFrames(), tailEnergy, peak);
    params.outputGainDb = 12.0f;
    params.drive = 1.0f;
    params.mix = 1.0f;
    processor->setParams(params);
    processor->reset();
    float protectedPeak = 0.0f;
    for (uint32_t blockIndex = 0u; blockIndex < 8u; ++blockIndex) {
        block.clear();
        for (uint32_t frame = 0u; frame < kBlockSize; ++frame) {
            block.input[0][frame] = 4.0f;
            block.input[1][frame] = -3.0f;
        }
        processor->process(block.inputPointers.data(), block.outputPointers.data(),
            kChannels, kChannels, kBlockSize);
        for (uint32_t channel = 0u; channel < 4u; ++channel) {
            for (uint32_t frame = 0u; frame < kBlockSize; ++frame) {
                protectedPeak = std::max(protectedPeak,
                    std::abs(block.output[channel][frame]));
            }
        }
    }
    const bool safetyWorked = protectedPeak <= 0.8915f
        && processor->safetyGain() < 0.99f
        && processor->excitationGovernorGain() < 0.99f;
    std::printf(
        "Ambi Effect Resonance Print safety: peak=%.6g output-gain=%.6g excitation-gain=%.6g\n",
        protectedPeak, processor->safetyGain(),
        processor->excitationGovernorGain());
    return printLooksValid && applyWorked && clearWorked && bypassIdentity
        && reapplyWorked && transposeWorked
        && processor->hasPrint() && processor->printApplied()
        && finite && tailEnergy > 1.0e-14 && peak < 4.0f && safetyWorked;
}

bool maskCoordinateCheck()
{
    auto processor = std::make_unique<s3g::AmbiEffectResonancePrint>();
    if (!processor->prepare(kSampleRate)) return false;
    s3g::AmbiEffectResonancePrintParams params {};
    params.order = 7u;
    params.body = s3g::AmbiEffectBody::Sphere24;
    params.maskAmount = 1.0f;
    params.maskWidth = 0.0f;
    params.maskCurve = 1.0f;
    const auto directions = s3g::ambiEffectBodyDirections(params.body);
    const auto strongest = [&]() {
        uint32_t best = 0u;
        for (uint32_t node = 1u; node < 24u; ++node) {
            if (processor->nodeWetMask(node) > processor->nodeWetMask(best)) {
                best = node;
            }
        }
        return best;
    };

    params.maskAzimuthDeg = 0.0f;
    params.maskElevationDeg = 90.0f;
    processor->setParams(params);
    const auto upper = directions[strongest()];
    params.maskElevationDeg = -90.0f;
    processor->setParams(params);
    const auto lower = directions[strongest()];
    params.maskElevationDeg = 0.0f;
    processor->setParams(params);
    const auto horizon = directions[strongest()];
    std::printf(
        "Ambi Effect Resonance Print mask axes: upper z=%.3f lower z=%.3f horizon x=%.3f\n",
        upper.z, lower.z, horizon.x);
    return upper.z > 0.8f && lower.z < -0.8f && horizon.x > 0.7f;
}

bool sanitizationAndCaptureCancellationCheck()
{
    auto processor = std::make_unique<s3g::AmbiEffectResonancePrint>();
    if (!processor->prepare(kSampleRate)) return false;
    s3g::AmbiEffectResonancePrintParams params {};
    params.order = 99u;
    params.body = s3g::AmbiEffectBody::Cube8;
    params.modalCount = 99u;
    params.captureSeconds = -1.0f;
    params.printEnabled = 99u;
    processor->setParams(params);
    const auto sanitized = processor->params();
    if (sanitized.order != 7u
        || sanitized.body != s3g::AmbiEffectBody::Icosa12
        || sanitized.modalCount != s3g::kResonancePrintMaxModes
        || sanitized.captureSeconds != 0.25f
        || sanitized.printEnabled != 1u) return false;
    processor->beginCapture();
    if (processor->params().printEnabled != 0u) return false;
    processor->clearPrint();
    if (processor->isCapturing() || processor->hasPrint()) return false;
    processor->beginCapture();
    params = sanitized;
    params.order = 6u;
    processor->setParams(params);
    return !processor->isCapturing();
}

} // namespace

int main()
{
#if !defined(__APPLE__)
    std::puts("Ambi Effect Resonance Print smoke skipped: Accelerate is required");
    return 0;
#else
    const bool identity = noPrintIdentityCheck();
    const bool capture = captureAndResynthesisCheck();
    const bool mask = maskCoordinateCheck();
    const bool sanitize = sanitizationAndCaptureCancellationCheck();
    if (!identity || !capture || !mask || !sanitize) {
        std::fprintf(stderr,
            "Ambi Effect Resonance Print smoke failed: identity=%d capture=%d mask=%d sanitize=%d\n",
            identity ? 1 : 0, capture ? 1 : 0, mask ? 1 : 0,
            sanitize ? 1 : 0);
        return 1;
    }
    std::puts("Ambi Effect Resonance Print smoke passed");
    return 0;
#endif
}
