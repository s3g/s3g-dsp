# Stereo slice sampler

## Delivered foundation

`StereoSliceSamplerNode` is the first native sample-engine component. It owns
no file handles and performs no decoding, allocation, locking, or asset
mutation on the render thread. The macOS control-side loader publishes one
immutable `StereoSampleAsset` containing left/right float channels; mono assets
are represented by an empty right channel and duplicated at render time.

The current node provides:

- up to 128 validated slices with exclusive start/end frames, gain, and reverse;
- MIDI notes mapped consecutively from a configurable base note (36 by default);
- 32 fixed one-shot voices with deterministic oldest-voice stealing;
- native-duration playback across differing source and host sample rates using
  linear interpolation;
- exact block-relative note-on, note-off, and choke placement;
- a 2 ms click-safe note-off/choke release; and
- true stereo preservation, mono duplication, finite-input validation, and
  explicit output clamping.

Tests cover exact-offset stereo output, slice selection, gain, reverse, release,
and stopped-only mutation. The node is deliberately in the platform-neutral
core so offline tests do not require Core Audio or the embedded CLAP rack.

## Live vertical slice

`STEREO SLICE SAMPLER` is an active indexed instrument with three independent
instances. New songs place one sampler at index `01`; `sampler`, `sample`, and
`slice` are accepted console names. Double-click its rack row or press
`Command-7` to open the editor.

The native editor provides:

- AudioToolbox decoding for mono or stereo formats supported by macOS;
- asynchronous decode plus bounded peak/onset analysis on a background queue;
- an immutable analysis publication separate from the render asset;
- a Retina-aware peak waveform with horizontal navigation and dynamic zoom
  through connected, sample-accurate PCM detail;
- deterministic transient auto-slicing with optional 4 ms zero-cross snap;
- double-click/add, drag, right-click/delete, and explicit marker controls;
- 1, 4, 8, 16, 32, or 64 equal-slice layouts;
- consecutive note mapping from an editable MIDI base note;
- tracker-facing decimal `S000`–`S127` slice identities alongside the current
  MIDI-note map;
- per-slice 0.00–2.00 gain, reverse, selected-slice audition, and a waveform
  playhead; and
- an explicit stop/reconfigure/restart path so asset and slice-table changes
  never race the callback or stop the sequencing transport.

Peak and transient results are derived editor data. They are rebuilt when an
asset is restored and are intentionally not part of the persisted song schema.
Marker edits change only slice metadata; source samples remain immutable.

The sampler mixes through a dedicated stereo bus after HOA decoding and before
the smoothed MAIN OUT gain. Quad tests map its stereo pair to the front pair;
the live application remains stereo for now.

## Next vertical slice

1. Add an immutable asset pool identified by stable song-local sample IDs.
2. Add non-destructive crop/normalize commands.
3. Persist source bookmark/path, content hash, optional embedded audio, and
   slice metadata in the new project format. Old Max files are not a target.
4. Add per-hit pitch, choke groups, interpolation modes, bit-depth/sample-rate
   color, filtering, and drive through typed FX actions.

The source asset stays stereo. A later graph adapter may route it to stereo,
quad, or discrete buses without changing the asset or slice model.
