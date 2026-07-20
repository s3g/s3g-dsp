# Imprint Atlas

The Imprint Atlas contains deterministic spaces authored by `s3g-mc Imprint
Sketch` for `s3g Ambi Imprint 64`.

Five entries exercise geometry-derived flutter, axial, coupled-chamber,
circulating, and irregular relay echo structures. Their paths are resolved into
the same directional reflection events used by user-authored imprints.

- Root `.s3gimprint` files are packaged with the CLAP plugin.
- `projects/*.json` files are editable `s3g-imprint-sketch` projects.
- `manifest.json` records names, categories, descriptions, seeds, families,
  durations, echo structures, and source project paths.
- `scripts/generate-imprint-atlas.mjs` regenerates and validates the atlas by
  running the sibling Imprint Sketch authoring engine in headless Chrome.

The runtime files and editable projects are paired outputs from the same
settings. Do not edit generated `.s3gimprint` files independently.
