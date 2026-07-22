# s3g-dsp

A pre-release collection of CLAP audio plugins for multichannel work in
REAPER. `s3g-dsp` is a sibling
project to the `s3g-mc` multichannel REAPER package and the `s3g-max` Max/MSP
externals package. It can be used with those projects or on its own.

Project documentation for `s3g-dsp` is available at
<https://s3g.github.io/s3g-dsp/>.

The project focuses on predictable multichannel routing, compact automation,
and clear control models. Some plugins use topology; others use simpler
relationship controls when that better fits the sound.

The supported target is macOS + REAPER. Other operating systems and
DAWs are not support targets at this stage.

## Status

This is a pre-release project. Plugin names, parameters, and saved states may
change.

The current macOS package installs 80 CLAP bundles, including fixed-width
variants for several processors, bus tools, and speaker-array utilities.
All bundles build, but clap-validator conformance remains in progress for some
older wrappers, chiefly around parameter text conversion and buffered state
I/O. The package should continue to be treated as a pre-release.

Plugin areas:

- [Processors](https://s3g.github.io/s3g-dsp/processors.html): topology,
  macro, spectral, buffer, delay, and multichannel transformation effects.
- [Mix / Pan](https://s3g.github.io/s3g-dsp/mix-pan.html): fold-down,
  metering, direct panning, matrix and node mixing, and speaker calibration.
- [Ambisonics](https://s3g.github.io/s3g-dsp/3oafx.html): 3OAFX,
  `ACN/SN3D` encoders and decoders, field and bus tools, visualization, and
  Ambi Imprint with its nineteen-space atlas. The 3OAFX family includes a
  Displacement Score player for time-varying 24-point field warps authored in
  the related `s3g-mc` browser utility.
- [Instruments](https://s3g.github.io/s3g-dsp/instruments.html): loaded-loop,
  granular, vector-wavetable, WORLD/voicebank vocal, and stochastic
  instruments, including the eight-channel
  [s3g Fault](https://s3g.github.io/s3g-dsp/fault.html) byte-field and codec
  synthesizer.

The [installation page](https://s3g.github.io/s3g-dsp/installing-plugins.html)
lists the included families and the REAPER routing notes that matter for wide
tracks and true stereo outputs.

## Design

- Reusable DSP lives in `dsp/`.
- Plugin wrappers live in `plugins/`.
- VOT-compatible 4 x 4 atlases live in `wavetables/vot/`; plugin loader
  examples live in `examples/`.
- The included Ambi Vox test bank uses a documented UTAU-style folder with
  WAV aliases and `oto.ini` timing. Ambi Vox renders up to 16 vocal sources
  into its fixed 64-channel ambisonic bus and retains WORLD spectral and
  aperiodicity analysis for its voice-model controls. Factory starting points
  and `.s3gvox` user presets retain the performance design while keeping source
  WAV and voicebank audio external.
- Fixed-width CLAP plugins are used where REAPER pin routing needs to be
  predictable.
- Relationship controls keep automation compact where that suits the plugin;
  point, matrix, and calibration tools expose individual controls when needed.
- Spatial plugin camera state is saved with the plugin state where it is part
  of the editing workflow. For example, a top-view panner or decoder display
  should reopen that way with the project.
- 24-channel 3OAFX-specific work is kept separate from general multichannel
  lane processors.
- C++ plugin and DSP work lives in `s3g-dsp`; generated RNBO exports use the
  separate `s3g-rnbo-clap` wrapper project.

## Build

Requirements:

- CMake 3.20 or newer
- A C++17 compiler
- macOS for the current CLAP GUI/plugin bundle targets
- REAPER as the primary tested DAW/host

Build the DSP smoke test:

```sh
cmake --preset dev
cmake --build --preset dev
./build/s3g_dsp_smoke
```

Build CLAP plugins:

```sh
cmake --preset clap
cmake --build --preset clap
```

By default, CMake fetches CLAP headers when CLAP plugin builds are enabled. To
use an existing CLAP checkout:

```sh
cmake -S . -B build-clap \
  -DS3G_BUILD_CLAP_PLUGIN=ON \
  -DS3G_FETCH_CLAP=OFF \
  -DS3G_CLAP_INCLUDE_DIR=/path/to/clap/include
cmake --build build-clap
```

WORLD speech vocoder support is enabled by default for CLAP builds and is used
by Ambi Vox Encoder's WORLD WAV and voicebank paths. The default FetchContent
revision is pinned for reproducible builds. WORLD support can be disabled with:

```sh
cmake -S . -B build-clap \
  -DS3G_BUILD_CLAP_PLUGIN=ON \
  -DS3G_ENABLE_WORLD=OFF
```

## Voicebank Builder

`s3g Vox Builder` is a macOS companion app for preparing Ambi Vox Encoder
voicebanks from one continuous recording, several WAV files, or a folder of
WAVs. It provides automatic silence-aware segmentation for a continuous source,
one-file-per-alias folder import, draggable boundaries, per-segment WORLD pitch
and voicing analysis, guarded level conditioning, filename/order alias guesses,
manual segment correction, audition, timing edits, and UTAU-style export.

```sh
cmake --preset apps
cmake --build --preset apps
open "build-apps/apps/vox_builder/s3g Vox Builder.app"
```

The app writes sliced WAVs, `oto.ini`, `voicebank.json`, `markers.csv`, and the
phoneme list. See [Vox Builder](docs/vox-builder.html) for the workflow.

`tools/voicebank_builder.py` remains available for batch preparation and exact
CSV marker workflows:

```sh
python3 tools/voicebank_builder.py my-recording.wav \
  --phonemes examples/voicebank-builder/phonemes.txt \
  --name my_voice \
  --output examples/voicebanks/my_voice
```

Pass `--markers markers.csv` with `alias,start_ms,end_ms` rows to bypass its
automatic slicer.

## Install Locally

On macOS, install the built CLAP bundles into the user CLAP plugin folder:

```sh
./scripts/install-clap-bundles.sh
```

REAPER may need a plugin rescan after installation.

## Pre-release Binaries

Pre-release macOS CLAP builds may be attached to GitHub pre-releases. These
builds are for early REAPER testing. Plugin names, parameter mappings, saved
states, and included plugins may change before a stable release.

Build output is not committed to this repository. Packaged binaries, when
available, should be downloaded from the GitHub releases page rather than from
the source tree.

Create the local pre-release zip after a complete CLAP build with:

```sh
./scripts/package-macos-clap-prerelease.sh
```

The package contains 80 CLAP bundles, the VOT wavetable library, the Ambi Vox
demo voicebank, and the applicable license notices. The packaging script
ad-hoc signs and strictly verifies every bundle by default. Set
`S3G_CODESIGN_IDENTITY` to use a different macOS signing identity; notarization
is a separate release step.

## Validate

The local smoke executables exercise shared DSP code:

```sh
./build/s3g_dsp_smoke
./build/s3g_3oafx_displacement_smoke
./build/s3g_ambi_imprint_safety_smoke
./build/s3g_ambi_ray_encoder_smoke
./build/s3g_ambi_pulsar_encoder_smoke
./build/s3g_psd_raw_field_smoke
./build/s3g_psd_raw_field_parameter_audit
```

The smoke tests cover multichannel routing, loop playback, finite output,
bounded peaks, de-click stress, high-order encoder/decoder paths, Ambi Imprint
safety, Ambi Ray room-response behavior, and Fault codec, morph, evolution,
and parameter-sensitivity behavior.

Check the static documentation and advisory GUI conventions with:

```sh
python3 scripts/check-docs.py
./scripts/audit-gui-style.sh
```

If `clap-validator` is installed, validate one or more installed bundles with:

```sh
clap-validator validate --only-failed \
  ~/Library/Audio/Plug-Ins/CLAP/s3g_macro_shred_mono.clap \
  ~/Library/Audio/Plug-Ins/CLAP/s3g_macro_shred.clap \
  ~/Library/Audio/Plug-Ins/CLAP/s3g_24ch_macro_shred.clap \
  ~/Library/Audio/Plug-Ins/CLAP/s3g_fault.clap
```

## Related Projects

[`s3g-mc`](https://github.com/s3g/s3g-mc) is the related REAPER package for
scripts, JSFX, process guides, and multichannel workflow tools. `s3g-dsp` can
be used alongside it or independently in matching macOS + REAPER sessions.

[`s3g-max`](https://github.com/s3g/s3g-max) is the related Max/MSP package for
`.mxo` externals, help patches, V8UI displays, and Max package workflows. It
wraps selected shared C++ DSP engines from this repository where that makes
sense for Max.

[`s3g-rnbo-clap`](https://github.com/s3g/s3g-rnbo-clap) is the separate wrapper
project for RNBO/Max-generated C++ exports. It follows the same CLAP release
workflow but is not required to build or use `s3g-dsp`.

The 3OAFX workflow guide is maintained with `s3g-mc`:
<https://s3g.github.io/s3g-mc/process-guides-3oafx.html>.

## License

BSD-3-Clause for the code in this repository unless a subdirectory states
otherwise. See `LICENSE`.

Third-party libraries used by optional builds retain their own licenses. CLAP
headers are MIT licensed and are fetched at build time unless an existing SDK
path is supplied. WORLD speech vocoder is BSD-style licensed and is fetched
when CLAP builds enable `S3G_ENABLE_WORLD`. Keep third-party notices with
source and binary distributions; see `THIRD_PARTY_NOTICES.md`.

## Attribution

BSD-3-Clause requires preserving the license and copyright notice in source and
binary redistributions.

Attribution is also appreciated for software development, publications,
research, teaching materials, and projects that build on or adapt this package.
See `CITATION.cff`.

Development assistance: OpenAI Codex.
