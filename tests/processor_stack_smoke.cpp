#include "s3g_processor_stack.h"
#include "s3g_processor_stack_presets.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>

namespace {

struct RenderStats {
    double energy = 0.0;
    double earlyEnergy = 0.0;
    double tailEnergy = 0.0;
    double differenceEnergy = 0.0;
    double feedbackBodyEnergy = 0.0;
    double feedbackStabEnergy = 0.0;
    float peak = 0.0f;
    float finalActivity = 0.0f;
    float sag = 0.0f;
    bool finite = true;
    bool active = false;
};

RenderStats render(s3g::ProcessorStackParams params,
    const std::vector<std::pair<uint32_t, int>>& noteOns,
    const std::vector<std::pair<uint32_t, int>>& noteOffs,
    uint32_t frames = 144000u, double sampleRate = 48000.0,
    float pressure = 0.0f, float bend = 0.0f)
{
    s3g::ProcessorStack stack;
    stack.prepare(sampleRate);
    stack.setParams(params);
    stack.setPressure(pressure);
    stack.setPitchBendSemitones(bend);
    RenderStats stats;
    size_t onIndex = 0u;
    size_t offIndex = 0u;
    float previousMono = 0.0f;
    const uint32_t earlyEnd = std::max<uint32_t>(1u, frames / 4u);
    const uint32_t tailStart = frames * 3u / 4u;
    for (uint32_t frame = 0u; frame < frames; ++frame) {
        while (onIndex < noteOns.size() && noteOns[onIndex].first == frame) {
            stack.noteOn(noteOns[onIndex].second,
                onIndex == 0u ? 0.92f : 0.74f);
            ++onIndex;
        }
        while (offIndex < noteOffs.size()
            && noteOffs[offIndex].first == frame) {
            stack.noteOff(noteOffs[offIndex].second);
            ++offIndex;
        }
        float left = 0.0f;
        float right = 0.0f;
        stack.processFrame(left, right);
        if (!std::isfinite(left) || !std::isfinite(right)) {
            stats.finite = false;
            break;
        }
        const float framePeak = std::max(std::abs(left), std::abs(right));
        const double frameEnergy = static_cast<double>(left) * left
            + static_cast<double>(right) * right;
        stats.energy += frameEnergy;
        const float mono = (left + right) * 0.5f;
        const float difference = mono - previousMono;
        stats.differenceEnergy += static_cast<double>(difference) * difference;
        previousMono = mono;
        stats.feedbackBodyEnergy += static_cast<double>(
            stack.feedbackBodyActivity()) * stack.feedbackBodyActivity();
        stats.feedbackStabEnergy += static_cast<double>(
            stack.feedbackStabActivity()) * stack.feedbackStabActivity();
        if (frame < earlyEnd) stats.earlyEnergy += frameEnergy;
        if (frame >= tailStart) stats.tailEnergy += frameEnergy;
        stats.peak = std::max(stats.peak, framePeak);
    }
    stats.finalActivity = stack.feedbackActivity();
    stats.sag = stack.sagEnvelope();
    stats.active = stack.active();
    return stats;
}

std::vector<float> renderSignature(s3g::ProcessorStackParams params)
{
    s3g::ProcessorStack stack;
    stack.prepare(48000.0);
    stack.setParams(params);
    stack.noteOn(43, 0.84f);
    std::vector<float> result(8192u);
    for (uint32_t frame = 0u; frame < result.size(); ++frame) {
        if (frame == 4096u) stack.noteOff(43);
        float left = 0.0f;
        float right = 0.0f;
        stack.processFrame(left, right);
        result[frame] = left + right * 0.37f;
    }
    return result;
}

bool near(float first, float second, float tolerance = 1.0e-6f)
{
    return std::abs(first - second) <= tolerance;
}

} // namespace

int main()
{
    s3g::ProcessorStack idle;
    idle.prepare(48000.0);
    for (uint32_t frame = 0u; frame < 4096u; ++frame) {
        float left = 1.0f;
        float right = 1.0f;
        idle.processFrame(left, right);
        if (left != 0.0f || right != 0.0f
            || !std::isfinite(left) || !std::isfinite(right)) {
            std::cerr << "idle Processor Stack was not finite silence\n";
            return 1;
        }
    }

    s3g::ProcessorStackParams invalid;
    invalid.mode = static_cast<s3g::ProcessorStackMode>(99u);
    invalid.circuit = static_cast<s3g::ProcessorStackCircuit>(99u);
    invalid.shape = -4.0f;
    invalid.wire = 3.0f;
    invalid.pick = std::numeric_limits<float>::quiet_NaN();
    invalid.glideMs = 9000.0f;
    invalid.feedback = 5.0f;
    invalid.polarity = -5.0f;
    invalid.arpPattern = static_cast<s3g::ProcessorStackArpPattern>(99u);
    invalid.scale = static_cast<s3g::ProcessorStackScale>(99u);
    invalid.arpRate = static_cast<s3g::ProcessorStackArpRate>(99u);
    invalid.arpOctaves = 99u;
    invalid.arpGate = -1.0f;
    invalid.customPatternLength = 99u;
    invalid.customPattern[0u] = -99;
    invalid.customPattern[1u] = 99;
    invalid.outputGainDb = 80.0f;
    const auto sanitized = s3g::sanitizeProcessorStackParams(invalid);
    if (sanitized.mode != s3g::ProcessorStackMode::Lead
        || sanitized.circuit != s3g::ProcessorStackCircuit::Diode
        || sanitized.shape != 0.0f || sanitized.wire != 1.0f
        || sanitized.pick != 0.72f || sanitized.glideMs != 2000.0f
        || sanitized.feedback != 1.0f || sanitized.polarity != 0.0f
        || sanitized.arpPattern != s3g::ProcessorStackArpPattern::Custom
        || sanitized.scale != s3g::ProcessorStackScale::Tritone
        || sanitized.arpRate != s3g::ProcessorStackArpRate::SixtyFourth
        || sanitized.arpOctaves != 4u || sanitized.arpGate != 0.05f
        || sanitized.customPatternLength != 8u
        || sanitized.customPattern[0u] != -8
        || sanitized.customPattern[1u] != 15
        || sanitized.outputGainDb != 6.0f) {
        std::cerr << "Processor Stack parameter sanitation failed\n";
        return 1;
    }

    s3g::ProcessorStackParams reference;
    reference.outputGainDb = -12.0f;
    const auto referenceStats = render(reference, {{ 0u, 43 }},
        {{ 24000u, 43 }});
    if (!referenceStats.finite || referenceStats.energy < 0.001
        || referenceStats.peak < 0.001f || referenceStats.peak > 1.0f) {
        std::cerr << "Processor Stack reference render failed: energy="
                  << referenceStats.energy << " peak=" << referenceStats.peak
                  << " finite=" << referenceStats.finite << "\n";
        return 1;
    }

    auto dry = reference;
    dry.feedback = 0.0f;
    dry.spill = 0.0f;
    dry.wire = 0.0f;
    const auto dryStats = render(dry, {{ 0u, 43 }}, {{ 12000u, 43 }});
    auto regenerated = dry;
    regenerated.feedback = 0.92f;
    regenerated.spill = 0.82f;
    regenerated.cone = 0.88f;
    regenerated.proximity = 0.76f;
    const auto regeneratedStats = render(regenerated,
        {{ 0u, 43 }}, {{ 12000u, 43 }});
    if (!regeneratedStats.finite
        || regeneratedStats.tailEnergy <= dryStats.tailEnergy * 1.5
        || regeneratedStats.peak > 1.0f) {
        std::cerr << "speaker feedback did not create a bounded longer tail: "
                  << dryStats.tailEnergy << " -> "
                  << regeneratedStats.tailEnergy
                  << " peak=" << regeneratedStats.peak << "\n";
        return 1;
    }

    auto bodyFeedback = reference;
    bodyFeedback.mode = s3g::ProcessorStackMode::Lead;
    bodyFeedback.feedback = 0.86f;
    bodyFeedback.proximity = 0.82f;
    bodyFeedback.harmonic = 0.74f;
    bodyFeedback.tracking = 0.94f;
    bodyFeedback.pierce = 0.0f;
    bodyFeedback.selfListen = 0.0f;
    const auto bodyFeedbackStats = render(bodyFeedback,
        {{ 0u, 52 }}, {}, 96000u);
    auto piercingFeedback = bodyFeedback;
    piercingFeedback.pierce = 1.0f;
    piercingFeedback.selfListen = 1.0f;
    const auto piercingFeedbackStats = render(piercingFeedback,
        {{ 0u, 52 }}, {}, 96000u);
    const double bodyStabRatio = bodyFeedbackStats.feedbackStabEnergy
        / std::max(1.0e-12, bodyFeedbackStats.feedbackBodyEnergy);
    const double piercingStabRatio = piercingFeedbackStats.feedbackStabEnergy
        / std::max(1.0e-12, piercingFeedbackStats.feedbackBodyEnergy);
    const double bodyBrightness = bodyFeedbackStats.differenceEnergy
        / std::max(1.0e-12, bodyFeedbackStats.energy);
    const double piercingBrightness = piercingFeedbackStats.differenceEnergy
        / std::max(1.0e-12, piercingFeedbackStats.energy);
    if (!piercingFeedbackStats.finite
        || piercingStabRatio <= bodyStabRatio * 1.20
        || piercingBrightness <= bodyBrightness * 1.02) {
        std::cerr << "self-listening stab lane did not overtake body mud: ratio="
                  << bodyStabRatio << " -> " << piercingStabRatio
                  << " brightness=" << bodyBrightness << " -> "
                  << piercingBrightness << "\n";
        return 1;
    }

    auto noString = reference;
    noString.wire = 0.0f;
    noString.feedback = 0.0f;
    noString.spill = 0.0f;
    noString.damping = 0.10f;
    const auto noStringStats = render(noString,
        {{ 0u, 45 }}, {{ 3000u, 45 }}, 48000u);
    auto pluckedString = noString;
    pluckedString.wire = 1.0f;
    const auto pluckedStringStats = render(pluckedString,
        {{ 0u, 45 }}, {{ 3000u, 45 }}, 48000u);
    if (!pluckedStringStats.finite
        || pluckedStringStats.tailEnergy
            <= noStringStats.tailEnergy + 1.0e-8) {
        std::cerr << "plucked-string waveguide did not outlast the pick packet: "
                  << noStringStats.tailEnergy << " -> "
                  << pluckedStringStats.tailEnergy << "\n";
        return 1;
    }
    auto drain = regenerated;
    drain.feedback = 0.78f;
    drain.spill = 0.58f;
    const auto drainStats = render(drain, {{ 0u, 43 }},
        {{ 12000u, 43 }}, 768000u);
    if (!drainStats.finite || drainStats.active
        || drainStats.finalActivity > 1.0e-5f
        || drainStats.tailEnergy > 1.0e-5) {
        std::cerr << "governed feedback did not drain to silence: active="
                  << drainStats.active << " activity="
                  << drainStats.finalActivity << " tail="
                  << drainStats.tailEnergy << "\n";
        return 1;
    }

    auto power = reference;
    power.mode = s3g::ProcessorStackMode::Power;
    power.shape = 0.76f;
    const auto powerStats = render(power, {{ 0u, 40 }}, {{ 30000u, 40 }},
        96000u);
    auto lead = power;
    lead.mode = s3g::ProcessorStackMode::Lead;
    const auto leadStats = render(lead, {{ 0u, 40 }}, {{ 30000u, 40 }},
        96000u);
    if (!powerStats.finite || !leadStats.finite
        || std::abs(powerStats.energy - leadStats.energy)
            < std::max(0.001, leadStats.energy * 0.01)
        || std::abs(powerStats.sag - leadStats.sag) < 1.0e-4f) {
        std::cerr << "POWER voicing did not audibly load the shared stack: "
                  << powerStats.energy << "/" << leadStats.energy
                  << " sag=" << powerStats.sag << "/" << leadStats.sag
                  << "\n";
        return 1;
    }

    auto hand = reference;
    hand.mode = s3g::ProcessorStackMode::Hand;
    const auto handStats = render(hand,
        {{ 0u, 40 }, { 3200u, 47 }, { 6400u, 52 }},
        {{ 26000u, 40 }, { 28000u, 47 }, { 30000u, 52 }}, 96000u);
    if (!handStats.finite || handStats.energy < 0.001
        || handStats.peak > 1.0f) {
        std::cerr << "HAND voicing was not bounded and audible\n";
        return 1;
    }

    auto straight = reference;
    straight.mode = s3g::ProcessorStackMode::Lead;
    straight.crooked = 0.0f;
    auto crooked = straight;
    crooked.crooked = 1.0f;
    const auto straightStats = render(straight,
        {{ 0u, 48 }, { 6000u, 49 }, { 12000u, 55 }, { 18000u, 43 }},
        {{ 5800u, 48 }, { 11800u, 49 }, { 17800u, 55 }, { 30000u, 43 }},
        72000u);
    const auto crookedStats = render(crooked,
        {{ 0u, 48 }, { 6000u, 49 }, { 12000u, 55 }, { 18000u, 43 }},
        {{ 5800u, 48 }, { 11800u, 49 }, { 17800u, 55 }, { 30000u, 43 }},
        72000u);
    if (!straightStats.finite || !crookedStats.finite
        || std::abs(straightStats.energy - crookedStats.energy)
            < std::max(0.001, straightStats.energy * 0.01)) {
        std::cerr << "CROOKED interval response was not expressed\n";
        return 1;
    }

    s3g::ProcessorStack arpeggiator;
    arpeggiator.prepare(48000.0);
    auto arpParams = reference;
    arpParams.mode = s3g::ProcessorStackMode::Lead;
    arpParams.arpPattern = s3g::ProcessorStackArpPattern::Up;
    arpParams.scale = s3g::ProcessorStackScale::Phrygian;
    arpParams.arpRate = s3g::ProcessorStackArpRate::Sixteenth;
    arpParams.arpOctaves = 2u;
    arpParams.arpGate = 0.5f;
    arpeggiator.setParams(arpParams);
    arpeggiator.setTempoBpm(120.0f);
    arpeggiator.noteOn(40, 0.9f);
    if (arpeggiator.arpCurrentNote() != 40
        || arpeggiator.arpStepCount() != 1u) {
        std::cerr << "arpeggiator did not attack its scale root immediately\n";
        return 1;
    }
    for (uint32_t frame = 0u; frame < 6000u; ++frame) {
        float left = 0.0f;
        float right = 0.0f;
        arpeggiator.processFrame(left, right);
    }
    if (arpeggiator.arpCurrentNote() != 41
        || arpeggiator.arpStepCount() != 2u) {
        std::cerr << "tempo-synced Phrygian rule missed its flat second\n";
        return 1;
    }
    for (uint32_t frame = 0u; frame < 6000u; ++frame) {
        float left = 0.0f;
        float right = 0.0f;
        arpeggiator.processFrame(left, right);
    }
    if (arpeggiator.arpCurrentNote() != 43
        || arpeggiator.arpStepCount() != 3u) {
        std::cerr << "tempo-synced Phrygian rule missed its minor third\n";
        return 1;
    }

    s3g::ProcessorStack customArp;
    customArp.prepare(48000.0);
    auto customParams = reference;
    customParams.mode = s3g::ProcessorStackMode::Lead;
    customParams.wire = 0.82f;
    customParams.feedback = 0.0f;
    customParams.crooked = 1.0f;
    customParams.outputGainDb = -18.0f;
    customParams.arpPattern = s3g::ProcessorStackArpPattern::Custom;
    customParams.scale = s3g::ProcessorStackScale::Phrygian;
    customParams.arpRate = s3g::ProcessorStackArpRate::Sixteenth;
    customParams.arpGate = 0.42f;
    customParams.customPatternLength = 4u;
    customParams.customPattern = {{ 0, 3, -1, 6, 0, 0, 0, 0 }};
    customArp.setParams(customParams);
    customArp.setTempoBpm(120.0f);
    customArp.noteOn(40, 0.9f);
    const std::array<int, 4u> expectedCustomNotes {{ 40, 45, 38, 50 }};
    float previousLeft = 0.0f;
    float previousRight = 0.0f;
    float maximumBoundaryDelta = 0.0f;
    for (uint32_t step = 0u; step < expectedCustomNotes.size(); ++step) {
        if (customArp.arpCurrentNote() != expectedCustomNotes[step]) {
            std::cerr << "custom scale-degree pattern produced note "
                      << customArp.arpCurrentNote() << " instead of "
                      << expectedCustomNotes[step] << " at step " << step
                      << "\n";
            return 1;
        }
        for (uint32_t frame = 0u; frame < 6000u; ++frame) {
            float left = 0.0f;
            float right = 0.0f;
            customArp.processFrame(left, right);
            if ((frame < 96u && step > 0u) || frame >= 5904u) {
                maximumBoundaryDelta = std::max(maximumBoundaryDelta,
                    std::max(std::abs(left - previousLeft),
                        std::abs(right - previousRight)));
            }
            previousLeft = left;
            previousRight = right;
        }
    }
    if (maximumBoundaryDelta > 0.35f) {
        std::cerr << "custom arpeggiator boundary was click-like: delta="
                  << maximumBoundaryDelta << "\n";
        return 1;
    }

    for (uint32_t circuit = 0u;
         circuit < s3g::kProcessorStackCircuitCount; ++circuit) {
        auto circuitParams = reference;
        circuitParams.circuit = static_cast<s3g::ProcessorStackCircuit>(circuit);
        circuitParams.bite = 0.78f;
        const auto circuitStats = render(circuitParams,
            {{ 0u, 50 }}, {{ 10000u, 50 }}, 36000u);
        if (!circuitStats.finite || circuitStats.energy < 1.0e-7
            || circuitStats.peak > 1.0f) {
            std::cerr << "pedal circuit " << circuit
                      << " failed: energy=" << circuitStats.energy
                      << " peak=" << circuitStats.peak << "\n";
            return 1;
        }
    }

    for (uint32_t preset = 0u;
         preset < s3g::kProcessorStackFactoryPresetCount; ++preset) {
        const auto presetParams = s3g::processorStackFactoryPreset(preset);
        if (s3g::processorStackFactoryPresetIndex(presetParams)
            != static_cast<int>(preset)) {
            std::cerr << "factory preset " << preset
                      << " did not round-trip through preset matching\n";
            return 1;
        }
        const auto presetStats = render(presetParams,
            {{ 0u, 43 }}, {{ 12000u, 43 }}, 36000u);
        if (!presetStats.finite || presetStats.energy < 1.0e-7
            || presetStats.peak > 1.0f) {
            std::cerr << "factory preset " << preset
                      << " failed: energy=" << presetStats.energy
                      << " peak=" << presetStats.peak << "\n";
            return 1;
        }
    }

    const auto firstSignature = renderSignature(reference);
    const auto secondSignature = renderSignature(reference);
    if (firstSignature != secondSignature) {
        std::cerr << "Processor Stack reset was not deterministic\n";
        return 1;
    }

    for (const double sampleRate : { 8000.0, 44100.0, 96000.0, 192000.0,
                                     768000.0 }) {
        auto stress = reference;
        stress.feedback = 1.0f;
        stress.proximity = 1.0f;
        stress.cone = 1.0f;
        stress.stack = 1.0f;
        stress.bite = 1.0f;
        stress.chaos = 1.0f;
        stress.outputGainDb = 6.0f;
        const uint32_t frames = static_cast<uint32_t>(sampleRate * 0.24);
        const auto stressStats = render(stress, {{ 0u, 24 }},
            {{ frames / 2u, 24 }}, frames, sampleRate, 1.0f, 12.0f);
        if (!stressStats.finite || stressStats.peak > 1.0f) {
            std::cerr << "Processor Stack stress failed at " << sampleRate
                      << " Hz, peak=" << stressStats.peak << "\n";
            return 1;
        }
    }

    s3g::ProcessorStack noteMemory;
    noteMemory.prepare(48000.0);
    noteMemory.noteOn(40, 0.8f);
    noteMemory.noteOn(47, 0.7f);
    if (noteMemory.heldNoteCount() != 2u) {
        std::cerr << "held-note memory did not retain two keys\n";
        return 1;
    }
    noteMemory.noteOff(47);
    if (noteMemory.heldNoteCount() != 1u) {
        std::cerr << "held-note fallback did not restore the prior key\n";
        return 1;
    }
    noteMemory.allNotesOff();
    if (noteMemory.heldNoteCount() != 0u) {
        std::cerr << "all-notes-off did not clear held-note memory\n";
        return 1;
    }

    if (!near(s3g::sanitizeProcessorStackParams(reference).outputGainDb,
            reference.outputGainDb)) {
        std::cerr << "valid Processor Stack parameters changed during sanitation\n";
        return 1;
    }

    std::cout << "Processor Stack smoke test passed\n";
    return 0;
}
