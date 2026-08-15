#pragma once

#include "s3g_processor_stack.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace s3g {

constexpr uint32_t kProcessorStackScoreSectionCount = 4u;
constexpr uint32_t kProcessorStackScoreRowsPerSection = 16u;
constexpr uint32_t kProcessorStackScorePlayerCount = 2u;
constexpr uint32_t kProcessorStackScoreStringCount = 6u;
constexpr uint32_t kProcessorStackScoreArrangementSlots = 8u;
constexpr uint32_t kProcessorStackScoreLocksPerPlayer = 2u;
constexpr int8_t kProcessorStackScoreHold = -2;
constexpr int8_t kProcessorStackScoreRest = -1;
constexpr int8_t kProcessorStackScoreMinimumFret = 0;
constexpr int8_t kProcessorStackScoreMaximumFret = 24;

// Standard guitar tuning, ordered low to high to match the vertical editor.
constexpr std::array<int, kProcessorStackScoreStringCount>
    kProcessorStackScoreOpenMidi {{ 40, 45, 50, 55, 59, 64 }};

constexpr size_t kProcessorStackScoreCellCount =
    static_cast<size_t>(kProcessorStackScoreSectionCount)
    * kProcessorStackScoreRowsPerSection
    * kProcessorStackScorePlayerCount
    * kProcessorStackScoreStringCount;

enum class ProcessorStackScoreLockControl : uint8_t {
    None = 0u,
    Neck,
    Body,
    Circuit,
    Bite,
    Tone,
    Bias,
    Stack,
    Sag,
    Focus,
    Cone,
    Cabinet,
    Mic,
    Feedback,
    Proximity,
    Harmonic,
    Tracking,
    Polarity,
    Root,
    Chaos,
    Pierce,
    SelfListen,
    TargetGlitch,
    Ratchet,
    OverloadMask,
    Count,
};

constexpr uint32_t kProcessorStackScoreLockControlCount =
    static_cast<uint32_t>(ProcessorStackScoreLockControl::Count);

struct ProcessorStackScoreLockCell {
    uint8_t control = static_cast<uint8_t>(
        ProcessorStackScoreLockControl::None);
    uint8_t reserved = 0u;
    uint16_t normalized = 0u;
};

static_assert(sizeof(ProcessorStackScoreLockCell) == 4u);

constexpr size_t kProcessorStackScoreLockCellCount =
    static_cast<size_t>(kProcessorStackScoreSectionCount)
    * kProcessorStackScoreRowsPerSection
    * kProcessorStackScorePlayerCount
    * kProcessorStackScoreLocksPerPlayer;

constexpr size_t processorStackScoreCellIndex(uint32_t section,
    uint32_t row, uint32_t player, uint32_t string) noexcept
{
    return (((static_cast<size_t>(section)
        * kProcessorStackScoreRowsPerSection + row)
        * kProcessorStackScorePlayerCount + player)
        * kProcessorStackScoreStringCount + string);
}

constexpr size_t processorStackScoreLockIndex(uint32_t section,
    uint32_t row, uint32_t player, uint32_t slot) noexcept
{
    return (((static_cast<size_t>(section)
        * kProcessorStackScoreRowsPerSection + row)
        * kProcessorStackScorePlayerCount + player)
        * kProcessorStackScoreLocksPerPlayer + slot);
}

struct ProcessorStackScoreProgram {
    std::array<int8_t, kProcessorStackScoreCellCount> cells {};
    std::array<uint8_t, kProcessorStackScoreArrangementSlots> arrangement {};
    std::array<ProcessorStackScoreLockCell,
        kProcessorStackScoreLockCellCount> locks {};
};

static_assert(sizeof(ProcessorStackScoreProgram) == 1800u);

inline ProcessorStackScoreProgram makeDefaultProcessorStackScoreProgram()
{
    ProcessorStackScoreProgram program;
    program.cells.fill(kProcessorStackScoreRest);
    for (uint32_t slot = 0u;
         slot < kProcessorStackScoreArrangementSlots; ++slot) {
        program.arrangement[slot] = static_cast<uint8_t>(
            slot % kProcessorStackScoreSectionCount);
    }
    return program;
}

inline ProcessorStackScoreProgram sanitizeProcessorStackScoreProgram(
    ProcessorStackScoreProgram program)
{
    for (auto& cell : program.cells) {
        cell = static_cast<int8_t>(std::clamp<int>(cell,
            kProcessorStackScoreHold, kProcessorStackScoreMaximumFret));
    }
    for (auto& section : program.arrangement) {
        section = static_cast<uint8_t>(std::min<uint32_t>(section,
            kProcessorStackScoreSectionCount - 1u));
    }
    for (auto& lock : program.locks) {
        if (lock.control >= kProcessorStackScoreLockControlCount) {
            lock.control = static_cast<uint8_t>(
                ProcessorStackScoreLockControl::None);
            lock.normalized = 0u;
        }
        lock.reserved = 0u;
    }
    return program;
}

inline ProcessorStackScoreLockCell processorStackScoreLock(
    const ProcessorStackScoreProgram& program, uint32_t section,
    uint32_t row, uint32_t player, uint32_t slot) noexcept
{
    return program.locks[processorStackScoreLockIndex(
        std::min(section, kProcessorStackScoreSectionCount - 1u),
        std::min(row, kProcessorStackScoreRowsPerSection - 1u),
        std::min(player, kProcessorStackScorePlayerCount - 1u),
        std::min(slot, kProcessorStackScoreLocksPerPlayer - 1u))];
}

inline void setProcessorStackScoreLock(ProcessorStackScoreProgram& program,
    uint32_t section, uint32_t row, uint32_t player, uint32_t slot,
    ProcessorStackScoreLockControl control, double normalized) noexcept
{
    auto& lock = program.locks[processorStackScoreLockIndex(
        std::min(section, kProcessorStackScoreSectionCount - 1u),
        std::min(row, kProcessorStackScoreRowsPerSection - 1u),
        std::min(player, kProcessorStackScorePlayerCount - 1u),
        std::min(slot, kProcessorStackScoreLocksPerPlayer - 1u))];
    lock.control = static_cast<uint8_t>(
        static_cast<uint32_t>(control) < kProcessorStackScoreLockControlCount
            ? control : ProcessorStackScoreLockControl::None);
    lock.reserved = 0u;
    normalized = std::isfinite(normalized) ? normalized : 0.0;
    lock.normalized = static_cast<uint16_t>(std::lround(
        std::clamp(normalized, 0.0, 1.0) * 65535.0));
}

inline double processorStackScoreLockNormalized(
    ProcessorStackScoreLockCell lock) noexcept
{
    return static_cast<double>(lock.normalized) / 65535.0;
}

inline const char* processorStackScoreLockControlName(
    ProcessorStackScoreLockControl control) noexcept
{
    switch (control) {
    case ProcessorStackScoreLockControl::None: return "CLEAR";
    case ProcessorStackScoreLockControl::Neck: return "NECK";
    case ProcessorStackScoreLockControl::Body: return "BODY";
    case ProcessorStackScoreLockControl::Circuit: return "CIRCUIT";
    case ProcessorStackScoreLockControl::Bite: return "BITE";
    case ProcessorStackScoreLockControl::Tone: return "TONE";
    case ProcessorStackScoreLockControl::Bias: return "BIAS";
    case ProcessorStackScoreLockControl::Stack: return "STACK";
    case ProcessorStackScoreLockControl::Sag: return "SAG";
    case ProcessorStackScoreLockControl::Focus: return "FOCUS";
    case ProcessorStackScoreLockControl::Cone: return "CONE";
    case ProcessorStackScoreLockControl::Cabinet: return "CAB";
    case ProcessorStackScoreLockControl::Mic: return "MIC";
    case ProcessorStackScoreLockControl::Feedback: return "FEEDBACK";
    case ProcessorStackScoreLockControl::Proximity: return "PROXIMITY";
    case ProcessorStackScoreLockControl::Harmonic: return "HARMONIC";
    case ProcessorStackScoreLockControl::Tracking: return "TRACK";
    case ProcessorStackScoreLockControl::Polarity: return "POLARITY";
    case ProcessorStackScoreLockControl::Root: return "ROOT";
    case ProcessorStackScoreLockControl::Chaos: return "CHAOS";
    case ProcessorStackScoreLockControl::Pierce: return "PIERCE";
    case ProcessorStackScoreLockControl::SelfListen: return "SELF LISTEN";
    case ProcessorStackScoreLockControl::TargetGlitch: return "TARGET GLITCH";
    case ProcessorStackScoreLockControl::Ratchet: return "RATCHET";
    case ProcessorStackScoreLockControl::OverloadMask: return "OVERLOAD MASK";
    case ProcessorStackScoreLockControl::Count: break;
    }
    return "CLEAR";
}

inline const char* processorStackScoreLockControlShortName(
    ProcessorStackScoreLockControl control) noexcept
{
    switch (control) {
    case ProcessorStackScoreLockControl::None: return "---";
    case ProcessorStackScoreLockControl::Neck: return "NEK";
    case ProcessorStackScoreLockControl::Body: return "BDY";
    case ProcessorStackScoreLockControl::Circuit: return "CIR";
    case ProcessorStackScoreLockControl::Bite: return "BIT";
    case ProcessorStackScoreLockControl::Tone: return "TON";
    case ProcessorStackScoreLockControl::Bias: return "BIA";
    case ProcessorStackScoreLockControl::Stack: return "STK";
    case ProcessorStackScoreLockControl::Sag: return "SAG";
    case ProcessorStackScoreLockControl::Focus: return "FOC";
    case ProcessorStackScoreLockControl::Cone: return "CON";
    case ProcessorStackScoreLockControl::Cabinet: return "CAB";
    case ProcessorStackScoreLockControl::Mic: return "MIC";
    case ProcessorStackScoreLockControl::Feedback: return "FBK";
    case ProcessorStackScoreLockControl::Proximity: return "PRX";
    case ProcessorStackScoreLockControl::Harmonic: return "HAR";
    case ProcessorStackScoreLockControl::Tracking: return "TRK";
    case ProcessorStackScoreLockControl::Polarity: return "POL";
    case ProcessorStackScoreLockControl::Root: return "ROT";
    case ProcessorStackScoreLockControl::Chaos: return "CHA";
    case ProcessorStackScoreLockControl::Pierce: return "PIR";
    case ProcessorStackScoreLockControl::SelfListen: return "LST";
    case ProcessorStackScoreLockControl::TargetGlitch: return "GLI";
    case ProcessorStackScoreLockControl::Ratchet: return "RAT";
    case ProcessorStackScoreLockControl::OverloadMask: return "MSK";
    case ProcessorStackScoreLockControl::Count: break;
    }
    return "---";
}

inline void applyProcessorStackScoreLock(ProcessorStackParams& params,
    uint32_t player, ProcessorStackScoreLockCell lock) noexcept
{
    const auto control = static_cast<ProcessorStackScoreLockControl>(
        std::min<uint32_t>(lock.control,
            kProcessorStackScoreLockControlCount - 1u));
    if (control == ProcessorStackScoreLockControl::None) return;
    const float value = static_cast<float>(
        processorStackScoreLockNormalized(lock));
    const uint32_t material = static_cast<uint32_t>(std::lround(value * 3.0f));
    const uint32_t circuit = static_cast<uint32_t>(std::lround(value * 7.0f));
    const bool playerB = player != 0u;
    switch (control) {
    case ProcessorStackScoreLockControl::Neck:
        if (playerB) params.neckB = static_cast<ProcessorStackNeckMaterial>(material);
        else params.neckA = static_cast<ProcessorStackNeckMaterial>(material);
        break;
    case ProcessorStackScoreLockControl::Body:
        if (playerB) params.bodyB = static_cast<ProcessorStackBodyMaterial>(material);
        else params.bodyA = static_cast<ProcessorStackBodyMaterial>(material);
        break;
    case ProcessorStackScoreLockControl::Circuit:
        if (playerB) params.circuitB = static_cast<ProcessorStackCircuit>(circuit);
        else params.circuit = static_cast<ProcessorStackCircuit>(circuit);
        break;
    case ProcessorStackScoreLockControl::Bite:
        if (playerB) params.biteB = value; else params.bite = value; break;
    case ProcessorStackScoreLockControl::Tone:
        if (playerB) params.pedalToneB = value; else params.pedalTone = value; break;
    case ProcessorStackScoreLockControl::Bias:
        if (playerB) params.biasB = value; else params.bias = value; break;
    case ProcessorStackScoreLockControl::Stack:
        if (playerB) params.stackB = value; else params.stack = value; break;
    case ProcessorStackScoreLockControl::Sag:
        if (playerB) params.sagB = value; else params.sag = value; break;
    case ProcessorStackScoreLockControl::Focus:
        if (playerB) params.focusB = value; else params.focus = value; break;
    case ProcessorStackScoreLockControl::Cone:
        if (playerB) params.coneB = value; else params.cone = value; break;
    case ProcessorStackScoreLockControl::Cabinet:
        if (playerB) params.cabinetB = value; else params.cabinet = value; break;
    case ProcessorStackScoreLockControl::Mic:
        if (playerB) params.micB = value; else params.mic = value; break;
    case ProcessorStackScoreLockControl::Feedback:
        if (playerB) params.feedbackB = value; else params.feedback = value; break;
    case ProcessorStackScoreLockControl::Proximity:
        if (playerB) params.proximityB = value; else params.proximity = value; break;
    case ProcessorStackScoreLockControl::Harmonic:
        if (playerB) params.harmonicB = value; else params.harmonic = value; break;
    case ProcessorStackScoreLockControl::Tracking:
        if (playerB) params.trackingB = value; else params.tracking = value; break;
    case ProcessorStackScoreLockControl::Polarity:
        if (playerB) params.polarityB = value; else params.polarity = value; break;
    case ProcessorStackScoreLockControl::Root:
        if (playerB) params.rootB = value; else params.root = value; break;
    case ProcessorStackScoreLockControl::Chaos:
        if (playerB) params.chaosB = value; else params.chaos = value; break;
    case ProcessorStackScoreLockControl::Pierce:
        if (playerB) params.pierceB = value; else params.pierce = value; break;
    case ProcessorStackScoreLockControl::SelfListen:
        if (playerB) params.selfListenB = value; else params.selfListen = value; break;
    case ProcessorStackScoreLockControl::TargetGlitch:
        if (playerB) params.targetGlitchB = value; else params.targetGlitch = value; break;
    case ProcessorStackScoreLockControl::Ratchet:
        if (playerB) params.glitchRatchetB = value; else params.glitchRatchet = value; break;
    case ProcessorStackScoreLockControl::OverloadMask:
        if (playerB) params.overloadMaskB = value; else params.overloadMask = value; break;
    case ProcessorStackScoreLockControl::None:
    case ProcessorStackScoreLockControl::Count:
        break;
    }
    if (!playerB) return;
    if (control >= ProcessorStackScoreLockControl::Circuit
        && control <= ProcessorStackScoreLockControl::Bias) {
        params.linkPedal = false;
    } else if (control >= ProcessorStackScoreLockControl::Stack
        && control <= ProcessorStackScoreLockControl::Mic) {
        params.linkAmplifier = false;
    } else if (control >= ProcessorStackScoreLockControl::Feedback) {
        params.linkFeedback = false;
    }
}

inline ProcessorStackParams processorStackScoreParamsForRow(
    const ProcessorStackScoreProgram& program,
    ProcessorStackParams params, uint32_t section, uint32_t row) noexcept
{
    for (uint32_t player = 0u;
         player < kProcessorStackScorePlayerCount; ++player) {
        for (uint32_t slot = 0u;
             slot < kProcessorStackScoreLocksPerPlayer; ++slot) {
            applyProcessorStackScoreLock(params, player,
                processorStackScoreLock(program,
                    section, row, player, slot));
        }
    }
    return sanitizeProcessorStackParams(params);
}

inline int8_t processorStackScoreCell(
    const ProcessorStackScoreProgram& program, uint32_t section,
    uint32_t row, uint32_t player, uint32_t string) noexcept
{
    return program.cells[processorStackScoreCellIndex(
        std::min(section, kProcessorStackScoreSectionCount - 1u),
        std::min(row, kProcessorStackScoreRowsPerSection - 1u),
        std::min(player, kProcessorStackScorePlayerCount - 1u),
        std::min(string, kProcessorStackScoreStringCount - 1u))];
}

inline void setProcessorStackScoreCell(ProcessorStackScoreProgram& program,
    uint32_t section, uint32_t row, uint32_t player, uint32_t string,
    int fret) noexcept
{
    program.cells[processorStackScoreCellIndex(
        std::min(section, kProcessorStackScoreSectionCount - 1u),
        std::min(row, kProcessorStackScoreRowsPerSection - 1u),
        std::min(player, kProcessorStackScorePlayerCount - 1u),
        std::min(string, kProcessorStackScoreStringCount - 1u))] =
        static_cast<int8_t>(std::clamp(fret,
            static_cast<int>(kProcessorStackScoreHold),
            static_cast<int>(kProcessorStackScoreMaximumFret)));
}

inline uint32_t processorStackScoreStringCommands(
    const ProcessorStackScoreProgram& program, uint32_t section,
    uint32_t row, uint32_t player, int* commands,
    uint32_t capacity) noexcept
{
    if (!commands || capacity == 0u) return 0u;
    const uint32_t count = std::min(
        capacity, kProcessorStackScoreStringCount);
    for (uint32_t string = 0u; string < count; ++string) {
        const int cell = processorStackScoreCell(
            program, section, row, player, string);
        commands[string] = cell == kProcessorStackScoreHold
            ? static_cast<int>(kProcessorStackScoreHold)
            : cell >= kProcessorStackScoreMinimumFret
                ? std::clamp(kProcessorStackScoreOpenMidi[string] + cell,
                    0, 127)
                : static_cast<int>(kProcessorStackScoreRest);
    }
    return count;
}

inline uint32_t processorStackScoreRandomNext(uint32_t& state) noexcept
{
    // A zero seed is still deterministic, but must not become the xorshift
    // fixed point. This generator is deliberately local to Score generation;
    // it never touches the synthesis randomizer or its parameter choices.
    if (state == 0u) state = 0x6d2b79f5u;
    state ^= state << 13u;
    state ^= state >> 17u;
    state ^= state << 5u;
    return state;
}

inline uint32_t processorStackScoreRandomRange(
    uint32_t& state, uint32_t limit) noexcept
{
    return limit > 0u ? processorStackScoreRandomNext(state) % limit : 0u;
}

inline void setProcessorStackScorePowerChord(
    ProcessorStackScoreProgram& program, uint32_t section, uint32_t row,
    uint32_t player, uint32_t rootString, int rootFret,
    bool includeOctave = true) noexcept
{
    // Standard-tuned E- and A-string power-chord shapes: the fifth and octave
    // are both two frets above the root on the next two strings. Keeping the
    // shape intact is more guitar-like than independently choosing pitches.
    rootString = std::min(rootString, 1u);
    rootFret = std::clamp(rootFret,
        static_cast<int>(kProcessorStackScoreMinimumFret),
        static_cast<int>(kProcessorStackScoreMaximumFret) - 2);
    setProcessorStackScoreCell(
        program, section, row, player, rootString, rootFret);
    setProcessorStackScoreCell(
        program, section, row, player, rootString + 1u, rootFret + 2);
    if (includeOctave) {
        setProcessorStackScoreCell(
            program, section, row, player, rootString + 2u, rootFret + 2);
    }
}

inline void replaceProcessorStackScoreRowWithHolds(
    ProcessorStackScoreProgram& program, uint32_t section,
    uint32_t sourceRow, uint32_t holdRow, uint32_t player) noexcept
{
    section = std::min(section, kProcessorStackScoreSectionCount - 1u);
    sourceRow = std::min(sourceRow,
        kProcessorStackScoreRowsPerSection - 1u);
    holdRow = std::min(holdRow,
        kProcessorStackScoreRowsPerSection - 1u);
    player = std::min(player, kProcessorStackScorePlayerCount - 1u);
    std::array<bool, kProcessorStackScoreStringCount> sounding {};
    for (uint32_t string = 0u;
         string < kProcessorStackScoreStringCount; ++string) {
        sounding[string] = processorStackScoreCell(
            program, section, sourceRow, player, string)
            >= kProcessorStackScoreMinimumFret;
        setProcessorStackScoreCell(program, section, holdRow,
            player, string, sounding[string]
                ? kProcessorStackScoreHold : kProcessorStackScoreRest);
    }
}

inline uint32_t addProcessorStackScoreHoldChains(
    ProcessorStackScoreProgram& program, uint32_t section,
    uint32_t& random, uint32_t chancePercent,
    uint32_t maximumLength = 2u) noexcept
{
    section = std::min(section, kProcessorStackScoreSectionCount - 1u);
    chancePercent = std::min(chancePercent, 100u);
    maximumLength = std::clamp(maximumLength, 1u,
        kProcessorStackScoreRowsPerSection - 1u);
    uint32_t holdCells = 0u;
    for (uint32_t player = 0u;
         player < kProcessorStackScorePlayerCount; ++player) {
        const uint32_t playerHoldStart = holdCells;
        for (uint32_t row = 0u;
             row + 1u < kProcessorStackScoreRowsPerSection; ++row) {
            std::array<bool, kProcessorStackScoreStringCount> active {};
            bool hasAttack = false;
            for (uint32_t string = 0u;
                 string < kProcessorStackScoreStringCount; ++string) {
                active[string] = processorStackScoreCell(
                    program, section, row, player, string)
                    >= kProcessorStackScoreMinimumFret;
                hasAttack = hasAttack || active[string];
            }
            if (!hasAttack || processorStackScoreRandomRange(random, 100u)
                    >= chancePercent) continue;
            const uint32_t length = 1u
                + processorStackScoreRandomRange(random, maximumLength);
            for (uint32_t offset = 1u;
                 offset <= length
                    && row + offset < kProcessorStackScoreRowsPerSection;
                 ++offset) {
                bool continued = false;
                for (uint32_t string = 0u;
                     string < kProcessorStackScoreStringCount; ++string) {
                    if (!active[string]) continue;
                    const int target = processorStackScoreCell(program,
                        section, row + offset, player, string);
                    if (target != kProcessorStackScoreRest
                        && target != kProcessorStackScoreHold) {
                        active[string] = false;
                        continue;
                    }
                    setProcessorStackScoreCell(program, section,
                        row + offset, player, string,
                        kProcessorStackScoreHold);
                    ++holdCells;
                    continued = true;
                }
                if (!continued) break;
            }
        }
        if (holdCells > playerHoldStart) continue;
        // Every generated player gets at least one visible duration event
        // whenever an attack has an open cell immediately beneath it.
        for (uint32_t row = 0u;
             row + 1u < kProcessorStackScoreRowsPerSection; ++row) {
            bool continued = false;
            for (uint32_t string = 0u;
                 string < kProcessorStackScoreStringCount; ++string) {
                if (processorStackScoreCell(program,
                        section, row, player, string)
                        < kProcessorStackScoreMinimumFret
                    || processorStackScoreCell(program,
                        section, row + 1u, player, string)
                        != kProcessorStackScoreRest) continue;
                setProcessorStackScoreCell(program, section, row + 1u,
                    player, string, kProcessorStackScoreHold);
                ++holdCells;
                continued = true;
            }
            if (continued) break;
        }
    }
    return holdCells;
}

// Builds a complete four-section guitar form without changing synthesis
// parameters. Section A is a low-string power riff, B uses gallop/pedal-note
// mechanics, C walks a compact minor-pentatonic solo box with a second-guitar
// harmony, and D trades breakdown chords between the two players.
inline ProcessorStackScoreProgram generateProcessorStackScore(uint32_t seed)
{
    ProcessorStackScoreProgram program =
        makeDefaultProcessorStackScoreProgram();
    uint32_t random = seed;

    static constexpr std::array<std::array<int8_t, 16u>, 4u>
        powerRiffMotifs {{
            {{ 0, -1, 0, 3, 0, 0, 6, -1, 0, -1, 0, 5, 0, 1, 3, -1 }},
            {{ 0, 0, -1, 5, 0, -1, 3, 6, 0, 0, -1, 1, 0, 5, 3, -1 }},
            {{ 0, -1, 1, 0, 6, -1, 5, 3, 0, -1, 0, 1, 3, 5, 6, -1 }},
            {{ 0, 0, 3, -1, 0, 6, -1, 5, 0, 1, 0, -1, 3, 6, 5, -1 }},
        }};
    const auto& riff = powerRiffMotifs[
        processorStackScoreRandomRange(random,
            static_cast<uint32_t>(powerRiffMotifs.size()))];
    const int riffBase = static_cast<int>(
        processorStackScoreRandomRange(random, 5u));
    for (uint32_t row = 0u; row < kProcessorStackScoreRowsPerSection;
         ++row) {
        if (riff[row] < 0) continue;
        const int fret = riffBase + riff[row];
        const bool accent = row == 0u || (row % 4u) == 3u
            || riff[row] >= 5;
        if (accent) {
            setProcessorStackScorePowerChord(
                program, 0u, row, 0u, 0u, fret, true);
            // An A-string-root shape places the second guitar a fifth above A
            // while keeping both hands in the same compact fret region.
            setProcessorStackScorePowerChord(
                program, 0u, row, 1u, 1u, fret + 2, row % 8u == 0u);
        } else {
            setProcessorStackScoreCell(program, 0u, row, 0u, 0u, fret);
            if ((row & 1u) == 0u) {
                setProcessorStackScoreCell(
                    program, 0u, row, 1u, 1u, fret + 2);
            }
        }
    }

    static constexpr std::array<int8_t, 6u> gallopIntervals {{
        0, 1, 3, 5, 6, 7,
    }};
    const int gallopBase = static_cast<int>(
        processorStackScoreRandomRange(random, 5u));
    int previousGallop = 0;
    for (uint32_t group = 0u; group < 4u; ++group) {
        int interval = gallopIntervals[processorStackScoreRandomRange(
            random, static_cast<uint32_t>(gallopIntervals.size()))];
        if (std::abs(interval - previousGallop) > 5) interval = 3;
        previousGallop = interval;
        const int fret = gallopBase + interval;
        const uint32_t row = group * 4u;
        setProcessorStackScoreCell(program, 1u, row, 0u, 0u, fret);
        setProcessorStackScorePowerChord(
            program, 1u, row + 1u, 1u, 1u, fret + 2, false);
        setProcessorStackScoreCell(program, 1u, row + 2u, 0u, 0u, fret);
        setProcessorStackScorePowerChord(
            program, 1u, row + 3u, 0u, 0u, fret, true);
        setProcessorStackScorePowerChord(
            program, 1u, row + 3u, 1u, 1u, fret + 2, true);
    }

    struct SoloPosition {
        uint8_t string;
        int8_t fretOffset;
    };
    // Minor-pentatonic box ordered from low to high pitch. Adjacent choices
    // cross at most one string and stay inside a four-fret hand position.
    static constexpr std::array<SoloPosition, 12u> soloBox {{
        { 0u, 0 }, { 0u, 3 }, { 1u, 0 }, { 1u, 2 },
        { 2u, 0 }, { 2u, 2 }, { 3u, 0 }, { 3u, 2 },
        { 4u, 0 }, { 4u, 3 }, { 5u, 0 }, { 5u, 3 },
    }};
    static constexpr std::array<int8_t, 8u> soloMoves {{
        1, 1, 2, -1, 2, -2, 3, -1,
    }};
    const int soloBase = 5 + static_cast<int>(
        processorStackScoreRandomRange(random, 13u));
    int soloIndex = 3 + static_cast<int>(
        processorStackScoreRandomRange(random, 5u));
    const uint32_t breathRow = 6u
        + processorStackScoreRandomRange(random, 3u);
    for (uint32_t row = 0u; row < kProcessorStackScoreRowsPerSection;
         ++row) {
        if (row == breathRow) continue;
        if (row == 15u) soloIndex = 8;
        const auto lead = soloBox[static_cast<size_t>(soloIndex)];
        setProcessorStackScoreCell(program, 2u, row, 0u,
            lead.string, soloBase + lead.fretOffset);
        const int harmonyIndex = std::min(soloIndex + 2,
            static_cast<int>(soloBox.size()) - 1);
        const auto harmony = soloBox[static_cast<size_t>(harmonyIndex)];
        setProcessorStackScoreCell(program, 2u, row, 1u,
            harmony.string, soloBase + harmony.fretOffset);
        if (row == 15u) {
            const auto leadDouble = soloBox[10u];
            const auto harmonyDouble = soloBox[6u];
            setProcessorStackScoreCell(program, 2u, row, 0u,
                leadDouble.string, soloBase + leadDouble.fretOffset);
            setProcessorStackScoreCell(program, 2u, row, 1u,
                harmonyDouble.string,
                soloBase + harmonyDouble.fretOffset);
            continue;
        }
        int next = soloIndex + soloMoves[processorStackScoreRandomRange(
            random, static_cast<uint32_t>(soloMoves.size()))];
        if (next < 0 || next >= static_cast<int>(soloBox.size())) {
            next = soloIndex + (next < 0 ? 2 : -2);
        }
        soloIndex = std::clamp(
            next, 0, static_cast<int>(soloBox.size()) - 1);
    }
    // A composed lead uses monophonic ties rather than allowing the next
    // random pitch to overlap the sustained string.
    for (uint32_t player = 0u;
         player < kProcessorStackScorePlayerCount; ++player) {
        replaceProcessorStackScoreRowWithHolds(
            program, 2u, 3u, 4u, player);
        replaceProcessorStackScoreRowWithHolds(
            program, 2u, 3u, 5u, player);
    }

    const int breakdownBase = static_cast<int>(
        processorStackScoreRandomRange(random, 6u));
    static constexpr std::array<uint8_t, 7u> breakdownRows {{
        0u, 3u, 5u, 8u, 11u, 13u, 15u,
    }};
    for (uint32_t index = 0u; index < breakdownRows.size(); ++index) {
        const uint32_t row = breakdownRows[index];
        const int fret = breakdownBase
            + ((index == 2u || index == 5u) ? 3 : 0);
        const bool together = row == 0u || row == 8u || row == 15u;
        if (together || (index & 1u) != 0u) {
            setProcessorStackScorePowerChord(
                program, 3u, row, 0u, 0u, fret, true);
        }
        if (together || (index & 1u) == 0u) {
            setProcessorStackScorePowerChord(
                program, 3u, row, 1u, 1u, fret + 2, true);
        }
    }
    setProcessorStackScoreCell(
        program, 3u, 6u, 0u, 0u, breakdownBase);
    setProcessorStackScoreCell(
        program, 3u, 7u, 1u, 1u, breakdownBase + 2);

    (void)addProcessorStackScoreHoldChains(
        program, 0u, random, 24u, 2u);
    (void)addProcessorStackScoreHoldChains(
        program, 1u, random, 18u, 2u);
    (void)addProcessorStackScoreHoldChains(
        program, 3u, random, 34u, 3u);

    static constexpr std::array<std::array<uint8_t, 8u>, 4u> forms {{
        {{ 0u, 0u, 1u, 0u, 2u, 1u, 3u, 2u }},
        {{ 0u, 1u, 0u, 2u, 0u, 1u, 3u, 2u }},
        {{ 1u, 0u, 1u, 2u, 0u, 3u, 1u, 2u }},
        {{ 0u, 0u, 1u, 3u, 0u, 2u, 1u, 2u }},
    }};
    program.arrangement = forms[processorStackScoreRandomRange(
        random, static_cast<uint32_t>(forms.size()))];
    return program;
}

inline void clearProcessorStackScoreSectionNotes(
    ProcessorStackScoreProgram& program, uint32_t section) noexcept
{
    section = std::min(section, kProcessorStackScoreSectionCount - 1u);
    for (uint32_t row = 0u; row < kProcessorStackScoreRowsPerSection; ++row) {
        for (uint32_t player = 0u;
             player < kProcessorStackScorePlayerCount; ++player) {
            for (uint32_t string = 0u;
                 string < kProcessorStackScoreStringCount; ++string) {
                setProcessorStackScoreCell(program,
                    section, row, player, string, kProcessorStackScoreRest);
            }
        }
    }
}

inline ProcessorStackScoreProgram randomizeProcessorStackScoreLead(
    ProcessorStackScoreProgram program, uint32_t section, uint32_t seed)
{
    section = std::min(section, kProcessorStackScoreSectionCount - 1u);
    clearProcessorStackScoreSectionNotes(program, section);
    uint32_t random = seed;
    struct Position {
        uint8_t string;
        int8_t fretOffset;
    };
    static constexpr std::array<Position, 12u> box {{
        { 0u, 0 }, { 0u, 3 }, { 1u, 0 }, { 1u, 2 },
        { 2u, 0 }, { 2u, 2 }, { 3u, 0 }, { 3u, 2 },
        { 4u, 0 }, { 4u, 3 }, { 5u, 0 }, { 5u, 3 },
    }};
    static constexpr std::array<int8_t, 10u> moves {{
        1, 1, 2, -1, 2, -2, 3, -1, 1, -2,
    }};
    const int baseFret = 5 + static_cast<int>(
        processorStackScoreRandomRange(random, 13u));
    int position = 2 + static_cast<int>(
        processorStackScoreRandomRange(random, 7u));
    const uint32_t breathA = 4u
        + processorStackScoreRandomRange(random, 5u);
    const uint32_t breathB = 10u
        + processorStackScoreRandomRange(random, 4u);
    for (uint32_t row = 0u; row < kProcessorStackScoreRowsPerSection; ++row) {
        if (row == breathA || row == breathB) continue;
        const auto lead = box[static_cast<size_t>(position)];
        setProcessorStackScoreCell(program, section, row, 0u,
            lead.string, baseFret + lead.fretOffset);
        const int harmonyDirection = (row / 4u) % 2u == 0u ? 2 : -2;
        const int harmonyPosition = std::clamp(position + harmonyDirection,
            0, static_cast<int>(box.size()) - 1);
        const auto harmony = box[static_cast<size_t>(harmonyPosition)];
        setProcessorStackScoreCell(program, section, row, 1u,
            harmony.string, baseFret + harmony.fretOffset);
        int next = position + moves[processorStackScoreRandomRange(
            random, static_cast<uint32_t>(moves.size()))];
        if (next < 0 || next >= static_cast<int>(box.size())) {
            next = position + (next < 0 ? 2 : -2);
        }
        position = std::clamp(next, 0, static_cast<int>(box.size()) - 1);
    }
    const uint32_t holdSource = 1u
        + processorStackScoreRandomRange(random, 2u);
    const uint32_t holdLength = 1u
        + processorStackScoreRandomRange(random, 2u);
    for (uint32_t player = 0u;
         player < kProcessorStackScorePlayerCount; ++player) {
        for (uint32_t offset = 1u; offset <= holdLength; ++offset) {
            replaceProcessorStackScoreRowWithHolds(program, section,
                holdSource, holdSource + offset, player);
        }
    }
    return program;
}

inline ProcessorStackScoreProgram randomizeProcessorStackScoreRiff(
    ProcessorStackScoreProgram program, uint32_t section, uint32_t seed)
{
    section = std::min(section, kProcessorStackScoreSectionCount - 1u);
    clearProcessorStackScoreSectionNotes(program, section);
    uint32_t random = seed;
    static constexpr std::array<std::array<int8_t, 16u>, 4u> motifs {{
        {{ 0, -1, 0, 3, 0, 5, -1, 6, 0, 0, 3, -1, 0, 5, 6, 3 }},
        {{ 0, 0, 3, -1, 0, 6, 5, -1, 0, 1, -1, 3, 0, 6, 5, 0 }},
        {{ 0, -1, 1, 3, 0, 0, 6, 5, 0, -1, 3, 1, 0, 5, 6, 0 }},
        {{ 0, 3, -1, 0, 5, -1, 6, 3, 0, 0, 1, -1, 5, 3, 6, 0 }},
    }};
    const auto& motif = motifs[processorStackScoreRandomRange(
        random, static_cast<uint32_t>(motifs.size()))];
    const int baseFret = static_cast<int>(
        processorStackScoreRandomRange(random, 6u));
    for (uint32_t row = 0u; row < kProcessorStackScoreRowsPerSection; ++row) {
        if (motif[row] < 0) continue;
        const int fret = baseFret + motif[row];
        const bool chord = row == 0u || (row % 4u) == 3u
            || processorStackScoreRandomRange(random, 100u) < 68u;
        if (chord) {
            setProcessorStackScorePowerChord(
                program, section, row, 0u, 0u, fret, true);
        } else {
            setProcessorStackScoreCell(
                program, section, row, 0u, 0u, fret);
        }
        const bool answer = (row % 4u) == 1u || chord
            || processorStackScoreRandomRange(random, 100u) < 30u;
        if (!answer) continue;
        if (chord || (row % 4u) == 1u) {
            setProcessorStackScorePowerChord(program, section, row, 1u,
                1u, fret + 2, chord && (row % 8u) == 0u);
        } else {
            setProcessorStackScoreCell(
                program, section, row, 1u, 1u, fret + 2);
        }
    }
    (void)addProcessorStackScoreHoldChains(
        program, section, random, 28u, 3u);
    return program;
}

inline ProcessorStackScoreProgram randomizeProcessorStackScoreLocks(
    ProcessorStackScoreProgram program, uint32_t section, uint32_t seed)
{
    section = std::min(section, kProcessorStackScoreSectionCount - 1u);
    for (uint32_t row = 0u; row < kProcessorStackScoreRowsPerSection; ++row) {
        for (uint32_t player = 0u;
             player < kProcessorStackScorePlayerCount; ++player) {
            for (uint32_t slot = 0u;
                 slot < kProcessorStackScoreLocksPerPlayer; ++slot) {
                setProcessorStackScoreLock(program, section, row,
                    player, slot, ProcessorStackScoreLockControl::None, 0.0);
            }
        }
    }
    static constexpr std::array<ProcessorStackScoreLockControl, 16u>
        controls {{
            ProcessorStackScoreLockControl::Circuit,
            ProcessorStackScoreLockControl::Bite,
            ProcessorStackScoreLockControl::Tone,
            ProcessorStackScoreLockControl::Stack,
            ProcessorStackScoreLockControl::Sag,
            ProcessorStackScoreLockControl::Focus,
            ProcessorStackScoreLockControl::Cone,
            ProcessorStackScoreLockControl::Cabinet,
            ProcessorStackScoreLockControl::Feedback,
            ProcessorStackScoreLockControl::Proximity,
            ProcessorStackScoreLockControl::Harmonic,
            ProcessorStackScoreLockControl::Tracking,
            ProcessorStackScoreLockControl::Pierce,
            ProcessorStackScoreLockControl::SelfListen,
            ProcessorStackScoreLockControl::TargetGlitch,
            ProcessorStackScoreLockControl::Ratchet,
        }};
    uint32_t random = seed;
    for (uint32_t player = 0u;
         player < kProcessorStackScorePlayerCount; ++player) {
        std::array<std::array<bool, kProcessorStackScoreRowsPerSection>,
            kProcessorStackScoreLocksPerPlayer> usedRows {};
        for (uint32_t event = 0u; event < 4u; ++event) {
            const uint32_t slot = event % kProcessorStackScoreLocksPerPlayer;
            const uint32_t desiredLength = 2u
                + processorStackScoreRandomRange(random, 3u);
            const uint32_t candidate = processorStackScoreRandomRange(
                random, kProcessorStackScoreRowsPerSection - 1u);
            uint32_t row = 0u;
            uint32_t runLength = 0u;
            bool found = false;
            // Prefer beginning a run on an attack or hold row, then fall
            // back to any free two-row span in the chosen L1/L2 lane.
            for (uint32_t pass = 0u; pass < 2u && !found; ++pass) {
                for (uint32_t attempt = 0u;
                     attempt < kProcessorStackScoreRowsPerSection - 1u;
                     ++attempt) {
                    const uint32_t next = (candidate + attempt)
                        % (kProcessorStackScoreRowsPerSection - 1u);
                    const uint32_t length = std::min(desiredLength,
                        kProcessorStackScoreRowsPerSection - next);
                    if (length < 2u) continue;
                    bool available = true;
                    for (uint32_t offset = 0u; offset < length; ++offset) {
                        available = available
                            && !usedRows[slot][next + offset];
                    }
                    if (!available) continue;
                    bool hasNote = false;
                    for (uint32_t string = 0u;
                         string < kProcessorStackScoreStringCount; ++string) {
                        hasNote = hasNote || processorStackScoreCell(program,
                            section, next, player, string)
                            != kProcessorStackScoreRest;
                    }
                    if (pass == 0u && !hasNote) continue;
                    row = next;
                    runLength = length;
                    found = true;
                    break;
                }
            }
            if (!found) continue;
            const auto control = controls[processorStackScoreRandomRange(
                random, static_cast<uint32_t>(controls.size()))];
            double value = (event & 1u) == 0u
                ? 0.72 + static_cast<double>(
                    processorStackScoreRandomRange(random, 24u)) / 100.0
                : 0.10 + static_cast<double>(
                    processorStackScoreRandomRange(random, 24u)) / 100.0;
            if (control == ProcessorStackScoreLockControl::Circuit) {
                value = static_cast<double>(1u
                    + processorStackScoreRandomRange(random, 7u)) / 7.0;
            }
            for (uint32_t offset = 0u; offset < runLength; ++offset) {
                usedRows[slot][row + offset] = true;
                setProcessorStackScoreLock(program, section, row + offset,
                    player, slot, control, value);
            }
        }
    }
    return program;
}

struct ProcessorStackScorePosition {
    int64_t globalRow = 0;
    uint32_t arrangementSlot = 0u;
    uint32_t section = 0u;
    uint32_t row = 0u;
    double fraction = 0.0;
};

inline int64_t processorStackScoreFloorDivide(
    int64_t value, int64_t divisor) noexcept
{
    const int64_t quotient = value / divisor;
    const int64_t remainder = value % divisor;
    return quotient - (remainder < 0 ? 1 : 0);
}

inline uint32_t processorStackScorePositiveModulo(
    int64_t value, uint32_t modulus) noexcept
{
    if (modulus == 0u) return 0u;
    int64_t remainder = value % static_cast<int64_t>(modulus);
    if (remainder < 0) remainder += static_cast<int64_t>(modulus);
    return static_cast<uint32_t>(remainder);
}

inline ProcessorStackScorePosition processorStackScorePosition(
    const ProcessorStackScoreProgram& program, double beat,
    double rowBeats, uint32_t arrangementLength) noexcept
{
    ProcessorStackScorePosition result;
    rowBeats = std::isfinite(rowBeats) && rowBeats > 0.0
        ? rowBeats : 0.25;
    const double timeline = (std::isfinite(beat) ? beat : 0.0) / rowBeats;
    const double floored = std::floor(timeline);
    result.globalRow = static_cast<int64_t>(std::clamp(floored,
        static_cast<double>(std::numeric_limits<int64_t>::min() + 1),
        static_cast<double>(std::numeric_limits<int64_t>::max() - 1)));
    result.fraction = std::clamp(timeline - floored, 0.0, 1.0);
    arrangementLength = std::clamp(arrangementLength, 1u,
        kProcessorStackScoreArrangementSlots);
    const int64_t sectionIndex = processorStackScoreFloorDivide(
        result.globalRow,
        static_cast<int64_t>(kProcessorStackScoreRowsPerSection));
    result.arrangementSlot = processorStackScorePositiveModulo(
        sectionIndex, arrangementLength);
    result.section = std::min<uint32_t>(
        program.arrangement[result.arrangementSlot],
        kProcessorStackScoreSectionCount - 1u);
    result.row = processorStackScorePositiveModulo(result.globalRow,
        kProcessorStackScoreRowsPerSection);
    return result;
}

inline uint32_t processorStackScoreNotes(
    const ProcessorStackScoreProgram& program, uint32_t section,
    uint32_t row, uint32_t player, int* notes,
    uint32_t capacity) noexcept
{
    if (!notes || capacity == 0u) return 0u;
    uint32_t count = 0u;
    for (uint32_t string = 0u;
         string < kProcessorStackScoreStringCount && count < capacity;
         ++string) {
        const int fret = processorStackScoreCell(
            program, section, row, player, string);
        if (fret < kProcessorStackScoreMinimumFret) continue;
        notes[count++] = std::clamp(
            kProcessorStackScoreOpenMidi[string] + fret, 0, 127);
    }
    return count;
}

} // namespace s3g
