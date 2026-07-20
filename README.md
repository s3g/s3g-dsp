# s3g-dsp

CLAP audio plugins for multichannel work in REAPER. `s3g-dsp` is a sibling
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

The current macOS package installs 73 CLAP bundles, including fixed-width
variants for several processors, bus tools, and speaker-array utilities.

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
  granular, vector-wavetable, LPC-style vocal, and stochastic instruments.

The [installation page](https://s3g.github.io/s3g-dsp/installing-plugins.html)
lists the included families and the REAPER routing notes that matter for wide
tracks and true stereo outputs.

## Design

- Reusable DSP lives in `dsp/`.
- Plugin wrappers live in `plugins/`.
- VOT-compatible 4 x 4 atlases live in `wavetables/vot/`; plugin loader
  examples live in `examples/`.
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
- Optional docs tooling: an active Node.js LTS release newer than Node.js 20

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
by Ambi Vox Encoder's WORLD WAV source path. It can be disabled with:

```sh
cmake -S . -B build-clap \
  -DS3G_BUILD_CLAP_PLUGIN=ON \
  -DS3G_ENABLE_WORLD=OFF
```

## Voicebank Builder

`tools/voicebank_builder.py` creates a starter UTAU-style voicebank from one
recorded WAV and an ordered phoneme list. It writes sliced WAVs,
`voicebank.json`, and `oto.ini`.

```sh
python3 tools/voicebank_builder.py my-recording.wav \
  --phonemes examples/voicebank-builder/phonemes.txt \
  --name my_voice \
  --output examples/voicebanks/my_voice
```

The automatic slicer uses silence-separated regions when possible. For precise
edits, pass `--markers markers.csv` with `alias,start_ms,end_ms` rows.

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

## Validate

The local smoke test exercises shared DSP code:

```sh
./build-clap/s3g_dsp_smoke
```

The smoke test covers multichannel routing, loop playback, finite output,
bounded peaks, de-click stress, and high-order encoder/decoder paths.

If `clap-validator` is installed, validate installed bundles with:

```sh
clap-validator validate \
  ~/Library/Audio/Plug-Ins/CLAP/s3g_8ch_delay_processor.clap --only-failed
clap-validator validate \
  ~/Library/Audio/Plug-Ins/CLAP/s3g_24ch_delay_processor.clap --only-failed
clap-validator validate \
  ~/Library/Audio/Plug-Ins/CLAP/s3g_mc_to_stereo_autogain.clap --only-failed
clap-validator validate \
  ~/Library/Audio/Plug-Ins/CLAP/s3g_mc_to_quad_autogain.clap --only-failed
clap-validator validate \
  ~/Library/Audio/Plug-Ins/CLAP/s3g_ambi_depth_16.clap --only-failed
clap-validator validate \
  ~/Library/Audio/Plug-Ins/CLAP/s3g_spectral_spray.clap --only-failed
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
