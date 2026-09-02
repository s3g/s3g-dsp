#pragma once

#include "s3g/tracker/sequencer.h"

#include <cstddef>
#include <cstdint>

namespace s3g::tracker {

// Geometry is an alternative editor for the same Track storage used by the
// tracker grid. Keeping its transforms in the core makes gestures
// deterministic, host-independent, and straightforward to cover with tests.
bool geometryCellIsHit(const Track& track, std::size_t row) noexcept;
std::size_t geometryHitCount(const Track& track) noexcept;

bool setGeometryHit(Track& track, std::size_t row, bool hit,
    uint8_t defaultNote);
bool setGeometryVelocity(Track& track, std::size_t row,
    float normalized);
bool rotateGeometryPhase(Track& track, int delta) noexcept;
bool setGeometryDensity(Track& track, std::size_t pulses,
    uint8_t defaultNote);
bool reverseGeometry(Track& track);
bool reflectGeometry(Track& track, std::size_t pivot);
bool morphGeometry(Track& track, const Track& target, float amount,
    uint8_t defaultNote);

} // namespace s3g::tracker
