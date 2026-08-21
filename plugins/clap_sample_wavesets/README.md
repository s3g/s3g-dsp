# Sample Wavesets

The bundle exposes two chromatic sample instruments whose playback unit is a
group of rising-crossing cycles:

- `s3g Sample Wavesets 2` has a fixed stereo output.
- `s3g Sample Wavesets 32` has a fixed 32-channel host output and assigns each
  newly triggered voice to an output channel or stereo pair.

Both instruments accept mono or stereo files. Their range, loop, pitch,
trigger, and voice controls follow Sample Player 2 so ordinary sampler
techniques remain direct while the Wavesets section changes the internal
reading of the file.

The instrument provides:

- one mono or stereo source with visible playback and loop marker lines, draggable
  Start, End, Loop Start, and Loop End handles, pointer-centered scroll zoom,
  Shift-scroll pan, and double-click fit;
- Forward, Reverse, wrapping-loop, and ping-pong play modes;
- as many as sixteen Poly voices plus Mono and Legato modes;
- Auto, Gate, One Shot, and Toggle triggers with Attack, Release, Root, Tune,
  Fine, and velocity response;
- Left Mono, Right Mono, Sum Mono, and True Stereo source interpretations;
- groups of 1, 2, 4, 8, 16, or 32 cycles, 1–16 repeats, and a 1–16 group
  stride;
- Stretch, Preserve, and Hold relationships between output time and source
  progression;
- Forward, Reverse, Pendulum, and Shuffle ordering inside each group;
- Repeat, Omit, Replace, Envelope, Multiply, Average, Interpolate, Fractal,
  additive Harmonic, Group Reverse, Cycle Reverse, and Telescope
  transformations with continuous Depth and Join controls;
- Raw, 8 kHz, 4 kHz, 1 kHz, and 250 Hz crossing-detail analysis;
- a per-source-channel group scope, one compositor-driven and note-labelled
  playhead per active voice, and twelve starting presets plus INIT;
- in Wavesets 32, a selectable active width from 2–32 channels, mono or stereo
  voice destinations, adjacent or split-bank stereo pairs, and Sequential,
  Reverse Sequential, Palindrome, Random, or no-repeat Random Cycle routing;
  and
- path or embedded project-audio state.

The waveform and scope draw one numbered lane per loaded source channel. The
scope follows the newest-triggered active voice, overlays its ordered source
group in gray with the processed result in the channel color, and marks its
live cycle phase. Cycle widths retain their measured sample durations instead
of being normalized to equal divisions. Explicit group bounds remain visible:
scrolling over the scope scales the bounded group between detailed and compact
views, and double-clicking fits the group to the scope. The analysis map stores
crossing units separately for every source channel and also stores a summed
map. Wavesets 2 exposes a fixed stereo output. Wavesets 32 uses the same map,
engine, lane renderer, and cursor model while widening only the voice-output
stage.

## Wavesets 32 output routing

The host-facing port is always 32 channels. `Outputs` selects the active
routing width from 2 through 32 without asking the host to renegotiate its bus.
Every route rule operates only inside that width. In Mono mode every selected
channel is a destination. In Stereo Pair mode there are
`floor(Outputs / 2)` destinations; an odd final channel stays silent.

`Adjacent` maps stereo destinations as 1/2, 3/4, and so on. `Split Banks`
divides the active width into two equal banks: with eight outputs the pairs are
1/5, 2/6, 3/7, and 4/8; with 32 they are 1/17 through 16/32. True Stereo keeps
the source lanes separate in a stereo destination. Left, Right, and Sum source
modes duplicate their mono result across the pair. A mono destination sums a
True Stereo source.

Sequential starts at the lowest destination, Reverse Sequential at the
highest, and Palindrome changes direction without repeating its endpoints.
Random permits immediate repeats. Random Cycle shuffles the active
destinations and visits each exactly once before reshuffling, without repeating
the boundary destination. The allocator advances on every accepted note-on:
Mono and Legato therefore move on every key press, while Poly retains each
assignment for that voice's complete lifetime and layers voices according to
the existing waveset voice rules. Changing a routing control resets the next
trigger to the beginning of the newly selected routing contract; sounding
voices keep their prior assignments.

## Tracker and MIDI

MIDI notes play chromatic pitches around Root. Note length controls Gate voices
and looped Auto voices, and note velocity is blended by the Velocity parameter.
`MIDI Receive` selects Omni or channels 1–16.

The selected channel also accepts:

| CC | Control |
| --- | --- |
| 16 / 17 | Start / End |
| 18 / 19 | Loop Start / Loop End |
| 20 / 21 | Group / Repeat |
| 22 / 23 | Depth / Join |
| 123 | Stop and clear all voices |

All 25 Wavesets 2 controls are CLAP parameters. Wavesets 32 adds Output Order,
Voice Output, Stereo Pair Map, and Output Count for a total of 29, so Tracker
CC lanes, host automation, and MIDI learn can coexist with note sequencing.

## Analysis model

Crossing analysis runs away from the audio callback. Each eligible rising
crossing is interpolated between adjacent samples and retains its fractional
start, length, peak, and RMS. Playback reads the resulting immutable maps, with
one shared voice clock keeping True Stereo output coordinated even though left
and right use independent crossing boundaries.

## Cursor presentation contract

Each neutral Sample Player-style voice cursor uses a persistent Core Animation
trajectory established by Sample Doubles, and its flag reports the MIDI note
number. The waveform cursor is a continuous source-transport indicator: it
begins exactly at Start, crosses every displayed source-channel lane, and moves
at the source-time rate implied by pitch, Time, Repeat, and Stride. The scope
retains the lower-level view of the newest voice's bounded waveset group,
measured cycle proportions, and channel-specific crossings. Routine AppKit
repaint and peak-readout traffic never drives the visible playheads. The 30 Hz
timer is reserved for static GUI feedback.

## Process model

The Process menu adapts the most useful single-source operations from the
[CDP DISTORT function set](https://www.composersdesktop.com/docs/html/cdistort.htm)
to real-time, polyphonic playback. Average aligns and averages the cycles in the
current group. Interpolate morphs the current cycle toward its adjacent source
cycle over Repeat. Fractal adds recursively smaller power-of-two copies.
Harmonic adds integer-ratio copies and is distinct from Multiply's single
phase-multiplied read. Group Reverse reverses both the cycle order and the
samples inside the selected group, while Cycle Reverse only reverses samples
inside each cycle. Telescope superimposes the group against a shared cycle and
progressively contracts it as Depth rises.

Delete, Filter, Divide, Replim, and Pulsed remain better treated as explicit
selection, frequency, or amplitude modes alongside Process. Interact requires a
second source, while the broader Distmark, Distmore, Splinter, Overload, and
Quirk families either construct larger-scale form or duplicate general-purpose
distortion, so they are outside this instrument's present one-source playback
scope.

Build the bundle with:

```sh
cmake --build build-clap --target s3g_sample_wavesets_clap -j 8
```

The build-tree bundle is written to:

```text
build-clap/plugins/clap_sample_wavesets/s3g_sample_wavesets.clap
```

The installer publishes the family bundle as `s3g_sample_wavesets_2.clap`;
hosts enumerate both plug-in identities from that bundle.
