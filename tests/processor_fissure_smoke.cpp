#include "s3g_processor_fissure.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>

namespace {

bool check(bool condition, const char* message)
{
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}

struct RenderStats {
    double energy = 0.0;
    double checksum = 0.0;
    float peak = 0.0f;
    std::array<double, s3g::kProcessorFissureMaxOutputs> channelEnergy {};
};

struct InputCouplingStats {
    double residualEnergy = 0.0;
    double inputEnergy = 0.0;
    double maximumCorrelation = 0.0;
};

RenderStats render(s3g::ProcessorFissure& processor, uint32_t channels,
    uint32_t frames, bool useInput)
{
    RenderStats stats;
    std::array<float, 2> input {};
    std::array<float, s3g::kProcessorFissureMaxOutputs> output {};
    constexpr float twoPi = 6.28318530717958647692f;
    for (uint32_t frame = 0u; frame < frames; ++frame) {
        input[0] = useInput
            ? 0.22f * std::sin(twoPi * 173.0f
                * static_cast<float>(frame) / 48000.0f)
            : 0.0f;
        input[1] = useInput
            ? 0.18f * std::sin(twoPi * 281.0f
                * static_cast<float>(frame) / 48000.0f)
            : 0.0f;
        processor.processFrame(useInput ? input.data() : nullptr,
            output.data(), channels);
        for (uint32_t channel = 0u; channel < channels; ++channel) {
            const float sample = output[channel];
            if (!std::isfinite(sample)) {
                stats.energy = -1.0;
                return stats;
            }
            stats.peak = std::max(stats.peak, std::abs(sample));
            const double squared = static_cast<double>(sample) * sample;
            stats.energy += squared;
            stats.channelEnergy[channel] += squared;
            stats.checksum += static_cast<double>(sample)
                * static_cast<double>(1u + ((frame * 17u + channel) % 97u));
        }
    }
    return stats;
}

float referenceOutputLimit(float value)
{
    const float magnitude = std::abs(value);
    if (magnitude <= 0.82f) return value;
    const float limited = 0.82f + 0.18f
        * std::tanh((magnitude - 0.82f) / 0.18f);
    return std::copysign(std::min(1.0f, limited), value);
}

bool verifiesTrueOutputFold(uint32_t outputChannels,
    s3g::RingOutputFormat format)
{
    s3g::ProcessorFissure direct;
    s3g::ProcessorFissure folded;
    direct.prepare(48000.0, s3g::kProcessorFissureMaxOutputs);
    folded.prepare(48000.0, outputChannels);
    s3g::RingOutputMixdown reference;
    reference.configure(format, 0.0f);
    std::array<float, s3g::kProcessorFissureMaxOutputs> directOutput {};
    std::array<float, s3g::kProcessorFissureMaxOutputs> foldedOutput {};
    std::array<float, s3g::kProcessorFissureMaxOutputs> expected {};
    double energy = 0.0;
    double difference = 0.0;
    for (uint32_t frame = 0u; frame < 16384u; ++frame) {
        direct.processFrame(nullptr, directOutput.data(),
            s3g::kProcessorFissureMaxOutputs);
        folded.processFrame(nullptr, foldedOutput.data(), outputChannels);
        reference.processFrame(directOutput.data(), expected.data());
        for (uint32_t channel = 0u; channel < outputChannels; ++channel) {
            const float wanted = referenceOutputLimit(expected[channel]);
            energy += static_cast<double>(wanted) * wanted;
            difference += std::abs(static_cast<double>(
                foldedOutput[channel] - wanted));
        }
    }
    return energy > 1.0e-7 && difference < 1.0e-5;
}

InputCouplingStats measureInputCoupling(s3g::ProcessorFissure& dry,
    s3g::ProcessorFissure& live, uint32_t frames)
{
    InputCouplingStats stats;
    std::array<float, 2> input {};
    std::array<float, 2> dryOutput {};
    std::array<float, 2> liveOutput {};
    std::array<std::array<double, 2>, 2> correlation {};
    std::array<double, 2> residualEnergy {};
    std::array<double, 2> channelInputEnergy {};
    constexpr float twoPi = 6.28318530717958647692f;
    for (uint32_t frame = 0u; frame < frames; ++frame) {
        input[0] = 0.22f * std::sin(twoPi * 173.0f
            * static_cast<float>(frame) / 48000.0f);
        input[1] = 0.18f * std::sin(twoPi * 281.0f
            * static_cast<float>(frame) / 48000.0f);
        dry.processFrame(nullptr, dryOutput.data(), 2u);
        live.processFrame(input.data(), liveOutput.data(), 2u);
        if (frame < 2048u) continue;
        for (uint32_t inputChannel = 0u; inputChannel < 2u;
             ++inputChannel) {
            const double sample = input[inputChannel];
            channelInputEnergy[inputChannel] += sample * sample;
            stats.inputEnergy += sample * sample;
        }
        for (uint32_t outputChannel = 0u; outputChannel < 2u;
             ++outputChannel) {
            const double residual = static_cast<double>(
                liveOutput[outputChannel] - dryOutput[outputChannel]);
            residualEnergy[outputChannel] += residual * residual;
            stats.residualEnergy += residual * residual;
            for (uint32_t inputChannel = 0u; inputChannel < 2u;
                 ++inputChannel) {
                correlation[outputChannel][inputChannel] += residual
                    * static_cast<double>(input[inputChannel]);
            }
        }
    }
    for (uint32_t outputChannel = 0u; outputChannel < 2u;
         ++outputChannel) {
        for (uint32_t inputChannel = 0u; inputChannel < 2u;
             ++inputChannel) {
            const double denominator = std::sqrt(
                residualEnergy[outputChannel]
                * channelInputEnergy[inputChannel]);
            if (denominator > 0.0) {
                stats.maximumCorrelation = std::max(
                    stats.maximumCorrelation,
                    std::abs(correlation[outputChannel][inputChannel])
                        / denominator);
            }
        }
    }
    return stats;
}

double measureProcessorDifference(s3g::ProcessorFissure& first,
    s3g::ProcessorFissure& second, uint32_t channels, uint32_t frames)
{
    std::array<float, s3g::kProcessorFissureMaxOutputs> firstOutput {};
    std::array<float, s3g::kProcessorFissureMaxOutputs> secondOutput {};
    double difference = 0.0;
    for (uint32_t frame = 0u; frame < frames; ++frame) {
        first.processFrame(nullptr, firstOutput.data(), channels);
        second.processFrame(nullptr, secondOutput.data(), channels);
        for (uint32_t channel = 0u; channel < channels; ++channel) {
            difference += std::abs(static_cast<double>(
                firstOutput[channel] - secondOutput[channel]));
        }
    }
    return difference;
}

} // namespace

int main()
{
    bool ok = true;
    constexpr uint32_t frames = 48000u;

    ok &= check(std::abs(s3g::processorFissureLowEnergyCurve(.20f)
                    - .04f) < 1.0e-7f
            && std::abs(s3g::processorFissureLowEnergyCurve(.50f)
                    - .25f) < 1.0e-7f,
        "the expanded low-energy control curve changed unexpectedly");
    ok &= check(s3g::processorFissureEventRateHz(0.0f) < .002f
            && s3g::processorFissureEventRateHz(.50f) > .5f
            && s3g::processorFissureEventRateHz(.50f) < 3.0f
            && s3g::processorFissureEventRateHz(1.0f) > 150.0f,
        "Rate no longer spans drone clocks through extreme cut-up rates");
    ok &= check(std::abs(s3g::processorFissureSpaceSeconds(0.0f)
                    - .008f) < 1.0e-7f
            && s3g::processorFissureSpaceSeconds(1.0f) > 3.9f,
        "Space no longer spans click gaps through multi-second absence");

    s3g::ProcessorFissure mappedControls;
    mappedControls.prepare(48000.0, 2u);
    auto mappedParams = mappedControls.params();
    mappedParams.pressure = .20f;
    mappedParams.mass = .20f;
    mappedParams.memory = .20f;
    mappedParams.body = .20f;
    mappedParams.voidAmount = .20f;
    mappedParams.space = .20f;
    mappedControls.setParams(mappedParams);
    mappedControls.reset();
    const auto performedMapped = mappedControls.performanceParams();
    ok &= check(std::abs(performedMapped.pressure - .04f) < 1.0e-7f
            && std::abs(performedMapped.mass - .04f) < 1.0e-7f
            && std::abs(performedMapped.memory - .04f) < 1.0e-7f
            && std::abs(performedMapped.body - .04f) < 1.0e-7f
            && std::abs(performedMapped.voidAmount - .104f) < 1.0e-6f
            && std::abs(performedMapped.space - .104f) < 1.0e-6f,
        "field controls did not enter the expanded low-energy mapping");

    auto splitParams = mappedControls.params();
    splitParams.edge = 1.0f;
    splitParams.rate = 0.0f;
    mappedControls.setParams(splitParams);
    mappedControls.reset();
    const float hardDroneRate = mappedControls.eventRateHz();
    splitParams.edge = 0.0f;
    splitParams.rate = 1.0f;
    mappedControls.setParams(splitParams);
    mappedControls.reset();
    ok &= check(hardDroneRate < .002f
            && mappedControls.eventRateHz() > 150.0f,
        "Edge and Rate remained coupled after the four-axis split");

    std::array<s3g::ProcessorFissure, 3> widthProcessors;
    std::array<RenderStats, 3> widthStats;
    constexpr std::array<uint32_t, 3> widths {{ 2u, 4u, 8u }};
    for (uint32_t index = 0u; index < widths.size(); ++index) {
        widthProcessors[index].prepare(48000.0, widths[index]);
        widthStats[index] = render(widthProcessors[index], widths[index],
            frames, false);
        ok &= check(widthStats[index].energy > 0.001,
            "an output-width variant produced silence");
        ok &= check(widthStats[index].peak <= 1.00001f,
            "the transparent safety ceiling was exceeded");
        for (uint32_t channel = 0u; channel < widths[index]; ++channel) {
            ok &= check(widthStats[index].channelEnergy[channel] > 1.0e-7,
                "a rendered output channel remained silent");
        }
    }
    for (uint32_t cell = 0u; cell < s3g::kProcessorFissureCells; ++cell) {
        const float stereoActivity = widthProcessors[0].cellActivity(cell);
        ok &= check(std::abs(stereoActivity
                    - widthProcessors[1].cellActivity(cell)) < 1.0e-7f
                && std::abs(stereoActivity
                    - widthProcessors[2].cellActivity(cell)) < 1.0e-7f,
            "renderer width changed the internal eight-cell composition");
    }
    ok &= check(verifiesTrueOutputFold(2u,
            s3g::RingOutputFormat::StereoRing),
        "Stereo output was not a true fold of the completed eight-lane field");
    ok &= check(verifiesTrueOutputFold(4u,
            s3g::RingOutputFormat::QuadRing),
        "Quad output was not a true fold of the completed eight-lane field");

    s3g::ProcessorFissure deterministicA;
    s3g::ProcessorFissure deterministicB;
    deterministicA.prepare(48000.0, 8u);
    deterministicB.prepare(48000.0, 8u);
    const auto deterministicStatsA = render(
        deterministicA, 8u, frames / 2u, false);
    const auto deterministicStatsB = render(
        deterministicB, 8u, frames / 2u, false);
    ok &= check(deterministicStatsA.checksum == deterministicStatsB.checksum
            && deterministicStatsA.energy == deterministicStatsB.energy,
        "identical seeds did not reproduce the same performance");

    s3g::ProcessorFissure dryCoupling;
    s3g::ProcessorFissure liveCoupling;
    dryCoupling.prepare(48000.0, 2u);
    liveCoupling.prepare(48000.0, 2u);
    auto couplingParams = liveCoupling.params();
    couplingParams.voice = 1.0f;
    couplingParams.contact = 0.0f;
    couplingParams.shaker = 0.0f;
    couplingParams.inputGainDb = 6.0f;
    dryCoupling.setParams(couplingParams);
    liveCoupling.setParams(couplingParams);
    dryCoupling.reset();
    liveCoupling.reset();
    const auto couplingStats = measureInputCoupling(
        dryCoupling, liveCoupling, frames / 2u);
    ok &= check(couplingStats.residualEnergy > 1.0e-7,
        "Input Coupling did not perturb the feedback ecology");
    ok &= check(couplingStats.maximumCorrelation < 0.18,
        "Input Coupling exposed a sample-aligned foreground copy");
    ok &= check(liveCoupling.inputTransferActivity() > 0.001f
            && dryCoupling.inputTransferActivity() < 0.0001f,
        "ecology-transfer telemetry did not distinguish live mic excitation");
    ok &= check(liveCoupling.contactActivity() < 0.0001f,
        "Input Coupling unexpectedly entered the contact-transducer path");

    s3g::ProcessorFissure pitchDriven;
    pitchDriven.prepare(48000.0, 2u);
    auto pitchParams = pitchDriven.params();
    pitchParams.voice = 1.0f;
    pitchParams.contact = 0.0f;
    pitchParams.shaker = 0.0f;
    pitchParams.motion = 0.72f;
    pitchDriven.setParams(pitchParams);
    pitchDriven.reset();
    std::array<float, s3g::kProcessorFissureCells * 3u>
        modalBeforePitch {};
    for (uint32_t cell = 0u; cell < s3g::kProcessorFissureCells; ++cell) {
        for (uint32_t mode = 0u; mode < 3u; ++mode) {
            modalBeforePitch[cell * 3u + mode]
                = pitchDriven.modalTargetFrequencyHz(cell, mode);
        }
    }
    std::array<float, 2u> pitchInput {};
    std::array<float, 2u> pitchOutput {};
    constexpr float pitchTestHz = 220.0f;
    constexpr float twoPi = 6.28318530717958647692f;
    for (uint32_t frame = 0u; frame < frames * 2u; ++frame) {
        const float sample = 0.20f * std::sin(twoPi * pitchTestHz
            * static_cast<float>(frame) / 48000.0f);
        pitchInput = {{ sample, sample }};
        pitchDriven.processFrame(pitchInput.data(), pitchOutput.data(), 2u);
    }
    ok &= check(std::abs(pitchDriven.detectedPitchHz() - pitchTestHz)
                < pitchTestHz * 0.035f
            && pitchDriven.pitchConfidence() > 0.45f,
        "external-mic pitch estimator did not lock to a monophonic source");
    float maximumModalPitchChange = 0.0f;
    for (uint32_t cell = 0u; cell < s3g::kProcessorFissureCells; ++cell) {
        for (uint32_t mode = 0u; mode < 3u; ++mode) {
            const float before = modalBeforePitch[cell * 3u + mode];
            maximumModalPitchChange = std::max(maximumModalPitchChange,
                std::abs(pitchDriven.modalTargetFrequencyHz(cell, mode)
                    - before) / std::max(1.0f, before));
        }
    }
    ok &= check(maximumModalPitchChange > 0.015f,
        "a confident pitch estimate did not attract the modal ecology");

    s3g::ProcessorFissure heldVoice;
    heldVoice.prepare(48000.0, 2u);
    heldVoice.setParams(pitchParams);
    heldVoice.reset();
    float heldPhase = 0.0f;
    float earlyHeldLevel = 0.0f;
    float earlyHeldDrive = 0.0f;
    for (uint32_t frame = 0u; frame < frames * 3u; ++frame) {
        const float time = static_cast<float>(frame) / 48000.0f;
        const float frequency = 196.0f * std::exp2(
            0.22f * std::sin(twoPi * 4.8f * time) / 12.0f);
        heldPhase += frequency / 48000.0f;
        heldPhase -= std::floor(heldPhase);
        const float sample = 0.15f * std::sin(twoPi * heldPhase)
            + 0.052f * std::sin(twoPi * heldPhase * 2.0f)
            + 0.021f * std::sin(twoPi * heldPhase * 3.0f);
        pitchInput = {{ sample, sample }};
        heldVoice.processFrame(pitchInput.data(), pitchOutput.data(), 2u);
        if (frame == frames) {
            earlyHeldLevel = heldVoice.inputLevelActivity();
            earlyHeldDrive = heldVoice.sustainedPitchDrive();
        }
    }
    ok &= check(earlyHeldLevel > 0.08f
            && heldVoice.inputLevelActivity() > earlyHeldLevel * 0.72f,
        "a continuously held mic note lost its post-input level");
    ok &= check(earlyHeldDrive > 0.20f
            && heldVoice.sustainedPitchDrive() > earlyHeldDrive * 0.72f
            && heldVoice.pitchConfidence() > 0.30f,
        "a continuously held sung note lost pitch-driven ecology pressure");

    s3g::ProcessorFissure highPitchDriven;
    highPitchDriven.prepare(48000.0, 2u);
    highPitchDriven.setParams(pitchParams);
    highPitchDriven.reset();
    constexpr float highPitchTestHz = 3200.0f;
    for (uint32_t frame = 0u; frame < frames; ++frame) {
        const float sample = 0.20f * std::sin(twoPi * highPitchTestHz
            * static_cast<float>(frame) / 48000.0f);
        pitchInput = {{ sample, sample }};
        highPitchDriven.processFrame(
            pitchInput.data(), pitchOutput.data(), 2u);
    }
    ok &= check(std::abs(highPitchDriven.detectedPitchHz()
                    - highPitchTestHz) < highPitchTestHz * 0.04f
            && highPitchDriven.pitchConfidence() > 0.40f,
        "external-mic pitch estimator did not retain high spring tones");

    s3g::ProcessorFissure unpitchedInput;
    unpitchedInput.prepare(48000.0, 2u);
    unpitchedInput.setParams(pitchParams);
    unpitchedInput.reset();
    uint32_t noiseState = 0x13579bdu;
    for (uint32_t frame = 0u; frame < frames; ++frame) {
        noiseState ^= noiseState << 13u;
        noiseState ^= noiseState >> 17u;
        noiseState ^= noiseState << 5u;
        const float sample = (static_cast<float>(noiseState & 0x00ffffffu)
                / static_cast<float>(0x00800000u) - 1.0f)
            * 0.20f;
        pitchInput = {{ sample, sample }};
        unpitchedInput.processFrame(
            pitchInput.data(), pitchOutput.data(), 2u);
    }
    ok &= check(unpitchedInput.pitchConfidence() < 0.20f,
        "unpitched mic noise produced a confident modal pitch target");

    s3g::ProcessorFissure boxIdentity;
    s3g::ProcessorFissure rattleIdentity;
    s3g::ProcessorFissure springIdentity;
    boxIdentity.prepare(48000.0, 8u);
    rattleIdentity.prepare(48000.0, 8u);
    springIdentity.prepare(48000.0, 8u);
    auto attachedParams = boxIdentity.params();
    attachedParams.contact = 0.0f;
    attachedParams.voice = 0.0f;
    attachedParams.shaker = 0.0f;
    attachedParams.rattle = 0.0f;
    attachedParams.spring = 0.0f;
    auto boxParams = attachedParams;
    boxParams.shaker = 1.0f;
    auto rattleParams = attachedParams;
    rattleParams.rattle = 1.0f;
    auto springParams = attachedParams;
    springParams.spring = 1.0f;
    boxIdentity.setParams(boxParams);
    rattleIdentity.setParams(rattleParams);
    springIdentity.setParams(springParams);
    boxIdentity.reset();
    rattleIdentity.reset();
    springIdentity.reset();
    const float dryHighMode = boxIdentity.modalTargetFrequencyHz(3u, 2u);
    const float springHighMode = springIdentity.modalTargetFrequencyHz(3u, 2u);
    const double boxRattleDifference = measureProcessorDifference(
        boxIdentity, rattleIdentity, 8u, frames / 2u);
    boxIdentity.reset();
    springIdentity.reset();
    const double boxSpringDifference = measureProcessorDifference(
        boxIdentity, springIdentity, 8u, frames / 2u);
    rattleIdentity.reset();
    springIdentity.reset();
    const double rattleSpringDifference = measureProcessorDifference(
        rattleIdentity, springIdentity, 8u, frames / 2u);
    ok &= check(boxRattleDifference > 0.1
            && boxSpringDifference > 0.1
            && rattleSpringDifference > 0.1
            && springHighMode > dryHighMode * 1.6f,
        "attached-object controls did not retain distinct sonic identities");

    s3g::ProcessorFissure authored;
    authored.prepare(48000.0, 8u);
    authored.setMatrixRoute(3u, 6u, -0.73f);
    authored.setCellLevel(3u, 0.27f);
    auto object = authored.object(3u);
    object.size = 0.14f;
    object.decay = 0.91f;
    object.hardness = 0.86f;
    object.sensitivity = 0.77f;
    object.drive = 0.68f;
    authored.setObject(3u, object);
    ok &= check(std::abs(authored.matrixRoute(3u, 6u) + 0.73f)
                < 1.0e-7f,
        "an authored signed matrix route was not retained");
    ok &= check(std::abs(authored.cellLevel(3u) - 0.27f) < 1.0e-7f,
        "an authored cell level was not retained");
    ok &= check(std::abs(authored.object(3u).size - 0.14f) < 1.0e-7f
            && std::abs(authored.object(3u).decay - 0.91f) < 1.0e-7f
            && std::abs(authored.object(3u).hardness - 0.86f) < 1.0e-7f,
        "authored physical-object character was not retained");

    s3g::ProcessorFissure actions;
    actions.prepare(48000.0, 4u);
    render(actions, 4u, frames / 8u, false);
    const uint32_t beforeCut = actions.matrixRevision();
    const uint32_t beforeCutSplice = actions.cutRevision();
    const float routeBeforeCut = actions.matrixRoute(2u, 5u);
    actions.trigger(s3g::ProcessorFissureAction::Cut);
    ok &= check(actions.matrixRevision() == beforeCut
            && actions.matrixRoute(2u, 5u) == routeBeforeCut,
        "a physical Cut gesture unexpectedly replaced authored topology");
    const auto cutStats = render(actions, 4u, frames / 16u, false);
    uint32_t cutCells = 0u;
    for (uint32_t cell = 0u; cell < s3g::kProcessorFissureCells; ++cell) {
        if (actions.cutDelayFrames(cell) > 0u) ++cutCells;
    }
    ok &= check(actions.cutRevision() >= beforeCutSplice
                + s3g::kProcessorFissureCells
            && cutCells == s3g::kProcessorFissureCells
            && cutStats.energy > 0.0001,
        "Cut did not commit guarded memory splices across all cells");

    s3g::ProcessorFissure maskedCuts;
    maskedCuts.prepare(48000.0, 8u);
    for (uint32_t cell = 0u; cell < s3g::kProcessorFissureCells; ++cell) {
        maskedCuts.setCutMask(cell, cell == 1u || cell == 5u);
    }
    maskedCuts.trigger(s3g::ProcessorFissureAction::Cut);
    render(maskedCuts, 8u, 4096u, false);
    uint32_t maskedCutCells = 0u;
    for (uint32_t cell = 0u; cell < s3g::kProcessorFissureCells; ++cell) {
        const bool cut = maskedCuts.cutDelayFrames(cell) > 0u;
        if (cut) ++maskedCutCells;
        ok &= check(cut == (cell == 1u || cell == 5u),
            "the cut mask did not isolate manual fracture eligibility");
    }
    ok &= check(maskedCutCells == 2u && maskedCuts.cutRevision() >= 2u,
        "manual Cut ignored the eight-cell eligibility mask");

    s3g::ProcessorFissure sparseCuts;
    s3g::ProcessorFissure denseCuts;
    sparseCuts.prepare(48000.0, 2u);
    denseCuts.prepare(48000.0, 2u);
    auto sparseCutParams = sparseCuts.params();
    sparseCutParams.edge = 0.10f;
    sparseCutParams.rate = 0.10f;
    sparseCutParams.voidAmount = 0.08f;
    sparseCutParams.space = 0.08f;
    sparseCuts.setCutVariation(0.12f);
    sparseCuts.setParams(sparseCutParams);
    sparseCuts.reset();
    auto denseCutParams = sparseCutParams;
    denseCutParams.edge = 0.96f;
    denseCutParams.rate = 0.96f;
    denseCutParams.voidAmount = 0.82f;
    denseCutParams.space = 0.42f;
    denseCutParams.memory = 0.88f;
    denseCutParams.motion = 0.74f;
    denseCuts.setCutVariation(0.94f);
    denseCuts.setParams(denseCutParams);
    denseCuts.reset();
    render(sparseCuts, 2u, frames / 2u, false);
    const auto denseCutStats = render(
        denseCuts, 2u, frames / 2u, false);
    ok &= check(denseCuts.cutRevision() > sparseCuts.cutRevision() + 8u
            && denseCutStats.energy > 0.0001,
        "Rate/Void did not span sparse motion through dense cut-up splices");

    s3g::ProcessorFissure rateGesture;
    rateGesture.prepare(48000.0, 2u);
    auto rateGestureParams = rateGesture.params();
    rateGestureParams.edge = 0.80f;
    rateGestureParams.rate = 0.0f;
    rateGestureParams.voidAmount = 0.0f;
    rateGesture.setParams(rateGestureParams);
    rateGesture.reset();
    for (uint32_t cell = 0u; cell < s3g::kProcessorFissureCells; ++cell) {
        rateGesture.setCutMask(cell, false);
    }
    render(rateGesture, 2u, 24000u, false);
    const uint32_t droneCuts = rateGesture.cutRevision();
    rateGestureParams.rate = .80f;
    rateGestureParams.voidAmount = .50f;
    for (uint32_t cell = 0u; cell < s3g::kProcessorFissureCells; ++cell) {
        rateGesture.setCutMask(cell, true);
    }
    rateGesture.setCutVariation(1.0f);
    rateGesture.setParams(rateGestureParams);
    render(rateGesture, 2u, 12000u, false);
    ok &= check(droneCuts == 0u
            && rateGesture.cutRevision() > droneCuts + 4u,
        "a rapid Rate gesture did not wake clocks scheduled in drone time");

    s3g::ProcessorFissure padCuts;
    padCuts.prepare(48000.0, 8u);
    auto padParams = padCuts.params();
    padParams.edge = 0.08f;
    padParams.voidAmount = 0.04f;
    padParams.memory = 0.72f;
    padCuts.setParams(padParams);
    padCuts.reset();
    render(padCuts, 8u, 12000u, false);
    const uint32_t beforePadCuts = padCuts.cutRevision();
    padCuts.setFracturePerformance(1.0f, 1.0f);
    const auto padStats = render(padCuts, 8u, 18000u, false);
    ok &= check(padCuts.cutRevision() > beforePadCuts + 8u
            && padStats.energy > 0.0001,
        "the spring fracture pad did not create a dense temporary cut field");
    padCuts.setFracturePerformance(0.0f, 0.0f);

    s3g::ProcessorFissure performedPucks;
    performedPucks.prepare(48000.0, 8u);
    auto performedPuckParams = performedPucks.params();
    performedPuckParams.edge = 0.16f;
    performedPuckParams.rate = 0.12f;
    performedPuckParams.voidAmount = 0.06f;
    performedPuckParams.space = 0.08f;
    performedPucks.setParams(performedPuckParams);
    performedPucks.reset();
    performedPucks.setFracturePerformance(0.24f, 0.72f, 0.66f, 0.58f);
    const uint32_t beforeFlick = performedPucks.cutRevision();
    performedPucks.pushFractureGesture(0u, 1.0f, 0.0f, 1.0f);
    render(performedPucks, 8u, 128u, false);
    float horizontalTraversal = 0.0f;
    for (uint32_t cell = 0u; cell < 8u; ++cell) {
        horizontalTraversal += performedPucks.fractureGestureActivity(
            0u, cell);
    }
    ok &= check(horizontalTraversal > 1.2f
            && performedPucks.cutRevision() > beforeFlick
            && performedPucks.fractureGestureEnergy(0u) > 0.25f,
        "a fast Cut Density flick did not traverse and cut multiple cells");

    performedPucks.pushFractureGesture(0u, -1.0f, 0.0f, 0.92f);
    render(performedPucks, 8u, 32u, false);
    ok &= check(performedPucks.fractureScarActivity() > 0.05f,
        "a fast directional reversal did not leave a temporary route scar");

    performedPucks.pushFractureGesture(1u, 0.0f, 1.0f, 0.88f);
    render(performedPucks, 8u, 64u, false);
    float linkedTraversal = 0.0f;
    for (uint32_t cell = 0u; cell < 8u; ++cell) {
        linkedTraversal += performedPucks.fractureGestureActivity(1u, cell);
    }
    ok &= check(linkedTraversal > 0.9f,
        "a vertical Rupture Shape gesture did not spread through links");
    render(performedPucks, 8u, 24000u, false);
    ok &= check(performedPucks.fractureGestureEnergy(0u) < 0.08f
            && performedPucks.fractureGestureEnergy(1u) < 0.08f
            && std::abs(performedPucks.fractureForce() - 0.72f) < 0.0001f
            && std::abs(performedPucks.fractureSpace() - 0.58f) < 0.0001f,
        "held puck position incorrectly retained gesture velocity");

    s3g::ProcessorFissure performanceLoop;
    performanceLoop.prepare(48000.0, 8u);
    auto loopParams = performanceLoop.params();
    loopParams.edge = 0.08f;
    loopParams.rate = 0.08f;
    loopParams.voidAmount = 0.04f;
    loopParams.space = 0.04f;
    loopParams.memory = 0.28f;
    loopParams.shaker = 0.05f;
    performanceLoop.setParams(loopParams);
    performanceLoop.reset();
    performanceLoop.setGrab(true);
    ok &= check(performanceLoop.grabbing() && !performanceLoop.grabbed(),
        "the first Grab toggle did not start a fresh performance capture");
    render(performanceLoop, 8u, 4800u, false);
    loopParams.edge = 0.94f;
    loopParams.rate = 0.94f;
    loopParams.voidAmount = 0.78f;
    loopParams.space = 0.72f;
    loopParams.memory = 0.86f;
    loopParams.shaker = 0.82f;
    performanceLoop.setParams(loopParams);
    performanceLoop.setCutVariation(0.91f);
    performanceLoop.setFracturePerformance(0.88f, 0.92f);
    performanceLoop.trigger(s3g::ProcessorFissureAction::Cut);
    render(performanceLoop, 8u, 4800u, false);
    performanceLoop.setGrab(false);
    const uint32_t capturedCutRevision = performanceLoop.cutRevision();
    ok &= check(!performanceLoop.grabbing() && performanceLoop.grabbed()
            && performanceLoop.grabRevision() > 0u
            && performanceLoop.performanceFrameCount() >= 39u
            && performanceLoop.grabDurationSeconds() >= 0.19f
            && performanceLoop.grabDurationSeconds() <= 0.22f,
        "the second Grab toggle did not close the performed duration");

    loopParams.edge = 0.02f;
    loopParams.rate = 0.02f;
    loopParams.voidAmount = 0.02f;
    loopParams.space = 0.02f;
    loopParams.memory = 0.12f;
    loopParams.shaker = 0.0f;
    performanceLoop.setParams(loopParams);
    performanceLoop.setCutVariation(0.02f);
    performanceLoop.setFracturePerformance(0.0f, 0.0f);
    render(performanceLoop, 8u, 4096u, false);
    performanceLoop.setRepeat(true);
    std::array<float, s3g::kProcessorFissureMaxOutputs> loopOutput {};
    float minimumLoopEdge = 1.0f;
    float maximumLoopEdge = 0.0f;
    float previousPhase = 0.0f;
    uint32_t phaseWraps = 0u;
    const uint32_t repeatFrames = static_cast<uint32_t>(
        performanceLoop.grabDurationSeconds() * 48000.0f * 3.2f);
    for (uint32_t frame = 0u; frame < repeatFrames; ++frame) {
        performanceLoop.processFrame(nullptr, loopOutput.data(), 8u);
        const float edge = performanceLoop.performanceParams().edge;
        minimumLoopEdge = std::min(minimumLoopEdge, edge);
        maximumLoopEdge = std::max(maximumLoopEdge, edge);
        const float phase = performanceLoop.repeatPhase();
        if (phase + 0.5f < previousPhase) ++phaseWraps;
        previousPhase = phase;
    }
    ok &= check(performanceLoop.repeatMix() > 0.8f
            && phaseWraps >= 2u
            && minimumLoopEdge < 0.20f && maximumLoopEdge > 0.78f
            && performanceLoop.cutRevision() >= capturedCutRevision + 16u,
        "Repeat did not replay the captured parameter and cut-event phrase");
    performanceLoop.setRepeat(false);
    render(performanceLoop, 8u, 16384u, false);
    ok &= check(performanceLoop.repeatMix() < 0.05f,
        "momentary Repeat did not crossfade back to the live ecology");
    actions.trigger(s3g::ProcessorFissureAction::Breach);
    const auto breachStats = render(actions, 4u, frames / 4u, false);
    ok &= check(breachStats.energy > 0.001,
        "Breach did not produce an audible cascade");

    actions.trigger(s3g::ProcessorFissureAction::Panic);
    const auto panicStats = render(actions, 4u, 2048u, true);
    ok &= check(actions.silenced() && panicStats.energy == 0.0,
        "Panic did not immediately silence internal and external excitation");
    actions.rearm();
    const auto rearmStats = render(actions, 4u, frames / 16u, false);
    ok &= check(!actions.silenced() && rearmStats.energy > 0.00001,
        "preset-style rearm did not reopen and excite a depleted ecology");
    actions.trigger(s3g::ProcessorFissureAction::Panic);
    actions.strikeCell(3u, 1.0f);
    const auto strikeStats = render(actions, 4u, frames / 16u, false);
    ok &= check(!actions.silenced() && strikeStats.energy > 0.00001,
        "a MIDI-style cell strike did not rearm Panic");
    actions.trigger(s3g::ProcessorFissureAction::Panic);
    actions.trigger(s3g::ProcessorFissureAction::Reseed, 90210u);
    const auto reseedStats = render(actions, 4u, frames / 8u, false);
    ok &= check(!actions.silenced() && reseedStats.energy > 0.0001,
        "New did not restart a panicked performance");

    if (!ok) return 1;
    std::cout << "Processor Fissure DSP smoke passed\n";
    return 0;
}
