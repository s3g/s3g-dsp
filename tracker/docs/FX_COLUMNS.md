# Typed FX columns

Every track has two action/value pairs: `FX1/V1` and `FX2/V2`. They preserve
the useful polymetric behavior of the v8 tracker while replacing short string
opcodes with typed actions and stable parameter keys.

## Cell behavior

The action and value sides are separate columns. Each has its own length,
stride, direction, mute state, playhead, and remembered value.

| Cell | Playback behavior |
| --- | --- |
| FX Empty (`---`) | Emit nothing; retain the remembered action. |
| FX Previous (`PRV`) | Execute the remembered action with the current remembered value. |
| FX parameter mnemonic | Remember and execute that typed parameter action. |
| Value (`0.000`…`1.000`) | Replace value memory before the action is resolved. |
| Value Previous (`PRV`) | Keep value memory unchanged. |

This means a value row may update memory while its action row is empty, then a
later Previous action can use it. Muting an FX column behaves like Empty;
muting a value column freezes its memory. Both muted columns continue advancing
so unmuting does not reset phase.

Parameters emit even on NOTE rests. Lane-relative actions resolve after the
current `INS` cell updates instrument memory. If both pairs address the same
resolved node, parameter, and scope on one tick, the later pair (`FX2`) wins.
Equal-sample
ordering is deterministic: an old note release, surviving parameter actions in
pair order, then the new onset. A note-scoped action uses the new onset's note
identity and node, or the active identity and onset node on a rest; it is
suppressed when neither exists. That Note behavior is part of the canonical
scheduler contract for future catalogs. The current membrane action catalog
supports Global scope only.

## GUI

Use the page button beside the module controls to cycle pages. Tab/Shift-Tab
traverses individual fields and wraps across the same sequence:

```text
NOTE / INS / VOL -> FX1 / V1 -> FX2 / V2
```

The left and right halves of a lane select the action and value fields. Action
cells show three-letter catalog mnemonics; values remain normalized and
readable rather than hexadecimal. On an FX page:

- double-click or `X` writes the page default (`membrane.tune` for FX1,
  `membrane.decay` for FX2) and initializes an unset value to `0.5`;
- Delete/`0` writes Empty;
- `R` writes Previous;
- `[` and `]` adjust the normalized value;
- the bottom envelope paints V1 or V2, with Option-click writing Previous.

The direct grid gesture is intentionally a fast default, not the eventual
action browser. Use the console to select any catalog action and one of the
scopes that action advertises; every current membrane action advertises only
Global.

## Console

```text
actions
fx @k 1 1 membrane.tune 0.28
fx @k 1 5 previous
fx @k 2 1 membrane.decay 0.22 global
fx @k 2 9 membrane.click 0.80 global
fx @k 2 9 clear
fxvalue @k 1 5 0.40
fxvalue @k 1 5 previous

len @k fx1 15
len @k v1 11
stride @k fx2 2
dir @k v2 palindrome
mute @k fx1 on
mute @k v1 off
```

Lane and row positions are one-based. Pair accepts `1`, `fx1`, or `f1` for
the first pair and `2`, `fx2`, or `f2` for the second.
Action keys are case-insensitive; the `membrane.` prefix may be omitted, and
the mnemonic printed by `actions` is accepted. Values must be finite normalized
floats from 0 to 1. Scope is optional and defaults to `global`; accepted values
in the command grammar are `global`, `channel`, and `note`, but the selected
catalog action must advertise that scope. Each current membrane rack instance
has one global parameter state, so its Channel and Note requests are rejected.

Sequencer actions share the same cells but do not take a parameter scope:

```text
fx @k 1 1 RR 0.50
fx @k 2 5 MT 0.25
fx @k 1 9 seq.stutter 1.0
```

`RR` emits 2–8 evenly spaced onsets inside a nominal tempo tick; `MT` maps 0/0.5/1 to
early/center/late around the compensated lookahead; `DL` delays up to one tick;
`FL` adds a 6–60 ms quieter hit; `ST` emits 2–8 hits across future ticks; `AC`
scales VOL by 0.5–1.5; and `GL` adds a half-tick hit at 0.15–0.60 velocity.
RR/DL/ST/GL use the straight nominal tick duration even when swing or a
functional warp moves the primary tracker rows; this preserves the v8 effect
contract. FX2 wins when both pairs recall the same timing action.

Commands mutate a candidate session and publish only after complete validation.
Unknown actions, invalid rows/pairs/scopes, and non-finite/out-of-range values
leave the live session unchanged.

## Instrument behavior

FX columns always advance their own playheads and memory, but parameter actions
execute only when the resolved rack instrument is internal. A MIDI OUT step
does not send membrane parameters, MIDI CC, or any other automation. There is
no user-facing Both route; use separate tracks for internal/MIDI doubling.

## Scheduler boundary

Timing operations are typed sequencer actions, not downstream CLAP parameters.
The default scheduler expands them into an allocation-free 8192-event timeline,
drains only events due in the current block, and orders equal-time events by
track then NoteOff, Parameter, NoteOn. Overflow fails closed before audio/MIDI
fanout. MT moves the release/parameter/onset row bundle together. A Kill or
retrigger cancels a matching primary onset that is still pending at or after
its release, preventing an orphan delayed note.

`OF` and `RP` resolve the primary NOTE source first; `PR`, `SK`, and `EU` then
gate that candidate before timing expansion. FX2 wins duplicate actions. Kill
and NOTE mute are hard releases that source transforms cannot resurrect, and a
rejected candidate neither allocates a note identity nor releases the current
voice. Accepted-note memory, probability RNG, and per-source-row skip counters
are deterministic across render-block partitioning. Parameter actions still
execute on gated rows according to their scope.
