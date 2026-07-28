#include "s3g_ambi_effect_partial_trace.h"
#include "s3g_ambi_effect_response_trace.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

namespace {

constexpr uint32_t kChannels = s3g::kAmbiEffectDjFilterMaxChannels;
constexpr uint32_t kBlock = 128u;
using Audio = std::array<std::vector<float>, kChannels>;

std::array<float*, kChannels> pointers(Audio& audio)
{
    std::array<float*, kChannels> result {};
    for (uint32_t channel = 0u; channel < kChannels; ++channel) {
        result[channel] = audio[channel].data();
    }
    return result;
}

bool finiteAndGuarded(const Audio& audio, float& peak)
{
    peak = 0.0f;
    for (const auto& channel : audio) {
        for (float value : channel) {
            if (!std::isfinite(value)) return false;
            peak = std::max(peak, std::abs(value));
        }
    }
    return peak <= 0.89126f;
}

bool partialTraceSmoke()
{
#if !S3G_HAS_PARTIAL_TRACE_FFT
    return true;
#else
    constexpr double sampleRate = 48000.0;
    s3g::AmbiEffectPartialTrace processor;
    s3g::AmbiEffectPartialTraceParams params {};
    params.order = 3u;
    params.partialCount = 8u;
    params.sensitivity = 0.75f;
    params.mix = 0.75f;
    params.traceGainDb = -9.0f;
    processor.setParams(params);
    if (!processor.prepare(sampleRate)) return false;

    Audio input {};
    Audio output {};
    for (auto& channel : input) channel.assign(kBlock, 0.0f);
    for (auto& channel : output) channel.assign(kBlock, 0.0f);
    auto in = pointers(input);
    auto out = pointers(output);
    float phaseA = 0.0f;
    float phaseB = 0.0f;
    float maximumPeak = 0.0f;
    for (uint32_t block = 0u; block < 180u; ++block) {
        for (uint32_t frame = 0u; frame < kBlock; ++frame) {
            const float value = 0.12f * std::sin(phaseA)
                + 0.06f * std::sin(phaseB);
            phaseA += 2.0f * s3g::kPi * 220.0f / static_cast<float>(sampleRate);
            phaseB += 2.0f * s3g::kPi * 554.37f / static_cast<float>(sampleRate);
            input[0][frame] = value;
            input[1][frame] = value * 0.35f;
            input[2][frame] = value * -0.18f;
            input[3][frame] = value * 0.72f;
        }
        processor.process(in.data(), out.data(), kChannels, kChannels, kBlock);
        float peak = 0.0f;
        if (!finiteAndGuarded(output, peak)) return false;
        maximumPeak = std::max(maximumPeak, peak);
    }
    const bool detected = processor.activePartialCount() >= 2u
        && processor.strongestFrequencyHz() > 180.0f
        && processor.strongestFrequencyHz() < 620.0f;
    const float frozenFrequency = processor.strongestFrequencyHz();
    params = processor.params();
    params.freeze = 1u;
    params.smear = 1.0f;
    processor.setParams(params);
    float frozenPeak = 0.0f;
    for (uint32_t block = 0u; block < 200u; ++block) {
        for (auto& channel : input) {
            std::fill(channel.begin(), channel.end(), 0.0f);
        }
        processor.process(in.data(), out.data(), kChannels, kChannels, kBlock);
        float peak = 0.0f;
        if (!finiteAndGuarded(output, peak)) return false;
        if (block >= 180u) frozenPeak = std::max(frozenPeak, peak);
    }
    const bool freezeHeld = processor.activePartialCount() >= 2u
        && std::abs(processor.strongestFrequencyHz() - frozenFrequency) < 0.01f
        && frozenPeak > 0.001f;

    params = processor.params();
    params.freeze = 0u;
    processor.setParams(params);
    float smearedPeak = 0.0f;
    for (uint32_t block = 0u; block < 120u; ++block) {
        processor.process(in.data(), out.data(), kChannels, kChannels, kBlock);
        float peak = 0.0f;
        if (!finiteAndGuarded(output, peak)) return false;
        if (block >= 100u) smearedPeak = std::max(smearedPeak, peak);
    }
    const bool smearHeld = processor.activePartialCount() > 0u
        && smearedPeak > frozenPeak * 0.5f;

    params = processor.params();
    params.smear = 0.0f;
    params.releaseMs = 40.0f;
    processor.setParams(params);
    for (uint32_t block = 0u; block < 220u; ++block) {
        processor.process(in.data(), out.data(), kChannels, kChannels, kBlock);
    }
    const bool released = processor.activePartialCount() == 0u;
    std::printf("Ambi Effect Partial Trace: detected=%d freeze=%d smear=%d released=%d frozen-peak=%.6f smeared-peak=%.6f\n",
        detected ? 1 : 0, freezeHeld ? 1 : 0, smearHeld ? 1 : 0,
        released ? 1 : 0, frozenPeak, smearedPeak);
    return detected && maximumPeak > 0.001f && freezeHeld
        && smearHeld && released;
#endif
}

bool partialTraceSafetySmoke()
{
#if !S3G_HAS_PARTIAL_TRACE_FFT
    return true;
#else
    constexpr double sampleRate = 48000.0;
    s3g::AmbiEffectPartialTrace processor;
    s3g::AmbiEffectPartialTraceParams params {};
    params.order = 3u;
    params.body = s3g::AmbiEffectBody::Icosa12;
    params.freeze = 1u;
    params.partialCount = s3g::kPartialTraceMaxPartials;
    params.traceGainDb = 12.0f;
    params.mix = 1.0f;
    params.outputGainDb = 12.0f;
    processor.setParams(params);
    if (!processor.prepare(sampleRate)) return false;

    std::array<s3g::PartialTraceFrozenVoiceState,
        s3g::kPartialTraceMaxPartials> voices {};
    for (uint32_t voice = 0u; voice < voices.size(); ++voice) {
        voices[voice].active = 1u;
        voices[voice].sourceFrequency = 110.0f + 47.0f * voice;
        voices[voice].currentFrequency = voices[voice].sourceFrequency;
        voices[voice].amplitude = 0.45f;
        voices[voice].phase = 0.25f;
        voices[voice].ownership.fill(1.0f);
    }
    processor.restoreFrozenState(s3g::AmbiEffectBody::Icosa12,
        voices.data(), static_cast<uint32_t>(voices.size()));

    Audio input {};
    Audio output {};
    for (auto& channel : input) channel.assign(kBlock, 0.0f);
    for (auto& channel : output) channel.assign(kBlock, 0.0f);
    auto in = pointers(input);
    auto out = pointers(output);
    std::array<float, kChannels> previousSample {};
    bool havePrevious = false;
    float phase = 0.0f;
    float maximumPeak = 0.0f;
    float maximumStep = 0.0f;
    float minimumGovernor = 1.0f;
    for (uint32_t block = 0u; block < 180u; ++block) {
        if (block % 6u == 0u) {
            const uint32_t cycle = (block / 6u) % 3u;
            params = processor.params();
            params.order = cycle == 0u ? 3u : (cycle == 1u ? 5u : 7u);
            params.body = cycle == 0u ? s3g::AmbiEffectBody::Icosa12
                : (cycle == 1u ? s3g::AmbiEffectBody::Dodeca20
                    : s3g::AmbiEffectBody::Sphere24);
            params.enabled = (block / 6u) % 2u;
            params.transposeSemitones = cycle == 0u ? -24.0f
                : (cycle == 1u ? 24.0f : 0.0f);
            params.traceGainDb = cycle == 1u ? -18.0f : 12.0f;
            params.smear = cycle * 0.5f;
            params.mix = cycle == 2u ? 0.25f : 1.0f;
            params.outputGainDb = cycle == 0u ? 12.0f : -6.0f;
            params.maskAmount = cycle == 0u ? 0.0f : 1.0f;
            params.maskAzimuthDeg = cycle == 1u ? -170.0f : 170.0f;
            params.maskElevationDeg = cycle == 1u ? -80.0f : 80.0f;
            params.maskDry = cycle == 2u ? 0.0f : 1.0f;
            params.topology = static_cast<s3g::AmbiEffectTopology>(cycle);
            processor.setParams(params);
        }
        for (uint32_t frame = 0u; frame < kBlock; ++frame) {
            for (uint32_t channel = 0u; channel < kChannels; ++channel) {
                input[channel][frame] = 0.035f * std::sin(
                    phase + static_cast<float>(channel) * 0.071f);
            }
            phase += 2.0f * s3g::kPi * 173.0f
                / static_cast<float>(sampleRate);
            phase -= std::floor(phase / (2.0f * s3g::kPi))
                * 2.0f * s3g::kPi;
        }
        processor.process(in.data(), out.data(), kChannels, kChannels, kBlock);
        float peak = 0.0f;
        if (!finiteAndGuarded(output, peak)) return false;
        maximumPeak = std::max(maximumPeak, peak);
        minimumGovernor = std::min(
            minimumGovernor, processor.traceGovernorGain());
        for (uint32_t channel = 0u; channel < kChannels; ++channel) {
            for (uint32_t frame = 0u; frame < kBlock; ++frame) {
                if (havePrevious || frame > 0u) {
                    const float previous = frame > 0u
                        ? output[channel][frame - 1u] : previousSample[channel];
                    maximumStep = std::max(maximumStep,
                        std::abs(output[channel][frame] - previous));
                }
            }
            previousSample[channel] = output[channel][kBlock - 1u];
        }
        havePrevious = true;
    }
    const bool governed = minimumGovernor < 0.95f;
    const bool clickSafe = maximumStep < 0.35f;
    std::printf("Ambi Effect Partial Trace safety: governed=%d min-gov=%.4f peak=%.6f max-step=%.6f\n",
        governed ? 1 : 0, minimumGovernor, maximumPeak, maximumStep);
    return governed && clickSafe && maximumPeak > 0.01f;
#endif
}

bool responseTraceSmoke()
{
#if !S3G_HAS_RESPONSE_TRACE_FFT
    return true;
#else
    constexpr double sampleRate = 48000.0;
    s3g::AmbiEffectResponseTrace processor;
    s3g::AmbiEffectResponseTraceParams params {};
    params.order = 1u;
    params.body = s3g::AmbiEffectBody::Tetra4;
    params.captureSeconds = 0.05f;
    params.mix = 1.0f;
    params.responseGainDb = 0.0f;
    processor.setParams(params);
    if (!processor.prepare(sampleRate)) return false;
    processor.startCapture();

    Audio input {};
    Audio output {};
    for (auto& channel : input) channel.assign(kBlock, 0.0f);
    for (auto& channel : output) channel.assign(kBlock, 0.0f);
    auto in = pointers(input);
    auto out = pointers(output);
    uint32_t captureFrame = 0u;
    while (processor.capturing()) {
        for (auto& channel : input) std::fill(channel.begin(), channel.end(), 0.0f);
        for (uint32_t frame = 0u; frame < kBlock; ++frame, ++captureFrame) {
            if (captureFrame == 0u) {
                input[0][frame] = 0.35f;
                input[3][frame] = 0.45f;
            }
            if (captureFrame == 280u) {
                input[0][frame] = -0.12f;
                input[3][frame] = -0.20f;
            }
        }
        processor.process(in.data(), out.data(), kChannels, kChannels, kBlock);
    }
    if (!processor.preparingResponse() || processor.hasResponse()
        || processor.preparationProgress() >= 0.5f) return false;
    params = processor.params();
    params.responseEnabled = 1u;
    processor.setParams(params);
    if (processor.responseApplied()) return false;
    for (uint32_t block = 0u;
        processor.preparingResponse() && block < 64u; ++block) {
        for (auto& channel : input) {
            std::fill(channel.begin(), channel.end(), 0.0f);
        }
        processor.process(in.data(), out.data(), kChannels, kChannels, kBlock);
    }
    if (!processor.hasResponse() || !processor.responseApplied()
        || processor.responseFrames() < 2000u
        || processor.capturedBody() != s3g::AmbiEffectBody::Tetra4
        || processor.capturedPickupCount() != 4u) return false;
    params = processor.params();
    params.responseEnabled = 0u;
    processor.setParams(params);
    for (uint32_t block = 0u; block < 16u; ++block) {
        processor.process(in.data(), out.data(), kChannels, kChannels, kBlock);
    }

    float minimumEnergy = 1000000.0f;
    float maximumEnergy = 0.0f;
    for (uint32_t node = 0u; node < processor.capturedPickupCount(); ++node) {
        float energy = 0.0f;
        for (uint32_t frame = 0u; frame < processor.responseFrames(); ++frame) {
            const float value = processor.responseSample(node, frame);
            energy += value * value;
        }
        minimumEnergy = std::min(minimumEnergy, energy);
        maximumEnergy = std::max(maximumEnergy, energy);
    }
    if (maximumEnergy <= minimumEnergy * 1.5f) return false;

    for (const auto body : { s3g::AmbiEffectBody::Cube8,
            s3g::AmbiEffectBody::Dodeca20,
            s3g::AmbiEffectBody::Sphere24 }) {
        params = processor.params();
        params.body = body;
        processor.setParams(params);
        for (uint32_t block = 0u; block < 96u; ++block) {
            processor.process(in.data(), out.data(),
                kChannels, kChannels, kBlock);
            if (!processor.preparingResponse()
                && processor.activePickupCount()
                    == s3g::ambiEffectBodyPickupCount(body)) break;
        }
        if (!processor.hasResponse()
            || processor.activePickupCount()
                != s3g::ambiEffectBodyPickupCount(body)) return false;
    }

    params = processor.params();
    params.responseEnabled = 1u;
    processor.setParams(params);
    for (auto& channel : input) std::fill(channel.begin(), channel.end(), 0.0f);
    bool impulseSent = false;
    float wetPeak = 0.0f;
    for (uint32_t block = 0u; block < 50u; ++block) {
        for (auto& channel : input) std::fill(channel.begin(), channel.end(), 0.0f);
        if (!impulseSent) {
            input[0][0] = 0.3f;
            input[3][0] = 0.25f;
            impulseSent = true;
        }
        processor.process(in.data(), out.data(), kChannels, kChannels, kBlock);
        float peak = 0.0f;
        if (!finiteAndGuarded(output, peak)) return false;
        wetPeak = std::max(wetPeak, peak);
    }
    std::printf("Ambi Effect Response Trace: response=%u kernels=%u body=%u latency=%u peak=%.6f safety=%.4f\n",
        processor.responseFrames(), processor.capturedPickupCount(),
        static_cast<uint32_t>(processor.capturedBody()),
        processor.latencyFrames(), wetPeak, processor.safetyGain());
    return processor.responseApplied() && wetPeak > 0.00001f;
#endif
}

bool responseTraceSafetySmoke()
{
#if !S3G_HAS_RESPONSE_TRACE_FFT
    return true;
#else
    constexpr double sampleRate = 48000.0;
    constexpr uint32_t responseFrames = 1024u;
    constexpr uint32_t responsePickups = 4u;
    s3g::AmbiEffectResponseTrace processor;
    s3g::AmbiEffectResponseTraceParams params {};
    params.order = 1u;
    params.body = s3g::AmbiEffectBody::Tetra4;
    params.responseEnabled = 1u;
    params.responseGainDb = 12.0f;
    params.tone = 1.0f;
    params.mix = 1.0f;
    params.outputGainDb = 12.0f;
    processor.setParams(params);
    if (!processor.prepare(sampleRate)) return false;

    std::vector<float> response(responsePickups * responseFrames, 0.0f);
    for (uint32_t node = 0u; node < responsePickups; ++node) {
        response[static_cast<size_t>(node) * responseFrames]
            = node % 2u == 0u ? 32.0f : -28.0f;
        response[static_cast<size_t>(node) * responseFrames + 173u]
            = node % 2u == 0u ? -9.0f : 11.0f;
    }
    if (!processor.loadResponse(response.data(), responsePickups,
        s3g::AmbiEffectBody::Tetra4, responseFrames, sampleRate, false)) {
        return false;
    }
    processor.setParams(params);

    Audio input {};
    Audio output {};
    for (auto& channel : input) channel.assign(kBlock, 0.0f);
    for (auto& channel : output) channel.assign(kBlock, 0.0f);
    auto in = pointers(input);
    auto out = pointers(output);
    std::array<float, kChannels> previousSample {};
    bool havePrevious = false;
    float phaseA = 0.0f;
    float phaseB = 0.0f;
    float currentInputScale = 1.0f;
    float targetInputScale = 1.0f;
    float maximumPeak = 0.0f;
    float maximumStep = 0.0f;
    uint32_t maximumStepBlock = 0u;
    uint32_t maximumStepChannel = 0u;
    uint32_t maximumStepFrame = 0u;
    uint32_t renderedBlocks = 0u;
    float minimumExcitationGovernor = 1.0f;
    float minimumResponseGovernor = 1.0f;
    float minimumSafety = 1.0f;
    const std::array<s3g::AmbiEffectBody, 5u> bodies {
        s3g::AmbiEffectBody::Tetra4,
        s3g::AmbiEffectBody::Cube8,
        s3g::AmbiEffectBody::Icosa12,
        s3g::AmbiEffectBody::Dodeca20,
        s3g::AmbiEffectBody::Sphere24,
    };
    const auto renderBlock = [&]() {
        for (uint32_t frame = 0u; frame < kBlock; ++frame) {
            currentInputScale += (targetInputScale - currentInputScale) * 0.002f;
            const float value = currentInputScale
                * (0.11f * std::sin(phaseA) + 0.055f * std::sin(phaseB));
            phaseA += 2.0f * s3g::kPi * 173.0f
                / static_cast<float>(sampleRate);
            phaseB += 2.0f * s3g::kPi * 421.0f
                / static_cast<float>(sampleRate);
            if (phaseA >= 2.0f * s3g::kPi) phaseA -= 2.0f * s3g::kPi;
            if (phaseB >= 2.0f * s3g::kPi) phaseB -= 2.0f * s3g::kPi;
            input[0][frame] = value;
            input[1][frame] = value * 0.45f;
            input[2][frame] = value * -0.30f;
            input[3][frame] = value * 0.72f;
        }
        processor.process(in.data(), out.data(), kChannels, kChannels, kBlock);
        float peak = 0.0f;
        if (!finiteAndGuarded(output, peak)) return false;
        maximumPeak = std::max(maximumPeak, peak);
        minimumExcitationGovernor = std::min(minimumExcitationGovernor,
            processor.excitationGovernorGain());
        minimumResponseGovernor = std::min(minimumResponseGovernor,
            processor.responseGovernorGain());
        minimumSafety = std::min(minimumSafety, processor.safetyGain());
        for (uint32_t channel = 0u; channel < kChannels; ++channel) {
            for (uint32_t frame = 0u; frame < kBlock; ++frame) {
                if (havePrevious || frame > 0u) {
                    const float previous = frame > 0u
                        ? output[channel][frame - 1u] : previousSample[channel];
                    const float step = std::abs(
                        output[channel][frame] - previous);
                    if (step > maximumStep) {
                        maximumStep = step;
                        maximumStepBlock = renderedBlocks;
                        maximumStepChannel = channel;
                        maximumStepFrame = frame;
                    }
                }
            }
            previousSample[channel] = output[channel][kBlock - 1u];
        }
        havePrevious = true;
        ++renderedBlocks;
        return true;
    };

    for (uint32_t block = 0u; block < 260u; ++block) {
        targetInputScale = block >= 12u && block < 36u ? 4.0f : 1.0f;
        if (block % 6u == 0u) {
            const uint32_t cycle = block / 6u;
            params = processor.params();
            params.responseEnabled = cycle % 3u != 0u ? 1u : 0u;
            params.tone = cycle % 2u ? 0.0f : 1.0f;
            params.responseGainDb = cycle % 2u ? -36.0f : 12.0f;
            params.mix = cycle % 3u ? 1.0f : 0.10f;
            params.outputGainDb = cycle % 2u ? -12.0f : 12.0f;
            params.topology = static_cast<s3g::AmbiEffectTopology>(cycle % 4u);
            params.topologyAmount = cycle % 2u ? 0.0f : 1.0f;
            params.roamingRateHz = cycle % 2u ? 0.005f : 2.0f;
            params.maskAmount = cycle % 2u ? 1.0f : 0.0f;
            params.maskAzimuthDeg = cycle % 2u ? -179.0f : 179.0f;
            params.maskElevationDeg = cycle % 2u ? -89.0f : 89.0f;
            params.maskWidth = cycle % 2u ? 0.0f : 1.0f;
            params.maskCurve = cycle % 2u ? 1.0f : 0.0f;
            params.maskDry = cycle % 3u ? 0.0f : 1.0f;
            if (block >= 48u && block % 36u == 12u) {
                const uint32_t bodyIndex = (block / 36u) % bodies.size();
                params.body = bodies[bodyIndex];
                params.order = std::min<uint32_t>(7u, bodyIndex + 1u);
            }
            if (block == 30u) {
                params.tone = std::numeric_limits<float>::quiet_NaN();
            }
            processor.setParams(params);
        }
        if (!renderBlock()) return false;
    }

    params = processor.params();
    params.body = s3g::AmbiEffectBody::Tetra4;
    params.order = 1u;
    params.responseEnabled = 1u;
    params.responseGainDb = 12.0f;
    params.mix = 1.0f;
    params.outputGainDb = 12.0f;
    processor.setParams(params);
    for (uint32_t block = 0u; block < 64u; ++block) {
        if (!renderBlock()) return false;
    }
    if (!processor.hasResponse() || !processor.responseAudible()) return false;
    processor.clearResponse();
    const bool tailHeldForFade = processor.hasResponse()
        && processor.tailFrames() > processor.latencyFrames();
    for (uint32_t block = 0u; block < 96u; ++block) {
        if (!renderBlock()) return false;
    }
    const bool cleared = !processor.hasResponse()
        && !processor.preparingResponse() && processor.tailFrames() == 0u;

    params = processor.params();
    params.body = s3g::AmbiEffectBody::Tetra4;
    params.order = 1u;
    params.captureSeconds = 0.05f;
    processor.setParams(params);
    for (uint32_t block = 0u; block < 16u; ++block) {
        if (!renderBlock()) return false;
    }
    processor.startCapture();
    if (!processor.capturing()) return false;
    params.order = 3u;
    processor.setParams(params);
    const bool geometryCancelledCapture = !processor.capturing();

    const bool governed = minimumResponseGovernor < 0.95f;
    const bool excitationGoverned = minimumExcitationGovernor < 0.95f;
    const bool outputGuarded = minimumSafety < 0.99f;
    const bool clickSafe = maximumStep < 0.35f;
    std::printf("Ambi Effect Response Trace safety: input-gov=%d min-input=%.4f return-gov=%d min-return=%.4f safe=%.4f peak=%.6f max-step=%.6f@%u/%u/%u clear-fade=%d capture-cancel=%d\n",
        excitationGoverned ? 1 : 0, minimumExcitationGovernor,
        governed ? 1 : 0, minimumResponseGovernor, minimumSafety,
        maximumPeak, maximumStep, maximumStepBlock, maximumStepChannel,
        maximumStepFrame, tailHeldForFade ? 1 : 0,
        geometryCancelledCapture ? 1 : 0);
    return excitationGoverned && governed && outputGuarded && clickSafe
        && maximumPeak > 0.01f
        && tailHeldForFade && cleared && geometryCancelledCapture;
#endif
}

} // namespace

int main()
{
    const bool partial = partialTraceSmoke();
    const bool partialSafety = partialTraceSafetySmoke();
    const bool response = responseTraceSmoke();
    const bool responseSafety = responseTraceSafetySmoke();
    if (!partial || !partialSafety || !response || !responseSafety) {
        std::fprintf(stderr,
            "Ambi Effect Trace smoke failed: partial=%d safety=%d response=%d response-safety=%d\n",
            partial ? 1 : 0, partialSafety ? 1 : 0,
            response ? 1 : 0, responseSafety ? 1 : 0);
        return 1;
    }
    std::puts("Ambi Effect Trace smoke passed");
    return 0;
}
