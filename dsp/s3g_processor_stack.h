#pragma once

#include "s3g_analog_drive_circuits.h"
#include "s3g_math.h"
#include "s3g_realtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace s3g {

enum class ProcessorStackMode : uint32_t {
    Power = 0u,
    Hand,
    Lead,
    Count,
};

inline constexpr uint32_t kProcessorStackModeCount =
    static_cast<uint32_t>(ProcessorStackMode::Count);

inline const char* processorStackModeName(ProcessorStackMode mode)
{
    switch (mode) {
    case ProcessorStackMode::Power: return "POWER";
    case ProcessorStackMode::Hand: return "HAND";
    case ProcessorStackMode::Lead: return "LEAD";
    case ProcessorStackMode::Count: break;
    }
    return "POWER";
}

enum class ProcessorStackArpPattern : uint32_t {
    Off = 0u,
    Up,
    Down,
    Pendulum,
    Pedal,
    Scramble,
    Custom,
    Count,
};

inline constexpr uint32_t kProcessorStackArpPatternCount =
    static_cast<uint32_t>(ProcessorStackArpPattern::Count);

inline const char* processorStackArpPatternName(
    ProcessorStackArpPattern pattern)
{
    switch (pattern) {
    case ProcessorStackArpPattern::Off: return "OFF";
    case ProcessorStackArpPattern::Up: return "UP";
    case ProcessorStackArpPattern::Down: return "DOWN";
    case ProcessorStackArpPattern::Pendulum: return "PENDULUM";
    case ProcessorStackArpPattern::Pedal: return "PEDAL";
    case ProcessorStackArpPattern::Scramble: return "SCRAMBLE";
    case ProcessorStackArpPattern::Custom: return "CUSTOM";
    case ProcessorStackArpPattern::Count: break;
    }
    return "OFF";
}

enum class ProcessorStackScale : uint32_t {
    Chromatic = 0u,
    Phrygian,
    HarmonicMinor,
    Diminished,
    Tritone,
    Count,
};

inline constexpr uint32_t kProcessorStackScaleCount =
    static_cast<uint32_t>(ProcessorStackScale::Count);

inline const char* processorStackScaleName(ProcessorStackScale scale)
{
    switch (scale) {
    case ProcessorStackScale::Chromatic: return "CHROMATIC";
    case ProcessorStackScale::Phrygian: return "PHRYGIAN";
    case ProcessorStackScale::HarmonicMinor: return "HARM MIN";
    case ProcessorStackScale::Diminished: return "DIMINISHED";
    case ProcessorStackScale::Tritone: return "TRITONE";
    case ProcessorStackScale::Count: break;
    }
    return "PHRYGIAN";
}

enum class ProcessorStackArpRate : uint32_t {
    Eighth = 0u,
    EighthTriplet,
    Sixteenth,
    SixteenthTriplet,
    ThirtySecond,
    SixtyFourth,
    Count,
};

inline constexpr uint32_t kProcessorStackArpRateCount =
    static_cast<uint32_t>(ProcessorStackArpRate::Count);

inline const char* processorStackArpRateName(ProcessorStackArpRate rate)
{
    switch (rate) {
    case ProcessorStackArpRate::Eighth: return "1/8";
    case ProcessorStackArpRate::EighthTriplet: return "1/8T";
    case ProcessorStackArpRate::Sixteenth: return "1/16";
    case ProcessorStackArpRate::SixteenthTriplet: return "1/16T";
    case ProcessorStackArpRate::ThirtySecond: return "1/32";
    case ProcessorStackArpRate::SixtyFourth: return "1/64";
    case ProcessorStackArpRate::Count: break;
    }
    return "1/16";
}

enum class ProcessorStackCircuit : uint32_t {
    Shred = 0u,
    Wool,
    Rat,
    ZoneA,
    ZoneB,
    FuzzI,
    FuzzII,
    Diode,
    Count,
};

inline constexpr uint32_t kProcessorStackCircuitCount =
    static_cast<uint32_t>(ProcessorStackCircuit::Count);

inline const char* processorStackCircuitName(ProcessorStackCircuit circuit)
{
    if (circuit == ProcessorStackCircuit::Shred) return "SHRED";
    const uint32_t index = static_cast<uint32_t>(circuit);
    if (index > 0u && index < kProcessorStackCircuitCount) {
        return analogDriveCircuitName(
            static_cast<AnalogDriveCircuit>(index - 1u));
    }
    return "SHRED";
}

struct ProcessorStackParams {
    ProcessorStackMode mode = ProcessorStackMode::Power;
    float shape = 0.58f;
    float wire = 0.56f;
    float pick = 0.72f;
    float damping = 0.38f;
    float glideMs = 34.0f;
    float crooked = 0.36f;
    float spill = 0.32f;

    ProcessorStackArpPattern arpPattern = ProcessorStackArpPattern::Off;
    ProcessorStackScale scale = ProcessorStackScale::Phrygian;
    ProcessorStackArpRate arpRate = ProcessorStackArpRate::Sixteenth;
    uint32_t arpOctaves = 2u;
    float arpGate = 0.62f;
    uint32_t customPatternLength = 8u;
    std::array<int32_t, 8u> customPattern {{
        0, 1, 2, 4, 3, 6, 5, 1,
    }};

    ProcessorStackCircuit circuit = ProcessorStackCircuit::Rat;
    float bite = 0.56f;
    float pedalTone = 0.54f;
    float bias = 0.52f;

    float stack = 0.62f;
    float sag = 0.46f;
    float focus = 0.55f;
    float cone = 0.64f;
    float cabinet = 0.52f;
    float mic = 0.34f;

    float feedback = 0.56f;
    float proximity = 0.58f;
    float harmonic = 0.42f;
    float tracking = 0.72f;
    float polarity = 0.78f;
    float root = 0.28f;
    float chaos = 0.32f;
    float pierce = 0.68f;
    float selfListen = 0.72f;
    float outputGainDb = -12.0f;
};

inline ProcessorStackParams sanitizeProcessorStackParams(
    ProcessorStackParams params)
{
    const auto finite = [](float value, float fallback,
                            float minimum, float maximum) {
        return clamp(std::isfinite(value) ? value : fallback,
            minimum, maximum);
    };
    params.mode = static_cast<ProcessorStackMode>(std::min<uint32_t>(
        static_cast<uint32_t>(params.mode), kProcessorStackModeCount - 1u));
    params.shape = finite(params.shape, 0.58f, 0.0f, 1.0f);
    params.wire = finite(params.wire, 0.56f, 0.0f, 1.0f);
    params.pick = finite(params.pick, 0.72f, 0.0f, 1.0f);
    params.damping = finite(params.damping, 0.38f, 0.0f, 1.0f);
    params.glideMs = finite(params.glideMs, 34.0f, 0.0f, 2000.0f);
    params.crooked = finite(params.crooked, 0.36f, 0.0f, 1.0f);
    params.spill = finite(params.spill, 0.32f, 0.0f, 1.0f);
    params.arpPattern = static_cast<ProcessorStackArpPattern>(
        std::min<uint32_t>(static_cast<uint32_t>(params.arpPattern),
            kProcessorStackArpPatternCount - 1u));
    params.scale = static_cast<ProcessorStackScale>(std::min<uint32_t>(
        static_cast<uint32_t>(params.scale),
        kProcessorStackScaleCount - 1u));
    params.arpRate = static_cast<ProcessorStackArpRate>(std::min<uint32_t>(
        static_cast<uint32_t>(params.arpRate),
        kProcessorStackArpRateCount - 1u));
    params.arpOctaves = std::clamp(params.arpOctaves, 1u, 4u);
    params.arpGate = finite(params.arpGate, 0.62f, 0.05f, 1.0f);
    params.customPatternLength = std::clamp(
        params.customPatternLength, 1u,
        static_cast<uint32_t>(params.customPattern.size()));
    for (auto& step : params.customPattern) {
        step = std::clamp(step, -8, 15);
    }
    params.circuit = static_cast<ProcessorStackCircuit>(std::min<uint32_t>(
        static_cast<uint32_t>(params.circuit),
        kProcessorStackCircuitCount - 1u));
    params.bite = finite(params.bite, 0.56f, 0.0f, 1.0f);
    params.pedalTone = finite(params.pedalTone, 0.54f, 0.0f, 1.0f);
    params.bias = finite(params.bias, 0.52f, 0.0f, 1.0f);
    params.stack = finite(params.stack, 0.62f, 0.0f, 1.0f);
    params.sag = finite(params.sag, 0.46f, 0.0f, 1.0f);
    params.focus = finite(params.focus, 0.55f, 0.0f, 1.0f);
    params.cone = finite(params.cone, 0.64f, 0.0f, 1.0f);
    params.cabinet = finite(params.cabinet, 0.52f, 0.0f, 1.0f);
    params.mic = finite(params.mic, 0.34f, 0.0f, 1.0f);
    params.feedback = finite(params.feedback, 0.56f, 0.0f, 1.0f);
    params.proximity = finite(params.proximity, 0.58f, 0.0f, 1.0f);
    params.harmonic = finite(params.harmonic, 0.42f, 0.0f, 1.0f);
    params.tracking = finite(params.tracking, 0.72f, 0.0f, 1.0f);
    params.polarity = finite(params.polarity, 0.78f, 0.0f, 1.0f);
    params.root = finite(params.root, 0.28f, 0.0f, 1.0f);
    params.chaos = finite(params.chaos, 0.32f, 0.0f, 1.0f);
    params.pierce = finite(params.pierce, 0.68f, 0.0f, 1.0f);
    params.selfListen = finite(params.selfListen, 0.72f, 0.0f, 1.0f);
    params.outputGainDb = finite(
        params.outputGainDb, -12.0f, -36.0f, 6.0f);
    return params;
}

// A shared amp/speaker feedback instrument. Four deliberately sparse exciter
// lanes enter one pedal, one supply envelope, and one nonlinear speaker. The
// speaker microphones, rather than a sustained oscillator bank, close the
// governed pitch-related loop. Storage is allocated only by prepare().
class ProcessorStack {
public:
    static constexpr uint32_t kExciterCount = 4u;
    static constexpr uint32_t kSpeakerModeCount = 4u;

    void prepare(double sampleRate)
    {
        sampleRate_ = std::isfinite(sampleRate)
            ? std::clamp(sampleRate, 8000.0, 768000.0) : 48000.0;
        const size_t wireSize = static_cast<size_t>(
            std::ceil(sampleRate_ / 18.0)) + 8u;
        for (auto& lane : lanes_) {
            lane.delay.assign(std::max<size_t>(wireSize, 16u), 0.0f);
        }
        const size_t loopSize = static_cast<size_t>(
            std::ceil(sampleRate_ * 0.032)) + 8u;
        loopDelay_.assign(std::max<size_t>(loopSize, 16u), 0.0f);
        roomDelay_.assign(std::max<size_t>(loopSize, 16u), 0.0f);
        reset();
    }

    void reset()
    {
        heldNotes_.fill(false);
        heldVelocities_.fill(0.0f);
        noteOrder_.fill(-1);
        noteOrderSize_ = 0u;
        for (auto& lane : lanes_) {
            std::fill(lane.delay.begin(), lane.delay.end(), 0.0f);
            lane = resetLanePreservingDelay(std::move(lane.delay));
        }
        std::fill(loopDelay_.begin(), loopDelay_.end(), 0.0f);
        std::fill(roomDelay_.begin(), roomDelay_.end(), 0.0f);
        loopWriteIndex_ = 0u;
        roomWriteIndex_ = 0u;
        loopDelaySamples_ = static_cast<float>(sampleRate_ * 0.0025);
        stabDelaySamples_ = static_cast<float>(sampleRate_ * 0.0008);
        loopDcInput_ = 0.0f;
        loopDcOutput_ = 0.0f;
        loopLow_ = 0.0f;
        loopHighLow_ = 0.0f;
        stabDcInput_ = 0.0f;
        stabDcOutput_ = 0.0f;
        stabLow_ = 0.0f;
        stabBand_ = 0.0f;
        bodyEnvelope_ = 0.0f;
        stabEnvelope_ = 0.0f;
        selfFocus_ = 0.0f;
        loopEnvelope_ = 0.0f;
        loopActivity_ = 0.0f;
        keyGate_ = 0.0f;
        crookedHarmonicSkew_ = 0.0f;
        speakerChoke_ = 1.0f;
        speakerChokeTarget_ = 1.0f;
        pressure_ = 0.0f;
        pitchBendSemitones_ = 0.0f;
        tempoBpm_ = 120.0f;
        arpPhaseSamples_ = 0.0;
        arpStepIndex_ = 0u;
        arpStepCount_ = 0u;
        arpCurrentNote_ = -1;
        arpGateOpen_ = false;
        lastPlayedNote_ = -1;
        lastRootNote_ = 45;
        ageCounter_ = 1u;
        pedalStates_.fill({});
        activeCircuit_ = params_.circuit;
        previousCircuit_ = activeCircuit_;
        circuitFade_ = 1.0f;
        pedalDcInput_ = 0.0f;
        pedalDcOutput_ = 0.0f;
        preampPreviousInput_ = 0.0f;
        preampMemory_ = 0.0f;
        toneLow_ = 0.0f;
        toneMidLow_ = 0.0f;
        toneHighLow_ = 0.0f;
        sagEnvelope_ = 0.0f;
        transformerLow_ = 0.0f;
        coilEnvelope_ = 0.0f;
        speakerDc_ = 0.0f;
        outputDcLeft_ = 0.0f;
        outputDcRight_ = 0.0f;
        speakerModes_.fill({});
        limiterGain_ = 1.0f;
        outputGainSmoothed_ = dbToGain(params_.outputGainDb);
        outputPeak_ = 0.0f;
        signalActive_ = false;
        randomState_ = 0x8f31d26bu;
        smoothed_ = params_;
        updateCoefficients();
    }

    void setParams(ProcessorStackParams params)
    {
        const ProcessorStackMode previousMode = params_.mode;
        const ProcessorStackArpPattern previousPattern = params_.arpPattern;
        const ProcessorStackScale previousScale = params_.scale;
        const uint32_t previousOctaves = params_.arpOctaves;
        const uint32_t previousPatternLength = params_.customPatternLength;
        const auto previousCustomPattern = params_.customPattern;
        params_ = sanitizeProcessorStackParams(params);
        const bool arpChanged = params_.arpPattern != previousPattern
            || params_.scale != previousScale
            || params_.arpOctaves != previousOctaves
            || params_.customPatternLength != previousPatternLength
            || params_.customPattern != previousCustomPattern;
        if (arpChanged) resetArpeggiator(noteOrderSize_ > 0u);
        if (params_.mode != previousMode && !arpChanged) {
            rebuildVoicing(false);
        }
    }

    ProcessorStackParams params() const { return params_; }

    void noteOn(int midiNote, float velocity = 1.0f)
    {
        midiNote = std::clamp(midiNote, 0, 127);
        velocity = clamp(std::isfinite(velocity) ? velocity : 1.0f,
            0.0f, 1.0f);
        const bool repeated = heldNotes_[static_cast<size_t>(midiNote)];
        removeNoteFromOrder(midiNote);
        heldNotes_[static_cast<size_t>(midiNote)] = true;
        heldVelocities_[static_cast<size_t>(midiNote)] = velocity;
        if (noteOrderSize_ < noteOrder_.size()) {
            noteOrder_[noteOrderSize_++] = static_cast<int16_t>(midiNote);
        }

        if (params_.arpPattern != ProcessorStackArpPattern::Off) {
            resetArpeggiator(true);
        } else {
            applyCrookedGesture(midiNote, repeated);
            lastPlayedNote_ = midiNote;
        }
        if (params_.arpPattern != ProcessorStackArpPattern::Off) {
            // resetArpeggiator() performs the first generated attack.
        } else if (params_.mode == ProcessorStackMode::Hand) {
            triggerHandLane(midiNote, velocity);
        } else {
            rebuildVoicing(true);
        }
        signalActive_ = true;
    }

    void noteOff(int midiNote)
    {
        midiNote = std::clamp(midiNote, 0, 127);
        heldNotes_[static_cast<size_t>(midiNote)] = false;
        removeNoteFromOrder(midiNote);
        if (params_.arpPattern != ProcessorStackArpPattern::Off) {
            if (noteOrderSize_ > 0u) resetArpeggiator(true);
            else {
                arpCurrentNote_ = -1;
                arpGateOpen_ = false;
                for (auto& lane : lanes_) lane.held = false;
            }
        } else if (params_.mode == ProcessorStackMode::Hand) {
            for (auto& lane : lanes_) {
                if (lane.sourceNote == midiNote) lane.held = false;
            }
        } else {
            rebuildVoicing(false);
        }
    }

    void allNotesOff()
    {
        heldNotes_.fill(false);
        noteOrderSize_ = 0u;
        noteOrder_.fill(-1);
        for (auto& lane : lanes_) lane.held = false;
        arpCurrentNote_ = -1;
        arpGateOpen_ = false;
        arpPhaseSamples_ = 0.0;
    }

    void setPressure(float pressure)
    {
        pressure_ = clamp(std::isfinite(pressure) ? pressure : 0.0f,
            0.0f, 1.0f);
    }

    void setPitchBendSemitones(float semitones)
    {
        pitchBendSemitones_ = clamp(
            std::isfinite(semitones) ? semitones : 0.0f, -24.0f, 24.0f);
    }

    void setTempoBpm(float bpm)
    {
        tempoBpm_ = clamp(std::isfinite(bpm) ? bpm : 120.0f,
            20.0f, 400.0f);
    }

    void processFrame(float& left, float& right)
    {
        if (!signalActive_ && noteOrderSize_ == 0u && !anyLaneActive()
            && loopActivity_ <= 1.0e-7f) {
            left = 0.0f;
            right = 0.0f;
            return;
        }
        smoothParams();
        processArpeggiator();
        const bool keysHeld = noteOrderSize_ > 0u;
        const float gateTarget = keysHeld ? 1.0f : 0.0f;
        const float gateCoefficient = keysHeld
            ? gateAttackCoefficient_
            : onePoleSeconds(0.08f + smoothed_.spill * 2.2f);
        keyGate_ += (gateTarget - keyGate_) * gateCoefficient;
        keyGate_ = flushDenormal(keyGate_);
        speakerChokeTarget_ += (1.0f - speakerChokeTarget_)
            * onePoleSeconds(0.024f + smoothed_.crooked * 0.045f);
        const float chokeTime = speakerChokeTarget_ < speakerChoke_
            ? 0.0008f : 0.0025f;
        speakerChoke_ += (speakerChokeTarget_ - speakerChoke_)
            * onePoleSeconds(chokeTime);
        crookedHarmonicSkew_ *= crookedSkewDecay_;
        crookedHarmonicSkew_ = flushDenormal(crookedHarmonicSkew_);

        float excitation = 0.0f;
        float rootWitness = 0.0f;
        float activeWeight = 0.0f;
        for (auto& lane : lanes_) {
            if (!lane.active) continue;
            float laneRoot = 0.0f;
            const float sample = processExciter(lane, laneRoot);
            excitation += sample * lane.gain;
            rootWitness += laneRoot * lane.gain;
            activeWeight += lane.gain * lane.gain;
        }
        if (activeWeight > 1.0f) {
            const float normalization = 1.0f / std::sqrt(activeWeight);
            excitation *= normalization;
            rootWitness *= normalization;
        }

        const float feedbackReturn = readFeedbackReturn();
        // SPILL lengthens the release of this gate; loop activity must not
        // reopen it, otherwise a hot speaker state can become a permanent
        // no-input oscillator after the final key release.
        const float sourceGate = !keysHeld && keyGate_ < 2.0e-4f
            ? 0.0f : keyGate_;
        const float excess = std::max(0.0f, loopEnvelope_ - 0.52f);
        const float governor = 1.0f / (1.0f + excess * 13.0f);
        const float requestedFeedback = smoothed_.feedback
            * (0.42f + smoothed_.feedback * 0.67f)
            * lerp(0.74f, 1.18f, smoothed_.proximity)
            * (1.0f + pressure_ * 0.24f);
        const float signedPolarity = (smoothed_.polarity - 0.5f) * 2.0f;
        const float loopInput = feedbackReturn * requestedFeedback
            * signedPolarity * governor * sourceGate;
        const float combined = clamp(excitation + loopInput, -5.0f, 5.0f);

        const float pedal = processPedal(combined);
        const float rootAmount = smoothed_.root
            * (1.0f - smoothed_.chaos * 0.72f);
        const float ampInput = pedal + rootWitness * rootAmount * 0.38f;
        const float power = processAmplifier(ampInput);
        float micA = 0.0f;
        float micB = 0.0f;
        processSpeaker(power * speakerChoke_, micA, micB);

        const float roomSamples = clamp(static_cast<float>(sampleRate_)
                * lerp(0.0065f, 0.00065f, smoothed_.proximity),
            2.0f, static_cast<float>(roomDelay_.size() - 2u));
        const float room = readDelay(roomDelay_, roomWriteIndex_, roomSamples);
        roomDelay_[roomWriteIndex_] = flushDenormal(
            clamp(micB * 0.82f + micA * 0.18f, -3.0f, 3.0f));
        roomWriteIndex_ = (roomWriteIndex_ + 1u) % roomDelay_.size();
        const float sideMic = micB * 0.72f + room * 0.28f;
        const float feedbackMic = lerp(micA, sideMic, smoothed_.mic)
            + (sideMic - micA) * smoothed_.chaos * 0.32f;
        loopDelay_[loopWriteIndex_] = flushDenormal(
            std::tanh(feedbackMic * (0.86f + smoothed_.cone * 0.42f)));
        loopWriteIndex_ = (loopWriteIndex_ + 1u) % loopDelay_.size();

        const float width = 0.08f + smoothed_.mic * 0.62f;
        float frameLeft = micA + (micA - sideMic) * width;
        float frameRight = micA - (micA - sideMic) * width;
        const float dcCoefficient = outputDcCoefficient_;
        outputDcLeft_ += (frameLeft - outputDcLeft_) * dcCoefficient;
        outputDcRight_ += (frameRight - outputDcRight_) * dcCoefficient;
        frameLeft -= outputDcLeft_;
        frameRight -= outputDcRight_;

        outputGainSmoothed_ += (dbToGain(smoothed_.outputGainDb)
            - outputGainSmoothed_) * outputGainCoefficient_;
        frameLeft *= outputGainSmoothed_;
        frameRight *= outputGainSmoothed_;
        const float peak = std::max(std::abs(frameLeft), std::abs(frameRight));
        const float limiterTarget = peak > 0.94f ? 0.94f / peak : 1.0f;
        if (limiterTarget < limiterGain_) limiterGain_ = limiterTarget;
        else limiterGain_ += (limiterTarget - limiterGain_)
            * limiterReleaseCoefficient_;
        left = safeOutput(frameLeft * limiterGain_);
        right = safeOutput(frameRight * limiterGain_);
        outputPeak_ += (std::max(std::abs(left), std::abs(right)) - outputPeak_)
            * (peak > outputPeak_ ? meterAttackCoefficient_
                                  : meterReleaseCoefficient_);
        outputPeak_ = flushDenormal(outputPeak_);

        const bool laneActive = anyLaneActive();
        signalActive_ = keysHeld || laneActive || keyGate_ > 1.0e-5f
            || loopActivity_ > 1.0e-5f || outputPeak_ > 1.0e-5f;
        if (!signalActive_) clearSignalState();
    }

    void processBlock(float* left, float* right, uint32_t frames)
    {
        for (uint32_t frame = 0u; frame < frames; ++frame) {
            float frameLeft = 0.0f;
            float frameRight = 0.0f;
            processFrame(frameLeft, frameRight);
            if (left) left[frame] = frameLeft;
            if (right) right[frame] = frameRight;
        }
    }

    bool active() const { return signalActive_; }
    uint32_t heldNoteCount() const { return noteOrderSize_; }
    float feedbackActivity() const { return loopActivity_; }
    float outputPeak() const { return outputPeak_; }
    float loopDelaySamples() const { return loopDelaySamples_; }
    float sagEnvelope() const { return sagEnvelope_; }
    float feedbackBodyActivity() const { return bodyEnvelope_; }
    float feedbackStabActivity() const { return stabEnvelope_; }
    uint64_t arpStepCount() const { return arpStepCount_; }
    int arpCurrentNote() const { return arpCurrentNote_; }

private:
    struct ExciterLane {
        std::vector<float> delay;
        size_t writeIndex = 0u;
        int sourceNote = -1;
        float noteOffset = 0.0f;
        float gain = 0.0f;
        float velocity = 0.0f;
        float currentFrequency = 110.0f;
        float targetFrequency = 110.0f;
        float overshootSemitones = 0.0f;
        float phase = 0.0f;
        float pickEnvelope = 0.0f;
        float pickSmoothed = 0.0f;
        float amplitude = 0.0f;
        float wireLow = 0.0f;
        float pickNoiseLow = 0.0f;
        float pickupPrevious = 0.0f;
        float pickupVelocity = 0.0f;
        float pickupLow = 0.0f;
        float rootLow = 0.0f;
        float outputSmoothed = 0.0f;
        float dispersionInput = 0.0f;
        float dispersionOutput = 0.0f;
        float pickDirection = 1.0f;
        uint32_t random = 1u;
        uint64_t age = 0u;
        bool active = false;
        bool held = false;
    };

    struct DriveState {
        float memory = 0.0f;
        float low = 0.0f;
        float high = 0.0f;
        float envelope = 0.0f;
    };

    struct SpeakerModeState {
        float first = 0.0f;
        float second = 0.0f;
    };

    static ExciterLane resetLanePreservingDelay(std::vector<float> delay)
    {
        ExciterLane lane;
        lane.delay = std::move(delay);
        return lane;
    }

    static float dbToGain(float db)
    {
        return db <= -120.0f ? 0.0f : std::pow(10.0f, db * 0.05f);
    }

    static float safeOutput(float value)
    {
        if (!std::isfinite(value)) return 0.0f;
        return clamp(value, -1.0f, 1.0f);
    }

    float onePoleHz(float frequency, float rateScale = 1.0f) const
    {
        const float sr = static_cast<float>(sampleRate_) * rateScale;
        return 1.0f - std::exp(-2.0f * kPi
            * std::min(frequency, sr * 0.45f) / sr);
    }

    float onePoleSeconds(float seconds, float rateScale = 1.0f) const
    {
        return 1.0f - std::exp(-1.0f / std::max(1.0f,
            seconds * static_cast<float>(sampleRate_) * rateScale));
    }

    static float asymmetric(float input, float gain, float bias)
    {
        const float positive = std::tanh((input + bias) * gain);
        const float negative = std::tanh((input - bias) * gain * 0.79f);
        const float zero = 0.5f * (std::tanh(bias * gain)
            + std::tanh(-bias * gain * 0.79f));
        return (positive + negative) * 0.5f - zero;
    }

    static float fold(float value)
    {
        const float shifted = value + 1.0f;
        const float wrapped = shifted
            - 4.0f * std::floor(shifted * 0.25f);
        return wrapped <= 2.0f ? wrapped - 1.0f : 3.0f - wrapped;
    }

    static float readDelay(const std::vector<float>& delay,
        size_t writeIndex, float delaySamples)
    {
        if (delay.size() < 4u) return 0.0f;
        const float size = static_cast<float>(delay.size());
        float read = static_cast<float>(writeIndex) - delaySamples;
        while (read < 0.0f) read += size;
        while (read >= size) read -= size;
        const auto first = static_cast<size_t>(read);
        const auto second = (first + 1u) % delay.size();
        const float fraction = read - static_cast<float>(first);
        return delay[first] + (delay[second] - delay[first]) * fraction;
    }

    float nextNoise(uint32_t& state)
    {
        state ^= state << 13u;
        state ^= state >> 17u;
        state ^= state << 5u;
        return static_cast<float>((state >> 8u) & 0x00ffffffu)
            / 8388607.5f - 1.0f;
    }

    float noteFrequency(float midiNote) const
    {
        return clamp(440.0f * std::exp2(
            (midiNote + pitchBendSemitones_ - 69.0f) / 12.0f),
            18.0f, static_cast<float>(sampleRate_ * 0.20));
    }

    void removeNoteFromOrder(int note)
    {
        for (uint32_t index = 0u; index < noteOrderSize_; ++index) {
            if (noteOrder_[index] != note) continue;
            for (uint32_t move = index + 1u; move < noteOrderSize_; ++move) {
                noteOrder_[move - 1u] = noteOrder_[move];
            }
            --noteOrderSize_;
            noteOrder_[noteOrderSize_] = -1;
            return;
        }
    }

    int latestHeldNote() const
    {
        return noteOrderSize_ > 0u ? noteOrder_[noteOrderSize_ - 1u] : -1;
    }

    static uint32_t scaleDegreeCount(ProcessorStackScale scale)
    {
        switch (scale) {
        case ProcessorStackScale::Chromatic: return 12u;
        case ProcessorStackScale::Phrygian: return 7u;
        case ProcessorStackScale::HarmonicMinor: return 7u;
        case ProcessorStackScale::Diminished: return 8u;
        case ProcessorStackScale::Tritone: return 6u;
        case ProcessorStackScale::Count: break;
        }
        return 7u;
    }

    static int scaleSemitone(ProcessorStackScale scale, uint32_t degree)
    {
        static constexpr std::array<int, 12u> chromatic {{
            0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11,
        }};
        static constexpr std::array<int, 7u> phrygian {{
            0, 1, 3, 5, 7, 8, 10,
        }};
        static constexpr std::array<int, 7u> harmonicMinor {{
            0, 2, 3, 5, 7, 8, 11,
        }};
        static constexpr std::array<int, 8u> diminished {{
            0, 2, 3, 5, 6, 8, 9, 11,
        }};
        static constexpr std::array<int, 6u> tritone {{
            0, 1, 4, 6, 7, 10,
        }};
        switch (scale) {
        case ProcessorStackScale::Chromatic:
            return chromatic[degree % chromatic.size()];
        case ProcessorStackScale::Phrygian:
            return phrygian[degree % phrygian.size()];
        case ProcessorStackScale::HarmonicMinor:
            return harmonicMinor[degree % harmonicMinor.size()];
        case ProcessorStackScale::Diminished:
            return diminished[degree % diminished.size()];
        case ProcessorStackScale::Tritone:
            return tritone[degree % tritone.size()];
        case ProcessorStackScale::Count: break;
        }
        return 0;
    }

    static int signedScaleSemitone(ProcessorStackScale scale, int degree)
    {
        const int count = static_cast<int>(scaleDegreeCount(scale));
        int octave = degree / count;
        int index = degree % count;
        if (index < 0) {
            index += count;
            --octave;
        }
        return scaleSemitone(scale, static_cast<uint32_t>(index))
            + octave * 12;
    }

    static float arpStepBeats(ProcessorStackArpRate rate)
    {
        switch (rate) {
        case ProcessorStackArpRate::Eighth: return 0.5f;
        case ProcessorStackArpRate::EighthTriplet: return 1.0f / 3.0f;
        case ProcessorStackArpRate::Sixteenth: return 0.25f;
        case ProcessorStackArpRate::SixteenthTriplet: return 1.0f / 6.0f;
        case ProcessorStackArpRate::ThirtySecond: return 0.125f;
        case ProcessorStackArpRate::SixtyFourth: return 0.0625f;
        case ProcessorStackArpRate::Count: break;
        }
        return 0.25f;
    }

    uint32_t arpSequencePosition(uint64_t step) const
    {
        const uint32_t degrees = scaleDegreeCount(params_.scale);
        const uint32_t length = std::max(1u, degrees * params_.arpOctaves);
        const uint32_t position = static_cast<uint32_t>(
            step % static_cast<uint64_t>(length));
        switch (params_.arpPattern) {
        case ProcessorStackArpPattern::Down:
            return length - 1u - position;
        case ProcessorStackArpPattern::Pendulum: {
            if (length <= 1u) return 0u;
            const uint32_t cycle = length * 2u - 2u;
            const uint32_t pendulum = static_cast<uint32_t>(
                step % static_cast<uint64_t>(cycle));
            return pendulum < length ? pendulum : cycle - pendulum;
        }
        case ProcessorStackArpPattern::Pedal:
            return (step & 1u) == 0u ? 0u
                : 1u + static_cast<uint32_t>((step / 2u)
                    % static_cast<uint64_t>(std::max(1u, length - 1u)));
        case ProcessorStackArpPattern::Scramble:
            // An odd stride walks every position before repeating while
            // avoiding a stored/random sequence in the audio thread.
            return static_cast<uint32_t>((step * 5u + step / 3u)
                % static_cast<uint64_t>(length));
        case ProcessorStackArpPattern::Custom:
            return position;
        case ProcessorStackArpPattern::Off:
        case ProcessorStackArpPattern::Up:
        case ProcessorStackArpPattern::Count:
            return position;
        }
        return position;
    }

    int arpeggiatedNote(uint64_t step) const
    {
        const int root = latestHeldNote();
        if (root < 0) return -1;
        if (params_.arpPattern == ProcessorStackArpPattern::Custom) {
            const uint32_t length = params_.customPatternLength;
            const uint32_t index = static_cast<uint32_t>(step
                % static_cast<uint64_t>(length));
            int result = root + signedScaleSemitone(
                params_.scale, params_.customPattern[index]);
            while (result > 127) result -= 12;
            while (result < 0) result += 12;
            return std::clamp(result, 0, 127);
        }
        const uint32_t degrees = scaleDegreeCount(params_.scale);
        const uint32_t position = arpSequencePosition(step);
        const int offset = scaleSemitone(params_.scale, position % degrees)
            + 12 * static_cast<int>(position / degrees);
        int result = root + offset;
        while (result > 127) result -= 12;
        return std::clamp(result, 0, 127);
    }

    void advanceArpeggiator()
    {
        const int note = arpeggiatedNote(arpStepIndex_);
        if (note < 0) return;
        applyCrookedGesture(note, note == lastPlayedNote_);
        lastPlayedNote_ = note;
        arpCurrentNote_ = note;
        arpGateOpen_ = true;
        rebuildVoicing(true);
        ++arpStepIndex_;
        ++arpStepCount_;
    }

    void resetArpeggiator(bool trigger)
    {
        arpPhaseSamples_ = 0.0;
        arpStepIndex_ = 0u;
        arpCurrentNote_ = -1;
        arpGateOpen_ = false;
        if (trigger && params_.arpPattern != ProcessorStackArpPattern::Off
            && noteOrderSize_ > 0u) {
            advanceArpeggiator();
        } else if (params_.arpPattern == ProcessorStackArpPattern::Off
            && noteOrderSize_ > 0u) {
            rebuildVoicing(false);
        }
    }

    void closeArpeggiatorGate()
    {
        arpGateOpen_ = false;
        for (auto& lane : lanes_) lane.held = false;
    }

    void processArpeggiator()
    {
        if (params_.arpPattern == ProcessorStackArpPattern::Off
            || noteOrderSize_ == 0u) return;
        const double stepSamples = std::max(1.0,
            static_cast<double>(sampleRate_) * 60.0
                / static_cast<double>(tempoBpm_)
                * static_cast<double>(arpStepBeats(params_.arpRate)));
        arpPhaseSamples_ += 1.0;
        const double gateSamples = stepSamples
            * static_cast<double>(smoothed_.arpGate);
        if (arpGateOpen_ && arpPhaseSamples_ >= gateSamples) {
            closeArpeggiatorGate();
        }
        if (arpPhaseSamples_ >= stepSamples) {
            arpPhaseSamples_ -= stepSamples;
            advanceArpeggiator();
        }
    }

    void applyCrookedGesture(int midiNote, bool repeated)
    {
        const int interval = lastPlayedNote_ >= 0
            ? midiNote - lastPlayedNote_ : 0;
        const int absolute = std::abs(interval);
        if (repeated || interval == 0) {
            speakerChokeTarget_ = std::min(speakerChokeTarget_,
                1.0f - params_.crooked * 0.78f);
        }
        const bool contrary = absolute == 1 || absolute == 2
            || absolute == 6;
        if (contrary) {
            const float direction = interval < 0 ? -1.0f : 1.0f;
            crookedHarmonicSkew_ = direction * params_.crooked
                * (absolute == 6 ? 1.8f : 0.92f);
        } else if (absolute >= 7) {
            const float direction = interval < 0 ? -1.0f : 1.0f;
            crookedHarmonicSkew_ = direction * params_.crooked
                * std::min(1.6f, static_cast<float>(absolute) * 0.08f);
        }
    }

    void configureLane(ExciterLane& lane, int sourceNote, float noteOffset,
        float gain, float velocity, bool retrigger, bool held)
    {
        const float target = noteFrequency(
            static_cast<float>(sourceNote) + noteOffset);
        if (!lane.active) lane.currentFrequency = target;
        lane.sourceNote = sourceNote;
        lane.noteOffset = noteOffset;
        lane.targetFrequency = target;
        lane.gain = gain;
        lane.velocity = velocity;
        lane.held = held;
        lane.active = true;
        lane.age = ageCounter_++;
        if (retrigger) {
            lane.pickEnvelope = 1.0f;
            lane.amplitude = std::max(lane.amplitude,
                0.18f + velocity * 0.82f);
            lane.pickDirection = -lane.pickDirection;
            lane.random ^= static_cast<uint32_t>(
                static_cast<uint32_t>(sourceNote + 1) * 0x9e3779b9u
                    + static_cast<uint32_t>(lane.age));
            const int interval = lastRootNote_ >= 0
                ? sourceNote - lastRootNote_ : 0;
            if (std::abs(interval) >= 7) {
                lane.overshootSemitones = std::copysign(
                    params_.crooked * std::min(2.8f,
                        0.13f * static_cast<float>(std::abs(interval))),
                    static_cast<float>(interval));
            }
        }
    }

    void rebuildVoicing(bool retrigger)
    {
        const bool arpeggiating =
            params_.arpPattern != ProcessorStackArpPattern::Off;
        const int rootNote = arpeggiating ? arpCurrentNote_ : latestHeldNote();
        if (rootNote < 0) {
            for (auto& lane : lanes_) lane.held = false;
            return;
        }
        const int heldRoot = latestHeldNote();
        const float velocity = heldRoot >= 0
            ? heldVelocities_[static_cast<size_t>(heldRoot)] : 0.8f;
        if (params_.mode == ProcessorStackMode::Lead
            || (arpeggiating && params_.mode == ProcessorStackMode::Hand)) {
            configureLane(lanes_[0u], rootNote, 0.0f, 1.0f,
                velocity, retrigger, !arpeggiating || arpGateOpen_);
            for (uint32_t lane = 1u; lane < lanes_.size(); ++lane) {
                lanes_[lane].held = false;
            }
        } else if (params_.mode == ProcessorStackMode::Power) {
            const float shape = params_.shape;
            const float rootGain = lerp(1.0f, 0.68f,
                std::max(0.0f, (shape - 0.72f) / 0.28f));
            const float fifthGain = 0.52f + shape * 0.24f;
            const float octavePosition = clamp(
                (shape - 0.22f) / 0.52f, 0.0f, 1.0f);
            const float octaveGain = octavePosition * octavePosition
                * (3.0f - 2.0f * octavePosition) * 0.72f;
            configureLane(lanes_[0u], rootNote, 0.0f, rootGain,
                velocity, retrigger, !arpeggiating || arpGateOpen_);
            configureLane(lanes_[1u], rootNote, 7.0f, fifthGain,
                velocity * 0.94f, retrigger,
                !arpeggiating || arpGateOpen_);
            configureLane(lanes_[2u], rootNote, 12.0f, octaveGain,
                velocity * 0.88f, retrigger,
                octaveGain > 1.0e-4f
                    && (!arpeggiating || arpGateOpen_));
            lanes_[3u].held = false;
        }
        lastRootNote_ = rootNote;
    }

    void triggerHandLane(int note, float velocity)
    {
        ExciterLane* selected = nullptr;
        for (auto& lane : lanes_) {
            if (lane.active && lane.sourceNote == note) {
                selected = &lane;
                break;
            }
            if (!selected && !lane.active) selected = &lane;
        }
        if (!selected) {
            selected = &*std::min_element(lanes_.begin(), lanes_.end(),
                [](const ExciterLane& first, const ExciterLane& second) {
                    if (first.held != second.held) return !first.held;
                    return first.age < second.age;
                });
        }
        configureLane(*selected, note, 0.0f, 1.0f,
            velocity, true, true);
        lastRootNote_ = note;
    }

    float processExciter(ExciterLane& lane, float& rootWitness)
    {
        lane.targetFrequency = noteFrequency(
            static_cast<float>(lane.sourceNote) + lane.noteOffset);
        const float glideSeconds = smoothed_.glideMs * 0.001f;
        float pitchCoefficient = glideSeconds <= 1.0e-6f
            ? 1.0f : onePoleSeconds(glideSeconds);
        if (lane.sourceNote < lastRootNote_ && smoothed_.crooked > 0.0f) {
            pitchCoefficient *= lerp(1.0f, 0.42f, smoothed_.crooked);
        }
        const float bentTarget = lane.targetFrequency * std::exp2(
            lane.overshootSemitones / 12.0f);
        lane.currentFrequency += (bentTarget - lane.currentFrequency)
            * pitchCoefficient;
        lane.overshootSemitones *= crookedPitchDecay_;
        lane.overshootSemitones = flushDenormal(lane.overshootSemitones);

        lane.phase += 2.0f * kPi * lane.currentFrequency
            / static_cast<float>(sampleRate_);
        if (lane.phase >= 2.0f * kPi) {
            lane.phase -= 2.0f * kPi
                * std::floor(lane.phase / (2.0f * kPi));
        }
        const float fundamental = std::sin(lane.phase);
        const float partialTwo = std::sin(lane.phase * 2.0f) * 0.24f;
        const float partialThree = std::sin(lane.phase * 3.0f) * 0.11f;

        const float pickMilliseconds = lerp(4.2f, 0.65f, smoothed_.pick);
        lane.pickEnvelope *= std::exp(-1.0f / std::max(1.0f,
            static_cast<float>(sampleRate_) * pickMilliseconds * 0.001f));
        lane.pickEnvelope = flushDenormal(lane.pickEnvelope);
        const float pickSlewTime = lane.pickEnvelope > lane.pickSmoothed
            ? 0.00022f : 0.00008f;
        lane.pickSmoothed += (lane.pickEnvelope - lane.pickSmoothed)
            * onePoleSeconds(pickSlewTime);
        lane.pickSmoothed = flushDenormal(lane.pickSmoothed);
        const float noise = nextNoise(lane.random);
        lane.pickNoiseLow += (noise - lane.pickNoiseLow)
            * onePoleHz(lerp(1400.0f, 9800.0f, smoothed_.pick));
        const float pickPacket = lane.pickSmoothed * lane.pickDirection
            * (lane.pickNoiseLow * (0.34f + smoothed_.pick * 0.28f)
                + fundamental * 0.46f + partialTwo * smoothed_.pick)
            * (0.28f + lane.velocity * 0.82f);

        const float releaseSeconds = 0.025f + smoothed_.spill * 0.48f;
        const float amplitudeTarget = lane.held ? 1.0f : 0.0f;
        const float amplitudeCoefficient = amplitudeTarget > lane.amplitude
            ? onePoleSeconds(0.006f) : onePoleSeconds(releaseSeconds);
        lane.amplitude += (amplitudeTarget - lane.amplitude)
            * amplitudeCoefficient;
        lane.amplitude = flushDenormal(lane.amplitude);

        const float maximumDelay = static_cast<float>(lane.delay.size() - 2u);
        const float delaySamples = clamp(
            static_cast<float>(sampleRate_) / lane.currentFrequency,
            2.0f, maximumDelay);
        const float delayed = readDelay(lane.delay, lane.writeIndex,
            delaySamples);
        const float pickupPosition = lerp(0.11f, 0.31f,
            1.0f - smoothed_.pick);
        const float pickupTap = readDelay(lane.delay, lane.writeIndex,
            clamp(delaySamples * pickupPosition, 1.0f, maximumDelay));
        const float wireCutoff = lerp(720.0f, 7600.0f,
            1.0f - smoothed_.damping);
        const float dispersion = 0.04f + smoothed_.wire * 0.18f
            + clamp(lane.currentFrequency / 4200.0f, 0.0f, 0.12f);
        const float dispersed = -dispersion * delayed
            + lane.dispersionInput + dispersion * lane.dispersionOutput;
        lane.dispersionInput = delayed;
        lane.dispersionOutput = flushDenormal(dispersed);
        lane.wireLow += (lane.dispersionOutput - lane.wireLow)
            * onePoleHz(wireCutoff);
        const float wireDecay = 0.985f
            + (1.0f - smoothed_.damping) * 0.0138f;
        const float injection = pickPacket
            * (0.54f + smoothed_.wire * 0.54f);
        lane.delay[lane.writeIndex] = flushDenormal(std::tanh(
            lane.wireLow * wireDecay + injection));
        lane.writeIndex = (lane.writeIndex + 1u) % lane.delay.size();

        const float comb = delayed - pickupTap;
        const float displacement = delayed * 0.74f
            + comb * (0.34f + smoothed_.pick * 0.34f);
        const float velocityScale = clamp(delaySamples * 0.06f,
            1.0f, 18.0f);
        const float pickupVelocityRaw = (displacement - lane.pickupPrevious)
            * velocityScale;
        lane.pickupPrevious = displacement;
        const float boundedVelocity = std::tanh(pickupVelocityRaw * 0.7f)
            / 0.7f;
        lane.pickupVelocity += (boundedVelocity - lane.pickupVelocity)
            * onePoleHz(8200.0f);
        const float pickup = displacement * 0.72f
            + lane.pickupVelocity * (0.12f + smoothed_.pick * 0.20f);
        lane.pickupLow += (pickup - lane.pickupLow)
            * onePoleHz(lerp(3800.0f, 11200.0f, smoothed_.pick));
        lane.rootLow += (lane.pickupLow - lane.rootLow)
            * onePoleHz(clamp(lane.currentFrequency * 1.45f,
                45.0f, 1800.0f));
        const float stringRelease = 0.22f + lane.amplitude * 0.78f;
        const float transientTonal = (fundamental + partialTwo + partialThree)
            * lane.pickSmoothed * 0.12f;
        const float output = lerp(pickPacket + transientTonal,
            lane.pickupLow * stringRelease + pickPacket * 0.18f,
            smoothed_.wire);
        lane.outputSmoothed += (output - lane.outputSmoothed)
            * onePoleHz(11800.0f);
        lane.outputSmoothed = flushDenormal(lane.outputSmoothed);
        rootWitness = lane.rootLow * lane.amplitude * 0.42f;
        if (!lane.held && lane.pickEnvelope < 1.0e-6f
            && lane.pickSmoothed < 1.0e-6f
            && lane.amplitude < 1.0e-6f
            && std::abs(lane.wireLow) < 1.0e-6f
            && std::abs(lane.pickupLow) < 1.0e-6f
            && std::abs(lane.outputSmoothed) < 1.0e-6f) {
            lane.active = false;
            lane.gain = 0.0f;
        }
        return lane.outputSmoothed * 0.34f;
    }

    float processSelectedCircuit(ProcessorStackCircuit circuit,
        float input, float rateScale)
    {
        const uint32_t index = std::min<uint32_t>(
            static_cast<uint32_t>(circuit), kProcessorStackCircuitCount - 1u);
        const float drive = smoothed_.bite;
        const float bias = (smoothed_.bias - 0.5f) * 0.18f;
        if (circuit == ProcessorStackCircuit::Shred) {
            const float pressured = std::tanh(input * (1.0f + drive * 9.0f));
            return lerp(pressured,
                fold(pressured * (1.0f + drive * 5.5f)),
                drive * drive * 0.72f);
        }
        return processAnalogDriveCircuit(
            static_cast<AnalogDriveCircuit>(index - 1u),
            pedalStates_[index], input, drive, smoothed_.pedalTone,
            bias, static_cast<float>(sampleRate_) * rateScale);
    }

    float processPedal(float input)
    {
        if (params_.circuit != activeCircuit_) {
            previousCircuit_ = activeCircuit_;
            activeCircuit_ = params_.circuit;
            circuitFade_ = 0.0f;
        }
        const auto render = [&](ProcessorStackCircuit circuit, float sample) {
            return processSelectedCircuit(circuit, sample, 2.0f);
        };
        const float midpoint = 0.5f * (preampPreviousInput_ + input);
        float active = 0.5f * (render(activeCircuit_, midpoint)
            + render(activeCircuit_, input));
        if (circuitFade_ < 1.0f) {
            const float previous = 0.5f * (render(previousCircuit_, midpoint)
                + render(previousCircuit_, input));
            active = lerp(previous, active, circuitFade_);
            circuitFade_ = std::min(1.0f,
                circuitFade_ + circuitFadeCoefficient_);
        }
        preampPreviousInput_ = input;
        const float starvation = lerp(0.62f, 1.0f, smoothed_.bias);
        const float raw = clamp(active * starvation, -2.5f, 2.5f);
        const float dcBlocked = raw - pedalDcInput_
            + pedalDcPole_ * pedalDcOutput_;
        pedalDcInput_ = raw;
        pedalDcOutput_ = flushDenormal(dcBlocked);
        return pedalDcOutput_;
    }

    float processAmplifier(float input)
    {
        const auto oversampledStage = [&](float sample) {
            const float stack = smoothed_.stack;
            const float focus = smoothed_.focus;
            const float bias = 0.018f + stack * 0.042f;
            float first = asymmetric(sample,
                1.0f + stack * stack * 13.0f, bias);
            preampMemory_ += (first - preampMemory_)
                * onePoleHz(lerp(2600.0f, 10500.0f, focus), 2.0f);
            float second = asymmetric(preampMemory_,
                1.0f + stack * 6.4f, -bias * 0.64f);

            toneLow_ += (second - toneLow_)
                * onePoleHz(lerp(110.0f, 240.0f, focus), 2.0f);
            toneMidLow_ += (second - toneMidLow_)
                * onePoleHz(lerp(520.0f, 1700.0f, focus), 2.0f);
            toneHighLow_ += (second - toneHighLow_)
                * onePoleHz(lerp(2600.0f, 7200.0f, focus), 2.0f);
            const float low = toneLow_;
            const float middle = toneMidLow_ - toneLow_;
            const float high = toneHighLow_ - toneMidLow_;
            const float voiced = low * lerp(1.18f, 0.76f, focus)
                + middle * lerp(0.72f, 1.72f, focus)
                + high * lerp(0.48f, 1.34f, focus);
            sagEnvelope_ += (std::abs(voiced) - sagEnvelope_)
                * (std::abs(voiced) > sagEnvelope_
                    ? sagAttackCoefficient_ : sagReleaseCoefficient_);
            const float rail = 1.0f / (1.0f + sagEnvelope_
                * smoothed_.sag * 3.4f);
            const float positive = std::tanh(voiced
                * (1.2f + stack * 5.6f) * rail);
            const float negative = std::tanh(voiced
                * (1.0f + stack * 4.7f) * rail);
            const float crossover = voiced >= 0.0f ? positive : negative;
            transformerLow_ += (crossover - transformerLow_)
                * onePoleHz(82.0f, 2.0f);
            return crossover + transformerLow_ * 0.12f;
        };
        const float midpoint = 0.5f * (amplifierPreviousInput_ + input);
        const float first = oversampledStage(midpoint);
        const float second = oversampledStage(input);
        amplifierPreviousInput_ = input;
        return flushDenormal(0.5f * (first + second));
    }

    void processSpeaker(float input, float& micA, float& micB)
    {
        const float cone = smoothed_.cone;
        const float cabinet = smoothed_.cabinet;
        coilEnvelope_ += (std::abs(input) - coilEnvelope_)
            * (std::abs(input) > coilEnvelope_
                ? coilAttackCoefficient_ : coilReleaseCoefficient_);
        const float compression = 1.0f
            / (1.0f + coilEnvelope_ * cone * 1.9f);
        const float displacementShift = 1.0f
            + clamp(speakerModes_[0u].first, -1.0f, 1.0f)
                * cone * 0.035f;
        const std::array<float, kSpeakerModeCount> baseFrequencies {{
            lerp(78.0f, 132.0f, 1.0f - cabinet),
            lerp(310.0f, 560.0f, 1.0f - cabinet),
            lerp(760.0f, 1480.0f, smoothed_.focus),
            lerp(2200.0f, 4300.0f, smoothed_.focus),
        }};
        const std::array<float, kSpeakerModeCount> radii {{
            0.992f, 0.976f, 0.958f, 0.925f,
        }};
        const std::array<float, kSpeakerModeCount> gains {{
            0.24f, 0.20f, 0.15f, 0.09f,
        }};
        std::array<float, kSpeakerModeCount> modes {};
        for (uint32_t index = 0u; index < speakerModes_.size(); ++index) {
            const float frequency = clamp(baseFrequencies[index]
                    * (index == 0u ? displacementShift : 1.0f),
                18.0f, static_cast<float>(sampleRate_ * 0.42));
            const float radius = std::pow(radii[index],
                48000.0f / static_cast<float>(sampleRate_));
            const float coefficient = 2.0f * radius
                * std::cos(2.0f * kPi * frequency
                    / static_cast<float>(sampleRate_));
            const float resonant = input * (1.0f - radius) * gains[index]
                + coefficient * speakerModes_[index].first
                - radius * radius * speakerModes_[index].second;
            speakerModes_[index].second = speakerModes_[index].first;
            speakerModes_[index].first = flushDenormal(
                clamp(resonant, -3.0f, 3.0f));
            modes[index] = speakerModes_[index].first;
        }
        const float modal = modes[0u] * 1.24f + modes[1u] * 1.08f
            + modes[2u] * 0.92f + modes[3u] * 0.72f;
        speakerDc_ += (modal - speakerDc_) * speakerDcCoefficient_;
        const float displacement = modal - speakerDc_;
        const float breakup = std::tanh((input - modes[0u] * 0.34f)
            * (1.0f + cone * cone * 8.0f)) * cone * 0.24f;
        const float driven = (input * 0.42f + displacement * 2.2f
            + breakup) * compression;
        const float normalization = std::max(0.25f,
            std::tanh(1.0f + cone * 4.0f));
        const float speaker = std::tanh(driven * (1.0f + cone * 4.0f))
            / normalization;
        micA = speaker * 0.78f + modes[0u] * 0.48f
            + modes[1u] * 0.34f + modes[2u] * 0.18f;
        micB = speaker * 0.58f + modes[0u] * 0.26f
            - modes[1u] * 0.28f + modes[2u] * 0.42f
            - modes[3u] * 0.32f + breakup * 0.28f;
        micA = clamp(micA, -3.0f, 3.0f);
        micB = clamp(micB, -3.0f, 3.0f);
    }

    float readFeedbackReturn()
    {
        const int activeRoot = params_.arpPattern
                != ProcessorStackArpPattern::Off
            && arpCurrentNote_ >= 0
            ? arpCurrentNote_ : latestHeldNote();
        const float rootFrequency = noteFrequency(
            static_cast<float>(activeRoot >= 0 ? activeRoot : lastRootNote_));
        const float bodyHarmonic = clamp(lerp(1.5f, 6.0f,
                smoothed_.harmonic) + crookedHarmonicSkew_ * 0.35f,
            1.0f, 7.0f);
        const float stabHarmonic = clamp(std::round(lerp(5.0f, 24.0f,
                smoothed_.harmonic) + crookedHarmonicSkew_ * 1.6f),
            3.0f, 28.0f);
        const float bodyNoteDelay = static_cast<float>(sampleRate_)
            / (rootFrequency * bodyHarmonic);
        const float stabFrequency = clamp(rootFrequency * stabHarmonic,
            620.0f, static_cast<float>(sampleRate_ * 0.18));
        const float stabNoteDelay = static_cast<float>(sampleRate_)
            / stabFrequency;
        const float roomDelay = static_cast<float>(sampleRate_)
            * lerp(0.0080f, 0.00055f, smoothed_.proximity);
        const float target = clamp(lerp(roomDelay, bodyNoteDelay,
                smoothed_.tracking),
            2.0f, static_cast<float>(loopDelay_.size() - 2u));
        const float stabRoomDelay = static_cast<float>(sampleRate_)
            * lerp(0.0016f, 0.00035f, smoothed_.proximity);
        const float stabTarget = clamp(lerp(stabRoomDelay, stabNoteDelay,
                0.55f + smoothed_.tracking * 0.45f),
            2.0f, static_cast<float>(loopDelay_.size() - 2u));
        loopDelaySamples_ += (target - loopDelaySamples_)
            * loopDelaySmoothingCoefficient_;
        stabDelaySamples_ += (stabTarget - stabDelaySamples_)
            * stabDelaySmoothingCoefficient_;
        const float delayed = readDelay(loopDelay_, loopWriteIndex_,
            loopDelaySamples_);
        const float stabDelayed = readDelay(loopDelay_, loopWriteIndex_,
            stabDelaySamples_);
        const float dc = delayed - loopDcInput_ + loopDcPole_ * loopDcOutput_;
        loopDcInput_ = delayed;
        loopDcOutput_ = flushDenormal(dc);
        const float stabDc = stabDelayed - stabDcInput_
            + loopDcPole_ * stabDcOutput_;
        stabDcInput_ = stabDelayed;
        stabDcOutput_ = flushDenormal(stabDc);
        loopLow_ += (loopDcOutput_ - loopLow_)
            * onePoleHz(lerp(1400.0f, 6200.0f, smoothed_.proximity)
                * lerp(1.0f, 0.64f, smoothed_.pierce));
        loopHighLow_ += (loopLow_ - loopHighLow_)
            * onePoleHz(lerp(105.0f, 430.0f, smoothed_.chaos));
        const float bodyBand = loopLow_ - loopHighLow_;

        const float svfFrequency = clamp(stabFrequency,
            80.0f, static_cast<float>(sampleRate_ * 0.18));
        const float svfCoefficient = clamp(2.0f * std::sin(
                kPi * svfFrequency / static_cast<float>(sampleRate_)),
            0.001f, 0.82f);
        const float svfDamping = lerp(0.46f, 0.09f, smoothed_.pierce);
        stabLow_ += svfCoefficient * stabBand_;
        const float stabHigh = stabDcOutput_ - stabLow_
            - svfDamping * stabBand_;
        stabBand_ += svfCoefficient * stabHigh;
        stabLow_ = flushDenormal(clamp(stabLow_, -3.0f, 3.0f));
        stabBand_ = flushDenormal(clamp(stabBand_, -3.0f, 3.0f));

        const auto followEnergy = [&](float signal, float& envelope) {
            const float coefficient = std::abs(signal) > envelope
                ? feedbackEnergyAttackCoefficient_
                : feedbackEnergyReleaseCoefficient_;
            envelope += (std::abs(signal) - envelope) * coefficient;
            envelope = flushDenormal(envelope);
        };
        followEnergy(bodyBand, bodyEnvelope_);
        followEnergy(stabBand_, stabEnvelope_);
        const float spectralShare = stabEnvelope_
            / std::max(1.0e-5f, bodyEnvelope_ + stabEnvelope_);
        const float desiredShare = 0.22f + smoothed_.pierce * 0.48f;
        const float focusTarget = clamp((desiredShare - spectralShare)
                * smoothed_.selfListen * 2.4f,
            0.0f, 1.0f);
        const float focusCoefficient = focusTarget > selfFocus_
            ? selfFocusAttackCoefficient_ : selfFocusReleaseCoefficient_;
        selfFocus_ += (focusTarget - selfFocus_) * focusCoefficient;
        selfFocus_ = flushDenormal(selfFocus_);

        const float bodyExcess = std::max(0.0f, bodyEnvelope_ - 0.34f);
        const float stabExcess = std::max(0.0f, stabEnvelope_ - 0.30f);
        const float bodyGovernor = 1.0f / (1.0f + bodyExcess * 15.0f);
        const float stabGovernor = 1.0f / (1.0f + stabExcess * 11.0f);
        const float bodyGain = lerp(0.92f, 0.42f,
                smoothed_.pierce * smoothed_.selfListen)
            * lerp(1.0f, 0.48f, selfFocus_) * bodyGovernor;
        const float stabGain = (0.12f + smoothed_.pierce * 1.22f)
            * (1.0f + selfFocus_ * 1.15f) * stabGovernor;
        const float governedReturn = clamp(bodyBand * bodyGain
                + stabBand_ * stabGain,
            -2.5f, 2.5f);
        loopEnvelope_ += (std::abs(governedReturn) - loopEnvelope_)
            * loopEnvelopeCoefficient_;
        loopActivity_ += (std::max(std::abs(bodyBand), std::abs(stabBand_))
                - loopActivity_)
            * loopActivityCoefficient_;
        loopEnvelope_ = flushDenormal(loopEnvelope_);
        loopActivity_ = flushDenormal(loopActivity_);
        return governedReturn;
    }

    bool anyLaneActive() const
    {
        for (const auto& lane : lanes_) {
            if (lane.active) return true;
        }
        return false;
    }

    void clearSignalState()
    {
        for (auto& lane : lanes_) {
            std::fill(lane.delay.begin(), lane.delay.end(), 0.0f);
            lane.active = false;
            lane.held = false;
            lane.wireLow = 0.0f;
            lane.pickNoiseLow = 0.0f;
            lane.pickupPrevious = 0.0f;
            lane.pickupVelocity = 0.0f;
            lane.pickupLow = 0.0f;
            lane.rootLow = 0.0f;
            lane.outputSmoothed = 0.0f;
            lane.pickSmoothed = 0.0f;
            lane.dispersionInput = 0.0f;
            lane.dispersionOutput = 0.0f;
        }
        std::fill(loopDelay_.begin(), loopDelay_.end(), 0.0f);
        std::fill(roomDelay_.begin(), roomDelay_.end(), 0.0f);
        loopDcInput_ = loopDcOutput_ = loopLow_ = loopHighLow_ = 0.0f;
        stabDcInput_ = stabDcOutput_ = stabLow_ = stabBand_ = 0.0f;
        bodyEnvelope_ = stabEnvelope_ = selfFocus_ = 0.0f;
        loopEnvelope_ = loopActivity_ = 0.0f;
        pedalDcInput_ = pedalDcOutput_ = 0.0f;
        preampMemory_ = toneLow_ = toneMidLow_ = toneHighLow_ = 0.0f;
        sagEnvelope_ = transformerLow_ = coilEnvelope_ = speakerDc_ = 0.0f;
        speakerModes_.fill({});
        outputDcLeft_ = outputDcRight_ = outputPeak_ = 0.0f;
        amplifierPreviousInput_ = preampPreviousInput_ = 0.0f;
        signalActive_ = false;
    }

    void smoothParams()
    {
        const float c = parameterSmoothingCoefficient_;
        const auto smooth = [c](float& value, float target) {
            value += (target - value) * c;
        };
        smooth(smoothed_.shape, params_.shape);
        smooth(smoothed_.wire, params_.wire);
        smooth(smoothed_.pick, params_.pick);
        smooth(smoothed_.damping, params_.damping);
        smooth(smoothed_.glideMs, params_.glideMs);
        smooth(smoothed_.crooked, params_.crooked);
        smooth(smoothed_.spill, params_.spill);
        smooth(smoothed_.arpGate, params_.arpGate);
        smooth(smoothed_.bite, params_.bite);
        smooth(smoothed_.pedalTone, params_.pedalTone);
        smooth(smoothed_.bias, params_.bias);
        smooth(smoothed_.stack, params_.stack);
        smooth(smoothed_.sag, params_.sag);
        smooth(smoothed_.focus, params_.focus);
        smooth(smoothed_.cone, params_.cone);
        smooth(smoothed_.cabinet, params_.cabinet);
        smooth(smoothed_.mic, params_.mic);
        smooth(smoothed_.feedback, params_.feedback);
        smooth(smoothed_.proximity, params_.proximity);
        smooth(smoothed_.harmonic, params_.harmonic);
        smooth(smoothed_.tracking, params_.tracking);
        smooth(smoothed_.polarity, params_.polarity);
        smooth(smoothed_.root, params_.root);
        smooth(smoothed_.chaos, params_.chaos);
        smooth(smoothed_.pierce, params_.pierce);
        smooth(smoothed_.selfListen, params_.selfListen);
        smooth(smoothed_.outputGainDb, params_.outputGainDb);
        smoothed_.mode = params_.mode;
        smoothed_.arpPattern = params_.arpPattern;
        smoothed_.scale = params_.scale;
        smoothed_.arpRate = params_.arpRate;
        smoothed_.arpOctaves = params_.arpOctaves;
        smoothed_.circuit = params_.circuit;
    }

    void updateCoefficients()
    {
        parameterSmoothingCoefficient_ = onePoleSeconds(0.015f);
        gateAttackCoefficient_ = onePoleSeconds(0.004f);
        loopDelaySmoothingCoefficient_ = onePoleSeconds(0.035f);
        stabDelaySmoothingCoefficient_ = onePoleSeconds(0.014f);
        loopEnvelopeCoefficient_ = onePoleSeconds(0.012f);
        loopActivityCoefficient_ = onePoleSeconds(0.075f);
        outputGainCoefficient_ = onePoleSeconds(0.015f);
        limiterReleaseCoefficient_ = onePoleSeconds(0.090f);
        meterAttackCoefficient_ = onePoleSeconds(0.006f);
        meterReleaseCoefficient_ = onePoleSeconds(0.160f);
        sagAttackCoefficient_ = onePoleSeconds(0.004f, 2.0f);
        sagReleaseCoefficient_ = onePoleSeconds(0.130f, 2.0f);
        coilAttackCoefficient_ = onePoleSeconds(0.0025f);
        coilReleaseCoefficient_ = onePoleSeconds(0.085f);
        speakerDcCoefficient_ = onePoleHz(11.0f);
        outputDcCoefficient_ = onePoleHz(7.0f);
        loopDcPole_ = std::exp(-2.0f * kPi * 18.0f
            / static_cast<float>(sampleRate_));
        pedalDcPole_ = std::exp(-2.0f * kPi * 14.0f
            / static_cast<float>(sampleRate_));
        crookedSkewDecay_ = std::exp(-1.0f
            / std::max(1.0f, static_cast<float>(sampleRate_) * 0.110f));
        crookedPitchDecay_ = std::exp(-1.0f
            / std::max(1.0f, static_cast<float>(sampleRate_) * 0.075f));
        circuitFadeCoefficient_ = 1.0f / std::max(1.0f,
            static_cast<float>(sampleRate_) * 0.020f);
        feedbackEnergyAttackCoefficient_ = onePoleSeconds(0.0025f);
        feedbackEnergyReleaseCoefficient_ = onePoleSeconds(0.085f);
        selfFocusAttackCoefficient_ = onePoleSeconds(0.004f);
        selfFocusReleaseCoefficient_ = onePoleSeconds(0.060f);
    }

    double sampleRate_ = 48000.0;
    ProcessorStackParams params_ {};
    ProcessorStackParams smoothed_ {};
    std::array<ExciterLane, kExciterCount> lanes_ {};
    std::array<bool, 128u> heldNotes_ {};
    std::array<float, 128u> heldVelocities_ {};
    std::array<int16_t, 128u> noteOrder_ {};
    uint32_t noteOrderSize_ = 0u;
    uint64_t ageCounter_ = 1u;
    int lastPlayedNote_ = -1;
    int lastRootNote_ = 45;

    std::vector<float> loopDelay_;
    std::vector<float> roomDelay_;
    size_t loopWriteIndex_ = 0u;
    size_t roomWriteIndex_ = 0u;
    float loopDelaySamples_ = 120.0f;
    float stabDelaySamples_ = 38.0f;
    float loopDcInput_ = 0.0f;
    float loopDcOutput_ = 0.0f;
    float loopLow_ = 0.0f;
    float loopHighLow_ = 0.0f;
    float stabDcInput_ = 0.0f;
    float stabDcOutput_ = 0.0f;
    float stabLow_ = 0.0f;
    float stabBand_ = 0.0f;
    float bodyEnvelope_ = 0.0f;
    float stabEnvelope_ = 0.0f;
    float selfFocus_ = 0.0f;
    float loopEnvelope_ = 0.0f;
    float loopActivity_ = 0.0f;
    float keyGate_ = 0.0f;
    float crookedHarmonicSkew_ = 0.0f;
    float speakerChoke_ = 1.0f;
    float speakerChokeTarget_ = 1.0f;
    float pressure_ = 0.0f;
    float pitchBendSemitones_ = 0.0f;
    float tempoBpm_ = 120.0f;
    double arpPhaseSamples_ = 0.0;
    uint64_t arpStepIndex_ = 0u;
    uint64_t arpStepCount_ = 0u;
    int arpCurrentNote_ = -1;
    bool arpGateOpen_ = false;

    std::array<DriveState, kProcessorStackCircuitCount> pedalStates_ {};
    ProcessorStackCircuit activeCircuit_ = ProcessorStackCircuit::Rat;
    ProcessorStackCircuit previousCircuit_ = ProcessorStackCircuit::Rat;
    float circuitFade_ = 1.0f;
    float pedalDcInput_ = 0.0f;
    float pedalDcOutput_ = 0.0f;
    float preampPreviousInput_ = 0.0f;
    float amplifierPreviousInput_ = 0.0f;
    float preampMemory_ = 0.0f;
    float toneLow_ = 0.0f;
    float toneMidLow_ = 0.0f;
    float toneHighLow_ = 0.0f;
    float sagEnvelope_ = 0.0f;
    float transformerLow_ = 0.0f;
    float coilEnvelope_ = 0.0f;
    float speakerDc_ = 0.0f;
    std::array<SpeakerModeState, kSpeakerModeCount> speakerModes_ {};
    float outputDcLeft_ = 0.0f;
    float outputDcRight_ = 0.0f;
    float limiterGain_ = 1.0f;
    float outputGainSmoothed_ = 0.25f;
    float outputPeak_ = 0.0f;
    bool signalActive_ = false;
    uint32_t randomState_ = 0x8f31d26bu;

    float parameterSmoothingCoefficient_ = 0.001f;
    float gateAttackCoefficient_ = 0.001f;
    float loopDelaySmoothingCoefficient_ = 0.001f;
    float stabDelaySmoothingCoefficient_ = 0.001f;
    float loopEnvelopeCoefficient_ = 0.001f;
    float loopActivityCoefficient_ = 0.001f;
    float outputGainCoefficient_ = 0.001f;
    float limiterReleaseCoefficient_ = 0.001f;
    float meterAttackCoefficient_ = 0.001f;
    float meterReleaseCoefficient_ = 0.001f;
    float sagAttackCoefficient_ = 0.001f;
    float sagReleaseCoefficient_ = 0.001f;
    float coilAttackCoefficient_ = 0.001f;
    float coilReleaseCoefficient_ = 0.001f;
    float speakerDcCoefficient_ = 0.001f;
    float outputDcCoefficient_ = 0.001f;
    float loopDcPole_ = 0.997f;
    float pedalDcPole_ = 0.998f;
    float crookedSkewDecay_ = 0.999f;
    float crookedPitchDecay_ = 0.999f;
    float circuitFadeCoefficient_ = 0.001f;
    float feedbackEnergyAttackCoefficient_ = 0.001f;
    float feedbackEnergyReleaseCoefficient_ = 0.001f;
    float selfFocusAttackCoefficient_ = 0.001f;
    float selfFocusReleaseCoefficient_ = 0.001f;
};

} // namespace s3g
