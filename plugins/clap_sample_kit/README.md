# Sample Kit 32

`s3g Sample Kit 32` is a sixteen-pad Sample-family instrument for mono and
stereo files. Pads are mapped consecutively from MIDI note 36 by default and
can be shifted together with Base Note. Its fixed 32-channel CLAP port carries
one to sixteen active stereo pairs: set Active Output Pairs to 1 to fold every
pad to channels 1–2, or raise it to 16 and route pads independently through
channels 1–32.

The editor is organized like a compact hardware sampler:

- a playable 4×4 pad face with per-pad meters and file drop;
- Sample, Sound, Amp / Filter, and Natural pages for the selected pad;
- a complete sixteen-strip mixer with gain, pan, mute, solo, and pair
  assignment;
- a Master / Output page with drive, bit reduction, rate hold, master level,
  global tuning, and active-pair selection;
- whole-kit `.s3gkit` load/save, plus Project, Link, and Embed sample storage.

On the Chop page, double-clicking the zoomable waveform adds a manual marker
at the displayed position. Markers remain draggable and the layout is capped
at sixteen regions.

Every pad holds up to eight alternate samples. Cycle, Shuffle, Random,
No Repeat, and Velocity modes select them per hit. Natural can add bounded,
bell-shaped gain, pitch, start, and delayed-timing variation. It has per-pad
and global bypasses, and its saved seed keeps playback and offline rendering
repeatable. A pad with only one loaded sample still receives all four per-hit
variations; only alternate-sample selection is inactive. The Natural page
includes the global bypass and live readouts of the offsets applied to the
most recent hit. Dropping several files on one pad fills its variation row.

Each loaded variation has an independent polyphonic Sample Player voice bank. Start/End,
forward and reverse one-shots, forward/reverse loops, ping-pong playback,
loop crossfade, tune, velocity response, a multimode resonant filter, and a
proportional ADSR are stored per pad. Four choke groups support closed/open
hat and mutually exclusive phrase workflows. One Shot, Gate, and Toggle
launch modes are available.

The ready-to-load
[`s3g Sample Kit BU16 4x4`](../../controllers/intech_grid_bu16/sample_kit/README.md)
profile maps one velocity-sensitive Intech Grid BU16 to the complete pad bank.
Its physical rows match the editor, with Pads 1–4 at the bottom and Pads 13–16
at the top. The profile expands the BU16's low native strike values by 2.5×.
The plug-in's global Velocity Curve offers Very Soft, Soft, Linear, Hard, and
Fixed 127 responses for further adjustment or use with other controllers.

Drive, bit depth, and rate hold are post-mixer character stages on every active
pair. Time-based, spatial, and parallel effects are intentionally left to
downstream plug-ins: assign pads to stereo pairs, then process those pairs in
the host. Inactive pairs remain silent.

Build the bundle with:

```sh
cmake --build build-clap --target s3g_sample_kit_clap -j 8
```

The macOS bundle is written to:

```text
build-clap/plugins/clap_sample_kit/s3g_sample_kit.clap
```
