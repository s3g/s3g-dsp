s3g-dsp - Windows x64 CLAP: VSTGUI terrain/map ports and menu-parity rollout

36 plugins are listed in PLUGIN_MANIFEST.tsv (canonical filename, stable ID and
host name). This is a scoped update, not a replacement for the entire collection.

New VSTGUI ports: Ambi Encoder Surface Terrain 64, Wave Terrain 64 and
Cartography 64. Surface Terrain and Cartography process track input; Wave
Terrain is an instrument. Use an appropriate multichannel track and decoder.
The other included plugins receive the restored custom-menu separators.

MANUAL INSTALLATION
1. Quit REAPER and back up the older versions of these same plugins.
2. Copy all 36 .clap files AND the complete Resources folder together into
   your CLAP search folder, e.g. %LOCALAPPDATA%\Programs\Common\CLAP\s3g-dsp.
3. Merge Resources with an existing folder; do not delete unrelated resources.
   Move duplicate older copies of these same plugins out of other CLAP search
   folders, so REAPER does not load a stale version.
4. Restart REAPER and rescan CLAP plugins if necessary.

Resources/Fonts contains the privately loaded Fira Code font and OFL license.
No system font installation is needed. A missing font uses a safe fallback,
but appearance will differ. Resources/Ray Atlas belongs to the Ray encoders;
do not omit it. Other third-party licenses are in Resources/Licenses and
THIRD_PARTY_NOTICES.md. SHA256SUMS.txt covers all packaged files except itself.

WINDOWS ACCEPTANCE CHECKS
- Dropdowns: separators between every item, selection marker, hover fill and
  correct selection; especially Wave Terrain's four-column musical-scale menu.
- Resize from 65% through 200%; close/reopen editors, save/reopen projects.
- Surface Terrain: original shell/path mesh and colored source markers,
  PATH/FORM/SKIN/WARP/READ tabs, cameras, zoom, logarithmic RATE and DIV.
  Compact RATE units h/c, m/c, s/c mean hours, minutes, seconds per cycle;
  DIV bt means beats. Host parameter text still uses the full unit names.
- Wave Terrain: shell/voice contours, selected voice and both wave traces,
  FIELD versus ROTATE controls, polygon controls, scale IDs and MIDI notes.
- Cartography: nine factory scenes, all eight processors, CORE/REL and
  MAP/PATH/FIELD pages; drag listener/sites in TOP (XY) and SIDE (XZ), then
  save/reopen a project including custom sites and camera settings.
- All three new ports: INIT/factory, RANDOM, double-click slider reset,
  automation/edit gestures, Unicode paths and .s3gpreset save/recall.
- Environmental encoders: existing SURF maps and POP windows, presets, live
  markers, automation, and project recall. Their extensions remain .s3gwind,
  .s3gwater, .s3ginsect, .s3gcryo and .s3gpyro.

These unsigned test binaries were cross-compiled on macOS. Native macOS tests
and Windows binary/resource validation do not replace testing in Windows REAPER.
