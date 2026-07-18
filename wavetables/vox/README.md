# s3g VOX Wavetable Atlases

These WAV files are 4 x 4 vocal-source atlases for `s3g Ambi Vox Encoder 64`.
They use the same single-cycle atlas format as the VOT library: mono 24-bit PCM
at 48 kHz, sixteen consecutive 256-sample tables.

The first four tables form the top row, followed row by row through the
bottom-right table. U moves left to right and V moves top to bottom.

| File | U | V |
| --- | --- | --- |
| `s3g-vox-blackmetal.wav` | throat closure and distortion edge | vowel darkness and rasp depth |
| `s3g-vox-throat.wav` | closure | subharmonic body |
| `s3g-vox-choir.wav` | strain | vowel family |
| `s3g-vox-animal.wav` | snarl | body size |

The tables provide the pitched body of the sound. Rasp, breath, and unstable
vocal behavior should be added in the Ambi Vox DSP layer rather than baked
entirely into the atlas.

Run `python3 scripts/generate-vox-wavetables.py` from the repository root to
regenerate the WAV files, manifest, and SVG previews.
