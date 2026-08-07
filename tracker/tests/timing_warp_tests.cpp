#include "s3g/tracker/timing_warp.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>

namespace {

using s3g::tracker::hasCorrection;
using s3g::tracker::TimingWarpAppendStatus;
using s3g::tracker::TimingWarpCorrection;
using s3g::tracker::TimingWarpKind;
using s3g::tracker::TimingWarpOptions;
using s3g::tracker::TimingWarpStack;
using s3g::tracker::TimingWarpTransform;

static_assert(std::is_trivially_copyable<TimingWarpStack>::value,
    "compiled stacks should be publishable without ownership transfer");
static_assert(noexcept(std::declval<const TimingWarpStack&>().map(0.5)),
    "audio-thread phase mapping must be noexcept");

int failures = 0;

void check(bool condition, const std::string& message)
{
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

void checkNear(double actual, double expected, double tolerance,
    const std::string& message)
{
    if (std::abs(actual - expected) <= tolerance) return;
    std::cerr << "FAIL: " << message << " (expected " << expected
              << ", got " << actual << ")\n";
    ++failures;
}

void testIdentityAndInputNormalization()
{
    const TimingWarpStack stack;
    checkNear(stack.map(0.0), 0.0, 0.0,
        "an empty stack must preserve the lower endpoint");
    checkNear(stack.map(0.375), 0.375, 0.0,
        "an empty stack must be the identity mapping");
    checkNear(stack.map(1.0), 1.0, 0.0,
        "an empty stack must preserve the upper endpoint");
    checkNear(stack.map(-3.0), 0.0, 0.0,
        "negative input phase must clamp to zero");
    checkNear(stack.map(4.0), 1.0, 0.0,
        "input phase above one must clamp to one");
    checkNear(stack.map(std::numeric_limits<double>::quiet_NaN()), 0.0,
        0.0, "NaN input phase must deterministically map to zero");
    checkNear(stack.map(std::numeric_limits<double>::infinity()), 1.0,
        0.0, "positive infinity must clamp to one");
    checkNear(stack.map(-std::numeric_limits<double>::infinity()), 0.0,
        0.0, "negative infinity must clamp to zero");
}

void testExponentialSegmentAndAlpha()
{
    TimingWarpStack stack;
    auto result = stack.append(TimingWarpTransform::exponential(2.0));
    check(result.added(), "a valid exponential transform must be accepted");
    checkNear(stack.map(0.25), 0.0625, 1.0e-15,
        "an exponent must bend normalized phase");
    checkNear(stack.map(0.5), 0.25, 1.0e-15,
        "an exponent must use the incoming normalized phase");
    checkNear(stack.map(0.0), 0.0, 0.0,
        "an exponential curve must preserve zero");
    checkNear(stack.map(1.0), 1.0, 0.0,
        "an exponential curve must preserve one");

    stack.clear();
    TimingWarpOptions segment;
    segment.phaseBegin = 0.25;
    segment.phaseEnd = 0.75;
    result = stack.append(TimingWarpTransform::exponential(2.0, segment));
    check(result.added(), "a finite phase segment must be accepted");
    checkNear(stack.map(0.2), 0.2, 0.0,
        "phase before a selected segment must pass through");
    checkNear(stack.map(0.5), 0.375, 1.0e-15,
        "a selected segment must be normalized, curved, and restored");
    checkNear(stack.map(0.8), 0.8, 0.0,
        "phase after a selected segment must pass through");
    checkNear(stack.map(0.25), 0.25, 0.0,
        "the beginning of a segment must remain invariant");
    checkNear(stack.map(0.75), 0.75, 0.0,
        "the end of a segment must remain invariant");

    stack.clear();
    TimingWarpOptions blend;
    blend.alpha = 0.25;
    stack.append(TimingWarpTransform::exponential(2.0, blend));
    checkNear(stack.map(0.5), 0.4375, 1.0e-15,
        "alpha must interpolate from incoming to transformed phase");

    stack.clear();
    blend.alpha = 0.0;
    stack.append(TimingWarpTransform::exponential(12.0, blend));
    checkNear(stack.map(0.371), 0.371, 0.0,
        "zero alpha must make a transform an exact identity");
}

void testRepetitions()
{
    TimingWarpOptions options;
    options.repetitions = 4u;
    TimingWarpStack stack;
    stack.append(TimingWarpTransform::exponential(2.0, options));
    checkNear(stack.map(0.375), 0.3125, 1.0e-15,
        "repetitions must curve independently inside each subdivision");
    checkNear(stack.map(0.625), 0.5625, 1.0e-15,
        "each repeated curve must use the correct subdivision offset");
    for (uint32_t boundary = 0u; boundary <= 4u; ++boundary) {
        const double phase = static_cast<double>(boundary) / 4.0;
        checkNear(stack.map(phase), phase, 0.0,
            "repetition boundaries must remain invariant");
    }

    stack.clear();
    options.phaseBegin = 0.2;
    options.phaseEnd = 0.8;
    options.repetitions = 3u;
    stack.append(TimingWarpTransform::exponential(2.0, options));
    checkNear(stack.map(0.4), 0.4, 1.0e-15,
        "segment subdivisions must preserve their internal boundaries");
    checkNear(stack.map(0.5), 0.45, 1.0e-15,
        "segment and repetition normalization must compose correctly");

    stack.clear();
    options = {};
    options.repetitions = 25u;
    stack.append(TimingWarpTransform::exponential(
        TimingWarpStack::kMinimumExponent, options));
    checkNear(stack.map(14.0 / 25.0), 14.0 / 25.0, 0.0,
        "round-off at a repeated subdivision must preserve its anchor");
}

void testStepQuantization()
{
    TimingWarpStack stack;
    stack.append(TimingWarpTransform::stepQuantize(4u));
    checkNear(stack.map(0.0), 0.0, 0.0,
        "step quantization must preserve zero");
    checkNear(stack.map(0.1), 0.0, 0.0,
        "step quantization must hold the preceding grid point");
    checkNear(stack.map(0.25), 0.25, 0.0,
        "step quantization must retain exact grid points");
    checkNear(stack.map(0.749), 0.5, 1.0e-15,
        "step quantization must remain on the active plateau");
    checkNear(stack.map(0.999), 0.75, 1.0e-15,
        "step quantization must not reach one before the endpoint");
    checkNear(stack.map(1.0), 1.0, 0.0,
        "step quantization must preserve one");

    stack.clear();
    TimingWarpOptions blend;
    blend.alpha = 0.5;
    stack.append(TimingWarpTransform::stepQuantize(4u, blend));
    checkNear(stack.map(0.6), 0.55, 1.0e-15,
        "step alpha must retain a proportional incoming slope");
}

void testEuclideanQuantization()
{
    TimingWarpStack stack;
    stack.append(TimingWarpTransform::euclideanQuantize(2u, 5u));
    checkNear(stack.map(0.499), 0.0, 0.0,
        "E(2,5) must hold the downbeat until its second source pulse");
    checkNear(stack.map(0.5), 0.6, 1.0e-15,
        "E(2,5) must place its upbeat at three fifths");
    checkNear(stack.map(0.999), 0.6, 1.0e-15,
        "E(2,5) must hold the upbeat until the cycle endpoint");
    checkNear(stack.map(1.0), 1.0, 0.0,
        "Euclidean quantization must preserve the cycle endpoint");

    stack.clear();
    stack.append(TimingWarpTransform::euclideanQuantize(5u, 8u));
    const std::array<double, 6u> expected {
        0.0, 0.25, 0.5, 0.625, 0.875, 1.0,
    };
    for (std::size_t pulse = 0u; pulse < expected.size(); ++pulse) {
        const double phase = static_cast<double>(pulse) / 5.0;
        checkNear(stack.map(phase), expected[pulse], 1.0e-15,
            "E(5,8) source anchors must use evenly distributed grid onsets");
    }

    TimingWarpStack steps;
    TimingWarpStack equalEuclidean;
    steps.append(TimingWarpTransform::stepQuantize(7u));
    equalEuclidean.append(
        TimingWarpTransform::euclideanQuantize(7u, 7u));
    for (std::size_t index = 0u; index <= 1000u; ++index) {
        const double phase = static_cast<double>(index) / 1000.0;
        checkNear(steps.map(phase), equalEuclidean.map(phase), 0.0,
            "ordinary step quantization must equal E(n,n)");
    }
}

void testSerialCompositionAndOrdering()
{
    TimingWarpStack exponentThenSteps;
    exponentThenSteps.append(TimingWarpTransform::exponential(2.0));
    exponentThenSteps.append(TimingWarpTransform::stepQuantize(4u));

    TimingWarpStack stepsThenExponent;
    stepsThenExponent.append(TimingWarpTransform::stepQuantize(4u));
    stepsThenExponent.append(TimingWarpTransform::exponential(2.0));

    checkNear(exponentThenSteps.map(0.8), 0.5, 1.0e-15,
        "each serial transform must receive the prior transform's output");
    checkNear(stepsThenExponent.map(0.8), 0.5625, 1.0e-15,
        "serial ordering must be retained rather than canonicalized");
    check(exponentThenSteps.map(0.8) != stepsThenExponent.map(0.8),
        "a transform stack must preserve musically meaningful ordering");
}

void testValidationAndInspection()
{
    TimingWarpStack stack;
    TimingWarpOptions malformed;
    malformed.phaseBegin = std::numeric_limits<double>::quiet_NaN();
    malformed.phaseEnd = 2.0;
    malformed.repetitions = 0u;
    malformed.alpha = std::numeric_limits<double>::infinity();
    const auto result = stack.append(TimingWarpTransform::exponential(
        -std::numeric_limits<double>::infinity(), malformed));
    check(result.added(),
        "repairable non-finite and out-of-range values must be accepted");
    check(hasCorrection(result.corrections,
              TimingWarpCorrection::NonFiniteSegment),
        "non-finite segment repair must be reported");
    check(hasCorrection(result.corrections,
              TimingWarpCorrection::ClampedSegment),
        "out-of-range segment repair must be reported");
    check(hasCorrection(result.corrections, TimingWarpCorrection::Exponent),
        "non-finite exponent repair must be reported");
    check(hasCorrection(result.corrections,
              TimingWarpCorrection::Repetitions),
        "zero repetitions repair must be reported");
    check(hasCorrection(result.corrections, TimingWarpCorrection::Alpha),
        "non-finite alpha repair must be reported");

    const auto* normalized = stack.transform(0u);
    check(normalized != nullptr,
        "an accepted transform must be available for inspection");
    if (normalized) {
        checkNear(normalized->options.phaseBegin, 0.0, 0.0,
            "a non-finite segment beginning must default to zero");
        checkNear(normalized->options.phaseEnd, 1.0, 0.0,
            "a segment ending above one must clamp to one");
        check(normalized->options.repetitions == 1u,
            "zero repetitions must normalize to one");
        checkNear(normalized->options.alpha, 1.0, 0.0,
            "non-finite alpha must default to one");
        checkNear(normalized->exponent, 1.0, 0.0,
            "non-finite exponent must default to one");
    }
    check(stack.transform(1u) == nullptr,
        "out-of-range transform inspection must return null");

    stack.clear();
    TimingWarpOptions reversed;
    reversed.phaseBegin = 0.8;
    reversed.phaseEnd = 0.2;
    const auto reversedResult = stack.append(
        TimingWarpTransform::exponential(2.0, reversed));
    check(reversedResult.added()
            && hasCorrection(reversedResult.corrections,
                TimingWarpCorrection::ReversedSegment),
        "reversed segment endpoints must be repaired and reported");
    normalized = stack.transform(0u);
    if (normalized) {
        checkNear(normalized->options.phaseBegin, 0.2, 0.0,
            "reversed segment repair must put the lower endpoint first");
        checkNear(normalized->options.phaseEnd, 0.8, 0.0,
            "reversed segment repair must put the upper endpoint second");
    }

    stack.clear();
    TimingWarpOptions degenerate;
    degenerate.phaseBegin = 0.4;
    degenerate.phaseEnd = 0.4;
    const auto degenerateResult = stack.append(
        TimingWarpTransform::exponential(2.0, degenerate));
    check(degenerateResult.status
            == TimingWarpAppendStatus::DegenerateSegment
            && stack.empty(),
        "a zero-width phase segment must be rejected without mutation");

    auto unsupported = TimingWarpTransform::exponential(2.0);
    unsupported.kind = static_cast<TimingWarpKind>(255u);
    const auto unsupportedResult = stack.append(unsupported);
    check(unsupportedResult.status == TimingWarpAppendStatus::UnsupportedKind
            && stack.empty(),
        "an unknown transform kind must be rejected without mutation");

    auto zeroStep = stack.append(TimingWarpTransform::stepQuantize(0u));
    check(zeroStep.added()
            && hasCorrection(zeroStep.corrections,
                TimingWarpCorrection::Steps),
        "zero step count must normalize to one and be reported");
    normalized = stack.transform(0u);
    check(normalized && normalized->steps == 1u
            && normalized->pulses == 1u,
        "a repaired step quantizer must retain valid compiled counts");

    stack.clear();
    auto zeroEuclidean = stack.append(
        TimingWarpTransform::euclideanQuantize(0u, 0u));
    check(zeroEuclidean.added()
            && hasCorrection(zeroEuclidean.corrections,
                TimingWarpCorrection::Steps)
            && hasCorrection(zeroEuclidean.corrections,
                TimingWarpCorrection::Pulses),
        "zero Euclidean dimensions must both be repaired and reported");

    stack.clear();
    auto dense = stack.append(
        TimingWarpTransform::euclideanQuantize(99u, 4u));
    normalized = stack.transform(0u);
    check(dense.added()
            && hasCorrection(dense.corrections,
                TimingWarpCorrection::Pulses)
            && normalized && normalized->pulses == 4u,
        "pulse count must clamp to the available Euclidean steps");
}

void testCompilationAndCapacity()
{
    std::array<TimingWarpTransform,
        TimingWarpStack::kMaximumTransforms + 2u> transforms {};
    for (auto& transform : transforms)
        transform = TimingWarpTransform::exponential(1.1);

    TimingWarpStack stack;
    const auto report = stack.compile(transforms.data(), transforms.size());
    check(report.requested == transforms.size()
            && report.added == TimingWarpStack::kMaximumTransforms
            && report.rejected == 2u,
        "bulk compilation must report accepted and over-capacity inputs");
    check(report.capacityExceeded
            && stack.size() == TimingWarpStack::kMaximumTransforms,
        "bulk compilation must retain its fixed-capacity prefix");

    const auto nullReport = stack.compile(nullptr, 3u);
    check(nullReport.sourceWasNull && nullReport.requested == 3u
            && nullReport.rejected == 3u && stack.empty(),
        "a null bulk source must be reported and produce an empty stack");

    std::array<TimingWarpTransform, 2u> mixed {
        TimingWarpTransform::exponential(2.0),
        TimingWarpTransform::exponential(2.0,
            TimingWarpOptions { 0.5, 0.5, 1u, 1.0 }),
    };
    const auto mixedReport = stack.compile(mixed.data(), mixed.size());
    check(mixedReport.added == 1u && mixedReport.rejected == 1u
            && stack.size() == 1u,
        "bulk compilation must skip invalid transforms and retain valid ones");

    const auto emptyReport = stack.compile(nullptr, 0u);
    check(!emptyReport.sourceWasNull && emptyReport.requested == 0u
            && stack.empty(),
        "a null source is valid for compiling an empty stack");
}

void testPrecomputationAndDeterminism()
{
    TimingWarpStack stack;
    stack.append(TimingWarpTransform::exponential(2.0));
    std::array<double, 5u> table {};
    check(stack.precompute(table.data(), table.size()) == table.size(),
        "precompute must report every written table value");
    const std::array<double, 5u> expected {
        0.0, 0.0625, 0.25, 0.5625, 1.0,
    };
    for (std::size_t index = 0u; index < table.size(); ++index)
        checkNear(table[index], expected[index], 1.0e-15,
            "precompute must sample the complete inclusive normalized range");

    std::array<double, 1u> single { -1.0 };
    check(stack.precompute(single.data(), single.size()) == 1u
            && single[0] == 0.0,
        "a one-value precomputed table must contain the lower endpoint");
    check(stack.precompute(nullptr, 4u) == 0u,
        "precompute must safely reject a null destination");
    check(stack.precompute(table.data(), 0u) == 0u,
        "precompute must safely accept an empty destination range");

    const double first = stack.map(0.73123456789);
    for (std::size_t iteration = 0u; iteration < 10000u; ++iteration)
        check(stack.map(0.73123456789) == first,
            "mapping the same phase and stack must be exactly deterministic");
}

void testMonotonicEndpointPreservation()
{
    TimingWarpStack stack;

    TimingWarpOptions curve;
    curve.repetitions = 3u;
    curve.alpha = 0.65;
    stack.append(TimingWarpTransform::exponential(1.37, curve));

    TimingWarpOptions euclidean;
    euclidean.phaseBegin = 0.1;
    euclidean.phaseEnd = 0.9;
    euclidean.repetitions = 2u;
    euclidean.alpha = 0.25;
    stack.append(TimingWarpTransform::euclideanQuantize(
        5u, 13u, euclidean));

    TimingWarpOptions steps;
    steps.phaseBegin = 0.2;
    steps.phaseEnd = 0.8;
    steps.repetitions = 5u;
    steps.alpha = 0.18;
    stack.append(TimingWarpTransform::stepQuantize(7u, steps));

    checkNear(stack.map(0.0), 0.0, 0.0,
        "a complex serial stack must preserve the lower endpoint");
    checkNear(stack.map(1.0), 1.0, 0.0,
        "a complex serial stack must preserve the upper endpoint");

    double previous = stack.map(0.0);
    constexpr std::size_t samples = 200000u;
    for (std::size_t index = 1u; index <= samples; ++index) {
        const double phase = static_cast<double>(index)
            / static_cast<double>(samples);
        const double value = stack.map(phase);
        check(value >= 0.0 && value <= 1.0,
            "a compiled stack must never leave normalized phase");
        check(value + 1.0e-14 >= previous,
            "serial transforms must remain nondecreasing and not reorder events");
        previous = value;
    }
}

} // namespace

int main()
{
    testIdentityAndInputNormalization();
    testExponentialSegmentAndAlpha();
    testRepetitions();
    testStepQuantization();
    testEuclideanQuantization();
    testSerialCompositionAndOrdering();
    testValidationAndInspection();
    testCompilationAndCapacity();
    testPrecomputationAndDeterminism();
    testMonotonicEndpointPreservation();

    if (failures == 0) {
        std::cout << "timing warp tests passed\n";
        return EXIT_SUCCESS;
    }
    std::cerr << failures << " timing warp test(s) failed\n";
    return EXIT_FAILURE;
}
