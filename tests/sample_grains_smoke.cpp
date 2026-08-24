#include "s3g_sample_grains.h"

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
    std::cerr << "sample grains smoke failed: " << message << '\n';
    std::exit(1);
}

std::shared_ptr<SampleAsset> makeAsset(uint8_t channels, uint32_t frames)
{
    auto asset = std::make_shared<SampleAsset>();
    asset->sampleRate = 48000.0;
    asset->channelCount = channels;
    constexpr double pi = 3.14159265358979323846;
    for (uint8_t channel = 0u; channel < channels; ++channel) {
        asset->channels[channel].resize(frames);
        for (uint32_t frame = 0u; frame < frames; ++frame)
            asset->channels[channel][frame] = static_cast<float>(
                (0.2 + 0.1 * channel) * std::sin(2.0 * pi
                    * (110.0 + 31.0 * channel) * frame / 48000.0));
    }
    require(asset->valid(), "fixture validity");
    return asset;
}

float peak(const std::vector<float>& samples)
{
    float result = 0.0f;
    for (float sample : samples) result = std::max(result, std::abs(sample));
    return result;
}

float maximumDifference(const std::vector<float>& first,
    const std::vector<float>& second)
{
    float result = 0.0f;
    for (std::size_t index = 0u; index < std::min(first.size(), second.size());
         ++index)
        result = std::max(result, std::abs(first[index] - second[index]));
    return result;
}

} // namespace

int main()
{
    SampleGrainsSettings invalid;
    invalid.grainDensityHz = 0.0f;
    require(!invalid.valid(), "invalid density rejected");

    auto mono = makeAsset(1u, 48000u);
    auto stereo = makeAsset(2u, 36000u);
    std::array<const SampleAsset*, kSampleLaneCount> sources {{
        mono.get(), stereo.get(), nullptr, nullptr,
    }};
    SampleGrainsEngine engine;
    require(engine.prepare(48000.0, 2u), "stereo prepare");
    require(engine.setAssets(sources), "source adoption");

    SampleGrainsSettings settings;
    settings.voiceMode = VoiceMode::Poly;
    settings.attackSeconds = 0.0f;
    settings.releaseSeconds = 0.0f;
    settings.outputGainDecibels = 0.0f;
    settings.grainDensityHz = 40.0f;
    settings.grainSizeMilliseconds = 120.0f;
    settings.path = LanePath::Down;
    settings.pathCycles = 2.0f;
    LanesRenderEvent note;
    note.kind = LaneEventKind::NoteOn;
    note.noteId = 1u;
    note.key = 60u;
    note.velocity = 1.0f;
    std::vector<float> left(4800u);
    std::vector<float> right(4800u);
    engine.render(settings, &note, 1u, left.data(), right.data(),
        static_cast<uint32_t>(left.size()));
    require(peak(left) > 1.0e-4f && peak(right) > 1.0e-4f,
        "ordinary grains render through stereo field");
    require(engine.activeVoiceCount() == 1u
            && engine.activeGrainCount() >= 3u,
        "density clock maintains overlapping grains");
    require(engine.voiceCursorCount() == 1u
            && engine.voiceCursors()[0u].lanePositionNormalized > 0.0f,
        "source-field path publishes its grain emitter cursor");
    require(engine.grainCursorCount() > 0u
            && engine.grainCursorCount()
                <= kMaximumPublishedGrainCursors,
        "active grains publish bounded live visualization cursors");
    const auto& liveGrain = engine.grainCursors()[0u];
    require(liveGrain.phase >= 0.0f && liveGrain.phase <= 1.0f
            && liveGrain.gain > 0.0f
            && liveGrain.laneSourcePositions[0u] >= 0.0f
            && std::abs(liveGrain.laneSourceSpans[0u]) > 0.0f,
        "live grain cursor exposes source window, envelope phase, and span");

    SampleGrainsSettings channelSettings;
    channelSettings.attackSeconds = 0.0f;
    channelSettings.releaseSeconds = 0.0f;
    channelSettings.outputGainDecibels = 0.0f;
    channelSettings.path = LanePath::Manual;
    channelSettings.manualLane = 0.0f;
    channelSettings.grainDensityHz = 10.0f;
    channelSettings.grainSizeMilliseconds = 200.0f;
    channelSettings.positionSpray = 0.0f;
    engine.reset();
    require(engine.setAssets({ stereo.get(), nullptr, nullptr, nullptr }),
        "stereo source adoption");
    channelSettings.channelMode = GrainChannelMode::Left;
    left.assign(1024u, 0.0f);
    right.assign(1024u, 0.0f);
    engine.render(channelSettings, &note, 1u, left.data(), right.data(),
        static_cast<uint32_t>(left.size()));
    require(maximumDifference(left, right) < 1.0e-6f,
        "left channel extraction becomes centered mono");

    engine.reset();
    channelSettings.channelMode = GrainChannelMode::PreserveOrigins;
    channelSettings.monoSpread = 0.0f;
    require(engine.setAssets({ mono.get(), nullptr, nullptr, nullptr }),
        "mono source adoption");
    left.assign(1024u, 0.0f);
    right.assign(1024u, 0.0f);
    engine.render(channelSettings, &note, 1u, left.data(), right.data(),
        static_cast<uint32_t>(left.size()));
    require(maximumDifference(left, right) < 1.0e-6f,
        "mono source remains centered when spread is zero");
    engine.reset();
    channelSettings.monoSpread = 1.0f;
    left.assign(1024u, 0.0f);
    right.assign(1024u, 0.0f);
    engine.render(channelSettings, &note, 1u, left.data(), right.data(),
        static_cast<uint32_t>(left.size()));
    require(maximumDifference(left, right) > 1.0e-5f,
        "mono spread gives each grain a stereo position");

    auto linkedStereo = makeAsset(2u, 36000u);
    linkedStereo->channels[1u] = linkedStereo->channels[0u];
    engine.reset();
    require(engine.setAssets({ linkedStereo.get(), nullptr, nullptr, nullptr }),
        "linked stereo fixture adoption");
    channelSettings.monoSpread = 0.0f;
    channelSettings.positionSpray = 0.2f;
    channelSettings.stereoLink = GrainStereoLink::Linked;
    left.assign(1024u, 0.0f);
    right.assign(1024u, 0.0f);
    engine.render(channelSettings, &note, 1u, left.data(), right.data(),
        static_cast<uint32_t>(left.size()));
    require(maximumDifference(left, right) < 1.0e-6f,
        "linked stereo grain uses one source trajectory");
    engine.reset();
    channelSettings.stereoLink = GrainStereoLink::Independent;
    left.assign(1024u, 0.0f);
    right.assign(1024u, 0.0f);
    engine.render(channelSettings, &note, 1u, left.data(), right.data(),
        static_cast<uint32_t>(left.size()));
    require(maximumDifference(left, right) > 1.0e-5f,
        "independent stereo grain uses separate source trajectories");

    engine.reset();
    require(engine.setAssets(sources), "restore mixed source adoption");

    for (const auto envelope : { GrainEnvelope::Parzen,
            GrainEnvelope::Sine, GrainEnvelope::Hann,
            GrainEnvelope::Triangle, GrainEnvelope::Gaussian }) {
        engine.reset();
        settings.grainEnvelope = envelope;
        left.assign(1024u, 0.0f);
        right.assign(1024u, 0.0f);
        engine.render(settings, &note, 1u, left.data(), right.data(),
            static_cast<uint32_t>(left.size()));
        require(peak(left) > 1.0e-5f, "grain envelope produces audio");
    }

    engine.reset();
    settings.grainMutate = GrainMutate::Doublets;
    settings.sourceTimeSync = true;
    left.assign(700u, 0.0f);
    right.assign(700u, 0.0f);
    engine.render(settings, &note, 1u, left.data(), right.data(),
        static_cast<uint32_t>(left.size()));
    require(engine.activeGrainCount() == 2u,
        "doublets create two source-time-related grains per event");

    for (const auto process : { GrainMutate::Sorter, GrainMutate::Stutter,
            GrainMutate::Shrink }) {
        engine.reset();
        settings.grainMutate = process;
        settings.mutateAmount = 1.0f;
        settings.regionCount = 8u;
        left.assign(4800u, 0.0f);
        right.assign(4800u, 0.0f);
        engine.render(settings, &note, 1u, left.data(), right.data(),
            static_cast<uint32_t>(left.size()));
        require(peak(left) > 1.0e-5f,
            "mutate operation remains a productive grain event process");
    }

    auto field = makeAsset(4u, 24000u);
    require(engine.prepare(48000.0, 32u), "multichannel prepare");
    require(engine.setAssets({ field.get(), nullptr, nullptr, nullptr }),
        "wide source adoption");
    settings = {};
    settings.voiceMode = VoiceMode::Poly;
    settings.attackSeconds = 0.0f;
    settings.outputGainDecibels = 0.0f;
    settings.path = LanePath::Manual;
    settings.manualLane = 0.0f;
    settings.outputMode = LaneOutputMode::Preserve;
    settings.activeOutputChannels = 32u;
    std::array<std::vector<float>, 32u> channels;
    std::array<float*, 32u> outputs {};
    for (std::size_t channel = 0u; channel < channels.size(); ++channel) {
        channels[channel].assign(1024u, 0.0f);
        outputs[channel] = channels[channel].data();
    }
    engine.render(settings, &note, 1u, outputs.data(), 32u, 1024u);
    for (std::size_t channel = 0u; channel < 4u; ++channel)
        require(peak(channels[channel]) > 1.0e-5f,
            "preserve field keeps a source channel active");
    for (std::size_t channel = 4u; channel < channels.size(); ++channel)
        require(peak(channels[channel]) == 0.0f,
            "preserve field does not invent source channels");

    engine.reset();
    settings.outputMode = LaneOutputMode::Distribute;
    settings.activeOutputChannels = 16u;
    settings.outputRouting.width = s3g::routing::OutputVoiceWidth::Stereo;
    settings.outputRouting.traversal
        = s3g::routing::OutputTraversal::Sequential;
    std::array<LanesRenderEvent, 8u> notes {};
    for (std::size_t index = 0u; index < notes.size(); ++index) {
        notes[index].kind = LaneEventKind::NoteOn;
        notes[index].noteId = index + 1u;
        notes[index].key = static_cast<uint8_t>(60u + index);
        notes[index].velocity = 1.0f;
    }
    for (auto& channel : channels)
        std::fill(channel.begin(), channel.end(), 0.0f);
    engine.render(settings, notes.data(), notes.size(), outputs.data(), 32u,
        1024u);
    for (std::size_t channel = 0u; channel < 16u; ++channel)
        require(peak(channels[channel]) > 1.0e-5f,
            "per-grain allocator traverses active distributed outputs");
    for (std::size_t channel = 16u; channel < channels.size(); ++channel)
        require(peak(channels[channel]) == 0.0f,
            "active output width limits distribution");

    std::cout << "sample grains smoke passed\n";
    return 0;
}
