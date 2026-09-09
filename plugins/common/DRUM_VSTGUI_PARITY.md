# Drum-family VSTGUI migration

## Scope and reference

The instrument rollout now covers **all ten Drum instruments**. The initial
Kick / Hi-Hat checkpoint established the shared canvas. Current Cocoa
implementations, the [family guide](../../docs/drums.html),
[Kick guide](../../docs/drum-kick.html), and
[Hi-Hat guide](../../docs/drum-hi-hat.html) are the specification. Existing doc
screenshots predate MIDI Receive; fresh Cocoa captures include that control.
Do not overwrite the original documentation images during comparison.

Snare, Floor Tom, Concert Bass, Toms, Clap, Cowbell, Crash, and Break now use the
same foundation, with their original per-instrument layouts and drawings.
Companion Echo/Overload/Mixer remain Cocoa and are a separate migration pass.

## Adaptation boundaries

- Keep original parameter IDs, ranges, defaults, DSP, note names, state versions,
  MIDI mappings, voice/choke behavior, and factory preset data.
- Cocoa and VSTGUI share the **actual** `kUiRows`, logarithmic slider transfer
  functions, factory-to-parameter assignments, and existing event queues.
- Use the shared CLAP lifecycle, 65–200% proportional sizing, bundled Fira Code
  with fallback, resource packaging, and Unicode file services.
- Use foundation `drawPluginTitle` for lowercase `s3g`, an uppercase plugin
  name, and shared `#D3D3D3` title text. This is a paint-only convention; keep
  host names, IDs, preset names, and paths unchanged.
- Draw instrument-specific illustrations locally, from the same published DSP
  activity/articulation atomics. Do not replace them with generic meters.
- Match observed Cocoa colors: its calibrated grays differ from literal RGB
  constants, and `NSFrameRect` uses the current fill, not `setStroke`. The title
  actions and performance pads consequently have no contrasting border.
- Center text on its corresponding slider/menu/button. Keep the original value
  cell bounds; compact numeric text rather than clipping away leading digits.
  Fit long labels within their original column when using Windows' larger font.

## Feature/interaction contract

| Area | Required parity |
| --- | --- |
| Layout | Original canvas and panel bounds: 920×680 normally, 920×704 Concert Bass, 920×760 Break with seven panels. Preserve row order, uppercase labels, title/status/preset band. |
| Sliders | Original linear/log mappings, full-row hit regions, bidirectional drag, double-click defaults, balanced host gestures including hide during drag. |
| Menus | Custom s3g drawing, 18px rows, 2px gap, preset selection/hover/dismissal; two-column OMNI / CH 1–16 menu. |
| Factory presets | All original presets and parameter assignments in every instrument; preserve MIDI Receive. |
| RANDOM | Original correlated algorithms, atomic batch/rejection when full: 23 timbral parameters normally, 25 Concert Bass, 33 Break. No changes to Output, Note Tracking, Velocity, MIDI Receive, or Trigger. |
| User presets | Existing `.s3gpreset` state format; UTF-8/UTF-16 paths; invalid loads leave parameters intact; LOAD preserves current output trim. |
| Project state | Original state reader/writer and legacy compatibility; project recall restores saved output trim, unlike user preset LOAD. |
| Kick graphic | Four concentric rings and core, original center, radii, pulse, opacity, color, line-width formulas, caption, and TRIGGER activity. |
| Hi-Hat graphic | Two cymbals, stand, eight rays, clutch, original formulas/caption; CLOSED / OPEN / PEDAL indicators reflect actual articulation. |
| Snare / Floor Tom / Concert Bass graphics | Original four rings, core, and seven cubic wire curves; retain each center, caption, and activity response. |
| Toms graphic | Original four rings/core and six LOW/MID/HIGH HEAD/RIM pads, including the two-row pad geometry. |
| Clap graphic | Five offset hand-contact ellipses, twelve rays, core; CLAP / FLAM / TIGHT. |
| Cowbell graphic | Three closed trapezoidal shells, seven rays, upper contact point; COW / MUTED / HIGH. |
| Crash graphic | Five cymbal ellipses, twelve rays, core; CRASH / CHOKE / BELL. CHOKE acts on an already ringing voice. |
| Break graphic | Four tapered band traces, original cycle counts/widths/activity colors, KICK / SNARE / TOM / HI-HAT pads; separate COHESION panel. |
| Performance | Pads enqueue original transient trigger gestures; audible without external MIDI and independent of MIDI Receive; open-to-closed/pedal choke retained. |
| Host changes | Periodic refresh updates parameters, graphic activity, and factory/CUSTOM recognition after automation/state changes. |

## Builds and verification

macOS remains Cocoa by default for source builds. Opt into these editors with
`S3G_ENABLE_DRUM_FAMILY_VSTGUI_ON_MACOS=ON` and portable GUI support enabled.
`S3G_BUILD_DRUM_GUI_PILOTS=ON` builds just these instruments without requiring
unrelated future components (which contain macOS-only audit targets).
`S3G_BUILD_DRUM_GUI_FAMILY=ON` selects all ten instruments without enabling
those unrelated components; use this for the Windows family cross-build.
Windows uses VSTGUI automatically when portable GUI support is enabled.

Verification targets:

- `audit_drum_<instrument>`: existing synthesis/CLAP/state tests
  and native macOS GUI lifecycle/resizing/RANDOM tests; run with Cocoa and VSTGUI.
- `s3g_drum_<instrument>_canvas_smoke` (ten targets): actual canvas
  clicks/drags through the original event queues and DSP; every row and factory
  preset, MIDI menu, RANDOM exclusions/backpressure, preset file round-trip,
  malformed file, output preservation/project recall, pads/audio/visual state,
  numeric fitting (including count units such as `12 modes`), and end-of-drag
  cleanup. Exercise Mac and Windows font sizes in the readout-fitting checks.
- `S3G_DRUM_CAPTURE_DIR` on canvas tests saves idle/active/menu/pad PNGs plus
  matched Cocoa idle/active PDF masters. These supplement fresh native-host
  captures from `scripts/generate-plugin-screenshots.py`.
- `S3G_DRUM_EXPECT_FONT_FALLBACK=1` supports a copied test bundle with its Fonts
  resource removed, verifying that missing resources do not prevent use.
- Windows PE/export/runtime dependency and font/license packaging validation.
  `bash scripts/package-windows-clap-drum-pilots.sh` creates a fresh test zip.

Reference output locations for the instrument rollout:

- `build-clap-canvas-cocoa-parity/drum-family-reference`: fresh Cocoa baseline.
- `build-clap-sample-vstgui-fidelity/drum-family-reference`: native-host VSTGUI captures.
- `build-clap-sample-vstgui-fidelity/drum-family-parity`: deterministic active/idle,
  menus, and real-audio pad captures.

Exact source comparisons verify unchanged DSP processing, state save/load,
parameter queues, slider mappings, factory assignments, and Cocoa draw methods
for all eight new ports. Fresh Cocoa and VSTGUI captures have been compared for
all eight; original published documentation images are not overwritten.

Actual REAPER testing of the new eight ports on Windows and user visual acceptance remain **pending**.
Automated tests and cross-compilation must not be reported as Windows runtime
acceptance. No installed plugins are replaced by the build or packaging steps.
