#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace s3g::tracker {

// Every transform maps a normalized phase to another normalized phase. The
// implementations are nondecreasing and preserve 0 and 1, so a serial stack
// cannot reorder events or move an event outside its cycle.
enum class TimingWarpKind : uint8_t {
    Exponential,
    StepQuantize,
    EuclideanQuantize,
};

struct TimingWarpOptions {
    // The transform is applied only inside this normalized, inclusive phase
    // range. Values outside it pass through unchanged. Reversed and
    // out-of-range endpoints are normalized when the stack is compiled.
    double phaseBegin = 0.0;
    double phaseEnd = 1.0;

    // Apply the transform independently this many times inside the selected
    // phase segment. For example, repetitions=4 applies a curve to each
    // quarter of the segment while preserving every quarter boundary.
    uint32_t repetitions = 1u;

    // Linear interpolation between the incoming phase (0) and the transformed
    // phase (1). This is normalized to [0, 1] during compilation.
    double alpha = 1.0;
};

struct TimingWarpTransform {
    TimingWarpKind kind = TimingWarpKind::Exponential;
    TimingWarpOptions options {};

    // Used by Exponential. Values are normalized to the supported positive
    // range when the transform is appended to a stack.
    double exponent = 1.0;

    // StepQuantize uses steps. EuclideanQuantize maps pulses evenly onto the
    // step grid. Both are normalized to supported nonzero ranges.
    uint32_t pulses = 1u;
    uint32_t steps = 1u;

    static TimingWarpTransform exponential(double exponent,
        TimingWarpOptions options = {}) noexcept;
    static TimingWarpTransform stepQuantize(uint32_t steps,
        TimingWarpOptions options = {}) noexcept;
    static TimingWarpTransform euclideanQuantize(uint32_t pulses,
        uint32_t steps, TimingWarpOptions options = {}) noexcept;
};

enum class TimingWarpCorrection : uint32_t {
    None = 0u,
    NonFiniteSegment = 1u << 0u,
    ClampedSegment = 1u << 1u,
    ReversedSegment = 1u << 2u,
    Exponent = 1u << 3u,
    Steps = 1u << 4u,
    Pulses = 1u << 5u,
    Repetitions = 1u << 6u,
    Alpha = 1u << 7u,
};

constexpr TimingWarpCorrection operator|(TimingWarpCorrection left,
    TimingWarpCorrection right) noexcept
{
    return static_cast<TimingWarpCorrection>(
        static_cast<uint32_t>(left) | static_cast<uint32_t>(right));
}

constexpr TimingWarpCorrection& operator|=(TimingWarpCorrection& left,
    TimingWarpCorrection right) noexcept
{
    left = left | right;
    return left;
}

constexpr bool hasCorrection(TimingWarpCorrection value,
    TimingWarpCorrection correction) noexcept
{
    return (static_cast<uint32_t>(value)
        & static_cast<uint32_t>(correction)) != 0u;
}

enum class TimingWarpAppendStatus : uint8_t {
    Added,
    CapacityExceeded,
    DegenerateSegment,
    UnsupportedKind,
};

struct TimingWarpAppendResult {
    TimingWarpAppendStatus status = TimingWarpAppendStatus::Added;
    TimingWarpCorrection corrections = TimingWarpCorrection::None;

    constexpr bool added() const noexcept
    {
        return status == TimingWarpAppendStatus::Added;
    }
};

struct TimingWarpCompileReport {
    std::size_t requested = 0u;
    std::size_t added = 0u;
    std::size_t rejected = 0u;
    TimingWarpCorrection corrections = TimingWarpCorrection::None;
    bool sourceWasNull = false;
    bool capacityExceeded = false;
};

// A fixed-capacity, compiled warp stack. append() and compile() perform all
// validation and normalization. Once configured, map() is deterministic,
// thread-safe for concurrent readers, noexcept, and performs no allocation.
// Configure a replacement stack on the control thread, then publish/copy it at
// a synchronization boundary rather than mutating a stack in use by audio.
class TimingWarpStack {
public:
    static constexpr std::size_t kMaximumTransforms = 32u;
    static constexpr uint32_t kMaximumSteps = 65536u;
    static constexpr uint32_t kMaximumRepetitions = 1024u;
    static constexpr double kMinimumExponent = 1.0 / 64.0;
    static constexpr double kMaximumExponent = 64.0;

    TimingWarpAppendResult append(TimingWarpTransform transform) noexcept;

    // Replaces the current contents. A null source with a nonzero count is
    // reported and produces an empty stack. Inputs beyond fixed capacity are
    // rejected rather than silently replacing earlier transforms.
    TimingWarpCompileReport compile(const TimingWarpTransform* transforms,
        std::size_t count) noexcept;

    void clear() noexcept { size_ = 0u; }
    std::size_t size() const noexcept { return size_; }
    bool empty() const noexcept { return size_ == 0u; }
    std::size_t capacity() const noexcept { return transforms_.size(); }

    // Returns the normalized descriptor retained by the compiled stack, or
    // nullptr for an out-of-range index.
    const TimingWarpTransform* transform(std::size_t index) const noexcept;

    // Input is clamped to [0, 1]. NaN maps to 0; infinities clamp to the
    // corresponding endpoint.
    double map(double normalizedPhase) const noexcept;

    // Fills an evenly spaced 0-to-1 lookup table without allocating and
    // returns the number of values written. A one-value table contains map(0).
    std::size_t precompute(double* output, std::size_t valueCount) const
        noexcept;

private:
    std::array<TimingWarpTransform, kMaximumTransforms> transforms_ {};
    std::size_t size_ = 0u;
};

// Project-owned library of composed timing-warp stacks. Slots are addressed
// one-based in the tracker UI/live language and zero-based in the engine.
// The fixed slot count makes WRP recall deterministic and allocation-free on
// the render thread; names are authoring metadata and are never touched by
// realtime recall.
constexpr std::size_t kMaximumTimingWarpLibraryEntries = 64u;
constexpr std::size_t kMaximumTimingWarpLibraryNameBytes = 64u;
constexpr uint32_t kMaximumLiveWarpCycleTicks = 16u;

struct TimingWarpLibraryEntry {
    std::string name;
    uint32_t cycleTicks = 16u;
    TimingWarpStack stack;
    bool occupied = false;
};

class TimingWarpLibrary {
public:
    bool store(std::size_t index, std::string name, uint32_t cycleTicks,
        const TimingWarpStack& stack);
    bool erase(std::size_t index) noexcept;
    void clear() noexcept;

    const TimingWarpLibraryEntry* entry(std::size_t index) const noexcept;
    TimingWarpLibraryEntry* entry(std::size_t index) noexcept;
    std::size_t size() const noexcept;

private:
    std::array<TimingWarpLibraryEntry,
        kMaximumTimingWarpLibraryEntries> entries_ {};
};

// SEQ value columns retain their normalized storage contract. WRP presents
// that value as a one-based library number while these helpers provide the
// exact reversible mapping used by the UI, console, and scheduler.
std::size_t timingWarpLibraryIndexFromNormalized(float normalized) noexcept;
float timingWarpLibraryNormalizedFromIndex(std::size_t index) noexcept;

} // namespace s3g::tracker
