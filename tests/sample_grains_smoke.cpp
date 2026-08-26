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
    invalid = {};
    invalid.grainSizeVariation = 1.1f;
    require(!invalid.valid(), "invalid size variation rejected");
    invalid = {};
    invalid.envelopeSkew = -1.1f;
    require(!invalid.valid(), "invalid envelope skew rejected");

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
            && liveGrain.pathClockPhase >= 0.0f
            && liveGrain.pathClockPhase <= 1.0f
            && liveGrain.laneSourcePositions[0u] >= 0.0f
            && std::abs(liveGrain.laneSourceSpans[0u]) > 0.0f,
        "live grain cursor exposes source window, envelope phase, and span");

    SampleGrainsSettings variationSettings;
    variationSettings.attackSeconds = 0.0f;
    variationSettings.outputGainDecibels = 0.0f;
    variationSettings.path = LanePath::Manual;
    variationSettings.manualLane = 0.0f;
    variationSettings.positionSpray = 0.0f;
    variationSettings.grainDensityHz = 10.0f;
    variationSettings.grainSizeMilliseconds = 200.0f;
    engine.reset();
    left.assign(128u, 0.0f);
    right.assign(128u, 0.0f);
    engine.render(variationSettings, &note, 1u, left.data(), right.data(),
        static_cast<uint32_t>(left.size()));
    const float baseSpan = std::abs(
        engine.grainCursors()[0u].laneSourceSpans[0u]);
    const float baseGain = engine.grainCursors()[0u].gain;
    const float baseGrainPhase = engine.grainCursors()[0u].phase;
    const float baseScanPosition = engine.voiceCursors()[0u]
        .sourcePositionNormalized;

    engine.reset();
    variationSettings.grainPitchSemitones = 12.0f;
    engine.render(variationSettings, &note, 1u, left.data(), right.data(),
        static_cast<uint32_t>(left.size()));
    const float octaveSpan = std::abs(
        engine.grainCursors()[0u].laneSourceSpans[0u]);
    require(std::abs(octaveSpan / baseSpan - 2.0f) < 0.01f
            && std::abs(engine.voiceCursors()[0u].sourcePositionNormalized
                - baseScanPosition) < 1.0e-6f
            && std::abs(engine.grainCursors()[0u].phase
                - baseGrainPhase) < 1.0e-6f,
        "grain pitch shift transposes without changing scan speed or duration");

    SampleGrainsSettings noteRateSettings;
    noteRateSettings.attackSeconds = 0.0f;
    noteRateSettings.outputGainDecibels = 0.0f;
    noteRateSettings.positionSpray = 0.0f;
    noteRateSettings.grainDensityHz = 100.0f;
    noteRateSettings.grainSizeMilliseconds = 200.0f;
    noteRateSettings.rateBasis = LaneRateBasis::Hertz;
    noteRateSettings.rate = 1.0f;
    left.assign(1024u, 0.0f);
    right.assign(1024u, 0.0f);
    engine.reset();
    engine.render(noteRateSettings, &note, 1u, left.data(), right.data(),
        static_cast<uint32_t>(left.size()));
    const float rootScanPosition = engine.voiceCursors()[0u]
        .sourcePositionNormalized;
    const float rootGrainSpan = std::abs(
        engine.grainCursors()[0u].laneSourceSpans[0u]);
    LanesRenderEvent octaveNote = note;
    octaveNote.key = 72u;
    engine.reset();
    engine.render(noteRateSettings, &octaveNote, 1u, left.data(), right.data(),
        static_cast<uint32_t>(left.size()));
    const float octaveScanPosition = engine.voiceCursors()[0u]
        .sourcePositionNormalized;
    const float octaveNoteGrainSpan = std::abs(
        engine.grainCursors()[0u].laneSourceSpans[0u]);
    require(rootScanPosition > 0.0f
            && std::abs(octaveScanPosition / rootScanPosition - 2.0f)
                < 0.01f
            && std::abs(octaveNoteGrainSpan - rootGrainSpan) < 1.0e-6f,
        "MIDI note tracks scan speed without transposing grain playback");

    engine.reset();
    variationSettings.grainPitchSemitones = 0.0f;
    variationSettings.grainSizeVariation = 1.0f;
    engine.render(variationSettings, &note, 1u, left.data(), right.data(),
        static_cast<uint32_t>(left.size()));
    require(std::abs(std::abs(engine.grainCursors()[0u]
                .laneSourceSpans[0u]) - baseSpan) > 1.0e-5f,
        "size variation changes individual grain duration");

    engine.reset();
    variationSettings.grainSizeVariation = 0.0f;
    variationSettings.grainLevelVariation = 1.0f;
    engine.render(variationSettings, &note, 1u, left.data(), right.data(),
        static_cast<uint32_t>(left.size()));
    require(engine.grainCursors()[0u].gain < baseGain,
        "level variation attenuates an individual grain before compensation");

    variationSettings.grainLevelVariation = 0.0f;
    variationSettings.grainDensityHz = 40.0f;
    variationSettings.grainSizeMilliseconds = 240.0f;
    variationSettings.grainTiming = GrainTiming::Regular;
    std::vector<float> regularLeft(3000u);
    std::vector<float> regularRight(3000u);
    engine.reset();
    engine.render(variationSettings, &note, 1u, regularLeft.data(),
        regularRight.data(), static_cast<uint32_t>(regularLeft.size()));
    variationSettings.grainTiming = GrainTiming::Scatter;
    variationSettings.timingScatter = 0.0f;
    std::vector<float> zeroScatterLeft(3000u);
    std::vector<float> zeroScatterRight(3000u);
    engine.reset();
    engine.render(variationSettings, &note, 1u, zeroScatterLeft.data(),
        zeroScatterRight.data(),
        static_cast<uint32_t>(zeroScatterLeft.size()));
    require(maximumDifference(regularLeft, zeroScatterLeft) < 1.0e-7f
            && maximumDifference(regularRight, zeroScatterRight) < 1.0e-7f,
        "zero timing scatter is sample-identical to regular timing");

    variationSettings.grainTiming = GrainTiming::Regular;
    variationSettings.envelopeSkew = 0.0f;
    engine.reset();
    engine.render(variationSettings, &note, 1u, regularLeft.data(),
        regularRight.data(), static_cast<uint32_t>(regularLeft.size()));
    variationSettings.envelopeSkew = 0.8f;
    engine.reset();
    engine.render(variationSettings, &note, 1u, zeroScatterLeft.data(),
        zeroScatterRight.data(),
        static_cast<uint32_t>(zeroScatterLeft.size()));
    require(maximumDifference(regularLeft, zeroScatterLeft) > 1.0e-5f,
        "envelope skew moves the selected window peak");

    engine.reset();
    settings.sourceAdvance = GrainSourceAdvance::Grain;
    settings.path = LanePath::StepsDown;
    settings.pathCycles = 2.0f;
    settings.positionSpray = 0.0f;
    left.assign(1300u, 0.0f);
    right.assign(1300u, 0.0f);
    engine.render(settings, &note, 1u, left.data(), right.data(),
        static_cast<uint32_t>(left.size()));
    require(engine.voiceCursorCount() == 1u
            && std::abs(engine.voiceCursors()[0u].pathPhase - 0.1875f)
                < 1.0e-5f
            && std::abs(engine.voiceCursors()[0u].lanePositionNormalized
                - 1.0f / 3.0f) < 1.0e-5f,
        "grain advance clocks the visible eight-step source path per event");

    settings = {};
    settings.attackSeconds = 0.0f;
    settings.grainSourceMode = GrainSourceMode::Freeze;
    settings.sourcePosition = 0.5f;
    settings.positionSpray = 0.2f;
    settings.positionBias = GrainPositionBias::Behind;
    engine.reset();
    left.assign(128u, 0.0f);
    right.assign(128u, 0.0f);
    engine.render(settings, &note, 1u, left.data(), right.data(),
        static_cast<uint32_t>(left.size()));
    require(engine.grainCursorCount() == 1u
            && engine.grainCursors()[0u].sourcePositionNormalized <= 0.5f,
        "behind bias constrains position spray behind the source point");
    settings.positionBias = GrainPositionBias::Ahead;
    engine.reset();
    engine.render(settings, &note, 1u, left.data(), right.data(),
        static_cast<uint32_t>(left.size()));
    require(engine.grainCursorCount() == 1u
            && engine.grainCursors()[0u].sourcePositionNormalized >= 0.5f,
        "ahead bias constrains position spray ahead of the source point");

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
    channelSettings.channelMode = GrainChannelMode::PreserveOrigins;
    left.assign(1024u, 0.0f);
    right.assign(1024u, 0.0f);
    engine.render(channelSettings, &note, 1u, left.data(), right.data(),
        static_cast<uint32_t>(left.size()));
    require(maximumDifference(left, right) > 1.0e-5f,
        "preserve origins retains distinct stereo channels");
    for (const auto channelMode : { GrainChannelMode::MonoSum,
            GrainChannelMode::Left, GrainChannelMode::Right,
            GrainChannelMode::Mid, GrainChannelMode::Side }) {
        engine.reset();
        channelSettings.channelMode = channelMode;
        left.assign(1024u, 0.0f);
        right.assign(1024u, 0.0f);
        engine.render(channelSettings, &note, 1u, left.data(), right.data(),
            static_cast<uint32_t>(left.size()));
        require(maximumDifference(left, right) < 1.0e-6f
                && peak(left) > 1.0e-5f,
            "stereo channel derivation becomes productive centered mono");
    }

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
    settings.grainDensityHz = 40.0f;
    settings.sourceTimeSync = true;
    settings.mutateAmount = 1.0f;
    left.assign(700u, 0.0f);
    right.assign(700u, 0.0f);
    engine.render(settings, &note, 1u, left.data(), right.data(),
        static_cast<uint32_t>(left.size()));
    require(engine.activeGrainCount() == 2u,
        "doublets create two source-time-related grains per event");

    engine.reset();
    settings.mutateAmount = 0.0f;
    engine.render(settings, &note, 1u, left.data(), right.data(),
        static_cast<uint32_t>(left.size()));
    require(engine.activeGrainCount() == 1u,
        "doublets Amount controls whether the paired grain is emitted");

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

    SampleGrainsSettings triggerSettings;
    triggerSettings.attackSeconds = 0.0f;
    triggerSettings.releaseSeconds = 0.0f;
    triggerSettings.outputGainDecibels = 0.0f;
    triggerSettings.rateBasis = LaneRateBasis::Hertz;
    triggerSettings.rate = 80.0f;
    triggerSettings.grainDensityHz = 20.0f;
    triggerSettings.grainSizeMilliseconds = 80.0f;
    triggerSettings.triggerMode = TriggerMode::OneShot;
    engine.reset();
    left.assign(1024u, 0.0f);
    right.assign(1024u, 0.0f);
    engine.render(triggerSettings, &note, 1u, left.data(), right.data(),
        static_cast<uint32_t>(left.size()));
    require(engine.voiceCursorCount() == 0u,
        "one-shot emitter stops after one selected-window traversal");
    triggerSettings.triggerMode = TriggerMode::Gate;
    engine.reset();
    engine.render(triggerSettings, &note, 1u, left.data(), right.data(),
        static_cast<uint32_t>(left.size()));
    require(engine.voiceCursorCount() == 1u,
        "gate emitter remains active until note-off");

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
