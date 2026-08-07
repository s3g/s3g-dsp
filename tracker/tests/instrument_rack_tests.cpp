#include "s3g/tracker/instrument_rack.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {

int failures = 0;

void check(bool condition, const char* message)
{
    if (condition) return;
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
}

bool closeEnough(double left, double right, double tolerance = 1.0e-5)
{
    return std::abs(left - right) <= tolerance;
}

} // namespace

int main()
{
    using namespace s3g::tracker;

    check(membraneParameterCount() == kMembraneParameterCount,
        "the editor and realtime rack must share one bounded catalog");
    for (std::size_t index = 0u; index < membraneParameterCount(); ++index) {
        const auto* parameter = membraneParameter(index);
        check(parameter && parameter->parameterId != 1u
                && parameter->parameterId != 20u,
            "topology and trigger parameters must not enter the base editor");
        if (!parameter) continue;
        const auto normalized = membraneNormalizedFromNative(
            parameter->parameterId, parameter->defaultValue);
        const auto roundTrip = membraneNativeFromNormalized(
            parameter->parameterId, normalized);
        const double tolerance = parameter->control
                == MembraneControlKind::Stepped
            ? 0.5 : 1.0e-4;
        check(closeEnough(roundTrip, parameter->defaultValue, tolerance),
            "native/normalized parameter conversion must round-trip");
    }
    check(sn76489ParameterCount() == kSn76489ParameterCount,
        "the PSG editor and audio node must share one bounded catalog");
    for (std::size_t index = 0u; index < sn76489ParameterCount(); ++index) {
        const auto* parameter = sn76489Parameter(index);
        check(parameter != nullptr, "PSG parameter catalog contains a hole");
        if (!parameter) continue;
        const auto normalized = sn76489NormalizedFromNative(
            parameter->parameterId, parameter->defaultValue);
        const auto roundTrip = sn76489NativeFromNormalized(
            parameter->parameterId, normalized);
        check(closeEnough(roundTrip, parameter->defaultValue, 1.0e-3),
            "PSG native/normalized conversion must round-trip");
    }
    check(ym2151ParameterCount() == kYm2151ParameterCount,
        "the YM2151 node and device view must share one parameter catalog");
    for (std::size_t index = 0u; index < ym2151ParameterCount(); ++index) {
        const auto* parameter = ym2151Parameter(index);
        check(parameter != nullptr, "YM2151 parameter catalog contains a hole");
        if (!parameter) continue;
        const auto normalized = ym2151NormalizedFromNative(
            parameter->parameterId, parameter->defaultValue);
        check(closeEnough(ym2151NativeFromNormalized(
                parameter->parameterId, normalized),
                parameter->defaultValue, 0.5),
            "YM2151 parameter conversion must round-trip");
    }
    constexpr std::array<InstrumentKind, kDaisyDrumKindCount> daisyKinds {{
        InstrumentKind::DaisyAnalogBassDrum,
        InstrumentKind::DaisyAnalogSnareDrum,
        InstrumentKind::DaisyHiHat,
        InstrumentKind::DaisySyntheticBassDrum,
        InstrumentKind::DaisySyntheticSnareDrum,
    }};
    check(instrumentTypeCount() == kInstrumentTypeCount,
        "the toolbox must enumerate every internal and MIDI instrument type");
    for (const auto kind : daisyKinds) {
        const auto count = daisyDrumParameterCount(kind);
        check(count > 0u && count <= kDaisyDrumParameterCapacity,
            "every DaisySP drum must expose a bounded editor catalog");
        for (std::size_t index = 0u; index < count; ++index) {
            const auto* parameter = daisyDrumParameter(kind, index);
            check(parameter != nullptr,
                "a DaisySP drum parameter catalog contains a hole");
            if (!parameter) continue;
            const auto normalized = daisyDrumNormalizedFromNative(kind,
                parameter->parameterId, parameter->defaultValue);
            check(closeEnough(daisyDrumNativeFromNormalized(kind,
                    parameter->parameterId, normalized),
                    parameter->defaultValue, 1.0e-3),
                "DaisySP drum parameter conversion must round-trip");
        }
    }

    auto rack = makeDefaultInstrumentRack();
    for (std::size_t slot = 0u; slot < kMidiOutRackSlotCount; ++slot) {
        check(rack.midiRoutes[slot].kind
                    == MidiInstrumentRouteKind::VirtualSource
                && rack.midiRoutes[slot].virtualSource == slot + 1u
                && rack.midiRoutes[slot].channel == slot + 1u,
            "every MIDI rack slot should default to its matching source and channel");
    }
    check(rack.slots.size() == 5u && rack.slots[0].nodeId == 0u
            && rack.slots[4].nodeId == 4u,
        "the membrane rack must expose five stable node IDs");
    check(std::all_of(rack.slots.begin(), rack.slots.end(),
            [](const auto& slot) {
                return slot.role == MembraneInstrumentRole::Kick;
            }),
        "membrane slots should be independent copies of the convincing kick");
    check(rack.instruments.size() == kInstrumentRackSlotCount
            && activeInstrumentCount(rack) == 3u
            && rackInstrumentAt(rack, 0u)->nodeId == 0u
            && rackInstrumentAt(rack, 1u)->nodeId
                == kStereoSamplerInstrumentNode
            && rackInstrumentAt(rack, 2u)->nodeId
                == kMidiOutInstrumentNode
            && defaultRackInstrument(kSn76489InstrumentNode)->kind
                == InstrumentKind::Sn76489Psg
            && defaultRackInstrument(kMidiOutInstrumentNode)->kind
                == InstrumentKind::MidiOut
            && defaultRackInstrument(kStereoSamplerInstrumentNode)->kind
                == InstrumentKind::StereoSliceSampler
            && instrumentRoutesToInternal(InstrumentKind::StereoSliceSampler)
            && !instrumentRoutesToInternal(InstrumentKind::Sn76489Psg)
            && !instrumentRoutesToInternal(InstrumentKind::Ym2151Opm)
            && instrumentRoutesToMidi(InstrumentKind::MidiOut),
        "the default song index should contain kick, sampler, and MIDI OUT");
    std::size_t addedIndex = 0u;
    uint32_t addedNode = kInvalidInstrumentNode;
    check(canAddInstrumentInstance(rack, InstrumentKind::MembraneKick)
            && canAddInstrumentInstance(rack,
                InstrumentKind::StereoSliceSampler)
            && !canAddInstrumentInstance(rack, InstrumentKind::Sn76489Psg)
            && !canAddInstrumentInstance(rack, InstrumentKind::Ym2151Opm)
            && canAddInstrumentInstance(rack, InstrumentKind::MidiOut)
            && addInstrumentInstance(rack, InstrumentKind::MembraneKick,
                &addedIndex, &addedNode)
            && addedIndex == 3u && addedNode == 1u
            && rackIndexForNode(rack, 1u) == 3u
            && cycleActiveInstrument(rack, kMidiOutInstrumentNode, 1)
                == 1u
            && cycleActiveInstrument(rack, kInvalidInstrumentNode, 1)
                == 0u
            && cycleActiveInstrument(rack, kInvalidInstrumentNode, -1)
                == 1u,
        "adding a kick should create the next indexed instance on a free DSP node");
    std::size_t addedSamplerIndex = 0u;
    uint32_t addedSamplerNode = kInvalidInstrumentNode;
    check(addInstrumentInstance(rack, InstrumentKind::StereoSliceSampler,
            &addedSamplerIndex, &addedSamplerNode)
            && addedSamplerIndex == 4u
            && addedSamplerNode == kStereoSamplerInstrumentNode + 1u
            && rackIndexForNode(rack, addedSamplerNode)
                == addedSamplerIndex,
        "adding a sampler should allocate the next stable sampler node");
    check(addInstrumentInstance(rack, InstrumentKind::MembraneKick)
            && addInstrumentInstance(rack, InstrumentKind::MembraneKick)
            && addInstrumentInstance(rack, InstrumentKind::MembraneKick)
            && !addInstrumentInstance(rack, InstrumentKind::MembraneKick)
            && activeInstrumentCount(rack, InstrumentKind::MembraneKick)
                == kMembraneRackSlotCount,
        "the instrument list should expose capacity without pre-filling five slots");
    check(addInstrumentInstance(rack, InstrumentKind::StereoSliceSampler)
            && !addInstrumentInstance(rack,
                InstrumentKind::StereoSliceSampler)
            && activeInstrumentCount(rack,
                InstrumentKind::StereoSliceSampler)
                == kStereoSamplerRackSlotCount,
        "samplers should expose three independent indexed instances");
    for (const auto kind : daisyKinds) {
        std::size_t firstIndex = 0u;
        uint32_t firstNode = kInvalidInstrumentNode;
        check(addInstrumentInstance(rack, kind, &firstIndex, &firstNode)
                && daisyDrumKindForNode(firstNode) == kind
                && daisyDrumRackSlotIndex(firstNode) == 0u
                && applyDaisyDrumPreset(rack, firstNode, 1u)
                && daisyDrumPresetIndex(rack, firstNode) == 1u,
            "a DaisySP drum should allocate its stable node and retain a preset");
        uint32_t secondNode = kInvalidInstrumentNode;
        check(addInstrumentInstance(rack, kind, nullptr, &secondNode)
                && daisyDrumRackSlotIndex(secondNode) == 1u,
            "a second DaisySP drum instance should receive isolated DSP state");
        const auto* firstParameter = daisyDrumParameter(kind, 0u);
        const float secondValue = firstParameter
            ? daisyDrumBaseParameter(rack, secondNode,
                firstParameter->parameterId) : 0.0f;
        check(firstParameter
                && setDaisyDrumBaseParameter(rack, firstNode,
                    firstParameter->parameterId, 0.123f)
                && closeEnough(daisyDrumBaseParameter(rack, firstNode,
                    firstParameter->parameterId), 0.123)
                && closeEnough(daisyDrumBaseParameter(rack, secondNode,
                    firstParameter->parameterId), secondValue)
                && daisyDrumPresetIndex(rack, firstNode)
                    == kDaisyDrumPresetCount,
            "DaisySP drum edits must remain isolated and report CUSTOM");
        check(addInstrumentInstance(rack, kind)
                && !addInstrumentInstance(rack, kind)
                && activeInstrumentCount(rack, kind)
                    == kDaisyDrumRackSlotCount,
            "each DaisySP drum family should expose three prepared instances");
    }
    std::size_t midiIndex = 0u;
    uint32_t midiNode = kInvalidInstrumentNode;
    check(addInstrumentInstance(rack, InstrumentKind::MidiOut,
            &midiIndex, &midiNode)
            && midiNode == kMidiOutInstrumentNode + 1u,
        "MIDI OUT should support independent indexed instances");
    MidiInstrumentRoute route;
    route.kind = MidiInstrumentRouteKind::Destination;
    route.destinationId = 4242;
    route.channel = 16u;
    check(setMidiInstrumentRoute(rack, midiNode, route)
            && midiInstrumentRoute(rack, midiNode)
            && midiInstrumentRoute(rack, midiNode)->destinationId == 4242
            && midiInstrumentRoute(rack, midiNode)->channel == 16u
            && midiInstrumentRoute(rack, kMidiOutInstrumentNode)->channel
                == 1u,
        "each MIDI instrument should own an isolated endpoint and channel");
    route.kind = MidiInstrumentRouteKind::VirtualSource;
    route.virtualSource = 99u;
    route.destinationId = 4242;
    check(setMidiInstrumentRoute(rack, midiNode, route)
            && midiInstrumentRoute(rack, midiNode)->virtualSource
                == kMidiOutRackSlotCount
            && midiInstrumentRoute(rack, midiNode)->destinationId == 0,
        "virtual MIDI routes should clamp to an owned source and clear destinations");

    const float kickTune = membraneBaseParameter(rack, 0u, 3u);
    const float floorTune = membraneBaseParameter(rack, 2u, 3u);
    const float highTune = membraneBaseParameter(rack, 4u, 3u);
    check(closeEnough(highTune, floorTune)
            && closeEnough(highTune, kickTune),
        "new membrane instances should begin from the same kick patch");
    const float priorSnareClick = membraneBaseParameter(rack, 1u, 9u);
    check(setMembraneBaseParameter(rack, 0u, 9u, 0.91f),
        "a valid base parameter edit should succeed");
    check(membranePresetIndex(rack, 0u) == kMembranePresetCount,
        "a manually edited membrane patch should be reported as custom");
    check(closeEnough(membraneBaseParameter(rack, 0u, 9u), 0.91)
            && closeEnough(membraneBaseParameter(rack, 1u, 9u),
                priorSnareClick),
        "base parameter edits must remain isolated by node");
    check(!setMembraneBaseParameter(rack, 5u, 9u, 0.5f)
            && !setMembraneBaseParameter(rack, 0u, 1u, 0.5f),
        "unknown nodes and topology parameters must fail closed");
    const float secondPsgMix = sn76489BaseParameter(rack,
        kSn76489InstrumentNode + 1u, 2u);
    check(setSn76489BaseParameter(rack, kSn76489InstrumentNode, 2u, 0.25f)
            && closeEnough(sn76489BaseParameter(rack,
                kSn76489InstrumentNode, 2u), 0.25)
            && closeEnough(sn76489BaseParameter(rack,
                kSn76489InstrumentNode + 1u, 2u), secondPsgMix),
        "PSG patch edits should remain isolated by rack node");
    check(!setSn76489BaseParameter(rack, 0u, 2u, 0.5f)
            && !setSn76489BaseParameter(rack,
                kSn76489InstrumentNode, 99u, 0.5f),
        "unknown PSG parameters must fail closed");
    check(applyMembranePreset(rack, 0u, 3u)
            && membranePresetIndex(rack, 0u) == 3u
            && membranePreset(3u)
            && membraneBaseParameter(rack, 0u, 9u)
                > membraneBaseParameter(rack, 1u, 9u),
        "membrane presets should apply only to the selected instance");

    if (failures != 0) return EXIT_FAILURE;
    std::cout << "instrument rack model tests passed\n";
    return EXIT_SUCCESS;
}
