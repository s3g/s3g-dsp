# Sample-family GUI refinement checks

The Cocoa implementations and the user guides are the reference for both
behavior and layout. Fira Code, proportional 65–200% scaling, and shared GUI
titles (lowercase `s3g`, uppercase plugin name, `#D3D3D3`) are the accepted
cross-platform changes. Use foundation `drawPluginTitle` only at paint time;
host names, IDs, preset names, and resource paths retain their original spelling.
Keep visualization-specific behavior in the relevant
plugin bridge; the common editor should consume the same published DSP state
as the Cocoa view.

| Area | Reference | Required behavior |
| --- | --- | --- |
| Cutups | [Guide](../../docs/sample-cutups.html), `s3g_sample_lanes_gui.inc` Cutups variant | Step Repeat is an integer slider (1–16), draggable in both directions within one automation gesture. |
| Motion | [Guide](../../docs/sample-motion.html), `drawScope` in `s3g_sample_motion_gui.inc` | Scope uses the newest live voice's motion, inner, or outer phase according to Sound. Path, source relationship, sound, and voice count update from state. Scope is a strip with a left title, separate graph, original trajectory orientation, envelope, and clock ticks. |
| Wavesets | [Guide](../../docs/sample-wavesets.html), Cocoa waveset scope | Source/result overlays remain distinct. The requested source trace is now lighter gray (`0xa0a0a0`). |
| Lanes | [Guide](../../docs/sample-lanes.html), Cocoa `drawWaveform` | Apply `laneSourcePhase` to the waveform and `laneTimelinePhase` to live cursors. Wrap Nudge rotates content within S–E; markers remain fixed. Split waveform accumulation at the wrap seam. |
| Doubles | [Guide](../../docs/sample-doubles.html), [engineering notes](../clap_sample_doubles/ENGINEERING.md) | Cyan A (`0x69d2dc`) and orange B (`0xff7047`) cursors, outlined transport controls, active deck indicators, LINK toggle, source/storage/BPM status, and working ½/AUTO/×2. Keep the retained tempo candidate after manual edits; AUTO can re-analyze recalled audio. Windows analysis runs off the UI/audio threads and preserves intervening manual BPM edits. |
| Circulator | [Guide](../../docs/sample-circulator.html), Cocoa `drawLoopWaveforms` | Separate normalized grayscale L/R envelopes for each loop. A/B load text matches the original slot colors. Manual mix position lives in the left vertical field; its gradient derives from the actual fade-shape gains. Keep direction arrow, S/E editing, queued-window markers, and Classic A's half-source mapping. |
| Rings | [Guide](../../docs/sample-rings.html), `s3g_sample_rings_gui.inc` | Selected-slot LOAD/CLEAR, capture-target selection, polar head editing, Option/Alt mute, and solo-mask restoration. Preserve concentric channel rings, peak/RMS envelopes, formation groups, muted-head visibility, dual read dots, and paused routing preview. |
| Shared controls | Cocoa `drawControl`/`drawSlider` and per-plugin conditional presentation | Center labels and values on tracks; center menu labels on menus. Keep original disabled behavior and manual emphasis. Preserve control bounds and typography; do not substitute new explanatory layouts. |
| Sample lifecycle | Per-plugin workers, `s3g_sample_storage.h` | Windows uses the same import/analysis lifecycle and PROJECT/LINK services. Preserve selected-lane reanalysis and consecutive multi-file drops. Cancel stale loads on clear/restore. UTF-8 locators must survive filesystem and project-state boundaries. |

See [audit remediation](SAMPLE_GUI_AUDIT_WORK.md) for implementation status and
the explicit boundaries of automated versus hosted verification.

## Automated acceptance

`tests/encoder_family_gui_smoke.mm` exercises the VSTGUI canvas through native
mouse events and public CLAP parameters. The portable Sample-family branch checks:

- Bidirectional Repeat dragging and one balanced automation gesture.
- LINK toggling and BPM octave buttons.
- AUTO analysis of an embedded 120 BPM pulse fixture, then retained-candidate recall.
- Deck A's rendered cursor staying fixed while Deck B advances with LINK off.
- Motion's source text changing with Sound, and its scope changing as DSP runs.
- Lanes' waveform changing under Wrap Nudge in a crop that excludes the seam.
- Circulator's manual field, gain-law gradient, opposite-polarity stereo capture,
  and independent loop-window marker drags.
- Rings selected C CLEAR preserving A/B/D, Option-mute, solo transfer/restoration,
  and balanced polar-edit automation gestures.
- Doubles and Wavesets native motion animations remain installed across unchanged
  redraws, including when no new DSP snapshot arrives.

`tests/sample_family_interaction_smoke.cpp` drives the actual portable canvas with
test-owned callbacks. It covers consecutive multi-file drops (including Unicode
paths), selected-slot clear/recalculation, disabled-control rejection, balanced
gestures, numeric readout fitting, and direct Doubles cue dragging without
opening system dialogs.
`tests/sample_cursor_clock_smoke.cpp` covers forward/reverse/loop/ping-pong motion,
pause and discontinuities, stable publications, and Motion voice smoothing.
`tests/sample_player_clap_smoke.cpp` exercises project collection/registration and
relocation under a Unicode project root.

These checks supplement CLAP/DSP smoke tests and reference captures. Windows
cross-compilation and package validation establish build/resource compatibility;
hosted Windows REAPER interaction and presentation still require a machine test.
