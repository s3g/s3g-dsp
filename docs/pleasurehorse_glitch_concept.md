# Pleasurehorse glitch synthesis concept

## Resume note

Source material: `/Volumes/samples/pleasurehorse`

The folder was studied as reference material for a sample-free stereo
synthesis instrument. The inventory at the time of analysis contained 559 WAV
files, approximately 740 MB and 70 minutes of audio. It includes newer hits,
`Sept2017`, `glitch`, `DISCS`, `DISCS-background`, and the process-derived
`psd-sounds` archive.

Product name: **s3g Processor Errant**. It is a procedural glitch phrase
synthesizer positioned near **s3g Processor Fault**. Earlier working titles
were **s3g Broken Syntax**, **s3g Error Loom**, **s3g Mutation Voice**, **s3g
Feral Archive**, **s3g Signal Animal**, and **s3g Glitch Grammar**.

## Reference findings

The library is organized around three musical time scales:

- `CELL`: roughly 10--700 ms impacts, rattles, subs, throat noises, and pulse
  trains.
- `PHRASE`: roughly 1--10 second mutations containing cuts, repetitions,
  harmonic blocks, and abrupt transformations.
- `FIELD`: sustained or slowly mutating states, commonly 45--260 seconds.

Recurring traits include pitched pulse trains, falling sub cycles, broadband
noise slabs, sparse impulses and gaps, harmonic/formant lattices, tremolo,
ratchets, block replacement, and repetitions that return in altered form.
The material ranges from compressed continuous walls to sparse high-crest
events.

Stereo is structural rather than decorative. The process-derived families
typically have median left/right correlation around 0.2--0.35, whereas the
conventional hits are mostly narrow or nearly mono. At least one short block
is effectively a pure-side event. The synthesis design should therefore
generate stereo relationships at event level instead of applying a final
chorus or random pan.

## Core concept

Each MIDI note creates an ancestor sound and then generates descendants by
transforming its own recent output:

```text
EXCITER -> BODY / VOICE -> CAPTURED CELL -> MUTATION GRAPH
        -> STEREO EVENT SCORE
```

The ancestor combines four procedural domains:

- `BODY`: band-limited saw/pulse synthesis, octave-down sub, stochastic curve,
  and saturated resonant filtering.
- `DUST`: filtered noise, clicks, rattles, and sparse impulse clouds.
- `THROAT`: formant banks, constricted resonances, and vowel-like motion.
- `WIRE`: combs, feedback resonators, buzzes, and harmonic stacks.

A short causal memory creates descendants through repetition, ratcheting,
segment hold and rearrangement, gates and zero insertion, reversal of
internally generated fragments, sample-rate and bit reduction, polarity
changes, resonant remapping, and cross-splicing between two related synthesis
lanes.

The central idea is **ancestry**. Later events retain recognizable material
from earlier events, producing accumulated process history instead of a spray
of unrelated random glitches. A seeded event graph must render repeatably and
be completely recoverable from plugin state.

The v0.2 performance model extends ancestry across MIDI notes. Every sounding
voice contributes to a causal family archive; a new note takes a fixed snapshot
so simultaneous notes become siblings and later notes inherit prior material.
Repeated notes favor recall, small intervals transform related branches,
fifths and octaves retain strong resemblance, and wider leaps admit rupture.
Low velocity favors faithful inheritance while higher velocity admits stronger
mutation and fresh excitation. MIDI key role can address pitch, event clock, or
both, with pitch gravity pulling second-order period walks toward the played
note.

The v0.3 voice gives that performance grammar a stable mutant-bass center. A
band-limited saw/pulse pair, octave-down sub, saturated four-stage resonant
low-pass filter, and note contour supply the playable body. Fault behavior is
moved into smoothed control paths: cross-wired detune, pulse width, cutoff,
resonance, feedback, block replacement, and archive routing. Segment edges,
loop wraps, ratchets, drops, held samples, parameter changes, and final output
are crossfaded, de-stepped, dezippered, or slew-bounded so intentional glitch
remains without clicks that read as DSP errors.

The v0.4 voice leans decisively into a heavy bass instrument. A continuous
MIDI-rooted saw/pulse/triangle spine and protected, centered sub remain outside
the event-genealogy damage path. `GROWL` pressures harmonics derived from that
same oscillator body, while the family archive, resonator throat, dust, wire,
and Crosswire voltage form a bounded upper layer. Drops, reversals, replacement,
and quantization can now rupture the circuit-board character without erasing
the fundamental or turning the entire note into an apparent DSP malfunction.
The editor adopts Processor Fault's output-monitor, Bass Lab, reconstruction-
path, and patch-panel language to make the family relationship explicit.

The v0.4.1 sanitation pass removes recurring transient artifacts from the
musical signal path. Rounded pulse shaping replaces the hardest periodic edge,
the throat bank receives continuous oscillator excitation instead of narrow
pulses and high-passed dust, and damage gates, held samples, memory wraps,
Crosswire voltages, filter motion, polyphonic normalization, and stolen voices
all receive longer independent ramps. The genealogy remains audible as circuit
instability, but no longer relies on click-like discontinuities for definition.

## Performance modes

- `CELL`: a finite one-shot suitable for unusual percussion.
- `PHRASE`: a deterministic 0.5--12 second generated utterance.
- `FIELD`: a held or latched state that periodically replaces parts of itself
  with mutated descendants.

MIDI pitch controls tonal and formant centers. Velocity controls excitation,
brightness, and descendant admission. Tempo synchronization is optional;
free timing remains a first-class behavior.

## Principal controls

- `GROWL`: pressure-shaped oscillator harmonics and formant body.
- `SPAN`: micro-event through long-form state.
- `DENSITY`: event count and overlap.
- `ANCESTRY`: reuse of prior material versus fresh generation.
- `MUTATION`: transformation severity.
- `REPEAT`: recurrence, ratchets, and phrase memory.
- `COHERENCE`: shared timing and pitch versus independent behavior.
- `REGISTER`, `KEY ROLE`, `PITCH GRAVITY`, `SUB`, `CUTOFF`, `RESONANCE`,
  `CONTOUR`, `DRIVE`, and `CROSSWIRE`.
- `STEREO TOPOLOGY`, `WIDTH`, `SEED`, `MUTATE`, bounded `RANDOM`, and
  `OUTPUT`.

Stereo topology assigns events to explicit roles: `SPINE` for shared mid,
`WINGS` for related decorrelated descendants, `EXCHANGE` for alternating
channels, and `SIDE` for complementary polarity. Low-frequency body remains
centered by default. Deliberate side-only behavior should be visible and
accompanied by a mono-compatibility indication.

## Product boundary

This instrument is distinct from existing s3g tools:

- Macro Fracture transforms incoming audio.
- Processor Fault derives continuous sound from bytes, waveform ancestry, and
  codec processes.
- Processor Errant composes finite, repeatable stereo utterances from internally
  synthesized sources and an event-genealogy score.

The current source-preview implementation stays compact: stereo output, eight
voices, a short causal phrase-memory buffer, Cell/Phrase/Field modes,
deterministic seed recall, and output containment. A deliberately matched
reference preset bank remains a later product step. Existing drum-character
and fracture kernels may be reused internally, but the scheduler, ancestry
graph, bass circuit, and stereo event topology are the product's new identity.
