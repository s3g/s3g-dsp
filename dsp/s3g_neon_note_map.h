#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <string_view>

namespace s3g::controller::neon_midi {
using PadNotes = std::array<uint8_t, 32>;
inline PadNotes sequentialNotes(unsigned base = 36) noexcept {
    PadNotes notes {};
    base = base > 96 ? 96 : base;
    for (unsigned i = 0; i < notes.size(); ++i) notes[i] = static_cast<uint8_t>(base + i);
    return notes;
}
inline bool validNotes(const PadNotes& notes) noexcept {
    std::array<bool, 128> used {};
    for (auto note : notes) {
        if (note > 127 || used[note]) return false;
        used[note] = true;
    }
    return true;
}
inline int padForNote(const PadNotes& notes, int key) noexcept {
    if (key < 0 || key > 127) return -1;
    for (unsigned i = 0; i < notes.size(); ++i) if (notes[i] == key) return static_cast<int>(i);
    return -1;
}

// A bank-major list: A1..A8, B1..B8, C1..C8, D1..D8. Numeric MIDI
// addresses avoid host-dependent octave names. Reject partial/ambiguous maps.
inline bool parseNotes(std::string_view text, PadNotes& result) noexcept {
    PadNotes next {};
    unsigned count = 0;
    size_t pos = 0;
    const auto separator = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == ',' || c == ';'; };
    while (pos < text.size()) {
        while (pos < text.size() && separator(text[pos])) ++pos;
        if (pos == text.size()) break;
        if (count == next.size() || text[pos] < '0' || text[pos] > '9') return false;
        unsigned value = 0;
        do {
            value = value * 10 + static_cast<unsigned>(text[pos++] - '0');
            if (value > 127) return false;
        } while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9');
        if (pos < text.size() && !separator(text[pos])) return false;
        next[count++] = static_cast<uint8_t>(value);
    }
    if (count != next.size() || !validNotes(next)) return false;
    result = next;
    return true;
}

struct NoteMap {
    PadNotes notes = sequentialNotes();
    bool custom = false;
    bool followUtility = true; // Only used by the receiving instrument.
    PadNotes resolved(unsigned base) const noexcept { return custom ? notes : sequentialNotes(base); }
};

// Single-writer publication, separate instances for GUI and received MIDI.
// Bounded reads leave the caller's previous snapshot intact on contention.
// Atomic payloads + sequential consistency make the seqlock data-race free.
class PublishedNoteMap {
public:
    PublishedNoteMap() noexcept { store(NoteMap {}); }
    void store(const NoteMap& value) noexcept {
        sequence_.fetch_add(1);
        for (unsigned i = 0; i < notes_.size(); ++i) notes_[i].store(value.notes[i]);
        flags_.store((value.custom ? 1u : 0u) | (value.followUtility ? 2u : 0u));
        sequence_.fetch_add(1);
    }
    bool read(NoteMap& value) const noexcept {
        for (unsigned attempt = 0; attempt < 3; ++attempt) {
            const auto before = sequence_.load();
            if (before & 1u) continue;
            NoteMap next;
            for (unsigned i = 0; i < notes_.size(); ++i) next.notes[i] = notes_[i].load();
            const auto flags = flags_.load();
            next.custom = flags & 1u; next.followUtility = flags & 2u;
            if (before == sequence_.load()) { value = next; return true; }
        }
        return false;
    }
private:
    std::atomic<uint32_t> sequence_ {0};
    std::array<std::atomic<uint8_t>, 32> notes_ {};
    std::atomic<uint8_t> flags_ {2};
};

// Private host-local v3 envelope. Not hardware MIDI. Unlike control packets,
// this is shared by both surfaces and works with hardware ownership disabled.
using NoteMapPacket = std::array<uint8_t, 41>;
inline NoteMapPacket encodeNoteMap(const PadNotes& notes) noexcept {
    NoteMapPacket packet {{0xf0, 0x7d, 'S', '3', 'G', 'N', 3, 0}};
    for (unsigned i = 0; i < notes.size(); ++i) packet[8+i] = notes[i];
    packet[40] = 0xf7;
    return packet;
}
inline bool decodeNoteMap(const uint8_t* data, uint32_t size, PadNotes& result) noexcept {
    if (!data || size != 41 || data[0] != 0xf0 || data[1] != 0x7d || data[2] != 'S'
        || data[3] != '3' || data[4] != 'G' || data[5] != 'N' || data[6] != 3 || data[7] != 0 || data[40] != 0xf7) return false;
    PadNotes notes {};
    for (unsigned i = 0; i < notes.size(); ++i) notes[i] = data[8+i];
    if (!validNotes(notes)) return false;
    result = notes; return true;
}
} // namespace s3g::controller::neon_midi
