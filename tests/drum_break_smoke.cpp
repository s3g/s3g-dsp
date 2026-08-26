#include "s3g_drum_break.h"
#include "s3g_drum_break_presets.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>

namespace {

struct Render {
    std::vector<float> left;
    std::vector<float> right;
};

Render render(const s3g::DrumBreakParams& params,
    s3g::DrumBreakArticulation articulation, float velocity = 1.0f,
    int note = -1, double sampleRate = 48000.0, double seconds = 2.5)
{
    s3g::DrumBreak instrument;
    instrument.prepare(sampleRate);
    instrument.setParams(params);
    instrument.reset();
    instrument.trigger(articulation, velocity, note);
    const uint32_t frames = static_cast<uint32_t>(sampleRate * seconds);
    Render result { std::vector<float>(frames), std::vector<float>(frames) };
    instrument.processBlock(result.left.data(), result.right.data(), frames);
    return result;
}

double energy(const std::vector<float>& signal, uint32_t begin = 0u,
    uint32_t end = std::numeric_limits<uint32_t>::max())
{
    end = std::min<uint32_t>(end, static_cast<uint32_t>(signal.size()));
    begin = std::min(begin, end);
    double sum = 0.0;
    for (uint32_t i = begin; i < end; ++i) {
        const double sample = signal[i];
        sum += sample * sample;
    }
    return sum;
}

double differenceEnergy(const std::vector<float>& signal)
{
    double sum = 0.0;
    for (uint32_t i = 1u; i < signal.size(); ++i) {
        const double delta = static_cast<double>(signal[i]) - signal[i - 1u];
        sum += delta * delta;
    }
    return sum;
}

double stereoDifference(const Render& result)
{
    double sum = 0.0;
    for (uint32_t i = 0u; i < result.left.size(); ++i) {
        const double delta = static_cast<double>(result.left[i])
            - result.right[i];
        sum += delta * delta;
    }
    return sum;
}

bool allFiniteAndBounded(const Render& result)
{
    for (uint32_t i = 0u; i < result.left.size(); ++i) {
        if (!std::isfinite(result.left[i]) || !std::isfinite(result.right[i])
            || std::abs(result.left[i]) > 3.0f
            || std::abs(result.right[i]) > 3.0f) return false;
    }
    return true;
}

bool safetyAndSilenceProbe()
{
    s3g::DrumBreak instrument;
    instrument.prepare(std::numeric_limits<double>::quiet_NaN());
    s3g::DrumBreakParams invalid;
    invalid.lowTuneHz = std::numeric_limits<float>::infinity();
    invalid.noteTracking = -1.0f;
    invalid.lowDropSemitones = 100.0f;
    invalid.lowDecaySeconds = -1.0f;
    invalid.midTuneHz = 900.0f;
    invalid.midCrack = 8.0f;
    invalid.highDecaySeconds = std::numeric_limits<float>::quiet_NaN();
    invalid.tomTuneHz = -100.0f;
    invalid.tomDecaySeconds = 99.0f;
    invalid.kickLevelDb = 99.0f;
    invalid.kickBandHz = -1.0f;
    invalid.snareLevelDb = -99.0f;
    invalid.snareBandHz = 99999.0f;
    invalid.tomLevelDb = 99.0f;
    invalid.tomBandHz = -1.0f;
    invalid.hiHatLevelDb = -99.0f;
    invalid.hiHatBandHz = 99999.0f;
    invalid.character.bias = 7.0f;
    invalid.outputGainDb = 90.0f;
    instrument.setParams(invalid);
    const auto p = instrument.params();
    if (p.lowTuneHz != 52.0f || p.noteTracking != 0.0f
        || p.lowDropSemitones != 42.0f || p.lowDecaySeconds != 0.05f
        || p.midTuneHz != 380.0f || p.midCrack != 1.0f
        || p.highDecaySeconds != 0.12f || p.character.bias != 1.0f
        || p.tomTuneHz != 58.0f || p.tomDecaySeconds != 2.4f
        || p.kickLevelDb != 12.0f || p.kickBandHz != 55.0f
        || p.snareLevelDb != -24.0f || p.snareBandHz != 6000.0f
        || p.tomLevelDb != 12.0f || p.tomBandHz != 80.0f
        || p.hiHatLevelDb != -24.0f || p.hiHatBandHz != 14000.0f
        || p.outputGainDb != 12.0f) {
        std::cerr << "break parameter sanitation failed\n";
        return false;
    }
    instrument.reset();
    for (uint32_t i = 0u; i < 4096u; ++i) {
        float left = 1.0f;
        float right = 1.0f;
        instrument.processFrame(left, right);
        if (left != 0.0f || right != 0.0f) {
            std::cerr << "break emitted output before a trigger\n";
            return false;
        }
    }
    return !instrument.active();
}

bool articulationProbe()
{
    const auto p = s3g::drumBreakFactoryPreset(0u);
    const Render kick = render(p, s3g::DrumBreakArticulation::Kick);
    const Render snare = render(p, s3g::DrumBreakArticulation::Snare);
    const Render tom = render(p, s3g::DrumBreakArticulation::Tom);
    const Render hat = render(p, s3g::DrumBreakArticulation::HiHat);
    if (!allFiniteAndBounded(kick) || !allFiniteAndBounded(snare)
        || !allFiniteAndBounded(tom) || !allFiniteAndBounded(hat)) {
        std::cerr << "break articulation render was unsafe\n";
        return false;
    }
    const double kickEnergy = energy(kick.left);
    const double snareEnergy = energy(snare.left);
    const double tomEnergy = energy(tom.left);
    const double hatEnergy = energy(hat.left);
    if (kickEnergy < 1.0e-4 || snareEnergy < 1.0e-4
        || tomEnergy < 1.0e-4 || hatEnergy < 1.0e-4) {
        std::cerr << "one or more break articulations were silent\n";
        return false;
    }
    const double kickBrightness = differenceEnergy(kick.left) / kickEnergy;
    const double snareBrightness = differenceEnergy(snare.left) / snareEnergy;
    const double tomBrightness = differenceEnergy(tom.left) / tomEnergy;
    const double hatBrightness = differenceEnergy(hat.left) / hatEnergy;
    if (!(kickBrightness < tomBrightness
            && tomBrightness < snareBrightness
            && snareBrightness < hatBrightness)) {
        std::cerr << "break family spectral signatures failed: "
                  << kickBrightness << ", " << snareBrightness << ", "
                  << tomBrightness << ", " << hatBrightness
                  << "\n";
        return false;
    }
    // The onset ramp must prevent a full-scale sample-zero discontinuity.
    if (std::abs(kick.left[0]) > 1.0e-7f
        || std::abs(snare.left[0]) > 1.0e-7f
        || std::abs(tom.left[0]) > 1.0e-7f
        || std::abs(hat.left[0]) > 1.0e-7f) {
        std::cerr << "break onset ramp failed\n";
        return false;
    }
    if (s3g::drumBreakArticulationForMidiNote(36)
            != s3g::DrumBreakArticulation::Kick
        || s3g::drumBreakArticulationForMidiNote(38)
            != s3g::DrumBreakArticulation::Snare
        || s3g::drumBreakArticulationForMidiNote(45)
            != s3g::DrumBreakArticulation::Tom
        || s3g::drumBreakArticulationForMidiNote(42)
            != s3g::DrumBreakArticulation::HiHat
        || s3g::drumBreakArticulationForMidiNote(49)
            != s3g::DrumBreakArticulation::HiHat) {
        std::cerr << "break MIDI family classification failed\n";
        return false;
    }
    return true;
}

bool perVoiceLevelAndBandProbe()
{
    const std::array<s3g::DrumBreakArticulation, 4u> articulations {{
        s3g::DrumBreakArticulation::Kick,
        s3g::DrumBreakArticulation::Snare,
        s3g::DrumBreakArticulation::Tom,
        s3g::DrumBreakArticulation::HiHat,
    }};
    for (const auto articulation : articulations) {
        auto loudParams = s3g::drumBreakFactoryPreset(0u);
        auto quietParams = loudParams;
        switch (articulation) {
        case s3g::DrumBreakArticulation::Kick:
            quietParams.kickLevelDb = -24.0f;
            break;
        case s3g::DrumBreakArticulation::Snare:
            quietParams.snareLevelDb = -24.0f;
            break;
        case s3g::DrumBreakArticulation::Tom:
            quietParams.tomLevelDb = -24.0f;
            break;
        case s3g::DrumBreakArticulation::HiHat:
            quietParams.hiHatLevelDb = -24.0f;
            break;
        }
        const auto loud = render(loudParams, articulation);
        const auto quiet = render(quietParams, articulation);
        if (!(energy(quiet.left) < energy(loud.left) * 0.01)) {
            std::cerr << "per-voice level failed for articulation "
                      << static_cast<uint32_t>(articulation) << "\n";
            return false;
        }
    }

    auto lowBandParams = s3g::drumBreakFactoryPreset(0u);
    lowBandParams.bleed = 1.0f;
    lowBandParams.kickBandHz = 55.0f;
    auto highBandParams = lowBandParams;
    highBandParams.kickBandHz = 900.0f;
    const auto isolatedKick = render(lowBandParams,
        s3g::DrumBreakArticulation::Kick);
    const auto openKick = render(highBandParams,
        s3g::DrumBreakArticulation::Kick);
    const double isolatedBrightness = differenceEnergy(isolatedKick.left)
        / energy(isolatedKick.left);
    const double openBrightness = differenceEnergy(openKick.left)
        / energy(openKick.left);
    if (!(openBrightness > isolatedBrightness * 1.35)) {
        std::cerr << "kick band-pass did not control upper-band bleed: "
                  << isolatedBrightness << " / " << openBrightness << "\n";
        return false;
    }
    return true;
}

bool widthAndVelocityProbe()
{
    auto mono = s3g::drumBreakFactoryPreset(0u);
    mono.stereoWidth = 0.0f;
    const Render centered = render(mono, s3g::DrumBreakArticulation::Snare);
    if (stereoDifference(centered) > 1.0e-14) {
        std::cerr << "break width zero was not exact dual mono\n";
        return false;
    }
    auto wide = mono;
    wide.stereoWidth = 1.0f;
    wide.room = 0.8f;
    const Render opened = render(wide, s3g::DrumBreakArticulation::Snare);
    if (stereoDifference(opened) < 1.0e-7) {
        std::cerr << "break stereo room did not open\n";
        return false;
    }
    const Render loud = render(mono, s3g::DrumBreakArticulation::Snare, 1.0f);
    const Render soft = render(mono, s3g::DrumBreakArticulation::Snare, 0.2f);
    if (!(energy(soft.left) < energy(loud.left) * 0.35)) {
        std::cerr << "break velocity response was ineffective\n";
        return false;
    }
    return true;
}

bool tailAndPolyphonyProbe()
{
    auto p = s3g::drumBreakFactoryPreset(4u);
    s3g::DrumBreak instrument;
    instrument.prepare(96000.0);
    instrument.setParams(p);
    for (uint32_t i = 0u; i < 80u; ++i) {
        instrument.trigger(static_cast<s3g::DrumBreakArticulation>(i % 4u),
            0.4f + static_cast<float>(i % 7u) * 0.08f);
        for (uint32_t frame = 0u; frame < 37u; ++frame) {
            float left = 0.0f;
            float right = 0.0f;
            instrument.processFrame(left, right);
            if (!std::isfinite(left) || !std::isfinite(right)) {
                std::cerr << "break polyphony stress became non-finite\n";
                return false;
            }
        }
    }
    const uint32_t limit = static_cast<uint32_t>(96000.0
        * (s3g::drumBreakTailSeconds(p, s3g::DrumBreakArticulation::Tom,
            96000.0) + 0.15));
    for (uint32_t frame = 0u; frame < limit && instrument.active(); ++frame) {
        float left = 0.0f;
        float right = 0.0f;
        instrument.processFrame(left, right);
    }
    if (instrument.active()) {
        std::cerr << "break failed to become inactive within declared tail\n";
        return false;
    }
    return true;
}

bool presetAndRandomProbe()
{
    for (uint32_t index = 0u;
         index < s3g::kDrumBreakFactoryPresetCount; ++index) {
        const auto p = s3g::drumBreakFactoryPreset(index);
        if (s3g::drumBreakFactoryPresetIndex(p) != static_cast<int>(index)) {
            std::cerr << "break preset identity failed at " << index << "\n";
            return false;
        }
        const auto r = render(p, static_cast<s3g::DrumBreakArticulation>(
            index % 4u), 0.86f, -1, 44100.0, 1.4);
        if (!allFiniteAndBounded(r) || energy(r.left) < 1.0e-6) {
            std::cerr << "break preset render failed at " << index << "\n";
            return false;
        }
    }
    auto preserved = s3g::drumBreakFactoryPreset(0u);
    preserved.noteTracking = 0.73f;
    preserved.velocitySensitivity = 0.41f;
    preserved.outputGainDb = -13.25f;
    const auto a = s3g::drumBreakSafeRandomParams(preserved, 12345u);
    const auto b = s3g::drumBreakSafeRandomParams(preserved, 54321u);
    if (a.noteTracking != preserved.noteTracking
        || a.velocitySensitivity != preserved.velocitySensitivity
        || a.outputGainDb != preserved.outputGainDb
        || (a.lowTuneHz == b.lowTuneHz && a.midTuneHz == b.midTuneHz
            && a.highTone == b.highTone && a.tomTuneHz == b.tomTuneHz
            && a.kickBandHz == b.kickBandHz
            && a.snareLevelDb == b.snareLevelDb)) {
        std::cerr << "break safe random preservation/variation failed\n";
        return false;
    }
    return true;
}

} // namespace

int main()
{
    if (!safetyAndSilenceProbe() || !articulationProbe()
        || !perVoiceLevelAndBandProbe()
        || !widthAndVelocityProbe() || !tailAndPolyphonyProbe()
        || !presetAndRandomProbe()) return 1;
    std::cout << "s3g Drum Break 2 smoke test passed\n";
    return 0;
}
