#include "s3g_ambisonic_speaker_decoder.h"
#include "s3g_realtime.h"

#include <clap/clap.h>
#include <clap/ext/gui.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>

#if defined(__APPLE__)
#import <Cocoa/Cocoa.h>
#include "../common/s3g_clap_macos.h"
#include "../common/s3g_cocoa_gui.h"
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <new>
#include <thread>
#include <utility>

namespace {

constexpr uint32_t kInputChannels = s3g::kAmbiSpeakerDecoderMaxChannels;
constexpr uint32_t kOutputChannels = s3g::kAmbiSpeakerDecoderMaxSpeakers;
constexpr uint32_t kStateVersion = 5;
constexpr uint32_t kMaxLayoutParamValue = static_cast<uint32_t>(s3g::AmbiSpeakerLayoutPreset::Srst25);
constexpr uint32_t kLayoutMenuCount = 14;

constexpr clap_id kLayoutParamId = 1;
constexpr clap_id kModeParamId = 2;
constexpr clap_id kOrderParamId = 3;
constexpr clap_id kActiveSpeakersParamId = 4;
constexpr clap_id kSelectedSpeakerParamId = 5;
constexpr clap_id kAzimuthParamId = 6;
constexpr clap_id kElevationParamId = 7;
constexpr clap_id kDistanceParamId = 8;
constexpr clap_id kSpeakerGainParamId = 9;
constexpr clap_id kSpeakerEnabledParamId = 10;
constexpr clap_id kRegularizationParamId = 11;
constexpr clap_id kWidthParamId = 12;
constexpr clap_id kEnergyParamId = 13;
constexpr clap_id kOutputParamId = 14;
constexpr clap_id kWeightingParamId = 15;
constexpr clap_id kCustomFieldParamId = 16;
constexpr NSInteger kMixerGainFieldTagBase = 1000;

struct SavedState {
    uint32_t version = kStateVersion;
    s3g::AmbiSpeakerDecoderParams params {};
    std::array<s3g::AmbiSpeaker, s3g::kAmbiSpeakerDecoderMaxSpeakers> speakers {};
    int32_t guiViewMode = 2;
    double guiViewAzDeg = 35.0;
    double guiViewElDeg = 34.0;
    double guiViewZoom = 1.0;
};

struct SavedStateV4 {
    uint32_t version = 4;
    s3g::AmbiSpeakerDecoderParams params {};
    std::array<s3g::AmbiSpeaker, s3g::kAmbiSpeakerDecoderMaxSpeakers> speakers {};
};

enum class DecoderCommandKind : uint32_t {
    SetParam = 0,
    SetAzimuth,
    SetElevation,
    SetDistance,
    SetGain,
    SetEnabled,
    SetSolo,
    SetWidth,
    SetOutput,
};

struct DecoderCommand {
    DecoderCommandKind kind = DecoderCommandKind::SetParam;
    clap_id paramId = CLAP_INVALID_ID;
    uint32_t speakerIndex = 0;
    double value = 0.0;
    uint64_t epoch = 0;
    uint32_t runtimeSerial = 0;
    bool deferRuntimeCommit = false;
    bool speakerFollowsSelection = false;
    uint64_t queueBoundary = 0;
};

template <typename Value, size_t Capacity>
class BoundedMpmcQueue {
    static_assert(Capacity >= 2u && (Capacity & (Capacity - 1u)) == 0u,
        "MPMC queue capacity must be a power of two");

    struct Cell {
        std::atomic<size_t> sequence { 0u };
        Value value {};
    };

public:
    BoundedMpmcQueue()
    {
        for (size_t i = 0; i < Capacity; ++i) {
            cells_[i].sequence.store(i, std::memory_order_relaxed);
        }
    }

    bool tryPush(const Value& value, uint64_t* ticket = nullptr)
    {
        size_t position = enqueuePosition_.load(std::memory_order_relaxed);
        Cell* cell = nullptr;
        for (uint32_t attempt = 0; attempt < 32u; ++attempt) {
            cell = &cells_[position & (Capacity - 1u)];
            const size_t sequence = cell->sequence.load(std::memory_order_acquire);
            const intptr_t difference = static_cast<intptr_t>(sequence)
                - static_cast<intptr_t>(position);
            if (difference == 0) {
                if (enqueuePosition_.compare_exchange_weak(position, position + 1u,
                        std::memory_order_relaxed, std::memory_order_relaxed)) {
                    cell->value = value;
                    cell->sequence.store(position + 1u, std::memory_order_release);
                    if (ticket) *ticket = static_cast<uint64_t>(position + 1u);
                    return true;
                }
            } else if (difference < 0) {
                return false;
            } else {
                position = enqueuePosition_.load(std::memory_order_relaxed);
            }
        }
        return false;
    }

    bool tryPop(Value& value, uint64_t* ticket = nullptr)
    {
        size_t position = dequeuePosition_.load(std::memory_order_relaxed);
        Cell* cell = nullptr;
        for (uint32_t attempt = 0; attempt < 32u; ++attempt) {
            cell = &cells_[position & (Capacity - 1u)];
            const size_t sequence = cell->sequence.load(std::memory_order_acquire);
            const intptr_t difference = static_cast<intptr_t>(sequence)
                - static_cast<intptr_t>(position + 1u);
            if (difference == 0) {
                if (dequeuePosition_.compare_exchange_weak(position, position + 1u,
                        std::memory_order_relaxed, std::memory_order_relaxed)) {
                    value = cell->value;
                    cell->sequence.store(position + Capacity, std::memory_order_release);
                    if (ticket) *ticket = static_cast<uint64_t>(position + 1u);
                    return true;
                }
            } else if (difference < 0) {
                return false;
            } else {
                position = dequeuePosition_.load(std::memory_order_relaxed);
            }
        }
        return false;
    }

    uint64_t producerPosition() const
    {
        return static_cast<uint64_t>(
            enqueuePosition_.load(std::memory_order_acquire));
    }

private:
    std::array<Cell, Capacity> cells_ {};
    alignas(64) std::atomic<size_t> enqueuePosition_ { 0u };
    alignas(64) std::atomic<size_t> dequeuePosition_ { 0u };
};

constexpr uint32_t kSnapshotWriterBit = 0x80000000u;
constexpr uint32_t kSnapshotReaderMask = ~kSnapshotWriterBit;
constexpr uint32_t kDecoderSnapshotCount = 3u;
constexpr uint32_t kDecoderCommandQueueCapacity = 4096u;
constexpr uint32_t kDecoderOverflowQueueCapacity = 4096u;
constexpr uint32_t kDecoderWorkerBatchLimit = 1024u;

template <typename Value>
class OrderedRuntimeValue {
public:
    struct Snapshot {
        Value value {};
        uint32_t serial = 0u;
        uint64_t packed = 0u;
    };

    OrderedRuntimeValue() = default;

    void initialize(Value value, uint32_t serial = 1u)
    {
        packed_.store(pack(value, serial), std::memory_order_relaxed);
    }

    Snapshot snapshot(
        std::memory_order order = std::memory_order_acquire) const
    {
        const uint64_t packed = packed_.load(order);
        return {
            decode(static_cast<uint32_t>(packed)),
            static_cast<uint32_t>(packed >> 32u),
            packed,
        };
    }

    Value load(std::memory_order order = std::memory_order_acquire) const
    {
        return snapshot(order).value;
    }

    bool storeIfNewer(Value value, uint32_t serial)
    {
        uint64_t expected = packed_.load(std::memory_order_acquire);
        const uint64_t desired = pack(value, serial);
        for (uint32_t attempt = 0u; attempt < 32u; ++attempt) {
            const uint32_t currentSerial =
                static_cast<uint32_t>(expected >> 32u);
            if (!serialAtLeast(serial, currentSerial)) return false;
            if (packed_.compare_exchange_weak(
                    expected, desired,
                    std::memory_order_release,
                    std::memory_order_acquire)) {
                return true;
            }
        }
        return false;
    }

private:
    static bool serialAtLeast(uint32_t candidate, uint32_t current)
    {
        return static_cast<int32_t>(candidate - current) >= 0;
    }

    static uint32_t encode(Value value)
    {
        uint32_t bits = 0u;
        static_assert(sizeof(Value) <= sizeof(bits),
            "runtime value must fit in 32 bits");
        std::memcpy(&bits, &value, sizeof(Value));
        return bits;
    }

    static Value decode(uint32_t bits)
    {
        Value value {};
        std::memcpy(&value, &bits, sizeof(Value));
        return value;
    }

    static uint64_t pack(Value value, uint32_t serial)
    {
        return (static_cast<uint64_t>(serial) << 32u)
            | static_cast<uint64_t>(encode(value));
    }

    std::atomic<uint64_t> packed_ { 0u };
};

static_assert(std::atomic<uint64_t>::is_always_lock_free,
    "speaker-decoder runtime overrides require lock-free 64-bit atomics");

struct RuntimeMixerState {
    uint32_t serial = 1u;
    float width = 1.0f;
    float outputGainDb = 0.0f;
    std::array<float, s3g::kAmbiSpeakerDecoderMaxSpeakers> gain {};
    std::array<bool, s3g::kAmbiSpeakerDecoderMaxSpeakers> enabled {};
    std::array<bool, s3g::kAmbiSpeakerDecoderMaxSpeakers> solo {};

    RuntimeMixerState()
    {
        gain.fill(1.0f);
        enabled.fill(true);
        solo.fill(false);
    }
};

struct DecoderSnapshotSlot {
    alignas(64) std::atomic<uint32_t> state { 0u };
    s3g::AmbiSpeakerDecoder decoder;
    RuntimeMixerState runtimeMixer;
};

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    double sampleRate = 48000.0;
    uint32_t maxFrames = 0;
    s3g::AmbiSpeakerDecoder modelDecoder;
    RuntimeMixerState modelRuntimeMixer;
    std::array<DecoderSnapshotSlot, kDecoderSnapshotCount> decoderSnapshots {};
    std::atomic<uint32_t> publishedDecoder { 0u };
    BoundedMpmcQueue<DecoderCommand, kDecoderCommandQueueCapacity> decoderCommands;
    BoundedMpmcQueue<DecoderCommand, kDecoderOverflowQueueCapacity>
        decoderOverflowCommands;
    // Even values are stable command generations. State load holds the
    // intervening odd value while atomically replacing model/runtime state.
    std::atomic<uint64_t> commandEpoch { 2u };
    std::atomic<uint64_t> completedQueuePosition { 0u };
    std::atomic<uint64_t> completedOverflowPosition { 0u };
    uint64_t workerAppliedQueuePosition = 0u;
    uint64_t workerAppliedOverflowPosition = 0u;
    bool workerHasPendingOverflow = false;
    DecoderCommand workerPendingOverflow {};
    uint64_t workerPendingOverflowPosition = 0u;
    bool workerHasPendingPrimary = false;
    DecoderCommand workerPendingPrimary {};
    uint64_t workerPendingPrimaryPosition = 0u;
    std::atomic<bool> stopDecoderWorker { false };
    std::thread decoderWorker;
    std::mutex modelMutex;
    std::mutex completionMutex;
    std::condition_variable completionCondition;
    bool decoderPublishPending = false;
    std::atomic<uint32_t> nextRuntimeSerial { 1u };
    OrderedRuntimeValue<uint32_t> realtimeLayout;
    OrderedRuntimeValue<uint32_t> realtimeCustomField;
    OrderedRuntimeValue<uint32_t> realtimeActiveSpeakers;
    OrderedRuntimeValue<uint32_t> realtimeSelectedSpeaker;
    OrderedRuntimeValue<uint32_t> realtimeSpeakerResetActive;
    OrderedRuntimeValue<float> realtimeWidth;
    OrderedRuntimeValue<float> realtimeOutputGainDb;
    std::array<OrderedRuntimeValue<float>,
        s3g::kAmbiSpeakerDecoderMaxSpeakers> realtimeSpeakerGain {};
    std::array<OrderedRuntimeValue<bool>,
        s3g::kAmbiSpeakerDecoderMaxSpeakers> realtimeSpeakerEnabled {};
    std::array<OrderedRuntimeValue<bool>,
        s3g::kAmbiSpeakerDecoderMaxSpeakers> realtimeSpeakerSolo {};
    uint32_t audioDecoderSnapshot = 0u;
    bool audioDecoderSnapshotHeld = false;
    std::atomic<float> outputPeak { 0.0f };
#if defined(__APPLE__)
    void* guiView = nullptr;
    s3g::clap_gui::ResponsiveViewport guiViewport {};
    bool guiVisible = false;
    int guiViewMode = 2;
    double guiViewAzDeg = 35.0;
    double guiViewElDeg = 34.0;
    double guiViewZoom = 1.0;
#endif

    Plugin()
    {
        realtimeLayout.initialize(
            static_cast<uint32_t>(s3g::AmbiSpeakerLayoutPreset::Sphere24));
        realtimeCustomField.initialize(
            static_cast<uint32_t>(s3g::AmbiSpeakerCustomField::FullSphere));
        realtimeActiveSpeakers.initialize(24u);
        realtimeSelectedSpeaker.initialize(0u);
        realtimeSpeakerResetActive.initialize(24u, 0u);
        realtimeWidth.initialize(1.0f);
        realtimeOutputGainDb.initialize(0.0f);
        for (uint32_t speaker = 0; speaker < s3g::kAmbiSpeakerDecoderMaxSpeakers; ++speaker) {
            realtimeSpeakerGain[speaker].initialize(1.0f);
            realtimeSpeakerEnabled[speaker].initialize(true);
            realtimeSpeakerSolo[speaker].initialize(false);
        }
    }
};

Plugin* self(const clap_plugin_t* plugin) { return static_cast<Plugin*>(plugin->plugin_data); }

uint32_t allocateRuntimeSerial(Plugin& plugin)
{
    return plugin.nextRuntimeSerial.fetch_add(
        1u, std::memory_order_acq_rel) + 1u;
}

bool runtimeSerialAtLeast(uint32_t candidate, uint32_t current)
{
    return static_cast<int32_t>(candidate - current) >= 0;
}

bool runtimeSerialNewer(uint32_t candidate, uint32_t current)
{
    return candidate != current && runtimeSerialAtLeast(candidate, current);
}

template <typename Value>
Value resolveRuntimeOverride(
    Value baseValue,
    uint32_t baseSerial,
    const OrderedRuntimeValue<Value>& overrideValue)
{
    const auto override =
        overrideValue.snapshot(std::memory_order_acquire);
    return runtimeSerialAtLeast(override.serial, baseSerial)
        ? override.value
        : baseValue;
}

RuntimeMixerState resolveRuntimeMixerState(
    const Plugin& plugin, const RuntimeMixerState& base)
{
    RuntimeMixerState resolved = base;
    resolved.width = resolveRuntimeOverride(
        base.width, base.serial, plugin.realtimeWidth);
    resolved.outputGainDb = resolveRuntimeOverride(
        base.outputGainDb, base.serial, plugin.realtimeOutputGainDb);
    const auto reset =
        plugin.realtimeSpeakerResetActive.snapshot(std::memory_order_acquire);
    const uint32_t resetActive = std::clamp<uint32_t>(
        reset.value, 2u, s3g::kAmbiSpeakerDecoderMaxSpeakers);
    for (uint32_t speaker = 0u;
         speaker < s3g::kAmbiSpeakerDecoderMaxSpeakers;
         ++speaker) {
        uint32_t gainSerial = base.serial;
        float gain = base.gain[speaker];
        if (runtimeSerialNewer(reset.serial, gainSerial)) {
            gainSerial = reset.serial;
            gain = 1.0f;
        }
        const auto gainOverride =
            plugin.realtimeSpeakerGain[speaker].snapshot(
                std::memory_order_acquire);
        if (runtimeSerialAtLeast(gainOverride.serial, gainSerial)) {
            gain = gainOverride.value;
        }
        resolved.gain[speaker] = gain;

        uint32_t enabledSerial = base.serial;
        bool enabled = base.enabled[speaker];
        if (runtimeSerialNewer(reset.serial, enabledSerial)) {
            enabledSerial = reset.serial;
            enabled = speaker < resetActive;
        }
        const auto enabledOverride =
            plugin.realtimeSpeakerEnabled[speaker].snapshot(
                std::memory_order_acquire);
        if (runtimeSerialAtLeast(enabledOverride.serial, enabledSerial)) {
            enabled = enabledOverride.value;
        }
        resolved.enabled[speaker] = enabled;

        uint32_t soloSerial = base.serial;
        bool solo = base.solo[speaker];
        if (runtimeSerialNewer(reset.serial, soloSerial)) {
            soloSerial = reset.serial;
            solo = false;
        }
        const auto soloOverride =
            plugin.realtimeSpeakerSolo[speaker].snapshot(
                std::memory_order_acquire);
        if (runtimeSerialAtLeast(soloOverride.serial, soloSerial)) {
            solo = soloOverride.value;
        }
        resolved.solo[speaker] = solo;
    }
    return resolved;
}

class DecoderSnapshotGuard {
public:
    DecoderSnapshotGuard() = default;
    DecoderSnapshotGuard(DecoderSnapshotSlot* slot, uint32_t index)
        : slot_(slot)
        , index_(index)
    {
    }
    DecoderSnapshotGuard(const DecoderSnapshotGuard&) = delete;
    DecoderSnapshotGuard& operator=(const DecoderSnapshotGuard&) = delete;
    DecoderSnapshotGuard(DecoderSnapshotGuard&& other) noexcept
        : slot_(std::exchange(other.slot_, nullptr))
        , index_(other.index_)
    {
    }
    DecoderSnapshotGuard& operator=(DecoderSnapshotGuard&& other) noexcept
    {
        if (this == &other) return *this;
        release();
        slot_ = std::exchange(other.slot_, nullptr);
        index_ = other.index_;
        return *this;
    }
    ~DecoderSnapshotGuard() { release(); }

    const s3g::AmbiSpeakerDecoder& decoder() const { return slot_->decoder; }
    const RuntimeMixerState& runtimeMixer() const
    {
        return slot_->runtimeMixer;
    }
    const s3g::AmbiSpeakerDecoder* operator->() const { return &slot_->decoder; }
    uint32_t index() const { return index_; }

private:
    void release()
    {
        if (!slot_) return;
        slot_->state.fetch_sub(1u, std::memory_order_release);
        slot_ = nullptr;
    }

    DecoderSnapshotSlot* slot_ = nullptr;
    uint32_t index_ = 0u;
};

DecoderSnapshotGuard acquireDecoderSnapshot(Plugin& plugin)
{
    for (;;) {
        const uint32_t index = plugin.publishedDecoder.load(std::memory_order_acquire)
            % kDecoderSnapshotCount;
        auto& slot = plugin.decoderSnapshots[index];
        uint32_t state = slot.state.load(std::memory_order_relaxed);
        while ((state & kSnapshotWriterBit) == 0u) {
            if ((state & kSnapshotReaderMask) == kSnapshotReaderMask) break;
            if (slot.state.compare_exchange_weak(state, state + 1u,
                    std::memory_order_acquire, std::memory_order_relaxed)) {
                if (plugin.publishedDecoder.load(std::memory_order_acquire) == index) {
                    return { &slot, index };
                }
                slot.state.fetch_sub(1u, std::memory_order_release);
                break;
            }
        }
    }
}

bool tryAcquireDecoderSnapshot(
    Plugin& plugin, uint32_t& acquiredIndex, uint32_t maxAttempts)
{
    for (uint32_t attempt = 0u; attempt < maxAttempts; ++attempt) {
        const uint32_t index =
            plugin.publishedDecoder.load(std::memory_order_acquire)
            % kDecoderSnapshotCount;
        auto& slot = plugin.decoderSnapshots[index];
        uint32_t state = slot.state.load(std::memory_order_relaxed);
        if ((state & kSnapshotWriterBit) != 0u
            || (state & kSnapshotReaderMask) == kSnapshotReaderMask) {
            continue;
        }
        if (!slot.state.compare_exchange_weak(
                state, state + 1u,
                std::memory_order_acquire,
                std::memory_order_relaxed)) {
            continue;
        }
        if (plugin.publishedDecoder.load(std::memory_order_acquire) == index) {
            acquiredIndex = index;
            return true;
        }
        slot.state.fetch_sub(1u, std::memory_order_release);
    }
    return false;
}

s3g::AmbiSpeakerDecoderParams visibleDecoderParams(
    const Plugin& plugin,
    const s3g::AmbiSpeakerDecoder& decoder,
    const RuntimeMixerState& runtimeBase)
{
    auto params = decoder.params();
    const auto runtime = resolveRuntimeMixerState(plugin, runtimeBase);
    params.selectedSpeaker = std::min<uint32_t>(
        plugin.realtimeSelectedSpeaker.load(std::memory_order_relaxed),
        std::max<uint32_t>(1u, params.activeSpeakers) - 1u);
    const auto& speakers = decoder.speakers();
    params.selectedAzimuthDeg = speakers[params.selectedSpeaker].azimuthDeg;
    params.selectedElevationDeg = speakers[params.selectedSpeaker].elevationDeg;
    params.selectedDistance = speakers[params.selectedSpeaker].distance;
    params.width = runtime.width;
    params.outputGainDb = runtime.outputGainDb;
    params.selectedGain = runtime.gain[params.selectedSpeaker];
    params.selectedEnabled = runtime.enabled[params.selectedSpeaker];
    return params;
}

std::array<s3g::AmbiSpeaker, s3g::kAmbiSpeakerDecoderMaxSpeakers> visibleDecoderSpeakers(
    const Plugin& plugin,
    const s3g::AmbiSpeakerDecoder& decoder,
    const RuntimeMixerState& runtimeBase)
{
    auto speakers = decoder.speakers();
    const auto runtime = resolveRuntimeMixerState(plugin, runtimeBase);
    for (uint32_t speaker = 0; speaker < speakers.size(); ++speaker) {
        speakers[speaker].gain = runtime.gain[speaker];
        speakers[speaker].enabled = runtime.enabled[speaker];
        speakers[speaker].solo = runtime.solo[speaker];
    }
    return speakers;
}

const char* layoutName(uint32_t value)
{
    switch (value) {
    case 1: return "QUAD";
    case 2: return "CUBE 8";
    case 3: return "CUBE 17";
    case 11: return "CUBE 41";
    case 4: return "DOME 24";
    case 5: return "DOME 25";
    case 6: return "QUAD+OH";
    case 7: return "SPHERE 24";
    case 8: return "DODECA 12";
    case 9: return "ICOSAHEDRON 20";
    case 12: return "LPAC 41";
    case 10: return "OCTO RING";
    case 13: return "SRST 25";
    default: return "CUSTOM";
    }
}

uint32_t layoutPresetForMenuIndex(uint32_t index)
{
    static constexpr uint32_t values[] = {
        0u, // CUSTOM
        2u, // CUBE 8
        3u, // CUBE 17
        11u, // CUBE 41
        8u, // DODECA 12
        4u, // DOME 24
        5u, // DOME 25
        9u, // ICOSAHEDRON 20
        12u, // LPAC 41
        10u, // OCTO RING
        1u, // QUAD
        6u, // QUAD+OH
        13u, // SRST 25
        7u, // SPHERE 24
    };
    return values[std::min<uint32_t>(index, static_cast<uint32_t>(std::size(values) - 1u))];
}

const char* modeName(uint32_t value)
{
    switch (value) {
    case 1: return "EPAD";
    case 2: return "MMD";
    case 3: return "ALLRAD";
    default: return "BASIC";
    }
}

const char* weightingName(uint32_t value)
{
    switch (value) {
    case 1: return "MAXRE";
    case 2: return "INPHASE";
    default: return "NONE";
    }
}

const char* customFieldName(uint32_t value)
{
    return value == 1 ? "HEMI" : "SPHERE";
}

bool publishModelDecoder(Plugin& plugin)
{
    const uint32_t current = plugin.publishedDecoder.load(std::memory_order_acquire)
        % kDecoderSnapshotCount;
    for (uint32_t offset = 1u; offset < kDecoderSnapshotCount; ++offset) {
        const uint32_t index = (current + offset) % kDecoderSnapshotCount;
        auto& slot = plugin.decoderSnapshots[index];
        uint32_t expected = 0u;
        if (!slot.state.compare_exchange_strong(expected, kSnapshotWriterBit,
                std::memory_order_acquire, std::memory_order_relaxed)) {
            continue;
        }
        slot.decoder = plugin.modelDecoder;
        slot.runtimeMixer = plugin.modelRuntimeMixer;
        slot.state.store(0u, std::memory_order_release);
        plugin.publishedDecoder.store(index, std::memory_order_release);
        plugin.decoderPublishPending = false;
        return true;
    }
    return false;
}

void publishModelDecoderBlocking(Plugin& plugin)
{
    while (!publishModelDecoder(plugin)) {
        std::this_thread::yield();
    }
}

void storeRuntimeFromDecoder(Plugin& plugin, const s3g::AmbiSpeakerDecoder& decoder)
{
    const auto params = decoder.params();
    const auto speakers = decoder.speakers();
    plugin.modelRuntimeMixer.serial = 1u;
    plugin.modelRuntimeMixer.width = params.width;
    plugin.modelRuntimeMixer.outputGainDb = params.outputGainDb;
    plugin.realtimeLayout.initialize(
        static_cast<uint32_t>(params.layout));
    plugin.realtimeCustomField.initialize(
        static_cast<uint32_t>(params.customField));
    plugin.realtimeActiveSpeakers.initialize(params.activeSpeakers);
    plugin.realtimeSelectedSpeaker.initialize(params.selectedSpeaker);
    plugin.realtimeSpeakerResetActive.initialize(params.activeSpeakers, 0u);
    plugin.realtimeWidth.initialize(params.width);
    plugin.realtimeOutputGainDb.initialize(params.outputGainDb);
    for (uint32_t speaker = 0; speaker < speakers.size(); ++speaker) {
        plugin.modelRuntimeMixer.gain[speaker] = speakers[speaker].gain;
        plugin.modelRuntimeMixer.enabled[speaker] = speakers[speaker].enabled;
        plugin.modelRuntimeMixer.solo[speaker] = speakers[speaker].solo;
        plugin.realtimeSpeakerGain[speaker].initialize(speakers[speaker].gain);
        plugin.realtimeSpeakerEnabled[speaker].initialize(
            speakers[speaker].enabled);
        plugin.realtimeSpeakerSolo[speaker].initialize(speakers[speaker].solo);
    }
}

void normalizeModelRuntimeControls(Plugin& plugin)
{
    auto params = plugin.modelDecoder.params();
    auto speakers = plugin.modelDecoder.speakers();
    params.width = 1.0f;
    params.outputGainDb = 0.0f;
    params.selectedGain = 1.0f;
    for (auto& speaker : speakers) {
        speaker.gain = 1.0f;
        speaker.solo = false;
    }
    plugin.modelDecoder.beginBatchUpdate();
    plugin.modelDecoder.setParams(params);
    plugin.modelDecoder.setSpeakers(speakers);
    plugin.modelDecoder.endBatchUpdate();
}

uint32_t activeSpeakersForLayout(uint32_t layout, uint32_t customCount)
{
    switch (static_cast<s3g::AmbiSpeakerLayoutPreset>(
        std::min<uint32_t>(layout, kMaxLayoutParamValue))) {
    case s3g::AmbiSpeakerLayoutPreset::Quad: return 4u;
    case s3g::AmbiSpeakerLayoutPreset::Cube8: return 8u;
    case s3g::AmbiSpeakerLayoutPreset::Cube17: return 17u;
    case s3g::AmbiSpeakerLayoutPreset::Dome24: return 24u;
    case s3g::AmbiSpeakerLayoutPreset::Dome25: return 25u;
    case s3g::AmbiSpeakerLayoutPreset::QuadOverhead6: return 6u;
    case s3g::AmbiSpeakerLayoutPreset::Sphere24: return 24u;
    case s3g::AmbiSpeakerLayoutPreset::Dodeca12: return 12u;
    case s3g::AmbiSpeakerLayoutPreset::Icosahedron20: return 20u;
    case s3g::AmbiSpeakerLayoutPreset::OctophonicRing: return 8u;
    case s3g::AmbiSpeakerLayoutPreset::Cube41: return 41u;
    case s3g::AmbiSpeakerLayoutPreset::Lpac41: return 41u;
    case s3g::AmbiSpeakerLayoutPreset::Srst25: return 25u;
    case s3g::AmbiSpeakerLayoutPreset::Custom:
    default:
        return std::clamp<uint32_t>(
            customCount, 2u, s3g::kAmbiSpeakerDecoderMaxSpeakers);
    }
}

void resetRuntimeSpeakerControls(
    Plugin& plugin, uint32_t activeSpeakers, uint32_t serial)
{
    activeSpeakers = std::clamp<uint32_t>(
        activeSpeakers, 2u, s3g::kAmbiSpeakerDecoderMaxSpeakers);
    plugin.realtimeActiveSpeakers.storeIfNewer(activeSpeakers, serial);
    const uint32_t selected = std::min<uint32_t>(
        plugin.realtimeSelectedSpeaker.load(std::memory_order_relaxed),
        activeSpeakers - 1u);
    plugin.realtimeSelectedSpeaker.storeIfNewer(selected, serial);
    // A layout reset is one atomic generation change. Older per-speaker
    // overrides become invisible without a channel-by-channel write window.
    plugin.realtimeSpeakerResetActive.storeIfNewer(activeSpeakers, serial);
}

void applyDesiredLayoutReset(
    Plugin& plugin, uint32_t layout, uint32_t serial)
{
    layout = std::min<uint32_t>(layout, kMaxLayoutParamValue);
    const uint32_t previous =
        plugin.realtimeLayout.load(std::memory_order_acquire);
    if (!plugin.realtimeLayout.storeIfNewer(layout, serial)) return;
    if (layout == previous) return;
    resetRuntimeSpeakerControls(
        plugin,
        activeSpeakersForLayout(
            layout,
            plugin.realtimeActiveSpeakers.load(std::memory_order_relaxed)),
        serial);
}

void applyDesiredActiveReset(
    Plugin& plugin, uint32_t activeSpeakers, uint32_t serial)
{
    activeSpeakers = std::clamp<uint32_t>(
        activeSpeakers, 2u, s3g::kAmbiSpeakerDecoderMaxSpeakers);
    const uint32_t previousLayout =
        plugin.realtimeLayout.load(std::memory_order_acquire);
    const uint32_t previousActive =
        plugin.realtimeActiveSpeakers.load(std::memory_order_relaxed);
    if (!plugin.realtimeLayout.storeIfNewer(
            static_cast<uint32_t>(
                s3g::AmbiSpeakerLayoutPreset::Custom),
            serial)) {
        return;
    }
    if (previousLayout
            == static_cast<uint32_t>(s3g::AmbiSpeakerLayoutPreset::Custom)
        && previousActive == activeSpeakers) {
        return;
    }
    resetRuntimeSpeakerControls(plugin, activeSpeakers, serial);
}

void applyDesiredCustomFieldReset(
    Plugin& plugin, uint32_t customField, uint32_t serial)
{
    customField = std::min<uint32_t>(customField, 1u);
    const uint32_t previousField =
        plugin.realtimeCustomField.load(std::memory_order_acquire);
    const uint32_t previousLayout =
        plugin.realtimeLayout.load(std::memory_order_acquire);
    if (!plugin.realtimeCustomField.storeIfNewer(customField, serial)
        || !plugin.realtimeLayout.storeIfNewer(
            static_cast<uint32_t>(
                s3g::AmbiSpeakerLayoutPreset::Custom),
            serial)) {
        return;
    }
    if (previousField == customField
        && previousLayout
            == static_cast<uint32_t>(s3g::AmbiSpeakerLayoutPreset::Custom)) {
        return;
    }
    resetRuntimeSpeakerControls(
        plugin,
        plugin.realtimeActiveSpeakers.load(std::memory_order_relaxed),
        serial);
}

bool applyDecoderCommand(Plugin& plugin, const DecoderCommand& command)
{
    if (command.epoch != plugin.commandEpoch.load(std::memory_order_acquire)) return false;
    auto params = plugin.modelDecoder.params();
    switch (command.kind) {
    case DecoderCommandKind::SetParam: {
        switch (command.paramId) {
        case kLayoutParamId:
            params.layout = static_cast<s3g::AmbiSpeakerLayoutPreset>(
                std::clamp<uint32_t>(
                    static_cast<uint32_t>(std::lround(command.value)),
                    0u, kMaxLayoutParamValue));
            break;
        case kModeParamId:
            params.mode = static_cast<s3g::AmbiSpeakerDecoderMode>(
                std::clamp<uint32_t>(
                    static_cast<uint32_t>(std::lround(command.value)), 0u, 3u));
            break;
        case kOrderParamId:
            params.order = std::clamp<uint32_t>(
                static_cast<uint32_t>(std::lround(command.value)),
                1u, s3g::kAmbiSpeakerDecoderMaxOrder);
            break;
        case kActiveSpeakersParamId:
            params.activeSpeakers = std::clamp<uint32_t>(
                static_cast<uint32_t>(std::lround(command.value)),
                2u, s3g::kAmbiSpeakerDecoderMaxSpeakers);
            params.layout = s3g::AmbiSpeakerLayoutPreset::Custom;
            break;
        case kSelectedSpeakerParamId:
            params.selectedSpeaker = std::clamp<uint32_t>(
                static_cast<uint32_t>(std::lround(command.value)),
                1u, std::max<uint32_t>(1u, params.activeSpeakers)) - 1u;
            break;
        case kRegularizationParamId:
            params.regularization = static_cast<float>(
                std::clamp(command.value, 0.0, 0.20));
            break;
        case kEnergyParamId:
            params.energy = static_cast<float>(
                std::clamp(command.value, 0.0, 1.50));
            break;
        case kWeightingParamId:
            params.weighting = static_cast<s3g::AmbiSpeakerDecoderWeighting>(
                std::clamp<uint32_t>(
                    static_cast<uint32_t>(std::lround(command.value)), 0u, 2u));
            break;
        case kCustomFieldParamId:
            params.customField = static_cast<s3g::AmbiSpeakerCustomField>(
                std::clamp<uint32_t>(
                    static_cast<uint32_t>(std::lround(command.value)), 0u, 1u));
            params.layout = s3g::AmbiSpeakerLayoutPreset::Custom;
            break;
        default:
            return false;
        }
        plugin.modelDecoder.setParams(params);
        return true;
    }
    case DecoderCommandKind::SetAzimuth:
    case DecoderCommandKind::SetElevation:
    case DecoderCommandKind::SetDistance: {
        const uint32_t speakerIndex = std::min<uint32_t>(
            command.speakerIndex,
            std::max<uint32_t>(1u, params.activeSpeakers) - 1u);
        auto speaker = plugin.modelDecoder.speaker(speakerIndex);
        if (command.kind == DecoderCommandKind::SetAzimuth) {
            speaker.azimuthDeg = static_cast<float>(
                std::clamp(command.value, -180.0, 180.0));
        } else if (command.kind == DecoderCommandKind::SetElevation) {
            speaker.elevationDeg = static_cast<float>(
                std::clamp(command.value, -90.0, 90.0));
        } else {
            speaker.distance = static_cast<float>(
                std::clamp(command.value, 0.15, 2.0));
        }
        plugin.modelDecoder.setSpeaker(speakerIndex, speaker);
        return true;
    }
    case DecoderCommandKind::SetGain: {
        const uint32_t speakerIndex = std::min<uint32_t>(
            command.speakerIndex,
            std::max<uint32_t>(1u, params.activeSpeakers) - 1u);
        params.selectedSpeaker = std::min<uint32_t>(
            speakerIndex,
            std::max<uint32_t>(1u, params.activeSpeakers) - 1u);
        plugin.modelDecoder.setParams(params);
        return true;
    }
    case DecoderCommandKind::SetEnabled: {
        const uint32_t speakerIndex = std::min<uint32_t>(
            command.speakerIndex,
            std::max<uint32_t>(1u, params.activeSpeakers) - 1u);
        plugin.modelDecoder.setSpeakerEnabled(speakerIndex, command.value >= 0.5);
        return true;
    }
    case DecoderCommandKind::SetSolo: {
        const uint32_t speakerIndex = std::min<uint32_t>(
            command.speakerIndex,
            std::max<uint32_t>(1u, params.activeSpeakers) - 1u);
        params.selectedSpeaker = std::min<uint32_t>(
            speakerIndex,
            std::max<uint32_t>(1u, params.activeSpeakers) - 1u);
        plugin.modelDecoder.setParams(params);
        return true;
    }
    case DecoderCommandKind::SetWidth:
    case DecoderCommandKind::SetOutput:
        return false;
    }
    return false;
}

void commitRuntimeCommand(Plugin& plugin, const DecoderCommand& command)
{
    const uint32_t serial = command.deferRuntimeCommit
        ? allocateRuntimeSerial(plugin)
        : command.runtimeSerial;
    switch (command.kind) {
    case DecoderCommandKind::SetParam:
        switch (command.paramId) {
        case kLayoutParamId:
            applyDesiredLayoutReset(
                plugin,
                std::clamp<uint32_t>(
                    static_cast<uint32_t>(std::lround(command.value)),
                    0u, kMaxLayoutParamValue),
                serial);
            break;
        case kActiveSpeakersParamId:
            applyDesiredActiveReset(
                plugin,
                std::clamp<uint32_t>(
                    static_cast<uint32_t>(std::lround(command.value)),
                    2u, s3g::kAmbiSpeakerDecoderMaxSpeakers),
                serial);
            break;
        case kCustomFieldParamId:
            applyDesiredCustomFieldReset(
                plugin,
                std::clamp<uint32_t>(
                    static_cast<uint32_t>(std::lround(command.value)),
                    0u, 1u),
                serial);
            break;
        case kSelectedSpeakerParamId: {
            const uint32_t active = std::clamp<uint32_t>(
                plugin.realtimeActiveSpeakers.load(std::memory_order_acquire),
                2u, s3g::kAmbiSpeakerDecoderMaxSpeakers);
            plugin.realtimeSelectedSpeaker.storeIfNewer(
                std::clamp<uint32_t>(
                    static_cast<uint32_t>(std::lround(command.value)),
                    1u, active) - 1u,
                serial);
            break;
        }
        default:
            break;
        }
        break;
    case DecoderCommandKind::SetAzimuth:
    case DecoderCommandKind::SetElevation:
    case DecoderCommandKind::SetDistance:
        plugin.realtimeLayout.storeIfNewer(
            static_cast<uint32_t>(
                s3g::AmbiSpeakerLayoutPreset::Custom),
            serial);
        break;
    case DecoderCommandKind::SetGain: {
        const uint32_t speaker = std::min<uint32_t>(
            command.speakerIndex,
            s3g::kAmbiSpeakerDecoderMaxSpeakers - 1u);
        plugin.realtimeSpeakerGain[speaker].storeIfNewer(
            static_cast<float>(
                std::clamp(command.value, 0.0, 2.0)),
            serial);
        plugin.realtimeSelectedSpeaker.storeIfNewer(
            std::min<uint32_t>(
                speaker,
                std::max<uint32_t>(
                    1u,
                    plugin.realtimeActiveSpeakers.load(
                        std::memory_order_acquire)) - 1u),
            serial);
        break;
    }
    case DecoderCommandKind::SetEnabled: {
        const uint32_t speaker = std::min<uint32_t>(
            command.speakerIndex,
            s3g::kAmbiSpeakerDecoderMaxSpeakers - 1u);
        plugin.realtimeSpeakerEnabled[speaker].storeIfNewer(
            command.value >= 0.5, serial);
        plugin.realtimeSelectedSpeaker.storeIfNewer(
            std::min<uint32_t>(
                speaker,
                std::max<uint32_t>(
                    1u,
                    plugin.realtimeActiveSpeakers.load(
                        std::memory_order_acquire)) - 1u),
            serial);
        break;
    }
    case DecoderCommandKind::SetSolo: {
        const uint32_t speaker = std::min<uint32_t>(
            command.speakerIndex,
            s3g::kAmbiSpeakerDecoderMaxSpeakers - 1u);
        plugin.realtimeSpeakerSolo[speaker].storeIfNewer(
            command.value >= 0.5, serial);
        plugin.realtimeSelectedSpeaker.storeIfNewer(
            std::min<uint32_t>(
                speaker,
                std::max<uint32_t>(
                    1u,
                    plugin.realtimeActiveSpeakers.load(
                        std::memory_order_acquire)) - 1u),
            serial);
        break;
    }
    case DecoderCommandKind::SetWidth:
        plugin.realtimeWidth.storeIfNewer(
            static_cast<float>(
                std::clamp(command.value, 0.0, 1.50)),
            serial);
        break;
    case DecoderCommandKind::SetOutput:
        plugin.realtimeOutputGainDb.storeIfNewer(
            static_cast<float>(
                std::clamp(command.value, -60.0, 12.0)),
            serial);
        break;
    }
}

bool shouldCommitRuntimeImmediately(const DecoderCommand& command)
{
    if (command.kind == DecoderCommandKind::SetWidth
        || command.kind == DecoderCommandKind::SetOutput) {
        return true;
    }
    if (command.kind == DecoderCommandKind::SetGain
        || command.kind == DecoderCommandKind::SetEnabled
        || command.kind == DecoderCommandKind::SetSolo) {
        // Selected-speaker parameters are resolved in actual FIFO order by
        // the worker. An optimistic write to the producer's stale selection
        // could otherwise survive on the wrong channel.
        return !command.speakerFollowsSelection;
    }
    return command.kind == DecoderCommandKind::SetParam
        && command.paramId == kSelectedSpeakerParamId;
}

bool storeWorkerCompletions(Plugin& plugin)
{
    bool changed = false;
    const uint64_t completedQueue =
        plugin.completedQueuePosition.load(std::memory_order_relaxed);
    if (completedQueue < plugin.workerAppliedQueuePosition) {
        plugin.completedQueuePosition.store(
            plugin.workerAppliedQueuePosition, std::memory_order_release);
        changed = true;
    }
    const uint64_t completedOverflow =
        plugin.completedOverflowPosition.load(std::memory_order_relaxed);
    if (completedOverflow < plugin.workerAppliedOverflowPosition) {
        plugin.completedOverflowPosition.store(
            plugin.workerAppliedOverflowPosition, std::memory_order_release);
        changed = true;
    }
    return changed;
}

bool loadPendingOverflow(Plugin& plugin)
{
    if (plugin.workerHasPendingOverflow) return true;
    if (!plugin.decoderOverflowCommands.tryPop(
            plugin.workerPendingOverflow,
            &plugin.workerPendingOverflowPosition)) {
        return false;
    }
    plugin.workerHasPendingOverflow = true;
    return true;
}

DecoderCommand resolveWorkerCommand(
    Plugin& plugin, const DecoderCommand& queued)
{
    DecoderCommand resolved = queued;
    if (resolved.speakerFollowsSelection) {
        const auto params = plugin.modelDecoder.params();
        resolved.speakerIndex = std::min<uint32_t>(
            params.selectedSpeaker,
            std::max<uint32_t>(1u, params.activeSpeakers) - 1u);
    }
    return resolved;
}

void applyPendingOverflow(Plugin& plugin)
{
    if (!plugin.workerHasPendingOverflow) return;
    if (plugin.workerPendingOverflow.epoch
        == plugin.commandEpoch.load(std::memory_order_acquire)) {
        auto committed =
            resolveWorkerCommand(plugin, plugin.workerPendingOverflow);
        if (applyDecoderCommand(plugin, committed)) {
            plugin.decoderPublishPending = true;
        }
        committed.deferRuntimeCommit = true;
        commitRuntimeCommand(plugin, committed);
    }
    plugin.workerAppliedOverflowPosition =
        plugin.workerPendingOverflowPosition;
    plugin.workerHasPendingOverflow = false;
}

void decoderWorkerMain(Plugin* plugin)
{
    while (!plugin->stopDecoderWorker.load(std::memory_order_acquire)) {
        bool receivedCommand = false;
        bool completedWork = false;
        {
            std::lock_guard<std::mutex> lock(plugin->modelMutex);
            plugin->modelDecoder.beginBatchUpdate();
            uint32_t appliedCount = 0u;
            while (appliedCount < kDecoderWorkerBatchLimit) {
                if (loadPendingOverflow(*plugin)
                    && plugin->workerPendingOverflow.queueBoundary
                        <= plugin->workerAppliedQueuePosition) {
                    applyPendingOverflow(*plugin);
                    receivedCommand = true;
                    ++appliedCount;
                    continue;
                }

                if (!plugin->workerHasPendingPrimary) {
                    if (!plugin->decoderCommands.tryPop(
                            plugin->workerPendingPrimary,
                            &plugin->workerPendingPrimaryPosition)) {
                        break;
                    }
                    plugin->workerHasPendingPrimary = true;
                }

                // An overflow command published before this primary reservation
                // must run first. Keep the primary pending across batches so
                // this ordering rule never bypasses the batch latency cap.
                if (loadPendingOverflow(*plugin)
                    && plugin->workerPendingOverflow.queueBoundary
                        < plugin->workerPendingPrimaryPosition) {
                    applyPendingOverflow(*plugin);
                    receivedCommand = true;
                    ++appliedCount;
                    continue;
                }

                receivedCommand = true;
                if (plugin->workerPendingPrimary.epoch
                    == plugin->commandEpoch.load(std::memory_order_acquire)) {
                    auto committed = resolveWorkerCommand(
                        *plugin, plugin->workerPendingPrimary);
                    if (applyDecoderCommand(*plugin, committed)) {
                        plugin->decoderPublishPending = true;
                    }
                    committed.deferRuntimeCommit = true;
                    commitRuntimeCommand(*plugin, committed);
                }
                plugin->workerAppliedQueuePosition =
                    plugin->workerPendingPrimaryPosition;
                plugin->workerHasPendingPrimary = false;
                ++appliedCount;
            }
            plugin->modelDecoder.endBatchUpdate();
            if (plugin->decoderPublishPending) {
                publishModelDecoder(*plugin);
            }
            if (!plugin->decoderPublishPending) {
                std::lock_guard<std::mutex> completionLock(
                    plugin->completionMutex);
                completedWork = storeWorkerCompletions(*plugin);
            }
        }
        if (completedWork) plugin->completionCondition.notify_all();
        if (!receivedCommand && !completedWork) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}

void prepareCommand(Plugin& plugin, DecoderCommand& command)
{
    const uint64_t epochBefore =
        plugin.commandEpoch.load(std::memory_order_acquire);
    if ((epochBefore & 1u) != 0u) {
        command.epoch = epochBefore + 1u;
        command.deferRuntimeCommit = true;
        return;
    }

    command.runtimeSerial = allocateRuntimeSerial(plugin);
    const uint64_t epochAfter =
        plugin.commandEpoch.load(std::memory_order_acquire);
    if (epochAfter == epochBefore) {
        command.epoch = epochBefore;
        command.deferRuntimeCommit = false;
        return;
    }

    // State load crossed this preparation. Queue the event for the stable
    // generation that follows it and let the worker publish its runtime
    // shadow after the loaded decoder is visible.
    command.epoch =
        (epochAfter & 1u) != 0u ? epochAfter + 1u : epochAfter;
    command.deferRuntimeCommit = true;
}

bool submitPreparedCommandNonRealtime(
    Plugin& plugin, DecoderCommand& command)
{
    while (!plugin.decoderCommands.tryPush(command)
        && !plugin.stopDecoderWorker.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    return !plugin.stopDecoderWorker.load(std::memory_order_acquire);
}

bool submitCommandNonRealtime(Plugin& plugin, DecoderCommand& command)
{
    prepareCommand(plugin, command);
    return submitPreparedCommandNonRealtime(plugin, command);
}

bool submitPreparedCommandRealtime(
    Plugin& plugin, DecoderCommand& command)
{
    if (plugin.decoderCommands.tryPush(command)) return true;
    command.queueBoundary = plugin.decoderCommands.producerPosition();
    return plugin.decoderOverflowCommands.tryPush(command);
}

bool submitCommandRealtime(Plugin& plugin, DecoderCommand& command)
{
    prepareCommand(plugin, command);
    return submitPreparedCommandRealtime(plugin, command);
}

bool waitForSubmittedCommands(Plugin& plugin, std::chrono::milliseconds timeout)
{
    const uint64_t queueTarget = plugin.decoderCommands.producerPosition();
    const uint64_t overflowTarget =
        plugin.decoderOverflowCommands.producerPosition();
    auto completed = [&] {
        return plugin.completedQueuePosition.load(std::memory_order_acquire)
                >= queueTarget
            && plugin.completedOverflowPosition.load(std::memory_order_acquire)
                >= overflowTarget;
    };
    if (completed()) return true;
    std::unique_lock<std::mutex> lock(plugin.completionMutex);
    return plugin.completionCondition.wait_for(lock, timeout, completed);
}

void applyParam(Plugin& p, clap_id id, double value)
{
    if (!std::isfinite(value)) return;
    const uint32_t selected = std::min<uint32_t>(
        p.realtimeSelectedSpeaker.load(std::memory_order_relaxed),
        std::max<uint32_t>(
            1u, p.realtimeActiveSpeakers.load(std::memory_order_relaxed)) - 1u);
    DecoderCommand command {};
    switch (id) {
    case kWidthParamId:
        command.kind = DecoderCommandKind::SetWidth;
        command.value = value;
        break;
    case kOutputParamId:
        command.kind = DecoderCommandKind::SetOutput;
        command.value = value;
        break;
    case kSelectedSpeakerParamId:
        command.kind = DecoderCommandKind::SetParam;
        command.paramId = id;
        command.value = value;
        break;
    case kLayoutParamId:
        command.kind = DecoderCommandKind::SetParam;
        command.paramId = id;
        command.value = value;
        break;
    case kActiveSpeakersParamId:
        command.kind = DecoderCommandKind::SetParam;
        command.paramId = id;
        command.value = value;
        break;
    case kCustomFieldParamId:
        command.kind = DecoderCommandKind::SetParam;
        command.paramId = id;
        command.value = value;
        break;
    case kAzimuthParamId:
        command.kind = DecoderCommandKind::SetAzimuth;
        command.speakerIndex = selected;
        command.speakerFollowsSelection = true;
        command.value = value;
        break;
    case kElevationParamId:
        command.kind = DecoderCommandKind::SetElevation;
        command.speakerIndex = selected;
        command.speakerFollowsSelection = true;
        command.value = value;
        break;
    case kDistanceParamId:
        command.kind = DecoderCommandKind::SetDistance;
        command.speakerIndex = selected;
        command.speakerFollowsSelection = true;
        command.value = value;
        break;
    case kSpeakerGainParamId:
        command.kind = DecoderCommandKind::SetGain;
        command.speakerIndex = selected;
        command.speakerFollowsSelection = true;
        command.value = value;
        break;
    case kSpeakerEnabledParamId:
        command.kind = DecoderCommandKind::SetEnabled;
        command.speakerIndex = selected;
        command.speakerFollowsSelection = true;
        command.value = value;
        break;
    default:
        command.kind = DecoderCommandKind::SetParam;
        command.paramId = id;
        command.value = value;
        break;
    }

    if (command.kind == DecoderCommandKind::SetWidth
        || command.kind == DecoderCommandKind::SetOutput) {
        prepareCommand(p, command);
        if (command.deferRuntimeCommit) {
            if (!submitPreparedCommandNonRealtime(p, command)) return;
        } else {
            commitRuntimeCommand(p, command);
        }
        return;
    }

    if (!submitCommandNonRealtime(p, command)) return;
    if (!command.deferRuntimeCommit
        && shouldCommitRuntimeImmediately(command)) {
        commitRuntimeCommand(p, command);
    }
}

void setSpeakerGainNonRealtime(Plugin& plugin, uint32_t speakerIndex, double value)
{
    if (speakerIndex >= s3g::kAmbiSpeakerDecoderMaxSpeakers) return;
    value = std::clamp(value, 0.0, 2.0);
    DecoderCommand command {};
    command.kind = DecoderCommandKind::SetGain;
    command.speakerIndex = speakerIndex;
    command.value = value;
    if (!submitCommandNonRealtime(plugin, command)) return;
    if (!command.deferRuntimeCommit
        && shouldCommitRuntimeImmediately(command)) {
        commitRuntimeCommand(plugin, command);
    }
}

void setSpeakerEnabledNonRealtime(Plugin& plugin, uint32_t speakerIndex, bool enabled)
{
    if (speakerIndex >= s3g::kAmbiSpeakerDecoderMaxSpeakers) return;
    DecoderCommand command {};
    command.kind = DecoderCommandKind::SetEnabled;
    command.speakerIndex = speakerIndex;
    command.value = enabled ? 1.0 : 0.0;
    if (!submitCommandNonRealtime(plugin, command)) return;
    if (!command.deferRuntimeCommit
        && shouldCommitRuntimeImmediately(command)) {
        commitRuntimeCommand(plugin, command);
    }
}

void setSpeakerSoloNonRealtime(Plugin& plugin, uint32_t speakerIndex, bool solo)
{
    if (speakerIndex >= s3g::kAmbiSpeakerDecoderMaxSpeakers) return;
    DecoderCommand command {};
    command.kind = DecoderCommandKind::SetSolo;
    command.speakerIndex = speakerIndex;
    command.value = solo ? 1.0 : 0.0;
    if (!submitCommandNonRealtime(plugin, command)) return;
    if (!command.deferRuntimeCommit
        && shouldCommitRuntimeImmediately(command)) {
        commitRuntimeCommand(plugin, command);
    }
}

bool init(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    {
        std::lock_guard<std::mutex> lock(p->modelMutex);
        p->modelDecoder.prepare(p->sampleRate);
        storeRuntimeFromDecoder(*p, p->modelDecoder);
        normalizeModelRuntimeControls(*p);
        for (auto& slot : p->decoderSnapshots) {
            slot.decoder = p->modelDecoder;
            slot.runtimeMixer = p->modelRuntimeMixer;
        }
        p->publishedDecoder.store(0u, std::memory_order_release);
        p->decoderSnapshots[0u].state.fetch_add(
            1u, std::memory_order_relaxed);
        p->audioDecoderSnapshot = 0u;
        p->audioDecoderSnapshotHeld = true;
    }
    try {
        p->decoderWorker = std::thread(decoderWorkerMain, p);
    } catch (...) {
        return false;
    }
    return true;
}

void destroy(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
#if defined(__APPLE__)
    if (p->guiView)
        s3g::clap_gui::destroyResponsiveViewport(p->guiViewport, p->guiView);
#endif
    p->stopDecoderWorker.store(true, std::memory_order_release);
    if (p->decoderWorker.joinable()) p->decoderWorker.join();
    if (p->audioDecoderSnapshotHeld) {
        p->decoderSnapshots[p->audioDecoderSnapshot].state.fetch_sub(
            1u, std::memory_order_release);
        p->audioDecoderSnapshotHeld = false;
    }
    delete p;
}

bool activate(const clap_plugin_t* plugin, double sampleRate, uint32_t, uint32_t maxFrames)
{
    auto* p = self(plugin);
    p->sampleRate = sampleRate;
    p->maxFrames = maxFrames;
    if (!waitForSubmittedCommands(*p, std::chrono::milliseconds(2000))) return false;
    std::lock_guard<std::mutex> lock(p->modelMutex);
    auto params = p->modelDecoder.params();
    auto speakers = p->modelDecoder.speakers();
    p->modelDecoder.prepare(sampleRate);
    p->modelDecoder.beginBatchUpdate();
    p->modelDecoder.setParams(params);
    p->modelDecoder.setSpeakers(speakers);
    p->modelDecoder.endBatchUpdate();
    p->decoderPublishPending = true;
    publishModelDecoderBlocking(*p);
    return true;
}

void deactivate(const clap_plugin_t*) {}
bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}
void reset(const clap_plugin_t* plugin) { self(plugin)->outputPeak.store(0.0f, std::memory_order_relaxed); }

void readParamEvents(Plugin& p, const clap_input_events_t* in)
{
    if (!in) return;
    uint32_t activeSpeakers = std::clamp<uint32_t>(
        p.realtimeActiveSpeakers.load(std::memory_order_acquire),
        2u, s3g::kAmbiSpeakerDecoderMaxSpeakers);
    uint32_t selected = std::min<uint32_t>(
        p.realtimeSelectedSpeaker.load(std::memory_order_relaxed),
        activeSpeakers - 1u);
    const uint32_t n = in->size(in);
    for (uint32_t i = 0; i < n; ++i) {
        const clap_event_header_t* ev = in->get(in, i);
        if (ev && ev->space_id == CLAP_CORE_EVENT_SPACE_ID && ev->type == CLAP_EVENT_PARAM_VALUE) {
            const auto* param = reinterpret_cast<const clap_event_param_value_t*>(ev);
            if (!std::isfinite(param->value)) continue;
            DecoderCommand command {};
            if (param->param_id == kWidthParamId) {
                command.kind = DecoderCommandKind::SetWidth;
                command.value = param->value;
                prepareCommand(p, command);
                if (command.deferRuntimeCommit) {
                    submitPreparedCommandRealtime(p, command);
                } else {
                    commitRuntimeCommand(p, command);
                }
                continue;
            }
            if (param->param_id == kOutputParamId) {
                command.kind = DecoderCommandKind::SetOutput;
                command.value = param->value;
                prepareCommand(p, command);
                if (command.deferRuntimeCommit) {
                    submitPreparedCommandRealtime(p, command);
                } else {
                    commitRuntimeCommand(p, command);
                }
                continue;
            }
            if (param->param_id == kSelectedSpeakerParamId) {
                command.kind = DecoderCommandKind::SetParam;
                command.paramId = param->param_id;
            } else if (param->param_id == kAzimuthParamId) {
                command.kind = DecoderCommandKind::SetAzimuth;
                command.speakerIndex = selected;
                command.speakerFollowsSelection = true;
            } else if (param->param_id == kElevationParamId) {
                command.kind = DecoderCommandKind::SetElevation;
                command.speakerIndex = selected;
                command.speakerFollowsSelection = true;
            } else if (param->param_id == kDistanceParamId) {
                command.kind = DecoderCommandKind::SetDistance;
                command.speakerIndex = selected;
                command.speakerFollowsSelection = true;
            } else if (param->param_id == kSpeakerGainParamId) {
                command.kind = DecoderCommandKind::SetGain;
                command.speakerIndex = selected;
                command.speakerFollowsSelection = true;
            } else if (param->param_id == kSpeakerEnabledParamId) {
                command.kind = DecoderCommandKind::SetEnabled;
                command.speakerIndex = selected;
                command.speakerFollowsSelection = true;
            } else {
                command.kind = DecoderCommandKind::SetParam;
                command.paramId = param->param_id;
            }
            command.value = param->value;
            if (!submitCommandRealtime(p, command)) continue;
            if (!command.deferRuntimeCommit
                && shouldCommitRuntimeImmediately(command)) {
                commitRuntimeCommand(p, command);
            }

            if (param->param_id == kSelectedSpeakerParamId) {
                selected = std::clamp<uint32_t>(
                    static_cast<uint32_t>(std::lround(param->value)),
                    1u, activeSpeakers) - 1u;
            } else if (param->param_id == kLayoutParamId) {
                const uint32_t layout = std::clamp<uint32_t>(
                    static_cast<uint32_t>(std::lround(param->value)),
                    0u, kMaxLayoutParamValue);
                activeSpeakers = activeSpeakersForLayout(
                    layout, activeSpeakers);
                selected = std::min<uint32_t>(
                    selected, activeSpeakers - 1u);
            } else if (param->param_id == kActiveSpeakersParamId) {
                activeSpeakers = std::clamp<uint32_t>(
                    static_cast<uint32_t>(std::lround(param->value)),
                    2u, s3g::kAmbiSpeakerDecoderMaxSpeakers);
                selected = std::min<uint32_t>(
                    selected, activeSpeakers - 1u);
            }
        }
    }
}

clap_process_status process(const clap_plugin_t* plugin, const clap_process_t* proc)
{
    auto* p = self(plugin);
    readParamEvents(*p, proc->in_events);
    if (proc->audio_inputs_count == 0 || proc->audio_outputs_count == 0) {
        return CLAP_PROCESS_CONTINUE;
    }

    const auto& input = proc->audio_inputs[0];
    auto& output = proc->audio_outputs[0];
    const uint32_t frames = proc->frames_count;
    const uint32_t inChannels = std::min<uint32_t>(input.channel_count, kInputChannels);
    const uint32_t outChannels = std::min<uint32_t>(output.channel_count, kOutputChannels);

    if (output.data32) {
        s3g::clearAudioBufferFromChannel(output, 0, frames);
    }
    if (!input.data32 || !output.data32 || outChannels == 0u) {
        return CLAP_PROCESS_CONTINUE;
    }

    float blockPeak = 0.0f;
    uint32_t nextSnapshot = 0u;
    if (tryAcquireDecoderSnapshot(*p, nextSnapshot, 32u)) {
        const uint32_t previousSnapshot = p->audioDecoderSnapshot;
        const bool releasePrevious = p->audioDecoderSnapshotHeld;
        p->audioDecoderSnapshot = nextSnapshot;
        p->audioDecoderSnapshotHeld = true;
        if (releasePrevious) {
            p->decoderSnapshots[previousSnapshot].state.fetch_sub(
                1u, std::memory_order_release);
        }
    }
    auto& snapshot = p->decoderSnapshots[p->audioDecoderSnapshot];
    snapshot.decoder.processBlock(
        input.data32, output.data32,
        inChannels, outChannels, frames);
    const auto params = snapshot.decoder.params();
    const auto& publishedSpeakers = snapshot.decoder.speakers();
    const auto runtime =
        resolveRuntimeMixerState(*p, snapshot.runtimeMixer);
    const uint32_t peakChannels = std::min<uint32_t>(outChannels, params.activeSpeakers);
    bool anySolo = false;
    for (uint32_t ch = 0; ch < peakChannels; ++ch) {
        anySolo = anySolo || runtime.solo[ch];
    }
    const float globalGain =
        runtime.width * s3g::dbToGain(runtime.outputGainDb);
    for (uint32_t ch = 0; ch < peakChannels; ++ch) {
        if (!output.data32[ch]) continue;
        const bool audible = publishedSpeakers[ch].enabled
            && runtime.enabled[ch]
            && (!anySolo || runtime.solo[ch]);
        const float channelGain = globalGain * runtime.gain[ch];
        for (uint32_t i = 0; i < frames; ++i) {
            const float sample = audible
                ? s3g::flushDenormal(output.data32[ch][i] * channelGain)
                : 0.0f;
            output.data32[ch][i] = sample;
            blockPeak = std::max(blockPeak, std::fabs(sample));
        }
    }
    s3g::clearAudioBufferFromChannel(output, outChannels, frames);
    p->outputPeak.store(std::max(p->outputPeak.load(std::memory_order_relaxed) * 0.90f, blockPeak), std::memory_order_relaxed);
    return CLAP_PROCESS_CONTINUE;
}

void onMainThread(const clap_plugin_t*) {}

uint32_t audioPortsCount(const clap_plugin_t*, bool) { return 1; }

bool audioPortsGet(const clap_plugin_t*, uint32_t index, bool isInput, clap_audio_port_info_t* info)
{
    if (index != 0 || !info) return false;
    info->id = isInput ? 10 : 20;
    std::strncpy(info->name, isInput ? "7OA ACN/SN3D In" : "64 Speaker Out", sizeof(info->name));
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = isInput ? kInputChannels : kOutputChannels;
    info->port_type = CLAP_PORT_SURROUND;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}

const clap_plugin_audio_ports_t audioPorts { audioPortsCount, audioPortsGet };

struct ParamDef { clap_id id; const char* name; double min; double max; double def; bool stepped; };
constexpr ParamDef kParamDefs[] {
    { kLayoutParamId, "Layout", 0.0, static_cast<double>(kMaxLayoutParamValue), 7.0, true },
    { kModeParamId, "Mode", 0.0, 3.0, 1.0, true },
    { kOrderParamId, "Order", 1.0, 7.0, 3.0, true },
    { kActiveSpeakersParamId, "Active Speakers", 2.0, 64.0, 24.0, true },
    { kSelectedSpeakerParamId, "Selected Speaker", 1.0, 64.0, 1.0, true },
    { kAzimuthParamId, "Speaker Azimuth", -180.0, 180.0, 0.0, false },
    { kElevationParamId, "Speaker Elevation", -90.0, 90.0, 0.0, false },
    { kDistanceParamId, "Speaker Distance", 0.15, 2.0, 1.0, false },
    { kSpeakerGainParamId, "Speaker Gain", 0.0, 2.0, 1.0, false },
    { kWidthParamId, "Width", 0.0, 1.50, 1.0, false },
    { kOutputParamId, "Output", -60.0, 12.0, 0.0, false },
    { kWeightingParamId, "Weighting", 0.0, 2.0, 1.0, true },
    { kCustomFieldParamId, "Custom Field", 0.0, 1.0, 0.0, true },
};

uint32_t paramsCount(const clap_plugin_t*) { return static_cast<uint32_t>(sizeof(kParamDefs) / sizeof(kParamDefs[0])); }

bool paramsGetInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info)
{
    if (!info || index >= paramsCount(nullptr)) return false;
    const auto& def = kParamDefs[index];
    info->id = def.id;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE | (def.stepped ? CLAP_PARAM_IS_STEPPED : 0);
    std::strncpy(info->name, def.name, sizeof(info->name));
    std::strncpy(info->module, "Ambi Speaker Decoder", sizeof(info->module));
    info->min_value = def.min;
    info->max_value = def.max;
    info->default_value = def.def;
    return true;
}

bool paramsGetValue(const clap_plugin_t* plugin, clap_id id, double* value)
{
    if (!value) return false;
    auto* pluginState = self(plugin);
    auto snapshot = acquireDecoderSnapshot(*pluginState);
    const auto p = visibleDecoderParams(
        *pluginState, snapshot.decoder(), snapshot.runtimeMixer());
    switch (id) {
    case kLayoutParamId: *value = static_cast<double>(static_cast<uint32_t>(p.layout)); return true;
    case kModeParamId: *value = static_cast<double>(static_cast<uint32_t>(p.mode)); return true;
    case kOrderParamId: *value = static_cast<double>(p.order); return true;
    case kActiveSpeakersParamId: *value = static_cast<double>(p.activeSpeakers); return true;
    case kSelectedSpeakerParamId: *value = static_cast<double>(p.selectedSpeaker + 1u); return true;
    case kAzimuthParamId: *value = p.selectedAzimuthDeg; return true;
    case kElevationParamId: *value = p.selectedElevationDeg; return true;
    case kDistanceParamId: *value = p.selectedDistance; return true;
    case kSpeakerGainParamId: *value = p.selectedGain; return true;
    case kSpeakerEnabledParamId: *value = 1.0; return true;
    case kRegularizationParamId: *value = p.regularization; return true;
    case kWidthParamId: *value = p.width; return true;
    case kEnergyParamId: *value = p.energy; return true;
    case kOutputParamId: *value = p.outputGainDb; return true;
    case kWeightingParamId: *value = static_cast<double>(static_cast<uint32_t>(p.weighting)); return true;
    case kCustomFieldParamId: *value = static_cast<double>(static_cast<uint32_t>(p.customField)); return true;
    default: return false;
    }
}

bool paramsValueToText(const clap_plugin_t*, clap_id id, double value, char* display, uint32_t size)
{
    if (!display || size == 0) return false;
    if (id == kLayoutParamId) std::snprintf(display, size, "%s", layoutName(static_cast<uint32_t>(std::lround(value))));
    else if (id == kModeParamId) std::snprintf(display, size, "%s", modeName(static_cast<uint32_t>(std::lround(value))));
    else if (id == kWeightingParamId) std::snprintf(display, size, "%s", weightingName(static_cast<uint32_t>(std::lround(value))));
    else if (id == kCustomFieldParamId) std::snprintf(display, size, "%s", customFieldName(static_cast<uint32_t>(std::lround(value))));
    else if (id == kOrderParamId) std::snprintf(display, size, "%.0fOA", value);
    else if (id == kActiveSpeakersParamId || id == kSelectedSpeakerParamId) std::snprintf(display, size, "%.0f", value);
    else if (id == kAzimuthParamId || id == kElevationParamId) std::snprintf(display, size, "%+.1f deg", value);
    else if (id == kOutputParamId) std::snprintf(display, size, "%+.1f dB", value);
    else if (id == kSpeakerEnabledParamId) std::snprintf(display, size, "ON");
    else std::snprintf(display, size, "%.3f", value);
    return true;
}

bool paramsTextToValue(const clap_plugin_t*, clap_id id, const char* display, double* value)
{
    if (!display || !value) return false;
    if (id == kModeParamId) {
        for (uint32_t mode = 0; mode <= 3u; ++mode) {
            if (std::strcmp(display, modeName(mode)) == 0) {
                *value = static_cast<double>(mode);
                return true;
            }
        }
    } else if (id == kLayoutParamId) {
        for (uint32_t layout = 0; layout <= kMaxLayoutParamValue; ++layout) {
            if (std::strcmp(display, layoutName(layout)) == 0) {
                *value = static_cast<double>(layout);
                return true;
            }
        }
    } else if (id == kWeightingParamId) {
        for (uint32_t weighting = 0; weighting <= 2u; ++weighting) {
            if (std::strcmp(display, weightingName(weighting)) == 0) {
                *value = static_cast<double>(weighting);
                return true;
            }
        }
    } else if (id == kCustomFieldParamId) {
        for (uint32_t field = 0; field <= 1u; ++field) {
            if (std::strcmp(display, customFieldName(field)) == 0) {
                *value = static_cast<double>(field);
                return true;
            }
        }
    }
    *value = std::atof(display);
    return true;
}

void paramsFlush(const clap_plugin_t* plugin, const clap_input_events_t* in, const clap_output_events_t*) { readParamEvents(*self(plugin), in); }
const clap_plugin_params_t paramsExt { paramsCount, paramsGetInfo, paramsGetValue, paramsValueToText, paramsTextToValue, paramsFlush };

bool writeStateBytes(
    const clap_ostream_t* stream, const void* source, size_t byteCount)
{
    const auto* bytes = static_cast<const uint8_t*>(source);
    size_t written = 0u;
    while (written < byteCount) {
        const int64_t amount = stream->write(
            stream, bytes + written, byteCount - written);
        if (amount <= 0
            || static_cast<uint64_t>(amount) > byteCount - written) {
            return false;
        }
        written += static_cast<size_t>(amount);
    }
    return true;
}

bool readStateBytes(
    const clap_istream_t* stream, void* destination, size_t byteCount)
{
    auto* bytes = static_cast<uint8_t*>(destination);
    size_t read = 0u;
    while (read < byteCount) {
        const int64_t amount =
            stream->read(stream, bytes + read, byteCount - read);
        if (amount <= 0
            || static_cast<uint64_t>(amount) > byteCount - read) {
            return false;
        }
        read += static_cast<size_t>(amount);
    }
    return true;
}

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    if (!stream || !stream->write) return false;
    SavedState s {};
    auto* p = self(plugin);
    if (!waitForSubmittedCommands(*p, std::chrono::milliseconds(2000))) return false;
    {
        auto snapshot = acquireDecoderSnapshot(*p);
        s.params = visibleDecoderParams(
            *p, snapshot.decoder(), snapshot.runtimeMixer());
        s.speakers = visibleDecoderSpeakers(
            *p, snapshot.decoder(), snapshot.runtimeMixer());
    }
#if defined(__APPLE__)
    s.guiViewMode = p->guiViewMode;
    s.guiViewAzDeg = p->guiViewAzDeg;
    s.guiViewElDeg = p->guiViewElDeg;
    s.guiViewZoom = p->guiViewZoom;
#endif
    return writeStateBytes(stream, &s, sizeof(s));
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) return false;
    uint32_t version = 0;
    if (!readStateBytes(stream, &version, sizeof(version))) return false;
    SavedState s {};
    s.version = version;
    if (version == kStateVersion) {
        char* rest = reinterpret_cast<char*>(&s) + sizeof(s.version);
        const size_t restSize = sizeof(s) - sizeof(s.version);
        if (!readStateBytes(stream, rest, restSize)) return false;
    } else if (version == 4u) {
        SavedStateV4 legacy {};
        legacy.version = version;
        char* rest = reinterpret_cast<char*>(&legacy) + sizeof(legacy.version);
        const size_t restSize = sizeof(legacy) - sizeof(legacy.version);
        if (!readStateBytes(stream, rest, restSize)) return false;
        s.params = legacy.params;
        s.speakers = legacy.speakers;
    } else {
        return false;
    }
    auto* p = self(plugin);
    std::lock_guard<std::mutex> lock(p->modelMutex);
    const uint64_t stableEpoch =
        p->commandEpoch.load(std::memory_order_acquire);
    if ((stableEpoch & 1u) != 0u) return false;
    p->commandEpoch.store(stableEpoch + 1u, std::memory_order_release);
    const uint32_t loadSerial = allocateRuntimeSerial(*p);

    const float loadedWidth = std::isfinite(s.params.width) ? s.params.width : 1.0f;
    const float loadedOutputGain =
        std::isfinite(s.params.outputGainDb) ? s.params.outputGainDb : 0.0f;
    const uint32_t loadedActiveSpeakers = std::clamp<uint32_t>(
        s.params.activeSpeakers, 2u, s3g::kAmbiSpeakerDecoderMaxSpeakers);
    p->realtimeLayout.storeIfNewer(
        std::min<uint32_t>(
            static_cast<uint32_t>(s.params.layout), kMaxLayoutParamValue),
        loadSerial);
    p->realtimeCustomField.storeIfNewer(
        std::min<uint32_t>(
            static_cast<uint32_t>(s.params.customField), 1u),
        loadSerial);
    p->realtimeActiveSpeakers.storeIfNewer(
        loadedActiveSpeakers, loadSerial);
    p->realtimeSelectedSpeaker.storeIfNewer(
        std::min<uint32_t>(
            s.params.selectedSpeaker,
            loadedActiveSpeakers - 1u),
        loadSerial);

    p->modelRuntimeMixer.serial = loadSerial;
    p->modelRuntimeMixer.width =
        std::clamp(loadedWidth, 0.0f, 1.50f);
    p->modelRuntimeMixer.outputGainDb =
        std::clamp(loadedOutputGain, -60.0f, 12.0f);
    auto internalParams = s.params;
    auto internalSpeakers = s.speakers;
    internalParams.width = 1.0f;
    internalParams.outputGainDb = 0.0f;
    internalParams.selectedGain = 1.0f;
    for (uint32_t speaker = 0; speaker < internalSpeakers.size(); ++speaker) {
        const float loadedGain = std::isfinite(s.speakers[speaker].gain)
            ? s.speakers[speaker].gain
            : 1.0f;
        p->modelRuntimeMixer.gain[speaker] =
            std::clamp(loadedGain, 0.0f, 2.0f);
        p->modelRuntimeMixer.enabled[speaker] =
            s.speakers[speaker].enabled;
        p->modelRuntimeMixer.solo[speaker] =
            s.speakers[speaker].solo;
        internalSpeakers[speaker].gain = 1.0f;
        internalSpeakers[speaker].solo = false;
    }
    p->modelDecoder.beginBatchUpdate();
    p->modelDecoder.setParams(internalParams);
    p->modelDecoder.setSpeakers(internalSpeakers);
    p->modelDecoder.endBatchUpdate();
    p->decoderPublishPending = true;
    publishModelDecoderBlocking(*p);
    p->commandEpoch.store(stableEpoch + 2u, std::memory_order_release);
#if defined(__APPLE__)
    p->guiViewMode = std::clamp<int>(s.guiViewMode, 0, 2);
    p->guiViewAzDeg = std::clamp(s.guiViewAzDeg, -180.0, 180.0);
    p->guiViewElDeg = std::clamp(s.guiViewElDeg, -90.0, 90.0);
    p->guiViewZoom = std::clamp(s.guiViewZoom, 0.25, 4.0);
#endif
    return true;
}

const clap_plugin_state_t stateExt { stateSave, stateLoad };

} // namespace

#if defined(__APPLE__)
@interface S3GAmbiSpeakerDecoderView : NSView <NSTextFieldDelegate> {
    void* _plugin;
    int _dragSlider;
    NSTimer* _timer;
    int _viewMode;
    int _rightPage;
    CGFloat _viewAzDeg;
    CGFloat _viewElDeg;
    CGFloat _viewZoom;
    BOOL _dragView;
    NSPoint _lastDragPoint;
    int _openMenu;
    int _hoverMenuItem;
    uint32_t _menuItemCount;
    NSPoint _menuOrigin;
    NSTextField* _azField;
    NSTextField* _elField;
    NSTextField* _distField;
    BOOL _azFieldDirty;
    BOOL _elFieldDirty;
    BOOL _distFieldDirty;
    BOOL _hasSpeakerSelection;
    int _mixerPage;
    NSTextField* _gainFields[16];
    BOOL _gainFieldDirty[16];
    int _dragMixerSpeaker;
    BOOL _dragMixerOutput;
    char _titlePresetName[64];
}
- (id)initWithPlugin:(void*)plugin;
- (BOOL)acceptsFirstResponder;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
- (void)storeViewState;
- (NSTextField*)makeValueField:(NSInteger)tag;
- (NSTextField*)makeMixerGainField:(NSInteger)tag;
- (void)updateValueFields;
- (void)updateMixerGainFieldsForRect:(NSRect)rect;
- (void)drawSlider:(NSString*)name value:(NSString*)value norm:(CGFloat)norm y:(CGFloat)y attrs:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style;
- (void)drawMenu:(NSString*)name value:(NSString*)value y:(CGFloat)y attrs:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style;
- (void)drawOpenMenu:(NSDictionary*)attrs;
- (void)updateMenuHover:(NSPoint)point;
- (NSRect)fieldPageButtonRect:(int)index inRect:(NSRect)rect;
- (void)drawFieldPageButtonsInRect:(NSRect)rect attrs:(NSDictionary*)attrs;
- (NSRect)viewButtonRect:(int)index inRect:(NSRect)rect;
- (NSRect)zoomButtonRect:(int)index inRect:(NSRect)rect;
- (void)drawViewButtonsInRect:(NSRect)rect attrs:(NSDictionary*)attrs;
- (void)drawZoomButtonsInRect:(NSRect)rect attrs:(NSDictionary*)attrs;
- (void)setViewPreset:(int)mode;
- (CGFloat)viewScaleForRect:(NSRect)rect;
- (NSPoint)projectWorldPoint:(s3g::Vec3)point rect:(NSRect)rect depth:(CGFloat*)depth;
- (void)drawAllRadTopology:(NSRect)rect
                     attrs:(NSDictionary*)attrs
                   decoder:(const s3g::AmbiSpeakerDecoder&)decoder
                  speakers:(const std::array<s3g::AmbiSpeaker,
                                s3g::kAmbiSpeakerDecoderMaxSpeakers>&)speakers;
- (void)drawSpeakerField:(NSRect)rect attrs:(NSDictionary*)attrs small:(NSDictionary*)small style:(const s3g::clap_gui::Style&)style;
- (void)drawDecodeMap:(NSRect)rect attrs:(NSDictionary*)attrs small:(NSDictionary*)small style:(const s3g::clap_gui::Style&)style;
- (NSRect)mixerOutputTrackRect:(NSRect)rect;
- (NSRect)mixerSpeakerRect:(uint32_t)index inRect:(NSRect)rect;
- (NSRect)mixerGainFieldRect:(uint32_t)slot inRect:(NSRect)rect;
- (NSRect)mixerMuteRect:(uint32_t)slot inRect:(NSRect)rect;
- (NSRect)mixerSoloRect:(uint32_t)slot inRect:(NSRect)rect;
- (NSRect)mixerPageButtonRect:(int)index inRect:(NSRect)rect;
- (void)drawMixerPageButtonsInRect:(NSRect)rect attrs:(NSDictionary*)attrs;
- (void)drawSpeakerMixer:(NSRect)rect attrs:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style;
- (void)updateMixerSpeakerGain:(NSPoint)point inRect:(NSRect)rect;
- (void)updateMixerOutput:(NSPoint)point inRect:(NSRect)rect;
- (void)toggleMixerSpeakerMute:(uint32_t)index;
- (void)toggleMixerSpeakerSolo:(uint32_t)index;
- (uint32_t)hitSpeakerAt:(NSPoint)pt inRect:(NSRect)rect found:(BOOL*)found;
- (void)updateSlider:(NSPoint)point;
@end

static NSColor* sdColor(int rgb, double alpha = 1.0) { return s3g::clap_gui::color(rgb, alpha); }

static float linearToSrgb(float v)
{
    const float x = std::clamp(v, 0.0f, 1.0f);
    return x <= 0.0031308f ? x * 12.92f : 1.055f * std::pow(x, 1.0f / 2.4f) - 0.055f;
}

static NSColor* speakerColorFromAed(float azDeg, float elDeg, float distance, bool selected)
{
    const float hue = std::fmod((azDeg / 360.0f) + 1.0f, 1.0f);
    const float light = std::clamp((std::clamp(elDeg, -90.0f, 90.0f) + 90.0f) / 180.0f, 0.28f, 0.88f);
    const float chroma = std::clamp(distance / 2.4f, 0.08f, 1.0f) * 0.37f;
    const float a = std::cos(hue * 2.0f * static_cast<float>(M_PI)) * chroma;
    const float b = std::sin(hue * 2.0f * static_cast<float>(M_PI)) * chroma;
    const float l3 = light + 0.3963377774f * a + 0.2158037573f * b;
    const float m3 = light - 0.1055613458f * a - 0.0638541728f * b;
    const float s3 = light - 0.0894841775f * a - 1.2914855480f * b;
    const float l = l3 * l3 * l3;
    const float m = m3 * m3 * m3;
    const float s = s3 * s3 * s3;
    float r = linearToSrgb(4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s);
    float g = linearToSrgb(-1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s);
    float bl = linearToSrgb(-0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s);
    const float grayMix = selected ? 0.08f : 0.18f;
    r = r * (1.0f - grayMix) + 0.74f * grayMix;
    g = g * (1.0f - grayMix) + 0.74f * grayMix;
    bl = bl * (1.0f - grayMix) + 0.74f * grayMix;
    return [NSColor colorWithCalibratedRed:r green:g blue:bl alpha:selected ? 1.0 : 0.88];
}

@implementation S3GAmbiSpeakerDecoderView
- (id)initWithPlugin:(void*)plugin
{
    self = [super initWithFrame:NSMakeRect(0, 0, 900, 620)];
    if (self) {
        _plugin = plugin;
        _dragSlider = -1;
        _timer = nil;
        _rightPage = 0;
        if (auto* p = static_cast<Plugin*>(_plugin)) {
            _viewMode = p->guiViewMode;
            _viewAzDeg = p->guiViewAzDeg;
            _viewElDeg = p->guiViewElDeg;
            _viewZoom = p->guiViewZoom;
        } else {
            _viewMode = 2;
            _viewAzDeg = 35.0;
            _viewElDeg = 34.0;
            _viewZoom = 1.0;
        }
        _dragView = NO;
        _lastDragPoint = NSMakePoint(0, 0);
        _openMenu = 0;
        _hoverMenuItem = -1;
        _menuItemCount = 0;
        _menuOrigin = NSMakePoint(0, 0);
        _azField = [self makeValueField:kAzimuthParamId];
        _elField = [self makeValueField:kElevationParamId];
        _distField = [self makeValueField:kDistanceParamId];
        [self addSubview:_azField];
        [self addSubview:_elField];
        [self addSubview:_distField];
        for (int i = 0; i < 16; ++i) {
            _gainFields[i] = [self makeMixerGainField:kMixerGainFieldTagBase + i];
            _gainFieldDirty[i] = NO;
            [self addSubview:_gainFields[i]];
        }
        _azFieldDirty = NO;
        _elFieldDirty = NO;
        _distFieldDirty = NO;
        _hasSpeakerSelection = NO;
        _mixerPage = 0;
        _dragMixerSpeaker = -1;
        _dragMixerOutput = NO;
        std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s", "CURRENT");
        [self updateValueFields];
    }
    return self;
}
- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (void)updateTrackingAreas
{
    for (NSTrackingArea* area in [self trackingAreas]) {
        [self removeTrackingArea:area];
    }
    [super updateTrackingAreas];
    NSTrackingAreaOptions options = NSTrackingMouseMoved | NSTrackingActiveInKeyWindow | NSTrackingInVisibleRect;
    NSTrackingArea* area = [[NSTrackingArea alloc] initWithRect:NSZeroRect options:options owner:self userInfo:nil];
    [self addTrackingArea:[area autorelease]];
}
- (void)dealloc { [self storeViewState]; [self stopRefreshTimer]; [super dealloc]; }
- (void)storeViewState
{
    auto* p = static_cast<Plugin*>(_plugin);
    if (!p) return;
    p->guiViewMode = _viewMode;
    p->guiViewAzDeg = _viewAzDeg;
    p->guiViewElDeg = _viewElDeg;
    p->guiViewZoom = _viewZoom;
}
- (void)startRefreshTimer { if (_timer) return; _timer = [NSTimer timerWithTimeInterval:1.0/20.0 target:self selector:@selector(refresh:) userInfo:nil repeats:YES]; [[NSRunLoop mainRunLoop] addTimer:_timer forMode:NSRunLoopCommonModes]; }
- (void)stopRefreshTimer { if (_timer) { [_timer invalidate]; _timer = nil; } }
- (void)refresh:(NSTimer*)timer
{
    (void)timer;
    if ([self isHidden] || !_plugin || !s3g::clap_support::hostAppIsActive()) return;
    [self setNeedsDisplay:YES];
}
- (NSTextField*)makeValueField:(NSInteger)tag
{
    NSTextField* field = [[NSTextField alloc] initWithFrame:NSMakeRect(812, 0, 54, 16)];
    [field setTag:tag];
    [field setDelegate:self];
    s3g::clap_gui::styleNumberTextField(field, 10.0, NSTextAlignmentCenter);
    [field setFormatter:nil];
    return [field autorelease];
}
- (NSTextField*)makeMixerGainField:(NSInteger)tag
{
    NSTextField* field = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 38, 16)];
    [field setTag:tag];
    [field setDelegate:self];
    s3g::clap_gui::styleNumberTextField(field, 8.5, NSTextAlignmentCenter);
    [field setFormatter:nil];
    [field setHidden:YES];
    return [field autorelease];
}
- (BOOL)fieldIsEditing:(NSTextField*)field
{
    return [[self window] firstResponder] == [field currentEditor];
}
- (void)updateValueFields
{
    if (!_plugin) return;
    auto* p = static_cast<Plugin*>(_plugin);
    auto snapshot = acquireDecoderSnapshot(*p);
    const auto prm = visibleDecoderParams(
        *p, snapshot.decoder(), snapshot.runtimeMixer());
    [_azField setHidden:NO];
    [_elField setHidden:NO];
    [_distField setHidden:NO];
    if (_rightPage != 1) {
        for (int i = 0; i < 16; ++i) [_gainFields[i] setHidden:YES];
    }
    [_azField setFrame:NSMakeRect(812, 390, 54, 16)];
    [_elField setFrame:NSMakeRect(812, 416, 54, 16)];
    [_distField setFrame:NSMakeRect(812, 442, 54, 16)];
    if (![self fieldIsEditing:_azField]) {
        [_azField setStringValue:[NSString stringWithFormat:@"%+.1f", prm.selectedAzimuthDeg]];
    }
    if (![self fieldIsEditing:_elField]) {
        [_elField setStringValue:[NSString stringWithFormat:@"%+.1f", prm.selectedElevationDeg]];
    }
    if (![self fieldIsEditing:_distField]) {
        [_distField setStringValue:[NSString stringWithFormat:@"%.2f", prm.selectedDistance]];
    }
}
- (void)updateMixerGainFieldsForRect:(NSRect)rect
{
    if (!_plugin) return;
    auto* p = static_cast<Plugin*>(_plugin);
    auto snapshot = acquireDecoderSnapshot(*p);
    const auto params = visibleDecoderParams(
        *p, snapshot.decoder(), snapshot.runtimeMixer());
    const auto speakers = visibleDecoderSpeakers(
        *p, snapshot.decoder(), snapshot.runtimeMixer());
    const uint32_t n = std::min<uint32_t>(params.activeSpeakers, s3g::kAmbiSpeakerDecoderMaxSpeakers);
    const uint32_t pageStart = static_cast<uint32_t>(_mixerPage) * 16u;
    for (uint32_t slot = 0; slot < 16u; ++slot) {
        NSTextField* field = _gainFields[slot];
        const uint32_t index = pageStart + slot;
        const BOOL visible = _rightPage == 1 && index < n;
        [field setHidden:!visible];
        if (!visible) continue;
        [field setFrame:[self mixerGainFieldRect:slot inRect:rect]];
        if (![self fieldIsEditing:field]) {
            [field setStringValue:[NSString stringWithFormat:@"%.2f", speakers[index].gain]];
        }
    }
}
- (void)controlTextDidChange:(NSNotification*)notification
{
    NSTextField* field = static_cast<NSTextField*>([notification object]);
    if (!field) return;
    if ([field tag] == kAzimuthParamId) {
        _azFieldDirty = YES;
    } else if ([field tag] == kElevationParamId) {
        _elFieldDirty = YES;
    } else if ([field tag] == kDistanceParamId) {
        _distFieldDirty = YES;
    } else if ([field tag] >= kMixerGainFieldTagBase && [field tag] < kMixerGainFieldTagBase + 16) {
        _gainFieldDirty[[field tag] - kMixerGainFieldTagBase] = YES;
    }
}
- (void)controlTextDidEndEditing:(NSNotification*)notification
{
    NSTextField* field = static_cast<NSTextField*>([notification object]);
    if (!field || !_plugin) return;
    auto* p = static_cast<Plugin*>(_plugin);
    const double value = [[field stringValue] doubleValue];
    if ([field tag] == kAzimuthParamId) {
        if (!_azFieldDirty) return;
        _azFieldDirty = NO;
        applyParam(*p, kAzimuthParamId, value);
    } else if ([field tag] == kElevationParamId) {
        if (!_elFieldDirty) return;
        _elFieldDirty = NO;
        applyParam(*p, kElevationParamId, value);
    } else if ([field tag] == kDistanceParamId) {
        if (!_distFieldDirty) return;
        _distFieldDirty = NO;
        applyParam(*p, kDistanceParamId, value);
    } else if ([field tag] >= kMixerGainFieldTagBase && [field tag] < kMixerGainFieldTagBase + 16) {
        const NSInteger slot = [field tag] - kMixerGainFieldTagBase;
        if (!_gainFieldDirty[slot]) return;
        _gainFieldDirty[slot] = NO;
        const uint32_t index = static_cast<uint32_t>(_mixerPage) * 16u + static_cast<uint32_t>(slot);
        auto snapshot = acquireDecoderSnapshot(*p);
        const uint32_t n = std::min<uint32_t>(
            visibleDecoderParams(
                *p, snapshot.decoder(), snapshot.runtimeMixer()).activeSpeakers,
            s3g::kAmbiSpeakerDecoderMaxSpeakers);
        if (index < n) {
            applyParam(*p, kSelectedSpeakerParamId, static_cast<double>(index + 1u));
            setSpeakerGainNonRealtime(*p, index, value);
            _hasSpeakerSelection = YES;
        }
    }
    [self updateValueFields];
    [self updateMixerGainFieldsForRect:NSMakeRect(34, 76, 564, 506)];
    [self setNeedsDisplay:YES];
}
- (void)drawSlider:(NSString*)name value:(NSString*)value norm:(CGFloat)norm y:(CGFloat)y attrs:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style
{
    s3g::clap_gui::drawSlider(name, value, norm, y, attrs, attrs, style, 642, 738, 826, 82);
}
- (void)drawMenu:(NSString*)name value:(NSString*)value y:(CGFloat)y attrs:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style
{
    s3g::clap_gui::drawMenu(name, value, y, attrs, attrs, style, 642, 738, 102);
}
- (void)drawOpenMenu:(NSDictionary*)attrs
{
    if (_openMenu <= 0 || _menuItemCount == 0) return;
    std::array<NSString*, kLayoutMenuCount> layoutItems {};
    for (uint32_t i = 0; i < kLayoutMenuCount; ++i) layoutItems[i] = [NSString stringWithUTF8String:layoutName(layoutPresetForMenuIndex(i))];
    static NSString* modeItems[] = { @"BASIC", @"EPAD", @"MMD", @"ALLRAD" };
    static NSString* orderItems[] = { @"1OA", @"2OA", @"3OA", @"4OA", @"5OA", @"6OA", @"7OA" };
    static NSString* weightItems[] = { @"NONE", @"MAXRE", @"INPHASE" };
    static NSString* fieldItems[] = { @"SPHERE", @"HEMI" };
    NSString** items = layoutItems.data();
    if (_openMenu == 2) items = modeItems;
    else if (_openMenu == 3) items = orderItems;
    else if (_openMenu == 4) items = weightItems;
    else if (_openMenu == 5) items = fieldItems;
    const CGFloat itemH = 18.0;
    const CGFloat w = _openMenu == 1 ? 150.0 : 124.0;
    NSRect menuRect = NSMakeRect(_menuOrigin.x, _menuOrigin.y, w, itemH * static_cast<CGFloat>(_menuItemCount));
    int selected = 0;
    if (_plugin) {
        auto* p = static_cast<Plugin*>(_plugin);
        auto snapshot = acquireDecoderSnapshot(*p);
        const auto prm = visibleDecoderParams(
            *p, snapshot.decoder(), snapshot.runtimeMixer());
        if (_openMenu == 1) {
            const uint32_t layoutValue = static_cast<uint32_t>(prm.layout);
            for (uint32_t i = 0; i < _menuItemCount; ++i) {
                if (layoutPresetForMenuIndex(i) == layoutValue) selected = static_cast<int>(i);
            }
        } else if (_openMenu == 2) selected = static_cast<int>(static_cast<uint32_t>(prm.mode));
        else if (_openMenu == 3) selected = static_cast<int>(prm.order - 1u);
        else if (_openMenu == 4) selected = static_cast<int>(static_cast<uint32_t>(prm.weighting));
        else if (_openMenu == 5) selected = static_cast<int>(static_cast<uint32_t>(prm.customField));
    }
    s3g::clap_gui::Style style;
    s3g::clap_gui::drawDropdownMenu(menuRect, itemH, items, _menuItemCount, selected, _hoverMenuItem, attrs, style);
}
- (void)updateMenuHover:(NSPoint)point
{
    if (_openMenu <= 0 || _menuItemCount == 0) return;
    const CGFloat itemH = 18.0;
    const CGFloat w = _openMenu == 1 ? 150.0 : 124.0;
    const NSRect menuRect = NSMakeRect(_menuOrigin.x, _menuOrigin.y, w, itemH * static_cast<CGFloat>(_menuItemCount));
    const int next = s3g::clap_gui::dropdownHitIndex(point, menuRect, itemH, _menuItemCount);
    if (next != _hoverMenuItem) {
        _hoverMenuItem = next;
        [self setNeedsDisplay:YES];
    }
}
- (NSRect)fieldPageButtonRect:(int)index inRect:(NSRect)rect
{
    const CGFloat w = 56.0;
    const CGFloat h = 13.0;
    const CGFloat gap = 5.0;
    return NSMakeRect(rect.origin.x + 132.0 + static_cast<CGFloat>(index) * (w + gap),
                      rect.origin.y + 4.0,
                      w,
                      h);
}
- (void)drawFieldPageButtonsInRect:(NSRect)rect attrs:(NSDictionary*)attrs
{
    static NSString* labels[] = { @"FIELD", @"MIXER", @"MAP" };
    s3g::clap_gui::Style style;
    for (int i = 0; i < 3; ++i) {
        s3g::clap_gui::drawHeaderButton([self fieldPageButtonRect:i inRect:rect], rect, labels[i], i == _rightPage, attrs, style);
    }
}
- (NSRect)viewButtonRect:(int)index inRect:(NSRect)rect
{
    const CGFloat w = 38.0;
    const CGFloat h = 13.0;
    const CGFloat gap = 5.0;
    const CGFloat x = NSMaxX(rect) - 10.0 - (3.0 - static_cast<CGFloat>(index)) * w - (2.0 - static_cast<CGFloat>(index)) * gap;
    return NSMakeRect(x, rect.origin.y + 4.0, w, h);
}
- (NSRect)zoomButtonRect:(int)index inRect:(NSRect)rect
{
    const CGFloat w = 18.0;
    const CGFloat h = 13.0;
    const CGFloat gap = 4.0;
    const CGFloat viewStart = [self viewButtonRect:0 inRect:rect].origin.x;
    const CGFloat x = viewStart - 12.0 - (2.0 - static_cast<CGFloat>(index)) * w - (1.0 - static_cast<CGFloat>(index)) * gap;
    return NSMakeRect(x, rect.origin.y + 4.0, w, h);
}
- (void)drawViewButtonsInRect:(NSRect)rect attrs:(NSDictionary*)attrs
{
    static NSString* labels[] = { @"TOP", @"SIDE", @"3/4" };
    s3g::clap_gui::Style style;
    for (int i = 0; i < 3; ++i) {
        s3g::clap_gui::drawHeaderButton([self viewButtonRect:i inRect:rect], rect, labels[i], i == _viewMode, attrs, style);
    }
}
- (void)drawZoomButtonsInRect:(NSRect)rect attrs:(NSDictionary*)attrs
{
    static NSString* labels[] = { @"-", @"+" };
    s3g::clap_gui::Style style;
    for (int i = 0; i < 2; ++i) {
        s3g::clap_gui::drawHeaderButton([self zoomButtonRect:i inRect:rect], rect, labels[i], false, attrs, style);
    }
}
- (void)setViewPreset:(int)mode
{
    _viewMode = mode;
    if (mode == 0) {
        _viewAzDeg = 90.0;
        _viewElDeg = 0.0;
    } else if (mode == 1) {
        _viewAzDeg = 90.0;
        _viewElDeg = 90.0;
    } else {
        _viewAzDeg = 35.0;
        _viewElDeg = 34.0;
    }
    [self storeViewState];
    [self setNeedsDisplay:YES];
}
- (CGFloat)viewScaleForRect:(NSRect)rect
{
    CGFloat layoutScale = 1.0;
    if (_plugin && _viewMode != 0 && _viewMode != 1) {
        auto* p = static_cast<Plugin*>(_plugin);
        auto snapshot = acquireDecoderSnapshot(*p);
        const auto layout = snapshot->params().layout;
        if (layout == s3g::AmbiSpeakerLayoutPreset::Cube8 || layout == s3g::AmbiSpeakerLayoutPreset::Cube17) {
            layoutScale = 0.82;
        }
    }
    return std::min(rect.size.width, rect.size.height) * 0.34 * layoutScale * std::clamp(_viewZoom, 0.55, 2.20);
}
- (NSPoint)projectWorldPoint:(s3g::Vec3)p rect:(NSRect)rect depth:(CGFloat*)depth
{
    const CGFloat cx = rect.origin.x + rect.size.width * 0.50;
    const CGFloat cy = rect.origin.y + rect.size.height * 0.54;
    const CGFloat scale = [self viewScaleForRect:rect];
    const float az = static_cast<float>(_viewAzDeg * M_PI / 180.0);
    const float el = static_cast<float>(_viewElDeg * M_PI / 180.0);
    const float ca = std::cos(az);
    const float sa = std::sin(az);
    const float ce = std::cos(el);
    const float se = std::sin(el);
    const float x1 = ca * p.x - sa * p.y;
    const float y1 = sa * p.x + ca * p.y;
    const float y2 = ce * y1 + se * p.z;
    const float z2 = -se * y1 + ce * p.z;
    if (depth) *depth = static_cast<CGFloat>(z2);
    return NSMakePoint(cx + static_cast<CGFloat>(x1) * scale, cy - static_cast<CGFloat>(y2) * scale);
}
- (void)addPolyhedronShellToPath:(NSBezierPath*)links dodecaShell:(BOOL)dodecaShell rect:(NSRect)rect
{
    if (!links) return;
    constexpr float phi = 1.61803398875f;
    constexpr float invPhi = 1.0f / phi;
    std::array<s3g::Vec3, 20> verts {};
    const uint32_t count = dodecaShell ? 20u : 12u;
    if (dodecaShell) {
        const float pts[20][3] {
            { 1, 1, 1 }, { 1, 1, -1 }, { 1, -1, 1 }, { 1, -1, -1 },
            { -1, 1, 1 }, { -1, 1, -1 }, { -1, -1, 1 }, { -1, -1, -1 },
            { 0, invPhi, phi }, { 0, invPhi, -phi }, { 0, -invPhi, phi }, { 0, -invPhi, -phi },
            { invPhi, phi, 0 }, { invPhi, -phi, 0 }, { -invPhi, phi, 0 }, { -invPhi, -phi, 0 },
            { phi, 0, invPhi }, { phi, 0, -invPhi }, { -phi, 0, invPhi }, { -phi, 0, -invPhi },
        };
        for (uint32_t i = 0; i < count; ++i) verts[i] = { pts[i][0], pts[i][1], pts[i][2] };
    } else {
        const float pts[12][3] {
            { 0, 1, phi }, { 0, -1, phi }, { 0, 1, -phi }, { 0, -1, -phi },
            { 1, phi, 0 }, { -1, phi, 0 }, { 1, -phi, 0 }, { -1, -phi, 0 },
            { phi, 0, 1 }, { -phi, 0, 1 }, { phi, 0, -1 }, { -phi, 0, -1 },
        };
        for (uint32_t i = 0; i < count; ++i) verts[i] = { pts[i][0], pts[i][1], pts[i][2] };
    }

    std::array<NSPoint, 20> points {};
    for (uint32_t i = 0; i < count; ++i) {
        const float d = std::sqrt(verts[i].x * verts[i].x + verts[i].y * verts[i].y + verts[i].z * verts[i].z);
        if (d > 0.000001f) {
            verts[i].x /= d;
            verts[i].y /= d;
            verts[i].z /= d;
        }
        points[i] = [self projectWorldPoint:verts[i] rect:rect depth:nil];
    }

    float minD2 = 999999.0f;
    for (uint32_t a = 0; a < count; ++a) {
        for (uint32_t b = a + 1u; b < count; ++b) {
            const float dx = verts[a].x - verts[b].x;
            const float dy = verts[a].y - verts[b].y;
            const float dz = verts[a].z - verts[b].z;
            const float d2 = dx * dx + dy * dy + dz * dz;
            if (d2 > 0.0001f) minD2 = std::min(minD2, d2);
        }
    }
    const float maxD2 = minD2 * 1.08f;
    for (uint32_t a = 0; a < count; ++a) {
        for (uint32_t b = a + 1u; b < count; ++b) {
            const float dx = verts[a].x - verts[b].x;
            const float dy = verts[a].y - verts[b].y;
            const float dz = verts[a].z - verts[b].z;
            const float d2 = dx * dx + dy * dy + dz * dz;
            if (d2 <= maxD2) {
                [links moveToPoint:points[a]];
                [links lineToPoint:points[b]];
            }
        }
    }
}
- (void)drawAllRadTopology:(NSRect)rect
                     attrs:(NSDictionary*)attrs
                   decoder:(const s3g::AmbiSpeakerDecoder&)decoder
                  speakers:(const std::array<s3g::AmbiSpeaker,
                                s3g::kAmbiSpeakerDecoderMaxSpeakers>&)speakers
{
    const auto& topology = decoder.allRadTopology();
    const uint32_t realSpeakerCount = std::min<uint32_t>(
        decoder.params().activeSpeakers, s3g::kAmbiSpeakerDecoderMaxSpeakers);
    const uint32_t nodeCount = std::min<uint32_t>(
        topology.nodeCount, static_cast<uint32_t>(topology.nodes.size()));

    auto pointForNode = [&](uint32_t index) {
        const auto& node = topology.nodes[index];
        s3g::Vec3 world = node.direction;
        if (node.kind == s3g::AllRadNodeKind::Real && node.speakerIndex < realSpeakerCount) {
            const float distance = speakers[node.speakerIndex].distance;
            world = {
                node.direction.x * distance,
                node.direction.y * distance,
                node.direction.z * distance
            };
        }
        return [self projectWorldPoint:world rect:rect depth:nil];
    };

    NSBezierPath* realEdges = [NSBezierPath bezierPath];
    NSBezierPath* supportEdges = [NSBezierPath bezierPath];
    const uint32_t edgeCount = std::min<uint32_t>(
        topology.edgeCount, static_cast<uint32_t>(topology.edges.size()));
    for (uint32_t i = 0; i < edgeCount; ++i) {
        const auto& edge = topology.edges[i];
        if (edge.a >= nodeCount || edge.b >= nodeCount || edge.a == edge.b) continue;
        const bool involvesSupport = topology.nodes[edge.a].kind != s3g::AllRadNodeKind::Real
            || topology.nodes[edge.b].kind != s3g::AllRadNodeKind::Real;
        NSBezierPath* path = involvesSupport ? supportEdges : realEdges;
        [path moveToPoint:pointForNode(edge.a)];
        [path lineToPoint:pointForNode(edge.b)];
    }

    [sdColor(0xd9c46a, 0.18) setStroke];
    [realEdges setLineWidth:0.65];
    [realEdges stroke];

    const CGFloat dash[] { 2.5, 2.5 };
    [supportEdges setLineDash:dash count:2 phase:0.0];
    [supportEdges setLineWidth:0.55];
    [sdColor(0xd9c46a, 0.13) setStroke];
    [supportEdges stroke];

    uint32_t topologyRealCount = 0u;
    uint32_t dropCount = 0u;
    uint32_t foldCount = 0u;
    NSBezierPath* dropPoints = [NSBezierPath bezierPath];
    NSBezierPath* foldPoints = [NSBezierPath bezierPath];
    for (uint32_t i = 0; i < nodeCount; ++i) {
        if (topology.nodes[i].kind == s3g::AllRadNodeKind::Real) {
            ++topologyRealCount;
            continue;
        }
        const NSPoint point = pointForNode(i);
        if (topology.nodes[i].kind == s3g::AllRadNodeKind::SupportDrop) {
            ++dropCount;
            [dropPoints appendBezierPathWithOvalInRect:
                NSMakeRect(point.x - 2.5, point.y - 2.5, 5.0, 5.0)];
        } else {
            ++foldCount;
            [foldPoints moveToPoint:NSMakePoint(point.x, point.y - 3.0)];
            [foldPoints lineToPoint:NSMakePoint(point.x + 3.0, point.y)];
            [foldPoints lineToPoint:NSMakePoint(point.x, point.y + 3.0)];
            [foldPoints lineToPoint:NSMakePoint(point.x - 3.0, point.y)];
            [foldPoints closePath];
        }
    }
    [dropPoints setLineWidth:0.75];
    [sdColor(0xe7a35a, 0.48) setStroke];
    [dropPoints stroke];
    [foldPoints setLineWidth:0.75];
    [sdColor(0x8fc3c9, 0.52) setStroke];
    [foldPoints stroke];

    NSString* dimension = @"INVALID";
    if (topology.valid) {
        if (topology.dimension == s3g::AllRadDimension::Ring2D) dimension = @"2D";
        else if (topology.dimension == s3g::AllRadDimension::Sphere3D) dimension = @"3D";
    }
    NSString* status = [NSString stringWithFormat:
        @"ALLRAD  %u REAL  +%u GAP  +%u FOLD  %@",
        topologyRealCount, dropCount, foldCount, dimension];
    [status drawAtPoint:NSMakePoint(rect.origin.x + 12.0, rect.origin.y + 9.0) withAttributes:attrs];
}
- (void)drawSpeakerField:(NSRect)rect attrs:(NSDictionary*)attrs small:(NSDictionary*)small style:(const s3g::clap_gui::Style&)style
{
    auto* p = static_cast<Plugin*>(_plugin);
    auto snapshot = acquireDecoderSnapshot(*p);
    const auto prm = visibleDecoderParams(
        *p, snapshot.decoder(), snapshot.runtimeMixer());
    [sdColor(0x111111) setFill]; NSRectFill(rect);
    [style.grid setStroke]; NSFrameRect(rect);

    const auto speakers = visibleDecoderSpeakers(
        *p, snapshot.decoder(), snapshot.runtimeMixer());
    const uint32_t n = std::min<uint32_t>(prm.activeSpeakers, s3g::kAmbiSpeakerDecoderMaxSpeakers);
    std::array<NSPoint, s3g::kAmbiSpeakerDecoderMaxSpeakers> points {};
    for (uint32_t i = 0; i < n; ++i) {
        const auto& sp = speakers[i];
        const s3g::Vec3 dir = s3g::directionFromAed(sp.azimuthDeg, sp.elevationDeg);
        const s3g::Vec3 world { dir.x * sp.distance, dir.y * sp.distance, dir.z * sp.distance };
        points[i] = [self projectWorldPoint:world rect:rect depth:nil];
        points[i].x = std::round(points[i].x);
        points[i].y = std::round(points[i].y);
    }
    const bool allRad = prm.mode == s3g::AmbiSpeakerDecoderMode::AllRad;
    if (allRad) {
        [self drawAllRadTopology:rect
                          attrs:small
                        decoder:snapshot.decoder()
                       speakers:speakers];
    } else {
        [sdColor(0x777777, 0.72) setStroke];
        NSBezierPath* links = [NSBezierPath bezierPath];
        auto edge = [&](uint32_t a, uint32_t b) {
            if (a >= n || b >= n) return;
            [links moveToPoint:points[a]];
            [links lineToPoint:points[b]];
        };
        auto ring = [&](uint32_t base, uint32_t count) {
            if (count < 2u) return;
            for (uint32_t i = 0; i < count; ++i) edge(base + i, base + ((i + 1u) % count));
        };
        const auto layout = prm.layout;
        if (layout == s3g::AmbiSpeakerLayoutPreset::Quad) {
            ring(0, 4);
        } else if (layout == s3g::AmbiSpeakerLayoutPreset::Cube8) {
            ring(0, 4);
            ring(4, 4);
            for (uint32_t i = 0; i < 4; ++i) edge(i, i + 4u);
        } else if (layout == s3g::AmbiSpeakerLayoutPreset::Cube17) {
            ring(0, 4);
            edge(4, 5); edge(5, 6);
            edge(6, 7); edge(7, 8);
            edge(8, 9); edge(9, 10);
            edge(10, 11); edge(11, 4);
            ring(12, 4);
            edge(0, 4); edge(1, 6); edge(2, 8); edge(3, 10);
            edge(4, 12); edge(6, 13); edge(8, 14); edge(10, 15);
            edge(12, 16); edge(13, 16); edge(14, 16); edge(15, 16);
        } else if (layout == s3g::AmbiSpeakerLayoutPreset::Cube41 || layout == s3g::AmbiSpeakerLayoutPreset::Lpac41) {
            ring(0, 16);
            ring(16, 12);
            ring(28, 8);
            ring(36, 4);
        } else if (layout == s3g::AmbiSpeakerLayoutPreset::Dome24 || layout == s3g::AmbiSpeakerLayoutPreset::Dome25 || layout == s3g::AmbiSpeakerLayoutPreset::Srst25) {
            ring(0, 12);
            ring(12, 8);
            ring(20, 4);
            for (uint32_t i = 0; i < 8; ++i) {
                const uint32_t lowerA = (i * 3u) / 2u;
                const uint32_t lowerB = (lowerA + 1u) % 12u;
                edge(12u + i, lowerA);
                edge(12u + i, lowerB);
            }
            for (uint32_t i = 0; i < 4; ++i) {
                edge(20u + i, 12u + i * 2u);
                edge(20u + i, 12u + ((i * 2u + 1u) % 8u));
            }
            if (layout == s3g::AmbiSpeakerLayoutPreset::Dome25 || layout == s3g::AmbiSpeakerLayoutPreset::Srst25) {
                for (uint32_t i = 0; i < 4; ++i) edge(24, 20u + i);
            }
        } else if (layout == s3g::AmbiSpeakerLayoutPreset::QuadOverhead6) {
            ring(0, 4);
            edge(4, 0); edge(4, 3);
            edge(5, 1); edge(5, 2);
            edge(4, 5);
        } else if (layout == s3g::AmbiSpeakerLayoutPreset::OctophonicRing) {
            ring(0, 8);
        } else if (layout == s3g::AmbiSpeakerLayoutPreset::Dodeca12) {
            [self addPolyhedronShellToPath:links dodecaShell:YES rect:rect];
        } else if (layout == s3g::AmbiSpeakerLayoutPreset::Icosahedron20) {
            [self addPolyhedronShellToPath:links dodecaShell:NO rect:rect];
        }
        [links setLineWidth:1.0];
        [links stroke];
    }

    for (uint32_t i = 0; i < n; ++i) {
        const auto& sp = speakers[i];
        const bool selected = _hasSpeakerSelection && i == prm.selectedSpeaker;
        const CGFloat r = selected ? 6.0 : 4.8;
        [speakerColorFromAed(sp.azimuthDeg, sp.elevationDeg, sp.distance, selected) setFill];
        NSRectFill(NSMakeRect(points[i].x - r, points[i].y - r, r * 2.0, r * 2.0));
        [sdColor(0x050505, 0.90) setStroke];
        NSFrameRect(NSMakeRect(points[i].x - r, points[i].y - r, r * 2.0, r * 2.0));
        if (selected) {
            [sdColor(0xf2f2f2) setStroke];
            NSFrameRect(NSMakeRect(points[i].x - 10.0, points[i].y - 10.0, 20.0, 20.0));
        }
        NSString* label = [NSString stringWithFormat:@"%u", i + 1u];
        NSDictionary* idAttrs = @{ NSForegroundColorAttributeName:selected ? sdColor(0xc8c8c8) : sdColor(0x151515),
                                   NSFontAttributeName:s3g::clap_gui::uiFont(7.5) };
        NSSize labelSize = [label sizeWithAttributes:idAttrs];
        [label drawAtPoint:NSMakePoint(points[i].x - labelSize.width * 0.5,
                                       points[i].y - labelSize.height * 0.5 - 0.5)
            withAttributes:idAttrs];
    }
    NSString* viewText = _viewMode == 0 ? @"TOP VIEW   0 front/top  -90 right  +90 left"
        : (_viewMode == 1 ? @"SIDE VIEW   +90 elevation up" : @"3/4 VIEW   drag blank space to rotate");
    [viewText drawAtPoint:NSMakePoint(rect.origin.x + 12, rect.origin.y + rect.size.height - 23) withAttributes:attrs];
}
- (void)drawDecodeMap:(NSRect)rect attrs:(NSDictionary*)attrs small:(NSDictionary*)small style:(const s3g::clap_gui::Style&)style
{
    auto* p = static_cast<Plugin*>(_plugin);
    auto snapshot = acquireDecoderSnapshot(*p);
    const auto prm = visibleDecoderParams(
        *p, snapshot.decoder(), snapshot.runtimeMixer());
    const auto speakers = visibleDecoderSpeakers(
        *p, snapshot.decoder(), snapshot.runtimeMixer());
    const auto& matrix = snapshot->matrix();
    const uint32_t n = std::min<uint32_t>(prm.activeSpeakers, s3g::kAmbiSpeakerDecoderMaxSpeakers);
    const uint32_t ambiCh = s3g::ambiChannelsForOrder(prm.order);
    [sdColor(0x111111) setFill]; NSRectFill(rect);
    [style.grid setStroke]; NSFrameRect(rect);

    std::array<NSPoint, s3g::kAmbiSpeakerDecoderMaxSpeakers> points {};
    for (uint32_t i = 0; i < n; ++i) {
        const auto& sp = speakers[i];
        const s3g::Vec3 dir = s3g::directionFromAed(sp.azimuthDeg, sp.elevationDeg);
        const s3g::Vec3 world { dir.x * sp.distance, dir.y * sp.distance, dir.z * sp.distance };
        points[i] = [self projectWorldPoint:world rect:rect depth:nil];
    }

    const bool allRad = prm.mode == s3g::AmbiSpeakerDecoderMode::AllRad;
    NSBezierPath* links = [NSBezierPath bezierPath];
    if (allRad) {
        [self drawAllRadTopology:rect
                          attrs:small
                        decoder:snapshot.decoder()
                       speakers:speakers];
    } else {
        auto edge = [&](uint32_t a, uint32_t b) {
            if (a >= n || b >= n) return;
            [links moveToPoint:points[a]];
            [links lineToPoint:points[b]];
        };
        auto ring = [&](uint32_t base, uint32_t count) {
            if (count < 2u || base >= n) return;
            count = std::min<uint32_t>(count, n - base);
            for (uint32_t i = 0; i < count; ++i) edge(base + i, base + ((i + 1u) % count));
        };
        const auto layout = prm.layout;
        if (layout == s3g::AmbiSpeakerLayoutPreset::Quad) {
            ring(0, 4);
        } else if (layout == s3g::AmbiSpeakerLayoutPreset::Cube8) {
            ring(0, 4); ring(4, 4);
            for (uint32_t i = 0; i < 4; ++i) edge(i, i + 4u);
        } else if (layout == s3g::AmbiSpeakerLayoutPreset::Cube17) {
            ring(0, 4);
            edge(4, 5); edge(5, 6); edge(6, 7); edge(7, 8);
            edge(8, 9); edge(9, 10); edge(10, 11); edge(11, 4);
            ring(12, 4);
            edge(0, 4); edge(1, 6); edge(2, 8); edge(3, 10);
            edge(4, 12); edge(6, 13); edge(8, 14); edge(10, 15);
            edge(12, 16); edge(13, 16); edge(14, 16); edge(15, 16);
        } else if (layout == s3g::AmbiSpeakerLayoutPreset::Cube41 || layout == s3g::AmbiSpeakerLayoutPreset::Lpac41) {
            ring(0, 16);
            ring(16, 12);
            ring(28, 8);
            ring(36, 4);
        } else if (layout == s3g::AmbiSpeakerLayoutPreset::Dome24 || layout == s3g::AmbiSpeakerLayoutPreset::Dome25 || layout == s3g::AmbiSpeakerLayoutPreset::Srst25) {
            ring(0, 12); ring(12, 8); ring(20, 4);
            for (uint32_t i = 0; i < 8; ++i) {
                const uint32_t lowerA = (i * 3u) / 2u;
                edge(12u + i, lowerA);
                edge(12u + i, (lowerA + 1u) % 12u);
            }
            for (uint32_t i = 0; i < 4; ++i) {
                edge(20u + i, 12u + i * 2u);
                edge(20u + i, 12u + ((i * 2u + 1u) % 8u));
            }
            if (layout == s3g::AmbiSpeakerLayoutPreset::Dome25 || layout == s3g::AmbiSpeakerLayoutPreset::Srst25) {
                for (uint32_t i = 0; i < 4; ++i) edge(24, 20u + i);
            }
        } else if (layout == s3g::AmbiSpeakerLayoutPreset::QuadOverhead6) {
            ring(0, 4);
            edge(4, 0); edge(4, 3); edge(5, 1); edge(5, 2); edge(4, 5);
        } else if (layout == s3g::AmbiSpeakerLayoutPreset::OctophonicRing) {
            ring(0, 8);
        } else if (layout == s3g::AmbiSpeakerLayoutPreset::Dodeca12) {
            [self addPolyhedronShellToPath:links dodecaShell:YES rect:rect];
        } else if (layout == s3g::AmbiSpeakerLayoutPreset::Icosahedron20) {
            [self addPolyhedronShellToPath:links dodecaShell:NO rect:rect];
        }
    }
    const uint32_t probeIndex = std::min<uint32_t>(prm.selectedSpeaker, n > 0u ? n - 1u : 0u);
    const auto& probeSpeaker = speakers[probeIndex];
    const auto probeDirection =
        s3g::directionFromAed(probeSpeaker.azimuthDeg, probeSpeaker.elevationDeg);
    const auto basis = allRad
        ? s3g::acnSn3dBasis7Canonical(probeDirection)
        : s3g::acnSn3dBasis7(probeDirection);
    std::array<float, s3g::kAmbiSpeakerDecoderMaxSpeakers> energy {};
    float maxEnergy = 0.000001f;
    for (uint32_t sp = 0; sp < n; ++sp) {
        float value = 0.0f;
        for (uint32_t ch = 0; ch < ambiCh; ++ch) {
            value += basis[ch] * matrix[sp][ch];
        }
        value *= speakers[sp].enabled ? speakers[sp].gain : 0.0f;
        energy[sp] = std::fabs(value);
        maxEnergy = std::max(maxEnergy, energy[sp]);
    }

    for (uint32_t i = 0; i < n; ++i) {
        const CGFloat e = std::pow(std::clamp<CGFloat>(energy[i] / maxEnergy, 0.0, 1.0), 0.70);
        const CGFloat r = 7.0;
        const NSRect square = NSMakeRect(points[i].x - r, points[i].y - r, r * 2.0, r * 2.0);
        [sdColor(0x151515) setFill]; NSRectFill(square);
        [s3g::clap_gui::heatColor(e, speakers[i].enabled ? 0.96 : 0.24) setFill];
        NSRectFill(NSInsetRect(square, 2.0, 2.0));
        [sdColor(i == probeIndex ? 0xf5f5f5 : (e > 0.92 ? 0xf0f0f0 : 0x777777)) setStroke];
        NSFrameRect(square);
        if (i == probeIndex) {
            [sdColor(0xf5f5f5, 0.92) setStroke];
            NSFrameRect(NSInsetRect(square, -4.0, -4.0));
            [NSBezierPath strokeLineFromPoint:NSMakePoint(NSMidX(square) - 13.0, NSMidY(square))
                                      toPoint:NSMakePoint(NSMidX(square) + 13.0, NSMidY(square))];
            [NSBezierPath strokeLineFromPoint:NSMakePoint(NSMidX(square), NSMidY(square) - 13.0)
                                      toPoint:NSMakePoint(NSMidX(square), NSMidY(square) + 13.0)];
        }
        NSString* label = [NSString stringWithFormat:@"%u", i + 1u];
        NSSize labelSize = [label sizeWithAttributes:small];
        [label drawAtPoint:NSMakePoint(NSMidX(square) - labelSize.width * 0.5,
                                       NSMidY(square) - labelSize.height * 0.5 - 0.5)
            withAttributes:small];
    }
    if (!allRad) {
        [sdColor(0xd0d0d0, 0.76) setStroke];
        [links setLineWidth:1.15];
        [links stroke];
    }
    [[NSString stringWithFormat:@"PROBE S%u   AZ %+.1f  EL %+.1f   %uOA",
        probeIndex + 1u,
        probeSpeaker.azimuthDeg,
        probeSpeaker.elevationDeg,
        prm.order]
        drawAtPoint:NSMakePoint(rect.origin.x + 12, rect.origin.y + rect.size.height - 23)
        withAttributes:attrs];
}
- (NSRect)mixerOutputTrackRect:(NSRect)rect
{
    return NSMakeRect(rect.origin.x + 88.0, rect.origin.y + 42.0, rect.size.width - 178.0, 9.0);
}
- (NSRect)mixerSpeakerRect:(uint32_t)index inRect:(NSRect)rect
{
    const uint32_t slot = index % 16u;
    const CGFloat laneW = 34.0;
    const CGFloat x = rect.origin.x + 12.0 + static_cast<CGFloat>(slot) * laneW;
    return NSMakeRect(x + 8.0, rect.origin.y + 128.0, 12.0, rect.size.height - 196.0);
}
- (NSRect)mixerGainFieldRect:(uint32_t)slot inRect:(NSRect)rect
{
    const NSRect fader = [self mixerSpeakerRect:slot inRect:rect];
    const CGFloat w = 30.0;
    return NSMakeRect(NSMidX(fader) - w * 0.5, NSMaxY(fader) + 7.0, w, 15.0);
}
- (NSRect)mixerMuteRect:(uint32_t)slot inRect:(NSRect)rect
{
    const CGFloat laneW = 34.0;
    const CGFloat x = rect.origin.x + 12.0 + static_cast<CGFloat>(slot) * laneW;
    return NSMakeRect(x + 1.0, NSMaxY(rect) - 42.0, 14.0, 14.0);
}
- (NSRect)mixerSoloRect:(uint32_t)slot inRect:(NSRect)rect
{
    const CGFloat laneW = 34.0;
    const CGFloat x = rect.origin.x + 12.0 + static_cast<CGFloat>(slot) * laneW;
    return NSMakeRect(x + 17.0, NSMaxY(rect) - 42.0, 14.0, 14.0);
}
- (NSRect)mixerPageButtonRect:(int)index inRect:(NSRect)rect
{
    const CGFloat w = 54.0;
    const CGFloat h = 13.0;
    const CGFloat gap = 5.0;
    const int count = 4;
    return NSMakeRect(NSMaxX(rect) - 18.0 - static_cast<CGFloat>(count - index) * w - static_cast<CGFloat>(count - index - 1) * gap,
                      rect.origin.y + 17.0,
                      w,
                      h);
}
- (void)drawMixerPageButtonsInRect:(NSRect)rect attrs:(NSDictionary*)attrs
{
    static NSString* labels[] = { @"1-16", @"17-32", @"33-48", @"49-64" };
    auto* p = static_cast<Plugin*>(_plugin);
    uint32_t n = 0u;
    if (p) {
        auto snapshot = acquireDecoderSnapshot(*p);
        n = std::min<uint32_t>(
            snapshot->params().activeSpeakers, s3g::kAmbiSpeakerDecoderMaxSpeakers);
    }
    const int pageCount = static_cast<int>((n + 15u) / 16u);
    if (pageCount <= 1) return;
    for (int i = 0; i < pageCount; ++i) {
        NSRect button = [self mixerPageButtonRect:i inRect:rect];
        [sdColor(i == _mixerPage ? 0x303030 : 0x151515) setFill];
        NSRectFill(button);
        [sdColor(i == _mixerPage ? 0xd1d1d1 : 0x555555) setStroke];
        NSFrameRect(button);
        NSSize size = [labels[i] sizeWithAttributes:attrs];
        [labels[i] drawAtPoint:NSMakePoint(button.origin.x + (button.size.width - size.width) * 0.5,
                                           button.origin.y + (button.size.height - size.height) * 0.5 - 0.5)
                 withAttributes:attrs];
    }
}
- (void)drawSpeakerMixer:(NSRect)rect attrs:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style
{
    auto* p = static_cast<Plugin*>(_plugin);
    auto snapshot = acquireDecoderSnapshot(*p);
    const auto speakers = visibleDecoderSpeakers(
        *p, snapshot.decoder(), snapshot.runtimeMixer());
    const auto params = visibleDecoderParams(
        *p, snapshot.decoder(), snapshot.runtimeMixer());
    const uint32_t n = std::min<uint32_t>(params.activeSpeakers, s3g::kAmbiSpeakerDecoderMaxSpeakers);
    const int pageCount = std::max<int>(1, static_cast<int>((n + 15u) / 16u));
    _mixerPage = std::clamp(_mixerPage, 0, pageCount - 1);
    const uint32_t pageStart = static_cast<uint32_t>(_mixerPage) * 16u;
    [self drawMixerPageButtonsInRect:rect attrs:attrs];
    s3g::clap_gui::drawSlider(@"OUT",
                               [NSString stringWithFormat:@"%+.1f dB", params.outputGainDb],
                               (params.outputGainDb + 60.0f) / 72.0f,
                               rect.origin.y + 36.0,
                               attrs,
                               attrs,
                               style,
                               rect.origin.x + 18.0,
                               rect.origin.x + 88.0,
                               rect.origin.x + rect.size.width - 70.0,
                               rect.size.width - 178.0);
    bool anySolo = false;
    for (uint32_t i = 0; i < n; ++i) anySolo = anySolo || speakers[i].solo;
    const uint32_t pageEnd = std::min<uint32_t>(n, pageStart + 16u);
    for (uint32_t i = pageStart; i < pageEnd; ++i) {
        const auto& sp = speakers[i];
        const bool selected = _hasSpeakerSelection && i == params.selectedSpeaker;
        const bool audible = sp.enabled && (!anySolo || sp.solo);
        const uint32_t pageSlot = i - pageStart;
        const CGFloat laneW = 34.0;
        const CGFloat laneX = rect.origin.x + 12.0 + static_cast<CGFloat>(pageSlot) * laneW;
        if (selected) {
            [sdColor(0x242424) setFill];
            NSRectFill(NSMakeRect(laneX - 2.0, rect.origin.y + 96.0, laneW - 3.0, rect.size.height - 102.0));
            [sdColor(0x777777) setStroke];
            NSFrameRect(NSMakeRect(laneX - 2.0, rect.origin.y + 96.0, laneW - 3.0, rect.size.height - 102.0));
        }
        const NSRect slot = [self mixerSpeakerRect:i inRect:rect];
        NSString* label = [NSString stringWithFormat:@"%u", i + 1u];
        [label drawAtPoint:NSMakePoint(laneX + (i < 9 ? 10.0 : 7.0), rect.origin.y + 97.0) withAttributes:attrs];
        [sdColor(audible ? 0x181818 : 0x0d0d0d) setFill];
        NSRectFill(slot);
        [sdColor(audible ? 0x545454 : 0x333333) setStroke];
        NSFrameRect(slot);
        const CGFloat norm = std::clamp<CGFloat>(sp.gain / 2.0f, 0.0, 1.0);
        NSRect fill = NSInsetRect(slot, 2.0, 2.0);
        const CGFloat fullH = fill.size.height;
        fill.origin.y += fullH * (1.0 - norm);
        fill.size.height = std::max<CGFloat>(1.0, fullH * norm);
        [speakerColorFromAed(sp.azimuthDeg, sp.elevationDeg, sp.distance, selected) setFill];
        NSRectFill(fill);
        [sdColor(selected ? 0xf2f2f2 : 0x9a9a9a) setFill];
        NSRectFill(NSMakeRect(slot.origin.x - 2.0,
                              slot.origin.y + slot.size.height * (1.0 - norm) - 1.0,
                              slot.size.width + 4.0,
                              3.0));
        NSRect mute = [self mixerMuteRect:pageSlot inRect:rect];
        [sdColor(sp.enabled ? 0x151515 : 0x3a3a3a) setFill]; NSRectFill(mute);
        [sdColor(sp.enabled ? 0x5a5a5a : 0xd1d1d1) setStroke]; NSFrameRect(mute);
        [@"M" drawAtPoint:NSMakePoint(mute.origin.x + 3.0, mute.origin.y + 2.0) withAttributes:attrs];
        NSRect solo = [self mixerSoloRect:pageSlot inRect:rect];
        [sdColor(sp.solo ? 0xd1d1d1 : 0x151515) setFill]; NSRectFill(solo);
        [sdColor(sp.solo ? 0xf2f2f2 : 0x5a5a5a) setStroke]; NSFrameRect(solo);
        NSDictionary* soloAttrs = sp.solo
            ? @{ NSForegroundColorAttributeName:sdColor(0x111111), NSFontAttributeName:[attrs objectForKey:NSFontAttributeName] }
            : attrs;
        [@"S" drawAtPoint:NSMakePoint(solo.origin.x + 3.0, solo.origin.y + 2.0) withAttributes:soloAttrs];
    }
    [self updateMixerGainFieldsForRect:rect];
}
- (void)updateMixerSpeakerGain:(NSPoint)point inRect:(NSRect)rect
{
    if (_dragMixerSpeaker < 0) return;
    const uint32_t index = static_cast<uint32_t>(_dragMixerSpeaker);
    NSRect track = [self mixerSpeakerRect:index inRect:rect];
    const CGFloat norm = std::clamp((NSMaxY(track) - point.y) / track.size.height, 0.0, 1.0);
    auto* p = static_cast<Plugin*>(_plugin);
    applyParam(*p, kSelectedSpeakerParamId, static_cast<double>(index + 1u));
    setSpeakerGainNonRealtime(*p, index, norm * 2.0);
    _hasSpeakerSelection = YES;
    [self setNeedsDisplay:YES];
}
- (void)updateMixerOutput:(NSPoint)point inRect:(NSRect)rect
{
    NSRect track = [self mixerOutputTrackRect:rect];
    const CGFloat norm = std::clamp((point.x - track.origin.x) / track.size.width, 0.0, 1.0);
    auto* p = static_cast<Plugin*>(_plugin);
    applyParam(*p, kOutputParamId, -60.0 + norm * 72.0);
    [self setNeedsDisplay:YES];
}
- (void)toggleMixerSpeakerMute:(uint32_t)index
{
    auto* p = static_cast<Plugin*>(_plugin);
    auto snapshot = acquireDecoderSnapshot(*p);
    const uint32_t n = std::min<uint32_t>(
        snapshot->params().activeSpeakers, s3g::kAmbiSpeakerDecoderMaxSpeakers);
    if (index >= n) return;
    const auto speakers = visibleDecoderSpeakers(
        *p, snapshot.decoder(), snapshot.runtimeMixer());
    const bool enabled = speakers[index].enabled;
    setSpeakerEnabledNonRealtime(*p, index, !enabled);
    _hasSpeakerSelection = YES;
    [self setNeedsDisplay:YES];
}
- (void)toggleMixerSpeakerSolo:(uint32_t)index
{
    auto* p = static_cast<Plugin*>(_plugin);
    auto snapshot = acquireDecoderSnapshot(*p);
    const uint32_t n = std::min<uint32_t>(
        snapshot->params().activeSpeakers, s3g::kAmbiSpeakerDecoderMaxSpeakers);
    if (index >= n) return;
    const auto speakers = visibleDecoderSpeakers(
        *p, snapshot.decoder(), snapshot.runtimeMixer());
    const bool solo = speakers[index].solo;
    setSpeakerSoloNonRealtime(*p, index, !solo);
    _hasSpeakerSelection = YES;
    [self setNeedsDisplay:YES];
}
- (uint32_t)hitSpeakerAt:(NSPoint)pt inRect:(NSRect)rect found:(BOOL*)found
{
    auto* p = static_cast<Plugin*>(_plugin);
    auto snapshot = acquireDecoderSnapshot(*p);
    const auto prm = visibleDecoderParams(
        *p, snapshot.decoder(), snapshot.runtimeMixer());
    const auto speakers = visibleDecoderSpeakers(
        *p, snapshot.decoder(), snapshot.runtimeMixer());
    const uint32_t n = std::min<uint32_t>(prm.activeSpeakers, s3g::kAmbiSpeakerDecoderMaxSpeakers);
    uint32_t best = prm.selectedSpeaker;
    CGFloat bestD = 999999.0;
    for (uint32_t i = 0; i < n; ++i) {
        const auto& sp = speakers[i];
        const s3g::Vec3 dir = s3g::directionFromAed(sp.azimuthDeg, sp.elevationDeg);
        const s3g::Vec3 world { dir.x * sp.distance, dir.y * sp.distance, dir.z * sp.distance };
        const NSPoint spPt = [self projectWorldPoint:world rect:rect depth:nil];
        const CGFloat d = std::hypot(pt.x - spPt.x, pt.y - spPt.y);
        if (d < bestD) { bestD = d; best = i; }
    }
    if (found) *found = bestD < 18.0;
    return best;
}
- (void)drawRect:(NSRect)dirty
{
    (void)dirty;
    auto* p = static_cast<Plugin*>(_plugin);
    s3g::clap_gui::Style style;
    [style.bg setFill]; NSRectFill([self bounds]);
    NSDictionary* small = s3g::clap_gui::softValueAttrs();
    NSDictionary* lab = s3g::clap_gui::softLabelAttrs();
    NSDictionary* titleAttrs = s3g::clap_gui::softTitleAttrs();
    const float pk = p->outputPeak.load(std::memory_order_relaxed);
    s3g::clap_gui::drawDecoderTitleBand(
        @"s3g AMBI DECODER SPEAKER",
        [NSString stringWithUTF8String:_titlePresetName],
        s3g::clap_gui::peakDbText(pk),
        s3g::clap_gui::encoderTitleBand(900.0, 620.0),
        titleAttrs, lab, small, style);

    s3g::clap_gui::drawPanelFrame(18, 42, 596, 556, style);
    s3g::clap_gui::drawPanelHeader(_rightPage == 0 ? @"SPEAKER FIELD" : (_rightPage == 1 ? @"SPEAKER MIXER" : @"DECODE MAP"), true, 18, 42, 596, 21, lab, style);
    [self drawFieldPageButtonsInRect:NSMakeRect(18, 42, 596, 556) attrs:small];
    if (_rightPage == 0 || _rightPage == 2) {
        for (int i = 0; i < 16; ++i) [_gainFields[i] setHidden:YES];
        [self drawZoomButtonsInRect:NSMakeRect(18, 42, 596, 556) attrs:small];
        [self drawViewButtonsInRect:NSMakeRect(18, 42, 596, 556) attrs:small];
        if (_rightPage == 0) {
            [self drawSpeakerField:NSMakeRect(34, 76, 564, 506) attrs:small small:small style:style];
        } else {
            [self drawDecodeMap:NSMakeRect(34, 76, 564, 506) attrs:small small:small style:style];
        }
    } else {
        [self drawSpeakerMixer:NSMakeRect(34, 76, 564, 506) attrs:small style:style];
    }

    const NSRect lowerPanel = NSMakeRect(630, 330, 250, 132);
    s3g::clap_gui::drawPanelFrame(630, 42, 250, 54, style);
    s3g::clap_gui::drawPanelHeader(@"OUTPUT", true, 630, 42, 250, 21, lab, style);
    s3g::clap_gui::drawPanelFrame(630, 108, 250, 210, style);
    s3g::clap_gui::drawPanelHeader(@"DECODER", true, 630, 108, 250, 21, lab, style);
    s3g::clap_gui::drawPanelFrame(lowerPanel.origin.x, lowerPanel.origin.y, lowerPanel.size.width, lowerPanel.size.height, style);
    s3g::clap_gui::drawPanelHeader(@"SPEAKER", true, lowerPanel.origin.x, lowerPanel.origin.y, lowerPanel.size.width, 21, lab, style);

    auto decoderSnapshot = acquireDecoderSnapshot(*p);
    const auto prm = visibleDecoderParams(
        *p, decoderSnapshot.decoder(), decoderSnapshot.runtimeMixer());
    [self drawSlider:@"OUT" value:[NSString stringWithFormat:@"%+.1f dB", prm.outputGainDb] norm:(prm.outputGainDb + 60.0f) / 72.0f y:78 attrs:small style:style];
    [self drawMenu:@"LAYOUT" value:[NSString stringWithUTF8String:layoutName(static_cast<uint32_t>(prm.layout))] y:144 attrs:small style:style];
    [self drawMenu:@"MODE" value:[NSString stringWithUTF8String:modeName(static_cast<uint32_t>(prm.mode))] y:170 attrs:small style:style];
    [self drawMenu:@"ORDER" value:[NSString stringWithFormat:@"%uOA", prm.order] y:196 attrs:small style:style];
    [self drawMenu:@"WGT" value:[NSString stringWithUTF8String:weightingName(static_cast<uint32_t>(prm.weighting))] y:222 attrs:small style:style];
    [self drawMenu:@"FIELD" value:[NSString stringWithUTF8String:customFieldName(static_cast<uint32_t>(prm.customField))] y:248 attrs:small style:style];
    [self drawSlider:@"COUNT" value:[NSString stringWithFormat:@"%u", prm.activeSpeakers] norm:(prm.activeSpeakers - 2.0) / 62.0 y:274 attrs:small style:style];
    [self drawSlider:@"WID" value:[NSString stringWithFormat:@"%.2f", prm.width] norm:prm.width / 1.50f y:300 attrs:small style:style];

    const CGFloat selectedNorm = prm.activeSpeakers > 1u
        ? static_cast<CGFloat>(prm.selectedSpeaker) / static_cast<CGFloat>(prm.activeSpeakers - 1u)
        : 0.0;
    [self drawSlider:@"SEL" value:[NSString stringWithFormat:@"%u", prm.selectedSpeaker + 1u] norm:selectedNorm y:366 attrs:small style:style];
    [self drawSlider:@"AZ" value:@"" norm:(prm.selectedAzimuthDeg + 180.0f) / 360.0f y:392 attrs:small style:style];
    [self drawSlider:@"EL" value:@"" norm:(prm.selectedElevationDeg + 90.0f) / 180.0f y:418 attrs:small style:style];
    [self drawSlider:@"DST" value:@"" norm:(prm.selectedDistance - 0.15f) / 1.85f y:444 attrs:small style:style];
    [self updateValueFields];
    [self drawOpenMenu:small];
}
- (void)updateSlider:(NSPoint)point
{
    auto* p = static_cast<Plugin*>(_plugin);
    auto snapshot = acquireDecoderSnapshot(*p);
    const auto params = visibleDecoderParams(
        *p, snapshot.decoder(), snapshot.runtimeMixer());
    const double n = std::clamp((point.x - 738.0) / 82.0, 0.0, 1.0);
    switch (_dragSlider) {
    case 4: applyParam(*p, kActiveSpeakersParamId, 2.0 + n * 62.0); break;
    case 6: applyParam(*p, kWidthParamId, n * 1.50); break;
    case 7: applyParam(*p, kSelectedSpeakerParamId, 1.0 + n * static_cast<double>(std::max<uint32_t>(1u, params.activeSpeakers) - 1u)); break;
    case 8: applyParam(*p, kAzimuthParamId, -180.0 + n * 360.0); break;
    case 9: applyParam(*p, kElevationParamId, -90.0 + n * 180.0); break;
    case 10: applyParam(*p, kDistanceParamId, 0.15 + n * 1.85); break;
    case 11: applyParam(*p, kSpeakerGainParamId, n * 2.0); break;
    case 13: applyParam(*p, kOutputParamId, -60.0 + n * 72.0); break;
    default: break;
    }
    [self setNeedsDisplay:YES];
}
- (void)mouseDown:(NSEvent*)event
{
    NSPoint pt = [self convertPoint:[event locationInWindow] fromView:nil];
    auto* p = static_cast<Plugin*>(_plugin);
    const auto titleBand = s3g::clap_gui::encoderTitleBand(900.0, 620.0);
    if (NSPointInRect(pt, s3g::clap_gui::cocoaRect(titleBand.presetMenu))) {
        applyParam(*p, kLayoutParamId,
            static_cast<double>(s3g::AmbiSpeakerLayoutPreset::Sphere24));
        applyParam(*p, kModeParamId, 1.0);
        applyParam(*p, kOrderParamId, 3.0);
        applyParam(*p, kWeightingParamId, 1.0);
        applyParam(*p, kCustomFieldParamId, 0.0);
        applyParam(*p, kActiveSpeakersParamId, 24.0);
        applyParam(*p, kWidthParamId, 1.0);
        applyParam(*p, kOutputParamId, 0.0);
        std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s", "INIT");
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(pt, s3g::clap_gui::cocoaRect(titleBand.loadButton))) {
        NSString* name = nil;
        if (s3g::clap_gui::loadPluginStatePreset(
                &p->plugin, @"Ambi Decoder Speaker", &name)) {
            std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s",
                name ? [name UTF8String] : "CUSTOM");
            [self updateValueFields];
            [self setNeedsDisplay:YES];
        } else {
            NSBeep();
        }
        return;
    }
    if (NSPointInRect(pt, s3g::clap_gui::cocoaRect(titleBand.saveButton))) {
        NSString* name = nil;
        if (s3g::clap_gui::savePluginStatePreset(
                &p->plugin, @"Ambi Decoder Speaker", &name)) {
            std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s",
                name ? [name UTF8String] : "CUSTOM");
            [self setNeedsDisplay:YES];
        } else {
            NSBeep();
        }
        return;
    }
    auto decoderSnapshot = acquireDecoderSnapshot(*p);
    const auto decoderParams = visibleDecoderParams(
        *p, decoderSnapshot.decoder(), decoderSnapshot.runtimeMixer());
    const BOOL textFieldHit = NSPointInRect(pt, [_azField frame])
        || NSPointInRect(pt, [_elField frame])
        || NSPointInRect(pt, [_distField frame]);
    BOOL mixerFieldHit = NO;
    for (int i = 0; i < 16; ++i) {
        if (![_gainFields[i] isHidden] && NSPointInRect(pt, [_gainFields[i] frame])) {
            mixerFieldHit = YES;
            break;
        }
    }
    if (!textFieldHit && !mixerFieldHit && [[self window] firstResponder] != self) {
        [[self window] makeFirstResponder:self];
    }
    if (_openMenu > 0) {
        const CGFloat itemH = 18.0;
        const CGFloat menuW = _openMenu == 1 ? 150.0 : 124.0;
        const NSRect menuRect = NSMakeRect(_menuOrigin.x, _menuOrigin.y, menuW, itemH * static_cast<CGFloat>(_menuItemCount));
        if (NSPointInRect(pt, menuRect)) {
            const uint32_t index = std::min<uint32_t>(_menuItemCount - 1u,
                static_cast<uint32_t>((pt.y - _menuOrigin.y) / itemH));
            switch (_openMenu) {
            case 1: applyParam(*p, kLayoutParamId, layoutPresetForMenuIndex(index)); break;
            case 2: applyParam(*p, kModeParamId, index); break;
            case 3: applyParam(*p, kOrderParamId, index + 1u); break;
            case 4: applyParam(*p, kWeightingParamId, index); break;
            case 5: applyParam(*p, kCustomFieldParamId, index); break;
            default: break;
            }
            _openMenu = 0;
            _hoverMenuItem = -1;
            _menuItemCount = 0;
            [self setNeedsDisplay:YES];
            return;
        }
        _openMenu = 0;
        _hoverMenuItem = -1;
        _menuItemCount = 0;
        [self setNeedsDisplay:YES];
    }
    auto openMenu = [&](int menu, uint32_t count, CGFloat preferredY) {
        const CGFloat itemH = 18.0;
        _openMenu = menu;
        _hoverMenuItem = -1;
        _menuItemCount = count;
        _menuOrigin = NSMakePoint(738.0, std::clamp(preferredY, 28.0, 616.0 - itemH * static_cast<CGFloat>(count)));
        [self setNeedsDisplay:YES];
    };
    const NSRect fieldPanel = NSMakeRect(18, 42, 596, 556);
    const NSRect fieldRect = NSMakeRect(34, 76, 564, 506);
    const NSRect mixerRect = NSMakeRect(34, 76, 564, 506);
    if (_rightPage == 1 && NSPointInRect(pt, mixerRect)) {
        const uint32_t n = std::min<uint32_t>(
            decoderParams.activeSpeakers, s3g::kAmbiSpeakerDecoderMaxSpeakers);
        const int pageCount = static_cast<int>((n + 15u) / 16u);
        if (pageCount > 1) {
            for (int i = 0; i < pageCount; ++i) {
                if (NSPointInRect(pt, [self mixerPageButtonRect:i inRect:mixerRect])) {
                    _mixerPage = i;
                    _dragMixerSpeaker = -1;
                    _dragMixerOutput = NO;
                    [self updateMixerGainFieldsForRect:mixerRect];
                    [self setNeedsDisplay:YES];
                    return;
                }
            }
        }
        if (NSPointInRect(pt, NSInsetRect([self mixerOutputTrackRect:mixerRect], -8.0, -8.0))) {
            double defaultValue = 0.0;
            if (s3g::clap_gui::sliderDoubleClickDefault(
                    event, &p->plugin, kOutputParamId, &defaultValue)) {
                applyParam(*p, kOutputParamId, defaultValue);
                _dragMixerOutput = NO;
                [self setNeedsDisplay:YES];
                return;
            }
            _dragMixerOutput = YES;
            [self updateMixerOutput:pt inRect:mixerRect];
            return;
        }
        const uint32_t pageStart = static_cast<uint32_t>(_mixerPage) * 16u;
        const uint32_t pageEnd = std::min<uint32_t>(n, pageStart + 16u);
        for (uint32_t i = pageStart; i < pageEnd; ++i) {
            const uint32_t slot = i - pageStart;
            if (NSPointInRect(pt, NSInsetRect([self mixerMuteRect:slot inRect:mixerRect], -4.0, -4.0))) {
                [self toggleMixerSpeakerMute:i];
                return;
            }
            if (NSPointInRect(pt, NSInsetRect([self mixerSoloRect:slot inRect:mixerRect], -4.0, -4.0))) {
                [self toggleMixerSpeakerSolo:i];
                return;
            }
            if (NSPointInRect(pt, NSInsetRect([self mixerSpeakerRect:i inRect:mixerRect], -6.0, -7.0))) {
                if ([event clickCount] >= 2) {
                    applyParam(*p, kSelectedSpeakerParamId,
                        static_cast<double>(i + 1u));
                    setSpeakerGainNonRealtime(*p, i, 1.0);
                    _dragMixerSpeaker = -1;
                    _hasSpeakerSelection = YES;
                    [self setNeedsDisplay:YES];
                    return;
                }
                _dragMixerSpeaker = static_cast<int>(i);
                [self updateMixerSpeakerGain:pt inRect:mixerRect];
                return;
            }
        }
    }
    if (NSPointInRect(pt, fieldPanel)) {
        for (int i = 0; i < 3; ++i) {
            if (NSPointInRect(pt, [self fieldPageButtonRect:i inRect:fieldPanel])) {
                _rightPage = i;
                _dragMixerSpeaker = -1;
                _dragMixerOutput = NO;
                if (_rightPage == 1) _hasSpeakerSelection = NO;
                [self updateValueFields];
                [self updateMixerGainFieldsForRect:mixerRect];
                [self setNeedsDisplay:YES];
                return;
            }
        }
    }
    if ((_rightPage == 0 || _rightPage == 2) && NSPointInRect(pt, fieldPanel)) {
        for (int i = 0; i < 2; ++i) {
            if (NSPointInRect(pt, [self zoomButtonRect:i inRect:fieldPanel])) {
                const CGFloat step = i == 0 ? -0.15 : 0.15;
                _viewZoom = std::clamp(_viewZoom + step, 0.55, 2.20);
                [self storeViewState];
                [self setNeedsDisplay:YES];
                return;
            }
        }
        for (int i = 0; i < 3; ++i) {
            if (NSPointInRect(pt, [self viewButtonRect:i inRect:fieldPanel])) {
                [self setViewPreset:i];
                return;
            }
        }
        if (_rightPage == 0 && NSPointInRect(pt, fieldRect)) {
            BOOL found = NO;
            const uint32_t best = [self hitSpeakerAt:pt inRect:fieldRect found:&found];
            if (found) {
                applyParam(*p, kSelectedSpeakerParamId, best + 1u);
                _hasSpeakerSelection = YES;
                [self setNeedsDisplay:YES];
                return;
            }
            _hasSpeakerSelection = NO;
            _dragView = YES;
            _lastDragPoint = pt;
            [self setNeedsDisplay:YES];
            return;
        }
        if (_rightPage == 2 && NSPointInRect(pt, fieldRect)) {
            _dragView = YES;
            _lastDragPoint = pt;
            [self setNeedsDisplay:YES];
            return;
        }
    }
    if (NSPointInRect(pt, NSMakeRect(738, 143, 102, 17))) { openMenu(1, kLayoutMenuCount, 162); return; }
    if (NSPointInRect(pt, NSMakeRect(738, 169, 102, 17))) { openMenu(2, 4, 188); return; }
    if (NSPointInRect(pt, NSMakeRect(738, 195, 102, 17))) { openMenu(3, 7, 214); return; }
    if (NSPointInRect(pt, NSMakeRect(738, 221, 102, 17))) { openMenu(4, 3, 240); return; }
    if (NSPointInRect(pt, NSMakeRect(738, 247, 102, 17))) { openMenu(5, 2, 266); return; }
    const CGFloat rows[] = { 78, 274, 300, 366, 392, 418, 444 };
    const int ids[] = { 13, 4, 6, 7, 8, 9, 10 };
    const clap_id params[] = {
        kOutputParamId, kActiveSpeakersParamId, kWidthParamId,
        kSelectedSpeakerParamId, kAzimuthParamId, kElevationParamId,
        kDistanceParamId
    };
    for (int i = 0; i < 7; ++i) {
        if (NSPointInRect(pt, NSMakeRect(638, rows[i] - 8, 230, 24))) {
            double defaultValue = 0.0;
            if (s3g::clap_gui::sliderDoubleClickDefault(
                    event, &p->plugin, params[i], &defaultValue)) {
                applyParam(*p, params[i], defaultValue);
                _dragSlider = -1;
                [self updateValueFields];
                [self setNeedsDisplay:YES];
                return;
            }
            _dragSlider = ids[i];
            if (_dragSlider >= 7 && _dragSlider <= 10) _hasSpeakerSelection = YES;
            [self updateSlider:pt];
            return;
        }
    }
}
- (void)mouseMoved:(NSEvent*)event
{
    [self updateMenuHover:[self convertPoint:[event locationInWindow] fromView:nil]];
}
- (void)mouseDragged:(NSEvent*)event
{
    NSPoint pt = [self convertPoint:[event locationInWindow] fromView:nil];
    [self updateMenuHover:pt];
    if (_dragView) {
        const CGFloat dx = pt.x - _lastDragPoint.x;
        const CGFloat dy = pt.y - _lastDragPoint.y;
        _viewAzDeg += dx * 0.35;
        _viewElDeg = std::clamp(_viewElDeg + dy * 0.35, -85.0, 85.0);
        _viewMode = -1;
        _lastDragPoint = pt;
        [self storeViewState];
        [self setNeedsDisplay:YES];
        return;
    }
    if (_dragMixerSpeaker >= 0) {
        [self updateMixerSpeakerGain:pt inRect:NSMakeRect(34, 76, 564, 506)];
        return;
    }
    if (_dragMixerOutput) {
        [self updateMixerOutput:pt inRect:NSMakeRect(34, 76, 564, 506)];
        return;
    }
    if (_dragSlider > 0) [self updateSlider:pt];
}
- (void)mouseUp:(NSEvent*)event { (void)event; _dragSlider = -1; _dragView = NO; _dragMixerSpeaker = -1; _dragMixerOutput = NO; }
@end

namespace {

bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool isFloating) { return !isFloating && std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0; }
bool guiGetPreferredApi(const clap_plugin_t*, const char** api, bool* isFloating) { if (!api || !isFloating) return false; *api = CLAP_WINDOW_API_COCOA; *isFloating = false; return true; }
bool guiCreate(const clap_plugin_t* plugin, const char* api, bool isFloating) { if (!guiIsApiSupported(plugin, api, isFloating)) return false; auto* p = self(plugin); if (p->guiView) return true; p->guiView = [[S3GAmbiSpeakerDecoderView alloc] initWithPlugin:p]; if (!p->guiView) return false; if (!s3g::clap_gui::createResponsiveViewport(p->guiViewport, static_cast<NSView*>(p->guiView), 900u, 620u)) { [static_cast<NSView*>(p->guiView) release]; p->guiView = nullptr; return false; } return true; }
void guiDestroy(const clap_plugin_t* plugin) { auto* p = self(plugin); if (p->guiView) { p->guiVisible = false; [static_cast<S3GAmbiSpeakerDecoderView*>(p->guiView) stopRefreshTimer]; s3g::clap_gui::destroyResponsiveViewport(p->guiViewport, p->guiView); } }
bool guiSetScale(const clap_plugin_t*, double) { return true; }
bool guiGetSize(const clap_plugin_t* plugin, uint32_t* w, uint32_t* h) { return s3g::clap_gui::getResponsiveViewportSize(self(plugin)->guiViewport, 900u, 620u, w, h); }
bool guiCanResize(const clap_plugin_t*) { return true; }
bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t* hints) { return s3g::clap_gui::getResponsiveResizeHints(hints); }
bool guiAdjustSize(const clap_plugin_t* plugin, uint32_t* w, uint32_t* h) { return s3g::clap_gui::adjustResponsiveViewportSize(self(plugin)->guiViewport, 900u, 620u, w, h); }
bool guiSetSize(const clap_plugin_t* plugin, uint32_t w, uint32_t h) { return s3g::clap_gui::setResponsiveViewportSize(self(plugin)->guiViewport, w, h); }
bool guiSetParent(const clap_plugin_t* plugin, const clap_window_t* win) { if (!win || std::strcmp(win->api, CLAP_WINDOW_API_COCOA) != 0 || !win->cocoa) return false; auto* p = self(plugin); return s3g::clap_gui::setResponsiveViewportParent(p->guiViewport, static_cast<NSView*>(win->cocoa), p->host); }
bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}
bool guiShow(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView || !s3g::clap_gui::setResponsiveViewportHidden(p->guiViewport, false)) return false; p->guiVisible = true; [static_cast<S3GAmbiSpeakerDecoderView*>(p->guiView) startRefreshTimer]; return true; }
bool guiHide(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView) return false; p->guiVisible = false; [static_cast<S3GAmbiSpeakerDecoderView*>(p->guiView) stopRefreshTimer]; return s3g::clap_gui::setResponsiveViewportHidden(p->guiViewport, true); }
const clap_plugin_gui_t guiExt { guiIsApiSupported, guiGetPreferredApi, guiCreate, guiDestroy, guiSetScale, guiGetSize, guiCanResize, guiGetResizeHints, guiAdjustSize, guiSetSize, guiSetParent, guiSetTransient, guiSuggestTitle, guiShow, guiHide };
#endif

const void* pluginGetExtension(const clap_plugin_t*, const char* id)
{
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &audioPorts;
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) return &paramsExt;
    if (std::strcmp(id, CLAP_EXT_STATE) == 0) return &stateExt;
#if defined(__APPLE__)
    if (std::strcmp(id, CLAP_EXT_GUI) == 0) return &guiExt;
#endif
    return nullptr;
}

const char* const features[] { CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_SURROUND, nullptr };

const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-dsp.ambi-speaker-decoder-64",
    "s3g Ambi Decoder Speaker 64",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.1.0",
    "1OA-7OA ACN/SN3D speaker decoder with 64-channel output, custom layouts, and ALLRAD support topology.",
    features
};

const clap_plugin_t* createPlugin(const clap_plugin_factory*, const clap_host_t* host, const char* pluginId)
{
    if (std::strcmp(pluginId, descriptor.id) != 0) return nullptr;
    auto* p = new (std::nothrow) Plugin();
    if (!p) return nullptr;
    p->host = host;
    p->plugin.desc = &descriptor;
    p->plugin.plugin_data = p;
    p->plugin.init = init;
    p->plugin.destroy = destroy;
    p->plugin.activate = activate;
    p->plugin.deactivate = deactivate;
    p->plugin.start_processing = startProcessing;
    p->plugin.stop_processing = stopProcessing;
    p->plugin.reset = reset;
    p->plugin.process = process;
    p->plugin.get_extension = pluginGetExtension;
    p->plugin.on_main_thread = onMainThread;
    return &p->plugin;
}

uint32_t factoryGetPluginCount(const clap_plugin_factory*) { return 1; }
const clap_plugin_descriptor_t* factoryGetPluginDescriptor(const clap_plugin_factory*, uint32_t index) { return index == 0 ? &descriptor : nullptr; }
const clap_plugin_factory_t factory { factoryGetPluginCount, factoryGetPluginDescriptor, createPlugin };
bool entryInit(const char*) { return true; }
void entryDeinit() {}
const void* entryGetFactory(const char* factoryId) { return std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0 ? &factory : nullptr; }

} // namespace

extern "C" const clap_plugin_entry_t clap_entry { CLAP_VERSION_INIT, entryInit, entryDeinit, entryGetFactory };
