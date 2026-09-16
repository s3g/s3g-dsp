# Custom dropdown parity

The reference is `s3g_cocoa_gui.h::drawDropdownMenu`, not the platform's
native option-menu appearance. Keep the original item ordering, hit rectangles,
row heights, menu position, multi-column ordering and parameter mapping.

Required visual elements:

- Dark outer backing and visible menu border.
- Inset selected/hover fills and the left selection/hover marker.
- Alternating row backgrounds.
- A thin, solid horizontal separator between **every** adjacent item, including
  selected and hovered items. No internal separator above the first row of a
  column. Separators are drawn after row fills and before text.
- Existing Fira Code typography and family text alignment.

## September 2026 regression repair

`s3g_ambi_effect_drawing.h` had reduced the menu to a flat background and hover
fill, dropping the row separators and other original menu framing. Restoring
that shared painter covers the literal Ambi Effect, Decoder, Input Encoder and
Environmental Encoder ports, as well as future editors using that painter.

The earlier Macro Shred/Fracture, Point Encoder and Stochastic Encoder painters
also lacked separators. They now call the same
`foundation::drawDropdownItemSeparator` primitive. It draws a one-logical-pixel
solid rule without inheriting a plot's stroke width or dash pattern, and restores
the drawing state afterward. Their established palette conversion is preserved.

The generic canvas popup, Sample Player, Sample Family and Matrix Upmix painters
already draw item separators; their working menu behavior was left intact.

## Regression checks

`tests/vstgui_dropdown_smoke.cpp` renders the actual shared painter and decoder
forwarder at 65%, 100%, 125%, 150% and 200%, with each possible hovered row and
without hover. It checks every internal rule against a no-rule negative control
at three positions across its width, including selected/hover boundaries. It
also checks first-row behavior, stroke-state restoration and row hit testing.

```sh
cmake --build build-clap-sample-vstgui-fidelity --target s3g_vstgui_dropdown_smoke
ctest --test-dir build-clap-sample-vstgui-fidelity \
  -R '^s3g_vstgui_dropdown_smoke$' --output-on-failure
```

Set `S3G_DROPDOWN_CAPTURE_DIR` to a writable directory to save PNG references.
On macOS the native render tests need window-server access.

Verified on 2026-09-11: all 60 shared/decoder menu renders, Wind and Adaptive
Decoder canvas interaction suites, and native CLAP host checks for Wind, Macro
Shred, Point and Stochastic passed. Those four plugin targets also rebuilt for
macOS arm64 and Windows x64. Actual Windows host rendering remains a manual
acceptance check.

The generic native host's queued-edit test now shows portable editors and
scales its mouse coordinates to their current size. Its previous unscaled
click failed on both the unchanged installed Wind and the rebuilt Wind; the
corrected test verifies the real begin/value/end sequence without skipping it.

## Complete rollout

`scripts/clap-vstgui-menu-rollout.tsv` lists the 36 affected binary variants:
the three terrain/map encoders, the five environmental encoders, four input
encoders, Point/Stochastic, six decoders, eight Ambi Effects, and eight Macro
Shred/Fracture/Pitch variants. Unaffected generic/Sample/Upmix painters are not
replaced merely because they also have dropdowns.

All 36 were rebuilt for macOS arm64 and Windows x64. The 27 native canvas/menu
suites passed (8 Ambi Effects, 6 decoders, 4 input encoders, 5 environmental
encoders, 3 terrain/map encoders and the separator render test). Separate native
CLAP host checks passed for all 10 standalone Point/Stochastic/Macro variants.
The final isolated CLAP validator sweep passed for all 36 bundles. It caught a
Cartography snapshot-padding serialization mismatch, now covered by explicit
byte-reproducibility and poisoned-padding tests; its retained state format and
DSP output remain unchanged.
See `MAP_ENCODERS_VSTGUI_PARITY.md` for the three completed terrain/map ports.

Installed 2026-09-11: all 36 canonical Mac bundles, with pre-update copies in
`CLAP Backups/menu-parity-20260911.UCLK50`. Installed signatures and file contents
matched the validated build sources. The final Windows package is
`dist/s3g-dsp-windows-vstgui-menu-parity-x64-20260911-120755.zip`; its complete
36-plugin manifest, archive integrity and SHA-256 sums were verified.

```sh
cmake -P scripts/package-windows-vstgui-menu-rollout.cmake
```

For full-collection Mac installation, follow the maintained
[local-build installation guide](../../docs/building-from-source.html#install-local).
Back up existing bundles before replacing them and preview the changes with
`--dry-run`. The family/menu manifests remain inputs to the Windows packagers;
building or packaging does not install plug-ins.

Windows packaging reads the same 36-plugin manifest, checks PE x64/CLAP exports
and static MinGW runtime linkage, strips only staged copies, merges all runtime
resources with conflict checks (including Ray Atlas), and includes font/VSTGUI
licenses, a manual test checklist and SHA-256 sums. Existing older Windows
packages are not silently overwritten. Runtime acceptance on Windows still
requires REAPER on the separate Windows machine.
