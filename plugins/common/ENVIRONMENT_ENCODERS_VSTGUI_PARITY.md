# Environmental encoders: Cocoa to VSTGUI parity

Scope: Ambi Encoder Wind 64, Water 64, Insect 64, Cryosphere 64 and
Pyrosphere 64. These remain zero-input, 64-output instruments. No DSP redesign.

The retained Cocoa editors, `docs/ambi-encoder-{wind,water,insect,cryosphere,pyrosphere}.html`,
`docs/parameter-surface.html`, and their `docs/assets/plugin-guis` images are
the adaptation references. Drawing geometry, layering, hit rectangles, camera
projection and interaction algorithms are ported directly, not replaced by
generic visualizations. Shared approved changes are Fira Code, family grays and
control styling, uppercase titles after lowercase `s3g`, portable dialogs and
proportional 65–200% resizing. Cross-platform font rasterization is not claimed
to be pixel-identical.

Custom dropdowns must retain the original between-item rules, framing and
selection treatment; see [VSTGUI_MENU_PARITY.md](VSTGUI_MENU_PARITY.md) for the
shared regression checks.

## Retained features

- All five: animated FIELD sources, voice selection/readouts, TOP/SIDE/3/4
  camera views, rotation and zoom; all factory presets, RANDOM, custom presets,
  original slider ranges and reset behavior, custom-drawn menus and output
  gain preservation when choosing a scene.
- SURF: original Voronoi cells, target and active cursors, PLAY/EDIT, ON/bypass,
  ADD/CAP/DEL, cell selection and dragging, curve/focus/glide, and the separate
  synchronized POP window. Single-scene preset files retain their existing
  meaning; complete maps are stored in project state.
- Wind: source energy and gust graphics, motion, projection and environment.
- Water: source energy/event graphics, water process and spatial controls.
- Insect: regime-dependent panels, labels and control visibility; production
  method markers and call activity; field listener HEARD/GAIN meters and modes.
- Cryosphere: ice-skin contact cursor and per-voice contact weighting, physical
  field and entity-score controls/readouts.
- Pyrosphere: fire-field source/gust visualization and entity-score controls
  and readouts.

The original plugin IDs, parameter metadata, channel layouts and state formats
are unchanged. Original Cocoa implementations remain behind the Mac build
switch. Their existing single-preset SURF semantics are retained, including
Water's bypass-on-custom-load behavior.

## Implementation and build

Enable `S3G_ENABLE_ENVIRONMENT_ENCODERS_VSTGUI_ON_MACOS=ON` for the portable Mac
editors. With that switch off, Mac builds use the retained Cocoa editors.
Windows uses the portable editors when VSTGUI is available.

The five `s3g_environment_encoder_*_canvas.inc` files contain the literal
adaptations. `s3g_environment_encoder_editor.inc` supplies shared editor
lifecycle, native preset dialogs, gestures and SURF pop-outs; drawing helpers
reuse the existing family foundation. Fira Code and its OFL license use the
existing bundle/adjacent-Resources packaging and missing-font fallback.
Preset I/O uses the shared UTF-8/native path helpers, including Windows UTF-16.

Audio owns mutable processor state. Insect's portable wrapper now uses ordered,
bounded parameter/surface/state queues and published control snapshots, as the
other environmental wrappers already did. This does not change its synthesis.
Both editor windows retain the latest pending SURF revision so rapid ADD/CAP
or cross-window edits cannot silently rebase on an older audio snapshot.
Automation drags have balanced begin/end gestures, with queue space reserved
for gesture completion. Initialized field geometry is published on first open,
even before audio activation.

Pop-outs are owned by their main host window rather than globally topmost.
The shared Windows auxiliary-window helper now establishes and clears that
native ownership when showing and hiding.

Build targets:

```sh
cmake --build build-clap-sample-vstgui-fidelity --target \
  s3g_ambi_wind_encoder_clap s3g_ambi_water_encoder_clap \
  s3g_ambi_insect_encoder_clap s3g_ambi_cryosphere_encoder_clap \
  s3g_ambi_pyrosphere_encoder_clap -j 6

cmake --build build-clap-windows-cross --target \
  s3g_ambi_wind_encoder_clap s3g_ambi_water_encoder_clap \
  s3g_ambi_insect_encoder_clap s3g_ambi_cryosphere_encoder_clap \
  s3g_ambi_pyrosphere_encoder_clap -j 6

cmake -P scripts/package-windows-environment-encoders.cmake
```

The packaging script validates x64 PE files, `clap_entry` exports and absence
of external MinGW runtime DLLs, strips only staged copies, includes resources
and notices, and produces a timestamped folder, checksums and ZIP in `dist`.
It does not install anything or overwrite an existing package.

## Verified 2026-09-11

- All five macOS arm64 VSTGUI, retained Cocoa and Windows x64 cross-builds succeeded.
- Five native canvas suites passed. They exercise first-open geometry; every
  factory preset and custom menu choice; slider hit maps and resulting values;
  camera views; SURF edits and X/Y orientation; balanced gestures; rapid edits
  before host flush; synchronized pop-outs; Unicode preset paths; chunked state
  roundtrips and truncated-state rejection; queue backpressure; live telemetry
  and finite audio; parenting, 65/100/150/200% resizing, hide and repeated destroy.
- The existing native CLAP GUI host passed for all five. Cocoa-only KVC/selector
  interaction checks remain for Cocoa; the portable canvases have their own
  direct interaction suites rather than pretending to expose Cocoa internals.
- Independent Cocoa/VSTGUI binaries matched parameter metadata and audio for
  four deterministic automated scenes per plugin, including a populated SURF
  state. Maximum observed sample difference was **0**, with a `1e-6` tolerance.
  Bidirectional state exchange and rejected truncated loads also passed.
- CLAP validator: **105 tests run; 80 passed, 0 failed, 25 skipped, 0 warnings**.
- Native PNG/PDF reference captures were generated and inspected against the
  original interfaces. The Windows ZIP integrity and staged SHA-256 sums passed.

Regression sources are `tests/environment_encoder_canvas_smoke.cpp` and
`tests/environment_encoder_clap_parity_smoke.cpp`. Canvas tests are registered
by `cmake/S3GEnvironmentEncoderTests.cmake`:

```sh
ctest --test-dir build-clap-sample-vstgui-fidelity \
  -R '^s3g_environment_.*_canvas_smoke$' --output-on-failure
```

Set `S3G_ENVIRONMENT_CAPTURE_DIR` to an existing writable capture directory for
PNG references and populated state fixtures. The binary comparator accepts
`<cocoa-binary> <vstgui-binary> <plugin-id> [populated-surface-state]`.

The five Mac VSTGUI bundles were installed on 2026-09-11 in
`~/Library/Audio/Plug-Ins/CLAP/s3g-dsp/`, under their canonical installed names.
Installed bundle contents matched the rebuilt sources and code-signature checks
passed. Only these five bundles were replaced; the main full-family install
receipt and unrelated plugins were left unchanged. Previous versions are
recoverable from `~/Library/Application Support/s3g-dsp/CLAP Backups/environment-vstgui-20260911.tXcyBX/`.

Interactive Mac REAPER acceptance and actual Windows REAPER testing remain
required, particularly native dialogs, Unicode paths, pop-out ownership,
project recall and display scaling. See `ENVIRONMENT_ENCODERS_WINDOWS_README.txt`
for the manual Windows test checklist.
