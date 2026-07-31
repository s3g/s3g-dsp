# s3g-dsp 0.5.0-pre

Apple silicon macOS CLAP pre-release collection for REAPER testing, released
July 30, 2026.

## Asset

- `s3g-dsp-macos-clap-0.5.0-pre.zip`

This archive contains arm64 binaries for Apple silicon Macs (M1, M2, M3, M4,
or newer). It is not compiled for Intel Macs and is not a universal binary.

## Highlights

- Expands the collection from 62 to 94 CLAP products with a canonical bundle
  manifest, identity-verified installer, and automatic backup of recognized
  renamed or retired aliases.
- Adds **s3g Processor No Input Mixer 8ch**, an output-only feedback instrument
  with an interactive signed routing matrix and signal-aware wiring view,
  movement and articulation behaviors, listener-reactive control, three insert
  slots per lane, two feedback-capable AUX processors, per-lane EQ and tuning,
  Parameter Surface interpolation, energy-scaled randomization, presets, MIDI
  control, and detachable page windows.
- Adds and expands ambisonic encoders for point, cloud, terrain, path, ray,
  bilocation, VOT wavetable, voicebank and WORLD resynthesis, wave terrain,
  wind, water, pyrosphere, cryosphere, insect, pulsar, neural ecology,
  stochastic, wrangler, and accelerometer-driven spatial workflows.
- Adds ambisonic DJ filter, delay, pitch, gain, Resonance Print, Partial Trace,
  Response Trace, Displacement, and Imprint effects, plus expanded speaker,
  adaptive, object, stereo, head, and utility decoding workflows.
- Adds Processor Fault, CRCLTR, Macro Shred, shared listener mode, Parameter
  Surface integration, multichannel monitoring, and expanded matrix, panner,
  calibration, and distributed-output tools.
- Includes 28 VOT wavetable banks and the synthetic Ambi Vox demonstration
  voicebank, with third-party attribution and loading instructions in the
  package.
- Expands the documentation to 83 checked HTML pages with native GUI captures,
  responsive layout audits, signal-flow explanations, parameter references,
  and installation guidance.

## Reliability fixes in this replacement build

- Makes CLAP saved state deterministic and resilient to short or buffered host
  streams across the stateful plugin families. This includes synchronizing the
  Speaker Decoder's background layout worker before state capture and fixing
  Stochastic Encoder state and parameter-text conversions.
- Stabilizes Layout, DBAP, LBAP, and VBAP Panner state restoration so the saved
  layout, selected source, and source coordinates are reapplied together. This
  addresses a path that could leave REAPER channel behavior appearing
  inconsistent after using multiple panner instances.
- Corrects named-value and unit conversions found by the CLAP validator across
  encoder, decoder, processor, matrix, mixer, and compact-effect families.
- Preserves a user's output-volume setting when loading presets, matching the
  existing Random behavior, and aligns AED azimuth controls, DSP positioning,
  and spatial diagrams to +90 degrees on the left and -90 degrees on the right.
- Adds regression checks for saved-state reproducibility, buffered state I/O,
  parameter conversions, preset output preservation, and Spectral Spray when a
  wider-channel plugin precedes it on a REAPER track.

## Installation

The packaged pre-release supports REAPER on Apple silicon (`arm64`) Macs. It is
not notarized, so macOS requires one Gatekeeper approval for the installer. The
installer writes only to the current user's Library and does not use `sudo`.

1. Quit REAPER and unzip the archive.
2. Double-click `Install s3g-dsp CLAPs.command`.
3. If macOS blocks it, click **OK**, open **System Settings > Privacy &
   Security**, and choose **Open Anyway**. Confirm **Open** and authenticate if
   asked. This option is available for about one hour after the blocked launch.
4. If prompted, allow Terminal to access the downloaded package, then wait for
   installation to finish.
5. Start REAPER and rescan CLAP plugins if they do not appear automatically.

The installer places the collection in
`~/Library/Audio/Plug-Ins/CLAP/s3g-dsp/`. On upgrades, it moves
identity-verified older s3g-dsp bundles—including copies in the former
top-level CLAP location—to a timestamped backup. Other CLAP products are
untouched.

After approval, the installer verifies each staged bundle, removes only
`com.apple.quarantine`, and preserves other extended attributes. Approval
applies to the installer, so REAPER should not present a separate Gatekeeper
dialog for each plugin afterward.

Pass `--dry-run` from Terminal to preview the exact changes.

## Pre-release notice

This remains pre-release software. Plugin names, parameter mappings, saved-state
compatibility, and the included product set may change before a stable release.
Stable CLAP identifiers are preserved across the current filename migration.
