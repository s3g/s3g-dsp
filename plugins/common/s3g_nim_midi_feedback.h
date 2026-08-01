#pragma once

#include <clap/clap.h>

// Optional private extension used by the NIM standalone host. Ordinary CLAP
// hosts do not need to implement or call it; both feedback streams remain
// enabled by default for them. A host which routes the two streams to
// independent MIDI destinations can disable either producer at its source.
#define S3G_NIM_MIDI_FEEDBACK_EXTENSION_ID \
    "org.s3g.no-input-mixer-midi-feedback"

typedef struct s3g_nim_midi_feedback {
    void (*set_enabled)(const clap_plugin_t* plugin,
        bool nrpn_enabled, bool matrix_enabled);
} s3g_nim_midi_feedback_t;
