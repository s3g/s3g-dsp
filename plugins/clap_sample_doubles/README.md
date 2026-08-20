# Sample Doubles

`s3g Sample Doubles 2` is a stereo two-read-head sample instrument for
two-copy cutting, slowed varispeed playback, and gradual or stepped phasing.
Both decks reference one immutable mono or stereo source while keeping
independent playback positions.

The instrument provides:

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
- Cut, Sharp, and Blend crossfader curves;
- path or embedded project-audio state; and
- Omni or single-channel MIDI reception with note-command and direct CC
  control.

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

Tracker `VOL` controls punch depth. Its repeat, flam, stutter, ratchet, and
microtime operations act on ordinary note events, so they can articulate the
same command keyboard without a separate MIDI CC lane.

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
