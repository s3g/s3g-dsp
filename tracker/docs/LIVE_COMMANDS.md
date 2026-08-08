# Live Code command reference

Live Code is the tracker-facing performance language embedded in the s3g
Tracker MIDI CLAP. It mutates a candidate pattern and publishes only after the
complete command validates, so an error leaves playback state unchanged.

Press `:` or backtick to focus the entry field. Enter runs a command, Up/Down
recalls history, Tab completes command names, and Escape returns to the grid.
Lane and row addresses are one-based. Targets accept a lane number or an
`@alias`.

The in-app Help window and `help` command are generated from the current
command registry. They are the authority when this source reference and a
build disagree.

## Essentials

```text
help
actions
demo
play
stop
panic
```

`actions` lists only the sequencing behaviors accepted by `SEQ1` and `SEQ2`.
The MIDI product has no internal-instrument parameter actions or scopes.

## Targets, tracks, kits, and aliases

```text
kit gm compact
kit superior basic
kit superior toms

aliases
alias kick 1
alias hats 3
@snare = 2
@kick

select @hats 9
name @hats CLOSED HAT
track add PERCUSSION
track remove 9
```

Kits establish useful lane names, MIDI note anchors, channels, and aliases.
Available templates are `compact`, `basic`, and `toms`; maps are `gm` and
`superior`. Track count is bounded at 32.

Aliases begin with a letter and contain letters, digits, or underscore. They
are case-insensitive and can be used anywhere a lane target is accepted.

## Notes and rhythms

```text
hit @kick 1
hit @snare 5 38
rest @kick 3
repeat @hats 7
kill @hats 16
note @kick 9 C-2

mask @kick x---x---x---x---
@hats x-x-x-x- <>
eu @hats 7 16
euclid @kick 5 16 2 palindrome
```

Mask symbols `x`, `X`, and `1` are hits; `-`, `.`, `_`, and `0` are rests.
Writing a mask also sets NOTE length. Directions accept `forward`, `reverse`,
`palindrome`, and `random`, or `>`, `<`, `<>`, and `?`.

## Generative NOTE operations

```text
rotate @kick 3
reverse @hats
fill @snare 4 1
sieve @hats 16 0 3 7 11
density @hats .65
thin @hats .20
rotatehits @kick -1
humanize @hats .15
```

These operations write explicit NOTE cells. Randomized commands use the
tracker's deterministic state rather than hiding a runtime random process.

## Volume

```text
vel @kick 1 127
vol @kick 1.0 .82 .9 .74
@hats vol .7 .45 .62 .5
randomize @hats vol .35 .85
```

`VOL` is displayed and edited as normalized `0.000`–`1.000`; `vel` remains a
convenient MIDI `0`–`127` spelling. MIDI output maps the resolved normalized
value back to velocity `0`–`127`.

## Polymetric columns

```text
len @kick note 16
len @kick vol 13
len @kick fx1 7
len @kick v1 5

stride @kick fx1 2
phase @kick v1 -1
dir @kick note palindrome
mute @kick fx2 toggle
unmute @kick
unmute all
solo @kick @snare
```

The visible columns are `note`, `vol`, `fx1`, `v1`, `fx2`, and `v2`.
`length` aliases `len`; `speed` and `spd` alias `stride`; `ph` aliases
`phase`; and `mode` aliases `dir`.

## Sequencing actions

```text
actions
fx @kick 1 1 RR 0.50
fx @kick 2 5 PR 0.75
fx @kick 1 9 previous
fx @kick 2 9 clear
fxvalue @kick 1 5 0.40
fxv @kick 2 5 previous

fx1 @kick RR !.=-
fx2 @kick EU .25,.5,=,-
```

Pair accepts `1`, `fx1`, or `f1`, and `2`, `fx2`, or `f2`. Direct action
keys are case-insensitive and accept a code (`RR`), name (`ratchet`), or stable
name (`seq.ratchet`). Values are finite normalized floats. Compact sequences
accept normalized lists or `! + * o . 0..9`; `=` recalls and `-` rests.

Convenience writers choose the first available pair on the requested row:

```text
probability @kick 1 75%
ratchet @kick 5 .5
microtime @snare 5 .35
delay @snare 9 .25
flam @snare 13 .4
stutter @hats 7 .7
skip @hats 8 .5
offset @kick 9 .625
repeatprev @hats 12 .4
accent @kick 1 1.0
ghost @snare 3 .35
euclidfx @hats 1 .5
```

See [Sequencing columns](FX_COLUMNS.md) for the complete action/value table
and realtime resolution order.

## Swing, gate, loop, and warps

```text
swing 56
gate 80
loop rows 5 16
loop on

warps
warp clear
warp cycle 8
warp exp 2.0 mix .6
warp step 5 segment 0 .5
warp eu 5 8 repeat 2
```

Tempo itself follows REAPER and is scaled by the editor's `RATE` menu. Swing
accepts `0.50`–`0.75` or `50`–`75`; gate is in milliseconds. Warp options
include `mix`/`alpha`, normalized `segment <begin> <end>`, and
`repeat <count>`.

## Alias-first shorthand

```text
@kick x---x---x---x---
@hats vol .7 .5 .6 .4
@hats eu 7 16
@hats palindrome
```

The alias can move before any lane-targeted operation. This makes short live
performance commands readable without introducing a second command language.

## Product boundary

Live Code does not set host BPM, select audio devices, assign internal
instruments, or write audio-effect parameters. Per-track MIDI bus and channel
routing live in the lane header; REAPER owns devices, instruments, audio, and
mixing.
