s3g-dsp: Errant 2, Feedback Shift 8, Fissure 8, Fault 8 — Windows x64 CLAP

Copy all four .clap files AND the adjacent Resources folder together to your
Windows CLAP scan location. Fira Code and its license are in Resources/Fonts;
no system font installation is required. Missing fonts use a safe fallback.
Restart REAPER/rescan after replacing earlier copies. Keep backups of existing
plugins and projects. Plugin IDs, parameter IDs and state formats are retained.

Errant is a stereo MIDI instrument. Feedback Shift and Fissure accept live
mono/stereo track audio and produce up to eight channels. Fault synthesizes
eight lanes from generated data or an imported WAVE/raw file. It does not load
audio from the track. Set the REAPER track channel count for multichannel output.

Windows host acceptance checklist (not replaced by Mac/cross-build tests):
- Open, close and reopen; resize through 65%, 100%, 150% and 200%.
- Check uppercase titles, Fira Code, gray levels, aligned labels/handles,
  complete readouts and separator lines in all custom menus.
- Automate sliders/menus, check host-to-GUI updates, drag gesture boundaries,
  double-click defaults and save/reopen REAPER projects.
- Save/load .s3gpreset files in Unicode paths; check the file listing/filter.
- Errant: MIDI receive/channel menu, ancestry display, GENERATE and evolution.
- Feedback Shift: PATCH/INSERT/ECOLOGY/AUX; A/B scenes, copy/random/morph,
  matrix selection, second-click erase, Alt-click negative route, FLIP/CLEAR,
  all node insert categories, node RND and live node/meter visualization.
- Fissure: all factory presets, matrix paint/route control, cut masks, six
  topologies, four stored scenes and morph, all object sliders/strikes,
  both fracture pucks with latch/spring return, GRAB/REPEAT and live overlays.
- Fault: all factories, SOUND/BASS LAB/MOD LAB, MIDI/free ADSR graph,
  output stereo/quad/direct and rotation, codec/algorithm menus, three
  modulation operators, GEN FIELD/RAND PATCH/MUTATE/UNDO and eight traces.
  OPEN ANY offers a custom menu: decode WAVE into eight lanes or interpret
  bytes directly. Decode mode falls back to raw bytes for unsupported formats,
  matching the Cocoa behavior. Test both choices and Unicode file paths.
  Imported sources remain path-referenced (not embedded in presets/projects).
  Keep source files available; check missing-file status when reopening.

SHA256SUMS.txt covers every packaged file except itself.
