# Sample GUI audit remediation

Reference: the Sample guides/READMEs and original Cocoa implementations.
Scope: fix the identified gaps in migrated editors; Sample Slicer and Processor
Ambi Grain remain separate, not-yet-migrated projects. No installation is requested.

- [x] Shared label/value alignment and conditional-control presentation
- [x] Windows shared workers, Project collection/registration, and path-state recall
- [x] Windows Wavesets automation reanalysis and Cutups BPM/status behavior
- [x] Selected-lane Cutups reanalysis and multi-file drop
- [x] Rings selected-slot commands, capture-target selection, polar editing, mute shortcut
- [x] Rings literal dual reads, formations, muted heads, and RMS waveform band
- [x] Doubles direct cue placement and MIDI command/hold feedback
- [x] Independent cursor trajectories/presentation implemented on both platforms
- [x] Portable interaction and storage/analysis regression coverage
- [x] macOS/Windows builds and automated macOS checks
- [x] Nine fresh reference captures and visual review, including loaded Rings,
  Cutups, and both Circulator loops

## Implementation notes

- Slider label/value centers follow the actual track center, including Player.
  Menu labels follow the menu center. Text is clipped to its original control
  bounds, with Cocoa's numeric precision reduction before clipping so narrow
  BPM/beat readouts retain their leading digits. Motion and Rings preserve
  Cocoa's conditional enabled states, colors,
  explanatory values, and manual-control emphasis.
- Windows imports use the existing per-plugin sample workers. LINK and PROJECT
  path recall, collection, registration, and pending-project retries are no longer
  macOS-only. Filesystem boundaries use UTF-8 paths; Windows decoding uses the
  shared WAV/AIFF reader. This does not expand Windows to every AVFoundation format.
- Wavesets Crossing Detail automation schedules source reanalysis. Cutups keeps
  the original analyzed BPM/status pipeline and recalculates only the selected
  lane. Clear/restore cancels superseded requests so a stale result cannot replace
  the new state.
- Multi-file drops fill consecutive slots up to capacity; Circulator's two-file
  drop addresses A/B. Rings LOAD/CLEAR uses the selected slot, capture-target edits
  update selection, polar dragging edits radial/angular head placement, Option/Alt
  H-buttons mute, and leaving solo restores the previous mute mask.
- Rings renders the original concentric source-channel geometry, peak/RMS bands,
  formation groups, and separate inner/outer read dots. Paused polar edits update
  the routing preview. Preset edits retain the Rings name with its original `*`.
- Doubles cue flags can be dragged directly; MIDI commands and held Punch/Drag
  are reflected in the controls. The prior LINK, deck-color, and BPM restoration
  checks remain in place.
- Doubles and Wavesets use persistent analytic cursor contracts, with native
  Core Animation / DirectComposition presentation. Unchanged redraws do not
  restart motion. Motion retains Cocoa's 30 Hz observed-position smoothing.
  Cursor labels use the shared font; overlays honor scaling and popup clipping,
  with software rendering if native presentation is unavailable.

## Verification and remaining acceptance

All nine migrated macOS bundles and Windows x64 bundles cross-build successfully.
The focused macOS test selection passes 36 tests, including DSP/CLAP state, native
GUI interactions, pure cursor-clock cases, and the portable canvas harness.
The storage test uses a Unicode project directory. The Objective-C symbol audit
reports 19 unique classes across these nine bundles.

The screenshot harness now waits for asynchronous sample publication, rather than
capturing an empty editor while its worker is still decoding. Player screenshot
tests use the proportional resize contract when its VSTGUI editor is enabled.

Build trees: `build-clap-sample-vstgui-fidelity` (macOS) and
`build-clap-windows-cross` (Windows x64). Current review captures are in
`/private/tmp/s3g-sample-audit-captures.KnNxQD`; canonical Cocoa documentation
images were not overwritten.

Focused regression command:

```sh
ctest --test-dir build-clap-sample-vstgui-fidelity \
  -R 's3g_(sample_.*|crcltr.*|clap_gui_param_queue_smoke|gui_layout_contract_smoke)' \
  --output-on-failure
```

Still requires external acceptance:

- Windows REAPER runtime tests of PROJECT/LINK relocation, dialogs/imports,
  resizing, and native compositor presentation. Cross-compilation is not a
  Windows hosted test.
- External screen capture of cursor continuity during a deliberately blocked
  UI thread. Automated tests check the clock and persistent native animations,
  but do not prove WindowServer output during that stall.

No installation or distribution archive was made in this pass. No claim of
complete hosted parity until the remaining machine tests are performed.
