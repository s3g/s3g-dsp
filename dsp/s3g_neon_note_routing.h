#pragma once
#include "s3g_neon_note_map.h"
#include "s3g_sample_neon.h"

namespace s3g::controller::neon_midi {
// Channel destinations: 0 = existing pad map, 1..32 = pinned chromatic pad,
// 33 = off. Held addresses survive edits to the map, channel or selected pad.
class NoteRouter {
public:
    void reset() noexcept { held_ = {}; }
    static int target(unsigned route, const PadNotes& notes, int key) noexcept {
        return key < 0 || key > 127 || route > 32 ? -1 : route ? int(route - 1) : padForNote(notes, key);
    }
    template<class Sink>
    void on(uint32_t time, int channel, int key, int32_t id, bool midi,
        unsigned route, const PadNotes& notes, float velocity, Sink&& sink) noexcept {
        const int pad = target(route, notes, key);
        if (channel < 0 || channel > 15 || pad < 0 || !std::isfinite(velocity)) return;
        Held* h = nullptr;
        for (auto& v : held_) if (!v.active) { h = &v; break; }
        if (!h) {
            h = &*std::min_element(held_.begin(), held_.end(), [](auto& a, auto& b) { return a.event.noteId < b.event.noteId; });
            auto choke = h->event; choke.frameOffset = time; choke.kind = sample::SampleNeonEventKind::Choke;
            if (!sink(choke)) return;
        }
        sample::SampleNeonEvent e;
        e.frameOffset = time; e.slot = static_cast<uint8_t>(pad);
        e.noteId = 0x6100000000000000ull | (++serial_ & 0x00ffffffffffffffull);
        e.key = route ? static_cast<uint8_t>(key) : 255;
        e.value = std::clamp(velocity, 0.f, 1.f);
        if (sink(e)) *h = {true, midi, channel, key, id, e};
    }
    template<class Sink>
    void off(uint32_t time, int channel, int key, int32_t id, bool midi, bool choke, Sink&& sink) noexcept {
        for (auto& h : held_) if (matches(h, channel, key, id, midi)) {
            auto e = h.event; e.frameOffset = time; e.value = 0;
            e.kind = choke ? sample::SampleNeonEventKind::Choke : sample::SampleNeonEventKind::Release;
            if (sink(e)) h.active = false;
        }
    }
    template<class Sink>
    void pressure(uint32_t time, int channel, int key, int32_t id, bool midi, float value, Sink&& sink) noexcept {
        if (!std::isfinite(value)) return;
        // Hosts may keep notes in CLAP dialect while delivering poly pressure
        // as raw MIDI. Channel/key still identify exactly the held notes.
        for (const auto& h : held_) if (matches(h, channel, key, id, h.midi) && (midi || id < 0 || !h.midi)) {
            auto e = h.event; e.frameOffset = time; e.value = value; e.kind = sample::SampleNeonEventKind::Pressure;
            sink(e);
        }
    }
private:
    struct Held { bool active = false, midi = false; int channel = 0, key = 0; int32_t id = -1; sample::SampleNeonEvent event; };
    static bool matches(const Held& h, int channel, int key, int32_t id, bool midi) noexcept {
        return h.active && h.midi == midi && (channel < 0 || channel == h.channel)
            && (key < 0 || key == h.key) && (id < 0 || id == h.id);
    }
    std::array<Held, 256> held_ {};
    uint64_t serial_ = 0;
};
} // namespace s3g::controller::neon_midi
