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
    processor->setParams(params);

    AudioBlock block;
    for (uint32_t channel = 0u; channel < 4u; ++channel) {
        for (uint32_t frame = 0u; frame < kBlockSize; ++frame) {
            block.input[channel][frame] = 0.1f * std::sin(
                static_cast<float>((channel + 1u) * (frame + 3u)) * 0.031f);
        }
    }
    processor->process(block.inputPointers.data(), block.outputPointers.data(),
        kChannels, kChannels, kBlockSize);
    for (uint32_t channel = 0u; channel < kChannels; ++channel) {
        for (uint32_t frame = 0u; frame < kBlockSize; ++frame) {
            const float expected = channel < 4u
                ? block.input[channel][frame] : 0.0f;
            if (block.output[channel][frame] != expected) return false;
        }
    }
    return processor->tailFrames() == 0u;
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
        && processor->tailFrames() > 1000u;

    processor->clearPrint();
    const bool clearWorked = !processor->hasPrint()
        && processor->tailFrames() == 0u
        && processor->printedModeCount(0u) == 0u;
    processor->setPrint(captured);
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

    std::printf(
        "Ambi Effect Resonance Print: modes=%u f0=%.2f confidence=%.3f tail=%u energy=%.9g peak=%.6g\n",
        totalModes, captured.fundamentalHz, captured.pitchConfidence,
        processor->tailFrames(), tailEnergy, peak);
    return printLooksValid && clearWorked && processor->hasPrint()
        && finite && tailEnergy > 1.0e-14 && peak < 4.0f;
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
    processor->setParams(params);
    const auto sanitized = processor->params();
    if (sanitized.order != 7u
        || sanitized.body != s3g::AmbiEffectBody::Icosa12
        || sanitized.modalCount != s3g::kResonancePrintMaxModes
        || sanitized.captureSeconds != 0.25f) return false;
    processor->beginCapture();
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
    const bool sanitize = sanitizationAndCaptureCancellationCheck();
    if (!identity || !capture || !sanitize) {
        std::fprintf(stderr,
            "Ambi Effect Resonance Print smoke failed: identity=%d capture=%d sanitize=%d\n",
            identity ? 1 : 0, capture ? 1 : 0, sanitize ? 1 : 0);
        return 1;
    }
    std::puts("Ambi Effect Resonance Print smoke passed");
    return 0;
#endif
}
