# Topology-family VSTGUI migration

## Scope and reference

Processor Delay 8/24, Processor Wave Geometry 8, and Processor Spectral 8/24.
The original Cocoa source (baseline `36c67a6`) and the
[Delay](../../docs/processor-delay.html),
[Wave Geometry](../../docs/processor-wave-geometry.html),
[Spectral](../../docs/processor-spectral.html), and
[Topology framework](../../docs/topology-framework.html) guides are the
specification. This is an adaptation, not a redesign. Cocoa remains compiled
as the comparison reference; documentation images have not been replaced.

## Parity contract

| Area | Preserved behavior |
| --- | --- |
| Layout | Original three columns, panel geometry, 26px row rhythm, complete engine and secondary controls, title/status/preset band. Shared Fira Code, lowercase `s3g` and uppercase plugin title, family colors, centered control text. |
| Field | Original point generators, camera projection/depth, TOP/SIDE/3/4 presets, orbit sensitivity and pitch limits. Original 54×18 heatmap, stops and normalization; no substitute graph. |
| Delay | Matrix input rows select the physical participating nodes. Preserve sparse lane identities, static neighbor edges, directional repeat markers, node arrivals, centroid spokes/halo/orbit, LST readout, topology-only RESET, and Echo Routes. |
| Wave Geometry | All eight mesh nodes remain present; matrix selects direct excitation. Preserve energy-brightened edges, directional square markers, node/centroid activity, injected-node appearance and per-lane readout. All thirteen engine controls and four Wave Mesh controls remain. |
| Spectral | Fixed 8/24-node mesh and direct-input matrix. Preserve directed transport markers, per-lane readout, CAP/CLR indicator/actions, automatic FRZ capture, RPT, coupled LO/HI bounds and three propagation controls. |
| Analyzer | Delay: 512 points spanning one second per lane. Wave: original 128-sample oscilloscope. Spectral: original one-second, grayscale 96×24 sonogram using a 48-sample Hann window, with invariant trigonometry cached. These are display-only. |
| Input | Every slider keeps its original range, mapping, full-row hit area and double-click default. Custom in-canvas menus retain all shapes, motion modes, variants and neighbor choices, hover and outside dismissal. LST remains clickable over the orbit field. |
| Presets | Original `.s3gpreset` codecs and directories, matrix restoration, Unicode paths and corrupt/truncated-file rejection. User LOAD and INIT preserve OUT; INIT leaves the matrix alone. No invented factory bank or RANDOM action. Project recall restores saved OUT as before. |
| Audio/state | Original DSP, descriptors, IDs, parameter metadata, state versions and legacy readers are unchanged. Spectral captures/history remain runtime audio state, not newly embedded in presets. |
| Lifecycle | Shared CLAP GUI lifecycle, 65–200% proportional resizing, packaged Fira Code/license, missing-font fallback, platform resource paths and Unicode file dialogs. |

One necessary extent correction: the original Spectral 24 canvas was 1356×820,
clipping its last matrix rows. Its VSTGUI canvas is 1356×912, with the original
controls and matrix at their original coordinates. The other four dimensions
are unchanged. All 24 rows and the footer are now visible and clickable.

## Threading and automation

These processors already publish controls and patch rows through atomic banks.
The portable editors retain that path; they do not mutate audio-owned DSP.
The added SPSC queue reports GUI value/begin/end events to the host through
`process` and `params.flush`. It deliberately does **not** replay old GUI
values into the bank when drained, which would overwrite newer automation
after host-output backpressure. Reserve END capacity before every gesture and
reject preset batches before changing anything if queue capacity is insufficient.
Patch edits notify the host that project state is dirty.

Delay and Spectral telemetry now use portable GUI visibility on both platforms;
Windows must not silently lose its live graph, scope, sonogram or output meter
behind a Cocoa-only visibility check. Spectral's analyzer publication is still
disabled while hidden. Telemetry does not change audio routing or processing.

## Builds and verification

Enable `S3G_ENABLE_PORTABLE_CLAP_GUI` and
`S3G_ENABLE_TOPOLOGY_FAMILY_VSTGUI_ON_MACOS=ON` for macOS comparison builds.
Windows selects these editors automatically with portable GUI support enabled.
Keep `S3G_BUILD_FUTURE_COMPONENTS=OFF` for the focused Windows cross-build;
these five targets do not require it.

Targets:

- `s3g_8ch_delay_processor_clap`, `s3g_24ch_delay_processor_clap`
- `s3g_wave_geometry_processor_clap`
- `s3g_spectral_topology_processor_clap`, `s3g_24ch_spectral_topology_processor_clap`
- `s3g_topology_{delay_8,delay_24,wave_geometry_8,spectral_topology_8,spectral_topology_24}_canvas_smoke`
- `audit_gui_topology_family`: all five native GUI/lifecycle/interaction audits

The five canvas tests exercise actual input and DSP: every slider/menu option,
all matrix cells, orbit/view/page/readout controls, output preservation and
Unicode presets, queue backpressure and saturation, hide during drag, actual
audio-driven graph telemetry, Spectral CAP/CLR/FRZ/RPT and hidden analyzer gating.
`S3G_TOPOLOGY_CAPTURE_DIR` writes matched Cocoa PDFs and portable PNGs, including
all shapes, active/readout/analyzer views and the larger Windows font metrics.
`S3G_TOPOLOGY_EXPECT_FONT_FALLBACK=1` supports running a **copy** of a test bundle
with its Fonts directory moved aside.

Native `s3g_encoder_family_gui_smoke` coverage retains Delay's sparse propagation
and legacy state tests, Wave Mesh audio/tail/state tests, and Spectral's
propagation-tail tests. Portable input is transformed to the current editor
scale. Camera/page changes are checked through rendered pixels instead of
accessing Cocoa-private ivars. Cocoa runs retain their original assertions.

Verification on 2026-09-09:

- Five macOS portable builds and five native GUI/resizing audits passed;
  the five Cocoa fallback builds/audits passed too.
- Five canvas interaction/audio tests passed, plus five copied-bundle
  missing-font runs. No installed bundle was changed for these tests.
- DSP smoke and Delay block-coalescing regression passed. Seventeen shared,
  Drum and Spectral legacy-state regressions passed.
- CLAP parameter/state/process-filter validation: 30 passed, 10 skipped,
  zero failures or warnings. Skipped tests are not claimed as passes.
- Five Windows x64 builds: correct `clap_entry` export, Windows/UCRT-only
  DLL dependencies, no extra compiler runtime DLLs; font/license hashes match
  the repository and macOS resources. Mac bundle signatures verified.
- Fresh native Cocoa and portable TOPO and SCOPE/SONO references are under
  each build tree's `topology-family-reference/`. Additional interaction
  captures are in `build-clap-sample-vstgui-fidelity/topology-family-parity/`.
  The sampled flat panel/background colors match exactly. Across 972 heat-cell
  centers per build, mean RGB-channel differences are under 0.24/255;
  font and rasterizer differences mean these are not pixel-identical images.

The grayscale/heat color tables are generated from Cocoa calibrated colors
converted to sRGB; `scripts/generate-topology-color-reference.swift` reproduces
the header contents on macOS. Keep those tables platform-independent.

Windows REAPER runtime verification is still required on the separate Windows
machine. Build/export/resource checks and Windows-sized font renders are not a
substitute for that test. Installation and a Windows transfer package are
separate steps; this migration does not change the installed plugins.
