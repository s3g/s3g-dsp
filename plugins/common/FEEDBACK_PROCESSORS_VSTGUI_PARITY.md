# Errant, Feedback Shift, Fissure and Fault: VSTGUI parity

Source of truth: the retained Cocoa drawing order, layout constants and event
handlers, together with `docs/processor-errant.html`,
`docs/processor-feedback-shift.html`, `docs/processor-fissure.html` and
`docs/processor-fault.html`. Older documentation screenshots do not override
controls present in the current Cocoa implementation.

## Preserved surfaces

- Errant: both columns, grammar/ancestry and bass/performance controls, MIDI
  receive, two live family-archive traces, pitch/status text and GENERATE.
- Feedback Shift: PATCH/INSERT/ECOLOGY/AUX; editable A/B matrices, selection,
  second-click erase, Option/Alt negative route, polarity and precise gain;
  A/B random/copy/morph; all node insert categories/effects and dependent
  controls, node-local RND, engine banks, ecology/governor and live readouts.
- Fissure: original matrix geometry/colors and activity overlays, route gain,
  cut masks, six topology choices, four scene stores/recalls and continuous
  morph, all eight selectable/strikeable physical objects, both fracture pucks
  with spring/latch behavior, Grab/Repeat and original performance feedback.
- Fault: all eight live traces, status and source interpretation, SOUND/BASS
  LAB/MOD LAB, fourteen factory patches, codec and curated algorithm choices,
  all three modulation operators, envelope graph/handles/crosshair, MIDI/free
  performance, output projection/rotation, generation/mutation and undo.

The original coordinates and hit maps are retained. Shared Fira Code, grayscale,
uppercase titles, centered control text and separated custom menus are used.
Long labels and complete prefixed numeric/unit readouts fit their original
rectangles. The shared CLAP lifecycle supplies proportional 65–200% resizing,
resource lookup and missing-font fallback on both platforms.

## Platform and state adaptation

All plugin IDs, parameter metadata and successful project-state formats remain
unchanged. The Cocoa implementations remain available when the Mac switch is
OFF. The signal-processing algorithms are unchanged.

GUI parameter changes use ordered audio-owner queues and balanced edit gestures;
host output-event rejection retains pending events for retry. Compound patch
changes use a larger portable queue. Fault no longer draws from mutable DSP
objects: it uses bounded atomic POD publication, atomic waveform samples and
queued source/patch handoff. Source decoding happens before handoff on the GUI
thread. Fissure publishes its four scene tables for race-free GUI state saves.

Custom preset files decode in an isolated inactive instance before updating
the running plugin. Errant, Fissure and Fault preserve OUT as Cocoa did;
Feedback Shift retains the Cocoa file helper's complete-patch restoration.
Fissure queues a complete validated state rather than replaying scene/morph
actions, which would otherwise overwrite live edits. GUI close releases held
Repeat/puck gestures. Fault's legacy defaults are now applied only after a
complete supported state has been read: rejected/truncated files no longer
reset performance/envelope settings. This correction applies to Cocoa too.

Fault's Cocoa Open Any accessory checkbox has a portable equivalent: a custom
s3g menu offers WAVE decoding or raw-byte interpretation before the native file
dialog. Decode mode retains the original fallback to raw bytes. Paths use the
shared UTF-8/UTF-16 boundary and UTF-8 filesystem conversion. Imported sources
remain path-referenced, not embedded; missing-file status is preserved.

## Build and verification

Mac option: `S3G_ENABLE_FEEDBACK_PROCESSORS_VSTGUI_ON_MACOS=ON`.
Windows: enabled by the existing VSTGUI option, including when
`S3G_BUILD_FUTURE_COMPONENTS=OFF`.

Targets: `s3g_processor_errant_clap`, `s3g_feedback_shift_clap`,
`s3g_processor_fissure_clap`, `s3g_psd_raw_field_clap`.
Tests: `s3g_feedback_{errant,shift,fissure,fault}_canvas_smoke`.
Set `S3G_FEEDBACK_CAPTURE_DIR` to produce first-open/page/menu/insert/live PNGs.

The direct tests cover parameter defaults, visible sliders, factory/menu
choices, Feedback Shift insert categories/effects, all eighteen Fault menus,
scene store/recall/morph, complete Fissure live/state restoration, pucks and
Repeat release, Unicode presets and WAVE import/interpretation recall, chunked
streams, rejected-event retry, gesture balance, native parenting and resizing.
Concurrent snapshot tests reject mixed-generation publication. Separate native
bundle tests exercise the public CLAP GUI lifecycle; Cocoa KVC-only checks stay
on Cocoa, while the direct tests exercise the portable controls.

Verification, 2026-09-11:

- Four Mac bundles and Windows x64 CLAP targets compile.
- Direct GUI tests, native bundle lifecycle tests and CLAP validator pass.
- Original four DSP and four CLAP smoke tests pass; CLAP tests load the new
  portable binaries, including Fault's imported-WAVE regression suite.
- Independently built Cocoa and VSTGUI modules match parameter metadata,
  state round-trips and three deterministic audio scenes in float and double:
  maximum sample difference is zero. Fault's truncated-state regression is
  included, with the loader correction in both independently built variants.

Mac installation is separate; follow the maintained
[local-build installation guide](../../docs/building-from-source.html#install-local)
for the full collection and back up existing bundles before replacing them.
Windows packaging:
`cmake -P scripts/package-windows-feedback-processors.cmake` verifies PE x64,
CLAP export and static MinGW runtime linkage, then includes Fira Code, licenses,
manual acceptance instructions and SHA-256 hashes.

Actual Windows REAPER drawing, audio-device routing and native dialog behavior
still require the Windows host pass. Cross-compilation and Mac tests are not
represented as Windows runtime testing. This batch's build does not install
these four new Mac bundles automatically.
