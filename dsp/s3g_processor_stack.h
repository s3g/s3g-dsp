#pragma once

#include "s3g_analog_drive_circuits.h"
#include "s3g_math.h"
#include "s3g_realtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
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
    Quarter,
    Half,
    Whole,
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
    case ProcessorStackArpRate::Quarter: return "1/4";
    case ProcessorStackArpRate::Half: return "1/2";
    case ProcessorStackArpRate::Whole: return "1/1";
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

enum class ProcessorStackPairRelation : uint32_t {
    Unison = 0u,
    FourthDown,
    FifthUp,
    OctaveUp,
    Contrary,
    Count,
};

inline constexpr uint32_t kProcessorStackPairRelationCount =
    static_cast<uint32_t>(ProcessorStackPairRelation::Count);

inline const char* processorStackPairRelationName(
    ProcessorStackPairRelation relation)
{
    switch (relation) {
    case ProcessorStackPairRelation::Unison: return "UNISON";
    case ProcessorStackPairRelation::FourthDown: return "4TH DOWN";
    case ProcessorStackPairRelation::FifthUp: return "5TH UP";
    case ProcessorStackPairRelation::OctaveUp: return "OCTAVE UP";
    case ProcessorStackPairRelation::Contrary: return "CONTRARY";
    case ProcessorStackPairRelation::Count: break;
    }
    return "UNISON";
}

enum class ProcessorStackArpRelation : uint32_t {
    Follow = 0u,
    Counter,
    Free,
    Count,
};

inline constexpr uint32_t kProcessorStackArpRelationCount =
    static_cast<uint32_t>(ProcessorStackArpRelation::Count);

inline const char* processorStackArpRelationName(
    ProcessorStackArpRelation relation)
{
    switch (relation) {
    case ProcessorStackArpRelation::Follow: return "FOLLOW";
    case ProcessorStackArpRelation::Counter: return "COUNTER";
    case ProcessorStackArpRelation::Free: return "FREE";
    case ProcessorStackArpRelation::Count: break;
    }
    return "FOLLOW";
}

enum class ProcessorStackNeckMaterial : uint32_t {
    Maple = 0u,
    Mahogany,
    Aluminum,
    Composite,
    Count,
};

inline constexpr uint32_t kProcessorStackNeckMaterialCount =
    static_cast<uint32_t>(ProcessorStackNeckMaterial::Count);

inline const char* processorStackNeckMaterialName(
    ProcessorStackNeckMaterial material)
{
    switch (material) {
    case ProcessorStackNeckMaterial::Maple: return "MAPLE";
    case ProcessorStackNeckMaterial::Mahogany: return "MAHOGANY";
    case ProcessorStackNeckMaterial::Aluminum: return "ALUMINUM";
    case ProcessorStackNeckMaterial::Composite: return "COMPOSITE";
    case ProcessorStackNeckMaterial::Count: break;
    }
    return "MAPLE";
}

enum class ProcessorStackBodyMaterial : uint32_t {
    SolidWood = 0u,
    HollowWood,
    SemiHollow,
    Aluminum,
    Count,
};

inline constexpr uint32_t kProcessorStackBodyMaterialCount =
    static_cast<uint32_t>(ProcessorStackBodyMaterial::Count);

inline const char* processorStackBodyMaterialName(
    ProcessorStackBodyMaterial material)
{
    switch (material) {
    case ProcessorStackBodyMaterial::SolidWood: return "SOLID WOOD";
    case ProcessorStackBodyMaterial::HollowWood: return "HOLLOW WOOD";
    case ProcessorStackBodyMaterial::SemiHollow: return "SEMI-HOLLOW";
    case ProcessorStackBodyMaterial::Aluminum: return "ALUMINUM";
    case ProcessorStackBodyMaterial::Count: break;
    }
    return "SOLID WOOD";
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
    float attackMs = 2.0f;
    float decayMs = 180.0f;
    float sustain = 0.78f;
    float releaseMs = 90.0f;

    float pairAmount = 0.0f;
    ProcessorStackPairRelation pairRelation =
        ProcessorStackPairRelation::Unison;
    float pairLoose = 0.24f;
    float pairSpread = 0.72f;
    ProcessorStackNeckMaterial neckA = ProcessorStackNeckMaterial::Maple;
    ProcessorStackBodyMaterial bodyA = ProcessorStackBodyMaterial::SolidWood;
    ProcessorStackNeckMaterial neckB =
        ProcessorStackNeckMaterial::Aluminum;
    ProcessorStackBodyMaterial bodyB =
        ProcessorStackBodyMaterial::HollowWood;

    bool arpHostSync = false;
    ProcessorStackArpPattern arpPattern = ProcessorStackArpPattern::Off;
    ProcessorStackScale scale = ProcessorStackScale::Phrygian;
    ProcessorStackArpRate arpRate = ProcessorStackArpRate::Sixteenth;
    uint32_t arpOctaves = 2u;
    float arpGate = 0.62f;
    uint32_t customPatternLength = 8u;
    std::array<int32_t, 8u> customPattern {{
        0, 1, 2, 4, 3, 6, 5, 1,
    }};
    ProcessorStackArpRelation arpBRelation =
        ProcessorStackArpRelation::Follow;
    ProcessorStackArpPattern arpPatternB = ProcessorStackArpPattern::Off;
    ProcessorStackScale scaleB = ProcessorStackScale::Phrygian;
    ProcessorStackArpRate arpRateB = ProcessorStackArpRate::Sixteenth;
    uint32_t arpOctavesB = 2u;
    float arpGateB = 0.62f;
    float arpPhaseB = 0.50f;
    uint32_t customPatternLengthB = 8u;
    std::array<int32_t, 8u> customPatternB {{
        0, 4, 2, 6, 1, 5, 3, 7,
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
    float targetGlitch = 0.0f;
    float glitchRatchet = 0.46f;
    float overloadMask = 0.76f;

    bool linkPedal = true;
    bool linkAmplifier = true;
    bool linkFeedback = true;
    ProcessorStackCircuit circuitB = ProcessorStackCircuit::Rat;
    float biteB = 0.56f;
    float pedalToneB = 0.54f;
    float biasB = 0.52f;
    float stackB = 0.62f;
    float sagB = 0.46f;
    float focusB = 0.55f;
    float coneB = 0.64f;
    float cabinetB = 0.52f;
    float micB = 0.34f;
    float feedbackB = 0.56f;
    float proximityB = 0.58f;
    float harmonicB = 0.42f;
    float trackingB = 0.72f;
    float polarityB = 0.78f;
    float rootB = 0.28f;
    float chaosB = 0.32f;
    float pierceB = 0.68f;
    float selfListenB = 0.72f;
    float targetGlitchB = 0.0f;
    float glitchRatchetB = 0.46f;
    float overloadMaskB = 0.76f;
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
    params.attackMs = finite(params.attackMs, 2.0f, 0.0f, 2000.0f);
    params.decayMs = finite(params.decayMs, 180.0f, 5.0f, 8000.0f);
    params.sustain = finite(params.sustain, 0.78f, 0.0f, 1.0f);
    params.releaseMs = finite(params.releaseMs, 90.0f, 5.0f, 20000.0f);
    params.pairAmount = finite(params.pairAmount, 0.0f, 0.0f, 1.0f);
    params.pairRelation = static_cast<ProcessorStackPairRelation>(
        std::min<uint32_t>(static_cast<uint32_t>(params.pairRelation),
            kProcessorStackPairRelationCount - 1u));
    params.pairLoose = finite(params.pairLoose, 0.24f, 0.0f, 1.0f);
    params.pairSpread = finite(params.pairSpread, 0.72f, 0.0f, 1.0f);
    params.neckA = static_cast<ProcessorStackNeckMaterial>(
        std::min<uint32_t>(static_cast<uint32_t>(params.neckA),
            kProcessorStackNeckMaterialCount - 1u));
    params.bodyA = static_cast<ProcessorStackBodyMaterial>(
        std::min<uint32_t>(static_cast<uint32_t>(params.bodyA),
            kProcessorStackBodyMaterialCount - 1u));
    params.neckB = static_cast<ProcessorStackNeckMaterial>(
        std::min<uint32_t>(static_cast<uint32_t>(params.neckB),
            kProcessorStackNeckMaterialCount - 1u));
    params.bodyB = static_cast<ProcessorStackBodyMaterial>(
        std::min<uint32_t>(static_cast<uint32_t>(params.bodyB),
            kProcessorStackBodyMaterialCount - 1u));
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
    params.arpBRelation = static_cast<ProcessorStackArpRelation>(
        std::min<uint32_t>(static_cast<uint32_t>(params.arpBRelation),
            kProcessorStackArpRelationCount - 1u));
    params.arpPatternB = static_cast<ProcessorStackArpPattern>(
        std::min<uint32_t>(static_cast<uint32_t>(params.arpPatternB),
            kProcessorStackArpPatternCount - 1u));
    params.scaleB = static_cast<ProcessorStackScale>(std::min<uint32_t>(
        static_cast<uint32_t>(params.scaleB),
        kProcessorStackScaleCount - 1u));
    params.arpRateB = static_cast<ProcessorStackArpRate>(std::min<uint32_t>(
        static_cast<uint32_t>(params.arpRateB),
        kProcessorStackArpRateCount - 1u));
    params.arpOctavesB = std::clamp(params.arpOctavesB, 1u, 4u);
    params.arpGateB = finite(params.arpGateB, 0.62f, 0.05f, 1.0f);
    params.arpPhaseB = finite(params.arpPhaseB, 0.50f, 0.0f, 1.0f);
    params.customPatternLengthB = std::clamp(
        params.customPatternLengthB, 1u,
        static_cast<uint32_t>(params.customPatternB.size()));
    for (auto& step : params.customPatternB) {
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
    params.targetGlitch = finite(params.targetGlitch, 0.0f, 0.0f, 1.0f);
    params.glitchRatchet = finite(params.glitchRatchet, 0.46f, 0.0f, 1.0f);
    params.overloadMask = finite(params.overloadMask, 0.76f, 0.0f, 1.0f);
    params.circuitB = static_cast<ProcessorStackCircuit>(std::min<uint32_t>(
        static_cast<uint32_t>(params.circuitB),
        kProcessorStackCircuitCount - 1u));
    params.biteB = finite(params.biteB, 0.56f, 0.0f, 1.0f);
    params.pedalToneB = finite(params.pedalToneB, 0.54f, 0.0f, 1.0f);
    params.biasB = finite(params.biasB, 0.52f, 0.0f, 1.0f);
    params.stackB = finite(params.stackB, 0.62f, 0.0f, 1.0f);
    params.sagB = finite(params.sagB, 0.46f, 0.0f, 1.0f);
    params.focusB = finite(params.focusB, 0.55f, 0.0f, 1.0f);
    params.coneB = finite(params.coneB, 0.64f, 0.0f, 1.0f);
    params.cabinetB = finite(params.cabinetB, 0.52f, 0.0f, 1.0f);
    params.micB = finite(params.micB, 0.34f, 0.0f, 1.0f);
    params.feedbackB = finite(params.feedbackB, 0.56f, 0.0f, 1.0f);
    params.proximityB = finite(params.proximityB, 0.58f, 0.0f, 1.0f);
    params.harmonicB = finite(params.harmonicB, 0.42f, 0.0f, 1.0f);
    params.trackingB = finite(params.trackingB, 0.72f, 0.0f, 1.0f);
    params.polarityB = finite(params.polarityB, 0.78f, 0.0f, 1.0f);
    params.rootB = finite(params.rootB, 0.28f, 0.0f, 1.0f);
    params.chaosB = finite(params.chaosB, 0.32f, 0.0f, 1.0f);
    params.pierceB = finite(params.pierceB, 0.68f, 0.0f, 1.0f);
    params.selfListenB = finite(params.selfListenB, 0.72f, 0.0f, 1.0f);
    params.targetGlitchB = finite(params.targetGlitchB, 0.0f, 0.0f, 1.0f);
    params.glitchRatchetB = finite(params.glitchRatchetB,
        0.46f, 0.0f, 1.0f);
    params.overloadMaskB = finite(params.overloadMaskB,
        0.76f, 0.0f, 1.0f);
    params.outputGainDb = finite(
        params.outputGainDb, -12.0f, -36.0f, 6.0f);
    return params;
}

// A dual-rig amp/speaker feedback instrument. Each four-lane guitar player
// owns a pedal, supply envelope, nonlinear speaker, room, and microphone
// feedback history. The two governed rigs can cross-listen, but they are not
// summed before their nonlinear stages. Storage is allocated only by
// prepare().
class ProcessorStack {
public:
    static constexpr uint32_t kExciterCount = 4u;
    static constexpr uint32_t kScoreStringCount = 6u;
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
        for (auto& lane : partnerLanes_) {
            lane.delay.assign(std::max<size_t>(wireSize, 16u), 0.0f);
        }
        const size_t loopSize = static_cast<size_t>(
            std::ceil(sampleRate_ * 0.032)) + 8u;
        const size_t glitchSize = static_cast<size_t>(
            std::ceil(sampleRate_ * 0.050)) + 8u;
        for (auto& rig : rigs_) {
            rig.loopDelay.assign(std::max<size_t>(loopSize, 16u), 0.0f);
            rig.roomDelay.assign(std::max<size_t>(loopSize, 16u), 0.0f);
            rig.glitchHistory.assign(
                std::max<size_t>(glitchSize, 32u), 0.0f);
        }
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
        for (auto& lane : partnerLanes_) {
            std::fill(lane.delay.begin(), lane.delay.end(), 0.0f);
            lane = resetLanePreservingDelay(std::move(lane.delay));
        }
        for (uint32_t index = 0u; index < rigs_.size(); ++index) {
            resetRig(rigs_[index], index);
        }
        keyGate_ = 0.0f;
        crookedHarmonicSkew_ = 0.0f;
        speakerChoke_ = 1.0f;
        speakerChokeTarget_ = 1.0f;
        pressure_ = 0.0f;
        pitchBendSemitones_ = 0.0f;
        tempoBpm_ = 120.0f;
        hostTransportBeat_ = 0.0;
        hostTransportActive_ = false;
        scorePlaybackActive_ = false;
        scorePlayerHeld_.fill(false);
        for (auto& player : scoreStringLanes_) player.fill(-1);
        arpeggiators_.fill(ArpState {});
        lastPlayedNote_ = -1;
        lastRootNote_ = 45;
        partnerRootNote_ = -1;
        partnerPrimaryRoot_ = -1;
        ageCounter_ = 1u;
        stereoSeparationActivity_ = 0.0f;
        linkedRigStress_ = 0.0f;
        outputDcLeft_ = 0.0f;
        outputDcRight_ = 0.0f;
        limiterGain_ = 1.0f;
        limiterWasAttacking_ = false;
        limiterAttackEventCount_ = 0u;
        maximumLimiterGainStep_ = 0.0f;
        outputGainSmoothed_ = dbToGain(params_.outputGainDb);
        outputPeak_ = 0.0f;
        signalActive_ = false;
        randomState_ = 0x8f31d26bu;
        smoothed_ = params_;
        updateCoefficients();
    }

    void setParams(ProcessorStackParams params)
    {
        const bool silentlyIdle = !signalActive_ && noteOrderSize_ == 0u
            && !anyLaneActive() && rigs_[0u].loopActivity <= 1.0e-7f
            && rigs_[1u].loopActivity <= 1.0e-7f;
        const ProcessorStackMode previousMode = params_.mode;
        const float previousShape = params_.shape;
        const float previousPairAmount = params_.pairAmount;
        const ProcessorStackPairRelation previousPairRelation =
            params_.pairRelation;
        const ProcessorStackArpPattern previousPattern = params_.arpPattern;
        const bool previousArpHostSync = params_.arpHostSync;
        const ProcessorStackScale previousScale = params_.scale;
        const ProcessorStackArpRate previousRate = params_.arpRate;
        const uint32_t previousOctaves = params_.arpOctaves;
        const uint32_t previousPatternLength = params_.customPatternLength;
        const auto previousCustomPattern = params_.customPattern;
        const ProcessorStackArpRelation previousArpBRelation =
            params_.arpBRelation;
        const ProcessorStackArpPattern previousPatternB =
            params_.arpPatternB;
        const ProcessorStackScale previousScaleB = params_.scaleB;
        const ProcessorStackArpRate previousRateB = params_.arpRateB;
        const uint32_t previousOctavesB = params_.arpOctavesB;
        const uint32_t previousPatternLengthB =
            params_.customPatternLengthB;
        const auto previousCustomPatternB = params_.customPatternB;
        params_ = sanitizeProcessorStackParams(params);
        if (silentlyIdle) {
            // Controls edited during silence must describe the very next
            // attack. Otherwise smoothing wakes only after note-on and the
            // old PICK/SHAPE values leak into the excitation front.
            smoothed_ = params_;
            outputGainSmoothed_ = dbToGain(params_.outputGainDb);
            for (uint32_t index = 0u; index < rigs_.size(); ++index) {
                auto& rig = rigs_[index];
                const ProcessorStackCircuit circuit = index == 1u
                        && !params_.linkPedal
                    ? params_.circuitB : params_.circuit;
                rig.activeCircuit = circuit;
                rig.previousCircuit = circuit;
                rig.circuitFade = 1.0f;
            }
        }
        if (scorePlaybackActive_) return;
        const bool arpChangedA = params_.arpHostSync != previousArpHostSync
            || params_.arpPattern != previousPattern
            || params_.scale != previousScale
            || params_.arpRate != previousRate
            || params_.arpOctaves != previousOctaves
            || params_.customPatternLength != previousPatternLength
            || params_.customPattern != previousCustomPattern;
        const bool arpChangedB = params_.arpHostSync != previousArpHostSync
            || params_.arpBRelation != previousArpBRelation
            || params_.arpPatternB != previousPatternB
            || params_.scaleB != previousScaleB
            || params_.arpRateB != previousRateB
            || params_.arpOctavesB != previousOctavesB
            || params_.customPatternLengthB != previousPatternLengthB
            || params_.customPatternB != previousCustomPatternB;
        const bool pairChanged = params_.pairRelation != previousPairRelation
            || (params_.pairAmount > 1.0e-4f)
                != (previousPairAmount > 1.0e-4f);
        if (params_.pairRelation != previousPairRelation) {
            partnerRootNote_ = -1;
            partnerPrimaryRoot_ = -1;
        }
        if (arpChangedA) resetArpeggiator(0u, noteOrderSize_ > 0u);
        if (arpChangedB) {
            partnerRootNote_ = -1;
            partnerPrimaryRoot_ = -1;
            if (params_.arpBRelation == ProcessorStackArpRelation::Follow) {
                arpeggiators_[1u] = ArpState {};
                rebuildPartnerVoicing(false);
            } else {
                resetArpeggiator(1u, noteOrderSize_ > 0u);
            }
        }
        if ((params_.mode != previousMode || pairChanged)
            && !arpChangedA && !arpChangedB) {
            rebuildVoicing(false);
        } else if (!arpChangedA && !arpChangedB
            && params_.mode == ProcessorStackMode::Power
            && params_.shape != previousShape && noteOrderSize_ > 0u) {
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

        const bool arpA = params_.arpPattern
            != ProcessorStackArpPattern::Off;
        const bool arpBIndependent = params_.arpBRelation
            != ProcessorStackArpRelation::Follow;
        const bool arpB = arpBIndependent && params_.arpPatternB
            != ProcessorStackArpPattern::Off;
        if (arpA) {
            resetArpeggiator(0u, true);
        } else {
            applyCrookedGesture(midiNote, repeated);
            lastPlayedNote_ = midiNote;
            if (params_.mode == ProcessorStackMode::Hand) {
                triggerHandLane(midiNote, velocity);
            } else {
                rebuildPrimaryVoicing(true);
            }
        }
        if (arpB) {
            resetArpeggiator(1u, true);
        } else if (params_.arpBRelation
                == ProcessorStackArpRelation::Follow) {
            // A's arpeggiator configures the following player. With no A
            // arpeggiator, HAND mirrors the selected lane and the other
            // modes rebuild the partner bank here.
            if (!arpA && params_.mode != ProcessorStackMode::Hand)
                rebuildPartnerVoicing(true);
        } else if (params_.mode != ProcessorStackMode::Hand || arpA) {
            rebuildPartnerVoicing(true);
        }
        signalActive_ = true;
    }

    void noteOff(int midiNote)
    {
        midiNote = std::clamp(midiNote, 0, 127);
        heldNotes_[static_cast<size_t>(midiNote)] = false;
        removeNoteFromOrder(midiNote);
        const bool arpA = params_.arpPattern
            != ProcessorStackArpPattern::Off;
        const bool arpBIndependent = params_.arpBRelation
            != ProcessorStackArpRelation::Follow;
        const bool arpB = arpBIndependent && params_.arpPatternB
            != ProcessorStackArpPattern::Off;
        if (arpA) {
            if (noteOrderSize_ > 0u) resetArpeggiator(0u, true);
            else {
                arpeggiators_[0u].currentNote = -1;
                closeArpeggiatorGate(0u);
            }
        } else if (params_.mode == ProcessorStackMode::Hand) {
            for (uint32_t index = 0u; index < lanes_.size(); ++index) {
                if (lanes_[index].sourceNote == midiNote) {
                    lanes_[index].held = false;
                    if (!arpB) partnerLanes_[index].held = false;
                }
            }
        } else {
            rebuildPrimaryVoicing(false);
        }
        if (arpB) {
            if (noteOrderSize_ > 0u) resetArpeggiator(1u, true);
            else {
                arpeggiators_[1u].currentNote = -1;
                closeArpeggiatorGate(1u);
            }
        } else if (params_.arpBRelation
                == ProcessorStackArpRelation::Follow) {
            if (!arpA && params_.mode != ProcessorStackMode::Hand)
                rebuildPartnerVoicing(false);
        } else if (params_.mode != ProcessorStackMode::Hand || arpA) {
            rebuildPartnerVoicing(false);
        }
    }

    void allNotesOff()
    {
        heldNotes_.fill(false);
        noteOrderSize_ = 0u;
        noteOrder_.fill(-1);
        for (auto& lane : lanes_) lane.held = false;
        for (auto& lane : partnerLanes_) lane.held = false;
        for (auto& arp : arpeggiators_) {
            arp.currentNote = -1;
            arp.gateOpen = false;
            arp.phaseSamples = 0.0;
            arp.hostStep = kUnprimedHostStep;
        }
        scorePlayerHeld_.fill(false);
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

    void setHostTransportBeat(double beat, bool active)
    {
        const bool nextActive = active && std::isfinite(beat);
        const bool changed = nextActive != hostTransportActive_;
        hostTransportActive_ = nextActive;
        if (std::isfinite(beat)) hostTransportBeat_ = beat;
        if (!changed || !params_.arpHostSync || noteOrderSize_ == 0u) {
            return;
        }
        if (params_.arpPattern != ProcessorStackArpPattern::Off) {
            resetArpeggiator(0u, true);
        }
        if (params_.pairAmount > 1.0e-4f
            && params_.arpBRelation != ProcessorStackArpRelation::Follow
            && params_.arpPatternB != ProcessorStackArpPattern::Off) {
            resetArpeggiator(1u, true);
        }
    }

    void setScorePlaybackActive(bool active)
    {
        if (active == scorePlaybackActive_) return;
        scorePlaybackActive_ = active;
        scoreReleasePlayer(0u);
        scoreReleasePlayer(1u);
        if (!active && noteOrderSize_ > 0u) rebuildVoicing(true);
    }

    void scorePlayerChord(uint32_t player, const int* notes,
        uint32_t noteCount, float velocity = 0.9f)
    {
        player = std::min<uint32_t>(player, 1u);
        auto& bank = player == 0u ? lanes_ : partnerLanes_;
        scoreStringLanes_[player].fill(-1);
        for (auto& lane : bank) lane.held = false;
        noteCount = notes ? std::min<uint32_t>(noteCount,
            static_cast<uint32_t>(bank.size())) : 0u;
        if (noteCount == 0u) {
            scorePlayerHeld_[player] = false;
            return;
        }
        velocity = clamp(std::isfinite(velocity) ? velocity : 0.9f,
            0.0f, 1.0f);
        const float gain = 1.0f / std::sqrt(static_cast<float>(noteCount));
        for (uint32_t index = 0u; index < noteCount; ++index) {
            const int note = std::clamp(notes[index], 0, 127);
            configureLane(bank[index], note, 0.0f, gain, gain,
                velocity, true, true, player);
        }
        scorePlayerHeld_[player] = true;
        if (player == 0u) {
            lastRootNote_ = std::clamp(notes[0u], 0, 127);
            lastPlayedNote_ = lastRootNote_;
        } else {
            partnerRootNote_ = std::clamp(notes[0u], 0, 127);
        }
        signalActive_ = true;
    }

    // Each command corresponds to one physical guitar string. MIDI notes
    // retrigger that string, -1 rests it, and -2 carries its existing lane
    // into the new row without another pick attack.
    uint32_t scorePlayerTabRow(uint32_t player, const int* commands,
        uint32_t commandCount, float velocity = 0.9f)
    {
        player = std::min<uint32_t>(player, 1u);
        auto& bank = player == 0u ? lanes_ : partnerLanes_;
        auto& previous = scoreStringLanes_[player];
        std::array<int8_t, kScoreStringCount> next {};
        next.fill(-1);
        std::array<bool, kExciterCount> used {};
        commandCount = commands ? std::min<uint32_t>(
            commandCount, kScoreStringCount) : 0u;
        uint32_t voiceCount = 0u;
        uint32_t attackCount = 0u;

        // Reserve genuinely sustained lanes first, so new attacks cannot
        // steal them when a row contains both holds and fresh notes.
        for (uint32_t string = 0u; string < commandCount; ++string) {
            if (commands[string] != -2) continue;
            const int laneIndex = previous[string];
            if (laneIndex < 0
                || laneIndex >= static_cast<int>(kExciterCount)
                || used[static_cast<size_t>(laneIndex)]
                || !bank[static_cast<size_t>(laneIndex)].active
                || !bank[static_cast<size_t>(laneIndex)].held) continue;
            next[string] = static_cast<int8_t>(laneIndex);
            used[static_cast<size_t>(laneIndex)] = true;
            ++voiceCount;
        }

        const auto availableLane = [&](int preferred) {
            if (preferred >= 0
                && preferred < static_cast<int>(kExciterCount)
                && !used[static_cast<size_t>(preferred)]) return preferred;
            for (uint32_t lane = 0u; lane < kExciterCount; ++lane) {
                if (!used[lane] && !bank[lane].active) {
                    return static_cast<int>(lane);
                }
            }
            for (uint32_t lane = 0u; lane < kExciterCount; ++lane) {
                if (!used[lane]) return static_cast<int>(lane);
            }
            return -1;
        };
        for (uint32_t string = 0u;
             string < commandCount && voiceCount < kExciterCount; ++string) {
            if (commands[string] < 0) continue;
            const int laneIndex = availableLane(previous[string]);
            if (laneIndex < 0) continue;
            next[string] = static_cast<int8_t>(laneIndex);
            used[static_cast<size_t>(laneIndex)] = true;
            ++voiceCount;
            ++attackCount;
        }

        velocity = clamp(std::isfinite(velocity) ? velocity : 0.9f,
            0.0f, 1.0f);
        const float gain = voiceCount > 0u
            ? 1.0f / std::sqrt(static_cast<float>(voiceCount)) : 0.0f;
        for (uint32_t string = 0u; string < commandCount; ++string) {
            const int laneIndex = next[string];
            if (laneIndex < 0) continue;
            auto& lane = bank[static_cast<size_t>(laneIndex)];
            if (commands[string] == -2) {
                lane.targetGain = gain;
                lane.targetAttackGain = gain;
                lane.velocity = velocity;
                lane.held = true;
                continue;
            }
            configureLane(lane, std::clamp(commands[string], 0, 127),
                0.0f, gain, gain, velocity, true, true, player);
        }
        for (uint32_t lane = 0u; lane < kExciterCount; ++lane) {
            if (!used[lane]) bank[lane].held = false;
        }
        previous = next;
        scorePlayerHeld_[player] = voiceCount > 0u;
        if (voiceCount == 0u) return attackCount;
        for (uint32_t string = 0u; string < commandCount; ++string) {
            const int laneIndex = next[string];
            if (laneIndex < 0) continue;
            const int note = bank[static_cast<size_t>(laneIndex)].sourceNote;
            if (player == 0u) {
                lastRootNote_ = note;
                lastPlayedNote_ = note;
            } else {
                partnerRootNote_ = note;
            }
            break;
        }
        signalActive_ = true;
        return attackCount;
    }

    void scoreRelatedTabRow(const int* primaryCommands,
        uint32_t commandCount, float velocity = 0.86f)
    {
        commandCount = primaryCommands ? std::min<uint32_t>(
            commandCount, kScoreStringCount) : 0u;
        std::array<int, kScoreStringCount> primaryNotes {};
        primaryNotes.fill(-1);
        int primaryRoot = -1;
        bool rootIsHeld = false;
        for (uint32_t string = 0u; string < commandCount; ++string) {
            const int command = primaryCommands[string];
            if (command >= 0) {
                primaryNotes[string] = std::clamp(command, 0, 127);
            } else if (command == -2) {
                const int laneIndex = scoreStringLanes_[0u][string];
                if (laneIndex >= 0
                    && laneIndex < static_cast<int>(kExciterCount)) {
                    primaryNotes[string] =
                        lanes_[static_cast<size_t>(laneIndex)].sourceNote;
                }
            }
            if (primaryRoot < 0 && primaryNotes[string] >= 0) {
                primaryRoot = primaryNotes[string];
                rootIsHeld = command == -2;
            }
        }
        if (primaryRoot < 0) {
            scoreReleasePlayer(1u);
            return;
        }
        const int relatedRoot = rootIsHeld && partnerRootNote_ >= 0
            ? partnerRootNote_ : partnerRootFor(primaryRoot, true);
        std::array<int, kScoreStringCount> relatedCommands {};
        relatedCommands.fill(-1);
        for (uint32_t string = 0u; string < commandCount; ++string) {
            if (primaryCommands[string] == -2) {
                relatedCommands[string] = -2;
            } else if (primaryNotes[string] >= 0) {
                relatedCommands[string] = std::clamp(relatedRoot
                    + primaryNotes[string] - primaryRoot, 0, 127);
            }
        }
        scorePlayerTabRow(1u, relatedCommands.data(), commandCount, velocity);
    }

    void scoreRelatedChord(const int* primaryNotes, uint32_t noteCount,
        float velocity = 0.86f)
    {
        noteCount = primaryNotes
            ? std::min<uint32_t>(noteCount, kExciterCount) : 0u;
        if (noteCount == 0u) {
            scoreReleasePlayer(1u);
            return;
        }
        const int primaryRoot = std::clamp(primaryNotes[0u], 0, 127);
        const int relatedRoot = partnerRootFor(primaryRoot, true);
        std::array<int, kExciterCount> related {};
        for (uint32_t index = 0u; index < noteCount; ++index) {
            related[index] = std::clamp(relatedRoot
                + (primaryNotes[index] - primaryRoot), 0, 127);
        }
        scorePlayerChord(1u, related.data(), noteCount, velocity);
    }

    void scoreReleasePlayer(uint32_t player)
    {
        player = std::min<uint32_t>(player, 1u);
        auto& bank = player == 0u ? lanes_ : partnerLanes_;
        for (auto& lane : bank) lane.held = false;
        scoreStringLanes_[player].fill(-1);
        scorePlayerHeld_[player] = false;
    }

    void scorePrepareNextTabRow(uint32_t player,
        const int* nextCommands, uint32_t commandCount)
    {
        player = std::min<uint32_t>(player, 1u);
        auto& bank = player == 0u ? lanes_ : partnerLanes_;
        auto& mapping = scoreStringLanes_[player];
        std::array<bool, kExciterCount> keep {};
        commandCount = nextCommands ? std::min<uint32_t>(
            commandCount, kScoreStringCount) : 0u;
        for (uint32_t string = 0u; string < kScoreStringCount; ++string) {
            const int laneIndex = mapping[string];
            const int command = string < commandCount
                ? nextCommands[string] : -1;
            const bool hold = command == -2
                && laneIndex >= 0
                && laneIndex < static_cast<int>(kExciterCount);
            if (hold) keep[static_cast<size_t>(laneIndex)] = true;
            else if (command < 0) mapping[string] = -1;
        }
        bool anyHeld = false;
        for (uint32_t lane = 0u; lane < kExciterCount; ++lane) {
            if (!keep[lane]) bank[lane].held = false;
            anyHeld = anyHeld || (keep[lane] && bank[lane].held);
        }
        scorePlayerHeld_[player] = anyHeld;
    }

    void processFrame(float& left, float& right)
    {
        if (!signalActive_ && noteOrderSize_ == 0u && !anyLaneActive()
            && rigs_[0u].loopActivity <= 1.0e-7f
            && rigs_[1u].loopActivity <= 1.0e-7f) {
            left = 0.0f;
            right = 0.0f;
            return;
        }
        smoothParams();
        processArpeggiators();
        const bool keysHeld = noteOrderSize_ > 0u
            || scorePlayerHeld_[0u] || scorePlayerHeld_[1u];
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

        const auto renderGuitar = [&](auto& guitarLanes,
                                      ProcessorStackNeckMaterial neck,
                                      ProcessorStackBodyMaterial body,
                                      float& guitarRoot) {
            float guitar = 0.0f;
            float activeWeight = 0.0f;
            const MaterialProfile material = materialProfile(neck, body);
            for (auto& lane : guitarLanes) {
                if (!lane.active) continue;
                const float gainCoefficient = std::abs(lane.noteOffset) > 0.5f
                    ? chordBloomCoefficient_ : voicingGainCoefficient_;
                lane.gain += (lane.targetGain - lane.gain)
                    * gainCoefficient;
                lane.gain = flushDenormal(lane.gain);
                lane.attackGain += (lane.targetAttackGain - lane.attackGain)
                    * attackGainCoefficient_;
                lane.attackGain = flushDenormal(lane.attackGain);
                float laneRoot = 0.0f;
                const float sample = processExciter(lane, laneRoot, material);
                guitar += sample * lane.gain;
                guitarRoot += laneRoot * lane.gain;
                activeWeight += lane.gain * lane.gain;
            }
            if (activeWeight > 1.0f) {
                const float normalization = 1.0f / std::sqrt(activeWeight);
                guitar *= normalization;
                guitarRoot *= normalization;
            }
            return guitar;
        };
        float rootA = 0.0f;
        float rootB = 0.0f;
        const float guitarA = renderGuitar(lanes_, smoothed_.neckA,
            smoothed_.bodyA, rootA);
        const float guitarB = renderGuitar(partnerLanes_, smoothed_.neckB,
            smoothed_.bodyB, rootB);
        const float pairGain = smoothed_.pairAmount;
        const float pairNormalization = 1.0f
            / std::sqrt(1.0f + pairGain * pairGain);
        float excitationA = guitarA;
        float excitationB = guitarB;
        float rootWitnessA = rootA;
        float rootWitnessB = rootB;
        const float calmAttackDepth = (1.0f - smoothed_.pick)
            * (1.0f - smoothed_.pick);
        const float excitationGate = lerp(1.0f, keyGate_, calmAttackDepth);
        excitationA *= excitationGate;
        excitationB *= excitationGate;
        rootWitnessA *= excitationGate;
        rootWitnessB *= excitationGate;

        const RigControls controlsA = rigControls(0u);
        const RigControls controlsB = rigControls(1u);

        const int activeRootA = params_.arpPattern
                    != ProcessorStackArpPattern::Off
                && arpeggiators_[0u].currentNote >= 0
            ? arpeggiators_[0u].currentNote : latestHeldNote();
        const int activeRootB = params_.arpBRelation
                    != ProcessorStackArpRelation::Follow
                && params_.arpPatternB != ProcessorStackArpPattern::Off
                && arpeggiators_[1u].currentNote >= 0
            ? arpeggiators_[1u].currentNote
            : partnerRootNote_ >= 0 ? partnerRootNote_
            : std::clamp((activeRootA >= 0 ? activeRootA : lastRootNote_) + 7,
                0, 127);
        const float feedbackReturnA = readFeedbackReturn(rigs_[0u],
            controlsA, activeRootA);
        const float feedbackReturnB = readFeedbackReturn(rigs_[1u],
            controlsB, activeRootB);
        // SPILL lengthens the release of this gate; loop activity must not
        // reopen it, otherwise a hot speaker state can become a permanent
        // no-input oscillator after the final key release.
        const float sourceGate = !keysHeld && keyGate_ < 2.0e-4f
            ? 0.0f : keyGate_;
        const auto renderRig = [&](RigState& rig, float excitation,
                                   float rootWitness, float feedbackReturn,
                                   float rigGate,
                                   const RigControls& controls) {
            const float governingEnvelope = std::max(rig.loopEnvelope,
                linkedRigStress_ * pairGain * 0.78f);
            const float excess = std::max(0.0f,
                governingEnvelope - 0.52f);
            const float governor = 1.0f / (1.0f + excess * 13.0f);
            const float linkedMask = std::max(rig.overloadMaskAmount,
                linkedRigStress_ * pairGain * 0.72f);
            const float requestedFeedback = controls.feedback
                * (0.42f + controls.feedback * 0.67f)
                * lerp(0.74f, 1.18f, controls.proximity)
                * (1.0f + pressure_ * 0.24f)
                * lerp(1.0f, 0.18f, linkedMask);
            const float signedPolarity = (controls.polarity - 0.5f) * 2.0f;
            const float loopInput = feedbackReturn * requestedFeedback
                * signedPolarity * governor * sourceGate * rigGate;
            const float combined = smoothLimit(excitation + loopInput, 4.0f);
            const float pedal = processPedal(rig, controls, combined);
            const float rootAmount = controls.root
                * (1.0f - controls.chaos * 0.72f);
            const float ampInput = pedal
                + rootWitness * rootAmount * 0.38f;
            const float power = processAmplifier(rig, controls, ampInput);
            RigFrame frame;
            processSpeaker(rig, controls, power * speakerChoke_,
                frame.nearMic, frame.farMic);
            processOverloadMask(rig, controls,
                frame.nearMic, frame.farMic);
            const float roomSamples = clamp(static_cast<float>(sampleRate_)
                    * lerp(0.0065f, 0.00065f, controls.proximity),
                2.0f, static_cast<float>(rig.roomDelay.size() - 2u));
            frame.room = readDelay(rig.roomDelay, rig.roomWriteIndex,
                roomSamples);
            frame.sideMic = frame.farMic * 0.72f + frame.room * 0.28f;
            frame.feedbackMic = lerp(frame.nearMic, frame.sideMic,
                    controls.mic)
                + (frame.sideMic - frame.nearMic)
                    * controls.chaos * 0.32f;
            frame.mono = frame.nearMic + (frame.sideMic - frame.nearMic)
                * (0.08f + controls.mic * 0.24f);
            return frame;
        };
        const float partnerGate = smoothStep(0.0f, 0.025f, pairGain);
        RigFrame frameA = renderRig(rigs_[0u], excitationA, rootWitnessA,
            feedbackReturnA, 1.0f, controlsA);
        RigFrame frameB = renderRig(rigs_[1u], excitationB, rootWitnessB,
            feedbackReturnB, partnerGate, controlsB);

        const float crossListen = pairGain
            * (0.045f + smoothed_.pairLoose * 0.10f
                + (controlsA.selfListen + controlsB.selfListen)
                    * 0.0375f)
            * lerp(1.0f, 0.36f, linkedRigStress_);
        const float loopWitnessA = lerp(frameA.feedbackMic,
            frameB.feedbackMic, crossListen);
        const float loopWitnessB = lerp(frameB.feedbackMic,
            frameA.feedbackMic, crossListen);
        const auto writeRigHistory = [&](RigState& rig,
                                         const RigFrame& frame,
                                         float loopWitness,
                                         const RigControls& controls) {
            // Both histories receive smooth microphone signals only. This
            // keeps the two feedback systems independent without creating a
            // discontinuity when cross-listen changes.
            rig.roomDelay[rig.roomWriteIndex] = flushDenormal(
                frame.farMic * 0.82f + frame.nearMic * 0.18f);
            rig.roomWriteIndex = (rig.roomWriteIndex + 1u)
                % rig.roomDelay.size();
            rig.loopDelay[rig.loopWriteIndex] = flushDenormal(
                std::tanh(loopWitness
                    * (0.86f + controls.cone * 0.42f)));
            rig.loopWriteIndex = (rig.loopWriteIndex + 1u)
                % rig.loopDelay.size();
        };
        writeRigHistory(rigs_[0u], frameA, loopWitnessA, controlsA);
        writeRigHistory(rigs_[1u], frameB, loopWitnessB, controlsB);

        const float localStressA = std::max(rigs_[0u].overloadMaskAmount,
            std::max(rigs_[0u].speakerProtection,
                smoothStep(0.42f, 1.08f, rigs_[0u].loopEnvelope)));
        const float localStressB = std::max(rigs_[1u].overloadMaskAmount,
            std::max(rigs_[1u].speakerProtection,
                smoothStep(0.42f, 1.08f, rigs_[1u].loopEnvelope)));
        const float linkedStressTarget = pairGain
            * std::max(localStressA, localStressB);
        linkedRigStress_ += (linkedStressTarget - linkedRigStress_)
            * (linkedStressTarget > linkedRigStress_
                ? speakerProtectionAttackCoefficient_
                : speakerProtectionReleaseCoefficient_);
        linkedRigStress_ = flushDenormal(linkedRigStress_);

        const float width = 0.08f + controlsA.mic * 0.62f;
        const float legacyLeft = frameA.nearMic
            + (frameA.nearMic - frameA.sideMic) * width;
        const float legacyRight = frameA.nearMic
            - (frameA.nearMic - frameA.sideMic) * width;
        const float spread = smoothed_.pairSpread;
        const float panAngleA = (1.0f - spread) * kPi * 0.25f;
        const float panAngleB = (1.0f + spread) * kPi * 0.25f;
        constexpr float kCenterCompensation = 1.41421356237f;
        const float panALeft = std::cos(panAngleA) * kCenterCompensation;
        const float panARight = std::sin(panAngleA) * kCenterCompensation;
        const float panBLeft = std::cos(panAngleB) * kCenterCompensation;
        const float panBRight = std::sin(panAngleB) * kCenterCompensation;
        const float roomCrossfeed = pairGain * spread * 0.075f
            * lerp(0.72f, 1.0f,
                (controlsA.proximity + controlsB.proximity) * 0.5f);
        const float voiceA = frameA.mono + frameB.room * roomCrossfeed;
        const float voiceB = frameB.mono + frameA.room * roomCrossfeed;
        const float dualLeft = (voiceA * panALeft
                + voiceB * pairGain * panBLeft)
            * pairNormalization;
        const float dualRight = (voiceA * panARight
                + voiceB * pairGain * panBRight)
            * pairNormalization;
        const float topologyBlend = smoothStep(0.0f, 0.08f, pairGain);
        float frameLeft = lerp(legacyLeft, dualLeft, topologyBlend);
        float frameRight = lerp(legacyRight, dualRight, topologyBlend);
        const float separation = std::abs(frameLeft - frameRight) * 0.5f;
        stereoSeparationActivity_ += (separation
                - stereoSeparationActivity_)
            * (separation > stereoSeparationActivity_
                ? meterAttackCoefficient_ : meterReleaseCoefficient_);
        stereoSeparationActivity_ = flushDenormal(
            stereoSeparationActivity_);
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
        const float limiterTarget = peak > 0.92f ? 0.92f / peak : 1.0f;
        const float previousLimiterGain = limiterGain_;
        const bool limiterAttacking = limiterTarget
            < limiterGain_ - 1.0e-5f;
        limiterGain_ += (limiterTarget - limiterGain_)
            * (limiterAttacking ? limiterAttackCoefficient_
                                : limiterReleaseCoefficient_);
        limiterGain_ = flushDenormal(limiterGain_);
        maximumLimiterGainStep_ = std::max(maximumLimiterGainStep_,
            std::abs(limiterGain_ - previousLimiterGain));
        if (limiterAttacking && !limiterWasAttacking_)
            ++limiterAttackEventCount_;
        limiterWasAttacking_ = limiterAttacking;

        frameLeft *= limiterGain_;
        frameRight *= limiterGain_;
        const float limitedPeak = std::max(std::abs(frameLeft),
            std::abs(frameRight));
        const float ceilingPeak = softCeiling(limitedPeak, 0.90f, 0.995f);
        const float ceilingGain = limitedPeak > 1.0e-9f
            ? ceilingPeak / limitedPeak : 1.0f;
        left = safeOutput(frameLeft * ceilingGain);
        right = safeOutput(frameRight * ceilingGain);
        outputPeak_ += (std::max(std::abs(left), std::abs(right)) - outputPeak_)
            * (peak > outputPeak_ ? meterAttackCoefficient_
                                  : meterReleaseCoefficient_);
        outputPeak_ = flushDenormal(outputPeak_);

        const bool laneActive = anyLaneActive();
        signalActive_ = keysHeld || laneActive || keyGate_ > 1.0e-5f
            || rigs_[0u].loopActivity > 1.0e-5f
            || rigs_[1u].loopActivity > 1.0e-5f
            || outputPeak_ > 1.0e-5f;
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
    float feedbackActivity() const
    {
        return std::max(rigs_[0u].loopActivity, rigs_[1u].loopActivity);
    }
    float outputPeak() const { return outputPeak_; }
    float loopDelaySamples() const { return rigs_[0u].loopDelaySamples; }
    float sagEnvelope() const
    {
        return std::max(rigs_[0u].sagEnvelope, rigs_[1u].sagEnvelope);
    }
    float feedbackBodyActivity() const
    {
        return std::max(rigs_[0u].bodyEnvelope, rigs_[1u].bodyEnvelope);
    }
    float feedbackStabActivity() const
    {
        return std::max(rigs_[0u].stabEnvelope, rigs_[1u].stabEnvelope);
    }
    float targetGlitchActivity() const
    {
        return std::max(rigs_[0u].glitchActivity,
            rigs_[1u].glitchActivity);
    }
    uint64_t targetGlitchTriggerCount() const
    {
        return rigs_[0u].glitchTriggerCount + rigs_[1u].glitchTriggerCount;
    }
    float overloadMaskActivity() const
    {
        return std::max(rigs_[0u].overloadMaskAmount,
            rigs_[1u].overloadMaskAmount);
    }
    float speakerProtectionActivity() const
    {
        return std::max(rigs_[0u].speakerProtection,
            rigs_[1u].speakerProtection);
    }
    float speakerProtectionPeak() const
    {
        return std::max(rigs_[0u].speakerProtectionPeak,
            rigs_[1u].speakerProtectionPeak);
    }
    float speakerModePreLimitPeak() const
    {
        return std::max(rigs_[0u].speakerModePreLimitPeak,
            rigs_[1u].speakerModePreLimitPeak);
    }
    uint64_t speakerSoftLimitCount() const
    {
        return rigs_[0u].speakerSoftLimitCount
            + rigs_[1u].speakerSoftLimitCount;
    }
    uint64_t micSoftLimitCount() const
    {
        return rigs_[0u].micSoftLimitCount + rigs_[1u].micSoftLimitCount;
    }
    uint64_t limiterAttackEventCount() const
    {
        return limiterAttackEventCount_;
    }
    float maximumLimiterGainStep() const { return maximumLimiterGainStep_; }
    float pairSideActivity() const { return stereoSeparationActivity_; }
    int partnerRootNote() const { return partnerRootNote_; }
    uint64_t arpStepCount() const { return arpeggiators_[0u].stepCount; }
    int arpCurrentNote() const { return arpeggiators_[0u].currentNote; }
    uint64_t partnerArpStepCount() const
    {
        return arpeggiators_[1u].stepCount;
    }
    int partnerArpCurrentNote() const
    {
        return arpeggiators_[1u].currentNote;
    }

private:
    enum class EnvelopeStage : uint8_t {
        Idle,
        Attack,
        Decay,
        Sustain,
        Release,
    };

    struct ExciterLane {
        std::vector<float> delay;
        size_t writeIndex = 0u;
        int sourceNote = -1;
        float noteOffset = 0.0f;
        float gain = 0.0f;
        float targetGain = 0.0f;
        float attackGain = 1.0f;
        float targetAttackGain = 1.0f;
        float velocity = 0.0f;
        float currentFrequency = 110.0f;
        float targetFrequency = 110.0f;
        float overshootSemitones = 0.0f;
        float phase = 0.0f;
        float pickEnvelope = 0.0f;
        float pickSmoothed = 0.0f;
        float pickPacketLow = 0.0f;
        float amplitude = 0.0f;
        float wireLow = 0.0f;
        float pickNoiseLow = 0.0f;
        float pickNoiseBody = 0.0f;
        float pickupPrevious = 0.0f;
        float pickupVelocity = 0.0f;
        float pickupLow = 0.0f;
        float rootLow = 0.0f;
        float outputSmoothed = 0.0f;
        float materialLow = 0.0f;
        float materialBodyLow = 0.0f;
        float dispersionInput = 0.0f;
        float dispersionOutput = 0.0f;
        float articulationEnvelope = 0.0f;
        float pickDirection = 1.0f;
        float detuneSemitones = 0.0f;
        uint32_t startDelaySamples = 0u;
        uint32_t samplesSinceAttack = 0xffffffffu;
        uint32_t random = 1u;
        uint64_t age = 0u;
        EnvelopeStage envelopeStage = EnvelopeStage::Idle;
        bool envelopeGate = false;
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

    struct MaterialProfile {
        float brightness = 0.0f;
        float sustain = 0.0f;
        float dispersion = 0.0f;
        float pickupShift = 0.0f;
        float bodyFrequency = 220.0f;
        float bodyAmount = 0.0f;
        float metallic = 0.0f;
    };

    struct RigState {
        std::vector<float> loopDelay;
        std::vector<float> roomDelay;
        std::vector<float> glitchHistory;
        size_t loopWriteIndex = 0u;
        size_t roomWriteIndex = 0u;
        size_t glitchWriteIndex = 0u;
        float loopDelaySamples = 120.0f;
        float stabDelaySamples = 38.0f;
        float loopDcInput = 0.0f;
        float loopDcOutput = 0.0f;
        float loopLow = 0.0f;
        float loopHighLow = 0.0f;
        float stabDcInput = 0.0f;
        float stabDcOutput = 0.0f;
        float stabLow = 0.0f;
        float stabBand = 0.0f;
        float bodyEnvelope = 0.0f;
        float stabEnvelope = 0.0f;
        float selfFocus = 0.0f;
        float glitchEnvelope = 0.0f;
        float glitchActivity = 0.0f;
        float glitchPhase = 0.0f;
        size_t glitchCaptureStart = 0u;
        uint32_t glitchCellSamples = 16u;
        uint32_t glitchRepeatIndex = 0u;
        uint32_t glitchRepeatCount = 0u;
        uint32_t glitchArmAgeSamples = 0u;
        uint64_t glitchTriggerCount = 0u;
        bool glitchArmed = false;
        bool glitchPlaying = false;
        float overloadLevel = 0.0f;
        float overloadRoughness = 0.0f;
        float overloadMaskAmount = 0.0f;
        float overloadPreviousMic = 0.0f;
        float overloadLowNear = 0.0f;
        float overloadLowFar = 0.0f;
        float speakerProtection = 0.0f;
        float speakerProtectionPeak = 0.0f;
        float speakerModePreLimitPeak = 0.0f;
        uint64_t speakerSoftLimitCount = 0u;
        uint64_t micSoftLimitCount = 0u;
        float loopEnvelope = 0.0f;
        float loopActivity = 0.0f;
        std::array<DriveState, kProcessorStackCircuitCount> pedalStates {};
        std::array<DriveState, kProcessorStackCircuitCount> pedalIdleStates {};
        ProcessorStackCircuit activeCircuit = ProcessorStackCircuit::Rat;
        ProcessorStackCircuit previousCircuit = ProcessorStackCircuit::Rat;
        float circuitFade = 1.0f;
        float pedalDcInput = 0.0f;
        float pedalDcOutput = 0.0f;
        float preampPreviousInput = 0.0f;
        float amplifierPreviousInput = 0.0f;
        float preampMemory = 0.0f;
        float toneLow = 0.0f;
        float toneMidLow = 0.0f;
        float toneHighLow = 0.0f;
        float sagEnvelope = 0.0f;
        float transformerLow = 0.0f;
        float coilEnvelope = 0.0f;
        float speakerDc = 0.0f;
        std::array<SpeakerModeState, kSpeakerModeCount> speakerModes {};
    };

    struct RigFrame {
        float nearMic = 0.0f;
        float farMic = 0.0f;
        float sideMic = 0.0f;
        float room = 0.0f;
        float feedbackMic = 0.0f;
        float mono = 0.0f;
    };

    struct RigControls {
        ProcessorStackCircuit circuit = ProcessorStackCircuit::Rat;
        float bite = 0.0f;
        float pedalTone = 0.0f;
        float bias = 0.0f;
        float stack = 0.0f;
        float sag = 0.0f;
        float focus = 0.0f;
        float cone = 0.0f;
        float cabinet = 0.0f;
        float mic = 0.0f;
        float feedback = 0.0f;
        float proximity = 0.0f;
        float harmonic = 0.0f;
        float tracking = 0.0f;
        float polarity = 0.0f;
        float root = 0.0f;
        float chaos = 0.0f;
        float pierce = 0.0f;
        float selfListen = 0.0f;
        float targetGlitch = 0.0f;
        float glitchRatchet = 0.0f;
        float overloadMask = 0.0f;
    };

    struct ArpControls {
        ProcessorStackArpPattern pattern = ProcessorStackArpPattern::Off;
        ProcessorStackScale scale = ProcessorStackScale::Phrygian;
        ProcessorStackArpRate rate = ProcessorStackArpRate::Sixteenth;
        uint32_t octaves = 2u;
        float gate = 0.62f;
        uint32_t length = 8u;
        std::array<int32_t, 8u> steps {};
    };

    static constexpr int64_t kUnprimedHostStep =
        std::numeric_limits<int64_t>::min();

    struct ArpState {
        double phaseSamples = 0.0;
        int64_t stepIndex = 0;
        uint64_t stepCount = 0u;
        int currentNote = -1;
        bool gateOpen = false;
        int64_t hostStep = kUnprimedHostStep;
    };

    static ExciterLane resetLanePreservingDelay(std::vector<float> delay)
    {
        ExciterLane lane;
        lane.delay = std::move(delay);
        return lane;
    }

    void resetRig(RigState& rig, uint32_t player)
    {
        auto loopDelay = std::move(rig.loopDelay);
        auto roomDelay = std::move(rig.roomDelay);
        auto glitchHistory = std::move(rig.glitchHistory);
        rig = RigState {};
        rig.loopDelay = std::move(loopDelay);
        rig.roomDelay = std::move(roomDelay);
        rig.glitchHistory = std::move(glitchHistory);
        std::fill(rig.loopDelay.begin(), rig.loopDelay.end(), 0.0f);
        std::fill(rig.roomDelay.begin(), rig.roomDelay.end(), 0.0f);
        std::fill(rig.glitchHistory.begin(), rig.glitchHistory.end(), 0.0f);
        rig.loopDelaySamples = static_cast<float>(sampleRate_ * 0.0025);
        rig.stabDelaySamples = static_cast<float>(sampleRate_ * 0.0008);
        const ProcessorStackCircuit circuit = player == 1u
                && !params_.linkPedal
            ? params_.circuitB : params_.circuit;
        rig.activeCircuit = circuit;
        rig.previousCircuit = circuit;
    }

    void clearRigSignalState(RigState& rig, uint32_t player)
    {
        const float protectionPeak = rig.speakerProtectionPeak;
        const float modePeak = rig.speakerModePreLimitPeak;
        const uint64_t speakerLimits = rig.speakerSoftLimitCount;
        const uint64_t microphoneLimits = rig.micSoftLimitCount;
        const uint64_t glitchTriggers = rig.glitchTriggerCount;
        resetRig(rig, player);
        rig.speakerProtectionPeak = protectionPeak;
        rig.speakerModePreLimitPeak = modePeak;
        rig.speakerSoftLimitCount = speakerLimits;
        rig.micSoftLimitCount = microphoneLimits;
        rig.glitchTriggerCount = glitchTriggers;
    }

    RigControls rigControls(uint32_t player) const
    {
        const bool partner = player == 1u;
        const bool ownPedal = partner && !smoothed_.linkPedal;
        const bool ownAmplifier = partner && !smoothed_.linkAmplifier;
        const bool ownFeedback = partner && !smoothed_.linkFeedback;
        RigControls controls;
        controls.circuit = ownPedal ? smoothed_.circuitB : smoothed_.circuit;
        controls.bite = ownPedal ? smoothed_.biteB : smoothed_.bite;
        controls.pedalTone = ownPedal
            ? smoothed_.pedalToneB : smoothed_.pedalTone;
        controls.bias = ownPedal ? smoothed_.biasB : smoothed_.bias;
        controls.stack = ownAmplifier ? smoothed_.stackB : smoothed_.stack;
        controls.sag = ownAmplifier ? smoothed_.sagB : smoothed_.sag;
        controls.focus = ownAmplifier ? smoothed_.focusB : smoothed_.focus;
        controls.cone = ownAmplifier ? smoothed_.coneB : smoothed_.cone;
        controls.cabinet = ownAmplifier
            ? smoothed_.cabinetB : smoothed_.cabinet;
        controls.mic = ownAmplifier ? smoothed_.micB : smoothed_.mic;
        controls.feedback = ownFeedback
            ? smoothed_.feedbackB : smoothed_.feedback;
        controls.proximity = ownFeedback
            ? smoothed_.proximityB : smoothed_.proximity;
        controls.harmonic = ownFeedback
            ? smoothed_.harmonicB : smoothed_.harmonic;
        controls.tracking = ownFeedback
            ? smoothed_.trackingB : smoothed_.tracking;
        controls.polarity = ownFeedback
            ? smoothed_.polarityB : smoothed_.polarity;
        controls.root = ownFeedback ? smoothed_.rootB : smoothed_.root;
        controls.chaos = ownFeedback ? smoothed_.chaosB : smoothed_.chaos;
        controls.pierce = ownFeedback ? smoothed_.pierceB : smoothed_.pierce;
        controls.selfListen = ownFeedback
            ? smoothed_.selfListenB : smoothed_.selfListen;
        controls.targetGlitch = ownFeedback
            ? smoothed_.targetGlitchB : smoothed_.targetGlitch;
        controls.glitchRatchet = ownFeedback
            ? smoothed_.glitchRatchetB : smoothed_.glitchRatchet;
        controls.overloadMask = ownFeedback
            ? smoothed_.overloadMaskB : smoothed_.overloadMask;
        return controls;
    }

    ArpControls arpControls(uint32_t player) const
    {
        ArpControls controls;
        if (player == 0u) {
            controls.pattern = params_.arpPattern;
            controls.scale = params_.scale;
            controls.rate = params_.arpRate;
            controls.octaves = params_.arpOctaves;
            controls.gate = smoothed_.arpGate;
            controls.length = params_.customPatternLength;
            controls.steps = params_.customPattern;
        } else {
            controls.pattern = params_.arpPatternB;
            controls.scale = params_.scaleB;
            controls.rate = params_.arpRateB;
            controls.octaves = params_.arpOctavesB;
            controls.gate = smoothed_.arpGateB;
            controls.length = params_.customPatternLengthB;
            controls.steps = params_.customPatternB;
        }
        return controls;
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

    static float smoothLimit(float value, float limit)
    {
        if (!std::isfinite(value)) return 0.0f;
        limit = std::max(1.0e-6f, limit);
        const float normalized = value / limit;
        const float squared = normalized * normalized;
        return value / std::sqrt(std::sqrt(1.0f + squared * squared));
    }

    static float smoothStep(float lower, float upper, float value)
    {
        const float normalized = clamp((value - lower)
                / std::max(1.0e-6f, upper - lower),
            0.0f, 1.0f);
        return normalized * normalized * (3.0f - 2.0f * normalized);
    }

    static float softCeiling(float magnitude, float knee, float ceiling)
    {
        if (!std::isfinite(magnitude)) return 0.0f;
        magnitude = std::max(0.0f, magnitude);
        ceiling = std::max(knee + 1.0e-6f, ceiling);
        if (magnitude <= knee) return magnitude;
        const float headroom = ceiling - knee;
        return knee + headroom * (1.0f
            - std::exp(-(magnitude - knee) / headroom));
    }

    static float softClip(float value, float knee, float ceiling)
    {
        if (!std::isfinite(value)) return 0.0f;
        return std::copysign(softCeiling(std::abs(value), knee, ceiling),
            value);
    }

    static MaterialProfile materialProfile(
        ProcessorStackNeckMaterial neck,
        ProcessorStackBodyMaterial body)
    {
        MaterialProfile profile;
        switch (neck) {
        case ProcessorStackNeckMaterial::Maple:
            break;
        case ProcessorStackNeckMaterial::Mahogany:
            profile.brightness -= 0.18f;
            profile.sustain -= 0.00055f;
            profile.bodyAmount += 0.055f;
            break;
        case ProcessorStackNeckMaterial::Aluminum:
            profile.brightness += 0.34f;
            profile.sustain += 0.0010f;
            profile.dispersion += 0.038f;
            profile.pickupShift -= 0.025f;
            profile.metallic += 0.15f;
            break;
        case ProcessorStackNeckMaterial::Composite:
            profile.brightness += 0.12f;
            profile.sustain += 0.00045f;
            profile.dispersion += 0.014f;
            profile.metallic += 0.035f;
            break;
        case ProcessorStackNeckMaterial::Count:
            break;
        }
        switch (body) {
        case ProcessorStackBodyMaterial::SolidWood:
            profile.bodyFrequency = 235.0f;
            break;
        case ProcessorStackBodyMaterial::HollowWood:
            profile.brightness -= 0.12f;
            profile.sustain -= 0.00035f;
            profile.bodyFrequency = 155.0f;
            profile.bodyAmount += 0.34f;
            break;
        case ProcessorStackBodyMaterial::SemiHollow:
            profile.brightness -= 0.04f;
            profile.bodyFrequency = 205.0f;
            profile.bodyAmount += 0.20f;
            break;
        case ProcessorStackBodyMaterial::Aluminum:
            profile.brightness += 0.25f;
            profile.sustain += 0.00085f;
            profile.dispersion += 0.026f;
            profile.bodyFrequency = 465.0f;
            profile.bodyAmount += 0.12f;
            profile.metallic += 0.12f;
            break;
        case ProcessorStackBodyMaterial::Count:
            break;
        }
        return profile;
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
        case ProcessorStackArpRate::Quarter: return 1.0f;
        case ProcessorStackArpRate::Half: return 2.0f;
        case ProcessorStackArpRate::Whole: return 4.0f;
        case ProcessorStackArpRate::Count: break;
        }
        return 0.25f;
    }

    static uint32_t positiveModulo(int64_t value, uint32_t modulus)
    {
        if (modulus == 0u) return 0u;
        int64_t remainder = value % static_cast<int64_t>(modulus);
        if (remainder < 0) remainder += static_cast<int64_t>(modulus);
        return static_cast<uint32_t>(remainder);
    }

    static int64_t floorDivide(int64_t value, int64_t divisor)
    {
        const int64_t quotient = value / divisor;
        const int64_t remainder = value % divisor;
        return quotient - (remainder < 0 ? 1 : 0);
    }

    uint32_t arpSequencePosition(int64_t step,
        const ArpControls& controls) const
    {
        const uint32_t degrees = scaleDegreeCount(controls.scale);
        const uint32_t length = std::max(1u, degrees * controls.octaves);
        const uint32_t position = positiveModulo(step, length);
        switch (controls.pattern) {
        case ProcessorStackArpPattern::Down:
            return length - 1u - position;
        case ProcessorStackArpPattern::Pendulum: {
            if (length <= 1u) return 0u;
            const uint32_t cycle = length * 2u - 2u;
            const uint32_t pendulum = positiveModulo(step, cycle);
            return pendulum < length ? pendulum : cycle - pendulum;
        }
        case ProcessorStackArpPattern::Pedal:
            return positiveModulo(step, 2u) == 0u ? 0u
                : 1u + positiveModulo(floorDivide(step, 2),
                    std::max(1u, length - 1u));
        case ProcessorStackArpPattern::Scramble:
            // An odd stride walks every position before repeating while
            // avoiding a stored/random sequence in the audio thread.
            return (positiveModulo(step, length) * 5u
                + positiveModulo(floorDivide(step, 3), length)) % length;
        case ProcessorStackArpPattern::Custom:
            return position;
        case ProcessorStackArpPattern::Off:
        case ProcessorStackArpPattern::Up:
        case ProcessorStackArpPattern::Count:
            return position;
        }
        return position;
    }

    int arpeggiatedNote(int64_t step, const ArpControls& controls,
        int root) const
    {
        if (root < 0) return -1;
        if (controls.pattern == ProcessorStackArpPattern::Custom) {
            const uint32_t length = controls.length;
            const uint32_t index = positiveModulo(step, length);
            int result = root + signedScaleSemitone(
                controls.scale, controls.steps[index]);
            while (result > 127) result -= 12;
            while (result < 0) result += 12;
            return std::clamp(result, 0, 127);
        }
        const uint32_t degrees = scaleDegreeCount(controls.scale);
        const uint32_t position = arpSequencePosition(step, controls);
        const int offset = scaleSemitone(controls.scale, position % degrees)
            + 12 * static_cast<int>(position / degrees);
        int result = root + offset;
        while (result > 127) result -= 12;
        return std::clamp(result, 0, 127);
    }

    void advanceArpeggiator(uint32_t player)
    {
        auto& state = arpeggiators_[std::min<uint32_t>(player, 1u)];
        const ArpControls controls = arpControls(player);
        int root = latestHeldNote();
        if (player == 1u
            && params_.arpBRelation == ProcessorStackArpRelation::Counter
            && root >= 0) {
            root = partnerRootFor(root, true);
        }
        const int note = arpeggiatedNote(state.stepIndex, controls, root);
        if (note < 0) return;
        applyCrookedGesture(note, note == lastPlayedNote_);
        lastPlayedNote_ = note;
        state.currentNote = note;
        state.gateOpen = true;
        if (player == 0u) {
            rebuildPrimaryVoicing(true);
            if (params_.arpBRelation == ProcessorStackArpRelation::Follow) {
                configurePartnerVoicing(note, true);
            }
        } else {
            partnerRootNote_ = note;
            configureIndependentPartnerVoicing(note, true, true);
        }
        ++state.stepIndex;
        ++state.stepCount;
    }

    struct HostArpPosition {
        int64_t step = 0;
        double fraction = 0.0;
    };

    bool usesHostArpClock() const
    {
        return params_.arpHostSync && hostTransportActive_;
    }

    HostArpPosition hostArpPosition(uint32_t player,
        const ArpControls& controls) const
    {
        const double stepBeats = static_cast<double>(
            arpStepBeats(controls.rate));
        double phase = player == 1u
            ? static_cast<double>(params_.arpPhaseB) : 0.0;
        // A full-step phase has the same grid alignment as zero in a
        // continuously running host timeline.
        if (phase >= 1.0 - 1.0e-9) phase = 0.0;
        const double position = hostTransportBeat_ / stepBeats - phase;
        const double floored = std::floor(position);
        HostArpPosition result;
        if (floored <= static_cast<double>(
                std::numeric_limits<int64_t>::min())) {
            result.step = std::numeric_limits<int64_t>::min() + 1;
        } else if (floored >= static_cast<double>(
                std::numeric_limits<int64_t>::max())) {
            result.step = std::numeric_limits<int64_t>::max() - 1;
        } else {
            result.step = static_cast<int64_t>(floored);
        }
        result.fraction = std::clamp(position - floored, 0.0, 1.0);
        return result;
    }

    bool isHostArpBoundary(const HostArpPosition& position,
        const ArpControls& controls) const
    {
        const double beatsPerSample = static_cast<double>(tempoBpm_)
            / (60.0 * sampleRate_);
        const double tolerance = std::max(1.0e-9,
            beatsPerSample / static_cast<double>(
                arpStepBeats(controls.rate)) * 1.5);
        return position.fraction <= tolerance;
    }

    void primeHostArpeggiator(uint32_t player,
        const ArpControls& controls, bool trigger)
    {
        auto& state = arpeggiators_[std::min<uint32_t>(player, 1u)];
        const HostArpPosition position = hostArpPosition(player, controls);
        state.hostStep = position.step;
        state.stepIndex = position.step;
        if (trigger && isHostArpBoundary(position, controls)) {
            advanceArpeggiator(player);
        }
    }

    void resetArpeggiator(uint32_t player, bool trigger)
    {
        auto& state = arpeggiators_[std::min<uint32_t>(player, 1u)];
        const ArpControls controls = arpControls(player);
        if (state.gateOpen) closeArpeggiatorGate(player);
        state.phaseSamples = 0.0;
        state.stepIndex = 0;
        state.currentNote = -1;
        state.gateOpen = false;
        state.hostStep = kUnprimedHostStep;
        if (trigger && controls.pattern != ProcessorStackArpPattern::Off
            && noteOrderSize_ > 0u) {
            if (usesHostArpClock()) {
                primeHostArpeggiator(player, controls, true);
            } else if (player == 1u && params_.arpPhaseB > 1.0e-4f) {
                const double stepSamples = std::max(1.0,
                    static_cast<double>(sampleRate_) * 60.0
                        / static_cast<double>(tempoBpm_)
                        * static_cast<double>(arpStepBeats(controls.rate)));
                state.phaseSamples = stepSamples
                    * (1.0 - static_cast<double>(params_.arpPhaseB));
            } else {
                advanceArpeggiator(player);
            }
        } else if (controls.pattern == ProcessorStackArpPattern::Off
            && noteOrderSize_ > 0u) {
            if (player == 0u) rebuildPrimaryVoicing(false);
            else rebuildPartnerVoicing(false);
        }
    }

    void closeArpeggiatorGate(uint32_t player)
    {
        auto& state = arpeggiators_[std::min<uint32_t>(player, 1u)];
        state.gateOpen = false;
        if (player == 0u) {
            for (auto& lane : lanes_) lane.held = false;
            if (params_.arpBRelation == ProcessorStackArpRelation::Follow) {
                for (auto& lane : partnerLanes_) lane.held = false;
            }
        } else {
            for (auto& lane : partnerLanes_) lane.held = false;
        }
    }

    void processArpeggiator(uint32_t player)
    {
        const ArpControls controls = arpControls(player);
        if (controls.pattern == ProcessorStackArpPattern::Off
            || noteOrderSize_ == 0u) return;
        auto& state = arpeggiators_[std::min<uint32_t>(player, 1u)];
        if (usesHostArpClock()) {
            const HostArpPosition position = hostArpPosition(player, controls);
            if (state.hostStep == kUnprimedHostStep) {
                primeHostArpeggiator(player, controls, true);
                return;
            }
            if (position.step != state.hostStep) {
                if (state.gateOpen) closeArpeggiatorGate(player);
                state.currentNote = -1;
                state.hostStep = position.step;
                state.stepIndex = position.step;
                if (isHostArpBoundary(position, controls)) {
                    advanceArpeggiator(player);
                }
                return;
            }
            if (state.gateOpen
                && position.fraction >= static_cast<double>(controls.gate)) {
                closeArpeggiatorGate(player);
            }
            return;
        }
        const double stepSamples = std::max(1.0,
            static_cast<double>(sampleRate_) * 60.0
                / static_cast<double>(tempoBpm_)
                * static_cast<double>(arpStepBeats(controls.rate)));
        state.phaseSamples += 1.0;
        const double gateSamples = stepSamples
            * static_cast<double>(controls.gate);
        if (state.gateOpen && state.phaseSamples >= gateSamples) {
            closeArpeggiatorGate(player);
        }
        if (state.phaseSamples >= stepSamples) {
            state.phaseSamples -= stepSamples;
            advanceArpeggiator(player);
        }
    }

    void processArpeggiators()
    {
        if (scorePlaybackActive_) return;
        processArpeggiator(0u);
        if (params_.pairAmount > 1.0e-4f
            && params_.arpBRelation != ProcessorStackArpRelation::Follow) {
            processArpeggiator(1u);
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
        float gain, float attackGainTarget, float velocity, bool retrigger,
        bool held, uint32_t player)
    {
        const float target = noteFrequency(
            static_cast<float>(sourceNote) + noteOffset);
        const bool wasActive = lane.active;
        if (!wasActive) {
            lane.currentFrequency = target;
            // New chord tones begin at their share of the common attack
            // budget, then slew toward the sustained SHAPE balance.
            lane.gain = std::min(gain, attackGainTarget);
            lane.attackGain = attackGainTarget;
        }
        lane.sourceNote = sourceNote;
        lane.noteOffset = noteOffset;
        lane.targetFrequency = target;
        lane.targetGain = gain;
        lane.targetAttackGain = attackGainTarget;
        lane.velocity = velocity;
        lane.held = held;
        lane.active = true;
        if (!wasActive || retrigger) lane.age = ageCounter_++;
        if (retrigger) {
            const uint32_t rigIndex = std::min<uint32_t>(player, 1u);
            const RigControls controls = rigControls(rigIndex);
            rigs_[rigIndex].glitchArmed =
                controls.targetGlitch > 1.0e-4f;
            rigs_[rigIndex].glitchArmAgeSamples = 0u;
            const float pickCurve = params_.pick * params_.pick;
            const float recoverySamples = static_cast<float>(sampleRate_)
                * lerp(0.004f, 0.032f, pickCurve);
            const float recovered = clamp(
                static_cast<float>(lane.samplesSinceAttack)
                    / std::max(1.0f, recoverySamples),
                0.0f, 1.0f);
            float attackDensity = lerp(1.0f, std::max(0.38f, recovered),
                pickCurve);
            const ArpControls arp = player == 1u
                    && params_.arpBRelation
                        != ProcessorStackArpRelation::Follow
                ? arpControls(1u) : arpControls(0u);
            if (arp.pattern != ProcessorStackArpPattern::Off) {
                const float stepSeconds = 60.0f
                    / std::max(20.0f, tempoBpm_)
                    * arpStepBeats(arp.rate);
                const float tempoDensity = clamp(stepSeconds / 0.045f,
                    0.38f, 1.0f);
                attackDensity = std::min(attackDensity,
                    lerp(1.0f, tempoDensity, pickCurve));
            }
            lane.pickEnvelope = std::max(
                lane.pickEnvelope, attackDensity);
            lane.amplitude = std::max(lane.amplitude,
                0.18f + velocity * 0.82f);
            if (recovered > 0.72f) {
                lane.pickDirection = -lane.pickDirection;
            }
            lane.samplesSinceAttack = 0u;
            lane.envelopeGate = held;
            lane.envelopeStage = held
                ? EnvelopeStage::Attack : EnvelopeStage::Release;
            lane.random ^= static_cast<uint32_t>(
                static_cast<uint32_t>(sourceNote + 1) * 0x9e3779b9u
                    + static_cast<uint32_t>(lane.age));
            const int previousRoot = player == 1u
                ? partnerRootNote_ : lastRootNote_;
            const int interval = previousRoot >= 0
                ? sourceNote - previousRoot : 0;
            if (std::abs(interval) >= 7) {
                lane.overshootSemitones = std::copysign(
                    params_.crooked * std::min(2.8f,
                        0.13f * static_cast<float>(std::abs(interval))),
                    static_cast<float>(interval));
            }
            if (player == 1u) {
                const float delayScatter = 0.5f
                    + nextNoise(lane.random) * 0.5f;
                const float delayMs = params_.pairLoose
                    * (1.2f + delayScatter * 10.0f);
                lane.startDelaySamples = static_cast<uint32_t>(
                    delayMs * 0.001f * static_cast<float>(sampleRate_));
                lane.detuneSemitones = params_.pairLoose
                    * nextNoise(lane.random) * 0.065f;
            }
        }
    }

    int partnerRootFor(int primaryRoot, bool retrigger)
    {
        int partnerRoot = primaryRoot;
        switch (params_.pairRelation) {
        case ProcessorStackPairRelation::Unison:
            break;
        case ProcessorStackPairRelation::FourthDown:
            partnerRoot -= 5;
            break;
        case ProcessorStackPairRelation::FifthUp:
            partnerRoot += 7;
            break;
        case ProcessorStackPairRelation::OctaveUp:
            partnerRoot += 12;
            break;
        case ProcessorStackPairRelation::Contrary:
            if (partnerRootNote_ < 0 || partnerPrimaryRoot_ < 0) {
                partnerRootNote_ = primaryRoot + 7;
            } else if (retrigger && primaryRoot != partnerPrimaryRoot_) {
                partnerRootNote_ -= primaryRoot - partnerPrimaryRoot_;
            }
            partnerRoot = partnerRootNote_;
            break;
        case ProcessorStackPairRelation::Count:
            break;
        }
        partnerRoot = std::clamp(partnerRoot, 12, 115);
        partnerRootNote_ = partnerRoot;
        partnerPrimaryRoot_ = primaryRoot;
        return partnerRoot;
    }

    void configureBankVoicing(std::array<ExciterLane, kExciterCount>& bank,
        int rootNote, float velocity, bool retrigger, bool arpeggiating,
        bool gateOpen, uint32_t player)
    {
        if (rootNote < 0) {
            for (auto& lane : bank) lane.held = false;
            return;
        }
        if (params_.mode == ProcessorStackMode::Lead
            || (params_.mode == ProcessorStackMode::Hand
                && (arpeggiating || player == 1u))) {
            configureLane(bank[0u], rootNote, 0.0f, 1.0f,
                1.0f, velocity, retrigger,
                !arpeggiating || gateOpen, player);
            for (uint32_t lane = 1u; lane < bank.size(); ++lane) {
                bank[lane].held = false;
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
            // SHAPE owns the sustained chord balance, not the number or
            // intensity of coincident pick transients. Keep one normalized
            // attack budget and let the upper tones grow through their
            // strings and their player's shared stack.
            // Root attack stays invariant across SHAPE; only its sustained
            // balance falls as the upper chord tones bloom in.
            const float rawRootAttack = 1.0f;
            const float rawFifthAttack = fifthGain * 0.08f;
            const float rawOctaveAttack = octaveGain * 0.008f;
            const float attackNorm = 1.0f / std::max(1.0f,
                std::sqrt(rawRootAttack * rawRootAttack
                    + rawFifthAttack * rawFifthAttack
                    + rawOctaveAttack * rawOctaveAttack));
            configureLane(bank[0u], rootNote, 0.0f, rootGain,
                rawRootAttack * attackNorm, velocity, retrigger,
                !arpeggiating || gateOpen, player);
            configureLane(bank[1u], rootNote, 7.0f, fifthGain,
                rawFifthAttack * attackNorm, velocity * 0.94f, retrigger,
                !arpeggiating || gateOpen, player);
            configureLane(bank[2u], rootNote, 12.0f, octaveGain,
                rawOctaveAttack * attackNorm, velocity * 0.88f, retrigger,
                octaveGain > 1.0e-4f
                    && (!arpeggiating || gateOpen), player);
            bank[3u].held = false;
        }
    }

    float heldRootVelocity() const
    {
        const int heldRoot = latestHeldNote();
        return heldRoot >= 0
            ? heldVelocities_[static_cast<size_t>(heldRoot)] : 0.8f;
    }

    void rebuildPrimaryVoicing(bool retrigger)
    {
        const bool arpeggiating =
            params_.arpPattern != ProcessorStackArpPattern::Off;
        const int rootNote = arpeggiating
            ? arpeggiators_[0u].currentNote : latestHeldNote();
        configureBankVoicing(lanes_, rootNote, heldRootVelocity(), retrigger,
            arpeggiating, arpeggiators_[0u].gateOpen, 0u);
        if (rootNote >= 0) lastRootNote_ = rootNote;
    }

    void configurePartnerVoicing(int primaryRoot, bool retrigger)
    {
        if (params_.pairAmount <= 1.0e-4f) {
            for (auto& lane : partnerLanes_) lane.held = false;
            return;
        }
        const int rootNote = partnerRootFor(primaryRoot, retrigger);
        const bool arpeggiating =
            params_.arpPattern != ProcessorStackArpPattern::Off;
        configureBankVoicing(partnerLanes_, rootNote,
            heldRootVelocity() * 0.94f, retrigger, arpeggiating,
            arpeggiators_[0u].gateOpen, 1u);
        partnerRootNote_ = rootNote;
    }

    void configureIndependentPartnerVoicing(int rootNote, bool retrigger,
        bool arpeggiating)
    {
        if (params_.pairAmount <= 1.0e-4f) {
            for (auto& lane : partnerLanes_) lane.held = false;
            return;
        }
        configureBankVoicing(partnerLanes_, rootNote,
            heldRootVelocity() * 0.94f, retrigger, arpeggiating,
            arpeggiators_[1u].gateOpen, 1u);
        partnerRootNote_ = rootNote;
    }

    void rebuildPartnerVoicing(bool retrigger)
    {
        if (params_.pairAmount <= 1.0e-4f) {
            for (auto& lane : partnerLanes_) lane.held = false;
            return;
        }
        if (params_.arpBRelation == ProcessorStackArpRelation::Follow) {
            const int primaryRoot = params_.arpPattern
                    != ProcessorStackArpPattern::Off
                ? arpeggiators_[0u].currentNote : latestHeldNote();
            if (primaryRoot >= 0) configurePartnerVoicing(
                primaryRoot, retrigger);
            return;
        }
        const bool arpeggiating =
            params_.arpPatternB != ProcessorStackArpPattern::Off;
        int rootNote = arpeggiating
            ? arpeggiators_[1u].currentNote : latestHeldNote();
        if (!arpeggiating
            && params_.arpBRelation == ProcessorStackArpRelation::Counter
            && rootNote >= 0) {
            rootNote = partnerRootFor(rootNote, retrigger);
        }
        configureIndependentPartnerVoicing(rootNote, retrigger,
            arpeggiating);
    }

    void rebuildVoicing(bool retrigger)
    {
        rebuildPrimaryVoicing(retrigger);
        rebuildPartnerVoicing(retrigger);
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
            1.0f, velocity, true, true, 0u);
        const uint32_t selectedIndex = static_cast<uint32_t>(
            selected - lanes_.data());
        if (params_.pairAmount > 1.0e-4f
            && (params_.arpBRelation == ProcessorStackArpRelation::Follow
                || params_.arpPatternB == ProcessorStackArpPattern::Off)) {
            const int partnerRoot = params_.arpBRelation
                    == ProcessorStackArpRelation::Free
                ? note : partnerRootFor(note, true);
            configureLane(partnerLanes_[selectedIndex], partnerRoot,
                0.0f, 1.0f, 1.0f, velocity * 0.94f,
                true, true, 1u);
            partnerRootNote_ = partnerRoot;
        }
        lastRootNote_ = note;
    }

    float processArticulationEnvelope(ExciterLane& lane)
    {
        if (lane.held != lane.envelopeGate) {
            lane.envelopeGate = lane.held;
            lane.envelopeStage = lane.held
                ? EnvelopeStage::Attack : EnvelopeStage::Release;
        }
        const auto timeCoefficient = [&](float milliseconds) {
            if (milliseconds <= 1.0e-4f) return 1.0f;
            // Five time constants place the perceptual endpoint at the
            // labelled ADSR time while keeping every transition continuous.
            return onePoleSeconds(milliseconds * 0.0002f);
        };
        switch (lane.envelopeStage) {
        case EnvelopeStage::Idle:
            lane.articulationEnvelope = 0.0f;
            break;
        case EnvelopeStage::Attack:
            lane.articulationEnvelope +=
                (1.0f - lane.articulationEnvelope)
                * timeCoefficient(smoothed_.attackMs);
            if (lane.articulationEnvelope >= 0.999f) {
                lane.articulationEnvelope = 1.0f;
                lane.envelopeStage = EnvelopeStage::Decay;
            }
            break;
        case EnvelopeStage::Decay:
            lane.articulationEnvelope +=
                (smoothed_.sustain - lane.articulationEnvelope)
                * timeCoefficient(smoothed_.decayMs);
            if (std::abs(lane.articulationEnvelope - smoothed_.sustain)
                    <= 5.0e-4f) {
                lane.articulationEnvelope = smoothed_.sustain;
                lane.envelopeStage = EnvelopeStage::Sustain;
            }
            break;
        case EnvelopeStage::Sustain:
            lane.articulationEnvelope +=
                (smoothed_.sustain - lane.articulationEnvelope)
                * onePoleSeconds(0.012f);
            break;
        case EnvelopeStage::Release:
            lane.articulationEnvelope +=
                (0.0f - lane.articulationEnvelope)
                * timeCoefficient(smoothed_.releaseMs);
            if (lane.articulationEnvelope <= 1.0e-5f) {
                lane.articulationEnvelope = 0.0f;
                lane.envelopeStage = EnvelopeStage::Idle;
            }
            break;
        }
        lane.articulationEnvelope = flushDenormal(clamp(
            lane.articulationEnvelope, 0.0f, 1.0f));
        return lane.articulationEnvelope;
    }

    float processExciter(ExciterLane& lane, float& rootWitness,
        const MaterialProfile& material)
    {
        if (lane.startDelaySamples > 0u) {
            --lane.startDelaySamples;
            rootWitness = 0.0f;
            return 0.0f;
        }
        if (lane.samplesSinceAttack < 0xffffffffu) {
            ++lane.samplesSinceAttack;
        }
        const float articulation = processArticulationEnvelope(lane);
        lane.targetFrequency = noteFrequency(
            static_cast<float>(lane.sourceNote) + lane.noteOffset
                + lane.detuneSemitones);
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
        const bool upperChordTone = smoothed_.mode == ProcessorStackMode::Power
            && std::abs(lane.noteOffset) > 0.5f;

        // Preserve the established response through three quarters of the
        // control, then compress only the rough endpoint. PICK still becomes
        // harder, but its last quarter no longer opens every scrape, partial,
        // comb and pickup-velocity path all the way at once.
        const float pickTimbre = smoothed_.pick <= 0.75f
            ? smoothed_.pick
            : 0.75f + (smoothed_.pick - 0.75f) * 0.55f;
        const float pickCurve = pickTimbre * pickTimbre;
        const float pickMilliseconds = lerp(12.0f, 1.15f, pickCurve);
        lane.pickEnvelope *= std::exp(-1.0f / std::max(1.0f,
            static_cast<float>(sampleRate_) * pickMilliseconds * 0.001f));
        lane.pickEnvelope = flushDenormal(lane.pickEnvelope);
        const float pickSlewTime = lane.pickEnvelope > lane.pickSmoothed
            ? lerp(0.0045f, 0.00055f, pickCurve)
            : lerp(0.0012f, 0.00020f, pickCurve);
        lane.pickSmoothed += (lane.pickEnvelope - lane.pickSmoothed)
            * onePoleSeconds(pickSlewTime);
        lane.pickSmoothed = flushDenormal(lane.pickSmoothed);
        const float noise = nextNoise(lane.random);
        lane.pickNoiseLow += (noise - lane.pickNoiseLow)
            * onePoleHz(lerp(280.0f, 7000.0f, pickCurve));
        lane.pickNoiseBody += (noise - lane.pickNoiseBody)
            * onePoleHz(lerp(120.0f, 900.0f, pickCurve));
        const float pickNoiseBand = lane.pickNoiseLow - lane.pickNoiseBody;
        const float noiseAmount = std::pow(pickTimbre, 1.5f) * 0.28f
            * (upperChordTone ? 0.14f : 1.0f);
        const float rawPickPacket = lane.pickSmoothed * lane.pickDirection
            * (pickNoiseBand * noiseAmount
                + fundamental * lerp(0.08f, 0.38f, pickTimbre)
                + partialTwo * pickTimbre * 0.62f)
            * (0.28f + lane.velocity * 0.82f) * lane.attackGain;
        const float pickPacketCutoff = upperChordTone
            ? lerp(120.0f, 1400.0f, pickCurve)
            : lerp(150.0f, 7600.0f,
                std::pow(pickTimbre, 1.35f));
        lane.pickPacketLow += (rawPickPacket - lane.pickPacketLow)
            * onePoleHz(pickPacketCutoff);
        lane.pickPacketLow = flushDenormal(lane.pickPacketLow);
        const float pickPacket = lane.pickPacketLow;

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
        const float pickupPosition = clamp(lerp(0.11f, 0.31f,
                1.0f - pickTimbre) + material.pickupShift,
            0.06f, 0.38f);
        const float pickupTap = readDelay(lane.delay, lane.writeIndex,
            clamp(delaySamples * pickupPosition, 1.0f, maximumDelay));
        const float wireCutoff = clamp(lerp(720.0f, 7600.0f,
                1.0f - smoothed_.damping) * std::exp2(material.brightness),
            320.0f, static_cast<float>(sampleRate_ * 0.36));
        const float dispersion = 0.04f + smoothed_.wire * 0.18f
            + material.dispersion
            + clamp(lane.currentFrequency / 4200.0f, 0.0f, 0.12f);
        const float dispersed = -dispersion * delayed
            + lane.dispersionInput + dispersion * lane.dispersionOutput;
        lane.dispersionInput = delayed;
        lane.dispersionOutput = flushDenormal(dispersed);
        lane.wireLow += (lane.dispersionOutput - lane.wireLow)
            * onePoleHz(wireCutoff);
        const float wireDecay = clamp(0.985f
                + (1.0f - smoothed_.damping) * 0.0138f
                + material.sustain,
            0.960f, 0.99945f);
        const float injection = pickPacket
            * (0.54f + smoothed_.wire * 0.54f);
        lane.delay[lane.writeIndex] = flushDenormal(std::tanh(
            lane.wireLow * wireDecay + injection));
        lane.writeIndex = (lane.writeIndex + 1u) % lane.delay.size();

        const float comb = delayed - pickupTap;
        const float displacement = delayed * 0.74f
            + comb * (0.34f + pickTimbre * 0.34f);
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
            + lane.pickupVelocity * (0.12f + pickTimbre * 0.14f);
        lane.pickupLow += (pickup - lane.pickupLow)
            * onePoleHz(lerp(3800.0f, 8200.0f, pickTimbre));
        lane.rootLow += (lane.pickupLow - lane.rootLow)
            * onePoleHz(clamp(lane.currentFrequency * 1.45f,
                45.0f, 1800.0f));
        const float stringRelease = 0.22f + lane.amplitude * 0.78f;
        const float transientTonal = (fundamental
                + partialTwo * pickTimbre
                + partialThree * pickCurve)
            * lane.pickSmoothed * lane.attackGain
            * lerp(0.02f, 0.085f, pickTimbre)
            * (upperChordTone ? 0.14f : 1.0f);
        const float output = lerp(pickPacket + transientTonal,
            lane.pickupLow * stringRelease + pickPacket * 0.18f,
            smoothed_.wire);
        lane.materialLow += (output - lane.materialLow)
            * onePoleHz(clamp(material.bodyFrequency * 1.8f,
                90.0f, 2200.0f));
        lane.materialBodyLow += (output - lane.materialBodyLow)
            * onePoleHz(clamp(material.bodyFrequency * 0.62f,
                45.0f, 900.0f));
        lane.materialLow = flushDenormal(lane.materialLow);
        lane.materialBodyLow = flushDenormal(lane.materialBodyLow);
        const float bodyBand = lane.materialLow - lane.materialBodyLow;
        const float neckEdge = output - lane.materialLow;
        const float metallicPartial = std::sin(lane.phase * 2.031f)
            * material.metallic * lane.amplitude * articulation * 0.075f;
        const float materialOutput = output
            + bodyBand * material.bodyAmount * 1.35f
            + neckEdge * material.brightness * 0.34f
            + metallicPartial;
        lane.outputSmoothed += (materialOutput - lane.outputSmoothed)
            * onePoleHz(9800.0f);
        lane.outputSmoothed = flushDenormal(lane.outputSmoothed);
        rootWitness = lane.rootLow * lane.amplitude
            * articulation * 0.42f;
        if (!lane.held && lane.pickEnvelope < 1.0e-6f
            && lane.pickSmoothed < 1.0e-6f
            && std::abs(lane.pickPacketLow) < 1.0e-6f
            && articulation < 1.0e-6f
            && lane.amplitude < 1.0e-6f
            && std::abs(lane.wireLow) < 1.0e-6f
            && std::abs(lane.pickupLow) < 1.0e-6f
            && std::abs(lane.outputSmoothed) < 1.0e-6f) {
            lane.active = false;
            lane.gain = 0.0f;
        }
        return lane.outputSmoothed * articulation * 0.34f;
    }

    float processSelectedCircuit(RigState& rig, const RigControls& controls,
        ProcessorStackCircuit circuit, float input, float rateScale)
    {
        const uint32_t index = std::min<uint32_t>(
            static_cast<uint32_t>(circuit), kProcessorStackCircuitCount - 1u);
        const float drive = controls.bite;
        const float bias = (controls.bias - 0.5f) * 0.18f;
        if (circuit == ProcessorStackCircuit::Shred) {
            const float pressured = std::tanh(input * (1.0f + drive * 9.0f));
            return lerp(pressured,
                fold(pressured * (1.0f + drive * 5.5f)),
                drive * drive * 0.72f);
        }
        const float driven = processAnalogDriveCircuit(
            static_cast<AnalogDriveCircuit>(index - 1u),
            rig.pedalStates[index], input, drive, controls.pedalTone,
            bias, static_cast<float>(sampleRate_) * rateScale);
        const float idle = processAnalogDriveCircuit(
            static_cast<AnalogDriveCircuit>(index - 1u),
            rig.pedalIdleStates[index], 0.0f, drive, controls.pedalTone,
            bias, static_cast<float>(sampleRate_) * rateScale);
        return driven - idle;
    }

    float processPedal(RigState& rig, const RigControls& controls, float input)
    {
        if (controls.circuit != rig.activeCircuit) {
            rig.previousCircuit = rig.activeCircuit;
            rig.activeCircuit = controls.circuit;
            rig.circuitFade = 0.0f;
        }
        const auto render = [&](ProcessorStackCircuit circuit, float sample) {
            return processSelectedCircuit(rig, controls,
                circuit, sample, 2.0f);
        };
        const float midpoint = 0.5f * (rig.preampPreviousInput + input);
        float active = 0.5f * (render(rig.activeCircuit, midpoint)
            + render(rig.activeCircuit, input));
        if (rig.circuitFade < 1.0f) {
            const float previous = 0.5f * (render(rig.previousCircuit, midpoint)
                + render(rig.previousCircuit, input));
            active = lerp(previous, active, rig.circuitFade);
            rig.circuitFade = std::min(1.0f,
                rig.circuitFade + circuitFadeCoefficient_);
        }
        rig.preampPreviousInput = input;
        const float starvation = lerp(0.62f, 1.0f, controls.bias);
        const float raw = softClip(active * starvation, 2.10f, 2.5f);
        const float dcBlocked = raw - rig.pedalDcInput
            + pedalDcPole_ * rig.pedalDcOutput;
        rig.pedalDcInput = raw;
        rig.pedalDcOutput = flushDenormal(dcBlocked);
        return rig.pedalDcOutput;
    }

    float processAmplifier(RigState& rig, const RigControls& controls,
        float input)
    {
        const auto oversampledStage = [&](float sample) {
            const float stack = controls.stack;
            const float focus = controls.focus;
            const float bias = 0.018f + stack * 0.042f;
            float first = asymmetric(sample,
                1.0f + stack * stack * 13.0f, bias);
            rig.preampMemory += (first - rig.preampMemory)
                * onePoleHz(lerp(2600.0f, 10500.0f, focus), 2.0f);
            float second = asymmetric(rig.preampMemory,
                1.0f + stack * 6.4f, -bias * 0.64f);

            rig.toneLow += (second - rig.toneLow)
                * onePoleHz(lerp(110.0f, 240.0f, focus), 2.0f);
            rig.toneMidLow += (second - rig.toneMidLow)
                * onePoleHz(lerp(520.0f, 1700.0f, focus), 2.0f);
            rig.toneHighLow += (second - rig.toneHighLow)
                * onePoleHz(lerp(2600.0f, 7200.0f, focus), 2.0f);
            const float low = rig.toneLow;
            const float middle = rig.toneMidLow - rig.toneLow;
            const float high = rig.toneHighLow - rig.toneMidLow;
            const float voiced = low * lerp(1.18f, 0.76f, focus)
                + middle * lerp(0.72f, 1.72f, focus)
                + high * lerp(0.48f, 1.34f, focus);
            rig.sagEnvelope += (std::abs(voiced) - rig.sagEnvelope)
                * (std::abs(voiced) > rig.sagEnvelope
                    ? sagAttackCoefficient_ : sagReleaseCoefficient_);
            const float rail = 1.0f / (1.0f + rig.sagEnvelope
                * controls.sag * 3.4f);
            const float positive = std::tanh(voiced
                * (1.2f + stack * 5.6f) * rail);
            const float negative = std::tanh(voiced
                * (1.0f + stack * 4.7f) * rail);
            // Crossfade the asymmetric halves through zero. Selecting a side
            // with a sign branch keeps value continuity but creates a slope
            // corner that the cabinet can magnify into brittle fizz.
            const float polarityBlend = 0.5f
                + std::tanh(voiced * 42.0f) * 0.5f;
            const float crossover = lerp(negative, positive, polarityBlend);
            rig.transformerLow += (crossover - rig.transformerLow)
                * onePoleHz(82.0f, 2.0f);
            return crossover + rig.transformerLow * 0.12f;
        };
        const float midpoint = 0.5f * (rig.amplifierPreviousInput + input);
        const float first = oversampledStage(midpoint);
        const float second = oversampledStage(input);
        rig.amplifierPreviousInput = input;
        return flushDenormal(0.5f * (first + second));
    }

    void processSpeaker(RigState& rig, const RigControls& controls, float input,
        float& micA, float& micB)
    {
        const float cone = controls.cone;
        const float cabinet = controls.cabinet;
        rig.coilEnvelope += (std::abs(input) - rig.coilEnvelope)
            * (std::abs(input) > rig.coilEnvelope
                ? coilAttackCoefficient_ : coilReleaseCoefficient_);
        const float compression = 1.0f
            / (1.0f + rig.coilEnvelope * cone * 1.9f);
        const float displacementShift = 1.0f
            + smoothLimit(rig.speakerModes[0u].first, 1.0f)
                * cone * 0.035f;

        // Govern the source of overload, rather than only filtering its
        // result. The previous microphone detector is combined with smooth
        // input, coil and loop stress, then smoothed before it modulates any
        // resonator coefficient.
        const float inputStress = smoothStep(0.72f, 1.10f,
            std::abs(input));
        const float coilStress = smoothStep(0.58f, 1.06f,
            rig.coilEnvelope);
        const float loopStress = smoothStep(0.42f, 1.08f,
            rig.loopEnvelope);
        const float intrinsicStress = std::max(inputStress * 0.36f,
            std::max(coilStress * 0.46f,
                loopStress * controls.feedback * 0.92f));
        const float detectedProtection = smoothStep(0.0f, 0.40f,
            rig.overloadMaskAmount);
        const float protectionTarget = std::max(linkedRigStress_ * 0.68f,
            std::max(detectedProtection, intrinsicStress
                * lerp(0.52f, 1.0f, controls.overloadMask)));
        rig.speakerProtection += (protectionTarget - rig.speakerProtection)
            * (protectionTarget > rig.speakerProtection
                ? speakerProtectionAttackCoefficient_
                : speakerProtectionReleaseCoefficient_);
        rig.speakerProtection = flushDenormal(rig.speakerProtection);
        rig.speakerProtectionPeak = std::max(rig.speakerProtectionPeak,
            rig.speakerProtection);
        const std::array<float, kSpeakerModeCount> baseFrequencies {{
            lerp(78.0f, 132.0f, 1.0f - cabinet),
            lerp(310.0f, 560.0f, 1.0f - cabinet),
            lerp(760.0f, 1480.0f, controls.focus),
            lerp(2200.0f, 4300.0f, controls.focus),
        }};
        const std::array<float, kSpeakerModeCount> radii {{
            0.992f, 0.976f, 0.958f, 0.925f,
        }};
        const std::array<float, kSpeakerModeCount> gains {{
            0.24f, 0.20f, 0.15f, 0.09f,
        }};
        std::array<float, kSpeakerModeCount> modes {};
        const float protectionSquared = rig.speakerProtection
            * rig.speakerProtection;
        const float protectionCurve = protectionSquared * protectionSquared;
        const float dampingExponent = 1.0f + protectionCurve
            * (1.30f + cone * 1.10f);
        const float modeDrive = lerp(1.0f, 0.54f, protectionCurve);
        constexpr float kSpeakerStateLimit = 3.0f;
        for (uint32_t index = 0u; index < rig.speakerModes.size(); ++index) {
            const float frequency = clamp(baseFrequencies[index]
                    * (index == 0u ? displacementShift : 1.0f),
                18.0f, static_cast<float>(sampleRate_ * 0.42));
            const float radius = std::pow(radii[index],
                48000.0f / static_cast<float>(sampleRate_)
                    * dampingExponent);
            const float coefficient = 2.0f * radius
                * std::cos(2.0f * kPi * frequency
                    / static_cast<float>(sampleRate_));
            const float resonant = input * (1.0f - radius) * gains[index]
                    * modeDrive
                + coefficient * rig.speakerModes[index].first
                - radius * radius * rig.speakerModes[index].second;
            rig.speakerModes[index].second = rig.speakerModes[index].first;
            rig.speakerModePreLimitPeak = std::max(
                rig.speakerModePreLimitPeak,
                std::abs(resonant));
            if (std::abs(resonant) > kSpeakerStateLimit * 0.84f)
                ++rig.speakerSoftLimitCount;
            rig.speakerModes[index].first = flushDenormal(
                softClip(resonant, kSpeakerStateLimit * 0.84f,
                    kSpeakerStateLimit));
            modes[index] = rig.speakerModes[index].first;
        }
        const float modal = modes[0u] * 1.24f + modes[1u] * 1.08f
            + modes[2u] * 0.92f + modes[3u] * 0.72f;
        rig.speakerDc += (modal - rig.speakerDc) * speakerDcCoefficient_;
        const float displacement = modal - rig.speakerDc;
        const float breakupGovernor = lerp(1.0f, 0.56f,
            protectionCurve);
        const float breakup = std::tanh((input - modes[0u] * 0.34f)
            * (1.0f + cone * cone * 8.0f) * breakupGovernor)
            * cone * 0.24f;
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
        // One linked continuous bound preserves the two microphone images and
        // replaces the previous independent hard clamps. The room and loop
        // paths both receive this same bounded signal.
        constexpr float kMicrophoneLimit = 3.0f;
        const float micPeak = std::max(std::abs(micA), std::abs(micB));
        if (micPeak > kMicrophoneLimit * 0.84f) ++rig.micSoftLimitCount;
        const float boundedMicPeak = softCeiling(micPeak,
            kMicrophoneLimit * 0.84f, kMicrophoneLimit);
        const float microphoneGain = micPeak > 1.0e-9f
            ? boundedMicPeak / micPeak : 1.0f;
        micA *= microphoneGain;
        micB *= microphoneGain;
    }

    void processOverloadMask(RigState& rig, const RigControls& controls,
        float& micA, float& micB)
    {
        const float rawLeft = micA;
        const float rawRight = micB;
        const float mono = (rawLeft + rawRight) * 0.5f;
        const float magnitude = std::max(std::abs(rawLeft),
            std::abs(rawRight));
        const float roughness = std::abs(mono - rig.overloadPreviousMic);
        rig.overloadPreviousMic = mono;
        rig.overloadLevel += (magnitude - rig.overloadLevel)
            * (magnitude > rig.overloadLevel
                ? overloadLevelAttackCoefficient_
                : overloadLevelReleaseCoefficient_);
        rig.overloadRoughness += (roughness - rig.overloadRoughness)
            * (roughness > rig.overloadRoughness
                ? overloadRoughnessAttackCoefficient_
                : overloadRoughnessReleaseCoefficient_);
        rig.overloadLevel = flushDenormal(rig.overloadLevel);
        rig.overloadRoughness = flushDenormal(rig.overloadRoughness);

        const float normalizedRoughness = rig.overloadRoughness
            / std::max(0.035f, rig.overloadLevel);
        const float levelStress = clamp((rig.overloadLevel - 0.58f)
                / 0.62f,
            0.0f, 1.0f);
        const float loopStress = clamp((rig.loopEnvelope - 0.44f)
                / 0.62f,
            0.0f, 1.0f);
        const float coilStress = clamp((rig.coilEnvelope - 0.64f)
                / 0.76f,
            0.0f, 1.0f);
        const float roughStress = clamp((normalizedRoughness - 0.34f)
                / 0.86f,
            0.0f, 1.0f);
        const float sustainedStress = std::max(loopStress,
            levelStress * 0.72f + coilStress * 0.28f);
        const float localMaskTarget = controls.overloadMask
            * sustainedStress * (0.28f + roughStress * 0.72f);
        const float maskTarget = std::max(localMaskTarget,
            linkedRigStress_ * controls.overloadMask * 0.72f);
        rig.overloadMaskAmount += (maskTarget - rig.overloadMaskAmount)
            * (maskTarget > rig.overloadMaskAmount
                ? overloadMaskAttackCoefficient_
                : overloadMaskReleaseCoefficient_);
        rig.overloadMaskAmount = flushDenormal(clamp(
            rig.overloadMaskAmount, 0.0f, 1.0f));

        const float maskCutoff = lerp(1550.0f, 2900.0f,
            controls.focus);
        const float maskLowCoefficient = onePoleHz(maskCutoff);
        rig.overloadLowNear += (rawLeft - rig.overloadLowNear)
            * maskLowCoefficient;
        rig.overloadLowFar += (rawRight - rig.overloadLowFar)
            * maskLowCoefficient;
        rig.overloadLowNear = flushDenormal(rig.overloadLowNear);
        rig.overloadLowFar = flushDenormal(rig.overloadLowFar);
        const float fullMaskStrength = smoothStep(0.90f, 1.0f,
            controls.overloadMask);
        const float spectralMask = rig.overloadMaskAmount
            * (0.78f + fullMaskStrength * 0.10f);
        const float energyMask = lerp(1.0f, 0.72f,
            rig.overloadMaskAmount);
        micA = lerp(rawLeft, rig.overloadLowNear * 0.86f, spectralMask)
            * energyMask;
        micB = lerp(rawRight, rig.overloadLowFar * 0.86f, spectralMask)
            * energyMask;
    }

    float readGlitchHistory(const RigState& rig, float position) const
    {
        if (rig.glitchHistory.size() < 4u) return 0.0f;
        const float size = static_cast<float>(rig.glitchHistory.size());
        while (position < 0.0f) position += size;
        while (position >= size) position -= size;
        const auto first = static_cast<size_t>(position);
        const auto second = (first + 1u) % rig.glitchHistory.size();
        const float fraction = position - static_cast<float>(first);
        return rig.glitchHistory[first]
            + (rig.glitchHistory[second] - rig.glitchHistory[first])
                * fraction;
    }

    void beginTargetGlitch(RigState& rig, const RigControls& controls,
        float targetFrequency)
    {
        if (rig.glitchHistory.size() < 32u) return;
        const float periodSamples = static_cast<float>(sampleRate_)
            / std::max(80.0f, targetFrequency);
        const float capturedCycles = lerp(6.0f, 1.25f,
            controls.glitchRatchet);
        const uint32_t maximumCell = static_cast<uint32_t>(std::min(
            static_cast<float>(rig.glitchHistory.size() - 4u),
            static_cast<float>(sampleRate_) * 0.018f));
        rig.glitchCellSamples = std::clamp(
            static_cast<uint32_t>(std::lround(periodSamples * capturedCycles)),
            12u, std::max(12u, maximumCell));
        rig.glitchCaptureStart = (rig.glitchWriteIndex
            + rig.glitchHistory.size() - rig.glitchCellSamples)
            % rig.glitchHistory.size();
        rig.glitchRepeatCount = std::clamp(2u + static_cast<uint32_t>(
            std::lround(controls.glitchRatchet * 5.0f
                + controls.targetGlitch * 2.0f)), 2u, 9u);
        rig.glitchRepeatIndex = 0u;
        rig.glitchPhase = 0.0f;
        rig.glitchPlaying = true;
        rig.glitchArmed = false;
        ++rig.glitchTriggerCount;
    }

    float processTargetGlitch(RigState& rig, const RigControls& controls)
    {
        if (!rig.glitchPlaying || rig.glitchCellSamples < 2u) return 0.0f;
        const float amount = controls.targetGlitch;
        const bool reverse = amount > 0.76f
            && (rig.glitchRepeatIndex % 4u) == 3u;
        const float phase = clamp(rig.glitchPhase, 0.0f,
            static_cast<float>(rig.glitchCellSamples - 1u));
        const float local = reverse
            ? static_cast<float>(rig.glitchCellSamples - 1u) - phase : phase;
        const float readPosition = static_cast<float>(rig.glitchCaptureStart)
            + local;
        const float normalized = phase
            / static_cast<float>(std::max(1u, rig.glitchCellSamples - 1u));
        const float window = 0.5f - 0.5f
            * std::cos(2.0f * kPi * normalized);
        const float result = readGlitchHistory(rig, readPosition) * window;

        float playbackRate = 1.0f;
        if (amount > 0.42f && (rig.glitchRepeatIndex % 3u) == 1u) {
            playbackRate = lerp(1.0f, 2.0f,
                (amount - 0.42f) / 0.58f);
        }
        rig.glitchPhase += playbackRate;
        if (rig.glitchPhase >= static_cast<float>(rig.glitchCellSamples)) {
            rig.glitchPhase = 0.0f;
            if (++rig.glitchRepeatIndex >= rig.glitchRepeatCount) {
                rig.glitchPlaying = false;
            }
        }
        return flushDenormal(result);
    }

    float readFeedbackReturn(RigState& rig, const RigControls& controls,
        int activeRoot)
    {
        const float rootFrequency = noteFrequency(
            static_cast<float>(activeRoot >= 0 ? activeRoot : lastRootNote_));
        const float bodyHarmonic = clamp(lerp(1.5f, 6.0f,
                controls.harmonic) + crookedHarmonicSkew_ * 0.35f,
            1.0f, 7.0f);
        const float stabHarmonic = clamp(std::round(lerp(5.0f, 24.0f,
                controls.harmonic) + crookedHarmonicSkew_ * 1.6f),
            3.0f, 28.0f);
        const float bodyNoteDelay = static_cast<float>(sampleRate_)
            / (rootFrequency * bodyHarmonic);
        const float stabFrequency = clamp(rootFrequency * stabHarmonic,
            620.0f, static_cast<float>(sampleRate_ * 0.18));
        const float stabNoteDelay = static_cast<float>(sampleRate_)
            / stabFrequency;
        const float roomDelay = static_cast<float>(sampleRate_)
            * lerp(0.0080f, 0.00055f, controls.proximity);
        const float target = clamp(lerp(roomDelay, bodyNoteDelay,
                controls.tracking),
            2.0f, static_cast<float>(rig.loopDelay.size() - 2u));
        const float stabRoomDelay = static_cast<float>(sampleRate_)
            * lerp(0.0016f, 0.00035f, controls.proximity);
        const float stabTarget = clamp(lerp(stabRoomDelay, stabNoteDelay,
                0.55f + controls.tracking * 0.45f),
            2.0f, static_cast<float>(rig.loopDelay.size() - 2u));
        rig.loopDelaySamples += (target - rig.loopDelaySamples)
            * loopDelaySmoothingCoefficient_;
        rig.stabDelaySamples += (stabTarget - rig.stabDelaySamples)
            * stabDelaySmoothingCoefficient_;
        const float delayed = readDelay(rig.loopDelay, rig.loopWriteIndex,
            rig.loopDelaySamples);
        const float stabDelayed = readDelay(rig.loopDelay,
            rig.loopWriteIndex, rig.stabDelaySamples);
        const float dc = delayed - rig.loopDcInput
            + loopDcPole_ * rig.loopDcOutput;
        rig.loopDcInput = delayed;
        rig.loopDcOutput = flushDenormal(dc);
        const float stabDc = stabDelayed - rig.stabDcInput
            + loopDcPole_ * rig.stabDcOutput;
        rig.stabDcInput = stabDelayed;
        rig.stabDcOutput = flushDenormal(stabDc);
        rig.loopLow += (rig.loopDcOutput - rig.loopLow)
            * onePoleHz(lerp(1400.0f, 6200.0f, controls.proximity)
                * lerp(1.0f, 0.64f, controls.pierce));
        rig.loopHighLow += (rig.loopLow - rig.loopHighLow)
            * onePoleHz(lerp(105.0f, 430.0f, controls.chaos));
        const float bodyBand = rig.loopLow - rig.loopHighLow;

        const float svfFrequency = clamp(stabFrequency,
            80.0f, static_cast<float>(sampleRate_ * 0.18));
        const float svfG = std::tan(kPi * svfFrequency
            / static_cast<float>(sampleRate_));
        const float svfDamping = lerp(1.05f, 0.20f, controls.pierce);
        const float svfA1 = 1.0f
            / (1.0f + svfG * (svfG + svfDamping));
        const float svfV3 = rig.stabDcOutput - rig.stabLow;
        const float stabBandSignal = svfA1
            * (rig.stabBand + svfG * svfV3);
        const float stabLowSignal = rig.stabLow + svfG * stabBandSignal;
        rig.stabBand = flushDenormal(smoothLimit(
            2.0f * stabBandSignal - rig.stabBand, 3.0f));
        rig.stabLow = flushDenormal(smoothLimit(
            2.0f * stabLowSignal - rig.stabLow, 3.0f));

        if (!rig.glitchPlaying && !rig.glitchHistory.empty()) {
            rig.glitchHistory[rig.glitchWriteIndex] = stabBandSignal;
            rig.glitchWriteIndex = (rig.glitchWriteIndex + 1u)
                % rig.glitchHistory.size();
            if (rig.glitchArmed) ++rig.glitchArmAgeSamples;
        }

        const auto followEnergy = [&](float signal, float& envelope) {
            const float coefficient = std::abs(signal) > envelope
                ? feedbackEnergyAttackCoefficient_
                : feedbackEnergyReleaseCoefficient_;
            envelope += (std::abs(signal) - envelope) * coefficient;
            envelope = flushDenormal(envelope);
        };
        followEnergy(bodyBand, rig.bodyEnvelope);
        followEnergy(stabBandSignal, rig.stabEnvelope);
        float spectralShare = rig.stabEnvelope
            / std::max(1.0e-5f, rig.bodyEnvelope + rig.stabEnvelope);
        const float desiredShare = 0.22f + controls.pierce * 0.48f;
        const uint32_t minimumCaptureAge = static_cast<uint32_t>(
            sampleRate_ * 0.0025);
        const bool coherentUpper = spectralShare
            > 0.12f + controls.pierce * 0.08f;
        const bool captureFallback = rig.glitchArmAgeSamples
                > static_cast<uint32_t>(sampleRate_ * 0.030)
            && rig.stabEnvelope > 8.0e-4f;
        if (rig.glitchArmed && !rig.glitchPlaying
            && controls.targetGlitch > 1.0e-4f
            && rig.glitchArmAgeSamples >= minimumCaptureAge
            && rig.stabEnvelope > 1.5e-3f
            && (coherentUpper || captureFallback)) {
            beginTargetGlitch(rig, controls, stabFrequency);
        }

        const float glitchSample = processTargetGlitch(rig, controls);
        const float glitchExcess = std::max(0.0f,
            rig.glitchEnvelope - 0.24f);
        const float glitchGovernor = 1.0f / (1.0f + glitchExcess * 14.0f);
        const float glitchGain = controls.targetGlitch
            * (0.30f + controls.pierce * 1.08f) * glitchGovernor
            * lerp(1.0f, 0.08f, rig.overloadMaskAmount);
        const float stabReturn = smoothLimit(stabBandSignal
                * (1.0f - controls.targetGlitch * 0.14f)
                + glitchSample * glitchGain,
            2.2f);
        followEnergy(glitchSample, rig.glitchEnvelope);
        followEnergy(stabReturn, rig.stabEnvelope);
        rig.glitchActivity += (std::abs(glitchSample * glitchGain)
                - rig.glitchActivity)
            * (std::abs(glitchSample * glitchGain) > rig.glitchActivity
                ? feedbackEnergyAttackCoefficient_
                : feedbackEnergyReleaseCoefficient_);
        rig.glitchActivity = flushDenormal(rig.glitchActivity);
        spectralShare = rig.stabEnvelope
            / std::max(1.0e-5f, rig.bodyEnvelope + rig.stabEnvelope);
        const float focusTarget = clamp((desiredShare - spectralShare)
                * controls.selfListen * 2.4f,
            0.0f, 1.0f);
        const float focusCoefficient = focusTarget > rig.selfFocus
            ? selfFocusAttackCoefficient_ : selfFocusReleaseCoefficient_;
        rig.selfFocus += (focusTarget - rig.selfFocus) * focusCoefficient;
        rig.selfFocus = flushDenormal(rig.selfFocus);

        const float bodyExcess = std::max(0.0f, rig.bodyEnvelope - 0.34f);
        const float stabExcess = std::max(0.0f, rig.stabEnvelope - 0.30f);
        const float bodyGovernor = 1.0f / (1.0f + bodyExcess * 15.0f);
        const float stabGovernor = 1.0f / (1.0f + stabExcess * 11.0f);
        const float bodyGain = lerp(0.92f, 0.42f,
                controls.pierce * controls.selfListen)
            * lerp(1.0f, 0.48f, rig.selfFocus) * bodyGovernor
            * lerp(1.0f, 0.62f, rig.overloadMaskAmount);
        const float stabGain = (0.12f + controls.pierce * 1.22f)
            * (1.0f + rig.selfFocus * 1.15f) * stabGovernor
            * lerp(1.0f, 0.16f, rig.overloadMaskAmount);
        const float governedReturn = smoothLimit(bodyBand * bodyGain
                + stabReturn * stabGain,
            2.5f);
        rig.loopEnvelope += (std::abs(governedReturn) - rig.loopEnvelope)
            * loopEnvelopeCoefficient_;
        rig.loopActivity += (std::max(std::abs(bodyBand),
                std::abs(stabReturn)) - rig.loopActivity)
            * loopActivityCoefficient_;
        rig.loopEnvelope = flushDenormal(rig.loopEnvelope);
        rig.loopActivity = flushDenormal(rig.loopActivity);
        return governedReturn;
    }

    bool anyLaneActive() const
    {
        for (const auto& lane : lanes_) {
            if (lane.active) return true;
        }
        for (const auto& lane : partnerLanes_) {
            if (lane.active) return true;
        }
        return false;
    }

    void clearSignalState()
    {
        const auto clearLanes = [](auto& guitarLanes) {
            for (auto& lane : guitarLanes) {
                std::fill(lane.delay.begin(), lane.delay.end(), 0.0f);
                lane.active = false;
                lane.held = false;
                lane.wireLow = 0.0f;
                lane.pickNoiseLow = 0.0f;
                lane.pickNoiseBody = 0.0f;
                lane.pickupPrevious = 0.0f;
                lane.pickupVelocity = 0.0f;
                lane.pickupLow = 0.0f;
                lane.rootLow = 0.0f;
                lane.outputSmoothed = 0.0f;
                lane.materialLow = 0.0f;
                lane.materialBodyLow = 0.0f;
                lane.pickSmoothed = 0.0f;
                lane.pickPacketLow = 0.0f;
                lane.dispersionInput = 0.0f;
                lane.dispersionOutput = 0.0f;
                lane.startDelaySamples = 0u;
            }
        };
        clearLanes(lanes_);
        clearLanes(partnerLanes_);
        for (uint32_t index = 0u; index < rigs_.size(); ++index) {
            clearRigSignalState(rigs_[index], index);
        }
        stereoSeparationActivity_ = 0.0f;
        linkedRigStress_ = 0.0f;
        partnerRootNote_ = -1;
        partnerPrimaryRoot_ = -1;
        outputDcLeft_ = outputDcRight_ = outputPeak_ = 0.0f;
        limiterGain_ = 1.0f;
        limiterWasAttacking_ = false;
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
        smooth(smoothed_.attackMs, params_.attackMs);
        smooth(smoothed_.decayMs, params_.decayMs);
        smooth(smoothed_.sustain, params_.sustain);
        smooth(smoothed_.releaseMs, params_.releaseMs);
        smooth(smoothed_.pairAmount, params_.pairAmount);
        smooth(smoothed_.pairLoose, params_.pairLoose);
        smooth(smoothed_.pairSpread, params_.pairSpread);
        smooth(smoothed_.arpGate, params_.arpGate);
        smooth(smoothed_.arpGateB, params_.arpGateB);
        smooth(smoothed_.arpPhaseB, params_.arpPhaseB);
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
        smooth(smoothed_.targetGlitch, params_.targetGlitch);
        smooth(smoothed_.glitchRatchet, params_.glitchRatchet);
        smooth(smoothed_.overloadMask, params_.overloadMask);
        smooth(smoothed_.biteB, params_.biteB);
        smooth(smoothed_.pedalToneB, params_.pedalToneB);
        smooth(smoothed_.biasB, params_.biasB);
        smooth(smoothed_.stackB, params_.stackB);
        smooth(smoothed_.sagB, params_.sagB);
        smooth(smoothed_.focusB, params_.focusB);
        smooth(smoothed_.coneB, params_.coneB);
        smooth(smoothed_.cabinetB, params_.cabinetB);
        smooth(smoothed_.micB, params_.micB);
        smooth(smoothed_.feedbackB, params_.feedbackB);
        smooth(smoothed_.proximityB, params_.proximityB);
        smooth(smoothed_.harmonicB, params_.harmonicB);
        smooth(smoothed_.trackingB, params_.trackingB);
        smooth(smoothed_.polarityB, params_.polarityB);
        smooth(smoothed_.rootB, params_.rootB);
        smooth(smoothed_.chaosB, params_.chaosB);
        smooth(smoothed_.pierceB, params_.pierceB);
        smooth(smoothed_.selfListenB, params_.selfListenB);
        smooth(smoothed_.targetGlitchB, params_.targetGlitchB);
        smooth(smoothed_.glitchRatchetB, params_.glitchRatchetB);
        smooth(smoothed_.overloadMaskB, params_.overloadMaskB);
        smooth(smoothed_.outputGainDb, params_.outputGainDb);
        smoothed_.mode = params_.mode;
        smoothed_.arpPattern = params_.arpPattern;
        smoothed_.scale = params_.scale;
        smoothed_.arpRate = params_.arpRate;
        smoothed_.arpOctaves = params_.arpOctaves;
        smoothed_.arpBRelation = params_.arpBRelation;
        smoothed_.arpPatternB = params_.arpPatternB;
        smoothed_.scaleB = params_.scaleB;
        smoothed_.arpRateB = params_.arpRateB;
        smoothed_.arpOctavesB = params_.arpOctavesB;
        smoothed_.customPatternLengthB = params_.customPatternLengthB;
        smoothed_.customPatternB = params_.customPatternB;
        smoothed_.circuit = params_.circuit;
        smoothed_.linkPedal = params_.linkPedal;
        smoothed_.linkAmplifier = params_.linkAmplifier;
        smoothed_.linkFeedback = params_.linkFeedback;
        smoothed_.circuitB = params_.circuitB;
        smoothed_.pairRelation = params_.pairRelation;
        smoothed_.neckA = params_.neckA;
        smoothed_.bodyA = params_.bodyA;
        smoothed_.neckB = params_.neckB;
        smoothed_.bodyB = params_.bodyB;
    }

    void updateCoefficients()
    {
        parameterSmoothingCoefficient_ = onePoleSeconds(0.015f);
        voicingGainCoefficient_ = onePoleSeconds(0.012f);
        chordBloomCoefficient_ = onePoleSeconds(0.045f);
        attackGainCoefficient_ = onePoleSeconds(0.0035f);
        gateAttackCoefficient_ = onePoleSeconds(0.004f);
        loopDelaySmoothingCoefficient_ = onePoleSeconds(0.035f);
        stabDelaySmoothingCoefficient_ = onePoleSeconds(0.014f);
        loopEnvelopeCoefficient_ = onePoleSeconds(0.012f);
        loopActivityCoefficient_ = onePoleSeconds(0.075f);
        outputGainCoefficient_ = onePoleSeconds(0.015f);
        limiterAttackCoefficient_ = onePoleSeconds(0.00075f);
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
        overloadLevelAttackCoefficient_ = onePoleSeconds(0.006f);
        overloadLevelReleaseCoefficient_ = onePoleSeconds(0.110f);
        overloadRoughnessAttackCoefficient_ = onePoleSeconds(0.003f);
        overloadRoughnessReleaseCoefficient_ = onePoleSeconds(0.055f);
        overloadMaskAttackCoefficient_ = onePoleSeconds(0.009f);
        overloadMaskReleaseCoefficient_ = onePoleSeconds(0.260f);
        speakerProtectionAttackCoefficient_ = onePoleSeconds(0.006f);
        speakerProtectionReleaseCoefficient_ = onePoleSeconds(0.120f);
    }

    double sampleRate_ = 48000.0;
    ProcessorStackParams params_ {};
    ProcessorStackParams smoothed_ {};
    std::array<ExciterLane, kExciterCount> lanes_ {};
    std::array<ExciterLane, kExciterCount> partnerLanes_ {};
    std::array<bool, 128u> heldNotes_ {};
    std::array<float, 128u> heldVelocities_ {};
    std::array<int16_t, 128u> noteOrder_ {};
    uint32_t noteOrderSize_ = 0u;
    uint64_t ageCounter_ = 1u;
    int lastPlayedNote_ = -1;
    int lastRootNote_ = 45;
    int partnerRootNote_ = -1;
    int partnerPrimaryRoot_ = -1;

    std::array<RigState, 2u> rigs_ {};
    float stereoSeparationActivity_ = 0.0f;
    float linkedRigStress_ = 0.0f;
    float keyGate_ = 0.0f;
    float crookedHarmonicSkew_ = 0.0f;
    float speakerChoke_ = 1.0f;
    float speakerChokeTarget_ = 1.0f;
    float pressure_ = 0.0f;
    float pitchBendSemitones_ = 0.0f;
    float tempoBpm_ = 120.0f;
    double hostTransportBeat_ = 0.0;
    bool hostTransportActive_ = false;
    bool scorePlaybackActive_ = false;
    std::array<bool, 2u> scorePlayerHeld_ {};
    std::array<std::array<int8_t, kScoreStringCount>, 2u>
        scoreStringLanes_ {};
    std::array<ArpState, 2u> arpeggiators_ {};

    float outputDcLeft_ = 0.0f;
    float outputDcRight_ = 0.0f;
    float limiterGain_ = 1.0f;
    bool limiterWasAttacking_ = false;
    uint64_t limiterAttackEventCount_ = 0u;
    float maximumLimiterGainStep_ = 0.0f;
    float outputGainSmoothed_ = 0.25f;
    float outputPeak_ = 0.0f;
    bool signalActive_ = false;
    uint32_t randomState_ = 0x8f31d26bu;

    float parameterSmoothingCoefficient_ = 0.001f;
    float voicingGainCoefficient_ = 0.001f;
    float chordBloomCoefficient_ = 0.001f;
    float attackGainCoefficient_ = 0.001f;
    float gateAttackCoefficient_ = 0.001f;
    float loopDelaySmoothingCoefficient_ = 0.001f;
    float stabDelaySmoothingCoefficient_ = 0.001f;
    float loopEnvelopeCoefficient_ = 0.001f;
    float loopActivityCoefficient_ = 0.001f;
    float outputGainCoefficient_ = 0.001f;
    float limiterAttackCoefficient_ = 0.001f;
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
    float overloadLevelAttackCoefficient_ = 0.001f;
    float overloadLevelReleaseCoefficient_ = 0.001f;
    float overloadRoughnessAttackCoefficient_ = 0.001f;
    float overloadRoughnessReleaseCoefficient_ = 0.001f;
    float overloadMaskAttackCoefficient_ = 0.001f;
    float overloadMaskReleaseCoefficient_ = 0.001f;
    float speakerProtectionAttackCoefficient_ = 0.001f;
    float speakerProtectionReleaseCoefficient_ = 0.001f;
};

} // namespace s3g
