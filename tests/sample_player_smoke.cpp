#include "s3g_sample_player.h"

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <utility>

namespace {

using namespace s3g::sample;

int failures = 0;

void check(bool condition, const char* message)
{
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

bool near(float actual, float expected, float tolerance = 1.0e-5f)
{
    return std::abs(actual - expected) <= tolerance;
}

SampleAsset rampAsset(uint8_t channels = 2u, uint32_t frames = 128u)
{
    SampleAsset asset;
    asset.sampleRate = 48000.0;
    asset.channelCount = channels;
    for (uint8_t channel = 0u; channel < channels; ++channel) {
        asset.channels[channel].resize(frames);
        for (uint32_t frame = 0u; frame < frames; ++frame) {
            asset.channels[channel][frame]
                = static_cast<float>(channel + 1u) * 0.1f
                + static_cast<float>(frame) * 0.001f;
        }
    }
    return asset;
}

void testForwardStartAndLength()
{
    auto asset = rampAsset();
    SamplePlayerEngine engine;
    check(asset.valid() && engine.prepare(48000.0, 2u)
            && engine.setAsset(&asset),
        "forward fixture did not prepare");
    PlayerSettings settings;
    settings.attackProportion = 0.0f;
    settings.start = 0.25;
    settings.length = 0.25;
    settings.gainDecibels = 0.0f;
    const RenderEvent note { 2u, EventKind::NoteOn, 1u, 60u, 1.0f, 1u };
    std::array<float, 8u> left {};
    std::array<float, 8u> right {};
    float* outputs[] { left.data(), right.data() };
    engine.render(settings, &note, 1u, outputs, 2u,
        static_cast<uint32_t>(left.size()));
    check(left[0u] == 0.0f && left[1u] == 0.0f
            && near(left[2u], 0.132f)
            && near(right[2u], 0.232f)
            && near(left[7u], 0.137f),
        "Start/Length or sample-accurate forward launch failed");
}

void testSafeDefaultBounds()
{
    SampleAsset asset;
    asset.sampleRate = 1000.0;
    asset.channelCount = 2u;
    asset.channels[0u] = {
        0.8f, 0.4f, -0.2f, -0.7f, -0.4f,
        0.3f, 0.8f, 0.5f, -0.1f, 0.4f,
    };
    asset.channels[1u] = {
        0.7f, 0.3f, -0.1f, -0.6f, -0.3f,
        0.2f, 0.7f, 0.4f, -0.05f, 0.3f,
    };
    const auto bounds = defaultSafeSampleBounds(asset);
    check(bounds.validFor(asset) && bounds.startFrame == 2u
            && bounds.endFrame == 9u
            && sampleBoundaryCrossesZero(asset, bounds.startFrame)
            && sampleBoundaryCrossesZero(asset, bounds.endFrame),
        "sample defaults did not select safe zero-crossing boundaries");

    SampleAsset dc;
    dc.sampleRate = 1000.0;
    dc.channelCount = 1u;
    dc.channels[0u] = { 0.8f, 0.5f, 0.2f, 0.1f, 0.3f };
    const auto fallback = defaultSafeSampleBounds(dc);
    check(fallback.validFor(dc) && fallback.startFrame == 0u
            && fallback.endFrame == dc.frameCount(),
        "zero-crossing fallback did not preserve a valid playback window");
}

void testReverseAndPitch()
{
    auto asset = rampAsset(1u, 64u);
    SamplePlayerEngine engine;
    check(engine.prepare(48000.0, 2u) && engine.setAsset(&asset),
        "reverse fixture did not prepare");
    PlayerSettings settings;
    settings.attackProportion = 0.0f;
    settings.playMode = PlayMode::Reverse;
    settings.length = 0.5;
    settings.tuneSemitones = 12.0f;
    settings.gainDecibels = 0.0f;
    settings.pan = -1.0f;
    const RenderEvent note { 0u, EventKind::NoteOn, 2u, 60u, 1.0f, 1u };
    std::array<float, 4u> left {};
    std::array<float, 4u> right {};
    float* outputs[] { left.data(), right.data() };
    engine.render(settings, &note, 1u, outputs, 2u, 4u);
    check(near(left[0u], 0.131f) && near(left[1u], 0.129f)
            && near(left[2u], 0.127f) && right[0u] == 0.0f,
        "reverse, octave tuning, or stereo pan failed");
}

void testForwardAndReverseLoops()
{
    auto asset = rampAsset(2u, 100u);
    SamplePlayerEngine engine;
    check(engine.prepare(48000.0, 2u) && engine.setAsset(&asset),
        "loop fixture did not prepare");
    PlayerSettings settings;
    settings.attackProportion = 0.0f;
    settings.playMode = PlayMode::ForwardLoop;
    settings.loopCrossfade = 0.0;
    settings.loopStart = 0.20;
    settings.loopEnd = 0.24;
    settings.gainDecibels = 0.0f;
    const RenderEvent note { 0u, EventKind::NoteOn, 3u, 60u, 1.0f, 1u };
    std::array<float, 28u> left {};
    std::array<float, 28u> right {};
    float* outputs[] { left.data(), right.data() };
    engine.render(settings, &note, 1u, outputs, 2u, 28u);
    check(near(left[20u], 0.120f) && near(left[23u], 0.123f)
            && near(left[24u], 0.120f),
        "forward loop points did not wrap exactly");

    engine.reset();
    settings.playMode = PlayMode::ReverseLoop;
    std::array<float, 84u> reverseLeft {};
    std::array<float, 84u> reverseRight {};
    float* reverseOutputs[] { reverseLeft.data(), reverseRight.data() };
    engine.render(settings, &note, 1u, reverseOutputs, 2u, 84u);
    check(near(reverseLeft[76u], 0.123f)
            && near(reverseLeft[79u], 0.120f)
            && near(reverseLeft[80u], 0.123f),
        "reverse loop points did not wrap exactly");
}

void testLoopCrossfadeAndPingPong()
{
    SampleAsset asset;
    asset.sampleRate = 48000.0;
    asset.channelCount = 1u;
    asset.channels[0u].resize(16u);
    for (uint32_t frame = 0u; frame < 16u; ++frame)
        asset.channels[0u][frame] = static_cast<float>(frame) / 15.0f;
    SamplePlayerEngine engine;
    check(engine.prepare(48000.0, 2u) && engine.setAsset(&asset),
        "crossfade/ping-pong fixture did not prepare");
    PlayerSettings settings;
    settings.attackProportion = 0.0f;
    settings.gainDecibels = 0.0f;
    settings.playMode = PlayMode::ForwardLoop;
    settings.loopStart = 0.25;
    settings.loopEnd = 0.75;
    settings.loopCrossfade = 0.0;
    const RenderEvent note { 0u, EventKind::NoteOn, 8u, 60u, 1.0f, 1u };
    std::array<float, 14u> dryLeft {};
    std::array<float, 14u> dryRight {};
    float* dryOutputs[] { dryLeft.data(), dryRight.data() };
    engine.render(settings, &note, 1u, dryOutputs, 2u,
        static_cast<uint32_t>(dryLeft.size()));
    const float drySeam = std::abs(dryLeft[12u] - dryLeft[11u]);

    engine.reset();
    settings.loopCrossfade = 0.5;
    std::array<float, 14u> fadedLeft {};
    std::array<float, 14u> fadedRight {};
    float* fadedOutputs[] { fadedLeft.data(), fadedRight.data() };
    engine.render(settings, &note, 1u, fadedOutputs, 2u,
        static_cast<uint32_t>(fadedLeft.size()));
    const float fadedSeam = std::abs(fadedLeft[12u] - fadedLeft[11u]);
    check(drySeam > 0.4f && fadedSeam < 0.08f
            && fadedSeam < drySeam * 0.25f,
        "loop crossfade did not suppress the wrap discontinuity");

    engine.reset();
    settings.playMode = PlayMode::ReverseLoop;
    settings.loopCrossfade = 0.0;
    std::array<float, 14u> reverseDryLeft {};
    std::array<float, 14u> reverseDryRight {};
    float* reverseDryOutputs[] {
        reverseDryLeft.data(), reverseDryRight.data(),
    };
    engine.render(settings, &note, 1u, reverseDryOutputs, 2u,
        static_cast<uint32_t>(reverseDryLeft.size()));
    const float reverseDrySeam = std::abs(
        reverseDryLeft[12u] - reverseDryLeft[11u]);
    engine.reset();
    settings.loopCrossfade = 0.5;
    std::array<float, 14u> reverseFadedLeft {};
    std::array<float, 14u> reverseFadedRight {};
    float* reverseFadedOutputs[] {
        reverseFadedLeft.data(), reverseFadedRight.data(),
    };
    engine.render(settings, &note, 1u, reverseFadedOutputs, 2u,
        static_cast<uint32_t>(reverseFadedLeft.size()));
    const float reverseFadedSeam = std::abs(
        reverseFadedLeft[12u] - reverseFadedLeft[11u]);
    check(reverseDrySeam > 0.4f && reverseFadedSeam < 0.08f
            && reverseFadedSeam < reverseDrySeam * 0.25f,
        "reverse loop crossfade did not suppress the wrap discontinuity");

    engine.reset();
    settings.playMode = PlayMode::ForwardPingPong;
    settings.length = 0.375;
    settings.loopStart = 0.125;
    settings.loopEnd = 0.375;
    std::array<float, 10u> pingLeft {};
    std::array<float, 10u> pingRight {};
    float* pingOutputs[] { pingLeft.data(), pingRight.data() };
    engine.render(settings, &note, 1u, pingOutputs, 2u,
        static_cast<uint32_t>(pingLeft.size()));
    check(near(pingLeft[5u], 5.0f / 15.0f)
            && near(pingLeft[6u], 4.0f / 15.0f)
            && near(pingLeft[8u], 2.0f / 15.0f)
            && near(pingLeft[9u], 3.0f / 15.0f),
        "forward ping-pong did not reflect at both loop points");

    engine.reset();
    settings.playMode = PlayMode::ReversePingPong;
    std::array<float, 8u> reverseLeft {};
    std::array<float, 8u> reverseRight {};
    float* reverseOutputs[] { reverseLeft.data(), reverseRight.data() };
    engine.render(settings, &note, 1u, reverseOutputs, 2u,
        static_cast<uint32_t>(reverseLeft.size()));
    check(near(reverseLeft[0u], 5.0f / 15.0f)
            && near(reverseLeft[3u], 2.0f / 15.0f)
            && near(reverseLeft[4u], 3.0f / 15.0f)
            && near(reverseLeft[6u], 5.0f / 15.0f)
            && near(reverseLeft[7u], 4.0f / 15.0f),
        "reverse ping-pong did not preserve its reverse launch direction");
}

void testMultimodeFilterAndEnvelopeAmount()
{
    SampleAsset asset;
    asset.sampleRate = 48000.0;
    asset.channelCount = 1u;
    asset.channels[0u].resize(1024u);
    constexpr double pi = 3.1415926535897932384626433832795;
    for (uint32_t frame = 0u; frame < asset.frameCount(); ++frame) {
        asset.channels[0u][frame] = static_cast<float>(std::sin(
            2.0 * pi * 2000.0 * static_cast<double>(frame) / 48000.0));
    }
    SamplePlayerEngine engine;
    check(engine.prepare(48000.0, 2u) && engine.setAsset(&asset),
        "filter fixture did not prepare");
    PlayerSettings settings;
    settings.attackProportion = 0.0f;
    settings.gainDecibels = 0.0f;
    settings.filterType = FilterType::LowPass;
    settings.filterCutoffHz = 200.0f;
    settings.filterResonance = 0.35f;
    const RenderEvent note { 0u, EventKind::NoteOn, 9u, 60u, 1.0f, 1u };
    const auto renderEnergy = [&](float envelopeAmount,
                                  FilterType type) {
        engine.reset();
        settings.filterEnvelopeAmount = envelopeAmount;
        settings.filterType = type;
        std::array<float, 256u> left {};
        std::array<float, 256u> right {};
        float* outputs[] { left.data(), right.data() };
        engine.render(settings, &note, 1u, outputs, 2u,
            static_cast<uint32_t>(left.size()));
        double energy = 0.0;
        bool finite = true;
        for (std::size_t frame = 128u; frame < left.size(); ++frame) {
            finite = finite && std::isfinite(left[frame]);
            energy += static_cast<double>(left[frame]) * left[frame];
        }
        return std::pair<double, bool> { energy, finite };
    };
    const auto closed = renderEnergy(0.0f, FilterType::LowPass);
    const auto opened = renderEnergy(1.0f, FilterType::LowPass);
    const auto high = renderEnergy(0.0f, FilterType::HighPass);
    check(closed.second && opened.second && high.second
            && opened.first > closed.first * 20.0
            && high.first > closed.first * 20.0,
        "multimode filter or bipolar envelope modulation was ineffective");
}

void testStretchPitchPreservesDuration()
{
    SampleAsset asset;
    asset.sampleRate = 48000.0;
    asset.channelCount = 1u;
    asset.channels[0u].resize(9600u);
    constexpr double pi = 3.1415926535897932384626433832795;
    for (uint32_t frame = 0u; frame < asset.frameCount(); ++frame) {
        asset.channels[0u][frame] = static_cast<float>(std::sin(
            2.0 * pi * 220.0 * static_cast<double>(frame) / 48000.0));
    }
    SamplePlayerEngine engine;
    check(engine.prepare(48000.0, 2u) && engine.setAsset(&asset),
        "stretch fixture did not prepare");
    PlayerSettings settings;
    settings.attackProportion = 0.0f;
    settings.decayProportion = 0.0f;
    settings.releaseProportion = 0.0f;
    settings.gainDecibels = 0.0f;
    settings.tuneSemitones = 12.0f;
    const RenderEvent note { 0u, EventKind::NoteOn, 10u, 60u, 1.0f, 1u };

    std::array<float, 5000u> rateLeft {};
    std::array<float, 5000u> rateRight {};
    float* rateOutputs[] { rateLeft.data(), rateRight.data() };
    engine.render(settings, &note, 1u, rateOutputs, 2u,
        static_cast<uint32_t>(rateLeft.size()));
    check(engine.activeVoiceCount() == 0u,
        "Rate mode did not shorten octave-up playback");

    engine.reset();
    settings.pitchMode = PitchMode::Stretch;
    std::array<float, 5000u> stretchLeft {};
    std::array<float, 5000u> stretchRight {};
    float* stretchOutputs[] { stretchLeft.data(), stretchRight.data() };
    engine.render(settings, &note, 1u, stretchOutputs, 2u,
        static_cast<uint32_t>(stretchLeft.size()));
    const bool activeAtRateEnding = engine.activeVoiceCount() == 1u;
    uint32_t upwardCrossings = 0u;
    for (std::size_t frame = 2001u; frame < stretchLeft.size(); ++frame) {
        if (stretchLeft[frame - 1u] <= 0.0f
            && stretchLeft[frame] > 0.0f) ++upwardCrossings;
    }
    const double measuredHz = static_cast<double>(upwardCrossings)
        * 48000.0 / static_cast<double>(stretchLeft.size() - 2001u);
    std::array<float, 5000u> tailLeft {};
    std::array<float, 5000u> tailRight {};
    float* tailOutputs[] { tailLeft.data(), tailRight.data() };
    engine.render(settings, nullptr, 0u, tailOutputs, 2u,
        static_cast<uint32_t>(tailLeft.size()));
    check(activeAtRateEnding && engine.activeVoiceCount() == 0u
            && measuredHz > 390.0 && measuredHz < 490.0,
        "Stretch mode did not preserve duration while shifting pitch");
}

void testRateBelowStretchAbovePitchMode()
{
    auto asset = rampAsset(1u, 1000u);
    SamplePlayerEngine engine;
    check(engine.prepare(48000.0, 2u) && engine.setAsset(&asset),
        "split pitch-mode fixture did not prepare");
    PlayerSettings settings;
    settings.pitchMode = PitchMode::RateBelowStretchAbove;
    settings.rootNote = 60u;
    settings.tuneSemitones = 24.0f;
    settings.attackProportion = 0.0f;
    settings.gainDecibels = 0.0f;
    const std::array<RenderEvent, 3u> notes {{
        { 0u, EventKind::NoteOn, 20u, 48u, 1.0f, 1u },
        { 0u, EventKind::NoteOn, 21u, 60u, 1.0f, 1u },
        { 0u, EventKind::NoteOn, 22u, 72u, 1.0f, 1u },
    }};
    std::array<float, 4u> left {};
    std::array<float, 4u> right {};
    float* outputs[] { left.data(), right.data() };
    engine.render(settings, notes.data(), notes.size(), outputs, 2u,
        static_cast<uint32_t>(left.size()));
    const auto& cursors = engine.voiceCursors();
    check(engine.voiceCursorCount() == 3u
            && cursors[0u].key == 48u
            && near(cursors[0u].sourcePositionNormalized, 0.006f)
            && cursors[1u].key == 60u
            && near(cursors[1u].sourcePositionNormalized, 0.003f)
            && cursors[2u].key == 72u
            && near(cursors[2u].sourcePositionNormalized, 0.003f),
        "split pitch mode did not use Rate below Root and Stretch at/above it");

    engine.reset();
    settings.voiceMode = VoiceMode::Legato;
    const std::array<RenderEvent, 2u> legatoNotes {{
        { 0u, EventKind::NoteOn, 23u, 48u, 1.0f, 1u },
        { 4u, EventKind::NoteOn, 24u, 72u, 1.0f, 1u },
    }};
    std::array<float, 8u> legatoLeft {};
    std::array<float, 8u> legatoRight {};
    float* legatoOutputs[] { legatoLeft.data(), legatoRight.data() };
    engine.render(settings, legatoNotes.data(), legatoNotes.size(),
        legatoOutputs, 2u, static_cast<uint32_t>(legatoLeft.size()));
    check(engine.voiceCursorCount() == 1u
            && engine.voiceCursors()[0u].key == 72u
            && near(engine.voiceCursors()[0u].sourcePositionNormalized,
                0.011f),
        "Legato voice did not switch algorithms across the Root boundary");
}

void testAdsrAndRelease()
{
    auto asset = rampAsset(2u, 4096u);
    asset.sampleRate = 1000.0;
    SamplePlayerEngine engine;
    check(engine.prepare(1000.0, 2u) && engine.setAsset(&asset),
        "ADSR fixture did not prepare");
    PlayerSettings settings;
    settings.playMode = PlayMode::ForwardLoop;
    settings.length = 0.004;
    settings.attackProportion = 0.25f;
    settings.decayProportion = 0.25f;
    settings.sustain = 0.5f;
    settings.releaseProportion = 0.25f;
    settings.gainDecibels = 0.0f;
    const std::array<RenderEvent, 2u> events {{
        { 0u, EventKind::NoteOn, 4u, 60u, 1.0f, 1u },
        { 12u, EventKind::NoteOff, 4u, 60u, 0.0f, 1u },
    }};
    std::array<float, 18u> left {};
    std::array<float, 18u> right {};
    float* outputs[] { left.data(), right.data() };
    engine.render(settings, events.data(), events.size(), outputs, 2u,
        static_cast<uint32_t>(left.size()));
    check(left[0u] == 0.0f && left[4u] > left[3u]
            && left[8u] < left[4u] && left[11u] > 0.0f
            && left[13u] < left[12u] && left[16u] == 0.0f
            && engine.activeVoiceCount() == 0u,
        "proportional ADSR stages or loop note-off release failed");
}

void testOneShotTailReleaseIgnoresNoteOff()
{
    auto asset = rampAsset(1u, 100u);
    asset.sampleRate = 1000.0;
    SamplePlayerEngine engine;
    check(engine.prepare(1000.0, 2u) && engine.setAsset(&asset),
        "one-shot release fixture did not prepare");
    PlayerSettings settings;
    settings.attackProportion = 0.0f;
    settings.releaseProportion = 0.2f;
    settings.gainDecibels = 0.0f;
    const std::array<RenderEvent, 2u> events {{
        { 0u, EventKind::NoteOn, 6u, 60u, 1.0f, 1u },
        { 5u, EventKind::NoteOff, 6u, 60u, 0.0f, 1u },
    }};
    std::array<float, 100u> left {};
    std::array<float, 100u> right {};
    float* outputs[] { left.data(), right.data() };
    engine.render(settings, events.data(), events.size(), outputs, 2u,
        static_cast<uint32_t>(left.size()));
    check(left[5u] > 0.0f && near(left[80u], 0.18f)
            && left[90u] < left[80u] && left[99u] == 0.0f
            && engine.activeVoiceCount() == 0u,
        "one-shot note-off or end-aligned proportional release failed");
}

void testTempoSyncRateAndStretch()
{
    auto asset = rampAsset(1u, 200u);
    SamplePlayerEngine engine;
    check(engine.prepare(48000.0, 2u) && engine.setAsset(&asset),
        "tempo-sync fixture did not prepare");
    PlayerSettings settings;
    settings.attackProportion = 0.0f;
    settings.releaseProportion = 0.0f;
    settings.gainDecibels = 0.0f;
    settings.syncMode = SyncMode::Host;
    settings.sourceTempoBpm = 120.0;
    settings.hostTempoBpm = 240.0;
    const RenderEvent note { 0u, EventKind::NoteOn, 20u, 60u, 1.0f, 1u };
    std::array<float, 4u> rateLeft {};
    std::array<float, 4u> rateRight {};
    float* rateOutputs[] { rateLeft.data(), rateRight.data() };
    engine.render(settings, &note, 1u, rateOutputs, 2u, 4u);
    check(near(rateLeft[0u], 0.1f) && near(rateLeft[1u], 0.102f)
            && near(rateLeft[3u], 0.106f),
        "Rate host sync did not scale playback speed and pitch together");

    engine.reset();
    settings.pitchMode = PitchMode::Stretch;
    std::array<float, 4u> stretchLeft {};
    std::array<float, 4u> stretchRight {};
    float* stretchOutputs[] { stretchLeft.data(), stretchRight.data() };
    engine.render(settings, &note, 1u, stretchOutputs, 2u, 4u);
    const auto& cursor = engine.voiceCursors()[0u];
    check(engine.voiceCursorCount() == 1u
            && near(cursor.sourcePositionNormalized, 0.03f, 0.002f),
        "Stretch host sync did not change transport duration independently");

    engine.reset();
    settings.syncMode = SyncMode::Free;
    std::array<float, 4u> freeLeft {};
    std::array<float, 4u> freeRight {};
    float* freeOutputs[] { freeLeft.data(), freeRight.data() };
    engine.render(settings, &note, 1u, freeOutputs, 2u, 4u);
    check(engine.voiceCursorCount() == 1u
            && near(engine.voiceCursors()[0u].sourcePositionNormalized,
                0.015f, 0.002f),
        "Free sync default did not preserve the original transport speed");

    SampleAsset tone;
    tone.sampleRate = 48000.0;
    tone.channelCount = 1u;
    tone.channels[0u].resize(24000u);
    constexpr double pi = 3.1415926535897932384626433832795;
    for (uint32_t frame = 0u; frame < tone.frameCount(); ++frame) {
        tone.channels[0u][frame] = static_cast<float>(std::sin(
            2.0 * pi * 220.0 * static_cast<double>(frame) / 48000.0));
    }
    engine.reset();
    check(engine.setAsset(&tone), "tempo-sync tone did not load");
    settings.syncMode = SyncMode::Host;
    settings.pitchMode = PitchMode::Stretch;
    std::array<float, 5000u> toneLeft {};
    std::array<float, 5000u> toneRight {};
    float* toneOutputs[] { toneLeft.data(), toneRight.data() };
    engine.render(settings, &note, 1u, toneOutputs, 2u,
        static_cast<uint32_t>(toneLeft.size()));
    uint32_t crossings = 0u;
    for (std::size_t frame = 2001u; frame < toneLeft.size(); ++frame) {
        if (toneLeft[frame - 1u] <= 0.0f && toneLeft[frame] > 0.0f)
            ++crossings;
    }
    const double measuredHz = static_cast<double>(crossings) * 48000.0
        / static_cast<double>(toneLeft.size() - 2001u);
    check(measuredHz > 190.0 && measuredHz < 250.0,
        "Stretch host sync changed pitch while changing duration");
}

void testTriggerAndRetriggerModes()
{
    auto asset = rampAsset(1u, 200u);
    SamplePlayerEngine engine;
    check(engine.prepare(48000.0, 2u) && engine.setAsset(&asset),
        "trigger fixture did not prepare");
    PlayerSettings settings;
    settings.attackProportion = 0.0f;
    settings.releaseProportion = 0.0f;
    settings.gainDecibels = 0.0f;
    settings.triggerMode = TriggerMode::Gate;
    const std::array<RenderEvent, 2u> gated {{
        { 0u, EventKind::NoteOn, 21u, 60u, 1.0f, 1u },
        { 4u, EventKind::NoteOff, 21u, 60u, 0.0f, 1u },
    }};
    std::array<float, 8u> gateLeft {};
    std::array<float, 8u> gateRight {};
    float* gateOutputs[] { gateLeft.data(), gateRight.data() };
    engine.render(settings, gated.data(), gated.size(), gateOutputs, 2u, 8u);
    check(gateLeft[3u] > 0.0f && gateLeft[4u] == 0.0f
            && engine.activeVoiceCount() == 0u,
        "Gate trigger did not stop a non-looping voice on note-off");

    const std::array<RenderEvent, 2u> repeated {{
        { 0u, EventKind::NoteOn, 22u, 60u, 1.0f, 1u },
        { 4u, EventKind::NoteOn, 23u, 60u, 1.0f, 1u },
    }};
    engine.reset();
    settings.triggerMode = TriggerMode::Auto;
    settings.retriggerMode = RetriggerMode::Layer;
    std::array<float, 8u> layerLeft {};
    std::array<float, 8u> layerRight {};
    float* layerOutputs[] { layerLeft.data(), layerRight.data() };
    engine.render(settings, repeated.data(), repeated.size(), layerOutputs,
        2u, 8u);
    check(engine.activeVoiceCount() == 2u,
        "Layer retrigger default did not preserve polyphonic repeats");

    engine.reset();
    settings.retriggerMode = RetriggerMode::Restart;
    std::array<float, 8u> restartLeft {};
    std::array<float, 8u> restartRight {};
    float* restartOutputs[] { restartLeft.data(), restartRight.data() };
    engine.render(settings, repeated.data(), repeated.size(), restartOutputs,
        2u, 8u);
    check(engine.activeVoiceCount() == 1u
            && near(engine.voiceCursors()[0u].sourcePositionNormalized,
                0.015f, 0.002f),
        "Restart retrigger did not replace the matching-key voice");

    engine.reset();
    settings.retriggerMode = RetriggerMode::Ignore;
    std::array<float, 8u> ignoreLeft {};
    std::array<float, 8u> ignoreRight {};
    float* ignoreOutputs[] { ignoreLeft.data(), ignoreRight.data() };
    engine.render(settings, repeated.data(), repeated.size(), ignoreOutputs,
        2u, 8u);
    check(engine.activeVoiceCount() == 1u
            && near(engine.voiceCursors()[0u].sourcePositionNormalized,
                0.035f, 0.002f),
        "Ignore retrigger interrupted the existing matching-key voice");

    engine.reset();
    settings.retriggerMode = RetriggerMode::Layer;
    settings.triggerMode = TriggerMode::Toggle;
    std::array<float, 8u> toggleLeft {};
    std::array<float, 8u> toggleRight {};
    float* toggleOutputs[] { toggleLeft.data(), toggleRight.data() };
    engine.render(settings, repeated.data(), repeated.size(), toggleOutputs,
        2u, 8u);
    check(toggleLeft[3u] > 0.0f && toggleLeft[4u] == 0.0f
            && engine.activeVoiceCount() == 0u,
        "Toggle trigger did not stop the matching-key voice");
}

void testMonoLegatoAndGlide()
{
    auto asset = rampAsset(1u, 1000u);
    asset.sampleRate = 1000.0;
    const std::array<RenderEvent, 2u> notes {{
        { 0u, EventKind::NoteOn, 24u, 60u, 1.0f, 1u },
        { 10u, EventKind::NoteOn, 25u, 72u, 1.0f, 1u },
    }};
    PlayerSettings settings;
    settings.attackProportion = 0.0f;
    settings.releaseProportion = 0.0f;
    settings.gainDecibels = 0.0f;
    settings.voiceMode = VoiceMode::Mono;
    SamplePlayerEngine mono;
    check(mono.prepare(1000.0, 2u) && mono.setAsset(&asset),
        "mono/legato fixture did not prepare");
    std::array<float, 20u> monoLeft {};
    std::array<float, 20u> monoRight {};
    float* monoOutputs[] { monoLeft.data(), monoRight.data() };
    mono.render(settings, notes.data(), notes.size(), monoOutputs, 2u, 20u);
    check(mono.activeVoiceCount() == 1u
            && mono.voiceCursors()[0u].key == 72u
            && mono.voiceCursors()[0u].sourcePositionNormalized < 0.020f,
        "Mono mode did not restart as a single voice");

    settings.voiceMode = VoiceMode::Legato;
    settings.glideSeconds = 0.010;
    SamplePlayerEngine glide;
    check(glide.prepare(1000.0, 2u) && glide.setAsset(&asset),
        "legato glide fixture did not prepare");
    std::array<float, 20u> glideLeft {};
    std::array<float, 20u> glideRight {};
    float* glideOutputs[] { glideLeft.data(), glideRight.data() };
    glide.render(settings, notes.data(), notes.size(), glideOutputs, 2u, 20u);
    const float glidedPosition = glide.voiceCursors()[0u]
        .sourcePositionNormalized;

    settings.glideSeconds = 0.0;
    SamplePlayerEngine instant;
    check(instant.prepare(1000.0, 2u) && instant.setAsset(&asset),
        "instant legato fixture did not prepare");
    std::array<float, 20u> instantLeft {};
    std::array<float, 20u> instantRight {};
    float* instantOutputs[] { instantLeft.data(), instantRight.data() };
    instant.render(settings, notes.data(), notes.size(), instantOutputs, 2u,
        20u);
    const float instantPosition = instant.voiceCursors()[0u]
        .sourcePositionNormalized;
    check(glide.activeVoiceCount() == 1u
            && glide.voiceCursors()[0u].key == 72u
            && glidedPosition > 0.020f
            && instantPosition > glidedPosition + 0.002f,
        "Legato did not preserve the playhead or apply Glide");
}

void testSixteenChannelLock()
{
    auto asset = rampAsset(16u, 64u);
    SamplePlayerEngine engine;
    check(engine.prepare(48000.0, 16u) && engine.setAsset(&asset),
        "16-channel fixture did not prepare");
    PlayerSettings settings;
    settings.attackProportion = 0.0f;
    settings.gainDecibels = 0.0f;
    const RenderEvent note { 0u, EventKind::NoteOn, 5u, 60u, 1.0f, 1u };
    std::array<std::array<float, 4u>, 16u> storage {};
    std::array<float*, 16u> outputs {};
    for (std::size_t channel = 0u; channel < outputs.size(); ++channel)
        outputs[channel] = storage[channel].data();
    engine.render(settings, &note, 1u, outputs.data(), 16u, 4u);
    bool locked = true;
    for (uint32_t channel = 0u; channel < 16u; ++channel) {
        locked = locked && near(storage[channel][0u],
            static_cast<float>(channel + 1u) * 0.1f);
    }
    check(locked, "16-channel playback did not preserve sample lanes");

    bool narrowerWidthsPreserved = true;
    for (uint8_t sourceChannels = 1u; sourceChannels <= 16u;
         ++sourceChannels) {
        auto narrower = rampAsset(sourceChannels, 64u);
        engine.reset();
        narrowerWidthsPreserved = narrowerWidthsPreserved
            && engine.setAsset(&narrower);
        std::array<std::array<float, 1u>, 16u> narrowStorage {};
        std::array<float*, 16u> narrowOutputs {};
        for (std::size_t channel = 0u; channel < narrowOutputs.size();
             ++channel)
            narrowOutputs[channel] = narrowStorage[channel].data();
        engine.render(settings, &note, 1u, narrowOutputs.data(), 16u, 1u);
        for (uint32_t channel = 0u; channel < 16u; ++channel) {
            const float expected = channel < sourceChannels
                ? static_cast<float>(channel + 1u) * 0.1f : 0.0f;
            narrowerWidthsPreserved = narrowerWidthsPreserved
                && near(narrowStorage[channel][0u], expected);
        }
    }
    check(narrowerWidthsPreserved,
        "16-channel playback did not accept and preserve every 1-16 "
        "channel source width");

    SampleAsset stereo;
    stereo.sampleRate = 48000.0;
    stereo.channelCount = 2u;
    stereo.channels[0u].assign(8u, 0.25f);
    stereo.channels[1u].assign(8u, 0.50f);
    engine.reset();
    check(engine.setAsset(&stereo),
        "16-channel stereo preservation fixture did not load");
    settings.pan = -1.0f;
    std::array<std::array<float, 1u>, 16u> preservedStorage {};
    std::array<float*, 16u> preservedOutputs {};
    for (std::size_t channel = 0u; channel < preservedOutputs.size();
         ++channel)
        preservedOutputs[channel] = preservedStorage[channel].data();
    engine.render(settings, &note, 1u, preservedOutputs.data(), 16u, 1u);
    const bool relationshipsPreserved = preservedStorage[0u][0u] > 0.0f
            && near(preservedStorage[1u][0u],
                preservedStorage[0u][0u] * 2.0f)
            && preservedStorage[2u][0u] == 0.0f;
    if (!relationshipsPreserved) {
        std::cerr << "16-channel preserved lanes: "
            << preservedStorage[0u][0u] << ", "
            << preservedStorage[1u][0u] << ", "
            << preservedStorage[2u][0u] << '\n';
    }
    check(relationshipsPreserved,
        "16-channel playback applied stereo Pan or filled an unused lane");
}

void testPostMixOutputGainAndPan()
{
    SampleAsset asset;
    asset.sampleRate = 48000.0;
    asset.channelCount = 1u;
    asset.channels[0u].assign(64u, 0.5f);
    SamplePlayerEngine engine;
    check(engine.prepare(48000.0, 2u) && engine.setAsset(&asset),
        "post-mix output fixture did not prepare");
    PlayerSettings settings;
    settings.attackProportion = 0.0f;
    settings.playMode = PlayMode::ForwardLoop;
    settings.loopCrossfade = 0.0;
    settings.gainDecibels = 0.0f;
    const RenderEvent note {
        0u, EventKind::NoteOn, 14u, 60u, 1.0f, 1u,
    };
    std::array<float, 4u> left {};
    std::array<float, 4u> right {};
    float* outputs[] { left.data(), right.data() };
    engine.render(settings, &note, 1u, outputs, 2u, 4u);
    const bool centered = near(left[0u], 0.5f)
        && near(right[0u], 0.5f);

    settings.gainDecibels = -6.020599913f;
    settings.pan = -1.0f;
    engine.render(settings, nullptr, 0u, outputs, 2u, 4u);
    const bool liveLeft = engine.activeVoiceCount() == 1u
        && near(left[0u], 0.25f, 1.0e-4f) && right[0u] == 0.0f;

    settings.gainDecibels = 0.0f;
    settings.pan = 1.0f;
    engine.render(settings, nullptr, 0u, outputs, 2u, 4u);
    const bool liveRight = engine.activeVoiceCount() == 1u
        && left[0u] == 0.0f && near(right[0u], 0.5f);
    check(centered && liveLeft && liveRight,
        "Out or stereo Pan remained captured by the initial note");
}

void testLiveTuneAndFine()
{
    auto asset = rampAsset(1u, 5000u);
    SamplePlayerEngine engine;
    check(engine.prepare(48000.0, 2u) && engine.setAsset(&asset),
        "live Tune/Fine fixture did not prepare");
    PlayerSettings settings;
    settings.attackProportion = 0.0f;
    settings.gainDecibels = 0.0f;
    settings.playMode = PlayMode::ForwardLoop;
    settings.loopCrossfade = 0.0;
    const RenderEvent note {
        0u, EventKind::NoteOn, 15u, 60u, 1.0f, 1u,
    };
    std::array<float, 4u> initialLeft {};
    std::array<float, 4u> initialRight {};
    float* initialOutputs[] { initialLeft.data(), initialRight.data() };
    engine.render(settings, &note, 1u, initialOutputs, 2u, 4u);

    settings.tuneSemitones = 12.0f;
    settings.fineTuneCents = 100.0f;
    settings.rootNote = 72u;
    std::array<float, 3u> transitionLeft {};
    std::array<float, 3u> transitionRight {};
    float* transitionOutputs[] {
        transitionLeft.data(), transitionRight.data(),
    };
    engine.render(settings, nullptr, 0u, transitionOutputs, 2u, 3u);
    const float transitionStep = transitionLeft[2u]
        - transitionLeft[1u];

    std::array<float, 600u> settlingLeft {};
    std::array<float, 600u> settlingRight {};
    float* settlingOutputs[] {
        settlingLeft.data(), settlingRight.data(),
    };
    engine.render(settings, nullptr, 0u, settlingOutputs, 2u, 600u);
    std::array<float, 3u> steadyLeft {};
    std::array<float, 3u> steadyRight {};
    float* steadyOutputs[] { steadyLeft.data(), steadyRight.data() };
    engine.render(settings, nullptr, 0u, steadyOutputs, 2u, 3u);
    const float steadyStep = steadyLeft[1u] - steadyLeft[0u];
    const float expectedStep = static_cast<float>(
        0.001 * std::pow(2.0, 13.0 / 12.0));
    check(engine.activeVoiceCount() == 1u
            && transitionStep > 0.001f
            && transitionStep < expectedStep
            && near(steadyStep, expectedStep, 2.0e-5f),
        "Tune/Fine did not smoothly retarget an active voice");
}

void testLiveSustainAndRelease()
{
    SampleAsset asset;
    asset.sampleRate = 1000.0;
    asset.channelCount = 1u;
    asset.channels[0u].assign(1000u, 0.8f);
    SamplePlayerEngine engine;
    check(engine.prepare(1000.0, 2u) && engine.setAsset(&asset),
        "live Sustain/Release fixture did not prepare");
    PlayerSettings settings;
    settings.attackProportion = 0.0f;
    settings.releaseProportion = 0.0f;
    settings.sustain = 1.0f;
    settings.gainDecibels = 0.0f;
    settings.playMode = PlayMode::ForwardLoop;
    settings.triggerMode = TriggerMode::Gate;
    settings.loopCrossfade = 0.0;
    const RenderEvent note {
        0u, EventKind::NoteOn, 16u, 60u, 1.0f, 1u,
    };
    std::array<float, 20u> initialLeft {};
    std::array<float, 20u> initialRight {};
    float* initialOutputs[] { initialLeft.data(), initialRight.data() };
    engine.render(settings, &note, 1u, initialOutputs, 2u, 20u);

    settings.sustain = 0.25f;
    settings.releaseProportion = 0.10f;
    std::array<float, 20u> sustainLeft {};
    std::array<float, 20u> sustainRight {};
    float* sustainOutputs[] { sustainLeft.data(), sustainRight.data() };
    engine.render(settings, nullptr, 0u, sustainOutputs, 2u, 20u);
    const bool sustainUpdated = sustainLeft[0u] > sustainLeft[19u]
        && near(sustainLeft[19u], 0.2f, 1.0e-4f);

    const RenderEvent noteOff {
        0u, EventKind::NoteOff, 16u, 60u, 0.0f, 1u,
    };
    std::array<float, 50u> releaseLeft {};
    std::array<float, 50u> releaseRight {};
    float* releaseOutputs[] { releaseLeft.data(), releaseRight.data() };
    engine.render(settings, &noteOff, 1u, releaseOutputs, 2u, 50u);
    const bool releaseUpdated = engine.activeVoiceCount() == 1u
        && releaseLeft[0u] > releaseLeft[49u]
        && releaseLeft[49u] > 0.0f;
    std::array<float, 60u> tailLeft {};
    std::array<float, 60u> tailRight {};
    float* tailOutputs[] { tailLeft.data(), tailRight.data() };
    engine.render(settings, nullptr, 0u, tailOutputs, 2u, 60u);
    check(sustainUpdated && releaseUpdated
            && engine.activeVoiceCount() == 0u,
        "Sustain/Release did not update a held voice before note-off");
}

void testLiveLoopEditing()
{
    auto asset = rampAsset(1u, 100u);
    asset.sampleRate = 1000.0;
    SamplePlayerEngine engine;
    check(engine.prepare(1000.0, 2u) && engine.setAsset(&asset),
        "live loop editing fixture did not prepare");
    PlayerSettings settings;
    settings.attackProportion = 0.0f;
    settings.gainDecibels = 0.0f;
    settings.playMode = PlayMode::ForwardLoop;
    settings.loopStart = 0.20;
    settings.loopEnd = 0.40;
    settings.loopCrossfade = 0.0;
    const RenderEvent note {
        0u, EventKind::NoteOn, 17u, 60u, 1.0f, 1u,
    };
    std::array<float, 45u> initialLeft {};
    std::array<float, 45u> initialRight {};
    float* initialOutputs[] { initialLeft.data(), initialRight.data() };
    engine.render(settings, &note, 1u, initialOutputs, 2u, 45u);

    settings.loopStart = 0.60;
    settings.loopEnd = 0.80;
    std::array<float, 1u> movedLeft {};
    std::array<float, 1u> movedRight {};
    float* movedOutputs[] { movedLeft.data(), movedRight.data() };
    engine.render(settings, nullptr, 0u, movedOutputs, 2u, 1u);
    const bool pointsMovedSafely = engine.voiceCursorCount() == 1u
        && near(engine.voiceCursors()[0u].sourcePositionNormalized, 0.65f)
        && near(movedLeft[0u], initialLeft[44u]);

    std::array<float, 40u> settledLeft {};
    std::array<float, 40u> settledRight {};
    float* settledOutputs[] { settledLeft.data(), settledRight.data() };
    engine.render(settings, nullptr, 0u, settledOutputs, 2u, 40u);
    settings.loopCrossfade = 0.50;
    std::array<float, 40u> crossfadedLeft {};
    std::array<float, 40u> crossfadedRight {};
    float* crossfadedOutputs[] {
        crossfadedLeft.data(), crossfadedRight.data(),
    };
    engine.render(settings, nullptr, 0u, crossfadedOutputs, 2u, 40u);
    const float settledMinimum = *std::min_element(
        crossfadedLeft.begin() + 16u, crossfadedLeft.end());
    const bool crossfadeMovedSafely
        = near(crossfadedLeft[0u], settledLeft[39u])
        && settledMinimum > 0.168f;
    check(pointsMovedSafely && crossfadeMovedSafely
            && engine.activeVoiceCount() == 1u,
        "active loop points/crossfade did not transition click-safely");
}

void testKillAllPlayback()
{
    auto asset = rampAsset(1u, 512u);
    SamplePlayerEngine engine;
    check(engine.prepare(48000.0, 2u) && engine.setAsset(&asset),
        "kill-all fixture did not prepare");
    PlayerSettings settings;
    settings.attackProportion = 0.0f;
    settings.playMode = PlayMode::ForwardLoop;
    settings.loopStart = 0.10;
    settings.loopEnd = 0.20;
    settings.gainDecibels = 0.0f;
    const RenderEvent firstNote {
        0u, EventKind::NoteOn, 18u, 60u, 1.0f, 1u,
    };
    std::array<float, 64u> playingLeft {};
    std::array<float, 64u> playingRight {};
    float* playingOutputs[] { playingLeft.data(), playingRight.data() };
    engine.render(settings, &firstNote, 1u, playingOutputs, 2u,
        static_cast<uint32_t>(playingLeft.size()));
    const bool wasPlaying = engine.activeVoiceCount() == 1u
        && engine.voiceCursorCount() == 1u && playingLeft.back() != 0.0f;

    engine.killAll();
    std::array<float, 32u> silentLeft {};
    std::array<float, 32u> silentRight {};
    silentLeft.fill(1.0f);
    silentRight.fill(1.0f);
    float* silentOutputs[] { silentLeft.data(), silentRight.data() };
    engine.render(settings, nullptr, 0u, silentOutputs, 2u,
        static_cast<uint32_t>(silentLeft.size()));
    bool silent = engine.activeVoiceCount() == 0u
        && engine.voiceCursorCount() == 0u;
    for (std::size_t frame = 0u; frame < silentLeft.size(); ++frame)
        silent = silent && silentLeft[frame] == 0.0f
            && silentRight[frame] == 0.0f;

    const RenderEvent secondNote {
        0u, EventKind::NoteOn, 19u, 67u, 1.0f, 1u,
    };
    std::array<float, 8u> restartedLeft {};
    std::array<float, 8u> restartedRight {};
    float* restartedOutputs[] {
        restartedLeft.data(), restartedRight.data(),
    };
    engine.render(settings, &secondNote, 1u, restartedOutputs, 2u,
        static_cast<uint32_t>(restartedLeft.size()));
    check(wasPlaying && silent && engine.activeVoiceCount() == 1u
            && restartedLeft.back() != 0.0f,
        "kill all did not silence voices or preserve fresh retriggering");
}

void testPolyphonicVoiceCursors()
{
    auto asset = rampAsset(1u, 1000u);
    SamplePlayerEngine engine;
    check(engine.prepare(48000.0, 2u) && engine.setAsset(&asset),
        "polyphonic cursor fixture did not prepare");
    PlayerSettings settings;
    settings.attackProportion = 0.0f;
    settings.gainDecibels = 0.0f;
    const std::array<RenderEvent, 2u> events {{
        { 0u, EventKind::NoteOn, 11u, 60u, 1.0f, 1u },
        { 5u, EventKind::NoteOn, 12u, 67u, 1.0f, 1u },
    }};
    std::array<float, 10u> left {};
    std::array<float, 10u> right {};
    float* outputs[] { left.data(), right.data() };
    engine.render(settings, events.data(), events.size(), outputs, 2u,
        static_cast<uint32_t>(left.size()));
    const auto& cursors = engine.voiceCursors();
    const bool cursorsIndependent = engine.voiceCursorCount() == 2u
            && cursors[0u].key == 60u && cursors[1u].key == 67u
            && near(cursors[0u].sourcePositionNormalized, 0.009f)
            && near(cursors[1u].sourcePositionNormalized,
                static_cast<float>(4.0 * std::pow(2.0, 7.0 / 12.0)
                    / 1000.0));
    if (!cursorsIndependent) {
        std::cerr << "cursor count=" << engine.voiceCursorCount()
            << " first=" << static_cast<unsigned>(cursors[0u].key)
            << '@' << cursors[0u].sourcePositionNormalized
            << " second=" << static_cast<unsigned>(cursors[1u].key)
            << '@' << cursors[1u].sourcePositionNormalized << '\n';
    }
    check(cursorsIndependent,
        "polyphonic voices collapsed into one averaged cursor");
}

void testPreparedAssetIsNotRescannedInRender()
{
    auto asset = rampAsset(8u, 4096u);
    SamplePlayerEngine engine;
    check(engine.prepare(48000.0, 16u) && engine.setAsset(&asset),
        "prepared-asset fixture did not validate");

    // A hosted asset is immutable after validation. Place a sentinel outside
    // the rendered range only after setAsset() so this test can distinguish
    // ordinary sample reads from an accidental full-file validation pass on
    // the audio thread.
    asset.channels[7u].back() = std::numeric_limits<float>::quiet_NaN();
    PlayerSettings settings;
    settings.attackProportion = 0.0f;
    settings.gainDecibels = 0.0f;
    const RenderEvent note {
        0u, EventKind::NoteOn, 20u, 60u, 1.0f, 1u,
    };
    std::array<std::array<float, 8u>, 16u> rendered {};
    std::array<float*, 16u> outputs {};
    for (std::size_t channel = 0u; channel < outputs.size(); ++channel)
        outputs[channel] = rendered[channel].data();
    engine.render(settings, &note, 1u, outputs.data(),
        static_cast<uint32_t>(outputs.size()),
        static_cast<uint32_t>(rendered[0u].size()));
    check(near(rendered[0u][0u], 0.1f)
            && near(rendered[7u][0u], 0.8f)
            && rendered[8u][0u] == 0.0f,
        "render or note-on rescanned the complete prepared sample asset");
}

} // namespace

int main()
{
    testSafeDefaultBounds();
    testForwardStartAndLength();
    testReverseAndPitch();
    testForwardAndReverseLoops();
    testLoopCrossfadeAndPingPong();
    testMultimodeFilterAndEnvelopeAmount();
    testStretchPitchPreservesDuration();
    testRateBelowStretchAbovePitchMode();
    testAdsrAndRelease();
    testOneShotTailReleaseIgnoresNoteOff();
    testTempoSyncRateAndStretch();
    testTriggerAndRetriggerModes();
    testMonoLegatoAndGlide();
    testSixteenChannelLock();
    testPostMixOutputGainAndPan();
    testLiveTuneAndFine();
    testLiveSustainAndRelease();
    testLiveLoopEditing();
    testKillAllPlayback();
    testPolyphonicVoiceCursors();
    testPreparedAssetIsNotRescannedInRender();
    if (failures != 0) {
        std::cerr << failures << " sample player smoke failure(s)\n";
        return 1;
    }
    std::cout << "sample player smoke passed\n";
    return 0;
}
