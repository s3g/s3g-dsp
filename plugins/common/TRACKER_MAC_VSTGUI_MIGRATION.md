# Tracker: Mac-first VSTGUI migration

Status: **opt-in drawing pilot, not a complete editor conversion**. Windows
CLAP remains deferred. Do not add Tracker to a Windows package or the list of
finished VSTGUI editors on the strength of this pilot.

## Scope of the first build

- `TrackerViewState`, `WorkspaceCallbacks` and the audio-device value type now
  live in ordinary C++ headers under `tracker/include/s3g/tracker`.
- The Tracker grid, frozen row-number gutter, value/gate/sequence envelope and
  its playback overlay submit a portable display list to VSTGUI.
- The original draw routines still determine every cell, color, label,
  selection, mute overlay, read head and envelope point. This is an adaptation
  of those routines, not a replacement spreadsheet or a raster screenshot.
- A Mac adapter creates a VSTGUI drawing context for the current native view.
  The native views remain responsible for input, scrolling, inline editing,
  menus, accessibility and their existing undo/command callbacks.
- The existing toolbar, page strip, other nine pages, detached windows, dialogs
  and project storage are still Cocoa. No VSTGUI `CFrame` owns the workspace yet.

This intentional bridge separates drawing changes from application-scale input
changes. It is only used by the four audited draw paths. The standalone Mac app
and builds with the switch off continue using Cocoa drawing.

## Preserved contracts

- Plugin ID/version, MIDI ports/channels, scheduler/publication rules and
  `.s3gt` / `.s3gpack` formats are unchanged. No sampler/audio-device feature is
  introduced into the MIDI-only CLAP.
- The pilot's main CLAP window now scales the complete 1320 x 860 logical
  workspace proportionally from 65% (858 x 559) to 200% (2640 x 1720).
  Initial size fits the main display when necessary. Resizing enlarges/shrinks
  fonts, controls, graphics and canvas dropdowns together; it no longer adds
  logical workspace. The Cocoa-only reference retains responsive resizing.
- Grid magnification remains a separate 55–180% control with 100% reset.
- Detached tool windows keep their original independent responsive layouts;
  they do not inherit the main window's zoom until reattached.
- Night Tracker colors and exact resolved font faces are retained. IBM Plex
  Mono is bundled; the Cocoa code's existing monospaced system fallback for
  unresolved faces is preserved, not silently replaced with Helvetica.
- VSTGUI's Mac color-space conversion, stroked-rectangle semantics and exact
  face lookup are handled in the adapter. Native text baseline metrics are
  preserved, including fixed-height rows using AppKit's UI font fallback.
- The IBM font OFL is now packaged at `Contents/Resources/Fonts/OFL.txt`.

## Building and checking

Use an existing configured Mac CLAP/VSTGUI build, with Tracker enabled:

```sh
cmake -S . -B build-clap-sample-vstgui-fidelity \
  -DS3G_BUILD_TRACKER_PREVIEW=ON \
  -DS3G_ENABLE_TRACKER_VSTGUI_PILOT_ON_MACOS=ON
cmake --build build-clap-sample-vstgui-fidelity \
  --target s3g_tracker_clap s3g_tracker_clap_smoke -j 4
ctest --test-dir build-clap-sample-vstgui-fidelity \
  --output-on-failure -R '^s3g_tracker_clap_smoke$'
```

The switch defaults OFF. ON requires the existing portable GUI dependency;
the standard CMake helper supplies linking, resources and bundle signing.
Keep a separate OFF build as the reference, and never load both variants into
one test process (they contain the same Objective-C class names/plugin ID).

The smoke test's `S3G_TRACKER_EXPECT_VSTGUI=1` check requires all four drawing
surfaces to have used VSTGUI; a silent Cocoa fallback is a failure. CTest sets
this automatically for the pilot configuration. To capture a parity matrix,
also set `S3G_TRACKER_PILOT_CAPTURE_DIR` to a build-artifact directory. This
writes compact/expanded 100%, expanded 55%/180%, and scrolled 180% PDFs. The
existing `S3G_GUI_SMOKE_PDF_DIR` captures Tracker, Song, Geometry and Warps
with the documentation/playback fixture. Run each capture against both bundles.
Use `1320 860` (after the plugin ID) for paired captures: other outer sizes
intentionally differ from the old responsive editor now.

The smoke also checks whole-interface sizing, stable integer size negotiation,
both host resize orderings, font/control geometry and hit testing, canvas-menu
selection, and double-click inline editing with nested grid zoom at 55%, 100%
and 180%. Set `S3G_TRACKER_SCALE_CAPTURE_DIR` to write whole-interface PDFs at
65%, 100%, 150% and 200%.

The pilot uses a native magnifying viewport around the original logical
workspace. AppKit's scroll-view magnification preserves the Auto Layout tree
and field-editor/event coordinates; manually transforming the page's own
frame/bounds does not. `set_scale` correctly returns false for Cocoa, whose
logical points already account for Retina DPI; user zoom uses `set_size`.
Help explicitly uses TextKit 1 in the pilot. Automatic TextKit 2 viewport
layout could repeatedly invalidate the host window when opening Help at a
fractional outer scale (reproduced at 900 x 586), eventually raising an AppKit
layout-loop exception. The content, typography, selection and scrolling remain
native. The smoke checks the text engine and supports paired Help captures
through `S3G_TRACKER_HELP_CAPTURE_DIR`.

Compare the paired zoom captures with:

```sh
ruby scripts/compare-tracker-pilot-captures.rb COCOA_CAPTURE_DIR PILOT_CAPTURE_DIR
```

This checks every visible grid word's position, font-face inventory and the
whole-page raster difference (not just words present in both files). The PDF fixture makes
the existing on-screen viewport clips explicit, since AppKit's layer-backed
scroll-view clips otherwise leak into neighboring panels during PDF drawing.
Each PDF has a `.pdf.json` sidecar with the actual viewport rectangle; the text
checker uses it to exclude glyphs hidden by a PDF clipping path, since Poppler
still reports those invisible glyphs. Whole-page raster comparison is unmasked.

## Validation caveats

Verified on the development Mac, 2026-09-11:

- The scaling refinement passes the GUI/MIDI smoke at 858 x 559, 1320 x 860
  and 2640 x 1720. The seven-size resize matrix checks lower/upper clamping,
  non-proportional requests, repeated adjustment without pixel drift, both
  resize call orderings, and editing with all three tested inner grid zooms.
  The rebuilt Cocoa-only configuration also passes its native-size smoke.
- After the Help text-engine fix, 20 consecutive registered GUI/MIDI runs
  pass, including close/reopen and preserving non-default inner grid zoom.
  Paired Help PDFs contain the same 242 visible words and font faces; the
  largest text-coordinate difference is 1 point (a separator rule). Help is
  not included in the strict grid pixel-parity claim: the old TextKit 2 PDF
  has a white text background while TextKit 1 renders the configured dark
  background. Its paired captures were inspected separately.
- Both Cocoa and pilot CLAP GUI/MIDI smoke runs pass at 1320 x 860; the
  registered pilot smoke requests 900 x 620, negotiated to 900 x 586.
- 29 core/editor/Song-roundtrip tests pass, including the new drawing contract.
- All five paired zoom/scroll captures pass. Every visible grid word matches
  within 0.01 point, font inventories match, and normalized whole-page pixel
  RMSE is 0.00032–0.00489 (threshold 0.01). This is not a pixel-identical claim.
  This comparison was rerun at 100% outer scale after adding the magnifying
  viewport; results are in the `scaling-parity` artifact folders.
- Rebuilding with VSTGUI disabled preserves a pixel-identical Tracker capture
  against the pre-migration Cocoa bundle.
- Mac arm64 bundle signing verifies. The initial drawing pilot was installed
  before this scaling refinement. Building does not replace that installation.
  The current scaling build is
  `build-clap-sample-vstgui-fidelity/plugins/clap_tracker/s3g_tracker.clap`;
  `build-tracker-vstgui-pilot/s3g_tracker.clap` is the earlier drawing pilot.

The broader native `s3g_tracker_workspace_layout_appkit_tests` currently has
five failing assertions: Warps playback redraw, Reshape profile redraw, two
Geometry ring/mute expectations and Geometry view dispatch. All five reproduce
when linking the test with the **unmodified HEAD workspace source**; the pilot
does not enable VSTGUI in that native test target. They are not silently marked
passing or fixed as part of the drawing migration.

At 760 x 620 the CLAP smoke test's MIDI-statistics/HOST-BPM header-separation
assertion fails in the original responsive Cocoa editor. The scaling pilot
does not use that narrow logical layout: its full 1320 x 860 surface scales
down, preserving the header geometry at its 858 x 559 minimum.
The old Pattern Bank test's positional `SongRow` initializers were updated to
named field assignments so it builds with the already-existing row-ID field;
no production Song logic was changed.

## Next migration gates

1. Review this drawing pilot in REAPER before replacing more of the editor.
2. Extract grid input/presentation logic from Objective-C responders into C++
   commands, retaining cell grammars, selections, drag operations, keyboard
   focus, read-only Song follow and undo grouping. Then move the Tracker page
   into a VSTGUI host with the same whole-interface and separate grid zoom.
3. Port one complete detachable tool (Warps is a contained candidate), including
   its controls, editing and playback overlays. Verify detach/reattach, focus,
   ordering and close/reopen in REAPER.
4. Continue page groups: Song; Geometry/Bursts; Phrases/Assemble; Reshape;
   Console/Help. Keep the native service adapters until each replacement has
   passed visual and interaction comparison.
5. Only after Mac editor parity, revisit Windows paths/durable atomic saves,
   dialogs, clipboard, keyboard conventions, detached-window ownership and
   host validation. The current C++ core does not by itself make a Windows port.

No installed plugin is replaced by building this pilot.
