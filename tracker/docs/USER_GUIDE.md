# s3g Tracker user guide

## What s3g Tracker is

s3g Tracker is a MIDI-generating CLAP instrument for REAPER. It follows the
host transport and tempo, sequences up to 32 polymetric tracks, and sends
sample-offset MIDI notes through eight CLAP MIDI output buses. It does not
generate audio or host instruments. Put a drum instrument or another MIDI
receiver after it in REAPER.

This guide describes the current MIDI CLAP product. Older standalone audio,
instrument-rack, mixer, and audio-device documents in this source tree are
design history rather than instructions for the current plug-in.

## REAPER setup

1. Insert **s3g Tracker** before an instrument in an FX chain, or route its
   MIDI output buses to other REAPER tracks.
2. Open the plug-in editor.
3. Choose each track's `B01`–`B08` output bus and `CH01`–`CH16` MIDI channel
   in that track's header.
4. Route or filter those buses in REAPER as required by the receiving
   instruments.

Tempo comes from REAPER. `RATE` applies a musical ratio to the host tempo:
`1/4×`, `1/2×`, `2/3×`, `1×`, `3/2×`, `2×`, or `4×`.

## Main grid

Every track displays six columns at once:

| Column | Purpose |
| --- | --- |
| `NOTE` | MIDI note, rest, retrigger, or kill |
| `VOL` | Normalized `0.000`–`1.000` velocity |
| `SEQ1` | First sequencing action |
| `V1` | Normalized value for `SEQ1` |
| `SEQ2` | Second sequencing action |
| `V2` | Normalized value for `SEQ2` |

Each column owns its own length, stride, phase, direction, mute state, and
playhead. A track can therefore combine, for example, a 16-row note pattern,
a 7-row volume pattern, and a 5-row probability pattern.

Each column header has separate length, direction, and mute rows. Length is
shown as `L<length>×<stride>`; double-click that row to type a length. Click
the `DIR` row to cycle forward `>`, reverse `<`, palindrome `<>`, and random
`?`. `MUTE` remains isolated on the bottom row. Stride and phase can also be
edited through Live Code.

Double-click a track name to rename it. The colored strip identifies the same
track in the grid and Rhythm Geometry.

## Entering cells

Double-click a cell, type a value, and press Return.

### NOTE

- MIDI integer: `60`
- Tracker note name: `C-4`, `C#4`, or another accepted MIDI note spelling
- Rest: `---` or `rest`
- Retrigger previous note: `RPT` or `repeat`
- Kill active note: `KIL` or `kill`

### VOL, V1, and V2

- Enter a normalized value from `0.000` to `1.000`.
- `PRV` or `previous` retains the remembered value.
- `DEF` or `default` restores default volume in `VOL`.

MIDI output converts normalized `VOL` to velocity `0`–`127`.

### SEQ1 and SEQ2

Right-click a sequencing cell to open the action chooser. The menu shows the
abbreviation, full name, and value meaning. Choosing a new action initializes
an unset adjacent value to `0.500`.

Direct entry remains available: double-click and type an abbreviation such as
`RR`, a name such as `ratchet`, or a stable name such as `seq.ratchet`.
Use `PRV` to recall the remembered action and `---` to clear the cell.

## Sequencing actions

| Code | Action | Meaning of `V1` or `V2` |
| --- | --- | --- |
| `RR` | Ratchet | 2–8 onsets inside one tracker tick |
| `MT` | Microtime | Early at `0`, centered at `0.5`, late at `1` |
| `DL` | Delay | 0–1 complete tracker tick |
| `FL` | Flam | 6–60 ms quieter secondary onset |
| `ST` | Stutter | 2–8 onsets extending across following ticks |
| `AC` | Accent | 0.5×–1.5× onset velocity |
| `GL` | Ghost | Half-tick secondary onset at 0.15–0.60 velocity scale |
| `PR` | Probability | Deterministic 0–100% note gate |
| `SK` | Skip | Play once per 2–8 visits to the row |
| `OF` | Note Offset | Read the NOTE source from four rows back to four ahead |
| `RP` | Repeat Previous | Probability of filling an empty source with the last emitted note |
| `EU` | Euclidean Gate | One through NOTE-length Euclidean hits |

When both pairs resolve the same kind of source transform, `SEQ2` wins.
Probability, skip, offset, repeat-previous, and Euclidean gating resolve the
note source before timing expansion. Ratchet, microtime, delay, flam, stutter,
accent, and ghost then shape the accepted onset.

## Selection and keyboard editing

Tracker-local shortcuts use Control so REAPER retains Command shortcuts.

| Keys | Action |
| --- | --- |
| Arrow keys | Move among cells and rows |
| Shift-Left / Shift-Right | Move between tracks |
| Home / End | First or last row |
| Page Up / Page Down | Move by a visible page |
| F9–F12 | Jump to 0%, 25%, 50%, or 75% of the pattern |
| Control-A | Select all tracker cells |
| Control-C / X / V | Copy, cut, or paste tracker cells |
| Control-= / - / 0 | Zoom in, zoom out, or reset tracker zoom |
| Delete or Backspace | Clear the selected cell |
| `:` or backtick | Focus the Live Code field |
| Escape | Leave an editor or return focus to the grid |
| Space | Request host play or continue |
| Shift-Space | Toggle the tracker loop |

Drag vertically on a `VOL`, `V1`, or `V2` cell to adjust its normalized value;
Option-drag is fine and Shift-drag is coarse. Control-drag from a numeric cell
when you want a rectangular selection instead. Double-click still provides
exact text entry. Drag the row-number gutter to set the global loop region
across all track columns.

## Patterns and Song

The pattern bank stores multiple named patterns inside the plug-in state.
Create, duplicate, rename, delete, and select patterns from the transport
area. Every edit is written to the active pattern automatically and marks the
REAPER project dirty; there is no separate pattern-save step. Song mode
arranges pattern references into a longer form. Pattern-bank
editing is locked while Song playback is active so a playing arrangement
cannot acquire unresolved references.

Toolbar number fields support vertical dragging: up increases and down
decreases. Option-drag provides fine control, Shift-drag is coarse, and a
click or double-click retains direct numeric entry.

In the lower value-envelope editor, large saturated-cyan breakpoints are rows
containing an active authored NOTE or retrigger; smaller dark-gray breakpoints
have no note at that row. The selected breakpoint retains a light outer
selection marker without hiding its note-presence color.

Each tracker-column header separates its label, length, direction, and mute
controls into four rows. Double-click the length row to edit it, or click the
DIR row to cycle direction. The bottom MUTE row is the only mouse target that
toggles that column's mute state.

The play button requests REAPER play/continue. The restart arrow returns the
tracker scheduler to row 1 without stopping REAPER. Panic releases every note
tracked as active and sends MIDI CC 123 (All Notes Off) on all 16 channels of
all eight tracker output buses.

REAPER project state stores the complete tracker project. The tracker does not
attempt to import Max tracker files or maintain compatibility with abandoned
standalone formats.

## Live Code

Press `:` (Shift-semicolon on a Mac keyboard) or backtick to focus the Live
Code field. Enter runs a command, Up/Down recalls history, Tab completes
command names, and Escape returns to the grid. Invalid commands leave the
session unchanged.

Useful starting commands:

```text
help
actions
aliases
alias kick 1
kit superior basic
drumscene techno 101
scene balanced 101
generateseed sketch 0.48 0.55 0.18
mutate 0.12 notes
variation scene drift 202
variation drumscene blast 666 launch beat
mask @kick x---x---x---x---
eu @kick 5 16
vol @kick 1.0 .8 .9 .7
randomize @kick vel .55 1.0
fx @kick 1 1 RR 0.50
fx @kick 2 5 PR 0.75
len @kick note 16
len @kick fx1 7
stride @kick v1 2
phase @kick fx2 -1
dir @kick note palindrome
mute @kick fx1 toggle
warps
warp exp 2.0 mix .5
```

`actions` lists only the sequencing behaviors accepted by `SEQ1` and `SEQ2`.
Internal-audio parameter actions and scopes are not part of the MIDI tracker.

## Tool pages and windows

- **Geometry** visualizes active, unmuted note polygons. A large yellow point
  marks a current note hit, then leaves a short radially and temporally fading
  halo without leaving a false hit point. Use its zoom controls for dense
  patterns.
- **Warps** edits the functional timing-warp stack.
- **Console** displays Live Code results and errors.
- **Help** contains the generated command reference and sequencing-action
  list.

Geometry, Warps, and Console can be detached with their arrow button or by
double-clicking the page tab. They remain views of the same live tracker state.

## Current boundaries

- MIDI output only; no audio ports or internal instruments.
- REAPER owns tempo, transport, devices, instruments, audio, and mixing.
- Eight CLAP MIDI output buses, with a separate channel per tracker track.
- Stereo or multichannel sound belongs to downstream instruments, not this
  plug-in.
- Command-key shortcuts are intentionally left to REAPER.
