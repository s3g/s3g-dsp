# s3g Processor Feedback Shift

`s3g Processor Feedback Shift` is an eight-node CLAP feedback instrument and
processor in the s3g Processor family. It exposes one stable eight-channel
excitation input and one stable eight-channel output.
It starts with the playable frequency-shift loop developed for the Slicer, then
places eight versions of that loop inside a signed patch matrix. Each node also
contains one selectable effect pedal, so connecting nodes in series, parallel,
or circular routes creates the processing chain.

This is intentionally not a replacement for `s3g No Input Mixer`. NIM models a
larger mixer ecosystem with resonant bodies, detailed channel strips, inserts,
auxes, controller integration, and movement behavior. Feedback Shift is a
lighter performance instrument: eight immediately playable SHIFT/RING nodes,
an exposed routing grid, and a deliberately broad pedal vocabulary whose full
processor controls remain visible in the selected-node editor.

## Signal architecture

Each node follows this path:

`selected exciter + matrix -> SHIFT/RING -> pedal -> containment -> dry output`

The contained node returns feed two related but deliberately separate paths:

`node returns -> lane SENDS -> press/saturate/fold/clip/tilt -> RETURN -> matrix sources`

`dry node outputs -> POST GRANULATOR -> output topology`

The first path is the eight-lane **FEEDBACK AUX**. Its linked dynamics and
saturation build continuous wall density; wavefold and clipping add increasingly
rigid abrasion. **RETURN** adds the processed lane signals back into the
corresponding matrix sources on the next sample. It therefore changes the
feedback ecology rather than processing the audible output bus. RETURN at zero
disconnects this feedback return while the dry node sources remain intact.

The second path is the multichannel **POST GRANULATOR**, placed after the node
network and immediately before 8-channel direct, quad, or stereo output
projection. SIZE, DENSITY, SCATTER, PITCH, and EDGE move it from broad clouds to
short micro-fragments with smooth windowed boundaries. **GRAIN MIX** blends the
granular stream with the dry post-node lanes. Moving it here makes grains
audible without feeding them back through the matrix and gives them an explicit
post-network role.

Grain launches share one master clock, but the lanes need not remain rigidly
locked. At 100% **COHERENCE**, every lane uses the same read position, pitch,
duration, and participation. Reducing COHERENCE lets **LANE DRIFT** progressively
separate those properties, including occasional lane dropout. Every lane still
reads only its own discrete buffer: independence changes temporal behavior but
never creates hidden cross-channel routing. The granulator runs before output
topology, so its eight-lane texture remains available to Direct 8 and is then
projected consistently in quad or stereo modes.

The AUX page exposes an independent 0–100% **SEND** for every node. A zero send
removes that lane from the feedback wall, but does not mute its dry output or
its post-granulator input. This follows No Input Mixer's useful separation of
per-lane send and bus-wide return gain. Feedback Shift deliberately does not
copy NIM's signed per-destination AUX return vectors: processed lane 1 returns
to matrix source 1, lane 2 to source 2, and so on. Cross-lane and inverted
routing remains the job of the visible 8×8 patch matrix, avoiding a second
hidden routing matrix.

The plugin always exposes one stable main 8-channel surround port. **8CH
DIRECT** places the post-pedal signals on eight discrete host outputs. **QUAD
RING** and **STEREO RING** project the eight nodes around a circular speaker
layout, write the result to outputs 1-4 or 1-2, and silence the unused outputs.
The continuously automatable **ROTATE** control turns the source-channel ring
before projection. Keeping one fixed port means the output mode can change
without rebuilding REAPER routing.

## Exciters and external input

Every node selects its own source before the feedback matrix is summed:

- **NOISE + HIT** preserves the original autonomous behavior: global EXCITE
  supplies continuous filtered noise and MIDI/HIT adds a velocity-sensitive
  burst;
- **NOISE** uses only the continuous internal noise source;
- **MIDI HIT** is silent until the node receives a MIDI note or HIT gesture;
- **TONE** uses an internal sine exciter. Its frequency follows the magnitude
  of that node's frequency setting, with an 18 Hz floor, and MIDI/HIT adds a
  short amplitude burst;
- **EXTERNAL** continuously reads the matching lane of the eight-channel input;
- **EXT GATE** opens that matching external lane with the MIDI/HIT envelope;
- **OFF** disconnects local excitation while matrix feedback may continue.

**INPUT** is a per-node -60 to +12 dB gain before the matrix. External lane 1
feeds node 1, lane 2 feeds node 2, and so on; no automatic fold or cross-channel
copying is hidden in the source layer. A stereo REAPER track therefore reaches
nodes 1 and 2 directly, while a multichannel track can address all eight nodes.

The 8×8 patch panel is read as **source columns into destination rows**. Routes
are signed and continuously automatable from -1 to +1. In the editor:

- click an empty cell to create a connection (`+0.94` on the diagonal,
  `+0.25` elsewhere), click an existing connection once to select it, then
  click the selected connection again to remove it. Option-click creates a
  negative connection. This is the same create/select/clear rule used by NIM;
- the selected GAIN row edits signed gain continuously from -1 to +1 with a
  three-decimal readout; POLARITY reverses its sign and CLEAR closes it.

The initial patch uses strong self-routes with quieter alternating-sign routes
from the preceding node.

## Selected node

Click NODE 1–8 to edit that node. Each node stores an independent:

- **MODE**: frequency SHIFT or RING modulation;
- **PEDAL**: Slicer processors (FILTER, DEGRADE, TRANSIENT, RESONATOR,
  EROSION, REPEATER, TIME), DRUM ECHO, BREAK BUS, DRUM BUS, or the original
  analog/performance pedals (WOOL, RAT, DIODE, FOLD, CRUSH, RELAY, PHASE);
- **FREQUENCY**: -6000 to +6000 Hz;
- **REGENERATION**: 0 to 118%;
- **COLOR**: effect intensity/timbre;
- **NODE LEVEL**: the node's discrete output level.
- A processor-specific insert panel. FILTER exposes resonance, cutoff, mode,
  drive and mix; DRUM ECHO exposes all heads, clock, time, feedback, wear,
  flutter, transient, sensitivity, duck, tone and mix; BREAK BUS and DRUM BUS
  similarly expose their complete compact control sets. Every other insert
  shows its own named controls rather than generic macros.

REPEATER and TIME are transient-listening memory nodes; they do not wait for or
quantize to host transport. Both expose SENSE, read-position-aware XFADE,
per-capture DRIFT, and signed LINK. LINK listens to the next node's return
(node 8 wraps to node 1), can trigger the local capture, and mixes that return
into the captured memory. Positive and negative links therefore create related
or phase-opposed temporal families. The insert header and progress line show
LISTEN, CAPTURE, and the active playback state. DRUM ECHO reuses the multi-head
Drum Echo engine, BREAK BUS uses the Slicer's dynamics/saturation/clip
processor, and DRUM BUS uses the console stage shared with Drum Mixer.

FREQUENCY remains an Hz-valued CLAP parameter, but the editor gives it a signed
two-part taper. The center is exactly 0 Hz, the inner 30% of either side is a
quadratic 0–1 Hz precision band, and the rest reaches 6000 Hz logarithmically.
Each SHIFT/RING node
also restores the Slicer circuit's local one-sample wet-feedback path. REGEN
opens that path progressively above 50%; COLOR both increases its upper feedback
ceiling and adds nonlinear shaping to frequency SHIFT as well as RING. Near-zero
frequency, high REGEN, and high COLOR can therefore cross into sustained,
breathing self-oscillation, while the outer governor continuously contains the
return instead of periodically resetting the circuit.

## Ecosystem and rhythm

- **EXCITE** controls continuous internal noise injection.
- **DRIFT** moves node shift frequencies at related offsets.
- **MOTION** sets the shared rate for the assignable LFO and chaotic drift
  generators, from approximately 0.01 to 8 Hz.
- **PULSE** fades in rhythmic feedback gating. At zero, no rhythmic gate is
  applied.
- **RATE** controls the free-running pulse rate.
- **SYNC** switches RATE to the host-tempo DIVISION.
- **DIV** ranges from 1/64 note to 16 bars.
- **SHAPE** selects SINE, SQUARE, RAMP, or RANDOM gating.
- **GAIN** controls the common gain before the final safety ceiling.
- **OUTPUT MODE** selects 8-channel direct, quad ring fold, or stereo ring
  fold; **ROTATE** turns the eight-channel source ring for either fold mode.
- **RUN** fades the complete ecosystem in or out.
- **HIT** excites the currently selected node.
- **PANIC** clears the complete feedback network.

Each selected node also has a compact motion assignment:

- **MOTION** chooses OFF, LFO, CHAOS, ENVELOPE, or PULSE;
- **TARGET** chooses FREQUENCY, REGEN, COLOR, LEVEL, or that node's AUX SEND;
- **DEPTH** is signed, so the same source can push or pull the destination;
- **SLEW** ranges from immediate gestures to slow, liquid transitions.

LFO lanes share a clock but use evenly distributed phases around the
eight-channel ring. CHAOS shares a rate while maintaining related independent
lane trajectories. ENVELOPE follows the selected node's contained activity,
and PULSE reuses the existing free or host-synchronized rhythm generator.
Motion changes effective DSP values without overwriting the user's stored base
parameter, so turning MOTION off returns cleanly to the edited patch.

There is no periodic or manual SQUASH control. Every node instead has a smooth
continuous containment stage that progressively reduces dangerous feedback
energy without imposing a hard rhythmic reset.

Incoming MIDI channels 1–8 excite nodes 1–8 respectively. Notes on other
channels excite all nodes. Note number does not retune a node; frequency remains
a direct performance parameter, while note velocity controls strike strength.

## Presets and randomization

The standard s3g title band contains the PRESET menu plus LOAD, SAVE and RANDOM.
The factory presets apply the complete patch: insert choices and their
parameters, SHIFT/RING settings, rhythm, output, and all signed routes. LOAD
and SAVE use the shared s3g preset store. RANDOM creates a constrained ecosystem
with sparse routing, bounded regeneration, and randomized insert parameters,
while preserving the user's main output level, output mode, and ring rotation.

Every factory preset stores its own feedback-wall send profile and a separately
voiced post granulator:

| Preset | Return | Grain mix | Coherence | Lane drift | Role |
| --- | ---: | ---: | ---: | ---: | --- |
| INIT MATRIX | 6% | 8% | 90% | 15% | Quiet wall, nearly locked grain air |
| CLOCKED RELAYS | 10% | 28% | 75% | 35% | Tight recirculation with related fragments |
| BREAK SWARM | 12% | 48% | 38% | 78% | Scattered transient swarm |
| DUB CIRCUIT | 16% | 22% | 68% | 42% | Dark feedback bloom and loose grain shadow |
| ERODED METAL | 10% | 36% | 32% | 70% | Pitched abrasive separation |
| FREEZE BRAID | 9% | 52% | 22% | 84% | Long, divergent memory braid |
| DRUM BUS RITUAL | 17% | 8% | 85% | 20% | Press and console glue with subtle grains |
| NEGATIVE FIELD | 8% | 30% | 30% | 76% | Dark inverse wall and unstable fragments |
| ZERO BREACH | 4% | 16% | 72% | 30% | Guarded near-zero instability |
| HARSH WALL | 14% | 32% | 18% | 88% | Dense wall with spatially broken grains |
| MICRO GRAINS | 16% | 82% | 5% | 100% | Maximally independent micro-fragments |

RANDOM uses the same feedback-aware limits, keeping RETURN between 4% and 17%,
GRAIN MIX between 10% and 65%, and generating bounded coherence/lane-drift
pairs.
Random patches retain the continuous containment stage, but should still be
treated like a feedback instrument and monitored at a sensible listening level.

## Build

The instrument is currently a future-component preview:

```sh
cmake -S . -B build-clap \
  -DS3G_BUILD_CLAP_PLUGIN=ON \
  -DS3G_BUILD_FUTURE_COMPONENTS=ON
cmake --build build-clap --target s3g_feedback_shift_clap
```

The macOS bundle is emitted at:

`build-clap/plugins/clap_feedback_shift/s3g_feedback_shift.clap`
