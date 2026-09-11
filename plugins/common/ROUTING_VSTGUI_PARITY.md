# Direct panning, routing, and array utility VSTGUI migration

## Scope and reference contract

This pass ports twelve CLAP editors. Installation and Windows host testing
are separate steps. The Ambi Node Bus variant is outside this pass.

The references are the retained Cocoa implementations and the existing
[direct-panning](../../docs/direct-panning.html),
[bus-mixing/routing](../../docs/bus-mixing-routing.html), and
[speaker-array utilities](../../docs/speaker-array-utilities.html) docs.
Cocoa reference captures were made before migration. Original layout
constants, projection equations, active mesh geometry, graph calculations,
and hit regions are reused; these are not generic replacement control grids.

| Editors | Preserved graphics and operations |
| --- | --- |
| Panner Layout / DBAP / LBAP / VBAP 64 | FIELD/MIXER/DESIGN, original speaker layouts and active mesh edges, AED source colors, LBAP/VBAP solver overlays, three camera presets and manual rotation/zoom, source/speaker selection and dragging, 64 source faders in 16-channel pages, mute/solo, global and selected-source controls, inside modes, custom speaker shape/count/coordinates, COPY and layout JSON LOAD/SAVE. Fixed-method variants retain their respective solvers. |
| Matrix Group 32 / 64 | Original group-width choices, all editable crosspoints, animated routing preview, six weight shapes, motion/rate/division/phase controls, spread/vortex/depth/smoothing, mode glossary, INIT, and each variant's original RANDOM distribution and seed. |
| Mixer Node Bus 128 | All sixteen nodes, source-layout outlines/faces, overlap coloring, source/output channel coupling, derived bus channel and OVER readouts, selected-node controls, LOCK Z indication, output meter grid, original three camera projections, node dragging and Shift-drag camera rotation. |
| Output Crossover 64 | Original speaker-map projection and meshes, coincident speaker labels, one to eight sub positions and channel labels, layout/high-channel/sub-offset coupling, crossover mode/frequency/focus, high/sub gains, bypass and mono fold. |
| Array Calibrate 16 / 26 / 32 / 64 | HPF/DELAY/TRIM tabs, original HPF response equation and frequency/dB grid, stage switches, global bypass and OUT, active channels, per-channel delay/trim bars and typed values, mute/polarity, and all channel pages. Bypassed controls retain their settings. |

The approved presentation changes are shared Fira Code typography, family
grays, lowercase `s3g` plus uppercase product titles, in-canvas menus, aligned
label/value baselines, centered slider handles, and proportional 65–200%
resizing through the existing CLAP lifecycle helper.

## State, automation, and files

DSP algorithm headers, CLAP identities, bus declarations, parameter IDs,
and state versions are unchanged. Existing state readers are retained,
including the panner v5/v6/v7 and Node Bus compatibility paths. Camera state
is available to the portable panners on both operating systems.

User presets use the original `.s3gpreset` codecs and retain dedicated global
OUT, as in Cocoa; host project recall restores it. Crossover has two band
gains rather than a dedicated global OUT and recalls both. Editors without
factory banks retain their original INIT/reset action rather than inventing
preset lists. Panner speaker files retain the existing
`s3g-layout-panner-speakers-v1` JSON representation and explicit coordinates.

Portable preset loading first validates an isolated plugin/model and only
publishes a successful load. Unicode paths use the shared native file-dialog
and UTF-8/UTF-16 helpers. Invalid/truncated data is rejected; non-finite state
checks were added where missing. Large finite JSON azimuths are reduced by a
full turn before reaching the original repeated-subtraction angle wrapper.

An existing LBAP/VBAP fixed-method guard also rejected Custom layouts, despite
their DESIGN controls and JSON import. It now explicitly allows Custom while
still enforcing the fixed method. This is necessary for edits/imports to
survive publication and recall; it does not add a different panning method.

Drawing reads published controls and isolated previews, not live DSP objects.
Group Matrix phase and meters are audio-published telemetry. Panners retain
their control-state mailbox; Node Bus retains its two-bank publication model.
The other editors use atomic parameter mirrors and the shared event queue.
GUI parameter writes reach DSP through process/flush, with balanced
begin/value/end events, compound-edit capacity checks, and reserved END
capacity under backpressure. Non-parameter panner design/camera changes
retain host-state dirty notifications. Existing host state-load entry points
remain separate from the GUI preset workflow.
Array Calibrate host state loading explicitly republishes the restored bank
before returning, so pre-activation host queries and the GUI agree with DSP.
This sequence has a dedicated regression check, independent of GUI preset
loading.

## Build, resources, and verification

Enable `S3G_ENABLE_ROUTING_VSTGUI_ON_MACOS=ON` for the Mac portable editors;
OFF retains the Cocoa fallback. Windows selects the portable editors when
VSTGUI is available.

The shared CMake helper links VSTGUI and packages Fira Code plus its license:

- Mac: `Contents/Resources/Fonts` inside each CLAP bundle.
- Windows: adjacent `Resources/Fonts`; identical resources may be shared
  when collecting plugins into a distribution folder.
- Panners additionally ship the pinned RapidJSON license in
  `Resources/Licenses/RapidJSON-LICENSE.txt` (inside Contents on Mac).
- Missing fonts use the existing safe platform fallback.

Local verification on 2026-09-10:

- Release Mac arm64 and Windows x64 builds for all twelve targets.
- All twelve retained Cocoa fallback targets still build.
- Twelve native Mac CLAP lifecycle/proportional resizing tests pass.
- Twelve canvas tests exercise parameter edits, Unicode presets, truncated
  files, automation backpressure, all panner source pages, all group matrix
  cells, all Node Bus nodes, crossover coupling, and every calibrator page.
  Calibrator tests also use real native text-edit controls for typed values.
- Panner camera coordinates are compared directly with Cocoa for every menu
  layout and all three views; Node Bus projections, Group Matrix RANDOM,
  and calibrator response calculations also compare with the originals.
- Eight existing panner publication/legacy-state tests and the Node Bus
  parameter/state audit pass.
- Mac bundle signatures and packaged font/license copies verify; Windows
  modules are x64 PE DLLs exporting `clap_entry` with only system DLL imports.
- CLAP-validator 0.3.2 passes all twelve Mac plugins: each reports 16 passed,
  five skipped, zero failed, and zero warnings. This includes process-driven
  parameter changes and buffered/pre-activation state reproducibility.
  The machine-readable report is
  `build-clap-sample-vstgui-fidelity/routing-validator.json`.

Canvas tests are selected with:

```sh
ctest --test-dir build-clap-sample-vstgui-fidelity -L routing --output-on-failure
python3 scripts/generate-routing-geometry.py --check
```

Mac GUI tests require access to the window server. Optional
`S3G_ROUTING_CAPTURE_DIR` writes reference PNGs. Current comparison artifacts
are under `build-clap-canvas-cocoa-parity/routing-reference/masters` (Cocoa)
and `build-clap-sample-vstgui-fidelity/routing-reference` (VSTGUI).
The geometry check verifies the literal active Cocoa mesh extraction for
both Panner and Node Bus; disabled experimental Cocoa meshes are excluded.

Windows compilation does not establish REAPER runtime parity. Test these
builds on Windows for dialog behavior, project recall, display scaling, and
the spatial/matrix/calibration interactions before distribution.

## Node Bus cursor/playback correction

A playback-time cursor edit exposed a publication race not covered by the
initial single-threaded interaction tests. The portable editor publishes its
complete parameter snapshot before queuing host notifications. The original
queue consumer also wrote to the control bank from the audio callback,
violating its single-producer contract. If it read while the GUI bank was
busy, it could fall back to the never-published, zero-filled audio bank;
sanitizing that data produced exactly one inactive node and then republished
the damaged topology.

Unpublished banks are now rejected, and each bank's publication stamp is
included inside its sequence-protected write. The portable GUI queue only
delivers host notifications; DSP consumes the already-published control
snapshot. Active host parameter flushes write the audio bank, while inactive
flushes use the main-thread control bank. Delayed GUI notifications therefore
also cannot overwrite newer host automation. Parameter IDs, DSP algorithms,
field gestures, layouts, and saved-state formats are unchanged.

The Node Bus canvas test now forces the exact busy/unpublished-bank sequence,
checks active-flush ownership and delayed notifications against host
automation, and repeatedly clicks/drags/scrolls the cursor in all three views
while a separate thread processes 128-channel audio. It checks the node count,
every active flag, cursor delivery, and balanced gestures throughout.

The corrected Mac build passes twenty consecutive stress runs, the native
GUI/resizing check, the Node Bus parameter/state audit, and CLAP-validator.
Windows x64 and both retained Cocoa Node Bus targets also build successfully.
The corrected build requires a separate installation step.
