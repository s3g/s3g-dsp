#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace s3g::controller::reloop_neon {

constexpr uint8_t kBankCount = 4u;
constexpr uint8_t kPadsPerBank = 8u;
constexpr uint8_t kSlotCount = kBankCount * kPadsPerBank;
constexpr uint8_t kLedSegmentsPerPad = 5u;
constexpr uint8_t kLedValuesPerPad = 1u + kLedSegmentsPerPad;
// Four bank lamps have separate SAMPLE-bank and editing-deck addresses.
// Clear both address sets, then light exactly the selected address.
constexpr std::size_t kFullLedFrameMessages = 2u * kBankCount + 1u
    + static_cast<std::size_t>(kPadsPerBank) * kLedValuesPerPad;
constexpr std::size_t kMaximumLedMessages =
    kFullLedFrameMessages + kPadsPerBank + kBankCount;

enum class Mode : uint8_t {
    Sampler = 0u,
    Slicer,
    HotCue,
    HotLoop,
};

enum class Layer : uint8_t {
    First = 0u,
    Second,
};

// Large-pad palette indices, not brightness values. Keep primary cell
// inventory red / last-played yellow / sounding white. Secondary pages each
// use a separate idle hue; selected tools and sounding cells remain white.
// Exact hue appearance must be checked on the connected firmware.
constexpr uint8_t secondaryPadColor(Mode mode, bool active = false) noexcept
{
    constexpr std::array<uint8_t, 4u> palette {{16u, 32u, 64u, 80u}};
    return active ? 127u : palette[static_cast<uint8_t>(mode) & 3u];
}

enum class Encoder : uint8_t {
    Trax = 0u,
    Loop,
};

enum class UtilityButton : uint8_t {
    Censor = 0u,
    Slip,
    DeckSync,
    Mode,
    Repeat,
    Sync,
};

// Printed left-to-right below every performance pad. The first three lamps
// select one trigger style; Repeat and Sync are independent status switches.
enum class PadStatusLamp : uint8_t {
    OneShot = 0u,
    Toggle,
    Hold,
    Repeat,
    Sync,
};

constexpr std::size_t lampIndex(PadStatusLamp lamp) noexcept
{
    return static_cast<std::size_t>(lamp);
}

enum class ActionType : uint8_t {
    None = 0u,
    Pad,
    PadPressure,
    SelectBank,
    SelectMode,
    EncoderTurn,
    EncoderPush,
    Utility,
    PadVelocity,
};

struct MidiMessage {
    uint8_t status = 0u;
    uint8_t data1 = 0u;
    uint8_t data2 = 0u;

    constexpr bool operator==(const MidiMessage& other) const noexcept
    {
        return status == other.status && data1 == other.data1
            && data2 == other.data2;
    }
};

struct Action {
    ActionType type = ActionType::None;
    Mode mode = Mode::Sampler;
    Layer layer = Layer::First;
    Encoder encoder = Encoder::Trax;
    UtilityButton utility = UtilityButton::Censor;
    uint8_t bank = 0xffu;
    uint8_t pad = 0xffu;
    uint8_t value = 0u;
    int8_t delta = 0;
    bool pressed = false;
    bool shifted = false;

    constexpr explicit operator bool() const noexcept
    {
        return type != ActionType::None;
    }
};

namespace detail {

struct PadAddress {
    Mode mode = Mode::Sampler;
    Layer layer = Layer::First;
    uint8_t pad = 0xffu;
    bool shifted = false;
    bool valid = false;
};

constexpr PadAddress decodePadAddress(uint8_t note) noexcept
{
    const uint8_t pad = static_cast<uint8_t>(note & 0x07u);
    const uint8_t group = static_cast<uint8_t>(note & 0x78u);
    switch (group) {
    case 0x00u: return { Mode::Sampler, Layer::First, pad, false, true };
    case 0x20u: return { Mode::Sampler, Layer::First, pad, true, true };
    case 0x60u: return { Mode::Sampler, Layer::Second, pad, false, true };
    case 0x40u: return { Mode::Sampler, Layer::Second, pad, true, true };
    case 0x08u: return { Mode::Slicer, Layer::First, pad, false, true };
    case 0x28u: return { Mode::Slicer, Layer::First, pad, true, true };
    case 0x68u: return { Mode::Slicer, Layer::Second, pad, false, true };
    case 0x48u: return { Mode::Slicer, Layer::Second, pad, true, true };
    case 0x10u: return { Mode::HotCue, Layer::First, pad, false, true };
    case 0x30u: return { Mode::HotCue, Layer::First, pad, true, true };
    case 0x70u: return { Mode::HotCue, Layer::Second, pad, false, true };
    case 0x50u: return { Mode::HotCue, Layer::Second, pad, true, true };
    case 0x18u: return { Mode::HotLoop, Layer::First, pad, false, true };
    case 0x38u: return { Mode::HotLoop, Layer::First, pad, true, true };
    case 0x78u: return { Mode::HotLoop, Layer::Second, pad, false, true };
    case 0x58u: return { Mode::HotLoop, Layer::Second, pad, true, true };
    default: return {};
    }
}

constexpr int8_t relativeDelta(uint8_t value) noexcept
{
    if (value == 0u || value == 64u) return 0;
    // Neon firmware revisions and MIDI translators have been observed using
    // both common relative encodings: 1/127 and 65/63. Decode the useful
    // acceleration range of either without turning a one-click 63/65 value
    // into a near-full-scale parameter jump.
    if (value <= 31u) return static_cast<int8_t>(value);
    if (value < 64u)
        return static_cast<int8_t>(-
            static_cast<int16_t>(64u - static_cast<uint16_t>(value)));
    if (value <= 95u)
        return static_cast<int8_t>(value - 64u);
    return static_cast<int8_t>(-
        static_cast<int16_t>(128u - static_cast<uint16_t>(value)));
}

constexpr bool isPressed(uint8_t status, uint8_t velocity) noexcept
{
    return (status & 0xf0u) == 0x90u && velocity != 0u;
}

constexpr uint8_t deckFromStatus(uint8_t status) noexcept
{
    const uint8_t channel = static_cast<uint8_t>(status & 0x0fu);
    if (channel >= 3u && channel <= 6u)
        return static_cast<uint8_t>(channel - 3u);
    if (channel >= 7u && channel <= 10u)
        return static_cast<uint8_t>(channel - 7u);
    return 0xffu;
}

} // namespace detail

// Decode the Neon factory MIDI map. Pad notes arrive on channels 8-11
// (status 97-9A); deck buttons use channels 4-7 (93-96); relative encoders
// use CC on channel 7 (B6). Note-off and note-on/velocity-zero are both
// accepted so host MIDI transformations do not break releases.
constexpr Action decode(MidiMessage message) noexcept
{
    const uint8_t kind = static_cast<uint8_t>(message.status & 0xf0u);
    const uint8_t channel = static_cast<uint8_t>(message.status & 0x0fu);
    const bool noteMessage = kind == 0x80u || kind == 0x90u;
    const bool pressed = detail::isPressed(message.status, message.data2);

    if ((noteMessage || kind == 0xa0u || kind == 0xb0u)
        && channel >= 7u && channel <= 10u) {
        const auto address = detail::decodePadAddress(message.data1);
        if (address.valid) {
            Action action;
            action.type = kind == 0xa0u ? ActionType::PadPressure
                : kind == 0xb0u ? ActionType::PadVelocity : ActionType::Pad;
            action.mode = address.mode;
            action.layer = address.layer;
            action.bank = address.layer == Layer::Second
                ? detail::deckFromStatus(message.status) : 0xffu;
            action.pad = address.pad;
            action.value = message.data2;
            action.pressed = action.type == ActionType::Pad ? pressed : true;
            action.shifted = address.shifted;
            return action;
        }
    }

    // Encoder presses are reported as a channel-7 note range in the factory
    // map. Decode this before the same status is considered a deck-D button.
    if (noteMessage && channel == 6u
        && ((message.data1 >= 0x20u && message.data1 <= 0x27u)
            || message.data1 == 0x5fu
            || (message.data1 >= 0x63u && message.data1 <= 0x66u))) {
        Action action;
        action.type = ActionType::EncoderPush;
        if (message.data1 >= 0x20u && message.data1 <= 0x23u) {
            action.encoder = Encoder::Trax;
            action.bank = static_cast<uint8_t>(message.data1 - 0x20u);
        } else if (message.data1 >= 0x24u
            && message.data1 <= 0x27u) {
            action.encoder = Encoder::Loop;
            action.bank = static_cast<uint8_t>(message.data1 - 0x24u);
        } else {
            action.encoder = message.data1 == 0x5fu
                ? Encoder::Trax : Encoder::Loop;
            action.bank = message.data1 == 0x5fu ? 0xffu
                : static_cast<uint8_t>(message.data1 - 0x63u);
            action.shifted = true;
        }
        action.value = message.data2;
        action.pressed = pressed;
        return action;
    }

    if (noteMessage && channel >= 3u && channel <= 6u) {
        const uint8_t bank = detail::deckFromStatus(message.status);
        if (message.data1 == 0x00u || message.data1 == 0x01u
            || message.data1 == 0x45u) {
            Action action;
            action.type = ActionType::SelectBank;
            action.bank = bank;
            action.value = message.data2;
            action.pressed = pressed;
            action.shifted = message.data1 == 0x45u;
            return action;
        }

        if (message.data1 >= 0x05u && message.data1 <= 0x0cu) {
            const uint8_t offset = static_cast<uint8_t>(message.data1 - 0x05u);
            Action action;
            action.type = ActionType::SelectMode;
            action.bank = bank;
            action.mode = static_cast<Mode>(offset & 0x03u);
            action.layer = offset >= 4u ? Layer::Second : Layer::First;
            action.value = message.data2;
            action.pressed = pressed;
            return action;
        }

        if ((message.data1 >= 0x10u && message.data1 <= 0x12u)
            || (message.data1 >= 0x55u && message.data1 <= 0x57u)) {
            const bool shifted = message.data1 >= 0x55u;
            const uint8_t base = shifted ? 0x55u : 0x10u;
            Action action;
            action.type = ActionType::Utility;
            action.utility = static_cast<UtilityButton>(
                message.data1 - base);
            action.bank = bank;
            action.value = message.data2;
            action.pressed = pressed;
            action.shifted = shifted;
            return action;
        }

        if (message.data1 == 0x20u || message.data1 == 0x24u
            || message.data1 == 0x5fu || message.data1 == 0x63u) {
            Action action;
            action.type = ActionType::EncoderPush;
            action.encoder = message.data1 == 0x20u
                    || message.data1 == 0x5fu
                ? Encoder::Trax : Encoder::Loop;
            action.bank = bank;
            action.value = message.data2;
            action.pressed = pressed;
            action.shifted = message.data1 >= 0x5fu;
            return action;
        }
    }

    if (noteMessage && channel == 6u) {
        if ((message.data1 >= 0x0du && message.data1 <= 0x0fu)
            || (message.data1 >= 0x52u && message.data1 <= 0x54u)) {
            const bool shifted = message.data1 >= 0x52u;
            const uint8_t base = shifted ? 0x52u : 0x0du;
            Action action;
            action.type = ActionType::Utility;
            action.utility = static_cast<UtilityButton>(
                static_cast<uint8_t>(UtilityButton::Mode)
                + static_cast<uint8_t>(message.data1 - base));
            action.bank = 0xffu;
            action.value = message.data2;
            action.pressed = pressed;
            action.shifted = shifted;
            return action;
        }

    }

    if (kind == 0xb0u && channel == 6u) {
        Action action;
        action.type = ActionType::EncoderTurn;
        action.value = message.data2;
        action.delta = detail::relativeDelta(message.data2);
        if (message.data1 == 0x00u || message.data1 == 0x45u) {
            action.encoder = Encoder::Trax;
            action.shifted = message.data1 == 0x45u;
            return action;
        }
        if (message.data1 >= 0x04u && message.data1 <= 0x07u) {
            action.encoder = Encoder::Loop;
            action.bank = static_cast<uint8_t>(message.data1 - 0x04u);
            return action;
        }
        if (message.data1 >= 0x49u && message.data1 <= 0x4cu) {
            action.encoder = Encoder::Loop;
            action.bank = static_cast<uint8_t>(message.data1 - 0x49u);
            action.shifted = true;
            return action;
        }
    }

    return {};
}

// Factory velocity is a CC immediately BEFORE a fixed-127 pad note, not
// aftertouch. Pair by physical pad AND full channel/address so another deck,
// page or SHIFT address cannot borrow it. Used by both the MIDI Utility and
// Sample Neon's direct hardware/control-envelope input. No allocation/locks.
constexpr bool isVelocityToggle(MidiMessage message) noexcept
{
    const auto channel = message.status & 0x0fu;
    return (message.status & 0xf0u) == 0x90u && channel >= 3u && channel <= 6u
        && message.data1 == 0x4eu && message.data2 != 0u;
}

class PadInputDecoder {
public:
    void prepare(double sampleRate) noexcept {
        maximumAge_ = static_cast<uint64_t>(std::clamp(sampleRate * 0.020, 1.0, 1000000.0));
        frameBase_ = 0u;
        clear();
    }
    void clear() noexcept { pending_ = {}; }
    bool pending() const noexcept {
        for (const auto& value : pending_) if (value.valid) return true;
        return false;
    }
    void advance(uint32_t frames) noexcept {
        frameBase_ += frames;
        for (auto& value : pending_)
            if (value.valid && frameBase_ >= value.frame && frameBase_ - value.frame > maximumAge_)
                value.valid = false;
    }
    Action process(MidiMessage message, uint32_t offset) noexcept {
        if (message.data1 > 127u || message.data2 > 127u) return {};
        auto action = decode(message);
        const uint8_t channel = message.status & 0x0fu;
        // A physical SHIFT+SAMPLER toggle was observed as 93..96,4E,7F.
        // It changes hardware sensitivity, not the musical surface.
        if ((action.pressed && (action.type == ActionType::SelectBank
                || action.type == ActionType::SelectMode))
            || isVelocityToggle(message)) clear();
        if (action.pad >= kPadsPerBank) return action;
        auto& value = pending_[action.pad];
        const uint64_t frame = frameBase_ + offset;
        if (action.type == ActionType::PadVelocity) {
            // A measured zero still accompanies a real note-on: MIDI velocity
            // zero would release it, so represent the quietest hit as one.
            value = {frame, channel, message.data1, std::max<uint8_t>(1u, action.value), true};
        } else if (action.type == ActionType::Pad) {
            if (action.pressed && value.valid && value.channel == channel
                && value.address == message.data1 && frame >= value.frame
                && frame - value.frame <= maximumAge_) action.value = value.velocity;
            // Consume once, including releases and mismatched contexts. When
            // velocity mode is off, use the original note velocity as before.
            value.valid = false;
        }
        return action;
    }
private:
    struct Pending {
        uint64_t frame = 0u;
        uint8_t channel = 0u, address = 0u, velocity = 0u;
        bool valid = false;
    };
    std::array<Pending, kPadsPerBank> pending_ {};
    uint64_t frameBase_ = 0u, maximumAge_ = 960u;
};

struct PerformanceState {
    uint8_t bank = 0u;
    Mode mode = Mode::Sampler;
    Layer layer = Layer::First;
    uint8_t selectedPad = 0u;
    std::array<bool, kPadsPerBank> heldPads {};
    std::array<float, kPadsPerBank> pressure {};
    bool censorHeld = false;
    bool slipHeld = false;
    bool modeHeld = false;
    bool repeatHeld = false;
    bool syncHeld = false;

    constexpr uint8_t slotForPad(uint8_t pad) const noexcept
    {
        return pad < kPadsPerBank
            ? static_cast<uint8_t>(bank * kPadsPerBank + pad) : 0xffu;
    }

    constexpr uint8_t performanceIndex(uint8_t pad) const noexcept
    {
        return pad < kPadsPerBank
            ? static_cast<uint8_t>(
                (layer == Layer::Second ? kPadsPerBank : 0u) + pad)
            : 0xffu;
    }

    constexpr bool apply(const Action& action) noexcept
    {
        switch (action.type) {
        case ActionType::SelectBank:
            if (action.pressed && action.bank < kBankCount) {
                bank = action.bank;
                return true;
            }
            break;
        case ActionType::SelectMode:
            if (action.pressed) {
                mode = action.mode;
                layer = action.layer;
                if (action.bank < kBankCount) bank = action.bank;
                return true;
            }
            break;
        case ActionType::Pad:
            if (action.pad < kPadsPerBank) {
                if (action.bank < kBankCount) bank = action.bank;
                mode = action.mode;
                layer = action.layer;
                selectedPad = action.pad;
                heldPads[action.pad] = action.pressed;
                if (!action.pressed) pressure[action.pad] = 0.0f;
                return true;
            }
            break;
        case ActionType::PadPressure:
            if (action.pad < kPadsPerBank) {
                if (action.bank < kBankCount) bank = action.bank;
                pressure[action.pad] = static_cast<float>(action.value)
                    / 127.0f;
                return true;
            }
            break;
        case ActionType::Utility:
            if (action.utility == UtilityButton::Censor)
                censorHeld = action.pressed;
            else if (action.utility == UtilityButton::Slip)
                slipHeld = action.pressed;
            else if (action.utility == UtilityButton::Mode)
                modeHeld = action.pressed;
            else if (action.utility == UtilityButton::Repeat)
                repeatHeld = action.pressed;
            else if (action.utility == UtilityButton::Sync
                || action.utility == UtilityButton::DeckSync)
                syncHeld = action.pressed;
            else return false;
            return true;
        default: break;
        }
        return false;
    }
};

struct PadLedState {
    uint8_t surface = 0u;
    std::array<uint8_t, kLedSegmentsPerPad> segments {};

    constexpr bool operator==(const PadLedState& other) const noexcept
    {
        return surface == other.surface && segments == other.segments;
    }
};

struct LedFrame {
    uint8_t bank = 0u;
    Mode mode = Mode::Sampler;
    Layer layer = Layer::First;
    std::array<PadLedState, kPadsPerBank> pads {};
};

constexpr MidiMessage bankLedMessage(uint8_t bank, Mode mode,
    uint8_t value = 127u, Layer layer = Layer::First) noexcept
{
    return {
        static_cast<uint8_t>(0x93u + std::min<uint8_t>(bank, 3u)),
        static_cast<uint8_t>(mode == Mode::Sampler && layer == Layer::First ? 0x00u : 0x01u),
        static_cast<uint8_t>(std::min<uint8_t>(value, 127u)),
    };
}

constexpr MidiMessage modeLedMessage(uint8_t bank, Mode mode, Layer layer,
    uint8_t value = 127u) noexcept
{
    return {
        static_cast<uint8_t>(0x93u + std::min<uint8_t>(bank, 3u)),
        static_cast<uint8_t>(0x05u + static_cast<uint8_t>(mode)
            + (layer == Layer::Second ? 4u : 0u)),
        static_cast<uint8_t>(std::min<uint8_t>(value, 127u)),
    };
}

// The mode lamp (05) is not the sampler-mode command (0D). NEON remembers
// each deck's last performance page. Reloop's MIDI map, p9, documents this
// separate first-layer SAMPLER trigger for decks A-D. Send it to every deck
// before restoring the chosen bank so a later bank press cannot recall EDIT.
constexpr MidiMessage samplerModeTrigger(uint8_t bank) noexcept
{
    return {static_cast<uint8_t>(0x93u + std::min<uint8_t>(bank, 3u)),
        0x0du, 127u};
}

// The large RGB performance pads use their playable note addresses for MIDI
// feedback. The 9B/20-47 range below controls only the five small indicators
// beneath each pad.
constexpr MidiMessage padSurfaceMessage(uint8_t bank, Mode mode, Layer layer,
    uint8_t pad, uint8_t value) noexcept
{
    const uint8_t boundedBank = std::min<uint8_t>(bank, 3u);
    const uint8_t boundedPad = std::min<uint8_t>(pad, 7u);
    const bool second = layer == Layer::Second;
    const uint8_t status = mode == Mode::Sampler && !second
        ? 0x97u : static_cast<uint8_t>(0x97u + boundedBank);
    const uint8_t base = second
        ? static_cast<uint8_t>(0x60u + static_cast<uint8_t>(mode) * 8u)
        : static_cast<uint8_t>(static_cast<uint8_t>(mode) * 8u);
    return { status, static_cast<uint8_t>(base + boundedPad),
        static_cast<uint8_t>(std::min<uint8_t>(value, 127u)) };
}

constexpr MidiMessage padLedMessage(uint8_t pad, uint8_t segment,
    uint8_t value) noexcept
{
    return { 0x9bu,
        static_cast<uint8_t>(0x20u + pad * kLedSegmentsPerPad + segment),
        static_cast<uint8_t>(std::min<uint8_t>(value, 127u)) };
}

constexpr MidiMessage padLedMessage(uint8_t pad, PadStatusLamp lamp,
    uint8_t value) noexcept
{
    return padLedMessage(pad, static_cast<uint8_t>(lamp), value);
}

// Retry a surface change a few times to survive delayed hardware lamp resets,
// then go quiet. Never turn steady LEDs into an endless MIDI heartbeat.
class LedRefreshRetries {
public:
    bool update(const LedFrame& frame, uint64_t nowMs, bool requested = false) noexcept {
        if (!initialized_ || requested || bank_ != frame.bank || mode_ != frame.mode || layer_ != frame.layer) {
            initialized_ = true; bank_ = frame.bank; mode_ = frame.mode; layer_ = frame.layer;
            attempt_ = 0u; due_ = nowMs + 50u;
            return false;
        }
        if (attempt_ >= 3u || nowMs < due_) return false;
        ++attempt_;
        due_ = nowMs + (attempt_ == 1u ? 100u : 200u);
        return true;
    }
private:
    bool initialized_ = false;
    uint8_t bank_ = 0u, attempt_ = 3u;
    Mode mode_ = Mode::Sampler;
    Layer layer_ = Layer::First;
    uint64_t due_ = 0u;
};

// These are output-only small status lamps, not playable notes. A host route
// returning them must not turn them into ordinary MIDI notes 32..71.
constexpr bool isStatusLedMessage(MidiMessage message) noexcept {
    return (message.status == 0x9bu || message.status == 0x8bu)
        && message.data1 >= 0x20u && message.data1 <= 0x47u;
}

class LedDiffEncoder {
public:
    std::size_t encode(const LedFrame& frame, MidiMessage* output,
        std::size_t capacity, bool force = false, bool refreshPads = false,
        bool restoreSamplerMode = true) noexcept
    {
        if (!output || capacity == 0u) return 0u;
        std::size_t count = 0u;
        const bool pageChanged = !initialized_ || shadow_.mode != frame.mode
            || shadow_.layer != frame.layer;
        const bool contextChanged = initialized_
            && (shadow_.bank != frame.bank || pageChanged);
        const auto append = [&](MidiMessage message) {
            if (count >= capacity) return false;
            output[count++] = message;
            return true;
        };
        if (contextChanged) {
            for (uint8_t pad = 0u; pad < kPadsPerBank; ++pad) {
                const auto old = padSurfaceMessage(shadow_.bank, shadow_.mode, shadow_.layer, pad, 0u);
                const auto next = padSurfaceMessage(frame.bank, frame.mode, frame.layer, pad, 0u);
                // Primary sampler banks share addresses. Do not darken that
                // same surface just before restoring its loaded-cell color.
                if (!(old == next) && !append(old)) return count;
            }
        }
        if (force || !initialized_ || contextChanged) {
            if (restoreSamplerMode && frame.mode == Mode::Sampler
                && frame.layer == Layer::First && (force || pageChanged)) {
                for (uint8_t bank = 0u; bank < kBankCount; ++bank)
                    if (!append(samplerModeTrigger(bank))) return count;
            }
            // Ordinary bank changes must not reinitialize the decks or
            // reassert their mode lamps. That can leave the old bank lit.
            if ((force || pageChanged)
                && !append(modeLedMessage(frame.bank, frame.mode, frame.layer))) return count;
            const auto selectedBank = bankLedMessage(frame.bank, frame.mode, 127u, frame.layer);
            for (uint8_t bank = 0u; bank < kBankCount; ++bank) {
                for (uint8_t address = 0u; address < 2u; ++address) {
                    const auto status = static_cast<uint8_t>(0x93u + bank);
                    if (status == selectedBank.status && address == selectedBank.data1) continue;
                    if (!append({status, address, 0u})) return count;
                }
            }
            // Bank selection follows ALL mode commands and stale lamp clears,
            // before the pad repaint (bank selection can reset pad colors).
            if (!append(selectedBank)) return count;
        }
        for (uint8_t pad = 0u; pad < kPadsPerBank; ++pad) {
            const auto padIndex = static_cast<std::size_t>(pad);
            const uint8_t nextSurface = frame.pads[padIndex].surface;
            if (force || refreshPads || !initialized_ || contextChanged
                || shadow_.pads[padIndex].surface != nextSurface) {
                if (!append(padSurfaceMessage(frame.bank, frame.mode,
                        frame.layer, pad, nextSurface))) return count;
                shadow_.pads[padIndex].surface = nextSurface;
            }
            for (uint8_t segment = 0u; segment < kLedSegmentsPerPad;
                ++segment) {
                const auto segmentIndex = static_cast<std::size_t>(segment);
                const uint8_t next = frame.pads[padIndex].segments[segmentIndex];
                if (!force && !refreshPads && initialized_ && !contextChanged
                    && shadow_.pads[padIndex].segments[segmentIndex] == next)
                    continue;
                if (!append(padLedMessage(pad, segment, next))) return count;
                shadow_.pads[padIndex].segments[segmentIndex] = next;
            }
        }
        shadow_.bank = frame.bank;
        shadow_.mode = frame.mode;
        shadow_.layer = frame.layer;
        initialized_ = true;
        return count;
    }

    void invalidate() noexcept { initialized_ = false; }

private:
    LedFrame shadow_ {};
    bool initialized_ = false;
};

constexpr std::array<uint8_t, 4u> enableFourDecksSysEx() noexcept
{
    return { 0xf0u, 0x0au, 0x00u, 0xf7u };
}

} // namespace s3g::controller::reloop_neon
