# s3g Drum Break

`s3g Drum Break` is a procedural four-voice break kit. It does not load or
play samples: kick, snare, tom, and hi-hat hits are generated from oscillators, modal
resonators, filtered stochastic energy, envelopes, cross-band bleed, and a
shared room/character path.

- Canonical MIDI notes are 36 (kick), 38 (snare), 45 (tom), and 42 (hi-hat),
  with related GM notes mapped to the same families.
- NOTE TRACKING transposes after the incoming note has selected its drum family.
- Every voice has an independent output level and adjustable broad band-pass
  center for balancing and spectral isolation.
- BLEED couples neighboring spectral layers so the kit can sound like one
  recorded performance rather than four unrelated synthesizers. Upper-band
  bleed into the kick is deliberately restrained.
- AGE, ROOM, and the shared drum-character controls move continuously between
  open stereo session color and bandwidth-limited, reduced-resolution breaks.
- RANDOM changes safe timbral controls while preserving output gain, note
  tracking, velocity sensitivity, and MIDI receive routing.
- Version-3 states add per-voice level/band controls and retire the cymbal voice.
  Version-1 and version-2 Break states remain loadable; retired version-2 cymbal
  values are discarded instead of being reinterpreted as new kick controls.

The DSP class in `dsp/s3g_drum_break.h` has no CLAP dependency and is intended
for later direct integration with `s3g-tracker`.
