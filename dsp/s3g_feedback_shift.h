#pragma once

#include "s3g_analog_drive_circuits.h"
#include "s3g_break_bus.h"
#include "s3g_drum_echo.h"
#include "s3g_drum_overload.h"
#include "s3g_macro_delay.h"
#include "s3g_macro_fracture.h"
#include "s3g_macro_pitch.h"
#include "s3g_macro_shred.h"
#include "s3g_mc_to_quad.h"
#include "s3g_mc_to_stereo.h"
#include "s3g_modulation_circuits.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

namespace s3g {

constexpr uint32_t kFeedbackShiftChannels = 8u;
constexpr uint32_t kFeedbackShiftMatrixCells =
    kFeedbackShiftChannels * kFeedbackShiftChannels;
constexpr uint32_t kFeedbackPedalExtraParameterCount = 8u;

enum class FeedbackShiftOutputMode : uint32_t {
    Direct8 = 0u,
    QuadRing,
    StereoRing,
    Count,
};

constexpr uint32_t kFeedbackShiftOutputModeCount =
    static_cast<uint32_t>(FeedbackShiftOutputMode::Count);

inline const char* feedbackShiftOutputModeName(FeedbackShiftOutputMode mode)
{
    switch (mode) {
    case FeedbackShiftOutputMode::Direct8: return "8CH DIRECT";
    case FeedbackShiftOutputMode::QuadRing: return "QUAD RING";
    case FeedbackShiftOutputMode::StereoRing: return "STEREO RING";
    case FeedbackShiftOutputMode::Count: break;
    }
    return "8CH DIRECT";
}

enum class FeedbackShiftMode : uint32_t {
    Frequency = 0u,
    Ring,
};

enum class FeedbackExciterSource : uint32_t {
    NoiseHit = 0u,
    Noise,
    Hit,
    Tone,
    SubBass,
    External,
    ExternalHit,
    Off,
    Count,
};

constexpr uint32_t kFeedbackExciterSourceCount =
    static_cast<uint32_t>(FeedbackExciterSource::Count);

inline const char* feedbackExciterSourceName(FeedbackExciterSource source)
{
    switch (source) {
    case FeedbackExciterSource::NoiseHit: return "NOISE + HIT";
    case FeedbackExciterSource::Noise: return "NOISE";
    case FeedbackExciterSource::Hit: return "MIDI HIT";
    case FeedbackExciterSource::Tone: return "TONE";
    case FeedbackExciterSource::SubBass: return "SUB BASS";
    case FeedbackExciterSource::External: return "EXTERNAL";
    case FeedbackExciterSource::ExternalHit: return "EXT GATE";
    case FeedbackExciterSource::Off: return "OFF";
    case FeedbackExciterSource::Count: break;
    }
    return "NOISE + HIT";
}

enum class FeedbackMorphSource : uint32_t {
    Manual = 0u,
    Envelope,
    Edge,
    Lfo,
    Chaos,
    Pulse,
    Count,
};

constexpr uint32_t kFeedbackMorphSourceCount =
    static_cast<uint32_t>(FeedbackMorphSource::Count);

inline const char* feedbackMorphSourceName(FeedbackMorphSource source)
{
    switch (source) {
    case FeedbackMorphSource::Manual: return "MANUAL";
    case FeedbackMorphSource::Envelope: return "ECOLOGY ENV";
    case FeedbackMorphSource::Edge: return "ECOLOGY EDGE";
    case FeedbackMorphSource::Lfo: return "LFO";
    case FeedbackMorphSource::Chaos: return "CHAOS";
    case FeedbackMorphSource::Pulse: return "PULSE";
    case FeedbackMorphSource::Count: break;
    }
    return "MANUAL";
}

enum class FeedbackPedalType : uint32_t {
    Bypass = 0u,
    Filter,
    Resonator,
    Phase,
    Transient,
    BreakBus,
    DrumBus,
    Degrade,
    Erosion,
    Relay,
    Wool,
    Rat,
    Diode,
    Fold,
    Repeater,
    TimeMangler,
    DrumEcho,
    MacroShred,
    MacroPitch,
    MacroDelay,
    Rotor,
    Chorus,
    Fracture,
    Count,
};

constexpr uint32_t kFeedbackPedalTypeCount =
    static_cast<uint32_t>(FeedbackPedalType::Count);

inline const char* feedbackPedalName(FeedbackPedalType type)
{
    switch (type) {
    case FeedbackPedalType::Bypass: return "BYPASS";
    case FeedbackPedalType::Filter: return "FILTER";
    case FeedbackPedalType::Resonator: return "RESONATOR";
    case FeedbackPedalType::Phase: return "PHASE";
    case FeedbackPedalType::Transient: return "TRANSIENT";
    case FeedbackPedalType::BreakBus: return "BREAK BUS";
    case FeedbackPedalType::DrumBus: return "DRUM BUS";
    case FeedbackPedalType::Degrade: return "DEGRADE";
    case FeedbackPedalType::Erosion: return "EROSION";
    case FeedbackPedalType::Relay: return "RELAY";
    case FeedbackPedalType::Wool: return "WOOL";
    case FeedbackPedalType::Rat: return "RAT";
    case FeedbackPedalType::Diode: return "DIODE";
    case FeedbackPedalType::Fold: return "FOLD";
    case FeedbackPedalType::Repeater: return "REPEATER";
    case FeedbackPedalType::TimeMangler: return "TIME";
    case FeedbackPedalType::DrumEcho: return "DRUM ECHO";
    case FeedbackPedalType::MacroShred: return "MACRO SHRED";
    case FeedbackPedalType::MacroPitch: return "MACRO PITCH";
    case FeedbackPedalType::MacroDelay: return "MACRO DELAY";
    case FeedbackPedalType::Rotor: return "ROTOR";
    case FeedbackPedalType::Chorus: return "CHORUS";
    case FeedbackPedalType::Fracture: return "FRACTURE";
    case FeedbackPedalType::Count: break;
    }
    return "BYPASS";
}

enum class FeedbackInsertCategory : uint32_t {
    Core = 0u,
    Spectral,
    Dynamics,
    Degrade,
    Drive,
    Shred,
    Pitch,
    Time,
    Modulation,
    Fracture,
    Count,
};

constexpr uint32_t kFeedbackInsertCategoryCount =
    static_cast<uint32_t>(FeedbackInsertCategory::Count);

inline const char* feedbackInsertCategoryName(FeedbackInsertCategory category)
{
    switch (category) {
    case FeedbackInsertCategory::Core: return "CORE";
    case FeedbackInsertCategory::Spectral: return "SPECTRAL";
    case FeedbackInsertCategory::Dynamics: return "DYNAMICS";
    case FeedbackInsertCategory::Degrade: return "DEGRADE";
    case FeedbackInsertCategory::Drive: return "DRIVE";
    case FeedbackInsertCategory::Shred: return "SHRED";
    case FeedbackInsertCategory::Pitch: return "PITCH";
    case FeedbackInsertCategory::Time: return "TIME";
    case FeedbackInsertCategory::Modulation: return "MODULATION";
    case FeedbackInsertCategory::Fracture: return "FRACTURE";
    case FeedbackInsertCategory::Count: break;
    }
    return "CORE";
}

inline FeedbackInsertCategory feedbackInsertCategory(
    FeedbackPedalType type)
{
    switch (type) {
    case FeedbackPedalType::Bypass: return FeedbackInsertCategory::Core;
    case FeedbackPedalType::Filter:
    case FeedbackPedalType::Resonator:
    case FeedbackPedalType::Phase: return FeedbackInsertCategory::Spectral;
    case FeedbackPedalType::Transient:
    case FeedbackPedalType::BreakBus:
    case FeedbackPedalType::DrumBus: return FeedbackInsertCategory::Dynamics;
    case FeedbackPedalType::Degrade:
    case FeedbackPedalType::Erosion:
    case FeedbackPedalType::Relay: return FeedbackInsertCategory::Degrade;
    case FeedbackPedalType::Wool:
    case FeedbackPedalType::Rat:
    case FeedbackPedalType::Diode:
    case FeedbackPedalType::Fold: return FeedbackInsertCategory::Drive;
    case FeedbackPedalType::MacroShred: return FeedbackInsertCategory::Shred;
    case FeedbackPedalType::MacroPitch: return FeedbackInsertCategory::Pitch;
    case FeedbackPedalType::Repeater:
    case FeedbackPedalType::TimeMangler:
    case FeedbackPedalType::DrumEcho:
    case FeedbackPedalType::MacroDelay: return FeedbackInsertCategory::Time;
    case FeedbackPedalType::Rotor:
    case FeedbackPedalType::Chorus:
        return FeedbackInsertCategory::Modulation;
    case FeedbackPedalType::Fracture: return FeedbackInsertCategory::Fracture;
    case FeedbackPedalType::Count: break;
    }
    return FeedbackInsertCategory::Core;
}

inline const char* feedbackPedalCategoryName(FeedbackPedalType type)
{
    return feedbackInsertCategoryName(feedbackInsertCategory(type));
}

inline uint32_t feedbackInsertEffectCount(FeedbackInsertCategory category)
{
    switch (category) {
    case FeedbackInsertCategory::Core: return 1u;
    case FeedbackInsertCategory::Spectral: return 3u;
    case FeedbackInsertCategory::Dynamics: return 3u;
    case FeedbackInsertCategory::Degrade: return 3u;
    case FeedbackInsertCategory::Drive: return 4u;
    case FeedbackInsertCategory::Shred: return kMacroShredCircuitCount;
    case FeedbackInsertCategory::Pitch: return 1u;
    case FeedbackInsertCategory::Time: return 4u;
    case FeedbackInsertCategory::Modulation: return 2u;
    case FeedbackInsertCategory::Fracture: return kFractureProcessorCount;
    case FeedbackInsertCategory::Count: break;
    }
    return 1u;
}

inline FeedbackPedalType feedbackInsertEffectPedal(
    FeedbackInsertCategory category, uint32_t effect)
{
    switch (category) {
    case FeedbackInsertCategory::Core: return FeedbackPedalType::Bypass;
    case FeedbackInsertCategory::Spectral: {
        static constexpr FeedbackPedalType effects[3u] {
            FeedbackPedalType::Filter, FeedbackPedalType::Resonator,
            FeedbackPedalType::Phase };
        return effects[std::min(effect, 2u)];
    }
    case FeedbackInsertCategory::Dynamics: {
        static constexpr FeedbackPedalType effects[3u] {
            FeedbackPedalType::Transient, FeedbackPedalType::BreakBus,
            FeedbackPedalType::DrumBus };
        return effects[std::min(effect, 2u)];
    }
    case FeedbackInsertCategory::Degrade: {
        static constexpr FeedbackPedalType effects[3u] {
            FeedbackPedalType::Degrade, FeedbackPedalType::Erosion,
            FeedbackPedalType::Relay };
        return effects[std::min(effect, 2u)];
    }
    case FeedbackInsertCategory::Drive: {
        static constexpr FeedbackPedalType effects[4u] {
            FeedbackPedalType::Wool, FeedbackPedalType::Rat,
            FeedbackPedalType::Diode, FeedbackPedalType::Fold };
        return effects[std::min(effect, 3u)];
    }
    case FeedbackInsertCategory::Shred: return FeedbackPedalType::MacroShred;
    case FeedbackInsertCategory::Pitch: return FeedbackPedalType::MacroPitch;
    case FeedbackInsertCategory::Time: {
        static constexpr FeedbackPedalType effects[4u] {
            FeedbackPedalType::Repeater, FeedbackPedalType::TimeMangler,
            FeedbackPedalType::DrumEcho, FeedbackPedalType::MacroDelay };
        return effects[std::min(effect, 3u)];
    }
    case FeedbackInsertCategory::Modulation: {
        static constexpr FeedbackPedalType effects[2u] {
            FeedbackPedalType::Rotor, FeedbackPedalType::Chorus };
        return effects[std::min(effect, 1u)];
    }
    case FeedbackInsertCategory::Fracture: return FeedbackPedalType::Fracture;
    case FeedbackInsertCategory::Count: break;
    }
    return FeedbackPedalType::Bypass;
}

inline const char* feedbackInsertEffectName(FeedbackInsertCategory category,
    uint32_t effect)
{
    if (category == FeedbackInsertCategory::Fracture) {
        return fractureProcessorName(static_cast<FractureProcessor>(
            std::min(effect, kFractureProcessorCount - 1u)));
    }
    if (category == FeedbackInsertCategory::Shred) {
        return macroShredCircuitName(static_cast<MacroShredCircuit>(
            std::min(effect, kMacroShredCircuitCount - 1u)));
    }
    return feedbackPedalName(feedbackInsertEffectPedal(category, effect));
}

inline uint32_t feedbackInsertEffectIndex(FeedbackPedalType pedal,
    uint32_t fractureProcessor = 0u, uint32_t shredCircuit = 0u)
{
    const auto category = feedbackInsertCategory(pedal);
    if (category == FeedbackInsertCategory::Fracture) {
        return std::min(fractureProcessor, kFractureProcessorCount - 1u);
    }
    if (category == FeedbackInsertCategory::Shred) {
        return std::min(shredCircuit, kMacroShredCircuitCount - 1u);
    }
    const uint32_t count = feedbackInsertEffectCount(category);
    for (uint32_t effect = 0u; effect < count; ++effect) {
        if (feedbackInsertEffectPedal(category, effect) == pedal) return effect;
    }
    return 0u;
}

inline const char* feedbackPedalControlName(
    FeedbackPedalType type, uint32_t control)
{
    static constexpr const char* generic[4u] {
        "AMOUNT", "TONE", "BIAS", "MIX",
    };
    if (control >= 4u) return "";
    switch (type) {
    case FeedbackPedalType::Filter: {
        static constexpr const char* names[4u] {
            "RESONANCE", "CUTOFF", "MODE", "MIX" };
        return names[control];
    }
    case FeedbackPedalType::Degrade: {
        static constexpr const char* names[4u] {
            "RATE", "BITS", "JITTER", "MIX" };
        return names[control];
    }
    case FeedbackPedalType::Transient: {
        static constexpr const char* names[4u] {
            "ATTACK", "SUSTAIN", "GATE", "MIX" };
        return names[control];
    }
    case FeedbackPedalType::Resonator: {
        static constexpr const char* names[4u] {
            "DECAY", "TUNE", "DAMP", "MIX" };
        return names[control];
    }
    case FeedbackPedalType::Erosion: {
        static constexpr const char* names[4u] {
            "DEPTH", "RATE", "FEEDBACK", "MIX" };
        return names[control];
    }
    case FeedbackPedalType::Repeater: {
        static constexpr const char* names[4u] {
            "WINDOW", "REPEATS", "DECAY", "DIR" };
        return names[control];
    }
    case FeedbackPedalType::TimeMangler: {
        static constexpr const char* names[4u] {
            "WINDOW", "PITCH", "BEHAVIOR", "MODE" };
        return names[control];
    }
    case FeedbackPedalType::DrumEcho: {
        static constexpr const char* names[4u] {
            "FEEDBACK", "TIME", "TONE", "MIX" };
        return names[control];
    }
    case FeedbackPedalType::BreakBus: {
        static constexpr const char* names[4u] {
            "PRESS", "RECOVERY", "BITE", "MIX" };
        return names[control];
    }
    case FeedbackPedalType::DrumBus: {
        static constexpr const char* names[4u] {
            "DRIVE", "WEIGHT", "TONE", "MIX" };
        return names[control];
    }
    case FeedbackPedalType::Fold: {
        static constexpr const char* names[4u] {
            "FOLDS", "SHAPE", "BIAS", "MIX" };
        return names[control];
    }
    case FeedbackPedalType::Relay: {
        static constexpr const char* names[4u] {
            "THRESHOLD", "HOLD", "HYSTERESIS", "MIX" };
        return names[control];
    }
    case FeedbackPedalType::Phase: {
        static constexpr const char* names[4u] {
            "DEPTH", "RATE", "FEEDBACK", "MIX" };
        return names[control];
    }
    case FeedbackPedalType::Fracture: {
        static constexpr const char* names[4u] {
            "PROCESSOR", "DEPTH", "COLOR", "BIAS" };
        return names[control];
    }
    case FeedbackPedalType::MacroShred: {
        static constexpr const char* names[4u] {
            "PRESSURE", "SHRED", "FEEDBACK", "COLOR" };
        return names[control];
    }
    case FeedbackPedalType::MacroPitch: {
        static constexpr const char* names[4u] {
            "PITCH", "FINE", "WINDOW", "GLIDE" };
        return names[control];
    }
    case FeedbackPedalType::MacroDelay: {
        static constexpr const char* names[4u] {
            "TIME", "FEEDBACK", "TONE", "CHARACTER" };
        return names[control];
    }
    case FeedbackPedalType::Rotor: {
        static constexpr const char* names[4u] {
            "AMOUNT", "RATE", "SHAPE", "MIX" };
        return names[control];
    }
    case FeedbackPedalType::Chorus: {
        static constexpr const char* names[4u] {
            "DEPTH", "RATE", "FEEDBACK", "MIX" };
        return names[control];
    }
    default: return generic[control];
    }
}

enum class FeedbackPedalControlDisplay : uint32_t {
    Percent = 0u,
    Bipolar,
    HertzLog,
    MillisecondsLog,
    MillisecondsSpacing,
    Count,
    Semitones,
    Decibels,
    Menu,
    Ratio,
};

struct FeedbackPedalControlInfo {
    const char* label = "";
    uint32_t storageSlot = 0u;
    FeedbackPedalControlDisplay display = FeedbackPedalControlDisplay::Percent;
    float displayMinimum = 0.0f;
    float displayMaximum = 100.0f;
    uint32_t menuCount = 0u;
};

inline uint32_t feedbackPedalControlCount(FeedbackPedalType type)
{
    switch (type) {
    case FeedbackPedalType::Bypass: return 0u;
    case FeedbackPedalType::Filter: return 5u;
    case FeedbackPedalType::Erosion: return 5u;
    case FeedbackPedalType::Repeater: return 9u;
    case FeedbackPedalType::TimeMangler: return 9u;
    case FeedbackPedalType::DrumEcho: return 11u;
    case FeedbackPedalType::BreakBus: return 9u;
    case FeedbackPedalType::DrumBus: return 10u;
    case FeedbackPedalType::Phase: return 5u;
    case FeedbackPedalType::Fracture: return 7u;
    case FeedbackPedalType::MacroShred: return 8u;
    case FeedbackPedalType::MacroPitch: return 5u;
    case FeedbackPedalType::MacroDelay: return 7u;
    case FeedbackPedalType::Rotor:
    case FeedbackPedalType::Chorus: return 4u;
    case FeedbackPedalType::Degrade:
    case FeedbackPedalType::Transient:
    case FeedbackPedalType::Resonator:
    case FeedbackPedalType::Wool:
    case FeedbackPedalType::Rat:
    case FeedbackPedalType::Diode:
    case FeedbackPedalType::Fold:
    case FeedbackPedalType::Relay:
        return 4u;
    case FeedbackPedalType::Count: break;
    }
    return 0u;
}

inline FeedbackPedalControlInfo feedbackPedalControlInfo(
    FeedbackPedalType type, uint32_t control)
{
    using Display = FeedbackPedalControlDisplay;
    const auto info = [&](const char* label, uint32_t slot, Display display,
                          float minimum = 0.0f, float maximum = 100.0f,
                          uint32_t menuCount = 0u) {
        return FeedbackPedalControlInfo {
            label, slot, display, minimum, maximum, menuCount };
    };
    switch (type) {
    case FeedbackPedalType::Filter:
        switch (control) {
        case 0u: return info("RES", 0u, Display::Percent);
        case 1u: return info("CUTOFF", 1u, Display::HertzLog, 30.0f, 20000.0f);
        case 2u: return info("MODE", 2u, Display::Menu, 0.0f, 1.0f, 4u);
        case 3u: return info("DRIVE", 4u, Display::Percent);
        case 4u: return info("MIX", 3u, Display::Percent);
        }
        break;
    case FeedbackPedalType::Degrade:
        switch (control) {
        case 0u: return info("RATE", 0u, Display::Count, 1.0f, 96.0f);
        case 1u: return info("BITS", 1u, Display::Count, 4.0f, 16.0f);
        case 2u: return info("JITTER", 2u, Display::Bipolar, -100.0f, 100.0f);
        case 3u: return info("MIX", 3u, Display::Percent);
        }
        break;
    case FeedbackPedalType::Transient:
        switch (control) {
        case 0u: return info("ATTACK", 0u, Display::Bipolar, -100.0f, 100.0f);
        case 1u: return info("SUSTAIN", 1u, Display::Bipolar, -100.0f, 100.0f);
        case 2u: return info("GATE", 2u, Display::Bipolar, -100.0f, 100.0f);
        case 3u: return info("MIX", 3u, Display::Percent);
        }
        break;
    case FeedbackPedalType::Resonator:
        switch (control) {
        case 0u: return info("DECAY", 0u, Display::Percent);
        case 1u: return info("TUNE", 1u, Display::HertzLog, 40.0f, 4000.0f);
        case 2u: return info("DAMP", 2u, Display::Bipolar, -100.0f, 100.0f);
        case 3u: return info("MIX", 3u, Display::Percent);
        }
        break;
    case FeedbackPedalType::Erosion:
        switch (control) {
        case 0u: return info("DEPTH", 0u, Display::Percent);
        case 1u: return info("RATE", 1u, Display::HertzLog, 10.0f, 16000.0f);
        case 2u: return info("FDBK", 2u, Display::Bipolar, -100.0f, 100.0f);
        case 3u: return info("SHAPE", 4u, Display::Menu, 0.0f, 1.0f, 2u);
        case 4u: return info("MIX", 3u, Display::Percent);
        }
        break;
    case FeedbackPedalType::Repeater:
        switch (control) {
        case 0u: return info("WINDOW", 0u, Display::Menu, 0.0f, 1.0f, 8u);
        case 1u: return info("REPEATS", 1u, Display::Count, 1.0f, 16.0f);
        case 2u: return info("DECAY", 2u, Display::Bipolar, -100.0f, 100.0f);
        case 3u: return info("DIR", 4u, Display::Menu, 0.0f, 1.0f, 3u);
        case 4u: return info("SENSE", 5u, Display::Percent);
        case 5u: return info("XFADE", 6u, Display::MillisecondsLog,
            0.15f, 12.0f);
        case 6u: return info("DRIFT", 7u, Display::Semitones, -3.0f, 3.0f);
        case 7u: return info("LINK", 8u, Display::Bipolar, -100.0f, 100.0f);
        case 8u: return info("MIX", 3u, Display::Percent);
        }
        break;
    case FeedbackPedalType::TimeMangler:
        switch (control) {
        case 0u: return info("WINDOW", 0u, Display::Menu, 0.0f, 1.0f, 8u);
        case 1u: return info("PITCH", 1u, Display::Semitones, -24.0f, 24.0f);
        case 2u: return info("BEHAVIOR", 2u, Display::Bipolar, -100.0f, 100.0f);
        case 3u: return info("MODE", 4u, Display::Menu, 0.0f, 1.0f, 3u);
        case 4u: return info("SENSE", 5u, Display::Percent);
        case 5u: return info("XFADE", 6u, Display::MillisecondsLog,
            0.15f, 12.0f);
        case 6u: return info("DRIFT", 7u, Display::Semitones, -3.0f, 3.0f);
        case 7u: return info("LINK", 8u, Display::Bipolar, -100.0f, 100.0f);
        case 8u: return info("MIX", 3u, Display::Percent);
        }
        break;
    case FeedbackPedalType::DrumEcho:
        switch (control) {
        case 0u: return info("HEADS", 4u, Display::Menu, 0.0f, 1.0f, 7u);
        case 1u: return info("CLOCK", 5u, Display::Menu, 0.0f, 1.0f, 10u);
        case 2u: return info("TIME", 1u, Display::MillisecondsLog, 20.0f, 1800.0f);
        case 3u: return info("FDBK", 0u, Display::Percent);
        case 4u: return info("WEAR", 6u, Display::Percent);
        case 5u: return info("FLUTTER", 7u, Display::Percent);
        case 6u: return info("TRANS", 8u, Display::Bipolar, -100.0f, 100.0f);
        case 7u: return info("SENSE", 9u, Display::Percent);
        case 8u: return info("DUCK", 10u, Display::Percent);
        case 9u: return info("TONE", 2u, Display::Bipolar, -100.0f, 100.0f);
        case 10u: return info("MIX", 3u, Display::Percent);
        }
        break;
    case FeedbackPedalType::BreakBus:
        switch (control) {
        case 0u: return info("PRESS", 0u, Display::Percent);
        case 1u: return info("SNAP", 4u, Display::Bipolar, -100.0f, 100.0f);
        case 2u: return info("RECOVERY", 1u, Display::Percent);
        case 3u: return info("SAT", 5u, Display::Percent);
        case 4u: return info("BITE", 2u, Display::Percent);
        case 5u: return info("CLIP", 6u, Display::Percent);
        case 6u: return info("TILT", 7u, Display::Bipolar, -100.0f, 100.0f);
        case 7u: return info("FIELD SAFE", 8u, Display::Menu, 0.0f, 1.0f, 2u);
        case 8u: return info("MIX", 3u, Display::Percent);
        }
        break;
    case FeedbackPedalType::DrumBus:
        switch (control) {
        case 0u: return info("CIRCUIT", 4u, Display::Menu, 0.0f, 1.0f, 8u);
        case 1u: return info("IN", 0u, Display::Decibels, 0.0f, 12.0f);
        case 2u: return info("OVERLOAD", 5u, Display::Percent);
        case 3u: return info("DENSITY", 6u, Display::Percent);
        case 4u: return info("PUNCH", 7u, Display::Bipolar, -100.0f, 100.0f);
        case 5u: return info("BIAS", 8u, Display::Bipolar, -100.0f, 100.0f);
        case 6u: return info("BREAKUP", 9u, Display::Percent);
        case 7u: return info("WEIGHT", 1u, Display::Percent);
        case 8u: return info("TONE", 2u, Display::Bipolar, -100.0f, 100.0f);
        case 9u: return info("MIX", 3u, Display::Percent);
        }
        break;
    case FeedbackPedalType::Wool:
    case FeedbackPedalType::Rat:
    case FeedbackPedalType::Diode:
        switch (control) {
        case 0u: return info("DRIVE", 0u, Display::Percent);
        case 1u: return info("TONE", 1u, Display::Percent);
        case 2u: return info("BIAS", 2u, Display::Bipolar, -100.0f, 100.0f);
        case 3u: return info("MIX", 3u, Display::Percent);
        }
        break;
    case FeedbackPedalType::Fold:
        switch (control) {
        case 0u: return info("FOLDS", 0u, Display::Percent);
        case 1u: return info("SHAPE", 1u, Display::Percent);
        case 2u: return info("BIAS", 2u, Display::Bipolar, -100.0f, 100.0f);
        case 3u: return info("MIX", 3u, Display::Percent);
        }
        break;
    case FeedbackPedalType::Relay:
        switch (control) {
        case 0u: return info("THRESH", 0u, Display::Percent);
        case 1u: return info("SLEW", 1u, Display::Percent);
        case 2u: return info("HYST", 2u, Display::Bipolar, -100.0f, 100.0f);
        case 3u: return info("MIX", 3u, Display::Percent);
        }
        break;
    case FeedbackPedalType::Phase:
        switch (control) {
        case 0u: return info("DEPTH", 0u, Display::Percent);
        case 1u: return info("CENTER", 1u, Display::HertzLog, 60.0f, 7200.0f);
        case 2u: return info("BIAS", 2u, Display::Bipolar, -100.0f, 100.0f);
        case 3u: return info("CROSS", 4u, Display::Bipolar, -100.0f, 100.0f);
        case 4u: return info("MIX", 3u, Display::Percent);
        }
        break;
    case FeedbackPedalType::Fracture:
        switch (control) {
        case 0u: return info("PROCESSOR", 6u, Display::Menu,
            0.0f, 1.0f, kFractureProcessorCount);
        case 1u: return info("DEPTH", 0u, Display::Percent);
        case 2u: return info("COLOR", 1u, Display::Percent);
        case 3u: return info("BIAS", 2u, Display::Bipolar,
            -100.0f, 100.0f);
        case 4u: return info("REACT", 4u, Display::Percent);
        case 5u: return info("MEMORY", 5u, Display::Percent);
        case 6u: return info("MIX", 3u, Display::Percent);
        }
        break;
    case FeedbackPedalType::MacroShred:
        switch (control) {
        case 0u: return info("PRESSURE", 0u, Display::Percent);
        case 1u: return info("SHRED", 1u, Display::Percent);
        case 2u: return info("FEEDBACK", 4u, Display::Percent);
        case 3u: return info("COLOR", 5u, Display::Percent);
        case 4u: return info("REACT", 6u, Display::Percent);
        case 5u: return info("TUNE", 7u, Display::Percent);
        case 6u: return info("BODY", 8u, Display::Percent);
        case 7u: return info("MIX", 3u, Display::Percent);
        }
        break;
    case FeedbackPedalType::MacroPitch:
        switch (control) {
        case 0u: return info("PITCH", 2u, Display::Semitones,
            -24.0f, 24.0f);
        case 1u: return info("FINE", 4u, Display::Bipolar,
            -100.0f, 100.0f);
        case 2u: return info("WINDOW", 0u, Display::MillisecondsLog,
            20.0f, 180.0f);
        case 3u: return info("GLIDE", 1u, Display::MillisecondsLog,
            10.0f, 2000.0f);
        case 4u: return info("MIX", 3u, Display::Percent);
        }
        break;
    case FeedbackPedalType::MacroDelay:
        switch (control) {
        case 0u: return info("TIME", 0u, Display::MillisecondsLog,
            5.0f, 2000.0f);
        case 1u: return info("FEEDBACK", 1u, Display::Percent);
        case 2u: return info("TONE", 4u, Display::Percent);
        case 3u: return info("CHARACTER", 5u, Display::Percent);
        case 4u: return info("SMEAR", 6u, Display::Percent);
        case 5u: return info("GLIDE", 7u, Display::MillisecondsLog,
            10.0f, 2000.0f);
        case 6u: return info("MIX", 3u, Display::Percent);
        }
        break;
    case FeedbackPedalType::Rotor:
        switch (control) {
        case 0u: return info("AMOUNT", 0u, Display::Percent);
        case 1u: return info("RATE", 1u, Display::HertzLog,
            0.08f, 45.0f);
        case 2u: return info("SHAPE", 2u, Display::Bipolar,
            -100.0f, 100.0f);
        case 3u: return info("MIX", 3u, Display::Percent);
        }
        break;
    case FeedbackPedalType::Chorus:
        switch (control) {
        case 0u: return info("DEPTH", 0u, Display::Percent);
        case 1u: return info("RATE", 1u, Display::HertzLog,
            0.05f, 6.0f);
        case 2u: return info("FEEDBACK", 2u, Display::Bipolar,
            -38.0f, 38.0f);
        case 3u: return info("MIX", 3u, Display::Percent);
        }
        break;
    case FeedbackPedalType::Bypass:
    case FeedbackPedalType::Count:
        break;
    }
    return {};
}

enum class FeedbackGrainShape : uint32_t {
    Tukey = 0u,
    Hann,
    Triangle,
    Decay,
    Gate,
    Count,
};

constexpr uint32_t kFeedbackGrainShapeCount =
    static_cast<uint32_t>(FeedbackGrainShape::Count);

inline const char* feedbackGrainShapeName(FeedbackGrainShape shape)
{
    switch (shape) {
    case FeedbackGrainShape::Tukey: return "TUKEY";
    case FeedbackGrainShape::Hann: return "HANN";
    case FeedbackGrainShape::Triangle: return "TRIANGLE";
    case FeedbackGrainShape::Decay: return "DECAY";
    case FeedbackGrainShape::Gate: return "GATE";
    case FeedbackGrainShape::Count: break;
    }
    return "TUKEY";
}

constexpr uint32_t kFeedbackAuxControlCount = 16u;

// The AUX uses the same display vocabulary as the node inserts, but its
// controls are global. Grain shape is the one discrete selector; all other
// controls can be swept continuously while the ecology is sounding.
inline FeedbackPedalControlInfo feedbackAuxControlInfo(uint32_t control)
{
    using Display = FeedbackPedalControlDisplay;
    switch (control) {
    case 0u: return { "PRESS", 0u, Display::Percent, 0.0f, 100.0f, 0u };
    case 1u: return { "SAT", 1u, Display::Percent, 0.0f, 100.0f, 0u };
    case 2u: return { "FOLD", 2u, Display::Percent, 0.0f, 100.0f, 0u };
    case 3u: return { "CLIP", 3u, Display::Percent, 0.0f, 100.0f, 0u };
    case 4u: return { "TILT", 9u, Display::Bipolar,
        -100.0f, 100.0f, 0u };
    case 5u: return { "RETURN", 10u, Display::Percent,
        0.0f, 100.0f, 0u };
    case 6u: return { "SIZE", 4u, Display::MillisecondsLog,
        2.0f, 250.0f, 0u };
    case 7u: return { "DENSITY", 5u, Display::Ratio,
        1.25f, 8.0f, 0u };
    case 8u: return { "SCATTER", 6u, Display::Percent,
        0.0f, 100.0f, 0u };
    case 9u: return { "PITCH", 7u, Display::Semitones,
        -12.0f, 12.0f, 0u };
    case 10u: return { "COHERENCE", 12u, Display::Percent,
        0.0f, 100.0f, 0u };
    case 11u: return { "LANE DRIFT", 13u, Display::Percent,
        0.0f, 100.0f, 0u };
    case 12u: return { "EDGE", 8u, Display::Percent,
        0.0f, 100.0f, 0u };
    case 13u: return { "GRAIN MIX", 11u, Display::Percent,
        0.0f, 100.0f, 0u };
    case 14u: return { "SPACE", 14u, Display::MillisecondsSpacing,
        0.0f, 2000.0f, 0u };
    case 15u: return { "SHAPE", 15u, Display::Menu,
        0.0f, 4.0f, kFeedbackGrainShapeCount };
    default: return {};
    }
}

inline const char* feedbackAuxMenuItem(uint32_t control, uint32_t item)
{
    if (control == 15u) {
        return feedbackGrainShapeName(static_cast<FeedbackGrainShape>(
            std::min<uint32_t>(item, kFeedbackGrainShapeCount - 1u)));
    }
    return "";
}

inline const char* feedbackPedalMenuItem(FeedbackPedalType type,
    uint32_t control, uint32_t item)
{
    static constexpr const char* filterModes[4u] {
        "LOW PASS", "BAND PASS", "HIGH PASS", "NOTCH" };
    static constexpr const char* erosionShapes[2u] { "SINE", "NOISE" };
    static constexpr const char* directions[3u] {
        "FORWARD", "REVERSE", "ALTERNATE" };
    static constexpr const char* timeModes[3u] {
        "REVERSE", "FREEZE", "BRAKE" };
    static constexpr const char* windows[8u] {
        "8 MS", "16 MS", "32 MS", "64 MS",
        "125 MS", "250 MS", "500 MS", "1000 MS" };
    static constexpr const char* boolean[2u] { "OFF", "ON" };
    if (type == FeedbackPedalType::Filter && control == 2u)
        return filterModes[std::min(item, 3u)];
    if (type == FeedbackPedalType::Erosion && control == 3u)
        return erosionShapes[std::min(item, 1u)];
    if (type == FeedbackPedalType::Repeater && control == 0u)
        return windows[std::min(item, 7u)];
    if (type == FeedbackPedalType::Repeater && control == 3u)
        return directions[std::min(item, 2u)];
    if (type == FeedbackPedalType::TimeMangler && control == 0u)
        return windows[std::min(item, 7u)];
    if (type == FeedbackPedalType::TimeMangler && control == 3u)
        return timeModes[std::min(item, 2u)];
    if (type == FeedbackPedalType::DrumEcho && control == 0u)
        return drumEchoHeadModeName(static_cast<DrumEchoHeadMode>(
            std::min(item, kDrumEchoHeadModeCount - 1u)));
    if (type == FeedbackPedalType::DrumEcho && control == 1u)
        return drumEchoClockName(static_cast<DrumEchoClock>(
            std::min(item, kDrumEchoClockCount - 1u)));
    if (type == FeedbackPedalType::BreakBus && control == 7u)
        return boolean[std::min(item, 1u)];
    if (type == FeedbackPedalType::DrumBus && control == 0u)
        return drumOverloadCircuitName(static_cast<DrumOverloadCircuit>(
            std::min(item, kDrumOverloadCircuitCount - 1u)));
    if (type == FeedbackPedalType::Fracture && control == 0u)
        return fractureProcessorName(static_cast<FractureProcessor>(
            std::min(item, kFractureProcessorCount - 1u)));
    return "";
}

enum class FeedbackPulseShape : uint32_t {
    Sine = 0u,
    Square,
    Ramp,
    Random,
    Count,
};

constexpr uint32_t kFeedbackPulseShapeCount =
    static_cast<uint32_t>(FeedbackPulseShape::Count);

inline const char* feedbackPulseShapeName(FeedbackPulseShape shape)
{
    switch (shape) {
    case FeedbackPulseShape::Sine: return "SINE";
    case FeedbackPulseShape::Square: return "SQUARE";
    case FeedbackPulseShape::Ramp: return "RAMP";
    case FeedbackPulseShape::Random: return "RANDOM";
    case FeedbackPulseShape::Count: break;
    }
    return "SINE";
}

constexpr std::array<float, 11u> kFeedbackPulseDivisionBeats {{
    0.0625f, 0.125f, 0.25f, 0.5f, 1.0f, 2.0f,
    4.0f, 8.0f, 16.0f, 32.0f, 64.0f,
}};

inline const char* feedbackPulseDivisionName(uint32_t index)
{
    static constexpr std::array<const char*, 11u> names {{
        "1/64", "1/32", "1/16", "1/8", "1/4", "1/2",
        "1 BAR", "2 BAR", "4 BAR", "8 BAR", "16 BAR",
    }};
    return names[std::min<uint32_t>(index,
        static_cast<uint32_t>(names.size()) - 1u)];
}

// The CLAP parameter remains expressed in Hz. The inner 30% of either half of
// the slider is a dedicated 0..1 Hz precision zone; the remaining travel is a
// logarithmic 1..6000 Hz range. This makes sub-Hz tuning genuinely playable
// rather than merely representable as a host automation value.
inline float feedbackShiftFrequencyFromControlNorm(float normalized)
{
    normalized = std::clamp(std::isfinite(normalized) ? normalized : 0.5f,
        0.0f, 1.0f);
    const float signedDistance = (normalized - 0.5f) * 2.0f;
    if (std::abs(signedDistance) < 1.0e-7f) return 0.0f;
    const float distance = std::abs(signedDistance);
    constexpr float precisionZone = 0.30f;
    float hertz = 0.0f;
    if (distance <= precisionZone) {
        const float subHertz = distance / precisionZone;
        hertz = subHertz * subHertz;
    } else {
        const float upper = (distance - precisionZone)
            / (1.0f - precisionZone);
        hertz = std::pow(6000.0f, upper);
    }
    return std::copysign(hertz, signedDistance);
}

inline float feedbackShiftFrequencyControlNorm(float frequencyHz)
{
    frequencyHz = std::clamp(std::isfinite(frequencyHz) ? frequencyHz : 0.0f,
        -6000.0f, 6000.0f);
    if (std::abs(frequencyHz) < 1.0e-7f) return 0.5f;
    constexpr float precisionZone = 0.30f;
    const float magnitude = std::abs(frequencyHz);
    const float distance = magnitude <= 1.0f
        ? std::sqrt(magnitude) * precisionZone
        : precisionZone + (1.0f - precisionZone)
            * std::log(magnitude) / std::log(6000.0f);
    return 0.5f + std::copysign(distance * 0.5f, frequencyHz);
}

struct FeedbackShiftNodeParams {
    FeedbackShiftMode mode = FeedbackShiftMode::Frequency;
    FeedbackPedalType pedal = FeedbackPedalType::Bypass;
    FeedbackExciterSource exciterSource = FeedbackExciterSource::NoiseHit;
    float exciterGainDb = 0.0f;
    float frequencyHz = 0.0f;
    float regeneration = 0.62f;
    float color = 0.0f;
    float body = 0.34f;
    float levelDb = -6.0f;
    float pedalAmount = 0.52f;
    float pedalTone = 0.50f;
    float pedalBias = 0.0f;
    float pedalMix = 0.82f;
    std::array<float, kFeedbackPedalExtraParameterCount> pedalExtra {{
        0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f,
    }};
};

inline float feedbackPedalStorageValue(
    const FeedbackShiftNodeParams& node, uint32_t slot)
{
    switch (slot) {
    case 0u: return node.pedalAmount;
    case 1u: return node.pedalTone;
    case 2u: return node.pedalBias;
    case 3u: return node.pedalMix;
    default:
        return slot < 4u + kFeedbackPedalExtraParameterCount
            ? node.pedalExtra[slot - 4u] : 0.0f;
    }
}

inline void setFeedbackPedalStorageValue(
    FeedbackShiftNodeParams& node, uint32_t slot, float value)
{
    switch (slot) {
    case 0u: node.pedalAmount = value; break;
    case 1u: node.pedalTone = value; break;
    case 2u: node.pedalBias = value; break;
    case 3u: node.pedalMix = value; break;
    default:
        if (slot < 4u + kFeedbackPedalExtraParameterCount)
            node.pedalExtra[slot - 4u] = value;
        break;
    }
}

struct FeedbackShiftParams {
    float excite = 0.24f;
    float drift = 0.10f;
    float spliceAmount = 0.64f;
    float spliceRate = 0.66f;
    float spliceRateFine = 0.0f;
    float spliceContrast = 0.82f;
    float spliceSpace = 0.30f;
    float subBassTune = 0.34f;
    float subBassShape = 0.18f;
    float subBassDrive = 0.28f;
    float subBassDecay = 0.56f;
    float subBassSustain = 0.0f;
    float morph = 0.0f;
    FeedbackMorphSource morphSource = FeedbackMorphSource::Manual;
    float morphDepth = 1.0f;
    float morphRate = 0.34f;
    float morphInertia = 0.32f;
    bool morphHold = false;
    uint32_t morphSync = 1u;
    uint32_t morphDivision = 4u;
    FeedbackPulseShape morphShape = FeedbackPulseShape::Sine;
    float governorReflex = 0.32f;
    float governorSensitivity = 0.28f;
    float governorRecovery = 0.48f;
    float governorRest = 0.0f;
    float outputGainDb = -18.0f;
    FeedbackShiftOutputMode outputMode = FeedbackShiftOutputMode::Direct8;
    float outputRotationDeg = 0.0f;
    float auxPress = 0.28f;
    float auxSaturation = 0.18f;
    float auxFold = 0.0f;
    float auxClip = 0.0f;
    float auxGrainSize = 0.46f;
    float auxGrainDensity = 0.34f;
    float auxGrainScatter = 0.12f;
    float auxGrainPitch = 0.0f;
    float auxGrainEdge = 0.62f;
    float auxGrainCoherence = 1.0f;
    float auxGrainLaneDrift = 0.50f;
    float auxGrainMix = 0.0f;
    float auxGrainSpacing = 0.0f;
    FeedbackGrainShape auxGrainShape = FeedbackGrainShape::Tukey;
    float auxTilt = 0.0f;
    float auxMix = 0.0f;
    std::array<float, kFeedbackShiftChannels> auxSend {{
        1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
    }};
    std::array<float, kFeedbackShiftChannels> sceneBAuxSend {{
        1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
    }};
    bool run = true;
    std::array<FeedbackShiftNodeParams, kFeedbackShiftChannels> nodes {};
    std::array<FeedbackShiftNodeParams, kFeedbackShiftChannels>
        sceneBNodes {};
    std::array<float, kFeedbackShiftMatrixCells> matrix {};
    std::array<float, kFeedbackShiftMatrixCells> sceneBMatrix {};
};

inline float feedbackSpliceRateHz(float normalizedRate, float fine = 0.0f)
{
    normalizedRate = std::clamp(std::isfinite(normalizedRate)
            ? normalizedRate : 0.66f,
        0.0f, 1.0f);
    // The first quarter of travel is a deliberate slow-density zone. Above
    // it, the original high-rate range remains available through 384 Hz.
    const float coarse = normalizedRate < 0.25f
        ? (1.0f / 60.0f) * std::pow(90.0f, normalizedRate * 4.0f)
        : 1.5f * std::pow(256.0f,
            (normalizedRate - 0.25f) / 0.75f);
    const float fineMultiplier = std::exp2(std::clamp(
        std::isfinite(fine) ? fine : 0.0f, -1.0f, 1.0f));
    return std::clamp(coarse * fineMultiplier,
        1.0f / 60.0f, 384.0f);
}

inline float feedbackSubBassFrequencyHz(float normalizedTune)
{
    normalizedTune = std::clamp(std::isfinite(normalizedTune)
            ? normalizedTune : 0.34f,
        0.0f, 1.0f);
    return 18.0f * std::pow(6.6666667f, normalizedTune);
}

inline float feedbackSubBassDecayMs(float normalizedDecay)
{
    normalizedDecay = std::clamp(std::isfinite(normalizedDecay)
            ? normalizedDecay : 0.56f,
        0.0f, 1.0f);
    return 35.0f * std::pow(100.0f, normalizedDecay);
}

inline FeedbackShiftParams defaultFeedbackShiftParams()
{
    FeedbackShiftParams params;
    static constexpr std::array<float, kFeedbackShiftChannels> frequencies {{
        -720.0f, -210.0f, -42.0f, -4.0f,
        4.0f, 42.0f, 210.0f, 720.0f,
    }};
    for (uint32_t node = 0u; node < kFeedbackShiftChannels; ++node) {
        auto& lane = params.nodes[node];
        lane.mode = FeedbackShiftMode::Frequency;
        lane.pedal = FeedbackPedalType::Bypass;
        lane.frequencyHz = frequencies[node];
        lane.regeneration = 0.56f + 0.015f * static_cast<float>(node);
        lane.color = -0.24f + 0.07f * static_cast<float>(node);
        lane.body = 0.18f + 0.055f * static_cast<float>(node);
        lane.levelDb = -6.0f;
        params.matrix[node * kFeedbackShiftChannels + node] = 0.72f;
        const uint32_t previous = (node + kFeedbackShiftChannels - 1u)
            % kFeedbackShiftChannels;
        params.matrix[node * kFeedbackShiftChannels + previous] =
            (node & 1u) == 0u ? 0.12f : -0.10f;

        auto& sceneB = params.sceneBNodes[node];
        sceneB = lane;
        sceneB.frequencyHz = frequencies[
            (node + 3u) % kFeedbackShiftChannels] * 0.42f;
        sceneB.regeneration = 0.77f + 0.018f
            * static_cast<float>(node % 4u);
        sceneB.color = -0.12f + 0.14f * static_cast<float>(node);
        sceneB.body = 0.54f + 0.052f * static_cast<float>(node);
        sceneB.levelDb = -7.5f;
        params.sceneBMatrix[node * kFeedbackShiftChannels + node] = 0.90f;
        params.sceneBMatrix[node * kFeedbackShiftChannels
            + (node + 1u) % kFeedbackShiftChannels]
            = (node & 1u) == 0u ? 0.30f : -0.27f;
        params.sceneBMatrix[node * kFeedbackShiftChannels
            + (node + 5u) % kFeedbackShiftChannels]
            = (node & 1u) == 0u ? -0.16f : 0.18f;
        params.sceneBAuxSend[node] = 0.24f
            + 0.07f * static_cast<float>(node % 4u);
    }
    return params;
}

class FeedbackShift {
public:
    FeedbackShift()
        : target_(defaultFeedbackShiftParams()), smoothed_(target_)
    {
    }

    void prepare(double sampleRate) noexcept
    {
        sampleRate_ = std::isfinite(sampleRate)
            ? std::clamp(sampleRate, 8000.0, 768000.0) : 48000.0;
        const float sr = static_cast<float>(sampleRate_);
        parameterCoefficient_ = onePoleCoefficient(18.0f, sr);
        runCoefficient_ = onePoleCoefficient(8.0f, sr);
        detectorAttackCoefficient_ = onePoleCoefficient(4.0f, sr);
        detectorReleaseCoefficient_ = onePoleCoefficient(220.0f, sr);
        pedalFastAttackCoefficient_ = onePoleCoefficient(0.25f, sr);
        pedalFastReleaseCoefficient_ = onePoleCoefficient(18.0f, sr);
        pedalSlowAttackCoefficient_ = onePoleCoefficient(12.0f, sr);
        pedalSlowReleaseCoefficient_ = onePoleCoefficient(180.0f, sr);
        pedalGateAttackCoefficient_ = onePoleCoefficient(0.5f, sr);
        pedalGateReleaseCoefficient_ = onePoleCoefficient(55.0f, sr);
        governorAttackCoefficient_ = onePoleCoefficient(12.0f, sr);
        governorReleaseCoefficient_ = onePoleCoefficient(650.0f, sr);
        grainDuckAttackCoefficient_ = onePoleCoefficient(2.0f, sr);
        grainDuckReleaseCoefficient_ = onePoleCoefficient(85.0f, sr);
        noiseHighpassCoefficient_ = frequencyCoefficient(18.0f, sr);
        sourceSwitchCoefficient_ = onePoleCoefficient(1.5f, sr);
        driftSlewCoefficient_ = onePoleCoefficient(850.0f, sr);
        splicePulseDecay_ = std::exp(-1.0f / (sr * 0.025f));
        burstDecay_ = std::exp(-1.0f / (sr * 0.075f));
        dcPole_ = std::exp(-kTwoPi * 12.0f / sr);
        pedalCrossfadeIncrement_ = 1.0f / std::max(1.0f, sr * 0.020f);
        for (auto& state : states_) {
            state.resonatorBuffer.assign(static_cast<std::size_t>(
                std::max(2048.0, std::ceil(sampleRate_ / 35.0))), 0.0f);
            state.temporalBuffer.assign(static_cast<std::size_t>(
                std::max(64.0, std::ceil(sampleRate_))), 0.0f);
            state.modulationRuntime.timeBuffer.assign(
                static_cast<std::size_t>(std::max(
                    64.0, std::ceil(sampleRate_ * 0.05))), 0.0f);
            state.echo.prepare(sampleRate_, 2.0);
            state.breakBus.prepare(sampleRate_);
            state.drumBus.prepare(sampleRate_);
            if (!state.macroShred) {
                state.macroShred = std::make_unique<MacroShredCore>();
            }
            state.macroShred->prepare(sampleRate_);
            if (!state.macroPitch) {
                state.macroPitch = std::make_unique<MacroPitch>();
            }
            state.macroPitch->prepare(sampleRate_, 1u);
            if (!state.macroDelay) {
                state.macroDelay = std::make_unique<MacroDelay>();
            }
            state.macroDelay->prepare(sampleRate_, 1u);
            if (!state.fracture) {
                state.fracture = std::make_unique<MacroFractureCore>();
            }
            state.fracture->prepare(sampleRate_);
        }
        auxGrainBufferFrames_ = static_cast<uint32_t>(std::max(
            64.0, std::ceil(sampleRate_ * 1.5)));
        auxGrainBuffer_.assign(static_cast<std::size_t>(
            auxGrainBufferFrames_) * kFeedbackShiftChannels, 0.0f);
        auxBreakBus_.prepare(sampleRate_);
        reset();
    }

    void reset() noexcept
    {
        for (uint32_t node = 0u; node < kFeedbackShiftChannels; ++node) {
            resetNodeState(states_[node], target_.nodes[node].pedal);
        }
        previousReturns_ = {};
        returns_ = {};
        ecologyReturns_ = {};
        phases_ = {};
        for (uint32_t node = 0u; node < kFeedbackShiftChannels; ++node) {
            phases_[node] = static_cast<float>(node)
                / static_cast<float>(kFeedbackShiftChannels) * kTwoPi;
        }
        driftValues_.fill(0.0f);
        driftTargets_.fill(0.0f);
        driftCountdowns_.fill(0u);
        morphPhase_ = 0.0f;
        morphRandom_ = 0.5f;
        morphChaosTarget_ = 0.73f;
        morphChaosValue_ = 0.5f;
        morphValue_ = target_.morph;
        morphDriverValue_ = target_.morph;
        morphInputEnvelope_ = 0.0f;
        morphEdge_ = 0.0f;
        laneEnvelopes_.fill(0.0f);
        laneSlowEnvelopes_.fill(0.0f);
        laneEdges_.fill(0.0f);
        effectiveAuxSend_ = target_.auxSend;
        noiseState_ = 0x6d2b79f5u;
        driftRandom_ = 0x44524654u;
        restRandom_ = 0x52455354u;
        spliceRandom_ = 0x53504c43u;
        restGains_.fill(1.0f);
        restPersistenceFrames_.fill(0.0f);
        restRemainingFrames_.fill(0u);
        restTriggerFrames_.fill(48000u);
        splicePatches_ = {};
        splicePendingPatches_ = {};
        splicePatchValid_.fill(false);
        spliceStages_.fill(SpliceStage::Waiting);
        spliceGains_.fill(1.0f);
        spliceFadeSteps_.fill(1.0f);
        spliceCountdowns_.fill(0u);
        spliceFlurryRemaining_.fill(0u);
        splicePulses_.fill(0.0f);
        spliceEventCount_ = 0u;
        burstEnvelope_.fill(0.0f);
        subBassEnvelope_.fill(0.0f);
        subBassDecayCoefficient_ = 0.9995f;
        subBassControlCounter_ = 0u;
        outputPeak_.fill(0.0f);
        nodeActivity_.fill(0.0f);
        routeSignals_.fill(0.0f);
        std::fill(auxGrainBuffer_.begin(), auxGrainBuffer_.end(), 0.0f);
        auxGrains_ = {};
        auxGrainWrite_ = 0u;
        auxGrainWrittenFrames_ = 0u;
        auxGrainSpawnCountdown_ = 0.0f;
        auxGrainSizeFrames_ = 96u;
        auxGrainDensityRatio_ = 3.0f;
        auxGrainScatterFrames_ = 0u;
        auxGrainPitchRatio_ = 1.0f;
        auxGrainFadeFraction_ = 0.28f;
        auxGrainSpacingFrames_ = 0u;
        auxGrainLastLengthFrames_ = 96u;
        auxGrainActivity_ = 0.0f;
        auxGrainDuck_.fill(0.0f);
        auxFoldPrevious_.fill(0.0f);
        auxFoldDcInput_.fill(0.0f);
        auxFoldDcOutput_.fill(0.0f);
        auxRandom_ = 0x41555838u;
        auxControlCounter_ = 0u;
        auxGrainControlCounter_ = 0u;
        auxActivity_ = 0.0f;
        auxGainReductionDb_ = 0.0f;
        auxBreakBus_.reset();
        minimumGovernor_ = 1.0f;
        smoothed_ = target_;
        for (uint32_t lane = 0u; lane < kFeedbackShiftChannels; ++lane) {
            scheduleNextRest(lane);
            scheduleNextSplice(lane, true);
        }
        runGain_ = target_.run ? 1.0f : 0.0f;
    }

    void setParams(FeedbackShiftParams params) noexcept
    {
        target_ = sanitize(params);
    }

    FeedbackShiftParams params() const noexcept { return target_; }

    void setTransport(double tempoBpm, bool hasTempo) noexcept
    {
        transportTempoBpm_ = std::isfinite(tempoBpm)
            ? std::clamp(tempoBpm, 1.0, 1000.0) : 120.0;
        transportHasTempo_ = hasTempo;
        for (auto& state : states_) {
            state.echo.setTempo(transportTempoBpm_, transportHasTempo_);
        }
    }

    void strike(uint32_t node, float velocity = 1.0f) noexcept
    {
        if (node >= kFeedbackShiftChannels || !std::isfinite(velocity)) {
            return;
        }
        burstEnvelope_[node] = std::max(burstEnvelope_[node],
            std::clamp(velocity, 0.0f, 1.0f));
        subBassEnvelope_[node] = std::max(subBassEnvelope_[node],
            std::clamp(velocity, 0.0f, 1.0f));
    }

    void strikeAll(float velocity = 1.0f) noexcept
    {
        for (uint32_t node = 0u; node < kFeedbackShiftChannels; ++node) {
            strike(node, velocity);
        }
    }

    void panic() noexcept { reset(); }

    void processFrame(float* output) noexcept
    {
        processFrame(nullptr, output);
    }

    void processFrame(const float* externalInput, float* output) noexcept
    {
        if (!output) return;
        smoothParameters();
        runGain_ += ((smoothed_.run ? 1.0f : 0.0f) - runGain_)
            * runCoefficient_;
        advanceMorph(externalInput);
        advanceRestGovernors();
        previousReturns_ = returns_;

        auto effectiveNodes = smoothed_.nodes;
        std::array<float, kFeedbackShiftMatrixCells> effectiveMatrix {};
        for (uint32_t node = 0u; node < kFeedbackShiftChannels; ++node) {
            const auto& sceneB = smoothed_.sceneBNodes[node];
            auto& effective = effectiveNodes[node];
            effective.frequencyHz = lerp(effective.frequencyHz,
                sceneB.frequencyHz, morphValue_);
            effective.regeneration = lerp(effective.regeneration,
                sceneB.regeneration, morphValue_);
            effective.color = lerp(effective.color,
                sceneB.color, morphValue_);
            effective.body = lerp(effective.body,
                sceneB.body, morphValue_);
            effective.levelDb = lerp(effective.levelDb,
                sceneB.levelDb, morphValue_);
            // Pea Soup-inspired negative control feedback: rising ecology
            // energy moves each lane's frequency relationship in a different
            // direction before the amplitude governor needs to clamp it. The
            // quadratic/log mapping preserves very fine sub-Hz motion while
            // still allowing decisive modal escapes at the end of the range.
            const float reflex = smoothed_.governorReflex;
            if (reflex > 1.0e-5f) {
                static constexpr std::array<float,
                    kFeedbackShiftChannels> directions {{
                    -1.00f, 0.73f, -0.51f, 0.37f,
                    1.00f, -0.79f, 0.57f, -0.41f,
                }};
                const float rms = std::sqrt(std::max(
                    0.0f, states_[node].energy));
                const float threshold = governorThreshold(
                    smoothed_.governorSensitivity);
                const float sustained = std::clamp((rms
                        - threshold * 0.35f)
                        / std::max(0.05f, threshold * 1.35f),
                    0.0f, 1.0f);
                const float reflexAmount = std::max(
                    laneEdges_[node], sustained * 0.72f);
                const float maximumHz = 0.02f
                    * std::pow(5000.0f, reflex);
                effective.frequencyHz = std::clamp(effective.frequencyHz
                        + directions[node] * maximumHz * reflexAmount,
                    -6000.0f, 6000.0f);
            }
            effectiveAuxSend_[node] = lerp(smoothed_.auxSend[node],
                smoothed_.sceneBAuxSend[node], morphValue_);
        }
        for (uint32_t index = 0u; index < kFeedbackShiftMatrixCells;
             ++index) {
            effectiveMatrix[index] = lerp(smoothed_.matrix[index],
                smoothed_.sceneBMatrix[index], morphValue_);
        }
        advanceSpliceEcology(effectiveNodes, effectiveMatrix);

        std::array<float, kFeedbackShiftChannels> matrixInput {};
        for (uint32_t destination = 0u;
             destination < kFeedbackShiftChannels; ++destination) {
            auto& state = states_[destination];
            const float noise = randomBipolar();
            state.noiseLowpass += (noise - state.noiseLowpass)
                * noiseHighpassCoefficient_;
            const float input = externalInput
                ? externalInput[destination] : 0.0f;
            const float excitation = processExciter(destination, input,
                noise - state.noiseLowpass, effectiveNodes[destination])
                * runGain_;
            float feedbackSum = 0.0f;
            float weight = 0.0f;
            for (uint32_t source = 0u;
                 source < kFeedbackShiftChannels; ++source) {
                const uint32_t index = destination
                    * kFeedbackShiftChannels + source;
                const float route = effectiveMatrix[index];
                if (std::abs(route) < 1.0e-7f) {
                    routeSignals_[index] = 0.0f;
                    continue;
                }
                const float signal = previousReturns_[source] * route;
                routeSignals_[index] = signal;
                feedbackSum += signal;
                weight += std::abs(route);
            }
            const float normalization = 1.0f
                / std::sqrt(std::max(1.0f, weight * 0.70f));
            // An external source is feed-forward audio, not part of the
            // feedback amount. REGEN controls recirculation through both the
            // visible matrix and processShift's local wet loop. Internal
            // exciters retain the original REGEN-dependent behavior, while
            // REGEN=0 turns an external lane into a useful fully wet insert.
            const auto source = effectiveNodes[destination].exciterSource;
            const bool externalSource = source
                    == FeedbackExciterSource::External
                || source == FeedbackExciterSource::ExternalHit;
            const float feedbackGain = regenerationGain(
                effectiveNodes[destination].regeneration);
            const float sourceGain = externalSource ? 1.0f
                : std::min(1.0f, feedbackGain);
            const float networkInput = excitation * sourceGain + feedbackSum
                * normalization * feedbackGain;
            matrixInput[destination] = std::clamp(networkInput
                * states_[destination].governor * runGain_,
                -5.0f, 5.0f);
        }

        minimumGovernor_ = 1.0f;
        std::array<float, kFeedbackShiftChannels> directOutput {};
        for (uint32_t node = 0u; node < kFeedbackShiftChannels; ++node) {
            auto& state = states_[node];
            const auto& params = effectiveNodes[node];
            float value = processShift(node, matrixInput[node], params);
            value = processPedal(node, value,
                previousReturns_[(node + 1u) % kFeedbackShiftChannels],
                params);
            if (!std::isfinite(value)) {
                clearNodeFaultState(state);
                value = 0.0f;
            }
            value = std::clamp(value, -8.0f, 8.0f);
            const float energyInput = value * value;
            const float detectorCoefficient = energyInput > state.energy
                ? detectorAttackCoefficient_ : detectorReleaseCoefficient_;
            state.energy += (energyInput - state.energy)
                * detectorCoefficient;
            state.energy = flush(state.energy);
            const float rms = std::sqrt(std::max(0.0f, state.energy));
            const float excess = std::max(0.0f, rms
                - governorThreshold(smoothed_.governorSensitivity));
            const float targetGovernor = 1.0f
                / (1.0f + excess * 0.9f + excess * excess * 5.0f);
            const float governorCoefficient = targetGovernor < state.governor
                ? governorAttackCoefficient_ : governorReleaseCoefficient_;
            state.governor += (targetGovernor - state.governor)
                * governorCoefficient;
            state.governor = std::clamp(state.governor, 0.12f, 1.0f);
            minimumGovernor_ = std::min(minimumGovernor_, state.governor);

            float returned = value * state.governor * restGains_[node]
                * spliceGains_[node];
            returned = dcBlock(returned, state.returnDcInput,
                state.returnDcOutput);
            returns_[node] = flush(returned);
            nodeActivity_[node] += (rms - nodeActivity_[node]) * 0.003f;

            const float gain = outputGain_ * dbGain(params.levelDb);
            float audition = dcBlock(value * gain * runGain_,
                state.outputDcInput, state.outputDcOutput);
            audition = transparentLimit(audition);
            directOutput[node] = flush(audition * spliceGains_[node]);
            burstEnvelope_[node] *= burstDecay_;
            if (burstEnvelope_[node] < 1.0e-6f) {
                burstEnvelope_[node] = 0.0f;
            }
        }
        // Detect the core network before the optional AUX return is injected.
        // This prevents an un-routed AUX wall from becoming an invisible
        // global modulation path into otherwise independent dry nodes.
        ecologyReturns_ = returns_;
        // The feedback wall returns to the matrix sources one sample later;
        // the granulator is deliberately post-network and precedes output
        // topology so direct, quad and stereo modes share the same texture.
        processAuxFeedback(returns_);
        processPostGranulator(directOutput);
        for (uint32_t lane = 0u; lane < kFeedbackShiftChannels; ++lane) {
            directOutput[lane] *= restGains_[lane];
        }
        std::fill(output, output + kFeedbackShiftChannels, 0.0f);
        if (smoothed_.outputMode == FeedbackShiftOutputMode::Direct8) {
            std::copy(directOutput.begin(), directOutput.end(), output);
        } else {
            McStereoParams foldParams;
            foldParams.inputChannels = kFeedbackShiftChannels;
            foldParams.rotationDegrees = smoothed_.outputRotationDeg;
            foldParams.layout = McStereoLayout::RingProjection;
            foldParams.autogain = McStereoAutogain::PowerSqrtN;
            foldParams.outputGainDb = 0.0f;
            if (smoothed_.outputMode == FeedbackShiftOutputMode::QuadRing) {
                processMcToQuadFrame(directOutput.data(),
                    kFeedbackShiftChannels, output, foldParams);
                for (uint32_t channel = 0u; channel < 4u; ++channel) {
                    output[channel] = transparentLimit(output[channel]);
                }
            } else {
                processMcToStereoFrame(directOutput.data(),
                    kFeedbackShiftChannels, output, foldParams);
                output[0u] = transparentLimit(output[0u]);
                output[1u] = transparentLimit(output[1u]);
            }
        }
        for (uint32_t channel = 0u; channel < kFeedbackShiftChannels;
             ++channel) {
            output[channel] = flush(output[channel]);
            outputPeak_[channel] = std::max(
                outputPeak_[channel] * 0.9992f, std::abs(output[channel]));
        }
        advanceDrift();
    }

    float outputPeak(uint32_t node) const noexcept
    {
        return node < kFeedbackShiftChannels ? outputPeak_[node] : 0.0f;
    }

    float nodeActivity(uint32_t node) const noexcept
    {
        return node < kFeedbackShiftChannels ? nodeActivity_[node] : 0.0f;
    }

    uint32_t temporalPhase(uint32_t node) const noexcept
    {
        return node < kFeedbackShiftChannels
            ? states_[node].temporalPhase : 0u;
    }

    float temporalProgress(uint32_t node) const noexcept
    {
        if (node >= kFeedbackShiftChannels) return 0.0f;
        const auto& state = states_[node];
        if (state.temporalFrames < 2u) return 0.0f;
        if (state.temporalPhase == 1u) {
            return std::clamp(static_cast<float>(state.temporalPosition)
                / static_cast<float>(state.temporalFrames), 0.0f, 1.0f);
        }
        if (state.temporalPhase != 2u) return 0.0f;
        const double length = static_cast<double>(state.temporalFrames);
        double position = std::fmod(state.temporalRead, length);
        if (position < 0.0) position += length;
        return std::clamp(static_cast<float>(position / length), 0.0f, 1.0f);
    }

    float routeSignal(uint32_t destination, uint32_t source) const noexcept
    {
        if (destination >= kFeedbackShiftChannels
            || source >= kFeedbackShiftChannels) return 0.0f;
        return routeSignals_[destination * kFeedbackShiftChannels + source];
    }

    float minimumGovernor() const noexcept { return minimumGovernor_; }
    float morphValue() const noexcept { return morphValue_; }
    float morphDriverValue() const noexcept { return morphDriverValue_; }
    float morphPhase() const noexcept { return morphPhase_; }
    float inputEnvelope() const noexcept
    {
        return std::clamp(morphInputEnvelope_, 0.0f, 1.0f);
    }
    float ecologyEnvelope() const noexcept { return inputEnvelope(); }
    float ecologyEdge() const noexcept { return morphEdge_; }
    float laneEcologyEnvelope(uint32_t lane) const noexcept
    {
        return lane < kFeedbackShiftChannels
            ? std::clamp(laneEnvelopes_[lane], 0.0f, 1.0f) : 0.0f;
    }
    float laneEcologyEdge(uint32_t lane) const noexcept
    {
        return lane < kFeedbackShiftChannels ? laneEdges_[lane] : 0.0f;
    }
    float restLaneActivity(uint32_t lane) const noexcept
    {
        return lane < kFeedbackShiftChannels
            ? 1.0f - restGains_[lane] : 0.0f;
    }
    float restActivity() const noexcept
    {
        float maximum = 0.0f;
        for (float gain : restGains_) maximum = std::max(maximum, 1.0f - gain);
        return maximum;
    }
    uint32_t restingLaneCount() const noexcept
    {
        uint32_t count = 0u;
        for (float gain : restGains_) count += gain < 0.5f ? 1u : 0u;
        return count;
    }
    float spliceLaneActivity(uint32_t lane) const noexcept
    {
        return lane < kFeedbackShiftChannels
            ? splicePulses_[lane] : 0.0f;
    }
    float spliceLaneGate(uint32_t lane) const noexcept
    {
        return lane < kFeedbackShiftChannels
            ? spliceGains_[lane] : 1.0f;
    }
    float spliceActivity() const noexcept
    {
        float maximum = 0.0f;
        for (float pulse : splicePulses_) maximum = std::max(maximum, pulse);
        return maximum;
    }
    uint32_t spliceEventCount() const noexcept { return spliceEventCount_; }
    float auxActivity() const noexcept { return auxActivity_; }
    float auxGainReductionDb() const noexcept { return auxGainReductionDb_; }
    float auxGrainActivity() const noexcept { return auxGrainActivity_; }
    float auxGrainSourceDuck() const noexcept
    {
        float sum = 0.0f;
        for (float duck : auxGrainDuck_) sum += duck;
        return sum / static_cast<float>(kFeedbackShiftChannels);
    }
private:
    static constexpr float kPi = 3.14159265358979323846f;
    static constexpr float kTwoPi = 2.0f * kPi;
    static constexpr std::size_t kHilbertStages = 4u;
    static constexpr uint32_t kBodyDelayFrames = 4096u;
    static constexpr uint32_t kAuxGrainVoiceCount = 10u;

    struct DriveState {
        float low = 0.0f;
        float high = 0.0f;
        float memory = 0.0f;
        float envelope = 0.0f;
    };

    struct AuxGrain {
        std::array<double, kFeedbackShiftChannels> readPosition {};
        std::array<float, kFeedbackShiftChannels> increment {};
        std::array<uint32_t, kFeedbackShiftChannels> age {};
        std::array<uint32_t, kFeedbackShiftChannels> length {};
        std::array<bool, kFeedbackShiftChannels> enabled {};
        bool active = false;
    };

    struct SplicePatch {
        FeedbackShiftMode mode = FeedbackShiftMode::Frequency;
        float frequencyHz = 0.0f;
        float regeneration = 0.5f;
        float color = 0.0f;
        float body = 0.3f;
        float levelDb = -9.0f;
        float auxSend = 0.0f;
        std::array<float, kFeedbackShiftChannels> routes {};
        bool audible = true;
    };

    enum class SpliceStage : uint8_t {
        Waiting = 0u,
        FadeOut,
        FadeIn,
    };

    struct ModulationState {
        float phase = 0.0f;
        float gate = 0.0f;
    };

    struct ModulationRuntime {
        std::vector<float> timeBuffer {};
        uint32_t timeWrite = 0u;
    };

    struct NodeState {
        std::array<float, kHilbertStages> hilbertA {};
        std::array<float, kHilbertStages> hilbertB {};
        DriveState drive {};
        float noiseLowpass = 0.0f;
        float sourceSignal = 0.0f;
        float sourcePhase = 0.0f;
        float returnDcInput = 0.0f;
        float returnDcOutput = 0.0f;
        float outputDcInput = 0.0f;
        float outputDcOutput = 0.0f;
        float phaseMemoryA = 0.0f;
        float phaseMemoryB = 0.0f;
        std::array<float, kBodyDelayFrames> bodyDelay {};
        uint32_t bodyWrite = 0u;
        float loopLowpass = 0.0f;
        float loopMemory = 0.0f;
        float crushHeld = 0.0f;
        float crushCounter = 0.0f;
        float relayEnvelope = 0.0f;
        float relayGate = 0.0f;
        float relaySlew = 0.0f;
        float energy = 0.0f;
        float governor = 1.0f;

        // Slicer-derived listening processors. All storage is prepared before
        // rendering; transient capture never follows the host transport.
        float filterIc1 = 0.0f;
        float filterIc2 = 0.0f;
        float held = 0.0f;
        uint32_t holdRemaining = 0u;
        uint32_t random = 0x9e3779b9u;
        float transientFast = 0.0f;
        float transientSlow = 0.0f;
        float transientGate = 1.0f;
        std::vector<float> resonatorBuffer {};
        uint32_t resonatorWrite = 0u;
        float resonatorDamped = 0.0f;
        std::array<float, 2048u> erosionBuffer {};
        uint32_t erosionWrite = 0u;
        float erosionPhase = 0.0f;
        float erosionNoise = 0.0f;
        float erosionTarget = 0.0f;
        float foldPreviousInput = 0.0f;
        float foldDcInput = 0.0f;
        float foldDcOutput = 0.0f;
        std::vector<float> temporalBuffer {};
        uint32_t temporalPhase = 0u; // listening, capture, playback
        uint32_t temporalFrames = 0u;
        uint32_t temporalPosition = 0u;
        uint32_t temporalRepeats = 1u;
        uint32_t temporalLoopCount = 0u;
        uint32_t temporalPlayback = 0u;
        double temporalRead = 0.0;
        float temporalDriftRatio = 1.0f;
        float temporalDetectorFast = 0.0f;
        float temporalDetectorSlow = 0.0f;
        bool temporalArmed = true;

        DrumEcho echo {};
        BreakBus breakBus {};
        DrumOverload drumBus {};
        ModulationState modulation {};
        ModulationRuntime modulationRuntime {};
        std::unique_ptr<MacroShredCore> macroShred {};
        std::unique_ptr<MacroPitch> macroPitch {};
        std::unique_ptr<MacroDelay> macroDelay {};
        std::unique_ptr<MacroFractureCore> fracture {};
        FeedbackPedalType activePedal = FeedbackPedalType::Bypass;
        float pedalCrossfade = 1.0f;
        uint32_t controlCounter = 0u;
    };

    static FeedbackShiftParams sanitize(FeedbackShiftParams params) noexcept
    {
        params.excite = finiteClamp(params.excite, 0.0f, 1.0f, 0.24f);
        params.drift = finiteClamp(params.drift, 0.0f, 1.0f, 0.10f);
        params.spliceAmount = finiteClamp(params.spliceAmount,
            0.0f, 1.0f, 0.64f);
        params.spliceRate = finiteClamp(params.spliceRate,
            0.0f, 1.0f, 0.66f);
        params.spliceRateFine = finiteClamp(params.spliceRateFine,
            -1.0f, 1.0f, 0.0f);
        params.spliceContrast = finiteClamp(params.spliceContrast,
            0.0f, 1.0f, 0.82f);
        params.spliceSpace = finiteClamp(params.spliceSpace,
            0.0f, 1.0f, 0.30f);
        params.subBassTune = finiteClamp(params.subBassTune,
            0.0f, 1.0f, 0.34f);
        params.subBassShape = finiteClamp(params.subBassShape,
            0.0f, 1.0f, 0.18f);
        params.subBassDrive = finiteClamp(params.subBassDrive,
            0.0f, 1.0f, 0.28f);
        params.subBassDecay = finiteClamp(params.subBassDecay,
            0.0f, 1.0f, 0.56f);
        params.subBassSustain = finiteClamp(params.subBassSustain,
            0.0f, 1.0f, 0.0f);
        params.morph = finiteClamp(params.morph, 0.0f, 1.0f, 0.0f);
        params.morphSource = static_cast<FeedbackMorphSource>(
            std::min<uint32_t>(static_cast<uint32_t>(params.morphSource),
                kFeedbackMorphSourceCount - 1u));
        params.morphDepth = finiteClamp(params.morphDepth,
            0.0f, 1.0f, 1.0f);
        params.morphRate = finiteClamp(params.morphRate,
            0.0f, 1.0f, 0.34f);
        params.morphInertia = finiteClamp(params.morphInertia,
            0.0f, 1.0f, 0.32f);
        params.morphSync = params.morphSync != 0u ? 1u : 0u;
        params.morphDivision = std::min<uint32_t>(params.morphDivision,
            static_cast<uint32_t>(kFeedbackPulseDivisionBeats.size()) - 1u);
        params.morphShape = static_cast<FeedbackPulseShape>(
            std::min<uint32_t>(static_cast<uint32_t>(params.morphShape),
                kFeedbackPulseShapeCount - 1u));
        params.governorReflex = finiteClamp(params.governorReflex,
            0.0f, 1.0f, 0.32f);
        params.governorSensitivity = finiteClamp(
            params.governorSensitivity, 0.0f, 1.0f, 0.28f);
        params.governorRecovery = finiteClamp(params.governorRecovery,
            0.0f, 1.0f, 0.48f);
        params.governorRest = finiteClamp(params.governorRest,
            0.0f, 1.0f, 0.0f);
        params.outputGainDb = finiteClamp(params.outputGainDb,
            -60.0f, 6.0f, -18.0f);
        params.outputMode = static_cast<FeedbackShiftOutputMode>(
            std::min<uint32_t>(static_cast<uint32_t>(params.outputMode),
                kFeedbackShiftOutputModeCount - 1u));
        params.outputRotationDeg = finiteClamp(params.outputRotationDeg,
            -180.0f, 180.0f, 0.0f);
        params.auxPress = finiteClamp(params.auxPress, 0.0f, 1.0f, 0.28f);
        params.auxSaturation = finiteClamp(params.auxSaturation,
            0.0f, 1.0f, 0.18f);
        params.auxFold = finiteClamp(params.auxFold, 0.0f, 1.0f, 0.0f);
        params.auxClip = finiteClamp(params.auxClip, 0.0f, 1.0f, 0.0f);
        params.auxGrainSize = finiteClamp(params.auxGrainSize,
            0.0f, 1.0f, 0.46f);
        params.auxGrainDensity = finiteClamp(params.auxGrainDensity,
            0.0f, 1.0f, 0.34f);
        params.auxGrainScatter = finiteClamp(params.auxGrainScatter,
            0.0f, 1.0f, 0.12f);
        params.auxGrainPitch = finiteClamp(params.auxGrainPitch,
            -1.0f, 1.0f, 0.0f);
        params.auxGrainEdge = finiteClamp(params.auxGrainEdge,
            0.0f, 1.0f, 0.62f);
        params.auxGrainCoherence = finiteClamp(params.auxGrainCoherence,
            0.0f, 1.0f, 1.0f);
        params.auxGrainLaneDrift = finiteClamp(params.auxGrainLaneDrift,
            0.0f, 1.0f, 0.50f);
        params.auxGrainMix = finiteClamp(params.auxGrainMix,
            0.0f, 1.0f, 0.0f);
        params.auxGrainSpacing = finiteClamp(params.auxGrainSpacing,
            0.0f, 1.0f, 0.0f);
        params.auxGrainShape = static_cast<FeedbackGrainShape>(
            std::min<uint32_t>(static_cast<uint32_t>(params.auxGrainShape),
                kFeedbackGrainShapeCount - 1u));
        params.auxTilt = finiteClamp(params.auxTilt, -1.0f, 1.0f, 0.0f);
        params.auxMix = finiteClamp(params.auxMix, 0.0f, 1.0f, 0.0f);
        for (float& send : params.auxSend) {
            send = finiteClamp(send, 0.0f, 1.0f, 1.0f);
        }
        for (float& send : params.sceneBAuxSend) {
            send = finiteClamp(send, 0.0f, 1.0f, 1.0f);
        }
        const auto sanitizeNode = [](FeedbackShiftNodeParams& node) noexcept {
            node.mode = static_cast<FeedbackShiftMode>(
                std::min<uint32_t>(static_cast<uint32_t>(node.mode), 1u));
            node.pedal = static_cast<FeedbackPedalType>(
                std::min<uint32_t>(static_cast<uint32_t>(node.pedal),
                    kFeedbackPedalTypeCount - 1u));
            node.exciterSource = static_cast<FeedbackExciterSource>(
                std::min<uint32_t>(
                    static_cast<uint32_t>(node.exciterSource),
                    kFeedbackExciterSourceCount - 1u));
            node.exciterGainDb = finiteClamp(node.exciterGainDb,
                -60.0f, 12.0f, 0.0f);
            node.frequencyHz = finiteClamp(node.frequencyHz,
                -6000.0f, 6000.0f, 0.0f);
            node.regeneration = finiteClamp(node.regeneration,
                0.0f, 1.0f, 0.62f);
            node.color = finiteClamp(node.color, -1.0f, 1.0f, 0.0f);
            node.body = finiteClamp(node.body, 0.0f, 1.0f, 0.34f);
            node.levelDb = finiteClamp(node.levelDb,
                -60.0f, 6.0f, -6.0f);
            node.pedalAmount = finiteClamp(node.pedalAmount,
                0.0f, 1.0f, 0.52f);
            node.pedalTone = finiteClamp(node.pedalTone,
                0.0f, 1.0f, 0.50f);
            node.pedalBias = finiteClamp(node.pedalBias,
                -1.0f, 1.0f, 0.0f);
            node.pedalMix = finiteClamp(node.pedalMix,
                0.0f, 1.0f, 0.82f);
            for (float& extra : node.pedalExtra) {
                extra = finiteClamp(extra, 0.0f, 1.0f, 0.5f);
            }
        };
        for (auto& node : params.nodes) {
            sanitizeNode(node);
        }
        for (auto& node : params.sceneBNodes) {
            sanitizeNode(node);
        }
        for (float& route : params.matrix) {
            route = finiteClamp(route, -1.0f, 1.0f, 0.0f);
        }
        for (float& route : params.sceneBMatrix) {
            route = finiteClamp(route, -1.0f, 1.0f, 0.0f);
        }
        return params;
    }

    static float finiteClamp(float value, float minimum, float maximum,
        float fallback) noexcept
    {
        return std::isfinite(value)
            ? std::clamp(value, minimum, maximum) : fallback;
    }

    static float flush(float value) noexcept
    {
        if (!std::isfinite(value)) return 0.0f;
        return std::abs(value) < 1.0e-20f ? 0.0f : value;
    }

    static float onePoleCoefficient(float milliseconds,
        float sampleRate) noexcept
    {
        const float frames = std::max(1.0f,
            milliseconds * 0.001f * sampleRate);
        return 1.0f - std::exp(-1.0f / frames);
    }

    static float frequencyCoefficient(float frequency,
        float sampleRate) noexcept
    {
        return 1.0f - std::exp(-kTwoPi
            * std::clamp(frequency, 1.0f, sampleRate * 0.45f) / sampleRate);
    }

    static float dbGain(float decibels) noexcept
    {
        return std::pow(10.0f, decibels * 0.05f);
    }

    static float lerp(float a, float b, float amount) noexcept
    {
        return a + (b - a) * amount;
    }

    // The control's broad center is the musically useful unity neighborhood.
    // Below 72% loops decay predictably; the upper 28% crosses gradually into
    // metastable and self-sustaining behavior instead of hiding it at the
    // final few pixels of the slider.
    static float regenerationGain(float normalized) noexcept
    {
        normalized = std::clamp(normalized, 0.0f, 1.0f);
        if (normalized <= 0.72f) {
            return normalized / 0.72f * 0.985f;
        }
        const float above = (normalized - 0.72f) / 0.28f;
        return 0.985f + std::pow(above, 1.35f) * 0.38f;
    }

    static float governorThreshold(float sensitivity) noexcept
    {
        // SENSE increases sensitivity by lowering the containment knee from
        // near full scale to a deliberately conservative -12 dB region.
        return 0.95f - 0.70f
            * std::clamp(sensitivity, 0.0f, 1.0f);
    }

    // Pass ordinary material unchanged and round only the last five percent
    // before full scale. This remains a safety ceiling rather than a permanent
    // tone-shaping stage.
    static float transparentLimit(float value) noexcept
    {
        const float magnitude = std::abs(value);
        if (magnitude <= 0.95f) return value;
        const float limited = 0.95f + 0.05f
            * std::tanh((magnitude - 0.95f) * 20.0f);
        return std::copysign(limited, value);
    }

    static float allpassCascade(float input,
        std::array<float, kHilbertStages>& state,
        const std::array<float, kHilbertStages>& coefficients) noexcept
    {
        float value = input;
        for (std::size_t stage = 0u; stage < state.size(); ++stage) {
            const float output = coefficients[stage] * value + state[stage];
            state[stage] = flush(value - coefficients[stage] * output);
            value = flush(output);
        }
        return value;
    }

    static float fold(float input) noexcept
    {
        float value = std::fmod(input + 3.0f, 4.0f);
        if (value < 0.0f) value += 4.0f;
        return std::abs(value - 2.0f) - 1.0f;
    }

    static uint32_t advanceRandom(uint32_t& state) noexcept
    {
        state ^= state << 13u;
        state ^= state >> 17u;
        state ^= state << 5u;
        if (state == 0u) state = 0x9e3779b9u;
        return state;
    }

    void resetNodeState(NodeState& state, FeedbackPedalType pedal) noexcept
    {
        state.hilbertA.fill(0.0f);
        state.hilbertB.fill(0.0f);
        state.drive = {};
        state.noiseLowpass = 0.0f;
        state.sourceSignal = 0.0f;
        state.sourcePhase = 0.0f;
        state.returnDcInput = state.returnDcOutput = 0.0f;
        state.outputDcInput = state.outputDcOutput = 0.0f;
        state.phaseMemoryA = state.phaseMemoryB = 0.0f;
        state.bodyDelay.fill(0.0f);
        state.bodyWrite = 0u;
        state.loopLowpass = 0.0f;
        state.loopMemory = 0.0f;
        state.crushHeld = state.crushCounter = 0.0f;
        state.relayEnvelope = state.relayGate = state.relaySlew = 0.0f;
        state.energy = 0.0f;
        state.governor = 1.0f;
        state.filterIc1 = state.filterIc2 = 0.0f;
        state.held = 0.0f;
        state.holdRemaining = 0u;
        state.random = 0x9e3779b9u;
        state.transientFast = state.transientSlow = 0.0f;
        state.transientGate = 1.0f;
        std::fill(state.resonatorBuffer.begin(),
            state.resonatorBuffer.end(), 0.0f);
        state.resonatorWrite = 0u;
        state.resonatorDamped = 0.0f;
        state.erosionBuffer.fill(0.0f);
        state.erosionWrite = 0u;
        state.erosionPhase = state.erosionNoise = state.erosionTarget = 0.0f;
        state.foldPreviousInput = state.foldDcInput
            = state.foldDcOutput = 0.0f;
        std::fill(state.temporalBuffer.begin(),
            state.temporalBuffer.end(), 0.0f);
        state.temporalPhase = state.temporalFrames
            = state.temporalPosition = state.temporalPlayback = 0u;
        state.temporalRepeats = 1u;
        state.temporalLoopCount = 0u;
        state.temporalRead = 0.0;
        state.temporalDriftRatio = 1.0f;
        state.temporalDetectorFast = state.temporalDetectorSlow = 0.0f;
        state.temporalArmed = true;
        state.echo.reset();
        state.breakBus.reset();
        state.drumBus.reset();
        state.modulation = {};
        std::fill(state.modulationRuntime.timeBuffer.begin(),
            state.modulationRuntime.timeBuffer.end(), 0.0f);
        state.modulationRuntime.timeWrite = 0u;
        if (state.macroShred) state.macroShred->reset();
        if (state.macroPitch) state.macroPitch->reset();
        if (state.macroDelay) state.macroDelay->reset();
        if (state.fracture) state.fracture->reset();
        state.activePedal = pedal;
        state.pedalCrossfade = 1.0f;
        state.controlCounter = 0u;
    }

    static void clearNodeFaultState(NodeState& state) noexcept
    {
        state.hilbertA.fill(0.0f);
        state.hilbertB.fill(0.0f);
        state.drive = {};
        state.modulation = {};
        std::fill(state.modulationRuntime.timeBuffer.begin(),
            state.modulationRuntime.timeBuffer.end(), 0.0f);
        state.modulationRuntime.timeWrite = 0u;
        if (state.macroShred) state.macroShred->reset();
        if (state.macroPitch) state.macroPitch->reset();
        if (state.macroDelay) state.macroDelay->reset();
        if (state.fracture) state.fracture->reset();
        state.returnDcInput = state.returnDcOutput = 0.0f;
        state.outputDcInput = state.outputDcOutput = 0.0f;
        state.filterIc1 = state.filterIc2 = 0.0f;
        state.phaseMemoryA = state.phaseMemoryB = 0.0f;
        state.bodyDelay.fill(0.0f);
        state.bodyWrite = 0u;
        state.loopLowpass = 0.0f;
        state.loopMemory = 0.0f;
        state.temporalPhase = 0u;
        state.temporalDriftRatio = 1.0f;
        state.temporalArmed = true;
        state.energy = 0.0f;
        state.governor = 1.0f;
        state.pedalCrossfade = 0.0f;
    }

    uint32_t temporalWindowFrames(float amount,
        const NodeState& state) const noexcept
    {
        static constexpr std::array<float, 8u> milliseconds {{
            8.0f, 16.0f, 32.0f, 64.0f,
            125.0f, 250.0f, 500.0f, 1000.0f,
        }};
        const uint32_t index = static_cast<uint32_t>(std::clamp(
            std::lround(amount * 7.0f), 0l, 7l));
        const uint32_t frames = static_cast<uint32_t>(std::max(8.0,
            std::round(sampleRate_ * milliseconds[index] * 0.001)));
        return state.temporalBuffer.empty() ? 0u
            : std::min<uint32_t>(frames,
                static_cast<uint32_t>(state.temporalBuffer.size()));
    }

    bool temporalOnset(NodeState& state, float input,
        float sensitivity) noexcept
    {
        const float magnitude = std::abs(input);
        const float fastCoefficient = magnitude > state.temporalDetectorFast
            ? pedalFastAttackCoefficient_ : pedalFastReleaseCoefficient_;
        const float slowCoefficient = magnitude > state.temporalDetectorSlow
            ? pedalSlowAttackCoefficient_ : pedalSlowReleaseCoefficient_;
        state.temporalDetectorFast += (magnitude
            - state.temporalDetectorFast) * fastCoefficient;
        state.temporalDetectorSlow += (magnitude
            - state.temporalDetectorSlow) * slowCoefficient;
        const float delta = state.temporalDetectorFast
            - state.temporalDetectorSlow;
        const float threshold = 0.002f
            * std::pow(32.0f, 1.0f - sensitivity);
        if (delta < threshold * 0.28f) state.temporalArmed = true;
        if (state.temporalArmed && delta > threshold) {
            state.temporalArmed = false;
            return true;
        }
        return false;
    }

    static float readTemporal(const NodeState& state,
        double position) noexcept
    {
        if (state.temporalFrames < 2u || state.temporalBuffer.empty()) {
            return 0.0f;
        }
        const double length = static_cast<double>(state.temporalFrames);
        while (position < 0.0) position += length;
        while (position >= length) position -= length;
        const uint32_t first = static_cast<uint32_t>(position);
        const uint32_t second = (first + 1u) % state.temporalFrames;
        const float fraction = static_cast<float>(position - first);
        return state.temporalBuffer[first]
            + (state.temporalBuffer[second]
                - state.temporalBuffer[first]) * fraction;
    }

    float processTemporal(NodeState& state, float input, float modulation,
        const FeedbackShiftNodeParams& params, bool mangler) noexcept
    {
        const uint32_t captureFrames = temporalWindowFrames(
            params.pedalAmount, state);
        const float link = params.pedalExtra[4u] * 2.0f - 1.0f;
        const float linkedInput = std::clamp(input
            + modulation * link * 0.55f, -8.0f, 8.0f);
        const float detectorInput = std::clamp(input
            + modulation * link * 0.72f, -8.0f, 8.0f);
        const float sensitivity = 0.12f + params.pedalExtra[1u] * 0.80f;
        const bool onset = temporalOnset(state, detectorInput, sensitivity);
        if (state.temporalPhase == 0u && onset && captureFrames > 1u) {
            state.temporalPhase = 1u;
            state.temporalFrames = captureFrames;
            state.temporalPosition = 0u;
            state.temporalPlayback = 0u;
            state.temporalLoopCount = 0u;
            state.temporalRepeats = mangler ? 1u
                : 1u + static_cast<uint32_t>(
                    std::lround(params.pedalTone * 15.0f));
            const float randomUnit = static_cast<float>(
                advanceRandom(state.random) & 0x00ffffffu) / 16777215.0f;
            const float signedDrift = params.pedalExtra[3u] * 2.0f - 1.0f;
            const float captureDriftSemitones = signedDrift
                * (0.35f + randomUnit * 0.65f) * 3.0f;
            state.temporalDriftRatio = std::pow(2.0f,
                captureDriftSemitones / 12.0f);
        }
        if (state.temporalPhase == 1u) {
            state.temporalBuffer[state.temporalPosition++] = linkedInput;
            if (state.temporalPosition >= state.temporalFrames) {
                state.temporalPhase = 2u;
                state.temporalPlayback = 0u;
                state.temporalLoopCount = 0u;
                const uint32_t mode = static_cast<uint32_t>(std::lround(
                    params.pedalExtra[0u] * 2.0f));
                state.temporalRead = (!mangler && mode == 1u)
                        || (mangler && mode == 0u)
                    ? static_cast<double>(state.temporalFrames - 1u) : 0.0;
            }
            return input;
        }
        if (state.temporalPhase != 2u || state.temporalFrames < 2u) {
            return input;
        }

        const uint32_t mode = static_cast<uint32_t>(std::lround(
            params.pedalExtra[0u] * 2.0f));
        const uint32_t repeat = state.temporalLoopCount;

        float wet = readTemporal(state, state.temporalRead);
        const float crossfadeMs = 0.15f * std::pow(80.0f,
            params.pedalExtra[2u]);
        const uint32_t edge = std::clamp<uint32_t>(
            static_cast<uint32_t>(std::lround(
                sampleRate_ * crossfadeMs * 0.001)), 2u,
            std::max<uint32_t>(2u, state.temporalFrames / 3u));
        const double length = static_cast<double>(state.temporalFrames);
        double wrappedRead = std::fmod(state.temporalRead, length);
        if (wrappedRead < 0.0) wrappedRead += length;
        const float edgeDistance = static_cast<float>(std::min(
            wrappedRead, length - wrappedRead));
        const float linearFade = std::clamp(edgeDistance
            / static_cast<float>(edge), 0.0f, 1.0f);
        const float edgeFade = std::sin(linearFade * kPi * 0.5f);
        const float transitionInput = std::clamp(input
            + modulation * link * 0.20f, -8.0f, 8.0f);
        wet = transitionInput + (wet - transitionInput)
            * edgeFade * edgeFade;

        if (!mangler) {
            const float decay = (params.pedalBias + 1.0f) * 0.5f;
            wet *= std::pow(std::max(0.08f, 1.0f - decay * 0.16f),
                static_cast<float>(repeat));
            const uint32_t direction = mode;
            const bool reverse = direction == 1u
                || (direction == 2u && (repeat & 1u) != 0u);
            state.temporalRead += (reverse ? -1.0 : 1.0)
                * state.temporalDriftRatio;
            ++state.temporalPlayback;
            const bool crossed = reverse ? state.temporalRead < 0.0
                : state.temporalRead >= length;
            if (crossed) {
                ++state.temporalLoopCount;
                if (state.temporalLoopCount >= state.temporalRepeats) {
                    state.temporalPhase = 0u;
                } else {
                    const bool nextReverse = direction == 1u
                        || (direction == 2u
                            && (state.temporalLoopCount & 1u) != 0u);
                    if (nextReverse != reverse) {
                        state.temporalRead = nextReverse
                            ? length - 1.0 : 0.0;
                    } else if (state.temporalRead < 0.0) {
                        state.temporalRead += length;
                    } else if (state.temporalRead >= length) {
                        state.temporalRead -= length;
                    }
                }
            }
        } else {
            const float pitch = std::pow(2.0f,
                (params.pedalTone * 2.0f - 1.0f) * 24.0f / 12.0f)
                * state.temporalDriftRatio;
            const float behavior = (params.pedalBias + 1.0f) * 0.5f;
            if (mode == 0u) {
                state.temporalRead -= pitch;
                ++state.temporalPlayback;
                if (state.temporalRead < 0.0) state.temporalPhase = 0u;
            } else if (mode == 1u) {
                const uint32_t decayFrames = state.temporalFrames
                    * (2u + static_cast<uint32_t>(
                        std::lround(behavior * 14.0f)));
                wet *= std::max(0.0f, 1.0f
                    - static_cast<float>(state.temporalPlayback)
                        / std::max(2u, decayFrames));
                state.temporalRead += pitch;
                ++state.temporalPlayback;
                if (state.temporalPlayback >= decayFrames) {
                    state.temporalPhase = 0u;
                }
            } else {
                const float brake = 0.99998f
                    - 0.00042f * behavior;
                const float speed = pitch * std::pow(brake,
                    static_cast<float>(state.temporalPlayback));
                state.temporalRead += speed;
                ++state.temporalPlayback;
                if (state.temporalRead >= state.temporalFrames - 1u
                    || speed < 0.002f) state.temporalPhase = 0u;
            }
        }
        return flush(wet);
    }

    float randomBipolar() noexcept
    {
        noiseState_ ^= noiseState_ << 13u;
        noiseState_ ^= noiseState_ >> 17u;
        noiseState_ ^= noiseState_ << 5u;
        if (noiseState_ == 0u) noiseState_ = 0x6d2b79f5u;
        return static_cast<float>(noiseState_ & 0x00ffffffu)
            / 8388607.5f - 1.0f;
    }

    static float randomUnit(uint32_t& state) noexcept
    {
        return static_cast<float>(advanceRandom(state) & 0x00ffffffu)
            / 16777215.0f;
    }

    void smoothParameters() noexcept
    {
        const auto smooth = [this](float current, float target) noexcept {
            return current + (target - current) * parameterCoefficient_;
        };
        smoothed_.excite = smooth(smoothed_.excite, target_.excite);
        smoothed_.drift = smooth(smoothed_.drift, target_.drift);
        smoothed_.spliceAmount = smooth(smoothed_.spliceAmount,
            target_.spliceAmount);
        smoothed_.spliceRate = smooth(smoothed_.spliceRate,
            target_.spliceRate);
        smoothed_.spliceRateFine = smooth(smoothed_.spliceRateFine,
            target_.spliceRateFine);
        smoothed_.spliceContrast = smooth(smoothed_.spliceContrast,
            target_.spliceContrast);
        smoothed_.spliceSpace = smooth(smoothed_.spliceSpace,
            target_.spliceSpace);
        smoothed_.subBassTune = smooth(smoothed_.subBassTune,
            target_.subBassTune);
        smoothed_.subBassShape = smooth(smoothed_.subBassShape,
            target_.subBassShape);
        smoothed_.subBassDrive = smooth(smoothed_.subBassDrive,
            target_.subBassDrive);
        smoothed_.subBassDecay = smooth(smoothed_.subBassDecay,
            target_.subBassDecay);
        smoothed_.subBassSustain = smooth(smoothed_.subBassSustain,
            target_.subBassSustain);
        smoothed_.morph = smooth(smoothed_.morph, target_.morph);
        smoothed_.morphDepth = smooth(smoothed_.morphDepth,
            target_.morphDepth);
        smoothed_.morphRate = smooth(smoothed_.morphRate,
            target_.morphRate);
        smoothed_.morphInertia = smooth(smoothed_.morphInertia,
            target_.morphInertia);
        smoothed_.governorReflex = smooth(smoothed_.governorReflex,
            target_.governorReflex);
        smoothed_.governorSensitivity = smooth(
            smoothed_.governorSensitivity, target_.governorSensitivity);
        smoothed_.governorRecovery = smooth(smoothed_.governorRecovery,
            target_.governorRecovery);
        smoothed_.governorRest = smooth(smoothed_.governorRest,
            target_.governorRest);
        governorReleaseCoefficient_ = onePoleCoefficient(
            100.0f * std::pow(50.0f, smoothed_.governorRecovery),
            static_cast<float>(sampleRate_));
        smoothed_.outputGainDb = smooth(smoothed_.outputGainDb,
            target_.outputGainDb);
        smoothed_.outputRotationDeg = smooth(smoothed_.outputRotationDeg,
            target_.outputRotationDeg);
        smoothed_.auxPress = smooth(smoothed_.auxPress, target_.auxPress);
        smoothed_.auxSaturation = smooth(smoothed_.auxSaturation,
            target_.auxSaturation);
        smoothed_.auxFold = smooth(smoothed_.auxFold, target_.auxFold);
        smoothed_.auxClip = smooth(smoothed_.auxClip, target_.auxClip);
        smoothed_.auxGrainSize = smooth(smoothed_.auxGrainSize,
            target_.auxGrainSize);
        smoothed_.auxGrainDensity = smooth(smoothed_.auxGrainDensity,
            target_.auxGrainDensity);
        smoothed_.auxGrainScatter = smooth(smoothed_.auxGrainScatter,
            target_.auxGrainScatter);
        smoothed_.auxGrainPitch = smooth(smoothed_.auxGrainPitch,
            target_.auxGrainPitch);
        smoothed_.auxGrainEdge = smooth(smoothed_.auxGrainEdge,
            target_.auxGrainEdge);
        smoothed_.auxGrainCoherence = smooth(smoothed_.auxGrainCoherence,
            target_.auxGrainCoherence);
        smoothed_.auxGrainLaneDrift = smooth(smoothed_.auxGrainLaneDrift,
            target_.auxGrainLaneDrift);
        smoothed_.auxGrainMix = smooth(smoothed_.auxGrainMix,
            target_.auxGrainMix);
        smoothed_.auxGrainSpacing = smooth(smoothed_.auxGrainSpacing,
            target_.auxGrainSpacing);
        smoothed_.auxTilt = smooth(smoothed_.auxTilt, target_.auxTilt);
        smoothed_.auxMix = smooth(smoothed_.auxMix, target_.auxMix);
        for (uint32_t node = 0u;
             node < kFeedbackShiftChannels; ++node) {
            smoothed_.auxSend[node] = smooth(smoothed_.auxSend[node],
                target_.auxSend[node]);
        }
        smoothed_.outputMode = target_.outputMode;
        smoothed_.morphSource = target_.morphSource;
        smoothed_.morphHold = target_.morphHold;
        smoothed_.morphSync = target_.morphSync;
        smoothed_.morphDivision = target_.morphDivision;
        smoothed_.morphShape = target_.morphShape;
        smoothed_.auxGrainShape = target_.auxGrainShape;
        smoothed_.run = target_.run;
        for (uint32_t node = 0u; node < kFeedbackShiftChannels; ++node) {
            auto& current = smoothed_.nodes[node];
            const auto& target = target_.nodes[node];
            auto& currentB = smoothed_.sceneBNodes[node];
            const auto& targetB = target_.sceneBNodes[node];
            current.frequencyHz = smooth(current.frequencyHz,
                target.frequencyHz);
            currentB.frequencyHz = smooth(currentB.frequencyHz,
                targetB.frequencyHz);
            current.exciterGainDb = smooth(current.exciterGainDb,
                target.exciterGainDb);
            current.regeneration = smooth(current.regeneration,
                target.regeneration);
            currentB.regeneration = smooth(currentB.regeneration,
                targetB.regeneration);
            current.color = smooth(current.color, target.color);
            currentB.color = smooth(currentB.color, targetB.color);
            current.body = smooth(current.body, target.body);
            currentB.body = smooth(currentB.body, targetB.body);
            current.levelDb = smooth(current.levelDb, target.levelDb);
            currentB.levelDb = smooth(currentB.levelDb, targetB.levelDb);
            current.pedalAmount = smooth(current.pedalAmount,
                target.pedalAmount);
            current.pedalTone = smooth(current.pedalTone,
                target.pedalTone);
            current.pedalBias = smooth(current.pedalBias,
                target.pedalBias);
            current.pedalMix = smooth(current.pedalMix,
                target.pedalMix);
            for (uint32_t extra = 0u;
                 extra < kFeedbackPedalExtraParameterCount; ++extra) {
                current.pedalExtra[extra] = smooth(current.pedalExtra[extra],
                    target.pedalExtra[extra]);
            }
            current.mode = target.mode;
            current.pedal = target.pedal;
            current.exciterSource = target.exciterSource;
            currentB.mode = target.mode;
            currentB.pedal = target.pedal;
            currentB.exciterSource = target.exciterSource;
        }
        for (uint32_t index = 0u; index < kFeedbackShiftMatrixCells;
             ++index) {
            smoothed_.matrix[index] = smooth(smoothed_.matrix[index],
                target_.matrix[index]);
            smoothed_.sceneBMatrix[index] = smooth(
                smoothed_.sceneBMatrix[index], target_.sceneBMatrix[index]);
        }
        for (uint32_t node = 0u; node < kFeedbackShiftChannels; ++node) {
            smoothed_.sceneBAuxSend[node] = smooth(
                smoothed_.sceneBAuxSend[node], target_.sceneBAuxSend[node]);
        }
        exciteGain_ = 0.000035f * std::pow(1500.0f, smoothed_.excite);
        outputGain_ = dbGain(smoothed_.outputGainDb);
    }

    void advanceMorph(const float* externalInput) noexcept
    {
        // Every lane owns the same fast/slow ecology detector used by its
        // REST governor. The morph drivers are the maxima of those detectors,
        // so the visible global motion is a summary of the actual lane
        // ecology instead of a second, unrelated envelope follower.
        const float slowMs = 45.0f + 155.0f
            * smoothed_.governorRecovery;
        const float slowCoefficient = onePoleCoefficient(slowMs,
            static_cast<float>(sampleRate_));
        const float edgeScale = 1.0f / (0.018f + 0.12f
            * (1.0f - smoothed_.governorSensitivity));
        float maximumEnvelope = 0.0f;
        float maximumEdge = 0.0f;
        for (uint32_t lane = 0u; lane < kFeedbackShiftChannels; ++lane) {
            const float external = externalInput
                ? std::abs(externalInput[lane]) : 0.0f;
            const float peak = std::min(2.0f,
                std::max(external, std::abs(ecologyReturns_[lane])));
            const float envelopeCoefficient = peak > laneEnvelopes_[lane]
                ? detectorAttackCoefficient_ : detectorReleaseCoefficient_;
            laneEnvelopes_[lane] += (peak - laneEnvelopes_[lane])
                * envelopeCoefficient;
            laneEnvelopes_[lane] = flush(laneEnvelopes_[lane]);
            laneSlowEnvelopes_[lane] += (laneEnvelopes_[lane]
                    - laneSlowEnvelopes_[lane]) * slowCoefficient;
            laneSlowEnvelopes_[lane] = flush(laneSlowEnvelopes_[lane]);
            const float edgeTarget = std::clamp((laneEnvelopes_[lane]
                    - laneSlowEnvelopes_[lane]) * edgeScale,
                0.0f, 1.0f);
            const float edgeCoefficient = edgeTarget > laneEdges_[lane]
                ? detectorAttackCoefficient_ : governorReleaseCoefficient_;
            laneEdges_[lane] += (edgeTarget - laneEdges_[lane])
                * edgeCoefficient;
            laneEdges_[lane] = std::clamp(
                flush(laneEdges_[lane]), 0.0f, 1.0f);
            maximumEnvelope = std::max(maximumEnvelope,
                laneEnvelopes_[lane]);
            maximumEdge = std::max(maximumEdge, laneEdges_[lane]);
        }
        morphInputEnvelope_ = maximumEnvelope;
        morphEdge_ = maximumEdge;
        if (smoothed_.morphHold) return;

        float rateHz = 0.01f * std::pow(800.0f, smoothed_.morphRate);
        if (smoothed_.morphSource == FeedbackMorphSource::Pulse
            && smoothed_.morphSync != 0u && transportHasTempo_) {
            const float beats = kFeedbackPulseDivisionBeats[
                smoothed_.morphDivision];
            rateHz = static_cast<float>(transportTempoBpm_)
                / (60.0f * std::max(0.001f, beats));
        }
        const float previousPhase = morphPhase_;
        morphPhase_ += rateHz / static_cast<float>(sampleRate_);
        morphPhase_ -= std::floor(morphPhase_);
        if (morphPhase_ < previousPhase) {
            morphRandom_ = randomBipolar() * 0.5f + 0.5f;
            morphChaosTarget_ = randomBipolar() * 0.5f + 0.5f;
        }
        const float chaosCoefficient = std::clamp(
            rateHz * 3.0f / static_cast<float>(sampleRate_),
            0.000001f, 0.02f);
        morphChaosValue_ += (morphChaosTarget_ - morphChaosValue_)
            * chaosCoefficient;

        float driver = smoothed_.morph;
        switch (smoothed_.morphSource) {
        case FeedbackMorphSource::Manual:
            driver = smoothed_.morph; break;
        case FeedbackMorphSource::Envelope:
            driver = std::clamp((morphInputEnvelope_
                    - governorThreshold(smoothed_.governorSensitivity)
                        * 0.25f) * 2.5f,
                0.0f, 1.0f);
            break;
        case FeedbackMorphSource::Edge:
            driver = morphEdge_;
            break;
        case FeedbackMorphSource::Lfo:
            driver = 0.5f - 0.5f * std::cos(morphPhase_ * kTwoPi); break;
        case FeedbackMorphSource::Chaos:
            driver = morphChaosValue_; break;
        case FeedbackMorphSource::Pulse:
            switch (smoothed_.morphShape) {
            case FeedbackPulseShape::Sine:
                driver = 0.5f - 0.5f
                    * std::cos(morphPhase_ * kTwoPi); break;
            case FeedbackPulseShape::Square:
                driver = morphPhase_ < 0.5f ? 1.0f : 0.0f; break;
            case FeedbackPulseShape::Ramp:
                driver = 1.0f - morphPhase_; break;
            case FeedbackPulseShape::Random:
                driver = morphRandom_; break;
            case FeedbackPulseShape::Count:
                break;
            }
            break;
        case FeedbackMorphSource::Count:
            break;
        }
        morphDriverValue_ = std::clamp(driver, 0.0f, 1.0f);
        const float target = smoothed_.morphSource
                == FeedbackMorphSource::Manual
            ? smoothed_.morph
            : smoothed_.morph + (morphDriverValue_ - smoothed_.morph)
                * smoothed_.morphDepth;
        const float inertiaMs = 2.0f * std::pow(
            2000.0f, smoothed_.morphInertia);
        const float coefficient = onePoleCoefficient(inertiaMs,
            static_cast<float>(sampleRate_));
        morphValue_ += (std::clamp(target, 0.0f, 1.0f) - morphValue_)
            * coefficient;
        morphValue_ = std::clamp(flush(morphValue_), 0.0f, 1.0f);
    }

    float spliceFrequency(float base, float contrast) noexcept
    {
        const float family = randomUnit(spliceRandom_);
        const float unit = randomUnit(spliceRandom_);
        const float sign = randomUnit(spliceRandom_) < 0.5f ? -1.0f : 1.0f;
        float radical = 0.0f;
        if (family < 0.22f) {
            radical = sign * 0.02f * std::pow(80.0f, unit);
        } else if (family < 0.46f) {
            radical = sign * 1.5f * std::pow(30.0f, unit);
        } else if (family < 0.82f) {
            radical = sign * 45.0f * std::pow(24.0f, unit);
        } else {
            radical = sign * (1000.0f + unit * 5000.0f);
        }
        return std::clamp(lerp(base, radical, contrast),
            -6000.0f, 6000.0f);
    }

    SplicePatch makeSplicePatch(uint32_t lane,
        const FeedbackShiftNodeParams& base,
        const std::array<float, kFeedbackShiftMatrixCells>& matrix) noexcept
    {
        SplicePatch patch;
        const float contrast = smoothed_.spliceContrast;
        patch.frequencyHz = spliceFrequency(base.frequencyHz, contrast);
        const float radicalRegen = randomUnit(spliceRandom_) < 0.18f
            ? 0.88f + randomUnit(spliceRandom_) * 0.11f
            : 0.06f + randomUnit(spliceRandom_) * 0.88f;
        patch.regeneration = std::clamp(lerp(base.regeneration,
            radicalRegen, contrast), 0.0f, 1.0f);
        patch.color = std::clamp(lerp(base.color,
            randomUnit(spliceRandom_) * 2.0f - 1.0f, contrast),
            -1.0f, 1.0f);
        const float bodyUnit = randomUnit(spliceRandom_);
        const float radicalBody = randomUnit(spliceRandom_) < 0.5f
            ? bodyUnit * bodyUnit * bodyUnit
            : std::pow(bodyUnit, 0.35f);
        patch.body = std::clamp(lerp(base.body, radicalBody, contrast),
            0.0f, 1.0f);
        const float radicalLevel = -24.0f
            + randomUnit(spliceRandom_) * 27.0f;
        patch.levelDb = std::clamp(lerp(base.levelDb, radicalLevel,
            contrast * 0.72f), -48.0f, 3.0f);
        patch.auxSend = std::clamp(lerp(effectiveAuxSend_[lane],
            randomUnit(spliceRandom_), contrast), 0.0f, 1.0f);
        patch.mode = contrast > 0.32f
                && randomUnit(spliceRandom_) < contrast * 0.48f
            ? (randomUnit(spliceRandom_) < 0.5f
                ? FeedbackShiftMode::Frequency : FeedbackShiftMode::Ring)
            : base.mode;

        std::array<float, kFeedbackShiftChannels> radicalRoutes {};
        const uint32_t routeCount = 1u + static_cast<uint32_t>(
            randomUnit(spliceRandom_) * (1.0f + contrast * 3.0f));
        for (uint32_t route = 0u; route < routeCount; ++route) {
            const uint32_t source = static_cast<uint32_t>(
                randomUnit(spliceRandom_) * kFeedbackShiftChannels)
                % kFeedbackShiftChannels;
            const float sign = randomUnit(spliceRandom_) < 0.5f
                ? -1.0f : 1.0f;
            radicalRoutes[source] = sign * (0.10f
                + 0.88f * std::pow(randomUnit(spliceRandom_), 0.72f));
        }
        for (uint32_t source = 0u; source < kFeedbackShiftChannels;
             ++source) {
            const uint32_t index = lane * kFeedbackShiftChannels + source;
            patch.routes[source] = std::clamp(lerp(matrix[index],
                radicalRoutes[source], contrast), -1.0f, 1.0f);
        }

        float silenceProbability = std::pow(smoothed_.spliceSpace, 1.25f)
            * (0.18f + contrast * 0.66f);
        // An onset calls the lane back into the ecology; quiet persistence is
        // where longer holes are allowed to form.
        silenceProbability *= 1.0f - laneEdges_[lane] * 0.78f;
        if (spliceGains_[lane] < 0.01f) silenceProbability *= 0.35f;
        patch.audible = smoothed_.spliceSpace <= 1.0e-4f
            || randomUnit(spliceRandom_) >= silenceProbability;
        return patch;
    }

    void scheduleNextSplice(uint32_t lane, bool audible) noexcept
    {
        float rateHz = feedbackSpliceRateHz(smoothed_.spliceRate,
            smoothed_.spliceRateFine);
        const float threshold = governorThreshold(
            smoothed_.governorSensitivity) * 0.40f;
        const float pressure = std::clamp((laneEnvelopes_[lane] - threshold)
                / std::max(0.04f, threshold),
            0.0f, 1.0f);
        rateHz *= 1.0f + laneEdges_[lane]
                * (3.0f + smoothed_.spliceAmount * 9.0f)
            + pressure * 2.5f;
        float interval = static_cast<float>(sampleRate_)
            / std::max(0.1f, rateHz);
        const float jitter = randomUnit(spliceRandom_) * 2.0f - 1.0f;
        interval *= std::pow(4.0f,
            jitter * smoothed_.spliceContrast * 0.92f);
        if (spliceFlurryRemaining_[lane] > 0u) {
            --spliceFlurryRemaining_[lane];
            interval *= 0.12f + randomUnit(spliceRandom_) * 0.26f;
        } else if (laneEdges_[lane] > 0.08f
            && randomUnit(spliceRandom_) < laneEdges_[lane]
                * (0.20f + smoothed_.spliceAmount * 0.72f)) {
            spliceFlurryRemaining_[lane] = 2u + static_cast<uint32_t>(
                randomUnit(spliceRandom_) * (3.0f
                    + smoothed_.spliceContrast * 9.0f));
        }
        if (!audible) {
            interval *= 1.0f + smoothed_.spliceSpace
                * (2.0f + randomUnit(spliceRandom_) * 10.0f);
        }
        spliceCountdowns_[lane] = std::max<uint32_t>(1u,
            static_cast<uint32_t>(std::lround(interval)));
    }

    void commitSplice(uint32_t lane) noexcept
    {
        splicePatches_[lane] = splicePendingPatches_[lane];
        splicePatchValid_[lane] = true;
        spliceGains_[lane] = 0.0f;
        if (splicePatches_[lane].audible) {
            spliceStages_[lane] = SpliceStage::FadeIn;
        } else {
            spliceStages_[lane] = SpliceStage::Waiting;
        }
        scheduleNextSplice(lane, splicePatches_[lane].audible);
    }

    void beginSplice(uint32_t lane, const FeedbackShiftNodeParams& base,
        const std::array<float, kFeedbackShiftMatrixCells>& matrix) noexcept
    {
        splicePendingPatches_[lane] = makeSplicePatch(lane, base, matrix);
        const float seamMs = 0.18f + 1.42f
            * std::pow(1.0f - smoothed_.spliceContrast, 2.0f);
        const float seamFrames = std::max(2.0f,
            static_cast<float>(sampleRate_) * seamMs * 0.001f);
        spliceFadeSteps_[lane] = 1.0f / seamFrames;
        splicePulses_[lane] = 1.0f;
        ++spliceEventCount_;
        if (spliceGains_[lane] <= 1.0e-4f) {
            commitSplice(lane);
        } else {
            spliceStages_[lane] = SpliceStage::FadeOut;
        }
    }

    void advanceSpliceEcology(
        std::array<FeedbackShiftNodeParams, kFeedbackShiftChannels>& nodes,
        std::array<float, kFeedbackShiftMatrixCells>& matrix) noexcept
    {
        const float amount = smoothed_.spliceAmount;
        for (uint32_t lane = 0u; lane < kFeedbackShiftChannels; ++lane) {
            splicePulses_[lane] *= splicePulseDecay_;
            if (splicePulses_[lane] < 1.0e-5f) splicePulses_[lane] = 0.0f;
            if (amount <= 1.0e-4f) {
                spliceGains_[lane] += (1.0f - spliceGains_[lane])
                    * sourceSwitchCoefficient_;
                spliceStages_[lane] = SpliceStage::Waiting;
                splicePatchValid_[lane] = false;
                continue;
            }

            switch (spliceStages_[lane]) {
            case SpliceStage::Waiting:
                if (spliceCountdowns_[lane] > 0u) {
                    --spliceCountdowns_[lane];
                } else {
                    beginSplice(lane, nodes[lane], matrix);
                }
                break;
            case SpliceStage::FadeOut:
                spliceGains_[lane] = std::max(0.0f,
                    spliceGains_[lane] - spliceFadeSteps_[lane]);
                if (spliceGains_[lane] <= 0.0f) commitSplice(lane);
                break;
            case SpliceStage::FadeIn:
                spliceGains_[lane] = std::min(1.0f,
                    spliceGains_[lane] + spliceFadeSteps_[lane]);
                if (spliceGains_[lane] >= 1.0f) {
                    spliceStages_[lane] = SpliceStage::Waiting;
                }
                break;
            }

            if (!splicePatchValid_[lane]) continue;
            const auto& patch = splicePatches_[lane];
            auto& node = nodes[lane];
            node.frequencyHz = lerp(node.frequencyHz,
                patch.frequencyHz, amount);
            node.regeneration = lerp(node.regeneration,
                patch.regeneration, amount);
            node.color = lerp(node.color, patch.color, amount);
            node.body = lerp(node.body, patch.body, amount);
            node.levelDb = lerp(node.levelDb, patch.levelDb, amount);
            if (amount * smoothed_.spliceContrast > 0.32f) {
                node.mode = patch.mode;
            }
            effectiveAuxSend_[lane] = lerp(effectiveAuxSend_[lane],
                patch.auxSend, amount);
            for (uint32_t source = 0u; source < kFeedbackShiftChannels;
                 ++source) {
                const uint32_t index = lane * kFeedbackShiftChannels + source;
                matrix[index] = lerp(matrix[index],
                    patch.routes[source], amount);
            }
        }
    }

    void scheduleNextRest(uint32_t lane) noexcept
    {
        if (lane >= kFeedbackShiftChannels) return;
        const float rest = std::clamp(smoothed_.governorRest, 0.0f, 1.0f);
        const float randomScale = 0.68f
            + randomUnit(restRandom_) * 0.78f;
        static constexpr std::array<float, kFeedbackShiftChannels>
            laneScales {{ 0.91f, 1.12f, 0.83f, 1.27f,
                          1.04f, 0.77f, 1.19f, 0.96f }};
        const float activeSeconds = (8.0f - rest * 7.15f)
            * randomScale * laneScales[lane];
        restTriggerFrames_[lane] = static_cast<uint32_t>(std::max(1.0,
            sampleRate_ * static_cast<double>(activeSeconds)));
    }

    void advanceRestGovernors() noexcept
    {
        const float rest = smoothed_.governorRest;
        const float threshold = governorThreshold(
            smoothed_.governorSensitivity) * 0.38f;
        const float downCoefficient = onePoleCoefficient(
            6.0f + 44.0f * (1.0f - rest),
            static_cast<float>(sampleRate_));
        const float upCoefficient = onePoleCoefficient(
            45.0f + 900.0f * smoothed_.governorRecovery,
            static_cast<float>(sampleRate_));
        for (uint32_t lane = 0u; lane < kFeedbackShiftChannels; ++lane) {
            float target = 1.0f;
            if (rest <= 1.0e-4f) {
                restPersistenceFrames_[lane] = 0.0f;
                restRemainingFrames_[lane] = 0u;
            } else if (restRemainingFrames_[lane] > 0u) {
                --restRemainingFrames_[lane];
                target = 0.0f;
                if (restRemainingFrames_[lane] == 0u) {
                    scheduleNextRest(lane);
                }
            } else {
                const float envelopePressure = std::clamp(
                    (laneEnvelopes_[lane] - threshold)
                        / std::max(0.05f, threshold),
                    0.0f, 1.0f);
                const uint32_t previous = (lane
                        + kFeedbackShiftChannels - 1u)
                    % kFeedbackShiftChannels;
                const uint32_t next = (lane + 1u)
                    % kFeedbackShiftChannels;
                const float neighborEdge = std::max(
                    laneEdges_[previous], laneEdges_[next]);
                if (laneEnvelopes_[lane] > threshold) {
                    // The lane's edge accelerates its own rest decision;
                    // adjacent edges add light ecological pressure without
                    // collapsing the ring into one global gate.
                    restPersistenceFrames_[lane] += 0.45f
                        + envelopePressure * 1.35f
                        + laneEdges_[lane] * 8.0f
                        + neighborEdge * 1.5f;
                } else {
                    restPersistenceFrames_[lane] = std::max(0.0f,
                        restPersistenceFrames_[lane] - 2.5f);
                }
                if (restPersistenceFrames_[lane] >= static_cast<float>(
                        restTriggerFrames_[lane])) {
                    const float randomScale = 0.64f
                        + randomUnit(restRandom_) * 0.92f;
                    const float silentSeconds = (0.055f
                            + rest * rest * 3.35f) * randomScale;
                    restRemainingFrames_[lane] = static_cast<uint32_t>(
                        std::max(1.0, sampleRate_
                            * static_cast<double>(silentSeconds)));
                    restPersistenceFrames_[lane] = 0.0f;
                    target = 0.0f;
                }
            }

            const float coefficient = target < restGains_[lane]
                ? downCoefficient : upCoefficient;
            restGains_[lane] += (target - restGains_[lane]) * coefficient;
            if (target == 0.0f && restGains_[lane] < 1.0e-5f) {
                restGains_[lane] = 0.0f;
            }
            restGains_[lane] = std::clamp(
                flush(restGains_[lane]), 0.0f, 1.0f);
        }
    }

    void advanceDrift() noexcept
    {
        for (uint32_t node = 0u; node < kFeedbackShiftChannels; ++node) {
            if (driftCountdowns_[node] == 0u) {
                driftTargets_[node] = randomUnit(driftRandom_) * 2.0f - 1.0f;
                const float randomSeconds = 0.65f
                    + randomUnit(driftRandom_) * 6.75f;
                driftCountdowns_[node] = static_cast<uint32_t>(std::max(
                    1.0, sampleRate_ * static_cast<double>(randomSeconds)));
            } else {
                --driftCountdowns_[node];
            }
            driftValues_[node] += (driftTargets_[node]
                    - driftValues_[node]) * driftSlewCoefficient_;
            driftValues_[node] = flush(driftValues_[node]);
        }
    }

    float processExciter(uint32_t node, float externalInput,
        float noise, const FeedbackShiftNodeParams& params) noexcept
    {
        auto& state = states_[node];
        const float hit = burstEnvelope_[node] * burstEnvelope_[node];
        static constexpr std::array<float, kFeedbackShiftChannels>
            subDetune {{
                0.9970f, 1.0024f, 0.9952f, 1.0041f,
                0.9983f, 1.0011f, 0.9964f, 1.0032f,
            }};
        const float sourceFrequency = params.exciterSource
                == FeedbackExciterSource::SubBass
            ? feedbackSubBassFrequencyHz(smoothed_.subBassTune)
                * subDetune[node]
            : std::clamp(std::abs(params.frequencyHz), 18.0f, 4000.0f);
        state.sourcePhase += kTwoPi * sourceFrequency
            / static_cast<float>(sampleRate_);
        state.sourcePhase -= kTwoPi * std::floor(
            state.sourcePhase / kTwoPi);
        const float tone = std::sin(state.sourcePhase);
        if (node == 0u && (subBassControlCounter_++ & 63u) == 0u) {
            const float decaySeconds = feedbackSubBassDecayMs(
                smoothed_.subBassDecay) * 0.001f;
            subBassDecayCoefficient_ = std::exp(-1.0f
                / std::max(1.0f, static_cast<float>(sampleRate_)
                    * decaySeconds));
        }
        subBassEnvelope_[node] *= subBassDecayCoefficient_;
        if (subBassEnvelope_[node] < 1.0e-7f) {
            subBassEnvelope_[node] = 0.0f;
        }
        const float triangle = 2.0f / kPi * std::asin(tone);
        const float squareDrive = 2.0f
            + smoothed_.subBassShape * smoothed_.subBassShape * 22.0f;
        const float softSquare = std::tanh(tone * squareDrive)
            / std::tanh(squareDrive);
        const float shapedSub = smoothed_.subBassShape < 0.5f
            ? lerp(tone, triangle, smoothed_.subBassShape * 2.0f)
            : lerp(triangle, softSquare,
                (smoothed_.subBassShape - 0.5f) * 2.0f);
        const float subDrive = 1.0f
            + smoothed_.subBassDrive * smoothed_.subBassDrive * 15.0f;
        const float sub = std::tanh(shapedSub * subDrive)
            / std::tanh(subDrive);
        const float subEnvelope = std::max(subBassEnvelope_[node],
            smoothed_.subBassSustain);
        float source = 0.0f;
        switch (params.exciterSource) {
        case FeedbackExciterSource::NoiseHit:
            source = noise * (exciteGain_ + hit * 0.44f); break;
        case FeedbackExciterSource::Noise:
            source = noise * exciteGain_; break;
        case FeedbackExciterSource::Hit:
            source = noise * hit * 0.44f; break;
        case FeedbackExciterSource::Tone:
            source = tone * (exciteGain_ + hit * 0.32f); break;
        case FeedbackExciterSource::SubBass:
            source = sub * subEnvelope; break;
        case FeedbackExciterSource::External:
            source = externalInput; break;
        case FeedbackExciterSource::ExternalHit:
            source = externalInput * hit; break;
        case FeedbackExciterSource::Off:
        case FeedbackExciterSource::Count:
            source = 0.0f; break;
        }
        source = std::clamp(source * dbGain(params.exciterGainDb),
            -8.0f, 8.0f);
        state.sourceSignal += (source - state.sourceSignal)
            * sourceSwitchCoefficient_;
        return flush(state.sourceSignal);
    }

    float processShift(uint32_t node, float input,
        const FeedbackShiftNodeParams& params) noexcept
    {
        auto& state = states_[node];
        const float nodeDrift = driftValues_[node]
            * smoothed_.drift * smoothed_.drift * 420.0f;
        const float frequency = std::clamp(params.frequencyHz + nodeDrift,
            -6000.0f, 6000.0f);
        const float sine = std::sin(phases_[node]);
        const float cosine = std::cos(phases_[node]);
        static constexpr std::array<float, kFeedbackShiftChannels>
            bodyRatios {{ 0.83f, 1.17f, 0.91f, 1.29f,
                          1.07f, 1.37f, 0.97f, 1.21f }};
        const float delayMs = (0.035f + params.body * params.body * 3.80f)
            * bodyRatios[node];
        const float delayFrames = std::clamp(delayMs * 0.001f
                * static_cast<float>(sampleRate_),
            1.0f, static_cast<float>(kBodyDelayFrames - 2u));
        float read = static_cast<float>(state.bodyWrite) - delayFrames;
        while (read < 0.0f) read += static_cast<float>(kBodyDelayFrames);
        const uint32_t first = static_cast<uint32_t>(read)
            % kBodyDelayFrames;
        const uint32_t second = (first + 1u) % kBodyDelayFrames;
        const float fraction = read - std::floor(read);
        const float bodyTap = state.bodyDelay[first]
            + (state.bodyDelay[second] - state.bodyDelay[first]) * fraction;

        const float localGain = regenerationGain(params.regeneration)
            * (0.34f + params.body * 0.14f) * state.governor;
        const float recursiveInput = std::clamp(
            input + bodyTap * localGain, -8.0f, 8.0f);
        float wet = 0.0f;
        if (params.mode == FeedbackShiftMode::Frequency) {
            constexpr std::array<float, kHilbertStages> branchA {{
                0.161758f, 0.733029f, 0.945350f, 0.990598f,
            }};
            constexpr std::array<float, kHilbertStages> branchB {{
                0.479401f, 0.876218f, 0.976599f, 0.997500f,
            }};
            const float inPhase = allpassCascade(recursiveInput,
                state.hilbertA, branchA);
            const float quadrature = allpassCascade(recursiveInput,
                state.hilbertB, branchB);
            wet = inPhase * cosine - quadrature * sine;
        } else {
            wet = recursiveInput * sine;
        }

        // COLOR is loop material rather than generic post distortion. A fixed
        // low/high split supplies loss on the dark side and pre-emphasis on
        // the bright side; biased, memory-dependent saturation appears only
        // as the control leaves its neutral center.
        const float split = frequencyCoefficient(2400.0f,
            static_cast<float>(sampleRate_));
        state.loopLowpass += (wet - state.loopLowpass) * split;
        const float low = state.loopLowpass;
        const float high = wet - low;
        const float dark = std::max(0.0f, -params.color);
        const float bright = std::max(0.0f, params.color);
        float colored = low * (1.0f - bright * 0.45f)
            + high * (1.0f - dark * 0.92f + bright * 1.60f);
        const float colorAmount = std::abs(params.color);
        if (colorAmount > 1.0e-5f) {
            const float drive = 1.0f + colorAmount * 5.0f
                + std::max(0.0f, params.regeneration - 0.72f) * 5.0f;
            const float bias = params.color * 0.10f
                + state.loopMemory * 0.055f;
            const float shaped = (std::tanh((colored + bias) * drive)
                    - std::tanh(bias * drive))
                / std::max(0.001f, std::tanh(drive));
            colored += (shaped - colored) * colorAmount * 0.78f;
            state.loopMemory += (shaped - state.loopMemory)
                * onePoleCoefficient(28.0f,
                    static_cast<float>(sampleRate_));
        } else {
            state.loopMemory *= 0.9995f;
        }
        colored = std::clamp(flush(colored), -6.0f, 6.0f);
        state.bodyDelay[state.bodyWrite] = colored;
        state.bodyWrite = (state.bodyWrite + 1u) % kBodyDelayFrames;
        const float bodyMix = params.body * 0.22f;
        wet = colored + (bodyTap - colored) * bodyMix;

        phases_[node] += kTwoPi * frequency
            / static_cast<float>(sampleRate_);
        while (phases_[node] >= kTwoPi) phases_[node] -= kTwoPi;
        while (phases_[node] < 0.0f) phases_[node] += kTwoPi;
        return std::clamp(flush(wet), -8.0f, 8.0f);
    }

    float processPedal(uint32_t node, float input, float modulation,
        const FeedbackShiftNodeParams& params) noexcept
    {
        auto& state = states_[node];
        if (state.activePedal != params.pedal) {
            state.activePedal = params.pedal;
            state.pedalCrossfade = 0.0f;
            state.temporalPhase = 0u;
            state.temporalArmed = true;
            state.controlCounter = 0u;
        }
        const float amount = params.pedalAmount;
        const float tone = params.pedalTone;
        const float bias = params.pedalBias;
        const float sr = static_cast<float>(sampleRate_);
        float wet = input;
        switch (state.activePedal) {
        case FeedbackPedalType::Bypass:
            wet = input;
            break;
        case FeedbackPedalType::Filter: {
            const float cutoff = 30.0f * std::pow(666.6667f, tone);
            const float g = std::tan(kPi
                * std::min(0.45f, cutoff / sr));
            const float k = 1.0f / (0.5f + 15.5f * amount * amount);
            const float a1 = 1.0f / (1.0f + g * (g + k));
            const float a2 = g * a1;
            const float a3 = g * a2;
            const float filterDrive = params.pedalExtra[0u];
            const float drive = 1.0f + filterDrive * filterDrive * 15.0f;
            const float driven = std::tanh(input * drive)
                / std::tanh(drive);
            const float v3 = driven - state.filterIc2;
            const float v1 = a1 * state.filterIc1 + a2 * v3;
            const float v2 = state.filterIc2
                + a2 * state.filterIc1 + a3 * v3;
            state.filterIc1 = flush(2.0f * v1 - state.filterIc1);
            state.filterIc2 = flush(2.0f * v2 - state.filterIc2);
            const float low = v2;
            const float band = v1;
            const float high = driven - k * v1 - v2;
            const uint32_t mode = static_cast<uint32_t>(std::clamp(
                std::floor((bias + 1.0f) * 2.0f), 0.0f, 3.0f));
            wet = mode == 0u ? low : mode == 1u ? band
                : mode == 2u ? high : low + high;
            break;
        }
        case FeedbackPedalType::Degrade: {
            if (state.holdRemaining == 0u) {
                const uint32_t base = 1u + static_cast<uint32_t>(
                    std::lround(amount * amount * 95.0f));
                const uint32_t jitter = static_cast<uint32_t>(
                    std::lround(base * 0.6f * std::abs(bias)));
                const int32_t offset = jitter == 0u ? 0
                    : static_cast<int32_t>(advanceRandom(state.random)
                        % (jitter * 2u + 1u))
                        - static_cast<int32_t>(jitter);
                state.holdRemaining = static_cast<uint32_t>(std::max(
                    1, static_cast<int32_t>(base) + offset));
                const uint32_t bits = 4u + static_cast<uint32_t>(
                    std::lround(tone * 12.0f));
                const float scale = static_cast<float>(1u << (bits - 1u));
                state.held = std::round(input * scale) / scale;
            }
            --state.holdRemaining;
            wet = state.held;
            break;
        }
        case FeedbackPedalType::Transient: {
            const float magnitude = std::abs(input);
            const float fastCoefficient = magnitude > state.transientFast
                ? pedalFastAttackCoefficient_ : pedalFastReleaseCoefficient_;
            const float slowCoefficient = magnitude > state.transientSlow
                ? pedalSlowAttackCoefficient_ : pedalSlowReleaseCoefficient_;
            state.transientFast += (magnitude - state.transientFast)
                * fastCoefficient;
            state.transientSlow += (magnitude - state.transientSlow)
                * slowCoefficient;
            const float transient = std::clamp(
                (state.transientFast - state.transientSlow)
                    / (state.transientSlow + 0.002f), 0.0f, 1.0f);
            const float gateThreshold = std::abs(bias) < 0.01f ? 0.0f
                : std::pow(10.0f, (-72.0f
                    + (bias + 1.0f) * 24.0f) * 0.05f);
            const float gateTarget = magnitude >= gateThreshold ? 1.0f : 0.0f;
            state.transientGate += (gateTarget - state.transientGate)
                * (gateTarget > state.transientGate
                    ? pedalGateAttackCoefficient_
                    : pedalGateReleaseCoefficient_);
            const float attackGain = 1.0f + (amount * 2.0f - 1.0f)
                * transient * 2.4f;
            const float sustainGain = 1.0f + (tone * 2.0f - 1.0f)
                * (1.0f - transient) * 0.92f;
            wet = input * std::max(0.0f, attackGain)
                * std::max(0.0f, sustainGain) * state.transientGate;
            break;
        }
        case FeedbackPedalType::Resonator: {
            if (state.resonatorBuffer.empty()) break;
            const float frequency = 40.0f * std::pow(100.0f, tone);
            const uint32_t delay = std::clamp<uint32_t>(
                static_cast<uint32_t>(std::lround(sampleRate_ / frequency)),
                1u, static_cast<uint32_t>(state.resonatorBuffer.size() - 1u));
            const uint32_t read = (state.resonatorWrite
                + static_cast<uint32_t>(state.resonatorBuffer.size()) - delay)
                % static_cast<uint32_t>(state.resonatorBuffer.size());
            const float delayed = state.resonatorBuffer[read];
            const float dampingCutoff = 250.0f + 17750.0f
                * std::pow(1.0f - (bias + 1.0f) * 0.5f, 2.0f);
            state.resonatorDamped += (delayed - state.resonatorDamped)
                * frequencyCoefficient(dampingCutoff, sr);
            state.resonatorBuffer[state.resonatorWrite] = flush(input
                + state.resonatorDamped * amount * 0.94f);
            state.resonatorWrite = (state.resonatorWrite + 1u)
                % static_cast<uint32_t>(state.resonatorBuffer.size());
            wet = delayed;
            break;
        }
        case FeedbackPedalType::Erosion: {
            const float rate = 10.0f * std::pow(1600.0f, tone);
            state.erosionPhase += kTwoPi * rate / sr;
            if (state.erosionPhase >= kTwoPi) {
                state.erosionPhase -= kTwoPi;
                state.erosionTarget = static_cast<float>(
                    advanceRandom(state.random) & 0x00ffffffu)
                    / 8388607.5f - 1.0f;
            }
            state.erosionNoise += (state.erosionTarget
                - state.erosionNoise) * 0.08f;
            const bool noiseShape = params.pedalExtra[0u] >= 0.5f;
            const float mod = noiseShape ? state.erosionNoise
                : std::sin(state.erosionPhase);
            const float delay = 1.0f + amount * amount * 2045.0f
                * (0.5f + mod * 0.5f);
            float position = static_cast<float>(state.erosionWrite) - delay;
            while (position < 0.0f) position += 2048.0f;
            const uint32_t first = static_cast<uint32_t>(position) & 2047u;
            const uint32_t second = (first + 1u) & 2047u;
            const float fraction = position - std::floor(position);
            wet = state.erosionBuffer[first]
                + (state.erosionBuffer[second]
                    - state.erosionBuffer[first]) * fraction;
            state.erosionBuffer[state.erosionWrite] = flush(input
                + wet * ((bias + 1.0f) * 0.5f) * 0.88f);
            state.erosionWrite = (state.erosionWrite + 1u) & 2047u;
            break;
        }
        case FeedbackPedalType::Repeater:
            wet = processTemporal(state, input, modulation, params, false);
            break;
        case FeedbackPedalType::TimeMangler:
            wet = processTemporal(state, input, modulation, params, true);
            break;
        case FeedbackPedalType::DrumEcho: {
            DrumEchoParams echo;
            echo.headMode = static_cast<DrumEchoHeadMode>(
                std::min<uint32_t>(6u,
                    static_cast<uint32_t>(std::lround(
                        params.pedalExtra[0u] * 6.0f))));
            echo.clock = static_cast<DrumEchoClock>(
                std::min<uint32_t>(kDrumEchoClockCount - 1u,
                    static_cast<uint32_t>(std::lround(
                        params.pedalExtra[1u]
                            * static_cast<float>(kDrumEchoClockCount - 1u)))));
            echo.timeMs = 20.0f * std::pow(90.0f, tone);
            echo.feedback = amount * 0.92f;
            echo.wear = params.pedalExtra[2u];
            echo.flutter = params.pedalExtra[3u];
            echo.transient = params.pedalExtra[4u] * 2.0f - 1.0f;
            echo.sensitivity = params.pedalExtra[5u];
            echo.duck = params.pedalExtra[6u];
            echo.tone = bias;
            echo.spread = 0.0f;
            echo.mix = 1.0f;
            echo.outputGainDb = 0.0f;
            state.echo.setParams(echo);
            float left = input;
            float right = input;
            state.echo.processFrame(left, right);
            wet = 0.5f * (left + right);
            break;
        }
        case FeedbackPedalType::BreakBus: {
            BreakBusParams bus;
            bus.press = amount;
            bus.snap = params.pedalExtra[0u] * 2.0f - 1.0f;
            bus.recovery = tone;
            bus.saturation = params.pedalExtra[1u];
            bus.bite = (bias + 1.0f) * 0.5f;
            bus.clip = params.pedalExtra[2u];
            bus.tilt = params.pedalExtra[3u] * 2.0f - 1.0f;
            bus.linkMode = BreakBusLinkMode::Free;
            bus.fieldSafe = params.pedalExtra[4u] >= 0.5f;
            if ((state.controlCounter++ & 63u) == 0u) {
                state.breakBus.setParams(bus);
            }
            wet = input;
            state.breakBus.processFrame(&wet, 1u);
            break;
        }
        case FeedbackPedalType::DrumBus: {
            DrumOverloadParams bus;
            bus.circuit = static_cast<DrumOverloadCircuit>(
                std::min<uint32_t>(kDrumOverloadCircuitCount - 1u,
                    static_cast<uint32_t>(std::lround(
                        params.pedalExtra[0u]
                            * static_cast<float>(
                                kDrumOverloadCircuitCount - 1u)))));
            bus.inputGainDb = amount * 12.0f;
            bus.overload = params.pedalExtra[1u];
            bus.density = params.pedalExtra[2u];
            bus.punch = params.pedalExtra[3u] * 2.0f - 1.0f;
            bus.bias = params.pedalExtra[4u] * 2.0f - 1.0f;
            bus.breakup = params.pedalExtra[5u];
            bus.weight = tone;
            bus.tone = bias;
            bus.stereoLink = 1.0f;
            bus.mix = 1.0f;
            bus.outputGainDb = -3.0f;
            state.drumBus.setParams(bus);
            float pair = input;
            wet = input;
            state.drumBus.processFrame(wet, pair);
            wet = 0.5f * (wet + pair);
            break;
        }
        case FeedbackPedalType::Wool:
            wet = processAnalogDriveCircuit(AnalogDriveCircuit::Wool,
                state.drive, input, amount, tone, bias, sr);
            break;
        case FeedbackPedalType::Rat:
            wet = processAnalogDriveCircuit(AnalogDriveCircuit::Rat,
                state.drive, input, amount, tone, bias, sr);
            break;
        case FeedbackPedalType::Diode:
            wet = processAnalogDriveCircuit(AnalogDriveCircuit::Diode,
                state.drive, input, amount, tone, bias, sr);
            break;
        case FeedbackPedalType::Fold:
        {
            const float drive = 1.0f + amount * amount * 31.0f;
            float accumulated = 0.0f;
            for (uint32_t step = 1u; step <= 4u; ++step) {
                const float fraction = static_cast<float>(step) * 0.25f;
                const float interpolated = state.foldPreviousInput
                    + (input - state.foldPreviousInput) * fraction;
                const float triangle = fold(interpolated * drive + bias);
                const float sine = std::sin((interpolated * drive + bias)
                    * kPi * 0.5f);
                accumulated += triangle + (sine - triangle) * tone;
            }
            state.foldPreviousInput = input;
            accumulated *= 0.25f;
            const float blocked = accumulated - state.foldDcInput
                + 0.995f * state.foldDcOutput;
            state.foldDcInput = accumulated;
            state.foldDcOutput = flush(blocked);
            wet = state.foldDcOutput;
            break;
        }
        case FeedbackPedalType::Relay: {
            const float magnitude = std::abs(input);
            const float attack = frequencyCoefficient(1200.0f, sr);
            const float release = frequencyCoefficient(18.0f, sr);
            state.relayEnvelope += (magnitude - state.relayEnvelope)
                * (magnitude > state.relayEnvelope ? attack : release);
            const float threshold = 0.32f * std::pow(0.025f, amount);
            if (state.relayGate < 0.5f
                && state.relayEnvelope > threshold) state.relayGate = 1.0f;
            else if (state.relayGate > 0.5f
                && state.relayEnvelope < threshold
                    * (0.28f + (bias + 1.0f) * 0.20f)) {
                state.relayGate = 0.0f;
            }
            state.relaySlew += (state.relayGate - state.relaySlew)
                * frequencyCoefficient(lerp(80.0f, 3200.0f, tone), sr);
            wet = input * (0.025f + state.relaySlew * 0.975f);
            break;
        }
        case FeedbackPedalType::Phase: {
            const float center = 60.0f * std::pow(120.0f, tone);
            const float tangent = std::tan(kPi * center / sr);
            const float coefficient = (1.0f - tangent)
                / (1.0f + tangent);
            const float first = -coefficient * input + state.phaseMemoryA;
            state.phaseMemoryA = flush(input + coefficient * first);
            const float second = -coefficient * first + state.phaseMemoryB;
            state.phaseMemoryB = flush(first + coefficient * second);
            wet = second + modulation
                * (params.pedalExtra[0u] * 2.0f - 1.0f) * 0.36f;
            wet = lerp(input, wet, amount);
            break;
        }
        case FeedbackPedalType::MacroShred: {
            MacroShredCoreParams shred;
            shred.inputGainDb = 0.0f;
            shred.pressure = amount;
            shred.shred = tone;
            shred.feedback = params.pedalExtra[0u];
            shred.color = params.pedalExtra[1u];
            shred.react = params.pedalExtra[2u];
            shred.tune = params.pedalExtra[3u];
            shred.body = params.pedalExtra[4u];
            shred.mix = 1.0f;
            shred.outputGainDb = 0.0f;
            shred.circuit = static_cast<MacroShredCircuit>(
                std::min<uint32_t>(kMacroShredCircuitCount - 1u,
                    static_cast<uint32_t>(std::lround(
                        params.pedalExtra[7u]
                            * static_cast<float>(
                                kMacroShredCircuitCount - 1u)))));
            if (state.macroShred) {
                state.macroShred->setParams(shred);
                wet = state.macroShred->processSample(input);
            }
            break;
        }
        case FeedbackPedalType::MacroPitch: {
            MacroPitchParams pitch;
            pitch.pitchSemitones = bias * 24.0f;
            pitch.fineCents = (params.pedalExtra[0u] * 2.0f - 1.0f)
                * 100.0f;
            pitch.windowMs = 20.0f * std::pow(9.0f, amount);
            pitch.glideMs = 10.0f * std::pow(200.0f, tone);
            pitch.spread = 0.0f;
            pitch.deviation = 0.0f;
            pitch.skew = 0.0f;
            pitch.center = 0.5f;
            pitch.mix = 1.0f;
            pitch.outputGainDb = 0.0f;
            if (state.macroPitch) {
                state.macroPitch->setParams(pitch);
                state.macroPitch->processFrame(&input, &wet);
            }
            break;
        }
        case FeedbackPedalType::MacroDelay: {
            MacroDelayParams delay;
            delay.timeMs = 5.0f * std::pow(400.0f, amount);
            delay.feedback = tone * 0.78f;
            delay.tone = params.pedalExtra[0u];
            delay.character = params.pedalExtra[1u];
            delay.smear = params.pedalExtra[2u];
            delay.glideMs = 10.0f * std::pow(
                200.0f, params.pedalExtra[3u]);
            delay.spread = 0.0f;
            delay.deviation = 0.0f;
            delay.skew = 0.0f;
            delay.center = 0.5f;
            delay.mix = 1.0f;
            delay.outputGainDb = 0.0f;
            if (state.macroDelay) {
                state.macroDelay->setParams(delay);
                state.macroDelay->processFrame(&input, &wet);
            }
            break;
        }
        case FeedbackPedalType::Rotor:
            wet = processRotorCircuit(state.modulation, input,
                amount, tone, bias, sr);
            break;
        case FeedbackPedalType::Chorus:
            wet = processChorusCircuit(state.modulationRuntime,
                state.modulation, input, amount, tone, bias, sr);
            break;
        case FeedbackPedalType::Fracture: {
            MacroFractureCoreParams fracture;
            fracture.processor = static_cast<FractureProcessor>(
                std::min<uint32_t>(kFractureProcessorCount - 1u,
                    static_cast<uint32_t>(std::lround(
                        params.pedalExtra[2u]
                            * static_cast<float>(
                                kFractureProcessorCount - 1u)))));
            fracture.amount = amount;
            fracture.color = tone;
            fracture.bias = bias;
            fracture.react = params.pedalExtra[0u];
            fracture.memory = params.pedalExtra[1u];
            fracture.mix = 1.0f;
            fracture.inputGainDb = 0.0f;
            fracture.outputGainDb = 0.0f;
            if (state.fracture) {
                state.fracture->setParams(fracture);
                wet = state.fracture->processSample(input, modulation);
            }
            break;
        }
        case FeedbackPedalType::Count:
            break;
        }
        state.pedalCrossfade = std::min(1.0f,
            state.pedalCrossfade + pedalCrossfadeIncrement_);
        const float mix = state.activePedal == FeedbackPedalType::Bypass
            ? 0.0f : params.pedalMix * state.pedalCrossfade;
        return flush(input + (wet - input) * mix);
    }

    float readAuxGrain(uint32_t channel, double position) const noexcept
    {
        if (auxGrainBufferFrames_ < 2u || auxGrainBuffer_.empty()) return 0.0f;
        const double length = static_cast<double>(auxGrainBufferFrames_);
        position = std::fmod(position, length);
        if (position < 0.0) position += length;
        const uint32_t first = static_cast<uint32_t>(position);
        const uint32_t second = first + 1u < auxGrainBufferFrames_
            ? first + 1u : 0u;
        const float fraction = static_cast<float>(
            position - static_cast<double>(first));
        const std::size_t firstIndex = static_cast<std::size_t>(first)
            * kFeedbackShiftChannels + channel;
        const std::size_t secondIndex = static_cast<std::size_t>(second)
            * kFeedbackShiftChannels + channel;
        return auxGrainBuffer_[firstIndex]
            + (auxGrainBuffer_[secondIndex] - auxGrainBuffer_[firstIndex])
                * fraction;
    }

    bool spawnAuxGrain() noexcept
    {
        if (auxGrainBufferFrames_ < 2u || auxGrainWrittenFrames_ < 8u) {
            return false;
        }
        AuxGrain* grain = nullptr;
        for (auto& candidate : auxGrains_) {
            if (!candidate.active) {
                grain = &candidate;
                break;
            }
        }
        if (!grain) return false;

        const auto randomUnit = [this]() noexcept {
            return static_cast<float>(
                advanceRandom(auxRandom_) & 0x00ffffffu) / 16777215.0f;
        };
        const float baseScatterRandom = randomUnit();
        const float divergence = (1.0f - smoothed_.auxGrainCoherence)
            * smoothed_.auxGrainLaneDrift;
        const float maximumLanePitchRatio = auxGrainPitchRatio_
            * std::pow(2.0f, divergence * 7.0f / 12.0f);
        const uint32_t sourceSpan = static_cast<uint32_t>(std::ceil(
            static_cast<float>(auxGrainSizeFrames_)
                * maximumLanePitchRatio));
        const uint32_t minimumDelay = std::min(auxGrainBufferFrames_ - 2u,
            std::max<uint32_t>(4u, sourceSpan + 2u));
        if (auxGrainWrittenFrames_ <= minimumDelay + 2u) return false;
        const uint32_t maximumDelay = std::min(auxGrainBufferFrames_ - 2u,
            auxGrainWrittenFrames_ - 2u);
        const uint32_t availableScatter = maximumDelay
            > minimumDelay + 2u
            ? std::min(auxGrainScatterFrames_,
                maximumDelay - minimumDelay - 2u) : 0u;
        const uint32_t scatter = static_cast<uint32_t>(std::lround(
            baseScatterRandom * static_cast<float>(availableScatter)));
        double baseRead = static_cast<double>(auxGrainWrite_)
            - static_cast<double>(minimumDelay + scatter);
        if (baseRead < 0.0) {
            baseRead += static_cast<double>(auxGrainBufferFrames_)
                * (1.0 + std::floor(-baseRead
                / static_cast<double>(auxGrainBufferFrames_)));
        }
        bool anyEnabled = false;
        uint32_t maximumLength = 8u;
        for (uint32_t channel = 0u;
             channel < kFeedbackShiftChannels; ++channel) {
            const float positionRandom = randomUnit() * 2.0f - 1.0f;
            const float pitchRandom = randomUnit() * 2.0f - 1.0f;
            const float lengthRandom = randomUnit() * 2.0f - 1.0f;
            const float dropoutRandom = randomUnit();
            const double laneOffset = positionRandom * divergence
                * static_cast<float>(availableScatter);
            grain->readPosition[channel] = std::fmod(
                baseRead + laneOffset,
                static_cast<double>(auxGrainBufferFrames_));
            if (grain->readPosition[channel] < 0.0) {
                grain->readPosition[channel] += auxGrainBufferFrames_;
            }
            const float laneSemitones = pitchRandom * divergence * 7.0f;
            grain->increment[channel] = auxGrainPitchRatio_
                * std::pow(2.0f, laneSemitones / 12.0f);
            const float lengthScale = std::pow(2.0f,
                lengthRandom * divergence * 0.80f);
            grain->length[channel] = std::clamp<uint32_t>(
                static_cast<uint32_t>(std::lround(
                    static_cast<float>(auxGrainSizeFrames_) * lengthScale)),
                8u, std::max<uint32_t>(8u,
                    auxGrainBufferFrames_ / 3u));
            maximumLength = std::max(maximumLength, grain->length[channel]);
            grain->age[channel] = 0u;
            grain->enabled[channel] = dropoutRandom
                >= divergence * 0.34f;
            anyEnabled = anyEnabled || grain->enabled[channel];
        }
        if (!anyEnabled) {
            grain->enabled[advanceRandom(auxRandom_)
                % kFeedbackShiftChannels] = true;
        }
        grain->active = true;
        auxGrainLastLengthFrames_ = maximumLength;
        return true;
    }

    float auxGrainEnvelope(float progress, uint32_t length) const noexcept
    {
        progress = std::clamp(progress, 0.0f, 1.0f);
        switch (smoothed_.auxGrainShape) {
        case FeedbackGrainShape::Hann: {
            const float sine = std::sin(progress * kPi);
            return sine * sine;
        }
        case FeedbackGrainShape::Triangle:
            return std::max(0.0f,
                1.0f - std::abs(progress * 2.0f - 1.0f));
        case FeedbackGrainShape::Decay: {
            const float attackFraction = std::max(
                2.0f / static_cast<float>(std::max(2u, length)), 0.012f);
            float attack = std::clamp(progress / attackFraction,
                0.0f, 1.0f);
            attack = attack * attack * (3.0f - 2.0f * attack);
            return attack * std::pow(std::max(0.0f, 1.0f - progress),
                0.45f + smoothed_.auxGrainEdge * 3.2f);
        }
        case FeedbackGrainShape::Gate: {
            const float antiClick = std::clamp(
                static_cast<float>(sampleRate_ * 0.00075)
                    / static_cast<float>(std::max(2u, length)),
                1.0f / static_cast<float>(std::max(2u, length)), 0.18f);
            float envelope = std::min({ 1.0f,
                progress / antiClick, (1.0f - progress) / antiClick });
            envelope = std::clamp(envelope, 0.0f, 1.0f);
            return envelope * envelope * (3.0f - 2.0f * envelope);
        }
        case FeedbackGrainShape::Tukey:
        case FeedbackGrainShape::Count:
            break;
        }
        float envelope = std::min({ 1.0f,
            progress / auxGrainFadeFraction_,
            (1.0f - progress) / auxGrainFadeFraction_ });
        envelope = std::clamp(envelope, 0.0f, 1.0f);
        return envelope * envelope * (3.0f - 2.0f * envelope);
    }

    void processPostGranulator(
        std::array<float, kFeedbackShiftChannels>& channels) noexcept
    {
        const auto dry = channels;
        if (auxGrainBufferFrames_ > 0u && !auxGrainBuffer_.empty()) {
            const std::size_t writeBase = static_cast<std::size_t>(
                auxGrainWrite_) * kFeedbackShiftChannels;
            for (uint32_t channel = 0u;
                 channel < kFeedbackShiftChannels; ++channel) {
                auxGrainBuffer_[writeBase + channel] = dry[channel];
            }
        }
        if ((auxGrainControlCounter_++ & 63u) == 0u) {
            const float grainMs = 2.0f * std::pow(125.0f,
                smoothed_.auxGrainSize);
            auxGrainSizeFrames_ = std::clamp<uint32_t>(
                static_cast<uint32_t>(std::lround(
                    sampleRate_ * grainMs * 0.001)), 8u,
                std::max<uint32_t>(8u, auxGrainBufferFrames_ / 3u));
            auxGrainDensityRatio_ = 1.25f
                + smoothed_.auxGrainDensity * 6.75f;
            auxGrainScatterFrames_ = static_cast<uint32_t>(std::lround(
                sampleRate_ * 0.75 * smoothed_.auxGrainScatter
                    * smoothed_.auxGrainScatter));
            auxGrainPitchRatio_ = std::pow(2.0f,
                smoothed_.auxGrainPitch);
            auxGrainFadeFraction_ = 0.08f
                + smoothed_.auxGrainEdge * 0.42f;
            const float spacingNorm = smoothed_.auxGrainSpacing;
            const float spacingMs = spacingNorm <= 0.001f ? 0.0f
                : 2000.0f * std::pow((spacingNorm - 0.001f) / 0.999f, 2.0f);
            auxGrainSpacingFrames_ = static_cast<uint32_t>(std::lround(
                sampleRate_ * spacingMs * 0.001));
        }

        if (smoothed_.auxGrainMix > 0.0001f) {
            auxGrainSpawnCountdown_ -= 1.0f;
            if (auxGrainSpawnCountdown_ <= 0.0f) {
                if (spawnAuxGrain()) {
                    const float nextInterval = auxGrainSpacingFrames_ > 0u
                        ? static_cast<float>(auxGrainLastLengthFrames_
                            + auxGrainSpacingFrames_)
                        : static_cast<float>(auxGrainSizeFrames_)
                            / auxGrainDensityRatio_;
                    auxGrainSpawnCountdown_ += std::max(1.0f, nextInterval);
                } else {
                    auxGrainSpawnCountdown_ = 1.0f;
                }
            }
        } else {
            auxGrainSpawnCountdown_ = 0.0f;
        }

        std::array<float, kFeedbackShiftChannels> grainOutput {};
        std::array<float, kFeedbackShiftChannels> envelopeSum {};
        uint32_t activeGrains = 0u;
        for (auto& grain : auxGrains_) {
            if (!grain.active) continue;
            bool stillActive = false;
            for (uint32_t channel = 0u;
                 channel < kFeedbackShiftChannels; ++channel) {
                if (!grain.enabled[channel]
                    || grain.length[channel] < 2u
                    || grain.age[channel] >= grain.length[channel]) {
                    continue;
                }
                const float progress = static_cast<float>(grain.age[channel])
                    / static_cast<float>(grain.length[channel] - 1u);
                const float envelope = auxGrainEnvelope(
                    progress, grain.length[channel]);
                grainOutput[channel] += readAuxGrain(
                    channel, grain.readPosition[channel]) * envelope;
                envelopeSum[channel] += envelope;
                grain.readPosition[channel] += grain.increment[channel];
                if (grain.readPosition[channel] >= auxGrainBufferFrames_) {
                    grain.readPosition[channel] = std::fmod(
                        grain.readPosition[channel],
                        static_cast<double>(auxGrainBufferFrames_));
                }
                ++grain.age[channel];
                stillActive = stillActive
                    || grain.age[channel] < grain.length[channel];
            }
            ++activeGrains;
            grain.active = stillActive;
        }
        for (uint32_t channel = 0u;
             channel < kFeedbackShiftChannels; ++channel) {
            // Preserve the audible grain window for one voice while applying
            // only power-style normalization when overlaps accumulate. Full
            // envelope-sum division would flatten every single-grain shape.
            const float wet = envelopeSum[channel] > 1.0e-5f
                ? grainOutput[channel] / std::sqrt(
                    std::max(1.0f, envelopeSum[channel]))
                : 0.0f;
            const float duckTarget = std::clamp(
                envelopeSum[channel], 0.0f, 1.0f);
            const float duckCoefficient = duckTarget
                    > auxGrainDuck_[channel]
                ? grainDuckAttackCoefficient_ : grainDuckReleaseCoefficient_;
            auxGrainDuck_[channel] += (duckTarget
                    - auxGrainDuck_[channel]) * duckCoefficient;
            auxGrainDuck_[channel] = std::clamp(
                flush(auxGrainDuck_[channel]), 0.0f, 1.0f);
            const float mix = smoothed_.auxGrainMix;
            // Retain the endpoint contract while making intermediate mixes
            // behave like a sidechained layer: active grains push the source
            // below its ordinary crossfade level, then release smoothly.
            const float dryGain = (1.0f - mix)
                * (1.0f - auxGrainDuck_[channel] * mix);
            channels[channel] = transparentLimit(
                dry[channel] * dryGain + wet * mix);
        }
        auxGrainActivity_ = static_cast<float>(activeGrains)
            / static_cast<float>(kAuxGrainVoiceCount);
        if (auxGrainBufferFrames_ > 0u) {
            auxGrainWrite_ = (auxGrainWrite_ + 1u) % auxGrainBufferFrames_;
            auxGrainWrittenFrames_ = std::min(auxGrainBufferFrames_,
                auxGrainWrittenFrames_ + 1u);
        }
    }

    void processAuxFeedback(
        std::array<float, kFeedbackShiftChannels>& channels) noexcept
    {
        const auto sourceReturns = channels;
        for (uint32_t channel = 0u;
             channel < kFeedbackShiftChannels; ++channel) {
            channels[channel] = sourceReturns[channel]
                * effectiveAuxSend_[channel];
        }
        if ((auxControlCounter_++ & 63u) == 0u) {
            BreakBusParams bus;
            bus.press = smoothed_.auxPress;
            bus.snap = 0.0f;
            bus.recovery = 0.52f;
            bus.saturation = smoothed_.auxSaturation;
            bus.bite = std::clamp(smoothed_.auxSaturation * 0.16f
                + smoothed_.auxFold * 0.24f, 0.0f, 1.0f);
            bus.clip = smoothed_.auxClip;
            bus.tilt = smoothed_.auxTilt;
            bus.linkMode = BreakBusLinkMode::All;
            bus.fieldSafe = false;
            auxBreakBus_.setParams(bus);
            auxBreakBus_.beginBlock();
        }

        const float foldAmount = smoothed_.auxFold;
        const float foldDrive = 1.0f + foldAmount * foldAmount * 31.0f;
        for (uint32_t channel = 0u;
             channel < kFeedbackShiftChannels; ++channel) {
            const float input = channels[channel];
            float accumulated = 0.0f;
            for (uint32_t step = 1u; step <= 4u; ++step) {
                const float fraction = static_cast<float>(step) * 0.25f;
                accumulated += fold((auxFoldPrevious_[channel]
                    + (input - auxFoldPrevious_[channel]) * fraction)
                        * foldDrive);
            }
            auxFoldPrevious_[channel] = input;
            const float folded = accumulated * 0.25f;
            const float shaped = input + (folded - input) * foldAmount;
            const float blocked = shaped - auxFoldDcInput_[channel]
                + 0.995f * auxFoldDcOutput_[channel];
            auxFoldDcInput_[channel] = shaped;
            auxFoldDcOutput_[channel] = flush(blocked);
            channels[channel] = auxFoldDcOutput_[channel];
        }

        auxBreakBus_.processFrame(channels.data(), kFeedbackShiftChannels);
        auxActivity_ = 0.0f;
        for (uint32_t channel = 0u;
             channel < kFeedbackShiftChannels; ++channel) {
            const float auxReturn = channels[channel] * smoothed_.auxMix;
            channels[channel] = flush(std::clamp(
                sourceReturns[channel] + auxReturn, -8.0f, 8.0f));
            auxActivity_ = std::max(auxActivity_, std::abs(auxReturn));
        }
        auxGainReductionDb_ = auxBreakBus_.gainReductionDb();
    }

    float dcBlock(float input, float& previousInput,
        float& previousOutput) noexcept
    {
        const float output = input - previousInput + dcPole_ * previousOutput;
        previousInput = input;
        previousOutput = flush(output);
        return previousOutput;
    }

    FeedbackShiftParams target_ { defaultFeedbackShiftParams() };
    FeedbackShiftParams smoothed_ { target_ };
    std::array<NodeState, kFeedbackShiftChannels> states_ {};
    std::array<float, kFeedbackShiftChannels> previousReturns_ {};
    std::array<float, kFeedbackShiftChannels> returns_ {};
    std::array<float, kFeedbackShiftChannels> ecologyReturns_ {};
    std::array<float, kFeedbackShiftChannels> phases_ {};
    std::array<float, kFeedbackShiftChannels> burstEnvelope_ {};
    std::array<float, kFeedbackShiftChannels> subBassEnvelope_ {};
    std::array<float, kFeedbackShiftChannels> effectiveAuxSend_ {{
        1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
    }};
    std::array<float, kFeedbackShiftChannels> outputPeak_ {};
    std::array<float, kFeedbackShiftChannels> nodeActivity_ {};
    std::array<float, kFeedbackShiftMatrixCells> routeSignals_ {};
    BreakBus auxBreakBus_ {};
    std::vector<float> auxGrainBuffer_ {};
    std::array<AuxGrain, kAuxGrainVoiceCount> auxGrains_ {};
    std::array<float, kFeedbackShiftChannels> auxFoldPrevious_ {};
    std::array<float, kFeedbackShiftChannels> auxFoldDcInput_ {};
    std::array<float, kFeedbackShiftChannels> auxFoldDcOutput_ {};
    double sampleRate_ = 48000.0;
    double transportTempoBpm_ = 120.0;
    bool transportHasTempo_ = false;
    uint32_t noiseState_ = 0x6d2b79f5u;
    uint32_t driftRandom_ = 0x44524654u;
    uint32_t restRandom_ = 0x52455354u;
    uint32_t spliceRandom_ = 0x53504c43u;
    uint32_t auxRandom_ = 0x41555838u;
    uint32_t auxGrainBufferFrames_ = 0u;
    uint32_t auxGrainWrite_ = 0u;
    uint32_t auxGrainWrittenFrames_ = 0u;
    uint32_t auxGrainSizeFrames_ = 96u;
    uint32_t auxGrainScatterFrames_ = 0u;
    uint32_t auxGrainSpacingFrames_ = 0u;
    uint32_t auxGrainLastLengthFrames_ = 96u;
    uint32_t auxControlCounter_ = 0u;
    uint32_t auxGrainControlCounter_ = 0u;
    std::array<float, kFeedbackShiftChannels> driftValues_ {};
    std::array<float, kFeedbackShiftChannels> driftTargets_ {};
    std::array<uint32_t, kFeedbackShiftChannels> driftCountdowns_ {};
    float morphPhase_ = 0.0f;
    float morphRandom_ = 0.5f;
    float morphChaosTarget_ = 0.73f;
    float morphChaosValue_ = 0.5f;
    float morphValue_ = 0.0f;
    float morphDriverValue_ = 0.0f;
    float morphInputEnvelope_ = 0.0f;
    float morphEdge_ = 0.0f;
    std::array<float, kFeedbackShiftChannels> laneEnvelopes_ {};
    std::array<float, kFeedbackShiftChannels> laneSlowEnvelopes_ {};
    std::array<float, kFeedbackShiftChannels> laneEdges_ {};
    std::array<float, kFeedbackShiftChannels> restGains_ {{
        1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
    }};
    std::array<float, kFeedbackShiftChannels> restPersistenceFrames_ {};
    std::array<uint32_t, kFeedbackShiftChannels> restRemainingFrames_ {};
    std::array<uint32_t, kFeedbackShiftChannels> restTriggerFrames_ {{
        48000u, 48000u, 48000u, 48000u,
        48000u, 48000u, 48000u, 48000u,
    }};
    std::array<SplicePatch, kFeedbackShiftChannels> splicePatches_ {};
    std::array<SplicePatch, kFeedbackShiftChannels>
        splicePendingPatches_ {};
    std::array<bool, kFeedbackShiftChannels> splicePatchValid_ {};
    std::array<SpliceStage, kFeedbackShiftChannels> spliceStages_ {};
    std::array<float, kFeedbackShiftChannels> spliceGains_ {{
        1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
    }};
    std::array<float, kFeedbackShiftChannels> spliceFadeSteps_ {{
        1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
    }};
    std::array<uint32_t, kFeedbackShiftChannels> spliceCountdowns_ {};
    std::array<uint32_t, kFeedbackShiftChannels> spliceFlurryRemaining_ {};
    std::array<float, kFeedbackShiftChannels> splicePulses_ {};
    uint32_t spliceEventCount_ = 0u;
    uint32_t subBassControlCounter_ = 0u;
    float burstDecay_ = 0.9997f;
    float subBassDecayCoefficient_ = 0.9995f;
    float runGain_ = 1.0f;
    float exciteGain_ = 0.001f;
    float outputGain_ = 0.125f;
    float minimumGovernor_ = 1.0f;
    float auxGrainSpawnCountdown_ = 0.0f;
    float auxGrainDensityRatio_ = 3.0f;
    float auxGrainPitchRatio_ = 1.0f;
    float auxGrainFadeFraction_ = 0.28f;
    float auxGrainActivity_ = 0.0f;
    std::array<float, kFeedbackShiftChannels> auxGrainDuck_ {};
    float auxActivity_ = 0.0f;
    float auxGainReductionDb_ = 0.0f;
    float parameterCoefficient_ = 0.002f;
    float runCoefficient_ = 0.002f;
    float detectorAttackCoefficient_ = 0.005f;
    float detectorReleaseCoefficient_ = 0.0001f;
    float driftSlewCoefficient_ = 0.00002f;
    float splicePulseDecay_ = 0.9992f;
    float pedalFastAttackCoefficient_ = 0.05f;
    float pedalFastReleaseCoefficient_ = 0.001f;
    float pedalSlowAttackCoefficient_ = 0.001f;
    float pedalSlowReleaseCoefficient_ = 0.0001f;
    float pedalGateAttackCoefficient_ = 0.01f;
    float pedalGateReleaseCoefficient_ = 0.0001f;
    float governorAttackCoefficient_ = 0.001f;
    float governorReleaseCoefficient_ = 0.00005f;
    float grainDuckAttackCoefficient_ = 0.01f;
    float grainDuckReleaseCoefficient_ = 0.0002f;
    float noiseHighpassCoefficient_ = 0.002f;
    float sourceSwitchCoefficient_ = 0.01f;
    float dcPole_ = 0.998f;
    float pedalCrossfadeIncrement_ = 0.001f;
};

} // namespace s3g
