# s3g-dsp

CLAP audio plugins for multichannel work in REAPER. `s3g-dsp` is a sibling
project to the `s3g-mc` multichannel REAPER package. It can be used with
`s3g-mc` or on its own.

Project documentation for `s3g-dsp` is available at
<https://s3g.github.io/s3g-dsp/>.

The current focus is predictable multichannel routing, compact automation, and
clear multichannel control models. Some plugins use topology; others use
simpler relationship controls when that better fits the sound.

The supported development target is macOS + REAPER. Other operating systems and
DAWs are not support targets at this stage.

## Status

This is a pre-release project. Plugin names, parameters, and saved states may
change.

Current plugins:

- `s3g 24ch Passthrough Test`: fixed 24-in/24-out CLAP passthrough/gain plugin.
- `s3g Delay Processor 8ch`: main topology-driven delay processor.
- `s3g Delay Processor 24ch`: wider delay build for 24-channel insert work.
- `s3g Loop Processor 8ch`: CLAP instrument plugin for loaded audio loops,
  lane playheads, waveform display, and phase relationship controls.
- `s3g Macro Delay 8ch`: compact multichannel delay macro with relationship
  controls.
- `s3g Macro Pitch 8ch`: compact multichannel pitch macro with the shared Macro
  control layout.
- `s3g MC to Stereo Autogain`: 128-channel input to true stereo output
  fold-down prototype.
- `s3g Multichannel Meter 64`: fixed 64-in/64-out passthrough meter with
  selectable visual width, level grid, spatial layout view, and energy history.
- `s3g Ambisonic Energy Visualizer 64`: fixed 64-in/64-out passthrough analyzer
  that projects `ACN/SN3D` channel energy onto a high-resolution heatmap.
- `s3g 3OAFX Point Encoder`: 16 point-source input to third-order
  `ACN/SN3D` ambisonic output with AED placement, point mixer, physics scenes,
  and Geist-driven bond breaking.
- `s3g 3OAFX Speaker Decoder 64`: first-through-seventh-order `ACN/SN3D`
  speaker decoder with curated layouts, custom speaker editing, and a stable
  64-channel output bus.
- `s3g Layout Panner`: direct 16-source to 64-speaker spatial panner with
  speaker presets, source mixer, and custom layout design.
- `s3g 3OAFX Send Decoder` / `s3g 3OAFX Return Encoder`: work-in-progress
  boundary plugins for the 3OAFX insert workflow.

Delay Processor is the main topology effect. Loop Processor is the first
loaded-audio instrument. MC to Stereo Autogain is a practical fold-down tool
for auditioning multichannel work in stereo.

## Design

- Reusable DSP lives in `dsp/`.
- Plugin wrappers live in `plugins/`.
- Fixed-width CLAP plugins are used where REAPER pin routing needs to be
  predictable.
- Per-channel settings are generated inside the plugin. REAPER sees compact
  global controls, not one parameter list per channel.
- 24-channel 3OAFX-specific work is kept separate from general multichannel
  lane processors.

## Documentation

GitHub Pages documentation:

```text
https://s3g.github.io/s3g-dsp/
```

Useful local docs:

- `docs/index.html`: documentation site home page.
- `docs/building-from-source.html`: source build, local install, and validation notes.
- `docs/installing-plugins.html`: installing packaged pre-release CLAP bundles.
- `docs/effects.html`: effect plugin index.
- `docs/3oafx.html`: 3OAFX and ambisonics-specific plugin index.
- `docs/instruments.html`: instrument plugin index.
- `docs/topology-framework.html`: shared topology framework overview.
- `docs/delay-processor.html`: Delay Processor overview and topology map.
- `docs/loop-processor.html`: Loop Processor workflow and control reference.
- `docs/mc-to-stereo-autogain.html`: MC to Stereo Autogain fold-down reference.
- `docs/multichannel-meter.html`: Multichannel Meter view modes and routing notes.
- `docs/ambisonic-energy-visualizer.html`: Ambisonic Energy Visualizer reference.
- `docs/3oafx-point-encoder.html`: 3OAFX Point Encoder reference.
- `docs/3oafx-speaker-decoder.html`: 3OAFX Speaker Decoder reference.
- `docs/3oafx-layout-panner.html`: Layout Panner reference.
- `docs/gui-style-guide.md`: working style guide for custom plugin GUIs.
- `docs/*_gui.png`: plugin GUI screenshots used by the documentation pages.
- `docs/topology_framework.svg`: shared topology framework diagram.
- `docs/topology_effect_map.svg`: generic topology-to-effect mapping diagram.
- `docs/topology_heatmap_example.svg`: topology heatmap visualization example.
- `docs/3oafx-insert-workflow.html`: current 3OAFX boundary workflow notes.

## Build

Requirements:

- CMake 3.20 or newer
- A C++17 compiler
- macOS for the current CLAP GUI/plugin bundle targets
- REAPER as the primary tested DAW/host
- Optional docs tooling: use an active Node.js LTS release newer than Node.js 20 if a docs build reports that Node.js 20 is deprecated

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

If `clap-validator` is installed, validate installed bundles with:

```sh
clap-validator validate \
  ~/Library/Audio/Plug-Ins/CLAP/s3g_8ch_delay_processor.clap --only-failed
clap-validator validate \
  ~/Library/Audio/Plug-Ins/CLAP/s3g_24ch_delay_processor.clap --only-failed
clap-validator validate \
  ~/Library/Audio/Plug-Ins/CLAP/s3g_mc_to_stereo_autogain.clap --only-failed
```

## Relationship to s3g-mc

`s3g-mc` is the main REAPER package. `s3g-dsp` is a sibling plugin project for
general multichannel effects, instruments, utilities, and ambisonics-specific
3OAFX tools. It can also be used independently in matching macOS + REAPER
sessions.

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
