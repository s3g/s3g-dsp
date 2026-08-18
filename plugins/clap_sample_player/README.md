# Sample Player

`Sample Player 2` is the stereo one-shot and loop instrument in the Sample family.
The bundle exposes two fixed-output CLAP descriptors:

- `Sample Player 2` accepts mono or stereo files and has a stereo output.
- `Sample Player 16` accepts 1–16-channel files and preserves every source
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
pitch while transport timing remains independent of MIDI pitch.
The waveform draws an independent MIDI-note-labeled cursor for every active
voice rather than averaging polyphonic positions.

Tempo Sync defaults to Free. Host mode compares Sample BPM with the CLAP host
tempo: Rate changes timing and pitch together, while Stretch follows tempo and
keeps MIDI pitch independent. Rate Below / Stretch Above uses Rate for notes
below Root Note and Stretch for Root Note and higher notes. Tune and Fine Tune
shift pitch without moving that keyboard split. Pitch-mode changes on active
voices use a 10 ms transition. Trigger defaults to Auto, retaining the original
one-shot note-off behavior for Forward/Reverse and gated note-off behavior for
loops. Gate, One Shot, and Toggle are also available. Retrigger defaults to
Layer, with Restart and Ignore choices for repeated keys. Voice mode defaults
to Poly; Mono restarts one voice, while Legato preserves its playhead and
envelope and applies the adjustable Glide time between connected notes.

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

The compact upper-right Output / MIDI panel begins with Out and also contains
the Omni or Channel 1-16 receive selector and velocity sensitivity. Receive
defaults to Omni. Stereo Pan is shown only in Sample Player 2; Sample Player 16
continues to preserve every source lane without balancing or duplication. Out
and stereo Pan process the final summed output, so changes immediately affect
notes that are already sounding instead of being captured at note-on.
`KILL ALL` is a momentary panic action in this panel. It immediately clears all
voices and held-note state, including One Shot or Toggle loops, without
unloading the sample or changing saved controls. A new note can start playback
normally after the kill.

Tune and Fine Tune also retarget every active voice through a 10 ms smoothing
transition; Root Note remains a trigger-time mapping. Sustain changes reach
held voices through the same 10 ms dezipper, and Release is recalculated from
the current setting until a voice actually enters its Release stage. Editing
Loop Start, Loop End, or Loop Crossfade updates active loop voices through a
5 ms click-safe transition. Start and Length remain fixed for each triggered
voice.

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
