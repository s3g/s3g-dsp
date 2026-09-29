#pragma once

#include <clap/events.h>
#include "s3g_reloop_neon.h"

namespace s3g::controller::neon_midi {
// Host MIDI loses USB packet boundaries. Recognize only the measured, adjacent
// bank + mode-on + mode-off triplet at one timestamp/on one port. No timeout:
// the next real mode-button press must still work, even in the same block.
inline bool isBankPageRecall(const clap_input_events_t* input, uint32_t index) noexcept {
    if (!input || !input->get || !input->size) return false;
    const auto count = input->size(input);
    const auto midi = [&](uint32_t n) -> const clap_event_midi_t* {
        if (n >= count) return nullptr;
        const auto* h = input->get(input, n);
        return h && h->space_id == CLAP_CORE_EVENT_SPACE_ID
            && h->type == CLAP_EVENT_MIDI && h->size >= sizeof(clap_event_midi_t)
            ? reinterpret_cast<const clap_event_midi_t*>(h) : nullptr;
    };
    const auto action = [](const clap_event_midi_t* e) {
        return reloop_neon::decode({e->data[0], e->data[1], e->data[2]});
    };
    const auto* current = midi(index);
    if (!current) return false;
    const auto selected = action(current);
    if (selected.type != reloop_neon::ActionType::SelectMode) return false;
    const uint32_t distance = selected.pressed ? 1u : 2u;
    if (index < distance) return false;
    const auto* bank = midi(index - distance);
    const auto* press = midi(index - distance + 1u);
    const auto* release = midi(index - distance + 2u);
    if (!bank || !press || !release || bank->header.time != press->header.time
        || bank->header.time != release->header.time || bank->port_index != press->port_index
        || bank->port_index != release->port_index) return false;
    const auto b = action(bank), p = action(press), r = action(release);
    return b.type == reloop_neon::ActionType::SelectBank && b.pressed && !b.shifted
        && p.type == reloop_neon::ActionType::SelectMode && p.pressed
        && r.type == reloop_neon::ActionType::SelectMode && !r.pressed
        && b.bank == p.bank && b.bank == r.bank && p.mode == r.mode && p.layer == r.layer;
}
} // namespace s3g::controller::neon_midi
