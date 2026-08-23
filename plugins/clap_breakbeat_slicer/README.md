# Sample Slicer

`Sample Slicer 16` is a multichannel multisample CLAP instrument designed to sit
after Tracker in REAPER. Tracker supplies timing and MIDI; this plug-in
owns sample files, slices, mapping, voices, and audio.

The bundle exposes two instruments backed by the same sampler core:

- `Sample Slicer 2` has one immutable 2-channel output and accepts
  mono or stereo files; and
- `Sample Slicer 16` has one immutable 16-channel output, accepts 1–16
  channel files, passes lanes through in source order, and clears every unused
  output to silence.

The first playable build includes:

- four break slots, defaulting to MIDI channels `1`, `2`, `3`, and `4`, each
  with a channel menu (`OMNI` or `1–16`) and an explicit consecutive Auto Map
  range;
- fixed output topology per variant, with no host restart or bus mutation when
  samples are loaded;
- one shared playback clock per voice, keeping every source channel precisely
  aligned through slice, pitch, reverse, and loop transitions;
- 32 fixed playback voices with velocity, a proportional ADSR per break,
  pitch, pan, reverse, looping, ping-pong, and choke behavior in the engine;
  one envelope gives the break a coherent articulation while attack, decay,
  and release scale to each triggered slice rather than milliseconds;
- Sample Player-family playback controls per break: Auto, Gate, One Shot, and
  Toggle triggers; Restart, Layer, and Ignore same-note retrigger policies;
  Poly, Mono, and Legato voice modes with optional glide; Rate, Stretch, and
  negative-rate/positive-stretch Hybrid pitch; Free or host-BPM sync; and
  click-safe loop crossfade. Fresh banks default to Auto / Restart / Poly, so
  repeating one note restarts its slice while different slices may overlap;
- independent waveform cursors and MIDI-note flags for every active voice,
  plus a global KILL ALL control that clears held, latched, and looping notes;
  Kill All and the complete Project/Link/Embed storage status stay fixed in the
  global header on Overview, Break Edit, Mixer, and Mutate;
- equal and transient slicing with zero-crossing snap, an adjustable
  0–20,000 µs pre-transient onset offset, and a transient-only minimum slice
  duration from zero through 1000 ms (20 ms by default); both are shared
  sliders with exact-entry number fields;
- manual marker add, drag, and delete, plus waveform zoom, sample-to-sample
  drawing at close zoom, distinct multichannel lanes, and a draggable
  horizontal viewport bar; Loop and Ping Pong slices add direct LS/LE handles
  that stay inside the selected slice and snap to safe zero crossings;
- Finder drag-and-drop onto a break card or overview waveform, including
  multi-file drops that fill successive breaks;
- an OVERVIEW page showing all four waveforms, slice markers, and independent
  playback cursors at once;
- a BREAK EDIT page with the detailed selected-break waveform, shared AMP
  ENVELOPE, and a compact mapped-note audition keyboard that shows pitch names
  and MIDI values, and illuminates notes received from the host or auditioned
  with the mouse; both routes use the same per-slice gain, pitch, pan, direction,
  launch, and choke behavior; its Sample / Slice, Slice / Map, Mapped Slice Playback,
  Selected Slice, Timing, and Amp Envelope toolboxes follow the Sample Player
  panel style, with categorical choices exposed as shared in-canvas dropdowns
  rather than cycle buttons and numeric Timing plus per-slice gain, pitch, and
  pan controls exposed as sliders with exact entry fields;
- a MUTATE page where one source fills every empty break slot with a
  different structural mutation; rearrange, repeat, pitch, Mixer FX, AUX Bus,
  and reverse are independently selectable, with reverse off by default;
  Mixer FX and AUX assign deterministic effect recipes independently to every
  generated slice, while occupied and building slots remain locked until cleared;
  over-level rendered slices receive one channel-linked gain reduction so their
  shared peak cannot exceed 0 dBFS, while quieter slices remain unchanged;
  Play Through schedules the mapped slices in order through their normal voice
  path and the Mutate waveform displays each live cursor and MIDI-note flag;
  generated audio retains mapped hit boundaries and follows Project/Embed storage,
  and EXPORT WAV writes the selected break to a 32-bit float file without
  using another slot;
- a Drum Mixer-family MIXER page with four strips providing level, stereo pan,
  low/mid/high EQ, tunable mid frequency, post-fader aux send, mute, solo,
  audition, lane meters, and width-aware middle-truncated sample names; pan is
  locked for sources wider than stereo;
- two serial post-playback insert slots per break with FILTER, DEGRADE,
  TRANSIENT, RESONATOR, EROSION, SHIFT, FOLD, REPEATER, and TIME devices;
  click `I1` or `I2` in a strip to open its editor, assign or bypass a device,
  drag its four controls, change its mode, reset it, or swap insert order
  without retriggering the active slice;
- a dedicated wet-only Break Bus AUX processor with PRESS, bipolar SNAP,
  RECOVERY, SAT, BITE, antiderivative-antialiased CLIP, TILT, and RETURN;
  dynamics can link ALL channels, adjacent PAIRS, or run FREE, while FIELD
  SAFE disables nonlinear stages for encoded spatial material; SAT, BITE, and
  CLIP then draw as muted `BYP` controls and reject interaction;
- a slice-first Break Edit workflow: slicing and marker edits come before Start
  and Auto Map; the Method menu selects Equal or Transient behavior and a
  separate SLICE action applies it, while Auto Map unlocks post-map gain,
  pitch, pan (mono/stereo only),
  reverse, launch-mode, choke, and mapped-note audition controls;
- a clearly scoped MAPPED SLICE PLAYBACK toolbox whose trigger, retrigger,
  voice, pitch, sync, source BPM, crossfade, glide, and envelope settings apply
  to every mapped slice note; the starting note triggers slice zero and plays
  the full file only when that file is still represented by one slice;
- background user-initiated file decoding and transient analysis with stale
  load cancellation when a slot is replaced or cleared;
- explicit remapping from a selectable lowest starting note and channel-aware
  CLAP note names; every break starts at MIDI note `48 / C2`, allowing 80
  slices through note 127, while start note 0 allows all 128 slices; equal and
  transient slicing plus manual marker creation respect the `128 - start`
  ceiling, and Auto Map never moves the selected start; marker moves preserve
  the map while marker add/delete, re-slicing, and start edits deliberately
  require another Auto Map press;
- a resizable native macOS bank/slice editor and audition control;
- versioned project state retaining the original external sample paths and
  every per-break playback setting;
- Project, Link, or optional decoded-PCM Embed storage, with Project selected
  for new instances; and
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

The shared `STORE` control selects `PROJECT` (the default for new instances),
`LINK`, or `EMBED` for the complete bank. Project copies unchanged source files
on a worker to the saved REAPER project's effective media directory under
`s3g Samples` and registers them with that project. Link keeps the original
paths. Embed stores decoded 32-bit float PCM in CLAP state while retaining
inter-channel and sample lock. The header reports referenced file bytes or the
decoded `STATE` size.

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

On `Sample Slicer 16`, the same EQ is applied independently to every source lane.
Break Bus keeps separate nonlinear and filter state per lane. Its `ALL`,
`PAIR`, and `FREE` modes alter only detector/gain linking: they never exchange,
decode, sum, or reorder samples. `FIELD SAFE` retains linked compression, SNAP,
and linear tilt while bypassing SAT, BITE, and CLIP. Unused channels remain
silent.

Embed is capped at 1 GiB of decoded audio per instance. Its approximate cost is
channels × frames × four bytes, often much larger than compressed break files;
hosts can repeat that payload in undo snapshots made for FX bypass or chain
edits. Project keeps routine plug-in state small. In an unsaved project or an
unsupported host it remains pending without discarding playable audio or any
unresolved path. REAPER Save As relocates registered media only when a
copy-media option is selected; an ordinary Save As does not copy the bank.

Generated Mutate audio has no source file to link. In Project mode it is
written as lossless media under `s3g Samples`; in Embed mode its decoded PCM is
saved in state. State saving fails rather than silently losing a generated
result if its selected destination cannot be committed.

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

Both variants are part of the release bundle manifest.
