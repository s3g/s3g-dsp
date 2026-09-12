# Deferred platform adaptations

Release decision, 2026-09-11: Tracker and Ambi Energy 64 remain macOS-only for
the immediate future. Their completed Windows adaptations belong to a later
release and are excluded from the current Windows package acceptance set.
The canonical active Mac plugin manifest continues to include both products.

- Tracker: the Mac-first VSTGUI drawing pilot is now opt-in, with native editing
  and other pages retained. See `TRACKER_MAC_VSTGUI_MIGRATION.md` for the staged
  plan and exact coverage. Windows still needs a dedicated application-scale
  GUI/services port; this pilot does not constitute a Windows adaptation.
- Ambi Energy 64: preserve the current Metal-based analysis/visualization.
  A future release needs a tested portable rendering/analysis backend.

Do not replace either GUI with a generic parameter panel or distribute a
headless Windows build as a completed port. Existing Mac builds remain valid.

Ambi Energy now has a separate, non-release GPU parity prototype under
`tests/ambi_energy_renderer` (Metal reference and Windows D3D11/HLSL replay).
This does not enable a Windows CLAP or migrate the GUI. Native Windows renderer
validation must precede the full VSTGUI/host integration described there.
