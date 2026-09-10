# Spectral / Buffer / Delay Field VSTGUI migration

## Scope and reference

Effect Spectral Spray 2/8, Processor Buffer 8, and Effect Delay Field 16.
The original Cocoa source at baseline `36c67a6`, fresh native Cocoa captures,
the [Spectral / Buffer guide](../../docs/spectral-buffer-processors.html), and
the [Delay Field guide](../../docs/distributed-output-processors.html) are the
specification. This is an adaptation, not a redesign. Cocoa remains available
as the comparison implementation; documentation images are not replaced.

These are discrete-channel effects, including Delay Field's stereo input and
fixed sixteen-channel output port. Ambisonic and generative processors are not
part of this batch.

## Parity contract

| Area | Preserved behavior |
| --- | --- |
| Layout | Original dimensions: Spray 760×376, Buffer 760×584, Delay Field 760×520. Original panels, rows, hit areas and preset/status band. Shared Fira Code, lowercase `s3g` and uppercase title, family grays, centered slider handles and aligned text. |
| Spray 2/8 | All fourteen controls, including continuous FRZ, frequency window, phase blur, spectral motion, safety and output. Original ranges, defaults and HI display/drag mapping; FFT 4096 / 8x OLA footer. No substitute visualization or extra control bank. |
| Buffer 8 | All twenty-one parameters, GRID/SYNC/ERRM menus, and CAP/CLR actions. Original eight-lane window preview formulas: deterministic deviation, spread/skew/center ratios, repeat positions, reverse/skip appearance, skip markers, chase cursor, error blocks, ghost strip and crossfade edges. This is the original parameter-driven preview, not a new live playback animation. |
| Delay Field 16 | All fifty-five parameters across Shard, Orbit, Cascade and Iterate. Original model/format/traversal menus, three Iterate toggles and seed slider. All sixteen delayed-only, pre-fold activity meters stay visible in every format; input and folded-output readouts and format footer remain. |
| Input | Original slider mappings, double-click defaults and menu options. Custom in-canvas menus with hover and outside dismissal; original control-only versus full-row hit conventions retained. |
| Presets | Existing `.s3gpreset` state layouts, versions and directories. LOAD and INIT preserve Spray/Buffer OUT, while project recall restores it. Delay Field has no global OUT: its original LOAD/INIT behavior restores/resets all model gains. The preset field remains an INIT action, not an invented factory bank. Unicode file paths use the shared dialogs. |
| Audio | Original DSP algorithms, parameter IDs/metadata, descriptors, routing and state versions. Buffer captured audio and Spray spectral history remain runtime state, not newly embedded preset data. Delay Field's model handoff and output folding are unchanged. |
| Lifecycle/resources | Shared CLAP GUI lifecycle and proportional 65–200% resizing. Bundled Fira Code/license, missing-font fallback, macOS bundle and Windows resource lookup, UTF-8/UTF-16 file paths. |

Rendered Cocoa output is authoritative when source drawing calls are ambiguous.
In these views `NSFrameRect` uses the current fill, not the preceding
`setStroke`. Buffer's preview/tracks/CAP/CLR and Delay Field's toggles therefore
do not gain visible outlines. Active Delay Field meter frames use the last
active fill color; idle frames match the strip. Custom calibrated Cocoa grays
use the shared calibrated-to-sRGB table, and meter colors are converted to
sRGB. Shared typography and renderer differences do not imply pixel-identical
screenshots.

Spray's state contains two legacy fields, `damage` and `repeat`, which have no
current Cocoa controls. The portable state path preserves them when loading
and resaving instead of losing them through the old partial published-parameter
snapshot. This compatibility correction adds neither controls nor a new state
version.

## Threading, automation and state

Portable editors publish controls through atomic banks. Only `process`,
`params.flush`, or inactive activation transfers those controls into the
audio-owned processor. Buffer CAP/CLR are queued audio-side actions. Rendering
reads atomic parameters and telemetry, not mutable DSP objects.

The SPSC notification queue reports balanced BEGIN/VALUE/END gestures. An END
slot is reserved before beginning an edit, and preset batches are rejected
before publication if capacity is insufficient. Draining does not replay old
GUI values into DSP; superseded value notifications are discarded without
dropping gesture boundaries. Incoming host automation is applied after GUI
service. DSP sanitization uses compare/exchange so it cannot overwrite a newer
GUI publication with an older value.

State and user preset codecs operate on isolated instances before publishing
validated controls. Truncated, unsupported-version and non-finite states are
rejected without partially modifying the live instance. Saving snapshots the
atomic control bank rather than reading audio-owned parameters.

## Builds and verification

On macOS enable `S3G_ENABLE_PORTABLE_CLAP_GUI` and
`S3G_ENABLE_MEMORY_EFFECTS_VSTGUI_ON_MACOS=ON`. Windows selects these editors
automatically when portable GUI support is enabled. This Windows batch builds
with `S3G_BUILD_FUTURE_COMPONENTS=OFF`.

Targets:

- `s3g_spectral_spray_clap`, `s3g_8ch_spectral_spray_clap`
- `s3g_buffer_processor_clap`, `s3g_delay_field_clap`
- `s3g_memory_{spectral_spray,8ch_spectral_spray,buffer_processor,delay_field}_canvas_smoke`
- `audit_gui_memory_effects`: all four native GUI/lifecycle/resizing audits
- `audit_spectral_spray_routing`: both widths with wider host buffers

Canvas tests exercise actual mouse input, all controls/options, menu rendering,
Unicode presets, output preservation, malformed states, gesture saturation,
blocked host output, newer host automation and hiding during a drag. Audio is
compared against an independently configured instance of the original DSP.
Buffer CAP/GHST/CLR and all sixteen Delay Field model/format combinations are
covered, including silence above the selected active output width.

`S3G_MEMORY_CAPTURE_DIR` writes portable PNGs and matched Cocoa PDFs for initial,
model, audio, detailed-window and Windows-font-metric scenarios.
`S3G_MEMORY_EXPECT_FONT_FALLBACK=1` supports running a **copy** of a test app with
its Fonts directory moved aside; never remove resources from installed bundles
to perform this check.

Verification on 2026-09-09:

- Four macOS portable builds and native GUI/resizing audits passed. All four
  Cocoa fallback builds and their native GUI audits passed too.
- Nine CTest checks passed: four canvas tests, shared layout and parameter queue,
  DSP smoke, Delay Field DSP smoke and Delay Field CLAP smoke.
- Both Spectral Spray routing audits passed with sixteen-channel host buffers.
- All four copied-app missing-font tests passed using the macOS fallback.
- CLAP parameter/state/process-filter validation: 24 passed, 8 skipped,
  zero failures or warnings. Skipped tests are not claimed as passes.
- Four Windows x64 builds passed, with `clap_entry` exports and Windows/UCRT-only
  DLL dependencies; no extra compiler runtime DLLs. Mac bundle signatures and
  matching font/license resources on both platforms verified.
- Fresh native Cocoa and portable reference captures are under each build
  tree's `memory-effects-reference/`. Interaction captures are under
  `build-clap-sample-vstgui-fidelity/memory-effects-parity/`.

Windows REAPER runtime testing remains required on the separate Windows
machine. Cross-builds, resource checks and Windows-sized font renders do not
replace that test. Installation and a Windows transfer package are separate
steps; this migration does not change the installed plugins.
