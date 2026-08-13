#include "s3g_acapella_resonator_bank.h"
#include "s3g_acapella_vocal_fx.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>

namespace {

constexpr uint32_t kSampleRate = 48000u;

struct Render {
    std::vector<float> left;
    std::vector<float> right;
    std::vector<float> carrierLeft;
    std::vector<float> carrierRight;
    bool finite = true;
    float peak = 0.0f;
    float maximumStep = 0.0f;
};

struct FormantMatrixRender {
    std::vector<float> left;
    std::vector<float> right;
    s3g::AcapellaResonatorMeterSnapshot meters {};
    bool finite = true;
    float peak = 0.0f;
    float maximumStep = 0.0f;
};

enum class FormantAnalysisSignal {
    Silence,
    Tone,
    Sibilant,
};

float deterministicNoise(uint32_t frame, uint32_t salt = 0u)
{
    uint32_t state = frame * 747796405u + 2891336453u + salt;
    state ^= state >> 16u;
    state *= 2246822519u;
    state ^= state >> 13u;
    state *= 3266489917u;
    state ^= state >> 16u;
    return static_cast<float>(state & 0x00ffffffu)
        * (2.0f / 16777215.0f) - 1.0f;
}

FormantMatrixRender renderFormantMatrix(
    s3g::AcapellaResonatorParams params,
    uint32_t frames = 36000u,
    bool externalCarrierAvailable = true,
    bool externalCarrierSignal = true,
    FormantAnalysisSignal analysisKind = FormantAnalysisSignal::Tone,
    float analysisFrequencyHz = 740.0f,
    s3g::AcapellaPhoneme phoneme = s3g::AcapellaPhoneme::AA,
    bool activeGesture = true,
    double tempoBpm = 120.0,
    bool tempoValid = true,
    bool stereoExternal = false)
{
    s3g::AcapellaResonatorBank bank;
    bank.setParams(params);
    FormantMatrixRender render;
    render.finite = bank.prepare(kSampleRate);
    bank.setTempo(tempoBpm, tempoValid);
    render.left.resize(frames);
    render.right.resize(frames);
    float previousLeft = 0.0f;
    for (uint32_t frame = 0u; frame < frames && render.finite; ++frame) {
        s3g::AcapellaResonatorGesture gesture;
        gesture.active = activeGesture;
        gesture.phoneme = activeGesture
            ? phoneme : s3g::AcapellaPhoneme::Silence;
        gesture.frequencyHz = 137.0f;
        gesture.voiceInstance = 0x4001u;
        gesture.stepIndex = 0u;
        gesture.stepProgress = static_cast<float>(frame)
            / static_cast<float>(std::max<uint32_t>(1u, frames));
        gesture.flags = frame == 0u
            ? s3g::kAcapellaWordStart | s3g::kAcapellaSyllableStart : 0u;
        if (activeGesture) {
            gesture.voiceCount = 1u;
            gesture.voiceFrequencyHz[0] = 137.0f;
            gesture.voiceGain[0] = 1.0f;
            gesture.voiceInstanceIds[0] = 0x4001u;
        }
        bank.setGesture(gesture);

        const float time = static_cast<float>(frame)
            / static_cast<float>(kSampleRate);
        float analysis = 0.0f;
        if (analysisKind == FormantAnalysisSignal::Tone) {
            analysis = 0.24f * std::sin(2.0f * s3g::kPi
                * analysisFrequencyHz * time);
        } else if (analysisKind == FormantAnalysisSignal::Sibilant) {
            analysis = 0.14f * deterministicNoise(frame, 0x51b17u)
                + 0.10f * std::sin(2.0f * s3g::kPi
                    * 5920.0f * time);
        }
        float externalLeft = 0.0f;
        float externalRight = 0.0f;
        if (externalCarrierSignal) {
            externalLeft = 0.18f * deterministicNoise(frame, 0xa173u)
                + 0.07f * std::sin(2.0f * s3g::kPi * 97.0f * time);
            externalRight = stereoExternal
                ? 0.18f * deterministicNoise(frame, 0xc421u)
                    + 0.07f * std::sin(
                        2.0f * s3g::kPi * 131.0f * time)
                : externalLeft;
        }
        const auto output = bank.processFrameStereo(
            analysis, analysis, externalLeft, externalRight,
            externalCarrierAvailable);
        render.left[frame] = output.left;
        render.right[frame] = output.right;
        const float peak = std::max(
            std::abs(output.left), std::abs(output.right));
        render.peak = std::max(render.peak, peak);
        render.maximumStep = std::max(render.maximumStep,
            std::abs(output.left - previousLeft));
        previousLeft = output.left;
        render.finite = std::isfinite(output.left)
            && std::isfinite(output.right) && peak <= 1.801f;
    }
    render.meters = bank.meterSnapshot();
    return render;
}

float analysisSignal(uint32_t frame, bool consonant)
{
    const float time = static_cast<float>(frame)
        / static_cast<float>(kSampleRate);
    const float fundamental = 118.0f + 16.0f
        * std::sin(2.0f * s3g::kPi * 0.37f * time);
    const float attack = std::min(1.0f, static_cast<float>(frame) / 600.0f);
    float value = 0.15f * std::sin(2.0f * s3g::kPi * fundamental * time)
        + 0.078f * std::sin(2.0f * s3g::kPi * fundamental * 2.02f * time)
        + 0.046f * std::sin(2.0f * s3g::kPi * fundamental * 4.06f * time);
    if (consonant) {
        // Deterministic, high-passed pseudo-noise-like excitation.
        const uint32_t local = frame % 24000u;
        const uint32_t consonantLocal = local - 8000u;
        const float edge = std::min({ 1.0f,
            static_cast<float>(consonantLocal) / 128.0f,
            static_cast<float>(7999u - consonantLocal) / 128.0f });
        value += std::max(0.0f, edge)
            * (0.038f * std::sin(2.0f * s3g::kPi * 3271.0f * time)
                + 0.026f * std::sin(2.0f * s3g::kPi * 5119.0f * time));
    }
    return attack * value;
}

float carrierSignal(uint32_t frame, float phaseOffset = 0.0f)
{
    const float time = static_cast<float>(frame)
        / static_cast<float>(kSampleRate);
    float value = 0.0f;
    for (uint32_t harmonic = 1u; harmonic <= 12u; ++harmonic) {
        value += std::sin(2.0f * s3g::kPi * 118.0f
            * static_cast<float>(harmonic) * time + phaseOffset)
            / static_cast<float>(harmonic);
    }
    return value * 0.12f;
}

s3g::AcapellaResonatorGesture gestureFor(uint32_t frame, bool consonant,
    s3g::AcapellaPhoneme forcedPhoneme = s3g::AcapellaPhoneme::Silence)
{
    s3g::AcapellaResonatorGesture gesture;
    gesture.phoneme = consonant
        ? s3g::AcapellaPhoneme::S : s3g::AcapellaPhoneme::AA;
    if (forcedPhoneme != s3g::AcapellaPhoneme::Silence) {
        gesture.phoneme = forcedPhoneme;
    }
    gesture.frequencyHz = 118.0f;
    gesture.voiceCount = 1u;
    gesture.voiceFrequencyHz[0] = 118.0f;
    gesture.voiceGain[0] = 1.0f;
    gesture.voiceInstanceIds[0] = 37u;
    gesture.stepIndex = frame / 12000u;
    gesture.stepProgress = static_cast<float>(frame % 12000u) / 12000.0f;
    gesture.voiceInstance = 37u;
    gesture.active = frame < 72000u;
    if (gesture.active) {
        gesture.flags = s3g::kAcapellaSyllableStart;
        if (gesture.stepIndex == 0u) gesture.flags |= s3g::kAcapellaWordStart;
        // WordEnd, like the production score flag, remains high for the
        // entire last phoneme. Release must wait for the following rest edge.
        if (gesture.stepIndex == 5u) gesture.flags |= s3g::kAcapellaWordEnd;
    } else {
        gesture.phoneme = s3g::AcapellaPhoneme::Silence;
        gesture.flags = s3g::kAcapellaForcedRest;
    }
    return gesture;
}

Render renderBank(s3g::AcapellaResonatorParams params,
    uint32_t frames = 96000u, bool sideOnly = false, bool automate = false,
    bool zeroCarrier = false, bool zeroAnalysis = false,
    s3g::AcapellaPhoneme forcedPhoneme = s3g::AcapellaPhoneme::Silence,
    uint32_t sampleRate = kSampleRate);
double rms(const std::vector<float>& values, uint32_t begin = 0u,
    uint32_t end = 0xffffffffu);
double differenceRms(const std::vector<float>& a,
    const std::vector<float>& b, uint32_t begin = 0u,
    uint32_t end = 0xffffffffu);

float syntheticVowelSample(uint32_t frame,
    const std::array<float, 3u>& formants,
    const std::array<float, 3u>& bandwidths)
{
    const float time = static_cast<float>(frame)
        / static_cast<float>(kSampleRate);
    constexpr float fundamental = 121.0f;
    float sample = 0.0f;
    for (uint32_t harmonic = 1u; harmonic <= 64u; ++harmonic) {
        const float frequency = fundamental * static_cast<float>(harmonic);
        if (frequency >= 7800.0f) break;
        float spectralEnvelope = 0.018f;
        constexpr std::array<float, 3u> weights {{ 1.0f, 0.78f, 0.42f }};
        for (uint32_t formant = 0u; formant < 3u; ++formant) {
            spectralEnvelope += weights[formant]
                * s3g::acapella_resonator_detail::bell(
                    frequency, formants[formant], bandwidths[formant]);
        }
        sample += spectralEnvelope
            * std::sin(2.0f * s3g::kPi * frequency * time
                + 0.17f * static_cast<float>(harmonic))
            / std::sqrt(static_cast<float>(harmonic));
    }
    return sample * 0.085f;
}

float normalizedVectorDistance(
    const std::array<float, s3g::kAcapellaResonatorBands>& left,
    const std::array<float, s3g::kAcapellaResonatorBands>& right)
{
    double dot = 0.0;
    double leftEnergy = 0.0;
    double rightEnergy = 0.0;
    for (uint32_t band = 0u; band < s3g::kAcapellaResonatorBands; ++band) {
        dot += static_cast<double>(left[band]) * right[band];
        leftEnergy += static_cast<double>(left[band]) * left[band];
        rightEnergy += static_cast<double>(right[band]) * right[band];
    }
    const double denominator = std::sqrt(leftEnergy * rightEnergy);
    return denominator > 1.0e-12
        ? static_cast<float>(1.0 - dot / denominator) : 0.0f;
}

bool measuredSpeechEnvelopeProbe()
{
    constexpr std::array<std::array<float, 3u>, 3u> formants {{
        {{ 730.0f, 1090.0f, 2440.0f }}, // AH
        {{ 270.0f, 2290.0f, 3010.0f }}, // EE
        {{ 300.0f,  870.0f, 2240.0f }}, // OO
    }};
    constexpr std::array<std::array<float, 3u>, 3u> bandwidths {{
        {{ 100.0f, 120.0f, 180.0f }},
        {{  75.0f, 130.0f, 190.0f }},
        {{  80.0f, 100.0f, 170.0f }},
    }};
    std::array<s3g::AcapellaResonatorMeterSnapshot, 3u> snapshots {};
    std::array<double, 3u> outputRms {};
    std::array<std::vector<float>, 3u> steadyOutput;
    for (uint32_t vowel = 0u; vowel < snapshots.size(); ++vowel) {
        s3g::AcapellaResonatorParams params;
        params.amount = 1.0f;
        params.mode = s3g::AcapellaResonatorMode::Vocoder;
        params.modulatorSource
            = s3g::AcapellaResonatorModulatorSource::ExternalMic;
        params.analysisBlend = 0.0f;
        params.attackMs = 2.0f;
        params.releaseMs = 65.0f;
        params.blurMs = 4.0f;
        params.carrierShape = s3g::AcapellaResonatorCarrierShape::Saw;
        params.carrierHarmonics = 0.94f;
        params.carrierNoise = 0.18f;
        params.voicingMode = s3g::AcapellaResonatorVoicingMode::Tonal;
        params.resonance = 0.48f;
        params.driveDb = 0.0f;
        params.matrixMode = s3g::AcapellaResonatorMatrixMode::Identity;
        params.matrixMorph = 1.0f;
        params.articulationThru = 0.0f;
        params.openLevel = 0.0f;
        s3g::AcapellaResonatorBank bank;
        bank.setParams(params);
        if (!bank.prepare(kSampleRate)) return false;
        double energy = 0.0;
        constexpr uint32_t frames = kSampleRate;
        steadyOutput[vowel].reserve(frames / 2u);
        for (uint32_t frame = 0u; frame < frames; ++frame) {
            s3g::AcapellaResonatorGesture gesture;
            gesture.active = true;
            gesture.carrierOnly = true;
            gesture.frequencyHz = 109.0f;
            gesture.voiceInstance = 0x7100u + vowel;
            gesture.voiceCount = 1u;
            gesture.voiceFrequencyHz[0] = 109.0f;
            gesture.voiceGain[0] = 1.0f;
            gesture.voiceInstanceIds[0] = gesture.voiceInstance;
            bank.setGesture(gesture);
            const float analysis = syntheticVowelSample(
                frame, formants[vowel], bandwidths[vowel]);
            const auto output = bank.processFrameStereo(
                analysis, analysis, 0.0f, 0.0f, false);
            if (!std::isfinite(output.left) || !std::isfinite(output.right)) {
                return false;
            }
            if (frame >= frames / 2u) {
                energy += 0.5 * (static_cast<double>(output.left) * output.left
                    + static_cast<double>(output.right) * output.right);
                steadyOutput[vowel].push_back(output.left);
            }
        }
        outputRms[vowel] = std::sqrt(energy / (frames / 2u));
        snapshots[vowel] = bank.meterSnapshot();
    }
    const float analysisAhEe = normalizedVectorDistance(
        snapshots[0].analysis, snapshots[1].analysis);
    const float analysisEeOo = normalizedVectorDistance(
        snapshots[1].analysis, snapshots[2].analysis);
    const float synthesisAhEe = normalizedVectorDistance(
        snapshots[0].synthesis, snapshots[1].synthesis);
    const float synthesisEeOo = normalizedVectorDistance(
        snapshots[1].synthesis, snapshots[2].synthesis);
    const double waveformAhEe = differenceRms(
        steadyOutput[0], steadyOutput[1])
        / std::max(1.0e-6, rms(steadyOutput[0]));
    const double waveformEeOo = differenceRms(
        steadyOutput[1], steadyOutput[2])
        / std::max(1.0e-6, rms(steadyOutput[1]));
    std::cerr << "measured vowel distances analysis/synthesis AH-EE "
              << analysisAhEe << '/' << synthesisAhEe << ", EE-OO "
              << analysisEeOo << '/' << synthesisEeOo << ", RMS "
              << outputRms[0] << '/' << outputRms[1] << '/'
              << outputRms[2] << ", waveform delta "
              << waveformAhEe << '/' << waveformEeOo << '\n';
    if (outputRms[0] < 0.002 || outputRms[1] < 0.002
        || outputRms[2] < 0.002
        || analysisAhEe < 0.12f || analysisEeOo < 0.12f
        || synthesisAhEe < 0.08f || synthesisEeOo < 0.045f
        || waveformAhEe < 0.20 || waveformEeOo < 0.20) {
        std::cerr << "measured speech formants collapsed into carrier loudness\n";
        return false;
    }
    return true;
}

bool measuredConsonantTransferProbe()
{
    s3g::AcapellaResonatorParams params;
    params.amount = 1.0f;
    params.mode = s3g::AcapellaResonatorMode::Vocoder;
    params.modulatorSource
        = s3g::AcapellaResonatorModulatorSource::ExternalMic;
    params.analysisBlend = 0.0f;
    params.attackMs = 2.0f;
    params.releaseMs = 65.0f;
    params.blurMs = 4.0f;
    params.carrierShape = s3g::AcapellaResonatorCarrierShape::Saw;
    params.carrierHarmonics = 0.94f;
    params.carrierNoise = 0.18f;
    params.voicingMode = s3g::AcapellaResonatorVoicingMode::Detect;
    params.voicingThreshold = 0.44f;
    params.sibilance = 0.78f;
    params.articulationThru = 0.0f;
    params.openLevel = 0.0f;
    params.driveDb = 0.0f;
    s3g::AcapellaResonatorBank bank;
    bank.setParams(params);
    if (!bank.prepare(kSampleRate)) return false;

    constexpr std::array<float, 3u> vowelFormants {{
        730.0f, 1090.0f, 2440.0f,
    }};
    constexpr std::array<float, 3u> vowelBandwidths {{
        100.0f, 120.0f, 180.0f,
    }};
    constexpr uint32_t segmentFrames = kSampleRate / 2u;
    s3g::AcapellaResonatorMeterSnapshot vowelSnapshot {};
    float previousNoiseLow = 0.0f;
    double consonantEnergy = 0.0;
    float maximumStep = 0.0f;
    float previousOutput = 0.0f;
    for (uint32_t frame = 0u; frame < segmentFrames * 2u; ++frame) {
        s3g::AcapellaResonatorGesture gesture;
        gesture.active = true;
        gesture.carrierOnly = true;
        gesture.frequencyHz = 109.0f;
        gesture.voiceInstance = 0x7200u;
        gesture.voiceCount = 1u;
        gesture.voiceFrequencyHz[0] = 109.0f;
        gesture.voiceGain[0] = 1.0f;
        gesture.voiceInstanceIds[0] = gesture.voiceInstance;
        bank.setGesture(gesture);
        float analysis = 0.0f;
        if (frame < segmentFrames) {
            analysis = syntheticVowelSample(
                frame, vowelFormants, vowelBandwidths);
        } else {
            const float noise = 0.14f * deterministicNoise(
                frame - segmentFrames, 0x9c41u);
            previousNoiseLow += (noise - previousNoiseLow) * 0.10f;
            analysis = noise - previousNoiseLow;
        }
        const auto output = bank.processFrameStereo(
            analysis, analysis, 0.0f, 0.0f, false);
        if (!std::isfinite(output.left) || !std::isfinite(output.right)) {
            return false;
        }
        maximumStep = std::max(maximumStep,
            std::abs(output.left - previousOutput));
        previousOutput = output.left;
        if (frame + 1u == segmentFrames) {
            vowelSnapshot = bank.meterSnapshot();
        }
        if (frame >= segmentFrames + segmentFrames / 2u) {
            consonantEnergy += 0.5
                * (static_cast<double>(output.left) * output.left
                    + static_cast<double>(output.right) * output.right);
        }
    }
    const auto consonantSnapshot = bank.meterSnapshot();
    double totalSynthesis = 1.0e-12;
    double upperSynthesis = 0.0;
    for (uint32_t band = 0u; band < s3g::kAcapellaResonatorBands; ++band) {
        totalSynthesis += consonantSnapshot.synthesis[band];
        if (band >= 15u) upperSynthesis += consonantSnapshot.synthesis[band];
    }
    const double consonantRms = std::sqrt(
        consonantEnergy / (segmentFrames / 2u));
    const double upperFraction = upperSynthesis / totalSynthesis;
    if (vowelSnapshot.unvoiced > 0.20f
        || consonantSnapshot.unvoiced < 0.80f
        || consonantRms < 0.003
        || upperFraction < 0.35
        || maximumStep > 0.041f) {
        std::cerr << "measured consonant transfer failed: V/UV "
                  << vowelSnapshot.unvoiced << '/'
                  << consonantSnapshot.unvoiced << ", RMS "
                  << consonantRms << ", upper fraction " << upperFraction
                  << ", maximum step " << maximumStep << '\n';
        return false;
    }
    return true;
}

bool voicePitchCarrierProbe()
{
    if (s3g::kAcapellaResonatorPitchScaleCount
            != s3g::kMusicalScaleCount + 1u
        || s3g::acapellaResonatorSharedScale(
            s3g::AcapellaResonatorPitchScale::Chromatic) != 0u
        || s3g::acapellaResonatorSharedScale(
            s3g::AcapellaResonatorPitchScale::Major) != 1u
        || s3g::acapellaResonatorSharedScale(
            s3g::AcapellaResonatorPitchScale::NaturalMinor) != 2u
        || s3g::acapellaResonatorSharedScale(
            s3g::AcapellaResonatorPitchScale::HarmonicMinor) != 5u
        || s3g::acapellaResonatorSharedScale(
            s3g::AcapellaResonatorPitchScale::Dorian) != 6u
        || s3g::acapellaResonatorSharedScale(
            s3g::AcapellaResonatorPitchScale::MajorPentatonic) != 3u
        || s3g::acapellaResonatorSharedScale(
            s3g::AcapellaResonatorPitchScale::MinorPentatonic) != 31u) {
        std::cerr << "legacy pitch-scale values were not preserved\n";
        return false;
    }
    std::array<bool, s3g::kMusicalScaleCount> sharedScales {};
    for (uint32_t value = 1u;
         value < s3g::kAcapellaResonatorPitchScaleCount; ++value) {
        const auto scale = static_cast<s3g::AcapellaResonatorPitchScale>(
            value);
        const uint32_t shared = s3g::acapellaResonatorSharedScale(scale);
        if (shared >= sharedScales.size() || sharedScales[shared]
            || s3g::acapellaResonatorPitchScaleValue(shared) != value) {
            std::cerr << "expanded pitch-scale map is not a permutation\n";
            return false;
        }
        sharedScales[shared] = true;
    }

    const auto renderPitch = [](s3g::AcapellaResonatorPitchScale scale,
                                bool finishWithSilence) {
        s3g::AcapellaResonatorParams params;
        params.amount = 0.0f;
        params.mode = s3g::AcapellaResonatorMode::Vocoder;
        params.modulatorSource
            = s3g::AcapellaResonatorModulatorSource::ExternalMic;
        params.carrierPitchSource
            = s3g::AcapellaResonatorCarrierPitchSource::Voice;
        params.pitchScaleRoot = 0u;
        params.pitchScale = scale;
        params.pitchHoldMs = 350.0f;
        params.carrierShape = s3g::AcapellaResonatorCarrierShape::Glottal;
        params.carrierHarmonics = 0.0f;
        params.carrierNoise = 0.0f;
        params.voicingMode = s3g::AcapellaResonatorVoicingMode::Tonal;
        params.carrierColor = 0.0f;
        params.articulationThru = 0.0f;
        s3g::AcapellaResonatorBank bank;
        bank.setParams(params);
        FormantMatrixRender result;
        result.finite = bank.prepare(kSampleRate);
        constexpr uint32_t voicedFrames = kSampleRate * 3u;
        constexpr uint32_t silentFrames = kSampleRate;
        const uint32_t totalFrames = voicedFrames
            + (finishWithSilence ? silentFrames : 0u);
        result.left.resize(totalFrames, 0.0f);
        for (uint32_t frame = 0u; frame < totalFrames && result.finite;
             ++frame) {
            bank.setGesture({});
            const float analysis = frame < voicedFrames
                ? 0.22f * std::sin(2.0f * s3g::kPi * 230.0f
                    * static_cast<float>(frame)
                    / static_cast<float>(kSampleRate))
                : 0.0f;
            const auto output = bank.processFrameStereo(
                analysis, analysis, 0.0f, 0.0f, false);
            result.left[frame] = output.left;
            result.finite = std::isfinite(output.left)
                && std::isfinite(output.right);
        }
        result.meters = bank.meterSnapshot();
        return result;
    };

    const auto continuous = renderPitch(
        s3g::AcapellaResonatorPitchScale::Continuous, false);
    const auto chromatic = renderPitch(
        s3g::AcapellaResonatorPitchScale::Chromatic, false);
    const auto cMajor = renderPitch(
        s3g::AcapellaResonatorPitchScale::Major, false);
    const auto released = renderPitch(
        s3g::AcapellaResonatorPitchScale::Major, true);
    const double sustainedRms = rms(cMajor.left,
        kSampleRate * 2u, kSampleRate * 3u);
    const double releasedRms = rms(released.left,
        kSampleRate * 3u + kSampleRate / 2u,
        kSampleRate * 4u);
    if (!continuous.finite || !chromatic.finite || !cMajor.finite
        || !continuous.meters.pitchActive
        || !chromatic.meters.pitchActive
        || !cMajor.meters.pitchActive
        || std::abs(continuous.meters.detectedPitchHz - 230.0f) > 3.0f
        || std::abs(chromatic.meters.detectedPitchHz - 233.0819f) > 1.0f
        || std::abs(cMajor.meters.detectedPitchHz - 220.0f) > 1.0f
        || continuous.meters.pitchConfidence < 0.68f
        || sustainedRms < 0.005
        || releasedRms > 1.0e-6
        || released.meters.pitchActive) {
        std::cerr << "voice pitch carrier failed: continuous/chromatic/major "
                  << continuous.meters.detectedPitchHz << '/'
                  << chromatic.meters.detectedPitchHz << '/'
                  << cMajor.meters.detectedPitchHz << ", confidence "
                  << continuous.meters.pitchConfidence << ", active "
                  << continuous.meters.pitchActive << '/'
                  << released.meters.pitchActive << ", RMS "
                  << sustainedRms << '/' << releasedRms << '\n';
        return false;
    }

    // Infinite is a genuine pitch-memory latch. A long silent interval must
    // still close the microphone gate, but unpitched articulation after that
    // interval must immediately reuse the last confident sung pitch rather
    // than waiting for a new YIN estimate.
    s3g::AcapellaResonatorParams infiniteParams;
    infiniteParams.amount = 0.0f;
    infiniteParams.mode = s3g::AcapellaResonatorMode::Vocoder;
    infiniteParams.modulatorSource
        = s3g::AcapellaResonatorModulatorSource::ExternalMic;
    infiniteParams.carrierPitchSource
        = s3g::AcapellaResonatorCarrierPitchSource::Voice;
    infiniteParams.pitchScaleRoot = 0u;
    infiniteParams.pitchScale = s3g::AcapellaResonatorPitchScale::Major;
    infiniteParams.pitchHoldMs
        = s3g::kAcapellaResonatorInfinitePitchHoldMs;
    infiniteParams.carrierShape
        = s3g::AcapellaResonatorCarrierShape::Glottal;
    infiniteParams.carrierHarmonics = 0.0f;
    infiniteParams.carrierNoise = 0.0f;
    infiniteParams.voicingMode
        = s3g::AcapellaResonatorVoicingMode::Tonal;
    infiniteParams.carrierColor = 0.0f;
    infiniteParams.articulationThru = 0.0f;
    s3g::AcapellaResonatorBank infiniteBank;
    infiniteBank.setParams(infiniteParams);
    if (!infiniteBank.prepare(kSampleRate)) {
        std::cerr << "infinite pitch hold did not prepare\n";
        return false;
    }
    constexpr uint32_t latchFrames = kSampleRate * 3u;
    constexpr uint32_t softSustainFrames = kSampleRate * 3u;
    constexpr uint32_t quietFrames = kSampleRate * 3u;
    constexpr uint32_t articulationFrames = kSampleRate;
    std::vector<float> infiniteOutput(
        latchFrames + softSustainFrames + quietFrames + articulationFrames,
        0.0f);
    bool infiniteFinite = true;
    for (uint32_t frame = 0u; frame < infiniteOutput.size(); ++frame) {
        float analysis = 0.0f;
        if (frame < latchFrames) {
            analysis = 0.22f * std::sin(2.0f * s3g::kPi * 230.0f
                * static_cast<float>(frame)
                / static_cast<float>(kSampleRate));
        } else if (frame < latchFrames + softSustainFrames) {
            // Far below the ordinary anti-drone close threshold, but still a
            // coherent sung note. Confident periodicity must keep it alive.
            analysis = 0.0015f * std::sin(2.0f * s3g::kPi * 230.0f
                * static_cast<float>(frame)
                / static_cast<float>(kSampleRate));
        } else if (frame >= latchFrames + softSustainFrames + quietFrames) {
            // Broadband consonant-like energy opens the mic gate but should
            // not supply a new stable periodic pitch.
            analysis = 0.18f * deterministicNoise(frame, 0x68f3u);
        }
        infiniteBank.setGesture({});
        const auto output = infiniteBank.processFrameStereo(
            analysis, analysis, 0.0f, 0.0f, false);
        infiniteOutput[frame] = output.left;
        infiniteFinite = infiniteFinite && std::isfinite(output.left)
            && std::isfinite(output.right);
    }
    const auto infiniteMeters = infiniteBank.meterSnapshot();
    const double softSustainRms = rms(infiniteOutput,
        latchFrames + softSustainFrames - kSampleRate,
        latchFrames + softSustainFrames);
    const double quietRms = rms(infiniteOutput,
        latchFrames + softSustainFrames + kSampleRate,
        latchFrames + softSustainFrames + quietFrames);
    const double resumedRms = rms(infiniteOutput,
        latchFrames + softSustainFrames + quietFrames + kSampleRate / 2u,
        latchFrames + softSustainFrames + quietFrames + articulationFrames);
    if (!infiniteFinite || softSustainRms < 0.005
        || quietRms > 1.0e-7 || resumedRms < 0.005
        || !infiniteMeters.pitchActive
        || std::abs(infiniteMeters.detectedPitchHz - 220.0f) > 1.0f) {
        std::cerr << "infinite pitch hold failed: pitch/active "
                  << infiniteMeters.detectedPitchHz << '/'
                  << infiniteMeters.pitchActive
                  << ", RMS soft/quiet/resumed " << softSustainRms << '/'
                  << quietRms << '/' << resumedRms << '\n';
        return false;
    }

    constexpr std::array<float, 9u> registerFrequencies {{
        49.0f, 52.0f, 82.41f, 220.0f, 440.0f, 880.0f, 1300.0f,
        1850.0f, 1980.0f,
    }};
    for (const float frequency : registerFrequencies) {
        s3g::AcapellaResonatorParams rangeParams = infiniteParams;
        rangeParams.pitchScale
            = s3g::AcapellaResonatorPitchScale::Continuous;
        s3g::AcapellaResonatorBank rangeBank;
        rangeBank.setParams(rangeParams);
        if (!rangeBank.prepare(kSampleRate)) return false;
        constexpr uint32_t rangeFrames = kSampleRate * 3u / 2u;
        double rangeEnergy = 0.0;
        bool rangeFinite = true;
        for (uint32_t frame = 0u; frame < rangeFrames; ++frame) {
            const float analysis = 0.18f * std::sin(
                2.0f * s3g::kPi * frequency
                * static_cast<float>(frame)
                / static_cast<float>(kSampleRate));
            rangeBank.setGesture({});
            // One register is deliberately anti-phase stereo. The audible
            // analysis bank remains stereo, while pitch acquisition must not
            // disappear in a null-prone L+R tracking sum.
            const float analysisRight = frequency == 1300.0f
                ? -analysis : analysis;
            const auto output = rangeBank.processFrameStereo(
                analysis, analysisRight, 0.0f, 0.0f, false);
            if (frame >= kSampleRate) {
                rangeEnergy += static_cast<double>(output.left) * output.left;
            }
            rangeFinite = rangeFinite && std::isfinite(output.left)
                && std::isfinite(output.right);
        }
        const auto rangeMeters = rangeBank.meterSnapshot();
        const double rangeRms = std::sqrt(rangeEnergy
            / static_cast<double>(rangeFrames - kSampleRate));
        const float allowedError = std::max(2.0f, frequency * 0.025f);
        if (!rangeFinite || !rangeMeters.pitchActive || rangeRms < 0.005
            || std::abs(rangeMeters.detectedPitchHz - frequency)
                > allowedError) {
            std::cerr << "voice pitch register failed at " << frequency
                      << " Hz: detected/active/RMS "
                      << rangeMeters.detectedPitchHz << '/'
                      << rangeMeters.pitchActive << '/' << rangeRms << '\n';
            return false;
        }
    }
    return true;
}

bool eightPoleAnalysisProbe()
{
    constexpr std::array<std::array<float, 3u>, 2u> formants {{
        {{ 270.0f, 2290.0f, 3010.0f }}, // EE
        {{ 300.0f,  870.0f, 2240.0f }}, // OO
    }};
    constexpr std::array<std::array<float, 3u>, 2u> bandwidths {{
        {{ 75.0f, 130.0f, 190.0f }},
        {{ 80.0f, 100.0f, 170.0f }},
    }};
    const auto analyze = [&](s3g::AcapellaResonatorAnalysisSlope slope,
                             uint32_t vowel) {
        s3g::AcapellaResonatorParams params;
        params.amount = 1.0f;
        params.mode = s3g::AcapellaResonatorMode::Vocoder;
        params.modulatorSource
            = s3g::AcapellaResonatorModulatorSource::ExternalMic;
        params.analysisSlope = slope;
        params.analysisBlend = 0.0f;
        params.attackMs = 2.0f;
        params.releaseMs = 65.0f;
        params.blurMs = 4.0f;
        params.carrierShape = s3g::AcapellaResonatorCarrierShape::Saw;
        params.voicingMode = s3g::AcapellaResonatorVoicingMode::Tonal;
        params.driveDb = 0.0f;
        s3g::AcapellaResonatorBank bank;
        bank.setParams(params);
        bank.prepare(kSampleRate);
        for (uint32_t frame = 0u; frame < kSampleRate; ++frame) {
            s3g::AcapellaResonatorGesture gesture;
            gesture.active = true;
            gesture.carrierOnly = true;
            gesture.frequencyHz = 109.0f;
            gesture.voiceInstance = 0x7300u;
            gesture.voiceCount = 1u;
            gesture.voiceFrequencyHz[0] = 109.0f;
            gesture.voiceGain[0] = 1.0f;
            gesture.voiceInstanceIds[0] = gesture.voiceInstance;
            bank.setGesture(gesture);
            const float input = syntheticVowelSample(
                frame, formants[vowel], bandwidths[vowel]);
            (void)bank.processFrameStereo(
                input, input, 0.0f, 0.0f, false);
        }
        return bank.meterSnapshot();
    };
    const auto fourEe = analyze(
        s3g::AcapellaResonatorAnalysisSlope::FourPole, 0u);
    const auto fourOo = analyze(
        s3g::AcapellaResonatorAnalysisSlope::FourPole, 1u);
    const auto eightEe = analyze(
        s3g::AcapellaResonatorAnalysisSlope::EightPole, 0u);
    const auto eightOo = analyze(
        s3g::AcapellaResonatorAnalysisSlope::EightPole, 1u);
    const float fourDistance = normalizedVectorDistance(
        fourEe.analysis, fourOo.analysis);
    const float eightDistance = normalizedVectorDistance(
        eightEe.analysis, eightOo.analysis);
    if (fourDistance < 0.12f
        || eightDistance < fourDistance * 1.10f) {
        std::cerr << "8-pole analyzer did not improve formant separation: "
                  << fourDistance << '/' << eightDistance << '\n';
        return false;
    }
    return true;
}

bool sampleRateAndRetriggerProbe()
{
    constexpr std::array<uint32_t, 4u> sampleRates {{
        8000u, 44100u, 96000u, 192000u,
    }};
    for (const uint32_t sampleRate : sampleRates) {
        s3g::AcapellaResonatorParams params;
        params.amount = 1.0f;
        params.mode = s3g::AcapellaResonatorMode::Resonator;
        params.carrierShape = s3g::AcapellaResonatorCarrierShape::Fold;
        params.carrierHarmonics = 1.0f;
        params.carrierNoise = 0.40f;
        params.resonance = 1.0f;
        params.driveDb = 24.0f;
        params.releaseMs = 1500.0f;
        params.bandShiftSemitones = 24.0f;
        params.bandStretch = 1.0f;
        params.tilt = 1.0f;
        params.matrixMode = s3g::AcapellaResonatorMatrixMode::Chord;
        params.matrixMorph = 1.0f;
        params.freeze = 0.72f;
        params.blurMs = 1200.0f;
        const uint32_t frames = sampleRate * 3u;
        const auto rendered = renderBank(params, frames,
            false, false, true, false, s3g::AcapellaPhoneme::AA,
            sampleRate);
        const uint32_t measureBegin = std::min<uint32_t>(frames,
            static_cast<uint32_t>(0.08f * sampleRate));
        if (!rendered.finite || rendered.peak > 2.0f
            || rms(rendered.left, measureBegin) < 0.002) {
            std::cerr << "resonator sample-rate edge failed at "
                      << sampleRate << " Hz: peak " << rendered.peak
                      << ", RMS " << rms(rendered.left, measureBegin) << '\n';
            return false;
        }
    }

    s3g::AcapellaResonatorParams params;
    params.amount = 1.0f;
    params.mode = s3g::AcapellaResonatorMode::Hybrid;
    params.carrierShape = s3g::AcapellaResonatorCarrierShape::Saw;
    params.resonance = 0.72f;
    params.driveDb = 8.0f;
    params.analysisBlend = 0.76f;
    s3g::AcapellaResonatorBank bank;
    bank.setParams(params);
    if (!bank.prepare(kSampleRate)) return false;
    constexpr uint32_t frames = 36000u;
    constexpr uint32_t retriggerFrame = 16000u;
    std::vector<float> signal(frames, 0.0f);
    for (uint32_t frame = 0u; frame < frames; ++frame) {
        const bool retriggered = frame >= retriggerFrame;
        auto gesture = gestureFor(frame, false,
            retriggered ? s3g::AcapellaPhoneme::IY
                        : s3g::AcapellaPhoneme::AA);
        gesture.frequencyHz = retriggered ? 196.0f : 118.0f;
        gesture.voiceInstance = retriggered ? 38u : 37u;
        gesture.voiceFrequencyHz[0] = gesture.frequencyHz;
        gesture.voiceInstanceIds[0] = gesture.voiceInstance;
        gesture.stepIndex = retriggered ? 1u : 0u;
        gesture.flags = s3g::kAcapellaSyllableStart
            | (retriggered ? 0u : s3g::kAcapellaWordStart);
        bank.setGesture(gesture);
        const float analysis = analysisSignal(frame, false);
        const auto output = bank.processFrameStereo(
            analysis, analysis, 0.0f, 0.0f);
        signal[frame] = output.left;
        if (!std::isfinite(output.left) || !std::isfinite(output.right)) {
            std::cerr << "resonator retrigger produced non-finite audio\n";
            return false;
        }
    }
    const double localRms = rms(signal, retriggerFrame - 480u,
        retriggerFrame + 960u);
    float maximumStep = 0.0f;
    for (uint32_t frame = retriggerFrame - 32u;
         frame < retriggerFrame + 960u; ++frame) {
        maximumStep = std::max(maximumStep,
            std::abs(signal[frame] - signal[frame - 1u]));
    }
    if (localRms < 0.002
        || maximumStep > std::max(0.06f,
            static_cast<float>(localRms * 6.0))) {
        std::cerr << "resonator note retrigger clicked: RMS " << localRms
                  << ", step " << maximumStep << '\n';
        return false;
    }

    // Exercise ensemble-style compaction: when the first voice is released,
    // the surviving voice moves from array index 1 to 0 but retains its ID.
    // Carrier phase must follow voiceInstanceIds rather than array position.
    bank.reset();
    bank.setParams(params);
    constexpr uint32_t compactFrame = 18000u;
    std::fill(signal.begin(), signal.end(), 0.0f);
    for (uint32_t frame = 0u; frame < frames; ++frame) {
        auto gesture = gestureFor(frame, false, s3g::AcapellaPhoneme::AA);
        if (frame < compactFrame) {
            gesture.voiceCount = 2u;
            gesture.voiceFrequencyHz[0] = 118.0f;
            gesture.voiceFrequencyHz[1] = 176.0f;
            gesture.voiceGain[0] = 0.70710678f;
            gesture.voiceGain[1] = 0.70710678f;
            gesture.voiceInstanceIds[0] = 41u;
            gesture.voiceInstanceIds[1] = 42u;
        } else {
            gesture.voiceCount = 1u;
            gesture.voiceFrequencyHz[0] = 176.0f;
            gesture.voiceGain[0] = 1.0f;
            gesture.voiceInstanceIds[0] = 42u;
            gesture.voiceFrequencyHz[1] = 0.0f;
            gesture.voiceGain[1] = 0.0f;
            gesture.voiceInstanceIds[1] = 0u;
        }
        gesture.frequencyHz = 176.0f;
        gesture.voiceInstance = 42u;
        bank.setGesture(gesture);
        const float analysis = analysisSignal(frame, false);
        const auto output = bank.processFrameStereo(
            analysis, analysis, 0.0f, 0.0f);
        signal[frame] = output.left;
        if (!std::isfinite(output.left) || !std::isfinite(output.right)) {
            std::cerr << "polyphonic carrier compaction became non-finite\n";
            return false;
        }
    }
    const double compactRms = rms(signal, compactFrame - 480u,
        compactFrame + 960u);
    maximumStep = 0.0f;
    for (uint32_t frame = compactFrame - 32u;
         frame < compactFrame + 960u; ++frame) {
        maximumStep = std::max(maximumStep,
            std::abs(signal[frame] - signal[frame - 1u]));
    }
    if (compactRms < 0.002
        || maximumStep > std::max(0.06f,
            static_cast<float>(compactRms * 6.0))) {
        std::cerr << "polyphonic carrier ID compaction clicked: RMS "
                  << compactRms << ", step " << maximumStep << '\n';
        return false;
    }
    std::vector<float> singleSignal(compactFrame, 0.0f);
    bank.reset();
    bank.setParams(params);
    for (uint32_t frame = 0u; frame < compactFrame; ++frame) {
        auto gesture = gestureFor(frame, false, s3g::AcapellaPhoneme::AA);
        gesture.frequencyHz = 176.0f;
        gesture.voiceInstance = 42u;
        gesture.voiceCount = 1u;
        gesture.voiceFrequencyHz[0] = 176.0f;
        gesture.voiceGain[0] = 1.0f;
        gesture.voiceInstanceIds[0] = 42u;
        bank.setGesture(gesture);
        const float analysis = analysisSignal(frame, false);
        singleSignal[frame] = bank.processFrameStereo(
            analysis, analysis, 0.0f, 0.0f).left;
    }
    const double polyRelativeDelta = differenceRms(signal, singleSignal,
        2048u, compactFrame - 512u)
        / std::max(1.0e-6, rms(singleSignal, 2048u, compactFrame - 512u));
    if (polyRelativeDelta < 0.05) {
        std::cerr << "internal carrier ignored polyphonic gesture voices: "
                  << polyRelativeDelta << '\n';
        return false;
    }
    return true;
}

Render renderBank(s3g::AcapellaResonatorParams params,
    uint32_t frames, bool sideOnly, bool automate, bool zeroCarrier,
    bool zeroAnalysis, s3g::AcapellaPhoneme forcedPhoneme,
    uint32_t sampleRate)
{
    s3g::AcapellaResonatorBank bank;
    bank.setParams(params);
    Render render;
    if (!bank.prepare(sampleRate)) {
        render.finite = false;
        return render;
    }
    render.left.resize(frames);
    render.right.resize(frames);
    render.carrierLeft.resize(frames);
    render.carrierRight.resize(frames);
    float previousLeft = 0.0f;
    for (uint32_t frame = 0u; frame < frames; ++frame) {
        const float referenceFrame = static_cast<float>(frame)
            * static_cast<float>(kSampleRate) / static_cast<float>(sampleRate);
        const uint32_t sourceFrame = static_cast<uint32_t>(referenceFrame);
        const bool consonant = (sourceFrame / 8000u) % 3u == 1u;
        auto gesture = gestureFor(sourceFrame, consonant, forcedPhoneme);
        bank.setGesture(gesture);
        if (automate && (frame % 4096u) == 0u) {
            const uint32_t stage = frame / 4096u;
            params.mode = static_cast<s3g::AcapellaResonatorMode>(stage % 3u);
            params.carrierShape = static_cast<
                s3g::AcapellaResonatorCarrierShape>(stage % 5u);
            params.amount = 0.72f + 0.28f * static_cast<float>(stage & 1u);
            params.resonance = (stage & 2u) ? 1.0f : 0.58f;
            params.driveDb = (stage & 4u) ? 24.0f : 4.0f;
            params.bandShiftSemitones = (stage & 8u) ? 24.0f : -24.0f;
            params.bandStretch = (stage & 16u) ? 1.0f : -1.0f;
            params.matrixMorph = static_cast<float>(stage % 5u) * 0.25f;
            params.matrixMode = static_cast<
                s3g::AcapellaResonatorMatrixMode>(stage % 5u);
            params.freeze = (stage & 1u) ? 1.0f : 0.0f;
            params.freezeTrigger = static_cast<
                s3g::AcapellaResonatorFreezeTrigger>(stage % 6u);
            params.blurMs = (stage & 2u) ? 2000.0f : 0.0f;
            bank.setParams(params);
        }
        const uint32_t activeFrames = static_cast<uint32_t>(
            1.5f * static_cast<float>(sampleRate));
        const float analysis = !zeroAnalysis && frame < activeFrames
            ? analysisSignal(sourceFrame, consonant) : 0.0f;
        const float carrierL = !zeroCarrier && frame < activeFrames
            ? carrierSignal(sourceFrame, 0.0f) : 0.0f;
        const float carrierR = !zeroCarrier && frame < activeFrames
            ? carrierSignal(sourceFrame, sideOnly ? s3g::kPi : 0.23f) : 0.0f;
        const float analysisL = sideOnly ? analysis : analysis * 0.94f;
        const float analysisR = sideOnly ? -analysis : analysis * 1.06f;
        const auto output = bank.processFrameStereo(
            analysisL, analysisR, carrierL, carrierR);
        render.left[frame] = output.left;
        render.right[frame] = output.right;
        // Bank Mix is defined against the selected carrier after the
        // External/Internal blend and External Gain, not the raw host input.
        render.carrierLeft[frame] = output.dryLeft;
        render.carrierRight[frame] = output.dryRight;
        const float peak = std::max(std::abs(output.left), std::abs(output.right));
        render.peak = std::max(render.peak, peak);
        render.maximumStep = std::max(render.maximumStep,
            std::abs(output.left - previousLeft));
        previousLeft = output.left;
        if (!std::isfinite(output.left) || !std::isfinite(output.right)
            || peak > 2.01f) {
            render.finite = false;
            break;
        }
    }
    return render;
}

double difference(const std::vector<float>& a, const std::vector<float>& b)
{
    double result = 0.0;
    const size_t count = std::min(a.size(), b.size());
    for (size_t index = 0u; index < count; ++index) {
        result += std::abs(static_cast<double>(a[index]) - b[index]);
    }
    return result;
}

double rms(const std::vector<float>& values, uint32_t begin, uint32_t end)
{
    begin = std::min<uint32_t>(begin, values.size());
    end = std::min<uint32_t>(end, values.size());
    if (end <= begin) return 0.0;
    double sum = 0.0;
    for (uint32_t index = begin; index < end; ++index) {
        sum += static_cast<double>(values[index]) * values[index];
    }
    return std::sqrt(sum / static_cast<double>(end - begin));
}

double differenceRms(const std::vector<float>& a,
    const std::vector<float>& b, uint32_t begin, uint32_t end)
{
    begin = std::min<uint32_t>(begin, std::min(a.size(), b.size()));
    end = std::min<uint32_t>(end, std::min(a.size(), b.size()));
    if (end <= begin) return 0.0;
    double sum = 0.0;
    for (uint32_t index = begin; index < end; ++index) {
        const double delta = static_cast<double>(a[index]) - b[index];
        sum += delta * delta;
    }
    return std::sqrt(sum / static_cast<double>(end - begin));
}

double toneAmplitude(const std::vector<float>& values, float frequencyHz,
    uint32_t begin, uint32_t end, float sampleRate = kSampleRate)
{
    begin = std::min<uint32_t>(begin, values.size());
    end = std::min<uint32_t>(end, values.size());
    if (end <= begin + 1u) return 0.0;
    double real = 0.0;
    double imaginary = 0.0;
    double windowSum = 0.0;
    const double phaseIncrement = 2.0 * static_cast<double>(s3g::kPi)
        * static_cast<double>(frequencyHz) / static_cast<double>(sampleRate);
    const double denominator = static_cast<double>(end - begin - 1u);
    for (uint32_t frame = begin; frame < end; ++frame) {
        const double position = static_cast<double>(frame - begin)
            / denominator;
        const double window = 0.5 - 0.5 * std::cos(
            2.0 * static_cast<double>(s3g::kPi) * position);
        const double phase = phaseIncrement * static_cast<double>(frame);
        const double sample = static_cast<double>(values[frame]) * window;
        real += sample * std::cos(phase);
        imaginary -= sample * std::sin(phase);
        windowSum += window;
    }
    return windowSum > 0.0
        ? 2.0 * std::sqrt(real * real + imaginary * imaginary) / windowSum
        : 0.0;
}

double bandpassRms(const std::vector<float>& values, float centerHz, float q,
    uint32_t begin, uint32_t end, float sampleRate = kSampleRate)
{
    begin = std::min<uint32_t>(begin, values.size());
    end = std::min<uint32_t>(end, values.size());
    if (end <= begin) return 0.0;
    // Independent RBJ analysis filter; do not reuse the implementation under
    // test to decide whether its high bands survived.
    const double omega = 2.0 * static_cast<double>(s3g::kPi)
        * static_cast<double>(centerHz) / static_cast<double>(sampleRate);
    const double alpha = std::sin(omega) / (2.0 * std::max(0.1f, q));
    const double inverseA0 = 1.0 / (1.0 + alpha);
    const double b0 = alpha * inverseA0;
    const double b2 = -b0;
    const double a1 = -2.0 * std::cos(omega) * inverseA0;
    const double a2 = (1.0 - alpha) * inverseA0;
    double x1 = 0.0;
    double x2 = 0.0;
    double y1 = 0.0;
    double y2 = 0.0;
    double sum = 0.0;
    uint32_t measured = 0u;
    for (uint32_t frame = 0u; frame < end; ++frame) {
        const double x0 = values[frame];
        const double band = b0 * x0 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1;
        x1 = x0;
        y2 = y1;
        y1 = band;
        if (frame >= begin) {
            sum += band * band;
            ++measured;
        }
    }
    return measured > 0u ? std::sqrt(sum / static_cast<double>(measured))
                         : 0.0;
}

bool neutralAndModeProbe()
{
    s3g::AcapellaResonatorBank reporting;
    s3g::AcapellaResonatorParams reportingParams;
    reportingParams.amount = 1.0f;
    reportingParams.resonance = 0.82f;
    reportingParams.releaseMs = 600.0f;
    reporting.setParams(reportingParams);
    if (!reporting.prepare(kSampleRate) || reporting.latencySamples() != 0u
        || reporting.tailSamples() == 0u) {
        std::cerr << "resonator latency/tail reporting contract failed\n";
        return false;
    }

    s3g::AcapellaResonatorParams neutral;
    neutral.amount = 0.0f;
    const auto dry = renderBank(neutral, 48000u);
    if (!dry.finite || difference(dry.left, dry.carrierLeft) > 1.0e-3
        || difference(dry.right, dry.carrierRight) > 1.0e-3) {
        std::cerr << "resonator amount-zero bypass was not transparent\n";
        return false;
    }

    constexpr std::array<s3g::AcapellaResonatorMode, 3u> modes {{
        s3g::AcapellaResonatorMode::Vocoder,
        s3g::AcapellaResonatorMode::Hybrid,
        s3g::AcapellaResonatorMode::Resonator,
    }};
    std::array<Render, modes.size()> modeRenders;
    for (uint32_t index = 0u; index < modes.size(); ++index) {
        s3g::AcapellaResonatorParams params;
        params.amount = 1.0f;
        params.mode = modes[index];
        params.carrierHarmonics = 0.82f;
        params.carrierNoise = 0.18f;
        params.analysisBlend = 0.76f;
        params.resonance = 0.72f;
        params.matrixMorph = 0.44f;
        modeRenders[index] = renderBank(params, 48000u);
        const double wetRms = rms(modeRenders[index].left, 2048u);
        const double deltaRms = differenceRms(modeRenders[index].left,
            modeRenders[index].carrierLeft, 2048u);
        const double dryRms = rms(modeRenders[index].carrierLeft, 2048u);
        if (!modeRenders[index].finite || wetRms < 0.003
            || deltaRms / std::max(1.0e-6, dryRms) < 0.08
            || modeRenders[index].maximumStep > 0.65f) {
            std::cerr << "resonator mode was silent, collapsed, or discontinuous: "
                      << index << ", wet RMS " << wetRms
                      << ", relative delta "
                      << deltaRms / std::max(1.0e-6, dryRms) << '\n';
            return false;
        }
    }
    for (uint32_t a = 0u; a < modeRenders.size(); ++a) {
        for (uint32_t b = a + 1u; b < modeRenders.size(); ++b) {
            const double delta = differenceRms(modeRenders[a].left,
                modeRenders[b].left, 2048u);
            const double reference = std::max(1.0e-6,
                rms(modeRenders[a].left, 2048u));
            if (delta / reference < 0.035) {
                std::cerr << "resonator modes collapsed together: "
                          << a << " / " << b << '\n';
                return false;
            }
        }
    }

    s3g::AcapellaResonatorParams unmodulated;
    unmodulated.amount = 1.0f;
    unmodulated.analysisBlend = 0.0f;
    unmodulated.mode = s3g::AcapellaResonatorMode::Vocoder;
    const auto silentVocoder = renderBank(unmodulated, 48000u,
        false, false, true, true);
    unmodulated.mode = s3g::AcapellaResonatorMode::Resonator;
    const auto soundingResonator = renderBank(unmodulated, 48000u,
        false, false, true, true);
    if (!silentVocoder.finite || !soundingResonator.finite
        || rms(soundingResonator.left, 2048u) < 0.003
        || rms(soundingResonator.left, 2048u)
            < rms(silentVocoder.left, 2048u) * 2.0) {
        std::cerr << "vocoder and independently driven resonator collapsed\n";
        return false;
    }
    return true;
}

bool selectedCarrierDryRoutingProbe()
{
    s3g::AcapellaResonatorParams externalLow;
    externalLow.amount = 0.0f;
    externalLow.externalCarrierMix = 1.0f;
    externalLow.externalCarrierGainDb = -24.0f;
    // Isolate the tonal path for the external-gain ratio. The dry carrier is
    // now intentionally post voiced/unvoiced texture, so a noise component
    // would not be expected to follow External Carrier Gain.
    externalLow.voicingMode = s3g::AcapellaResonatorVoicingMode::Tonal;
    externalLow.carrierNoise = 0.0f;
    externalLow.sibilance = 0.0f;
    const auto lowGain = renderBank(externalLow, 48000u);
    auto externalUnity = externalLow;
    externalUnity.externalCarrierGainDb = 0.0f;
    const auto unityGain = renderBank(externalUnity, 48000u);
    const double lowRms = rms(lowGain.left, 2048u);
    const double unityRms = rms(unityGain.left, 2048u);
    if (!lowGain.finite || !unityGain.finite || unityRms < 0.02
        || unityRms < lowRms * 10.0) {
        std::cerr << "Amount-zero external carrier mix/gain was ineffective: "
                  << lowRms << '/' << unityRms << '\n';
        return false;
    }

    s3g::AcapellaResonatorParams internal;
    internal.amount = 0.0f;
    internal.externalCarrierMix = 0.0f;
    internal.externalCarrierGainDb = 24.0f;
    const auto internalWithInput = renderBank(internal, 48000u);
    const auto internalWithoutInput = renderBank(internal, 48000u,
        false, false, true);
    const double internalRms = rms(internalWithInput.left, 2048u);
    const double externalLeak = differenceRms(internalWithInput.left,
        internalWithoutInput.left, 2048u)
        / std::max(1.0e-6, internalRms);
    if (!internalWithInput.finite || !internalWithoutInput.finite
        || internalRms < 0.003 || externalLeak > 1.0e-4) {
        std::cerr << "Amount-zero internal carrier route was lost or leaked "
                  << "external input: RMS/leak " << internalRms << '/'
                  << externalLeak << '\n';
        return false;
    }
    return true;
}

bool carrierAndControlAxesProbe()
{
    s3g::AcapellaResonatorParams base;
    base.amount = 1.0f;
    base.mode = s3g::AcapellaResonatorMode::Hybrid;
    base.analysisBlend = 0.72f;
    base.gestureFollow = 1.0f;
    base.resonance = 0.68f;
    base.matrixMorph = 1.0f;

    constexpr std::array<s3g::AcapellaResonatorCarrierShape, 5u> shapes {{
        s3g::AcapellaResonatorCarrierShape::Glottal,
        s3g::AcapellaResonatorCarrierShape::Saw,
        s3g::AcapellaResonatorCarrierShape::Pulse,
        s3g::AcapellaResonatorCarrierShape::Fold,
        s3g::AcapellaResonatorCarrierShape::Noise,
    }};
    std::array<Render, shapes.size()> carrierRenders;
    for (uint32_t index = 0u; index < shapes.size(); ++index) {
        auto params = base;
        params.carrierShape = shapes[index];
        carrierRenders[index] = renderBank(params, 36000u,
            false, false, true);
        if (!carrierRenders[index].finite
            || rms(carrierRenders[index].left, 2048u) < 0.003) {
            std::cerr << "internal carrier shape was inaudible: "
                      << index << '\n';
            return false;
        }
        if (index > 0u) {
            const double relative = differenceRms(
                carrierRenders[index - 1u].left,
                carrierRenders[index].left, 2048u)
                / std::max(1.0e-6,
                    rms(carrierRenders[index - 1u].left, 2048u));
            if (relative < 0.035) {
                std::cerr << "adjacent carrier shapes were indistinguishable: "
                          << index - 1u << " / " << index << '\n';
                return false;
            }
        }
    }

    constexpr std::array<s3g::AcapellaPhoneme, 3u> vowels {{
        s3g::AcapellaPhoneme::AA,
        s3g::AcapellaPhoneme::IY,
        s3g::AcapellaPhoneme::UW,
    }};
    std::array<Render, vowels.size()> vowelRenders;
    for (uint32_t index = 0u; index < vowels.size(); ++index) {
        auto params = base;
        params.analysisBlend = 1.0f;
        vowelRenders[index] = renderBank(params, 36000u,
            false, false, true, false, vowels[index]);
        if (!vowelRenders[index].finite
            || rms(vowelRenders[index].left, 2048u) < 0.003) {
            std::cerr << "phoneme target was inaudible\n";
            return false;
        }
    }
    if (differenceRms(vowelRenders[0].left, vowelRenders[1].left, 2048u)
            / std::max(1.0e-6, rms(vowelRenders[0].left, 2048u)) < 0.035
        || differenceRms(vowelRenders[1].left, vowelRenders[2].left, 2048u)
            / std::max(1.0e-6, rms(vowelRenders[1].left, 2048u)) < 0.035) {
        std::cerr << "phoneme target envelopes collapsed together\n";
        return false;
    }

    const auto axisChanged = [&](s3g::AcapellaResonatorParams low,
                                 s3g::AcapellaResonatorParams high,
                                 const char* name) {
        const auto a = renderBank(low, 32000u, false, false, true);
        const auto b = renderBank(high, 32000u, false, false, true);
        const double reference = std::max(1.0e-6, rms(a.left, 2048u));
        const double relative = differenceRms(a.left, b.left, 2048u)
            / reference;
        if (!a.finite || !b.finite || rms(a.left, 2048u) < 0.002
            || rms(b.left, 2048u) < 0.002 || relative < 0.025) {
            std::cerr << "resonator control axis was not observable: "
                      << name << ", relative delta " << relative << '\n';
            return false;
        }
        return true;
    };

    auto low = base;
    auto high = base;
    low.carrierHarmonics = 0.05f; high.carrierHarmonics = 1.0f;
    if (!axisChanged(low, high, "carrier harmonics")) return false;
    low = base; high = base;
    low.carrierColor = -1.0f; high.carrierColor = 1.0f;
    if (!axisChanged(low, high, "carrier color")) return false;
    low = base; high = base;
    low.carrierNoise = 0.0f; high.carrierNoise = 1.0f;
    if (!axisChanged(low, high, "carrier noise")) return false;
    low = base; high = base;
    low.analysisBlend = 0.0f; high.analysisBlend = 1.0f;
    if (!axisChanged(low, high, "analysis blend")) return false;
    low = base; high = base;
    low.attackMs = 0.5f; high.attackMs = 120.0f;
    if (!axisChanged(low, high, "attack")) return false;
    low = base; high = base;
    low.releaseMs = 5.0f; high.releaseMs = 1500.0f;
    if (!axisChanged(low, high, "release")) return false;
    low = base; high = base;
    low.gestureFollow = 0.0f; high.gestureFollow = 1.0f;
    if (!axisChanged(low, high, "gesture follow")) return false;
    low = base; high = base;
    low.resonance = 0.05f; high.resonance = 1.0f;
    if (!axisChanged(low, high, "resonance")) return false;
    low = base; high = base;
    low.driveDb = 0.0f; high.driveDb = 24.0f;
    if (!axisChanged(low, high, "drive")) return false;
    low = base; high = base;
    low.bandShiftSemitones = -18.0f; high.bandShiftSemitones = 18.0f;
    if (!axisChanged(low, high, "band shift")) return false;
    low = base; high = base;
    low.bandStretch = -0.8f; high.bandStretch = 0.8f;
    if (!axisChanged(low, high, "band stretch")) return false;
    low = base; high = base;
    low.tilt = -1.0f; high.tilt = 1.0f;
    if (!axisChanged(low, high, "tilt")) return false;
    low = base; high = base;
    low.blurMs = 0.0f; high.blurMs = 1200.0f;
    if (!axisChanged(low, high, "blur")) return false;
    low = base; high = base;
    low.matrixMode = s3g::AcapellaResonatorMatrixMode::Chord;
    high.matrixMode = s3g::AcapellaResonatorMatrixMode::Chord;
    low.matrixMorph = 0.0f; high.matrixMorph = 1.0f;
    if (!axisChanged(low, high, "matrix morph")) return false;
    low = base; high = base;
    low.stereoSpread = 0.0f; high.stereoSpread = 1.0f;
    if (!axisChanged(low, high, "stereo spread")) return false;
    low = base; high = base;
    low.freeze = 1.0f; high.freeze = 1.0f;
    low.freezeTrigger = s3g::AcapellaResonatorFreezeTrigger::Syllable;
    high.freezeTrigger = s3g::AcapellaResonatorFreezeTrigger::Word;
    if (!axisChanged(low, high, "freeze trigger")) return false;

    constexpr std::array<s3g::AcapellaResonatorMatrixMode, 5u> matrices {{
        s3g::AcapellaResonatorMatrixMode::Identity,
        s3g::AcapellaResonatorMatrixMode::Rotate,
        s3g::AcapellaResonatorMatrixMode::Mirror,
        s3g::AcapellaResonatorMatrixMode::Chord,
        s3g::AcapellaResonatorMatrixMode::Sparse,
    }};
    auto identityParams = base;
    identityParams.matrixMode = matrices[0];
    const auto identity = renderBank(identityParams, 32000u,
        false, false, true);
    for (uint32_t index = 1u; index < matrices.size(); ++index) {
        auto params = base;
        params.matrixMode = matrices[index];
        const auto transformed = renderBank(params, 32000u,
            false, false, true);
        const double relative = differenceRms(identity.left,
            transformed.left, 2048u)
            / std::max(1.0e-6, rms(identity.left, 2048u));
        if (!transformed.finite || relative < 0.035) {
            std::cerr << "matrix mode was not observable: " << index
                      << ", relative delta " << relative << '\n';
            return false;
        }
    }
    return true;
}

bool stereoFreezeAndConsonantProbe()
{
    s3g::AcapellaResonatorParams params;
    params.amount = 1.0f;
    params.mode = s3g::AcapellaResonatorMode::Hybrid;
    params.resonance = 0.78f;
    params.analysisBlend = 0.84f;
    params.sibilance = 1.0f;
    params.stereoSpread = 0.82f;
    const auto side = renderBank(params, 60000u, true);
    const double sideRms = rms(side.left, 2048u);
    if (!side.finite || sideRms < 0.003
        || differenceRms(side.left, side.carrierLeft, 2048u)
            / std::max(1.0e-6, rms(side.carrierLeft, 2048u)) < 0.08
        || differenceRms(side.left, side.right, 2048u)
            / std::max(1.0e-6, sideRms) < 0.05) {
        std::cerr << "resonator side-only stereo path was lost or bypassed\n";
        return false;
    }

    params.freezeTrigger = s3g::AcapellaResonatorFreezeTrigger::Syllable;
    params.blurMs = 480.0f;
    params.releaseMs = 900.0f;
    params.freeze = 0.0f;
    const auto unfrozen = renderBank(params, 144000u);
    params.freeze = 1.0f;
    const auto frozen = renderBank(params, 144000u);
    const double unfrozenTailRms = rms(unfrozen.left, 74000u, 104000u);
    const double frozenTailRms = rms(frozen.left, 74000u, 104000u);
    const double frozenLateRms = rms(frozen.left, 132000u, 144000u);
    if (!unfrozen.finite || !frozen.finite || frozen.peak > 2.0f
        || frozenTailRms < 0.001
        || frozenTailRms < unfrozenTailRms * 1.18
        || frozenLateRms > frozenTailRms * 0.82
        || frozen.maximumStep > 0.65f) {
        std::cerr << "resonator envelope freeze was unstable or had no tail\n";
        return false;
    }

    auto lowSibilance = params;
    lowSibilance.freeze = 0.0f;
    lowSibilance.sibilance = 0.0f;
    lowSibilance.gestureFollow = 1.0f;
    auto highSibilance = lowSibilance;
    highSibilance.sibilance = 1.0f;
    highSibilance.gestureFollow = 1.0f;
    constexpr std::array<s3g::AcapellaPhoneme, 4u> consonants {{
        s3g::AcapellaPhoneme::S,
        s3g::AcapellaPhoneme::SH,
        s3g::AcapellaPhoneme::T,
        s3g::AcapellaPhoneme::K,
    }};
    constexpr uint32_t consonantBegin = 7900u;
    constexpr uint32_t consonantEnd = 15000u;
    for (const auto phoneme : consonants) {
        const auto soft = renderBank(lowSibilance, 48000u,
            false, false, false, false, phoneme);
        const auto articulated = renderBank(highSibilance, 48000u,
            false, false, false, false, phoneme);
        const double reference = std::max(1.0e-6,
            rms(soft.left, consonantBegin, consonantEnd));
        const double consonantDelta = differenceRms(soft.left,
            articulated.left, consonantBegin, consonantEnd);
        double consonantEnergy = 0.0;
        float consonantPeak = 0.0f;
        float consonantStep = 0.0f;
        float previous = articulated.left[consonantBegin];
        for (uint32_t frame = consonantBegin; frame < consonantEnd; ++frame) {
            const float sample = articulated.left[frame] - soft.left[frame];
            consonantEnergy += static_cast<double>(sample) * sample;
            consonantPeak = std::max(consonantPeak, std::abs(sample));
            consonantStep = std::max(consonantStep,
                std::abs(articulated.left[frame] - previous));
            previous = articulated.left[frame];
        }
        const double consonantRms = std::sqrt(consonantEnergy
            / static_cast<double>(consonantEnd - consonantBegin));
        if (!soft.finite || !articulated.finite
            || consonantDelta / reference < 0.05
            || consonantRms < 1.0e-4
            || consonantPeak > consonantRms * 10.0 + 1.0e-4
            || consonantStep > std::max(0.08f,
                static_cast<float>(consonantRms * 6.0))) {
            std::cerr << "consonant rail was ineffective or snare-like: "
                      << static_cast<uint32_t>(phoneme)
                      << ", RMS " << consonantRms
                      << ", peak " << consonantPeak
                      << ", step " << consonantStep << '\n';
            return false;
        }
    }
    return true;
}

bool fullCarrierReplacementProbe()
{
    constexpr std::array<float, s3g::kAcapellaResonatorMaxVoices>
        oldFrequencies {{ 71.0f, 83.0f, 97.0f, 113.0f,
            131.0f, 151.0f, 173.0f, 197.0f }};
    constexpr std::array<float, s3g::kAcapellaResonatorMaxVoices>
        newFrequencies {{ 233.0f, 307.0f, 401.0f, 521.0f,
            677.0f, 877.0f, 1129.0f, 1451.0f }};
    constexpr uint32_t replacementFrame = 16000u;
    constexpr uint32_t measurementBegin = 32000u;
    constexpr uint32_t frames = 60000u;

    s3g::AcapellaResonatorParams params;
    params.amount = 1.0f;
    params.mode = s3g::AcapellaResonatorMode::Resonator;
    params.carrierShape = s3g::AcapellaResonatorCarrierShape::Glottal;
    params.carrierHarmonics = 0.0f;
    params.carrierNoise = 0.0f;
    params.analysisBlend = 1.0f;
    params.attackMs = 2.0f;
    params.releaseMs = 80.0f;
    params.resonance = 0.35f;
    params.driveDb = 0.0f;
    params.sibilance = 0.0f;
    params.stereoSpread = 0.0f;
    params.blurMs = 0.0f;
    params.gestureFollow = 1.0f;

    const auto renderReplacement = [&](bool fullNewSet) {
        s3g::AcapellaResonatorBank bank;
        bank.setParams(params);
        std::vector<float> signal(frames, 0.0f);
        bool finite = bank.prepare(kSampleRate);
        for (uint32_t frame = 0u; frame < frames && finite; ++frame) {
            const bool replaced = frame >= replacementFrame;
            const auto& frequencies = replaced
                ? newFrequencies : oldFrequencies;
            s3g::AcapellaResonatorGesture gesture;
            gesture.phoneme = s3g::AcapellaPhoneme::AA;
            gesture.active = true;
            gesture.frequencyHz = frequencies.back();
            gesture.voiceInstance = replaced ? 207u : 107u;
            gesture.stepIndex = replaced ? 1u : 0u;
            gesture.stepProgress = 0.5f;
            gesture.flags = frame == 0u
                ? s3g::kAcapellaWordStart : 0u;
            const uint32_t firstVoice = replaced && !fullNewSet
                ? s3g::kAcapellaResonatorMaxVoices - 1u : 0u;
            gesture.voiceCount = replaced && !fullNewSet
                ? 1u : s3g::kAcapellaResonatorMaxVoices;
            for (uint32_t voice = firstVoice;
                 voice < s3g::kAcapellaResonatorMaxVoices; ++voice) {
                const uint32_t destination = replaced && !fullNewSet
                    ? 0u : voice;
                gesture.voiceFrequencyHz[destination] = frequencies[voice];
                gesture.voiceGain[destination] = replaced && !fullNewSet
                    ? 1.0f : 0.35355339f;
                gesture.voiceInstanceIds[destination] = (replaced
                    ? 200u : 100u) + voice;
            }
            // Keep the dominant fallback metadata identical in the full-set
            // and one-voice renders. Their A/B difference can therefore only
            // come from honoring the fixed carrier array.
            bank.setGesture(gesture);
            const float time = static_cast<float>(frame)
                / static_cast<float>(kSampleRate);
            const float analysis = 0.18f * std::sin(
                2.0f * s3g::kPi * 173.0f * time)
                + 0.11f * std::sin(
                    2.0f * s3g::kPi * 547.0f * time);
            const auto output = bank.processFrameStereo(
                analysis, analysis, 0.0f, 0.0f, false);
            signal[frame] = output.left;
            finite = std::isfinite(output.left)
                && std::isfinite(output.right);
        }
        return std::pair<std::vector<float>, bool> {
            std::move(signal), finite };
    };

    const auto full = renderReplacement(true);
    const auto single = renderReplacement(false);
    const double referenceRms = rms(single.first,
        measurementBegin, frames);
    const double relativeDelta = differenceRms(full.first, single.first,
        measurementBegin, frames) / std::max(1.0e-6, referenceRms);
    const double fullRms = rms(full.first, measurementBegin, frames);
    uint32_t audibleNewTones = 0u;
    double weakestAudibleRatio = 1.0;
    std::array<double, s3g::kAcapellaResonatorMaxVoices> newToneRatios {};
    std::array<double, s3g::kAcapellaResonatorMaxVoices> oldToneRatios {};
    for (uint32_t voice = 0u;
         voice < s3g::kAcapellaResonatorMaxVoices; ++voice) {
        newToneRatios[voice] = toneAmplitude(full.first,
            newFrequencies[voice], measurementBegin, frames)
            / std::max(1.0e-6, fullRms);
        oldToneRatios[voice] = toneAmplitude(full.first,
            oldFrequencies[voice], measurementBegin, frames)
            / std::max(1.0e-6, fullRms);
        if (voice + 1u < s3g::kAcapellaResonatorMaxVoices
            && newToneRatios[voice] >= 0.020) {
            ++audibleNewTones;
            weakestAudibleRatio = std::min(weakestAudibleRatio,
                newToneRatios[voice]);
        }
    }
    const double oldResidual = *std::max_element(
        oldToneRatios.begin(), oldToneRatios.end());
    const double representativeNew = std::min({
        newToneRatios[0], newToneRatios[3], newToneRatios[6] });
    if (!full.second || !single.second || fullRms < 0.002
        || relativeDelta < 0.12 || audibleNewTones < 6u
        || representativeNew < 0.020
        || oldResidual > std::max(0.018, representativeNew * 0.70)) {
        std::cerr << "full eight-voice carrier replacement collapsed: RMS "
                  << fullRms << ", poly/single relative delta "
                  << relativeDelta << ", audible non-dominant new tones "
                  << audibleNewTones << ", weakest counted ratio "
                  << (audibleNewTones > 0u ? weakestAudibleRatio : 0.0)
                  << ", representative new ratio " << representativeNew
                  << ", maximum old residual " << oldResidual
                  << ", new ratios";
        for (const double ratio : newToneRatios) std::cerr << ' ' << ratio;
        std::cerr << ", old ratios";
        for (const double ratio : oldToneRatios) std::cerr << ' ' << ratio;
        std::cerr << '\n';
        return false;
    }
    return true;
}

bool freezeBoundaryProbe()
{
    constexpr uint32_t firstChangeFrame = 12000u;
    constexpr uint32_t restFrame = 24000u;
    constexpr uint32_t frames = 48000u;
    constexpr float lowFrequency = 500.0f;
    constexpr float highFrequency = 3600.0f;

    const auto renderFreeze = [&](s3g::AcapellaResonatorFreezeTrigger trigger,
                                  float freeze) {
        s3g::AcapellaResonatorParams params;
        params.amount = 1.0f;
        params.mode = s3g::AcapellaResonatorMode::Vocoder;
        params.carrierShape = s3g::AcapellaResonatorCarrierShape::Glottal;
        params.carrierHarmonics = 0.0f;
        params.carrierNoise = 0.0f;
        // Isolate freeze capture from the v4 voiced/unvoiced switch. The
        // source spectrum deliberately changes from vowel to fricative here;
        // Detect would also replace the carrier and invalidate this A/B.
        params.voicingMode = s3g::AcapellaResonatorVoicingMode::Tonal;
        params.analysisBlend = 0.0f;
        params.attackMs = 3.0f;
        params.releaseMs = 160.0f;
        params.resonance = 0.55f;
        params.driveDb = 0.0f;
        params.sibilance = 0.0f;
        params.freeze = freeze;
        params.freezeTrigger = trigger;
        params.blurMs = 0.0f;
        s3g::AcapellaResonatorBank bank;
        bank.setParams(params);
        std::vector<float> signal(frames, 0.0f);
        bool finite = bank.prepare(kSampleRate);
        for (uint32_t frame = 0u; frame < frames && finite; ++frame) {
            const bool active = frame < restFrame;
            const bool highSource = frame >= firstChangeFrame;
            s3g::AcapellaResonatorGesture gesture;
            gesture.active = active;
            gesture.phoneme = !active ? s3g::AcapellaPhoneme::Silence
                : highSource ? s3g::AcapellaPhoneme::SH
                             : s3g::AcapellaPhoneme::AA;
            gesture.frequencyHz = 173.0f;
            gesture.voiceInstance = 301u;
            gesture.stepIndex = !active ? 2u : highSource ? 1u : 0u;
            gesture.stepProgress = active ? 0.5f : 0.0f;
            gesture.flags = !active ? s3g::kAcapellaForcedRest
                : !highSource ? s3g::kAcapellaWordStart : 0u;
            if (active) {
                gesture.voiceCount = 1u;
                gesture.voiceFrequencyHz[0] = 173.0f;
                gesture.voiceGain[0] = 1.0f;
                gesture.voiceInstanceIds[0] = 301u;
            }
            // Deliberately update every sample with persistent WordStart and
            // ForcedRest flags. An edge trigger must not recapture merely
            // because a score flag remains high throughout its step.
            bank.setGesture(gesture);
            const float time = static_cast<float>(frame)
                / static_cast<float>(kSampleRate);
            const float sourceFrequency = highSource
                ? highFrequency : lowFrequency;
            const float analysis = active ? 0.24f * std::sin(
                2.0f * s3g::kPi * sourceFrequency * time) : 0.0f;
            const float carrier = 0.14f * (std::sin(
                2.0f * s3g::kPi * lowFrequency * time)
                + std::sin(2.0f * s3g::kPi * highFrequency * time));
            const auto output = bank.processFrameStereo(
                analysis, analysis, carrier, carrier);
            signal[frame] = output.left;
            finite = std::isfinite(output.left)
                && std::isfinite(output.right);
        }
        return std::pair<std::vector<float>, bool> {
            std::move(signal), finite };
    };

    const auto continuous = renderFreeze(
        s3g::AcapellaResonatorFreezeTrigger::Continuous, 1.0f);
    const auto rest = renderFreeze(
        s3g::AcapellaResonatorFreezeTrigger::Rest, 1.0f);
    const auto unfrozen = renderFreeze(
        s3g::AcapellaResonatorFreezeTrigger::Rest, 0.0f);
    const auto toneRatio = [&](const std::vector<float>& signal,
                               uint32_t begin, uint32_t end) {
        const double low = toneAmplitude(signal, lowFrequency, begin, end);
        const double high = toneAmplitude(signal, highFrequency, begin, end);
        return high / std::max(1.0e-8, low);
    };
    const double continuousInitialRatio = toneRatio(
        continuous.first, 6000u, 11000u);
    const double continuousChangedRatio = toneRatio(
        continuous.first, 17000u, 23000u);
    const double unfrozenChangedRatio = toneRatio(
        unfrozen.first, 17000u, 23000u);
    const double continuousRatioDriftDb = 20.0 * std::log10(
        std::max(1.0e-8, continuousChangedRatio)
        / std::max(1.0e-8, continuousInitialRatio));
    const double continuousRestRatio = toneRatio(
        continuous.first, 26000u, 33000u);
    const double restCaptureRatio = toneRatio(
        rest.first, 26000u, 33000u);
    const double restRelativeDelta = differenceRms(rest.first,
        unfrozen.first, 28000u, 40000u)
        / std::max(1.0e-6, rms(unfrozen.first, 28000u, 40000u));
    const double restPreEdgeDelta = differenceRms(rest.first,
        unfrozen.first, 2048u, restFrame - 256u)
        / std::max(1.0e-6, rms(unfrozen.first, 2048u, restFrame - 256u));
    const double restEarlyRms = rms(rest.first, 24500u, 30000u);
    const double restLateRms = rms(rest.first, 43000u, 48000u);
    const double continuousEarlyRms = rms(
        continuous.first, 24500u, 30000u);
    const double continuousLateRms = rms(
        continuous.first, 43000u, 48000u);
    if (!continuous.second || !rest.second || !unfrozen.second
        || std::abs(continuousRatioDriftDb) > 4.0
        || unfrozenChangedRatio < continuousChangedRatio * 1.8
        || restCaptureRatio < continuousRestRatio * 1.8
        || restPreEdgeDelta > 0.01
        || restRelativeDelta < 0.025
        || restEarlyRms < 0.001
        || restLateRms > restEarlyRms * 0.65
        || continuousEarlyRms < 0.001
        || continuousLateRms > continuousEarlyRms * 0.65) {
        std::cerr << "freeze boundary semantics failed: Continuous drift "
                  << continuousRatioDriftDb << " dB, changed ratios frozen/"
                  << "live " << continuousChangedRatio << '/'
                  << unfrozenChangedRatio << ", rest/continuous capture "
                  << restCaptureRatio << '/' << continuousRestRatio
                  << ", Rest pre/post relative delta " << restPreEdgeDelta
                  << '/' << restRelativeDelta
                  << ", Rest early/late RMS " << restEarlyRms << '/'
                  << restLateRms << ", Continuous early/late RMS "
                  << continuousEarlyRms << '/' << continuousLateRms << '\n';
        return false;
    }
    return true;
}

bool repeatedMidFreezeRecaptureProbe()
{
    constexpr uint32_t frames = 36000u;
    constexpr std::array<uint32_t, 3u> eventFrames {{ 0u, 12000u, 24000u }};
    constexpr std::array<s3g::AcapellaPhoneme, 3u> phonemes {{
        s3g::AcapellaPhoneme::AA,
        s3g::AcapellaPhoneme::IY,
        s3g::AcapellaPhoneme::SH,
    }};
    const auto render = [&](s3g::AcapellaResonatorFreezeTrigger trigger,
                            uint32_t suppressedCapture) {
        s3g::AcapellaResonatorParams params;
        params.amount = 1.0f;
        params.mode = s3g::AcapellaResonatorMode::Hybrid;
        params.carrierShape = s3g::AcapellaResonatorCarrierShape::Saw;
        params.carrierHarmonics = 0.82f;
        params.carrierNoise = 0.0f;
        params.analysisBlend = 1.0f;
        params.attackMs = 8.0f;
        params.releaseMs = 180.0f;
        params.resonance = 0.62f;
        params.driveDb = 0.0f;
        params.sibilance = 0.25f;
        params.freeze = 0.5f;
        params.freezeTrigger = trigger;
        params.blurMs = 0.0f;
        params.gestureFollow = 1.0f;
        s3g::AcapellaResonatorBank bank;
        bank.setParams(params);
        bool finite = bank.prepare(kSampleRate);
        std::vector<float> signal(frames, 0.0f);
        uint32_t step = 0u;
        for (uint32_t frame = 0u; frame < frames && finite; ++frame) {
            if (step + 1u < eventFrames.size()
                && frame >= eventFrames[step + 1u]) {
                ++step;
            }
            if (suppressedCapture == step && frame == eventFrames[step]) {
                // Preserve all earlier captures and suppress only this one.
                // No step carries WordStart, so changing the reference to
                // Word cannot itself arm another capture.
                params.freezeTrigger =
                    s3g::AcapellaResonatorFreezeTrigger::Word;
                bank.setParams(params);
            }
            s3g::AcapellaResonatorGesture gesture;
            gesture.active = true;
            gesture.phoneme = phonemes[step];
            gesture.frequencyHz = 137.0f;
            gesture.voiceInstance = 801u;
            gesture.voiceCount = 1u;
            gesture.voiceFrequencyHz[0] = 137.0f;
            gesture.voiceGain[0] = 1.0f;
            gesture.voiceInstanceIds[0] = 801u;
            gesture.stepIndex = step;
            gesture.stepProgress = static_cast<float>(
                frame - eventFrames[step]) / 12000.0f;
            gesture.flags = s3g::kAcapellaSyllableStart;
            if (step == 0u) gesture.flags |= s3g::kAcapellaWordStart;
            bank.setGesture(gesture);
            const float time = static_cast<float>(frame)
                / static_cast<float>(kSampleRate);
            const float analysis = 0.19f * std::sin(
                2.0f * s3g::kPi * 137.0f * time)
                + 0.08f * std::sin(
                    2.0f * s3g::kPi * 548.0f * time);
            const float carrier = carrierSignal(frame);
            const auto output = bank.processFrameStereo(
                analysis, analysis, carrier, carrier);
            signal[frame] = output.left;
            finite = std::isfinite(output.left)
                && std::isfinite(output.right);
        }
        return std::pair<std::vector<float>, bool> {
            std::move(signal), finite };
    };

    for (const auto trigger : {
            s3g::AcapellaResonatorFreezeTrigger::Phoneme,
            s3g::AcapellaResonatorFreezeTrigger::Syllable }) {
        const auto captured = render(trigger, 0xffffffffu);
        for (uint32_t captureIndex : { 1u, 2u }) {
            const auto reference = render(trigger, captureIndex);
            const uint32_t event = eventFrames[captureIndex];
            const uint32_t capture = event
                + std::max<uint32_t>(16u,
                    static_cast<uint32_t>(kSampleRate * 0.006f)) - 1u;
            const double localRms = rms(reference.first,
                capture - 256u, capture + 512u);
            const double preDelta = differenceRms(captured.first,
                reference.first, event, capture)
                / std::max(1.0e-6, localRms);
            const double boundaryDelta = differenceRms(captured.first,
                reference.first, capture, capture + 32u)
                / std::max(1.0e-6, localRms);
            const double laterDelta = differenceRms(captured.first,
                reference.first, capture + 768u, capture + 4096u)
                / std::max(1.0e-6, rms(reference.first,
                    capture + 768u, capture + 4096u));
            if (!captured.second || !reference.second || localRms < 0.002
                || preDelta > 1.0e-5 || boundaryDelta > 0.12
                || laterDelta < 0.015) {
                std::cerr << "mid-Freeze recapture was discontinuous or "
                          << "inaudible for trigger/index "
                          << static_cast<uint32_t>(trigger) << '/'
                          << captureIndex << ": local RMS " << localRms
                          << ", pre/boundary/later relative delta "
                          << preDelta << '/' << boundaryDelta << '/'
                          << laterDelta << '\n';
                return false;
            }
        }
    }
    return true;
}

bool activeTailContractProbe()
{
    bool okay = true;
    for (const float amount : { 0.0f, 1.0f }) {
        s3g::AcapellaResonatorParams params;
        params.amount = amount;
        params.mode = s3g::AcapellaResonatorMode::Hybrid;
        params.carrierShape = s3g::AcapellaResonatorCarrierShape::Saw;
        params.releaseMs = 120.0f;
        params.resonance = 0.48f;
        params.blurMs = 70.0f;
        params.freeze = 0.0f;
        s3g::AcapellaResonatorBank bank;
        bank.setParams(params);
        if (!bank.prepare(kSampleRate)) {
            std::cerr << "active/tail probe could not prepare bank\n";
            okay = false;
            continue;
        }
        s3g::AcapellaResonatorGesture gesture;
        gesture.active = true;
        gesture.phoneme = s3g::AcapellaPhoneme::AA;
        gesture.frequencyHz = 146.83f;
        gesture.voiceInstance = 401u;
        gesture.voiceCount = 1u;
        gesture.voiceFrequencyHz[0] = 146.83f;
        gesture.voiceGain[0] = 1.0f;
        gesture.voiceInstanceIds[0] = 401u;
        for (uint32_t frame = 0u; frame < 12000u; ++frame) {
            bank.setGesture(gesture);
            const float time = static_cast<float>(frame)
                / static_cast<float>(kSampleRate);
            const float analysis = 0.2f * std::sin(
                2.0f * s3g::kPi * 293.66f * time);
            const float carrier = 0.14f * std::sin(
                2.0f * s3g::kPi * 146.83f * time);
            bank.processFrameStereo(analysis, analysis, carrier, carrier);
        }
        const bool activeWhileWet = bank.active();
        const uint32_t declaredTail = bank.tailSamples();
        gesture.active = false;
        gesture.phoneme = s3g::AcapellaPhoneme::Silence;
        gesture.flags = s3g::kAcapellaForcedRest;
        gesture.voiceCount = 0u;
        gesture.stepIndex = 1u;
        bank.setGesture(gesture);
        const auto boundary = bank.processFrameStereo(
            0.0f, 0.0f, 0.0f, 0.0f);

        uint32_t firstInactive = bank.active() ? 0xffffffffu : 0u;
        const uint32_t observationLimit = declaredTail + kSampleRate * 4u;
        float inactiveAudiblePeak = !bank.active()
            ? std::max(std::abs(boundary.left), std::abs(boundary.right))
            : 0.0f;
        float declaredBoundaryPeak = declaredTail == 0u
            ? std::max(std::abs(boundary.left), std::abs(boundary.right))
            : 0.0f;
        bool finite = std::isfinite(boundary.left)
            && std::isfinite(boundary.right);
        for (uint32_t elapsed = 1u; elapsed <= observationLimit; ++elapsed) {
            bank.setGesture(gesture);
            const auto output = bank.processFrameStereo(
                0.0f, 0.0f, 0.0f, 0.0f);
            const float peak = std::max(
                std::abs(output.left), std::abs(output.right));
            finite = finite && std::isfinite(output.left)
                && std::isfinite(output.right);
            if (!bank.active() && firstInactive == 0xffffffffu) {
                firstInactive = elapsed;
            }
            if (!bank.active()) inactiveAudiblePeak = std::max(
                inactiveAudiblePeak, peak);
            const uint32_t boundaryDistance = elapsed > declaredTail
                ? elapsed - declaredTail : declaredTail - elapsed;
            if (boundaryDistance <= 256u) {
                declaredBoundaryPeak = std::max(declaredBoundaryPeak, peak);
            }
        }
        const bool wetContract = activeWhileWet
            && declaredTail > 0u && firstInactive != 0xffffffffu
            && firstInactive <= declaredTail;
        const bool activityCoversAudibleTail = inactiveAudiblePeak < 1.0e-4f;
        if (!finite || !wetContract
            || !activityCoversAudibleTail) {
            std::cerr << "active()/tailSamples disagreement at Amount "
                      << amount << ": declared " << declaredTail
                      << " samples, observed inactive "
                      << (firstInactive == 0xffffffffu
                            ? -1 : static_cast<int64_t>(firstInactive))
                      << ", active while wet " << activeWhileWet
                      << ", peak after inactive " << inactiveAudiblePeak
                      << ", peak around declared boundary "
                      << declaredBoundaryPeak << '\n';
            okay = false;
        }
    }
    return okay;
}

bool ordinaryRateHighBandProbe()
{
    const auto renderTone = [](float frequency) {
        constexpr uint32_t frames = 48000u;
        s3g::AcapellaResonatorParams params;
        params.amount = 1.0f;
        params.mode = s3g::AcapellaResonatorMode::Vocoder;
        // A broadband, MIDI-gated noise carrier makes the synthesis-band
        // transfer observable without adding a direct articulation rail.
        params.carrierShape = s3g::AcapellaResonatorCarrierShape::Noise;
        params.voicingMode = s3g::AcapellaResonatorVoicingMode::Noise;
        params.carrierHarmonics = 0.0f;
        params.carrierNoise = 1.0f;
        params.analysisBlend = 0.0f;
        params.attackMs = 2.0f;
        params.releaseMs = 80.0f;
        params.resonance = 0.55f;
        params.driveDb = 0.0f;
        // Hold the score rail constant and disable its sibilance injection:
        // this probe measures the real analysis/synthesis band transfer.
        params.sibilance = 0.0f;
        params.articulationThru = 0.0f;
        params.blurMs = 0.0f;
        s3g::AcapellaResonatorBank bank;
        bank.setParams(params);
        FormantMatrixRender result;
        result.left.resize(frames, 0.0f);
        bool finite = bank.prepare(kSampleRate);
        s3g::AcapellaResonatorGesture gesture;
        gesture.active = true;
        gesture.phoneme = s3g::AcapellaPhoneme::AA;
        gesture.frequencyHz = 137.0f;
        gesture.voiceInstance = 501u;
        gesture.voiceCount = 1u;
        gesture.voiceFrequencyHz[0] = 137.0f;
        gesture.voiceGain[0] = 1.0f;
        gesture.voiceInstanceIds[0] = 501u;
        for (uint32_t frame = 0u; frame < frames && finite; ++frame) {
            bank.setGesture(gesture);
            const float time = static_cast<float>(frame)
                / static_cast<float>(kSampleRate);
            const float analysis = 0.24f * std::sin(
                2.0f * s3g::kPi * frequency * time);
            const auto output = bank.processFrameStereo(
                analysis, analysis, 0.0f, 0.0f);
            result.left[frame] = output.left;
            finite = std::isfinite(output.left)
                && std::isfinite(output.right);
        }
        result.meters = bank.meterSnapshot();
        result.finite = finite;
        return result;
    };

    // Probe actual Speech22 channel centers, avoiding an apparent
    // cross-render failure caused by asking the independent measurement
    // filters to separate off-center tones from deliberately broad adjacent
    // vocoder channels.
    constexpr float lowFrequency = 1760.0f;
    constexpr float highFrequency = 5920.0f;
    const auto low = renderTone(lowFrequency);
    const auto high = renderTone(highFrequency);
    const double lowRms = rms(low.left, 12000u, 48000u);
    const double highRms = rms(high.left, 12000u, 48000u);
    const double lowBandRms = bandpassRms(
        low.left, lowFrequency, 3.0f, 12000u, 48000u);
    const double highBandRms = bandpassRms(
        high.left, highFrequency, 3.0f, 12000u, 48000u);
    const double relativeBandLevel = highBandRms
        / std::max(1.0e-6, lowBandRms);
    constexpr uint32_t lowBand = 13u; // 1760 Hz
    constexpr uint32_t highBand = 20u; // 5920 Hz
    const double highSelectivity = high.meters.analysis[highBand]
        / std::max(1.0e-6f, low.meters.analysis[highBand]);
    const double lowSelectivity = low.meters.analysis[lowBand]
        / std::max(1.0e-6f, high.meters.analysis[lowBand]);
    if (!low.finite || !high.finite || highBandRms < 0.0015
        || relativeBandLevel < 0.125
        || highSelectivity < 1.6 || lowSelectivity < 1.6) {
        std::cerr << "ordinary-rate measured high band was crushed: low/high "
                  << "overall RMS " << lowRms << '/' << highRms
                  << ", selected-band RMS " << lowBandRms << '/'
                  << highBandRms << ", high/low band ratio "
                  << relativeBandLevel << ", high/low selectivity "
                  << highSelectivity << '/' << lowSelectivity << '\n';
        return false;
    }
    return true;
}

bool focusedContractProbe()
{
    bool okay = true;
    if (!fullCarrierReplacementProbe()) okay = false;
    if (!freezeBoundaryProbe()) okay = false;
    if (!repeatedMidFreezeRecaptureProbe()) okay = false;
    if (!activeTailContractProbe()) okay = false;
    if (!ordinaryRateHighBandProbe()) okay = false;
    return okay;
}

struct VowelSuspensionMetrics {
    double activeRms = 0.0;
    double unfrozenActiveRms = 0.0;
    double tailRms = 0.0;
    double unfrozenTailRms = 0.0;
    double tailDelta = 0.0;
    double lateRms = 0.0;
};

VowelSuspensionMetrics gVowelSuspensionMetrics;

struct AutomationBoundaryMetrics {
    uint32_t sampleRate = 0u;
    double localRms = 0.0;
    float maximumStep = 0.0f;
    float stepLimit = 0.0f;
};

std::array<AutomationBoundaryMetrics, 2u> gAutomationBoundaryMetrics {};

bool vowelSuspensionProfileProbe()
{
    const auto base = s3g::acapellaResonatorProfileBase(6u);
    const auto profile = s3g::acapellaResonatorProfileEffects(
        6u, s3g::acapellaVocalFxPreset(base));
    auto unfrozenParams = profile.resonator;
    unfrozenParams.freeze = 0.0f;
    const auto renderProfile = [&](s3g::AcapellaResonatorParams params) {
        constexpr uint32_t activeFrames = 36000u;
        constexpr uint32_t frames = 96000u;
        s3g::AcapellaResonatorBank bank;
        bank.setParams(params);
        std::vector<float> signal(frames, 0.0f);
        bool finite = bank.prepare(kSampleRate);
        for (uint32_t frame = 0u; frame < frames && finite; ++frame) {
            const bool active = frame < activeFrames;
            s3g::AcapellaResonatorGesture gesture;
            gesture.active = active;
            gesture.phoneme = active ? s3g::AcapellaPhoneme::AA
                                     : s3g::AcapellaPhoneme::Silence;
            gesture.frequencyHz = 137.0f;
            gesture.voiceInstance = 601u;
            gesture.stepIndex = active ? 0u : 1u;
            gesture.stepProgress = active ? static_cast<float>(frame)
                    / static_cast<float>(activeFrames) : 0.0f;
            gesture.flags = active
                ? static_cast<uint8_t>(s3g::kAcapellaWordStart
                    | s3g::kAcapellaSyllableStart)
                : s3g::kAcapellaForcedRest;
            if (active) {
                gesture.voiceCount = 1u;
                gesture.voiceFrequencyHz[0] = 137.0f;
                gesture.voiceGain[0] = 1.0f;
                gesture.voiceInstanceIds[0] = 601u;
            }
            bank.setGesture(gesture);
            const float time = static_cast<float>(frame)
                / static_cast<float>(kSampleRate);
            const float analysis = active
                ? 0.18f * std::sin(2.0f * s3g::kPi * 137.0f * time)
                    + 0.09f * std::sin(
                        2.0f * s3g::kPi * 274.0f * time)
                    + 0.05f * std::sin(
                        2.0f * s3g::kPi * 822.0f * time)
                : 0.0f;
            const float carrier = active ? carrierSignal(frame) : 0.0f;
            const auto output = bank.processFrameStereo(
                analysis, analysis, carrier, carrier);
            signal[frame] = output.left;
            finite = std::isfinite(output.left)
                && std::isfinite(output.right);
        }
        return std::pair<std::vector<float>, bool> {
            std::move(signal), finite };
    };

    const auto frozen = renderProfile(profile.resonator);
    const auto unfrozen = renderProfile(unfrozenParams);
    const double activeRms = rms(frozen.first, 4096u, 34000u);
    const double unfrozenActiveRms = rms(unfrozen.first, 4096u, 34000u);
    const double tailRms = rms(frozen.first, 37000u, 65000u);
    const double unfrozenTailRms = rms(unfrozen.first, 37000u, 65000u);
    const double tailDelta = differenceRms(frozen.first,
        unfrozen.first, 37000u, 65000u)
        / std::max(1.0e-6, unfrozenTailRms);
    const double lateRms = rms(frozen.first, 84000u, 96000u);
    gVowelSuspensionMetrics = { activeRms, unfrozenActiveRms,
        tailRms, unfrozenTailRms, tailDelta, lateRms };
    if (!frozen.second || !unfrozen.second
        || activeRms < 0.006
        || activeRms < unfrozenActiveRms * 0.35
        || tailRms < 0.001
        || tailRms < unfrozenTailRms * 1.12
        || tailDelta < 0.12
        || lateRms > tailRms * 0.75) {
        std::cerr << "Vowel Suspension lost audibility/freeze tail: active "
                  << "frozen/unfrozen RMS " << activeRms << '/'
                  << unfrozenActiveRms << ", tail frozen/unfrozen RMS "
                  << tailRms << '/' << unfrozenTailRms
                  << ", relative tail delta " << tailDelta
                  << ", late RMS " << lateRms << '\n';
        return false;
    }
    return true;
}

bool extremeAutomationBoundaryProbe()
{
    constexpr std::array<uint32_t, 2u> sampleRates {{ 8000u, 48000u }};
    bool okay = true;
    uint32_t rateIndex = 0u;
    for (const uint32_t sampleRate : sampleRates) {
        s3g::AcapellaResonatorParams params;
        params.amount = 1.0f;
        params.mode = s3g::AcapellaResonatorMode::Resonator;
        params.carrierShape = s3g::AcapellaResonatorCarrierShape::Fold;
        params.carrierHarmonics = 0.88f;
        params.carrierNoise = 0.18f;
        params.analysisBlend = 0.78f;
        params.attackMs = 2.0f;
        params.releaseMs = 180.0f;
        params.driveDb = 10.0f;
        params.resonance = 0.05f;
        params.bandShiftSemitones = -24.0f;
        params.bandStretch = -1.0f;
        params.matrixMode = s3g::AcapellaResonatorMatrixMode::Chord;
        params.matrixMorph = 0.72f;
        s3g::AcapellaResonatorBank bank;
        bank.setParams(params);
        const uint32_t boundary = sampleRate / 2u;
        const uint32_t frames = sampleRate * 2u;
        std::vector<float> signal(frames, 0.0f);
        bool finite = bank.prepare(sampleRate);
        for (uint32_t frame = 0u; frame < frames && finite; ++frame) {
            if (frame == boundary) {
                params.resonance = 1.0f;
                params.bandShiftSemitones = 24.0f;
                params.bandStretch = 1.0f;
                bank.setParams(params);
            }
            s3g::AcapellaResonatorGesture gesture;
            gesture.active = true;
            gesture.phoneme = s3g::AcapellaPhoneme::SH;
            gesture.frequencyHz = 131.0f;
            gesture.voiceInstance = 701u;
            gesture.voiceCount = 1u;
            gesture.voiceFrequencyHz[0] = 131.0f;
            gesture.voiceGain[0] = 1.0f;
            gesture.voiceInstanceIds[0] = 701u;
            bank.setGesture(gesture);
            const float time = static_cast<float>(frame)
                / static_cast<float>(sampleRate);
            const float analysis = 0.17f * std::sin(
                2.0f * s3g::kPi * 131.0f * time)
                + 0.07f * std::sin(2.0f * s3g::kPi
                    * std::min(2800.0f, sampleRate * 0.34f) * time);
            const auto output = bank.processFrameStereo(
                analysis, analysis, 0.0f, 0.0f);
            signal[frame] = output.left;
            finite = std::isfinite(output.left)
                && std::isfinite(output.right)
                && std::abs(output.left) <= 1.801f
                && std::abs(output.right) <= 1.801f;
        }
        const uint32_t preWindow = std::max<uint32_t>(32u,
            sampleRate / 200u);
        const uint32_t postWindow = std::max<uint32_t>(64u,
            sampleRate / 100u);
        const double localRms = rms(signal, boundary - preWindow,
            boundary + postWindow);
        float maximumStep = 0.0f;
        for (uint32_t frame = boundary - 4u;
             frame < boundary + postWindow; ++frame) {
            maximumStep = std::max(maximumStep,
                std::abs(signal[frame] - signal[frame - 1u]));
        }
        const float stepLimit = std::max(0.045f,
            static_cast<float>(localRms * 2.0));
        gAutomationBoundaryMetrics[rateIndex++] = {
            sampleRate, localRms, maximumStep, stepLimit };
        if (!finite || localRms < 0.002 || maximumStep > stepLimit) {
            std::cerr << "extreme filter automation clicked at "
                      << sampleRate << " Hz: local RMS " << localRms
                      << ", maximum step " << maximumStep
                      << ", limit " << stepLimit << '\n';
            okay = false;
        }
    }
    return okay;
}

bool formantBandLayoutProbe()
{
    static_assert(s3g::kAcapellaResonatorBands == 22u,
        "Formant Matrix must expose twenty-two routes");
    static_assert(s3g::kAcapellaResonatorWideBands == 16u,
        "legacy wide layout must retain sixteen active bands");
    constexpr std::array<float, 22u> expected {{
        185.0f, 220.0f, 262.0f, 311.0f, 370.0f, 440.0f,
        523.0f, 622.0f, 740.0f, 880.0f, 1047.0f, 1245.0f,
        1480.0f, 1760.0f, 2093.0f, 2489.0f, 2960.0f, 3520.0f,
        4186.0f, 4978.0f, 5920.0f, 7040.0f,
    }};

    s3g::AcapellaResonatorParams params;
    params.bandLayout = s3g::AcapellaResonatorBandLayout::Speech22;
    s3g::AcapellaResonatorBank bank;
    bank.setParams(params);
    if (!bank.prepare(kSampleRate) || bank.activeBandCount() != 22u
        || bank.meterSnapshot().activeBands != 22u) {
        std::cerr << "Speech22 did not activate twenty-two bands\n";
        return false;
    }
    for (uint32_t band = 0u; band < expected.size(); ++band) {
        if (std::abs(bank.bandFrequencyHz(band) - expected[band]) > 0.01f
            || std::abs(s3g::kAcapellaSpeechBandFrequencies[band]
                - expected[band]) > 0.01f) {
            std::cerr << "Speech22 frequency mismatch at band " << band
                      << ": " << bank.bandFrequencyHz(band) << '\n';
            return false;
        }
    }
    if (bank.bandFrequencyHz(22u) != 0.0f) {
        std::cerr << "out-of-range band frequency was not guarded\n";
        return false;
    }

    params.bandLayout = s3g::AcapellaResonatorBandLayout::Wide16;
    bank.setParams(params);
    for (uint32_t frame = 0u; frame < 32u; ++frame) {
        (void)bank.processFrameStereo(0.0f, 0.0f, 0.0f, 0.0f, false);
    }
    if (bank.activeBandCount() != 16u
        || bank.meterSnapshot().activeBands != 16u
        || std::abs(bank.bandFrequencyHz(0u) - 90.0f) > 0.01f
        || std::abs(bank.bandFrequencyHz(15u) - 9000.0f) > 0.1f) {
        std::cerr << "Wide16 layout/count/endpoints were incorrect: "
                  << bank.activeBandCount() << ", "
                  << bank.bandFrequencyHz(0u) << " / "
                  << bank.bandFrequencyHz(15u) << '\n';
        return false;
    }
    for (uint32_t band = 1u; band < 16u; ++band) {
        if (!(bank.bandFrequencyHz(band) > bank.bandFrequencyHz(band - 1u))) {
            std::cerr << "Wide16 centers were not strictly increasing\n";
            return false;
        }
    }
    const auto wideMeters = renderFormantMatrix(params, 36000u,
        true, true, FormantAnalysisSignal::Sibilant, 5920.0f,
        s3g::AcapellaPhoneme::S);
    float inactiveMeterMaximum = 0.0f;
    for (uint32_t band = 16u; band < 22u; ++band) {
        inactiveMeterMaximum = std::max(inactiveMeterMaximum,
            std::max(wideMeters.meters.analysis[band],
                wideMeters.meters.synthesis[band]));
    }
    if (!wideMeters.finite || wideMeters.meters.activeBands != 16u
        || inactiveMeterMaximum > 1.0e-6f) {
        std::cerr << "Wide16 inactive meter lanes leaked: "
                  << inactiveMeterMaximum << '\n';
        return false;
    }

    params.bandLayout = s3g::AcapellaResonatorBandLayout::Speech22;
    bank.setParams(params);
    for (uint32_t frame = 0u; frame < 32u; ++frame) {
        (void)bank.processFrameStereo(0.0f, 0.0f, 0.0f, 0.0f, false);
    }
    for (uint32_t band = 0u; band < expected.size(); ++band) {
        if (std::abs(bank.bandFrequencyHz(band) - expected[band]) > 0.01f) {
            std::cerr << "runtime Speech22 restoration failed at band "
                      << band << '\n';
            return false;
        }
    }
    return true;
}

bool formantParameterSanitizationProbe()
{
    s3g::AcapellaResonatorParams invalid;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    invalid.bandLayout = static_cast<s3g::AcapellaResonatorBandLayout>(99u);
    invalid.voicingMode = static_cast<s3g::AcapellaResonatorVoicingMode>(99u);
    invalid.stereoMode = static_cast<s3g::AcapellaResonatorStereoMode>(99u);
    invalid.carrierLfoShape = static_cast<
        s3g::AcapellaResonatorCarrierLfoShape>(99u);
    invalid.voicingThreshold = nan;
    invalid.voicedTransitionMs = -1000.0f;
    invalid.unvoicedTransitionMs = 1000.0f;
    invalid.externalCarrierMix = nan;
    invalid.externalCarrierGainDb = 1000.0f;
    invalid.pulseWidth = -4.0f;
    invalid.carrierLfoRateHz = nan;
    invalid.carrierLfoDepthSemitones = 1000.0f;
    invalid.carrierLfoPwmDepth = -1000.0f;
    invalid.carrierLfoSyncDivisionBeats = nan;
    invalid.openLevel = nan;
    invalid.articulationThru = 1000.0f;
    invalid.coupling = 99;
    invalid.bandTrims.fill(nan);
    invalid.customMatrixA.fill(nan);
    invalid.customMatrixB.fill(std::numeric_limits<float>::infinity());
    invalid.customMatrixMorph = nan;
    const auto safe = s3g::sanitizeAcapellaResonatorParams(invalid);
    if (safe.bandLayout != s3g::AcapellaResonatorBandLayout::Wide16
        || safe.voicingMode != s3g::AcapellaResonatorVoicingMode::Detect
        || safe.stereoMode != s3g::AcapellaResonatorStereoMode::OddEven
        || safe.carrierLfoShape
            != s3g::AcapellaResonatorCarrierLfoShape::Square
        || safe.voicedTransitionMs != 10.0f
        || safe.unvoicedTransitionMs != 250.0f
        || safe.externalCarrierGainDb != 24.0f
        || safe.pulseWidth != 0.05f
        || safe.carrierLfoDepthSemitones != 24.0f
        || safe.carrierLfoPwmDepth != 0.0f
        || safe.articulationThru != 1.0f || safe.coupling != 3) {
        std::cerr << "v4 scalar/enumerated parameter sanitization failed\n";
        return false;
    }
    for (uint32_t index = 0u; index < safe.bandTrims.size(); ++index) {
        if (!std::isfinite(safe.bandTrims[index])
            || safe.bandTrims[index] != 1.0f) {
            std::cerr << "v4 band trim sanitization failed\n";
            return false;
        }
    }
    for (const float route : safe.customMatrixA) {
        if (!std::isfinite(route) || route != 0.0f) {
            std::cerr << "v4 scene A sanitization failed\n";
            return false;
        }
    }
    for (const float route : safe.customMatrixB) {
        if (!std::isfinite(route) || route != 0.0f) {
            std::cerr << "v4 scene B sanitization failed\n";
            return false;
        }
    }
    const auto rendered = renderFormantMatrix(safe, 12000u,
        true, true, FormantAnalysisSignal::Sibilant, 5920.0f,
        s3g::AcapellaPhoneme::S);
    if (!rendered.finite || rendered.peak > 1.801f) {
        std::cerr << "sanitized v4 parameter block was not process-safe\n";
        return false;
    }
    return true;
}

bool formantVoicingProbe()
{
    s3g::AcapellaResonatorParams base;
    base.amount = 1.0f;
    base.mode = s3g::AcapellaResonatorMode::FilterBank;
    base.openLevel = 0.78f;
    base.gestureFollow = 0.0f;
    base.carrierShape = s3g::AcapellaResonatorCarrierShape::Pulse;
    base.carrierHarmonics = 1.0f;
    base.carrierNoise = 1.0f;
    base.externalCarrierMix = 0.0f;
    base.voicedTransitionMs = 10.0f;
    base.unvoicedTransitionMs = 10.0f;
    base.articulationThru = 0.0f;
    base.driveDb = 0.0f;

    constexpr std::array<s3g::AcapellaResonatorVoicingMode, 3u> modes {{
        s3g::AcapellaResonatorVoicingMode::Tonal,
        s3g::AcapellaResonatorVoicingMode::Noise,
        s3g::AcapellaResonatorVoicingMode::Blend,
    }};
    std::array<FormantMatrixRender, modes.size()> rendered;
    for (uint32_t index = 0u; index < modes.size(); ++index) {
        auto params = base;
        params.voicingMode = modes[index];
        rendered[index] = renderFormantMatrix(params, 36000u,
            false, false);
        if (!rendered[index].finite
            || rms(rendered[index].left, 4096u) < 0.001) {
            std::cerr << "V/UV mode was silent or non-finite: "
                      << index << '\n';
            return false;
        }
    }
    if (rendered[0].meters.unvoiced > 0.05f
        || rendered[1].meters.unvoiced < 0.95f
        || std::abs(rendered[2].meters.unvoiced - 0.5f) > 0.05f
        || differenceRms(rendered[0].left, rendered[1].left, 4096u)
            / std::max(1.0e-6, rms(rendered[0].left, 4096u)) < 0.20) {
        std::cerr << "Tonal/Noise/Blend V/UV modes collapsed: meter values "
                  << rendered[0].meters.unvoiced << ' '
                  << rendered[1].meters.unvoiced << ' '
                  << rendered[2].meters.unvoiced << '\n';
        return false;
    }

    auto detect = base;
    detect.voicingMode = s3g::AcapellaResonatorVoicingMode::Detect;
    detect.voicingThreshold = 0.48f;
    const auto voiced = renderFormantMatrix(detect, 36000u,
        false, false, FormantAnalysisSignal::Tone, 220.0f,
        s3g::AcapellaPhoneme::AA);
    const auto unvoiced = renderFormantMatrix(detect, 36000u,
        false, false, FormantAnalysisSignal::Sibilant, 5920.0f,
        s3g::AcapellaPhoneme::S);
    auto highThreshold = detect;
    highThreshold.voicingThreshold = 1.0f;
    const auto thresholded = renderFormantMatrix(highThreshold, 36000u,
        false, false, FormantAnalysisSignal::Sibilant, 5920.0f,
        s3g::AcapellaPhoneme::S);
    if (!voiced.finite || !unvoiced.finite || !thresholded.finite
        || voiced.meters.unvoiced > 0.10f
        || unvoiced.meters.unvoiced < 0.90f
        || thresholded.meters.unvoiced > 0.10f) {
        std::cerr << "Detect threshold did not classify V/UV material: "
                  << voiced.meters.unvoiced << ' '
                  << unvoiced.meters.unvoiced << ' '
                  << thresholded.meters.unvoiced << '\n';
        return false;
    }

    const auto transitionValue = [&](bool toNoise, float milliseconds) {
        auto params = base;
        params.voicingMode = toNoise
            ? s3g::AcapellaResonatorVoicingMode::Tonal
            : s3g::AcapellaResonatorVoicingMode::Noise;
        params.voicedTransitionMs = milliseconds;
        params.unvoicedTransitionMs = milliseconds;
        s3g::AcapellaResonatorBank bank;
        bank.setParams(params);
        bank.prepare(kSampleRate);
        auto gesture = gestureFor(0u, false, s3g::AcapellaPhoneme::AA);
        for (uint32_t frame = 0u; frame < 12000u; ++frame) {
            bank.setGesture(gesture);
            (void)bank.processFrameStereo(0.2f, 0.2f, 0.0f, 0.0f, false);
        }
        params.voicingMode = toNoise
            ? s3g::AcapellaResonatorVoicingMode::Noise
            : s3g::AcapellaResonatorVoicingMode::Tonal;
        bank.setParams(params);
        for (uint32_t frame = 0u; frame < 2400u; ++frame) {
            bank.setGesture(gesture);
            (void)bank.processFrameStereo(0.2f, 0.2f, 0.0f, 0.0f, false);
        }
        return bank.meterSnapshot().unvoiced;
    };
    const float fastToNoise = transitionValue(true, 10.0f);
    const float slowToNoise = transitionValue(true, 250.0f);
    const float fastToTonal = transitionValue(false, 10.0f);
    const float slowToTonal = transitionValue(false, 250.0f);
    if (fastToNoise < slowToNoise + 0.25f
        || fastToTonal > slowToTonal - 0.25f) {
        std::cerr << "directional V/UV transition times were ineffective: "
                  << fastToNoise << '/' << slowToNoise << " and "
                  << fastToTonal << '/' << slowToTonal << '\n';
        return false;
    }
    return true;
}

bool formantOpenAndCarrierInputProbe()
{
    s3g::AcapellaResonatorParams open;
    open.amount = 1.0f;
    open.mode = s3g::AcapellaResonatorMode::FilterBank;
    open.openLevel = 1.0f;
    open.gestureFollow = 0.0f;
    open.externalCarrierMix = 1.0f;
    open.articulationThru = 0.0f;
    open.sibilance = 0.0f;
    open.driveDb = 0.0f;
    const auto fixedOpen = renderFormantMatrix(open, 32000u,
        true, true, FormantAnalysisSignal::Silence, 0.0f,
        s3g::AcapellaPhoneme::Silence, false);
    open.openLevel = 0.0f;
    const auto fixedClosed = renderFormantMatrix(open, 32000u,
        true, true, FormantAnalysisSignal::Silence, 0.0f,
        s3g::AcapellaPhoneme::Silence, false);
    if (!fixedOpen.finite || !fixedClosed.finite
        || rms(fixedOpen.left, 4096u) < 0.003
        || rms(fixedClosed.left, 4096u)
            > rms(fixedOpen.left, 4096u) * 0.03) {
        std::cerr << "Open Level did not provide a fixed filter-bank rail: "
                  << rms(fixedOpen.left, 4096u) << " / "
                  << rms(fixedClosed.left, 4096u) << '\n';
        return false;
    }

    s3g::AcapellaResonatorParams carrier;
    carrier.amount = 1.0f;
    carrier.mode = s3g::AcapellaResonatorMode::Vocoder;
    carrier.analysisBlend = 0.0f;
    carrier.attackMs = 2.0f;
    carrier.releaseMs = 80.0f;
    carrier.externalCarrierMix = 1.0f;
    carrier.externalCarrierGainDb = 0.0f;
    carrier.carrierShape = s3g::AcapellaResonatorCarrierShape::Saw;
    carrier.carrierHarmonics = 1.0f;
    carrier.carrierNoise = 0.0f;
    carrier.voicingMode = s3g::AcapellaResonatorVoicingMode::Tonal;
    carrier.articulationThru = 0.0f;
    carrier.driveDb = 0.0f;
    const auto absent = renderFormantMatrix(carrier, 36000u,
        false, false);
    const auto explicitlySilent = renderFormantMatrix(carrier, 36000u,
        true, false);
    const auto external = renderFormantMatrix(carrier, 36000u,
        true, true);
    carrier.externalCarrierMix = 0.0f;
    const auto internal = renderFormantMatrix(carrier, 36000u,
        true, true);
    const double absentRms = rms(absent.left, 4096u);
    const double silentRms = rms(explicitlySilent.left, 4096u);
    const double externalRms = rms(external.left, 4096u);
    const double absentInternalDelta = differenceRms(
        absent.left, internal.left, 4096u)
        / std::max(1.0e-6, rms(internal.left, 4096u));
    if (!absent.finite || !explicitlySilent.finite || !external.finite
        || !internal.finite || absentRms < 0.002 || externalRms < 0.002
        || silentRms > absentRms * 0.025
        || absentInternalDelta > 1.0e-4) {
        std::cerr << "absent and explicitly silent carriers were conflated: "
                  << "RMS absent/silent/external " << absentRms << '/'
                  << silentRms << '/' << externalRms
                  << ", absent/internal delta "
                  << absentInternalDelta << '\n';
        return false;
    }
    return true;
}

bool formantRoutingTrimAndMeterProbe()
{
    s3g::AcapellaResonatorParams base;
    base.amount = 1.0f;
    base.mode = s3g::AcapellaResonatorMode::Vocoder;
    base.analysisBlend = 0.0f;
    base.attackMs = 1.0f;
    base.releaseMs = 50.0f;
    base.externalCarrierMix = 1.0f;
    base.matrixMode = s3g::AcapellaResonatorMatrixMode::Identity;
    base.matrixMorph = 1.0f;
    base.articulationThru = 0.0f;
    base.sibilance = 0.0f;
    base.stereoSpread = 0.0f;
    base.driveDb = 0.0f;
    const auto baseline = renderFormantMatrix(base, 36000u,
        true, true, FormantAnalysisSignal::Tone, 740.0f);
    float maximumAnalysisMeter = 0.0f;
    float maximumSynthesisMeter = 0.0f;
    float maximumAnalysisDisplay = 0.0f;
    float maximumSynthesisDisplay = 0.0f;
    for (uint32_t band = 0u; band < 22u; ++band) {
        const float analysis = baseline.meters.analysis[band];
        const float synthesis = baseline.meters.synthesis[band];
        if (!std::isfinite(analysis) || !std::isfinite(synthesis)
            || analysis < 0.0f || synthesis < 0.0f) {
            std::cerr << "band meter was non-finite or negative\n";
            return false;
        }
        maximumAnalysisMeter = std::max(maximumAnalysisMeter, analysis);
        maximumSynthesisMeter = std::max(maximumSynthesisMeter, synthesis);
        maximumAnalysisDisplay = std::max(maximumAnalysisDisplay,
            s3g::acapellaResonatorMeterDisplayLevel(analysis));
        maximumSynthesisDisplay = std::max(maximumSynthesisDisplay,
            s3g::acapellaResonatorMeterDisplayLevel(synthesis));
    }
    if (!baseline.finite || maximumAnalysisMeter < 1.0e-4f
        || maximumSynthesisMeter < 1.0e-5f
        || maximumAnalysisDisplay < 0.10f
        || maximumSynthesisDisplay < 0.10f) {
        std::cerr << "twenty-two band meters did not report activity: "
                  << maximumAnalysisMeter << '/' << maximumSynthesisMeter
                  << ", display " << maximumAnalysisDisplay << '/'
                  << maximumSynthesisDisplay
                  << '\n';
        return false;
    }
    if (s3g::acapellaResonatorMeterDisplayLevel(0.0f) != 0.0f
        || s3g::acapellaResonatorMeterDisplayLevel(1.0f) != 1.0f
        || s3g::acapellaResonatorMeterDisplayLevel(0.001f) < 0.15f
        || s3g::acapellaResonatorMeterDisplayLevel(
            std::numeric_limits<float>::quiet_NaN()) != 0.0f) {
        std::cerr << "band meter display calibration was incorrect\n";
        return false;
    }

    auto allTrimmed = base;
    allTrimmed.bandTrims.fill(0.0f);
    const auto trimmed = renderFormantMatrix(allTrimmed, 36000u,
        true, true, FormantAnalysisSignal::Tone, 740.0f);
    if (!trimmed.finite
        || rms(trimmed.left, 4096u) > rms(baseline.left, 4096u) * 0.04) {
        std::cerr << "per-band trims did not close every synthesis band: "
                  << rms(baseline.left, 4096u) << '/'
                  << rms(trimmed.left, 4096u) << '\n';
        return false;
    }

    s3g::AcapellaResonatorBank setterBank;
    setterBank.setParams(base);
    setterBank.setBandTrim(8u, std::numeric_limits<float>::infinity());
    setterBank.setBandTrim(22u, 0.0f);
    setterBank.setCustomMatrixCell(false, 0u, 0u, 9.0f);
    setterBank.setCustomMatrixCell(true, 0u, 0u, -9.0f);
    setterBank.setCustomMatrixCell(false, 22u, 0u, 1.0f);
    if (setterBank.params().bandTrims[8u] != 1.0f
        || setterBank.params().customMatrixA[0u] != 1.0f
        || setterBank.params().customMatrixB[0u] != -1.0f) {
        std::cerr << "RT trim/matrix setters failed bounds/sanitization\n";
        return false;
    }

    const auto routed = [&](int32_t coupling) {
        auto params = base;
        params.coupling = coupling;
        params.matrixMode = s3g::AcapellaResonatorMatrixMode::Identity;
        return renderFormantMatrix(params, 40000u, true, true,
            FormantAnalysisSignal::Tone, 185.0f);
    };
    const auto couplingZero = routed(0);
    const auto couplingPlus = routed(3);
    const auto couplingMinus = routed(-3);
    const auto dominantBand = [](const auto& values) {
        return static_cast<uint32_t>(std::distance(values.begin(),
            std::max_element(values.begin(), values.end())));
    };
    const uint32_t zeroBand = dominantBand(couplingZero.meters.synthesis);
    const uint32_t plusBand = dominantBand(couplingPlus.meters.synthesis);
    const uint32_t minusBand = dominantBand(couplingMinus.meters.synthesis);
    const float plusWrapped = couplingPlus.meters.synthesis[20u]
        + couplingPlus.meters.synthesis[21u];
    if (!couplingZero.finite || !couplingPlus.finite || !couplingMinus.finite
        || !(plusBand > zeroBand)
        || plusWrapped > couplingPlus.meters.synthesis[plusBand] * 0.8f
        || minusBand > zeroBand + 1u) {
        std::cerr << "non-wrapping coupling did not shift/clip as expected: "
                  << zeroBand << '/' << plusBand << '/' << minusBand
                  << ", wrapped energy " << plusWrapped << '\n';
        return false;
    }

    auto customA = base;
    customA.matrixMode = s3g::AcapellaResonatorMatrixMode::Custom;
    customA.matrixMorph = 1.0f;
    customA.customMatrixA.fill(0.0f);
    customA.customMatrixB.fill(0.0f);
    const uint32_t source = 8u;
    const uint32_t destination = 15u;
    customA.customMatrixA[destination * 22u + source] = 1.0f;
    customA.customMatrixB[destination * 22u + source] = -1.0f;
    customA.customMatrixMorph = 0.0f;
    const auto sceneA = renderFormantMatrix(customA, 42000u,
        true, true, FormantAnalysisSignal::Tone, 740.0f);
    customA.customMatrixMorph = 0.5f;
    const auto midpoint = renderFormantMatrix(customA, 42000u,
        true, true, FormantAnalysisSignal::Tone, 740.0f);
    customA.customMatrixMorph = 1.0f;
    const auto sceneB = renderFormantMatrix(customA, 42000u,
        true, true, FormantAnalysisSignal::Tone, 740.0f);
    const double aRms = rms(sceneA.left, 6000u);
    const double bRms = rms(sceneB.left, 6000u);
    const double midRms = rms(midpoint.left, 6000u);
    if (!sceneA.finite || !sceneB.finite || !midpoint.finite
        || aRms < 0.001 || bRms < 0.001
        || midRms > std::min(aRms, bRms) * 0.12
        || std::abs(aRms - bRms) > std::max(aRms, bRms) * 0.15) {
        std::cerr << "signed A/B matrix morph was ineffective: "
                  << aRms << '/' << midRms << '/' << bRms << '\n';
        return false;
    }

    auto normalized = customA;
    normalized.customMatrixMorph = 0.0f;
    normalized.customMatrixA.fill(0.0f);
    for (uint32_t src = 0u; src < 22u; ++src) {
        normalized.customMatrixA[destination * 22u + src] = 1.0f;
    }
    const auto dense = renderFormantMatrix(normalized, 42000u,
        true, true, FormantAnalysisSignal::Tone, 740.0f);
    auto single = normalized;
    single.customMatrixA.fill(0.0f);
    single.customMatrixA[destination * 22u + source] = 1.0f;
    const auto oneRoute = renderFormantMatrix(single, 42000u,
        true, true, FormantAnalysisSignal::Tone, 740.0f);
    if (!dense.finite || !oneRoute.finite
        || rms(dense.left, 6000u) > rms(oneRoute.left, 6000u) * 2.2) {
        std::cerr << "dense custom row lacked absolute-sum normalization: "
                  << rms(oneRoute.left, 6000u) << '/'
                  << rms(dense.left, 6000u) << '\n';
        return false;
    }
    return true;
}

bool formantStereoAndArticulationProbe()
{
    s3g::AcapellaResonatorParams base;
    base.amount = 1.0f;
    base.mode = s3g::AcapellaResonatorMode::FilterBank;
    base.openLevel = 0.75f;
    base.gestureFollow = 0.0f;
    base.externalCarrierMix = 1.0f;
    base.stereoSpread = 1.0f;
    base.articulationThru = 0.0f;
    base.driveDb = 0.0f;
    constexpr std::array<s3g::AcapellaResonatorStereoMode, 3u> modes {{
        s3g::AcapellaResonatorStereoMode::Mono,
        s3g::AcapellaResonatorStereoMode::Spread,
        s3g::AcapellaResonatorStereoMode::OddEven,
    }};
    std::array<FormantMatrixRender, 3u> renders;
    for (uint32_t index = 0u; index < modes.size(); ++index) {
        auto params = base;
        params.stereoMode = modes[index];
        renders[index] = renderFormantMatrix(params, 36000u,
            true, true, FormantAnalysisSignal::Tone, 740.0f,
            s3g::AcapellaPhoneme::AA, true, 120.0, true, false);
    }
    const double monoDifference = differenceRms(
        renders[0].left, renders[0].right, 4096u)
        / std::max(1.0e-6, rms(renders[0].left, 4096u));
    const double spreadDifference = differenceRms(
        renders[1].left, renders[1].right, 4096u)
        / std::max(1.0e-6, rms(renders[1].left, 4096u));
    const double oddEvenDifference = differenceRms(
        renders[2].left, renders[2].right, 4096u)
        / std::max(1.0e-6, rms(renders[2].left, 4096u));
    if (!renders[0].finite || !renders[1].finite || !renders[2].finite
        || monoDifference > 1.0e-5
        || spreadDifference < 0.05 || oddEvenDifference < 0.05
        || differenceRms(renders[1].left, renders[2].left, 4096u)
            / std::max(1.0e-6, rms(renders[1].left, 4096u)) < 0.025) {
        std::cerr << "Mono/Spread/OddEven stereo modes collapsed: "
                  << monoDifference << '/' << spreadDifference << '/'
                  << oddEvenDifference << '\n';
        return false;
    }

    auto dryArticulation = base;
    dryArticulation.openLevel = 0.0f;
    dryArticulation.mode = s3g::AcapellaResonatorMode::Vocoder;
    dryArticulation.externalCarrierMix = 0.0f;
    dryArticulation.voicingMode = s3g::AcapellaResonatorVoicingMode::Tonal;
    dryArticulation.sibilance = 0.0f;
    dryArticulation.articulationThru = 0.0f;
    const auto noThru = renderFormantMatrix(dryArticulation, 36000u,
        false, false, FormantAnalysisSignal::Sibilant, 5920.0f,
        s3g::AcapellaPhoneme::S);
    dryArticulation.articulationThru = 1.0f;
    const auto fullThru = renderFormantMatrix(dryArticulation, 36000u,
        false, false, FormantAnalysisSignal::Sibilant, 5920.0f,
        s3g::AcapellaPhoneme::S);
    const double highNoThru = bandpassRms(noThru.left,
        5920.0f, 2.0f, 4096u, 36000u);
    const double highFullThru = bandpassRms(fullThru.left,
        5920.0f, 2.0f, 4096u, 36000u);
    if (!noThru.finite || !fullThru.finite
        || highFullThru < highNoThru * 1.5 || highFullThru < 0.001) {
        std::cerr << "articulation-thru highpass was inaudible: "
                  << highNoThru << '/' << highFullThru << '\n';
        return false;
    }
    return true;
}

bool formantCarrierLfoAndAutomationProbe()
{
    s3g::AcapellaResonatorParams base;
    base.amount = 1.0f;
    base.mode = s3g::AcapellaResonatorMode::FilterBank;
    base.openLevel = 0.78f;
    base.gestureFollow = 0.0f;
    base.externalCarrierMix = 0.0f;
    base.voicingMode = s3g::AcapellaResonatorVoicingMode::Tonal;
    base.carrierShape = s3g::AcapellaResonatorCarrierShape::Pulse;
    base.carrierHarmonics = 1.0f;
    base.carrierNoise = 0.0f;
    base.pulseWidth = 0.50f;
    base.carrierLfoRateHz = 3.0f;
    base.carrierLfoDepthSemitones = 8.0f;
    base.carrierLfoPwmDepth = 0.75f;
    base.articulationThru = 0.0f;
    base.driveDb = 0.0f;
    base.carrierLfoSync = false;
    base.carrierLfoShape = s3g::AcapellaResonatorCarrierLfoShape::Triangle;
    const auto freeTriangle = renderFormantMatrix(base, 48000u,
        false, false);
    base.carrierLfoShape = s3g::AcapellaResonatorCarrierLfoShape::Square;
    const auto freeSquare = renderFormantMatrix(base, 48000u,
        false, false);
    auto noModulation = base;
    noModulation.carrierLfoDepthSemitones = 0.0f;
    noModulation.carrierLfoPwmDepth = 0.0f;
    const auto unmodulated = renderFormantMatrix(noModulation, 48000u,
        false, false);
    if (!freeTriangle.finite || !freeSquare.finite || !unmodulated.finite
        || differenceRms(freeTriangle.left, unmodulated.left, 4096u)
            / std::max(1.0e-6, rms(unmodulated.left, 4096u)) < 0.05
        || differenceRms(freeTriangle.left, freeSquare.left, 4096u)
            / std::max(1.0e-6, rms(freeTriangle.left, 4096u)) < 0.05
        || freeSquare.maximumStep > 0.65f) {
        std::cerr << "free LFO FM/PWM/shape axes were ineffective or clicked\n";
        return false;
    }

    auto synchronized = base;
    synchronized.carrierLfoSync = true;
    synchronized.carrierLfoSyncDivisionBeats = 1.0f;
    synchronized.carrierLfoShape =
        s3g::AcapellaResonatorCarrierLfoShape::Triangle;
    const auto sync120 = renderFormantMatrix(synchronized, 48000u,
        false, false, FormantAnalysisSignal::Tone, 740.0f,
        s3g::AcapellaPhoneme::AA, true, 120.0, true);
    const auto sync60 = renderFormantMatrix(synchronized, 48000u,
        false, false, FormantAnalysisSignal::Tone, 740.0f,
        s3g::AcapellaPhoneme::AA, true, 60.0, true);
    const auto invalidTempo = renderFormantMatrix(synchronized, 48000u,
        false, false, FormantAnalysisSignal::Tone, 740.0f,
        s3g::AcapellaPhoneme::AA, true, 120.0, false);
    if (!sync120.finite || !sync60.finite || !invalidTempo.finite
        || differenceRms(sync120.left, sync60.left, 4096u)
            / std::max(1.0e-6, rms(sync120.left, 4096u)) < 0.05
        || differenceRms(invalidTempo.left, freeTriangle.left, 4096u)
            / std::max(1.0e-6, rms(freeTriangle.left, 4096u)) > 0.01) {
        std::cerr << "tempo-synchronized LFO did not follow/fallback correctly\n";
        return false;
    }

    s3g::AcapellaResonatorBank bank;
    auto params = base;
    params.matrixMode = s3g::AcapellaResonatorMatrixMode::Custom;
    params.customMatrixA.fill(0.0f);
    params.customMatrixB.fill(0.0f);
    for (uint32_t band = 0u; band < 22u; ++band) {
        params.customMatrixA[band * 22u + band] = 1.0f;
        params.customMatrixB[band * 22u + (21u - band)] = -1.0f;
    }
    bank.setParams(params);
    if (!bank.prepare(kSampleRate)) return false;
    std::vector<float> signal(96000u, 0.0f);
    bool finite = true;
    float maximumBoundaryStep = 0.0f;
    float previous = 0.0f;
    for (uint32_t frame = 0u; frame < signal.size() && finite; ++frame) {
        if ((frame % 1024u) == 0u) {
            const uint32_t stage = frame / 1024u;
            params.bandLayout = (stage & 1u)
                ? s3g::AcapellaResonatorBandLayout::Wide16
                : s3g::AcapellaResonatorBandLayout::Speech22;
            params.voicingMode = static_cast<
                s3g::AcapellaResonatorVoicingMode>(stage % 4u);
            params.voicingThreshold = static_cast<float>(stage % 9u) / 8.0f;
            params.voicedTransitionMs = (stage & 2u) ? 250.0f : 10.0f;
            params.unvoicedTransitionMs = (stage & 4u) ? 250.0f : 10.0f;
            params.openLevel = (stage & 1u) ? 1.0f : 0.0f;
            params.articulationThru = (stage & 2u) ? 1.0f : 0.0f;
            params.coupling = static_cast<int32_t>(stage % 7u) - 3;
            params.customMatrixMorph = static_cast<float>(stage % 5u) * 0.25f;
            params.stereoMode = static_cast<s3g::AcapellaResonatorStereoMode>(
                stage % 3u);
            params.carrierLfoShape = static_cast<
                s3g::AcapellaResonatorCarrierLfoShape>(stage % 2u);
            params.carrierLfoRateHz = (stage & 1u) ? 13.0f : 0.02f;
            params.carrierLfoDepthSemitones = (stage & 2u) ? 24.0f : 0.0f;
            params.carrierLfoPwmDepth = (stage & 4u) ? 1.0f : 0.0f;
            params.carrierLfoSync = (stage & 8u) != 0u;
            params.carrierLfoSyncDivisionBeats = (stage & 16u)
                ? 16.0f : 0.0625f;
            params.externalCarrierMix = (stage & 1u) ? 1.0f : 0.0f;
            params.externalCarrierGainDb = (stage & 2u) ? 24.0f : -24.0f;
            for (uint32_t band = 0u; band < 22u; ++band) {
                params.bandTrims[band] = static_cast<float>(
                    (band + stage) % 3u);
            }
            bank.setParams(params);
            bank.setTempo((stage & 1u) ? 400.0 : 20.0, true);
        }
        s3g::AcapellaResonatorGesture gesture;
        gesture.active = true;
        gesture.phoneme = (frame / 2048u) & 1u
            ? s3g::AcapellaPhoneme::S : s3g::AcapellaPhoneme::AA;
        gesture.frequencyHz = 137.0f;
        gesture.voiceInstance = 0x5001u;
        gesture.voiceCount = 1u;
        gesture.voiceFrequencyHz[0] = 137.0f;
        gesture.voiceGain[0] = 1.0f;
        gesture.voiceInstanceIds[0] = 0x5001u;
        gesture.stepIndex = frame / 2048u;
        gesture.flags = (frame % 2048u) == 0u
            ? s3g::kAcapellaSyllableStart : 0u;
        bank.setGesture(gesture);
        const float time = static_cast<float>(frame)
            / static_cast<float>(kSampleRate);
        const float analysis = 0.18f * std::sin(
            2.0f * s3g::kPi * 740.0f * time)
            + 0.04f * deterministicNoise(frame, 0x8421u);
        const float external = 0.18f * deterministicNoise(frame, 0x9137u);
        const auto output = bank.processFrameStereo(
            analysis, analysis, external, -external, (frame & 4096u) != 0u);
        signal[frame] = output.left;
        const float step = std::abs(output.left - previous);
        if ((frame % 1024u) < 128u) {
            maximumBoundaryStep = std::max(maximumBoundaryStep, step);
        }
        previous = output.left;
        finite = std::isfinite(output.left) && std::isfinite(output.right)
            && std::abs(output.left) <= 1.801f
            && std::abs(output.right) <= 1.801f;
    }
    const double stressRms = rms(signal, 4096u);
    if (!finite || stressRms < 0.001 || maximumBoundaryStep > 1.25f) {
        std::cerr << "v4 matrix automation stress failed: RMS "
                  << stressRms << ", boundary step "
                  << maximumBoundaryStep << '\n';
        return false;
    }
    bank.reset();
    if (bank.active()) {
        std::cerr << "v4 bank reset did not clear activity lifecycle\n";
        return false;
    }
    return true;
}

bool profileAndStressProbe()
{
    std::array<double, 9u> matrixFingerprints {};
    for (uint32_t profile = 0u;
         profile < s3g::kAcapellaResonatorProfileFirst; ++profile) {
        const auto preset = static_cast<s3g::AcapellaSourcePreset>(profile);
        const auto sourceEffects = s3g::acapellaVocalFxPreset(preset);
        const auto routedEffects = s3g::acapellaResonatorProfileEffects(
            profile, sourceEffects);
        if (routedEffects.resonator.amount != sourceEffects.resonator.amount
            || routedEffects.resonator.mode != sourceEffects.resonator.mode
            || routedEffects.resonator.carrierHarmonics
                != sourceEffects.resonator.carrierHarmonics
            || routedEffects.resonator.carrierColor
                != sourceEffects.resonator.carrierColor
            || routedEffects.resonator.resonance
                != sourceEffects.resonator.resonance) {
            std::cerr << "source preset lost its resonator configuration: "
                      << profile << '\n';
            return false;
        }
    }

    for (uint32_t profile = s3g::kAcapellaResonatorProfileFirst;
         profile < s3g::kAcapellaResonatorProfileFirst
                + s3g::kAcapellaResonatorProfileCount; ++profile) {
        const auto base = s3g::acapellaResonatorProfileBase(profile);
        const auto wetParams = s3g::acapellaResonatorProfileEffects(
            profile, s3g::acapellaVocalFxPreset(base));
        if (profile >= 15u) {
            const auto& bank = wetParams.resonator;
            uint32_t offDiagonalA = 0u;
            uint32_t offDiagonalB = 0u;
            double sceneDifference = 0.0;
            double fingerprint = 0.0;
            for (uint32_t destination = 0u;
                 destination < s3g::kAcapellaResonatorBands;
                 ++destination) {
                for (uint32_t source = 0u;
                     source < s3g::kAcapellaResonatorBands; ++source) {
                    const size_t cell = destination
                            * s3g::kAcapellaResonatorBands
                        + source;
                    const float a = bank.customMatrixA[cell];
                    const float b = bank.customMatrixB[cell];
                    if (destination != source && std::abs(a) > 1.0e-5f) {
                        ++offDiagonalA;
                    }
                    if (destination != source && std::abs(b) > 1.0e-5f) {
                        ++offDiagonalB;
                    }
                    sceneDifference += std::abs(a - b);
                    fingerprint += static_cast<double>(a) * (cell + 3u)
                        + static_cast<double>(b) * (cell + 11u) * 1.731;
                }
            }
            const size_t fingerprintIndex = profile - 15u;
            matrixFingerprints[fingerprintIndex] = fingerprint;
            bool unique = true;
            for (size_t earlier = 0u; earlier < fingerprintIndex; ++earlier) {
                unique = unique
                    && std::abs(fingerprint - matrixFingerprints[earlier])
                        > 1.0e-3;
            }
            if (bank.matrixMode != s3g::AcapellaResonatorMatrixMode::Custom
                || bank.matrixMorph < 0.999f
                || bank.modulatorSource
                    != s3g::AcapellaResonatorModulatorSource::ExternalMic
                || offDiagonalA < 10u || offDiagonalB < 10u
                || sceneDifference < 4.0 || !unique) {
                std::cerr << "matrix-first profile contract failed: "
                          << profile << ", off-diagonal " << offDiagonalA
                          << '/' << offDiagonalB << ", scene delta "
                          << sceneDifference << ", unique " << unique << '\n';
                return false;
            }
        }
        auto dryParams = wetParams;
        dryParams.resonator.amount = 0.0f;
        s3g::AcapellaVocalEffects wet;
        s3g::AcapellaVocalEffects dry;
        wet.setParams(wetParams);
        dry.setParams(dryParams);
        wet.prepare(kSampleRate);
        dry.prepare(kSampleRate);
        std::vector<float> wetSignal(60000u);
        std::vector<float> drySignal(60000u);
        for (uint32_t frame = 0u; frame < wetSignal.size(); ++frame) {
            const bool consonant = (frame / 8000u) % 3u == 1u;
            const auto gesture = gestureFor(frame, consonant);
            wet.setResonatorGesture(gesture);
            dry.setResonatorGesture(gesture);
            const float left = analysisSignal(frame, consonant);
            const float right = left * 0.87f + carrierSignal(frame, 0.4f) * 0.13f;
            const auto wetFrame = wet.processFrameStereo(left, right);
            const auto dryFrame = dry.processFrameStereo(left, right);
            wetSignal[frame] = wetFrame.left;
            drySignal[frame] = dryFrame.left;
            if (!std::isfinite(wetFrame.left) || !std::isfinite(wetFrame.right)
                || std::abs(wetFrame.left) > 0.981f
                || std::abs(wetFrame.right) > 0.981f) {
                std::cerr << "factory resonator profile exceeded output guard\n";
                return false;
            }
        }
        const double wetRms = rms(wetSignal, 2048u);
        const double dryRms = rms(drySignal, 2048u);
        const double deltaRms = differenceRms(wetSignal, drySignal, 2048u);
        if (wetRms < 0.003
            || deltaRms / std::max(1.0e-6, dryRms) < 0.08) {
            std::cerr << "factory resonator profile was inaudible: "
                      << profile << ", wet RMS " << wetRms
                      << ", relative delta "
                      << deltaRms / std::max(1.0e-6, dryRms) << '\n';
            return false;
        }
    }

    s3g::AcapellaResonatorParams extreme;
    extreme.amount = 1.0f;
    extreme.resonance = 1.0f;
    extreme.driveDb = 24.0f;
    extreme.releaseMs = 5000.0f;
    extreme.freeze = 1.0f;
    extreme.freezeTrigger = s3g::AcapellaResonatorFreezeTrigger::Word;
    extreme.blurMs = 2000.0f;
    extreme.stereoSpread = 1.0f;
    const auto stress = renderBank(extreme, kSampleRate * 10u, false, true);
    if (!stress.finite || stress.peak > 2.0f || stress.maximumStep > 1.5f) {
        std::cerr << "resonator long automation stress was not bounded\n";
        return false;
    }
    return true;
}

} // namespace

int main()
{
    if (!formantBandLayoutProbe()
        || !formantParameterSanitizationProbe()
        || !formantVoicingProbe()
        || !formantOpenAndCarrierInputProbe()
        || !formantRoutingTrimAndMeterProbe()
        || !formantStereoAndArticulationProbe()
        || !measuredSpeechEnvelopeProbe()
        || !measuredConsonantTransferProbe()
        || !voicePitchCarrierProbe()
        || !eightPoleAnalysisProbe()
        || !formantCarrierLfoAndAutomationProbe()
        || !neutralAndModeProbe()
        || !selectedCarrierDryRoutingProbe()
        || !sampleRateAndRetriggerProbe()
        || !carrierAndControlAxesProbe()
        || !stereoFreezeAndConsonantProbe()
        || !focusedContractProbe()
        || !vowelSuspensionProfileProbe()
        || !extremeAutomationBoundaryProbe()
        || !profileAndStressProbe()) {
        return 1;
    }
    std::cout << "Acapella resonator regression smoke passed"
              << "; Vowel Suspension active RMS frozen/unfrozen "
              << gVowelSuspensionMetrics.activeRms << '/'
              << gVowelSuspensionMetrics.unfrozenActiveRms
              << ", tail RMS frozen/unfrozen "
              << gVowelSuspensionMetrics.tailRms << '/'
              << gVowelSuspensionMetrics.unfrozenTailRms
              << ", tail delta " << gVowelSuspensionMetrics.tailDelta
              << ", late RMS " << gVowelSuspensionMetrics.lateRms;
    for (const auto& metrics : gAutomationBoundaryMetrics) {
        std::cout << "; automation " << metrics.sampleRate
                  << " Hz local RMS/step/limit " << metrics.localRms << '/'
                  << metrics.maximumStep << '/' << metrics.stepLimit;
    }
    std::cout << '\n';
    return 0;
}
