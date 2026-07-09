# 3OAFX Processing Workflow

The current CLAP 3OAFX path uses ordinary third-order `ACN/SN3D` plugin
inserts. Each 3OAFX effect decodes the input to the fixed 24-point virtual
speaker layer internally, applies one spatial effect and one AED return mask,
then encodes back to 3OA.

Current HTML docs:

- [`3oafx.html`](3oafx.html): Ambisonics overview
- [`3oafx-effects.html`](3oafx-effects.html): 3OAFX Delay, Pitch, Filter, and Gain
- [`3oafx-point-encoder.html`](3oafx-point-encoder.html): 16-point AED encoder
- [`3oafx-speaker-decoder.html`](3oafx-speaker-decoder.html): 1OA to 7OA speaker decoder
- [`3oafx-layout-panner.html`](3oafx-layout-panner.html): direct source-to-speaker panner
- [`ambisonic-rotate.html`](ambisonic-rotate.html): 1OA to 7OA ACN/SN3D field rotation utility
- [`ambisonic-order-band-tool.html`](ambisonic-order-band-tool.html): order-band gain and weighting utility
- [`ambisonic-stereo-decoder.html`](ambisonic-stereo-decoder.html): ACN/SN3D to true stereo decoder
- [`ambisonic-head-decoder.html`](ambisonic-head-decoder.html): synthetic binaural/transaural decoder
- [`ambisonic-energy-visualizer.html`](ambisonic-energy-visualizer.html): ambisonic field analyzer
- [`3oafx-insert-workflow.html`](3oafx-insert-workflow.html): current processing workflow

Typical signal path:

```text
3OA source
-> s3g 3OAFX Delay / Pitch / Filter / Gain
-> optional additional 3OAFX effects
-> Ambi Speaker Decoder, Ambi Stereo Decoder, or Ambi Head Decoder
```

Inside each 3OAFX effect:

```text
3OA input
-> decode to 24 virtual speaker points
-> dry copy path
-> one effect return and one AED return mask
-> mixer
-> encode back to 3OA output
```

The dry copy stays separate from the effect return until the mixer. This keeps
the reliable part of the JSFX send/return strategy while avoiding a 72-channel
boundary-pair workflow inside the CLAP package.

The earlier rack and boundary-pair CLAP experiments remain source checkpoints
only. They are not built, installed, or packaged in current pre-release builds.

The larger REAPER workflow remains documented in the
[s3g-mc 3OAFX process guide](https://s3g.github.io/s3g-mc/process-guides-3oafx.html).
