#include "s3g_drum_cowbell.h"
#include "s3g_drum_cowbell_presets.h"

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

Rendered render(const s3g::DrumCowbellParams& params,
    s3g::DrumCowbellArticulation articulation, int note = -1,
    float velocity = 1.0f, double seconds = 1.2,
    double sampleRate = 48000.0)
{
    s3g::DrumCowbell synth;
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
    const auto audio = render({}, s3g::DrumCowbellArticulation::Cowbell);
    double peak = 0.0;
    double maximumStep = 0.0;
    for (uint32_t frame = 0u; frame < audio.left.size(); ++frame) {
        if (!std::isfinite(audio.left[frame])
            || !std::isfinite(audio.right[frame])) {
            std::cerr << "cowbell produced non-finite output\n";
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
        std::cerr << "cowbell onset/level failed: peak=" << peak
                  << " step=" << maximumStep << "\n";
        return false;
    }
    return true;
}

bool sanitationProbe()
{
    s3g::DrumCowbell synth;
    synth.prepare(std::numeric_limits<double>::quiet_NaN());
    s3g::DrumCowbellParams invalid;
    invalid.tuneHz = std::numeric_limits<float>::infinity();
    invalid.intervalRatio = -20.0f;
    invalid.decaySeconds = 99.0f;
    invalid.bodyDecaySeconds = -2.0f;
    invalid.bendSemitones = 500.0f;
    invalid.stereoWidth = -1.0f;
    invalid.outputGainDb = 99.0f;
    synth.setParams(invalid);
    const auto p = synth.params();
    if (p.tuneHz != 560.0f || p.intervalRatio != 1.08f
        || p.decaySeconds != 2.0f || p.bodyDecaySeconds != 0.02f
        || p.bendSemitones != 24.0f || p.stereoWidth != 0.0f
        || p.outputGainDb != 12.0f) {
        std::cerr << "cowbell sanitation failed\n";
        return false;
    }
    synth.reset();
    for (uint32_t frame = 0u; frame < 128u; ++frame) {
        float left = 1.0f;
        float right = -1.0f;
        synth.processFrame(left, right);
        if (left != 0.0f || right != 0.0f) return false;
    }
    s3g::DrumCowbellParams silent;
    silent.velocitySensitivity = 1.0f;
    synth.setParams(silent);
    synth.trigger(s3g::DrumCowbellArticulation::Cowbell, 0.0f);
    return !synth.active();
}

bool articulationAndTrackingProbe()
{
    s3g::DrumCowbellParams params;
    params.stereoWidth = 0.0f;
    params.character = {};
    const auto cow = render(params,
        s3g::DrumCowbellArticulation::Cowbell, 56);
    const auto muted = render(params,
        s3g::DrumCowbellArticulation::Muted, 57);
    const auto high = render(params,
        s3g::DrumCowbellArticulation::High, 58);
    const double cowE90 = e90Milliseconds(cow);
    const double mutedE90 = e90Milliseconds(muted);
    const double highE90 = e90Milliseconds(high);
    const double cowMotion = variation(cow) / std::sqrt(energy(cow));
    const double highMotion = variation(high) / std::sqrt(energy(high));
    std::cout << "cowbell E90 ms cow/muted/high: " << cowE90 << " / "
              << mutedE90 << " / " << highE90 << "\n";
    if (!(cowE90 >= 18.0 && cowE90 <= 500.0)
        || !(mutedE90 < cowE90 * 0.62)
        || !(highE90 < cowE90)
        || !(highMotion > cowMotion * 1.08)) {
        std::cerr << "cowbell articulation response failed\n";
        return false;
    }

    params.noteTracking = 1.0f;
    params.noise = 0.0f;
    params.body = 0.0f;
    const auto low = render(params,
        s3g::DrumCowbellArticulation::Cowbell, 56);
    const auto octave = render(params,
        s3g::DrumCowbellArticulation::Cowbell, 68);
    if (!(variation(octave) > variation(low) * 1.35)
        || low.left != low.right) {
        std::cerr << "cowbell tracking or mono compatibility failed\n";
        return false;
    }

    params.stereoWidth = 1.0f;
    params.body = 0.8f;
    params.noise = 0.3f;
    const auto wide = render(params,
        s3g::DrumCowbellArticulation::Cowbell, 56);
    if (!(differenceEnergy(wide) > energy(wide) * 1.0e-5)) {
        std::cerr << "cowbell stereo width produced no side energy\n";
        return false;
    }

    params.velocitySensitivity = 1.0f;
    const auto soft = render(params,
        s3g::DrumCowbellArticulation::Cowbell, 56, 0.2f);
    const auto loud = render(params,
        s3g::DrumCowbellArticulation::Cowbell, 56, 1.0f);
    return energy(loud) > energy(soft) * 5.0;
}

bool chokeLatchLifecycleProbe()
{
    s3g::DrumCowbellParams params;
    params.decaySeconds = 0.8f;
    params.bodyDecaySeconds = 0.7f;
    s3g::DrumCowbell choked;
    s3g::DrumCowbell reference;
    choked.prepare(48000.0);
    reference.prepare(48000.0);
    choked.setParams(params);
    reference.setParams(params);
    choked.trigger(s3g::DrumCowbellArticulation::Cowbell, 1.0f, 56);
    reference.trigger(s3g::DrumCowbellArticulation::Cowbell, 1.0f, 56);
    for (uint32_t frame = 0u; frame < 1024u; ++frame) {
        float a = 0.0f, b = 0.0f, c = 0.0f, d = 0.0f;
        choked.processFrame(a, b);
        reference.processFrame(c, d);
    }
    choked.trigger(s3g::DrumCowbellArticulation::Muted, 0.7f, 57);
    double chokedTail = 0.0;
    double referenceTail = 0.0;
    for (uint32_t frame = 0u; frame < 12000u; ++frame) {
        float a = 0.0f, b = 0.0f, c = 0.0f, d = 0.0f;
        choked.processFrame(a, b);
        reference.processFrame(c, d);
        if (frame > 1200u) {
            chokedTail += static_cast<double>(a) * a + b * b;
            referenceTail += static_cast<double>(c) * c + d * d;
        }
    }
    if (!(chokedTail < referenceTail * 0.35)) {
        std::cerr << "cowbell muted articulation did not choke prior voice\n";
        return false;
    }

    s3g::DrumCowbell original;
    s3g::DrumCowbell edited;
    original.prepare(48000.0);
    edited.prepare(48000.0);
    original.setParams(params);
    edited.setParams(params);
    original.trigger(s3g::DrumCowbellArticulation::Cowbell, 0.8f, 56);
    edited.trigger(s3g::DrumCowbellArticulation::Cowbell, 0.8f, 56);
    for (uint32_t frame = 0u; frame < 256u; ++frame) {
        float a = 0.0f, b = 0.0f, c = 0.0f, d = 0.0f;
        original.processFrame(a, b);
        edited.processFrame(c, d);
        if (a != c || b != d) return false;
    }
    auto changed = params;
    changed.tuneHz = 1400.0f;
    changed.decaySeconds = 0.04f;
    edited.setParams(changed);
    for (uint32_t frame = 0u; frame < 4096u; ++frame) {
        float a = 0.0f, b = 0.0f, c = 0.0f, d = 0.0f;
        original.processFrame(a, b);
        edited.processFrame(c, d);
        if (a != c || b != d) {
            std::cerr << "cowbell hit parameters were not latched\n";
            return false;
        }
    }
    edited.reset();
    return !edited.active();
}

bool presetAndRandomProbe()
{
    std::set<std::string> names;
    for (uint32_t index = 0u;
         index < s3g::kDrumCowbellFactoryPresetCount; ++index) {
        const auto& info = s3g::drumCowbellFactoryPresetInfo(index);
        const auto params = s3g::drumCowbellFactoryPreset(index);
        if (!names.insert(info.name).second || !info.description
            || std::string(info.description).empty()
            || s3g::drumCowbellFactoryPresetIndex(params)
                != static_cast<int32_t>(index)) {
            std::cerr << "cowbell preset metadata/lookup failed\n";
            return false;
        }
    }
    s3g::DrumCowbellParams current;
    current.noteTracking = 0.37f;
    current.velocitySensitivity = 0.44f;
    current.outputGainDb = -13.0f;
    uint32_t state = 0x4357424cu;
    for (uint32_t iteration = 0u; iteration < 512u; ++iteration) {
        const auto p = s3g::drumCowbellSafeRandomParams(current, state);
        if (p.noteTracking != current.noteTracking
            || p.velocitySensitivity != current.velocitySensitivity
            || p.outputGainDb != current.outputGainDb
            || p.tuneHz < 180.0f || p.tuneHz > 2400.0f
            || p.intervalRatio < 1.08f || p.intervalRatio > 2.60f
            || p.decaySeconds < 0.025f || p.decaySeconds > 2.0f) {
            std::cerr << "cowbell safe random contract failed\n";
            return false;
        }
    }
    return true;
}

bool stressProbe()
{
    for (double sampleRate : {8000.0, 44100.0, 96000.0, 768000.0}) {
        s3g::DrumCowbell synth;
        synth.prepare(sampleRate);
        s3g::DrumCowbellParams params;
        params.decaySeconds = 0.025f;
        params.bodyDecaySeconds = 0.02f;
        params.noiseDecaySeconds = 0.004f;
        params.character.drive = 1.0f;
        params.character.bitDepthReduction = 1.0f;
        params.outputGainDb = 12.0f;
        synth.setParams(params);
        for (uint32_t frame = 0u; frame < 16000u; ++frame) {
            if ((frame % 31u) == 0u) {
                synth.trigger(static_cast<s3g::DrumCowbellArticulation>(
                    (frame / 31u) % 3u), 1.0f,
                    static_cast<int>(frame % 128u));
            }
            float left = 0.0f;
            float right = 0.0f;
            synth.processFrame(left, right);
            if (!std::isfinite(left) || !std::isfinite(right)
                || std::abs(left) > 3.0f || std::abs(right) > 3.0f) {
                std::cerr << "cowbell stress failed at " << sampleRate << "\n";
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
    std::cout << "cowbell smoke passed\n";
    return 0;
}
