# Terrain/map encoder VSTGUI parity

## Scope and reference

The portable editors are literal adaptations of the retained Cocoa implementation
in each plugin wrapper. The DSP headers, parameter IDs/ranges, channel layouts
and versioned state field layouts are unchanged. The documentation and original
screenshots were used to check the feature inventory and graphics; the current
Cocoa source is authoritative where older screenshots show earlier layouts.

| Plugin | Retained controls and graphics |
| --- | --- |
| Surface Terrain 64 | Original sampled shell, shaded facets, path mesh, selected path and colored square source markers; PATH/FORM/SKIN/WARP/READ pages; playback/sync, logarithmic RATE/DIV, camera presets, rotate/zoom, display smoothing and mesh crossfade. |
| Wave Terrain 64 | Original faceted terrain, voice/scan contours, voice selection, topography and oscillator waveforms; all four terrain tabs; FIELD/ROTATE and curved/polygon control layouts; 102 scale choices with the original four-column order and stable scale IDs; LISTEN, MIDI, envelopes and projection. |
| Cartography 64 | Original authored/moving site map, listener, arrival/level/occlusion feedback, nine factory scenes and eight site engines; CORE/REL and MAP/PATH/FIELD pages; XY/XZ site/listener edits, camera rotation/zoom and saved camera conventions. |

References: `docs/ambi-encoder-surface-terrain.html`,
`docs/ambi-encoder-wave-terrain.html`, `docs/ambi-encoder-cartography.html`, and
their images in `docs/assets/plugin-guis/`.

## Shared foundation and threading

All three use the shared CLAP lifecycle, proportional 65–200% resizing,
privately loaded Fira Code with fallback, resource paths, native UTF-8 file
dialogs and custom dropdown painter. Plugin titles retain lowercase `s3g` and
uppercase names. Surface's narrow RATE/DIV readouts abbreviate units to `h/c`,
`m/c`, `s/c` and `bt` so the number is not clipped; host parameter text retains
the full units. The original control rectangles and slider hit maps are retained.

`s3g_map_encoder_editor.inc` centralizes gestures and preset file operations.
File recall decodes into a temporary candidate before changing live state and
preserves OUT, using the original `.s3gpreset` codecs and preset directories.
Original INIT/RANDOM/factory behavior and their OUT/ORDER/LISTEN preservation
rules remain intact. Queue capacity is reserved for compound edits and gesture
ends, including when a host rejects output events.

Surface retains its coalesced drag queue. Its original geometry sampling and
path construction run in a joined, latest-request worker; the GUI rasterizes
the ordered paths into a cached image and retains the 120 ms mesh crossfade.
No VSTGUI/platform drawing calls run on that worker or on the audio thread.

Wave uses its existing published control/voice snapshots. Cartography now
publishes fixed-size map snapshots; the audio publisher only tries a lock and
skips a contended publication, never waiting or allocating. GUI drawing and
state saving do not read the live DSP object. Active state replacements and
factory layout resets share the ordered GUI-to-audio command sequence.
Cartography's legacy struct-padding bytes are explicitly zeroed when saved,
making snapshots byte-reproducible without changing any serialized field,
offset, version, or size. Original states remain loadable by either editor.

## Build and test

On macOS enable `S3G_ENABLE_MAP_ENCODERS_VSTGUI_ON_MACOS=ON`; the default OFF
retains the Cocoa reference. Windows uses the portable editor when VSTGUI is
available. Existing `s3g_enable_vstgui_gui` helpers link and package resources.

```sh
cmake --build build-clap-sample-vstgui-fidelity --target \
  s3g_ambi_terrain_navigator_clap s3g_ambi_wave_terrain_encoder_clap \
  s3g_ambi_cartography_encoder_clap \
  s3g_map_surface_canvas_smoke s3g_map_wave_canvas_smoke \
  s3g_map_cartography_canvas_smoke
ctest --test-dir build-clap-sample-vstgui-fidelity \
  -R '^s3g_map_.*_canvas_smoke$' --output-on-failure
```

Set `S3G_MAP_CAPTURE_DIR` to save native offscreen PNG references. The macOS GUI
tests require window-server access. The tests use the actual canvas pointer
entry points, tabs, menu hit maps, slider ranges, factory scenes, Unicode preset
files, OUT preservation, chunked/truncated state streams, queue backpressure,
balanced gestures, resize/reparent/hide/destroy lifecycle and live audio.
Cartography also exercises authored site/listener drags, factory reset ordering,
concurrent snapshot/state reads during audio, and poisoned-padding state saves.

Verified 2026-09-11: all three native canvas suites passed. Independently loaded
Cocoa/VSTGUI binaries matched metadata, exchanged states in both directions,
and produced a maximum sample difference of **0** across three automated scenes
per plugin, including Wave MIDI events and nonzero output checks. Windows x64
cross-builds passed. Windows REAPER rendering remains a manual acceptance test.

The full menu repair rollout, its manifest, packaging and installation commands
are documented in `VSTGUI_MENU_PARITY.md`.
