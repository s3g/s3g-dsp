# Monitoring / Fold-down VSTGUI migration

## Scope and references

Ported Output Autogain Stereo 2, Output Autogain Quad 4, Analyzer Meter 64,
and Matrix Upmix 64. Installation is a separate step.

The references are the retained Cocoa implementations and the existing
[family documentation](../../docs/monitoring-fold-down.html),
[Stereo](../../docs/output-autogain-stereo.html),
[Quad](../../docs/output-autogain-quad.html),
[Analyzer](../../docs/analyzer-meter.html), and
[Matrix Upmix](../../docs/matrix-upmix.html) documentation.
Fresh Cocoa captures were made before editing; graphics were adapted from
the source calculations rather than reconstructed from screenshots.

Shared presentation changes are the approved Fira Code font, family grays,
lowercase `s3g` with uppercase product titles, centered slider handles,
aligned text, and in-canvas menus. DSP headers, CLAP IDs, bus declarations,
parameter IDs, and state versions are unchanged.

## Parity contract

| Editor | Preserved behavior and graphics |
| --- | --- |
| Output Autogain Stereo 2 | Eight layouts, three autogain modes, up to 128 inputs, stereo projection/pan map, reference halos, gain-dependent connections, output meters, weight/3D rails, and disabled 3D controls outside projection layouts. |
| Output Autogain Quad 4 | Eight layouts, three autogain modes, original L/R/RB/LB output order and speaker map, gain-dependent connection intensities/node sizes, four meters, and all routing controls. |
| Analyzer Meter 64 | GRID/FIELD/HEAT, all 14 field layouts, original grid-column rule, channel and dB labels, camera presets/drag/zoom, 64-by-144 rolling heat history, calibrated activity colors, and WIDTH/layout coupling. WIDTH remains display-only; audio passthrough is not reduced. |
| Matrix Upmix 64 | All 14 factory presets, both 42-entry format menus in their original column-major order, four Auto Fill recipes and weight shapes, APPLY/CLEAR, click/paint/vertical-drag/right-delete crosspoints, selected gain, input normalization/output limiting, overload marks, and the contained stereo-difference polarity rules. |
| Matrix custom layouts | COUNT/SELECT/AZ/EL/DST sliders, typed coordinate fields, AED/XYZ readouts, default layouts, COPY OTHER MAP, channel-order semantics, and DONE. |
| Matrix Tier Rings | Original tier grouping, projected positions, contours, label collision/density rules, gain-dependent node colors/sizes, focus hit targets, real rectangular/irregular plans and centered zeniths. Every dense-matrix crosspoint remains addressable. |

LOAD/SAVE, INIT/factory selection, Unicode paths, and host project recall are
preserved. User preset loading retains global OUT in the three processors;
host state recall restores it. The analyzer retains its three display
parameters. Existing state readers remain: Stereo v1/v2, Quad v1, Analyzer
v1–v3, and Matrix v1–v7, including legacy matrix normalization migration.
Portable loads are transactional and reject truncated/non-finite state.

Autogain and Analyzer refresh at the original 24 Hz; Matrix at 30 Hz.
Fractional-millisecond scheduling is opt-in; existing canvases retain their
33 ms default.

## Resizing and independent windows

Autogain and Matrix use the shared proportional 65–200% CLAP lifecycle.
Analyzer intentionally retains independent width/height resizing and the
original 720-by-430 minimum, with maxima extended to 1960-by-1120. Its canvas
reflows for aspect changes while supporting proportional enlargement.
Only editors opting into responsive layout use the new reflow path.

Matrix POP OUT opens a separate native window containing a second VSTGUI
canvas, not a substitute page or an in-host overlay. Main and detached views
share the editing model and focus. DOCK returns the host to LAYOUT;
closing the detached window leaves the host on MATRIX. Host hide also hides
the detached window, and host destruction closes its editor before releasing
the native parent. The detached window additionally supports proportional
65–200% resizing. Cocoa uses NSPanel; Windows uses a module-owned window
class that is unregistered after the last window is destroyed.

## Threading and resources

Autogain GUI edits publish atomic control values; process/flush owns gain
ramps. Analyzer parameters/telemetry remain atomic. Matrix uses a
main-thread-only editing model and an allocation-free SPSC triple-buffer
mailbox for custom layouts/manual routing; only process/flush modifies the
audio DSP. Parameter values come from the current atomic bank rather than
being replayed from older routing snapshots. Intermediate host format changes
also propagate the stereo-only polarity constraint.

Parameter notifications use the shared bounded queue with balanced
begin/value/end gestures and reserved END capacity. Compound parameter edits
preflight queue capacity. Non-parameter matrix edits mark host state dirty.
Preset codecs operate on isolated models before publishing successful loads.

All four targets use the shared resource/font/dialog helpers:

- Mac: each CLAP bundle contains `Contents/Resources/Fonts/FiraCode-Regular.ttf`
  and `FiraCode-LICENSE.txt`.
- Windows: each build target stages the same files in adjacent
  `Resources/Fonts`; a combined distribution can share that identical folder.
- Missing private fonts are non-fatal and use the foundation's platform
  monospace/system fallback.
- UTF-8 paths are converted through the existing Windows UTF-16 helpers.

Calibrated bespoke colors are converted to sRGB using
`scripts/generate-monitoring-color-reference.swift`; existing shared grays
and heat colors use `s3g_topology_colors.h`.

## Build and verification — 2026-09-10

Enable `S3G_ENABLE_MONITORING_VSTGUI_ON_MACOS=ON` in a portable-GUI Mac build.
The default remains OFF for the retained Cocoa fallback. Windows selects
the portable editors whenever VSTGUI is available.

Verified locally:

- Release Mac arm64 builds and Windows x64 cross-builds for all four targets.
- All four retained Cocoa fallback targets still build.
- `audit_gui_monitoring`: four native CLAP lifecycle/resizing checks pass.
- Four dedicated canvas tests cover the controls, menus, graphics/state,
  queue pressure, Unicode presets, malformed state, and float/double audio.
  Matrix compares factory state/coefficients and all named-layout Tier Rings
  coordinates directly against Cocoa; all 4096 crosspoints are hit-tested.
- With `S3G_MONITOR_NATIVE_WINDOWS=1`, the Matrix test also exercises native
  coordinate text entry and POP OUT/DOCK/close/hide/destruction.
- Fifteen selected tests passed, including existing Matrix DSP/CLAP tests,
  the shared parameter queue, and representative Sample, Drum, Topology,
  and Memory canvas regressions. The seven monitoring tests passed again
  after the final build.
- The four canvas tests also passed when their executables were copied
  without font resources; Mac correctly selected Menlo.
- clap-validator 0.3.2 passed all four plugins (with its applicable skipped
  tests reported in the logs), with no failures.
- Mac bundle signatures/font/license hashes checked. Windows binaries export
  `clap_entry`, link the portable GUI, and have no external MinGW runtime DLL
  dependency.

Fresh native PNG/PDF references are in
`build-clap-sample-vstgui-fidelity/monitoring-reference`.
Original Cocoa references are in
`build-clap-canvas-cocoa-parity/monitoring-reference`.
Detailed matched renders are produced by setting `S3G_MONITOR_CAPTURE_DIR`
when running the canvas tests. Validator results are in
`build-clap-sample-vstgui-fidelity/monitoring-validator.json`.

Not yet verified: running these four new Windows editors in Windows REAPER,
including detached-window DPI/multi-monitor behavior and file dialogs.
Mac REAPER user acceptance is also still required after installation.
