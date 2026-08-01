# OXI E16 scene: No Input Mixer 8ch

This directory contains the first performance-oriented E16 scene for the
No Input Mixer CLAP.

- Scene file for OXI's `Scenes` folder: `Scenes/NIM P2.oxie16`
- Identical compatibility copy: `s3g_no_input_mixer.oxie16`
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
| 1 | LIVE | Feedback, Coupling, Flow, Phase, Agency, Motion, Spread, Vortex | Formant, Space, Preset Variance/Response Mode, Aux A/B Return, Drift, Output |
| 2 | MOTION | Event Rate/Length, Density, Chaos, Slew, Choke, Field Rate/Phase | Response Depth/Threshold/Attack/Release, Behavior Depth, Field Shape, Behavior, Output |
| 3 | AUXTONE | Internal/House Tone, Aux A/B Type, Aux A Gain/Tone/Bias/Return | Aux B Gain/Tone/Bias/Return, Aux A/B Feedback, Ceiling, Output |
| 4 | ACTIONS | Record, Playback, Clear Last, Clear All, Cancel, BU16 Flip/Latch, Sign | Seed, Random Low/Mid/High, Forget, Matrix Clear, BU16 Ramp, Output/Panic |
| 5 | MATRIX1 | Matrix self-routes 1–8 | Forward ring routes 1→2 through 8→1 |
| 6 | MATRIX2 | Reverse ring routes 2→1 through 1→8 | Opposite-lane routes 1↔5, 2↔6, 3↔7, and 4↔8 |
| 7 | SENDS | Aux A lane sends `A1SD`–`A8SD` | Aux B lane sends `B1SD`–`B8SD` |
| 8 | RETURNS | Signed Aux A lane returns `A1RN`–`A8RN` | Signed Aux B lane returns `B1RN`–`B8RN` |
| 9 | MIXER | Lane 1–8 Level | Lane 1–8 Body |
| 10 | EQ LOHI | Lane 1–8 Low | Lane 1–8 High |
| 11 | EQ MID | Lane 1–8 Mid Frequency | Lane 1–8 Mid Gain |
| 12 | TUNING | Lane 1–8 Tune Note | Lane 1–8 Tune Cents |

All turns use 14-bit NRPN. The NRPN number is the stable CLAP parameter ID.
The E16 page supplies USB3 and MIDI channel 16; individual actions inherit
those settings. Page labels name the encoder-turn parameter only. In
particular, MOTION encoders 1–5 read `Event Rt`, `Event Ln`, `Density`,
`Chaos`, and `Slew`; their shared gesture presses are documented below rather
than appended to the dial names.

On SENDS, the first letter identifies the aux bus, the number identifies the
source lane, and `SD` means send. For example, `A1SD` is lane 1's Aux A send
and `B1SD` is lane 1's Aux B send.

RETURNS follows the same bus-first convention with `RN` for return. For
example, `A1RN` is lane 1's signed Aux A return and `B1RN` is lane 1's signed
Aux B return.

### Encoder presses

The four-character page label always names the **turn** action. A press can
perform a different command and does not change what turning that dial controls.

The first five press positions are identical on every page:

| Encoder | Press action |
| ---: | --- |
| 1 | NIM Gesture Record |
| 2 | NIM Gesture Play/Stop |
| 3 | Clear selected/last gesture loop |
| 4 | Clear all gesture loops |
| 5 | Cancel recording |

Additional presses are page-specific:

| Page | Encoder/turn label | Press action |
| --- | --- | --- |
| LIVE, MOTION, AUXTONE, ACTIONS | `OUT!` | Panic |
| MOTION | `BDEP` | Toggle Response direction between Normal and Invert |
| MIXER | Lane Level 6–8 | Toggle that lane's mute |
| MATRIX1 | `8>1` | Panic |
| AUXTONE | `AR/M`, `BR/M` | Toggle complete Aux A or Aux B mute |
| TUNING | Tune Note 6–8 | Toggle that lane's pitch lock |

The ACTIONS page is deliberately press-oriented:

| Encoders | Press actions |
| --- | --- |
| 1–5 | Record, Play/Stop, Clear Last, Clear All, Cancel |
| 6–8 | BU16 Flip, BU16 Latch, New Sign |
| 9–12 | Seed, Random Low, Random Mid, Random High |
| 13–14 | Forget, Clear Matrix (`MX0`) |
| 15 | No press; turn `RAMP` |
| 16 | Press Panic; turn `OUT!` |

Lane kills are omitted.

On AUXTONE, `ABIA` and `BBIA` are bipolar −1 to +1 third-parameter macros for
the selected Aux effect, not routing polarity. In the plugin or standalone app,
open **MIXER**, then click the Aux A or Aux B **EDIT** button. The third Effect
Editor row shows the current effect-specific name—Bias, Asymmetry, Direction,
Balance, Center, Regeneration, or another label—and its signed value. Routing
polarity is controlled by the signed matrix and the `A1RN`–`B8RN` return
encoders.

Field Shape, Behavior, Response Mode, and both Aux effect Type selectors are
mapped as stepped E16 turns. Quality, clock divisions, and insert Type remain
in the plug-in GUI. MOTION encoder 13 turns Behavior Depth at NRPN 60;
the existing Shape and Behavior encoders automatically include the appended
Bloom/Braid/Attract and Ratchet/Cascade/Erode choices, so the scene does not
need a new mapping.
pressing it toggles Response direction without disturbing that depth. BU16
mode and New Sign are deliberate press-control exceptions; continuous BU16
Ramp is on ACTIONS encoder 15 and remains available in the GUI, host automation,
or at NRPN 59. The three insert
pages from the first draft have been replaced by TUNING, signed per-lane
RETURNS, and a second performance matrix page.

## Import and host routing

1. Open the OXI App and choose or assign its computer storage directory.
2. Copy `Scenes/NIM P2.oxie16` into the `Scenes` subfolder inside
   that directory. The App does not list scene files placed directly at the
   storage directory root.
3. Open the E16 `Scenes` tab and refresh the computer-side scene list.
4. Drag `NIM P2` into an available E16 scene slot.
5. In REAPER, enable the E16 USB3 MIDI input and route channel 16 to the track
   containing No Input Mixer.
6. Keep the E16 scene's automatic parameter transmission off initially. Use
   `Send All` only when intentionally replacing the current plug-in settings.

Do not route the same channel-16 command stream to a musical instrument. The
notes used by encoder presses are control commands, not performance notes.

## NIM Gesture companion

The optional `s3g Utility NIM Gesture` MIDI-only CLAP is used in conjunction
with this E16 scene. It can record free-running E16 parameter loops and return
its live and played NRPN values to the E16 rings.
Place it before No Input Mixer and select E16 USB3 as the track's MIDI hardware
output. Keep E16 USB Thru off to prevent a feedback loop. See
[`docs/nim_gesture.md`](../../docs/nim_gesture.md) for routing and controls.

The No Input Mixer standalone already embeds NIM Gesture. Open **MIDI**, select
E16 USB3 as the feedback destination, and use **EDIT NIM GESTURES** for its
recorder. The same window lists the available CoreMIDI input sources; check
only the devices that should feed the embedded chain. The remembered list is
rescanned whenever the MIDI window opens. E16 USB Thru must still remain off.

No Input Mixer mirrors its current parameters as channel-16 NRPN on the final
MIDI output. Direct GUI and automation changes return on the next process
block. Activation, Random, factory-preset changes, and project restoration send
a throttled complete snapshot at sixteen parameters per block, allowing the
E16 rings to settle to the current patch without one large MIDI burst. Keep
E16 USB Thru off so this returned state does not re-enter the plugin chain.
