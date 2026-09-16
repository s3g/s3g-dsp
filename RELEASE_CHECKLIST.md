# s3g-dsp 0.10.0-pre Release Checklist

Release preparation, not a publication or platform-certification record.
Previous public release: `v0.9.0-pre` (August 27, 2026).

## Scope

- Apple silicon macOS: 122 CLAP bundles / 129 runtime descriptors.
- Experimental Windows x64: 121 CLAP files / 128 runtime descriptors.
- Ambi Energy is Mac-only. No standalone applications in these downloads.
- Mac release preset enables all completed VSTGUI migrations and Tracker's
  portable shell/pages. Windows uses a native MSVC Release build, including
  the compiler-specific fixes merged through PR #1.
- Windows 10 / REAPER is the testing baseline; Windows 11 is not yet validated.
  No hardware minimum is inferred from the older test laptop.

## Source and metadata

- [x] Review product inventory and changes since `v0.9.0-pre`.
- [x] Prepare `0.10.0` project / `0.10.0-pre` archive versions and release notes.
- [x] Update installation/build guidance and experimental Windows labeling.
- [x] Correct Mac packaging gates to 122 bundles / 129 descriptors.
- [x] Keep private Windows integration/review/handoff notes in ignored `.notes/`.
- [ ] Commit and push reviewed release inputs; final packaging requires a clean
  tree. Adjust the citation date if publication moves beyond September 16.

## Validation

- [x] Build the Mac release preset; pass 47 Python tooling tests and the static
  documentation audit (131 pages / 4,951 local references).
- [x] Run Mac regressions: 299/300 passed. The remaining
  `s3g_tracker_workspace_layout_appkit_tests` failure reproduces the same five
  assertions observed on both sides of the earlier merge review. The portable
  Tracker GUI tests pass; this is not an all-green regression result.
  The optional CLAP MIDI-adapter regression was then enabled, built and passed
  separately (one additional test).
- [x] Verify the Mac rehearsal package: 122 bundles / 129 descriptors,
  signatures, installer dry-run, archive extraction and checksum. All 121
  converted editors include Fira Code; Ambi Energy retains its native editor.
- [ ] Resolve or explicitly accept/document the existing AppKit test failure
  before publication; do not silently treat it as a pass.
- [ ] Run the manually dispatched `Experimental Windows prerelease` workflow
  from the exact committed release revision. All configured plugins must build;
  native Windows/portable Tracker regressions and packaging must pass.
- [ ] Test the exact extracted Windows archive in REAPER: discovery, GUI/DPI,
  menus, file import, Unicode paths, project/preset recall, automation and MIDI.
- [ ] Test representative Mac projects with the exact candidate archive.
- [ ] Review current realtime/allocation/validator/sanitizer evidence and
  document any remaining exceptions; old reports are not fresh release passes.

## Publication

- [ ] Rebuild final Mac and Windows archives from the same clean commit.
- [ ] Verify both ZIP checksums and retain test evidence separately from assets.
- [ ] Create `v0.10.0-pre` at that revision and mark the GitHub release prerelease.
- [ ] Publish the two ZIPs and their `.sha256` files with the 0.10 release notes.
  The Windows asset filename and release body must both say experimental.

Non-final packages are explicitly rehearsals. Build scripts do not install
plug-ins, create tags, or publish a GitHub release. Internal engineering notes,
private handoff kits, machine-specific logs and source snapshots are not release
assets. Preserve earlier archives and installed binaries for rollback.
