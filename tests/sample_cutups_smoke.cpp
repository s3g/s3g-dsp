#include "s3g_sample_cutups.h"
#include "s3g_sample_cutups_analysis.h"

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
    std::cerr << "sample cutups smoke failed: " << message << '\n';
    std::exit(1);
}

std::shared_ptr<SampleAsset> makeAsset(float level, uint32_t frames,
    uint8_t channels = 2u)
{
    auto asset = std::make_shared<SampleAsset>();
    asset->sampleRate = 48000.0;
    asset->channelCount = channels;
    for (uint8_t channel = 0u; channel < channels; ++channel)
        asset->channels[channel].assign(frames,
            level * (1.0f - 0.08f * channel));
    require(asset->valid(), "fixture validity");
    return asset;
}

std::shared_ptr<SampleAsset> makeRamp(uint32_t frames)
{
    auto asset = std::make_shared<SampleAsset>();
    asset->sampleRate = 48000.0;
    asset->channelCount = 1u;
    asset->channels[0u].resize(frames);
    for (uint32_t frame = 0u; frame < frames; ++frame)
        asset->channels[0u][frame] = static_cast<float>(frame)
            / static_cast<float>(frames - 1u);
    require(asset->valid(), "ramp validity");
    return asset;
}

CutupsLaneMetadata makeMetadata()
{
    CutupsLaneMetadata metadata;
    metadata.transientRegions.count = 4u;
    metadata.transientRegions.starts[0u] = 0.0f;
    metadata.transientRegions.starts[1u] = 0.10f;
    metadata.transientRegions.starts[2u] = 0.42f;
    metadata.transientRegions.starts[3u] = 0.81f;
    metadata.analyzedBpm = 127.5;
    metadata.tempoConfidence = 0.8f;
    metadata.tempoValid = true;
    return metadata;
}

SampleAsset makeWideTempoAsset()
{
    SampleAsset asset;
    asset.sampleRate = 4000.0;
    asset.channelCount = 4u;
    constexpr uint32_t frames = 4000u * 12u;
    constexpr uint32_t beatFrames = 2000u;
    for (uint8_t channel = 0u; channel < asset.channelCount; ++channel)
        asset.channels[channel].assign(frames, 0.0f);
    for (uint32_t beat = 0u; beat < frames; beat += beatFrames) {
        for (uint32_t offset = 0u; offset < 80u && beat + offset < frames;
             ++offset) {
            const float pulse = std::exp(-static_cast<float>(offset) / 12.0f);
            for (uint8_t channel = 0u; channel < asset.channelCount; ++channel)
                asset.channels[channel][beat + offset] = pulse
                    * (1.0f - 0.1f * channel);
        }
    }
    return asset;
}

} // namespace

int main()
{
    SampleCutupsSettings settings;
    require(settings.valid(), "default settings valid");
    settings.regionCount = 1u;
    settings.patternLength = 1u;
    require(settings.valid(), "one step and one region are valid");
    settings.regionCount = 64u;
    settings.patternLength = 64u;
    require(settings.valid(), "64 steps and regions are valid");
    settings.regionCount = 65u;
    settings.patternLength = 65u;
    require(!settings.valid(), "more than 64 steps and regions rejected");
    settings.patternLength = 1u;
    settings.regionCount = 0u;
    require(!settings.valid(), "zero regions rejected");
    settings = {};
    settings.start = settings.end;
    require(!settings.valid(), "invalid source window rejected");
    settings = {};

    const auto wideAnalysis = analyzeCutupsAsset(makeWideTempoAsset());
    require(wideAnalysis.valid() && wideAnalysis.tempoValid
            && std::abs(wideAnalysis.analyzedBpm - 120.0) < 1.0,
        "per-file BPM analysis accepts multichannel sources");
    require(wideAnalysis.transientRegions.count >= 8u,
        "transient-derived regions are produced during file analysis");
    const auto noPreroll = analyzeCutupsAsset(makeWideTempoAsset(),
        kMaximumCutupsRegions, 0.0, 0u);
    const auto tenMillisecondPreroll = analyzeCutupsAsset(
        makeWideTempoAsset(), kMaximumCutupsRegions, 0.0, 10000u);
    require(noPreroll.transientRegions.count > 1u
            && tenMillisecondPreroll.transientRegions.count > 1u
            && tenMillisecondPreroll.transientRegions.starts[1u]
                < noPreroll.transientRegions.starts[1u],
        "transient preroll moves detected region starts earlier");

    const std::array<std::shared_ptr<SampleAsset>, 4u> assets {{
        makeAsset(0.2f, 48000u),
        makeAsset(0.4f, 36000u),
        makeAsset(0.6f, 72000u, 1u),
        makeAsset(0.8f, 24000u),
    }};
    std::array<const SampleAsset*, 4u> pointers {};
    for (std::size_t index = 0u; index < assets.size(); ++index)
        pointers[index] = assets[index].get();

    SampleCutupsEngine engine;
    require(engine.prepare(48000.0), "prepare stereo");
    require(engine.setAssets(pointers), "set four source files");
    settings.clockBasis = CutClockBasis::Hertz;
    settings.cutRateHz = 8.0f;
    settings.fileOrder = CutFileOrder::Down;
    settings.sourceOrder = CutSourceOrder::Forward;
    settings.regionCount = 16u;
    settings.patternLength = 16u;
    settings.attackSeconds = 0.0f;
    settings.releaseSeconds = 0.0f;
    settings.outputGainDecibels = 0.0f;
    settings.pan = 0.0f;
    settings.joinMilliseconds = 0.0f;
    CutupsRenderEvent note;
    note.kind = CutupsEventKind::NoteOn;
    note.noteId = 1u;
    std::vector<float> left(18001u);
    std::vector<float> right(18001u);
    engine.render(settings, &note, 1u, left.data(), right.data(),
        static_cast<uint32_t>(left.size()));
    require(engine.activeVoiceCount() == 1u, "note starts one voice");
    require(engine.voiceCursorCount() == 1u, "voice cursor published");
    const auto ordered = engine.voiceCursors()[0u];
    require(ordered.lane == 3u, "file order advances once per cut");
    require(ordered.region == 3u, "region order advances independently");
    require(left[7000u] > left[1000u], "successive cuts change files");

    engine.reset();
    settings.fileOrder = CutFileOrder::RandomCycle;
    settings.sourceOrder = CutSourceOrder::Random;
    settings.seed = 4312u;
    left.assign(24000u, 0.0f);
    right.assign(24000u, 0.0f);
    engine.render(settings, &note, 1u, left.data(), right.data(),
        static_cast<uint32_t>(left.size()));
    const auto deterministic = left;
    engine.reset();
    std::fill(left.begin(), left.end(), 0.0f);
    engine.render(settings, &note, 1u, left.data(), right.data(),
        static_cast<uint32_t>(left.size()));
    require(left == deterministic, "seeded cut stream is deterministic");

    engine.reset();
    settings.fileOrder = CutFileOrder::Random;
    settings.sourceOrder = CutSourceOrder::Random;
    settings.repeatCount = 3u;
    left.assign(12001u, 0.0f);
    right.assign(12001u, 0.0f);
    engine.render(settings, &note, 1u, left.data(), right.data(),
        static_cast<uint32_t>(left.size()));
    require(engine.voiceCursors()[0u].patternStep == 1u,
        "step repeat advances only after the configured repetitions");
    require(std::abs(left[1000u] - left[7000u]) < 1.0e-6f,
        "step repeat retains the chosen file and region address");

    auto ramp = makeRamp(48000u);
    require(engine.setAssets({ ramp.get(), nullptr, nullptr, nullptr }),
        "install transient fixture");
    const CutupsLaneMetadata metadata = makeMetadata();
    require(engine.setMetadata(0u, &metadata), "install lane analysis");
    engine.reset();
    settings = {};
    settings.clockBasis = CutClockBasis::Hertz;
    settings.cutRateHz = 20.0f;
    settings.fileOrder = CutFileOrder::Down;
    settings.regionMode = CutRegionMode::Transient;
    settings.sourceOrder = CutSourceOrder::Forward;
    settings.regionCount = 16u;
    settings.attackSeconds = 0.0f;
    settings.outputGainDecibels = 0.0f;
    settings.pan = -1.0f;
    settings.joinMilliseconds = 0.0f;
    left.assign(4801u, 0.0f);
    right.assign(4801u, 0.0f);
    engine.render(settings, &note, 1u, left.data(), right.data(),
        static_cast<uint32_t>(left.size()));
    require(left[2400u] > 0.09f && left[2400u] < 0.16f,
        "transient mode starts the second cut at detected onset");
    require(engine.voiceCursors()[0u].region == 2u,
        "transient region ordinal is published");

    engine.reset();
    settings.regionMode = CutRegionMode::Equal;
    settings.sourceOrder = CutSourceOrder::Timeline;
    settings.clockBasis = CutClockBasis::Host;
    settings.division = CutDivision::Whole;
    settings.tempoSync = true;
    settings.laneBpm[0u] = 120.0f;
    left.assign(4801u, 0.0f);
    right.assign(4801u, 0.0f);
    engine.render(settings, &note, 1u, left.data(), right.data(),
        static_cast<uint32_t>(left.size()), 240.0);
    require(engine.voiceCursors()[0u].sourcePositionNormalized > 0.19f
            && engine.voiceCursors()[0u].sourcePositionNormalized < 0.21f,
        "per-file BPM scales playback against host tempo");

    engine.reset();
    settings = {};
    settings.clockBasis = CutClockBasis::Hertz;
    settings.cutRateHz = 20.0f;
    settings.sourceOrder = CutSourceOrder::Forward;
    settings.regionCount = 2u;
    settings.attackSeconds = 0.0f;
    settings.outputGainDecibels = 0.0f;
    settings.pan = -1.0f;
    settings.joinMilliseconds = 8.0f;
    left.assign(6000u, 0.0f);
    right.assign(6000u, 0.0f);
    engine.render(settings, &note, 1u, left.data(), right.data(),
        static_cast<uint32_t>(left.size()));
    double maximumDelta = 0.0;
    for (std::size_t index = 1u; index < left.size(); ++index)
        maximumDelta = std::max(maximumDelta,
            std::abs(static_cast<double>(left[index] - left[index - 1u])));
    require(maximumDelta < 0.01,
        "join suppresses discontinuities between distant regions");

    engine.reset();
    settings.joinMilliseconds = 0.0f;
    settings.triggerMode = TriggerMode::OneShot;
    settings.patternLength = 2u;
    settings.releaseSeconds = 0.0f;
    left.assign(5000u, 0.0f);
    right.assign(5000u, 0.0f);
    engine.render(settings, &note, 1u, left.data(), right.data(),
        static_cast<uint32_t>(left.size()));
    require(left[3000u] > 0.0f,
        "one shot renders the final pattern step");
    require(engine.activeVoiceCount() == 0u,
        "one shot releases at the completed pattern boundary");

    std::array<std::shared_ptr<SampleAsset>, 1u> wide {{
        makeAsset(0.25f, 48000u, 8u),
    }};
    SampleCutupsEngine wideEngine;
    require(wideEngine.prepare(48000.0, 32u), "prepare wide output");
    require(wideEngine.setAssets({ wide[0].get(), nullptr, nullptr, nullptr }),
        "set wide source");
    settings = {};
    settings.clockBasis = CutClockBasis::Hertz;
    settings.cutRateHz = 20.0f;
    settings.attackSeconds = 0.0f;
    settings.outputGainDecibels = 0.0f;
    std::array<std::vector<float>, 32u> outputStorage;
    std::array<float*, 32u> outputs {};
    for (std::size_t channel = 0u; channel < outputs.size(); ++channel) {
        outputStorage[channel].assign(256u, 0.0f);
        outputs[channel] = outputStorage[channel].data();
    }
    wideEngine.render(settings, &note, 1u, outputs.data(), 32u, 256u);
    require(outputStorage[7u][100u] > 0.0f
            && outputStorage[8u][100u] == 0.0f,
        "preserve field retains wide source channel order");

    settings.triggerMode = TriggerMode::Gate;
    settings.releaseSeconds = 0.0f;
    std::array<CutupsRenderEvent, 2u> events {{
        { 8u, CutupsEventKind::NoteOn, 77u, 60u, 1.0f, 0u },
        { 32u, CutupsEventKind::NoteOff, 77u, 60u, 0.0f, 0u },
    }};
    engine.reset();
    require(engine.setAssets({ ramp.get(), nullptr, nullptr, nullptr }),
        "restore release fixture");
    left.assign(64u, 1.0f);
    right.assign(64u, 1.0f);
    engine.render(settings, events.data(), events.size(), left.data(),
        right.data(), static_cast<uint32_t>(left.size()));
    require(std::all_of(left.begin(), left.begin() + 8u,
            [](float value) { return value == 0.0f; }),
        "sample accurate onset");
    require(std::all_of(left.begin() + 32u, left.end(),
            [](float value) { return value == 0.0f; }),
        "sample accurate release");

    std::cout << "sample cutups smoke passed\n";
    return 0;
}
