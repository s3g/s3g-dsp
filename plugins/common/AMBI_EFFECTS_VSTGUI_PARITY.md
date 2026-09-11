# Ambi Effect VSTGUI parity — 2026-09-10

This pass covers DJ Filter, Delay, Pitch, Gain, Resonance Print, Partial
Trace, Response Trace, and Displacement (all 64-channel CLAP effects).
Installation and a Windows distribution ZIP are separate, user-requested steps.

## Reference contract

The retained Cocoa implementations and the eight `docs/ambi-effect-*.html`
pages are the reference. The VSTGUI canvases retain the original coordinates,
projection equations, depth ordering, mesh edges, pickup glyphs, energy halos,
relationship rays, selected routes, mask targets, timeline, hit rectangles,
menu mappings, and slider response curves.

| Editor | Features retained beyond the shared controls |
| --- | --- |
| DJ Filter | LP/open/HP field, filter and resonance trim markers/rails, pickup selection and two-value reset, input halos, topology routes |
| Delay | Effective time/feedback relationships and readouts, independent pickup rails, dual-tap DSP, input halos and routes |
| Pitch | Effective interval rays, pitch/window pickup rails, window/glide controls, input halos and routes |
| Gain | Effective-gain rays and selected dB/input readout, individual gain rail, input halos and routes |
| Resonance Print | Capture/cancel/apply/bypass/clear, modal strength field, fundamental/confidence/mode counts, 24 tune/decay pairs, GOV/SAFE status |
| Partial Trace | Apply/bypass, freeze/unfreeze, smear, active partial/strongest-frequency status, trace activity and mask field |
| Response Trace | Capture/apply/bypass/clear, capture/conditioning/kernel-build progress, directional kernel count, response activity, fixed 1024-sample latency |
| Displacement | Version-1 JSON score import, 64 scenes/24 points, source/target meshes and labels, Oklab-derived point colors, timeline scrub, four views, orbit, scroll/button zoom, clock/playback/distance modes |

All eight retain INIT, LOAD, SAVE, host automation, camera state, original
parameter IDs/ranges and the existing state versions. Preset recall preserves
OUT; complete host state restores it. Captured prints, frozen voices, response
banks, and parsed displacement scores remain embedded in state; they are not
replaced with file references.

The compact Resonance Print GUI intentionally has no MASK CURVE slider:
the original editor and documentation expose it through host automation.
Some older DJ Filter prose still refers to twenty pickups/two explicit bodies;
the current Cocoa/DSP source supports 24 slots and Sphere24. This migration
preserves that source behavior. Displacement retains its original float32-only
audio processing rather than changing its DSP/port contract.

## Shared adaptations

- Fira Code and its license use the existing bundled-resource/fallback policy.
- Titles keep lowercase `s3g` with uppercase plugin names. Family grays,
  centered slider handles, aligned labels/readouts, buttons and custom menus
  use the shared foundation.
- Original calibrated colors are converted to sRGB; continuously varying
  colors use the Generic RGB ICC matrix/TRC, independently of AppKit.
- CLAP embedding, show/hide/destroy and proportional 65–200% resizing use
  `s3g_clap_canvas_gui.inc`.
- Presets and score dialogs use the shared UTF-8/UTF-16 path handling.
- GUI edits publish canonical scalar values and bounded begin/value/end
  gestures. Audio process/flush owns live DSP parameter application. Queued
  notifications do not replay old values over newer host automation.

## Capture state and Windows DSP

Resonance Print and both Trace engines previously required Accelerate to
prepare. `dsp/s3g_ambi_effect_fft.h` retains the exact Accelerate calls on Mac
and supplies an allocation-free-after-setup packed-real radix-2 backend
elsewhere. Packing and normalization are checked against Accelerate at the
actual 2048/4096 transform sizes. The existing DSP smoke suites also run with
the portable backend forced on Mac.

Capture state is read through a single-producer/single-consumer triple buffer,
not directly from mutable DSP storage. Response Trace mirrors samples during
its existing bounded conditioning work, so saving a preset does not introduce
a full-bank audio-thread copy. Banks are preallocated at activation; at
48 kHz the three state buffers add approximately 20 MiB for Response Trace.
Incomplete conditioning is not serialized as a finished response.

Loading capture state while active requests the normal CLAP host restart.
The replacement is decoded privately, values/camera are published immediately,
and captured data is restored at the next activation. This avoids preparing
or replacing a live convolution engine on the GUI/audio callback. Tests honor
the restart request. A host must service that request to hear the replacement
capture immediately.

Displacement preserves its existing immutable runtime replacement mechanism.
Its version-1 state writer now zeros the three padding bytes after `bypass`;
this fixes nondeterministic repeated saves without changing the codec layout.

## Build and checks

Enable the Mac port with:

```sh
cmake -S . -B build-clap-sample-vstgui-fidelity \
  -DS3G_ENABLE_AMBI_EFFECTS_VSTGUI_ON_MACOS=ON
```

Windows enables these editors when VSTGUI is available. Turning the Mac option
off retains the original Cocoa editors for comparison.

Verified in this workspace:

- All eight Mac CLAP targets and all eight MinGW Windows x64 CLAP targets build.
- Eight source-level GUI suites pass: parameter coverage, queue pressure,
  balanced gestures, continuous sliders/default reset, menus, pickup rails,
  cameras, UTF-8 presets, OUT preservation and truncated-file rejection.
- Eight native Mac GUI lifecycle/resize/documentation captures pass.
- Independent retained-Cocoa versus portable binaries match parameter metadata,
  bidirectional state exchange and deterministic float/double audio scenarios.
- Existing Resonance Print and both Trace CLAP capture/state/legacy/level-safety
  tests pass, with active-state restart handling.
- Native/portable FFT comparisons and both forced-portable DSP suites pass.
- Capture snapshot producer/consumer stress passes under ThreadSanitizer.
- CLAP validator: eight passed, zero failed.
- Mac signatures and font/license payloads verified. Windows imports contain
  system DLLs only; no external MinGW runtime or FFT DLL is needed.

Artifacts:

- `build-clap-sample-vstgui-fidelity/ambi-effect-reference/bundles.tsv`:
  scoped manifest using canonical installation names.
- `ambi-effect-reference/masters/`: native PDF reference captures.
- `ambi-effect-reference/interactions/`: offscreen PNG references.
- `ambi-effect-reference/validator.json`: validator report.
- `build-clap-canvas-cocoa-parity/ambi-effect-reference/masters/`:
  retained Cocoa comparison captures.

Windows REAPER interaction/listening, mixed-DPI monitors, and active capture
preset switching on the separate Windows machine remain the manual acceptance
step. These are not claimed as tested by cross-compilation.
