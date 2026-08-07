#pragma once

#include "s3g/tracker/sequencer.h"
#include "s3g/tracker/timing_event_types.h"

#include <array>
#include <cstddef>

namespace s3g::tracker {

struct SequencerActionValue {
    SequencerAction action = SequencerAction::Ratchet;
    float normalized = 0.0f;
    bool active = false;
};

// Resolve both polymetric FX pairs at a tracker tick into one timing profile.
// If both pairs name the same action, the later pair wins, matching the v8
// tracker. Distinct actions combine (for example RR + ST or MT + AC).
TimingEventExpansion resolveTimingActions(
    const std::array<SequencerActionValue, kFxPairCount>& actions,
    const TransportSettings& transport, uint64_t tickDurationSamples,
    bool compensateMicroTiming) noexcept;

} // namespace s3g::tracker
