#include "s3g_drum_snare.h"
#include "s3g_drum_snare_presets.h"

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

StereoRender render(const s3g::DrumSnareParams& params,
    double sampleRate = 48000.0, float velocity = 1.0f,
    int midiNote = 38, double seconds = 1.0)
{
    s3g::DrumSnare snare;
    snare.prepare(sampleRate);
    snare.setParams(params);
    snare.reset();
    snare.trigger(velocity, midiNote);
    const uint32_t frames = static_cast<uint32_t>(sampleRate * seconds);
    StereoRender result { std::vector<float>(frames),
        std::vector<float>(frames) };
    snare.processBlock(result.left.data(), result.right.data(), frames);
    return result;
}

std::array<float, 26u> paramVector(const s3g::DrumSnareParams& params)
{
    return {{
        params.tuneHz,
        params.noteTracking,
        params.pitchDropSemitones,
        params.pitchSweepMs,
        params.shellSpread,
        params.body,
        params.ring,
        params.bodyDecaySeconds,
        params.punch,
        params.wires,
        params.wireTone,
        params.wireTension,
        params.wireDecaySeconds,
        params.rattle,
        params.click,
        params.clickTone,
        params.character.drive,
        params.character.bias,
        params.character.compression,
        params.character.sampleRateReduction,
        params.character.bitDepthReduction,
        params.character.reconstruction,
        params.character.tone,
        params.stereoWidth,
        params.velocitySensitivity,
        params.outputGainDb,
    }};
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
    const std::vector<float>& right, uint32_t begin = 0u,
    uint32_t end = std::numeric_limits<uint32_t>::max())
{
    end = std::min<uint32_t>(end, static_cast<uint32_t>(
        std::min(left.size(), right.size())));
    begin = std::min(begin, end);
    double result = 0.0;
    for (uint32_t frame = begin; frame < end; ++frame) {
        const double difference = static_cast<double>(left[frame])
            - right[frame];
        result += difference * difference;
    }
    return result;
}

double renderDifference(const std::vector<float>& left,
    const std::vector<float>& right, uint32_t begin = 0u,
    uint32_t end = std::numeric_limits<uint32_t>::max())
{
    return differenceEnergy(left, right, begin, end);
}

double firstDifferenceBrightness(const std::vector<float>& signal,
    uint32_t begin, uint32_t end)
{
    end = std::min<uint32_t>(end, static_cast<uint32_t>(signal.size()));
    begin = std::min(begin, end);
    double differences = 0.0;
    double total = 0.0;
    for (uint32_t frame = std::max<uint32_t>(begin + 1u, 1u);
         frame < end; ++frame) {
        const double current = signal[frame];
        const double delta = current - signal[frame - 1u];
        differences += delta * delta;
        total += current * current;
    }
    return differences / std::max(total, 1.0e-20);
}

double goertzelPower(const std::vector<float>& signal, double sampleRate,
    uint32_t begin, uint32_t end, double frequency)
{
    end = std::min<uint32_t>(end, static_cast<uint32_t>(signal.size()));
    begin = std::min(begin, end);
    const double coefficient = 2.0 * std::cos(
        2.0 * static_cast<double>(s3g::kPi) * frequency / sampleRate);
    double previous = 0.0;
    double previousTwo = 0.0;
    for (uint32_t frame = begin; frame < end; ++frame) {
        const double next = signal[frame] + coefficient * previous
            - previousTwo;
        previousTwo = previous;
        previous = next;
    }
    return previous * previous + previousTwo * previousTwo
        - coefficient * previous * previousTwo;
}

double dominantFrequency(const std::vector<float>& signal,
    double sampleRate, double beginSeconds, double endSeconds,
    double lowHz, double highHz, double stepHz = 2.0)
{
    const uint32_t begin = std::min<uint32_t>(
        static_cast<uint32_t>(beginSeconds * sampleRate),
        static_cast<uint32_t>(signal.size()));
    const uint32_t end = std::min<uint32_t>(
        static_cast<uint32_t>(endSeconds * sampleRate),
        static_cast<uint32_t>(signal.size()));
    double bestFrequency = lowHz;
    double bestPower = -1.0;
    for (double frequency = lowHz; frequency <= highHz;
         frequency += stepHz) {
        const double power = goertzelPower(signal, sampleRate, begin, end,
            frequency);
        if (power > bestPower) {
            bestPower = power;
            bestFrequency = frequency;
        }
    }
    return bestFrequency;
}

double magnitudeSpectralCentroid(const std::vector<float>& signal,
    double sampleRate, double beginSeconds, double endSeconds,
    double lowHz = 50.0, double highHz = 20000.0,
    double stepHz = 50.0)
{
    const uint32_t begin = std::min<uint32_t>(
        static_cast<uint32_t>(beginSeconds * sampleRate),
        static_cast<uint32_t>(signal.size()));
    const uint32_t end = std::min<uint32_t>(
        static_cast<uint32_t>(endSeconds * sampleRate),
        static_cast<uint32_t>(signal.size()));
    double weighted = 0.0;
    double magnitudeSum = 0.0;
    for (double frequency = lowHz; frequency <= highHz;
         frequency += stepHz) {
        const double magnitude = std::sqrt(std::max(0.0,
            goertzelPower(signal, sampleRate, begin, end, frequency)));
        weighted += frequency * magnitude;
        magnitudeSum += magnitude;
    }
    return weighted / std::max(magnitudeSum, 1.0e-20);
}

bool silenceAndSafetyProbe()
{
    s3g::DrumSnare snare;
    snare.prepare(std::numeric_limits<double>::quiet_NaN());
    s3g::DrumSnareParams invalid;
    invalid.tuneHz = std::numeric_limits<float>::infinity();
    invalid.noteTracking = -8.0f;
    invalid.pitchDropSemitones = 400.0f;
    invalid.pitchSweepMs = std::numeric_limits<float>::quiet_NaN();
    invalid.bodyDecaySeconds = -1.0f;
    invalid.wires = 8.0f;
    invalid.wireDecaySeconds = -2.0f;
    invalid.character.bias = 99.0f;
    invalid.outputGainDb = 200.0f;
    snare.setParams(invalid);
    snare.reset();

    const auto sanitized = snare.params();
    if (sanitized.tuneHz != 180.0f || sanitized.noteTracking != 0.0f
        || sanitized.pitchDropSemitones != 36.0f
        || sanitized.pitchSweepMs != 22.0f
        || sanitized.bodyDecaySeconds != 0.02f
        || sanitized.wires != 1.0f
        || sanitized.wireDecaySeconds != 0.01f
        || sanitized.character.bias != 1.0f
        || sanitized.outputGainDb != 12.0f) {
        std::cerr << "snare parameter sanitation failed\n";
        return false;
    }

    auto maximumTune = sanitized;
    maximumTune.tuneHz = 999.0f;
    snare.setParams(maximumTune);
    if (snare.params().tuneHz != 420.0f) {
        std::cerr << "snare tune ceiling did not include the corpus p90\n";
        return false;
    }
    snare.reset();

    for (uint32_t frame = 0u; frame < 4096u; ++frame) {
        float left = 1.0f;
        float right = -1.0f;
        snare.processFrame(left, right);
        if (left != 0.0f || right != 0.0f) {
            std::cerr << "untriggered snare was not silent\n";
            return false;
        }
    }

    snare.trigger(std::numeric_limits<float>::infinity(), 2000);
    for (uint32_t frame = 0u; frame < 300000u; ++frame) {
        float left = 0.0f;
        float right = 0.0f;
        snare.processFrame(left, right);
        if (!std::isfinite(left) || !std::isfinite(right)
            || std::abs(left) > 3.0f || std::abs(right) > 3.0f) {
            std::cerr << "snare safety bound failed at frame " << frame
                      << "\n";
            return false;
        }
    }
    return true;
}

bool defaultAndTuningProbe()
{
    const auto defaultHit = render({}, 48000.0, 1.0f, 38, 1.0);
    if (!(energy(defaultHit.left) > 1.0)) {
        std::cerr << "default snare lacked energy\n";
        return false;
    }

    s3g::DrumSnareParams tonal;
    tonal.tuneHz = 145.0f;
    tonal.noteTracking = 1.0f;
    tonal.pitchDropSemitones = 0.0f;
    tonal.body = 1.0f;
    tonal.ring = 0.0f;
    tonal.bodyDecaySeconds = 1.5f;
    tonal.punch = 0.0f;
    tonal.wires = 0.0f;
    tonal.click = 0.0f;
    tonal.outputGainDb = -12.0f;
    const auto low = render(tonal, 48000.0, 1.0f, 38, 0.9);
    const auto high = render(tonal, 48000.0, 1.0f, 50, 0.9);
    const double lowHz = dominantFrequency(low.left, 48000.0,
        0.35, 0.80, 80.0, 500.0);
    const double highHz = dominantFrequency(high.left, 48000.0,
        0.35, 0.80, 100.0, 700.0);
    if (!(lowHz > 138.0 && lowHz < 152.0)
        || !(highHz / lowHz > 1.90 && highHz / lowHz < 2.10)) {
        std::cerr << "snare tune/note tracking failed: " << lowHz
                  << " / " << highHz << "\n";
        return false;
    }
    return true;
}

bool pitchGestureProbe()
{
    s3g::DrumSnareParams flat;
    flat.tuneHz = 155.0f;
    flat.pitchDropSemitones = 0.0f;
    flat.pitchSweepMs = 85.0f;
    flat.shellSpread = 0.0f;
    flat.body = 0.7f;
    flat.ring = 0.0f;
    flat.bodyDecaySeconds = 0.8f;
    flat.punch = 1.0f;
    flat.wires = 0.0f;
    flat.click = 0.0f;
    flat.outputGainDb = -9.0f;
    auto dropped = flat;
    dropped.pitchDropSemitones = 24.0f;
    const auto noGesture = render(flat, 48000.0, 1.0f, 38, 0.7);
    const auto gesture = render(dropped, 48000.0, 1.0f, 38, 0.7);
    const double earlyDifference = renderDifference(noGesture.left,
        gesture.left, 0u, 7200u);
    const double lateDifference = renderDifference(noGesture.left,
        gesture.left, 16800u, 31200u);
    if (!(earlyDifference > 0.02)
        || !(earlyDifference > lateDifference * 100.0 + 0.01)) {
        std::cerr << "pitch drop did not create an early settling gesture: "
                  << earlyDifference << " / " << lateDifference << "\n";
        return false;
    }
    return true;
}

bool isolatedImpactLifecycleProbe()
{
    s3g::DrumSnare snare;
    snare.prepare(48000.0);
    s3g::DrumSnareParams params;
    params.tuneHz = 180.0f;
    params.pitchDropSemitones = 30.0f;
    params.pitchSweepMs = 250.0f;
    params.body = 0.0f;
    params.ring = 0.0f;
    params.bodyDecaySeconds = 0.02f;
    params.punch = 0.0f;
    params.wires = 0.0f;
    params.click = 0.0f;
    params.outputGainDb = 0.0f;
    snare.setParams(params);
    snare.reset();
    snare.trigger();

    double lateImpactEnergy = 0.0;
    for (uint32_t frame = 0u; frame < 9600u; ++frame) {
        float left = 0.0f;
        float right = 0.0f;
        snare.processFrame(left, right);
        if (frame >= 7200u) {
            lateImpactEnergy += static_cast<double>(left) * left;
        }
    }
    if (!snare.active() || !(lateImpactEnergy > 1.0e-7)) {
        std::cerr << "long zero-punch impact was cut by shell activity: "
                  << snare.active() << ", " << lateImpactEnergy << "\n";
        return false;
    }
    for (uint32_t frame = 0u; frame < 24000u; ++frame) {
        float left = 0.0f;
        float right = 0.0f;
        snare.processFrame(left, right);
    }
    if (snare.active()) {
        std::cerr << "isolated impact did not finish its bounded tail\n";
        return false;
    }
    return true;
}

bool shellAndWireControlProbe()
{
    s3g::DrumSnareParams shortBody;
    shortBody.wires = 0.0f;
    shortBody.click = 0.0f;
    shortBody.ring = 0.0f;
    shortBody.bodyDecaySeconds = 0.08f;
    auto longBody = shortBody;
    longBody.ring = 0.85f;
    longBody.shellSpread = 0.92f;
    longBody.bodyDecaySeconds = 1.2f;
    const auto shortHit = render(shortBody, 48000.0, 1.0f, 38, 1.0);
    const auto longHit = render(longBody, 48000.0, 1.0f, 38, 1.0);
    if (!(energy(longHit.left, 24000u) >
            energy(shortHit.left, 24000u) * 100.0 + 0.01)) {
        std::cerr << "body decay/ring did not extend shell modes\n";
        return false;
    }
    auto compactModes = longBody;
    compactModes.shellSpread = 0.0f;
    const auto compact = render(compactModes, 48000.0, 1.0f, 38, 0.5);
    const auto spread = render(longBody, 48000.0, 1.0f, 38, 0.5);
    if (!(renderDifference(compact.left, spread.left) > 0.1)) {
        std::cerr << "shell spread did not alter modal structure\n";
        return false;
    }

    s3g::DrumSnareParams wires;
    wires.body = 0.0f;
    wires.ring = 0.0f;
    wires.bodyDecaySeconds = 0.02f;
    wires.punch = 0.0f;
    wires.click = 0.0f;
    wires.wires = 1.0f;
    wires.wireDecaySeconds = 0.65f;
    wires.rattle = 0.0f;
    wires.wireTone = 0.05f;
    const auto dark = render(wires, 48000.0, 1.0f, 38, 0.65);
    wires.wireTone = 0.95f;
    const auto bright = render(wires, 48000.0, 1.0f, 38, 0.65);
    const double darkBrightness = firstDifferenceBrightness(
        dark.left, 1200u, 24000u);
    const double brightBrightness = firstDifferenceBrightness(
        bright.left, 1200u, 24000u);
    if (!(brightBrightness > darkBrightness * 1.25)) {
        std::cerr << "wire tone did not raise spectral brightness: "
                  << darkBrightness << " / " << brightBrightness << "\n";
        return false;
    }

    auto slack = wires;
    slack.wireTension = 0.0f;
    auto taut = wires;
    taut.wireTension = 1.0f;
    const auto slackHit = render(slack, 48000.0, 1.0f, 38, 0.5);
    const auto tautHit = render(taut, 48000.0, 1.0f, 38, 0.5);
    if (!(renderDifference(slackHit.left, tautHit.left) > 0.05)) {
        std::cerr << "wire tension did not alter the wire response\n";
        return false;
    }

    auto plain = wires;
    plain.wireDecaySeconds = 0.24f;
    plain.rattle = 0.0f;
    auto rattling = plain;
    rattling.rattle = 1.0f;
    const auto plainHit = render(plain, 48000.0, 1.0f, 38, 0.9);
    const auto rattleHit = render(rattling, 48000.0, 1.0f, 38, 0.9);
    const double plainLate = energy(plainHit.left, 19200u);
    const double rattleLate = energy(rattleHit.left, 19200u);
    if (!(rattleLate > plainLate * 8.0 + 0.001)) {
        std::cerr << "rattle did not create an independent late wire tail: "
                  << plainLate << " / " << rattleLate << "\n";
        return false;
    }
    return true;
}

bool wireCentroidProbe()
{
    s3g::DrumSnareParams params;
    params.body = 0.0f;
    params.ring = 0.0f;
    params.bodyDecaySeconds = 0.02f;
    params.punch = 0.0f;
    params.wires = 1.0f;
    params.wireTension = 0.48f;
    params.wireDecaySeconds = 0.60f;
    params.rattle = 0.0f;
    params.click = 0.0f;
    params.stereoWidth = 1.0f;
    params.outputGainDb = -6.0f;

    std::array<double, 3u> centroids {};
    const std::array<float, 3u> tones {{ 0.0f, 0.5f, 1.0f }};
    for (uint32_t index = 0u; index < tones.size(); ++index) {
        params.wireTone = tones[index];
        const auto rendered = render(params, 48000.0, 1.0f, 38, 0.40);
        std::vector<float> isolatedSide(rendered.left.size());
        for (uint32_t frame = 0u; frame < isolatedSide.size(); ++frame) {
            isolatedSide[frame] = (rendered.left[frame]
                - rendered.right[frame]) * 0.5f;
        }
        centroids[index] = magnitudeSpectralCentroid(isolatedSide,
            48000.0, 0.04, 0.34);
    }

    if (!(centroids[0u] > 2250.0 && centroids[0u] < 2650.0)
        || !(centroids[1u] > 4250.0 && centroids[1u] < 4850.0)
        || !(centroids[2u] > 8800.0 && centroids[2u] < 10050.0)
        || !(centroids[0u] < centroids[1u]
            && centroids[1u] < centroids[2u])) {
        std::cerr << "wire magnitude centroids missed corpus brackets: "
                  << centroids[0u] << ", " << centroids[1u] << ", "
                  << centroids[2u] << " Hz\n";
        return false;
    }
    std::cout << "wire magnitude centroids: " << centroids[0u] << ", "
              << centroids[1u] << ", " << centroids[2u] << " Hz\n";
    return true;
}

bool stereoContractProbe()
{
    s3g::DrumSnareParams centered;
    centered.stereoWidth = 0.0f;
    centered.wires = 1.0f;
    centered.rattle = 0.7f;
    const auto mono = render(centered, 48000.0, 1.0f, 38, 0.6);
    if (mono.left != mono.right) {
        std::cerr << "width zero did not produce exact dual mono\n";
        return false;
    }

    auto shellOnly = centered;
    shellOnly.stereoWidth = 1.0f;
    shellOnly.wires = 0.0f;
    shellOnly.click = 0.8f;
    const auto centeredShell = render(shellOnly, 48000.0, 1.0f, 38, 0.6);
    if (centeredShell.left != centeredShell.right) {
        std::cerr << "shell/impact/click leaked into the side channel\n";
        return false;
    }

    auto wide = centered;
    wide.stereoWidth = 1.0f;
    const auto spread = render(wide, 48000.0, 1.0f, 38, 0.6);
    const double sideEnergy = differenceEnergy(spread.left, spread.right);
    const double total = energy(spread.left) + energy(spread.right);
    if (!(sideEnergy > total * 1.0e-4)) {
        std::cerr << "wire width did not create stereo energy\n";
        return false;
    }

    // Build unequal character histories, then demand an immediate exact mono
    // contract when width is changed live.
    s3g::DrumSnare transition;
    transition.prepare(48000.0);
    wide.character.drive = 0.62f;
    wide.character.bias = 0.25f;
    wide.character.compression = 0.4f;
    wide.character.reconstruction = 0.3f;
    transition.setParams(wide);
    transition.reset();
    transition.trigger();
    for (uint32_t frame = 0u; frame < 2048u; ++frame) {
        float left = 0.0f;
        float right = 0.0f;
        transition.processFrame(left, right);
    }
    wide.stereoWidth = 0.0f;
    transition.setParams(wide);
    for (uint32_t frame = 0u; frame < 8192u; ++frame) {
        float left = 0.0f;
        float right = 0.0f;
        transition.processFrame(left, right);
        if (left != right) {
            std::cerr << "wide-to-zero transition was not exact dual mono\n";
            return false;
        }
    }

    s3g::DrumSnare monoPath;
    monoPath.prepare(48000.0);
    monoPath.setParams(centered);
    monoPath.reset();
    monoPath.trigger();
    std::array<float, 1024u> block {};
    monoPath.processBlock(block.data(), nullptr,
        static_cast<uint32_t>(block.size()));
    double blockEnergy = 0.0;
    for (float sample : block) blockEnergy += sample * sample;
    if (!(blockEnergy > 0.01)) {
        std::cerr << "mono output path was silent\n";
        return false;
    }
    return true;
}

bool velocityAndLatchProbe()
{
    s3g::DrumSnare silentTrigger;
    silentTrigger.prepare(48000.0);
    s3g::DrumSnareParams silentParams;
    silentParams.velocitySensitivity = 1.0f;
    silentParams.bodyDecaySeconds = 3.0f;
    silentParams.wireDecaySeconds = 4.0f;
    silentParams.rattle = 1.0f;
    silentTrigger.setParams(silentParams);
    silentTrigger.reset();
    silentTrigger.trigger(0.0f, 38);
    if (silentTrigger.active()) {
        std::cerr << "silent zero-velocity trigger consumed a long voice\n";
        return false;
    }
    for (uint32_t frame = 0u; frame < 1024u; ++frame) {
        float left = 1.0f;
        float right = -1.0f;
        silentTrigger.processFrame(left, right);
        if (left != 0.0f || right != 0.0f) {
            std::cerr << "zero-velocity trigger produced output\n";
            return false;
        }
    }

    s3g::DrumSnareParams params;
    params.velocitySensitivity = 1.0f;
    const auto quiet = render(params, 48000.0, 0.2f, 38, 0.5);
    const auto loud = render(params, 48000.0, 1.0f, 38, 0.5);
    if (!(energy(loud.left) > energy(quiet.left) * 8.0)) {
        std::cerr << "velocity sensitivity did not control energy\n";
        return false;
    }
    params.velocitySensitivity = 0.0f;
    const auto fixedQuiet = render(params, 48000.0, 0.2f, 38, 0.5);
    const auto fixedLoud = render(params, 48000.0, 1.0f, 38, 0.5);
    if (fixedQuiet.left != fixedLoud.left
        || fixedQuiet.right != fixedLoud.right) {
        std::cerr << "zero velocity sensitivity was not fixed/deterministic\n";
        return false;
    }

    s3g::DrumSnare baseline;
    s3g::DrumSnare changed;
    baseline.prepare(48000.0);
    changed.prepare(48000.0);
    s3g::DrumSnareParams original;
    original.bodyDecaySeconds = 1.0f;
    original.wireDecaySeconds = 0.8f;
    original.stereoWidth = 0.0f;
    baseline.setParams(original);
    changed.setParams(original);
    baseline.reset();
    changed.reset();
    baseline.trigger(0.9f, 38);
    changed.trigger(0.9f, 38);
    double existingVoiceDifference = 0.0;
    double newHitDifference = 0.0;
    for (uint32_t frame = 0u; frame < 18000u; ++frame) {
        if (frame == 512u) {
            auto locked = original;
            locked.tuneHz = 330.0f;
            locked.shellSpread = 1.0f;
            locked.body = 0.05f;
            locked.ring = 0.95f;
            locked.wires = 0.02f;
            locked.wireTone = 1.0f;
            locked.wireTension = 1.0f;
            locked.rattle = 0.0f;
            locked.click = 0.9f;
            changed.setParams(locked);
        }
        if (frame == 5000u) {
            baseline.trigger(0.9f, 38);
            changed.trigger(0.9f, 38);
        }
        float baselineLeft = 0.0f;
        float baselineRight = 0.0f;
        float changedLeft = 0.0f;
        float changedRight = 0.0f;
        baseline.processFrame(baselineLeft, baselineRight);
        changed.processFrame(changedLeft, changedRight);
        const double delta = static_cast<double>(baselineLeft) - changedLeft;
        if (frame < 5000u) existingVoiceDifference += delta * delta;
        else newHitDifference += delta * delta;
    }
    if (existingVoiceDifference != 0.0 || !(newHitDifference > 0.1)) {
        std::cerr << "per-hit parameter latching failed: "
                  << existingVoiceDifference << " / " << newHitDifference
                  << "\n";
        return false;
    }
    return true;
}

bool polyphonyLifecycleAndResetProbe()
{
    s3g::DrumSnare snare;
    snare.prepare(48000.0);
    s3g::DrumSnareParams params;
    params.bodyDecaySeconds = 0.22f;
    params.wireDecaySeconds = 0.28f;
    snare.setParams(params);
    snare.reset();
    for (uint32_t hit = 0u; hit < s3g::DrumSnare::kVoiceCount + 7u; ++hit) {
        snare.trigger(0.35f + 0.025f * static_cast<float>(hit),
            31 + static_cast<int>(hit));
    }
    if (!snare.active()) {
        std::cerr << "snare roll did not activate the voice pool\n";
        return false;
    }
    double rollEnergy = 0.0;
    for (uint32_t frame = 0u; frame < 180000u; ++frame) {
        if (frame == 240u || frame == 560u || frame == 1100u) {
            snare.trigger(0.95f, 38);
        }
        float left = 0.0f;
        float right = 0.0f;
        snare.processFrame(left, right);
        rollEnergy += static_cast<double>(left) * left;
        if (!std::isfinite(left) || !std::isfinite(right)) {
            std::cerr << "roll/flam render became non-finite\n";
            return false;
        }
    }
    if (!(rollEnergy > 1.0) || snare.active()) {
        std::cerr << "voice stealing or short lifecycle failed\n";
        return false;
    }

    s3g::DrumSnare longWire;
    longWire.prepare(48000.0);
    params = {};
    params.bodyDecaySeconds = 0.02f;
    params.body = 0.0f;
    params.ring = 0.0f;
    params.punch = 0.0f;
    params.click = 0.0f;
    params.wires = 1.0f;
    params.wireDecaySeconds = 0.40f;
    params.rattle = 1.0f;
    longWire.setParams(params);
    longWire.reset();
    longWire.trigger();
    double lateEnergy = 0.0;
    for (uint32_t frame = 0u; frame < 67200u; ++frame) {
        float left = 0.0f;
        float right = 0.0f;
        longWire.processFrame(left, right);
        if (frame >= 57600u) lateEnergy += static_cast<double>(left) * left;
    }
    if (!longWire.active() || !(lateEnergy > 5.0e-7)) {
        std::cerr << "long rattle tail was cut off by the body lifetime: "
                  << longWire.active() << ", " << lateEnergy << "\n";
        return false;
    }
    for (uint32_t frame = 0u; frame < 96000u; ++frame) {
        float left = 0.0f;
        float right = 0.0f;
        longWire.processFrame(left, right);
    }
    if (longWire.active()) {
        std::cerr << "longest snare tail never became inactive\n";
        return false;
    }

    auto colored = s3g::drumSnareFactoryPreset(5u);
    const auto first = render(colored, 48000.0, 0.91f, 38, 0.5);
    const auto second = render(colored, 48000.0, 0.91f, 38, 0.5);
    auto cleanParams = colored;
    cleanParams.character = {};
    const auto clean = render(cleanParams, 48000.0, 0.91f, 38, 0.5);
    if (first.left != second.left || first.right != second.right
        || !(renderDifference(first.left, clean.left) > 0.05)) {
        std::cerr << "reset determinism/shared character failed\n";
        return false;
    }
    return true;
}

bool rimAndPresetProbe()
{
    std::vector<std::string> names;
    std::vector<std::vector<float>> fingerprints;
    for (uint32_t index = 0u;
         index < s3g::kDrumSnareFactoryPresetCount; ++index) {
        const auto& info = s3g::drumSnareFactoryPresetInfo(index);
        const auto params = s3g::drumSnareFactoryPreset(index);
        if (!info.name || !info.description || info.name[0] == '\0'
            || info.description[0] == '\0'
            || std::find(names.begin(), names.end(), info.name) != names.end()
            || s3g::drumSnareFactoryPresetIndex(params)
                != static_cast<int32_t>(index)) {
            std::cerr << "snare preset metadata/round trip failed at "
                      << index << "\n";
            return false;
        }
        names.emplace_back(info.name);
        const auto rendered = render(params, 48000.0, 0.93f, 38, 0.45);
        const double signature = energy(rendered.left)
            + 0.37 * differenceEnergy(rendered.left, rendered.right);
        if (!std::isfinite(signature) || !(signature > 0.001)) {
            std::cerr << "snare factory preset was invalid/silent at "
                      << index << "\n";
            return false;
        }
        for (const auto& previous : fingerprints) {
            if (!(renderDifference(rendered.left, previous) > 1.0e-4)) {
                std::cerr << "snare presets were not sonically distinct at "
                          << index << "\n";
                return false;
            }
        }
        fingerprints.push_back(rendered.left);
    }

    const std::array<std::array<double, 2u>, 2u> modalBands {{
        {{ 400.0, 450.0 }},
        {{ 735.0, 810.0 }},
    }};
    for (uint32_t preset = 0u; preset < modalBands.size(); ++preset) {
        const uint32_t index = preset + 12u;
        auto rim = s3g::drumSnareFactoryPreset(index);
        if (!(rim.wires <= 0.025f)) {
            std::cerr << "rim/stick preset wire amount was not low\n";
            return false;
        }
        rim.stereoWidth = 1.0f;
        const auto rendered = render(rim, 48000.0, 1.0f, 38, 0.5);
        const double dominant = dominantFrequency(rendered.left, 48000.0,
            0.14, 0.30, 180.0, 1800.0, 2.0);
        const double sideRatio = differenceEnergy(
            rendered.left, rendered.right)
            / std::max(energy(rendered.left) + energy(rendered.right),
                1.0e-20);
        if (!(dominant > modalBands[preset][0u]
                && dominant < modalBands[preset][1u])
            || !(sideRatio < 0.005)) {
            std::cerr << "rim/stick modal or low-wire contract failed at "
                      << index << ": " << dominant << ", " << sideRatio
                      << "\n";
            return false;
        }
        std::cout << s3g::drumSnareFactoryPresetInfo(index).name
                  << " late dominant: " << dominant << " Hz\n";
    }

    auto edited = s3g::drumSnareFactoryPreset(0u);
    edited.tuneHz += 0.1f;
    if (s3g::drumSnareFactoryPresetIndex(edited) != -1) {
        std::cerr << "edited snare incorrectly matched a preset\n";
        return false;
    }
    edited = s3g::drumSnareFactoryPreset(0u);
    edited.outputGainDb += 3.0f;
    if (s3g::drumSnareFactoryPresetIndex(edited) != 0) {
        std::cerr << "output trim incorrectly changed snare preset identity\n";
        return false;
    }
    return true;
}

bool safeRandomProbe()
{
    s3g::DrumSnareParams current;
    current.noteTracking = 0.371f;
    current.velocitySensitivity = 0.427f;
    current.outputGainDb = -17.25f;

    constexpr uint32_t kSeed = 0x5a17c9e3u;
    s3g::DrumRandom firstRandom(kSeed);
    s3g::DrumRandom secondRandom(kSeed);
    const auto first = s3g::drumSnareSafeRandomParams(
        current, firstRandom);
    const auto second = s3g::drumSnareSafeRandomParams(
        current, secondRandom);
    uint32_t state = kSeed;
    const auto fromState = s3g::drumSnareSafeRandomParams(current, state);
    if (paramVector(first) != paramVector(second)
        || paramVector(first) != paramVector(fromState)
        || firstRandom.state() != secondRandom.state()
        || state != firstRandom.state()) {
        std::cerr << "snare safe random was not seed deterministic\n";
        return false;
    }

    std::array<float, 26u> minimums {};
    std::array<float, 26u> maximums {};
    minimums.fill(std::numeric_limits<float>::max());
    maximums.fill(std::numeric_limits<float>::lowest());
    uint32_t randomState = 0x9e3779b9u;
    uint32_t rimLikeCount = 0u;
    std::array<float, 26u> previous {};
    uint32_t distinctVoices = 0u;
    for (uint32_t iteration = 0u; iteration < 512u; ++iteration) {
        const auto params = s3g::drumSnareSafeRandomParams(
            current, randomState);
        const auto values = paramVector(params);
        for (uint32_t index = 0u; index < values.size(); ++index) {
            if (!std::isfinite(values[index])) {
                std::cerr << "snare safe random produced a non-finite value\n";
                return false;
            }
            minimums[index] = std::min(minimums[index], values[index]);
            maximums[index] = std::max(maximums[index], values[index]);
        }
        if (iteration > 0u && values != previous) ++distinctVoices;
        previous = values;

        const bool inCuratedRanges =
            params.tuneHz >= 95.0f && params.tuneHz <= 395.0f
            && params.pitchDropSemitones >= 0.0f
            && params.pitchDropSemitones <= 18.0f
            && params.pitchSweepMs >= 2.5f
            && params.pitchSweepMs <= 63.0f
            && params.shellSpread >= 0.15f
            && params.shellSpread <= 1.0f
            && params.body >= 0.05f && params.body <= 0.94f
            && params.ring >= 0.04f && params.ring <= 0.85f
            && params.bodyDecaySeconds >= 0.07f
            && params.bodyDecaySeconds <= 1.10f
            && params.punch >= 0.45f && params.punch <= 1.0f
            && params.wires >= 0.01f && params.wires <= 0.95f
            && params.wireTone >= 0.18f && params.wireTone <= 0.98f
            && params.wireTension >= 0.15f
            && params.wireTension <= 0.98f
            && params.wireDecaySeconds >= 0.04f
            && params.wireDecaySeconds <= 0.92f
            && params.rattle >= 0.0f && params.rattle <= 0.56f
            && params.click >= 0.02f && params.click <= 0.90f
            && params.clickTone >= 0.25f && params.clickTone <= 1.0f
            && params.character.drive >= 0.01f
            && params.character.drive <= 0.45f
            && params.character.bias >= -0.22f
            && params.character.bias <= 0.22f
            && params.character.compression >= 0.01f
            && params.character.compression <= 0.55f
            && params.character.sampleRateReduction >= 0.0f
            && params.character.sampleRateReduction <= 0.45f
            && params.character.bitDepthReduction >= 0.0f
            && params.character.bitDepthReduction <= 0.36f
            && params.character.sampleRateReduction
                    + params.character.bitDepthReduction <= 0.580001f
            && params.character.reconstruction >= 0.01f
            && params.character.reconstruction <= 0.62f
            && params.character.tone >= -0.40f
            && params.character.tone <= 0.40f
            && params.stereoWidth >= 0.05f
            && params.stereoWidth <= 0.95f;
        if (!inCuratedRanges
            || params.noteTracking != current.noteTracking
            || params.velocitySensitivity != current.velocitySensitivity
            || params.outputGainDb != current.outputGainDb) {
            std::cerr << "snare safe random range/ownership failed at "
                      << iteration << "\n";
            return false;
        }

        if (params.wires < 0.10f) {
            ++rimLikeCount;
            if (!(params.tuneHz >= 230.0f && params.punch >= 0.76f
                    && params.click >= 0.35f
                    && params.body + params.ring + params.click >= 0.549f)) {
                std::cerr << "wire-light random snare lacked rim articulation\n";
                return false;
            }
        }
    }

    // Every randomized control must move over the deterministic corpus; the
    // protected indices are intentionally excluded.
    for (uint32_t index = 0u; index < minimums.size(); ++index) {
        if (index == 1u || index == 24u || index == 25u) continue;
        if (!(maximums[index] - minimums[index] > 1.0e-4f)) {
            std::cerr << "snare safe random lacked diversity at control "
                      << index << "\n";
            return false;
        }
    }
    if (distinctVoices != 511u || rimLikeCount == 0u
        || !(minimums[0u] < 105.0f && maximums[0u] > 370.0f)
        || !(minimums[9u] < 0.04f && maximums[9u] > 0.90f)
        || !(minimums[23u] < 0.08f && maximums[23u] > 0.90f)) {
        std::cerr << "snare safe random did not span its curated families\n";
        return false;
    }

    // Rendering keeps the user's default -6 dB trim. Sample-level safety,
    // non-silence, and a conservative peak ceiling are checked across all
    // four families and many character combinations.
    current = {};
    uint32_t renderState = 0x31415927u;
    for (uint32_t iteration = 0u; iteration < 64u; ++iteration) {
        const auto params = s3g::drumSnareSafeRandomParams(
            current, renderState);
        const auto rendered = render(params, 48000.0, 0.9f, 38, 0.32);
        double totalEnergy = 0.0;
        float peak = 0.0f;
        for (uint32_t frame = 0u; frame < rendered.left.size(); ++frame) {
            const float left = rendered.left[frame];
            const float right = rendered.right[frame];
            if (!std::isfinite(left) || !std::isfinite(right)) {
                std::cerr << "non-finite safe-random snare render\n";
                return false;
            }
            totalEnergy += static_cast<double>(left) * left
                + static_cast<double>(right) * right;
            peak = std::max(peak, std::max(std::abs(left), std::abs(right)));
        }
        if (!(totalEnergy > 1.0e-4) || !(peak < 2.0f)) {
            std::cerr << "unsafe/inaudible random snare render at "
                      << iteration << ": energy=" << totalEnergy
                      << ", peak=" << peak << "\n";
            return false;
        }
    }
    return true;
}

bool sampleRateProbe()
{
    for (double sampleRate : { 44100.0, 48000.0, 96000.0 }) {
        const auto rendered = render({}, sampleRate, 1.0f, 38, 0.4);
        const double signalEnergy = energy(rendered.left);
        if (!std::isfinite(signalEnergy) || !(signalEnergy > 0.01)) {
            std::cerr << "snare render failed at " << sampleRate << "\n";
            return false;
        }
        for (uint32_t frame = 0u; frame < rendered.left.size(); ++frame) {
            if (!std::isfinite(rendered.left[frame])
                || !std::isfinite(rendered.right[frame])) {
                std::cerr << "non-finite snare sample at " << sampleRate
                          << "\n";
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
        || !defaultAndTuningProbe()
        || !pitchGestureProbe()
        || !isolatedImpactLifecycleProbe()
        || !shellAndWireControlProbe()
        || !wireCentroidProbe()
        || !stereoContractProbe()
        || !velocityAndLatchProbe()
        || !polyphonyLifecycleAndResetProbe()
        || !rimAndPresetProbe()
        || !safeRandomProbe()
        || !sampleRateProbe()) {
        return 1;
    }
    std::cout << "drum snare smoke passed\n";
    return 0;
}
