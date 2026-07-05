# s3g-dsp

Native C++ DSP experiments and CLAP plugin targets for the `s3g-mc`
multichannel REAPER package.

The current focus is reliable multichannel lane processing in REAPER: fixed
width plugins, predictable FX pin behavior, compact host automation, and a
topology layer that generates per-channel DSP variation internally.

## Status

This is an early public development repository. The code is useful for testing
and iteration, but the plugin set and parameter mappings may still change.

Current validated targets:

- `s3g 24ch Passthrough Test`: fixed 24-in/24-out CLAP passthrough/gain plugin.
- `s3g Delay Processor 8ch`: primary topology-driven multichannel delay test.
- `s3g Delay Processor 24ch`: wider delay build for future 3OAFX insert work.
- `s3g Diffusion Mesh` fixed-width wrappers: neutral multichannel diffusion
  test plugins.
- `s3g 3OAFX Send Decoder` / `s3g 3OAFX Return Encoder`: experimental boundary
  plugins for the 3OAFX insert workflow.

The most developed plugin is the Delay Processor. It includes a native macOS
CLAP GUI with a patch matrix, topology view, heatmap, readout, and ANIM/VAR
motion controls.

## Design

- Reusable DSP lives in `dsp/`.
- Plugin wrappers live in `plugins/`.
- Fixed-width CLAP wrappers are preferred for REAPER workflows where the FX pin
  connector needs predictable multichannel I/O.
- Per-channel settings are generated internally. The host sees global effect
  controls and topology/meta controls rather than one automatable parameter per
  lane.
- 24-channel 3OAFX-specific work is kept separate from general multichannel
  lane processors.

## Documentation

The GitHub Pages documentation draft lives in `docs/` and is intended to publish
as:

```text
https://s3g.github.io/s3g-dsp/
```

Useful local docs:

- `docs/index.html`: documentation site home page.
- `docs/delay-processor.html`: Delay Processor overview and topology map.
- `docs/topology_framework.svg`: shared topology framework diagram.
- `docs/3OAFX_insert_workflow.md`: current 3OAFX boundary workflow notes.

## Build

Requirements:

- CMake 3.20 or newer
- A C++17 compiler
- macOS for the current native CLAP GUI/plugin bundle targets

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
```

## Relationship to s3g-mc

`s3g-mc` is the main REAPER package. `s3g-dsp` is a sibling repository for
native DSP/plugin work that may support `s3g-mc` workflows, especially
multichannel lane effects and future 3OAFX insert chains.

## License

BSD-3-Clause for the code in this repository unless a subdirectory states
otherwise. See `LICENSE`.

CLAP headers are MIT licensed and are fetched at build time unless an existing
SDK path is supplied. Keep the CLAP notice with source and binary distributions;
see `THIRD_PARTY_NOTICES.md`.
