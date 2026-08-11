#include "s3g_feedback_shift.h"
#include "s3g_feedback_shift_presets.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>

namespace {

bool check(bool condition, const char* message)
{
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}

s3g::FeedbackShiftParams stableFeedbackShiftParams()
{
    auto params = s3g::defaultFeedbackShiftParams();
    // Legacy component probes isolate their named subsystem. The dedicated
    // splice tests below exercise the new virtual-patch layer explicitly.
    params.spliceAmount = 0.0f;
    return params;
}

struct ExternalShiftSpectrum {
    double inputTone = 0.0;
    double lowerSideband = 0.0;
    double upperSideband = 0.0;
};

ExternalShiftSpectrum measureExternalShift(float shiftHz)
{
    constexpr double sampleRate = 48000.0;
    constexpr double inputHz = 440.0;
    constexpr uint32_t settleFrames = 8192u;
    constexpr uint32_t measurementFrames = 48000u;
    constexpr double twoPi = 6.28318530717958647692;

    auto params = stableFeedbackShiftParams();
    params.matrix.fill(0.0f);
    params.excite = 0.0f;
    params.drift = 0.0f;
    params.auxMix = 0.0f;
    params.auxGrainMix = 0.0f;
    params.outputGainDb = 0.0f;
    for (auto& node : params.nodes) {
        node.exciterSource = s3g::FeedbackExciterSource::Off;
        node.regeneration = 0.0f;
        node.levelDb = -60.0f;
        node.pedal = s3g::FeedbackPedalType::Bypass;
        node.color = 0.0f;
    }
    params.nodes[0u].exciterSource = s3g::FeedbackExciterSource::External;
    params.nodes[0u].regeneration = 0.0f;
    params.nodes[0u].levelDb = 0.0f;
    params.nodes[0u].frequencyHz = shiftHz;

    s3g::FeedbackShift processor;
    processor.setParams(params);
    processor.prepare(sampleRate);
    std::array<float, s3g::kFeedbackShiftChannels> input {};
    std::array<float, s3g::kFeedbackShiftChannels> output {};
    double inputReal = 0.0;
    double inputImag = 0.0;
    double lowerReal = 0.0;
    double lowerImag = 0.0;
    double upperReal = 0.0;
    double upperImag = 0.0;
    for (uint32_t frame = 0u;
         frame < settleFrames + measurementFrames; ++frame) {
        input.fill(0.0f);
        input[0u] = 0.25f * std::sin(static_cast<float>(
            twoPi * inputHz * static_cast<double>(frame) / sampleRate));
        processor.processFrame(input.data(), output.data());
        if (frame < settleFrames) continue;
        const double time = static_cast<double>(frame) / sampleRate;
        const double sample = output[0u];
        const auto project = [&](double frequency, double& real,
                                 double& imaginary) {
            const double phase = twoPi * frequency * time;
            real += sample * std::cos(phase);
            imaginary -= sample * std::sin(phase);
        };
        project(inputHz, inputReal, inputImag);
        project(std::abs(inputHz - std::abs(shiftHz)), lowerReal, lowerImag);
        project(inputHz + std::abs(shiftHz), upperReal, upperImag);
    }
    const auto magnitude = [](double real, double imaginary) {
        return std::sqrt(real * real + imaginary * imaginary);
    };
    return {
        magnitude(inputReal, inputImag),
        magnitude(lowerReal, lowerImag),
        magnitude(upperReal, upperImag),
    };
}

} // namespace

int main()
{
    bool ok = true;
    ok &= check(std::abs(s3g::feedbackShiftFrequencyFromControlNorm(0.5f))
            < 1.0e-7f,
        "frequency taper did not center exactly at zero");
    ok &= check(std::abs(s3g::feedbackShiftFrequencyFromControlNorm(0.55f)
            - 0.111111f) < 0.0001f
        && std::abs(s3g::feedbackShiftFrequencyFromControlNorm(0.65f)
            - 1.0f) < 0.0001f,
        "frequency taper did not preserve fine control around zero");
    ok &= check(std::abs(s3g::feedbackShiftFrequencyFromControlNorm(0.0f)
            + 6000.0f) < 0.01f
        && std::abs(s3g::feedbackShiftFrequencyFromControlNorm(1.0f)
            - 6000.0f) < 0.01f,
        "frequency taper did not reach both edges");
    ok &= check(std::abs(s3g::feedbackSpliceRateHz(0.0f)
                - 1.0f / 60.0f) < 0.00001f
            && std::abs(s3g::feedbackSpliceRateHz(0.25f) - 1.5f) < 0.001f
            && std::abs(s3g::feedbackSpliceRateHz(1.0f) - 384.0f) < 0.01f,
        "splice rate did not span minute-scale cuts through fragment density");
    ok &= check(std::abs(s3g::feedbackSpliceRateHz(0.25f, -1.0f)
                - 0.75f) < 0.001f
            && std::abs(s3g::feedbackSpliceRateHz(0.25f, 1.0f)
                - 3.0f) < 0.001f,
        "splice fine rate did not provide a centered one-octave trim");
    ok &= check(s3g::feedbackPedalControlCount(
            s3g::FeedbackPedalType::Fracture) == 7u
            && std::strcmp(s3g::feedbackPedalMenuItem(
                s3g::FeedbackPedalType::Fracture, 0u, 9u),
                "OCT STACK") == 0,
        "Fracture insert did not expose its ten-processor family");
    ok &= check(s3g::kFeedbackPedalTypeCount == 23u
            && s3g::kFeedbackInsertCategoryCount == 10u
            && std::strcmp(s3g::feedbackPedalCategoryName(
                s3g::FeedbackPedalType::Filter), "SPECTRAL") == 0
            && std::strcmp(s3g::feedbackPedalCategoryName(
                s3g::FeedbackPedalType::BreakBus), "DYNAMICS") == 0
            && std::strcmp(s3g::feedbackPedalCategoryName(
                s3g::FeedbackPedalType::MacroShred), "SHRED") == 0
            && std::strcmp(s3g::feedbackPedalCategoryName(
                s3g::FeedbackPedalType::MacroPitch), "PITCH") == 0
            && std::strcmp(s3g::feedbackPedalCategoryName(
                s3g::FeedbackPedalType::Repeater), "TIME") == 0
            && std::strcmp(s3g::feedbackPedalCategoryName(
                s3g::FeedbackPedalType::Rotor), "MODULATION") == 0,
        "insert family order or category map was not canonical");
    ok &= check(s3g::feedbackInsertEffectCount(
                s3g::FeedbackInsertCategory::Drive) == 4u
            && s3g::feedbackInsertEffectCount(
                s3g::FeedbackInsertCategory::Fracture)
                == s3g::kFractureProcessorCount
            && s3g::feedbackInsertEffectPedal(
                s3g::FeedbackInsertCategory::Time, 1u)
                == s3g::FeedbackPedalType::TimeMangler
            && s3g::feedbackInsertEffectPedal(
                s3g::FeedbackInsertCategory::Time, 3u)
                == s3g::FeedbackPedalType::MacroDelay
            && s3g::feedbackInsertEffectCount(
                s3g::FeedbackInsertCategory::Shred)
                == s3g::kMacroShredCircuitCount
            && std::strcmp(s3g::feedbackInsertEffectName(
                s3g::FeedbackInsertCategory::Shred, 5u), "FUZZ I") == 0
            && s3g::feedbackInsertEffectPedal(
                s3g::FeedbackInsertCategory::Modulation, 1u)
                == s3g::FeedbackPedalType::Chorus
            && std::strcmp(s3g::feedbackInsertEffectName(
                s3g::FeedbackInsertCategory::Fracture, 9u),
                "OCT STACK") == 0
            && s3g::feedbackInsertEffectIndex(
                s3g::FeedbackPedalType::Fracture, 2u) == 2u
            && s3g::feedbackInsertEffectIndex(
                s3g::FeedbackPedalType::MacroShred, 0u, 6u) == 6u,
        "two-level category/effect hierarchy did not flatten processor families");
    ok &= check(s3g::feedbackPedalControlCount(
                s3g::FeedbackPedalType::MacroShred) == 8u
            && s3g::feedbackPedalControlCount(
                s3g::FeedbackPedalType::MacroPitch) == 5u
            && s3g::feedbackPedalControlCount(
                s3g::FeedbackPedalType::MacroDelay) == 7u
            && s3g::feedbackPedalControlInfo(
                s3g::FeedbackPedalType::MacroShred, 2u).storageSlot == 4u
            && s3g::feedbackPedalControlInfo(
                s3g::FeedbackPedalType::MacroPitch, 0u).storageSlot == 2u
            && s3g::feedbackPedalControlInfo(
                s3g::FeedbackPedalType::MacroDelay, 4u).storageSlot == 6u,
        "new insert families did not expose their contextual controls");
    for (float frequency : { -6000.0f, -100.0f, -1.0f, -0.5f, -0.1f,
             -0.01f, 0.0f, 0.01f, 0.1f, 0.5f, 1.0f, 100.0f, 6000.0f }) {
        const float restored = s3g::feedbackShiftFrequencyFromControlNorm(
            s3g::feedbackShiftFrequencyControlNorm(frequency));
        ok &= check(std::abs(restored - frequency)
                < std::max(0.001f, std::abs(frequency) * 0.0001f),
            "frequency taper inverse was not stable");
    }

    auto spliceLowParams = stableFeedbackShiftParams();
    spliceLowParams.spliceAmount = 1.0f;
    spliceLowParams.spliceRate = 0.0f;
    spliceLowParams.spliceContrast = 0.9f;
    spliceLowParams.spliceSpace = 0.0f;
    auto spliceHighParams = spliceLowParams;
    spliceHighParams.spliceRate = 1.0f;
    spliceHighParams.spliceSpace = 0.78f;
    s3g::FeedbackShift spliceLow;
    s3g::FeedbackShift spliceHigh;
    spliceLow.setParams(spliceLowParams);
    spliceHigh.setParams(spliceHighParams);
    spliceLow.prepare(48000.0);
    spliceHigh.prepare(48000.0);
    std::array<float, s3g::kFeedbackShiftChannels> spliceInput {};
    std::array<float, s3g::kFeedbackShiftChannels> spliceLowOutput {};
    std::array<float, s3g::kFeedbackShiftChannels> spliceHighOutput {};
    bool partialSpliceGap = false;
    bool finiteSpliceOutput = true;
    for (uint32_t frame = 0u; frame < 24000u; ++frame) {
        const float input = 0.16f * std::sin(static_cast<float>(frame)
            * 6.28318530718f * 173.0f / 48000.0f);
        spliceInput.fill(input);
        spliceLow.processFrame(spliceInput.data(), spliceLowOutput.data());
        spliceHigh.processFrame(spliceInput.data(), spliceHighOutput.data());
        uint32_t closed = 0u;
        for (uint32_t lane = 0u; lane < s3g::kFeedbackShiftChannels; ++lane) {
            closed += spliceHigh.spliceLaneGate(lane) < 0.10f ? 1u : 0u;
            finiteSpliceOutput = finiteSpliceOutput
                && std::isfinite(spliceHighOutput[lane]);
        }
        partialSpliceGap = partialSpliceGap || (closed > 0u && closed < 8u);
    }
    ok &= check(spliceHigh.spliceEventCount()
            > spliceLow.spliceEventCount() * 12u
            && spliceHigh.spliceEventCount() > 400u,
        "splice density did not escape the NIM/mid-tempo event range");
    ok &= check(partialSpliceGap,
        "splice SPACE did not create asynchronous lane holes");
    ok &= check(finiteSpliceOutput,
        "high-density virtual patch splicing produced a non-finite sample");

    auto subParams = stableFeedbackShiftParams();
    subParams.matrix.fill(0.0f);
    subParams.excite = 0.0f;
    subParams.outputGainDb = -12.0f;
    subParams.subBassTune = 0.30f;
    subParams.subBassShape = 0.42f;
    subParams.subBassDrive = 0.55f;
    subParams.subBassDecay = 0.42f;
    subParams.subBassSustain = 0.0f;
    for (auto& node : subParams.nodes) {
        node.exciterSource = s3g::FeedbackExciterSource::Off;
        node.regeneration = 0.0f;
        node.levelDb = -60.0f;
    }
    subParams.nodes[0u].exciterSource
        = s3g::FeedbackExciterSource::SubBass;
    subParams.nodes[0u].regeneration = 0.72f;
    subParams.nodes[0u].frequencyHz = 0.0f;
    subParams.nodes[0u].levelDb = 0.0f;
    s3g::FeedbackShift subProbe;
    subProbe.setParams(subParams);
    subProbe.prepare(48000.0);
    subProbe.strike(0u, 1.0f);
    std::array<float, s3g::kFeedbackShiftChannels> subOutput {};
    double subEarlyEnergy = 0.0;
    double subLateEnergy = 0.0;
    for (uint32_t frame = 0u; frame < 48000u; ++frame) {
        subProbe.processFrame(subOutput.data());
        const double energy = static_cast<double>(subOutput[0u])
            * subOutput[0u];
        if (frame < 6000u) subEarlyEnergy += energy;
        if (frame >= 42000u) subLateEnergy += energy;
    }
    ok &= check(subEarlyEnergy > 1.0e-4
            && subLateEnergy < subEarlyEnergy * 0.08,
        "SUB BASS source did not produce a decaying low-frequency excitation");

    s3g::FeedbackShift clockProbe;
    auto clockParams = clockProbe.params();
    clockParams.morphSource = s3g::FeedbackMorphSource::Pulse;
    clockParams.morphSync = 1u;
    clockParams.morphDivision = 4u;
    clockParams.morphShape = s3g::FeedbackPulseShape::Sine;
    clockProbe.setParams(clockParams);
    clockProbe.prepare(48000.0);
    clockProbe.setTransport(123.0, true);
    std::array<float, s3g::kFeedbackShiftChannels> clockOutput {};
    for (uint32_t frame = 0u; frame < 48000u; ++frame) {
        clockProbe.processFrame(clockOutput.data());
    }
    ok &= check(std::abs(clockProbe.morphPhase() - 0.05f) < 0.001f,
        "host-synchronized morph pulse did not follow tempo");

    ok &= check(std::string(s3g::feedbackMorphSourceName(
            s3g::FeedbackMorphSource::Envelope)) == "ECOLOGY ENV"
        && std::string(s3g::feedbackMorphSourceName(
            s3g::FeedbackMorphSource::Edge)) == "ECOLOGY EDGE",
        "ecology-responsive morph drivers were not exposed");

    auto edgeParams = stableFeedbackShiftParams();
    edgeParams.matrix.fill(0.0f);
    edgeParams.excite = 0.0f;
    edgeParams.auxMix = 0.0f;
    edgeParams.auxGrainMix = 0.0f;
    edgeParams.morph = 0.0f;
    edgeParams.morphSource = s3g::FeedbackMorphSource::Edge;
    edgeParams.morphDepth = 1.0f;
    edgeParams.morphInertia = 0.0f;
    edgeParams.governorReflex = 0.0f;
    edgeParams.governorSensitivity = 0.72f;
    for (auto& node : edgeParams.nodes) {
        node.exciterSource = s3g::FeedbackExciterSource::Off;
        node.regeneration = 0.0f;
        node.levelDb = -60.0f;
        node.pedal = s3g::FeedbackPedalType::Bypass;
    }
    edgeParams.nodes[0u].exciterSource
        = s3g::FeedbackExciterSource::External;
    edgeParams.nodes[0u].levelDb = 0.0f;
    edgeParams.nodes[0u].frequencyHz = 0.0f;
    s3g::FeedbackShift edgeProbe;
    edgeProbe.setParams(edgeParams);
    edgeProbe.prepare(48000.0);
    std::array<float, s3g::kFeedbackShiftChannels> edgeInput {};
    std::array<float, s3g::kFeedbackShiftChannels> edgeOutput {};
    for (uint32_t frame = 0u; frame < 2048u; ++frame) {
        edgeProbe.processFrame(edgeInput.data(), edgeOutput.data());
    }
    float peakEdge = 0.0f;
    float peakEdgeMorph = 0.0f;
    for (uint32_t frame = 0u; frame < 36000u; ++frame) {
        edgeInput[0u] = 0.72f * std::sin(static_cast<float>(frame)
            * 2.0f * 3.14159265358979323846f * 181.0f / 48000.0f);
        edgeProbe.processFrame(edgeInput.data(), edgeOutput.data());
        peakEdge = std::max(peakEdge, edgeProbe.ecologyEdge());
        peakEdgeMorph = std::max(peakEdgeMorph, edgeProbe.morphValue());
    }
    const float settledEdge = edgeProbe.ecologyEdge();
    ok &= check(peakEdge > 0.12f && peakEdgeMorph > 0.08f
            && settledEdge < peakEdge * 0.55f,
        "ECOLOGY EDGE did not articulate the onset separately from level");

    auto reflexParams = edgeParams;
    reflexParams.morphSource = s3g::FeedbackMorphSource::Manual;
    reflexParams.governorSensitivity = 1.0f;
    reflexParams.governorReflex = 0.0f;
    s3g::FeedbackShift staticReflex;
    s3g::FeedbackShift movingReflex;
    staticReflex.setParams(reflexParams);
    reflexParams.governorReflex = 1.0f;
    movingReflex.setParams(reflexParams);
    staticReflex.prepare(48000.0);
    movingReflex.prepare(48000.0);
    double reflexDifference = 0.0;
    for (uint32_t frame = 0u; frame < 24000u; ++frame) {
        edgeInput[0u] = 0.72f * std::sin(static_cast<float>(frame)
            * 2.0f * 3.14159265358979323846f * 181.0f / 48000.0f);
        staticReflex.processFrame(edgeInput.data(), edgeOutput.data());
        const float staticSample = edgeOutput[0u];
        movingReflex.processFrame(edgeInput.data(), edgeOutput.data());
        reflexDifference += std::abs(static_cast<double>(
            staticSample - edgeOutput[0u]));
    }
    ok &= check(reflexDifference > 10.0,
        "REFLEX did not move a sustained ecology away from its static mode");

    auto noRestParams = edgeParams;
    noRestParams.morphSource = s3g::FeedbackMorphSource::Manual;
    noRestParams.governorReflex = 0.0f;
    noRestParams.governorSensitivity = 1.0f;
    noRestParams.governorRecovery = 0.18f;
    noRestParams.governorRest = 0.0f;
    auto restParams = noRestParams;
    restParams.governorRest = 1.0f;
    s3g::FeedbackShift noRestProbe;
    s3g::FeedbackShift restProbe;
    noRestProbe.setParams(noRestParams);
    restProbe.setParams(restParams);
    noRestProbe.prepare(48000.0);
    restProbe.prepare(48000.0);
    uint32_t noRestSilentFrames = 0u;
    uint32_t restSilentFrames = 0u;
    float maximumRestActivity = 0.0f;
    std::array<float, s3g::kFeedbackShiftChannels> noRestOutput {};
    for (uint32_t frame = 0u; frame < 240000u; ++frame) {
        edgeInput[0u] = 0.68f * std::sin(static_cast<float>(frame)
            * 2.0f * 3.14159265358979323846f * 181.0f / 48000.0f);
        noRestProbe.processFrame(edgeInput.data(), noRestOutput.data());
        restProbe.processFrame(edgeInput.data(), edgeOutput.data());
        noRestSilentFrames += std::abs(noRestOutput[0u]) < 1.0e-5f
            ? 1u : 0u;
        restSilentFrames += std::abs(edgeOutput[0u]) < 1.0e-5f
            ? 1u : 0u;
        maximumRestActivity = std::max(maximumRestActivity,
            restProbe.restActivity());
    }
    ok &= check(maximumRestActivity > 0.95f
            && restSilentFrames > noRestSilentFrames + 24000u,
        "REST did not create genuine irregular recovery silence");

    auto laneRestParams = noRestParams;
    laneRestParams.governorRest = 1.0f;
    for (uint32_t lane = 0u; lane < 4u; ++lane) {
        laneRestParams.nodes[lane].exciterSource
            = s3g::FeedbackExciterSource::External;
        laneRestParams.nodes[lane].frequencyHz = 0.0f;
        laneRestParams.nodes[lane].regeneration = 0.0f;
        laneRestParams.nodes[lane].levelDb = 0.0f;
    }
    s3g::FeedbackShift staggeredRest;
    staggeredRest.setParams(laneRestParams);
    staggeredRest.prepare(48000.0);
    std::array<uint32_t, 4u> firstLaneRest {{
        std::numeric_limits<uint32_t>::max(),
        std::numeric_limits<uint32_t>::max(),
        std::numeric_limits<uint32_t>::max(),
        std::numeric_limits<uint32_t>::max(),
    }};
    bool sawPartialLaneRest = false;
    for (uint32_t frame = 0u; frame < 360000u; ++frame) {
        edgeInput.fill(0.0f);
        for (uint32_t lane = 0u; lane < 4u; ++lane) {
            edgeInput[lane] = 0.68f * std::sin(static_cast<float>(frame)
                * 2.0f * 3.14159265358979323846f
                * (151.0f + 37.0f * static_cast<float>(lane)) / 48000.0f);
        }
        staggeredRest.processFrame(edgeInput.data(), edgeOutput.data());
        uint32_t activeLaneRests = 0u;
        for (uint32_t lane = 0u; lane < 4u; ++lane) {
            if (staggeredRest.restLaneActivity(lane) > 0.5f) {
                ++activeLaneRests;
                if (firstLaneRest[lane]
                        == std::numeric_limits<uint32_t>::max()) {
                    firstLaneRest[lane] = frame;
                }
            }
        }
        sawPartialLaneRest = sawPartialLaneRest
            || (activeLaneRests > 0u && activeLaneRests < 4u);
    }
    const auto firstRestRange = std::minmax_element(
        firstLaneRest.begin(), firstLaneRest.end());
    const bool everyLaneRested = std::all_of(firstLaneRest.begin(),
        firstLaneRest.end(), [](uint32_t frame) {
            return frame != std::numeric_limits<uint32_t>::max();
        });
    ok &= check(everyLaneRested && sawPartialLaneRest
            && *firstRestRange.second > *firstRestRange.first + 2400u,
        "ecology REST did not stagger independent lane recoveries");

    auto sourceParams = stableFeedbackShiftParams();
    sourceParams.matrix.fill(0.0f);
    sourceParams.excite = 0.0f;
    sourceParams.auxGrainMix = 0.0f;
    sourceParams.outputGainDb = -6.0f;
    for (auto& node : sourceParams.nodes) {
        node.exciterSource = s3g::FeedbackExciterSource::Off;
        node.regeneration = 0.0f;
        node.levelDb = -60.0f;
        node.pedal = s3g::FeedbackPedalType::Bypass;
    }
    sourceParams.nodes[0u].exciterSource
        = s3g::FeedbackExciterSource::External;
    sourceParams.nodes[0u].regeneration = 1.0f;
    sourceParams.nodes[0u].levelDb = 0.0f;
    sourceParams.nodes[0u].frequencyHz = 0.0f;
    s3g::FeedbackShift externalSource;
    externalSource.setParams(sourceParams);
    externalSource.prepare(48000.0);
    std::array<float, s3g::kFeedbackShiftChannels> sourceInput {};
    std::array<float, s3g::kFeedbackShiftChannels> sourceOutput {};
    std::array<double, s3g::kFeedbackShiftChannels> sourceEnergy {};
    for (uint32_t frame = 0u; frame < 8192u; ++frame) {
        sourceInput.fill(0.0f);
        sourceInput[0u] = std::sin(static_cast<float>(frame)
            * 2.0f * 3.14159265358979323846f * 173.0f / 48000.0f) * 0.25f;
        externalSource.processFrame(sourceInput.data(), sourceOutput.data());
        for (uint32_t channel = 0u;
             channel < s3g::kFeedbackShiftChannels; ++channel) {
            sourceEnergy[channel] += static_cast<double>(sourceOutput[channel])
                * sourceOutput[channel];
        }
    }
    double sourceLeak = 0.0;
    for (uint32_t channel = 1u;
         channel < s3g::kFeedbackShiftChannels; ++channel) {
        sourceLeak += sourceEnergy[channel];
    }
    ok &= check(sourceEnergy[0u] > 1.0e-5 && sourceLeak < 1.0e-18,
        "discrete external excitation did not remain on its assigned node");

    const auto zeroShiftSpectrum = measureExternalShift(0.0f);
    const auto wideShiftSpectrum = measureExternalShift(720.0f);
    ok &= check(zeroShiftSpectrum.inputTone > 1000.0,
        "zero-Hz external source did not preserve its input-frequency energy");
    ok &= check(std::max(wideShiftSpectrum.lowerSideband,
                    wideShiftSpectrum.upperSideband)
            > wideShiftSpectrum.inputTone * 20.0,
        "nonzero external SHIFT behaved like an unprocessed dry path");

    auto toneParams = sourceParams;
    toneParams.nodes[0u].exciterSource = s3g::FeedbackExciterSource::Tone;
    toneParams.nodes[0u].frequencyHz = 110.0f;
    toneParams.excite = 0.75f;
    s3g::FeedbackShift staticTone;
    s3g::FeedbackShift movingTone;
    staticTone.setParams(toneParams);
    toneParams.sceneBNodes[0u].frequencyHz = 880.0f;
    toneParams.sceneBNodes[0u].levelDb = 0.0f;
    toneParams.morphSource = s3g::FeedbackMorphSource::Lfo;
    toneParams.morphRate = 0.76f;
    toneParams.morphDepth = 1.0f;
    toneParams.morphInertia = 0.0f;
    movingTone.setParams(toneParams);
    staticTone.prepare(48000.0);
    movingTone.prepare(48000.0);
    double morphDifference = 0.0;
    float morphPeak = 0.0f;
    std::array<float, s3g::kFeedbackShiftChannels> staticOutput {};
    std::array<float, s3g::kFeedbackShiftChannels> movingOutput {};
    for (uint32_t frame = 0u; frame < 24000u; ++frame) {
        staticTone.processFrame(staticOutput.data());
        movingTone.processFrame(movingOutput.data());
        morphDifference += std::abs(static_cast<double>(
            staticOutput[0u] - movingOutput[0u]));
        morphPeak = std::max(morphPeak,
            movingTone.morphValue());
    }
    ok &= check(morphDifference > 0.01 && morphPeak > 0.10f,
        "LFO ecology morph did not reach the Scene B frequency");

    s3g::FeedbackShift synth;
    synth.prepare(48000.0);
    synth.setTransport(120.0, true);
    std::array<float, s3g::kFeedbackShiftChannels> output {};
    std::array<float, s3g::kFeedbackShiftChannels> peaks {};
    for (uint32_t frame = 0u; frame < 96000u; ++frame) {
        synth.processFrame(output.data());
        for (uint32_t channel = 0u;
             channel < s3g::kFeedbackShiftChannels; ++channel) {
            ok &= check(std::isfinite(output[channel])
                    && std::abs(output[channel]) <= 1.0f,
                "default matrix produced invalid or unbounded audio");
            peaks[channel] = std::max(peaks[channel],
                std::abs(output[channel]));
        }
    }
    for (float peak : peaks) {
        ok &= check(peak > 1.0e-7f,
            "one or more feedback matrix outputs were silent");
    }

    struct ShiftInstabilityProbe {
        double earlyEnergy = 0.0;
        double tailEnergy = 0.0;
        float minimumGovernor = 1.0f;
    };
    const auto probeShiftInstability = [&](float color) {
        s3g::FeedbackShift probe;
        auto params = stableFeedbackShiftParams();
        params.matrix.fill(0.0f);
        params.excite = 0.0f;
        params.drift = 0.0f;
        params.outputGainDb = -12.0f;
        for (auto& node : params.nodes) {
            node.frequencyHz = 0.0f;
            node.regeneration = 0.0f;
            node.color = 0.0f;
            node.levelDb = -60.0f;
            node.pedal = s3g::FeedbackPedalType::Bypass;
        }
        params.nodes[0u].frequencyHz = 0.20f;
        params.nodes[0u].regeneration = 1.0f;
        params.nodes[0u].color = color;
        params.nodes[0u].levelDb = 0.0f;
        probe.setParams(params);
        probe.prepare(48000.0);
        probe.strike(0u, 1.0f);
        ShiftInstabilityProbe result;
        for (uint32_t frame = 0u; frame < 48000u; ++frame) {
            probe.processFrame(output.data());
            const double energy = static_cast<double>(output[0u])
                * output[0u];
            (frame < 36000u ? result.earlyEnergy : result.tailEnergy)
                += energy;
            result.minimumGovernor = std::min(result.minimumGovernor,
                probe.minimumGovernor());
        }
        return result;
    };
    const auto cleanShift = probeShiftInstability(0.0f);
    const auto coloredShift = probeShiftInstability(1.0f);
    const bool restoredShiftInstability = coloredShift.tailEnergy
            > cleanShift.tailEnergy * 4.0
        && coloredShift.minimumGovernor < cleanShift.minimumGovernor * 0.92f;
    if (!restoredShiftInstability) {
        std::cerr << "SHIFT instability probe: clean tail="
                  << cleanShift.tailEnergy << ", colored tail="
                  << coloredShift.tailEnergy << ", governors="
                  << cleanShift.minimumGovernor << " / "
                  << coloredShift.minimumGovernor << '\n';
    }
    ok &= check(restoredShiftInstability,
        "near-zero REGEN/COLOR no longer opens the local SHIFT feedback loop");

    const auto verifyFold = [&](s3g::FeedbackShiftOutputMode mode,
                                uint32_t activeChannels) {
        s3g::FeedbackShift folded;
        auto params = stableFeedbackShiftParams();
        params.outputMode = mode;
        params.outputRotationDeg = 41.0f;
        folded.setParams(params);
        folded.prepare(48000.0);
        folded.strikeAll(1.0f);
        std::array<double, s3g::kFeedbackShiftChannels> energy {};
        for (uint32_t frame = 0u; frame < 8192u; ++frame) {
            folded.processFrame(output.data());
            for (uint32_t channel = 0u;
                 channel < s3g::kFeedbackShiftChannels; ++channel) {
                energy[channel] += static_cast<double>(output[channel])
                    * output[channel];
            }
        }
        double active = 0.0;
        double silent = 0.0;
        for (uint32_t channel = 0u;
             channel < s3g::kFeedbackShiftChannels; ++channel) {
            (channel < activeChannels ? active : silent) += energy[channel];
        }
        return active > 1.0e-8 && silent < 1.0e-20;
    };
    ok &= check(verifyFold(s3g::FeedbackShiftOutputMode::QuadRing, 4u),
        "quad ring fold did not silence outputs five through eight");
    ok &= check(verifyFold(s3g::FeedbackShiftOutputMode::StereoRing, 2u),
        "stereo ring fold did not silence outputs three through eight");

    s3g::FeedbackShift ringZero;
    s3g::FeedbackShift ringRotated;
    auto ringParams = stableFeedbackShiftParams();
    ringParams.outputMode = s3g::FeedbackShiftOutputMode::QuadRing;
    ringZero.setParams(ringParams);
    ringParams.outputRotationDeg = 90.0f;
    ringRotated.setParams(ringParams);
    ringZero.prepare(48000.0);
    ringRotated.prepare(48000.0);
    ringZero.strike(0u, 1.0f);
    ringRotated.strike(0u, 1.0f);
    double rotationDifference = 0.0;
    std::array<float, s3g::kFeedbackShiftChannels> zeroOutput {};
    std::array<float, s3g::kFeedbackShiftChannels> rotatedOutput {};
    for (uint32_t frame = 0u; frame < 8192u; ++frame) {
        ringZero.processFrame(zeroOutput.data());
        ringRotated.processFrame(rotatedOutput.data());
        for (uint32_t channel = 0u; channel < 4u; ++channel) {
            rotationDifference += std::abs(
                static_cast<double>(zeroOutput[channel]
                    - rotatedOutput[channel]));
        }
    }
    ok &= check(rotationDifference > 1.0e-5,
        "channel-ring rotation did not move the quad projection");

    ok &= check(s3g::feedbackPedalControlCount(
            s3g::FeedbackPedalType::Repeater) == 9u
        && s3g::feedbackPedalControlCount(
            s3g::FeedbackPedalType::TimeMangler) == 9u
        && s3g::feedbackPedalControlInfo(
            s3g::FeedbackPedalType::Repeater, 7u).storageSlot == 8u
        && std::string(s3g::feedbackPedalControlInfo(
            s3g::FeedbackPedalType::TimeMangler, 5u).label) == "XFADE",
        "temporal ecology control surface is incomplete");

    s3g::FeedbackShift linkedTemporal;
    auto linkedParams = stableFeedbackShiftParams();
    linkedParams.matrix.fill(0.0f);
    linkedParams.excite = 0.0f;
    linkedParams.nodes[0u].pedal = s3g::FeedbackPedalType::Repeater;
    linkedParams.nodes[0u].frequencyHz = 0.0f;
    linkedParams.nodes[0u].regeneration = 1.0f;
    linkedParams.nodes[0u].pedalAmount = 0.0f;
    linkedParams.nodes[0u].pedalTone = 0.0f;
    linkedParams.nodes[0u].pedalMix = 1.0f;
    linkedParams.nodes[0u].pedalExtra[1u] = 1.0f; // sensitivity
    linkedParams.nodes[0u].pedalExtra[2u] = 0.65f; // crossfade
    linkedParams.nodes[0u].pedalExtra[3u] = 0.72f; // capture drift
    linkedParams.nodes[0u].pedalExtra[4u] = 1.0f; // link from node two
    linkedParams.nodes[1u].frequencyHz = 0.0f;
    linkedParams.nodes[1u].regeneration = 1.0f;
    linkedParams.nodes[1u].exciterGainDb = 12.0f;
    linkedTemporal.setParams(linkedParams);
    linkedTemporal.prepare(48000.0);
    linkedTemporal.strike(1u, 1.0f);
    bool linkedCapture = false;
    bool linkedPlayback = false;
    float linkedNodePeak = 0.0f;
    for (uint32_t frame = 0u; frame < 4096u; ++frame) {
        linkedTemporal.processFrame(output.data());
        linkedCapture = linkedCapture
            || linkedTemporal.temporalPhase(0u) == 1u;
        linkedPlayback = linkedPlayback
            || linkedTemporal.temporalPhase(0u) == 2u;
        linkedNodePeak = std::max(linkedNodePeak,
            linkedTemporal.outputPeak(1u));
        for (float sample : output) {
            ok &= check(std::isfinite(sample) && std::abs(sample) <= 1.0f,
                "linked temporal capture produced invalid audio");
        }
    }
    if (!linkedCapture || !linkedPlayback) {
        std::cerr << "linked repeater probe: capture=" << linkedCapture
                  << ", playback=" << linkedPlayback
                  << ", node2 peak=" << linkedNodePeak << '\n';
    }
    ok &= check(linkedCapture && linkedPlayback,
        "adjacent node did not trigger and seed the linked repeater");

    ok &= check(s3g::kFeedbackAuxControlCount == 16u
        && std::string(s3g::feedbackAuxControlInfo(4u).label) == "TILT"
        && std::string(s3g::feedbackAuxControlInfo(5u).label) == "RETURN"
        && std::string(s3g::feedbackAuxControlInfo(10u).label)
            == "COHERENCE"
        && s3g::feedbackAuxControlInfo(10u).storageSlot == 12u
        && std::string(s3g::feedbackAuxControlInfo(11u).label)
            == "LANE DRIFT"
        && s3g::feedbackAuxControlInfo(11u).storageSlot == 13u
        && std::string(s3g::feedbackAuxControlInfo(13u).label)
            == "GRAIN MIX"
        && s3g::feedbackAuxControlInfo(13u).storageSlot == 11u
        && std::string(s3g::feedbackAuxControlInfo(14u).label) == "SPACE"
        && s3g::feedbackAuxControlInfo(14u).storageSlot == 14u
        && std::string(s3g::feedbackAuxControlInfo(15u).label) == "SHAPE"
        && s3g::feedbackAuxControlInfo(15u).menuCount
            == s3g::kFeedbackGrainShapeCount
        && std::string(s3g::feedbackAuxMenuItem(15u, 3u)) == "DECAY",
        "AUX wall/grain control surface is incomplete");

    auto noRouteDryParams = stableFeedbackShiftParams();
    noRouteDryParams.matrix.fill(0.0f);
    noRouteDryParams.excite = 0.0f;
    for (auto& node : noRouteDryParams.nodes) {
        node.frequencyHz = 0.0f;
        node.regeneration = 1.0f;
        node.pedal = s3g::FeedbackPedalType::Bypass;
    }
    noRouteDryParams.auxGrainMix = 0.0f;
    auto noRouteWallParams = noRouteDryParams;
    noRouteWallParams.auxPress = 1.0f;
    noRouteWallParams.auxSaturation = 1.0f;
    noRouteWallParams.auxFold = 1.0f;
    noRouteWallParams.auxClip = 1.0f;
    noRouteWallParams.auxMix = 1.0f;
    auto noRouteGrainParams = noRouteDryParams;
    noRouteGrainParams.auxGrainSize = 0.12f;
    noRouteGrainParams.auxGrainDensity = 1.0f;
    noRouteGrainParams.auxGrainScatter = 1.0f;
    noRouteGrainParams.auxGrainMix = 1.0f;
    s3g::FeedbackShift noRouteDry;
    s3g::FeedbackShift noRouteWall;
    s3g::FeedbackShift noRouteGrain;
    noRouteDry.setParams(noRouteDryParams);
    noRouteWall.setParams(noRouteWallParams);
    noRouteGrain.setParams(noRouteGrainParams);
    noRouteDry.prepare(48000.0);
    noRouteWall.prepare(48000.0);
    noRouteGrain.prepare(48000.0);
    noRouteDry.strikeAll(1.0f);
    noRouteWall.strikeAll(1.0f);
    noRouteGrain.strikeAll(1.0f);
    double noRouteWallDifference = 0.0;
    double noRouteGrainDifference = 0.0;
    std::array<float, s3g::kFeedbackShiftChannels> noRouteDryOutput {};
    std::array<float, s3g::kFeedbackShiftChannels> noRouteWallOutput {};
    std::array<float, s3g::kFeedbackShiftChannels> noRouteGrainOutput {};
    for (uint32_t frame = 0u; frame < 16384u; ++frame) {
        noRouteDry.processFrame(noRouteDryOutput.data());
        noRouteWall.processFrame(noRouteWallOutput.data());
        noRouteGrain.processFrame(noRouteGrainOutput.data());
        for (uint32_t channel = 0u;
             channel < s3g::kFeedbackShiftChannels; ++channel) {
            noRouteWallDifference += std::abs(static_cast<double>(
                noRouteDryOutput[channel] - noRouteWallOutput[channel]));
            noRouteGrainDifference += std::abs(static_cast<double>(
                noRouteDryOutput[channel] - noRouteGrainOutput[channel]));
        }
    }
    ok &= check(noRouteWallDifference < 1.0e-12,
        "feedback AUX wall leaked into the audible dry output");
    ok &= check(noRouteGrainDifference > 0.01,
        "granulator did not move to the post-network output path");

    auto duckDryParams = noRouteDryParams;
    duckDryParams.drift = 0.0f;
    duckDryParams.governorReflex = 0.0f;
    duckDryParams.governorRest = 0.0f;
    duckDryParams.auxGrainSize = 0.10f;
    duckDryParams.auxGrainDensity = 1.0f;
    duckDryParams.auxGrainScatter = 0.0f;
    duckDryParams.auxGrainPitch = 0.0f;
    duckDryParams.auxGrainShape = s3g::FeedbackGrainShape::Hann;
    duckDryParams.auxGrainMix = 0.0f;
    auto duckMixedParams = duckDryParams;
    duckMixedParams.auxGrainMix = 0.5f;
    auto duckWetParams = duckDryParams;
    duckWetParams.auxGrainMix = 1.0f;
    s3g::FeedbackShift grainDry;
    s3g::FeedbackShift grainMixed;
    s3g::FeedbackShift grainWet;
    grainDry.setParams(duckDryParams);
    grainMixed.setParams(duckMixedParams);
    grainWet.setParams(duckWetParams);
    grainDry.prepare(48000.0);
    grainMixed.prepare(48000.0);
    grainWet.prepare(48000.0);
    grainDry.strikeAll(1.0f);
    grainMixed.strikeAll(1.0f);
    grainWet.strikeAll(1.0f);
    double duckDifferenceFromStaticCrossfade = 0.0;
    float maximumSourceDuck = 0.0f;
    std::array<float, s3g::kFeedbackShiftChannels> duckDryOutput {};
    std::array<float, s3g::kFeedbackShiftChannels> duckMixedOutput {};
    std::array<float, s3g::kFeedbackShiftChannels> duckWetOutput {};
    for (uint32_t frame = 0u; frame < 48000u; ++frame) {
        grainDry.processFrame(duckDryOutput.data());
        grainMixed.processFrame(duckMixedOutput.data());
        grainWet.processFrame(duckWetOutput.data());
        maximumSourceDuck = std::max(maximumSourceDuck,
            grainMixed.auxGrainSourceDuck());
        for (uint32_t channel = 0u;
             channel < s3g::kFeedbackShiftChannels; ++channel) {
            const float staticCrossfade = 0.5f
                * (duckDryOutput[channel] + duckWetOutput[channel]);
            duckDifferenceFromStaticCrossfade += std::abs(
                static_cast<double>(duckMixedOutput[channel]
                    - staticCrossfade));
        }
    }
    ok &= check(maximumSourceDuck > 0.45f
            && duckDifferenceFromStaticCrossfade > 0.01,
        "active grains did not duck the source side of an intermediate mix");

    auto dryAuxParams = stableFeedbackShiftParams();
    dryAuxParams.matrix.fill(0.0f);
    dryAuxParams.excite = 0.0f;
    dryAuxParams.outputGainDb = -6.0f;
    for (auto& node : dryAuxParams.nodes) {
        node.frequencyHz = 0.0f;
        node.regeneration = 0.0f;
        node.levelDb = -60.0f;
        node.pedal = s3g::FeedbackPedalType::Bypass;
    }
    dryAuxParams.nodes[0u].regeneration = 0.92f;
    dryAuxParams.nodes[0u].levelDb = 0.0f;
    dryAuxParams.matrix[0u] = 0.82f;
    dryAuxParams.auxGrainMix = 0.0f;
    auto granularParams = dryAuxParams;
    granularParams.auxGrainSize = 0.22f;
    granularParams.auxGrainDensity = 0.72f;
    granularParams.auxGrainScatter = 0.84f;
    granularParams.auxGrainPitch = 0.32f;
    granularParams.auxGrainEdge = 0.72f;
    granularParams.auxGrainCoherence = 1.0f;
    granularParams.auxGrainLaneDrift = 1.0f;
    granularParams.auxGrainMix = 1.0f;
    granularParams.auxMix = 0.0f;
    auto grainDryParams = granularParams;
    grainDryParams.auxGrainMix = 0.0f;
    auto sendMutedParams = granularParams;
    sendMutedParams.auxSend[0u] = 0.0f;
    auto independentParams = granularParams;
    independentParams.auxGrainCoherence = 0.0f;
    s3g::FeedbackShift dryAux;
    s3g::FeedbackShift granularAux;
    s3g::FeedbackShift grainDryAux;
    s3g::FeedbackShift sendMutedAux;
    s3g::FeedbackShift independentAux;
    dryAux.setParams(dryAuxParams);
    granularAux.setParams(granularParams);
    grainDryAux.setParams(grainDryParams);
    sendMutedAux.setParams(sendMutedParams);
    independentAux.setParams(independentParams);
    dryAux.prepare(48000.0);
    granularAux.prepare(48000.0);
    grainDryAux.prepare(48000.0);
    sendMutedAux.prepare(48000.0);
    independentAux.prepare(48000.0);
    dryAux.strike(0u, 1.0f);
    granularAux.strike(0u, 1.0f);
    grainDryAux.strike(0u, 1.0f);
    sendMutedAux.strike(0u, 1.0f);
    independentAux.strike(0u, 1.0f);
    std::array<float, s3g::kFeedbackShiftChannels> dryAuxOutput {};
    std::array<float, s3g::kFeedbackShiftChannels> granularOutput {};
    std::array<float, s3g::kFeedbackShiftChannels> grainDryOutput {};
    std::array<float, s3g::kFeedbackShiftChannels> sendMutedOutput {};
    std::array<float, s3g::kFeedbackShiftChannels> independentOutput {};
    double auxDifference = 0.0;
    double grainMixDifference = 0.0;
    double mutedSendDifference = 0.0;
    double coherenceDifference = 0.0;
    double auxLaneLeak = 0.0;
    bool activeGrainsObserved = false;
    for (uint32_t frame = 0u; frame < 16384u; ++frame) {
        dryAux.processFrame(dryAuxOutput.data());
        granularAux.processFrame(granularOutput.data());
        grainDryAux.processFrame(grainDryOutput.data());
        sendMutedAux.processFrame(sendMutedOutput.data());
        independentAux.processFrame(independentOutput.data());
        auxDifference += std::abs(static_cast<double>(
            dryAuxOutput[0u] - granularOutput[0u]));
        grainMixDifference += std::abs(static_cast<double>(
            grainDryOutput[0u] - granularOutput[0u]));
        mutedSendDifference += std::abs(static_cast<double>(
            granularOutput[0u] - sendMutedOutput[0u]));
        coherenceDifference += std::abs(static_cast<double>(
            granularOutput[0u] - independentOutput[0u]));
        activeGrainsObserved = activeGrainsObserved
            || granularAux.auxGrainActivity() > 0.05f;
        for (uint32_t channel = 1u;
             channel < s3g::kFeedbackShiftChannels; ++channel) {
            auxLaneLeak += static_cast<double>(granularOutput[channel])
                * granularOutput[channel];
        }
        for (float sample : granularOutput) {
            ok &= check(std::isfinite(sample) && std::abs(sample) <= 1.0f,
                "AUX wall/grain chain produced invalid audio");
        }
    }
    ok &= check(auxDifference > 0.01 && activeGrainsObserved,
        "post-network granulator did not transform the signal");
    ok &= check(grainMixDifference > 0.01,
        "GRAIN MIX did not independently blend the granulator");
    ok &= check(mutedSendDifference < 1.0e-12,
        "feedback AUX send unexpectedly changed the post granulator");
    ok &= check(coherenceDifference > 0.01,
        "COHERENCE and LANE DRIFT did not separate lane grain behavior");
    ok &= check(auxLaneLeak < 1.0e-18,
        "post granulator borrowed audio between discrete channel buffers");

    auto sparseBase = stableFeedbackShiftParams();
    sparseBase.matrix.fill(0.0f);
    sparseBase.excite = 0.0f;
    sparseBase.drift = 0.0f;
    sparseBase.outputGainDb = 0.0f;
    for (auto& node : sparseBase.nodes) {
        node.exciterSource = s3g::FeedbackExciterSource::Off;
        node.frequencyHz = 0.0f;
        node.regeneration = 0.0f;
        node.color = 0.0f;
        node.body = 0.0f;
        node.levelDb = -60.0f;
        node.pedal = s3g::FeedbackPedalType::Bypass;
    }
    sparseBase.nodes[0u].exciterSource
        = s3g::FeedbackExciterSource::External;
    sparseBase.nodes[0u].levelDb = 0.0f;
    sparseBase.auxGrainSize = 0.16f;
    sparseBase.auxGrainDensity = 1.0f;
    sparseBase.auxGrainScatter = 0.0f;
    sparseBase.auxGrainPitch = 0.0f;
    sparseBase.auxGrainCoherence = 1.0f;
    sparseBase.auxGrainLaneDrift = 0.0f;
    sparseBase.auxGrainMix = 1.0f;
    sparseBase.auxGrainSpacing = 0.0f;
    sparseBase.auxGrainShape = s3g::FeedbackGrainShape::Hann;
    auto sparseParams = sparseBase;
    sparseParams.auxGrainSpacing = 0.35f;
    auto decayParams = sparseBase;
    decayParams.auxGrainShape = s3g::FeedbackGrainShape::Decay;
    s3g::FeedbackShift denseGrains;
    s3g::FeedbackShift sparseGrains;
    s3g::FeedbackShift decayGrains;
    denseGrains.setParams(sparseBase);
    sparseGrains.setParams(sparseParams);
    decayGrains.setParams(decayParams);
    denseGrains.prepare(48000.0);
    sparseGrains.prepare(48000.0);
    decayGrains.prepare(48000.0);
    std::array<float, s3g::kFeedbackShiftChannels> sparseInput {};
    std::array<float, s3g::kFeedbackShiftChannels> denseOutput {};
    std::array<float, s3g::kFeedbackShiftChannels> sparseOutput {};
    std::array<float, s3g::kFeedbackShiftChannels> decayOutput {};
    uint32_t denseSilentFrames = 0u;
    uint32_t sparseSilentFrames = 0u;
    uint32_t sparseActiveFrames = 0u;
    double grainShapeDifference = 0.0;
    for (uint32_t frame = 0u; frame < 96000u; ++frame) {
        sparseInput.fill(0.0f);
        sparseInput[0u] = 0.25f * std::sin(static_cast<float>(frame)
            * 2.0f * 3.14159265358979323846f * 181.0f / 48000.0f);
        denseGrains.processFrame(sparseInput.data(), denseOutput.data());
        sparseGrains.processFrame(sparseInput.data(), sparseOutput.data());
        decayGrains.processFrame(sparseInput.data(), decayOutput.data());
        if (frame < 24000u) continue;
        denseSilentFrames += std::abs(denseOutput[0u]) < 1.0e-7f ? 1u : 0u;
        sparseSilentFrames += std::abs(sparseOutput[0u]) < 1.0e-7f ? 1u : 0u;
        sparseActiveFrames += std::abs(sparseOutput[0u]) > 1.0e-4f ? 1u : 0u;
        grainShapeDifference += std::abs(static_cast<double>(
            denseOutput[0u] - decayOutput[0u]));
    }
    ok &= check(sparseSilentFrames > denseSilentFrames + 20000u
            && sparseSilentFrames > 36000u
            && sparseActiveFrames > 500u,
        "grain SPACE did not produce genuine audible gaps");
    ok &= check(grainShapeDifference > 0.01,
        "grain SHAPE selection did not change the rendered window");

    auto wallParams = dryAuxParams;
    wallParams.auxPress = 0.82f;
    wallParams.auxSaturation = 0.78f;
    wallParams.auxFold = 0.72f;
    wallParams.auxClip = 0.64f;
    wallParams.auxMix = 1.0f;
    auto wallMutedParams = wallParams;
    wallMutedParams.auxSend[0u] = 0.0f;
    s3g::FeedbackShift wallAux;
    s3g::FeedbackShift wallMutedAux;
    wallAux.setParams(wallParams);
    wallMutedAux.setParams(wallMutedParams);
    wallAux.prepare(48000.0);
    wallMutedAux.prepare(48000.0);
    wallAux.strike(0u, 1.0f);
    wallMutedAux.strike(0u, 1.0f);
    dryAux.panic();
    dryAux.strike(0u, 1.0f);
    double wallDifference = 0.0;
    double wallMutedDifference = 0.0;
    float wallActivityPeak = 0.0f;
    for (uint32_t frame = 0u; frame < 16384u; ++frame) {
        dryAux.processFrame(dryAuxOutput.data());
        wallAux.processFrame(granularOutput.data());
        wallMutedAux.processFrame(sendMutedOutput.data());
        wallDifference += std::abs(static_cast<double>(
            dryAuxOutput[0u] - granularOutput[0u]));
        wallMutedDifference += std::abs(static_cast<double>(
            dryAuxOutput[0u] - sendMutedOutput[0u]));
        wallActivityPeak = std::max(wallActivityPeak, wallAux.auxActivity());
    }
    ok &= check(wallDifference > 0.01 && wallActivityPeak > 1.0e-8f,
        "feedback AUX wall did not recirculate through the matrix source");
    ok &= check(wallMutedDifference < 1.0e-12,
        "zero lane send did not remove that lane from the feedback AUX");

    for (uint32_t preset = 0u;
         preset < s3g::kFeedbackShiftPresetCount; ++preset) {
        const auto presetParams = s3g::feedbackShiftPreset(preset);
        double sceneDistance = 0.0;
        for (uint32_t node = 0u;
             node < s3g::kFeedbackShiftChannels; ++node) {
            sceneDistance += std::abs(
                presetParams.nodes[node].frequencyHz
                - presetParams.sceneBNodes[node].frequencyHz);
            sceneDistance += std::abs(
                presetParams.nodes[node].regeneration
                - presetParams.sceneBNodes[node].regeneration);
            sceneDistance += std::abs(
                presetParams.nodes[node].color
                - presetParams.sceneBNodes[node].color);
            sceneDistance += std::abs(
                presetParams.nodes[node].body
                - presetParams.sceneBNodes[node].body);
            sceneDistance += std::abs(
                presetParams.nodes[node].levelDb
                - presetParams.sceneBNodes[node].levelDb);
        }
        for (uint32_t route = 0u;
             route < presetParams.matrix.size(); ++route) {
            sceneDistance += std::abs(presetParams.matrix[route]
                - presetParams.sceneBMatrix[route]);
        }
        ok &= check(sceneDistance > 0.1
                && static_cast<uint32_t>(presetParams.morphSource)
                    < s3g::kFeedbackMorphSourceCount
                && presetParams.morph >= 0.0f
                && presetParams.morph <= 1.0f
                && presetParams.morphDepth >= 0.0f
                && presetParams.morphDepth <= 1.0f,
            "factory preset did not define a valid paired ecology");
        s3g::FeedbackShift presetSynth;
        presetSynth.setParams(presetParams);
        presetSynth.prepare(48000.0);
        presetSynth.setTransport(120.0, true);
        presetSynth.strikeAll(0.8f);
        double presetEnergy = 0.0;
        std::array<float, s3g::kFeedbackShiftChannels> presetInput {};
        for (uint32_t frame = 0u; frame < 16384u; ++frame) {
            presetInput.fill(0.0f);
            presetInput[0u] = 0.15f * std::sin(static_cast<float>(frame)
                * 2.0f * 3.14159265358979323846f * 97.0f / 48000.0f);
            presetSynth.processFrame(presetInput.data(), output.data());
            for (float sample : output) {
                ok &= check(std::isfinite(sample) && std::abs(sample) <= 1.0f,
                    "built-in preset produced invalid audio");
                presetEnergy += static_cast<double>(sample) * sample;
            }
        }
        ok &= check(presetEnergy > 1.0e-10,
            "factory preset produced no audible energy");
    }
    for (uint32_t pedal = 0u;
         pedal < s3g::kFeedbackPedalTypeCount; ++pedal) {
        auto pedalProbe = stableFeedbackShiftParams();
        pedalProbe.nodes[0u].pedal = static_cast<s3g::FeedbackPedalType>(pedal);
        pedalProbe.nodes[0u].pedalAmount = 0.72f;
        pedalProbe.nodes[0u].pedalTone = 0.63f;
        pedalProbe.nodes[0u].pedalBias = 0.28f;
        pedalProbe.nodes[0u].pedalMix = 1.0f;
        synth.panic();
        synth.setParams(pedalProbe);
        synth.strike(0u, 1.0f);
        for (uint32_t frame = 0u; frame < 4096u; ++frame) {
            synth.processFrame(output.data());
            for (float sample : output) {
                ok &= check(std::isfinite(sample) && std::abs(sample) <= 1.0f,
                    "pedal-bank processor produced invalid audio");
            }
        }
    }
    const auto randomA = s3g::randomFeedbackShiftParams(0x12345678u);
    const auto randomB = s3g::randomFeedbackShiftParams(0x12345678u);
    const auto randomSeed = stableFeedbackShiftParams();
    ok &= check(randomA.sceneBNodes[0u].frequencyHz
            == randomB.sceneBNodes[0u].frequencyHz
        && randomA.sceneBMatrix == randomB.sceneBMatrix
        && randomA.sceneBAuxSend == randomB.sceneBAuxSend
        && randomA.nodes[0u].frequencyHz
            == randomSeed.nodes[0u].frequencyHz
        && randomA.matrix == randomSeed.matrix
        && randomA.auxSend == randomSeed.auxSend
        && randomA.sceneBNodes[0u].regeneration >= 0.0f
        && randomA.sceneBNodes[0u].regeneration <= 1.0f
        && randomA.sceneBNodes[0u].color >= -1.0f
        && randomA.sceneBNodes[0u].color <= 1.0f
        && randomA.sceneBNodes[0u].body >= 0.0f
        && randomA.sceneBNodes[0u].body <= 1.0f
        && static_cast<uint32_t>(randomA.nodes[0u].exciterSource)
            < s3g::kFeedbackExciterSourceCount
        && static_cast<uint32_t>(randomA.morphSource)
            < s3g::kFeedbackMorphSourceCount,
        "Scene B randomization was not reproducible or bounded");
    auto preservedOutput = stableFeedbackShiftParams();
    preservedOutput.outputGainDb = -7.3f;
    preservedOutput.outputMode = s3g::FeedbackShiftOutputMode::StereoRing;
    preservedOutput.outputRotationDeg = 47.0f;
    preservedOutput.morph = 0.37f;
    preservedOutput.governorReflex = 0.81f;
    preservedOutput.governorSensitivity = 0.63f;
    preservedOutput.governorRecovery = 0.74f;
    preservedOutput.governorRest = 0.59f;
    preservedOutput.sceneBNodes[0u].frequencyHz = 1234.0f;
    preservedOutput.sceneBMatrix[0u] = -0.55f;
    preservedOutput.sceneBAuxSend[0u] = 0.91f;
    const auto topologySafeRandom = s3g::randomFeedbackShiftParams(
        0x87654321u, preservedOutput);
    ok &= check(topologySafeRandom.outputGainDb
            == preservedOutput.outputGainDb
        && topologySafeRandom.outputMode == preservedOutput.outputMode
        && topologySafeRandom.outputRotationDeg
            == preservedOutput.outputRotationDeg
        && topologySafeRandom.governorReflex
            == preservedOutput.governorReflex
        && topologySafeRandom.governorSensitivity
            == preservedOutput.governorSensitivity
        && topologySafeRandom.governorRecovery
            == preservedOutput.governorRecovery
        && topologySafeRandom.governorRest
            == preservedOutput.governorRest
        && topologySafeRandom.nodes[0u].frequencyHz
            == preservedOutput.nodes[0u].frequencyHz
        && topologySafeRandom.matrix == preservedOutput.matrix
        && topologySafeRandom.auxSend == preservedOutput.auxSend,
        "RANDOM changed Scene A or user-owned output topology");
    const auto randomizedSceneA = s3g::randomFeedbackShiftScene(
        0x6a09e667u, preservedOutput, false);
    const auto randomizedSceneB = s3g::randomFeedbackShiftScene(
        0xbb67ae85u, preservedOutput, true);
    ok &= check(randomizedSceneA.sceneBNodes[0u].frequencyHz
            == preservedOutput.sceneBNodes[0u].frequencyHz
        && randomizedSceneA.sceneBMatrix == preservedOutput.sceneBMatrix
        && randomizedSceneA.sceneBAuxSend == preservedOutput.sceneBAuxSend
        && randomizedSceneA.nodes[0u].frequencyHz
            != preservedOutput.nodes[0u].frequencyHz
        && randomizedSceneA.matrix != preservedOutput.matrix
        && randomizedSceneA.nodes[0u].exciterSource
            == preservedOutput.nodes[0u].exciterSource
        && randomizedSceneA.nodes[0u].pedal
            == preservedOutput.nodes[0u].pedal
        && randomizedSceneA.morph == preservedOutput.morph
        && randomizedSceneA.outputGainDb == preservedOutput.outputGainDb,
        "RANDOM A changed Scene B or shared performance controls");
    ok &= check(randomizedSceneB.nodes[0u].frequencyHz
            == preservedOutput.nodes[0u].frequencyHz
        && randomizedSceneB.matrix == preservedOutput.matrix
        && randomizedSceneB.auxSend == preservedOutput.auxSend
        && randomizedSceneB.sceneBNodes[0u].frequencyHz
            != preservedOutput.sceneBNodes[0u].frequencyHz
        && randomizedSceneB.sceneBMatrix != preservedOutput.sceneBMatrix
        && randomizedSceneB.morph == preservedOutput.morph
        && randomizedSceneB.outputMode == preservedOutput.outputMode,
        "RANDOM B changed Scene A or shared performance controls");
    synth.panic();
    synth.setParams(randomA);
    synth.strikeAll(1.0f);
    for (uint32_t frame = 0u; frame < 16384u; ++frame) {
        synth.processFrame(output.data());
        for (float sample : output) {
            ok &= check(std::isfinite(sample) && std::abs(sample) <= 1.0f,
                "constrained random patch escaped its safety ceiling");
        }
    }

    auto wild = synth.params();
    wild.excite = 1.0f;
    wild.morph = 1.0f;
    wild.morphSource = s3g::FeedbackMorphSource::Manual;
    wild.outputGainDb = 6.0f;
    wild.matrix.fill(1.0f);
    wild.sceneBMatrix.fill(1.0f);
    wild.sceneBAuxSend.fill(1.0f);
    for (uint32_t node = 0u; node < s3g::kFeedbackShiftChannels; ++node) {
        wild.nodes[node].frequencyHz = node < 4u ? -6000.0f : 6000.0f;
        wild.nodes[node].regeneration = 1.0f;
        wild.nodes[node].color = 1.0f;
        wild.nodes[node].body = 1.0f;
        wild.nodes[node].pedal = static_cast<s3g::FeedbackPedalType>(
            node % s3g::kFeedbackPedalTypeCount);
        wild.sceneBNodes[node] = wild.nodes[node];
        synth.strike(node, 1.0f);
    }
    synth.setParams(wild);
    float minimumGovernor = 1.0f;
    for (uint32_t frame = 0u; frame < 96000u; ++frame) {
        synth.processFrame(output.data());
        minimumGovernor = std::min(minimumGovernor,
            synth.minimumGovernor());
        for (float sample : output) {
            ok &= check(std::isfinite(sample) && std::abs(sample) <= 1.0f,
                "fully connected wild matrix escaped its safety ceiling");
        }
    }
    ok &= check(minimumGovernor < 0.85f,
        "continuous containment did not respond to a wild matrix");

    auto stopped = synth.params();
    stopped.run = false;
    synth.setParams(stopped);
    for (uint32_t frame = 0u; frame < 96000u; ++frame) {
        synth.processFrame(output.data());
    }
    for (float sample : output) {
        ok &= check(std::abs(sample) < 1.0e-5f,
            "RUN off did not fade every output to silence");
    }

    if (ok) std::cout << "feedback shift smoke tests passed\n";
    return ok ? 0 : 1;
}
