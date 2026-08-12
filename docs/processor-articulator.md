# s3g Processor Articulator

s3g Processor Articulator is a sample-free polyphonic vocal instrument. Typed
text is compiled into a fixed phoneme score, then rendered with a hybrid
articulatory source: mathematical glottal flow, additive harmonics, moving
formant resonators, and an oral/nasal pressure-wave tract. No recorded
waveform, grain, impulse response, voicebank, or learned reconstruction data is
loaded or replayed.

The name describes the process: a phoneme processor drives a continuously
moving articulator model. It intentionally does not imply a recorded vocal
source or a particular musical genre.

## Instrument identity

- Host name: s3g Processor Articulator
- CLAP ID: org.s3g.s3g-dsp.processor-articulator
- Bundle: s3g_processor_articulator.clap
- Version: 3.1.0
- Audio: stereo output
- MIDI: note input, velocity, bends, and host transport

This is a breaking identity change from the development sketch. Old sessions
are not migrated automatically.

## Signal path

    typed text
      -> lexicon and pronunciation rules
      -> stress, syllable, and coarticulation analysis
      -> fixed 96-step phoneme score
      -> glottal flow + aspiration + subharmonics
      -> additive/formant source
      -> moving resonator tract
      -> oral and nasal waveguides
      -> intelligibility rail + safe onset/retrigger shaping
      -> octave layers + fold drive
      -> unified phase-vocoder frame field
      -> serial/parallel dynamics + de-essing
      -> three-head tape echo
      -> stereo output guard

All working memory is allocated during activation. Audio processing performs
no file access and no dynamic allocation.

## Natural text and phoneme score

The PHONEME SCORE panel is the primary instrument view. Enter ordinary English
text and choose COMPILE. A bounded lexicon of more than 300 conversational,
lyric, function, and irregular word forms feeds a grapheme-to-phoneme rule
layer that produces consonants, vowels, stress, word starts, and relative
durations. The score grid exposes the exact result so pronunciation can be
checked before playing.

Productive endings are resolved from the base pronunciation rather than read
as unrelated letter clusters. This covers voiced and unvoiced plural or
possessive endings, the three regular past-tense endings, `-ing`, `-er`, `-ly`,
`-ness`, `-less`, and common apostrophe contractions. Words outside the
lexicon use expanded vowel, silent-letter, consonant-cluster, and suffix rules.

Before dictionary lookup, a deterministic context pass examines up to two
words on either side of an ambiguous form. It selects pronunciations for
common homographs including `live`, `read`, `wind`, `tear`, `lead`, `bass`,
`close`, `use`, `house`, `bow`, `row`, `wound`, `record`, `present`, `project`,
`refuse`, `minute`, and `content`. Hard punctuation and scored rests terminate
the context window. The score header reports `CTX` when one or more contextual
choices were made.

Intelligibility controls the strength of the clean articulation rail and the
contrast of vowel/consonant cues. Coarticulation controls how quickly the tract
moves between gestures. Phrase Rate scales the compiled event durations.

Consonants remain part of that moving tract rather than entering as a separate
noise voice. Stop releases are short, place-colored pressure events routed
through the formants and waveguide. Frication is injected at the constriction,
with contextual glottal energy returning through its tail into the following
vowel. Consonant score levels intentionally sit below vowel levels so dynamics
processing does not turn articulation into isolated drum-like hits.

The renderer assigns each consonant a role in the continuous phrase: onset
lead, onset release, vowel bridge, coda start, or coda continuation. Only an
onset release may open fully into its following vowel. Coda clusters instead
move away from the preceding vowel and keep the tract constricted, preventing
each written consonant from turning into a separate implied syllable.

The compiler and runtime score have fixed capacities. Long input is truncated
safely instead of allocating on the audio thread.

### Forced rhythmic rests

A vertical bar between words compiles to a full REST score event:

    hello | worlds

In Free timing, that rest lasts one Phrase Rate step. In Note Sync or Transport
Sync, it lasts exactly one selected Phrase Division. Repeated bars extend the
same event, so two bars last two divisions and three bars last three:

    hold || the ||| space

Ordinary spaces retain their short natural boundary gap. Forced rests are
labeled REST in the score and show their division multiplier.

## Phrase playback

Phrase Mode defines what a held note does after the final compiled event:

- One Shot plays the score once, then becomes silent while the note remains
  held. A new note trigger starts from the first event.
- Loop While Held restarts the score for as long as the note is held.

Phrase Sync offers note-relative or host-transport timing. Phrase Division
selects the synchronized rhythmic unit. Free timing uses the entered phrase
durations directly.

Setting Phrase Depth to zero bypasses score motion and leaves the continuously
held articulator available for manual vowel performance.

## Hybrid articulatory source

The source combines three complementary procedural techniques:

1. A glottal-flow derivative with aspiration, fold collision, irregular
   subharmonics, and pressure-sensitive turbulence.
2. Phase-coherent additive harmonics shaped by a continuously recalculated
   formant envelope.
3. A Kelly-Lochbaum-style tract that propagates pressure in both directions
   through oral tube sections, with lip radiation and a separate nasal branch.

Hybrid Blend balances the physical/source-filter path against the smoother
additive path. Waveguide Tract balances propagated pressure motion against the
direct resonator tract. Onset Guard and a phase-preserving retrigger dezipper
prevent hard discontinuities when notes start or steal an active voice.

Eight fixed voice slots provide polyphony. Procedural doubles use independent
oscillator state, tract variation, pitch offset, entrance timing, and random
sequence; they do not replay or delay a recorded take.

## Unified phase-vocoder field

The PVOC page places one linked-stereo 1024-point, four-times-overlapped phase
vocoder between the octave/fuzz stage and dynamics. Both mid and side material
pass through the same transport and fixed latency. Every analysis frame stores
magnitude and instantaneous-frequency advance. Each read head and channel owns
an independent synthesis-phase accumulator, so time movement remains continuous
without replaying waveform samples.

The field has five coordinated control groups:

- **Transport:** PVOC Mode selects Live, Freeze, Stretch, Scrub, Reverse, Loop,
  or Cloud. Memory defines up to ten seconds of causal generated history;
  Position, Speed, and Loop Length navigate it. Time Spread distributes as many
  as eight read heads through that history. Feedback is a contractive blend with
  bounded spectral state; output normalization is never returned to the loop.
- **Frequency:** PVOC Pitch moves fine harmonic structure, while PVOC Formant
  moves the smoothed spectral envelope independently. Frequency Warp bends the
  bin map nonlinearly. Harmonic Lock attracts instantaneous frequencies toward
  the current MIDI-note lattice.
- **Partials:** Peak / Residue moves in one direction toward prominent partials
  and in the other toward breath, frication, and low-level residue. Partial
  Cloud adds deterministic head detuning and dispersal rather than switching
  bins on and off.
- **Phase:** Identity, Peak Locked, Loose, and Diffuse modes determine how each
  bin relates to nearby spectral peaks. Coherence sets the strength of that
  relationship; Phase Drift introduces continuous deterministic phase motion.
- **Gesture:** Transient Preserve briefly reacquires live phase and adds a
  deliberately limited live spectral component at attacks and consonant
  boundaries. Capture Trigger can follow notes, phonemes, syllables, words,
  rests, or a continuous interval. Capture Release shapes the note/rest decay,
  and Gesture Follow controls capture and opening response.

Freeze is therefore an articulation-aware spectral suspension rather than a
global latch. A boundary event arms capture once; the engine waits three hops
and selects the strongest recent voiced frame instead of freezing the onset
guard or silence. The held frame continues from its own phase trajectory and
releases with the gesture. Stretch and Reverse traverse earlier generated
frames, Scrub moves bidirectionally around Position, Loop repeats a bounded
region, and Cloud combines dispersed time heads with independent partial and
phase behavior.

Transient preservation is bounded so it cannot become an accidental full-level
dry bypass. The later intelligibility rail does not add a second clean layer
over PVOC. On a fresh instance, raising Amount reveals a syllable-triggered
Freeze. Explicitly neutral Live mode takes the exact latency-aligned dry route.

The Instrument Profile menu includes eight PVOC-led starting points: **Vowel
Suspension**, **Reverse Breath**, **Formant Loom**, **Partial Rain**, **Phase
Choir**, **Consonant Shadow**, **Time Scar**, and **Chord Glass**. The last two
show how feedback, pitch/formant separation, cloud heads, and instrument
polyphony can work as one gesture.

The ring contains only analysis frames generated live by this procedural
instrument. It imports no recordings and allocates no memory during audio
processing. Gain is constrained at the per-head envelope ratio, spectral-bin,
frame-energy, feedback, and linked-output stages; diagnostics expose every
guard before the plug-in limiter. The plugin reports the fixed 1024-sample
latency and a bounded tail covering PVOC memory, capture release, feedback, and
tape echo. Version 3.1 resets IDs 65–86 when loading version 3.0 states because
the unstable first-generation PVOC state is intentionally not migrated.

## Multi-head tape echo

The integrated echo is an original procedural tape-delay stage built for this
instrument. It provides:

- three virtual playback heads and seven head combinations;
- Free, 1/4, 1/8, and 1/16 timing;
- host-tempo tracking for synchronized modes;
- feedback, tape wear, flutter, tone, head spread, and wet mix;
- smoothed delay-time movement and cross-channel feedback.

Free Time sets the base head timing in milliseconds. In synchronized modes the
selected rhythmic division sets that base, while Head Spread controls the
spacing among the three heads. Wear adds level-dependent tape coloration;
Flutter adds slow transport motion. The delay has a bounded tail and keeps
time, parameter, and bypass changes smoothed to avoid clicks.

The stage is inspired by the musical structure of classic multi-head tape
echoes, not presented as a circuit or product emulation.

## Processor editor

The editor follows the s3g Processor layout:

- a shared title band with preset load/save;
- output controls first;
- one large process-specific view switchable between PHONEME SCORE and
  PVOC FIELD;
- persistent two-column parameter panels;
- shared processor sliders, menus, toggles, typography, colors, and spacing;
- one explicit process-page switch and no OS-default control styling;
- vertical scrolling only when the host gives the editor less height.

The native size is 1356 by 968 logical pixels.

## Internal API

The implementation still uses the original AcapellaSourceSynth C++ type names
internally so the DSP headers and test corpus remain stable. Those names are
not exposed by the host descriptor or editor.

The main headers are:

- dsp/s3g_acapella_source_synth.h
- dsp/s3g_acapella_ensemble_synth.h
- dsp/s3g_acapella_text_compiler.h
- dsp/s3g_articulatory_waveguide.h
- dsp/s3g_acapella_pvoc_field.h
- dsp/s3g_acapella_vocal_fx.h

The standalone renderer accepts the public profile aliases neutral, rhythmic,
air, pressed, overdrive, and subharmonic.

## Build

    cmake --preset clap
    cmake --build build-clap --target s3g_acapella_source_synth_clap

The bundle is written to:

    build-clap/plugins/clap_acapella_source_synth/
      s3g_processor_articulator.clap
