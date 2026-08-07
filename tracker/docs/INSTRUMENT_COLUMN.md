# Track defaults and per-row instrument overrides

Every track points to a zero-based entry in the song instrument index in the
right toolbox.
That default is the normal, readable choice for a lane. `INS` is an optional
polymetric override column for the cases where changing instruments inside one
track is musically useful; a project does not need to fill it merely to select
an instrument.

The initial song index is:

| Index | Instrument | Output |
| ---: | --- | --- |
| `00` | `MEMBRANE KICK` | internal audio |
| `01` | `STEREO SLICE SAMPLER` | internal stereo audio |
| `02` | `MIDI OUT` | its own CoreMIDI endpoint/channel |

`AVAILABLE` is a type library, not another set of tracker slots. Pressing
`+ ADD` creates the next song index (`03`, then `04`, etc.) and assigns a
separate prepared device. Current capacities are five Membrane Kicks, three
stereo samplers, three instances of each DaisySP drum type, and eight MIDI OUT devices. Every internal
instance owns independent synthesis/patch state; every MIDI OUT owns an
independent endpoint and channel edited in the bottom Device View.

MIDI OUT is an instrument, not a second routing switch. Choosing an internal
instrument never emits MIDI, and choosing MIDI OUT never also triggers the
internal graph. A future layered audio+MIDI behavior should be represented by
an explicit stack instrument whose contents are visible in the rack.

## Cell states

| Cell | Display | Meaning |
| --- | --- | --- |
| Empty | `---` | Retain remembered instrument. |
| Previous | `PRV` | Explicitly retain remembered instrument. |
| Instrument | decimal `I00` index | Replace memory with that song instrument. |

The track default seeds memory at transport reset. The INS field keeps its own
length, stride, direction, mute state, and playhead. It advances while muted,
but a muted column does not update memory. A cell can change memory on a rest
to prepare the instrument for a later note.

At each lane tick the scheduler reads INS, resolves NOTE/VOL and lane-relative
FX, then emits `NoteOff`, parameters, and `NoteOn` in deterministic order. An
active note retains its concrete rack node, so an instrument change releases
the old node at the next lane tick without stopping transport; the next onset
then reaches the new node. Output destination is derived from that resolved
node for every event.

## Editing

Click a song-index row in the right toolbox to assign it as the selected
track's default. In INS, press Return or double-click and type a populated
decimal index, `PRV`, or `CLEAR`. The compact direct keys remain available:

- `1..7`: write a populated song index and advance;
- `0` or Delete: Empty;
- `R`: Previous;
- `[` and `]`: cycle indices;
- `X`: write the current track default.

Console lane/row numbers are one-based; song indices are zero-based. Additional
kick, sampler, DaisySP drum, and MIDI instances are assigned through the toolbox:

```text
instrument 1 kick
instrument 2 sampler
instrument 3 midi
instrument 2 5 2
instrument 2 9 previous
instrument 2 13 clear

track add Slice Lead
track remove 4
len 2 ins 15
stride 2 ins 2
dir 2 ins palindrome
```

The rack boundary is intentionally independent of NOTE pitch. MIDI OUT uses
the NOTE value and the selected MIDI instrument's channel; track MIDI channels
are retained only as legacy pattern data and do not override device ownership.
Internal instruments interpret the same canonical pitch through their own
voice model.
