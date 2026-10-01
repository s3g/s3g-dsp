#pragma once

#include "s3g_sample_neon_family.h"

#include "s3g_reloop_neon.h"
#include "s3g_sample_player.h"
#include "s3g_sample_neon_fx.h"
#include "s3g_sample_wavesets.h"
#include "s3g_sample_neon_stack.h"
#include "s3g_sample_neon_waveset_stack.h"
#include "s3g_sample_neon_lanes.h"
#include "s3g_sample_neon_frame_player.h"
#include "s3g_voice_output_allocator.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace s3g::sample {

constexpr std::size_t kSampleNeonSlotCount = 32u;
constexpr uint32_t kSampleNeonOutputChannels = 32u;
constexpr std::size_t kSampleNeonMaximumBlockEvents = 512u;

// Ambisonic channels are always ACN / SN3D. No decoding, normalization
// conversion, speaker remapping, or implicit downmix occurs in this engine.
enum class SampleNeonSourceFormat : uint8_t { Discrete, Ambisonic };
enum class SampleNeonOutputLayout : uint8_t {
    Stereo, StereoStems, Quad, Octo, Ambisonic1, Ambisonic2, Ambisonic3,
};

inline constexpr uint32_t sampleNeonBusWidth(SampleNeonOutputLayout layout)
{
    constexpr uint32_t widths[] { 2u, 2u, 4u, 8u, 4u, 9u, 16u };
    const auto index = static_cast<unsigned>(layout);
    return index < 7u ? widths[index] : 0u;
}

inline constexpr uint32_t sampleNeonBusCount(SampleNeonOutputLayout layout)
{
    const auto width = sampleNeonBusWidth(layout);
    return layout == SampleNeonOutputLayout::Stereo ? 1u
        : width ? kSampleNeonOutputChannels / width : 0u;
}

inline constexpr bool sampleNeonChannelCountSupported(uint32_t channels)
{
    return channels == 1u || channels == 2u || channels == 4u
        || channels == 8u || channels == 9u || channels == 16u;
}

inline constexpr bool sampleNeonRouteCompatible(uint32_t channels,
    SampleNeonSourceFormat format, SampleNeonOutputLayout layout,
    uint32_t bus, bool distribute = false)
{
    const bool ambisonic = layout >= SampleNeonOutputLayout::Ambisonic1;
    const auto width = sampleNeonBusWidth(layout);
    return sampleNeonChannelCountSupported(channels)
        && bus < sampleNeonBusCount(layout)
        && (ambisonic
            ? format == SampleNeonSourceFormat::Ambisonic && channels == width
            : format == SampleNeonSourceFormat::Discrete
                && (distribute || (channels <= 8u && channels <= width)));
}

enum class SampleNeonEventKind : uint8_t {
    Trigger = 0u,
    Release,
    Pressure,
    Choke,
    StackPosition, // transient performance override; -1 resumes saved navigation
};

// These are the first three mutually-exclusive status lamps printed below
// every Neon pad. Repeat and Sync are independent switches represented by
// lamps four and five.
enum class SampleNeonSliceTriggerMode : uint8_t {
    OneShot = 0u,
    Toggle,
    Hold,
};

constexpr std::size_t kSampleNeonSliceCount = 32u;
constexpr std::size_t kSampleNeonCueCount = 8u;
constexpr std::size_t kSampleNeonSavedLoopCount = 8u;

enum class SampleNeonChopMode : uint8_t {
    LiveMark = 0u,
    Transient,
    Equal,
    BeatGrid,
};

enum class SampleNeonMotionPath : uint8_t {
    Forward = 0u,
    Reverse,
    RoundTrip,
    Drunk,
};

// One cell, one technique. CHOP previews bypass performance playback.
enum class SampleNeonPlayback : uint8_t { Sample, Motion, Grains, SliceSequence, Stretch, Wavesets, Lanes, Spectral, Cutups };
enum class SampleNeonClock : uint8_t { Free, Host };

struct SampleNeonSliceLayout {
    std::array<double, kSampleNeonSliceCount + 1u> boundaries {{
        0.0, 1.0,
    }};
    uint8_t sliceCount = 1u;

    bool valid() const noexcept
    {
        if (sliceCount == 0u || sliceCount > kSampleNeonSliceCount
            || boundaries[0u] != 0.0
            || boundaries[sliceCount] != 1.0) return false;
        for (std::size_t index = 0u; index <= sliceCount; ++index) {
            if (!std::isfinite(boundaries[index])
                || boundaries[index] < 0.0 || boundaries[index] > 1.0
                || (index != 0u
                    && boundaries[index] <= boundaries[index - 1u]))
                return false;
        }
        return true;
    }
};

inline SampleNeonSliceLayout equalSampleNeonSliceLayout(
    std::size_t sliceCount) noexcept
{
    SampleNeonSliceLayout result;
    sliceCount = std::clamp<std::size_t>(
        sliceCount, 1u, kSampleNeonSliceCount);
    result.boundaries.fill(0.0);
    result.sliceCount = static_cast<uint8_t>(sliceCount);
    for (std::size_t index = 0u; index <= sliceCount; ++index)
        result.boundaries[index] = static_cast<double>(index)
            / static_cast<double>(sliceCount);
    return result;
}

inline SampleNeonSliceLayout transientSampleNeonSliceLayout(
    const float* starts, std::size_t startCount,
    std::size_t maximumSlices = kSampleNeonSliceCount,
    double preRollMs = 0.0, double durationSeconds = 0.0) noexcept
{
    SampleNeonSliceLayout result;
    result.boundaries.fill(0.0);
    result.boundaries[0u] = 0.0;
    maximumSlices = std::clamp<std::size_t>(
        maximumSlices, 1u, kSampleNeonSliceCount);
    std::size_t count = 1u;
    // Starts and duration refer to the trimmed source. Shift detections, not
    // an already shifted map, so dragging the control never accumulates drift.
    const double lead = std::isfinite(durationSeconds) && durationSeconds > 0.0
        && std::isfinite(preRollMs)
        ? std::clamp(preRollMs, 0.0, 50.0) * 0.001 / durationSeconds : 0.0;
    if (starts) {
        for (std::size_t index = 0u;
             index < startCount && count < maximumSlices; ++index) {
            const double attack = starts[index];
            if (!std::isfinite(attack) || attack <= 0.0 || attack >= 1.0) continue;
            const double position = std::max(0.0, attack - lead);
            if (position <= 0.0
                || position <= result.boundaries[count - 1u]) continue;
            result.boundaries[count++] = position;
        }
    }
    result.sliceCount = static_cast<uint8_t>(count);
    result.boundaries[count] = 1.0;
    return result;
}

inline SampleNeonSliceLayout beatGridSampleNeonSliceLayout(
    double durationSeconds, double sourceBpm,
    double beatsPerSlice) noexcept
{
    if (!(durationSeconds > 0.0) || !std::isfinite(durationSeconds)
        || !(sourceBpm > 0.0) || !std::isfinite(sourceBpm)
        || !(beatsPerSlice > 0.0) || !std::isfinite(beatsPerSlice))
        return equalSampleNeonSliceLayout(1u);
    const double sliceSeconds = 60.0 / sourceBpm * beatsPerSlice;
    const std::size_t sliceCount = std::clamp<std::size_t>(
        static_cast<std::size_t>(std::ceil(
            durationSeconds / sliceSeconds)),
        1u, kSampleNeonSliceCount);
    SampleNeonSliceLayout result;
    result.boundaries.fill(0.0);
    result.sliceCount = static_cast<uint8_t>(sliceCount);
    for (std::size_t index = 0u; index < sliceCount; ++index)
        result.boundaries[index] = std::clamp(
            static_cast<double>(index) * sliceSeconds / durationSeconds,
            0.0, 1.0);
    result.boundaries[sliceCount] = 1.0;
    return result;
}

inline bool addSampleNeonSliceMarker(SampleNeonSliceLayout& layout,
    double position) noexcept
{
    if (!layout.valid() || layout.sliceCount >= kSampleNeonSliceCount
        || !std::isfinite(position) || position <= 0.0 || position >= 1.0)
        return false;
    std::size_t insertion = 1u;
    while (insertion < layout.sliceCount
        && layout.boundaries[insertion] < position) ++insertion;
    constexpr double minimumGap = 1.0e-6;
    if (position - layout.boundaries[insertion - 1u] <= minimumGap
        || layout.boundaries[insertion] - position <= minimumGap)
        return false;
    for (std::size_t index = layout.sliceCount + 1u;
         index > insertion; --index)
        layout.boundaries[index] = layout.boundaries[index - 1u];
    layout.boundaries[insertion] = position;
    ++layout.sliceCount;
    return true;
}

inline bool moveSampleNeonSliceMarker(SampleNeonSliceLayout& layout,
    std::size_t markerIndex, double position) noexcept
{
    if (!layout.valid() || markerIndex == 0u
        || markerIndex >= layout.sliceCount || !std::isfinite(position))
        return false;
    constexpr double minimumGap = 1.0e-6;
    layout.boundaries[markerIndex] = std::clamp(position,
        layout.boundaries[markerIndex - 1u] + minimumGap,
        layout.boundaries[markerIndex + 1u] - minimumGap);
    return true;
}

// Numeric navigation only: no PCM, voices, delay lines or FFT history. Decks
// may use this to issue a NEW launch after a host's transport-start reset.
struct NeonTransportPosition {
    uint64_t ageFrames = 0, serial = 0, seed = 0;
    unsigned layer = 0, sourceLayer = 0, sequenceIndex = 0, mosaicIndex = 0;
    double sourcePhase = 0, heldPosition = 0, eventPosition = 0, doubletPosition = 0;
    double sourcePosition = 0, lanePhase = 0, stackCuePosition = 0;
    float stackTarget = -1, velocity = 1;
    bool selectedSource = false, reverse = false;
};

struct SampleNeonEvent {
    uint32_t frameOffset = 0u;
    SampleNeonEventKind kind = SampleNeonEventKind::Trigger;
    uint64_t noteId = 0u;
    uint8_t slot = 0u;
    controller::reloop_neon::Mode mode =
        controller::reloop_neon::Mode::Sampler;
    uint8_t performanceIndex = 0u;
    float value = 1.0f;
    bool shifted = false;
    bool reverse = false;
    bool loopedSlicer = false;
    bool alternate = false;
    bool selectedSource = false;
    uint8_t sourceLayer = 255u; // explicit audition without changing the edit layer
    bool resumeNavigation = false; // explicit source/manual choice exits an isolated layer audition
    // Address (slot) and musical pitch are deliberately independent. Factory
    // pad triggers leave key at 255 and retain the pad's existing tuning.
    uint8_t key = 255u;
    bool snapStackPosition = false; // cue recall starts at the blend, not a glide from layer zero
    const NeonTransportPosition* transportPosition = nullptr; // valid for this render only
};

struct SampleNeonSlotSettings {
    // Optional deck platter override, normalized within the source window.
    // Disabled for every existing Neon instance and saved state.
    double sourceScrub = -1.0;
    const float* deckPlatterRates = nullptr;
    const float* deckScanPositions = nullptr;
    float deckScanMix = 1.f;
    double scrubbed(double natural, unsigned frame=0) const noexcept {
        if (sourceScrub<0 && !deckScanPositions) return natural;
        const double target=deckScanPositions ? deckScanPositions[frame] : sourceScrub;
        // Circular interpolation avoids scanning across the whole file when
        // releasing near the source wrap. Ordinary Neon never enables this.
        const double difference=target-natural;
        return neonUnitPhase(natural+(difference-std::round(difference))*deckScanMix);
    }
    bool deckWavesetSources = false; // Decks enables all stack source policies.
    NeonFamilySettings family;
    const NeonStack* stack = nullptr;
    NeonSourceMode sourceMode = NeonSourceMode::Primary;
    uint8_t selectedLayer = 0u;
    float stackCycleSeconds = 4.0f, stackCycleBeats = 8.0f;
    float gainDecibels = -6.0f;
    float pan = 0.0f;
    float tuneSemitones = 0.0f;
    double start = 0.0;
    double end = 1.0;
    float attackProportion = 0.0f;
    float decayProportion = 0.0f;
    float sustain = 1.0f;
    float releaseProportion = 0.005f;
    FilterType filterType = FilterType::Off;
    float filterCutoffHz = 20000.0f;
    float filterResonance = 0.0f;
    float velocitySensitivity = 1.0f;
    TriggerMode triggerMode = TriggerMode::Auto;
    RetriggerMode retriggerMode = RetriggerMode::Restart;
    uint8_t noteVoiceMode = 0u; // 0 original pad behavior, 1 Mono, 2 Poly, 3 Legato
    uint8_t noteVoiceLimit = 8u;
    uint8_t rootNote = 60u;
    s3g::routing::TriggerOutputAllocator<16u>* noteRoutingAllocator = nullptr;
    s3g::routing::TriggerOutputAllocator<32u>* noteCutRoutingAllocator = nullptr;
    uint8_t noteVoiceOrdinal = 255u;
    SampleNeonMangleCharacter character =
        SampleNeonMangleCharacter::Filter;
    float mangle = 0.0f;
    float pressureDepth = 0.75f;
    uint8_t outputBus = 0u;
    SampleNeonSourceFormat sourceFormat = SampleNeonSourceFormat::Discrete;
    uint8_t chokeGroup = 0u;
    bool repeat = false;
    uint8_t direction = 0u; // forward, reverse, ping-pong, reverse ping-pong
    bool sync = false;
    bool velocityEnabled = true;
    double sourceDurationSeconds = 0.0;
    int deckPerformanceLayer = -1; // Decks' clock-preserving discrete layer ride
    bool deckTransport = false; // opt-in launch offsets for embedded decks
    double sourceTempoBpm = 120.0;
    double slicerDomainStart = 0.0;
    double slicerDomainBeats = 8.0;
    double slicerQuantizeBeats = 0.25;
    SampleNeonSliceLayout sliceLayout =
        equalSampleNeonSliceLayout(kSampleNeonSliceCount);
    std::array<double, kSampleNeonCueCount> hotCues {};
    std::array<bool, kSampleNeonCueCount> hotCueEnabled {};
    std::array<double, kSampleNeonSavedLoopCount> loopStarts {};
    std::array<double, kSampleNeonSavedLoopCount> loopEnds {};
    std::array<bool, kSampleNeonSavedLoopCount> loopEnabled {};
    std::array<SampleNeonSliceTriggerMode,
        kSampleNeonSliceCount> sliceTriggers {};
    std::array<bool, kSampleNeonSliceCount> sliceRepeat {};
    std::array<bool, kSampleNeonSliceCount> sliceSync {};
    uint8_t sliceCount = static_cast<uint8_t>(kSampleNeonSliceCount);
    SampleNeonPlayback playback = SampleNeonPlayback::Sample;
    SampleNeonClock clock = SampleNeonClock::Free;
    SampleNeonMotionPath motionPath = SampleNeonMotionPath::Forward;
    float motionCycleBeats = 8.0f;
    float motionCycleSeconds = 4.0f;
    float shotSeconds = 1.0f;
    float grainIntervalBeats = 0.25f;
    double launchPosition = 0.0;
    float grainDensityHz = 12.0f;
    float grainSizeMs = 80.0f;
    float grainPosition = 0.5f;
    float grainSpray = 0.15f;
    float grainPitchSpraySemitones = 0.0f;
    float grainReverseChance = 0.0f;
    NeonCharacterValues fx = kSampleNeonFxDefaults[0];
    std::array<float, 4u> technique {{ 0.35f, 0.0f, 0.0f, 1.0f }};
    float techniqueAttackSeconds = 0.005f, techniqueReleaseSeconds = 0.005f;

    SampleNeonSlotSettings() noexcept
    {
        for (std::size_t index = 0u; index < hotCues.size(); ++index)
            hotCues[index] = static_cast<double>(index)
                / static_cast<double>(hotCues.size());
        for (std::size_t index = 0u; index < loopStarts.size(); ++index) {
            loopStarts[index] = static_cast<double>(index)
                / static_cast<double>(loopStarts.size());
            loopEnds[index] = std::min(1.0, loopStarts[index] + loopLength(
                static_cast<uint8_t>(index)));
        }
    }

private:
    static double loopLength(uint8_t index) noexcept
    {
        static constexpr std::array<double, 8u> lengths {{
            1.0, 0.5, 0.25, 0.125,
            0.0625, 0.03125, 0.015625, 0.0078125,
        }};
        return lengths[std::min<std::size_t>(index, lengths.size() - 1u)];
    }
};

inline bool neonDistributes(const SampleNeonSlotSettings& control) noexcept {
    return control.sourceFormat == SampleNeonSourceFormat::Discrete
        && control.family[NeonFamily::RoutingMode] != 0;
}
inline SampleVoiceEnvelope neonSliceEnvelope(const NeonFamilySettings& f) noexcept {
    const float scale = std::max(1.f, f[NeonFamily::SliceAttack] + f[NeonFamily::SliceDecay] + f[NeonFamily::SliceRelease]);
    return {f[NeonFamily::SliceAttack]/scale, f[NeonFamily::SliceDecay]/scale,
        f[NeonFamily::SliceSustain], f[NeonFamily::SliceRelease]/scale};
}

struct SampleNeonSettings {
    std::array<SampleNeonSlotSettings, kSampleNeonSlotCount> slots {};
    SampleNeonOutputLayout outputLayout = SampleNeonOutputLayout::Stereo;
    float masterGainDecibels = -6.0f;
    float globalMangle = 0.0f;
    float hostTempoBpm = 120.0f;
    double hostBeatPosition = 0.0;
    bool transportPlaying = false;
    bool hostBeatValid = false;
    bool tempoSync = false;

    SampleNeonSettings() noexcept
    {
        for (std::size_t slot = 0u; slot < slots.size(); ++slot) {
            slots[slot].character = static_cast<SampleNeonMangleCharacter>(
                slot % 8u);
        }
    }
};

class SampleNeonEngine {
public:
    NeonTransportPosition transportPosition(unsigned slot) const noexcept {
        NeonTransportPosition p;
        if (slot >= kSampleNeonSlotCount) return p;
        const auto& e = grainEmitters_[slot];
        p.ageFrames=e.ageFrames;p.serial=e.serial;p.seed=e.seed;p.layer=e.layer;
        p.sequenceIndex=e.sequenceIndex;p.mosaicIndex=e.mosaicIndex;p.sourcePhase=e.sourcePhase;
        p.heldPosition=e.heldPosition;p.eventPosition=e.eventPosition;p.doubletPosition=e.doubletPosition;
        p.selectedSource=e.selectedSource;p.velocity=e.velocity;
        p.lanePhase=lanes_[slot].phase();p.stackTarget=stackCueTargets_[slot];p.stackCuePosition=stackCuePositions_[slot];
        const auto n=voiceCursorCount(slot);
        if(n){const auto& c=voiceCursors(slot)[n-1];p.sourcePosition=c.sourcePositionNormalized;p.reverse=c.reverse;p.sourceLayer=c.layer;
            if(!e.ownsOutput)p.layer=c.layer;}
        return p;
    }
    void seekSource(std::size_t slot, double position) noexcept {
        if (slot >= players_.size()) return;
        players_[slot].seekVoices(position); lanes_[slot].seek(position);cutups_[slot].seekTimeline(position);
    }
    bool prepare(double sampleRate, uint32_t maximumFrames, bool prepareEffects = true)
    {
        if (!(sampleRate > 0.0) || !std::isfinite(sampleRate)
            || maximumFrames == 0u) return false;
        sampleRate_ = sampleRate;
        maximumFrames_ = maximumFrames;
        try {
            for (auto& channel : scratch_)
                channel.assign(maximumFrames, 0.0f);
            techniqueEnvelope_.assign(maximumFrames, 1.0f);
            waveScanPositions_.assign(maximumFrames, -2.0f);
            waveScanVelocities_.assign(maximumFrames, 1.0f);
            waveScanRetriggers_.assign(maximumFrames, 0u);
            frameSourcePositions_.assign(maximumFrames, 0.f);
        } catch (...) {
            unprepare();
            return false;
        }
        for (std::size_t slot = 0u; slot < players_.size(); ++slot) {
            if (!players_[slot].prepare(sampleRate, kMaximumAudioChannels) || (prepareEffects && !effects_[slot].prepare(sampleRate))) {
                unprepare();
                return false;
            }
            players_[slot].setPreparedAsset(assets_[slot]);
            wavesets_[slot].prepare(sampleRate, 16u);
            cutups_[slot].prepare(sampleRate, 16u);
            waveScans_[slot].prepare(sampleRate);
            if (!framePlayers_[slot].prepare()) { unprepare(); return false; }
        }
        prepared_ = true;
        reset();
        return true;
    }

    void unprepare() noexcept
    {
        reset();
        for (auto& waveset : wavesets_) waveset.setPreparedMap(nullptr);
        waveMaps_.fill(nullptr);
        for (auto& player : players_) player.unprepare();
        for (auto& effect : effects_) effect.unprepare();
        for (auto& player : framePlayers_) player.unprepare();
        for (auto& cut : cutups_) cut.unprepare();
        for (auto& channel : scratch_) channel.clear();
        techniqueEnvelope_.clear();
        waveScanPositions_.clear(); waveScanVelocities_.clear(); waveScanRetriggers_.clear();
        frameSourcePositions_.clear();
        maximumFrames_ = 0u;
        prepared_ = false;
    }

    void reset() noexcept
    {
        for (auto& player : players_) player.reset();
        for (auto& effect : effects_) effect.reset();
        for (auto& waveset : wavesets_) waveset.reset();
        for (auto& scan : waveScans_) scan.reset();
        for (auto& lane : lanes_) lane.reset();
        for (auto& player : framePlayers_) player.reset();
        for (auto& cut : cutups_) cut.reset();
        waveCursorCounts_.fill(0u);
        waveActive_.fill(false);
        pressure_.fill(0.0f);
        slotPeaks_.fill(0.0f);
        motionBeatPosition_ = 0.0;
        grainEmitters_ = {};
        outputPeak_ = 0.0f;
        stackPositions_.fill(0.0f);
        stackPathPhases_.fill(-1.0f);
        stackCueTargets_.fill(-1.0f); stackCuePositions_.fill(0.0); stackCueReturning_.fill(false);
        stackVoiceCounts_.fill(0u);
        for (unsigned n = 0u; n < stackSeeds_.size(); ++n) stackSeeds_[n] = 0x9e3779b97f4a7c15ull + n;
        for (unsigned n = 0u; n < allocators_.size(); ++n) allocators_[n].reset(0x6d2b79f5u + n);
        gestureRoutes_ = {}; routeSignatures_.fill(0u);
    }

    void killAll() noexcept
    {
        stackPathPhases_.fill(-1.0f);
        stackCueTargets_.fill(-1.0f); stackCueReturning_.fill(false);
        for (auto& player : players_) player.killAll();
        for (auto& effect : effects_) effect.reset();
        for (auto& waveset : wavesets_) waveset.reset();
        for (auto& scan : waveScans_) scan.reset();
        for (auto& lane : lanes_) lane.reset();
        for (auto& player : framePlayers_) player.reset();
        for (auto& cut : cutups_) cut.reset();
        waveCursorCounts_.fill(0u);
        grainEmitters_ = {};
        stackVoiceCounts_.fill(0u);
    }

    bool setAsset(std::size_t slot, const SampleAsset* asset) noexcept
    {
        if (slot >= players_.size() || (asset
                && !sampleNeonChannelCountSupported(asset->channelCount))
            || !players_[slot].setAsset(asset)) return false;
        if (assets_[slot] != asset) resetSlotProcessing(slot);
        assets_[slot] = asset;
        return true;
    }

    void setPreparedAsset(std::size_t slot,
        const SampleAsset* asset) noexcept
    {
        if (slot >= players_.size()) return;
        if (assets_[slot] != asset) resetSlotProcessing(slot);
        assets_[slot] = asset;
        players_[slot].setPreparedAsset(asset);
    }

    bool slotLoaded(std::size_t slot) const noexcept
    {
        return slot < assets_.size() && assets_[slot] != nullptr;
    }
    void setPreparedWavesets(std::size_t slot, const WavesetMap* map) noexcept {
        if (slot >= waveMaps_.size() || waveMaps_[slot] == map) return;
        waveMaps_[slot] = map; wavesets_[slot].setPreparedMap(map);
    }

    float slotPeak(std::size_t slot) const noexcept
    {
        return slot < slotPeaks_.size() ? slotPeaks_[slot] : 0.0f;
    }

    float slotPressure(std::size_t slot) const noexcept
    {
        return slot < pressure_.size() ? pressure_[slot] : 0.0f;
    }

    float outputPeak() const noexcept { return outputPeak_; }
    float stackPosition(std::size_t slot) const noexcept { return stackPositions_[slot]; }
    float stackPathPhase(std::size_t slot, const SampleNeonSettings& settings) const noexcept {
        const auto& s = settings.slots[slot]; const auto& e = grainEmitters_[slot];
        if (s.playback == SampleNeonPlayback::Cutups) return waveActive_[slot] && cutups_[slot].activeVoiceCount() ? stackPathPhases_[slot] : -1.f;
        const bool path = s.playback == SampleNeonPlayback::Lanes ? s.family[NeonFamily::LaneAuto] != 0
            : s.sourceMode == NeonSourceMode::Scan && s.playback != SampleNeonPlayback::Sample
                && s.playback != SampleNeonPlayback::SliceSequence;
        return path && e.active && !e.selectedSource && s.stack && s.stack->count > 1 ? stackPathPhases_[slot] : -1.f;
    }
    bool stackScanActive(std::size_t slot, const SampleNeonSettings& settings) const noexcept {
        const auto& e = grainEmitters_[slot];
        const auto& s = settings.slots[slot];
        return (((s.sourceMode == NeonSourceMode::Scan || s.playback == SampleNeonPlayback::Lanes) && !e.selectedSource)
            || stackCueTargets_[slot] >= 0 || stackCueReturning_[slot]) && e.active
            && s.playback != SampleNeonPlayback::Sample && s.playback != SampleNeonPlayback::SliceSequence;
    }

    // Display only: -1 keeps the edit layer, -2 follows the scan blend, and
    // 0..31 follows a discrete playing layer. Never infer a fresh random or
    // velocity choice in the editor, or change selectedLayer to follow audio.
    int stackWaveformLayer(std::size_t slot, const SampleNeonSettings& settings) const noexcept {
        if (slot >= players_.size()) return -1;
        const auto& s = settings.slots[slot];
        const auto& emitter = grainEmitters_[slot];
        if (s.playback == SampleNeonPlayback::Cutups && waveActive_[slot])
            return waveCursorCounts_[slot] ? waveCursors_[slot][waveCursorCounts_[slot]-1].layer : -1;
        if (emitter.ownsOutput) {
            if (!emitter.active) return -1;
            if (stackScanActive(slot, settings)) return -2;
            if (emitter.selectedSource) return -1;
            return static_cast<int>(emitter.layer);
        }
        if (waveActive_[slot]) return wavesets_[slot].activeVoiceCount() ? 0 : -1;
        if (!stackVoiceCounts_[slot]) return -1;
        const auto& voice = stackVoices_[slot][stackVoiceCounts_[slot] - 1u];
        // A source replacement must not draw new audio for an old voice.
        return voice.follow && stackLayer(s, voice.layer, assets_[slot]).asset == voice.asset
            ? static_cast<int>(voice.layer) : -1;
    }

    double motionPosition(std::size_t slot,
        const SampleNeonSettings& settings) const noexcept
    {
        if (slot < kSampleNeonSlotCount && grainEmitters_[slot].ownsOutput) {
            const auto& s = settings.slots[slot];
            if ((s.playback == SampleNeonPlayback::Motion && s.family[NeonFamily::MotionModel] != 0)
                || (s.playback == SampleNeonPlayback::Grains && s.family[NeonFamily::GrainSource] != 0))
                return grainEmitters_[slot].doubletPosition;
        }
        return slot < kSampleNeonSlotCount
            ? scanPosition(settings, slot, grainEmitters_[slot].ageFrames, 0u)
            : 0.0;
    }

    struct MotionVisual {
        double seconds = 0, cycle = 0;
        float articulation = 0;
        bool active = false;
    };
    MotionVisual motionVisual(std::size_t slot, const SampleNeonSettings& settings) const noexcept {
        const auto& s = settings.slots[slot]; const auto& e = grainEmitters_[slot];
        MotionVisual result;
        if (s.playback != SampleNeonPlayback::Motion || !e.active || !e.ownsOutput) return result;
        result.active = true;
        result.seconds = s.clock == SampleNeonClock::Host
            ? motionBeatAtFrame(settings, 0) * 60.0 / std::clamp<double>(settings.hostTempoBpm, 20, 999)
            : e.ageFrames / sampleRate_;
        result.cycle = s.clock == SampleNeonClock::Host
            ? std::clamp<double>(s.motionCycleBeats, .25, 32) * 60.0 / std::clamp<double>(settings.hostTempoBpm, 20, 999)
            : std::clamp<double>(s.motionCycleSeconds, .05, 30);
        result.articulation = neonMotionArticulation(s.family, result.seconds);
        return result;
    }

    // Same trajectory used by the engine and its scope; the display does not
    // invent another waveform or advance the clock on the GUI timer.
    static double motionTrajectory(const SampleNeonSlotSettings& control, double phase,
        double seconds, std::size_t slot) noexcept {
        double position = resolvedMotionValue(control.motionPath, phase + control.launchPosition, slot);
        const auto& f = control.family;
        const double travel = f[NeonFamily::MotionTravel];
        switch (static_cast<unsigned>(f[NeonFamily::MotionTrajectory])) {
        case 1: position = .5; break;
        case 2: position = .5 + .5 * std::sin(neonUnitPhase(phase) * 6.28318530718); break;
        case 3: {
            const double p = neonUnitPhase(phase);
            position = std::clamp(p + travel * (resolvedMotionValue(SampleNeonMotionPath::RoundTrip, p * 8, slot) - .5), 0.0, 1.0);
            break;
        }
        case 4: position = neonUnitPhase(phase * travel + neonUnitPhase(phase * 8) * (1 - travel)); break;
        default: break;
        }
        const bool bounded = f[NeonFamily::MotionTrajectory] != 0 ? f[NeonFamily::MotionTrajectory] != 4
            : control.motionPath != SampleNeonMotionPath::Forward && control.motionPath != SampleNeonMotionPath::Reverse;
        if (f[NeonFamily::MotionSound] == 2 && bounded
            && neonUnitPhase(seconds * f[NeonFamily::MotorRate]) > f[NeonFamily::MotorSymmetry]) position = 1 - position;
        return std::clamp(f[NeonFamily::MotionLocus] + (position - .5) * f[NeonFamily::MotionField], 0.0, 1.0);
    }

    const std::array<VoiceCursor, kMaximumVoices>& voiceCursors(
        std::size_t slot) const noexcept
    {
        static const std::array<VoiceCursor, kMaximumVoices> empty {};
        return slot < players_.size() ? waveActive_[slot] ? waveCursors_[slot] : players_[slot].voiceCursors() : empty;
    }

    uint32_t voiceCursorCount(std::size_t slot) const noexcept
    {
        return slot < players_.size() ? waveActive_[slot] ? waveCursorCounts_[slot] : players_[slot].voiceCursorCount() : 0u;
    }

    std::size_t activeVoiceCount() const noexcept
    {
        std::size_t count = 0u;
        for (const auto& player : players_)
            count += player.activeVoiceCount();
        for (const auto& wave : wavesets_) count += wave.activeVoiceCount();
        for (const auto& scan : waveScans_) count += scan.activeVoiceCount();
        for (const auto& lane : lanes_) count += lane.cursorCount();
        for (const auto& player : framePlayers_) count += player.cursorCount();
        for (const auto& cut : cutups_) count += cut.activeVoiceCount();
        return count;
    }

    bool slotPlaybackActive(std::size_t slot) const noexcept {
        return slot < players_.size() && (players_[slot].activeVoiceCount() != 0u
            || wavesets_[slot].activeVoiceCount() != 0u || waveScans_[slot].activeVoiceCount() != 0u
            || cutups_[slot].activeVoiceCount() != 0u || grainEmitters_[slot].active);
    }

    // Voice-pool adapter: reset only one prepared renderer, never allocate in
    // process(). The ordinary engine API and its original pad behavior remain.
    void resetVoice(std::size_t slot) noexcept {
        if (slot >= players_.size()) return;
        players_[slot].killAll(); resetSlotProcessing(slot);
        stackPathPhases_[slot] = -1.f;
        routeSignatures_[slot] = 0;
    }
    using VoiceSink = void (*)(void*, unsigned, float* const*, unsigned, uint32_t);

    void render(const SampleNeonSettings& settings,
        const SampleNeonEvent* events, std::size_t eventCount,
        float* const* outputs, uint32_t outputChannelCount,
        uint32_t frameCount, VoiceSink sink = nullptr, void* context = nullptr,
        uint32_t activeMask = UINT32_MAX) noexcept
    {
        if (!outputs || (outputChannelCount != 2u
                && outputChannelCount != kSampleNeonOutputChannels)) return;
        for (uint32_t channel = 0u; channel < outputChannelCount; ++channel) {
            if (!outputs[channel]) return;
            std::fill(outputs[channel], outputs[channel] + frameCount, 0.0f);
        }
        if (!prepared_ || frameCount == 0u || frameCount > maximumFrames_)
            return;

        eventCount = events
            ? std::min(eventCount, kSampleNeonMaximumBlockEvents) : 0u;
        synchronizeMotionClock(settings);
        for (std::size_t index = 0u; index < eventCount; ++index) {
            const auto& event = events[index];
            if (event.slot >= kSampleNeonSlotCount
                || event.kind != SampleNeonEventKind::Pressure) continue;
            pressure_[event.slot] = std::clamp(event.value, 0.0f, 1.0f);
        }

        const float masterGain = decibelsToLinear(
            settings.masterGainDecibels);
        outputPeak_ = 0.0f;

        for (std::size_t slot = 0u; slot < players_.size(); ++slot) {
            if (!(activeMask & (uint32_t(1) << slot))) continue;
            const auto& routeControl = settings.slots[slot];
            const unsigned routeSignature = 1u + static_cast<unsigned>(routeControl.sourceFormat)
                + (static_cast<unsigned>(settings.outputLayout) << 2u)
                + (static_cast<unsigned>(routeControl.family[NeonFamily::RoutingMode]) << 6u)
                + (static_cast<unsigned>(routeControl.family[NeonFamily::RoutingWidth]) << 7u)
                + (static_cast<unsigned>(routeControl.family[NeonFamily::RoutingTraversal]) << 8u)
                + (static_cast<unsigned>(routeControl.family[NeonFamily::GrainStereoLink]) << 12u)
                + (static_cast<unsigned>(routeControl.family[NeonFamily::WavesetEngine]) << 13u);
            if (lastPlayback_[slot] != settings.slots[slot].playback || routeSignatures_[slot] != routeSignature) {
                players_[slot].killAll();
                effects_[slot].reset();
                wavesets_[slot].reset(); waveActive_[slot] = false;
                waveScans_[slot].reset(); waveCursorCounts_[slot] = 0u;
                lanes_[slot].reset();
                framePlayers_[slot].reset();
                cutups_[slot].reset();
                grainEmitters_[slot] = {};
                stackVoiceCounts_[slot] = 0u;
                lastPlayback_[slot] = settings.slots[slot].playback;
                routeSignatures_[slot] = routeSignature;
                allocators_[slot].reset(0x6d2b79f5u + static_cast<unsigned>(slot));
                gestureRoutes_[slot] = {}; continuousRoutingActive_[slot] = false;
            }
            std::array<RenderEvent, kSampleNeonMaximumBlockEvents>
                playerEvents {};
            std::size_t playerEventCount = 0u;
            for (std::size_t index = 0u; index < eventCount; ++index) {
                const auto& source = events[index];
                if (source.kind == SampleNeonEventKind::Pressure || source.kind == SampleNeonEventKind::StackPosition
                    || source.slot >= kSampleNeonSlotCount) continue;
                if (playerEventCount >= playerEvents.size()) break;
                const auto& control = settings.slots[source.slot];
                const uint8_t group = settings.slots[slot].chokeGroup;
                if (source.kind == SampleNeonEventKind::Trigger
                    && slot != source.slot
                    && group != 0u
                    && settings.slots[source.slot].chokeGroup == group) {
                    RenderEvent choke;
                    choke.frameOffset = source.frameOffset;
                    choke.kind = EventKind::Choke;
                    choke.noteId = 0u;
                    choke.key = 0xffu;
                    playerEvents[playerEventCount++] = choke;
                    if (playerEventCount >= playerEvents.size()) break;
                }

                const bool sampler = source.mode
                    == controller::reloop_neon::Mode::Sampler;
                if (sampler
                    && control.playback != SampleNeonPlayback::Sample
                    && source.kind != SampleNeonEventKind::Choke) {
                    if (slot == source.slot && (control.playback == SampleNeonPlayback::Wavesets || control.playback == SampleNeonPlayback::Cutups)
                        && source.kind == SampleNeonEventKind::Trigger)
                        playerEvents[playerEventCount++] = RenderEvent {source.frameOffset, EventKind::Choke, 0u, 0xffu};
                    continue;
                }

                if (source.slot != slot) continue;

                auto routedSource = source;
                routedSource.slot = static_cast<uint8_t>(slot);
                auto sourceSettings = settings.slots[slot];
                unsigned layer = sourceSettings.selectedLayer;
                if (sampler && source.kind == SampleNeonEventKind::Trigger && !source.selectedSource)
                    layer = chooseStackLayer(sourceSettings, source.value, slot);
                if (source.sourceLayer < 32u) layer = source.sourceLayer;
                const auto selectedSource = stackLayer(sourceSettings, layer, assets_[slot]);
                sourceSettings.start = selectedSource.start; sourceSettings.end = selectedSource.end;
                auto event = makePlayerEvent(sourceSettings,
                    routedSource, effectiveMangle(settings, slot));
                event.sourceAsset = selectedSource.asset;
                if (!selectedSource.asset) event.sourceGain = 0.0f;
                event.sourceStart = selectedSource.start;
                event.sourceLength = selectedSource.end - selectedSource.start;
                event.sourceLayer = static_cast<uint8_t>(layer);
                if (source.kind == SampleNeonEventKind::Trigger) event.route = nextRoute(settings, slot);
                if (source.mode == controller::reloop_neon::Mode::Slicer) event.envelope = neonSliceEnvelope(sourceSettings.family);
                if (source.kind == SampleNeonEventKind::Trigger)
                    rememberStackVoice(slot, event, layer, sampler && !source.selectedSource);
                if (sampler && source.kind == SampleNeonEventKind::Trigger) {
                    applyLaunchPosition(sourceSettings, sourceSettings,
                        source.slot, motionBeatAtFrame(settings,
                            source.frameOffset), settings.transportPlaying,
                        event);
                    if(source.transportPosition){event.resumePositionNormalized=source.transportPosition->sourcePosition;
                        event.resumeReverse=source.transportPosition->reverse?1:0;}
                }
                playerEvents[playerEventCount++] = event;
            }
            if(settings.slots[slot].playback==SampleNeonPlayback::Sample&&settings.slots[slot].deckPerformanceLayer>=0){
                const unsigned layer=static_cast<unsigned>(settings.slots[slot].deckPerformanceLayer);
                const auto source=stackLayer(settings.slots[slot],layer,assets_[slot]);
                players_[slot].retargetSource(source.asset,source.start,source.end,static_cast<uint8_t>(layer));
            }
            appendTechniqueEvents(settings, slot, events, eventCount,
                playerEvents, playerEventCount, frameCount);

            std::array<float*, kMaximumAudioChannels> channels {};
            for (std::size_t channel = 0u; channel < channels.size(); ++channel)
                channels[channel] = scratch_[channel].data();
            players_[slot].render(makePlayerSettings(settings, slot),
                playerEvents.data(), playerEventCount, channels.data(),
                kMaximumAudioChannels,
                frameCount, settings.slots[slot].playback == SampleNeonPlayback::Sample
                    ? settings.slots[slot].deckPlatterRates : nullptr);
            updateStackVoices(slot);
            const auto& frameControl = settings.slots[slot];
            if (frameControl.playback == SampleNeonPlayback::Cutups)
                renderCutups(settings, slot, events, eventCount, channels.data(), frameCount);
            const bool oscillator = frameControl.playback == SampleNeonPlayback::Wavesets && frameControl.family[NeonFamily::WavesetEngine] != 0;
            const bool framePlayback = frameControl.playback == SampleNeonPlayback::Spectral || oscillator;
            if (settings.slots[slot].playback == SampleNeonPlayback::Wavesets && !oscillator)
                renderWavesets(settings, slot, events, eventCount, channels.data(), frameCount);
            if (framePlayback) {
                const auto& s = frameControl;
                framePlayers_[slot].render(s.stack, {assets_[slot], s.start, s.end, waveMaps_[slot]}, s.selectedLayer,
                    s.start, s.end, s.family, oscillator, static_cast<unsigned>(s.family[NeonFamily::OscGroup]),
                    sampleRate_, decibelsToLinear(s.gainDecibels), s.tuneSemitones, s.velocityEnabled,
                    waveScanPositions_.data(), frameSourcePositions_.data(), waveScanVelocities_.data(),
                    waveScanRetriggers_.data(), channels.data(), frameCount);
                waveActive_[slot] = grainEmitters_[slot].ownsOutput;
                waveCursors_[slot] = framePlayers_[slot].cursors();
                waveCursorCounts_[slot] = framePlayers_[slot].cursorCount();
                if (oscillator && s.sourceFormat == SampleNeonSourceFormat::Ambisonic && grainEmitters_[slot].ownsOutput) {
                    for (auto* channel : channels) std::fill_n(channel, frameCount, 0.f);
                    waveCursorCounts_[slot] = 0;
                }
            }
            if (settings.slots[slot].playback == SampleNeonPlayback::Lanes) {
                const auto& s = settings.slots[slot];
                double resumePhase=-1;
                for(std::size_t n=0;n<eventCount;++n)if(events[n].slot==slot&&events[n].transportPosition)
                    resumePhase=events[n].transportPosition->lanePhase;
                lanes_[slot].render(s.stack, {assets_[slot], s.start, s.end}, s.selectedLayer,
                    s.start, s.end, s.family, sampleRate_, decibelsToLinear(s.gainDecibels),
                    s.tuneSemitones, s.direction, s.velocityEnabled, waveScanPositions_.data(),
                    waveScanVelocities_.data(), waveScanRetriggers_.data(), channels.data(), frameCount, s.deckPlatterRates,s.deckTransport?s.launchPosition:0,resumePhase);
                waveActive_[slot] = grainEmitters_[slot].ownsOutput;
                waveCursors_[slot] = lanes_[slot].cursors();
                waveCursorCounts_[slot] = lanes_[slot].cursorCount();
                stackPositions_[slot] = lanes_[slot].position();
            }
            if ((settings.slots[slot].playback == SampleNeonPlayback::Lanes
                || settings.slots[slot].playback == SampleNeonPlayback::Wavesets || framePlayback) && neonDistributes(settings.slots[slot]))
                routeContinuous(settings, slot, events, eventCount, channels.data(), frameCount);
            slotPeaks_[slot] = 0.0f;
            const auto& slotSettings = settings.slots[slot];
            const auto* asset = assets_[slot];
            if (!asset || !sampleNeonRouteCompatible(asset->channelCount,
                    slotSettings.sourceFormat, settings.outputLayout,
                    slotSettings.outputBus, neonDistributes(slotSettings))) continue;
            const uint32_t first = slotSettings.outputBus
                * sampleNeonBusWidth(settings.outputLayout);
            const bool distributed = neonDistributes(slotSettings);
            const uint32_t count = distributed ? sampleNeonBusWidth(settings.outputLayout) : std::max<uint32_t>(2u, asset->channelCount);
            if (first + count > outputChannelCount) continue;
            const uint32_t processedChannels = distributed ? count : asset->channelCount;
            for (uint32_t channel = 0u; channel < processedChannels; ++channel)
                for (uint32_t frame = 0u; frame < frameCount; ++frame)
                    channels[channel][frame] *= techniqueEnvelope_[frame];
            if (sink) {
                sink(context, static_cast<unsigned>(slot), channels.data(), processedChannels, frameCount);
                continue; // Sum note voices BEFORE the pad's character effects.
            }
            // Explicit Stop and choke groups silence tails at their event
            // frame. Internal sequence note changes leave effect tails alive.
            uint32_t fxFrame = 0u;
            const auto processFxTo = [&](uint32_t end) {
                std::array<float*, kMaximumAudioChannels> segment {};
                for (unsigned ch = 0u; ch < processedChannels; ++ch) segment[ch] = channels[ch] + fxFrame;
                effects_[slot].process(segment.data(), processedChannels, end - fxFrame,
                    slotSettings.character, slotSettings.fx, effectiveMangle(settings, slot),
                    slotSettings.sourceFormat == SampleNeonSourceFormat::Ambisonic, settings.hostTempoBpm);
                fxFrame = end;
            };
            for (std::size_t n = 0u; n < eventCount; ++n) {
                const auto& event = events[n];
                if (event.slot >= kSampleNeonSlotCount) continue;
                const bool stop = event.slot == slot && event.kind == SampleNeonEventKind::Choke && event.noteId == 0u;
                const bool peer = event.slot != slot && event.kind == SampleNeonEventKind::Trigger
                    && slotSettings.chokeGroup && slotSettings.chokeGroup == settings.slots[event.slot].chokeGroup;
                if (!stop && !peer) continue;
                processFxTo(std::clamp(event.frameOffset, fxFrame, frameCount));
                effects_[slot].reset();
            }
            processFxTo(frameCount);
            // Pan is a stereo-only operation. Every spatial channel has
            // identical gain/envelope/time processing, including grains.
            const float pan = std::clamp(slotSettings.pan, -1.0f, 1.0f);
            for (uint32_t channel = 0u; channel < count; ++channel) {
                const float balance = (distributed ? count : asset->channelCount) > 2u ? 1.0f
                    : std::sqrt(channel == 0u ? std::min(1.0f, 1.0f - pan)
                        : std::min(1.0f, 1.0f + pan));
                const auto* source = channels[!distributed && asset->channelCount == 1u
                    ? 0u : channel];
                for (uint32_t frame = 0u; frame < frameCount; ++frame) {
                    auto& output = outputs[first + channel][frame];
                    const float sample = source[frame] * balance;
                    slotPeaks_[slot] = std::max(slotPeaks_[slot], std::abs(sample));
                    output += sample * masterGain;
                    outputPeak_ = std::max(outputPeak_, std::abs(output));
                }
            }
        }
        advanceMotionClock(settings, frameCount);
    }

private:
    SampleVoiceRoute nextRoute(const SampleNeonSettings& settings, std::size_t slot) noexcept {
        const auto& control = settings.slots[slot];
        if (!neonDistributes(control)) return {};
        s3g::routing::VoiceOutputRouting routing;
        routing.width = static_cast<s3g::routing::OutputVoiceWidth>(control.family[NeonFamily::RoutingWidth]);
        routing.traversal = static_cast<s3g::routing::OutputTraversal>(control.family[NeonFamily::RoutingTraversal]);
        auto& allocator = control.noteRoutingAllocator ? *control.noteRoutingAllocator : allocators_[slot];
        const auto assignment = allocator.next(sampleNeonBusWidth(settings.outputLayout), routing);
        return {assignment.channelCount, assignment.firstChannel, assignment.secondChannel, 0xffu};
    }
    // Continuous Lanes/Wavesets are one object per pad gesture. Event-based
    // methods allocate in their RenderEvent instead, once per note/grain/slice.
    void routeContinuous(const SampleNeonSettings& settings, std::size_t slot,
        const SampleNeonEvent* events, std::size_t count, float* const* channels, uint32_t frames) noexcept {
        const unsigned sourceChannels = assets_[slot] ? assets_[slot]->channelCount : 0u;
        std::size_t next = 0;
        for (uint32_t frame = 0; frame < frames; ++frame) {
            while (next < count && events[next].frameOffset <= frame) {
                const auto& event = events[next++];
                if (event.slot != slot || event.kind != SampleNeonEventKind::Trigger) continue;
                continuousRoutingActive_[slot] = event.mode == controller::reloop_neon::Mode::Sampler;
                if (continuousRoutingActive_[slot]) gestureRoutes_[slot] = nextRoute(settings, slot);
            }
            if (!continuousRoutingActive_[slot]) continue;
            const auto route = gestureRoutes_[slot];
            if (!route.width) continue;
            float left = 0.f, right = 0.f;
            unsigned leftCount = 0u, rightCount = 0u;
            for (unsigned ch = 0; ch < sourceChannels; ++ch) {
                if (route.width == 2u && (ch & 1u)) { right += channels[ch][frame]; ++rightCount; }
                else { left += channels[ch][frame]; ++leftCount; }
            }
            left /= static_cast<float>(std::max(1u,leftCount));
            right = rightCount ? right / rightCount : left;
            for (unsigned ch = 0; ch < kMaximumAudioChannels; ++ch) channels[ch][frame] = 0.f;
            channels[route.first][frame] = left;
            if (route.width == 2u) channels[route.second][frame] = right;
        }
    }
    struct StackVoice {
        const SampleAsset* asset = nullptr;
        uint64_t noteId = 0u;
        unsigned layer = 0u;
        bool follow = false;
    };
    void rememberStackVoice(std::size_t slot, const RenderEvent& event, unsigned layer, bool follow) noexcept {
        auto& voices = stackVoices_[slot];
        auto& count = stackVoiceCounts_[slot];
        if (count == voices.size()) {
            for (unsigned n = 1u; n < count; ++n) voices[n - 1u] = voices[n];
            --count;
        }
        voices[count++] = {event.sourceAsset, event.noteId, layer, follow};
    }
    void updateStackVoices(std::size_t slot) noexcept {
        auto& voices = stackVoices_[slot];
        auto& count = stackVoiceCounts_[slot];
        const auto& player = players_[slot];
        unsigned kept = 0u;
        // Retain launch order only for voices that still exist. If a short
        // overlapping hit ends, follow the most recent surviving hit instead.
        for (unsigned n = 0u; player.activeVoiceCount() && n < count; ++n)
            for (unsigned cursor = 0u; cursor < player.voiceCursorCount(); ++cursor)
                if (voices[n].asset == player.voiceCursors()[cursor].sourceAsset
                    && voices[n].noteId == player.voiceCursors()[cursor].noteId) {
                    voices[kept++] = voices[n]; break;
                }
        count = kept;
    }
    static NeonStackLayer stackLayer(const SampleNeonSlotSettings& s, unsigned layer,
        const SampleAsset* fallback) noexcept {
        if (!s.stack || !s.stack->count) return {fallback, s.start, s.end};
        layer = std::min<unsigned>(layer, s.stack->count - 1u);
        auto result = s.stack->layers[layer];
        if (layer == s.selectedLayer) { result.start = s.start; result.end = s.end; }
        return result;
    }
    unsigned chooseStackLayer(const SampleNeonSlotSettings& s, float velocity, std::size_t slot) noexcept {
        const unsigned count = s.stack ? s.stack->count : 0u;
        if (!count) return 0u;
        unsigned layer = 0u;
        if (s.sourceMode == NeonSourceMode::Selected) layer = std::min<unsigned>(s.selectedLayer, count - 1u);
        else if (s.sourceMode == NeonSourceMode::Velocity) layer = neonVelocityLayer(velocity, count);
        else if (s.sourceMode == NeonSourceMode::Random)
            layer = std::min(count - 1u, static_cast<unsigned>(nextRandom01(stackSeeds_[slot]) * count));
        if(s.stack&&s.stack->skipEmptyLayers&&!s.stack->layers[layer].asset){
            const auto blend=neonStackBlend(count>1?double(layer)/(count-1):0.,s.stack);
            layer=blend.mix>.5f?blend.second:blend.first;}
        stackPositions_[slot] = count > 1u ? static_cast<float>(layer) / (count - 1u) : 0.0f;
        return layer;
    }
    double stackScan(const SampleNeonSettings& settings, std::size_t slot,
        uint64_t age, uint32_t frame) noexcept {
        const auto& s = settings.slots[slot];
        const double phase = s.clock == SampleNeonClock::Host
            ? motionBeatAtFrame(settings, frame) / std::clamp<double>(s.stackCycleBeats, 0.25, 32.0)
            : static_cast<double>(age) / sampleRate_ / std::clamp<double>(s.stackCycleSeconds, 0.05, 30.0);
        const double clock = s.family[NeonFamily::StackAdvance] != 0 && s.playback == SampleNeonPlayback::Grains
            ? static_cast<double>(grainEmitters_[slot].serial) / 32.0 : phase;
        // Avoid float rounding to 1: the wrapped graph would jump to its start
        // while the cursor is still at the right edge of this cycle.
        stackPathPhases_[slot] = std::min(std::nextafter(1.f, 0.f),
            static_cast<float>(neonUnitPhase(clock + s.family[NeonFamily::StackOffset])));
        return neonStackPath(s.family, clock);
    }
    double cuePosition(std::size_t slot, double normal, double alpha) noexcept {
        auto& position = stackCuePositions_[slot];
        const double target = stackCueTargets_[slot] >= 0 ? stackCueTargets_[slot] : normal;
        if (stackCueTargets_[slot] >= 0 || stackCueReturning_[slot]) {
            position += (target - position) * alpha;
            if (stackCueTargets_[slot] < 0 && (std::abs(position - normal) < .00001
                || !stackCueReturnFrames_[slot] || --stackCueReturnFrames_[slot] == 0)) stackCueReturning_[slot] = false;
        } else position = normal;
        return std::clamp(position, 0.0, 1.0);
    }
    struct GrainEmitter {
        unsigned layer = 0u;
        bool selectedSource = false;
        bool active = false;
        bool ownsOutput = false;
        SampleNeonClock clock = SampleNeonClock::Free;
        TriggerMode trigger = TriggerMode::OneShot;
        uint64_t noteId = 0u;
        double framesUntilNext = 0.0;
        uint64_t ageFrames = 0u;
        uint64_t durationFrames = 0u;
        uint32_t releaseFrames = 0u;
        uint32_t releaseTotal = 1u, attackFrames = 1u, fadeOutFrames = 1u;
        float envelope = 0.0f, releaseLevel = 0.0f;
        uint64_t serial = 0u;
        unsigned sequenceIndex = 0u;
        unsigned mosaicIndex = 0u;
        double sourcePhase = 0;
        float pressure = 0;
        double heldPosition = 0.0, eventPosition = 0.0;
        bool doubletPending = false;
        double doubletPosition = 0.0;
        uint64_t seed = 0x9e3779b97f4a7c15ull;
        float velocity = 1.0f;
    };

    void insertTechniqueVoice(const SampleNeonSlotSettings& control, GrainEmitter& emitter,
        RenderEvent grain, std::array<RenderEvent, kSampleNeonMaximumBlockEvents>& events, std::size_t& count) noexcept {
        const bool independent = control.playback == SampleNeonPlayback::Grains
            && control.sourceFormat == SampleNeonSourceFormat::Discrete
            && control.family[NeonFamily::GrainStereoLink] != 0
            && grain.sourceAsset && grain.sourceAsset->channelCount == 2u;
        if (!independent) { insertEvent(events,count,grain); return; }
        if (count + 2u > events.size()) return;
        auto right = grain;
        grain.route.sourceChannel = 0u; right.route.sourceChannel = 1u;
        right.noteId ^= 0x0400000000000000ull;
        if (grain.route.width == 2u) {
            grain.route.width = right.route.width = 1u;
            right.route.first = right.route.second;
        } else if (grain.route.width == 1u) {
            grain.sourceGain *= .5f; right.sourceGain *= .5f;
        }
        const double random = nextRandom01(emitter.seed)*2-1;
        const double bias = control.family[NeonFamily::GrainBias] == 0 ? -std::abs(random)
            : control.family[NeonFamily::GrainBias] == 2 ? std::abs(random) : random;
        right.startOffsetNormalized = std::clamp(grain.startOffsetNormalized + bias*control.grainSpray*grain.sourceLength,
            0., std::max(0.,grain.sourceLength-grain.windowLengthNormalized));
        const double pitch = control.family[NeonFamily::GrainPitch]
            + (nextRandom01(emitter.seed)*2-1)*control.grainPitchSpraySemitones;
        const int semitones = static_cast<int>(std::lround(pitch));
        right.key = static_cast<uint8_t>(std::clamp(60+semitones,0,127));
        right.fineTuneOffsetCents = static_cast<float>((pitch-semitones)*100);
        right.playModeOverride = static_cast<uint8_t>(nextRandom01(emitter.seed)<control.grainReverseChance ? PlayMode::Reverse : PlayMode::Forward);
        insertEvent(events,count,grain); insertEvent(events,count,right);
    }

    static float decibelsToLinear(float decibels) noexcept
    {
        return std::pow(10.0f, std::clamp(decibels, -60.0f, 12.0f)
            * 0.05f);
    }

    float effectiveMangle(const SampleNeonSettings& settings,
        std::size_t slot) const noexcept
    {
        const auto& selected = settings.slots[slot];
        return std::clamp(settings.globalMangle + selected.mangle
            + pressure_[slot] * selected.pressureDepth, 0.0f, 1.0f);
    }

    static float hashBipolar(uint64_t noteId, std::size_t slot) noexcept
    {
        uint64_t value = noteId
            ^ (static_cast<uint64_t>(slot) + 0x9e3779b97f4a7c15ull);
        value ^= value >> 30u;
        value *= 0xbf58476d1ce4e5b9ull;
        value ^= value >> 27u;
        value *= 0x94d049bb133111ebull;
        value ^= value >> 31u;
        const uint32_t low = static_cast<uint32_t>(value & 0x00ffffffu);
        return static_cast<float>(low) / 8388607.5f - 1.0f;
    }

    static double resolvedMotionValue(SampleNeonMotionPath path,
        double phase, std::size_t slot) noexcept
    {
        phase -= std::floor(phase);
        switch (path) {
        case SampleNeonMotionPath::Reverse: return 1.0 - phase;
        case SampleNeonMotionPath::RoundTrip:
            return phase < 0.5 ? phase * 2.0 : 2.0 - phase * 2.0;
        case SampleNeonMotionPath::Drunk: {
            const double a = std::sin((phase + slot * 0.071) * 6.28318530718);
            const double b = std::sin((phase * 2.71 + slot * 0.113)
                * 6.28318530718);
            return std::clamp(0.5 + a * 0.31 + b * 0.19, 0.0, 1.0);
        }
        case SampleNeonMotionPath::Forward: return phase;
        }
        return phase;
    }

    double scanPosition(const SampleNeonSettings& settings, std::size_t slot,
        uint64_t ageFrames, uint32_t frameOffset) const noexcept
    {
        const auto& control = settings.slots[slot];
        if (control.playback == SampleNeonPlayback::Stretch) {
            const double seconds = control.clock == SampleNeonClock::Host
                ? control.motionCycleBeats * 60.0 / std::clamp<double>(settings.hostTempoBpm, 20.0, 999.0)
                : control.motionCycleSeconds;
            const double phase = static_cast<double>(ageFrames) / sampleRate_ / std::clamp(seconds, 0.05, 96.0);
            const double shifted=phase+(control.deckTransport?control.launchPosition:0);
            const double position = control.scrubbed(shifted - std::floor(shifted), frameOffset);
            return control.direction == 1u ? 1.0 - position : position;
        }
        if (control.playback != SampleNeonPlayback::Motion)
            return std::clamp(control.launchPosition, 0.0, 1.0);
        const double phase = control.clock == SampleNeonClock::Host
            ? motionBeatAtFrame(settings, frameOffset) / std::clamp<double>(control.motionCycleBeats, 0.25, 32.0)
            : static_cast<double>(ageFrames) / sampleRate_ / std::clamp<double>(control.motionCycleSeconds, 0.05, 30.0);
        const double seconds = control.clock == SampleNeonClock::Host
            ? motionBeatAtFrame(settings, frameOffset) * 60.0 / settings.hostTempoBpm : ageFrames / sampleRate_;
        return control.scrubbed(motionTrajectory(control, phase, seconds, slot), frameOffset);
    }

    void synchronizeMotionClock(const SampleNeonSettings& settings) noexcept
    {
        if (!settings.transportPlaying || !settings.hostBeatValid) return;
        motionBeatPosition_ = settings.hostBeatPosition;
    }

    void advanceMotionClock(const SampleNeonSettings& settings,
        uint32_t frameCount) noexcept
    {
        if (!settings.transportPlaying) return;
        const double beatDelta = static_cast<double>(frameCount) / sampleRate_
            * std::clamp<double>(settings.hostTempoBpm, 20.0, 999.0) / 60.0;
        if (settings.hostBeatValid)
            motionBeatPosition_ = settings.hostBeatPosition + beatDelta;
        else
            motionBeatPosition_ += beatDelta;
    }

    double motionBeatAtFrame(const SampleNeonSettings& settings,
        uint32_t frameOffset) const noexcept
    {
        if (!settings.transportPlaying) return motionBeatPosition_;
        return motionBeatPosition_ + static_cast<double>(frameOffset)
            / sampleRate_ * std::clamp<double>(
                settings.hostTempoBpm, 20.0, 999.0) / 60.0;
    }

    void applyLaunchPosition(const SampleNeonSlotSettings& control,
        const SampleNeonSlotSettings& target, std::size_t controllerSlot,
        double beatPosition, bool transportPlaying,
        RenderEvent& event) const noexcept
    {
        const double range = std::max(0.0,
            std::clamp(target.end, 0.0, 1.0)
                - std::clamp(target.start, 0.0, 1.0));
        if (!(range > 0.0)) return;
        (void)beatPosition; (void)controllerSlot; (void)transportPlaying;
        const double launch = std::clamp(control.launchPosition, 0.0, 1.0);
        const auto mode = event.playModeOverride
                <= static_cast<uint8_t>(PlayMode::ReversePingPong)
            ? static_cast<PlayMode>(event.playModeOverride)
            : PlayMode::Forward;
        const bool reverse = mode == PlayMode::Reverse
            || mode == PlayMode::ReverseLoop
            || mode == PlayMode::ReversePingPong;
        // Position zero means the natural beginning of playback in either
        // direction, so Censor/reverse still starts at the selected End.
        const double directionalLaunch = reverse ? 1.0 - launch : launch;
        const double targetOffset = std::clamp(range * directionalLaunch
                + event.startOffsetNormalized,
            0.0, range);
        const double requested = event.windowLengthNormalized;
        if (reverse) {
            const double available = std::max(1.0e-9, targetOffset);
            const double window = requested > 0.0
                ? std::min(requested, available) : available;
            event.startOffsetNormalized = std::max(
                0.0, targetOffset - window);
            event.windowLengthNormalized = window;
        } else {
            const double available = std::max(
                1.0e-9, range - targetOffset);
            event.startOffsetNormalized = targetOffset;
            event.windowLengthNormalized = requested > 0.0
                ? std::min(requested, available) : available;
        }
    }

    static double nextRandom01(uint64_t& state) noexcept
    {
        state ^= state >> 12u; state ^= state << 25u; state ^= state >> 27u;
        return static_cast<double>((state * 2685821657736338717ull) >> 11u)
            * (1.0 / 9007199254740992.0);
    }

    void resetSlotProcessing(std::size_t slot) noexcept
    {
        stackCueTargets_[slot] = -1; stackCueReturning_[slot] = false;
        grainEmitters_[slot] = {};
        stackVoiceCounts_[slot] = 0u;
        effects_[slot].reset(); wavesets_[slot].reset(); waveActive_[slot] = false;
        waveScans_[slot].reset(); waveCursorCounts_[slot] = 0u;
        lanes_[slot].reset();
        framePlayers_[slot].reset();
        cutups_[slot].reset();
    }

#include "s3g_sample_neon_cutups.inc"

    void renderWavesets(const SampleNeonSettings& settings, std::size_t slot,
        const SampleNeonEvent* input, std::size_t count, float* const* channels, uint32_t frames) noexcept
    {
        const auto& control = settings.slots[slot];
        WavesetSettings s;
        const auto primary = stackLayer(control, 0u, assets_[slot]);
        s.preserveSourceChannels = true; s.activeOutputChannelCount = 16u;
        s.start = primary.start; s.end = primary.end; s.loopStart = s.start; s.loopEnd = s.end;
        s.playMode = control.direction == 1u ? WavesetPlayMode::Reverse : WavesetPlayMode::Forward;
        if (control.repeat || control.triggerMode == TriggerMode::Gate || control.triggerMode == TriggerMode::Toggle)
            s.playMode = control.direction == 1u ? WavesetPlayMode::ReverseLoop : WavesetPlayMode::ForwardLoop;
        s.triggerMode = control.triggerMode; s.tuneSemitones = control.tuneSemitones;
        s.velocitySensitivity = control.velocityEnabled ? 1.0f : 0.0f;
        s.outputGainDecibels = control.gainDecibels;
        s.groupSize = 1u + static_cast<unsigned>(std::lround(control.technique[0] * 31.0f));
        s.repeats = 1u + static_cast<unsigned>(std::lround(control.technique[1] * 15.0f));
        s.shape = static_cast<WavesetShape>(std::clamp<int>(static_cast<int>(std::lround(control.technique[2] * 11.0f)), 0, 11));
        s.processAmount = control.technique[3];
        if (control.sourceMode == NeonSourceMode::Scan || control.deckWavesetSources) {
            wavesets_[slot].reset();
            waveActive_[slot] = grainEmitters_[slot].ownsOutput;
            if (control.sourceFormat == SampleNeonSourceFormat::Ambisonic) {
                waveScans_[slot].reset(); waveCursorCounts_[slot] = 0u;
                for (unsigned ch = 0u; ch < 16u; ++ch) std::fill_n(channels[ch], frames, 0.0f);
                return;
            }
            double launch=control.deckTransport?control.launchPosition:-1;
            for(std::size_t n=0;n<count;++n)if(input[n].slot==slot&&input[n].transportPosition){
                const auto& p=*input[n].transportPosition;const auto layer=stackLayer(control,p.sourceLayer,assets_[slot]);
                launch=std::clamp((p.sourcePosition-layer.start)/std::max(1.e-9,layer.end-layer.start),0.,1.);}
            waveScans_[slot].render(s, control.stack, control.selectedLayer, control.start, control.end,
                waveScanVelocities_.data(), waveScanPositions_.data(), waveScanRetriggers_.data(), channels, frames, control.sourceScrub,control.deckScanPositions,launch);
            waveCursors_[slot] = waveScans_[slot].cursors();
            waveCursorCounts_[slot] = waveScans_[slot].cursorCount();
            return;
        }
        waveScans_[slot].reset();
        const bool ready = control.sourceFormat != SampleNeonSourceFormat::Ambisonic && waveMaps_[slot]
            && waveMaps_[slot]->asset.get() == assets_[slot];
        std::array<WavesetRenderEvent, kSampleNeonMaximumBlockEvents> events {};
        std::size_t total = 0u;
        uint32_t begin = 0u;
        const auto renderTo = [&](uint32_t end) {
            if (waveActive_[slot] && end > begin) {
                std::array<float*, 16u> segment {};
                for (unsigned ch = 0u; ch < 16u; ++ch) segment[ch] = channels[ch] + begin;
                if (ready) wavesets_[slot].render(s, events.data(), total, segment.data(), 16u, end - begin);
                else {
                    wavesets_[slot].reset();
                    for (auto* channel : segment) std::fill_n(channel, end - begin, 0.0f);
                }
            }
            begin = end; total = 0u;
        };
        for (std::size_t n = 0u; n < count; ++n) {
            const auto& event = input[n];
            if (event.slot >= kSampleNeonSlotCount || event.kind == SampleNeonEventKind::Pressure
                || event.kind == SampleNeonEventKind::StackPosition) continue;
            const uint32_t at = std::clamp(event.frameOffset, begin, frames);
            if (event.slot != slot) {
                if (waveActive_[slot] && control.chokeGroup && settings.slots[event.slot].chokeGroup == control.chokeGroup
                    && event.kind == SampleNeonEventKind::Trigger)
                    events[total++] = {at - begin, WavesetEventKind::StopAll, 0u, 60u, 0.0f, 0u};
                continue;
            }
            if (event.kind == SampleNeonEventKind::Trigger) {
                const bool next = event.mode == controller::reloop_neon::Mode::Sampler;
                if (waveActive_[slot] != next) {
                    // CHOP remains a direct sample audition. Preserve both
                    // sides of a mode transition within the same audio block.
                    renderTo(at); wavesets_[slot].reset(); waveActive_[slot] = next;
                }
            }
            if (waveActive_[slot]) events[total++] = { at - begin,
                event.kind == SampleNeonEventKind::Trigger ? WavesetEventKind::NoteOn
                    : event.kind == SampleNeonEventKind::Release ? WavesetEventKind::NoteOff : WavesetEventKind::StopAll,
                event.noteId, 60u, event.value, 0u };
        }
        renderTo(frames);
        const auto& cursors = wavesets_[slot].voiceCursors();
        waveCursorCounts_[slot] = wavesets_[slot].voiceCursorCount();
        for (unsigned n = 0u; n < wavesets_[slot].voiceCursorCount(); ++n) {
            waveCursors_[slot][n] = { cursors[n].sourcePositionNormalized, cursors[n].key,
                static_cast<float>(s.start), static_cast<float>(s.end), cursors[n].identity, primary.asset };
            waveCursors_[slot][n].layer = 0u;
        }
    }

    static void insertEvent(std::array<RenderEvent, kSampleNeonMaximumBlockEvents>& events,
        std::size_t& count, const RenderEvent& event) noexcept
    {
        if (count >= events.size()) return;
        std::size_t index = count++;
        while (index && events[index - 1u].frameOffset > event.frameOffset) {
            events[index] = events[index - 1u]; --index;
        }
        events[index] = event;
    }

    static double sortedGrainPosition(const NeonStackLayer& source, double position, unsigned count, float amount) noexcept
    {
        count = std::clamp(count, 2u, 32u);
        std::array<std::pair<double, unsigned>, 32u> ranks {};
        // Bounded, channel-linked energy ranking. No allocation or PCM analysis
        // cache is created in process(); all channels contribute equally.
        for (unsigned n = 0; n < count; ++n) {
            double energy = 0;
            for (unsigned probe = 0; probe < 4; ++probe) {
                const double phase = source.start + (source.end - source.start) * (n + (probe + .5) / 4) / count;
                const auto at = std::min<std::size_t>(source.asset->frameCount() - 1, static_cast<std::size_t>(phase * source.asset->frameCount()));
                for (const auto& channel : source.asset->channels) if (at < channel.size()) energy += channel[at] * channel[at];
            }
            ranks[n] = {energy, n};
        }
        std::sort(ranks.begin(), ranks.begin() + count);
        const unsigned index = std::min(count - 1, static_cast<unsigned>(position * count));
        const double sorted = (ranks[index].second + neonUnitPhase(position * count)) / count;
        return position + (sorted - position) * amount;
    }

    void appendTechniqueRange(const SampleNeonSettings& settings, std::size_t slot,
        uint32_t begin, uint32_t end,
        std::array<RenderEvent, kSampleNeonMaximumBlockEvents>& events, std::size_t& count) noexcept
    {
        auto& emitter = grainEmitters_[slot];
        const auto& control = settings.slots[slot];
        const bool motion = control.playback == SampleNeonPlayback::Motion;
        const bool stretch = control.playback == SampleNeonPlayback::Stretch;
        const bool sequence = control.playback == SampleNeonPlayback::SliceSequence;
        const bool lanes = control.playback == SampleNeonPlayback::Lanes;
        const bool spectral = control.playback == SampleNeonPlayback::Spectral;
        const bool oscillator = control.playback == SampleNeonPlayback::Wavesets && control.family[NeonFamily::WavesetEngine] != 0;
        const bool waveScan = control.playback == SampleNeonPlayback::Wavesets || lanes || spectral;
        const bool running = control.clock == SampleNeonClock::Free || settings.transportPlaying;
        const auto& f = control.family;
        const unsigned motionModel = motion ? static_cast<unsigned>(f[NeonFamily::MotionModel]) : 0u;
        const bool grains = control.playback == SampleNeonPlayback::Grains;
        const unsigned process = grains ? static_cast<unsigned>(f[NeonFamily::GrainProcess]) : 0u;
        const double density = motionModel ? f[NeonFamily::EventRate] : motion || stretch ? 50.0
            : sequence && control.clock == SampleNeonClock::Free ? 1.0 + control.technique[0] * 19.0
            : control.clock == SampleNeonClock::Free ? std::clamp<double>(control.grainDensityHz, 1.0, 80.0)
            : std::clamp<double>(settings.hostTempoBpm, 20.0, 999.0) / 60.0
                / std::clamp<double>(control.grainIntervalBeats, 0.03125, 4.0);
        const double interval = sampleRate_ / std::clamp(density * (grains ? f[NeonFamily::GrainDensityScale] : 1), 0.1, 160.0);
        const double cueAlpha = 1-std::exp(-1.0/(sampleRate_*std::clamp<double>(f[NeonFamily::LaneSlew],.001,.1)));
        for (uint32_t frame = begin; frame < end; ++frame) {
            if (waveScan) waveScanPositions_[frame] = emitter.ownsOutput ? -2.0f : -1.0f;
            if (!emitter.ownsOutput) { techniqueEnvelope_[frame] = 1.0f; continue; }
            techniqueEnvelope_[frame] = 0.0f;
            if (!emitter.active) continue;
            if (!running && !emitter.releaseFrames) {
                if (waveScan) waveScanPositions_[frame] = -3.0f;
                continue; // HOST pauses, FREE never follows transport.
            }
            const bool timed = emitter.durationFrames != UINT64_MAX;
            if (timed && emitter.ageFrames >= emitter.durationFrames) {
                emitter.active = false;
                insertEvent(events, count, RenderEvent { frame, EventKind::Choke, 0u, 0xffu });
                continue;
            }
            double envelope = std::min(1.0, static_cast<double>(emitter.ageFrames + 1u) / emitter.attackFrames);
            if (timed) envelope = std::min(envelope,
                static_cast<double>(emitter.durationFrames - emitter.ageFrames) / emitter.fadeOutFrames);
            if (emitter.releaseFrames) envelope = emitter.releaseLevel
                * static_cast<double>(emitter.releaseFrames) / emitter.releaseTotal;
            emitter.envelope = static_cast<float>(envelope);
            techniqueEnvelope_[frame] = static_cast<float>(envelope);
            if (motion) {
                const double seconds = control.clock == SampleNeonClock::Host
                    ? motionBeatAtFrame(settings, frame) * 60.0 / std::clamp<double>(settings.hostTempoBpm, 20, 999)
                    : emitter.ageFrames / sampleRate_;
                techniqueEnvelope_[frame] *= neonMotionArticulation(f, seconds);
            }
            const bool liveStack = stackCueTargets_[slot] >= 0 || stackCueReturning_[slot];
            const double ordinaryStack = !waveScan && !liveStack ? stackPositions_[slot] : control.stack && control.stack->count > 1u
                ? !emitter.selectedSource && (control.sourceMode == NeonSourceMode::Scan || lanes)
                    ? lanes && f[NeonFamily::LaneAuto] == 0 ? f[NeonFamily::LanePosition]
                        : stackScan(settings, slot, emitter.ageFrames, frame)
                    : static_cast<double>(emitter.layer) / (control.stack->count - 1u) : 0.0;
            const double livePosition = cuePosition(slot, ordinaryStack, cueAlpha);
            if (waveScan) {
                double position = liveStack ? livePosition : ordinaryStack;
                if (!liveStack && !lanes && f[NeonFamily::StackJump] != 0 && control.stack && control.stack->count > 1u)
                    position = std::round(position * (control.stack->count - 1u)) / (control.stack->count - 1u);
                waveScanPositions_[frame] = static_cast<float>(position);
                waveScanVelocities_[frame] = emitter.velocity;
                stackPositions_[slot] = static_cast<float>(position);
                if (spectral || oscillator) {
                    const double base = spectral ? control.launchPosition : f[NeonFamily::OscPosition];
                    frameSourcePositions_[frame] = static_cast<float>(emitter.sourcePhase == 0 ? base : neonUnitPhase(base + emitter.sourcePhase));
                    frameSourcePositions_[frame] = static_cast<float>(control.scrubbed(frameSourcePositions_[frame],frame));
                    const double advance = spectral ? std::clamp<double>(f[NeonFamily::SpectralAdvance]
                        + emitter.pressure * f[NeonFamily::SpectralPressure], 0, 1) : f[NeonFamily::OscScan];
                    const double seconds = control.clock == SampleNeonClock::Host
                        ? control.motionCycleBeats * 60. / std::clamp<double>(settings.hostTempoBpm, 20, 999) : control.motionCycleSeconds;
                    emitter.sourcePhase = neonUnitPhase(emitter.sourcePhase + advance / (sampleRate_ * std::max(.05, seconds)));
                }
            }
            if (!waveScan && emitter.framesUntilNext <= 0.0
                && assets_[slot] && count + 1u < events.size()) {
                const bool scan = ((!emitter.selectedSource && control.sourceMode == NeonSourceMode::Scan) || liveStack) && control.stack
                    && (motion || stretch || control.playback == SampleNeonPlayback::Grains);
                const double positionInStack = scan ? liveStack ? livePosition : stackScan(settings, slot, emitter.ageFrames, frame) : 0.0;
                auto blend = scan ? neonStackBlend(positionInStack, control.stack)
                    : NeonStackBlend {emitter.layer, emitter.layer, 0.0f};
                if(sequence&&control.deckPerformanceLayer>=0){const unsigned layer=static_cast<unsigned>(control.deckPerformanceLayer);blend={layer,layer,0};}
                if (scan && !liveStack && f[NeonFamily::StackJump] != 0) {
                    const unsigned layer = blend.mix >= .5f ? blend.second : blend.first;
                    blend = {layer, layer, 0};
                }
                if (scan) stackPositions_[slot] = !liveStack && f[NeonFamily::StackJump] != 0 && control.stack->count > 1u
                    ? static_cast<float>(blend.first) / (control.stack->count - 1u) : static_cast<float>(positionInStack);
                const auto source = stackLayer(control, blend.first, assets_[slot]);
                const double range = std::max(0.0, source.end - source.start);
                // Missing sources remain silent, but must not stall the held
                // gesture's scan clock or release envelope.
                if (source.asset && range > 0.0) {
                RenderEvent grain;
                grain.sourceAsset = source.asset; grain.sourceStart = source.start; grain.sourceLength = range;
                grain.sourceLayer = static_cast<uint8_t>(blend.first);
                grain.frameOffset = frame; grain.kind = EventKind::NoteOn;
                grain.noteId = 0x7000000000000000ull | (static_cast<uint64_t>(slot) << 48u) | (++emitter.serial & 0x0000ffffffffffffull);
                grain.key = 60u; grain.velocity = emitter.velocity;
                grain.triggerModeOverride = static_cast<uint8_t>(TriggerMode::OneShot);
                grain.syncModeOverride = static_cast<uint8_t>(SyncMode::Free);
                const double duration = source.asset->frameCount() / source.asset->sampleRate;
                double windowSeconds = motion ? f[NeonFamily::MotionWindow] * .001 : stretch ? .04
                    : control.grainSizeMs * .001 * f[NeonFamily::GrainSizeScale];
                const unsigned repetitions = 1u + static_cast<unsigned>(f[NeonFamily::GrainAmount] * 7);
                if (grains && f[NeonFamily::GrainSizeVariation] > 0)
                    windowSeconds *= 1 + (nextRandom01(emitter.seed) * 2 - 1) * f[NeonFamily::GrainSizeVariation] * .95;
                if (process == 3u) windowSeconds *= std::pow(.65, (emitter.serial - 1u) % repetitions);
                const double window = std::min(range, std::clamp(windowSeconds, .001, 4.0)
                    * (stretch ? std::pow(2.0, control.tuneSemitones / 12.0) : 1.0) / duration);
                const double random = motion || stretch || sequence ? 0.0 : nextRandom01(emitter.seed) * 2.0 - 1.0;
                double position = scanPosition(settings, slot, emitter.ageFrames, frame);
                if (grains) {
                    const unsigned sourceMode = static_cast<unsigned>(f[NeonFamily::GrainSource]);
                    if (sourceMode == 1u) {
                        const double phase = control.clock == SampleNeonClock::Host
                            ? motionBeatAtFrame(settings, frame) / control.motionCycleBeats
                            : emitter.ageFrames / sampleRate_ / control.motionCycleSeconds;
                        position = resolvedMotionValue(control.motionPath, phase + control.launchPosition, slot);
                    } else if (sourceMode == 2u) position = nextRandom01(emitter.seed);
                    else if (sourceMode == 3u) {
                        const unsigned regions = static_cast<unsigned>(f[NeonFamily::GrainRegions]);
                        position = static_cast<double>((emitter.serial - 1u) % regions) / regions;
                    }
                    const double bias = f[NeonFamily::GrainBias] == 0 ? -std::abs(random)
                        : f[NeonFamily::GrainBias] == 2 ? std::abs(random) : random;
                    position = std::clamp(position + bias * control.grainSpray, 0.0, 1.0);
                    if (process == 1u) position = sortedGrainPosition(source, position,
                        static_cast<unsigned>(f[NeonFamily::GrainRegions]), f[NeonFamily::GrainAmount]);
                    if (process == 2u || process == 3u) {
                        if ((emitter.serial - 1u) % repetitions == 0) emitter.heldPosition = position;
                        position = emitter.heldPosition;
                    }
                    if (process == 4u && emitter.doubletPending) {
                        position = emitter.doubletPosition;
                        if (f[NeonFamily::GrainTimeSync] != 0)
                            position = std::clamp(position + interval * .5 / sampleRate_ / std::max(.000001, duration * range), 0.0, 1.0);
                    }
                }
                if (motion && f[NeonFamily::MotionJitter] > 0)
                    position = std::clamp(position + (nextRandom01(emitter.seed) * 2 - 1) * f[NeonFamily::MotionJitter] * f[NeonFamily::MotionField], 0.0, 1.0);
                if (motionModel) {
                    const unsigned repeats = static_cast<unsigned>(f[NeonFamily::EventRepeats]);
                    const unsigned step = static_cast<unsigned>((emitter.serial - 1u) % repeats);
                    if (step == 0 && motionModel != 1u) emitter.eventPosition = position;
                    position = std::clamp(emitter.eventPosition + (motionModel == 2u ? step * f[NeonFamily::EventStep] : 0), 0.0, 1.0);
                }
                // Motion/Stretch already apply the platter in scanPosition().
                if (!motion && !stretch) position=control.scrubbed(position,frame);
                emitter.doubletPosition = position;
                grain.startOffsetNormalized = std::min(range * position, std::max(0.0, range - window));
                grain.windowLengthNormalized = window;
                const double pitch = motionModel && f[NeonFamily::EventPitch] > 0
                    ? (nextRandom01(emitter.seed) * 2 - 1) * f[NeonFamily::EventPitch]
                    : motion || stretch || sequence ? 0.0 : f[NeonFamily::GrainPitch]
                        + (nextRandom01(emitter.seed) * 2.0 - 1.0) * control.grainPitchSpraySemitones;
                const float levelVariation = motionModel ? f[NeonFamily::EventLevel] : grains ? f[NeonFamily::GrainLevelVariation] : 0;
                if (levelVariation > 0) grain.sourceGain *= static_cast<float>(1 - nextRandom01(emitter.seed) * levelVariation);
                if (grains && f[NeonFamily::GrainWindow] > 0) {
                    grain.grainWindow = static_cast<uint8_t>(f[NeonFamily::GrainWindow] - 1);
                    grain.grainSkew = f[NeonFamily::GrainSkew];
                }
                const int semitones = static_cast<int>(std::lround(pitch));
                grain.key = static_cast<uint8_t>(std::clamp(60 + semitones, 0, 127));
                grain.fineTuneOffsetCents = static_cast<float>((pitch - semitones) * 100.0);
                grain.gainOffsetDecibels = sequence ? 0.0f : motion || stretch ? -3.0f
                    : -std::clamp(static_cast<float>(3.0 + density * 0.16), 3.0f, 12.0f);
                const bool reverse = sequence || stretch ? control.direction == 1u
                    : motion ? (control.motionPath == SampleNeonMotionPath::Reverse) != (motionModel == 4u && (emitter.serial % 2u == 0))
                    : nextRandom01(emitter.seed) < control.grainReverseChance;
                grain.playModeOverride = static_cast<uint8_t>(reverse ? PlayMode::Reverse : PlayMode::Forward);
                if (sequence) {
                    grain.envelope = neonSliceEnvelope(f);
                    // Fit the envelope to this actual step (including the
                    // fractional scheduler remainder), not an arbitrarily
                    // long slice. Keep a bounded, channel-coherent overlap.
                    grain.stepDurationFrames = static_cast<uint32_t>(std::max(1.0,
                        std::ceil(interval + emitter.framesUntilNext)));
                    grain.edgeFadeFrames = static_cast<uint32_t>(std::max(2.0,
                        std::min(std::round(sampleRate_ * .005),
                            std::floor(interval * .25))));
                    const bool primaryMap = control.deckPerformanceLayer<0 && control.stack && control.selectedLayer != 0u && !emitter.selectedSource;
                    const auto slices = std::clamp<unsigned>(primaryMap ? control.stack->primarySliceCount : control.sliceCount, 1u, 32u);
                    const auto repeats = 1u + static_cast<unsigned>(std::lround(control.technique[2] * 7.0f));
                    unsigned index = static_cast<unsigned>((emitter.serial - 1u) / repeats % slices);
                    if(control.deckTransport){unsigned first=0;while(first+1<slices&&control.sliceLayout.boundaries[first+1]<=control.launchPosition)++first;index=(index+first)%slices;}
                    if (control.technique[1] > 0.75f) {
                        if ((emitter.serial - 1u) % repeats == 0u)
                            emitter.sequenceIndex = std::min(slices - 1u, static_cast<unsigned>(nextRandom01(emitter.seed) * slices));
                        index = emitter.sequenceIndex;
                    }
                    else if (control.technique[1] >= 0.25f) index = slices - 1u - index;
                    auto layout = control.sliceLayout.valid() && control.sliceLayout.sliceCount == slices
                        ? control.sliceLayout : equalSampleNeonSliceLayout(slices);
                    if (primaryMap) { layout.sliceCount = static_cast<uint8_t>(slices); layout.boundaries = control.stack->primarySlices; }
                    if(control.sourceScrub>=0 || control.deckScanPositions){const double at=control.scrubbed(double(index)/slices,frame);index=0;while(index+1<slices&&layout.boundaries[index+1]<=at)++index;}
                    grain.startOffsetNormalized = range * layout.boundaries[index];
                    grain.windowLengthNormalized = range * (layout.boundaries[index + 1u] - layout.boundaries[index]);
                    if (f[NeonFamily::MosaicMode] != 0 && control.stack && control.stack->fragmentCount) {
                        const auto& stack = *control.stack;
                        if ((emitter.serial - 1) % repeats == 0)
                            emitter.mosaicIndex = neonChooseMosaic(stack, emitter.mosaicIndex,
                                static_cast<unsigned>(f[NeonFamily::MosaicMode]), f[NeonFamily::MosaicScope] != 0, f[NeonFamily::MosaicTarget]);
                        emitter.mosaicIndex = std::min(emitter.mosaicIndex, stack.fragmentCount - 1);
                        const auto& fragment = stack.fragments[emitter.mosaicIndex];
                        grain.sourceAsset = stack.layers[fragment.layer].asset;
                        grain.sourceStart = fragment.start; grain.sourceLength = fragment.end - fragment.start;
                        grain.startOffsetNormalized = 0; grain.windowLengthNormalized = grain.sourceLength;
                        grain.sourceLayer = fragment.layer; emitter.layer = fragment.layer;
                    }
                    RenderEvent handoff {frame, EventKind::Choke};
                    handoff.chokeFadeFrames = grain.edgeFadeFrames;
                    insertEvent(events, count, handoff);
                }
                if (!sequence || nextRandom01(emitter.seed) < control.technique[3]) {
                    grain.route = nextRoute(settings, slot);
                    // Overlapping voices retain their own immutable source.
                    // Linear source weights preserve correlated channel fields.
                    if (scan && blend.first != blend.second && blend.mix > 0.0f) {
                        auto other = grain;
                        const auto b = stackLayer(control, blend.second, assets_[slot]);
                        if (b.asset) {
                            other.sourceAsset = b.asset; other.sourceStart = b.start; other.sourceLength = b.end - b.start;
                            other.sourceLayer = static_cast<uint8_t>(blend.second);
                            const double bDuration = b.asset->frameCount() / b.asset->sampleRate;
                            const double bWindow = std::min(other.sourceLength, std::clamp(windowSeconds, .001, 4.0)
                                * (stretch ? std::pow(2.0, control.tuneSemitones / 12.0) : 1.0) / bDuration);
                            other.startOffsetNormalized = std::min(other.sourceLength * position, std::max(0.0, other.sourceLength - bWindow));
                            other.windowLengthNormalized = bWindow;
                            other.noteId ^= 0x0800000000000000ull;
                            other.sourceGain *= blend.mix;
                            insertTechniqueVoice(control, emitter, other, events, count);
                        }
                        grain.sourceGain *= 1.0f - blend.mix;
                    }
                    insertTechniqueVoice(control, emitter, grain, events, count);
                }
                }
                double spacing = interval;
                if (process == 4u) {
                    if (emitter.doubletPending) { emitter.doubletPending = false; spacing *= .5; }
                    else if (nextRandom01(emitter.seed) < f[NeonFamily::GrainAmount]) {
                        emitter.doubletPending = true; spacing *= .5;
                    }
                } else emitter.doubletPending = false;
                if (grains && f[NeonFamily::GrainScatter] > 0)
                    spacing *= 1 + (nextRandom01(emitter.seed) * 2 - 1) * f[NeonFamily::GrainScatter] * .9;
                if (motionModel == 3u) spacing *= emitter.serial % 2u ? .35 : 1.65;
                if (motionModel && f[NeonFamily::EventCurve] != 0) {
                    const double step = static_cast<double>((emitter.serial - 1u) % static_cast<unsigned>(f[NeonFamily::EventRepeats]));
                    spacing *= std::pow(2.0, f[NeonFamily::EventCurve] * (step / f[NeonFamily::EventRepeats] * 2 - 1));
                }
                emitter.framesUntilNext += std::max(sampleRate_ / 320.0, spacing);
            }
            emitter.framesUntilNext = std::max(-1.0, emitter.framesUntilNext - 1.0);
            ++emitter.ageFrames;
            if (emitter.releaseFrames && --emitter.releaseFrames == 0u) {
                emitter.active = false;
                insertEvent(events, count, RenderEvent { frame, EventKind::Choke, 0u, 0xffu });
            }
        }
    }

    void appendTechniqueEvents(const SampleNeonSettings& settings, std::size_t slot,
        const SampleNeonEvent* input, std::size_t inputCount,
        std::array<RenderEvent, kSampleNeonMaximumBlockEvents>& events,
        std::size_t& count, uint32_t frames) noexcept
    {
        auto& emitter = grainEmitters_[slot];
        const auto& control = settings.slots[slot];
        if (control.playback == SampleNeonPlayback::Wavesets || control.playback == SampleNeonPlayback::Lanes || control.playback == SampleNeonPlayback::Spectral)
            std::fill_n(waveScanRetriggers_.data(), frames, 0u);
        if (control.playback == SampleNeonPlayback::Sample || control.playback == SampleNeonPlayback::Cutups
            || (control.playback == SampleNeonPlayback::Wavesets && control.sourceMode != NeonSourceMode::Scan && !control.deckWavesetSources && control.family[NeonFamily::WavesetEngine] == 0)) {
            emitter = {};
            std::fill_n(techniqueEnvelope_.data(), frames, 1.0f); return;
        }
        if (emitter.clock != control.clock) {
            emitter.clock = control.clock; emitter.framesUntilNext = 0.0;
        }
        uint32_t begin = 0u;
        for (std::size_t i = 0u; i < inputCount; ++i) {
            const auto& event = input[i];
            if (event.slot >= kSampleNeonSlotCount) continue;
            const bool peerChoke = event.kind == SampleNeonEventKind::Trigger && event.slot != slot
                && control.chokeGroup && control.chokeGroup == settings.slots[event.slot].chokeGroup;
            if (event.slot != slot && !peerChoke) continue;
            const uint32_t at = std::clamp(event.frameOffset, begin, frames);
            appendTechniqueRange(settings, slot, begin, at, events, count);
            begin = at;
            if (event.kind == SampleNeonEventKind::Pressure) { emitter.pressure = std::clamp(event.value, 0.f, 1.f); continue; }
            if (event.kind == SampleNeonEventKind::StackPosition) {
                const bool hadOverride = stackCueTargets_[slot] >= 0 || stackCueReturning_[slot];
                if (event.resumeNavigation && emitter.selectedSource) {
                    emitter.selectedSource = false;
                    emitter.layer = chooseStackLayer(control, emitter.velocity, slot);
                }
                stackCueTargets_[slot] = std::isfinite(event.value) ? std::clamp(event.value, -1.0f, 1.0f) : -1.0f;
                if (event.snapStackPosition && stackCueTargets_[slot] >= 0)
                    stackCuePositions_[slot] = stackCueTargets_[slot];
                stackCueReturning_[slot] = hadOverride && stackCueTargets_[slot] < 0;
                stackCueReturnFrames_[slot] = static_cast<uint32_t>(sampleRate_ * control.family[NeonFamily::LaneSlew] * 8);
                continue;
            }
            if (peerChoke || event.kind == SampleNeonEventKind::Choke) {
                emitter.active = false; continue;
            }
            if (event.mode != controller::reloop_neon::Mode::Sampler) {
                if (event.kind == SampleNeonEventKind::Trigger) { emitter = {}; }
                continue;
            }
            if (event.kind == SampleNeonEventKind::Trigger) {
                if (control.triggerMode == TriggerMode::Toggle && emitter.active) {
                    if (!emitter.releaseFrames) {
                        emitter.releaseFrames = emitter.releaseTotal;
                        emitter.releaseLevel = emitter.envelope;
                    }
                    continue;
                }
                insertEvent(events, count, RenderEvent { at, EventKind::Choke, 0u, 0xffu });
                emitter.active = true; emitter.ownsOutput = true;
                emitter.noteId = event.noteId; emitter.ageFrames = 0u; emitter.releaseFrames = 0u;
                stackPathPhases_[slot] = -1.f;
                emitter.framesUntilNext = 0.0;
                emitter.serial = 0u;
                emitter.mosaicIndex = 0u; emitter.sourcePhase = 0; emitter.pressure = 0;
                emitter.heldPosition = emitter.eventPosition = control.launchPosition;
                emitter.doubletPending = false;
                emitter.trigger = control.triggerMode;
                emitter.velocity = std::clamp(event.value, 0.0f, 1.0f);
                emitter.selectedSource = event.selectedSource && control.playback != SampleNeonPlayback::SliceSequence;
                emitter.layer = event.selectedSource ? control.selectedLayer : chooseStackLayer(control, emitter.velocity, slot);
                if (event.sourceLayer < 32u) emitter.layer = event.sourceLayer;
                if (control.playback == SampleNeonPlayback::SliceSequence) emitter.layer = 0u;
                if ((control.playback == SampleNeonPlayback::Wavesets || control.playback == SampleNeonPlayback::Lanes || control.playback == SampleNeonPlayback::Spectral)
                    && at < frames) waveScanRetriggers_[at] = 1u;
                const bool shaped = control.playback != SampleNeonPlayback::SliceSequence;
                emitter.attackFrames = static_cast<uint32_t>(std::max(1.0, std::round(sampleRate_
                    * (shaped ? std::clamp<double>(control.techniqueAttackSeconds, 0.001, 10.0) : 0.005))));
                emitter.releaseTotal = static_cast<uint32_t>(std::max(1.0, std::round(sampleRate_
                    * (shaped ? std::clamp<double>(control.techniqueReleaseSeconds, 0.001, 10.0) : 0.005))));
                emitter.fadeOutFrames = emitter.releaseTotal; emitter.envelope = 0.0f;
                emitter.seed ^= event.noteId + 0x9e3779b97f4a7c15ull;
                emitter.durationFrames = control.triggerMode == TriggerMode::Gate || control.triggerMode == TriggerMode::Toggle
                    ? UINT64_MAX : static_cast<uint64_t>(std::max(1.0,
                        std::round(sampleRate_ * std::clamp<double>(control.shotSeconds, 0.05, 30.0))));
                if (control.playback == SampleNeonPlayback::Stretch && emitter.durationFrames != UINT64_MAX)
                    emitter.durationFrames = static_cast<uint64_t>(std::max(1.0, std::round(sampleRate_
                        * (control.clock == SampleNeonClock::Host ? control.motionCycleBeats * 60.0 / std::clamp<double>(settings.hostTempoBpm, 20.0, 999.0)
                            : control.motionCycleSeconds))));
                if (emitter.durationFrames != UINT64_MAX
                    && static_cast<uint64_t>(emitter.attackFrames) + emitter.fadeOutFrames > emitter.durationFrames) {
                    const double scale = static_cast<double>(emitter.durationFrames)
                        / (emitter.attackFrames + emitter.fadeOutFrames);
                    emitter.attackFrames = static_cast<uint32_t>(std::max(1.0, emitter.attackFrames * scale));
                    emitter.fadeOutFrames = static_cast<uint32_t>(std::max(1.0, emitter.fadeOutFrames * scale));
                }
                if(const auto* p=event.transportPosition){
                    emitter.ageFrames=p->ageFrames;emitter.serial=p->serial;emitter.seed=p->seed;emitter.layer=p->layer;
                    emitter.sequenceIndex=p->sequenceIndex;emitter.mosaicIndex=p->mosaicIndex;emitter.sourcePhase=p->sourcePhase;
                    emitter.heldPosition=p->heldPosition;emitter.eventPosition=p->eventPosition;emitter.doubletPosition=p->doubletPosition;
                    emitter.selectedSource=p->selectedSource;
                    stackCueTargets_[slot]=p->stackTarget;stackCuePositions_[slot]=p->stackCuePosition;
                }
            } else if (event.kind == SampleNeonEventKind::Release
                && emitter.trigger == TriggerMode::Gate && emitter.noteId == event.noteId) {
                if (!emitter.releaseFrames) {
                    emitter.releaseFrames = emitter.releaseTotal;
                    emitter.releaseLevel = emitter.envelope;
                }
            }
        }
        appendTechniqueRange(settings, slot, begin, frames, events, count);
    }

    static RenderEvent makePlayerEvent(
        const SampleNeonSlotSettings& settings,
        const SampleNeonEvent& source, float mangle) noexcept
    {
        RenderEvent result;
        result.frameOffset = source.frameOffset;
        result.kind = source.kind == SampleNeonEventKind::Trigger
            ? EventKind::NoteOn
            : source.kind == SampleNeonEventKind::Release
                ? EventKind::NoteOff : EventKind::Choke;
        result.noteId = source.noteId;
        result.key = result.kind == EventKind::Choke && result.noteId == 0u ? 0xffu : 60u;
        result.velocity = std::clamp(source.value, 0.0f, 1.0f);
        result.variationIndex = 0xffu;

        using controller::reloop_neon::Mode;
        const uint8_t activeSliceCount = std::clamp<uint8_t>(
            settings.sliceCount, 1u,
            static_cast<uint8_t>(kSampleNeonSliceCount));
        const uint8_t sliceIndex = std::min<uint8_t>(
            source.performanceIndex,
            static_cast<uint8_t>(activeSliceCount - 1u));
        PlayMode playMode = PlayMode::Forward;
        const auto applySliceWindow = [&] {
            // All three slice performance modes use the one authored slice
            // map. Markers remain relative to the selected Start/End range.
            const double range = std::max(0.0,
                std::clamp(settings.end, 0.0, 1.0)
                    - std::clamp(settings.start, 0.0, 1.0));
            const bool customLayout = settings.sliceLayout.valid()
                && settings.sliceLayout.sliceCount == activeSliceCount;
            double sliceStart = customLayout
                ? settings.sliceLayout.boundaries[sliceIndex]
                : static_cast<double>(sliceIndex)
                    / static_cast<double>(activeSliceCount);
            double sliceEnd = customLayout
                ? settings.sliceLayout.boundaries[sliceIndex + 1u]
                : static_cast<double>(sliceIndex + 1u)
                    / static_cast<double>(activeSliceCount);
            result.startOffsetNormalized = range * sliceStart;
            result.windowLengthNormalized = range
                * std::max(0.0, sliceEnd - sliceStart);
            result.syncModeOverride = static_cast<uint8_t>(
                settings.sliceSync[sliceIndex]
                    ? SyncMode::Host : SyncMode::Free);
        };
        switch (source.mode) {
        case Mode::Sampler: {
            const bool reverse = (settings.direction == 1u || settings.direction == 3u)
                != (source.shifted || source.reverse);
            if (settings.direction >= 2u)
                playMode = reverse ? PlayMode::ReversePingPong : PlayMode::ForwardPingPong;
            else if (settings.repeat)
                playMode = reverse
                    ? PlayMode::ReverseLoop : PlayMode::ForwardLoop;
            else
                playMode = reverse
                    ? PlayMode::Reverse : PlayMode::Forward;
            if (settings.sync)
                result.syncModeOverride = static_cast<uint8_t>(
                    SyncMode::Host);
            break;
        }
        case Mode::Slicer: {
            applySliceWindow();
            playMode = source.reverse
                ? PlayMode::Reverse : PlayMode::Forward;
            switch (settings.sliceTriggers[sliceIndex]) {
            case SampleNeonSliceTriggerMode::OneShot:
                result.triggerModeOverride = static_cast<uint8_t>(
                    TriggerMode::OneShot);
                break;
            case SampleNeonSliceTriggerMode::Toggle:
                result.triggerModeOverride = static_cast<uint8_t>(
                    TriggerMode::Toggle);
                break;
            case SampleNeonSliceTriggerMode::Hold:
                result.triggerModeOverride = static_cast<uint8_t>(
                    TriggerMode::Gate);
                break;
            }
            if (settings.sliceRepeat[sliceIndex] || source.loopedSlicer) {
                playMode = source.reverse
                    ? PlayMode::ReverseLoop : PlayMode::ForwardLoop;
            }
            if (source.loopedSlicer)
                result.triggerModeOverride = static_cast<uint8_t>(
                    TriggerMode::Gate);
            break;
        }
        case Mode::HotCue:
            applySliceWindow();
            result.triggerModeOverride = static_cast<uint8_t>(
                TriggerMode::OneShot);
            playMode = source.alternate || source.reverse
                ? PlayMode::Reverse : PlayMode::Forward;
            break;
        case Mode::HotLoop:
            applySliceWindow();
            result.triggerModeOverride = static_cast<uint8_t>(
                source.alternate ? TriggerMode::Toggle : TriggerMode::Gate);
            playMode = source.alternate
                ? source.reverse ? PlayMode::ReversePingPong
                                 : PlayMode::ForwardPingPong
                : source.reverse ? PlayMode::ReverseLoop
                                 : PlayMode::ForwardLoop;
            break;
        }

        (void)mangle; // FX run after playback, never override its direction/window.
        result.playModeOverride = static_cast<uint8_t>(playMode);
        return result;
    }

    PlayerSettings makePlayerSettings(const SampleNeonSettings& settings,
        std::size_t slot) const noexcept
    {
        const auto& source = settings.slots[slot];
        PlayerSettings result;
        result.playMode = PlayMode::Forward;
        result.pitchMode = PitchMode::Rate;
        result.syncMode = settings.tempoSync || source.sync
            ? SyncMode::Host : SyncMode::Free;
        result.sourceTempoBpm = std::clamp(
            source.sourceTempoBpm, 20.0, 999.0);
        result.hostTempoBpm = std::clamp<double>(
            settings.hostTempoBpm, 20.0, 999.0);
        result.triggerMode = source.triggerMode;
        result.retriggerMode = source.retriggerMode;
        result.voiceMode = VoiceMode::Poly;
        result.start = std::clamp(source.start, 0.0, 1.0);
        const double end = std::clamp(source.end, result.start, 1.0);
        result.length = std::max(0.0, end - result.start);
        result.loopStart = result.start;
        result.loopEnd = end;
        result.loopCrossfade = 0.01;
        result.tuneSemitones = std::clamp(source.tuneSemitones,
            -60.0f, 60.0f);
        result.rootNote = 60u;
        result.attackProportion = std::clamp(
            source.attackProportion, 0.0f, 1.0f);
        result.decayProportion = std::clamp(
            source.decayProportion, 0.0f,
            1.0f - result.attackProportion);
        result.releaseProportion = std::clamp(
            source.releaseProportion, 0.0f,
            1.0f - result.attackProportion - result.decayProportion);
        result.sustain = std::clamp(source.sustain, 0.0f, 1.0f);
        result.gainDecibels = std::clamp(source.gainDecibels,
            -60.0f, 12.0f);
        result.pan = std::clamp(source.pan, -1.0f, 1.0f);
        result.velocitySensitivity = source.velocityEnabled
            ? std::clamp(source.velocitySensitivity, 0.0f, 1.0f) : 0.0f;
        result.filterType = source.filterType;
        result.filterCutoffHz = std::clamp(source.filterCutoffHz,
            20.0f, 20000.0f);
        result.filterResonance = std::clamp(source.filterResonance,
            0.0f, 1.0f);
        if (source.playback != SampleNeonPlayback::Sample && grainEmitters_[slot].ownsOutput) {
            result.syncMode = SyncMode::Free;
            result.retriggerMode = RetriggerMode::Layer;
            result.attackProportion = 0.25f;
            result.decayProportion = 0.0f;
            result.sustain = 1.0f;
            result.releaseProportion = 0.5f;
            if (source.playback == SampleNeonPlayback::SliceSequence) {
                result.attackProportion = 0.005f; result.releaseProportion = 0.02f;
            }
        }
        return result;
    }

    std::array<SamplePlayerEngine, kSampleNeonSlotCount> players_ {};
    std::array<s3g::routing::TriggerOutputAllocator<16u>, kSampleNeonSlotCount> allocators_ {};
    std::array<SampleVoiceRoute, kSampleNeonSlotCount> gestureRoutes_ {};
    std::array<unsigned, kSampleNeonSlotCount> routeSignatures_ {};
    std::array<bool, kSampleNeonSlotCount> continuousRoutingActive_ {};
    std::array<float, kSampleNeonSlotCount> stackPositions_ {};
    std::array<float, kSampleNeonSlotCount> stackPathPhases_ {};
    std::array<float, kSampleNeonSlotCount> stackCueTargets_ {};
    std::array<double, kSampleNeonSlotCount> stackCuePositions_ {};
    std::array<bool, kSampleNeonSlotCount> stackCueReturning_ {};
    std::array<uint32_t, kSampleNeonSlotCount> stackCueReturnFrames_ {};
    std::array<std::array<StackVoice, kMaximumVoices>, kSampleNeonSlotCount> stackVoices_ {};
    std::array<unsigned, kSampleNeonSlotCount> stackVoiceCounts_ {};
    std::array<uint64_t, kSampleNeonSlotCount> stackSeeds_ = [] {
        std::array<uint64_t, kSampleNeonSlotCount> result {};
        for (unsigned n = 0; n < result.size(); ++n) result[n] = 0x9e3779b97f4a7c15ull + n;
        return result;
    }();
    std::array<SampleNeonFx, kSampleNeonSlotCount> effects_ {};
    std::array<SampleWavesetsEngine, kSampleNeonSlotCount> wavesets_ {};
    std::array<BasicSampleCutupsEngine<kSampleNeonLayers>, kSampleNeonSlotCount> cutups_ {};
    std::array<NeonWavesetStackPlayer, kSampleNeonSlotCount> waveScans_ {};
    std::array<NeonLanesPlayer, kSampleNeonSlotCount> lanes_ {};
    std::array<NeonFramePlayer, kSampleNeonSlotCount> framePlayers_ {};
    std::array<unsigned, kSampleNeonSlotCount> waveCursorCounts_ {};
    std::array<const WavesetMap*, kSampleNeonSlotCount> waveMaps_ {};
    std::array<bool, kSampleNeonSlotCount> waveActive_ {};
    std::array<std::array<VoiceCursor, kMaximumVoices>, kSampleNeonSlotCount> waveCursors_ {};
    std::array<const SampleAsset*, kSampleNeonSlotCount> assets_ {};
    std::array<std::vector<float>, kMaximumAudioChannels> scratch_ {};
    std::array<float, kSampleNeonSlotCount> pressure_ {};
    std::array<float, kSampleNeonSlotCount> slotPeaks_ {};
    double motionBeatPosition_ = 0.0;
    std::array<GrainEmitter, kSampleNeonSlotCount> grainEmitters_ {};
    std::array<SampleNeonPlayback, kSampleNeonSlotCount> lastPlayback_ {};
    std::vector<float> techniqueEnvelope_;
    std::vector<float> waveScanPositions_;
    std::vector<float> waveScanVelocities_;
    std::vector<uint8_t> waveScanRetriggers_;
    std::vector<float> frameSourcePositions_;
    double sampleRate_ = 48000.0;
    uint32_t maximumFrames_ = 0u;
    float outputPeak_ = 0.0f;
    bool prepared_ = false;
};

} // namespace s3g::sample
