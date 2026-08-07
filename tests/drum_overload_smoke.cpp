#include "s3g_drum_overload.h"
#include "s3g_drum_floor_tom.h"
#include "s3g_drum_toms.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>

namespace {

constexpr double kSampleRate = 48000.0;
constexpr uint32_t kFrames = 48000u;

struct StereoSample {
    float left = 0.0f;
    float right = 0.0f;
};

float pseudoNoise(uint32_t frame)
{
    uint32_t value = frame * 747796405u + 2891336453u;
    value = ((value >> ((value >> 28u) + 4u)) ^ value) * 277803737u;
    value = (value >> 22u) ^ value;
    return static_cast<float>(value) * (2.0f / 4294967295.0f) - 1.0f;
}

StereoSample cleanDrumSample(uint32_t frame)
{
    const uint32_t beatFrame = frame % 12000u;
    const float time = static_cast<float>(beatFrame / kSampleRate);
    const float kickEnvelope = std::exp(-time * 15.0f);
    const float kickPhase = 2.0f * s3g::kPi
        * (48.0f * time + 5.2f * (1.0f - std::exp(-time * 30.0f)));
    const float kick = 0.78f * kickEnvelope * std::sin(kickPhase);

    const uint32_t snareFrame = (beatFrame + 6000u) % 12000u;
    const float snareTime = static_cast<float>(snareFrame / kSampleRate);
    const float snareEnvelope = std::exp(-snareTime * 24.0f);
    const float snare = snareFrame < 5000u
        ? snareEnvelope * (0.34f * pseudoNoise(frame)
            + 0.24f * std::sin(2.0f * s3g::kPi * 185.0f * snareTime))
        : 0.0f;
    const float click = beatFrame < 32u
        ? (1.0f - static_cast<float>(beatFrame) / 32.0f) * 0.52f
        : 0.0f;
    return {
        kick + snare + click,
        kick * 0.97f - snare * 0.82f + click * 0.91f,
    };
}

struct Render {
    std::vector<float> left;
    std::vector<float> right;
};

Render render(const s3g::DrumOverloadParams& params,
    uint32_t frames = kFrames)
{
    s3g::DrumOverload overload;
    overload.setParams(params);
    overload.prepare(kSampleRate);
    Render result { std::vector<float>(frames), std::vector<float>(frames) };
    for (uint32_t frame = 0u; frame < frames; ++frame) {
        auto sample = cleanDrumSample(frame);
        overload.processFrame(sample.left, sample.right);
        result.left[frame] = sample.left;
        result.right[frame] = sample.right;
    }
    return result;
}

float rms(const std::vector<float>& samples, uint32_t first = 0u)
{
    double energy = 0.0;
    for (uint32_t frame = first; frame < samples.size(); ++frame) {
        energy += static_cast<double>(samples[frame]) * samples[frame];
    }
    return static_cast<float>(std::sqrt(
        energy / std::max<size_t>(1u, samples.size() - first)));
}

float peak(const std::vector<float>& samples)
{
    float result = 0.0f;
    for (float sample : samples) result = std::max(result, std::abs(sample));
    return result;
}

float differenceRms(const std::vector<float>& a,
    const std::vector<float>& b)
{
    double energy = 0.0;
    for (uint32_t frame = 0u; frame < a.size(); ++frame) {
        const double difference = static_cast<double>(a[frame]) - b[frame];
        energy += difference * difference;
    }
    return static_cast<float>(std::sqrt(energy / a.size()));
}

bool sanitationProbe()
{
    s3g::DrumOverload overload;
    overload.setParams({
        static_cast<s3g::DrumOverloadCircuit>(999u),
        std::numeric_limits<float>::infinity(),
        -2.0f,
        8.0f,
        -9.0f,
        std::numeric_limits<float>::quiet_NaN(),
        4.0f,
        -1.0f,
        5.0f,
        -3.0f,
        2.0f,
        -90.0f,
        true,
    });
    const auto params = overload.params();
    const bool ok = params.circuit == s3g::DrumOverloadCircuit::Speaker
        && params.inputGainDb == 0.0f
        && params.overload == 0.0f
        && params.density == 1.0f
        && params.punch == -1.0f
        && params.bias == 0.0f
        && params.breakup == 1.0f
        && params.weight == 0.0f
        && params.tone == 1.0f
        && params.stereoLink == 0.0f
        && params.mix == 1.0f
        && params.outputGainDb == -36.0f
        && params.bypass;
    if (!ok) std::cerr << "Drum Overload parameter sanitation failed\n";
    return ok;
}

bool bypassProbe()
{
    s3g::DrumOverloadParams params;
    params.circuit = s3g::DrumOverloadCircuit::Rupture;
    params.inputGainDb = 24.0f;
    params.overload = 1.0f;
    params.density = 1.0f;
    params.breakup = 1.0f;
    params.outputGainDb = 12.0f;
    params.bypass = true;

    s3g::DrumOverload overload;
    overload.setParams(params);
    overload.prepare(kSampleRate);
    for (uint32_t frame = 0u; frame < 8192u; ++frame) {
        const auto source = cleanDrumSample(frame);
        float left = source.left;
        float right = source.right;
        overload.processFrame(left, right);
        if (left != source.left || right != source.right) {
            std::cerr << "Drum Overload bypass was not sample exact\n";
            return false;
        }
    }
    return true;
}

bool safetyProbe()
{
    s3g::DrumOverloadParams params;
    params.circuit = s3g::DrumOverloadCircuit::Rupture;
    params.inputGainDb = 24.0f;
    params.overload = 1.0f;
    params.density = 1.0f;
    params.punch = 1.0f;
    params.bias = 1.0f;
    params.breakup = 1.0f;
    params.weight = 1.0f;
    params.tone = 1.0f;
    params.mix = 1.0f;
    params.outputGainDb = 12.0f;

    s3g::DrumOverload overload;
    overload.setParams(params);
    overload.prepare(std::numeric_limits<double>::quiet_NaN());
    for (uint32_t frame = 0u; frame < 100000u; ++frame) {
        float left = frame % 13u == 0u
            ? std::numeric_limits<float>::quiet_NaN()
            : ((frame & 1u) ? 1.0e30f : -1.0e30f);
        float right = frame % 17u == 0u
            ? std::numeric_limits<float>::infinity()
            : pseudoNoise(frame) * 1.0e22f;
        overload.processFrame(left, right);
        if (!std::isfinite(left) || !std::isfinite(right)
            || std::abs(left) > 8.0f || std::abs(right) > 8.0f) {
            std::cerr << "Drum Overload safety bound failed at "
                      << frame << "\n";
            return false;
        }
    }
    return true;
}

bool deterministicResetProbe()
{
    s3g::DrumOverloadParams params;
    params.circuit = s3g::DrumOverloadCircuit::Valve;
    params.overload = 0.78f;
    params.density = 0.67f;
    params.punch = 0.51f;
    params.bias = -0.38f;
    params.breakup = 0.29f;

    s3g::DrumOverload overload;
    overload.setParams(params);
    overload.prepare(kSampleRate);
    std::array<float, 4096u> first {};
    std::array<float, 4096u> second {};
    for (uint32_t pass = 0u; pass < 2u; ++pass) {
        overload.reset();
        auto& destination = pass == 0u ? first : second;
        for (uint32_t frame = 0u; frame < destination.size(); ++frame) {
            auto sample = cleanDrumSample(frame);
            overload.processFrame(sample.left, sample.right);
            destination[frame] = sample.left;
        }
    }
    if (first != second) {
        std::cerr << "Drum Overload reset was not deterministic\n";
        return false;
    }
    return true;
}

bool stereoIsolationProbe()
{
    s3g::DrumOverloadParams params;
    params.circuit = s3g::DrumOverloadCircuit::Valve;
    params.overload = 0.9f;
    params.density = 0.8f;
    params.bias = 0.8f;
    params.stereoLink = 1.0f;
    params.mix = 1.0f;

    s3g::DrumOverload overload;
    overload.setParams(params);
    overload.prepare(kSampleRate);
    for (uint32_t frame = 0u; frame < 12000u; ++frame) {
        float left = cleanDrumSample(frame).left;
        float right = 0.0f;
        overload.processFrame(left, right);
        if (right != 0.0f) {
            std::cerr << "Drum Overload leaked audio between stereo lanes\n";
            return false;
        }
    }
    return true;
}

bool circuitProbe()
{
    s3g::DrumOverloadParams params;
    params.inputGainDb = 3.0f;
    params.overload = 0.86f;
    params.breakup = 0.58f;
    params.mix = 1.0f;
    params.outputGainDb = -6.0f;
    std::array<Render, s3g::kDrumOverloadCircuitCount> renders;
    for (uint32_t circuit = 0u;
         circuit < s3g::kDrumOverloadCircuitCount; ++circuit) {
        params.circuit = static_cast<s3g::DrumOverloadCircuit>(circuit);
        renders[circuit] = render(params, 16000u);
    }
    for (uint32_t circuit = 1u;
         circuit < s3g::kDrumOverloadCircuitCount; ++circuit) {
        const float difference = differenceRms(
            renders[0u].left, renders[circuit].left);
        if (!(difference > 0.015f)) {
            std::cerr << "Drum Overload circuit was not distinct: "
                      << circuit << " / " << difference << "\n";
            return false;
        }
    }
    return true;
}

bool drumTuningProbe()
{
    s3g::DrumOverloadParams dryParams;
    dryParams.bypass = true;
    const auto dry = render(dryParams);

    s3g::DrumOverloadParams wetParams;
    wetParams.circuit = s3g::DrumOverloadCircuit::Console;
    const auto wet = render(wetParams);
    const float dryCrest = peak(dry.left) / std::max(rms(dry.left), 1.0e-6f);
    const float wetCrest = peak(wet.left) / std::max(rms(wet.left), 1.0e-6f);
    const float difference = differenceRms(dry.left, wet.left);
    if (!(difference > 0.04f) || !(wetCrest < dryCrest * 0.97f)) {
        std::cerr << "Default Drum Overload voicing did not create dense "
                  << "drum overload: diff=" << difference
                  << " crest=" << dryCrest << "/" << wetCrest << "\n";
        return false;
    }
    return true;
}

float renderQuietLaneRms(float link)
{
    s3g::DrumOverloadParams params;
    params.inputGainDb = 12.0f;
    params.overload = 0.35f;
    params.density = 1.0f;
    params.punch = 0.0f;
    params.weight = 0.0f;
    params.stereoLink = link;
    params.mix = 1.0f;
    params.outputGainDb = 0.0f;
    s3g::DrumOverload overload;
    overload.setParams(params);
    overload.prepare(kSampleRate);
    std::vector<float> quiet(12000u);
    for (uint32_t frame = 0u; frame < quiet.size(); ++frame) {
        const float time = static_cast<float>(frame / kSampleRate);
        float left = 0.9f * std::sin(2.0f * s3g::kPi * 97.0f * time);
        float right = 0.055f * std::sin(2.0f * s3g::kPi * 311.0f * time);
        overload.processFrame(left, right);
        quiet[frame] = right;
    }
    return rms(quiet, 3000u);
}

bool stereoLinkProbe()
{
    const float independent = renderQuietLaneRms(0.0f);
    const float linked = renderQuietLaneRms(1.0f);
    if (!(linked < independent * 0.90f)) {
        std::cerr << "Stereo link did not share gain reduction: "
                  << independent << " / " << linked << "\n";
        return false;
    }
    return true;
}

bool completedTomFamilyProbe()
{
    s3g::DrumFloorTom floorTom;
    s3g::DrumFloorTomParams floorParams;
    floorParams.stereoWidth = 0.72f;
    floorTom.setParams(floorParams);
    floorTom.prepare(kSampleRate);

    s3g::DrumToms toms;
    s3g::DrumTomsParams tomParams;
    tomParams.stereoWidth = 0.88f;
    toms.setParams(tomParams);
    toms.prepare(kSampleRate);

    s3g::DrumOverloadParams overloadParams;
    overloadParams.circuit = s3g::DrumOverloadCircuit::Transformer;
    overloadParams.overload = 0.68f;
    overloadParams.density = 0.46f;
    overloadParams.weight = 0.84f;
    overloadParams.mix = 0.86f;
    s3g::DrumOverload overload;
    overload.setParams(overloadParams);
    overload.prepare(kSampleRate);

    std::vector<float> dryLeft(kFrames);
    std::vector<float> dryRight(kFrames);
    std::vector<float> wetLeft(kFrames);
    std::vector<float> wetRight(kFrames);
    for (uint32_t frame = 0u; frame < kFrames; ++frame) {
        if (frame == 0u || frame == 30000u) {
            floorTom.trigger(s3g::DrumTomArticulation::Head,
                frame == 0u ? 1.0f : 0.78f, 43);
        }
        if (frame == 9000u) {
            toms.trigger(s3g::DrumTomSlot::Low,
                s3g::DrumTomArticulation::Head, 0.94f, 45);
        } else if (frame == 17000u) {
            toms.trigger(s3g::DrumTomSlot::Mid,
                s3g::DrumTomArticulation::Head, 0.88f, 48);
        } else if (frame == 24000u) {
            toms.trigger(s3g::DrumTomSlot::High,
                s3g::DrumTomArticulation::Head, 0.82f, 50);
        } else if (frame == 38000u) {
            toms.trigger(s3g::DrumTomSlot::Mid,
                s3g::DrumTomArticulation::RimStick, 0.90f, 48);
        }

        float floorLeft = 0.0f;
        float floorRight = 0.0f;
        float rackLeft = 0.0f;
        float rackRight = 0.0f;
        floorTom.processFrame(floorLeft, floorRight);
        toms.processFrame(rackLeft, rackRight);
        const float sourceLeft = floorLeft + rackLeft;
        const float sourceRight = floorRight + rackRight;
        dryLeft[frame] = sourceLeft;
        dryRight[frame] = sourceRight;
        wetLeft[frame] = sourceLeft;
        wetRight[frame] = sourceRight;
        overload.processFrame(wetLeft[frame], wetRight[frame]);
        if (!std::isfinite(wetLeft[frame])
            || !std::isfinite(wetRight[frame])) {
            std::cerr << "Completed tom-family render produced invalid output\n";
            return false;
        }
    }

    std::vector<float> drySide(kFrames);
    std::vector<float> wetSide(kFrames);
    for (uint32_t frame = 0u; frame < kFrames; ++frame) {
        drySide[frame] = (dryLeft[frame] - dryRight[frame]) * 0.5f;
        wetSide[frame] = (wetLeft[frame] - wetRight[frame]) * 0.5f;
    }
    const float difference = differenceRms(dryLeft, wetLeft);
    const float drySideRms = rms(drySide);
    const float wetSideRms = rms(wetSide);
    if (!(difference > 0.01f) || !(drySideRms > 1.0e-4f)
        || !(wetSideRms > drySideRms * 0.30f)
        || peak(wetLeft) > 8.0f || peak(wetRight) > 8.0f) {
        std::cerr << "Drum Overload tom-family tuning failed: diff="
                  << difference << " side=" << drySideRms
                  << "/" << wetSideRms << "\n";
        return false;
    }
    return true;
}

} // namespace

int main()
{
    if (!sanitationProbe() || !bypassProbe() || !safetyProbe()
        || !deterministicResetProbe() || !stereoIsolationProbe()
        || !circuitProbe() || !drumTuningProbe() || !stereoLinkProbe()
        || !completedTomFamilyProbe()) {
        return 1;
    }
    std::cout << "s3g Drum Overload smoke passed\n";
    return 0;
}
