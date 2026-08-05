# Keychron Q0 Max NIM controller

This directory contains a dedicated wired USB MIDI firmware for the encoder
variant of the Keychron Q0 Max. The first complete five-key row below the knob
controls NIM Gesture, from left to right:

| Key | MIDI command | LED |
| --- | --- | --- |
| Record | Channel 16, note 112 | Red |
| Play | Channel 16, note 113 | Green |
| Clear Last | Channel 16, note 114 | Amber |
| Clear All | Channel 16, note 115 | Orange-red |
| Cancel | Channel 16, note 116 | Magenta |

Each press sends Note On at velocity 127 and each release sends Note Off. NIM's
standalone app returns state on channel 15 using the same note numbers. An
active state is bright, an available but inactive state is dim, and all five
LEDs become very dim if no state heartbeat has arrived for 2.5 seconds. All
other LEDs remain dark; the stock ambient RGB animation is suppressed.

The firmware is for cable mode. Bluetooth and 2.4 GHz continue to act as
keyboard transports and do not carry USB MIDI. Keychron Launcher/VIA is
disabled in this image to leave a USB endpoint for MIDI, so install Keychron's
factory firmware to restore Launcher/VIA use.

## Files

- `qmk/keymap.c`: NIM MIDI mapping and RGB feedback behavior.
- `qmk/config.h`: QMK MIDI configuration.
- `qmk/rules.mk`: build features; USB MIDI on, Raw HID and VIA off.
- `firmware/keychron_q0_max_encoder_nim.bin`: compiled image for the Q0 Max
  encoder model only.

Compiled firmware SHA-256:

```text
f02cf6bd7906764d7806d1cb2a61bfc0e786ee74f01526a883181cd29d240132
```

## Standalone app routing

1. Connect the Q0 Max by USB and set its rear switch to Cable.
2. Open the No Input Mixer standalone app's **MIDI** window.
3. Enable **Keychron Q0 Max** in the input-source list.
4. Choose **Keychron Q0 Max** for **GESTURE KEYS** output.

The first feedback snapshot should illuminate the five keys within one second.
Record and Cancel are both bright during a take. Clear Last becomes bright
when a last-touched loop is available; Clear All becomes bright when any loop
exists.

## Flashing

Keep a copy of Keychron's factory Q0 Max encoder firmware before changing the
device. To enter the bootloader, unplug USB, set the switch to Cable, hold the
`O` key (the keypad's letter-O-position key) or the reset button underneath
`0`, and reconnect USB. Then flash
`firmware/keychron_q0_max_encoder_nim.bin` with QMK Toolbox. Do not select a
different Q0/Q0 Max variant.

The same bootloader procedure remains available after installing this image.
Use it with the official factory Q0 Max encoder `.bin` to recover the stock
firmware.

## Rebuilding

The checked-in image was built from Keychron's `wireless_playground` branch at
commit `666862cb8123b64a6b96718d739c6203ad99031f`:

```sh
cp -R qmk keychron-qmk/keyboards/keychron/q0_max/encoder/keymaps/nim
cd keychron-qmk
qmk compile -kb keychron/q0_max/encoder -km nim
```

The build must report `MIDI_ENABLE`; it must not report `RAW_ENABLE`,
`VIA_ENABLE`, or `KEYBOARD_SHARED_EP`.
