# Sample Motion

`s3g Sample Motion 2` and `s3g Sample Motion 32` are polyphonic sample
instruments that turn position in a mono or stereo recording into a performed
trajectory. They combine bounded and stochastic source motion, nested packet
articulation, realtime segment-event scheduling, and trigger-time output
routing in one polyphonic voice model.

The instruments provide:

- Hover, Mirror, seeded Drunk, alternating Zigzag, one-way Forward/Reverse,
  crawling Moving Loop, and non-inverting Round Trip source motion;
- a default `Normal` speed basis where `1x` follows the sample's ordinary
  native playback rate, including source duration and note pitch, plus a
  legacy-compatible absolute `Hertz` basis;
- Continuous playback, ordinary Packets, or Motor, which nests that packet
  stream inside a slower outer envelope;
- Linear, Rounded, Exponential, and Plateau Motor envelope shapes, with
  adjustable Symmetry;
- Motion Speed, Travel, Jitter, packet Inner Rate and Duty, Motor Outer Rate,
  and context-sensitive Join smoothing;
- a fixed-size segment-event layer with Freeze, Iterate, Pulser, Doublets,
  Bounce, and Routed Iterate models, plus Clock/Packet/Turn triggers, repeat
  count, Step, pitch and level variation, interval acceleration, and Cut or
  four-lane Layer playback;
- as many as sixteen Poly voices, plus Mono and phase-preserving Legato modes;
- Auto, Gate, One Shot, and Toggle triggers, chromatic Root/Tune/Fine control,
  amplitude envelope, velocity response, and MIDI channel selection;
- asynchronous load/drop/clear and Project, Link, or Embed sample storage,
  waveform zoom/pan/fit, S/E/L handles, factory/file presets, motion scope,
  host-correct gestures, and compositor note/output flags; and
- either fixed stereo output or a 32-channel edition with 2–32 active outputs,
  mono or stereo-pair voice destinations, adjacent or split-bank pairs, and
  sequential, reverse, palindrome, random, or no-repeat random-cycle
  allocation at note, trajectory-turn, or segment-event boundaries, with an
  optional adjacent-destination guard.

## Motion and articulation

Hover and Mirror always move back and forth. Drunk can choose either direction
for each bounded step. Zigzag deliberately alternates forward and reverse
segments while Travel sets their maximum length and Jitter varies their length
and speed. Forward and Reverse are ordinary one-way scans; Join crossfades their
wrap seam.

Moving Loop reads one way and advances its Field window by Step on every wrap.
Round Trip uses Mirror's forward/reverse geometry without inverting the return.
Step also advances Iterate, Routed Iterate, and Doublets source selection.

Packets applies only the inner pulse train. Motor uses the same train inside a
slower shaped outer envelope and reverses Hover, Mirror, Drunk, and Zigzag
travel on the falling side. Forward and Reverse remain one-way in Motor.

## Playback hierarchy and segment events

The editor presents the engine as a numbered flow: `1 SOURCE FIELD + MOTION`
feeds one active `2 SOUND` model, which then feeds `3 VOICE / OUTPUT`. A single
`SOUND` menu selects Continuous, Packets, Motor, Freeze, Iterate, Pulser,
Doublets, Bounce, or Routed Iterate. This matches the DSP: selecting an Event
process replaces Direct articulation rather than layering an unseen process
over it. The second panel shows only controls used by the selected Sound model,
and the scope repeats the active Path, Source, and Sound stages.

An Event-Sound readout states exactly how the selected process obtains its
source position and which Motion properties remain active. Motion always sets
event playback speed and onset direction, and `Turn` can use its boundaries as
the event clock. It does not always set event position: Freeze and Bounce lock
to Locus; Doublets uses Locus plus Step; Iterate and Routed Iterate follow the
live Motion cursor only at Step zero and otherwise use a Locus-plus-Step
sequence;
Pulser chooses random positions in the Locus-centered Field, except that Moving
Loop carries the complete random-selection Field through Start–End.

The readout classifies the connection as `FULL PATH FOLLOW`, `MOVING FIELD`, or
`POSITION OVERRIDE`, with an additional warning when Bounce acceleration or
contiguous Doublets grouping requires Clock. Controls that the current Path,
Sound, or Trigger cannot use remain visible for orientation but are dimmed and
non-interactive. Travel is active only for Drunk and Zigzag; Source Step is
active for Moving Loop, Iterate, Doublets, and Routed Iterate. With an Event
sound, Clock uses Event Rate, Packet exposes the actual Packet Rate (`Inner
Rate`), and Turn follows Path boundaries. Pulser retains Event Rate as a
duration basis under Packet or Turn, while Bounce Start Rate and Accel are
disabled outside Clock.

Choosing an Event entry from `SOUND` loads a usable starting recipe for that
process, after which
every displayed value remains freely adjustable. Host automation of the
Segment Model parameter changes only the parameter and does not rewrite the
other automated controls.

The Event panel gives each process its own relevant control names instead of a
generic shared bank. Freeze repeats one Locus slice; Iterate follows the live
trajectory at Step zero or advances by Step, with natural timing, pitch, and
level drift; Pulser produces short shaped selections from random positions;
Doublets emits contiguous AAABBBCCC-style slice groups; Bounce accelerates,
decays, and shrinks within each repeat group; and Routed Iterate allocates every
event independently. Clock uses Event Rate, except that Clocked Doublets starts
the next repetition when the prior slice ends. Packet follows the inner packet
clock, and Turn follows trajectory boundaries. Cut replaces the current event
while Layer permits as many as four simultaneous event lanes per MIDI voice.
Join and Shape control event edges, while Jitter becomes repeatable event-time
scatter for the iteration and packet models.

Sample Motion 32 can route at Note, Turn, or Segment boundaries. Turn with
Zigzag can reassign a held voice at each change of direction; Segment assigns
each event independently and is selected automatically by Routed Iterate.

Version-1 states load with their former absolute Motion Rate by selecting
`Hertz` automatically and preserve the earlier linear Motor envelope. New
states default to `Normal` and `Rounded`.

## Sample storage

`PROJECT` is the new-instance default. It copies the unchanged source on the
loader worker to the saved REAPER project's effective media directory under
`s3g Samples` and registers the copy with that project. `LINK` retains the
original path. `EMBED` writes decoded 32-bit float PCM into CLAP state; its
approximate size is channels × frames × four bytes, and hosts may repeat that
large payload in undo snapshots for FX bypass or chain changes. A Project
request remains pending in an unsaved project or unsupported host without
discarding the current source or unresolved path. REAPER Save As relocates the
registered file only when a copy-media option is selected. A decoded asset
with no usable locator remains embedded as a safety fallback while that
locator is unavailable.

Relevant source material and inspiration are listed in the central
[References](../../docs/references.html#sample-motion).

## MIDI

Notes scale source-motion speed chromatically around Root. The selected MIDI
channel also accepts:

| CC | Control |
| --- | --- |
| 16 / 17 | Start / End |
| 18 / 19 | Locus / Field |
| 20 / 21 | Motion Speed / Travel |
| 22 / 23 | Inner Rate / Outer Rate |
| 123 | Stop all voices |

The stereo descriptor exposes 36 CLAP parameters. Sample Motion 32 exposes 42,
adding four allocator controls plus Route On and Avoid Adjacent.

## Build

From the repository root:

```sh
cmake -S . -B build-clap -DS3G_BUILD_CLAP_PLUGINS=ON \
  -DS3G_BUILD_SAMPLE_MOTION_PREVIEW=ON
cmake --build build-clap --target s3g_sample_motion_clap
```

The bundle is written below `build-clap/plugins/clap_sample_motion/` and
contains both descriptors.
