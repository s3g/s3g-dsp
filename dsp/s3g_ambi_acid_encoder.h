#pragma once

#include "s3g_analog_drive_circuits.h"
#include "s3g_ambi_field_listener.h"
#include "s3g_ambisonic_geometry.h"
#include "s3g_math.h"
#include "s3g_musical_scales.h"
#include "s3g_realtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace s3g {

constexpr uint32_t kAmbiAcidChannels = 16u;
constexpr uint32_t kAmbiAcidStepCount = 16u;
constexpr uint32_t kAmbiAcidWakePointCount = 4u;
constexpr uint32_t kAmbiAcidWakeBufferSamples = 32768u;

struct AmbiAcidStep {
    int32_t semitoneOffset = 0;
    bool gate = true;
    bool accent = false;
    bool slide = false;
};

// Step offsets remain chromatic automation values. The selected scale is
// applied relative to ROOT at playback and while editing notes, which makes
// scale changes non-destructive and keeps legacy patterns compatible.
inline int32_t ambiAcidQuantizeSemitoneOffset(
    int32_t offset, uint32_t scale)
{
    offset = std::clamp(offset, -36, 36);
    const auto& definition = musicalScaleDefinition(scale);
    int32_t best = 0;
    int32_t bestDistance = std::numeric_limits<int32_t>::max();
    for (int32_t octave = -3; octave <= 3; ++octave) {
        for (uint32_t degree = 0u; degree < definition.size; ++degree) {
            const int32_t candidate = octave * 12
                + definition.semitones[degree];
            if (candidate < -36 || candidate > 36) continue;
            const int32_t distance = std::abs(candidate - offset);
            if (distance < bestDistance
                || (distance == bestDistance && candidate < best)) {
                best = candidate;
                bestDistance = distance;
            }
        }
    }
    return best;
}

inline int32_t ambiAcidMoveScaleDegree(
    int32_t offset, uint32_t scale, int32_t direction)
{
    const int32_t current = ambiAcidQuantizeSemitoneOffset(offset, scale);
    if (direction == 0) return current;
    const int32_t increment = direction > 0 ? 1 : -1;
    for (int32_t candidate = current + increment;
         candidate >= -36 && candidate <= 36; candidate += increment) {
        if (ambiAcidQuantizeSemitoneOffset(candidate, scale) == candidate) {
            return candidate;
        }
    }
    return current;
}

enum class AmbiAcidDriveCircuit : uint32_t {
    Classic = 0u,
    Shred,
    Wool,
    Rat,
    ZoneA,
    ZoneB,
    FuzzI,
    FuzzII,
    Diode,
    Count,
};

inline constexpr uint32_t kAmbiAcidDriveCircuitCount =
    static_cast<uint32_t>(AmbiAcidDriveCircuit::Count);

inline const char* ambiAcidDriveCircuitName(AmbiAcidDriveCircuit circuit)
{
    switch (circuit) {
    case AmbiAcidDriveCircuit::Classic: return "CLASSIC";
    case AmbiAcidDriveCircuit::Shred: return "SHRED";
    case AmbiAcidDriveCircuit::Wool: return "WOOL";
    case AmbiAcidDriveCircuit::Rat: return "RAT";
    case AmbiAcidDriveCircuit::ZoneA: return "ZONE A";
    case AmbiAcidDriveCircuit::ZoneB: return "ZONE B";
    case AmbiAcidDriveCircuit::FuzzI: return "FUZZ I";
    case AmbiAcidDriveCircuit::FuzzII: return "FUZZ II";
    case AmbiAcidDriveCircuit::Diode: return "DIODE";
    case AmbiAcidDriveCircuit::Count: break;
    }
    return "CLASSIC";
}

enum class AmbiAcidOutputMode : uint32_t {
    Ambisonic = 0u,
    DualMono,
};

// One authored point belongs to each sequencer step. X is front/back, Y is
// listener-left/right, and Z is a normalized height which is scaled by the
// global elevation control before encoding.
struct AmbiAcidSpatialPoint {
    float x = 1.0f;
    float y = 0.0f;
    float z = 0.0f;
};

inline constexpr std::array<AmbiAcidSpatialPoint, kAmbiAcidStepCount>
    kAmbiAcidDefaultSpatialPath {{
        {  1.000000f,  0.000000f,  0.00f },
        {  0.923880f,  0.382683f,  0.35f },
        {  0.707107f,  0.707107f,  0.65f },
        {  0.382683f,  0.923880f,  0.90f },
        {  0.000000f,  1.000000f,  1.00f },
        { -0.382683f,  0.923880f,  0.70f },
        { -0.707107f,  0.707107f,  0.35f },
        { -0.923880f,  0.382683f,  0.00f },
        { -1.000000f,  0.000000f, -0.35f },
        { -0.923880f, -0.382683f, -0.70f },
        { -0.707107f, -0.707107f, -1.00f },
        { -0.382683f, -0.923880f, -0.90f },
        {  0.000000f, -1.000000f, -0.65f },
        {  0.382683f, -0.923880f, -0.35f },
        {  0.707107f, -0.707107f,  0.00f },
        {  0.923880f, -0.382683f,  0.20f },
    }};

struct AmbiAcidPatternPreset {
    const char* name;
    std::array<AmbiAcidStep, kAmbiAcidStepCount> steps;
};

inline constexpr std::array<AmbiAcidPatternPreset, 12u>
    kAmbiAcidPatternPresets {{
        { "CHROME BURROW", {{
            { 0, true,  true,  false }, { 0, true,  false, true  },
            { 12, true, false, false }, { 3, true,  false, false },
            { 0, true,  true,  false }, { 7, true,  false, true  },
            { 10, true, false, false }, { 0, false, false, false },
            { 0, true,  false, true  }, { -2, true, false, false },
            { 3, true,  true,  false }, { 7, true,  false, false },
            { 0, true,  false, false }, { 12, true, true,  true  },
            { 10, true, false, false }, { 3, true,  false, false },
        }}},
        { "VENOM CIRCUIT", {{
            { 0, true,  true,  true  }, { 1, true,  false, true  },
            { 7, true,  false, false }, { 0, true,  false, false },
            { 12, true, true,  false }, { 10, true, false, true  },
            { 8, true,  false, false }, { 0, false, false, false },
            { 3, true,  true,  true  }, { 4, true,  false, true  },
            { 7, true,  false, false }, { 13, true, false, false },
            { 12, true, true,  true  }, { 10, true, false, false },
            { 1, true,  false, true  }, { 0, true,  false, false },
        }}},
        { "SUBWAY TEETH", {{
            { -12, true, true,  false }, { -12, true, false, false },
            { -5, true,  false, true  }, { -3, true, false, false },
            { 0, true,   true,  false }, { 0, false, false, false },
            { -2, true,  false, true  }, { -5, true, false, false },
            { -12, true, true,  false }, { -8, true, false, true  },
            { -7, true,  false, false }, { 0, false, false, false },
            { 0, true,   true,  true  }, { -2, true, false, false },
            { -5, true,  false, false }, { -11, true, false, false },
        }}},
        { "NEON CENTIPEDE", {{
            { 0, true,  true,  true }, { 3, true,  false, true },
            { 5, true,  false, true }, { 7, true,  false, true },
            { 10, true, true,  true }, { 12, true, false, true },
            { 15, true, false, true }, { 12, true, false, true },
            { 7, true,  true,  true }, { 8, true,  false, true },
            { 10, true, false, true }, { 7, true,  false, true },
            { 3, true,  true,  true }, { 5, true,  false, true },
            { 1, true,  false, true }, { 0, true,  false, false },
        }}},
        { "RUSTED APSE", {{
            { 0, true,  true,  false }, { 0, false, false, false },
            { 7, true,  false, true  }, { 7, true,  false, false },
            { 3, true,  true,  false }, { 0, false, false, false },
            { -2, true, false, true  }, { 0, true,  false, false },
            { 0, true,  true,  false }, { 12, true, false, true  },
            { 10, true, false, false }, { 0, false, false, false },
            { 3, true,  true,  false }, { 7, true,  false, true  },
            { 5, true,  false, false }, { 1, true,  false, false },
        }}},
        { "MERCURY STAIRS", {{
            { -12, true, true,  true  }, { -8, true, false, true  },
            { -5, true,  false, true  }, { -2, true, false, true  },
            { 0, true,   true,  true  }, { 3, true,  false, true  },
            { 7, true,   false, true  }, { 10, true, false, true  },
            { 12, true,  true,  true  }, { 15, true, false, true  },
            { 19, true,  false, true  }, { 22, true, false, false },
            { 19, true,  true,  true  }, { 12, true, false, true  },
            { 7, true,   false, true  }, { 0, true,  false, false },
        }}},
        { "BLACKLIGHT MOSS", {{
            { 0, true,   true,  false }, { 0, false, false, false },
            { 0, false,  false, false }, { -5, true, false, true  },
            { -2, true,  false, false }, { 0, false, false, false },
            { 7, true,   true,  false }, { 0, false, false, false },
            { 3, true,   false, true  }, { 0, false, false, false },
            { 10, true,  true,  false }, { 0, false, false, false },
            { 0, true,   false, true  }, { -12, true, true, false },
            { 0, false,  false, false }, { -2, true, false, false },
        }}},
        { "QUARRY SIGNAL", {{
            { 0, true,  true,  false }, { 7, true,  false, false },
            { 0, true,  false, true  }, { 12, true, false, false },
            { 7, true,  true,  false }, { 14, true, false, true  },
            { 12, true, false, false }, { 0, false, false, false },
            { -5, true, true,  false }, { 2, true,  false, false },
            { 7, true,  false, true  }, { 5, true,  false, false },
            { 0, true,  true,  false }, { 19, true, false, true  },
            { 12, true, false, false }, { 7, true,  false, false },
        }}},
        { "NIGHT FOUNDRY", {{
            { 0, true,  true,  false }, { 0, false, false, false },
            { 12, true, false, true  }, { 10, true, false, false },
            { 0, false, false, false }, { 3, true,  true,  false },
            { 5, true,  false, true  }, { 7, true,  false, false },
            { 0, true,  true,  false }, { -2, true, false, false },
            { 0, false, false, false }, { 10, true, false, true  },
            { 12, true, true,  false }, { 7, true,  false, false },
            { 3, true,  false, true  }, { 1, true,  false, false },
        }}},
        { "STATIC ROOT", {{
            { 0, true, true, false }, { 0, true, false, true  },
            { 0, true, false, false }, { 0, false, false, false },
            { 0, true, true, false }, { 0, true, false, true  },
            { 0, true, false, false }, { 0, true, false, false },
            { 0, true, true, false }, { 0, false, false, false },
            { 0, true, false, true  }, { 0, true, false, false },
            { 0, true, true, false }, { 0, true, false, true  },
            { 0, true, false, false }, { 0, true, false, false },
        }}},
        { "HOLLOW RELAY", {{
            { 0, true,  true,  false }, { 7, true,  false, true  },
            { 12, true, false, false }, { 0, false, false, false },
            { 3, true,  false, false }, { 10, true, true,  true  },
            { 7, true,  false, false }, { 0, false, false, false },
            { -12, true, true, false }, { -5, true, false, true  },
            { 0, true,  false, false }, { 0, false, false, false },
            { -9, true, false, false }, { -2, true, true, true  },
            { 3, true,  false, false }, { 7, true, false, false },
        }}},
        { "GLASS WORM", {{
            { 0, true,  true,  true  }, { 1, true,  false, true  },
            { 3, true,  false, true  }, { 4, true,  false, false },
            { 7, true,  true,  true  }, { 6, true,  false, true  },
            { 5, true,  false, true  }, { 3, true,  false, false },
            { 0, true,  true,  true  }, { -1, true, false, true  },
            { -3, true, false, true  }, { -5, true, false, false },
            { -2, true, true,  true  }, { 0, true,  false, true  },
            { 1, true,  false, true  }, { 0, true,  false, false },
        }}},
    }};

inline constexpr auto kAmbiAcidChromeBurrowPattern =
    kAmbiAcidPatternPresets[0u].steps;

struct AmbiAcidParams {
    uint32_t order = 3u;
    float tempoBpm = 126.0f;
    uint32_t stepsPerBeat = 4u;
    uint32_t patternLength = 16u;
    int32_t rootMidiNote = 36;
    float gateLength = 0.58f;
    float waveShape = 0.16f;
    float pulseWidth = 0.50f;
    float cutoffHz = 310.0f;
    float resonance = 0.78f;
    float filterEnvelopeOctaves = 3.25f;
    float filterDecayMs = 185.0f;
    float accentAmount = 0.78f;
    float drive = 0.46f;
    float slideMs = 82.0f;
    float centerAzimuthDeg = 0.0f;
    float pathTurns = 1.0f;
    float elevationSpreadDeg = 22.0f;
    float spatialSpread = 1.0f;
    float edgeLeadDeg = 24.0f;
    float wakeAmount = 0.52f;
    float wakeMs = 92.0f;
    // Retained only so version 1-4 plugin states keep their binary layout.
    // Listener behavior is no longer part of Ambi Acid's signal path.
    AmbiFieldListenMode fieldListenMode = AmbiFieldListenMode::Off;
    float fieldListenAmount = 0.62f;
    float listenerMemorySeconds = 0.56f;
    float outputGainDb = -10.0f;
};

// A compact monophonic bassline instrument whose filtered body, resonant
// edge, and delayed edge wake occupy related positions in an ACN/SN3D field.
class AmbiAcidEncoder {
public:
    void prepare(double sampleRate)
    {
        sampleRate_ = std::isfinite(sampleRate)
            ? std::clamp(sampleRate, 8000.0, 768000.0) : 48000.0;
        driveCircuitFadeCoefficient_ = 1.0f
            / std::max(1.0f, static_cast<float>(sampleRate_ * 0.020));
        reset();
    }

    void reset()
    {
        performanceRootActive_ = false;
        started_ = false;
        currentStep_ = 0u;
        completedSteps_ = 0u;
        stepPhase_ = 0.0;
        oscillatorPhase_ = 0.0;
        subOscillatorPhase_ = 0.0;
        subLevelSmoothed_ = subLevel_;
        pitchLog2_ = std::log2(midiNoteHz(params_.rootMidiNote));
        targetPitchLog2_ = pitchLog2_;
        slideActive_ = false;
        gateOpen_ = false;
        amplitudeEnvelope_ = 0.0f;
        filterEnvelope_ = 0.0f;
        accentEnvelope_ = 0.0f;
        oscillatorDc_ = 0.0f;
        bodyDc_ = 0.0f;
        edgeDc_ = 0.0f;
        filterStages_.fill(0.0f);
        bodyDriveStates_.fill({});
        edgeDriveStates_.fill({});
        activeDriveCircuit_ = driveCircuit_;
        previousDriveCircuit_ = driveCircuit_;
        driveCircuitFade_ = 1.0f;
        driveMixSmoothed_ = driveMix_;
        wakeBuffer_.fill(0.0f);
        wakeWritePosition_ = 0u;
        targetDirection_ = { 1.0f, 0.0f, 0.0f };
        bodyDirection_ = targetDirection_;
        edgeDirection_ = targetDirection_;
        wakeDirections_.fill(targetDirection_);
        effectiveCutoffHz_ = params_.cutoffHz;
        filterCutoffLog2_ = std::log2(effectiveCutoffHz_);
        limiterGain_ = 1.0f;
        activity_ = 0.0f;
        lastWakeEnergy_ = 0.0f;
        lastSyncedAbsoluteStep_ = std::numeric_limits<int64_t>::min();
    }

    void setParams(AmbiAcidParams params)
    {
        sanitize(params);
        params_ = params;
    }

    AmbiAcidParams params() const { return params_; }

    void setScale(uint32_t scale)
    {
        scale_ = std::min<uint32_t>(scale, kMusicalScaleCount - 1u);
    }

    uint32_t scale() const { return scale_; }

    void setSubOctave(int32_t octave)
    {
        subOctave_ = std::clamp(octave, -2, 0);
    }

    int32_t subOctave() const { return subOctave_; }

    void setSubLevel(float level)
    {
        subLevel_ = clamp(finiteOr(level, 0.0f), 0.0f, 1.0f);
    }

    float subLevel() const { return subLevel_; }

    void setDriveCircuit(AmbiAcidDriveCircuit circuit)
    {
        driveCircuit_ = static_cast<AmbiAcidDriveCircuit>(
            std::min<uint32_t>(static_cast<uint32_t>(circuit),
                kAmbiAcidDriveCircuitCount - 1u));
    }

    AmbiAcidDriveCircuit driveCircuit() const { return driveCircuit_; }

    void setDriveMix(float mix)
    {
        driveMix_ = clamp(finiteOr(mix, 0.0f), 0.0f, 1.0f);
    }

    float driveMix() const { return driveMix_; }

    void setOutputMode(AmbiAcidOutputMode mode)
    {
        outputMode_ = static_cast<AmbiAcidOutputMode>(
            std::min<uint32_t>(static_cast<uint32_t>(mode), 1u));
    }

    AmbiAcidOutputMode outputMode() const { return outputMode_; }

    void setPerformanceRoot(int32_t midiNote)
    {
        performanceRootMidiNote_ = std::clamp(midiNote, 0, 127);
        performanceRootActive_ = true;
        retargetCurrentPitch();
    }

    void clearPerformanceRoot()
    {
        performanceRootActive_ = false;
        retargetCurrentPitch();
    }

    bool performanceRootActive() const { return performanceRootActive_; }
    int32_t effectiveRootMidiNote() const
    {
        return performanceRootActive_
            ? performanceRootMidiNote_ : params_.rootMidiNote;
    }

    void restartSequence()
    {
        started_ = false;
        currentStep_ = 0u;
        stepPhase_ = 0.0;
        lastSyncedAbsoluteStep_ = std::numeric_limits<int64_t>::min();
    }

    void setStep(uint32_t index, AmbiAcidStep step)
    {
        if (index >= kAmbiAcidStepCount) return;
        step.semitoneOffset = std::clamp(step.semitoneOffset, -36, 36);
        pattern_[index] = step;
    }

    AmbiAcidStep step(uint32_t index) const
    {
        return pattern_[std::min<uint32_t>(
            index, kAmbiAcidStepCount - 1u)];
    }

    void setPattern(
        const std::array<AmbiAcidStep, kAmbiAcidStepCount>& pattern)
    {
        for (uint32_t index = 0u; index < kAmbiAcidStepCount; ++index) {
            setStep(index, pattern[index]);
        }
    }

    const std::array<AmbiAcidStep, kAmbiAcidStepCount>& pattern() const
    {
        return pattern_;
    }

    void setSpatialPoint(uint32_t index, AmbiAcidSpatialPoint point)
    {
        if (index >= kAmbiAcidStepCount) return;
        const auto& fallback = kAmbiAcidDefaultSpatialPath[index];
        point.x = clamp(finiteOr(point.x, fallback.x), -1.0f, 1.0f);
        point.y = clamp(finiteOr(point.y, fallback.y), -1.0f, 1.0f);
        point.z = clamp(finiteOr(point.z, fallback.z), -1.0f, 1.0f);
        spatialPath_[index] = point;
    }

    AmbiAcidSpatialPoint spatialPoint(uint32_t index) const
    {
        return spatialPath_[std::min<uint32_t>(
            index, kAmbiAcidStepCount - 1u)];
    }

    void setSpatialPath(const std::array<AmbiAcidSpatialPoint,
        kAmbiAcidStepCount>& path)
    {
        for (uint32_t index = 0u; index < kAmbiAcidStepCount; ++index) {
            setSpatialPoint(index, path[index]);
        }
    }

    const std::array<AmbiAcidSpatialPoint, kAmbiAcidStepCount>&
        spatialPath() const
    {
        return spatialPath_;
    }

    void processFrame(float* output, uint32_t outputChannels)
    {
        if (!output || outputChannels == 0u) return;
        const uint32_t requestedChannels = outputChannels;
        std::fill(output, output + requestedChannels, 0.0f);
        outputChannels = std::min(outputChannels, kAmbiAcidChannels);
        if (!started_) beginStep(0u);

        const double stepIncrement = static_cast<double>(params_.tempoBpm)
            * static_cast<double>(params_.stepsPerBeat)
            / (60.0 * sampleRate_);
        stepPhase_ += stepIncrement;
        if (stepPhase_ >= 1.0) {
            stepPhase_ -= std::floor(stepPhase_);
            beginStep((currentStep_ + 1u) % params_.patternLength);
        }

        lastSyncedAbsoluteStep_ = std::numeric_limits<int64_t>::min();
        renderCurrentFrame(output, outputChannels);
    }

    // Aligns the sequencer to an absolute host beat position. Calling this at
    // every sample makes starts, seeks, loop wraps, and tempo changes converge
    // immediately without letting a second free-running clock drift alongside
    // the host transport.
    void processFrameSynced(float* output, uint32_t outputChannels,
        double hostBeat, bool playing)
    {
        if (!output || outputChannels == 0u) return;
        const uint32_t requestedChannels = outputChannels;
        std::fill(output, output + requestedChannels, 0.0f);
        outputChannels = std::min(outputChannels, kAmbiAcidChannels);
        if (!playing || !std::isfinite(hostBeat)) {
            started_ = false;
            gateOpen_ = false;
            stepPhase_ = 0.0;
            lastSyncedAbsoluteStep_ = std::numeric_limits<int64_t>::min();
            renderCurrentFrame(output, outputChannels);
            return;
        }

        const double stepPosition = hostBeat
            * static_cast<double>(params_.stepsPerBeat);
        const double flooredStep = std::floor(stepPosition);
        const double boundedStep = std::clamp(flooredStep,
            static_cast<double>(std::numeric_limits<int64_t>::min() + 1),
            static_cast<double>(std::numeric_limits<int64_t>::max()));
        const int64_t absoluteStep = static_cast<int64_t>(boundedStep);
        int64_t wrappedStep = absoluteStep
            % static_cast<int64_t>(params_.patternLength);
        if (wrappedStep < 0) wrappedStep += params_.patternLength;
        if (absoluteStep != lastSyncedAbsoluteStep_) {
            if (lastSyncedAbsoluteStep_ == std::numeric_limits<int64_t>::min()
                || absoluteStep != lastSyncedAbsoluteStep_ + 1) {
                started_ = false;
            }
            beginStep(static_cast<uint32_t>(wrappedStep));
            lastSyncedAbsoluteStep_ = absoluteStep;
        }
        stepPhase_ = std::clamp(stepPosition - flooredStep, 0.0, 1.0);
        renderCurrentFrame(output, outputChannels);
    }

    void processBlock(float* const* output, uint32_t outputChannels,
        uint32_t frames)
    {
        if (!output) return;
        std::array<float, kAmbiAcidChannels> frame {};
        for (uint32_t channel = kAmbiAcidChannels;
             channel < outputChannels; ++channel) {
            if (output[channel]) {
                std::fill(output[channel], output[channel] + frames, 0.0f);
            }
        }
        for (uint32_t sample = 0u; sample < frames; ++sample) {
            processFrame(frame.data(), outputChannels);
            for (uint32_t channel = 0u;
                 channel < std::min(outputChannels, kAmbiAcidChannels);
                 ++channel) {
                if (output[channel]) output[channel][sample] = frame[channel];
            }
        }
    }

    uint32_t currentStep() const { return currentStep_; }
    uint64_t completedSteps() const { return completedSteps_; }
    float currentFrequencyHz() const { return std::exp2(pitchLog2_); }
    float targetFrequencyHz() const { return std::exp2(targetPitchLog2_); }
    float effectiveCutoffHz() const { return effectiveCutoffHz_; }
    float amplitudeEnvelope() const { return amplitudeEnvelope_; }
    float activity() const { return activity_; }
    float wakeEnergy() const { return lastWakeEnergy_; }
    Vec3 targetDirection() const { return targetDirection_; }
    Vec3 bodyDirection() const { return bodyDirection_; }
    Vec3 edgeDirection() const { return edgeDirection_; }

private:
    void renderCurrentFrame(float* output, uint32_t outputChannels)
    {
        const auto& current = pattern_[currentStep_];
        const auto& next = pattern_[
            (currentStep_ + 1u) % params_.patternLength];
        const bool tiedToNext = current.gate && current.slide && next.gate;
        if (!current.gate
            || (stepPhase_ >= static_cast<double>(params_.gateLength)
                && !tiedToNext)) {
            gateOpen_ = false;
        }

        updatePitch();
        updateEnvelopes();
        updateDirections();
        const auto oscillator = renderOscillator();
        float body = 0.0f;
        float edge = 0.0f;
        renderFilter(oscillator.main, body, edge);
        processDrive(body, edge);
        // The sine sub is a parallel foundation: it bypasses both the
        // nonlinear ladder drive and the selected post-filter circuit, then
        // rejoins only the body signal. This keeps its fundamental clean and
        // prevents distortion products from entering the spatial edge/wake.
        body = (body + oscillator.cleanSub) * amplitudeEnvelope_;
        edge *= amplitudeEnvelope_;

        wakeBuffer_[wakeWritePosition_] = flushDenormal(edge);
        wakeWritePosition_ =
            (wakeWritePosition_ + 1u) % kAmbiAcidWakeBufferSamples;

        const float bodyGain = 0.72f;
        const float edgeGain = 0.10f + params_.spatialSpread * 0.12f;
        const bool ambisonic = outputMode_ == AmbiAcidOutputMode::Ambisonic;
        const uint32_t activeChannels = ambisonic
            ? std::min<uint32_t>(outputChannels,
                (params_.order + 1u) * (params_.order + 1u))
            : std::min<uint32_t>(outputChannels, 2u);
        if (ambisonic) {
            const auto bodyBasis = acnSn3dBasis(bodyDirection_);
            const auto edgeBasis = acnSn3dBasis(edgeDirection_);
            for (uint32_t channel = 0u; channel < activeChannels; ++channel) {
                output[channel] = body * bodyBasis[channel] * bodyGain
                    + edge * edgeBasis[channel] * edgeGain;
            }
        } else {
            const float mono = body * bodyGain + edge * edgeGain;
            for (uint32_t channel = 0u; channel < activeChannels; ++channel) {
                output[channel] = mono;
            }
        }

        lastWakeEnergy_ = 0.0f;
        const float wakeGain = params_.wakeAmount * 0.18f;
        constexpr float wakeWeightSum = 1.0f + 0.68f + 0.46f + 0.31f;
        for (uint32_t tap = 0u; tap < kAmbiAcidWakePointCount; ++tap) {
            const uint32_t delaySamples = wakeDelaySamples(tap);
            const uint32_t readPosition = (wakeWritePosition_
                + kAmbiAcidWakeBufferSamples - 1u - delaySamples)
                % kAmbiAcidWakeBufferSamples;
            const float delayed = wakeBuffer_[readPosition];
            const float weight = std::pow(0.68f, static_cast<float>(tap))
                / wakeWeightSum;
            const float wakeSample = delayed * wakeGain * weight;
            lastWakeEnergy_ += std::fabs(wakeSample);
            if (ambisonic) {
                const auto basis = acnSn3dBasis(wakeDirections_[tap]);
                for (uint32_t channel = 0u;
                     channel < activeChannels; ++channel) {
                    output[channel] += wakeSample * basis[channel];
                }
            } else {
                for (uint32_t channel = 0u;
                     channel < activeChannels; ++channel) {
                    output[channel] += wakeSample;
                }
            }
        }
        applyOutputGainAndLimiter(output, activeChannels);
    }

    static float finiteOr(float value, float fallback)
    {
        return std::isfinite(value) ? value : fallback;
    }

    static float midiNoteHz(int32_t midiNote)
    {
        return 440.0f * std::pow(2.0f,
            static_cast<float>(midiNote - 69) / 12.0f);
    }

    static Vec3 blendDirection(Vec3 a, Vec3 b, float amount)
    {
        amount = clamp(amount, 0.0f, 1.0f);
        return normalize({
            lerp(a.x, b.x, amount),
            lerp(a.y, b.y, amount),
            lerp(a.z, b.z, amount),
        });
    }

    static Vec3 rotateAroundZ(Vec3 direction, float degrees)
    {
        const float angle = degrees * kPi / 180.0f;
        const float cosine = std::cos(angle);
        const float sine = std::sin(angle);
        return normalize({
            direction.x * cosine - direction.y * sine,
            direction.x * sine + direction.y * cosine,
            direction.z,
        });
    }

    static float polyBlep(float phase, float increment)
    {
        if (phase < increment) {
            const float x = phase / increment;
            return x + x - x * x - 1.0f;
        }
        if (phase > 1.0f - increment) {
            const float x = (phase - 1.0f) / increment;
            return x * x + x + x + 1.0f;
        }
        return 0.0f;
    }

    void sanitize(AmbiAcidParams& params) const
    {
        params.order = std::clamp<uint32_t>(params.order, 1u, 3u);
        params.tempoBpm = clamp(
            finiteOr(params.tempoBpm, 126.0f), 30.0f, 300.0f);
        params.stepsPerBeat =
            std::clamp<uint32_t>(params.stepsPerBeat, 1u, 8u);
        params.patternLength =
            std::clamp<uint32_t>(params.patternLength, 1u, 16u);
        params.rootMidiNote = std::clamp(params.rootMidiNote, 12, 72);
        params.gateLength = clamp(
            finiteOr(params.gateLength, 0.58f), 0.05f, 1.0f);
        params.waveShape = clamp(
            finiteOr(params.waveShape, 0.16f), 0.0f, 1.0f);
        params.pulseWidth = clamp(
            finiteOr(params.pulseWidth, 0.50f), 0.12f, 0.88f);
        params.cutoffHz = clamp(
            finiteOr(params.cutoffHz, 310.0f), 30.0f, 12000.0f);
        params.resonance = clamp(
            finiteOr(params.resonance, 0.78f), 0.0f, 1.0f);
        params.filterEnvelopeOctaves = clamp(
            finiteOr(params.filterEnvelopeOctaves, 3.25f), 0.0f, 7.0f);
        params.filterDecayMs = clamp(
            finiteOr(params.filterDecayMs, 185.0f), 20.0f, 2000.0f);
        params.accentAmount = clamp(
            finiteOr(params.accentAmount, 0.78f), 0.0f, 1.0f);
        params.drive = clamp(
            finiteOr(params.drive, 0.46f), 0.0f, 1.0f);
        params.slideMs = clamp(
            finiteOr(params.slideMs, 82.0f), 5.0f, 500.0f);
        params.centerAzimuthDeg = clamp(
            finiteOr(params.centerAzimuthDeg, 0.0f), -180.0f, 180.0f);
        params.pathTurns = clamp(
            finiteOr(params.pathTurns, 1.0f), -4.0f, 4.0f);
        params.elevationSpreadDeg = clamp(
            finiteOr(params.elevationSpreadDeg, 22.0f), 0.0f, 70.0f);
        params.spatialSpread = clamp(
            finiteOr(params.spatialSpread, 1.0f), 0.0f, 1.0f);
        params.edgeLeadDeg = clamp(
            finiteOr(params.edgeLeadDeg, 24.0f), -90.0f, 90.0f);
        params.wakeAmount = clamp(
            finiteOr(params.wakeAmount, 0.52f), 0.0f, 1.0f);
        params.wakeMs = clamp(
            finiteOr(params.wakeMs, 92.0f), 5.0f, 240.0f);
        params.fieldListenMode =
            sanitizeAmbiFieldListenMode(params.fieldListenMode);
        params.fieldListenAmount = clamp(
            finiteOr(params.fieldListenAmount, 0.62f), 0.0f, 1.0f);
        params.listenerMemorySeconds = clamp(
            finiteOr(params.listenerMemorySeconds, 0.56f), 0.05f, 4.0f);
        params.outputGainDb = clamp(
            finiteOr(params.outputGainDb, -10.0f), -36.0f, 6.0f);
    }

    Vec3 authoredDirection(uint32_t stepIndex) const
    {
        const auto& point = spatialPath_[std::min<uint32_t>(
            stepIndex, kAmbiAcidStepCount - 1u)];
        const float phase = static_cast<float>(stepIndex)
            / static_cast<float>(std::max<uint32_t>(1u, params_.patternLength));
        const float authoredAzimuth = std::atan2(point.y, point.x)
            * 180.0f / kPi;
        const float azimuth = authoredAzimuth + params_.centerAzimuthDeg
            + phase * (params_.pathTurns - 1.0f) * 360.0f;
        const float elevation = point.z * params_.elevationSpreadDeg;
        const Vec3 path = directionFromAed(azimuth, elevation);
        return blendDirection(
            { 1.0f, 0.0f, 0.0f }, path, params_.spatialSpread);
    }

    void beginStep(uint32_t stepIndex)
    {
        const bool tied = started_
            && pattern_[currentStep_].gate
            && pattern_[currentStep_].slide
            && pattern_[stepIndex].gate;
        const Vec3 previousDirection = targetDirection_;
        currentStep_ = stepIndex;
        started_ = true;
        ++completedSteps_;

        const auto& current = pattern_[currentStep_];
        const int32_t note = std::clamp(
            effectiveRootMidiNote() + ambiAcidQuantizeSemitoneOffset(
                current.semitoneOffset, scale_), 0, 127);
        targetPitchLog2_ = std::log2(midiNoteHz(note));
        slideActive_ = tied;
        if (!tied) pitchLog2_ = targetPitchLog2_;

        if (current.gate) {
            gateOpen_ = true;
            if (!tied) {
                amplitudeEnvelope_ = std::max(
                    amplitudeEnvelope_, 0.015f);
                filterEnvelope_ = 1.0f;
                accentEnvelope_ = current.accent ? 1.0f : 0.0f;
            } else if (current.accent) {
                filterEnvelope_ = std::max(filterEnvelope_, 0.82f);
                accentEnvelope_ = 1.0f;
            }
        } else {
            gateOpen_ = false;
        }

        for (uint32_t index = kAmbiAcidWakePointCount - 1u;
             index > 0u; --index) {
            wakeDirections_[index] = wakeDirections_[index - 1u];
        }
        wakeDirections_[0u] = previousDirection;

        const Vec3 authored = authoredDirection(currentStep_);
        targetDirection_ = authored;
    }

    void retargetCurrentPitch()
    {
        if (!started_) return;
        const int32_t note = std::clamp(effectiveRootMidiNote()
            + ambiAcidQuantizeSemitoneOffset(
                pattern_[currentStep_].semitoneOffset, scale_), 0, 127);
        targetPitchLog2_ = std::log2(midiNoteHz(note));
        pitchLog2_ = targetPitchLog2_;
        slideActive_ = false;
    }

    void updatePitch()
    {
        if (!slideActive_) {
            pitchLog2_ = targetPitchLog2_;
            return;
        }
        const float seconds = params_.slideMs * 0.001f;
        const float coefficient = 1.0f - std::exp(-1.0f
            / std::max(1.0, sampleRate_ * static_cast<double>(seconds) * 0.22));
        pitchLog2_ += (targetPitchLog2_ - pitchLog2_) * coefficient;
        if (std::fabs(targetPitchLog2_ - pitchLog2_) < 1.0e-6f) {
            pitchLog2_ = targetPitchLog2_;
            slideActive_ = false;
        }
    }

    void updateEnvelopes()
    {
        const float attackCoefficient = 1.0f - std::exp(-1.0f
            / std::max(1.0, sampleRate_ * 0.0015));
        const float releaseCoefficient = 1.0f - std::exp(-1.0f
            / std::max(1.0, sampleRate_ * 0.024));
        const float accentGain = pattern_[currentStep_].accent
            ? 1.0f + params_.accentAmount * 0.24f : 1.0f;
        const float amplitudeTarget = gateOpen_ ? accentGain : 0.0f;
        amplitudeEnvelope_ += (amplitudeTarget - amplitudeEnvelope_)
            * (amplitudeTarget > amplitudeEnvelope_
                ? attackCoefficient : releaseCoefficient);

        const float filterDecaySeconds = params_.filterDecayMs * 0.001f
            * (1.0f + params_.accentAmount * accentEnvelope_ * 0.72f);
        filterEnvelope_ *= std::exp(-1.0f
            / std::max(1.0, sampleRate_ * static_cast<double>(filterDecaySeconds)));
        accentEnvelope_ *= std::exp(-1.0f
            / std::max(1.0, sampleRate_ * 0.135));
        amplitudeEnvelope_ = flushDenormal(amplitudeEnvelope_);
        filterEnvelope_ = flushDenormal(filterEnvelope_);
        accentEnvelope_ = flushDenormal(accentEnvelope_);
    }

    void updateDirections()
    {
        const float seconds = slideActive_
            ? params_.slideMs * 0.001f : 0.012f;
        const float coefficient = 1.0f - std::exp(-1.0f
            / std::max(1.0, sampleRate_ * static_cast<double>(seconds)));
        bodyDirection_ = normalize({
            bodyDirection_.x + (targetDirection_.x - bodyDirection_.x)
                * coefficient,
            bodyDirection_.y + (targetDirection_.y - bodyDirection_.y)
                * coefficient,
            bodyDirection_.z + (targetDirection_.z - bodyDirection_.z)
                * coefficient,
        });
        const Vec3 edgeTarget = rotateAroundZ(
            targetDirection_, params_.edgeLeadDeg * params_.spatialSpread);
        edgeDirection_ = normalize({
            edgeDirection_.x + (edgeTarget.x - edgeDirection_.x)
                * coefficient,
            edgeDirection_.y + (edgeTarget.y - edgeDirection_.y)
                * coefficient,
            edgeDirection_.z + (edgeTarget.z - edgeDirection_.z)
                * coefficient,
        });
    }

    struct OscillatorFrame {
        float main = 0.0f;
        float cleanSub = 0.0f;
    };

    OscillatorFrame renderOscillator()
    {
        const float frequency = std::clamp(
            std::exp2(pitchLog2_), 8.0f,
            static_cast<float>(sampleRate_ * 0.42));
        const float increment = std::min(
            0.42f, frequency / static_cast<float>(sampleRate_));
        const float phase = static_cast<float>(oscillatorPhase_);
        const float saw = 2.0f * phase - 1.0f
            - polyBlep(phase, increment);
        float pulse = phase < params_.pulseWidth ? 1.0f : -1.0f;
        pulse += polyBlep(phase, increment);
        float pulsePhase = phase - params_.pulseWidth;
        if (pulsePhase < 0.0f) pulsePhase += 1.0f;
        pulse -= polyBlep(pulsePhase, increment);
        oscillatorPhase_ += increment;
        oscillatorPhase_ -= std::floor(oscillatorPhase_);

        const float raw = lerp(saw, pulse, params_.waveShape);
        const float subFrequency = frequency * std::exp2(
            static_cast<float>(subOctave_));
        const float subIncrement = std::min(
            0.42f, subFrequency / static_cast<float>(sampleRate_));
        const float sub = std::sin(
            2.0f * kPi * static_cast<float>(subOscillatorPhase_));
        subOscillatorPhase_ += subIncrement;
        subOscillatorPhase_ -= std::floor(subOscillatorPhase_);
        const float subSmoothing = 1.0f - std::exp(-1.0f
            / std::max(1.0, sampleRate_ * 0.004));
        subLevelSmoothed_ += (subLevel_ - subLevelSmoothed_) * subSmoothing;
        const float dcCoefficient = 1.0f - std::exp(-2.0f * kPi * 9.0f
            / static_cast<float>(sampleRate_));
        oscillatorDc_ += (raw - oscillatorDc_) * dcCoefficient;
        return {
            raw - oscillatorDc_,
            sub * subLevelSmoothed_ * 0.72f,
        };
    }

    void renderFilter(float oscillator, float& body, float& edge)
    {
        const float accentOctaves = params_.accentAmount
            * accentEnvelope_ * 1.15f;
        const float cutoff = params_.cutoffHz * std::pow(2.0f,
            params_.filterEnvelopeOctaves * filterEnvelope_
                + accentOctaves);
        const float targetCutoffHz = std::clamp(cutoff, 25.0f,
            std::min(19000.0f, static_cast<float>(sampleRate_ * 0.41)));
        const float targetCutoffLog2 = std::log2(targetCutoffHz);
        // The pattern envelope and host automation can change the requested
        // cutoff at a step boundary. Ramp in log
        // frequency so the ladder coefficient never takes a one-sample leap,
        // which is especially audible as a click against a dark low-cutoff
        // signal. The fast upward time keeps the acid attack intact; the
        // slightly slower downward time avoids a zippery closing tail.
        const float cutoffSmoothingSeconds =
            targetCutoffLog2 > filterCutoffLog2_ ? 0.0020f : 0.0060f;
        const float cutoffSmoothing = 1.0f - std::exp(-1.0f
            / std::max(1.0, sampleRate_
                * static_cast<double>(cutoffSmoothingSeconds)));
        filterCutoffLog2_ +=
            (targetCutoffLog2 - filterCutoffLog2_) * cutoffSmoothing;
        effectiveCutoffHz_ = std::exp2(filterCutoffLog2_);
        const float substepRate = static_cast<float>(sampleRate_ * 2.0);
        const float coefficient = 1.0f - std::exp(
            -2.0f * kPi * effectiveCutoffHz_ / substepRate);
        const float driveGain = 1.0f + params_.drive * 7.0f;
        const float feedback = params_.resonance * 3.72f;
        for (uint32_t substep = 0u; substep < 2u; ++substep) {
            float stageInput = std::tanh(
                oscillator * driveGain - filterStages_[3u] * feedback);
            for (uint32_t stage = 0u; stage < filterStages_.size(); ++stage) {
                filterStages_[stage] +=
                    (stageInput - filterStages_[stage]) * coefficient;
                filterStages_[stage] = flushDenormal(filterStages_[stage]);
                stageInput = std::tanh(filterStages_[stage]);
            }
        }
        const float resonanceCompensation = 1.0f + params_.resonance * 1.35f;
        body = filterStages_[3u] * resonanceCompensation * 0.72f;
        edge = (filterStages_[2u] - filterStages_[3u])
            * (1.15f + params_.resonance * 2.2f);
        const float dcCoefficient = 1.0f - std::exp(-2.0f * kPi * 12.0f
            / static_cast<float>(sampleRate_));
        bodyDc_ += (body - bodyDc_) * dcCoefficient;
        edgeDc_ += (edge - edgeDc_) * dcCoefficient;
        body -= bodyDc_;
        edge -= edgeDc_;
    }

    struct DriveCircuitState {
        float memory = 0.0f;
        float low = 0.0f;
        float high = 0.0f;
        float envelope = 0.0f;
    };

    static float foldDrive(float value)
    {
        const float shifted = value + 1.0f;
        const float wrapped = shifted
            - 4.0f * std::floor(shifted * 0.25f);
        return wrapped <= 2.0f ? wrapped - 1.0f : 3.0f - wrapped;
    }

    float processDriveCircuit(AmbiAcidDriveCircuit circuit,
        std::array<DriveCircuitState, kAmbiAcidDriveCircuitCount>& states,
        float input)
    {
        const float amount = params_.drive;
        if (circuit == AmbiAcidDriveCircuit::Classic) {
            if (amount <= 0.0001f) return input;
            const float gain = 1.0f + amount * 10.0f;
            return std::tanh(input * gain) / std::tanh(gain);
        }
        if (circuit == AmbiAcidDriveCircuit::Shred) {
            const float gain = 1.0f + amount * 10.0f;
            const float saturated = std::tanh(input * gain);
            const float folded = foldDrive(input * (1.0f + amount * 6.0f));
            return lerp(saturated, folded, amount * amount * 0.72f);
        }
        const uint32_t index = std::min<uint32_t>(
            static_cast<uint32_t>(circuit),
            kAmbiAcidDriveCircuitCount - 1u);
        const float tone = clamp(std::log(
            std::max(30.0f, effectiveCutoffHz_) / 30.0f)
            / std::log(12000.0f / 30.0f), 0.0f, 1.0f);
        const float bias = accentEnvelope_ * params_.accentAmount * 0.04f;
        return processAnalogDriveCircuit(
            static_cast<AnalogDriveCircuit>(index - 2u), states[index],
            input, amount, tone, bias, static_cast<float>(sampleRate_));
    }

    void processDrive(float& body, float& edge)
    {
        if (driveCircuit_ != activeDriveCircuit_) {
            previousDriveCircuit_ = activeDriveCircuit_;
            activeDriveCircuit_ = driveCircuit_;
            driveCircuitFade_ = 0.0f;
        }
        const float activeBody = processDriveCircuit(
            activeDriveCircuit_, bodyDriveStates_, body);
        const float activeEdge = processDriveCircuit(
            activeDriveCircuit_, edgeDriveStates_, edge);
        float wetBody = activeBody;
        float wetEdge = activeEdge;
        if (driveCircuitFade_ < 1.0f) {
            const float previousBody = processDriveCircuit(
                previousDriveCircuit_, bodyDriveStates_, body);
            const float previousEdge = processDriveCircuit(
                previousDriveCircuit_, edgeDriveStates_, edge);
            wetBody = lerp(previousBody, activeBody, driveCircuitFade_);
            wetEdge = lerp(previousEdge, activeEdge, driveCircuitFade_);
            driveCircuitFade_ = std::min(1.0f,
                driveCircuitFade_ + driveCircuitFadeCoefficient_);
        }
        const float mixSmoothing = 1.0f - std::exp(-1.0f
            / std::max(1.0, sampleRate_ * 0.006));
        driveMixSmoothed_ += (driveMix_ - driveMixSmoothed_) * mixSmoothing;
        body = lerp(body, wetBody, driveMixSmoothed_);
        edge = lerp(edge, wetEdge, driveMixSmoothed_);
    }

    uint32_t wakeDelaySamples(uint32_t tap) const
    {
        const float fraction = static_cast<float>(tap + 1u)
            / static_cast<float>(kAmbiAcidWakePointCount);
        const double delay = sampleRate_ * params_.wakeMs * 0.001
            * static_cast<double>(fraction);
        return std::min<uint32_t>(
            kAmbiAcidWakeBufferSamples - 2u,
            static_cast<uint32_t>(std::max(1.0, std::round(delay))));
    }

    void applyOutputGainAndLimiter(float* output, uint32_t channels)
    {
        const float gain = dbToGain(params_.outputGainDb);
        float peak = 0.0f;
        for (uint32_t channel = 0u; channel < channels; ++channel) {
            peak = std::max(peak, std::fabs(output[channel] * gain));
        }
        const float desired = peak > 0.96f ? 0.96f / peak : 1.0f;
        if (desired < limiterGain_) {
            limiterGain_ = desired;
        } else {
            const float release = 1.0f - std::exp(-1.0f
                / std::max(1.0, sampleRate_ * 0.080));
            limiterGain_ += (desired - limiterGain_) * release;
        }
        peak = 0.0f;
        const float finalGain = gain * limiterGain_;
        for (uint32_t channel = 0u; channel < channels; ++channel) {
            output[channel] = flushDenormal(output[channel] * finalGain);
            peak = std::max(peak, std::fabs(output[channel]));
        }
        activity_ += (peak - activity_)
            * (peak > activity_ ? 0.08f : 0.0015f);
        activity_ = flushDenormal(activity_);
    }

    double sampleRate_ = 48000.0;
    AmbiAcidParams params_ {};
    uint32_t scale_ = 0u;
    int32_t subOctave_ = -1;
    float subLevel_ = 0.0f;
    float subLevelSmoothed_ = 0.0f;
    AmbiAcidDriveCircuit driveCircuit_ = AmbiAcidDriveCircuit::Classic;
    float driveMix_ = 0.0f;
    AmbiAcidOutputMode outputMode_ = AmbiAcidOutputMode::Ambisonic;
    bool performanceRootActive_ = false;
    int32_t performanceRootMidiNote_ = 36;
    std::array<AmbiAcidStep, kAmbiAcidStepCount> pattern_ =
        kAmbiAcidChromeBurrowPattern;
    std::array<AmbiAcidSpatialPoint, kAmbiAcidStepCount> spatialPath_ =
        kAmbiAcidDefaultSpatialPath;
    bool started_ = false;
    uint32_t currentStep_ = 0u;
    uint64_t completedSteps_ = 0u;
    double stepPhase_ = 0.0;
    double oscillatorPhase_ = 0.0;
    double subOscillatorPhase_ = 0.0;
    float pitchLog2_ = 5.0f;
    float targetPitchLog2_ = 5.0f;
    bool slideActive_ = false;
    bool gateOpen_ = false;
    float amplitudeEnvelope_ = 0.0f;
    float filterEnvelope_ = 0.0f;
    float accentEnvelope_ = 0.0f;
    float oscillatorDc_ = 0.0f;
    float bodyDc_ = 0.0f;
    float edgeDc_ = 0.0f;
    std::array<float, 4u> filterStages_ {};
    std::array<DriveCircuitState, kAmbiAcidDriveCircuitCount>
        bodyDriveStates_ {};
    std::array<DriveCircuitState, kAmbiAcidDriveCircuitCount>
        edgeDriveStates_ {};
    AmbiAcidDriveCircuit activeDriveCircuit_ =
        AmbiAcidDriveCircuit::Classic;
    AmbiAcidDriveCircuit previousDriveCircuit_ =
        AmbiAcidDriveCircuit::Classic;
    float driveCircuitFade_ = 1.0f;
    float driveCircuitFadeCoefficient_ = 0.001f;
    float driveMixSmoothed_ = 0.0f;
    std::array<float, kAmbiAcidWakeBufferSamples> wakeBuffer_ {};
    uint32_t wakeWritePosition_ = 0u;
    Vec3 targetDirection_ { 1.0f, 0.0f, 0.0f };
    Vec3 bodyDirection_ { 1.0f, 0.0f, 0.0f };
    Vec3 edgeDirection_ { 1.0f, 0.0f, 0.0f };
    std::array<Vec3, kAmbiAcidWakePointCount> wakeDirections_ {};
    float effectiveCutoffHz_ = 310.0f;
    float filterCutoffLog2_ = std::log2(310.0f);
    float limiterGain_ = 1.0f;
    float activity_ = 0.0f;
    float lastWakeEnergy_ = 0.0f;
    int64_t lastSyncedAbsoluteStep_ = std::numeric_limits<int64_t>::min();
};

} // namespace s3g
