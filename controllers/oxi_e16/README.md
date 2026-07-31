# OXI E16 scene: No Input Mixer 8ch

This directory contains the first performance-oriented E16 scene for the
No Input Mixer CLAP.

- Scene file: `s3g_no_input_mixer.oxie16`
- Reproducible source and validator: `generate_no_input_mixer_scene.mjs`
- MIDI route: E16 USB3, channel 16
- Scene title: `NIM P2`

The generated `.oxie16` file is minified JSON in the native OXI App 1.5 scene
format. Its import has been verified with the official app. Regenerate it
after changing the layout:

```sh
node controllers/oxi_e16/generate_no_input_mixer_scene.mjs
node controllers/oxi_e16/generate_no_input_mixer_scene.mjs --check
```

## Page layout

The encoders are numbered left-to-right, top-to-bottom across the physical
4-by-4 grid.

| Page | Title | Encoders 1–8 | Encoders 9–16 |
| ---: | --- | --- | --- |
| 1 | LIVE | Feedback, Coupling, Flow, Phase, Agency, Motion, Spread, Vortex | Formant, Space, Surface X/Y, Aux A/B Return, Drift, Output |
| 2 | MOTION | Event Rate/Length, Density, Chaos, Slew, Choke, Movement Rate/Phase | React Depth/Threshold/Attack/Release/Polarity, Flow, Agency, Output |
| 3 | MIXER | Lane 1–8 Level | Lane 1–8 Body |
| 4 | MATRIX1 | Matrix self-routes 1–8 | Forward ring routes 1→2 through 8→1 |
| 5 | MATRIX2 | Reverse ring routes 2→1 through 1→8 | Opposite-lane routes 1↔5, 2↔6, 3↔7, and 4↔8 |
| 6 | SENDS | Lane 1–8 Aux A Send | Lane 1–8 Aux B Send |
| 7 | AUXTONE | Internal/House Tone, Formant, Space, Aux A Gain/Tone/Bias/Return | Aux B Gain/Tone/Bias/Return, Aux A/B Feedback, Ceiling, Output |
| 8 | EQ LOHI | Lane 1–8 Low | Lane 1–8 High |
| 9 | EQ MID | Lane 1–8 Mid Frequency | Lane 1–8 Mid Gain |
| 10 | TUNING | Lane 1–8 Tune Note | Lane 1–8 Tune Cents |
| 11 | RETURNS | Lane 1–8 signed Aux A Return | Lane 1–8 signed Aux B Return |
| 12 | ACTIONS | New, Random Low/Mid/High, Forget, Panic, Clear Matrix, Output | Kill lanes 1–8 |

All turns use 14-bit NRPN. The NRPN number is the stable CLAP parameter ID.
The E16 page supplies USB3 and MIDI channel 16; individual actions inherit
those settings.

### Encoder presses

- Pages 1–11, encoders 1–5: NIM Gesture Record, Playback, Clear Last,
  Clear All, and Cancel Recording. These stable assignments override the older
  lane-level mute and Tune Note press shortcuts in those five positions.
- LIVE, MOTION, and AUXTONE Output: Panic.
- MIXER lane Level 6–8: toggle the corresponding lane mute.
- MATRIX1 route 8→1: Panic.
- AUXTONE Aux A/B Return: toggle the corresponding complete aux mute.
- TUNING Tune Note 6–8: toggle the corresponding lane pitch lock.
- ACTIONS uses clearly labelled push-only controls for New, three Random
  energy levels, Forget, Panic, Clear Matrix, and lane kills.

Categorical selectors such as Behavior, movement/react shape, Quality, aux
effect Type, clock divisions, and insert Type remain in the plug-in GUI. No
menu-valued parameter is assigned to an E16 turn encoder. The three insert
pages from the first draft have been replaced by TUNING, signed per-lane
RETURNS, and a second performance matrix page.

## Import and host routing

1. Open the OXI App and choose or assign its computer storage directory.
2. Copy `s3g_no_input_mixer.oxie16` into the `Scenes` subfolder inside that
   directory. The App does not list scene files placed directly at the storage
   directory root.
3. Open the E16 `Scenes` tab and refresh the computer-side scene list.
4. Drag `NIM P2` into an available E16 scene slot.
5. In REAPER, enable the E16 USB3 MIDI input and route channel 16 to the track
   containing No Input Mixer.
6. Keep the E16 scene's automatic parameter transmission off initially. Use
   `Send All` only when intentionally replacing the current plug-in settings.

Do not route the same channel-16 command stream to a musical instrument. The
notes used by encoder presses are control commands, not performance notes.

## NIM Gesture companion

The optional `s3g Utility NIM Gesture` MIDI-only CLAP can record free-running
parameter loops and return its live and played NRPN values to the E16 rings.
Place it before No Input Mixer and select E16 USB3 as the track's MIDI hardware
output. Keep E16 USB Thru off to prevent a feedback loop. See
[`docs/nim_gesture.md`](../../docs/nim_gesture.md) for routing and controls.

The No Input Mixer standalone already embeds NIM Gesture. Open **MIDI**, select
E16 USB3 as the feedback destination, and use **EDIT NIM GESTURES** for its
recorder. Input from all CoreMIDI sources present at application launch feeds
the embedded chain. E16 USB Thru must still remain off.

Direct changes made in the No Input Mixer GUI, factory-preset changes, and
project restoration are not yet translated into E16 feedback. Preset loading
and `Send All` should still be treated as explicit state changes.
