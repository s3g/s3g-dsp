# s3g Processor No Input Mixer 8ch — DSP design and implementation notes

## Product premise

`s3g Processor No Input Mixer 8ch` is a zero-audio-input, eight-output
feedback instrument. It does not upmix a mono oscillator. Eight independent
lane states, resonant bodies, EQs, insert chains, return paths, and output
meters exist from the first sample.

The performer composes by disturbing and balancing an electronic ecology:
seed energy, phase, gain, filtering, nonlinearity, and cross-lane coupling.
The intended operating region includes silence, stable tones, beating
clusters, intermittent bursts, and bounded chaotic states.

## Instrument-agency behavior

The adaptation is grounded in working relationships rather than imitation:

- The instrument remains an audio mixer with internal feedback and optional
  effect cells, with no external sound source.
- `AGENCY` makes the performer's relationship with the circuit less
  unilateral: measured source/destination activity changes coupling inside
  the limits of the hand-patched graph.
- `SPACE` adds route-level hysteresis. Low-activity paths can recede into
  silence and must gather enough energy to reopen; it is not an output gate.
- `FORGET` changes only a bounded handful of relationships and one aux send,
  preserving the larger instrument while making yesterday's solution less
  recoverable. It attempts `2 + round(AGENCY * 8)` off-diagonal mutations:
  an existing route is either removed or polarity-reversed at 72–96% of its
  former magnitude, while an empty cell may receive a low-gain signed route.
  It also moves the movement phase but preserves lane, insert, master, and
  containment settings.
- `INTERNAL` and `HOUSE` tone are separate. The circuit may retain the bright
  spectrum it needs internally while the audition feed is made less abrasive.
- Two aux-return loops and an eight-strip mixer page retain the physical
  vocabulary of sends, returns, loop gain, EQ, faders, and mutes.

These choices make the circuit an active performance partner rather than a
fully determined signal chain. They do not reproduce any artist's equipment,
settings, repertoire, or personal performance decisions.

## Configurable-circuit behavior

The concept adapts working principles rather than recreating a historical
circuit:

- Gain, filtering, phase shift, and feedback form the sound source.
- Four slow-to-fast recurrent cells inside each lane create nested time scales.
- Signed cross-lane paths allow reinforcement, cancellation, and mutual
  modulation instead of simple parallel feedback.
- A `FORMANT` path splits a lane into low-pass and high-pass branches, then
  multiplies and blends them back into the loop.
- A resonant `BODY` bank makes each lane behave like an exciter/pickup pair
  coupled to a different virtual object.
- Component tolerance, Brownian drift, and state-dependent coupling keep the
  network alive without using free-running LFOs as the primary behavior.
- `SEED` injects a short deterministic impulse/noise packet. There is no
  continuous oscillator and no external audio input.

The existing clean-room recurrent design in
[`dsp/s3g_neural_synthesis.h`](../dsp/s3g_neural_synthesis.h) is the starting
point for phase-bearing rings, hierarchical coupling, and slow drift. The
bounded regeneration approach in
[`dsp/s3g_psd_raw_field.h`](../dsp/s3g_psd_raw_field.h) is the starting point
for DC control, loop activity measurement, and high-gain containment.

## Eight-channel signal graph

For lane `i`, the feedback excitation is:

```text
e_i[n] = seed_i[n]
       + aux_A[n] * return_A
       + polarity_i * aux_B[n] * return_B
       + sum_j matrix[i,j] * move_ij[n] * space_ij[n]
             * agency_ij[n] * phase_ij(return_j[n - 1])
```

The lane then runs:

```text
MATRIX SUM
  -> BODY / FORMANT
  -> 4-BAND EQ
  -> INSERT 1
  -> INSERT 2
  -> INSERT 3
  -> AUX A/B SENDS
  -> INTERNAL TONE
  -> DC BLOCK
  -> RETURN DELAY
  -> 8x8 MATRIX

RETURN -> LANE LEVEL -> HOUSE TONE -> MASTER OUT -> LIMITER -> OUTPUT 1..8

LANE RETURNS -> AUX SEND SUM -> AUX EFFECT -> AUX FEEDBACK -> AUX RETURNS
```

Every return path has at least one sample of memory, so the graph has no
algebraic loop. Fractional micro-delays and first-order all-pass sections make
`PHASE` a compositional control rather than a hidden implementation detail.

The 8x8 matrix is signed. A crosspoint stores gain and polarity; its phase,
movement, component drift, agency, and space factors are derived from global
controls and live state. Diagonal cells are local regeneration; off-diagonal
cells make one lane excite, inhibit, or frequency-pull another. A positive
crosspoint adds the source return without inversion; a negative crosspoint
multiplies that return by −1 before the destination sum. Negative is therefore
polarity, not attenuation: cancellation depends on the route's correlation and
phase, and delayed or nonlinear paths can instead favor a different feedback
mode. In the primary
wiring view, dragging a source port to a destination creates a route; repeating
the gesture or clicking the selected cable dissolves it, and Option-drag creates
a negative route. In the alternate grid, a first click patches or selects an
intersection and a second click on the selected intersection dissolves it.

## s3g Matrix movement

The movement layer uses the existing Group Matrix vocabulary and equations:
`FLOW`, `PULSE`, `CHASE`, `SWIRL`, `SCAT`, and `HOLD`, with `FLOW`, `SPREAD`,
`VORTEX`, `DEPTH`, `RATE`, and `PHASE` controls. `DEPTH` is the public label for
the stable CLAP Motion parameter. Generated weights multiply the stored manual
gains. They never create a connection in an empty cell, change its sign, or
raise it beyond the performer's stored ceiling.

The control-rate field is converted into a perceptual gain law. For sources
with several patched destinations, `w_peak` is the largest active weight for
that source; this keeps one route in focus while the field transfers energy
between the others. A source with only one patched destination uses its
absolute generated weight so it can still pulse or fade.

```text
w_focus = w / max(w_peak, epsilon)       // multiple active routes
w_focus = w                              // one active route
w_curve = clamp(w_focus, 0, 1) ^ 1.35
movement_db = -(1 - w_curve) * DEPTH * 30 dB
g_effective = g_base * db_to_gain(movement_db)
rate_hz = 0.05 * 100 ^ RATE
```

`RATE` therefore spans 0.05–5 Hz and its GUI readout is in Hz. The destination
normalizer measures `g_base`, excluding `movement_db`; otherwise it would
counteract attenuation and perceptually mask the route transfer. At the
default 38% depth, a 10% weight sits about 11 dB below the focused route.

This preserves a crucial distinction: patching describes the electronic
instrument; movement describes how energy travels inside that instrument.
The wiring renderer shows the signal itself rather than a proxy for movement.
Each matrix route publishes its exact post-phase, post-gain, post-motion audio
contribution into a lock-free 24 kHz scope ring. The GUI bends that measured
waveform around the signed cable shell, derives visibility from its RMS level,
and reports the selected route in dBFS. Motion, drift, agency, space, polarity,
and phase are visible only insofar as they change the routed audio. The
alternate grid keeps stored/effective movement cells, groups effective gain
indicators under their corresponding source columns, and aligns each lane's
output peak meter to that same column. Output meters map −60 to 0 dBFS instead
of displaying linear amplitude.

## Resonant body and formant cell

Each lane owns four damped resonators distributed over low, low-mid,
high-mid, and high ranges. `BODY` moves their base scale, `LOSS` changes decay,
and `MATERIAL` selects a fixed ratio/damping family. Small deterministic
per-lane variations prevent eight identical resonators from collapsing into a
single mono state.

`FORMANT` crossfades from the ordinary body output toward a product path:

```text
formant = highpass(body, f_low) * lowpass(body, f_high)
```

The product path is normalized and DC-blocked before re-entering the lane. Its
center and bandwidth drift slightly under the slow recurrent cells.

## Nonlinear insert library

Every lane has three serial slots. Each slot uses the same stable parameter
IDs and exposes contextual labels in the GUI.

| Type | DSP character | Primary controls |
| --- | --- | --- |
| `WOOL` | Two cascaded symmetric clipping cells, interstage recovery, and a passive-style tilt/notch tone stage | `SUSTAIN`, `TONE`, `BIAS`, `LEVEL` |
| `RAT` | Frequency-dependent high-gain stage, hard clip, then a variable post low-pass | `DIST`, `FILTER`, `BIAS`, `LEVEL` |
| `ZONE A` | Dual high-gain stages with a focused upper-mid contour | `DIST`, `MID F`, `MID G`, `LEVEL` |
| `ZONE B` | A lower, wider variant with asymmetric clipping and more loop memory | `DIST`, `MID F`, `MID G`, `LEVEL` |
| `FUZZ I` | Asymmetric soft/hard hybrid with voltage-sag memory | `GAIN`, `SAG`, `BIAS`, `LEVEL` |
| `FUZZ II` | Gated comparator-like fuzz with hysteresis | `GAIN`, `GATE`, `BIAS`, `LEVEL` |
| `DIODE` | Selectable symmetric/asymmetric diode curve | `GAIN`, `SHAPE`, `BIAS`, `LEVEL` |
| `RING` | Bipolar multiplication by another lane or slow network cell | `SOURCE`, `DEPTH`, `BIAS`, `LEVEL` |

`WOOL`, `RAT`, and `ZONE` describe circuit-inspired transfer families, not
component-perfect branded pedal emulations. Release names can be changed
without changing the underlying algorithms.

Nonlinear cells run at 2x by default. A quality menu may select 1x, 2x, or 4x;
oversampling applies to the active nonlinear stages, not to meters or slow
control state.

## Network controls

Global `NETWORK` controls:

- `FDBK`: overall return gain, extending above unity into the governed region.
- `COUPL`: scales off-diagonal matrix energy without changing local feedback.
- `PHASE`: blends direct and all-pass-bearing return paths.
- `DRIFT`: component-tolerance movement and Brownian weight variation.
- `FORMANT`: global depth trim for the per-lane product paths.
- `AGENCY`: state-dependent variation in coupling inside the patched graph.
- `SPACE`: route-activity hysteresis and willingness to remain quiet.
- `VARIANCE`: bounded deviation applied when a factory patch is recalled.
- `SEED`: injects a deterministic excitation packet.

Selected-lane controls expose `BODY`, `LOSS`, `LEVEL`, `AUX A`, `AUX B`,
`MUTE`, and `KILL`.
The lane EQ remains `LOW`, `MID F`, `MID G`, and `HIGH`. Matrix, lane, EQ,
and insert state are stored per lane; global macros never collapse the eight
lane states into one shared processor.

## Aux-return and tone structure

Each of two return buses sums its eight independent lane sends, adds bounded
self-feedback, and enters one clicklessly replaceable nonlinear cell. The cell
can use any type from the lane insert library. `GAIN`, `TONE`, `RETURN`, and
`LOOP` remain separate: processor drive does not stand in for return level,
and return level does not silently change the internal bus feedback.

The aux output is returned to the lane excitation stage with a fixed stable
distribution: A reinforces all destinations while B alternates polarity. This
gives the buses different circuit roles without adding another hidden matrix.
All aux paths retain one-sample memory and pass through DC control.

`INTERNAL` is a tilt-like one-pole tone path applied to governed lane returns.
`HOUSE` is a separate tilt on the audition output. Neither parameter rewrites
per-lane EQ, and `HOUSE` cannot alter the feedback ecology.

## Editor and mixer interaction contract

The editor is a full-width shell with four logical pages rather than a
permanent side control column:

- `PATCH` combines signed wiring (or the alternate exact grid), network,
  movement, and selected crosspoint controls.
- `MIXER` combines the eight complete strips, two aux-return processors, and
  separate internal/house tone controls.
- `CHANNEL` combines eight lane summaries with the selected lane, EQ, and
  insert controls.
- `SAFETY` combines output and containment controls.

`PANIC` stays fixed in the title band. `POP` detaches the current page and
`DOCK` closes that attached window. Every detached page uses the same renderer
and native 1356-by-820 coordinate system as its nested form; there is no second
set of mixer controls and no DSP state lives in a window. The `MIXER` renderer
and hit geometry are shared exactly between both forms. Body, loss, signed
local loop, three EQ gains, two aux sends, aux parameters, master tone, and
fader use continuous click-and-drag control; insert rows and mute buttons
remain discrete. Closing or hiding the host editor hides its attached pages.
When a plugin view is first responder, unmodified Left/Right moves to the
adjacent logical page; if that page is detached, its attached window comes to
the front. The shortcut is handled only while the plugin GUI owns focus.

## Containment

Containment should change only genuinely unsafe behavior:

- DC blockers bracket nonlinear feedback sections.
- Per-lane peak and RMS observers estimate loop energy.
- A slow energy governor reduces excess return gain above the safe region;
  it does not compress ordinary output transients.
- The final eight outputs use independent ceiling limiters.
- NaN/Inf and denormal guards clear only the affected state.
- `PANIC` ramps all outputs down, clears feedback/resonator/filter memories,
  and rearms from silence without a click.
- `KILL` clears one lane; `MUTE` removes it from audition while its internal
  ecology may continue running.

The containment view reports `QUIET`, `STABLE`, `EDGE`, or `RUNAWAY` from
measured loop energy. Those states drive the GUI's restrained cyan/orange/red
signal colors.

## Plugin and state contract

- CLAP audio ports: zero inputs, one fixed eight-channel output bus.
- Host name: `s3g Processor No Input Mixer 8ch`.
- GUI title: `s3g PROCESSOR NO INPUT MIXER 8CH`.
- Matrix, lane, insert, and performance controls are exposed as automatable
  CLAP parameters; discrete insert types change with a short clickless
  crossfade.
- The native editor and each synchronized detachable page are 1356 by 820
  pixels. Detaching a page does not create another audio engine or parameter
  surface.
- Seeds, matrix state, resonator variations, insert types, all lane controls,
  and containment settings are serialized.
- Ten deterministic factory patches, preset variance, localized forgetting,
  and bounded full-network randomization
  resolve into the same stored parameter surface; reseeding remains an
  independent action.
- The CLAP surface contains 320 parameters. Version-one state is migrated by
  retaining its globals, matrix, lane, EQ, and insert values, then filling new
  movement and aux fields from conservative defaults.
- Processing is allocation-free after `prepare()`.

## Acceptance targets

- A seed can produce eight measurably distinct output lanes without input.
- With off-diagonal gains at zero, every lane remains an independent feedback
  instrument.
- Signed coupling can suppress as well as excite another lane.
- Matrix movement cannot open an unpatched cell.
- Aux sends and returns have a measurable audio effect and remain bounded.
- Preset variance preserves topology; `FORGET` remains a local relationship
  mutation rather than a disguised full randomizer.
- Insert changes do not click or erase the surrounding network state.
- `PANIC` silences and clears the graph deterministically.
- No finite parameter combination can produce NaN/Inf output.
- The DSP core resets to silence. Plugin activation injects a deterministic
  seed packet, and `NEW` injects a fresh deterministic packet; the resulting
  state settles into bounded audio without immediate limiter lockup.

## Historical reference

Toshimaru Nakamura's no-input mixing-board practice is an explicit historical
reference for treating an ordinary mixer with no external audio input as an
electronic instrument, and for approaching its unpredictability, indeterminacy,
and surprise as material rather than error. His [artist biography](https://www.toshimarunakamura.com/bio)
describes that practice directly. Mudd and Brown's 2023 NIME paper,
[“Musical Pathways through the No-Input Mixer”](https://nime.org/proc/nime2023_56/),
places Nakamura's naming and practice within a wider history of mixer-feedback
work. The present design also draws on effect-pedal feedback, instrument agency,
quiet-space practice, configurable gain and phase networks, formant processes,
and the excitation of resonant objects. It remains a new software instrument,
not a reconstruction or artist emulation.
