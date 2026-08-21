#include "s3g_sample_wavesets.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "sample wavesets smoke failed: " << message << '\n';
        std::exit(1);
    }
}

std::shared_ptr<const s3g::sample::SampleAsset> makeSource()
{
    constexpr double sampleRate = 48000.0;
    constexpr uint32_t frames = 48000u;
    auto asset = std::make_shared<s3g::sample::SampleAsset>();
    asset->sampleRate = sampleRate;
    asset->channelCount = 2u;
    asset->channels[0u].resize(frames);
    asset->channels[1u].resize(frames);
    for (uint32_t frame = 0u; frame < frames; ++frame) {
        const double time = static_cast<double>(frame) / sampleRate;
        asset->channels[0u][frame] = static_cast<float>(
            0.68 * std::sin(2.0 * 3.14159265358979323846 * 220.0 * time));
        asset->channels[1u][frame] = static_cast<float>(
            0.52 * std::sin(2.0 * 3.14159265358979323846 * 331.0 * time
                + 0.19));
    }
    require(asset->valid(), "fixture asset");
    return asset;
}

std::shared_ptr<const s3g::sample::SampleAsset> makeProcessSource()
{
    constexpr double sampleRate = 48000.0;
    constexpr uint32_t frames = 48000u;
    auto asset = std::make_shared<s3g::sample::SampleAsset>();
    asset->sampleRate = sampleRate;
    asset->channelCount = 2u;
    asset->channels[0u].resize(frames);
    asset->channels[1u].resize(frames);
    double phaseLeft = 0.0;
    double phaseRight = 0.31;
    for (uint32_t frame = 0u; frame < frames; ++frame) {
        const double time = static_cast<double>(frame) / sampleRate;
        phaseLeft += 2.0 * 3.14159265358979323846
            * (105.0 + 270.0 * time + 31.0 * std::sin(time * 19.0))
            / sampleRate;
        phaseRight += 2.0 * 3.14159265358979323846
            * (137.0 + 193.0 * time + 24.0 * std::sin(time * 13.0))
            / sampleRate;
        asset->channels[0u][frame] = static_cast<float>(
            0.52 * std::sin(phaseLeft)
            + 0.21 * std::sin(phaseLeft * 2.17 + time * 7.0)
            + 0.09 * std::sin(phaseLeft * 5.31));
        asset->channels[1u][frame] = static_cast<float>(
            0.46 * std::sin(phaseRight)
            + 0.24 * std::sin(phaseRight * 2.73 + time * 5.0)
            + 0.08 * std::sin(phaseRight * 6.07));
    }
    require(asset->valid(), "process fixture asset");
    return asset;
}

float difference(const std::vector<float>& left,
    const std::vector<float>& right)
{
    float sum = 0.0f;
    for (std::size_t index = 0u; index < left.size(); ++index)
        sum += std::abs(left[index] - right[index]);
    return sum;
}

bool finiteSignal(const std::vector<float>& signal)
{
    return std::all_of(signal.begin(), signal.end(), [](float sample) {
        return std::isfinite(sample);
    });
}

float energy(const std::vector<float>& signal)
{
    float sum = 0.0f;
    for (float sample : signal) sum += std::abs(sample);
    return sum;
}

} // namespace

int main()
{
    using namespace s3g::sample;
    const auto source = makeSource();
    const auto raw = analyzeWavesets(source, WavesetCrossingDetail::Raw);
    const auto filtered = analyzeWavesets(source,
        WavesetCrossingDetail::Hz1000);
    require(raw && raw->valid(), "raw analysis");
    require(filtered && filtered->valid(), "filtered analysis");
    require(raw->channelUnits[0u].size() > 150u,
        "left waveset analysis");
    require(raw->channelUnits[1u].size() > raw->channelUnits[0u].size(),
        "right channel retains its own crossing map");
    require(!raw->sumUnits.empty(), "sum-mono analysis");
    require(raw->asset.get() == source.get(), "immutable source retained");
    require(std::any_of(raw->channelUnits[0u].begin(),
            raw->channelUnits[0u].end(), [](const WavesetUnit& unit) {
                return std::abs(unit.startPosition
                    - std::round(unit.startPosition)) > 1.0e-3;
            }),
        "sub-sample crossings retained");

    WavesetSettings settings;
    settings.playMode = WavesetPlayMode::ForwardLoop;
    settings.sourceMode = WavesetSourceMode::TrueChannels;
    settings.voiceMode = VoiceMode::Poly;
    settings.groupSize = 4u;
    settings.repeats = 2u;
    settings.outputGainDecibels = -12.0f;
    settings.attackSeconds = 0.0f;
    settings.releaseSeconds = 0.0f;

    constexpr uint32_t frames = 4096u;
    std::array<std::vector<float>, 2u> rendered {{
        std::vector<float>(frames), std::vector<float>(frames),
    }};
    std::array<float*, 2u> outputs {{
        rendered[0u].data(), rendered[1u].data(),
    }};
    const WavesetRenderEvent root {
        0u, WavesetEventKind::NoteOn, 1u, 60u, 1.0f, 0u,
    };
    SampleWavesetsEngine engine;
    require(engine.prepare(48000.0, 2u), "prepare");
    engine.setMap(raw.get());
    engine.render(settings, &root, 1u, outputs.data(), 2u, frames);
    require(engine.outputPeak() > 0.01f, "stereo waveset output");
    require(engine.activeVoiceCount() == 1u, "one voice starts");
    require(engine.primaryPositionNormalized() > 0.0f,
        "playhead advances");
    require(engine.voiceCursorCount() == 1u
            && engine.voiceCursors()[0u].key == 60u
            && engine.voiceCursors()[0u].identity != 0u,
        "voice cursor publishes note flag and stable identity");
    const auto& firstCursor = engine.voiceCursors()[0u];
    require(firstCursor.sourcePositionNormalized >= 0.0f
            && firstCursor.sourcePositionNormalized <= 1.0f
            && firstCursor.groupPositionNormalized >= 0.0f
            && firstCursor.groupPositionNormalized <= 1.0f
            && firstCursor.transportPositionNormalized >= 0.0f
            && firstCursor.transportPositionNormalized <= 1.0f
            && firstCursor.oscillatorPhase >= 0.0f
            && firstCursor.oscillatorPhase < 1.0f
            && firstCursor.cycleOffset < settings.groupSize,
        "voice cursor publishes the exact cycle and phase state");
    require(std::abs(firstCursor.sourcePositionNormalized
            - firstCursor.groupPositionNormalized) > 1.0e-6f,
        "voice cursor reports the fractional read head, not only group anchor");
    require(std::abs(firstCursor.transportPositionNormalized
            - static_cast<float>(frames) / 96000.0f) < 2.0e-4f,
        "waveform transport advances continuously at the source-time rate");
    require(difference(rendered[0u], rendered[1u]) > 1.0f,
        "true stereo preserves independent channel wavesets");

    const auto processSource = makeProcessSource();
    const auto processMap = analyzeWavesets(processSource,
        WavesetCrossingDetail::Raw);
    require(processMap && processMap->valid(), "process fixture analysis");
    struct ShapeRender {
        std::array<std::vector<float>, 2u> channels;
        float position = -1.0f;
    };
    const auto renderShape = [&](WavesetShape shape) {
        constexpr uint32_t processFrames = 8192u;
        ShapeRender result {{
            std::vector<float>(processFrames),
            std::vector<float>(processFrames),
        }};
        std::array<float*, 2u> shapeOutputs {{
            result.channels[0u].data(), result.channels[1u].data(),
        }};
        WavesetSettings shaped = settings;
        shaped.groupSize = 8u;
        shaped.repeats = 4u;
        shaped.stride = 1u;
        shaped.shape = shape;
        shaped.processAmount = 1.0f;
        SampleWavesetsEngine shapedEngine;
        require(shapedEngine.prepare(48000.0, 2u), "shape prepare");
        shapedEngine.setMap(processMap.get());
        shapedEngine.render(shaped, &root, 1u, shapeOutputs.data(), 2u,
            processFrames);
        result.position = shapedEngine.primaryPositionNormalized();
        require(finiteSignal(result.channels[0u])
                && finiteSignal(result.channels[1u])
                && difference(result.channels[0u], result.channels[1u])
                    > 0.1f,
            "waveset process remains finite and true stereo");
        return result;
    };
    const ShapeRender unprocessed = renderShape(WavesetShape::Repeat);
    const std::array<WavesetShape, 7u> requestedShapes {{
        WavesetShape::Average,
        WavesetShape::Interpolate,
        WavesetShape::Fractal,
        WavesetShape::AdditiveHarmonic,
        WavesetShape::GroupReverse,
        WavesetShape::CycleReverse,
        WavesetShape::Telescope,
    }};
    std::array<ShapeRender, requestedShapes.size()> shapedResults;
    for (std::size_t index = 0u; index < requestedShapes.size(); ++index) {
        shapedResults[index] = renderShape(requestedShapes[index]);
        require(difference(unprocessed.channels[0u],
                shapedResults[index].channels[0u]) > 0.5f,
            "requested waveset process changes the source shape");
    }
    require(difference(shapedResults[2u].channels[0u],
            shapedResults[3u].channels[0u]) > 0.5f,
        "fractal and additive harmonic are distinct processes");
    require(difference(shapedResults[4u].channels[0u],
            shapedResults[5u].channels[0u]) > 0.5f,
        "group reverse and cycle reverse are distinct processes");
    require(shapedResults[6u].position > unprocessed.position + 0.01f,
        "telescope contracts a group instead of only recoloring its cycle");

    constexpr uint32_t stressFrames = 1024u;
    std::array<WavesetRenderEvent, kMaximumWavesetVoices> stressNotes {};
    for (uint32_t voice = 0u; voice < stressNotes.size(); ++voice) {
        stressNotes[voice] = { 0u, WavesetEventKind::NoteOn, voice + 1u,
            static_cast<uint8_t>(48u + voice), 0.8f, 0u };
    }
    std::array<std::vector<float>, 2u> stressSignal {{
        std::vector<float>(stressFrames), std::vector<float>(stressFrames),
    }};
    std::array<float*, 2u> stressOutputs {{
        stressSignal[0u].data(), stressSignal[1u].data(),
    }};
    WavesetSettings stressSettings = settings;
    stressSettings.groupSize = 32u;
    stressSettings.repeats = 4u;
    stressSettings.shape = WavesetShape::Average;
    stressSettings.processAmount = 1.0f;
    SampleWavesetsEngine stressEngine;
    require(stressEngine.prepare(48000.0, 2u), "process stress prepare");
    stressEngine.setMap(processMap.get());
    stressEngine.render(stressSettings, stressNotes.data(), stressNotes.size(),
        stressOutputs.data(), 2u, stressFrames);
    require(stressEngine.activeVoiceCount() == kMaximumWavesetVoices
            && finiteSignal(stressSignal[0u])
            && finiteSignal(stressSignal[1u])
            && stressEngine.outputPeak() > 0.001f,
        "32-cycle Average remains finite across all sixteen voices");

    WavesetSettings leadIn = settings;
    leadIn.start = 0.0;
    leadIn.end = 1.0;
    leadIn.loopStart = 0.60;
    leadIn.loopEnd = 0.80;
    SampleWavesetsEngine forwardLeadIn;
    require(forwardLeadIn.prepare(48000.0, 2u), "forward lead-in prepare");
    forwardLeadIn.setMap(raw.get());
    forwardLeadIn.render(leadIn, &root, 1u, outputs.data(), 2u, frames);
    require(forwardLeadIn.primaryPositionNormalized() < 0.10f,
        "forward loop traverses Start-to-Loop lead-in");

    leadIn.playMode = WavesetPlayMode::ReverseLoop;
    leadIn.loopStart = 0.20;
    leadIn.loopEnd = 0.40;
    SampleWavesetsEngine reverseLeadIn;
    require(reverseLeadIn.prepare(48000.0, 2u), "reverse lead-in prepare");
    reverseLeadIn.setMap(raw.get());
    reverseLeadIn.render(leadIn, &root, 1u, outputs.data(), 2u, frames);
    require(reverseLeadIn.primaryPositionNormalized() > 0.90f,
        "reverse loop traverses End-to-Loop lead-in");

    WavesetSettings pointLoop = settings;
    pointLoop.playMode = WavesetPlayMode::ForwardPingPong;
    pointLoop.start = 0.50;
    pointLoop.end = 1.0;
    pointLoop.loopStart = 0.50;
    pointLoop.loopEnd = 0.50;
    pointLoop.groupSize = 1u;
    pointLoop.repeats = 1u;
    pointLoop.stride = 16u;
    SampleWavesetsEngine degenerateLoop;
    require(degenerateLoop.prepare(48000.0, 2u), "point-loop prepare");
    degenerateLoop.setMap(raw.get());
    degenerateLoop.render(pointLoop, &root, 1u, outputs.data(), 2u, frames);
    require(degenerateLoop.activeVoiceCount() == 1u
            && std::abs(degenerateLoop.primaryPositionNormalized() - 0.50f)
                < 0.02f,
        "zero-width ping-pong loop holds without blocking");

    settings.sourceMode = WavesetSourceMode::Left;
    SampleWavesetsEngine left;
    require(left.prepare(48000.0, 2u), "left prepare");
    left.setMap(raw.get());
    left.render(settings, &root, 1u, outputs.data(), 2u, frames);
    require(difference(rendered[0u], rendered[1u]) < 1.0e-4f,
        "left source is dual mono");

    settings.sourceMode = WavesetSourceMode::Right;
    SampleWavesetsEngine right;
    require(right.prepare(48000.0, 2u), "right prepare");
    right.setMap(raw.get());
    right.render(settings, &root, 1u, outputs.data(), 2u, frames);
    require(difference(rendered[0u], rendered[1u]) < 1.0e-4f,
        "right source is dual mono");

    settings.sourceMode = WavesetSourceMode::SumMono;
    SampleWavesetsEngine sum;
    require(sum.prepare(48000.0, 2u), "sum prepare");
    sum.setMap(raw.get());
    sum.render(settings, &root, 1u, outputs.data(), 2u, frames);
    require(difference(rendered[0u], rendered[1u]) < 1.0e-4f,
        "sum source is dual mono");

    const std::array<WavesetRenderEvent, 2u> chord {{
        { 0u, WavesetEventKind::NoteOn, 2u, 64u, 0.8f, 0u },
        { 32u, WavesetEventKind::NoteOn, 3u, 67u, 0.7f, 0u },
    }};
    settings.sourceMode = WavesetSourceMode::TrueChannels;
    engine.render(settings, chord.data(), chord.size(), outputs.data(), 2u,
        frames);
    require(engine.activeVoiceCount() == 3u, "poly mode layers notes");
    require(engine.voiceCursorCount() == 3u
            && engine.voiceCursors()[0u].key == 60u
            && engine.voiceCursors()[1u].key == 64u
            && engine.voiceCursors()[2u].key == 67u
            && engine.voiceCursors()[0u].identity
                != engine.voiceCursors()[1u].identity
            && engine.voiceCursors()[1u].identity
                != engine.voiceCursors()[2u].identity,
        "poly mode publishes an independently labelled playhead per voice");

    constexpr uint32_t routedFrames = 512u;
    std::array<std::vector<float>, kMaximumWavesetOutputChannels>
        routedSignal;
    std::array<float*, kMaximumWavesetOutputChannels> routedOutputs {};
    for (std::size_t channel = 0u; channel < routedSignal.size(); ++channel) {
        routedSignal[channel].resize(routedFrames);
        routedOutputs[channel] = routedSignal[channel].data();
    }
    WavesetSettings routedSettings = settings;
    routedSettings.voiceMode = VoiceMode::Poly;
    routedSettings.playMode = WavesetPlayMode::ForwardLoop;
    routedSettings.sourceMode = WavesetSourceMode::TrueChannels;
    routedSettings.outputRouting.width
        = s3g::routing::OutputVoiceWidth::Mono;
    routedSettings.outputRouting.traversal
        = s3g::routing::OutputTraversal::Sequential;
    routedSettings.activeOutputChannelCount = 32u;
    const std::array<WavesetRenderEvent, 2u> routedChord {{
        { 0u, WavesetEventKind::NoteOn, 21u, 60u, 1.0f, 0u },
        { 32u, WavesetEventKind::NoteOn, 22u, 64u, 1.0f, 0u },
    }};
    SampleWavesetsEngine routed;
    require(routed.prepare(48000.0, 32u), "32-channel prepare");
    routed.setMap(raw.get());
    routed.render(routedSettings, routedChord.data(), routedChord.size(),
        routedOutputs.data(), 32u, routedFrames);
    require(energy(routedSignal[0u]) > 0.1f
            && energy(routedSignal[1u]) > 0.1f
            && energy(routedSignal[2u]) == 0.0f,
        "sequential mono triggers occupy independent output channels");
    require(routed.voiceCursorCount() == 2u
            && routed.voiceCursors()[0u].outputChannelCount == 1u
            && routed.voiceCursors()[0u].outputFirstChannel == 0u
            && routed.voiceCursors()[1u].outputFirstChannel == 1u,
        "voice cursors retain mono output assignments");

    routedSettings.outputRouting.width
        = s3g::routing::OutputVoiceWidth::Stereo;
    routedSettings.outputRouting.pairLayout
        = s3g::routing::StereoPairLayout::Adjacent;
    SampleWavesetsEngine adjacentPairs;
    require(adjacentPairs.prepare(48000.0, 32u),
        "adjacent-pair prepare");
    adjacentPairs.setMap(raw.get());
    adjacentPairs.render(routedSettings, routedChord.data(),
        routedChord.size(), routedOutputs.data(), 32u, routedFrames);
    require(energy(routedSignal[0u]) > 0.1f
            && energy(routedSignal[1u]) > 0.1f
            && energy(routedSignal[2u]) > 0.1f
            && energy(routedSignal[3u]) > 0.1f
            && energy(routedSignal[4u]) == 0.0f
            && difference(routedSignal[0u], routedSignal[1u]) > 0.1f,
        "adjacent stereo routing preserves true-stereo pairs");

    routedSettings.outputRouting.pairLayout
        = s3g::routing::StereoPairLayout::SplitBanks;
    SampleWavesetsEngine splitPairs;
    require(splitPairs.prepare(48000.0, 32u), "split-pair prepare");
    splitPairs.setMap(raw.get());
    splitPairs.render(routedSettings, routedChord.data(), routedChord.size(),
        routedOutputs.data(), 32u, routedFrames);
    require(energy(routedSignal[0u]) > 0.1f
            && energy(routedSignal[16u]) > 0.1f
            && energy(routedSignal[1u]) > 0.1f
            && energy(routedSignal[17u]) > 0.1f
            && energy(routedSignal[2u]) == 0.0f,
        "split-bank stereo routing preserves paired destinations");

    routedSettings.voiceMode = VoiceMode::Mono;
    routedSettings.outputRouting.width
        = s3g::routing::OutputVoiceWidth::Mono;
    routedSettings.outputRouting.pairLayout
        = s3g::routing::StereoPairLayout::Adjacent;
    SampleWavesetsEngine routedMono;
    require(routedMono.prepare(48000.0, 32u),
        "routed mono voice-mode prepare");
    routedMono.setMap(raw.get());
    routedMono.render(routedSettings, routedChord.data(), routedChord.size(),
        routedOutputs.data(), 32u, routedFrames);
    require(routedMono.activeVoiceCount() == 1u
            && routedMono.voiceCursorCount() == 1u
            && routedMono.voiceCursors()[0u].key == 64u
            && routedMono.voiceCursors()[0u].outputFirstChannel == 1u,
        "each monophonic key press advances to a new output channel");

    settings.voiceMode = VoiceMode::Mono;
    SampleWavesetsEngine mono;
    require(mono.prepare(48000.0, 2u), "mono prepare");
    mono.setMap(raw.get());
    mono.render(settings, chord.data(), chord.size(), outputs.data(), 2u,
        frames);
    require(mono.activeVoiceCount() == 1u, "mono mode keeps one voice");

    settings.voiceMode = VoiceMode::Legato;
    SampleWavesetsEngine legato;
    require(legato.prepare(48000.0, 2u), "legato prepare");
    legato.setMap(raw.get());
    legato.render(settings, chord.data(), chord.size(), outputs.data(), 2u,
        frames);
    require(legato.activeVoiceCount() == 1u && legato.primaryKey() == 67u,
        "legato retargets one running voice");

    settings.voiceMode = VoiceMode::Poly;
    settings.advance = WavesetAdvanceMode::Hold;
    SampleWavesetsEngine held;
    require(held.prepare(48000.0, 2u), "hold prepare");
    held.setMap(raw.get());
    held.render(settings, &root, 1u, outputs.data(), 2u, frames);
    require(held.primaryPositionNormalized() < 0.01f,
        "Hold repeats one source group");

    settings.advance = WavesetAdvanceMode::Stretch;
    settings.playMode = WavesetPlayMode::Forward;
    settings.end = 0.02;
    settings.loopEnd = 0.02;
    SampleWavesetsEngine oneShot;
    require(oneShot.prepare(48000.0, 2u), "one-shot prepare");
    oneShot.setMap(raw.get());
    oneShot.render(settings, &root, 1u, outputs.data(), 2u, frames);
    for (uint32_t block = 0u; block < 6u; ++block)
        oneShot.render(settings, nullptr, 0u, outputs.data(), 2u, frames);
    require(oneShot.activeVoiceCount() == 0u,
        "forward mode stops at End");

    const WavesetRenderEvent stop {
        0u, WavesetEventKind::StopAll, 0u, 0u, 0.0f, 0u,
    };
    engine.render(settings, &stop, 1u, outputs.data(), 2u, 256u);
    require(engine.activeVoiceCount() == 0u
            && engine.voiceCursorCount() == 0u,
        "Stop All clears voices and published playheads");

    std::cout << "s3g Sample Wavesets 2 smoke: ok\n";
    return 0;
}
