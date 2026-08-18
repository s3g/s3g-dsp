# s3g Sample Player

`s3g Sample Player 2` is the stereo one-shot and loop instrument in the Sample family.
The bundle exposes two fixed-output CLAP descriptors:

- `s3g Sample Player 2` accepts mono or stereo files and has a stereo output.
- `s3g Sample Player 16` accepts 1–16-channel files and preserves every source
  lane on a fixed 16-channel output.

The player responds chromatically around the Root Note and provides Rate and
Stretch pitch modes alongside Forward,
Forward Loop, Reverse, Reverse Loop, and forward/reverse Ping-Pong modes. Start
and Length define the active sample window. Loop Start and Loop End are
absolute source positions clipped into that window. Forward and reverse wraps
have an adjustable loop-span crossfade; ping-pong modes reflect continuously.
A newly loaded sample initializes Start, the derived End, Loop Start, and Loop
End at safe zero crossings near the file boundaries. The four labeled waveform
handles can be dragged directly, with scroll zoom, Shift-scroll pan, and a
double-click fit command. Double-clicking a boundary slider restores its
sample-aware default. Rate mode links pitch and playback speed like a
conventional sampler. Stretch mode uses overlapped grain readers to change
pitch while the transport, Start/Length duration, and loop timing remain fixed.
The waveform draws an independent MIDI-note-labeled cursor for every active
voice rather than averaging polyphonic positions.

The resonant state-variable filter provides bypass, low-pass, band-pass,
high-pass, and notch types, logarithmic cutoff, resonance, and bipolar
six-octave modulation from the proportional amplitude envelope. The amp
section supplies proportional Attack, Decay, and Release, plus Sustain, Gain,
and velocity sensitivity; Tune and Fine Tune adjust pitch. Sample Player 2 adds
stereo Pan. Sample Player 16 does not expose Pan: every source lane maps
unchanged to its corresponding output lane and unused lanes remain silent. A, D, and R
are percentages of the audible Start/Length duration and together cannot
exceed 100%. That duration follows pitch in Rate mode and remains locked to the
sample timeline in Stretch mode. Forward and Reverse ignore note-off and place Release at
the end of the one-shot; loop modes enter Release on note-off.

Samples can be loaded from the editor or dropped on its waveform. By default,
decoded audio is embedded in CLAP state so projects remain portable. All
channels use one playback clock, including Start/Length, loop wrapping,
Rate/Stretch playback, pitch, and envelope changes.

Build the bundle with:

```sh
cmake --build build-clap --target s3g_sample_player_clap -j 8
```

The macOS bundle is written to:

```text
build-clap/plugins/clap_sample_player/s3g_sample_player.clap
```
