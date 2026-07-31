# s3g-dsp

A pre-release collection of CLAP audio plugins for multichannel work in
REAPER. `s3g-dsp` is a sibling project to the `s3g-mc` multichannel REAPER
package and the `s3g-max` Max/MSP externals package. It can be used with those
projects or on its own.

Project documentation for `s3g-dsp` is available at
<https://s3g.github.io/s3g-dsp/>.

The project focuses on predictable multichannel routing, compact automation,
and clear control models. Some plugins use topology; others use simpler
relationship controls when that better fits the sound.

The packaged pre-release supports REAPER on Apple silicon Macs only
(arm64—M1, M2, M3, M4, or newer). It does not contain Intel `x86_64`
binaries. Other operating systems and DAWs are unsupported.

## Status

This is a pre-release project. Plugin names, parameters, and saved states may
change.

The current release is **0.5.0-pre** (July 30, 2026). Its Apple-silicon-only
macOS package installs 94 CLAP products, including fixed-width variants for
effects, bus tools, and speaker-array utilities. Installed bundle filenames
follow the same family-first order as the host names, such as
`s3g_ambi_encoder_modal_16.clap` and
`s3g_processor_no_input_mixer_8ch.clap`.

Plugin areas:

- [Multichannel](https://s3g.github.io/s3g-dsp/multichannel.html):
  [effects](https://s3g.github.io/s3g-dsp/multichannel-effects.html),
  fold-down, metering, direct panning, matrix and node mixing, and speaker
  calibration for ordinary channel lanes.
- [Ambisonics](https://s3g.github.io/s3g-dsp/ambisonics.html): separate
  [encoders](https://s3g.github.io/s3g-dsp/ambisonic-encoders.html),
  [decoders](https://s3g.github.io/s3g-dsp/ambisonic-decoders.html),
  [effects](https://s3g.github.io/s3g-dsp/ambisonic-effects.html), and
  [utilities](https://s3g.github.io/s3g-dsp/ambisonic-utilities.html) for
  `ACN/SN3D` workflows, including Processor Ambi Imprint and the order-adaptive
  Ambi Effect DJ Filter, Delay, Pitch, Gain, Resonance Print, Partial Trace,
  Response Trace, and Displacement. The [Listener Mode
  guide](https://s3g.github.io/s3g-dsp/listener-mode.html)
  describes plugins that use their own encoded field as an internal score;
  [Parameter Surface](https://s3g.github.io/s3g-dsp/parameter-surface.html)
  describes preset-cell interpolation and automatable X/Y performance.
- [Instruments](https://s3g.github.io/s3g-dsp/instruments.html): no-input
  feedback, loaded-loop, granular, vector-wavetable, weather, liquid,
  WORLD/voicebank vocal, and stochastic instruments, including the
  eight-channel
  [Processor No Input Mixer](https://s3g.github.io/s3g-dsp/processor-no-input-mixer.html)
  output-only feedback ecology.

The [installation page](https://s3g.github.io/s3g-dsp/installing-plugins.html)
lists the included families and the REAPER routing notes that matter for wide
tracks and true stereo outputs.

## Design

- Reusable DSP lives in `dsp/`; CLAP wrappers live in `plugins/`.
- VOT-compatible 4 x 4 atlases live in `wavetables/vot/`; plugin loader
  examples live in `examples/`.
- The included Ambi Vox test bank uses a documented UTAU-style folder with
  WAV aliases and `oto.ini` timing. Ambi Vox renders up to 16 vocal sources
  into its fixed 64-channel ambisonic bus and retains WORLD spectral and
  aperiodicity analysis for its voice-model controls. Factory starting points
  and `.s3gvox` user presets retain the performance design while keeping source
  WAV and voicebank audio external.
- Processor No Input Mixer is inherently eight-channel and generates its own
  feedback network without an audio input. Its matrix/wiring, mixer, channel,
  safety, AUX, and Parameter Surface pages can be detached without duplicating
  controls; movement, listener-reactive behavior, presets, random energy modes,
  EQ, inserts, and AUX returns remain part of one saved CLAP state.
- Fixed-width CLAP plugins are used where REAPER pin routing needs to be
  predictable.
- Relationship controls keep automation compact where that suits the plugin;
  point, matrix, and calibration tools expose individual controls when needed.
- Ambi Effects use order-adaptive 12-, 20-, or 24-pickup listener bodies while
  keeping encoded-field processing separate from ordinary channel-lane effects.

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

Build CLAP plugins with the release configuration:

```sh
cmake --preset clap -DS3G_ENABLE_WORLD=ON
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

WORLD speech vocoder support is enabled by default and is statically linked
into the release build of Ambi Encoder Vox for its WORLD WAV and voicebank
paths. The explicit option above also corrects an older CMake cache in which
the feature was disabled. The FetchContent revision is pinned for reproducible
builds. Custom builds can disable WORLD with:

```sh
cmake -S . -B build-clap \
  -DS3G_BUILD_CLAP_PLUGIN=ON \
  -DS3G_ENABLE_WORLD=OFF
```

## Voicebank Builder

`s3g Vox Builder` is a macOS companion app for preparing Ambi Encoder Vox
voicebanks from generated material, continuous recordings, or folders of WAV
files. It provides segmentation, WORLD analysis, audition, timing edits, and
UTAU-style export. See [Vox Builder](docs/vox-builder.html) for the complete
workflow.

```sh
cmake --preset apps
cmake --build --preset apps
open "build-apps/apps/vox_builder/s3g Vox Builder.app"
```

The apps preset also builds `s3g No Input Mixer`, a safety-muted standalone
host for the existing No Input Mixer CLAP implementation. Its output section
selects Stereo Autogain, Quad Autogain, or direct eight-channel Core Audio
output. The reusable host architecture is documented in
[`standalone/README.md`](standalone/README.md).

```sh
cmake --build build-apps --target s3g_no_input_mixer_app
cmake --build build-apps --target audit_no_input_mixer_standalone
open "build-apps/apps/no_input_mixer_standalone/s3g No Input Mixer.app"
```

## Install

The packaged pre-release supports REAPER on Apple silicon (`arm64`) Macs. It is
not notarized, so macOS requires one Gatekeeper approval for the installer.

1. Quit REAPER, download the macOS CLAP zip from the
   [GitHub releases page](https://github.com/s3g/s3g-dsp/releases), and unzip
   it.
2. Double-click `Install s3g-dsp CLAPs.command` in the unzipped package.
3. If macOS blocks it, click **OK**, open **System Settings > Privacy &
   Security**, and choose **Open Anyway**. Confirm **Open** and authenticate if
   asked. This option is available for about one hour after the blocked launch.
4. If prompted, allow Terminal to access the downloaded package, then wait for
   installation to finish.
5. Start REAPER and rescan CLAP plugins if the new builds do not appear
   automatically.

See Apple's
[Open a Mac app from an unknown developer](https://support.apple.com/guide/mac-help/mh40616/mac)
for the same approval process. If a binary is unavailable, see
[Building From Source](https://s3g.github.io/s3g-dsp/building-from-source.html).

### Install Location and Safety

```text
~/Library/Audio/Plug-Ins/CLAP/s3g-dsp/
```

On upgrades, the installer moves identity-verified older s3g-dsp
bundles—including copies in the former top-level CLAP location—to a timestamped
backup. Other CLAP products are untouched.

The installer verifies each bundle, removes only `com.apple.quarantine` from
its staged s3g-dsp copies, and preserves other extended attributes. It writes
to the current user's Library without `sudo` and does not change global
Gatekeeper settings or unrelated plugins. Approval applies to the installer;
REAPER should not show a separate Gatekeeper dialog for each plugin afterward.

### Manual Installation

Use this only for a fresh manual install; it cannot migrate older top-level
copies or retire renamed aliases. In Terminal, type `cd` followed by a space,
drag the unzipped `s3g-dsp-macos-clap-0.5.0-pre` folder from Finder into the
Terminal window, and press Return. Then run:

```sh
destination="$HOME/Library/Audio/Plug-Ins/CLAP/s3g-dsp"
mkdir -p "$destination"
for bundle in ./*.clap; do
  installed="$destination/$(basename "$bundle")"
  ditto "$bundle" "$installed"
  xattr -drs com.apple.quarantine "$installed"
done
```

### If a Plugin Does Not Appear

- Confirm that its complete `.clap` bundle is in
  `~/Library/Audio/Plug-Ins/CLAP/s3g-dsp/`.
- Rescan CLAP plugins in REAPER, then restart the host.
- If REAPER shows per-plugin Gatekeeper dialogs or duplicate names, quit REAPER
  and rerun the packaged installer. If the installer itself is blocked, repeat
  the **Open Anyway** step within one hour.

For more detail, see the
[installation guide](https://s3g.github.io/s3g-dsp/installing-plugins.html).

## Pre-release Binaries

The current release asset is `s3g-dsp-macos-clap-0.5.0-pre.zip`. It contains
arm64 binaries for Apple silicon only, not Intel-compatible or universal
binaries. Pre-release macOS CLAP builds are attached to the [GitHub releases
page](https://github.com/s3g/s3g-dsp/releases) for early REAPER testing. Plugin
names, parameter mappings, saved states, and included plugins may change before
a stable release.

The package contains 94 CLAP products, the VOT wavetable library, the Ambi Vox
demo voicebank, release notes, the applicable license notices, and an
`Install s3g-dsp CLAPs.command` installer. Run the packaged installer instead of
drag-copying the bundles so recognized older aliases can be backed up safely.

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

## License

BSD-3-Clause for the code in this repository unless a subdirectory states
otherwise. See `LICENSE`.

The distributed CLAP binaries incorporate MIT-licensed CLAP headers and the
Ambi Vox binary statically incorporates the BSD-style licensed WORLD speech
vocoder. Their required license texts are the complete third-party notice set
for this release; see `THIRD_PARTY_NOTICES.md`.

## Attribution

BSD-3-Clause requires preserving the license and copyright notice in source and
binary redistributions.

Attribution is also appreciated for software development, publications,
research, teaching materials, and projects that build on or adapt this package.
See `CITATION.cff`.
