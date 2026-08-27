# s3g No Input Mixer 0.9.0-pre

Apple silicon macOS standalone pre-release, prepared August 27, 2026.

## Asset

- `s3g-no-input-mixer-app-macos-arm64-0.9.0-pre.zip`

## Highlights

- Uses the No Input Mixer's SAFETY renderer directly for stereo-ring, quad-ring, and eight-channel output, removing the duplicate embedded Output Autogain processors and editor. The persistent ROUTE selector and SAFETY page now control one saved format; legacy app modes and the active output rotation migrate on first launch.
- Retains safe-muted launch, hardware-aware output-bank selection, selectable CoreMIDI input and feedback destinations, embedded NIM Gesture, and the identity-verified user-level installer.
- Rebuilds the standalone application and distribution metadata against the s3g-dsp 0.9.0 release baseline.

---

# s3g No Input Mixer 0.8.0-pre

Apple silicon macOS standalone pre-release, released August 21, 2026.

## Asset

- `s3g-no-input-mixer-app-macos-arm64-0.8.0-pre.zip`

## Highlights

- Updated the embedded No Input Mixer core and its saved-state migration while preserving the safe direct eight-channel default for existing sessions.
- Retained safe-muted launch, Stereo and Quad Autogain monitoring, direct eight-channel output, selectable CoreMIDI input and feedback destinations, embedded NIM Gesture, and the identity-verified user-level installer.

---

# s3g No Input Mixer 0.7.0-pre

Apple silicon macOS standalone pre-release, released August 14, 2026.

## Asset

- `s3g-no-input-mixer-app-macos-arm64-0.7.0-pre.zip`

## Highlights

- Adds an independently selectable CoreMIDI destination for NIM Gesture key feedback, with state refresh and heartbeat behavior for record, playback, clear, and cancel controls.
- Retains the safe-muted launch, embedded No Input Mixer and NIM Gesture DSP, stereo/quad/direct output choices, and identity-verified user-level installer from the previous pre-release.

---

# s3g No Input Mixer 0.6.0-pre

Apple silicon macOS standalone pre-release, released August 2, 2026.

## Asset

- `s3g-no-input-mixer-app-macos-arm64-0.6.0-pre.zip`

The application is an arm64-only build for Apple silicon Macs and currently requires macOS 15 or newer. It is distributed separately from the s3g-dsp CLAP collection and does not require REAPER or an installed copy of the CLAP plugin.

## Highlights

- Embeds the same No Input Mixer DSP used by `s3g Processor No Input Mixer 8` so plugin-side DSP changes remain shared with the standalone application.
- Provides Stereo Autogain, Quad Autogain, and direct eight-channel Core Audio output, including selectable output-channel banks on larger interfaces.
- Opens monitoring safe-muted. The output device, output mode, and channel bank can be checked before selecting `AUDIO ON`; `PANIC` remains available in the persistent output strip.
- Includes optional CoreMIDI source selection and the same E16, BU16, NRPN, note, velocity, and LED-feedback protocol as the CLAP processor.
- Embeds NIM Gesture for recording parameter motion. Unsaved loops are cleared on quit; portable `.nimgesture` sessions are loaded and played only when the user explicitly requests them.
- Includes opt-in realtime diagnostics for callback timing, Core Audio overloads and discontinuities, embedded CLAP errors, non-finite output, and MIDI drops.

## Installation

Use the included `Install s3g No Input Mixer.command`. It verifies the app, installs it to the current user's `~/Applications` folder without `sudo`, and moves an identity-verified prior copy to a timestamped backup. It does not modify the separately installed s3g-dsp CLAP collection.

The application and installer are not Apple notarized. See `README.txt` in the archive for the Gatekeeper approval sequence, first-launch audio safety steps, manual installation, updates, troubleshooting, and removal.

## Pre-release notice

This remains pre-release software. Application preferences, controller mappings, saved processor state, and standalone session formats may change before a stable release.
