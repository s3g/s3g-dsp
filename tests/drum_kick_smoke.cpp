#include "s3g_drum_kick.h"
#include "s3g_drum_kick_presets.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

struct StereoRender {
    std::vector<float> left;
    std::vector<float> right;
};

StereoRender render(const s3g::DrumKickParams& params,
    double sampleRate = 48000.0, float velocity = 1.0f,
    int midiNote = 36, double seconds = 1.0)
{
    s3g::DrumKick kick;
    kick.prepare(sampleRate);
    kick.setParams(params);
    kick.reset();
    kick.trigger(velocity, midiNote);
    const uint32_t frames = static_cast<uint32_t>(sampleRate * seconds);
    StereoRender result { std::vector<float>(frames),
        std::vector<float>(frames) };
    kick.processBlock(result.left.data(), result.right.data(), frames);
    return result;
}

double energy(const std::vector<float>& signal, uint32_t begin = 0u,
    uint32_t end = std::numeric_limits<uint32_t>::max())
{
    end = std::min<uint32_t>(end, static_cast<uint32_t>(signal.size()));
    begin = std::min(begin, end);
    double result = 0.0;
    for (uint32_t frame = begin; frame < end; ++frame) {
        const double sample = signal[frame];
        result += sample * sample;
    }
    return result;
}

double differenceEnergy(const std::vector<float>& left,
    const std::vector<float>& right)
{
    double result = 0.0;
    const uint32_t frames = static_cast<uint32_t>(
        std::min(left.size(), right.size()));
    for (uint32_t frame = 0u; frame < frames; ++frame) {
        const double difference = static_cast<double>(left[frame]) - right[frame];
        result += difference * difference;
    }
    return result;
}

bool sameKickParams(const s3g::DrumKickParams& left,
    const s3g::DrumKickParams& right)
{
    return left.tuneHz == right.tuneHz
        && left.noteTracking == right.noteTracking
        && left.pitchDropSemitones == right.pitchDropSemitones
        && left.pitchSweepMs == right.pitchSweepMs
        && left.pitchSettle == right.pitchSettle
        && left.body == right.body
        && left.harmonics == right.harmonics
        && left.decaySeconds == right.decaySeconds
        && left.tail == right.tail
        && left.punch == right.punch
        && left.click == right.click
        && left.clickTone == right.clickTone
        && left.clickDecayMs == right.clickDecayMs
        && left.texture == right.texture
        && left.textureTone == right.textureTone
        && left.textureDecaySeconds == right.textureDecaySeconds
        && left.character.drive == right.character.drive
        && left.character.bias == right.character.bias
        && left.character.compression == right.character.compression
        && left.character.sampleRateReduction
            == right.character.sampleRateReduction
        && left.character.bitDepthReduction
            == right.character.bitDepthReduction
        && left.character.reconstruction == right.character.reconstruction
        && left.character.tone == right.character.tone
        && left.stereoWidth == right.stereoWidth
        && left.velocitySensitivity == right.velocitySensitivity
        && left.outputGainDb == right.outputGainDb;
}

double estimateFrequency(const std::vector<float>& signal,
    double sampleRate, double beginSeconds, double endSeconds)
{
    const uint32_t begin = std::min<uint32_t>(
        static_cast<uint32_t>(beginSeconds * sampleRate),
        static_cast<uint32_t>(signal.size()));
    const uint32_t end = std::min<uint32_t>(
        static_cast<uint32_t>(endSeconds * sampleRate),
        static_cast<uint32_t>(signal.size()));
    uint32_t crossings = 0u;
    for (uint32_t frame = begin + 1u; frame < end; ++frame) {
        if (signal[frame - 1u] <= 0.0f && signal[frame] > 0.0f) {
            ++crossings;
        }
    }
    const double duration = static_cast<double>(end - begin) / sampleRate;
    return duration > 0.0 ? static_cast<double>(crossings) / duration : 0.0;
}

bool silenceAndSafetyProbe()
{
    s3g::DrumKick kick;
    kick.prepare(std::numeric_limits<double>::quiet_NaN());
    s3g::DrumKickParams invalid;
    invalid.tuneHz = std::numeric_limits<float>::infinity();
    invalid.noteTracking = -8.0f;
    invalid.pitchDropSemitones = 400.0f;
    invalid.pitchSweepMs = std::numeric_limits<float>::quiet_NaN();
    invalid.decaySeconds = -1.0f;
    invalid.character.bias = 99.0f;
    invalid.outputGainDb = 200.0f;
    kick.setParams(invalid);
    kick.reset();

    const auto sanitized = kick.params();
    if (sanitized.tuneHz != 48.0f || sanitized.noteTracking != 0.0f
        || sanitized.pitchDropSemitones != 60.0f
        || sanitized.pitchSweepMs != 45.0f
        || sanitized.decaySeconds != 0.02f
        || sanitized.character.bias != 1.0f
        || sanitized.outputGainDb != 12.0f) {
        std::cerr << "kick parameter sanitation failed\n";
        return false;
    }

    for (uint32_t frame = 0u; frame < 4096u; ++frame) {
        float left = 1.0f;
        float right = -1.0f;
        kick.processFrame(left, right);
        if (left != 0.0f || right != 0.0f) {
            std::cerr << "untriggered kick was not silent\n";
            return false;
        }
    }

    kick.trigger(std::numeric_limits<float>::infinity(), 2000);
    for (uint32_t frame = 0u; frame < 500000u; ++frame) {
        float left = 0.0f;
        float right = 0.0f;
        kick.processFrame(left, right);
        if (!std::isfinite(left) || !std::isfinite(right)
            || std::abs(left) > 3.0f || std::abs(right) > 3.0f) {
            std::cerr << "kick safety bound failed at frame " << frame << "\n";
            return false;
        }
    }
    return true;
}

bool defaultVoiceProbe()
{
    const auto rendered = render({}, 48000.0, 1.0f, 36, 1.2);
    const double total = energy(rendered.left);
    const double lateFrequency = estimateFrequency(
        rendered.left, 48000.0, 0.45, 0.85);
    if (!(total > 1.0) || !(lateFrequency > 35.0 && lateFrequency < 75.0)) {
        std::cerr << "default voice lacked a plausible tuned kick body: energy "
                  << total << ", frequency " << lateFrequency << "\n";
        return false;
    }
    return true;
}

bool stereoContractProbe()
{
    s3g::DrumKickParams mono;
    mono.stereoWidth = 0.0f;
    mono.texture = 0.72f;
    mono.click = 0.55f;
    const auto centered = render(mono, 48000.0, 1.0f, 36, 0.6);
    if (centered.left != centered.right) {
        std::cerr << "width zero did not produce exact dual mono\n";
        return false;
    }

    auto wide = mono;
    wide.stereoWidth = 1.0f;
    wide.textureTone = 0.78f;
    const auto spread = render(wide, 48000.0, 1.0f, 36, 0.6);
    const double sideEnergy = differenceEnergy(spread.left, spread.right);
    const double mainEnergy = energy(spread.left) + energy(spread.right);
    if (!(sideEnergy > mainEnergy * 1.0e-4)) {
        std::cerr << "width did not create meaningful stereo top energy: "
                  << sideEnergy << " / " << mainEnergy << "\n";
        return false;
    }

    s3g::DrumKick kick;
    kick.prepare(48000.0);
    kick.setParams(wide);
    kick.reset();
    kick.trigger();
    std::array<float, 1024u> monoBlock {};
    kick.processBlock(monoBlock.data(), nullptr,
        static_cast<uint32_t>(monoBlock.size()));
    if (!(energy(std::vector<float>(monoBlock.begin(), monoBlock.end())) > 0.01)) {
        std::cerr << "mono block output path was silent\n";
        return false;
    }
    return true;
}

bool pitchAndTrackingProbe()
{
    s3g::DrumKickParams tonal;
    tonal.tuneHz = 42.0f;
    tonal.noteTracking = 1.0f;
    tonal.pitchDropSemitones = 0.0f;
    tonal.body = 0.0f;
    tonal.harmonics = 0.0f;
    tonal.click = 0.0f;
    tonal.texture = 0.0f;
    tonal.decaySeconds = 2.0f;
    tonal.tail = 0.0f;
    tonal.outputGainDb = -12.0f;
    const auto low = render(tonal, 48000.0, 1.0f, 36, 1.0);
    const auto high = render(tonal, 48000.0, 1.0f, 48, 1.0);
    const double lowHz = estimateFrequency(low.left, 48000.0, 0.2, 0.8);
    const double highHz = estimateFrequency(high.left, 48000.0, 0.2, 0.8);
    if (!(lowHz > 38.0 && lowHz < 46.0)
        || !(highHz / lowHz > 1.90 && highHz / lowHz < 2.10)) {
        std::cerr << "note tracking did not span an octave: "
                  << lowHz << " / " << highHz << "\n";
        return false;
    }

    tonal.pitchDropSemitones = 30.0f;
    tonal.pitchSweepMs = 95.0f;
    tonal.pitchSettle = 0.45f;
    const auto swept = render(tonal, 48000.0, 1.0f, 36, 1.0);
    const double earlyHz = estimateFrequency(
        swept.left, 48000.0, 0.015, 0.10);
    const double settledHz = estimateFrequency(
        swept.left, 48000.0, 0.55, 0.95);
    if (!(earlyHz > settledHz * 1.35)) {
        std::cerr << "pitch envelope did not settle downward: "
                  << earlyHz << " / " << settledHz << "\n";
        return false;
    }
    return true;
}

bool envelopeAndVelocityProbe()
{
    s3g::DrumKickParams params;
    params.click = 0.0f;
    params.texture = 0.0f;
    params.tail = 0.0f;
    params.decaySeconds = 0.12f;
    const auto shortHit = render(params, 48000.0, 1.0f, 36, 1.0);
    params.decaySeconds = 1.5f;
    params.tail = 0.75f;
    const auto longHit = render(params, 48000.0, 1.0f, 36, 1.0);
    const uint32_t lateBegin = 24000u;
    if (!(energy(longHit.left, lateBegin) >
            energy(shortHit.left, lateBegin) * 100.0 + 0.01)) {
        std::cerr << "decay and tail did not extend the body\n";
        return false;
    }

    params.decaySeconds = 0.02f;
    params.tail = 0.0f;
    params.body = 0.0f;
    params.texture = 1.0f;
    params.textureDecaySeconds = 4.0f;
    const auto longTexture = render(params, 48000.0, 1.0f, 36, 2.5);
    if (!(energy(longTexture.left, 96000u) > 0.01)) {
        std::cerr << "independent texture was cut off at the body lifetime\n";
        return false;
    }

    params = {};
    params.velocitySensitivity = 1.0f;
    const auto quiet = render(params, 48000.0, 0.2f, 36, 0.5);
    const auto loud = render(params, 48000.0, 1.0f, 36, 0.5);
    if (!(energy(loud.left) > energy(quiet.left) * 8.0)) {
        std::cerr << "velocity sensitivity did not control energy\n";
        return false;
    }

    params.velocitySensitivity = 0.0f;
    const auto fixedQuiet = render(params, 48000.0, 0.2f, 36, 0.5);
    const auto fixedLoud = render(params, 48000.0, 1.0f, 36, 0.5);
    if (fixedQuiet.left != fixedLoud.left) {
        std::cerr << "zero velocity sensitivity was not deterministic/fixed\n";
        return false;
    }
    return true;
}

bool polyphonyAndLifecycleProbe()
{
    s3g::DrumKick kick;
    kick.prepare(48000.0);
    s3g::DrumKickParams params;
    params.decaySeconds = 0.25f;
    params.tail = 0.0f;
    params.click = 0.0f;
    params.texture = 0.0f;
    kick.setParams(params);
    kick.reset();
    for (uint32_t note = 0u; note < s3g::DrumKick::kVoiceCount + 5u; ++note) {
        kick.trigger(0.4f + 0.03f * static_cast<float>(note),
            30 + static_cast<int>(note));
    }
    if (!kick.active()) {
        std::cerr << "overlapping triggers did not activate the voice pool\n";
        return false;
    }

    double outputEnergy = 0.0;
    for (uint32_t frame = 0u; frame < 200000u; ++frame) {
        if (frame == 200u || frame == 900u) kick.trigger(0.9f, 48);
        float left = 0.0f;
        float right = 0.0f;
        kick.processFrame(left, right);
        outputEnergy += static_cast<double>(left) * left;
        if (!std::isfinite(left) || !std::isfinite(right)) {
            std::cerr << "polyphonic render became non-finite\n";
            return false;
        }
    }
    if (!(outputEnergy > 1.0) || kick.active()) {
        std::cerr << "voice pool energy/lifecycle failed\n";
        return false;
    }
    return true;
}

bool characterAndResetProbe()
{
    s3g::DrumKickParams dry;
    dry.click = 0.25f;
    dry.texture = 0.1f;
    const auto clean = render(dry, 48000.0, 1.0f, 36, 0.5);
    auto colored = dry;
    colored.character.drive = 0.75f;
    colored.character.bias = 0.33f;
    colored.character.compression = 0.45f;
    colored.character.sampleRateReduction = 0.55f;
    colored.character.bitDepthReduction = 0.4f;
    colored.character.reconstruction = 0.35f;
    colored.character.tone = -0.2f;
    const auto first = render(colored, 48000.0, 1.0f, 36, 0.5);
    const auto second = render(colored, 48000.0, 1.0f, 36, 0.5);
    if (first.left != second.left || first.right != second.right) {
        std::cerr << "kick reset did not reproduce deterministic output\n";
        return false;
    }
    double changed = 0.0;
    for (uint32_t frame = 0u; frame < first.left.size(); ++frame) {
        const double difference = static_cast<double>(first.left[frame])
            - clean.left[frame];
        changed += difference * difference;
    }
    if (!(changed > 0.1)) {
        std::cerr << "shared character stage did not affect the kick\n";
        return false;
    }
    return true;
}

bool safeRandomProbe()
{
    s3g::DrumKickParams current;
    current.noteTracking = 0.37125f;
    current.velocitySensitivity = 0.61875f;
    current.outputGainDb = -6.0f;

    constexpr uint32_t kSeed = 0x51e37a9du;
    const auto first = s3g::drumKickSafeRandomParams(current, kSeed);
    const auto repeated = s3g::drumKickSafeRandomParams(current, kSeed);
    if (!sameKickParams(first, repeated)) {
        std::cerr << "safe RANDOM was not seed-deterministic\n";
        return false;
    }

    s3g::DrumRandom stream(kSeed);
    const auto streamedFirst = s3g::drumKickSafeRandomParams(current, stream);
    const auto streamedSecond = s3g::drumKickSafeRandomParams(current, stream);
    if (!sameKickParams(first, streamedFirst)
        || sameKickParams(streamedFirst, streamedSecond)) {
        std::cerr << "safe RANDOM reusable stream contract failed\n";
        return false;
    }
    stream.reset(kSeed);
    if (!sameKickParams(first,
            s3g::drumKickSafeRandomParams(current, stream))) {
        std::cerr << "safe RANDOM stream reset was not deterministic\n";
        return false;
    }

    uint32_t cleanConversions = 0u;
    uint32_t distinctFromPrevious = 0u;
    float maximumPeak = 0.0f;
    s3g::DrumKickParams previous {};
    constexpr uint32_t kPatchCount = 128u;
    for (uint32_t index = 0u; index < kPatchCount; ++index) {
        const auto params = s3g::drumKickSafeRandomParams(
            current, 0x9e3779b9u + index * 0x85ebca6bu);
        if (params.noteTracking != current.noteTracking
            || params.velocitySensitivity != current.velocitySensitivity
            || params.outputGainDb != current.outputGainDb) {
            std::cerr << "safe RANDOM changed a user-owned control at "
                      << index << "\n";
            return false;
        }
        const std::array<float, 23u> randomized {{
            params.tuneHz, params.pitchDropSemitones, params.pitchSweepMs,
            params.pitchSettle, params.body, params.harmonics,
            params.decaySeconds, params.tail, params.punch, params.click,
            params.clickTone, params.clickDecayMs, params.texture,
            params.textureTone, params.textureDecaySeconds,
            params.character.drive, params.character.bias,
            params.character.compression,
            params.character.sampleRateReduction,
            params.character.bitDepthReduction,
            params.character.reconstruction, params.character.tone,
            params.stereoWidth,
        }};
        if (!std::all_of(randomized.begin(), randomized.end(),
                [](float value) { return std::isfinite(value); })
            || params.tuneHz < 32.0f || params.tuneHz > 74.0f
            || params.pitchDropSemitones < 10.0f
            || params.pitchDropSemitones > 44.0f
            || params.pitchSweepMs < 12.0f || params.pitchSweepMs > 150.0f
            || params.pitchSettle < 0.05f || params.pitchSettle > 0.72f
            || params.body < 0.12f || params.body > 0.72f
            || params.harmonics < 0.015f || params.harmonics > 0.58f
            || params.decaySeconds < 0.20f || params.decaySeconds > 2.40f
            || params.tail < 0.02f || params.tail > 0.72f
            || params.punch < 0.38f || params.punch > 0.98f
            || params.click < 0.015f || params.click > 0.48f
            || params.clickTone < 0.22f || params.clickTone > 0.90f
            || params.clickDecayMs < 1.4f || params.clickDecayMs > 15.0f
            || params.texture < 0.005f || params.texture > 0.26f
            || params.textureTone < 0.12f || params.textureTone > 0.84f
            || params.textureDecaySeconds < 0.025f
            || params.textureDecaySeconds > 0.65f
            || params.character.drive < 0.0f
            || params.character.drive > 0.52f
            || std::abs(params.character.bias)
                > 0.04001f + params.character.drive * 0.50001f
            || params.character.compression < 0.0f
            || params.character.compression > 0.50f
            || params.character.sampleRateReduction < 0.0f
            || params.character.sampleRateReduction > 0.58f
            || params.character.bitDepthReduction < 0.0f
            || params.character.bitDepthReduction > 0.52f
            || params.character.reconstruction < 0.0f
            || params.character.reconstruction > 0.52f
            || params.character.tone < -0.42f
            || params.character.tone > 0.42f
            || params.stereoWidth < 0.0f || params.stereoWidth > 0.85f) {
            std::cerr << "safe RANDOM range/finite contract failed at "
                      << index << "\n";
            return false;
        }
        const bool clean = params.character.sampleRateReduction == 0.0f;
        if (clean) {
            ++cleanConversions;
            if (params.character.bitDepthReduction != 0.0f
                || params.character.reconstruction != 0.0f) {
                std::cerr << "clean RANDOM conversion was not correlated\n";
                return false;
            }
        }
        if (params.decaySeconds > 1.56f
            && (params.tuneHz > 49.0f || params.tail < 0.32f
                || params.punch > 0.72f)) {
            std::cerr << "long RANDOM kick lost its deep-tail correlation\n";
            return false;
        }
        if (index > 0u && !sameKickParams(params, previous)) {
            ++distinctFromPrevious;
        }
        previous = params;

        const auto rendered = render(params, 48000.0, 0.92f, 36, 0.32);
        const double signalEnergy = energy(rendered.left)
            + energy(rendered.right);
        float peak = 0.0f;
        for (uint32_t frame = 0u; frame < rendered.left.size(); ++frame) {
            const float left = rendered.left[frame];
            const float right = rendered.right[frame];
            if (!std::isfinite(left) || !std::isfinite(right)) {
                std::cerr << "safe RANDOM render became non-finite at "
                          << index << "\n";
                return false;
            }
            peak = std::max({ peak, std::abs(left), std::abs(right) });
        }
        maximumPeak = std::max(maximumPeak, peak);
        if (!(signalEnergy > 1.0e-4) || peak > 2.0f) {
            std::cerr << "safe RANDOM render energy/peak failed at " << index
                      << ": " << signalEnergy << " / " << peak << "\n";
            return false;
        }
    }
    if (distinctFromPrevious < kPatchCount - 2u
        || cleanConversions < 24u || cleanConversions > 64u) {
        std::cerr << "safe RANDOM diversity/clean probability failed: "
                  << distinctFromPrevious << " / " << cleanConversions
                  << "\n";
        return false;
    }
    std::cout << "safe RANDOM max peak=" << maximumPeak
              << ", clean conversions=" << cleanConversions << "\n";
    return true;
}

bool presetsAndSampleRatesProbe()
{
    std::vector<std::string> names;
    std::vector<std::vector<float>> fingerprints;
    for (uint32_t index = 0u;
         index < s3g::kDrumKickFactoryPresetCount; ++index) {
        const auto& info = s3g::drumKickFactoryPresetInfo(index);
        const auto params = s3g::drumKickFactoryPreset(index);
        if (!info.name || !info.description || info.name[0] == '\0'
            || info.description[0] == '\0'
            || std::find(names.begin(), names.end(), info.name) != names.end()
            || s3g::drumKickFactoryPresetIndex(params)
                != static_cast<int32_t>(index)) {
            std::cerr << "factory preset metadata/round trip failed at "
                      << index << "\n";
            return false;
        }
        names.emplace_back(info.name);
        const auto rendered = render(params, 48000.0, 0.93f, 36, 0.35);
        const double signature = energy(rendered.left)
            + 0.37 * differenceEnergy(rendered.left, rendered.right);
        if (!std::isfinite(signature) || !(signature > 0.001)) {
            std::cerr << "factory preset was invalid/silent at " << index << "\n";
            return false;
        }
        for (const auto& previous : fingerprints) {
            double difference = 0.0;
            for (uint32_t frame = 0u; frame < rendered.left.size(); ++frame) {
                const double delta = static_cast<double>(rendered.left[frame])
                    - previous[frame];
                difference += delta * delta;
            }
            if (!(difference > 1.0e-4)) {
                std::cerr << "factory presets were not sonically distinct at "
                          << index << "\n";
                return false;
            }
        }
        fingerprints.push_back(rendered.left);
    }

    auto edited = s3g::drumKickFactoryPreset(0u);
    edited.tuneHz += 0.1f;
    if (s3g::drumKickFactoryPresetIndex(edited) != -1) {
        std::cerr << "edited settings incorrectly matched a factory preset\n";
        return false;
    }
    edited = s3g::drumKickFactoryPreset(0u);
    edited.outputGainDb += 3.0f;
    if (s3g::drumKickFactoryPresetIndex(edited) != 0) {
        std::cerr << "output trim incorrectly changed the voice preset\n";
        return false;
    }

    for (double sampleRate : { 44100.0, 48000.0, 96000.0 }) {
        const auto rendered = render({}, sampleRate, 1.0f, 36, 0.35);
        const double signalEnergy = energy(rendered.left);
        if (!std::isfinite(signalEnergy) || !(signalEnergy > 0.01)) {
            std::cerr << "sample-rate render failed at " << sampleRate << "\n";
            return false;
        }
        for (float sample : rendered.left) {
            if (!std::isfinite(sample)) {
                std::cerr << "non-finite sample at " << sampleRate << "\n";
                return false;
            }
        }
    }
    return true;
}

} // namespace

int main()
{
    if (!silenceAndSafetyProbe()
        || !defaultVoiceProbe()
        || !stereoContractProbe()
        || !pitchAndTrackingProbe()
        || !envelopeAndVelocityProbe()
        || !polyphonyAndLifecycleProbe()
        || !characterAndResetProbe()
        || !safeRandomProbe()
        || !presetsAndSampleRatesProbe()) {
        return 1;
    }
    std::cout << "drum kick smoke passed\n";
    return 0;
}
