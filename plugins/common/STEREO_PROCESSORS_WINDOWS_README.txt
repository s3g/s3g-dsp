s3g-dsp: LF Synth 2, Lowform 2, Stack 2, Conduit 2 — Windows x64 CLAP

Copy the four .clap files AND the adjacent Resources folder together to your
Windows CLAP scan location. No separate font installation is needed. Fira Code
and its license are in Resources/Fonts. Keep Resources alongside the plugins;
if it is missing, the editor uses a safe system-font fallback.

Restart REAPER or rescan CLAP plugins after replacing existing copies. These
retain their established plugin IDs and project-state versions. Back up your
projects and earlier plugins before testing.

LF Synth, Lowform and Stack are stereo MIDI instruments. Conduit is a live-input
audio effect: use a mono or stereo audio source on the REAPER track.

Manual acceptance checklist:
- Open, close and reopen each editor; resize through 65%, 100%, 150% and 200%.
- Check Fira Code, complete labels, slider handle alignment, and separator lines
  in every custom menu. Confirm the window remains correctly parented.
- Try every factory preset and RANDOM. Record slider/menu automation; verify
  drag begin/end, double-click reset and host automation updates.
- Save/load custom .s3gpreset files, including a Unicode folder/filename; verify
  the open dialog lists files. Custom load must leave OUT unchanged.
- Save/reopen a REAPER project with different settings in each plugin.
- Lowform: all eight body engines, BUILD/MOTION, conditional Modal Drive,
  two-column modulation targets, three live activity marks, Pitch/Accent/Gate/
  Octave arp lanes, drag painting, double-click defaults, right-click REST.
- Stack: all four pages, independent rigs, links/copy, both arp patterns;
  SCORE's A/B tab entry, arrow/Tab/Return keys, one/two-digit frets, H/~ holds,
  rest, repeat and bracket adjustment. Control-C/V copies/pastes tabs.
  Right-click L1/L2 to select row locks, drag vertically, double-click to clear.
  Check section/arrangement editing, all five score randomizers, live playheads
  and recall of score cells, arrangement and locks.
- Conduit: both columns of the material menu, every Input Listen/Pedal/Position
  option, stereo channel identity, octave controls, PANIC, eight-second energy/
  reduction histories and changing mic/path/body readouts.

The Mac and cross-build tests do not substitute for this Windows host pass.
SHA256SUMS.txt covers all packaged files except itself.
