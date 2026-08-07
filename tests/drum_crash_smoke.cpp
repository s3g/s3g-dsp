#include "s3g_drum_crash.h"
#include "s3g_drum_crash_presets.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <set>
#include <string>
#include <vector>

namespace {

struct Rendered {
    std::vector<float> left;
    std::vector<float> right;
};

Rendered render(const s3g::DrumCrashParams& params,
    s3g::DrumCrashArticulation articulation, int note = -1,
    float velocity = 1.0f, double seconds = 4.0,
    double sampleRate = 48000.0)
{
    s3g::DrumCrash synth;
    synth.prepare(sampleRate);
    synth.setParams(params);
    synth.trigger(articulation, velocity, note);
    Rendered out;
    const uint32_t frames = static_cast<uint32_t>(seconds * sampleRate);
    out.left.resize(frames);
    out.right.resize(frames);
    synth.processBlock(out.left.data(), out.right.data(), frames);
    return out;
}

double energy(const Rendered& audio, uint32_t begin = 0u,
    uint32_t end = std::numeric_limits<uint32_t>::max())
{
    end = std::min<uint32_t>(end, static_cast<uint32_t>(audio.left.size()));
    double total = 0.0;
    for (uint32_t frame = std::min(begin, end); frame < end; ++frame) {
        total += static_cast<double>(audio.left[frame]) * audio.left[frame]
            + static_cast<double>(audio.right[frame]) * audio.right[frame];
    }
    return total;
}

double differenceEnergy(const Rendered& audio)
{
    double total = 0.0;
    for (uint32_t frame = 0u; frame < audio.left.size(); ++frame) {
        const double side = audio.left[frame] - audio.right[frame];
        total += side * side;
    }
    return total;
}

double variation(const Rendered& audio)
{
    double total = 0.0;
    for (uint32_t frame = 1u; frame < audio.left.size(); ++frame) {
        total += std::abs(audio.left[frame] - audio.left[frame - 1u]);
    }
    return total;
}

double e90Milliseconds(const Rendered& audio, double sampleRate = 48000.0)
{
    const double total = energy(audio);
    if (total <= 0.0) return 0.0;
    double accumulated = 0.0;
    for (uint32_t frame = 0u; frame < audio.left.size(); ++frame) {
        accumulated += static_cast<double>(audio.left[frame])
                * audio.left[frame]
            + static_cast<double>(audio.right[frame]) * audio.right[frame];
        if (accumulated >= total * 0.90) {
            return static_cast<double>(frame) / sampleRate * 1000.0;
        }
    }
    return static_cast<double>(audio.left.size()) / sampleRate * 1000.0;
}

bool finiteAndOnsetProbe()
{
    const auto audio = render({}, s3g::DrumCrashArticulation::Crash);
    double peak = 0.0;
    double maximumStep = 0.0;
    for (uint32_t frame = 0u; frame < audio.left.size(); ++frame) {
        if (!std::isfinite(audio.left[frame])
            || !std::isfinite(audio.right[frame])) {
            std::cerr << "crash produced non-finite output\n";
            return false;
        }
        peak = std::max({peak, std::abs(static_cast<double>(audio.left[frame])),
            std::abs(static_cast<double>(audio.right[frame]))});
        if (frame > 0u) {
            maximumStep = std::max(maximumStep,
                std::abs(static_cast<double>(audio.left[frame])
                    - audio.left[frame - 1u]));
        }
    }
    if (audio.left[0u] != 0.0f || audio.right[0u] != 0.0f
        || peak < 1.0e-4 || peak > 1.5 || maximumStep > 0.55) {
        std::cerr << "crash onset/level failed: peak=" << peak
                  << " step=" << maximumStep << "\n";
        return false;
    }
    return true;
}

bool sanitationProbe()
{
    s3g::DrumCrash synth;
    synth.prepare(std::numeric_limits<double>::quiet_NaN());
    s3g::DrumCrashParams invalid;
    invalid.tuneHz = std::numeric_limits<float>::infinity();
    invalid.density = -20.0f;
    invalid.decaySeconds = 99.0f;
    invalid.washDecaySeconds = -2.0f;
    invalid.bellTuneHz = 90000.0f;
    invalid.chokeTimeMs = -1.0f;
    invalid.stereoWidth = -1.0f;
    invalid.outputGainDb = 99.0f;
    synth.setParams(invalid);
    const auto p = synth.params();
    if (p.tuneHz != 720.0f || p.density != 4.0f
        || p.decaySeconds != 8.0f || p.washDecaySeconds != 0.08f
        || p.bellTuneHz != 6000.0f || p.chokeTimeMs != 0.5f
        || p.stereoWidth != 0.0f || p.outputGainDb != 12.0f) {
        std::cerr << "crash sanitation failed\n";
        return false;
    }
    synth.reset();
    for (uint32_t frame = 0u; frame < 128u; ++frame) {
        float left = 1.0f;
        float right = -1.0f;
        synth.processFrame(left, right);
        if (left != 0.0f || right != 0.0f) return false;
    }
    s3g::DrumCrashParams silent;
    silent.velocitySensitivity = 1.0f;
    synth.setParams(silent);
    synth.trigger(s3g::DrumCrashArticulation::Crash, 0.0f);
    return !synth.active();
}

bool articulationAndTrackingProbe()
{
    s3g::DrumCrashParams params;
    params.stereoWidth = 0.0f;
    params.character = {};
    const auto crash = render(params, s3g::DrumCrashArticulation::Crash, 49);
    const auto choke = render(params, s3g::DrumCrashArticulation::Choke, 50);
    const auto bell = render(params, s3g::DrumCrashArticulation::Bell, 53);
    const double crashE90 = e90Milliseconds(crash);
    const double chokeE90 = e90Milliseconds(choke);
    const double bellE90 = e90Milliseconds(bell);
    std::cout << "crash E90 ms crash/choke/bell: " << crashE90 << " / "
              << chokeE90 << " / " << bellE90 << "\n";
    if (!(crashE90 >= 180.0 && crashE90 <= 3600.0)
        || !(chokeE90 < crashE90 * 0.20)
        || !(bellE90 < crashE90)) {
        std::cerr << "crash articulation response failed\n";
        return false;
    }

    params.noteTracking = 1.0f;
    params.wash = 0.0f;
    const auto low = render(params,
        s3g::DrumCrashArticulation::Crash, 49, 1.0f, 1.0);
    const auto octave = render(params,
        s3g::DrumCrashArticulation::Crash, 61, 1.0f, 1.0);
    const double lowMotion = variation(low) / std::sqrt(energy(low));
    const double highMotion = variation(octave) / std::sqrt(energy(octave));
    if (!(highMotion > lowMotion * 1.22) || low.left != low.right) {
        std::cerr << "crash tracking or mono compatibility failed\n";
        return false;
    }

    params.stereoWidth = 1.0f;
    params.wash = 0.9f;
    const auto wide = render(params,
        s3g::DrumCrashArticulation::Crash, 49, 1.0f, 1.0);
    if (!(differenceEnergy(wide) > energy(wide) * 1.0e-4)) {
        std::cerr << "crash stereo width produced no side energy\n";
        return false;
    }

    params.velocitySensitivity = 1.0f;
    const auto soft = render(params,
        s3g::DrumCrashArticulation::Crash, 49, 0.2f, 1.0);
    const auto loud = render(params,
        s3g::DrumCrashArticulation::Crash, 49, 1.0f, 1.0);
    return energy(loud) > energy(soft) * 5.0;
}

bool chokeLatchLifecycleProbe()
{
    s3g::DrumCrashParams params;
    params.decaySeconds = 3.0f;
    params.washDecaySeconds = 3.0f;
    params.chokeTimeMs = 8.0f;
    s3g::DrumCrash choked;
    s3g::DrumCrash reference;
    choked.prepare(48000.0);
    reference.prepare(48000.0);
    choked.setParams(params);
    reference.setParams(params);
    choked.trigger(s3g::DrumCrashArticulation::Crash, 1.0f, 49);
    reference.trigger(s3g::DrumCrashArticulation::Crash, 1.0f, 49);
    for (uint32_t frame = 0u; frame < 4096u; ++frame) {
        float a = 0.0f, b = 0.0f, c = 0.0f, d = 0.0f;
        choked.processFrame(a, b);
        reference.processFrame(c, d);
    }
    choked.trigger(s3g::DrumCrashArticulation::Choke, 0.7f, 50);
    double chokedTail = 0.0;
    double referenceTail = 0.0;
    for (uint32_t frame = 0u; frame < 30000u; ++frame) {
        float a = 0.0f, b = 0.0f, c = 0.0f, d = 0.0f;
        choked.processFrame(a, b);
        reference.processFrame(c, d);
        if (frame > 6000u) {
            chokedTail += static_cast<double>(a) * a + b * b;
            referenceTail += static_cast<double>(c) * c + d * d;
        }
    }
    if (!(chokedTail < referenceTail * 0.08)) {
        std::cerr << "crash choke did not suppress prior voice\n";
        return false;
    }

    s3g::DrumCrash original;
    s3g::DrumCrash edited;
    original.prepare(48000.0);
    edited.prepare(48000.0);
    original.setParams(params);
    edited.setParams(params);
    original.trigger(s3g::DrumCrashArticulation::Crash, 0.8f, 49);
    edited.trigger(s3g::DrumCrashArticulation::Crash, 0.8f, 49);
    for (uint32_t frame = 0u; frame < 256u; ++frame) {
        float a = 0.0f, b = 0.0f, c = 0.0f, d = 0.0f;
        original.processFrame(a, b);
        edited.processFrame(c, d);
        if (a != c || b != d) return false;
    }
    auto changed = params;
    changed.tuneHz = 1500.0f;
    changed.decaySeconds = 0.08f;
    changed.washDecaySeconds = 0.08f;
    edited.setParams(changed);
    for (uint32_t frame = 0u; frame < 4096u; ++frame) {
        float a = 0.0f, b = 0.0f, c = 0.0f, d = 0.0f;
        original.processFrame(a, b);
        edited.processFrame(c, d);
        if (a != c || b != d) {
            std::cerr << "crash hit parameters were not latched\n";
            return false;
        }
    }

    s3g::DrumCrash shortVoice;
    shortVoice.prepare(48000.0);
    s3g::DrumCrashParams shortParams;
    shortParams.decaySeconds = 0.08f;
    shortParams.washDecaySeconds = 0.08f;
    shortParams.wash = 0.0f;
    shortParams.bell = 0.0f;
    shortVoice.setParams(shortParams);
    shortVoice.trigger(s3g::DrumCrashArticulation::Crash);
    for (uint32_t frame = 0u; frame < 48000u * 3u; ++frame) {
        float left = 0.0f, right = 0.0f;
        shortVoice.processFrame(left, right);
    }
    return !shortVoice.active();
}

bool presetAndRandomProbe()
{
    std::set<std::string> names;
    for (uint32_t index = 0u;
         index < s3g::kDrumCrashFactoryPresetCount; ++index) {
        const auto& info = s3g::drumCrashFactoryPresetInfo(index);
        const auto params = s3g::drumCrashFactoryPreset(index);
        if (!names.insert(info.name).second || !info.description
            || std::string(info.description).empty()
            || s3g::drumCrashFactoryPresetIndex(params)
                != static_cast<int32_t>(index)) {
            std::cerr << "crash preset metadata/lookup failed\n";
            return false;
        }
    }
    s3g::DrumCrashParams current;
    current.noteTracking = 0.37f;
    current.velocitySensitivity = 0.44f;
    current.outputGainDb = -13.0f;
    uint32_t state = 0x43524153u;
    for (uint32_t iteration = 0u; iteration < 512u; ++iteration) {
        const auto p = s3g::drumCrashSafeRandomParams(current, state);
        if (p.noteTracking != current.noteTracking
            || p.velocitySensitivity != current.velocitySensitivity
            || p.outputGainDb != current.outputGainDb
            || p.tuneHz < 180.0f || p.tuneHz > 2400.0f
            || p.density < 4.0f || p.density > 16.0f
            || p.decaySeconds < 0.08f || p.decaySeconds > 8.0f) {
            std::cerr << "crash safe random contract failed\n";
            return false;
        }
    }
    return true;
}

bool stressProbe()
{
    for (double sampleRate : {8000.0, 44100.0, 96000.0, 768000.0}) {
        s3g::DrumCrash synth;
        synth.prepare(sampleRate);
        s3g::DrumCrashParams params;
        params.decaySeconds = 0.08f;
        params.washDecaySeconds = 0.08f;
        params.character.drive = 1.0f;
        params.character.bitDepthReduction = 1.0f;
        params.outputGainDb = 12.0f;
        synth.setParams(params);
        for (uint32_t frame = 0u; frame < 16000u; ++frame) {
            if ((frame % 43u) == 0u) {
                synth.trigger(static_cast<s3g::DrumCrashArticulation>(
                    (frame / 43u) % 3u), 1.0f,
                    static_cast<int>(frame % 128u));
            }
            float left = 0.0f;
            float right = 0.0f;
            synth.processFrame(left, right);
            if (!std::isfinite(left) || !std::isfinite(right)
                || std::abs(left) > 4.0f || std::abs(right) > 4.0f) {
                std::cerr << "crash stress failed at " << sampleRate << "\n";
                return false;
            }
        }
    }
    return true;
}

} // namespace

int main()
{
    const bool ok = finiteAndOnsetProbe()
        && sanitationProbe()
        && articulationAndTrackingProbe()
        && chokeLatchLifecycleProbe()
        && presetAndRandomProbe()
        && stressProbe();
    if (!ok) return 1;
    std::cout << "crash smoke passed\n";
    return 0;
}
