# s3g Processor Formant Matrix

s3g Processor Formant Matrix is a stereo channel-vocoder and resonant
filter-bank audio effect with an integrated, sample-free speech generator. It
uses the classic vocoder topology: speech or another dynamic signal supplies
the modulator envelopes, while a MIDI-played oscillator supplies the harmonic
carrier.

```text
external mic/audio ---------+
                            +-> modulator selector -> analysis bank
typed text -> internal speech+

MIDI notes --------+
                  +-> pitch source -> procedural oscillators -> carrier shaping
voice pitch tracker+
  -> synthesis filters and VCAs

analysis envelopes
  -> voiced/unvoiced control
  -> coupling or 22 x 22 routing matrix
  -> synthesis VCAs
  -> articulation thru + post effects
  -> stereo output
```

The input signal describes the articulation; the internal oscillator supplies
the pitch and timbre that receive it. **Modulator Source** selects External Mic,
Internal Speech, or a blend of the two. The internal speech option makes the
effect self-contained when no microphone is available, but it remains an
explicit source choice rather than an automatic detector. No recorded voice,
waveform, grain, impulse response, or learned reconstruction data is loaded or
replayed.

## Plug-in identity

- Host name: `s3g Processor Formant Matrix`
- CLAP ID: `org.s3g.s3g-dsp.formant-matrix`
- Version: `5.2.5`
- Installed bundle: `s3g_processor_formant_matrix.clap`
- Main input: stereo `Modulator In`
- Main output: stereo `Formant Matrix Out`
- Note input: CLAP notes and MIDI
- Host categories: audio effect, filter, stereo

The input and output support both 32-bit and 64-bit host audio and may be
paired for in-place processing. MIDI note pitch, velocity, polyphony, phrase
timing, and host transport remain available while either modulator is selected;
Voice Pitch mode can instead derive a monophonic carrier directly from the
selected modulator without MIDI input.
The plug-in is advertised as an audio effect/filter rather than a CLAPi
instrument, but its note input remains available for playing the internal
carrier from MIDI on the same track or by routing from another track.

## Modulator and carrier architecture

The analysis bank has two modulator sources:

- **External Mic** uses the main stereo input. A microphone is the conventional
  choice, but a drum loop, field recording, feedback network, or complete mix
  can provide equally useful articulation. **Mic Gain** conditions this input
  by -24 to +24 dB before analysis.
- **Internal Speech** compiles text into phonemes, renders the gestures
  procedurally, and measures their energy through the same analysis bank.
- **Blend** combines the external and internal modulators before analysis, so
  their shared spectrum produces one coherent envelope vector.

The selected modulator is independent of the carrier waveform. In External Mic
mode an absent or silent input intentionally produces no analysis energy; it
does not switch to the generated phrase during pauses. Choose Internal Speech
when a self-contained modulator is wanted.

External Mic also places the complete bank output behind a click-safe microphone
presence gate. A held MIDI note therefore remains silent when the microphone is
silent, even when Bank Mix exposes some raw carrier, Open Level holds the bank
partly open, or Envelope Freeze contains a captured shape. Those controls retain
their normal sound while microphone articulation is present. The gate is before
the tape echo, so echoes already in motion decay naturally after the live bank
closes.

This anti-drone close also bounds unusually long Bank Release, Blur, and
Freeze tails in External Mic mode. They still shape the spoken gesture, but
they cannot hold a carrier indefinitely after the microphone becomes quiet.

The carrier is always the internal procedural oscillator bank. **Carrier Pitch
Source** chooses MIDI or Voice Pitch. MIDI notes determine pitch, chord,
velocity, and lifetime in the polyphonic mode. Voice Pitch estimates the
fundamental of the selected modulator, drives one oscillator voice, and keeps
that carrier alive for as long as the sung note remains above the modulator
gate. It does not use a fixed post-onset note duration. The carrier then
passes through the synthesis filters and VCAs under control of the selected
analysis envelopes. This separation is what produces the classic talk-box-like
vocoder relationship: the analyzer supplies articulation while either the
keyboard or the tracked voice supplies carrier pitch.

### Classic mic quick start

New instances open in **Classic Mic**, the silence-safe external-modulator
topology. Choose an Internal Speech profile when a self-contained generated
phrase is wanted instead.

1. Select the **Classic Mic** profile.
2. Route a microphone or vocal track to **Modulator In** and choose **External
   Mic** as Modulator Source.
3. Adjust Mic Gain until the MIC rail and orange analysis bands respond to
   speech, while room/interface noise still lets the bank close.
4. Hold a MIDI note or chord. The mic opens the bank; MIDI supplies the audible
   carrier.

For a keyboard-free setup, set **Pitch Source** to **Voice Pitch**, choose a
Scale and Root, and sing. No MIDI note is required. Use **Continuous** for
natural pitch tracking or a named scale for musical correction. **Pitch Hold**
bridges brief unvoiced consonants and detector dropouts. Move it fully right to
**Infinite** to latch the last confidently detected pitch until Voice Pitch is
disabled or the plug-in is reset. Microphone silence still closes the
anti-drone gate, so the latch remembers pitch without producing a drone.

Classic Mic sets Bank Mix to 100%, removing the raw MIDI-carrier side of the
bank crossfade, and Articulation Thru to 0%, removing the direct high-passed
microphone rail. Raise Articulation Thru only when a deliberate consonant feed
is wanted alongside the carrier. The External Mic presence gate remains active
for this profile and for custom External Mic patches.

If the host has no input connected, select **Internal Speech**, enter text on
the PHRASE page, and play MIDI in the same way. Blend is useful for reinforcing
a live mic with the generated articulation rather than for automatic fallback.

## Filter-bank layouts

Two layouts cover complementary uses.

### Speech 22

Speech 22 is the default intelligibility-oriented layout. It comprises low-pass
and high-pass endpoint channels surrounding twenty approximately quarter-octave
band-pass channels:

```text
LP 185, 220, 262, 311, 370, 440, 523, 622, 740, 880,
1047, 1245, 1480, 1760, 2093, 2489, 2960, 3520, 4186,
4978, 5920, HP 7040 Hz
```

The dense midrange spacing gives consonant transitions and adjacent vowel
formants independent control. **Analysis Slope** selects a four-pole or
eight-pole response per channel. Four Pole is broader and smoother; Eight Pole
is the Classic Mic default and provides stronger adjacent-formant separation.
Matched gain compensation keeps the choice about selectivity rather than
loudness. Modest high-frequency pre-emphasis and contrast expansion follow the
filters, so the synthesis VCAs receive the shape of the word—not merely the
overall microphone amplitude.

### Wide 16

Wide 16 uses sixteen logarithmically spaced band-pass channels from 90 Hz to
the lower of 9 kHz or 43 percent of the current sample rate. The remaining six
matrix channels are inactive. It is useful for non-vocal carriers, large
spectral shifts, sparse resonances, and deliberately abstract articulation.
The routing page keeps the same conceptual workflow while showing which
channels are active in the selected layout.

Shift, Stretch, Tilt, Resonance, and Drive reshape either layout without
changing the stored routing patch. Filter coefficients and envelope timing are
smoothed for real-time automation.

## Analysis, voiced/unvoiced control, and articulation

Every active analysis channel has its own envelope follower. Attack and
Release set the directional response, while Analysis / Phoneme balances the
measured selected modulator against the score-derived target. The score term is
available when Internal Speech contributes to the source; it can keep a quiet
or heavily transformed phoneme structurally useful without replacing measured
dynamics. With External Mic selected, the live analysis remains the defining
gesture.

Eight-pole analysis is deliberately selectable rather than universal: narrow
bands improve vowel identity and consonant localization, while four-pole bands
can sound more forgiving on noisy sources and abstract material. Neither mode
passes the dry microphone waveform.

Classic Mic uses a 2 ms nominal attack, 65 ms nominal release, and 4 ms Blur.
These settings preserve vowel transitions and stop/fricative edges; longer
Release or Blur values intentionally move back toward a smooth, sustained
vocal-driven filter sound.

**Bank Mode** defines how that control signal opens the synthesis bank:

- **Vocoder** follows only the measured analysis envelopes and ignores Open
  Level.
- **Hybrid** blends measured and score-derived phoneme envelopes, with Open
  Level acting as the minimum VCA floor.
- **Filter Bank** is driven primarily by Open Level while retaining a
  restrained contribution from the current measured and scored articulation.

**Bank Mix** crossfades between the procedural MIDI carrier and the
envelope-controlled bank. In External Mic mode both sides of that crossfade are
inside the microphone presence gate; reducing Bank Mix does not turn the held
carrier into an always-on monitor.

Modulator selection is independent of Bank Mode. All three modes use the same
MIDI carrier, so changing External Mic, Internal Speech, or Blend changes the
gesture source without changing oscillator pitch or phase.

The voiced/unvoiced controller decides whether the synthesis bank is excited
primarily by the tonal carrier or its shaped noise path. **Voiced / Unvoiced
Mode** offers Tonal, Noise, Blend, and Detect modes. Detect compares low/mid
harmonic energy with high-band noise energy and uses phoneme classification as
a bias. For External Mic, that measured decision also drives the
consonant-noise exciter, so S, SH, F, T, and breath releases remain audible
through the matched synthesis bands without adding dry microphone audio.
**Voicing Threshold** sets the decision boundary, **Voiced Level** and
**Unvoiced Level** set the two path levels, and **To Voiced** and **To
Unvoiced** set the directional transition times from 10 to 250 ms. Internal
hysteresis prevents repeated switching around consonant boundaries.

This transition is deliberately continuous. Fricatives and stop releases join
the same resonant gesture as the surrounding vowel instead of appearing as
separate snare-like transients.

Three related controls extend the classic envelope-following behavior:

- **Open Level** establishes the Hybrid mode's minimum synthesis-VCA level and
  the Filter Bank mode's fixed opening. Vocoder mode deliberately ignores it.
  In External Mic mode this floor remains subject to microphone presence.
- **Band Coupling** shifts every analysis envelope from -3 to +3 synthesis bands
  without wrapping at the endpoints. It is the fast, musically readable
  alternative to editing hundreds of matrix cells.
- **Articulation Thru** adds a controlled high-passed path from the selected
  modulator. It follows unvoiced energy and shares the bank's dynamics shaping
  so clarity can be restored without detaching consonants from the carrier.
  For a pure mic vocoder, keep it low and use the bank's unvoiced-noise path
  for most consonant energy.

Envelope Freeze holds a band-energy vector rather than waveform or phase data.
It can capture continuously or at note, phoneme, syllable, word, or rest
boundaries. Blur and Gesture Follow smooth the held vector and its relationship
to the phrase score. Each recapture crossfades from the previous vector. With
External Mic selected, silence closes the output and begins releasing the held
vector; Envelope Freeze cannot sustain a carrier drone through a silent mic.
The downstream tape echo is intentionally unaffected and may continue its tail.

## Routing matrix

The ROUTE page is the primary editor page. Its 22 by 22 matrix maps analysis
envelopes on one axis to synthesis VCAs on the other. Identity produces the
ordinary matched-band vocoder; off-diagonal routes translate, broaden, split,
or reorganize articulation independently of filter tuning.

The Matrix menu provides Identity, Rotate, Mirror, Chord, Sparse, and Custom
maps. **Matrix Depth** moves from identity toward the selected map, allowing any
structural transformation to be introduced gradually.

Custom contains two complete routing scenes, **Routing A** and **Routing B**.
Every crosspoint is a signed gain from -1 to +1, and **Matrix A / B** moves
continuously between the scenes. Destination rows are normalized before they
control the synthesis bank, keeping dense patches bounded. Identity, copy,
clear, and bounded random operations provide useful starting points without
changing filter, carrier, or phrase settings. The selected crosspoint has a
full-size gain control for exact editing, while the grid reports live routed
activity.

Band Coupling is applied as a non-wrapping coarse relationship. The custom
matrix is the detailed relationship layer; it can fan one analysis band into
several synthesis channels or collect several analysis bands into one
destination. Routing gains and morph movement are bounded so dense patches
cannot create an uncontrolled feedback path.

**Band Trim 1** through **Band Trim 22** scale the corresponding signed
synthesis gain from 0 to 200 percent after routing and tilt. They do not trim
the analysis followers. Each channel displays both the measured level of the
selected modulator and its mapped synthesis level, so the effect of routing,
morph, coupling, and trim is visible in place. The ROUTE source rails separately
show **MIC** and **INTERNAL SPEECH**, with the selected or blended role marked;
the orange band meters always show what actually reached analysis. Trims are
stored controls; all meter values are read-only snapshots of current activity.

## Stereo patterns

Stereo Pattern defines how synthesis channels are placed after routing:

- **Mono** keeps every channel centered.
- **Spread** distributes channels progressively across the stereo field.
- **Odd / Even** assigns adjacent bands to opposing sides.

Bank Stereo Spread scales the selected pattern. At zero, left and right use
the same carrier and placement relationship; widening does not alter envelope
timing or matrix routing.

## Internal carrier and LFO

The sample-free carrier provides glottal, saw, pulse, folded, and noise shapes.
Harmonics, Color, Noise, and **Pulse Width** determine how much usable energy
reaches the synthesis filters. Its oscillator slots use stable note identities,
so note stealing and polyphonic compaction do not reset surviving phases or
introduce hard discontinuities.

In Voice Pitch mode, a bounded monophonic periodicity tracker estimates roughly
48–2000 Hz from the selected modulator. Its tracking-only high-pass and
anti-alias low-pass do not filter the audible microphone or the 22-band
vocoder analysis. **Scale Root** and **Pitch Scale** can
leave the result Continuous or quantize it to Chromatic, Major, Natural Minor,
Harmonic Minor, Dorian, Major Pentatonic, or Minor Pentatonic. Pitch Hold ranges
from 20 to 1999 ms, with the maximum control position selecting **Infinite**.
Infinite retains the last confident pitch across consonants and arbitrarily
long silences. It never overrides the External Mic silence gate: the carrier is
muted while the mic is quiet, then returns at the remembered pitch when new
articulation opens the gate. MIDI mode remains the choice for polyphonic chords.
While a confident periodic voice is still present, the gate uses a lower
closing threshold than it uses for non-periodic room noise. This lets a quiet
held vowel sustain without weakening the normal anti-drone behavior.

A dedicated **Carrier LFO** with Triangle and Square shapes can animate carrier
pitch and pulse width. **Rate** can run freely or follow host tempo when
**Sync** is enabled, using the **Division** menu. **FM** and **PWM** depth are
applied to the procedural carrier only; they do not repitch the microphone,
the generated speech, or the analysis envelopes. This keeps keyboard and
modulator roles independent while allowing animated carrier spectra.

## Built-in speech generator

The PHRASE page is secondary to routing but remains a complete text-modulator
and performance layer.
Ordinary English text is compiled through a bounded lexicon, pronunciation
rules, contextual homograph selection, stress, syllable, and coarticulation
passes. The score strip previews the resulting phonemes and forced rests before
playback, with timing controls directly below it.

A vertical bar inserts a rhythmically stable forced rest:

```text
hello | worlds
hold || the ||| space
```

Repeated bars extend the rest by the corresponding number of timing units.
Phrase Mode selects one-shot or loop-while-held playback. Phrase Sync can be
note-relative or transport-relative, and Phrase Division supplies the host
rhythmic unit. Free timing follows the compiled durations.

The underlying generator combines mathematical glottal flow, additive
harmonics, moving formants, oral/nasal waveguides, aspiration, and contextual
constriction noise. Eight fixed voice slots and procedural doubles provide
polyphony without replaying recorded takes. It drives the analysis bank when
Modulator Source is Internal Speech or Blend. In External Mic mode the phrase
may remain compiled and ready, but it does not silently replace the input.

## Carrier shaping and post processing

The octave layers and fold/fuzz stages reshape the articulated bank output.
Their position after the bank keeps them audible in every modulator mode without
altering the microphone or generated speech before envelope analysis. The
glottal, saw, pulse, folded, and noise oscillator controls separately determine
the harmonic spectrum presented to the synthesis filters. Post-bank shaping
then feeds serial and parallel dynamics, de-essing, width, and the synchronized
multi-head tape echo. The integrated echo offers three virtual heads, seven
combinations, free or host-synchronized timing, feedback, wear, flutter, tone,
spread, and a bounded tail.

Articulation Thru joins the bank before final dynamics so consonant support
receives the same level control as the carrier. The final output guard and
click-safe parameter smoothing remain active in mic, speech, and blended
operation.

## Editor organization

The native editor is 1356 by 968 logical pixels and follows the shared s3g
Processor grammar: a common title band, an output control reachable from the
primary page, a large process-specific view, compact grayscale toolboxes, and
color reserved for live signal meaning.

The logical pages are:

- **ROUTE** — 22 by 22 matrix, band meters and trims, selected crosspoint,
  MIC and INTERNAL SPEECH source rails, selected-modulator analysis, envelope
  behavior, and output.
- **BANK** — filter layout, tuning, response, voiced/unvoiced behavior, stereo
  pattern, and fixed-bank behavior.
- **SOURCES** — modulator selection and mic gain, procedural MIDI-carrier
  waveform, and carrier LFO.
- **PHRASE** — text entry, compiled phoneme score, timing, audition, voice, and
  polyphony.
- **FX** — octave/fuzz, dynamics, de-essing, stereo width, and tape echo.

The phrase text field is page-owned and hidden whenever another page or an
overlapping menu is active. Matrix cells, meters, trims, and their hit regions
share the same geometry so visual and mouse locations remain aligned.

## State and compatibility

Version 5 uses state format 19. The IDs formerly assigned to the external
carrier mix and gain now represent Modulator Source and Mic Gain. A format-18
state is therefore migrated deliberately rather than reinterpreted: its source
becomes Internal Speech and its mic gain becomes 0 dB, while the former Custom
profile moves from slot 14 to slot 15. Existing phrase, synthesis, ensemble,
echo, matrix, and post-effect settings are preserved where they retain the same
meaning.

The public CLAP ID and bundle identity changed for version 4. A host project
that refers only to the former plug-in ID may therefore require manual
replacement even though a directly loaded older state payload can be migrated.
The internal `AcapellaSourceSynth` type and file names remain unchanged for C++
source and test stability; they are not exposed as the product identity.

All matrix, filter, envelope, carrier, and phrase storage has fixed capacity.
Activation allocates working memory; the audio path performs no file access or
dynamic allocation.

## Main implementation files

- `dsp/s3g_acapella_resonator_bank.h`
- `dsp/s3g_acapella_source_synth.h`
- `dsp/s3g_acapella_ensemble_synth.h`
- `dsp/s3g_acapella_text_compiler.h`
- `dsp/s3g_articulatory_waveguide.h`
- `dsp/s3g_acapella_vocal_fx.h`
- `plugins/clap_acapella_source_synth/s3g_acapella_source_synth_clap.cpp`
- `plugins/clap_acapella_source_synth/s3g_formant_matrix_gui.inc`

## Build

```sh
cmake --preset clap
cmake --build build-clap --target s3g_acapella_source_synth_clap
```

The development bundle is written to:

```text
build-clap/plugins/clap_acapella_source_synth/s3g_formant_matrix.clap
```

Release packaging installs it under the canonical bundle name
`s3g_processor_formant_matrix.clap`.

## Design references

The filter-bank workflow was informed by established analog vocoder practice,
including the [GRP Synthesizer V22 product description](https://www.grpsynthesizer.it/index.php/en/products/grp-synthesizer-v22-en.html)
and its [official owner's manual](https://www.grpsynthesizer.it/images/prodotti/V22/download/v22manual_en.pdf).
Formant Matrix is an original software design, not a circuit emulation or an
affiliated product.
