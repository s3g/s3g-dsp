# Pleasurehorse glitch synthesis concept

## Resume note

Source material: `/Volumes/samples/pleasurehorse`

The folder was studied as reference material for a sample-free stereo
synthesis instrument. The inventory at the time of analysis contained 559 WAV
files, approximately 740 MB and 70 minutes of audio. It includes newer hits,
`Sept2017`, `glitch`, `DISCS`, `DISCS-background`, and the process-derived
`psd-sounds` archive.

Working product description: a procedural glitch phrase synthesizer. The
leading title is **s3g Broken Syntax**. Other shortlisted titles are **s3g
Error Loom**, **s3g Mutation Voice**, **s3g Feral Archive**, and **s3g Signal
Animal**. The earlier descriptive title was **s3g Glitch Grammar**.

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

- `BODY`: pitch-swept sine/FM, sub pulses, and modal knocks.
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

## Performance modes

- `CELL`: a finite one-shot suitable for unusual percussion.
- `PHRASE`: a deterministic 0.5--12 second generated utterance.
- `FIELD`: a held or latched state that periodically replaces parts of itself
  with mutated descendants.

MIDI pitch controls tonal and formant centers. Velocity controls excitation,
brightness, and descendant admission. Tempo synchronization is optional;
free timing remains a first-class behavior.

## Principal controls

- `MATERIAL`: Body / Dust / Throat / Wire region or morph.
- `SPAN`: micro-event through long-form state.
- `DENSITY`: event count and overlap.
- `ANCESTRY`: reuse of prior material versus fresh generation.
- `MUTATION`: transformation severity.
- `REPEAT`: recurrence, ratchets, and phrase memory.
- `COHERENCE`: shared timing and pitch versus independent behavior.
- `REGISTER`, `TRACK`, `TONE`, and `DRIVE`.
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
- Broken Syntax composes finite, repeatable stereo utterances from internally
  synthesized sources and an event-genealogy score.

An initial implementation should stay compact: stereo output, four voices, a
short causal phrase-memory buffer, Cell/Phrase/Field modes, deterministic seed
recall, output containment, and approximately twelve deliberately matched
reference presets. Existing drum-character and fracture kernels may be reused
internally, but the scheduler, ancestry graph, and stereo event topology are
the product's new identity.
