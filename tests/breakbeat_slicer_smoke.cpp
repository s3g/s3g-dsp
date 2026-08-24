#include "s3g_breakbeat_slicer.h"

#include <algorithm>
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

    BankSnapshot initialized;
    initializeEmptyBank(initialized);
    bool initializedRouting = initialized.valid();
    for (std::size_t index = 0u; index < initialized.slots.size(); ++index) {
        const auto& slot = initialized.slots[index];
        initializedRouting = initializedRouting
            && slot.rootNote == 48u && slot.mappedRootNote == 48u
            && slot.midiChannel == index + 1u;
    }
    check(initializedRouting,
        "empty bank did not default to C2/start 48 and MIDI channels 1-4");
    check(maximumSlicesForStartNote(0u) == 128u
            && maximumSlicesForStartNote(48u) == 80u
            && maximumSlicesForStartNote(127u) == 1u,
        "start-note-derived slice capacities were incorrect");

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

    BankSnapshot fitted = bank;
    fitted.slots[0u].rootNote = 127u;
    fitted.slots[0u].mappedSliceCount = 0u;
    check(!autoMapSlotConsecutively(fitted, 0u)
            && fitted.slots[0u].rootNote == 127u
            && fitted.slots[0u].mappedSliceCount == 0u,
        "auto map moved or accepted an overflowing start-note range");

    BankSnapshot fullRange;
    initializeEmptyBank(fullRange);
    fullRange.slots[0u] = oneSliceSlot(constantAsset(0.5f, 0.25f));
    fullRange.slots[0u].rootNote = 0u;
    fullRange.slots[0u].mappedRootNote = 0u;
    fullRange.slots[0u].midiChannel = 1u;
    const auto fullSlices = makeEqualSlices(*fullRange.slots[0u].asset,
        kMaximumSlicesPerSlot);
    fullRange.slots[0u].sliceCount = static_cast<uint16_t>(
        fullSlices.size());
    std::copy(fullSlices.begin(), fullSlices.end(),
        fullRange.slots[0u].slices.begin());
    fullRange.slots[0u].mappedSliceCount = 0u;
    check(fullSlices.size() == 128u
            && autoMapSlotConsecutively(fullRange, 0u)
            && fullRange.slots[0u].mappedRootNote == 0u
            && fullRange.slots[0u].mappedSliceCount == 128u
            && fullRange.valid(),
        "start note 0 did not expose and map all 128 MIDI notes");

    bank.slots[1u].midiChannel = 17u;
    check(!bank.valid(), "an invalid per-break MIDI channel was accepted");
}

void testSourceSafeMutationVariations()
{
    BankSnapshot bank;
    initializeEmptyBank(bank);
    bank.slots[0u] = oneSliceSlot(constantAsset(0.5f, 0.25f));
    bank.slots[0u].sliceCount = 4u;
    for (uint32_t index = 0u; index < 4u; ++index) {
        bank.slots[0u].slices[index].startFrame = index * 1024u;
        bank.slots[0u].slices[index].endFrame = (index + 1u) * 1024u;
    }
    bank.slots[0u].inserts[0u] = defaultInsertSettings(
        InsertType::Degrade);
    bank.slots[0u].mixerAuxSend = 0.42f;
    check(mapSlotConsecutively(bank, 0u, 48u) && bank.valid(),
        "mutation fixture did not map");

    const auto originalAsset = bank.slots[0u].asset;
    const MutationVariation original = captureMutationVariation(
        bank.slots[0u]);
    MutationVariation first = original;
    MutationVariation second = original;
    check(original.occupied && original.sliceCount == 4u
            && original.validFor(*originalAsset),
        "captured mutation variation was incomplete");
    check(mutateVariation(first, *originalAsset, 1977u, 0.65f)
            && mutateVariation(second, *originalAsset, 1977u, 0.65f)
            && std::memcmp(first.slices.data(), second.slices.data(),
                sizeof(Slice) * first.sliceCount) == 0,
        "mutation variation was not deterministic for a stored seed");
    bool differs = false;
    for (std::size_t index = 0u; index < first.sliceCount; ++index) {
        differs = differs
            || first.slices[index].gain != original.slices[index].gain
            || first.slices[index].transposeSemitones
                != original.slices[index].transposeSemitones
            || first.slices[index].reverse
                != original.slices[index].reverse
            || first.slices[index].pan != original.slices[index].pan;
    }
    check(differs, "mutation recipe did not change any slice treatment");
    check(applyMutationVariation(bank.slots[0u], first)
            && bank.slots[0u].asset == originalAsset
            && bank.slots[0u].mappedSliceCount == 4u
            && bank.slots[0u].inserts[0u].type == InsertType::Degrade
            && near(bank.slots[0u].mixerAuxSend, 0.42f)
            && bank.valid(),
        "recalling a variation replaced audio or lost routing/processing");
    check(applyMutationVariation(bank.slots[0u], original)
            && bank.slots[0u].asset == originalAsset
            && std::memcmp(bank.slots[0u].slices.data(),
                original.slices.data(), sizeof(Slice) * original.sliceCount)
                == 0,
        "variation A did not restore the original slice treatment");

    auto wideAsset = std::make_shared<SampleAsset>();
    wideAsset->sampleRate = 48000.0;
    wideAsset->channelCount = 4u;
    for (uint32_t channel = 0u; channel < 4u; ++channel)
        wideAsset->channels[channel].assign(256u, 0.1f * (channel + 1u));
    const SampleSlot wideSlot = oneSliceSlot(wideAsset);
    MutationVariation panOnly = captureMutationVariation(wideSlot);
    const MutationVariation unchanged = panOnly;
    check(mutateVariation(panOnly, *wideAsset, 41u, 1.0f, MutationPan)
            && std::memcmp(panOnly.slices.data(), unchanged.slices.data(),
                sizeof(Slice) * panOnly.sliceCount) == 0,
        "wide-source pan mutation altered an unrelated slice property");
}

void testStructuralMutationUses()
{
    const auto asset = constantAsset(0.5f, 0.25f, 8192u);
    SampleSlot slot = oneSliceSlot(asset);
    slot.sliceCount = 8u;
    for (uint32_t index = 0u; index < slot.sliceCount; ++index) {
        slot.slices[index].startFrame = index * 1024u;
        slot.slices[index].endFrame = (index + 1u) * 1024u;
    }
    const MutationVariation original = captureMutationVariation(slot);
    MutationVariation first = original;
    MutationVariation second = original;
    check(structurallyMutateVariation(first, *asset, 90210u, 0.72f,
              80u)
            && structurallyMutateVariation(second, *asset, 90210u,
                0.72f, 80u)
            && first.sliceCount > original.sliceCount
            && first.sliceCount <= 80u
            && std::memcmp(first.slices.data(), second.slices.data(),
                sizeof(first.slices)) == 0,
        "structural mutation was not deterministic or did not repeat hits");
    bool orderChanged = first.sliceCount != original.sliceCount;
    bool defaultReversed = false;
    for (std::size_t index = 0u; index < first.sliceCount; ++index) {
        if (index < original.sliceCount)
            orderChanged = orderChanged
                || first.slices[index].startFrame
                    != original.slices[index].startFrame;
        defaultReversed = defaultReversed || first.slices[index].reverse;
    }
    check(orderChanged && !defaultReversed && first.validFor(*asset),
        "default structural mutation did not rearrange safely or used reverse");

    MutationVariation noRepeat = original;
    check(structurallyMutateVariation(noRepeat, *asset, 7u, 0.72f, 80u,
              StructuralRearrange)
            && noRepeat.sliceCount == original.sliceCount,
        "disabling repeat changed the structural mutation length");
    bool treatmentChanged = false;
    for (std::size_t index = 0u; index < noRepeat.sliceCount; ++index) {
        treatmentChanged = treatmentChanged
            || noRepeat.slices[index].reverse
                != original.slices[index].reverse
            || noRepeat.slices[index].transposeSemitones
                != original.slices[index].transposeSemitones;
    }
    check(!treatmentChanged,
        "disabled pitch/reverse treatments changed a structural mutation");

    MutationVariation single = captureMutationVariation(
        oneSliceSlot(asset));
    check(structurallyMutateVariation(single, *asset, 44u, 0.72f, 6u,
              StructuralRepeat)
            && single.sliceCount > 1u && single.sliceCount <= 6u,
        "repeat did not turn a one-shot source into a repeated structure");

    SampleSlot firstFx = slot;
    SampleSlot secondFx = slot;
    check((kDefaultStructuralMutationUses & StructuralMixerFx) != 0u
            && (kDefaultStructuralMutationUses & StructuralReverse) == 0u
            && mutateMixerEffects(firstFx, 112358u)
            && mutateMixerEffects(secondFx, 112358u)
            && firstFx.inserts[0u].type != InsertType::Off
            && firstFx.inserts[1u].type != InsertType::Off
            && firstFx.valid(),
        "default mutation uses or generated mixer effects are invalid");
    bool deterministicFx = true;
    for (std::size_t insert = 0u; insert < firstFx.inserts.size(); ++insert) {
        deterministicFx = deterministicFx
            && firstFx.inserts[insert].type == secondFx.inserts[insert].type
            && firstFx.inserts[insert].mode == secondFx.inserts[insert].mode
            && firstFx.inserts[insert].variant
                == secondFx.inserts[insert].variant
            && firstFx.inserts[insert].values
                == secondFx.inserts[insert].values;
    }
    deterministicFx = deterministicFx
        && firstFx.mixerLowEqDb == secondFx.mixerLowEqDb
        && firstFx.mixerMidEqDb == secondFx.mixerMidEqDb
        && firstFx.mixerHighEqDb == secondFx.mixerHighEqDb
        && firstFx.mixerMidFrequencyHz == secondFx.mixerMidFrequencyHz
        && (std::abs(firstFx.mixerLowEqDb) > 0.01f
            || std::abs(firstFx.mixerMidEqDb) > 0.01f
            || std::abs(firstFx.mixerHighEqDb) > 0.01f);
    check(deterministicFx,
        "mixer insert and EQ mutation was not deterministic or distinct");
}

void testNamedStructuralMutationOperations()
{
    auto writableAsset = std::make_shared<SampleAsset>();
    writableAsset->sampleRate = 48000.0;
    writableAsset->channelCount = 2u;
    writableAsset->channels[0u].resize(1000u);
    writableAsset->channels[1u].resize(1000u);
    const auto fill = [&writableAsset](uint32_t start, uint32_t end,
                          float value) {
        for (uint32_t frame = start; frame < end; ++frame) {
            writableAsset->channels[0u][frame] = value;
            writableAsset->channels[1u][frame] = value * 0.5f;
        }
    };
    fill(0u, 100u, 0.8f);
    fill(100u, 300u, 0.1f);
    fill(300u, 600u, 0.6f);
    fill(600u, 1000u, 0.3f);
    std::shared_ptr<const SampleAsset> asset = writableAsset;
    SampleSlot slot = oneSliceSlot(asset);
    slot.sliceCount = 4u;
    slot.slices[0u] = { 300u, 600u };
    slot.slices[1u] = { 0u, 100u };
    slot.slices[2u] = { 600u, 1000u };
    slot.slices[3u] = { 100u, 300u };
    const MutationVariation original = captureMutationVariation(slot);

    MutationVariation loudness = original;
    check(applyStructuralMutationOperation(loudness, *asset,
              StructuralMutationOperation::Sorter, 1u, 0.72f, 80u,
              false, 0u)
            && loudness.slices[0u].startFrame == 100u
            && loudness.slices[1u].startFrame == 600u
            && loudness.slices[2u].startFrame == 300u
            && loudness.slices[3u].startFrame == 0u,
        "SORTER did not order slices by multichannel loudness");

    MutationVariation duration = original;
    check(applyStructuralMutationOperation(duration, *asset,
              StructuralMutationOperation::Sorter, 1u, 0.72f, 80u,
              true, 0u)
            && duration.slices[0u].startFrame == 0u
            && duration.slices[1u].startFrame == 100u
            && duration.slices[2u].startFrame == 300u
            && duration.slices[3u].startFrame == 600u,
        "SORTER did not order slices by duration");

    MutationVariation heads = original;
    bool foundShortHead = false;
    check(applyStructuralMutationOperation(heads, *asset,
              StructuralMutationOperation::Stutter, 1234u, 0.72f, 20u,
              false, 0u)
            && heads.sliceCount > original.sliceCount
            && heads.validFor(*asset),
        "STUTTER beginnings did not build a valid expanded pattern");
    for (std::size_t index = 0u; index < heads.sliceCount; ++index) {
        const auto& candidate = heads.slices[index];
        for (std::size_t source = 0u; source < original.sliceCount; ++source) {
            const auto& reference = original.slices[source];
            foundShortHead = foundShortHead
                || (candidate.startFrame == reference.startFrame
                    && candidate.endFrame < reference.endFrame);
        }
    }
    check(foundShortHead,
        "STUTTER beginnings did not create a shortened slice head");

    MutationVariation groups = original;
    check(applyStructuralMutationOperation(groups, *asset,
              StructuralMutationOperation::Stutter, 4321u, 0.72f, 20u,
              true, 0u)
            && groups.sliceCount > original.sliceCount
            && groups.validFor(*asset),
        "STUTTER joined groups did not repeat a valid contiguous group");

    MutationVariation shrink = original;
    check(applyStructuralMutationOperation(shrink, *asset,
              StructuralMutationOperation::Shrink, 19u, 0.8f, 20u,
              true, 0u)
            && shrink.sliceCount > original.sliceCount
            && shrink.validFor(*asset),
        "SHRINK did not build a valid accelerated repeat chain");
    bool foundShrinkChain = false;
    for (std::size_t index = 1u; index < shrink.sliceCount; ++index) {
        const auto& previous = shrink.slices[index - 1u];
        const auto& current = shrink.slices[index];
        foundShrinkChain = foundShrinkChain
            || (current.startFrame == previous.startFrame
                && current.endFrame - current.startFrame
                    < previous.endFrame - previous.startFrame
                && current.transposeSemitones
                    > previous.transposeSemitones);
    }
    check(foundShrinkChain,
        "SHRINK did not progressively truncate and accelerate repeats");

    MutationVariation doublets = original;
    check(applyStructuralMutationOperation(doublets, *asset,
              StructuralMutationOperation::Doublets, 7u, 0.72f, 8u,
              false, 0u)
            && doublets.sliceCount == 8u && doublets.validFor(*asset),
        "DOUBLETS did not repeat every segment exactly twice");
    bool paired = true;
    for (std::size_t index = 0u; index < original.sliceCount; ++index) {
        paired = paired
            && doublets.slices[index * 2u].startFrame
                == original.slices[index].startFrame
            && doublets.slices[index * 2u + 1u].startFrame
                == original.slices[index].startFrame;
    }
    check(paired,
        "DOUBLETS did not retain adjacent source-segment pairs");
    MutationVariation overflow = original;
    check(!applyStructuralMutationOperation(overflow, *asset,
              StructuralMutationOperation::Doublets, 7u, 0.72f, 7u,
              false, 0u)
            && overflow.sliceCount == original.sliceCount,
        "DOUBLETS silently dropped segments when the MIDI map was too small");
}

void testMutationSlicePeakCeiling()
{
    SampleAsset asset;
    asset.sampleRate = 48000.0;
    asset.channelCount = 2u;
    asset.channels[0u] = {
        2.0f, -1.0f, 0.5f, 1.5f, 0.25f, -0.5f, 0.4f, -0.2f,
    };
    asset.channels[1u] = {
        1.0f, -3.0f, 0.25f, 0.75f, -0.3f, 0.2f, -0.1f, 0.5f,
    };
    constexpr std::array<uint32_t, 2u> starts {{ 0u, 4u }};
    check(reduceSlicePeaksToCeiling(asset, starts.data(), starts.size()),
        "the mutation slice peak ceiling rejected valid audio");
    float firstPeak = 0.0f;
    float secondPeak = 0.0f;
    for (uint32_t frame = 0u; frame < 4u; ++frame)
        firstPeak = std::max(firstPeak, multichannelMagnitude(asset, frame));
    for (uint32_t frame = 4u; frame < 8u; ++frame)
        secondPeak = std::max(secondPeak,
            multichannelMagnitude(asset, frame));
    check(firstPeak <= 1.0f && near(asset.channels[1u][1u], -1.0f)
            && near(asset.channels[0u][0u], 2.0f / 3.0f),
        "an over-level mutation slice was not reduced coherently to 0 dBFS");
    check(near(secondPeak, 0.5f) && near(asset.channels[0u][5u], -0.5f),
        "a mutation slice below 0 dBFS was changed by the peak ceiling");
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
    const auto preRolled = makeTransientSlices(asset, analysis, 8u, 0u,
        2000u);
    check(preRolled.size() == 3u && preRolled[0u].endFrame == 99u
            && preRolled[1u].startFrame == 99u
            && preRolled[1u].endFrame == 399u
            && preRolled[2u].startFrame == 399u,
        "microsecond transient pre-roll did not move slice onsets earlier");
    SampleAnalysis crowded = analysis;
    crowded.transients = {{ 100u, 1.0f }, { 115u, 1.0f },
        { 200u, 1.0f }, { 980u, 1.0f }};
    const auto durationFiltered = makeTransientSlices(asset, crowded,
        8u, 0u, 0u, 50u);
    bool minimumDurationHeld = durationFiltered.size() == 3u;
    for (const auto& slice : durationFiltered)
        minimumDurationHeld = minimumDurationHeld
            && slice.endFrame - slice.startFrame >= 50u;
    check(minimumDurationHeld
            && durationFiltered[0u].endFrame == 100u
            && durationFiltered[1u].endFrame == 200u
            && durationFiltered[2u].endFrame == 1000u,
        "minimum transient-slice duration admitted a short region");
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
    bank.slots[0u].loopCrossfade = 0.0f;
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

void testPriorityLaneInserts()
{
    auto alternating = std::make_shared<SampleAsset>();
    alternating->sampleRate = 48000.0;
    alternating->channelCount = 2u;
    alternating->channels[0u].resize(4096u);
    alternating->channels[1u].resize(4096u);
    for (uint32_t frame = 0u; frame < 4096u; ++frame) {
        const float sample = (frame & 1u) == 0u ? 0.6f : -0.6f;
        alternating->channels[0u][frame] = sample;
        alternating->channels[1u][frame] = sample * 0.5f;
    }
    BankSnapshot bank;
    bank.slots[0u] = oneSliceSlot(alternating);
    check(mapSlotConsecutively(bank, 0u, 60u),
        "insert fixture mapping failed");
    const RenderEvent event { 0u, EventKind::NoteOn, 100u, 60u, 0u, 1.0f };
    SlicerEngine engine;
    check(engine.prepare(48000.0) && engine.setBank(&bank),
        "insert fixture did not prepare");
    std::array<float, 512u> dryLeft {};
    std::array<float, 512u> dryRight {};
    engine.render(&event, 1u, dryLeft.data(), dryRight.data(),
        static_cast<uint32_t>(dryLeft.size()));

    bank.slots[0u].inserts[0u] = defaultInsertSettings(InsertType::Filter);
    bank.slots[0u].inserts[0u].values = {{ 0.16f, 0.1f, 0.0f, 1.0f }};
    check(bank.valid() && engine.setBank(&bank),
        "filter insert settings were rejected");
    std::array<float, 512u> filterLeft {};
    std::array<float, 512u> filterRight {};
    engine.render(&event, 1u, filterLeft.data(), filterRight.data(),
        static_cast<uint32_t>(filterLeft.size()));
    float dryEnergy = 0.0f;
    float filterEnergy = 0.0f;
    for (std::size_t frame = 256u; frame < dryLeft.size(); ++frame) {
        dryEnergy += std::abs(dryLeft[frame]);
        filterEnergy += std::abs(filterLeft[frame]);
    }
    check(filterEnergy < dryEnergy * 0.08f,
        "post-playback filter insert did not reject high-frequency audio");

    auto ramp = std::make_shared<SampleAsset>();
    ramp->sampleRate = 48000.0;
    ramp->channelCount = 2u;
    ramp->channels[0u].resize(4096u);
    ramp->channels[1u].resize(4096u);
    for (uint32_t frame = 0u; frame < 4096u; ++frame) {
        ramp->channels[0u][frame] = static_cast<float>(frame % 256u) / 256.0f;
        ramp->channels[1u][frame] = ramp->channels[0u][frame] * 0.5f;
    }
    bank.slots[0u] = oneSliceSlot(ramp);
    bank.slots[0u].inserts[0u] = defaultInsertSettings(InsertType::Degrade);
    bank.slots[0u].inserts[0u].values = {{ 0.72f, 0.2f, 0.0f, 1.0f }};
    check(mapSlotConsecutively(bank, 0u, 60u) && engine.setBank(&bank),
        "degrade insert fixture failed");
    std::array<float, 96u> degradeLeft {};
    std::array<float, 96u> degradeRight {};
    engine.render(&event, 1u, degradeLeft.data(), degradeRight.data(),
        static_cast<uint32_t>(degradeLeft.size()));
    bool held = true;
    for (std::size_t frame = 1u; frame < 24u; ++frame)
        held = held && near(degradeLeft[frame], degradeLeft[0u]);
    check(held && !near(degradeLeft[64u], degradeLeft[0u]),
        "degrade insert did not use its shared sample-hold clock");

    auto shaped = std::make_shared<SampleAsset>();
    shaped->sampleRate = 48000.0;
    shaped->channelCount = 2u;
    shaped->channels[0u].assign(4096u, 0.10f);
    shaped->channels[1u].assign(4096u, 0.05f);
    for (uint32_t frame = 0u; frame < 32u; ++frame) {
        shaped->channels[0u][frame] = 0.8f;
        shaped->channels[1u][frame] = 0.4f;
    }
    bank.slots[0u] = oneSliceSlot(shaped);
    bank.slots[0u].inserts[0u] = defaultInsertSettings(
        InsertType::Transient);
    bank.slots[0u].inserts[0u].values[0u] = 0.5f;
    bank.slots[0u].inserts[0u].values[1u] = 0.0f;
    check(mapSlotConsecutively(bank, 0u, 60u) && engine.setBank(&bank),
        "transient insert fixture failed");
    std::array<float, 512u> transientLeft {};
    std::array<float, 512u> transientRight {};
    engine.render(&event, 1u, transientLeft.data(), transientRight.data(),
        static_cast<uint32_t>(transientLeft.size()));
    bool linked = true;
    for (std::size_t frame = 0u; frame < transientLeft.size(); ++frame) {
        if (std::abs(transientRight[frame]) < 1.0e-7f) continue;
        linked = linked && near(transientLeft[frame]
            / transientRight[frame], 2.0f, 1.0e-4f);
    }
    check(linked,
        "linked transient insert changed inter-channel relationships");
    check(transientLeft[200u] < 0.10f * 0.7071068f,
        "transient insert did not reduce the configured sustain");

    auto impulse = std::make_shared<SampleAsset>();
    impulse->sampleRate = 48000.0;
    impulse->channelCount = 2u;
    impulse->channels[0u].assign(4096u, 0.0f);
    impulse->channels[1u].assign(4096u, 0.0f);
    impulse->channels[0u][0u] = 0.8f;
    impulse->channels[1u][0u] = 0.4f;
    bank.slots[0u] = oneSliceSlot(impulse);
    bank.slots[0u].inserts[0u] = defaultInsertSettings(
        InsertType::Resonator);
    bank.slots[0u].inserts[0u].values = {{ 0.5f, 0.7f, 0.2f, 1.0f }};
    check(mapSlotConsecutively(bank, 0u, 60u) && engine.setBank(&bank),
        "resonator insert fixture failed");
    std::array<float, 300u> resonatorLeft {};
    std::array<float, 300u> resonatorRight {};
    engine.render(&event, 1u, resonatorLeft.data(), resonatorRight.data(),
        static_cast<uint32_t>(resonatorLeft.size()));
    check(std::abs(resonatorLeft[120u]) > 0.2f
            && near(resonatorLeft[120u] / resonatorRight[120u],
                2.0f, 1.0e-4f),
        "resonator insert did not create a sample-locked tuned return");

    bank.slots[0u] = oneSliceSlot(impulse);
    bank.slots[0u].inserts[0u] = defaultInsertSettings(
        InsertType::Erosion);
    bank.slots[0u].inserts[0u].values = {{ 0.0f, 0.0f, 0.0f, 1.0f }};
    check(mapSlotConsecutively(bank, 0u, 60u) && engine.setBank(&bank),
        "erosion insert fixture failed");
    std::array<float, 16u> erosionLeft {};
    std::array<float, 16u> erosionRight {};
    engine.render(&event, 1u, erosionLeft.data(), erosionRight.data(),
        static_cast<uint32_t>(erosionLeft.size()));
    check(std::abs(erosionLeft[1u]) > 0.1f
            && near(erosionLeft[1u] / erosionRight[1u],
                2.0f, 1.0e-4f),
        "erosion insert did not produce a locked short-delay response");

    bank.slots[0u] = oneSliceSlot(constantAsset(0.4f, 0.2f));
    bank.slots[0u].inserts[0u] = defaultInsertSettings(
        InsertType::Shifter);
    bank.slots[0u].inserts[0u].variant = 1u;
    bank.slots[0u].inserts[0u].values = {{ 0.35f, 0.0f, 0.0f, 1.0f }};
    check(mapSlotConsecutively(bank, 0u, 60u) && engine.setBank(&bank),
        "ring-shift insert fixture failed");
    std::array<float, 256u> shifterLeft {};
    std::array<float, 256u> shifterRight {};
    engine.render(&event, 1u, shifterLeft.data(), shifterRight.data(),
        static_cast<uint32_t>(shifterLeft.size()));
    const auto [shiftMinimum, shiftMaximum] = std::minmax_element(
        shifterLeft.begin(), shifterLeft.end());
    bool shiftLinked = true;
    for (std::size_t frame = 0u; frame < shifterLeft.size(); ++frame) {
        if (std::abs(shifterRight[frame]) < 1.0e-7f) continue;
        shiftLinked = shiftLinked && near(shifterLeft[frame]
            / shifterRight[frame], 2.0f, 1.0e-4f);
    }
    check(shiftLinked && *shiftMaximum - *shiftMinimum > 0.3f,
        "ring shifter lacked modulation or changed channel relationships");

    bank.slots[0u] = oneSliceSlot(constantAsset(0.02f, 0.01f, 96000u));
    bank.slots[0u].inserts[0u] = defaultInsertSettings(
        InsertType::Shifter);
    bank.slots[0u].inserts[0u].variant = 0u;
    bank.slots[0u].inserts[0u].values = {{ 0.50f, 1.0f, 0.0f, 1.0f }};
    check(mapSlotConsecutively(bank, 0u, 60u) && engine.setBank(&bank),
        "governed shifter fixture failed");
    std::vector<float> governedShiftLeft(90000u, 0.0f);
    std::vector<float> governedShiftRight(90000u, 0.0f);
    engine.render(&event, 1u, governedShiftLeft.data(),
        governedShiftRight.data(),
        static_cast<uint32_t>(governedShiftLeft.size()));
    float preSquashEnergy = 0.0f;
    float squashEnergy = 0.0f;
    float governedPeak = 0.0f;
    for (const float sample : governedShiftLeft)
        governedPeak = std::max(governedPeak, std::abs(sample));
    for (uint32_t frame = 30000u; frame < 34000u; ++frame)
        preSquashEnergy += std::abs(governedShiftLeft[frame]);
    for (uint32_t frame = 37000u; frame < 39000u; ++frame)
        squashEnergy += std::abs(governedShiftLeft[frame]) * 2.0f;
    check(std::all_of(governedShiftLeft.begin(), governedShiftLeft.end(),
                [](float sample) { return std::isfinite(sample); })
            && preSquashEnergy > 0.1f
            && squashEnergy < preSquashEnergy * 0.85f
            && governedPeak < 8.0f,
        "shifter governor did not periodically squash sustained regeneration");

    bank.slots[0u] = oneSliceSlot(constantAsset(0.7f, 0.35f));
    bank.slots[0u].inserts[0u] = defaultInsertSettings(
        InsertType::Wavefolder);
    bank.slots[0u].inserts[0u].values = {{ 0.80f, 0.50f, 0.20f, 1.0f }};
    check(mapSlotConsecutively(bank, 0u, 60u) && engine.setBank(&bank),
        "wavefolder insert fixture failed");
    std::array<float, 128u> foldLeft {};
    std::array<float, 128u> foldRight {};
    engine.render(&event, 1u, foldLeft.data(), foldRight.data(),
        static_cast<uint32_t>(foldLeft.size()));
    check(std::all_of(foldLeft.begin(), foldLeft.end(), [](float sample) {
                return std::isfinite(sample) && std::abs(sample) <= 1.1f;
            })
            && std::abs(foldLeft[64u] - 0.7f * 0.7071068f) > 0.05f,
        "oversampled wavefolder was inactive or unbounded");

    bank.slots[0u] = oneSliceSlot(ramp);
    bank.slots[0u].inserts[0u] = defaultInsertSettings(
        InsertType::Repeater);
    bank.slots[0u].inserts[0u].values = {{ 0.0f, 0.20f, 0.0f, 1.0f }};
    check(mapSlotConsecutively(bank, 0u, 60u) && engine.setBank(&bank),
        "buffer repeater fixture failed");
    std::array<float, 2100u> repeaterLeft {};
    std::array<float, 2100u> repeaterRight {};
    engine.render(&event, 1u, repeaterLeft.data(), repeaterRight.data(),
        static_cast<uint32_t>(repeaterLeft.size()));
    // The ramp crosses the transient threshold near its start. An 8 ms
    // window at 48 kHz is 384 frames; compare equal full-level positions in
    // successive repeats after the de-click attack.
    check(near(repeaterLeft[635u], repeaterLeft[1019u], 1.0e-4f)
            && near(repeaterRight[635u], repeaterRight[1019u], 1.0e-4f)
            && near(repeaterLeft[635u] / repeaterRight[635u],
                2.0f, 1.0e-4f),
        "transient buffer repeat was not periodic or multichannel locked");

    bank.slots[0u].inserts[0u].values[2u] = 1.0f;
    check(engine.setBank(&bank), "pitch-decay repeater fixture failed");
    std::array<float, 2100u> decayedRepeatLeft {};
    std::array<float, 2100u> decayedRepeatRight {};
    engine.render(&event, 1u, decayedRepeatLeft.data(),
        decayedRepeatRight.data(),
        static_cast<uint32_t>(decayedRepeatLeft.size()));
    const float firstRepeatSlope = std::abs(
        decayedRepeatLeft[635u] - decayedRepeatLeft[627u]);
    const float secondRepeatSlope = std::abs(
        decayedRepeatLeft[1019u] - decayedRepeatLeft[1011u]);
    check(secondRepeatSlope < firstRepeatSlope * 0.7f
            && secondRepeatSlope > firstRepeatSlope * 0.2f
            && near(decayedRepeatLeft[1019u] / decayedRepeatRight[1019u],
                2.0f, 1.0e-4f),
        "repeater pitch decay did not slow its shared read head");

    bank.slots[0u] = oneSliceSlot(ramp);
    bank.slots[0u].inserts[0u] = defaultInsertSettings(
        InsertType::TimeMangler);
    bank.slots[0u].inserts[0u].values = {{ 0.0f, 0.50f, 0.0f, 1.0f }};
    check(mapSlotConsecutively(bank, 0u, 60u) && engine.setBank(&bank),
        "time mangler fixture failed");
    std::array<float, 900u> timeLeft {};
    std::array<float, 900u> timeRight {};
    engine.render(&event, 1u, timeLeft.data(), timeRight.data(),
        static_cast<uint32_t>(timeLeft.size()));
    check(timeLeft[635u] > timeLeft[636u]
            && near(timeLeft[635u] / timeRight[635u],
                2.0f, 1.0e-4f),
        "transient reverse capture did not share one multichannel read head");

    auto freezeAsset = std::make_shared<SampleAsset>();
    freezeAsset->sampleRate = 48000.0;
    freezeAsset->channelCount = 2u;
    freezeAsset->channels[0u].assign(1000u, 0.4f);
    freezeAsset->channels[1u].assign(1000u, 0.2f);
    freezeAsset->channels[0u][0u] = 0.0f;
    freezeAsset->channels[1u][0u] = 0.0f;
    bank.slots[0u] = oneSliceSlot(freezeAsset);
    bank.slots[0u].inserts[0u] = defaultInsertSettings(
        InsertType::TimeMangler);
    bank.slots[0u].inserts[0u].variant = 1u;
    // Minimum DECAY is 125 ms, or 6000 frames at 48 kHz.
    bank.slots[0u].inserts[0u].values = {{ 0.0f, 0.50f, 0.0f, 1.0f }};
    check(mapSlotConsecutively(bank, 0u, 60u) && engine.setBank(&bank),
        "finite freeze fixture failed");
    std::array<float, 7000u> freezeLeft {};
    std::array<float, 7000u> freezeRight {};
    engine.render(&event, 1u, freezeLeft.data(), freezeRight.data(),
        static_cast<uint32_t>(freezeLeft.size()));
    check(std::abs(freezeLeft[2000u]) > 0.05f
            && near(freezeLeft[2000u] / freezeRight[2000u],
                2.0f, 1.0e-4f)
            && std::abs(freezeLeft[6500u]) < 1.0e-5f
            && std::abs(freezeRight[6500u]) < 1.0e-5f,
        "freeze did not decay to silence and return to listening");

    auto smoothingAsset = std::make_shared<SampleAsset>();
    smoothingAsset->sampleRate = 48000.0;
    smoothingAsset->channelCount = 2u;
    smoothingAsset->channels[0u].assign(4096u, -0.4f);
    smoothingAsset->channels[1u].assign(4096u, -0.2f);
    smoothingAsset->channels[0u][0u] = 0.0f;
    smoothingAsset->channels[1u][0u] = 0.0f;
    std::fill(smoothingAsset->channels[0u].begin() + 1u,
        smoothingAsset->channels[0u].begin() + 400u, 0.4f);
    std::fill(smoothingAsset->channels[1u].begin() + 1u,
        smoothingAsset->channels[1u].begin() + 400u, 0.2f);
    bank.slots[0u] = oneSliceSlot(smoothingAsset);
    bank.slots[0u].inserts[0u] = defaultInsertSettings(
        InsertType::Repeater);
    bank.slots[0u].inserts[0u].values = {{ 0.0f, 0.20f, 0.0f, 1.0f }};
    check(mapSlotConsecutively(bank, 0u, 60u) && engine.setBank(&bank),
        "live insert smoothing fixture failed");
    std::array<float, 600u> beforeMixChangeLeft {};
    std::array<float, 600u> beforeMixChangeRight {};
    engine.render(&event, 1u, beforeMixChangeLeft.data(),
        beforeMixChangeRight.data(),
        static_cast<uint32_t>(beforeMixChangeLeft.size()));
    MixerSnapshot smoothedMixer = mixerSnapshotFromBank(bank);
    smoothedMixer.strips[0u].inserts[0u].values[3u] = 0.0f;
    engine.setPreparedMixer(&smoothedMixer);
    std::array<float, 128u> afterMixChangeLeft {};
    std::array<float, 128u> afterMixChangeRight {};
    engine.render(nullptr, 0u, afterMixChangeLeft.data(),
        afterMixChangeRight.data(),
        static_cast<uint32_t>(afterMixChangeLeft.size()));
    check(std::abs(afterMixChangeLeft[0u]
                - beforeMixChangeLeft.back()) < 0.01f
            && near(afterMixChangeLeft[0u] / afterMixChangeRight[0u],
                2.0f, 1.0e-4f),
        "live buffer mix change introduced a discontinuity");

    bank.slots[0u].inserts[1u] = defaultInsertSettings(InsertType::Filter);
    check(bank.valid(), "two serial inserts were not accepted per lane");
    bank.slots[0u].inserts[1u].variant = 3u;
    check(!bank.valid(), "an invalid insert mode was accepted");
    bank.slots[0u].inserts[1u].variant = 0u;
    bank.slots[0u].inserts[1u].values[0u]
        = std::numeric_limits<float>::quiet_NaN();
    check(!bank.valid(), "non-finite insert settings were accepted");
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

    mixer.strips[0u].lowEqDb = 0.0f;
    mixer.strips[0u].inserts[0u] = defaultInsertSettings(
        InsertType::Transient);
    mixer.strips[0u].inserts[0u].values = {{ 0.5f, 0.0f, 0.0f, 1.0f }};
    engine.setPreparedMixer(&mixer);
    std::array<float, 512u> insertLeft {};
    std::array<float, 512u> insertRight {};
    const float beforeInsertAdoption = engine.slotPlayheadNormalized(0u);
    engine.render(nullptr, 0u, insertLeft.data(), insertRight.data(),
        static_cast<uint32_t>(insertLeft.size()));
    check(engine.activeVoiceCount() == 1u
            && engine.slotPlayheadNormalized(0u) > beforeInsertAdoption
            && insertLeft.back() < 0.4f * center * 0.7f
            && insertRight.back() < 0.2f * center * 0.7f,
        "publishing an insert reset the voice or failed to affect active audio");

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
    bool supportedWidthsPreserved = true;
    for (uint8_t sourceChannels = 1u; sourceChannels <= 16u;
         ++sourceChannels) {
        auto asset = std::make_shared<SampleAsset>();
        asset->sampleRate = 48000.0;
        asset->channelCount = sourceChannels;
        for (uint32_t channel = 0u; channel < sourceChannels; ++channel) {
            asset->channels[channel].assign(32u,
                static_cast<float>(channel + 1u) * 0.03f);
        }
        BankSnapshot bank;
        bank.slots[0u] = oneSliceSlot(asset);
        supportedWidthsPreserved = supportedWidthsPreserved
            && mapSlotConsecutively(bank, 0u, 60u);
        SlicerEngine engine;
        supportedWidthsPreserved = supportedWidthsPreserved
            && engine.prepare(48000.0) && engine.setBank(&bank);
        std::array<std::array<float, 8u>, 16u> rendered {};
        for (auto& channel : rendered) channel.fill(1.0f);
        std::array<float*, 16u> outputs {};
        for (std::size_t channel = 0u; channel < rendered.size(); ++channel)
            outputs[channel] = rendered[channel].data();
        const RenderEvent event {
            0u, EventKind::NoteOn, 1u, 60u, 0u, 1.0f,
        };
        engine.render(&event, 1u, outputs.data(),
            static_cast<uint32_t>(outputs.size()), 8u);
        for (uint32_t channel = 0u; channel < 16u; ++channel) {
            if (sourceChannels == 1u && channel < 2u) {
                supportedWidthsPreserved = supportedWidthsPreserved
                    && near(rendered[channel][0u],
                        0.03f * 0.7071067811865475f);
            } else if (channel < sourceChannels) {
                const float panGain = sourceChannels == 2u
                    ? 0.7071067811865475f : 1.0f;
                supportedWidthsPreserved = supportedWidthsPreserved
                    && near(rendered[channel][0u],
                        static_cast<float>(channel + 1u) * 0.03f
                            * panGain);
            } else {
                supportedWidthsPreserved = supportedWidthsPreserved
                    && std::all_of(rendered[channel].begin(),
                        rendered[channel].end(),
                        [](float sample) { return sample == 0.0f; });
            }
        }
    }
    check(supportedWidthsPreserved,
        "Slicer 16 did not accept and preserve every 1-16 channel source "
        "width or clear its unused outputs");
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

void testInheritedSamplePlaybackBehavior()
{
    auto mutableAsset = std::make_shared<SampleAsset>();
    mutableAsset->sampleRate = 1000.0;
    mutableAsset->channelCount = 1u;
    mutableAsset->channels[0u].resize(400u);
    for (uint32_t frame = 0u; frame < mutableAsset->frameCount(); ++frame)
        mutableAsset->channels[0u][frame]
            = 0.1f + static_cast<float>(frame) * 0.001f;
    std::shared_ptr<const SampleAsset> asset = mutableAsset;
    BankSnapshot bank;
    bank.slots[0u] = oneSliceSlot(asset);
    bank.slots[0u].sliceCount = 2u;
    bank.slots[0u].slices[0u].endFrame = 200u;
    bank.slots[0u].slices[1u].startFrame = 200u;
    bank.slots[0u].slices[1u].endFrame = 400u;
    check(bank.slots[0u].retriggerMode == RetriggerMode::Restart
            && bank.slots[0u].voiceMode == VoiceMode::Poly
            && mapSlotConsecutively(bank, 0u, 60u)
            && bank.valid(),
        "Slicer did not inherit the restart/poly Sample-family defaults");

    const std::array<RenderEvent, 2u> repeated {{
        { 0u, EventKind::NoteOn, 200u, 60u, 0u, 1.0f, 1u },
        { 4u, EventKind::NoteOn, 201u, 60u, 0u, 1.0f, 1u },
    }};
    SlicerEngine engine;
    check(engine.prepare(1000.0) && engine.setBank(&bank),
        "inherited playback fixture did not prepare");
    std::array<float, 8u> left {};
    std::array<float, 8u> right {};
    engine.render(repeated.data(), repeated.size(), left.data(), right.data(),
        static_cast<uint32_t>(left.size()));
    check(engine.activeVoiceCount() == 1u
            && engine.voiceCursorCount() == 1u
            && engine.voiceCursors()[0u].key == 60u
            && engine.voiceCursors()[0u].sourcePositionNormalized < 0.02f,
        "default same-note retrigger did not restart the mapped slice");

    bank.slots[0u].retriggerMode = RetriggerMode::Layer;
    engine.reset();
    check(engine.setBank(&bank), "Layer bank was rejected");
    engine.render(repeated.data(), repeated.size(), left.data(), right.data(),
        static_cast<uint32_t>(left.size()));
    check(engine.activeVoiceCount() == 2u
            && engine.voiceCursorCount() == 2u,
        "Layer did not preserve optional same-slice polyphony");

    bank.slots[0u].retriggerMode = RetriggerMode::Ignore;
    engine.reset();
    check(engine.setBank(&bank), "Ignore bank was rejected");
    engine.render(repeated.data(), repeated.size(), left.data(), right.data(),
        static_cast<uint32_t>(left.size()));
    check(engine.activeVoiceCount() == 1u
            && engine.voiceCursors()[0u].sourcePositionNormalized > 0.019f,
        "Ignore interrupted the existing same-note slice");

    bank.slots[0u].retriggerMode = RetriggerMode::Restart;
    const std::array<RenderEvent, 2u> different {{
        { 0u, EventKind::NoteOn, 202u, 60u, 0u, 1.0f, 1u },
        { 2u, EventKind::NoteOn, 203u, 61u, 0u, 1.0f, 1u },
    }};
    engine.reset();
    check(engine.setBank(&bank), "Poly bank was rejected");
    engine.render(different.data(), different.size(), left.data(),
        right.data(), static_cast<uint32_t>(left.size()));
    check(engine.activeVoiceCount() == 2u,
        "Poly mode did not allow different mapped slices to overlap");

    bank.slots[0u].voiceMode = VoiceMode::Mono;
    engine.reset();
    check(engine.setBank(&bank), "Mono bank was rejected");
    engine.render(different.data(), different.size(), left.data(),
        right.data(), static_cast<uint32_t>(left.size()));
    check(engine.activeVoiceCount() == 1u
            && engine.voiceCursors()[0u].key == 61u,
        "Mono did not replace the previous mapped slice");

    bank.slots[0u].voiceMode = VoiceMode::Legato;
    bank.slots[0u].glideSeconds = 0.05f;
    bank.slots[0u].slices[1u].transposeSemitones = 12.0f;
    engine.reset();
    check(engine.setBank(&bank), "Legato bank was rejected");
    engine.render(different.data(), different.size(), left.data(),
        right.data(), static_cast<uint32_t>(left.size()));
    check(engine.activeVoiceCount() == 1u
            && engine.voiceCursors()[0u].key == 61u,
        "Legato did not retarget the active break voice");

    bank.slots[0u].voiceMode = VoiceMode::Poly;
    bank.slots[0u].glideSeconds = 0.0f;
    bank.slots[0u].slices[1u].transposeSemitones = 0.0f;
    bank.slots[0u].triggerMode = TriggerMode::Gate;
    const std::array<RenderEvent, 2u> gated {{
        { 0u, EventKind::NoteOn, 204u, 60u, 0u, 1.0f, 1u },
        { 4u, EventKind::NoteOff, 204u, 60u, 0u, 0.0f, 1u },
    }};
    engine.reset();
    check(engine.setBank(&bank), "Gate bank was rejected");
    engine.render(gated.data(), gated.size(), left.data(), right.data(),
        static_cast<uint32_t>(left.size()));
    check(engine.activeVoiceCount() == 0u && left[3u] > 0.0f
            && left[4u] > 0.0f && left[5u] == 0.0f,
        "Gate did not release a non-looping mapped slice on note-off");

    bank.slots[0u].triggerMode = TriggerMode::Toggle;
    engine.reset();
    check(engine.setBank(&bank), "Toggle bank was rejected");
    engine.render(repeated.data(), repeated.size(), left.data(), right.data(),
        static_cast<uint32_t>(left.size()));
    check(engine.activeVoiceCount() == 0u && left[3u] > 0.0f
            && left[4u] > 0.0f && left[5u] == 0.0f,
        "Toggle did not stop the matching mapped slice");

    bank.slots[0u].triggerMode = TriggerMode::Auto;
    bank.slots[0u].syncMode = SyncMode::Host;
    bank.slots[0u].sourceTempoBpm = 100.0f;
    engine.reset();
    check(engine.setBank(&bank), "Host-sync bank was rejected");
    engine.render(repeated.data(), 1u, left.data(), right.data(),
        static_cast<uint32_t>(left.size()), 200.0);
    check(engine.voiceCursors()[0u].sourcePositionNormalized > 0.035f,
        "host tempo did not scale Slicer playback duration");

    bank.slots[0u].syncMode = SyncMode::Free;
    bank.slots[0u].pitchMode = PitchMode::Stretch;
    bank.slots[0u].slices[0u].transposeSemitones = 12.0f;
    engine.reset();
    check(engine.setBank(&bank), "Stretch bank was rejected");
    std::array<float, 120u> stretchLeft {};
    std::array<float, 120u> stretchRight {};
    const RenderEvent pitched {
        0u, EventKind::NoteOn, 205u, 60u, 0u, 1.0f, 1u,
    };
    engine.render(&pitched, 1u, stretchLeft.data(), stretchRight.data(),
        static_cast<uint32_t>(stretchLeft.size()));
    check(engine.activeVoiceCount() == 1u,
        "Stretch pitch did not preserve the mapped slice duration");
    engine.killAll();
    check(engine.activeVoiceCount() == 0u
            && engine.voiceCursorCount() == 0u,
        "Kill All did not clear inherited Slicer playback state");
}

void testStopSlotEvent()
{
    BankSnapshot bank;
    initializeEmptyBank(bank);
    bank.slots[0u] = oneSliceSlot(
        constantAsset(0.5f, 0.25f, 128u, 1000.0));
    check(mapSlotConsecutively(bank, 0u, 60u) && bank.valid(),
        "slot-stop fixture was invalid");

    SlicerEngine engine;
    check(engine.prepare(1000.0) && engine.setBank(&bank),
        "slot-stop fixture did not prepare");
    const std::array<RenderEvent, 2u> events {{
        { 0u, EventKind::NoteOn, 207u, 60u, 0u, 1.0f, 1u },
        { 32u, EventKind::StopSlot, 0u, 0u, 0u, 0.0f, 0u },
    }};
    std::array<float, 64u> left {};
    std::array<float, 64u> right {};
    engine.render(events.data(), events.size(), left.data(), right.data(),
        static_cast<uint32_t>(left.size()));
    const bool silentAfterStop = std::all_of(left.begin() + 32u,
        left.end(), [](float sample) { return sample == 0.0f; });
    check(left[16u] > 0.0f && silentAfterStop
            && engine.activeVoiceCount() == 0u,
        "the sample-accurate slot stop did not terminate playthrough audio");
}

void testInheritedLoopCrossfade()
{
    auto mutableAsset = std::make_shared<SampleAsset>();
    mutableAsset->sampleRate = 48000.0;
    mutableAsset->channelCount = 1u;
    mutableAsset->channels[0u].resize(16u);
    for (uint32_t frame = 0u; frame < 16u; ++frame)
        mutableAsset->channels[0u][frame]
            = static_cast<float>(frame) / 15.0f;
    std::shared_ptr<const SampleAsset> asset = mutableAsset;
    BankSnapshot bank;
    bank.slots[0u] = oneSliceSlot(asset, LaunchMode::Loop);
    bank.slots[0u].slices[0u].loopStartFrame = 4u;
    bank.slots[0u].slices[0u].loopEndFrame = 12u;
    bank.slots[0u].loopCrossfade = 0.0f;
    check(mapSlotConsecutively(bank, 0u, 60u) && bank.valid(),
        "Slicer crossfade fixture was invalid");

    const RenderEvent note {
        0u, EventKind::NoteOn, 206u, 60u, 0u, 1.0f, 1u,
    };
    SlicerEngine engine;
    check(engine.prepare(48000.0) && engine.setBank(&bank),
        "Slicer crossfade fixture did not prepare");
    std::array<float, 14u> dryLeft {};
    std::array<float, 14u> dryRight {};
    engine.render(&note, 1u, dryLeft.data(), dryRight.data(),
        static_cast<uint32_t>(dryLeft.size()));
    const float drySeam = std::abs(dryLeft[12u] - dryLeft[11u]);

    bank.slots[0u].loopCrossfade = 0.5f;
    engine.reset();
    check(engine.setBank(&bank), "Slicer crossfade bank was rejected");
    std::array<float, 14u> fadedLeft {};
    std::array<float, 14u> fadedRight {};
    engine.render(&note, 1u, fadedLeft.data(), fadedRight.data(),
        static_cast<uint32_t>(fadedLeft.size()));
    const float fadedSeam = std::abs(fadedLeft[12u] - fadedLeft[11u]);
    check(drySeam > 0.3f && fadedSeam < 0.08f
            && fadedSeam < drySeam * 0.25f,
        "inherited loop crossfade did not suppress the slice-loop seam");
}

void testPingPongDirectionAcrossProcessBlocks()
{
    auto mutableAsset = std::make_shared<SampleAsset>();
    mutableAsset->sampleRate = 48000.0;
    mutableAsset->channelCount = 2u;
    mutableAsset->channels[0u].resize(12u);
    mutableAsset->channels[1u].resize(12u);
    for (uint32_t frame = 0u; frame < 12u; ++frame) {
        const float sample = static_cast<float>(frame) / 16.0f;
        mutableAsset->channels[0u][frame] = sample;
        mutableAsset->channels[1u][frame] = sample;
    }
    std::shared_ptr<const SampleAsset> asset = mutableAsset;
    BankSnapshot bank;
    initializeEmptyBank(bank);
    bank.slots[0u] = oneSliceSlot(asset, LaunchMode::PingPong);
    bank.slots[0u].slices[0u].startFrame = 2u;
    bank.slots[0u].slices[0u].endFrame = 10u;
    bank.slots[0u].slices[0u].pan = -1.0f;
    check(mapSlotConsecutively(bank, 0u, 60u) && bank.valid(),
        "Slicer ping-pong fixture was invalid");

    SlicerEngine engine;
    check(engine.prepare(48000.0) && engine.setBank(&bank),
        "Slicer ping-pong fixture did not prepare");
    const RenderEvent note {
        0u, EventKind::NoteOn, 208u, 60u, 0u, 1.0f, 1u,
    };
    std::array<float, 10u> firstLeft {};
    std::array<float, 10u> firstRight {};
    engine.render(&note, 1u, firstLeft.data(), firstRight.data(),
        static_cast<uint32_t>(firstLeft.size()));
    std::array<float, 10u> secondLeft {};
    std::array<float, 10u> secondRight {};
    engine.render(nullptr, 0u, secondLeft.data(), secondRight.data(),
        static_cast<uint32_t>(secondLeft.size()));

    check(near(firstLeft[7u], 9.0f / 16.0f)
            && near(firstLeft[8u], 8.0f / 16.0f)
            && near(firstLeft[9u], 7.0f / 16.0f),
        "Slicer ping-pong did not reflect cleanly at the slice end");
    check(near(secondLeft[0u], 6.0f / 16.0f)
            && near(secondLeft[4u], 2.0f / 16.0f)
            && near(secondLeft[5u], 3.0f / 16.0f)
            && near(secondLeft[9u], 7.0f / 16.0f),
        "Slicer ping-pong direction reset at an audio-block boundary");
}

} // namespace

int main()
{
    testBankMappingAndValidation();
    testSourceSafeMutationVariations();
    testStructuralMutationUses();
    testNamedStructuralMutationOperations();
    testMutationSlicePeakCeiling();
    testAnalysisAndSliceEditing();
    testTwoSlotSampleAccuratePlayback();
    testReversePitchAndPan();
    testSixteenChannelSliceLock();
    testWideOutputSilencesUnusedChannels();
    testCleanMixKeepsFloatingPointHeadroom();
    testMixerEqAndAuxBus();
    testPriorityLaneInserts();
    testRealtimeMixerPublicationPreservesPlayback();
    testBreakBusCore();
    testProportionalSliceEnvelope();
    testLoopGateAndChoke();
    testInheritedSamplePlaybackBehavior();
    testStopSlotEvent();
    testInheritedLoopCrossfade();
    testPingPongDirectionAcrossProcessBlocks();
    if (failures == 0)
        std::cout << "breakbeat slicer smoke tests passed\n";
    return failures == 0 ? 0 : 1;
}
