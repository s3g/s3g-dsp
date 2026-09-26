#pragma once
#include "s3g_reloop_neon.h"

namespace s3g::controller::neon_midi {

// Private, host-local control envelope. Never send this research-ID SysEx to
// hardware. Sequencers can forward it without treating editor buttons as notes.
// Fixed 15-byte, 7-bit-clean wire format, with explicit signature and version.
enum class BridgeKind : uint8_t { Sync = 0u, Control, SelectCell, Pressure };
struct BridgeMessage {
    BridgeKind kind = BridgeKind::Sync;
    uint8_t bank = 0u, base = 36u, channel = 0u;
    reloop_neon::MidiMessage midi {};
    uint8_t cell = 0u;
};
using BridgePacket = std::array<uint8_t, 15u>;
inline BridgePacket encodeBridge(const BridgeMessage& message) noexcept {
    return {{0xf0u, 0x7du, 'S', '3', 'G', 'N', 1u,
        static_cast<uint8_t>(message.kind), message.bank, message.base, message.channel,
        static_cast<uint8_t>(message.kind == BridgeKind::Control ? message.midi.status & 0x7fu : message.cell),
        message.midi.data1, message.midi.data2, 0xf7u}};
}
inline bool decodeBridge(const uint8_t* bytes, uint32_t size, BridgeMessage& message) noexcept {
    if (!bytes || size != 15u || bytes[0] != 0xf0u || bytes[1] != 0x7du
        || bytes[2] != 'S' || bytes[3] != '3' || bytes[4] != 'G' || bytes[5] != 'N'
        || bytes[6] != 1u || bytes[7] > 3u || bytes[8] > 3u || bytes[9] > 96u
        || bytes[10] > 15u || bytes[14] != 0xf7u) return false;
    for (uint32_t i = 1u; i < 14u; ++i) if (bytes[i] > 127u) return false;
    const auto kind = static_cast<BridgeKind>(bytes[7]);
    if (kind != BridgeKind::Control && bytes[11] > 31u) return false;
    if (kind == BridgeKind::Control && (bytes[11] & 0x70u) > 0x30u) return false;
    message.kind = kind; message.bank = bytes[8]; message.base = bytes[9]; message.channel = bytes[10];
    message.midi = {static_cast<uint8_t>(bytes[11] | 0x80u), bytes[12], bytes[13]};
    message.cell = kind == BridgeKind::Control ? 0u : bytes[11];
    return true;
}
} // namespace s3g::controller::neon_midi
