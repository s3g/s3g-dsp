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

struct RepeatContrastStats {
    double liveEnergy = 0.0;
    double repeatEnergy = 0.0;
    double differenceEnergy = 0.0;
    double correlation = 0.0;
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

RepeatContrastStats measureRepeatContrast(s3g::ProcessorFissure& live,
    s3g::ProcessorFissure& repeated, uint32_t frames)
{
    RepeatContrastStats stats;
    std::array<float, s3g::kProcessorFissureMaxOutputs> liveOutput {};
    std::array<float, s3g::kProcessorFissureMaxOutputs> repeatOutput {};
    double dot = 0.0;
    for (uint32_t frame = 0u; frame < frames; ++frame) {
        live.processFrame(nullptr, liveOutput.data(), 8u);
        repeated.processFrame(nullptr, repeatOutput.data(), 8u);
        for (uint32_t channel = 0u; channel < 8u; ++channel) {
            const double a = liveOutput[channel];
            const double b = repeatOutput[channel];
            const double difference = b - a;
            stats.liveEnergy += a * a;
            stats.repeatEnergy += b * b;
            stats.differenceEnergy += difference * difference;
            dot += a * b;
        }
    }
    const double denominator = std::sqrt(
        stats.liveEnergy * stats.repeatEnergy);
    stats.correlation = denominator > 0.0 ? dot / denominator : 1.0;
    return stats;
}

} // namespace

int main()
{
    bool ok = true;
    constexpr uint32_t frames = 48000u;

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

    s3g::ProcessorFissure quietShaker;
    s3g::ProcessorFissure activeShaker;
    quietShaker.prepare(48000.0, 8u);
    activeShaker.prepare(48000.0, 8u);
    auto quietPhysical = quietShaker.params();
    quietPhysical.shaker = 0.0f;
    quietPhysical.contact = 0.0f;
    quietShaker.setParams(quietPhysical);
    auto activePhysical = quietPhysical;
    activePhysical.shaker = 1.0f;
    activePhysical.rattle = 0.85f;
    activePhysical.spring = 0.78f;
    activeShaker.setParams(activePhysical);
    const auto quietPhysicalStats = render(
        quietShaker, 8u, frames / 2u, false);
    const auto activePhysicalStats = render(
        activeShaker, 8u, frames / 2u, false);
    ok &= check(std::abs(activePhysicalStats.checksum
                - quietPhysicalStats.checksum) > 0.1,
        "Shaker/rattle modal excitation did not change the performance");

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
    sparseCutParams.voidAmount = 0.08f;
    sparseCuts.setCutVariation(0.12f);
    sparseCuts.setParams(sparseCutParams);
    sparseCuts.reset();
    auto denseCutParams = sparseCutParams;
    denseCutParams.edge = 0.96f;
    denseCutParams.voidAmount = 0.82f;
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
        "Edge/Void did not span sparse motion through dense cut-up splices");

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

    padCuts.grabHistory();
    const uint32_t grabRevision = padCuts.grabRevision();
    padCuts.setRepeat(true);
    const auto repeatStats = render(padCuts, 8u, 4096u, false);
    ok &= check(padCuts.grabbed() && grabRevision > 0u
            && padCuts.repeatMix() > 0.8f && repeatStats.energy > 0.0001,
        "Grab/Repeat did not revisit ecological history while audio continued");
    padCuts.setRepeat(false);
    render(padCuts, 8u, 16384u, false);
    ok &= check(padCuts.repeatMix() < 0.05f,
        "momentary Repeat did not crossfade back to the live ecology");

    s3g::ProcessorFissure liveEcology;
    s3g::ProcessorFissure repeatedEcology;
    liveEcology.prepare(48000.0, 8u);
    repeatedEcology.prepare(48000.0, 8u);
    auto repeatParams = liveEcology.params();
    repeatParams.memory = 0.78f;
    repeatParams.edge = 0.56f;
    repeatParams.voidAmount = 0.22f;
    liveEcology.setParams(repeatParams);
    repeatedEcology.setParams(repeatParams);
    render(liveEcology, 8u, 36000u, false);
    render(repeatedEcology, 8u, 36000u, false);
    repeatedEcology.grabHistory();
    repeatedEcology.setRepeat(true);
    const auto repeatContrast = measureRepeatContrast(
        liveEcology, repeatedEcology, 16000u);
    ok &= check(repeatContrast.repeatEnergy > 0.0001
            && repeatContrast.differenceEnergy
                > repeatContrast.liveEnergy * 0.75
            && repeatContrast.correlation < 0.85,
        "Grab/Repeat remained perceptually buried beneath the live ecology");
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
