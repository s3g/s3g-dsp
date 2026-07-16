# s3g-dsp GUI Style Guide

This is the working style reference for custom macOS CLAP plugin GUIs in
`s3g-dsp`. The goal is consistency across plugins, not decoration.

## Overall Look

- Use a flat grayscale interface on a near-black background.
- Keep typography small, left-aligned, and monospaced.
- Use regular-weight text by default. Avoid bold UI labels unless the glyph is
  part of an icon-like marker. The shared Cocoa helpers use soft grays:
  label text around `0xa8a8a8`, value/status text around `0x929292`, and
  titles around `0xc8c8c8`.
- New plugin GUIs should use the shared Cocoa helpers in
  `plugins/common/s3g_cocoa_gui.h` for text, panels, sliders, menus, dropdowns,
  peak text, and header buttons. A new one-off text dictionary or custom
  control renderer should be treated as a deliberate exception and named in
  review.
- Prefer compact panels, square controls, thin linework, and high-contrast
  gray accents. Avoid pure white text as the default visual voice.
- Avoid nested panel containers. A panel should sit directly on the main
  window background unless it is a specific content view like a waveform,
  topology display, patch matrix, or meter.
- Give each plugin one primary visual object. Delay Processor uses the
  topology view; Loop Processor uses the waveform/playhead view; fold-down
  tools use projection and meter views.
- Use color only when it carries signal meaning. The default UI stays
  grayscale; heatmaps, meters, and warnings are exceptions.

## Panel Rules

- Draw each toolbox as one rectangle: frame, fill, and header share the same
  `x`, `y`, and `width`.
- Do not inset the header relative to the panel frame.
- Use a dark header strip with a 2 px light line at the top.
- Use `+` / `-` at the left of collapsible headers only. If a panel cannot be
  toggled by clicking its header, draw a static header with no disclosure
  marker. A `-` promises an open/close interaction.
- Keep toolbox header titles normal weight. The header strip and top line are
  enough hierarchy; avoid bold or highlighted panel titles unless the title is
  an active status indicator.
- Put controls inside the panel with a small internal text/control inset.
- Keep panels aligned by their top edges when they form a visual column.
- Size panels to their visible controls. After the final row, leave only enough
  bottom padding to match the family reference, usually about 12-18 px. Do not
  leave large empty panel interiors because another plugin has more controls in
  the same panel type.
- Use consistent stacked-panel gaps inside a plugin family. Macro and other
  toolbox stacks should stay around 12-14 px unless the whole family layout is
  intentionally revised.

Reference pattern:

```cpp
drawPanelFrame(panelX, panelY, panelW, panelH, style);
drawPanelHeader(@"ENGINE", true, panelX, panelY, panelW, headerH, attrs, style);
drawDisclosurePanelHeader(@"ENGINE", open, panelX, panelY, panelW, headerH, attrs, style);
```

Use `drawPanelHeader()` for static panels and `drawDisclosurePanelHeader()`
only when the header click handler actually toggles the panel body.

The advisory script `scripts/audit-gui-style.sh` checks for common GUI drift:
false disclosure markers, local bold/bright text, hand-formatted peak readouts,
binary controls drawn as sliders, text-entry styling, timer redraw gates, saved
view state, and obvious draw/hit geometry review points. Run it directly or via
`cmake --build <build-dir> --target audit_gui_style`. It exits successfully by
default and prints warnings; pass `--strict` when a release checklist should
fail on any warning.

## Sliders And Menus

- Sliders are square, horizontal, and text-labeled with short all-caps names.
- Use three-letter abbreviations when possible: `OUT`, `CTR`, `LEN`, `SMR`.
- Standard toolbox slider/menu rows should use a readable 22-26 px vertical
  pitch. Use tighter 18 px rows only for explicitly compact matrix/topology
  surfaces where the family reference already depends on that density.
- Keep draw geometry and mouse geometry from the same row constants. If a row
  pitch or panel height changes, update hit rectangles and dropdown origins in
  the same pass.
- `PK` / peak output status is a readout, not an editable control. Draw it in
  the upper-right title/status area with other compact host/plugin status text,
  not inside the right-side parameter toolbox or `OUTPUT` panel.
- Double-clicking a slider should return that parameter to its default value.
  This is a package-wide interaction convention, not a plugin-specific feature.
- Parameters with discrete named states must be menus, not sliders. Examples:
  motion modes, layout modes, shape choices, neighbor counts, launch modes,
  mute/on-off states, and global mode toggles.
- Small discrete numeric sets such as ambisonic `ORD` or filter `POLES` should
  also use menus. High-cardinality numeric counts such as `ACTIVE 1-64` may
  stay sliders when a long menu would slow editing.
- Menus should use the same dark fill, gray border, and compact type as sliders.
- Do not use system-style popup controls inside the plugin canvas.
- Dropdown lists should use the shared custom renderer where possible:
  selected rows get a quiet left strip, rollover rows get a slightly lighter
  gray fill, and click hit-testing should follow the visible row rectangles.
- Reuse abbreviations consistently across plugins. When a label is reused, it
  should describe the same kind of relationship everywhere.

Before editing a GUI, check the controls being drawn against this section:
continuous numeric values use sliders; named/discrete values use menus or
buttons. Do this before build/test so style drift is caught in the edit pass.

## Primary Visuals

- The primary visual gets the largest uninterrupted area in the window.
- Its frame/header follows the same panel language as the toolboxes, but the
  inside can be plugin-specific.
- Playheads, topology nodes, meters, and lane indicators should intersect or
  overlay the thing they describe. Avoid detached mini-displays when the
  position belongs directly to a waveform, topology, or meter.
- If a visualization has a secondary layer, such as the Delay Processor heatmap,
  separate it clearly from labels and borders.

## Spatial Views

Ambisonic and 3OAFX infrastructure plugins use a restrained spatial-view
grammar:

- Put compact `TOP`, `SIDE`, and `3/4` view buttons in the primary visual
  header when a point scene can be reoriented.
- Put camera zoom `-` / `+` immediately to the left of `TOP`, `SIDE`, and
  `3/4` when both are present. Keep this ordering consistent across encoders,
  decoders, and panners.
- Put mode buttons that affect interaction, such as `EDIT` / `PLAY`, in the
  primary visual header just after the title when there is room. They should
  read as part of the field mode, not as right-side parameter controls.
- When camera position is part of the normal editing workflow, save view mode,
  manual rotation, and zoom with plugin state so reopening a project preserves
  the user's inspection angle.
- Spatial tools with view buttons should either persist the view state or be
  explicitly documented as momentary meters/diagnostics. Do not add unsaved
  camera controls to a new creative/spatial plugin.
- Put compact `-` / `+` camera zoom buttons near the view buttons when point
  density can obscure the spatial read. Camera zoom changes only the view
  projection, never AED distance values.
- Speaker and source point numbers should use the shared regular UI font,
  not `Menlo-Bold`. Selected labels can become slightly lighter, but should
  stay in the muted gray family rather than jumping to bright white.
- AED point displays follow the `s3g-mc` screen convention: in `TOP` view,
  azimuth `0` degrees draws at the top of the field, `180` degrees at the
  bottom, `-90` degrees to the right, and `+90` degrees to the left. In `SIDE`
  view, positive elevation draws upward.
- Physics or motion scene presets belong in a `SCENE` menu, with continuous
  force trims below it. Scene selection may set several related controls, but
  subsequent trim edits should read as a custom scene.
- Physics scenes should be behaviorally distinct, not only different speeds:
  `ORBIT` should read as slow circulation, `BOUNCE` as inward/gravity-like
  rebound and collision, `COLLIDE` as point-to-point pressure, and `SCATTER` as
  non-orbital burst energy.
- Physics scenes should use a persistent point world rather than one-frame
  trigonometric offsets when collision or rebound is central to the behavior.
  The preferred model is previous-position/Verlet-style motion, nearest-neighbor
  constraints or springs, boundary collision, and a moving `POLTERGEIST`
  cue-ball source that prevents the scene from settling into static balance.
  `POLTERGEIST` should be exposed as a host-automatable macro where the motion
  benefits from one readable agitation control. It should have a visible radius
  of influence drawn as a projected 3D sphere, not a flat screen-space circle:
  points outside the radius are not pushed, and points inside it receive smooth
  falloff, carry, and glancing energy from the moving source. The force should
  begin at the visible sphere edge so the visual boundary and physical boundary
  agree. If a plugin has a global hemisphere constraint, auxiliary motion
  sources such as `POLTERGEIST` must adapt to that hemisphere too; the force path
  and any clipped radius drawing should match the constrained point field.
  In point-network scenes, `POLTERGEIST` amount also dissolves nearest-neighbor
  relationships: `0%` keeps the scene's normal bonds, while `100%` disables
  neighbor springs so the Geist/collision/boundary model dominates.
  Geist strikes may erase local bonds for a musically readable duration; broken
  links should disappear or fade in the nearest-neighbor drawing while released
  points get a subtle visual marker.
  Avoid inverse-distance force fields here because they create jerky close-range
  acceleration and point grinding. Visualize collision energy with outline/link
  brightness, not point-size changes, so the display does not imply gain or
  distance changes.
- Point-to-point collisions should behave like elastic ball contact, not like
  grinding overlap. If motion can settle into static balance, add an explicit
  macro force or scene energy rather than making points jitter against one
  another.

Speaker decoder views are spatial infrastructure, not physics instruments:

- Use a single large speaker field as the primary visual. Draw speaker points,
  IDs, and a simple layout outline without implying gain changes through point
  size.
- Speaker layout presets should number room-style speakers from the stereo
  right position, continue clockwise around the current elevation layer, then
  continue upward through higher layers. Fixed 3OAFX virtual-speaker layouts may
  preserve their workflow-specific order.
- Speaker decoder fields should use the same camera controls as Point Encoder:
  `TOP`, `SIDE`, `3/4`, `-`, `+`, and blank-field drag rotation.
- Speaker markers should use square Point Encoder-style blocks with centered
  IDs and the shared AED/OKLCH color mapping. Selected speakers get a thin
  outer square frame.
- Speaker decoder connection lines should show the intended 3D layout mesh,
  not nearest-neighbor analysis and not speaker list order. Cubes draw as cube
  wireframes; domes draw as rings with vertical layer connections. If a layout
  uses stacked cube tiers, preserve the actual 3D distance so upper/lower
  corner speakers project to the same plan-view coordinates as the middle-tier
  corners. For CUBE17, keep the drawing readable: lower, middle, and upper
  tiers use simple horizontal/vertical edges, with the top four corners
  radiating to the central top speaker.
- Keep decoder setup in one right-side `DECODER` panel: layout, mode, order,
  order weighting, active speaker count, and musically readable matrix trims.
  Numerical conditioning controls such as regularization should stay internal
  unless they solve a visible user problem.
- Keep per-speaker editing in one right-side `SPEAKER` panel: selected speaker,
  AED position, and distance. Speakers are always active; level adjustment
  belongs in the mixer.
- Keep decoder speaker levels in a primary `MIXER` view that swaps with the
  large speaker field: one lane per active speaker, per-speaker gain as a
  continuous fader, and global `OUT` in the same view. Speaker mixer faders
  should use the panel height generously, include compact numeric text fields
  for exact gain entry, and group speakers in pages of 16 so mute/solo
  diagnostics remain readable at high speaker counts.
- Discrete decoder states such as layout, mode, order, weighting, and field
  shape use dropdown menus, not click-to-advance controls. Numeric layout edits
  use sliders.
- AED angles and speaker distance should also provide compact text-entry boxes
  for exact placement. Sliders are for fast movement; boxes are for measured
  coordinates.
- Custom speaker layouts need a generated starting point. Changing speaker
  count should create a logical layout, with an explicit full-sphere/hemisphere
  field choice before manual AED edits.
- `MANUAL` should be the first physics scene for spatial tools that expose
  point automation. It disables internal motion so REAPER automation lanes can
  drive AED/gain directly. `CUSTOM` is reserved for hand-tweaked moving states
  after a scene preset has been modified.
- Use `SCALE` as the motion-space radius limit. `1.0` is the normal unit-sphere
  reach for scene presets; values above `1.0` should be an explicit user choice
  rather than a preset default. This is separate from camera zoom: scale changes
  the animated/smoothed point positions used by the encoder, while camera zoom
  changes only the view.
- Spatial point-source plugins with many automatable points should include a
  compact point mixer surface when space allows: one lane per point, gain as a
  continuous fader, and mute/solo as discrete buttons. The selected-point AED
  editor can stay separate from this mixer.
- When the point mixer would compete with the spatial field, put the mixer on a
  second primary-view page rather than shrinking the spatial field. This is the
  preferred pattern for future 32/64 point tools.
- Dragging directly on a visible point may edit AED when the active view has a
  clear 2D mapping. In `TOP`, point drag edits azimuth and distance while
  preserving elevation. In `SIDE`, point drag edits elevation plus the lateral
  axis while preserving the hidden depth component. In `3/4` or manually
  rotated views, point clicks should select points and blank-field drags should
  rotate the inspection view.
- Use OKLCH-derived point colors for spatial identity: hue follows azimuth with
  `0` degrees at red and wrapping like a color wheel, lightness follows
  elevation, and chroma follows distance. Keep chroma restrained so color
  carries meaning without breaking the grayscale UI.
- Draw nearest-neighbor links as a quiet analysis overlay behind spatial
  points. These links show geometric proximity and should not imply audio
  routing unless the plugin explicitly uses them for DSP.
- For LBAP and VBAP panners, topology overlays are not just decoration: draw
  the actual solver topology. Use distinct quiet colors for the methods
  (`LBAP` cyan, `VBAP` yellow), and show imaginary solver support points
  faintly so topology lines do not appear to terminate in empty space.
- If a direct panner layout has no real lower-elevation speakers, negative
  elevation should remain meaningful as a smooth rolloff toward `-90` degrees
  rather than being clamped to the perimeter.
- Keep spatial control panels separate from view controls. View changes are
  inspection tools; automation-relevant controls remain in the toolbox stack.
- Prefer readable full parameter names in spatial infrastructure panels when
  the panel width supports them. Compact three-letter labels are still fine for
  dense Macro/effect families where repeated controls need tight alignment.

Direct layout panners are spatial infrastructure, but they are not ambisonic
encoder/decoder pairs:

- Keep direct panners as one plugin when the audio model is source-to-speaker
  gain rendering. Use linked plugin pairs only when there is a meaningful
  intermediate format, such as ACN/SN3D ambisonics.
- Use a large `LAYOUT FIELD` primary view that can show speaker geometry and
  source points together. Speakers remain quieter square markers; source points
  are larger square markers with `S1` style IDs.
- Current direct panners may expose up to 16 source points before moving to a
  wider/page-based source mixer. Keep all 16 visible in the field and mixer
  when the window size allows it.
- Direct source dragging should feel like free XYZ movement in the current
  camera plane. AED controls may remain as readable coordinate sliders, but
  field interaction should not feel constrained to angular/radius paths.
- Use a second `SOURCE MIXER` primary view for source gain, mute, and solo when
  mixer controls would crowd the spatial field.
- Use a third `LAYOUT DESIGN` primary view for custom speaker layouts rather
  than opening a separate floating editor window. `DESIGN` should make speaker
  points selectable and draggable, hide source points, and keep camera controls
  identical to `FIELD`.
- In `DESIGN`, use a `SHAPE` menu for speaker layout rules instead of a simple
  sphere/hemisphere toggle. `AUTO` chooses useful defaults by count, while
  explicit user-selectable shapes such as `RING`, `DOME`, `GEO`, and `STACK`
  keep the count slider meaningful for arbitrary layouts up to 64 speakers.
  `AUTO` may generate canonical polyhedra such as tetrahedra, octahedra, cubes,
  icosahedra, or dodecahedra for matching counts, but those fixed-count
  polyhedra should not be separate `SHAPE` menu choices. Hide panning method,
  focus, diffusion, and other audio-distribution controls in `DESIGN`; they
  change audio behavior, not the speaker geometry being edited.
- Layout presets and panning methods are dropdown menus. Continuous controls
  such as focus, rolloff, smoothing, global AED offsets, diffusion, selected
  source AED, source gain, and output gain are sliders.
- Draw layout meshes from the intended speaker geometry, not nearest-neighbor
  analysis. Direct panners may later add analysis overlays, but those should be
  visually secondary and clearly distinct from the speaker layout mesh.
- Custom layout import/export uses readable JSON with `shape`, `speaker_count`,
  and a `speakers` array of `azimuth`, `elevation`, and `distance` objects. This
  is the preferred hand-editable format unless a future workflow needs a more
  compact plain-text layout list.

## Layout

- Avoid large unused bands between information regions.
- Align related columns at the same top y-position.
- Do not put cards inside cards.
- Use collapsible toolboxes when a right-side control stack becomes tall.

## Macro Family Layout

Macro plugins use a fixed panel grammar so related effects can be stacked and
read quickly:

- `ENGINE` is the effect-specific panel.
- `RELATIONSHIPS` always uses the shared lane relationship set:
  `SPRD`, `DEV`, `SKW`, `CTR`, `GLD`.
- `OUTPUT` always holds `MIX` and `OUT`, in that order.
- The upper-right title area uses compact status readouts, not prose:
  `PK` followed by the channel width such as `8CH`.
- Avoid explanatory copy inside the plugin canvas. Put explanations in docs,
  tooltips, or diagrams instead.
- Keep shared labels in the same visual positions across Macro plugins when the
  window size allows it.
- Keep the left-column `ENGINE` and lane relationship stack visually equivalent
  across Macro plugins: same top origin, same panel style, control-fitted panel
  heights, and the same stacked-panel gap. If an engine has fewer controls, the
  panel gets shorter; it does not keep unused height.
- The lane relationship preview follows the roomier `s3g Macro Pitch` spacing:
  a compact framed lane-bar display with `L1`, `L2`, etc. labels, dark tracks,
  gray fills, and normalized per-lane relationship values. Keep enough panel
  height for eight lanes to breathe, even if an effect-specific engine panel
  requires a taller window. Avoid effect-specific graph styles in the Macro
  family preview area unless the whole family adopts them.

## New GUI Review Checklist

Before adding or shipping a new custom plugin GUI, check these items:

- Text uses shared regular-weight helpers; no local `Menlo-Bold` label blocks
  unless there is a named visual exception.
- Peak status uses `peakDbText()` and lives in the upper-right status area.
- Timer-driven repainting is gated by view visibility and host foreground
  activity; text fields are not repainted while the user is editing.
- Discrete named choices and small discrete numeric sets use menus. Long
  numeric ranges can stay sliders when they are faster to edit that way.
- Sliders double-click to reset and share their draw/hit row geometry.
- Numeric text fields use the shared number-field style so selection remains
  readable and entry does not fight the redraw loop.
- Static toolbox headers have no `+` / `-`; disclosure headers use
  `drawDisclosurePanelHeader()` and have a working header click target.
- Spatial camera buttons and zoom follow the package ordering and save view
  state when the view is part of normal creative editing.

## Current Visual Reference

The current primary reference is `s3g Delay Processor 8ch`:

- right-side toolboxes drawn directly on the main background
- dark header strip with light top line
- compact sliders and menus
- minimal grayscale panel frames
- no extra parent container behind the toolbox column

`s3g Loop Processor 8ch` is the sample-lane reference:

- waveform is the primary visual object
- lane cursors intersect the waveform timeline
- loop region markers show the active window
- output lanes use compact square cells rather than a full patch matrix
