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

## Tudor-inspired behavior

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
       + sum_j matrix[i,j] * phase_ij(return_j[n - delay_ij])
```

The lane then runs:

```text
MATRIX SUM
  -> BODY / FORMANT
  -> 4-BAND EQ
  -> INSERT 1
  -> INSERT 2
  -> INSERT 3
  -> SEND
  -> DC BLOCK
  -> RETURN DELAY
  -> 8x8 MATRIX

SEND -> LANE LEVEL -> MASTER OUT -> LIMITER -> OUTPUT 1..8
```

Every return path has at least one sample of memory, so the graph has no
algebraic loop. Fractional micro-delays and first-order all-pass sections make
`PHASE` a compositional control rather than a hidden implementation detail.

The 8x8 matrix is signed. A crosspoint stores gain, polarity, slew, and a
small phase/delay offset. Diagonal cells are local regeneration; off-diagonal
cells make one lane excite, inhibit, or frequency-pull another.

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
| `MUFF` | Two cascaded symmetric clipping cells, interstage recovery, and a passive-style tilt/notch tone stage | `SUSTAIN`, `TONE`, `BIAS`, `LEVEL` |
| `RAT` | Frequency-dependent high-gain stage, hard clip, then a variable post low-pass | `DIST`, `FILTER`, `BIAS`, `LEVEL` |
| `ZONE A` | Dual high-gain stages with a focused upper-mid contour | `DIST`, `MID F`, `MID G`, `LEVEL` |
| `ZONE B` | A lower, wider variant with asymmetric clipping and more loop memory | `DIST`, `MID F`, `MID G`, `LEVEL` |
| `FUZZ I` | Asymmetric soft/hard hybrid with voltage-sag memory | `GAIN`, `SAG`, `BIAS`, `LEVEL` |
| `FUZZ II` | Gated comparator-like fuzz with hysteresis | `GAIN`, `GATE`, `BIAS`, `LEVEL` |
| `DIODE` | Selectable symmetric/asymmetric diode curve | `GAIN`, `SHAPE`, `BIAS`, `LEVEL` |
| `RING` | Bipolar multiplication by another lane or slow network cell | `SOURCE`, `DEPTH`, `BIAS`, `LEVEL` |

`MUFF`, `RAT`, and `ZONE` describe circuit-inspired transfer families, not
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
- `HIER`: makes slower recurrent groups gate faster groups.
- `DRIFT`: component-tolerance movement and Brownian weight variation.
- `FORMANT`: global depth trim for the per-lane product paths.
- `SEED`: injects a deterministic excitation packet.

Selected-lane controls expose `BODY`, `LOSS`, `LEVEL`, `MUTE`, and `KILL`.
The lane EQ remains `LOW`, `MID F`, `MID G`, and `HIGH`. Matrix, lane, EQ,
and insert state are stored per lane; global macros never collapse the eight
lane states into one shared processor.

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
- Seeds, matrix state, resonator variations, insert types, all lane controls,
  and containment settings are serialized.
- Processing is allocation-free after `prepare()`.

## Acceptance targets

- A seed can produce eight measurably distinct output lanes without input.
- With off-diagonal gains at zero, every lane remains an independent feedback
  instrument.
- Signed coupling can suppress as well as excite another lane.
- Insert changes do not click or erase the surrounding network state.
- `PANIC` silences and clears the graph deterministically.
- No finite parameter combination can produce NaN/Inf output.
- The DSP core resets to silence. Plugin activation injects a deterministic
  seed packet, and `NEW` injects a fresh deterministic packet; the resulting
  state settles into bounded audio without immediate limiter lockup.

## Historical reference

The conceptual cues are Tudor's use of gain stages, phase shifting, feedback,
and formant processes in *Untitled*, and the excitation of resonant objects in
*Rainforest IV*. This design remains a new software instrument rather than a
reconstruction of either work.
