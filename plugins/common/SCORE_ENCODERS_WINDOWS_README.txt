s3g-dsp — Acid 16, Horizon 64 and VOT 64 / Windows x64 CLAP

Copy all three .clap files AND the Resources folder together into your CLAP
search folder. Add that folder to REAPER's CLAP paths if necessary. Back up
older copies of these same plugins and avoid duplicate copies in scan paths.
Restart REAPER and rescan.

Resources/Fonts contains Fira Code and its license. No system font installation
is needed. A missing font falls back safely, but Fira Code is needed for the
intended appearance. Keep the Resources folder beside the Windows .clap files.

The optional VOT Wavetables folder contains the repository's atlas library,
README and manifest. Use VOT's SOURCE / SYNTH LOAD button to select a WAV.
The loaded USER atlas is embedded in the plugin's project/preset state; the
original WAV is not required to reopen that project.

These are cross-compiled Windows x64 binaries with VSTGUI and static MinGW
runtime linkage. Mac tests and PE/export checks do not replace Windows REAPER
acceptance testing.

Windows acceptance checklist

1. Open all editors, check Fira Code, uppercase labels, menu item separators,
   centered slider handles, and proportional resizing from 65% through 200%.
2. Acid: use a 16-channel track. Test all 12 patterns; drag notes; toggle G/A/S;
   edit both spatial views; reset/randomize paths. Check LINE/OSC/DRIVE/FIELD,
   the 101-scale menu, drive circuits, format and OUT. Test arrows, G/A/S,
   Space, wheel and Shift-octave shortcuts while the editor has focus.
   Check INT/HOST clock, transport seeks/loops, MIDI transposition and wake.
3. Horizon: use a 64-channel track. Test all 16 scenes and nine ecologies;
   verify the generator controls change with ecology. Check circle/bar/diamond
   entities, live activity, listener meters, TOP/SIDE/3/4, orbit and zoom.
   Check factory/random preservation of OUT, ORDER and listener controls.
4. VOT: use a 64-channel track. Test FREE/MIDI/BOTH, FIELD/VECTOR/SCORE,
   voice selection, camera/orbit/zoom, vector-pad drag and live trails.
   Edit both score lanes, route points, TIME/U/V/CURVE, node count and RESET,
   sustain-loop ends, and every score mode. Load WAV/WAVE files, including
   non-ASCII names, then check the 16-table grid and interpolated waveform.
5. Record slider and pad automation. Verify gestures finish on mouse-up and
   closing the editor; double-click sliders to reset defaults.
6. Save/load Acid and VOT .s3gpreset files and Horizon .s3ghorizon files using
   non-ASCII names. The Open dialog must list matching files. Acid/VOT user
   loads preserve OUT; Horizon user loads preserve OUT and ORDER, matching
   the Cocoa implementations. Factory and project policies remain distinct.
7. Exchange presets with Mac. Save/reopen projects with a VOT USER atlas and
   edited score, non-default cameras/pages and output orders. Verify restored
   audio, routing, controls and live graphics after playback resumes.

Licenses: LICENSE.txt, THIRD_PARTY_NOTICES.md, Resources/Fonts/FiraCode-LICENSE.txt.
SHA256SUMS.txt covers the package contents.
