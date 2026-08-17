#include "s3g/tracker/timing_action_resolver.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace s3g::tracker {
namespace {

uint64_t roundedSamples(long double value) noexcept
{
    if (!(value > 0.0L)) return 0u;
    const long double maximum = static_cast<long double>(
        std::numeric_limits<uint64_t>::max());
    if (value >= maximum) return std::numeric_limits<uint64_t>::max();
    return static_cast<uint64_t>(std::floor(value + 0.5L));
}

long double millisecondsInSamples(double milliseconds,
    double sampleRate) noexcept
{
    return static_cast<long double>(milliseconds)
        * static_cast<long double>(sampleRate) * 0.001L;
}

uint8_t repetitionCount(float normalized) noexcept
{
    const float amount = std::clamp(normalized, 0.0f, 1.0f);
    return static_cast<uint8_t>(std::clamp<long>(
        std::lround(2.0f + amount * 6.0f), 2l, 8l));
}

} // namespace

TimingEventExpansion resolveTimingActions(
    const std::array<SequencerActionValue, kFxPairCount>& actions,
    const TransportSettings& transport, uint64_t tickDurationSamples,
    bool compensateMicroTiming) noexcept
{
    TimingEventExpansion result;
    result.tickDurationSamples = std::max<uint64_t>(
        tickDurationSamples, 1u);

    bool hasMicroTime = false;
    float microTime = 0.5f;
    float delay = 0.0f;
    bool hasDelay = false;
    for (const auto& entry : actions) {
        if (!entry.active) continue;
        const float value = std::clamp(entry.normalized, 0.0f, 1.0f);
        switch (entry.action) {
        case SequencerAction::Ratchet:
            result.ratchetCount = repetitionCount(value);
            break;
        case SequencerAction::MicroTime:
            hasMicroTime = true;
            microTime = value;
            break;
        case SequencerAction::Delay:
            hasDelay = true;
            delay = value;
            break;
        case SequencerAction::Flam:
            result.flamEnabled = true;
            result.flamDelaySamples = roundedSamples(millisecondsInSamples(
                6.0 + static_cast<double>(value) * 54.0,
                transport.sampleRate));
            break;
        case SequencerAction::Stutter:
            result.stutterCount = repetitionCount(value);
            break;
        case SequencerAction::Accent:
            result.velocityScale = 0.5f + value;
            break;
        case SequencerAction::Ghost:
            result.ghostEnabled = true;
            result.ghostVelocityScale = 0.15f + value * 0.45f;
            break;
        // These actions transform/gate the primary note source in Sequencer
        // before it reaches this timing-expansion stage.
        case SequencerAction::Probability:
        case SequencerAction::Skip:
        case SequencerAction::Offset:
        case SequencerAction::RepeatPrevious:
        case SequencerAction::Euclid:
        case SequencerAction::Count:
            break;
        }
    }

    long double baseDelay = compensateMicroTiming
        ? millisecondsInSamples(transport.timingLookaheadMilliseconds,
              transport.sampleRate)
        : 0.0L;
    if (hasMicroTime) {
        baseDelay += static_cast<long double>((microTime - 0.5f) * 2.0f)
            * millisecondsInSamples(
                transport.microTimingRangeMilliseconds,
                transport.sampleRate);
    }
    if (hasDelay) {
        baseDelay += static_cast<long double>(delay)
            * static_cast<long double>(result.tickDurationSamples);
    }
    result.baseDelaySamples = roundedSamples(std::max(0.0L, baseDelay));
    return result;
}

} // namespace s3g::tracker
