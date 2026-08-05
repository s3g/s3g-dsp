#include QMK_KEYBOARD_H
#include "keychron_common.h"

extern MidiDevice midi_device;

enum layers {
    BASE,
    FN,
    L2,
    L3,
};

enum custom_keycodes {
    NIM_RECORD = SAFE_RANGE,
    NIM_PLAY,
    NIM_CLEAR_LAST,
    NIM_CLEAR_ALL,
    NIM_CANCEL,
};

enum nim_protocol {
    NIM_COMMAND_CHANNEL = 15,
    NIM_FEEDBACK_CHANNEL = 14,
    NIM_RECORD_NOTE = 112,
    NIM_PLAY_NOTE = 113,
    NIM_CLEAR_LAST_NOTE = 114,
    NIM_CLEAR_ALL_NOTE = 115,
    NIM_CANCEL_NOTE = 116,
    NIM_COMMAND_COUNT = 5,
};

static const uint8_t nim_command_notes[NIM_COMMAND_COUNT] = {
    NIM_RECORD_NOTE,
    NIM_PLAY_NOTE,
    NIM_CLEAR_LAST_NOTE,
    NIM_CLEAR_ALL_NOTE,
    NIM_CANCEL_NOTE,
};

static bool nim_feedback_state[NIM_COMMAND_COUNT];
static bool nim_feedback_seen;
static uint32_t nim_feedback_timer;

// clang-format off
const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    [BASE] = LAYOUT_tenkey_27(
        KC_MUTE,         KC_ESC,          KC_DEL,        KC_TAB,       KC_BSPC,
        NIM_RECORD,      NIM_PLAY,        NIM_CLEAR_LAST,NIM_CLEAR_ALL,NIM_CANCEL,
        MC_2,            KC_P7,           KC_P8,         KC_P9,        KC_PPLS,
        MC_3,            KC_P4,           KC_P5,         KC_P6,
        MC_4,            KC_P1,           KC_P2,         KC_P3,        KC_PENT,
        MO(FN),          KC_P0,                          KC_PDOT),

    [FN] = LAYOUT_tenkey_27(
        RGB_TOG,         BT_HST1,         BT_HST2,       BT_HST3,      P2P4G,
        _______,         RGB_MOD,         RGB_VAI,       RGB_HUI,      _______,
        _______,         RGB_RMOD,        RGB_VAD,       RGB_HUD,      _______,
        _______,         RGB_SAI,         RGB_SPI,       KC_MPRV,
        _______,         RGB_SAD,         RGB_SPD,       KC_MPLY,      _______,
        _______,         RGB_TOG,                        KC_MNXT),

    [L2] = LAYOUT_tenkey_27(
        _______, _______, _______, _______, _______,
        _______, _______, _______, _______, _______,
        _______, _______, _______, _______, _______,
        _______, _______, _______, _______,
        _______, _______, _______, _______, _______,
        _______, _______,          _______),

    [L3] = LAYOUT_tenkey_27(
        _______, _______, _______, _______, _______,
        _______, _______, _______, _______, _______,
        _______, _______, _______, _______, _______,
        _______, _______, _______, _______,
        _______, _______, _______, _______, _______,
        _______, _______,          _______),
};

#if defined(ENCODER_MAP_ENABLE)
const uint16_t PROGMEM encoder_map[][NUM_ENCODERS][2] = {
    [BASE] = {ENCODER_CCW_CW(KC_VOLD, KC_VOLU)},
    [FN]   = {ENCODER_CCW_CW(RGB_VAD, RGB_VAI)},
    [L2]   = {ENCODER_CCW_CW(KC_VOLD, KC_VOLU)},
    [L3]   = {ENCODER_CCW_CW(RGB_VAD, RGB_VAI)},
};
#endif
// clang-format on

static uint8_t nim_feedback_index(uint8_t note) {
    if (note < NIM_RECORD_NOTE || note > NIM_CANCEL_NOTE) {
        return NIM_COMMAND_COUNT;
    }
    return note - NIM_RECORD_NOTE;
}

static void nim_update_feedback(uint8_t channel, uint8_t note, bool active) {
    if (channel != NIM_FEEDBACK_CHANNEL) {
        return;
    }
    const uint8_t index = nim_feedback_index(note);
    if (index >= NIM_COMMAND_COUNT) {
        return;
    }
    nim_feedback_state[index] = active;
    nim_feedback_seen = true;
    nim_feedback_timer = timer_read32();
}

static void nim_feedback_note_on(
    MidiDevice *device, uint8_t channel, uint8_t note, uint8_t velocity) {
    (void)device;
    nim_update_feedback(channel, note, velocity > 0);
}

static void nim_feedback_note_off(
    MidiDevice *device, uint8_t channel, uint8_t note, uint8_t velocity) {
    (void)device;
    (void)velocity;
    nim_update_feedback(channel, note, false);
}

void keyboard_post_init_user(void) {
    midi_register_noteon_callback(&midi_device, nim_feedback_note_on);
    midi_register_noteoff_callback(&midi_device, nim_feedback_note_off);
    rgb_matrix_enable_noeeprom();
}

bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    if (!process_record_keychron_common(keycode, record)) {
        return false;
    }
    if (keycode < NIM_RECORD || keycode > NIM_CANCEL) {
        return true;
    }

    const uint8_t note = nim_command_notes[keycode - NIM_RECORD];
    if (record->event.pressed) {
        midi_send_noteon(&midi_device, NIM_COMMAND_CHANNEL, note, 127);
    } else {
        midi_send_noteoff(&midi_device, NIM_COMMAND_CHANNEL, note, 0);
    }
    return false;
}

static void nim_set_state_led(uint8_t led, uint8_t red, uint8_t green,
    uint8_t blue, bool active, bool connected) {
    const uint8_t level = connected ? (active ? 255 : 24) : 7;
    rgb_matrix_set_color(led,
        ((uint16_t)red * level) / 255,
        ((uint16_t)green * level) / 255,
        ((uint16_t)blue * level) / 255);
}

bool rgb_matrix_indicators_user(void) {
    const bool connected = nim_feedback_seen
        && timer_elapsed32(nim_feedback_timer) < 2500;

    // Suppress the keyboard's ambient RGB animation. Only the five NIM state
    // keys below are allowed to illuminate in this dedicated controller image.
    for (uint8_t led = 0; led < RGB_MATRIX_LED_COUNT; ++led) {
        rgb_matrix_set_color(led, 0, 0, 0);
    }

    // LED indices 4–8 are the first complete five-key row below the knob.
    nim_set_state_led(4, 255,   0,   0, nim_feedback_state[0], connected);
    nim_set_state_led(5,   0, 220,  48, nim_feedback_state[1], connected);
    nim_set_state_led(6, 255, 136,   0, nim_feedback_state[2], connected);
    nim_set_state_led(7, 255,  32,   0, nim_feedback_state[3], connected);
    nim_set_state_led(8, 210,   0, 180, nim_feedback_state[4], connected);
    return true;
}
