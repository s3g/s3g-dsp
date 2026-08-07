#include "s3g_drum_character.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>

namespace {

constexpr double kSampleRate = 48000.0;
constexpr uint32_t kFrames = 8192u;

float sourceSample(uint32_t frame)
{
    const float time = static_cast<float>(frame / kSampleRate);
    const float slowEnvelope = 0.28f + 0.72f
        * (0.5f + 0.5f * std::sin(2.0f * s3g::kPi * 3.7f * time));
    return slowEnvelope * (
        0.58f * std::sin(2.0f * s3g::kPi * 173.0f * time)
        + 0.21f * std::sin(2.0f * s3g::kPi * 6839.0f * time));
}

std::vector<float> render(const s3g::DrumCharacterParams& params)
{
    s3g::DrumCharacter character;
    character.prepare(kSampleRate);
    character.setParams(params);
    character.reset();

    std::vector<float> result(kFrames);
    for (uint32_t frame = 0u; frame < kFrames; ++frame) {
        float left = sourceSample(frame);
        float right = -0.31f * sourceSample(frame + 29u);
        character.processFrame(left, right);
        result[frame] = left;
    }
    return result;
}

float differenceRms(const std::vector<float>& a,
    const std::vector<float>& b)
{
    double energy = 0.0;
    for (uint32_t index = 0u; index < a.size(); ++index) {
        const double difference = static_cast<double>(a[index]) - b[index];
        energy += difference * difference;
    }
    return static_cast<float>(std::sqrt(energy / a.size()));
}

bool transparencyProbe()
{
    s3g::DrumCharacter character;
    character.prepare(kSampleRate);
    float maximumError = 0.0f;
    for (uint32_t frame = 0u; frame < kFrames; ++frame) {
        const float expectedLeft = sourceSample(frame);
        const float expectedRight = -0.37f * sourceSample(frame + 113u);
        float left = expectedLeft;
        float right = expectedRight;
        character.processFrame(left, right);
        maximumError = std::max(maximumError,
            std::max(std::abs(left - expectedLeft),
                std::abs(right - expectedRight)));
    }
    if (maximumError > 1.0e-7f) {
        std::cerr << "default character is not transparent: "
                  << maximumError << "\n";
        return false;
    }
    return true;
}

bool parameterSanitationProbe()
{
    s3g::DrumCharacter character;
    character.prepare(std::numeric_limits<double>::quiet_NaN());
    character.setParams({
        std::numeric_limits<float>::quiet_NaN(),
        -12.0f,
        std::numeric_limits<float>::infinity(),
        -4.0f,
        8.0f,
        2.0f,
        -3.0f,
    });
    const auto params = character.params();
    if (params.drive != 0.0f || params.bias != -1.0f
        || params.compression != 0.0f
        || params.sampleRateReduction != 0.0f
        || params.bitDepthReduction != 1.0f
        || params.reconstruction != 1.0f || params.tone != -1.0f) {
        std::cerr << "character parameter sanitation failed\n";
        return false;
    }

    character.reset();
    for (uint32_t frame = 0u; frame < 2048u; ++frame) {
        float left = sourceSample(frame);
        float right = -sourceSample(frame + 7u);
        character.processFrame(left, right);
        if (!std::isfinite(left) || !std::isfinite(right)) {
            std::cerr << "invalid sample rate or sanitized parameters produced NaN\n";
            return false;
        }
    }
    return true;
}

bool safetyProbe()
{
    s3g::DrumCharacter character;
    character.prepare(kSampleRate);
    character.setParams({ 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f });
    character.reset();

    for (uint32_t frame = 0u; frame < 100000u; ++frame) {
        float left = (frame % 17u == 0u)
            ? std::numeric_limits<float>::infinity()
            : ((frame & 1u) ? 1.0e30f : -1.0e30f);
        float right = (frame % 29u == 0u)
            ? std::numeric_limits<float>::quiet_NaN()
            : sourceSample(frame);
        character.processFrame(left, right);
        if (!std::isfinite(left) || !std::isfinite(right)
            || std::abs(left) > 8.0f || std::abs(right) > 8.0f) {
            std::cerr << "character safety bound failed at frame "
                      << frame << "\n";
            return false;
        }
    }
    return true;
}

bool deterministicResetProbe()
{
    s3g::DrumCharacter character;
    character.prepare(kSampleRate);
    character.setParams({ 0.72f, -0.35f, 0.61f, 0.77f,
        0.83f, 0.48f, -0.42f });

    std::array<float, 2048u> first {};
    std::array<float, 2048u> second {};
    for (uint32_t pass = 0u; pass < 2u; ++pass) {
        character.reset();
        auto& output = pass == 0u ? first : second;
        for (uint32_t frame = 0u; frame < output.size(); ++frame) {
            float left = sourceSample(frame);
            float right = sourceSample(frame + 71u);
            character.processFrame(left, right);
            output[frame] = left;
        }
    }
    if (first != second) {
        std::cerr << "character reset is not deterministic\n";
        return false;
    }
    return true;
}

bool stereoIndependenceProbe()
{
    s3g::DrumCharacter character;
    character.prepare(kSampleRate);
    character.setParams({ 0.86f, 0.73f, 0.68f, 0.74f,
        0.79f, 0.66f, 0.58f });

    std::array<float, 4096u> leftRun {};
    character.reset();
    for (uint32_t frame = 0u; frame < leftRun.size(); ++frame) {
        float left = sourceSample(frame);
        float right = 0.0f;
        character.processFrame(left, right);
        leftRun[frame] = left;
        if (right != 0.0f) {
            std::cerr << "left signal leaked into the right character state\n";
            return false;
        }
    }

    character.reset();
    for (uint32_t frame = 0u; frame < leftRun.size(); ++frame) {
        float left = 0.0f;
        float right = sourceSample(frame);
        character.processFrame(left, right);
        if (left != 0.0f || right != leftRun[frame]) {
            std::cerr << "character channels are not exactly independent\n";
            return false;
        }
    }
    return true;
}

bool stageAudibilityProbe()
{
    const auto dry = render({});
    struct Stage {
        const char* name;
        s3g::DrumCharacterParams params;
        float minimumDifference;
    };
    const std::array<Stage, 6u> stages {{
        { "drive", { 0.86f, 0.64f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }, 0.02f },
        { "compression", { 0.0f, 0.0f, 0.9f, 0.0f, 0.0f, 0.0f, 0.0f }, 0.01f },
        { "sample hold", { 0.0f, 0.0f, 0.0f, 0.9f, 0.0f, 0.0f, 0.0f }, 0.02f },
        { "bit quantizer", { 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f }, 0.02f },
        { "reconstruction", { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.88f, 0.0f }, 0.02f },
        { "tone", { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.85f }, 0.02f },
    }};

    for (const auto& stage : stages) {
        const float difference = differenceRms(dry, render(stage.params));
        if (!(difference > stage.minimumDifference)) {
            std::cerr << stage.name << " stage was not audible: "
                      << difference << "\n";
            return false;
        }
    }

    const auto symmetricDrive = render({ 0.8f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f });
    const auto asymmetricDrive = render({ 0.8f, 0.8f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f });
    if (!(differenceRms(symmetricDrive, asymmetricDrive) > 0.005f)) {
        std::cerr << "drive bias did not produce asymmetric coloration\n";
        return false;
    }
    return true;
}

bool smoothingProbe()
{
    s3g::DrumCharacter character;
    character.prepare(kSampleRate);
    float left = 0.37f;
    float right = -0.37f;
    for (uint32_t frame = 0u; frame < 1024u; ++frame) {
        left = 0.37f;
        right = -0.37f;
        character.processFrame(left, right);
    }
    const float before = left;

    character.setParams({ 1.0f, 0.4f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f });
    left = 0.37f;
    right = -0.37f;
    character.processFrame(left, right);
    const float firstChange = std::abs(left - before);
    for (uint32_t frame = 0u; frame < 3000u; ++frame) {
        left = 0.37f;
        right = -0.37f;
        character.processFrame(left, right);
    }
    const float settledChange = std::abs(left - before);
    if (!(firstChange < 0.02f) || !(settledChange > firstChange * 4.0f)) {
        std::cerr << "character parameter transition was not smoothed: "
                  << firstChange << " / " << settledChange << "\n";
        return false;
    }
    return true;
}

bool tailActivityProbe()
{
    s3g::DrumCharacter transparent;
    transparent.prepare(kSampleRate);
    float left = 1.0f;
    float right = -1.0f;
    transparent.processFrame(left, right);
    if (transparent.active()) {
        std::cerr << "transparent character reported a false tail\n";
        return false;
    }

    s3g::DrumCharacter stateful;
    stateful.prepare(kSampleRate);
    stateful.setParams({ 0.0f, 0.0f, 0.0f, 0.7f,
        0.0f, 0.9f, -0.6f });
    stateful.reset();
    left = 0.8f;
    right = -0.4f;
    stateful.processFrame(left, right);
    if (!stateful.active()) {
        std::cerr << "stateful character did not report its filter tail\n";
        return false;
    }
    for (uint32_t frame = 0u; frame < 48000u && stateful.active(); ++frame) {
        left = 0.0f;
        right = 0.0f;
        stateful.processFrame(left, right);
        if (!std::isfinite(left) || !std::isfinite(right)) return false;
    }
    if (stateful.active()) {
        std::cerr << "character tail did not become inactive\n";
        return false;
    }
    return true;
}

bool dcBlockingProbe()
{
    s3g::DrumCharacter character;
    character.prepare(kSampleRate);
    character.setParams({ 1.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f });
    character.reset();
    double sum = 0.0;
    double signalEnergy = 0.0;
    uint32_t count = 0u;
    for (uint32_t frame = 0u; frame < 96000u; ++frame) {
        const float input = 0.55f * std::sin(2.0f * s3g::kPi * 53.0f
            * static_cast<float>(frame) / static_cast<float>(kSampleRate));
        float left = input;
        float right = input;
        character.processFrame(left, right);
        if (frame >= 48000u) {
            sum += left;
            signalEnergy += static_cast<double>(left) * left;
            ++count;
        }
    }
    const double mean = count > 0u ? sum / count : 1.0;
    if (std::abs(mean) > 0.01 || signalEnergy < 1.0) {
        std::cerr << "asymmetric drive DC blocking failed: " << mean << "\n";
        return false;
    }
    return true;
}

} // namespace

int main()
{
    const bool ok = transparencyProbe()
        && parameterSanitationProbe()
        && safetyProbe()
        && deterministicResetProbe()
        && stereoIndependenceProbe()
        && stageAudibilityProbe()
        && smoothingProbe()
        && tailActivityProbe()
        && dcBlockingProbe();
    if (ok) std::cout << "drum character smoke passed\n";
    return ok ? 0 : 1;
}
