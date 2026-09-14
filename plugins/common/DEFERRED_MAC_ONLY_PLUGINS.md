# Deferred platform adaptations

Release decision, 2026-09-11: Tracker and Ambi Energy 64 remain macOS-only for
the immediate future. Their completed Windows adaptations belong to a later
release and are excluded from the current Windows package acceptance set.
The canonical active Mac plugin manifest continues to include both products.

- Tracker update, 2026-09-13: all ten portable VSTGUI pages, Win32 hosting,
  platform services and the shared MIDI engine now link as a Windows x64 CLAP
  test build. Native Windows/REAPER execution is still pending. See
  `TRACKER_MAC_VSTGUI_MIGRATION.md` and `TRACKER_WINDOWS_README.txt`.
  This dedicated test package is not yet in the accepted Windows release set.
- Ambi Energy 64: preserve the current Metal-based analysis/visualization.
  A future release needs a tested portable rendering/analysis backend.

Do not replace either GUI with a generic parameter panel or distribute a
headless Windows build as a completed port. Existing Mac builds remain valid.

Ambi Energy now has a separate, non-release GPU parity prototype under
`tests/ambi_energy_renderer` (Metal reference and Windows D3D11/HLSL replay).
This does not enable a Windows CLAP or migrate the GUI. Native Windows renderer
validation must precede the full VSTGUI/host integration described there.
