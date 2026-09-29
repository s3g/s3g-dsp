#include "s3g_reloop_neon.h"
#include "s3g_sample_neon.h"
#include "s3g_sample_neon_poly.h"
#include "s3g_neon_note_routing.h"
#include "s3g_sample_neon_fill.h"
#include "s3g_sample_neon_edit.h"
#include "../plugins/clap_sample_neon/s3g_sample_neon_layout.h"
#include "../plugins/clap_sample_neon/s3g_sample_neon_labels.h"
#include "../plugins/clap_sample_neon/s3g_sample_neon_visual_state.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string_view>

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

void testDisplayVocabulary()
{
    using s3g::sample_neon_gui::Labels;
    using s3g::sample::NeonSourceMode;
    using s3g::sample::NeonStackShape;
    using View = std::string_view;
    check(View(Labels::stackPath) == "STACK PATH"
        && View(Labels::layerSources[static_cast<unsigned>(NeonSourceMode::Scan)]) == Labels::stackPath
        && View(Labels::lanesNavigation[1]) == Labels::stackPath
        && View(Labels::editViews[2]) == Labels::stackPath,
        "Layer source, Lanes navigation and edit view must use the same Stack Path name");
    check(View(Labels::layerSources[static_cast<unsigned>(NeonSourceMode::Selected)]) == Labels::editLayer
        && View(Labels::layerSources[0]) == "PRIMARY LAYER"
        && View(Labels::layerSources[2]) == "VELOCITY"
        && View(Labels::layerSources[3]) == "RANDOM / TRIGGER",
        "Display vocabulary must preserve the saved layer-source menu order");
    check(View(Labels::lanesNavigation[0]) == "MANUAL"
        && View(s3g::sample::kNeonStackShapeNames[static_cast<unsigned>(NeonStackShape::Manual)]) == "CUSTOM",
        "Manual layer control must remain distinct from a custom breakpoint shape");
    check(View(Labels::sourcePath) != Labels::stackPath
        && View(Labels::sourceCycle) != Labels::stackCycle,
        "Within-source movement must remain distinct from between-layer movement");
    // Existing 10 px monospaced labels: no row/column or font-size changes.
    for (const char* label : {Labels::stackPath, Labels::stackCycle, Labels::sourcePath,
            Labels::sourceCycle, Labels::editLayer, Labels::pitchSpray, Labels::reverseChance})
        check(View(label).size() * 6.2 <= s3g::sample_neon_gui::InspectorLayout::labelWidth,
            "Standardized label no longer fits the existing toolbox label width");
}

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
    check(first == kFullLedFrameMessages + kBankCount + (kBankCount - 1u)
            && messages[0u] == MidiMessage { 0x93u, 0x0du, 127u }
            && messages[1u] == MidiMessage { 0x94u, 0x0du, 127u }
            && messages[2u] == MidiMessage { 0x95u, 0x0du, 127u }
            && messages[3u] == MidiMessage { 0x96u, 0x0du, 127u }
            && messages[4u] == MidiMessage { 0x94u, 0x05u, 127u }
            && messages[7u] == MidiMessage { 0x93u, 0x05u, 127u }
            && messages[15u] == MidiMessage { 0x93u, 0x00u, 127u }
            && messages[16u] == MidiMessage { 0x97u, 0x00u, 96u }
            && messages[17u] == MidiMessage { 0x9bu, 0x20u, 127u }
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
    check(switched == kFullLedFrameMessages + kPadsPerBank + (kBankCount - 1u)
            && messages[0u] == MidiMessage { 0x97u, 0x00u, 0u }
            && messages[8u] == MidiMessage { 0x94u, 0x0au, 127u }
            && messages[11u] == MidiMessage { 0x93u, 0x0au, 127u }
            && messages[19u] == MidiMessage { 0x93u, 0x01u, 127u }
            && messages[20u] == MidiMessage { 0x97u, 0x68u, 96u },
        "mode/layer LED switch did not retarget the large pads");
    check(enableFourDecksSysEx()
            == std::array<uint8_t, 4u> {{ 0xf0u, 0x0au, 0x00u, 0xf7u }},
        "four-deck enable SysEx changed");
    frame = {}; frame.pads[0u].surface = 48u;
    encoder.invalidate(); encoder.encode(frame, messages.data(), messages.size());
    frame.bank = 1u;
    const auto bankSwitch = encoder.encode(frame, messages.data(), messages.size());
    for (std::size_t i = 0u; i < bankSwitch; ++i)
        check(!(messages[i].status >= 0x93u && messages[i].status <= 0x96u
            && messages[i].data1 == 0x0du),
            "Ordinary SAMPLE bank change reinitializes all hardware decks");
    check(bankSwitch == kFullLedFrameMessages - 1u
        && messages[7u] == MidiMessage {0x94u, 0u, 127u}
        && messages[8u] == MidiMessage {0x97u, 0u, 48u},
        "Sampler bank switch darkened its shared pad addresses");
    check(encoder.encode(frame, messages.data(), messages.size(), false, true) == 48u
        && messages[0u] == MidiMessage {0x97u, 0u, 48u},
        "Pad-only refresh did not restore unchanged inventory without bank commands");
    check(encoder.encode(frame, messages.data(), messages.size()) == 0u,
        "Pad refresh corrupted diff state");

    // Every deck can remember a different editing page. Entering SAMPLE
    // clears those hardware page recalls, THEN restores the intended bank.
    // Lamp-only updates/retries must not keep issuing mode/bank commands.
    for (uint8_t bank = 0u; bank < kBankCount; ++bank) {
        frame.bank = bank;
        frame.mode = Mode::HotCue; frame.layer = Layer::Second;
        encoder.encode(frame, messages.data(), messages.size());
        frame.mode = Mode::Sampler; frame.layer = Layer::First;
        const auto n = encoder.encode(frame, messages.data(), messages.size());
        unsigned restored = 0u;
        bool bankRestored = false;
        for (std::size_t i = 0u; i < n; ++i) {
            const auto& message = messages[i];
            if (message.status >= 0x93u && message.status <= 0x96u
                && message.data1 == 0x0du && message.data2 == 127u) {
                check(!bankRestored, "Sampler trigger came after restoring the selected bank");
                restored |= 1u << (message.status - 0x93u);
            }
            if (message == bankLedMessage(bank, Mode::Sampler)) {
                check(restored == 15u, "Not all remembered deck modes were reset before bank selection");
                bankRestored = true;
            }
        }
        check(restored == 15u && bankRestored && n <= kMaximumLedMessages,
            "SAMPLE transition did not restore every deck and the selected bank");
        check(encoder.encode(frame, messages.data(), messages.size()) == 0u,
            "Steady SAMPLE mode emitted a hardware heartbeat");
        const auto retry = encoder.encode(frame, messages.data(), messages.size(), false, true);
        check(retry == kPadsPerBank * kLedValuesPerPad,
            "Pad settling retry changed the hardware mode");
        const auto dark = encoder.encode(frame, messages.data(), messages.size(), true, false, false);
        check(dark == kPadsPerBank * kLedValuesPerPad,
            "Relinquishing LED ownership must only clear pads, not bank/mode commands");
        for (std::size_t i = 0u; i < dark; ++i)
            check(messages[i].status >= 0x97u, "Owner OFF must not select a hardware bank or mode");
        frame.mode = Mode::HotCue;
        const auto editing = encoder.encode(frame, messages.data(), messages.size());
        for (std::size_t i = 0u; i < editing; ++i)
            check(messages[i].data1 != 0x0du,
                "Explicit EDIT selection was forced back into SAMPLE");
    }
    frame.mode = Mode::Sampler; frame.layer = Layer::First; frame.bank = 1u;

    // Reproduce B/EDIT -> A/SAMPLE -> B/SAMPLE with stale lights in both
    // hardware address sets. Only the selected bank may remain illuminated.
    // This also covers SAMPLE FX, which uses deck (01), not bank (00), lamps.
    LedDiffEncoder bankEncoder;
    std::array<std::array<bool, 2u>, kBankCount> bankLamps;
    for (auto& lamp : bankLamps) lamp.fill(true);
    const auto checkBankLamps = [&](const LedFrame& desired, unsigned modeTriggers) {
        const auto n = bankEncoder.encode(desired, messages.data(), messages.size());
        unsigned triggers = 0u;
        MidiMessage lastContext {};
        for (std::size_t i = 0u; i < n; ++i) {
            const auto& message = messages[i];
            if (message.status < 0x93u || message.status > 0x96u) continue;
            lastContext = message;
            if (message.data1 < 2u)
                bankLamps[message.status - 0x93u][message.data1] = message.data2 != 0u;
            if (message.data1 == 0x0du) ++triggers;
        }
        const uint8_t selectedAddress = desired.mode == Mode::Sampler
            && desired.layer == Layer::First ? 0u : 1u;
        for (uint8_t bank = 0u; bank < kBankCount; ++bank)
            for (uint8_t address = 0u; address < 2u; ++address)
                check(bankLamps[bank][address] == (bank == desired.bank && address == selectedAddress),
                    "Bank feedback left a stale bank/deck lamp on or the selected bank dark");
        check(triggers == modeTriggers && lastContext == MidiMessage {
            static_cast<uint8_t>(0x93u + desired.bank), selectedAddress, 127u},
            "Mode commands overwrote the final bank selection");
        check(n <= kMaximumLedMessages && bankEncoder.encode(desired, messages.data(), messages.size()) == 0u,
            "Bank indicator feedback exceeded capacity or emitted a heartbeat");
    };
    LedFrame bankFrame; bankFrame.bank = 1u; bankFrame.mode = Mode::HotCue;
    checkBankLamps(bankFrame, 0u);
    bankFrame.bank = 0u; bankFrame.mode = Mode::Sampler;
    checkBankLamps(bankFrame, 4u);
    bankFrame.bank = 1u;
    checkBankLamps(bankFrame, 0u);
    for (uint8_t page = 0u; page < 8u; ++page) {
        bankFrame.mode = static_cast<Mode>(page % 4u);
        bankFrame.layer = page < 4u ? Layer::First : Layer::Second;
        const auto n = bankEncoder.encode(bankFrame, messages.data(), messages.size());
        // Consume this page transition in the lamp model before testing banks.
        for (std::size_t i = 0u; i < n; ++i) {
            const auto& m = messages[i];
            if (m.status >= 0x93u && m.status <= 0x96u && m.data1 < 2u)
                bankLamps[m.status - 0x93u][m.data1] = m.data2 != 0u;
        }
        for (uint8_t bank : std::array<uint8_t, 4u> {{0u, 2u, 3u, 1u}}) {
            bankFrame.bank = bank;
            checkBankLamps(bankFrame, 0u);
        }
    }

    // Every mode AND secondary tool layer follows across A-D. Unit encoders
    // are independent; ordinary bank moves and settling retries stay quiet.
    for (unsigned previous=0;previous<8;++previous) for (unsigned page=0;page<8;++page) {
        LedDiffEncoder unit, otherUnit;
        LedFrame desired; desired.mode=static_cast<Mode>(previous%4);
        desired.layer=previous<4 ? Layer::First : Layer::Second;
        unit.encode(desired,messages.data(),messages.size());
        otherUnit.encode(desired,messages.data(),messages.size());
        const auto otherFrame=desired;
        desired.mode=static_cast<Mode>(page%4); desired.layer=page<4 ? Layer::First : Layer::Second;
        desired.bank=3;
        const auto n=unit.encode(desired,messages.data(),messages.size());
        unsigned updated=0; bool bankRestored=false;
        for (std::size_t i=0;i<n;++i) {
            const auto m=messages[i];
            if (m.status>=0x93 && m.status<=0x96 && m.data1>=5 && m.data1<=12 && m.data2==127) {
                check(!bankRestored && m==modeLedMessage(m.status-0x93,desired.mode,desired.layer),
                    "All remembered deck pages receive the chosen mode/layer before bank restore");
                updated|=1u<<(m.status-0x93);
            }
            if (m==bankLedMessage(desired.bank,desired.mode,127,desired.layer)) bankRestored=true;
        }
        check(updated==(previous==page ? 0u : 15u) && bankRestored && n<=kMaximumLedMessages,
            "Every performance-page transition updates all four decks within bounded MIDI output");
        check(otherUnit.encode(otherFrame,messages.data(),messages.size())==0,
            "Changing one USB controller leaves the other unit context untouched");
        for (uint8_t bank=0;bank<4;++bank) {
            desired.bank=bank;
            const auto count=unit.encode(desired,messages.data(),messages.size());
            for (std::size_t i=0;i<count;++i) {
                const auto m=messages[i];
                check(!(m.status>=0x93 && m.status<=0x96 && m.data1>=5 && m.data1<=13),
                    "Bank move does not reinitialize any performance mode");
            }
            check(unit.encode(desired,messages.data(),messages.size(),false,true)==48,
                "All-mode settling retry only repaints pads");
            check(unit.encode(desired,messages.data(),messages.size())==0,"All modes settle without a MIDI heartbeat");
        }
    }

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

void testLedPacketPacing()
{
    using namespace s3g::controller::reloop_neon;
    std::array<MidiMessage, kMaximumLedMessages> messages {};
    // Virtual time: the regression never sleeps or opens a physical MIDI port.
    const auto verify = [&](std::size_t count, bool expectModes) {
        unsigned now = 0u, lastSend = 0u, modeCount = 0u, batches = 0u;
        bool previousMode = false;
        std::size_t delivered = 0u;
        const bool ok = sendLedFeedback(messages.data(), count,
            [&](const MidiMessage* batch, std::size_t size) {
                check(size > 0u && size <= kMaximumLedMessages,
                    "LED packet is empty or exceeds its bounded transport");
                const bool mode = isModeFeedback(batch[0]);
                if (mode) {
                    ++modeCount;
                    check(size == 1u, "Mode command was buried in a bulk LED packet");
                }
                if (mode || previousMode)
                    check(now - lastSend >= kModeFeedbackGapMs,
                        "Mode command lost its leading/trailing quiet interval");
                for (std::size_t i = 0u; i < size; ++i) {
                    check(isModeFeedback(batch[i]) == mode,
                        "Pad/bank packet contains a stateful mode command");
                    check(delivered < count && batch[i] == messages[delivered],
                        "Pacing reordered, duplicated or changed a MIDI message");
                    ++delivered;
                }
                ++batches; lastSend = now; previousMode = mode;
                return true;
            }, [&](unsigned milliseconds) {
                check(milliseconds == 20u, "Mode gap differs from the hardware-tested 20 ms");
                now += milliseconds;
                return true;
            });
        check(ok && delivered == count && (modeCount != 0u) == expectModes,
            "Paced LED transport did not preserve the complete frame");
        if (previousMode)
            check(now - lastSend >= kModeFeedbackGapMs,
                "Terminal mode command was not protected from the next frame");
        if (!expectModes)
            check(now == 0u && batches == (count ? 1u : 0u),
                "Ordinary bank/pad updates acquired unnecessary waits or fragmentation");
    };

    for (unsigned previous = 0u; previous < 8u; ++previous) {
        for (unsigned page = 0u; page < 8u; ++page) {
            LedDiffEncoder encoder;
            LedFrame frame;
            frame.mode = static_cast<Mode>(previous % 4u);
            frame.layer = previous < 4u ? Layer::First : Layer::Second;
            verify(encoder.encode(frame, messages.data(), messages.size()), true);
            frame.bank = 3u;
            frame.mode = static_cast<Mode>(page % 4u);
            frame.layer = page < 4u ? Layer::First : Layer::Second;
            verify(encoder.encode(frame, messages.data(), messages.size()), previous != page);
            for (uint8_t bank = 0u; bank < kBankCount; ++bank) {
                frame.bank = bank;
                verify(encoder.encode(frame, messages.data(), messages.size()), false);
                verify(encoder.encode(frame, messages.data(), messages.size(), false, true), false);
                verify(encoder.encode(frame, messages.data(), messages.size()), false);
            }
            for (auto& pad : frame.pads) pad = {};
            const auto dark = encoder.encode(frame, messages.data(), messages.size(), true, false, false);
            verify(dark, false);
            for (std::size_t i = 0u; i < dark; ++i)
                check(messages[i].status >= 0x97u, "Owner OFF emitted a bank or mode write");
            verify(encoder.encode(frame, messages.data(), messages.size(), true), true);
        }
    }

    // Worst-case separation: terminal modes, ordinary batches on either side,
    // and maximum-size packets preserve the same timing/order invariants.
    messages.fill(modeLedMessage(0u, Mode::HotCue, Layer::First));
    verify(messages.size(), true);
    verify(1u, true);
    messages.fill(padLedMessage(0u, 0u, 127u));
    verify(messages.size(), false);
    messages[1u] = modeLedMessage(1u, Mode::Slicer, Layer::Second);
    messages[3u] = samplerModeTrigger(2u);
    verify(5u, true);

    // Every transport/wait failure stops immediately, rather than sending the
    // remainder and committing a misleading successful frame to the cache.
    unsigned operations = 0u;
    const auto run = [&](unsigned failAt) {
        operations = 0u;
        return sendLedFeedback(messages.data(), 5u,
            [&](const MidiMessage*, std::size_t) { return operations++ != failAt; },
            [&](unsigned) { return operations++ != failAt; });
    };
    check(run(100u), "Fake LED transport unexpectedly failed");
    const auto totalOperations = operations;
    for (unsigned failAt = 0u; failAt < totalOperations; ++failAt)
        check(!run(failAt) && operations == failAt + 1u,
            "LED transport continued after a failed send/wait");

    unsigned calls = 0u;
    const auto send = [&](const MidiMessage*, std::size_t) { ++calls; return true; };
    const auto pause = [&](unsigned) { ++calls; return true; };
    check(sendLedFeedback(nullptr, 0u, send, pause) && calls == 0u,
        "Empty LED frame must be a successful no-op");
    check(!sendLedFeedback(nullptr, 1u, send, pause) && calls == 0u,
        "Null nonempty LED frame reached the transport");
    check(!sendLedFeedback(messages.data(), messages.size() + 1u, send, pause) && calls == 0u,
        "Oversized LED frame reached the transport");
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
    settings.slots[0u].character = SampleNeonMangleCharacter::Punch;
    settings.slots[17u].gainDecibels = 0.0f;
    settings.slots[17u].outputBus = 15u;
    settings.slots[17u].character = SampleNeonMangleCharacter::Punch;
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
    settings.slots[0u].character = SampleNeonMangleCharacter::Punch;
    // These assertions inspect the first raw source sample, not its new fade.
    settings.slots[0u].family[NeonFamily::SliceAttack] = 0;
    settings.slots[0u].family[NeonFamily::SliceRelease] = 0;
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
    settings.slots[0u].character = SampleNeonMangleCharacter::Punch;
    settings.slots[0u].family[NeonFamily::SliceAttack] = 0;
    settings.slots[0u].family[NeonFamily::SliceRelease] = 0;
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
    settings.slots[1u].character = SampleNeonMangleCharacter::Punch;
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
    control.character = SampleNeonMangleCharacter::Punch;
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
    settings.slots[0u].character = SampleNeonMangleCharacter::Punch; // No filter in this pan fixture.
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
    using C = s3g::sample_neon_gui::CanvasLayout;
    using W = s3g::sample_neon_gui::WaveLayout;
    using E = s3g::sample_neon_gui::PlaybackLayout;
    using F = s3g::sample_neon_gui::FamilyGrid;
    using M = s3g::sample_neon_gui::MiniLayout;
    check(C::width * 9u == C::height * 16u, "Neon canvas must retain a 16:9 aspect ratio");
    check(W::height() == 510 && W::top + W::height() + 14 == W::overviewTop()
        && W::pathTop == W::overviewTop() && W::bottom() == I::row(F::cutoff(false)) + 18,
        "Main waveform, overview and path must use fixed, aligned geometry");
    check(E::row(5) == I::row(2) && E::controlWidth == I::controlWidth
        && E::trackWidth == I::trackWidth && C::statusTop + 30 == C::height,
        "Playback must reclaim duplicate FX rows without shrinking its controls");
    for (unsigned sound = 0; sound < 3; ++sound) {
        check(F::row(10, true, sound) == E::row(20) + I::rowPitch
            && I::row(F::cutoff(true, sound)) + 26 <= C::statusTop,
            "Motion details must follow the engine controls and fit above the footer");
        check(F::row(17, true, sound) == F::row(10, true, sound) + F::articulationCount(sound) * I::rowPitch
            && I::row(F::cutoff(true, sound)) == F::row(19, true, sound) + I::rowPitch,
            "Motion articulation and event rows must retain standard spacing");
    }
    check(F::row(10) == E::row(21) + I::rowPitch
        && F::row(17) == F::row(15) + I::rowPitch
        && I::row(F::cutoff(false)) == F::row(19) + I::rowPitch
        && I::row(F::cutoff(false)) + 26 <= C::statusTop,
        "Grains details must fit in the inspector at standard row spacing");
    check(M::padY(7) + M::padHeight + 22 == M::actions
        && M::capture + 18 == M::bottom,
        "Miniature NEON pads and actions must fit the fixed workspace height");
    for (unsigned row = 0u; row <= 20u; ++row) {
        check(I::slider(row) + 7.0 == I::row(row)
            && I::menu(row) + 12.5 == I::row(row)
            && I::button(row) + I::buttonHeight * 0.5 == I::row(row),
            "Mixed right-panel controls do not share a row center");
        check(I::row(row + 1u) - I::row(row) == 24.0
            && I::row(row) + 11.5 < I::row(row + 1u) - 11.5,
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
    const auto quiet = constantAsset(.1f, -.2f), loud = constantAsset(.4f, -.8f), silent = constantAsset(0, 0);
    std::array<const SampleAsset*, 32> layers {};
    layers[0] = &quiet; layers[1] = &loud; layers[2] = &silent; layers[31] = &source;
    const auto each = sampleNeonStackNormalizeGains(layers, false);
    const auto linked = sampleNeonStackNormalizeGains(layers, true);
    check(std::abs(each[0] / each[1] - 4) < 1e-6 && each[2] == 0 && each[3] == 0 && each[31] == 0,
        "Stack normalization did not skip silence/missing/invalid layers");
    check(linked[0] == linked[1] && linked[1] == each[1] && linked[2] == 0,
        "Balanced normalization did not choose a common gain from the loudest layer");
    const auto balancedQuiet = sampleNeonNormalize(quiet, linked[0]);
    const auto balancedLoud = sampleNeonNormalize(loud, linked[1]);
    check(std::abs(balancedLoud.channels[1][0] / balancedQuiet.channels[1][0] - 4) < 1e-6
        && std::abs(balancedLoud.channels[1][0] + kSampleNeonNormalizePeak) < 1e-6,
        "Balanced normalization changed inter-layer level or missed the peak target");
    layers.fill(&quiet); layers.back() = &loud;
    const auto fullStack = sampleNeonStackNormalizeGains(layers, true);
    check(std::all_of(fullStack.begin(), fullStack.end(), [&](double value) { return value == each[1]; }),
        "32-layer normalization missed the final layer's peak");
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
    // Include the new 5 ms overlap before expecting only the incoming cursor.
    for (unsigned n = 0u; n < 42u; ++n) engine->render(settings, nullptr, 0u, output.pointers.data(), 32u, 64u);
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

void testExpandedCharacterModes() {
    using namespace s3g::sample;
    SampleNeonFx fx;
    check(fx.prepare(48000),"Expanded FX prepare");
    OutputBlock block;
    // Every algorithm and named model must be deterministic after reset,
    // remain finite at extremes, and retain channel ratios in field mode.
    for(unsigned effect=0;effect<8;++effect) {
        const auto type=static_cast<SampleNeonMangleCharacter>(effect);
        const unsigned models=*kNeonCharacterDefs[effect][0].choices?static_cast<unsigned>(kNeonCharacterDefs[effect][0].maximum)+1:1;
        for(unsigned mode=0;mode<models;++mode) {
            auto values=neonCharacterDefaults(effect);
            if(models>1) values[0]=static_cast<float>(mode)/float(models-1);
            if(effect==0) values[3]=.85f;
            if(effect==1) {values[1]=.25f;values[2]=.7f;values[4]=.5f;values[5]=.5f;}
            if(effect==2) {values[1]=.75f;values[2]=.8f;}
            if(effect==4) values[3]=.9f;
            if(effect==7) {values[2]=.8f;values[3]=.6f;values[5]=1;}
            std::array<double,2> fingerprints {};
            for(unsigned pass=0;pass<2;++pass) {
                fx.reset();
                for(unsigned cycle=0;cycle<140;++cycle) {
                    for(unsigned frame=0;frame<64;++frame) {
                        const float input=cycle<100?static_cast<float>(.13*std::sin((cycle*64+frame)*.071)):0;
                        for(unsigned ch=0;ch<16;++ch) block.samples[ch][frame]=input*(float(ch+1)/16);
                    }
                    fx.process(block.pointers.data(),16,64,type,values,1,effect<6,120);
                    for(unsigned frame=0;frame<64;++frame) {
                        fingerprints[pass]+=block.samples[15][frame]*(1+frame*.001);
                        for(unsigned ch=0;ch<16;++ch) {
                            check(std::isfinite(block.samples[ch][frame])&&std::abs(block.samples[ch][frame])<20,"Character model escaped finite bounded output");
                            if(effect<6) check(std::abs(block.samples[ch][frame]-block.samples[15][frame]*(float(ch+1)/16))<1e-5,"Character model altered encoded channel ratios");
                        }
                    }
                }
            }
            check(std::abs(fingerprints[0]-fingerprints[1])<1e-6,"Character reset/retrigger was nondeterministic");
        }
    }
    auto filter=neonCharacterDefaults(0);filter[0]=1;filter[1]=.5f;
    fx.reset();
    for(unsigned n=0;n<64;++n)for(unsigned ch=0;ch<16;++ch)block.samples[ch][n]=.123f;
    fx.process(block.pointers.data(),16,64,SampleNeonMangleCharacter::Filter,filter,1,true);
    check(std::all_of(block.samples[0].begin(),block.samples[0].end(),[](float x){return x==.123f;}),"DJ midpoint is not neutral");
    auto echo=neonCharacterDefaults(1);echo[7]=1;echo[8]=5.f/8;
    check(neonCharacterEchoSeconds(echo,120)==.5 && neonCharacterEchoSeconds(echo,240)==.25,"Beat echo did not follow tempo");
    check(neonCharacterKnob(1,0,true,true)==8 && neonCharacterKnob(1,0,true,false)==1,
        "Echo time encoder must edit the division in Beats mode");
    echo[8]=1;
    check(neonCharacterEchoSeconds(echo,20)==2,"Echo memory limit differs from displayed time cap");
    check(!neonCharacterControlAllowed(1,0,1,true)&&neonCharacterControlAllowed(1,0,1,false),"Tape field restriction missing");
    // Space must remain continuous through its main-buffer wrap (2.1 s),
    // then reset without leaking any old tank or predelay sample.
    auto space=neonCharacterDefaults(2);space[2]=1;fx.reset();
    for(unsigned cycle=0;cycle<1700;++cycle) {
        for(auto& channel:block.samples)channel.fill(0);
        if(cycle==0)block.samples[0][0]=.5f;
        fx.process(block.pointers.data(),1,64,SampleNeonMangleCharacter::Space,space,1,false);
        for(float x:block.samples[0])check(std::isfinite(x)&&std::abs(x)<1,"Space tank unstable through ring wrap");
    }
    fx.reset();for(auto& channel:block.samples)channel.fill(0);
    fx.process(block.pointers.data(),1,64,SampleNeonMangleCharacter::Space,space,1,false);
    check(std::all_of(block.samples[0].begin(),block.samples[0].end(),[](float x){return x==0;}),"Space reset leaked a tank tail");
}

void benchmarkCharacterFx() {
    if (!std::getenv("S3G_NEON_FX_BENCHMARK")) return;
    using namespace s3g::sample;
    SampleNeonFx fx;
    check(fx.prepare(48000),"FX benchmark prepare");
    OutputBlock block, source;
    for(unsigned ch=0;ch<16;++ch) for(unsigned n=0;n<64;++n)
        source.samples[ch][n]=static_cast<float>(.2*std::sin(n*.071)*(ch+1)/16);
    constexpr unsigned blocks=1500;
    for(unsigned effect=0;effect<8;++effect) {
        fx.reset();
        const auto values=neonCharacterDefaults(effect);
        const auto start=std::chrono::steady_clock::now();
        for(unsigned n=0;n<blocks;++n) {
            block.samples=source.samples;
            fx.process(block.pointers.data(),16,64,static_cast<SampleNeonMangleCharacter>(effect),values,.8f,false);
        }
        const auto elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
        std::cout << "FX benchmark, one 16-channel pad, " << kNeonCharacterNames[effect]
            << ": " << elapsed/(blocks*64.0/48000)*100 << "% of one realtime core\n";
    }
}

void testStackPlayback() {
    using namespace s3g::sample;
    auto a = constantAsset(0.2f, 0.4f, 48000u);
    auto b = constantAsset(-0.2f, -0.4f, 24000u);
    NeonStack stack; stack.count = 2u;
    stack.layers[0] = {&a, 0.0, 1.0}; stack.layers[1] = {&b, 0.0, 1.0};
    check(neonVelocityLayer(0.001f, 32u) == 0u && neonVelocityLayer(1.0f, 32u) == 31u,
        "velocity layers must cover quietest through maximum strikes");
    const auto center = neonStackBlend(0.5, 32u);
    check(center.first == 15u && center.second == 16u && center.mix == 0.5f,
        "stack interpolation must address all 32 layers");
    auto engine = std::make_unique<SampleNeonEngine>();
    check(engine->prepare(48000.0, 64u) && engine->setAsset(0u, &a), "stack engine prepare");
    SampleNeonSettings settings; settings.masterGainDecibels = 0.0f;
    auto& s = settings.slots[0]; s.stack = &stack; s.gainDecibels = 0.0f; s.pressureDepth = 0.0f;
    s.releaseProportion = 0.0f; s.sourceMode = NeonSourceMode::Velocity;
    SampleNeonEvent note; note.slot = 0u; note.noteId = 101u; note.value = 1.0f;
    OutputBlock output;
    engine->render(settings, &note, 1u, output.pointers.data(), 32u, 64u);
    check(output.samples[0][32] < -0.1f, "maximum velocity must choose upper layer");
    s.sourceMode = NeonSourceMode::Selected; s.selectedLayer = 1u;
    engine->reset(); engine->render(settings, &note, 1u, output.pointers.data(), 32u, 64u);
    check(output.samples[0][32] < -0.1f, "selected layer must override primary");
    s.sourceMode = NeonSourceMode::Primary;
    engine->reset(); engine->render(settings, &note, 1u, output.pointers.data(), 32u, 64u);
    check(output.samples[0][32] > 0.1f, "primary must remain layer one while editing another layer");
    note.selectedSource = true;
    engine->reset(); engine->render(settings, &note, 1u, output.pointers.data(), 32u, 64u);
    check(output.samples[0][32] < -0.1f, "explicit layer audition must play edited layer without changing source mode");
    note.selectedSource = false;
    s.sourceMode = NeonSourceMode::Random;
    bool positive = false, negative = false;
    for (unsigned n = 0u; n < 64u; ++n) {
        engine->killAll(); note.noteId++;
        engine->render(settings, &note, 1u, output.pointers.data(), 32u, 64u);
        positive |= output.samples[0][32] > 0.05f; negative |= output.samples[0][32] < -0.05f;
    }
    check(positive && negative, "random trigger selection must reach both layers");
    for (auto playback : {SampleNeonPlayback::Motion, SampleNeonPlayback::Grains, SampleNeonPlayback::Stretch}) {
        engine->reset(); s.playback = playback; s.sourceMode = NeonSourceMode::Scan;
        s.stackCycleSeconds = 0.5f;
        s.clock = SampleNeonClock::Free; settings.transportPlaying = false;
        s.triggerMode = TriggerMode::Gate; s.grainDensityHz = 80.0f; s.grainSizeMs = 40.0f;
        s.grainSpray = 0.0f; s.techniqueAttackSeconds = 0.001f; s.techniqueReleaseSeconds = 0.01f;
        positive = negative = false;
        for (unsigned block = 0u; block < 500u; ++block) {
            engine->render(settings, block == 0u ? &note : nullptr, block == 0u ? 1u : 0u, output.pointers.data(), 32u, 64u);
            for (unsigned frame = 0u; frame < 64u; ++frame) {
                const float left = output.samples[0][frame], right = output.samples[1][frame];
                check(std::isfinite(left) && std::abs(right - 2.0f * left) < 1.0e-5f, "scan must retain channel-linked field");
                positive |= left > 0.02f; negative |= left < -0.02f;
            }
        }
        check(positive && negative && engine->stackScanActive(0u, settings)
            && engine->stackWaveformLayer(0u, settings) == -2, "held FREE Motion/Grains/Stretch must traverse layers with transport stopped");
        engine->reset(); note.selectedSource = true;
        positive = negative = false;
        for (unsigned block = 0u; block < 500u; ++block) {
            engine->render(settings, block ? nullptr : &note, block ? 0u : 1u, output.pointers.data(), 32u, 64u);
            for (float v : output.samples[0]) { positive |= v > 0.02f; negative |= v < -0.02f; }
        }
        check(!positive && negative && !engine->stackScanActive(0u, settings)
            && engine->stackWaveformLayer(0u, settings) == -1, "explicit audition must isolate selected layer rather than scan");
        note.selectedSource = false;
        auto release = note; release.kind = SampleNeonEventKind::Release;
        for (unsigned n = 0; n < 30u; ++n)
            engine->render(settings, n ? nullptr : &release, n ? 0u : 1u, output.pointers.data(), 32u, 64u);
        check(!engine->slotPlaybackActive(0u), "stack HOLD release must stop all layer grains");
        s.clock = SampleNeonClock::Host; engine->reset();
        engine->render(settings, &note, 1u, output.pointers.data(), 32u, 64u);
        check(output.samples[0][32] == 0.0f, "HOST stack must pause with stopped transport");
    }
    engine->reset(); s.playback = SampleNeonPlayback::SliceSequence; s.clock = SampleNeonClock::Free;
    s.sourceMode = NeonSourceMode::Selected; s.selectedLayer = 1u;
    s.sliceCount = 3u; s.sliceLayout = equalSampleNeonSliceLayout(3u);
    stack.primarySliceCount = 2u; stack.primarySlices = {}; stack.primarySlices[1] = 0.25; stack.primarySlices[2] = 1.0;
    s.technique = {{1.0f, 0.0f, 0.0f, 1.0f}};
    for (unsigned n = 0u; n < 43u; ++n)
        engine->render(settings, n ? nullptr : &note, n ? 0u : 1u, output.pointers.data(), 32u, 64u);
    check(engine->voiceCursorCount(0u) == 1u && engine->voiceCursors(0u)[0].sourceAsset == &a
        && engine->stackWaveformLayer(0u, settings) == 0
        && std::abs(engine->voiceCursors(0u)[0].sourceStartNormalized - 0.25f) < 1.0e-5f,
        "Slice Sequence must use the primary source and its own markers while another layer is edited");
}

void testStackWaveform() {
    using namespace s3g::sample;
    auto a = constantAsset(0.2f, 0.4f, 48000u);
    auto b = constantAsset(-0.2f, -0.4f, 256u);
    NeonStack stack; stack.count = 2u;
    stack.layers[0] = {&a, 0.0, 1.0}; stack.layers[1] = {&b, 0.0, 1.0};
    auto engine = std::make_unique<SampleNeonEngine>();
    check(engine->prepare(48000.0, 64u) && engine->setAsset(0u, &a), "waveform engine prepare");
    SampleNeonSettings settings;
    auto& s = settings.slots[0]; s.stack = &stack; s.selectedLayer = 1u;
    s.clock = SampleNeonClock::Free; s.triggerMode = TriggerMode::Gate;
    s.grainSpray = 0.0f; s.grainSizeMs = 40.0f; s.grainDensityHz = 80.0f;
    SampleNeonEvent note; note.slot = 0u; note.noteId = 9u;
    OutputBlock output;
    const auto render = [&](bool trigger) {
        engine->render(settings, trigger ? &note : nullptr, trigger ? 1u : 0u, output.pointers.data(), 32u, 64u);
    };
    for (auto playback : {SampleNeonPlayback::Sample, SampleNeonPlayback::Motion,
             SampleNeonPlayback::Grains, SampleNeonPlayback::Stretch}) {
        s.playback = playback; engine->reset();
        for (auto mode : {NeonSourceMode::Primary, NeonSourceMode::Selected, NeonSourceMode::Velocity, NeonSourceMode::Random}) {
            s.sourceMode = mode;
            bool seen[2] {};
            for (unsigned n = 0u; n < 32u; ++n) {
                note.noteId++; note.value = n % 2u ? 1.0f : 0.1f;
                render(true);
                const int layer = engine->stackWaveformLayer(0u, settings);
                const int expected = mode == NeonSourceMode::Primary ? 0 : mode == NeonSourceMode::Selected ? 1
                    : mode == NeonSourceMode::Velocity ? static_cast<int>(n % 2u) : layer;
                check(layer >= 0 && layer < 2 && layer == expected, "discrete waveform must follow actual trigger selection in every technique");
                if (layer >= 0 && layer < 2) seen[layer] = true;
                const auto* asset = layer ? &b : &a;
                for (unsigned c = 0u; c < engine->voiceCursorCount(0u); ++c)
                    check(engine->voiceCursors(0u)[c].sourceAsset == asset, "waveform layer must agree with actual audio source");
                check(s.selectedLayer == 1u, "following cannot move edit selection");
            }
            if (mode == NeonSourceMode::Random || mode == NeonSourceMode::Velocity)
                check(seen[0] && seen[1], "discrete waveform must reach both layers");
            note.selectedSource = true; render(true);
            check(engine->stackWaveformLayer(0u, settings) == -1, "isolated audition keeps edit-layer waveform");
            note.selectedSource = false;
        }
        engine->killAll();
        check(engine->stackWaveformLayer(0u, settings) == -1, "stop clears waveform follow");
    }
    // Overlapping one-shots: the short, latest hit ends before the primary.
    // Keep tracking that older voice across many short hits and arbitrary IDs.
    engine->reset(); s.playback = SampleNeonPlayback::Sample; s.sourceMode = NeonSourceMode::Velocity;
    s.triggerMode = TriggerMode::OneShot; s.retriggerMode = RetriggerMode::Layer;
    note.noteId = 900u; note.value = 0.1f; render(true);
    for (unsigned n = 0u; n < 64u; ++n) {
        note.noteId = 800u - n; note.value = 1.0f; render(true);
        check(engine->stackWaveformLayer(0u, settings) == 1, "latest overlapping voice is shown independently of note-ID ordering");
        for (unsigned block = 0u; block < 5u; ++block) render(false);
        check(engine->stackWaveformLayer(0u, settings) == 0, "ended hit must fall back to surviving layer");
    }
    note.mode = s3g::controller::reloop_neon::Mode::Slicer;
    s.sliceCount = 1u; s.sliceLayout = equalSampleNeonSliceLayout(1u);
    render(true);
    check(engine->stackWaveformLayer(0u, settings) == -1, "slice audition cannot redirect edit waveform");
    engine->reset();
    check(engine->stackWaveformLayer(0u, settings) == -1, "reset clears waveform tracking");
    // Layer identity must not be guessed from asset pointers: layers can
    // reference the same source, and all 32 velocity bands are addressable.
    stack.count = 32u;
    for (unsigned layer = 0u; layer < 32u; ++layer) stack.layers[layer] = {layer % 2u ? &b : &a, 0.0, 1.0};
    s.retriggerMode = RetriggerMode::Restart; note.mode = s3g::controller::reloop_neon::Mode::Sampler;
    for (unsigned layer = 0u; layer < 32u; ++layer) {
        ++note.noteId; note.value = (static_cast<float>(layer) + 0.5f) / 32.0f; render(true);
        check(engine->stackWaveformLayer(0u, settings) == static_cast<int>(layer), "waveform follows every velocity band even when layers share PCM");
    }
}

void testWavesetStackScan() {
    using namespace s3g::sample;
    auto a = std::make_shared<SampleAsset>(constantAsset(0.0f, 0.0f, 12000u));
    auto b = std::make_shared<SampleAsset>(*a);
    for (unsigned n = 0u; n < a->frameCount(); ++n) {
        a->channels[0][n] = 0.15f * std::sin(static_cast<float>(n) * 0.08f);
        b->channels[0][n] = 0.40f * std::sin(static_cast<float>(n) * 0.13f);
        a->channels[1][n] = a->channels[0][n] * 2.0f;
        b->channels[1][n] = b->channels[0][n] * 2.0f;
    }
    const auto mapA = analyzeWavesets(a), mapB = analyzeWavesets(b);
    check(mapA && mapB, "waveset stack analysis");
    NeonStack stack; stack.count = 2u;
    stack.layers[0] = {a.get(), 0.0, 1.0, mapA.get()}; stack.layers[1] = {b.get(), 0.0, 1.0, mapB.get()};
    auto engine = std::make_unique<SampleNeonEngine>();
    check(engine->prepare(48000.0, 64u) && engine->setAsset(0u, a.get()), "waveset scan engine prepare");
    SampleNeonSettings settings; settings.masterGainDecibels = 0.0f;
    auto& s = settings.slots[0]; s.stack = &stack; s.sourceMode = NeonSourceMode::Scan;
    s.playback = SampleNeonPlayback::Wavesets; s.triggerMode = TriggerMode::Gate; s.clock = SampleNeonClock::Free;
    s.stackCycleSeconds = 0.5f;
    s.gainDecibels = 0.0f; s.pressureDepth = 0.0f; s.technique = {};
    s.techniqueAttackSeconds = 0.001f; s.techniqueReleaseSeconds = 0.01f;
    SampleNeonEvent note; note.slot = 0u; note.noteId = 312u;
    OutputBlock output; bool heard = false, sawA = false, sawB = false;
    for (unsigned n = 0u; n < 450u; ++n) {
        engine->render(settings, n ? nullptr : &note, n ? 0u : 1u, output.pointers.data(), 32u, 64u);
        for (unsigned f = 0u; f < 64u; ++f) {
            const auto x = output.samples[0][f]; heard |= std::abs(x) > 0.02f;
            check(std::isfinite(x) && std::abs(output.samples[1][f] - x * 2.0f) < 0.0001f,
                "Wavesets stack must preserve the discrete channel correspondence");
        }
        for (unsigned c = 0u; c < engine->voiceCursorCount(0u); ++c) {
            sawA |= engine->voiceCursors(0u)[c].sourceAsset == a.get();
            sawB |= engine->voiceCursors(0u)[c].sourceAsset == b.get();
        }
    }
    check(heard && sawA && sawB && engine->stackScanActive(0u, settings)
        && engine->stackWaveformLayer(0u, settings) == -2, "held Wavesets must crossfade both analyzed sources");
    s.selectedLayer = 1; note.selectedSource = true;
    engine->render(settings, &note, 1u, output.pointers.data(), 32u, 64u);
    SampleNeonEvent position; position.kind = SampleNeonEventKind::StackPosition; position.value = 1;
    engine->render(settings, &position, 1u, output.pointers.data(), 32u, 64u);
    for (unsigned n = 0; n < 100; ++n) engine->render(settings, nullptr, 0u, output.pointers.data(), 32u, 64u);
    check(engine->stackPosition(0) > .999f, "Wavesets manual override reaches last layer");
    position.value = -1; position.resumeNavigation = true;
    engine->render(settings, &position, 1u, output.pointers.data(), 32u, 64u);
    bool low = false, high = false;
    for (unsigned n = 0; n < 450; ++n) {
        engine->render(settings, nullptr, 0u, output.pointers.data(), 32u, 64u);
        low |= engine->stackPosition(0) < .2f; high |= engine->stackPosition(0) > .8f;
    }
    check(low && high && engine->stackWaveformLayer(0,settings) == -2 && s.selectedLayer == 1,
        "Wavesets scan resumes after manual override and edit audition without another trigger");
    note.selectedSource = false;
    s.clock = SampleNeonClock::Host; settings.transportPlaying = false;
    engine->render(settings, nullptr, 0u, output.pointers.data(), 32u, 64u);
    check(engine->outputPeak() == 0.0f, "HOST Wavesets scan must pause with stopped transport");
    auto off = note; off.kind = SampleNeonEventKind::Release;
    for (unsigned n = 0u; n < 30u; ++n)
        engine->render(settings, n ? nullptr : &off, n ? 0u : 1u, output.pointers.data(), 32u, 64u);
    check(!engine->slotPlaybackActive(0u), "Wavesets HOLD release must finish even with host stopped");
    auto quad = std::make_shared<SampleAsset>(*a); quad->channelCount = 4u;
    quad->channels[2] = quad->channels[0]; quad->channels[3] = quad->channels[1];
    const auto quadMap = analyzeWavesets(quad);
    stack.count = 1u; stack.layers[0] = {quad.get(), 0.0, 1.0, quadMap.get()};
    settings.outputLayout = SampleNeonOutputLayout::Ambisonic1;
    s.clock = SampleNeonClock::Free; s.sourceFormat = SampleNeonSourceFormat::Ambisonic;
    check(engine->setAsset(0u, quad.get()), "valid 1OA route for Wavesets restriction test");
    engine->reset(); engine->render(settings, &note, 1u, output.pointers.data(), 32u, 64u);
    check(engine->outputPeak() == 0.0f, "Wavesets stack must not bypass the ACN/SN3D restriction");
}

void testFamilyIntegration()
{
    using namespace s3g::sample;
    using F = NeonFamily;
    NeonFamilySettings path;
    check(path.valid(), "family defaults valid");
    path[F::PathCount] = 3;
    const auto t = neonFamilyIndex(F::PathTime), v = neonFamilyIndex(F::PathValue);
    path.values[t] = 0; path.values[t + 1] = .25f; path.values[t + 2] = 1;
    path.values[v] = 0; path.values[v + 1] = 1; path.values[v + 2] = 0;
    check(path.valid() && std::abs(neonStackPath(path, .125) - .5) < 1e-6
        && std::abs(neonStackPath(path, .625) - .5) < 1e-6, "breakpoint times and layer positions");
    path[F::StackCurve] = 1;
    check(neonStackPath(path, .125) < .1, "curved stack interpolation");
    path.values[t + 1] = 1;
    check(!path.valid(), "duplicate breakpoint times rejected");
    for (unsigned shape = 0; shape < 5; ++shape) {
        check(grainWindow(static_cast<GrainEnvelope>(shape), .5f, 0) > .99f, "grain envelope peak");
        check(grainWindow(static_cast<GrainEnvelope>(shape), .1f, .8f)
            < grainWindow(static_cast<GrainEnvelope>(shape), .1f, -.8f), "grain envelope skew");
    }
    auto source = constantAsset(0, 0, 48000);
    source.channelCount = 16;
    for (unsigned ch = 0; ch < 16; ++ch) {
        source.channels[ch].resize(48000);
        for (unsigned i = 0; i < 48000; ++i)
            source.channels[ch][i] = float(ch + 1) * .02f * float(.2 + .8 * i / 48000.0)
                * std::sin(float(i) * float(.02 + .0000009 * i));
    }
    auto other = source;
    for (auto& channel : other.channels) for (auto& sample : channel) sample *= -.5f;
    NeonStack stack; stack.count = 2;
    stack.layers[0] = {&source, 0, 1}; stack.layers[1] = {&other, .1, .9};
    SampleNeonSettings settings; settings.masterGainDecibels = 0;
    settings.outputLayout = SampleNeonOutputLayout::Ambisonic3;
    auto& slot = settings.slots[0]; slot.sourceFormat = SampleNeonSourceFormat::Ambisonic;
    slot.stack = &stack; slot.gainDecibels = 0; slot.triggerMode = TriggerMode::Gate;
    slot.grainDensityHz = 60; slot.grainSizeMs = 50; slot.launchPosition = .25;
    slot.motionCycleSeconds = .25; slot.stackCycleSeconds = .25;
    auto engine = std::make_unique<SampleNeonEngine>();
    check(engine->prepare(48000, 64) && engine->setAsset(0, &source), "family engine prepare");
    SampleNeonEvent note; note.noteId = 91; note.value = 1;
    const auto render = [&] {
        engine->reset(); OutputBlock output; std::vector<float> result;
        for (unsigned block = 0; block < 400; ++block) {
            engine->render(settings, block ? nullptr : &note, block ? 0 : 1, output.pointers.data(), 32, 64);
            for (unsigned frame = 0; frame < 64; ++frame) {
                const float x = output.samples[0][frame]; result.push_back(x);
                for (unsigned ch = 1; ch < 16; ++ch)
                    check(std::isfinite(output.samples[ch][frame]) && std::abs(output.samples[ch][frame] - (ch + 1) * x) < .0001,
                        "family processing must preserve all 16 ACN channel ratios");
            }
        }
        auto off = note; off.kind = SampleNeonEventKind::Release;
        for (unsigned block = 0; block < 10; ++block)
            engine->render(settings, block ? nullptr : &off, block ? 0 : 1, output.pointers.data(), 32, 64);
        check(!engine->slotPlaybackActive(0), "family HOLD releases cleanly");
        return result;
    };
    unsigned cases = 0;
    for (auto playback : {SampleNeonPlayback::Motion, SampleNeonPlayback::Grains}) {
        slot.playback = playback; slot.family = {}; slot.sourceMode = NeonSourceMode::Primary;
        const auto baseline = render();
        const auto altered = [&](F key, float value) {
            slot.family = {}; slot.family[key] = value;
            const auto result = render(); double difference = 0, energy = 0;
            for (unsigned i = 0; i < result.size(); ++i) { difference += std::abs(result[i] - baseline[i]); energy += std::abs(result[i]); }
            check(difference > .01 && energy > .01, "family control must change audible processing"); ++cases;
        };
        if (playback == SampleNeonPlayback::Motion) {
            for (unsigned i = 1; i <= 2; ++i) altered(F::MotionSound, float(i));
            for (unsigned i = 1; i <= 4; ++i) altered(F::MotionModel, float(i));
            for (unsigned i = 1; i <= 4; ++i) altered(F::MotionTrajectory, float(i));
            altered(F::MotionJitter, .4f); altered(F::MotionField, .2f);
        } else {
            for (unsigned i = 1; i <= 3; ++i) altered(F::GrainSource, float(i));
            for (unsigned i = 1; i <= 4; ++i) altered(F::GrainProcess, float(i));
            for (unsigned i = 1; i <= 5; ++i) altered(F::GrainWindow, float(i));
            altered(F::GrainPitch, 12); altered(F::GrainScatter, .8f);
            altered(F::GrainSizeVariation, .8f); altered(F::GrainLevelVariation, .8f);
        }
        slot.family = {}; slot.family[F::StackShape] = 0; slot.sourceMode = NeonSourceMode::Scan;
        const auto scanned = render(); slot.family[F::StackJump] = 1; const auto jumped = render();
        check(scanned != jumped && scanned != baseline, "custom stack path and Jump alter linked-source sound");
    }
    std::cout << "Neon family audio/ACN cases: " << cases << '\n';
}

void testChopDestinations()
{
    using namespace s3g::sample;
    for (unsigned source : {0u, 5u, 15u, 31u})
        for (unsigned target = 0u; target < 32u; ++target)
            for (unsigned slices = 1u; slices <= 32u; ++slices) {
                const auto pads = neonChopDestinationPlan(1u << source, source, slices, false, target);
                const bool fits = target + slices <= 32u;
                const bool safe = target == source || source < target || source >= target + slices;
                check(pads.valid() == (fits && safe), "explicit CHOP range must fit and protect a source inside the range");
                if (pads.valid()) {
                    check(pads.count == slices && pads.replacesSource == (target == source), "CHOP plan count/source replacement");
                    for (unsigned n = 0u; n < slices; ++n)
                        check(pads.pads[n] == target + n, "explicit slice destinations must be consecutive across banks");
                }
                const auto stack = neonChopDestinationPlan(1u << source, source, slices, true, target);
                check(stack.valid() && stack.count == 1u && stack.pads[0] == target,
                    "one-pad layer stack must use the exact chosen target for all slice counts");
            }
    const auto autoPads = neonChopDestinationPlan(0x15u, 0u, 3u, false);
    check(autoPads.valid() && autoPads.pads[0] == 1u && autoPads.pads[1] == 3u && autoPads.pads[2] == 5u,
        "Auto keeps its first-empty policy and skips occupied pads");
    const auto blocked = neonChopDestinationPlan(1u << 9u, 0u, 3u, false, 8u);
    check(blocked.error == NeonChopDestinationError::Occupied && blocked.blockedPad == 9u && blocked.count == 0u,
        "occupied pad in middle of explicit range must reject the whole assignment");
    check(!neonChopDestinationPlan(0xffffffffu, 0u, 1u, true, 8u).valid(), "stack must protect an occupied non-source target");
    const auto replace = neonChopDestinationPlan(1u << 5u, 5u, 32u, false, kNeonChopAutoDestination, true);
    check(replace.valid() && replace.count == 32u && replace.pads[0] == 5u && replace.pads[1] == 0u
        && replace.pads[31] == 31u, "hardware Shift assignment retains source-first/empty-pad behavior");
    check(!neonChopDestinationPlan(1u, 0u, 32u, false).valid(), "Auto keep-source must reject insufficient capacity");
    for (unsigned invalid : {33u, 255u})
        check(!neonChopDestinationPlan(0u, 0u, 1u, false, invalid).valid(), "invalid destination must not address outside the pad array");
}

void testStackPerformanceOverride()
{
    using namespace s3g::sample;
    auto a=rampAsset(48001), b=rampAsset(48001);
    for(auto* asset:{&a,&b}) {
        asset->channelCount=16;
        for(unsigned ch=0;ch<16;++ch) asset->channels[ch].assign(48001,(asset==&a?.02f:-.01f)*(ch+1));
    }
    NeonStack stack; stack.count=32;
    for(unsigned n=0;n<32;++n) stack.layers[n]={n%2?&b:&a,0,1};
    auto engine=std::make_unique<SampleNeonEngine>();
    check(engine->prepare(48000,64)&&engine->setAsset(0,&a),"STACK performance prepare");
    SampleNeonSettings settings;settings.outputLayout=SampleNeonOutputLayout::Ambisonic3;settings.masterGainDecibels=0;
    auto& s=settings.slots[0];s.stack=&stack;s.sourceFormat=SampleNeonSourceFormat::Ambisonic;s.gainDecibels=0;
    s.triggerMode=TriggerMode::Gate;s.clock=SampleNeonClock::Free;s.techniqueAttackSeconds=.001f;s.techniqueReleaseSeconds=.001f;
    s.family[NeonFamily::LaneSlew]=.001f;s.family[NeonFamily::LaneAuto]=1;s.stackCycleSeconds=.1f;
    s.sourceMode=NeonSourceMode::Scan;
    OutputBlock output;
    const auto render=[&](const SampleNeonEvent* event=nullptr) {
        engine->render(settings,event,event?1:0,output.pointers.data(),32,64);
        for(unsigned ch=1;ch<16;++ch)for(unsigned frame=0;frame<64;++frame)
            check(std::isfinite(output.samples[ch][frame])&&std::abs(output.samples[ch][frame]-output.samples[0][frame]*(ch+1))<.00002f,
                "STACK cue preserves linked ACN/SN3D channels");
    };
    for(auto method:{SampleNeonPlayback::Motion,SampleNeonPlayback::Grains,SampleNeonPlayback::Stretch,SampleNeonPlayback::Lanes}) {
        engine->reset();s.playback=method;
        const auto saved=s.family.values;
        SampleNeonEvent note;note.noteId=42;render(&note);
        SampleNeonEvent cue;cue.kind=SampleNeonEventKind::StackPosition;cue.value=1;cue.frameOffset=32;render(&cue);
        for(unsigned n=0;n<300;++n)render();
        check(engine->stackPosition(0)>.999f && engine->stackWaveformLayer(0,settings)==-2,"held cue reaches layer 32 without changing technique");
        cue.frameOffset=0;cue.value=0;render(&cue);for(unsigned n=0;n<300;++n)render();
        check(engine->stackPosition(0)<.001f,"held cue returns to first layer with slew");
        cue.value=-1;render(&cue);bool moved=false;
        for(unsigned n=0;n<150;++n){render();moved|=engine->stackPosition(0)>.2f;}
        check(moved&&s.family.values==saved&&s.sourceMode==NeonSourceMode::Scan,"release resumes saved navigation without rewriting path");
        // A layer audition deliberately ignores the path until the user
        // explicitly selects navigation again; takeover must not retrigger.
        note.selectedSource = true; s.selectedLayer = 31; render(&note);
        for (unsigned n = 0; n < 100; ++n) render();
        check(engine->stackWaveformLayer(0,settings) == -1, "isolated edit audition retains its layer");
        cue.value = 1; render(&cue);
        for (unsigned n = 0; n < 100; ++n) render();
        cue.value = -1; cue.resumeNavigation = true; cue.frameOffset = 32; render(&cue);
        bool low = false, high = false;
        for (unsigned n = 0; n < 600; ++n) {
            render(); low |= engine->stackPosition(0) < .3f; high |= engine->stackPosition(0) > .7f;
        }
        check(low && high && engine->stackWaveformLayer(0,settings) == -2 && s.selectedLayer == 31,
            "explicit navigation resumes held stack scan after latched override and edit audition");
        check(s.family.values == saved, "navigation takeover retains breakpoint shape and timing");
        note.kind=SampleNeonEventKind::Release;render(&note);for(unsigned n=0;n<10;++n)render();
        check(!engine->slotPlaybackActive(0),"layer override must not strand a released underlying voice");
    }
    engine->reset();s.playback=SampleNeonPlayback::Sample;s.sourceMode=NeonSourceMode::Primary;s.selectedLayer=0;
    SampleNeonEvent note;note.sourceLayer=31;render(&note);
    check(engine->stackWaveformLayer(0,settings)==31&&s.selectedLayer==0&&s.sourceMode==NeonSourceMode::Primary,
        "explicit layer audition follows sounding source without changing edit layer or source mode");
}

void testStackShapes()
{
    using namespace s3g::sample;
    using F = NeonFamily;
    const auto t = neonFamilyIndex(F::PathTime), v = neonFamilyIndex(F::PathValue);
    for (unsigned shape = 1; shape <= 6; ++shape) for (unsigned count = 2; count <= 32; ++count) {
        NeonFamilySettings f;
        f[F::StackCurve] = .75f; f[F::StackOffset] = .2f; f[F::GrainPitch] = 7;
        neonSetStackShape(f, static_cast<NeonStackShape>(shape), count, 19);
        const unsigned points = static_cast<unsigned>(f[F::PathCount]);
        check(f.valid() && points >= neonStackShapeMinimum(static_cast<NeonStackShape>(shape)), "shape points valid and ordered at every supported resolution");
        check(f[F::StackCurve] == 0 && f[F::StackOffset] == .2f && f[F::GrainPitch] == 7, "shape replaces curve without changing clock/technique");
        f[F::StackOffset] = 0;
        for (unsigned n = 0; n + 1 < points; ++n)
            check(std::abs(neonStackPath(f, f.values[t + n]) - f.values[v + n]) < 1e-6, "audio visits displayed breakpoint coordinates");
        const auto before = f.values;
        neonSetStackShape(f, NeonStackShape::Manual, count);
        for (unsigned i = 1; i < before.size(); ++i)
            check(f.values[i] == before[i], "Manual must unlock without resetting points/settings");
    }
    NeonFamilySettings up, down, triangle, sine, square;
    neonSetStackShape(up, NeonStackShape::RampUp, 32);
    neonSetStackShape(down, NeonStackShape::RampDown, 32);
    neonSetStackShape(triangle, NeonStackShape::Triangle, 32);
    neonSetStackShape(sine, NeonStackShape::Sine, 32);
    neonSetStackShape(square, NeonStackShape::Square, 32);
    check(std::abs(neonStackPath(up,.125)-.125) < 1e-6 && std::abs(neonStackPath(down,.125)-.875) < 1e-6, "ramp directions");
    check(std::abs(neonStackPath(triangle,.5)-1) < 1e-6 && std::abs(neonStackPath(triangle,.75)-.5) < 1e-6, "Triangle reaches full stack with even point count");
    check(neonStackPath(sine,.125) < .15 && neonStackPath(sine,.125) > .14, "Sine has curved ramp distinct from Triangle");
    check(neonStackPath(square,.25) == 0 && neonStackPath(square,.75) == 1, "Square plateaus");
}

void testStackPointEditing() {
    using namespace s3g::sample;
    using F = NeonFamily;
    const auto t = neonFamilyIndex(F::PathTime), v = neonFamilyIndex(F::PathValue);
    for (unsigned shape = 1; shape <= 6; ++shape) {
        NeonFamilySettings f; neonSetStackShape(f, static_cast<NeonStackShape>(shape), 9);
        f[F::StackCurve] = .6f; f[F::StackOffset] = .2f; f[F::LaneAuto] = 1;
        const auto before = f;
        check(neonMoveStackPoint(f, 4, f.values[t+4], f.values[v+4]) && f.valid()
            && f[F::StackShape] == 0, "direct point edit unlocks every named shape");
        f[F::StackShape] = before[F::StackShape];
        check(f.values == before.values, "unlock keeps existing curve coordinates, phase and navigation intact");
        check(neonMoveStackPoint(f,4,-1,2) && f.valid() && f.values[t+4] > f.values[t+3]
            && f.values[v+4] == 1, "drag clamps to neighbors and stack range");
        check(neonMoveStackPoint(f,4,2,-1) && f.valid() && f.values[t+4] < f.values[t+5]
            && f.values[v+4] == 0, "drag cannot cross the next node");
        check(neonMoveStackPoint(f,0,.4f,.7f) && neonMoveStackPoint(f,8,.6f,.3f)
            && f.valid() && f.values[t] == 0 && f.values[t+8] == 1, "endpoints move vertically only");
        check(neonRemoveStackPoint(f,4) && f.valid() && f[F::PathCount] == 8
            && f[F::StackCurve] == .6f && f[F::StackOffset] == .2f && f[F::LaneAuto] == 1,
            "right-click removal keeps curve/navigation controls");
    }
    NeonFamilySettings f; neonSetStackShape(f,NeonStackShape::RampUp,2);
    check(neonAddStackPoint(f,.7f,.2f,.01f) == 1 && neonAddStackPoint(f,.2f,.8f,.01f) == 1
        && f.valid() && f[F::PathCount] == 4 && f.values[t+1] == .2f && f.values[t+2] == .7f,
        "empty-space clicks insert sorted points");
    check(neonAddStackPoint(f,.2f,.4f,.01f) == 1 && f[F::PathCount] == 4 && f.valid(),
        "vertically aligned click cannot create a duplicate time");
    check(!neonRemoveStackPoint(f,0) && !neonRemoveStackPoint(f,3), "cannot remove endpoints");
    check(neonRemoveStackPoint(f,1) && neonRemoveStackPoint(f,1) && !neonRemoveStackPoint(f,1)
        && f.valid() && f[F::PathCount] == 2, "two-point minimum");
    neonSetStackShape(f,NeonStackShape::RampUp,32);
    const auto full = f.values;
    check(neonAddStackPoint(f,.51f,.1f,.01f) == -1 && f.values == full, "32-point cap does not overwrite a node");
    check(neonAddStackPoint(f,.001f,.6f,.01f) == 0 && f.valid() && f[F::PathCount] == 32
        && f.values[t] == 0 && f.values[v] == .6f, "endpoint editing remains available at capacity");
    const auto safe = f.values;
    check(!neonMoveStackPoint(f,3,std::numeric_limits<float>::quiet_NaN(),0)
        && neonAddStackPoint(f,0,std::numeric_limits<float>::infinity(),.01f) == -1 && f.values == safe,
        "reject nonfinite breakpoint input");
}

void testStackPathPhase() {
    using namespace s3g::sample;
    auto a = rampAsset(48000); NeonStack stack; stack.count = 2;
    stack.layers[0] = {&a,0,1}; stack.layers[1] = {&a,0,1};
    auto engine = std::make_unique<SampleNeonEngine>();
    check(engine->prepare(48000,64) && engine->setAsset(0,&a), "path phase prepare");
    SampleNeonSettings settings; auto& s = settings.slots[0];
    s.stack = &stack; s.playback = SampleNeonPlayback::Lanes; s.clock = SampleNeonClock::Free;
    s.triggerMode = TriggerMode::Gate; s.stackCycleSeconds = 1; s.stackCycleBeats = 8;
    s.family[NeonFamily::LaneAuto] = 1; s.family[NeonFamily::StackOffset] = .125f;
    SampleNeonEvent note; OutputBlock out;
    const auto render = [&](const SampleNeonEvent* event = nullptr) {
        engine->render(settings,event,event?1:0,out.pointers.data(),32,64);
    };
    check(engine->stackPathPhase(0,settings) == -1, "stopped path has no false playhead");
    render(&note);
    check(std::abs(engine->stackPathPhase(0,settings) - (.125 + 63./48000)) < 1e-6,
        "FREE phase includes offset and actual rendered age");
    s.clock = SampleNeonClock::Host; settings.hostBeatValid = true;
    settings.transportPlaying = true; settings.hostBeatPosition = 2; settings.hostTempoBpm = 120;
    render();
    check(std::abs(engine->stackPathPhase(0,settings) - (.375 + 63./48000*2/8)) < 1e-6,
        "HOST phase follows beat timeline and stack cycle");
    const float paused = engine->stackPathPhase(0,settings);
    settings.transportPlaying = false; for(unsigned n=0;n<10;++n) render();
    check(engine->stackPathPhase(0,settings) == paused, "stopped HOST keeps the last real phase");
    s.family[NeonFamily::LaneAuto] = 0;
    check(engine->stackPathPhase(0,settings) == -1, "manual Lanes has no automatic path dot");
    s.playback = SampleNeonPlayback::Grains; s.clock = SampleNeonClock::Free;
    s.sourceMode = NeonSourceMode::Scan; s.grainDensityHz = 20;
    s.family[NeonFamily::StackAdvance] = 1;
    render(&note); const float first = engine->stackPathPhase(0,settings);
    check(std::abs(first-.125f)<1e-6, "event path starts at the first emitted grain plus offset");
    for(unsigned n=0;n<10;++n)render();
    check(engine->stackPathPhase(0,settings) == first, "event path does not drift between grains");
    for(unsigned n=0;n<30;++n)render();
    check(std::abs(engine->stackPathPhase(0,settings) - (first+1.f/32)) < 1e-6,
        "event path advances once per emitted grain");
    note.selectedSource = true; render(&note);
    check(engine->stackPathPhase(0,settings) == -1, "edit-layer audition hides inactive path dot");
    engine->killAll(); check(engine->stackPathPhase(0,settings) == -1, "kill clears path phase");
}

void testLanesPlayback() {
    using namespace s3g::sample;
    auto a = rampAsset(48001), b = rampAsset(24001);
    for (auto* asset : {&a, &b}) {
        asset->channelCount = 16;
        for (unsigned ch = 1; ch < 16; ++ch) {
            asset->channels[ch] = asset->channels[0];
            for (auto& v : asset->channels[ch]) v *= float(ch + 1);
        }
    }
    NeonStack stack; stack.count = 32;
    for (unsigned n = 0; n < 32; ++n) stack.layers[n] = {n % 2 ? &b : &a, 0, 1};
    auto engine = std::make_unique<SampleNeonEngine>();
    check(engine->prepare(48000, 64) && engine->setAsset(0, &a), "Lanes prepare");
    SampleNeonSettings settings; settings.masterGainDecibels = 0;
    settings.outputLayout = SampleNeonOutputLayout::Ambisonic3;
    auto& s = settings.slots[0]; s.stack = &stack; s.playback = SampleNeonPlayback::Lanes;
    s.sourceFormat = SampleNeonSourceFormat::Ambisonic; s.gainDecibels = 0;
    s.triggerMode = TriggerMode::Gate; s.clock = SampleNeonClock::Free;
    s.sourceMode = NeonSourceMode::Random; // Stored source selector must not compete with Lanes.
    s.techniqueAttackSeconds = .001f; s.techniqueReleaseSeconds = .001f;
    s.family[NeonFamily::LaneJoin] = 0;
    SampleNeonEvent note; note.noteId = 10; note.value = 1;
    OutputBlock out;
    const auto render = [&](const SampleNeonEvent* event = nullptr) {
        engine->render(settings, event, event ? 1 : 0, out.pointers.data(), 32, 64);
        for (unsigned ch = 1; ch < 16; ++ch) for (unsigned i = 0; i < 64; ++i)
            check(std::abs(out.samples[ch][i] - out.samples[0][i] * (ch + 1)) < .00002f, "Lanes preserves ACN channel ratios");
    };
    render(&note);
    render();
    check(std::abs(out.samples[0][32] - 96.f / 48001) < .00001f, "Lanes is continuous native-rate PCM, not a grain window");
    check(engine->stackWaveformLayer(0, settings) == -2 && engine->stackPosition(0) == 0, "Lanes manually follows first layer independent of Random source mode");
    s.family[NeonFamily::LanePosition] = 1;
    for (unsigned n = 0; n < 100; ++n) render();
    check(engine->stackPosition(0) > .999f && engine->voiceCursorCount(0) == 1
        && engine->voiceCursors(0)[0].sourceAsset == &b && engine->voiceCursors(0)[0].sourcePositionNormalized > .13,
        "held Lanes reaches layer 32 without restarting the read head");
    check(engine->voiceCursors(0)[0].layer == 31 && engine->voiceCursors(0)[0].level > .999f,
        "Lanes display reports actual layer identity and smoothed weight even with duplicate assets");
    s.family[NeonFamily::LanePosition] = .5f;
    for (unsigned n=0;n<100;++n) render();
    float energy=0; bool layer16=false,layer17=false;
    for (unsigned n=0;n<engine->voiceCursorCount(0);++n) {
        const auto& cursor=engine->voiceCursors(0)[n]; energy+=cursor.level*cursor.level;
        layer16 |= cursor.layer==15; layer17 |= cursor.layer==16;
    }
    check(layer16 && layer17 && std::abs(energy-1.f)<.001f,
        "Lanes cursor weights expose the actual equal-power mix");
    settings.transportPlaying = false; s.clock = SampleNeonClock::Host; render();
    check(out.samples[0][32] == 0, "Lanes HOST pauses");
    settings.transportPlaying = true; render();
    check(out.samples[0][32] > .13f, "Lanes HOST resumes instead of retriggering");
    s.clock = SampleNeonClock::Free; s.family[NeonFamily::LaneAuto] = 1; s.stackCycleSeconds = .05f;
    bool first = false, last = false;
    for (unsigned n = 0; n < 100; ++n) { render(); first |= engine->stackPosition(0) < .3; last |= engine->stackPosition(0) > .7; }
    check(first && last, "Lanes optional breakpoint path traverses while held");
    SampleNeonEvent release = note; release.kind = SampleNeonEventKind::Release;
    render(&release); render();
    check(out.samples[0][32] == 0 && !engine->slotPlaybackActive(0), "Lanes release ends held gesture");
    s.family[NeonFamily::LaneAuto] = 0; s.family[NeonFamily::LanePosition] = .5;
    s.family[NeonFamily::StackJump] = 1;
    for (unsigned n = 1; n < 31; ++n) stack.layers[n].asset = nullptr;
    render(&note);
    check(engine->stackPosition(0) == 1, "Lanes jump skips missing layers with nearest loaded tie-break");
    note.kind = SampleNeonEventKind::Choke; render(&note);
    check(out.samples[0][32] == 0, "Lanes explicit STOP silences immediately");
    note.kind = SampleNeonEventKind::Trigger; note.mode = s3g::controller::reloop_neon::Mode::Slicer;
    render(&note);
    check(engine->stackWaveformLayer(0, settings) == -1 && engine->voiceCursorCount(0) != 0, "Lanes CHOP remains direct source audition");
    s.triggerMode = TriggerMode::OneShot; s.shotSeconds = .05f;
    note.mode = s3g::controller::reloop_neon::Mode::Sampler; render(&note);
    for (unsigned n = 0; n < 40; ++n) render();
    check(!engine->slotPlaybackActive(0), "Lanes one-shot respects explicit duration");
}

void testPlaybackVisuals() {
    using namespace s3g::sample;
    auto source=rampAsset(48000);
    auto engine=std::make_unique<SampleNeonEngine>();
    check(engine->prepare(48000,64) && engine->setAsset(0,&source),"visual telemetry prepare");
    SampleNeonSettings settings; auto& s=settings.slots[0];
    s.playback=SampleNeonPlayback::Grains; s.clock=SampleNeonClock::Free;
    s.grainDensityHz=1; s.grainSizeMs=200; s.grainSpray=0; s.grainReverseChance=1;
    s.launchPosition=.4; s.family[NeonFamily::GrainWindow]=2;
    s.triggerMode=TriggerMode::Gate;
    OutputBlock output; SampleNeonEvent note; note.noteId=11; note.value=1;
    engine->render(settings,&note,1,output.pointers.data(),32,64);
    check(engine->voiceCursorCount(0)>0,"Grains publishes live window");
    auto first=engine->voiceCursors(0)[0];
    check(first.reverse && first.window==1 && first.windowPhase>0 && first.windowPhase<1,
        "Grains publishes latched direction/envelope and elapsed phase");
    check(std::abs(neonCursorSource(first,first.windowPhase)-first.sourcePositionNormalized)<.0001,
        "reverse grain contour and audible read position agree");
    s.family[NeonFamily::GrainWindow]=4;
    engine->render(settings,nullptr,0,output.pointers.data(),32,64);
    const auto second=engine->voiceCursors(0)[0];
    check(second.window==first.window && second.windowPhase>first.windowPhase,
        "editing Grain Window does not redraw an existing voice with a new envelope");
    check(std::isfinite(second.level) && second.level>=0,"cursor intensity follows finite rendered voice level");
    for (unsigned window=0;window<5;++window) for (float skew : {-.8f,0.f,.8f}) {
        auto cursor=second; cursor.window=static_cast<uint8_t>(window); cursor.windowSkew=skew;
        for (unsigned n=0;n<=32;++n) {
            const auto phase=n/32.f;
            check(neonCursorEnvelope(cursor,phase)==grainWindow(static_cast<GrainEnvelope>(window),phase,skew),
                "Grains contour reuses the exact DSP window function");
        }
    }
    VoiceCursor adsr; adsr.attack=.1f; adsr.decay=.2f; adsr.sustain=.4f; adsr.release=.2f;
    check(std::abs(neonCursorEnvelope(adsr,.05f)-.5f)<.00001f
        && std::abs(neonCursorEnvelope(adsr,.9f)-.2f)<.00001f,"legacy ADSR contour retains attack and release");
    for (unsigned count=1;count<=32;++count) {
        const auto layout=neonLaneViewLayout(count,510);
        check(layout.rows==count && layout.height>=14,"all stack lanes fit at readable height through layer 32");
        check(layout.top(0)==0 && layout.top(count-1)+layout.height<=510,"first and last lanes stay inside waveform");
        for (unsigned layer=1;layer<count;++layer)
            check(layout.top(layer)>layout.top(layer-1)+layout.height,"lane rows never overlap");
    }
    check(neonLaneViewLayout(0,510).rows==0 && neonLaneViewLayout(32,0).rows==0,
        "empty lane layout safely handles no rows or height");
    NeonVisualPublication publication; NeonVisualSnapshot snapshot;
    publication.publish(engine->voiceCursors(0),engine->voiceCursorCount(0));
    check(publication.read(snapshot) && snapshot.count>0 && snapshot.cursors[0].reverse
        && snapshot.cursors[0].sourceAsset==&source && snapshot.cursors[0].window==1,
        "atomic GUI snapshot carries complete grain identity and window");
    publication.clear(); check(publication.read(snapshot) && snapshot.count==0 && !snapshot.motion.active,
        "reset clears published cursors and scope");
    engine->reset(); s.playback=SampleNeonPlayback::Motion; s.motionCycleSeconds=.7f;
    s.family[NeonFamily::MotionSound]=2;
    engine->render(settings,&note,1,output.pointers.data(),32,64);
    const auto motion=engine->motionVisual(0,settings);
    check(motion.active && motion.seconds>0 && std::abs(motion.cycle-.7)<.00001
        && motion.articulation==neonMotionArticulation(s.family,motion.seconds),
        "Motion scope publishes engine clock and actual packet/motor articulation");
    check(std::abs(engine->motionPosition(0,settings)-SampleNeonEngine::motionTrajectory(s,
        motion.seconds/motion.cycle,motion.seconds,0))<.00001,"Motion scope trajectory agrees with audio scan");
    s.clock=SampleNeonClock::Host; settings.transportPlaying=false;
    engine->render(settings,nullptr,0,output.pointers.data(),32,64);
    const auto paused=engine->motionVisual(0,settings);
    engine->render(settings,nullptr,0,output.pointers.data(),32,64);
    check(engine->motionVisual(0,settings).seconds==paused.seconds,"HOST scope cannot drift while stopped");
    engine->killAll(); check(!engine->motionVisual(0,settings).active,"killed Motion leaves no fake moving scope");
}

void testSliceEnvelopeAndRouting() {
    using namespace s3g::sample;
    using F=NeonFamily;
    auto source=std::make_shared<SampleAsset>(rampAsset(48000));
    auto engine=std::make_unique<SampleNeonEngine>();
    check(engine->prepare(48000,64) && engine->setAsset(0,source.get()),"routing/envelope prepare");
    SampleNeonSettings s; auto& c=s.slots[0];
    s.masterGainDecibels=0; c.gainDecibels=0; c.triggerMode=TriggerMode::Gate;
    c.clock=SampleNeonClock::Free; c.sliceCount=2;
    c.sliceLayout=equalSampleNeonSliceLayout(2); c.sliceLayout.boundaries[1]=.25;
    c.family[F::SliceAttack]=.1f; c.family[F::SliceDecay]=.2f;
    c.family[F::SliceSustain]=.4f; c.family[F::SliceRelease]=.3f;
    c.playback=SampleNeonPlayback::SliceSequence;
    c.technique[0]=0; c.technique[3]=1;
    OutputBlock out; SampleNeonEvent note; note.noteId=1;
    engine->render(s,&note,1,out.pointers.data(),32,64);
    auto head=engine->voiceCursors(0)[0];
    check(engine->voiceCursorCount(0)==1 && std::abs(head.sourceEndNormalized-.25f)<.0001f
        && std::abs(head.attack-.1f)<.0001f && std::abs(head.decay-.2f)<.0001f
        && std::abs(head.sustain-.4f)<.0001f && std::abs(head.release-.3f)<.0001f,
        "Slice Sequence uses proportional ADSR on the short slice");
    for(unsigned n=1;n<=750;++n) engine->render(s,nullptr,0,out.pointers.data(),32,64);
    head=engine->voiceCursors(0)[0];
    check(std::abs(head.sourceStartNormalized-.25f)<.0001f && std::abs(head.sourceEndNormalized-1)<.0001f
        && std::abs(head.attack-.1f)<.0001f && std::abs(head.release-.3f)<.0001f,
        "Unequal next slice retains proportions but scales times to its own window");
    for (float tune : {-12.f,0.f,12.f}) {
        engine->reset(); c.tuneSemitones=tune; c.playback=SampleNeonPlayback::Sample;
        note.mode=s3g::controller::reloop_neon::Mode::Slicer; note.performanceIndex=1;
        engine->render(s,&note,1,out.pointers.data(),32,64);
        const auto slice=engine->voiceCursors(0)[0];
        check(std::abs(slice.attack-.1f)<.0002f && std::abs(slice.decay-.2f)<.0002f
            && std::abs(slice.release-.3f)<.0002f,"CHOP audition matches the slice envelope at changed pitch");
    }
    c.family[F::SliceAttack]=c.family[F::SliceDecay]=c.family[F::SliceRelease]=1;
    const auto envelope=neonSliceEnvelope(c.family);
    check(std::abs(envelope.attack+envelope.decay+envelope.release-1)<.00001f,
        "overfull ADR proportions fit the slice without losing their relative shape");
    note.mode=s3g::controller::reloop_neon::Mode::Sampler;
    c.tuneSemitones=0; c.launchPosition=.3; c.grainPosition=.3f; c.grainSpray=.5f;
    c.grainDensityHz=1; c.grainSizeMs=200; c.family[F::GrainWindow]=2;
    const auto energy=[&](unsigned channel) { double v=0; for(float x:out.samples[channel])v+=std::abs(x);return v; };
    c.playback=SampleNeonPlayback::Grains;
    engine->reset(); engine->render(s,&note,1,out.pointers.data(),32,64);
    check(out.samples[0]==out.samples[1],"linked grains preserve identical stereo channels");
    c.family[F::GrainStereoLink]=1;
    engine->render(s,&note,1,out.pointers.data(),32,64);
    check(engine->voiceCursorCount(0)==2 && out.samples[0]!=out.samples[1]
        && energy(0)>0 && energy(1)>0,"independent grains retain both source channels with distinct read trajectories");
    c.family[F::GrainStereoLink]=0; c.family[F::RoutingMode]=1; c.family[F::RoutingWidth]=1;
    s.outputLayout=SampleNeonOutputLayout::Quad; c.outputBus=1; c.grainSpray=0;
    c.family[F::LanePosition]=0;
    for (auto method : {SampleNeonPlayback::Sample,SampleNeonPlayback::Motion,SampleNeonPlayback::Grains,
        SampleNeonPlayback::SliceSequence,SampleNeonPlayback::Stretch,SampleNeonPlayback::Lanes}) {
        c.playback=method; engine->reset(); ++note.noteId;
        engine->render(s,&note,1,out.pointers.data(),32,64);
        for(unsigned n=0;n<3;++n)engine->render(s,nullptr,0,out.pointers.data(),32,64);
        check(energy(4)>0 && energy(5)>0 && energy(6)==0 && energy(7)==0,
            "distribute starts in first stereo pair of selected quad bus");
        check(energy(0)==0 && energy(8)==0,"distribute cannot leak outside selected bus");
        SampleNeonEvent stop; stop.kind=SampleNeonEventKind::Choke;
        engine->render(s,&stop,1,out.pointers.data(),32,64);
        ++note.noteId; engine->render(s,&note,1,out.pointers.data(),32,64);
        // Motion may have launched further windows; test exact next-pair
        // sequence on one-event methods and continuous gestures.
        if(method==SampleNeonPlayback::Sample || method==SampleNeonPlayback::Grains || method==SampleNeonPlayback::Lanes)
            check(energy(6)>0 && energy(7)>0 && energy(4)==0,"next object advances to the next stereo pair");
    }
    auto waveSource=std::make_shared<SampleAsset>(*source);
    for(unsigned ch=0;ch<2;++ch)for(unsigned frame=0;frame<48000;++frame)
        waveSource->channels[ch][frame]=std::sin(float(frame)*.06f)*.2f;
    const auto waveMap=analyzeWavesets(waveSource);
    check(waveMap&&waveMap->valid(),"distributed waveset fixture analysis");
    engine->setPreparedAsset(0,waveSource.get()); engine->setPreparedWavesets(0,waveMap.get());
    NeonStack stack; stack.count=2;
    stack.layers[0]={waveSource.get(),0,1,waveMap.get()}; stack.layers[1]=stack.layers[0];
    c.stack=&stack; c.playback=SampleNeonPlayback::Wavesets;
    for(auto sourceMode:{NeonSourceMode::Primary,NeonSourceMode::Scan}) {
        c.sourceMode=sourceMode; engine->reset(); ++note.noteId;
        engine->render(s,&note,1,out.pointers.data(),32,64);
        for(unsigned n=0;n<5;++n)engine->render(s,nullptr,0,out.pointers.data(),32,64);
        check(energy(4)>0 && energy(5)>0 && energy(6)==0 && energy(7)==0,
            "Wavesets routes one coherent stereo object per pad gesture, including stack scan");
        check(engine->voiceCursorCount(0)>0 && engine->voiceCursors(0)[0].layer<2,
            "Wavesets publishes logical layer identity for Stack Lanes");
    }
    c.stack=nullptr; c.sourceMode=NeonSourceMode::Primary;
    auto spatial=std::make_shared<SampleAsset>(); spatial->sampleRate=48000; spatial->channelCount=16;
    for(unsigned ch=0;ch<16;++ch) { spatial->channels[ch]=source->channels[0];
        for(float& x:spatial->channels[ch])x*=float(ch+1)*.02f; }
    check(engine->setAsset(0,spatial.get()),"load 16-channel routing source");
    c.playback=SampleNeonPlayback::Grains; c.outputBus=0;
    c.family[F::GrainStereoLink]=1; c.family[F::RoutingWidth]=0;
    c.sourceFormat=SampleNeonSourceFormat::Ambisonic; s.outputLayout=SampleNeonOutputLayout::Ambisonic3;
    engine->reset(); engine->render(s,&note,1,out.pointers.data(),32,64);
    bool coherent=energy(0)>0;
    for(unsigned ch=1;ch<16;++ch)for(unsigned n=0;n<64;++n)
        coherent &= std::abs(out.samples[ch][n]-out.samples[0][n]*(ch+1))<1.e-5f;
    check(coherent,"Ambisonics forces linked Preserve Field even with latent independent/distribute controls");
    c.sourceFormat=SampleNeonSourceFormat::Discrete; s.outputLayout=SampleNeonOutputLayout::Stereo;
    engine->reset(); engine->render(s,&note,1,out.pointers.data(),32,64);
    check(energy(0)>0 && energy(1)==0 && energy(2)==0,"explicit Distribute folds a wide discrete source into a mono object");
    c.family[F::RoutingMode]=0; engine->render(s,&note,1,out.pointers.data(),32,64);
    check(energy(0)==0 && energy(1)==0,"Preserve Field never silently folds a wide source onto a narrow bus");
}

void testSliceSequenceHandoffs()
{
    using namespace s3g::sample;
    using F = NeonFamily;
    auto asset = constantAsset(.5f, .5f, 192000);
    SampleNeonSettings settings;
    auto& c = settings.slots[0];
    settings.masterGainDecibels = c.gainDecibels = 0;
    c.triggerMode = TriggerMode::Gate;
    c.playback = SampleNeonPlayback::SliceSequence;
    c.clock = SampleNeonClock::Free;
    c.sliceCount = 2; c.sliceLayout = equalSampleNeonSliceLayout(2);
    c.technique[0] = c.technique[3] = 1;
    c.family[F::SliceAttack] = c.family[F::SliceDecay] = c.family[F::SliceRelease] = 0;
    c.family[F::SliceSustain] = 1;
    const auto render = [&](unsigned blockSize, double rate, unsigned frames, bool stop = false) {
        auto engine = std::make_unique<SampleNeonEngine>();
        check(engine->prepare(rate, 64) && engine->setAsset(0, &asset), "prepare slice handoff fixture");
        OutputBlock out;
        SampleNeonEvent note; note.noteId = 123;
        std::vector<float> audio;
        audio.reserve(frames);
        bool bounded = true, coherent = true;
        for (unsigned at = 0; at < frames;) {
            const unsigned size = std::min(blockSize, frames - at);
            SampleNeonEvent release = note; release.kind = SampleNeonEventKind::Release;
            const bool releaseNow = stop && at == 4096;
            engine->render(settings, at == 0 ? &note : releaseNow ? &release : nullptr,
                at == 0 || releaseNow ? 1 : 0, out.pointers.data(), 32, size);
            bounded &= engine->voiceCursorCount(0) <= 2;
            for (unsigned n = 0; n < size; ++n) {
                audio.push_back(out.samples[0][n]);
                for (unsigned ch = 1; ch < asset.channelCount; ++ch)
                    coherent &= std::abs(out.samples[ch][n] - out.samples[0][n]
                        * (asset.channelCount == 16 ? float(ch + 1) : 1.f)) < .00001f;
            }
            at += size;
        }
        check(bounded, "slice handoffs never accumulate more than two voices");
        check(coherent, "slice handoff weights preserve the channel field");
        return audio;
    };
    const auto maxJump = [](const std::vector<float>& audio) {
        float result = 0;
        for (std::size_t i = 1; i < audio.size(); ++i)
            result = std::max(result, std::abs(audio[i] - audio[i - 1]));
        return result;
    };
    auto audio = render(64, 48000, 12000);
    bool unity = true;
    for (unsigned i = 240; i < audio.size(); ++i) unity &= std::abs(audio[i] - .5f) < .000001f;
    check(unity, "correlated continuous slices crossfade without a gain bump or dip at zero ADR");
    c.clock = SampleNeonClock::Host; c.grainIntervalBeats = .03125;
    settings.hostTempoBpm = 300; settings.transportPlaying = true;
    for (double rate : {44100., 48000., 96000.}) {
        const auto fast = render(64, rate, 4000);
        for (unsigned i = static_cast<unsigned>(std::ceil(rate * .005)); i < fast.size(); ++i)
            unity &= std::abs(fast[i] - .5f) < .000001f;
    }
    check(unity, "fractional fast steps retain complementary fade lengths without gain flutter");
    c.clock = SampleNeonClock::Free;
    for (unsigned ch = 0; ch < 2; ++ch)
        std::fill(asset.channels[ch].begin() + 96000, asset.channels[ch].end(), -.5f);
    audio = render(64, 48000, 12000);
    check(audio == render(13, 48000, 12000), "slice handoffs are invariant to host block partition");
    check(std::abs(audio[2400] - .5f) < .000001f && std::abs(audio[2639] + .5f) < .000001f
        && maxJump(audio) < .007f, "opposite-polarity long slices overlap for 5 ms instead of hard cutting");
    c.family[F::SliceAttack] = .1f; c.family[F::SliceDecay] = .2f;
    c.family[F::SliceSustain] = .4f; c.family[F::SliceRelease] = .3f;
    audio = render(64, 48000, 12000);
    check(std::abs(audio[240] - .5f) < .00001f && std::abs(audio[720] - .2f) < .00001f
        && audio[2399] < .06f && maxJump(audio) < .007f,
        "ADSR fits the step duration and releases before the next handoff, not the long source duration");
    c.family[F::SliceAttack] = c.family[F::SliceDecay] = c.family[F::SliceRelease] = 0;
    c.family[F::SliceSustain] = 1;
    c.family[F::SliceAttack] = .1f; c.family[F::SliceSustain] = 0;
    check(maxJump(render(64, 48000, 12000)) < .007f,
        "zero Decay cannot hard-drop an attacked slice from peak to sustain");
    c.family[F::SliceAttack] = 0; c.family[F::SliceSustain] = 1;
    for (float tune : {-24.f, 0.f, 24.f}) for (unsigned reverse : {0u, 1u}) {
        c.tuneSemitones = tune; c.direction = static_cast<uint8_t>(reverse);
        check(maxJump(render(64, 48000, 12000)) < .007f,
            "reverse and repitched slices retain bounded step transitions");
    }
    c.tuneSemitones = 0; c.direction = 0;
    c.clock = SampleNeonClock::Host; c.grainIntervalBeats = .03125;
    settings.hostTempoBpm = 300; settings.transportPlaying = true;
    for (double rate : {44100., 48000., 96000.}) {
        audio = render(64, rate, 4000);
        check(audio == render(17, rate, 4000) && maxJump(audio) < .023f,
            "160 Hz fractional steps scale safety overlap to a quarter step at every sample rate");
    }
    c.clock = SampleNeonClock::Free;
    audio = render(64, 48000, 8192, true);
    check(audio.back() == 0 && maxJump(audio) < .01f, "gate release fades and terminates overlapping slices");
    {
        auto engine = std::make_unique<SampleNeonEngine>();
        engine->prepare(48000, 64); engine->setAsset(0, &asset);
        SampleNeonEvent note; note.noteId = 987;
        OutputBlock out; float previous = 0, jump = 0;
        bool quietRest = true, bounded = true;
        for (unsigned block = 0; block < 400; ++block) {
            if (block == 64) { c.clock = SampleNeonClock::Host; c.technique[1] = 1; c.technique[2] = .5f; }
            if (block == 128) c.technique[3] = 0;
            if (block == 192) { c.technique[3] = 1; c.clock = SampleNeonClock::Free; }
            engine->render(settings, block ? nullptr : &note, block ? 0 : 1, out.pointers.data(), 32, 64);
            bounded &= engine->voiceCursorCount(0) <= 2;
            for (float x : out.samples[0]) {
                jump = std::max(jump, std::abs(x - previous)); previous = x;
                if (block >= 140 && block < 192) quietRest &= x == 0;
            }
        }
        check(bounded && quietRest && jump < .023f,
            "rate/clock changes, random repeats and skipped steps fade without accumulating voices or stuck tails");
        c.technique[1] = c.technique[2] = 0;
    }
    const auto longAsset = asset;
    asset = constantAsset(.5f, .5f, 512);
    for (unsigned ch = 0; ch < 2; ++ch)
        std::fill(asset.channels[ch].begin() + 256, asset.channels[ch].end(), -.5f);
    for (float tune : {0.f, 24.f}) for (unsigned reverse : {0u, 1u}) {
        c.tuneSemitones = tune; c.direction = static_cast<uint8_t>(reverse);
        audio = render(64, 48000, 4800);
        check(audio[0] == 0 && audio[2399] == 0 && audio.back() == 0 && maxJump(audio) < .051f,
            "short repitched slices fade at their actual source boundary without reading into adjacent slices");
    }
    c.tuneSemitones = 0; c.direction = 0; asset = longAsset;
    // Same clock/fade for all ACN/SN3D components, including a sign-changing field.
    asset.channelCount = 16;
    for (unsigned ch = 1; ch < 16; ++ch) {
        asset.channels[ch] = asset.channels[0];
        for (auto& x : asset.channels[ch]) x *= float(ch + 1);
    }
    c.sourceFormat = SampleNeonSourceFormat::Ambisonic;
    settings.outputLayout = SampleNeonOutputLayout::Ambisonic3;
    render(64, 48000, 6000);
}

void testBufferFill() {
    using namespace s3g::sample;
    NeonFill fill;
    check(fill.prepare(48000), "fill preallocates tapes");
    NeonFillSettings s; s.breakup = 0;
    OutputBlock out;
    const auto render = [&](float input, const NeonFillEvent* event = nullptr, float gain = 1.f) {
        for (unsigned ch = 0; ch < 32; ++ch) out.samples[ch].fill(input * float(ch + 1) * gain);
        fill.render(out.pointers.data(),32,64,120,gain,s,event,event ? 1 : 0);
        for (unsigned ch = 1; ch < 32; ++ch) for (unsigned n = 0; n < 64; ++n)
            check(std::isfinite(out.samples[ch][n]) && std::abs(out.samples[ch][n] - out.samples[0][n] * float(ch + 1)) < .0001,
                "fill retains all 32 source channel relationships");
    };
    NeonFillEvent on {32,true}, off {32,false};
    render(.01f,&on);
    check(fill.active(), "fill can grab partial history at the actual MIDI frame");
    fill.reset();
    NeonFillEvent empty {0,true}; render(0,&empty);
    check(!fill.active(), "empty history cannot strand an override");
    for (unsigned block = 0; block < 400; ++block) render(.01f);
    check(out.samples[0][0] == .01f, "fill idle dry path is bit identical");
    render(-.03f,&on);
    check(out.samples[0][0] == -.03f && out.samples[0][31] == -.03f, "fill note offset never changes earlier samples");
    for (unsigned block = 0; block < 10; ++block) render(-.03f);
    check(out.samples[0][32] > .009f, "fill totally replaces live negative input with captured history");
    render(-.03f,nullptr,.5f);
    check(out.samples[0][32] > .004f && out.samples[0][32] < .0051f, "master gain still controls frozen audio");
    s.breakup = 1; s.repeat = 5;
    bool heard = false, gap = false;
    for (unsigned block = 0; block < 300; ++block) {
        render(-.03f); heard |= out.samples[0][32] > .005f; gap |= std::abs(out.samples[0][32]) < 1e-8f;
    }
    check(heard && gap, "breakup produces repeats and linked gaps");
    render(-.03f,&off);
    for (unsigned block = 0; block < 6; ++block) render(-.03f);
    check(!fill.active() && out.samples[0][32] == -.03f, "fill release returns to current live output");
    render(-.03f,&on); check(fill.active(), "second hold grabs the continuously updated history");
    fill.reset(); render(0);
    check(!fill.active() && out.samples[0][32] == 0, "panic clears frozen and rolling tapes without replaying stale audio");
}

void testFramePlaybackAndMosaic()
{
    using namespace s3g::sample;
    auto asset = std::make_shared<SampleAsset>();
    asset->sampleRate = 48000; asset->channelCount = 16;
    for (unsigned ch = 0; ch < 16; ++ch) {
        asset->channels[ch].resize(16384);
        for (unsigned i = 0; i < 16384; ++i) asset->channels[ch][i] = static_cast<float>(
            .2 * std::sin(6.283185307179586 * 375 * i / 48000) * (ch % 2 ? -.5 : 1));
    }
    const auto map = analyzeWavesets(asset);
    auto engine = std::make_unique<SampleNeonEngine>();
    check(engine->prepare(48000, 64), "frame synthesis prepare");
    engine->setAsset(0, asset.get()); engine->setPreparedWavesets(0, map.get());
    NeonStack stack; stack.count = 2;
    stack.layers[0] = stack.layers[1] = {asset.get(), .1, .9, map.get()};
    SampleNeonSettings settings;
    settings.masterGainDecibels = 0; settings.outputLayout = SampleNeonOutputLayout::Ambisonic3;
    auto& s = settings.slots[0]; s.stack = &stack; s.start = .1; s.end = .9; s.launchPosition = .5;
    s.playback = SampleNeonPlayback::Spectral; s.clock = SampleNeonClock::Free;
    s.triggerMode = TriggerMode::Gate; s.sourceFormat = SampleNeonSourceFormat::Ambisonic;
    s.gainDecibels = 0; s.techniqueAttackSeconds = .005f; s.techniqueReleaseSeconds = .01f;
    s.technique[0] = 0;
    SampleNeonEvent on; on.kind = SampleNeonEventKind::Trigger; on.slot = 0;
    on.mode = s3g::controller::reloop_neon::Mode::Sampler; on.value = 1; on.noteId = 42;
    SampleNeonEvent off = on; off.kind = SampleNeonEventKind::Release;
    OutputBlock out;
    const auto render = [&](const SampleNeonEvent* event = nullptr) { engine->render(settings, event, event ? 1 : 0, out.pointers.data(), 32, 64); };
    for (unsigned mode = 0; mode < 2; ++mode) {
        s.playback = mode ? SampleNeonPlayback::Wavesets : SampleNeonPlayback::Spectral;
        s.family[NeonFamily::WavesetEngine] = static_cast<float>(mode);
        s.family[NeonFamily::OscFrequency] = 220; s.family[NeonFamily::OscPosition] = .5;
        if (mode) {
            asset->channelCount = 2;
            for (unsigned ch = 2; ch < 16; ++ch) asset->channels[ch].clear();
            settings.outputLayout = SampleNeonOutputLayout::Stereo; s.sourceFormat = SampleNeonSourceFormat::Discrete;
        }
        engine->reset(); render(&on);
        double energy = 0; bool linked = true; unsigned crossings = 0; float last = 0;
        std::vector<double> costs;
        for (unsigned block = 0; block < 1000; ++block) {
            const auto begin = std::chrono::steady_clock::now(); render();
            costs.push_back(std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - begin).count());
            for (unsigned i = 0; i < 64; ++i) {
                const float a = out.samples[0][i];
                if (block > 31) { energy += a * a; crossings += a > 0 && last <= 0; }
                last = a;
                for (unsigned ch = 0; ch < asset->channelCount; ++ch)
                    linked &= std::isfinite(out.samples[ch][i]) && std::abs(out.samples[ch][i] - a * (ch % 2 ? -.5f : 1.f)) < .0001f;
            }
        }
        const double hz = crossings * 48000. / (968 * 64);
        check(energy > 1 && linked, "Spectral/Oscillator sustains beyond source duration and links all channels");
        check(std::abs(hz - (mode ? 220 : 375)) < 3, "frame synthesis preserves spectral frequency / oscillator tuning");
        check(engine->voiceCursorCount(0) > 0 && engine->slotPlaybackActive(0), "frame synth publishes active waveform cursor");
        std::sort(costs.begin(), costs.end());
        std::cout << (mode ? "Oscillator stereo" : "Spectral 3OA") << " p99 " << costs[990] << " us / 1333 us, pitch " << hz << " Hz\n";
        render(&off); for (unsigned n = 0; n < 30; ++n) render();
        check(!engine->slotPlaybackActive(0) && engine->slotPeak(0) == 0, "frame synth HOLD releases to silence");
        s.sourceMode = NeonSourceMode::Scan; s.stackCycleSeconds = .1f;
        engine->reset(); render(&on); float minimum = 1, maximum = 0;
        for (unsigned n = 0; n < 150; ++n) { render(); minimum = std::min(minimum, engine->stackPosition(0)); maximum = std::max(maximum, engine->stackPosition(0)); }
        check(maximum - minimum > .8f && engine->stackScanActive(0, settings), "frame engine held stack path moves and publishes");
        s.sourceMode = NeonSourceMode::Primary;
        s.clock = SampleNeonClock::Host; settings.transportPlaying = false;
        render(); check(engine->slotPeak(0) == 0, "frame HOST pauses without output");
        s.clock = SampleNeonClock::Free;
        engine->killAll(); render(); check(engine->slotPeak(0) == 0, "frame panic clears synthesis tail");
    }
    s.playback = SampleNeonPlayback::Spectral; s.launchPosition = .2; s.motionCycleSeconds = .1f;
    s.family[NeonFamily::SpectralAdvance] = 0; s.family[NeonFamily::SpectralPressure] = 1;
    engine->reset(); render(&on); for (unsigned n = 0; n < 20; ++n) render();
    const auto frozen = engine->voiceCursors(0)[0].sourcePositionNormalized;
    for (unsigned n = 0; n < 20; ++n) render();
    check(engine->voiceCursors(0)[0].sourcePositionNormalized == frozen, "Spectral zero advance freezes source position");
    auto pressure = on; pressure.kind = SampleNeonEventKind::Pressure; pressure.value = .5f; pressure.frameOffset = 32;
    render(&pressure); for (unsigned n = 0; n < 20; ++n) render();
    check(std::abs(engine->voiceCursors(0)[0].sourcePositionNormalized - frozen) > .05,
        "Spectral pressure advances source frames without changing sample selection");
    auto choke = on; choke.kind = SampleNeonEventKind::Choke; choke.noteId = 0; choke.frameOffset = 32;
    render(&choke);
    bool stopped = true; for (unsigned n = 32; n < 64; ++n) stopped &= out.samples[0][n] == 0;
    check(stopped, "Spectral explicit stop is sample accurate");
    auto dc = constantAsset(.25f, -.125f, 4096);
    engine->setAsset(0, &dc); s.stack = nullptr; s.start = 0; s.end = 1;
    render(&on); for (unsigned n = 0; n < 40; ++n) render();
    check(engine->slotPeak(0) < 1.e-6, "Spectral DC source does not become a synthetic window tone");
    engine->setAsset(0, asset.get()); s.stack = &stack; s.start = .1; s.end = .9;
    // Descriptor decisions have deterministic ties and never use an L+R sum.
    stack.fragmentCount = 3;
    stack.fragments[0] = {.1f, .3f, .2f, .1f, 0};
    stack.fragments[1] = {.3f, .5f, .25f, .12f, 0};
    stack.fragments[2] = {.5f, .9f, .9f, .8f, 1};
    check(neonChooseMosaic(stack, 0, 1, true, .5f) == 1, "Mosaic similarity follows closest descriptor");
    check(neonChooseMosaic(stack, 0, 2, true, .5f) == 2, "Mosaic contrast crosses layers");
    check(neonChooseMosaic(stack, 0, 2, false, .5f) == 1, "Mosaic primary scope excludes other layers");
    check(neonChooseMosaic(stack, 0, 3, true, 1) == 2 && neonChooseMosaic(stack, 0, 4, true, 1) == 2,
        "Mosaic energy/brightness targets choose appropriate fragments");
    const auto d = neonDescribeFragment(stack.layers[0], 0, .1, .9);
    check(d.energy > .5 && d.brightness < .02, "bounded descriptors recognize a sustained low tone");
    s.playback = SampleNeonPlayback::SliceSequence; s.technique = {{1, 0, 0, 1}};
    s.family[NeonFamily::MosaicMode] = 2; s.family[NeonFamily::MosaicScope] = 1;
    engine->reset(); render(&on);
    check(engine->stackWaveformLayer(0, settings) == 1, "Mosaic source waveform follows chosen stack layer");
    bool audible = false;
    for (unsigned n = 0; n < 100; ++n) { render(); audible |= engine->slotPeak(0) > .01f; }
    check(audible, "Mosaic fragments reach output through existing slice envelopes");
    auto cancelling = std::make_shared<SampleAsset>(*asset);
    for (unsigned i = 0; i < cancelling->frameCount(); ++i) cancelling->channels[1][i] = -cancelling->channels[0][i];
    check(bool(analyzeNeonWavesets(cancelling)), "Neon cycles survive perfectly cancelling stereo channels");
    std::fill(cancelling->channels[0].begin(), cancelling->channels[0].end(), 0);
    check(bool(analyzeNeonWavesets(cancelling)), "Neon cycles use active reference when channel one is silent");
}

void testSpectralColour()
{
    using namespace s3g::sample;
    using F = NeonFamily;
    for (double rate : {44100., 48000., 96000.}) {
        SampleAsset asset; asset.sampleRate = rate; asset.channelCount = 16;
        for (unsigned ch = 0; ch < 16; ++ch) {
            auto& data = asset.channels[ch]; data.resize(16384);
            for (unsigned i = 0; i < 8192; ++i) data[i] = static_cast<float>(
                (.16 * std::sin(6.283185307179586 * 8.5 * i / 1024)
                + .025 * std::sin(6.283185307179586 * 48.5 * i / 1024))
                * (ch == 15 ? 0 : ch % 2 ? -.5 : 1));
        }
        auto player = std::make_unique<NeonFramePlayer>();
        check(player->prepare(), "expanded Spectral prepares outside audio");
        NeonFamilySettings neutral; neutral[F::SpectralBlur] = 0;
        std::array<float, 64> positions {}, sourcePositions {}, velocities {};
        std::array<uint8_t, 64> triggers {};
        velocities.fill(1);
        std::array<std::array<float, 64>, 16> output {};
        std::array<float*, 16> pointers {};
        for (unsigned ch = 0; ch < 16; ++ch) pointers[ch] = output[ch].data();
        bool linked = true, bounded = true;
        const auto render = [&](const NeonFamilySettings& f, unsigned blocks, bool reset, bool changeSource = false, bool changeColour = false) {
            if (reset) player->reset();
            std::vector<float> samples; samples.reserve(blocks * 64);
            for (unsigned block = 0; block < blocks; ++block) {
                sourcePositions.fill(changeSource && block >= 64 ? .8f : .1f);
                player->render(nullptr, {&asset, 0, 1, nullptr}, 0, 0, 1,
                    changeColour && block < 64 ? neutral : f, false, 1, rate, 1, 0, false,
                    positions.data(), sourcePositions.data(), velocities.data(), triggers.data(), pointers.data(), 64);
                for (unsigned i = 0; i < 64; ++i) {
                    samples.push_back(output[0][i]);
                    for (unsigned ch = 0; ch < 16; ++ch) {
                        bounded &= std::isfinite(output[ch][i]) && std::abs(output[ch][i]) < 2;
                        linked &= std::abs(output[ch][i] - output[0][i] * (ch == 15 ? 0 : ch % 2 ? -.5f : 1.f)) < 1.e-5f;
                    }
                }
            }
            return samples;
        };
        const auto magnitude = [](const std::vector<float>& samples, double bin, unsigned first = 8192) {
            std::complex<double> sum {};
            for (unsigned i = first; i < samples.size(); ++i)
                sum += double(samples[i]) * std::polar(1., -6.283185307179586 * bin * i / 1024);
            return std::abs(sum) / (samples.size() - first);
        };
        const auto energy = [](const std::vector<float>& samples, unsigned first = 8192) {
            double sum = 0; for (unsigned i = first; i < samples.size(); ++i) sum += samples[i] * samples[i];
            return sum / (samples.size() - first);
        };
        const auto original = render(neutral, 256, true);
        const double ratio = magnitude(original, 8.5) / magnitude(original, 48.5);
        auto f = neutral; f[F::SpectralFocus] = 1;
        const auto focused = render(f, 256, true);
        f[F::SpectralFocus] = -1;
        const auto flat = render(f, 256, true);
        check(magnitude(focused, 8.5) / magnitude(focused, 48.5) > ratio * 2
            && magnitude(flat, 8.5) / magnitude(flat, 48.5) < ratio * .6, "Focus strengthens peaks / flattens quieter partials");
        check(energy(focused) < energy(original) * 1.1 && energy(focused) > energy(original) * .8,
            "Focus energy compensation prevents a loudness-only effect");
        f = neutral; f[F::SpectralTilt] = 6; const auto bright = render(f, 256, true);
        f[F::SpectralTilt] = -6; const auto dark = render(f, 256, true);
        check(magnitude(bright, 8.5) / magnitude(bright, 48.5) < ratio * .5
            && magnitude(dark, 8.5) / magnitude(dark, 48.5) > ratio * 2, "Tilt moves the spectral balance both directions");
        f = neutral; f[F::SpectralThin] = .9f;
        const auto thin = render(f, 256, true);
        check(energy(thin) < energy(original) * .5, "Thin removes deterministic bands");
        check(thin == render(f, 256, true), "Thin is repeatable across retriggers");
        f = neutral; f[F::SpectralSmear] = 1;
        const auto smeared = render(f, 256, true, true);
        const auto immediate = render(neutral, 256, true, true);
        check(energy(immediate) < 1.e-12 && energy(smeared) > energy(original) * .1,
            "Smear retains spectral energy after scanning into silence");
        check(magnitude(smeared, 8.5) > magnitude(original, 8.5) * .25,
            "Smear retains off-bin pitch while its target is silent");
        f[F::SpectralFocus] = -.8f; f[F::SpectralTilt] = 6; f[F::SpectralThin] = 1;
        const auto live = render(f, 256, true, false, true);
        check(energy(live) < energy(original) * .5, "live colour changes respond despite long Smear");
        float jump = 0; for (unsigned i = 1; i < live.size(); ++i) jump = std::max(jump, std::abs(live[i] - live[i - 1]));
        check(jump < .15f, "hop-smoothed colour changes avoid hard gain steps");
        check(linked && bounded, "expanded Spectral stays finite and preserves linked 16-channel ratios / silent channels");
        player->reset(); sourcePositions.fill(.8f);
        player->render(nullptr, {&asset, 0, 1, nullptr}, 0, 0, 1, f, false, 1, rate, 1, 0, false,
            positions.data(), sourcePositions.data(), velocities.data(), triggers.data(), pointers.data(), 64);
        check(std::all_of(output[0].begin(), output[0].end(), [](float x) { return x == 0; }),
            "reset discards Smear and never lifts a silent source");
    }
}

#include "sample_neon_cutups_checks.inc"
#include "sample_neon_poly_checks.inc"

int main()
{
    testDisplayVocabulary();
    testNotePolyphony();
    testProtocolDecode();
    testPadVelocityPairing();
    testLedDiffs();
    testLedPacketPacing();
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
    testExpandedCharacterModes();
    benchmarkCharacterFx();
    testStackPlayback();
    testStackWaveform();
    testWavesetStackScan();
    testFamilyIntegration();
    testStackShapes();
    testStackPointEditing();
    testStackPathPhase();
    testStackPerformanceOverride();
    testChopDestinations();
    testLanesPlayback();
    testPlaybackVisuals();
    testSliceEnvelopeAndRouting();
    testSliceSequenceHandoffs();
    testBufferFill();
    testFramePlaybackAndMosaic();
    testSpectralColour();
    testCutupsPlayback();
    if (failures != 0) {
        std::cerr << failures << " Sample Neon checks failed\n";
        return 1;
    }
    std::cout << "Sample Neon checks passed\n";
    return 0;
}
