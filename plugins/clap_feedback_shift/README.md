# s3g Processor Feedback Shift

`s3g Processor Feedback Shift` is an eight-node CLAP audio processor for
performing a feedback ecology rather than programming eight independent effect
lanes. It exposes stable 8-channel audio input and output ports and is
advertised as an audio effect, not a CLAP instrument. MIDI remains available as
an excitation source.

Version 0.13 deliberately replaces the earlier motion/rhythm architecture and
state format. Old Feedback Shift project state is rejected rather than
migrated.

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
- **INPUT ENV** follows the largest external-input envelope;
- **LFO** traverses the scenes periodically;
- **CHAOS** produces smooth, related random trajectories;
- **PULSE** supplies sine, square, ramp, or random trajectories and may run
  freely or follow the host tempo division.

**DEPTH** moves an automatic driver toward its result without destroying the
manual base position. **INERTIA** ranges from quick gestures to multi-second
scene travel. **HOLD** freezes the current driver state and scene position.
The UI displays the live A/B position, driver value, phase, and input envelope.

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
- the containment stage reduces dangerous sustained energy continuously. It
  does not periodically reset or rhythmically squash the network.

The matrix is read as source columns into destination rows. Click an empty cell
to create a route; click the selected active cell again to remove it.
Option-click creates a negative connection. The selected crosspoint has a
continuous three-decimal gain control, polarity action, and clear action.

## Sources and audio contract

Every node chooses one source:

- **NOISE + HIT**, **NOISE**, **MIDI HIT**, and **TONE** are internal sources;
- **EXTERNAL** continuously processes the matching audio input lane;
- **EXT GATE** opens that lane with the hit envelope;
- **OFF** removes local excitation while matrix feedback may continue.

INPUT is a per-node -60 to +12 dB source gain. External lane 1 feeds node 1,
lane 2 feeds node 2, and so on. There is no hidden stereo copy or fold-down.
Incoming MIDI channels 1–8 excite nodes 1–8. Other channels excite all nodes.

The plug-in always exposes one 8-channel surround output:

- **8CH DIRECT** preserves the eight discrete processed lanes;
- **QUAD RING** projects the node ring to outputs 1–4;
- **STEREO RING** projects it to outputs 1–2;
- **ROTATE** turns the source-channel ring before either projection.

Unused output lanes are silent. The port does not change when the output mode
changes, avoiding a host routing rebuild.

## Inserts and secondary processors

Each node has one selectable insert drawn from the s3g processor family,
including FILTER, DEGRADE, TRANSIENT, RESONATOR, EROSION, REPEATER, TIME,
DRUM ECHO, BREAK BUS, and DRUM BUS. The selected insert exposes its specific
controls. REPEATER and TIME listen for transients and do not require host
transport lock.

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

## Presets and randomization

The factory bank now contains paired ecologies rather than static patches:

- **COLD START** — neutral reference core;
- **ZERO WEATHER** — sub-Hz pressure and long transitions;
- **RUSTED RING** — bipolar ring-material movement;
- **IMPACT CAVITY** — BODY-led resonant changes;
- **EXTERNAL TEAR** — external-input envelope performance;
- **HARSH WALL** — dense supercritical destination;
- **MICRO CUTS** — pulse-driven fragmented state movement.

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
