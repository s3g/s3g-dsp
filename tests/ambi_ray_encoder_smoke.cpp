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

} // namespace

int main()
{
    const auto descriptor = s3g::makeDefaultAmbiRayDescriptor();
    if (descriptor.cells.size() != 8u || descriptor.room.polygon.size() != 4u) {
        std::cerr << "Ambi Ray default scene was not constructed\n";
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
    const float reflectionDirectionChange = std::abs(referenceResolved.direction.x - movedResolved.direction.x)
        + std::abs(referenceResolved.direction.y - movedResolved.direction.y)
        + std::abs(referenceResolved.direction.z - movedResolved.direction.z);
    if (!std::isfinite(movedResolved.delayMs) || !std::isfinite(movedResolved.gain)
        || std::abs(referenceResolved.delayMs - referenceReflection.delayMs) > 0.05f
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
    const float rightEnergy = channelEnergy(directOutput.channels[1]);
    const float frontEnergy = channelEnergy(directOutput.channels[3]);
    if (!(rightEnergy > frontEnergy * 2.0f) || channelEnergy(directOutput.channels[0]) <= 0.0f) {
        std::cerr << "Ambi Ray direct path did not encode its source position: "
                  << rightEnergy << " / " << frontEnergy << "\n";
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
    if (!finiteBuffer(listenerOutput) || !(listenerFrontEnergy > listenerRightEnergy * 2.0f)) {
        std::cerr << "Ambi Ray direct path did not follow listener position: "
                  << listenerRightEnergy << " / " << listenerFrontEnergy << "\n";
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

    std::cout << "Ambi Ray direct right/front energy: " << rightEnergy << " / " << frontEnergy << "\n";
    std::cout << "Ambi Ray listener front/right energy: " << listenerFrontEnergy << " / " << listenerRightEnergy << "\n";
    std::cout << "Ambi Ray room early/tail/directional energy: "
              << earlyEnergy << " / " << tailEnergy << " / " << directionalEnergy << "\n";
    std::cout << "Ambi Ray motion peak/step: " << peak << " / " << maximumStep << "\n";
    return 0;
}
