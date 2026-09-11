# Input encoders: Cocoa to VSTGUI parity

Scope: Cloud 64, Path 64, Ray 64, Ray Bilocation 64. No DSP redesign.

Authoritative references are the original Cocoa implementations retained behind
the build switch, their `docs/ambi-encoder-*.html` manuals, and the corresponding
`docs/assets/plugin-guis` screenshots. Port coordinates, paths, colors, drawing
order, hit rectangles and interaction algorithms directly. Do not substitute
generic visualizations. Approved shared changes: Fira Code, family typography
and controls, uppercase titles after lowercase `s3g`, portable dialogs, and
proportional 65–200% window resizing.

Adaptation coverage:

- Cloud: all per-cloud automation and selected-cloud aliases; five shapes and
  forces; animated source points, cloud labels, camera and zoom; INIT/RANDOM,
  presets and original v3/v4 state.
- Path: original projected trajectories, colored points and moving sources;
  EDIT/PLAY, add/drag/delete/clear/regenerate; source assignment, free/host sync,
  playback/interpolation modes, rate curve; JSON/SVG workflows; v3/v4 state.
- Ray: original top/side room geometry, cells, bounce paths and arrival marks;
  SRC/LIS dragging, field-listen modes, all 19 Atlas choices, imported fields
  embedded in state and legacy coordinate conventions.
- Bilocation: both original maps and elevation views, transition/presence
  graphic, linked source mappings, eight curated pairs, independent LOAD A/B,
  room characters, state containing both fields.
- All: untouched first-open before audio activation, show/hide/reopen,
  resize 65/100/200%, balanced automation gestures, transactional failed loads,
  missing font fallback and license/resource packaging on both platforms.

Keep Cocoa and VSTGUI builds separate for binary/audio and native screenshot
comparison. Windows cross-compilation is not Windows host runtime testing.

## Implementation

Enable `S3G_ENABLE_INPUT_ENCODERS_VSTGUI_ON_MACOS=ON` for the portable Mac
editors. Windows uses them when VSTGUI is available. The original Cocoa editors
remain buildable with the Mac option off; plug-in IDs, parameter IDs/ranges,
channel layouts and saved-state formats are unchanged.

The four `s3g_input_encoder_*_canvas.inc` files retain the original drawing and
interaction algorithms. They use the existing shared lifecycle, fonts, menus,
proportional resizing, parameter gesture queue and native file dialogs.
Path and Ray file readers use VSTGUI's already-pinned RapidJSON, with its
license packaged alongside the font license. Both Ray products retain the
19-entry bundled Atlas; Bilocation retains the eight original contrast pairs.
Imported Ray fields are embedded in projects and presets, not dependent on
their original file paths. Path retains its original lossless XYZ/time JSON
and practical numeric-pair SVG import/export, not a new general SVG renderer.

Audio owns mutable processors. Editors use atomic parameter values and their
own display models. Cloud/Path source positions use a bounded audio-to-GUI
snapshot; edited paths use a bounded GUI-to-audio snapshot. Ray room metadata
is separate from the audio runtime, and its tail query uses immutable room
descriptors plus atomic controls rather than reading mutable DSP parameters.

Deliberate correctness fixes discovered during adaptation:

- Activating Cloud/Path no longer erases custom geometry loaded or edited
  before playback starts.
- Failed project/preset imports do not partially replace parameters or fields.
  Non-finite binary states are rejected; legacy empty Ray states restore the
  built-in room(s), including Bilocation's original far-room character.
- Path's value column uses available right-side space to retain readable
  fractional rates and numeric beat divisions with Fira Code.
- Headerless map panels retain their original white top rules; title status
  text cannot overlap the RANDOM button.

## Verified 2026-09-10

- All four macOS arm64 VSTGUI CLAP builds and retained Cocoa builds succeeded.
- All four Windows x64 VSTGUI CLAP cross-builds succeeded; `clap_entry` exports
  are present and DLL imports are Windows system/UCRT dependencies.
- Native Mac GUI lifecycle tests passed for all four: first creation,
  parenting, show/hide/reopen and proportional 65–200% resizing.
- Four registered canvas regression suites passed. These exercise first-open
  graphics, custom menus, camera controls, path edits and file roundtrips,
  all Atlas entries and pairs, preset OUT preservation, balanced gestures,
  malformed/truncated state rejection, every retained legacy version,
  finite playback audio and edits concurrent with audio processing.
- Binary Cocoa/VSTGUI metadata, bidirectional state and audio comparisons
  passed for all four (audio comparison tolerance `1e-6`).
- CLAP validator: **84 tests run; 64 passed, 0 failed, 20 skipped, 0 warnings**.
- A temporary Cloud bundle without its font opened and resized successfully;
  the shared loader reported its Menlo fallback. Production bundles were not
  modified for this check.
- Bundled fonts, font licenses, Ray Atlas files/manifest and applicable
  RapidJSON licenses were compared with their sources on both platforms.
- Native reference captures were inspected against the original manual images.

Regression sources: `tests/input_encoder_canvas_smoke.cpp`,
`tests/input_encoder_clap_parity_smoke.cpp` and the existing
`s3g_decoder_family_gui_smoke` host. Canvas suites are registered by
`cmake/S3GInputEncoderTests.cmake`:

```sh
ctest --test-dir build-clap-sample-vstgui-fidelity \
  -R '^s3g_input_encoder_.*_canvas_smoke$' --output-on-failure
```

These builds have **not been installed**. Interactive Mac REAPER acceptance
and Windows REAPER testing (especially dialogs, Unicode filenames, Atlas/pair
recall and high-DPI/resizing) remain the next handoff checks. Automated parity
checks are not a claim of pixel-identical cross-platform font rasterization.
