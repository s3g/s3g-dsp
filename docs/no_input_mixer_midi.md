# s3g Processor No Input Mixer 8ch — MIDI protocol

Status: development draft, version 0.4.

The No Input Mixer CLAP accepts a dedicated MIDI 1 controller stream on its
single `Controller MIDI In` note port. The initial implementation is designed
for the OXI E16 but keeps the wire protocol independent of any controller.

## E16 connection

- MIDI input channel: channel 16 (wire channel nibble `15`).
- Recommended E16 route: a dedicated virtual USB port, preferably USB3.
- Parameter turns: 14-bit NRPN.
- Momentary and toggle commands: Note On. Note Off is ignored.
- Factory preset recall: Program Change.

Using a dedicated port and channel prevents the command notes from colliding
with musical MIDI sent elsewhere in a setup.

## BU16 matrix and signed LED feedback

Four 4-by-4 note grids cover the complete 8-by-8 matrix. Each controller uses
notes 0–15 from left-to-right, top-to-bottom:

| Controller | MIDI channel | Destinations | Sources |
| --- | ---: | --- | --- |
| Upper left | 1 | 1–4 | 1–4 |
| Upper right | 2 | 1–4 | 5–8 |
| Lower left | 3 | 5–8 | 1–4 |
| Lower right | 4 | 5–8 | 5–8 |

The PATCH page exposes the automatable `BU16 Mode` parameter as two s3g-style
buttons:

- `BU16 FLIP`: pressure linearly moves an existing positive crosspoint toward
  `-1`, or an existing negative crosspoint toward `+1`. Polyphonic pressure
  updates a smoothed overlay while held, Note Off ramps back to the
  saved/effective value, and empty points remain empty.
- `BU16 LATCH`: the first Note On for an unlit point creates it and captures
  the native strike velocity as magnitude (`velocity / 127`). During the first
  roughly 50 ms, only a larger pressure value may raise that captured
  magnitude; smaller and later values are ignored. This correction path is
  shared by positive and negative polarity. Note Off—or Note On with velocity
  zero—only ends the physical hold and leaves the gain latched. Pressing a
  wired point removes it.

The continuous `BU16 Ramp` parameter at NRPN address `59` controls both modes
from `20` to `10000 ms` and defaults to `1000 ms`. It fades LATCH creation and
removal through the same DSP overlay used for FLIP pressure and release. The
stored matrix parameter changes to its destination immediately; the audible
graph, GUI crosspoint, and returned LED value follow the ramped effective gain.

`NEW +` and `NEW -` select the sign of newly created latch points. They control
the stepped `BU16 New Sign` parameter at NRPN address `58`. The E16 ACTIONS
page toggles it with one encoder press. FLIP and LATCH share the same stored
matrix: switching modes never clears wires. LATCH edits are normal saved matrix
values, so they remain intact across mode changes and project-state recall.
Clear Matrix and Random operate on that same matrix in either mode.

The generated BU16 profiles begin each press in native velocity mode
(`button_mode = -1`) and send the measured strike velocity as the first Note
On. They then switch that held pad to pressure mode (`button_mode = -2`) and
emit polyphonic pressure. Release sends Note Off and restores velocity mode
for the next strike. The separate message types let LATCH distinguish its
brief attack-peak correction from FLIP's continuous pressure.

The processor returns every displayed crosspoint on its MIDI output as
polyphonic pressure, using the same channel and note. The pressure value is a
signed seven-bit encoding:

| Pressure | Matrix value | BU16 LED |
| ---: | --- | --- |
| 64 | zero | off |
| 65–127 | positive, increasing magnitude | s3g orange fade |
| 63–0 | negative, increasing magnitude | s3g cyan fade |

Effective ramp changes are emitted as they move. A complete snapshot is repeated once per
second so a controller, MIDI route, or standalone destination can reconnect
without leaving stale LEDs. Feedback events carry `CLAP_EVENT_DONT_RECORD`.
The generated Grid profiles are in `controllers/intech_grid_bu16`. Their
System Setup installs `self.midirx_cb`, the callback API required by current
Grid firmware for host-to-controller MIDI. Reload profiles made before this
callback was added if note input works but LEDs remain dark.

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
| Global parameters | CLAP IDs `1` through `59` |
| Matrix source `s` to destination `d` | `100 + 8 * d + s` |
| Lane direct parameter | `1000 + 100 * lane + offset` |
| Lane insert parameter | `1000 + 100 * lane + 20 + 10 * slot + field` |

Lane, slot, source, and destination numbers in the formulas are zero-based.
Published IDs must not be reassigned to different parameters.
Global parameter `57` is the stepped `BU16 Mode`: `0` is Flip and `1` is
Latch. Global parameter `58` is the stepped `BU16 New Sign`: `0` is positive
and `1` is negative. For a conventional E16 NRPN mapping, raw value `0` selects
positive and `16383` selects negative.
Global parameter `59` is continuous `BU16 Ramp`, from `20` to `10000 ms`, with
a default of `1000 ms`.

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

## E16 parameter-state feedback

The processor mirrors its current parameter values as channel-16 NRPN on its
final MIDI output. Direct GUI edits and host-automation changes are sent on the
next process block. Activation, Random, factory-preset recall, and project-state
restoration produce a complete current-state snapshot. The snapshot is
throttled to sixteen parameters per block so a large patch change does not
place hundreds of MIDI events in one audio callback.

The output uses the same stable parameter IDs and value conversions documented
above. All generated CC events carry `CLAP_EVENT_DONT_RECORD`. An NRPN received
from the E16 is forwarded and marked as already synchronized, preventing an
extra duplicate state message for an encoder turn. Route the processor's final
MIDI output to the E16 USB3 port and leave E16 USB Thru off.

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
| 117 | Select BU16 Flip mode |
| 118 | Select BU16 Latch mode |
| 119 | Toggle the sign of newly latched wires |
| 120 | New excitation seed |
| 121 | Forget |
| 122 | Random, medium-energy profile |
| 123 | Panic |
| 124 | Clear the complete 8-by-8 matrix |
| 125 | Random, low-energy profile |
| 126 | Random, high-energy profile |

All three Random commands generate free-running movement and set Tempo Sync
off. Sync can be enabled deliberately after randomization when host-clocked
matrix movement is wanted.

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
- The first performance scene is generated from
  `controllers/oxi_e16/generate_no_input_mixer_scene.mjs`; full-matrix and
  independent insert deep-edit scenes are not generated yet.
