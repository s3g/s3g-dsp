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
  -> BANK level + ART level + post effects
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
- Version: `5.10.0`
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
silent, even when Open Level holds the bank partly open or Envelope Freeze
contains a captured shape. BANK never exposes a raw-carrier bypass. Those
controls retain their normal sound while microphone articulation is present.
The gate is before the tape echo, so echoes already in motion decay naturally
after the live bank closes.

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

The generated speech voice and the carrier have deliberately separate pitch
roles. Formant Matrix renders the internal speech source at a fixed neutral
excitation frequency so its changing vocal-tract spectrum can be analyzed
reliably; MIDI or Voice Pitch is carried separately into the oscillator bank
and exclusively determines the synthesized result's pitch. The former source
Vibrato, Drift, Glide, Scoop, and Decline controls remain load-compatible but
are hidden and inert in this product.

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

Classic Mic sets BANK to 100% and ART to 0%, producing only the
envelope-controlled carrier bank. Raise ART when a deliberate amount of the
complete selected modulator is wanted alongside it. The External Mic presence
gate remains active for this profile and for custom External Mic patches.

If the host has no input connected, select **Internal Speech**, enter text on
the PHRASE page, and play MIDI in the same way. Blend is useful for reinforcing
a live mic with the generated articulation rather than for automatic fallback.

### Mouth Circuit quick start

**Mouth Circuit** is the focused mouth-model profile. It keeps the carrier
bright and monophonic, removes the microphone waveform from the output, and
uses a 16-coefficient all-pole estimate of the vocal tract to separate mouth
shape from the sung fundamental.

1. Select **Mouth Circuit** and hold one MIDI note.
2. Speak slowly while exaggerating `ee`, `ah`, `oh`, and `oo`; keep the mouth
   close to the microphone and use Mic Gain to place room noise below the
   anti-drone gate.
3. Leave Routing A selected for the direct mouth shape. Move Matrix A / B
   toward B to lift each analyzed resonance by two synthesis channels.
4. Use **Mouth Focus** around 85–95% for a strongly pitch-independent tract.
   Reduce it when a noisy microphone or very sharp consonants need more of the
   immediate fixed-bank response.
5. Add octave, fuzz, or echo only after the vowels and consonants read clearly.

Unlike Voice Pitch, Mouth Model does not control oscillator pitch. It estimates
the resonant envelope of the mouth while MIDI remains the excitation, which is
the defining signal relationship for this style of playing.

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
formants independent control. **Analysis Response** offers three analyzers:

- **4 Pole** uses broad, forgiving channels for noisy or abstract sources.
- **8 Pole** is the Classic Mic default and provides stronger adjacent-formant
  separation.
- **Mouth Model** estimates a 16th-order all-pole vocal-tract envelope from a
  causal 24 ms microphone window every 5 ms. This reduces imprint from the
  singer's fundamental while retaining the slowly moving mouth resonances.

The Mouth Model crossfades to its estimate only after the first valid window.
Vowels and sonorants use the pitch-resistant tract shape; fricatives and stops
remain on the faster measured-band path so consonants do not smear. It adds no
declared plug-in latency, dynamic allocation, dry microphone path, or recorded
material. Matched gain compensation keeps 4 Pole and 8 Pole about selectivity
rather than loudness. **Analysis Width** is calibrated independently of that
pole choice: low values use narrow, selective bands; high values broaden their
overlap so speech remains continuous between fixed centers. Changing 4 Pole to
8 Pole therefore changes skirt rejection without a hidden center-frequency
level jump. Narrow settings suit clean close microphones; broader settings are
often clearer for sung vowels and pitch movement.

**Definition** adds an energy-domain observer, local spectral
contrast, transient-sensitive consonant tracking, band-local tonal/noise
excitation, and carrier-band compensation. At zero it preserves the legacy
envelope and voicing response; increasing it makes the synthesis VCAs follow
the shape and timing of the word rather than merely the microphone's overall
amplitude.
It never passes the dry microphone waveform.

Before those analyzers, the analysis conditioner remains separate from the
output effects:

- **Analysis Low EQ**, **Mid EQ**, and **Air EQ** form a
  reconstruction-preserving three-band input contour.
- **Analysis Compression** reduces phrase-level variation before the band
  detectors, keeping quiet syllables comparable to loud vowels.
- **Voice Focus** removes rumble and contours the speech-presence region.
- **Analysis Leveler** continuously normalizes phrases into a bounded detector
  range, with slow recovery so breaths do not surge.
- **Analysis Noise Reject** learns a slow floor independently in each band and
  applies click-safe downward expansion. **Analysis Spectral Balance** lifts
  weak but valid formant bands relative to a dominant band without flattening
  the envelope into a fixed spectrum.

The anti-drone input gate is independent of these analysis controls, and none
of them changes the optional full-band ART waveform.

**HF Detail** restores fricatives above the useful resolution of the fixed
bank through a two-pole high-frequency residual. It remains carrier- and
anti-drone-gated, so it is not dry microphone monitoring:

- **Synthetic** uses only the carrier bank's voiced/unvoiced reconstruction.
- **Switched** admits the residual only for strong high-frequency, noise-like
  articulation. This is the normal starting point for clearer `s`, `sh`, `f`,
  and stop releases.
- **Direct** keeps the residual ready whenever a carrier is active, for the
  most literal consonant timing and greatest microphone-tone imprint.

**HF Detail Level** sets its amount and **HF Detail Cutoff** its lower edge.
Start near 4.2 kHz; raise it if vowels become airy, or lower it if a dark
microphone loses consonants.

**Transfer Mode** selects the envelope-transfer philosophy:

- **Precision** uses the energy observer directly, shortens matrix-control
  slewing, speeds carrier-band compensation, and remains linear when Bank
  Drive is zero. It is the default for Classic Mic and the matrix-first mic
  profiles.
- **Expressive** retains the expanded spectral contrast and saturating bank
  response used by older creative profiles and migrated projects.

Definition is deliberately one macro across the instrument. With External Mic
it controls measured analysis precision and band-local carrier reconstruction.
With Internal Speech it also tightens generated phoneme articulation. After
the octave/fuzz stages it preserves a bounded amount of the clean articulated
bank, never the microphone input itself.

**Mouth Focus** is active only for Mouth Model. At 0% the output follows the
fast measured bank, retaining more pitch imprint and immediate consonant
movement. At 100% voiced/periodic regions use the LPC vocal-tract estimate as
strongly as its confidence permits. Fricative/noise evidence automatically
reduces the LPC contribution even at 100%, keeping S, F, SH, T, and stop edges
on the faster articulation path. The control is smoothed for live performance.

### Wide 16

Wide 16 uses sixteen logarithmically spaced band-pass channels from 90 Hz to
the lower of 9 kHz or 43 percent of the current sample rate. The remaining six
matrix channels are inactive. The ROUTE editor therefore becomes a full-size
16 by 16 matrix with sixteen meters, labels, trims, and hit regions. Its hidden
Speech-22 cells remain stored, and return unchanged when Speech 22 is selected
again. Identity, Random, Clear, and scene Copy likewise affect only the visible
16 by 16 area while Wide 16 is active. It is useful for non-vocal carriers,
large spectral shifts, sparse resonances, and deliberately abstract
articulation.

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
gesture. These 22 envelope signals are internal control paths, not CLAP audio,
CV, or modulation outputs: the matrix remaps them directly onto synthesis-band
VCAs. Identity sends follower 1 to synthesis band 1, follower 2 to band 2, and
so on; a custom matrix can fan, collect, invert, or morph those relationships.
It therefore changes spectral transfer inside the bank, whereas an exposed
envelope-follower output would let a band control parameters or devices outside
that fixed analysis-to-synthesis path.

Eight-pole analysis is deliberately selectable rather than universal: narrow
bands improve vowel identity and consonant localization, while four-pole bands
can sound more forgiving on noisy sources and abstract material. Mouth Model
is the pitch-resistant option for talk-box-style articulation. None of the
three responses passes the dry microphone waveform.

Classic Mic uses a 2 ms nominal attack, 65 ms nominal release, 4 ms Blur,
Precision transfer, moderate Voice Focus/Leveler, and Carrier Density. It also
starts Definition at 78%. These settings preserve vowel transitions
and stop/fricative edges; longer Release or Blur values, or lower Definition,
intentionally move back toward a smooth, sustained vocal-driven filter sound.

**Bank Mode** defines how that control signal opens the synthesis bank:

- **Vocoder** follows only the measured analysis envelopes and ignores Open
  Level.
- **Hybrid** blends measured and score-derived phoneme envelopes, with Open
  Level acting as the minimum VCA floor.
- **Filter Bank** is driven primarily by Open Level while retaining a
  restrained contribution from the current measured and scored articulation.

**BANK** is the level of the envelope-controlled synthesis bank. It is not a
dry/wet crossfade: at zero the raw procedural carrier does not bypass the
synthesis filters or VCAs. To audition a continuously opened carrier, select
Filter Bank mode and raise Open Level.

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

The measured consonant path is divided into three overlapping speech zones
centered in the low, middle, and upper fricative regions. Each zone tracks its
own energy and positive spectral flux, then excites only the corresponding
synthesis channels. **Consonant Level** sets the amount, **Consonant Color**
moves from periodic buzz to generated hiss, and **Consonant Speed** moves from
immediate stop/fricative timing toward a smoother response. This keeps `s`,
`sh`, `f`, and plosive releases from collapsing into one global noise burst.
The hiss is synthesized carrier energy; no microphone waveform is copied
unless ART is explicitly raised.

Three related controls extend the classic envelope-following behavior:

- **Open Level** establishes the Hybrid mode's minimum synthesis-VCA level and
  the Filter Bank mode's fixed opening. Vocoder mode deliberately ignores it.
  In External Mic mode this floor remains subject to microphone presence.
- **Band Coupling** shifts every analysis envelope from -3 to +3 synthesis bands
  without wrapping at the endpoints. It is the fast, musically readable
  alternative to editing hundreds of matrix cells.
- **ART** is the independent full-band level of the selected modulator. At
  BANK 0% / ART 100%, Internal Speech directly auditions the generated voice;
  External Mic directly monitors the gated input; Blend auditions their mix.
  BANK 0% / ART 0% is silent rather than exposing the carrier. For a pure
  vocoder, keep ART at zero and use the bank's generated unvoiced-noise path
  for consonant energy.

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

### Matrix-first profiles

The profiles after Classic Mic are complete custom-routing patches. Each owns
two signed 22 by 22 scenes; **Matrix A / B** exposes the most important motion
inside the sound instead of merely decorating a fixed diagonal vocoder.

- **Formant Glide** moves continuously between a downward and upward formant
  translation while retaining neighboring-band articulation.
- **Fixed Circuit** collects analysis into stepped three-band blocks for a
  stable, quantized character. Voice pitch is chromatic and Pitch Hold is
  Infinite.
- **Glass Harmony** fans each analysis band into several higher and lower
  synthesis bands, with an odd/even stereo pattern and a tuned carrier.
- **Public Address** limits routing to the speech midrange and uses signed
  adjacent-band cancellation before its compressed, driven post chain.
- **Pocket Radio** collects the upper speech bands into a small changing set
  of narrow destinations, with noise-carrier articulation and a short echo.
- **Low Persona** morphs between moderate and deep downward formant maps.
- **Bright Persona** morphs between two upward formant maps while preserving
  sibilant detail.
- **Broken Relay** uses deterministic sparse, signed cross-wiring in two
  unrelated scenes, plus synchronized carrier motion and multi-head echo.
- **Vocal Alloy** is a MIDI-played polyphonic matrix that combines diagonal,
  shifted, mirrored, and signed cross-routes across its two scenes.
- **Mouth Circuit** uses MIDI pitch, a bright saw carrier, the LPC mouth
  response, fast consonant support, and a restrained scene that morphs from
  direct articulation to a two-channel formant lift.
- **Impulse Matrix** turns short external transients into long, high-resonance
  Wide-16 noise-bank decays.
- **Gated Bank** uses external program energy as a fast broadband gate around
  an open, MIDI-pitched pulse filter bank.
- **Pulse Bank** is a self-running Wide-16 MIDI carrier with slow pulse-width
  animation and no external modulator requirement.
- **Rhythm Transfer** maps the band envelopes of drums or loops onto a bright,
  polyphonic MIDI carrier with fast envelope timing.
- **Shift Morph** crossfades two Speech-22 scenes offset below and above the
  diagonal for manual or host-automated formant movement.
- **Spectral Drone** holds open a sparse Wide-16 matrix with slow carrier
  animation, broad stereo placement, and restrained tape echo.

Formant Glide through Mouth Circuit start from External Mic and Speech-22
analysis. Formant Glide through Broken Relay use Voice Pitch so the carrier
follows a sung note; Vocal Alloy and Mouth Circuit deliberately use MIDI Pitch.
Impulse Matrix, Gated Bank, Rhythm Transfer, and Shift Morph need external
audio plus a held MIDI note. Pulse Bank and Spectral Drone need only MIDI.
They remain starting points: editing any control or crosspoint moves the
profile display to Custom without discarding the routing.

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
Harmonics, Color, Noise, and **Pulse Width** determine its base spectrum.
**Carrier Density** adds a restrained subharmonic excitation and a low-level
broadband generated carrier floor. This supplies energy between widely spaced
harmonics at high notes, allowing narrow speech bands to articulate rather
than vanish, while the main oscillator preserves the played pitch. Its
oscillator slots use stable note identities,
so note stealing and polyphonic compaction do not reset surviving phases or
introduce hard discontinuities.

In Voice Pitch mode, a bounded monophonic periodicity tracker estimates roughly
48–2000 Hz from the selected modulator. Its tracking-only high-pass and
anti-alias low-pass do not filter the audible microphone or the 22-band
vocoder analysis. **Scale Root** and **Pitch Scale** can leave the result
Continuous or quantize it through the shared s3g-dsp catalog of 101 musical
scales. The four-column menu begins with the core chromatic, major/minor,
pentatonic, whole-tone, blues, modal, and bebop families, then continues through
regional, modern, and limited-transposition collections. The original eight
Formant Matrix scale values remain stable for existing projects and automation.
Pitch Hold ranges
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
Text phrases are played one word per MIDI note. The first note speaks the first
word, the next note speaks the next word, and the cursor wraps to the beginning
after the last word. Any automatic boundary, punctuation pause, or `|` rest
following a word belongs to that note, so a long-held note reaches silence
instead of continuing into the next word. Recompiling the text or reloading the
plug-in resets the cursor to word one. Phrase Mode still selects one-shot or
loop-while-held playback for the non-text phoneme patterns. Phrase Sync can be
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

ART joins BANK before final dynamics, so the direct modulator and articulated
carrier share the same post processing. The final output guard and click-safe
parameter smoothing remain active in mic, speech, and blended operation.

## Editor organization

The native editor is 1356 by 968 logical pixels and follows the shared s3g
Processor grammar: a common title band, an output control reachable from the
primary page, a large process-specific view, compact grayscale toolboxes, and
color reserved for live signal meaning.

Each adjustable parameter has one page owner; the live meters may mirror signal
state on ROUTE, but controls are never duplicated between pages:

- **ROUTE** — 22 by 22 matrix, band meters and trims, selected crosspoint,
  matrix topology and layout, Bank Mode, BANK and ART levels, Definition,
  Mouth Focus, performance envelopes, and output. MIC and INTERNAL SPEECH rails
  are visual signal-flow monitors here; their source controls live on SOURCES.
- **BANK** — Transfer Mode, analysis response and calibrated Width; input
  Low/Mid/Air EQ, Compression, Voice Focus, Leveler, per-band Noise Reject and
  Spectral Balance; phoneme blend, resonance, drive, spectral tuning,
  three-zone consonant level/color/speed, HF Detail mode/level/cutoff, stereo
  pattern, freeze/blur, gesture follow, and voiced/unvoiced transition timing.
- **SOURCES** — modulator selection and mic gain, pitch source, root, scale and
  hold, procedural carrier waveform/color/density, and carrier LFO.
- **PHRASE** — text entry, compiled phoneme score, timing, audition, voice, and
  polyphony.
- **FX** — the left column contains post-bank dynamics, stereo width, octave,
  and fuzz shaping; the right column contains the complete multi-head tape
  path. Final Output remains on ROUTE.
  Compact labels remain inside the label lane instead of overlapping slider
  tracks or value readouts.

The phrase text field is page-owned and hidden whenever another page or an
overlapping menu is active. Matrix cells, meters, trims, and their hit regions
share the same geometry so visual and mouse locations remain aligned.

## State and compatibility

Version 5.10 uses state format 27. The IDs formerly assigned to the external
carrier mix and gain now represent Modulator Source and Mic Gain. A format-18
state is therefore migrated deliberately rather than reinterpreted: its source
becomes Internal Speech and its mic gain becomes 0 dB, while the former Custom
profile moves from slot 14 to the current Custom slot. Format-20 state used
slot 15 for Custom; format 21 moved that value to slot 24 so nine matrix-first
factory profiles could occupy slots 15 through 23. Format 22 inserts Mouth
Circuit at slot 24 and migrates the former format-21 Custom value to slot 25.
Format 23 appends Mouth Focus at stable automation ID 1103. Formats 22 and
earlier migrate it to 100%, reproducing the implicit fully focused behavior of
the original Mouth Model; new instances use a gentler 80% default.
Format 24 appends Transfer Mode, Voice Focus, Analysis Leveler, Consonant
Color, Consonant Speed, and Carrier Density at IDs 1104 through 1109. Format-23
and older states migrate those controls to neutral Expressive values so their
sound is not silently reinterpreted; the revised Classic Mic, Mouth Circuit,
and matrix-first factory profiles opt into Precision explicitly.
Format 25 appends Analysis Width, HF Detail Mode/Level/Cutoff, Analysis
Low/Mid/Air EQ, Analysis Compression, Analysis Noise Reject, and Analysis
Spectral Balance at IDs 1110 through 1119. Format-24 and older states map their
fixed analyzer Q onto the equivalent Width for the saved pole response and
initialize every new conditioning/residual path neutrally. This preserves the
previous sound while new instances and revised microphone profiles opt into
the clearer front end. Format 26 appends Impulse Matrix, Gated Bank, Pulse
Bank, Rhythm Transfer, Shift Morph, and Spectral Drone at profile slots 25
through 30, moving Custom to slot 31. A format-25 Custom selection migrates to
the new Custom slot without changing its stored matrix or controls.
Format 27 changes BANK from a raw-carrier/bank crossfade to a synthesis-bank
level and changes ART from a consonant-weighted high-pass feed to a full-band
selected-modulator level. It also separates the internal analysis voice's
fixed neutral excitation from MIDI/Voice Pitch carrier frequency. The former
source pitch-performance IDs remain stored for older projects but are hidden
and do not affect audio. Existing phrase, ensemble, echo, matrix, and
post-effect settings are preserved where they retain the same meaning.

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
