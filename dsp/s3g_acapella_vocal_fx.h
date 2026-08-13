#pragma once

#include "s3g_acapella_source_synth.h"
#include "s3g_acapella_resonator_bank.h"
#include "s3g_drum_echo.h"
#include "s3g_math.h"
#include "s3g_realtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace s3g {

struct AcapellaVocalFxParams {
    float octaveDown = 0.0f;
    float octaveUp = 0.0f;
    float fuzzDriveDb = 0.0f;
    float fuzzMix = 0.0f;
    float fuzzToneHz = 6500.0f;
    float compression = 0.22f;
    float parallelCrush = 0.0f;
    float deEss = 0.10f;
    DrumEchoHeadMode echoHeads = DrumEchoHeadMode::Heads123;
    DrumEchoClock echoClock = DrumEchoClock::Eighth;
    float echoTimeMs = 180.0f;
    float echoFeedback = 0.34f;
    float echoWear = 0.18f;
    float echoFlutter = 0.10f;
    float echoTone = -0.12f;
    float echoSpread = 0.58f;
    float echoMix = 0.0f;
    float width = 0.0f;
    float intelligibility = 0.78f;
    AcapellaResonatorParams resonator = [] {
        AcapellaResonatorParams bank;
        bank.amount = 0.90f;
        bank.mode = AcapellaResonatorMode::Hybrid;
        bank.analysisBlend = 0.72f;
        bank.gestureFollow = 0.82f;
        bank.externalCarrierMix = 0.55f;
        return bank;
    }();
};

struct AcapellaStereoFrame {
    float left = 0.0f;
    float right = 0.0f;
};

inline AcapellaVocalFxParams sanitizeAcapellaVocalFxParams(
    AcapellaVocalFxParams params)
{
    params.octaveDown = clamp(acapellaFiniteOr(params.octaveDown, 0.0f),
        0.0f, 1.0f);
    params.octaveUp = clamp(acapellaFiniteOr(params.octaveUp, 0.0f),
        0.0f, 1.0f);
    params.fuzzDriveDb = clamp(acapellaFiniteOr(params.fuzzDriveDb, 0.0f),
        0.0f, 30.0f);
    params.fuzzMix = clamp(acapellaFiniteOr(params.fuzzMix, 0.0f),
        0.0f, 1.0f);
    params.fuzzToneHz = clamp(acapellaFiniteOr(params.fuzzToneHz, 6500.0f),
        700.0f, 16000.0f);
    params.compression = clamp(acapellaFiniteOr(params.compression, 0.22f),
        0.0f, 1.0f);
    params.parallelCrush = clamp(acapellaFiniteOr(params.parallelCrush, 0.0f),
        0.0f, 1.0f);
    params.deEss = clamp(acapellaFiniteOr(params.deEss, 0.10f), 0.0f, 1.0f);
    params.echoHeads = static_cast<DrumEchoHeadMode>(std::min<uint32_t>(
        static_cast<uint32_t>(params.echoHeads), kDrumEchoHeadModeCount - 1u));
    params.echoClock = static_cast<DrumEchoClock>(std::min<uint32_t>(
        static_cast<uint32_t>(params.echoClock), kDrumEchoClockCount - 1u));
    params.echoTimeMs = clamp(acapellaFiniteOr(params.echoTimeMs, 180.0f),
        20.0f, 1800.0f);
    params.echoFeedback = clamp(acapellaFiniteOr(params.echoFeedback, 0.34f),
        0.0f, 0.92f);
    params.echoWear = clamp(acapellaFiniteOr(params.echoWear, 0.18f),
        0.0f, 1.0f);
    params.echoFlutter = clamp(acapellaFiniteOr(params.echoFlutter, 0.10f),
        0.0f, 1.0f);
    params.echoTone = clamp(acapellaFiniteOr(params.echoTone, -0.12f),
        -1.0f, 1.0f);
    params.echoSpread = clamp(acapellaFiniteOr(params.echoSpread, 0.58f),
        0.0f, 1.0f);
    params.echoMix = clamp(acapellaFiniteOr(params.echoMix, 0.0f),
        0.0f, 1.0f);
    params.width = clamp(acapellaFiniteOr(params.width, 0.0f), 0.0f, 1.0f);
    params.intelligibility = clamp(acapellaFiniteOr(
        params.intelligibility, 0.78f), 0.0f, 1.0f);
    params.resonator = sanitizeAcapellaResonatorParams(params.resonator);
    return params;
}

inline AcapellaVocalFxParams acapellaVocalFxPreset(
    AcapellaSourcePreset preset)
{
    AcapellaVocalFxParams params;
    switch (preset) {
    case AcapellaSourcePreset::RhythmicRap:
        params.compression = 0.40f;
        params.parallelCrush = 0.10f;
        params.deEss = 0.24f;
        params.fuzzDriveDb = 6.0f;
        params.fuzzMix = 0.06f;
        params.resonator.amount = 0.92f;
        params.resonator.mode = AcapellaResonatorMode::Hybrid;
        params.resonator.analysisBlend = 0.62f;
        params.resonator.gestureFollow = 0.90f;
        params.resonator.sibilance = 0.72f;
        params.resonator.attackMs = 2.5f;
        params.resonator.releaseMs = 82.0f;
        params.resonator.coupling = 1;
        params.resonator.externalCarrierMix = 0.64f;
        params.resonator.stereoMode = AcapellaResonatorStereoMode::OddEven;
        params.resonator.stereoSpread = 0.42f;
        break;
    case AcapellaSourcePreset::AirySung:
        params.compression = 0.18f;
        params.deEss = 0.28f;
        params.width = 0.10f;
        params.resonator.amount = 0.90f;
        params.resonator.mode = AcapellaResonatorMode::Hybrid;
        params.resonator.voicingMode =
            AcapellaResonatorVoicingMode::Blend;
        params.resonator.carrierNoise = 0.34f;
        params.resonator.releaseMs = 115.0f;
        params.resonator.blurMs = 24.0f;
        params.resonator.externalCarrierMix = 0.42f;
        params.resonator.stereoMode = AcapellaResonatorStereoMode::Spread;
        params.resonator.stereoSpread = 0.68f;
        break;
    case AcapellaSourcePreset::PressedLead:
        params.compression = 0.50f;
        params.parallelCrush = 0.16f;
        params.deEss = 0.20f;
        params.fuzzDriveDb = 9.0f;
        params.fuzzMix = 0.13f;
        params.fuzzToneHz = 7200.0f;
        params.resonator.amount = 0.95f;
        params.resonator.mode = AcapellaResonatorMode::Vocoder;
        params.resonator.resonance = 0.48f;
        params.resonator.driveDb = 5.0f;
        params.resonator.gestureFollow = 0.84f;
        params.resonator.externalCarrierMix = 0.76f;
        params.resonator.openLevel = 0.025f;
        break;
    case AcapellaSourcePreset::HarshScream:
        params.octaveDown = 0.07f;
        params.octaveUp = 0.14f;
        params.fuzzDriveDb = 15.0f;
        params.fuzzMix = 0.34f;
        params.fuzzToneHz = 6800.0f;
        params.compression = 0.70f;
        params.parallelCrush = 0.34f;
        params.deEss = 0.42f;
        params.echoHeads = DrumEchoHeadMode::Head12;
        params.echoClock = DrumEchoClock::Sixteenth;
        params.echoFeedback = 0.25f;
        params.echoMix = 0.08f;
        params.width = 0.20f;
        params.resonator.amount = 0.96f;
        params.resonator.mode = AcapellaResonatorMode::Hybrid;
        params.resonator.carrierHarmonics = 0.84f;
        params.resonator.carrierColor = 0.68f;
        params.resonator.resonance = 0.56f;
        params.resonator.driveDb = 11.0f;
        params.resonator.analysisBlend = 0.70f;
        params.resonator.sibilance = 0.72f;
        params.resonator.externalCarrierMix = 0.58f;
        params.resonator.carrierLfoRateHz = 0.31f;
        params.resonator.carrierLfoDepthSemitones = 0.45f;
        params.resonator.stereoMode = AcapellaResonatorStereoMode::OddEven;
        params.resonator.stereoSpread = 0.58f;
        break;
    case AcapellaSourcePreset::DeathGrowl:
        params.octaveDown = 0.34f;
        params.octaveUp = 0.04f;
        params.fuzzDriveDb = 18.0f;
        params.fuzzMix = 0.43f;
        params.fuzzToneHz = 3900.0f;
        params.compression = 0.78f;
        params.parallelCrush = 0.46f;
        params.deEss = 0.22f;
        params.echoHeads = DrumEchoHeadMode::Head23;
        params.echoClock = DrumEchoClock::EighthTriplet;
        params.echoFeedback = 0.39f;
        params.echoTone = -0.42f;
        params.echoMix = 0.17f;
        params.width = 0.16f;
        params.resonator.amount = 0.98f;
        params.resonator.mode = AcapellaResonatorMode::FilterBank;
        params.resonator.carrierHarmonics = 0.92f;
        params.resonator.carrierColor = 0.38f;
        params.resonator.resonance = 0.68f;
        params.resonator.driveDb = 15.0f;
        params.resonator.bandShiftSemitones = -2.4f;
        params.resonator.tilt = -0.32f;
        params.resonator.openLevel = 0.26f;
        params.resonator.coupling = -2;
        params.resonator.externalCarrierMix = 0.48f;
        break;
    case AcapellaSourcePreset::NeutralSung:
    default:
        params.resonator.amount = 0.90f;
        params.resonator.mode = AcapellaResonatorMode::Hybrid;
        params.resonator.bandLayout =
            AcapellaResonatorBandLayout::Speech22;
        params.resonator.voicingMode =
            AcapellaResonatorVoicingMode::Detect;
        params.resonator.matrixMode =
            AcapellaResonatorMatrixMode::Custom;
        params.resonator.matrixMorph = 1.0f;
        params.resonator.externalCarrierMix = 0.55f;
        params.resonator.stereoMode = AcapellaResonatorStereoMode::OddEven;
        break;
    }
    return sanitizeAcapellaVocalFxParams(params);
}

constexpr uint32_t kAcapellaResonatorProfileFirst = 6u;
constexpr uint32_t kAcapellaResonatorProfileCount = 25u;

inline AcapellaSourcePreset acapellaResonatorProfileBase(uint32_t index)
{
    switch (index) {
    case 6u:
    case 7u:
    case 9u:
        return AcapellaSourcePreset::AirySung;
    case 10u:
    case 11u:
        return AcapellaSourcePreset::RhythmicRap;
    case 12u:
        return AcapellaSourcePreset::PressedLead;
    default:
        return AcapellaSourcePreset::NeutralSung;
    }
}

// Shared by the plug-in, renderer, and regressions so factory profiles cannot
// drift away from what is actually tested. Each profile starts from a neutral
// bank while the source preset supplies its underlying articulation colour.
inline AcapellaVocalFxParams acapellaResonatorProfileEffects(uint32_t index,
    AcapellaVocalFxParams effects)
{
    if (index < kAcapellaResonatorProfileFirst
        || index >= kAcapellaResonatorProfileFirst
            + kAcapellaResonatorProfileCount) {
        return sanitizeAcapellaVocalFxParams(effects);
    }
    effects.resonator = AcapellaResonatorParams {};
    const auto clearScene = [](auto& scene) { scene.fill(0.0f); };
    const auto addRoute = [](auto& scene, int32_t destination,
                              int32_t source, float gain) {
        if (destination < 0 || source < 0
            || destination >= static_cast<int32_t>(kAcapellaResonatorBands)
            || source >= static_cast<int32_t>(kAcapellaResonatorBands)) {
            return;
        }
        scene[static_cast<size_t>(destination)
                * kAcapellaResonatorBands
            + static_cast<size_t>(source)] += gain;
    };
    const auto shiftedScene = [&](auto& scene, int32_t shift,
                                  float adjacent = 0.0f) {
        clearScene(scene);
        for (int32_t source = 0;
             source < static_cast<int32_t>(kAcapellaResonatorBands);
             ++source) {
            const int32_t destination = std::clamp(source + shift, 0,
                static_cast<int32_t>(kAcapellaResonatorBands) - 1);
            addRoute(scene, destination, source, 1.0f);
            if (adjacent != 0.0f) {
                addRoute(scene, destination - 1, source, adjacent);
                addRoute(scene, destination + 1, source, adjacent);
            }
        }
    };
    const auto configureMicMatrix = [&](AcapellaResonatorParams& bank) {
        bank.amount = 1.0f;
        bank.bandLayout = AcapellaResonatorBandLayout::Speech22;
        bank.analysisSlope = AcapellaResonatorAnalysisSlope::EightPole;
        bank.mode = AcapellaResonatorMode::Vocoder;
        bank.modulatorSource = AcapellaResonatorModulatorSource::ExternalMic;
        bank.carrierPitchSource = AcapellaResonatorCarrierPitchSource::Voice;
        bank.pitchScale = AcapellaResonatorPitchScale::Continuous;
        bank.pitchHoldMs = kAcapellaResonatorInfinitePitchHoldMs;
        bank.analysisBlend = 0.0f;
        bank.transferMode = AcapellaResonatorTransferMode::Precision;
        bank.voiceFocus = 0.28f;
        bank.analysisLeveler = 0.72f;
        bank.consonantColor = 0.35f;
        bank.consonantSpeed = 0.18f;
        bank.carrierDensity = 0.58f;
        bank.analysisWidth = 0.68f;
        bank.hfDetailMode = AcapellaResonatorHfDetailMode::Switched;
        bank.hfDetailLevel = 0.16f;
        bank.hfDetailCutoffHz = 4200.0f;
        bank.analysisLowDb = -1.5f;
        bank.analysisMidDb = 2.0f;
        bank.analysisAirDb = 1.5f;
        bank.analysisCompression = 0.42f;
        bank.analysisNoiseReject = 0.46f;
        bank.analysisSpectralBalance = 0.32f;
        bank.attackMs = 2.0f;
        bank.releaseMs = 78.0f;
        bank.blurMs = 5.0f;
        bank.openLevel = 0.0f;
        bank.articulationThru = 0.0f;
        bank.matrixMode = AcapellaResonatorMatrixMode::Custom;
        bank.matrixMorph = 1.0f;
        bank.stereoMode = AcapellaResonatorStereoMode::Spread;
        bank.stereoSpread = 0.34f;
    };
    switch (index) {
    case 6u: // Vowel Suspension
        effects.resonator.amount = 0.94f;
        effects.resonator.mode = AcapellaResonatorMode::Hybrid;
        effects.resonator.carrierShape =
            AcapellaResonatorCarrierShape::Glottal;
        effects.resonator.carrierHarmonics = 0.78f;
        effects.resonator.carrierColor = 0.48f;
        effects.resonator.resonance = 0.66f;
        effects.resonator.attackMs = 18.0f;
        effects.resonator.releaseMs = 360.0f;
        effects.resonator.analysisBlend = 0.88f;
        effects.resonator.gestureFollow = 0.92f;
        effects.resonator.freeze = 0.86f;
        effects.resonator.freezeTrigger =
            AcapellaResonatorFreezeTrigger::Word;
        effects.resonator.blurMs = 280.0f;
        break;
    case 7u: // Breath Mirror
        effects.resonator.amount = 0.92f;
        effects.resonator.mode = AcapellaResonatorMode::Vocoder;
        effects.resonator.carrierShape = AcapellaResonatorCarrierShape::Noise;
        effects.resonator.carrierHarmonics = 0.38f;
        effects.resonator.carrierColor = 0.78f;
        effects.resonator.carrierNoise = 0.78f;
        effects.resonator.resonance = 0.54f;
        effects.resonator.releaseMs = 210.0f;
        effects.resonator.sibilance = 0.88f;
        effects.resonator.matrixMode = AcapellaResonatorMatrixMode::Mirror;
        effects.resonator.matrixMorph = 0.76f;
        effects.resonator.tilt = 0.34f;
        effects.resonator.stereoSpread = 0.72f;
        break;
    case 8u: // Formant Loom
        effects.resonator.amount = 0.96f;
        effects.resonator.mode = AcapellaResonatorMode::Hybrid;
        effects.resonator.carrierShape = AcapellaResonatorCarrierShape::Fold;
        effects.resonator.carrierHarmonics = 0.88f;
        effects.resonator.carrierColor = 0.64f;
        effects.resonator.resonance = 0.72f;
        effects.resonator.driveDb = 7.0f;
        effects.resonator.analysisBlend = 0.78f;
        effects.resonator.bandShiftSemitones = 3.2f;
        effects.resonator.bandStretch = 0.36f;
        effects.resonator.matrixMode = AcapellaResonatorMatrixMode::Rotate;
        effects.resonator.matrixMorph = 0.42f;
        effects.resonator.gestureFollow = 0.82f;
        break;
    case 9u: // Resonant Rain
        effects.resonator.amount = 1.0f;
        effects.resonator.mode = AcapellaResonatorMode::Resonator;
        effects.resonator.carrierShape = AcapellaResonatorCarrierShape::Pulse;
        effects.resonator.carrierHarmonics = 0.86f;
        effects.resonator.carrierColor = 0.72f;
        effects.resonator.carrierNoise = 0.46f;
        effects.resonator.resonance = 0.88f;
        effects.resonator.driveDb = 10.0f;
        effects.resonator.releaseMs = 520.0f;
        effects.resonator.bandStretch = 0.58f;
        effects.resonator.matrixMode = AcapellaResonatorMatrixMode::Sparse;
        effects.resonator.matrixMorph = 0.68f;
        effects.resonator.blurMs = 120.0f;
        effects.resonator.gestureFollow = 0.34f;
        effects.resonator.stereoSpread = 0.88f;
        break;
    case 10u: // Carrier Choir
        effects.resonator.amount = 0.96f;
        effects.resonator.mode = AcapellaResonatorMode::Hybrid;
        effects.resonator.carrierShape = AcapellaResonatorCarrierShape::Saw;
        effects.resonator.carrierHarmonics = 0.92f;
        effects.resonator.carrierColor = 0.56f;
        effects.resonator.carrierNoise = 0.16f;
        effects.resonator.resonance = 0.62f;
        effects.resonator.releaseMs = 260.0f;
        effects.resonator.analysisBlend = 0.84f;
        effects.resonator.gestureFollow = 0.90f;
        effects.resonator.matrixMode = AcapellaResonatorMatrixMode::Chord;
        effects.resonator.matrixMorph = 0.36f;
        effects.resonator.blurMs = 72.0f;
        effects.resonator.stereoSpread = 0.94f;
        break;
    case 11u: // Consonant Shadow
        effects.resonator.amount = 0.90f;
        effects.resonator.mode = AcapellaResonatorMode::Hybrid;
        effects.resonator.carrierShape = AcapellaResonatorCarrierShape::Noise;
        effects.resonator.carrierNoise = 0.58f;
        effects.resonator.resonance = 0.48f;
        effects.resonator.attackMs = 0.8f;
        effects.resonator.releaseMs = 95.0f;
        effects.resonator.analysisBlend = 0.92f;
        effects.resonator.gestureFollow = 1.0f;
        effects.resonator.sibilance = 1.0f;
        effects.resonator.tilt = 0.42f;
        effects.resonator.matrixMorph = 0.22f;
        break;
    case 12u: // Moving Scar
        effects.resonator.amount = 1.0f;
        effects.resonator.mode = AcapellaResonatorMode::Resonator;
        effects.resonator.carrierShape = AcapellaResonatorCarrierShape::Fold;
        effects.resonator.carrierHarmonics = 1.0f;
        effects.resonator.carrierColor = 0.34f;
        effects.resonator.carrierNoise = 0.24f;
        effects.resonator.resonance = 0.92f;
        effects.resonator.driveDb = 18.0f;
        effects.resonator.releaseMs = 680.0f;
        effects.resonator.bandShiftSemitones = -4.0f;
        effects.resonator.bandStretch = -0.38f;
        effects.resonator.matrixMode = AcapellaResonatorMatrixMode::Rotate;
        effects.resonator.matrixMorph = 0.78f;
        effects.resonator.freeze = 0.36f;
        effects.resonator.freezeTrigger =
            AcapellaResonatorFreezeTrigger::Word;
        effects.resonator.blurMs = 190.0f;
        effects.resonator.gestureFollow = 0.22f;
        effects.resonator.stereoSpread = 0.80f;
        break;
    case 13u: // Chord Glass
        effects.resonator.amount = 0.98f;
        effects.resonator.mode = AcapellaResonatorMode::Hybrid;
        effects.resonator.carrierShape = AcapellaResonatorCarrierShape::Saw;
        effects.resonator.carrierHarmonics = 0.96f;
        effects.resonator.carrierColor = 0.62f;
        effects.resonator.resonance = 0.86f;
        effects.resonator.driveDb = 5.0f;
        effects.resonator.releaseMs = 820.0f;
        effects.resonator.analysisBlend = 0.74f;
        effects.resonator.gestureFollow = 0.76f;
        effects.resonator.bandShiftSemitones = 2.0f;
        effects.resonator.bandStretch = 0.24f;
        effects.resonator.matrixMode = AcapellaResonatorMatrixMode::Chord;
        effects.resonator.matrixMorph = 0.86f;
        effects.resonator.freeze = 0.22f;
        effects.resonator.freezeTrigger =
            AcapellaResonatorFreezeTrigger::Syllable;
        effects.resonator.blurMs = 240.0f;
        effects.resonator.stereoSpread = 1.0f;
        break;
    case 14u: // Classic Mic
        effects.resonator.amount = 1.0f;
        effects.resonator.bandLayout =
            AcapellaResonatorBandLayout::Speech22;
        effects.resonator.analysisSlope =
            AcapellaResonatorAnalysisSlope::EightPole;
        effects.resonator.mode = AcapellaResonatorMode::Vocoder;
        effects.resonator.modulatorSource =
            AcapellaResonatorModulatorSource::ExternalMic;
        effects.resonator.micGainDb = 0.0f;
        effects.resonator.carrierShape = AcapellaResonatorCarrierShape::Saw;
        effects.resonator.carrierHarmonics = 0.94f;
        effects.resonator.carrierColor = 0.12f;
        effects.resonator.carrierNoise = 0.18f;
        effects.resonator.analysisBlend = 0.0f;
        effects.resonator.transferMode =
            AcapellaResonatorTransferMode::Precision;
        effects.resonator.voiceFocus = 0.28f;
        effects.resonator.analysisLeveler = 0.72f;
        effects.resonator.consonantColor = 0.35f;
        effects.resonator.consonantSpeed = 0.18f;
        effects.resonator.carrierDensity = 0.58f;
        effects.resonator.analysisWidth = 0.68f;
        effects.resonator.hfDetailMode =
            AcapellaResonatorHfDetailMode::Switched;
        effects.resonator.hfDetailLevel = 0.16f;
        effects.resonator.hfDetailCutoffHz = 4200.0f;
        effects.resonator.analysisLowDb = -1.5f;
        effects.resonator.analysisMidDb = 2.0f;
        effects.resonator.analysisAirDb = 1.5f;
        effects.resonator.analysisCompression = 0.42f;
        effects.resonator.analysisNoiseReject = 0.46f;
        effects.resonator.analysisSpectralBalance = 0.32f;
        // Fast, lightly smoothed followers preserve plosives and transitions
        // between formants. The earlier 6 ms attack + 110 ms release + 70 ms
        // Blur behaved mainly as a broadband loudness follower.
        effects.resonator.attackMs = 2.0f;
        effects.resonator.releaseMs = 65.0f;
        effects.resonator.blurMs = 4.0f;
        effects.resonator.voicingThreshold = 0.44f;
        effects.resonator.sibilance = 0.78f;
        effects.resonator.openLevel = 0.0f;
        // A classic fully-wet vocoder must not monitor the microphone itself.
        // Articulation Thru remains available as an explicit creative option,
        // but the quick-start profile keeps that direct high-pass rail closed.
        effects.resonator.articulationThru = 0.0f;
        effects.resonator.voicingMode =
            AcapellaResonatorVoicingMode::Detect;
        effects.resonator.bandShiftSemitones = 0.0f;
        effects.resonator.bandStretch = 0.0f;
        effects.resonator.coupling = 0;
        effects.resonator.matrixMode =
            AcapellaResonatorMatrixMode::Identity;
        effects.resonator.matrixMorph = 1.0f;
        effects.resonator.stereoMode = AcapellaResonatorStereoMode::Mono;
        effects.resonator.stereoSpread = 0.0f;
        break;
    case 15u: { // Formant Glide
        auto& bank = effects.resonator;
        configureMicMatrix(bank);
        bank.carrierShape = AcapellaResonatorCarrierShape::Glottal;
        bank.carrierHarmonics = 0.82f;
        bank.carrierColor = 0.16f;
        bank.resonance = 0.58f;
        bank.customMatrixMorph = 0.50f;
        shiftedScene(bank.customMatrixA, -2, 0.18f);
        shiftedScene(bank.customMatrixB, 3, 0.18f);
        effects.compression = 0.34f;
        effects.intelligibility = 0.90f;
        break;
    }
    case 16u: { // Fixed Circuit
        auto& bank = effects.resonator;
        configureMicMatrix(bank);
        bank.pitchScale = AcapellaResonatorPitchScale::Chromatic;
        bank.carrierShape = AcapellaResonatorCarrierShape::Pulse;
        bank.pulseWidth = 0.42f;
        bank.carrierHarmonics = 0.92f;
        bank.carrierColor = 0.30f;
        bank.voicingMode = AcapellaResonatorVoicingMode::Tonal;
        bank.resonance = 0.70f;
        bank.releaseMs = 105.0f;
        bank.customMatrixMorph = 0.0f;
        clearScene(bank.customMatrixA);
        clearScene(bank.customMatrixB);
        for (int32_t source = 0; source < 22; ++source) {
            const int32_t block = (source / 3) * 3 + 1;
            addRoute(bank.customMatrixA, block, source, 1.0f);
            addRoute(bank.customMatrixB, 21 - block, source,
                source % 2 == 0 ? 1.0f : -0.72f);
        }
        effects.compression = 0.52f;
        effects.parallelCrush = 0.10f;
        effects.intelligibility = 0.86f;
        break;
    }
    case 17u: { // Glass Harmony
        auto& bank = effects.resonator;
        configureMicMatrix(bank);
        bank.pitchScaleRoot = 0u;
        bank.pitchScale = AcapellaResonatorPitchScale::Major;
        bank.carrierShape = AcapellaResonatorCarrierShape::Saw;
        bank.carrierHarmonics = 0.96f;
        bank.carrierColor = 0.48f;
        bank.resonance = 0.78f;
        bank.releaseMs = 180.0f;
        bank.customMatrixMorph = 0.42f;
        clearScene(bank.customMatrixA);
        clearScene(bank.customMatrixB);
        for (int32_t source = 0; source < 22; ++source) {
            addRoute(bank.customMatrixA, source, source, 0.84f);
            addRoute(bank.customMatrixA, source + 3, source, 0.52f);
            addRoute(bank.customMatrixB, source + 2, source, 0.68f);
            addRoute(bank.customMatrixB, source + 5, source, 0.44f);
            addRoute(bank.customMatrixB, source - 4, source, 0.24f);
        }
        bank.stereoMode = AcapellaResonatorStereoMode::OddEven;
        bank.stereoSpread = 0.86f;
        effects.octaveUp = 0.09f;
        effects.width = 0.22f;
        effects.intelligibility = 0.88f;
        break;
    }
    case 18u: { // Public Address
        auto& bank = effects.resonator;
        configureMicMatrix(bank);
        bank.carrierShape = AcapellaResonatorCarrierShape::Fold;
        bank.carrierHarmonics = 0.88f;
        bank.carrierColor = 0.66f;
        bank.carrierNoise = 0.28f;
        bank.resonance = 0.52f;
        bank.driveDb = 10.0f;
        bank.releaseMs = 54.0f;
        bank.customMatrixMorph = 0.26f;
        clearScene(bank.customMatrixA);
        clearScene(bank.customMatrixB);
        for (int32_t source = 4; source <= 17; ++source) {
            addRoute(bank.customMatrixA, source, source, 1.0f);
            addRoute(bank.customMatrixA, source + 1, source, -0.34f);
            const int32_t destination = 8 + (source % 7);
            addRoute(bank.customMatrixB, destination, source, 1.0f);
            addRoute(bank.customMatrixB, destination - 1, source, -0.52f);
        }
        effects.fuzzDriveDb = 17.0f;
        effects.fuzzMix = 0.30f;
        effects.fuzzToneHz = 3200.0f;
        effects.compression = 0.72f;
        effects.parallelCrush = 0.24f;
        effects.intelligibility = 0.74f;
        break;
    }
    case 19u: { // Pocket Radio
        auto& bank = effects.resonator;
        configureMicMatrix(bank);
        bank.pitchScale = AcapellaResonatorPitchScale::Chromatic;
        bank.carrierShape = AcapellaResonatorCarrierShape::Noise;
        bank.carrierHarmonics = 0.54f;
        bank.carrierNoise = 0.62f;
        bank.resonance = 0.64f;
        bank.releaseMs = 70.0f;
        bank.sibilance = 0.88f;
        bank.customMatrixMorph = 0.58f;
        clearScene(bank.customMatrixA);
        clearScene(bank.customMatrixB);
        for (int32_t source = 6; source <= 19; ++source) {
            const int32_t destinationA = 10 + source % 5;
            const int32_t destinationB = 12 + source % 4;
            addRoute(bank.customMatrixA, destinationA, source, 1.0f);
            addRoute(bank.customMatrixA, destinationA + 1, source, -0.28f);
            addRoute(bank.customMatrixB, destinationB, source,
                source % 2 == 0 ? 1.0f : -0.64f);
            addRoute(bank.customMatrixB, destinationB - 1, source, 0.24f);
        }
        effects.fuzzDriveDb = 8.0f;
        effects.fuzzMix = 0.12f;
        effects.fuzzToneHz = 2700.0f;
        effects.compression = 0.66f;
        effects.parallelCrush = 0.18f;
        effects.echoHeads = DrumEchoHeadMode::Head1;
        effects.echoClock = DrumEchoClock::Sixteenth;
        effects.echoFeedback = 0.16f;
        effects.echoTone = -0.38f;
        effects.echoMix = 0.08f;
        effects.intelligibility = 0.78f;
        break;
    }
    case 20u: { // Low Persona
        auto& bank = effects.resonator;
        configureMicMatrix(bank);
        bank.carrierShape = AcapellaResonatorCarrierShape::Glottal;
        bank.carrierHarmonics = 0.84f;
        bank.carrierColor = -0.36f;
        bank.resonance = 0.70f;
        bank.tilt = -0.48f;
        bank.customMatrixMorph = 0.36f;
        shiftedScene(bank.customMatrixA, -2, 0.16f);
        shiftedScene(bank.customMatrixB, -5, 0.24f);
        effects.octaveDown = 0.18f;
        effects.compression = 0.54f;
        effects.intelligibility = 0.90f;
        break;
    }
    case 21u: { // Bright Persona
        auto& bank = effects.resonator;
        configureMicMatrix(bank);
        bank.carrierShape = AcapellaResonatorCarrierShape::Saw;
        bank.carrierHarmonics = 0.90f;
        bank.carrierColor = 0.54f;
        bank.carrierNoise = 0.20f;
        bank.resonance = 0.62f;
        bank.tilt = 0.56f;
        bank.sibilance = 0.90f;
        bank.customMatrixMorph = 0.44f;
        shiftedScene(bank.customMatrixA, 2, 0.14f);
        shiftedScene(bank.customMatrixB, 5, 0.20f);
        effects.octaveUp = 0.12f;
        effects.deEss = 0.22f;
        effects.compression = 0.40f;
        effects.intelligibility = 0.92f;
        break;
    }
    case 22u: { // Broken Relay
        auto& bank = effects.resonator;
        configureMicMatrix(bank);
        bank.pitchScale = AcapellaResonatorPitchScale::MinorPentatonic;
        bank.carrierShape = AcapellaResonatorCarrierShape::Fold;
        bank.carrierHarmonics = 1.0f;
        bank.carrierColor = 0.72f;
        bank.resonance = 0.82f;
        bank.driveDb = 13.0f;
        bank.releaseMs = 92.0f;
        bank.customMatrixMorph = 0.66f;
        clearScene(bank.customMatrixA);
        clearScene(bank.customMatrixB);
        for (int32_t source = 0; source < 22; ++source) {
            if (source % 3 != 1) {
                addRoute(bank.customMatrixA,
                    (source * 7 + 3) % 22, source,
                    source % 2 == 0 ? 1.0f : -0.74f);
            }
            if (source % 4 != 2) {
                addRoute(bank.customMatrixB,
                    (source * 11 + 5) % 22, source,
                    source % 3 == 0 ? -0.82f : 1.0f);
            }
        }
        bank.carrierLfoShape = AcapellaResonatorCarrierLfoShape::Square;
        bank.carrierLfoSync = true;
        bank.carrierLfoSyncDivisionBeats = 0.25f;
        bank.carrierLfoDepthSemitones = 0.72f;
        bank.carrierLfoPwmDepth = 0.38f;
        bank.stereoMode = AcapellaResonatorStereoMode::OddEven;
        bank.stereoSpread = 0.82f;
        effects.parallelCrush = 0.28f;
        effects.echoHeads = DrumEchoHeadMode::Head13;
        effects.echoClock = DrumEchoClock::Sixteenth;
        effects.echoFeedback = 0.42f;
        effects.echoWear = 0.54f;
        effects.echoFlutter = 0.28f;
        effects.echoSpread = 0.84f;
        effects.echoMix = 0.24f;
        effects.intelligibility = 0.72f;
        break;
    }
    case 23u: { // Vocal Alloy
        auto& bank = effects.resonator;
        configureMicMatrix(bank);
        bank.carrierPitchSource = AcapellaResonatorCarrierPitchSource::Midi;
        bank.carrierShape = AcapellaResonatorCarrierShape::Saw;
        bank.carrierHarmonics = 0.98f;
        bank.carrierColor = 0.34f;
        bank.carrierNoise = 0.14f;
        bank.resonance = 0.84f;
        bank.driveDb = 6.0f;
        bank.releaseMs = 240.0f;
        bank.customMatrixMorph = 0.48f;
        clearScene(bank.customMatrixA);
        clearScene(bank.customMatrixB);
        for (int32_t source = 0; source < 22; ++source) {
            addRoute(bank.customMatrixA, source, source, 0.72f);
            addRoute(bank.customMatrixA, source + 4, source, 0.48f);
            addRoute(bank.customMatrixA, source - 3, source, 0.26f);
            addRoute(bank.customMatrixB, 21 - source, source, 0.62f);
            addRoute(bank.customMatrixB, source + 7, source, 0.38f);
            addRoute(bank.customMatrixB, source - 5, source, -0.22f);
        }
        bank.stereoMode = AcapellaResonatorStereoMode::OddEven;
        bank.stereoSpread = 1.0f;
        effects.compression = 0.48f;
        effects.width = 0.30f;
        effects.echoHeads = DrumEchoHeadMode::Head23;
        effects.echoClock = DrumEchoClock::EighthTriplet;
        effects.echoFeedback = 0.24f;
        effects.echoMix = 0.10f;
        effects.intelligibility = 0.86f;
        break;
    }
    case 24u: { // Mouth Circuit
        auto& bank = effects.resonator;
        configureMicMatrix(bank);
        bank.analysisSlope = AcapellaResonatorAnalysisSlope::MouthModel;
        bank.mouthFocus = 0.92f;
        bank.voiceFocus = 0.34f;
        bank.analysisLeveler = 0.68f;
        bank.consonantColor = 0.18f;
        bank.consonantSpeed = 0.12f;
        bank.carrierDensity = 0.66f;
        bank.analysisWidth = 0.62f;
        bank.hfDetailLevel = 0.12f;
        bank.hfDetailCutoffHz = 3900.0f;
        bank.carrierPitchSource = AcapellaResonatorCarrierPitchSource::Midi;
        bank.carrierShape = AcapellaResonatorCarrierShape::Saw;
        bank.carrierHarmonics = 0.98f;
        bank.carrierColor = 0.24f;
        bank.carrierNoise = 0.08f;
        bank.voicingMode = AcapellaResonatorVoicingMode::Detect;
        bank.voicingThreshold = 0.44f;
        bank.attackMs = 1.0f;
        bank.releaseMs = 42.0f;
        bank.blurMs = 1.0f;
        bank.resonance = 0.44f;
        bank.driveDb = 1.5f;
        bank.sibilance = 0.72f;
        bank.stereoMode = AcapellaResonatorStereoMode::Mono;
        bank.stereoSpread = 0.0f;
        clearScene(bank.customMatrixA);
        clearScene(bank.customMatrixB);
        for (int32_t band = 0; band < 22; ++band) {
            addRoute(bank.customMatrixA, band, band, 1.0f);
            addRoute(bank.customMatrixA,
                band + (band % 2 == 0 ? 1 : -1), band, 0.055f);
            addRoute(bank.customMatrixB, band, band, 1.0f);
            addRoute(bank.customMatrixB, band + 2, band, 0.62f);
        }
        bank.customMatrixMorph = 0.0f;
        effects.compression = 0.44f;
        effects.deEss = 0.18f;
        effects.intelligibility = 0.94f;
        break;
    }
    case 25u: { // Impulse Matrix
        auto& bank = effects.resonator;
        configureMicMatrix(bank);
        bank.bandLayout = AcapellaResonatorBandLayout::Wide16;
        bank.analysisSlope = AcapellaResonatorAnalysisSlope::FourPole;
        bank.analysisWidth = 0.30f;
        bank.transferMode = AcapellaResonatorTransferMode::Expressive;
        bank.carrierPitchSource = AcapellaResonatorCarrierPitchSource::Midi;
        bank.carrierShape = AcapellaResonatorCarrierShape::Noise;
        bank.carrierHarmonics = 0.52f;
        bank.carrierNoise = 0.90f;
        bank.carrierDensity = 0.42f;
        bank.voicingMode = AcapellaResonatorVoicingMode::Noise;
        bank.attackMs = 0.5f;
        bank.releaseMs = 330.0f;
        bank.blurMs = 0.0f;
        bank.resonance = 0.92f;
        bank.driveDb = 1.0f;
        bank.sibilance = 0.32f;
        bank.customMatrixMorph = 0.0f;
        clearScene(bank.customMatrixA);
        clearScene(bank.customMatrixB);
        for (int32_t band = 0; band < 16; ++band) {
            addRoute(bank.customMatrixA, band, band, 1.0f);
            addRoute(bank.customMatrixB, band, band, 0.78f);
            addRoute(bank.customMatrixB,
                band + (band % 2 == 0 ? 2 : -2), band, 0.38f);
        }
        bank.stereoMode = AcapellaResonatorStereoMode::OddEven;
        bank.stereoSpread = 0.72f;
        effects.compression = 0.30f;
        effects.intelligibility = 0.62f;
        break;
    }
    case 26u: { // Gated Bank
        auto& bank = effects.resonator;
        configureMicMatrix(bank);
        bank.bandLayout = AcapellaResonatorBandLayout::Wide16;
        bank.analysisSlope = AcapellaResonatorAnalysisSlope::FourPole;
        bank.analysisWidth = 0.62f;
        bank.mode = AcapellaResonatorMode::FilterBank;
        bank.carrierPitchSource = AcapellaResonatorCarrierPitchSource::Midi;
        bank.carrierShape = AcapellaResonatorCarrierShape::Pulse;
        bank.pulseWidth = 0.40f;
        bank.carrierHarmonics = 0.94f;
        bank.carrierColor = 0.22f;
        bank.carrierNoise = 0.12f;
        bank.openLevel = 0.76f;
        bank.attackMs = 0.5f;
        bank.releaseMs = 38.0f;
        bank.blurMs = 1.0f;
        bank.resonance = 0.70f;
        bank.driveDb = 3.5f;
        bank.bandShiftSemitones = -2.0f;
        bank.bandStretch = 0.18f;
        bank.customMatrixMorph = 0.0f;
        clearScene(bank.customMatrixA);
        clearScene(bank.customMatrixB);
        for (int32_t band = 0; band < 16; ++band) {
            addRoute(bank.customMatrixA, band, band, 1.0f);
            addRoute(bank.customMatrixB, band, band, 1.0f);
            if (band % 3 == 0) {
                addRoute(bank.customMatrixB, band + 1, band, 0.34f);
            }
        }
        bank.stereoMode = AcapellaResonatorStereoMode::Spread;
        bank.stereoSpread = 0.48f;
        effects.compression = 0.44f;
        effects.intelligibility = 0.68f;
        break;
    }
    case 27u: { // Pulse Bank
        auto& bank = effects.resonator;
        bank.amount = 1.0f;
        bank.bandLayout = AcapellaResonatorBandLayout::Wide16;
        bank.mode = AcapellaResonatorMode::FilterBank;
        bank.modulatorSource =
            AcapellaResonatorModulatorSource::InternalSpeech;
        bank.carrierPitchSource = AcapellaResonatorCarrierPitchSource::Midi;
        bank.carrierShape = AcapellaResonatorCarrierShape::Pulse;
        bank.pulseWidth = 0.34f;
        bank.carrierHarmonics = 0.96f;
        bank.carrierColor = 0.20f;
        bank.carrierNoise = 0.08f;
        bank.carrierDensity = 0.48f;
        bank.carrierLfoShape = AcapellaResonatorCarrierLfoShape::Triangle;
        bank.carrierLfoRateHz = 0.34f;
        bank.carrierLfoDepthSemitones = 0.12f;
        bank.carrierLfoPwmDepth = 0.72f;
        bank.openLevel = 0.74f;
        bank.resonance = 0.68f;
        bank.driveDb = 3.0f;
        bank.bandStretch = 0.16f;
        bank.tilt = -0.10f;
        bank.matrixMode = AcapellaResonatorMatrixMode::Custom;
        bank.matrixMorph = 1.0f;
        bank.customMatrixMorph = 0.0f;
        clearScene(bank.customMatrixA);
        clearScene(bank.customMatrixB);
        for (int32_t band = 0; band < 16; ++band) {
            addRoute(bank.customMatrixA, band, band, 1.0f);
            addRoute(bank.customMatrixB, band, band, 0.82f);
            addRoute(bank.customMatrixB, band + 2, band, 0.30f);
        }
        bank.stereoMode = AcapellaResonatorStereoMode::OddEven;
        bank.stereoSpread = 0.56f;
        effects.compression = 0.38f;
        effects.intelligibility = 0.64f;
        break;
    }
    case 28u: { // Rhythm Transfer
        auto& bank = effects.resonator;
        configureMicMatrix(bank);
        bank.bandLayout = AcapellaResonatorBandLayout::Wide16;
        bank.analysisSlope = AcapellaResonatorAnalysisSlope::FourPole;
        bank.analysisWidth = 0.56f;
        bank.transferMode = AcapellaResonatorTransferMode::Expressive;
        bank.carrierPitchSource = AcapellaResonatorCarrierPitchSource::Midi;
        bank.carrierShape = AcapellaResonatorCarrierShape::Saw;
        bank.carrierHarmonics = 0.98f;
        bank.carrierColor = 0.28f;
        bank.carrierNoise = 0.18f;
        bank.carrierDensity = 0.62f;
        bank.attackMs = 0.5f;
        bank.releaseMs = 48.0f;
        bank.blurMs = 1.0f;
        bank.resonance = 0.66f;
        bank.driveDb = 3.0f;
        bank.customMatrixMorph = 0.0f;
        clearScene(bank.customMatrixA);
        clearScene(bank.customMatrixB);
        for (int32_t band = 0; band < 16; ++band) {
            addRoute(bank.customMatrixA, band, band, 1.0f);
            addRoute(bank.customMatrixB, band, band, 0.72f);
            addRoute(bank.customMatrixB, band + 1, band, 0.42f);
        }
        bank.stereoMode = AcapellaResonatorStereoMode::Spread;
        bank.stereoSpread = 0.52f;
        effects.compression = 0.48f;
        effects.intelligibility = 0.72f;
        break;
    }
    case 29u: { // Shift Morph
        auto& bank = effects.resonator;
        configureMicMatrix(bank);
        bank.carrierPitchSource = AcapellaResonatorCarrierPitchSource::Midi;
        bank.carrierShape = AcapellaResonatorCarrierShape::Saw;
        bank.carrierHarmonics = 0.96f;
        bank.carrierColor = 0.34f;
        bank.carrierNoise = 0.10f;
        bank.resonance = 0.72f;
        bank.driveDb = 2.0f;
        bank.customMatrixMorph = 0.50f;
        shiftedScene(bank.customMatrixA, -3, 0.14f);
        shiftedScene(bank.customMatrixB, 4, 0.14f);
        bank.stereoMode = AcapellaResonatorStereoMode::OddEven;
        bank.stereoSpread = 0.62f;
        effects.compression = 0.40f;
        effects.intelligibility = 0.82f;
        break;
    }
    case 30u: { // Spectral Drone
        auto& bank = effects.resonator;
        bank.amount = 1.0f;
        bank.bandLayout = AcapellaResonatorBandLayout::Wide16;
        bank.mode = AcapellaResonatorMode::FilterBank;
        bank.modulatorSource =
            AcapellaResonatorModulatorSource::InternalSpeech;
        bank.carrierPitchSource = AcapellaResonatorCarrierPitchSource::Midi;
        bank.carrierShape = AcapellaResonatorCarrierShape::Saw;
        bank.carrierHarmonics = 0.98f;
        bank.carrierColor = 0.40f;
        bank.carrierNoise = 0.34f;
        bank.carrierDensity = 0.70f;
        bank.carrierLfoShape = AcapellaResonatorCarrierLfoShape::Triangle;
        bank.carrierLfoRateHz = 0.11f;
        bank.carrierLfoDepthSemitones = 0.26f;
        bank.carrierLfoPwmDepth = 0.18f;
        bank.openLevel = 0.66f;
        bank.resonance = 0.82f;
        bank.driveDb = 5.0f;
        bank.bandShiftSemitones = -4.0f;
        bank.bandStretch = 0.42f;
        bank.tilt = -0.16f;
        bank.matrixMode = AcapellaResonatorMatrixMode::Custom;
        bank.matrixMorph = 1.0f;
        bank.customMatrixMorph = 0.46f;
        clearScene(bank.customMatrixA);
        clearScene(bank.customMatrixB);
        for (int32_t source = 0; source < 16; ++source) {
            if (source % 3 != 1) {
                addRoute(bank.customMatrixA, source, source, 1.0f);
                addRoute(bank.customMatrixA, source + 3, source, 0.34f);
            }
            if (source % 4 != 2) {
                addRoute(bank.customMatrixB, 15 - source, source, 0.72f);
                addRoute(bank.customMatrixB, source - 2, source, 0.28f);
            }
        }
        bank.stereoMode = AcapellaResonatorStereoMode::Spread;
        bank.stereoSpread = 0.92f;
        effects.compression = 0.46f;
        effects.echoHeads = DrumEchoHeadMode::Head13;
        effects.echoClock = DrumEchoClock::Quarter;
        effects.echoFeedback = 0.32f;
        effects.echoWear = 0.26f;
        effects.echoFlutter = 0.18f;
        effects.echoSpread = 0.80f;
        effects.echoMix = 0.16f;
        effects.intelligibility = 0.58f;
        break;
    }
    default:
        break;
    }
    // Definition is one macro across the procedural articulation, measured
    // filter-bank analysis, and post-shape preserve rail. Keep direct DSP
    // profile users identical to the CLAP parameter bridge.
    effects.resonator.definition = effects.intelligibility;
    return sanitizeAcapellaVocalFxParams(effects);
}

// A bounded, allocation-free post chain tailored to the synthesized vocal.
// The octave generators are waveform operations, not pitch-shifted samples:
// full-wave rectification supplies octave-up energy, while a hysteretic
// period divider supplies octave-down energy.
class AcapellaVocalEffects {
public:
    void prepare(double sampleRate)
    {
        sampleRate_ = std::isfinite(sampleRate)
            ? clamp(static_cast<float>(sampleRate), 8000.0f, 192000.0f)
            : 48000.0f;
        params_ = sanitizeAcapellaVocalFxParams(params_);
        smoothingCoefficient_ = coefficient(18.0f);
        pitchLowCoefficient_ = lowpassCoefficient(720.0f);
        subLowCoefficient_ = lowpassCoefficient(520.0f);
        rectifiedDcCoefficient_ = lowpassCoefficient(24.0f);
        deEssCoefficient_ = lowpassCoefficient(4800.0f);
        crushLowCoefficient_ = lowpassCoefficient(260.0f);
        crushHighCoefficient_ = lowpassCoefficient(3200.0f);
        crushAttackCoefficient_ = coefficient(0.8f);
        crushReleaseCoefficient_ = coefficient(70.0f);
        crushGainAttackCoefficient_ = coefficient(1.0f);
        crushGainReleaseCoefficient_ = coefficient(85.0f);
        subAttackCoefficient_ = coefficient(2.0f);
        subReleaseCoefficient_ = coefficient(45.0f);
        fastAttackCoefficient_ = coefficient(0.7f);
        fastReleaseCoefficient_ = coefficient(48.0f);
        fastGainAttackCoefficient_ = coefficient(1.0f);
        slowAttackCoefficient_ = coefficient(11.0f);
        slowReleaseCoefficient_ = coefficient(180.0f);
        slowGainAttackCoefficient_ = coefficient(1.0f);
        deEssAttackCoefficient_ = coefficient(0.6f);
        deEssReleaseCoefficient_ = coefficient(55.0f);
        deEssGainAttackCoefficient_ = coefficient(0.8f);
        deEssGainReleaseCoefficient_ = coefficient(65.0f);
        limiterReleaseCoefficient_ = coefficient(70.0f);
        activityReleaseCoefficient_ = coefficient(180.0f);
        resonatorBank_.setParams(params_.resonator);
        (void)resonatorBank_.prepare(sampleRate_);
        tapeEcho_.prepare(sampleRate_, 12.0);
        reset();
    }

    void reset()
    {
        smoothed_ = params_;
        previousInput_ = 0.0f;
        pitchLow1_ = 0.0f;
        pitchLow2_ = 0.0f;
        pitchDividerArmed_ = false;
        subPolarity_ = 1.0f;
        subEnvelope_ = 0.0f;
        subLow_ = 0.0f;
        rectifiedDc_ = 0.0f;
        fuzzTone_ = 0.0f;
        fuzzDcInput_ = 0.0f;
        fuzzDcOutput_ = 0.0f;
        resonatorBank_.setParams(params_.resonator);
        resonatorBank_.reset();
        resonatorOutputSide_ = 0.0f;
        fastEnvelope_ = 0.0f;
        fastGain_ = 1.0f;
        slowEnvelope_ = 0.0f;
        slowGain_ = 1.0f;
        crushLow_ = 0.0f;
        crushHighLow_ = 0.0f;
        crushEnvelopes_.fill(0.0f);
        crushGains_.fill(1.0f);
        deEssLow_ = 0.0f;
        deEssEnvelope_ = 0.0f;
        deEssGain_ = 1.0f;
        updateTapeEchoParams();
        tapeEcho_.reset();
        stereoInputSide_ = 0.0f;
        externalCarrierLeft_ = 0.0f;
        externalCarrierRight_ = 0.0f;
        separateCarrierInput_ = false;
        externalCarrierAvailable_ = false;
        targetMicInputGain_ = dbToGain(params_.resonator.micGainDb);
        // Reset has no host-buffer availability information. Initialize from
        // the same safe no-input topology used by an idle source change;
        // Blend will fade a connected mic toward its normalized gain instead
        // of beginning 3 dB hot on the first frame.
        snapModulatorRouting();
        allpassInput_ = 0.0f;
        allpassOutput_ = 0.0f;
        limiterGain_ = 1.0f;
        previousOutputLeft_ = 0.0f;
        previousOutputRight_ = 0.0f;
        activityEnvelope_ = 0.0f;
        coefficientCounter_ = 0u;
        updateToneCoefficient();
    }

    void setParams(AcapellaVocalFxParams params)
    {
        const auto previousModulatorSource
            = params_.resonator.modulatorSource;
        params_ = sanitizeAcapellaVocalFxParams(params);
        targetMicInputGain_ = dbToGain(params_.resonator.micGainDb);
        resonatorBank_.setParams(params_.resonator);
        if (previousModulatorSource != params_.resonator.modulatorSource
            && !resonatorBank_.active()) {
            snapModulatorRouting();
        }
    }

    void setRealtimeControlParams(const AcapellaVocalFxParams& next)
    {
        const auto previousModulatorSource
            = params_.resonator.modulatorSource;
        params_.octaveDown = next.octaveDown;
        params_.octaveUp = next.octaveUp;
        params_.fuzzDriveDb = next.fuzzDriveDb;
        params_.fuzzMix = next.fuzzMix;
        params_.fuzzToneHz = next.fuzzToneHz;
        params_.compression = next.compression;
        params_.parallelCrush = next.parallelCrush;
        params_.deEss = next.deEss;
        params_.echoHeads = next.echoHeads;
        params_.echoClock = next.echoClock;
        params_.echoTimeMs = next.echoTimeMs;
        params_.echoFeedback = next.echoFeedback;
        params_.echoWear = next.echoWear;
        params_.echoFlutter = next.echoFlutter;
        params_.echoTone = next.echoTone;
        params_.echoSpread = next.echoSpread;
        params_.echoMix = next.echoMix;
        params_.width = next.width;
        params_.intelligibility = next.intelligibility;
        copyAcapellaResonatorControlParams(
            params_.resonator, next.resonator);
        targetMicInputGain_ = dbToGain(params_.resonator.micGainDb);
        resonatorBank_.setRealtimeControlParams(next.resonator);
        if (previousModulatorSource != params_.resonator.modulatorSource
            && !resonatorBank_.active()) {
            snapModulatorRouting();
        }
    }

    void setResonatorGesture(AcapellaResonatorGesture gesture)
    {
        resonatorBank_.setGesture(gesture);
    }

    void setResonatorBandTrim(uint32_t band, float value)
    {
        if (band >= kAcapellaResonatorBands) return;
        params_.resonator.bandTrims[band] = clamp(
            acapellaFiniteOr(value, 1.0f), 0.0f, 2.0f);
        resonatorBank_.setBandTrim(band, value);
    }

    void setResonatorCustomMatrixCell(bool sceneB, uint32_t destination,
        uint32_t source, float value)
    {
        if (destination >= kAcapellaResonatorBands
            || source >= kAcapellaResonatorBands) return;
        auto& scene = sceneB ? params_.resonator.customMatrixB
                             : params_.resonator.customMatrixA;
        scene[destination * kAcapellaResonatorBands + source] = clamp(
            acapellaFiniteOr(value, 0.0f), -1.0f, 1.0f);
        resonatorBank_.setCustomMatrixCell(
            sceneB, destination, source, value);
    }

    AcapellaResonatorMeterSnapshot resonatorMeterSnapshot() const
    {
        return resonatorBank_.meterSnapshot();
    }

    void setTempo(double beatsPerMinute, bool valid = true)
    {
        resonatorBank_.setTempo(beatsPerMinute, valid);
        tapeEcho_.setTempo(beatsPerMinute, valid);
    }

    uint32_t latencySamples() const { return resonatorBank_.latencySamples(); }

    uint32_t tailSamples() const
    {
        const uint64_t resonator = resonatorBank_.tailSamples();
        const uint64_t echo = (params_.echoMix > 1.0e-4f
                || smoothed_.echoMix > 1.0e-4f)
            ? tapeEcho_.tailSamplesForParams(tapeEchoParams(params_)) : 0u;
        return static_cast<uint32_t>(std::min<uint64_t>(
            0xfffffffeu, resonator + echo));
    }

    const AcapellaVocalFxParams& params() const { return params_; }
    bool active() const
    {
        return resonatorBank_.active()
            || (smoothed_.echoMix > 1.0e-4f
                && activityEnvelope_ > 1.0e-4f);
    }

    AcapellaStereoFrame processFrame(float input)
    {
        input = std::isfinite(input) ? clamp(input, -2.0f, 2.0f) : 0.0f;
        // The legacy self-carrier path shapes its input before the bank. The
        // v5 mic/speech-modulator path uses the bank's MIDI oscillator and
        // applies the same octave/fuzz stage after the bank so these exposed
        // controls remain musically active.
        const float carrierInput = separateCarrierInput_
            ? 0.5f * (externalCarrierLeft_ + externalCarrierRight_)
            : input;
        const float carrierSide = separateCarrierInput_
            ? 0.5f * (externalCarrierLeft_ - externalCarrierRight_)
            : stereoInputSide_;
        smoothParams();
        if ((coefficientCounter_++ & 15u) == 0u) {
            updateToneCoefficient();
            updateTapeEchoParams();
        }

        float signal = separateCarrierInput_ ? 0.0f
                                             : shapeSignal(carrierInput);

        // In the dedicated v5 route, `input` is the selected external-mic /
        // internal-speech modulator and the bank's stable-ID oscillator set is
        // always the carrier. The legacy route remains a self-carrier API for
        // standalone DSP users and older source-engine tests.
        // The legacy one-/two-channel API is a self-carrier path: its input
        // feeds both analysis and carrier processing.  The explicit v4 API
        // keeps an absent host carrier distinct so the procedural carrier can
        // take over.  Track the routing form separately from buffer
        // availability; otherwise an explicitly absent carrier accidentally
        // falls back to the analysis voice, while the legacy path goes mute.
        const bool bankCarrierAvailable = separateCarrierInput_
            ? externalCarrierAvailable_ : true;
        const float bankCarrierLeft = bankCarrierAvailable
            ? signal + carrierSide : 0.0f;
        const float bankCarrierRight = bankCarrierAvailable
            ? signal - carrierSide : 0.0f;
        const auto resonated = resonatorBank_.processFrameStereo(
            input + stereoInputSide_, input - stereoInputSide_,
            bankCarrierLeft, bankCarrierRight,
            bankCarrierAvailable);
        const float bankOutputMid
            = 0.5f * (resonated.left + resonated.right);
        const float bankOutputSide
            = 0.5f * (resonated.left - resonated.right);
        signal = bankOutputMid;
        resonatorOutputSide_ = bankOutputSide;
        if (separateCarrierInput_) signal = shapeSignal(signal);
        // In the dedicated vocoder route the intelligibility rail must remain
        // inside the analyzed/VCA-controlled bank. Pulling the dry oscillator
        // back in here makes a silent microphone audible whenever a
        // destructive shape effect is enabled. The legacy self-carrier API
        // retains its original dry-input preservation behavior.
        const float cleanInput = separateCarrierInput_
            ? bankOutputMid
            : 0.5f * (resonated.dryLeft + resonated.dryRight);
        const float cleanInputSide = separateCarrierInput_
            ? bankOutputSide
            : 0.5f * (resonated.dryLeft - resonated.dryRight);

        const float linkedDynamicsLevel = std::max(
            std::abs(signal), std::abs(resonatorOutputSide_));
        signal = compressorStage(signal, linkedDynamicsLevel,
            fastEnvelope_, fastGain_,
            -18.0f, 8.0f, fastAttackCoefficient_, fastReleaseCoefficient_,
            fastGainAttackCoefficient_, fastReleaseCoefficient_,
            smoothed_.compression);
        signal = compressorStage(signal, linkedDynamicsLevel,
            slowEnvelope_, slowGain_,
            -14.0f, 3.2f, slowAttackCoefficient_, slowReleaseCoefficient_,
            slowGainAttackCoefficient_, slowReleaseCoefficient_,
            smoothed_.compression * 0.78f);
        const float compressorMakeup = std::exp2(
            smoothed_.compression * 4.5f / 6.020599913f);
        signal *= compressorMakeup;
        // Use the same linked gain detector for mid and side. Letting a wide
        // bank side rail skip dynamics reintroduced harsh consonant splats.
        resonatorOutputSide_ *= fastGain_ * slowGain_ * compressorMakeup;

        crushLow_ += crushLowCoefficient_ * (signal - crushLow_);
        crushHighLow_ += crushHighCoefficient_ * (signal - crushHighLow_);
        const float lowBand = crushLow_;
        const float midBand = crushHighLow_ - crushLow_;
        const float highBand = signal - crushHighLow_;
        const float crushed = crushBand(lowBand, 0u, 0.085f, 4.0f, 0.48f)
            + crushBand(midBand, 1u, 0.055f, 5.5f, 0.38f)
            + crushBand(highBand, 2u, 0.030f, 7.0f, 0.24f);
        const float parallel = signal * 0.60f + crushed;
        signal = lerp(signal, parallel, smoothed_.parallelCrush);
        const float sideCrushed = std::tanh(resonatorOutputSide_ * 4.2f)
            * 0.32f;
        resonatorOutputSide_ = lerp(resonatorOutputSide_, sideCrushed,
            smoothed_.parallelCrush);

        deEssLow_ += deEssCoefficient_ * (signal - deEssLow_);
        const float deEssHigh = signal - deEssLow_;
        const float highLevel = std::max(
            std::abs(deEssHigh), std::abs(resonatorOutputSide_));
        deEssEnvelope_ += (highLevel - deEssEnvelope_)
            * (highLevel > deEssEnvelope_ ? deEssAttackCoefficient_
                                          : deEssReleaseCoefficient_);
        const float deEssThreshold = 0.035f;
        const float desiredDeEss = deEssEnvelope_ > deEssThreshold
            ? clamp(deEssThreshold / deEssEnvelope_, 0.16f, 1.0f) : 1.0f;
        const float targetDeEss = lerp(1.0f, desiredDeEss, smoothed_.deEss);
        deEssGain_ += (targetDeEss - deEssGain_)
            * (targetDeEss < deEssGain_ ? deEssGainAttackCoefficient_
                                        : deEssGainReleaseCoefficient_);
        signal = deEssLow_ + deEssHigh * deEssGain_;
        resonatorOutputSide_ *= deEssGain_;

        // Keep a phase-aligned clean articulation rail under nonlinear
        // waveshaping. The bank already integrates consonant energy into its
        // upper resonators, so this rail only compensates destructive legacy
        // effects and never scales with the resonator amount.
        const float destructiveAmount = std::max({
            smoothed_.fuzzMix,
            smoothed_.parallelCrush * 0.82f,
            (smoothed_.octaveDown + smoothed_.octaveUp) * 0.42f,
        });
        const float cleanPreserve = smoothed_.intelligibility
            * destructiveAmount * 0.36f;
        signal = lerp(signal, cleanInput, cleanPreserve);
        resonatorOutputSide_ = lerp(resonatorOutputSide_, cleanInputSide,
            cleanPreserve);

        constexpr float allpassCoefficient = 0.71f;
        const float decorrelated = -allpassCoefficient * signal
            + allpassInput_ + allpassCoefficient * allpassOutput_;
        allpassInput_ = signal;
        allpassOutput_ = flushDenormal(decorrelated);
        const float side = (decorrelated - signal)
            * smoothed_.width * 0.34f;
        float left = signal + side + resonatorOutputSide_;
        float right = signal - side - resonatorOutputSide_;
        tapeEcho_.processFrame(left, right);

        const float peak = std::max(std::abs(left), std::abs(right));
        const float desiredLimiter = peak > 0.94f ? 0.94f / peak : 1.0f;
        if (desiredLimiter < limiterGain_) {
            limiterGain_ = desiredLimiter;
        } else {
            limiterGain_ += (1.0f - limiterGain_)
                * limiterReleaseCoefficient_;
        }
        left = clamp(left * limiterGain_, -0.98f, 0.98f);
        right = clamp(right * limiterGain_, -0.98f, 0.98f);
        constexpr float maximumFrameStep = 0.18f;
        left = previousOutputLeft_ + clamp(left - previousOutputLeft_,
            -maximumFrameStep, maximumFrameStep);
        right = previousOutputRight_ + clamp(right - previousOutputRight_,
            -maximumFrameStep, maximumFrameStep);
        previousOutputLeft_ = left;
        previousOutputRight_ = right;
        if (!std::isfinite(left) || !std::isfinite(right)) {
            reset();
            return {};
        }
        const float outputActivity = std::max(std::abs(left), std::abs(right));
        if (outputActivity > activityEnvelope_) {
            activityEnvelope_ = outputActivity;
        } else {
            activityEnvelope_ += (outputActivity - activityEnvelope_)
                * activityReleaseCoefficient_;
        }
        return { left, right };
    }

    AcapellaStereoFrame processFrameStereo(float leftInput, float rightInput)
    {
        leftInput = std::isfinite(leftInput) ? leftInput : 0.0f;
        rightInput = std::isfinite(rightInput) ? rightInput : 0.0f;
        const float mid = 0.5f * (leftInput + rightInput);
        const float side = 0.5f * (leftInput - rightInput) * 0.82f;
        stereoInputSide_ = side;
        auto output = processFrame(mid);
        stereoInputSide_ = 0.0f;
        const float peak = std::max(std::abs(output.left),
            std::abs(output.right));
        if (peak > 0.98f) {
            const float gain = 0.98f / peak;
            output.left *= gain;
            output.right *= gain;
        }
        return output;
    }

    // Dedicated v5 route. The first stereo pair is procedurally synthesized
    // speech, the second is the host microphone, and the bank's stable-ID MIDI
    // oscillators are the carrier. Source selection is explicit: External Mic
    // never starts the internal voice during a pause, while Blend falls back
    // to full internal speech only when no host input buffer exists.
    AcapellaStereoFrame processFrameStereo(float internalSpeechLeft,
        float internalSpeechRight, float micLeft, float micRight)
    {
        return processFrameStereo(internalSpeechLeft, internalSpeechRight,
            micLeft, micRight, true);
    }

    AcapellaStereoFrame processFrameStereo(float internalSpeechLeft,
        float internalSpeechRight, float micLeft, float micRight,
        bool micAvailable)
    {
        internalSpeechLeft = std::isfinite(internalSpeechLeft)
            ? clamp(internalSpeechLeft, -4.0f, 4.0f) : 0.0f;
        internalSpeechRight = std::isfinite(internalSpeechRight)
            ? clamp(internalSpeechRight, -4.0f, 4.0f) : 0.0f;
        micLeft = std::isfinite(micLeft) ? clamp(micLeft, -4.0f, 4.0f)
                                         : 0.0f;
        micRight = std::isfinite(micRight) ? clamp(micRight, -4.0f, 4.0f)
                                           : 0.0f;

        const auto source = params_.resonator.modulatorSource;
        float speechTarget = source == AcapellaResonatorModulatorSource::ExternalMic
            ? 0.0f : 1.0f;
        float micTarget = source == AcapellaResonatorModulatorSource::InternalSpeech
            ? 0.0f : 1.0f;
        if (source == AcapellaResonatorModulatorSource::Blend && micAvailable) {
            constexpr float normalizedBlendGain = 0.70710678118f;
            speechTarget = normalizedBlendGain;
            micTarget = normalizedBlendGain;
        } else if (!micAvailable) {
            micTarget = 0.0f;
            if (source == AcapellaResonatorModulatorSource::Blend) {
                speechTarget = 1.0f;
            }
        }
        smoothedInternalSpeechGain_ += (speechTarget
            - smoothedInternalSpeechGain_) * smoothingCoefficient_;
        smoothedMicGain_ += (micTarget - smoothedMicGain_)
            * smoothingCoefficient_;
        smoothedMicInputGain_ += (targetMicInputGain_
            - smoothedMicInputGain_)
            * smoothingCoefficient_;
        const float selectedLeft = internalSpeechLeft
                * smoothedInternalSpeechGain_
            + micLeft * smoothedMicGain_ * smoothedMicInputGain_;
        const float selectedRight = internalSpeechRight
                * smoothedInternalSpeechGain_
            + micRight * smoothedMicGain_ * smoothedMicInputGain_;

        // A false carrier-availability flag forces the bank to use only its
        // internal stable-ID oscillator set. No mic waveform can leak onto the
        // carrier rail.
        externalCarrierLeft_ = 0.0f;
        externalCarrierRight_ = 0.0f;
        separateCarrierInput_ = true;
        externalCarrierAvailable_ = false;
        const float mid = 0.5f * (selectedLeft + selectedRight);
        stereoInputSide_ = 0.5f * (selectedLeft - selectedRight) * 0.82f;
        auto output = processFrame(mid);
        stereoInputSide_ = 0.0f;
        separateCarrierInput_ = false;
        externalCarrierAvailable_ = false;
        const float peak = std::max(std::abs(output.left),
            std::abs(output.right));
        if (peak > 0.98f) {
            const float gain = 0.98f / peak;
            output.left *= gain;
            output.right *= gain;
        }
        return output;
    }

private:

    void snapModulatorRouting()
    {
        switch (params_.resonator.modulatorSource) {
        case AcapellaResonatorModulatorSource::ExternalMic:
            smoothedInternalSpeechGain_ = 0.0f;
            smoothedMicGain_ = 1.0f;
            break;
        case AcapellaResonatorModulatorSource::Blend:
            // Buffer availability is a per-process fact. Start from the
            // documented no-input fallback and let the click-safe smoother
            // introduce a connected microphone on the audio path.
            smoothedInternalSpeechGain_ = 1.0f;
            smoothedMicGain_ = 0.0f;
            break;
        case AcapellaResonatorModulatorSource::InternalSpeech:
        default:
            smoothedInternalSpeechGain_ = 1.0f;
            smoothedMicGain_ = 0.0f;
            break;
        }
        smoothedMicInputGain_ = targetMicInputGain_;
    }

    float coefficient(float milliseconds) const
    {
        const float samples = std::max(1.0f,
            milliseconds * 0.001f * sampleRate_);
        return 1.0f - std::exp(-1.0f / samples);
    }

    float lowpassCoefficient(float frequencyHz) const
    {
        return 1.0f - std::exp(-2.0f * kPi
            * clamp(frequencyHz, 10.0f, sampleRate_ * 0.45f) / sampleRate_);
    }

    static float fuzzShape(float input, float drive)
    {
        const float bias = 0.055f;
        return (std::tanh((input + bias) * drive)
            - std::tanh(bias * drive)) * 0.72f;
    }

    float shapeSignal(float input)
    {
        pitchLow1_ += pitchLowCoefficient_ * (input - pitchLow1_);
        pitchLow2_ += pitchLowCoefficient_ * (pitchLow1_ - pitchLow2_);
        constexpr float hysteresis = 0.0025f;
        if (pitchLow2_ <= -hysteresis) pitchDividerArmed_ = true;
        if (pitchDividerArmed_ && pitchLow2_ >= hysteresis) {
            subPolarity_ = -subPolarity_;
            pitchDividerArmed_ = false;
        }
        const float absoluteInput = std::abs(input);
        const float subEnvelopeCoefficient = absoluteInput > subEnvelope_
            ? subAttackCoefficient_ : subReleaseCoefficient_;
        subEnvelope_ += (absoluteInput - subEnvelope_)
            * subEnvelopeCoefficient;
        subLow_ += subLowCoefficient_
            * (subPolarity_ * subEnvelope_ - subLow_);

        const float rectified = std::abs(input);
        rectifiedDc_ += rectifiedDcCoefficient_
            * (rectified - rectifiedDc_);
        const float octaveUp = (rectified - rectifiedDc_) * 1.55f;
        const float octaveNormalization = 1.0f
            + 0.28f * (smoothed_.octaveDown + smoothed_.octaveUp);
        const float octaveSignal = (input
            + subLow_ * smoothed_.octaveDown * 0.72f
            + octaveUp * smoothed_.octaveUp * 0.58f)
            / octaveNormalization;

        const float drive = std::exp2(
            smoothed_.fuzzDriveDb / 6.020599913f);
        const float midpoint = 0.5f * (previousInput_ + octaveSignal);
        const float fuzz = 0.5f * (fuzzShape(midpoint, drive)
            + fuzzShape(octaveSignal, drive));
        previousInput_ = octaveSignal;
        fuzzTone_ += fuzzToneCoefficient_ * (fuzz - fuzzTone_);
        const float fuzzDc = fuzzTone_ - fuzzDcInput_
            + 0.997f * fuzzDcOutput_;
        fuzzDcInput_ = fuzzTone_;
        fuzzDcOutput_ = flushDenormal(fuzzDc);
        return lerp(octaveSignal, fuzzDcOutput_, smoothed_.fuzzMix);
    }

    float compressorStage(float input, float detectorLevel,
        float& envelope, float& gain,
        float thresholdDb, float ratio, float envelopeAttackCoefficient,
        float envelopeReleaseCoefficient, float gainAttackCoefficient,
        float gainReleaseCoefficient, float amount)
    {
        const float level = std::max(std::abs(input), detectorLevel);
        envelope += (level - envelope)
            * (level > envelope ? envelopeAttackCoefficient
                                : envelopeReleaseCoefficient);
        const float levelDb = 20.0f * std::log10(std::max(envelope, 1.0e-8f));
        const float compressedDb = levelDb > thresholdDb
            ? thresholdDb + (levelDb - thresholdDb) / ratio : levelDb;
        const float desiredGain = std::exp2(
            (compressedDb - levelDb) * amount / 6.020599913f);
        gain += (desiredGain - gain)
            * (desiredGain < gain ? gainAttackCoefficient
                                  : gainReleaseCoefficient);
        return input * gain;
    }

    float crushBand(float input, uint32_t index, float threshold,
        float drive, float level)
    {
        const float magnitude = std::abs(input);
        crushEnvelopes_[index] += (magnitude - crushEnvelopes_[index])
            * (magnitude > crushEnvelopes_[index] ? crushAttackCoefficient_
                                                  : crushReleaseCoefficient_);
        const float desired = crushEnvelopes_[index] > threshold
            ? (threshold + (crushEnvelopes_[index] - threshold) / 9.0f)
                / crushEnvelopes_[index]
            : 1.0f;
        crushGains_[index] += (desired - crushGains_[index])
            * (desired < crushGains_[index] ? crushGainAttackCoefficient_
                                            : crushGainReleaseCoefficient_);
        return std::tanh(input * crushGains_[index] * drive * 1.6f) * level;
    }

    void smoothParams()
    {
        smoothed_.octaveDown += (params_.octaveDown - smoothed_.octaveDown)
            * smoothingCoefficient_;
        smoothed_.octaveUp += (params_.octaveUp - smoothed_.octaveUp)
            * smoothingCoefficient_;
        smoothed_.fuzzDriveDb += (params_.fuzzDriveDb - smoothed_.fuzzDriveDb)
            * smoothingCoefficient_;
        smoothed_.fuzzMix += (params_.fuzzMix - smoothed_.fuzzMix)
            * smoothingCoefficient_;
        smoothed_.fuzzToneHz += (params_.fuzzToneHz - smoothed_.fuzzToneHz)
            * smoothingCoefficient_;
        smoothed_.compression += (params_.compression - smoothed_.compression)
            * smoothingCoefficient_;
        smoothed_.parallelCrush += (params_.parallelCrush
            - smoothed_.parallelCrush) * smoothingCoefficient_;
        smoothed_.deEss += (params_.deEss - smoothed_.deEss)
            * smoothingCoefficient_;
        smoothed_.echoHeads = params_.echoHeads;
        smoothed_.echoClock = params_.echoClock;
        smoothed_.echoTimeMs += (params_.echoTimeMs - smoothed_.echoTimeMs)
            * smoothingCoefficient_;
        smoothed_.echoFeedback += (params_.echoFeedback
            - smoothed_.echoFeedback) * smoothingCoefficient_;
        smoothed_.echoWear += (params_.echoWear - smoothed_.echoWear)
            * smoothingCoefficient_;
        smoothed_.echoFlutter += (params_.echoFlutter
            - smoothed_.echoFlutter) * smoothingCoefficient_;
        smoothed_.echoTone += (params_.echoTone - smoothed_.echoTone)
            * smoothingCoefficient_;
        smoothed_.echoSpread += (params_.echoSpread - smoothed_.echoSpread)
            * smoothingCoefficient_;
        smoothed_.echoMix += (params_.echoMix - smoothed_.echoMix)
            * smoothingCoefficient_;
        smoothed_.width += (params_.width - smoothed_.width)
            * smoothingCoefficient_;
        smoothed_.intelligibility += (params_.intelligibility
            - smoothed_.intelligibility) * smoothingCoefficient_;
    }

    void updateToneCoefficient()
    {
        fuzzToneCoefficient_ = lowpassCoefficient(smoothed_.fuzzToneHz);
    }

    void updateTapeEchoParams()
    {
        tapeEcho_.setParams(tapeEchoParams(smoothed_));
    }

    static DrumEchoParams tapeEchoParams(
        const AcapellaVocalFxParams& source)
    {
        DrumEchoParams echo;
        echo.headMode = source.echoHeads;
        echo.clock = source.echoClock;
        echo.timeMs = source.echoTimeMs;
        echo.feedback = source.echoFeedback;
        echo.wear = source.echoWear;
        echo.flutter = source.echoFlutter;
        echo.transient = 0.0f;
        echo.sensitivity = 0.0f;
        echo.duck = 0.0f;
        echo.tone = source.echoTone;
        echo.spread = source.echoSpread;
        echo.mix = source.echoMix;
        echo.outputGainDb = 0.0f;
        echo.bypass = false;
        return echo;
    }

    float sampleRate_ = 48000.0f;
    AcapellaVocalFxParams params_ {};
    AcapellaVocalFxParams smoothed_ {};
    float smoothingCoefficient_ = 0.001f;
    float pitchLowCoefficient_ = 0.08f;
    float subLowCoefficient_ = 0.06f;
    float rectifiedDcCoefficient_ = 0.003f;
    float deEssCoefficient_ = 0.4f;
    float crushLowCoefficient_ = 0.03f;
    float crushHighCoefficient_ = 0.3f;
    float fuzzToneCoefficient_ = 0.5f;
    float subAttackCoefficient_ = 0.01f;
    float subReleaseCoefficient_ = 0.001f;
    float fastAttackCoefficient_ = 0.02f;
    float fastReleaseCoefficient_ = 0.001f;
    float fastGainAttackCoefficient_ = 0.02f;
    float slowAttackCoefficient_ = 0.002f;
    float slowReleaseCoefficient_ = 0.0001f;
    float slowGainAttackCoefficient_ = 0.02f;
    float crushAttackCoefficient_ = 0.02f;
    float crushReleaseCoefficient_ = 0.0003f;
    float crushGainAttackCoefficient_ = 0.02f;
    float crushGainReleaseCoefficient_ = 0.0002f;
    float deEssAttackCoefficient_ = 0.03f;
    float deEssReleaseCoefficient_ = 0.0004f;
    float deEssGainAttackCoefficient_ = 0.02f;
    float deEssGainReleaseCoefficient_ = 0.0003f;
    float limiterReleaseCoefficient_ = 0.0003f;
    float activityReleaseCoefficient_ = 0.00004f;
    float previousInput_ = 0.0f;
    float pitchLow1_ = 0.0f;
    float pitchLow2_ = 0.0f;
    bool pitchDividerArmed_ = false;
    float subPolarity_ = 1.0f;
    float subEnvelope_ = 0.0f;
    float subLow_ = 0.0f;
    float rectifiedDc_ = 0.0f;
    float fuzzTone_ = 0.0f;
    float fuzzDcInput_ = 0.0f;
    float fuzzDcOutput_ = 0.0f;
    AcapellaResonatorBank resonatorBank_ {};
    float resonatorOutputSide_ = 0.0f;
    float fastEnvelope_ = 0.0f;
    float fastGain_ = 1.0f;
    float slowEnvelope_ = 0.0f;
    float slowGain_ = 1.0f;
    float crushLow_ = 0.0f;
    float crushHighLow_ = 0.0f;
    std::array<float, 3u> crushEnvelopes_ {};
    std::array<float, 3u> crushGains_ { 1.0f, 1.0f, 1.0f };
    float deEssLow_ = 0.0f;
    float deEssEnvelope_ = 0.0f;
    float deEssGain_ = 1.0f;
    DrumEcho tapeEcho_ {};
    float stereoInputSide_ = 0.0f;
    float externalCarrierLeft_ = 0.0f;
    float externalCarrierRight_ = 0.0f;
    bool separateCarrierInput_ = false;
    bool externalCarrierAvailable_ = false;
    float smoothedInternalSpeechGain_ = 1.0f;
    float smoothedMicGain_ = 0.0f;
    float smoothedMicInputGain_ = 1.0f;
    float targetMicInputGain_ = 1.0f;
    float allpassInput_ = 0.0f;
    float allpassOutput_ = 0.0f;
    float limiterGain_ = 1.0f;
    float previousOutputLeft_ = 0.0f;
    float previousOutputRight_ = 0.0f;
    float activityEnvelope_ = 0.0f;
    uint32_t coefficientCounter_ = 0u;
};

} // namespace s3g
