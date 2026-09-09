#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace s3g::portable_gui {

// Published transport contract, not a stream of repaint positions. Routine
// anticipative DSP publications must not re-anchor the presentation clock.
struct SampleCursorTrajectory {
    uintptr_t asset = 0;
    uint64_t identity = 0, discontinuity = 0;
    double position = -1, low = 0, high = 1, rate = 0;
    bool running = false, loop = false, pingPong = false;
    bool smoothObserved = false; // Motion's original 30 Hz interpolation.
};

class SampleCursorClock {
public:
    bool synchronize(const SampleCursorTrajectory& next, double now)
    {
        const bool hard = !initialized || next.asset != contract.asset
            || next.identity != contract.identity
            || next.discontinuity != contract.discontinuity
            || (next.position >= 0) != (contract.position >= 0);
        const bool changed = hard || next.low != contract.low
            || next.high != contract.high || next.rate != contract.rate
            || next.running != contract.running || next.loop != contract.loop
            || next.pingPong != contract.pingPong
            || next.smoothObserved != contract.smoothObserved
            || (next.smoothObserved && next.position != contract.position);
        if (!changed) return false;
        const double previous = positionAt(now);
        const double previousRate = rateAt(now);
        const bool preserve = !hard && contract.running;
        anchorPosition = preserve ? previous : next.position;
        if (next.smoothObserved && !hard) anchorPosition = previous;
        anchorRate = preserve && next.pingPong
            ? std::copysign(std::abs(next.rate), previousRate) : next.rate;
        contract = next;
        anchorTime = now;
        initialized = true;
        return true;
    }

    double positionAt(double now) const
    {
        if (!initialized || contract.position < 0) return -1;
        const double elapsed = std::max(0.0, now - anchorTime);
        if (contract.smoothObserved)
            return anchorPosition + (contract.position - anchorPosition)
                * std::clamp(elapsed * 30.0, 0.0, 1.0);
        if (!contract.running || !(contract.high > contract.low))
            return anchorPosition;
        const double span = contract.high - contract.low;
        double p = anchorPosition + elapsed * contract.rate;
        if (!contract.loop) return std::clamp(p, contract.low, contract.high);
        if (contract.pingPong) {
            // Encode direction in an unfolded triangular-wave phase.
            p = anchorRate >= 0 ? anchorPosition - contract.low
                : 2.0 * span - (anchorPosition - contract.low);
            p = positiveModulo(p + elapsed * std::abs(contract.rate), 2 * span);
            return contract.low + (p <= span ? p : 2 * span - p);
        }
        return contract.low + positiveModulo(p - contract.low, span);
    }

    double rateAt(double now) const
    {
        if (!contract.running) return 0;
        if (!contract.pingPong || !contract.loop) return contract.rate;
        const double span = contract.high - contract.low;
        if (!(span > 0)) return 0;
        double p = anchorRate >= 0 ? anchorPosition - contract.low
            : 2 * span - (anchorPosition - contract.low);
        p = positiveModulo(p + std::max(0.0, now - anchorTime)
            * std::abs(contract.rate), 2 * span);
        return (p < span ? 1 : -1) * std::abs(contract.rate);
    }

    const SampleCursorTrajectory& trajectory() const { return contract; }
    static double positiveModulo(double value, double span)
    {
        double result = std::fmod(value, span);
        return result < 0 ? result + span : result;
    }

private:
    SampleCursorTrajectory contract {};
    double anchorTime = 0, anchorPosition = -1, anchorRate = 0;
    bool initialized = false;
};
} // namespace s3g::portable_gui
