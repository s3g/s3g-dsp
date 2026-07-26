# s3g-dsp GUI Layout Templates

This document is the structural companion to the visual
[GUI Style Guide](gui-style-guide.md). The style guide defines how controls
look and behave; this document defines where shared classes of controls live.

The templates are layout contracts, not complete plugin skins. A plugin keeps
its own primary visual and effect-specific controls, but panel roles, control
order, column geometry, and ordinary row alignment follow its class template.

## Global Contract

Every custom plugin GUI follows these rules:

- The title band holds the full plugin name, preset controls, actions, and a
  compact far-right status such as `PK`.
- The title band has no standalone underline. Every toolbox and primary
  field/view draws the same two-pixel accent line on its own top edge, so the
  content regions provide the visual separation without a competing rule.
- The first toolbox or primary field/view begins at y 42 px. This leaves the
  same 11 px clear band below the 18 px title controls, whose lower edge is
  y 31 px. Editors do not move this top edge upward to recover canvas space.
- A channel count appears at the end of the plugin name only when it
  distinguishes a meaningful variant. Encoder names omit `64` because
  64-channel support is universal across that family.
- Encoder title actions always read `PRESET`, `LOAD`, `SAVE`, `RANDOM`, from
  left to right. `LOAD` and `SAVE` in this strip operate on the complete plugin
  state; media, atlas, map, capture, and selected-object actions stay in their
  contextual panels.
- The primary visual gets the largest uninterrupted region.
- Parameter toolboxes begin at the same top origin within a family.
- Shared controls occupy family slots. A plugin-specific control does not move
  a shared control merely because it would otherwise read more naturally first.
- Shared toolbox interiors use the same row order within a family. Unsupported
  trailing controls are omitted; controls are not shifted around an unsupported
  shared control in the middle of the sequence.
- The first parameter column begins with `OUTPUT`. `OUT` is its first row.
- A single undivided parameter stack also begins with `OUT`.
- `OUTPUT` contains only final-audition controls: output level, final wet/dry
  mix, limiter or safety, final bypass, monitor selection, and genuinely
  final cleanup.
- Source coloration, generator feedback, field listening, capture, motion,
  topology, and projection do not belong in `OUTPUT`.
- Ordinary toolbox rows use a 26 px pitch. A 24 px compact pitch must be
  declared explicitly and is reserved for a genuinely dense panel.
- The first ordinary control baseline is always 36 px below its toolbox top,
  including panels with tabs, header actions, or alternate modes.
- A contextual toolbox is sized from its visible rows. Hidden or retired
  controls do not reserve rows, and an absent conditional toolbox does not
  reserve a panel-sized hole: the next visible toolbox follows at the standard
  12 px stacked-panel gap.
- Visible contextual rows are packed into contiguous row indices. Drawing,
  menu origins, dropdown placement, and hit testing all consume the same
  effective row coordinate.
- Space below the final row must have a named use such as a preview, capture
  surface, meter, status block, listener controls, or contextual actions.
  Unlabelled space left by deleted or hidden controls is removed.
- Draw geometry, menu geometry, hit rectangles, and dropdown origins come from
  the same panel/row declaration.
- Stable CLAP parameter IDs never change to match GUI order. Mirrored controls
  use the same ID, range, display formatter, default, and automation path.
- Slider value text is right-aligned in a bounded value cell. The renderer
  removes unnecessary numeric precision before clipping, so data never crosses
  its panel border.

## Encoder Title Band

Every encoder and procedural generator provides a preset selector followed by
`LOAD`, `SAVE`, and `RANDOM` in the top portion of the editor. The shared
contract has three canvas profiles:

| Profile | Preset label x | Preset menu | Load | Save | Random |
| --- | ---: | --- | --- | --- | --- |
| Compact, below 1000 px | 238 | x 300, w 148 | x 456, w 44 | x 508, w 44 | x 560, w 62 |
| Medium, 1000–1099 px | 280 | x 342, w 170 | x 520, w 48 | x 576, w 48 | x 632, w 66 |
| Wide, 1100 px and above | 320 | x 382, w 190 | x 580, w 48 | x 636, w 48 | x 692, w 66 |

All controls use y 13 and height 15; the plugin title baseline is y 14. The
`PRESET` caption uses the shared title-menu baseline at y 15 so it aligns with
the selection and `LOAD`, `SAVE`, and `RANDOM` text rather than with ordinary
toolbox labels. Every family member calls the shared title-band or title-preset
renderer for this control. Status is right-aligned with an 18 px inset. A
plugin may expose only a single factory choice such as `INIT`, but the preset
field remains in the family slot.

### Global and contextual actions

The title strip is deliberately narrow in meaning:

- `PRESET` selects a complete factory or user state.
- `LOAD` and `SAVE` read or write a complete plugin state preset.
- `RANDOM` creates a safe generative variation and marks the state `RANDOM`.
- Source files, ray fields, atlases, JSON/SVG paths, wave tables, captured
  material, mutate/grow/reseed actions, and selected-object edits remain in the
  panel that owns them.

Randomization preserves the performer’s audition frame: output level,
ambisonic order, external media references, listener/monitor configuration,
and GUI page/camera state. It may vary sound generation, timing, envelopes,
internal topology, source projection, and motion within safe ranges.

## Shared Encoder Control Families

Within an encoder family, controls use these semantic sequences. Unsupported
controls are omitted without changing the order of the remaining shared items.

1. `ENGINE`: `ORDER`, mode/trigger, voices/objects, source selection.
2. `TUNING`: base/root, scale, tune, spread, detune.
3. `ENVELOPE`: attack, decay, sustain, release, then shape/window.
4. `PROJECTION`: azimuth, elevation, distance, width/spread, follow/inertia.
5. `MOTION`: mode/scene, clock/sync, rate, amount/depth, deviation/chaos,
   then axis-specific controls.
6. `LISTENER`: enable, pickup set, listening mode, amount/return,
   response/bypass.
7. `ENVIRONMENT`: place/type, size, decay, damping, air.
8. Global preset actions stay in the title band; contextual actions stay with
   their owning source, capture, editor, or selected-object panel.

`TOPOLOGY`, when present, is the first panel in the second parameter column.
Projection follows topology, then motion. Tuning and envelope groups keep
their complete family order even when the plugin uses unusual synthesis or
spatial terminology.

The encoder-family release audit uses an explicit membership list rather than
inferring membership from a legacy display name. It currently covers 16
editors: Point, Cloud, Path, Surface Terrain, Ray, Ray Bilocation, Insect,
Neural Ecology, Pulsar, Stochastic, VOT, Vox, Water, Wave Terrain, Wind, and
Wrangler. Renaming a member cannot silently remove it from title-action,
host-name, output-placement, render, or responsive-GUI checks.

## Plugin Classes

| Class | First parameter column | Second parameter column |
| --- | --- | --- |
| Procedural encoder/generator | `OUTPUT`, `ENGINE`/`SOURCE`, `TUNING`, event/timing, tone/shape, `ENVELOPE` | `TOPOLOGY` when present, `PROJECTION`, motion, selection, event/timing, `LISTENER`, `CAPTURE` |
| Effect/processor | `OUTPUT`, `ENGINE`, modulation, tone/shape, timing | `RELATIONSHIPS`, routing, diagnostics, utilities |
| Macro effect | `OUTPUT`, `ENGINE`, `RELATIONSHIPS` | family preview, diagnostics, utilities |
| Spatial panner/decoder | `OUTPUT`/monitor, layout/decoder, selected source or speaker, routing | projection, listening, diagnostics, utilities |
| Mixer/matrix/lane tool | `OUTPUT`, global routing, lane controls | selected-object controls, diagnostics, utilities |
| Compact utility | One stack: `OUTPUT`, processing controls, utilities | Optional only when required |
| Compact effect | `OUTPUT`, effect engine, tone/timing | modulation or relationships |
| Analyzer/monitor | Toolbar, then the primary meter/view | Optional compact display controls |
| Output utility | Primary routing/field view | `OUTPUT`, routing, processing |

Panels that do not apply are omitted; they do not leave empty placeholders.
Effect-specific panels may be inserted after the closest semantic role while
preserving `OUTPUT` first and keeping final-audition controls separate.
Conditional family anchors are different from placeholders: when an anchored
shared panel exists, it occupies its family location; when it does not exist,
the next applicable panel may use that space.

## Compact Effect Family Reference

The compact Effect family covers Spectral Spray 2ch/8ch, Shard Scatter,
Orbit Delay, and Cascade Taps. Host names begin with `s3g Effect`; a channel
suffix remains only where it distinguishes installable variants.

All five use the shared 760 × 376 responsive canvas and two-column grid:
x 18 / width 352 and x 388 / width 354, both beginning at y 42. `OUTPUT` is
the first panel and `OUT` is row 0. The remaining effect controls are grouped
by engine, timing, tone, modulation, or relationships and packed with the
standard 12 px panel gap. A one-off historical y coordinate may not replace
the shared panel and row declaration.

The title order is `PRESET`, `LOAD`, `SAVE`; compact effects do not receive a
universal `RANDOM` action. Every continuous slider resets to its CLAP default
on double-click, values remain inside the bounded right cell, and each editor
uses the responsive viewport.

## 3OAFX Family Reference

3OAFX host names keep `s3g 3OAFX` first and use the second word to identify
the workflow: `Effect` for Delay, Pitch, Filter, and Gain; `Transform` for
Displacement. Stable IDs and bundle filenames remain unchanged.

The four single effects use a shared 880 × 500 layout: a primary field at
x 18 / y 42 and a right parameter column at x 518 / y 42. `OUTPUT` is first,
followed by the effect engine and return-mask/projection controls.
Displacement uses the 920 × 610 spatial variant, with its field at x 18 /
y 42 and its x 648 parameter column beginning with `OUTPUT`; score playback,
warp, and distance panels follow in that order.

All 3OAFX titles use `PRESET`, `LOAD`, and `SAVE`, with contextual score/file
loading retained in the panel that owns it. Sliders use shared 26 px rows and
double-click defaults. Menus and contextual dropdowns consume the same panel
geometry used for drawing and hit testing.

## Analyzer Family Reference

The Analyzer family covers Meter 64ch and Ambi Energy 64ch. Both are
signal-transparent and expose the sortable `s3g Analyzer` host prefix.
Their title order is `PRESET`, `LOAD`, `SAVE`; the main meter or energy field
begins at y 86 after a common y 42 toolbar.

Analyzer editors may resize their visualization dynamically rather than use a
fixed aspect viewport. Their minimum content insets, toolbar height, bottom
clearance, title typography, and control baselines remain shared. Continuous
display controls still reset to their declared default on double-click.

## Output Utility Family Reference

The Output family covers Autogain Stereo, Autogain Quad, and Crossover. These
tools exist for final audition, fold-down, crossover, or delivery preparation,
so their host names begin with `s3g Output`.

All three use the shared 920 × 560 responsive canvas. The primary routing or
speaker field occupies x 18 / y 42 / width 560. The parameter column begins at
x 596 / y 42 with `OUTPUT`; routing and processing panels follow at the
standard 12 px gap. Autogain puts `OUT` first. Crossover has no single master
gain, so its declared output exception begins with the two actual final stages,
`SUB` and `HIGH`, followed by final `BYP` and bypass-fold `FOLD` audition
controls; it does not invent a redundant master parameter.

The title order is `PRESET`, `LOAD`, `SAVE`, with `PK` or a compact operating
summary at far right. All sliders use bounded values, shared row geometry, and
double-click defaults.

## Ambi Imprint Reference

Ambi Imprint is a developed loaded-resource processor and therefore sorts as
`s3g Processor Ambi Imprint 64ch`, alongside Ambi Grain rather than under a
one-member Imprint family.

Its shared 900 × 480 responsive layout places the field at x 18 / y 42 and the
parameter column at x 616. `OUTPUT` is first with `OUT` then `BYP`; the
contextual `IMPRINT` file/atlas panel follows, then `PROCESS` with `ORDER`,
`MIX`, `FOC`, `WID`, and listener mode. Title `LOAD` and `SAVE` operate on
complete plugin state. The separate `LOAD` inside `IMPRINT` remains the
contextual `.s3gimprint` action.

## Macro Family Reference

The Macro family is an explicit seven-editor release group: Delay 8ch/24ch,
Pitch 8ch/24ch, and Shred Mono/8ch/24ch. Channel suffixes remain in these
titles because they distinguish installable processing variants.

Every multichannel Macro uses the same 760 px-wide two-column grid: the first
column is x 18 / width 352 and the second is x 388 / width 354. Delay and
Pitch use a 760 × 496 canvas:

- `OUTPUT` is first at x 18, y 42. `OUT` is row 0 at y 78 and `MIX` is row 1
  at y 104.
- `ENGINE` follows at x 18, y 134. Its height is fitted to the actual engine
  row count.
- `RELATIONSHIPS` remains in the first semantic stack after `ENGINE`, at the
  standard 12 px gap; its shared five-row order is `SPRD`, `DEV`, `SKW`,
  `CTR`, `GLD`.
- The complete second column is the named lane-relationship preview. This
  panel has the explicit `LaneRelationships` layout role and never moves into
  the first-column parameter stack.

Multichannel Shred extends that exact grid to a 760 × 620 canvas. Its
eight-row `ENGINE` requires `RELATIONSHIPS` to begin at y 382. The lane preview
remains at the top of the shared second column and the feedback-containment
field follows it. The containment toolbox must visibly contain its activity
meter, feedback field, and `PANIC` action; it is not a spacer.

Shred Mono is the declared compact exception. It remains a 416 px single
column and omits channel-only relationship and lane-preview panels. To fit the
full title, far-right `PK`, and `PRESET`/`LOAD`/`SAVE` without clipping, title
actions use a second header row and `OUTPUT` begins at y 68. Its content stack
is `OUTPUT`, `ENGINE`, then `CONTAINMENT`, with the normal 12 px panel gaps.
No Macro editor exposes `RANDOM`.

All Macro slider rows use the shared 16 px label inset, 108 px control inset,
bounded 42 px right-value cell, 26 px pitch, and 18 px bottom clearance.
The value cell ends 16 px before its toolbox's right edge; units such as `%`,
`dB`, `ms`, `ct`, and `st` are clipped or precision-reduced inside that cell
and may never paint beyond the toolbox frame.
Double-click restores the declared CLAP default. Draw and hit geometry come
from the same family panel declarations, and every Macro editor uses the
shared responsive viewport.

For every multichannel Macro, the complete distinguishing suffix (`8CH` or
`24CH`) is part of the left title string. The far-right status contains only
`PK` and its value; channel-count text is forbidden there.

## Array Utility Family Reference

The Array family covers HPF, Delay, and Trim in their 16-, 26-, 32-, and
64-channel variants. All twelve bundles consume the same 720 × 388 responsive
layout and shared title contract.

The title contains the full channel-specific Array name, followed by
`PRESET`, `LOAD`, and `SAVE`; Array utilities do not expose `RANDOM`. `PK` is
the only far-right status. The channel count remains in the title because it
distinguishes separately installable routing variants.

The common top grid begins at y 42:

- `OUTPUT` is at x 18, width 332. `OUT` is row 0 and final `BYPASS` is row 1.
- `ARRAY` is at x 362, width 340. Integer `ACTIVE` is row 0.
- The plugin-specific editor spans x 18 through x 702 at y 134. HPF uses this
  region for its response field and filter controls. Delay and Trim use the
  same eight-row channel-page geometry, navigation positions, value column,
  and 26 px row pitch.

Every continuous Array slider, including each channel Delay or Trim row,
double-clicks to its declared CLAP default through the normal parameter-update
path. Draw, text-entry, drag, and reset geometry share the family layout
declaration. Value text is bounded and loses unnecessary precision before it
can cross a panel edge.

## Ambi Transform Family Reference

The Transform family covers Rotate 64, Group Rotate 64/128, Depth 16, Group
Depth 64/128, and Order Band 64. Host names begin with `s3g Ambi Transform`
so the complete family sorts together, and meaningful channel counts remain
at the end of each name. The host browser uses `Rot` and `Grp` where needed
to keep long variants scannable; editor titles retain `ROTATE` and `GROUP`.

All seven bundles use the shared responsive 820 × 496 canvas. The title
contains the complete channel-specific name, `PRESET`, `LOAD`, and `SAVE`;
Transform utilities do not expose title-bar `RANDOM`. `PK` is the only
far-right status. Group-count and ambisonic-order explanations belong in the
field or toolboxes rather than beside `PK`.
The title band reserves enough name width for the longest 128-channel member
before the consistently aligned `PRESET` label.

The common layout begins at y 42:

- The spatial or weighting field occupies x 18, width 506, and reaches the
  common bottom at y 478.
- `OUTPUT` occupies x 536, width 266, with `OUT` in row 0. A variable ORDER
  parameter is row 1; fixed-order group variants use their final WIDTH control
  there instead.
- Transform-specific toolboxes begin at x 536 / y 134. They use the normal
  26 px row pitch, 16 px label inset, 108 px control inset, bounded value cell,
  and content-fitted height.
- Order Band places `WEIGHTING` above `ORDER BANDS` in the same right column.
  Depth 16 places `DEPTH` above `ENVIRONMENT`, without hidden or blank rows.

Labels use complete family vocabulary (`ORDER`, `PITCH`, `ROLL`, `SPREAD`,
`TWIST`, `WIDTH`). Every slider resets to its declared CLAP default on double
click. Field-camera controls preserve the established TOP, BACK, and 3/4
orientation conventions.

## Matrix Family Reference

The Matrix family covers the general Group Matrix 32/64 and Ambi Group Matrix
64/128 bundles. General host names begin with `s3g Matrix`; ambisonic variants
begin with `s3g Ambi Matrix`. In both branches, `Group` precedes the channel
count so each branch remains adjacent and predictable in a host browser.

All variants consume a shared responsive 1040 × 648 GUI contract:

- `GROUP MATRIX` is at x 18 / y 42, with a shared 344 px matrix grid.
- `PATTERN PREVIEW` is at x 466 / y 42.
- `OUTPUT` is the first toolbox at x 738 / y 42, with `OUT` in row 0.
- `PATTERN` begins at x 738 / y 108. General matrices add `GROUP` before the
  shared SHAPE, MODE, DEPTH, SPREAD, VORTEX, MOTION, RATE, DIVISION, PHASE,
  and SMOOTHING order.
- The disclosure glossary remains at x 18 / y 500.

The four implementations share one geometry and control-order declaration;
variant code may differ only for matrix dimensions, group modes, DSP types,
and browser identity. Title actions are `PRESET`, `LOAD`, `SAVE`, and
`RANDOM`. `RANDOM` invokes the matrix randomizer at the current `DEVIATION`
amount; the action must not be duplicated or abbreviated inside the matrix
panel. Every slider and crosspoint supports default reset, and value units
remain bounded inside their toolboxes.

## Mixer Family Reference

The Mixer family currently consists of Mixer Node Bus 128 and Ambi Mixer Node
Bus 128. Host names are `s3g Mixer Node Bus 128` and
`s3g Ambi Mixer Node Bus 128`; their full editor titles retain `128CH`.

Both variants use the responsive 920 × 920 Mixer contract. The title exposes
aligned `PRESET`, `LOAD`, and `SAVE` controls, with `PK` as the only far-right
status. The common content begins at y 42:

- `NODE FIELD` occupies x 18, width 560. Its field view is x 34 / y 78 /
  528 × 528, followed by bus identity, node-weight diagnostics, and peak
  meters inside the same bounded panel.
- `OUTPUT` is the first right-column toolbox at x 596 / y 42. `OUT` is its
  only row and includes the dB unit.
- `BUS / CURSOR` begins at x 596 / y 108. The general variant orders BED,
  CHANNELS, NODES, INFLUENCE, CURSOR X/Y/Z, RADIUS, FOCUS, GATE, and LOCK Z.
  The fixed-3OA variant omits BED, CHANNELS, and GATE without leaving blank
  rows.
- The selected `NODE` toolbox follows with a 12 px gap. ACTIVE is a toggle,
  source and bus identity precede LEVEL, spatial coordinates remain together,
  and shape/focus/rotation controls follow. The ambisonic variant uses its
  fixed bus range as a readout and compacts the unavailable rows.

All panels use the shared 26 px row pitch, 16 px label inset, bounded value
cell, and 18 px bottom clearance. Continuous sliders reset through their
declared CLAP defaults on double click. Menu text and labels are uppercase;
values are rounded before they can cross a toolbox border.

## Large Procedural Encoder Reference

Stochastic, Pulsar, and Wrangler are the first reference implementations.
Their native canvases use:

| Metric | Value |
| --- | ---: |
| Parameter top | 42 px |
| First column | x 630, width 250 px |
| Second column | x 896, width 246 px |
| Header height | 21 px |
| Stacked-panel gap | 12 px |
| First ordinary row | panel y + 36 px |
| Ordinary row pitch | 26 px |
| Declared compact pitch | 24 px |
| Final row baseline to panel bottom | 18 px |
| Label inset | 16 px |
| Control inset | 108 px |
| Slider track width | 82 px |
| Menu width | 124 px |

A one-row `OUTPUT` panel is 54 px high. Larger panel heights remain
control-fitted because different roles contain different row counts, but their
rows and stacking are calculated from the shared metrics. The standard
control-fitted height is:

`36 + (visible rows - 1) × 26 + bottom clearance`

The normal bottom clearance is exactly 18 px, including one-row `OUTPUT`
panels. A panel may declare a larger height only when a dropdown, meter,
preview, status, or action row follows the ordinary controls; that named
content must visibly occupy the added space. It may not preserve a hidden row.
Contextual tabs and modes recalculate both visible row count and the y position
of every panel that follows.

Wrangler's `OSC SHAPE / PULSE` panel is the initial declared compact exception.
Its ordinary first-column toolboxes use the standard 26 px pitch.

### Large-encoder family slots

| Shared item | Required location |
| --- | --- |
| `OUTPUT` | First column, panel y 42; `OUT` at y 78 |
| `ORDER` | Second row of `OUTPUT`, immediately below `OUT`; menu box x 738, control baseline y 104 |
| `TOPOLOGY` | Top of the second column at x 896, y 42, when topology controls exist |

The shared topology row order is `SHAPE`, `MOTION`, `RATE`, `AMOUNT`, `DEPTH`,
`SCALE`, `COLLAPSE`, then `TWIST`. A plugin may stop before `TWIST`, but it
does not insert a plugin-specific row into this shared sequence. Plugin-specific
topology extensions follow the shared rows.

All large Ambi encoders use the same `ORDER` location in `OUTPUT`. Stochastic
and Wrangler also use the same topology anchor and shared topology row order.
Compact encoders keep the same OUT-then-ORDER adjacency using their native row
pitch. Final-audition controls may follow ORDER in the OUTPUT toolbox; source,
engine, synthesis, and navigation controls begin in the next toolbox.

### Parameter Surface page

Procedural instruments without an internal score may expose the shared
`SURF` page inside the primary field-view toolbox. `SURF` is a peer of
`FIELD`, `CURVE`, and `LISTEN`; it never creates a disclosure panel or a third
parameter column. Page tabs retain the common 70 px width and 4 px header
inset, in the semantic order `FIELD`, plugin-specific edit/listen pages, then
`SURF`.

The surface uses two compact control rows. The first uses the shared order
`EDIT/PLAY`, `ON/OFF`, `ADD`, `DEL`, `CAP`, selected-cell preset, then `POP`.
The second uses `CURVE`, `FOCUS -/+`, then `GLIDE -/+`. The plot fills the
remaining field view. Cell sites are movable only in EDIT; the X/Y cursor is
movable only in PLAY. Empty space is not retained for unavailable cells, and
the surface remains off until at least two cells exist.

On macOS, `POP` opens the same interactive SURF view in a host-attached utility
panel and returns the primary editor to `FIELD`, allowing both views to remain
visible. The auxiliary view shares plugin state and host X/Y automation; it
does not own a second surface. Its page is locked to `SURF`, its `POP` button
closes the panel, and plugin GUI hide/destroy must hide or destroy it. Closing
the panel never changes the saved surface or the main editor's camera state.

All implementations support as many as 24 cells, inverse-distance
interpolation, the shared `SOFT`, `LINEAR`, `SMOOTH`, and `TIGHT` normalized
weight curves, a 0.25…8 focus exponent, shortest-path angle interpolation, and
nearest-cell selection for discrete values. `GLIDE` uses the common OFF, 40,
80, 160, 240, 400, 640, 1000, 1500, and 2000 ms steps; the displayed time is
the approximate 99% settling time. Any field point affected by a surface must
apply this glide in its DSP coordinate path, including shortest-path azimuth,
so its field drawing and encoded motion cannot jump between cell states. The
surface uses exact clipped
Voronoi polygons, the muted HSV region palette, influence halos, circular
nodes, and crosshair treatment established by s3g-mc Snapshot Surface; sampled
or tiled approximations are not permitted. `SURFACE X` and `SURFACE Y` are normalized,
automatable host parameters under the `Parameter Surface` module. `OUT`,
`ORDER`, final-audition controls, and cursor coordinates stay outside the
interpolated snapshot. In PLAY with the surface on, every toolbox slider and
menu that participates in the surface displays its effective interpolated or
nearest-cell value at the GUI refresh rate; EDIT displays the base state being
captured. A plugin with an existing score/listener system declares
whether that system is part of a cell or remains live; Wrangler keeps its
auditory score live, while Stochastic includes its listener in the cell state.
Complete surface state belongs to plugin/project state; portable one-preset
files remain one-state formats. Stochastic and Wrangler are the reference
implementations.

### Compact spatial-encoder slots

Compact spatial encoders keep the same semantic ordering even when their
primary field leaves room for only one parameter column:

| Member | `OUTPUT` | First post-output shared control |
| --- | --- | --- |
| Point | `OUT` at y 78 | `ORDER` at y 103 in `OUTPUT / POINT` |
| Cloud | `OUT` at y 78 | `ORDER` at y 104 in `OUTPUT / CLOUD` |
| Path | `OUT` at y 78 | `ORDER` at y 104 in `OUTPUT / PATH` |
| Surface Terrain | `OUT` at y 78 | `ORDER` at y 104 in `OUTPUT` |
| Ray | `OUT` at y 70 | `ORDER` at y 96 in `OUTPUT` |
| Ray Bilocation | `OUT` at y 692 | `ORDER` at y 718 in `OUTPUT / FIELD` |

Surface Terrain uses its PATH/FORM/SKIN/WARP/READ tabs inside the surface
toolbox header. The tabs do not consume an ordinary row: every contextual page
begins at toolbox y + 36 and packs only its visible controls. Its motion
toolbox follows the surface toolbox at the standard 12 px gap and uses the
standard 26 px row pitch.

Musical `SCALE` menus use the shared 101-scale 12-TET catalog declared in
`dsp/s3g_musical_scales.h`. VOT and VOX expose the complete catalog; Wave
Terrain adds `FREE` before the same list. All three use the same bounded
four-column menu and canonical display order: core tonal scales, modes,
pentatonic families, bebop collections, regional/global selections, then
modern and limited-transposition collections.

Stable numeric IDs are independent of display order, so existing host
automation and saved presets retain their meaning. New scales append to the
catalog; they do not insert into the stable ID sequence. Related variants are
adjacent in the menu and use the base-family-first vocabulary, including
`PENTATONIC MAJOR` / `PENTATONIC MINOR`, `BLUES MAJOR` / `BLUES MINOR`,
`NEAPOLITAN MAJOR` / `NEAPOLITAN MINOR`, and `HUNGARIAN MAJOR` /
`HUNGARIAN MINOR`. Full names are preferred, with bounded abbreviations only
when a name would exceed its menu column.

These are pitch-class mappings in 12-tone equal temperament, not claims to
reproduce culture-specific tuning, intonation, ornament, or melodic grammar.
`VECTOR` is not a Wave Terrain interpretation option. Legacy state value 9 is
sanitized to `CROSS`.

Wave Terrain's `ENGINE` rows follow the shared generator order: `MODE`,
integer `VOICES`, `BASE`, `SPREAD`, `TUNE`, then `DETUNE`. Hosts and the
native GUI must render `VOICES` without a fractional suffix. Wave Terrain
does not expose a separate monophonic/polyphonic trigger parameter; it keeps
the established voice and envelope behavior selected by `MODE`.

The Wave Terrain field always draws the complete closed terrain domain.
`SPACE` and `CENTER` project that domain into the outgoing ambisonic field;
they do not crop the preview into an open spherical patch. A click on a voice
contour selects it, while actual pointer movement takes precedence and rotates
the camera. Camera drag therefore remains available over the full field rather
than only over gaps between contours.

## Panner Family Reference

The Panner-family audit covers Layout, DBAP, LBAP, and VBAP. All four use a
900 x 720 responsive canvas, expose the sortable `s3g Panner <method>` host
name, and share the same title order: `PRESET`, `LOAD`, `SAVE`, then far-right
`PK`. The title does not repeat a channel count. Panners do not inherit the
Encoder family's unconditional `RANDOM` action.

The primary field uses x 18, y 42, width 596, height 616. Its inner field uses
x 34, y 76, width 564, height 566. All four members expose `FIELD`, `MIXER`,
and `DESIGN` pages. The right-side stack is:

| Context | Panel | Frame | Shared row order |
| --- | --- | --- | --- |
| All pages | `OUTPUT` | x 630, y 42, w 250, h 54 | `OUT` at y 78 |
| Field/Mixer | `PANNER` | x 630, y 108, w 250, h 314 | `LAYOUT`, `METHOD`, `FOC`, `ROLL`, `SMTH`, `GAZ`, `GEL`, `GDST`, `DIF`, `IN`, `SRC` |
| Field/Mixer | `SOURCE` | x 630, y 434, w 250, h 158 | `SEL`, `AZ`, `EL`, `DST`, `GAIN` |
| Design | `PANNER` | x 630, y 108, w 250, h 150 | `LAYOUT`, `SHAPE`, `COUNT`, then layout actions |
| Design | `SPEAKER` | x 630, y 270, w 250, h 132 | `SEL`, `AZ`, `EL`, `DST` |

Layout Panner keeps `METHOD` as an editable menu; a fixed-method member renders
its method in the same row as an aligned read-only value. This preserves row
alignment without presenting a false choice or leaving blank space. Ordinary
rows use the global 26 px pitch, panels use the global 12 px gap, and the last
row retains the global 18 px bottom clearance.

Title `LOAD` and `SAVE` operate on complete plug-in state. Design-page `LOAD`
and `SAVE` remain contextual layout-JSON actions. Every continuous Panner
slider, including the mirrored mixer output, per-source mixer gains, and
design-page speaker controls, resets to its declared or documented default on
double-click.

## Processor Family Reference

The Processor-family audit uses eight concepts and ten editors: Delay 8ch/24ch,
Buffer, Wave Geometry, Loop, Multi Loop, Fault, Ambi Grain, and Spectral
8ch/24ch. Membership is explicit so a renamed or multichannel variant
cannot silently escape the family contract.

All Processors use the shared Processor title renderer. Their complete title
ends in the meaningful processing width (`8CH`, `16CH`, or `24CH`), while the
far-right status contains only `PK` and an exceptional transient state such as
`CLIP`; it never repeats the channel count. The title-band order is `PRESET`,
`LOAD`, `SAVE`; `PRESET` restores the declared initial parameter state and
`LOAD`/`SAVE` operate on the complete plug-in state. `RANDOM` is not
a universal Processor title action. Safe random, mutate, reseed, generated
field, and captured-memory actions remain in the panel that owns their result.
Likewise, `LOAD AUDIO` or an equivalent media-source action remains beside the
waveform, source list, or playback transport and is never confused with state
preset loading.

Processor title typography is enforced inside the shared renderer, not supplied
by an editor's local content palette. The plugin name always uses
`softTitleAttrs()`, `PRESET`/`LOAD`/`SAVE` use `softLabelAttrs()`, and the preset
selection and `PK` status use `softValueAttrs()`. A locally brighter, bolder, or
dimmer toolbox palette cannot leak into the title strip.

`OUTPUT` is the first panel in the Processor control stack, even when the
primary visual occupies the left side or the controls sit in the right column.
`OUT` is always its first row and always displays dB. Final wet/dry `MIX` may
follow it; generation feedback, scan level, source rate, topology amount,
spectral damage, and other sound-building controls belong to their semantic
panels. A one-row OUTPUT panel is 54 px high. A two-row OUTPUT panel is 80 px
high. Both use the family first-row offset of 36 px, 26 px row pitch, and 18 px
clearance after the final baseline.

Every Processor toolbox uses the same interior anchors: its title begins 8 px
from the frame, ordinary labels begin 16 px from the frame, and sliders or
menus begin 108 px from the frame. Numeric values occupy a bounded 42 px cell
that ends 16 px before the right border. The slider track stops before that
cell; narrower toolboxes shorten the track instead of allowing the value to
cross the frame. Contextual rows outside an ordinary slider stack still use
the same 16 px label anchor; custom field headings retain the 8 px header
anchor.

Parameter toolboxes do not collapse. A Processor whose complete control set
fits in one column keeps a single persistent stack. A taller Processor uses a
second parameter column on a wider responsive design canvas; the host window
expands instead of scaling or hiding controls.

Delay 8ch/24ch, Wave Geometry 8ch, and Spectral 8ch/24ch share the topology
Processor column contract:

- responsive canvas width 1356 px; fitted canvas heights are Delay 8ch/24ch
  696 px, Wave Geometry 8ch 788 px, Spectral 8ch 814 px, and Spectral 24ch
  904 px
- primary field x 12 / y 42 / width 620 / height 638; its 600 px interior has
  a 10 px bottom inset and the field shell never stretches to fill a taller
  neighboring column
- field and toolbox headings use the shared soft-label font/color and a 12 px
  field-title inset; effects do not substitute brighter legacy palettes
- the `TOPO` page always exposes `TOP`, `SIDE`, and `3/4` camera buttons in
  the field header; ordinary click-drag in the 600 px field rotates from the
  selected camera without requiring a modifier key
- the second field page keeps the same 600 x 600 content surface. Channel
  views use the shared 2 x 4 grid through 8 channels and 4-column adaptive
  grid above 8 channels, with identical gaps and cell heights across effects
- PK appears once, at the far right of the plugin title band; field headers do
  not repeat it
- first parameter column x 644 / width 344: `OUTPUT`, processing `ENGINE`, then
  `PATCH MATRIX`
- second parameter column x 1000 / width 344: `TOPOLOGY` at y 42, followed
  by any topology-specific transport panel (`ECHO ROUTES`, `WAVE MESH`, or
  `PROPAGATION`)
- 12 px field/column, inter-column, and right-edge gutters
- all toolboxes are permanently visible; headers are static and have no
  disclosure marker
- editor minimum width is 1356 px, so hosts resize narrow saved windows to the
  complete two-column canvas
- the drawing surface is never magnified or reduced; typography and controls
  remain at 1:1 scale
- height may resize independently and expose overflow through vertical
  scrolling
- the canvas ends with a 12 px exterior gutter after the lowest real field,
  panel, patch footer, or status item

The remaining layout follows the Processor subtype:

| Subtype | Persistent layout after `OUTPUT` |
| --- | --- |
| Topology effect | first column: processing engine then patch/routing; second column: `TOPOLOGY`, then the effect-specific mesh/propagation controls when present |
| Loaded-media instrument | playback/source engine, source/window, `RELATIONSHIPS` |
| Buffer/time processor | engine, `RELATIONSHIPS`, corruption/memory |
| Developed generator | field/source, codec/shape, performance/envelope |

Loop and Multi Loop fit `OUTPUT`, `ENGINE`, and `RELATIONSHIPS` in their
existing single right column, so those panels remain open without widening the
canvas.

All Processor editors use the responsive viewport contract. Every continuous
slider, including topology, relationship, contextual-source, and output rows,
resets through its declared CLAP default on double-click. Menu choices render
uppercase through the shared bounded menu renderer. Numeric output values use
the shared bounded value cell, which may remove a space or unnecessary
precision before it allows text to cross a toolbox border.

The `audit_gui_processor_family` build target renders and validates all ten
editors, their host-name prefix, responsive sizing, and shared default lookup.

## Decoder Family Reference

The decoder-family audit uses an explicit six-member list: Head, Stereo, Sub,
Adaptive 64, Object 64, and Speaker 64. All six expose the sortable host-name
prefix `s3g Ambi Decoder ` and are rendered by the
`audit_gui_decoder_family` target.

Decoder editors use a large primary field on the left and a single setup column
on the right. The compact Sub editor uses the same semantic order in its one
parameter stack.

| Stack position | Panel | Shared row order |
| ---: | --- | --- |
| 1 | `OUTPUT` | `OUT`, then final monitor/preserve/safety controls |
| 2 | `DECODER` | `LAYOUT`/`FIELD`, `MODE`/`METHOD`, `ORDER`, `WGT`, `AGN`, then count/width trims |
| 3 | Decoder-specific processing | binaural, transaural, stereo pickup, adaptive, object |
| 4 | Selection/editing | selected speaker or other selected output |

An unsupported shared row is omitted without reordering the remaining rows.
Head Decoder's `BINAURAL` and `TRANSAURAL` panels both fit its setup column;
both remain permanently visible with static headers rather than disclosure
controls.
Use `ORDER` consistently rather than mixing `ORD` and `ORDER`, and render it as
a menu. `OUT` always includes its `dB` unit and remains the first slider in the
right-side stack. Fixed channel width is not repeated beside `PK` in the title
band; the member name and layout/count controls already communicate it.

Decoder configuration is infrastructure, so it does not inherit the encoder
family's unconditional `RANDOM` action. Every decoder uses the shared Decoder
title renderer, established 10.5 px soft-title face, and aligned `PRESET`,
`LOAD`, and `SAVE` controls. `PRESET` restores the decoder's initial state;
`LOAD` and `SAVE` operate on the complete plug-in state. A decoder exposes
`RANDOM` only when it has a safe, musically meaningful randomization contract.

All decoder editors use the shared responsive viewport with a 480 x 360
minimum. Destruction must detach the viewport even if a host destroys the
plug-in before calling `gui.destroy()`.

Menu labels, selected values, and dropdown items render in uppercase throughout
both families, including proper method and layout names. If an uppercase value
does not fit, use the shared compact abbreviation before falling back to a
clipped/ellipsized value. Spatial pickup markers use the family diamond motif.
Head diagrams favor a simple, elongated geometric silhouette with only the
guides needed to read orientation and ear/pickup position.

Head and Stereo fields print the active camera at the bottom of the primary
view as `CAM <VIEW> AZ <angle> EL <angle>`. Preset cameras remain stable until
actual pointer movement begins; mouse-down alone never switches to a different
projection. Dragging from TOP, BACK, or SIDE seeds the free camera from that
exact preset, then continues without a jump. Head treats its brow/nose cross as
an orientation mark rather than a fixed overlay: TOP shows only its edge-on
sliver, BACK shows the full mark, and free 3/4 views keep the brow above the
nose stem instead of inverting it. Field axes must not bleed through the opaque
head silhouette and create a second accidental face marker.

Head combines decode mode and virtual-layout selection in its `FIELD` menu.
`INTERNAL GRID` selects direct decoding; choosing any named virtual layout
selects that layout and switches the decoder to Virtual Field. A second
Direct/Virtual menu is omitted because Direct has no further choices.

## Shared Slider Interaction

Every continuous slider in the Encoder, Decoder, Panner, and Array families
resets to its declared parameter default on a double-click. This includes
sliders on contextual pages, mirrored mixer views, and per-channel Array
editors, not only the primary parameter columns. The reset must use the same
parameter-update path as an ordinary drag so the GUI, DSP state,
selection-dependent values, and host-visible state stay synchronized. Menus,
toggles, view controls, and spatial-field dragging do not inherit this gesture.

## Shared Implementation

`plugins/common/s3g_gui_layout.h` is platform-neutral and provides:

- plugin-class and panel-role declarations;
- shared-control roles, conditional panel anchors, and family control slots;
- executable semantic row orders for engine, tuning, envelope, projection,
  motion, listener, and environment families;
- encoder title-band geometry for compact, medium, and wide canvases;
- canonical metrics and large-encoder columns;
- panel creation, content-fitted height, compacted visible-row placement, and
  stack placement;
- row, menu, and slider-hit geometry;
- compile-time canvas, row containment, column alignment, gap, output-first,
  family-slot, and conditional-anchor validation.

`plugins/common/s3g_cocoa_gui.h` converts contract rectangles to Cocoa geometry
and draws contract panels using the existing visual helpers. It also provides
the shared Encoder and Decoder title renderers, complete-state preset file I/O,
bounded slider-value rendering with precision reduction, and the shared lookup
that resolves a slider's default from its CLAP parameter declaration.

Each migrated GUI declares its panel stack as `constexpr` data and validates
it with `static_assert`. Its first GUI slider is also asserted to be `OUT`.
This makes geometry drift a build failure instead of a visual-review surprise.

## Adoption

New custom GUIs must select a plugin class and use the shared layout contract.
Existing plugins migrate family by family. A family becomes a reference only
after its draw paths, menu paths, and hit paths consume shared geometry and its
native and responsive GUI smokes pass. The decoder-family smoke accepts both
responsive and fixed legacy members so the current family can be rendered
during migration; fixed mode does not satisfy the responsive reference
contract.

The advisory style audit checks reference-family adoption. The
`s3g_gui_layout_contract_smoke` target tests the platform-neutral layout
primitives and class-template invariants.
