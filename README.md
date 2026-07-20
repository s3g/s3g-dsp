# s3g-dsp

CLAP audio plugins for multichannel work in REAPER. `s3g-dsp` is a sibling
project to the `s3g-mc` multichannel REAPER package and the `s3g-max` Max/MSP
externals package. It can be used with those projects or on its own.

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

The current macOS CLAP package includes more than sixty plugin bundles,
including fixed-width variants for several calibration and bus utilities.

Current plugins:

- `s3g Passthrough Test 24ch`: fixed 24-in/24-out CLAP passthrough/gain plugin.
- `s3g Delay Processor 8ch/24ch`: multichannel delay processors with topology
  controls.
- `s3g Loop Processor 8ch`: CLAP instrument plugin for loaded audio loops,
  lane playheads, waveform display, and phase relationship controls.
- `s3g Multi Loop Processor 8ch`: CLAP instrument plugin for combining up to
  four loaded audio files into one eight-lane loop instrument.
- `s3g Ambi VOT Encoder 64`: 64-voice vector-wavetable instrument with editable
  U/V scoring, scale and harmonic tuning, AED motion, and
  first-through-seventh-order `ACN/SN3D` output. A reproducible twenty-four-atlas
  library includes tonal, dynamic, algorithmic, and heavy-glitch surfaces.
- `s3g Ambi Vox Encoder 64`: Ambi VOT-derived vocal vector-wavetable
  instrument with a default black-metal source bank and a reproducible VOX
  atlas library for choir, throat, creature, and rasp-driven source fields.
- `s3g Ambi Stochastic Encoder 64`: 64-voice instrument with second-order
  stochastic pressure walks, four-generator selection, autonomous time-fields,
  topology-driven AED placement, MIDI/free operation, and first-through-seventh-
  order `ACN/SN3D` output.
- `s3g Ambi Environment Generator 64`: procedural field-recording instrument
  combining spatial wind, rain, water, fire, insect, machine, and ambient-air
  layers with first-through-seventh-order `ACN/SN3D` output.
- `s3g Ambi Imprint 64`: fixed 64-channel directional convolution processor
  for `.s3gimprint` architectural, natural, open, or imaginary responses
  exported directly by `s3g-mc Imprint Sketch`, with an orbitable geometry
  view, a bundled atlas of fourteen editable precomposed spaces, and first-
  through-seventh-order `ACN/SN3D` operation.
- `s3g Ambi Grain Processor`: CLAP instrument plugin for loaded `ACN/SN3D`
  ambisonic media with channel-locked grain events.
- `s3g Macro Delay 8ch/24ch`: compact multichannel delay macros with
  relationship controls.
- `s3g Macro Pitch 8ch/24ch`: compact multichannel pitch macros with the
  shared Macro control layout.
- `s3g Buffer Processor 8ch`: multichannel buffer processor for skip, reverse,
  crush, error-path, and corrupted-window experiments.
- `s3g Wave Geometry Processor 8ch`: topology-shaped wave processor for
  folding, rectification, clipping, sample-step motion, bit collapse, a
  post-processing scope view, and dual tape-head loop playback.
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
- `s3g Ambi Cloud Encoder 64`: 64-source input to first-through-seventh-order
  `ACN/SN3D` ambisonic output with one to four cloud centers, spread, jitter,
  drift, and source-cloud distribution controls.
- `s3g Ambi Path Encoder 64`: 64-source input to first-through-seventh-order
  `ACN/SN3D` ambisonic output driven by drawable path trajectories, with JSON
  path save/recall and SVG import/export for interchange.
- `s3g Ambi Speaker Decoder 64`: first-through-seventh-order `ACN/SN3D`
  speaker decoder with curated layouts, custom speaker editing, and a stable
  64-channel output bus. The speaker field camera view is saved with the host
  project.
- `s3g Ambi Object Decoder 64`: hybrid ambisonic speaker decoder that blends a
  normal decoded field with a directional object-panning path using VBAP, LBAP,
  or DBAP.
- `s3g Ambi Adaptive Decoder 64`: dual-band ambisonic speaker decoder that
  blends diffuse and focused decode paths using confidence and transient cues.
- `s3g Ambi Sub Decoder`: ambisonic low-frequency decoder for deriving up to
  eight sub feeds from `ACN/SN3D` input.
- `s3g Layout Panner`: direct 64-source to 64-speaker spatial panner with
  speaker presets, source mixer, custom layout design, inside-source modes,
  and project-saved field views.
- `s3g DBAP Panner`: distance-based amplitude panner variant using the same
  curated speaker layouts, inside-source modes, and custom layout design
  surface as Layout Panner.
- `s3g LBAP Panner`: layer/local-lobe amplitude panner variant with
  topology-aware speaker relationships, imaginary solver support, and 2D
  elevation-as-level behavior.
- `s3g VBAP Panner`: vector-base amplitude panner variant with solver
  topology overlays, imaginary speakers for weak hulls, pole-cap blending, and
  2D elevation-as-level behavior.
- `s3g Sub Crossover`: layout-aware subwoofer crossover/send utility for up to
  eight subwoofer channels.
- `s3g Array HPF 16/26/32/64`: post-decoder high-pass filters for main speaker
  arrays.
- `s3g Array Delay 16/26/32/64`: per-channel speaker calibration delay
  utilities for arrival-time alignment.
- `s3g Array Trim 16/26/32/64`: per-channel speaker trim, mute, and polarity
  utilities for speaker-system calibration.
- `s3g Ambi Rotate 64`: first-through-seventh-order `ACN/SN3D` field
  rotation utility with yaw, pitch, roll, and order-width trim.
- `s3g Ambi Depth 16`: third-order `ACN/SN3D` field depth utility with
  order shaping, air damping, and a distance-tail layer.
- `s3g Ambi Group Matrix 64/128`: bus-level matrix mixers for four or eight
  lane-locked 3OA feeds.
- `s3g Ambi Node Bus Mixer 128`: fixed 3OA bus mixer for up to eight
  16-channel ambisonic feeds using node positions and a movable cursor instead
  of a crosspoint matrix. Nodes default to a flat Z-locked mix plane.
- `s3g Ambi Group Rotate 64/128`: bus-level rotation utilities for four or
  eight lane-locked 3OA feeds.
- `s3g Ambi Group Depth 64/128`: bus-level depth/order-shaping utilities for
  four or eight lane-locked 3OA feeds.
- `s3g Ambi Order / Band 64`: first-through-seventh-order
  order-band gain and weighting utility with Flat, MaxRE, In-phase, and
  Custom modes.
- `s3g Ambi Stereo Decoder`: first-through-seventh-order `ACN/SN3D` to
  true stereo using virtual fields and stereo pickup models.
- `s3g Ambi Head Decoder`: synthetic binaural/transaural stereo decoder
  with no external SOFA files.
- `s3g Spectral Spray 2ch/8ch`: C++ spectral processors for FFT-bin scatter,
  smear, feedback, hold, freeze, and frequency-window experiments.
- `s3g Spectral Topology Processor 8ch/24ch`: topology-shaped spectral
  processor with lane patching, topology controls, broken-bin/repeat layers,
  and a grayscale one-second sonogram view.
- `s3g Shard Scatter`: 2-in/16-out grain-shard spatial scatter effect with
  density, guard, scatter, pitch, feedback, and de-click safeguards.
- `s3g Orbit Delay`: 2-in/16-out orbiting delay effect with spread, focus,
  feedback, damping, and transport-stop de-click behavior.
- `s3g Cascade Taps`: 2-in/16-out stepped tap-ring processor with cascade
  timing, decay, damping, and a `SOFT` control for safer handoff behavior.
- `s3g Group Matrix 32/64`: general group matrix mixers for 32- or 64-channel
  buses.
- `s3g Node Bus Mixer 128`: general 128-channel node/cursor mixer for
  routing source blocks into a layout-aware output bed, with optional 3D node
  placement when Z lock is disabled.
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
- Spatial plugin camera state is saved with the plugin state where it is part
  of the editing workflow. For example, a top-view panner or decoder display
  should reopen that way with the project.
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

[`s3g-rnbo-clap`](https://github.com/s3g/s3g-rnbo-clap) is a separate
experimental wrapper for RNBO/Max-generated C++ exports. It is useful for
testing RNBO-based plugin ideas in the same CLAP workflow, but it is not
required to build or use `s3g-dsp`.

The 3OAFX workflow guide is maintained with `s3g-mc`:
<https://s3g.github.io/s3g-mc/process-guides-3oafx.html>.

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
