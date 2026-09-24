# One Intech Grid BU16: Sample Kit 4x4 pads

`sample_kit_bu16_4x4.json` is a ready-to-load Grid Editor profile for one
BU16 module controlling all sixteen `s3g Sample Kit 32` pads.

The profile sends velocity-sensitive MIDI Note On and Note Off messages on
MIDI channel 1. It matches the on-screen MPC orientation instead of the
BU16's raw element order:

| Physical row | Sample Kit pads | MIDI notes |
| --- | --- | --- |
| Top | 13–16 | 48–51 |
| Upper middle | 9–12 | 44–47 |
| Lower middle | 5–8 | 40–43 |
| Bottom | 1–4 | 36–39 |

Pads glow muted teal at rest and s3g orange while held. The light response is
local to the controller, so it does not require MIDI feedback from the plug-in.
Native BU16 strike velocity is expanded by 2.5× and clipped at MIDI 127 before
it is sent, making the controller's normally low strike values cover a useful
drum-performance range. Continuous pressure is deliberately not sent because
Sample Kit consumes note velocity, not polyphonic pressure. Note Off is
retained for Gate and Toggle workflows.

## Load the profile

1. Update the BU16 with Grid Editor and keep it on firmware 1.5.7 or newer.
2. Import `sample_kit_bu16_4x4.json` into Grid Editor.
3. Load the profile onto one BU16 user page.
4. Route Grid MIDI channel 1 to the Sample Kit track.
5. Leave Sample Kit `BASE NOTE` at its default `36`. Set `MIDI RECEIVE` to
   `OMNI` or `1`. Start with the plug-in's `VELOCITY CURVE` at `LINEAR`; use
   `SOFT` or `VERY SOFT` only if more response is still needed.

This is a Grid Editor configuration profile, not a replacement for Intech's
base device firmware.

The profile is generated from one source. After changing its mapping, rebuild
and verify it with:

```sh
node controllers/intech_grid_bu16/sample_kit/generate_sample_kit_profile.mjs
node controllers/intech_grid_bu16/sample_kit/generate_sample_kit_profile.mjs --check
```
