# Ambi Vox LPC Loader Examples

These are synthetic, provenance-clean test phrases for `s3g Ambi Vox Encoder 64`.

Use the `LOAD` button in the plugin's `PHRASE` panel and choose one of the `.hex` files in this folder. The current loader treats each 4-byte group as one internal LPC-style frame:

- byte 1: energy and pitch high bits
- byte 2: pitch low bits and formant/coefficient A
- byte 3: formant/coefficient B and C high bits
- byte 4: C low bits and flags

This is not a bit-exact Speak & Spell/TMS5220 ROM format yet. These files are starter materials for the new encoded-frame path.

## Design Tool

Generate more files with:

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
