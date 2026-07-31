# Four Intech Grid BU16 modules: No Input Mixer matrix

No Input Mixer accepts four 4-by-4 note grids as one velocity-sensitive 8-by-8
feedback matrix. The mapping works identically in the CLAP and standalone app.

Configure each BU16 to emit notes 0–15 from left to right and top to bottom,
with a distinct MIDI channel:

| BU16 | MIDI channel | Destination rows | Source columns |
| ---: | ---: | --- | --- |
| 1, upper left | 1 | 1–4 | 1–4 |
| 2, upper right | 2 | 1–4 | 5–8 |
| 3, lower left | 3 | 5–8 | 1–4 |
| 4, lower right | 4 | 5–8 | 5–8 |

A note-on introduces a momentary matrix connection whose magnitude is
`velocity / 127`. If the saved crosspoint is negative, the played connection
keeps that negative polarity; an empty or positive crosspoint plays positive.
Note-off, including note-on with velocity zero, releases the overlay and
restores the saved matrix value. PANIC releases all held connections.

The overlay does not rewrite plug-in state or automation. The active point is
shown in the WIRES and GRID views. MIDI channel 16 is deliberately excluded
because it is reserved for OXI E16 NRPN and No Input Mixer command notes.
