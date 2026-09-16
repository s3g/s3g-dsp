# Acid, Horizon and VOT: portable GUI parity

References are the retained Cocoa implementations, `docs/ambi-encoder-acid.html`,
`docs/ambi-encoder-horizon.html`, `docs/ambi-encoder-vot.html` and their screenshots
in `docs/assets/plugin-guis`. The source geometry, projection mathematics,
ordered drawing paths, visibility rules and hit maps are retained. The shared
foundation supplies Fira Code/fallback, established colors and typography,
menu separators, proportional 65–200% resizing, platform resources and dialogs.

## Feature inventory

- Acid: sixteen note bars with note/interval/rest labels, selected step and live
  playhead; G/A/S; all four control pages; persistent FORMAT/OUT; 101-scale and
  circuit menus; twelve factory patterns; random pattern; editable TOP X/Y and
  SIDE Y/Z spatial points, selected flags, double-click/reset/random; arrows,
  G/A/S/Space and wheel/Shift shortcuts. INT/HOST, MIDI and output modes use the
  original DSP and parameter identities.
- Horizon: original acoustic-horizon projection and circle/bar/diamond entity
  symbols, energy, orientation guides, TOP/SIDE/3/4/orbit/zoom; all nine ecologies
  and their contextual generator rows; all sixteen factory scenes and safe
  random; complete atmosphere, score, identity and field-listener controls with
  eight live listener meters. View state and meters are platform-independent.
- VOT: FIELD voice selection, nearest-neighbor relationships, camera and trails;
  VECTOR's sixteen waveforms, pad, route, selected voice and interpolated wave;
  SCORE's U/V lanes, route-node editing, TIME/U/V/CURVE, 2–16 nodes, reset and
  sustain-loop bounds; original synthesis, tuning, AED and high-level score
  controls, 101 scales, INIT and bounded random. USER WAV/WAVE loading preserves
  PCM16/24/32 and float32 decoding and multichannel averaging. Unsupported,
  truncated and non-finite files are rejected without replacing a valid bank.

## State and automation

Acid and Horizon retain their audio-thread GUI queues. Portable editors reserve
space for complete actions and closing gestures, retain host backpressure and
close edits on mouse-up/hide/destruction. VOT now queues GUI parameter edits
through process/flush and publishes atomic display values, instead of letting
the GUI mutate the audio engine. Its DSP and state versions are unchanged.
Score nodes and USER banks retain their existing shared publication mechanisms;
composition edits mark host state dirty. No file decoder runs on the audio
thread. Acid and VOT saves read published values, not the running audio engine.

Acid/VOT use the original `.s3gpreset` state bytes. User loads first decode an
isolated inactive instance and preserve OUT. Horizon retains its own versioned
`.s3ghorizon` format and Cocoa's actual OUT/ORDER-preserving custom-load policy;
the general guide's description of complete custom states is less specific.
Horizon factory/random preserves OUT/ORDER/listener controls. Project state
retains the original output, camera/page, score and embedded-atlas behavior.
User file paths use UTF-8/UTF-16 helpers. WAV dialogs accept `.wav` and `.wave`.

## Build and verification

Mac uses `S3G_ENABLE_SCORE_ENCODERS_VSTGUI_ON_MACOS=ON`; OFF preserves the Cocoa
reference. Windows with portable VSTGUI automatically selects these editors.
The Acid/Horizon targets are available without enabling unrelated future code.

```sh
cmake -S . -B build-clap-sample-vstgui-fidelity \
  -DS3G_ENABLE_SCORE_ENCODERS_VSTGUI_ON_MACOS=ON
cmake --build build-clap-sample-vstgui-fidelity --target \
  s3g_ambi_acid_encoder_clap s3g_ambi_horizon_encoder_clap s3g_ambi_vot_encoder_clap \
  s3g_score_acid_canvas_smoke s3g_score_horizon_canvas_smoke s3g_score_vot_canvas_smoke
ctest --test-dir build-clap-sample-vstgui-fidelity \
  -R '^s3g_score_.*_canvas_smoke$' --output-on-failure
```

Native tests need a window-server session. `S3G_SCORE_CAPTURE_DIR` saves page,
factory, menu, first-open, live and restored PNGs. The independent
`s3g_input_encoder_clap_parity_smoke` compares separately compiled Cocoa/VSTGUI
binaries for metadata, chunked state exchange and audio. VOT's original lack
of double-precision audio support is unchanged; Acid/Horizon retain theirs.

The scoped package manifest is `scripts/clap-vstgui-score-encoders.tsv`.
For full-collection Mac installation, follow the maintained
[local-build installation guide](../../docs/building-from-source.html#install-local)
and back up existing bundles before replacing them. Windows packaging uses
`cmake -P scripts/package-windows-score-encoders.cmake`, checking x64 PE,
`clap_entry`, static runtime linkage, font/licenses and SHA-256 hashes. The ZIP
includes the repository VOT atlases for manual import. Windows REAPER acceptance
remains the manual checklist in `SCORE_ENCODERS_WINDOWS_README.txt`.

Verified on Mac: all three direct canvas tests, the shared dropdown-separator
test and all three independent native-bundle lifecycle/resize checks pass.
Acid and Horizon's original DSP/CLAP smoke tests pass. Each instrument matches
the independently built Cocoa binary in all three audio/state/metadata scenes
(maximum sample difference 0). CLAP validator: 52 passed, 11 skipped, 0 failed.
The Windows x64 package passes PE/export/static-runtime, SHA-256 and ZIP integrity
checks. Actual Windows REAPER GUI/dialog acceptance is not claimed by these tests.
