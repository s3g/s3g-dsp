#include "s3g/tracker/timing_warp.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace s3g::tracker {
namespace {

double unitPhase(double value) noexcept
{
    if (std::isnan(value)) return 0.0;
    if (value <= 0.0) return 0.0;
    if (value >= 1.0) return 1.0;
    return value;
}

bool supportedKind(TimingWarpKind kind) noexcept
{
    switch (kind) {
    case TimingWarpKind::Exponential:
    case TimingWarpKind::StepQuantize:
    case TimingWarpKind::EuclideanQuantize:
        return true;
    }
    return false;
}

uint32_t normalizedSteps(uint32_t value,
    TimingWarpCorrection& corrections) noexcept
{
    if (value == 0u) {
        corrections |= TimingWarpCorrection::Steps;
        return 1u;
    }
    if (value > TimingWarpStack::kMaximumSteps) {
        corrections |= TimingWarpCorrection::Steps;
        return TimingWarpStack::kMaximumSteps;
    }
    return value;
}

double stableScaledPhase(double phase, uint32_t subdivisions) noexcept
{
    double scaled = phase * static_cast<double>(subdivisions);
    const double nearest = std::round(scaled);
    const double tolerance = 8.0 * std::numeric_limits<double>::epsilon()
        * std::max(1.0, std::abs(scaled));
    // Ratios such as j/7 are not always represented so that multiplying by 7
    // lands exactly on j. Snap only round-off-sized differences, keeping
    // mathematically exact grid anchors stable across construction paths.
    if (std::abs(scaled - nearest) <= tolerance) scaled = nearest;
    return scaled;
}

uint32_t stableSubdivisionIndex(double phase,
    uint32_t subdivisions) noexcept
{
    const double scaled = stableScaledPhase(phase, subdivisions);
    uint32_t index = static_cast<uint32_t>(std::floor(scaled));
    if (index >= subdivisions) index = subdivisions - 1u;
    return index;
}

TimingWarpAppendResult normalize(TimingWarpTransform& transform) noexcept
{
    TimingWarpAppendResult result;
    if (!supportedKind(transform.kind)) {
        result.status = TimingWarpAppendStatus::UnsupportedKind;
        return result;
    }

    auto& options = transform.options;
    if (!std::isfinite(options.phaseBegin)) {
        options.phaseBegin = 0.0;
        result.corrections |= TimingWarpCorrection::NonFiniteSegment;
    }
    if (!std::isfinite(options.phaseEnd)) {
        options.phaseEnd = 1.0;
        result.corrections |= TimingWarpCorrection::NonFiniteSegment;
    }

    const double begin = std::clamp(options.phaseBegin, 0.0, 1.0);
    const double end = std::clamp(options.phaseEnd, 0.0, 1.0);
    if (begin != options.phaseBegin || end != options.phaseEnd)
        result.corrections |= TimingWarpCorrection::ClampedSegment;
    options.phaseBegin = begin;
    options.phaseEnd = end;
    if (options.phaseBegin > options.phaseEnd) {
        std::swap(options.phaseBegin, options.phaseEnd);
        result.corrections |= TimingWarpCorrection::ReversedSegment;
    }
    if (options.phaseBegin == options.phaseEnd) {
        result.status = TimingWarpAppendStatus::DegenerateSegment;
        return result;
    }

    if (options.repetitions == 0u) {
        options.repetitions = 1u;
        result.corrections |= TimingWarpCorrection::Repetitions;
    } else if (options.repetitions
        > TimingWarpStack::kMaximumRepetitions) {
        options.repetitions = TimingWarpStack::kMaximumRepetitions;
        result.corrections |= TimingWarpCorrection::Repetitions;
    }

    if (!std::isfinite(options.alpha)) {
        options.alpha = 1.0;
        result.corrections |= TimingWarpCorrection::Alpha;
    } else {
        const double alpha = std::clamp(options.alpha, 0.0, 1.0);
        if (alpha != options.alpha)
            result.corrections |= TimingWarpCorrection::Alpha;
        options.alpha = alpha;
    }

    switch (transform.kind) {
    case TimingWarpKind::Exponential:
        if (!std::isfinite(transform.exponent)) {
            transform.exponent = 1.0;
            result.corrections |= TimingWarpCorrection::Exponent;
        } else {
            const double exponent = std::clamp(transform.exponent,
                TimingWarpStack::kMinimumExponent,
                TimingWarpStack::kMaximumExponent);
            if (exponent != transform.exponent)
                result.corrections |= TimingWarpCorrection::Exponent;
            transform.exponent = exponent;
        }
        transform.pulses = 1u;
        transform.steps = 1u;
        break;
    case TimingWarpKind::StepQuantize:
        transform.exponent = 1.0;
        transform.steps = normalizedSteps(transform.steps,
            result.corrections);
        transform.pulses = transform.steps;
        break;
    case TimingWarpKind::EuclideanQuantize:
        transform.exponent = 1.0;
        transform.steps = normalizedSteps(transform.steps,
            result.corrections);
        if (transform.pulses == 0u) {
            transform.pulses = 1u;
            result.corrections |= TimingWarpCorrection::Pulses;
        } else if (transform.pulses > transform.steps) {
            transform.pulses = transform.steps;
            result.corrections |= TimingWarpCorrection::Pulses;
        }
        break;
    }
    return result;
}

double stepCurve(double phase, uint32_t steps) noexcept
{
    if (phase <= 0.0) return 0.0;
    if (phase >= 1.0) return 1.0;

    const uint32_t gridIndex = stableSubdivisionIndex(phase, steps);
    return static_cast<double>(gridIndex) / static_cast<double>(steps);
}

double euclideanCurve(double phase, uint32_t pulses,
    uint32_t steps) noexcept
{
    if (phase <= 0.0) return 0.0;
    if (phase >= 1.0) return 1.0;

    // Pulse j begins on ceil(j * steps / pulses). Sampling at the evenly
    // spaced source anchors j/pulses therefore yields a left-anchored modular
    // Euclidean distribution. For example E(2,5) maps 0.5 to 3/5, the familiar
    // quintuplet swing upbeat. Holding each anchor until the next one makes
    // this the intended nondecreasing step quantizer; alpha restores a
    // continuous slope when less than one.
    const uint32_t pulseIndex = stableSubdivisionIndex(phase, pulses);
    const uint64_t numerator = static_cast<uint64_t>(pulseIndex)
        * static_cast<uint64_t>(steps);
    const uint64_t gridIndex = (numerator
        + static_cast<uint64_t>(pulses) - 1u)
        / static_cast<uint64_t>(pulses);
    return static_cast<double>(gridIndex) / static_cast<double>(steps);
}

double curve(double phase, const TimingWarpTransform& transform) noexcept
{
    switch (transform.kind) {
    case TimingWarpKind::Exponential:
        return std::pow(phase, transform.exponent);
    case TimingWarpKind::StepQuantize:
        return stepCurve(phase, transform.steps);
    case TimingWarpKind::EuclideanQuantize:
        return euclideanCurve(phase, transform.pulses, transform.steps);
    }
    return phase;
}

double repeatedCurve(double phase,
    const TimingWarpTransform& transform) noexcept
{
    if (phase <= 0.0) return 0.0;
    if (phase >= 1.0) return 1.0;

    const uint32_t repetitions = transform.options.repetitions;
    const double scaled = stableScaledPhase(phase, repetitions);
    uint32_t repetition = static_cast<uint32_t>(std::floor(scaled));
    if (repetition >= repetitions) repetition = repetitions - 1u;
    const double localPhase = std::clamp(
        scaled - static_cast<double>(repetition), 0.0, 1.0);
    const double localResult = unitPhase(curve(localPhase, transform));
    return (static_cast<double>(repetition) + localResult)
        / static_cast<double>(repetitions);
}

double apply(double phase, const TimingWarpTransform& transform) noexcept
{
    const auto& options = transform.options;
    if (phase <= options.phaseBegin || phase >= options.phaseEnd)
        return phase;

    const double span = options.phaseEnd - options.phaseBegin;
    const double localPhase = unitPhase(
        (phase - options.phaseBegin) / span);
    const double transformed = repeatedCurve(localPhase, transform);
    const double blended = localPhase
        + options.alpha * (transformed - localPhase);
    return std::clamp(options.phaseBegin + span * blended,
        options.phaseBegin, options.phaseEnd);
}

} // namespace

TimingWarpTransform TimingWarpTransform::exponential(double newExponent,
    TimingWarpOptions newOptions) noexcept
{
    TimingWarpTransform transform;
    transform.kind = TimingWarpKind::Exponential;
    transform.options = newOptions;
    transform.exponent = newExponent;
    return transform;
}

TimingWarpTransform TimingWarpTransform::stepQuantize(uint32_t newSteps,
    TimingWarpOptions newOptions) noexcept
{
    TimingWarpTransform transform;
    transform.kind = TimingWarpKind::StepQuantize;
    transform.options = newOptions;
    transform.steps = newSteps;
    transform.pulses = newSteps;
    return transform;
}

TimingWarpTransform TimingWarpTransform::euclideanQuantize(
    uint32_t newPulses, uint32_t newSteps,
    TimingWarpOptions newOptions) noexcept
{
    TimingWarpTransform transform;
    transform.kind = TimingWarpKind::EuclideanQuantize;
    transform.options = newOptions;
    transform.pulses = newPulses;
    transform.steps = newSteps;
    return transform;
}

TimingWarpAppendResult TimingWarpStack::append(
    TimingWarpTransform newTransform) noexcept
{
    auto result = normalize(newTransform);
    if (!result.added()) return result;
    if (size_ >= transforms_.size()) {
        result.status = TimingWarpAppendStatus::CapacityExceeded;
        return result;
    }
    transforms_[size_++] = newTransform;
    return result;
}

TimingWarpCompileReport TimingWarpStack::compile(
    const TimingWarpTransform* newTransforms, std::size_t count) noexcept
{
    clear();
    TimingWarpCompileReport report;
    report.requested = count;
    if (!newTransforms && count > 0u) {
        report.rejected = count;
        report.sourceWasNull = true;
        return report;
    }

    for (std::size_t index = 0u; index < count; ++index) {
        const auto result = append(newTransforms[index]);
        report.corrections |= result.corrections;
        if (result.added()) {
            ++report.added;
        } else {
            ++report.rejected;
            if (result.status == TimingWarpAppendStatus::CapacityExceeded)
                report.capacityExceeded = true;
        }
    }
    return report;
}

const TimingWarpTransform* TimingWarpStack::transform(
    std::size_t index) const noexcept
{
    return index < size_ ? &transforms_[index] : nullptr;
}

double TimingWarpStack::map(double normalizedPhase) const noexcept
{
    double phase = unitPhase(normalizedPhase);
    for (std::size_t index = 0u; index < size_; ++index)
        phase = apply(phase, transforms_[index]);
    return unitPhase(phase);
}

std::size_t TimingWarpStack::precompute(double* output,
    std::size_t valueCount) const noexcept
{
    if (!output || valueCount == 0u) return 0u;
    if (valueCount == 1u) {
        output[0] = map(0.0);
        return 1u;
    }

    const double denominator = static_cast<double>(valueCount - 1u);
    for (std::size_t index = 0u; index < valueCount; ++index) {
        output[index] = map(static_cast<double>(index) / denominator);
    }
    return valueCount;
}

bool TimingWarpLibrary::store(std::size_t index, std::string name,
    uint32_t cycleTicks, const TimingWarpStack& stack)
{
    if (index >= entries_.size() || cycleTicks == 0u
        || cycleTicks > kMaximumLiveWarpCycleTicks
        || name.size() > kMaximumTimingWarpLibraryNameBytes) return false;
    auto& target = entries_[index];
    target.name = std::move(name);
    target.cycleTicks = cycleTicks;
    target.stack = stack;
    target.occupied = true;
    return true;
}

bool TimingWarpLibrary::erase(std::size_t index) noexcept
{
    if (index >= entries_.size() || !entries_[index].occupied) return false;
    entries_[index] = {};
    return true;
}

void TimingWarpLibrary::clear() noexcept
{
    for (auto& entry : entries_) entry = {};
}

const TimingWarpLibraryEntry* TimingWarpLibrary::entry(
    std::size_t index) const noexcept
{
    return index < entries_.size() && entries_[index].occupied
        ? &entries_[index] : nullptr;
}

TimingWarpLibraryEntry* TimingWarpLibrary::entry(
    std::size_t index) noexcept
{
    return index < entries_.size() && entries_[index].occupied
        ? &entries_[index] : nullptr;
}

std::size_t TimingWarpLibrary::size() const noexcept
{
    std::size_t count = 0u;
    for (const auto& entry : entries_)
        if (entry.occupied) ++count;
    return count;
}

std::size_t timingWarpLibraryIndexFromNormalized(float normalized) noexcept
{
    if (!std::isfinite(normalized)) normalized = 0.0f;
    const auto last = kMaximumTimingWarpLibraryEntries - 1u;
    return static_cast<std::size_t>(std::clamp<long>(std::lround(
        static_cast<double>(std::clamp(normalized, 0.0f, 1.0f))
            * static_cast<double>(last)), 0l, static_cast<long>(last)));
}

float timingWarpLibraryNormalizedFromIndex(std::size_t index) noexcept
{
    const auto last = kMaximumTimingWarpLibraryEntries - 1u;
    return static_cast<float>(std::min(index, last))
        / static_cast<float>(last);
}

} // namespace s3g::tracker
