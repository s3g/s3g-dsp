# Sample Wavesets

`s3g Sample Wavesets` analyzes one mono or stereo recording into
pseudo-wavecycles and gives two free-running decks independent access to the
same source. Each deck retains its own position, speed, level, phase, and
transport state. Shared waveset controls shape both streams before a
Doubles-style crossfader.

The instrument provides:

- two independently positioned waveset decks sharing one stereo-safe crossing
  map;
- per-deck Play/Pause, Stop, Restart, Position, Speed, and Level controls;
- linked or independent transport plus shared Restart, Play, and Stop;
- a continuous crossfader with Cut, Sharp, and Blend curves;
- Stretch, Preserve, and Hold relationships between output time and source
  progression;
- groups of 1, 2, 4, 8, 16, or 32 cycles, 1–16 whole-group repeats, and a
  signed stride;
- Forward, Reverse, Pendulum, and Shuffle traversal within each group;
- Repeat, Omit, Replace, Envelope, and Harmonic transformations with continuous
  Depth and Join controls;
- Raw, 8 kHz, 4 kHz, 1 kHz, and 250 Hz crossing-detail analysis;
- a focused group scope, direct waveform seeking, button/MIDI feedback, and
  compositor-driven deck cursors;
- five factory starting presets; and
- path or embedded project-audio state.

## Tracker and MIDI

`MIDI Channel` selects one receive channel for the complete instrument. Notes
are commands rather than pitches:

| Note | Command |
| --- | --- |
| 36 / 37 / 38 / 39 | Restart both / Stop both / Play both / Pause both |
| 40 / 41 | Play or pause Deck A / Deck B |
| 42 / 43 | Restart Deck A / Deck B |
| 44 / 45 | Stop Deck A / Deck B |
| 46 / 47 / 48 | Crossfader A / center / B |

When Link is on, either deck's GUI transport controls both decks. The selected
MIDI channel also accepts:

| CC | Control |
| --- | --- |
| 1 or 16 | Crossfader, A through B |
| 17 / 18 | Deck A / Deck B position |
| 19 / 20 | Deck A / Deck B speed, 0.25× to 4× |
| 21 | Shared group-size selection |
| 22 | Shared process amount |

All 20 controls are ordinary CLAP parameters, so Tracker CC lanes, host
automation, and MIDI learn can coexist with transport-note sequencing.

## Analysis model

The analysis adapts the rising-zero-crossing capture model in Graham
Wakefield's 2012 Gen waveset chopper/repeater. Each crossing is interpolated
between adjacent samples and retains its fractional start, length, peak, and
RMS. The loaded file replaces the Gen patch's rolling recording buffer: the
full source is analyzed in the background, then both decks read the immutable
map without a competing write head.

## Cursor presentation contract

The cyan Deck A and orange Deck B cursors use persistent Core Animation
trajectories on macOS. Routine AppKit repaint and meter traffic never drives
the visible playheads or reinstalls an unchanged trajectory. This is the same
presentation contract established by Sample Doubles, so both cursors continue
through host gesture and menu-presentation gaps.

Build the bundle with:

```sh
cmake --build build-clap --target s3g_sample_wavesets_clap -j 8
```

The macOS bundle is written to:

```text
build-clap/plugins/clap_sample_wavesets/s3g_sample_wavesets.clap
```
