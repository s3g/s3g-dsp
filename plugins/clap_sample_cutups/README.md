# s3g Sample Cutups 2 / 32

Sample Cutups is a four-file instrument for rhythmic edits between source
files. It is a separate Sample-family instrument, while sharing the family’s
file loading, storage, voice, waveform, and multichannel routing conventions.

Each note starts a deterministic pattern of cuts. File Order chooses the lane;
Source Order chooses a region inside that lane. Regions can be equal divisions
or boundaries detected from transients when the file is loaded. The loader also
estimates a BPM for each file; Tempo Sync applies that rate against host tempo.

The clock can follow host divisions from whole notes through sixteenths and
triplets, or run freely in hertz. Swing, timing variation, repeat count, gate,
join crossfade, reverse probability, pitch variation, and level variation
shape the result. A 16-step manual pattern stores a lane and source position per
step.

- **Sample Cutups 2** accepts mono and stereo files and has a fixed stereo
  output.
- **Sample Cutups 32** accepts one- to sixteen-channel files and has a fixed
  32-channel output, with Preserve Field or Distribute routing and Note, Cut,
  or Pattern allocation cadence.

The full guide is in `docs/sample-cutups.html`.
