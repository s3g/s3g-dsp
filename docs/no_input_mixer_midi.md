# s3g Processor No Input Mixer 8ch — MIDI protocol

Status: development draft, version 0.1.

The No Input Mixer CLAP accepts a dedicated MIDI 1 controller stream on its
single `Controller MIDI In` note port. The initial implementation is designed
for the OXI E16 but keeps the wire protocol independent of any controller.

## Connection

- MIDI input channel: channel 16 (wire channel nibble `15`).
- Recommended E16 route: a dedicated virtual USB port, preferably USB3.
- Parameter turns: 14-bit NRPN.
- Momentary and toggle commands: Note On. Note Off is ignored.
- Factory preset recall: Program Change.

Using a dedicated port and channel prevents the command notes from colliding
with musical MIDI sent elsewhere in a setup.

## NRPN parameter addressing

The NRPN parameter number is the stable CLAP parameter ID. A conventional
MIDI 1 NRPN sequence is accepted:

1. CC 99: parameter MSB.
2. CC 98: parameter LSB.
3. CC 6: data MSB.
4. CC 38: data LSB.

CC 6 is applied as a coarse value and CC 38 refines it. Selecting an RPN with
CC 100 or CC 101 clears the active NRPN selection. The null NRPN `127/127` is
ignored.

The current parameter address families are:

| Family | NRPN parameter number |
| --- | --- |
| Global parameters | CLAP IDs `1` through `56` |
| Matrix source `s` to destination `d` | `100 + 8 * d + s` |
| Lane direct parameter | `1000 + 100 * lane + offset` |
| Lane insert parameter | `1000 + 100 * lane + 20 + 10 * slot + field` |

Lane, slot, source, and destination numbers in the formulas are zero-based.
Published IDs must not be reassigned to different parameters.

Lane direct offsets:

| Offset | Parameter |
| ---: | --- |
| 0 | Body |
| 1 | Loss |
| 2 | Level |
| 3 | Mute |
| 4 | Low EQ |
| 5 | Mid frequency |
| 6 | Mid gain |
| 7 | High EQ |
| 8 | Aux A send |
| 9 | Aux B send |
| 10 | Tune note |
| 11 | Tune cents |
| 12 | Pitch lock |
| 13 | Aux A tap |
| 14 | Aux B tap |
| 15 | Aux A return |
| 16 | Aux B return |

Insert fields:

| Field | Parameter |
| ---: | --- |
| 0 | Type |
| 1 | Gain |
| 2 | Tone |
| 3 | Bias |
| 4 | Level |
| 5 | Bypass |

## NRPN value conversion

The Data Entry pair forms an unsigned value from `0` through `16383`.
Continuous parameters map that normalized value over their published CLAP
minimum and maximum. Stepped parameters are rounded to the nearest valid
integer. Bipolar parameter center is value `8192`.

Lane mid-frequency parameters use an exponential mapping over 80–8000 Hz so
the encoder has useful resolution throughout the audible range. Other
parameters that already expose a normalized perceptual control, including
event rate, event length, slew, and movement rate, retain their existing
normalized mapping.

Every accepted MIDI parameter change is returned to the host as a live CLAP
parameter event with `CLAP_EVENT_DONT_RECORD`. This keeps the plug-in state and
host display current while allowing the original MIDI performance to be
recorded without duplicate parameter automation.

## Note commands

Only Note On messages with non-zero velocity on channel 16 trigger commands.

| Note | Command |
| ---: | --- |
| 32–39 | Toggle lane mute 1–8 |
| 40–47 | Kill lane 1–8 |
| 48–55 | Toggle Insert 1 bypass for lanes 1–8 |
| 56–63 | Toggle Insert 2 bypass for lanes 1–8 |
| 64–71 | Toggle Insert 3 bypass for lanes 1–8 |
| 72 | Toggle Aux A mute |
| 73 | Toggle Aux B mute |
| 80–87 | Toggle pitch lock for lanes 1–8 |
| 120 | New excitation seed |
| 121 | Forget |
| 122 | Random, medium-energy profile |
| 123 | Panic |
| 124 | Clear the complete 8-by-8 matrix |
| 125 | Random, low-energy profile |
| 126 | Random, high-energy profile |

Mute and bypass commands toggle the current plug-in value rather than a value
remembered by the controller. They therefore remain correct after preset or
project-state changes.

## Program Change

Program Change values `0` through `19` recall the twenty current factory
presets. The current Variance value is applied in the same way as a GUI preset
recall. Controller interfaces may display these wire values as programs 1–20.

## Initial implementation limits

- The control channel is fixed to channel 16.
- Soft takeover is not implemented yet.
- The CLAP has no MIDI output port and does not send NRPN feedback to the E16.
- The first performance scene is generated from
  `controllers/oxi_e16/generate_no_input_mixer_scene.mjs`; full-matrix and
  independent insert deep-edit scenes are not generated yet.
