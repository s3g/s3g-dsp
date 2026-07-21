# s3g Demo Synthetic Voicebank

This is a small, synthetic UTAU-style bank for testing the Ambi Vox Encoder
loader, phrase mapping, timing, and multivoice playback. It is useful for a
first-run smoke test, but it is not intended as a polished singing voice.

Load this folder with the `LOAD` button in the plugin's `PHRASE` panel. The
plugin reads the WAV aliases and `oto.ini`, then creates a WORLD analysis cache
in the user's macOS cache directory.

Contents:

- `oto.ini`: UTAU-style alias and timing records.
- `s3g-pronunciations.txt`: example phrase-to-alias overrides.
- `voicebank.json`: source and slicing metadata from the s3g voicebank builder.
- `*.wav`: synthetic vowel and consonant-vowel aliases.

The bank was generated from the repository's synthetic demo source with
`tools/voicebank_builder.py`. Replace it with a properly recorded and timed
voicebank for intelligible or performance-ready results.
