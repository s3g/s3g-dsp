#pragma once

#include "s3g_reloop_neon.h"
#include "s3g_neon_midi_bridge.h"
#include "s3g_neon_note_map.h"

#include <array>
#include <cstdint>

namespace s3g::controller::neon_midi {

namespace neon = reloop_neon;

// One physical NEON per instance. Only unmodified first-layer SAMPLE pads
// author musical notes. Other pages are editing surfaces, not extra notes.
// No hardware output, raw MIDI thru, allocation, or locks belong in this class.
class Mapper {
public:
    struct Result {
        bool bridge = false;
        BridgeKind kind = BridgeKind::Control;
        uint8_t cell = 0u;
    };
    struct Held {
        bool active = false;
        bool releasing = false;
        uint8_t note = 0u;
        uint8_t channel = 0u;
        uint8_t cell = 0u;
    };

    uint8_t bank() const noexcept { return bank_; }
    uint8_t baseNote() const noexcept { return base_; }
    uint8_t channel() const noexcept { return channel_; } // zero based
    bool holds(uint8_t note, uint8_t channel) const noexcept {
        for (const auto& held : held_) if (held.active && held.note == note && held.channel == channel) return true;
        return false;
    }
    neon::Mode mode() const noexcept { return surface_.mode; }
    neon::Layer layer() const noexcept { return surface_.layer; }
    uint32_t heldCells() const noexcept {
        uint32_t mask = 0u;
        for (const auto& held : held_)
            if (held.active) mask |= uint32_t{1} << held.cell;
        return mask;
    }
    bool pending() const noexcept {
        for (const auto& held : held_)
            if (held.releasing) return true;
        return padInput_.pending();
    }
    void prepare(double rate) noexcept { padInput_.prepare(rate); }
    void advance(uint32_t frames) noexcept { padInput_.advance(frames); }
    void setBank(uint8_t bank) noexcept {
        bank = std::min<uint8_t>(bank, 3u);
        if (bank != bank_) padInput_.clear();
        bank_ = bank;
    }
    void setBaseNote(uint8_t note) noexcept {
        note = std::min<uint8_t>(note, 96u);
        if (note != base_) padInput_.clear();
        base_ = note;
    }
    void setChannel(uint8_t channel) noexcept {
        channel = std::min<uint8_t>(channel, 15u);
        if (channel != channel_) padInput_.clear();
        channel_ = channel;
    }
    void setNotes(const PadNotes& notes) noexcept {
        if (notes != notes_) padInput_.clear();
        notes_ = notes; customNotes_ = true;
    }

    // Hold addresses are latched on press: changes of bank, mapping, page or
    // channel must never change the destination of an already-held release.
    void panic() noexcept {
        padInput_.clear();
        for (auto& held : held_) if (held.active) held.releasing = true;
        surface_.modeHeld = surface_.repeatHeld = surface_.syncHeld = false;
        surface_.censorHeld = surface_.slipHeld = false;
    }

    template <typename Sink>
    void flush(uint32_t time, Sink&& sink) noexcept {
        for (auto& held : held_)
            if (held.releasing) release(held, time, sink);
    }

    template <typename Sink>
    Result process(neon::MidiMessage message, uint32_t time, Sink&& sink) noexcept {
        if (message.data1 > 127u || message.data2 > 127u
            || neon::isStatusLedMessage(message)) return {};
        const auto action = padInput_.process(message, time);
        if (!action) return {neon::isVelocityToggle(message), BridgeKind::Control, 0u};

        if (action.type == neon::ActionType::SelectBank) {
            // CHOP bank buttons address slices, not the performance cell bank.
            if (action.pressed && !action.shifted
                && surface_.mode != neon::Mode::Slicer && surface_.mode != neon::Mode::HotCue) setBank(action.bank);
            return {true, BridgeKind::Control, 0u};
        }
        if (action.type == neon::ActionType::SelectMode) {
            // A mode-button status can reflect a deck rather than the SAMPLER
            // bank. Do not overwrite the latched cell bank on a page change.
            if (action.pressed) {
                surface_.mode = action.mode;
                surface_.layer = action.layer;
            }
            return {true, BridgeKind::Control, 0u};
        }
        if (action.type == neon::ActionType::Utility) {
            (void)surface_.apply(action);
            return {true, BridgeKind::Control, 0u};
        }
        if (action.type == neon::ActionType::PadVelocity) {
            // Musical hits use the latched value locally; editing/audition
            // pages need the raw CC followed by their raw note downstream.
            return {!performancePad(action, message), BridgeKind::Control, 0u};
        }
        if (action.type == neon::ActionType::PadPressure && action.pad < 8u) {
            const auto& held = held_[action.pad];
            return held.active ? Result {true, BridgeKind::Pressure, held.cell}
                : Result {true, BridgeKind::Control, 0u};
        }
        if (action.type != neon::ActionType::Pad || action.pad >= 8u)
            return {true, BridgeKind::Control, 0u};
        auto& held = held_[action.pad];
        if (!action.pressed) {
            // Some firmware changes release addresses when a page or SHIFT
            // changes under a held finger. Physical pad identity is stable.
            const bool musical = held.active;
            release(held, time, sink);
            return musical ? Result {} : Result {true, BridgeKind::Control, 0u};
        }
        surface_.mode = action.mode;
        surface_.layer = action.layer;
        if (!performancePad(action, message))
            return {true, BridgeKind::Control, 0u};

        if (!release(held, time, sink)) return {};
        const auto cell = static_cast<uint8_t>(bank_ * 8u + action.pad);
        const auto note = customNotes_ ? notes_[cell] : static_cast<uint8_t>(base_ + cell);
        if (sink(time, neon::MidiMessage {
                static_cast<uint8_t>(0x90u | channel_), note, action.value })) {
            held = {true, false, note, channel_, cell};
            return {true, BridgeKind::SelectCell, cell};
        }
        return {};
    }

private:
    bool performancePad(const neon::Action& action, neon::MidiMessage message) const noexcept {
        return (message.status & 0x0fu) == 7u && action.mode == neon::Mode::Sampler
            && action.layer == neon::Layer::First && !action.shifted
            && !surface_.modeHeld && !surface_.repeatHeld && !surface_.syncHeld
            && !surface_.censorHeld && !surface_.slipHeld;
    }
    template <typename Sink>
    bool release(Held& held, uint32_t time, Sink&& sink) noexcept {
        if (!held.active) return true;
        held.releasing = true;
        if (!sink(time, neon::MidiMessage {
                static_cast<uint8_t>(0x80u | held.channel), held.note, 0u }))
            return false; // Retry next block; never silently lose a note-off.
        held = {};
        return true;
    }
    neon::PerformanceState surface_ {};
    neon::PadInputDecoder padInput_ {};
    std::array<Held, 8u> held_ {};
    PadNotes notes_ = sequentialNotes();
    bool customNotes_ = false;
    uint8_t bank_ = 0u, base_ = 36u, channel_ = 0u;
};

} // namespace s3g::controller::neon_midi
