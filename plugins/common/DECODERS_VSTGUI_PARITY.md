# Ambisonic Decoder VSTGUI parity

Implemented: Speaker 64, Object 64, Adaptive 64, Stereo 2, Head 2,
and Sub 8. Built and locally verified on 2026-09-10; installation and
real-host acceptance on macOS/Windows remain separate steps.

## Reference contract

The retained Cocoa source and `docs/ambisonic-decoders.html` plus the six
`docs/ambi-decoder-*.html` pages are authoritative. Original Cocoa captures
are in `build-clap-canvas-cocoa-parity/decoder-reference/masters`.

| Editor | Required parity |
| --- | --- |
| Speaker | FIELD/MIXER/MAP; all 14 layout entries; exact custom AED fields; selected speaker; 16-fader pages; exact gain entry; mute/solo; ALLRAD real/gap/fold topology and fallback; probe map; original meshes; camera and zoom; state v4/v5. |
| Object | All layouts and three panning methods; content-derived direction/cue visualization; field/object branch controls; camera/zoom and project recall. No invented custom-position editor. |
| Adaptive | All layouts; confidence/transient/focus visualization; diffuse/sharp controls; camera/zoom and project recall. No invented custom-position editor. |
| Stereo | Ten pickup patterns including MS labels and dashed negative lobes; virtual speaker fields; AB capsule spacing; mic elevation; view rotation; output meters and original routing. |
| Head | Synthetic head/ear geometry; direct/virtual-field paths; binaural/transaural controls; orientation and ear width; camera/zoom/wheel; mode-dependent controls and meters. No external HRTF importer. |
| Sub | Original numbered sub-ring geometry; 0OA–7OA; 1–8 sub feeds; cutoff/width/output; bypass silences output; title preset workflow. |

Shared intentional changes: Fira Code, family grays, lowercase `s3g` plus
uppercase titles, aligned control text and centered handles, custom menus,
embedded font/license, UTF-8 dialogs, and proportional 65–200% resizing.
DSP algorithms, bus widths, parameter identifiers and state formats must stay
compatible. INIT and user preset recall preserve OUT; host recall restores it.

## Implementation boundaries

The six `s3g_decoder_*_canvas.inc` editors retain Cocoa drawing geometry,
projection equations, topology, menu mappings, and pointer interactions.
`s3g_decoder_drawing.h` supplies the approved shared controls and retains
Generic RGB-to-sRGB conversion and small numbered-node text.

Speaker retains its existing worker, bounded commands, published snapshots,
indexed mixer edits, and 20 Hz refresh. Exact AED and gain fields use the
shared native numeric editor. Object, Adaptive, Stereo, Head, and Sub publish
scalar UI values atomically and apply them on the processing side, with
balanced host edit gestures. Object/Adaptive render a separate geometry model;
CUSTOM retains the preceding layout's speaker count, including rapid UI edits.
Immutable state records preserve unexposed legacy fields without concurrently
mutating the processing model. DSP algorithms and original float/double support
are unchanged.

The retained Cocoa option stays available. Enable
`S3G_ENABLE_DECODERS_VSTGUI_ON_MACOS=ON` for these Mac ports; Windows selects
them when portable VSTGUI is enabled. All six use `s3g_enable_vstgui_gui` for
linking, resources, font fallback, lifecycle, and proportional resizing.

## Verification

- All six Mac VSTGUI, Mac Cocoa fallback, and Windows x64 targets build.
- Six `s3g_decoder_*_canvas_smoke` tests pass: original menu/slider mappings,
  double-click defaults, cameras, pickup modes, mixer pages, exact numeric
  fields, Unicode presets, OUT preservation, state recall, invalid inputs,
  gesture balance/backpressure, and host automation precedence.
- Object/Adaptive first-open regression: the GUI-only model must call
  `prepare()` before its initial `setParams()`. Otherwise the unchanged
  Sphere 24 default leaves all speakers at their placeholder coordinates.
  Tests reproduce the failure without this initialization and verify all 24
  coordinates with it, before/after activation, on editor reopening, after
  unchanged refreshes, and when recalling untouched default state. These checks
  do not change layouts to initialize the display. Before/after PNGs are in
  `decoder-reference/first-open-before` and `first-open-after`.
- Speaker v4 and Object v1/v2 legacy chunks load successfully; current state
  formats are exchanged in both directions with the retained Cocoa binaries.
- `s3g_decoder_clap_parity_smoke` passes for all six: unchanged parameter
  metadata, chunked/transactional state handling, and audio comparisons over
  three scenes. Double-precision comparisons cover Speaker and Stereo, the
  two original double-capable decoders.
- `audit_gui_decoder_family` passes for all six: native GUI lifecycle and
  proportional sizing. Matched documentation captures were visually compared
  against Cocoa, with additional FIELD/MIXER/MAP and pickup-mode captures.
- CLAP validator 0.3.2 passes for all six. Eight earlier Ambi Effect canvas
  tests also pass after the small shared drawing-helper extension.
- All six Mac bundles pass signature verification. Both platform builds
  contain byte-identical Fira Code font/license resources. Windows binaries
  export `clap_entry` and have no external MinGW C++/thread runtime dependency.

Artifacts are under `build-clap-sample-vstgui-fidelity/decoder-reference`:
`masters/` (native PDFs), `interactions/` (canvas PNGs), `validator.json`, and
`bundles.tsv` (the six-plugin manifest). Cocoa comparison masters are under
`build-clap-canvas-cocoa-parity/decoder-reference/masters`.

To rerun interaction tests, build the six `s3g_decoder_*_canvas_smoke` targets,
then run:

```sh
S3G_DECODER_NATIVE_FIELDS=1 ctest --test-dir build-clap-sample-vstgui-fidelity \
  -R '^s3g_decoder_.*_canvas_smoke$' --output-on-failure
```

The native tests need the macOS window server. Windows REAPER has not been run
on this machine. Manual acceptance should exercise project/preset recall,
65–200% scaling, field rotation/zoom, all four Speaker mixer pages, typed
coordinates/gains, ALLRAD map/mute/solo, Head's two output paths, Stereo pickup
patterns, and Sub bypass. Do not treat cross-compilation as Windows host testing.
