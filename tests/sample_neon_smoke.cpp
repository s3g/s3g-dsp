#include "s3g_reloop_neon.h"
#include "s3g_sample_neon.h"
#include "s3g_sample_neon_edit.h"
#include "../plugins/clap_sample_neon/s3g_sample_neon_layout.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>

namespace {

int failures = 0;

void check(bool condition, const char* message)
{
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

s3g::sample::SampleAsset constantAsset(float left, float right,
    uint32_t frames = 512u)
{
    s3g::sample::SampleAsset result;
    result.sampleRate = 48000.0;
    result.channelCount = 2u;
    result.channels[0u].assign(frames, left);
    result.channels[1u].assign(frames, right);
    return result;
}

s3g::sample::SampleAsset rampAsset(uint32_t frames = 512u)
{
    s3g::sample::SampleAsset result;
    result.sampleRate = 48000.0;
    result.channelCount = 2u;
    result.channels[0u].resize(frames);
    result.channels[1u].resize(frames);
    for (uint32_t frame = 0u; frame < frames; ++frame) {
        const float value = static_cast<float>(frame)
            / static_cast<float>(frames);
        result.channels[0u][frame] = value;
        result.channels[1u][frame] = value;
    }
    return result;
}

struct OutputBlock {
    static constexpr uint32_t kFrames = 64u;
    std::array<std::array<float, kFrames>,
        s3g::sample::kSampleNeonOutputChannels> samples {};
    std::array<float*, s3g::sample::kSampleNeonOutputChannels> pointers {};

    OutputBlock()
    {
        for (std::size_t channel = 0u; channel < pointers.size(); ++channel)
            pointers[channel] = samples[channel].data();
    }
};

void testProtocolDecode()
{
    using namespace s3g::controller::reloop_neon;
    PerformanceState state;

    const Action bankC = decode({ 0x95u, 0x00u, 0x7fu });
    check(bankC.type == ActionType::SelectBank && bankC.bank == 2u
            && bankC.pressed && state.apply(bankC) && state.bank == 2u,
        "bank C did not decode");

    const Action samplePad = decode({ 0x97u, 0x03u, 100u });
    check(samplePad.type == ActionType::Pad
            && samplePad.mode == Mode::Sampler && samplePad.pad == 3u
            && samplePad.layer == Layer::First && samplePad.pressed,
        "first-layer sample pad did not decode");
    state.apply(samplePad);
    check(state.slotForPad(samplePad.pad) == 19u,
        "bank/pad did not map to one of 32 slots");

    const Action secondLayer = decode({ 0x9au, 0x6fu, 127u });
    check(secondLayer.type == ActionType::Pad
            && secondLayer.mode == Mode::Slicer
            && secondLayer.layer == Layer::Second
            && secondLayer.bank == 3u && secondLayer.pad == 7u,
        "second-layer deck-D slicer pad did not decode");
    state.apply(secondLayer);
    check(state.bank == 3u && state.performanceIndex(7u) == 15u,
        "second layer did not address performance index 15");

    const Action hotLoop = decode({ 0x94u, 0x08u, 127u });
    check(hotLoop.type == ActionType::SelectMode
            && hotLoop.mode == Mode::HotLoop
            && hotLoop.layer == Layer::First,
        "Hot Loop mode button did not decode");

    const Action pressure = decode({ 0xa8u, 0x72u, 64u });
    check(pressure.type == ActionType::PadPressure
            && pressure.mode == Mode::HotCue && pressure.pad == 2u,
        "poly-pressure pad message did not decode");
    const auto velocity = decode({0xb8u, 0x72u, 3u});
    check(velocity.type == ActionType::PadVelocity && velocity.pad == 2u
        && velocity.mode == Mode::HotCue, "Velocity CC was confused with aftertouch");

    const Action traxClockwise = decode({ 0xb6u, 0x00u, 0x03u });
    const Action loopCounterClockwise = decode({ 0xb6u, 0x05u, 0x7fu });
    const Action centeredClockwise = decode({ 0xb6u, 0x00u, 0x41u });
    const Action centeredCounterClockwise = decode({ 0xb6u, 0x00u, 0x3fu });
    check(traxClockwise.type == ActionType::EncoderTurn
            && traxClockwise.encoder == Encoder::Trax
            && traxClockwise.delta == 3,
        "TRAX relative encoder did not decode");
    check(loopCounterClockwise.type == ActionType::EncoderTurn
            && loopCounterClockwise.encoder == Encoder::Loop
            && loopCounterClockwise.bank == 1u
            && loopCounterClockwise.delta == -1,
        "LOOP relative encoder did not decode");
    check(centeredClockwise.delta == 1 && centeredCounterClockwise.delta == -1,
        "63/65 relative encoder dialect did not decode");

    const Action loopPushC = decode({ 0x96u, 0x26u, 127u });
    check(loopPushC.type == ActionType::EncoderPush
            && loopPushC.encoder == Encoder::Loop
            && loopPushC.bank == 2u,
        "per-deck LOOP press did not decode");

    const Action release = decode({ 0x87u, 0x03u, 64u });
    check(release.type == ActionType::Pad && !release.pressed,
        "note-off pad release did not decode");
}

void testLedDiffs()
{
    using namespace s3g::controller::reloop_neon;
    for (uint8_t mode = 0u; mode < 4u; ++mode) {
        const auto hue = secondaryPadColor(static_cast<Mode>(mode));
        check(hue != 0u && hue != 48u && hue != 104u && hue != 127u,
            "Secondary page reused the primary cell palette");
        check(secondaryPadColor(static_cast<Mode>(mode), true) == 127u,
            "Secondary active feedback lost its white accent");
        for (uint8_t other = mode + 1u; other < 4u; ++other)
            check(hue != secondaryPadColor(static_cast<Mode>(other)),
                "Secondary pages share a palette index");
    }
    check(padLedMessage(0u, PadStatusLamp::OneShot, 127u)
                == MidiMessage { 0x9bu, 0x20u, 127u }
            && padLedMessage(0u, PadStatusLamp::Toggle, 127u)
                == MidiMessage { 0x9bu, 0x21u, 127u }
            && padLedMessage(0u, PadStatusLamp::Hold, 127u)
                == MidiMessage { 0x9bu, 0x22u, 127u }
            && padLedMessage(0u, PadStatusLamp::Repeat, 127u)
                == MidiMessage { 0x9bu, 0x23u, 127u }
            && padLedMessage(0u, PadStatusLamp::Sync, 127u)
                == MidiMessage { 0x9bu, 0x24u, 127u },
        "printed One Shot/Toggle/Hold/Repeat/Sync lamp order changed");
    LedFrame frame;
    frame.pads[0u].surface = 96u;
    frame.pads[0u].segments[0u] = 127u;
    frame.pads[7u].segments[4u] = 64u;
    std::array<MidiMessage, kMaximumLedMessages> messages {};
    LedDiffEncoder encoder;
    const std::size_t first = encoder.encode(frame, messages.data(),
        messages.size());
    check(first == kFullLedFrameMessages
            && messages[0u] == MidiMessage { 0x93u, 0x00u, 127u }
            && messages[1u] == MidiMessage { 0x93u, 0x05u, 127u }
            && messages[2u] == MidiMessage { 0x97u, 0x00u, 96u }
            && messages[3u] == MidiMessage { 0x9bu, 0x20u, 127u }
            && messages[first - 1u]
                == MidiMessage { 0x9bu, 0x47u, 64u },
        "initial LED frame was not fully encoded");
    check(encoder.encode(frame, messages.data(), messages.size()) == 0u,
        "unchanged LED frame was not coalesced");
    frame.pads[3u].segments[2u] = 91u;
    const std::size_t changed = encoder.encode(frame, messages.data(),
        messages.size());
    check(changed == 1u
            && messages[0u] == MidiMessage { 0x9bu, 0x31u, 91u },
        "single LED change produced the wrong MIDI message");
    frame.mode = Mode::Slicer;
    frame.layer = Layer::Second;
    const std::size_t switched = encoder.encode(frame, messages.data(),
        messages.size());
    check(switched == kMaximumLedMessages
            && messages[0u] == MidiMessage { 0x97u, 0x00u, 0u }
            && messages[8u] == MidiMessage { 0x93u, 0x01u, 127u }
            && messages[9u] == MidiMessage { 0x93u, 0x0au, 127u }
            && messages[10u] == MidiMessage { 0x97u, 0x68u, 96u },
        "mode/layer LED switch did not retarget the large pads");
    check(enableFourDecksSysEx()
            == std::array<uint8_t, 4u> {{ 0xf0u, 0x0au, 0x00u, 0xf7u }},
        "four-deck enable SysEx changed");
    frame = {}; frame.pads[0u].surface = 48u;
    encoder.invalidate(); encoder.encode(frame, messages.data(), messages.size());
    frame.bank = 1u;
    check(encoder.encode(frame, messages.data(), messages.size()) == kFullLedFrameMessages
        && messages[0u] == MidiMessage {0x94u, 0u, 127u}
        && messages[2u] == MidiMessage {0x97u, 0u, 48u},
        "Sampler bank switch darkened its shared pad addresses");
    check(encoder.encode(frame, messages.data(), messages.size(), false, true) == 48u
        && messages[0u] == MidiMessage {0x97u, 0u, 48u},
        "Pad-only refresh did not restore unchanged inventory without bank commands");
    check(encoder.encode(frame, messages.data(), messages.size()) == 0u,
        "Pad refresh corrupted diff state");

    LedRefreshRetries retries;
    check(!retries.update(frame, 0u) && !retries.update(frame, 49u)
        && retries.update(frame, 50u) && !retries.update(frame, 149u)
        && retries.update(frame, 150u) && !retries.update(frame, 349u)
        && retries.update(frame, 350u) && !retries.update(frame, 1000000u),
        "LED recovery retries did not end after the short settling window");
    frame.bank = 3u;
    check(!retries.update(frame, 1000010u) && retries.update(frame, 1000060u),
        "Bank change did not re-arm LED recovery");
    check(!retries.update(frame, 1000100u, true) && retries.update(frame, 1000150u),
        "Reselecting the same bank did not re-arm LED recovery");
    check(isStatusLedMessage({0x9bu, 0x24u, 127u}) && isStatusLedMessage({0x8bu, 0x47u, 0u})
        && !isStatusLedMessage({0x97u, 0x01u, 48u}) && !isStatusLedMessage({0x90u, 0x24u, 127u}),
        "LED-only guard consumed ordinary pads/notes or missed status lamps");
}

void testThirtyTwoSlotRouting()
{
    using namespace s3g::sample;
    auto first = constantAsset(0.25f, 0.5f);
    auto second = constantAsset(0.125f, 0.25f);
    auto engineStorage = std::make_unique<SampleNeonEngine>();
    auto& engine = *engineStorage;
    check(engine.prepare(48000.0, OutputBlock::kFrames)
            && engine.setAsset(0u, &first)
            && engine.setAsset(17u, &second),
        "32-slot routing fixture did not prepare");
    SampleNeonSettings settings;
    settings.masterGainDecibels = 0.0f;
    settings.outputLayout = SampleNeonOutputLayout::StereoStems;
    settings.slots[0u].gainDecibels = 0.0f;
    settings.slots[0u].outputBus = 0u;
    settings.slots[0u].character = SampleNeonMangleCharacter::Pulse;
    settings.slots[17u].gainDecibels = 0.0f;
    settings.slots[17u].outputBus = 15u;
    settings.slots[17u].character = SampleNeonMangleCharacter::Pulse;
    const std::array<SampleNeonEvent, 2u> events {{
        { 0u, SampleNeonEventKind::Trigger, 1u, 0u,
            s3g::controller::reloop_neon::Mode::Sampler, 0u, 1.0f },
        { 4u, SampleNeonEventKind::Trigger, 2u, 17u,
            s3g::controller::reloop_neon::Mode::Sampler, 1u, 1.0f },
    }};
    OutputBlock output;
    engine.render(settings, events.data(), events.size(),
        output.pointers.data(), kSampleNeonOutputChannels,
        OutputBlock::kFrames);
    check(std::abs(output.samples[0u][0u] - 0.25f) < 1.0e-5f
            && std::abs(output.samples[1u][0u] - 0.5f) < 1.0e-5f,
        "slot 1 did not reach output pair 1");
    check(output.samples[30u][3u] == 0.0f
            && std::abs(output.samples[30u][4u] - 0.125f) < 1.0e-5f
            && std::abs(output.samples[31u][4u] - 0.25f) < 1.0e-5f,
        "slot 18 did not reach output pair 16 sample-accurately");
    check(engine.activeVoiceCount() == 2u,
        "slot triggers did not create independent voices");
}

void testPerformanceWindowsAndPressure()
{
    using namespace s3g::sample;
    auto ramp = rampAsset();
    auto engineStorage = std::make_unique<SampleNeonEngine>();
    auto& engine = *engineStorage;
    check(engine.prepare(48000.0, OutputBlock::kFrames)
            && engine.setAsset(0u, &ramp),
        "performance-window fixture did not prepare");
    SampleNeonSettings settings;
    settings.masterGainDecibels = 0.0f;
    settings.slots[0u].gainDecibels = 0.0f;
    settings.slots[0u].velocitySensitivity = 0.0f;
    settings.slots[0u].character = SampleNeonMangleCharacter::Pulse;
    const std::array<SampleNeonEvent, 2u> events {{
        { 0u, SampleNeonEventKind::Pressure, 1u, 0u,
            s3g::controller::reloop_neon::Mode::Slicer, 16u, 0.75f },
        { 0u, SampleNeonEventKind::Trigger, 2u, 0u,
            s3g::controller::reloop_neon::Mode::Slicer, 16u, 1.0f },
    }};
    OutputBlock output;
    engine.render(settings, events.data(), events.size(),
        output.pointers.data(), 2u, OutputBlock::kFrames);
    check(std::abs(engine.slotPressure(0u) - 0.75f) < 1.0e-6f,
        "pad pressure was not retained as a live macro");
    check(output.samples[0u][0u] > 0.45f
            && output.samples[0u][0u] < 0.55f,
        "Slicer pad 17 did not begin at the halfway window");

    engine.reset();
    settings.slots[0u].start = 0.25;
    settings.slots[0u].end = 0.75;
    const SampleNeonEvent rangedSlice {
        0u, SampleNeonEventKind::Trigger, 3u, 0u,
        s3g::controller::reloop_neon::Mode::Slicer, 0u, 1.0f,
    };
    OutputBlock rangedOutput;
    engine.render(settings, &rangedSlice, 1u,
        rangedOutput.pointers.data(), 2u, OutputBlock::kFrames);
    check(rangedOutput.samples[0u][0u] > 0.1f
            && rangedOutput.samples[0u][0u] < 0.13f,
        "Slicer ignored the slot Start/End range");

    engine.reset();
    settings.slots[0u].start = 0.0;
    settings.slots[0u].end = 1.0;
    settings.slots[0u].sliceCount = 8u;
    const SampleNeonEvent variableCountSlice {
        0u, SampleNeonEventKind::Trigger, 4u, 0u,
        s3g::controller::reloop_neon::Mode::Slicer, 4u, 1.0f,
    };
    OutputBlock variableCountOutput;
    engine.render(settings, &variableCountSlice, 1u,
        variableCountOutput.pointers.data(), 2u, OutputBlock::kFrames);
    check(variableCountOutput.samples[0u][0u] > 0.45f
            && variableCountOutput.samples[0u][0u] < 0.55f,
        "variable Slicer count did not redistribute the active range");

    engine.reset();
    settings.slots[0u].sliceCount = 32u;
    settings.slots[0u].releaseProportion = 0.0f;
    settings.slots[0u].sliceTriggers[0u] =
        SampleNeonSliceTriggerMode::Hold;
    const std::array<SampleNeonEvent, 2u> gated {{
        { 0u, SampleNeonEventKind::Trigger, 5u, 0u,
            s3g::controller::reloop_neon::Mode::Slicer, 0u, 1.0f },
        { 1u, SampleNeonEventKind::Release, 5u, 0u,
            s3g::controller::reloop_neon::Mode::Slicer, 0u, 0.0f },
    }};
    OutputBlock gatedOutput;
    engine.render(settings, gated.data(), gated.size(),
        gatedOutput.pointers.data(), 2u, OutputBlock::kFrames);
    check(engine.activeVoiceCount() == 0u,
        "per-slice Gate mode ignored pad release");

    engine.reset();
    settings.slots[0u].sliceTriggers[0u] =
        SampleNeonSliceTriggerMode::OneShot;
    OutputBlock oneShotOutput;
    engine.render(settings, gated.data(), gated.size(),
        oneShotOutput.pointers.data(), 2u, OutputBlock::kFrames);
    check(oneShotOutput.samples[0u][2u] > 0.001f
            && gatedOutput.samples[0u][2u] == 0.0f,
        "per-slice One Shot mode did not outlast Gate release");

    engine.reset();
    settings.slots[0u].sliceTriggers[0u] =
        SampleNeonSliceTriggerMode::Toggle;
    const std::array<SampleNeonEvent, 3u> toggled {{
        { 0u, SampleNeonEventKind::Trigger, 7u, 0u,
            s3g::controller::reloop_neon::Mode::Slicer, 0u, 1.0f },
        { 1u, SampleNeonEventKind::Release, 7u, 0u,
            s3g::controller::reloop_neon::Mode::Slicer, 0u, 0.0f },
        { 2u, SampleNeonEventKind::Trigger, 7u, 0u,
            s3g::controller::reloop_neon::Mode::Slicer, 0u, 1.0f },
    }};
    OutputBlock toggleOutput;
    engine.render(settings, toggled.data(), toggled.size(),
        toggleOutput.pointers.data(), 2u, OutputBlock::kFrames);
    check(engine.activeVoiceCount() == 0u,
        "per-slice Toggle mode did not stop on its second press");

    engine.reset();
    settings.slots[0u].sliceTriggers[0u] =
        SampleNeonSliceTriggerMode::OneShot;
    settings.slots[0u].sliceRepeat[0u] = true;
    settings.slots[0u].sliceSync[0u] = true;
    settings.hostTempoBpm = 240.0f;
    const SampleNeonEvent repeated {
        0u, SampleNeonEventKind::Trigger, 8u, 0u,
        s3g::controller::reloop_neon::Mode::Slicer, 0u, 1.0f,
    };
    OutputBlock repeatedOutput;
    engine.render(settings, &repeated, 1u,
        repeatedOutput.pointers.data(), 2u, OutputBlock::kFrames);
    check(engine.activeVoiceCount() == 1u
            && repeatedOutput.samples[0u][63u] > 0.001f,
        "fourth Repeat lamp did not produce continuous slice looping");
    check(repeatedOutput.samples[0u][1u] > 0.003f,
        "fifth Sync lamp did not apply the host-tempo ratio");
    settings.slots[0u].sliceRepeat[0u] = false;
    settings.slots[0u].sliceSync[0u] = false;
    settings.hostTempoBpm = 120.0f;

    engine.reset();
    const SampleNeonEvent loop {
        0u, SampleNeonEventKind::Trigger, 6u, 0u,
        s3g::controller::reloop_neon::Mode::HotLoop, 7u, 1.0f,
    };
    OutputBlock loopOutput;
    engine.render(settings, &loop, 1u, loopOutput.pointers.data(), 2u,
        OutputBlock::kFrames);
    check(engine.activeVoiceCount() == 1u
            && std::isfinite(loopOutput.samples[0u][63u]),
        "Hot Loop did not remain active through its wrapped window");

    engine.reset();
    const SampleNeonEvent reverse {
        0u, SampleNeonEventKind::Trigger, 7u, 0u,
        s3g::controller::reloop_neon::Mode::Sampler, 0u, 1.0f, true,
    };
    OutputBlock reverseOutput;
    engine.render(settings, &reverse, 1u,
        reverseOutput.pointers.data(), 2u, OutputBlock::kFrames);
    check(reverseOutput.samples[0u][0u] > 0.9f,
        "shifted Sampler pad did not begin in reverse");
}

void testV07V08PerformanceModel()
{
    using namespace s3g::sample;
    auto live = equalSampleNeonSliceLayout(4u);
    check(addSampleNeonSliceMarker(live, 0.375)
            && live.sliceCount == 5u
            && moveSampleNeonSliceMarker(live, 2u, 0.4)
            && std::abs(live.boundaries[2u] - 0.4) < 1.0e-9,
        "Live Mark chop editing did not preserve ordered boundaries");
    const std::array<float, 4u> transients {{ 0.0f, 0.13f, 0.51f, 0.8f }};
    const auto transient = transientSampleNeonSliceLayout(
        transients.data(), transients.size(), 4u);
    check(transient.valid() && transient.sliceCount == 4u
            && std::abs(transient.boundaries[2u] - 0.51) < 0.001,
        "Transient chop layout did not use analyzed starts");
    const auto lead = transientSampleNeonSliceLayout(transients.data(), transients.size(), 4u, 20.0, 2.0);
    check(lead.valid() && lead.sliceCount == 4u && lead.boundaries[0] == 0.0
        && lead.boundaries[4] == 1.0 && std::abs(lead.boundaries[2] - 0.50) < 1.0e-6,
        "Transient pre-roll did not lead attacks by milliseconds while preserving trim edges");
    const auto shortTrim = transientSampleNeonSliceLayout(transients.data(), transients.size(), 4u, 50.0, 0.1);
    check(shortTrim.valid() && shortTrim.sliceCount == 3u
        && std::abs(shortTrim.boundaries[1] - 0.01) < 1.0e-6,
        "Pre-roll did not clamp/collapse starts before a short trimmed source");
    const auto restored = transientSampleNeonSliceLayout(transients.data(), transients.size(), 4u, 0.0, 0.1);
    check(restored.boundaries == transient.boundaries && restored.sliceCount == 4u,
        "Returning pre-roll to zero did not restore original transient starts");
    check(transientSampleNeonSliceLayout(transients.data(), transients.size(), 2u, 10.0, 2.0).sliceCount == 2u,
        "Pre-roll ignored the requested maximum slices");
    check(transientSampleNeonSliceLayout(transients.data(), transients.size(), 4u, 50.0, 0.0).boundaries == transient.boundaries,
        "Pre-roll divided by an absent source duration");
    const auto grid = beatGridSampleNeonSliceLayout(4.0, 120.0, 1.0);
    check(grid.valid() && grid.sliceCount == 8u
            && std::abs(grid.boundaries[1u] - 0.125) < 1.0e-9,
        "Beat Grid chop layout did not follow BPM and division");

    auto ramp = rampAsset();
    auto tone = constantAsset(1.0f, 1.0f);
    auto engineStorage = std::make_unique<SampleNeonEngine>();
    auto& engine = *engineStorage;
    check(engine.prepare(48000.0, OutputBlock::kFrames)
            && engine.setAsset(0u, &ramp)
            && engine.setAsset(1u, &tone),
        "v0.7/v0.8 engine fixture did not prepare");
    SampleNeonSettings settings;
    settings.masterGainDecibels = 0.0f;
    settings.slots[0u].gainDecibels = 0.0f;
    settings.slots[0u].velocitySensitivity = 0.0f;
    settings.slots[0u].character = SampleNeonMangleCharacter::Pulse;
    settings.slots[0u].sliceCount = 2u;
    settings.slots[0u].sliceLayout = equalSampleNeonSliceLayout(2u);
    settings.slots[0u].sliceLayout.boundaries[1u] = 0.25;
    const SampleNeonEvent customSlice {
        0u, SampleNeonEventKind::Trigger, 20u, 0u,
        s3g::controller::reloop_neon::Mode::Slicer, 1u, 1.0f,
    };
    OutputBlock customOutput;
    engine.render(settings, &customSlice, 1u,
        customOutput.pointers.data(), 2u, OutputBlock::kFrames);
    check(customOutput.samples[0u][0u] > 0.20f
            && customOutput.samples[0u][0u] < 0.30f,
        "engine ignored a custom 32-boundary chop layout");

    engine.reset();
    settings.slots[0u].sliceCount = 8u;
    settings.slots[0u].sliceLayout = equalSampleNeonSliceLayout(8u);
    settings.slots[0u].sourceDurationSeconds = 8.0;
    settings.slots[0u].sourceTempoBpm = 120.0;
    settings.slots[0u].slicerDomainStart = 0.5;
    settings.slots[0u].slicerDomainBeats = 2.0;
    settings.slots[0u].slicerQuantizeBeats = 0.125;
    const SampleNeonEvent domainSlice {
        0u, SampleNeonEventKind::Trigger, 26u, 0u,
        s3g::controller::reloop_neon::Mode::Slicer, 7u, 1.0f,
    };
    OutputBlock domainOutput;
    engine.render(settings, &domainSlice, 1u,
        domainOutput.pointers.data(), 2u, OutputBlock::kFrames);
    check(domainOutput.samples[0u][0u] > 0.85f
            && domainOutput.samples[0u][0u] < 0.95f,
        "legacy Slicer transforms altered the visible shared slice map");

    engine.reset();
    settings.slots[0u].sliceCount = 32u;
    settings.slots[0u].sliceLayout = equalSampleNeonSliceLayout(32u);
    settings.slots[0u].sourceDurationSeconds = 0.0;
    settings.slots[0u].sliceTriggers[0u] =
        SampleNeonSliceTriggerMode::OneShot;
    const std::array<SampleNeonEvent, 2u> loopedSlicer {{
        { 0u, SampleNeonEventKind::Trigger, 21u, 0u,
            s3g::controller::reloop_neon::Mode::Slicer, 0u, 1.0f,
            false, false, true },
        { 40u, SampleNeonEventKind::Release, 21u, 0u,
            s3g::controller::reloop_neon::Mode::Slicer, 0u, 0.0f,
            false, false, true },
    }};
    OutputBlock loopedOutput;
    engine.render(settings, loopedSlicer.data(), loopedSlicer.size(),
        loopedOutput.pointers.data(), 2u, OutputBlock::kFrames);
    check(loopedOutput.samples[0u][33u] > 0.001f
            && engine.activeVoiceCount() == 0u,
        "secondary Slicer layer was not a held Looped Slicer");

    engine.reset();
    settings.slots[0u].sliceCount = 4u;
    settings.slots[0u].sliceLayout = equalSampleNeonSliceLayout(4u);
    const SampleNeonEvent cue {
        0u, SampleNeonEventKind::Trigger, 22u, 0u,
        s3g::controller::reloop_neon::Mode::HotCue, 3u, 1.0f,
    };
    OutputBlock cueOutput;
    engine.render(settings, &cue, 1u, cueOutput.pointers.data(), 2u,
        OutputBlock::kFrames);
    check(cueOutput.samples[0u][0u] > 0.70f,
        "Jump did not use the shared slice map");

    engine.reset();
    const SampleNeonEvent reverseJump {
        0u, SampleNeonEventKind::Trigger, 221u, 0u,
        s3g::controller::reloop_neon::Mode::HotCue, 3u, 1.0f,
        false, false, false, true,
    };
    OutputBlock reverseJumpOutput;
    engine.render(settings, &reverseJump, 1u,
        reverseJumpOutput.pointers.data(), 2u, OutputBlock::kFrames);
    check(reverseJumpOutput.samples[0u][0u] > 0.95f,
        "Jump second layer did not reverse the selected slice");

    engine.reset();
    const SampleNeonEvent heldLoop {
        0u, SampleNeonEventKind::Trigger, 23u, 0u,
        s3g::controller::reloop_neon::Mode::HotLoop, 2u, 1.0f,
    };
    OutputBlock heldLoopOutput;
    engine.render(settings, &heldLoop, 1u,
        heldLoopOutput.pointers.data(), 2u, OutputBlock::kFrames);
    check(heldLoopOutput.samples[0u][0u] > 0.45f
            && engine.activeVoiceCount() == 1u,
        "Loop did not repeat a window from the shared slice map");

    engine.reset();
    const SampleNeonEvent pingPongLoop {
        0u, SampleNeonEventKind::Trigger, 231u, 0u,
        s3g::controller::reloop_neon::Mode::HotLoop, 2u, 1.0f,
        false, false, false, true,
    };
    OutputBlock pingPongStart;
    engine.render(settings, &pingPongLoop, 1u,
        pingPongStart.pointers.data(), 2u, OutputBlock::kFrames);
    const bool pingPongStarted = engine.activeVoiceCount() == 1u;
    OutputBlock pingPongStop;
    engine.render(settings, &pingPongLoop, 1u,
        pingPongStop.pointers.data(), 2u, OutputBlock::kFrames);
    check(pingPongStarted && engine.activeVoiceCount() == 0u,
        "Loop second layer did not toggle its ping-pong slice");

    engine.reset();
    settings.slots[0u].chokeGroup = 2u;
    settings.slots[1u].chokeGroup = 2u;
    settings.slots[1u].gainDecibels = 0.0f;
    settings.slots[1u].velocitySensitivity = 0.0f;
    settings.slots[1u].character = SampleNeonMangleCharacter::Pulse;
    const std::array<SampleNeonEvent, 2u> choked {{
        { 0u, SampleNeonEventKind::Trigger, 24u, 0u,
            s3g::controller::reloop_neon::Mode::Sampler, 0u, 1.0f },
        { 8u, SampleNeonEventKind::Trigger, 25u, 1u,
            s3g::controller::reloop_neon::Mode::Sampler, 0u, 1.0f },
    }};
    OutputBlock chokeOutput;
    engine.render(settings, choked.data(), choked.size(),
        chokeOutput.pointers.data(), 2u, OutputBlock::kFrames);
    check(engine.activeVoiceCount() == 1u
            && chokeOutput.samples[0u][8u] > 0.9f,
        "choke groups did not stop peers before the new trigger");
}

void testPlaybackTechniques()
{
    using namespace s3g::sample;
    auto source = constantAsset(0.6f, 0.3f, 48000u);
    auto neighbor = constantAsset(0.9f, 0.9f, 48000u);
    auto engineStorage = std::make_unique<SampleNeonEngine>();
    auto& engine = *engineStorage;
    check(engine.prepare(48000.0, OutputBlock::kFrames)
        && engine.setAsset(0u, &source) && engine.setAsset(1u, &neighbor),
        "Technique fixture failed to prepare");
    SampleNeonSettings settings;
    settings.masterGainDecibels = 0.0f;
    auto& control = settings.slots[0];
    control.gainDecibels = 0.0f;
    control.character = SampleNeonMangleCharacter::Pulse;
    control.velocitySensitivity = 0.0f;
    control.triggerMode = TriggerMode::OneShot;
    control.shotSeconds = 0.05f;
    control.grainSizeMs = 500.0f;
    control.grainDensityHz = 80.0f;
    control.grainSpray = 0.0f;
    control.sourceDurationSeconds = 1.0;
    control.launchPosition = 0.1;
    SampleNeonEvent trigger { 13u, SampleNeonEventKind::Trigger, 30u, 0u,
        s3g::controller::reloop_neon::Mode::Sampler, 0u, 1.0f };
    OutputBlock output;
    control.motionCycleSeconds = 0.05f;
    control.techniqueAttackSeconds = 0.1f; control.techniqueReleaseSeconds = 0.2f;
    for (const auto technique : { SampleNeonPlayback::Motion, SampleNeonPlayback::Grains,
             SampleNeonPlayback::SliceSequence, SampleNeonPlayback::Stretch }) {
        engine.reset();
        control.playback = technique;
        control.clock = SampleNeonClock::Free;
        control.tuneSemitones = -24.0f; // Long, slow grains may not extend the shot.
        bool audible = false;
        uint64_t absolute = 0u;
        for (unsigned block = 0u; block < 55u; ++block) {
            engine.render(settings, block == 0u ? &trigger : nullptr, block == 0u ? 1u : 0u,
                output.pointers.data(), 2u, OutputBlock::kFrames);
            for (uint32_t frame = 0u; frame < OutputBlock::kFrames; ++frame, ++absolute) {
                const float sample = output.samples[0u][frame];
                if (absolute < 13u || absolute >= 2413u)
                    check(sample == 0.0f, "Generated one-shot exceeded its explicit sample-accurate duration");
                else audible = audible || std::abs(sample) > 0.0001f;
                check(std::abs(output.samples[1u][frame] - 0.5f * sample) < 1.0e-6f,
                    "Technique envelope broke linked channels");
            }
            check(engine.voiceCursorCount(1u) == 0u, "Playback mixed an adjacent cell");
        }
        check(audible, "FREE technique was silent with host transport stopped");
    }

    control.techniqueAttackSeconds = 0.005f; control.techniqueReleaseSeconds = 0.005f;
    engine.reset(); trigger.frameOffset = 0u;
    control.tuneSemitones = 0.0f; control.playback = SampleNeonPlayback::Motion;
    control.triggerMode = TriggerMode::Gate; control.motionCycleSeconds = 0.1f;
    engine.render(settings, &trigger, 1u, output.pointers.data(), 2u, OutputBlock::kFrames);
    const double before = engine.motionPosition(0u, settings);
    for (unsigned block = 0u; block < 10u; ++block)
        engine.render(settings, nullptr, 0u, output.pointers.data(), 2u, OutputBlock::kFrames);
    check(engine.motionPosition(0u, settings) > before + 0.1,
        "FREE Motion did not continuously scan while host was stopped");
    const auto cursors = engine.voiceCursors(0u);
    check(engine.voiceCursorCount(0u) > 0u, "Motion did not create scanning windows");
    (void)cursors;
    auto release = trigger; release.kind = SampleNeonEventKind::Release; release.frameOffset = 17u;
    engine.render(settings, &release, 1u, output.pointers.data(), 2u, OutputBlock::kFrames);
    for (unsigned block = 0u; block < 5u; ++block)
        engine.render(settings, nullptr, 0u, output.pointers.data(), 2u, OutputBlock::kFrames);
    check(std::all_of(output.samples[0].begin(), output.samples[0].end(), [](float v) { return v == 0.0f; }),
        "HOLD technique did not stop on release");

    engine.reset(); control.clock = SampleNeonClock::Host;
    settings.hostBeatValid = true; settings.hostBeatPosition = 2.0;
    engine.render(settings, &trigger, 1u, output.pointers.data(), 2u, OutputBlock::kFrames);
    check(std::all_of(output.samples[0].begin(), output.samples[0].end(), [](float v) { return v == 0.0f; }),
        "HOST clock emitted while transport was stopped");
    settings.transportPlaying = true;
    engine.render(settings, nullptr, 0u, output.pointers.data(), 2u, OutputBlock::kFrames);
    check(engine.voiceCursorCount(0u) > 0u
        && engine.motionPosition(0u, settings) > 0.3, "HOST clock did not resume/follow timeline");
    control.clock = SampleNeonClock::Free; settings.transportPlaying = false;
    engine.render(settings, nullptr, 0u, output.pointers.data(), 2u, OutputBlock::kFrames);
    check(*std::max_element(output.samples[0].begin(), output.samples[0].end()) > 0.0f,
        "Unlocking HOST playback did not resume with transport stopped");

    engine.reset(); control.playback = SampleNeonPlayback::Grains;
    control.triggerMode = TriggerMode::Toggle;
    engine.render(settings, &trigger, 1u, output.pointers.data(), 2u, OutputBlock::kFrames);
    engine.render(settings, &trigger, 1u, output.pointers.data(), 2u, OutputBlock::kFrames);
    for (unsigned i = 0u; i < 5u; ++i)
        engine.render(settings, nullptr, 0u, output.pointers.data(), 2u, OutputBlock::kFrames);
    check(std::all_of(output.samples[0].begin(), output.samples[0].end(), [](float v) { return v == 0.0f; }),
        "TOGGLE did not stop on the second strike");
    engine.render(settings, &trigger, 1u, output.pointers.data(), 2u, OutputBlock::kFrames);
    control.playback = SampleNeonPlayback::Sample;
    engine.render(settings, nullptr, 0u, output.pointers.data(), 2u, OutputBlock::kFrames);
    check(engine.activeVoiceCount() == 0u, "Selecting Sample left the old technique sounding");

    control.playback = SampleNeonPlayback::Grains;
    control.sliceCount = 2u; control.sliceLayout = equalSampleNeonSliceLayout(2u);
    SampleNeonEvent slice { 0u, SampleNeonEventKind::Trigger, 31u, 0u,
        s3g::controller::reloop_neon::Mode::Slicer, 1u, 1.0f };
    engine.render(settings, &slice, 1u, output.pointers.data(), 2u, OutputBlock::kFrames);
    check(engine.voiceCursorCount(0u) == 1u
        && engine.voiceCursors(0u)[0u].sourceStartNormalized > 0.49f,
        "CHOP preview did not bypass the selected playback technique");
}

void testTechniqueFades()
{
    using namespace s3g::sample;
    auto source = constantAsset(0.6f, 0.3f, 48000u);
    auto engine = std::make_unique<SampleNeonEngine>();
    auto reference = std::make_unique<SampleNeonEngine>();
    check(engine->prepare(48000.0, OutputBlock::kFrames) && reference->prepare(48000.0, OutputBlock::kFrames)
        && engine->setAsset(0u, &source) && reference->setAsset(0u, &source), "Fade fixture did not prepare");
    SampleNeonSettings settings;
    auto& cell = settings.slots[0]; cell.gainDecibels = 0.0f;
    cell.triggerMode = TriggerMode::Gate; cell.grainDensityHz = 80.0f;
    cell.grainSizeMs = 100.0f; cell.grainSpray = 0.0f;
    cell.techniqueAttackSeconds = 0.1f; cell.techniqueReleaseSeconds = 0.2f;
    SampleNeonEvent trigger; trigger.noteId = 31u;
    OutputBlock shaped, dry;
    for (auto method : {SampleNeonPlayback::Motion, SampleNeonPlayback::Grains}) {
        cell.playback = method; engine->reset(); reference->reset();
        auto flat = settings; flat.slots[0].techniqueAttackSeconds = 0.001f;
        bool heardLateRelease = false;
        // Release early in the attack (64 ms); continue grains through the
        // entire 200 ms tail, even though Motion windows last only 40 ms.
        constexpr unsigned holdBlocks = 48u;
        auto up = trigger; up.kind = SampleNeonEventKind::Release;
        for (unsigned block = 0u; block < 240u; ++block) {
            const auto* event = block == 0u ? &trigger : block == holdBlocks ? &up : nullptr;
            engine->render(settings, event, event ? 1u : 0u, shaped.pointers.data(), 2u, OutputBlock::kFrames);
            reference->render(flat, block == 0u ? &trigger : nullptr, block == 0u ? 1u : 0u,
                dry.pointers.data(), 2u, OutputBlock::kFrames);
            for (unsigned f = 0u; f < OutputBlock::kFrames; ++f) {
                const unsigned at = block * OutputBlock::kFrames + f;
                const unsigned releasedAt = holdBlocks * OutputBlock::kFrames;
                const float expected = at < releasedAt ? std::min(1.0f, (at + 1u) / 4800.0f)
                    : at < releasedAt + 9600u ? (releasedAt / 4800.0f) * (1.0f - (at - releasedAt) / 9600.0f) : 0.0f;
                if (at > 64u && at != releasedAt + 9599u)
                    check(std::abs(shaped.samples[0][f] - dry.samples[0][f] * expected) < 0.00001f,
                        "Gesture envelope jumped, drifted, or stopped generating during release");
                check(std::abs(shaped.samples[1][f] - shaped.samples[0][f] * 0.5f) < 0.00001f,
                    "Gesture fade changed linked channel relationships");
                if (at > releasedAt + 4800u && std::abs(shaped.samples[0][f]) > 0.00001f) heardLateRelease = true;
            }
        }
        check(heardLateRelease && !engine->slotPlaybackActive(0u), "Long release was truncated or stranded its generator");
    }
}

void testSpatialSources()
{
    using namespace s3g::sample;
    using Mode = s3g::controller::reloop_neon::Mode;
    for (const auto layout : { SampleNeonOutputLayout::Quad,
             SampleNeonOutputLayout::Octo, SampleNeonOutputLayout::Ambisonic1,
             SampleNeonOutputLayout::Ambisonic2, SampleNeonOutputLayout::Ambisonic3 }) {
        const uint32_t width = sampleNeonBusWidth(layout);
        SampleAsset asset;
        asset.sampleRate = 48000.0;
        asset.channelCount = static_cast<uint8_t>(width);
        for (uint32_t channel = 0u; channel < width; ++channel) {
            asset.channels[channel].resize(4096u);
            for (std::size_t frame = 0u; frame < 4096u; ++frame)
                asset.channels[channel][frame] = static_cast<float>(channel + 1u)
                    * (0.01f + 0.02f * std::sin(static_cast<float>(frame) * 0.03f));
        }
        auto engineStorage = std::make_unique<SampleNeonEngine>();
        auto& engine = *engineStorage;
        check(engine.prepare(48000.0, OutputBlock::kFrames) && engine.setAsset(0u, &asset),
            "spatial source did not prepare");
        SampleNeonSettings settings;
        settings.outputLayout = layout;
        settings.masterGainDecibels = 0.0f;
        auto& slot = settings.slots[0u];
        slot.gainDecibels = 0.0f;
        slot.sourceFormat = layout >= SampleNeonOutputLayout::Ambisonic1
            ? SampleNeonSourceFormat::Ambisonic : SampleNeonSourceFormat::Discrete;
        slot.outputBus = static_cast<uint8_t>(sampleNeonBusCount(layout) - 1u);
        slot.pan = 1.0f; // Must NOT alter spatial channel gains.
        slot.tuneSemitones = 7.0f;
        slot.sliceLayout = equalSampleNeonSliceLayout(4u);
        slot.sliceCount = 4u;
        slot.sourceDurationSeconds = 4096.0 / 48000.0;
        for (unsigned variant = 0u; variant < 4u; ++variant) {
            engine.reset();
            slot.playback = variant == 3u ? SampleNeonPlayback::Grains : SampleNeonPlayback::Sample;
            slot.grainSizeMs = 20.0f;
            slot.grainDensityHz = 80.0f;
            slot.grainSpray = 0.5f;
            slot.grainPitchSpraySemitones = 12.0f;
            slot.grainReverseChance = 0.5f;
            SampleNeonEvent event;
            event.noteId = 100u + variant;
            event.mode = variant == 1u ? Mode::Slicer : variant == 2u ? Mode::HotLoop : Mode::Sampler;
            event.performanceIndex = 2u;
            event.reverse = variant == 2u;
            OutputBlock output;
            float peak = 0.0f;
            const auto first = slot.outputBus * width;
            for (unsigned block = 0u; block < 20u; ++block) {
                engine.render(settings, block == 0u ? &event : nullptr,
                    block == 0u ? 1u : 0u, output.pointers.data(), 32u, OutputBlock::kFrames);
                for (uint32_t channel = 0u; channel < 32u; ++channel)
                    for (uint32_t frame = 0u; frame < OutputBlock::kFrames; ++frame) {
                        const float actual = output.samples[channel][frame];
                        const bool routed = channel >= first && channel < first + width;
                        const float expected = routed ? output.samples[first][frame]
                            * static_cast<float>(channel - first + 1u) : 0.0f;
                        check(std::isfinite(actual) && std::abs(actual - expected) < 1.0e-5f,
                            "spatial channels lost gain/time coherence or leaked outside bus");
                        peak = std::max(peak, std::abs(actual));
                    }
            }
            check(peak > 0.001f, "spatial playback/slice/loop/grain fixture was silent");
        }
        settings.outputLayout = SampleNeonOutputLayout::Stereo;
        slot.outputBus = 0u;
        engine.reset();
        SampleNeonEvent event;
        OutputBlock output;
        engine.render(settings, &event, 1u, output.pointers.data(), 32u, OutputBlock::kFrames);
        check(engine.outputPeak() == 0.0f, "spatial source was implicitly downmixed to stereo");
        settings.outputLayout = layout;
        slot.outputBus = static_cast<uint8_t>(sampleNeonBusCount(layout));
        engine.render(settings, &event, 1u, output.pointers.data(), 32u, OutputBlock::kFrames);
        check(engine.outputPeak() == 0.0f, "out-of-range bus wrapped into another bus");
    }
    check(!sampleNeonRouteCompatible(4u, SampleNeonSourceFormat::Discrete,
        SampleNeonOutputLayout::Ambisonic1, 0u), "quad was silently treated as 1OA");
    check(!sampleNeonRouteCompatible(4u, SampleNeonSourceFormat::Ambisonic,
        SampleNeonOutputLayout::Quad, 0u), "1OA was silently treated as quad");
    SampleAsset mono;
    mono.sampleRate = 48000.0;
    mono.channelCount = 1u;
    mono.channels[0u].assign(256u, 0.5f);
    auto engineStorage = std::make_unique<SampleNeonEngine>();
    auto& engine = *engineStorage;
    check(engine.prepare(48000.0, OutputBlock::kFrames) && engine.setAsset(0u, &mono),
        "mono fixture did not prepare");
    SampleNeonSettings settings;
    settings.masterGainDecibels = 0.0f;
    settings.slots[0u].gainDecibels = 0.0f;
    settings.slots[0u].character = SampleNeonMangleCharacter::Pulse; // No filter in this pan fixture.
    SampleNeonEvent event;
    OutputBlock output;
    engine.render(settings, &event, 1u, output.pointers.data(), 32u, OutputBlock::kFrames);
    check(output.samples[0u][32u] == 0.5f && output.samples[1u][32u] == 0.5f,
        "mono was not duplicated to both stereo channels");
    settings.slots[0u].pan = 1.0f;
    engine.reset();
    engine.render(settings, &event, 1u, output.pointers.data(), 32u, OutputBlock::kFrames);
    check(output.samples[0u][32u] == 0.0f && output.samples[1u][32u] == 0.5f,
        "stereo-only pan changed during multichannel migration");
}

void testPadVelocityPairing()
{
    using namespace s3g::controller::reloop_neon;
    PadInputDecoder decoder;
    decoder.prepare(48000.0);
    for (uint8_t channel = 7u; channel <= 10u; ++channel) {
        for (uint8_t address = 0u; address < 128u; ++address) {
            if (!decode({static_cast<uint8_t>(0x90u | channel), address, 127u})) continue;
            decoder.process({static_cast<uint8_t>(0xb0u | channel), address, 3u}, 63u);
            decoder.advance(64u);
            const auto hit = decoder.process({static_cast<uint8_t>(0x90u | channel), address, 127u}, 0u);
            check(hit.type == ActionType::Pad && hit.pressed && hit.value == 3u,
                "Factory velocity failed across block/page/deck/SHIFT address");
            check(decoder.process({static_cast<uint8_t>(0x90u | channel), address, 127u}, 1u).value == 127u,
                "Latched velocity was used more than once");
        }
    }
    for (uint8_t pad = 0u; pad < 8u; ++pad) decoder.process({0xb7u, pad, static_cast<uint8_t>(pad + 1u)}, 0u);
    for (uint8_t pad = 0u; pad < 8u; ++pad) {
        decoder.process({0xa7u, pad, 100u}, 0u);
        check(decoder.process({0x97u, pad, 127u}, 0u).value == pad + 1u,
            "Chord velocities leaked between pads or were replaced by aftertouch");
    }
    decoder.process({0xb7u, 0u, 20u}, 0u); decoder.advance(961u);
    check(!decoder.pending() && decoder.process({0x97u, 0u, 127u}, 0u).value == 127u,
        "Stale velocity survived the pairing timeout");
    decoder.process({0xb7u, 0u, 20u}, 0u);
    check(decoder.process({0x98u, 0u, 127u}, 0u).value == 127u, "Velocity leaked to another deck");
    decoder.process({0xb7u, 0u, 20u}, 0u);
    check(decoder.process({0x97u, 8u, 127u}, 0u).value == 127u, "Velocity leaked to CHOP");
    for (MidiMessage boundary : {MidiMessage{0x87u, 0u, 0u}, {0x94u, 0u, 127u},
            {0x93u, 5u, 127u}, {0x93u, 0x4eu, 127u}}) {
        decoder.process({0xb7u, 0u, 20u}, 0u); decoder.process(boundary, 1u);
        check(decoder.process({0x97u, 0u, 127u}, 2u).value == 127u,
            "Release/bank/page/velocity-toggle failed to clear a pending velocity");
    }
    decoder.process({0xb7u, 0u, 0u}, 0u);
    const auto quietest = decoder.process({0x97u, 0u, 127u}, 1u);
    check(quietest.pressed && quietest.value == 1u, "Zero measured velocity turned a real hit into note-off");
}

void testWaveformViewport()
{
    using namespace s3g::sample;
    using Display = SampleNeonWaveformDisplay;
    for (unsigned channels : {1u, 2u, 4u, 8u, 9u, 16u}) {
        check(sampleNeonWaveformRows(Display::CombinedPeaks, channels) == 1u
            && sampleNeonWaveformChannel(Display::CombinedPeaks, 0u) == -1,
            "Combined waveform must include all channels in one envelope");
        check(sampleNeonWaveformRows(Display::FirstChannel, channels) == 1u
            && sampleNeonWaveformChannel(Display::FirstChannel, 0u) == 0,
            "First-channel waveform must not switch to the loudest channel");
        check(sampleNeonWaveformRows(Display::AllChannels, channels) == channels,
            "All-channel waveform must include stereo as two rows");
        for (unsigned row = 0u; row < channels; ++row)
            check(sampleNeonWaveformChannel(Display::AllChannels, row) == static_cast<int>(row),
                "Stacked waveform reordered channels");
    }
    check(sampleNeonWaveformRows(Display::AllChannels, 0u) == 1u
        && sampleNeonWaveformRows(Display::AllChannels, 32u) == 16u,
        "Waveform row count must stay bounded for empty/invalid sources");
    using s3g::sample::SampleNeonWaveformViewport;
    SampleNeonWaveformViewport view;
    view.sync(1.0, 0.2);
    view.zoomAt(4.0, 0.7);
    check(std::abs(view.start + 0.7 * view.width() - 0.7) < 1.0e-9,
        "Mouse zoom did not preserve the point under the pointer");
    const double start = view.start;
    view.sync(4.0, 0.2); // Timer redraw must not follow the unchanged cursor.
    check(view.start == start, "Redraw undid pointer-anchored zoom");
    view.pan(0.25);
    check(view.start > start && view.lastCursor == 0.2, "Pan changed the edit cursor");
    view.sync(8.0, 0.2); // Hardware zoom still focuses the editing cursor.
    check(std::abs(view.start + view.width() * 0.5 - 0.2) < 1.0e-9,
        "Encoder zoom did not return to the edit cursor");
    view.zoomAt(500.0, 1.0);
    view.pan(1000.0);
    check(view.zoom == 32.0 && view.start + view.width() == 1.0,
        "Waveform zoom/pan escaped source bounds");
    view.zoomAt(0.1, 0.3);
    check(view.start == 0.0 && view.width() == 1.0, "Zoom out did not restore the full waveform");
    view.zoomAt(2.0, 0.5);
    view.sync(2.0, 0.9);
    check(view.start + view.width() >= 0.9, "Encoder cursor was stranded outside the view");
}

void testInspectorLayout()
{
    using I = s3g::sample_neon_gui::InspectorLayout;
    for (unsigned row = 0u; row <= 20u; ++row) {
        check(I::slider(row) + 7.0 == I::row(row)
            && I::menu(row) + 12.5 == I::row(row)
            && I::button(row) + I::buttonHeight * 0.5 == I::row(row),
            "Mixed right-panel controls do not share a row center");
        check(I::row(row + 1u) - I::row(row) == 28.0
            && I::row(row) + 13.5 < I::row(row + 1u) - 13.5,
            "Inspector rows are uneven or slider hit regions overlap");
    }
    for (unsigned count : {1u, 2u, 4u}) {
        check(I::columnLeft(count - 1u, count) + I::columnWidth(count) == I::left + I::width,
            "Action row has an uneven right margin");
        for (unsigned n = 1u; n < count; ++n)
            check(I::columnLeft(n, count) - I::columnLeft(n - 1u, count) - I::columnWidth(count) == I::buttonGap,
                "Action buttons have uneven gaps");
    }
    check(I::menuHeight == 15.0 && I::popupRowHeight == 18.0,
        "Inspector spacing changed compact family menu sizing");
}

void testSourceNormalization()
{
    using namespace s3g::sample;
    SampleAsset source;
    source.channelCount = 16u;
    source.sampleRate = 96000.0;
    for (uint8_t c = 0u; c < source.channelCount; ++c)
        source.channels[c] = {0.0f, (c + 1u) * 0.01f, -static_cast<float>(c + 1u) * 0.02f};
    const auto normalized = sampleNeonNormalize(source);
    const double gain = sampleNeonNormalizeGain(source);
    check(normalized.valid() && normalized.channelCount == 16u
        && normalized.sampleRate == source.sampleRate && normalized.frameCount() == 3u,
        "Normalization changed the source dimensions");
    for (uint8_t c = 0u; c < 16u; ++c)
        for (uint32_t f = 0u; f < 3u; ++f)
            check(std::abs(normalized.channels[c][f] - source.channels[c][f] * gain) < 1.0e-7,
                "Normalization changed channel ratios or polarity");
    check(std::abs(normalized.channels[15u][2u] + kSampleNeonNormalizePeak) < 1.0e-7
        && std::abs(sampleNeonNormalizeGain(normalized) - 1.0) < 1.0e-6,
        "Normalization missed -1 dBFS or was not idempotent");
    check(source.channels[15u][2u] == -0.32f, "Normalization modified its immutable source");
    check(sampleNeonNormalizeGain(constantAsset(0.0f, 0.0f)) == 0.0
        && !sampleNeonNormalize(constantAsset(0.0f, 0.0f)).valid(), "Silence normalization divided by zero");
    const auto attenuated = sampleNeonNormalize(constantAsset(2.0f, -4.0f));
    check(std::abs(attenuated.channels[1u][0u] + kSampleNeonNormalizePeak) < 1.0e-7,
        "Over-range normalization failed to attenuate");
    source.channels[0u][0u] = std::numeric_limits<float>::quiet_NaN();
    check(sampleNeonNormalizeGain(source) == 0.0, "Nonfinite normalization source was accepted");
}

void testBoundarySelectionAndCapture()
{
    using namespace s3g::sample;
    SampleAsset asset;
    asset.sampleRate = 48000.0;
    asset.channelCount = 1u;
    asset.channels[0u].assign(64u, 0.8f);
    asset.channels[0u][31u] = -0.02f;
    asset.channels[0u][32u] = 0.03f;
    check(sampleNeonBoundary(asset, 34u, 8u, 55u, 8u) == 32u,
        "mono boundary missed nearby zero crossing");
    asset.channelCount = 4u;
    for (uint8_t channel = 1u; channel < 4u; ++channel) {
        asset.channels[channel] = asset.channels[0u];
        for (auto& value : asset.channels[channel]) value *= channel % 2u ? -1.0f : 1.0f;
    }
    check(sampleNeonBoundary(asset, 34u, 8u, 55u, 8u) == 32u,
        "shared MC crossing was not selected");
    for (uint8_t channel = 0u; channel < 4u; ++channel) {
        asset.channels[channel].assign(64u, channel % 2u ? -0.8f : 0.8f);
        asset.channels[channel][31u] = 0.01f * (channel + 1u);
        asset.channels[channel][32u] = 0.02f * (channel + 1u);
    }
    // Channel 0 never crosses zero; signed channel sums elsewhere cancel.
    check(sampleNeonBoundary(asset, 35u, 8u, 55u, 8u) == 32u,
        "best-case MC boundary used signed cancellation instead of energy");
    check(sampleNeonBoundary(asset, 35u, 35u, 40u, 8u) >= 35u,
        "boundary search crossed its neighbor constraint");
    for (uint8_t channel = 0u; channel < 4u; ++channel)
        asset.channels[channel].assign(64u, 0.0f);
    check(sampleNeonBoundary(asset, 35u, 8u, 55u, 8u) == 35u,
        "silence shifted an already valid boundary");
    for (uint8_t channel = 0u; channel < 4u; ++channel) {
        asset.channels[channel].assign(1000u, 0.1f);
        asset.channels[channel][315u] = -0.1f;
        asset.channels[channel][316u] = 0.1f;
    }
    const auto crossing = sampleNeonBoundary(asset, 318u, 300u, 330u, 8u);
    check(sampleNeonBoundary(asset, crossing, 300u, 330u, 8u) == crossing,
        "an existing shared crossing drifted on repeated snapping");
    auto engineStorage = std::make_unique<SampleNeonEngine>();
    auto& engine = *engineStorage;
    check(engine.prepare(48000.0, OutputBlock::kFrames) && engine.setAsset(0u, &asset),
        "integer-frame slice fixture did not prepare");
    SampleNeonSettings settings;
    settings.outputLayout = SampleNeonOutputLayout::Quad;
    auto& slot = settings.slots[0u];
    slot.start = 173.0 / 1000.0; slot.end = 937.0 / 1000.0;
    slot.sliceCount = 3u;
    slot.sliceLayout = equalSampleNeonSliceLayout(3u);
    slot.sliceLayout.boundaries[1u] = (317.0 - 173.0) / (937.0 - 173.0);
    slot.sliceLayout.boundaries[2u] = (673.0 - 173.0) / (937.0 - 173.0);
    SampleNeonEvent slice;
    slice.mode = s3g::controller::reloop_neon::Mode::HotCue; slice.performanceIndex = 2u;
    OutputBlock rendered;
    engine.render(settings, &slice, 1u, rendered.pointers.data(), 32u, OutputBlock::kFrames);
    check(engine.voiceCursorCount(0u) > 0u
        && std::abs(engine.voiceCursors(0u)[0u].sourceStartNormalized - 0.673f) < 1.0e-6f
        && std::abs(engine.voiceCursors(0u)[0u].sourceEndNormalized - 0.937f) < 1.0e-6f,
        "normalized slice playback rounded a stored frame boundary incorrectly");
    SampleNeonRecorder recorder;
    check(recorder.prepare(8000.0), "capture preallocation failed");
    OutputBlock output;
    for (uint32_t channel = 0u; channel < 32u; ++channel)
        output.samples[channel].fill(static_cast<float>(channel) / 32.0f);
    recorder.start(4u);
    check(!recorder.append(output.pointers.data(), 64u, 8u)
        && !recorder.append(output.pointers.data(), 64u, 8u), "capture unexpectedly full");
    check(recorder.waveCount.load() > 0u && recorder.waveChannels.load() == 4u
        && recorder.waveform[0u][0u].minimum.load() == 0.25f
        && recorder.waveform[3u][0u].maximum.load() == 11.0f / 32.0f,
        "live waveform was not published before finishing recording");
    const auto captured = recorder.finish();
    check(captured.valid() && captured.channelCount == 4u && captured.frameCount() == 128u,
        "capture lost its shape or duration");
    check(captured.channels[0u][0u] == 0.25f && captured.channels[3u][127u] == 11.0f / 32.0f,
        "capture did not preserve selected bus order and levels");
    recorder.start(2u);
    check(recorder.waveCount.load() == 0u && recorder.waveChannels.load() == 2u,
        "new take retained the previous live waveform");
    for (uint8_t width : { 1u, 2u, 4u, 8u, 9u, 16u }) {
        SampleAsset source;
        source.sampleRate = 96000.0; source.channelCount = width;
        for (uint8_t ch = 0u; ch < width; ++ch) {
            source.channels[ch].resize(100u);
            for (uint32_t frame = 0u; frame < 100u; ++frame)
                source.channels[ch][frame] = (ch + 1u) * frame / 2000.0f;
        }
        const auto cropped = sampleNeonCrop(source, 17u, 83u);
        check(cropped.valid() && cropped.frameCount() == 66u && cropped.channelCount == width
            && cropped.sampleRate == 96000.0, "capture crop changed the asset shape or rate");
        for (uint8_t ch = 0u; ch < width; ++ch)
            for (uint32_t frame = 0u; frame < 66u; ++frame)
                check(cropped.channels[ch][frame] == source.channels[ch][frame + 17u],
                    "capture crop did not preserve linked-channel sample data");
        check(sampleNeonCrop(source, 83u, 17u).frameCount() == 0u
            && sampleNeonCrop(source, 0u, 101u).frameCount() == 0u,
            "crop accepted an inverted or out-of-bounds interval");
    }
}

void testCharacterFxAndNewPlayback()
{
    using namespace s3g::sample;
    SampleNeonFx fx;
    check(fx.prepare(48000.0), "FX preparation failed");
    OutputBlock output, dry;
    for (unsigned effect = 0u; effect < 8u; ++effect) {
        fx.reset();
        const auto character = static_cast<SampleNeonMangleCharacter>(effect);
        double difference = 0.0;
        bool linked = true, finite = true;
        for (unsigned block = 0u; block < 256u; ++block) {
            for (unsigned frame = 0u; frame < 64u; ++frame) {
                const double t = (block * 64u + frame) / 48000.0;
                const float a = static_cast<float>(0.21 * std::sin(t * 6200.0));
                const float b = static_cast<float>(0.13 * std::cos(t * 2311.0));
                output.samples[0u][frame] = a; output.samples[1u][frame] = b;
                for (unsigned ch = 2u; ch < 16u; ++ch)
                    output.samples[ch][frame] = a * (ch / 16.0f) + b * (1.0f - ch / 16.0f);
            }
            dry.samples = output.samples;
            fx.process(output.pointers.data(), 16u, 64u, character, kSampleNeonFxDefaults[effect], 0.8f, effect < 6u);
            for (unsigned frame = 0u; frame < 64u; ++frame) {
                difference += std::abs(output.samples[0u][frame] - dry.samples[0u][frame]);
                for (unsigned ch = 2u; ch < 16u; ++ch) {
                    finite = finite && std::isfinite(output.samples[ch][frame]);
                    linked = linked && std::abs(output.samples[ch][frame]
                        - output.samples[0u][frame] * (ch / 16.0f)
                        - output.samples[1u][frame] * (1.0f - ch / 16.0f)) < 2.0e-5f;
                }
            }
        }
        check(finite && difference > 0.1, "Character FX was silent, bypassed or non-finite");
        if (effect < 6u) check(linked, "Ambisonic-safe effect broke linear channel relationships");
        else {
            output.samples = dry.samples;
            fx.process(output.pointers.data(), 16u, 64u, character, kSampleNeonFxDefaults[effect], 1.0f, true);
            check(output.samples == dry.samples, "Ambisonic DSP did not bypass unsafe effect exactly");
        }
        fx.reset();
        for (auto& channel : output.samples) channel.fill(0.0f);
        fx.process(output.pointers.data(), 16u, 64u, character, kSampleNeonFxDefaults[effect], 1.0f, false);
        check(std::all_of(output.samples[0u].begin(), output.samples[0u].end(), [](float v) { return v == 0.0f; }),
            "reset leaked an old Character FX tail");
    }

    auto asset = std::make_shared<SampleAsset>();
    asset->sampleRate = 48000.0; asset->channelCount = 16u;
    for (unsigned ch = 0u; ch < 16u; ++ch) {
        asset->channels[ch].resize(48000u);
        for (unsigned frame = 0u; frame < 48000u; ++frame)
            asset->channels[ch][frame] = static_cast<float>(std::sin(frame * 0.06) * 0.02 * (ch + 1u));
    }
    const auto map = analyzeWavesets(asset);
    check(map && map->valid(), "Wavesets fixture analysis failed");
    auto engine = std::make_unique<SampleNeonEngine>();
    check(engine->prepare(48000.0, 64u) && engine->setAsset(0u, asset.get()), "modern playback fixture failed");
    engine->setPreparedWavesets(0u, map.get());
    SampleNeonSettings settings;
    settings.outputLayout = SampleNeonOutputLayout::Ambisonic3;
    settings.masterGainDecibels = 0.0f;
    auto& slot = settings.slots[0u];
    slot.gainDecibels = 0.0f; slot.velocityEnabled = false;
    slot.sourceFormat = SampleNeonSourceFormat::Ambisonic;
    slot.triggerMode = TriggerMode::OneShot;
    slot.shotSeconds = 0.5f; slot.motionCycleSeconds = 0.5f;
    slot.sliceCount = 4u; slot.sliceLayout = equalSampleNeonSliceLayout(4u);
    SampleNeonEvent trigger {0u, SampleNeonEventKind::Trigger, 7u, 0u,
        s3g::controller::reloop_neon::Mode::Sampler, 0u, 1.0f};
    // Discrete sixteen-channel assets intentionally do not fit an ambisonic
    // route. Use an octo fixture to verify Wavesets' real multichannel output.
    auto octo = std::make_shared<SampleAsset>(*asset); octo->channelCount = 8u;
    for (unsigned ch = 8u; ch < 16u; ++ch) octo->channels[ch].clear();
    const auto octoMap = analyzeWavesets(octo);
    check(octoMap && octoMap->valid(), "Octo Wavesets fixture analysis failed");
    for (unsigned method = 0u; method < 6u; ++method) {
        slot.playback = static_cast<SampleNeonPlayback>(method);
        slot.sourceFormat = method == 5u ? SampleNeonSourceFormat::Discrete : SampleNeonSourceFormat::Ambisonic;
        settings.outputLayout = method == 5u ? SampleNeonOutputLayout::Octo : SampleNeonOutputLayout::Ambisonic3;
        engine->setPreparedAsset(0u, method == 5u ? octo.get() : asset.get());
        engine->setPreparedWavesets(0u, method == 5u ? octoMap.get() : map.get());
        for (unsigned effect = 0u; effect < (method == 5u ? 8u : 6u); ++effect) {
            slot.character = static_cast<SampleNeonMangleCharacter>(effect); slot.fx = kSampleNeonFxDefaults[effect];
            std::array<float, 4096u> plain {};
            double difference = 0.0, energy = 0.0;
            for (unsigned pass = 0u; pass < 2u; ++pass) {
                engine->reset(); slot.mangle = pass ? 0.8f : 0.0f;
                for (unsigned block = 0u; block < 64u; ++block) {
                    engine->render(settings, block ? nullptr : &trigger, block ? 0u : 1u,
                        output.pointers.data(), 32u, 64u);
                    for (unsigned frame = 0u; frame < 64u; ++frame) {
                        const float sample = output.samples[0u][frame];
                        if (!pass) plain[block * 64u + frame] = sample;
                        else difference += std::abs(sample - plain[block * 64u + frame]);
                        energy += std::abs(sample);
                        check(std::abs(output.samples[7u][frame] - sample * 8.0f) < 2.0e-5f || effect >= 6u,
                            "playback/FX broke linked multichannel routing");
                    }
                }
            }
            if (!(energy > 0.01 && difference > 0.001))
                std::cerr << "method=" << method << " effect=" << effect << " energy=" << energy << " difference=" << difference << '\n';
            check(energy > 0.01 && difference > 0.001, "Character FX did not process a playback method");
            auto stop = trigger; stop.kind = SampleNeonEventKind::Choke; stop.noteId = 0u;
            engine->render(settings, &stop, 1u, output.pointers.data(), 32u, 64u);
            check(engine->activeVoiceCount() == 0u, "Stop left playback voices active");
        }
    }
    engine->reset(); slot.mangle = 0.0f;
    auto chop = trigger; chop.frameOffset = 32u;
    chop.mode = s3g::controller::reloop_neon::Mode::HotCue; chop.performanceIndex = 2u;
    std::array<SampleNeonEvent, 2u> transition {{trigger, chop}};
    engine->render(settings, transition.data(), transition.size(), output.pointers.data(), 32u, 64u);
    check(std::any_of(output.samples[0u].begin(), output.samples[0u].begin() + 32u, [](float v) { return std::abs(v) > 1.0e-7f; })
        && engine->voiceCursorCount(0u) == 1u && engine->voiceCursors(0u)[0u].sourceStartNormalized == 0.5f,
        "Wavesets to CHOP transition lost the first half of an audio block");
    auto lateWave = trigger; lateWave.frameOffset = 32u;
    engine->render(settings, &lateWave, 1u, output.pointers.data(), 32u, 64u);
    check(std::any_of(output.samples[0u].begin(), output.samples[0u].begin() + 32u, [](float v) { return std::abs(v) > 1.0e-7f; })
        && engine->activeVoiceCount() == 1u, "CHOP to Wavesets did not hand off at the event frame");
    engine->reset(); slot.sourceFormat = SampleNeonSourceFormat::Ambisonic;
    engine->setPreparedAsset(0u, asset.get()); engine->setPreparedWavesets(0u, map.get());
    settings.outputLayout = SampleNeonOutputLayout::Ambisonic3;
    engine->render(settings, &trigger, 1u, output.pointers.data(), 32u, 64u);
    check(engine->activeVoiceCount() == 0u && engine->outputPeak() == 0.0f,
        "Ambisonic Wavesets was not blocked in DSP");
    engine->reset(); slot.playback = SampleNeonPlayback::SliceSequence;
    slot.mangle = 0.0f; slot.character = SampleNeonMangleCharacter::Filter;
    slot.technique = {{1.0f, 0.0f, 0.0f, 1.0f}};
    engine->render(settings, &trigger, 1u, output.pointers.data(), 32u, 64u);
    check(engine->voiceCursorCount(0u) == 1u && engine->voiceCursors(0u)[0u].sourceStartNormalized == 0.0f,
        "Slice Sequence did not start at the first authored region");
    for (unsigned n = 0u; n < 40u; ++n) engine->render(settings, nullptr, 0u, output.pointers.data(), 32u, 64u);
    check(engine->voiceCursorCount(0u) == 1u && std::abs(engine->voiceCursors(0u)[0u].sourceStartNormalized - 0.25f) < 1.0e-5f,
        "Slice Sequence did not advance through CHOP markers");
    engine->reset(); slot.technique[1u] = 0.5f;
    engine->render(settings, &trigger, 1u, output.pointers.data(), 32u, 64u);
    check(engine->voiceCursorCount(0u) == 1u && std::abs(engine->voiceCursors(0u)[0u].sourceStartNormalized - 0.75f) < 1.0e-5f,
        "Slice Sequence reverse order did not start at the last region");
    engine->reset(); slot.technique[3u] = 0.0f;
    engine->render(settings, &trigger, 1u, output.pointers.data(), 32u, 64u);
    check(engine->activeVoiceCount() == 0u && engine->outputPeak() == 0.0f,
        "Slice Sequence zero probability still emitted a voice");
}

} // namespace

int main()
{
    testProtocolDecode();
    testPadVelocityPairing();
    testLedDiffs();
    testThirtyTwoSlotRouting();
    testPerformanceWindowsAndPressure();
    testV07V08PerformanceModel();
    testPlaybackTechniques();
    testTechniqueFades();
    testSpatialSources();
    testInspectorLayout();
    testSourceNormalization();
    testBoundarySelectionAndCapture();
    testWaveformViewport();
    testCharacterFxAndNewPlayback();
    if (failures != 0) {
        std::cerr << failures << " Sample Neon checks failed\n";
        return 1;
    }
    std::cout << "Sample Neon checks passed\n";
    return 0;
}
