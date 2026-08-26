# s3g Drum Echo 2

Drum Echo is a stereo multi-head tape delay built around percussion rather
than a general-purpose echo. Three equally spaced playback heads can be used
alone or in combinations, with free timing or host-synchronized divisions.

Its drum response stage detects new attacks before the delay write:

- `HIT SEND` emphasizes or suppresses detected transients in the repeats.
- `SENSE` changes the detector threshold for quiet percussion or full buses.
- `DUCK` clears space for each new hit without muting the existing tail.
- `TONE`, `WEAR`, and `FLUT` progressively soften and destabilize repeats.
- `SPREAD` places the three heads across the stereo field and adds restrained
  cross-feedback.

The design takes the useful workflow cues of classic three-head tape echoes,
but it is original DSP and is not an emulation of a particular product.

## Signal path

`Input -> transient detector -> transient send -> tape write -> three heads
-> filtered/saturated feedback -> hit duck -> mix -> output`

## Build and use in REAPER

```sh
cmake -S . -B build-clap \
  -DCMAKE_BUILD_TYPE=Release \
  -DS3G_BUILD_CLAP_PLUGIN=ON
cmake --build build-clap --target s3g_drum_echo_clap
```

The macOS bundle is written to:

`build-clap/plugins/clap_drum_echo/s3g_drum_echo.clap`

The stable CLAP identifier is `org.s3g.s3g-dsp.drum-echo`.

## Tracker integration seam

The host-independent implementation is `dsp/s3g_drum_echo.h`. A host owns one
`s3g::DrumEcho`, calls `prepare(sampleRate)`, `setTempo(...)`, and
`setParams(...)`, then calls `processFrame(left, right)` for each stereo frame.
The audio path allocates no memory after `prepare()`.
