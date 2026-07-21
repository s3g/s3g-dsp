# s3g Vox Builder Example

This folder contains a starter source recording and phoneme order for
`s3g Vox Builder`.

Build and open the macOS app:

```sh
cmake --preset apps
cmake --build --preset apps
open "build-apps/apps/vox_builder/s3g Vox Builder.app"
```

Drop `s3g_demo_voice_source.wav` into the waveform area. The app reads the
aliases from its editor, detects the 35 regions, analyzes each region with
WORLD, and lets you drag boundaries or edit timing before export.

You can also drop `examples/voicebanks/s3g-demo-synthetic` as a folder. Its WAV
files are naturally sorted and imported as one exact segment per filename.
The app level-matches those files before WORLD analysis and reports filename or
ordered-inventory alias confidence. Use the Segment header `+` and `-` controls
when the automatic segment count needs correction.

For a new bank, record one WAV with the aliases in `phonemes.txt` in order and
leave a small gap between syllables.

The command-line builder remains useful for batch work:

```sh
python3 tools/voicebank_builder.py my-recording.wav \
  --phonemes examples/voicebank-builder/phonemes.txt \
  --name my_voice \
  --output examples/voicebanks/my_voice
```

Use `--markers markers.csv` for hand-corrected boundaries with rows in this
form:

```csv
a,120,520
e,620,1010
```
