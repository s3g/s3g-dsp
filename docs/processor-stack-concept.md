# s3g Processor Stack — feedback guitar synth design

## Product premise

**s3g Processor Stack** is a MIDI instrument in which a distorted amplifier,
loudspeaker, microphone, and short acoustic return are the shared sounding
body. Guitar-like strings may excite that body, but they are deliberately
plain and incomplete. The recognizable voice comes from the pedal, preamp,
power stage, stressed speaker, and governed feedback path.

The musical center is narrow and specific:

- one-finger power chords that become dense through shared nonlinear
  intermodulation;
- crooked, abrupt monophonic leads with slides, pinched attacks, unstable
  harmonic selection, and hard rhythmic gaps;
- playable feedback that can cough, squelch, bloom, change harmonic, or drain
  after release without turning into an unbounded no-input oscillator.

Version 0.6.1 implements the shared four-lane plucked-string exciter, eight pedal transfers,
oversampled preamp and power stages, shared supply sag, four-mode nonlinear
speaker, two microphone views, governed pitch-related return, POWER/HAND/LEAD
allocation, interval-driven CROOKED behavior, a host-tempo scale arpeggiator
with an editable eight-step rule, independently governed BODY/STAB feedback,
period-targeted glitch capture, adaptive overload masking, pressure, pitch
bend, twenty-two factory
presets, versioned CLAP state, and the
compact editor described below. Later
sections retain the original rationale and listening criteria so further
changes can be judged against the product premise.

Version 0.5.1 also separates the calm end of `PICK` from the hard attack path.
At zero, the seed is noise-free, slow-rising, and band-limited before it reaches
the pedal; raising PICK continuously restores the scrape noise, upper partials,
bandwidth, and fast jab. Parameter changes made while the processor is silent
are adopted before the next note, so a zeroed PLAY toolbox cannot leak the old
PICK value into its first excitation. Bias-bearing pedal models run against a
matched idle reference so their startup settling does not become an amp click.

Version 0.5.2 removes hard state clipping from the microphone feedback path.
STAB now uses a topology-preserving state-variable resonator whose integrator
states and combined returns approach their bounds continuously. Raising
`FEEDBACK` can still drive compression, harmonic capture, and instability, but
it cannot repeatedly strike a flat numerical rail. Maximum `PICK` is also a
bounded band-pass scrape rather than full-band noise: its envelope is slightly
rounder, its tonal packet is normalized downward, and pickup velocity is
band-limited before the shared distortion stack.

Version 0.6.0 adds a per-exciter ADSR before the shared pedal. It shapes the
player/string energy without acting as a master VCA: `RELEASE` can close the
source while `SPILL` lets the already-excited amp, speaker, and microphone loop
continue. POWER chord tones share the gesture, HAND voices retain independent
envelopes, and every generated arpeggiator step retriggers its allocated
string. Dense arpeggios now measure attack recovery and host-tempo spacing;
high `PICK` is progressively energy-normalized, and pick polarity changes wait
until the preceding packet has cleared rather than reversing a live transient.

Version 0.6.1 removes the remaining flat numerical rails from the pedal,
speaker-mode, microphone, room, and output path. The cabinet now responds to
measured coil, feedback, and microphone stress by smoothly shortening modal
decay and reducing breakup drive before overload becomes incoherent. A
linear-through-normal-level soft knee remains as an emergency bound. The two
microphone views share one linked bound, the room stores that already-governed
signal without reclipping it, and the output stage uses a finite limiter attack
followed by a continuous soft ceiling. The asymmetric power-stage halves are
also blended through zero rather than selected by an abrupt sign branch.

The angular lead behavior takes broad cues from the lurching intervals,
contrary motion, dry articulation, and unstable guitar sonorities associated
with Captain Beefheart-era music. It does not encode melodies, solos, player
timing, or a particular guitarist's tone. The performer supplies the phrase;
the circuit makes the phrase argumentative.

## Identity and product boundary

Processor Stack is not a realistic six-string guitar followed by an amp effect.
It is one shared electro-acoustic circuit whose pitch attractors are selected
by MIDI.

It is distinct from Processor LF Synth in four ways:

- LF Synth begins with a sustained twelve-mode membrane and protects its first
  mode from Shred. Processor Stack begins with sparse pick/string excitation and
  deliberately lets the shared amplifier destroy and recombine its pitches.
- LF Synth uses two independent upper-band feedback lanes. Processor Stack uses
  one central amp loop plus two microphone returns so simultaneous notes fight
  inside the same nonlinear state.
- LF Synth's crossover preserves bass weight. Processor Stack preserves only a
  modest root witness; speaker-generated difference tones, octave jumps, and
  missing fundamentals are part of the instrument.
- LF Synth's cabinet is the end of the amplifier trajectory. Processor Stack's
  nonlinear speaker and microphone signal are inside the feedback loop and are
  therefore the primary resonator.

This shared topology is the main product invariant. Chord voices must never
receive independent complete amp models and be summed afterward.

## Signal graph

```text
MIDI NOTE / CHORD MEMORY
        |
        +-> PICK PACKET --------------------+
        |                                   |
        +-> PLUCKED STRING / PICKUP (0--100%) +-> ROOT WITNESS
                                            |
MIC RETURN -> BODY / STAB / TARGET GLITCH ---+-> PEDAL CIRCUIT
                                                -> PREAMP
                                                -> TONE / PRESENCE
                                                -> PUSH-PULL POWER + SAG
                                                -> NONLINEAR SPEAKER / CABINET
                                                -> MIC A -----------------> MID
                                                -> MIC B / ROOM ----------> SIDE
                                                    |
                                                    +-> DELAY / DC BLOCK
                                                        / LOOP GOVERNOR
                                                        -> MIC RETURN

MID + SIDE -> RELEASE CONTAINMENT -> LINKED CEILING -> OUTPUT
```

The feedback tap follows speaker distortion. Moving the tap ahead of the
speaker would turn the design into a distortion pedal with a resonant delay
and lose its central behavior.

All held notes enter the same pedal and amp. A root, fifth, and octave therefore
produce level-dependent sum and difference products, sag against one another,
and compete for the loop's current harmonic. The stereo output is a two-mic
view of one cabinet, not two unrelated guitar amplifiers.

## Exciter: enough string, not a guitar simulation

Each active pitch owns a small fixed-cost exciter lane. Four lanes are enough
for the first implementation. A lane contains:

1. A 1--4 ms velocity-shaped pick packet: asymmetric pulse, short filtered
   noise, and a polarity determined by alternating virtual pick direction.
2. A weak band-limited transient pitch witness that disappears with the pick
   packet rather than sustaining the note as an oscillator voice.
3. A fractional-delay string with bridge loss, damping, slight dispersion,
   and nonlinear recirculation.
4. A position-dependent pickup comb and velocity/displacement blend before
   the shared pedal.

At `STRING = 0`, the packet is still sufficient to start the amp loop and the
instrument remains fully playable. At `STRING = 100%`, the source becomes more
recognizably plucked, but it should still sound unfinished when the stack and
feedback are bypassed. There is no body model, fret-noise sampler, sympathetic
six-string bank, or acoustic-guitar radiation stage in version one.

A quiet root witness can bypass the most destructive pedal transfer and rejoin
at the power stage. Unlike LF Synth's protected bass branch, this is not an
exact complementary low-band replacement. It is narrow, level-dependent, and
allowed to disappear at high `CHAOS`, so the cabinet rather than a clean sine
continues to define the perceived pitch.

## MIDI and performance grammar

### Voicing modes

- `POWER`: one played note excites root, fifth, and octave. `SHAPE` moves from
  root + fifth to root + fifth + octave, then toward inverted fifth + octave.
  The generated fifth uses equal-tempered pitch by default; a small
  just-intonation pull may be introduced only as a continuous color control.
  Sustained lane weights and attack weights are separate: every POWER voicing
  shares one normalized pick-energy budget, with a restrained upper-octave
  seed. The root attack remains invariant while fifth and octave seeds are
  separately band-limited and bloom into their sustained balance. SHAPE
  automation slews held gains and never generates a new pick.
- `HAND`: up to four played notes feed the shared amp. Oldest-note stealing
  damps the stolen exciter before reuse while leaving the amp and speaker
  states intact.
- `LEAD`: monophonic last-note priority with legato retuning. New attacks can
  jab the circuit; legato notes move its attractor without resetting the amp.

Changing voicing mode does not swap synthesis engines. It only changes how
MIDI notes populate the same exciter lanes.

### Scale arpeggiator

The optional arpeggiator sits before voicing allocation. One held root is
mapped through CHROMATIC, PHRYGIAN, HARMONIC MINOR, DIMINISHED, or TRITONE
interval rules, then UP, DOWN, PENDULUM, PEDAL, or SCRAMBLE decides traversal.
The generated step may therefore remain one LEAD string or expand into the
POWER root/fifth/octave load; it never creates a separate amp per note.

Rates are host-tempo eighth, eighth-triplet, sixteenth, sixteenth-triplet,
thirty-second, or sixty-fourth notes, plus quarter, half, and whole notes for
slow processional movement, with a 120 BPM fallback. OCTAVES selects one
through four registers and GATE closes only the current string excitation;
speaker and amplifier state remain shared between steps. SCRAMBLE is a fixed
integer stride, not audio-thread randomness, so saved phrases and offline
renders remain deterministic.

Each generated gate drives the same per-string ADSR used for direct playing.
At dense rates, a recovery governor reduces only the excess high-PICK packet
energy and defers alternating polarity while the prior attack remains live.
It does not change the selected scale degree, gate duration, or sustained
speaker state.

CUSTOM reads one to eight signed scale degrees from the public STEP controls.
Zero addresses the held root, positive degrees climb through the selected
scale, and negative degrees select notes below the root. This makes the phrase
explicit and automatable without turning it into a hidden random generator.

A declared cell of three zero degrees with short GATE and high DAMP produces
the instrument's repeated palm-muted chug grammar. POWER supplies the fifth
inside the same nonlinear stack, so this remains one tightly gated riff voice
rather than separately distorted chord oscillators.

### Crooked interval response

`CROOKED` does not generate notes or randomize a melody. It exaggerates the
consequence of intervals the performer actually plays:

- repeated notes briefly choke and re-strike the speaker;
- seconds and tritones make the loop hesitate between neighboring harmonic
  targets and can produce a short squawk;
- fourths, fifths, and octaves acquire the loop quickly and firmly;
- wide leaps overshoot the new delay target before falling back;
- descending legato motion retains the previous harmonic slightly longer than
  ascending motion;
- high-velocity notes shorten the hesitation and hit the power-stage sag
  harder, while low velocity admits a drier, more reluctant note.

All decisions are deterministic from previous note, new note, velocity, and
the saved seed. At `CROOKED = 0`, pitch follows ordinary smoothed MIDI without
interval-dependent behavior.

Pitch bend moves the exciter immediately but the microphone-loop attractor
more slowly, making bends pull against feedback. Channel pressure raises loop
return and speaker stress. Mod wheel can morph microphone position or pedal
bias. These mappings should be direct defaults, not hard-coded requirements.

### Gates and silence

Note-on injects energy; a held gate keeps the microphone return eligible for
regeneration. Note-off damps the direct string but lets speaker and loop
energy decay according to `SPILL`. A new note can inherit that decaying state.
When no keys are held, the loop makeup gain moves below unity and must
eventually drain to numerical silence. A later explicit `HOLD` feature may
permit indefinite performance states, but it is outside version one.

## Pedal and amplifier circuit

The circuit is a reduced-order musical model rather than a component-exact
emulation of a named product.

### Pedal

The existing analog drive transfers provide useful clean-room starting
points: SHRED, WOOL, RAT, ZONE A/B, FUZZ I/II, and DIODE. Processor Stack should
reuse their transfer behavior but give the pedal its own memory, input filter,
bias, voltage-starvation envelope, and output tone state.

`PEDAL` selects the circuit. `BITE` sets its drive and upper-mid entry level.
`BIAS` moves asymmetry and starvation together. This is intentionally a small
surface: the pedal exists to provoke the amp rather than become a separate
general-purpose pedalboard.

### Amp

The shared mono-to-stereo stack uses:

- two cascaded asymmetric preamp stages;
- a broad bass shelf, sweepable mid emphasis, and treble/presence loss;
- a push-pull power transfer with crossover roughness at restrained levels;
- an envelope-controlled supply rail shared by every active pitch;
- transformer low-frequency memory and a bounded DC blocker;
- 2x oversampling around the pedal, preamp, and power nonlinearities.

`STACK` coordinates preamp gain and power-stage arrival. `SAG` controls the
depth and recovery of the shared supply envelope. `FOCUS` moves the tone path
from blunt low-mid bark through narrow nasal mids to a hard upper-mid edge.

Power chords depend on placing the summation before these stages. The audible
compression and intermodulation should change when one chord tone is released,
even if the others continue at constant amplitude.

## Speaker as the main oscillator

The speaker/cabinet section is not just an impulse-response-like filter. It is
a small dynamic system inside the microphone loop:

- a low resonance and electrical damping state;
- three lossy cone/baffle modes spanning low mids through presence;
- displacement-dependent resonance shift;
- asymmetric cone limiting and voice-coil compression;
- a high-frequency breakup branch that appears only under sufficient power;
- cabinet leakage that supplies a quieter, less distorted root witness.

`CONE` raises displacement nonlinearity, mode coupling, and breakup together.
`CAB` moves the low resonance, modal spacing, and high-frequency loss along a
coordinated compact-to-large trajectory. The model need not reproduce a named
driver or enclosure, but its state must continue evolving inside the loop even
when the direct output mix is low.

Microphone A is the stable centered view. Microphone B combines an off-axis
speaker tap with a short room reflection. `MIC` moves their balance and phase,
creating stereo width and changing which speaker mode returns to the input.
Low stereo difference is gradually removed below roughly 120 Hz.

## Feedback mechanism

LF Synth's Bass Shred supplies the proven safety grammar:

- at least one sample of loop memory;
- fractional delay smoothing during pitch changes;
- DC blocking before reinjection;
- color-dependent loop low-pass;
- a slow loop-energy follower and gain governor;
- bounded nonlinear storage;
- activity measurement for tail processing;
- a gate that allows regeneration to drain after its source disappears.

Processor Stack changes the topology around that grammar. It uses one shared
speaker-derived history but reads it through two returns. BODY is broad,
high-passed, and limited to lower harmonics. STAB uses a shorter delay and a
narrow topology-preserving state-variable band around a quantized upper
partial. Its integrator states and the combined return use continuous
asymptotic bounds rather than hard clipping. Each lane has its own energy
follower and governor before a final whole-loop governor.

For current root frequency `f0`, selected harmonic `h`, sample rate `fs`, and
estimated circuit phase delay `d_phase`, the note-related delay target begins
with:

```text
d_note = clamp(fs / (f0 * h) - d_phase, d_min, d_max)
d_loop = lerp(d_room, d_note, TRACK)
```

`HARMONIC` moves BODY across roughly the first through sixth partial and STAB
across roughly the fifth through twenty-fourth. The speaker modes and loop
filter still bias the final winner rather than synthesizing a clean sine.

`FEEDBACK` controls requested return gain. `PROXIMITY` shortens the room term,
opens the loop bandwidth, and increases microphone coupling. `POLARITY` is a
continuous path through negative, weak, and positive return rather than a
cosmetic phase switch. Negative values should favor cancellation at one mode
and reinforcement at another.

The effective return is governed by measured loop energy:

```text
excess = max(0, loop_rms - target_energy)
governor = 1 / (1 + excess * governor_slope)
g_effective = g_requested * key_gate * governor
return = saturate(loop_filter(delay(mic_signal)) * g_effective)
```

The governor acts on the internal return, not on the final output, and should
recover slowly enough that feedback breathes instead of pumping like a master
compressor. A separate linked output ceiling catches pathological peaks.

`PIERCE` sets the intended upper-lane share and its resonant focus.
`SELF LISTEN` compares measured BODY and STAB energy. When low speaker modes
consume more than their intended share, it smoothly ducks BODY and raises
STAB inside their independent limits. It therefore responds to what the
speaker is actually returning rather than applying a static treble boost.

`TARGET GLITCH` arms on a real or arpeggiated attack but does not immediately
freeze an arbitrary buffer position. The self-listener first requires coherent
STAB energy, then locks a recent micro-cell to a small group of periods at the
current upper-partial target. `RATCHET` shortens that cell and raises a finite
repeat count. Raised-cosine cell windows, deterministic speed/reverse edits,
and a dedicated energy governor make the fractures intentional while the
complete recurrence still passes through the main loop governor and key gate.
With HAND polyphony, the captured material is the chord's shared speaker
response rather than one independent glitch line per string.

`OVERLOAD MASK` detects the failure mode in which sustained speaker level,
voice-coil stress, high loop energy, and rapid sample-to-sample roughness occur
together. It is deliberately not a final-output compressor. The detector first
spectrally masks the microphone signal, then reduces STAB and glitch return
more strongly than BODY and lowers requested loop drive. Its attack ignores
isolated pick edges and its long recovery prevents chatter. A loud coherent low
drone can therefore remain massive while a broadband feedback collapse clears
space before the linked ceiling becomes the audible processor.

## Public control surface

The first editor can remain compact with four functional groups.

### PLAY

- `MODE`: POWER / HAND / LEAD
- `SHAPE`: power-chord inversion or HAND/LEAD exciter thickness
- `STRING`: pick packet through the plucked-string waveguide and pickup view
- `PICK`: rounded, noise-free seed through scrape and hard jab
- `DAMP`: exciter and palm damping
- `GLIDE`: ordinary pitch slew
- `CROOKED`: interval-dependent hesitation, overshoot, and choke
- `SPILL`: note-release inheritance of amp and feedback energy
- `ATTACK`: time for each string/exciter to reach the shared pedal
- `DECAY`: time from the attack peak toward held source level
- `SUSTAIN`: held string feed into the stack, independent of feedback sustain
- `RELEASE`: post-gate source fade before SPILL governs the existing stack tail

### ARPEGGIATOR

- `PATTERN`: OFF / UP / DOWN / PENDULUM / PEDAL / SCRAMBLE / CUSTOM
- `SCALE RULE`: CHROMATIC / PHRYGIAN / HARM MIN / DIMINISHED / TRITONE
- `RATE`: host-tempo 1/8 through 1/64 with triplets, plus 1/4, 1/2, and 1/1
- `OCTAVES`: one through four scale registers
- `GATE`: held fraction of each generated step
- `LENGTH`: one through eight declared steps used by CUSTOM
- `STEP 1` ... `STEP 8`: signed scale degrees from -8 through +15

### PEDAL

- `CIRCUIT`: SHRED / WOOL / RAT / ZONE A / ZONE B / FUZZ I / FUZZ II / DIODE
- `BITE`: pedal input and clipping intensity
- `TONE`: pre-emphasis and pedal output bandwidth
- `BIAS`: asymmetry, memory, and voltage starvation

### STACK

- `STACK`: coordinated preamp and power arrival
- `SAG`: shared supply compression and recovery
- `FOCUS`: tone-stack mid center and presence trajectory
- `CONE`: loudspeaker displacement, compression, and breakup
- `CAB`: low resonance, modal spacing, and bandwidth
- `MIC`: centered mic through off-axis/room stereo return

### LOOP / OUTPUT

- `FEEDBACK`: requested microphone return
- `PROXIMITY`: loop delay, bandwidth, and coupling
- `HARMONIC`: preferred feedback partial region
- `TRACK`: fixed amp/room resonance through note-related attraction
- `POLARITY`: signed loop return
- `ROOT`: narrow root witness before the power stage
- `CHAOS`: speaker-mode competition and cross-mic coupling, not random notes
- `PIERCE`: focus and spectral budget of the note-tracked upper return
- `SELF LISTEN`: adaptive body ducking when measured upper feedback is masked
- `TARGET GLITCH`: amount of detector-armed STAB cell capture and reinjection
- `RATCHET`: shorter target-period cells, more repeats, and stronger recurrence
- `OVERLOAD MASK`: adaptive spectral and return-gain containment for dense rough energy
- `OUTPUT`: final trim

`CROOKED` governs performance response; `CHAOS` governs circuit response. They
must remain audibly distinct.

## Stereo behavior

The amp, sag envelope, and main speaker displacement are shared and centered.
Stereo arises after meaningful nonlinear state has already formed:

- two microphone positions sample different combinations of speaker modes;
- a short decorrelated room return enters the secondary feedback path;
- high speaker breakup may alternate or cross-couple between the mic paths;
- the side signal is high-passed so the power-chord root remains compatible
  with mono playback.

Width must collapse gracefully in mono. There is no final chorus, Haas-only
widening, or random per-note pan.

## Implementation reuse and new work

Useful existing parts:

- `dsp/s3g_bass_shred.h`: feedback activity, DC control, delay smoothing,
  source gating, loop filtering, and energy-governor behavior;
- `dsp/s3g_analog_drive_circuits.h`: the eight pedal transfer families;
- `dsp/s3g_bass_amplifier.h`: asymmetric stages, shared sag concepts,
  transformer memory, topology crossfades, and parameter sanitation;
- `dsp/s3g_fractional_waveguide_network.h`: a reference for fractional-delay
  tuning and click-safe delay morphing. Processor Stack uses a smaller linearly
  interpolated circular delay because the microphone return is deliberately
  lossy and band-limited rather than a lossless waveguide.

These should be adapted rather than connected unchanged. Bass Shred's clean
low replacement and dual independent loops oppose this product's shared,
speaker-led identity. Bass Amplifier's bass voicing and terminal cabinet also
need a separate guitar-stack implementation.

Proposed fixed-cost core types:

```text
ProcessorStackExciterLane     // four instances, pick + plucked string/pickup
ProcessorStackPedal           // one shared circuit and memory
ProcessorStackAmplifier       // one preamp, tone, power, and shared sag body
ProcessorStackSpeaker         // one nonlinear modal cone, two microphone taps
ProcessorStackFeedbackLoop    // two taps around one governed central return
ProcessorStackSynth           // MIDI memory, voicing, containment, meters
```

No allocation, locks, file I/O, or unbounded iteration may occur while
processing. Pedal/amp nonlinearities use fixed 2x oversampling; speaker modes
and feedback delays run at the host rate. Parameter changes are smoothed, and
discrete circuit or voicing changes crossfade complete stateful paths rather
than switching transfer functions on one sample.

## Prototype order

1. Build a mono spike/noise exciter into one shared pedal, amp, nonlinear
   speaker, and feedback return. Prove that it squelches and changes harmonic
   without any sustained oscillator or string.
2. Add note-related fractional loop tuning and the held/release gate. Prove
   stable retuning, bounded tails, and deterministic reset.
3. Add the three voicing modes and four exciter lanes. Verify that POWER and
   HAND intermodulate before the amp rather than sounding like layered mono
   patches.
4. Add the plucked string, pickup comb, and root witness. Keep them removable in
   listening tests so the amp/speaker remains the identity.
5. Add the second microphone, room return, and mono-safe stereo behavior.
6. Add interval-driven CROOKED behavior, expression mappings, presets, CLAP
   state, and GUI only after the raw circuit is satisfying.

The first useful prototype should expose only `MODE`, `STRING`, `BITE`, `STACK`,
`CONE`, `FEEDBACK`, `PROXIMITY`, `HARMONIC`, `CROOKED`, `SPILL`, and `OUTPUT`.
This is enough to determine whether the instrument has a real identity before
the complete surface is committed.

## Verification and listening criteria

Automated checks should establish:

- finite, bounded output across sample rates from 8 kHz through 768 kHz;
- exact silence before excitation and eventual silence after all releases;
- deterministic reset and block-size-independent MIDI timing;
- no allocations during note, parameter, or process handling;
- click-safe continuous delay retuning and circuit/voicing transitions;
- a longer but still decaying tail as FEEDBACK or SPILL rises;
- stereo output that remains finite and does not lose the low root in mono;
- audible shared-sag interaction when one note of a held chord changes;
- output containment that does not conceal internal feedback activity.

Listening tests should answer more important product questions:

- With `STRING = 0`, can a short pick packet become a sustained, speaker-led
  squelch at high feedback?
- Does bypassing `CONE` remove the instrument's identity rather than merely
  make it brighter?
- Does a one-note POWER voicing sound like one damaged stack, not three synth
  oscillators through distortion?
- Do seconds, tritones, repeated notes, and wide leaps become angular at high
  CROOKED while the exact played phrase remains recognizable?
- Can pressure ride a note from bark into howl and back without a gain jump?
- At moderate settings, can dry rests and hard rhythmic stops survive the
  feedback tail?

If the answers depend on a detailed string model, the topology has drifted
away from the premise.

## Initial preset intentions

- `BARE STACK`: almost no wire, moderate cone, short spill; the amp is exposed.
- `ONE FINGER RIFF`: POWER mode, root/fifth/octave, hard pick, shared sag.
- `TROUT MASK JAB`: LEAD mode, dry gaps, high CROOKED, nasal focus, reluctant
  feedback. The name is a broad cultural cue, not a transcription or tone
  replica.
- `SPEAKER COUGH`: low tracking, negative polarity, strong cone displacement.
- `PINCHED WIRE`: thin exciter, high harmonic target, short bright feedback.
- `BROWNOUT FIFTH`: starved pedal bias and slow supply recovery.
- `ROOM FIGHT`: wide mic return and competing root/fifth loop attraction.
- `WELDED CHORD`: HAND mode with heavy stack intermodulation and little wire.
- `HOWL ON PRESSURE`: restrained default return with pressure opening the loop.
- `AMP LEFT ON`: long spill near the safe sustaining boundary, but guaranteed
  to drain after the final release.
