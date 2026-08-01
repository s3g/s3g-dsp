#pragma once

#include "s3g_analog_drive_circuits.h"
#include "s3g_fracture_processors.h"
#include "s3g_math.h"
#include "s3g_matrix_flow_shapes.h"
#include "s3g_realtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace s3g {

constexpr uint32_t kNoInputMixerChannels = 8u;
constexpr uint32_t kNoInputMixerInsertSlots = 3u;
constexpr uint32_t kNoInputMixerMatrixCells =
    kNoInputMixerChannels * kNoInputMixerChannels;
constexpr uint8_t kNoInputMatrixGridChannels = 4u;
constexpr uint8_t kNoInputMatrixGridNotesPerController = 16u;
constexpr uint8_t kNoInputMatrixFeedbackCommand = 0xa0u;
constexpr uint8_t kNoInputMatrixFeedbackCenter = 64u;

enum class NoInputMatrixMidiMode : uint32_t {
    Flip = 0u,
    Latch,
    Count,
};

enum class NoInputMatrixMidiSign : uint32_t {
    Positive = 0u,
    Negative,
    Count,
};

constexpr float kNoInputMatrixMidiRampMinimumMs = 20.0f;
constexpr float kNoInputMatrixMidiRampDefaultMs = 1000.0f;
constexpr float kNoInputMatrixMidiRampMaximumMs = 10000.0f;

inline const char* noInputMatrixMidiModeName(NoInputMatrixMidiMode mode)
{
    switch (mode) {
    case NoInputMatrixMidiMode::Flip: return "FLIP";
    case NoInputMatrixMidiMode::Latch: return "LATCH";
    default: return "FLIP";
    }
}

inline const char* noInputMatrixMidiSignName(NoInputMatrixMidiSign sign)
{
    return sign == NoInputMatrixMidiSign::Negative
        ? "NEGATIVE" : "POSITIVE";
}

inline float noInputMatrixMidiGain(NoInputMatrixMidiMode mode,
    float baseGain, uint8_t velocity,
    NoInputMatrixMidiSign latchSign = NoInputMatrixMidiSign::Positive)
{
    baseGain = std::clamp(std::isfinite(baseGain) ? baseGain : 0.0f,
        -1.0f, 1.0f);
    const float normalized = static_cast<float>(velocity & 0x7fu) / 127.0f;
    if (mode == NoInputMatrixMidiMode::Flip) {
        if (std::abs(baseGain) <= 1.0e-7f) return 0.0f;
        const float opposite = baseGain > 0.0f ? -1.0f : 1.0f;
        return baseGain + (opposite - baseGain) * normalized;
    }
    return latchSign == NoInputMatrixMidiSign::Negative
        ? -normalized : normalized;
}

inline bool decodeNoInputMatrixGridNote(uint8_t channel, uint8_t note,
    uint32_t& destination, uint32_t& source)
{
    if (channel >= kNoInputMatrixGridChannels
        || note >= kNoInputMatrixGridNotesPerController) return false;
    const uint32_t tileRow = channel / 2u;
    const uint32_t tileColumn = channel % 2u;
    destination = tileRow * 4u + note / 4u;
    source = tileColumn * 4u + note % 4u;
    return true;
}

inline bool encodeNoInputMatrixGridPoint(uint32_t destination,
    uint32_t source, uint8_t& channel, uint8_t& note)
{
    if (destination >= kNoInputMixerChannels
        || source >= kNoInputMixerChannels) return false;
    channel = static_cast<uint8_t>((destination / 4u) * 2u
        + source / 4u);
    note = static_cast<uint8_t>((destination % 4u) * 4u
        + source % 4u);
    return true;
}

inline uint8_t encodeNoInputMatrixFeedbackValue(float gain)
{
    gain = std::clamp(std::isfinite(gain) ? gain : 0.0f, -1.0f, 1.0f);
    if (gain > 0.0f) {
        return static_cast<uint8_t>(kNoInputMatrixFeedbackCenter
            + std::lround(gain * 63.0f));
    }
    if (gain < 0.0f) {
        return static_cast<uint8_t>(kNoInputMatrixFeedbackCenter
            - std::lround(-gain * 64.0f));
    }
    return kNoInputMatrixFeedbackCenter;
}

inline float decodeNoInputMatrixFeedbackValue(uint8_t value)
{
    value &= 0x7fu;
    if (value > kNoInputMatrixFeedbackCenter) {
        return static_cast<float>(value - kNoInputMatrixFeedbackCenter)
            / 63.0f;
    }
    if (value < kNoInputMatrixFeedbackCenter) {
        return -static_cast<float>(kNoInputMatrixFeedbackCenter - value)
            / 64.0f;
    }
    return 0.0f;
}

enum class NoInputDistortionType : uint32_t {
    Bypass = 0u,
    Wool,
    Rat,
    ZoneA,
    ZoneB,
    FuzzI,
    FuzzII,
    Diode,
    Ring,
    Relay,
    Crush,
    Splice,
    Logic,
    Shred,
    Void,
    Rotor,
    Phase,
    Chorus,
    Throat,
    Robot,
    OctDown,
    OctUp,
    OctStack,
    Count,
};

constexpr uint32_t kNoInputDistortionTypeCount =
    static_cast<uint32_t>(NoInputDistortionType::Count);

inline const char* noInputDistortionName(NoInputDistortionType type)
{
    switch (type) {
    case NoInputDistortionType::Bypass: return "BYPASS";
    case NoInputDistortionType::Wool: return "WOOL";
    case NoInputDistortionType::Rat: return "RAT";
    case NoInputDistortionType::ZoneA: return "ZONE A";
    case NoInputDistortionType::ZoneB: return "ZONE B";
    case NoInputDistortionType::FuzzI: return "FUZZ I";
    case NoInputDistortionType::FuzzII: return "FUZZ II";
    case NoInputDistortionType::Diode: return "DIODE";
    case NoInputDistortionType::Ring: return "RING";
    case NoInputDistortionType::Relay: return "RELAY";
    case NoInputDistortionType::Crush: return "CRUSH";
    case NoInputDistortionType::Splice: return "SPLICE";
    case NoInputDistortionType::Logic: return "LOGIC";
    case NoInputDistortionType::Shred: return "SHRED";
    case NoInputDistortionType::Void: return "VOID";
    case NoInputDistortionType::Rotor: return "ROTOR";
    case NoInputDistortionType::Phase: return "PHASE";
    case NoInputDistortionType::Chorus: return "CHORUS";
    case NoInputDistortionType::Throat: return "THROAT";
    case NoInputDistortionType::Robot: return "ROBOT";
    case NoInputDistortionType::OctDown: return "OCT DOWN";
    case NoInputDistortionType::OctUp: return "OCT UP";
    case NoInputDistortionType::OctStack: return "OCT STACK";
    case NoInputDistortionType::Count: break;
    }
    return "BYPASS";
}

enum class NoInputMovementBehavior : uint32_t {
    Glide = 0u,
    Step,
    Cut,
    Burst,
    Scramble,
    Ratchet,
    Cascade,
    Erode,
    Count,
};

constexpr uint32_t kNoInputMovementBehaviorCount =
    static_cast<uint32_t>(NoInputMovementBehavior::Count);

enum class NoInputReactMode : uint32_t {
    Off = 0u,
    Follow,
    Avoid,
    Edge,
    Balance,
    Count,
};

enum class NoInputRandomEnergy : uint32_t {
    High = 0u,
    Mid,
    Low,
    Count,
};

inline const char* noInputRandomEnergyName(NoInputRandomEnergy energy)
{
    switch (energy) {
    case NoInputRandomEnergy::High: return "HIGH / QUICK";
    case NoInputRandomEnergy::Mid: return "MID / MODERATE";
    case NoInputRandomEnergy::Low: return "LOW / SLOW";
    case NoInputRandomEnergy::Count: break;
    }
    return "MID / MODERATE";
}

inline float noInputRandomSeedAmount(NoInputRandomEnergy energy)
{
    switch (energy) {
    case NoInputRandomEnergy::High: return 0.86f;
    case NoInputRandomEnergy::Mid: return 0.76f;
    case NoInputRandomEnergy::Low: return 0.70f;
    case NoInputRandomEnergy::Count: break;
    }
    return 0.76f;
}

inline const char* noInputReactModeName(NoInputReactMode mode)
{
    switch (mode) {
    case NoInputReactMode::Off: return "OFF";
    case NoInputReactMode::Follow: return "FOLLOW";
    case NoInputReactMode::Avoid: return "AVOID";
    case NoInputReactMode::Edge: return "EDGE";
    case NoInputReactMode::Balance: return "BALANCE";
    case NoInputReactMode::Count: break;
    }
    return "OFF";
}

enum class NoInputAuxTap : uint32_t {
    Return = 0u,
    PreEq,
    PostEq,
    PostInsert,
    Count,
};

inline const char* noInputAuxTapName(NoInputAuxTap tap)
{
    switch (tap) {
    case NoInputAuxTap::Return: return "RETURN";
    case NoInputAuxTap::PreEq: return "PRE EQ";
    case NoInputAuxTap::PostEq: return "POST EQ";
    case NoInputAuxTap::PostInsert: return "POST INSERT";
    case NoInputAuxTap::Count: break;
    }
    return "RETURN";
}

constexpr uint32_t kNoInputClockDivisionCount = 10u;

inline float noInputClockDivisionBeats(uint32_t division)
{
    static constexpr std::array<float, kNoInputClockDivisionCount> beats {{
        0.0625f, 0.125f, 0.25f, 0.5f, 1.0f,
        2.0f, 4.0f, 8.0f, 16.0f, 32.0f,
    }};
    return beats[std::min<uint32_t>(division,
        kNoInputClockDivisionCount - 1u)];
}

inline const char* noInputClockDivisionName(uint32_t division)
{
    static constexpr std::array<const char*, kNoInputClockDivisionCount>
        names {{
            "1/64", "1/32", "1/16", "1/8", "1/4",
            "1/2", "1 BAR", "2 BARS", "4 BARS", "8 BARS",
        }};
    return names[std::min<uint32_t>(division,
        kNoInputClockDivisionCount - 1u)];
}

inline const char* noInputMovementBehaviorName(NoInputMovementBehavior behavior)
{
    switch (behavior) {
    case NoInputMovementBehavior::Glide: return "GLIDE";
    case NoInputMovementBehavior::Step: return "STEP";
    case NoInputMovementBehavior::Cut: return "CUT";
    case NoInputMovementBehavior::Burst: return "BURST";
    case NoInputMovementBehavior::Scramble: return "SCRAMBLE";
    case NoInputMovementBehavior::Ratchet: return "RATCHET";
    case NoInputMovementBehavior::Cascade: return "CASCADE";
    case NoInputMovementBehavior::Erode: return "ERODE";
    case NoInputMovementBehavior::Count: break;
    }
    return "GLIDE";
}

inline bool noInputMovementBehaviorUsesAmplitude(
    NoInputMovementBehavior behavior)
{
    return behavior == NoInputMovementBehavior::Cut
        || behavior == NoInputMovementBehavior::Burst
        || behavior == NoInputMovementBehavior::Scramble
        || behavior == NoInputMovementBehavior::Ratchet
        || behavior == NoInputMovementBehavior::Cascade
        || behavior == NoInputMovementBehavior::Erode;
}

inline bool noInputMovementBehaviorUsesLength(
    NoInputMovementBehavior behavior)
{
    return behavior == NoInputMovementBehavior::Cut
        || behavior == NoInputMovementBehavior::Burst
        || behavior == NoInputMovementBehavior::Ratchet
        || behavior == NoInputMovementBehavior::Cascade
        || behavior == NoInputMovementBehavior::Erode;
}

inline bool noInputMovementBehaviorUsesDensity(
    NoInputMovementBehavior behavior)
{
    return noInputMovementBehaviorUsesLength(behavior)
        || behavior == NoInputMovementBehavior::Scramble;
}

struct NoInputMovementBehaviorParams {
    NoInputMovementBehavior behavior = NoInputMovementBehavior::Glide;
    float eventRate = 0.42f;
    float length = 0.32f;
    float density = 0.55f;
    float chaos = 0.34f;
    float slew = 0.22f;
    float choke = 0.0f;
};

inline NoInputMovementBehaviorParams sanitizeNoInputMovementBehaviorParams(
    NoInputMovementBehaviorParams params)
{
    params.behavior = static_cast<NoInputMovementBehavior>(
        std::min<uint32_t>(static_cast<uint32_t>(params.behavior),
            static_cast<uint32_t>(NoInputMovementBehavior::Count) - 1u));
    const auto normalized = [](float value, float fallback) {
        return clamp(std::isfinite(value) ? value : fallback, 0.0f, 1.0f);
    };
    params.eventRate = normalized(params.eventRate, 0.42f);
    params.length = normalized(params.length, 0.32f);
    params.density = normalized(params.density, 0.55f);
    params.chaos = normalized(params.chaos, 0.34f);
    params.slew = normalized(params.slew, 0.22f);
    params.choke = normalized(params.choke, 0.0f);
    return params;
}

inline float noInputMovementEventRateHz(float normalizedRate)
{
    normalizedRate = clamp(std::isfinite(normalizedRate)
        ? normalizedRate : 0.42f, 0.0f, 1.0f);
    return 0.25f * std::pow(320.0f, normalizedRate);
}

inline float noInputMovementEventRateHz(float normalizedRate, bool slow)
{
    if (!slow) return noInputMovementEventRateHz(normalizedRate);
    normalizedRate = clamp(std::isfinite(normalizedRate)
        ? normalizedRate : 0.42f, 0.0f, 1.0f);
    return (1.0f / 600.0f) * std::pow(600.0f, normalizedRate);
}

inline float noInputMovementLengthMs(float normalizedLength)
{
    normalizedLength = clamp(std::isfinite(normalizedLength)
        ? normalizedLength : 0.32f, 0.0f, 1.0f);
    return 0.5f * std::pow(500.0f, normalizedLength);
}

inline float noInputMovementLengthNormalized(float milliseconds)
{
    milliseconds = clamp(std::isfinite(milliseconds)
        ? milliseconds : noInputMovementLengthMs(0.32f), 0.5f, 250.0f);
    return std::log(milliseconds / 0.5f) / std::log(500.0f);
}

inline float noInputMovementSlewMs(float normalizedSlew)
{
    normalizedSlew = clamp(std::isfinite(normalizedSlew)
        ? normalizedSlew : 0.22f, 0.0f, 1.0f);
    return 0.5f * std::pow(40.0f, normalizedSlew);
}

inline float noInputMovementSlewNormalized(float milliseconds)
{
    milliseconds = clamp(std::isfinite(milliseconds)
        ? milliseconds : noInputMovementSlewMs(0.22f), 0.5f, 20.0f);
    return std::log(milliseconds / 0.5f) / std::log(40.0f);
}

inline NoInputMovementBehaviorParams randomizedNoInputMovementBehaviorParams(
    uint32_t seed, NoInputRandomEnergy energy = NoInputRandomEnergy::Mid)
{
    const auto unit = [&seed]() {
        seed += 0x9e3779b9u;
        uint32_t value = seed;
        value ^= value >> 16u;
        value *= 0x7feb352du;
        value ^= value >> 15u;
        value *= 0x846ca68bu;
        value ^= value >> 16u;
        return static_cast<float>(value & 0x00ffffffu)
            / static_cast<float>(0x01000000u);
    };
    NoInputMovementBehaviorParams params;
    energy = static_cast<NoInputRandomEnergy>(std::min<uint32_t>(
        static_cast<uint32_t>(energy),
        static_cast<uint32_t>(NoInputRandomEnergy::Count) - 1u));
    const auto randomLength = [&unit](float minimumMs, float maximumMs) {
        return noInputMovementLengthNormalized(
            lerp(minimumMs, maximumMs, unit()));
    };
    if (energy == NoInputRandomEnergy::High) {
        static constexpr std::array<NoInputMovementBehavior, 5u>
            highBehaviors {{
                NoInputMovementBehavior::Cut,
                NoInputMovementBehavior::Burst,
                NoInputMovementBehavior::Scramble,
                NoInputMovementBehavior::Ratchet,
                NoInputMovementBehavior::Cascade,
            }};
        params.behavior = highBehaviors[
            static_cast<uint32_t>(unit() * highBehaviors.size())
                % highBehaviors.size()];
        params.eventRate = 0.75f + unit() * 0.23f;
        params.length = randomLength(6.0f, 40.0f);
        params.density = 0.34f + unit() * 0.46f;
        params.chaos = 0.58f + unit() * 0.40f;
        params.slew = noInputMovementSlewNormalized(
            lerp(1.0f, 2.0f, unit()));
        params.choke = 0.66f + unit() * 0.34f;
    } else if (energy == NoInputRandomEnergy::Low) {
        params.behavior = NoInputMovementBehavior::Glide;
        params.eventRate = 0.64f + unit() * 0.20f;
        params.length = 0.36f + unit() * 0.40f;
        params.density = 0.70f + unit() * 0.24f;
        params.chaos = 0.08f + unit() * 0.28f;
        params.slew = 0.24f + unit() * 0.30f;
        params.choke = 0.0f;
    } else {
        static constexpr std::array<NoInputMovementBehavior, 7u>
            midBehaviors {{
                NoInputMovementBehavior::Step,
                NoInputMovementBehavior::Cut,
                NoInputMovementBehavior::Burst,
                NoInputMovementBehavior::Scramble,
                NoInputMovementBehavior::Ratchet,
                NoInputMovementBehavior::Cascade,
                NoInputMovementBehavior::Erode,
            }};
        params.behavior = midBehaviors[
            static_cast<uint32_t>(unit() * midBehaviors.size())
                % midBehaviors.size()];
        params.eventRate = 0.42f + unit() * 0.28f;
        params.length = randomLength(20.0f, 140.0f);
        params.density = 0.46f + unit() * 0.42f;
        params.chaos = 0.24f + unit() * 0.54f;
        params.slew = 0.04f + unit() * 0.25f;
        params.choke = 0.34f + unit() * 0.48f;
    }
    return sanitizeNoInputMovementBehaviorParams(params);
}

struct NoInputInsertParams {
    NoInputDistortionType type = NoInputDistortionType::Bypass;
    float gain = 0.35f;
    float tone = 0.50f;
    float bias = 0.0f;
    float levelDb = 0.0f;
    uint32_t bypass = 0u;
};

struct NoInputAuxParams {
    NoInputInsertParams effect {};
    float feedback = 0.24f;
    float returnGain = 0.18f;
};

struct NoInputLaneParams {
    float body = 0.50f;
    float loss = 0.38f;
    float levelDb = -3.0f;
    uint32_t mute = 0u;
    float lowDb = 0.0f;
    float midFrequencyHz = 850.0f;
    float midGainDb = 0.0f;
    float highDb = 0.0f;
    std::array<float, 2u> auxSend {{ 0.0f, 0.0f }};
    float tuneNote = 60.0f;
    float tuneCents = 0.0f;
    uint32_t pitchLock = 0u;
    std::array<NoInputAuxTap, 2u> auxTap {{
        NoInputAuxTap::Return, NoInputAuxTap::Return,
    }};
    std::array<float, 2u> auxReturn {{ 0.42f, 0.36f }};
    std::array<NoInputInsertParams, kNoInputMixerInsertSlots> inserts {};
};

struct NoInputMixerParams {
    float outputGainDb = -18.0f;
    float ceilingDb = -1.0f;
    uint32_t limiterEnabled = 1u;
    uint32_t dcBlockEnabled = 1u;
    float feedback = 0.82f;
    float coupling = 0.42f;
    float phase = 0.34f;
    float drift = 0.18f;
    float formant = 0.30f;
    float agency = 0.28f;
    float space = 0.10f;
    float variance = 0.12f;
    float internalTone = 0.0f;
    float houseTone = -0.08f;
    float flow = 0.42f;
    float spread = 0.36f;
    float vortex = 0.0f;
    float motion = 0.0f;
    MatrixFlowShape motionShape = MatrixFlowShape::Flow;
    float motionRate = 0.15f;
    float motionPhase = 0.0f;
    NoInputReactMode reactMode = NoInputReactMode::Off;
    float reactDepth = 0.0f;
    float reactThreshold = 0.24f;
    float reactAttack = 0.18f;
    float reactRelease = 0.42f;
    float reactPolarity = 1.0f;
    uint32_t controllerHold = 0u;
    uint32_t slowTime = 0u;
    uint32_t clockSync = 0u;
    uint32_t fieldDivision = 6u;
    uint32_t eventDivision = 2u;
    float surfaceX = 0.5f;
    float surfaceY = 0.5f;
    uint32_t quality = 1u;
    uint32_t seed = 0x5455444fu;
    std::array<NoInputAuxParams, 2u> aux {};
    std::array<float, kNoInputMixerMatrixCells> matrix {};
    std::array<NoInputLaneParams, kNoInputMixerChannels> lanes {};
};

inline NoInputMixerParams defaultNoInputMixerParams()
{
    NoInputMixerParams params;
    params.aux[0].effect.type = NoInputDistortionType::Diode;
    params.aux[0].effect.gain = 0.18f;
    params.aux[0].effect.tone = 0.34f;
    params.aux[0].feedback = 0.28f;
    params.aux[0].returnGain = 0.16f;
    params.aux[1].effect.type = NoInputDistortionType::Ring;
    params.aux[1].effect.gain = 0.22f;
    params.aux[1].effect.tone = 0.58f;
    params.aux[1].feedback = 0.18f;
    params.aux[1].returnGain = 0.12f;
    for (uint32_t destination = 0u;
         destination < kNoInputMixerChannels; ++destination) {
        for (uint32_t source = 0u;
             source < kNoInputMixerChannels; ++source) {
            params.matrix[destination * kNoInputMixerChannels + source] =
                destination == source ? 0.94f : 0.0f;
        }
        const uint32_t previous =
            (destination + kNoInputMixerChannels - 1u)
            % kNoInputMixerChannels;
        params.matrix[destination * kNoInputMixerChannels + previous] =
            (destination & 1u) == 0u ? 0.12f : -0.10f;

        auto& lane = params.lanes[destination];
        lane.body = 0.28f + 0.055f * static_cast<float>(destination);
        lane.loss = 0.31f + 0.025f * static_cast<float>(destination % 3u);
        lane.levelDb = -4.5f;
        lane.auxSend[0] = 0.08f + 0.015f
            * static_cast<float>(destination % 4u);
        lane.auxSend[1] = 0.04f + 0.012f
            * static_cast<float>((destination + 2u) % 4u);
        lane.tuneNote = 45.0f + static_cast<float>(destination) * 3.0f;
        lane.auxReturn[0] = 0.42f;
        lane.auxReturn[1] = (destination & 1u) == 0u ? 0.36f : -0.36f;
        lane.midFrequencyHz = 420.0f
            * std::pow(1.24f, static_cast<float>(destination));

        lane.inserts[0].type = NoInputDistortionType::Wool;
        lane.inserts[0].gain = 0.26f + 0.018f * static_cast<float>(destination);
        lane.inserts[0].tone = 0.38f + 0.045f * static_cast<float>(destination % 4u);
        lane.inserts[0].levelDb = -8.0f;
        lane.inserts[1].type = NoInputDistortionType::Rat;
        lane.inserts[1].gain = 0.24f;
        lane.inserts[1].tone = 0.58f;
        lane.inserts[1].levelDb = -5.0f;
        lane.inserts[1].bypass = 1u;
        lane.inserts[2].type = NoInputDistortionType::ZoneA;
        lane.inserts[2].gain = 0.20f;
        lane.inserts[2].tone = 0.52f;
        lane.inserts[2].levelDb = -6.0f;
        lane.inserts[2].bypass = 1u;
    }
    return params;
}

inline NoInputInsertParams sanitizeNoInputInsertParams(
    NoInputInsertParams params)
{
    const uint32_t type = std::min<uint32_t>(
        static_cast<uint32_t>(params.type),
        kNoInputDistortionTypeCount - 1u);
    params.type = static_cast<NoInputDistortionType>(type);
    params.gain = clamp(std::isfinite(params.gain) ? params.gain : 0.0f,
        0.0f, 1.0f);
    params.tone = clamp(std::isfinite(params.tone) ? params.tone : 0.5f,
        0.0f, 1.0f);
    params.bias = clamp(std::isfinite(params.bias) ? params.bias : 0.0f,
        -1.0f, 1.0f);
    params.levelDb = clamp(
        std::isfinite(params.levelDb) ? params.levelDb : 0.0f,
        -24.0f, 12.0f);
    params.bypass = params.bypass != 0u ? 1u : 0u;
    return params;
}

inline NoInputMixerParams sanitizeNoInputMixerParams(
    NoInputMixerParams params)
{
    params.outputGainDb = clamp(
        std::isfinite(params.outputGainDb) ? params.outputGainDb : -18.0f,
        -60.0f, 6.0f);
    params.ceilingDb = clamp(
        std::isfinite(params.ceilingDb) ? params.ceilingDb : -1.0f,
        -18.0f, 0.0f);
    params.limiterEnabled = params.limiterEnabled != 0u ? 1u : 0u;
    params.dcBlockEnabled = params.dcBlockEnabled != 0u ? 1u : 0u;
    params.feedback = clamp(
        std::isfinite(params.feedback) ? params.feedback : 0.82f,
        0.0f, 1.25f);
    params.coupling = clamp(
        std::isfinite(params.coupling) ? params.coupling : 0.42f,
        0.0f, 1.25f);
    params.phase = clamp(std::isfinite(params.phase) ? params.phase : 0.34f,
        0.0f, 1.0f);
    params.drift = clamp(std::isfinite(params.drift) ? params.drift : 0.18f,
        0.0f, 1.0f);
    params.formant = clamp(
        std::isfinite(params.formant) ? params.formant : 0.30f,
        0.0f, 1.0f);
    params.agency = clamp(
        std::isfinite(params.agency) ? params.agency : 0.28f,
        0.0f, 1.0f);
    params.space = clamp(
        std::isfinite(params.space) ? params.space : 0.10f,
        0.0f, 1.0f);
    params.variance = clamp(
        std::isfinite(params.variance) ? params.variance : 0.12f,
        0.0f, 1.0f);
    params.internalTone = clamp(
        std::isfinite(params.internalTone) ? params.internalTone : 0.0f,
        -1.0f, 1.0f);
    params.houseTone = clamp(
        std::isfinite(params.houseTone) ? params.houseTone : -0.08f,
        -1.0f, 1.0f);
    params.flow = clamp(std::isfinite(params.flow) ? params.flow : 0.42f,
        0.0f, 1.0f);
    params.spread = clamp(
        std::isfinite(params.spread) ? params.spread : 0.36f,
        0.0f, 1.0f);
    params.vortex = clamp(
        std::isfinite(params.vortex) ? params.vortex : 0.0f,
        -1.0f, 1.0f);
    params.motion = clamp(
        std::isfinite(params.motion) ? params.motion : 0.0f,
        0.0f, 1.0f);
    params.motionShape = matrixFlowShapeFromIndex(
        static_cast<uint32_t>(params.motionShape));
    params.motionRate = clamp(
        std::isfinite(params.motionRate) ? params.motionRate : 0.15f,
        0.0f, 1.0f);
    params.motionPhase = clamp(
        std::isfinite(params.motionPhase) ? params.motionPhase : 0.0f,
        0.0f, 1.0f);
    params.reactMode = static_cast<NoInputReactMode>(
        std::min<uint32_t>(static_cast<uint32_t>(params.reactMode),
            static_cast<uint32_t>(NoInputReactMode::Count) - 1u));
    params.reactDepth = clamp(std::isfinite(params.reactDepth)
        ? params.reactDepth : 0.0f, 0.0f, 1.0f);
    params.reactThreshold = clamp(std::isfinite(params.reactThreshold)
        ? params.reactThreshold : 0.24f, 0.0f, 1.0f);
    params.reactAttack = clamp(std::isfinite(params.reactAttack)
        ? params.reactAttack : 0.18f, 0.0f, 1.0f);
    params.reactRelease = clamp(std::isfinite(params.reactRelease)
        ? params.reactRelease : 0.42f, 0.0f, 1.0f);
    params.reactPolarity = std::isfinite(params.reactPolarity)
            && params.reactPolarity < 0.0f
        ? -1.0f : 1.0f;
    params.controllerHold = params.controllerHold != 0u ? 1u : 0u;
    params.slowTime = params.slowTime != 0u ? 1u : 0u;
    params.clockSync = params.clockSync != 0u ? 1u : 0u;
    params.fieldDivision = std::min<uint32_t>(params.fieldDivision,
        kNoInputClockDivisionCount - 1u);
    params.eventDivision = std::min<uint32_t>(params.eventDivision,
        kNoInputClockDivisionCount - 1u);
    params.surfaceX = clamp(std::isfinite(params.surfaceX)
        ? params.surfaceX : 0.5f, 0.0f, 1.0f);
    params.surfaceY = clamp(std::isfinite(params.surfaceY)
        ? params.surfaceY : 0.5f, 0.0f, 1.0f);
    params.quality = std::min<uint32_t>(params.quality, 2u);
    if (params.seed == 0u) params.seed = 1u;
    for (float& value : params.matrix) {
        value = clamp(std::isfinite(value) ? value : 0.0f, -1.0f, 1.0f);
    }
    for (auto& lane : params.lanes) {
        lane.body = clamp(std::isfinite(lane.body) ? lane.body : 0.5f,
            0.0f, 1.0f);
        lane.loss = clamp(std::isfinite(lane.loss) ? lane.loss : 0.38f,
            0.0f, 1.0f);
        lane.levelDb = clamp(
            std::isfinite(lane.levelDb) ? lane.levelDb : -3.0f,
            -60.0f, 12.0f);
        lane.mute = lane.mute != 0u ? 1u : 0u;
        lane.lowDb = clamp(std::isfinite(lane.lowDb) ? lane.lowDb : 0.0f,
            -18.0f, 18.0f);
        lane.midFrequencyHz = clamp(
            std::isfinite(lane.midFrequencyHz)
                ? lane.midFrequencyHz : 850.0f,
            80.0f, 8000.0f);
        lane.midGainDb = clamp(
            std::isfinite(lane.midGainDb) ? lane.midGainDb : 0.0f,
            -18.0f, 18.0f);
        lane.highDb = clamp(
            std::isfinite(lane.highDb) ? lane.highDb : 0.0f,
            -18.0f, 18.0f);
        for (float& send : lane.auxSend) {
            send = clamp(std::isfinite(send) ? send : 0.0f,
                0.0f, 1.0f);
        }
        lane.tuneNote = clamp(std::isfinite(lane.tuneNote)
            ? lane.tuneNote : 60.0f, 24.0f, 108.0f);
        lane.tuneCents = clamp(std::isfinite(lane.tuneCents)
            ? lane.tuneCents : 0.0f, -100.0f, 100.0f);
        lane.pitchLock = lane.pitchLock != 0u ? 1u : 0u;
        for (auto& tap : lane.auxTap) {
            tap = static_cast<NoInputAuxTap>(std::min<uint32_t>(
                static_cast<uint32_t>(tap),
                static_cast<uint32_t>(NoInputAuxTap::Count) - 1u));
        }
        for (float& value : lane.auxReturn) {
            value = clamp(std::isfinite(value) ? value : 0.0f,
                -1.0f, 1.0f);
        }
        for (auto& insert : lane.inserts) {
            insert = sanitizeNoInputInsertParams(insert);
        }
    }
    for (auto& aux : params.aux) {
        aux.effect = sanitizeNoInputInsertParams(aux.effect);
        aux.effect.bypass = 0u;
        aux.effect.levelDb = 0.0f;
        aux.feedback = clamp(
            std::isfinite(aux.feedback) ? aux.feedback : 0.24f,
            0.0f, 0.96f);
        aux.returnGain = clamp(
            std::isfinite(aux.returnGain) ? aux.returnGain : 0.18f,
            0.0f, 1.0f);
    }
    return params;
}

inline float noInputWrapPhase(float phase)
{
    phase = std::fmod(phase, 1.0f);
    return phase < 0.0f ? phase + 1.0f : phase;
}

constexpr float kNoInputMixerMotionRangeDb = 30.0f;

inline float noInputMixerMotionRateHz(float normalizedRate)
{
    normalizedRate = clamp(std::isfinite(normalizedRate)
        ? normalizedRate : 0.0f, 0.0f, 1.0f);
    return 0.05f * std::pow(100.0f, normalizedRate);
}

inline float noInputMixerMotionRateHz(float normalizedRate, bool slow)
{
    if (!slow) return noInputMixerMotionRateHz(normalizedRate);
    normalizedRate = clamp(std::isfinite(normalizedRate)
        ? normalizedRate : 0.0f, 0.0f, 1.0f);
    return (1.0f / 600.0f) * std::pow(150.0f, normalizedRate);
}

inline float noInputSyncedRateHz(
    uint32_t division, double tempoBpm)
{
    const double tempo = std::clamp(
        std::isfinite(tempoBpm) ? tempoBpm : 120.0, 1.0, 1000.0);
    const double seconds = static_cast<double>(
        noInputClockDivisionBeats(division)) * 60.0 / tempo;
    return static_cast<float>(1.0 / std::max(1.0e-6, seconds));
}

inline float noInputReactThreshold(float normalized)
{
    normalized = clamp(std::isfinite(normalized) ? normalized : 0.24f,
        0.0f, 1.0f);
    return 0.0005f * std::pow(1600.0f, normalized);
}

inline float noInputReactAttackMs(float normalized)
{
    normalized = clamp(std::isfinite(normalized) ? normalized : 0.18f,
        0.0f, 1.0f);
    return 0.5f * std::pow(4000.0f, normalized);
}

inline float noInputReactReleaseMs(float normalized)
{
    normalized = clamp(std::isfinite(normalized) ? normalized : 0.42f,
        0.0f, 1.0f);
    return 5.0f * std::pow(2000.0f, normalized);
}

inline float noInputMixerMotionGainScale(float generatedWeight,
    float activePeakWeight, uint32_t activeRouteCount, float depth)
{
    depth = clamp(std::isfinite(depth) ? depth : 0.0f, 0.0f, 1.0f);
    if (depth <= 1.0e-6f) return 1.0f;
    generatedWeight = clamp(std::isfinite(generatedWeight)
        ? generatedWeight : 0.0f, 0.0f, 1.0f);
    activePeakWeight = clamp(std::isfinite(activePeakWeight)
        ? activePeakWeight : 0.0f, 0.0f, 1.0f);
    const float focusedWeight = activeRouteCount > 1u
        ? generatedWeight / std::max(1.0e-6f, activePeakWeight)
        : generatedWeight;
    const float perceptualWeight = std::pow(
        clamp(focusedWeight, 0.0f, 1.0f), 1.35f);
    const float attenuationDb = -(1.0f - perceptualWeight)
        * depth * kNoInputMixerMotionRangeDb;
    return dbToGain(attenuationDb);
}

// The same generated-flow vocabulary used by the s3g Group Matrix family.
// The caller multiplies these weights by the hand-patched matrix, so a closed
// crosspoint stays closed while an active circuit can move inside its ceiling.
inline std::array<float, kNoInputMixerMatrixCells>
noInputMixerMotionWeights(const NoInputMixerParams& rawParams, float phase)
{
    const auto params = sanitizeNoInputMixerParams(rawParams);
    std::array<float, kNoInputMixerMatrixCells> weights {};
    const float phaseForShape = params.motionShape == MatrixFlowShape::Hold
        ? params.motionPhase
        : noInputWrapPhase(phase + params.motionPhase);
    const bool swirlShape = params.motionShape == MatrixFlowShape::Swirl;
    const float flowAmount = swirlShape
        ? std::max(0.24f, params.flow) : params.flow;
    const float width = 0.045f + params.spread * 1.25f
        + flowAmount * 0.58f;
    const float angle = phaseForShape * 2.0f * kPi;

    if (params.motionShape == MatrixFlowShape::Pulse) {
        for (uint32_t destination = 0u; destination < 8u; ++destination) {
            for (uint32_t source = 0u; source < 8u; ++source) {
                const float offset = static_cast<float>(source + destination)
                    * 0.125f * params.spread;
                weights[destination * 8u + source] =
                    matrixFlowPulse(phaseForShape + offset);
            }
        }
        return weights;
    }

    if (params.motionShape == MatrixFlowShape::Chase) {
        const float chaseWidth = 0.35f + params.spread * 2.4f;
        const float position = phaseForShape * 8.0f;
        for (uint32_t source = 0u; source < 8u; ++source) {
            float sum = 0.0f;
            const float center = std::fmod(
                static_cast<float>(source) + position, 8.0f);
            for (uint32_t destination = 0u; destination < 8u;
                 ++destination) {
                const float value = matrixFlowRingWeight(
                    8u, destination, center, chaseWidth);
                weights[destination * 8u + source] = value;
                sum += value;
            }
            if (sum > 1.0e-6f) {
                for (uint32_t destination = 0u; destination < 8u;
                     ++destination) {
                    weights[destination * 8u + source] /= sum;
                }
            }
        }
        return weights;
    }

    if (params.motionShape == MatrixFlowShape::Scatter) {
        const float step = phaseForShape * 12.0f;
        const uint32_t seedA = static_cast<uint32_t>(std::floor(step));
        const uint32_t seedB = seedA + 1u;
        const float interpolation = 0.5f - 0.5f
            * std::cos((step - std::floor(step)) * kPi);
        const float threshold = 0.74f - params.spread * 0.42f;
        for (uint32_t source = 0u; source < 8u; ++source) {
            float sum = 0.0f;
            for (uint32_t destination = 0u; destination < 8u;
                 ++destination) {
                const float a = matrixFlowHash01(source, destination, seedA);
                const float b = matrixFlowHash01(source, destination, seedB);
                const float hash = lerp(a, b, interpolation);
                const float value = 0.04f + std::max(0.0f,
                    (hash - threshold) / std::max(0.05f, 1.0f - threshold));
                weights[destination * 8u + source] = value;
                sum += value;
            }
            if (sum > 1.0e-6f) {
                for (uint32_t destination = 0u; destination < 8u;
                     ++destination) {
                    weights[destination * 8u + source] /= sum;
                }
            }
        }
        return weights;
    }

    if (params.motionShape == MatrixFlowShape::Bloom) {
        // A shell opens away from each source, reaches the opposite side of
        // the ring, then folds home. VORTEX twists the center of that shell
        // without destroying its two-sided bloom.
        const float bloom = 0.5f - 0.5f * std::cos(angle);
        const float radius = bloom * (0.30f + flowAmount * 3.70f);
        const float shellWidth = 0.16f + params.spread * 1.15f;
        const float twist = std::sin(angle) * params.vortex
            * (0.25f + flowAmount * 0.90f);
        for (uint32_t source = 0u; source < 8u; ++source) {
            float center = std::fmod(
                static_cast<float>(source) + twist, 8.0f);
            if (center < 0.0f) center += 8.0f;
            float sum = 0.0f;
            for (uint32_t destination = 0u; destination < 8u;
                 ++destination) {
                float distance = std::abs(
                    static_cast<float>(destination) - center);
                distance = std::min(distance, 8.0f - distance);
                const float shellDistance = distance - radius;
                const float value = std::exp(-(shellDistance * shellDistance)
                    / std::max(0.001f, shellWidth * shellWidth));
                weights[destination * 8u + source] = value;
                sum += value;
            }
            if (sum > 1.0e-6f) {
                for (uint32_t destination = 0u; destination < 8u;
                     ++destination) {
                    weights[destination * 8u + source] /= sum;
                }
            }
        }
        return weights;
    }

    if (params.motionShape == MatrixFlowShape::Braid) {
        // Two counter-moving strands exchange prominence twice per cycle.
        // Source parity offsets the exchange so adjacent columns interleave
        // rather than moving as one block.
        const float travel = phaseForShape * 8.0f
            * (0.35f + flowAmount * 1.65f);
        const float strandWidth = 0.20f + params.spread * 1.30f;
        for (uint32_t source = 0u; source < 8u; ++source) {
            const float parity = (source & 1u) == 0u ? 0.0f : kPi;
            const float exchange = 0.5f + 0.5f * std::sin(
                angle * 2.0f + parity + params.vortex * kPi * 0.75f);
            float forwardCenter = std::fmod(static_cast<float>(source)
                + travel + params.vortex * 0.65f, 8.0f);
            float reverseCenter = std::fmod(static_cast<float>(source)
                + 4.0f - travel + params.vortex * 0.65f, 8.0f);
            if (forwardCenter < 0.0f) forwardCenter += 8.0f;
            if (reverseCenter < 0.0f) reverseCenter += 8.0f;
            float sum = 0.0f;
            for (uint32_t destination = 0u; destination < 8u;
                 ++destination) {
                const float forward = matrixFlowRingWeight(8u, destination,
                    forwardCenter, strandWidth);
                const float reverse = matrixFlowRingWeight(8u, destination,
                    reverseCenter, strandWidth);
                const float value = forward * (0.18f + exchange * 0.82f)
                    + reverse * (1.0f - exchange * 0.82f);
                weights[destination * 8u + source] = value;
                sum += value;
            }
            if (sum > 1.0e-6f) {
                for (uint32_t destination = 0u; destination < 8u;
                     ++destination) {
                    weights[destination * 8u + source] /= sum;
                }
            }
        }
        return weights;
    }

    if (params.motionShape == MatrixFlowShape::Attract) {
        // A shared attractor travels around the destination ring. FLOW is the
        // pull strength, SPREAD is its halo, and VORTEX adds a source-offset
        // orbit so a fully attracted field still retains internal motion.
        const float direction = params.vortex < -0.001f ? -1.0f : 1.0f;
        const float attractor = phaseForShape * 8.0f * direction;
        const float pull = 0.12f + flowAmount * 0.86f;
        const float halo = 0.18f + params.spread * 1.55f;
        for (uint32_t source = 0u; source < 8u; ++source) {
            float delta = attractor - static_cast<float>(source);
            while (delta > 4.0f) delta -= 8.0f;
            while (delta < -4.0f) delta += 8.0f;
            const float sourceAngle = static_cast<float>(source)
                * kPi * 0.25f;
            const float orbit = std::sin(angle + sourceAngle)
                * params.vortex * (1.0f - pull * 0.55f);
            float center = std::fmod(static_cast<float>(source)
                + delta * pull + orbit, 8.0f);
            if (center < 0.0f) center += 8.0f;
            float sum = 0.0f;
            for (uint32_t destination = 0u; destination < 8u;
                 ++destination) {
                const float value = matrixFlowRingWeight(8u, destination,
                    center, halo);
                weights[destination * 8u + source] = value;
                sum += value;
            }
            if (sum > 1.0e-6f) {
                for (uint32_t destination = 0u; destination < 8u;
                     ++destination) {
                    weights[destination * 8u + source] /= sum;
                }
            }
        }
        return weights;
    }

    const float vortex = params.vortex + (swirlShape ? 1.35f : 0.0f);
    const float orbitX = std::cos(angle) * flowAmount
        * (swirlShape ? 1.12f : 1.0f);
    const float orbitY = std::sin(angle) * flowAmount
        * (swirlShape ? 1.12f : 1.0f);
    for (uint32_t source = 0u; source < 8u; ++source) {
        const float sourceAngle = static_cast<float>(source) * kPi * 0.25f;
        float centerX = std::cos(sourceAngle);
        float centerY = std::sin(sourceAngle);
        const float swirlX = -centerY * vortex * flowAmount;
        const float swirlY = centerX * vortex * flowAmount;
        const float sourcePhase = angle + sourceAngle;
        const float swirlAmplitude = swirlShape ? 0.54f : 0.20f;
        centerX = centerX * (1.0f - 0.32f * flowAmount) + swirlX
            + orbitX * 0.46f + std::cos(sourcePhase) * flowAmount
                * swirlAmplitude;
        centerY = centerY * (1.0f - 0.32f * flowAmount) + swirlY
            + orbitY * 0.46f + std::sin(sourcePhase) * flowAmount
                * swirlAmplitude;
        float sum = 0.0f;
        for (uint32_t destination = 0u; destination < 8u;
             ++destination) {
            const float destinationAngle = static_cast<float>(destination)
                * kPi * 0.25f;
            const float dx = std::cos(destinationAngle) - centerX;
            const float dy = std::sin(destinationAngle) - centerY;
            const float value = std::exp(-(dx * dx + dy * dy)
                / std::max(0.001f, width * width));
            weights[destination * 8u + source] = value;
            sum += value;
        }
        if (sum > 1.0e-6f) {
            for (uint32_t destination = 0u; destination < 8u;
                 ++destination) {
                weights[destination * 8u + source] /= sum;
            }
        }
    }
    return weights;
}

constexpr uint32_t kNoInputMixerFactoryPresetCount = 20u;

inline const char* noInputMixerFactoryPresetName(uint32_t index)
{
    static constexpr std::array<const char*,
        kNoInputMixerFactoryPresetCount> names {{
        "INIT",
        "CIRCUIT LATTICE",
        "RAIN FOREST",
        "WOOL RING",
        "RAT CAGE",
        "ZONE WEB",
        "NEGATIVE SPACE",
        "RELAY BLOOM",
        "OPEN HOUSE",
        "MOBILE CIRCUIT",
        "STATIC CHOIR",
        "RAZOR CLOCK",
        "SUBHARMONIC WELL",
        "SPEECH CIRCUIT",
        "SPLICE STORM",
        "PHASE ORCHARD",
        "LOGIC FLOCK",
        "OCTAVE LADDER",
        "AUX MIRROR",
        "WALL ENGINE",
    }};
    return names[std::min<uint32_t>(index,
        kNoInputMixerFactoryPresetCount - 1u)];
}

inline NoInputMovementBehaviorParams noInputMixerFactoryBehavior(
    uint32_t index)
{
    NoInputMovementBehaviorParams params;
    switch (std::min<uint32_t>(index,
        kNoInputMixerFactoryPresetCount - 1u)) {
    case 1u:
        params.behavior = NoInputMovementBehavior::Step;
        params.eventRate = 0.36f;
        params.density = 0.66f;
        params.slew = 0.18f;
        params.choke = 0.16f;
        break;
    case 2u:
        params.behavior = NoInputMovementBehavior::Erode;
        params.eventRate = 0.24f;
        params.length = 0.78f;
        params.density = 0.62f;
        params.chaos = 0.28f;
        params.slew = 0.48f;
        params.choke = 0.22f;
        break;
    case 3u:
        params.behavior = NoInputMovementBehavior::Cut;
        params.eventRate = 0.48f;
        params.length = 0.42f;
        params.density = 0.70f;
        params.choke = 0.46f;
        break;
    case 4u:
        params.behavior = NoInputMovementBehavior::Burst;
        params.eventRate = 0.76f;
        params.length = 0.14f;
        params.density = 0.46f;
        params.chaos = 0.82f;
        params.slew = 0.04f;
        params.choke = 0.90f;
        break;
    case 5u:
        params.behavior = NoInputMovementBehavior::Scramble;
        params.eventRate = 0.63f;
        params.density = 0.48f;
        params.chaos = 0.68f;
        params.slew = 0.10f;
        params.choke = 0.38f;
        break;
    case 6u:
        params.behavior = NoInputMovementBehavior::Cut;
        params.eventRate = 0.58f;
        params.length = 0.18f;
        params.density = 0.30f;
        params.chaos = 0.56f;
        params.slew = 0.06f;
        params.choke = 0.86f;
        break;
    case 7u:
        params.behavior = NoInputMovementBehavior::Ratchet;
        params.eventRate = 0.50f;
        params.length = 0.38f;
        params.density = 0.58f;
        params.chaos = 0.62f;
        params.choke = 0.58f;
        break;
    case 9u:
        params.behavior = NoInputMovementBehavior::Cascade;
        params.eventRate = 0.44f;
        params.length = 0.58f;
        params.density = 0.62f;
        params.chaos = 0.46f;
        params.choke = 0.24f;
        break;
    case 10u:
        params.behavior = NoInputMovementBehavior::Glide;
        params.eventRate = 0.28f;
        params.density = 0.82f;
        params.slew = 0.44f;
        break;
    case 11u:
        params.behavior = NoInputMovementBehavior::Cut;
        params.eventRate = 0.86f;
        params.length = 0.08f;
        params.density = 0.42f;
        params.chaos = 0.76f;
        params.slew = 0.025f;
        params.choke = 0.94f;
        break;
    case 12u:
        params.behavior = NoInputMovementBehavior::Glide;
        params.eventRate = 0.30f;
        params.density = 0.88f;
        params.slew = 0.50f;
        break;
    case 13u:
        params.behavior = NoInputMovementBehavior::Step;
        params.eventRate = 0.40f;
        params.length = 0.48f;
        params.density = 0.68f;
        params.chaos = 0.32f;
        params.slew = 0.24f;
        params.choke = 0.18f;
        break;
    case 14u:
        params.behavior = NoInputMovementBehavior::Burst;
        params.eventRate = 0.88f;
        params.length = 0.10f;
        params.density = 0.38f;
        params.chaos = 0.92f;
        params.slew = 0.018f;
        params.choke = 0.96f;
        break;
    case 15u:
        params.behavior = NoInputMovementBehavior::Glide;
        params.eventRate = 0.34f;
        params.density = 0.80f;
        params.slew = 0.38f;
        break;
    case 16u:
        params.behavior = NoInputMovementBehavior::Scramble;
        params.eventRate = 0.72f;
        params.length = 0.16f;
        params.density = 0.52f;
        params.chaos = 0.86f;
        params.slew = 0.05f;
        params.choke = 0.72f;
        break;
    case 17u:
        params.behavior = NoInputMovementBehavior::Step;
        params.eventRate = 0.52f;
        params.length = 0.36f;
        params.density = 0.72f;
        params.chaos = 0.30f;
        params.slew = 0.20f;
        params.choke = 0.22f;
        break;
    case 18u:
        params.behavior = NoInputMovementBehavior::Cut;
        params.eventRate = 0.44f;
        params.length = 0.54f;
        params.density = 0.76f;
        params.chaos = 0.24f;
        params.slew = 0.28f;
        params.choke = 0.34f;
        break;
    case 19u:
        params.behavior = NoInputMovementBehavior::Glide;
        params.eventRate = 0.32f;
        params.density = 0.92f;
        params.slew = 0.42f;
        break;
    default:
        break;
    }
    return sanitizeNoInputMovementBehaviorParams(params);
}

inline NoInputMixerParams noInputMixerFactoryPreset(uint32_t index)
{
    index = std::min<uint32_t>(index,
        kNoInputMixerFactoryPresetCount - 1u);
    auto params = defaultNoInputMixerParams();
    if (index == 0u) return params;

    params.matrix.fill(0.0f);
    const auto route = [&](uint32_t destination, uint32_t source,
        float gain) {
        params.matrix[destination * kNoInputMixerChannels + source] = gain;
    };

    switch (index) {
    case 1u: { // Circuit Lattice: signed, phase-bearing mutual excitation.
        params.seed = 0x4c415454u;
        params.feedback = 0.91f;
        params.coupling = 0.68f;
        params.phase = 0.72f;
        params.drift = 0.36f;
        params.formant = 0.44f;
        params.agency = 0.62f;
        params.motion = 0.34f;
        params.motionShape = MatrixFlowShape::Swirl;
        params.vortex = 0.42f;
        params.reactMode = NoInputReactMode::Balance;
        params.reactDepth = 0.34f;
        params.reactThreshold = 0.20f;
        params.reactAttack = 0.14f;
        params.reactRelease = 0.38f;
        for (uint32_t lane = 0u; lane < kNoInputMixerChannels; ++lane) {
            route(lane, lane, 0.92f + 0.008f * static_cast<float>(lane));
            route(lane, (lane + 7u) % 8u,
                (lane & 1u) == 0u ? 0.28f : -0.24f);
            route(lane, (lane + 3u) % 8u,
                (lane % 3u) == 0u ? -0.14f : 0.11f);
            auto& voice = params.lanes[lane];
            voice.body = 0.18f + 0.086f * static_cast<float>(lane);
            voice.loss = 0.27f + 0.035f * static_cast<float>(lane % 4u);
            voice.tuneNote = 40.0f
                + static_cast<float>((lane * 5u) % 24u);
            voice.tuneCents = (lane & 1u) == 0u ? -7.0f : 7.0f;
            voice.pitchLock = 1u;
            voice.inserts[0].type = (lane & 1u) == 0u
                ? NoInputDistortionType::Diode
                : NoInputDistortionType::Ring;
            voice.inserts[0].gain = 0.28f + 0.025f * (lane % 3u);
            voice.inserts[0].tone = 0.34f + 0.07f * (lane % 5u);
            voice.inserts[0].levelDb = -7.5f;
        }
        break;
    }
    case 2u: { // Rain Forest: long resonant bodies with sparse cross-strikes.
        params.seed = 0x5241494eu;
        params.feedback = 0.88f;
        params.coupling = 0.48f;
        params.phase = 0.58f;
        params.drift = 0.52f;
        params.formant = 0.24f;
        params.agency = 0.54f;
        params.space = 0.36f;
        params.motion = 0.28f;
        params.motionShape = MatrixFlowShape::Attract;
        params.reactMode = NoInputReactMode::Edge;
        params.reactDepth = 0.42f;
        params.reactThreshold = 0.16f;
        params.reactAttack = 0.34f;
        params.reactRelease = 0.72f;
        params.slowTime = 1u;
        for (uint32_t lane = 0u; lane < kNoInputMixerChannels; ++lane) {
            route(lane, lane, 0.96f);
            route(lane, (lane + 5u) % 8u,
                (lane & 1u) == 0u ? 0.18f : -0.16f);
            auto& voice = params.lanes[lane];
            voice.body = 0.12f + 0.105f * static_cast<float>(lane);
            voice.loss = 0.18f + 0.022f * static_cast<float>(lane % 3u);
            voice.tuneNote = 36.0f
                + static_cast<float>((lane * 7u) % 29u);
            voice.pitchLock = 1u;
            voice.lowDb = 2.5f - 0.5f * static_cast<float>(lane % 3u);
            voice.highDb = -2.0f + 0.6f * static_cast<float>(lane % 4u);
            voice.inserts[0].type = NoInputDistortionType::Diode;
            voice.inserts[0].gain = 0.17f;
            voice.inserts[0].tone = 0.28f + 0.05f * (lane % 4u);
            voice.inserts[0].levelDb = -5.5f;
        }
        break;
    }
    case 3u: { // Wool Ring: compressed walls feeding ring-modulated returns.
        params.seed = 0x574f4f4cu;
        params.feedback = 0.90f;
        params.coupling = 0.57f;
        params.phase = 0.31f;
        params.drift = 0.22f;
        params.formant = 0.37f;
        params.motion = 0.26f;
        params.motionShape = MatrixFlowShape::Chase;
        params.clockSync = 1u;
        params.fieldDivision = 5u;
        params.eventDivision = 3u;
        params.reactMode = NoInputReactMode::Follow;
        params.reactDepth = 0.24f;
        for (uint32_t lane = 0u; lane < kNoInputMixerChannels; ++lane) {
            route(lane, lane, 0.94f);
            route(lane, (lane + 1u) % 8u,
                (lane & 1u) == 0u ? 0.22f : -0.18f);
            auto& voice = params.lanes[lane];
            voice.body = 0.24f + 0.067f * static_cast<float>(lane);
            voice.loss = 0.34f;
            voice.inserts[0].type = NoInputDistortionType::Wool;
            voice.inserts[0].gain = 0.46f + 0.025f * (lane % 3u);
            voice.inserts[0].tone = 0.24f + 0.085f * (lane % 5u);
            voice.inserts[0].levelDb = -11.0f;
            voice.inserts[1].type = NoInputDistortionType::Ring;
            voice.inserts[1].gain = 0.20f + 0.025f * (lane % 4u);
            voice.inserts[1].tone = 0.43f;
            voice.inserts[1].levelDb = -4.0f;
            voice.inserts[1].bypass = 0u;
        }
        break;
    }
    case 4u: { // Rat Cage: bright hard-clipped cells in a directed ring.
        params.seed = 0x52415443u;
        params.feedback = 0.93f;
        params.coupling = 0.63f;
        params.phase = 0.46f;
        params.drift = 0.15f;
        params.formant = 0.31f;
        params.agency = 0.42f;
        params.motion = 0.18f;
        params.motionShape = MatrixFlowShape::Pulse;
        params.clockSync = 1u;
        params.fieldDivision = 3u;
        params.eventDivision = 1u;
        params.reactMode = NoInputReactMode::Edge;
        params.reactDepth = 0.70f;
        params.reactThreshold = 0.27f;
        params.reactAttack = 0.05f;
        params.reactRelease = 0.18f;
        for (uint32_t lane = 0u; lane < kNoInputMixerChannels; ++lane) {
            route(lane, lane, 0.91f);
            route(lane, (lane + 7u) % 8u, 0.27f);
            if ((lane & 1u) != 0u) route(lane, (lane + 4u) % 8u, -0.13f);
            auto& voice = params.lanes[lane];
            voice.body = 0.36f + 0.048f * static_cast<float>(lane % 5u);
            voice.loss = 0.42f;
            voice.midGainDb = 3.0f;
            voice.midFrequencyHz = 720.0f + 260.0f * lane;
            voice.inserts[0].type = NoInputDistortionType::Rat;
            voice.inserts[0].gain = 0.38f + 0.025f * (lane % 4u);
            voice.inserts[0].tone = 0.42f + 0.06f * (lane % 3u);
            voice.inserts[0].levelDb = -9.0f;
        }
        break;
    }
    case 5u: { // Zone Web: alternating focused and wide high-gain cells.
        params.seed = 0x5a4f4e45u;
        params.feedback = 0.89f;
        params.coupling = 0.72f;
        params.phase = 0.39f;
        params.drift = 0.28f;
        params.formant = 0.46f;
        params.motion = 0.38f;
        params.motionShape = MatrixFlowShape::Flow;
        params.vortex = -0.34f;
        params.reactMode = NoInputReactMode::Balance;
        params.reactDepth = 0.56f;
        params.reactThreshold = 0.22f;
        params.reactPolarity = -0.32f;
        for (uint32_t lane = 0u; lane < kNoInputMixerChannels; ++lane) {
            route(lane, lane, 0.93f);
            route(lane, (lane + 2u) % 8u,
                (lane & 1u) == 0u ? 0.25f : -0.21f);
            route(lane, (lane + 5u) % 8u, 0.10f);
            auto& voice = params.lanes[lane];
            voice.body = 0.20f + 0.075f * static_cast<float>(lane);
            voice.loss = 0.38f + 0.025f * static_cast<float>(lane % 3u);
            voice.midGainDb = (lane & 1u) == 0u ? 5.0f : -3.5f;
            voice.midFrequencyHz = 580.0f + 410.0f * lane;
            voice.inserts[0].type = (lane & 1u) == 0u
                ? NoInputDistortionType::ZoneA
                : NoInputDistortionType::ZoneB;
            voice.inserts[0].gain = 0.36f;
            voice.inserts[0].tone = 0.31f + 0.07f * (lane % 5u);
            voice.inserts[0].bias = (lane & 1u) == 0u ? 0.08f : -0.10f;
            voice.inserts[0].levelDb = -10.0f;
        }
        break;
    }
    case 6u: { // Negative Space: inhibitory routes dominate the ecology.
        params.seed = 0x4e454753u;
        params.feedback = 0.94f;
        params.coupling = 0.76f;
        params.phase = 0.67f;
        params.drift = 0.33f;
        params.formant = 0.18f;
        params.space = 0.68f;
        params.agency = 0.74f;
        params.motion = 0.30f;
        params.motionShape = MatrixFlowShape::Hold;
        params.reactMode = NoInputReactMode::Avoid;
        params.reactDepth = 0.68f;
        params.reactThreshold = 0.18f;
        params.reactAttack = 0.08f;
        params.reactRelease = 0.44f;
        for (uint32_t lane = 0u; lane < kNoInputMixerChannels; ++lane) {
            route(lane, lane, 0.95f);
            route(lane, (lane + 1u) % 8u, -0.31f);
            route(lane, (lane + 4u) % 8u,
                (lane & 1u) == 0u ? -0.17f : 0.12f);
            auto& voice = params.lanes[lane];
            voice.body = 0.15f + 0.09f * static_cast<float>(lane);
            voice.loss = 0.26f + 0.03f * static_cast<float>(lane % 4u);
            voice.inserts[0].type = NoInputDistortionType::Diode;
            voice.inserts[0].gain = 0.22f;
            voice.inserts[0].bias = (lane & 1u) == 0u ? -0.14f : 0.14f;
            voice.inserts[0].levelDb = -5.0f;
        }
        break;
    }
    case 7u: { // Relay Bloom: gated fuzz cells open into diode recovery.
        params.seed = 0x52454c59u;
        params.feedback = 0.92f;
        params.coupling = 0.54f;
        params.phase = 0.24f;
        params.drift = 0.44f;
        params.formant = 0.55f;
        params.agency = 0.70f;
        params.space = 0.28f;
        params.motion = 0.46f;
        params.motionShape = MatrixFlowShape::Bloom;
        params.reactMode = NoInputReactMode::Edge;
        params.reactDepth = 0.62f;
        params.reactThreshold = 0.32f;
        params.reactAttack = 0.04f;
        params.reactRelease = 0.22f;
        for (uint32_t lane = 0u; lane < kNoInputMixerChannels; ++lane) {
            route(lane, lane, 0.96f);
            route(lane, (lane + 3u) % 8u,
                (lane % 3u) == 0u ? -0.20f : 0.17f);
            auto& voice = params.lanes[lane];
            voice.body = 0.27f + 0.055f * static_cast<float>(lane);
            voice.loss = 0.30f + 0.04f * static_cast<float>(lane % 3u);
            voice.highDb = 2.0f;
            voice.inserts[0].type = (lane & 1u) == 0u
                ? NoInputDistortionType::FuzzII
                : NoInputDistortionType::FuzzI;
            voice.inserts[0].gain = 0.34f + 0.025f * (lane % 4u);
            voice.inserts[0].tone = 0.38f + 0.05f * (lane % 4u);
            voice.inserts[0].levelDb = -8.5f;
            voice.inserts[1].type = NoInputDistortionType::Diode;
            voice.inserts[1].gain = 0.16f;
            voice.inserts[1].tone = 0.62f;
            voice.inserts[1].levelDb = -3.0f;
            voice.inserts[1].bypass = 0u;
        }
        break;
    }
    case 8u: { // Open House: restrained channels, strong return practice.
        params.seed = 0x4e414b41u;
        params.feedback = 0.90f;
        params.coupling = 0.39f;
        params.phase = 0.27f;
        params.drift = 0.31f;
        params.formant = 0.16f;
        params.agency = 0.82f;
        params.space = 0.76f;
        params.internalTone = 0.18f;
        params.houseTone = -0.34f;
        params.motion = 0.24f;
        params.motionShape = MatrixFlowShape::Hold;
        params.slowTime = 1u;
        params.reactMode = NoInputReactMode::Follow;
        params.reactDepth = 0.28f;
        params.reactThreshold = 0.14f;
        params.reactAttack = 0.40f;
        params.reactRelease = 0.76f;
        params.aux[0].effect.type = NoInputDistortionType::Diode;
        params.aux[0].effect.gain = 0.24f;
        params.aux[0].feedback = 0.48f;
        params.aux[0].returnGain = 0.32f;
        params.aux[1].effect.type = NoInputDistortionType::Ring;
        params.aux[1].effect.gain = 0.34f;
        params.aux[1].feedback = 0.37f;
        params.aux[1].returnGain = 0.25f;
        for (uint32_t lane = 0u; lane < kNoInputMixerChannels; ++lane) {
            route(lane, lane, 0.95f);
            if ((lane & 1u) == 0u) {
                route(lane, (lane + 5u) % 8u, -0.13f);
            }
            auto& voice = params.lanes[lane];
            voice.body = 0.16f + 0.087f * static_cast<float>(lane);
            voice.loss = 0.22f + 0.035f * static_cast<float>(lane % 4u);
            voice.levelDb = -6.0f;
            voice.auxSend[0] = (lane % 3u) == 0u ? 0.42f : 0.14f;
            voice.auxSend[1] = (lane % 3u) == 1u ? 0.36f : 0.09f;
            voice.tuneNote = 33.0f
                + static_cast<float>((lane * 5u) % 31u);
            voice.tuneCents = (lane % 3u) == 0u ? -12.0f : 0.0f;
            voice.pitchLock = 1u;
            voice.auxTap[0] = (lane & 1u) == 0u
                ? NoInputAuxTap::PreEq : NoInputAuxTap::PostEq;
            voice.auxTap[1] = (lane & 1u) == 0u
                ? NoInputAuxTap::PostInsert : NoInputAuxTap::Return;
            voice.auxReturn[0] = 0.24f
                + 0.05f * static_cast<float>(lane % 3u);
            voice.auxReturn[1] = (lane & 1u) == 0u
                ? -0.30f : 0.34f;
            voice.inserts[0].type = NoInputDistortionType::Diode;
            voice.inserts[0].gain = 0.18f;
            voice.inserts[0].levelDb = -4.0f;
            voice.inserts[1].bypass = 1u;
            voice.inserts[2].bypass = 1u;
        }
        break;
    }
    case 9u: { // Mobile Circuit: a moving, signed network of nonlinear cells.
        params.seed = 0x4d4f4249u;
        params.feedback = 0.93f;
        params.coupling = 0.78f;
        params.phase = 0.74f;
        params.drift = 0.48f;
        params.formant = 0.52f;
        params.agency = 0.88f;
        params.space = 0.22f;
        params.internalTone = 0.28f;
        params.houseTone = -0.12f;
        params.flow = 0.72f;
        params.spread = 0.58f;
        params.vortex = 0.76f;
        params.motion = 0.72f;
        params.motionShape = MatrixFlowShape::Braid;
        params.motionRate = 0.22f;
        params.clockSync = 1u;
        params.fieldDivision = 4u;
        params.eventDivision = 2u;
        params.reactMode = NoInputReactMode::Balance;
        params.reactDepth = 0.72f;
        params.reactThreshold = 0.24f;
        params.reactAttack = 0.10f;
        params.reactRelease = 0.30f;
        params.reactPolarity = 0.46f;
        params.aux[0].effect.type = NoInputDistortionType::Wool;
        params.aux[0].effect.gain = 0.32f;
        params.aux[0].feedback = 0.34f;
        params.aux[0].returnGain = 0.22f;
        params.aux[1].effect.type = NoInputDistortionType::Rat;
        params.aux[1].effect.gain = 0.28f;
        params.aux[1].feedback = 0.29f;
        params.aux[1].returnGain = 0.20f;
        for (uint32_t lane = 0u; lane < kNoInputMixerChannels; ++lane) {
            route(lane, lane, 0.92f);
            route(lane, (lane + 7u) % 8u,
                (lane & 1u) == 0u ? 0.27f : -0.24f);
            route(lane, (lane + 3u) % 8u,
                (lane & 2u) == 0u ? -0.16f : 0.12f);
            auto& voice = params.lanes[lane];
            voice.body = 0.14f + 0.095f * static_cast<float>(lane);
            voice.loss = 0.25f + 0.028f * static_cast<float>(lane % 4u);
            voice.auxSend[0] = 0.16f + 0.04f * static_cast<float>(lane % 3u);
            voice.auxSend[1] = 0.12f + 0.03f * static_cast<float>((lane + 1u) % 4u);
            voice.inserts[0].type = (lane & 1u) == 0u
                ? NoInputDistortionType::Wool : NoInputDistortionType::Rat;
            voice.inserts[0].gain = 0.29f + 0.025f * (lane % 4u);
            voice.inserts[0].levelDb = -8.0f;
            voice.inserts[1].type = (lane & 1u) == 0u
                ? NoInputDistortionType::Ring : NoInputDistortionType::ZoneA;
            voice.inserts[1].gain = 0.18f;
            voice.inserts[1].levelDb = -5.0f;
            voice.inserts[1].bypass = 0u;
        }
        break;
    }
    case 10u: { // Static Choir: tuned voices breathing through vocal cells.
        params.seed = 0x43484f52u;
        params.outputGainDb = -14.0f;
        params.feedback = 0.92f;
        params.coupling = 0.46f;
        params.phase = 0.58f;
        params.drift = 0.14f;
        params.formant = 0.72f;
        params.agency = 0.38f;
        params.space = 0.20f;
        params.flow = 0.34f;
        params.spread = 0.76f;
        params.motion = 0.28f;
        params.motionShape = MatrixFlowShape::Flow;
        params.motionRate = 0.72f;
        params.slowTime = 1u;
        params.reactMode = NoInputReactMode::Follow;
        params.reactDepth = 0.18f;
        params.reactThreshold = 0.10f;
        params.reactAttack = 0.42f;
        params.reactRelease = 0.74f;
        params.aux[0].effect.type = NoInputDistortionType::Chorus;
        params.aux[0].effect.gain = 0.34f;
        params.aux[0].effect.tone = 0.30f;
        params.aux[0].feedback = 0.24f;
        params.aux[0].returnGain = 0.28f;
        params.aux[1].effect.type = NoInputDistortionType::Phase;
        params.aux[1].effect.gain = 0.28f;
        params.aux[1].effect.tone = 0.58f;
        params.aux[1].feedback = 0.18f;
        params.aux[1].returnGain = 0.22f;
        static constexpr std::array<float, 8u> notes {{
            36.0f, 43.0f, 48.0f, 55.0f,
            60.0f, 67.0f, 72.0f, 79.0f,
        }};
        for (uint32_t lane = 0u; lane < kNoInputMixerChannels; ++lane) {
            route(lane, lane, 0.96f);
            route(lane, (lane + 4u) % 8u,
                (lane & 1u) == 0u ? 0.13f : -0.11f);
            route(lane, (lane + 2u) % 8u, 0.07f);
            auto& voice = params.lanes[lane];
            voice.body = 0.14f + 0.096f * static_cast<float>(lane);
            voice.loss = 0.20f + 0.025f * static_cast<float>(lane % 4u);
            voice.levelDb = -2.5f;
            voice.tuneNote = notes[lane];
            voice.tuneCents = (lane & 1u) == 0u ? -5.0f : 5.0f;
            voice.pitchLock = 1u;
            voice.midGainDb = 2.5f;
            voice.midFrequencyHz = 360.0f + 190.0f * lane;
            voice.auxSend[0] = 0.20f + 0.03f * (lane % 3u);
            voice.auxSend[1] = 0.12f + 0.025f * ((lane + 1u) % 3u);
            voice.auxTap[0] = NoInputAuxTap::PostEq;
            voice.auxTap[1] = NoInputAuxTap::PostInsert;
            voice.auxReturn[0] = 0.30f;
            voice.auxReturn[1] = (lane & 1u) == 0u ? -0.18f : 0.22f;
            voice.inserts[0].type = NoInputDistortionType::Throat;
            voice.inserts[0].gain = 0.28f;
            voice.inserts[0].tone = 0.18f + 0.09f * (lane % 5u);
            voice.inserts[0].levelDb = -4.0f;
            voice.inserts[1].type = (lane & 1u) == 0u
                ? NoInputDistortionType::Chorus
                : NoInputDistortionType::OctStack;
            voice.inserts[1].gain = 0.18f;
            voice.inserts[1].tone = 0.44f;
            voice.inserts[1].levelDb = -3.0f;
            voice.inserts[1].bypass = 0u;
        }
        break;
    }
    case 11u: { // Razor Clock: hard logic under rapid synchronized cuts.
        params.seed = 0x525a434bu;
        params.outputGainDb = -13.0f;
        params.feedback = 0.98f;
        params.coupling = 0.84f;
        params.phase = 0.24f;
        params.drift = 0.18f;
        params.formant = 0.10f;
        params.agency = 0.94f;
        params.space = 0.05f;
        params.flow = 0.86f;
        params.spread = 0.34f;
        params.vortex = -0.48f;
        params.motion = 0.94f;
        params.motionShape = MatrixFlowShape::Pulse;
        params.clockSync = 1u;
        params.fieldDivision = 1u;
        params.eventDivision = 0u;
        params.reactMode = NoInputReactMode::Edge;
        params.reactDepth = 0.38f;
        params.reactThreshold = 0.12f;
        params.reactAttack = 0.025f;
        params.reactRelease = 0.14f;
        params.aux[0].effect.type = NoInputDistortionType::Crush;
        params.aux[0].effect.gain = 0.52f;
        params.aux[0].feedback = 0.42f;
        params.aux[0].returnGain = 0.24f;
        params.aux[1].effect.type = NoInputDistortionType::Logic;
        params.aux[1].effect.gain = 0.44f;
        params.aux[1].feedback = 0.34f;
        params.aux[1].returnGain = 0.20f;
        for (uint32_t lane = 0u; lane < kNoInputMixerChannels; ++lane) {
            route(lane, lane, 0.97f);
            route(lane, (lane + 7u) % 8u, 0.34f);
            route(lane, (lane + 3u) % 8u, -0.24f);
            route(lane, (lane + 5u) % 8u,
                (lane & 1u) == 0u ? 0.16f : -0.15f);
            auto& voice = params.lanes[lane];
            voice.body = 0.30f + 0.045f * (lane % 5u);
            voice.loss = 0.38f + 0.025f * (lane % 3u);
            voice.levelDb = -4.0f;
            voice.highDb = 4.0f;
            voice.auxSend[0] = 0.18f + 0.035f * (lane % 4u);
            voice.auxSend[1] = 0.12f + 0.025f * ((lane + 2u) % 4u);
            voice.inserts[0].type = (lane % 3u) == 0u
                ? NoInputDistortionType::Logic
                : ((lane % 3u) == 1u ? NoInputDistortionType::Shred
                    : NoInputDistortionType::Crush);
            voice.inserts[0].gain = 0.48f + 0.035f * (lane % 4u);
            voice.inserts[0].tone = 0.30f + 0.08f * (lane % 4u);
            voice.inserts[0].bias = (lane & 1u) == 0u ? 0.10f : -0.10f;
            voice.inserts[0].levelDb = -9.0f;
            voice.inserts[1].type = NoInputDistortionType::Diode;
            voice.inserts[1].gain = 0.24f;
            voice.inserts[1].levelDb = -4.0f;
            voice.inserts[1].bypass = 0u;
        }
        break;
    }
    case 12u: { // Subharmonic Well: low tuned returns and octave division.
        params.seed = 0x53554257u;
        params.outputGainDb = -11.0f;
        params.feedback = 0.91f;
        params.coupling = 0.34f;
        params.phase = 0.68f;
        params.drift = 0.20f;
        params.formant = 0.26f;
        params.agency = 0.42f;
        params.space = 0.30f;
        params.internalTone = -0.42f;
        params.houseTone = -0.36f;
        params.flow = 0.28f;
        params.spread = 0.68f;
        params.motion = 0.38f;
        params.motionShape = MatrixFlowShape::Flow;
        params.motionRate = 0.70f;
        params.slowTime = 1u;
        params.reactMode = NoInputReactMode::Balance;
        params.reactDepth = 0.16f;
        params.reactThreshold = 0.08f;
        params.reactAttack = 0.44f;
        params.reactRelease = 0.78f;
        params.aux[0].effect.type = NoInputDistortionType::Rotor;
        params.aux[0].effect.gain = 0.28f;
        params.aux[0].effect.tone = 0.16f;
        params.aux[0].feedback = 0.32f;
        params.aux[0].returnGain = 0.30f;
        params.aux[1].effect.type = NoInputDistortionType::OctStack;
        params.aux[1].effect.gain = 0.26f;
        params.aux[1].effect.tone = 0.30f;
        params.aux[1].feedback = 0.24f;
        params.aux[1].returnGain = 0.24f;
        for (uint32_t lane = 0u; lane < kNoInputMixerChannels; ++lane) {
            route(lane, lane, 0.97f);
            route(lane, (lane + 4u) % 8u,
                (lane & 1u) == 0u ? 0.12f : -0.10f);
            auto& voice = params.lanes[lane];
            voice.body = 0.08f + 0.065f * lane;
            voice.loss = 0.16f + 0.018f * (lane % 4u);
            voice.levelDb = -1.5f;
            voice.lowDb = 5.0f;
            voice.midGainDb = -2.5f;
            voice.highDb = -7.0f;
            voice.tuneNote = 24.0f + 3.0f * lane;
            voice.tuneCents = (lane & 1u) == 0u ? -9.0f : 6.0f;
            voice.pitchLock = 1u;
            voice.auxSend[0] = 0.16f + 0.025f * (lane % 3u);
            voice.auxSend[1] = 0.20f + 0.03f * ((lane + 1u) % 3u);
            voice.auxTap[0] = NoInputAuxTap::PreEq;
            voice.auxTap[1] = NoInputAuxTap::PostInsert;
            voice.auxReturn[0] = 0.26f;
            voice.auxReturn[1] = 0.22f;
            voice.inserts[0].type = (lane & 1u) == 0u
                ? NoInputDistortionType::OctDown
                : NoInputDistortionType::OctStack;
            voice.inserts[0].gain = 0.30f;
            voice.inserts[0].tone = 0.26f + 0.04f * (lane % 4u);
            voice.inserts[0].levelDb = -3.0f;
            voice.inserts[1].type = NoInputDistortionType::Wool;
            voice.inserts[1].gain = 0.14f;
            voice.inserts[1].levelDb = -5.0f;
            voice.inserts[1].bypass = 0u;
        }
        break;
    }
    case 13u: { // Speech Circuit: formant voices cross-modulate the matrix.
        params.seed = 0x53504543u;
        params.outputGainDb = -13.0f;
        params.feedback = 0.92f;
        params.coupling = 0.62f;
        params.phase = 0.50f;
        params.drift = 0.34f;
        params.formant = 0.88f;
        params.agency = 0.74f;
        params.space = 0.18f;
        params.internalTone = 0.18f;
        params.houseTone = 0.10f;
        params.flow = 0.58f;
        params.spread = 0.46f;
        params.vortex = 0.30f;
        params.motion = 0.52f;
        params.motionShape = MatrixFlowShape::Swirl;
        params.motionRate = 0.36f;
        params.reactMode = NoInputReactMode::Balance;
        params.reactDepth = 0.48f;
        params.reactThreshold = 0.18f;
        params.reactAttack = 0.16f;
        params.reactRelease = 0.46f;
        params.aux[0].effect.type = NoInputDistortionType::Throat;
        params.aux[0].effect.gain = 0.38f;
        params.aux[0].effect.tone = 0.72f;
        params.aux[0].feedback = 0.26f;
        params.aux[0].returnGain = 0.28f;
        params.aux[1].effect.type = NoInputDistortionType::Robot;
        params.aux[1].effect.gain = 0.32f;
        params.aux[1].effect.tone = 0.42f;
        params.aux[1].feedback = 0.22f;
        params.aux[1].returnGain = 0.22f;
        for (uint32_t lane = 0u; lane < kNoInputMixerChannels; ++lane) {
            route(lane, lane, 0.94f);
            route(lane, (lane + 1u) % 8u, 0.19f);
            route(lane, (lane + 5u) % 8u,
                (lane & 1u) == 0u ? -0.15f : 0.13f);
            auto& voice = params.lanes[lane];
            voice.body = 0.18f + 0.075f * lane;
            voice.loss = 0.25f + 0.03f * (lane % 4u);
            voice.midFrequencyHz = 300.0f + 420.0f * lane;
            voice.midGainDb = (lane & 1u) == 0u ? 5.0f : -2.0f;
            voice.highDb = 2.0f;
            voice.auxSend[0] = 0.20f + 0.04f * (lane % 3u);
            voice.auxSend[1] = 0.14f + 0.035f * ((lane + 1u) % 3u);
            voice.auxTap[0] = NoInputAuxTap::PostEq;
            voice.auxTap[1] = NoInputAuxTap::PostInsert;
            voice.auxReturn[0] = 0.30f;
            voice.auxReturn[1] = (lane & 1u) == 0u ? -0.22f : 0.25f;
            voice.inserts[0].type = (lane & 1u) == 0u
                ? NoInputDistortionType::Throat
                : NoInputDistortionType::Robot;
            voice.inserts[0].gain = 0.34f;
            voice.inserts[0].tone = 0.12f + 0.105f * lane;
            voice.inserts[0].levelDb = -5.0f;
            voice.inserts[1].type = NoInputDistortionType::Ring;
            voice.inserts[1].gain = 0.16f;
            voice.inserts[1].tone = 0.52f;
            voice.inserts[1].levelDb = -3.0f;
            voice.inserts[1].bypass = 0u;
        }
        break;
    }
    case 14u: { // Splice Storm: buffer cuts and destructive short events.
        params.seed = 0x53504c43u;
        params.outputGainDb = -14.0f;
        params.feedback = 0.97f;
        params.coupling = 0.78f;
        params.phase = 0.32f;
        params.drift = 0.26f;
        params.formant = 0.18f;
        params.agency = 0.92f;
        params.space = 0.08f;
        params.flow = 0.88f;
        params.spread = 0.72f;
        params.vortex = -0.66f;
        params.motion = 0.96f;
        params.motionShape = MatrixFlowShape::Scatter;
        params.clockSync = 1u;
        params.fieldDivision = 2u;
        params.eventDivision = 0u;
        params.reactMode = NoInputReactMode::Edge;
        params.reactDepth = 0.44f;
        params.reactThreshold = 0.16f;
        params.reactAttack = 0.02f;
        params.reactRelease = 0.16f;
        params.aux[0].effect.type = NoInputDistortionType::Splice;
        params.aux[0].effect.gain = 0.52f;
        params.aux[0].effect.tone = 0.20f;
        params.aux[0].feedback = 0.46f;
        params.aux[0].returnGain = 0.24f;
        params.aux[1].effect.type = NoInputDistortionType::Void;
        params.aux[1].effect.gain = 0.42f;
        params.aux[1].effect.tone = 0.68f;
        params.aux[1].feedback = 0.38f;
        params.aux[1].returnGain = 0.20f;
        for (uint32_t lane = 0u; lane < kNoInputMixerChannels; ++lane) {
            route(lane, lane, 0.96f);
            route(lane, (lane + 7u) % 8u, 0.30f);
            route(lane, (lane + 2u) % 8u, -0.22f);
            route(lane, (lane + 5u) % 8u,
                (lane & 1u) == 0u ? 0.14f : -0.13f);
            auto& voice = params.lanes[lane];
            voice.body = 0.24f + 0.05f * (lane % 5u);
            voice.loss = 0.32f + 0.025f * (lane % 3u);
            voice.levelDb = -4.5f;
            voice.auxSend[0] = 0.22f + 0.04f * (lane % 4u);
            voice.auxSend[1] = 0.14f + 0.03f * ((lane + 2u) % 4u);
            voice.auxTap[0] = NoInputAuxTap::PreEq;
            voice.auxTap[1] = NoInputAuxTap::PostInsert;
            voice.auxReturn[0] = (lane & 1u) == 0u ? 0.34f : -0.28f;
            voice.auxReturn[1] = (lane & 2u) == 0u ? -0.24f : 0.26f;
            voice.inserts[0].type = NoInputDistortionType::Splice;
            voice.inserts[0].gain = 0.42f + 0.04f * (lane % 4u);
            voice.inserts[0].tone = 0.10f + 0.08f * (lane % 5u);
            voice.inserts[0].bias = (lane & 1u) == 0u ? -0.50f : 0.50f;
            voice.inserts[0].levelDb = -7.0f;
            voice.inserts[1].type = NoInputDistortionType::Shred;
            voice.inserts[1].gain = 0.30f;
            voice.inserts[1].tone = 0.58f;
            voice.inserts[1].levelDb = -5.0f;
            voice.inserts[1].bypass = 0u;
            voice.inserts[2].type = NoInputDistortionType::Crush;
            voice.inserts[2].gain = 0.22f;
            voice.inserts[2].tone = 0.40f;
            voice.inserts[2].levelDb = -3.0f;
            voice.inserts[2].bypass = (lane & 1u) == 0u ? 0u : 1u;
        }
        break;
    }
    case 15u: { // Phase Orchard: slowly detuned modulation branches.
        params.seed = 0x50484f52u;
        params.outputGainDb = -13.0f;
        params.feedback = 0.92f;
        params.coupling = 0.54f;
        params.phase = 0.90f;
        params.drift = 0.46f;
        params.formant = 0.28f;
        params.agency = 0.56f;
        params.space = 0.26f;
        params.flow = 0.48f;
        params.spread = 0.82f;
        params.vortex = 0.54f;
        params.motion = 0.46f;
        params.motionShape = MatrixFlowShape::Swirl;
        params.motionRate = 0.74f;
        params.slowTime = 1u;
        params.reactMode = NoInputReactMode::Follow;
        params.reactDepth = 0.22f;
        params.reactThreshold = 0.12f;
        params.reactAttack = 0.34f;
        params.reactRelease = 0.70f;
        params.aux[0].effect.type = NoInputDistortionType::Chorus;
        params.aux[0].effect.gain = 0.42f;
        params.aux[0].effect.tone = 0.22f;
        params.aux[0].feedback = 0.30f;
        params.aux[0].returnGain = 0.30f;
        params.aux[1].effect.type = NoInputDistortionType::Phase;
        params.aux[1].effect.gain = 0.46f;
        params.aux[1].effect.tone = 0.64f;
        params.aux[1].feedback = 0.28f;
        params.aux[1].returnGain = 0.26f;
        for (uint32_t lane = 0u; lane < kNoInputMixerChannels; ++lane) {
            route(lane, lane, 0.95f);
            route(lane, (lane + 1u) % 8u,
                (lane & 1u) == 0u ? 0.17f : -0.15f);
            route(lane, (lane + 4u) % 8u, 0.09f);
            auto& voice = params.lanes[lane];
            voice.body = 0.16f + 0.085f * lane;
            voice.loss = 0.22f + 0.035f * (lane % 4u);
            voice.tuneNote = 38.0f + 5.0f * lane;
            voice.tuneCents = (lane & 1u) == 0u ? -14.0f : 11.0f;
            voice.pitchLock = 1u;
            voice.auxSend[0] = 0.20f;
            voice.auxSend[1] = 0.18f;
            voice.auxReturn[0] = 0.28f;
            voice.auxReturn[1] = (lane & 1u) == 0u ? -0.20f : 0.24f;
            voice.inserts[0].type = static_cast<NoInputDistortionType>(
                static_cast<uint32_t>(NoInputDistortionType::Rotor)
                    + lane % 3u);
            voice.inserts[0].gain = 0.30f;
            voice.inserts[0].tone = 0.16f + 0.09f * (lane % 5u);
            voice.inserts[0].levelDb = -4.0f;
            voice.inserts[1].type = NoInputDistortionType::Chorus;
            voice.inserts[1].gain = 0.18f;
            voice.inserts[1].tone = 0.48f;
            voice.inserts[1].levelDb = -3.0f;
            voice.inserts[1].bypass = 0u;
        }
        break;
    }
    case 16u: { // Logic Flock: relay and logic cells form unstable clusters.
        params.seed = 0x4c464c4bu;
        params.outputGainDb = -14.0f;
        params.feedback = 0.96f;
        params.coupling = 0.82f;
        params.phase = 0.20f;
        params.drift = 0.58f;
        params.formant = 0.34f;
        params.agency = 0.96f;
        params.space = 0.14f;
        params.flow = 0.82f;
        params.spread = 0.62f;
        params.vortex = 0.72f;
        params.motion = 0.86f;
        params.motionShape = MatrixFlowShape::Scatter;
        params.motionRate = 0.62f;
        params.reactMode = NoInputReactMode::Edge;
        params.reactDepth = 0.54f;
        params.reactThreshold = 0.20f;
        params.reactAttack = 0.04f;
        params.reactRelease = 0.24f;
        params.aux[0].effect.type = NoInputDistortionType::Logic;
        params.aux[0].effect.gain = 0.40f;
        params.aux[0].feedback = 0.38f;
        params.aux[0].returnGain = 0.24f;
        params.aux[1].effect.type = NoInputDistortionType::Crush;
        params.aux[1].effect.gain = 0.34f;
        params.aux[1].feedback = 0.32f;
        params.aux[1].returnGain = 0.22f;
        for (uint32_t lane = 0u; lane < kNoInputMixerChannels; ++lane) {
            route(lane, lane, 0.96f);
            route(lane, (lane + 7u) % 8u,
                (lane & 1u) == 0u ? 0.29f : -0.27f);
            route(lane, (lane + 2u) % 8u,
                (lane % 3u) == 0u ? -0.20f : 0.18f);
            route(lane, (lane + 5u) % 8u, 0.11f);
            auto& voice = params.lanes[lane];
            voice.body = 0.22f + 0.06f * (lane % 6u);
            voice.loss = 0.30f + 0.035f * (lane % 4u);
            voice.levelDb = -4.0f;
            voice.auxSend[0] = 0.18f + 0.035f * (lane % 4u);
            voice.auxSend[1] = 0.14f + 0.03f * ((lane + 1u) % 4u);
            voice.inserts[0].type = (lane % 3u) == 0u
                ? NoInputDistortionType::Logic
                : ((lane % 3u) == 1u ? NoInputDistortionType::Relay
                    : NoInputDistortionType::Robot);
            voice.inserts[0].gain = 0.38f + 0.04f * (lane % 4u);
            voice.inserts[0].tone = 0.26f + 0.07f * (lane % 5u);
            voice.inserts[0].bias = (lane & 1u) == 0u ? -0.16f : 0.16f;
            voice.inserts[0].levelDb = -7.0f;
            voice.inserts[1].type = NoInputDistortionType::Diode;
            voice.inserts[1].gain = 0.18f;
            voice.inserts[1].levelDb = -3.0f;
            voice.inserts[1].bypass = 0u;
        }
        break;
    }
    case 17u: { // Octave Ladder: pitch-locked lanes exchange octave voices.
        params.seed = 0x4f43544cu;
        params.outputGainDb = -12.0f;
        params.feedback = 0.92f;
        params.coupling = 0.60f;
        params.phase = 0.46f;
        params.drift = 0.18f;
        params.formant = 0.40f;
        params.agency = 0.62f;
        params.space = 0.20f;
        params.flow = 0.68f;
        params.spread = 0.42f;
        params.motion = 0.68f;
        params.motionShape = MatrixFlowShape::Chase;
        params.clockSync = 1u;
        params.fieldDivision = 5u;
        params.eventDivision = 3u;
        params.reactMode = NoInputReactMode::Balance;
        params.reactDepth = 0.34f;
        params.reactThreshold = 0.14f;
        params.reactAttack = 0.20f;
        params.reactRelease = 0.48f;
        params.aux[0].effect.type = NoInputDistortionType::OctStack;
        params.aux[0].effect.gain = 0.36f;
        params.aux[0].feedback = 0.28f;
        params.aux[0].returnGain = 0.28f;
        params.aux[1].effect.type = NoInputDistortionType::Chorus;
        params.aux[1].effect.gain = 0.30f;
        params.aux[1].feedback = 0.22f;
        params.aux[1].returnGain = 0.22f;
        for (uint32_t lane = 0u; lane < kNoInputMixerChannels; ++lane) {
            route(lane, lane, 0.95f);
            route(lane, (lane + 7u) % 8u, 0.22f);
            route(lane, (lane + 3u) % 8u,
                (lane & 1u) == 0u ? -0.14f : 0.12f);
            auto& voice = params.lanes[lane];
            voice.body = 0.12f + 0.09f * lane;
            voice.loss = 0.22f + 0.03f * (lane % 4u);
            voice.tuneNote = 31.0f + 5.0f * lane;
            voice.tuneCents = (lane % 3u) == 0u ? -7.0f : 0.0f;
            voice.pitchLock = 1u;
            voice.auxSend[0] = 0.18f + 0.03f * (lane % 3u);
            voice.auxSend[1] = 0.12f + 0.025f * ((lane + 1u) % 3u);
            voice.auxReturn[0] = 0.28f;
            voice.auxReturn[1] = 0.20f;
            voice.inserts[0].type = lane < 2u
                ? NoInputDistortionType::OctDown
                : (lane < 6u ? NoInputDistortionType::OctStack
                    : NoInputDistortionType::OctUp);
            voice.inserts[0].gain = 0.34f;
            voice.inserts[0].tone = 0.26f + 0.06f * (lane % 4u);
            voice.inserts[0].levelDb = -4.0f;
            voice.inserts[1].type = NoInputDistortionType::Diode;
            voice.inserts[1].gain = 0.16f;
            voice.inserts[1].levelDb = -3.0f;
            voice.inserts[1].bypass = 0u;
        }
        break;
    }
    case 18u: { // Aux Mirror: the aux topology is the primary composition.
        params.seed = 0x4155584du;
        params.outputGainDb = -12.0f;
        params.feedback = 0.90f;
        params.coupling = 0.34f;
        params.phase = 0.64f;
        params.drift = 0.28f;
        params.formant = 0.22f;
        params.agency = 0.72f;
        params.space = 0.50f;
        params.internalTone = 0.12f;
        params.houseTone = -0.18f;
        params.flow = 0.38f;
        params.spread = 0.74f;
        params.motion = 0.36f;
        params.motionShape = MatrixFlowShape::Flow;
        params.motionRate = 0.30f;
        params.reactMode = NoInputReactMode::Avoid;
        params.reactDepth = 0.30f;
        params.reactThreshold = 0.12f;
        params.reactAttack = 0.22f;
        params.reactRelease = 0.60f;
        params.aux[0].effect.type = NoInputDistortionType::Ring;
        params.aux[0].effect.gain = 0.42f;
        params.aux[0].effect.tone = 0.36f;
        params.aux[0].feedback = 0.56f;
        params.aux[0].returnGain = 0.38f;
        params.aux[1].effect.type = NoInputDistortionType::Chorus;
        params.aux[1].effect.gain = 0.38f;
        params.aux[1].effect.tone = 0.62f;
        params.aux[1].feedback = 0.48f;
        params.aux[1].returnGain = 0.34f;
        for (uint32_t lane = 0u; lane < kNoInputMixerChannels; ++lane) {
            route(lane, lane, 0.96f);
            if ((lane & 1u) == 0u)
                route(lane, (lane + 4u) % 8u, -0.10f);
            auto& voice = params.lanes[lane];
            voice.body = 0.16f + 0.085f * lane;
            voice.loss = 0.20f + 0.03f * (lane % 4u);
            voice.levelDb = -2.0f;
            voice.auxSend[0] = (lane & 1u) == 0u ? 0.52f : 0.16f;
            voice.auxSend[1] = (lane & 1u) == 0u ? 0.14f : 0.48f;
            voice.auxTap[0] = (lane & 1u) == 0u
                ? NoInputAuxTap::PreEq : NoInputAuxTap::PostInsert;
            voice.auxTap[1] = (lane & 1u) == 0u
                ? NoInputAuxTap::PostInsert : NoInputAuxTap::PostEq;
            const uint32_t mirror = kNoInputMixerChannels - 1u - lane;
            voice.auxReturn[0] = (mirror & 1u) == 0u ? 0.48f : -0.42f;
            voice.auxReturn[1] = (lane & 1u) == 0u ? -0.40f : 0.46f;
            voice.inserts[0].type = NoInputDistortionType::Diode;
            voice.inserts[0].gain = 0.16f;
            voice.inserts[0].tone = 0.42f;
            voice.inserts[0].levelDb = -3.0f;
            voice.inserts[1].bypass = 1u;
            voice.inserts[2].bypass = 1u;
        }
        break;
    }
    case 19u: { // Wall Engine: dense continuous saturation without cuts.
        params.seed = 0x57414c4cu;
        params.outputGainDb = -18.0f;
        params.feedback = 1.04f;
        params.coupling = 0.94f;
        params.phase = 0.38f;
        params.drift = 0.26f;
        params.formant = 0.52f;
        params.agency = 0.94f;
        params.space = 0.01f;
        params.internalTone = 0.20f;
        params.houseTone = -0.08f;
        params.flow = 0.26f;
        params.spread = 0.92f;
        params.vortex = 0.18f;
        params.motion = 0.22f;
        params.motionShape = MatrixFlowShape::Flow;
        params.motionRate = 0.12f;
        params.reactMode = NoInputReactMode::Off;
        params.reactDepth = 0.0f;
        params.aux[0].effect.type = NoInputDistortionType::Wool;
        params.aux[0].effect.gain = 0.52f;
        params.aux[0].effect.tone = 0.30f;
        params.aux[0].feedback = 0.62f;
        params.aux[0].returnGain = 0.38f;
        params.aux[1].effect.type = NoInputDistortionType::Rat;
        params.aux[1].effect.gain = 0.46f;
        params.aux[1].effect.tone = 0.58f;
        params.aux[1].feedback = 0.56f;
        params.aux[1].returnGain = 0.34f;
        for (uint32_t lane = 0u; lane < kNoInputMixerChannels; ++lane) {
            route(lane, lane, 0.99f);
            route(lane, (lane + 7u) % 8u, 0.36f);
            route(lane, (lane + 1u) % 8u, 0.31f);
            route(lane, (lane + 2u) % 8u,
                (lane & 1u) == 0u ? -0.24f : 0.23f);
            route(lane, (lane + 4u) % 8u, 0.18f);
            auto& voice = params.lanes[lane];
            voice.body = 0.20f + 0.06f * (lane % 6u);
            voice.loss = 0.34f + 0.025f * (lane % 3u);
            voice.levelDb = -5.0f;
            voice.lowDb = 2.0f;
            voice.midGainDb = 3.5f;
            voice.midFrequencyHz = 420.0f + 310.0f * lane;
            voice.highDb = 1.5f;
            voice.auxSend[0] = 0.30f + 0.025f * (lane % 3u);
            voice.auxSend[1] = 0.26f + 0.025f * ((lane + 1u) % 3u);
            voice.auxReturn[0] = 0.42f;
            voice.auxReturn[1] = 0.36f;
            voice.inserts[0].type = (lane & 1u) == 0u
                ? NoInputDistortionType::Wool : NoInputDistortionType::Rat;
            voice.inserts[0].gain = 0.52f;
            voice.inserts[0].tone = 0.28f + 0.07f * (lane % 4u);
            voice.inserts[0].levelDb = -11.0f;
            voice.inserts[1].type = (lane & 1u) == 0u
                ? NoInputDistortionType::ZoneA
                : NoInputDistortionType::ZoneB;
            voice.inserts[1].gain = 0.40f;
            voice.inserts[1].tone = 0.36f + 0.06f * (lane % 3u);
            voice.inserts[1].levelDb = -10.0f;
            voice.inserts[1].bypass = 0u;
            voice.inserts[2].type = (lane % 3u) == 0u
                ? NoInputDistortionType::FuzzI
                : NoInputDistortionType::Diode;
            voice.inserts[2].gain = 0.28f;
            voice.inserts[2].tone = 0.50f;
            voice.inserts[2].levelDb = -7.0f;
            voice.inserts[2].bypass = 0u;
        }
        break;
    }
    default:
        break;
    }
    return sanitizeNoInputMixerParams(params);
}

inline NoInputMixerParams randomizedNoInputMixerParams(uint32_t seed,
    NoInputRandomEnergy energy = NoInputRandomEnergy::Mid)
{
    uint32_t randomState = seed == 0u ? 1u : seed;
    const auto next = [&randomState]() {
        randomState += 0x9e3779b9u;
        uint32_t value = randomState;
        value ^= value >> 16u;
        value *= 0x7feb352du;
        value ^= value >> 15u;
        value *= 0x846ca68bu;
        value ^= value >> 16u;
        return value;
    };
    const auto unit = [&next]() {
        return static_cast<float>(next() & 0x00ffffffu)
            / static_cast<float>(0x01000000u);
    };
    const auto bipolar = [&unit]() { return unit() * 2.0f - 1.0f; };

    energy = static_cast<NoInputRandomEnergy>(std::min<uint32_t>(
        static_cast<uint32_t>(energy),
        static_cast<uint32_t>(NoInputRandomEnergy::Count) - 1u));
    const bool highEnergy = energy == NoInputRandomEnergy::High;
    const bool lowEnergy = energy == NoInputRandomEnergy::Low;

    auto params = defaultNoInputMixerParams();
    params.seed = seed == 0u ? 1u : seed;
    params.outputGainDb = highEnergy ? -11.0f + unit() * 4.0f
        : (lowEnergy ? -14.0f + unit() * 4.0f
            : -17.0f + unit() * 4.0f);
    params.ceilingDb = -1.0f;
    params.feedback = highEnergy ? 0.94f + unit() * 0.08f
        : (lowEnergy ? 0.87f + unit() * 0.08f
            : 0.90f + unit() * 0.08f);
    params.coupling = highEnergy ? 0.52f + unit() * 0.38f
        : (lowEnergy ? 0.22f + unit() * 0.30f
            : 0.36f + unit() * 0.38f);
    params.phase = 0.14f + unit() * 0.66f;
    params.drift = highEnergy ? 0.20f + unit() * 0.46f
        : (lowEnergy ? 0.04f + unit() * 0.20f
            : 0.10f + unit() * 0.38f);
    params.formant = 0.10f + unit() * 0.52f;
    params.agency = highEnergy ? 0.56f + unit() * 0.38f
        : (lowEnergy ? 0.18f + unit() * 0.38f
            : 0.34f + unit() * 0.50f);
    params.space = highEnergy ? 0.02f + unit() * 0.22f
        : (lowEnergy ? 0.04f + unit() * 0.24f
            : 0.05f + unit() * 0.35f);
    params.variance = 0.08f + unit() * 0.46f;
    params.internalTone = bipolar() * 0.46f;
    params.houseTone = -0.42f + unit() * 0.58f;
    params.flow = highEnergy ? 0.58f + unit() * 0.36f
        : (lowEnergy ? 0.18f + unit() * 0.42f
            : 0.32f + unit() * 0.50f);
    params.spread = highEnergy ? 0.22f + unit() * 0.66f
        : (lowEnergy ? 0.26f + unit() * 0.42f
            : 0.18f + unit() * 0.62f);
    params.vortex = bipolar() * 0.88f;
    params.motion = highEnergy ? 0.72f + unit() * 0.24f
        : (lowEnergy ? 0.38f + unit() * 0.18f
            : 0.42f + unit() * 0.26f);
    if (highEnergy) {
        static constexpr std::array<MatrixFlowShape, 6u> highShapes {{
            MatrixFlowShape::Pulse, MatrixFlowShape::Chase,
            MatrixFlowShape::Swirl, MatrixFlowShape::Scatter,
            MatrixFlowShape::Bloom, MatrixFlowShape::Braid,
        }};
        params.motionShape = highShapes[next() % highShapes.size()];
    } else if (lowEnergy) {
        static constexpr std::array<MatrixFlowShape, 5u> slowShapes {{
            MatrixFlowShape::Flow, MatrixFlowShape::Chase,
            MatrixFlowShape::Swirl, MatrixFlowShape::Bloom,
            MatrixFlowShape::Attract,
        }};
        params.motionShape = slowShapes[next() % slowShapes.size()];
    } else {
        static constexpr std::array<MatrixFlowShape, 8u> midShapes {{
            MatrixFlowShape::Flow, MatrixFlowShape::Pulse,
            MatrixFlowShape::Chase, MatrixFlowShape::Swirl,
            MatrixFlowShape::Scatter, MatrixFlowShape::Bloom,
            MatrixFlowShape::Braid, MatrixFlowShape::Attract,
        }};
        params.motionShape = midShapes[next() % midShapes.size()];
    }
    params.motionRate = highEnergy ? 0.72f + unit() * 0.23f
        : (lowEnergy ? 0.58f + unit() * 0.20f
            : 0.42f + unit() * 0.24f);
    params.motionPhase = unit();
    params.reactMode = highEnergy
        ? (unit() < 0.58f ? NoInputReactMode::Edge
            : NoInputReactMode::Balance)
        : (lowEnergy
            ? (unit() < 0.34f ? NoInputReactMode::Follow
                : NoInputReactMode::Balance)
            : (unit() < 0.30f ? NoInputReactMode::Off
                : static_cast<NoInputReactMode>(1u + next() % 4u)));
    params.reactDepth = params.reactMode == NoInputReactMode::Off
        ? 0.0f : (highEnergy ? 0.24f + unit() * 0.26f
            : (lowEnergy ? 0.10f + unit() * 0.16f
                : 0.16f + unit() * 0.26f));
    params.reactThreshold = highEnergy ? 0.08f + unit() * 0.20f
        : (lowEnergy ? 0.06f + unit() * 0.16f
            : 0.08f + unit() * 0.28f);
    params.reactAttack = highEnergy ? 0.02f + unit() * 0.18f
        : (lowEnergy ? 0.28f + unit() * 0.32f
            : 0.08f + unit() * 0.32f);
    params.reactRelease = highEnergy ? 0.08f + unit() * 0.24f
        : (lowEnergy ? 0.48f + unit() * 0.34f
            : 0.22f + unit() * 0.38f);
    params.reactPolarity = highEnergy ? 0.55f + unit() * 0.45f
        : (lowEnergy ? 0.35f + unit() * 0.35f : bipolar());
    params.controllerHold = 0u;
    params.slowTime = lowEnergy ? 1u : 0u;
    // Random movement is always free-running. Host sync is a deliberate
    // performance choice and should never be enabled by a random patch.
    // Retain this draw so existing seeds keep every later random value.
    (void)unit();
    params.clockSync = 0u;
    params.fieldDivision = highEnergy ? next() % 4u
        : (lowEnergy ? 5u + next() % 3u : 2u + next() % 4u);
    params.eventDivision = highEnergy ? next() % 4u
        : (lowEnergy ? 5u + next() % 3u : 2u + next() % 4u);
    params.surfaceX = 0.5f;
    params.surfaceY = 0.5f;
    params.quality = unit() > 0.82f ? 2u : 1u;
    params.matrix.fill(0.0f);

    static constexpr std::array<NoInputDistortionType, 9u>
        immediateInsertTypes {{
            NoInputDistortionType::Wool, NoInputDistortionType::Rat,
            NoInputDistortionType::ZoneA, NoInputDistortionType::ZoneB,
            NoInputDistortionType::FuzzI, NoInputDistortionType::FuzzII,
            NoInputDistortionType::Diode, NoInputDistortionType::Ring,
            NoInputDistortionType::Crush,
        }};

    for (uint32_t lane = 0u; lane < kNoInputMixerChannels; ++lane) {
        params.matrix[lane * kNoInputMixerChannels + lane] =
            highEnergy ? 0.95f + unit() * 0.05f
                : (lowEnergy ? 0.90f + unit() * 0.06f
                    : 0.92f + unit() * 0.06f);
        const uint32_t neighbor = (lane + 7u) % kNoInputMixerChannels;
        params.matrix[lane * kNoInputMixerChannels + neighbor] =
            (unit() > 0.34f ? 1.0f : -1.0f)
            * (highEnergy ? 0.18f + unit() * 0.20f
                : (lowEnergy ? 0.08f + unit() * 0.12f
                    : 0.12f + unit() * 0.20f));
        if (unit() > (highEnergy ? 0.14f : (lowEnergy ? 0.72f : 0.46f))) {
            uint32_t source = next() % kNoInputMixerChannels;
            if (source == lane || source == neighbor) {
                source = (source + 3u) % kNoInputMixerChannels;
            }
            params.matrix[lane * kNoInputMixerChannels + source] =
                (unit() > 0.5f ? 1.0f : -1.0f)
                * (highEnergy ? 0.10f + unit() * 0.24f
                    : (lowEnergy ? 0.04f + unit() * 0.10f
                        : 0.06f + unit() * 0.20f));
        }
        if (highEnergy) {
            uint32_t source = (lane + 2u + next() % 5u)
                % kNoInputMixerChannels;
            if (source == lane || source == neighbor)
                source = (source + 3u) % kNoInputMixerChannels;
            params.matrix[lane * kNoInputMixerChannels + source] =
                (unit() > 0.58f ? 1.0f : -1.0f)
                * (0.07f + unit() * 0.17f);
        }

        auto& voice = params.lanes[lane];
        voice.body = 0.12f + unit() * 0.72f;
        voice.loss = highEnergy ? 0.24f + unit() * 0.32f
            : (lowEnergy ? 0.16f + unit() * 0.24f
                : 0.20f + unit() * 0.30f);
        voice.levelDb = highEnergy ? -3.0f + unit() * 4.0f
            : (lowEnergy ? -2.0f + unit() * 3.0f
                : -4.0f + unit() * 4.0f);
        voice.lowDb = bipolar() * 5.5f;
        voice.midFrequencyHz = 120.0f * std::pow(52.0f, unit());
        voice.midGainDb = bipolar() * 6.5f;
        voice.highDb = bipolar() * 5.5f;
        voice.auxSend[0] = unit() * (highEnergy ? 0.58f
            : (lowEnergy ? 0.24f : 0.42f));
        voice.auxSend[1] = unit() * (highEnergy ? 0.52f
            : (lowEnergy ? 0.22f : 0.38f));
        voice.tuneNote = 33.0f + static_cast<float>(next() % 40u);
        voice.tuneCents = bipolar() * 24.0f;
        voice.pitchLock = unit() > 0.46f ? 1u : 0u;
        voice.auxTap[0] = static_cast<NoInputAuxTap>(next() % 4u);
        voice.auxTap[1] = static_cast<NoInputAuxTap>(next() % 4u);
        voice.auxReturn[0] = lowEnergy
            ? 0.16f + unit() * 0.18f
            : (unit() > 0.22f ? 1.0f : -1.0f)
                * (0.12f + unit() * (highEnergy ? 0.52f : 0.40f));
        voice.auxReturn[1] = lowEnergy
            ? 0.12f + unit() * 0.16f
            : (unit() > 0.42f ? 1.0f : -1.0f)
                * (0.10f + unit() * (highEnergy ? 0.48f : 0.36f));
        for (uint32_t slot = 0u; slot < kNoInputMixerInsertSlots; ++slot) {
            auto& insert = voice.inserts[slot];
            insert.type = slot == 0u
                ? immediateInsertTypes[next() % immediateInsertTypes.size()]
                : static_cast<NoInputDistortionType>(
                    1u + next() % (kNoInputDistortionTypeCount - 1u));
            insert.gain = highEnergy ? 0.30f + unit() * 0.38f
                : (lowEnergy ? 0.10f + unit() * 0.22f
                    : 0.18f + unit() * (slot == 0u ? 0.34f : 0.26f));
            insert.tone = 0.18f + unit() * 0.68f;
            insert.bias = bipolar() * 0.22f;
            insert.levelDb = highEnergy ? -8.0f + unit() * 5.0f
                : (lowEnergy ? -6.0f + unit() * 4.0f
                    : -9.0f + unit() * 5.0f);
            insert.bypass = slot == 0u
                || unit() > (highEnergy ? 0.44f
                    : (lowEnergy ? 0.84f : 0.66f)) ? 0u : 1u;
        }
    }
    for (uint32_t bus = 0u; bus < 2u; ++bus) {
        auto& aux = params.aux[bus];
        aux.effect.type = static_cast<NoInputDistortionType>(
            1u + next() % (kNoInputDistortionTypeCount - 1u));
        aux.effect.gain = highEnergy ? 0.24f + unit() * 0.44f
            : (lowEnergy ? 0.08f + unit() * 0.20f
                : 0.14f + unit() * 0.34f);
        aux.effect.tone = 0.16f + unit() * 0.68f;
        aux.feedback = highEnergy ? 0.28f + unit() * 0.42f
            : (lowEnergy ? 0.08f + unit() * 0.20f
                : 0.16f + unit() * 0.34f);
        aux.returnGain = highEnergy ? 0.16f + unit() * 0.30f
            : (lowEnergy ? 0.08f + unit() * 0.16f
                : 0.10f + unit() * 0.24f);
    }
    return sanitizeNoInputMixerParams(params);
}

inline NoInputMixerParams variedNoInputMixerParams(
    NoInputMixerParams params, uint32_t seed, float amount)
{
    uint32_t state = seed == 0u ? 1u : seed;
    const auto randomUnit = [&state]() {
        state += 0x9e3779b9u;
        uint32_t value = state;
        value ^= value >> 16u;
        value *= 0x7feb352du;
        value ^= value >> 15u;
        value *= 0x846ca68bu;
        value ^= value >> 16u;
        return static_cast<float>(value & 0x00ffffffu)
            / static_cast<float>(0x01000000u);
    };
    const auto signedRandom = [&randomUnit]() {
        return randomUnit() * 2.0f - 1.0f;
    };
    amount = clamp(std::isfinite(amount) ? amount : 0.0f, 0.0f, 1.0f);
    params.seed = seed == 0u ? 1u : seed;
    params.variance = amount;
    params.feedback += signedRandom() * 0.035f * amount;
    params.coupling += signedRandom() * 0.10f * amount;
    params.phase += signedRandom() * 0.12f * amount;
    params.reactDepth += signedRandom() * 0.18f * amount;
    params.reactThreshold += signedRandom() * 0.12f * amount;
    params.reactAttack += signedRandom() * 0.14f * amount;
    params.reactRelease += signedRandom() * 0.16f * amount;
    params.reactPolarity += signedRandom() * 0.24f * amount;
    params.motionPhase = noInputWrapPhase(
        params.motionPhase + signedRandom() * amount);
    for (float& route : params.matrix) {
        if (std::abs(route) > 1.0e-6f) {
            route *= 1.0f + signedRandom() * 0.22f * amount;
        }
    }
    for (auto& lane : params.lanes) {
        lane.body += signedRandom() * 0.10f * amount;
        lane.loss += signedRandom() * 0.08f * amount;
        lane.levelDb += signedRandom() * 2.5f * amount;
        lane.lowDb += signedRandom() * 3.0f * amount;
        lane.midFrequencyHz *= std::exp2(signedRandom() * 0.55f * amount);
        lane.midGainDb += signedRandom() * 3.5f * amount;
        lane.highDb += signedRandom() * 3.0f * amount;
        for (float& send : lane.auxSend) {
            send += signedRandom() * 0.16f * amount;
        }
        lane.tuneCents += signedRandom() * 18.0f * amount;
        for (float& returnAmount : lane.auxReturn) {
            returnAmount += signedRandom() * 0.18f * amount;
        }
        for (auto& insert : lane.inserts) {
            insert.gain += signedRandom() * 0.12f * amount;
            insert.tone += signedRandom() * 0.16f * amount;
            insert.bias += signedRandom() * 0.12f * amount;
        }
    }
    for (auto& aux : params.aux) {
        aux.effect.gain += signedRandom() * 0.12f * amount;
        aux.effect.tone += signedRandom() * 0.16f * amount;
        aux.feedback += signedRandom() * 0.12f * amount;
        aux.returnGain += signedRandom() * 0.10f * amount;
    }
    return sanitizeNoInputMixerParams(params);
}

// FORGET is deliberately local: it loosens a handful of relationships while
// preserving the instrument, channel settings, pedals, and global safety.
inline NoInputMixerParams forgottenNoInputMixerParams(
    NoInputMixerParams params, uint32_t seed)
{
    uint32_t state = seed == 0u ? 1u : seed;
    const auto next = [&state]() {
        state += 0x9e3779b9u;
        uint32_t value = state;
        value ^= value >> 16u;
        value *= 0x7feb352du;
        value ^= value >> 15u;
        value *= 0x846ca68bu;
        value ^= value >> 16u;
        return value;
    };
    const auto unit = [&next]() {
        return static_cast<float>(next() & 0x00ffffffu)
            / static_cast<float>(0x01000000u);
    };
    const uint32_t changes = 2u + static_cast<uint32_t>(
        std::lround(params.agency * 8.0f));
    for (uint32_t change = 0u; change < changes; ++change) {
        const uint32_t destination = next() % kNoInputMixerChannels;
        uint32_t source = next() % kNoInputMixerChannels;
        if (source == destination) source = (source + 3u) % 8u;
        float& route = params.matrix[destination * 8u + source];
        if (std::abs(route) > 1.0e-5f) {
            if (unit() < 0.58f) route = 0.0f;
            else route = -route * (0.72f + unit() * 0.24f);
        } else {
            route = (unit() < 0.38f ? -1.0f : 1.0f)
                * (0.07f + unit() * (0.10f + params.agency * 0.18f));
        }
    }
    const uint32_t lane = next() % kNoInputMixerChannels;
    params.lanes[lane].auxSend[next() & 1u] = 0.08f + unit() * 0.48f;
    params.motionPhase = unit();
    params.seed = seed == 0u ? 1u : seed;
    return sanitizeNoInputMixerParams(params);
}

enum class NoInputContainmentState : uint32_t {
    Quiet = 0u,
    Stable,
    Edge,
    Runaway,
};

inline const char* noInputContainmentName(NoInputContainmentState state)
{
    switch (state) {
    case NoInputContainmentState::Quiet: return "QUIET";
    case NoInputContainmentState::Stable: return "STABLE";
    case NoInputContainmentState::Edge: return "EDGE";
    case NoInputContainmentState::Runaway: return "RUNAWAY";
    }
    return "QUIET";
}

class NoInputMixer {
public:
    void prepare(double sampleRate)
    {
        sampleRate_ = std::max(1.0, sampleRate);
        // A Parameter Surface gesture is evaluated at control rate.  Give
        // every continuous DSP parameter one final audio-rate ramp so those
        // control-rate updates cannot become impulses inside the feedback
        // network.  The time is a near-complete (60 dB) settling interval.
        constexpr float kSixtyDb = 6.90775527898f;
        constexpr float kParameterRampSeconds = 0.020f;
        parameterSlew_ = 1.0f - std::exp(-kSixtyDb
            / std::max(1.0f, static_cast<float>(sampleRate_)
                * kParameterRampSeconds));
        constexpr float kSurfaceMatrixRampSeconds = 0.080f;
        surfaceMatrixSlew_ = 1.0f - std::exp(-kSixtyDb
            / std::max(1.0f, static_cast<float>(sampleRate_)
                * kSurfaceMatrixRampSeconds));
        dcPole_ = std::exp(-2.0f * kPi * 12.0f
            / static_cast<float>(sampleRate_));
        energyAttack_ = 1.0f - std::exp(-1.0f
            / static_cast<float>(sampleRate_ * 0.012));
        energyRelease_ = 1.0f - std::exp(-1.0f
            / static_cast<float>(sampleRate_ * 0.180));
        governorAttack_ = 1.0f - std::exp(-1.0f
            / static_cast<float>(sampleRate_ * 0.018));
        governorRelease_ = 1.0f - std::exp(-1.0f
            / static_cast<float>(sampleRate_ * 0.420));
        auxMuteSlew_ = 1.0f - std::exp(-1.0f
            / static_cast<float>(sampleRate_ * 0.004));
        movementCoefficientSlew_ = -1.0f;
        refreshMovementCoefficients();
        setMidiMatrixRampMs(midiMatrixRampMs_);
        panicSamplesTotal_ = std::max<uint32_t>(
            1u, static_cast<uint32_t>(sampleRate_ * 0.008));
        parameterSmoothingInitialized_ = false;
        setParams(targetParams_);
        reset();
    }

    void setParams(NoInputMixerParams params)
    {
        const auto sanitized = sanitizeNoInputMixerParams(params);
        for (uint32_t lane = 0u; lane < kNoInputMixerChannels; ++lane) {
            const auto& previous = targetParams_.lanes[lane];
            const auto& next = sanitized.lanes[lane];
            if (previous.body != next.body || previous.loss != next.loss
                || previous.tuneNote != next.tuneNote
                || previous.tuneCents != next.tuneCents
                || previous.pitchLock != next.pitchLock) {
                bodyCoefficientsDirty_[lane] = 1u;
            }
            if (previous.lowDb != next.lowDb
                || previous.midFrequencyHz != next.midFrequencyHz
                || previous.midGainDb != next.midGainDb
                || previous.highDb != next.highDb) {
                eqCoefficientsDirty_[lane] = 1u;
            }
        }
        targetParams_ = sanitized;
        if (!parameterSmoothingInitialized_ || !audioProcessingStarted_) {
            params_ = targetParams_;
            parameterSmoothingInitialized_ = true;
            parameterSmoothingActive_ = false;
        } else {
            copyDiscreteParameters();
            parameterSmoothingActive_ = true;
        }
        for (uint32_t index = 0u;
             index < kNoInputMixerMatrixCells; ++index) {
            if (midiMatrixReleasePending_[index] != 0u) {
                midiMatrixTarget_[index] = targetParams_.matrix[index];
            }
        }
        for (uint32_t lane = 0u; lane < kNoInputMixerChannels; ++lane) {
            for (uint32_t slot = 0u;
                 slot < kNoInputMixerInsertSlots; ++slot) {
                auto& runtime = laneState_[lane].inserts[slot];
                const auto& insert = params_.lanes[lane].inserts[slot];
                const NoInputDistortionType effective = insert.bypass != 0u
                    ? NoInputDistortionType::Bypass : insert.type;
                if (!runtime.initialized) {
                    runtime.currentType = effective;
                    runtime.previousType = effective;
                    runtime.crossfade = 1.0f;
                    runtime.initialized = true;
                } else if (runtime.currentType != effective) {
                    runtime.previousType = runtime.currentType;
                    runtime.currentType = effective;
                    runtime.crossfade = 0.0f;
                }
            }
        }
        for (uint32_t bus = 0u; bus < 2u; ++bus) {
            auto& runtime = auxState_[bus].insert;
            const auto type = params_.aux[bus].effect.type;
            if (!runtime.initialized) {
                runtime.currentType = type;
                runtime.previousType = type;
                runtime.crossfade = 1.0f;
                runtime.initialized = true;
            } else if (runtime.currentType != type) {
                runtime.previousType = runtime.currentType;
                runtime.currentType = type;
                runtime.crossfade = 0.0f;
            }
        }
        if (controlCounter_ == 0u) {
            for (uint32_t lane = 0u;
                 lane < kNoInputMixerChannels; ++lane) {
                updateEq(lane);
                updateBodyCoefficients(lane);
            }
            rebuildMotionTargets();
        }
    }

    void setMidiMatrixRampMs(float milliseconds)
    {
        midiMatrixRampMs_ = clamp(std::isfinite(milliseconds)
                ? milliseconds : kNoInputMatrixMidiRampDefaultMs,
            kNoInputMatrixMidiRampMinimumMs,
            kNoInputMatrixMidiRampMaximumMs);
        // Treat the displayed time as a near-complete (60 dB) settling
        // interval rather than a one-pole time constant. This makes a
        // 1000 ms setting read as an approximately one-second fade.
        constexpr float kSixtyDb = 6.90775527898f;
        const float samples = static_cast<float>(sampleRate_)
            * midiMatrixRampMs_ * 0.001f;
        midiMatrixSlew_ = 1.0f - std::exp(-kSixtyDb
            / std::max(1.0f, samples));
    }

    float midiMatrixRampMs() const { return midiMatrixRampMs_; }

    void setMidiMatrixConnection(uint32_t destination, uint32_t source,
        float gain)
    {
        if (destination >= kNoInputMixerChannels
            || source >= kNoInputMixerChannels) return;
        const uint32_t index = destination * kNoInputMixerChannels + source;
        if (midiMatrixActive_[index] == 0u) {
            midiMatrixGain_[index] = params_.matrix[index];
            midiMatrixActive_[index] = 1u;
        }
        midiMatrixTarget_[index] = clamp(gain, -1.0f, 1.0f);
        midiMatrixReleasePending_[index] = 0u;
        midiMatrixActive_[index] = 1u;
    }

    void releaseMidiMatrixConnection(uint32_t destination, uint32_t source)
    {
        if (destination >= kNoInputMixerChannels
            || source >= kNoInputMixerChannels) return;
        const uint32_t index = destination * kNoInputMixerChannels + source;
        if (midiMatrixActive_[index] == 0u) return;
        midiMatrixTarget_[index] = params_.matrix[index];
        midiMatrixReleasePending_[index] = 1u;
    }

    void clearMidiMatrixConnections()
    {
        midiMatrixActive_.fill(0u);
        midiMatrixGain_.fill(0.0f);
        midiMatrixTarget_.fill(0.0f);
        midiMatrixReleasePending_.fill(0u);
    }

    float effectiveMatrixGain(uint32_t destination, uint32_t source) const
    {
        if (destination >= kNoInputMixerChannels
            || source >= kNoInputMixerChannels) return 0.0f;
        return effectiveMatrixGain(
            destination * kNoInputMixerChannels + source);
    }

    bool midiMatrixConnectionActive(
        uint32_t destination, uint32_t source) const
    {
        if (destination >= kNoInputMixerChannels
            || source >= kNoInputMixerChannels) return false;
        return midiMatrixActive_[
            destination * kNoInputMixerChannels + source] != 0u;
    }

    void setTransport(double tempoBpm, bool hasTempo)
    {
        transportTempoBpm_ = std::clamp(
            std::isfinite(tempoBpm) ? tempoBpm : 120.0, 1.0, 1000.0);
        transportHasTempo_ = hasTempo;
    }

    const NoInputMixerParams& params() const { return targetParams_; }

    void setParameterSurfaceMutationEnabled(bool enabled)
    {
        parameterSurfaceMutationEnabled_ = enabled;
    }

    void setMovementBehaviorParams(NoInputMovementBehaviorParams params)
    {
        const auto previous = behaviorParams_.behavior;
        behaviorTargetParams_ = sanitizeNoInputMovementBehaviorParams(params);
        if (!audioProcessingStarted_) {
            behaviorParams_ = behaviorTargetParams_;
        } else {
            behaviorParams_.behavior = behaviorTargetParams_.behavior;
            parameterSmoothingActive_ = true;
        }
        if (previous != behaviorParams_.behavior) {
            behaviorSamplesUntilEvent_ = 0u;
            behaviorEnvelopeSample_ = 0u;
            behaviorEnvelopeSamples_ = 0u;
            behaviorAttackSamples_ = 0u;
            behaviorReleaseSamples_ = 0u;
            if (behaviorParams_.behavior == NoInputMovementBehavior::Glide) {
                behaviorTarget_.fill(1.0f);
                behaviorCurrent_.fill(1.0f);
                behaviorStart_.fill(1.0f);
                laneBehaviorGate_.fill(1.0f);
            }
        }
    }

    const NoInputMovementBehaviorParams& movementBehaviorParams() const
    {
        return behaviorTargetParams_;
    }

    void setMovementBehaviorDepth(float depth)
    {
        behaviorDepthTarget_ = clamp(std::isfinite(depth) ? depth : 0.0f,
            0.0f, 1.0f);
        if (!audioProcessingStarted_) {
            behaviorDepth_ = behaviorDepthTarget_;
        } else {
            parameterSmoothingActive_ = true;
        }
    }

    float movementBehaviorDepth() const { return behaviorDepthTarget_; }

    void setAuxMuted(uint32_t bus, bool muted)
    {
        if (bus >= auxMuted_.size()) return;
        auxMuted_[bus] = muted ? 1u : 0u;
    }

    bool auxMuted(uint32_t bus) const
    {
        return bus < auxMuted_.size() && auxMuted_[bus] != 0u;
    }

    void reset()
    {
        params_ = targetParams_;
        behaviorParams_ = behaviorTargetParams_;
        behaviorDepth_ = behaviorDepthTarget_;
        parameterSmoothingActive_ = false;
        audioProcessingStarted_ = false;
        clearMidiMatrixConnections();
        clearSignalState();
        silenced_ = true;
        seedRemaining_ = 0u;
        panicRemaining_ = 0u;
        containmentState_ = NoInputContainmentState::Quiet;
    }

    void reseed(uint32_t seed, float amount = 0.45f)
    {
        clearSignalState();
        randomState_ = seed == 0u ? 1u : seed;
        seedAmount_ = clamp(amount, 0.01f, 1.0f);
        seedRemaining_ = std::max<uint32_t>(
            16u, static_cast<uint32_t>(sampleRate_ * 0.018));
        silenced_ = false;
        panicRemaining_ = 0u;
    }

    void triggerSeed(float amount = 0.35f)
    {
        seedAmount_ = clamp(amount, 0.01f, 1.0f);
        seedRemaining_ = std::max<uint32_t>(
            16u, static_cast<uint32_t>(sampleRate_ * 0.012));
        silenced_ = false;
    }

    void panic()
    {
        clearMidiMatrixConnections();
        panicRemaining_ = panicSamplesTotal_;
    }

    void killLane(uint32_t lane)
    {
        if (lane >= kNoInputMixerChannels) return;
        clearLaneSignalState(laneState_[lane]);
        returns_[lane] = 0.0f;
        previousReturns_[lane] = 0.0f;
        tapPreEq_[lane] = previousTapPreEq_[lane] = 0.0f;
        tapPostEq_[lane] = previousTapPostEq_[lane] = 0.0f;
        tapPostInsert_[lane] = previousTapPostInsert_[lane] = 0.0f;
        lanePeak_[lane] = 0.0f;
        laneActivity_[lane] = 0.0f;
        responseEnergy_[lane] = 0.0f;
        laneBehaviorCircuitGate_[lane] = 1.0f;
        laneBehaviorLowpass_[lane] = 0.0f;
        for (uint32_t source = 0u; source < kNoInputMixerChannels;
             ++source) {
            phaseMemory_[lane * kNoInputMixerChannels + source] = 0.0f;
            phaseMemory_[source * kNoInputMixerChannels + lane] = 0.0f;
            behaviorRouteLowpass_[lane * kNoInputMixerChannels + source]
                = 0.0f;
            behaviorRouteLowpass_[source * kNoInputMixerChannels + lane]
                = 0.0f;
        }
        updateEq(lane);
        updateBodyCoefficients(lane);
        initializeInsertTypes(lane);
    }

    void processFrame(float* output)
    {
        if (!output) return;
        audioProcessingStarted_ = true;
        if (parameterSmoothingActive_) {
            advanceParameterSmoothing();
        } else {
            // Preserve the coefficient-refresh phase used by active ramps
            // without scanning every continuous parameter while stationary.
            ++parameterUpdateCounter_;
        }
        routeSignals_.fill(0.0f);
        advanceMidiMatrixConnections();
        if (silenced_ && seedRemaining_ == 0u) {
            std::fill(output, output + kNoInputMixerChannels, 0.0f);
            return;
        }

        if (params_.controllerHold == 0u
            && params_.motion > 0.0001f
            && params_.motionShape != MatrixFlowShape::Hold) {
            const float hz = params_.clockSync != 0u && transportHasTempo_
                ? noInputSyncedRateHz(params_.fieldDivision,
                    transportTempoBpm_)
                : noInputMixerMotionRateHz(params_.motionRate,
                    params_.slowTime != 0u);
            motionPhase_ = noInputWrapPhase(motionPhase_
                + hz / static_cast<float>(sampleRate_));
        }
        const bool behaviorNeutral = movementBehaviorLayerIsNeutral();
        if (params_.controllerHold == 0u) {
            if ((controlCounter_++ & 31u) == 0u) updateSlowControl();
            if (!behaviorNeutral) updateMovementBehavior();
        }
        // HOLD freezes the field and behavior clocks, but Response remains a
        // live audio follower. Its own OFF mode is the explicit way to stop it.
        updateReact();
        if (!behaviorNeutral || movementCircuitProcessing_)
            advanceMovementCircuitGates(behaviorNeutral);
        previousReturns_ = returns_;
        previousTapPreEq_ = tapPreEq_;
        previousTapPostEq_ = tapPostEq_;
        previousTapPostInsert_ = tapPostInsert_;
        previousAuxReturns_ = auxReturns_;
        for (uint32_t bus = 0u; bus < 2u; ++bus) {
            const float target = auxMuted_[bus] != 0u ? 0.0f : 1.0f;
            auxMuteGain_[bus] += (target - auxMuteGain_[bus]) * auxMuteSlew_;
        }
        processAuxReturns();

        std::array<float, kNoInputMixerChannels> activeMotionPeak {};
        std::array<uint32_t, kNoInputMixerChannels> activeMotionRouteCount {};
        for (uint32_t destination = 0u;
             destination < kNoInputMixerChannels; ++destination) {
            for (uint32_t source = 0u;
                 source < kNoInputMixerChannels; ++source) {
                const uint32_t index =
                    destination * kNoInputMixerChannels + source;
                if (std::abs(effectiveMatrixGain(index)) < 1.0e-7f) continue;
                activeMotionPeak[source] = std::max(
                    activeMotionPeak[source], effectiveMotionWeight(index));
                ++activeMotionRouteCount[source];
            }
        }

        std::array<float, kNoInputMixerChannels> matrixInput {};
        for (uint32_t destination = 0u;
             destination < kNoInputMixerChannels; ++destination) {
            float sum = seedForLane(destination)
                + auxReturns_[0] * params_.aux[0].returnGain
                    * params_.lanes[destination].auxReturn[0]
                + auxReturns_[1] * params_.aux[1].returnGain
                    * params_.lanes[destination].auxReturn[1];
            float weightSum = 0.0f;
            for (uint32_t source = 0u;
                 source < kNoInputMixerChannels; ++source) {
                const uint32_t index =
                    destination * kNoInputMixerChannels + source;
                const float matrixGain = effectiveMatrixGain(index);
                if (std::abs(matrixGain) < 1.0e-7f) continue;
                const float networkScale = source == destination
                    ? params_.feedback
                    : params_.feedback * params_.coupling;
                const float driftScale = 1.0f
                    + driftState_[index] * params_.drift * 0.14f;
                float motionScale = noInputMixerMotionGainScale(
                    effectiveMotionWeight(index), activeMotionPeak[source],
                    activeMotionRouteCount[source], params_.motion);
                if (params_.reactMode != NoInputReactMode::Off
                    && params_.reactDepth > 1.0e-6f) {
                    const float reactScale = 0.0316227766f
                        + reactCurrent_[index] * 0.9683772234f;
                    motionScale *= lerp(1.0f, reactScale,
                        params_.reactDepth);
                }
                const float spaceScale = lerp(1.0f,
                    routeSpaceGate_[index], params_.space);
                const float activityDifference = laneActivity_[source]
                    - laneActivity_[destination];
                const float agencyScale = clamp(1.0f + params_.agency
                    * (activityDifference * 0.58f
                        + driftState_[index] * 0.07f), 0.62f, 1.38f);
                const float baseGain = matrixGain * networkScale * driftScale
                    * spaceScale * agencyScale;
                const float gain = baseGain * motionScale;
                const float coefficient = 0.08f + params_.phase
                    * (0.20f + 0.22f * hashUnit(index + 0x3157u));
                const float input = previousReturns_[source];
                const float allpass = -coefficient * input
                    + phaseMemory_[index];
                phaseMemory_[index] = flushDenormal(
                    input + coefficient * allpass);
                const float phased = lerp(input, allpass, params_.phase);
                float conditioned = phased;
                if (movementCircuitProcessing_
                    && behaviorDepth_ > 1.0e-7f) {
                    const float behaviorGate = lerp(1.0f,
                        behaviorCircuitGate_[index], behaviorDepth_);
                    conditioned = processMovementLpg(phased,
                        behaviorGate, behaviorRouteLowpass_[index]);
                }
                routeSignals_[index] = flushDenormal(conditioned * gain);
                sum += routeSignals_[index];
                // Normalize the graph, not the movement envelope. Including
                // the modulated gain here applies inverse compensation and
                // makes route motion disappear perceptually.
                weightSum += std::abs(baseGain);
            }
            const float normalization = 1.0f
                / std::max(1.0f, 0.42f + weightSum * 0.68f);
            const float normalized = sum * normalization;
            const float clampScale = std::abs(normalized) > 6.0f
                ? 6.0f / std::abs(normalized) : 1.0f;
            for (uint32_t source = 0u;
                 source < kNoInputMixerChannels; ++source) {
                const uint32_t index =
                    destination * kNoInputMixerChannels + source;
                routeSignals_[index] = flushDenormal(
                    routeSignals_[index] * normalization * clampScale);
            }
            matrixInput[destination] = clamp(normalized, -6.0f, 6.0f);
        }

        float networkPeak = 0.0f;
        for (uint32_t lane = 0u; lane < kNoInputMixerChannels; ++lane) {
            float value = processBody(lane, matrixInput[lane]);
            value = processFormant(lane, value);
            tapPreEq_[lane] = value;
            value = laneState_[lane].lowShelf.process(value);
            value = laneState_[lane].midPeak.process(value);
            value = laneState_[lane].highShelf.process(value);
            tapPostEq_[lane] = value;

            for (uint32_t slot = 0u;
                 slot < kNoInputMixerInsertSlots; ++slot) {
                const uint32_t ringSource =
                    (lane + slot + 1u) % kNoInputMixerChannels;
                value = processInsert(lane, slot, value,
                    previousReturns_[ringSource]);
            }

            if (!std::isfinite(value)) {
                killLane(lane);
                value = 0.0f;
            }
            // Response listens to the completed processor lane before CHOKE.
            // Keep this detector separate from the post-choke governor so a
            // movement window cannot recursively manufacture its own activity.
            const float responseEnergyInput = value * value;
            const float responseCoefficient = responseEnergyInput
                    > responseEnergy_[lane]
                ? energyAttack_ : energyRelease_;
            responseEnergy_[lane] += (responseEnergyInput
                - responseEnergy_[lane]) * responseCoefficient;
            responseEnergy_[lane] = std::max(0.0f,
                flushDenormal(responseEnergy_[lane]));
            const float responseRms = std::sqrt(responseEnergy_[lane]);
            if (movementCircuitProcessing_
                && behaviorParams_.choke > 1.0e-7f) {
                const float chokeGate = lerp(1.0f,
                    laneBehaviorCircuitGate_[lane], behaviorParams_.choke);
                value = processMovementLpg(value, chokeGate,
                    laneBehaviorLowpass_[lane]);
            }
            tapPostInsert_[lane] = value;
            value = clamp(value, -8.0f, 8.0f);

            auto& state = laneState_[lane];
            const float energyInput = value * value;
            const float energyCoefficient = energyInput > state.energy
                ? energyAttack_ : energyRelease_;
            state.energy += (energyInput - state.energy) * energyCoefficient;
            state.energy = std::max(0.0f, flushDenormal(state.energy));
            const float rms = std::sqrt(state.energy);
            const float excess = std::max(0.0f, rms - 0.42f);
            const float targetGovernor = 1.0f
                / (1.0f + excess * excess * 28.0f + excess * 3.5f);
            const float governorCoefficient = targetGovernor < state.governor
                ? governorAttack_ : governorRelease_;
            state.governor += (targetGovernor - state.governor)
                * governorCoefficient;
            state.governor = clamp(state.governor, 0.055f, 1.0f);

            float returned = value * state.governor;
            returned = processTone(returned, params_.internalTone,
                state.internalToneLow);
            returned = dcBlock(returned, state.returnDcInput,
                state.returnDcOutput);
            returns_[lane] = flushDenormal(returned);
            networkPeak = std::max(networkPeak, std::abs(returned));

            float audition = value * dbToGain(params_.lanes[lane].levelDb)
                * dbToGain(params_.outputGainDb);
            if (params_.lanes[lane].mute != 0u) audition = 0.0f;
            audition = processTone(audition, params_.houseTone,
                state.houseToneLow);
            audition = dcBlock(audition, state.outputDcInput,
                state.outputDcOutput);
            if (params_.limiterEnabled != 0u) {
                audition = softLimit(audition,
                    dbToGain(params_.ceilingDb));
            }
            if (panicRemaining_ > 0u) {
                audition *= static_cast<float>(panicRemaining_)
                    / static_cast<float>(panicSamplesTotal_);
            }
            output[lane] = std::isfinite(audition)
                ? flushDenormal(audition) : 0.0f;
            lanePeak_[lane] = std::max(
                lanePeak_[lane] * 0.9994f, std::abs(output[lane]));
            laneActivity_[lane] += (responseRms - laneActivity_[lane])
                * 0.003f;
        }

        if (seedRemaining_ > 0u) --seedRemaining_;
        if (panicRemaining_ > 0u) {
            --panicRemaining_;
            if (panicRemaining_ == 0u) {
                clearSignalState();
                silenced_ = true;
                containmentState_ = NoInputContainmentState::Quiet;
                std::fill(output, output + kNoInputMixerChannels, 0.0f);
                return;
            }
        }

        networkActivity_ += (networkPeak - networkActivity_) * 0.0025f;
        const float minimumGovernor = minimumLaneGovernor();
        if (networkActivity_ < 1.0e-5f) {
            containmentState_ = NoInputContainmentState::Quiet;
        } else if (minimumGovernor > 0.72f) {
            containmentState_ = NoInputContainmentState::Stable;
        } else if (minimumGovernor > 0.28f) {
            containmentState_ = NoInputContainmentState::Edge;
        } else {
            containmentState_ = NoInputContainmentState::Runaway;
        }
    }

    float lanePeak(uint32_t lane) const
    {
        return lane < kNoInputMixerChannels ? lanePeak_[lane] : 0.0f;
    }

    float laneActivity(uint32_t lane) const
    {
        return lane < kNoInputMixerChannels ? laneActivity_[lane] : 0.0f;
    }

    float routeSignal(uint32_t route) const
    {
        return route < kNoInputMixerMatrixCells
            ? routeSignals_[route] : 0.0f;
    }

    float networkActivity() const { return networkActivity_; }
    float auxActivity(uint32_t bus) const
    {
        return bus < 2u ? auxActivity_[bus] : 0.0f;
    }
    float motionPhase() const { return motionPhase_; }
    float behaviorRouteGate(uint32_t route) const
    {
        if (route >= kNoInputMixerMatrixCells) return 0.0f;
        return noInputMovementBehaviorUsesAmplitude(
                behaviorParams_.behavior)
            ? behaviorCircuitGate_[route] : behaviorCurrent_[route];
    }
    float reactRouteGate(uint32_t route) const
    {
        return route < kNoInputMixerMatrixCells
            ? reactCurrent_[route] : 1.0f;
    }
    float minimumGovernor() const { return minimumLaneGovernor(); }
    NoInputContainmentState containmentState() const
    {
        return containmentState_;
    }

private:
    void copyDiscreteParameters()
    {
        params_.limiterEnabled = targetParams_.limiterEnabled;
        params_.dcBlockEnabled = targetParams_.dcBlockEnabled;
        params_.motionShape = targetParams_.motionShape;
        params_.reactMode = targetParams_.reactMode;
        params_.reactPolarity = targetParams_.reactPolarity;
        params_.controllerHold = targetParams_.controllerHold;
        params_.slowTime = targetParams_.slowTime;
        params_.clockSync = targetParams_.clockSync;
        params_.fieldDivision = targetParams_.fieldDivision;
        params_.eventDivision = targetParams_.eventDivision;
        params_.quality = targetParams_.quality;
        params_.seed = targetParams_.seed;
        for (uint32_t bus = 0u; bus < 2u; ++bus) {
            params_.aux[bus].effect.type =
                targetParams_.aux[bus].effect.type;
            params_.aux[bus].effect.bypass =
                targetParams_.aux[bus].effect.bypass;
        }
        for (uint32_t lane = 0u;
             lane < kNoInputMixerChannels; ++lane) {
            params_.lanes[lane].mute = targetParams_.lanes[lane].mute;
            params_.lanes[lane].pitchLock =
                targetParams_.lanes[lane].pitchLock;
            for (uint32_t bus = 0u; bus < 2u; ++bus) {
                params_.lanes[lane].auxTap[bus] =
                    targetParams_.lanes[lane].auxTap[bus];
            }
            for (uint32_t slot = 0u;
                 slot < kNoInputMixerInsertSlots; ++slot) {
                params_.lanes[lane].inserts[slot].type =
                    targetParams_.lanes[lane].inserts[slot].type;
                params_.lanes[lane].inserts[slot].bypass =
                    targetParams_.lanes[lane].inserts[slot].bypass;
            }
        }
    }

    void advanceParameterSmoothing()
    {
        bool anySmoothing = false;
        const auto smoothWith = [](float& current, float target,
            float coefficient) {
            const float difference = target - current;
            if (std::abs(difference) <= 1.0e-7f) {
                current = target;
                return false;
            }
            current += difference * coefficient;
            current = flushDenormal(current);
            return true;
        };
        const auto smooth = [this, &smoothWith, &anySmoothing](
            float& current, float target) {
            const bool moving = smoothWith(current, target, parameterSlew_);
            anySmoothing = anySmoothing || moving;
            return moving;
        };

        smooth(params_.outputGainDb, targetParams_.outputGainDb);
        smooth(params_.ceilingDb, targetParams_.ceilingDb);
        smooth(params_.feedback, targetParams_.feedback);
        smooth(params_.coupling, targetParams_.coupling);
        smooth(params_.phase, targetParams_.phase);
        smooth(params_.drift, targetParams_.drift);
        smooth(params_.formant, targetParams_.formant);
        smooth(params_.agency, targetParams_.agency);
        smooth(params_.space, targetParams_.space);
        smooth(params_.variance, targetParams_.variance);
        smooth(params_.internalTone, targetParams_.internalTone);
        smooth(params_.houseTone, targetParams_.houseTone);
        smooth(params_.flow, targetParams_.flow);
        smooth(params_.spread, targetParams_.spread);
        smooth(params_.vortex, targetParams_.vortex);
        smooth(params_.motion, targetParams_.motion);
        smooth(params_.motionRate, targetParams_.motionRate);
        smooth(params_.motionPhase, targetParams_.motionPhase);
        smooth(params_.reactDepth, targetParams_.reactDepth);
        smooth(params_.reactThreshold, targetParams_.reactThreshold);
        smooth(params_.reactAttack, targetParams_.reactAttack);
        smooth(params_.reactRelease, targetParams_.reactRelease);
        smooth(params_.surfaceX, targetParams_.surfaceX);
        smooth(params_.surfaceY, targetParams_.surfaceY);

        for (uint32_t index = 0u;
             index < kNoInputMixerMatrixCells; ++index) {
            anySmoothing = smoothWith(params_.matrix[index],
                targetParams_.matrix[index], parameterSurfaceMutationEnabled_
                    ? surfaceMatrixSlew_ : parameterSlew_) || anySmoothing;
        }
        for (uint32_t bus = 0u; bus < 2u; ++bus) {
            auto& current = params_.aux[bus];
            const auto& target = targetParams_.aux[bus];
            smooth(current.effect.gain, target.effect.gain);
            smooth(current.effect.tone, target.effect.tone);
            smooth(current.effect.bias, target.effect.bias);
            smooth(current.effect.levelDb, target.effect.levelDb);
            smooth(current.feedback, target.feedback);
            smooth(current.returnGain, target.returnGain);
        }
        for (uint32_t lane = 0u;
             lane < kNoInputMixerChannels; ++lane) {
            auto& current = params_.lanes[lane];
            const auto& target = targetParams_.lanes[lane];
            bodyCoefficientsDirty_[lane] |= smooth(current.body,
                target.body) ? 1u : 0u;
            bodyCoefficientsDirty_[lane] |= smooth(current.loss,
                target.loss) ? 1u : 0u;
            smooth(current.levelDb, target.levelDb);
            eqCoefficientsDirty_[lane] |= smooth(current.lowDb,
                target.lowDb) ? 1u : 0u;
            eqCoefficientsDirty_[lane] |= smooth(current.midFrequencyHz,
                target.midFrequencyHz) ? 1u : 0u;
            eqCoefficientsDirty_[lane] |= smooth(current.midGainDb,
                target.midGainDb) ? 1u : 0u;
            eqCoefficientsDirty_[lane] |= smooth(current.highDb,
                target.highDb) ? 1u : 0u;
            bodyCoefficientsDirty_[lane] |= smooth(current.tuneNote,
                target.tuneNote) ? 1u : 0u;
            bodyCoefficientsDirty_[lane] |= smooth(current.tuneCents,
                target.tuneCents) ? 1u : 0u;
            for (uint32_t bus = 0u; bus < 2u; ++bus) {
                smooth(current.auxSend[bus], target.auxSend[bus]);
                smooth(current.auxReturn[bus], target.auxReturn[bus]);
            }
            for (uint32_t slot = 0u;
                 slot < kNoInputMixerInsertSlots; ++slot) {
                auto& currentInsert = current.inserts[slot];
                const auto& targetInsert = target.inserts[slot];
                smooth(currentInsert.gain, targetInsert.gain);
                smooth(currentInsert.tone, targetInsert.tone);
                smooth(currentInsert.bias, targetInsert.bias);
                smooth(currentInsert.levelDb, targetInsert.levelDb);
            }
        }

        smooth(behaviorParams_.eventRate, behaviorTargetParams_.eventRate);
        smooth(behaviorParams_.length, behaviorTargetParams_.length);
        smooth(behaviorParams_.density, behaviorTargetParams_.density);
        smooth(behaviorParams_.chaos, behaviorTargetParams_.chaos);
        smooth(behaviorParams_.slew, behaviorTargetParams_.slew);
        smooth(behaviorParams_.choke, behaviorTargetParams_.choke);
        smooth(behaviorDepth_, behaviorDepthTarget_);

        // Trigonometric coefficient construction is relatively expensive.
        // Refresh only lanes whose smoothed controls actually moved.
        if ((parameterUpdateCounter_++ & 31u) == 0u) {
            for (uint32_t lane = 0u;
                 lane < kNoInputMixerChannels; ++lane) {
                if (eqCoefficientsDirty_[lane] != 0u) {
                    updateEq(lane);
                    eqCoefficientsDirty_[lane] = 0u;
                }
                if (bodyCoefficientsDirty_[lane] != 0u) {
                    updateBodyCoefficients(lane);
                    bodyCoefficientsDirty_[lane] = 0u;
                }
            }
        }
        bool coefficientsDirty = false;
        for (uint32_t lane = 0u;
             lane < kNoInputMixerChannels; ++lane) {
            coefficientsDirty = coefficientsDirty
                || eqCoefficientsDirty_[lane] != 0u
                || bodyCoefficientsDirty_[lane] != 0u;
        }
        parameterSmoothingActive_ = anySmoothing || coefficientsDirty;
    }

    struct Biquad {
        float b0 = 1.0f;
        float b1 = 0.0f;
        float b2 = 0.0f;
        float a1 = 0.0f;
        float a2 = 0.0f;
        float z1 = 0.0f;
        float z2 = 0.0f;

        float process(float input)
        {
            const float output = input * b0 + z1;
            z1 = flushDenormal(input * b1 - output * a1 + z2);
            z2 = flushDenormal(input * b2 - output * a2);
            return output;
        }

        void clear() { z1 = z2 = 0.0f; }
    };

    struct Resonator {
        float c1 = 0.0f;
        float c2 = 0.0f;
        float inputGain = 0.0f;
        float z1 = 0.0f;
        float z2 = 0.0f;

        float process(float input)
        {
            const float output = input * inputGain + c1 * z1 + c2 * z2;
            z2 = z1;
            z1 = flushDenormal(clamp(output, -12.0f, 12.0f));
            return z1;
        }

        void clear() { z1 = z2 = 0.0f; }
    };

    struct DistortionState {
        float low = 0.0f;
        float high = 0.0f;
        float memory = 0.0f;
        float envelope = 0.0f;
        float previous = 0.0f;
        float phase = 0.0f;
        float gate = 0.0f;
    };

    struct InsertRuntime {
        std::array<DistortionState, kNoInputDistortionTypeCount> states {};
        std::array<float, 8192u> timeBuffer {};
        uint32_t timeWrite = 0u;
        NoInputDistortionType currentType = NoInputDistortionType::Bypass;
        NoInputDistortionType previousType = NoInputDistortionType::Bypass;
        float crossfade = 1.0f;
        bool initialized = false;
    };

    struct LaneState {
        std::array<Resonator, 4u> resonators {};
        Biquad lowShelf {};
        Biquad midPeak {};
        Biquad highShelf {};
        std::array<InsertRuntime, kNoInputMixerInsertSlots> inserts {};
        float formantLow = 0.0f;
        float formantHigh = 0.0f;
        float returnDcInput = 0.0f;
        float returnDcOutput = 0.0f;
        float outputDcInput = 0.0f;
        float outputDcOutput = 0.0f;
        float internalToneLow = 0.0f;
        float houseToneLow = 0.0f;
        float energy = 0.0f;
        float governor = 1.0f;
    };

    struct AuxState {
        InsertRuntime insert {};
        float dcInput = 0.0f;
        float dcOutput = 0.0f;
        float activity = 0.0f;
    };

    static void clearLaneSignalState(LaneState& state)
    {
        // SPLICE and CHORUS share each insert's fixed real-time buffer.
        // Aggregate assignment would materialize a full LaneState temporary
        // on the caller's stack; CLAP reset may run on a 512 KiB audio thread.
        std::memset(&state, 0, sizeof(state));
        state.governor = 1.0f;
    }

    static void clearAuxSignalState(AuxState& state)
    {
        std::memset(&state, 0, sizeof(state));
    }

    static void setBiquad(Biquad& biquad,
        float b0, float b1, float b2, float a0, float a1, float a2)
    {
        const float inverseA0 = 1.0f / std::max(1.0e-9f, a0);
        biquad.b0 = b0 * inverseA0;
        biquad.b1 = b1 * inverseA0;
        biquad.b2 = b2 * inverseA0;
        biquad.a1 = a1 * inverseA0;
        biquad.a2 = a2 * inverseA0;
    }

    void setLowShelf(Biquad& biquad, float frequency, float gainDb)
    {
        const float a = std::pow(10.0f, gainDb / 40.0f);
        const float omega = 2.0f * kPi * clamp(frequency, 20.0f,
            static_cast<float>(sampleRate_ * 0.45))
            / static_cast<float>(sampleRate_);
        const float cosine = std::cos(omega);
        const float sine = std::sin(omega);
        const float alpha = sine * std::sqrt((a + 1.0f / a) * 2.0f);
        const float beta = 2.0f * std::sqrt(a) * alpha;
        setBiquad(biquad,
            a * ((a + 1.0f) - (a - 1.0f) * cosine + beta),
            2.0f * a * ((a - 1.0f) - (a + 1.0f) * cosine),
            a * ((a + 1.0f) - (a - 1.0f) * cosine - beta),
            (a + 1.0f) + (a - 1.0f) * cosine + beta,
            -2.0f * ((a - 1.0f) + (a + 1.0f) * cosine),
            (a + 1.0f) + (a - 1.0f) * cosine - beta);
    }

    void setHighShelf(Biquad& biquad, float frequency, float gainDb)
    {
        const float a = std::pow(10.0f, gainDb / 40.0f);
        const float omega = 2.0f * kPi * clamp(frequency, 20.0f,
            static_cast<float>(sampleRate_ * 0.45))
            / static_cast<float>(sampleRate_);
        const float cosine = std::cos(omega);
        const float sine = std::sin(omega);
        const float alpha = sine * std::sqrt((a + 1.0f / a) * 2.0f);
        const float beta = 2.0f * std::sqrt(a) * alpha;
        setBiquad(biquad,
            a * ((a + 1.0f) + (a - 1.0f) * cosine + beta),
            -2.0f * a * ((a - 1.0f) + (a + 1.0f) * cosine),
            a * ((a + 1.0f) + (a - 1.0f) * cosine - beta),
            (a + 1.0f) - (a - 1.0f) * cosine + beta,
            2.0f * ((a - 1.0f) - (a + 1.0f) * cosine),
            (a + 1.0f) - (a - 1.0f) * cosine - beta);
    }

    void setPeaking(Biquad& biquad, float frequency, float q, float gainDb)
    {
        const float a = std::pow(10.0f, gainDb / 40.0f);
        const float omega = 2.0f * kPi * clamp(frequency, 20.0f,
            static_cast<float>(sampleRate_ * 0.45))
            / static_cast<float>(sampleRate_);
        const float alpha = std::sin(omega) / (2.0f * std::max(0.1f, q));
        const float cosine = std::cos(omega);
        setBiquad(biquad,
            1.0f + alpha * a,
            -2.0f * cosine,
            1.0f - alpha * a,
            1.0f + alpha / a,
            -2.0f * cosine,
            1.0f - alpha / a);
    }

    void updateEq(uint32_t lane)
    {
        if (lane >= kNoInputMixerChannels) return;
        auto& state = laneState_[lane];
        const auto& params = params_.lanes[lane];
        setLowShelf(state.lowShelf, 180.0f, params.lowDb);
        setPeaking(state.midPeak, params.midFrequencyHz, 0.82f,
            params.midGainDb);
        setHighShelf(state.highShelf, 4200.0f, params.highDb);
    }

    void updateBodyCoefficients(uint32_t lane)
    {
        static constexpr std::array<float, 4u> ratios {{
            1.0f, 1.47f, 2.11f, 3.29f,
        }};
        if (lane >= kNoInputMixerChannels) return;
        const auto& params = params_.lanes[lane];
        const float laneSpread = 1.0f + (static_cast<float>(lane) - 3.5f)
            * 0.013f;
        const float base = params.pitchLock != 0u
            ? 440.0f * std::pow(2.0f,
                (params.tuneNote - 69.0f + params.tuneCents * 0.01f)
                    / 12.0f)
            : 44.0f * std::pow(2.0f, params.body * 5.0f) * laneSpread;
        const float decaySeconds = lerp(2.8f, 0.055f, params.loss);
        const float radius = std::exp(-1.0f
            / std::max(1.0f,
                static_cast<float>(sampleRate_) * decaySeconds));
        for (uint32_t mode = 0u; mode < 4u; ++mode) {
            const float detune = params.pitchLock != 0u ? 1.0f
                : 1.0f + driftState_[lane * 8u + mode]
                    * params_.drift * 0.018f;
            const float frequency = clamp(base * ratios[mode] * detune,
                24.0f, static_cast<float>(sampleRate_ * 0.42));
            const float omega = 2.0f * kPi * frequency
                / static_cast<float>(sampleRate_);
            auto& resonator = laneState_[lane].resonators[mode];
            resonator.c1 = 2.0f * radius * std::cos(omega);
            resonator.c2 = -radius * radius;
            resonator.inputGain = std::max(0.00015f,
                (1.0f - radius) * (2.4f + 0.35f * mode));
        }
    }

    void initializeInsertTypes(uint32_t lane)
    {
        for (uint32_t slot = 0u;
             slot < kNoInputMixerInsertSlots; ++slot) {
            auto& runtime = laneState_[lane].inserts[slot];
            const auto& insert = params_.lanes[lane].inserts[slot];
            const auto type = insert.bypass != 0u
                ? NoInputDistortionType::Bypass : insert.type;
            runtime.currentType = type;
            runtime.previousType = type;
            runtime.crossfade = 1.0f;
            runtime.initialized = true;
        }
    }

    float processBody(uint32_t lane, float input)
    {
        auto& state = laneState_[lane];
        float body = 0.0f;
        for (uint32_t mode = 0u; mode < 4u; ++mode) {
            const float signedInput = (mode & 1u) == 0u ? input : -input;
            body += state.resonators[mode].process(signedInput);
        }
        body *= 0.31f;
        return std::tanh(input * 0.24f + body * 1.38f);
    }

    float processFormant(uint32_t lane, float input)
    {
        auto& state = laneState_[lane];
        const float body = params_.lanes[lane].body;
        const float lowHz = 120.0f + body * 780.0f;
        const float highHz = 1300.0f + body * 5600.0f;
        const float lowCoefficient = 1.0f - std::exp(-2.0f * kPi * lowHz
            / static_cast<float>(sampleRate_));
        const float highCoefficient = 1.0f - std::exp(-2.0f * kPi * highHz
            / static_cast<float>(sampleRate_));
        state.formantLow += (input - state.formantLow) * lowCoefficient;
        state.formantHigh += (input - state.formantHigh) * highCoefficient;
        state.formantLow = flushDenormal(state.formantLow);
        state.formantHigh = flushDenormal(state.formantHigh);
        const float highpass = input - state.formantLow;
        const float product = std::tanh(
            highpass * state.formantHigh * 5.5f);
        return lerp(input, product, params_.formant * 0.82f);
    }

    float processInsert(uint32_t lane, uint32_t slot,
        float input, float ringSource)
    {
        auto& runtime = laneState_[lane].inserts[slot];
        const auto& params = params_.lanes[lane].inserts[slot];
        const uint32_t substeps = 1u << params_.quality;
        const float step = 1.0f / static_cast<float>(substeps);
        float output = 0.0f;
        const float previousInput = runtime.states[
            static_cast<uint32_t>(runtime.currentType)].previous;
        for (uint32_t substep = 0u; substep < substeps; ++substep) {
            const float fraction = static_cast<float>(substep + 1u) * step;
            const float sample = lerp(previousInput, input, fraction);
            const float current = processDistortion(runtime, runtime.currentType,
                sample, ringSource, params);
            if (runtime.crossfade < 1.0f) {
                const float previous = processDistortion(runtime,
                    runtime.previousType, sample, ringSource, params);
                output += lerp(previous, current, runtime.crossfade);
            } else {
                output += current;
            }
        }
        runtime.states[static_cast<uint32_t>(runtime.currentType)].previous = input;
        output *= step;
        if (runtime.crossfade < 1.0f) {
            runtime.crossfade = std::min(1.0f,
                runtime.crossfade + static_cast<float>(substeps)
                    / static_cast<float>(sampleRate_ * 0.020));
        }
        return flushDenormal(output * dbToGain(params.levelDb));
    }

    float processAuxInsert(uint32_t bus, float input, float ringSource)
    {
        auto& runtime = auxState_[bus].insert;
        const auto& params = params_.aux[bus].effect;
        const uint32_t substeps = 1u << params_.quality;
        const float step = 1.0f / static_cast<float>(substeps);
        float output = 0.0f;
        const float previousInput = runtime.states[
            static_cast<uint32_t>(runtime.currentType)].previous;
        for (uint32_t substep = 0u; substep < substeps; ++substep) {
            const float fraction = static_cast<float>(substep + 1u) * step;
            const float sample = lerp(previousInput, input, fraction);
            const float current = processDistortion(runtime,
                runtime.currentType, sample, ringSource, params);
            if (runtime.crossfade < 1.0f) {
                const float previous = processDistortion(runtime,
                    runtime.previousType, sample, ringSource, params);
                output += lerp(previous, current, runtime.crossfade);
            } else {
                output += current;
            }
        }
        runtime.states[static_cast<uint32_t>(runtime.currentType)].previous
            = input;
        output *= step;
        if (runtime.crossfade < 1.0f) {
            runtime.crossfade = std::min(1.0f,
                runtime.crossfade + static_cast<float>(substeps)
                    / static_cast<float>(sampleRate_ * 0.020));
        }
        return flushDenormal(output);
    }

    void processAuxReturns()
    {
        for (uint32_t bus = 0u; bus < 2u; ++bus) {
            float input = previousAuxReturns_[bus]
                * params_.aux[bus].feedback;
            float sendWeight = 0.0f;
            for (uint32_t lane = 0u; lane < kNoInputMixerChannels;
                 ++lane) {
                const float send = params_.lanes[lane].auxSend[bus];
                float tapped = previousReturns_[lane];
                switch (params_.lanes[lane].auxTap[bus]) {
                case NoInputAuxTap::PreEq:
                    tapped = previousTapPreEq_[lane]; break;
                case NoInputAuxTap::PostEq:
                    tapped = previousTapPostEq_[lane]; break;
                case NoInputAuxTap::PostInsert:
                    tapped = previousTapPostInsert_[lane]; break;
                case NoInputAuxTap::Return:
                case NoInputAuxTap::Count:
                    break;
                }
                input += tapped * send;
                sendWeight += send;
            }
            input /= std::max(1.0f, 0.7f + sendWeight * 0.52f);
            const float ringSource = previousAuxReturns_[1u - bus];
            float value = processAuxInsert(bus, clamp(input, -5.0f, 5.0f),
                ringSource);
            value = dcBlock(clamp(value, -6.0f, 6.0f),
                auxState_[bus].dcInput, auxState_[bus].dcOutput);
            value = flushDenormal(value * auxMuteGain_[bus]);
            auxReturns_[bus] = value;
            auxState_[bus].activity += (std::abs(value)
                - auxState_[bus].activity) * 0.0025f;
            auxActivity_[bus] = auxState_[bus].activity;
        }
    }

    float processDistortion(InsertRuntime& runtime,
        NoInputDistortionType type, float input, float ringSource,
        const NoInputInsertParams& params)
    {
        auto& state = runtime.states[static_cast<uint32_t>(type)];
        const float gain = params.gain;
        const float tone = params.tone;
        const float rawBias = params.bias;
        const float bias = params.bias * 0.22f;
        const float sr = static_cast<float>(sampleRate_)
            * static_cast<float>(1u << params_.quality);
        const auto onePole = [sr](float frequency) {
            return 1.0f - std::exp(-2.0f * kPi
                * std::min(frequency, sr * 0.45f) / sr);
        };
        switch (type) {
        case NoInputDistortionType::Bypass:
            return input;
        case NoInputDistortionType::Wool:
            return processAnalogDriveCircuit(AnalogDriveCircuit::Wool,
                state, input, gain, tone, bias, sr);
        case NoInputDistortionType::Rat:
            return processAnalogDriveCircuit(AnalogDriveCircuit::Rat,
                state, input, gain, tone, bias, sr);
        case NoInputDistortionType::ZoneA:
            return processAnalogDriveCircuit(AnalogDriveCircuit::ZoneA,
                state, input, gain, tone, bias, sr);
        case NoInputDistortionType::ZoneB:
            return processAnalogDriveCircuit(AnalogDriveCircuit::ZoneB,
                state, input, gain, tone, bias, sr);
        case NoInputDistortionType::FuzzI:
            return processAnalogDriveCircuit(AnalogDriveCircuit::FuzzI,
                state, input, gain, tone, bias, sr);
        case NoInputDistortionType::FuzzII:
            return processAnalogDriveCircuit(AnalogDriveCircuit::FuzzII,
                state, input, gain, tone, bias, sr);
        case NoInputDistortionType::Diode:
            return processAnalogDriveCircuit(AnalogDriveCircuit::Diode,
                state, input, gain, tone, bias, sr);
        case NoInputDistortionType::Ring: {
            const float depth = gain;
            const float carrier = std::tanh(ringSource
                * (1.0f + tone * 8.0f));
            return lerp(input, input * carrier * 2.0f + bias, depth);
        }
        case NoInputDistortionType::Relay:
            return processFractureProcessor(FractureProcessor::Relay,
                runtime, state, input, ringSource, gain, tone, rawBias, sr);
        case NoInputDistortionType::Crush:
            return processFractureProcessor(FractureProcessor::Crush,
                runtime, state, input, ringSource, gain, tone, rawBias, sr);
        case NoInputDistortionType::Splice:
            return processFractureProcessor(FractureProcessor::Splice,
                runtime, state, input, ringSource, gain, tone, rawBias, sr);
        case NoInputDistortionType::Logic:
            return processFractureProcessor(FractureProcessor::Logic,
                runtime, state, input, ringSource, gain, tone, rawBias, sr);
        case NoInputDistortionType::Shred: {
            const float lowCut = lerp(180.0f, 2400.0f, tone);
            const float highCut = lerp(1800.0f, 12000.0f, tone);
            state.low += (input - state.low) * onePole(lowCut);
            state.high += (input - state.high) * onePole(highCut);
            const float mid = state.high - state.low;
            const float high = input - state.high;
            const auto fold = [](float value) {
                value = std::fmod(value + 3.0f, 4.0f);
                if (value < 0.0f) value += 4.0f;
                return std::abs(value - 2.0f) - 1.0f;
            };
            const float drive = 2.0f + gain * gain * 46.0f;
            const float low = std::tanh((state.low + bias) * drive);
            const float middle = fold((mid - bias * 0.5f)
                * drive * 1.45f);
            const float upper = clamp(high * drive * 2.2f,
                -1.0f, 1.0f);
            return std::tanh(low * 0.52f + middle * 0.68f
                + upper * 0.46f);
        }
        case NoInputDistortionType::Void:
            return processFractureProcessor(FractureProcessor::Void,
                runtime, state, input, ringSource, gain, tone, rawBias, sr);
        case NoInputDistortionType::Rotor: {
            const float rate = 0.08f * std::pow(562.5f, tone);
            state.phase = noInputWrapPhase(state.phase + rate / sr);
            const float sine = std::sin(state.phase * 2.0f * kPi);
            const float shape = std::tanh((sine + rawBias * 0.62f)
                * (1.25f + std::abs(rawBias) * 5.5f));
            const float amplitude = 0.5f + shape * 0.5f;
            return input * lerp(1.0f, amplitude * 1.32f, gain);
        }
        case NoInputDistortionType::Phase: {
            const float rate = 0.03f * std::pow(266.6667f, tone);
            state.phase = noInputWrapPhase(state.phase + rate / sr);
            const float sweep = std::sin(state.phase * 2.0f * kPi);
            const float center = 520.0f * std::pow(2.0f,
                rawBias * 2.0f + sweep * 1.75f);
            const float frequency = clamp(center, 55.0f, sr * 0.18f);
            const float tangent = std::tan(kPi * frequency / sr);
            const float coefficient = (1.0f - tangent)
                / (1.0f + tangent);
            const auto allpass = [coefficient](float sample, float& memory) {
                const float output = -coefficient * sample + memory;
                memory = flushDenormal(sample + coefficient * output);
                return output;
            };
            float phased = allpass(input, state.low);
            phased = allpass(phased, state.high);
            phased = allpass(phased, state.memory);
            phased = allpass(phased, state.envelope);
            return lerp(input, phased, gain * 0.5f);
        }
        case NoInputDistortionType::Chorus: {
            constexpr uint32_t size = 8192u;
            const float feedback = rawBias * 0.38f;
            runtime.timeBuffer[runtime.timeWrite] = clamp(
                input + state.gate * feedback, -5.0f, 5.0f);
            runtime.timeWrite = (runtime.timeWrite + 1u) % size;
            const float rate = 0.05f * std::pow(120.0f, tone);
            state.phase = noInputWrapPhase(state.phase + rate / sr);
            const float modulation = std::sin(state.phase * 2.0f * kPi);
            const float baseMs = 8.0f + std::abs(rawBias) * 11.0f;
            const float depthMs = 0.6f + gain * 8.5f;
            const float delaySamples = clamp(
                (baseMs + modulation * depthMs) * sr * 0.001f,
                1.0f, static_cast<float>(size - 3u));
            const uint32_t delayWhole = static_cast<uint32_t>(delaySamples);
            const float fraction = delaySamples
                - static_cast<float>(delayWhole);
            const uint32_t first = (runtime.timeWrite + size
                - delayWhole - 1u) % size;
            const uint32_t second = (first + size - 1u) % size;
            const float delayed = lerp(runtime.timeBuffer[first],
                runtime.timeBuffer[second], fraction);
            state.gate = flushDenormal(delayed);
            return lerp(input, delayed, gain * 0.78f);
        }
        case NoInputDistortionType::Throat:
            return processFractureProcessor(FractureProcessor::Throat,
                runtime, state, input, ringSource, gain, tone, rawBias, sr);
        case NoInputDistortionType::Robot:
            return processFractureProcessor(FractureProcessor::Robot,
                runtime, state, input, ringSource, gain, tone, rawBias, sr);
        case NoInputDistortionType::OctDown:
            return processFractureProcessor(FractureProcessor::OctDown,
                runtime, state, input, ringSource, gain, tone, rawBias, sr);
        case NoInputDistortionType::OctUp:
            return processFractureProcessor(FractureProcessor::OctUp,
                runtime, state, input, ringSource, gain, tone, rawBias, sr);
        case NoInputDistortionType::OctStack:
            return processFractureProcessor(FractureProcessor::OctStack,
                runtime, state, input, ringSource, gain, tone, rawBias, sr);
        case NoInputDistortionType::Count:
            break;
        }
        return input;
    }

    float dcBlock(float input, float& previousInput, float& previousOutput)
    {
        if (params_.dcBlockEnabled == 0u) return input;
        const float output = input - previousInput + dcPole_ * previousOutput;
        previousInput = input;
        previousOutput = flushDenormal(output);
        return previousOutput;
    }

    float processTone(float input, float tone, float& lowState)
    {
        const float coefficient = 1.0f - std::exp(-2.0f * kPi * 1150.0f
            / static_cast<float>(sampleRate_));
        lowState += (input - lowState) * coefficient;
        lowState = flushDenormal(lowState);
        if (tone < 0.0f) {
            return lerp(input, lowState, -tone * 0.88f);
        }
        const float high = input - lowState;
        return input + high * tone * 1.25f;
    }

    static float softLimit(float input, float ceiling)
    {
        ceiling = std::max(0.001f, ceiling);
        const float absolute = std::abs(input);
        const float knee = ceiling * 0.72f;
        if (absolute <= knee) return input;
        const float span = std::max(1.0e-6f, ceiling - knee);
        const float limited = knee + span
            * (1.0f - std::exp(-(absolute - knee) / span));
        return std::copysign(std::min(ceiling, limited), input);
    }

    float seedForLane(uint32_t lane)
    {
        if (seedRemaining_ == 0u) return 0.0f;
        const float phase = static_cast<float>(seedRemaining_)
            / static_cast<float>(std::max<uint32_t>(1u,
                static_cast<uint32_t>(sampleRate_ * 0.018)));
        const float envelope = phase * phase;
        const float random = randomSigned();
        const float polarity = (lane & 1u) == 0u ? 1.0f : -1.0f;
        return random * envelope * seedAmount_ * 0.34f
            * polarity * (0.72f + 0.055f * static_cast<float>(lane));
    }

    float effectiveMotionWeight(uint32_t index) const
    {
        if (behaviorParams_.behavior == NoInputMovementBehavior::Glide) {
            return motionCurrent_[index];
        }
        if (behaviorParams_.behavior == NoInputMovementBehavior::Step) {
            return lerp(motionCurrent_[index], behaviorCurrent_[index],
                behaviorDepth_);
        }
        // Binary articulation selects a topology mask independently from the
        // continuously moving field. The field supplies motion weight while
        // behaviorCurrent_ supplies one explicit amplitude window below.
        return motionCurrent_[index];
    }

    float effectiveMatrixGain(uint32_t index) const
    {
        if (index >= kNoInputMixerMatrixCells) return 0.0f;
        return midiMatrixActive_[index] != 0u
            ? midiMatrixGain_[index] : params_.matrix[index];
    }

    void advanceMidiMatrixConnections()
    {
        for (uint32_t index = 0u;
             index < kNoInputMixerMatrixCells; ++index) {
            if (midiMatrixActive_[index] == 0u) continue;
            midiMatrixGain_[index] +=
                (midiMatrixTarget_[index] - midiMatrixGain_[index])
                * midiMatrixSlew_;
            if (midiMatrixReleasePending_[index] != 0u
                && std::abs(midiMatrixTarget_[index]
                    - midiMatrixGain_[index]) <= 1.0e-3f) {
                midiMatrixActive_[index] = 0u;
                midiMatrixReleasePending_[index] = 0u;
                midiMatrixGain_[index] = 0.0f;
                midiMatrixTarget_[index] = 0.0f;
            }
        }
    }

    uint32_t behaviorHash(uint32_t salt) const
    {
        uint32_t value = params_.seed ^ (behaviorEventCount_ * 0x9e3779b9u)
            ^ salt;
        value ^= value >> 16u;
        value *= 0x7feb352du;
        value ^= value >> 15u;
        value *= 0x846ca68bu;
        value ^= value >> 16u;
        return value;
    }

    float behaviorHashUnit(uint32_t salt) const
    {
        return static_cast<float>(behaviorHash(salt) & 0x00ffffffu)
            / static_cast<float>(0x01000000u);
    }

    void triggerMovementBehaviorEvent()
    {
        ++behaviorEventCount_;
        const auto behavior = behaviorParams_.behavior;
        behaviorStart_ = behaviorCurrent_;
        if (behavior == NoInputMovementBehavior::Step) {
            behaviorTarget_ = motionTarget_;
        } else {
            behaviorTarget_.fill(0.0f);
            behaviorOrder_.fill(0.0f);
            const float density = behaviorParams_.density;
            const uint32_t burstDestination = behaviorHash(0x42555253u)
                % kNoInputMixerChannels;
            for (uint32_t source = 0u; source < kNoInputMixerChannels;
                 ++source) {
                uint32_t activeCount = 0u;
                uint32_t strongestIndex = 0u;
                float strongest = -1.0f;
                for (uint32_t destination = 0u;
                     destination < kNoInputMixerChannels; ++destination) {
                    const uint32_t index = destination
                        * kNoInputMixerChannels + source;
                    if (std::abs(effectiveMatrixGain(index)) < 1.0e-7f)
                        continue;
                    const float random = behaviorHashUnit(index * 0x45d9f3bu
                        + 0x53544550u);
                    behaviorOrder_[index] = clamp(
                        motionTarget_[index]
                            * (1.0f - behaviorParams_.chaos)
                        + random * behaviorParams_.chaos,
                        0.0f, 1.0f);
                    float probability = density;
                    if (behavior == NoInputMovementBehavior::Cut) {
                        probability *= 0.45f + motionTarget_[index] * 0.75f;
                    } else if (behavior == NoInputMovementBehavior::Burst) {
                        const uint32_t ringDistance = std::min<uint32_t>(
                            (destination + 8u - burstDestination) % 8u,
                            (burstDestination + 8u - destination) % 8u);
                        const float cluster = std::exp(
                            -static_cast<float>(ringDistance)
                                * lerp(1.4f, 0.18f,
                                    behaviorParams_.chaos));
                        probability *= cluster;
                    }
                    const float ranking = random
                        + motionTarget_[index] * (0.34f
                            * (1.0f - behaviorParams_.chaos));
                    if (ranking > strongest) {
                        strongest = ranking;
                        strongestIndex = index;
                    }
                    if (behavior == NoInputMovementBehavior::Cascade
                        || behavior == NoInputMovementBehavior::Erode
                        || random < clamp(probability, 0.0f, 1.0f)) {
                        behaviorTarget_[index] = 1.0f;
                        ++activeCount;
                    }
                }
                if ((behavior == NoInputMovementBehavior::Scramble
                        || behavior == NoInputMovementBehavior::Ratchet)
                    && activeCount == 0u && strongest >= 0.0f
                    && density > 0.001f) {
                    behaviorTarget_[strongestIndex] = 1.0f;
                }
                if (behavior == NoInputMovementBehavior::Erode
                    && activeCount > 0u) {
                    for (uint32_t destination = 0u;
                         destination < kNoInputMixerChannels;
                         ++destination) {
                        const uint32_t index = destination
                            * kNoInputMixerChannels + source;
                        if (behaviorTarget_[index] <= 0.0f) continue;
                        uint32_t rank = 0u;
                        for (uint32_t other = 0u;
                             other < kNoInputMixerChannels; ++other) {
                            const uint32_t otherIndex = other
                                * kNoInputMixerChannels + source;
                            if (behaviorTarget_[otherIndex] > 0.0f
                                && behaviorOrder_[otherIndex]
                                    < behaviorOrder_[index]) {
                                ++rank;
                            }
                        }
                        behaviorOrder_[index] =
                            (static_cast<float>(rank) + 0.5f)
                            / static_cast<float>(activeCount);
                    }
                }
            }
        }

        behaviorRatchetCount_ = 2u + static_cast<uint32_t>(std::lround(
            behaviorParams_.density * 3.0f
                + behaviorParams_.chaos * 4.0f));
        const float deterministicStart = static_cast<float>(
            (behaviorEventCount_ - 1u) % kNoInputMixerChannels);
        const float randomStart = static_cast<float>(
            behaviorHash(0x43415343u) % kNoInputMixerChannels);
        behaviorCascadeStart_ = lerp(deterministicStart, randomStart,
            behaviorParams_.chaos);
        behaviorCascadeDirection_ = params_.vortex < -0.001f ? -1.0f : 1.0f;
        if (behaviorHashUnit(0x44495245u) < behaviorParams_.chaos * 0.5f) {
            behaviorCascadeDirection_ = -behaviorCascadeDirection_;
        }

        const float hz = params_.clockSync != 0u && transportHasTempo_
            ? noInputSyncedRateHz(params_.eventDivision,
                transportTempoBpm_)
            : noInputMovementEventRateHz(behaviorParams_.eventRate,
                params_.slowTime != 0u);
        float interval = static_cast<float>(sampleRate_) / hz;
        const float jitter = (behaviorHashUnit(0x4a495454u) * 2.0f - 1.0f)
            * behaviorParams_.chaos;
        const float jitterDepth = behavior == NoInputMovementBehavior::Burst
                || behavior == NoInputMovementBehavior::Ratchet
            ? 0.82f : (behavior == NoInputMovementBehavior::Cascade
                ? 0.56f : 0.38f);
        interval *= std::max(0.12f, 1.0f + jitter * jitterDepth);
        behaviorSamplesUntilEvent_ = std::max<uint32_t>(1u,
            static_cast<uint32_t>(interval));

        const uint32_t edgeSamples = std::max<uint32_t>(2u,
            static_cast<uint32_t>(std::lround(
                static_cast<float>(sampleRate_)
                * noInputMovementSlewMs(behaviorParams_.slew) * 0.001f)));
        if (noInputMovementBehaviorUsesLength(behavior)) {
            const float lengthSamples = static_cast<float>(sampleRate_)
                * noInputMovementLengthMs(behaviorParams_.length) * 0.001f;
            behaviorEnvelopeSamples_ = std::max<uint32_t>(4u,
                static_cast<uint32_t>(std::min(lengthSamples,
                    interval * lerp(0.25f, 0.95f,
                        behaviorParams_.density))));
            if (behavior == NoInputMovementBehavior::Ratchet) {
                const uint32_t minimumSmoothRatchet =
                    behaviorRatchetCount_ * edgeSamples * 4u;
                const uint32_t maximumEventLength = std::max<uint32_t>(
                    4u, static_cast<uint32_t>(interval * 0.95f));
                behaviorEnvelopeSamples_ = std::max(
                    behaviorEnvelopeSamples_, std::min(
                        minimumSmoothRatchet, maximumEventLength));
            }
            if (behavior == NoInputMovementBehavior::Erode) {
                const uint32_t minimumSmoothErosion = edgeSamples * 12u;
                const uint32_t maximumEventLength = std::max<uint32_t>(
                    4u, static_cast<uint32_t>(interval * 0.95f));
                behaviorEnvelopeSamples_ = std::max(
                    behaviorEnvelopeSamples_, std::min(
                        minimumSmoothErosion, maximumEventLength));
            }
            const float attackScale = behavior
                    == NoInputMovementBehavior::Burst
                ? lerp(0.90f, 0.72f, behaviorParams_.chaos)
                : 1.0f;
            const uint32_t desiredAttack = std::max<uint32_t>(2u,
                static_cast<uint32_t>(std::lround(
                    static_cast<float>(edgeSamples) * attackScale)));
            behaviorAttackSamples_ = std::max<uint32_t>(2u,
                std::min<uint32_t>(desiredAttack,
                    behaviorEnvelopeSamples_ / 2u));
            const float releaseScale = behavior
                    == NoInputMovementBehavior::Burst
                ? lerp(1.35f, 2.15f, behaviorParams_.chaos)
                : 1.0f;
            const uint32_t desiredRelease = std::max<uint32_t>(2u,
                static_cast<uint32_t>(std::lround(
                    static_cast<float>(edgeSamples) * releaseScale)));
            behaviorReleaseSamples_ = std::max<uint32_t>(2u,
                std::min<uint32_t>(desiredRelease,
                    behaviorEnvelopeSamples_ - behaviorAttackSamples_));
            behaviorEnvelopeSample_ = 0u;
        } else if (behavior == NoInputMovementBehavior::Scramble) {
            behaviorEnvelopeSamples_ = edgeSamples;
            behaviorAttackSamples_ = edgeSamples;
            behaviorReleaseSamples_ = 0u;
            behaviorEnvelopeSample_ = 0u;
        } else {
            behaviorEnvelopeSample_ = 0u;
            behaviorEnvelopeSamples_ = 0u;
            behaviorAttackSamples_ = 0u;
            behaviorReleaseSamples_ = 0u;
        }
    }

    static float raisedCosine(float normalized)
    {
        normalized = clamp(normalized, 0.0f, 1.0f);
        return 0.5f - 0.5f * std::cos(kPi * normalized);
    }

    void updateWindowedMovementBehavior()
    {
        const auto behavior = behaviorParams_.behavior;
        if (behavior == NoInputMovementBehavior::Scramble) {
            if (behaviorEnvelopeSamples_ <= 1u
                || behaviorEnvelopeSample_ >= behaviorEnvelopeSamples_) {
                behaviorCurrent_ = behaviorTarget_;
                return;
            }
            const float phase = static_cast<float>(behaviorEnvelopeSample_)
                / static_cast<float>(behaviorEnvelopeSamples_ - 1u);
            const float crossfade = raisedCosine(phase);
            for (uint32_t index = 0u;
                 index < kNoInputMixerMatrixCells; ++index) {
                behaviorCurrent_[index] = flushDenormal(clamp(
                    lerp(behaviorStart_[index], behaviorTarget_[index],
                        crossfade), 0.0f, 1.0f));
            }
            ++behaviorEnvelopeSample_;
            return;
        }

        if (behavior == NoInputMovementBehavior::Ratchet) {
            if (behaviorEnvelopeSamples_ <= 1u
                || behaviorEnvelopeSample_ >= behaviorEnvelopeSamples_) {
                behaviorCurrent_.fill(0.0f);
                return;
            }
            const float phase = static_cast<float>(behaviorEnvelopeSample_)
                / static_cast<float>(behaviorEnvelopeSamples_);
            const float cycles = static_cast<float>(
                std::max<uint32_t>(2u, behaviorRatchetCount_));
            const float pulsePhase = std::fmod(phase * cycles, 1.0f);
            const float duty = lerp(0.24f, 0.78f,
                behaviorParams_.density);
            const float subcycleSamples = static_cast<float>(
                behaviorEnvelopeSamples_) / cycles;
            const float edge = clamp(static_cast<float>(
                behaviorAttackSamples_) / std::max(2.0f, subcycleSamples),
                0.01f, duty * 0.48f);
            float window = 0.0f;
            if (pulsePhase < duty) {
                const float attack = raisedCosine(
                    pulsePhase / std::max(0.001f, edge));
                const float release = 1.0f - raisedCosine(
                    (pulsePhase - (duty - edge))
                        / std::max(0.001f, edge));
                window = std::min(attack, release);
            }
            const float eventAttack = raisedCosine(
                static_cast<float>(behaviorEnvelopeSample_)
                    / static_cast<float>(
                        std::max<uint32_t>(2u, behaviorAttackSamples_)));
            for (uint32_t index = 0u;
                 index < kNoInputMixerMatrixCells; ++index) {
                const float target = behaviorTarget_[index] * window;
                behaviorCurrent_[index] = flushDenormal(clamp(
                    lerp(behaviorStart_[index], target, eventAttack),
                    0.0f, 1.0f));
            }
            ++behaviorEnvelopeSample_;
            return;
        }

        if (behavior == NoInputMovementBehavior::Cascade) {
            if (behaviorEnvelopeSamples_ <= 1u
                || behaviorEnvelopeSample_ >= behaviorEnvelopeSamples_) {
                behaviorCurrent_.fill(0.0f);
                return;
            }
            const float phase = static_cast<float>(behaviorEnvelopeSample_)
                / static_cast<float>(behaviorEnvelopeSamples_ - 1u);
            float center = std::fmod(behaviorCascadeStart_
                + behaviorCascadeDirection_ * phase * 8.0f, 8.0f);
            if (center < 0.0f) center += 8.0f;
            const float trail = 0.20f
                + behaviorParams_.density * 2.65f;
            const float attack = raisedCosine(
                static_cast<float>(behaviorEnvelopeSample_)
                    / static_cast<float>(
                        std::max<uint32_t>(2u, behaviorAttackSamples_)));
            const uint32_t releaseStart = behaviorEnvelopeSamples_
                - behaviorReleaseSamples_;
            const float release = behaviorEnvelopeSample_ < releaseStart
                ? 1.0f : 1.0f - raisedCosine(
                    static_cast<float>(behaviorEnvelopeSample_
                        - releaseStart)
                    / static_cast<float>(
                        std::max<uint32_t>(2u, behaviorReleaseSamples_)));
            for (uint32_t index = 0u;
                 index < kNoInputMixerMatrixCells; ++index) {
                const uint32_t destination = index
                    / kNoInputMixerChannels;
                float point = static_cast<float>(destination)
                    + (behaviorOrder_[index] - 0.5f)
                        * behaviorParams_.chaos * 0.90f;
                point = std::fmod(point, 8.0f);
                if (point < 0.0f) point += 8.0f;
                float distance = std::abs(point - center);
                distance = std::min(distance, 8.0f - distance);
                const float gate = behaviorTarget_[index]
                    * std::exp(-(distance * distance)
                        / std::max(0.001f, trail * trail));
                behaviorCurrent_[index] = flushDenormal(clamp(
                    lerp(behaviorStart_[index], gate, attack)
                        * release, 0.0f, 1.0f));
            }
            ++behaviorEnvelopeSample_;
            return;
        }

        if (behavior == NoInputMovementBehavior::Erode) {
            const float edgeCoefficient = movementBehaviorSlewCoefficient_;
            if (behaviorEnvelopeSamples_ <= 1u
                || behaviorEnvelopeSample_ >= behaviorEnvelopeSamples_) {
                for (float& gate : behaviorCurrent_) {
                    gate += (1.0f - gate) * edgeCoefficient;
                    gate = flushDenormal(clamp(gate, 0.0f, 1.0f));
                }
                return;
            }
            const float phase = static_cast<float>(behaviorEnvelopeSample_)
                / static_cast<float>(behaviorEnvelopeSamples_ - 1u);
            const float erosion = phase < 0.72f
                ? raisedCosine(phase / 0.72f)
                : 1.0f - raisedCosine((phase - 0.72f) / 0.28f);
            const float removal = erosion
                * (1.0f - behaviorParams_.density * 0.90f);
            const float softness = 0.015f
                + static_cast<float>(behaviorAttackSamples_)
                    / static_cast<float>(behaviorEnvelopeSamples_)
                    * 2.25f;
            for (uint32_t index = 0u;
                 index < kNoInputMixerMatrixCells; ++index) {
                float gate = 1.0f;
                if (behaviorTarget_[index] > 0.0f && removal > 0.0f) {
                    gate = raisedCosine((behaviorOrder_[index] - removal)
                        / std::max(0.001f, softness) + 0.5f);
                }
                behaviorCurrent_[index] += (gate
                    - behaviorCurrent_[index]) * edgeCoefficient;
                behaviorCurrent_[index] = flushDenormal(clamp(
                    behaviorCurrent_[index], 0.0f, 1.0f));
            }
            ++behaviorEnvelopeSample_;
            return;
        }

        if (behavior != NoInputMovementBehavior::Cut
            && behavior != NoInputMovementBehavior::Burst) return;
        if (behaviorEnvelopeSamples_ <= 1u
            || behaviorEnvelopeSample_ >= behaviorEnvelopeSamples_) {
            behaviorCurrent_.fill(0.0f);
            return;
        }

        const uint32_t sample = behaviorEnvelopeSample_;
        const uint32_t releaseStart = behaviorEnvelopeSamples_
            - behaviorReleaseSamples_;
        if (sample < behaviorAttackSamples_) {
            const float phase = behaviorAttackSamples_ > 1u
                ? static_cast<float>(sample)
                    / static_cast<float>(behaviorAttackSamples_ - 1u)
                : 1.0f;
            const float attack = raisedCosine(phase);
            for (uint32_t index = 0u;
                 index < kNoInputMixerMatrixCells; ++index) {
                behaviorCurrent_[index] = flushDenormal(clamp(
                    lerp(behaviorStart_[index], behaviorTarget_[index],
                        attack), 0.0f, 1.0f));
            }
        } else if (sample >= releaseStart) {
            const float phase = behaviorReleaseSamples_ > 1u
                ? static_cast<float>(sample - releaseStart)
                    / static_cast<float>(behaviorReleaseSamples_ - 1u)
                : 1.0f;
            const float release = 1.0f - raisedCosine(phase);
            for (uint32_t index = 0u;
                 index < kNoInputMixerMatrixCells; ++index) {
                behaviorCurrent_[index] = flushDenormal(
                    behaviorTarget_[index] * release);
            }
        } else {
            behaviorCurrent_ = behaviorTarget_;
        }
        ++behaviorEnvelopeSample_;
    }

    void updateMovementBehavior()
    {
        if (behaviorParams_.behavior == NoInputMovementBehavior::Glide) {
            return;
        }
        refreshMovementCoefficients();
        if (behaviorSamplesUntilEvent_ == 0u) {
            triggerMovementBehaviorEvent();
        } else {
            --behaviorSamplesUntilEvent_;
        }

        if (noInputMovementBehaviorUsesAmplitude(
                behaviorParams_.behavior)) {
            updateWindowedMovementBehavior();
        } else {
            for (uint32_t index = 0u;
                 index < kNoInputMixerMatrixCells; ++index) {
                behaviorCurrent_[index] += (behaviorTarget_[index]
                    - behaviorCurrent_[index])
                    * movementBehaviorSlewCoefficient_;
                behaviorCurrent_[index] = flushDenormal(
                    clamp(behaviorCurrent_[index], 0.0f, 1.0f));
            }
        }
        laneBehaviorGate_.fill(0.0f);
        for (uint32_t destination = 0u;
             destination < kNoInputMixerChannels; ++destination) {
            bool hasRoute = false;
            for (uint32_t source = 0u; source < kNoInputMixerChannels;
                 ++source) {
                const uint32_t index = destination
                    * kNoInputMixerChannels + source;
                if (std::abs(effectiveMatrixGain(index)) < 1.0e-7f)
                    continue;
                hasRoute = true;
                laneBehaviorGate_[destination] = std::max(
                    laneBehaviorGate_[destination], behaviorCurrent_[index]);
            }
            if (!hasRoute) laneBehaviorGate_[destination] = 1.0f;
        }
    }

    bool movementBehaviorLayerIsNeutral() const
    {
        if (behaviorParams_.behavior == NoInputMovementBehavior::Glide)
            return true;
        // CHOKE is an independent use of the behavior window, so a zero
        // route depth is neutral only when the post-lane choke is also zero.
        return behaviorDepth_ <= 1.0e-7f
            && behaviorDepthTarget_ <= 1.0e-7f
            && behaviorParams_.choke <= 1.0e-7f
            && behaviorTargetParams_.choke <= 1.0e-7f;
    }

    void refreshMovementCoefficients()
    {
        if (movementCoefficientSlew_ == behaviorParams_.slew
            && movementCoefficientSampleRate_ == sampleRate_) return;
        movementCoefficientSlew_ = behaviorParams_.slew;
        movementCoefficientSampleRate_ = sampleRate_;
        const float slewMs = noInputMovementSlewMs(behaviorParams_.slew);
        const float slewSeconds = slewMs * 0.001f;
        movementBehaviorSlewCoefficient_ = 1.0f - std::exp(-1.0f
            / std::max(1.0f, static_cast<float>(sampleRate_)
                * slewSeconds));
        // Times describe near-complete (60 dB) settling. The asymmetric
        // release is the slow, memory-bearing half of the vactrol response.
        const float openMs = 8.0f + slewMs * 1.6f;
        const float closeMs = 45.0f + slewMs * 5.25f;
        const auto coefficient = [this](float milliseconds) {
            constexpr float kSettle = 6.90775527898f;
            return 1.0f - std::exp(-kSettle
                / std::max(1.0f, static_cast<float>(sampleRate_)
                    * milliseconds * 0.001f));
        };
        movementCircuitOpenCoefficient_ = coefficient(openMs);
        movementCircuitCloseCoefficient_ = coefficient(closeMs);
    }

    void advanceMovementCircuitGates(bool behaviorNeutral)
    {
        const bool circuitBehavior = noInputMovementBehaviorUsesAmplitude(
            behaviorParams_.behavior) && !behaviorNeutral;
        const bool routeCircuitAudible = behaviorDepth_ > 1.0e-7f;
        const bool laneCircuitAudible = behaviorParams_.choke > 1.0e-7f;
        const bool targetActive = circuitBehavior
            && (routeCircuitAudible || laneCircuitAudible);

        if (targetActive && !movementCircuitProcessing_) {
            // A disabled behavior has no meaningful hidden optical state.
            // Re-enter from the transparent side and let both the parameter
            // ramp and vactrol response close smoothly toward the new target.
            behaviorCircuitGate_.fill(1.0f);
            laneBehaviorCircuitGate_.fill(1.0f);
            movementCircuitProcessing_ = true;
        }
        if (!targetActive) {
            if (!routeCircuitAudible && !laneCircuitAudible) {
                movementCircuitProcessing_ = false;
                return;
            }
            // Non-amplitude modes release the previous LPG to transparent.
            if (!movementCircuitProcessing_) return;
        }

        refreshMovementCoefficients();
        bool released = !targetActive;
        for (uint32_t index = 0u;
             index < kNoInputMixerMatrixCells; ++index) {
            const float target = targetActive
                ? behaviorCurrent_[index] : 1.0f;
            const float amount = target > behaviorCircuitGate_[index]
                ? movementCircuitOpenCoefficient_
                : movementCircuitCloseCoefficient_;
            behaviorCircuitGate_[index] += (target
                - behaviorCircuitGate_[index]) * amount;
            behaviorCircuitGate_[index] = flushDenormal(clamp(
                behaviorCircuitGate_[index], 0.0f, 1.0f));
            if (released
                && behaviorCircuitGate_[index] < 0.9995f) released = false;
        }
        for (uint32_t lane = 0u; lane < kNoInputMixerChannels; ++lane) {
            const float target = targetActive
                ? laneBehaviorGate_[lane] : 1.0f;
            const float amount = target > laneBehaviorCircuitGate_[lane]
                ? movementCircuitOpenCoefficient_
                : movementCircuitCloseCoefficient_;
            laneBehaviorCircuitGate_[lane] += (target
                - laneBehaviorCircuitGate_[lane]) * amount;
            laneBehaviorCircuitGate_[lane] = flushDenormal(clamp(
                laneBehaviorCircuitGate_[lane], 0.0f, 1.0f));
            if (released
                && laneBehaviorCircuitGate_[lane] < 0.9995f)
                released = false;
        }
        if (released) {
            // processMovementLpg() is already exactly transparent at this
            // threshold, so subsequent samples can omit all circuit work.
            behaviorCircuitGate_.fill(1.0f);
            laneBehaviorCircuitGate_.fill(1.0f);
            movementCircuitProcessing_ = false;
        }
    }

    float processMovementLpg(float input, float gate, float& lowpass)
    {
        gate = clamp(gate, 0.0f, 1.0f);
        if (gate >= 0.9995f) {
            lowpass = input;
            return input;
        }
        // A one-pole low-pass follows the optical gate. At an open gate the
        // path is transparent; as it closes, bandwidth falls before silence,
        // avoiding a broadband edge inside the feedback circuit.
        const float cutoff = 70.0f + gate * gate * 17930.0f;
        const float angular = 2.0f * kPi * cutoff
            / static_cast<float>(sampleRate_);
        const float filterCoefficient = angular / (1.0f + angular);
        lowpass += (input - lowpass) * filterCoefficient;
        lowpass = flushDenormal(lowpass);
        const float toneMix = 1.0f - gate;
        return flushDenormal(lerp(input, lowpass, toneMix) * gate);
    }

    void updateReact()
    {
        if (params_.reactMode == NoInputReactMode::Off) {
            reactCurrent_.fill(1.0f);
            reactEdge_.fill(0.0f);
            return;
        }
        const float threshold = noInputReactThreshold(
            params_.reactThreshold);
        const float attackSeconds = noInputReactAttackMs(
            params_.reactAttack) * 0.001f;
        const float releaseSeconds = noInputReactReleaseMs(
            params_.reactRelease) * 0.001f;
        const float attack = 1.0f - std::exp(-1.0f
            / std::max(1.0f, static_cast<float>(sampleRate_)
                * attackSeconds));
        const float release = 1.0f - std::exp(-1.0f
            / std::max(1.0f, static_cast<float>(sampleRate_)
                * releaseSeconds));
        for (uint32_t lane = 0u; lane < kNoInputMixerChannels; ++lane) {
            const bool above = laneActivity_[lane] >= threshold;
            if (above && reactWasAbove_[lane] == 0u) {
                reactEdge_[lane] = 1.0f;
            } else {
                reactEdge_[lane] += (0.0f - reactEdge_[lane]) * release;
            }
            reactWasAbove_[lane] = above ? 1u : 0u;
        }
        const float range = std::max(0.0005f, threshold * 3.0f);
        for (uint32_t destination = 0u;
             destination < kNoInputMixerChannels; ++destination) {
            const float destinationGate = clamp(
                (laneActivity_[destination] - threshold) / range,
                0.0f, 1.0f);
            for (uint32_t source = 0u;
                 source < kNoInputMixerChannels; ++source) {
                const uint32_t index = destination
                    * kNoInputMixerChannels + source;
                const float sourceGate = clamp(
                    (laneActivity_[source] - threshold) / range,
                    0.0f, 1.0f);
                float target = 1.0f;
                switch (params_.reactMode) {
                case NoInputReactMode::Follow:
                    target = sourceGate; break;
                case NoInputReactMode::Avoid:
                    target = 1.0f - destinationGate; break;
                case NoInputReactMode::Edge:
                    target = reactEdge_[source]; break;
                case NoInputReactMode::Balance:
                    target = clamp(0.5f
                        + (laneActivity_[source]
                            - laneActivity_[destination])
                            / std::max(0.001f, range * 2.0f),
                        0.0f, 1.0f);
                    break;
                case NoInputReactMode::Off:
                case NoInputReactMode::Count:
                    target = 1.0f; break;
                }
                if (params_.reactPolarity < 0.0f) target = 1.0f - target;
                const float coefficient = target > reactCurrent_[index]
                    ? attack : release;
                reactCurrent_[index] += (target - reactCurrent_[index])
                    * coefficient;
                reactCurrent_[index] = flushDenormal(clamp(
                    reactCurrent_[index], 0.0f, 1.0f));
            }
        }
    }

    void updateSlowControl()
    {
        rebuildMotionTargets();
        for (uint32_t index = 0u;
             index < kNoInputMixerMatrixCells; ++index) {
            driftVelocity_[index] += randomSigned() * 0.014f;
            driftVelocity_[index] *= 0.972f;
            driftState_[index] += driftVelocity_[index] * 0.025f;
            driftState_[index] *= 0.9992f;
            if (driftState_[index] > 1.0f) {
                driftState_[index] = 1.0f;
                driftVelocity_[index] = -std::abs(driftVelocity_[index]);
            } else if (driftState_[index] < -1.0f) {
                driftState_[index] = -1.0f;
                driftVelocity_[index] = std::abs(driftVelocity_[index]);
            }
            motionCurrent_[index] += (motionTarget_[index]
                - motionCurrent_[index]) * 0.16f;
            const uint32_t source = index % kNoInputMixerChannels;
            const float closeThreshold = 0.0015f + params_.space
                * (0.010f + hashUnit(index + 0x53504143u) * 0.022f);
            const float openThreshold = closeThreshold
                * (1.8f + params_.agency * 1.6f);
            const float activity = laneActivity_[source];
            float gateTarget = routeSpaceGate_[index];
            if (seedRemaining_ > 0u || activity > openThreshold) {
                gateTarget = 1.0f;
            } else if (activity < closeThreshold) {
                gateTarget = 0.025f;
            }
            const float gateSpeed = gateTarget > routeSpaceGate_[index]
                ? 0.045f : 0.012f;
            routeSpaceGate_[index] += (gateTarget
                - routeSpaceGate_[index]) * gateSpeed;
        }
    }

    void rebuildMotionTargets()
    {
        const auto generated = noInputMixerMotionWeights(params_,
            motionPhase_);
        for (uint32_t index = 0u; index < kNoInputMixerMatrixCells;
             ++index) {
            motionTarget_[index] = generated[index];
        }
    }

    void clearSignalState()
    {
        for (auto& lane : laneState_) clearLaneSignalState(lane);
        returns_.fill(0.0f);
        previousReturns_.fill(0.0f);
        tapPreEq_.fill(0.0f);
        tapPostEq_.fill(0.0f);
        tapPostInsert_.fill(0.0f);
        previousTapPreEq_.fill(0.0f);
        previousTapPostEq_.fill(0.0f);
        previousTapPostInsert_.fill(0.0f);
        routeSignals_.fill(0.0f);
        phaseMemory_.fill(0.0f);
        driftState_.fill(0.0f);
        driftVelocity_.fill(0.0f);
        motionTarget_.fill(1.0f);
        motionCurrent_.fill(1.0f);
        behaviorTarget_.fill(1.0f);
        behaviorStart_.fill(1.0f);
        behaviorCurrent_.fill(1.0f);
        behaviorOrder_.fill(0.0f);
        laneBehaviorGate_.fill(1.0f);
        behaviorCircuitGate_.fill(1.0f);
        behaviorRouteLowpass_.fill(0.0f);
        laneBehaviorCircuitGate_.fill(1.0f);
        laneBehaviorLowpass_.fill(0.0f);
        movementCircuitProcessing_ = false;
        routeSpaceGate_.fill(1.0f);
        reactCurrent_.fill(1.0f);
        reactEdge_.fill(0.0f);
        reactWasAbove_.fill(0u);
        for (auto& aux : auxState_) clearAuxSignalState(aux);
        auxReturns_.fill(0.0f);
        previousAuxReturns_.fill(0.0f);
        auxActivity_.fill(0.0f);
        for (uint32_t bus = 0u; bus < 2u; ++bus) {
            auxMuteGain_[bus] = auxMuted_[bus] != 0u ? 0.0f : 1.0f;
        }
        lanePeak_.fill(0.0f);
        laneActivity_.fill(0.0f);
        responseEnergy_.fill(0.0f);
        networkActivity_ = 0.0f;
        controlCounter_ = 0u;
        behaviorEventCount_ = 0u;
        behaviorSamplesUntilEvent_ = 0u;
        behaviorEnvelopeSample_ = 0u;
        behaviorEnvelopeSamples_ = 0u;
        behaviorAttackSamples_ = 0u;
        behaviorReleaseSamples_ = 0u;
        behaviorRatchetCount_ = 2u;
        behaviorCascadeStart_ = 0.0f;
        behaviorCascadeDirection_ = 1.0f;
        motionPhase_ = params_.motionPhase;
        for (uint32_t lane = 0u; lane < kNoInputMixerChannels; ++lane) {
            updateEq(lane);
            updateBodyCoefficients(lane);
            eqCoefficientsDirty_[lane] = 0u;
            bodyCoefficientsDirty_[lane] = 0u;
            initializeInsertTypes(lane);
        }
        for (uint32_t bus = 0u; bus < 2u; ++bus) {
            auto& runtime = auxState_[bus].insert;
            const auto type = params_.aux[bus].effect.type;
            runtime.currentType = type;
            runtime.previousType = type;
            runtime.crossfade = 1.0f;
            runtime.initialized = true;
        }
        rebuildMotionTargets();
        motionCurrent_ = motionTarget_;
    }

    float minimumLaneGovernor() const
    {
        float result = 1.0f;
        for (const auto& lane : laneState_) {
            result = std::min(result, lane.governor);
        }
        return result;
    }

    uint32_t randomU32()
    {
        randomState_ += 0x9e3779b9u;
        uint32_t value = randomState_;
        value ^= value >> 16u;
        value *= 0x7feb352du;
        value ^= value >> 15u;
        value *= 0x846ca68bu;
        value ^= value >> 16u;
        return value;
    }

    float randomSigned()
    {
        return static_cast<float>(randomU32() & 0x00ffffffu)
            / static_cast<float>(0x00800000u) - 1.0f;
    }

    static float hashUnit(uint32_t value)
    {
        value ^= value >> 16u;
        value *= 0x7feb352du;
        value ^= value >> 15u;
        value *= 0x846ca68bu;
        value ^= value >> 16u;
        return static_cast<float>(value & 0x00ffffffu)
            / static_cast<float>(0x01000000u);
    }

    double sampleRate_ = 48000.0;
    NoInputMixerParams params_ = defaultNoInputMixerParams();
    NoInputMixerParams targetParams_ = defaultNoInputMixerParams();
    std::array<float, kNoInputMixerMatrixCells> midiMatrixGain_ {};
    std::array<float, kNoInputMixerMatrixCells> midiMatrixTarget_ {};
    std::array<uint32_t, kNoInputMixerMatrixCells> midiMatrixActive_ {};
    std::array<uint32_t,
        kNoInputMixerMatrixCells> midiMatrixReleasePending_ {};
    NoInputMovementBehaviorParams behaviorParams_ {};
    NoInputMovementBehaviorParams behaviorTargetParams_ {};
    std::array<LaneState, kNoInputMixerChannels> laneState_ {};
    std::array<AuxState, 2u> auxState_ {};
    std::array<float, kNoInputMixerChannels> returns_ {};
    std::array<float, kNoInputMixerChannels> previousReturns_ {};
    std::array<float, kNoInputMixerChannels> tapPreEq_ {};
    std::array<float, kNoInputMixerChannels> tapPostEq_ {};
    std::array<float, kNoInputMixerChannels> tapPostInsert_ {};
    std::array<float, kNoInputMixerChannels> previousTapPreEq_ {};
    std::array<float, kNoInputMixerChannels> previousTapPostEq_ {};
    std::array<float, kNoInputMixerChannels> previousTapPostInsert_ {};
    std::array<float, kNoInputMixerMatrixCells> routeSignals_ {};
    std::array<float, 2u> auxReturns_ {};
    std::array<float, 2u> previousAuxReturns_ {};
    std::array<float, 2u> auxActivity_ {};
    std::array<uint32_t, 2u> auxMuted_ {};
    std::array<float, 2u> auxMuteGain_ {{ 1.0f, 1.0f }};
    std::array<float, kNoInputMixerMatrixCells> phaseMemory_ {};
    std::array<float, kNoInputMixerMatrixCells> driftState_ {};
    std::array<float, kNoInputMixerMatrixCells> driftVelocity_ {};
    std::array<float, kNoInputMixerMatrixCells> motionTarget_ {};
    std::array<float, kNoInputMixerMatrixCells> motionCurrent_ {};
    std::array<float, kNoInputMixerMatrixCells> behaviorTarget_ {};
    std::array<float, kNoInputMixerMatrixCells> behaviorStart_ {};
    std::array<float, kNoInputMixerMatrixCells> behaviorCurrent_ {};
    std::array<float, kNoInputMixerMatrixCells> behaviorOrder_ {};
    std::array<float, kNoInputMixerChannels> laneBehaviorGate_ {};
    std::array<float, kNoInputMixerMatrixCells> behaviorCircuitGate_ {};
    std::array<float, kNoInputMixerMatrixCells> behaviorRouteLowpass_ {};
    std::array<float, kNoInputMixerChannels> laneBehaviorCircuitGate_ {};
    std::array<float, kNoInputMixerChannels> laneBehaviorLowpass_ {};
    std::array<float, kNoInputMixerMatrixCells> routeSpaceGate_ {};
    std::array<float, kNoInputMixerMatrixCells> reactCurrent_ {};
    std::array<float, kNoInputMixerChannels> reactEdge_ {};
    std::array<uint32_t, kNoInputMixerChannels> reactWasAbove_ {};
    std::array<float, kNoInputMixerChannels> lanePeak_ {};
    std::array<float, kNoInputMixerChannels> laneActivity_ {};
    std::array<float, kNoInputMixerChannels> responseEnergy_ {};
    std::array<uint8_t, kNoInputMixerChannels> eqCoefficientsDirty_ {};
    std::array<uint8_t, kNoInputMixerChannels> bodyCoefficientsDirty_ {};
    float networkActivity_ = 0.0f;
    float dcPole_ = 0.998f;
    float energyAttack_ = 0.002f;
    float energyRelease_ = 0.0001f;
    float governorAttack_ = 0.001f;
    float governorRelease_ = 0.00005f;
    float auxMuteSlew_ = 0.005f;
    float parameterSlew_ = 0.01f;
    float surfaceMatrixSlew_ = 0.002f;
    float midiMatrixSlew_ = 0.005f;
    float midiMatrixRampMs_ = kNoInputMatrixMidiRampDefaultMs;
    double movementCoefficientSampleRate_ = 0.0;
    float movementCoefficientSlew_ = -1.0f;
    float movementBehaviorSlewCoefficient_ = 1.0f;
    float movementCircuitOpenCoefficient_ = 1.0f;
    float movementCircuitCloseCoefficient_ = 1.0f;
    float behaviorDepth_ = 0.0f;
    float behaviorDepthTarget_ = 0.0f;
    float seedAmount_ = 0.45f;
    float motionPhase_ = 0.0f;
    uint32_t seedRemaining_ = 0u;
    uint32_t randomState_ = 0x5455444fu;
    uint32_t controlCounter_ = 0u;
    uint32_t parameterUpdateCounter_ = 0u;
    uint32_t behaviorEventCount_ = 0u;
    uint32_t behaviorSamplesUntilEvent_ = 0u;
    uint32_t behaviorEnvelopeSample_ = 0u;
    uint32_t behaviorEnvelopeSamples_ = 0u;
    uint32_t behaviorAttackSamples_ = 0u;
    uint32_t behaviorReleaseSamples_ = 0u;
    uint32_t behaviorRatchetCount_ = 2u;
    uint32_t panicRemaining_ = 0u;
    uint32_t panicSamplesTotal_ = 384u;
    double transportTempoBpm_ = 120.0;
    float behaviorCascadeStart_ = 0.0f;
    float behaviorCascadeDirection_ = 1.0f;
    bool transportHasTempo_ = false;
    bool parameterSmoothingInitialized_ = false;
    bool parameterSmoothingActive_ = false;
    bool audioProcessingStarted_ = false;
    bool parameterSurfaceMutationEnabled_ = false;
    bool movementCircuitProcessing_ = false;
    bool silenced_ = true;
    NoInputContainmentState containmentState_ =
        NoInputContainmentState::Quiet;
};

} // namespace s3g
