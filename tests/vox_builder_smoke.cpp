#include "s3g_vox_builder.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <unordered_set>
#include <vector>

namespace {

[[noreturn]] void fail(const char* message)
{
    std::cerr << "Vox Builder smoke test failed: " << message << '\n';
    std::exit(1);
}

} // namespace

int main()
{
    constexpr int sampleRate = 1000;
    std::vector<float> samples(3000u, 0.0f);
    const auto addTone = [&](size_t start, size_t end, float frequency) {
        for (size_t i = start; i < end; ++i) {
            samples[i] = 0.5f * std::sin(2.0f * 3.14159265358979323846f
                * frequency * static_cast<float>(i) / static_cast<float>(sampleRate));
        }
    };
    addTone(180u, 720u, 90.0f);
    addTone(1110u, 1680u, 120.0f);
    addTone(2110u, 2740u, 150.0f);

    const auto aliases = s3g::voxBuilderParseAliases("a, ke\nso");
    if (aliases.size() != 3u || aliases[1] != "ke") fail("alias parsing");
    const auto segments = s3g::voxBuilderDetectSegments(
        samples, sampleRate, aliases, { -30.0f, 80.0f, 20.0f });
    if (segments.size() != 3u) fail("segment count");
    if (!(segments[0].startSample < 300u && segments[0].endSample < segments[1].startSample + 1u)) {
        fail("first region");
    }
    if (!(segments[1].startSample > 700u && segments[2].startSample > 1700u)) {
        fail("gap boundaries");
    }

    const std::vector<float> crossing { 0.8f, 0.4f, 0.2f, -0.1f, -0.6f, -0.9f };
    if (s3g::voxBuilderSnapToZeroCrossing(crossing, 2u, 2u, 1u, 5u) != 3u) {
        fail("zero crossing snap");
    }

    auto edited = segments;
    const uint64_t moved = edited[0].endSample + 40u;
    if (!s3g::voxBuilderMoveBoundary(edited, 1u, moved, 10u)) fail("boundary move");
    if (edited[0].endSample != moved || edited[1].startSample != moved) fail("boundary continuity");
    if (s3g::voxBuilderSafeStem("  a / sharp  ") != "a_sharp") fail("safe filename");
    if (segments[0].fixedMs >= segments[1].fixedMs) fail("vowel timing suggestion");

    std::vector<float> quiet(1000u, 0.0f);
    for (size_t i = 0u; i < quiet.size(); ++i) {
        quiet[i] = 0.01f * std::sin(2.0f * 3.14159265358979323846f
            * 100.0f * static_cast<float>(i) / static_cast<float>(sampleRate)) + 0.002f;
    }
    const auto level = s3g::voxBuilderConditionAudio(quiet, sampleRate);
    if (!level.usable || level.normalizationGainDb < 10.0f || level.outputPeakDb > -2.9f) {
        fail("audio conditioning");
    }
    const std::vector<std::string> inventory { "a", "ka", "su" };
    if (s3g::voxBuilderAliasMatchIndex("03_ka_C4", inventory) != 1) {
        fail("filename alias match");
    }
    const auto& phraseAliases = s3g::voxBuilderAutoPhraseAliases();
    if (phraseAliases.size() != 92u
        || s3g::voxBuilderAliasMatchIndex("cha", phraseAliases) < 0
        || s3g::voxBuilderAliasMatchIndex("ya", phraseAliases) < 0) {
        fail("automatic phrase vocabulary");
    }
    s3g::VoxBuilderAcousticFeatures acoustic;
    acoustic.formant1Hz = 1050.0f;
    acoustic.formant2Hz = 1400.0f;
    acoustic.vowelConfidence = 0.9f;
    acoustic.onsetRmsRatio = 0.67f;
    acoustic.onsetPeriodicity = 0.05f;
    acoustic.onsetHighness = 0.82f;
    acoustic.onsetTransient = 0.22f;
    const auto guess = s3g::voxBuilderGuessCoreAlias(acoustic);
    if (guess.alias != "sa" || guess.confidence < 0.5f) fail("acoustic core alias guess");
    const auto ranked = s3g::voxBuilderRankCoreAliases(acoustic);
    if (ranked.size() != 35u || ranked.front().alias != "sa") {
        fail("acoustic alias ranking");
    }
    std::unordered_set<std::string> rankedAliases;
    float previousCost = -1.0f;
    for (const auto& candidate : ranked) {
        if (!rankedAliases.insert(candidate.alias).second || candidate.matchCost < previousCost) {
            fail("acoustic alias ranking order");
        }
        previousCost = candidate.matchCost;
    }
    const std::vector<std::vector<s3g::VoxBuilderAliasGuess>> duplicateChoices {
        { { "sa", 0.90f, 0.10f }, { "se", 0.45f, 1.00f } },
        { { "sa", 0.80f, 0.12f }, { "si", 0.20f, 2.00f } },
    };
    const auto uniqueChoices = s3g::voxBuilderChooseUniqueAliases(
        duplicateChoices, { "se" });
    if (uniqueChoices.size() != 2u || uniqueChoices[0].alias != "sa"
        || uniqueChoices[1].alias != "si") {
        fail("one-use acoustic aliases");
    }
    if (std::string(s3g::voxBuilderAliasAssignmentName(
        s3g::VoxBuilderAliasAssignment::Acoustic)) != "acoustic") {
        fail("alias assignment name");
    }

    std::cout << "Vox Builder smoke test passed\n";
    return 0;
}
