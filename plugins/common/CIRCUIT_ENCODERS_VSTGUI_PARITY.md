# Pulsar, Neural Ecology and Wrangler: portable GUI parity

The retained Cocoa editors and instrument documentation are the specification.
This batch adapts their drawing order, paths, projection math, geometry, control
visibility and pointer algorithms; it does not replace them with generic panels.
The original Cocoa branches remain buildable for independent comparisons.

| Instrument | Original interface retained | State and actions retained |
| --- | --- | --- |
| Pulsar | FIELD point cloud and event halos; A/B/C PULSARETS waveform/envelope/FM traces; four-lobe NEURAL display; LISTEN auditory body; cameras, zoom and lane selection | All lane/tuning/retrigger controls, 4–32 points, capture request/progress/table status, factory/random, `.s3gpulsar` and project codec |
| Neural Ecology | FIELD nodes, signed connections and adaptive pickup diamonds; SCORE lattice with 1/2/4/8 planes, cell signature bars, portals, trail, active/selected indicators and base/effective slider handles | GROW, STOP/cell audition, GO/PLAY/FOLLOW, MIDI direction navigation, freeze, feedback flashes, resident/live genomes and lattice in `.s3gne` and project state |
| Wrangler | FIELD, HIST 19 / MODERN 5 CURVE dimensions and four 16-voice banks; stored dashed/effective solid curves; WRITE/SETTLE LISTEN displays; full Voronoi SURF and synchronized POP | Labels only arm; curve drag locks dimension/voice; CURVES OFF preserves values; all listener controls; 24 cells, assignments/capture/delete, focus/curve/glide, automated X/Y, `.s3gawp` and full project SURF |

References: `docs/ambi-encoder-pulsar.html`,
`docs/ambi-encoder-neural-ecology.html`, `docs/ambi-encoder-wrangler.html`,
`docs/parameter-surface.html`, and their screenshots in
`docs/assets/plugin-guis/`.

## Shared portable services

- CLAP lifecycle and proportional 65–200% resizing use `s3g_clap_canvas_gui.inc`.
- The established family palette, Fira Code body/title metrics, alignment,
  sliders, panels and separated custom dropdowns use the shared foundation.
  Titles retain lowercase `s3g` and uppercase instrument names.
- Main-thread edits use the existing instrument control API. A bounded queue
  carries host notifications only, not a second DSP edit. Begin/value/end
  gestures reserve space for their ends and survive host backpressure. Preset
  and random actions rescan parameter values; persistent edits mark state dirty.
- Original preset bytes/extensions and codecs are retained. Only file opening
  changes to the shared UTF-8/UTF-16-safe helper and native file dialogs. Preset
  recall preserves OUT; project recall restores it. Pulsar's capture material
  remains runtime-only under the original behavior. Wrangler's complete SURF
  lives in project state, not its single-instrument preset files.
- Before the first audio block, private display engines publish the actual
  initialized field geometry (and Neural Ecology node visibility). They never
  mutate the audio-owned engine. Once audio telemetry arrives it takes over.
- Wrangler POP uses the shared owned auxiliary window on both platforms.
  Edits, custom menus and EDIT/PLAY mode remain synchronized; POP returns the
  main editor to FIELD and hides with the parent GUI.
- Pulsar's two original lane-header text anchors are retained. Long waveform
  names and readouts fit together using measured Fira Code widths, avoiding
  overlap without deleting information or changing the layout.

No DSP algorithm, CLAP descriptor, parameter ID/range, or file version changes.

## Build and verification

Enable `S3G_ENABLE_CIRCUIT_ENCODERS_VSTGUI_ON_MACOS=ON` on Mac. Its default OFF
keeps the Cocoa reference. Windows selects VSTGUI whenever VSTGUI is enabled.

```sh
cmake -S . -B build-clap-sample-vstgui-fidelity \
  -DS3G_ENABLE_CIRCUIT_ENCODERS_VSTGUI_ON_MACOS=ON
cmake --build build-clap-sample-vstgui-fidelity --target \
  s3g_ambi_pulsar_encoder_clap s3g_ambi_neural_ecology_clap \
  s3g_ambi_wrangler_encoder_clap \
  s3g_circuit_pulsar_canvas_smoke s3g_circuit_neural_canvas_smoke \
  s3g_circuit_wrangler_canvas_smoke
ctest --test-dir build-clap-sample-vstgui-fidelity \
  -R 's3g_circuit_.*_canvas_smoke' --output-on-failure
```

`S3G_CIRCUIT_CAPTURE_DIR` optionally saves offscreen PNGs for first-open, pages,
menus, lane previews, lattice planes, curve banks, SURF/POP and live recall.
Native GUI tests require a window-server session.

The direct-canvas tests exercise all menu entries and visible sliders, original
hit maps and double-click defaults, balanced gestures/backpressure, Unicode
preset paths, project recall, capture completion, lattice navigation, curve
locking/value preservation and POP synchronization/menus. Independent built-
bundle host checks cover native parenting, show/hide and proportional resizing.

`s3g_input_encoder_clap_parity_smoke` also supports these zero-input instruments:
pass the independently built Cocoa and VSTGUI executable paths plus the plugin
ID. The three tested scenes per instrument matched parameter metadata, exchanged
states in both directions and produced identical audio (maximum difference 0).
CLAP validator reported 50 passed, 13 skipped, 0 failed across the three modules;
skips include unsupported note ports for the autonomous instruments.

Windows x64 builds are cross-compiled and packaging checks PE architecture,
`clap_entry`, and absence of external MinGW runtime DLL dependencies. This is
not a Windows REAPER GUI acceptance result; use the packaged README checklist
on the Windows test machine.

## Scoped distribution

`scripts/clap-vstgui-circuit-encoders.tsv` names exactly these three canonical
bundles. `scripts/package-windows-circuit-encoders.cmake` creates a timestamped
folder/ZIP with Resources, Fira Code's license, notices and SHA-256 checksums.

For full-collection Mac installation, follow the maintained
[local-build installation guide](../../docs/building-from-source.html#install-local).
Back up current bundles before replacing them and preview changes with
`--dry-run`. Building/packaging does not update the installed copies.
