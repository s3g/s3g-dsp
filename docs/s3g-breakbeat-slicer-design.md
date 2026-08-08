# s3g Slicer

Status: playable source-build CLAP preview. Both fixed-output variants share a
four-break engine and editor with per-break MIDI filtering, explicit Auto Map,
four simultaneous waveform/playhead overviews, and a detailed slice editor.

## Product decision

Build **s3g Slicer** as a separate multichannel CLAP instrument that pairs
with s3g Tracker through ordinary MIDI. Do not put sample playback back inside
the Tracker plug-in.

- **s3g Tracker owns sequence behavior:** host synchronization, polymeter,
  probability, microtiming, retriggers, ratchets, direction, warps, pattern and
  song structure, and MIDI bus/channel output.
- **s3g Slicer owns sound behavior:** sample files, slice analysis and
  editing, MIDI-to-slice mapping, voices, envelopes, choking, looping, pitch,
  playback character, channel-preserving spatial audio, and audio output.
- **REAPER owns routing and mixing:** Tracker sends MIDI to one or more slicer
  tracks; each slicer returns audio to its own REAPER track and can be followed
  by any s3g-dsp or third-party effects.

This boundary also makes the slicer useful from REAPER's MIDI editor, another
sequencer, or a controller without loading s3g Tracker.

Proposed identity:

| Item | Value |
|---|---|
| Product name | `s3g Slicer` |
| CLAP target | `s3g_slicer.clap` |
| CLAP identifier | `org.s3g.s3g-dsp.breakbeat-slicer` |
| Initial I/O | two shared-core variants: fixed stereo or fixed 16-channel output; one CLAP note input |
| Break bank | 4 independently mapped break slots |
| Voices | 32 fixed voices with deterministic stealing |

## What is already reusable

The earlier work is a substantial engine and editor prototype, not disposable
standalone code.

| Existing component | Keep | Adapt |
|---|---|---|
| `SampleAsset` | immutable 1–16 channel float audio and validation | retain one shared frame domain across every source channel |
| `StereoSampleAnalysis` | bounded peak cache and transient locations | store one immutable analysis object per sample slot |
| transient slicing | deterministic onset analysis and zero-cross snapping | expose sensitivity, minimum spacing, and maximum slice count in the editor |
| equal slicing | deterministic contiguous slice construction | add musically named counts and a direct count editor |
| marker editing | add, drag, delete, snap | add undo/redo local to the plug-in editor |
| `SampleSlice` | start/end, gain, reverse | add pitch, pan, choke group, play mode, and loop region |
| `SamplerEnvelope` | click-safe ADSR | store a normalized envelope per slice so articulation follows marker and pitch changes |
| sampler voice engine | sample-offset events, interpolation, velocity, reverse, 32 voices | one voice clock reads up to 16 sample-locked channels from an immutable bank snapshot |
| native waveform view | Retina-aware peak view, sample-level zoom, playhead, marker gestures | detach it from `TrackerViewState` and bind it to the selected bank slot |
| macOS decoder | native floating-point file decoding | accept and validate 1–16 equal-length channels, then add worker loading and path/bookmark state |

The original Max breakbeat patch also established several useful interaction
ideas: onset and novelty analysis, 64-slice editing, waveform zoom, zero-cross
nudge, polyphonic playback, an ADSR overlay, duration/rate/amplitude controls,
and slice metadata stored beside the sample. The C++ implementation already
improves on that prototype with true 1–16 channel playback, immutable assets, exact
event offsets, deterministic voice stealing, and tested de-clicking.

## Multisample model

The bank deliberately holds four substantial breaks rather than sixteen small
slots. Each break owns a MIDI channel filter (`OMNI` or channels `1–16`) and a
consecutive note range.

```text
Break Bank (4 slots)
  break 1: amen_01.wav -> slices 00...15, MIDI channel 1
  break 2: think.wav   -> slices 00...07, MIDI channel 2
  break 3: fills.wav   -> slices 00...11, MIDI omni
              |
              v
Per-break Auto Map
  C1  -> break 1 / slice 00
  C#1 -> break 1 / slice 01
  ...
  C2  -> slot 01 / slice 00
  ...
              |
              v
32 fixed playback voices -> configured 2 / 4 / 8 / 16-channel output
```

Auto Map commits every current slice consecutively from the break's root note.
Moving a marker preserves that mapping because slice indices do not change.
Loading, re-slicing, adding/deleting a marker, or changing the root makes the
map stale; pressing Auto Map again explicitly rebuilds it. Ranges may overlap because
channel filtering belongs to each break. Host note names include the break and
slice and publish the corresponding channel when it is not Omni.

This model supports both important workflows:

1. **Break mode:** one file is divided into slices and consecutive Tracker
   notes address its pieces.
2. **Kit mode:** several one-shot or sliced files share the bank and are placed
   anywhere on the 128-note map.

## Relationship to Amigo and ProTracker

Amigo is a useful interaction and playback-character reference, particularly
its separate Sample/Slice modes, equal and transient slicing, direct waveform
editing, root-note mapping, mono/poly choice, gate, loop and ping-pong modes,
velocity switch, optional interpolation, reverse, and 8-bit/downsampled source
path. Its current slice mode is monophonic and holds one loaded sample, so that
is not the architecture to copy for a multisample s3g instrument.

The s3g implementation should be independently developed and should not claim
cycle-accurate Amiga or Paula emulation. The character section should instead
offer explicit, understandable choices:

- `CLEAN`: floating-point source with high-quality interpolation;
- `TRACKER`: 8-bit quantized source, a selectable tracker-rate table, and
  zero-order-hold or linear playback;
- `CRUNCH`: continuously variable rate reduction, bit depth, reconstruction
  filtering, and drive using the existing s3g character DSP conventions.

The 36 historical-rate workflow and processing a genuinely quantized,
downsampled source buffer are worth adopting conceptually: that produces a
different result from placing a generic bitcrusher after a clean sampler. An
independent cycle-repeat stretch can be developed later; it should be called
`CYCLE STRETCH`, not presented as an Akai emulation.

References:

- [Amigo product page](https://potenzadsp.com/plugins/amigo/)
- [Amigo manual](https://potenzadsp.com/wp-content/uploads/2024/10/Amigo-Manual.pdf)

## Editor structure

Use one resizable window with three pages. Keep the established s3g black and
reduced-gray theme, IBM Plex Mono, restrained semantic color, and large cell
text.

### OVERVIEW

- four break cards with filename, channel count, slice count, root note,
  MIDI-channel menu, and Auto Map state;
- four simultaneous waveform overviews with slice markers and independent
  real-time playback cursors;
- drag files onto an empty or occupied row;
- multi-select files and fill successive slots;
- audition the whole sample or its first mapped slice;
- show missing-file and modified-file state without a modal dialog.

### BREAK EDIT

- selected file waveform with connected sample-level zoom, a proportional
  draggable viewport bar, and fine horizontal scrolling;
- a separate waveform lane for every decoded source channel;
- transient, novelty, equal, and manual slicing;
- add/drag/delete markers, zero-cross snap, undo/redo;
- slice start/end, loop start/end, gain, pan, pitch, reverse, choke, and launch
  mode;
- a per-slice ADSR editor with attack, decay, and release expressed as
  proportions of the rendered slice duration, plus a live playhead;
- keyboard audition using the currently mapped note.

Novelty slicing was present in the Max/FluCoMa prototype but is not in the C++
analysis code yet. It is a second analysis stage, not a blocker for the first
playable plug-in.

- compact two-octave keyboard audition for the current mapped slices.

Keyboard cells reserve separate lines for the pitch name and numeric MIDI note
value. The taller control rows keep slice values, action labels, and keys
readable without crowding the waveform.

### MIXER

- four Drum Mixer-family strips with post-voice low/mid/high EQ, tunable mid
  frequency, break level, stereo pan, mute, solo, audition, post-fader AUX
  send, and per-break peak meters;
- an AUX BUS processor with Drive, Glue, Room, Weight, Tone, Return, activity,
  and gain-reduction indication;
- one output panel controlling the host-visible output gain;
- multichannel break pan remains locked so quad, octal, and 3OA channel order
  is never folded or altered; Slicer 16 applies EQ per discrete channel and
  runs the AUX return on eight linked pairs without downmixing or reordering.

Playback-character controls can become a later page without changing the
four-break mapping model.

The current AUX character stage is temporary. The researched replacement is
specified in [s3g Break Bus: AUX processor research and design direction](s3g-breakbeat-aux-research.md).

## CLAP and host contract

The plug-in should expose:

- one MIDI/CLAP note input port;
- one immutable main audio output: stereo in `s3g Slicer 2`, or 16 discrete
  channels in `s3g Slicer 16` (unused lanes are silent);
- dynamic note names for mapped slices;
- automatable global performance parameters only; and
- versioned state containing bank metadata, mapping, slices, and global sound
  parameters.

Bank slots and hundreds of slice properties should not each become fixed CLAP
parameters. They are document state edited through the custom GUI. Useful live
parameters such as output gain, global pitch, envelope times, playback model,
rate/bit-depth character, and selected-slot audition can remain host-visible.

Tracker integration is intentionally ordinary MIDI:

```text
s3g Tracker lane
  -> REAPER MIDI send (bus/channel filter or remap)
  -> s3g Slicer track
  -> fixed stereo or 16-channel discrete audio
  -> REAPER mixer / s3g effects
```

A Tracker note chooses a mapped slice. `VOL` becomes note velocity. Note length
matters in `GATE`, `LOOP`, and `PING PONG`; it is harmless in `ONE SHOT`.
Tracker retriggers, probability, microtiming, and warps therefore work without
special integration code.

## State and sample portability

State version 6 saves global parameters, the selected break, output
configuration, external paths, slice tables, per-slice/per-slot properties,
each break's committed root/count, MIDI channel, level, pan, EQ, mid frequency,
AUX send, mute and solo, the shared AUX processor settings, and the
project-audio embedding preference. With
embedding enabled, decoded 1–16 channel floating-point audio follows the fixed
metadata in the CLAP state stream and can be restored without its original
file. State loading validates channel/frame bounds before allocating and
rebuilds analysis from the embedded asset.

Embedded audio is the current default for project reliability, while the GUI
can switch to compact path-only state and displays the approximate decoded
payload size. The bounded first implementation embeds up to 1 GiB per plug-in
instance and falls back to paths for later slots if that total is exceeded.
Future state should prefer original compressed bytes when the decoder can
restore them from memory, and use content hashes before asking the user to
relocate a missing external file.

Do not carry the old Max metadata format forward. A small JSON bank/preset
format may be offered for interchange, while the CLAP state remains a compact,
length-prefixed binary format with strict bounds checks.

## Real-time publication

The old node only permits asset and slice mutation while stopped. That was safe
for the standalone application but is not acceptable for a hosted sampler.
Loading, slicing, or remapping must not stop REAPER transport.

Use the same prepared-snapshot principle as the Tracker scheduler. Sample-bank
and runtime-mixer publication are separate: a fader, EQ, mute, solo, send, or
bus edit must never replace sample ownership or reset a voice, filter, or bus
tail.

1. Decode, validate, analyze, and build a complete immutable bank snapshot on
   a worker/control thread.
2. Publish only a pointer-sized pending update to the audio processor.
3. Adopt the snapshot at a process-block boundary.
4. Existing voices retain their original immutable asset and copied slice
   bounds until they finish; new notes use the new sample-bank snapshot.
5. Reclaim retired snapshots on a non-audio thread after an audio-thread
   generation acknowledgement.

Mixer edits publish a smaller POD snapshot independently. The audio thread
adopts it without clearing voice clocks, post-strip EQ histories, playheads, or
AUX state. Mixer state remains duplicated in the document bank only so project
serialization and the editor have one coherent saved model.

The process callback performs no file I/O, allocation, locks, sample analysis,
or object destruction. Sample loading failure leaves the last valid bank
playing.

## Implementation sequence

### 1. Extract and prove the core

- move the asset, analysis, slice, envelope, and voice code from Tracker into a
  sampler-owned platform-neutral module;
- leave a temporary include alias if Tracker tests still require the old path;
- add a four-break immutable bank, per-break note ranges, and fixed voice state;
- add tests for cross-break/channel triggering, overlapping ranges, velocity,
  note-off, choke groups, reverse, pitch, and snapshot replacement.

Exit condition: an offline test can trigger slices from separate assets at
exact frame offsets, including a 16-channel asset, with no render-thread
allocation or per-channel clock drift.

### 2. First playable CLAP

- create `plugins/clap_breakbeat_slicer`;
- implement note input, configurable multichannel output, state, parameters,
  tail, note names, and
  a minimal responsive Cocoa editor;
- load up to four files, select a break, auto-slice equally or by transient, edit
  markers, auto-map, audition, and play from Tracker/REAPER;
- keep playback clean/linear for this milestone.

Exit condition: save and reopen a REAPER project, relocate files if necessary,
and play two mapped breaks from separate Tracker lanes.

### 3. Breakbeat performance controls

- per-slice pitch, pan, gain, reverse, choke and launch mode;
- loop and ping-pong playback with boundary crossfades;
- host-tempo metadata and a non-destructive original-BPM/bars display;
- per-break MIDI filters, CLAP note names, presets, and drag/drop multi-load.

Exit condition: a full break kit can be programmed without opening a generic
host parameter list.

### 4. Tracker character

- offline/prepared 8-bit source buffers;
- tracker rate table and interpolation choices;
- reconstruction filter and drive;
- independently implemented cycle-repeat stretch;
- optional novelty/structural analysis.

Exit condition: clean and deliberately period-correct/crunchy paths are both
stable, gain-matched enough to compare, and automatable without rebuilding
audio on the render thread.

### 5. Expansion

- velocity layers and round robin;
- multiple multichannel audio output buses if REAPER/CLAP routing tests justify
  them;
- sample-pool deduplication across slots;
- optional SFZ export/import for basic zones, without making SFZ the internal
  state model.

## First implementation judgment

The first vertical slice is now implemented in
`plugins/clap_breakbeat_slicer`, backed by `dsp/s3g_breakbeat_slicer.h`.
It provides four break slots, 128 slices per break, explicit consecutive Auto
Map ranges, per-break Omni/channel filtering, 32 fixed voices, four live
waveform overviews, a detailed multichannel waveform/marker editor, equal and
transient slicing, root-note remapping, keyboard audition,
velocity-sensitive playback, envelopes,
pitch/pan/reverse, loop and choke behavior, CLAP state, dynamic note names,
and two immutable host-visible variants. Stereo exposes two outputs and accepts
mono/stereo assets. Slicer 16 exposes sixteen generic outputs and accepts 1–16
channel assets; unused outputs are cleared to silence every block.

Multichannel timing is deliberately a structural invariant. Each voice stores
one source position, increment, envelope, direction, and loop state. It reads
all active channels at that position and advances the position exactly once
afterward. Slice markers are shared frame indices, so changing slices cannot
offset an individual channel. Pan applies only to mono and stereo sources;
quad, octal, and ambisonic channels pass in their original order.
A source is never truncated or folded into an invalid spatial field. The
stereo variant rejects assets wider than two channels; the fixed 16-channel
variant preserves source ordering and needs no run-time port reconfiguration.

This remains a source-build preview and is intentionally absent from the
release bundle manifest. Finder files can be dropped onto a break card or
overview waveform, and user-initiated file decode and analysis run on a
generation-checked worker; CLAP state restoration remains synchronous by
contract. State version 7 embeds decoded multichannel samples and per-slice
normalized envelopes directly in
the host project. Current limitations are explicit: bank replacement ends
currently playing voices, path-only state has no relocation UI, and loop
points, fine tune, and labels are not yet all exposed by the custom
editor. Those are the next publication/editor tasks; they do not require
changing the Tracker/MIDI boundary.

Build and validate the preview on macOS with:

```sh
cmake -S . -B build-clap \
  -DS3G_BUILD_CLAP_PLUGIN=ON \
  -DS3G_BUILD_BREAKBEAT_SLICER_PREVIEW=ON
cmake --build build-clap \
  --target s3g_breakbeat_slicer_clap \
           s3g_breakbeat_slicer_clap_smoke \
           s3g_breakbeat_slicer_smoke
ctest --test-dir build-clap -R s3g_breakbeat_slicer --output-on-failure
```

The resulting bundle is
`build-clap/plugins/clap_breakbeat_slicer/s3g_slicer.clap`.
