# Ambi Ray Encoder Atlas

This directory contains the bundled Ambi Ray Encoder space atlas.

- `*.s3gray` files are runtime fields loaded by the plugin.
- `projects/*.json` files are editable Ray Sketch projects retained for
  regeneration and inspection; they are not packaged in the plugin bundle.
- `manifest.json` records provenance and generated field statistics.
- `scripts/generate-ray-atlas.mjs` regenerates and validates the atlas through
  the actual Ray Sketch authoring engine.
- `scripts/check-ray-atlas.mjs` validates the checked-in atlas without launching
  the authoring utility.

The atlas shares its named spatial designs with the Ambi Imprint Atlas, while
the exported data is Ray-specific: sampled source cells, stable reflection
slots, bounce positions, and late profiles.
