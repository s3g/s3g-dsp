# Examples

## Ambi Vox Demo Voicebank

`voicebanks/s3g-demo-synthetic/` is the small synthetic voicebank included with
pre-release packages. Load that folder from the Ambi Vox Encoder `PHRASE`
panel to audition phrase mapping without preparing a bank first.

`voicebank-builder/` contains source and phoneme-list material used with
`tools/voicebank_builder.py`. See the demo bank README for its format and
limitations.

## Legacy LPC Research

`ambi-vox-lpc/` preserves synthetic frame files and the design script from the
earlier LPC experiment. The current WORLD/voicebank Ambi Vox Encoder does not
load these `.hex` files, and they are not included in binary packages.

The VOT wavetable library is maintained separately in `wavetables/vot/`.
