# LF Synth, Lowform, Stack and Conduit: VSTGUI parity

The retained Cocoa implementations are the source of geometry, drawing order,
visibility and hit maps. References: `docs/low-frequency-synth.html`,
`docs/processor-lowform.html`, `docs/processor-stack.html` and
`docs/processor-conduit.html`. Some checked-in documentation images predate
Lowform's two-page consolidation and Stack's SCORE page; the current Cocoa
source, not those older images, determines the port.

## Preserved features

- LF Synth: both original columns and all eight panels, 32 parameters,
  logarithmic controls, sixteen presets, safe random, MIDI receive and the
  amplitude-LFO clock/division/position controls.
- Lowform: BUILD/MOTION, eight engine-dependent control sets and panel titles,
  conditional Modal Drive, three live modulation indicators, category-ordered
  two-column target menus, secondary routes, expression/controller settings,
  layer envelopes and all four eight-step arp lanes (Pitch, Accent, Gate/Tie,
  Octave). Right-click always edits Pitch REST; drag painting and double-click
  restore each lane's own value. All 120 parameter identities are unchanged.
- Stack: PLAY/RIG A/RIG B/SCORE, both custom arpeggiator lanes, linking/copying
  rigs, mute/level/pan, speaker choices; two six-string tab grids, four sections,
  eight arrangement slots and two per-player row locks. Fret entry, two-digit
  timing, holds, rests, repeat, bracket adjustment, arrow/Tab/Return navigation
  and Command/Control clipboard shortcuts retain the original semantics.
  All five score randomizers remain independent of sound randomization.
  The original 60 Hz score publication/playhead pipeline remains intact.
  Row-lock context menus retain their heading, B-rig note, control groups and
  current values, using custom separated rows instead of native OS menus.
- Conduit: two-column 26-material menu; mono/stereo port configurations and
  four input-listen choices; pedal/position, all 19 sliders; energy, reduction,
  mic/path/body readouts and the original 160-sample, 20 Hz containment history.
  Factory/random preserves Input, Mix and Out; PANIC stays an audio-thread action.

## Shared behavior and state

All four use packaged Fira Code with the established fallback, shared grayscale,
aligned labels/slider handles and separated custom menus. Long labels fit their
original rectangles without moving controls. Shared lifecycle and resource/path
helpers provide proportional 65–200% sizing on macOS and Windows, and UTF-8 file
paths/UTF-16 Windows dialogs. Stack headings are disabled popup rows; the new
optional selection mask leaves existing popup callers unchanged.

Parameter IDs, ranges, defaults, plugin IDs and project-state versions are
unchanged. LF/Lowform/Stack retain their audio-thread parameter queues. Conduit's
portable editor now publishes through the same queue pattern, rather than
writing directly to the running DSP; rendering and state saves use atomic
published values. The audio algorithms are unchanged.

Custom `.s3gpreset` loads decode an isolated inactive instance, then enqueue the
complete parameter update, preserving OUT. Stack also restores score cells,
arrangement and locks. Factory/random retains each plugin's original preservation
rules. Note: Lowform's guide describes controller/arp preservation for preset
load generally, but the actual Cocoa file-load helper preserves OUT only;
the port retains that behavior. Factory/random uses the broader preservation
policy, including Motion Clock for randomization.

## Build and acceptance

Mac: `S3G_ENABLE_STEREO_PROCESSORS_VSTGUI_ON_MACOS=ON`. OFF retains Cocoa.
Windows enables these editors with the existing portable-GUI option, without
requiring the unrelated complete-component build switch.

Build targets: `s3g_low_frequency_synth_clap`, `s3g_processor_lowform_clap`,
`s3g_processor_stack_clap`, `s3g_processor_conduit_clap`.
Direct interaction tests: `s3g_stereo_{lf,lowform,stack,conduit}_canvas_smoke`.
Set `S3G_STEREO_CAPTURE_DIR` to capture factory/page/menu/live PNGs.

Acceptance includes all parameters, visible sliders/menu endpoints, factory
presets, dynamic pages, custom editors, UTF-8 preset filenames, OUT preservation,
chunked state streams, automation backpressure/gesture balance, audio and native
parenting/65–200% resizing. Separate Cocoa/VSTGUI modules are compared with
`s3g_input_encoder_clap_parity_smoke` for state, metadata, float and double audio.

Mac installation is separate; follow the maintained
[local-build installation guide](../../docs/building-from-source.html#install-local)
for the full collection and back up existing bundles before replacing them.
Windows packaging: `cmake -P scripts/package-windows-stereo-processors.cmake`.
It verifies x64 PE/export/static-runtime linkage and packages font/license files,
manual acceptance notes and SHA-256 hashes. Actual Windows REAPER GUI/dialog and
clipboard acceptance remains a manual test, not a claim of the Mac tests.

## Verification recorded 2026-09-11

- All four macOS VSTGUI bundles and Windows x64 CLAP targets compiled.
- Four direct canvas tests and the shared dropdown regression passed; native
  bundle lifecycle/parenting/proportional-resize tests passed for all four.
- CLAP validator passed for all four; the original four DSP and four CLAP smoke
  suites also passed, with the CLAP tests loading the new VSTGUI binaries.
- Independently compiled Cocoa and VSTGUI modules matched parameter metadata,
  chunked state and all three deterministic audio scenes in both float and
  double precision: maximum sample difference was zero. The parity harness now
  restores the initial scene between sample formats, because the first pass
  automates MIDI receive and may otherwise suppress the second pass's notes.
- Windows package archive integrity and every payload SHA-256 hash verified.
  Fira Code and its license are included. No installed Mac bundles were changed.
