# s3g-dsp 0.5.0-pre

Apple silicon macOS CLAP pre-release collection for REAPER testing, released
July 28, 2026.

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

## Installation

The pre-release bundles are ad-hoc signed but are not Apple notarized. Perform
the installation while signed in to a macOS administrator account, or have
administrator credentials available for Gatekeeper approval. The installer
itself writes only to the current user's Library and does not use `sudo`.

1. Quit REAPER and unzip the archive.
2. Double-click `Install s3g-dsp CLAPs.command`.
3. If macOS says the command cannot be opened because it is from an
   unidentified developer, click **OK**.
4. Open **System Settings > Privacy & Security**, scroll to **Security**, and
   click **Open Anyway** for the installer. Confirm **Open** and authenticate
   when macOS asks. The override is offered for about one hour after the failed
   launch attempt.
5. If macOS asks whether Terminal may access the downloaded package, click
   **Allow**. Wait for the installer to finish before opening REAPER.
6. Start REAPER and rescan CLAP plugins if they do not appear automatically.

The installer checks the identity of every bundle before changing the user
CLAP directory, installs canonical product names, and backs up recognized older
aliases under:

```text
~/Library/Application Support/s3g-dsp/CLAP Backups/
```

After the user approves the installer, it copies only the verified s3g-dsp
bundles without propagating their download quarantine attribute. It does not
disable Gatekeeper or alter other CLAP products. A blanket command such as
`sudo xattr` against the complete CLAP folder is therefore neither required nor
recommended.

Pass `--dry-run` from Terminal to preview the exact changes.

## Pre-release notice

This remains pre-release software. Plugin names, parameter mappings, saved-state
compatibility, and the included product set may change before a stable release.
Stable CLAP identifiers are preserved across the current filename migration.
