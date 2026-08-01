# NIM Gesture CLAP

`s3g Utility NIM Gesture` is a MIDI-only CLAP note effect paired with
`s3g Processor No Input Mixer 8ch`. It records free-running OXI E16 encoder
gestures without using the E16's finite internal motion tracks and returns the
played values to the E16 for LED-ring feedback.

## Signal path in REAPER

Use one track, with the FX in this order:

1. `s3g Utility NIM Gesture`
2. `s3g Processor No Input Mixer 8ch`

Set the track MIDI input to E16 USB Port 3 and Grid, then enable MIDI hardware
outputs to both E16 USB Port 3 and Grid. NIM Gesture generates canonical
channel-16 NRPN; No Input Mixer consumes it and forwards the channel-16 CC
stream for E16 ring feedback. It also outputs signed channels 1–4 poly-pressure
snapshots for the four BU16 matrix LED grids.

Turn **USB Thru off on the E16**. Otherwise the returned ring-feedback stream
can be sent back into REAPER and form a MIDI loop.

## Standalone application

The No Input Mixer standalone embeds this same CLAP before the embedded No
Input Mixer processor. Open **MIDI** in the application's output strip to pick
separate CoreMIDI destinations for E16 and Grid feedback, or leave either one
off. All CoreMIDI sources present at launch feed the Gesture processor;
non-channel-16 events pass through to No Input Mixer. The Gesture GUI and its
free-running loops are available from **EDIT NIM GESTURES**, and its state is
restored with the application.

The standalone's four-controller matrix protocol reserves MIDI channels 1–4
for four 4-by-4 button grids. Notes 0–15 cover one quadrant per controller.
`BU16 FLIP` moves an existing positive point toward `-1`, or an existing
negative point toward `+1`, then slews back to the stored value on release.
`BU16 LATCH` edits that same stored matrix: a strike on an empty point creates
it, release keeps it, and the next strike removes it. The initial Note On
velocity sets gain. Increasing polyphonic pressure during the following 50 ms
may raise that captured gain, which corrects falsely low native strike reports
without allowing the rest of the hold to rewrite the latch. Positive and
negative points use this same capture path. The `BU16 New Sign` parameter at
NRPN address 58 chooses the polarity of newly created points.
The shared `BU16 Ramp` parameter at NRPN address 59 controls both the FLIP
pressure/release transition and LATCH connection fades from 20 to 10000 ms;
the default is 1000 ms.
The processor sends the displayed value back as polyphonic pressure: s3g
orange LEDs show positive magnitude, cyan LEDs show negative magnitude, and
zero is dark.
Channel 16 remains reserved for the E16 NRPN and command protocol.

## Free recording

Record is global for a take, but the resulting loops belong to parameters:

1. Start Record.
2. Move any number of E16 controls.
3. Stop Record to set that take's free-running length.

Every parameter touched in that take receives or replaces its own loop. Loops
created in later takes can have different lengths, so the result becomes
polymetric without relying on tempo or REAPER transport. Existing loops keep
playing until their parameter is first touched during a new take. Cancel
discards the current take and restores those existing loops.

The CLAP exposes Record, Playback, Clear Last, Clear All, and Live Takeover in
the host's generic parameter view. Loop Count and Last Loop Length are
read-only status parameters. Loops are stored in CLAP project state.

Live Takeover suppresses a parameter's recorded playback briefly after a live
turn. The default is 650 ms.

## Native overview

The native GUI shows the complete 12-page `NIM P2` control surface as compact
4-by-4 E16 grids, ordered P01–P12 from left to right and top to bottom to match
page navigation on the hardware. Each inner ring shows the most recent live or
played 14-bit value; an outer colored ring marks a parameter that owns a loop.
A recording-touch ring changes color while the current take is replacing that
parameter.

Click an encoder to inspect its NRPN number, raw value, and loop length. Double
click it, or use **Clear Sel**, to clear that parameter's loop. Record,
Play/Pause, Clear All, Cancel, and Live Takeover are available in the top strip.
The ACTIONS card mirrors the E16 page for orientation. Its first five positions
show the shared gesture transport, positions 6–14 show the processor actions,
and its final two rings show BU16 Ramp and Output. The card deliberately does
not fire the hardware push actions from the GUI.

## E16 command notes

All commands use MIDI channel 16. They are consumed by NIM Gesture rather than
passed to No Input Mixer.

| Note | Command |
| ---: | --- |
| 112 | Toggle Record; stopping commits the take |
| 113 | Toggle Playback |
| 114 | Clear the last-touched parameter loop |
| 115 | Clear all loops |
| 116 | Cancel the current recording take |

All twelve pages of `NIM P2` assign these commands to the first five encoder presses,
so the gesture transport does not move when the E16 page changes. Their turn
actions remain the parameters shown on each page. On ACTIONS those five
positions are press-only; the remaining controls provide matrix mode/sign,
Seed, Forget, Random, Clear Matrix, BU16 Ramp, and Output/Panic. The same
gesture controls are also available in the native GUI and REAPER's generic
parameter view.

## Feedback scope

Live values passing through NIM Gesture and values generated by its loops are
returned as NRPN and update matching E16 rings. The final No Input Mixer plugin
also mirrors direct GUI and host-automation changes and sends throttled complete
snapshots after activation, Random, factory-preset recall, and project-state
restoration. Because NIM Gesture is upstream in the FX chain, that final state
mirror reaches the physical E16 output but does not travel backward into NIM
Gesture's own overview GUI.
