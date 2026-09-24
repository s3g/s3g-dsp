#include "s3g_sample_kit.h"

#include <array>
#include <cmath>
#include <iostream>
#include <memory>

namespace {

int failures = 0;

void check(bool condition, const char* message)
{
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

s3g::sample::SampleAsset constantAsset(float left, float right,
    uint32_t frames = 128u)
{
    s3g::sample::SampleAsset result;
    result.sampleRate = 48000.0;
    result.channelCount = 2u;
    result.channels[0u].assign(frames, left);
    result.channels[1u].assign(frames, right);
    return result;
}

struct OutputBlock {
    std::array<std::array<float, 32u>,
        s3g::sample::kSampleKitOutputChannels> samples {};
    std::array<float*, s3g::sample::kSampleKitOutputChannels> pointers {};

    OutputBlock()
    {
        for (std::size_t channel = 0u; channel < pointers.size(); ++channel)
            pointers[channel] = samples[channel].data();
    }
};

void testPadMappingAndRouting()
{
    using namespace s3g::sample;
    auto first = constantAsset(0.25f, 0.5f);
    auto second = constantAsset(0.125f, 0.25f);
    SampleKitEngine engine;
    check(engine.prepare(48000.0, 32u)
            && engine.setAsset(0u, &first)
            && engine.setAsset(1u, &second),
        "kit routing fixture did not prepare");
    SampleKitSettings settings;
    settings.masterGainDecibels = 0.0f;
    settings.activeOutputPairs = 16u;
    settings.pads[0u].gainDecibels = 0.0f;
    settings.pads[0u].outputPair = 0u;
    settings.pads[1u].gainDecibels = 0.0f;
    settings.pads[1u].outputPair = 15u;
    const std::array<RenderEvent, 2u> events {{
        { 0u, EventKind::NoteOn, 1u, 36u, 1.0f, 0u },
        { 2u, EventKind::NoteOn, 2u, 37u, 1.0f, 0u },
    }};
    OutputBlock output;
    engine.render(settings, events.data(), events.size(),
        output.pointers.data(), kSampleKitOutputChannels, 32u);
    check(std::abs(output.samples[0u][0u] - 0.25f) < 1.0e-5f
            && std::abs(output.samples[1u][0u] - 0.5f) < 1.0e-5f,
        "pad 1 did not reach pair 1");
    check(output.samples[30u][1u] == 0.0f
            && std::abs(output.samples[30u][2u] - 0.125f) < 1.0e-5f
            && std::abs(output.samples[31u][2u] - 0.25f) < 1.0e-5f,
        "pad 2 did not reach pair 16 sample-accurately");
    check(output.samples[2u][4u] == 0.0f,
        "unused pairs were not silent");
}

void testPairFoldAndChoke()
{
    using namespace s3g::sample;
    auto source = constantAsset(0.2f, 0.2f, 512u);
    SampleKitEngine engine;
    check(engine.prepare(48000.0, 32u)
            && engine.setAsset(0u, &source)
            && engine.setAsset(1u, &source),
        "kit choke fixture did not prepare");
    SampleKitSettings settings;
    settings.masterGainDecibels = 0.0f;
    settings.activeOutputPairs = 1u;
    settings.pads[0u].gainDecibels = 0.0f;
    settings.pads[0u].outputPair = 12u;
    settings.pads[0u].chokeGroup = 1u;
    settings.pads[1u].gainDecibels = 0.0f;
    settings.pads[1u].outputPair = 7u;
    settings.pads[1u].chokeGroup = 1u;
    const std::array<RenderEvent, 2u> events {{
        { 0u, EventKind::NoteOn, 1u, 36u, 1.0f, 0u },
        { 8u, EventKind::NoteOn, 2u, 37u, 1.0f, 0u },
    }};
    OutputBlock output;
    engine.render(settings, events.data(), events.size(),
        output.pointers.data(), kSampleKitOutputChannels, 32u);
    check(std::abs(output.samples[0u][7u] - 0.2f) < 1.0e-5f
            && std::abs(output.samples[0u][8u] - 0.2f) < 1.0e-5f,
        "one-pair mode did not fold pad routes to channels 1-2");
    check(engine.activeVoiceCount() == 1u,
        "matching choke group did not stop the older pad voice");
}

void testMixerAndEffects()
{
    using namespace s3g::sample;
    auto source = constantAsset(0.25f, 0.25f, 512u);
    SampleKitEngine engine;
    check(engine.prepare(48000.0, 32u)
            && engine.setAsset(0u, &source)
            && engine.setAsset(1u, &source),
        "kit mixer fixture did not prepare");
    SampleKitSettings settings;
    settings.masterGainDecibels = 0.0f;
    settings.pads[0u].gainDecibels = 0.0f;
    settings.pads[1u].gainDecibels = 0.0f;
    settings.pads[0u].soloed = true;
    const std::array<RenderEvent, 2u> events {{
        { 0u, EventKind::NoteOn, 1u, 36u, 1.0f, 0u },
        { 0u, EventKind::NoteOn, 2u, 37u, 1.0f, 0u },
    }};
    OutputBlock output;
    engine.render(settings, events.data(), events.size(),
        output.pointers.data(), kSampleKitOutputChannels, 32u);
    check(std::abs(output.samples[0u][0u] - 0.25f) < 1.0e-5f,
        "solo did not exclude a non-soloed pad");

    settings.pads[0u].soloed = false;
    settings.drive = 0.75f;
    settings.bitDepth = 8u;
    settings.rateReduction = 4u;
    OutputBlock effected;
    engine.render(settings, nullptr, 0u, effected.pointers.data(),
        kSampleKitOutputChannels, 32u);
    check(effected.samples[0u][0u] != effected.samples[0u][0u + 4u]
            || std::abs(effected.samples[0u][0u]) > 0.01f,
        "character effects produced no signal");
    check(effected.samples[0u][0u] == effected.samples[0u][1u]
            && effected.samples[0u][1u] == effected.samples[0u][2u],
        "rate reduction did not hold samples");

    SampleKitEngine widthEngine;
    check(widthEngine.prepare(48000.0, 32u)
            && widthEngine.setAsset(0u, &source),
        "kit output-width fixture did not prepare");
    SampleKitSettings widthSettings;
    widthSettings.masterGainDecibels = 0.0f;
    widthSettings.activeOutputPairs = 16u;
    widthSettings.rateReduction = 4u;
    widthSettings.pads[0u].gainDecibels = 0.0f;
    widthSettings.pads[0u].outputPair = 15u;
    const RenderEvent hit {
        0u, EventKind::NoteOn, 3u, 36u, 1.0f, 0u,
    };
    OutputBlock wide;
    widthEngine.render(widthSettings, &hit, 1u, wide.pointers.data(),
        kSampleKitOutputChannels, 1u);
    widthSettings.activeOutputPairs = 1u;
    OutputBlock narrow;
    widthEngine.render(widthSettings, nullptr, 0u, narrow.pointers.data(),
        kSampleKitOutputChannels, 32u);
    check(narrow.samples[30u][0u] == 0.0f
            && narrow.samples[31u][0u] == 0.0f,
        "inactive pairs leaked held rate-reduction samples");
}

void testVariationSelectionAndBypass()
{
    using namespace s3g::sample;
    auto first = constantAsset(0.1f, 0.1f, 4u);
    auto second = constantAsset(0.2f, 0.2f, 4u);
    auto third = constantAsset(0.3f, 0.3f, 4u);
    SampleKitEngine engine;
    check(engine.prepare(48000.0, 32u)
            && engine.setAsset(0u, 0u, &first)
            && engine.setAsset(0u, 1u, &second)
            && engine.setAsset(0u, 2u, &third),
        "kit variation fixture did not prepare");
    SampleKitSettings settings;
    settings.masterGainDecibels = 0.0f;
    settings.naturalEnabled = true;
    settings.pads[0u].gainDecibels = 0.0f;
    settings.pads[0u].velocitySensitivity = 0.0f;
    settings.pads[0u].naturalEnabled = true;
    settings.pads[0u].variationMode = SampleKitVariationMode::Cycle;
    const std::array<RenderEvent, 4u> cycle {{
        { 0u, EventKind::NoteOn, 1u, 36u, 1.0f, 0u },
        { 8u, EventKind::NoteOn, 2u, 36u, 1.0f, 0u },
        { 16u, EventKind::NoteOn, 3u, 36u, 1.0f, 0u },
        { 24u, EventKind::NoteOn, 4u, 36u, 1.0f, 0u },
    }};
    OutputBlock output;
    engine.render(settings, cycle.data(), cycle.size(),
        output.pointers.data(), kSampleKitOutputChannels, 32u);
    check(std::abs(output.samples[0u][0u] - 0.02f) < 1.0e-5f
            && std::abs(output.samples[0u][8u] - 0.04f) < 1.0e-5f
            && std::abs(output.samples[0u][16u] - 0.06f) < 1.0e-5f
            && std::abs(output.samples[0u][24u] - 0.02f) < 1.0e-5f,
        "cycle mode did not rotate through loaded variations");

    engine.reset();
    settings.pads[0u].naturalEnabled = false;
    const std::array<RenderEvent, 2u> bypassed {{
        { 0u, EventKind::NoteOn, 5u, 36u, 1.0f, 0u },
        { 8u, EventKind::NoteOn, 6u, 36u, 1.0f, 0u },
    }};
    OutputBlock bypassOutput;
    engine.render(settings, bypassed.data(), bypassed.size(),
        bypassOutput.pointers.data(), kSampleKitOutputChannels, 32u);
    check(std::abs(bypassOutput.samples[0u][0u] - 0.02f) < 1.0e-5f
            && std::abs(bypassOutput.samples[0u][8u] - 0.02f) < 1.0e-5f,
        "Natural bypass did not hold the first loaded variation");

    engine.reset();
    settings.pads[0u].naturalEnabled = true;
    settings.pads[0u].variationMode = SampleKitVariationMode::Velocity;
    const std::array<RenderEvent, 3u> layered {{
        { 0u, EventKind::NoteOn, 7u, 36u, 0.1f, 0u },
        { 8u, EventKind::NoteOn, 8u, 36u, 0.5f, 0u },
        { 16u, EventKind::NoteOn, 9u, 36u, 0.99f, 0u },
    }};
    OutputBlock layerOutput;
    engine.render(settings, layered.data(), layered.size(),
        layerOutput.pointers.data(), kSampleKitOutputChannels, 32u);
    check(std::abs(layerOutput.samples[0u][0u] - 0.02f) < 1.0e-5f
            && std::abs(layerOutput.samples[0u][8u] - 0.04f) < 1.0e-5f
            && std::abs(layerOutput.samples[0u][16u] - 0.06f) < 1.0e-5f,
        "velocity mode did not choose the expected sample layers");

    engine.reset();
    settings.pads[0u].variationMode = SampleKitVariationMode::NoRepeat;
    OutputBlock noRepeatOutput;
    engine.render(settings, cycle.data(), cycle.size(),
        noRepeatOutput.pointers.data(), kSampleKitOutputChannels, 32u);
    check(noRepeatOutput.samples[0u][0u]
                != noRepeatOutput.samples[0u][8u]
            && noRepeatOutput.samples[0u][8u]
                != noRepeatOutput.samples[0u][16u]
            && noRepeatOutput.samples[0u][16u]
                != noRepeatOutput.samples[0u][24u],
        "No Repeat selected adjacent duplicate variations");

    engine.reset();
    settings.pads[0u].variationMode = SampleKitVariationMode::Shuffle;
    OutputBlock shuffleOutput;
    engine.render(settings, cycle.data(), cycle.size(),
        shuffleOutput.pointers.data(), kSampleKitOutputChannels, 32u);
    const float shuffleA = shuffleOutput.samples[0u][0u];
    const float shuffleB = shuffleOutput.samples[0u][8u];
    const float shuffleC = shuffleOutput.samples[0u][16u];
    check(shuffleA != shuffleB && shuffleA != shuffleC
            && shuffleB != shuffleC
            && shuffleC != shuffleOutput.samples[0u][24u],
        "Shuffle did not exhaust a unique deck or avoid its boundary repeat");
}

void testDeterministicNaturalSeed()
{
    using namespace s3g::sample;
    auto first = constantAsset(0.1f, 0.1f, 128u);
    auto second = constantAsset(0.2f, 0.2f, 128u);
    const auto makeEngine = [&]() {
        auto engine = std::make_unique<SampleKitEngine>();
        check(engine->prepare(48000.0, 32u)
                && engine->setAsset(0u, 0u, &first)
                && engine->setAsset(0u, 1u, &second),
            "deterministic Natural fixture did not prepare");
        return engine;
    };
    SampleKitSettings settings;
    settings.masterGainDecibels = 0.0f;
    settings.randomSeed = 777u;
    settings.naturalEnabled = true;
    settings.pads[0u].gainDecibels = 0.0f;
    settings.pads[0u].velocitySensitivity = 0.0f;
    settings.pads[0u].naturalEnabled = true;
    settings.pads[0u].variationMode = SampleKitVariationMode::Random;
    settings.pads[0u].naturalGainDecibels = 3.0f;
    const std::array<RenderEvent, 4u> events {{
        { 0u, EventKind::NoteOn, 20u, 36u, 1.0f, 0u },
        { 8u, EventKind::NoteOn, 21u, 36u, 1.0f, 0u },
        { 16u, EventKind::NoteOn, 22u, 36u, 1.0f, 0u },
        { 24u, EventKind::NoteOn, 23u, 36u, 1.0f, 0u },
    }};
    auto firstEngine = makeEngine();
    auto secondEngine = makeEngine();
    OutputBlock firstOutput;
    OutputBlock secondOutput;
    firstEngine->render(settings, events.data(), events.size(),
        firstOutput.pointers.data(), kSampleKitOutputChannels, 32u);
    secondEngine->render(settings, events.data(), events.size(),
        secondOutput.pointers.data(), kSampleKitOutputChannels, 32u);
    check(firstOutput.samples == secondOutput.samples,
        "equal Natural seeds did not render identically");

    auto changedEngine = makeEngine();
    settings.randomSeed = 778u;
    OutputBlock changedOutput;
    changedEngine->render(settings, events.data(), events.size(),
        changedOutput.pointers.data(), kSampleKitOutputChannels, 32u);
    check(firstOutput.samples != changedOutput.samples,
        "changing the Natural seed did not change the rendered sequence");
}

void testNaturalTimingAcrossBlocks()
{
    using namespace s3g::sample;
    auto source = constantAsset(0.2f, 0.2f, 512u);
    SampleKitEngine engine;
    check(engine.prepare(48000.0, 32u)
            && engine.setAsset(0u, 0u, &source),
        "Natural timing fixture did not prepare");
    SampleKitSettings settings;
    settings.masterGainDecibels = 0.0f;
    settings.randomSeed = 777u;
    settings.naturalEnabled = true;
    settings.pads[0u].gainDecibels = 0.0f;
    settings.pads[0u].naturalEnabled = true;
    settings.pads[0u].naturalTimingMilliseconds = 12.0f;
    const RenderEvent hit {
        31u, EventKind::NoteOn, 30u, 36u, 1.0f, 0u,
    };
    OutputBlock firstBlock;
    engine.render(settings, &hit, 1u, firstBlock.pointers.data(),
        kSampleKitOutputChannels, 32u);
    check(firstBlock.samples[0u][31u] == 0.0f,
        "Natural timing did not delay a boundary hit");
    bool eventuallyPlayed = false;
    for (int block = 0; block < 20 && !eventuallyPlayed; ++block) {
        OutputBlock continuation;
        engine.render(settings, nullptr, 0u, continuation.pointers.data(),
            kSampleKitOutputChannels, 32u);
        eventuallyPlayed = std::any_of(continuation.samples[0u].begin(),
            continuation.samples[0u].end(), [](float sample) {
                return std::abs(sample) > 1.0e-6f;
            });
    }
    check(eventuallyPlayed,
        "a delayed Natural hit was lost at an audio-block boundary");

    engine.reset();
    settings.pads[0u].triggerMode = TriggerMode::Gate;
    const std::array<RenderEvent, 2u> cancelled {{
        { 31u, EventKind::NoteOn, 31u, 36u, 1.0f, 0u },
        { 31u, EventKind::NoteOff, 31u, 36u, 0.0f, 0u },
    }};
    OutputBlock cancelledStart;
    engine.render(settings, cancelled.data(), cancelled.size(),
        cancelledStart.pointers.data(), kSampleKitOutputChannels, 32u);
    bool cancelledPlayed = false;
    for (int block = 0; block < 20 && !cancelledPlayed; ++block) {
        OutputBlock continuation;
        engine.render(settings, nullptr, 0u, continuation.pointers.data(),
            kSampleKitOutputChannels, 32u);
        cancelledPlayed = std::any_of(continuation.samples[0u].begin(),
            continuation.samples[0u].end(), [](float sample) {
                return std::abs(sample) > 1.0e-6f;
            });
    }
    check(!cancelledPlayed,
        "note-off did not cancel a pending delayed Natural hit");
}

void testSingleSampleNaturalHumanization()
{
    using namespace s3g::sample;
    auto source = constantAsset(0.1f, 0.1f, 512u);
    SampleKitEngine engine;
    check(engine.prepare(48000.0, 32u)
            && engine.setAsset(0u, 0u, &source),
        "single-sample Natural fixture did not prepare");
    SampleKitSettings settings;
    settings.masterGainDecibels = 0.0f;
    settings.randomSeed = 777u;
    settings.naturalEnabled = true;
    settings.pads[0u].gainDecibels = 0.0f;
    settings.pads[0u].velocitySensitivity = 0.0f;
    settings.pads[0u].naturalEnabled = true;
    settings.pads[0u].naturalGainDecibels = 3.0f;
    const RenderEvent hit {
        0u, EventKind::NoteOn, 40u, 36u, 1.0f, 0u,
    };
    OutputBlock natural;
    engine.render(settings, &hit, 1u, natural.pointers.data(),
        kSampleKitOutputChannels, 32u);
    check(engine.lastSelectedVariation(0u) == 0u
            && std::abs(natural.samples[0u][0u] - 0.1f) > 1.0e-5f,
        "Natural did not vary gain for a pad with one loaded sample");
    check(std::abs(engine.lastNaturalOffsets(0u).gainDecibels) > 1.0e-5f,
        "single-sample Natural did not publish its per-hit offset");

    engine.reset();
    settings.pads[0u].naturalEnabled = false;
    OutputBlock bypassed;
    engine.render(settings, &hit, 1u, bypassed.pointers.data(),
        kSampleKitOutputChannels, 32u);
    check(std::abs(bypassed.samples[0u][0u] - 0.1f) < 1.0e-5f
            && engine.lastNaturalOffsets(0u).gainDecibels == 0.0f,
        "single-sample Natural bypass still applied hit jitter");
}

void testChopLayouts()
{
    using namespace s3g::sample;
    const auto equal = equalChopLayout(16u);
    check(equal.valid() && equal.sliceCount == 16u
            && std::abs(equal.boundaries[5u] - 0.3125) < 1.0e-12,
        "equal chop layout was not normalized across sixteen pads");

    const std::array<float, 6u> transients {{
        0.0f, 0.10f, 0.26f, 0.51f, 0.74f, 0.90f,
    }};
    const auto transient = transientChopLayout(
        transients.data(), transients.size(), 4u);
    check(transient.valid() && transient.sliceCount == 4u
            && std::abs(transient.boundaries[1u] - 0.10) < 1.0e-6
            && std::abs(transient.boundaries[3u] - 0.51) < 1.0e-6,
        "transient chop layout did not honor its region limit");

    const auto beat = beatGridChopLayout(4.0, 120.0, 1.0);
    check(beat.valid() && beat.sliceCount == 8u
            && std::abs(beat.boundaries[7u] - 0.875) < 1.0e-12,
        "beat-grid chop layout did not use source BPM");

    auto live = equalChopLayout(1u);
    check(addChopMarker(live, 0.65)
            && addChopMarker(live, 0.20)
            && live.valid() && live.sliceCount == 3u
            && std::abs(live.boundaries[1u] - 0.20) < 1.0e-12
            && std::abs(live.boundaries[2u] - 0.65) < 1.0e-12,
        "live markers were not inserted in timeline order");
    check(moveChopMarker(live, 1u, 0.30)
            && std::abs(live.boundaries[1u] - 0.30) < 1.0e-12
            && live.valid(),
        "a chop marker could not be moved safely");
    check(!addChopMarker(live, 0.30)
            && !moveChopMarker(live, 0u, 0.25),
        "chop markers accepted duplicates or an endpoint move");
    auto manual = equalChopLayout(1u);
    bool filled = true;
    for (std::size_t marker = 1u;
         marker < kSampleKitMaximumChops; ++marker)
        filled = addChopMarker(manual,
            static_cast<double>(marker)
                / static_cast<double>(kSampleKitMaximumChops)) && filled;
    check(filled && manual.valid()
            && manual.sliceCount == kSampleKitMaximumChops
            && !addChopMarker(manual, 0.99),
        "manual chop markers did not stop at sixteen regions");
    check(std::abs(preRolledChopPosition(0.5, 2.0, 20.0) - 0.49)
                < 1.0e-12
            && preRolledChopPosition(0.005, 2.0, 20.0) == 0.0,
        "chop pre-roll did not shift and clamp marker positions");
}

void testVelocityCurves()
{
    using namespace s3g::sample;
    const float input = 0.25f;
    const float verySoft = applySampleKitVelocityCurve(
        input, SampleKitVelocityCurve::VerySoft);
    const float soft = applySampleKitVelocityCurve(
        input, SampleKitVelocityCurve::Soft);
    const float linear = applySampleKitVelocityCurve(
        input, SampleKitVelocityCurve::Linear);
    const float hard = applySampleKitVelocityCurve(
        input, SampleKitVelocityCurve::Hard);
    check(verySoft > soft && soft > linear && linear > hard
            && std::abs(linear - input) < 1.0e-7f,
        "Sample Kit velocity curves were not ordered around linear");
    check(applySampleKitVelocityCurve(
                input, SampleKitVelocityCurve::Fixed) == 1.0f
            && applySampleKitVelocityCurve(
                0.0f, SampleKitVelocityCurve::Fixed) == 0.0f,
        "fixed Sample Kit velocity did not preserve zero and full hits");
}

} // namespace

int main()
{
    testPadMappingAndRouting();
    testPairFoldAndChoke();
    testMixerAndEffects();
    testVariationSelectionAndBypass();
    testDeterministicNaturalSeed();
    testNaturalTimingAcrossBlocks();
    testSingleSampleNaturalHumanization();
    testChopLayouts();
    testVelocityCurves();
    if (failures != 0) return 1;
    std::cout << "s3g Sample Kit smoke: ok\n";
    return 0;
}
