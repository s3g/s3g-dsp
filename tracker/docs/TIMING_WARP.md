# Timing warps

The tracker now has a bounded, deterministic phase-warp layer inspired by the
musical framework described in *Functional Iterative Sequence Warping: An Open
Framework for Exploring Warped Ramps, Exponential and Euclidean Rhythms, and
Multi-Part Swing* (ICMC 2025 proceedings).

This is an independent C++ implementation of the mathematical idea. No source
code was copied from a reference implementation. That distinction matters
because the paper explains a compositional model, while the tracker needs a
real-time-safe implementation with its own event and clock contracts.

## Model

Each transform maps normalized phase `x` in `[0, 1]` to another normalized
phase. Transforms are composed serially: the output of one becomes the input of
the next. The available transforms are exponential, stepped quantization, and
Euclidean quantization.

A transform can operate on the full cycle or a selected phase segment. Its
local ramp can repeat inside that segment, and `alpha` blends the transformed
ramp with its input. Outside the segment, phase is unchanged. Validation fixes
cycle endpoints. The low-level stack normalizes repairable definitions and
reports every correction; the live-command layer rejects invalid user input
transactionally.

The sequencer first applies conventional two-part swing, then maps global cycle
phase through the stack. Tick duration is the difference between adjacent
warped phases converted to samples. This keeps scheduling independent of audio
block size and preserves the total cycle duration.

That adjacent-phase duration positions primary tracker rows. The v8-style
RR/DL/ST/GL sequencing effects deliberately use the straight nominal tempo
tick for their internal spacing, even while swing or this warp layer moves the
primary rows. A regression test locks that separation of contracts.

## Real-time boundary

The stack has a fixed maximum of 32 normalized descriptors. Mapping evaluates
those descriptors directly, is allocation-free and `noexcept`, and an optional
`precompute()` API is available for consumers that need a lookup table. The
sequencer currently evaluates the stack directly.

Stepped transforms can intentionally make several tracker ticks coincident.
The current policy advances every coincident tick in stable order, writes the
capacity-limited event prefix, and counts every omitted event. A worst-case
collision regression test makes that behavior explicit. Warps remain on the
sole sequencer worker rather than running on the device callback. Before Play,
the live-audio path bounds warped ticks across its complete callback-derived
audio horizon and one maximum callback block; it rejects a pattern whose
off/on and FX expansion could exceed either fixed event budget. Runtime
overflow still fails closed, resets both destinations, and is reported.

The current live command path limits every warp cycle to 16 ticks. Untimed
patterns retain the 32-track, four-event-per-tick 2048-event collision bound.
When timing actions are reachable, preflight uses 18 events per track/tick
(release, two parameters, and the maximum RR+ST fifteen-onset expansion) and
rejects a warped horizon that cannot fit. Future hits live in a separate fixed
8192-event timeline; this does not weaken the 2048-event due-block bound.

The first integration applies one global stack to the transport. Future timing
lanes can reuse the transform model but still need explicit per-lane phase,
launch, collision, and event-density rules.

See [Live Commands](LIVE_COMMANDS.md) for command syntax.
