# s3g Processor Feedback Shift

`s3g Processor Feedback Shift` is an eight-node CLAP audio processor for
performing a feedback ecology rather than programming eight independent effect
lanes. It exposes stable 8-channel audio input and output ports and is
advertised as an audio effect, not a CLAP instrument. MIDI remains available as
an excitation source.

Version 0.18 replaces the earlier motion/rhythm architecture and state format.
Old Feedback Shift project state is rejected rather than migrated.

## The instrument

The primary instrument is a pair of complete feedback scenes:

- **Scene A** and **Scene B** each store the 8x8 signed patch matrix plus every
  node's SHIFT, REGEN, COLOR, BODY, LEVEL, and AUX SEND;
- node MODE, INSERT, SOURCE, INPUT, and insert parameters are shared by the two
  scenes, so morphing changes the ecology rather than swapping processors;
- **MORPH** moves continuously from A to B. The matrix, node feedback material,
  levels, and sends all move together under one performance gesture;
- **RANDOM A** and **RANDOM B** mutate only the named scene. The opposite
  scene, shared node sources/inserts, and the user's output gain, output mode,
  and rotation remain stable;
- **COPY A > B** and **COPY B > A** establish a new starting or destination
  ecology without stopping the network.

The GUI's A and B buttons choose which scene is being edited. They do not move
MORPH. This permits editing the destination while listening anywhere along the
trajectory.

## Morph drivers

MORPH may be manual or driven by one coherent source:

- **MANUAL** uses the MORPH control directly;
- **ECOLOGY ENV** follows the strongest signal across external input and the
  core feedback network;
- **ECOLOGY EDGE** compares fast and slow followers, producing a gesture from
  rising energy rather than remaining high for the duration of a loud sound;
- **LFO** traverses the scenes periodically;
- **CHAOS** produces smooth, related random trajectories;
- **PULSE** supplies sine, square, ramp, or random trajectories and may run
  freely or follow the host tempo division.

**DEPTH** moves an automatic driver toward its result without destroying the
manual base position. **INERTIA** ranges from quick gestures to multi-second
scene travel. **HOLD** freezes the current driver state and scene position.
The UI displays the live A/B position, driver value, phase, and active ecology
detector. For PULSE, the right side exposes transport controls. For every other
driver it exposes the feedback governors:

- **REFLEX** moves the eight nodes' frequency relationships in deliberately
  different directions as an onset or sustained mode builds. Its range starts
  with sub-Hz motion and opens gradually into decisive modal changes;
- **SENSE** lowers the energy threshold for both REFLEX and amplitude
  containment;
- **RECOVER** determines how slowly the reflex edge and containment release.
- **REST** gives every lane its own fast envelope, slow envelope, edge,
  persistence accumulator, and recovery timer. A lane's edge accelerates its
  own rest decision, while adjacent edges exert lighter pressure around the
  ring. Lanes therefore disappear and recover at different irregular times
  instead of passing through a master gate. Higher values reach rest sooner
  and remain silent longer. REST is energy-driven and stochastic, not
  synchronized to the host or a repeating gate pattern.

This is negative control feedback rather than a periodic squash: the network
first tries to leave a dominant mode, then the amplitude governor intervenes
if energy continues to accumulate, and REST can finally clear individual lanes
around a persistent texture. Removing one lane also changes what the remaining
matrix receives, allowing another part of the ecology to bloom. The design is
informed by Nicolas Collins'
*Pea Soup*, where an envelope follower changes the feedback system's phase
relationship as loudness builds. The optional AUX return is detected only
after it reaches a visible matrix route; an un-routed AUX wall cannot become a
hidden global modulation source.

## Virtual-patch splice engine

The splice engine operates above Scene A/B motion. It does not merely move the
same two patches faster. Every lane continuously invents short-lived virtual
patches that can change its matrix row and polarity, SHIFT/RING mode, frequency
family, regeneration, COLOR, BODY, level, AUX send, and audible presence.

- **SPLICE** crossfades the authored ecology toward these temporary patches;
- **RANGE** spans one event per minute to 384 events per second per lane. The
  first quarter of its travel is a deliberately expanded slow zone from one
  event per minute to 1.5 Hz; the remaining travel reaches micro-cut density;
- **FINE** trims the RANGE result symmetrically from half-speed to double-speed
  without making the user traverse the complete exponential range again.
  Eight clocks remain asynchronous and free-running rather than following the
  host grid;
- **CONTRAST** expands each event from a related variation to a radical change
  between sub-Hz, low, audio-rate, and high-frequency shift families;
- **SPACE** increases the chance and duration of lane-specific holes. A quiet
  lane is biased to return on its next event so sparse settings continue to
  breathe rather than latching into silence.

Each patch change uses a 0.18–1.6 ms down/change/up seam. Matrix and synthesis
state are committed only while that lane is at the bottom of the seam. This is
the equivalent of a very short guarded tape splice: transitions stay decisive
without relying on raw digital discontinuities. Ecology edges can initiate
two-to-twelve-event flurries, and rising envelope pressure accelerates only the
corresponding lane. The result alternates dense patch storms with independent
holes instead of imposing a common rhythmic gate.

The Engine panel has **SRC**, **SPL**, and **SUB** banks. SPL opens SPLICE,
RANGE, FINE, CONTRAST, and SPACE. SUB exposes TUNE, SHAPE, DRIVE, DECAY, and
SUSTAIN for the shared sub-bass generator. Teal ticks on the node buttons show
individual splice events; the existing green envelope, gold edge, and rust
REST telemetry remain visible at the same time.

## Node sound

Each node follows this core path:

`exciter + matrix return -> SHIFT/RING -> BODY -> INSERT -> containment -> level`

The unadorned core is designed to remain useful with INSERT, AUX, and the post
granulator bypassed:

- **SHIFT** is a four-stage analytic frequency shifter with a dedicated
  quadratic sub-Hz control zone around 0 Hz and logarithmic reach to ±6000 Hz;
- **RING** uses the same frequency control as a ring-modulation carrier;
- **REGEN** has a broad, controllable region around unity before it enters a
  supercritical range. External input remains feed-forward at REGEN 0;
- **COLOR** is bipolar: negative settings darken the loop, zero stays close to
  the clean shifter, and positive settings pre-emphasize the bright loop
  material. Away from zero it introduces asymmetric, memory-dependent
  saturation rather than a generic output distortion;
- **BODY** introduces a per-node fractional micro-delay with different fixed
  lane proportions. It changes pitch tendency, phase, and feedback density
  without becoming a conventional audible delay;
- the containment stage reduces dangerous sustained energy continuously, with
  user-adjustable SENSE and RECOVER. It does not periodically reset or
  rhythmically squash the network;
- the REFLEX governor acts before containment, nudging a building mode toward
  another resonant relationship instead of merely making it quieter.
- **DRIFT** uses eight independent, irregularly timed random trajectories with
  slow interpolation. It no longer imposes one repeating sine-wave rise and
  fall across every node.

The matrix is read as source columns into destination rows. Click an empty cell
to create a route; click the selected active cell again to remove it.
Option-click creates a negative connection. The selected crosspoint has a
continuous three-decimal gain control, polarity action, and clear action.

## Sources and audio contract

Every node chooses one source:

- **NOISE + HIT**, **NOISE**, **MIDI HIT**, **TONE**, and **SUB BASS** are
  internal sources;
- **EXTERNAL** continuously processes the matching audio input lane;
- **EXT GATE** opens that lane with the hit envelope;
- **OFF** removes local excitation while matrix feedback may continue.

INPUT is a per-node -60 to +12 dB source gain. External lane 1 feeds node 1,
lane 2 feeds node 2, and so on. There is no hidden stereo copy or fold-down.
Incoming MIDI channels 1–8 excite nodes 1–8. Other channels excite all nodes.
SUB BASS uses the same per-node source choice, MIDI/excitation strikes, INPUT,
and matrix path as every other source. TUNE covers 18–120 Hz; SHAPE moves from
sine through triangle to soft square, DRIVE adds controlled saturation, DECAY
covers short thumps through long tails, and SUSTAIN supplies a continuous floor
for feedback drones. Selecting SUB BASS on several nodes creates detuned
instances rather than a mono signal copied across the ring.

The plug-in always exposes one 8-channel surround output:

- **8CH DIRECT** preserves the eight discrete processed lanes;
- **QUAD RING** projects the node ring to outputs 1–4;
- **STEREO RING** projects it to outputs 1–2;
- **ROTATE** turns the source-channel ring before either projection.

Unused output lanes are silent. The port does not change when the output mode
changes, avoiding a host routing rebuild.

## Inserts and secondary processors

Each node has one selectable insert drawn from the s3g processor family. The
node editor uses exactly two selection levels: **CATEGORY** chooses the primary
role, then **EFFECT** shows every processor in that role:

- **CORE** — BYPASS;
- **SPECTRAL** — FILTER, RESONATOR, PHASE;
- **DYNAMICS** — TRANSIENT, BREAK BUS, DRUM BUS;
- **DEGRADE** — DEGRADE, EROSION, RELAY;
- **DRIVE** — WOOL, RAT, DIODE, FOLD;
- **SHRED** — SHRED, WOOL, RAT, ZONE A, ZONE B, FUZZ I, FUZZ II, DIODE;
- **PITCH** — MACRO PITCH;
- **TIME** — REPEATER, TIME, DRUM ECHO, MACRO DELAY;
- **MODULATION** — ROTOR, CHORUS;
- **FRACTURE** — RELAY, CRUSH, SPLICE, LOGIC, VOID, THROAT, ROBOT, OCT DOWN,
  OCT UP, OCT STACK.

The former top-level CRUSH was the exact same processor as DEGRADE and has
been removed. FRACTURE's CRUSH remains because it belongs to the distinct
ten-circuit morphing family. The ten circuits are exposed directly by EFFECT;
there is no third PROCESSOR menu. Contextual DEPTH/COLOR/BIAS controls are
followed by REACT, MEMORY, and MIX. The selected insert exposes its specific
controls. SHRED follows the same two-level rule: its eight circuits appear
directly in EFFECT, while PRESSURE, SHRED, FEEDBACK, COLOR, REACT, TUNE, BODY,
and MIX remain in the contextual control panel. MACRO PITCH exposes coarse and
fine pitch, window, glide, and mix. MACRO DELAY exposes time, feedback, tone,
character, smear, glide, and mix. ROTOR and CHORUS reuse the modulation cores
from No Input Mixer, with independent per-node state and storage allocated in
the plug-in's prepare phase. REPEATER and TIME listen for transients and do not
require host transport lock.

The clearest remaining insert gaps are a short multichannel diffusion/allpass
network and a dedicated event-responsive gate/duck processor. Diffusion would
open the ecology spatially without duplicating DRUM ECHO; gate/duck would make
edge-controlled negative space without duplicating TRANSIENT or the global
REST engine. The drive category is already dense, so new saturation variants
should add a genuinely different memory or feedback model rather than another
waveshaper.

These processors are secondary to the core ecology. Factory INIT deliberately
bypasses them so SHIFT, REGEN, COLOR, and BODY can be judged on their own.

The AUX page contains two distinct paths:

`scene lane sends -> press/saturate/fold/clip/tilt -> RETURN -> matrix sources`

`dry node outputs -> POST GRANULATOR -> output topology`

Scene A and B own separate lane-send amounts; the bus processor itself is
global. RETURN therefore changes the feedback ecology. The granulator is
post-network and has an independent GRAIN MIX. COHERENCE and LANE DRIFT control
whether channel grains stay related or separate, while every lane still reads
only its own buffer.

At **SPACE 0**, DENSITY may overlap grains into a continuous cloud. Any
nonzero SPACE switches the scheduler into gapped mode: the next grain waits for
the longest current lane grain to finish, then observes the displayed 0–2000 ms
silence interval. At 100% GRAIN MIX the gaps are genuinely silent rather than
falling back to dry audio. **SHAPE** selects TUKEY, HANN, TRIANGLE, DECAY, or
GATE windows; GATE retains a short anti-click boundary while giving the hardest
sparse articulation. At intermediate GRAIN MIX settings, each lane's grain
envelope also ducks its source side with a fast attack and smooth release. This
keeps the granulated layer legible instead of placing it over a static dry
crossfade; the fully dry and fully wet endpoints remain unchanged.

## Presets and randomization

The factory bank now contains paired ecologies rather than static patches:

- **COLD START** — neutral reference core;
- **ZERO WEATHER** — sub-Hz pressure and long transitions;
- **RUSTED RING** — bipolar ring-material movement;
- **IMPACT CAVITY** — BODY-led resonant changes;
- **EXTERNAL TEAR** — external-input envelope performance;
- **HARSH WALL** — dense supercritical destination;
- **MICRO CUTS** — pulse-driven fragmented state movement;
- **MILLION SPLICES** — maximum-rate virtual-patch changes with ecology-driven
  flurries and sparse holes;
- **SUB FRACTURE** — slow sub pulses split through changing Fracture circuits.

Preset changes and either RANDOM action are smoothed into the running DSP and
do not panic or clear the feedback state. PANIC remains an explicit performance
safety action.

## Build

```sh
cmake -S . -B build-clap \
  -DS3G_BUILD_CLAP_PLUGIN=ON \
  -DS3G_BUILD_FUTURE_COMPONENTS=ON
cmake --build build-clap --target s3g_feedback_shift_clap
```

The macOS bundle is emitted at:

`build-clap/plugins/clap_feedback_shift/s3g_feedback_shift.clap`
