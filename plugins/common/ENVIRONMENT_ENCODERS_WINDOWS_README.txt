s3g-dsp: Environmental Encoders - Windows x64 CLAP test build

Includes Ambi Encoder Wind 64, Water 64, Insect 64, Cryosphere 64 and Pyrosphere 64.
These are zero-input instruments, not track-input effects or sample players.
Use a 64-channel REAPER track followed by an appropriate ambisonic decoder.

Manual installation:
1. Quit REAPER.
2. Copy the five .clap files AND Resources into your CLAP search folder, for
   example %LOCALAPPDATA%\Programs\Common\CLAP\s3g-dsp.
3. Remove/move aside older copies of these same five plugins from other CLAP
   search folders to avoid loading a duplicate version.
4. Restart REAPER; rescan CLAP plugins if needed.

Resources/Fonts contains the privately loaded Fira Code font and its OFL license.
Keep this folder beside the plugins. No system font installation is needed.
Missing fonts use a safe system monospace fallback (appearance will differ).
LICENSE.txt and THIRD_PARTY_NOTICES.md contain the project/VSTGUI notices.

Check on Windows:
- The original FIELD graphics, camera buttons, rotation, zoom and live markers.
- All factory presets, RANDOM, sliders, custom menus and double-click reset.
- Enlarge and shrink the editor between 65% and 200%.
- SURF: ADD two scenes, enable, drag X/Y, EDIT cells, CAP, DEL, curve/focus/glide.
- POP: synchronized editing, placement above the main FX window, close/hide,
  reopening, and cleanup when the main editor closes.
- Save and reopen a REAPER project with a populated SURF map and X/Y automation.
- Save/load a custom preset in a folder whose name includes non-ASCII characters.
- Insect: regime-specific labels, hidden controls and HEARD/GAIN meters.
- Cryosphere: ice-skin contact marker; Cryosphere/Pyrosphere: entity score readout.

Preset extensions: .s3gwind, .s3gwater, .s3ginsect, .s3gcryo, .s3gpyro.
Single-preset files store one scene; the full SURF map belongs to project state.

These unsigned pre-release binaries were cross-compiled on macOS. Mac native
GUI, state and DSP comparisons do not replace runtime testing in Windows REAPER.
