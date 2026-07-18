#include "s3g_ambi_vot_encoder.h"
#include "s3g_realtime.h"

#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/gui.h>
#include <clap/ext/note-ports.h>
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
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <memory>
#include <new>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kOutputChannels = s3g::kAmbiVotMaxChannels;
constexpr uint32_t kStateVersion = 5;
constexpr uint32_t kGuiW = 1160;
constexpr uint32_t kGuiH = 894;

constexpr clap_id kOrderParamId = 1;
constexpr clap_id kVoicesParamId = 2;
constexpr clap_id kModeParamId = 3;
constexpr clap_id kPresetParamId = 4;
constexpr clap_id kBaseNoteParamId = 5;
constexpr clap_id kTuneParamId = 6;
constexpr clap_id kVectorXParamId = 7;
constexpr clap_id kVectorYParamId = 8;
constexpr clap_id kScanParamId = 9;
constexpr clap_id kScanRateParamId = 10;
constexpr clap_id kMorphParamId = 11;
constexpr clap_id kDetuneParamId = 12;
constexpr clap_id kSpreadParamId = 13;
constexpr clap_id kMotionRateParamId = 14;
constexpr clap_id kAttackParamId = 15;
constexpr clap_id kReleaseParamId = 16;
constexpr clap_id kOutputParamId = 17;
constexpr clap_id kMotionSceneParamId = 18;
constexpr clap_id kMotionClockParamId = 19;
constexpr clap_id kSyncDivisionParamId = 20;
constexpr clap_id kMotionAmountParamId = 21;
constexpr clap_id kCoherenceParamId = 22;
constexpr clap_id kChaosParamId = 23;
constexpr clap_id kLinkParamId = 24;
constexpr clap_id kSmoothParamId = 25;
constexpr clap_id kCenterAzimuthParamId = 26;
constexpr clap_id kCenterElevationParamId = 27;
constexpr clap_id kCenterDistanceParamId = 28;
constexpr clap_id kNeighborRadiusParamId = 29;
constexpr clap_id kRequiredNeighborsParamId = 30;
constexpr clap_id kDecayParamId = 31;
constexpr clap_id kSustainParamId = 32;
constexpr clap_id kScaleParamId = 33;
constexpr clap_id kPitchSpreadParamId = 34;
constexpr clap_id kHarmonicsParamId = 35;
constexpr clap_id kSubharmonicsParamId = 36;
constexpr clap_id kScoreModeParamId = 37;
constexpr clap_id kScoreDurationParamId = 38;
constexpr clap_id kScoreDepthParamId = 39;

struct LegacyParamsV1 {
    uint32_t order = 3;
    uint32_t voices = 8;
    s3g::AmbiVotMode mode = s3g::AmbiVotMode::Free;
    s3g::AmbiVotPreset preset = s3g::AmbiVotPreset::Classic;
    float baseNote = 48.0f;
    float tuneCents = 0.0f;
    float vectorX = 0.20f;
    float vectorY = 0.55f;
    float scan = 0.30f;
    float scanRateHz = 0.045f;
    float morph = 1.0f;
    float detune = 0.10f;
    float spread = 0.65f;
    float driftHz = 0.025f;
    float attackMs = 35.0f;
    float releaseMs = 650.0f;
    float outputGainDb = -18.0f;
};

struct SavedStateV1 {
    uint32_t version = 1;
    LegacyParamsV1 params {};
};

struct AmbiVotParamsV2 {
    uint32_t order = 3;
    uint32_t voices = 8;
    s3g::AmbiVotMode mode = s3g::AmbiVotMode::Free;
    s3g::AmbiVotPreset preset = s3g::AmbiVotPreset::Classic;
    float baseNote = 48.0f;
    float tuneCents = 0.0f;
    float vectorX = 0.20f;
    float vectorY = 0.55f;
    float scan = 0.30f;
    float scanRate = 1.0f;
    float morph = 1.0f;
    float detune = 0.10f;
    float motionSpread = 0.65f;
    float motionRateHz = 0.045f;
    float attackMs = 35.0f;
    float releaseMs = 650.0f;
    float outputGainDb = -18.0f;
    s3g::AmbiVotMotionScene motionScene = s3g::AmbiVotMotionScene::Flow;
    s3g::AmbiVotMotionClock motionClock = s3g::AmbiVotMotionClock::Free;
    float syncDivisionBeats = 8.0f;
    float motionAmount = 0.72f;
    float motionCoherence = 0.62f;
    float motionChaos = 0.18f;
    float motionLink = 0.72f;
    float motionSmooth = 0.72f;
    float centerAzimuthDeg = 0.0f;
    float centerElevationDeg = 0.0f;
    float centerDistance = 1.0f;
};

struct SavedStateV2 {
    uint32_t version = 2;
    AmbiVotParamsV2 params {};
    int32_t guiPage = 0;
    int32_t guiViewMode = 2;
    float guiViewAzDeg = 38.0f;
    float guiViewElDeg = 32.0f;
    float guiViewZoom = 1.0f;
};

struct AmbiVotParamsV3 {
    uint32_t order = 3;
    uint32_t voices = 8;
    s3g::AmbiVotMode mode = s3g::AmbiVotMode::Free;
    s3g::AmbiVotPreset preset = s3g::AmbiVotPreset::Classic;
    float baseNote = 48.0f;
    float tuneCents = 0.0f;
    float vectorX = 0.20f;
    float vectorY = 0.55f;
    float scan = 0.30f;
    float scanRate = 1.0f;
    float morph = 1.0f;
    float detune = 0.10f;
    float motionSpread = 0.65f;
    float motionRateHz = 0.045f;
    float attackMs = 35.0f;
    float releaseMs = 650.0f;
    float outputGainDb = -18.0f;
    s3g::AmbiVotMotionScene motionScene = s3g::AmbiVotMotionScene::Flow;
    s3g::AmbiVotMotionClock motionClock = s3g::AmbiVotMotionClock::Free;
    float syncDivisionBeats = 8.0f;
    float motionAmount = 0.72f;
    float motionCoherence = 0.62f;
    float motionChaos = 0.18f;
    float motionLink = 0.72f;
    float motionSmooth = 0.72f;
    float centerAzimuthDeg = 0.0f;
    float centerElevationDeg = 0.0f;
    float centerDistance = 1.0f;
    float neighborRadius = 0.90f;
    uint32_t requiredNeighbors = 1;
};

struct SavedStateV3 {
    uint32_t version = 3;
    AmbiVotParamsV3 params {};
    int32_t guiPage = 0;
    int32_t guiViewMode = 2;
    float guiViewAzDeg = 38.0f;
    float guiViewElDeg = 32.0f;
    float guiViewZoom = 1.0f;
};

struct AmbiVotParamsV4 {
    uint32_t order = 3;
    uint32_t voices = 8;
    s3g::AmbiVotMode mode = s3g::AmbiVotMode::Free;
    s3g::AmbiVotPreset preset = s3g::AmbiVotPreset::Classic;
    float baseNote = 48.0f;
    float tuneCents = 0.0f;
    float vectorX = 0.20f;
    float vectorY = 0.55f;
    float scan = 0.30f;
    float scanRate = 1.0f;
    float morph = 1.0f;
    float detune = 0.10f;
    float motionSpread = 0.65f;
    float motionRateHz = 0.045f;
    float attackMs = 35.0f;
    float decayMs = 220.0f;
    float sustain = 0.68f;
    float releaseMs = 650.0f;
    float outputGainDb = -18.0f;
    s3g::AmbiVotMotionScene motionScene = s3g::AmbiVotMotionScene::Flow;
    s3g::AmbiVotMotionClock motionClock = s3g::AmbiVotMotionClock::Free;
    float syncDivisionBeats = 8.0f;
    float motionAmount = 0.72f;
    float motionCoherence = 0.62f;
    float motionChaos = 0.18f;
    float motionLink = 0.72f;
    float motionSmooth = 0.72f;
    float centerAzimuthDeg = 0.0f;
    float centerElevationDeg = 0.0f;
    float centerDistance = 1.0f;
    float neighborRadius = 0.90f;
    uint32_t requiredNeighbors = 1;
};

struct SavedStateV4 {
    uint32_t version = 4;
    AmbiVotParamsV4 params {};
    int32_t guiPage = 0;
    int32_t guiViewMode = 2;
    float guiViewAzDeg = 38.0f;
    float guiViewElDeg = 32.0f;
    float guiViewZoom = 1.0f;
};

struct SavedState {
    uint32_t version = kStateVersion;
    s3g::AmbiVotParams params {};
    s3g::AmbiVotVectorScore score = s3g::ambiVotDefaultScore();
    int32_t guiPage = 0;
    int32_t guiViewMode = 2;
    float guiViewAzDeg = 38.0f;
    float guiViewElDeg = 32.0f;
    float guiViewZoom = 1.0f;
};

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    double sampleRate = 48000.0;
    s3g::AmbiVotEncoder engine {};
    s3g::AmbiVotParams params {};
    std::array<std::shared_ptr<const s3g::AmbiVotTableBank>, 4> presetBanks {};
    std::shared_ptr<const s3g::AmbiVotTableBank> userBank;
    std::atomic<uint32_t> scoreNodeCount { 8u };
    std::atomic<uint32_t> scoreSustainStart { 2u };
    std::atomic<uint32_t> scoreSustainEnd { 6u };
    std::array<std::atomic<float>, s3g::kAmbiVotMaxScoreNodes> scoreTime {};
    std::array<std::atomic<float>, s3g::kAmbiVotMaxScoreNodes> scoreU {};
    std::array<std::atomic<float>, s3g::kAmbiVotMaxScoreNodes> scoreV {};
    std::array<std::atomic<uint32_t>, s3g::kAmbiVotMaxScoreNodes> scoreCurve {};
    std::atomic<float> outputPeak { 0.0f };
    std::atomic<uint32_t> lastMidiNote { 0u };
#if defined(__APPLE__)
    void* guiView = nullptr;
    std::atomic<bool> guiVisible { false };
    std::array<std::atomic<float>, s3g::kAmbiVotMaxVoices> guiAzimuth {};
    std::array<std::atomic<float>, s3g::kAmbiVotMaxVoices> guiElevation {};
    std::array<std::atomic<float>, s3g::kAmbiVotMaxVoices> guiDistance {};
    std::array<std::atomic<float>, s3g::kAmbiVotMaxVoices> guiU {};
    std::array<std::atomic<float>, s3g::kAmbiVotMaxVoices> guiV {};
    std::array<std::atomic<uint32_t>, s3g::kAmbiVotMaxVoices> guiNeighborCount {};
    std::array<std::atomic<uint32_t>, s3g::kAmbiVotMaxVoices> guiNeighborGate {};
    std::atomic<float> guiMotionPhase { 0.0f };
    int guiPage = 0;
    int guiViewMode = 2;
    float guiViewAzDeg = 38.0f;
    float guiViewElDeg = 32.0f;
    float guiViewZoom = 1.0f;
#endif
};

Plugin* self(const clap_plugin_t* plugin) { return static_cast<Plugin*>(plugin->plugin_data); }

s3g::AmbiVotVectorScore loadScore(const Plugin& plugin)
{
    s3g::AmbiVotVectorScore score {};
    score.nodeCount = plugin.scoreNodeCount.load(std::memory_order_acquire);
    score.sustainStart = plugin.scoreSustainStart.load(std::memory_order_relaxed);
    score.sustainEnd = plugin.scoreSustainEnd.load(std::memory_order_relaxed);
    for (uint32_t i = 0; i < s3g::kAmbiVotMaxScoreNodes; ++i) {
        score.nodes[i].time = plugin.scoreTime[i].load(std::memory_order_relaxed);
        score.nodes[i].u = plugin.scoreU[i].load(std::memory_order_relaxed);
        score.nodes[i].v = plugin.scoreV[i].load(std::memory_order_relaxed);
        score.nodes[i].curve = static_cast<s3g::AmbiVotScoreCurve>(
            plugin.scoreCurve[i].load(std::memory_order_relaxed));
    }
    s3g::ambiVotNormalizeScore(score);
    return score;
}

void storeScore(Plugin& plugin, s3g::AmbiVotVectorScore score)
{
    s3g::ambiVotNormalizeScore(score);
    for (uint32_t i = 0; i < s3g::kAmbiVotMaxScoreNodes; ++i) {
        plugin.scoreTime[i].store(score.nodes[i].time, std::memory_order_relaxed);
        plugin.scoreU[i].store(score.nodes[i].u, std::memory_order_relaxed);
        plugin.scoreV[i].store(score.nodes[i].v, std::memory_order_relaxed);
        plugin.scoreCurve[i].store(static_cast<uint32_t>(score.nodes[i].curve), std::memory_order_relaxed);
    }
    plugin.scoreSustainStart.store(score.sustainStart, std::memory_order_relaxed);
    plugin.scoreSustainEnd.store(score.sustainEnd, std::memory_order_relaxed);
    plugin.scoreNodeCount.store(score.nodeCount, std::memory_order_release);
}

std::shared_ptr<const s3g::AmbiVotTableBank> makePresetBank(s3g::AmbiVotPreset preset)
{
    return std::make_shared<s3g::AmbiVotTableBank>(s3g::ambiVotPresetBank(preset));
}

std::shared_ptr<const s3g::AmbiVotTableBank> activeBank(Plugin& plugin)
{
    if (plugin.params.preset == s3g::AmbiVotPreset::User) {
        auto user = std::atomic_load_explicit(&plugin.userBank, std::memory_order_acquire);
        if (user) return user;
    }
    const uint32_t index = std::min<uint32_t>(
        static_cast<uint32_t>(plugin.params.preset),
        static_cast<uint32_t>(s3g::AmbiVotPreset::Formant));
    return plugin.presetBanks[index];
}

bool writeExact(const clap_ostream_t* stream, const void* data, size_t size)
{
    const auto* bytes = static_cast<const uint8_t*>(data);
    size_t done = 0;
    while (done < size) {
        const int64_t count = stream->write(stream, bytes + done, size - done);
        if (count <= 0) return false;
        done += static_cast<size_t>(count);
    }
    return true;
}

bool readExact(const clap_istream_t* stream, void* data, size_t size)
{
    auto* bytes = static_cast<uint8_t*>(data);
    size_t done = 0;
    while (done < size) {
        const int64_t count = stream->read(stream, bytes + done, size - done);
        if (count <= 0) return false;
        done += static_cast<size_t>(count);
    }
    return true;
}

s3g::AmbiVotParams migrateLegacy(const LegacyParamsV1& old)
{
    s3g::AmbiVotParams params {};
    params.order = old.order;
    params.voices = old.voices;
    params.mode = old.mode;
    params.preset = old.preset;
    params.baseNote = old.baseNote;
    params.tuneCents = old.tuneCents;
    params.vectorX = old.vectorX;
    params.vectorY = old.vectorY;
    params.scan = old.scan;
    params.scanRate = 1.0f;
    params.morph = old.morph;
    params.detune = old.detune;
    params.motionSpread = old.spread;
    params.motionRateHz = std::max(0.001f, std::fabs(old.driftHz));
    params.attackMs = old.attackMs;
    params.decayMs = 220.0f;
    params.sustain = 1.0f;
    params.releaseMs = old.releaseMs;
    params.outputGainDb = old.outputGainDb;
    params.scoreMode = s3g::AmbiVotScoreMode::Off;
    return params;
}

s3g::AmbiVotParams migrateV2(const AmbiVotParamsV2& old)
{
    s3g::AmbiVotParams params {};
    params.order = old.order;
    params.voices = old.voices;
    params.mode = old.mode;
    params.preset = old.preset;
    params.baseNote = old.baseNote;
    params.tuneCents = old.tuneCents;
    params.vectorX = old.vectorX;
    params.vectorY = old.vectorY;
    params.scan = old.scan;
    params.scanRate = old.scanRate;
    params.morph = old.morph;
    params.detune = old.detune;
    params.motionSpread = old.motionSpread;
    params.motionRateHz = old.motionRateHz;
    params.attackMs = old.attackMs;
    params.decayMs = 220.0f;
    params.sustain = 1.0f;
    params.releaseMs = old.releaseMs;
    params.outputGainDb = old.outputGainDb;
    params.motionScene = old.motionScene;
    params.motionClock = old.motionClock;
    params.syncDivisionBeats = old.syncDivisionBeats;
    params.motionAmount = old.motionAmount;
    params.motionCoherence = old.motionCoherence;
    params.motionChaos = old.motionChaos;
    params.motionLink = old.motionLink;
    params.motionSmooth = old.motionSmooth;
    params.centerAzimuthDeg = old.centerAzimuthDeg;
    params.centerElevationDeg = old.centerElevationDeg;
    params.centerDistance = old.centerDistance;
    params.scoreMode = s3g::AmbiVotScoreMode::Off;
    return params;
}

s3g::AmbiVotParams migrateV3(const AmbiVotParamsV3& old)
{
    s3g::AmbiVotParams params {};
    params.order = old.order;
    params.voices = old.voices;
    params.mode = old.mode;
    params.preset = old.preset;
    params.baseNote = old.baseNote;
    params.tuneCents = old.tuneCents;
    params.vectorX = old.vectorX;
    params.vectorY = old.vectorY;
    params.scan = old.scan;
    params.scanRate = old.scanRate;
    params.morph = old.morph;
    params.detune = old.detune;
    params.motionSpread = old.motionSpread;
    params.motionRateHz = old.motionRateHz;
    params.attackMs = old.attackMs;
    params.decayMs = 220.0f;
    params.sustain = 1.0f;
    params.releaseMs = old.releaseMs;
    params.outputGainDb = old.outputGainDb;
    params.motionScene = old.motionScene;
    params.motionClock = old.motionClock;
    params.syncDivisionBeats = old.syncDivisionBeats;
    params.motionAmount = old.motionAmount;
    params.motionCoherence = old.motionCoherence;
    params.motionChaos = old.motionChaos;
    params.motionLink = old.motionLink;
    params.motionSmooth = old.motionSmooth;
    params.centerAzimuthDeg = old.centerAzimuthDeg;
    params.centerElevationDeg = old.centerElevationDeg;
    params.centerDistance = old.centerDistance;
    params.neighborRadius = old.neighborRadius;
    params.requiredNeighbors = old.requiredNeighbors;
    params.scoreMode = s3g::AmbiVotScoreMode::Off;
    return params;
}

s3g::AmbiVotParams migrateV4(const AmbiVotParamsV4& old)
{
    s3g::AmbiVotParams params {};
    params.order = old.order;
    params.voices = old.voices;
    params.mode = old.mode;
    params.preset = old.preset;
    params.baseNote = old.baseNote;
    params.tuneCents = old.tuneCents;
    params.vectorX = old.vectorX;
    params.vectorY = old.vectorY;
    params.scan = old.scan;
    params.scanRate = old.scanRate;
    params.morph = old.morph;
    params.detune = old.detune;
    params.motionSpread = old.motionSpread;
    params.motionRateHz = old.motionRateHz;
    params.attackMs = old.attackMs;
    params.decayMs = old.decayMs;
    params.sustain = old.sustain;
    params.releaseMs = old.releaseMs;
    params.outputGainDb = old.outputGainDb;
    params.motionScene = old.motionScene;
    params.motionClock = old.motionClock;
    params.syncDivisionBeats = old.syncDivisionBeats;
    params.motionAmount = old.motionAmount;
    params.motionCoherence = old.motionCoherence;
    params.motionChaos = old.motionChaos;
    params.motionLink = old.motionLink;
    params.motionSmooth = old.motionSmooth;
    params.centerAzimuthDeg = old.centerAzimuthDeg;
    params.centerElevationDeg = old.centerElevationDeg;
    params.centerDistance = old.centerDistance;
    params.neighborRadius = old.neighborRadius;
    params.requiredNeighbors = old.requiredNeighbors;
    params.scoreMode = s3g::AmbiVotScoreMode::Off;
    return params;
}

void applyParam(Plugin& plugin, clap_id id, double value)
{
    switch (id) {
    case kOrderParamId: plugin.params.order = static_cast<uint32_t>(std::lround(value)); break;
    case kVoicesParamId: plugin.params.voices = static_cast<uint32_t>(std::lround(value)); break;
    case kModeParamId: plugin.params.mode = static_cast<s3g::AmbiVotMode>(static_cast<uint32_t>(std::lround(value))); break;
    case kPresetParamId: plugin.params.preset = static_cast<s3g::AmbiVotPreset>(static_cast<uint32_t>(std::lround(value))); break;
    case kBaseNoteParamId: plugin.params.baseNote = static_cast<float>(value); break;
    case kTuneParamId: plugin.params.tuneCents = static_cast<float>(value); break;
    case kVectorXParamId: plugin.params.vectorX = static_cast<float>(value); break;
    case kVectorYParamId: plugin.params.vectorY = static_cast<float>(value); break;
    case kScanParamId: plugin.params.scan = static_cast<float>(value); break;
    case kScanRateParamId: plugin.params.scanRate = static_cast<float>(value); break;
    case kMorphParamId: plugin.params.morph = static_cast<float>(value); break;
    case kDetuneParamId: plugin.params.detune = static_cast<float>(value); break;
    case kScaleParamId: plugin.params.scale = static_cast<s3g::AmbiVotScale>(static_cast<uint32_t>(std::lround(value))); break;
    case kPitchSpreadParamId: plugin.params.pitchSpread = static_cast<float>(value); break;
    case kHarmonicsParamId: plugin.params.harmonicAmount = static_cast<float>(value); break;
    case kSubharmonicsParamId: plugin.params.subharmonicAmount = static_cast<float>(value); break;
    case kSpreadParamId: plugin.params.motionSpread = static_cast<float>(value); break;
    case kMotionRateParamId: plugin.params.motionRateHz = static_cast<float>(value); break;
    case kAttackParamId: plugin.params.attackMs = static_cast<float>(value); break;
    case kDecayParamId: plugin.params.decayMs = static_cast<float>(value); break;
    case kSustainParamId: plugin.params.sustain = static_cast<float>(value); break;
    case kReleaseParamId: plugin.params.releaseMs = static_cast<float>(value); break;
    case kOutputParamId: plugin.params.outputGainDb = static_cast<float>(value); break;
    case kMotionSceneParamId: plugin.params.motionScene = static_cast<s3g::AmbiVotMotionScene>(static_cast<uint32_t>(std::lround(value))); break;
    case kMotionClockParamId: plugin.params.motionClock = static_cast<s3g::AmbiVotMotionClock>(static_cast<uint32_t>(std::lround(value))); break;
    case kSyncDivisionParamId: plugin.params.syncDivisionBeats = static_cast<float>(value); break;
    case kMotionAmountParamId: plugin.params.motionAmount = static_cast<float>(value); break;
    case kCoherenceParamId: plugin.params.motionCoherence = static_cast<float>(value); break;
    case kChaosParamId: plugin.params.motionChaos = static_cast<float>(value); break;
    case kLinkParamId: plugin.params.motionLink = static_cast<float>(value); break;
    case kSmoothParamId: plugin.params.motionSmooth = static_cast<float>(value); break;
    case kCenterAzimuthParamId: plugin.params.centerAzimuthDeg = static_cast<float>(value); break;
    case kCenterElevationParamId: plugin.params.centerElevationDeg = static_cast<float>(value); break;
    case kCenterDistanceParamId: plugin.params.centerDistance = static_cast<float>(value); break;
    case kNeighborRadiusParamId: plugin.params.neighborRadius = static_cast<float>(value); break;
    case kRequiredNeighborsParamId: plugin.params.requiredNeighbors = static_cast<uint32_t>(std::lround(value)); break;
    case kScoreModeParamId: plugin.params.scoreMode = static_cast<s3g::AmbiVotScoreMode>(static_cast<uint32_t>(std::lround(value))); break;
    case kScoreDurationParamId: plugin.params.scoreDurationSec = static_cast<float>(value); break;
    case kScoreDepthParamId: plugin.params.scoreDepth = static_cast<float>(value); break;
    default: return;
    }
    plugin.engine.setParams(plugin.params);
    plugin.params = plugin.engine.params();
}

void readEvents(Plugin& plugin, const clap_input_events_t* events)
{
    if (!events) return;
    const uint32_t count = events->size(events);
    for (uint32_t i = 0; i < count; ++i) {
        const clap_event_header_t* event = events->get(events, i);
        if (!event || event->space_id != CLAP_CORE_EVENT_SPACE_ID) continue;
        if (event->type == CLAP_EVENT_PARAM_VALUE) {
            const auto* param = reinterpret_cast<const clap_event_param_value_t*>(event);
            applyParam(plugin, param->param_id, param->value);
        } else if (event->type == CLAP_EVENT_NOTE_ON || event->type == CLAP_EVENT_NOTE_OFF || event->type == CLAP_EVENT_NOTE_CHOKE) {
            const auto* note = reinterpret_cast<const clap_event_note_t*>(event);
            if (event->type == CLAP_EVENT_NOTE_ON && note->velocity > 0.0) {
                plugin.engine.noteOn(note->key, static_cast<float>(note->velocity));
                plugin.lastMidiNote.store(static_cast<uint32_t>(note->key), std::memory_order_relaxed);
            } else {
                plugin.engine.noteOff(note->key);
            }
        }
    }
}

void updateTransportPhase(Plugin& plugin, const clap_event_transport_t* transport)
{
    if (plugin.params.motionClock != s3g::AmbiVotMotionClock::Sync) {
        plugin.engine.useFreePhase();
        return;
    }
    if (transport && (transport->flags & CLAP_TRANSPORT_HAS_BEATS_TIMELINE) != 0) {
        const double beats = static_cast<double>(transport->song_pos_beats) / static_cast<double>(CLAP_BEATTIME_FACTOR);
        const double division = std::max(0.25, static_cast<double>(plugin.params.syncDivisionBeats));
        double phase = std::fmod(beats / division, 1.0);
        if (phase < 0.0) phase += 1.0;
        plugin.engine.setExternalPhase(static_cast<float>(phase));
    } else {
        plugin.engine.useFreePhase();
    }
}

#if defined(__APPLE__)
void publishMotionSnapshot(Plugin& plugin)
{
    if (!plugin.guiVisible.load(std::memory_order_relaxed)) return;
    const auto& points = plugin.engine.motionPoints();
    const auto& neighborCounts = plugin.engine.neighborCounts();
    const auto& neighborGates = plugin.engine.neighborGates();
    for (uint32_t i = 0; i < s3g::kAmbiVotMaxVoices; ++i) {
        plugin.guiAzimuth[i].store(points[i].azimuthDeg, std::memory_order_relaxed);
        plugin.guiElevation[i].store(points[i].elevationDeg, std::memory_order_relaxed);
        plugin.guiDistance[i].store(points[i].distance, std::memory_order_relaxed);
        plugin.guiU[i].store(points[i].u, std::memory_order_relaxed);
        plugin.guiV[i].store(points[i].v, std::memory_order_relaxed);
        plugin.guiNeighborCount[i].store(neighborCounts[i], std::memory_order_relaxed);
        plugin.guiNeighborGate[i].store(neighborGates[i], std::memory_order_relaxed);
    }
    plugin.guiMotionPhase.store(plugin.engine.motionPhase(), std::memory_order_relaxed);
}

void guiDestroy(const clap_plugin_t* plugin);
#endif

bool init(const clap_plugin_t*) { return true; }

void destroy(const clap_plugin_t* plugin)
{
#if defined(__APPLE__)
    guiDestroy(plugin);
#endif
    delete self(plugin);
}

bool activate(const clap_plugin_t* plugin, double sampleRate, uint32_t, uint32_t)
{
    auto* state = self(plugin);
    state->sampleRate = sampleRate;
    state->engine.prepare(sampleRate);
    state->engine.setParams(state->params);
    state->engine.setScore(loadScore(*state));
    return true;
}

void deactivate(const clap_plugin_t*) {}
bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}

void reset(const clap_plugin_t* plugin)
{
    auto* state = self(plugin);
    state->engine.reset();
    state->outputPeak.store(0.0f, std::memory_order_relaxed);
}

clap_process_status process(const clap_plugin_t* plugin, const clap_process_t* processData)
{
    auto* state = self(plugin);
    if (!processData) return CLAP_PROCESS_CONTINUE;
    readEvents(*state, processData->in_events);
    updateTransportPhase(*state, processData->transport);
    if (processData->audio_outputs_count == 0u || !processData->audio_outputs) return CLAP_PROCESS_CONTINUE;

    auto& output = processData->audio_outputs[0];
    const uint32_t frames = processData->frames_count;
    if (!output.data32) {
        if (output.data64) {
            for (uint32_t ch = 0; ch < output.channel_count; ++ch) {
                if (output.data64[ch]) std::fill(output.data64[ch], output.data64[ch] + frames, 0.0);
            }
        }
        return CLAP_PROCESS_CONTINUE;
    }

    float* outputs[kOutputChannels] {};
    const uint32_t outputChannels = std::min<uint32_t>(output.channel_count, kOutputChannels);
    for (uint32_t ch = 0; ch < outputChannels; ++ch) outputs[ch] = output.data32[ch];
    auto bank = activeBank(*state);
    if (bank) {
        state->engine.setParams(state->params);
        state->engine.setScore(loadScore(*state));
        state->engine.process(*bank, outputs, outputChannels, frames);
    } else {
        for (uint32_t ch = 0; ch < outputChannels; ++ch) {
            if (outputs[ch]) std::fill(outputs[ch], outputs[ch] + frames, 0.0f);
        }
    }
    s3g::clearAudioBufferFromChannel(output, outputChannels, frames);

    float peak = 0.0f;
    for (uint32_t ch = 0; ch < outputChannels; ++ch) {
        if (!output.data32[ch]) continue;
        for (uint32_t frame = 0; frame < frames; ++frame) peak = std::max(peak, std::fabs(output.data32[ch][frame]));
    }
    state->outputPeak.store(std::max(state->outputPeak.load(std::memory_order_relaxed) * 0.92f, peak), std::memory_order_relaxed);
#if defined(__APPLE__)
    publishMotionSnapshot(*state);
#endif
    return CLAP_PROCESS_CONTINUE;
}

void onMainThread(const clap_plugin_t*) {}

uint32_t audioPortsCount(const clap_plugin_t*, bool isInput) { return isInput ? 0u : 1u; }

bool audioPortsGet(const clap_plugin_t*, uint32_t index, bool isInput, clap_audio_port_info_t* info)
{
    if (isInput || index != 0u || !info) return false;
    info->id = 20;
    std::strncpy(info->name, "7OA ACN/SN3D Out", sizeof(info->name));
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = kOutputChannels;
    info->port_type = CLAP_PORT_AMBISONIC;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}
const clap_plugin_audio_ports_t audioPorts { audioPortsCount, audioPortsGet };

uint32_t notePortsCount(const clap_plugin_t*, bool isInput) { return isInput ? 1u : 0u; }

bool notePortsGet(const clap_plugin_t*, uint32_t index, bool isInput, clap_note_port_info_t* info)
{
    if (!isInput || index != 0u || !info) return false;
    info->id = 30;
    info->supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI;
    info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
    std::strncpy(info->name, "MIDI In", sizeof(info->name));
    return true;
}
const clap_plugin_note_ports_t notePorts { notePortsCount, notePortsGet };

struct ParamDef {
    clap_id id;
    const char* name;
    double min;
    double max;
    double def;
    bool stepped;
};

constexpr ParamDef kParams[] {
    { kOrderParamId, "Order", 1.0, 7.0, 3.0, true },
    { kVoicesParamId, "Voices", 1.0, 64.0, 8.0, true },
    { kModeParamId, "Mode", 0.0, 2.0, 0.0, true },
    { kPresetParamId, "Wave Set", 0.0, 4.0, 1.0, true },
    { kBaseNoteParamId, "Base Note", 12.0, 96.0, 48.0, false },
    { kTuneParamId, "Tune Cents", -1200.0, 1200.0, 0.0, false },
    { kVectorXParamId, "Vector X", 0.0, 1.0, 0.20, false },
    { kVectorYParamId, "Vector Y", 0.0, 1.0, 0.55, false },
    { kScanParamId, "Scan Depth", 0.0, 1.0, 0.30, false },
    { kScanRateParamId, "Scan Ratio", -4.0, 4.0, 1.0, false },
    { kMorphParamId, "Morph", 0.0, 1.0, 1.0, false },
    { kDetuneParamId, "Pitch Deviation", 0.0, 1.0, 0.10, false },
    { kScaleParamId, "Pitch Scale", 0.0, 5.0, 0.0, true },
    { kPitchSpreadParamId, "Pitch Spread", 0.0, 2.0, 1.0, false },
    { kHarmonicsParamId, "Harmonic Pull", 0.0, 1.0, 0.0, false },
    { kSubharmonicsParamId, "Subharmonic Pull", 0.0, 1.0, 0.0, false },
    { kSpreadParamId, "Motion Spread", 0.0, 1.0, 0.65, false },
    { kMotionRateParamId, "Motion Rate", 0.001, 2.0, 0.045, false },
    { kAttackParamId, "Attack", 1.0, 2000.0, 35.0, false },
    { kDecayParamId, "Decay", 5.0, 4000.0, 220.0, false },
    { kSustainParamId, "Sustain", 0.0, 1.0, 0.68, false },
    { kReleaseParamId, "Release", 5.0, 8000.0, 650.0, false },
    { kOutputParamId, "Output", -60.0, 12.0, -18.0, false },
    { kMotionSceneParamId, "Motion Scene", 0.0, 4.0, 2.0, true },
    { kMotionClockParamId, "Motion Clock", 0.0, 1.0, 0.0, true },
    { kSyncDivisionParamId, "Sync Division", 0.25, 64.0, 8.0, false },
    { kMotionAmountParamId, "Motion Amount", 0.0, 1.0, 0.72, false },
    { kCoherenceParamId, "Motion Coherence", 0.0, 1.0, 0.62, false },
    { kChaosParamId, "Motion Chaos", 0.0, 1.0, 0.18, false },
    { kLinkParamId, "Space Timbre Link", 0.0, 1.0, 0.72, false },
    { kSmoothParamId, "Motion Smooth", 0.0, 1.0, 0.72, false },
    { kCenterAzimuthParamId, "Center Azimuth", -180.0, 180.0, 0.0, false },
    { kCenterElevationParamId, "Center Elevation", -90.0, 90.0, 0.0, false },
    { kCenterDistanceParamId, "Center Distance", 0.15, 2.0, 1.0, false },
    { kNeighborRadiusParamId, "Neighbor Proximity", 0.05, 4.0, 0.90, false },
    { kRequiredNeighborsParamId, "Required Neighbors", 1.0, 63.0, 1.0, true },
    { kScoreModeParamId, "Vector Score Mode", 0.0, 3.0, 2.0, true },
    { kScoreDurationParamId, "Vector Score Duration", 0.25, 60.0, 8.0, false },
    { kScoreDepthParamId, "Vector Score Depth", 0.0, 1.0, 1.0, false },
};

uint32_t paramsCount(const clap_plugin_t*) { return static_cast<uint32_t>(std::size(kParams)); }

bool paramsGetInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info)
{
    if (!info || index >= paramsCount(nullptr)) return false;
    const auto& def = kParams[index];
    info->id = def.id;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE | (def.stepped ? CLAP_PARAM_IS_STEPPED : 0);
    std::strncpy(info->name, def.name, sizeof(info->name));
    std::strncpy(info->module, "Ambi VOT Encoder", sizeof(info->module));
    info->min_value = def.min;
    info->max_value = def.max;
    info->default_value = def.def;
    return true;
}

bool paramsGetValue(const clap_plugin_t* plugin, clap_id id, double* value)
{
    if (!value) return false;
    const auto params = self(plugin)->params;
    switch (id) {
    case kOrderParamId: *value = params.order; return true;
    case kVoicesParamId: *value = params.voices; return true;
    case kModeParamId: *value = static_cast<uint32_t>(params.mode); return true;
    case kPresetParamId: *value = static_cast<uint32_t>(params.preset); return true;
    case kBaseNoteParamId: *value = params.baseNote; return true;
    case kTuneParamId: *value = params.tuneCents; return true;
    case kVectorXParamId: *value = params.vectorX; return true;
    case kVectorYParamId: *value = params.vectorY; return true;
    case kScanParamId: *value = params.scan; return true;
    case kScanRateParamId: *value = params.scanRate; return true;
    case kMorphParamId: *value = params.morph; return true;
    case kDetuneParamId: *value = params.detune; return true;
    case kScaleParamId: *value = static_cast<uint32_t>(params.scale); return true;
    case kPitchSpreadParamId: *value = params.pitchSpread; return true;
    case kHarmonicsParamId: *value = params.harmonicAmount; return true;
    case kSubharmonicsParamId: *value = params.subharmonicAmount; return true;
    case kSpreadParamId: *value = params.motionSpread; return true;
    case kMotionRateParamId: *value = params.motionRateHz; return true;
    case kAttackParamId: *value = params.attackMs; return true;
    case kDecayParamId: *value = params.decayMs; return true;
    case kSustainParamId: *value = params.sustain; return true;
    case kReleaseParamId: *value = params.releaseMs; return true;
    case kOutputParamId: *value = params.outputGainDb; return true;
    case kMotionSceneParamId: *value = static_cast<uint32_t>(params.motionScene); return true;
    case kMotionClockParamId: *value = static_cast<uint32_t>(params.motionClock); return true;
    case kSyncDivisionParamId: *value = params.syncDivisionBeats; return true;
    case kMotionAmountParamId: *value = params.motionAmount; return true;
    case kCoherenceParamId: *value = params.motionCoherence; return true;
    case kChaosParamId: *value = params.motionChaos; return true;
    case kLinkParamId: *value = params.motionLink; return true;
    case kSmoothParamId: *value = params.motionSmooth; return true;
    case kCenterAzimuthParamId: *value = params.centerAzimuthDeg; return true;
    case kCenterElevationParamId: *value = params.centerElevationDeg; return true;
    case kCenterDistanceParamId: *value = params.centerDistance; return true;
    case kNeighborRadiusParamId: *value = params.neighborRadius; return true;
    case kRequiredNeighborsParamId: *value = params.requiredNeighbors; return true;
    case kScoreModeParamId: *value = static_cast<uint32_t>(params.scoreMode); return true;
    case kScoreDurationParamId: *value = params.scoreDurationSec; return true;
    case kScoreDepthParamId: *value = params.scoreDepth; return true;
    default: return false;
    }
}

bool paramsValueToText(const clap_plugin_t*, clap_id id, double value, char* display, uint32_t size)
{
    if (!display || size == 0u) return false;
    if (id == kOrderParamId) std::snprintf(display, size, "%.0fOA", value);
    else if (id == kModeParamId) std::snprintf(display, size, "%s", s3g::ambiVotModeName(static_cast<s3g::AmbiVotMode>(static_cast<uint32_t>(std::lround(value)))));
    else if (id == kPresetParamId) std::snprintf(display, size, "%s", s3g::ambiVotPresetName(static_cast<s3g::AmbiVotPreset>(static_cast<uint32_t>(std::lround(value)))));
    else if (id == kMotionSceneParamId) std::snprintf(display, size, "%s", s3g::ambiVotMotionSceneName(static_cast<s3g::AmbiVotMotionScene>(static_cast<uint32_t>(std::lround(value)))));
    else if (id == kMotionClockParamId) std::snprintf(display, size, "%s", s3g::ambiVotMotionClockName(static_cast<s3g::AmbiVotMotionClock>(static_cast<uint32_t>(std::lround(value)))));
    else if (id == kScaleParamId) std::snprintf(display, size, "%s", s3g::ambiVotScaleName(static_cast<s3g::AmbiVotScale>(static_cast<uint32_t>(std::lround(value)))));
    else if (id == kScoreModeParamId) std::snprintf(display, size, "%s", s3g::ambiVotScoreModeName(static_cast<s3g::AmbiVotScoreMode>(static_cast<uint32_t>(std::lround(value)))));
    else if (id == kTuneParamId) std::snprintf(display, size, "%+.0f ct", value);
    else if (id == kDetuneParamId) std::snprintf(display, size, "%.0f ct", value * 100.0);
    else if (id == kPitchSpreadParamId) std::snprintf(display, size, "%.0f%%", value * 100.0);
    else if (id == kMotionRateParamId) std::snprintf(display, size, "%.3f Hz", value);
    else if (id == kScanRateParamId) std::snprintf(display, size, "%+.2fx", value);
    else if (id == kSyncDivisionParamId) std::snprintf(display, size, "%.2f beats", value);
    else if (id == kScoreDurationParamId) std::snprintf(display, size, "%.2f s", value);
    else if (id == kAttackParamId || id == kDecayParamId || id == kReleaseParamId) std::snprintf(display, size, "%.0f ms", value);
    else if (id == kOutputParamId) std::snprintf(display, size, "%+.1f dB", value);
    else if (id == kCenterAzimuthParamId || id == kCenterElevationParamId) std::snprintf(display, size, "%+.0f deg", value);
    else if (id == kNeighborRadiusParamId) std::snprintf(display, size, "%.2f", value);
    else if (id == kMotionAmountParamId || id == kSpreadParamId || id == kCoherenceParamId || id == kChaosParamId || id == kLinkParamId || id == kSmoothParamId || id == kScanParamId || id == kMorphParamId || id == kSustainParamId || id == kHarmonicsParamId || id == kSubharmonicsParamId || id == kScoreDepthParamId) std::snprintf(display, size, "%.0f%%", value * 100.0);
    else if (id == kOrderParamId || id == kVoicesParamId || id == kRequiredNeighborsParamId) std::snprintf(display, size, "%.0f", value);
    else std::snprintf(display, size, "%.2f", value);
    return true;
}

bool paramsTextToValue(const clap_plugin_t*, clap_id, const char* display, double* value)
{
    if (!display || !value) return false;
    *value = std::atof(display);
    return true;
}

void paramsFlush(const clap_plugin_t* plugin, const clap_input_events_t* events, const clap_output_events_t*)
{
    readEvents(*self(plugin), events);
}
const clap_plugin_params_t paramsExt { paramsCount, paramsGetInfo, paramsGetValue, paramsValueToText, paramsTextToValue, paramsFlush };

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    if (!stream || !stream->write) return false;
    auto* state = self(plugin);
    SavedState saved {};
    saved.params = state->params;
    saved.score = loadScore(*state);
#if defined(__APPLE__)
    saved.guiPage = state->guiPage;
    saved.guiViewMode = state->guiViewMode;
    saved.guiViewAzDeg = state->guiViewAzDeg;
    saved.guiViewElDeg = state->guiViewElDeg;
    saved.guiViewZoom = state->guiViewZoom;
#endif
    return writeExact(stream, &saved, sizeof(saved));
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) return false;
    uint32_t version = 0;
    if (!readExact(stream, &version, sizeof(version))) return false;
    auto* state = self(plugin);
    if (version == 1u) {
        SavedStateV1 legacy {};
        legacy.version = version;
        if (!readExact(stream, reinterpret_cast<uint8_t*>(&legacy) + sizeof(version), sizeof(legacy) - sizeof(version))) return false;
        state->params = migrateLegacy(legacy.params);
    } else if (version == 2u) {
        SavedStateV2 legacy {};
        legacy.version = version;
        if (!readExact(stream, reinterpret_cast<uint8_t*>(&legacy) + sizeof(version), sizeof(legacy) - sizeof(version))) return false;
        state->params = migrateV2(legacy.params);
#if defined(__APPLE__)
        state->guiPage = std::clamp(legacy.guiPage, 0, 2);
        state->guiViewMode = std::clamp(legacy.guiViewMode, -1, 2);
        state->guiViewAzDeg = std::clamp(legacy.guiViewAzDeg, -180.0f, 180.0f);
        state->guiViewElDeg = std::clamp(legacy.guiViewElDeg, -85.0f, 85.0f);
        state->guiViewZoom = std::clamp(legacy.guiViewZoom, 0.55f, 2.4f);
#endif
    } else if (version == 3u) {
        SavedStateV3 legacy {};
        legacy.version = version;
        if (!readExact(stream, reinterpret_cast<uint8_t*>(&legacy) + sizeof(version), sizeof(legacy) - sizeof(version))) return false;
        state->params = migrateV3(legacy.params);
#if defined(__APPLE__)
        state->guiPage = std::clamp(legacy.guiPage, 0, 2);
        state->guiViewMode = std::clamp(legacy.guiViewMode, -1, 2);
        state->guiViewAzDeg = std::clamp(legacy.guiViewAzDeg, -180.0f, 180.0f);
        state->guiViewElDeg = std::clamp(legacy.guiViewElDeg, -85.0f, 85.0f);
        state->guiViewZoom = std::clamp(legacy.guiViewZoom, 0.55f, 2.4f);
#endif
    } else if (version == 4u) {
        SavedStateV4 legacy {};
        legacy.version = version;
        if (!readExact(stream, reinterpret_cast<uint8_t*>(&legacy) + sizeof(version), sizeof(legacy) - sizeof(version))) return false;
        state->params = migrateV4(legacy.params);
#if defined(__APPLE__)
        state->guiPage = std::clamp(legacy.guiPage, 0, 2);
        state->guiViewMode = std::clamp(legacy.guiViewMode, -1, 2);
        state->guiViewAzDeg = std::clamp(legacy.guiViewAzDeg, -180.0f, 180.0f);
        state->guiViewElDeg = std::clamp(legacy.guiViewElDeg, -85.0f, 85.0f);
        state->guiViewZoom = std::clamp(legacy.guiViewZoom, 0.55f, 2.4f);
#endif
    } else if (version == kStateVersion) {
        SavedState saved {};
        saved.version = version;
        if (!readExact(stream, reinterpret_cast<uint8_t*>(&saved) + sizeof(version), sizeof(saved) - sizeof(version))) return false;
        state->params = saved.params;
        storeScore(*state, saved.score);
#if defined(__APPLE__)
        state->guiPage = std::clamp(saved.guiPage, 0, 2);
        state->guiViewMode = std::clamp(saved.guiViewMode, -1, 2);
        state->guiViewAzDeg = std::clamp(saved.guiViewAzDeg, -180.0f, 180.0f);
        state->guiViewElDeg = std::clamp(saved.guiViewElDeg, -85.0f, 85.0f);
        state->guiViewZoom = std::clamp(saved.guiViewZoom, 0.55f, 2.4f);
#endif
    } else {
        return false;
    }
    state->engine.setParams(state->params);
    state->engine.setScore(loadScore(*state));
    state->params = state->engine.params();
    return true;
}
const clap_plugin_state_t stateExt { stateSave, stateLoad };

} // namespace

#if defined(__APPLE__)

static NSColor* votColor(int rgb, double alpha = 1.0)
{
    return s3g::clap_gui::color(rgb, alpha);
}

static float votLinearToSrgb(float value)
{
    const float x = std::clamp(value, 0.0f, 1.0f);
    return x <= 0.0031308f ? x * 12.92f : 1.055f * std::pow(x, 1.0f / 2.4f) - 0.055f;
}

static NSColor* votPointColor(float azimuthDeg, float elevationDeg, float distance, bool selected)
{
    const float hue = std::fmod((azimuthDeg / 360.0f) + 1.0f, 1.0f);
    const float light = std::clamp((std::clamp(elevationDeg, -90.0f, 90.0f) + 90.0f) / 180.0f, 0.30f, 0.86f);
    const float chroma = std::clamp(distance / 2.0f, 0.10f, 1.0f) * 0.34f;
    const float a = std::cos(hue * 2.0f * static_cast<float>(M_PI)) * chroma;
    const float b = std::sin(hue * 2.0f * static_cast<float>(M_PI)) * chroma;
    const float l3 = light + 0.3963377774f * a + 0.2158037573f * b;
    const float m3 = light - 0.1055613458f * a - 0.0638541728f * b;
    const float s3 = light - 0.0894841775f * a - 1.2914855480f * b;
    const float l = l3 * l3 * l3;
    const float m = m3 * m3 * m3;
    const float s = s3 * s3 * s3;
    float r = votLinearToSrgb(4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s);
    float g = votLinearToSrgb(-1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s);
    float blue = votLinearToSrgb(-0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s);
    const float grayMix = selected ? 0.06f : 0.16f;
    r = r * (1.0f - grayMix) + 0.72f * grayMix;
    g = g * (1.0f - grayMix) + 0.72f * grayMix;
    blue = blue * (1.0f - grayMix) + 0.72f * grayMix;
    return [NSColor colorWithCalibratedRed:r green:g blue:blue alpha:selected ? 1.0 : 0.88];
}

static std::vector<float> readWavMono(NSURL* url)
{
    std::vector<float> result;
    NSData* data = [NSData dataWithContentsOfURL:url];
    if (!data || [data length] < 44) return result;
    const uint8_t* bytes = static_cast<const uint8_t*>([data bytes]);
    const size_t size = [data length];
    const auto u16 = [&](size_t offset) -> uint16_t {
        return offset + 1 < size ? static_cast<uint16_t>(bytes[offset] | (bytes[offset + 1] << 8)) : 0u;
    };
    const auto u32 = [&](size_t offset) -> uint32_t {
        return offset + 3 < size
            ? static_cast<uint32_t>(bytes[offset] | (bytes[offset + 1] << 8) | (bytes[offset + 2] << 16) | (bytes[offset + 3] << 24))
            : 0u;
    };
    if (std::memcmp(bytes, "RIFF", 4) != 0 || std::memcmp(bytes + 8, "WAVE", 4) != 0) return result;
    uint16_t audioFormat = 0;
    uint16_t channels = 0;
    uint16_t bits = 0;
    size_t dataOffset = 0;
    uint32_t dataSize = 0;
    for (size_t offset = 12; offset + 8 <= size;) {
        const uint32_t chunkSize = u32(offset + 4);
        if (offset + 8 + chunkSize > size) break;
        if (std::memcmp(bytes + offset, "fmt ", 4) == 0 && chunkSize >= 16) {
            audioFormat = u16(offset + 8);
            channels = u16(offset + 10);
            bits = u16(offset + 22);
        } else if (std::memcmp(bytes + offset, "data", 4) == 0) {
            dataOffset = offset + 8;
            dataSize = chunkSize;
        }
        offset += 8 + chunkSize + (chunkSize & 1u);
    }
    if (dataOffset == 0 || channels == 0) return result;
    const uint32_t bytesPerSample = bits / 8u;
    if (bytesPerSample == 0) return result;
    const uint32_t frameBytes = bytesPerSample * channels;
    const uint32_t frames = dataSize / std::max<uint32_t>(1u, frameBytes);
    result.reserve(frames);
    for (uint32_t frame = 0; frame < frames; ++frame) {
        const size_t offset = dataOffset + static_cast<size_t>(frame) * frameBytes;
        float sum = 0.0f;
        for (uint32_t channel = 0; channel < channels; ++channel) {
            const size_t p = offset + channel * bytesPerSample;
            float value = 0.0f;
            if (audioFormat == 3 && bits == 32 && p + 3 < size) {
                std::memcpy(&value, bytes + p, sizeof(float));
            } else if (audioFormat == 1 && bits == 16 && p + 1 < size) {
                const int16_t sample = static_cast<int16_t>(bytes[p] | (bytes[p + 1] << 8));
                value = static_cast<float>(sample) / 32768.0f;
            } else if (audioFormat == 1 && bits == 24 && p + 2 < size) {
                int32_t sample = static_cast<int32_t>(bytes[p] | (bytes[p + 1] << 8) | (bytes[p + 2] << 16));
                if ((sample & 0x800000) != 0) sample |= ~0xffffff;
                value = static_cast<float>(sample) / 8388608.0f;
            } else if (audioFormat == 1 && bits == 32 && p + 3 < size) {
                int32_t sample = 0;
                std::memcpy(&sample, bytes + p, sizeof(int32_t));
                value = static_cast<float>(sample) / 2147483648.0f;
            }
            sum += value;
        }
        result.push_back(sum / static_cast<float>(channels));
    }
    return result;
}

@interface S3GAmbiVotEncoderView : NSView {
    Plugin* _plugin;
    NSTimer* _timer;
    clap_id _dragParam;
    int _dragArea;
    int _openMenu;
    int _hoverMenuItem;
    uint32_t _menuItemCount;
    NSPoint _menuOrigin;
    CGFloat _menuWidth;
    int _leftPage;
    uint32_t _selectedVoice;
    uint32_t _selectedScoreNode;
    int _scoreDragLane;
    int _viewMode;
    CGFloat _viewAzDeg;
    CGFloat _viewElDeg;
    CGFloat _viewZoom;
    BOOL _dragView;
    NSPoint _lastDragPoint;
    std::array<std::array<s3g::AmbiVotMotionPoint, 48>, s3g::kAmbiVotMaxVoices> _trails;
    uint32_t _trailHead;
    uint32_t _trailCount;
}
- (instancetype)initWithPlugin:(Plugin*)plugin;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
@end

@implementation S3GAmbiVotEncoderView

- (instancetype)initWithPlugin:(Plugin*)plugin
{
    self = [super initWithFrame:NSMakeRect(0, 0, kGuiW, kGuiH)];
    if (self) {
        _plugin = plugin;
        _timer = nil;
        _dragParam = CLAP_INVALID_ID;
        _dragArea = 0;
        _openMenu = 0;
        _hoverMenuItem = -1;
        _menuItemCount = 0;
        _menuOrigin = NSMakePoint(0, 0);
        _menuWidth = 124.0;
        _leftPage = plugin ? plugin->guiPage : 0;
        _selectedVoice = 0;
        _selectedScoreNode = 0;
        _scoreDragLane = -1;
        _viewMode = plugin ? plugin->guiViewMode : 2;
        _viewAzDeg = plugin ? plugin->guiViewAzDeg : 38.0;
        _viewElDeg = plugin ? plugin->guiViewElDeg : 32.0;
        _viewZoom = plugin ? plugin->guiViewZoom : 1.0;
        _dragView = NO;
        _lastDragPoint = NSMakePoint(0, 0);
        _trailHead = 0;
        _trailCount = 0;
        [self setWantsLayer:YES];
    }
    return self;
}

- (BOOL)isFlipped { return YES; }

- (void)updateTrackingAreas
{
    for (NSTrackingArea* area in [self trackingAreas]) [self removeTrackingArea:area];
    [super updateTrackingAreas];
    const NSTrackingAreaOptions options = NSTrackingMouseMoved | NSTrackingActiveInKeyWindow | NSTrackingInVisibleRect;
    NSTrackingArea* area = [[NSTrackingArea alloc] initWithRect:NSZeroRect options:options owner:self userInfo:nil];
    [self addTrackingArea:[area autorelease]];
}

- (void)storeViewState
{
    if (!_plugin) return;
    _plugin->guiPage = _leftPage;
    _plugin->guiViewMode = _viewMode;
    _plugin->guiViewAzDeg = static_cast<float>(_viewAzDeg);
    _plugin->guiViewElDeg = static_cast<float>(_viewElDeg);
    _plugin->guiViewZoom = static_cast<float>(_viewZoom);
}

- (s3g::AmbiVotMotionPoint)snapshotPoint:(uint32_t)index
{
    s3g::AmbiVotMotionPoint point {};
    if (!_plugin || index >= s3g::kAmbiVotMaxVoices) return point;
    point.azimuthDeg = _plugin->guiAzimuth[index].load(std::memory_order_relaxed);
    point.elevationDeg = _plugin->guiElevation[index].load(std::memory_order_relaxed);
    point.distance = _plugin->guiDistance[index].load(std::memory_order_relaxed);
    point.u = _plugin->guiU[index].load(std::memory_order_relaxed);
    point.v = _plugin->guiV[index].load(std::memory_order_relaxed);
    return point;
}

- (void)captureTrails
{
    if (!_plugin) return;
    for (uint32_t i = 0; i < s3g::kAmbiVotMaxVoices; ++i) _trails[i][_trailHead] = [self snapshotPoint:i];
    _trailHead = (_trailHead + 1u) % 48u;
    _trailCount = std::min<uint32_t>(48u, _trailCount + 1u);
}

- (void)startRefreshTimer
{
    if (_timer) return;
    _timer = [NSTimer timerWithTimeInterval:1.0 / 30.0 target:self selector:@selector(refresh:) userInfo:nil repeats:YES];
    [[NSRunLoop mainRunLoop] addTimer:_timer forMode:NSRunLoopCommonModes];
}

- (void)stopRefreshTimer
{
    if (!_timer) return;
    [_timer invalidate];
    _timer = nil;
}

- (void)refresh:(NSTimer*)timer
{
    (void)timer;
    if ([self isHidden] || !_plugin || !_plugin->guiVisible.load(std::memory_order_relaxed)) return;
    if (!s3g::clap_support::hostAppIsActive()) return;
    [self captureTrails];
    [self setNeedsDisplay:YES];
}

- (void)dealloc
{
    [self storeViewState];
    [self stopRefreshTimer];
    [super dealloc];
}

- (NSRect)leftPanelRect { return NSMakeRect(18, 42, 596, 834); }
- (NSRect)leftContentRect { return NSMakeRect(34, 72, 564, 788); }

- (NSRect)pageButtonRect:(int)index
{
    return NSMakeRect(130.0 + static_cast<CGFloat>(index) * 58.0, 46.0, 52.0, 13.0);
}

- (NSRect)viewButtonRect:(int)index
{
    return NSMakeRect(430.0 + static_cast<CGFloat>(index) * 49.0, 46.0, 43.0, 13.0);
}

- (NSRect)zoomButtonRect:(int)index
{
    return NSMakeRect(378.0 + static_cast<CGFloat>(index) * 23.0, 46.0, 18.0, 13.0);
}

- (NSRect)synthLoadButtonRect { return NSMakeRect(824, 46, 48, 13); }
- (NSRect)scoreRemoveButtonRect { return NSMakeRect(1032, 484, 18, 13); }
- (NSRect)scoreAddButtonRect { return NSMakeRect(1054, 484, 18, 13); }
- (NSRect)scoreResetButtonRect { return NSMakeRect(1080, 484, 54, 13); }

- (void)setViewPreset:(int)mode
{
    _viewMode = mode;
    if (mode == 0) {
        _viewAzDeg = 0.0;
        _viewElDeg = 0.0;
    } else if (mode == 1) {
        _viewAzDeg = 90.0;
        _viewElDeg = 90.0;
    } else {
        _viewAzDeg = 38.0;
        _viewElDeg = 32.0;
    }
    [self storeViewState];
    [self setNeedsDisplay:YES];
}

- (CGFloat)viewScaleForRect:(NSRect)rect
{
    return std::min(rect.size.width, rect.size.height) * 0.25 * std::clamp(_viewZoom, 0.55, 2.20);
}

- (NSPoint)projectWorldPoint:(s3g::Vec3)point rect:(NSRect)rect depth:(CGFloat*)depth
{
    const CGFloat centerX = NSMidX(rect);
    const CGFloat centerY = NSMidY(rect);
    const CGFloat scale = [self viewScaleForRect:rect];
    if (_viewMode == 0) {
        if (depth) *depth = static_cast<CGFloat>(point.z);
        return NSMakePoint(centerX - static_cast<CGFloat>(point.y) * scale,
                           centerY - static_cast<CGFloat>(point.x) * scale);
    }
    if (_viewMode == 1) {
        if (depth) *depth = static_cast<CGFloat>(point.x);
        return NSMakePoint(centerX - static_cast<CGFloat>(point.y) * scale,
                           centerY - static_cast<CGFloat>(point.z) * scale);
    }
    const float azimuth = static_cast<float>(_viewAzDeg * M_PI / 180.0);
    const float elevation = static_cast<float>(_viewElDeg * M_PI / 180.0);
    const float ca = std::cos(azimuth);
    const float sa = std::sin(azimuth);
    const float ce = std::cos(elevation);
    const float se = std::sin(elevation);
    const float x1 = ca * point.x - sa * point.y;
    const float y1 = sa * point.x + ca * point.y;
    const float y2 = ce * y1 - se * point.z;
    const float z2 = se * y1 + ce * point.z;
    if (depth) *depth = static_cast<CGFloat>(z2);
    return NSMakePoint(centerX + static_cast<CGFloat>(x1) * scale,
                       centerY - static_cast<CGFloat>(y2) * scale);
}

- (NSPoint)projectMotionPoint:(const s3g::AmbiVotMotionPoint&)point rect:(NSRect)rect depth:(CGFloat*)depth
{
    const s3g::Vec3 direction = s3g::directionFromAed(point.azimuthDeg, point.elevationDeg);
    return [self projectWorldPoint:{ direction.x * point.distance, direction.y * point.distance, direction.z * point.distance }
                             rect:rect depth:depth];
}

- (void)drawPageButtons:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style
{
    static NSString* labels[] = { @"FIELD", @"VECTOR", @"SCORE" };
    const NSRect header = NSMakeRect(18, 42, 596, 21);
    for (int i = 0; i < 3; ++i) {
        s3g::clap_gui::drawHeaderButton([self pageButtonRect:i], header, labels[i], i == _leftPage, attrs, style);
    }
}

- (void)drawViewButtons:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style
{
    if (_leftPage != 0) return;
    static NSString* labels[] = { @"TOP", @"SIDE", @"3/4" };
    const NSRect header = NSMakeRect(18, 42, 596, 21);
    for (int i = 0; i < 3; ++i) {
        s3g::clap_gui::drawHeaderButton([self viewButtonRect:i], header, labels[i], i == _viewMode, attrs, style);
    }
    s3g::clap_gui::drawHeaderButton([self zoomButtonRect:0], header, @"-", false, attrs, style);
    s3g::clap_gui::drawHeaderButton([self zoomButtonRect:1], header, @"+", false, attrs, style);
}

- (void)drawField:(NSRect)rect attrs:(NSDictionary*)attrs
{
    [votColor(0x0b0b0b) setFill];
    NSRectFill(rect);
    [votColor(0x575757) setStroke];
    NSFrameRect(rect);
    [NSGraphicsContext saveGraphicsState];
    [[NSBezierPath bezierPathWithRect:NSInsetRect(rect, 1, 1)] addClip];

    const CGFloat radius = [self viewScaleForRect:rect];
    [votColor(0x343434) setStroke];
    NSBezierPath* sphere = [NSBezierPath bezierPathWithOvalInRect:NSMakeRect(NSMidX(rect) - radius, NSMidY(rect) - radius, radius * 2.0, radius * 2.0)];
    [sphere setLineWidth:0.8];
    [sphere stroke];
    NSBezierPath* axes = [NSBezierPath bezierPath];
    [axes moveToPoint:NSMakePoint(NSMidX(rect), NSMinY(rect) + 8)];
    [axes lineToPoint:NSMakePoint(NSMidX(rect), NSMaxY(rect) - 8)];
    [axes moveToPoint:NSMakePoint(NSMinX(rect) + 8, NSMidY(rect))];
    [axes lineToPoint:NSMakePoint(NSMaxX(rect) - 8, NSMidY(rect))];
    [axes setLineWidth:0.6];
    [axes stroke];

    const uint32_t voices = std::clamp<uint32_t>(_plugin->params.voices, 1u, s3g::kAmbiVotMaxVoices);
    _selectedVoice = std::min<uint32_t>(_selectedVoice, voices - 1u);
    struct ProjectedPoint {
        uint32_t index;
        CGFloat depth;
        NSPoint point;
        s3g::Vec3 world;
        s3g::AmbiVotMotionPoint motion;
    };
    std::array<ProjectedPoint, s3g::kAmbiVotMaxVoices> projected {};
    for (uint32_t voice = 0; voice < voices; ++voice) {
        projected[voice].index = voice;
        projected[voice].motion = [self snapshotPoint:voice];
        const s3g::Vec3 direction = s3g::directionFromAed(
            projected[voice].motion.azimuthDeg, projected[voice].motion.elevationDeg);
        projected[voice].world = {
            direction.x * projected[voice].motion.distance,
            direction.y * projected[voice].motion.distance,
            direction.z * projected[voice].motion.distance,
        };
        projected[voice].point = [self projectWorldPoint:projected[voice].world rect:rect depth:&projected[voice].depth];
    }
    NSBezierPath* weakEdges = [NSBezierPath bezierPath];
    NSBezierPath* mediumEdges = [NSBezierPath bezierPath];
    NSBezierPath* strongEdges = [NSBezierPath bezierPath];
    NSBezierPath* selectedEdges = [NSBezierPath bezierPath];
    const float neighborRadius = std::max(0.05f, _plugin->params.neighborRadius);
    for (uint32_t a = 0; a < voices; ++a) {
        for (uint32_t b = a + 1u; b < voices; ++b) {
            const float dx = projected[a].world.x - projected[b].world.x;
            const float dy = projected[a].world.y - projected[b].world.y;
            const float dz = projected[a].world.z - projected[b].world.z;
            const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (distance > neighborRadius) continue;
            const float proximity = 1.0f - distance / neighborRadius;
            NSBezierPath* edge = proximity > 0.66f ? strongEdges : (proximity > 0.33f ? mediumEdges : weakEdges);
            [edge moveToPoint:projected[a].point];
            [edge lineToPoint:projected[b].point];
            if (a == _selectedVoice || b == _selectedVoice) {
                [selectedEdges moveToPoint:projected[a].point];
                [selectedEdges lineToPoint:projected[b].point];
            }
        }
    }
    [votColor(0x707070, 0.22) setStroke];
    [weakEdges setLineWidth:0.65];
    [weakEdges stroke];
    [votColor(0x8a8a8a, 0.34) setStroke];
    [mediumEdges setLineWidth:0.85];
    [mediumEdges stroke];
    [votColor(0xb0b0b0, 0.52) setStroke];
    [strongEdges setLineWidth:1.05];
    [strongEdges stroke];
    const auto selectedMotion = [self snapshotPoint:_selectedVoice];
    [[votPointColor(selectedMotion.azimuthDeg, selectedMotion.elevationDeg, selectedMotion.distance, true)
        colorWithAlphaComponent:0.70] setStroke];
    [selectedEdges setLineWidth:1.35];
    [selectedEdges stroke];

    std::sort(projected.begin(), projected.begin() + voices,
        [](const ProjectedPoint& a, const ProjectedPoint& b) { return a.depth < b.depth; });
    NSDictionary* idAttrs = s3g::clap_gui::textAttrs(votColor(0x0b0b0b), voices > 32u ? 5.5 : 7.0);
    for (uint32_t drawIndex = 0; drawIndex < voices; ++drawIndex) {
        const auto& item = projected[drawIndex];
        const bool selected = item.index == _selectedVoice;
        const bool gated = _plugin->guiNeighborGate[item.index].load(std::memory_order_relaxed) != 0u;
        const CGFloat size = selected ? 15.0 : (voices > 32u ? 9.0 : 12.0);
        const NSRect pointRect = NSMakeRect(item.point.x - size * 0.5, item.point.y - size * 0.5, size, size);
        [[votPointColor(item.motion.azimuthDeg, item.motion.elevationDeg, item.motion.distance, selected)
            colorWithAlphaComponent:(gated || _plugin->params.mode == s3g::AmbiVotMode::Midi) ? 0.96 : 0.42] setFill];
        NSRectFill(pointRect);
        [votColor(selected ? 0xe0e0e0 : 0x111111) setStroke];
        NSFrameRect(pointRect);
        NSString* label = [NSString stringWithFormat:@"%u", item.index + 1u];
        const NSSize labelSize = [label sizeWithAttributes:idAttrs];
        [label drawAtPoint:NSMakePoint(NSMidX(pointRect) - labelSize.width * 0.5,
                                       NSMidY(pointRect) - labelSize.height * 0.5 - 0.5)
            withAttributes:idAttrs];
    }
    [NSGraphicsContext restoreGraphicsState];

    const auto selected = [self snapshotPoint:_selectedVoice];
    const uint32_t neighbors = _plugin->guiNeighborCount[_selectedVoice].load(std::memory_order_relaxed);
    const bool gated = _plugin->guiNeighborGate[_selectedVoice].load(std::memory_order_relaxed) != 0u;
    NSString* readout = [NSString stringWithFormat:@"V%u   AZ %+06.1f   EL %+05.1f   D %.2f   N %u/%u   %@",
        _selectedVoice + 1u, selected.azimuthDeg, selected.elevationDeg, selected.distance,
        neighbors, _plugin->params.requiredNeighbors, gated ? @"ON" : @"OFF"];
    [readout drawAtPoint:NSMakePoint(rect.origin.x + 10, rect.origin.y + 9) withAttributes:attrs];
}

- (void)drawMiniWave:(const std::array<float, s3g::kAmbiVotTableSize>&)table rect:(NSRect)rect color:(NSColor*)color
{
    NSBezierPath* path = [NSBezierPath bezierPath];
    for (uint32_t i = 0; i < s3g::kAmbiVotTableSize; ++i) {
        const CGFloat x = rect.origin.x + static_cast<CGFloat>(i) / static_cast<CGFloat>(s3g::kAmbiVotTableSize - 1u) * rect.size.width;
        const CGFloat y = NSMidY(rect) - static_cast<CGFloat>(table[i]) * rect.size.height * 0.40;
        if (i == 0u) [path moveToPoint:NSMakePoint(x, y)];
        else [path lineToPoint:NSMakePoint(x, y)];
    }
    [color setStroke];
    [path setLineWidth:0.75];
    [path stroke];
}

- (void)drawVectorPage:(NSRect)rect attrs:(NSDictionary*)attrs valueAttrs:(NSDictionary*)valueAttrs style:(const s3g::clap_gui::Style&)style
{
    auto bank = activeBank(*_plugin);
    if (!bank) return;
    const NSRect bankRect = NSMakeRect(rect.origin.x + 8, rect.origin.y + 8, 252, 252);
    const NSRect pathRect = NSMakeRect(rect.origin.x + 278, rect.origin.y + 8, 270, 252);
    [votColor(0x0b0b0b) setFill];
    NSRectFill(bankRect);
    NSRectFill(pathRect);
    [votColor(0x575757) setStroke];
    NSFrameRect(bankRect);
    NSFrameRect(pathRect);
    const uint32_t voices = std::clamp<uint32_t>(_plugin->params.voices, 1u, s3g::kAmbiVotMaxVoices);
    _selectedVoice = std::min<uint32_t>(_selectedVoice, voices - 1u);
    const auto selected = [self snapshotPoint:_selectedVoice];
    const auto vectorWeights = s3g::ambiVotVectorWeights(
        selected.u, selected.v, _plugin->params.morph);
    const float maxWeight = *std::max_element(vectorWeights.values.begin(), vectorWeights.values.end());
    const CGFloat cellW = bankRect.size.width / 4.0;
    const CGFloat cellH = bankRect.size.height / 4.0;
    for (uint32_t displayRow = 0; displayRow < 4u; ++displayRow) {
        const uint32_t row = 3u - displayRow;
        for (uint32_t column = 0; column < 4u; ++column) {
            const uint32_t table = row * 4u + column;
            const NSRect cell = NSMakeRect(bankRect.origin.x + column * cellW,
                                           bankRect.origin.y + displayRow * cellH, cellW, cellH);
            const CGFloat influence = std::sqrt(vectorWeights.values[table]
                / std::max(0.000001f, maxWeight));
            [[NSColor colorWithWhite:0.055 + influence * 0.15 alpha:1.0] setFill];
            NSRectFill(NSInsetRect(cell, 1, 1));
            [votColor(0x303030) setStroke];
            NSFrameRect(cell);
            [self drawMiniWave:bank->tables[table]
                          rect:NSInsetRect(cell, 5, 8)
                         color:[NSColor colorWithWhite:0.38 + influence * 0.35 alpha:1.0]];
        }
    }
    [@"TABLE BANK" drawAtPoint:NSMakePoint(bankRect.origin.x + 8, bankRect.origin.y + 7) withAttributes:attrs];
    [@"VECTOR PATH" drawAtPoint:NSMakePoint(pathRect.origin.x + 8, pathRect.origin.y + 7) withAttributes:attrs];

    const NSRect pathInner = NSInsetRect(pathRect, 14, 28);
    for (int i = 1; i < 4; ++i) {
        [votColor(0x303030) setStroke];
        [NSBezierPath strokeLineFromPoint:NSMakePoint(pathInner.origin.x + pathInner.size.width * i / 4.0, pathInner.origin.y)
                                  toPoint:NSMakePoint(pathInner.origin.x + pathInner.size.width * i / 4.0, NSMaxY(pathInner))];
        [NSBezierPath strokeLineFromPoint:NSMakePoint(pathInner.origin.x, pathInner.origin.y + pathInner.size.height * i / 4.0)
                                  toPoint:NSMakePoint(NSMaxX(pathInner), pathInner.origin.y + pathInner.size.height * i / 4.0)];
    }
    const CGFloat hillRadiusX = vectorWeights.sigmaCells * pathInner.size.width / 4.0;
    const CGFloat hillRadiusY = vectorWeights.sigmaCells * pathInner.size.height / 4.0;
    for (uint32_t row = 0; row < 4u; ++row) {
        for (uint32_t column = 0; column < 4u; ++column) {
            const uint32_t table = row * 4u + column;
            const CGFloat centerU = (static_cast<CGFloat>(column) + 0.5) / 4.0;
            const CGFloat centerV = (static_cast<CGFloat>(row) + 0.5) / 4.0;
            const NSPoint center = NSMakePoint(pathInner.origin.x + centerU * pathInner.size.width,
                                               pathInner.origin.y + (1.0 - centerV) * pathInner.size.height);
            [votColor(0x3b3b3b) setStroke];
            NSBezierPath* hill = [NSBezierPath bezierPathWithOvalInRect:NSMakeRect(
                center.x - hillRadiusX, center.y - hillRadiusY, hillRadiusX * 2.0, hillRadiusY * 2.0)];
            [hill setLineWidth:0.65];
            [hill stroke];
            const CGFloat influence = std::sqrt(vectorWeights.values[table]
                / std::max(0.000001f, maxWeight));
            [[NSColor colorWithWhite:0.26 + influence * 0.50 alpha:1.0] setFill];
            NSRectFill(NSMakeRect(center.x - 2.0, center.y - 2.0, 4.0, 4.0));
        }
    }
    const bool vectorMoving = _plugin->params.motionScene != s3g::AmbiVotMotionScene::Manual;
    const float vectorCoverage = vectorMoving
        ? _plugin->params.scan
        : 0.0f;
    if (vectorCoverage > 0.001f) {
        const float linkedU = 0.5f + 0.5f * std::sin(selected.azimuthDeg * s3g::kPi / 180.0f);
        const float linkedV = std::clamp((selected.elevationDeg + 90.0f) / 180.0f, 0.0f, 1.0f);
        const float centerU = s3g::lerp(_plugin->params.vectorX, linkedU, _plugin->params.motionLink);
        const float centerV = s3g::lerp(_plugin->params.vectorY, linkedV, _plugin->params.motionLink);
        const float minimumU = centerU * (1.0f - vectorCoverage);
        const float maximumV = centerV * (1.0f - vectorCoverage) + vectorCoverage;
        const NSRect coverageRect = NSMakeRect(
            pathInner.origin.x + minimumU * pathInner.size.width,
            pathInner.origin.y + (1.0f - maximumV) * pathInner.size.height,
            vectorCoverage * pathInner.size.width,
            vectorCoverage * pathInner.size.height);
        [votColor(0x666666) setStroke];
        NSFrameRect(coverageRect);
    }
    if (_trailCount > 1u) {
        NSBezierPath* path = [NSBezierPath bezierPath];
        for (uint32_t step = 0; step < _trailCount; ++step) {
            const uint32_t index = (_trailHead + 48u - _trailCount + step) % 48u;
            const auto& point = _trails[_selectedVoice][index];
            const NSPoint p = NSMakePoint(pathInner.origin.x + point.u * pathInner.size.width,
                                          pathInner.origin.y + (1.0f - point.v) * pathInner.size.height);
            if (step == 0u) [path moveToPoint:p];
            else [path lineToPoint:p];
        }
        const auto point = [self snapshotPoint:_selectedVoice];
        [[votPointColor(point.azimuthDeg, point.elevationDeg, point.distance, true)
            colorWithAlphaComponent:0.64] setStroke];
        [path setLineWidth:1.4];
        [path stroke];
    }
    NSDictionary* idAttrs = s3g::clap_gui::textAttrs(votColor(0x0b0b0b), voices > 32u ? 5.5 : 7.0);
    for (uint32_t voice = 0; voice < voices; ++voice) {
        const auto point = [self snapshotPoint:voice];
        const bool selected = voice == _selectedVoice;
        const CGFloat size = selected ? 14.0 : (voices > 32u ? 8.0 : 11.0);
        const NSPoint p = NSMakePoint(pathInner.origin.x + point.u * pathInner.size.width,
                                      pathInner.origin.y + (1.0f - point.v) * pathInner.size.height);
        const NSRect marker = NSMakeRect(p.x - size * 0.5, p.y - size * 0.5, size, size);
        [votPointColor(point.azimuthDeg, point.elevationDeg, point.distance, selected) setFill];
        NSRectFill(marker);
        [votColor(selected ? 0xe0e0e0 : 0x111111) setStroke];
        NSFrameRect(marker);
        NSString* label = [NSString stringWithFormat:@"%u", voice + 1u];
        const NSSize sizeText = [label sizeWithAttributes:idAttrs];
        [label drawAtPoint:NSMakePoint(NSMidX(marker) - sizeText.width * 0.5, NSMidY(marker) - sizeText.height * 0.5 - 0.5)
            withAttributes:idAttrs];
    }

    const NSRect waveRect = NSMakeRect(rect.origin.x + 8, rect.origin.y + 278, 540, 300);
    [votColor(0x0b0b0b) setFill];
    NSRectFill(waveRect);
    [votColor(0x575757) setStroke];
    NSFrameRect(waveRect);
    [@"INTERPOLATED WAVEFORM" drawAtPoint:NSMakePoint(waveRect.origin.x + 8, waveRect.origin.y + 7) withAttributes:attrs];
    [votColor(0x2d2d2d) setStroke];
    [NSBezierPath strokeLineFromPoint:NSMakePoint(waveRect.origin.x + 8, NSMidY(waveRect))
                              toPoint:NSMakePoint(NSMaxX(waveRect) - 8, NSMidY(waveRect))];
    NSBezierPath* waveform = [NSBezierPath bezierPath];
    constexpr uint32_t samples = 512;
    for (uint32_t i = 0; i < samples; ++i) {
        const float phase = static_cast<float>(i) / static_cast<float>(samples - 1u);
        const float value = s3g::ambiVotVectorSample(*bank, vectorWeights, phase);
        const CGFloat x = waveRect.origin.x + 10.0 + phase * (waveRect.size.width - 20.0);
        const CGFloat y = NSMidY(waveRect) - static_cast<CGFloat>(value) * (waveRect.size.height * 0.39);
        if (i == 0u) [waveform moveToPoint:NSMakePoint(x, y)];
        else [waveform lineToPoint:NSMakePoint(x, y)];
    }
    [votPointColor(selected.azimuthDeg, selected.elevationDeg, selected.distance, true) setStroke];
    [waveform setLineWidth:1.25];
    [waveform stroke];
    NSString* waveformReadout = [NSString stringWithFormat:@"V%u   U %.3f   V %.3f   LINK %.0f%%",
        _selectedVoice + 1u, selected.u, selected.v, _plugin->params.motionLink * 100.0f];
    const NSSize waveformReadoutSize = [waveformReadout sizeWithAttributes:valueAttrs];
    [waveformReadout drawAtPoint:NSMakePoint(NSMaxX(waveRect) - waveformReadoutSize.width - 8, waveRect.origin.y + 7)
        withAttributes:valueAttrs];

    s3g::clap_gui::drawSlider(@"X", [NSString stringWithFormat:@"%.2f", _plugin->params.vectorX],
        _plugin->params.vectorX, rect.origin.y + 606, attrs, valueAttrs, style, rect.origin.x + 8, rect.origin.x + 86, rect.origin.x + 270, 170);
    s3g::clap_gui::drawSlider(@"Y", [NSString stringWithFormat:@"%.2f", _plugin->params.vectorY],
        _plugin->params.vectorY, rect.origin.y + 632, attrs, valueAttrs, style, rect.origin.x + 8, rect.origin.x + 86, rect.origin.x + 270, 170);
    s3g::clap_gui::drawSlider(@"SCAN", [NSString stringWithFormat:@"%.0f%%", _plugin->params.scan * 100.0f],
        _plugin->params.scan, rect.origin.y + 658, attrs, valueAttrs, style, rect.origin.x + 8, rect.origin.x + 86, rect.origin.x + 270, 170);
    s3g::clap_gui::drawSlider(@"MORPH", [NSString stringWithFormat:@"%.0f%%", _plugin->params.morph * 100.0f],
        _plugin->params.morph, rect.origin.y + 684, attrs, valueAttrs, style, rect.origin.x + 8, rect.origin.x + 86, rect.origin.x + 270, 170);
}

- (void)drawScorePage:(NSRect)rect attrs:(NSDictionary*)attrs valueAttrs:(NSDictionary*)valueAttrs style:(const s3g::clap_gui::Style&)style
{
    const auto score = loadScore(*_plugin);
    const uint32_t count = score.nodeCount;
    _selectedScoreNode = std::min<uint32_t>(_selectedScoreNode, count - 1u);
    const NSRect timelineRect = NSMakeRect(rect.origin.x + 8, rect.origin.y + 8, 540, 306);
    const NSRect routeRect = NSMakeRect(rect.origin.x + 8, rect.origin.y + 328, 540, 292);
    [votColor(0x0b0b0b) setFill];
    NSRectFill(timelineRect);
    NSRectFill(routeRect);
    [votColor(0x575757) setStroke];
    NSFrameRect(timelineRect);
    NSFrameRect(routeRect);
    [@"VECTOR SCORE" drawAtPoint:NSMakePoint(timelineRect.origin.x + 8, timelineRect.origin.y + 7) withAttributes:attrs];
    NSString* scoreStatus = [NSString stringWithFormat:@"%u NODES   %@   %.2f S",
        count, [NSString stringWithUTF8String:s3g::ambiVotScoreModeName(_plugin->params.scoreMode)],
        _plugin->params.scoreDurationSec];
    const NSSize statusSize = [scoreStatus sizeWithAttributes:valueAttrs];
    [scoreStatus drawAtPoint:NSMakePoint(NSMaxX(timelineRect) - statusSize.width - 8, timelineRect.origin.y + 7)
              withAttributes:valueAttrs];

    const NSRect uLane = NSMakeRect(timelineRect.origin.x + 14, timelineRect.origin.y + 34,
                                    timelineRect.size.width - 28, 112);
    const NSRect vLane = NSMakeRect(timelineRect.origin.x + 14, timelineRect.origin.y + 174,
                                    timelineRect.size.width - 28, 112);
    const float sustainStart = score.nodes[score.sustainStart].time;
    const float sustainEnd = score.nodes[score.sustainEnd].time;
    for (const auto lane : { uLane, vLane }) {
        [[NSColor colorWithWhite:0.12 alpha:1.0] setFill];
        NSRectFill(NSMakeRect(lane.origin.x + sustainStart * lane.size.width, lane.origin.y,
                              (sustainEnd - sustainStart) * lane.size.width, lane.size.height));
        [votColor(0x343434) setStroke];
        NSFrameRect(lane);
        for (int division = 1; division < 4; ++division) {
            [NSBezierPath strokeLineFromPoint:NSMakePoint(lane.origin.x + lane.size.width * division / 4.0, lane.origin.y)
                                      toPoint:NSMakePoint(lane.origin.x + lane.size.width * division / 4.0, NSMaxY(lane))];
        }
        [NSBezierPath strokeLineFromPoint:NSMakePoint(lane.origin.x, NSMidY(lane))
                                  toPoint:NSMakePoint(NSMaxX(lane), NSMidY(lane))];
    }
    [@"U" drawAtPoint:NSMakePoint(uLane.origin.x + 6, uLane.origin.y + 5) withAttributes:attrs];
    [@"V" drawAtPoint:NSMakePoint(vLane.origin.x + 6, vLane.origin.y + 5) withAttributes:attrs];

    auto drawLane = [&](NSRect lane, bool drawU, NSColor* color) {
        NSBezierPath* path = [NSBezierPath bezierPath];
        bool first = true;
        for (uint32_t segment = 0; segment + 1u < count; ++segment) {
            const auto& from = score.nodes[segment];
            const auto& to = score.nodes[segment + 1u];
            for (uint32_t step = 0; step <= 20u; ++step) {
                const float local = static_cast<float>(step) / 20.0f;
                const float interpolation = s3g::ambiVotScoreCurveValue(from.curve, local);
                const float time = s3g::lerp(from.time, to.time, local);
                const float value = s3g::lerp(drawU ? from.u : from.v, drawU ? to.u : to.v, interpolation);
                const NSPoint point = NSMakePoint(lane.origin.x + time * lane.size.width,
                                                   lane.origin.y + (1.0f - value) * lane.size.height);
                if (first) { [path moveToPoint:point]; first = false; }
                else [path lineToPoint:point];
            }
        }
        [color setStroke];
        [path setLineWidth:1.2];
        [path stroke];
        for (uint32_t node = 0; node < count; ++node) {
            const float value = drawU ? score.nodes[node].u : score.nodes[node].v;
            const NSPoint point = NSMakePoint(lane.origin.x + score.nodes[node].time * lane.size.width,
                                               lane.origin.y + (1.0f - value) * lane.size.height);
            const CGFloat size = node == _selectedScoreNode ? 9.0 : 7.0;
            [[NSColor colorWithWhite:node == _selectedScoreNode ? 0.88 : 0.58 alpha:1.0] setFill];
            NSRectFill(NSMakeRect(point.x - size * 0.5, point.y - size * 0.5, size, size));
        }
    };
    drawLane(uLane, true, votColor(0xd0d0d0));
    drawLane(vLane, false, votColor(0x858585));

    [@"FIELD ROUTE" drawAtPoint:NSMakePoint(routeRect.origin.x + 8, routeRect.origin.y + 7) withAttributes:attrs];
    const NSRect routeInner = NSMakeRect(routeRect.origin.x + 14, routeRect.origin.y + 30,
                                         routeRect.size.width - 28, routeRect.size.height - 44);
    [votColor(0x303030) setStroke];
    for (int division = 1; division < 4; ++division) {
        [NSBezierPath strokeLineFromPoint:NSMakePoint(routeInner.origin.x + routeInner.size.width * division / 4.0, routeInner.origin.y)
                                  toPoint:NSMakePoint(routeInner.origin.x + routeInner.size.width * division / 4.0, NSMaxY(routeInner))];
        [NSBezierPath strokeLineFromPoint:NSMakePoint(routeInner.origin.x, routeInner.origin.y + routeInner.size.height * division / 4.0)
                                  toPoint:NSMakePoint(NSMaxX(routeInner), routeInner.origin.y + routeInner.size.height * division / 4.0)];
    }
    NSBezierPath* route = [NSBezierPath bezierPath];
    for (uint32_t node = 0; node < count; ++node) {
        const NSPoint point = NSMakePoint(routeInner.origin.x + score.nodes[node].u * routeInner.size.width,
                                          routeInner.origin.y + (1.0f - score.nodes[node].v) * routeInner.size.height);
        if (node == 0u) [route moveToPoint:point];
        else [route lineToPoint:point];
    }
    [votColor(0x949494) setStroke];
    [route setLineWidth:1.15];
    [route stroke];
    NSDictionary* nodeAttrs = s3g::clap_gui::textAttrs(votColor(0x0b0b0b), 7.0);
    for (uint32_t node = 0; node < count; ++node) {
        const NSPoint point = NSMakePoint(routeInner.origin.x + score.nodes[node].u * routeInner.size.width,
                                          routeInner.origin.y + (1.0f - score.nodes[node].v) * routeInner.size.height);
        const CGFloat size = node == _selectedScoreNode ? 14.0 : 11.0;
        [[NSColor colorWithWhite:node == _selectedScoreNode ? 0.90 : 0.62 alpha:1.0] setFill];
        const NSRect marker = NSMakeRect(point.x - size * 0.5, point.y - size * 0.5, size, size);
        NSRectFill(marker);
        [votColor(0x151515) setStroke];
        NSFrameRect(marker);
        NSString* label = [NSString stringWithFormat:@"%u", node + 1u];
        const NSSize labelSize = [label sizeWithAttributes:nodeAttrs];
        [label drawAtPoint:NSMakePoint(NSMidX(marker) - labelSize.width * 0.5,
                                       NSMidY(marker) - labelSize.height * 0.5 - 0.5)
            withAttributes:nodeAttrs];
    }

    const auto& selected = score.nodes[_selectedScoreNode];
    s3g::clap_gui::drawSlider(@"TIME", [NSString stringWithFormat:@"%.3f", selected.time],
        selected.time, rect.origin.y + 648, attrs, valueAttrs, style,
        rect.origin.x + 8, rect.origin.x + 86, rect.origin.x + 270, 170);
    s3g::clap_gui::drawSlider(@"U", [NSString stringWithFormat:@"%.3f", selected.u],
        selected.u, rect.origin.y + 674, attrs, valueAttrs, style,
        rect.origin.x + 8, rect.origin.x + 86, rect.origin.x + 270, 170);
    s3g::clap_gui::drawSlider(@"V", [NSString stringWithFormat:@"%.3f", selected.v],
        selected.v, rect.origin.y + 700, attrs, valueAttrs, style,
        rect.origin.x + 8, rect.origin.x + 86, rect.origin.x + 270, 170);
    s3g::clap_gui::drawMenu(@"CURVE", [NSString stringWithUTF8String:s3g::ambiVotScoreCurveName(selected.curve)],
        rect.origin.y + 726, attrs, valueAttrs, style, rect.origin.x + 8, rect.origin.x + 86, 184);
}

- (void)drawMenu:(NSString*)name value:(NSString*)value y:(CGFloat)y attrs:(NSDictionary*)attrs valueAttrs:(NSDictionary*)valueAttrs style:(const s3g::clap_gui::Style&)style
{
    [self drawMenuAtX:630 name:name value:value y:y attrs:attrs valueAttrs:valueAttrs style:style];
}

- (void)drawMenuAtX:(CGFloat)panelX name:(NSString*)name value:(NSString*)value y:(CGFloat)y attrs:(NSDictionary*)attrs valueAttrs:(NSDictionary*)valueAttrs style:(const s3g::clap_gui::Style&)style
{
    s3g::clap_gui::drawMenu(name, value, y, attrs, valueAttrs, style, panelX + 16, panelX + 108, 124);
}

- (void)drawSlider:(NSString*)name param:(clap_id)param value:(double)value min:(double)min max:(double)max y:(CGFloat)y attrs:(NSDictionary*)attrs valueAttrs:(NSDictionary*)valueAttrs style:(const s3g::clap_gui::Style&)style
{
    [self drawSliderAtX:630 name:name param:param value:value min:min max:max y:y attrs:attrs valueAttrs:valueAttrs style:style];
}

- (void)drawSliderAtX:(CGFloat)panelX name:(NSString*)name param:(clap_id)param value:(double)value min:(double)min max:(double)max y:(CGFloat)y attrs:(NSDictionary*)attrs valueAttrs:(NSDictionary*)valueAttrs style:(const s3g::clap_gui::Style&)style
{
    double norm = (value - min) / std::max(0.000001, max - min);
    if (param == kMotionRateParamId || param == kScoreDurationParamId) norm = std::log(value / min) / std::log(max / min);
    else if (param == kAttackParamId || param == kDecayParamId || param == kReleaseParamId) norm = std::log(value / min) / std::log(max / min);
    char text[64] {};
    paramsValueToText(nullptr, param, value, text, sizeof(text));
    s3g::clap_gui::drawSlider(name, [NSString stringWithUTF8String:text], norm, y, attrs, valueAttrs, style,
        panelX + 16, panelX + 108, panelX + 196, 82);
}

- (void)drawPanels:(NSDictionary*)attrs valueAttrs:(NSDictionary*)valueAttrs style:(const s3g::clap_gui::Style&)style
{
    const auto p = _plugin->params;
    s3g::clap_gui::drawPanelFrame(630, 42, 250, 202, style);
    s3g::clap_gui::drawPanelHeader(@"SYNTH", true, 630, 42, 250, 21, attrs, style);
    const NSRect synthHeader = NSMakeRect(630, 42, 250, 21);
    s3g::clap_gui::drawHeaderActionButton(
        [self synthLoadButtonRect], synthHeader, @"LOAD", attrs, style);
    [self drawMenu:@"MODE" value:[NSString stringWithUTF8String:s3g::ambiVotModeName(p.mode)] y:78 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawMenu:@"WAVE" value:[NSString stringWithUTF8String:s3g::ambiVotPresetName(p.preset)] y:104 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawMenu:@"ORDER" value:[NSString stringWithFormat:@"%uOA", p.order] y:130 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"VOICES" param:kVoicesParamId value:p.voices min:1 max:64 y:156 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"BASE" param:kBaseNoteParamId value:p.baseNote min:12 max:96 y:182 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"TUNE" param:kTuneParamId value:p.tuneCents min:-1200 max:1200 y:208 attrs:attrs valueAttrs:valueAttrs style:style];

    s3g::clap_gui::drawPanelFrame(630, 256, 250, 176, style);
    s3g::clap_gui::drawPanelHeader(@"TUNING", true, 630, 256, 250, 21, attrs, style);
    [self drawMenu:@"SCALE" value:[NSString stringWithUTF8String:s3g::ambiVotScaleName(p.scale)] y:292 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"SPREAD" param:kPitchSpreadParamId value:p.pitchSpread min:0 max:2 y:318 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"DEV" param:kDetuneParamId value:p.detune min:0 max:1 y:344 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"HARM" param:kHarmonicsParamId value:p.harmonicAmount min:0 max:1 y:370 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"SUB" param:kSubharmonicsParamId value:p.subharmonicAmount min:0 max:1 y:396 attrs:attrs valueAttrs:valueAttrs style:style];

    s3g::clap_gui::drawPanelFrame(630, 444, 250, 150, style);
    s3g::clap_gui::drawPanelHeader(@"ENVELOPE", true, 630, 444, 250, 21, attrs, style);
    [self drawSlider:@"ATTACK" param:kAttackParamId value:p.attackMs min:1 max:2000 y:480 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"DECAY" param:kDecayParamId value:p.decayMs min:5 max:4000 y:506 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"SUSTAIN" param:kSustainParamId value:p.sustain min:0 max:1 y:532 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"RELEASE" param:kReleaseParamId value:p.releaseMs min:5 max:8000 y:558 attrs:attrs valueAttrs:valueAttrs style:style];

    s3g::clap_gui::drawPanelFrame(630, 606, 250, 48, style);
    s3g::clap_gui::drawPanelHeader(@"OUTPUT", true, 630, 606, 250, 21, attrs, style);
    [self drawSlider:@"OUT" param:kOutputParamId value:p.outputGainDb min:-60 max:12 y:630 attrs:attrs valueAttrs:valueAttrs style:style];

    constexpr CGFloat motionX = 896;
    s3g::clap_gui::drawPanelFrame(motionX, 42, 246, 426, style);
    s3g::clap_gui::drawPanelHeader(@"MOTION", true, motionX, 42, 246, 21, attrs, style);
    [self drawMenuAtX:motionX name:@"SCENE" value:[NSString stringWithUTF8String:s3g::ambiVotMotionSceneName(p.motionScene)] y:78 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawMenuAtX:motionX name:@"CLOCK" value:[NSString stringWithUTF8String:s3g::ambiVotMotionClockName(p.motionClock)] y:104 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSliderAtX:motionX name:@"RATE" param:kMotionRateParamId value:p.motionRateHz min:0.001 max:2 y:130 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSliderAtX:motionX name:@"DIV" param:kSyncDivisionParamId value:p.syncDivisionBeats min:0.25 max:64 y:156 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSliderAtX:motionX name:@"AMOUNT" param:kMotionAmountParamId value:p.motionAmount min:0 max:1 y:182 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSliderAtX:motionX name:@"SPREAD" param:kSpreadParamId value:p.motionSpread min:0 max:1 y:208 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSliderAtX:motionX name:@"COHERE" param:kCoherenceParamId value:p.motionCoherence min:0 max:1 y:234 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSliderAtX:motionX name:@"CHAOS" param:kChaosParamId value:p.motionChaos min:0 max:1 y:260 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSliderAtX:motionX name:@"LINK" param:kLinkParamId value:p.motionLink min:0 max:1 y:286 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSliderAtX:motionX name:@"NEAR" param:kNeighborRadiusParamId value:p.neighborRadius min:0.05 max:4 y:312 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSliderAtX:motionX name:@"NBR" param:kRequiredNeighborsParamId value:p.requiredNeighbors min:1 max:63 y:338 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSliderAtX:motionX name:@"SMOOTH" param:kSmoothParamId value:p.motionSmooth min:0 max:1 y:364 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSliderAtX:motionX name:@"AZIMUTH" param:kCenterAzimuthParamId value:p.centerAzimuthDeg min:-180 max:180 y:390 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSliderAtX:motionX name:@"ELEV" param:kCenterElevationParamId value:p.centerElevationDeg min:-90 max:90 y:416 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSliderAtX:motionX name:@"DISTANCE" param:kCenterDistanceParamId value:p.centerDistance min:0.15 max:2 y:442 attrs:attrs valueAttrs:valueAttrs style:style];

    const auto score = loadScore(*_plugin);
    s3g::clap_gui::drawPanelFrame(motionX, 480, 246, 202, style);
    s3g::clap_gui::drawPanelHeader(@"VECTOR SCORE", true, motionX, 480, 246, 21, attrs, style);
    const NSRect scoreHeader = NSMakeRect(motionX, 480, 246, 21);
    s3g::clap_gui::drawHeaderActionButton([self scoreRemoveButtonRect], scoreHeader, @"-", attrs, style);
    s3g::clap_gui::drawHeaderActionButton([self scoreAddButtonRect], scoreHeader, @"+", attrs, style);
    s3g::clap_gui::drawHeaderActionButton([self scoreResetButtonRect], scoreHeader, @"RESET", attrs, style);
    [self drawMenuAtX:motionX name:@"MODE" value:[NSString stringWithUTF8String:s3g::ambiVotScoreModeName(p.scoreMode)] y:516 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSliderAtX:motionX name:@"DURATION" param:kScoreDurationParamId value:p.scoreDurationSec min:0.25 max:60 y:542 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSliderAtX:motionX name:@"DEPTH" param:kScoreDepthParamId value:p.scoreDepth min:0 max:1 y:568 attrs:attrs valueAttrs:valueAttrs style:style];
    s3g::clap_gui::drawSlider(@"LOOP A", [NSString stringWithFormat:@"N%u", score.sustainStart + 1u],
        static_cast<double>(score.sustainStart) / static_cast<double>(score.nodeCount - 1u), 594,
        attrs, valueAttrs, style, motionX + 16, motionX + 108, motionX + 196, 82);
    s3g::clap_gui::drawSlider(@"LOOP B", [NSString stringWithFormat:@"N%u", score.sustainEnd + 1u],
        static_cast<double>(score.sustainEnd) / static_cast<double>(score.nodeCount - 1u), 620,
        attrs, valueAttrs, style, motionX + 16, motionX + 108, motionX + 196, 82);
    NSString* scoreInfo = [NSString stringWithFormat:@"%u NODES   LOOP %u-%u", score.nodeCount,
        score.sustainStart + 1u, score.sustainEnd + 1u];
    [scoreInfo drawAtPoint:NSMakePoint(motionX + 16, 654) withAttributes:valueAttrs];
}

- (void)drawOpenMenu:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style
{
    if (_openMenu <= 0 || _menuItemCount == 0u) return;
    static NSString* modeItems[] = { @"FREE", @"MIDI", @"BOTH" };
    static NSString* waveItems[] = { @"SINE", @"CLASSIC", @"DIGITAL", @"FORMANT", @"USER" };
    static NSString* orderItems[] = { @"1OA", @"2OA", @"3OA", @"4OA", @"5OA", @"6OA", @"7OA" };
    static NSString* sceneItems[] = { @"MANUAL", @"ORBIT", @"FLOW", @"PATH", @"PULSE" };
    static NSString* clockItems[] = { @"FREE", @"SYNC" };
    static NSString* scaleItems[] = { @"CHROM", @"MAJOR", @"MINOR", @"PENTA", @"WHOLE", @"HARM MIN" };
    static NSString* scoreModeItems[] = { @"OFF", @"ONE", @"LOOP", @"PING" };
    static NSString* curveItems[] = { @"LINEAR", @"SMOOTH", @"EXP", @"HOLD" };
    NSString** items = modeItems;
    int selected = 0;
    if (_openMenu == 1) selected = static_cast<int>(static_cast<uint32_t>(_plugin->params.mode));
    else if (_openMenu == 2) {
        items = waveItems;
        selected = static_cast<int>(static_cast<uint32_t>(_plugin->params.preset));
    } else if (_openMenu == 3) {
        items = orderItems;
        selected = static_cast<int>(_plugin->params.order - 1u);
    } else if (_openMenu == 4) {
        items = sceneItems;
        selected = static_cast<int>(static_cast<uint32_t>(_plugin->params.motionScene));
    } else if (_openMenu == 5) {
        items = clockItems;
        selected = static_cast<int>(static_cast<uint32_t>(_plugin->params.motionClock));
    } else if (_openMenu == 6) {
        items = scaleItems;
        selected = static_cast<int>(static_cast<uint32_t>(_plugin->params.scale));
    } else if (_openMenu == 7) {
        items = scoreModeItems;
        selected = static_cast<int>(static_cast<uint32_t>(_plugin->params.scoreMode));
    } else {
        items = curveItems;
        const auto score = loadScore(*_plugin);
        selected = static_cast<int>(static_cast<uint32_t>(score.nodes[std::min<uint32_t>(_selectedScoreNode, score.nodeCount - 1u)].curve));
    }
    const CGFloat itemHeight = 18.0;
    const NSRect menuRect = NSMakeRect(_menuOrigin.x, _menuOrigin.y, _menuWidth, itemHeight * _menuItemCount);
    s3g::clap_gui::drawDropdownMenu(menuRect, itemHeight, items, _menuItemCount, selected, _hoverMenuItem, attrs, style);
}

- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    if (!_plugin) return;
    const s3g::clap_gui::Style style = s3g::clap_gui::softTextStyle();
    [style.bg setFill];
    NSRectFill([self bounds]);
    NSDictionary* titleAttrs = s3g::clap_gui::softTitleAttrs();
    NSDictionary* labelAttrs = s3g::clap_gui::softLabelAttrs();
    NSDictionary* valueAttrs = s3g::clap_gui::softValueAttrs();
    [@"s3g AMBI VOT ENCODER 64" drawAtPoint:NSMakePoint(18, 14) withAttributes:titleAttrs];
    NSString* status = [NSString stringWithFormat:@"%@   64 CH",
        s3g::clap_gui::peakDbText(_plugin->outputPeak.load(std::memory_order_relaxed))];
    s3g::clap_gui::drawRightStatus(status, kGuiW, 14, valueAttrs);

    const NSRect left = [self leftPanelRect];
    s3g::clap_gui::drawPanelFrame(left.origin.x, left.origin.y, left.size.width, left.size.height, style);
    s3g::clap_gui::drawPanelHeader(@"VOICE SPACE", true, left.origin.x, left.origin.y, left.size.width, 21, labelAttrs, style);
    [self drawPageButtons:labelAttrs style:style];
    [self drawViewButtons:labelAttrs style:style];
    if (_leftPage == 0) [self drawField:[self leftContentRect] attrs:valueAttrs];
    else if (_leftPage == 1) [self drawVectorPage:[self leftContentRect] attrs:labelAttrs valueAttrs:valueAttrs style:style];
    else [self drawScorePage:[self leftContentRect] attrs:labelAttrs valueAttrs:valueAttrs style:style];
    [self drawPanels:labelAttrs valueAttrs:valueAttrs style:style];
    [self drawOpenMenu:valueAttrs style:style];
}

- (void)loadWave
{
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    [panel setCanChooseDirectories:NO];
    [panel setAllowsMultipleSelection:NO];
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    [panel setAllowedFileTypes:@[ @"wav", @"wave" ]];
#pragma clang diagnostic pop
    if ([panel runModal] != NSModalResponseOK) return;
    std::vector<float> wave = readWavMono([panel URL]);
    if (wave.size() < 32u) return;
    std::shared_ptr<const s3g::AmbiVotTableBank> bank =
        std::make_shared<s3g::AmbiVotTableBank>(s3g::ambiVotBankFromWave(wave));
    std::atomic_store_explicit(&_plugin->userBank, std::move(bank), std::memory_order_release);
    applyParam(*_plugin, kPresetParamId, static_cast<double>(s3g::AmbiVotPreset::User));
    [self setNeedsDisplay:YES];
}

- (double)defaultValueForParam:(clap_id)param
{
    for (const auto& def : kParams) {
        if (def.id == param) return def.def;
    }
    return 0.0;
}

- (void)setParam:(clap_id)param fromNorm:(double)norm
{
    norm = std::clamp(norm, 0.0, 1.0);
    double value = 0.0;
    switch (param) {
    case kVoicesParamId: value = 1.0 + norm * 63.0; break;
    case kBaseNoteParamId: value = 12.0 + norm * 84.0; break;
    case kTuneParamId: value = -1200.0 + norm * 2400.0; break;
    case kDetuneParamId:
    case kHarmonicsParamId:
    case kSubharmonicsParamId:
    case kScoreDepthParamId:
    case kMotionAmountParamId:
    case kSpreadParamId:
    case kCoherenceParamId:
    case kChaosParamId:
    case kLinkParamId:
    case kSmoothParamId:
    case kVectorXParamId:
    case kVectorYParamId:
    case kScanParamId:
    case kMorphParamId:
    case kSustainParamId:
        value = norm;
        break;
    case kPitchSpreadParamId: value = norm * 2.0; break;
    case kAttackParamId: value = 1.0 * std::pow(2000.0, norm); break;
    case kDecayParamId: value = 5.0 * std::pow(800.0, norm); break;
    case kReleaseParamId: value = 5.0 * std::pow(1600.0, norm); break;
    case kMotionRateParamId: value = 0.001 * std::pow(2000.0, norm); break;
    case kScoreDurationParamId: value = 0.25 * std::pow(240.0, norm); break;
    case kSyncDivisionParamId: value = 0.25 + norm * 63.75; break;
    case kCenterAzimuthParamId: value = -180.0 + norm * 360.0; break;
    case kCenterElevationParamId: value = -90.0 + norm * 180.0; break;
    case kCenterDistanceParamId: value = 0.15 + norm * 1.85; break;
    case kNeighborRadiusParamId: value = 0.05 + norm * 3.95; break;
    case kRequiredNeighborsParamId: value = 1.0 + norm * 62.0; break;
    case kOutputParamId: value = -60.0 + norm * 72.0; break;
    default: return;
    }
    applyParam(*_plugin, param, value);
}

- (void)updateDraggedSlider:(NSPoint)point
{
    if (_dragParam == CLAP_INVALID_ID) return;
    const CGFloat trackX = _dragArea == 2 ? 120.0 : (_dragArea == 7 ? 1004.0 : 738.0);
    const CGFloat trackWidth = _dragArea == 2 ? 170.0 : 82.0;
    [self setParam:_dragParam fromNorm:(point.x - trackX) / trackWidth];
    [self setNeedsDisplay:YES];
}

- (void)openMenu:(int)menu count:(uint32_t)count x:(CGFloat)x y:(CGFloat)y width:(CGFloat)width
{
    _openMenu = menu;
    _menuItemCount = count;
    _hoverMenuItem = -1;
    _menuOrigin = NSMakePoint(x, y + 18.0);
    _menuWidth = width;
    [self setNeedsDisplay:YES];
}

- (void)closeMenuAtPoint:(NSPoint)point
{
    const CGFloat itemHeight = 18.0;
    const NSRect menuRect = NSMakeRect(_menuOrigin.x, _menuOrigin.y, _menuWidth, itemHeight * _menuItemCount);
    const int hit = s3g::clap_gui::dropdownHitIndex(point, menuRect, itemHeight, _menuItemCount);
    if (hit >= 0) {
        if (_openMenu == 1) applyParam(*_plugin, kModeParamId, hit);
        else if (_openMenu == 2) applyParam(*_plugin, kPresetParamId, hit);
        else if (_openMenu == 3) applyParam(*_plugin, kOrderParamId, hit + 1);
        else if (_openMenu == 4) applyParam(*_plugin, kMotionSceneParamId, hit);
        else if (_openMenu == 5) applyParam(*_plugin, kMotionClockParamId, hit);
        else if (_openMenu == 6) applyParam(*_plugin, kScaleParamId, hit);
        else if (_openMenu == 7) applyParam(*_plugin, kScoreModeParamId, hit);
        else if (_openMenu == 8) {
            auto score = loadScore(*_plugin);
            const uint32_t node = std::min<uint32_t>(_selectedScoreNode, score.nodeCount - 1u);
            score.nodes[node].curve = static_cast<s3g::AmbiVotScoreCurve>(hit);
            storeScore(*_plugin, score);
        }
    }
    _openMenu = 0;
    _hoverMenuItem = -1;
    _menuItemCount = 0;
    _menuWidth = 124.0;
    [self setNeedsDisplay:YES];
}

- (int)hitVoiceAtPoint:(NSPoint)point
{
    const NSRect rect = [self leftContentRect];
    const uint32_t voices = std::clamp<uint32_t>(_plugin->params.voices, 1u, s3g::kAmbiVotMaxVoices);
    int bestVoice = -1;
    CGFloat bestDistance = 144.0;
    for (uint32_t voice = 0; voice < voices; ++voice) {
        CGFloat depth = 0.0;
        const NSPoint projected = [self projectMotionPoint:[self snapshotPoint:voice] rect:rect depth:&depth];
        const CGFloat dx = point.x - projected.x;
        const CGFloat dy = point.y - projected.y;
        const CGFloat distance = dx * dx + dy * dy;
        if (distance < bestDistance) {
            bestDistance = distance;
            bestVoice = static_cast<int>(voice);
        }
    }
    return bestVoice;
}

- (void)selectVectorVoiceAtPoint:(NSPoint)point
{
    const NSRect content = [self leftContentRect];
    const NSRect pathRect = NSMakeRect(content.origin.x + 278, content.origin.y + 8, 270, 252);
    const NSRect inner = NSInsetRect(pathRect, 14, 28);
    const uint32_t voices = std::clamp<uint32_t>(_plugin->params.voices, 1u, s3g::kAmbiVotMaxVoices);
    int bestVoice = -1;
    CGFloat bestDistance = 196.0;
    for (uint32_t voice = 0; voice < voices; ++voice) {
        const auto motion = [self snapshotPoint:voice];
        const NSPoint projected = NSMakePoint(inner.origin.x + motion.u * inner.size.width,
                                              inner.origin.y + (1.0f - motion.v) * inner.size.height);
        const CGFloat dx = point.x - projected.x;
        const CGFloat dy = point.y - projected.y;
        const CGFloat distance = dx * dx + dy * dy;
        if (distance < bestDistance) {
            bestDistance = distance;
            bestVoice = static_cast<int>(voice);
        }
    }
    if (bestVoice >= 0) _selectedVoice = static_cast<uint32_t>(bestVoice);
}

- (void)updateScoreDragAtPoint:(NSPoint)point
{
    auto score = loadScore(*_plugin);
    const uint32_t node = std::min<uint32_t>(_selectedScoreNode, score.nodeCount - 1u);
    const NSRect content = [self leftContentRect];
    if (_dragArea == 4) {
        const NSRect timelineRect = NSMakeRect(content.origin.x + 8, content.origin.y + 8, 540, 306);
        const NSRect lane = _scoreDragLane == 0
            ? NSMakeRect(timelineRect.origin.x + 14, timelineRect.origin.y + 34, timelineRect.size.width - 28, 112)
            : NSMakeRect(timelineRect.origin.x + 14, timelineRect.origin.y + 174, timelineRect.size.width - 28, 112);
        if (node > 0u && node + 1u < score.nodeCount) {
            const float minimum = score.nodes[node - 1u].time + 0.01f;
            const float maximum = score.nodes[node + 1u].time - 0.01f;
            score.nodes[node].time = std::clamp(static_cast<float>((point.x - lane.origin.x) / lane.size.width), minimum, maximum);
        }
        const float value = std::clamp(static_cast<float>(1.0 - (point.y - lane.origin.y) / lane.size.height), 0.0f, 1.0f);
        if (_scoreDragLane == 0) score.nodes[node].u = value;
        else score.nodes[node].v = value;
    } else if (_dragArea == 5) {
        const NSRect routeRect = NSMakeRect(content.origin.x + 8, content.origin.y + 328, 540, 292);
        const NSRect inner = NSMakeRect(routeRect.origin.x + 14, routeRect.origin.y + 30,
                                        routeRect.size.width - 28, routeRect.size.height - 44);
        score.nodes[node].u = std::clamp(static_cast<float>((point.x - inner.origin.x) / inner.size.width), 0.0f, 1.0f);
        score.nodes[node].v = std::clamp(static_cast<float>(1.0 - (point.y - inner.origin.y) / inner.size.height), 0.0f, 1.0f);
    } else if (_dragArea == 6) {
        const float value = std::clamp(static_cast<float>((point.x - (content.origin.x + 270)) / 170.0), 0.0f, 1.0f);
        if (_scoreDragLane == 2 && node > 0u && node + 1u < score.nodeCount) {
            score.nodes[node].time = std::clamp(value, score.nodes[node - 1u].time + 0.01f,
                                                score.nodes[node + 1u].time - 0.01f);
        } else if (_scoreDragLane == 3) score.nodes[node].u = value;
        else if (_scoreDragLane == 4) score.nodes[node].v = value;
    } else if (_dragArea == 8) {
        const float norm = std::clamp(static_cast<float>((point.x - 1004.0) / 82.0), 0.0f, 1.0f);
        const uint32_t selected = std::min<uint32_t>(
            static_cast<uint32_t>(std::lround(norm * static_cast<float>(score.nodeCount - 1u))),
            score.nodeCount - 1u);
        if (_scoreDragLane == 5) score.sustainStart = std::min<uint32_t>(selected, score.sustainEnd - 1u);
        else score.sustainEnd = std::max<uint32_t>(selected, score.sustainStart + 1u);
    }
    storeScore(*_plugin, score);
    [self setNeedsDisplay:YES];
}

- (void)mouseDown:(NSEvent*)event
{
    const NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    if (_openMenu > 0) {
        [self closeMenuAtPoint:point];
        return;
    }
    if (NSPointInRect(point, [self synthLoadButtonRect])) {
        [self loadWave];
        return;
    }
    if (NSPointInRect(point, [self scoreResetButtonRect])) {
        storeScore(*_plugin, s3g::ambiVotDefaultScore());
        _selectedScoreNode = 0u;
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, [self scoreRemoveButtonRect])) {
        auto score = loadScore(*_plugin);
        if (score.nodeCount > 2u) {
            --score.nodeCount;
            score.nodes[score.nodeCount - 1u].time = 1.0f;
            score.sustainStart = std::min<uint32_t>(score.sustainStart, score.nodeCount - 2u);
            score.sustainEnd = std::max<uint32_t>(score.sustainStart + 1u, score.nodeCount - 2u);
            storeScore(*_plugin, score);
            _selectedScoreNode = std::min<uint32_t>(_selectedScoreNode, score.nodeCount - 1u);
        }
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, [self scoreAddButtonRect])) {
        auto score = loadScore(*_plugin);
        if (score.nodeCount < s3g::kAmbiVotMaxScoreNodes) {
            const uint32_t oldLast = score.nodeCount - 1u;
            const uint32_t newLast = score.nodeCount;
            score.nodes[newLast] = score.nodes[oldLast];
            score.nodes[newLast].time = 1.0f;
            score.nodes[oldLast].time = 0.5f * (score.nodes[oldLast - 1u].time + 1.0f);
            ++score.nodeCount;
            score.sustainEnd = score.nodeCount - 2u;
            storeScore(*_plugin, score);
            _selectedScoreNode = oldLast;
        }
        [self setNeedsDisplay:YES];
        return;
    }
    for (int i = 0; i < 3; ++i) {
        if (NSPointInRect(point, [self pageButtonRect:i])) {
            _leftPage = i;
            [self storeViewState];
            [self setNeedsDisplay:YES];
            return;
        }
    }
    if (_leftPage == 0) {
        for (int i = 0; i < 3; ++i) {
            if (NSPointInRect(point, [self viewButtonRect:i])) {
                [self setViewPreset:i];
                return;
            }
        }
        for (int i = 0; i < 2; ++i) {
            if (NSPointInRect(point, [self zoomButtonRect:i])) {
                _viewZoom = std::clamp(_viewZoom * (i == 0 ? 0.82 : 1.22), 0.55, 2.20);
                _viewMode = -1;
                [self storeViewState];
                [self setNeedsDisplay:YES];
                return;
            }
        }
        if (NSPointInRect(point, [self leftContentRect])) {
            if (([event modifierFlags] & NSEventModifierFlagShift) != 0) {
                _dragView = YES;
                _lastDragPoint = point;
                return;
            }
            const int hit = [self hitVoiceAtPoint:point];
            if (hit >= 0) {
                _selectedVoice = static_cast<uint32_t>(hit);
                [self setNeedsDisplay:YES];
                return;
            }
        }
    } else if (_leftPage == 1) {
        const NSRect content = [self leftContentRect];
        const NSRect bankRect = NSMakeRect(content.origin.x + 8, content.origin.y + 8, 252, 252);
        const NSRect pathRect = NSMakeRect(content.origin.x + 278, content.origin.y + 8, 270, 252);
        if (NSPointInRect(point, pathRect)) {
            [self selectVectorVoiceAtPoint:point];
            [self setNeedsDisplay:YES];
            return;
        }
        if (NSPointInRect(point, bankRect)) {
            applyParam(*_plugin, kVectorXParamId, std::clamp((point.x - bankRect.origin.x) / bankRect.size.width, 0.0, 1.0));
            applyParam(*_plugin, kVectorYParamId, std::clamp(1.0 - (point.y - bankRect.origin.y) / bankRect.size.height, 0.0, 1.0));
            _dragParam = kVectorXParamId;
            _dragArea = 3;
            [self setNeedsDisplay:YES];
            return;
        }
    } else {
        const auto score = loadScore(*_plugin);
        const NSRect content = [self leftContentRect];
        const NSRect timelineRect = NSMakeRect(content.origin.x + 8, content.origin.y + 8, 540, 306);
        const NSRect lanes[] {
            NSMakeRect(timelineRect.origin.x + 14, timelineRect.origin.y + 34, timelineRect.size.width - 28, 112),
            NSMakeRect(timelineRect.origin.x + 14, timelineRect.origin.y + 174, timelineRect.size.width - 28, 112),
        };
        CGFloat bestDistance = 144.0;
        int bestNode = -1;
        int bestLane = -1;
        for (uint32_t node = 0; node < score.nodeCount; ++node) {
            for (int lane = 0; lane < 2; ++lane) {
                const float value = lane == 0 ? score.nodes[node].u : score.nodes[node].v;
                const NSPoint handle = NSMakePoint(lanes[lane].origin.x + score.nodes[node].time * lanes[lane].size.width,
                                                    lanes[lane].origin.y + (1.0f - value) * lanes[lane].size.height);
                const CGFloat dx = point.x - handle.x;
                const CGFloat dy = point.y - handle.y;
                const CGFloat distance = dx * dx + dy * dy;
                if (distance < bestDistance) {
                    bestDistance = distance;
                    bestNode = static_cast<int>(node);
                    bestLane = lane;
                }
            }
        }
        if (bestNode >= 0) {
            _selectedScoreNode = static_cast<uint32_t>(bestNode);
            _scoreDragLane = bestLane;
            _dragArea = 4;
            [self setNeedsDisplay:YES];
            return;
        }
        const NSRect routeRect = NSMakeRect(content.origin.x + 8, content.origin.y + 328, 540, 292);
        const NSRect routeInner = NSMakeRect(routeRect.origin.x + 14, routeRect.origin.y + 30,
                                             routeRect.size.width - 28, routeRect.size.height - 44);
        if (NSPointInRect(point, routeInner)) {
            bestDistance = 196.0;
            bestNode = -1;
            for (uint32_t node = 0; node < score.nodeCount; ++node) {
                const NSPoint handle = NSMakePoint(routeInner.origin.x + score.nodes[node].u * routeInner.size.width,
                                                    routeInner.origin.y + (1.0f - score.nodes[node].v) * routeInner.size.height);
                const CGFloat dx = point.x - handle.x;
                const CGFloat dy = point.y - handle.y;
                const CGFloat distance = dx * dx + dy * dy;
                if (distance < bestDistance) {
                    bestDistance = distance;
                    bestNode = static_cast<int>(node);
                }
            }
            if (bestNode >= 0) {
                _selectedScoreNode = static_cast<uint32_t>(bestNode);
                _dragArea = 5;
                [self setNeedsDisplay:YES];
                return;
            }
        }
    }

    struct MenuHit { int menu; uint32_t count; CGFloat x; CGFloat y; CGFloat width; };
    static constexpr MenuHit menus[] {
        { 1, 3, 738, 78, 124 }, { 2, 5, 738, 104, 124 }, { 3, 7, 738, 130, 124 },
        { 6, 6, 738, 292, 124 },
        { 4, 5, 1004, 78, 124 }, { 5, 2, 1004, 104, 124 },
        { 7, 4, 1004, 516, 124 },
    };
    for (const auto& menu : menus) {
        if (NSPointInRect(point, NSMakeRect(menu.x, menu.y - 1, menu.width, 17))) {
            [self openMenu:menu.menu count:menu.count x:menu.x y:menu.y width:menu.width];
            return;
        }
    }
    if (_leftPage == 2 && NSPointInRect(point, NSMakeRect(120, 797, 184, 17))) {
        [self openMenu:8 count:4 x:120 y:798 width:184];
        return;
    }

    struct SliderHit { clap_id param; CGFloat x; CGFloat y; int area; };
    static constexpr SliderHit sliders[] {
        { kVoicesParamId, 638, 156, 1 }, { kBaseNoteParamId, 638, 182, 1 }, { kTuneParamId, 638, 208, 1 },
        { kPitchSpreadParamId, 638, 318, 1 }, { kDetuneParamId, 638, 344, 1 },
        { kHarmonicsParamId, 638, 370, 1 }, { kSubharmonicsParamId, 638, 396, 1 },
        { kAttackParamId, 638, 480, 1 }, { kDecayParamId, 638, 506, 1 },
        { kSustainParamId, 638, 532, 1 }, { kReleaseParamId, 638, 558, 1 },
        { kOutputParamId, 638, 630, 1 },
        { kMotionRateParamId, 904, 130, 7 }, { kSyncDivisionParamId, 904, 156, 7 },
        { kMotionAmountParamId, 904, 182, 7 }, { kSpreadParamId, 904, 208, 7 },
        { kCoherenceParamId, 904, 234, 7 }, { kChaosParamId, 904, 260, 7 },
        { kLinkParamId, 904, 286, 7 }, { kNeighborRadiusParamId, 904, 312, 7 },
        { kRequiredNeighborsParamId, 904, 338, 7 }, { kSmoothParamId, 904, 364, 7 },
        { kCenterAzimuthParamId, 904, 390, 7 }, { kCenterElevationParamId, 904, 416, 7 },
        { kCenterDistanceParamId, 904, 442, 7 },
        { kScoreDurationParamId, 904, 542, 7 }, { kScoreDepthParamId, 904, 568, 7 },
    };
    for (const auto& slider : sliders) {
        if (NSPointInRect(point, NSMakeRect(slider.x, slider.y - 8, 232, 24))) {
            if ([event clickCount] >= 2) applyParam(*_plugin, slider.param, [self defaultValueForParam:slider.param]);
            else {
                _dragParam = slider.param;
                _dragArea = slider.area;
                [self updateDraggedSlider:point];
            }
            return;
        }
    }
    const CGFloat scoreLoopRows[] = { 594, 620 };
    for (int i = 0; i < 2; ++i) {
        if (!NSPointInRect(point, NSMakeRect(904, scoreLoopRows[i] - 8, 232, 24))) continue;
        if ([event clickCount] >= 2) {
            auto score = loadScore(*_plugin);
            if (i == 0) score.sustainStart = std::min<uint32_t>(2u, score.nodeCount - 2u);
            else score.sustainEnd = std::max<uint32_t>(score.sustainStart + 1u, score.nodeCount - 2u);
            storeScore(*_plugin, score);
            [self setNeedsDisplay:YES];
        } else {
            _dragArea = 8;
            _scoreDragLane = i + 5;
            [self updateScoreDragAtPoint:point];
        }
        return;
    }

    if (_leftPage == 1) {
        const CGFloat localRows[] = { 678, 704, 730, 756 };
        const clap_id localParams[] = { kVectorXParamId, kVectorYParamId, kScanParamId, kMorphParamId };
        for (int i = 0; i < 4; ++i) {
            if (NSPointInRect(point, NSMakeRect(38, localRows[i] - 8, 280, 24))) {
                if ([event clickCount] >= 2) applyParam(*_plugin, localParams[i], [self defaultValueForParam:localParams[i]]);
                else {
                    _dragParam = localParams[i];
                    _dragArea = 2;
                    [self updateDraggedSlider:point];
                }
                return;
            }
        }
    } else if (_leftPage == 2) {
        const CGFloat localRows[] = { 720, 746, 772 };
        for (int i = 0; i < 3; ++i) {
            if (!NSPointInRect(point, NSMakeRect(38, localRows[i] - 8, 280, 24))) continue;
            if ([event clickCount] >= 2) {
                auto score = loadScore(*_plugin);
                const auto defaults = s3g::ambiVotDefaultScore();
                const uint32_t node = std::min<uint32_t>(_selectedScoreNode, score.nodeCount - 1u);
                if (i == 0 && node > 0u && node + 1u < score.nodeCount) {
                    score.nodes[node].time = static_cast<float>(node) / static_cast<float>(score.nodeCount - 1u);
                } else if (i == 1) score.nodes[node].u = defaults.nodes[node].u;
                else if (i == 2) score.nodes[node].v = defaults.nodes[node].v;
                storeScore(*_plugin, score);
                [self setNeedsDisplay:YES];
            } else {
                _dragArea = 6;
                _scoreDragLane = i + 2;
                [self updateScoreDragAtPoint:point];
            }
            return;
        }
    }
}

- (void)mouseDragged:(NSEvent*)event
{
    const NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    if (_dragView) {
        const CGFloat dx = point.x - _lastDragPoint.x;
        const CGFloat dy = point.y - _lastDragPoint.y;
        _viewAzDeg += dx * 0.35;
        while (_viewAzDeg > 180.0) _viewAzDeg -= 360.0;
        while (_viewAzDeg <= -180.0) _viewAzDeg += 360.0;
        _viewElDeg = std::clamp(_viewElDeg - dy * 0.30, -85.0, 85.0);
        _viewMode = -1;
        _lastDragPoint = point;
        [self storeViewState];
        [self setNeedsDisplay:YES];
        return;
    }
    if (_dragArea == 3) {
        const NSRect content = [self leftContentRect];
        const NSRect bankRect = NSMakeRect(content.origin.x + 8, content.origin.y + 8, 252, 252);
        applyParam(*_plugin, kVectorXParamId, std::clamp((point.x - bankRect.origin.x) / bankRect.size.width, 0.0, 1.0));
        applyParam(*_plugin, kVectorYParamId, std::clamp(1.0 - (point.y - bankRect.origin.y) / bankRect.size.height, 0.0, 1.0));
        [self setNeedsDisplay:YES];
        return;
    }
    if ((_dragArea >= 4 && _dragArea <= 6) || _dragArea == 8) {
        [self updateScoreDragAtPoint:point];
        return;
    }
    [self updateDraggedSlider:point];
}

- (void)mouseUp:(NSEvent*)event
{
    (void)event;
    _dragParam = CLAP_INVALID_ID;
    _dragArea = 0;
    _scoreDragLane = -1;
    _dragView = NO;
}

- (void)mouseMoved:(NSEvent*)event
{
    if (_openMenu <= 0 || _menuItemCount == 0u) return;
    const NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    const CGFloat itemHeight = 18.0;
    const NSRect menuRect = NSMakeRect(_menuOrigin.x, _menuOrigin.y, _menuWidth, itemHeight * _menuItemCount);
    const int next = s3g::clap_gui::dropdownHitIndex(point, menuRect, itemHeight, _menuItemCount);
    if (next != _hoverMenuItem) {
        _hoverMenuItem = next;
        [self setNeedsDisplay:YES];
    }
}

@end

namespace {

bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool isFloating)
{
    return !isFloating && api && std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0;
}

bool guiGetPreferredApi(const clap_plugin_t*, const char** api, bool* isFloating)
{
    if (!api || !isFloating) return false;
    *api = CLAP_WINDOW_API_COCOA;
    *isFloating = false;
    return true;
}

bool guiCreate(const clap_plugin_t* plugin, const char* api, bool isFloating)
{
    if (!guiIsApiSupported(plugin, api, isFloating)) return false;
    auto* state = self(plugin);
    if (state->guiView) return true;
    state->guiView = [[S3GAmbiVotEncoderView alloc] initWithPlugin:state];
    return state->guiView != nullptr;
}

void guiDestroy(const clap_plugin_t* plugin)
{
    auto* state = self(plugin);
    if (!state || !state->guiView) return;
    state->guiVisible.store(false, std::memory_order_relaxed);
    auto* view = static_cast<S3GAmbiVotEncoderView*>(state->guiView);
    [view stopRefreshTimer];
    [view removeFromSuperview];
    [view release];
    state->guiView = nullptr;
}

bool guiSetScale(const clap_plugin_t*, double) { return true; }
bool guiGetSize(const clap_plugin_t*, uint32_t* width, uint32_t* height)
{
    if (!width || !height) return false;
    *width = kGuiW;
    *height = kGuiH;
    return true;
}
bool guiCanResize(const clap_plugin_t*) { return false; }
bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t*) { return false; }
bool guiAdjustSize(const clap_plugin_t*, uint32_t*, uint32_t*) { return false; }
bool guiSetSize(const clap_plugin_t* plugin, uint32_t width, uint32_t height)
{
    auto* state = self(plugin);
    if (!state->guiView) return false;
    [static_cast<NSView*>(state->guiView) setFrameSize:NSMakeSize(width, height)];
    return true;
}
bool guiSetParent(const clap_plugin_t* plugin, const clap_window_t* window)
{
    if (!window || !window->api || std::strcmp(window->api, CLAP_WINDOW_API_COCOA) != 0 || !window->cocoa) return false;
    auto* state = self(plugin);
    if (!state->guiView) return false;
    NSView* parent = static_cast<NSView*>(window->cocoa);
    NSView* view = static_cast<NSView*>(state->guiView);
    [parent addSubview:view];
    [view setFrame:NSMakeRect(0, 0, kGuiW, kGuiH)];
    return true;
}
bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}
bool guiShow(const clap_plugin_t* plugin)
{
    auto* state = self(plugin);
    if (!state->guiView) return false;
    state->guiVisible.store(true, std::memory_order_relaxed);
    [static_cast<NSView*>(state->guiView) setHidden:NO];
    [static_cast<S3GAmbiVotEncoderView*>(state->guiView) startRefreshTimer];
    return true;
}
bool guiHide(const clap_plugin_t* plugin)
{
    auto* state = self(plugin);
    if (!state->guiView) return false;
    state->guiVisible.store(false, std::memory_order_relaxed);
    [static_cast<S3GAmbiVotEncoderView*>(state->guiView) stopRefreshTimer];
    [static_cast<NSView*>(state->guiView) setHidden:YES];
    return true;
}

const clap_plugin_gui_t guiExt {
    guiIsApiSupported,
    guiGetPreferredApi,
    guiCreate,
    guiDestroy,
    guiSetScale,
    guiGetSize,
    guiCanResize,
    guiGetResizeHints,
    guiAdjustSize,
    guiSetSize,
    guiSetParent,
    guiSetTransient,
    guiSuggestTitle,
    guiShow,
    guiHide,
};

} // namespace
#endif

namespace {

const void* pluginGetExtension(const clap_plugin_t*, const char* id)
{
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &audioPorts;
    if (std::strcmp(id, CLAP_EXT_NOTE_PORTS) == 0) return &notePorts;
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) return &paramsExt;
    if (std::strcmp(id, CLAP_EXT_STATE) == 0) return &stateExt;
#if defined(__APPLE__)
    if (std::strcmp(id, CLAP_EXT_GUI) == 0) return &guiExt;
#endif
    return nullptr;
}

const char* const features[] {
    CLAP_PLUGIN_FEATURE_INSTRUMENT,
    CLAP_PLUGIN_FEATURE_SYNTHESIZER,
    CLAP_PLUGIN_FEATURE_AMBISONIC,
    nullptr,
};

const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-dsp.ambi-vot-encoder-64",
    "s3g Ambi VOT Encoder 64",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.4.0-pre",
    "Ambisonic vector-wavetable instrument with editable scoring, scale tuning, and AED motion.",
    features,
};

const clap_plugin_t* create(const clap_host_t* host)
{
    auto* state = new (std::nothrow) Plugin();
    if (!state) return nullptr;
    state->host = host;
    state->presetBanks[0] = makePresetBank(s3g::AmbiVotPreset::Sine);
    state->presetBanks[1] = makePresetBank(s3g::AmbiVotPreset::Classic);
    state->presetBanks[2] = makePresetBank(s3g::AmbiVotPreset::Digital);
    state->presetBanks[3] = makePresetBank(s3g::AmbiVotPreset::Formant);
    std::shared_ptr<const s3g::AmbiVotTableBank> empty;
    std::atomic_store_explicit(&state->userBank, std::move(empty), std::memory_order_release);
    storeScore(*state, s3g::ambiVotDefaultScore());
    state->engine.prepare(state->sampleRate);
    state->engine.setParams(state->params);
    state->engine.setScore(loadScore(*state));
    state->params = state->engine.params();
#if defined(__APPLE__)
    const auto& points = state->engine.motionPoints();
    for (uint32_t i = 0; i < s3g::kAmbiVotMaxVoices; ++i) {
        state->guiAzimuth[i].store(points[i].azimuthDeg, std::memory_order_relaxed);
        state->guiElevation[i].store(points[i].elevationDeg, std::memory_order_relaxed);
        state->guiDistance[i].store(points[i].distance, std::memory_order_relaxed);
        state->guiU[i].store(points[i].u, std::memory_order_relaxed);
        state->guiV[i].store(points[i].v, std::memory_order_relaxed);
        state->guiNeighborCount[i].store(0u, std::memory_order_relaxed);
        state->guiNeighborGate[i].store(0u, std::memory_order_relaxed);
    }
#endif
    state->plugin.desc = &descriptor;
    state->plugin.plugin_data = state;
    state->plugin.init = init;
    state->plugin.destroy = destroy;
    state->plugin.activate = activate;
    state->plugin.deactivate = deactivate;
    state->plugin.start_processing = startProcessing;
    state->plugin.stop_processing = stopProcessing;
    state->plugin.reset = reset;
    state->plugin.process = process;
    state->plugin.get_extension = pluginGetExtension;
    state->plugin.on_main_thread = onMainThread;
    return &state->plugin;
}

uint32_t factoryGetPluginCount(const clap_plugin_factory*) { return 1; }
const clap_plugin_descriptor_t* factoryGetPluginDescriptor(const clap_plugin_factory*, uint32_t index)
{
    return index == 0u ? &descriptor : nullptr;
}
const clap_plugin_t* factoryCreatePlugin(const clap_plugin_factory*, const clap_host_t* host, const char* pluginId)
{
    return pluginId && std::strcmp(pluginId, descriptor.id) == 0 ? create(host) : nullptr;
}

const clap_plugin_factory pluginFactory {
    factoryGetPluginCount,
    factoryGetPluginDescriptor,
    factoryCreatePlugin,
};

bool entryInit(const char*) { return true; }
void entryDeinit() {}
const void* entryGetFactory(const char* factoryId)
{
    return factoryId && std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0 ? &pluginFactory : nullptr;
}

} // namespace

extern "C" const CLAP_EXPORT clap_plugin_entry_t clap_entry {
    CLAP_VERSION_INIT,
    entryInit,
    entryDeinit,
    entryGetFactory,
};
