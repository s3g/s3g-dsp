# s3g-dsp

CLAP audio plugins for multichannel work in REAPER. `s3g-dsp` is a sibling
project to the `s3g-mc` multichannel REAPER package. It can be used with
`s3g-mc` or on its own.

Project documentation for `s3g-dsp` is available at
<https://s3g.github.io/s3g-dsp/>.

The project focuses on predictable multichannel routing, compact automation,
and clear control models. Some plugins use topology; others use simpler
relationship controls when that better fits the sound.

The supported development target is macOS + REAPER. Other operating systems and
DAWs are not support targets at this stage.

## Status

This is a pre-release project. Plugin names, parameters, and saved states may
change.

Current plugins:

- `s3g Passthrough Test 24ch`: fixed 24-in/24-out CLAP passthrough/gain plugin.
- `s3g Delay Processor 8ch`: multichannel delay processor with topology controls.
- `s3g Delay Processor 24ch`: wider fixed-width delay build.
- `s3g Loop Processor 8ch`: CLAP instrument plugin for loaded audio loops,
  lane playheads, waveform display, and phase relationship controls.
- `s3g Multi Loop Processor 8ch`: CLAP instrument plugin for combining up to
  four loaded audio files into one eight-lane loop instrument.
- `s3g Ambi Grain Processor`: CLAP instrument plugin for loaded `ACN/SN3D`
  ambisonic media with channel-locked grain events.
- `s3g Macro Delay 8ch` / `s3g Macro Delay 24ch`: compact multichannel delay
  macros with relationship controls.
- `s3g Macro Pitch 8ch` / `s3g Macro Pitch 24ch`: compact multichannel pitch
  macros with the shared Macro control layout.
- `s3g MC to Stereo Autogain`: 128-channel input to true stereo output
  fold-down tool.
- `s3g MC to Quad Autogain`: 128-channel input to quad output fold-down
  with output order `L`, `R`, `RB`, `LB`.
- `s3g Multichannel Meter 64`: fixed 64-in/64-out passthrough meter with
  selectable visual width, level grid, spatial layout view, and energy history.
- `s3g Ambi Energy Visualizer 64`: fixed 64-in/64-out passthrough analyzer
  that projects `ACN/SN3D` channel energy onto a high-resolution heatmap.
- `s3g Ambi Point Encoder`: 16 point-source input to third-order
  `ACN/SN3D` ambisonic output with AED placement, point mixer, physics scenes,
  and Geist-driven bond breaking.
- `s3g Ambi Speaker Decoder 64`: first-through-seventh-order `ACN/SN3D`
  speaker decoder with curated layouts, custom speaker editing, and a stable
  64-channel output bus.
- `s3g Layout Panner`: direct 16-source to 64-speaker spatial panner with
  speaker presets, source mixer, and custom layout design.
- `s3g Ambi Rotate 64`: first-through-seventh-order `ACN/SN3D` field
  rotation utility with yaw, pitch, roll, and order-width trim.
- `s3g Ambi Order / Band 64`: first-through-seventh-order
  order-band gain and weighting utility with Flat, MaxRE, In-phase, and
  Custom modes.
- `s3g Ambi Stereo Decoder`: first-through-seventh-order `ACN/SN3D` to
  true stereo using virtual fields and stereo pickup models.
- `s3g Ambi Head Decoder`: synthetic binaural/transaural stereo decoder
  with no external SOFA files.
- `s3g Spectral Spray 2ch` / `s3g Spectral Spray 8ch`: C++ spectral processors for FFT-bin scatter,
  smear, feedback, hold, freeze, and frequency-window experiments.
- `s3g Shard Scatter`: 2-in/16-out grain-shard spatial scatter effect with
  density, guard, scatter, pitch, feedback, and de-click safeguards.
- `s3g Orbit Delay`: 2-in/16-out orbiting delay effect with spread, focus,
  feedback, damping, and transport-stop de-click behavior.
- `s3g Cascade Taps`: 2-in/16-out stepped tap-ring processor with cascade
  timing, decay, damping, and a `SOFT` control for safer handoff behavior.
- `s3g 3OAFX Delay` / `s3g 3OAFX Filter` / `s3g 3OAFX Gain` /
  `s3g 3OAFX Pitch`: single-effect third-order processors with internal
  24-point virtual speaker masking.

## Design

- Reusable DSP lives in `dsp/`.
- Plugin wrappers live in `plugins/`.
- Fixed-width CLAP plugins are used where REAPER pin routing needs to be
  predictable.
- Per-channel settings are generated inside the plugin. REAPER sees compact
  global controls, not one parameter list per channel.
- 24-channel 3OAFX-specific work is kept separate from general multichannel
  lane processors.
- C++ effects live in `s3g-dsp`; RNBO/Max experiments are useful
  prototypes, but generated RNBO source belongs in the separate wrapper workflow
  rather than this source tree.

## Documentation

GitHub Pages documentation:

```text
https://s3g.github.io/s3g-dsp/
```

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

The smoke test includes Loop Processor and Multi Loop Processor checks for
loop playback stability, source-to-lane mapping, mixed source channel counts,
source rules, source-rate spread bounds, and non-finite/clipping guardrails. It
also exercises newer effect and multichannel fold-down DSP paths for finite
output, bounded peaks, and de-click stress cases.

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
  ~/Library/Audio/Plug-Ins/CLAP/s3g_spectral_spray.clap --only-failed
```

## Related Projects

[`s3g-mc`](https://github.com/s3g/s3g-mc) is the main REAPER package.
`s3g-dsp` is a sibling plugin project for general multichannel effects,
instruments, utilities, and ambisonics-specific 3OAFX tools. It can also be
used independently in matching macOS + REAPER sessions.

[`s3g-rnbo-clap`](https://github.com/s3g/s3g-rnbo-clap) is a separate
experimental wrapper for RNBO/Max-generated C++ exports. It is useful for
testing RNBO-based plugin ideas in the same CLAP workflow, but it is not
required to build or use `s3g-dsp`.

Useful `s3g-mc` references:

- <https://s3g.github.io/s3g-mc/installation.html>
- <https://s3g.github.io/s3g-mc/process-guides-3oafx.html>

## License

BSD-3-Clause for the code in this repository unless a subdirectory states
otherwise. See `LICENSE`.

CLAP headers are MIT licensed and are fetched at build time unless an existing
SDK path is supplied. Keep the CLAP notice with source and binary distributions;
see `THIRD_PARTY_NOTICES.md`.

## Attribution

BSD-3-Clause requires preserving the license and copyright notice in source and
binary redistributions.

Attribution is also appreciated for software development, publications,
research, teaching materials, and projects that build on or adapt this package.
See `CITATION.cff`.

Development assistance: OpenAI Codex.
