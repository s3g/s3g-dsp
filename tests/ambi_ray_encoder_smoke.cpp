#include "s3g_ambi_ray_encoder.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {

constexpr double kSampleRate = 48000.0;
constexpr uint32_t kBlockFrames = 128u;

struct RenderBuffer {
    std::array<std::vector<float>, s3g::kAmbiRayMaxChannels> channels;
    std::array<float*, s3g::kAmbiRayMaxChannels> pointers {};

    explicit RenderBuffer(uint32_t frames)
    {
        for (uint32_t channel = 0u; channel < channels.size(); ++channel) {
            channels[channel].assign(frames, 0.0f);
            pointers[channel] = channels[channel].data();
        }
    }
};

bool finiteBuffer(const RenderBuffer& buffer)
{
    for (const auto& channel : buffer.channels) {
        for (float value : channel) {
            if (!std::isfinite(value)) return false;
        }
    }
    return true;
}

float channelEnergy(const std::vector<float>& channel)
{
    double energy = 0.0;
    for (float value : channel) energy += static_cast<double>(value) * static_cast<double>(value);
    return static_cast<float>(energy);
}

float channelDot(const std::vector<float>& a, const std::vector<float>& b)
{
    double result = 0.0;
    const size_t count = std::min(a.size(), b.size());
    for (size_t index = 0u; index < count; ++index)
        result += static_cast<double>(a[index]) * static_cast<double>(b[index]);
    return static_cast<float>(result);
}

double tonePower(const std::vector<float>& signal, double frequency)
{
    double real = 0.0;
    double imaginary = 0.0;
    for (size_t frame = 0u; frame < signal.size(); ++frame) {
        const double phase = 2.0 * static_cast<double>(s3g::kPi) * frequency
            * static_cast<double>(frame) / kSampleRate;
        real += static_cast<double>(signal[frame]) * std::cos(phase);
        imaginary -= static_cast<double>(signal[frame]) * std::sin(phase);
    }
    return real * real + imaginary * imaginary;
}

struct DopplerProbe {
    std::array<std::vector<float>, 3u> outputs;
    float physicalError = 0.0f;
};

DopplerProbe renderDopplerProbe()
{
    constexpr uint32_t frames = 96000u;
    constexpr uint32_t warmupFrames = 12000u;
    constexpr float inputFrequency = 200.0f;
    constexpr float initialDelayFrames = 1024.0f;
    constexpr float delaySlope = 0.08f;
    s3g::ambi_ray_detail::FractionalDelay delay;
    delay.prepare(kSampleRate, 4.0f);
    std::array<s3g::ambi_ray_detail::DopplerDelayTap, 3u> taps;
    for (auto& tap : taps) {
        tap.prepare(kSampleRate);
        tap.setTarget(initialDelayFrames, 1u, true);
    }

    DopplerProbe result;
    for (auto& output : result.outputs) output.reserve(frames - warmupFrames);
    float phase = 0.0f;
    for (uint32_t frame = 0u; frame < frames; ++frame) {
        if (frame % 32u == 0u) {
            const float target = initialDelayFrames + delaySlope * static_cast<float>(frame + 32u);
            for (auto& tap : taps) tap.setTarget(target, 32u, false);
        }
        delay.write(std::sin(phase));
        const float physical = delay.read(taps[1].physicalDelayFrames());
        const float stable = taps[0].read(delay, 0.0f);
        const float natural = taps[1].read(delay, 1.0f);
        const float exaggerated = taps[2].read(delay, 2.0f);
        result.physicalError = std::max(result.physicalError, std::abs(natural - physical));
        if (frame >= warmupFrames) {
            result.outputs[0].push_back(stable);
            result.outputs[1].push_back(natural);
            result.outputs[2].push_back(exaggerated);
        }
        delay.advance();
        phase += 2.0f * s3g::kPi * inputFrequency / static_cast<float>(kSampleRate);
        if (phase > 2.0f * s3g::kPi) phase -= 2.0f * s3g::kPi;
    }
    return result;
}

struct FieldListenProbe {
    std::array<float, s3g::kAmbiFieldListenerMaxLobes> envelope {};
    std::array<float, s3g::kAmbiFieldListenerMaxLobes> weight {};
    float activity = 0.0f;
};

FieldListenProbe renderFieldListenProbe(
    const s3g::AmbiRayDescriptor& descriptor,
    s3g::AmbiFieldListenMode mode,
    bool directOnly = false)
{
    s3g::AmbiRayEncoder encoder;
    s3g::AmbiRayEncoderParams params;
    params.order = 3u;
    params.direct = directOnly ? 1.0f : 0.0f;
    params.early = directOnly ? 0.0f : 1.0f;
    params.late = 0.0f;
    params.outputGainDb = -24.0f;
    params.fieldListenMode = mode;
    encoder.setParams(params);
    if (!encoder.prepare(kSampleRate, descriptor)) return {};

    std::array<float, kBlockFrames> input {};
    RenderBuffer output(kBlockFrames);
    uint32_t noise = 0x8a31f25du;
    for (uint32_t block = 0u; block < 800u; ++block) {
        for (uint32_t frame = 0u; frame < kBlockFrames; ++frame) {
            noise = noise * 1664525u + 1013904223u;
            input[frame] = (static_cast<float>((noise >> 8u) & 0xffffu)
                / 32767.5f - 1.0f) * 0.08f;
        }
        encoder.process(input.data(), output.pointers.data(), 16u, kBlockFrames);
    }

    FieldListenProbe result;
    result.activity = encoder.fieldListenActivity();
    for (uint32_t lobe = 0u; lobe < result.envelope.size(); ++lobe) {
        result.envelope[lobe] = encoder.fieldListenEnvelope(lobe);
        result.weight[lobe] = encoder.fieldListenWeight(lobe);
    }
    return result;
}

} // namespace

int main()
{
    const auto descriptor = s3g::makeDefaultAmbiRayDescriptor();
    if (descriptor.cells.size() != 8u || descriptor.room.polygon.size() != 4u) {
        std::cerr << "Ambi Ray default scene was not constructed\n";
        return 1;
    }
    if (std::abs(s3g::AmbiRayEncoderParams {}.doppler - 0.5f) > 0.0001f
        || s3g::sanitizeAmbiRayEncoderParams({ .doppler = 3.0f }).doppler != 2.0f) {
        std::cerr << "Ambi Ray Doppler parameter defaults or bounds changed\n";
        return 1;
    }

    const auto dopplerProbe = renderDopplerProbe();
    const std::array<double, 3u> probeFrequencies { 200.0, 184.0, 168.0 };
    std::array<double, 3u> probePowers {};
    for (uint32_t index = 0u; index < probePowers.size(); ++index)
        probePowers[index] = tonePower(dopplerProbe.outputs[index], probeFrequencies[index]);
    if (dopplerProbe.physicalError > 0.00001f
        || probePowers[0] <= tonePower(dopplerProbe.outputs[0], probeFrequencies[1]) * 4.0
        || probePowers[1] <= tonePower(dopplerProbe.outputs[1], probeFrequencies[0]) * 8.0
        || probePowers[2] <= tonePower(dopplerProbe.outputs[2], probeFrequencies[1]) * 4.0) {
        std::cerr << "Ambi Ray Doppler scaling failed: " << dopplerProbe.physicalError << " / "
                  << probePowers[0] << " / " << probePowers[1] << " / " << probePowers[2] << "\n";
        return 1;
    }
    const s3g::Vec3 leftDirection = s3g::ambi_ray_detail::ambisonicDirectionFromWorldVector({ -1.0f, 0.0f, 0.0f });
    const s3g::Vec3 rightDirection = s3g::ambi_ray_detail::ambisonicDirectionFromWorldVector({ 1.0f, 0.0f, 0.0f });
    const s3g::Vec3 frontDirection = s3g::ambi_ray_detail::ambisonicDirectionFromWorldVector({ 0.0f, 1.0f, 0.0f });
    const auto leftAed = s3g::ambi_ray_detail::aedFromWorldVector({ -1.0f, 0.0f, 0.0f });
    if (leftDirection.y < 0.999f || rightDirection.y > -0.999f || frontDirection.x < 0.999f
        || std::abs(leftAed[0] - 90.0f) > 0.001f) {
        std::cerr << "Ambi Ray world-to-ambisonic orientation is reversed\n";
        return 1;
    }
    for (const auto& cell : descriptor.cells) {
        for (const auto& reflection : cell.reflections) {
            const auto& bounce = reflection.bouncePositionMetres;
            if (!reflection.hasBouncePosition || !std::isfinite(bounce.x)
                || !std::isfinite(bounce.y) || !std::isfinite(bounce.z)) {
                std::cerr << "Ambi Ray default reflection has no display bounce point\n";
                return 1;
            }
        }
    }
    const auto& referenceCell = descriptor.cells.front();
    const auto& referenceReflection = referenceCell.reflections.front();
    const auto referenceResolved = s3g::resolveAmbiRayReflection(
        descriptor, referenceCell, referenceReflection, referenceCell.positionMetres,
        descriptor.listenerPositionMetres);
    const s3g::Vec3 movedListener {
        descriptor.listenerPositionMetres.x + 1.0f,
        descriptor.listenerPositionMetres.y - 0.7f,
        descriptor.listenerPositionMetres.z + 0.35f
    };
    const auto movedResolved = s3g::resolveAmbiRayReflection(
        descriptor, referenceCell, referenceReflection, referenceCell.positionMetres, movedListener);
    const s3g::Vec3 referenceBounceWorld {
        referenceReflection.bouncePositionMetres.x - descriptor.listenerPositionMetres.x,
        referenceReflection.bouncePositionMetres.y - descriptor.listenerPositionMetres.y,
        referenceReflection.bouncePositionMetres.z - descriptor.listenerPositionMetres.z
    };
    const s3g::Vec3 expectedReferenceDirection =
        s3g::ambi_ray_detail::ambisonicDirectionFromWorldVector(referenceBounceWorld);
    const float referenceDirectionDot = referenceResolved.direction.x * expectedReferenceDirection.x
        + referenceResolved.direction.y * expectedReferenceDirection.y
        + referenceResolved.direction.z * expectedReferenceDirection.z;
    const float reflectionDirectionChange = std::abs(referenceResolved.direction.x - movedResolved.direction.x)
        + std::abs(referenceResolved.direction.y - movedResolved.direction.y)
        + std::abs(referenceResolved.direction.z - movedResolved.direction.z);
    if (!std::isfinite(movedResolved.delayMs) || !std::isfinite(movedResolved.gain)
        || std::abs(referenceResolved.delayMs - referenceReflection.delayMs) > 0.05f
        || referenceDirectionDot < 0.999f
        || reflectionDirectionChange < 0.01f) {
        std::cerr << "Ambi Ray listener-aware reflection resolution failed\n";
        return 1;
    }

    s3g::AmbiRayEncoder directEncoder;
    s3g::AmbiRayEncoderParams directParams;
    directParams.order = 3u;
    directParams.sourceX = 0.08f;
    directParams.sourceY = 0.50f;
    directParams.sourceZ = 0.50f;
    directParams.direct = 1.0f;
    directParams.early = 0.0f;
    directParams.late = 0.0f;
    directParams.outputGainDb = -12.0f;
    directEncoder.setParams(directParams);
    if (!directEncoder.prepare(kSampleRate, descriptor)) {
        std::cerr << "Ambi Ray direct encoder did not prepare\n";
        return 1;
    }

    constexpr uint32_t directFrames = 4096u;
    std::vector<float> directInput(directFrames, 0.0f);
    directInput[0] = 1.0f;
    RenderBuffer directOutput(directFrames);
    directEncoder.process(directInput.data(), directOutput.pointers.data(), 16u, directFrames);
    if (!finiteBuffer(directOutput)) {
        std::cerr << "Ambi Ray direct render produced non-finite output\n";
        return 1;
    }
    const float lateralEnergy = channelEnergy(directOutput.channels[1]);
    const float frontEnergy = channelEnergy(directOutput.channels[3]);
    const float leftCorrelation = channelDot(directOutput.channels[0], directOutput.channels[1]);
    if (!(lateralEnergy > frontEnergy * 2.0f) || leftCorrelation <= 0.0f
        || channelEnergy(directOutput.channels[0]) <= 0.0f) {
        std::cerr << "Ambi Ray direct path did not encode source-left correctly: "
                  << lateralEnergy << " / " << frontEnergy << " / " << leftCorrelation << "\n";
        return 1;
    }

    s3g::AmbiRayEncoder rightEncoder;
    auto rightParams = directParams;
    rightParams.sourceX = 0.92f;
    rightEncoder.setParams(rightParams);
    if (!rightEncoder.prepare(kSampleRate, descriptor)) return 1;
    RenderBuffer rightOutput(directFrames);
    rightEncoder.process(directInput.data(), rightOutput.pointers.data(), 16u, directFrames);
    const float rightCorrelation = channelDot(rightOutput.channels[0], rightOutput.channels[1]);
    if (!finiteBuffer(rightOutput) || rightCorrelation >= 0.0f) {
        std::cerr << "Ambi Ray direct path did not encode source-right correctly: "
                  << rightCorrelation << "\n";
        return 1;
    }

    s3g::AmbiRayEncoder listenerEncoder;
    auto listenerParams = directParams;
    listenerParams.sourceX = 0.5f;
    listenerParams.sourceY = 0.82f;
    listenerParams.listenerX = 0.5f;
    listenerParams.listenerY = 0.12f;
    listenerEncoder.setParams(listenerParams);
    if (!listenerEncoder.prepare(kSampleRate, descriptor)) return 1;
    RenderBuffer listenerOutput(directFrames);
    listenerEncoder.process(directInput.data(), listenerOutput.pointers.data(), 16u, directFrames);
    const float listenerRightEnergy = channelEnergy(listenerOutput.channels[1]);
    const float listenerFrontEnergy = channelEnergy(listenerOutput.channels[3]);
    const float frontCorrelation = channelDot(listenerOutput.channels[0], listenerOutput.channels[3]);
    if (!finiteBuffer(listenerOutput) || !(listenerFrontEnergy > listenerRightEnergy * 2.0f)
        || frontCorrelation <= 0.0f) {
        std::cerr << "Ambi Ray direct path did not follow listener position: "
                  << listenerRightEnergy << " / " << listenerFrontEnergy << " / " << frontCorrelation << "\n";
        return 1;
    }

    s3g::AmbiRayEncoder roomEncoder;
    s3g::AmbiRayEncoderParams roomParams;
    roomParams.order = 3u;
    roomParams.sourceX = 0.5f;
    roomParams.sourceY = 0.25f;
    roomParams.sourceZ = 0.5f;
    roomParams.direct = 0.0f;
    roomParams.early = 0.85f;
    roomParams.late = 0.75f;
    roomParams.outputGainDb = -12.0f;
    roomEncoder.setParams(roomParams);
    if (!roomEncoder.prepare(kSampleRate, descriptor)) {
        std::cerr << "Ambi Ray room encoder did not prepare\n";
        return 1;
    }

    constexpr uint32_t roomFrames = 48000u;
    std::vector<float> impulse(roomFrames, 0.0f);
    impulse[0] = 1.0f;
    RenderBuffer roomOutput(roomFrames);
    roomEncoder.process(impulse.data(), roomOutput.pointers.data(), 16u, roomFrames);
    if (!finiteBuffer(roomOutput)) {
        std::cerr << "Ambi Ray room render produced non-finite output\n";
        return 1;
    }
    double earlyEnergy = 0.0;
    double tailEnergy = 0.0;
    double directionalEnergy = 0.0;
    for (uint32_t frame = 0u; frame < roomFrames; ++frame) {
        const double value = roomOutput.channels[0][frame];
        if (frame < 8000u) earlyEnergy += value * value;
        if (frame >= 12000u) tailEnergy += value * value;
        directionalEnergy += static_cast<double>(roomOutput.channels[1][frame]) * roomOutput.channels[1][frame];
    }
    if (earlyEnergy <= 1.0e-8 || tailEnergy <= 1.0e-9 || directionalEnergy <= 1.0e-9) {
        std::cerr << "Ambi Ray early/late directional field was silent: "
                  << earlyEnergy << " / " << tailEnergy << " / " << directionalEnergy << "\n";
        return 1;
    }

    auto listenerDescriptor = descriptor;
    for (auto& cell : listenerDescriptor.cells) {
        for (auto& reflection : cell.reflections) {
            reflection.gain *= reflection.slot == 0u ? 4.0f : 0.08f;
        }
    }
    const auto offListen = renderFieldListenProbe(
        listenerDescriptor, s3g::AmbiFieldListenMode::Off);
    const auto followListen = renderFieldListenProbe(
        listenerDescriptor, s3g::AmbiFieldListenMode::Follow);
    const auto counterListen = renderFieldListenProbe(
        listenerDescriptor, s3g::AmbiFieldListenMode::Counter);
    const auto balanceListen = renderFieldListenProbe(
        listenerDescriptor, s3g::AmbiFieldListenMode::Balance);
    uint32_t strongestLobe = 0u;
    for (uint32_t lobe = 1u; lobe < offListen.envelope.size(); ++lobe) {
        if (offListen.envelope[lobe] > offListen.envelope[strongestLobe])
            strongestLobe = lobe;
    }
    const uint32_t oppositeLobe =
        static_cast<uint32_t>(offListen.envelope.size() - 1u) - strongestLobe;
    for (float weight : offListen.weight) {
        if (std::abs(weight - 1.0f) > 0.00001f) {
            std::cerr << "Ambi Ray Field Listen Off changed room weights\n";
            return 1;
        }
    }
    if (!(offListen.activity > 0.02f)
        || !(followListen.weight[strongestLobe] > 1.02f)
        || !(counterListen.weight[oppositeLobe]
            > counterListen.weight[strongestLobe] + 0.03f)
        || !(balanceListen.weight[strongestLobe] < 0.98f)) {
        std::cerr << "Ambi Ray Field Listen modes did not reshape the room: "
                  << strongestLobe << " / " << oppositeLobe << ", follow "
                  << followListen.weight[strongestLobe] << ", counter "
                  << counterListen.weight[strongestLobe] << " / "
                  << counterListen.weight[oppositeLobe] << ", balance "
                  << balanceListen.weight[strongestLobe] << "\n";
        return 1;
    }
    const auto directOnlyListen = renderFieldListenProbe(
        listenerDescriptor, s3g::AmbiFieldListenMode::Follow, true);
    if (directOnlyListen.activity > 0.00001f) {
        std::cerr << "Ambi Ray direct path leaked into Field Listen: "
                  << directOnlyListen.activity << "\n";
        return 1;
    }

    s3g::AmbiRayEncoder movingEncoder;
    auto movingParams = directParams;
    movingParams.sourceX = 0.1f;
    movingParams.sourceY = 0.28f;
    movingParams.movementMs = 45.0f;
    movingParams.direct = 0.75f;
    movingParams.early = 0.45f;
    movingParams.late = 0.25f;
    movingEncoder.setParams(movingParams);
    if (!movingEncoder.prepare(kSampleRate, descriptor)) return 1;
    std::array<float, kBlockFrames> movingInput {};
    RenderBuffer movingOutput(kBlockFrames);
    float phase = 0.0f;
    float previous = 0.0f;
    float peak = 0.0f;
    float maximumStep = 0.0f;
    for (uint32_t block = 0u; block < 240u; ++block) {
        movingParams.sourceX = 0.1f + 0.8f * static_cast<float>(block) / 239.0f;
        movingParams.sourceY = 0.28f + 0.44f * static_cast<float>(block % 80u) / 79.0f;
        movingParams.listenerX = 0.82f - 0.46f * static_cast<float>(block) / 239.0f;
        movingParams.listenerY = 0.72f - 0.28f * static_cast<float>(block % 96u) / 95.0f;
        movingParams.listenerZ = 0.30f + 0.40f * static_cast<float>(block % 120u) / 119.0f;
        movingEncoder.setParams(movingParams);
        for (uint32_t frame = 0u; frame < kBlockFrames; ++frame) {
            movingInput[frame] = std::sin(phase) * 0.12f;
            phase += 2.0f * s3g::kPi * 233.0f / static_cast<float>(kSampleRate);
            if (phase > 2.0f * s3g::kPi) phase -= 2.0f * s3g::kPi;
            for (auto& channel : movingOutput.channels) channel[frame] = 0.0f;
        }
        movingEncoder.process(movingInput.data(), movingOutput.pointers.data(), 16u, kBlockFrames);
        if (!finiteBuffer(movingOutput)) {
            std::cerr << "Ambi Ray moving source/listener produced non-finite output\n";
            return 1;
        }
        for (uint32_t frame = 0u; frame < kBlockFrames; ++frame) {
            const float value = movingOutput.channels[0][frame];
            peak = std::max(peak, std::abs(value));
            maximumStep = std::max(maximumStep, std::abs(value - previous));
            previous = value;
        }
    }
    if (peak <= 0.0001f || peak > 1.001f || maximumStep > 0.25f) {
        std::cerr << "Ambi Ray source/listener automation was unstable: " << peak << " / " << maximumStep << "\n";
        return 1;
    }

    std::cout << "Ambi Ray direct lateral/front energy and left/right sign: "
              << lateralEnergy << " / " << frontEnergy << " / " << leftCorrelation << " / " << rightCorrelation << "\n";
    std::cout << "Ambi Ray listener front/right energy: " << listenerFrontEnergy << " / " << listenerRightEnergy << "\n";
    std::cout << "Ambi Ray room early/tail/directional energy: "
              << earlyEnergy << " / " << tailEnergy << " / " << directionalEnergy << "\n";
    std::cout << "Ambi Ray Field Listen activity/follow/counter/balance: "
              << offListen.activity << " / " << followListen.weight[strongestLobe]
              << " / " << counterListen.weight[oppositeLobe]
              << " / " << balanceListen.weight[strongestLobe] << "\n";
    std::cout << "Ambi Ray motion peak/step: " << peak << " / " << maximumStep << "\n";
    std::cout << "Ambi Ray Doppler 0/100/200 percent tone power: "
              << probePowers[0] << " / " << probePowers[1] << " / " << probePowers[2]
              << " (physical error " << dopplerProbe.physicalError << ")\n";
    return 0;
}
