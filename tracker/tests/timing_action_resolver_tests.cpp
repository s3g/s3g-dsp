#include "s3g/tracker/timing_action_resolver.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {

using s3g::tracker::SequencerAction;
using s3g::tracker::SequencerActionValue;
using s3g::tracker::TransportSettings;
using s3g::tracker::resolveTimingActions;

int failures = 0;

void check(bool condition, const char* message)
{
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

SequencerActionValue action(SequencerAction kind, float value)
{
    return { kind, value, true };
}

void testV8Mappings()
{
    TransportSettings transport;
    transport.sampleRate = 48000.0;
    transport.timingLookaheadMilliseconds = 25.0;
    transport.microTimingRangeMilliseconds = 25.0;

    auto resolved = resolveTimingActions({
        action(SequencerAction::Ratchet, 1.0f),
        action(SequencerAction::Stutter, 0.0f),
    }, transport, 6000u, false);
    check(resolved.ratchetCount == 8u && resolved.stutterCount == 2u,
        "RR and ST should map normalized values to 2..8 repetitions");

    resolved = resolveTimingActions({
        action(SequencerAction::MicroTime, 0.0f),
        action(SequencerAction::Delay, 0.5f),
    }, transport, 6000u, true);
    check(resolved.baseDelaySamples == 3000u,
        "earliest MT should cancel lookahead before a half-tick DL");

    resolved = resolveTimingActions({
        action(SequencerAction::Accent, 1.0f),
        action(SequencerAction::Ghost, 1.0f),
    }, transport, 6000u, false);
    check(std::abs(resolved.velocityScale - 1.5f) < 0.00001f
            && std::abs(resolved.ghostVelocityScale - 0.6f) < 0.00001f,
        "AC and GL should retain the v8 velocity ranges");

    resolved = resolveTimingActions({
        action(SequencerAction::Flam, 1.0f),
        {},
    }, transport, 6000u, false);
    check(resolved.flamEnabled && resolved.flamDelaySamples == 2880u,
        "FL should map 1.0 to sixty milliseconds");
}

void testLaterPairWinsAndCompensation()
{
    TransportSettings transport;
    transport.sampleRate = 48000.0;
    transport.timingLookaheadMilliseconds = 25.0;
    transport.microTimingRangeMilliseconds = 25.0;
    auto resolved = resolveTimingActions({
        action(SequencerAction::Ratchet, 0.0f),
        action(SequencerAction::Ratchet, 1.0f),
    }, transport, 6000u, true);
    check(resolved.ratchetCount == 8u
            && resolved.baseDelaySamples == 1200u,
        "FX2 should win duplicate actions and MT compensation should delay all lanes");
}

} // namespace

int main()
{
    testV8Mappings();
    testLaterPairWinsAndCompensation();
    if (failures != 0) return EXIT_FAILURE;
    std::cout << "timing action resolver tests passed\n";
    return EXIT_SUCCESS;
}
