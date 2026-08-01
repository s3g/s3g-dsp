# Four Intech Grid BU16 modules: No Input Mixer matrix

No Input Mixer accepts four 4-by-4 note grids as one velocity-sensitive 8-by-8
feedback matrix. The mapping works identically in the CLAP and standalone app.

Configure each BU16 to emit notes 0–15 from left to right and top to bottom,
with a distinct MIDI channel:

| BU16 | MIDI channel | Destination rows | Source columns |
| ---: | ---: | --- | --- |
| 1, upper left | 1 | 1–4 | 1–4 |
| 2, upper right | 2 | 1–4 | 5–8 |
| 3, lower left | 3 | 5–8 | 1–4 |
| 4, lower right | 4 | 5–8 | 5–8 |

## Ready-to-load Grid profiles

This directory includes four generated Grid Editor profiles:

| Position | Profile |
| --- | --- |
| Upper left | `nim_bu16_ch1_upper_left.json` |
| Upper right | `nim_bu16_ch2_upper_right.json` |
| Lower left | `nim_bu16_ch3_lower_left.json` |
| Lower right | `nim_bu16_ch4_lower_right.json` |

Load the matching profile onto each physical BU16 with Grid Editor. Keep each
module in its normal orientation so its buttons are numbered left-to-right,
top-to-bottom. Each press begins in the BU16's native velocity mode and sends
that measured strike velocity once. The held pad then switches to pressure
mode and emits continuous polyphonic-pressure updates. FLIP follows that
pressure through the shared DSP ramp. LATCH captures the native event, then uses
only the largest pressure value in a roughly 50 ms attack window to correct an
occasionally under-reported strike. Release emits Note Off and restores
velocity mode for the next strike.

The System element's Setup event installs the firmware 1.5.5+
`self.midirx_cb` callback used for LED feedback. If an earlier revision of
these profiles is already stored on the modules, reload all four generated
JSON files; the older MIDI-RX event form will send notes but will not update
the LEDs on current firmware.

No Input Mixer returns each matrix value as polyphonic pressure on the same
MIDI channel and note. Feedback value 64 is zero; 65–127 is positive and
63–0 is negative. The profiles turn zero off, fade positive magnitude in s3g
orange (`#c95e3b`), and fade negative magnitude in s3g cyan (`#57bfc4`). The processor sends effective ramped changes
and periodically refreshes the full matrix so controllers resynchronize after
reconnection.

The profiles are generated from one source. After editing the mapping, rebuild
and verify them with:

```sh
node controllers/intech_grid_bu16/generate_nim_profiles.mjs
node controllers/intech_grid_bu16/generate_nim_profiles.mjs --check
```

The `BU16 FLIP` and `BU16 LATCH` buttons at the top of the plugin's PATCH page
select two shared MIDI modes:

- **FLIP:** pressure moves an existing positive crosspoint toward `-1`, or an
  existing negative crosspoint toward `+1`. The shared ramp softens pressure steps
  and note-off restores the saved value. Empty points remain empty.
- **LATCH:** pressing an unlit cell creates it and captures the native strike
  velocity as its signed gain (`velocity / 127`). During the first roughly
  50 ms, a larger pressure sample may raise the captured magnitude; lower or
  later pressure cannot change it. This attack-peak correction is identical
  for positive and negative polarity. Release leaves the value latched.
  Pressing a lit cell again removes it; further messages from that same hold
  are ignored.

`BU16 RAMP` sets the shared Flip and Latch transition from `20` to `10000 ms`
and defaults to `1000 ms`. Latch creations and removals fade through the same
path used by Flip pressure. The plug-in and standalone save this setting, and
it is exposed as CLAP parameter and NRPN address `59`.

`NEW +` and `NEW -` choose the polarity of newly latched cells. This is the
stepped `BU16 New Sign` CLAP parameter at NRPN address `58`; the E16 ACTIONS
page toggles it with a press. FLIP and LATCH share one stored matrix, so mode
changes preserve every wire. LATCH edits are saved plug-in state. Clear Matrix
and Random act on that same matrix in either mode. PANIC only releases active
performance pressure and does not erase stored wires.

FLIP's held-pressure overlay does not rewrite plug-in state or automation;
LATCH intentionally edits matrix parameters. The active point is shown in the
WIRES and GRID views. MIDI channel 16 is deliberately excluded because it is
reserved for OXI E16 NRPN and No Input Mixer command notes.
