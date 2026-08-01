#pragma once

#include <clap/clap.h>
#include <clap/ext/state.h>

// Private extension shared by the NIM Gesture CLAP and the standalone host.
// The stream contains one complete portable .nimgesture file. Loading is
// transactional: a malformed stream leaves the current gesture set untouched.
// A successful load replaces every loop and always leaves Record and Play off.
// Save fails while a take is being recorded. Clear removes committed and
// in-progress motion and also leaves Record and Play off. These calls are safe
// while processing; the gesture processor never blocks its audio callback.
#define S3G_NIM_GESTURE_SESSION_EXTENSION \
    "org.s3g.s3g-dsp.nim-gesture-session/1"

typedef struct s3g_nim_gesture_session {
    bool (CLAP_ABI *save)(const clap_plugin_t* plugin,
        const clap_ostream_t* stream);
    bool (CLAP_ABI *load)(const clap_plugin_t* plugin,
        const clap_istream_t* stream);
    bool (CLAP_ABI *clear)(const clap_plugin_t* plugin);
} s3g_nim_gesture_session_t;
