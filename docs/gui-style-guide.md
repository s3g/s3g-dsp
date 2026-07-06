# s3g-dsp GUI Style Guide

This is the working style reference for custom macOS CLAP plugin GUIs in
`s3g-dsp`. The goal is consistency across plugins, not decoration.

## Overall Look

- Use a flat grayscale interface on a near-black background.
- Keep typography small, left-aligned, and monospaced.
- Prefer compact panels, square controls, thin linework, and high-contrast
  white/gray accents.
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
- Use `+` / `-` at the left of collapsible headers.
- Put controls inside the panel with a small internal text/control inset.
- Keep panels aligned by their top edges when they form a visual column.

Reference pattern:

```cpp
drawPanelFrame(panelX, panelY, panelW, panelH, style);
drawPanelHeader(@"ENGINE", open, panelX, panelY, panelW, headerH, attrs, style);
```

## Sliders And Menus

- Sliders are square, horizontal, and text-labeled with short all-caps names.
- Use three-letter abbreviations when possible: `OUT`, `CTR`, `LEN`, `SMR`.
- Menus should use the same dark fill, gray border, and compact type as sliders.
- Do not use native-looking popup controls inside the plugin canvas.
- Reuse abbreviations consistently across plugins. When a label is reused, it
  should describe the same kind of relationship everywhere.

## Primary Visuals

- The primary visual gets the largest uninterrupted area in the window.
- Its frame/header follows the same panel language as the toolboxes, but the
  inside can be plugin-specific.
- Playheads, topology nodes, meters, and lane indicators should intersect or
  overlay the thing they describe. Avoid detached mini-displays when the
  position belongs directly to a waveform, topology, or meter.
- If a visualization has a secondary layer, such as the Delay Processor heatmap,
  separate it clearly from labels and borders.

## Layout

- Avoid large unused bands between information regions.
- Align related columns at the same top y-position.
- Do not put cards inside cards.
- Use collapsible toolboxes when a right-side control stack becomes tall.

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
