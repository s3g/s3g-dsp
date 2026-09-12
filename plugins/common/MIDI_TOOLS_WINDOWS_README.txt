s3g-dsp MIDI tools - Windows x64 CLAP test package

Included: s3g Utility NIM Gesture and s3g Relay.
Both are MIDI-only plugins: they do not generate or process track audio.

Copy the complete folder to a location scanned by REAPER for CLAP plugins.
Keep Resources beside the two .clap files. Resources/Fonts contains Fira Code
and its license; no system-wide font installation is needed. The GUI falls
back to a system font if the resource is missing. Do not keep a second copy
of the same plugin ID in another scanned folder.

These binaries were cross-compiled on macOS. Mac GUI/control tests, CLAP
validation and Cocoa/VSTGUI MIDI/state parity tests passed. Actual Windows
REAPER behavior still requires the manual checks below.

Shared checks
- Scan, insert, open/close and reopen both editors; resize from 65% to 200%.
- Confirm Fira Code text, s3g styling, centered controls and menu separators.
- Save/reopen a REAPER project and check parameters and MIDI routing.
- Record automation from buttons/sliders; verify no stuck edit gestures.

NIM Gesture
- Route MIDI/NRPN input to the plugin and route its MIDI output downstream.
- Record changes to two controls, commit and play their independent loops.
- Check value/loop/recording rings, selected-control details, takeover drag
  and double-click reset, clear selected/all, and cancel recording.
- Changing the selected control does not send a new knob value. Hardware
  action glyphs are informational; use the five transport buttons above.
- Double-click a parameter cell to clear its loop. Closing the editor must
  not stop musical playback. Project state retains loops and Play state.
- No Input Mixer 8's VSTGUI/Windows migration is not part of this package.
  Use a MIDI monitor or appropriate downstream NRPN receiver for Windows
  testing until that companion port is ready. This is not a Windows build
  of the separate NIM standalone/hardware-host application.

Relay
- Put a MIDI instrument after Relay and start host transport. Relay routes
  to its configured MIDI channels; it is not a simple MIDI-thru effect.
- Inspect FIELD, LEARNING, FORM, MIDI and INJECT during playback. Check the
  eight relay selectors, VOICE/CC ROUTING tabs, matrix modes/cell selection,
  16/32/64-cell form planes, MIDI filters/clear and input activity.
- Exercise all menu columns, especially the scale and input-channel menus.
- Factory presets and RANDOM must retain the existing MIDI assignments.
- CRYSTALLIZE must hold the form and set Memory/Freeze; THAW must restore
  the remembered Memory value, including after preset/project recall.
- SAVE/LOAD .s3gpreset files in a folder with spaces and non-ASCII text.
  Existing presets must appear in the Windows open dialog. Cancel must do
  nothing; corrupt or missing files must not replace the running patch.

SHA256SUMS.txt covers the package payload. LICENSE.txt, THIRD_PARTY_NOTICES.md
and Resources/Fonts/FiraCode-LICENSE.txt contain redistribution notices.
