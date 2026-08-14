# s3g-dsp 0.7.0-pre Release Checklist

This file records preparation of the release after `v0.6.0-pre`. Source,
metadata, documentation, screenshots, and release gates are ready for the
final clean-tree distribution build. Commit, tag, and publication remain
deliberately pending.

## Release baseline

- Target suite version and tag: `0.7.0-pre` / `v0.7.0-pre`
- Previous release: `v0.6.0-pre` (August 2, 2026)
- Release date: August 14, 2026
- Target platform: Apple silicon macOS (`arm64`), REAPER CLAP support
- Frozen inventory: 124 installable CLAP bundles containing 125 plug-in
  descriptors; two bundles are NIM products and 122 are non-NIM products
- Optional standalone asset: s3g No Input Mixer for macOS 15 or newer

## Scope and product metadata

- [x] Review changes since `v0.6.0-pre` and freeze the 0.7 product scope.
- [x] Include Tracker, both Slicer descriptors, Cartography, Acid, Horizon,
  Membrane Kick, Feedback Shift, Processor Errant, Processor Fissure, Formant
  Matrix, Processor LF Synth, Processor Stack, Processor Conduit, and the
  complete procedural drum family in the standard build and package inventory.
- [x] Include the expanded Macro Shred circuit and state-migration behavior.
- [x] Freeze the promoted products' CLAP IDs, display names, parameter IDs,
  state versions, and default channel layouts.
- [x] Update `scripts/clap-bundles.tsv`, package count gates, non-NIM evidence
  counts, documentation claims, and legacy-name handling together.
- [x] Keep public documentation centered on operation, signal flow, audible
  process, and saved-state behavior; remove development-history and source-idea
  framing from the promoted products' user guides.

## Documentation and screenshots

- [x] Documentation audit: 121 HTML pages and 4,708 local references.
- [x] Add or revise user guides for Formant Matrix, Processor Fissure,
  Processor LF Synth, Processor Stack, Processor Conduit, and Macro Shred.
- [x] Add top-down SVG signal-flow diagrams for Formant Matrix and Processor Stack.
- [x] Add Processor LF Synth, Processor Stack, Processor Conduit, Processor
  Errant, s3g Slicer, and Drum Echo to the screenshot manifest and regenerate
  their PNG images and PDF masters.
- [x] Regenerate active-signal captures for Formant Matrix, Processor Fissure,
  Processor Feedback Shift, Processor Conduit, and Macro Shred.
- [x] Review the updated screenshots and diagrams at full resolution for
  legibility, active metering, responsive layout, and unclipped controls.
- [x] GUI style audit completed with 23 existing advisory findings outside the
  promoted Processor and Macro Shred interfaces.

## Structural and build checks

- [x] Manifest audit: 124 active bundles and 103 legacy names; source and built
  metadata verified, with runtime descriptor-version reconciliation reserved
  for package verification.
- [x] Python unit suite: 37 of 37 tests passed.
- [x] Standard Release configure and complete CLAP build passed.
- [x] Release CTest suite: 112 of 112 tests passed.
- [x] Standalone Release configure/build and its 43-test CTest suite passed.
- [x] No Input Mixer processing/state, installer upgrade/rollback, and package-
  verifier audits passed.

## Release gates

- [x] The consolidated `release_checks_non_nim` target passed all six phases
  and wrote a passing machine-readable summary.
- [x] ASan/UBSan non-NIM suite: 109 of 109 tests passed.
- [x] Bundle manifest and Objective-C class-symbol audits passed.
- [x] CLAP validation passed for all 122 non-NIM manifest products.
- [x] Allocation sweep passed for all 122 non-NIM products and all 482 measured
  process scenarios with zero callback-thread allocations or deallocations.
- [x] All ten strict/advisory realtime profiles passed. The 96 kHz maximum-
  density Water/Insect profile remains advisory and allocation-free as
  documented.
- [x] Focused Processor LF Synth, Processor Stack, Processor Conduit, and Macro
  Shred circuit/state/GUI/realtime audits passed.
- [x] Processor Fissure parameter text conversion, fracture-control state,
  Repeat behavior, and migration from state versions 4 through 6 were repaired
  and verified by native smoke tests plus clap-validator.

## Release metadata

- [x] Finalize both release-note files and record August 14, 2026.
- [x] Set `CITATION.cff` to `0.7.0-pre` and August 14, 2026.
- [x] Update public README and documentation download paths to `0.7.0-pre`.
- [x] Confirm the CMake project version is `0.7.0` and both package scripts
  default to `0.7.0-pre`.

## Distribution, tag, and publication

- [x] Build non-final dirty-tree rehearsal archives for the CLAP collection and
  No Input Mixer app; staged and freshly extracted verification passed.
- [x] Verify both rehearsal SHA-256 checksum files and inspect their release
  notes, README, license, third-party notices, manifests, installer/provenance,
  and application payloads. The HTML guides and GUI captures retain the
  established website-only distribution contract.
- [ ] Commit the complete release inputs; final packaging requires a clean
  tree.
- [ ] Build and verify the final clean-tree CLAP distribution archive.
- [ ] Build and verify the final clean-tree No Input Mixer app archive.
- [ ] Create annotated tag `v0.7.0-pre` at the verified packaging revision.
- [ ] Publish the assets, checksums, and release notes together.

## Preparation evidence

Preparation used source revision `38fd9c7` plus the reviewed release changes
represented by this checklist. Machine-readable evidence is retained under
`build-clap-release/`, including the exact-inventory validator and allocation
reports, the ten-profile realtime summary, and per-phase logs. Non-final dirty-
tree packages may be built to verify distribution layout; the final archives
must be reproduced from the committed, clean release revision before tagging.
