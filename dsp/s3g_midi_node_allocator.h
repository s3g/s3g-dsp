#pragma once

#include <array>
#include <cstdint>

namespace s3g {

inline uint32_t nextMidiNodeRandom(uint32_t& state)
{
    if (state == 0u) state = 0x9e3779b9u;
    state ^= state << 13u;
    state ^= state >> 17u;
    state ^= state << 5u;
    return state;
}

// freeMask uses its low eight bits for nodes 1..8. A zero mask means all
// nodes are occupied, in which case the selected node is the voice to steal.
inline uint32_t allocateSequentialMidiNode(
    uint32_t freeMask, uint32_t& cursor)
{
    cursor %= 8u;
    if ((freeMask & 0xffu) != 0u) {
        for (uint32_t offset = 0u; offset < 8u; ++offset) {
            const uint32_t node = (cursor + offset) % 8u;
            if ((freeMask & (1u << node)) == 0u) continue;
            cursor = (node + 1u) % 8u;
            return node;
        }
    }
    const uint32_t node = cursor;
    cursor = (cursor + 1u) % 8u;
    return node;
}

inline uint32_t allocateRandomMidiNode(
    uint32_t freeMask, uint32_t& randomState)
{
    std::array<uint32_t, 8u> candidates {};
    uint32_t count = 0u;
    for (uint32_t node = 0u; node < 8u; ++node) {
        if ((freeMask & 0xffu) == 0u
            || (freeMask & (1u << node)) != 0u) {
            candidates[count++] = node;
        }
    }
    return candidates[nextMidiNodeRandom(randomState) % count];
}

struct MidiNodeShuffleBag {
    uint32_t remainingMask = 0xffu;
    uint32_t lastNode = 8u;

    void reset()
    {
        remainingMask = 0xffu;
        lastNode = 8u;
    }
};

// Random-without-replacement node selection. With all nodes available, every
// node is emitted once before the bag refills. The first draw of a new bag also
// avoids the previous bag's final node, preventing a boundary repeat.
inline uint32_t allocateShuffledMidiNode(uint32_t freeMask,
    uint32_t& randomState, MidiNodeShuffleBag& bag)
{
    const uint32_t available = (freeMask & 0xffu) != 0u
        ? freeMask & 0xffu : 0xffu;
    if ((bag.remainingMask & 0xffu) == 0u) {
        bag.remainingMask = 0xffu;
    }
    uint32_t candidates = bag.remainingMask & available;
    if (candidates == 0u) {
        // Occupied voices can temporarily hide every remaining bag entry.
        // Choose an available node without discarding the inaccessible entries.
        candidates = available;
    }
    const uint32_t withoutLast = bag.lastNode < 8u
        ? candidates & ~(1u << bag.lastNode) : candidates;
    if (withoutLast != 0u) candidates = withoutLast;

    std::array<uint32_t, 8u> nodes {};
    uint32_t count = 0u;
    for (uint32_t node = 0u; node < 8u; ++node) {
        if ((candidates & (1u << node)) != 0u) nodes[count++] = node;
    }
    const uint32_t selected =
        nodes[nextMidiNodeRandom(randomState) % count];
    bag.remainingMask &= ~(1u << selected);
    bag.lastNode = selected;
    return selected;
}

inline uint32_t routeSequencerNode(uint32_t midiMode, uint32_t laneNode,
    uint32_t& sequentialCursor, uint32_t& randomState,
    MidiNodeShuffleBag& shuffleBag)
{
    if (midiMode == 2u) {
        return allocateSequentialMidiNode(0xffu, sequentialCursor);
    }
    if (midiMode == 3u) {
        return allocateShuffledMidiNode(
            0xffu, randomState, shuffleBag);
    }
    return laneNode % 8u;
}

} // namespace s3g
