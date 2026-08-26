# s3g Drum Overload 2

Drum Overload is a stereo-only, zero-latency distortion effect voiced for
clean procedural drums, especially the `s3g_drum_*` family. It uses one shared
control set for both lanes and a continuously variable stereo-linked detector;
audio never crosses between the left and right signal paths.

## Signal path

`Input -> linked transient detector -> Density/Punch -> low-band preservation
-> 2x nonlinear core -> Tone -> Mix -> Output`

The eight circuits cover a wide range of drum overload:

- **Console**: broad, controlled saturation for a drum bus.
- **Valve**: asymmetric even-harmonic weight.
- **Clip**: hard/soft clipping for attacks and electronic drums.
- **Rupture**: folded overload for broken, aggressive percussion.
- **Tape**: magnetic memory and softened head bandwidth for ringing shells.
- **Transformer**: low-frequency core emphasis tuned for floor and rack toms.
- **Diode**: asymmetric attack clipping for sticks, rims, and snares.
- **Speaker**: level-dependent cone sag and bandwidth loss for overloaded kits.

`Overload` is the main drive and saturation-depth control. `Density` pulls the
body of each hit forward, while bipolar `Punch` either preserves/enhances the
attack or crushes it. `Weight` returns a clean portion of the sub-210 Hz band
after the overload core. `Breakup`, `Bias`, and `Tone` shape the distortion.
`Link` shares detector gain between lanes without collapsing the stereo image.
The clean Weight handoff is tuned to 210 Hz, covering the completed drum
family's 58–182 Hz floor/low/mid/high tom fundamentals while their upper shell
modes still excite the distortion core.

## Build and use in REAPER

```sh
cmake -S . -B build-clap \
  -DCMAKE_BUILD_TYPE=Release \
  -DS3G_BUILD_CLAP_PLUGIN=ON
cmake --build build-clap --target s3g_drum_overload_clap
```

The macOS bundle is written to:

`build-clap/plugins/clap_drum_overload/s3g_drum_overload.clap`

Copy the bundle to `~/Library/Audio/Plug-Ins/CLAP/`, then use
**Preferences -> Plug-ins -> Re-scan** in REAPER. The CLAP identifier is
`org.s3g.s3g-dsp.drum-overload`.

Useful starting points:

- Drum bus: Console, OVR 55%, DENS 45%, PUNCH +20%, WEIGHT 75%, MIX 75%.
- Kick weight: Valve, OVR 65%, DENS 35%, BIAS +20%, WEIGHT 90%, MIX 85%.
- Snare clip: Clip, OVR 72%, DENS 55%, PUNCH +35%, BREAK 25%, MIX 80%.
- Open toms: Tape, OVR 58%, DENS 38%, WEIGHT 82%, BREAK 18%, MIX 78%.
- Floor tom: Transformer, OVR 68%, DENS 46%, WEIGHT 84%, MIX 86%.
- Rim/sticks: Diode, OVR 70%, PUNCH +28%, BIAS +18%, MIX 76%.
- Destroyed kit: Rupture, OVR 90%, DENS 75%, PUNCH -20%, BREAK 70%, MIX 100%.

## Tracker integration seam

The reusable implementation is the host-independent
`dsp/s3g_drum_overload.h`. A tracker effect owns one `s3g::DrumOverload`, calls
`prepare(sampleRate)` and `setParams(...)`, then calls
`processFrame(left, right)` once per stereo frame. The parameters are a plain
POD value block and the audio path allocates no memory.
