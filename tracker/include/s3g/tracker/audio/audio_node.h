#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace s3g::tracker::audio {

constexpr uint32_t kInitialMaximumBlockFrames = 4096u;
constexpr uint32_t kInitialMaximumNodeEvents = 1024u;
constexpr uint32_t kInitialMaximumAudioChannels = 16u;

enum class AudioLayout : uint8_t {
    Stereo,
    Quad,
    HoaThirdOrder,
    DirectSixteen,
};

constexpr uint32_t channelCount(AudioLayout layout) noexcept
{
    switch (layout) {
    case AudioLayout::Stereo: return 2u;
    case AudioLayout::Quad: return 4u;
    case AudioLayout::HoaThirdOrder:
    case AudioLayout::DirectSixteen: return 16u;
    }
    return 0u;
}

// Graph targets remain explicit adapter edges. The current tracker exposes
// internal DSP and MIDI OUT as separate rack instruments; a future layered
// rack instrument may publish more than one RouteTarget deliberately.
enum class RouteTargetKind : uint8_t {
    ExternalMidi,
    NativeInstrument,
    EmbeddedClap,
};

struct RouteTarget {
    RouteTargetKind kind = RouteTargetKind::ExternalMidi;
    uint32_t node = 0u;
    uint16_t port = 0u;
    uint16_t channel = 0u;
    bool enabled = true;
};

enum class InstrumentEventKind : uint8_t {
    NoteOn,
    NoteOff,
    Choke,
    ParameterValue,
};

enum class InstrumentParameterScope : uint8_t {
    Global,
    Channel,
    Note,
};

// Adapter-local event consumed by an InstrumentNode. The sequencer's
// canonical event can evolve independently and be converted at the route
// boundary. frameOffset is always relative to the current render block.
struct InstrumentRenderEvent {
    uint32_t frameOffset = 0u;
    uint32_t sourceLane = 0u;
    InstrumentEventKind kind = InstrumentEventKind::NoteOn;
    int16_t key = 60;
    int16_t channel = 0;
    // Preserve the canonical scheduler identity. A CLAP adapter narrows a
    // nonzero value to its per-instance int32 voice table at the edge.
    uint64_t noteId = 0u;
    uint32_t parameterId = 0u;
    // Normalized canonical value; the concrete node maps it to its native
    // parameter range using control-thread metadata.
    double value = 0.0;
    InstrumentParameterScope parameterScope =
        InstrumentParameterScope::Global;
};

static_assert(std::is_trivially_copyable<InstrumentRenderEvent>::value,
    "InstrumentRenderEvent must remain callback-safe");

struct AudioRenderSpec {
    double sampleRate = 48000.0;
    uint32_t minimumFrames = 1u;
    uint32_t maximumFrames = kInitialMaximumBlockFrames;
};

struct PlanarAudioBlock {
    float* const* channels = nullptr;
    uint32_t channelCount = 0u;
    uint32_t frameCount = 0u;

    bool valid() const noexcept
    {
        if (!channels || channelCount == 0u || frameCount == 0u)
            return false;
        for (uint32_t channel = 0u; channel < channelCount; ++channel) {
            if (!channels[channel]) return false;
        }
        return true;
    }

    void clear() const noexcept
    {
        if (!channels) return;
        for (uint32_t channel = 0u; channel < channelCount; ++channel) {
            if (channels[channel]) {
                std::fill_n(channels[channel], frameCount, 0.0f);
            }
        }
    }
};

// Lifecycle and parameter/state mutation happen on the control thread while
// stopped. reset() and render() are audio-thread calls and must not allocate,
// lock, perform I/O, or call Cocoa.
class InstrumentNode {
public:
    virtual ~InstrumentNode() = default;

    virtual bool prepare(const AudioRenderSpec& spec) = 0;
    virtual void unprepare() = 0;
    // CLAP-style process-thread lifecycle is explicit. prepare/unprepare run
    // on the control thread while stopped; these two calls and reset/render
    // run on the render thread.
    virtual bool startProcessing() noexcept = 0;
    virtual void stopProcessing() noexcept = 0;
    virtual bool isProcessing() const noexcept = 0;
    virtual void reset() noexcept = 0;
    virtual AudioLayout outputLayout() const noexcept = 0;
    virtual uint32_t latencyFrames() const noexcept = 0;

    virtual void render(const InstrumentRenderEvent* events,
        std::size_t eventCount, const PlanarAudioBlock& output) noexcept = 0;
};

} // namespace s3g::tracker::audio
