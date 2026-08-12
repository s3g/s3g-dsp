#include "s3g_acapella_pvoc_field.h"
#include "s3g_acapella_vocal_fx.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <numeric>
#include <vector>

namespace {

constexpr uint32_t kSampleRate = 48000u;

struct StereoRender {
    std::vector<float> left;
    std::vector<float> right;
    std::vector<float> dryLeft;
    std::vector<float> dryRight;
    s3g::AcapellaPvocDiagnostics diagnostics {};
};

float voiceSignal(uint32_t frame, uint32_t sampleRate, float phaseOffset = 0.0f)
{
    const float time = static_cast<float>(frame)
        / static_cast<float>(sampleRate);
    const float fundamental = 118.0f + 22.0f
        * std::sin(2.0f * s3g::kPi * 0.31f * time);
    const float attack = std::min(1.0f, static_cast<float>(frame) / 720.0f);
    const float syllable = 0.72f + 0.28f * std::max(0.0f,
        std::sin(2.0f * s3g::kPi * 1.43f * time));
    return attack * syllable * (
        0.16f * std::sin(2.0f * s3g::kPi * fundamental * time + phaseOffset)
        + 0.078f * std::sin(2.0f * s3g::kPi * fundamental * 2.03f * time
            + phaseOffset * 0.7f)
        + 0.052f * std::sin(2.0f * s3g::kPi * fundamental * 3.97f * time
            - phaseOffset * 0.4f)
        + 0.024f * std::sin(2.0f * s3g::kPi * 2287.0f * time
            + phaseOffset * 1.3f));
}

s3g::AcapellaPvocGesture gestureAt(uint32_t frame, uint32_t activeFrames)
{
    s3g::AcapellaPvocGesture gesture;
    gesture.phoneme = s3g::AcapellaPhoneme::AA;
    gesture.frequencyHz = 118.0f;
    gesture.voiceInstance = 41u;
    gesture.active = frame < activeFrames;
    constexpr uint32_t stepFrames = 16800u;
    gesture.stepIndex = frame / stepFrames;
    gesture.stepProgress = static_cast<float>(frame % stepFrames)
        / static_cast<float>(stepFrames);
    if ((frame % stepFrames) < 320u) {
        gesture.flags = s3g::kAcapellaSyllableStart;
        if (gesture.stepIndex == 0u || gesture.stepIndex == 3u) {
            gesture.flags |= s3g::kAcapellaWordStart;
        }
    }
    if (!gesture.active) {
        gesture.phoneme = s3g::AcapellaPhoneme::Silence;
        gesture.flags = 0u;
    }
    return gesture;
}

StereoRender renderField(s3g::AcapellaPvocParams params,
    uint32_t totalFrames, uint32_t activeFrames, bool antiPhase = false,
    bool automate = false)
{
    s3g::AcapellaPvocField field;
    field.setParams(params);
    StereoRender render;
    if (!field.prepare(kSampleRate)) return render;
    render.left.resize(totalFrames);
    render.right.resize(totalFrames);
    render.dryLeft.resize(totalFrames);
    render.dryRight.resize(totalFrames);
    for (uint32_t frame = 0u; frame < totalFrames; ++frame) {
        if (automate && frame > 0u && (frame % 7919u) == 0u) {
            const uint32_t stage = (frame / 7919u) % 7u;
            params.mode = static_cast<s3g::AcapellaPvocMode>(stage);
            params.position = stage & 1u ? 0.92f : 0.08f;
            params.speed = stage & 1u ? -1.65f : 1.35f;
            params.heads = 1u + stage;
            params.feedback = stage & 1u ? 0.90f : 0.05f;
            params.phaseMode = static_cast<s3g::AcapellaPvocPhaseMode>(
                stage % s3g::kAcapellaPvocPhaseModeCount);
            field.setParams(params);
        }
        auto gesture = gestureAt(frame, activeFrames);
        field.setGesture(gesture);
        const bool sounding = frame < activeFrames;
        const float left = sounding ? voiceSignal(frame, kSampleRate) : 0.0f;
        const float rightVoice = sounding
            ? voiceSignal(frame, kSampleRate, 0.43f) : 0.0f;
        const float right = antiPhase ? -left : rightVoice;
        const auto output = field.processFrameStereo(left, right);
        render.left[frame] = output.left;
        render.right[frame] = output.right;
        render.dryLeft[frame] = output.dryLeft;
        render.dryRight[frame] = output.dryRight;
        if (!std::isfinite(output.left) || !std::isfinite(output.right)) {
            render.left.clear();
            return render;
        }
    }
    render.diagnostics = field.diagnostics();
    return render;
}

double energy(const std::vector<float>& signal, uint32_t begin, uint32_t end)
{
    begin = std::min<uint32_t>(begin, static_cast<uint32_t>(signal.size()));
    end = std::min<uint32_t>(end, static_cast<uint32_t>(signal.size()));
    double sum = 0.0;
    for (uint32_t frame = begin; frame < end; ++frame) {
        const double value = signal[frame];
        sum += value * value;
    }
    return sum;
}

double peak(const std::vector<float>& signal)
{
    double result = 0.0;
    for (float value : signal) result = std::max(result, std::abs(double(value)));
    return result;
}

double maximumStep(const std::vector<float>& signal)
{
    double result = 0.0;
    float previous = 0.0f;
    for (float value : signal) {
        result = std::max(result,
            std::abs(static_cast<double>(value) - previous));
        previous = value;
    }
    return result;
}

double residualDb(const std::vector<float>& wet,
    const std::vector<float>& dry, uint32_t begin, uint32_t end)
{
    begin = std::min<uint32_t>(begin,
        static_cast<uint32_t>(std::min(wet.size(), dry.size())));
    end = std::min<uint32_t>(end,
        static_cast<uint32_t>(std::min(wet.size(), dry.size())));
    double dot = 0.0;
    double dryPower = 0.0;
    for (uint32_t frame = begin; frame < end; ++frame) {
        dot += static_cast<double>(wet[frame]) * dry[frame];
        dryPower += static_cast<double>(dry[frame]) * dry[frame];
    }
    const double gain = dot / std::max(dryPower, 1.0e-20);
    double residual = 0.0;
    for (uint32_t frame = begin; frame < end; ++frame) {
        const double difference = wet[frame] - gain * dry[frame];
        residual += difference * difference;
    }
    return 10.0 * std::log10(std::max(residual, 1.0e-20)
        / std::max(dryPower, 1.0e-20));
}

double rmsRatioDb(const std::vector<float>& wet,
    const std::vector<float>& dry, uint32_t begin, uint32_t end)
{
    return 10.0 * std::log10(std::max(energy(wet, begin, end), 1.0e-20)
        / std::max(energy(dry, begin, end), 1.0e-20));
}

bool transportModesAreAudibleAndBounded()
{
    constexpr uint32_t totalFrames = 4u * kSampleRate;
    constexpr uint32_t activeFrames = 3u * kSampleRate;
    constexpr uint32_t metricBegin = kSampleRate / 2u;
    constexpr uint32_t metricEnd = 5u * kSampleRate / 2u;
    constexpr std::array<s3g::AcapellaPvocMode, 6u> modes {{
        s3g::AcapellaPvocMode::Freeze,
        s3g::AcapellaPvocMode::Stretch,
        s3g::AcapellaPvocMode::Scrub,
        s3g::AcapellaPvocMode::Reverse,
        s3g::AcapellaPvocMode::Loop,
        s3g::AcapellaPvocMode::Cloud,
    }};
    std::array<double, modes.size()> residuals {};
    std::array<std::vector<float>, modes.size()> modeSignals;
    for (uint32_t index = 0u; index < modes.size(); ++index) {
        s3g::AcapellaPvocParams params;
        params.amount = 1.0f;
        params.mode = modes[index];
        params.memoryMs = 1100.0f;
        params.position = 0.34f;
        params.speed = 0.48f;
        params.loopLengthMs = 190.0f;
        params.timeSpread = 0.64f;
        params.heads = 2u;
        params.transientPreserve = 0.12f;
        params.captureTrigger = s3g::AcapellaPvocCaptureTrigger::Syllable;
        params.gestureFollow = 1.0f;
        if (params.mode == s3g::AcapellaPvocMode::Cloud) {
            params.heads = 5u;
            params.partialCloud = 0.62f;
            params.phaseMode = s3g::AcapellaPvocPhaseMode::Diffuse;
            params.coherence = 0.28f;
            params.phaseDrift = 0.52f;
        }
        const auto output = renderField(
            params, totalFrames, activeFrames);
        if (output.left.empty()) {
            std::cerr << "PVOC mode render failed for " << index << '\n';
            return false;
        }
        residuals[index] = residualDb(output.left, output.dryLeft,
            metricBegin, metricEnd);
        const double level = rmsRatioDb(output.left, output.dryLeft,
            metricBegin, metricEnd);
        if (!(residuals[index] > -20.0)
            || !(level > -15.0 && level < 8.0)
            || !(peak(output.left) < 1.26)
            || !(maximumStep(output.left) < 0.32)
            || output.diagnostics.nonFiniteRecoveries != 0u
            || output.diagnostics.outputGuardHits != 0u) {
            std::cerr << "PVOC mode " << index << " failed: residual "
                      << residuals[index] << " dB, level " << level
                      << " dB, peak " << peak(output.left) << ", step "
                      << maximumStep(output.left) << ", recoveries/guards "
                      << output.diagnostics.nonFiniteRecoveries << '/'
                      << output.diagnostics.outputGuardHits << '\n';
            return false;
        }
        modeSignals[index] = output.left;
    }
    double minimumPairwise = 100.0;
    for (uint32_t first = 0u; first < modes.size(); ++first) {
        for (uint32_t second = first + 1u; second < modes.size(); ++second) {
            minimumPairwise = std::min(minimumPairwise,
                residualDb(modeSignals[first], modeSignals[second],
                    metricBegin, metricEnd));
        }
    }
    if (!(minimumPairwise > -24.0)) {
        std::cerr << "PVOC transport modes collapsed: minimum pairwise "
                  << minimumPairwise << " dB\n";
        return false;
    }
    return true;
}

bool stereoSideIsProcessed()
{
    s3g::AcapellaPvocParams params;
    params.amount = 1.0f;
    params.mode = s3g::AcapellaPvocMode::Cloud;
    params.memoryMs = 1600.0f;
    params.position = 0.42f;
    params.speed = 0.62f;
    params.timeSpread = 0.78f;
    params.heads = 5u;
    params.partialCloud = 0.70f;
    params.phaseMode = s3g::AcapellaPvocPhaseMode::Diffuse;
    params.coherence = 0.24f;
    params.phaseDrift = 0.55f;
    params.transientPreserve = 0.08f;
    const auto output = renderField(params,
        3u * kSampleRate, 5u * kSampleRate / 2u, true);
    const uint32_t begin = kSampleRate / 2u;
    const uint32_t end = 2u * kSampleRate;
    if (output.left.empty()
        || !(energy(output.left, begin, end) > 0.05)
        || !(energy(output.right, begin, end) > 0.05)
        || !(residualDb(output.left, output.dryLeft, begin, end) > -16.0)) {
        std::cerr << "PVOC stereo side path was silent or bypassed\n";
        return false;
    }
    return true;
}

bool factoryProfilesAreAudible()
{
    constexpr uint32_t totalFrames = 3u * kSampleRate;
    constexpr uint32_t begin = kSampleRate / 2u;
    constexpr uint32_t end = 5u * kSampleRate / 2u;
    for (uint32_t profile = s3g::kAcapellaPvocProfileFirst;
         profile < s3g::kAcapellaPvocProfileFirst
                + s3g::kAcapellaPvocProfileCount; ++profile) {
        const auto base = s3g::acapellaPvocProfileBase(profile);
        auto wetParams = s3g::acapellaPvocProfileEffects(profile,
            s3g::acapellaVocalFxPreset(base));
        auto dryParams = wetParams;
        dryParams.pvoc.amount = 0.0f;
        s3g::AcapellaVocalEffects wet;
        s3g::AcapellaVocalEffects dry;
        wet.setParams(wetParams);
        dry.setParams(dryParams);
        wet.prepare(kSampleRate);
        dry.prepare(kSampleRate);
        std::vector<float> wetSignal(totalFrames, 0.0f);
        std::vector<float> drySignal(totalFrames, 0.0f);
        for (uint32_t frame = 0u; frame < totalFrames; ++frame) {
            const auto gesture = gestureAt(frame, totalFrames);
            wet.setPvocGesture(gesture);
            dry.setPvocGesture(gesture);
            const float left = voiceSignal(frame, kSampleRate);
            const float right = voiceSignal(frame, kSampleRate, 0.41f);
            const auto wetOutput = wet.processFrameStereo(left, right);
            const auto dryOutput = dry.processFrameStereo(left, right);
            wetSignal[frame] = 0.5f * (wetOutput.left + wetOutput.right);
            drySignal[frame] = 0.5f * (dryOutput.left + dryOutput.right);
        }
        const double residual = residualDb(wetSignal, drySignal, begin, end);
        const double required = profile == 12u ? -12.0 : -18.0;
        const auto& diagnostics = wet.pvocDiagnostics();
        if (!(residual > required)
            || !(peak(wetSignal) < 0.981)
            || diagnostics.nonFiniteRecoveries != 0u
            || diagnostics.spectralGuardHits != 0u
            || diagnostics.outputGuardHits != 0u) {
            std::cerr << "PVOC factory profile " << profile
                      << " failed: residual " << residual << " dB, peak "
                      << peak(wetSignal) << ", recovery/spectral/output "
                      << diagnostics.nonFiniteRecoveries << '/'
                      << diagnostics.spectralGuardHits << '/'
                      << diagnostics.outputGuardHits << '\n';
            return false;
        }
    }
    return true;
}

bool timeScarDoesNotRunAway()
{
    auto params = s3g::acapellaPvocProfileEffects(12u,
        s3g::acapellaVocalFxPreset(
            s3g::acapellaPvocProfileBase(12u))).pvoc;
    constexpr uint32_t activeFrames = 8u * kSampleRate;
    constexpr uint32_t totalFrames = 13u * kSampleRate;
    const auto output = renderField(params, totalFrames, activeFrames);
    if (output.left.empty()) return false;

    std::array<double, 12u> windows {};
    for (uint32_t window = 0u; window < windows.size(); ++window) {
        const uint32_t begin = (2u * kSampleRate)
            + window * (kSampleRate / 2u);
        const uint32_t end = begin + kSampleRate / 2u;
        windows[window] = 10.0 * std::log10(std::max(
            energy(output.left, begin, end)
                / static_cast<double>(kSampleRate / 2u), 1.0e-20));
    }
    std::array<double, windows.size()> ordered = windows;
    std::sort(ordered.begin(), ordered.end());
    const double median = 0.5 * (ordered[5] + ordered[6]);
    const double maximum = *std::max_element(windows.begin(), windows.end());
    const double slope = (windows.back() - windows.front()) / 5.5;
    const double tailRms = std::sqrt(energy(output.left,
        12u * kSampleRate, totalFrames) / static_cast<double>(kSampleRate));
    if (!(slope < 0.20)
        || !(maximum < median + 6.0)
        || !(tailRms < 1.0e-4)
        || !(peak(output.left) < 1.26)
        || output.diagnostics.nonFiniteRecoveries != 0u
        || output.diagnostics.spectralGuardHits != 0u
        || output.diagnostics.outputGuardHits != 0u) {
        std::cerr << "Time Scar runaway contract failed: slope " << slope
                  << " dB/s, max/median " << maximum << '/' << median
                  << ", tail " << tailRms << ", peak " << peak(output.left)
                  << ", recovery/spectral/output "
                  << output.diagnostics.nonFiniteRecoveries << '/'
                  << output.diagnostics.spectralGuardHits << '/'
                  << output.diagnostics.outputGuardHits << '\n';
        return false;
    }
    return true;
}

bool automationAndExtremeFeedbackStayFinite()
{
    s3g::AcapellaPvocParams params;
    params.amount = 1.0f;
    params.mode = s3g::AcapellaPvocMode::Cloud;
    params.memoryMs = 2200.0f;
    params.position = 0.5f;
    params.speed = 0.8f;
    params.loopLengthMs = 90.0f;
    params.timeSpread = 1.0f;
    params.heads = 8u;
    params.feedback = 0.94f;
    params.pitchSemitones = 12.0f;
    params.formantSemitones = -12.0f;
    params.warp = 0.72f;
    params.partialCloud = 1.0f;
    params.phaseMode = s3g::AcapellaPvocPhaseMode::Diffuse;
    params.coherence = 0.0f;
    params.phaseDrift = 1.0f;
    params.transientPreserve = 0.0f;
    const auto output = renderField(params,
        8u * kSampleRate, 7u * kSampleRate, false, true);
    if (output.left.empty()
        || !(peak(output.left) < 1.26)
        || !(maximumStep(output.left) < 0.48)
        || output.diagnostics.nonFiniteRecoveries != 0u
        || output.diagnostics.outputGuardHits != 0u) {
        std::cerr << "PVOC automation/extreme feedback failed: peak "
                  << peak(output.left) << ", step "
                  << maximumStep(output.left) << ", recoveries/output guards "
                  << output.diagnostics.nonFiniteRecoveries << '/'
                  << output.diagnostics.outputGuardHits << '\n';
        return false;
    }
    return true;
}

} // namespace

int main()
{
#if !S3G_HAS_ACCELERATE_FFT
    std::cout << "Acapella PVOC regression skipped without Accelerate FFT\n";
    return 0;
#else
    if (!transportModesAreAudibleAndBounded()
        || !stereoSideIsProcessed()
        || !factoryProfilesAreAudible()
        || !timeScarDoesNotRunAway()
        || !automationAndExtremeFeedbackStayFinite()) {
        return 1;
    }
    std::cout << "Acapella PVOC regression smoke test passed\n";
    return 0;
#endif
}
