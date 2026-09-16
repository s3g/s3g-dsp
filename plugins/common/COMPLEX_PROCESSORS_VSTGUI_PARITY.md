# No Input Mixer / Formant Matrix / Vox / Imprint migration

The retained Cocoa code and the existing documentation are the references:
`docs/processor-no-input-mixer.html`, `docs/formant-matrix.html`,
`docs/ambi-encoder-vox.html`, `docs/processor-ambi-imprint.html`, and their
original images in `docs/assets/plugin-guis`. Those images are not replaced.
The portable canvases retain the original geometry and event hit maps, using
the existing shared Fira Code, grayscale, separators, text alignment and
65–200% proportional resizing. Titles keep lowercase `s3g` and uppercase names.

## Preserved surfaces and behavior

- No Input Mixer: PATCH (cables/grid), MIXER, CHANNEL, SAFETY and AUX; scopes,
  meters and activity; lane selection, routing, polarity and smooth controls;
  lane and aux insert editors; independent page pop-out/dock; presets, seeded
  randomization, NEW/FORGET, containment, PANIC, MIDI matrix and NRPN control.
  The retired `if (false && page == 5)` SURF GUI is not reintroduced. Its
  compatibility parameters/state remain intact. The standalone app stays Cocoa.
- Formant Matrix: the current `s3g_formant_matrix_gui.inc` is the reference,
  not the obsolete disabled GUI in the wrapper. ROUTE/BANK/SOURCES/PHRASE/FX,
  the 22/16-band A/B matrix, signed crosspoints, scene operations, per-band
  trims/meters, live/internal input paths, editable/compiled phrase and the
  complete pitch-scale menu are retained.
- Vox: FIELD and LYRICS, colored voice markers/trails/orientation guides,
  camera and selection, multiline cue sheet/map, generator and cue modes,
  FREE/MIDI/BOTH, SPEAK/TEXTURE, voice/phrase/ensemble/spectral controls, factory
  presets and `.s3gvox` JSON. WAV parsing, WORLD analysis/resynthesis, five pitch
  anchors, oto.ini timings, pronunciation overrides, nested voicebanks and
  UTF-8/Shift-JIS text decoding are ported. The plugin uses the same packed-real
  FFT contract on Accelerate and Windows, not a silent fallback.
- Imprint: all 19 atlas responses, external `.s3gimprint`, room/polygon/profile
  geometry, camera controls, focus/width/mix, field listening, convolution,
  safety and project/preset state. The FFT adapter preserves the original
  packed format and normalization on Windows.

The native GUI source remains buildable. No plugin IDs, host parameter IDs,
audio-port layouts or state-format version numbers are changed.

## Platform boundaries and safeguards

GUI parameter edits use ordered queues and balanced begin/value/end gestures,
including host backpressure. Preset reads are staged before committing. Vox
and Formant Matrix reject truncated payloads before changing the live patch;
Imprint still accepts its documented legacy files without a camera tail.
No Input Mixer preset recall retains the complete-patch reseed/reset operation.
GUI preset recall preserves the current output gain, as in Cocoa.

Vox publishes a bounded atomic parameter snapshot for GUI/state readers, and
hands source reset requests to process/flush. Its lyric editor has an explicit
scroll viewport: the pinned VSTGUI factory ignores its rectangle argument and
the text document sizes itself. Long-document scrolling is covered by a test.
Windows LOAD distinguishes a WAV picker from a voicebank-folder picker; both
use the shared Unicode filesystem boundary. Mac No Input Mixer pop-outs remain
independent across Spaces; Windows pop-outs are owned by the REAPER FX window
so they stay above it rather than becoming globally topmost.

Fira Code and its OFL license are bundled per Mac plugin and shared in the
Windows folder. Imprint Atlas and the RapidJSON/WORLD licenses are packaged.
WORLD is linked statically; the Windows package rejects WORLD-disabled builds.
The portable cache uses the existing Mac cache folder and
`%LOCALAPPDATA%/org.s3g.s3g-dsp/AmbiVoxWorld` on Windows. A changed file-timestamp
hash can cause a one-time reanalysis of older Mac cache entries.

Vox source-storage policy is unchanged: external vocal WAVs/voicebanks are not
embedded or automatically restored from a saved project. Reload the external
source after reopening; a user preset keeps the currently loaded source.
Expanding this policy is a separate feature, not part of the GUI migration.

## Build and test

Mac: `S3G_ENABLE_COMPLEX_PROCESSORS_VSTGUI_ON_MACOS=ON`; OFF retains Cocoa.
Enable `S3G_ENABLE_WORLD=ON` for Vox. Windows selects these VSTGUI editors with
the existing portable-GUI option and the MinGW x64 toolchain.

Targets: `s3g_no_input_mixer_clap`, `s3g_acapella_source_synth_clap`,
`s3g_ambi_vox_encoder_clap`, `s3g_ambi_imprint_clap`.

Verification performed for this batch:

- All four Mac bundles and Windows x64 binaries build; retained Cocoa builds
  also compile with WORLD enabled.
- Five direct GUI tests cover parenting, 65/100/150/200% resizing, page/field
  rendering, text/scrolling, pop-out/dock, atlas and voicebank loading, Unicode
  presets, output preservation, fragmented/truncated state and edit gestures.
- `s3g_complex_processor_clap_parity_smoke` compares separately built Cocoa and
  VSTGUI metadata, parameter values, bidirectional state and audio. All four
  have zero maximum sample error in these deterministic scenes.
- Native versus forced-portable Vox FFT tests process the same synthetic
  voicebank: 1,310,720 samples, maximum difference approximately 1.43e-6.
  Imprint's forced-portable convolution safety and FFT/Accelerate equivalence
  tests also pass, as do existing Formant Matrix and No Input Mixer CLAP tests.
- Captures are visually checked against the original documentation graphics.
  Set `S3G_COMPLEX_CAPTURE_DIR` on the direct tests to regenerate them, or
  `S3G_COMPLEX_AUDIO_CAPTURE` to write the test's float32 audio.

These are macOS and cross-build checks, **not** a Windows REAPER runtime pass.
Native Windows dialogs, editing/IME, host automation/undo and window behavior
still need the separate machine. See `COMPLEX_PROCESSORS_WINDOWS_README.txt`.

For full-collection Mac installation, use the maintained
[local-build installation guide](../../docs/building-from-source.html#install-local).
Back up existing bundles and preview the changes with `--dry-run` before
installation. Building does not install.

Package: `cmake -P scripts/package-windows-complex-processors.cmake`.
The ZIP checks PE/export/static-runtime linkage and includes SHA-256 hashes,
font, atlas, instructions and licenses.

This family-specific package does not include Tracker or Ambi Energy. Tracker
is included in the experimental Windows suite; Ambi Energy remains Mac-only.
See the current [platform and installation guide](../../docs/installing-plugins.html#windows-suite).
