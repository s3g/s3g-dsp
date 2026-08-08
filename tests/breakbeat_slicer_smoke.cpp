#include "s3g_breakbeat_slicer.h"

#include <array>
#include <cmath>
#include <iostream>
#include <memory>

namespace {

using namespace s3g::breakbeat;

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

std::shared_ptr<const SampleAsset> constantAsset(float left, float right,
    uint32_t frames = 4096u, double sampleRate = 48000.0)
{
    auto asset = std::make_shared<SampleAsset>();
    asset->sampleRate = sampleRate;
    asset->channelCount = 2u;
    asset->channels[0u].assign(frames, left);
    asset->channels[1u].assign(frames, right);
    return asset;
}

SampleSlot oneSliceSlot(std::shared_ptr<const SampleAsset> asset,
    LaunchMode launch = LaunchMode::OneShot)
{
    SampleSlot slot;
    slot.asset = std::move(asset);
    slot.sliceCount = 1u;
    slot.slices[0u].startFrame = 0u;
    slot.slices[0u].endFrame = slot.asset->frameCount();
    slot.slices[0u].launchMode = launch;
    slot.envelope.attackProportion = 0.0f;
    slot.envelope.decayProportion = 0.0f;
    slot.envelope.sustain = 1.0f;
    slot.envelope.releaseProportion = 1.0f
        / static_cast<float>(slot.asset->frameCount());
    return slot;
}

void testBankMappingAndValidation()
{
    SampleAsset mismatched;
    mismatched.channelCount = 4u;
    for (uint32_t channel = 0u; channel < 4u; ++channel)
        mismatched.channels[channel].assign(32u, 0.0f);
    mismatched.channels[3u].resize(31u);
    check(!mismatched.valid(),
        "mismatched multichannel lane lengths were accepted");

    BankSnapshot bank;
    check(bank.valid(), "a default empty bank was not valid");
    bank.slots[0u] = oneSliceSlot(constantAsset(0.5f, 0.25f));
    bank.slots[0u].sliceCount = 3u;
    bank.slots[0u].slices[0u].endFrame = 1000u;
    bank.slots[0u].slices[1u] = { 1000u, 2000u };
    bank.slots[0u].slices[2u] = { 2000u, 4096u };
    bank.slots[1u] = oneSliceSlot(constantAsset(0.2f, 0.6f));

    check(mapSlotConsecutively(bank, 0u, 36u)
            && bank.slots[0u].mappedRootNote == 36u
            && bank.slots[0u].mappedSliceCount == 3u,
        "consecutive slice mapping failed");
    check(moveSliceMarker(bank.slots[0u].slices.data(),
            bank.slots[0u].sliceCount, 1u, 900u)
            && bank.slots[0u].mappedRootNote == 36u
            && bank.slots[0u].mappedSliceCount == 3u,
        "moving a marker invalidated an unchanged slice-number mapping");
    bank.slots[0u].midiChannel = 1u;
    bank.slots[1u].midiChannel = 2u;
    check(mapSlotConsecutively(bank, 1u, 36u, false)
            && bank.slots[1u].mappedRootNote == 36u
            && bank.slots[1u].mappedSliceCount == 1u,
        "per-channel overlapping mapping failed");
    check(bank.valid(), "a populated multisample bank was not valid");

    bank.slots[1u].midiChannel = 17u;
    check(!bank.valid(), "an invalid per-break MIDI channel was accepted");
}

void testAnalysisAndSliceEditing()
{
    SampleAsset asset;
    asset.sampleRate = 1000.0;
    asset.channelCount = 2u;
    asset.channels[0u].assign(1000u, 0.0f);
    asset.channels[1u].assign(1000u, 0.0f);
    asset.channels[0u][100u] = 0.2f;
    asset.channels[0u][101u] = 0.9f;
    asset.channels[0u][102u] = 0.3f;
    asset.channels[1u][400u] = -0.2f;
    asset.channels[1u][401u] = -0.75f;
    asset.channels[1u][402u] = -0.25f;

    AnalysisSettings settings;
    settings.maximumPeakCount = 100u;
    settings.minimumTransientLevel = 0.1f;
    settings.transientSensitivity = 1.5f;
    settings.minimumTransientSpacingSeconds = 0.05;
    settings.transientLookaheadSeconds = 0.004;
    const SampleAnalysis analysis = analyzeSample(asset, settings);
    check(analysis.validFor(asset) && analysis.peaks.size() == 100u
            && analysis.transients.size() == 2u
            && analysis.transients[0u].frame == 101u
            && analysis.transients[1u].frame == 401u,
        "bounded waveform or transient analysis failed");
    check(nearestZeroFrame(asset, 101u, 3u) == 99u,
        "nearest zero-frame search was not deterministic");

    auto slices = makeTransientSlices(asset, analysis, 8u, 3u);
    check(slices.size() == 3u && slices[0u].endFrame == 99u
            && slices[1u].startFrame == 99u
            && slices[1u].endFrame == 399u
            && slices[2u].startFrame == 399u,
        "transient slice construction failed");
    std::size_t count = slices.size();
    slices.resize(8u);
    check(addSliceMarker(slices.data(), count, slices.size(), 200u)
            && count == 4u
            && moveSliceMarker(slices.data(), count, 2u, 220u)
            && deleteSliceMarker(slices.data(), count, 2u)
            && count == 3u,
        "non-destructive marker editing failed");

    const auto equal = makeEqualSlices(asset, 16u);
    check(equal.size() == 16u && equal.front().startFrame == 0u
            && equal.back().endFrame == 1000u,
        "equal slice construction failed");
}

void testTwoSlotSampleAccuratePlayback()
{
    BankSnapshot bank;
    bank.slots[0u] = oneSliceSlot(constantAsset(0.8f, 0.4f));
    bank.slots[1u] = oneSliceSlot(constantAsset(0.2f, 0.6f));
    bank.slots[0u].midiChannel = 1u;
    bank.slots[1u].midiChannel = 2u;
    check(mapSlotConsecutively(bank, 0u, 36u)
            && mapSlotConsecutively(bank, 1u, 36u),
        "two-slot fixture mapping failed");

    SlicerEngine engine;
    check(bank.valid() && engine.prepare(48000.0) && engine.setBank(&bank),
        "two-slot engine fixture did not prepare");
    std::array<RenderEvent, 2u> events {{
        { 2u, EventKind::NoteOn, 10u, 36u, 0u, 0.5f, 1u },
        { 4u, EventKind::NoteOn, 11u, 36u, 0u, 1.0f, 2u },
    }};
    std::array<float, 12u> left {};
    std::array<float, 12u> right {};
    engine.render(events.data(), events.size(), left.data(), right.data(),
        static_cast<uint32_t>(left.size()));

    constexpr float center = 0.7071067811865475f;
    check(left[0u] == 0.0f && left[1u] == 0.0f
            && near(left[2u], 0.8f * 0.5f * center)
            && near(right[2u], 0.4f * 0.5f * center),
        "first slot did not start at the exact event frame with velocity");
    check(near(left[4u], (0.8f * 0.5f + 0.2f) * center)
            && near(right[4u], (0.4f * 0.5f + 0.6f) * center)
            && engine.activeVoiceCount() == 2u,
        "second slot did not mix independently with the first slot");

    bank.slots[0u].solo = true;
    bank.slots[0u].mixerGain = 0.5f;
    check(engine.setBank(&bank), "mixer fixture bank was rejected");
    const RenderEvent omni { 0u, EventKind::NoteOn, 12u, 36u, 0u, 1.0f,
        0u };
    left.fill(0.0f);
    right.fill(0.0f);
    engine.render(&omni, 1u, left.data(), right.data(),
        static_cast<uint32_t>(left.size()));
    check(near(left[0u], 0.8f * 0.5f * center)
            && near(right[0u], 0.4f * 0.5f * center)
            && engine.activeVoiceCount() == 1u
            && engine.slotPeak(0u) > 0.0f
            && engine.slotPeak(1u) == 0.0f,
        "per-break mixer gain, solo, or metering failed");
}

void testReversePitchAndPan()
{
    auto mutableAsset = std::make_shared<SampleAsset>();
    mutableAsset->sampleRate = 48000.0;
    mutableAsset->channelCount = 2u;
    mutableAsset->channels[0u].resize(1024u);
    mutableAsset->channels[1u].resize(1024u);
    for (uint32_t frame = 0u; frame < 1024u; ++frame) {
        mutableAsset->channels[0u][frame]
            = static_cast<float>(frame) / 1024.0f;
        mutableAsset->channels[1u][frame]
            = -mutableAsset->channels[0u][frame];
    }
    std::shared_ptr<const SampleAsset> asset = mutableAsset;
    BankSnapshot bank;
    bank.slots[0u] = oneSliceSlot(asset);
    auto& slice = bank.slots[0u].slices[0u];
    slice.reverse = true;
    slice.transposeSemitones = 12.0f;
    slice.pan = -1.0f;
    check(mapSlotConsecutively(bank, 0u, 60u),
        "reverse fixture mapping failed");

    SlicerEngine engine;
    check(engine.prepare(48000.0) && engine.setBank(&bank),
        "reverse fixture did not prepare");
    const RenderEvent event { 0u, EventKind::NoteOn, 20u, 60u, 0u, 1.0f };
    std::array<float, 4u> left {};
    std::array<float, 4u> right {};
    engine.render(&event, 1u, left.data(), right.data(), 4u);
    check(near(left[0u], mutableAsset->channels[0u][1023u])
            && near(left[1u], mutableAsset->channels[0u][1021u])
            && near(left[2u], mutableAsset->channels[0u][1019u])
            && right[0u] == 0.0f && right[1u] == 0.0f,
        "reverse, octave pitch, or hard-left pan playback was incorrect");
}

void testSixteenChannelSliceLock()
{
    auto mutableAsset = std::make_shared<SampleAsset>();
    mutableAsset->sampleRate = 48000.0;
    mutableAsset->channelCount = 16u;
    for (uint32_t channel = 0u; channel < 16u; ++channel) {
        mutableAsset->channels[channel].resize(64u);
        for (uint32_t frame = 0u; frame < 64u; ++frame) {
            mutableAsset->channels[channel][frame]
                = static_cast<float>(channel) * 0.1f
                + static_cast<float>(frame) * 0.001f;
        }
    }
    std::shared_ptr<const SampleAsset> asset = mutableAsset;
    BankSnapshot bank;
    bank.slots[0u] = oneSliceSlot(asset);
    bank.slots[0u].sliceCount = 2u;
    bank.slots[0u].slices[0u] = { 8u, 16u };
    bank.slots[0u].slices[1u] = { 32u, 40u };
    check(mapSlotConsecutively(bank, 0u, 70u),
        "16-channel fixture mapping failed");
    SlicerEngine engine;
    check(engine.prepare(48000.0) && engine.setBank(&bank),
        "16-channel fixture did not prepare");

    std::array<std::array<float, 8u>, 16u> rendered {};
    std::array<float*, 16u> outputPointers {};
    for (std::size_t channel = 0u; channel < rendered.size(); ++channel)
        outputPointers[channel] = rendered[channel].data();
    const RenderEvent first { 2u, EventKind::NoteOn, 50u, 70u, 0u, 1.0f };
    engine.render(&first, 1u, outputPointers.data(),
        static_cast<uint32_t>(outputPointers.size()), 8u);
    bool locked = true;
    for (std::size_t channel = 0u; channel < rendered.size(); ++channel) {
        locked = locked && rendered[channel][0u] == 0.0f
            && rendered[channel][1u] == 0.0f
            && near(rendered[channel][2u],
                mutableAsset->channels[channel][8u])
            && near(rendered[channel][3u],
                mutableAsset->channels[channel][9u]);
    }
    check(locked,
        "16-channel voice did not share one sample-accurate slice clock");

    engine.reset();
    for (auto& channel : rendered) channel.fill(0.0f);
    const RenderEvent second { 0u, EventKind::NoteOn, 51u, 71u, 0u, 1.0f };
    engine.render(&second, 1u, outputPointers.data(),
        static_cast<uint32_t>(outputPointers.size()), 8u);
    locked = true;
    for (std::size_t channel = 0u; channel < rendered.size(); ++channel) {
        for (std::size_t frame = 0u; frame < 4u;
             ++frame) {
            locked = locked && near(rendered[channel][frame],
                mutableAsset->channels[channel][32u + frame]);
        }
    }
    check(locked,
        "moving to another slice displaced one or more source channels");

    bank.slots[0u].slices[0u].launchMode = LaunchMode::Loop;
    bank.slots[0u].slices[0u].loopStartFrame = 8u;
    bank.slots[0u].slices[0u].loopEndFrame = 12u;
    check(engine.setBank(&bank), "16-channel loop bank was rejected");
    for (auto& channel : rendered) channel.fill(0.0f);
    engine.render(&first, 1u, outputPointers.data(),
        static_cast<uint32_t>(outputPointers.size()), 8u);
    constexpr std::array<uint32_t, 6u> expectedFrames {{
        8u, 9u, 10u, 11u, 8u, 9u,
    }};
    locked = true;
    for (std::size_t channel = 0u; channel < rendered.size(); ++channel) {
        for (std::size_t frame = 0u; frame < expectedFrames.size(); ++frame) {
            locked = locked && near(rendered[channel][frame + 2u],
                mutableAsset->channels[channel][expectedFrames[frame]]);
        }
    }
    check(locked,
        "16-channel loop wrap did not remain sample locked");

    std::array<float, 8u> truncatedLeft {};
    std::array<float, 8u> truncatedRight {};
    engine.reset();
    engine.render(&first, 1u, truncatedLeft.data(), truncatedRight.data(),
        static_cast<uint32_t>(truncatedLeft.size()));
    check(std::all_of(truncatedLeft.begin(), truncatedLeft.end(),
                [](float sample) { return sample == 0.0f; })
            && std::all_of(truncatedRight.begin(), truncatedRight.end(),
                [](float sample) { return sample == 0.0f; }),
        "16-channel source was silently truncated to stereo");
}

void testCleanMixKeepsFloatingPointHeadroom()
{
    BankSnapshot bank;
    bank.slots[0u] = oneSliceSlot(constantAsset(1.0f, 1.0f));
    bank.slots[1u] = oneSliceSlot(constantAsset(1.0f, 1.0f));
    check(mapSlotConsecutively(bank, 0u, 60u)
            && mapSlotConsecutively(bank, 1u, 61u),
        "headroom fixture mapping failed");
    SlicerEngine engine;
    check(engine.prepare(48000.0) && engine.setBank(&bank),
        "headroom fixture did not prepare");
    std::array<RenderEvent, 2u> events {{
        { 0u, EventKind::NoteOn, 40u, 60u, 0u, 1.0f },
        { 0u, EventKind::NoteOn, 41u, 61u, 0u, 1.0f },
    }};
    std::array<float, 2u> left {};
    std::array<float, 2u> right {};
    engine.render(events.data(), events.size(), left.data(), right.data(),
        static_cast<uint32_t>(left.size()));
    check(left[0u] > 1.4f && right[0u] > 1.4f,
        "clean engine unexpectedly hard-clipped its polyphonic mix");
}

void testMixerEqAndAuxBus()
{
    BankSnapshot bank;
    bank.slots[0u] = oneSliceSlot(constantAsset(0.24f, 0.18f));
    bank.slots[0u].mixerLowEqDb = 6.0f;
    check(mapSlotConsecutively(bank, 0u, 60u),
        "EQ/aux fixture mapping failed");
    SlicerEngine engine;
    check(engine.prepare(48000.0) && engine.setBank(&bank),
        "EQ/aux fixture did not prepare");
    const RenderEvent event { 0u, EventKind::NoteOn, 80u, 60u, 0u, 1.0f };
    std::array<float, 1024u> dryLeft {};
    std::array<float, 1024u> dryRight {};
    engine.render(&event, 1u, dryLeft.data(), dryRight.data(),
        static_cast<uint32_t>(dryLeft.size()));
    constexpr float center = 0.7071067811865475f;
    check(dryLeft.back() > 0.24f * center * 1.5f
            && dryRight.back() > 0.18f * center * 1.5f,
        "per-break low EQ did not affect the strip output");

    bank.auxEnabled = true;
    bank.slots[0u].mixerAuxSend = 1.0f;
    check(engine.setBank(&bank), "aux bus fixture bank was rejected");
    std::array<float, 1024u> wetLeft {};
    std::array<float, 1024u> wetRight {};
    engine.render(&event, 1u, wetLeft.data(), wetRight.data(),
        static_cast<uint32_t>(wetLeft.size()));
    check(std::abs(wetLeft.back() - dryLeft.back()) > 1.0e-4f
            && std::abs(wetRight.back() - dryRight.back()) > 1.0e-4f
            && engine.auxActivity() > 0.0f
            && engine.auxGainReductionDb() <= 0.0f,
        "post-fader aux send or shared bus processor did not run");
}

void testRealtimeMixerPublicationPreservesPlayback()
{
    BankSnapshot bank;
    bank.slots[0u] = oneSliceSlot(constantAsset(0.4f, 0.2f));
    check(mapSlotConsecutively(bank, 0u, 60u),
        "realtime mixer fixture mapping failed");
    SlicerEngine engine;
    check(engine.prepare(48000.0) && engine.setBank(&bank),
        "realtime mixer fixture did not prepare");
    const RenderEvent event { 0u, EventKind::NoteOn, 90u, 60u, 0u, 1.0f };
    std::array<float, 8u> firstLeft {};
    std::array<float, 8u> firstRight {};
    engine.render(&event, 1u, firstLeft.data(), firstRight.data(),
        static_cast<uint32_t>(firstLeft.size()));
    const float firstPlayhead = engine.slotPlayheadNormalized(0u);

    MixerSnapshot mixer = mixerSnapshotFromBank(bank);
    mixer.strips[0u].gain = 0.5f;
    engine.setPreparedMixer(&mixer);
    std::array<float, 8u> secondLeft {};
    std::array<float, 8u> secondRight {};
    engine.render(nullptr, 0u, secondLeft.data(), secondRight.data(),
        static_cast<uint32_t>(secondLeft.size()));
    constexpr float center = 0.7071067811865475f;
    check(engine.activeVoiceCount() == 1u
            && engine.slotPlayheadNormalized(0u) > firstPlayhead
            && near(secondLeft[0u], 0.4f * center * 0.5f)
            && near(secondRight[0u], 0.2f * center * 0.5f),
        "publishing mixer state reset or restarted the active sample voice");

    mixer.strips[0u].gain = 1.0f;
    mixer.strips[0u].lowEqDb = 12.0f;
    engine.setPreparedMixer(&mixer);
    std::array<float, 64u> eqLeft {};
    std::array<float, 64u> eqRight {};
    const float beforeEqAdoption = engine.slotPlayheadNormalized(0u);
    engine.render(nullptr, 0u, eqLeft.data(), eqRight.data(),
        static_cast<uint32_t>(eqLeft.size()));
    check(engine.activeVoiceCount() == 1u
            && engine.slotPlayheadNormalized(0u) > beforeEqAdoption
            && eqLeft.back() > 0.4f * center * 1.25f
            && eqRight.back() > 0.2f * center * 1.25f,
        "post-playback EQ did not update an already-playing sample voice");

    BankSnapshot editedBank = bank;
    editedBank.slots[0u].rootNote = 61u;
    engine.setPreparedBank(&editedBank);
    const float beforeBankAdoption = engine.slotPlayheadNormalized(0u);
    engine.render(nullptr, 0u, secondLeft.data(), secondRight.data(),
        static_cast<uint32_t>(secondLeft.size()));
    check(engine.activeVoiceCount() == 1u
            && engine.slotPlayheadNormalized(0u) > beforeBankAdoption,
        "prepared sample-bank adoption ended an already-playing voice");
}

void testBreakBusCore()
{
    s3g::BreakBus bus;
    s3g::BreakBusParams params;
    params.press = 1.0f;
    params.snap = 0.0f;
    params.recovery = 0.25f;
    params.saturation = 0.0f;
    params.bite = 0.0f;
    params.clip = 0.0f;
    params.tilt = 0.0f;
    params.linkMode = s3g::BreakBusLinkMode::All;
    params.fieldSafe = true;
    check(bus.prepare(48000.0) && bus.setParams(params),
        "Break Bus linked dynamics did not prepare");
    bus.beginBlock();
    std::array<float, 4u> linked {{ 0.2f, 0.4f, 0.6f, 0.8f }};
    for (uint32_t frame = 0u; frame < 2048u; ++frame) {
        const float sign = (frame & 1u) == 0u ? 1.0f : -1.0f;
        linked = {{ sign * 0.2f, sign * 0.4f,
            sign * 0.6f, sign * 0.8f }};
        bus.processFrame(linked.data(), static_cast<uint32_t>(linked.size()));
    }
    check(near(linked[1u] / linked[0u], 2.0f, 1.0e-4f)
            && near(linked[2u] / linked[0u], 3.0f, 1.0e-4f)
            && near(linked[3u] / linked[0u], 4.0f, 1.0e-4f)
            && bus.activity() > 0.0f && bus.gainReductionDb() < -1.0f,
        "ALL-linked field-safe dynamics changed channel relationships");

    params.press = 0.2f;
    params.saturation = 0.8f;
    params.bite = 0.7f;
    params.clip = 0.9f;
    params.tilt = 0.3f;
    params.linkMode = s3g::BreakBusLinkMode::Pair;
    params.fieldSafe = false;
    check(bus.setParams(params), "Break Bus nonlinear mode was rejected");
    bus.reset();
    bus.beginBlock();
    std::array<float, 4u> colored {{ 2.0f, -1.7f, 1.4f, -1.1f }};
    for (uint32_t frame = 0u; frame < 256u; ++frame) {
        colored = {{ 2.0f, -1.7f, 1.4f, -1.1f }};
        bus.processFrame(colored.data(),
            static_cast<uint32_t>(colored.size()));
    }
    check(std::all_of(colored.begin(), colored.end(), [](float sample) {
                return std::isfinite(sample) && std::abs(sample) <= 1.31f;
            })
            && std::abs(colored[0u] - 2.0f) > 0.1f,
        "Break Bus SAT/BITE/ADAA clip chain was inactive or unbounded");
}

void testWideOutputSilencesUnusedChannels()
{
    BankSnapshot bank;
    bank.slots[0u] = oneSliceSlot(constantAsset(0.5f, 0.25f, 32u));
    check(mapSlotConsecutively(bank, 0u, 60u),
        "stereo-to-wide fixture mapping failed");
    SlicerEngine engine;
    check(engine.prepare(48000.0) && engine.setBank(&bank),
        "stereo-to-wide fixture did not prepare");
    std::array<std::array<float, 8u>, 16u> rendered {};
    for (auto& channel : rendered) channel.fill(1.0f);
    std::array<float*, 16u> outputs {};
    for (std::size_t channel = 0u; channel < rendered.size(); ++channel)
        outputs[channel] = rendered[channel].data();
    const RenderEvent event { 0u, EventKind::NoteOn, 1u, 60u, 0u, 1.0f };
    engine.render(&event, 1u, outputs.data(),
        static_cast<uint32_t>(outputs.size()), 8u);
    bool unusedSilent = rendered[0u][0u] != 0.0f
        && rendered[1u][0u] != 0.0f;
    for (std::size_t channel = 2u; channel < rendered.size(); ++channel) {
        unusedSilent = unusedSilent && std::all_of(
            rendered[channel].begin(), rendered[channel].end(),
            [](float sample) { return sample == 0.0f; });
    }
    check(unusedSilent,
        "fixed wide output did not clear unused source channels");
}

void testProportionalSliceEnvelope()
{
    BankSnapshot bank;
    bank.slots[0u] = oneSliceSlot(
        constantAsset(1.0f, 1.0f, 150u, 1000.0));
    bank.slots[0u].sliceCount = 2u;
    bank.slots[0u].slices[0u] = { 0u, 100u };
    bank.slots[0u].slices[1u] = { 100u, 150u };
    auto& envelope = bank.slots[0u].envelope;
    envelope.attackProportion = 0.10f;
    envelope.decayProportion = 0.10f;
    envelope.sustain = 0.50f;
    envelope.releaseProportion = 0.20f;
    check(mapSlotConsecutively(bank, 0u, 60u) && bank.valid(),
        "proportional envelope fixture was rejected");

    SlicerEngine engine;
    check(engine.prepare(1000.0) && engine.setBank(&bank),
        "proportional envelope engine did not prepare");
    const RenderEvent event { 0u, EventKind::NoteOn, 40u, 60u, 0u, 1.0f };
    std::array<float, 100u> left {};
    std::array<float, 100u> right {};
    engine.render(&event, 1u, left.data(), right.data(),
        static_cast<uint32_t>(left.size()));

    constexpr float center = 0.7071067811865475f;
    check(left[0u] == 0.0f
            && near(left[5u], center * 0.5f)
            && near(left[10u], center)
            && near(left[20u], center * 0.5f)
            && near(left[90u], center * 0.5f * (9.0f / 19.0f))
            && left[99u] == 0.0f,
        "one-shot ADSR stages were not proportional to slice duration");

    engine.reset();
    const RenderEvent shortEvent {
        0u, EventKind::NoteOn, 41u, 61u, 0u, 1.0f,
    };
    std::array<float, 50u> shortLeft {};
    std::array<float, 50u> shortRight {};
    engine.render(&shortEvent, 1u, shortLeft.data(), shortRight.data(),
        static_cast<uint32_t>(shortLeft.size()));
    check(shortLeft[0u] == 0.0f
            && near(shortLeft[5u], center)
            && near(shortLeft[10u], center * 0.5f)
            && near(shortLeft[45u], center * 0.5f * (4.0f / 9.0f))
            && shortLeft[49u] == 0.0f,
        "the break envelope did not scale across different slice lengths");

    envelope.attackProportion = 0.6f;
    envelope.decayProportion = 0.3f;
    envelope.releaseProportion = 0.2f;
    check(!bank.valid(),
        "an envelope longer than its normalized slice duration was accepted");
}

void testLoopGateAndChoke()
{
    BankSnapshot bank;
    bank.slots[0u] = oneSliceSlot(
        constantAsset(1.0f, 1.0f, 32u, 1000.0), LaunchMode::Loop);
    bank.slots[0u].slices[0u].loopStartFrame = 2u;
    bank.slots[0u].slices[0u].loopEndFrame = 6u;
    bank.slots[0u].slices[0u].chokeGroup = 1u;
    bank.slots[0u].sliceCount = 2u;
    bank.slots[0u].slices[1u] = bank.slots[0u].slices[0u];
    bank.slots[0u].slices[1u].startFrame = 8u;
    bank.slots[0u].slices[1u].endFrame = 32u;
    bank.slots[0u].slices[1u].loopStartFrame = 10u;
    bank.slots[0u].slices[1u].loopEndFrame = 14u;
    check(mapSlotConsecutively(bank, 0u, 40u),
        "loop fixture mapping failed");

    SlicerEngine engine;
    check(engine.prepare(1000.0) && engine.setBank(&bank),
        "loop fixture did not prepare");
    std::array<RenderEvent, 3u> events {{
        { 0u, EventKind::NoteOn, 30u, 40u, 0u, 1.0f },
        { 8u, EventKind::NoteOn, 31u, 41u, 0u, 0.5f },
        { 12u, EventKind::NoteOff, 31u, 41u, 0u, 0.0f },
    }};
    std::array<float, 18u> left {};
    std::array<float, 18u> right {};
    engine.render(events.data(), events.size(), left.data(), right.data(),
        static_cast<uint32_t>(left.size()));
    check(left[7u] > 0.6f
            && near(left[8u], 1.5f * 0.7071067811865475f)
            && near(left[10u], 0.5f * 0.7071067811865475f),
        "same-group retrigger did not fade and replace the first loop voice");
    check(left[12u] > 0.3f && left[13u] == 0.0f
            && engine.activeVoiceCount() == 0u,
        "loop note-off did not apply the configured release");
}

} // namespace

int main()
{
    testBankMappingAndValidation();
    testAnalysisAndSliceEditing();
    testTwoSlotSampleAccuratePlayback();
    testReversePitchAndPan();
    testSixteenChannelSliceLock();
    testWideOutputSilencesUnusedChannels();
    testCleanMixKeepsFloatingPointHeadroom();
    testMixerEqAndAuxBus();
    testRealtimeMixerPublicationPreservesPlayback();
    testBreakBusCore();
    testProportionalSliceEnvelope();
    testLoopGateAndChoke();
    if (failures == 0)
        std::cout << "breakbeat slicer smoke tests passed\n";
    return failures == 0 ? 0 : 1;
}
