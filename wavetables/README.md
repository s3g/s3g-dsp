# Wavetables

`vot/` is the shared wavetable library for `s3g Ambi VOT Encoder 64`. Its WAV
files are 4 x 4 atlases containing sixteen consecutive single-cycle tables.
The library includes tonal, dynamic, glitch, codec, and vocal banks.

Load an atlas with the VOT Encoder's `LOAD` button. The selected USER atlas is
stored in CLAP project state, so the original WAV does not need to remain at
the same path after the REAPER project is saved.

The current Ambi Vox Encoder does not use this folder. Its loadable LPC-style
phrase examples live in `examples/ambi-vox-lpc/`.

Regenerate the complete library from the repository root:

```sh
python3 scripts/generate-vot-wavetables.py
```
