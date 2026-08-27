# s3g-dsp 0.9.0-pre Release Checklist

This file records preparation of the release after `v0.8.0-pre`. Source,
metadata, documentation, screenshots, and release gates are prepared here for
the final clean-tree distribution build. Commit, tag, and publication remain
deliberately pending.

## Release baseline

- Target suite version and tag: `0.9.0-pre` / `v0.9.0-pre`
- Previous release: `v0.8.0-pre` (August 21, 2026)
- Preparation date: August 27, 2026
- Target platform: Apple silicon macOS (`arm64`), REAPER CLAP support
- Frozen inventory: 119 installable CLAP bundles containing 125 plug-in
  descriptors; two bundles are NIM products and 117 are non-NIM products
- Recognized upgrade inventory: 211 legacy bundle names
- Optional standalone asset: s3g No Input Mixer for macOS 15 or newer

## Scope and product metadata

- [x] Review changes since `v0.8.0-pre` and freeze the 0.9 product scope.
- [x] Promote Sample Motion 2/32, Sample Lanes 2/32, Sample Grains 2/32,
  Sample Rings 8, Effect Delay Field 16, and Matrix Upmix 64 into the standard
  build, documentation, and package inventory.
- [x] Publish Sample Circulator 2 as the revised CRCLTR product while retaining
  its stable CLAP identity and first ten parameter identifiers.
- [x] Consolidate Array HPF, Delay, and Trim into Array Calibrate 16/26/32/64,
  and retain the retired bundle identities in the legacy manifest.
- [x] Retire Processor Loop, Processor Multi Loop, Shard Scatter, Orbit Delay,
  Cascade Taps, and the 24-channel passthrough from the active inventory.
- [x] Normalize public bundle filenames and host names around explicit channel
  counts without changing stable CLAP identifiers.
- [x] Freeze the promoted products' CLAP IDs, display names, parameter IDs,
  state versions, and default channel layouts.
- [x] Reconcile the active and legacy manifests, package count gate,
  documentation claims, and release notes.

## Documentation and screenshots

- [x] Add or revise user guides for Sample Motion, Sample Lanes, Sample Grains,
  Sample Rings, Sample Slicer, Sample Circulator, Delay Field, Matrix Upmix,
  Array Calibrate, and the renamed channel-width products.
- [x] Add or refresh the corresponding native GUI captures.
- [x] Pass the static documentation audit: 130 HTML pages and 4,977 local
  references checked.
- [x] Complete the GUI style audit with no blocking findings; retain 41
  advisory warnings for future cleanup.

## Structural and build checks

- [x] Verify all 119 active bundles, 211 legacy names, and 125 runtime
  descriptors from a fresh Release build.
- [x] Pass all 41 Python release-tooling unit tests.
- [x] Complete the standard Release configure and full CLAP build.
- [x] Pass all 151 Release CTest cases.
- [x] Rebuild the standalone Release tree and pass all 57 CTest cases after
  completing the No Input Mixer output-routing refactor.
- [x] Pass the No Input Mixer processing/state, installer, and package-verifier
  audits after that refactor.

## Release gates

- [ ] Complete all consolidated `release_checks_non_nim` phases and retain
  their machine-readable evidence.
- [x] Pass all 146 ASan/UBSan non-NIM tests after correcting Cascade's wrapped
  interpolation boundary.
- [x] Pass the source/built bundle manifest audit with descriptor-version
  synchronization deferred to packaging, and verify 156 unique Objective-C
  classes across the 119 active bundles.
- [x] Pass isolated CLAP validation for every non-NIM bundle and descriptor.
- [x] Pass the callback-allocation audits with zero realtime allocation
  operations.
- [x] Pass all strict realtime profiles and retain the documented 96 kHz
  maximum-density advisory evidence. The first environmental 48 kHz pass had
  one scheduler-sensitive p99 outlier; its exact Water rerun and the complete
  two-plugin profile rerun both passed.
- [x] Pass focused Sample-family, Sample Circulator, Delay Field, Matrix Upmix,
  Array Calibrate, state, GUI, output-allocation, and routing regressions.

## Release metadata

- [x] Add suite and standalone release notes dated August 27, 2026 while
  retaining the 0.8 historical entries.
- [x] Set `CITATION.cff` to `0.9.0-pre` and August 27, 2026.
- [x] Update public README and documentation download paths to `0.9.0-pre`.
- [x] Set the CMake project version to `0.9.0` and both package scripts to
  default to `0.9.0-pre`.
- [x] Set the explicit package inventory gate to 119 bundles and 125 runtime
  descriptors.

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
- [ ] Create annotated tag `v0.9.0-pre` at the verified packaging revision.
- [ ] Publish the assets, checksums, and release notes together.

## Preparation evidence

Preparation begins from `v0.8.0-pre`, the subsequent changes committed through
`5330e61`, and the release metadata changes represented by this checklist.
Machine-readable evidence is retained under `build-clap-release/`,
`build-clap-sanitize/`, and `build-apps/`. Non-final dirty-tree packages may
verify distribution layout; final archives must be reproduced from the
committed, clean release revision before tagging. The No Input Mixer standalone
output-routing refactor is implemented across its engine, app, diagnostic,
tests, documentation, and package instructions; all 57 registered standalone
tests and the separate embedded-chain smoke test pass.
