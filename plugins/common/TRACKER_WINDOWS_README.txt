s3g Tracker - Windows x64 CLAP TEST BUILD

STATUS
All ten existing VSTGUI pages and the MIDI engine are connected to the Win32
host. Builds may be native MSVC or cross-built; see the suite README and source
provenance for this package's build method and revision. Verify the exact
archive in REAPER; this remains unsigned, experimental Windows software.
There is no retired standalone app or built-in synth/sample playback engine.
Tracker generates MIDI; route its output to an instrument in REAPER.

INSTALL FOR TESTING
1. Quit REAPER before replacing any previously loaded Tracker binary.
2. Copy this entire folder to your chosen CLAP search location and add that
   folder to REAPER's CLAP paths if needed. Avoid duplicate Tracker copies.
3. Keep s3g_tracker.clap and Resources beside one another. Fonts are private
   plugin resources and do not require system installation. Keep the licenses.
4. Rescan and insert s3g Tracker before an instrument (or route its MIDI to one).
   Existing Mac .s3gt projects and .s3gpack asset packs use the same file format.

OPTIONAL AUTOMATED CHECK
Double-click run-windows-check.cmd from the extracted folder. It briefly opens
test windows, then checks DLL loading, state recall, ten-page navigation and
page exposure above the shell (not just window visibility flags),
65-200% sizing, detachable HWNDs, text-entry routing, multiple instances and
audio processing. It does not install anything or launch REAPER. The result
is printed in the console. This cannot certify behavior in the real host.

REAPER ACCEPTANCE CHECKLIST
- On first open, TRACKER must be visible immediately. Single-click SONG and
  each other tab before detaching anything; each page must appear and accept
  input. Recheck TRACKER/SONG after resizing, hide/show, and state recall.
  Floating and reattaching a tool must not be needed to expose its contents.
- Open all ten pages; confirm the original controls, displays, menu separators,
  compact submenus, centered button text and lane/bank colors are retained.
- Resize the whole plugin from 65% to 200%, including a 1920x1080 screen and
  Windows display scaling at 100%, 125%, 150% and 200%. Check click alignment.
  Move between monitors. Detached tools use responsive layouts, not main zoom.
- Detach/re-attach Geometry, Bursts, Phrases, Assemble, Reshape, Warps, Console
  and Help. They should stay owned by/in front of their REAPER FX window.
- Type commands WITH SPACES on Tracker and Console. Space must type a space,
  not start REAPER. Check Ctrl+A/C/V/X/Z, selection, history, Tab completion,
  Enter/Escape and focus switching back to REAPER and between two instances.
- Edit notes/velocity/FX, geometry and pitch maps; use bank import/copy/delete,
  Phrase capture/place and Assemble/Reshape operations. Check undo/redo.
- Save/reload REAPER projects, .s3gt and .s3gpack files, including paths and names
  with spaces, accents and non-Latin characters. Check list filters and cancel.
- Check Song row mutes, reorder, quantized launch, playing edits, loop toggle,
  the final non-looping row and recall of patterns other than A01.
- In Tracker VIEW test STATIC (default), CENTER and PAGE with long polymetric
  patterns. Pin a NOTE lane or choose SELECTED LANE; right-click a lane name
  for FOLLOW THIS LANE. CENTER pins the source row; PAGE shows its actual group
  size (16 at 55% grid zoom, 8 at 100%, 4 at 180%). Other cursors stay independent.
  Verify reverse/random/stride/loop jumps, rests, Song changes and lane reorder.
  Scroll/edit to hold following; ending an edit must not resume it. Click RESUME.
  Save/reload mode and source. A missing source must not select another lane.
- At fractional host BPM and 44.1/48/96 kHz, listen to looped Phrase/Assemble
  previews. Verify no pause at the seam; MIDI timing must not follow GUI FPS.
  Compare MIDI capture to Mac for transport start/seek/loop and tempo changes.
- Test VIEW Live Code from Tracker and detached Console: view status,
  view source 1 (or @alias/selected), view follow static/center/page,
  view zoom 125 (55-180; also +, -, reset), view notes name/midi,
  view jump 4 (1-16), view detail on/off, and view resume. Choose one value
  from the slash-separated alternatives. Resume leaves the main text field
  safely and rejoins following; zoom/detail remain temporary editor settings.
  These commands must not alter MIDI timing or the armed recording lane.
- Test MIDI STEP/LIVE Q/LIVE MT, channel routing, panic, SYNC and note release
  while closing/hiding editors. Repeat with multiple instances/project reload.
- Test once with a COPY of the package whose Resources folder has been moved
  aside. Fonts should fall back safely, though appearance will differ. Restore
  Resources before visual comparison. Never modify a loaded plugin's resources.

TYPOGRAPHY
The Tracker grid uses the same bundled IBM Plex Mono faces as Mac. Shared
suite controls use Fira Code on Windows (Mac's Menlo is not redistributed).
DirectWrite and CoreText may rasterize glyphs differently; check readability
and alignment rather than expecting identical antialiasing pixels.

LICENSES
LICENSE.txt and TRACKER-LICENSE.txt cover repository/Tracker code.
THIRD_PARTY_NOTICES.md covers incorporated dependencies. Font notices are in
Resources/Fonts/FiraCode-LICENSE.txt and Resources/Fonts/OFL.txt (IBM Plex).
