#pragma once

#define MIDI_ADVANCED

// The second key in the command row is normally Num Lock. Its firmware LED
// indicator would overwrite the NIM Play color after our RGB callback.
#ifdef NUM_LOCK_INDEX
#    undef NUM_LOCK_INDEX
#endif
