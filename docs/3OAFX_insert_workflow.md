# 3OAFX Insert Workflow

The first native plugin tests are designed for the 24-channel virtual speaker
layer used by the `s3g-mc` 3OAFX workflow.

Related `s3g-mc` docs:

- [s3g-mc 3OAFX process guide](https://s3g.github.io/s3g-mc/process-guides-3oafx.html)
- [3OAFX Send Return Controller](https://s3g.github.io/s3g-mc/process-guides-3oafx.html#3oafx-send-return-controller)
- [s3g-mc tools overview](https://s3g.github.io/s3g-mc/tools.html)

Typical signal path:

```text
3OA source
-> s3g 3OAFX Send Decoder
-> 24-channel native insert plugin
-> s3g 3OAFX Return Encoder
-> 3OA output
```

`s3g 3OAFX Send Decoder` decodes a 3OA `ACN/SN3D` input to the fixed
24-point virtual speaker layer and emits a 72-channel bus:

- `1-24`: wet/effect lane
- `25-48`: decoded dry copy
- `49-72`: mask signal

`s3g 3OAFX Return Encoder` reads the same 72-channel bus, applies the return
mask/mixer, and encodes the result back to 3OA `ACN/SN3D`.

The mask signal on `49-72` is the audio-rate sync path between the two boundary
plugins. REAPER parameter links are still recommended so the Return Encoder GUI
and independent-return controls visibly follow the Send Decoder.

## REAPER Parameter Links

After inserting the native boundary pair, link the Return Encoder direction
parameters to the Send Decoder:

1. Put the plugins on a 72-channel REAPER track in this order:

   ```text
   s3g 3OAFX Send Decoder
   -> 24-channel insert effect
   -> s3g 3OAFX Return Encoder
   ```

2. Open `s3g 3OAFX Return Encoder`.
3. Use REAPER's `Param` menu for `Azimuth`, then choose
   `Parameter modulation/MIDI link`.
4. Enable parameter linking from another FX parameter.
5. Set the source FX to `s3g 3OAFX Send Decoder`.
6. Set the source parameter to `Azimuth`.
7. Use a neutral 1:1 scale and offset.
8. Repeat the same link for `Elevation`.

Useful optional links:

- `Width` -> `Width`
- `Focus` -> `Focus`
- `Rear Reject` -> `Rear Reject`

When these links are active, moving the Send Decoder's direction controls should
also move the Return Encoder's custom GUI. If REAPER has an older plugin bundle
loaded in memory, reload the plugin instances or restart REAPER after
reinstalling the CLAP bundle.

The initial plugin, `s3g 24ch Passthrough Test`, should not change the spatial
image except for its gain parameter. Its purpose is to confirm that REAPER and
the selected plugin format expose and preserve 24-channel audio correctly.

This passthrough has been confirmed to work as a 24-channel CLAP in REAPER.

The general plugin direction is fixed-width where REAPER pin behavior needs to
be predictable. `s3g Delay Processor 24ch` is the current wide insert effect for
testing this boundary.

Future 3OAFX-specific processors should keep the fixed 24-in/24-out shape when
they sit directly on the virtual speaker insert layer, where channel count and
speaker order are part of the musical contract.
