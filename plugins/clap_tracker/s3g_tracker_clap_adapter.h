#pragma once

#include "s3g/tracker/clap_midi_engine.h"
#include <clap/clap.h>

namespace s3g::tracker::clap_adapter {

using midi::HostTransport;

// Preserve the native wrapper's beat/seconds fallback and tempo policy.
inline HostTransport readHostTransport(const clap_event_transport_t* source) noexcept
{
    HostTransport result;
    if (!source) return result;
    result.playing = (source->flags & CLAP_TRANSPORT_IS_PLAYING) != 0u;
    if ((source->flags & CLAP_TRANSPORT_HAS_TEMPO) != 0u
        && std::isfinite(source->tempo) && source->tempo > 0.0) {
        result.hasTempo = true;
        result.tempo = source->tempo;
        if (std::isfinite(source->tempo_inc))
            result.tempoIncrement = source->tempo_inc;
    }
    if ((source->flags & CLAP_TRANSPORT_HAS_BEATS_TIMELINE) != 0u) {
        result.beat = static_cast<double>(source->song_pos_beats)
            / static_cast<double>(CLAP_BEATTIME_FACTOR);
        result.hasBeat = std::isfinite(result.beat);
    } else if ((source->flags & CLAP_TRANSPORT_HAS_SECONDS_TIMELINE) != 0u) {
        const double seconds = static_cast<double>(source->song_pos_seconds)
            / static_cast<double>(CLAP_SECTIME_FACTOR);
        result.beat = seconds * result.tempo / 60.0;
        result.hasBeat = std::isfinite(result.beat);
    }
    return result;
}

inline bool readInput(const void* context, uint32_t index,
    midi::InputEvent& result) noexcept
{
    const auto* input = static_cast<const clap_input_events_t*>(context);
    if (!input || !input->get) return false;
    const auto* event = input->get(input, index);
    if (!event || event->space_id != CLAP_CORE_EVENT_SPACE_ID) return false;
    result.time = event->time;
    if (event->type == CLAP_EVENT_TRANSPORT
            && event->size >= sizeof(clap_event_transport_t)) {
        result.kind = midi::InputEvent::Kind::Transport;
        result.transport = readHostTransport(
            reinterpret_cast<const clap_event_transport_t*>(event));
        return true;
    }
    if (event->type == CLAP_EVENT_MIDI
            && event->size >= sizeof(clap_event_midi_t)) {
        result.kind = midi::InputEvent::Kind::Midi;
        const auto& message = *reinterpret_cast<const clap_event_midi_t*>(event);
        result.midi.port_index = message.port_index;
        for (unsigned i = 0; i < 3; ++i) result.midi.data[i] = message.data[i];
        return true;
    }
    return false;
}

inline bool pushMidi(const void* context, uint32_t offset,
    uint8_t status, uint8_t data1, uint8_t data2) noexcept
{
    const auto* output = static_cast<const clap_output_events_t*>(context);
    if (!output || !output->try_push) return false;
    clap_event_midi_t event {};
    event.header.size = sizeof(event);
    event.header.time = offset;
    event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    event.header.type = CLAP_EVENT_MIDI;
    event.port_index = 0;
    event.data[0] = status;
    event.data[1] = data1;
    event.data[2] = data2;
    return output->try_push(output, &event.header);
}

inline midi::HostServices hostServices(const clap_host_t* host) noexcept
{
    midi::HostServices services;
    services.context = host;
    services.requestProcess = [](const void* context) noexcept {
        const auto* h = static_cast<const clap_host_t*>(context);
        if (h && h->request_process) h->request_process(h);
    };
    services.requestCallback = [](const void* context) noexcept {
        const auto* h = static_cast<const clap_host_t*>(context);
        if (h && h->request_callback) h->request_callback(h);
    };
    services.markDirty = [](const void* context) noexcept {
        const auto* h = static_cast<const clap_host_t*>(context);
        if (!h || !h->get_extension) return;
        const auto* state = static_cast<const clap_host_state_t*>(
            h->get_extension(h, CLAP_EXT_STATE));
        if (state && state->mark_dirty) state->mark_dirty(h);
    };
    return services;
}

inline clap_process_status process(midi::Engine& engine,
    const clap_process_t* source) noexcept
{
    if (!source) return CLAP_PROCESS_ERROR;
    midi::ProcessData data;
    data.frames_count = source->frames_count;
    data.transport = readHostTransport(source->transport);
    data.in_events.context = source->in_events;
    data.in_events.count = source->in_events && source->in_events->size
        ? source->in_events->size(source->in_events) : 0;
    data.in_events.get = readInput;
    const midi::MidiOutput output {source->out_events, pushMidi};
    data.out_events = source->out_events ? &output : nullptr;
    midi::process(engine, data);
    return CLAP_PROCESS_CONTINUE;
}

} // namespace s3g::tracker::clap_adapter

