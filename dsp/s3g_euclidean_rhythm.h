#pragma once

#include <algorithm>
#include <cstdint>

namespace s3g {

// Allocation-free Euclidean pulse distribution. This is the accumulator form
// of Bjorklund's algorithm: over one cycle it emits exactly `pulses` hits with
// cyclic gaps differing by at most one step. Positive rotation delays a lane.
inline bool euclideanRhythmPulse(uint32_t step, uint32_t pulses,
    uint32_t steps, uint32_t rotation = 0u)
{
    steps = std::max<uint32_t>(1u, steps);
    pulses = std::min(pulses, steps);
    if (pulses == 0u) return false;
    if (pulses == steps) return true;
    const uint32_t shifted =
        (step % steps + steps - rotation % steps) % steps;
    return (shifted * pulses) % steps < pulses;
}

// Generalized Euclidean lane count. At or below one pulse per step this is
// identical to euclideanRhythmPulse(). Above that density, every step receives
// an equal base number of hits and the remainder is Euclidean-distributed as
// one additional ratchet. The total over a cycle is exactly `pulses`.
inline uint32_t euclideanRhythmRatchetCount(uint32_t step, uint32_t pulses,
    uint32_t steps, uint32_t rotation = 0u)
{
    steps = std::max<uint32_t>(1u, steps);
    const uint32_t base = pulses / steps;
    const uint32_t remainder = pulses % steps;
    return base + (euclideanRhythmPulse(
        step, remainder, steps, rotation) ? 1u : 0u);
}

} // namespace s3g
