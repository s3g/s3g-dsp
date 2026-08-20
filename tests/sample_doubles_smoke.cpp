#include "s3g_sample_doubles.h"
#include "s3g_sample_tempo_estimator.h"

#include <array>
#include <cmath>
#include <iostream>
#include <vector>

namespace {

using namespace s3g::sample;

int failures = 0;

void check(bool condition, const char* message)
{
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

bool near(float actual, float expected, float tolerance = 1.0e-4f)
{
    return std::abs(actual - expected) <= tolerance;
}

SampleAsset rampAsset(uint8_t channels = 2u, uint32_t frames = 4096u)
{
    SampleAsset asset;
    asset.sampleRate = 1000.0;
    asset.channelCount = channels;
    for (uint8_t channel = 0u; channel < channels; ++channel) {
        asset.channels[channel].resize(frames);
        for (uint32_t frame = 0u; frame < frames; ++frame) {
            asset.channels[channel][frame] = static_cast<float>(frame)
                    * 0.0001f
                + static_cast<float>(channel) * 0.25f;
        }
    }
    return asset;
}

SampleAsset constantAsset(float value = 1.0f, uint32_t frames = 16000u)
{
    SampleAsset asset;
    asset.sampleRate = 1000.0;
    asset.channelCount = 1u;
    asset.channels[0u].assign(frames, value);
    return asset;
}

SampleAsset clickAsset(double bpm, bool antiPhaseStereo = false)
{
    SampleAsset asset;
    asset.sampleRate = 4000.0;
    asset.channelCount = antiPhaseStereo ? 2u : 1u;
    const uint32_t frames = static_cast<uint32_t>(asset.sampleRate * 24.0);
    for (uint8_t channel = 0u; channel < asset.channelCount; ++channel)
        asset.channels[channel].assign(frames, 0.0f);
    const uint32_t period = static_cast<uint32_t>(std::llround(
        asset.sampleRate * 60.0 / bpm));
    const uint32_t leading = static_cast<uint32_t>(asset.sampleRate * 1.0);
    for (uint32_t beat = leading; beat + 8u < frames; beat += period) {
        for (uint32_t sample = 0u; sample < 8u; ++sample) {
            const float value = std::exp(-static_cast<float>(sample) * 0.7f);
            asset.channels[0u][beat + sample] = value;
            if (antiPhaseStereo)
                asset.channels[1u][beat + sample] = -value;
        }
    }
    return asset;
}

void testTwoDeckOffsetAndStereo()
{
    auto asset = rampAsset();
    SampleDoublesEngine engine;
    check(engine.prepare(1000.0) && engine.setAsset(&asset),
        "two-deck fixture did not prepare");
    DoublesSettings settings;
    settings.sourceTempoBpm = 60.0;
    settings.speedSemitones = 0.0;
    settings.offsetBeats = 1.0;
    settings.crossfader = -1.0;
    settings.gainDecibels = 0.0f;
    const DoublesRenderEvent restart {
        2u, DoublesEventKind::Restart, 1u, 1.0f,
    };
    std::array<float, 8u> left {};
    std::array<float, 8u> right {};
    float* outputs[] { left.data(), right.data() };
    engine.render(settings, &restart, 1u, outputs, 2u,
        static_cast<uint32_t>(left.size()));
    check(left[0u] == 0.0f && left[1u] == 0.0f
            && near(left[2u], 0.0f) && near(left[7u], 0.0005f)
            && near(right[2u], 0.25f) && near(right[7u], 0.2505f),
        "Deck A launch, sample offset, or stereo lane mapping failed");

    engine.reset();
    settings.crossfader = 1.0;
    left.fill(0.0f);
    right.fill(0.0f);
    engine.render(settings, &restart, 1u, outputs, 2u,
        static_cast<uint32_t>(left.size()));
    check(near(left[2u], 0.1f) && near(left[7u], 0.1005f)
            && near(right[2u], 0.35f),
        "Deck B did not launch at the signed beat offset");
}

void testTrackerPunchGateAndVelocity()
{
    auto asset = rampAsset();
    SampleDoublesEngine engine;
    check(engine.prepare(1000.0) && engine.setAsset(&asset),
        "punch fixture did not prepare");
    DoublesSettings settings;
    settings.sourceTempoBpm = 60.0;
    settings.speedSemitones = 0.0;
    settings.offsetBeats = 1.0;
    settings.crossfader = -1.0;
    settings.gainDecibels = 0.0f;
    settings.crossfadeCurve = DoublesCrossfadeCurve::Cut;
    const std::array<DoublesRenderEvent, 3u> events {{
        { 0u, DoublesEventKind::Restart, 1u, 1.0f },
        { 10u, DoublesEventKind::PunchBOn, 2u, 1.0f },
        { 30u, DoublesEventKind::PunchBOff, 2u, 0.0f },
    }};
    std::array<float, 48u> left {};
    std::array<float, 48u> right {};
    float* outputs[] { left.data(), right.data() };
    engine.render(settings, events.data(), events.size(), outputs, 2u,
        static_cast<uint32_t>(left.size()));
    check(left[8u] < 0.002f && left[15u] > 0.10f
            && left[26u] > 0.10f && left[38u] < 0.01f,
        "gated Deck B punch did not cut over and return sample-accurately");

    engine.reset();
    settings.crossfadeCurve = DoublesCrossfadeCurve::Blend;
    const std::array<DoublesRenderEvent, 2u> shallow {{
        { 0u, DoublesEventKind::Restart, 3u, 1.0f },
        { 1u, DoublesEventKind::PunchBOn, 4u, 0.25f },
    }};
    left.fill(0.0f);
    right.fill(0.0f);
    engine.render(settings, shallow.data(), shallow.size(), outputs, 2u,
        static_cast<uint32_t>(left.size()));
    check(left[20u] > 0.03f && left[20u] < 0.06f,
        "punch velocity did not provide partial crossfader depth");
}

void testVarispeedAndGradualPhase()
{
    auto asset = rampAsset(1u, 16000u);
    SampleDoublesEngine engine;
    check(engine.prepare(1000.0) && engine.setAsset(&asset),
        "varispeed fixture did not prepare");
    DoublesSettings settings;
    settings.sourceTempoBpm = 120.0;
    settings.speedSemitones = -12.0;
    settings.phaseCents = 100.0;
    settings.offsetBeats = 0.0;
    settings.crossfader = 0.0;
    settings.crossfadeCurve = DoublesCrossfadeCurve::Blend;
    settings.gainDecibels = 0.0f;
    const DoublesRenderEvent restart {
        0u, DoublesEventKind::Restart, 5u, 1.0f,
    };
    std::vector<float> left(2000u, 0.0f);
    std::vector<float> right(2000u, 0.0f);
    float* outputs[] { left.data(), right.data() };
    engine.render(settings, &restart, 1u, outputs, 2u,
        static_cast<uint32_t>(left.size()));
    const float a = engine.deckAPositionNormalized();
    const float b = engine.deckBPositionNormalized();
    check(a > 0.0624f && a < 0.0626f && b > a + 0.003f,
        "coupled half-speed varispeed or independent Deck B drift failed");
}

void testLoopSyncAndPhaseSteps()
{
    auto asset = rampAsset(1u, 1000u);
    SampleDoublesEngine engine;
    check(engine.prepare(1000.0) && engine.setAsset(&asset),
        "phase-step fixture did not prepare");
    DoublesSettings settings;
    settings.sourceTempoBpm = 600.0;
    settings.speedSemitones = 0.0;
    settings.offsetBeats = 0.0;
    settings.phaseStepBeats = 0.25;
    settings.start = 0.0;
    settings.end = 0.1;
    settings.loop = true;
    settings.crossfader = 1.0;
    settings.gainDecibels = 0.0f;
    const std::array<DoublesRenderEvent, 3u> events {{
        { 0u, DoublesEventKind::Restart, 6u, 1.0f },
        { 10u, DoublesEventKind::PhaseStepForward, 7u, 1.0f },
        { 40u, DoublesEventKind::SyncDeckB, 8u, 1.0f },
    }};
    std::array<float, 60u> left {};
    std::array<float, 60u> right {};
    float* outputs[] { left.data(), right.data() };
    engine.render(settings, events.data(), events.size(), outputs, 2u,
        static_cast<uint32_t>(left.size()));
    // One beat is 100 source frames; a 1/4-beat phase step jumps 25 frames.
    check(left[9u] < 0.001f && left[12u] > 0.0035f
            && near(left[42u], 0.0042f, 0.0003f)
            && engine.deckBPositionNormalized() < 0.061f,
        "discrete phase step, sync, or independent loop wrapping failed");
}

void testContinuousCursorProgressUnderControlSweeps()
{
    auto asset = rampAsset(2u, 96000u);
    SampleDoublesEngine engine;
    check(engine.prepare(48000.0) && engine.setAsset(&asset),
        "cursor-progress fixture did not prepare");
    DoublesSettings settings;
    settings.sourceTempoBpm = 120.0;
    settings.speedSemitones = -7.0;
    settings.offsetBeats = 0.25;
    settings.loop = true;
    settings.crossfader = 0.0;
    settings.crossfadeCurve = DoublesCrossfadeCurve::Blend;
    settings.gainDecibels = -12.0f;
    const DoublesRenderEvent restart {
        0u, DoublesEventKind::Restart, 10u, 1.0f,
    };
    std::array<float, 64u> left {};
    std::array<float, 64u> right {};
    float* outputs[] { left.data(), right.data() };
    engine.render(settings, &restart, 1u, outputs, 2u,
        static_cast<uint32_t>(left.size()));

    float previousA = engine.deckAPositionNormalized();
    float previousB = engine.deckBPositionNormalized();
    bool progressed = previousA >= 0.0f && previousB >= 0.0f;
    for (uint32_t block = 0u; progressed && block < 12000u; ++block) {
        const double sweep = static_cast<double>(block % 997u) / 996.0;
        settings.speedSemitones = -24.0 + sweep * 36.0;
        settings.sourceTempoBpm = 20.0 + sweep * 979.0;
        settings.phaseCents = -100.0 + sweep * 200.0;
        settings.offsetBeats = -8.0 + sweep * 16.0;
        settings.crossfader = -1.0 + sweep * 2.0;
        engine.render(settings, nullptr, 0u, outputs, 2u,
            static_cast<uint32_t>(left.size()));
        const float currentA = engine.deckAPositionNormalized();
        const float currentB = engine.deckBPositionNormalized();
        // With looping enabled and the minimum legal speed still positive,
        // neither DSP read head may remain at the same position for a whole
        // 64-sample block while controls are changing.
        progressed = currentA >= 0.0f && currentB >= 0.0f
            && currentA != previousA && currentB != previousB;
        previousA = currentA;
        previousB = currentB;
    }
    check(progressed,
        "a DSP read head stalled during continuous live control sweeps");
}

void testIndependentDeckLevelsAndTransport()
{
    auto asset = constantAsset();
    SampleDoublesEngine engine;
    check(engine.prepare(1000.0) && engine.setAsset(&asset),
        "independent-deck fixture did not prepare");
    DoublesSettings settings;
    settings.speedSemitones = 0.0;
    settings.sourceTempoBpm = 60.0;
    settings.offsetBeats = 0.0;
    settings.crossfader = -1.0;
    settings.gainDecibels = 0.0f;
    settings.deckALevelDecibels = -6.0f;
    settings.deckBLevelDecibels = -12.0f;
    settings.linkDecks = false;
    const std::array<DoublesRenderEvent, 2u> events {{
        { 0u, DoublesEventKind::Restart, 1u, 1.0f },
        { 16u, DoublesEventKind::ToggleDeckA, 2u, 1.0f },
    }};
    std::array<float, 32u> left {};
    std::array<float, 32u> right {};
    float* outputs[] { left.data(), right.data() };
    engine.render(settings, events.data(), events.size(), outputs, 2u,
        static_cast<uint32_t>(left.size()));
    check(near(left[8u], 0.501187f, 0.002f) && left[24u] == 0.0f
            && !engine.deckAActive() && engine.deckBActive(),
        "Deck A level or unlinked pause failed");

    const DoublesRenderEvent resumeA {
        0u, DoublesEventKind::ToggleDeckA, 3u, 1.0f,
    };
    engine.render(settings, &resumeA, 1u, outputs, 2u,
        static_cast<uint32_t>(left.size()));
    check(engine.deckAActive() && engine.deckBActive()
            && left[8u] > 0.49f,
        "unlinked Deck A resume failed");

    settings.linkDecks = true;
    const DoublesRenderEvent linkedToggle {
        0u, DoublesEventKind::ToggleDeckB, 4u, 1.0f,
    };
    engine.render(settings, &linkedToggle, 1u, outputs, 2u,
        static_cast<uint32_t>(left.size()));
    check(!engine.deckAActive() && !engine.deckBActive(),
        "linked deck transport did not pause both decks");

    engine.reset();
    settings.linkDecks = false;
    settings.crossfader = 1.0;
    const DoublesRenderEvent restart {
        0u, DoublesEventKind::Restart, 5u, 1.0f,
    };
    engine.render(settings, &restart, 1u, outputs, 2u,
        static_cast<uint32_t>(left.size()));
    check(near(left[8u], 0.251189f, 0.002f),
        "Deck B independent level failed");
}

void testDragMotorAndLivePhase()
{
    auto asset = rampAsset(1u, 20000u);
    SampleDoublesEngine engine;
    check(engine.prepare(1000.0) && engine.setAsset(&asset),
        "drag/phase fixture did not prepare");
    DoublesSettings settings;
    settings.speedSemitones = 0.0;
    settings.sourceTempoBpm = 60.0;
    settings.offsetBeats = 0.0;
    settings.crossfader = -1.0;
    settings.gainDecibels = 0.0f;
    const std::array<DoublesRenderEvent, 2u> dragEvents {{
        { 0u, DoublesEventKind::Restart, 1u, 1.0f },
        { 0u, DoublesEventKind::DragAOn, 2u, 1.0f },
    }};
    std::vector<float> left(1000u, 0.0f);
    std::vector<float> right(1000u, 0.0f);
    float* outputs[] { left.data(), right.data() };
    engine.render(settings, dragEvents.data(), dragEvents.size(),
        outputs, 2u, static_cast<uint32_t>(left.size()));
    const float draggedPosition = engine.deckAPositionNormalized();
    check(engine.dragAHeld() && draggedPosition > 0.007f
            && draggedPosition < 0.014f
            && engine.deckARateScale() < 0.18,
        "momentary Drag A did not load the platter");

    const DoublesRenderEvent dragOff {
        0u, DoublesEventKind::DragAOff, 2u, 0.0f,
    };
    engine.render(settings, &dragOff, 1u, outputs, 2u,
        static_cast<uint32_t>(left.size()));
    check(!engine.dragAHeld() && engine.deckARateScale() > 0.98
            && engine.deckAPositionNormalized() > draggedPosition + 0.035f,
        "motor recovery after Drag A release failed");

    engine.reset();
    const DoublesRenderEvent restart {
        0u, DoublesEventKind::Restart, 3u, 1.0f,
    };
    std::array<float, 1u> oneLeft {};
    std::array<float, 1u> oneRight {};
    float* oneOutputs[] { oneLeft.data(), oneRight.data() };
    engine.render(settings, &restart, 1u, oneOutputs, 2u, 1u);
    settings.livePhaseBeats = 0.25;
    engine.render(settings, nullptr, 0u, oneOutputs, 2u, 1u);
    const double phaseFrames = (engine.deckBPositionNormalized()
        - engine.deckAPositionNormalized()) * asset.frameCount();
    check(std::abs(phaseFrames - 250.0) < 1.5,
        "live Deck B phase did not move by the requested source beat fraction");
}

void testTempoEstimator()
{
    for (const double bpm : { 90.0, 120.0, 160.0 }) {
        const auto asset = clickAsset(bpm, bpm == 120.0);
        const TempoEstimate estimate = estimateSampleTempo(asset);
        check(estimate.valid && std::abs(estimate.bpm - bpm) < 2.0
                && estimate.confidence > 0.55f,
            "load-time tempo estimator missed a periodic source");
    }
    SampleAsset silence;
    silence.sampleRate = 48000.0;
    silence.channelCount = 1u;
    silence.channels[0u].assign(48000u * 4u, 0.0f);
    check(!estimateSampleTempo(silence).valid,
        "tempo estimator claimed a BPM for silence");
}

void testMidiCommandMap()
{
    constexpr std::array<double, 13u> expected {{
        -4.0, -2.0, -1.0, -0.5, -0.25, -0.125, 0.0,
        0.125, 0.25, 0.5, 1.0, 2.0, 4.0,
    }};
    bool valid = true;
    for (uint8_t index = 0u; index < expected.size(); ++index) {
        double offset = 99.0;
        valid = valid && doublesOffsetForMidiNote(
            static_cast<uint8_t>(kDoublesMidiFirstOffset + index), offset)
            && std::abs(offset - expected[index]) < 1.0e-12;
    }
    double ignored = 0.0;
    valid = valid && !doublesOffsetForMidiNote(47u, ignored)
        && !doublesOffsetForMidiNote(61u, ignored);
    check(valid, "Tracker offset-note map is incomplete or unstable");
}

} // namespace

int main()
{
    testTwoDeckOffsetAndStereo();
    testTrackerPunchGateAndVelocity();
    testVarispeedAndGradualPhase();
    testLoopSyncAndPhaseSteps();
    testContinuousCursorProgressUnderControlSweeps();
    testIndependentDeckLevelsAndTransport();
    testDragMotorAndLivePhase();
    testTempoEstimator();
    testMidiCommandMap();
    if (failures != 0) return 1;
    std::cout << "s3g Sample Doubles smoke: ok\n";
    return 0;
}
