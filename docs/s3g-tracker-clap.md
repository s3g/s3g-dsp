# s3g Tracker CLAP preview

For operation inside REAPER, start with the public
[s3g Tracker guide](s3g-tracker.html). The source-level
[user guide](../tracker/docs/USER_GUIDE.md) and tracker
[documentation index](../tracker/docs/README.md) separate current product
documentation from earlier standalone design history.

`s3g Tracker` is the existing native tracker workspace converted into a
MIDI-generating CLAP device. This is not a reduced step-sequencer rewrite.
The CLAP embeds the maintained tracker workspace and its native project model:

- one unified polymetric grid per lane: NOTE, VOL, SEQ1/V1, and SEQ2/V2;
- up to 32 tracks, independent column lengths/phase/stride/direction/mute;
- pattern bank, Song page, prepared pattern-boundary changes, and global
  tracker loop;
- timing warps, ratchet, microtime, delay, flam, stutter, accent, ghost,
  probability, skip, offset, repeat-previous, and Euclidean actions;
- live-code command entry and categorized help;
- embedded Tracker, Song, Geometry, Warps, Console, and Help pages, with
  Geometry, Warps, and Console detachable into independent windows;
- per-lane REAPER MIDI-bus/channel controls and value-envelope editor;
- responsive tracker zoom, scrolling, direct cell entry, and copy/paste;
- native schema-4 tracker project JSON stored in the REAPER project.

REAPER replaces the standalone host boundary. It owns the audio device,
transport, tempo, downstream instruments/effects, rendering, and final mix.
The tracker publishes MIDI events with sample offsets and follows REAPER seek
and loop discontinuities without allocating or destroying runtime state in the
audio callback.

## Build and test

```sh
cmake --preset clap-tracker
cmake --build --preset clap-tracker -j4
ctest --test-dir build-tracker -L tracker --output-on-failure
```

The resulting bundle is:

```text
build-tracker/plugins/clap_tracker/s3g_tracker.clap
```

## REAPER routing

Place `s3g Tracker` before a drum instrument in an FX chain, or route its MIDI
to instrument tracks. The initial project is the tracker demo configured as a
General MIDI basic kit on channel 10.

BUS 01–08 map directly to the eight CLAP note-output buses. Each track owns one
bus and one-based MIDI channel; there is no row-level instrument or bus column.
REAPER decides which downstream track or plug-in receives each bus. The
tracker exposes no audio ports and never opens a global macOS MIDI destination.
Click the `Bxx` or `CHxx` control in a lane header to edit that lane directly;
there is no tracker-global channel assignment. Click that header's `SYNC`
control to restart its NOTE, VOL, and both sequencing action/value loops
together at row 1 on the next tracker tick, ignoring their authored phase
offsets without changing them or moving REAPER and the other tracks.

Tracker-local editing uses a Control-key layer: `Control-A/C/X/V` for select
all/copy/cut/paste and `Control-=/−/0` for zoom. Command-key combinations are
deliberately passed through to REAPER. Double-click a lane name to edit it.

The editor play button uses the CLAP transport-control extension to request
host play/continue when the host supplies it. REAPER tempo and quarter-note
position are authoritative. The restart arrow resets the tracker to row 1
without stopping REAPER; Panic sends tracked note-offs plus CC 123 on every
channel of all eight MIDI buses. Pattern edits are saved automatically into
the plug-in state and mark the REAPER project dirty.
The displayed host BPM is read-only; the RATE menu applies musical ratios
(`1/4×`, `1/2×`, `2/3×`, `1×`, `3/2×`, `2×`, or `4×`) to the tracker clock.
The tracker also retains swing, functional timing warps, column phase, and its
optional row loop.

## DSP boundary

The tracker has no internal instrument rack, row-level instrument column,
mixer page, audio-device chooser, synthesis editor, or audio-parameter effects.
SEQ1 and SEQ2 accept only sequencing behaviors such as probability, ratchet,
microtime, delay, flam, stutter, accent, ghost, skip, offset,
repeat-previous, and Euclidean gating. Sound is produced by downstream s3g-dsp
drum/sampler CLAP instruments or other plug-ins in REAPER. The CLAP normalizes
legacy internal routing and audio actions at its state boundary.

Future work should improve explicit downstream device mapping and controller
integration for BU16, E16, and the Keychron keypad without replacing the
tracker workspace or duplicating DSP inside this plug-in.
