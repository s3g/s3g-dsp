MIDI_ENABLE = yes

# The dedicated NIM image uses its compiled keymap directly. Disabling Raw HID
# and VIA also frees the third STM32F401 USB endpoint required by USB MIDI.
RAW_ENABLE = no
VIA_ENABLE = no
