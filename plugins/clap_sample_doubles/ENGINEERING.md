# Sample Doubles engineering notes

This file records implementation constraints and future work that should not
be presented as current user-facing behavior. The shipped controls and MIDI
map remain documented in [README.md](README.md) and
[the user guide](../../docs/sample-doubles.html).

## Resolved: playheads freezing after a GUI gesture

Status: fixed on macOS, 2026-08-19.

In live REAPER testing, releasing the crossfader could leave both playheads
visually stationary for roughly 0.30–0.43 seconds before they jumped to their
correct positions. Audio continued normally. The unrelated peak readout froze
for the same interval, which was the important clue: this was not a stalled
sample engine or an incorrect read-head position. The entire embedded AppKit
view had temporarily stopped presenting after the parameter gesture ended.

Sample Doubles exposed this more readily than the earlier Sample instruments
because its two continuously moving read heads are watched while another
continuous control is dragged and released. Those instruments can retain their
ordinary AppKit redraw paths unless the same hosted presentation failure is
demonstrated; their DSP is not the source of this difference.

The following approaches did not solve the actual failure and must not become
the primary on-screen playhead path again:

- changing the GUI timer between 30 and 60 Hz;
- adding the timer to common run-loop modes;
- invalidating either the waveform alone or the full view every tick;
- advancing a wall-clock cursor and drawing it in `drawRect:`; or
- moving an overlay view or layer from each timer callback.

Wall-clock interpolation is still required because an anticipative host may
render a burst and then publish no new DSP position while buffered audio is
heard. It cannot, by itself, make AppKit present a frame during a host/UI gap.

### Required presentation contract

The working implementation is the non-interactive
`S3GSampleDoublesCursorView` in
[s3g_sample_doubles_clap.cpp](s3g_sample_doubles_clap.cpp). It converts a
coherent DSP snapshot into two persistent, linear `CABasicAnimation`
trajectories. Once committed, WindowServer advances those trajectories without
requiring another AppKit timer or paint pass.

Preserve these invariants:

- Publish each cursor snapshot coherently: asset identity, both positions,
  resolved bounds, sample-domain rates, active mask, playing/loop state, and a
  discontinuity serial must describe the same engine state.
- Snap and replace a trajectory for an asset, geometry, transport, or other
  hard discontinuity. For a rate-only change, preserve the current phase and
  change the slope.
- Do not reinstall unchanged animations because a crossfader, gain, meter,
  menu, mouse, MIDI, or ordinary repaint event occurred.
- Preserve phase analytically from the animation contract. Client-side
  `presentationLayer` state can itself be stale during the gap this design is
  intended to survive.
- Keep the cursor view hit-test transparent, clipped to the waveform, and
  masked under an open menu.
- Keep the non-screen `drawRect:` playhead fallback for documentation/PDF
  rendering, but do not draw a second stationary playhead on screen.
- The 30 Hz common-mode timer may service loading, meters, and ordinary control
  feedback. Visible playhead motion must not depend on that timer.

The contract currently models predictable, positive, piecewise-linear motion.
Reverse, ping-pong, scratching, time-stretch, granular, or discontinuously
modulated heads would need an explicit extension rather than an approximation
hidden inside the current animation.

### Regression rule

The Sample Doubles section of
[encoder_family_gui_smoke.mm](../../tests/encoder_family_gui_smoke.mm) must
continue to cover anticipative render-ahead and the crossfader mouse-up
transition. With DSP publication deliberately held fixed, visual time must
advance; both animations must remain active; and an ordinary mouse-up must not
increase the animation-install count.

That in-process smoke verifies the clock and animation contract, but it cannot
prove that WindowServer presented intermediate frames while the same process's
main thread was blocked. For changes to this path, repeat the hosted acceptance
test: block AppKit for at least 650 ms immediately after crossfader mouse-up and
capture the actual window pixels from another thread/process. The accepted fix
showed both playheads moving on every captured transition with no stationary
run. Sampling `presentationLayer` is not a substitute for the pixel test.

## Implemented performance contract

Status: shipped in the development build, 2026-08-19.

### Independent decks and mixer

Deck A Level and Deck B Level are continuous CLAP parameters applied before
the crossfader with 5 ms audio-thread smoothing. `Out` remains the smoothed
post-crossfade master. The new parameter IDs follow the original twelve; they
were not inserted into or allowed to renumber the established parameter map.

Each deck has a Play/Pause action. Link defaults on and makes either per-deck
action operate both heads, preserving the earlier two-deck transport. With
Link off, the active states are independent and are included in the coherent
cursor publication so a paused deck's compositor trajectory stops too.

Drag A and Drag B are momentary platter gestures. A held gesture ramps that
deck toward a 0.16 rate multiplier over 30 ms; release lets the motor recover
to its normal rate over 220 ms. The audio engine owns the ramps. GUI mouse-up,
MIDI note-off, editor close, activation/reset, and transport lifecycle handling
must continue to prevent a latched drag.

The GUI publishes action pulses and held Punch/Drag state from the same command
path used by MIDI. Transport, sync, step, per-deck toggles, and offset-select
notes therefore provide visible feedback even without a mouse gesture. Routine
feedback repainting must not reinstall the compositor cursor animations.

`B Offset` retains its original latched behavior: Restart, Sync, or an offset
note applies it. The separate `B Live Phase` parameter moves Deck B immediately
by the parameter delta, preserving old-session semantics while providing a
precise automatable phase gesture.

State version two stores sixteen parameters. The version-one reader still
accepts the original twelve-parameter layout and supplies neutral A/B levels,
Link on, and zero live phase. Keep this migration test whenever state changes.

### Load-time source BPM estimation

The platform-neutral estimator in
[`s3g_sample_tempo_estimator.h`](../../dsp/s3g_sample_tempo_estimator.h) runs
only on the sample-loader worker after decoding. It chooses the strongest
channel to avoid stereo phase cancellation, derives a 200 Hz log-energy onset
envelope, combines normalized autocorrelation with a beat-grid score, refines
the winning lag, and compares half/double candidates over 50–220 BPM.

It returns BPM, confidence, validity, and octave ambiguity. Short, silent, and
non-periodic input is rejected. A fresh load automatically applies only an
unambiguous estimate with confidence at least 0.62. Other valid estimates are
shown as suggestions with `1/2`, `Auto`, and `x2` choices. `Sample BPM` remains
the authoritative editable value.

Each load records the BPM parameter revision at request time. A completed
worker result may update or suggest tempo only when that revision still
matches, so a manual edit wins over stale analysis. Project restore marks the
tempo as restored, preserves it exactly, and does not synchronously re-analyze
embedded or path-based state.

The accepted BPM feeds the existing source-domain beat formula:

```text
beat frames = source sample rate × 60 / source BPM
```

Offset, phase step, and live phase all use that distance. Varispeed changes how
long and at what pitch those source frames are heard; it does not redefine the
source beat grid.

### MIDI, Tracker, and phase-music workflow

Notes 44/45 toggle Deck A/B and gated notes 46/47 operate Drag A/B. The fixed
continuous map is CC16 Crossfader, CC17 Deck A Level, CC18 Deck B Level, and
CC19 B Live Phase. These controls remain ordinary automatable CLAP parameters;
the direct CC path is an additional raw MIDI/Tracker route.

The note-command keyboard, velocity-sensitive Punch A/B, and beat-valued
offset notes remain the core Tracker workflow. Tempo estimation changes the
source-frame distance of phase commands, not the time when Tracker emits them.
The Reich-inspired gradual and stepped settings in the user guide remain the
starting configurations; named phase presets are possible future convenience,
not a separate DSP mode.

A dub-print recorder remains intentionally out of scope. The design centers on
live or automated cutting between two copies, with authentic varispeed
supplying the slowed result.

### Regression coverage

The core smoke covers independent pre-fader levels, linked and unlinked deck
transport, the Drag motor, exact live-phase displacement, tempo fixtures at
multiple BPM values, stereo anti-phase input, and silence rejection. The CLAP
smoke covers the appended parameter and note maps, fixed MIDI CC conversion,
version-one migration, and version-two save headers. The GUI smoke exercises
all continuous controls and menus, linked state, tracking-mode transitions,
render-ahead, and the compositor contract described above.
