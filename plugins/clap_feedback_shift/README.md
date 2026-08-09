# s3g Processor Feedback Shift

`s3g Processor Feedback Shift` is an eight-node, eight-output CLAP feedback
instrument in the s3g Processor family.
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

`matrix + excitation -> SHIFT/RING -> pedal -> containment -> matrix return`

The plugin always exposes one stable main 8-channel surround port. **8CH
DIRECT** places the post-pedal signals on eight discrete host outputs. **QUAD
RING** and **STEREO RING** project the eight nodes around a circular speaker
layout, write the result to outputs 1-4 or 1-2, and silence the unused outputs.
The continuously automatable **ROTATE** control turns the source-channel ring
before projection. Keeping one fixed port means the output mode can change
without rebuilding REAPER routing.

The 8×8 patch panel is read as **source columns into destination rows**. Routes
are signed and continuously automatable from -1 to +1. In the editor:

- click a cell to cycle `OFF -> +0.5 -> +1 -> OFF`;
- Option-click a cell to cycle `OFF -> -0.5 -> -1 -> OFF`.

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

REPEATER and TIME listen for transients inside their node; they do not wait for
or quantize to host transport. DRUM ECHO reuses the multi-head Drum Echo engine,
BREAK BUS uses the Slicer's dynamics/saturation/clip processor, and DRUM BUS
uses the console stage shared with Drum Mixer.

FREQUENCY remains an Hz-valued CLAP parameter, but the editor gives it a signed
logarithmic taper. The center is exactly 0 Hz, adjacent travel covers sub-Hz and
low-Hz shifts precisely, and either edge reaches 6000 Hz.

## Ecosystem and rhythm

- **EXCITE** controls continuous internal noise injection.
- **DRIFT** moves node shift frequencies at related offsets.
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

There is no periodic or manual SQUASH control. Every node instead has a smooth
continuous containment stage that progressively reduces dangerous feedback
energy without imposing a hard rhythmic reset.

Incoming MIDI channels 1–8 excite nodes 1–8 respectively. Notes on other
channels excite all nodes. Note number does not retune a node; frequency remains
a direct performance parameter, while note velocity controls strike strength.

## Presets and randomization

The standard s3g title band contains the PRESET menu plus LOAD, SAVE and RANDOM.
The eight factory presets apply the complete patch: insert choices and their
parameters, SHIFT/RING settings, rhythm, output, and all signed routes. LOAD
and SAVE use the shared s3g preset store. RANDOM creates a constrained ecosystem
with sparse routing, bounded regeneration, randomized insert parameters, and
conservative output gain. Random patches retain the continuous containment
stage, but should still be treated like a feedback instrument and monitored at
a sensible listening level.

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
