# Modal, Medium and Membrane Kick: portable GUI parity

The retained Cocoa canvases, their screenshots and the three instrument guides
are the reference: `docs/ambi-encoder-modal.html`, `docs/ambi-encoder-medium.html`,
`docs/ambi-encoder-membrane-kick.html`, and `docs/assets/plugin-guis/`.
This migration retains their paths, draw order, projection math, hit maps,
control visibility, factory voices and parameter/state identities.

## Interface inventory

- Modal: 4–8 colored bodies, energy halos, world-aligned tetra/cube listener
  diamonds, moving actuator routes, TOP/SIDE/3/4 projection, zoom, body selection
  and drag, all-body AED RESET, per-body SKIN X/Y and double-click reset, selected
  body readouts, listener/character/projection controls, 25 factory presets,
  guarded random, HOA and body-stem formats, original project/preset state.
- Medium: all eight nodes/twelve edges, live energy and strike halos, selected
  white ring, held-MIDI cyan rings and note labels, click-to-select/strike,
  cameras/orbit/zoom, SOURCE/SEQ/MIDI pages, all four exciters, per-node masks,
  Euclidean pulses/rotations/ratchets, scale/note pool, four MIDI modes, seven
  factory presets, guarded random and original state versions.
- Membrane Kick: original animated boundary and sixteen pickup points, all
  five shapes/amounts, actual strike crosshair, click-to-place-and-strike,
  BODY/STRIKE pages, manual STRIKE, three placement modes, MIDI receive,
  tracking/velocity, fourteen factory presets, random factory voice, HOA,
  direct pickup labels and stereo output, original state versions.

The Membrane Kick preset/action group is shifted 40 logical pixels to the right
to keep its full title readable at the shared font size on both platforms.
The same title-band geometry drives drawing and hit testing; no controls are
removed or reduced in font size. All other source geometry is retained.

## Shared foundation and thread ownership

CLAP lifecycle and proportional 65–200% sizing use `s3g_clap_canvas_gui.inc`.
Fira Code, fallback fonts, resource paths, colors, centered controls, menus with
item separators, and Unicode file dialogs use the existing portable foundation.
Titles use lowercase `s3g` with uppercase names, and menus use uppercase labels.

Modal retains its control/audio mailbox and adds notification-only automation
events, so host notifications never apply edits a second time. Its initialized
body geometry remains available before activation. Medium/Membrane retain their
audio-service parameter queues and atomic display values. Medium's meter and
camera state is now platform-independent; editor show/hide controls publication.

Editors reserve queue room for compound gestures and factory/random actions,
retain events under host output backpressure, and close gestures on mouse-up,
hide and destruction. DSP algorithms, parameter IDs/ranges, descriptors and
state versions are unchanged.

All three use their original `.s3gpreset` bytes. File loading first decodes an
isolated inactive instance; a truncated file cannot damage the current voice.
Valid values are handed to the existing control/audio pipeline, preserving OUT.
Medium's sequencer/MIDI reset and Membrane's trigger-gate reset are queued in
order on the audio thread. Project recall retains its original OUT restoration.
Factory/random preservation policies remain those of the individual Cocoa GUI.

## Build and verification

Enable `S3G_ENABLE_RESONATOR_ENCODERS_VSTGUI_ON_MACOS=ON` on Mac. Default OFF
keeps the Cocoa reference. Windows selects these editors with portable VSTGUI
enabled; Membrane Kick's target is included without enabling unrelated future
components.

```sh
cmake -S . -B build-clap-sample-vstgui-fidelity \
  -DS3G_ENABLE_RESONATOR_ENCODERS_VSTGUI_ON_MACOS=ON
cmake --build build-clap-sample-vstgui-fidelity --target \
  s3g_accelerometer_field_encoder_clap s3g_ambi_encoder_medium_clap \
  s3g_ambi_membrane_kick_clap s3g_resonator_modal_canvas_smoke \
  s3g_resonator_medium_canvas_smoke s3g_resonator_membrane_canvas_smoke
ctest --test-dir build-clap-sample-vstgui-fidelity \
  -R 's3g_resonator_.*_canvas_smoke' --output-on-failure
```

`S3G_RESONATOR_CAPTURE_DIR` saves original-page, factory, menu, first-open and
live/restored PNGs. Native tests require a macOS window-server session.
`s3g_input_encoder_clap_parity_smoke` compares independently built Cocoa/VSTGUI
modules for metadata, chunked state exchange, rejected truncated state, and
float/double audio in three automated MIDI/input scenes. Membrane's momentary
Trigger is released on both sides after state restore because it is not saved.

Windows packaging checks x64 PE, exported `clap_entry`, static runtime linkage,
font/license resources and SHA-256 checksums. Windows REAPER GUI and file-dialog
acceptance remains a separate manual step using the packaged checklist.

Verified on Mac: all three direct-canvas tests and all three independent native
bundle lifecycle/resize tests pass. The original Modal DSP/state and Membrane
DSP/CLAP regressions pass. CLAP validator reports 54 passed, 9 skipped, 0 failed.
All three automated float/double comparison scenes per instrument match the
independently built Cocoa binaries exactly (maximum sample difference 0).

## Distribution scope

`scripts/clap-vstgui-resonator-encoders.tsv` contains exactly these three bundles.
`cmake -P scripts/package-windows-resonator-encoders.cmake` creates a timestamped
folder and ZIP. `bash scripts/install-vstgui-resonator-encoders.sh --dry-run`
checks native install scope; running without the flag makes verified recoverable
backups and a separate receipt. Building/packaging does not install the plugins.
