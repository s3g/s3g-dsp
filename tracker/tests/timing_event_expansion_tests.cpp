#include "s3g/tracker/timing_event_expansion.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {

using s3g::tracker::ScheduledEvent;
using s3g::tracker::TimingEventExpansion;
using s3g::tracker::expandTimingEvent;

int failures = 0;

void check(bool condition, const char* message)
{
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

void testRatchetAndCrossTickStutter()
{
    ScheduledEvent primary;
    primary.absoluteSampleTime = 100u;
    primary.frameOffset = 4u;
    primary.noteId = 10u;
    primary.normalizedVelocity = 0.8f;
    TimingEventExpansion timing;
    timing.tickDurationSamples = 80u;
    timing.baseDelaySamples = 10u;
    timing.ratchetCount = 4u;
    timing.stutterCount = 3u;

    std::array<ScheduledEvent, 8u> events {};
    std::size_t count = 0u;
    uint64_t nextId = 11u;
    expandTimingEvent(primary, timing, [&] { return nextId++; },
        [&](const ScheduledEvent& event) { events[count++] = event; });

    const std::array<uint64_t, 6u> expectedTimes {
        110u, 130u, 150u, 170u, 190u, 270u,
    };
    check(count == expectedTimes.size(),
        "four ratchets plus two stutters should emit six onsets");
    for (std::size_t index = 0u; index < expectedTimes.size(); ++index) {
        check(events[index].absoluteSampleTime == expectedTimes[index],
            "ratchet/stutter onset should use exact integer sample timing");
        check(events[index].frameOffset
                == 4u + expectedTimes[index] - 100u,
            "expanded frame offsets should follow absolute sample timing");
        check(events[index].noteId == 10u + index,
            "every secondary onset should have a stable unique note ID");
    }
}

void testFlamGhostAndVelocityScales()
{
    ScheduledEvent primary;
    primary.absoluteSampleTime = 1000u;
    primary.noteId = 4u;
    primary.normalizedVelocity = 0.8f;
    TimingEventExpansion timing;
    timing.tickDurationSamples = 200u;
    timing.flamEnabled = true;
    timing.flamDelaySamples = 30u;
    timing.ghostEnabled = true;
    timing.velocityScale = 1.25f;
    timing.ghostVelocityScale = 0.25f;

    std::array<ScheduledEvent, 3u> events {};
    std::size_t count = 0u;
    uint64_t nextId = 5u;
    expandTimingEvent(primary, timing, [&] { return nextId++; },
        [&](const ScheduledEvent& event) { events[count++] = event; });
    check(count == 3u && events[0u].absoluteSampleTime == 1000u
            && events[1u].absoluteSampleTime == 1030u
            && events[2u].absoluteSampleTime == 1100u,
        "flam and ghost offsets should be relative to the delayed primary");
    check(std::abs(events[0u].normalizedVelocity - 1.0f) < 0.00001f
            && std::abs(events[1u].normalizedVelocity - 0.72f) < 0.00001f
            && std::abs(events[2u].normalizedVelocity - 0.2f) < 0.00001f,
        "accent, flam attenuation, and ghost velocity should clamp correctly");
}

} // namespace

int main()
{
    testRatchetAndCrossTickStutter();
    testFlamGhostAndVelocityScales();
    if (failures != 0) return EXIT_FAILURE;
    std::cout << "timing event expansion tests passed\n";
    return EXIT_SUCCESS;
}
