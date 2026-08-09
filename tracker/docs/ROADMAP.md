# Roadmap

## M0 — architecture bootstrap (complete)

- Local repository, CMake presets, BSD license.
- Dependency-free C++ sequencer proof and tests.
- Cocoa tracker/routing/controller concept shell.
- Max design-reference audit, architecture, product, and CLAP decisions.

Exit: clean configure/build/test with no Max or `s3g-dsp` dependency.

## M1 — MIDI-first vertical slice (current)

Implemented in the testable slice:

- Editable, horizontally scrollable grid with up to 32 tracks, independent
  NOTE/INS/VOL playheads, per-track indexed instruments, and optional per-row
  overrides.
- Retained Rhythm Geometry pop-out and full-width selected-lane Volume
  Envelope module.
- Top-deck native command engine/console with history and completion, drum
  kits and aliases, compact masks and velocity sequences, independent
  NOTE/INS/VOL structure, Euclidean/sieve writing, seeded Max-derived mask
  transforms, and
  mute/solo/name performance commands.
- Retained Song alternate window with explicit Pattern/Song transport mode,
  logical-tick row duration/repeats, BPM/swing/mute overrides, quantized row
  queueing, and final-tail draining.
- Main-workspace performance mixer with every sequence strip, event-stage
  velocity/input trim, NOTE mute/solo, instrument selection, step activity, and a
  real smoothed post-decode MAIN OUT gain/mute/peak strip. True per-lane audio
  mixing remains a graph-bus task.
- Per-MIDI-instrument selection across eight separately published named virtual
  sources or physical destinations plus channel selection,
  bounded-lookahead timestamped
  note-on/note-off scheduling, configurable gate, Stop/Panic, route cleanup,
  diagnostics, and a fail-hard CoreMIDI loopback test executable for hosts
  with MIDI service access.
- Canonical destination-neutral scheduled events with stable note IDs,
  a 2048-event fixed capacity, functional timing-warp commands, and destination
  derived from each resolved rack instrument.
- Retained Functional Timing Warps editor with a composite curve, tick guides,
  cycle control, transform stack, and complete exponential/step/Euclidean
  parameter editing.
- Independent `FX1/V1` and `FX2/V2` fields with typed catalog actions, stable
  parameter keys, deterministic duplicate/order rules, and a Global-only first
  membrane catalog. Actions resolve relative to each lane's remembered
  per-row instrument.
- A right-side song instrument index and available-type library. New songs
  contain one Membrane Kick, one Stereo Slice Sampler, and MIDI OUT; `+ ADD`
  creates further kick, sampler, DaisySP drum, or MIDI instances with
  independent state/routes. PSG and YMFM sources are archived, not active.
- Native Membrane Kick window with active-instance tabs and normalized base patches,
  grouped controls, strike geometry, audition, reset, and five complete
  presets. Base editing is
  separate from sample-timed pattern FX automation.
- Optional live selectable-stereo AUHAL output with the embedded rack,
  callback-derived audio/MIDI horizons, one shared device clock, bounded batch
  fanout, instrument-derived availability, and fail-closed reset/re-anchor.
- Exact-offset CLAP events, per-slot state isolation, stereo/quad decode, and
  fail-hard Core Audio/CoreMIDI test executables for service-enabled machines.
- Global row-loop selection across all columns, Reaper-style Play/Loop/Stop/
  Pause states, and pause/resume that preserves musical phase.
- Exact double-click length entry for every polymetric column, targeted
  deterministic normalized-VOL randomization commands, and velocity-scaled output across
  Membrane, sampler, DaisySP, and MIDI instruments.
- A persistent bottom Device View with instrument-to-master topology and two
  explicit empty effect slots ready for the graph-device phase.
- A strict versioned `.s3gt` project document with complete pattern/column,
  transport, rack, sampler, MIDI-route, MAIN OUT, seed, per-pattern alias, and Song state;
  transactional decode; atomic save; and guarded asynchronous Cocoa Open/Save.
- A schema-v2 ordered pattern bank with stable IDs, active selection,
  New/Duplicate/Rename/Delete controls, per-pattern aliases and lane note memory, strict
  Song references, and responsive selection controls. Multi-pattern Song
  drafts persist now; allocation-free cross-pattern playback remains gated on
  the prepared-runtime boundary design.
- A 760×560 responsive main workspace: intrinsic tracker lanes and mixer rows
  retain readability inside two-axis scroll views, while transport/module
  strips, toolbox, envelope, console, and Device View adapt without imposing a
  track-count-derived window width.

Remaining before M1 exit:

- Design per-lane timing fields beyond the delivered NOTE/INS/VOL and FX
  pairs.
- Complete duration/choke behavior and destination latency calibration.
- Extend the delivered authoritative PR/SK/OF/RP/EU source/gate layer with
  live-random value cells and shared undoable semantic actions.
- Run the 15/16/13/7 Superior Drummer soak test and define timing/drop gates.
- Add the first controller input path after semantic actions are extracted from
  GUI/console commands.

Exit: a 15/16/13/7-length groove drives Superior Drummer for an extended run
without timing drift, stuck notes, UI-thread MIDI, or unreported queue drops.

## M2 — tight controller integration

- Shared semantic action registry and configurable bindings.
- Keychron navigation/entry/transport map; MIDI firmware only after actions
  stabilize.
- BU16 selected-lane step/pressure page, lane banking, LED diffs, reconnect
  snapshots.
- OXI E16 NRPN pages for lane structure, values, transforms, and transport;
  ring feedback.
- Persistent endpoint IDs, hot-plug, loop suppression, device diagnostics.

Exit: the core four-lane performance can be created, varied, muted, launched,
and recovered without touching the mouse.

## M3 — composition system

- Versioned project document, undo/redo, autosave/recovery.
- Extend the delivered pattern bank, Song boundary switching, and quantized
  command-driven variation launches with snapshots, chains, and direct GUI
  pattern launch controls.
- Move the current command mutations behind shared undoable semantic actions,
  then add command search and the remaining high-value language features.
- Connect the current Euclidean/mask/transform and delivered first-pass seeded
  generation/mutation commands to Geometry controls, then add root/scale-aware
  generation, richer typed symbols, and multi-parameter envelopes.
- Explicit native schema evolution policy; no Max file compatibility layer.

Exit: the selected musical behaviors from Max have native specifications and
tests, without carrying over file formats or accidental runtime quirks.

## M4 — internal sample engine

Foundation delivered:

- Instrument-node contract, fixed event buffers, and a five-instance embedded
  membrane CLAP rack created under one shared entry-session lifetime.
- Stable rack node IDs, release-safe per-row instrument changes, independent
  tails, base-patch mailboxes, and a native rack editor.
- Live selectable-device stereo output through the proven AUHAL layer, including
  raw timestamp preflight, callback telemetry, and the shared audio/MIDI clock.
- Headless stereo/quad decode and CLAP state coverage.
- A platform-neutral native `StereoSliceSamplerNode`: immutable stereo/mono
  assets, 128 slices, 32 bounded voices, exact-offset onset/release/choke,
  native-rate conversion, reverse, and deterministic tests.
- Indexed sampler instances, mono/stereo AudioToolbox loading, waveform/slice
  editor, base-note mapping, reverse editing, audition, and safe live-graph
  restart on asset or slice-table changes.
- Background decoding, bounded waveform/transient analysis, automatic and
  manual zero-snapped slicing, zoom/pan, per-slice gain/reverse, persisted file
  references and slice metadata, retryable rehydration, and visible failures.

Remaining work:

- Pinned curated `s3g-dsp` dependency and reusable platform targets.
- Extend the native sampler with per-hit pitch, higher-quality interpolation,
  and bit/sample-rate color modes.
- Persist output device UID; add live stereo/quad master-layout switching, channel routing,
  meters, and offline sample-event tests.
- Lane-isolated render buses for true track gain, pan, sends, inserts, and
  audio meters feeding the existing layout-aware MAIN OUT boundary.

Exit: the same project can switch between external Superior MIDI and internal
samples without changing tracker timing.

## M5 — DSP graph and drum synthesis

- Native graph topology, prepared swaps, latency compensation, state blobs.
- Curated embedded CLAP nodes from `s3g-dsp` where state/editor reuse pays.
- Add a dedicated snare body/wire/noise instrument with explicit wire decay
  controls; do not present a kick preset as a completed snare.
- Add metal/noise hi-hat and cymbal voices with closed/open articulation and
  choke behavior behind the same node/event contract.
- Add a gabber kick device chain around the membrane voice: pre/post drive,
  waveshaping, clip topology, filtering, resampling, and safe output trim.
- Reusable drum synth voices, sends/inserts, stereo/quad/discrete/ambisonic
  buses, sample plus synthesis layering.
- Evaluate `s3g-dsp` Encoder Modal as a later percussion source after the
  dedicated core drum set is coherent.
- Real-time allocation/timing gates derived from `s3g-dsp` audits.
- Evaluate selected pinned DaisySP drum/effect modules behind native tracker
  adapters and sound-quality fixtures. PSG/YM2151 are archived and excluded
  from the active graph; new chip emulators are not an active milestone.

Exit: multichannel projects survive layout/device changes and meet declared
callback-load and no-allocation gates.

## M6 — optional DAW product

- Thin tracker CLAP wrapper following host transport and exposing note/audio
  ports.
- State compatibility between standalone projects and plugin state.
- Decide separately whether any third-party plugin scanning or AU/VST3 hosting
  belongs in the product.
