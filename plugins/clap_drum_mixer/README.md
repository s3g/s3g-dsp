# s3g Drum Mixer 16

`s3g Drum Mixer 16` is an eight-lane stereo drum mixer with one fixed
16-channel input and output port. REAPER's plug-in pin connector assigns each
source to an input pair.

- `SUM` places the pre-master stereo mix on outputs 1/2 and clears outputs
  3-16.
- `DIRECT` places the eight post-strip signals on outputs 1/2 through 15/16.
- Each strip has level, stereo balance, low/high shelves, a tunable 120 Hz to
  8 kHz mid bell, click-free mute/solo transitions, and a post-fader AUX send.
- The stereo AUX return combines room/body shaping with the s3g drum overload
  core before the master level. It is not present in `DIRECT` mode.

The DSP class in `dsp/s3g_drum_mixer.h` has no CLAP dependency and is intended
for later direct integration with `s3g-tracker`.
