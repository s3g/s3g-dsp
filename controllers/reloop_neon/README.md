# Reloop Neon + s3g Sample Neon 32 in REAPER

This pairing uses the Neon's factory MIDI behavior. Do not flash or replace
its firmware.

## Install and scan

Copy the built bundle into the user CLAP folder:

```sh
mkdir -p "$HOME/Library/Audio/Plug-Ins/CLAP/s3g-dsp"
cp -R build-clap/plugins/clap_sample_neon/s3g_sample_neon.clap \
  "$HOME/Library/Audio/Plug-Ins/CLAP/s3g-dsp/s3g_sample_neon.clap"
```

In REAPER, use **Preferences → Plug-ins → CLAP → Re-scan**. Insert
`s3g Sample Neon 32` on a new track. Version 0.21 has one instrument, not separate
stereo and multichannel variants. It keeps the 0.13 plug-in ID and loads 0.13–0.19
sets; newly saved sets require 0.20 or later. Version 0.19 uses its original 5 ms
fades. In pre-0.19 sets, old Character effects are replaced:
older sets reset Character to Filter and cell/global Mangle to zero. Save a separate
copy before migrating. Old combined techniques resolve to
Grains, then Motion, then Sample; adjacent-cell mixing and mute/solo are retired.
Older sets can sound different under this simplified model.

CHOP → TRANSIENTS now has a 0–50 ms **PRE-ROLL** slider with a clickable numeric
value. Scroll over the waveform to zoom at the pointer; Shift-scroll or a
horizontal trackpad gesture pans without changing the sound or edit cursor.

## REAPER routing

1. Connect the Neon and enable its input under **Preferences → Audio → MIDI
   Devices**. Enable both input and control messages for experimentation, but
   route the device to the instrument track as ordinary MIDI.
2. Arm the Sample Neon track, enable input monitoring, and choose the Reloop
   Neon as the track MIDI input on all channels.
3. Open the plug-in and turn **NEON OWNER: ON**. Leave this off on every other
   Sample Neon instance. The explicit owner prevents multiple instances from
   sending competing LED frames. On macOS the button changes to
   **NEON: CONNECTED** when its direct CoreMIDI LED path is live.
4. **Remove any MIDI hardware send back to the Neon on macOS.** The owner
   instance drives its LEDs directly. Since **0.20.1**, the Mac plug-in has no
   CLAP MIDI output, preventing duplicate LED streams through the track.
   Non-macOS hosts retain the dedicated CLAP LED output; route it only to the
   controller, never back into the instrument or another sound generator.
5. Disable any REAPER option that consumes the same Neon buttons as actions or
   control-surface shortcuts on this track. A message should reach Sample Neon
   once, not both the instrument and an unrelated action binding.

The intended signal path is:

```text
Reloop Neon MIDI → armed REAPER track → Sample Neon → audio outputs
                                      ↘ direct CoreMIDI LED output → Neon
```

For multichannel playback, set the track to 32 channels and preserve channel
order through the FX pins and sends. Choose Stereo, Stereo Stems, Quad, Octo,
or 1OA/2OA/3OA ACN/SN3D in the header. Assign the cell's Format and Output bus
in the inspector; bus labels show the exact channel range. Stereo defaults to
channels 1–2. Spatial sources are never automatically downmixed. Monitor
ambisonic material through a matching ACN/SN3D decoder after Neon.

## First performance test

1. Drop contrasting sounds on A1–A8 and longer material on B1–B8.
2. Press Bank A and verify the first hardware row plays A1–A8 and reports pad
   activity in the editor.
3. In Sampler, hold Mode and tap a pad to cycle One Shot, Toggle, and Hold.
   Hold Slip/Repeat and tap it to toggle per-slot looping; hold Sync and tap it
   to toggle per-slot tempo sync. The small LEDs report those five states in
   the order printed on the Neon. Use **Velocity On/Fixed** and **Choke 0–4**
   in the slot panel to shape kit behavior.
4. Press Hot Cue (**EDIT**), then select a source cell; the pad also auditions
   its current processed sound. LOOP moves the
   cursor; push sets Start and Shift+push sets End. TRAX zooms. Enter Slicer
   (**CHOP**) and choose Live
   Markers, Transients, Equal, or Beat Grid from the menus. LOOP still moves
   the cursor; its push adds a marker and Shift+push removes the nearest one.
   **Assign One/All** creates ordinary playable cells in empty destinations;
   the SOURCE menu lets Assign All replace the source with slice 1. Other
   occupied cells are kept. Shift + CHOP secondary pad 8 also assigns with
   source replacement, allowing all 32 slices to fit. Assigned slices are new
   cropped samples at 0–100%, not trim windows on the whole original. Select
   a cell in PLAY or EDIT, then CHOP to re-slice just that cell. Original files
   stay untouched. Older trim-based assignments must be reassigned to commit
   their audio. Keep **ZERO CROSS**
   on for shared-channel boundary snapping, or switch it off for manual placement.
5. Select the Sampler secondary layer for eight velocity/aftertouch Pad FX:
   Filter, Echo, Comb, Ring, Flutter, Pulse, Drive, and Crush. These run after
   any playback technique. Drive/Crush are disabled for ACN/SN3D sources.
6. In CHOP, banks A–D audition slices 1–8, 9–16, 17–24 and 25–32. Its secondary
   pads are Add, Delete, Audition, Zero Cross, Equal, Transients, Assign One,
   Assign All. An assignment needs enough empty cells; 32 slices can be
   auditioned even when fewer than 32 destinations are available.
7. EDIT's secondary pads are **Source, Sample, Motion, Grains, One Shot, Hold,
   Toggle, Stop**. Sample/Motion/Grains commit to one technique, matching the
   right toolbox's PLAYBACK menu; Source only opens trim/source editing.
   Choices audition the sound; Stop chokes only this cell without a mute latch.
   Motion continuously scans; Grains makes a cloud around its Position.
   Their CLOCK menu defaults to Free/Pad (works with REAPER stopped), with
   optional Host Transport. Shot Length sets the entire one-shot duration;
   Grain Size controls individual grains, not cloud duration.
   The PLAYBACK menu also offers **Slice Sequence** (this cell's CHOP markers),
   **Stretch** (duration independent of pitch), and **Wavesets** (cycle processing,
   discrete sources only). Sequence and Stretch also have Free/Host clocks.
   **EDIT VIEW → Character FX** opens three editable parameters for each effect;
   secondary pads then select FX instead of the usual edit shortcuts.
   Shift is fine/alternate editing,
   not the secondary-layer switch. The GUI has an explicit SECOND/PRIMARY button.
8. Hot Loop (**RESAMPLE**) records one internal output bus after master gain.
   Primary pads are Record, Stop, Audition, Discard, Bus −, Bus +, Review, Assign.
   Select **PLAY PADS** on the miniature NEON (RESAMPLE secondary), then use
   **SHIFT + pad 1 (REC)** to start/stop capture and the unshifted pads to perform.
   The shortcut works on either RESAMPLE layer; it never triggers pad 1 or
   overwrites a completed take. Discard the review before a new recording.
   The toolbox keeps Record/Stop, but no longer repeats the pad grid;
   the waveform and write cursor draw live, including stacked multichannel rows.
   no page switch is needed. A–D banks match the NEON. Loaded pads on the
   hardware's RESAMPLE secondary layer also play normally (including Hold/Toggle);
   empty pads choose a capture target. Source playback never changes that target.
   Choose an empty target, review/trim with the encoders, then Assign.
   **CROP TO SELECTION** (Shift + RESAMPLE primary pad 7) discards the unused
   head/tail of the review, preserving every channel. Target-pad presses audition
   the take; cells assigned before cropping retain their original audio.
   The result is a normal cell, preserving stereo, quad, octo or ACN/SN3D channels.
9. In PLAY, LOOP adjusts selected-cell gain and TRAX controls Mangle. In CHOP
   and EDIT Source/Trim, TRAX zooms, push resets, and Shift+push focuses the trim
   or slice. EDIT Motion uses LOOP offset/TRAX cycle; Grains uses LOOP size/TRAX
   density or host interval. LOOP push auditions without changing technique.
   EDIT encoders follow the right toolbox view on either pad layer.
   Slice Sequence uses LOOP step rate/TRAX repeats; Stretch uses duration/tune;
   Wavesets uses group/repeats. Character FX uses parameter 1/2, with TRAX push
   switching to parameter 3/Amount. LOOP push auditions.

The five small pad lamps remain **One Shot, Toggle, Hold, Loop, Sync**, with
Loop fourth. For generated playback, Loop means Hold/Toggle and Sync means Host
clock. Sync+pad switches Free/Host (except always-Free Wavesets);
Repeat/Slip+pad switches One Shot/Toggle.
CHOP auditions bypass cell techniques; EDIT shortcuts explicitly update them.
Capture is block-accurate and limited to 30 seconds or 1,440,000 frames. Completed
captures and committed slices are embedded in sets/project state; imported
files remain linked until sliced and assigned.
See the [plug-in guide](../../plugins/clap_sample_neon/README.md) for the boundary
selection algorithm, routing matrix and capture limits.

The miniature pads explicitly distinguish **AUDIO**, **EMPTY**, **LOADING** and
**OFFLINE**; bank buttons show how many of their eight cells contain audio.
Primary hardware cell pads use **red = loaded**, **yellow = last played in that bank**,
**white = active**, **off = empty/offline**. Selecting a cell for editing does not
change last-played history. Active generators stay lit through quiet grain gaps
and their release fades. On Mac, bank/mode changes receive three short pad-only
recovery updates (approximately 50/150/350 ms), then unchanged LEDs stay quiet.
Version 0.20.1 removes the continuous 250 ms refresh and duplicate host MIDI
stream from 0.20.0. If LED notes return as input, a loaded pad's light can look
like a new strike, switch the selected waveform, and strand a Hold gesture.
For a stuck sound use **KILL**, not Reset All; Reset All clears the set's cells.

Version 0.21 separates secondary pages using large-pad palette values
**PLAY FX = 16 / CHOP tools = 32 / EDIT tools = 64 / RESAMPLE cells = 80**.
Selected tools and playing cells are white; disabled tools/empty cells stay off
(the selected empty capture target remains highlighted). The GUI stays minimal
gray. The palette bytes are tested, but confirm their visible hues on hardware.

The right-side buttons are modifiers in PLAY and RESAMPLE's loaded-cell layer:
**MODE + pad** cycles the trigger mode; **SLIP/REPEAT + pad** toggles Sample repeat
or generated One Shot/Toggle; **SYNC + pad** toggles Sample sync or generated
Free/Host clock (Wavesets is always Free). **CENSOR + pad** reverses a new direct
sample gesture where the hardware emits CENSOR, not MODE. They do not start
REAPER transport, and EDIT's shortcut pads retain their dedicated edit meanings.

**EDIT → Source / Trim → NORMALIZE SOURCE / -1 dBFS** uses one linked gain for
every source channel. It keeps trim, markers, playback/FX settings and other
copied cells unchanged, and never writes the original file. The normalized
source is embedded in the project/set; subsequent gain and FX can exceed this peak.

Click a cell then use **Ctrl-C / Ctrl-V** (also Cmd-C/V), or right-click its pad,
to copy/paste audio and all sound settings across cells/banks. Occupied cells
require replace confirmation. Motion and Grains now have **Attack/Release** in
seconds: Hold fades in on press and out on release; Toggle fades out on the next
strike. One Shot fits both fades within Shot Length.

Use **RESET ALL → CLEAR ALL** to start over. Save a set first: this clears cell
audio, sound settings and capture, but preserves output/MIDI setup and never
deletes source files. Cancel leaves the set untouched.

The plug-in header reports whether its macOS LED connection is starting,
searching, connected, or in error. The automated tests verify the documented
native MIDI bytes, sample routing, SysEx, and feedback messages; the probe
below remains useful for testing the hardware independently of REAPER.

## Two controllers and Smart Link

[Reloop's FAQ](https://www.reloop.com/faqs/neon.html) supports two Smart-Linked
NEONs (or up to four separately connected controllers), with a dedicated unit
per deck. The mini-jack link shares one USB connection; some hosts require the
supplied dual USB cable for power. It does not create extra pad/control types.
Two units physically provide sixteen pads and four encoders simultaneously.

Sample Neon currently has **one shared surface/bank and one direct LED endpoint**,
not independent left/right NEON bank assignments. Smart Link alone does not
double this plug-in's 32 cells. Independent banks need explicit mapping work:
the [factory MIDI map](https://www.reloop.com/media/custom/upload/Reloop-NEON_MIDI-Map.pdf)
uses identical primary Sampler pad messages (97,00–07) across A–D, while other
mode/layer addresses distinguish decks. Capturing a linked pair's MIDI is needed
before promising separate sampler banks; bank-select messages alone cannot
identify which unit sent a shared-address pad press.

For direct hardware diagnosis on macOS, build and run the included CoreMIDI
probe:

```sh
cmake --build build-clap --target s3g_reloop_neon_probe -j 8
./build-clap/s3g_reloop_neon_probe
./build-clap/s3g_reloop_neon_probe --monitor 10
./build-clap/s3g_reloop_neon_probe --led-test
```

The LED test performs a short chase across the large pads and their five small
indicators, then explicitly clears the test frame. The monitor is read-only
and prints both the raw bytes and the decoded Sample Neon action.
