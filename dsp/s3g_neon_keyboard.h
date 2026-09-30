#pragma once
#include "s3g_neon_note_map.h"
#include "s3g_musical_scales.h"

namespace s3g::controller::neon_midi {
// Stable layout IDs: chromatic, scale degrees, manually addressed pads.
// 255 is an unassigned/out-of-range key, never a MIDI byte.
inline PadNotes keyboardNotes(unsigned layout, unsigned first, unsigned scale,
    const PadNotes& manual = sequentialNotes(48)) noexcept {
    if (layout == 2) return manual;
    PadNotes notes {};
    const auto& definition = s3g::musicalScaleDefinition(layout == 1 ? scale : 0);
    for (unsigned cell = 0; cell < notes.size(); ++cell) {
        const unsigned key = first + 12u * (cell / definition.size)
            + static_cast<unsigned>(definition.semitones[cell % definition.size]);
        notes[cell] = key <= 127 ? static_cast<uint8_t>(key) : 255;
    }
    return notes;
}

// Host-local v4 map: unit, channel, 32 keys and a 32-bit OFF mask in five
// seven-bit bytes. Separate from the unique note-to-cell map (v3).
using KeyboardMapPacket = std::array<uint8_t,47>;
inline KeyboardMapPacket encodeKeyboardMap(unsigned unit, unsigned channel, const PadNotes& notes) noexcept {
    KeyboardMapPacket packet {{0xf0,0x7d,'S','3','G','N',4,static_cast<uint8_t>(unit),static_cast<uint8_t>(channel)}};
    for (unsigned i=0;i<32;++i) {
        packet[9+i] = notes[i] < 128 ? notes[i] : 0;
        if (notes[i] >= 128) packet[41+i/7] |= uint8_t(1u << (i%7));
    }
    packet[46]=0xf7; return packet;
}
inline bool decodeKeyboardMap(const uint8_t* bytes, uint32_t size,
    uint8_t& unit, uint8_t& channel, PadNotes& notes) noexcept {
    if (!bytes || size!=47 || bytes[0]!=0xf0 || bytes[1]!=0x7d
        || bytes[2]!='S' || bytes[3]!='3' || bytes[4]!='G' || bytes[5]!='N'
        || bytes[6]!=4 || bytes[7]>1 || bytes[8]>15 || bytes[45]>15 || bytes[46]!=0xf7) return false;
    for (unsigned i=1;i<46;++i) if(bytes[i]>127) return false;
    PadNotes next;
    for (unsigned i=0;i<32;++i) next[i] = bytes[41+i/7] & (1u << (i%7)) ? 255 : bytes[9+i];
    unit=bytes[7]; channel=bytes[8]; notes=next; return true;
}
} // namespace s3g::controller::neon_midi
