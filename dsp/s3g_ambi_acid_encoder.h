#pragma once

#include "s3g_ambi_field_listener.h"
#include "s3g_ambisonic_geometry.h"
#include "s3g_math.h"
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
    float spatialSpread = 0.78f;
    float edgeLeadDeg = 24.0f;
    float wakeAmount = 0.52f;
    float wakeMs = 92.0f;
    AmbiFieldListenMode fieldListenMode = AmbiFieldListenMode::Off;
    float fieldListenAmount = 0.62f;
    float listenerMemorySeconds = 0.56f;
    float outputGainDb = -10.0f;
};

// A compact monophonic bassline instrument whose filtered body, resonant
// edge, and delayed edge wake occupy related positions in an ACN/SN3D field.
// Listener Mode reads the completed pre-output field. At step boundaries it
// can bend the next position and gently bias its filter, but it never changes
// programmed pitch, gate, accent, slide, or clock decisions.
class AmbiAcidEncoder {
public:
    void prepare(double sampleRate)
    {
        sampleRate_ = std::isfinite(sampleRate)
            ? std::clamp(sampleRate, 8000.0, 768000.0) : 48000.0;
        fieldListener_.prepare(sampleRate_);
        fieldListener_.setExtendedAnalysisEnabled(true);
        fieldListener_.setMemorySeconds(params_.listenerMemorySeconds);
        const auto& directions = ambiFieldListenerCubeDirections();
        fieldListener_.setDirections(directions.data(),
            static_cast<uint32_t>(directions.size()));
        reset();
    }

    void reset()
    {
        started_ = false;
        currentStep_ = 0u;
        completedSteps_ = 0u;
        stepPhase_ = 0.0;
        oscillatorPhase_ = 0.0;
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
        wakeBuffer_.fill(0.0f);
        wakeWritePosition_ = 0u;
        targetDirection_ = { 1.0f, 0.0f, 0.0f };
        bodyDirection_ = targetDirection_;
        edgeDirection_ = targetDirection_;
        wakeDirections_.fill(targetDirection_);
        listenerCutoffBiasOctaves_ = 0.0f;
        effectiveCutoffHz_ = params_.cutoffHz;
        limiterGain_ = 1.0f;
        activity_ = 0.0f;
        lastWakeEnergy_ = 0.0f;
        lastSyncedAbsoluteStep_ = std::numeric_limits<int64_t>::min();
        fieldListener_.reset();
    }

    void setParams(AmbiAcidParams params)
    {
        sanitize(params);
        const bool listenerMemoryChanged =
            params.listenerMemorySeconds != params_.listenerMemorySeconds;
        params_ = params;
        if (listenerMemoryChanged) {
            fieldListener_.setMemorySeconds(params_.listenerMemorySeconds);
        }
    }

    AmbiAcidParams params() const { return params_; }

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
    float fieldListenActivity() const { return fieldListener_.activity(); }
    float fieldListenEnvelope(uint32_t pickup) const
    {
        return fieldListener_.envelope(pickup);
    }

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
        const float oscillator = renderOscillator();
        float body = 0.0f;
        float edge = 0.0f;
        renderFilter(oscillator, body, edge);
        body *= amplitudeEnvelope_;
        edge *= amplitudeEnvelope_;

        wakeBuffer_[wakeWritePosition_] = flushDenormal(edge);
        wakeWritePosition_ =
            (wakeWritePosition_ + 1u) % kAmbiAcidWakeBufferSamples;

        const uint32_t activeChannels = std::min<uint32_t>(
            outputChannels, (params_.order + 1u) * (params_.order + 1u));
        const auto bodyBasis = acnSn3dBasis(bodyDirection_);
        const auto edgeBasis = acnSn3dBasis(edgeDirection_);
        const float bodyGain = 0.72f;
        const float edgeGain = 0.10f + params_.spatialSpread * 0.12f;
        for (uint32_t channel = 0u; channel < activeChannels; ++channel) {
            output[channel] = body * bodyBasis[channel] * bodyGain
                + edge * edgeBasis[channel] * edgeGain;
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
            const auto basis = acnSn3dBasis(wakeDirections_[tap]);
            for (uint32_t channel = 0u; channel < activeChannels; ++channel) {
                output[channel] += wakeSample * basis[channel];
            }
        }

        // OUT trim and the shared limiter remain outside the auditory loop.
        // The first-order component is sufficient for the broad Cube-8 score,
        // while the complete selected order remains present at the output.
        fieldListener_.processFrame(
            output, std::min<uint32_t>(activeChannels, 4u));
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

    static float quantizeListenerControl(float value)
    {
        // Listener decisions occur only at step boundaries. A small control-
        // rate quantizer prevents insignificant floating-point differences in
        // the envelope followers from accumulating through the feedback path.
        return std::round(value * 4096.0f) / 4096.0f;
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
            finiteOr(params.spatialSpread, 0.78f), 0.0f, 1.0f);
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
        const float phase = static_cast<float>(stepIndex)
            / static_cast<float>(std::max<uint32_t>(1u, params_.patternLength));
        const float azimuth = params_.centerAzimuthDeg - 180.0f
            + phase * params_.pathTurns * 360.0f;
        const float elevation = params_.elevationSpreadDeg * std::sin(
            (phase * 2.0f + 0.125f) * 2.0f * kPi);
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
            params_.rootMidiNote + current.semitoneOffset, 0, 127);
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
        listenerCutoffBiasOctaves_ = 0.0f;
        if (params_.fieldListenMode != AmbiFieldListenMode::Off
            && params_.fieldListenAmount > 0.0f) {
            const float activity = quantizeListenerControl(
                fieldListener_.activity());
            if (activity > 0.001f) {
                const Vec3 heard = fieldListener_.preferredDirection(
                    params_.fieldListenMode);
                const float steering = params_.fieldListenAmount * activity
                    * 0.68f;
                targetDirection_ = blendDirection(authored, heard, steering);
                const float preference = quantizeListenerControl(
                    fieldListener_.preference(
                        authored, params_.fieldListenMode));
                listenerCutoffBiasOctaves_ = (preference - 0.5f) * 2.0f
                    * params_.fieldListenAmount * activity * 0.32f;
            }
        }
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

    float renderOscillator()
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
        const float dcCoefficient = 1.0f - std::exp(-2.0f * kPi * 9.0f
            / static_cast<float>(sampleRate_));
        oscillatorDc_ += (raw - oscillatorDc_) * dcCoefficient;
        return raw - oscillatorDc_;
    }

    void renderFilter(float oscillator, float& body, float& edge)
    {
        const float accentOctaves = params_.accentAmount
            * accentEnvelope_ * 1.15f;
        const float cutoff = params_.cutoffHz * std::pow(2.0f,
            params_.filterEnvelopeOctaves * filterEnvelope_
                + accentOctaves + listenerCutoffBiasOctaves_);
        effectiveCutoffHz_ = std::clamp(cutoff, 25.0f,
            std::min(19000.0f, static_cast<float>(sampleRate_ * 0.41)));
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
    std::array<AmbiAcidStep, kAmbiAcidStepCount> pattern_ =
        kAmbiAcidChromeBurrowPattern;
    bool started_ = false;
    uint32_t currentStep_ = 0u;
    uint64_t completedSteps_ = 0u;
    double stepPhase_ = 0.0;
    double oscillatorPhase_ = 0.0;
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
    std::array<float, kAmbiAcidWakeBufferSamples> wakeBuffer_ {};
    uint32_t wakeWritePosition_ = 0u;
    Vec3 targetDirection_ { 1.0f, 0.0f, 0.0f };
    Vec3 bodyDirection_ { 1.0f, 0.0f, 0.0f };
    Vec3 edgeDirection_ { 1.0f, 0.0f, 0.0f };
    std::array<Vec3, kAmbiAcidWakePointCount> wakeDirections_ {};
    float listenerCutoffBiasOctaves_ = 0.0f;
    float effectiveCutoffHz_ = 310.0f;
    float limiterGain_ = 1.0f;
    float activity_ = 0.0f;
    float lastWakeEnergy_ = 0.0f;
    int64_t lastSyncedAbsoluteStep_ = std::numeric_limits<int64_t>::min();
    AmbiFieldListener fieldListener_ {};
};

} // namespace s3g
