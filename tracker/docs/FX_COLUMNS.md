# Sequencing columns

Every MIDI track owns two action/value pairs: `SEQ1/V1` and `SEQ2/V2`.
They preserve the v8 tracker's polymetric sequencing behavior without
exposing audio-effect names, plug-in parameter IDs, hexadecimal opcodes, or
the abandoned internal-instrument rack.

## Cell memory

The action and value sides are independent columns. Each has its own length,
stride, phase, direction, mute state, and playhead.

| Cell | Playback behavior |
| --- | --- |
| Action Empty (`---`) | Emit nothing while retaining remembered action state |
| Action Previous (`PRV`) | Execute the remembered action with the remembered value |
| Sequencing code | Remember and execute that typed sequencing action |
| Value (`0.000`–`1.000`) | Replace value memory before resolving the action |
| Value Previous (`PRV`) | Retain value memory |

A value row can update memory while its action row is empty. A later `PRV`
action then uses that value. Muting an action behaves like Empty; muting a
value freezes its memory. Both muted columns continue advancing, so unmuting
does not reset phase.

If both pairs resolve the same source transformation, `SEQ2` wins. Equal-time
event ordering is deterministic.

## Action chooser and direct entry

Right-click a `SEQ1` or `SEQ2` cell to open the complete action chooser. The
menu shows each abbreviation, full name, and normalized value meaning.
Choosing an action initializes an unset adjacent value to `0.500`.

Double-click retains direct tracker entry. These forms are equivalent:

```text
RR
ratchet
seq.ratchet
```

Use `---` or `clear` for Empty and `PRV` or `previous` for recall.

## Current action catalog

| Code | Stable name | Action | Normalized value |
| --- | --- | --- | --- |
| `RR` | `seq.ratchet` | Ratchet | 2–8 onsets within one tick |
| `MT` | `seq.microtime` | Microtime | Early / center / late |
| `DL` | `seq.delay` | Delay | 0–1 tick |
| `FL` | `seq.flam` | Flam | 6–60 ms secondary onset |
| `ST` | `seq.stutter` | Stutter | 2–8 onsets over following ticks |
| `AC` | `seq.accent` | Accent | 0.5×–1.5× velocity |
| `GL` | `seq.ghost` | Ghost | Half-tick hit at 0.15–0.60 velocity scale |
| `PR` | `seq.probability` | Probability | 0–100% deterministic gate |
| `SK` | `seq.skip` | Skip | One play per 2–8 visits |
| `OF` | `seq.offset` | Note Offset | Source row offset from −4 to +4 |
| `RP` | `seq.repeat_previous` | Repeat Previous | 0–100% fill probability |
| `EU` | `seq.euclid` | Euclidean Gate | 1 through NOTE-length hits |

`OF` and `RP` resolve the source note first. `PR`, `SK`, and `EU` gate the
candidate next. The accepted note then enters the bounded timing expansion
used by `RR`, `MT`, `DL`, `FL`, `ST`, `AC`, and `GL`.

## Live Code

```text
actions
fx @kick 1 1 RR 0.50
fx @kick 2 5 PR 0.75
fx @kick 1 9 previous
fx @kick 2 9 clear
fxvalue @kick 1 5 0.40
fxvalue @kick 2 5 previous

fx1 @kick RR !.=-
fx2 @kick EU .25,.5,=,-

len @kick fx1 15
stride @kick v1 2
phase @kick fx2 -1
dir @kick v2 palindrome
mute @kick fx1 toggle
```

Lane and row positions are one-based. Pair accepts `1`, `fx1`, or `f1` for
the first pair and `2`, `fx2`, or `f2` for the second. Keys are
case-insensitive. Values must be finite normalized floats from 0 to 1.

Compact sequences accept normalized comma-separated values or the symbols
`! + * o . 0..9`; `=` recalls and `-` rests. Commands are transactional:
unknown actions and invalid values leave the session unchanged.

The MIDI product intentionally rejects former membrane parameter names and
parameter scopes. `actions` lists only choices that can be authored in the
visible `SEQ1` and `SEQ2` cells.

## Realtime boundary

The scheduler expands sequencing operations into a fixed-capacity event
timeline, emits only events due in the current host block, and does not
allocate in realtime. Microtime moves the release/action/onset row bundle
together. Kill and retrigger cancel matching delayed primary onsets so timing
effects cannot leave orphan notes.
