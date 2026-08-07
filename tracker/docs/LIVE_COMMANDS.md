# Live command language

The console is a native, deliberately small performance language. It borrows
the most useful ideas from the v8 Max tracker without reproducing its parser or
file formats. Lane and row positions are one-based everywhere.

Press `:` or backtick to focus the console. Enter runs a command, Up/Down
recalls history, Tab completes command names, and Escape returns focus to the
tracker grid. A failed command leaves the complete session unchanged.

The toolbar `HELP` button or `Window > Console Help` (`Command-5`) opens a
retained, categorized reference for the complete top-level vocabulary. It is
selectable, keyboard-scrollable, and returns to its previous position when
reopened. The window and the console's `help` output are generated from one
command registry, which also gates the top-level spellings accepted by the
parser.

## Start with a kit

```text
kit compact
kit superior compact
kit gm basic
kit superior toms
aliases
```

Omitting the map selects `superior`. A kit names and maps its template lanes,
sets their MIDI channel and note anchors, and installs useful aliases. Existing
rhythm cells are retained; hits in the kit lanes are retuned to the selected
map. Lanes beyond the template are preserved but muted, so `kit compact` is an
audibly compact kit; `unmute all` can reopen the retained lanes.

| Template | Lanes |
| --- | --- |
| `compact` | kick, snare, closed hat, open hat |
| `basic` | kick, snare, low tom, closed hat, open hat, crash, ride |
| `toms` | low tom, mid tom, high tom, floor tom |

The `gm` map writes channel 10. The `superior` map writes channel 1 and uses
the audited v8 pitches, including closed hat 61 and toms 41/45/48/43. These are
starting points, not a promise to match every user's Superior Drummer library.

Kit aliases include concise and descriptive forms such as `@k`, `@kick`,
`@s`, `@snare`, `@h`, `@hat`, `@o`, and `@open`. Inspect or add bindings with:

```text
aliases
alias perc 6
alias hats @h
@ghost = 2
@h
```

Alias names begin with a letter and contain letters, digits, or underscore.
They are case-insensitive and point to one-based lane targets.

## Assign indexed instruments

The short form sets a track's default song instrument. This is the normal
workflow: add as many lanes as the pattern needs and give each one a single
instrument. The row form is an optional tracker-style `INS` override for a
step that deliberately changes instrument:

```text
instrument @k kick
instrument @s midi
instrument 6 sampler
instrument 6 1 kick
instrument 6 5 midi
instrument 6 13 previous
instrument 6 15 clear

len 6 ins 15
stride 6 ins 2
dir 6 ins palindrome
mute 6 ins toggle
```

| Index | Name | Instrument |
| ---: | --- | --- |
| 0 | `kick` | Membrane Kick |
| 1 | `sampler`, `sample`, `slice` | Stereo Slice Sampler |
| 2 | `midi`, `midiout` | External MIDI OUT |

Named and numeric forms are equivalent for the three new-song instances;
indices are decimal and zero-based even though lane and row addresses are
one-based. `inst` aliases `instrument`. Additional Membrane Kick, sampler,
DaisySP drum, and MIDI OUT instances are created and assigned from the right
toolbox. The console intentionally cannot guess which dynamically added song
index the user means.

An explicit cell updates remembered instrument even on a NOTE rest. Empty
retains memory without a visual assertion; Previous (`PRV`) explicitly marks
reuse. The INS field has independent length, stride, direction, mute state, and
playhead. A track's short-form instrument is the memory seed at
transport reset.

Instrument identity determines destination: kick, sampler, and DaisySP drums are
internal; MIDI OUT is external. Each MIDI OUT instance owns its CoreMIDI
endpoint and channel in Device View; a lane's old MIDI channel field is not a
second route. `out` and `route` are retained only as rejected legacy spellings.
Per-row edits publish like other cells and do not stop transport.
Changing the track default stops and cleans scheduled notes before publishing.
Every active note retains the concrete node that received its onset, so a
later INS change cannot misroute its release.

See [Per-row Instrument Column](INSTRUMENT_COLUMN.md) for GUI gestures,
same-tick FX resolution, and MIDI behavior.

## Write rhythms

Canonical and compact forms are equivalent:

```text
mask 1 x---x---x---x---
mask @h x-x-x-x- palindrome
@k x---x---
@h x1.-_0 <>
```

`x`, `X`, and `1` are hits. `-`, `.`, `_`, and `0` are rests. Writing a mask
sets the NOTE length to the mask length. Hits reuse the lane's existing note or
its kit anchor. An optional direction accepts `forward`, `reverse`, `random`,
or `palindrome`, plus `>`, `<`, `?`, and `<>`.

Euclidean and deterministic transforms operate on the active NOTE length:

```text
eu @h 7 16
@h eu 7 16 1 <>
rotate @h 2
@h reverse
fill @k 4
fill @k 4 1
```

`eu`, `e`, and `euclid` use the v8 floor-distribution rule. Rotation is a
signed whole-step right rotation; negative values rotate left. `fill` adds a
hit at each interval and optional zero-based phase offset without replacing
existing non-rest cells. `reverse` reverses all active NOTE cells, including
Retrigger and Kill cells.

The following transforms preserve the useful mask behavior audited from the
Max v8 JavaScript source, while using a session-owned deterministic random
stream so results are reproducible and rejected commands remain transactional:

```text
sieve @h 5 0 2
density @h 0.45
thin @h note 0.20
rotatehits @h 3
humanize @h 0.35
```

`sieve` replaces NOTE with a `modulus`-row mask and hits the normalized residue
positions. `density` replaces the active NOTE mask with Bernoulli
hits; `thin` independently removes existing hits. `rotatehits` rotates only
the hit mask and clears all non-hit cell types. `humanize` probabilistically
moves an eligible hit one circular neighbor left or right when that destination
is empty in the source mask. Each accepts an optional `note` field word and alias-first
forms such as `@h thin 0.2`.

## Write velocity patterns

```text
velseq @k !.+-9
@k vel !+*.-
vol @k 127,96,64,32
velseq @k .25 .5 1.0 ==
```

Compact symbols map as follows:

| Symbol | Result |
| --- | --- |
| `!` | 1.00 |
| `+` | 0.85 |
| `*`, `o` | 0.70 |
| `.` | 0.55 |
| `0`…`9` | digit divided by 10 |
| `-`, `_`, `=` | Previous |

Digits use the compact scale only inside a multi-character symbolic string.
A single bare `9`, like any integer list value, means MIDI velocity 9.

Lists may contain MIDI integers `0..127`, normalized decimal values `0..1`,
or the words `full`, `accent`, `mid`, `default`, and `previous`.
Use `randomize`, `random`, or `rand` to materialize repeatable random values
over a lane's active VOL length (the command aliases remain `vel` and
`velocity`):

```text
randomize @k vel
random @h velocity 48 112
rand 3 32 96
```

The range is inclusive MIDI velocity and defaults to `1..127`. Results come
from the session-owned deterministic command stream, so the same command from
the same session state writes the same pattern. A live `?` velocity cell is
still explicitly rejected: randomness is written into visible cells instead
of being hidden in playback state. `mute` and `--` are also rejected. Use a
NOTE/mask rest when a step must not trigger.

The canonical `vel <lane> <row> <0..127>` edits one cell. Alias-first
`@name vel <sequence>` intentionally means `velseq`.

## Independent column structure

```text
len @k 15
len @k vel 11
len @k ins 5
len @k fx1 8
stride @h note 2
@h stride vel 3
phase @k note -1
@h phase vel 2
dir @o palindrome
@o dir vel random
@o >
```

`length` aliases `len`; `speed` and `spd` alias `stride`; `phase` aliases `ph`;
and `mode` aliases `dir`. Phase is an absolute signed row rotation normalized
against that field's length. If the field is omitted, the operation targets
NOTE. The same commands accept `fx1`, `v1`, `fx2`, and `v2`; all fields keep
independent length, stride, phase, direction, mute, and playhead state. In the
grid, double-click any column header and enter `1..256` to set that column's
length without using the console.

## Typed FX/action-value pairs

```text
actions
fx @k 1 1 membrane.tune 0.28
fx @k f1 5 previous
fx @k 2 1 membrane.decay 0.22 global
fx @k 2 9 click 0.80 global
fx @k 1 13 RR 0.50
fx @k 2 15 MT 0.25
fx @k 1 16 ST 1.0
fx @k 2 9 clear
fxvalue @k 1 5 0.40
fxv @k 1 5 previous

len @k fx1 15
stride @k v1 2
dir @k fx2 palindrome
mute @k v2 on
```

Each track owns `FX1/V1` and `FX2/V2`. Action and value columns advance and
remember state independently. Empty emits nothing but retains action memory;
Previous executes the remembered action. A normalized value is resolved before
its action on the same tick, including NOTE rests. If both pairs address the
same resolved target, FX2 wins. Membrane actions are instrument-relative: the
current INS memory moves an existing FX pattern among rack nodes without
rewriting FX cells. Stable action keys, optional `membrane.` prefixes, and
three-letter mnemonics are accepted; raw numeric CLAP IDs are not a command
contract. The scope grammar recognizes `global`, `channel`, and `note`, but an
action must advertise the requested scope. Each membrane slot has one global
parameter state and the current catalog advertises Global only, so its Channel
and Note requests fail transactionally. Sequencer actions do not take a scope:
`RR`, `MT`, `DL`, `FL`, `ST`, `AC`, and `GL` reshape the destination-neutral
onsets sent to internal audio and MIDI alike.

Window > Membrane Kick (`Command-4`) edits one kick instance's base
patch. Those controls establish manual state; FX columns remain the sample-timed
automation layer and win when scheduled at the same frame as a base update.
Dedicated snare, tom, hat, and cymbal DSPs remain separate future instruments.

See [Typed FX Columns](FX_COLUMNS.md) for exact memory, mute, ordering, GUI,
and note-identity rules.

## Performance and direct editing

```text
play | stop | panic
bpm 126
swing 56
gate 70
loop rows 5 12
loop on

mute @o
unmute @o
solo @k @s
unmute all
@k name Deep Kick

select @h 7
hit @k 1 36
rest @k 2
repeat @h 6
kill @o 8
note @s 5 38
vel @s 5 112
```

`swing` accepts `0.50..0.75` or `50..75`. Gate is milliseconds. Loop rows are
inclusive and one-based; `loop off` and `loop toggle` are also available. `mute`
defaults to toggle and also accepts `on`, `off`, or `toggle`.

## Functional timing warps

Timing transforms are applied serially after traditional pair swing. The
current stack is global and repeats over the selected tracker-tick cycle:

```text
warps
warp clear
warp cycle 16
warp exp 2
warp exp 0.65 mix 0.7 segment 0.25 0.75 repeat 2
warp step 8 mix 0.5
warp eu 7 16 segment 0 1
```

`mix` and `alpha` are synonyms for a blend from 0 to 1. A segment uses
normalized cycle phase with `0 <= begin < end <= 1`; `repeat` subdivides that
segment. Commands are transactional, and invalid transforms leave the session
unchanged. All live warp cycles are currently limited to 16 ticks by the
32-track event-density budget, including extreme exponential stacks. The
canonical slice capacity is 2048 events, and the live-audio build preflights
the warped density for both its complete lookahead and one callback block
before Play.
Open `WARPS`, `Window > Timing Warps`, or press `Command-8` for the visual
editor. Its curve overlays the identity timing line, marks tracker-tick
positions, and updates live as transforms are added, selected, removed, or
edited. Cycle, transform type, primary values, mix, segment, and repeat all
edit the same stack used by the console and scheduler. See
[Timing Warps](TIMING_WARP.md) for the model.

## Track count and destination

Tracks are dynamic up to the bounded 32-track realtime publication limit:

```text
track add CHIP BASS
instrument 9 psg
track add SUPERIOR KICK
instrument 10 midi
track remove 9
```

The main grid derives `INT` or `MIDI` from the resolved rack entry. One track
never silently fans to both destinations. Use two tracks when the same rhythm
should excite an internal voice and an external instrument; their polymetric
columns remain independently editable. A track-default change retains
transport phase; the old active owner is released on the next lane tick and
subsequent onsets use the new instrument.

## Intentional boundary

This native set implements parameter actions plus the bounded `RR`, `MT`, `DL`,
`FL`, `ST`, `AC`, and `GL` timing scheduler. Use `actions` for keys and author
them with `fx @lane <pair> <row> <action> <0..1>`. Independent per-column phase
and authoritative PR/SK/OF/RP/EU playback are implemented. Source transforms
run before gates and the accepted primary note then enters timing expansion.
The standalone Song window provides quantized row queueing; a future console
surface will expose the same launch operation. Pattern snapshots, live-random
velocity cells, and Max project import are not implemented. The explicit
`randomize` command writes concrete velocity cells.
