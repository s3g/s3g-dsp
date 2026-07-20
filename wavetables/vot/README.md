# s3g VOT Wavetable Atlases

These WAV files are 4 x 4 timbre atlases for `s3g Ambi VOT Encoder 64`.
Choose `LOAD` in the plugin, select an atlas, and the wave set changes to
`USER`. The loaded atlas is stored with the REAPER project.

Each file contains sixteen phase-coherent tables. Most atlases are RMS-matched;
the dynamic atlases intentionally vary energy across the grid. The first four
tables form the top row of the VOT display, followed row by row through the
bottom-right table. U moves left to right and V moves top to bottom.

## Tonal Atlases

| File | U | V |
| --- | --- | --- |
| `s3g-vot-foundation.wav` | harmonic density | odd/even balance |
| `s3g-vot-body-bass.wav` | brightness | body and asymmetry |
| `s3g-vot-pulse-reed.wav` | edge brightness | pulse width |
| `s3g-vot-formant.wav` | formant center | bandwidth and vowel |
| `s3g-vot-organ.wav` | registration | drawbar family |
| `s3g-vot-glass-metal.wav` | partial density | harmonic lattice |
| `s3g-vot-fold-phase.wav` | fold intensity | phase geometry |
| `s3g-vot-digital.wav` | sample resolution | amplitude resolution |
| `s3g-vot-string.wav` | excitation brightness | pluck and bow balance |

## Dynamic and Experimental Atlases

| File | U | V | Energy surface |
| --- | --- | --- | --- |
| `s3g-vot-rungler.wav` | feedback amount | register tap and seed | rises with feedback, about 11 dB |
| `s3g-vot-barberpole.wav` | cyclic spectral center | octave density | level matched |
| `s3g-vot-harmonic-mirage.wav` | consonance to cluster | registration and inversion | level matched |
| `s3g-vot-wave-terrain.wav` | traversal angle | terrain deformation | quiet center, loud perimeter |
| `s3g-vot-codec-ghost.wav` | prediction failure | packet block size | rises with failure, about 10 dB |
| `s3g-vot-codec-conceal.wav` | packet loss | concealment method | rises with loss, about 9 dB |
| `s3g-vot-codec-residual.wav` | residual emphasis | predictor memory | rises with residual pressure, about 9 dB |
| `s3g-vot-codec-residual-delta.wav` | difference emphasis | difference order | rises through first- to fourth-order differences, about 9 dB |
| `s3g-vot-codec-residual-comb.wav` | comb memory | predictor lag | rises with lagged residual memory, about 10 dB |
| `s3g-vot-codec-residual-feedback.wav` | feedback amount | feedback lag | rises through bounded recursive memory, about 12 dB |
| `s3g-vot-codec-cascade.wav` | transcode generations | packet block size | rises through collapse, about 12 dB |
| `s3g-vot-cellular.wav` | generation depth | rule family | rises with activity, about 11 dB |

These banks change how motion articulates the instrument. Rungler and Cellular
can grow from subdued patterns into forceful digital states. Wave Terrain acts
like an energy basin. The Codec family moves from predictor and stale-block
behavior into concealment, isolated residuals, finite-difference edges, comb
memories, bounded residual feedback, and repeated transcode collapse. The three
Residual variations preserve a clear cyclic pitch anchor while exposing
different structures inside prediction error. Barberpole wraps its spectral
center across the left and right edges so a looping vector path can suggest
continuous ascent or descent.

## Heavy Glitch Atlases

| File | U | V |
| --- | --- | --- |
| `s3g-vot-glitch-address.wav` | address damage | repeat and reorder mode |
| `s3g-vot-glitch-pcm.wav` | data corruption | bit-pattern family |
| `s3g-vot-glitch-fracture.wav` | fracture depth | polarity and dropout mode |

The glitch banks contain intentional discontinuities and cyclic data-like
material. VOT derives pitch-dependent band-limited tables when an atlas is
loaded, allowing the damage to remain forceful without using the same bright
table at every MIDI register.

## Vocal Atlases

| File | U | V |
| --- | --- | --- |
| `s3g-vot-vocal-blackmetal.wav` | throat closure and distortion edge | vowel darkness and rasp depth |
| `s3g-vot-vocal-throat.wav` | closure | subharmonic body |
| `s3g-vot-vocal-choir.wav` | strain | vowel family |
| `s3g-vot-vocal-animal.wav` | snarl | body size |

These banks provide vocal and formant-adjacent USER material for VOT. They use
the same validated 4 x 4 atlas contract as the rest of the library. Ambi Vox
Encoder uses LPC-style phrase frames instead of these WAV files.

## Atlas Format

The canonical format is mono 24-bit PCM WAV at 48 kHz with exactly 4096
samples: sixteen consecutive 256-sample tables. The declared sample rate does
not set oscillator pitch.

Neighboring cells should share fundamental phase and polarity. Remove DC,
keep levels comparable, and treat the waveform as one periodic cycle. Other
WAV lengths remain supported as source recordings: the plugin divides them
into sixteen equal regions and derives a cyclic table from each region.

Run `python3 scripts/generate-vot-wavetables.py` from the repository root to
regenerate the WAV files, manifest, and SVG previews.
