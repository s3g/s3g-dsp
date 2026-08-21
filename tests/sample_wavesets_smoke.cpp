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
        const double phase = 2.0 * 3.14159265358979323846 * 220.0
            * static_cast<double>(frame) / sampleRate;
        asset->channels[0u][frame] = static_cast<float>(0.7
            * std::sin(phase) + 0.12 * std::sin(phase * 2.0));
        asset->channels[1u][frame] = static_cast<float>(0.55
            * std::sin(phase + 0.15));
    }
    require(asset->valid(), "fixture asset");
    return asset;
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
    require(raw->units.size() > 150u, "waveset count");
    require(raw->asset.get() == source.get(), "immutable source retained");
    const bool hasFractionalBoundary = std::any_of(raw->units.begin(),
        raw->units.end(), [](const WavesetUnit& unit) {
            return std::abs(unit.startPosition
                - std::round(unit.startPosition)) > 1.0e-3;
        });
    require(hasFractionalBoundary, "sub-sample rising crossings retained");
    const double expectedCycle = source->sampleRate / 220.0;
    require(std::abs(raw->units[raw->units.size() / 2u].sampleLength()
            - expectedCycle) < 4.0,
        "waveset spans one rising-crossing cycle");

    WavesetSettings settings;
    settings.advance = WavesetAdvanceMode::Stretch;
    settings.groupSize = 4u;
    settings.repeats = 3u;
    settings.shape = WavesetShape::Repeat;
    settings.processAmount = 0.0f;
    settings.outputGainDecibels = -12.0f;
    settings.crossfader = -1.0;
    settings.positions = {{ 0.0, 0.5 }};

    constexpr uint32_t frames = 4800u;
    std::array<std::vector<float>, 2u> rendered {{
        std::vector<float>(frames), std::vector<float>(frames),
    }};
    std::array<float*, 2u> outputs {{
        rendered[0u].data(), rendered[1u].data(),
    }};
    SampleWavesetsEngine engine;
    engine.prepare(48000.0, 512u);
    engine.setMap(raw.get());
    const WavesetRenderEvent playBoth {
        0u, WavesetEventKind::PlayBoth, 0u, 1.0f,
    };
    engine.render(settings, &playBoth, 1u, outputs.data(), 2u, frames);
    require(engine.outputPeak() > 0.01f, "free-running deck output");
    require(engine.deckPlaying(0u) && engine.deckPlaying(1u),
        "both decks play independently");
    require(engine.activeMask() == 3u, "both deck activity bits");
    require(engine.deckPositionNormalized(0u) > 0.001f,
        "deck A advances through source chronology");
    require(engine.deckPositionNormalized(1u) > 0.5f,
        "deck B advances from its own start position");
    require(engine.deckOutputPhaseNormalized(0u) >= 0.0f
            && engine.deckOutputPhaseNormalized(0u) <= 1.0f,
        "deck output phase");

    const WavesetRenderEvent pauseA {
        0u, WavesetEventKind::PauseDeck, 0u, 0.0f,
    };
    engine.render(settings, &pauseA, 1u, outputs.data(), 2u, frames);
    require(!engine.deckPlaying(0u) && engine.deckPlaying(1u),
        "one deck can pause while the other keeps running");

    // Preserve skips source groups to offset the repeated output duration.
    SampleWavesetsEngine stretch;
    SampleWavesetsEngine preserve;
    stretch.prepare(48000.0, 512u); preserve.prepare(48000.0, 512u);
    stretch.setMap(raw.get()); preserve.setMap(raw.get());
    WavesetSettings timeSettings = settings;
    timeSettings.repeats = 4u;
    const WavesetRenderEvent restartA {
        0u, WavesetEventKind::RestartDeck, 0u, 1.0f,
    };
    stretch.render(timeSettings, &restartA, 1u, outputs.data(), 2u, frames);
    const float stretchPosition = stretch.deckPositionNormalized(0u);
    timeSettings.advance = WavesetAdvanceMode::Preserve;
    preserve.render(timeSettings, &restartA, 1u, outputs.data(), 2u, frames);
    require(preserve.deckPositionNormalized(0u) > stretchPosition * 2.0f,
        "Preserve advances farther than Stretch");

    SampleWavesetsEngine held;
    held.prepare(48000.0, 512u); held.setMap(raw.get());
    timeSettings.advance = WavesetAdvanceMode::Hold;
    timeSettings.positions[1u] = 0.5;
    timeSettings.crossfader = 1.0;
    const WavesetRenderEvent restartB {
        0u, WavesetEventKind::RestartDeck, 1u, 1.0f,
    };
    held.render(timeSettings, &restartB, 1u, outputs.data(), 2u, frames);
    require(std::abs(held.deckPositionNormalized(1u) - 0.5f) < 0.01f,
        "Hold repeats deck B at its selected source position");

    const WavesetRenderEvent stopBoth {
        0u, WavesetEventKind::StopBoth, 0u, 0.0f,
    };
    held.render(timeSettings, &stopBoth, 1u, outputs.data(), 2u, 256u);
    require(held.activeMask() == 0u, "Stop returns both decks to rest");

    timeSettings.shape = WavesetShape::Harmonic;
    timeSettings.processAmount = 0.75f;
    SampleWavesetsEngine processed;
    processed.prepare(48000.0, 512u); processed.setMap(raw.get());
    processed.render(timeSettings, &restartB, 1u, outputs.data(), 2u, frames);
    require(std::isfinite(processed.outputPeak())
            && processed.outputPeak() > 0.001f,
        "waveset process modes remain finite");

    std::cout << "s3g Sample Wavesets smoke: ok\n";
    return 0;
}
