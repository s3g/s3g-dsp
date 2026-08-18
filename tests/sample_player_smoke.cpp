#include "s3g_sample_player.h"

#include <array>
#include <cmath>
#include <iostream>
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
    testAdsrAndRelease();
    testOneShotTailReleaseIgnoresNoteOff();
    testSixteenChannelLock();
    if (failures != 0) {
        std::cerr << failures << " sample player smoke failure(s)\n";
        return 1;
    }
    std::cout << "sample player smoke passed\n";
    return 0;
}
