#include "s3g_sample_rings.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {

using namespace s3g::sample;

SampleAsset makeAsset(uint8_t channels, uint32_t frames, float base)
{
    SampleAsset asset;
    asset.sampleRate = 48000.0;
    asset.channelCount = channels;
    for (uint8_t channel = 0u; channel < channels; ++channel) {
        asset.channels[channel].resize(frames);
        for (uint32_t frame = 0u; frame < frames; ++frame) {
            asset.channels[channel][frame] = base
                + static_cast<float>(channel + 1u) * 0.03f
                + 0.2f * std::sin(static_cast<float>(frame)
                    * 0.031f * static_cast<float>(channel + 1u));
        }
    }
    return asset;
}

float energy(const std::vector<float>& channel)
{
    float result = 0.0f;
    for (float value : channel) {
        if (!std::isfinite(value)) return -1.0f;
        result += value * value;
    }
    return result;
}

bool render(SampleRingsEngine& engine,
    const SampleRingsSettings& settings,
    std::array<std::vector<float>, 8u>& output, uint32_t frames = 512u,
    bool playing = true)
{
    std::array<float*, 8u> pointers {};
    for (std::size_t channel = 0u; channel < output.size(); ++channel) {
        output[channel].assign(frames, 0.0f);
        pointers[channel] = output[channel].data();
    }
    engine.render(settings, pointers.data(), 8u, frames, playing);
    for (const auto& channel : output)
        if (energy(channel) < 0.0f) return false;
    return true;
}

} // namespace

int main()
{
    SampleRingsEngine engine;
    if (!engine.prepare(48000.0)) return 1;

    auto mono = makeAsset(1u, 4096u, 0.01f);
    auto stereo = makeAsset(2u, 4096u, 0.05f);
    std::array<SampleAsset, 4u> fields {{
        makeAsset(8u, 4096u, 0.10f),
        makeAsset(8u, 5120u, 0.20f),
        makeAsset(8u, 6144u, 0.30f),
        makeAsset(8u, 7168u, 0.40f),
    }};

    SampleRingsSettings settings;
    settings.outputGainDecibels = -6.0f;
    settings.slots[0u].gainDecibels = 0.0f;
    settings.ringPath = SampleRingsRingPath::Fixed;
    settings.formation = SampleRingsHeadFormation::Field8;
    settings.ringPosition = 0.0f;
    settings.pathSlewMilliseconds = 0.0f;
    std::array<std::vector<float>, 8u> output;

    if (!engine.setAsset(0u, &mono) || !render(engine, settings, output))
        return 1;
    for (const auto& channel : output) {
        if (!(energy(channel) > 0.0f)) {
            std::cerr << "mono field formation left an output silent\n";
            return 1;
        }
    }
    if (!render(engine, settings, output, 512u, false)) return 1;
    for (const auto& channel : output)
        if (energy(channel) != 0.0f) {
            std::cerr << "paused engine was not silent\n";
            return 1;
        }

    engine.setPreparedAsset(0u, &stereo);
    settings.formation = SampleRingsHeadFormation::Pairs;
    engine.resync(settings);
    if (!render(engine, settings, output)) return 1;
    const auto pairCursors = engine.cursors();
    for (std::size_t head = 0u; head < pairCursors.size(); head += 2u) {
        if (pairCursors[head].formationLeader != head
            || pairCursors[head + 1u].formationLeader != head
            || pairCursors[head].channelA != 0u
            || pairCursors[head + 1u].channelA != 1u
            || std::abs(pairCursors[head].radialPosition
                - pairCursors[head + 1u].radialPosition) > 1.0e-5f
            || std::abs(pairCursors[head].phase
                - pairCursors[head + 1u].phase) < 1.0e-3f) {
            std::cerr << "pair formation did not link radial motion while preserving head clocks\n";
            return 1;
        }
    }

    for (std::size_t slot = 0u; slot < fields.size(); ++slot) {
        engine.setPreparedAsset(slot, &fields[slot]);
        settings.slots[slot].gainDecibels = -6.0f;
    }
    settings.formation = SampleRingsHeadFormation::Field8;
    settings.ringPosition = 0.0f;
    engine.resync(settings);
    if (!render(engine, settings, output)) return 1;
    for (std::size_t head = 0u; head < kSampleRingsHeadCount; ++head) {
        const auto& cursor = engine.cursors()[head];
        if (cursor.sourceA != 0u || cursor.channelA != head
            || cursor.formationLeader != 0u) {
            std::cerr << "field A channel correspondence mismatch\n";
            return 1;
        }
    }

    settings.ringPosition = 1.0f;
    if (!render(engine, settings, output, 1u)) return 1;
    if (engine.cursors()[0u].sourceA != 3u
        || !(engine.cursors()[0u].phaseA > 0.05f)) {
        std::cerr << "unselected source clocks did not run continuously\n";
        return 1;
    }

    settings.ringPosition = 0.5f;
    settings.ringBlend = 1.0f;
    if (!render(engine, settings, output)) return 1;
    bool matchedFieldBlend = true;
    for (std::size_t head = 0u; head < kSampleRingsHeadCount; ++head) {
        const auto& cursor = engine.cursors()[head];
        matchedFieldBlend = matchedFieldBlend && cursor.sourceA == 1u
            && cursor.sourceB == 2u && cursor.channelA == head
            && cursor.channelB == head && cursor.sourceMix > 0.4f
            && cursor.sourceMix < 0.6f;
    }
    if (!matchedFieldBlend) {
        std::cerr << "field formation did not crossfade matching channels\n";
        return 1;
    }

    settings.formation = SampleRingsHeadFormation::Free;
    settings.ringPath = SampleRingsRingPath::Manual;
    for (std::size_t head = 0u; head < kSampleRingsHeadCount; ++head)
        settings.manualRings[head] = static_cast<float>(head * 4u)
            / 31.0f;
    engine.resync(settings);
    if (!render(engine, settings, output)) return 1;
    for (std::size_t head = 0u; head < kSampleRingsHeadCount; ++head) {
        const uint8_t expectedRing = static_cast<uint8_t>(head * 4u);
        const auto& cursor = engine.cursors()[head];
        const bool useB = cursor.sourceMix >= 0.5f;
        const uint8_t ring = useB ? cursor.ringB : cursor.ringA;
        const uint8_t source = useB ? cursor.sourceB : cursor.sourceA;
        const uint8_t channel = useB ? cursor.channelB : cursor.channelA;
        if (ring != expectedRing || source != expectedRing / 8u
            || channel != expectedRing % 8u) {
            std::cerr << "manual free-ring assignment mismatch\n";
            return 1;
        }
    }

    settings.formation = SampleRingsHeadFormation::Field8;
    settings.ringPath = SampleRingsRingPath::Outward;
    settings.ringPosition = 0.5f;
    settings.pathDepth = 1.0f;
    settings.radialRatio = 1.0f;
    settings.reverseRadialPath = false;
    engine.resync(settings);
    if (!render(engine, settings, output, 512u)) return 1;
    const float outwardPosition = engine.cursors()[0u].radialPosition;
    settings.reverseRadialPath = true;
    engine.resync(settings);
    if (!render(engine, settings, output, 512u)) return 1;
    const float reversedPosition = engine.cursors()[0u].radialPosition;
    if (!(outwardPosition < 0.2f && reversedPosition > 0.8f)) {
        std::cerr << "radial reverse did not reverse path time\n";
        return 1;
    }

    settings.reverseRadialPath = false;
    settings.ringPath = SampleRingsRingPath::Fixed;
    settings.ringPosition = 0.0f;
    settings.relationship = SampleRingsRelationship::Unison;
    settings.reverseAngularMotion = false;
    engine.resync(settings);
    if (!render(engine, settings, output, 256u)) return 1;
    const auto forwardAngular = engine.cursors()[0u];
    settings.reverseAngularMotion = true;
    engine.resync(settings);
    if (!render(engine, settings, output, 256u)) return 1;
    const auto reversedAngular = engine.cursors()[0u];
    if (!(forwardAngular.phaseA > 0.01f && forwardAngular.phaseA < 0.1f
        && reversedAngular.phaseA > 0.9f
        && forwardAngular.rate > 0.0f && reversedAngular.rate < 0.0f)) {
        std::cerr << "angular reverse did not reverse visible loop motion\n";
        return 1;
    }
    settings.slots[0u].reverse = true;
    engine.resync(settings);
    if (!render(engine, settings, output, 256u)) return 1;
    if (!(engine.cursors()[0u].phaseA < 0.1f
        && engine.cursors()[0u].rate > 0.0f)) {
        std::cerr << "slot and angular reverse did not compose visibly\n";
        return 1;
    }
    settings.slots[0u].reverse = false;

    settings.relationship = SampleRingsRelationship::Manual;
    settings.relationshipGlideMilliseconds = 0.0f;
    settings.manualPhases[0u] = 0.25f;
    settings.manualRates[0u] = -1.0f;
    settings.reverseAngularMotion = false;
    engine.resync(settings);
    if (!render(engine, settings, output, 256u)) return 1;
    if (!(engine.cursors()[0u].phaseA > 0.15f
        && engine.cursors()[0u].phaseA < 0.25f
        && engine.cursors()[0u].rate < 0.0f)) {
        std::cerr << "negative manual rate did not counter-rotate its head\n";
        return 1;
    }
    settings.manualRates[0u] = 0.0f;
    engine.resync(settings);
    if (!render(engine, settings, output, 256u)) return 1;
    if (std::abs(engine.cursors()[0u].phaseA - 0.25f) > 1.0e-5f
        || std::abs(engine.cursors()[0u].rate) > 1.0e-6f) {
        std::cerr << "zero manual rate did not stop its head\n";
        return 1;
    }
    settings.manualRates[0u] = -1.0f;
    settings.reverseAngularMotion = true;
    engine.resync(settings);
    if (!render(engine, settings, output, 256u)) return 1;
    if (!(engine.cursors()[0u].phaseA > 0.25f
        && engine.cursors()[0u].rate > 0.0f)) {
        std::cerr << "angular reverse did not compose with signed manual rate\n";
        return 1;
    }

    settings.reverseAngularMotion = false;
    settings.relationship = SampleRingsRelationship::Fan;
    settings.relationshipAmount = 1.0f;
    engine.resync(settings);
    if (!render(engine, settings, output, 1u)) return 1;
    const float positiveFanLow = engine.cursors()[0u].rate;
    const float positiveFanHigh = engine.cursors()[7u].rate;
    settings.relationshipAmount = -1.0f;
    if (!render(engine, settings, output, 1u)) return 1;
    if (!(positiveFanLow < positiveFanHigh
        && engine.cursors()[0u].rate > engine.cursors()[7u].rate)) {
        std::cerr << "negative angle amount did not mirror fan ordering\n";
        return 1;
    }

    settings.relationship = SampleRingsRelationship::Ratio;
    settings.relationshipAmount = 1.0f;
    engine.resync(settings);
    if (!render(engine, settings, output, 1u)) return 1;
    const float positiveRatioLow = engine.cursors()[0u].rate;
    const float positiveRatioHigh = engine.cursors()[7u].rate;
    settings.relationshipAmount = -1.0f;
    if (!render(engine, settings, output, 1u)) return 1;
    if (!(positiveRatioLow < positiveRatioHigh
        && engine.cursors()[0u].rate > engine.cursors()[7u].rate)) {
        std::cerr << "negative angle amount did not mirror ratio ordering\n";
        return 1;
    }

    settings.ringPath = SampleRingsRingPath::Outward;
    settings.ringPosition = 0.5f;
    settings.pathDepth = 1.0f;
    settings.pathOffset = 0.0f;
    settings.pathSpread = 0.0f;
    settings.pathSlewMilliseconds = 0.0f;
    settings.radialRatio = 1.0f;
    settings.playbackRate = 1.0f;
    settings.reverseRadialPath = false;
    settings.reverseAngularMotion = false;
    settings.relationship = SampleRingsRelationship::Unison;
    settings.relationshipAmount = 0.0f;
    engine.resync(settings);
    if (!render(engine, settings, output, 48000u)) return 1;
    const float normalPathPhase = engine.cursors()[0u].pathPhase;
    const float normalAngularRate = engine.cursors()[0u].rate;
    settings.playbackRate = 2.0f;
    engine.resync(settings);
    if (!render(engine, settings, output, 48000u)) return 1;
    const float doubledPathPhase = engine.cursors()[0u].pathPhase;
    const float doubledAngularRate = engine.cursors()[0u].rate;
    if (std::abs(normalPathPhase - 0.125f) > 0.002f
        || std::abs(doubledPathPhase - 0.25f) > 0.002f
        || std::abs(doubledPathPhase - normalPathPhase * 2.0f) > 0.002f
        || std::abs(doubledAngularRate
            - normalAngularRate * 2.0f) > 0.002f) {
        std::cerr << "playback rate did not scale radial and angular clocks together\n";
        return 1;
    }
    settings.playbackRate = 1.0f;
    engine.setPreparedAsset(0u, &fields[3u]);
    engine.resync(settings);
    if (!render(engine, settings, output, 48000u)) return 1;
    if (std::abs(engine.cursors()[0u].pathPhase
            - normalPathPhase) > 0.002f) {
        std::cerr << "radial clock still depended on the first source duration\n";
        return 1;
    }
    engine.setPreparedAsset(0u, &fields[0u]);

    settings.playbackRate = 1.0f;
    settings.radialRatio = 1.0f;
    settings.pathOffset = 0.0f;
    settings.pathSpread = 1.0f;
    settings.pathDepth = 1.0f;
    settings.ringPosition = 0.5f;
    settings.pathSlewMilliseconds = 0.0f;
    settings.formation = SampleRingsHeadFormation::Field8;
    settings.ringPath = SampleRingsRingPath::Outward;
    engine.resync(settings);
    if (!render(engine, settings, output, 1u)) return 1;
    if (engine.cursors()[0u].radialPosition > 0.01f) {
        std::cerr << "field-8 formation spread shifted its single radial group\n";
        return 1;
    }

    settings.pathSpread = 0.0f;
    settings.ringPath = SampleRingsRingPath::Fixed;
    engine.resync(settings);
    if (!render(engine, settings, output, 48000u)) return 1;
    if (std::abs(engine.cursors()[0u].pathPhase) > 1.0e-6f) {
        std::cerr << "fixed ring advanced a hidden radial clock\n";
        return 1;
    }
    settings.ringPath = SampleRingsRingPath::Manual;
    if (!render(engine, settings, output, 48000u)) return 1;
    if (std::abs(engine.cursors()[0u].pathPhase) > 1.0e-6f) {
        std::cerr << "manual ring advanced a hidden radial clock\n";
        return 1;
    }
    settings.ringPath = SampleRingsRingPath::Outward;
    if (!render(engine, settings, output, 1u)) return 1;
    if (std::abs(engine.cursors()[0u].pathPhase) > 1.0e-6f) {
        std::cerr << "moving path resumed from an inactive hidden phase\n";
        return 1;
    }

    settings.ringPath = SampleRingsRingPath::Fixed;
    settings.formation = SampleRingsHeadFormation::Field8;
    settings.relationship = SampleRingsRelationship::Canon;
    settings.relationshipAmount = 0.5f;
    settings.ringPosition = 0.5f;
    settings.pathSpread = 0.0f;
    engine.resync(settings);
    const auto stoppedCursors = engine.cursors();
    if (!stoppedCursors[0u].active
        || std::abs(stoppedCursors[0u].phaseA) > 1.0e-6f
        || std::abs(stoppedCursors[7u].phaseA - 0.4375f) > 1.0e-5f) {
        std::cerr << "transport relaunch did not expose its start-state cursors\n";
        return 1;
    }
    if (!render(engine, settings, output, 512u, false)) return 1;
    if (!engine.cursors()[0u].active
        || std::abs(engine.cursors()[0u].phaseA
            - stoppedCursors[0u].phaseA) > 1.0e-6f
        || std::abs(engine.cursors()[7u].phaseA
            - stoppedCursors[7u].phaseA) > 1.0e-6f) {
        std::cerr << "paused transport did not preserve visible head positions\n";
        return 1;
    }

    settings.headMask = 0x55u;
    if (!render(engine, settings, output)) return 1;
    for (std::size_t head = 0u; head < output.size(); ++head) {
        if (((settings.headMask >> head) & 1u) == 0u
            && energy(output[head]) != 0.0f) {
            std::cerr << "head mask leaked audio\n";
            return 1;
        }
    }

    std::cout << "Sample Rings smoke passed\n";
    return 0;
}
