# Sample Doubles

`s3g Sample Doubles 2` is a stereo two-read-head sample instrument for
two-copy cutting, slowed varispeed playback, and gradual or stepped phasing.
Both decks reference one immutable mono or stereo source while keeping
independent playback positions.

The instrument provides:

- coupled speed and pitch from -24 to +12 semitones;
- a source-BPM clock for Deck B offsets and phase steps;
- a signed -8 to +8 beat Deck B offset;
- ±100 cents of Deck B-only speed drift;
- 1/16, 1/8, 1/4, 1/2, 1, 2, and 4 beat phase steps;
- draggable Start and End boundaries plus optional looping;
- Cut, Sharp, and Blend crossfader curves;
- path or embedded project-audio state; and
- Omni or single-channel MIDI reception.

## Tracker command notes

| Note | Command |
| --- | --- |
| 36 | Restart both decks |
| 37 | Stop |
| 38 | Sync Deck B to Deck A plus the current offset |
| 39 / 42 | Move Deck B backward / forward by Phase Step |
| 40 / 41 | Gate a velocity-sensitive Punch A / Punch B |
| 43 | Play or resume |
| 48–60 | Select -4, -2, -1, -1/2, -1/4, -1/8, 0, +1/8, +1/4, +1/2, +1, +2, or +4 beats and sync immediately |

Tracker `VOL` controls punch depth. Its repeat, flam, stutter, ratchet, and
microtime operations act on ordinary note events, so they can articulate the
same command keyboard without a separate MIDI CC lane.

Build the bundle with:

```sh
cmake --build build-clap --target s3g_sample_doubles_clap -j 8
```

The macOS bundle is written to:

```text
build-clap/plugins/clap_sample_doubles/s3g_sample_doubles.clap
```
