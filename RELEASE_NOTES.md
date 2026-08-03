# s3g-dsp 0.6.0-pre

Apple silicon macOS CLAP collection and optional No Input Mixer standalone
pre-release, released August 2, 2026.

## Assets

- `s3g-dsp-macos-clap-0.6.0-pre.zip` — 98 CLAP products for REAPER
- `s3g-no-input-mixer-app-macos-arm64-0.6.0-pre.zip` — standalone No Input
  Mixer application

Both archives contain arm64 binaries for Apple silicon Macs (M1, M2, M3, M4,
or newer). They are not compiled for Intel Macs and are not universal binaries.
The standalone app currently requires macOS 15 or newer and is distributed
separately rather than being embedded in the CLAP collection archive.

## Highlights

- Expands the collection to 98 CLAP products, with a canonical product list, an
  identity-verified installer, and automatic backups of recognized renamed or
  retired aliases.
- Adds **s3g Processor No Input Mixer 8ch**, an output-only feedback instrument
  with an interactive signed routing matrix, signal-aware wiring, movement and
  articulation behaviors, listener-reactive control, three inserts per lane, two
  feedback-capable AUX processors, per-lane EQ and tuning, energy-scaled
  randomization, presets, MIDI control, and detachable page windows. The
  optional NIM Gesture utility supports recording controller gestures.
- Adds Ambi Encoder Medium 16 and the mono, 8-channel, and 24-channel Macro
  Fracture family. Ambisonic encoders and instruments now cover point, cloud,
  terrain, path, ray, bilocation, the Modal encoder, VOT wavetable, voicebank
  and WORLD resynthesis, wave terrain, wind, water, pyrosphere, cryosphere,
  insect, pulsar, neural ecology, stochastic, and wrangler workflows.
- Adds ambisonic DJ filter, delay, pitch, gain, Resonance Print, Partial Trace,
  Response Trace, Displacement, and Imprint effects, together with expanded
  speaker, adaptive, object, stereo, head, and utility decoding.
- Adds Processor Fault, CRCLTR, Macro Shred, shared listener mode, Parameter
  Surface integration, multichannel monitoring, and expanded matrix, panner,
  calibration, and distributed-output tools.
- Includes 28 VOT wavetable banks and the synthetic Ambi Vox demonstration
  voicebank, with third-party attribution and loading instructions.
  Documentation now includes 91 checked HTML pages with native GUI captures,
  responsive-layout checks, signal-flow explanations, parameter references,
  and installation guidance.

## Reliability and release checks

- Makes live controls safer in the environmental, terrain, and direct-panner
  families by handing GUI changes to the audio engine instead of directly
  changing active processing. It also fixes an Ambi Vox editor memory error
  found by memory-safety testing. Closing the editor now fully disconnects its
  window before any delayed actions can reach it.
- Improves saved-session reliability across the stateful plug-ins, including
  hosts that save or restore data in small pieces. The 8- and 24-channel
  Spectral Topology versions now save reproducibly while remaining compatible
  with v0.5 sessions. Speaker Decoder waits for its background layout update
  before saving, and Stochastic Encoder saved settings and displayed parameter
  values are corrected.
- Restores Layout, DBAP, LBAP, and VBAP Panner sessions with the saved layout,
  selected source, and coordinates together, avoiding inconsistent REAPER
  channel behavior after using several panner instances. Neural Ecology now
  preserves existing session seeds across its full 32-bit Circuit Seed range,
  and named values and units are corrected across encoder, decoder, processor,
  matrix, mixer, and compact-effect families.
- Removes extremely small output residues from Bilocation, Layout Panner, and
  Orbit Delay. Realtime tests now reject invalid or near-zero “subnormal” output
  samples. Presets preserve the user's output-volume setting, as Random already
  does, and AED controls, processing, and diagrams consistently place +90
  degrees on the left and -90 degrees on the right.
- Automated testing now validates the complete CLAP collection in isolation and
  includes 53 CTest cases, memory and undefined-behavior checks (ASan/UBSan),
  randomized rotation comparisons, environmental filter A/B comparisons,
  fixed-seed fingerprints, non-default v0.5 state fixtures with expected
  parameter snapshots, and dedicated Wrangler, Fault, and Ambi Vox probes.
  Regression tests also cover reproducible and buffered state saving, parameter
  conversions, preset output preservation, and Spectral Spray placed after a
  wider-channel plug-in on a REAPER track.
- Realtime tests cover the core, wide-buffer, Spectral Topology, Spectral Spray,
  Water, and Insect weak points without requesting or releasing memory during
  audio processing. Each supported profile ran at least 10,000 measured blocks
  per scenario; 99 percent completed within 75 percent of the buffer deadline,
  with no more than one percent missing the deadline. Raw maxima and miss counts
  remain available in the JSON evidence. The separate maximum-density
  Water/Insect test at 96 kHz remained stable and allocation-free but did not
  meet this timing limit, so it remains an advisory measurement rather than a
  supported maximum load.
- A separate memory-allocation test covers the exact 96-product non-NIM
  inventory. All 96 products pass CLAP validation, and all 380 tested scenarios
  complete with zero memory allocation or deallocation during audio processing.
  Allocator self-tests and a deliberately allocating test plug-in ensure that
  this check cannot report a false all-zero result.
- Packaging starts from a fresh Release build. Final-release mode rejects dirty,
  stale, or non-Release sources, updates package metadata from the running
  plug-in descriptions before signing, verifies all 98 identities after
  extracting the ZIP, tests the installer in isolation, and writes a SHA-256
  checksum.

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
