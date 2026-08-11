# s3g-dsp 0.7.0-pre Release Checklist

This file records preparation of the release after `v0.6.0-pre`. Source,
metadata, and release gates are ready for the final clean-tree distribution
build; packaging, archive verification, tagging, and publication remain
deliberately pending.

## Release baseline

- Target suite version and tag: `0.7.0-pre` / `v0.7.0-pre`
- Previous release: `v0.6.0-pre` (August 2, 2026)
- Release date: August 11, 2026
- Target platform: Apple silicon macOS (`arm64`), REAPER CLAP support
- Frozen inventory: 119 installable CLAP bundles containing 120 plug-in
  descriptors; two bundles are NIM products and 117 are non-NIM products
- Optional standalone asset: s3g No Input Mixer for macOS 15 or newer

## Scope and product metadata

- [x] Review changes since `v0.6.0-pre` and freeze the 0.7 product scope.
- [x] Include Tracker, both Slicer descriptors, Cartography, Acid, Horizon,
  Membrane Kick, Feedback Shift, Processor Errant, and the complete procedural
  drum family in the standard build and package inventory.
- [x] Freeze the promoted products' CLAP IDs, display names, parameter IDs,
  state versions, and default channel layouts.
- [x] Update `scripts/clap-bundles.tsv`, package count gates, non-NIM evidence
  counts, documentation claims, and legacy-name handling together.
- [x] Remove preview and release-candidate status language from the promoted
  products' public documentation and navigation.

## Structural and build checks

- [x] Documentation audit: 115 HTML pages and 4,461 local references.
- [x] Manifest audit: 119 active bundles and 97 legacy names; source and built
  metadata verified with runtime descriptor-version reconciliation deferred to
  packaging.
- [x] Python unit suite: 37 tests passed.
- [x] Standard Release configure and complete CLAP build passed.
- [x] Release CTest suite: 102 of 102 tests passed.
- [x] Standalone Release configure/build and its 40-test CTest suite passed.
- [x] No Input Mixer processing/state and installer upgrade/rollback audits
  passed.

## Release gates

- [x] The consolidated `release_checks_non_nim` target passed all six phases
  and wrote a passing machine-readable summary.
- [x] ASan/UBSan non-NIM suite: 99 of 99 tests passed; the 28 affected
  Tracker/Slicer/Membrane Kick/Feedback Shift/Errant tests passed again after
  the final repairs.
- [x] Bundle manifest and Objective-C class-symbol audits passed.
- [x] CLAP validation passed for all 117 non-NIM manifest bundles. The runner
  avoids two inapplicable Tracker process-note cases in clap-validator 0.3.2,
  whose output-port query uses the input direction; Tracker's output-only port
  contract was also checked with the corrected 0.4.1 validator and the native
  CLAP smoke test.
- [x] Allocation sweep passed for all 117 non-NIM bundles with zero callback-
  thread allocations or deallocations.
- [x] All ten strict/advisory realtime profiles passed. The 96 kHz maximum-
  density Water/Insect profile remains advisory and allocation-free as
  documented.
- [x] Focused drum-family, Membrane Kick, Acid, Horizon, Tracker, Slicer,
  Feedback Shift, and Processor Errant audits passed.
- [ ] Review the final staged documentation payload, screenshot manifests, PDF
  masters, third-party notices, and license files during package verification.

## Release metadata

- [x] Finalize both release-note files and record August 11, 2026.
- [x] Set `CITATION.cff` to `0.7.0-pre` and August 11, 2026.
- [x] Update public README and documentation download paths to `0.7.0-pre`.
- [x] Confirm the CMake project version is `0.7.0` and both package scripts
  default to `0.7.0-pre`.

## Final distribution, tag, and publication

- [ ] Commit the complete release inputs; final packaging requires a clean
  tree.
- [ ] Run `./scripts/package-macos-clap-prerelease.sh`.
- [ ] Run `./scripts/package-macos-nim-app-prerelease.sh`.
- [ ] Extract and verify both finished archives and their SHA-256 checksums.
- [ ] Create annotated tag `v0.7.0-pre` at the verified packaging revision.
- [ ] Publish the assets, checksums, and release notes together.

## Preparation evidence

Preparation used source revision `a1ae179` plus the release changes represented
by this checklist. Machine-readable evidence is retained under
`build-clap-release/`, including the exact-inventory validator and allocation
reports, the ten-profile realtime summary, and per-phase logs. The final
distribution build has not been run.
