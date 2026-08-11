# s3g Slicer

`s3g Slicer` is a multichannel multisample CLAP instrument designed to sit
after `s3g Tracker` in REAPER. Tracker supplies timing and MIDI; this plug-in
owns sample files, slices, mapping, voices, and audio.

The bundle exposes two instruments backed by the same sampler core:

- `s3g Slicer 2` has one immutable 2-channel output and accepts
  mono or stereo files; and
- `s3g Slicer` has one immutable 16-channel output, accepts 1–16
  channel files, passes lanes through in source order, and clears every unused
  output to silence.

The first playable build includes:

- four break slots, each with a MIDI channel menu (`OMNI` or `1–16`) and an
  explicit consecutive Auto Map range;
- fixed output topology per variant, with no host restart or bus mutation when
  samples are loaded;
- one shared playback clock per voice, keeping every source channel precisely
  aligned through slice, pitch, reverse, and loop transitions;
- 32 fixed playback voices with velocity, a proportional ADSR per break,
  pitch, pan, reverse, looping, ping-pong, and choke behavior in the engine;
  one envelope gives the break a coherent articulation while attack, decay,
  and release scale to each triggered slice rather than milliseconds;
- equal and transient slicing with zero-crossing snap plus an adjustable
  0–20,000 µs pre-transient onset offset;
- manual marker add, drag, and delete, plus waveform zoom, sample-to-sample
  drawing at close zoom, distinct multichannel lanes, and a draggable
  horizontal viewport bar;
- Finder drag-and-drop onto a break card or overview waveform, including
  multi-file drops that fill successive breaks;
- an OVERVIEW page showing all four waveforms, slice markers, and independent
  playback cursors at once;
- a BREAK EDIT page with the detailed selected-break waveform, global BREAK
  ENVELOPE, and a compact mapped-note audition keyboard that shows pitch names
  and MIDI values;
- a Drum Mixer-family MIXER page with four strips providing level, stereo pan,
  low/mid/high EQ, tunable mid frequency, post-fader aux send, mute, solo,
  audition, lane meters, and width-aware middle-truncated sample names; pan is
  locked for sources wider than stereo;
- two serial post-playback insert slots per break with FILTER, DEGRADE,
  TRANSIENT, RESONATOR, EROSION, SHIFT, FOLD, REPEATER, and TIME devices;
  click `I1` or `I2` in a strip to open its editor, assign or bypass a device,
  drag its four controls, change its mode, reset it, or swap insert order
  without retriggering the active slice;
- a dedicated wet-only `s3g Break Bus` AUX processor with PRESS, bipolar SNAP,
  RECOVERY, SAT, BITE, antiderivative-antialiased CLIP, TILT, and RETURN;
  dynamics can link ALL channels, adjacent PAIRS, or run FREE, while FIELD
  SAFE disables nonlinear stages for encoded spatial material; SAT, BITE, and
  CLIP then draw as muted `BYP` controls and reject interaction;
- selected-slice gain, pitch, pan (mono/stereo only), reverse, launch-mode,
  and choke controls;
- background user-initiated file decoding and transient analysis with stale
  load cancellation when a slot is replaced or cleared;
- explicit remapping from a selectable root note and channel-aware CLAP note
  names; marker moves preserve the map while marker add/delete, re-slicing,
  and root edits deliberately require another Auto Map press;
- a resizable native macOS bank/slice editor and audition control;
- versioned project state retaining the original external sample paths;
- optional decoded-audio embedding in CLAP project state (enabled by default),
  allowing the host project to reopen without the original files; and
- one MIDI/CLAP note input and one fixed main output per variant.

Mixer controls use a runtime snapshot separate from the sample/slice bank and
publish continuously during click-drag interaction. Changing an insert, level,
pan, EQ, mute, solo, AUX send, or bus setting affects active voices on the next
audio block; it does not restart playback. Parameter edits retain insert and
post-playback histories; changing a device type clears only that insert's
history.

For spatial sources, channel order is preserved without downmixing or
per-channel timing. The 16-channel variant exposes a generic 16-channel port so
it can carry quad, octal, arbitrary discrete multichannel, or 3OA ACN/SN3D
material without claiming that every loaded file uses the same spatial
encoding. The stereo variant rejects wider files; it never folds or truncates
them. Set the REAPER track to 16 channels when using the 16-channel variant.

## Project audio

`PROJECT AUDIO: EMBED` is enabled by default. The button reports the decoded
audio size that will be added to plug-in state. REAPER stores that CLAP state
with the project, so reopening or moving the project does not require the
original sample paths. Embedded audio remains inter-channel and sample locked.

## Mixer and aux routing

The strip and bus architecture follows `s3g Drum Mixer 16`, reduced to the
four break slots. Each strip's signal order is playback voices, insert 1,
insert 2, three-band EQ, pan for mono/stereo sources, level/mute/solo,
metering, then the squared post-fader AUX send. The dry strip and processed
return meet before the global output control.

FILTER provides low-pass, band-pass, high-pass, and notch modes with cutoff,
resonance, drive, and mix. DEGRADE combines a shared multichannel sample-hold
clock with bit reduction and timing jitter. TRANSIENT uses one linked detector
for attack, sustain, and gate shaping across every source channel. RESONATOR
uses one tune/clock with independent delay state per channel, plus feedback,
damping, and parallel amount. Both insert slots process every channel at the
same frame and never exchange or reorder channels.

EROSION provides sine- or noise-modulated micro-delay with depth, feedback,
and mix, using one modulation clock across the entire source. SHIFT switches
between an approximate quadrature frequency shifter and ring modulation, with
shared carrier phase, regeneration, and color. Its linked regeneration governor
preserves the direct shifted signal while smoothly dumping only the feedback
path after 750 ms of sustained high regeneration. The dump holds for 30 ms and
recovers over about 180 ms; an emergency energy detector can invoke it sooner,
and a soft feedback ceiling catches numerical bursts. FOLD switches between
triangle wavefolding and clipping; it uses four interpolated substeps per
output sample plus DC blocking to reduce fold/clip aliasing and bias artifacts.

REPEATER detects an attack in the lane audio, captures a window beginning at
that transient, then repeats it forward, reversed, or in alternating
directions. BUFFER ranges from 8–1000 ms, REPEATS selects 1–16 repetitions, and
PITCH DECAY lowers each successive repeat by as much as one octave per repeat,
with mild attenuation and softened repeat boundaries. TIME uses the same
transient-started capture model and offers REVERSE, FREEZE, and TAPE modes with
±24-semitone playback and wet/dry mix. The third control is mode-specific:
RELEASE gives Reverse a 2–200 ms exit, DECAY makes Freeze fade to silence over
0.125–16 seconds, and BRAKE controls Tape deceleration. A new transient can
replace an active Freeze before its decay finishes.

Both devices derive one onset decision from all source channels and use one
capture boundary, write head, and fractional read head for every channel. They
do not consult host tempo, beat position, or transport state. Their fixed
memory is prepared before audio processing;
no buffer allocation occurs in the render callback. At 48 kHz the maximum
capture is about 1.36 seconds, with longer requested windows safely clamped.

A newly enabled buffer device passes dry audio until it detects an onset and
records the selected window. Repeater and the one-shot Time modes return to
listening after playback; Freeze returns to listening when its decay reaches
silence. Wet/dry mix, pitch, pitch decay, brake, and the mode-specific time
controls are smoothed during live editing. Capture length, repeat count, and
mode are latched at onset so a slider or mode change cannot restructure the
buffer already playing. Short crossfades protect capture, repeat, and return-to-
dry boundaries.

On `s3g Slicer`, the same EQ is applied independently to every source lane.
Break Bus keeps separate nonlinear and filter state per lane. Its `ALL`,
`PAIR`, and `FREE` modes alter only detector/gain linking: they never exchange,
decode, sum, or reorder samples. `FIELD SAFE` retains linked compression, SNAP,
and linear tilt while bypassing SAT, BITE, and CLIP. Unused channels remain
silent.

Click the button to select `PROJECT AUDIO: PATHS` when smaller project files
are preferable. The original paths are still stored as useful references in
embedded mode. A single plug-in instance embeds up to 1 GiB of decoded audio;
if the bank exceeds that bound, the button reports `PARTIAL` and later slots
fall back to paths.

Standard CLAP does not provide a plug-in with REAPER's current project-media
directory, so the plug-in does not silently copy files into that folder.
Embedding is the reliable automatic project-portability path. A future
explicit “collect bank to folder” operation can let the user choose the REAPER
media folder and then rewrite the stored paths.

## Build

```sh
cmake -S . -B build-clap \
  -DS3G_BUILD_CLAP_PLUGIN=ON
cmake --build build-clap --target s3g_breakbeat_slicer_clap -j 8
```

The bundle is written to:

```text
build-clap/plugins/clap_breakbeat_slicer/s3g_slicer.clap
```

Both variants are part of the 0.7 release bundle manifest. See
[`docs/s3g-breakbeat-slicer-design.md`](../../docs/s3g-breakbeat-slicer-design.md)
for the product boundary, architecture, state model, and implementation
sequence.

The research and signal-path rationale behind Break Bus is documented in
[`docs/s3g-breakbeat-aux-research.md`](../../docs/s3g-breakbeat-aux-research.md).
