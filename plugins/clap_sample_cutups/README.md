# s3g Sample Cutups 2 / 32

Sample Cutups is a four-file instrument for rhythmic edits between source
files. It is a separate Sample-family instrument, while sharing the family’s
file loading, storage, voice, waveform, and multichannel routing conventions.

Each note starts a deterministic pattern of cuts. File Order chooses the lane;
Source Order chooses a region inside that lane. Regions can be equal divisions
or boundaries detected from transients when the file is loaded. The loader also
estimates a BPM for each file; Tempo Sync applies that rate against host tempo.
Transient Preroll can move detected boundaries up to 50 ms earlier, and the
selected lane can be reanalysed without decoding the file again. Waveforms show
equal divisions or detected transient boundaries according to Region Mode.
Steps / Regions is one shared musical length from 1 to 64: it sets both the
pattern length and the number of available equal or transient regions.
File Order contains every file-lane pattern: Down, Up, Palindrome, Pairs,
Outside In, Center Out, Stagger, Random, Random Cycle, and Manual. The Cut
Pattern graph previews the selected order; clicking it switches to Manual and
authors file lanes, while Option-drag authors Manual Source positions.

Poly Path relates notes two through four to the primary file path. Together
keeps the voices aligned, Step Offset advances each voice one step farther,
Quarter Spread distributes them around the order cycle, and Mirror Pairs
combines reverse and half-cycle relationships. The setting applies only in
Poly voice mode.

The clock can follow host divisions from whole notes through sixteenths and
triplets, or run freely in hertz. Swing, timing variation, repeat count, gate,
join crossfade, reverse probability, pitch variation, and level variation
shape the result. A manual pattern stores a lane and source position per step,
with as many as 64 authored steps.

- **Sample Cutups 2** accepts mono and stereo files and has a fixed stereo
  output.
- **Sample Cutups 32** accepts one- to sixteen-channel files and has a fixed
  32-channel output, with Preserve Field or Distribute routing and Note, Cut,
  or Pattern allocation cadence.

The full guide is in `docs/sample-cutups.html`.
