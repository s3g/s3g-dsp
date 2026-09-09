# Slicer and Ambi Grain VSTGUI migration

The original Cocoa views and the Sample Slicer / Processor Ambi Grain guides
are the reference. Keep the same geometry, visualizations, DSP, state formats,
sample-channel handling, and interaction semantics. Accepted changes are Fira
Code, family-sized titles with lowercase `s3g` and uppercase product names in
shared `#D3D3D3`, and proportional 65–200% resizing. Use foundation
`drawPluginTitle` only for GUI presentation; host names, IDs, preset names, and
paths retain their original spelling.

References: `docs/s3g-slicer.html`, `docs/sample-slicer.html`,
`docs/processor-ambi-grain.html`, the original images under
`docs/assets/plugin-guis/`, and both plugin-local Cocoa implementations. The
Cocoa code and original reference images remain available for comparison.

## Implemented inventory

- [x] Slicer Overview: four independent waves, markers, cursors/note flags,
  channel and Auto Map controls, successive multi-file drop.
- [x] Slicer Break Edit: Equal/Transient action, zero-crossing marker and LS/LE
  editing, zoom/pan/navigator, Start/Auto Map, mapped-control locking, playback,
  envelope, exact numeric entry, and mapped audition keyboard.
- [x] Slicer Mixer: four live strips, EQ/pan/levels/meters, both insert editors,
  nine devices and modes, live updates without retrigger, Break Bus/Field Safe.
- [x] Slicer Mutate: five operations, operation variants, treatment toggles,
  empty-slot locks, background generation, Play Through, and WAV export.
- [x] Slicer shared file/analysis/mutation/project workers on macOS and Windows;
  preserve Project/Link/Embed and both fixed-output variants.
- [x] Ambi Grain: original W-channel waveform, source/jitter/grain markers,
  transport, order/mode/window menus, conditional sources, grain/density limits,
  shared field normalization, file recall, and preset save/load.
- [x] Shared title font, canvas menus, centered control text, resize/lifecycle,
  bundled font/license/fallback, UTF-8 file dialogs, automation edit gestures.
- [x] macOS + Windows builds, focused DSP/state/GUI interaction tests, and
  reference captures of all four Slicer pages plus Ambi Grain.

## Verification (2026-09-09)

- macOS portable builds: `build-clap-sample-vstgui-fidelity`.
- Windows x86-64 cross-builds: `build-clap-windows-cross`. PE imports contain
  Windows system/UCRT libraries, not separately deployed MinGW runtime DLLs.
- Both native Cocoa fallback targets also build with portable GUI disabled
  in `build-clap-canvas-cocoa-parity`.
- 38 selected DSP, CLAP, GUI, automation-queue, and Sample-family regression
  tests passed. This includes the actual Slicer and Ambi Grain canvas actions
  and hosted lifecycle/resizing checks for Slicer 16, Slicer 2, and Ambi Grain.
- Slicer's optional native text-entry test passed at 150% scale with desktop
  access. Enable it with `S3G_CANVAS_NATIVE_TEXT_TEST=1`; a headless/sandboxed
  AppKit session cannot instantiate the native text control reliably.
- Ambi Grain tests cover Unicode import without an open editor, latest-load
  selection, failed-load preservation, recall overriding a pending import,
  and destruction while a reader is active. Completion is published before
  requesting the host main-thread callback.
- Fira Code and its license are present in each macOS bundle and the Windows
  sibling `Resources/Fonts` directories. Font fallback uses the existing
  family foundation.
- Five reference captures were generated through the existing documentation
  fixture workflow, including the Slicer mutation exercise. The private
  documentation extension is exposed only when
  `S3G_GUI_DOCUMENTATION_CAPTURE=1`; ordinary hosts do not see it.

Reproduce reference captures without overwriting the original Cocoa images:

```sh
scripts/generate-plugin-screenshots.py --no-build \
  --build-dir build-clap-sample-vstgui-fidelity \
  --output-dir build-clap-sample-vstgui-fidelity/canvas-reference \
  --scale 2 breakbeat-slicer ambi-grain-processor
```

## Remaining host acceptance

The GUI migration builds were subsequently installed on macOS at the user's
request. The later Ambi Grain storage update described below was also installed
on macOS on 2026-09-09 after backing up the prior bundle. Hosted Windows
acceptance remains a separate machine test;
cross-compilation is not runtime verification.
On Windows, check both Slicer variants and Ambi Grain in REAPER, including
multi-file load/drop, Unicode paths, preset filtering/recall, Slicer WAV export
and project assets, automation recording, numeric entry, and 65–200% resizing.
The ports preserve the Cocoa drawing geometry and behavior; platform text
rasterization is not claimed to be pixel-identical.

## Ambi Grain storage follow-up (2026-09-09)

This user-approved functional addition extends the migration without moving
the original waveform, transport, or engine controls. Both VSTGUI and retained
Cocoa views now expose STORE PROJECT/LINK/EMBED, the locator, status, and RELINK
in the unused space below the waveform.

- New instances default to PROJECT. Shared `s3g_sample_storage.h` supplies
  content-verified collection, project-relative locators, exact owning-project
  discovery, and balanced `file_in_project_ex2` registration/rename callbacks.
- Worker reads/copies/materializes audio without calling project APIs off the
  main thread. Publication rechecks the project location to handle Save As
  during collection. Pending collection does not interrupt playback.
- State version 2 retains the exact version-1 parameter-prefix ABI and adds a
  bounded length-prefixed UTF-8 locator, explicit storage metadata, and optional
  interleaved PCM. Existing non-empty v1 locators recall as LINK; empty legacy
  states unload audio and default to PROJECT. New states need an updated plugin.
- EMBED preserves PCM and common-field normalization metadata exactly, with a
  1 GiB cap. Pending PROJECT keeps pathless/embedded audio in state until it has
  project media. Conversion to PROJECT can materialize the embedded field even
  after the original file is gone. Source-less conversion to LINK is refused.
- Missing-file recall retains the locator and settings, clears unrelated old
  audio, and shows a relink message. RELINK preserves the recalled parameters.
  Invalid/truncated states leave live state untouched. Preset saves stage a
  complete sibling file before replacement, preserving an old preset on error.
- `tests/ambi_grain_storage_smoke.inc`, exercised by the existing Ambi Grain
  canvas test, covers legacy, long Unicode/missing locators, all STORE menu
  selections, 4/9/16-channel embedding, unsafe/oversized/truncated state rejection,
  project collection/deduplication/relocation, late host context, rename callbacks,
  storage changes during loading, pending collection, and registration balance.

References are captured separately under
`build-clap-sample-vstgui-fidelity/ambi-storage-reference`, leaving the original
Cocoa reference image unchanged. The reference fixture deliberately exercises
legacy LINK recall; new instances still default to PROJECT. Actual REAPER Save
As/media-copy acceptance on Windows remains a machine test, not a claim made
from the mocked host or successful cross-build.
