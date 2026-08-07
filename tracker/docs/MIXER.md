# Performance mixer

The main workspace has a dedicated performance-mixer page beneath the
transport and live-code entry. The console readout remains in the right
toolbox. Open the mixer with the toolbar `MIXER` button or
`Command-6`; the button becomes `TRACKER` while the mixer is visible.
The page is horizontally scrollable, keeps transport and command entry in
reach, and presents every current sequence track (up to 32) plus a distinct
`MAIN OUT` strip.

## Track strips

The current audio graph does not yet expose a separate rendered audio bus for
each tracker lane. Track-strip controls therefore describe exactly what they
change:

| Control | Current behavior |
| --- | --- |
| `VEL / INPUT` | Multiplies each new Note On's normalized velocity before the canonical event is fanned to MIDI and internal audio. It is an excitation/input trim, not a post-instrument volume fader; it can change membrane timbre and does not attenuate an already-ringing tail. The UI floor is MIDI velocity 1/127; use NOTE mute for silence. |
| `M` | Mutes that lane's NOTE onsets. INS, VOL, and FX polymeters continue to advance. |
| `S` | Temporarily NOTE-mutes every other lane. Switching or clearing solo preserves the NOTE mute state that existed before solo. |
| Instrument (`KCK`, `SMP`, `MID`, etc.) | Cycles through populated song instruments. Internal/MIDI destination follows the selected index; there is no hidden Both mode. Timestamp-safe cleanup still applies. |
| `STEP ACT` | Shows whether the current NOTE playhead row contains an onset/retrigger. It is intentionally not labelled or scaled as an audio meter. |

The strip also reports the track name, MIDI channel, derived destination, and
either its default instrument or `INS SEQ` when per-row overrides are present.
The persistent right toolbox offers direct indexed selection. Mixer and toolbox
edits publish the same pattern model used by the tracker grid and command
engine.

## MAIN OUT

`MAIN OUT` is a real post-decode internal-audio bus. Its fader and mute
control a realtime-safe normalized gain mailbox in the audio engine. Gain
changes use an approximately 5 ms render-thread ramp and apply equally to the
tested stereo and quad layouts. The strip's peak bar is the aggregate rendered
hardware-output peak.

Timestamped MIDI does not pass through this audio bus. The strip says
`MIDI BYPASSES MAIN OUT` so muting MAIN OUT cannot be mistaken for a MIDI
panic or Superior Drummer volume control.

## Keyboard operation

- Left/Right or Tab selects a track strip or MAIN OUT.
- Up/Down adjusts the selected trim; Shift makes fine changes.
- `M` toggles mute, `S` solos a track, and `R` cycles its default instrument.
- Space toggles playback.

Mouse, keyboard, and accessibility selection share the light-gray focus outline.

## Next audio-mixer boundary

True per-track audio faders, pan, inserts, sends, and level meters require
lane-isolated render buses. The present internal rack groups events by
instrument target and sums those outputs before decode, so a single node can
be excited by several tracker lanes. The future graph should
render lane buses explicitly, then combine them into layout-aware
stereo/quad/discrete/ambisonic masters. Until that boundary exists, the UI
must not disguise velocity or step activity as audio gain or dB metering.
