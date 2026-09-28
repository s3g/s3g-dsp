#pragma once
#include "s3g_reloop_neon.h"
#include <cstddef>

namespace s3g::controller::neon_midi {
// One parser per physical endpoint. SysEx/realtime traffic is not forwarded.
// Keep running status and partial messages across packets, but only classify
// page restores following a bank press in the SAME packet as automatic.
class InputPacketDecoder {
public:
    void reset() noexcept { status_ = first_ = size_ = 0; sysex_ = false; }
    template<class Sink>
    void packet(const uint8_t* bytes, std::size_t count, Sink&& sink) noexcept {
        bool bank = false;
        for (std::size_t i = 0; bytes && i < count; ++i) {
            const auto b = bytes[i];
            if (b >= 0xf8) continue;
            if (b & 0x80) {
                size_ = 0;
                if (b >= 0xf0) { sysex_ = b == 0xf0; status_ = 0; }
                else { sysex_ = false; status_ = b; }
                continue;
            }
            if (sysex_ || !status_) continue;
            const auto kind = status_ & 0xf0;
            if (kind == 0xc0 || kind == 0xd0) continue;
            if (!size_) { first_ = b; size_ = 1; continue; }
            const reloop_neon::MidiMessage message {status_, first_, b};
            const auto action = reloop_neon::decode(message);
            const bool restore = bank && action.type == reloop_neon::ActionType::SelectMode;
            if (action.type == reloop_neon::ActionType::SelectBank && action.pressed) bank = true;
            sink(message, restore); size_ = 0;
        }
    }
private:
    uint8_t status_ = 0, first_ = 0, size_ = 0;
    bool sysex_ = false;
};
} // namespace s3g::controller::neon_midi
