s3g-dsp — Modal, Medium and Membrane Kick / Windows x64 CLAP

Copy the three .clap files AND the Resources folder together into your CLAP
search folder, for example C:\Users\YOUR_NAME\AppData\Local\Programs\Common\CLAP\s3g-dsp.
If using another folder, add it to REAPER's CLAP search paths. Remove duplicate
older copies of these same three plugins from other scan folders after backing
them up. Restart REAPER and rescan if needed.

Keep Resources\Fonts\FiraCode-Regular.ttf and FiraCode-LICENSE.txt beside the
plugins. No system font installation is required. The GUI has a safe fallback
if Resources is missing, but Fira Code is needed for the intended appearance.

These builds are cross-compiled Windows x64 binaries, with embedded VSTGUI and
static MinGW runtime. Packaging checks PE architecture, clap_entry and runtime
dependencies. Mac GUI/DSP tests are not Windows REAPER acceptance tests.

Windows acceptance checklist

1. Open all three editors; verify full titles, all-caps labels, custom menu
   separators and Fira Code. Resize from 65% through 200%, then close/reopen.
2. Modal: body layout must be visible immediately. Select/drag bodies in TOP
   and SIDE, select/orbit in 3/4, zoom and RESET. Edit each selected body's
   SKIN X/Y pad and BODY AED; test 4–8 bodies, all factory presets, listener
   choices and live actuator routes. Feed mono audio or MIDI. Test HOA and
   eight body stems with sufficient REAPER track channels.
3. Medium: verify eight nodes/twelve edges, click-to-strike, selection and live
   halos. Test SOURCE / SEQ / MIDI pages, per-node MASK/PULSES/ROTATE, scale
   and note count, all four exciters and all four MIDI modes. Hold MIDI notes
   and check cyan rings/note labels. Test optional mono audio excitation.
4. Membrane Kick: use MIDI or STRIKE (no audio input). Try all five shapes and
   amounts, BODY/STRIKE pages, click-to-place, FIXED/RANDOM AREA/RANDOM RIM,
   MIDI channel receive, tracking/velocity and all fourteen factory presets.
   Test HOA, 16 PICKUPS and STEREO DOWNMIX; the last two bypass SPACE controls.
5. Record slider, body and skin automation; verify gestures end on mouse-up
   and closing the editor. Double-click sliders to reset their defaults.
6. SAVE/LOAD .s3gpreset files using non-ASCII filenames; verify the Open dialog
   lists them. User preset loading preserves OUT; project recall restores it.
   Exchange preset files with Mac and verify notes/sequence/trigger behavior.
7. Save/reopen a REAPER project, including each output format and non-default
   GUI view. Check live graphics and controls after playback resumes.

Licenses: LICENSE.txt, THIRD_PARTY_NOTICES.md and Resources\Fonts\FiraCode-LICENSE.txt.
SHA256SUMS.txt lists the three binaries and bundled font resources.
