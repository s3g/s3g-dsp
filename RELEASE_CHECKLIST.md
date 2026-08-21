# s3g-dsp 0.8.0-pre Release Checklist

This file records preparation of the release after `v0.7.0-pre`. Source,
metadata, documentation, screenshots, and release gates are prepared here for
the final clean-tree distribution build. Commit, tag, and publication remain
deliberately pending.

## Release baseline

- Target suite version and tag: `0.8.0-pre` / `v0.8.0-pre`
- Previous release: `v0.7.0-pre` (August 14, 2026)
- Preparation date: August 21, 2026
- Target platform: Apple silicon macOS (`arm64`), REAPER CLAP support
- Frozen inventory: 127 installable CLAP bundles containing 130 plug-in
  descriptors; two bundles are NIM products and 125 are non-NIM products
- Optional standalone asset: s3g No Input Mixer for macOS 15 or newer

## Scope and product metadata

- [x] Review changes since `v0.7.0-pre` and freeze the 0.8 product scope.
- [x] Promote Sample Player 2/16, Sample Doubles 2, and Sample Wavesets 2/32
  into the standard build, documentation, and package inventory.
- [x] Retain Sample Slicer's stable CLAP identity while publishing the revised
  `s3g Sample Slicer 16` bundle and host name plus recognized legacy aliases.
- [x] Include Tracker step recording, held-note editing, project history,
  runtime planning, activity feedback, and REAPER acceptance support.
- [x] Include Processor Stack score/host-sync work, Membrane Kick MIDI updates,
  and shared direct/quad/stereo ring-output rendering.
- [x] Freeze the promoted products' CLAP IDs, display names, parameter IDs,
  state versions, and default channel layouts.
- [x] Update the active and legacy manifests, package count gate, documentation
  claims, and release notes together.

## Documentation and screenshots

- [x] Add or revise user guides for Sample Player, Sample Doubles, Sample
  Slicer, Sample Wavesets, Tracker, Processor Stack, and ring-output formats.
- [x] Add current native Sample Player, Sample Doubles, Sample Wavesets, Sample
  Slicer, and Tracker captures to the documentation set.
- [x] Reorganize Sample-family navigation and move CRCLTR into the Sample
  section while keeping user-facing Doubles language descriptive and neutral.
- [x] Pass the static documentation audit: 125 HTML pages and 4,825 local
  references checked.
- [x] Complete the GUI style audit: no blocking findings; 23 advisory warnings
  retained for future cleanup.

## Structural and build checks

- [x] Verify all 127 active bundles, 108 legacy names, and 130 runtime
  descriptors from a fresh Release build.
- [x] Pass all 41 Python release-tooling unit tests, including explicit
  multi-descriptor package-inventory coverage.
- [x] Complete the standard Release configure and full CLAP build.
- [x] Pass all 128 Release CTest cases.
- [x] Complete the standalone Release configure/build and pass all 49 CTest
  cases.
- [x] Pass the No Input Mixer processing/state, installer, and package-verifier
  audits.

## Release gates

- [x] Complete all six consolidated `release_checks_non_nim` phases and retain
  their machine-readable evidence. The initial aggregate run caught an
  intermittent Tracker detached-window test; after replacing its immediate
  inspection with a bounded wait for the exact window contract, that serial
  phase passed 124/124 and the focused test passed 30/30 repetitions.
- [x] Pass all 124 ASan/UBSan non-NIM tests.
- [x] Pass the bundle manifest and Objective-C class-symbol audits (166 unique
  Objective-C classes across 127 bundles).
- [x] Pass isolated CLAP validation for all 125 non-NIM bundle identities.
- [x] Pass 125 callback-allocation audits covering 494 process scenarios with
  zero realtime allocation operations.
- [x] Pass all nine strict realtime profiles and retain the documented 96 kHz
  maximum-density Water/Insect advisory evidence.
- [x] Pass focused Sample-family DSP, CLAP, state, GUI, cursor, voice-output,
  and routing tests.
- [x] Pass focused Tracker, Processor Stack, Membrane Kick, and ring-output
  regression tests.

## Release metadata

- [x] Add suite and standalone release notes dated August 21, 2026.
- [x] Set `CITATION.cff` to `0.8.0-pre` and August 21, 2026.
- [x] Update public README and documentation download paths to `0.8.0-pre`.
- [x] Set the CMake project version to `0.8.0` and both package scripts to
  default to `0.8.0-pre`.
- [x] Raise the explicit package inventory gate to 127 bundles.

## Distribution, tag, and publication

- [x] Build and verify non-final dirty-tree rehearsal archives for the CLAP
  collection and No Input Mixer app.
- [x] Verify both rehearsal SHA-256 checksum files and inspect their release
  notes, README, license, third-party notices, manifests, installer/provenance,
  and application payloads.
- [ ] Commit the complete release inputs; final packaging requires a clean
  tree.
- [ ] Build and verify the final clean-tree CLAP distribution archive.
- [ ] Build and verify the final clean-tree No Input Mixer app archive.
- [ ] Create annotated tag `v0.8.0-pre` at the verified packaging revision.
- [ ] Publish the assets, checksums, and release notes together.

## Preparation evidence

Preparation begins from source revision `ac16c03` plus the reviewed release
changes represented by this checklist. Machine-readable evidence is retained
under `build-clap-release/`, `build-clap-sanitize/`, and `build-apps/`.
The initial consolidated summary records the Tracker timing failure it exposed;
the subsequent exact serial rerun and 30-repetition focused run are the passing
evidence for the corrected test, while the other five retained aggregate phases
were already green.
Non-final dirty-tree packages may verify distribution layout; the final
archives must be reproduced from the committed, clean release revision before
tagging.
