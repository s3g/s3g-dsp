# Sample Doubles

`s3g Sample Doubles 2` is a stereo sample instrument with two independently
positioned read heads over one mono or stereo source. It combines shared
varispeed playback with per-deck levels, cues, transport, timing controls, and
a performance crossfader.

The instrument provides:

- five factory presets for the documented two-deck starting configurations;
- coupled speed and pitch from -24 to +12 semitones;
- load-time source-BPM estimation with confidence, manual override, and
  half/double choices;
- a signed -8 to +8 beat Deck B offset;
- a separate continuous ±1 beat live phase control;
- ±100 cents of Deck B-only speed drift;
- 1/16, 1/8, 1/4, 1/2, 1, 2, and 4 beat phase steps;
- draggable Start and End boundaries plus optional looping;
- independent smoothed Deck A and Deck B levels;
- linked or independent deck Play/Pause controls;
- momentary per-deck Drag gestures with a smooth motor recovery;
- one zero-crossing cue marker and retrigger action per deck, with adjustable
  reaction-time pre-roll and direct marker dragging;
- Cut, Sharp, and Blend crossfader curves;
- path or embedded project-audio state; and
- Omni or single-channel MIDI reception with note-command and direct CC
  control.

`Auto` retains the most recent analyzed BPM even after the Sample BPM slider
is edited, so pressing it restores that candidate. If no analysis is retained,
it rescans the current immutable sample on the loader worker without reloading
the audio, moving the playheads, or clearing deck cues.

The factory presets change Speed, Deck B Offset/Drift/Live Phase, Phase Step,
Loop, crossfader position, and crossfader curve. They preserve the sample,
source BPM, S/E region, cue state and pre-roll, MIDI receive channel, Link,
both deck levels, and Out.

## Tracker command notes

| Note | Command |
| --- | --- |
| 36 | Restart both decks |
| 37 | Stop |
| 38 | Sync Deck B to Deck A plus the current offset |
| 39 / 42 | Move Deck B backward / forward by Phase Step |
| 40 / 41 | Gate a velocity-sensitive Punch A / Punch B |
| 43 | Play or resume |
| 44 / 45 | Toggle Deck A / Deck B Play/Pause |
| 46 / 47 | Gate Drag A / Drag B |
| 48–60 | Select -4, -2, -1, -1/2, -1/4, -1/8, 0, +1/8, +1/4, +1/2, +1, +2, or +4 beats and sync immediately |
| 61 / 63 | Replace Deck A / Deck B cue after the configured pre-roll, snapped to the nearest zero crossing |
| 62 / 64 | Retrigger Deck A / Deck B from its cue |

Tracker `VOL` controls punch depth and Drag strength. Full velocity gives the
deepest slowdown; lower velocity produces a lighter slowdown. Its repeat,
flam, stutter, ratchet, and
microtime operations act on ordinary note events, so they can articulate the
same command keyboard without a separate MIDI CC lane.

Each deck retains exactly one cue. Pressing its Cue button or command note
looks backward by `Cue Preroll` (0–1000 ms, default 150 ms), replaces the
previous point, and snaps the result to a nearby zero crossing. The lookback
follows the deck's current varispeed, Deck B drift, and Drag rate, so it
represents heard time instead of a fixed number of source samples. Once a cue
exists, click and drag its colored waveform marker to place it directly with
the mouse; direct placement bypasses pre-roll but still zero-crossing-snaps.
Trigger starts that deck from the marker on every press, including when the
deck was paused, so Tracker repeats and ratchets can create cue-point patterns.

## Continuous MIDI controls

| CC | Control |
| --- | --- |
| 16 | Crossfader, A to B |
| 17 | Deck A Level, -60 to +12 dB |
| 18 | Deck B Level, -60 to +12 dB |
| 19 | Deck B Live Phase, -1 to +1 beat |

The fixed CC map is useful for direct MIDI and Tracker CC lanes. The same four
controls remain ordinary CLAP parameters, so hosts may also automate or learn
them through their normal parameter system. GUI and incoming MIDI commands
share the same visible button/hold feedback.

Build the bundle with:

```sh
cmake --build build-clap --target s3g_sample_doubles_clap -j 8
```

The macOS bundle is written to:

```text
build-clap/plugins/clap_sample_doubles/s3g_sample_doubles.clap
```

Implementation constraints and proposed future work are tracked in
[ENGINEERING.md](ENGINEERING.md).
