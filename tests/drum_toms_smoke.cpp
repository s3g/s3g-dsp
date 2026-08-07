#include "s3g_drum_toms.h"
#include "s3g_drum_toms_presets.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

struct Render {
    std::vector<float> left;
    std::vector<float> right;
};

Render render(const s3g::DrumTomsParams& params, s3g::DrumTomSlot slot,
    s3g::DrumTomArticulation articulation, int midiNote,
    float velocity = 1.0f, double seconds = 0.45,
    double sampleRate = 48000.0)
{
    s3g::DrumToms toms;
    toms.prepare(sampleRate);
    toms.setParams(params);
    toms.reset();
    toms.trigger(slot, articulation, velocity, midiNote);
    Render result {
        std::vector<float>(static_cast<size_t>(sampleRate * seconds)),
        std::vector<float>(static_cast<size_t>(sampleRate * seconds)),
    };
    toms.processBlock(result.left.data(), result.right.data(),
        static_cast<uint32_t>(result.left.size()));
    return result;
}

std::array<float, 26u> paramVector(const s3g::DrumTomsParams& p)
{
    return {{
        p.lowTuneHz, p.noteTracking, p.midTuneHz, p.highTuneHz,
        p.pitchDropSemitones, p.pitchSweepMs, p.shellSpread, p.body,
        p.ring, p.bodyDecaySeconds, p.decaySpread, p.punch, p.rimLevel,
        p.rimCharacter, p.rimDecaySeconds, p.stickTone,
        p.character.drive, p.character.bias, p.character.compression,
        p.character.sampleRateReduction, p.character.bitDepthReduction,
        p.character.reconstruction, p.character.tone, p.stereoWidth,
        p.velocitySensitivity, p.outputGainDb,
    }};
}

double energy(const std::vector<float>& signal, uint32_t begin = 0u)
{
    begin = std::min<uint32_t>(begin,
        static_cast<uint32_t>(signal.size()));
    double result = 0.0;
    for (uint32_t frame = begin; frame < signal.size(); ++frame) {
        result += static_cast<double>(signal[frame]) * signal[frame];
    }
    return result;
}

double differenceEnergy(const Render& rendered)
{
    double result = 0.0;
    for (uint32_t frame = 0u; frame < rendered.left.size(); ++frame) {
        const double difference = static_cast<double>(rendered.left[frame])
            - rendered.right[frame];
        result += difference * difference;
    }
    return result;
}

bool sanitationAndResolvedPitchProbe()
{
    s3g::DrumToms toms;
    toms.prepare(std::numeric_limits<double>::quiet_NaN());
    s3g::DrumTomsParams invalid;
    invalid.lowTuneHz = std::numeric_limits<float>::infinity();
    invalid.noteTracking = -4.0f;
    invalid.midTuneHz = -9.0f;
    invalid.highTuneHz = std::numeric_limits<float>::quiet_NaN();
    invalid.pitchDropSemitones = 99.0f;
    invalid.pitchSweepMs = -2.0f;
    invalid.bodyDecaySeconds = 8.0f;
    invalid.decaySpread = -5.0f;
    invalid.rimDecaySeconds = 4.0f;
    invalid.outputGainDb = 90.0f;
    toms.setParams(invalid);
    const auto p = toms.params();
    if (p.lowTuneHz != 82.0f || p.noteTracking != 0.0f
        || p.midTuneHz != 55.0f || p.highTuneHz != 182.0f
        || p.pitchDropSemitones != 30.0f || p.pitchSweepMs != 1.0f
        || p.bodyDecaySeconds != 3.0f || p.decaySpread != -1.0f
        || p.rimDecaySeconds != 0.30f || p.outputGainDb != 12.0f) {
        std::cerr << "toms sanitation failed\n";
        return false;
    }

    s3g::DrumTomsParams tuned;
    tuned.noteTracking = 1.0f;
    const auto low = s3g::drumTomsResolvedVoiceSettings(tuned,
        s3g::DrumTomSlot::Low, 45);
    const auto mid = s3g::drumTomsResolvedVoiceSettings(tuned,
        s3g::DrumTomSlot::Mid, 48);
    const auto high = s3g::drumTomsResolvedVoiceSettings(tuned,
        s3g::DrumTomSlot::High, 50);
    const auto octave = s3g::drumTomsResolvedVoiceSettings(tuned,
        s3g::DrumTomSlot::Low, 57);
    if (!(low.frequencyHz < mid.frequencyHz
            && mid.frequencyHz < high.frequencyHz)
        || std::fabs(octave.frequencyHz / low.frequencyHz - 2.0f)
            > 1.0e-5f) {
        std::cerr << "toms resolved tuning/tracking failed\n";
        return false;
    }

    toms.reset();
    for (uint32_t frame = 0u; frame < 4096u; ++frame) {
        float left = 1.0f;
        float right = -1.0f;
        toms.processFrame(left, right);
        if (left != 0.0f || right != 0.0f) {
            std::cerr << "untriggered toms were not silent\n";
            return false;
        }
    }
    s3g::DrumTomsParams silent;
    silent.velocitySensitivity = 1.0f;
    toms.setParams(silent);
    toms.trigger(s3g::DrumTomSlot::Mid,
        s3g::DrumTomArticulation::Head, 0.0f, 48);
    if (toms.active()) {
        std::cerr << "zero velocity activated toms\n";
        return false;
    }
    return true;
}

bool articulationMidiAndStereoProbe()
{
    const s3g::DrumTomsParams defaults;
    const auto head = render(defaults, s3g::DrumTomSlot::Mid,
        s3g::DrumTomArticulation::Head, 48, 0.9f, 0.45);
    const auto rim = render(defaults, s3g::DrumTomSlot::Mid,
        s3g::DrumTomArticulation::RimStick, 48, 0.9f, 0.30);
    if (!(energy(head.left) > 1.0e-4)
        || !(energy(rim.left) > 1.0e-4)
        || !(energy(rim.left, 7200u) < energy(head.left, 7200u) * 0.18
            + 1.0e-8)) {
        std::cerr << "toms head/rim articulation failed\n";
        return false;
    }

    const auto rimOctave = render(defaults, s3g::DrumTomSlot::Mid,
        s3g::DrumTomArticulation::RimStick, 60, 0.9f, 0.30);
    const auto rimSideStick = render(defaults, s3g::DrumTomSlot::Mid,
        s3g::DrumTomArticulation::RimStick, 37, 0.9f, 0.30);
    if (rim.left != rimOctave.left || rim.right != rimOctave.right
        || rim.left != rimSideStick.left
        || rim.right != rimSideStick.right) {
        std::cerr << "toms rim key normalization failed\n";
        return false;
    }

    s3g::DrumToms direct;
    direct.prepare(48000.0);
    constexpr std::array<int, 9u> mappedNotes {{
        37, 45, 47, 48, 50, 57, 59, 60, 62,
    }};
    for (int note : mappedNotes) {
        direct.reset();
        if (!direct.triggerMidi(0.8f, note) || !direct.active()) {
            std::cerr << "toms direct MIDI map failed at " << note << "\n";
            return false;
        }
    }
    direct.reset();
    if (direct.triggerMidi(0.8f, 36) || direct.active()) {
        std::cerr << "toms accepted an undocumented direct MIDI key\n";
        return false;
    }

    s3g::DrumTomsParams mono = defaults;
    mono.stereoWidth = 0.0f;
    for (auto slot : { s3g::DrumTomSlot::Low, s3g::DrumTomSlot::Mid,
            s3g::DrumTomSlot::High }) {
        const int note = s3g::drumTomSlotCanonicalMidiNote(slot);
        const auto centered = render(mono, slot,
            s3g::DrumTomArticulation::Head, note);
        if (centered.left != centered.right) {
            std::cerr << "toms width zero was not exact mono\n";
            return false;
        }
    }
    mono.stereoWidth = 1.0f;
    const auto wide = render(mono, s3g::DrumTomSlot::High,
        s3g::DrumTomArticulation::RimStick, 50);
    if (!(differenceEnergy(wide)
            > (energy(wide.left) + energy(wide.right)) * 1.0e-6)) {
        std::cerr << "toms width did not expose stereo detail\n";
        return false;
    }
    return true;
}

bool latchResetAndLifecycleProbe()
{
    s3g::DrumTomsParams first;
    first.bodyDecaySeconds = 0.82f;
    first.ring = 0.64f;
    first.decaySpread = 0.44f;
    s3g::DrumToms reference;
    s3g::DrumToms edited;
    reference.prepare(48000.0);
    edited.prepare(48000.0);
    reference.setParams(first);
    edited.setParams(first);
    reference.trigger(s3g::DrumTomSlot::Low,
        s3g::DrumTomArticulation::Head, 0.84f, 45);
    edited.trigger(s3g::DrumTomSlot::Low,
        s3g::DrumTomArticulation::Head, 0.84f, 45);
    for (uint32_t frame = 0u; frame < 512u; ++frame) {
        float a = 0.0f, b = 0.0f, c = 0.0f, d = 0.0f;
        reference.processFrame(a, b);
        edited.processFrame(c, d);
    }
    auto second = first;
    second.lowTuneHz = 170.0f;
    second.bodyDecaySeconds = 0.04f;
    second.decaySpread = -0.9f;
    second.ring = 0.0f;
    second.rimCharacter = 1.0f;
    edited.setParams(second);
    for (uint32_t frame = 0u; frame < 8192u; ++frame) {
        float a = 0.0f, b = 0.0f, c = 0.0f, d = 0.0f;
        reference.processFrame(a, b);
        edited.processFrame(c, d);
        if (a != c || b != d) {
            std::cerr << "toms per-hit controls were not latched\n";
            return false;
        }
    }

    s3g::DrumToms deterministic;
    deterministic.prepare(48000.0);
    deterministic.setParams(first);
    const auto capture = [&]() {
        std::array<float, 4096u> signal {};
        deterministic.trigger(s3g::DrumTomSlot::High,
            s3g::DrumTomArticulation::RimStick, 0.77f, 50);
        for (float& sample : signal) {
            float right = 0.0f;
            deterministic.processFrame(sample, right);
            sample += right * 0.31f;
        }
        return signal;
    };
    const auto before = capture();
    deterministic.reset();
    const auto after = capture();
    if (before != after) {
        std::cerr << "toms reset was not deterministic\n";
        return false;
    }

    deterministic.reset();
    for (uint32_t hit = 0u; hit < 80u; ++hit) {
        const auto slot = static_cast<s3g::DrumTomSlot>(hit % 3u);
        deterministic.trigger(slot,
            (hit & 1u) != 0u ? s3g::DrumTomArticulation::RimStick
                             : s3g::DrumTomArticulation::Head,
            0.5f + static_cast<float>(hit % 5u) * 0.1f,
            s3g::drumTomSlotCanonicalMidiNote(slot));
    }
    for (uint32_t frame = 0u; frame < 500000u; ++frame) {
        float left = 0.0f;
        float right = 0.0f;
        deterministic.processFrame(left, right);
        if (!std::isfinite(left) || !std::isfinite(right)
            || std::abs(left) > 4.0f || std::abs(right) > 4.0f) {
            std::cerr << "toms overload/lifecycle safety failed\n";
            return false;
        }
    }
    if (deterministic.active()) {
        std::cerr << "toms did not finish bounded lifecycle\n";
        return false;
    }
    return true;
}

bool presetsAndRandomProbe()
{
    std::vector<std::string> names;
    for (uint32_t index = 0u; index < s3g::kDrumTomsFactoryPresetCount;
         ++index) {
        const auto& info = s3g::drumTomsFactoryPresetInfo(index);
        const auto preset = s3g::drumTomsFactoryPreset(index);
        if (!info.name || !info.description || info.name[0] == '\0'
            || std::find(names.begin(), names.end(), info.name) != names.end()
            || s3g::drumTomsFactoryPresetIndex(preset)
                != static_cast<int32_t>(index)) {
            std::cerr << "toms factory preset contract failed at "
                      << index << "\n";
            return false;
        }
        names.emplace_back(info.name);
        for (auto slot : { s3g::DrumTomSlot::Low, s3g::DrumTomSlot::Mid,
                s3g::DrumTomSlot::High }) {
            const auto head = render(preset, slot,
                s3g::DrumTomArticulation::Head,
                s3g::drumTomSlotCanonicalMidiNote(slot), 0.9f, 0.18);
            if (!(energy(head.left) > 1.0e-5)) {
                std::cerr << "toms factory preset voice was silent\n";
                return false;
            }
        }
    }
    auto edited = s3g::drumTomsFactoryPreset(0u);
    edited.lowTuneHz += 0.1f;
    if (s3g::drumTomsFactoryPresetIndex(edited) != -1) return false;
    edited = s3g::drumTomsFactoryPreset(0u);
    edited.outputGainDb += 2.0f;
    if (s3g::drumTomsFactoryPresetIndex(edited) != 0) return false;

    s3g::DrumTomsParams current;
    current.noteTracking = 0.371f;
    current.velocitySensitivity = 0.427f;
    current.outputGainDb = -15.0f;
    s3g::DrumRandom firstRandom(0x731fe29bu);
    s3g::DrumRandom secondRandom(0x731fe29bu);
    if (paramVector(s3g::drumTomsSafeRandomParams(current, firstRandom))
        != paramVector(s3g::drumTomsSafeRandomParams(
            current, secondRandom))) {
        std::cerr << "toms RANDOM was not deterministic\n";
        return false;
    }

    uint32_t state = 0x9e3779b9u;
    std::array<float, 26u> previous {};
    uint32_t distinct = 0u;
    for (uint32_t iteration = 0u; iteration < 256u; ++iteration) {
        const auto p = s3g::drumTomsSafeRandomParams(current, state);
        const auto values = paramVector(p);
        if (iteration > 0u && values != previous) ++distinct;
        previous = values;
        if (p.noteTracking != current.noteTracking
            || p.velocitySensitivity != current.velocitySensitivity
            || p.outputGainDb != current.outputGainDb
            || !(p.lowTuneHz < p.midTuneHz && p.midTuneHz < p.highTuneHz)
            || p.lowTuneHz < 40.0f || p.highTuneHz > 420.0f
            || p.pitchDropSemitones < 0.0f
            || p.pitchDropSemitones > 8.0f
            || p.bodyDecaySeconds < 0.055f
            || p.bodyDecaySeconds > 1.05f
            || p.rimDecaySeconds < 0.034f
            || p.rimDecaySeconds > 0.105f
            || p.stereoWidth < 0.0f || p.stereoWidth > 0.92f
            || !std::all_of(values.begin(), values.end(),
                [](float value) { return std::isfinite(value); })) {
            std::cerr << "toms RANDOM range/ownership failed at "
                      << iteration << "\n";
            return false;
        }
        if (iteration < 24u) {
            const auto slot = static_cast<s3g::DrumTomSlot>(iteration % 3u);
            for (auto articulation : { s3g::DrumTomArticulation::Head,
                    s3g::DrumTomArticulation::RimStick }) {
                const auto rendered = render(p, slot, articulation,
                    s3g::drumTomSlotCanonicalMidiNote(slot), 0.9f, 0.22);
                if (!(energy(rendered.left) > 1.0e-6)
                    || !std::all_of(rendered.left.begin(),
                        rendered.left.end(), [](float sample) {
                            return std::isfinite(sample)
                                && std::abs(sample) < 2.0f;
                        })) {
                    std::cerr << "toms RANDOM render failed\n";
                    return false;
                }
            }
        }
    }
    if (distinct != 255u) {
        std::cerr << "toms RANDOM lacked diversity\n";
        return false;
    }
    return true;
}

} // namespace

int main()
{
    if (!sanitationAndResolvedPitchProbe()
        || !articulationMidiAndStereoProbe()
        || !latchResetAndLifecycleProbe()
        || !presetsAndRandomProbe()) {
        return 1;
    }
    std::cout << "drum toms smoke passed\n";
    return 0;
}
