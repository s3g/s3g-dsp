# s3g-dsp 0.6.0-pre

Apple silicon macOS CLAP pre-release collection for REAPER testing, released
August 2, 2026.

## Asset

- `s3g-dsp-macos-clap-0.6.0-pre.zip`

This archive contains arm64 binaries for Apple silicon Macs (M1, M2, M3, M4,
or newer). It is not compiled for Intel Macs and is not a universal binary.
The separately built No Input Mixer standalone app is not included in this
CLAP collection archive.

## Highlights

- Expands the collection to 98 CLAP products with a canonical bundle
  manifest, identity-verified installer, and automatic backup of recognized
  renamed or retired aliases.
- Adds Ambi Encoder Medium 16, the mono/8-channel/24-channel Macro Fracture
  family, and the optional NIM Gesture controller utility.
- Adds **s3g Processor No Input Mixer 8ch**, an output-only feedback instrument
  with an interactive signed routing matrix and signal-aware wiring view,
  movement and articulation behaviors, listener-reactive control, three insert
  slots per lane, two feedback-capable AUX processors, per-lane EQ and tuning,
  energy-scaled randomization, presets, MIDI control, and detachable page
  windows.
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
- Expands the documentation to 89 checked HTML pages with native GUI captures,
  responsive layout audits, signal-flow explanations, parameter references,
  and installation guidance.

## Reliability and release checks

- Moves GUI-originated parameter and state changes in the environmental,
  terrain, and direct-panner families through audio-thread-owned publication
  paths instead of mutating active DSP objects from Cocoa callbacks.
- Fixes an Ambi Vox editor-lifetime use-after-free found by ASan/UBSan. Closing
  the editor now detaches its retained Cocoa view before later main-thread
  callbacks can run.
- Adds isolated full-manifest CLAP validation, a populated CTest suite,
  ASan/UBSan builds, randomized rotation equivalence, environmental SVF A/B
  regression checks, fixed-seed fingerprints, non-default v0.5 state fixtures
  with expected parameter snapshots, and previously orphaned Wrangler, Fault,
  and Ambi Vox probes.
- Adds strict non-allocating realtime release profiles for the core,
  wide-buffer, Spectral Topology, Spectral Spray, Water, and Insect weak points.
  The supported profiles pass at least 10,000 measured blocks per scenario,
  with p99 processing time no greater than 75 percent of the buffer deadline
  and a deadline-miss rate no greater than one percent. Raw maxima and miss
  counts remain in the JSON evidence. The separate 96 kHz Water/Insect
  maximum-density measurement stayed finite and allocation-free but did not
  meet that strict timing threshold, so it remains advisory rather than a
  claimed supported maximum load.
- Adds a separate allocation-only gate over the exact 96-plugin non-NIM
  inventory, backed by allocator-hook self-tests and a known-bad allocating
  CLAP so an inactive probe cannot create a false all-zero pass.
- The complete non-NIM gate passes 53 CTest cases, CLAP validation for all 96
  non-NIM products, and an allocator-probed 96-product, 380-scenario sweep with
  zero process-time allocation or deallocation operations.
- Packages only a freshly rebuilt Release artifact tree. The packager rejects
  dirty final sources and stale or non-Release trees, synchronizes staged
  component metadata from runtime descriptors before signing, verifies all 98
  identities after archive extraction, performs an isolated installer dry run,
  and writes a SHA-256 checksum.
- Makes CLAP saved state deterministic and resilient to short or buffered host
  streams across the stateful plugin families. This includes deterministic
  8- and 24-channel Spectral Topology serialization with v0.5 state
  compatibility, synchronizing the Speaker Decoder's background layout worker
  before state capture, and fixing Stochastic Encoder state and parameter-text
  conversions.
- Stabilizes Layout, DBAP, LBAP, and VBAP Panner state restoration so the saved
  layout, selected source, and source coordinates are reapplied together. This
  addresses a path that could leave REAPER channel behavior appearing
  inconsistent after using multiple panner instances.
- Aligns Neural Ecology's Circuit Seed parameter with its intentional full
  32-bit DSP and saved-state range, preserving released session seeds instead
  of folding them into the former 16-bit descriptor range.
- Corrects named-value and unit conversions found by the CLAP validator across
  encoder, decoder, processor, matrix, mixer, and compact-effect families.
- Flushes subnormal output residues in Bilocation, Layout Panner, and Orbit
  Delay paths, and extends the realtime audit to reject non-finite or subnormal
  output samples.
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
