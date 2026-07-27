#include "s3g_crcltr.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {

constexpr double kSampleRate = 1000.0;
constexpr uint32_t kBlockFrames = 25u;

bool finiteBuffer(const std::vector<float>& buffer)
{
    return std::all_of(buffer.begin(), buffer.end(),
        [](float value) { return std::isfinite(value); });
}

float energy(const std::vector<float>& buffer)
{
    float sum = 0.0f;
    for (float value : buffer) sum += value * value;
    return sum;
}

void render(s3g::Crcltr& dsp, const std::vector<float>& left,
            const std::vector<float>& right, std::vector<float>& outputLeft,
            std::vector<float>& outputRight)
{
    outputLeft.resize(left.size());
    outputRight.resize(left.size());
    dsp.process(left.data(), right.data(), outputLeft.data(), outputRight.data(),
        static_cast<uint32_t>(left.size()));
}

} // namespace

int main()
{
    bool ok = true;
    s3g::Crcltr dsp;
    ok = ok && dsp.prepare(kSampleRate, kBlockFrames);
    ok = ok && !dsp.usingExternalMemory();
    ok = ok && dsp.loopCapacityFrames() == 32000u;

    s3g::CrcltrParams params;
    params.blend = 0.0f;
    params.outputGain = 1.0f;
    dsp.setParams(params);

    std::vector<float> inputLeft(50u, 0.1f);
    std::vector<float> inputRight(50u, -0.1f);
    std::vector<float> outputLeft;
    std::vector<float> outputRight;
    render(dsp, inputLeft, inputRight, outputLeft, outputRight);
    ok = ok && finiteBuffer(outputLeft) && finiteBuffer(outputRight);
    ok = ok && energy(outputLeft) > 0.01f && energy(outputRight) > 0.01f;

    // A tap shorter than 100 ms must leave the current loop untouched.
    params.recordTarget = s3g::CrcltrRecordTarget::Loop1;
    params.record = true;
    dsp.setParams(params);
    render(dsp, inputLeft, inputRight, outputLeft, outputRight);
    params.record = false;
    dsp.setParams(params);
    std::vector<float> oneFrame(1u, 0.0f);
    render(dsp, oneFrame, oneFrame, outputLeft, outputRight);
    ok = ok && dsp.recordedFrames(0u) == 0u;

    // Loop 1 records twice the intended loop duration, matching CRCLTR v1.0.1.
    std::vector<float> recordLeft(400u);
    std::vector<float> recordRight(400u);
    for (uint32_t i = 0u; i < recordLeft.size(); ++i) {
        recordLeft[i] = 0.22f * std::sin(0.031f * static_cast<float>(i));
        recordRight[i] = 0.18f * std::cos(0.027f * static_cast<float>(i));
    }
    params.record = true;
    dsp.setParams(params);
    render(dsp, recordLeft, recordRight, outputLeft, outputRight);
    params.record = false;
    params.blend = 1.0f;
    params.crossfadeMode = s3g::CrcltrCrossfadeMode::Manual;
    params.crossfade = 0.0f;
    dsp.setParams(params);
    render(dsp, oneFrame, oneFrame, outputLeft, outputRight);
    ok = ok && dsp.recordedFrames(0u) == 400u;
    ok = ok && dsp.playbackFrames(0u) == 200u;

    std::vector<float> silence(300u, 0.0f);
    render(dsp, silence, silence, outputLeft, outputRight);
    ok = ok && finiteBuffer(outputLeft) && finiteBuffer(outputRight);
    ok = ok && energy(outputLeft) > 0.001f && energy(outputRight) > 0.001f;

    // Loop 2 uses the full recorded duration and the ducked seam player.
    params.recordTarget = s3g::CrcltrRecordTarget::Loop2;
    params.record = true;
    dsp.setParams(params);
    render(dsp, recordLeft, recordRight, outputLeft, outputRight);
    params.record = false;
    params.crossfade = 1.0f;
    dsp.setParams(params);
    render(dsp, oneFrame, oneFrame, outputLeft, outputRight);
    ok = ok && dsp.recordedFrames(1u) == 400u;
    ok = ok && dsp.playbackFrames(1u) == 400u;
    render(dsp, silence, silence, outputLeft, outputRight);
    ok = ok && finiteBuffer(outputLeft) && finiteBuffer(outputRight);
    ok = ok && energy(outputLeft) > 0.0001f && energy(outputRight) > 0.0001f;

    // The exact same core can run against caller-owned Daisy SDRAM buffers.
    const uint32_t loopCapacity = s3g::Crcltr::requiredLoopCapacity(kSampleRate);
    const uint32_t preRollCapacity = s3g::Crcltr::requiredPreRollCapacity(kSampleRate);
    std::vector<float> loop1Left(loopCapacity);
    std::vector<float> loop1Right(loopCapacity);
    std::vector<float> loop2Left(loopCapacity);
    std::vector<float> loop2Right(loopCapacity);
    std::vector<float> pre1Left(preRollCapacity);
    std::vector<float> pre1Right(preRollCapacity);
    std::vector<float> pre2Left(preRollCapacity);
    std::vector<float> pre2Right(preRollCapacity);
    s3g::CrcltrMemory memory {
        loop1Left.data(), loop1Right.data(), loop2Left.data(), loop2Right.data(),
        loopCapacity,
        pre1Left.data(), pre1Right.data(), pre2Left.data(), pre2Right.data(),
        preRollCapacity,
    };
    s3g::Crcltr externalDsp;
    ok = ok && externalDsp.prepare(kSampleRate, kBlockFrames, &memory);
    ok = ok && externalDsp.usingExternalMemory();

    if (!ok) {
        std::cerr << "CRCLTR smoke test failed\n";
        return 1;
    }
    std::cout << "CRCLTR smoke test passed\n";
    return 0;
}
