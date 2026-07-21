# Legacy Ambi Vox LPC Research Fixtures

These synthetic phrase-frame files preserve the earlier LPC design experiment.
The current `s3g Ambi Vox Encoder 64` uses WORLD analysis and UTAU-style
voicebanks; its `LOAD` button does not accept these `.hex` files.

The experimental frame representation treated each 4-byte group as one
internal LPC-style frame:

- byte 1: energy and pitch high bits
- byte 2: pitch low bits and formant/coefficient A
- byte 3: formant/coefficient B and C high bits
- byte 4: C low bits and flags

This is not a bit-exact Speak & Spell/TMS5220 ROM format. The files remain in
the source tree as historical DSP research material and are not shipped in the
macOS CLAP package.

## Design Tool

Regenerate or extend the research fixtures with:

```sh
python3 scripts/design-ambi-vox-lpc.py
```

By default this writes a batch to `examples/ambi-vox-lpc/generated` using these voice recipes:

- `clear`
- `speakspell`
- `blackmetal`
- `demon`
- `choir`
- `whisper`
- `robot`

Custom example:

```sh
python3 scripts/design-ambi-vox-lpc.py --phrase "black metal voice" --recipe blackmetal --recipe whisper
```
