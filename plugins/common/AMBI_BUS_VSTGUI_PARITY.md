# Ambisonic bus and transform VSTGUI parity

Scope: ten editors, with Cocoa retained as a comparison build. No DSP algorithm,
CLAP identifier, parameter identifier/range, bus width, or state version changed.
macOS selection: `S3G_ENABLE_AMBI_BUS_VSTGUI_ON_MACOS=ON`. Windows selects these
editors when the portable VSTGUI foundation is available.

## Reference inventory

| Editors | Preserved behavior and graphics |
| --- | --- |
| Ambi Matrix Group 64 / 128 | Fixed four/eight 16-channel groups; every matrix cell, ceiling/live fills, six motion shapes, FREE/SYNC, transport preview phase, original random seeds/distribution, deviation control, three-column collapsible glossary. The four-group flow plot retains its different 44-point top/bottom margins. |
| Ambi Mixer Node Bus 128 | Eight lane-locked 3OA feeds, input-range readouts, all node/cursor controls, radius/overlap rings, active selection, top/side/3/4 views, shift-drag camera, zoom, LOCK Z, and two-column gain/peak meters. No ordinary speaker-bed menus or node meshes are added. |
| Ambi Transform Rot 64 | Original cube vertices/edges, deformation and projection equations, axes, camera presets/free rotation, order menu, seven rotation/relationship controls. |
| Ambi Transform Grp Rot 64 / 128 | Original four/eight field points and connections, per-group transformation equations/readouts, camera controls, coherent 16-channel fields. |
| Ambi Transform Depth 16 / Grp Depth 64 / 128 | Exact near/far group-position mapping, spread wobble, air haze curves/dashes, tail ellipses and depth ticks. The single-field editor keeps its separate TAIL/SIZE/DECAY/DAMPING environment panel. |
| Ambi Transform Order Band 64 | Eight weighted order bars, active-order cutoff, four weighting choices, amount and all eight gain trims. |

Sources: the corresponding retained Cocoa `drawRect` and pointer handlers;
`docs/ambisonic-bus-processors.html`, `docs/ambi-transform-rot.html`,
`docs/ambi-transform-order-band.html`, and `docs/ambisonic-utilities.html`.
Layout rectangles come from `s3g_gui_layout.h`, not a new layout generator.

## Foundation and ownership

- Shared CLAP lifecycle, proportional 65–200% resizing, Fira Code/fallback,
  resource lookup, menus, sliders, aligned text and UTF-8 file dialogs.
- Existing state codecs and LOAD/SAVE/INIT behavior; preset recall preserves OUT.
  Rotation presets additionally preserve the existing saved camera-mode field.
- Rotation retains its existing atomic parameter publication/audio-owned DSP.
  Matrix/Depth/Order use an atomic parameter mirror, with audio-side application.
  The GUI never draws from the live processor. Depth visualization calls the
  same pure group-state function as the DSP.
- GUI event queues notify the host without replaying older GUI values over
  newer host automation. State-save uses a private candidate; state-load decodes
  completely before publishing. Tail queries read published parameters.
- Ambi Node Bus shares the corrected versioned parameter banks and unpublished-
  bank guards with ordinary Node Bus, including the concurrent cursor regression.
- Windows modules export `clap_entry` and use the common resource packaging helper.

## Visual adaptation notes

The comparison is against actual rendered Cocoa references, not just drawing
intent: `NSFrameRect` uses the current **fill** color. This means the field and
matrix-cell boxes have no contrasting outline, while gain/peak meters and order
bars do. These differences are preserved. Air-haze dash lengths are converted
from absolute Cocoa points to VSTGUI's stroke-width units; Windows dash offset
uses stroke-width units as well.

The family-wide Fira Code typography and centered control text/handles are
intentional shared-foundation changes. The Matrix deviation label remains fully
readable rather than retaining the Cocoa label/track overlap. The original
four-group rotation readout's overlapping pairs remain unchanged in this strict
adaptation; no new arrangement or visualization has been substituted.

## Verification

- Ten source-level `ambi_bus` canvas tests: every parameter, real menu/slider
  interactions, all matrix cells, Cocoa randomization/projection comparisons,
  node selection/locking, camera recall, depth/order formulas, Unicode presets,
  balanced gestures/backpressure and float/double audio. Legacy Rotate v1/v2,
  Group Rotate v1 and Depth v1/v2 recall are included.
- The shared Node Bus regression runs actual audio concurrently with repeated
  cursor click/drag/wheel operations in all views. Pixel checks assert visible
  Ambi gain and peak meter outlines even at zero signal.
- `s3g_ambi_bus_clap_parity_smoke cocoa-binary vstgui-binary plugin-id` compares
  separately built modules: parameter metadata, state loading in both directions,
  partial stream reads/writes, transactional rejection of truncated state and
  float/double output under multiple scenes and automation bursts (tolerance
  1e-6). It does not create GUIs; duplicate retained Objective-C class warnings
  are expected when both builds are loaded in this diagnostic process.
- Native Cocoa and VSTGUI lifecycle/reference captures for all ten. Documentation
  scenes include non-default depth, environment and ambisonic cursor settings.
- Ordinary Matrix 32/64 and Node Bus canvas regressions; Ambi Group Rotate DSP
  equivalence; CLAP validator for all ten; Mac signing/font checks and Windows
  x64 module/export/dependency inspection.

Reference artifacts are under `ambi-bus-reference/` in the Mac VSTGUI and Cocoa
build directories. Native Windows/REAPER validation is still a separate device
test. This migration does not install plugins or create a distribution archive.
