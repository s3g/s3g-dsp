#include "s3g_sample_lanes.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>

namespace {

using namespace s3g::sample;

void require(bool condition, const char* message)
{
    if (condition) return;
    std::cerr << "sample lanes smoke failed: " << message << '\n';
    std::exit(1);
}

std::shared_ptr<SampleAsset> makeAsset(float level, uint32_t frames,
    bool stereo = true)
{
    auto asset = std::make_shared<SampleAsset>();
    asset->sampleRate = 48000.0;
    asset->channelCount = stereo ? 2u : 1u;
    for (uint8_t channel = 0u; channel < asset->channelCount; ++channel)
        asset->channels[channel].assign(frames,
            level * (channel == 0u ? 1.0f : 0.75f));
    require(asset->valid(), "fixture validity");
    return asset;
}

std::shared_ptr<SampleAsset> makeRampAsset(uint32_t frames)
{
    auto asset = std::make_shared<SampleAsset>();
    asset->sampleRate = 48000.0;
    asset->channelCount = 1u;
    asset->channels[0u].resize(frames);
    for (uint32_t frame = 0u; frame < frames; ++frame)
        asset->channels[0u][frame] = -1.0f + 2.0f
            * static_cast<float>(frame) / static_cast<float>(frames - 1u);
    require(asset->valid(), "ramp fixture validity");
    return asset;
}

std::shared_ptr<SampleAsset> makeToneAsset(float frequency, uint32_t frames)
{
    auto asset = std::make_shared<SampleAsset>();
    asset->sampleRate = 48000.0;
    asset->channelCount = 1u;
    asset->channels[0u].resize(frames);
    constexpr double pi = 3.14159265358979323846;
    for (uint32_t frame = 0u; frame < frames; ++frame)
        asset->channels[0u][frame] = static_cast<float>(std::sin(
            2.0 * pi * frequency * static_cast<double>(frame) / 48000.0));
    require(asset->valid(), "tone fixture validity");
    return asset;
}

std::shared_ptr<SampleAsset> makeWideAsset(uint8_t channels, uint32_t frames)
{
    auto asset = std::make_shared<SampleAsset>();
    asset->sampleRate = 48000.0;
    asset->channelCount = channels;
    for (uint8_t channel = 0u; channel < channels; ++channel)
        asset->channels[channel].assign(frames, 0.1f * (channel + 1u));
    require(asset->valid(), "wide fixture validity");
    return asset;
}

} // namespace

int main()
{
    require(std::abs(laneSourcePhase(0.10, 0.25) - 0.35) < 1.0e-12,
        "positive nudge advances displayed source content");
    require(std::abs(laneSourcePhase(0.90, 0.25) - 0.15) < 1.0e-12,
        "displayed nudge wraps source content");
    require(std::abs(laneTimelinePhase(0.15, 0.25) - 0.90) < 1.0e-12,
        "nudge mapping preserves timeline-space playhead");

    SampleLanesSettings manualPath;
    manualPath.path = LanePath::Manual;
    manualPath.manualPathPointCount = 4u;
    manualPath.manualPathPoints[0u] = { 0.0f, 0.25f };
    manualPath.manualPathPoints[1u] = { 0.20f, 1.0f };
    manualPath.manualPathPoints[2u] = { 0.70f, 0.0f };
    manualPath.manualPathPoints[3u] = { 1.0f, 0.75f };
    require(manualPath.valid(), "manual breakpoint path is valid");
    require(std::abs(sampleLanePathUnit(0.45, manualPath) - 0.5)
            < 1.0e-6,
        "manual breakpoint path interpolates between editable points");
    require(randomLanePathUnit(0.31, 4312u)
            == sampleLanePathUnit(0.31, [] {
                SampleLanesSettings value;
                value.path = LanePath::Random;
                value.seed = 4312u;
                return value;
            }()),
        "random scope and engine share one seeded path evaluator");

    SampleLanesSettings invalid;
    invalid.start = invalid.end;
    require(!invalid.valid(), "invalid bounds rejected");

    std::array<std::shared_ptr<SampleAsset>, kSampleLaneCount> assets {{
        makeAsset(0.20f, 48000u),
        makeAsset(0.40f, 24000u),
        makeAsset(0.60f, 72000u, false),
        makeAsset(0.80f, 36000u),
    }};
    std::array<const SampleAsset*, kSampleLaneCount> pointers {};
    for (std::size_t lane = 0u; lane < assets.size(); ++lane)
        pointers[lane] = assets[lane].get();

    SampleLanesEngine engine;
    require(engine.prepare(48000.0), "prepare");
    require(engine.setAssets(pointers), "set four lanes");

    SampleLanesSettings settings;
    settings.voiceMode = VoiceMode::Mono;
    settings.path = LanePath::Down;
    settings.pathShape = LanePathShape::Linear;
    settings.blend = LaneBlend::Crossfade;
    settings.rateBasis = LaneRateBasis::Hertz;
    settings.rate = 1.0f;
    settings.attackSeconds = 0.0f;
    settings.outputGainDecibels = 0.0f;
    settings.pan = 0.0f;
    settings.laneSlewSeconds = 0.0f;
    LanesRenderEvent note;
    note.kind = LaneEventKind::NoteOn;
    note.noteId = 1u;
    std::vector<float> left(36001u);
    std::vector<float> right(36001u);
    engine.render(settings, &note, 1u, left.data(), right.data(),
        static_cast<uint32_t>(left.size()));
    require(engine.activeVoiceCount() == 1u, "mono read head");
    require(engine.voiceCursorCount() == 1u, "cursor publication");
    const auto diagonal = engine.voiceCursors()[0u];
    require(diagonal.sourcePositionNormalized > 0.74f
            && diagonal.sourcePositionNormalized < 0.76f,
        "normalized horizontal phase");
    require(diagonal.lanePositionNormalized > 0.74f
            && diagonal.lanePositionNormalized < 0.76f,
        "linear diagonal crosses all lanes");
    require(left.back() > left.front(), "lane crossfade changes source");
    require(right.back() < left.back()
            && right.back() > left.back() * 0.8f,
        "mono and stereo lanes preserve their channel relationships");

    engine.reset();
    settings.path = LanePath::StepsDown;
    settings.blend = LaneBlend::Jump;
    settings.rate = 4.0f;
    settings.laneSlewSeconds = 0.003f;
    left.assign(12000u, 0.0f);
    right.assign(12000u, 0.0f);
    engine.render(settings, &note, 1u, left.data(), right.data(),
        static_cast<uint32_t>(left.size()));
    require(*std::max_element(left.begin(), left.end()) > 0.3f,
        "step path jumps lanes");
    double maximumDelta = 0.0;
    for (std::size_t frame = 1u; frame < left.size(); ++frame)
        maximumDelta = std::max(maximumDelta,
            std::abs(static_cast<double>(left[frame] - left[frame - 1u])));
    require(maximumDelta < 0.02, "jump slew prevents source click");

    engine.reset();
    settings.path = LanePath::Random;
    settings.seed = 543u;
    settings.rate = 3.0f;
    left.assign(24000u, 0.0f);
    right.assign(24000u, 0.0f);
    engine.render(settings, &note, 1u, left.data(), right.data(),
        static_cast<uint32_t>(left.size()));
    const auto randomCursor = engine.voiceCursors()[0u];
    require(std::abs(randomCursor.lanePositionNormalized
            - sampleLanePathUnit(randomCursor.pathPhase, settings))
            < 1.0e-6,
        "random read head follows the displayed seeded path");
    const auto randomA = left;
    engine.reset();
    engine.render(settings, &note, 1u, left.data(), right.data(),
        static_cast<uint32_t>(left.size()));
    require(randomA == left, "seeded random lane path is reproducible");

    engine.reset();
    require(engine.setAsset(1u, nullptr), "clear lane");
    require(engine.setAsset(2u, nullptr), "clear second lane");
    settings.path = LanePath::Manual;
    settings.manualLane = 0.5f;
    settings.blend = LaneBlend::Crossfade;
    settings.rate = 1.0f;
    left.assign(64u, 0.0f);
    right.assign(64u, 0.0f);
    engine.render(settings, &note, 1u, left.data(), right.data(),
        static_cast<uint32_t>(left.size()));
    require(left[20u] > 0.2f, "empty lanes interpolate nearest loaded files");

    engine.reset();
    settings.triggerMode = TriggerMode::Gate;
    settings.releaseSeconds = 0.0f;
    std::array<LanesRenderEvent, 2u> events {{
        { 8u, LaneEventKind::NoteOn, 77u, 60u, 1.0f, 0u },
        { 32u, LaneEventKind::NoteOff, 77u, 60u, 0.0f, 0u },
    }};
    left.assign(64u, 1.0f);
    right.assign(64u, 1.0f);
    engine.render(settings, events.data(), events.size(), left.data(),
        right.data(), static_cast<uint32_t>(left.size()));
    require(std::all_of(left.begin(), left.begin() + 8u,
            [](float value) { return value == 0.0f; }),
        "sample-accurate onset");
    require(std::all_of(left.begin() + 32u, left.end(),
            [](float value) { return value == 0.0f; }),
        "sample-accurate release");

    // A deliberately discontinuous ramp would click by two full-scale units
    // at the loop boundary without the endpoint overlap.
    auto ramp = makeRampAsset(48000u);
    require(engine.setAssets({ ramp.get(), nullptr, nullptr, nullptr }),
        "set seam fixture");
    engine.reset();
    settings = {};
    settings.voiceMode = VoiceMode::Mono;
    settings.path = LanePath::Manual;
    settings.manualLane = 0.0f;
    settings.rateBasis = LaneRateBasis::Normal;
    settings.rate = 1.0f;
    settings.attackSeconds = 0.0f;
    settings.outputGainDecibels = 0.0f;
    settings.pan = -1.0f;
    settings.loopCrossfade = 0.02;
    left.assign(50000u, 0.0f);
    right.assign(50000u, 0.0f);
    engine.render(settings, &note, 1u, left.data(), right.data(),
        static_cast<uint32_t>(left.size()));
    maximumDelta = 0.0;
    for (std::size_t frame = 1u; frame < left.size(); ++frame)
        maximumDelta = std::max(maximumDelta,
            std::abs(static_cast<double>(left[frame] - left[frame - 1u])));
    require(maximumDelta < 0.01, "forward endpoint join is seamless");

    engine.reset();
    settings.transport = LaneTransport::Reverse;
    std::fill(left.begin(), left.end(), 0.0f);
    std::fill(right.begin(), right.end(), 0.0f);
    engine.render(settings, &note, 1u, left.data(), right.data(),
        static_cast<uint32_t>(left.size()));
    maximumDelta = 0.0;
    for (std::size_t frame = 1u; frame < left.size(); ++frame)
        maximumDelta = std::max(maximumDelta,
            std::abs(static_cast<double>(left[frame] - left[frame - 1u])));
    require(maximumDelta < 0.01, "reverse endpoint join is seamless");

    engine.reset();
    settings.transport = LaneTransport::Forward;
    settings.laneStretch[0u] = 2.0f;
    left.assign(100000u, 0.0f);
    right.assign(100000u, 0.0f);
    engine.render(settings, &note, 1u, left.data(), right.data(),
        static_cast<uint32_t>(left.size()));
    maximumDelta = 0.0;
    for (std::size_t frame = 1u; frame < left.size(); ++frame)
        maximumDelta = std::max(maximumDelta,
            std::abs(static_cast<double>(left[frame] - left[frame - 1u])));
    if (maximumDelta >= 0.02)
        std::cerr << "stretched seam delta: " << maximumDelta << '\n';
    require(maximumDelta < 0.02,
        "stretched lane remains seamless across its wrapped endpoint");

    auto tone = makeToneAsset(220.0f, 96000u);
    require(engine.setAssets({ tone.get(), nullptr, nullptr, nullptr }),
        "set lane timing fixture");
    engine.reset();
    settings = {};
    settings.voiceMode = VoiceMode::Mono;
    settings.path = LanePath::Manual;
    settings.manualLane = 0.0f;
    settings.attackSeconds = 0.0f;
    settings.outputGainDecibels = 0.0f;
    settings.pan = -1.0f;
    settings.laneSpeed[0u] = 2.0f;
    left.assign(12000u, 0.0f);
    right.assign(12000u, 0.0f);
    engine.render(settings, &note, 1u, left.data(), right.data(),
        static_cast<uint32_t>(left.size()));
    require(engine.voiceCursors()[0u].sourcePositionNormalized > 0.12f
            && engine.voiceCursors()[0u].sourcePositionNormalized < 0.13f
            && engine.voiceCursors()[0u].laneSourcePositions[0u] > 0.24f
            && engine.voiceCursors()[0u].laneSourcePositions[0u] < 0.26f,
        "lane speed advances independently of the path clock");

    engine.reset();
    settings.laneSpeed[0u] = 1.0f;
    settings.laneStretch[0u] = 2.0f;
    left.assign(48000u, 0.0f);
    right.assign(48000u, 0.0f);
    engine.render(settings, &note, 1u, left.data(), right.data(),
        static_cast<uint32_t>(left.size()));
    const auto stretched = engine.voiceCursors()[0u];
    require(stretched.sourcePositionNormalized > 0.49f
            && stretched.sourcePositionNormalized < 0.51f
            && stretched.laneSourcePositions[0u] > 0.24f
            && stretched.laneSourcePositions[0u] < 0.26f,
        "stretch extends lane timing without slowing the path clock");
    uint32_t crossings = 0u;
    for (std::size_t frame = 8001u; frame < left.size(); ++frame)
        if (left[frame - 1u] <= 0.0f && left[frame] > 0.0f) ++crossings;
    const double stretchedFrequency = static_cast<double>(crossings)
        * 48000.0 / static_cast<double>(left.size() - 8001u);
    require(stretchedFrequency > 190.0 && stretchedFrequency < 250.0,
        "stretch preserves lane pitch");

    engine.reset();
    settings.laneStretch[0u] = 1.0f;
    settings.laneNudge[0u] = -0.25f;
    left.assign(1u, 0.0f);
    right.assign(1u, 0.0f);
    engine.render(settings, &note, 1u, left.data(), right.data(), 1u);
    require(engine.voiceCursors()[0u].laneSourcePositions[0u] > 0.74f
            && engine.voiceCursors()[0u].laneSourcePositions[0u] < 0.76f,
        "negative timing nudge wraps within the lane loop");

    auto field = makeWideAsset(4u, 24000u);
    require(engine.prepare(48000.0, 32u), "32-channel prepare");
    require(engine.setAssets({ field.get(), nullptr, nullptr, nullptr }),
        "wide lane adoption");
    settings = {};
    settings.voiceMode = VoiceMode::Poly;
    settings.path = LanePath::Manual;
    settings.manualLane = 0.0f;
    settings.attackSeconds = 0.0f;
    settings.outputGainDecibels = 0.0f;
    settings.outputMode = LaneOutputMode::Preserve;
    settings.activeOutputChannels = 32u;
    std::array<std::vector<float>, 32u> channels;
    std::array<float*, 32u> outputs {};
    for (std::size_t channel = 0u; channel < channels.size(); ++channel) {
        channels[channel].assign(128u, 0.0f);
        outputs[channel] = channels[channel].data();
    }
    engine.render(settings, &note, 1u, outputs.data(), 32u, 128u);
    for (std::size_t channel = 0u; channel < 4u; ++channel)
        require(channels[channel][64u] > 0.05f,
            "preserve field keeps a lane source channel");
    for (std::size_t channel = 4u; channel < channels.size(); ++channel)
        require(channels[channel][64u] == 0.0f,
            "preserve field leaves absent source channels silent");

    engine.reset();
    settings.outputMode = LaneOutputMode::Distribute;
    settings.activeOutputChannels = 8u;
    settings.outputRouting.width = s3g::routing::OutputVoiceWidth::Stereo;
    settings.outputRouting.traversal
        = s3g::routing::OutputTraversal::Sequential;
    std::array<LanesRenderEvent, 4u> notes {};
    for (std::size_t index = 0u; index < notes.size(); ++index) {
        notes[index].kind = LaneEventKind::NoteOn;
        notes[index].noteId = index + 1u;
        notes[index].key = static_cast<uint8_t>(60u + index);
        notes[index].velocity = 1.0f;
    }
    for (auto& channel : channels)
        std::fill(channel.begin(), channel.end(), 0.0f);
    engine.render(settings, notes.data(), notes.size(), outputs.data(), 32u,
        128u);
    for (std::size_t channel = 0u; channel < 8u; ++channel)
        require(channels[channel][64u] > 0.01f,
            "distributed lane voices traverse active outputs");
    for (std::size_t channel = 8u; channel < channels.size(); ++channel)
        require(channels[channel][64u] == 0.0f,
            "active output width bounds lane distribution");

    std::cout << "sample lanes smoke passed\n";
    return 0;
}
