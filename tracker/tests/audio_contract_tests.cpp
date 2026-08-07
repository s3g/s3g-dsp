#include "s3g/tracker/audio/audio_node.h"
#include "s3g/tracker/audio/fixed_event_buffer.h"
#include "s3g/tracker/audio/membrane_clap_node.h"
#include "s3g/tracker/audio/tracker_audio_engine.h"

#include <array>
#include <cstdlib>
#include <iostream>
#include <type_traits>

namespace {

using s3g::tracker::audio::AudioLayout;
using s3g::tracker::audio::FixedEventBuffer;
using s3g::tracker::audio::InstrumentEventKind;
using s3g::tracker::audio::InstrumentRenderEvent;
using s3g::tracker::audio::PlanarAudioBlock;

bool expect(bool condition, const char* message)
{
    if (!condition) std::cerr << message << '\n';
    return condition;
}

} // namespace

int main()
{
    static_assert(std::is_class<
            s3g::tracker::audio::TrackerAudioEngine>::value,
        "the public audio graph contract must compile without CLAP headers");
    static_assert(std::is_class<
            s3g::tracker::audio::MembraneClapNode>::value,
        "the public instrument adapter must compile without CLAP headers");
    bool ok = true;
    ok &= expect(s3g::tracker::audio::channelCount(AudioLayout::Stereo) == 2u,
        "stereo layout count is wrong");
    ok &= expect(s3g::tracker::audio::channelCount(AudioLayout::Quad) == 4u,
        "quad layout count is wrong");
    ok &= expect(s3g::tracker::audio::channelCount(
            AudioLayout::HoaThirdOrder) == 16u,
        "third-order HOA layout count is wrong");

    FixedEventBuffer<InstrumentRenderEvent, 2u> events;
    ok &= expect(events.push({ 7u, 1u, InstrumentEventKind::NoteOn,
            36, 0, 17, 0u, 0.75 }),
        "first bounded event was rejected");
    ok &= expect(events.push({ 31u, 1u, InstrumentEventKind::NoteOff,
            36, 0, 17, 0u, 0.0 }),
        "second bounded event was rejected");
    ok &= expect(!events.push({}), "overflowing event was accepted");
    ok &= expect(events.size() == 2u && events.droppedCount() == 1u,
        "bounded event overflow telemetry is wrong");
    ok &= expect(events[0u].frameOffset == 7u
            && events[1u].frameOffset == 31u,
        "bounded events did not retain order");
    events.clear();
    ok &= expect(events.empty() && events.droppedCount() == 1u,
        "clearing a block erased overflow telemetry");

    std::array<float, 4u> left { 1.0f, 2.0f, 3.0f, 4.0f };
    std::array<float, 4u> right { -1.0f, -2.0f, -3.0f, -4.0f };
    std::array<float*, 2u> channels { left.data(), right.data() };
    PlanarAudioBlock block { channels.data(), 2u, 4u };
    ok &= expect(block.valid(), "valid planar output was rejected");
    block.clear();
    for (float sample : left) ok &= expect(sample == 0.0f,
        "left planar output was not cleared");
    for (float sample : right) ok &= expect(sample == 0.0f,
        "right planar output was not cleared");

    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
