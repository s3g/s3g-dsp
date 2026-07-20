# s3g Voicebank Builder

This folder contains a starter phoneme script for `tools/voicebank_builder.py`.

Record one WAV with the phonemes in `phonemes.txt` in order, leaving a small gap
between each syllable. Then run:

```sh
python3 tools/voicebank_builder.py my-recording.wav \
  --phonemes examples/voicebank-builder/phonemes.txt \
  --name my_voice \
  --output examples/voicebanks/my_voice
```

The tool writes sliced WAV files, `voicebank.json`, and `oto.ini`. The automatic
slicer is intentionally simple; use `--markers markers.csv` for hand-corrected
boundaries with rows in this form:

```csv
a,120,520
e,620,1010
```
