# MIDI test guide

## What this slice proves

The app can edit a native pattern, schedule it outside the Cocoa event loop,
and emit timestamped MIDI 1.0 note-on/note-off packets. Each indexed MIDI OUT
instrument owns its CoreMIDI endpoint and channel; pattern track-channel data
does not override that device route. The default demo uses
General MIDI drum pitches, a 48 kHz scheduling timeline, four
ticks per beat, and a 90 ms gate.

It does not host Superior Drummer. Use the named virtual source through the
standalone instrument's MIDI settings or a DAW track, or choose a CoreMIDI
destination directly.

The optional `dev-audio` build additionally makes raw AUHAL time the master
clock and partitions the canonical stream by indexed instrument. Its
audio queue covers `max(20 ms, two callback budgets + 12 ms)` while paired
MIDI is submitted only to `max(20 ms, one callback budget + 12 ms)`. The plain
`dev` build retains the fixed 20 ms MIDI-only scheduler.

## Build and launch

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev --output-on-failure
open "build/dev/s3g Tracker.app"
```

To test the indexed Membrane Kick, DaisySP drums, and native stereo sampler,
build and launch the audio preset instead:

```sh
cmake --preset dev-audio
cmake --build --preset dev-audio
ctest --preset dev-audio --output-on-failure
open "build/dev-audio/s3g Tracker.app"
```

The CoreMIDI integration executable is deliberately not registered in CTest by
default because sandboxed build processes may not reach macOS MIDI services.
From a normal Terminal, this command creates eight uniquely named virtual
sources, connects CoreMIDI listeners to the first two, and verifies endpoint
isolation, channels, gate interval, off-before-on retrigger ordering, Kill, and
old-route cleanup; initialization failures are test failures, not skips:

```sh
./build/dev/s3g_tracker_midi_integration_tests
```

Expected output:

```text
CoreMIDI independent virtual sources, instrument-owned channel, gate, identity, retrigger, Kill, and cleanup integration passed
```

To register that fail-hard test on a machine whose test runner has CoreMIDI
access, configure with `-DS3G_TRACKER_ENABLE_COREMIDI_INTEGRATION_TEST=ON`.

## Route to Superior Drummer

### Virtual-source route

1. Double-click the indexed `MIDI OUT` row (or press Command-9) and leave
   `s3g Tracker 1 [VIRTUAL]` selected. Choose the channel beside it.
2. In Superior Drummer standalone, enable `s3g Tracker 1` as a MIDI input. If
   Superior is hosted in a DAW, enable the source in the DAW and route/monitor
   it on Superior's instrument track.
3. Press Play. The toolbar should show `Playing • timestamped CoreMIDI`, the
   `SENT` counter should advance, and `DROP` should remain zero.

### Direct-destination route

1. Connect or expose the desired CoreMIDI destination.
2. Select a MIDI OUT instrument, press `REFRESH DEVICES`, and choose the
   destination and channel in the MIDI Instrument window.
3. The tracker stops, sends immediate cleanup, keeps
   the old endpoint alive for a final sweep at least 8 ms beyond its latest
   submitted timestamp (and never earlier than 28 ms after the cleanup
   request), and only then changes endpoints. If any cleanup packet submission
   fails, the route remains unchanged and the error is shown.
4. Press Play again.

If an instrument-owned destination disappears, Play fails with the unavailable
route identified instead of silently changing that song device. Stop,
endpoint/channel changes, app termination, and Panic all send note cleanup to
every active MIDI instrument route.

In the live-audio build, the initial song index is 00 MEMBRANE KICK,
01 STEREO SLICE SAMPLER, and 02 MIDI OUT. Additional internal or MIDI
instances are added from the right toolbox. Each receives the next free song
index. There is no per-track
Both route. Use two tracks for intentional internal/external doubling. Losing
MIDI while MIDI OUT is active stops the transport and resets internal voices.
Membrane FX are graph parameters and do not become MIDI CC.

## Editing controls

- Click a NOTE, INS, VOL, FX action, or FX value subfield to select it. On
  NOTE, typing a digit or `A`–`G` starts inline entry immediately; MIDI numbers
  and names such as `C-4`, `C4`, `F#3`, and `Db4` are accepted. Double-click
  or Return opens exact entry on any field; Return commits/advances and Escape
  cancels. INS accepts buffered zero-based decimal rack indices; VOL accepts
  normalized decimals from `0.000` through `1.000`.
- Left/Right moves between NOTE, INS, VOL, and FX fields; Up/Down moves rows.
  Shift-Left/Right changes tracks while retaining the current field.
  Shift-Up/Down extends the global loop region. Home/End jumps to the
  first/last row, Page Up/Down moves by a view, and F9–F12 jumps to
  0/25/50/75 percent.
- On NOTE, `X` toggles a hit, Delete writes Rest, `R` writes Retrigger
  Previous, and `K` writes Kill.
- Drag across tracker cells for a rectangular selection. `Command-C/X/V`
  copies, cuts, or pastes typed tab/newline-separated cells; `Command-A`
  selects the visible page. Pasting one cell fills the selected block.
- `Command-+/-/0` zooms the tracker or resets it. The fixed right toolbox has
  matching `− / percent / +` controls; zoom range is 55–180 percent.
- On FX pages, `X` writes the page's default action/value, `0`/Delete clears,
  and `R` writes Previous. `[` / `]` adjust the remembered V1/V2 value.
- On INS, typing digits starts zero-based decimal index entry (`0` is `I00`);
  Delete clears, `R` writes Previous, and `[` / `]` cycle populated instruments. Empty and
  Previous both retain remembered instrument; Previous makes that intention
  visible.
- `[` / `]` lower/raise normalized VOL by `0.05`. `M` mutes the selected field.
- Tab/Shift-Tab traverses fields and wraps across NOTE/INS/VOL, FX1/V1, and
  FX2/V2 while the grid has focus. The page button changes pages directly.
- Space toggles Play/Pause while the grid has focus; Shift-Space toggles the
  global row loop. Stop returns every column to row 1.
- Drag the Volume Envelope to paint explicit `0.000..1.000` values; Option-click
  writes Previous.
- Open Rhythm Geometry from the toolbar or Window menu, then click a rhythm
  layer or legend to select that lane. Closing the pop-out does not stop
  playback.
- Open Membrane Kick with the toolbar button, Window menu, or Command-4.
  The editor initially shows only song instrument `00`. If another kick was
  added in the toolbox, choose its indexed tab or number key. Drag parameter bars or strike
  point, press Space to audition, press `R` to restore its patch, or choose one
  of five complete kick presets. The editor
  is visible in both builds, but audition and live parameter publication require
  `dev-audio`. Tab traverses its controls, arrows edit the focused parameter,
  Shift-arrows make fine adjustments, and Return activates a focused role or
  action button.
- Assign a Stereo Slice Sampler to a track, then double-click it or click its
  expand region. The Window menu and Command-7 open it too. Load a mono/stereo
  file, choose an equal-slice count and base note, select a slice, and audition
  it. Consecutive tracker notes starting at the base note address consecutive
  slices; each slice can be reversed independently.
- Open a MIDI OUT instrument editor and use `AUDITION C4` while transport is
  stopped. The selected endpoint should receive note 60 on that instrument's
  channel followed by a note-off 150 ms later.
- Open the performance mixer with the toolbar button, Window menu, or
  Command-6. Track `VEL / INPUT` scales future MIDI/internal Note On
  velocities; `M` and `S` gate NOTE sequences; `R` cycles the selected
  instrument. The MAIN OUT fader/mute affects internal post-decode audio only,
  never timestamped MIDI. Use Left/Right or Tab to select strips and
  Up/Down (Shift for fine changes) to adjust them.
- `:` or backtick focuses the console; Escape returns to the grid; Up/Down
  recalls history; Tab completes command names.

Useful console commands:

```text
help
demo
play | stop | panic
kit superior compact
aliases
@k x---x---x---x---
@h eu 7 16 1 <>
@k vel !.+-9
vol @s 127,96,64,96
bpm 126
swing 56
gate 70
instrument @k kick
instrument 6 1 floor
instrument 6 5 tom
instrument 6 9 high
len 6 ins 15
actions
fx @k 1 1 membrane.tune 0.28
select 4 7
hit 1 1 36
rest 1 2
repeat 3 5
kill 4 8
vel 2 5 112
len @o note 7
len @o vel 5
stride @h vel 2
dir @o palindrome
rotate @h 1
sieve @h 5 0 2
density @h 0.45
thin @h 0.2
rotatehits @h 3
humanize @h 0.35
fill @k 4
mute @o toggle
solo @k @s @h
unmute all
```

Lane and row arguments are one-based. The console is a small native command
language, not a Max command/file compatibility layer. See
[Live Command Language](LIVE_COMMANDS.md) for exact syntax and symbol maps.

## Visual and safety checks

- NOTE playheads appear as green lane marks in the tracker; Rhythm Geometry
  contains pulse polygons without static vertex dots. Its single yellow point
  appears only on a lane whose current sequencer step emitted a NOTE onset and
  disappears on rests. VOL playheads appear as amber lane-edge marks and the
  Envelope cursor.
- `SENT` counts successful note-ons. `DROP` combines bounded sequencer overflow
  and failed CoreMIDI packet submissions, including cleanup packets.
- In the live-audio build, `A-DROP`, `LATE`, and `CLK` must remain zero. A late
  callback event silences the block, makes the epoch fatal, resets the graph,
  and stops paired playback. A worker-detected scheduling underrun separately
  resets both outputs, waits through cleanup, and re-anchors before resuming;
  neither path attempts callback catch-up.
- Last-event text reports lane, MIDI note, velocity, and channel.
- The mixer track activity bars report current NOTE-step activity, not audio
  level. Only MAIN OUT has a rendered-audio peak bar; MIDI bypasses that bus.
- Repeated hits on the same channel/note send note-off immediately before the
  new note-on; a Kill cell sends the final note-off.
- Panic sends CC 123 on every active MIDI instrument's owned endpoint/channel
  immediately and again beyond every already-submitted MIDI timestamp plus the
  cleanup margin.
- Song opens a retained editor. Leave `SONG TRANSPORT` off to verify ordinary
  Pattern playback, then enable it and verify row duration/repeats, BPM/swing,
  lane mutes, natural completion/loop, live-row highlighting, and each queued
  quantization. Pattern-bank IDs populate the Song popup. Multi-pattern drafts
  save in schema 2, while enabling a cross-pattern Song must currently fail
  with the explicit prepared-boundary-switch message rather than substituting
  the active pattern.
