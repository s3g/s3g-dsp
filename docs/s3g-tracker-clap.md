# s3g Tracker CLAP preview

`s3g Tracker` is the existing native tracker workspace converted into a
MIDI-generating CLAP device. This is not a reduced step-sequencer rewrite.
The CLAP embeds the maintained tracker workspace and its native project model:

- editable polymetric NOTE, BUS, VOL, and two FX action/value column pairs;
- up to 32 tracks, independent column lengths/phase/stride/direction/mute;
- pattern bank, Song page, prepared pattern-boundary changes, and global
  tracker loop;
- timing warps, ratchet, microtime, delay, flam, stutter, accent, ghost,
  probability, skip, offset, repeat-previous, and Euclidean actions;
- live-code command entry and categorized help;
- embedded Tracker, Song, Geometry, Warps, Console, and Help pages;
- selected-track REAPER MIDI-bus/channel strip and value-envelope editor;
- responsive tracker zoom, scrolling, direct cell entry, and copy/paste;
- native schema-3 tracker project JSON stored in the REAPER project.

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

BUS 01–08 map directly to the eight CLAP note-output buses. Each track owns a
one-based MIDI channel, and tracker BUS cells can change the output bus per
row. REAPER decides which downstream track or plug-in receives each bus. The
tracker exposes no audio ports and never opens a global macOS MIDI destination.

The editor transport buttons use the CLAP transport-control extension when the
host supplies it. REAPER tempo and quarter-note position are authoritative;
the tracker retains ticks-per-beat, swing, functional timing warps, column
phase, and its optional row loop.

## DSP boundary

The tracker has no internal instrument rack, mixer page, audio-device chooser,
or synthesis editor. Sound is produced by downstream s3g-dsp drum/sampler CLAP
instruments or other plug-ins in REAPER. The project codec retains the internal
node representation required by the sequencer, but the CLAP normalizes it to
the eight MIDI buses at its state boundary.

Future work should improve explicit downstream device mapping and controller
integration for BU16, E16, and the Keychron keypad without replacing the
tracker workspace or duplicating DSP inside this plug-in.
