# NIM Gesture and Relay: VSTGUI parity

Source of truth: the retained Cocoa drawing methods, geometry and event handlers
in `clap_nim_gesture` and `clap_relay`, plus `docs/nim_gesture.md` and
`docs/s3g-relay.html`. Original documentation images remain untouched. The
portable editors adapt the original surfaces rather than replacing their
visualizations with a generic parameter panel.

## Preserved behavior

- NIM Gesture: all twelve hardware-order cards and 192 slots; value, loop and
  recording rings; action glyphs; selected NRPN/value/loop detail; Record, Play,
  Clear Selected, Clear All and Cancel; smooth takeover and double-click reset;
  cell selection and double-click loop deletion. Action glyphs remain
  informational. Closing the GUI does not stop musical playback.
- Relay: FIELD, LEARNING, FORM, MIDI and INJECT; signed pentad/relay network,
  register and diffuse bus; weight/flow matrices and selection; 16/32/64-cell
  forms, plane inspection, dealt climate and recent trail; console filters and
  clear; injection controls/activity; all eight VOICE/CC ROUTING surfaces;
  factory presets, routing-preserving randomization, Crystallize/Thaw and full
  user presets. Multi-column menus retain their item order and separated rows.

Shared Fira Code, grayscale, uppercase titles with lowercase `s3g`, control
alignment and 65–200% proportional resizing use the existing foundation.
Original semantic colors, coordinates and custom drawings are retained. Mac
fonts are inside each bundle; Windows resources sit beside the CLAP binaries.
Missing fonts use the shared fallback. Relay file dialogs use the shared
extension filter and UTF-8/UTF-16 path boundary.

Plugin IDs, parameter metadata, MIDI processing and project-state formats are
unchanged. NIM's session extension and embedded standalone entry symbol remain
available. The standalone application itself is not migrated here.

GUI edits use ordered queues and balanced host edit gestures with backpressure
retry. NIM's rapid transport clicks are not coalesced, and Clear Selected
captures its target before a later selection change. Relay validates imported
preset files in an inactive staging instance, then transfers the complete
state to process/flush, including held-form position and thaw-memory flags.

## Builds and verification

Mac: `S3G_ENABLE_MIDI_TOOLS_VSTGUI_ON_MACOS=ON`. OFF retains Cocoa.
Windows uses the existing VSTGUI switch; Relay is included when this batch is
enabled even if `S3G_BUILD_RELAY_PREVIEW=OFF`.
Targets: `s3g_nim_gesture_clap`, `s3g_relay_clap`.

Verification on 2026-09-11:

- Both Mac bundles and Windows x64 CLAP binaries build. Retained Cocoa targets
  also rebuild with the Mac migration switch OFF.
- Direct tests `s3g_midi_nim_gesture_canvas_smoke` and
  `s3g_midi_relay_canvas_smoke` exercise native parenting/resizing, every Relay
  parameter's control hit area, menus, presets, all views and principal NIM
  recording/loop commands. They also check edit balance, output-queue rejection,
  Unicode presets, corrupt-file rejection and Crystallize state restoration.
- Both public CLAP GUI lifecycle tests and CLAP validator pass. Original
  NIM Gesture and Relay CLAP regression tests and Relay core smoke pass.
  Cocoa selector-specific checks stay on Cocoa; portable controls are tested
  directly rather than attempting Objective-C selectors on a VSTGUI view.
- `s3g_midi_tool_clap_parity_smoke` loads independent Cocoa/VSTGUI binaries.
  Three deterministic scenes match parameter/port metadata, cross-build state
  bytes, transactional truncated-file rejection and exact MIDI bytes/order/
  frame offsets: 1,256 events per NIM scene; 66, 14 and 24 for Relay.
  NIM's transient Last Loop Length is compared after loading the same state
  into both modules, since the original loader reselects the last stored loop.
- PNG captures were visually reviewed against the original surfaces. Set
  `S3G_MIDI_CAPTURE_DIR` when running the direct tests to regenerate them.

Scoped installer: `bash scripts/install-vstgui-midi-tools.sh [--dry-run]`.
It backs up and verifies only these two bundles. Building does not install.
Windows ZIP: `cmake -P scripts/package-windows-midi-tools.cmake`, with PE/export
and runtime-linkage checks, Fira Code, licenses, instructions and SHA-256 hashes.

Windows host drawing, MIDI device routing and native preset dialogs still need
the manual REAPER pass. NIM Gesture's intended No Input Mixer 8 companion is
not migrated in this batch; use a MIDI monitor/NRPN receiver for initial tests.

## Tracker recommendation

Keep Tracker Mac-only for now, as a deliberate exception to this migration
queue. This is a recommendation, not a platform-policy change in this patch.

`plugins/clap_tracker/CMakeLists.txt` builds eight native Objective-C++ UI
sources: controls, help, reshape, warp, workspace, phrase, assemble and song.
It also depends on Cocoa, CoreText, AudioToolbox and UniformTypeIdentifiers.
This is an application-like multiwindow editor, not a single plugin panel.
`tracker/CMakeLists.txt` already separates a C++ core and core tests, so a
Windows implementation is feasible, but it warrants a dedicated project with
explicit multiwindow, keyboard, editing, file and audio parity acceptance.
No Tracker sources, resources, platform gates or installation are changed here.

After this pair, six active manifest entries still lack the new GUI wiring:
No Input Mixer 8, Formant Matrix 2, Ambi Vox 64, Ambi Imprint 64, Ambi Energy 64
and Tracker. No Input Mixer is the logical next companion project, but its
detached editing windows need a larger parity pass. Ambi Energy's Metal view
and Imprint's Apple FFT/convolution path merit separate platform decisions;
they should not be treated as low-risk panel-only ports.
