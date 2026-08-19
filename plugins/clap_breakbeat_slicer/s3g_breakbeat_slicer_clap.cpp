#include "s3g_breakbeat_slicer.h"
#include "../common/s3g_clap_state_stream.h"

#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/audio-ports-config.h>
#include <clap/ext/gui.h>
#include <clap/ext/note-name.h>
#include <clap/ext/note-ports.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>

#if defined(__APPLE__)
#import <AVFoundation/AVFoundation.h>
#import <Cocoa/Cocoa.h>
#import <CoreText/CoreText.h>
#include "../common/s3g_audio_file_export.h"
#include "../common/s3g_cocoa_gui.h"
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using s3g::breakbeat::BankSnapshot;
using s3g::breakbeat::EventKind;
using s3g::breakbeat::FilterMode;
using s3g::breakbeat::InsertSettings;
using s3g::breakbeat::InsertType;
using s3g::breakbeat::MixerSnapshot;
using s3g::breakbeat::MutationVariation;
using s3g::breakbeat::RenderEvent;
using s3g::breakbeat::SampleAnalysis;
using s3g::breakbeat::SampleAsset;
using s3g::breakbeat::Slice;

constexpr uint32_t kStateMagic = 0x53423353u; // "S3BS"
constexpr uint32_t kLegacyStateVersion = 9u;
constexpr uint32_t kPreviousStateVersion = 10u;
constexpr uint32_t kMutationContainerStateVersion = 11u;
constexpr uint32_t kStateVersion = 12u;
constexpr uint32_t kMutationStateMagic = 0x4154554du; // "MUTA"
constexpr uint32_t kLegacyMutationStateVersion = 1u;
constexpr uint32_t kStructuralMutationStateVersion = 2u;
constexpr uint32_t kMixerFxMutationStateVersion = 3u;
constexpr uint32_t kMutationStateVersion = 4u;
constexpr uint32_t kMaximumTransientPreRollMicroseconds = 20000u;
constexpr uint32_t kDefaultMinimumTransientSliceMilliseconds = 20u;
constexpr uint32_t kMaximumMinimumTransientSliceMilliseconds = 1000u;
constexpr uint64_t kMaximumEmbeddedAudioBytes = 1024ull * 1024ull * 1024ull;
constexpr uint32_t kGuiWidth = 1080u;
constexpr uint32_t kGuiHeight = 800u;
constexpr std::size_t kMaximumBlockEvents = 2048u;
constexpr std::size_t kMaximumPathBytes = 1024u;

constexpr clap_id kOutputGainParamId = 1u;
constexpr clap_id kVelocityParamId = 2u;
constexpr clap_id kStereoOutputConfigId = 2002u;
constexpr clap_id kSixteenChannelOutputConfigId = 2016u;

struct OutputConfigDef {
    clap_id id;
    const char* name;
    uint32_t channelCount;
    const char* portType;
};

const std::array<OutputConfigDef, 2u> kOutputConfigs {{
    { kStereoOutputConfigId, "Stereo", 2u, CLAP_PORT_STEREO },
    { kSixteenChannelOutputConfigId, "16 Channel", 16u, nullptr },
}};

struct ParamDef {
    clap_id id;
    const char* name;
    const char* module;
    double minimum;
    double maximum;
    double defaultValue;
    bool stepped;
};

constexpr std::array<ParamDef, 2u> kParamDefs {{
    { kOutputGainParamId, "Output Gain", "Output", -60.0, 12.0, -6.0,
        false },
    { kVelocityParamId, "Velocity Sensitivity", "MIDI", 0.0, 1.0, 1.0,
        false },
}};

struct SavedSlot {
    std::array<char, kMaximumPathBytes> path {};
    std::array<Slice, s3g::breakbeat::kMaximumSlicesPerSlot> slices {};
    s3g::breakbeat::Envelope envelope {};
    uint32_t sliceCount = 0u;
    uint32_t mappedSliceCount = 0u;
    float mixerGain = 1.0f;
    float mixerPan = 0.0f;
    float mixerLowEqDb = 0.0f;
    float mixerMidEqDb = 0.0f;
    float mixerHighEqDb = 0.0f;
    float mixerMidFrequencyHz = 900.0f;
    float mixerAuxSend = 0.0f;
    std::array<s3g::breakbeat::InsertSettings,
        s3g::breakbeat::kInsertSlotsPerStrip> inserts {};
    uint8_t rootNote = 48u;
    uint8_t mappedRootNote = 48u;
    uint8_t midiChannel = 0u;
    uint8_t muted = 0u;
    uint8_t solo = 0u;
    uint8_t embedded = 0u;
    uint8_t channelCount = 0u;
    uint32_t frameCount = 0u;
    double sampleRate = 0.0;
};

struct SavedState {
    uint32_t magic = kStateMagic;
    uint32_t version = kStateVersion;
    uint32_t selectedSlot = 0u;
    uint32_t interpolation = 1u;
    double outputGainDb = -6.0;
    double velocitySensitivity = 1.0;
    clap_id outputConfigId = kStereoOutputConfigId;
    float auxPress = 0.42f;
    float auxSnap = 0.18f;
    float auxRecovery = 0.34f;
    float auxSaturation = 0.20f;
    float auxBite = 0.08f;
    float auxClip = 0.0f;
    float auxTilt = 0.0f;
    float auxReturnDb = -9.0f;
    uint8_t embedSamples = 1u;
    uint8_t auxEnabled = 0u;
    uint8_t auxLinkMode = 0u;
    uint8_t auxFieldSafe = 0u;
    // State v10 deliberately occupies the four alignment bytes that preceded
    // slots in v9, keeping the fixed metadata and embedded-audio offset stable.
    uint32_t transientPreRollMicroseconds = 0u;
    std::array<SavedSlot, s3g::breakbeat::kMaximumSampleSlots> slots {};
};

struct SavedMutationState {
    uint32_t magic = kMutationStateMagic;
    uint32_t version = kMutationStateVersion;
    uint32_t nextSeed = 1u;
    float depth = 0.25f;
    uint8_t targets = s3g::breakbeat::kDefaultMutationTargets;
    std::array<uint8_t,
        s3g::breakbeat::kMaximumSampleSlots> selectedVariations {};
    std::array<uint8_t, 3u> reserved {};
    std::array<std::array<MutationVariation,
        s3g::breakbeat::kMutationVariationCount>,
        s3g::breakbeat::kMaximumSampleSlots> variations {};
};

struct SavedPlaybackSlot {
    float sourceTempoBpm = 120.0f;
    float loopCrossfade = 0.02f;
    float glideSeconds = 0.0f;
    uint8_t triggerMode = static_cast<uint8_t>(
        s3g::breakbeat::TriggerMode::Auto);
    uint8_t retriggerMode = static_cast<uint8_t>(
        s3g::breakbeat::RetriggerMode::Restart);
    uint8_t voiceMode = static_cast<uint8_t>(
        s3g::breakbeat::VoiceMode::Poly);
    uint8_t pitchMode = static_cast<uint8_t>(
        s3g::breakbeat::PitchMode::Rate);
    uint8_t syncMode = static_cast<uint8_t>(
        s3g::breakbeat::SyncMode::Free);
    std::array<uint8_t, 3u> reserved {};
};

struct SavedPlaybackState {
    uint32_t magic = 0x59414c50u; // "PLAY"
    uint32_t version = 1u;
    std::array<SavedPlaybackSlot,
        s3g::breakbeat::kMaximumSampleSlots> slots {};
};

#if defined(__APPLE__)
enum class LoadRequestKind : uint8_t {
    File = 0u,
    MutationPrint,
    MutationExport,
};

struct LoadRequest {
    LoadRequestKind kind = LoadRequestKind::File;
    uint32_t slotIndex = 0u;
    uint32_t sourceSlot = 0u;
    uint64_t generation = 0u;
    std::string path;
    std::shared_ptr<const BankSnapshot> printBank;
    double renderSampleRate = 48000.0;
    uint32_t renderChannelCount = 2u;
    uint32_t mutationSeed = 0u;
    uint8_t mutationUses = 0u;
};

struct LoadResult {
    LoadRequestKind kind = LoadRequestKind::File;
    uint32_t slotIndex = 0u;
    uint32_t sourceSlot = 0u;
    uint64_t generation = 0u;
    std::string path;
    std::shared_ptr<const SampleAsset> asset;
    std::shared_ptr<const SampleAnalysis> analysis;
    std::vector<uint32_t> sliceStarts;
    std::string error;
};
#endif

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    const clap_host_note_name_t* hostNoteNames = nullptr;
    const clap_host_state_t* hostState = nullptr;
    double sampleRate = 48000.0;
    uint32_t maximumFrames = 0u;
    s3g::breakbeat::SlicerEngine engine;
    const BankSnapshot* audioBank = nullptr;
    const MixerSnapshot* audioMixer = nullptr;
    std::atomic<const BankSnapshot*> publishedBank { nullptr };
    std::atomic<const MixerSnapshot*> publishedMixer { nullptr };
    std::shared_ptr<const BankSnapshot> controlBank;
    std::shared_ptr<const MixerSnapshot> controlMixer;
    // A first-generation RCU retirement store. Published banks are reclaimed
    // only after processing has stopped or the plug-in is destroyed, so the
    // audio callback never owns, releases, or destroys sample memory.
    std::vector<std::shared_ptr<const BankSnapshot>> retainedBanks;
    std::vector<std::shared_ptr<const MixerSnapshot>> retainedMixers;
    std::array<std::string, s3g::breakbeat::kMaximumSampleSlots> samplePaths {};
    std::array<std::shared_ptr<const SampleAnalysis>,
        s3g::breakbeat::kMaximumSampleSlots> analyses {};
    std::atomic<double> outputGainDb { -6.0 };
    std::atomic<double> velocitySensitivity { 1.0 };
    std::atomic<uint32_t> pendingAuditionNote { 0u }; // key + 1
    std::atomic<uint32_t> pendingAuditionChannel { 0u };
    std::atomic<uint32_t> pendingPlaythroughSlot { 0u }; // slot + 1
    std::atomic<uint32_t> playthroughSlotTelemetry { 0u }; // slot + 1
    std::atomic<bool> pendingKillAll { false };
    std::atomic<uint64_t> auditionCounter { 1u };
    bool audioPlaythroughActive = false;
    uint8_t audioPlaythroughSlot = 0u;
    uint16_t audioPlaythroughNextSlice = 0u;
    uint64_t audioPlaythroughFramesUntilBoundary = 0u;
    std::array<std::atomic<float>,
        s3g::breakbeat::kMidiNoteCount> midiKeyActivity {};
    std::array<std::atomic<uint8_t>,
        s3g::breakbeat::kMidiNoteCount> midiKeyChannels {};
    std::atomic<float> outputPeak { 0.0f };
    std::array<std::atomic<float>,
        s3g::breakbeat::kMaximumSampleSlots> slotPlayheads {};
    std::array<std::atomic<float>,
        s3g::breakbeat::kMaximumVoices> voicePlayheads {};
    std::array<std::atomic<uint8_t>,
        s3g::breakbeat::kMaximumVoices> voicePlayheadSlots {};
    std::array<std::atomic<uint8_t>,
        s3g::breakbeat::kMaximumVoices> voicePlayheadKeys {};
    std::array<std::atomic<float>,
        s3g::breakbeat::kMaximumSampleSlots> slotPeaks {};
    std::atomic<float> auxActivity { 0.0f };
    std::atomic<float> auxGainReductionDb { 0.0f };
    std::array<RenderEvent, kMaximumBlockEvents> blockEvents {};
    std::array<std::vector<float>,
        s3g::breakbeat::kMaximumAudioChannels> scratchChannels {};
    clap_id outputConfigId = kStereoOutputConfigId;
    uint32_t outputChannelCount = 2u;
    bool embedSamplesInState = true;
    uint32_t transientPreRollMicroseconds = 0u;
    uint32_t minimumTransientSliceMilliseconds
        = kDefaultMinimumTransientSliceMilliseconds;
    uint32_t selectedSlot = 0u;
    std::array<std::array<MutationVariation,
        s3g::breakbeat::kMutationVariationCount>,
        s3g::breakbeat::kMaximumSampleSlots> variations {};
    std::array<uint8_t,
        s3g::breakbeat::kMaximumSampleSlots> selectedVariations {};
    uint32_t mutationSeed = 1u;
    float mutationDepth = 0.25f;
    uint8_t mutationTargets = s3g::breakbeat::kDefaultMutationTargets;
    uint8_t structuralMutationUses
        = s3g::breakbeat::kDefaultStructuralMutationUses;
    std::array<bool,
        s3g::breakbeat::kMaximumSampleSlots> pendingMutationSlots {};
    std::string status { "LOAD A BREAK OR ONE-SHOT" };
    bool active = false;
#if defined(__APPLE__)
    std::mutex loaderMutex;
    std::condition_variable loaderCondition;
    std::deque<LoadRequest> loadRequests;
    std::deque<LoadResult> loadResults;
    std::thread loaderThread;
    std::array<uint64_t,
        s3g::breakbeat::kMaximumSampleSlots> loadGenerations {};
    uint64_t exportGeneration = 0u;
    bool loaderStopping = false;
    void* guiView = nullptr;
    s3g::clap_gui::ResponsiveViewport guiViewport {};
#endif
};

#if defined(__APPLE__)
void registerBundledFonts()
{
    NSBundle* bundle = [NSBundle bundleForClass:
        NSClassFromString(@"S3GBreakbeatSlicerView")];
    constexpr std::array<const char*, 3u> names {{
        "IBMPlexMono-Regular", "IBMPlexMono-Medium",
        "IBMPlexMono-SemiBold",
    }};
    for (const char* name : names) {
        NSURL* url = [bundle URLForResource:[NSString stringWithUTF8String:name]
            withExtension:@"ttf" subdirectory:@"Fonts"];
        if (!url) continue;
        CFErrorRef error = nullptr;
        (void)CTFontManagerRegisterFontsForURL((__bridge CFURLRef)url,
            kCTFontManagerScopeProcess, &error);
        if (error) CFRelease(error);
    }
}
#endif

Plugin* self(const clap_plugin_t* plugin)
{
    return static_cast<Plugin*>(plugin->plugin_data);
}

const OutputConfigDef* outputConfig(clap_id id) noexcept
{
    for (const auto& config : kOutputConfigs) {
        if (config.id == id) return &config;
    }
    return nullptr;
}

const ParamDef* paramDef(clap_id id)
{
    for (const auto& def : kParamDefs) {
        if (def.id == id) return &def;
    }
    return nullptr;
}

double clampParam(const ParamDef& def, double value)
{
    value = std::isfinite(value) ? value : def.defaultValue;
    value = std::clamp(value, def.minimum, def.maximum);
    return def.stepped ? std::round(value) : value;
}

double paramValue(const Plugin& instance, clap_id id)
{
    switch (id) {
    case kOutputGainParamId:
        return instance.outputGainDb.load(std::memory_order_acquire);
    case kVelocityParamId:
        return instance.velocitySensitivity.load(std::memory_order_acquire);
    default:
        return 0.0;
    }
}

void setParam(Plugin& instance, clap_id id, double value)
{
    const auto* def = paramDef(id);
    if (!def) return;
    value = clampParam(*def, value);
    switch (id) {
    case kOutputGainParamId:
        instance.outputGainDb.store(value, std::memory_order_release);
        break;
    case kVelocityParamId:
        instance.velocitySensitivity.store(value, std::memory_order_release);
        break;
    default:
        break;
    }
}

void requestProcess(Plugin& instance)
{
    if (instance.host && instance.host->request_process)
        instance.host->request_process(instance.host);
}

void notifyNoteNamesChanged(Plugin& instance)
{
    if (instance.host && instance.hostNoteNames
        && instance.hostNoteNames->changed)
        instance.hostNoteNames->changed(instance.host);
}

void markStateDirty(Plugin& instance)
{
    if (instance.host && instance.hostState && instance.hostState->mark_dirty)
        instance.hostState->mark_dirty(instance.host);
}

bool publishBank(Plugin& instance,
    std::shared_ptr<const BankSnapshot> bank, bool dirty = true)
{
    if (!bank || !bank->valid()) return false;
    auto mixer = std::make_shared<MixerSnapshot>(
        s3g::breakbeat::mixerSnapshotFromBank(*bank));
    if (!mixer->valid()) return false;
    instance.retainedBanks.push_back(bank);
    instance.retainedMixers.push_back(mixer);
    instance.controlBank = std::move(bank);
    instance.controlMixer = std::move(mixer);
    instance.publishedBank.store(instance.controlBank.get(),
        std::memory_order_release);
    instance.publishedMixer.store(instance.controlMixer.get(),
        std::memory_order_release);
    notifyNoteNamesChanged(instance);
    requestProcess(instance);
    if (dirty) markStateDirty(instance);
    return true;
}

bool publishMixerBank(Plugin& instance,
    std::shared_ptr<const BankSnapshot> bank, bool dirty = true)
{
    if (!bank || !bank->valid()) return false;
    auto mixer = std::make_shared<MixerSnapshot>(
        s3g::breakbeat::mixerSnapshotFromBank(*bank));
    if (!mixer->valid()) return false;
    // The document bank retains mixer values for state/UI, but only the small
    // runtime mixer snapshot is offered to the audio thread.
    instance.controlBank = std::move(bank);
    instance.retainedMixers.push_back(mixer);
    instance.controlMixer = std::move(mixer);
    instance.publishedMixer.store(instance.controlMixer.get(),
        std::memory_order_release);
    requestProcess(instance);
    if (dirty) markStateDirty(instance);
    return true;
}

std::shared_ptr<BankSnapshot> editableBank(const Plugin& instance)
{
    if (instance.controlBank)
        return std::make_shared<BankSnapshot>(*instance.controlBank);
    auto bank = std::make_shared<BankSnapshot>();
    s3g::breakbeat::initializeEmptyBank(*bank);
    return bank;
}

bool replaceSlotSlices(Plugin& instance, uint32_t slotIndex,
    const std::vector<Slice>& slices)
{
    if (slotIndex >= s3g::breakbeat::kMaximumSampleSlots || slices.empty()
        || slices.size() > s3g::breakbeat::kMaximumSlicesPerSlot)
        return false;
    auto bank = editableBank(instance);
    auto& slot = bank->slots[slotIndex];
    if (!slot.asset || slices.size()
        > s3g::breakbeat::maximumSlicesForStartNote(slot.rootNote))
        return false;
    slot.slices = {};
    slot.sliceCount = static_cast<uint16_t>(slices.size());
    std::copy(slices.begin(), slices.end(), slot.slices.begin());
    slot.mappedSliceCount = 0u;
    return publishBank(instance, std::move(bank));
}

bool setSlotRootNote(Plugin& instance, uint32_t slotIndex,
    uint8_t rootNote)
{
    if (slotIndex >= s3g::breakbeat::kMaximumSampleSlots) return false;
    auto bank = editableBank(instance);
    auto& slot = bank->slots[slotIndex];
    if (!slot.asset || static_cast<std::size_t>(rootNote) + slot.sliceCount
        > s3g::breakbeat::kMidiNoteCount) return false;
    slot.rootNote = rootNote;
    slot.mappedSliceCount = 0u;
    return publishBank(instance, std::move(bank));
}

bool setSlotMidiChannel(Plugin& instance, uint32_t slotIndex,
    uint8_t midiChannel)
{
    if (slotIndex >= s3g::breakbeat::kMaximumSampleSlots
        || midiChannel > 16u) return false;
    auto bank = editableBank(instance);
    bank->slots[slotIndex].midiChannel = midiChannel;
    return publishBank(instance, std::move(bank));
}

bool automapSlot(Plugin& instance, uint32_t slotIndex)
{
    if (slotIndex >= s3g::breakbeat::kMaximumSampleSlots) return false;
    auto bank = editableBank(instance);
    auto& slot = bank->slots[slotIndex];
    if (!slot.asset || slot.sliceCount == 0u) return false;
    if (!s3g::breakbeat::autoMapSlotConsecutively(*bank,
            static_cast<uint8_t>(slotIndex), true)) return false;
    return publishBank(instance, std::move(bank));
}

bool slotHasCompleteMap(const s3g::breakbeat::SampleSlot& slot) noexcept
{
    return slot.asset && slot.sliceCount > 0u
        && slot.mappedSliceCount == slot.sliceCount
        && slot.mappedRootNote == slot.rootNote;
}

void resetSlotVariations(Plugin& instance, uint32_t slotIndex,
    const s3g::breakbeat::SampleSlot* source = nullptr)
{
    if (slotIndex >= instance.variations.size()) return;
    instance.variations[slotIndex] = {};
    instance.selectedVariations[slotIndex] = 0u;
    if (source && source->asset && source->sliceCount > 0u)
        instance.variations[slotIndex][0u]
            = s3g::breakbeat::captureMutationVariation(*source);
}

uint32_t emptyMutationSlotCount(const Plugin& instance,
    uint32_t sourceSlot) noexcept
{
    if (!instance.controlBank) return 0u;
    uint32_t count = 0u;
    for (uint32_t index = 0u;
         index < s3g::breakbeat::kMaximumSampleSlots; ++index) {
        if (index != sourceSlot && !instance.controlBank->slots[index].asset
            && !instance.pendingMutationSlots[index]) ++count;
    }
    return count;
}

void configureMutationAux(BankSnapshot& bank,
    s3g::breakbeat::SampleSlot& source, uint32_t seed, bool enabled)
{
    if (!enabled) {
        bank.auxEnabled = false;
        source.mixerAuxSend = 0.0f;
        return;
    }
    uint32_t random = seed ^ 0xa511e9b3u;
    const auto unit = [&random] {
        return s3g::breakbeat::mutationUnit(random);
    };
    bank.auxEnabled = true;
    bank.auxFieldSafe = source.asset && source.asset->channelCount > 2u;
    bank.auxLinkMode = s3g::BreakBusLinkMode::All;
    source.mixerAuxSend = 0.38f + unit() * 0.42f;
    bank.auxPress = 0.34f + unit() * 0.46f;
    bank.auxSnap = -0.12f + unit() * 0.58f;
    bank.auxRecovery = 0.22f + unit() * 0.56f;
    bank.auxSaturation = 0.15f + unit() * 0.46f;
    bank.auxBite = 0.04f + unit() * 0.36f;
    bank.auxClip = unit() * 0.22f;
    bank.auxTilt = -0.22f + unit() * 0.44f;
    bank.auxReturnDb = -10.0f + unit() * 5.0f;
}

std::shared_ptr<const BankSnapshot> structuralMutationBank(
    const Plugin& instance, uint32_t sourceSlot, uint32_t destinationSlot,
    uint32_t seed)
{
    if (!instance.controlBank
        || sourceSlot >= s3g::breakbeat::kMaximumSampleSlots
        || destinationSlot >= s3g::breakbeat::kMaximumSampleSlots)
        return {};
    auto bank = std::make_shared<BankSnapshot>(*instance.controlBank);
    auto& source = bank->slots[sourceSlot];
    if (!source.asset || source.sliceCount == 0u) return {};
    auto variation = s3g::breakbeat::captureMutationVariation(source);
    const std::size_t maximum = std::min(
        s3g::breakbeat::maximumSlicesForStartNote(source.rootNote),
        s3g::breakbeat::maximumSlicesForStartNote(
            bank->slots[destinationSlot].rootNote));
    constexpr float depth = 0.72f;
    if (!s3g::breakbeat::structurallyMutateVariation(variation,
            *source.asset, seed, depth, maximum,
            instance.structuralMutationUses)
        || !s3g::breakbeat::applyMutationVariation(source, variation))
        return {};
    source.mappedRootNote = source.rootNote;
    source.mappedSliceCount = source.sliceCount;

    // Mutation effects are printed per arranged slice below. Keep the
    // structural snapshot dry at the AUX stage so one recipe cannot leak
    // across the entire generated break.
    configureMutationAux(*bank, source, seed, false);
    return bank->valid() ? bank : std::shared_ptr<const BankSnapshot> {};
}

int mappedNoteFor(const BankSnapshot& bank, uint32_t slotIndex,
    uint32_t sliceIndex) noexcept
{
    if (slotIndex >= bank.slots.size()) return -1;
    const auto& slot = bank.slots[slotIndex];
    if (sliceIndex >= slot.mappedSliceCount
        || sliceIndex >= slot.sliceCount) return -1;
    return static_cast<int>(slot.mappedRootNote + sliceIndex);
}

void initializeBank(Plugin& instance)
{
    auto bank = std::make_shared<BankSnapshot>();
    s3g::breakbeat::initializeEmptyBank(*bank);
    instance.retainedBanks.reserve(64u);
    instance.retainedMixers.reserve(64u);
    publishBank(instance, std::move(bank), false);
}

#if defined(__APPLE__)
bool decodeSampleFile(const std::string& path,
    std::shared_ptr<const SampleAsset>& assetOut,
    std::shared_ptr<const SampleAnalysis>& analysisOut,
    std::string& error)
{
    @autoreleasepool {
        NSString* nsPath = [NSString stringWithUTF8String:path.c_str()];
        if (!nsPath) {
            error = "INVALID FILE PATH";
            return false;
        }
        NSError* nsError = nil;
        AVAudioFile* file = [[AVAudioFile alloc]
            initForReading:[NSURL fileURLWithPath:nsPath] error:&nsError];
        if (!file) {
            error = "COULD NOT OPEN SAMPLE";
            return false;
        }
        AVAudioFormat* format = [file processingFormat];
        const AVAudioChannelCount channels = [format channelCount];
        const AVAudioFramePosition fileFrames = [file length];
        if (channels < 1u
            || channels > s3g::breakbeat::kMaximumAudioChannels
            || fileFrames < 1
            || static_cast<uint64_t>(fileFrames)
                > std::numeric_limits<uint32_t>::max()) {
            error = "USE A 1-16 CHANNEL SAMPLE UNDER 2^32 FRAMES";
            return false;
        }
        const auto frames = static_cast<AVAudioFrameCount>(fileFrames);
        AVAudioPCMBuffer* buffer = [[AVAudioPCMBuffer alloc]
            initWithPCMFormat:format frameCapacity:frames];
        if (!buffer || ![file readIntoBuffer:buffer error:&nsError]
            || [buffer frameLength] == 0u || ![buffer floatChannelData]) {
            error = "SAMPLE DECODE FAILED";
            return false;
        }
        const uint32_t decodedFrames = [buffer frameLength];
        auto asset = std::make_shared<SampleAsset>();
        asset->sampleRate = [format sampleRate];
        asset->channelCount = static_cast<uint8_t>(channels);
        for (AVAudioChannelCount channel = 0u; channel < channels;
             ++channel) {
            asset->channels[channel].assign(
                [buffer floatChannelData][channel],
                [buffer floatChannelData][channel] + decodedFrames);
        }
        if (!asset->valid()) {
            error = "DECODED SAMPLE DATA IS INVALID";
            return false;
        }
        auto analysis = std::make_shared<SampleAnalysis>(
            s3g::breakbeat::analyzeSample(*asset));
        if (!analysis->validFor(*asset)) {
            error = "WAVEFORM ANALYSIS FAILED";
            return false;
        }
        assetOut = std::move(asset);
        analysisOut = std::move(analysis);
        error.clear();
        return true;
    }
}

bool renderBuiltBreak(const LoadRequest& request,
    std::shared_ptr<const SampleAsset>& assetOut,
    std::shared_ptr<const SampleAnalysis>& analysisOut,
    std::vector<uint32_t>& sliceStartsOut,
    std::string& error)
{
    if (!request.printBank
        || request.sourceSlot >= request.printBank->slots.size()) {
        error = "RENDER SOURCE IS NOT AVAILABLE";
        return false;
    }
    const auto& source = request.printBank->slots[request.sourceSlot];
    if (!source.asset || source.sliceCount == 0u
        || !(request.renderSampleRate > 0.0)
        || !std::isfinite(request.renderSampleRate)) {
        error = "LOAD AND SLICE A BREAK BEFORE RENDERING";
        return false;
    }
    const uint32_t outputChannels = source.asset->channelCount <= 2u
        ? 2u : source.asset->channelCount;
    if (outputChannels > request.renderChannelCount) {
        error = "RENDER SOURCE IS WIDER THAN THIS PLUGIN";
        return false;
    }

    BankSnapshot renderBank = *request.printBank;
    for (std::size_t index = 0u; index < renderBank.slots.size(); ++index) {
        if (index == request.sourceSlot) continue;
        const uint8_t root = renderBank.slots[index].rootNote;
        const uint8_t midi = renderBank.slots[index].midiChannel;
        renderBank.slots[index] = {};
        renderBank.slots[index].rootNote = root;
        renderBank.slots[index].mappedRootNote = root;
        renderBank.slots[index].midiChannel = midi;
    }
    auto& printable = renderBank.slots[request.sourceSlot];
    printable.muted = false;
    printable.solo = false;
    printable.mappedRootNote = printable.rootNote;
    printable.mappedSliceCount = printable.sliceCount;
    for (std::size_t index = 0u; index < printable.sliceCount; ++index) {
        printable.slices[index].launchMode
            = s3g::breakbeat::LaunchMode::OneShot;
        printable.slices[index].loopStartFrame = 0u;
        printable.slices[index].loopEndFrame = 0u;
    }
    if (!renderBank.valid()) {
        error = "CURRENT BREAK CANNOT BE RENDERED";
        return false;
    }

    std::vector<RenderEvent> events;
    std::vector<uint32_t> sliceDurations;
    events.reserve(printable.sliceCount);
    sliceDurations.reserve(printable.sliceCount);
    sliceStartsOut.clear();
    sliceStartsOut.reserve(printable.sliceCount);
    uint64_t mainFrames = 0u;
    for (std::size_t index = 0u; index < printable.sliceCount; ++index) {
        const auto& slice = printable.slices[index];
        const double semitones = static_cast<double>(
            slice.transposeSemitones)
            + static_cast<double>(slice.fineTuneCents) / 100.0;
        const double increment = printable.asset->sampleRate
            / request.renderSampleRate * std::pow(2.0, semitones / 12.0);
        const double sourceFrames = static_cast<double>(
            slice.endFrame - slice.startFrame);
        const uint64_t duration = static_cast<uint64_t>(std::max(1.0,
            std::ceil(sourceFrames / std::max(increment, 1.0e-12))));
        if (duration > std::numeric_limits<uint32_t>::max()
            || mainFrames
                > std::numeric_limits<uint32_t>::max() - duration) {
            error = "RENDER IS TOO LONG";
            return false;
        }
        sliceStartsOut.push_back(static_cast<uint32_t>(mainFrames));
        sliceDurations.push_back(static_cast<uint32_t>(duration));
        events.push_back({ static_cast<uint32_t>(mainFrames),
            EventKind::NoteOn, static_cast<uint64_t>(index + 1u),
            static_cast<uint8_t>(printable.rootNote + index), 0u, 1.0f,
            printable.midiChannel == 0u ? static_cast<uint8_t>(1u)
                                        : printable.midiChannel });
        mainFrames += duration;
    }
    const uint64_t tailFrames = static_cast<uint64_t>(std::lround(
        request.renderSampleRate * 2.0));
    const uint64_t totalFrames64 = mainFrames + tailFrames;
    const uint64_t totalBytes = totalFrames64 * outputChannels
        * sizeof(float);
    if (totalFrames64 == 0u
        || totalFrames64 > std::numeric_limits<uint32_t>::max()
        || totalBytes > kMaximumEmbeddedAudioBytes) {
        error = "RENDER EXCEEDS THE 1 GB AUDIO LIMIT";
        return false;
    }
    const uint32_t totalFrames = static_cast<uint32_t>(totalFrames64);
    auto rendered = std::make_shared<SampleAsset>();
    rendered->sampleRate = request.renderSampleRate;
    rendered->channelCount = static_cast<uint8_t>(outputChannels);
    std::array<float*, s3g::breakbeat::kMaximumAudioChannels> outputs {};
    for (uint32_t channel = 0u; channel < outputChannels; ++channel) {
        rendered->channels[channel].assign(totalFrames, 0.0f);
        outputs[channel] = rendered->channels[channel].data();
    }
    auto renderer = std::unique_ptr<s3g::breakbeat::SlicerEngine>(
        new (std::nothrow) s3g::breakbeat::SlicerEngine());
    if (!renderer
        || !renderer->prepare(request.renderSampleRate, outputChannels)
        || !renderer->setBank(&renderBank)) {
        error = "COULD NOT PREPARE THE RENDER ENGINE";
        return false;
    }
    const bool perSliceEffects = request.kind
            == LoadRequestKind::MutationPrint
        && (request.mutationUses
            & (s3g::breakbeat::StructuralMixerFx
                | s3g::breakbeat::StructuralAuxBus)) != 0u;
    if (!perSliceEffects) {
        renderer->render(events.data(), events.size(), outputs.data(),
            outputChannels, totalFrames);
    } else {
        s3g::breakbeat::MixerSnapshot activeMixer;
        for (std::size_t index = 0u; index < printable.sliceCount; ++index) {
            BankSnapshot effectBank = renderBank;
            auto& effectSource = effectBank.slots[request.sourceSlot];
            uint32_t effectSeed = request.mutationSeed
                ^ (0x9e3779b9u * static_cast<uint32_t>(index + 1u));
            (void)s3g::breakbeat::nextMutationRandom(effectSeed);
            if ((request.mutationUses
                    & s3g::breakbeat::StructuralMixerFx) != 0u
                && !s3g::breakbeat::mutateMixerEffects(effectSource,
                    effectSeed, 0.72f)) {
                error = "COULD NOT BUILD PER-SLICE MIXER EFFECTS";
                return false;
            }
            configureMutationAux(effectBank, effectSource, effectSeed,
                (request.mutationUses
                    & s3g::breakbeat::StructuralAuxBus) != 0u);
            if (!effectBank.valid()) {
                error = "PER-SLICE EFFECT RECIPE IS INVALID";
                return false;
            }
            activeMixer = s3g::breakbeat::mixerSnapshotFromBank(effectBank);
            renderer->setPreparedMixer(&activeMixer);
            RenderEvent event { 0u, EventKind::NoteOn,
                static_cast<uint64_t>(index + 1u),
                static_cast<uint8_t>(printable.rootNote + index), 0u, 1.0f,
                printable.midiChannel == 0u ? static_cast<uint8_t>(1u)
                                            : printable.midiChannel };
            std::array<float*, s3g::breakbeat::kMaximumAudioChannels>
                segmentOutputs {};
            for (uint32_t channel = 0u; channel < outputChannels; ++channel)
                segmentOutputs[channel] = outputs[channel]
                    + sliceStartsOut[index];
            renderer->render(&event, 1u, segmentOutputs.data(),
                outputChannels, sliceDurations[index]);
        }
        std::array<float*, s3g::breakbeat::kMaximumAudioChannels>
            tailOutputs {};
        for (uint32_t channel = 0u; channel < outputChannels; ++channel)
            tailOutputs[channel] = outputs[channel]
                + static_cast<uint32_t>(mainFrames);
        renderer->render(nullptr, 0u, tailOutputs.data(), outputChannels,
            static_cast<uint32_t>(tailFrames));
    }

    uint32_t trimmedFrames = static_cast<uint32_t>(mainFrames);
    for (uint32_t frame = totalFrames; frame > mainFrames; --frame) {
        bool audible = false;
        for (uint32_t channel = 0u; channel < outputChannels; ++channel) {
            if (std::abs(rendered->channels[channel][frame - 1u])
                > 1.0e-6f) {
                audible = true;
                break;
            }
        }
        if (audible) {
            trimmedFrames = std::min<uint32_t>(totalFrames,
                frame + static_cast<uint32_t>(std::lround(
                    request.renderSampleRate * 0.010)));
            break;
        }
    }
    trimmedFrames = std::max<uint32_t>(trimmedFrames, 1u);
    for (uint32_t channel = 0u; channel < outputChannels; ++channel)
        rendered->channels[channel].resize(trimmedFrames);
    if (!rendered->valid()) {
        error = "RENDERED AUDIO IS INVALID";
        return false;
    }
    if (request.kind == LoadRequestKind::MutationPrint) {
        if (!s3g::breakbeat::reduceSlicePeaksToCeiling(*rendered,
                sliceStartsOut.data(), sliceStartsOut.size(), 1.0f)) {
            error = "COULD NOT APPLY THE MUTATION PEAK CEILING";
            return false;
        }
        auto analysis = std::make_shared<SampleAnalysis>(
            s3g::breakbeat::analyzeSample(*rendered));
        if (!analysis->validFor(*rendered)) {
            error = "RENDERED WAVEFORM ANALYSIS FAILED";
            return false;
        }
        analysisOut = std::move(analysis);
    } else {
        analysisOut.reset();
    }
    assetOut = std::move(rendered);
    error.clear();
    return true;
}

bool writeRenderedWaveFile(const std::string& path,
    const SampleAsset& asset, std::string& error)
{
    if (path.empty() || !asset.valid()) {
        error = "EXPORT TARGET OR RENDERED AUDIO IS INVALID";
        return false;
    }
    std::array<const float*, s3g::breakbeat::kMaximumAudioChannels>
        channels {};
    for (uint32_t channel = 0u; channel < asset.channelCount; ++channel)
        channels[channel] = asset.channels[channel].data();
    return s3g::audio_file::writePlanarFloatWaveAtomically(path,
        asset.sampleRate, asset.channelCount, asset.frameCount(),
        channels.data(), error);
}

bool installDecodedSample(Plugin& instance, uint32_t slotIndex,
    const std::string& path, std::shared_ptr<const SampleAsset> asset,
    std::shared_ptr<const SampleAnalysis> analysis)
{
    if (slotIndex >= s3g::breakbeat::kMaximumSampleSlots || !asset
        || !analysis || !asset->valid() || !analysis->validFor(*asset)) {
        instance.status = "INVALID DECODED SAMPLE";
        return false;
    }
    const uint32_t sourceChannelCount = asset->channelCount;
    if (sourceChannelCount > instance.outputChannelCount) {
        instance.status = "USE S3G SAMPLE SLICER FOR THIS FILE";
        return false;
    }
    auto bank = editableBank(instance);
    const auto previous = bank->slots[slotIndex];
    const uint8_t root = previous.rootNote;
    const uint8_t channel = previous.midiChannel;
    s3g::breakbeat::clearMappingsForSlot(*bank,
        static_cast<uint8_t>(slotIndex));
    auto& slot = bank->slots[slotIndex];
    slot = {};
    slot.asset = std::move(asset);
    slot.rootNote = root;
    slot.mappedRootNote = root;
    slot.midiChannel = channel;
    slot.envelope = previous.envelope;
    slot.mixerGain = previous.mixerGain;
    slot.mixerPan = previous.mixerPan;
    slot.mixerLowEqDb = previous.mixerLowEqDb;
    slot.mixerMidEqDb = previous.mixerMidEqDb;
    slot.mixerHighEqDb = previous.mixerHighEqDb;
    slot.mixerMidFrequencyHz = previous.mixerMidFrequencyHz;
    slot.mixerAuxSend = previous.mixerAuxSend;
    slot.inserts = previous.inserts;
    slot.triggerMode = previous.triggerMode;
    slot.retriggerMode = previous.retriggerMode;
    slot.voiceMode = previous.voiceMode;
    slot.pitchMode = previous.pitchMode;
    slot.syncMode = previous.syncMode;
    slot.sourceTempoBpm = previous.sourceTempoBpm;
    slot.loopCrossfade = previous.loopCrossfade;
    slot.glideSeconds = previous.glideSeconds;
    slot.muted = previous.muted;
    slot.solo = previous.solo;
    slot.sliceCount = 1u;
    slot.slices[0u].startFrame = 0u;
    slot.slices[0u].endFrame = slot.asset->frameCount();
    if (!publishBank(instance, std::move(bank), false)) {
        instance.status = "COULD NOT PUBLISH SAMPLE BANK";
        return false;
    }
    instance.samplePaths[slotIndex] = path;
    instance.analyses[slotIndex] = std::move(analysis);
    resetSlotVariations(instance, slotIndex,
        &instance.controlBank->slots[slotIndex]);
    markStateDirty(instance);
    if (sourceChannelCount > 2u) {
        instance.status = "SAMPLE READY - AUTO MAP TO PLAY";
    } else {
        instance.status = "SAMPLE READY - AUTO MAP TO PLAY";
    }
    return true;
}

bool installMutationPrint(Plugin& instance, const LoadResult& result)
{
    const uint32_t slotIndex = result.slotIndex;
    if (slotIndex >= s3g::breakbeat::kMaximumSampleSlots || !result.asset
        || !result.analysis || !result.asset->valid()
        || !result.analysis->validFor(*result.asset)) return false;
    auto bank = editableBank(instance);
    auto& destination = bank->slots[slotIndex];
    if (destination.asset) {
        instance.status = "MUTATION TARGET IS NO LONGER EMPTY";
        return false;
    }
    const uint8_t root = destination.rootNote;
    const uint8_t midiChannel = destination.midiChannel;
    destination = {};
    destination.asset = result.asset;
    destination.rootNote = root;
    destination.mappedRootNote = root;
    destination.midiChannel = midiChannel;
    const std::size_t maximum = std::min(destination.slices.size(),
        s3g::breakbeat::maximumSlicesForStartNote(root));
    std::vector<uint32_t> starts;
    starts.reserve(std::min(maximum, result.sliceStarts.size()));
    for (const uint32_t start : result.sliceStarts) {
        if (start >= destination.asset->frameCount()
            || (!starts.empty() && start <= starts.back())) continue;
        starts.push_back(start);
        if (starts.size() >= maximum) break;
    }
    if (starts.empty() || starts.front() != 0u) starts.insert(
        starts.begin(), 0u);
    destination.sliceCount = static_cast<uint16_t>(starts.size());
    destination.mappedSliceCount = destination.sliceCount;
    for (std::size_t index = 0u; index < starts.size(); ++index) {
        destination.slices[index].startFrame = starts[index];
        destination.slices[index].endFrame = index + 1u < starts.size()
            ? starts[index + 1u] : destination.asset->frameCount();
    }
    if (!publishBank(instance, std::move(bank), false)) return false;
    instance.samplePaths[slotIndex] = result.path;
    instance.analyses[slotIndex] = result.analysis;
    resetSlotVariations(instance, slotIndex,
        &instance.controlBank->slots[slotIndex]);
    markStateDirty(instance);
    instance.status = "MUTATION COMPLETE - NEW BREAK IS MAPPED";
    return true;
}

bool startSampleLoader(Plugin& instance)
{
    try {
        instance.loaderThread = std::thread([&instance] {
            for (;;) {
                LoadRequest request;
                {
                    std::unique_lock<std::mutex> lock(instance.loaderMutex);
                    instance.loaderCondition.wait(lock, [&instance] {
                        return instance.loaderStopping
                            || !instance.loadRequests.empty();
                    });
                    if (instance.loaderStopping) return;
                    request = std::move(instance.loadRequests.front());
                    instance.loadRequests.pop_front();
                }
                LoadResult result;
                result.kind = request.kind;
                result.slotIndex = request.slotIndex;
                result.sourceSlot = request.sourceSlot;
                result.generation = request.generation;
                result.path = std::move(request.path);
                try {
                    if (result.kind == LoadRequestKind::File) {
                        decodeSampleFile(result.path, result.asset,
                            result.analysis, result.error);
                    } else if (renderBuiltBreak(request, result.asset,
                            result.analysis, result.sliceStarts,
                            result.error)
                        && result.kind == LoadRequestKind::MutationExport) {
                        (void)writeRenderedWaveFile(result.path,
                            *result.asset, result.error);
                    }
                } catch (...) {
                    result.asset.reset();
                    result.analysis.reset();
                    result.error = result.kind
                            == LoadRequestKind::MutationExport
                        ? "EXPORT RAN OUT OF RESOURCES"
                        : result.kind == LoadRequestKind::MutationPrint
                            ? "MUTATION RAN OUT OF RESOURCES"
                            : "SAMPLE DECODE RAN OUT OF RESOURCES";
                }
                {
                    std::lock_guard<std::mutex> lock(instance.loaderMutex);
                    if (instance.loaderStopping) return;
                    instance.loadResults.push_back(std::move(result));
                }
                if (instance.host && instance.host->request_callback)
                    instance.host->request_callback(instance.host);
            }
        });
    } catch (...) {
        return false;
    }
    return true;
}

void stopSampleLoader(Plugin& instance)
{
    {
        std::lock_guard<std::mutex> lock(instance.loaderMutex);
        instance.loaderStopping = true;
        instance.loadRequests.clear();
    }
    instance.loaderCondition.notify_all();
    if (instance.loaderThread.joinable()) instance.loaderThread.join();
    std::lock_guard<std::mutex> lock(instance.loaderMutex);
    instance.loadResults.clear();
}

void queueSampleLoad(Plugin& instance, uint32_t slotIndex,
    std::string path)
{
    if (slotIndex >= s3g::breakbeat::kMaximumSampleSlots
        || path.empty()) return;
    instance.pendingMutationSlots[slotIndex] = false;
    const uint64_t generation = ++instance.loadGenerations[slotIndex];
    {
        std::lock_guard<std::mutex> lock(instance.loaderMutex);
        LoadRequest request;
        request.kind = LoadRequestKind::File;
        request.slotIndex = slotIndex;
        request.generation = generation;
        request.path = std::move(path);
        instance.loadRequests.push_back(std::move(request));
    }
    instance.status = "LOADING AND ANALYZING SAMPLE";
    instance.loaderCondition.notify_one();
}

bool queueStructuralMutation(Plugin& instance, uint32_t sourceSlot,
    uint32_t destinationSlot, uint32_t seed)
{
    if (!instance.controlBank
        || sourceSlot >= s3g::breakbeat::kMaximumSampleSlots
        || destinationSlot >= s3g::breakbeat::kMaximumSampleSlots
        || sourceSlot == destinationSlot
        || !instance.controlBank->slots[sourceSlot].asset
        || instance.controlBank->slots[destinationSlot].asset
        || instance.pendingMutationSlots[destinationSlot]) return false;
    auto mutationBank = structuralMutationBank(instance, sourceSlot,
        destinationSlot, seed);
    if (!mutationBank) return false;
    const uint64_t generation
        = ++instance.loadGenerations[destinationSlot];
    LoadRequest request;
    request.kind = LoadRequestKind::MutationPrint;
    request.slotIndex = destinationSlot;
    request.sourceSlot = sourceSlot;
    request.generation = generation;
    request.path = "MUTATED BREAK " + std::to_string(sourceSlot + 1u)
        + " SEED " + std::to_string(seed);
    request.printBank = std::move(mutationBank);
    request.renderSampleRate = instance.sampleRate;
    request.renderChannelCount = instance.outputChannelCount;
    request.mutationSeed = seed;
    request.mutationUses = instance.structuralMutationUses;
    {
        std::lock_guard<std::mutex> lock(instance.loaderMutex);
        instance.loadRequests.push_back(std::move(request));
    }
    instance.pendingMutationSlots[destinationSlot] = true;
    instance.loaderCondition.notify_one();
    return true;
}

uint32_t fillEmptySlotsWithMutations(Plugin& instance,
    uint32_t sourceSlot)
{
    if (!instance.controlBank
        || sourceSlot >= s3g::breakbeat::kMaximumSampleSlots
        || !instance.controlBank->slots[sourceSlot].asset) return 0u;
    uint32_t queued = 0u;
    for (uint32_t offset = 1u;
         offset <= s3g::breakbeat::kMaximumSampleSlots; ++offset) {
        const uint32_t destination = (sourceSlot + offset)
            % s3g::breakbeat::kMaximumSampleSlots;
        if (instance.controlBank->slots[destination].asset
            || instance.pendingMutationSlots[destination]) continue;
        const uint32_t seed = instance.mutationSeed++;
        if (instance.mutationSeed == 0u) instance.mutationSeed = 1u;
        if (queueStructuralMutation(instance, sourceSlot, destination,
                seed)) ++queued;
    }
    if (queued > 0u) markStateDirty(instance);
    return queued;
}

bool queueMutationExport(Plugin& instance, uint32_t sourceSlot,
    std::string path)
{
    if (!instance.controlBank || path.empty()
        || sourceSlot >= s3g::breakbeat::kMaximumSampleSlots
        || !instance.controlBank->slots[sourceSlot].asset) return false;
    LoadRequest request;
    request.kind = LoadRequestKind::MutationExport;
    request.slotIndex = sourceSlot;
    request.sourceSlot = sourceSlot;
    request.generation = ++instance.exportGeneration;
    request.path = std::move(path);
    request.printBank = instance.controlBank;
    request.renderSampleRate = instance.sampleRate;
    request.renderChannelCount = instance.outputChannelCount;
    {
        std::lock_guard<std::mutex> lock(instance.loaderMutex);
        instance.loadRequests.push_back(std::move(request));
    }
    instance.status = "RENDERING BREAK FOR WAV EXPORT";
    instance.loaderCondition.notify_one();
    return true;
}

void cancelSampleLoad(Plugin& instance, uint32_t slotIndex)
{
    if (slotIndex >= instance.loadGenerations.size()) return;
    ++instance.loadGenerations[slotIndex];
    instance.pendingMutationSlots[slotIndex] = false;
}

void cancelAllSampleLoads(Plugin& instance)
{
    for (auto& generation : instance.loadGenerations) ++generation;
    instance.pendingMutationSlots.fill(false);
    ++instance.exportGeneration;
    std::lock_guard<std::mutex> lock(instance.loaderMutex);
    instance.loadRequests.clear();
    instance.loadResults.clear();
}

void serviceSampleLoads(Plugin& instance)
{
    std::deque<LoadResult> completed;
    {
        std::lock_guard<std::mutex> lock(instance.loaderMutex);
        completed.swap(instance.loadResults);
    }
    for (auto& result : completed) {
        if (result.kind == LoadRequestKind::MutationExport) {
            if (result.generation != instance.exportGeneration) continue;
            instance.status = result.error.empty() && result.asset
                ? "BREAK WAV EXPORT COMPLETE"
                : result.error.empty() ? "BREAK WAV EXPORT FAILED"
                                       : result.error;
            continue;
        }
        if (result.slotIndex >= instance.loadGenerations.size()
            || result.generation
                != instance.loadGenerations[result.slotIndex]) continue;
        if (result.kind == LoadRequestKind::MutationPrint)
            instance.pendingMutationSlots[result.slotIndex] = false;
        if (!result.asset || !result.analysis) {
            instance.status = result.error.empty()
                ? "SAMPLE DECODE FAILED" : result.error;
            continue;
        }
        if (result.kind == LoadRequestKind::MutationPrint) {
            (void)installMutationPrint(instance, result);
        } else {
            (void)installDecodedSample(instance, result.slotIndex,
                result.path, std::move(result.asset),
                std::move(result.analysis));
        }
    }
}
#endif

void appendNoteEvent(Plugin& instance, std::size_t& count,
    uint32_t frame, EventKind kind, uint64_t noteId, uint8_t key,
    float velocity, uint8_t midiChannel,
    uint8_t chokeGroup = 0u) noexcept
{
    if (count >= instance.blockEvents.size()) return;
    instance.blockEvents[count++] = {
        frame, kind, noteId, key, chokeGroup,
        std::clamp(velocity, 0.0f, 1.0f),
        midiChannel,
    };
    if (kind == EventKind::NoteOn) {
        instance.midiKeyActivity[key].store(
            std::clamp(velocity, 0.0f, 1.0f),
            std::memory_order_relaxed);
        instance.midiKeyChannels[key].store(midiChannel,
            std::memory_order_relaxed);
    }
}

bool insertRenderEventSorted(Plugin& instance, std::size_t& count,
    const RenderEvent& event) noexcept
{
    if (count >= instance.blockEvents.size()) return false;
    std::size_t position = count;
    while (position > 0u
        && instance.blockEvents[position - 1u].frameOffset
            > event.frameOffset) {
        instance.blockEvents[position]
            = instance.blockEvents[position - 1u];
        --position;
    }
    instance.blockEvents[position] = event;
    ++count;
    if (event.kind == EventKind::NoteOn) {
        instance.midiKeyActivity[event.key].store(event.velocity,
            std::memory_order_relaxed);
        instance.midiKeyChannels[event.key].store(event.midiChannel,
            std::memory_order_relaxed);
    }
    return true;
}

void stopPlaythrough(Plugin& instance) noexcept
{
    instance.audioPlaythroughActive = false;
    instance.audioPlaythroughNextSlice = 0u;
    instance.audioPlaythroughFramesUntilBoundary = 0u;
    instance.playthroughSlotTelemetry.store(0u,
        std::memory_order_relaxed);
}

uint64_t playthroughSliceDuration(const Plugin& instance,
    const s3g::breakbeat::SampleSlot& slot,
    const s3g::breakbeat::Slice& slice, double hostTempoBpm) noexcept
{
    if (!slot.asset || !(instance.sampleRate > 0.0)
        || slice.endFrame <= slice.startFrame) return 1u;
    const double semitones = static_cast<double>(slice.transposeSemitones)
        + static_cast<double>(slice.fineTuneCents) * 0.01;
    const double pitchRatio = std::pow(2.0, semitones / 12.0);
    const auto pitchMode = slot.pitchMode
            == s3g::breakbeat::PitchMode::RateBelowStretchAbove
        ? semitones < 0.0 ? s3g::breakbeat::PitchMode::Rate
                          : s3g::breakbeat::PitchMode::Stretch
        : slot.pitchMode;
    const double syncRatio = slot.syncMode
            == s3g::breakbeat::SyncMode::Host
        ? std::clamp(hostTempoBpm
                / static_cast<double>(slot.sourceTempoBpm), 0.01, 32.0)
        : 1.0;
    const double transportRatio = slot.asset->sampleRate
        / instance.sampleRate * syncRatio;
    const double increment = transportRatio
        * (pitchMode == s3g::breakbeat::PitchMode::Rate
            ? pitchRatio : 1.0);
    const double frames = std::ceil(static_cast<double>(
        slice.endFrame - slice.startFrame)
        / std::max(increment, std::numeric_limits<double>::min()));
    return static_cast<uint64_t>(std::clamp(frames, 1.0,
        static_cast<double>(std::numeric_limits<uint32_t>::max())));
}

void schedulePlaythrough(Plugin& instance, uint32_t frames,
    double hostTempoBpm, std::size_t& renderEventCount) noexcept
{
    const uint32_t requested = instance.pendingPlaythroughSlot.exchange(
        0u, std::memory_order_acq_rel);
    if (requested > 0u
        && requested <= s3g::breakbeat::kMaximumSampleSlots) {
        instance.audioPlaythroughActive = true;
        instance.audioPlaythroughSlot = static_cast<uint8_t>(requested - 1u);
        instance.audioPlaythroughNextSlice = 0u;
        instance.audioPlaythroughFramesUntilBoundary = 0u;
        instance.playthroughSlotTelemetry.store(requested,
            std::memory_order_relaxed);
    }
    if (!instance.audioPlaythroughActive || !instance.audioBank
        || instance.audioPlaythroughSlot >= instance.audioBank->slots.size())
        return;
    const auto& slot = instance.audioBank->slots[
        instance.audioPlaythroughSlot];
    if (!slotHasCompleteMap(slot)) {
        stopPlaythrough(instance);
        return;
    }

    uint64_t boundary = instance.audioPlaythroughFramesUntilBoundary;
    while (instance.audioPlaythroughActive && boundary < frames) {
        const uint32_t frame = static_cast<uint32_t>(boundary);
        const RenderEvent stop { frame, EventKind::StopSlot, 0u, 0u,
            instance.audioPlaythroughSlot, 0.0f, 0u };
        if (!insertRenderEventSorted(instance, renderEventCount, stop)) {
            stopPlaythrough(instance);
            return;
        }
        if (instance.audioPlaythroughNextSlice >= slot.sliceCount) {
            stopPlaythrough(instance);
            break;
        }
        const uint16_t sliceIndex = instance.audioPlaythroughNextSlice++;
        const uint8_t key = static_cast<uint8_t>(
            slot.mappedRootNote + sliceIndex);
        const RenderEvent note { frame, EventKind::NoteOn,
            instance.auditionCounter.fetch_add(1u,
                std::memory_order_relaxed), key, 0u, 1.0f,
            slot.midiChannel == 0u ? static_cast<uint8_t>(1u)
                                   : slot.midiChannel };
        if (!insertRenderEventSorted(instance, renderEventCount, note)) {
            stopPlaythrough(instance);
            return;
        }
        boundary += playthroughSliceDuration(instance, slot,
            slot.slices[sliceIndex], hostTempoBpm);
    }
    if (instance.audioPlaythroughActive)
        instance.audioPlaythroughFramesUntilBoundary = boundary - frames;
}

void readInputEvents(Plugin& instance, const clap_input_events_t* events,
    uint32_t frames, std::size_t& renderEventCount) noexcept
{
    const uint32_t audition = instance.pendingAuditionNote.exchange(0u,
        std::memory_order_acq_rel);
    const uint32_t auditionChannel = instance.pendingAuditionChannel.exchange(
        0u, std::memory_order_acq_rel);
    if (audition > 0u && audition <= s3g::breakbeat::kMidiNoteCount) {
        appendNoteEvent(instance, renderEventCount, 0u, EventKind::NoteOn,
            instance.auditionCounter.fetch_add(1u,
                std::memory_order_relaxed),
            static_cast<uint8_t>(audition - 1u), 1.0f,
            static_cast<uint8_t>(std::min<uint32_t>(auditionChannel, 16u)));
    }
    if (!events || !events->size || !events->get) return;
    const uint32_t eventCount = events->size(events);
    const float sensitivity = static_cast<float>(
        instance.velocitySensitivity.load(std::memory_order_relaxed));
    for (uint32_t index = 0u; index < eventCount; ++index) {
        const clap_event_header_t* header = events->get(events, index);
        if (!header || header->space_id != CLAP_CORE_EVENT_SPACE_ID) continue;
        if (header->type == CLAP_EVENT_PARAM_VALUE
            && header->size >= sizeof(clap_event_param_value_t)) {
            const auto* event = reinterpret_cast<
                const clap_event_param_value_t*>(header);
            setParam(instance, event->param_id, event->value);
            continue;
        }
        const uint32_t frame = frames == 0u ? 0u
            : std::min(header->time, frames - 1u);
        if ((header->type == CLAP_EVENT_NOTE_ON
                || header->type == CLAP_EVENT_NOTE_OFF
                || header->type == CLAP_EVENT_NOTE_CHOKE)
            && header->size >= sizeof(clap_event_note_t)) {
            const auto* event = reinterpret_cast<const clap_event_note_t*>(
                header);
            if (event->port_index != 0 || event->key < 0 || event->key > 127)
                continue;
            const uint64_t noteId = event->note_id >= 0
                ? static_cast<uint64_t>(event->note_id) + 1u : 0u;
            const auto kind = header->type == CLAP_EVENT_NOTE_ON
                ? EventKind::NoteOn
                : header->type == CLAP_EVENT_NOTE_OFF
                    ? EventKind::NoteOff : EventKind::Choke;
            const float velocity = kind == EventKind::NoteOn
                ? 1.0f + (static_cast<float>(event->velocity) - 1.0f)
                    * sensitivity
                : 0.0f;
            appendNoteEvent(instance, renderEventCount, frame, kind, noteId,
                static_cast<uint8_t>(event->key), velocity,
                event->channel >= 0 && event->channel < 16
                    ? static_cast<uint8_t>(event->channel + 1) : 0u);
            continue;
        }
        if (header->type == CLAP_EVENT_MIDI
            && header->size >= sizeof(clap_event_midi_t)) {
            const auto* event = reinterpret_cast<const clap_event_midi_t*>(
                header);
            if (event->port_index != 0u) continue;
            const uint8_t status = event->data[0u] & 0xf0u;
            const int16_t channel = static_cast<int16_t>(event->data[0u]
                & 0x0fu);
            const uint8_t key = event->data[1u] & 0x7fu;
            if (status == 0x90u && event->data[2u] != 0u) {
                const float raw = static_cast<float>(event->data[2u])
                    / 127.0f;
                appendNoteEvent(instance, renderEventCount, frame,
                    EventKind::NoteOn, 0u, key,
                    1.0f + (raw - 1.0f) * sensitivity,
                    static_cast<uint8_t>(channel + 1));
            } else if (status == 0x80u
                || (status == 0x90u && event->data[2u] == 0u)) {
                appendNoteEvent(instance, renderEventCount, frame,
                    EventKind::NoteOff, 0u, key, 0.0f,
                    static_cast<uint8_t>(channel + 1));
            }
        }
    }
}

void readParameterEvents(Plugin& instance,
    const clap_input_events_t* events) noexcept
{
    if (!events || !events->size || !events->get) return;
    const uint32_t eventCount = events->size(events);
    for (uint32_t index = 0u; index < eventCount; ++index) {
        const clap_event_header_t* header = events->get(events, index);
        if (!header || header->space_id != CLAP_CORE_EVENT_SPACE_ID
            || header->type != CLAP_EVENT_PARAM_VALUE
            || header->size < sizeof(clap_event_param_value_t)) continue;
        const auto* event = reinterpret_cast<
            const clap_event_param_value_t*>(header);
        setParam(instance, event->param_id, event->value);
    }
}

bool pluginInit(const clap_plugin_t* plugin)
{
    auto& instance = *self(plugin);
    for (auto& playhead : instance.slotPlayheads)
        playhead.store(-1.0f, std::memory_order_relaxed);
    for (auto& playhead : instance.voicePlayheads)
        playhead.store(-1.0f, std::memory_order_relaxed);
    for (auto& slot : instance.voicePlayheadSlots)
        slot.store(0xffu, std::memory_order_relaxed);
    for (auto& key : instance.voicePlayheadKeys)
        key.store(0u, std::memory_order_relaxed);
    for (auto& peak : instance.slotPeaks)
        peak.store(0.0f, std::memory_order_relaxed);
    instance.auxActivity.store(0.0f, std::memory_order_relaxed);
    instance.auxGainReductionDb.store(0.0f, std::memory_order_relaxed);
    if (instance.host && instance.host->get_extension) {
        instance.hostNoteNames = static_cast<const clap_host_note_name_t*>(
            instance.host->get_extension(instance.host, CLAP_EXT_NOTE_NAME));
        instance.hostState = static_cast<const clap_host_state_t*>(
            instance.host->get_extension(instance.host, CLAP_EXT_STATE));
    }
    initializeBank(instance);
#if defined(__APPLE__)
    registerBundledFonts();
    if (!startSampleLoader(instance)) {
        instance.status = "COULD NOT START SAMPLE LOADER";
        return false;
    }
#endif
    return true;
}

#if defined(__APPLE__)
void destroyGui(Plugin& instance);
#endif

void pluginDestroy(const clap_plugin_t* plugin)
{
#if defined(__APPLE__)
    auto& instance = *self(plugin);
    destroyGui(instance);
    stopSampleLoader(instance);
#endif
    delete self(plugin);
}

bool pluginActivate(const clap_plugin_t* plugin, double sampleRate,
    uint32_t, uint32_t maximumFrames)
{
    auto& instance = *self(plugin);
    if (!(sampleRate > 0.0) || maximumFrames == 0u
        || !instance.engine.prepare(sampleRate,
            instance.outputChannelCount)) return false;
    instance.sampleRate = sampleRate;
    instance.maximumFrames = maximumFrames;
    for (std::size_t channel = 0u;
         channel < instance.scratchChannels.size(); ++channel) {
        if (channel < instance.outputChannelCount)
            instance.scratchChannels[channel].assign(maximumFrames, 0.0f);
        else instance.scratchChannels[channel].clear();
    }
    instance.audioBank = instance.publishedBank.load(
        std::memory_order_acquire);
    instance.audioMixer = instance.publishedMixer.load(
        std::memory_order_acquire);
    instance.engine.setPreparedBank(instance.audioBank);
    instance.engine.setPreparedMixer(instance.audioMixer);
    stopPlaythrough(instance);
    instance.pendingPlaythroughSlot.store(0u, std::memory_order_relaxed);
    instance.active = true;
    return true;
}

void pluginDeactivate(const clap_plugin_t* plugin)
{
    auto& instance = *self(plugin);
    instance.active = false;
    instance.engine.unprepare();
    instance.audioBank = nullptr;
    instance.audioMixer = nullptr;
    stopPlaythrough(instance);
    instance.pendingPlaythroughSlot.store(0u, std::memory_order_relaxed);
    for (auto& playhead : instance.slotPlayheads)
        playhead.store(-1.0f, std::memory_order_relaxed);
    for (auto& playhead : instance.voicePlayheads)
        playhead.store(-1.0f, std::memory_order_relaxed);
    for (auto& slot : instance.voicePlayheadSlots)
        slot.store(0xffu, std::memory_order_relaxed);
    for (auto& key : instance.voicePlayheadKeys)
        key.store(0u, std::memory_order_relaxed);
    for (auto& activity : instance.midiKeyActivity)
        activity.store(0.0f, std::memory_order_relaxed);
    for (auto& channel : instance.midiKeyChannels)
        channel.store(0u, std::memory_order_relaxed);
    for (auto& peak : instance.slotPeaks)
        peak.store(0.0f, std::memory_order_relaxed);
    instance.auxActivity.store(0.0f, std::memory_order_relaxed);
    instance.auxGainReductionDb.store(0.0f, std::memory_order_relaxed);
    for (auto& channel : instance.scratchChannels) channel.clear();
    // No audio callback can still hold an older immutable bank here. Retain
    // only the current control snapshot so repeated edits do not accumulate
    // sample memory across deactivate/reactivate cycles.
    instance.retainedBanks.clear();
    if (instance.controlBank)
        instance.retainedBanks.push_back(instance.controlBank);
    instance.retainedMixers.clear();
    if (instance.controlMixer)
        instance.retainedMixers.push_back(instance.controlMixer);
}

bool pluginStartProcessing(const clap_plugin_t* plugin)
{
    return self(plugin)->active;
}

void pluginStopProcessing(const clap_plugin_t*) {}

void pluginReset(const clap_plugin_t* plugin)
{
    auto& instance = *self(plugin);
    instance.engine.reset();
    stopPlaythrough(instance);
    instance.pendingPlaythroughSlot.store(0u, std::memory_order_relaxed);
    for (auto& playhead : instance.slotPlayheads)
        playhead.store(-1.0f, std::memory_order_relaxed);
    for (auto& playhead : instance.voicePlayheads)
        playhead.store(-1.0f, std::memory_order_relaxed);
    for (auto& slot : instance.voicePlayheadSlots)
        slot.store(0xffu, std::memory_order_relaxed);
    for (auto& key : instance.voicePlayheadKeys)
        key.store(0u, std::memory_order_relaxed);
    for (auto& activity : instance.midiKeyActivity)
        activity.store(0.0f, std::memory_order_relaxed);
    for (auto& channel : instance.midiKeyChannels)
        channel.store(0u, std::memory_order_relaxed);
    for (auto& peak : instance.slotPeaks)
        peak.store(0.0f, std::memory_order_relaxed);
    instance.auxActivity.store(0.0f, std::memory_order_relaxed);
    instance.auxGainReductionDb.store(0.0f, std::memory_order_relaxed);
}

clap_process_status pluginProcess(const clap_plugin_t* plugin,
    const clap_process_t* process)
{
    if (!process) return CLAP_PROCESS_ERROR;
    auto& instance = *self(plugin);
    const uint32_t frames = process->frames_count;
    if (frames > instance.maximumFrames
        || instance.outputChannelCount == 0u
        || instance.outputChannelCount > instance.scratchChannels.size()
        || frames > instance.scratchChannels[0u].size())
        return CLAP_PROCESS_ERROR;

    const BankSnapshot* nextBank = instance.publishedBank.load(
        std::memory_order_acquire);
    if (nextBank != instance.audioBank) {
        instance.audioBank = nextBank;
        instance.engine.setPreparedBank(nextBank);
    }
    const MixerSnapshot* nextMixer = instance.publishedMixer.load(
        std::memory_order_acquire);
    if (nextMixer != instance.audioMixer) {
        instance.audioMixer = nextMixer;
        instance.engine.setPreparedMixer(nextMixer);
    }
    double hostTempoBpm = 120.0;
    if (process->transport
        && (process->transport->flags & CLAP_TRANSPORT_HAS_TEMPO) != 0u
        && std::isfinite(process->transport->tempo)
        && process->transport->tempo > 0.0)
        hostTempoBpm = process->transport->tempo;
    std::size_t renderEventCount = 0u;
    readInputEvents(instance, process->in_events, frames, renderEventCount);
    if (instance.pendingKillAll.exchange(false, std::memory_order_acq_rel)) {
        instance.engine.killAll();
        stopPlaythrough(instance);
    }
    schedulePlaythrough(instance, frames, hostTempoBpm,
        renderEventCount);
    std::array<float*, s3g::breakbeat::kMaximumAudioChannels> scratch {};
    for (uint32_t channel = 0u; channel < instance.outputChannelCount;
         ++channel)
        scratch[channel] = instance.scratchChannels[channel].data();
    instance.engine.render(instance.blockEvents.data(), renderEventCount,
        scratch.data(), instance.outputChannelCount, frames, hostTempoBpm);
    for (std::size_t slot = 0u; slot < instance.slotPlayheads.size(); ++slot)
        instance.slotPlayheads[slot].store(
            instance.engine.slotPlayheadNormalized(
                static_cast<uint8_t>(slot)),
            std::memory_order_relaxed);
    const auto& cursors = instance.engine.voiceCursors();
    const uint32_t cursorCount = instance.engine.voiceCursorCount();
    for (std::size_t index = 0u;
         index < instance.voicePlayheads.size(); ++index) {
        const bool active = index < cursorCount;
        instance.voicePlayheads[index].store(active
                ? cursors[index].sourcePositionNormalized : -1.0f,
            std::memory_order_relaxed);
        instance.voicePlayheadSlots[index].store(active
                ? cursors[index].slotIndex : 0xffu,
            std::memory_order_relaxed);
        instance.voicePlayheadKeys[index].store(active
                ? cursors[index].key : 0u,
            std::memory_order_relaxed);
    }
    for (std::size_t slot = 0u; slot < instance.slotPeaks.size(); ++slot) {
        const float previousSlotPeak = instance.slotPeaks[slot].load(
            std::memory_order_relaxed);
        instance.slotPeaks[slot].store(std::max(
            instance.engine.slotPeak(static_cast<uint8_t>(slot)),
            previousSlotPeak * 0.90f), std::memory_order_relaxed);
    }
    const float previousAux = instance.auxActivity.load(
        std::memory_order_relaxed);
    instance.auxActivity.store(std::max(instance.engine.auxActivity(),
        previousAux * 0.90f), std::memory_order_relaxed);
    instance.auxGainReductionDb.store(
        instance.engine.auxGainReductionDb(), std::memory_order_relaxed);
    const float keyDecay = static_cast<float>(std::exp(
        -static_cast<double>(frames)
            / std::max(1.0, instance.sampleRate * 0.12)));
    for (auto& activity : instance.midiKeyActivity) {
        activity.store(activity.load(std::memory_order_relaxed) * keyDecay,
            std::memory_order_relaxed);
    }

    const float gain = static_cast<float>(std::pow(10.0,
        instance.outputGainDb.load(std::memory_order_relaxed) / 20.0));
    float peak = 0.0f;
    if (process->audio_outputs_count > 0u && process->audio_outputs) {
        auto& output = process->audio_outputs[0u];
        if (output.channel_count < instance.outputChannelCount)
            return CLAP_PROCESS_ERROR;
        for (uint32_t channel = 0u; channel < output.channel_count; ++channel) {
            const bool activeChannel = channel < instance.outputChannelCount;
            const auto* source = activeChannel
                ? instance.scratchChannels[channel].data() : nullptr;
            if (output.data32 && output.data32[channel]) {
                for (uint32_t frame = 0u; frame < frames; ++frame) {
                    const float value = source ? source[frame] * gain : 0.0f;
                    output.data32[channel][frame] = value;
                    peak = std::max(peak, std::abs(value));
                }
            } else if (output.data64 && output.data64[channel]) {
                for (uint32_t frame = 0u; frame < frames; ++frame) {
                    const float value = source ? source[frame] * gain : 0.0f;
                    output.data64[channel][frame] = value;
                    peak = std::max(peak, std::abs(value));
                }
            }
        }
    }
    const float previous = instance.outputPeak.load(std::memory_order_relaxed);
    instance.outputPeak.store(std::max(peak, previous * 0.92f),
        std::memory_order_relaxed);
    // Keep the instrument awake: this makes host MIDI and editor audition
    // deterministic even in hosts whose sleeping-plugin wake policy differs.
    return CLAP_PROCESS_CONTINUE;
}

void pluginOnMainThread(const clap_plugin_t* plugin)
{
#if defined(__APPLE__)
    serviceSampleLoads(*self(plugin));
#else
    (void)plugin;
#endif
}

uint32_t audioPortsCount(const clap_plugin_t*, bool isInput)
{
    return isInput ? 0u : 1u;
}

bool fillOutputPortInfo(const OutputConfigDef& config, uint32_t index,
    bool isInput,
    clap_audio_port_info_t* info)
{
    if (isInput || index != 0u || !info) return false;
    *info = {};
    info->id = 20u;
    std::snprintf(info->name, sizeof(info->name), "%s Out", config.name);
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = config.channelCount;
    info->port_type = config.portType;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}

bool audioPortsGet(const clap_plugin_t* plugin, uint32_t index, bool isInput,
    clap_audio_port_info_t* info)
{
    const auto* config = outputConfig(self(plugin)->outputConfigId);
    return config && fillOutputPortInfo(*config, index, isInput, info);
}

const clap_plugin_audio_ports_t audioPorts {
    audioPortsCount,
    audioPortsGet,
};

uint32_t audioPortsConfigCount(const clap_plugin_t*)
{
    return 1u;
}

bool audioPortsConfigGet(const clap_plugin_t* plugin, uint32_t index,
    clap_audio_ports_config_t* config)
{
    if (!config || index != 0u) return false;
    const auto* def = outputConfig(self(plugin)->outputConfigId);
    if (!def) return false;
    *config = {};
    config->id = def->id;
    std::snprintf(config->name, sizeof(config->name), "%s", def->name);
    config->input_port_count = 0u;
    config->output_port_count = 1u;
    config->has_main_output = true;
    config->main_output_channel_count = def->channelCount;
    config->main_output_port_type = def->portType;
    return true;
}

bool audioPortsConfigSelect(const clap_plugin_t* plugin, clap_id id)
{
    return self(plugin)->outputConfigId == id;
}

const clap_plugin_audio_ports_config_t audioPortsConfig {
    audioPortsConfigCount,
    audioPortsConfigGet,
    audioPortsConfigSelect,
};

clap_id audioPortsConfigCurrent(const clap_plugin_t* plugin)
{
    return self(plugin)->outputConfigId;
}

bool audioPortsConfigInfoGet(const clap_plugin_t* plugin, clap_id configId,
    uint32_t portIndex, bool isInput, clap_audio_port_info_t* info)
{
    const auto& instance = *self(plugin);
    const auto* config = outputConfig(instance.outputConfigId);
    return config && configId == instance.outputConfigId
        && fillOutputPortInfo(*config, portIndex, isInput, info);
}

const clap_plugin_audio_ports_config_info_t audioPortsConfigInfo {
    audioPortsConfigCurrent,
    audioPortsConfigInfoGet,
};

uint32_t notePortsCount(const clap_plugin_t*, bool isInput)
{
    return isInput ? 1u : 0u;
}

bool notePortsGet(const clap_plugin_t*, uint32_t index, bool isInput,
    clap_note_port_info_t* info)
{
    if (!isInput || index != 0u || !info) return false;
    *info = {};
    info->id = 30u;
    info->supported_dialects = CLAP_NOTE_DIALECT_CLAP
        | CLAP_NOTE_DIALECT_MIDI;
    info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
    std::snprintf(info->name, sizeof(info->name), "%s", "MIDI In");
    return true;
}

const clap_plugin_note_ports_t notePorts {
    notePortsCount,
    notePortsGet,
};

uint32_t noteNameCount(const clap_plugin_t* plugin)
{
    const auto& instance = *self(plugin);
    if (!instance.controlBank) return 0u;
    uint32_t count = 0u;
    for (const auto& slot : instance.controlBank->slots)
        count += std::min<uint16_t>(slot.mappedSliceCount, slot.sliceCount);
    return count;
}

bool noteNameGet(const clap_plugin_t* plugin, uint32_t index,
    clap_note_name_t* noteName)
{
    if (!noteName) return false;
    const auto& instance = *self(plugin);
    if (!instance.controlBank) return false;
    uint32_t found = 0u;
    for (std::size_t slotIndex = 0u;
         slotIndex < instance.controlBank->slots.size(); ++slotIndex) {
        const auto& slot = instance.controlBank->slots[slotIndex];
        const uint16_t count = std::min<uint16_t>(
            slot.mappedSliceCount, slot.sliceCount);
        for (uint16_t slice = 0u; slice < count; ++slice) {
            if (found++ != index) continue;
            *noteName = {};
            noteName->port = 0;
            noteName->key = static_cast<int16_t>(slot.mappedRootNote + slice);
            noteName->channel = slot.midiChannel == 0u
                ? -1 : static_cast<int16_t>(slot.midiChannel - 1u);
            std::snprintf(noteName->name, sizeof(noteName->name),
                "BREAK %u / SLICE %03u",
                static_cast<unsigned>(slotIndex + 1u),
                static_cast<unsigned>(slice + 1u));
            return true;
        }
    }
    return false;
}

const clap_plugin_note_name_t noteNames {
    noteNameCount,
    noteNameGet,
};

uint32_t paramsCount(const clap_plugin_t*)
{
    return static_cast<uint32_t>(kParamDefs.size());
}

bool paramsGetInfo(const clap_plugin_t*, uint32_t index,
    clap_param_info_t* info)
{
    if (!info || index >= kParamDefs.size()) return false;
    const auto& def = kParamDefs[index];
    *info = {};
    info->id = def.id;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    if (def.stepped) info->flags |= CLAP_PARAM_IS_STEPPED;
    std::snprintf(info->name, sizeof(info->name), "%s", def.name);
    std::snprintf(info->module, sizeof(info->module), "%s", def.module);
    info->min_value = def.minimum;
    info->max_value = def.maximum;
    info->default_value = def.defaultValue;
    return true;
}

bool paramsGetValue(const clap_plugin_t* plugin, clap_id id, double* value)
{
    if (!value || !paramDef(id)) return false;
    *value = paramValue(*self(plugin), id);
    return true;
}

bool paramsValueToText(const clap_plugin_t*, clap_id id, double value,
    char* display, uint32_t size)
{
    if (!display || size == 0u || !paramDef(id)) return false;
    if (id == kOutputGainParamId)
        std::snprintf(display, size, "%+.1f dB", value);
    else if (id == kVelocityParamId)
        std::snprintf(display, size, "%.0f %%", value * 100.0);
    return true;
}

bool paramsTextToValue(const clap_plugin_t*, clap_id id,
    const char* display, double* value)
{
    if (!display || !value || !paramDef(id)) return false;
    char* end = nullptr;
    double parsed = std::strtod(display, &end);
    if (end == display) return false;
    if (id == kVelocityParamId && std::strchr(display, '%')) parsed *= 0.01;
    *value = parsed;
    return true;
}

void paramsFlush(const clap_plugin_t* plugin,
    const clap_input_events_t* input, const clap_output_events_t*)
{
    readParameterEvents(*self(plugin), input);
}

const clap_plugin_params_t params {
    paramsCount,
    paramsGetInfo,
    paramsGetValue,
    paramsValueToText,
    paramsTextToValue,
    paramsFlush,
};

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    if (!stream || !stream->write) return false;
    const auto& instance = *self(plugin);
    if (!instance.controlBank) return false;
    SavedState saved;
    std::memset(&saved, 0, sizeof(saved));
    saved.magic = kStateMagic;
    saved.version = kStateVersion;
    saved.selectedSlot = instance.selectedSlot;
    saved.interpolation = static_cast<uint32_t>(
        instance.controlBank->interpolation);
    saved.outputGainDb = paramValue(instance, kOutputGainParamId);
    saved.velocitySensitivity = paramValue(instance, kVelocityParamId);
    saved.outputConfigId = instance.outputConfigId;
    saved.auxPress = instance.controlBank->auxPress;
    saved.auxSnap = instance.controlBank->auxSnap;
    saved.auxRecovery = instance.controlBank->auxRecovery;
    saved.auxSaturation = instance.controlBank->auxSaturation;
    saved.auxBite = instance.controlBank->auxBite;
    saved.auxClip = instance.controlBank->auxClip;
    saved.auxTilt = instance.controlBank->auxTilt;
    saved.auxReturnDb = instance.controlBank->auxReturnDb;
    saved.embedSamples = instance.embedSamplesInState ? 1u : 0u;
    saved.auxEnabled = instance.controlBank->auxEnabled ? 1u : 0u;
    saved.auxLinkMode = static_cast<uint8_t>(
        instance.controlBank->auxLinkMode);
    saved.auxFieldSafe = instance.controlBank->auxFieldSafe ? 1u : 0u;
    saved.transientPreRollMicroseconds
        = instance.transientPreRollMicroseconds;
    uint64_t embeddedBytes = 0u;
    for (std::size_t index = 0u; index < saved.slots.size(); ++index) {
        const auto& source = instance.controlBank->slots[index];
        auto& destination = saved.slots[index];
        std::snprintf(destination.path.data(), destination.path.size(), "%s",
            instance.samplePaths[index].c_str());
        for (std::size_t sliceIndex = 0u;
             sliceIndex < destination.slices.size(); ++sliceIndex) {
            const auto& sourceSlice = source.slices[sliceIndex];
            auto& destinationSlice = destination.slices[sliceIndex];
            destinationSlice.startFrame = sourceSlice.startFrame;
            destinationSlice.endFrame = sourceSlice.endFrame;
            destinationSlice.loopStartFrame = sourceSlice.loopStartFrame;
            destinationSlice.loopEndFrame = sourceSlice.loopEndFrame;
            destinationSlice.gain = sourceSlice.gain;
            destinationSlice.pan = sourceSlice.pan;
            destinationSlice.transposeSemitones
                = sourceSlice.transposeSemitones;
            destinationSlice.fineTuneCents = sourceSlice.fineTuneCents;
            destinationSlice.chokeGroup = sourceSlice.chokeGroup;
            destinationSlice.launchMode = sourceSlice.launchMode;
            destinationSlice.reverse = sourceSlice.reverse;
        }
        destination.envelope.attackProportion
            = source.envelope.attackProportion;
        destination.envelope.decayProportion
            = source.envelope.decayProportion;
        destination.envelope.sustain = source.envelope.sustain;
        destination.envelope.releaseProportion
            = source.envelope.releaseProportion;
        destination.sliceCount = source.sliceCount;
        destination.mappedSliceCount = source.mappedSliceCount;
        destination.mixerGain = source.mixerGain;
        destination.mixerPan = source.mixerPan;
        destination.mixerLowEqDb = source.mixerLowEqDb;
        destination.mixerMidEqDb = source.mixerMidEqDb;
        destination.mixerHighEqDb = source.mixerHighEqDb;
        destination.mixerMidFrequencyHz = source.mixerMidFrequencyHz;
        destination.mixerAuxSend = source.mixerAuxSend;
        for (std::size_t insertIndex = 0u;
             insertIndex < destination.inserts.size(); ++insertIndex) {
            const auto& sourceInsert = source.inserts[insertIndex];
            auto& destinationInsert = destination.inserts[insertIndex];
            destinationInsert.type = sourceInsert.type;
            destinationInsert.mode = sourceInsert.mode;
            destinationInsert.variant = sourceInsert.variant;
            destinationInsert.values = sourceInsert.values;
            destinationInsert.bypassed = sourceInsert.bypassed;
        }
        destination.rootNote = source.rootNote;
        destination.mappedRootNote = source.mappedRootNote;
        destination.midiChannel = source.midiChannel;
        destination.muted = source.muted ? 1u : 0u;
        destination.solo = source.solo ? 1u : 0u;
        if (source.asset) {
            destination.channelCount = source.asset->channelCount;
            destination.frameCount = source.asset->frameCount();
            destination.sampleRate = source.asset->sampleRate;
            const uint64_t bytes = static_cast<uint64_t>(
                destination.channelCount) * destination.frameCount
                * sizeof(float);
            const bool generated = instance.samplePaths[index].rfind(
                    "PRINTED BREAK ", 0u) == 0u
                || instance.samplePaths[index].rfind(
                    "MUTATED BREAK ", 0u) == 0u;
            if ((instance.embedSamplesInState || generated)
                && bytes <= kMaximumEmbeddedAudioBytes - embeddedBytes) {
                destination.embedded = 1u;
                embeddedBytes += bytes;
            } else if (generated) {
                return false;
            }
        }
    }
    if (!s3g::clap_state::writeAll(stream, &saved, sizeof(saved)))
        return false;
    for (std::size_t index = 0u; index < saved.slots.size(); ++index) {
        const auto& metadata = saved.slots[index];
        const auto& slot = instance.controlBank->slots[index];
        if (metadata.embedded == 0u || !slot.asset) continue;
        for (std::size_t channel = 0u;
             channel < slot.asset->channelCount; ++channel) {
            const auto& samples = slot.asset->channels[channel];
            if (!s3g::clap_state::writeAll(stream, samples.data(),
                    samples.size() * sizeof(float))) return false;
        }
    }
    auto mutation = std::unique_ptr<SavedMutationState>(
        new (std::nothrow) SavedMutationState());
    if (!mutation) return false;
    mutation->magic = kMutationStateMagic;
    mutation->version = kMutationStateVersion;
    mutation->nextSeed = instance.mutationSeed;
    mutation->depth = instance.mutationDepth;
    mutation->targets = instance.structuralMutationUses;
    mutation->selectedVariations = instance.selectedVariations;
    const uint16_t minimumSliceMilliseconds = static_cast<uint16_t>(
        std::min(instance.minimumTransientSliceMilliseconds,
            kMaximumMinimumTransientSliceMilliseconds));
    mutation->reserved[0u] = static_cast<uint8_t>(
        minimumSliceMilliseconds & 0xffu);
    mutation->reserved[1u] = static_cast<uint8_t>(
        minimumSliceMilliseconds >> 8u);
    static_assert(std::is_trivially_copyable_v<MutationVariation>);
    std::memcpy(mutation->variations.data(), instance.variations.data(),
        sizeof(instance.variations));
    if (!s3g::clap_state::writeAll(stream, mutation.get(),
            sizeof(*mutation))) return false;
    SavedPlaybackState playback;
    for (std::size_t index = 0u; index < playback.slots.size(); ++index) {
        const auto& source = instance.controlBank->slots[index];
        auto& destination = playback.slots[index];
        destination.sourceTempoBpm = source.sourceTempoBpm;
        destination.loopCrossfade = source.loopCrossfade;
        destination.glideSeconds = source.glideSeconds;
        destination.triggerMode = static_cast<uint8_t>(source.triggerMode);
        destination.retriggerMode = static_cast<uint8_t>(
            source.retriggerMode);
        destination.voiceMode = static_cast<uint8_t>(source.voiceMode);
        destination.pitchMode = static_cast<uint8_t>(source.pitchMode);
        destination.syncMode = static_cast<uint8_t>(source.syncMode);
    }
    return s3g::clap_state::writeAll(stream, &playback,
        sizeof(playback));
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) return false;
    SavedState saved;
    if (!s3g::clap_state::readAll(stream, &saved, sizeof(saved))
        || saved.magic != kStateMagic
        || (saved.version != kLegacyStateVersion
            && saved.version != kPreviousStateVersion
            && saved.version != kMutationContainerStateVersion
            && saved.version != kStateVersion))
        return false;
    auto& instance = *self(plugin);
#if defined(__APPLE__)
    cancelAllSampleLoads(instance);
#endif
    auto bank = std::make_shared<BankSnapshot>();
    s3g::breakbeat::initializeEmptyBank(*bank);
    bank->interpolation = saved.interpolation == 0u
        ? s3g::breakbeat::Interpolation::Nearest
        : s3g::breakbeat::Interpolation::Linear;
    bank->auxEnabled = saved.auxEnabled != 0u;
    bank->auxPress = std::isfinite(saved.auxPress)
        ? std::clamp(saved.auxPress, 0.0f, 1.0f) : 0.42f;
    bank->auxSnap = std::isfinite(saved.auxSnap)
        ? std::clamp(saved.auxSnap, -1.0f, 1.0f) : 0.18f;
    bank->auxRecovery = std::isfinite(saved.auxRecovery)
        ? std::clamp(saved.auxRecovery, 0.0f, 1.0f) : 0.34f;
    bank->auxSaturation = std::isfinite(saved.auxSaturation)
        ? std::clamp(saved.auxSaturation, 0.0f, 1.0f) : 0.20f;
    bank->auxBite = std::isfinite(saved.auxBite)
        ? std::clamp(saved.auxBite, 0.0f, 1.0f) : 0.08f;
    bank->auxClip = std::isfinite(saved.auxClip)
        ? std::clamp(saved.auxClip, 0.0f, 1.0f) : 0.0f;
    bank->auxTilt = std::isfinite(saved.auxTilt)
        ? std::clamp(saved.auxTilt, -1.0f, 1.0f) : 0.0f;
    bank->auxReturnDb = std::isfinite(saved.auxReturnDb)
        ? std::clamp(saved.auxReturnDb, -60.0f, 12.0f) : -9.0f;
    bank->auxLinkMode = static_cast<s3g::BreakBusLinkMode>(
        std::min<uint8_t>(saved.auxLinkMode,
            static_cast<uint8_t>(s3g::BreakBusLinkMode::Free)));
    bank->auxFieldSafe = saved.auxFieldSafe != 0u;
    instance.samplePaths = {};
    instance.analyses = {};
    uint64_t embeddedBytes = 0u;
    for (std::size_t index = 0u; index < saved.slots.size(); ++index) {
        const auto& source = saved.slots[index];
        bank->slots[index].rootNote = std::min<uint8_t>(source.rootNote, 127u);
        bank->slots[index].mappedRootNote = std::min<uint8_t>(
            source.mappedRootNote, 127u);
        bank->slots[index].midiChannel = std::min<uint8_t>(
            source.midiChannel, 16u);
        bank->slots[index].mixerGain = std::isfinite(source.mixerGain)
            ? std::clamp(source.mixerGain, 0.0f, 2.0f) : 1.0f;
        bank->slots[index].mixerPan = std::isfinite(source.mixerPan)
            ? std::clamp(source.mixerPan, -1.0f, 1.0f) : 0.0f;
        bank->slots[index].mixerLowEqDb = std::isfinite(
            source.mixerLowEqDb)
            ? std::clamp(source.mixerLowEqDb, -12.0f, 12.0f) : 0.0f;
        bank->slots[index].mixerMidEqDb = std::isfinite(
            source.mixerMidEqDb)
            ? std::clamp(source.mixerMidEqDb, -12.0f, 12.0f) : 0.0f;
        bank->slots[index].mixerHighEqDb = std::isfinite(
            source.mixerHighEqDb)
            ? std::clamp(source.mixerHighEqDb, -12.0f, 12.0f) : 0.0f;
        bank->slots[index].mixerMidFrequencyHz = std::isfinite(
            source.mixerMidFrequencyHz)
            ? std::clamp(source.mixerMidFrequencyHz, 120.0f, 8000.0f)
            : 900.0f;
        bank->slots[index].mixerAuxSend = std::isfinite(
            source.mixerAuxSend)
            ? std::clamp(source.mixerAuxSend, 0.0f, 1.0f) : 0.0f;
        for (std::size_t insert = 0u;
             insert < bank->slots[index].inserts.size(); ++insert) {
            auto restored = source.inserts[insert];
            // Variant occupies padding reserved by state v9's original
            // InsertSettings layout. Normalize an indeterminate legacy byte
            // while preserving the device and its four existing values.
            if (restored.variant > 2u) restored.variant = 0u;
            bank->slots[index].inserts[insert] = restored.valid()
                ? restored : s3g::breakbeat::InsertSettings {};
        }
        bank->slots[index].envelope = source.envelope.valid()
            ? source.envelope : s3g::breakbeat::Envelope {};
        bank->slots[index].muted = source.muted != 0u;
        bank->slots[index].solo = source.solo != 0u;
        std::shared_ptr<const SampleAsset> asset;
        std::shared_ptr<const SampleAnalysis> analysis;
        std::string error;
        const std::string path(source.path.data(),
            strnlen(source.path.data(), source.path.size()));
        if (source.embedded != 0u) {
            if (source.channelCount == 0u
                || source.channelCount
                    > s3g::breakbeat::kMaximumAudioChannels
                || source.frameCount == 0u || !(source.sampleRate > 0.0)
                || !std::isfinite(source.sampleRate)) return false;
            if (source.channelCount > instance.outputChannelCount) {
                instance.status
                    = "STATE REQUIRES S3G SAMPLE SLICER";
                return false;
            }
            const uint64_t bytes = static_cast<uint64_t>(source.channelCount)
                * source.frameCount * sizeof(float);
            if (bytes > kMaximumEmbeddedAudioBytes - embeddedBytes)
                return false;
            embeddedBytes += bytes;
            try {
                auto decoded = std::make_shared<SampleAsset>();
                decoded->channelCount = source.channelCount;
                decoded->sampleRate = source.sampleRate;
                for (std::size_t channel = 0u;
                     channel < decoded->channelCount; ++channel) {
                    auto& samples = decoded->channels[channel];
                    samples.resize(source.frameCount);
                    if (!s3g::clap_state::readAll(stream, samples.data(),
                            samples.size() * sizeof(float))) return false;
                }
                if (!decoded->valid()) return false;
                auto decodedAnalysis = std::make_shared<SampleAnalysis>(
                    s3g::breakbeat::analyzeSample(*decoded));
                if (!decodedAnalysis->validFor(*decoded)) return false;
                asset = std::move(decoded);
                analysis = std::move(decodedAnalysis);
            } catch (...) {
                return false;
            }
        } else {
#if defined(__APPLE__)
            if (path.empty()
                || !decodeSampleFile(path, asset, analysis, error)) continue;
            if (asset->channelCount > instance.outputChannelCount) {
                instance.status
                    = "STATE REQUIRES S3G SAMPLE SLICER";
                return false;
            }
#else
            continue;
#endif
        }
        auto& slot = bank->slots[index];
        slot.asset = std::move(asset);
        slot.sliceCount = static_cast<uint16_t>(std::min<std::size_t>(
            source.sliceCount, slot.slices.size()));
        std::copy_n(source.slices.begin(), slot.sliceCount,
            slot.slices.begin());
        bool slicesValid = slot.sliceCount > 0u;
        for (std::size_t slice = 0u; slice < slot.sliceCount; ++slice)
            slicesValid = slicesValid && slot.slices[slice].validFor(*slot.asset);
        if (!slicesValid) {
            slot.slices = {};
            slot.sliceCount = 1u;
            slot.slices[0u].endFrame = slot.asset->frameCount();
        }
        slot.mappedSliceCount = static_cast<uint16_t>(
            std::min<std::size_t>({ source.mappedSliceCount,
                slot.sliceCount,
                s3g::breakbeat::kMidiNoteCount - slot.mappedRootNote }));
        instance.samplePaths[index] = path;
        instance.analyses[index] = std::move(analysis);
    }
    std::unique_ptr<SavedMutationState> mutation;
    bool restoredMutations = false;
    if (saved.version >= kMutationContainerStateVersion) {
        mutation.reset(new (std::nothrow) SavedMutationState());
        if (!mutation
            || !s3g::clap_state::readAll(stream, mutation.get(),
                sizeof(*mutation))
            || mutation->magic != kMutationStateMagic
            || (mutation->version != kLegacyMutationStateVersion
                && mutation->version != kStructuralMutationStateVersion
                && mutation->version != kMixerFxMutationStateVersion
                && mutation->version != kMutationStateVersion)) return false;
        restoredMutations = true;
    }
    if (saved.version == kStateVersion) {
        SavedPlaybackState playback;
        if (!s3g::clap_state::readAll(stream, &playback, sizeof(playback))
            || playback.magic != 0x59414c50u || playback.version != 1u)
            return false;
        for (std::size_t index = 0u; index < playback.slots.size(); ++index) {
            const auto& source = playback.slots[index];
            auto& slot = bank->slots[index];
            slot.sourceTempoBpm = std::isfinite(source.sourceTempoBpm)
                ? std::clamp(source.sourceTempoBpm, 20.0f, 999.0f)
                : 120.0f;
            slot.loopCrossfade = std::isfinite(source.loopCrossfade)
                ? std::clamp(source.loopCrossfade, 0.0f, 0.5f) : 0.02f;
            slot.glideSeconds = std::isfinite(source.glideSeconds)
                ? std::clamp(source.glideSeconds, 0.0f, 2.0f) : 0.0f;
            slot.triggerMode = static_cast<s3g::breakbeat::TriggerMode>(
                std::min<uint8_t>(source.triggerMode,
                    static_cast<uint8_t>(
                        s3g::breakbeat::TriggerMode::Toggle)));
            slot.retriggerMode = static_cast<
                s3g::breakbeat::RetriggerMode>(std::min<uint8_t>(
                    source.retriggerMode, static_cast<uint8_t>(
                        s3g::breakbeat::RetriggerMode::Ignore)));
            slot.voiceMode = static_cast<s3g::breakbeat::VoiceMode>(
                std::min<uint8_t>(source.voiceMode,
                    static_cast<uint8_t>(
                        s3g::breakbeat::VoiceMode::Legato)));
            slot.pitchMode = static_cast<s3g::breakbeat::PitchMode>(
                std::min<uint8_t>(source.pitchMode,
                    static_cast<uint8_t>(
                        s3g::breakbeat::PitchMode::RateBelowStretchAbove)));
            slot.syncMode = static_cast<s3g::breakbeat::SyncMode>(
                std::min<uint8_t>(source.syncMode,
                    static_cast<uint8_t>(
                        s3g::breakbeat::SyncMode::Host)));
        }
    }
    if (!bank->valid()
        || !publishBank(instance, std::move(bank), false)) return false;
    instance.variations = {};
    instance.selectedVariations = {};
    if (restoredMutations) {
        instance.mutationSeed = mutation->nextSeed == 0u
            ? 1u : mutation->nextSeed;
        instance.mutationDepth = std::isfinite(mutation->depth)
            ? std::clamp(mutation->depth, 0.0f, 1.0f) : 0.25f;
        instance.mutationTargets
            = s3g::breakbeat::kDefaultMutationTargets;
        if (mutation->version == kLegacyMutationStateVersion) {
            instance.structuralMutationUses
                = s3g::breakbeat::StructuralRearrange
                | s3g::breakbeat::StructuralRepeat
                | s3g::breakbeat::StructuralAuxBus
                | s3g::breakbeat::StructuralMixerFx;
            if ((mutation->targets
                    & s3g::breakbeat::MutationPitch) != 0u)
                instance.structuralMutationUses
                    |= s3g::breakbeat::StructuralPitch;
            if ((mutation->targets
                    & s3g::breakbeat::MutationReverse) != 0u)
                instance.structuralMutationUses
                    |= s3g::breakbeat::StructuralReverse;
        } else if (mutation->version == kStructuralMutationStateVersion) {
            instance.structuralMutationUses = (mutation->targets
                    & s3g::breakbeat::kAllStructuralMutationUses)
                | s3g::breakbeat::StructuralMixerFx;
        } else {
            instance.structuralMutationUses = mutation->targets
                & s3g::breakbeat::kAllStructuralMutationUses;
        }
        for (std::size_t slotIndex = 0u;
             slotIndex < instance.variations.size(); ++slotIndex) {
            const auto& slot = instance.controlBank->slots[slotIndex];
            instance.selectedVariations[slotIndex] = std::min<uint8_t>(
                mutation->selectedVariations[slotIndex],
                static_cast<uint8_t>(
                    s3g::breakbeat::kMutationVariationCount - 1u));
            for (std::size_t variationIndex = 0u;
                 variationIndex < s3g::breakbeat::kMutationVariationCount;
                 ++variationIndex) {
                const auto& candidate
                    = mutation->variations[slotIndex][variationIndex];
                if (!candidate.occupied) continue;
                if (slot.asset && candidate.validFor(*slot.asset))
                    instance.variations[slotIndex][variationIndex]
                        = candidate;
            }
        }
    } else {
        instance.mutationSeed = 1u;
        instance.mutationDepth = 0.25f;
        instance.mutationTargets
            = s3g::breakbeat::kDefaultMutationTargets;
        instance.structuralMutationUses
            = s3g::breakbeat::kDefaultStructuralMutationUses;
        for (std::size_t index = 0u;
             index < instance.controlBank->slots.size(); ++index)
            resetSlotVariations(instance, static_cast<uint32_t>(index),
                &instance.controlBank->slots[index]);
    }
    instance.minimumTransientSliceMilliseconds = restoredMutations
            && mutation->version >= kMutationStateVersion
        ? std::min<uint32_t>(static_cast<uint32_t>(
                mutation->reserved[0u])
                | (static_cast<uint32_t>(mutation->reserved[1u]) << 8u),
            kMaximumMinimumTransientSliceMilliseconds)
        : kDefaultMinimumTransientSliceMilliseconds;
    instance.selectedSlot = std::min<uint32_t>(saved.selectedSlot,
        static_cast<uint32_t>(s3g::breakbeat::kMaximumSampleSlots - 1u));
    setParam(instance, kOutputGainParamId, saved.outputGainDb);
    setParam(instance, kVelocityParamId, saved.velocitySensitivity);
    instance.embedSamplesInState = saved.embedSamples != 0u;
    instance.transientPreRollMicroseconds
        = saved.version >= kPreviousStateVersion
        ? std::min(saved.transientPreRollMicroseconds,
            kMaximumTransientPreRollMicroseconds)
        : 0u;
    instance.status = "PROJECT STATE RESTORED";
    return true;
}

const clap_plugin_state_t state {
    stateSave,
    stateLoad,
};

} // namespace

// The native editor follows below so the platform-neutral CLAP and DSP
// contract above remains directly testable on every platform.

#if defined(__APPLE__)

namespace {

NSRect bankRowRect(uint32_t index)
{
    return NSMakeRect(24.0, 101.0 + static_cast<CGFloat>(index) * 100.0,
        238.0, 92.0);
}

NSRect bankPanelRect()
{
    return NSMakeRect(18.0, 74.0, 250.0, 432.0);
}

NSRect slotChannelRect(uint32_t index)
{
    const NSRect row = bankRowRect(index);
    return NSMakeRect(row.origin.x + 8.0, row.origin.y + 64.0,
        82.0, 19.0);
}

NSRect slotAutomapRect(uint32_t index)
{
    const NSRect row = bankRowRect(index);
    return NSMakeRect(row.origin.x + 98.0, row.origin.y + 64.0,
        132.0, 19.0);
}

NSRect overviewWaveformRect(uint32_t index)
{
    return NSMakeRect(292.0, 74.0 + static_cast<CGFloat>(index) * 112.0,
        770.0, 104.0);
}

NSRect waveformRect()
{
    return NSMakeRect(294.0, 101.0, 756.0, 245.0);
}

NSRect waveformNavigatorRect()
{
    return NSMakeRect(294.0, 350.0, 756.0, 8.0);
}

NSRect sampleToolboxRect()
{
    return NSMakeRect(282.0, 74.0, 780.0, 328.0);
}

NSRect sampleLoadButtonRect()
{
    return NSMakeRect(816.0, 77.0, 66.0, 17.0);
}

NSRect sampleClearButtonRect()
{
    return NSMakeRect(888.0, 77.0, 66.0, 17.0);
}

NSRect sliceMapToolboxRect()
{
    return NSMakeRect(282.0, 414.0, 380.0, 164.0);
}

NSRect playbackToolboxRect()
{
    return NSMakeRect(674.0, 414.0, 388.0, 164.0);
}

enum DetailMenuKind : NSInteger {
    kDetailMenuNone = -1,
    kDetailMenuMidiChannel = 0,
    kDetailMenuSliceMethod,
    kDetailMenuStartNote,
    kDetailMenuTrigger,
    kDetailMenuRetrigger,
    kDetailMenuVoice,
    kDetailMenuPitch,
    kDetailMenuSync,
    kDetailMenuSliceLaunch,
    kDetailMenuSliceChoke,
};

enum DetailNumericControl : NSInteger {
    kDetailNumericNone = -1,
    kDetailNumericPreRoll = 0,
    kDetailNumericMinimumSlice,
    kDetailNumericSourceTempo,
    kDetailNumericLoopCrossfade,
    kDetailNumericGlide,
    kDetailNumericSliceGain,
    kDetailNumericSlicePitch,
    kDetailNumericSlicePan,
    kDetailNumericCount,
};

constexpr NSInteger kDetailNumericFieldTagBase = 4100;

NSRect selectedSliceToolboxRect()
{
    return NSMakeRect(282.0, 590.0, 510.0, 100.0);
}

NSRect timingToolboxRect()
{
    return NSMakeRect(804.0, 590.0, 258.0, 100.0);
}

NSRect processorMenuRect(NSRect panel, CGFloat y)
{
    return NSMakeRect(static_cast<CGFloat>(
            s3g::gui_layout::processorControlX(panel.origin.x)),
        y - 1.0, static_cast<CGFloat>(
            s3g::gui_layout::processorMenuWidth(panel.size.width)), 15.0);
}

NSRect sliceMethodMenuRect()
{
    return processorMenuRect(sliceMapToolboxRect(), 448.0);
}

CGFloat detailNumericY(uint32_t index)
{
    static constexpr CGFloat rows[] {
        474.0, 500.0, 624.0, 650.0, 676.0,
    };
    return rows[std::min<uint32_t>(index,
        static_cast<uint32_t>(std::size(rows) - 1u))];
}

bool isSlicePropertyNumeric(uint32_t index) noexcept
{
    return index >= kDetailNumericSliceGain
        && index <= kDetailNumericSlicePan;
}

uint32_t slicePropertyForNumeric(uint32_t index) noexcept
{
    return index - static_cast<uint32_t>(kDetailNumericSliceGain);
}

CGFloat slicePropertyGroupX(uint32_t property)
{
    return 296.0 + static_cast<CGFloat>(property) * 158.0;
}

constexpr CGFloat kSlicePropertyGroupWidth = 156.0;
constexpr CGFloat kSlicePropertySliderY = 624.0;

NSRect slicePropertyTrackRect(uint32_t property)
{
    return s3g::clap_gui::mixerStripSliderTrackRect(
        slicePropertyGroupX(property), kSlicePropertyGroupWidth,
        kSlicePropertySliderY);
}

NSRect slicePropertyHitRect(uint32_t property)
{
    return s3g::clap_gui::mixerStripSliderHitRect(
        slicePropertyGroupX(property), kSlicePropertyGroupWidth,
        kSlicePropertySliderY);
}

NSRect slicePropertyFieldRect(uint32_t property)
{
    constexpr CGFloat valueWidth = 40.0;
    return NSMakeRect(slicePropertyGroupX(property)
            + kSlicePropertyGroupWidth
            - static_cast<CGFloat>(
                s3g::gui_layout::kStandardMetrics.panelRightInset)
            - valueWidth,
        kSlicePropertySliderY - 2.0, valueWidth, 16.0);
}

NSRect detailNumericPanel(uint32_t index)
{
    if (isSlicePropertyNumeric(index)) return selectedSliceToolboxRect();
    return index < 2u ? sliceMapToolboxRect() : timingToolboxRect();
}

NSRect detailNumericTrackRect(uint32_t index)
{
    if (isSlicePropertyNumeric(index))
        return slicePropertyTrackRect(slicePropertyForNumeric(index));
    const NSRect panel = detailNumericPanel(index);
    return NSMakeRect(static_cast<CGFloat>(
            s3g::gui_layout::processorControlX(NSMinX(panel))),
        detailNumericY(index) + 1.0,
        static_cast<CGFloat>(
            s3g::gui_layout::processorTrackWidth(NSWidth(panel))), 9.0);
}

NSRect detailNumericHitRect(uint32_t index)
{
    if (isSlicePropertyNumeric(index))
        return slicePropertyHitRect(slicePropertyForNumeric(index));
    const NSRect panel = detailNumericPanel(index);
    return NSMakeRect(NSMinX(panel) + static_cast<CGFloat>(
            s3g::gui_layout::kStandardMetrics.hitInset),
        detailNumericY(index) - 8.0,
        NSWidth(panel) - 2.0 * static_cast<CGFloat>(
            s3g::gui_layout::kStandardMetrics.hitInset),
        static_cast<CGFloat>(
            s3g::gui_layout::kStandardMetrics.hitHeight));
}

NSRect detailNumericFieldRect(uint32_t index)
{
    if (isSlicePropertyNumeric(index))
        return slicePropertyFieldRect(slicePropertyForNumeric(index));
    const NSRect panel = detailNumericPanel(index);
    return NSMakeRect(static_cast<CGFloat>(
            s3g::gui_layout::processorValueX(
                NSMinX(panel), NSWidth(panel))),
        detailNumericY(index) - 2.0,
        static_cast<CGFloat>(
            s3g::gui_layout::kStandardMetrics.processorValueWidth), 16.0);
}

NSRect startNoteMenuRect()
{
    return processorMenuRect(sliceMapToolboxRect(), 526.0);
}

NSRect automapButtonRect()
{
    const NSRect panel = sliceMapToolboxRect();
    return NSMakeRect(NSMaxX(panel) - 88.0, panel.origin.y + 3.0,
        76.0, 17.0);
}

NSRect sliceActionButtonRect()
{
    const NSRect automap = automapButtonRect();
    return NSMakeRect(NSMinX(automap) - 62.0, NSMinY(automap),
        56.0, NSHeight(automap));
}

NSRect playbackMenuRect(uint32_t index)
{
    return processorMenuRect(playbackToolboxRect(),
        448.0 + static_cast<CGFloat>(index) * 26.0);
}

NSRect sliceLaunchMenuRect()
{
    return NSMakeRect(360.0, 650.0, 154.0, 15.0);
}

NSRect sliceReverseButtonRect()
{
    return NSMakeRect(532.0, 648.0, 76.0, 19.0);
}

NSRect sliceChokeMenuRect()
{
    return NSMakeRect(688.0, 650.0, 84.0, 15.0);
}

NSString* const kSlicerMethodItems[] = {
    @"EQUAL 4", @"EQUAL 8", @"EQUAL 16", @"EQUAL 32", @"TRANSIENT",
};

NSString* const kSlicerTriggerItems[] = {
    @"AUTO", @"GATE", @"ONE SHOT", @"TOGGLE",
};

NSString* const kSlicerRetriggerItems[] = {
    @"LAYER", @"RESTART", @"IGNORE",
};

NSString* const kSlicerVoiceItems[] = { @"POLY", @"MONO", @"LEGATO" };

NSString* const kSlicerPitchItems[] = {
    @"RATE", @"STRETCH", @"RATE BELOW / STRETCH ABOVE",
};

NSString* const kSlicerSyncItems[] = { @"FREE", @"HOST" };

NSString* const kSlicerLaunchItems[] = {
    @"ONE SHOT", @"GATE", @"THRU", @"LOOP", @"PING PONG",
};

NSRect detailMenuControlRect(NSInteger kind, uint32_t slotIndex)
{
    switch (kind) {
    case kDetailMenuMidiChannel: return slotChannelRect(slotIndex);
    case kDetailMenuSliceMethod: return sliceMethodMenuRect();
    case kDetailMenuStartNote: return startNoteMenuRect();
    case kDetailMenuTrigger: return playbackMenuRect(0u);
    case kDetailMenuRetrigger: return playbackMenuRect(1u);
    case kDetailMenuVoice: return playbackMenuRect(2u);
    case kDetailMenuPitch: return playbackMenuRect(3u);
    case kDetailMenuSync: return playbackMenuRect(4u);
    case kDetailMenuSliceLaunch: return sliceLaunchMenuRect();
    case kDetailMenuSliceChoke: return sliceChokeMenuRect();
    default: return NSZeroRect;
    }
}

uint32_t detailMenuColumns(NSInteger kind)
{
    if (kind == kDetailMenuStartNote) return 8u;
    if (kind == kDetailMenuMidiChannel
        || kind == kDetailMenuSliceChoke) return 2u;
    return 1u;
}

NSRect detailMenuDropdownRect(NSInteger kind, uint32_t itemCount,
    uint32_t slotIndex)
{
    constexpr CGFloat itemHeight = 20.0;
    const uint32_t columns = detailMenuColumns(kind);
    const uint32_t rows = s3g::clap_gui::multiColumnMenuRows(
        itemCount, columns);
    const NSRect control = detailMenuControlRect(kind, slotIndex);
    CGFloat width = NSWidth(control);
    if (kind == kDetailMenuStartNote) width = 640.0;
    else if (kind == kDetailMenuMidiChannel) width = 300.0;
    else if (kind == kDetailMenuSliceChoke) width = 240.0;
    const CGFloat height = itemHeight * static_cast<CGFloat>(rows);
    CGFloat x = NSMinX(control);
    if (kind == kDetailMenuStartNote)
        x = std::clamp<CGFloat>(NSMaxX(control) - width, 18.0,
            static_cast<CGFloat>(kGuiWidth) - width - 18.0);
    else
        x = std::clamp<CGFloat>(x, 18.0,
            static_cast<CGFloat>(kGuiWidth) - width - 18.0);
    CGFloat y = NSMaxY(control) + 1.0;
    if (y + height > static_cast<CGFloat>(kGuiHeight) - 24.0)
        y = std::max<CGFloat>(42.0, NSMinY(control) - height - 1.0);
    return NSMakeRect(x, y, width, height);
}

NSRect editorTabRect(uint32_t index)
{
    constexpr CGFloat widths[] { 82.0, 96.0, 68.0, 72.0 };
    CGFloat x = 292.0;
    for (uint32_t i = 0u; i < index; ++i) x += widths[i] + 6.0;
    return NSMakeRect(x, 45.0, widths[index], 18.0);
}

NSRect mutateWaveformRect()
{
    return NSMakeRect(292.0, 74.0, 770.0, 164.0);
}

NSRect mutateSlotRect(uint32_t index)
{
    return NSMakeRect(292.0 + static_cast<CGFloat>(index) * 194.5,
        250.0, 184.5, 64.0);
}

NSRect mutateActionButtonRect(uint32_t index)
{
    constexpr CGFloat widths[] { 220.0, 156.0, 112.0, 112.0 };
    CGFloat x = 292.0;
    for (uint32_t i = 0u; i < index; ++i) x += widths[i] + 8.0;
    return NSMakeRect(x, 326.0, widths[index], 24.0);
}

NSRect mutateUsesPanelRect()
{
    return NSMakeRect(292.0, 362.0, 770.0, 94.0);
}

NSRect mutateUseRect(uint32_t index)
{
    return NSMakeRect(310.0 + static_cast<CGFloat>(index) * 122.0,
        396.0, 112.0, 22.0);
}

NSRect embedAudioButtonRect()
{
    return NSMakeRect(858.0, 45.0, 204.0, 18.0);
}

NSRect killAllButtonRect()
{
    return NSMakeRect(774.0, 45.0, 78.0, 18.0);
}

NSRect keyboardRect()
{
    return NSMakeRect(282.0, 704.0, 780.0, 50.0);
}

NSRect envelopePanelRect()
{
    return NSMakeRect(18.0, 518.0, 250.0, 236.0);
}

CGFloat envelopeSliderY(uint32_t index)
{
    return 576.0 + static_cast<CGFloat>(index) * 42.0;
}

NSRect envelopeSliderHitRect(uint32_t index)
{
    const NSRect panel = envelopePanelRect();
    return s3g::clap_gui::mixerStripSliderHitRect(
        NSMinX(panel), NSWidth(panel), envelopeSliderY(index));
}

NSRect envelopeSliderTrackRect(uint32_t index)
{
    const NSRect panel = envelopePanelRect();
    return s3g::clap_gui::mixerStripSliderTrackRect(
        NSMinX(panel), NSWidth(panel), envelopeSliderY(index));
}

NSRect mixerStripRect(uint32_t index)
{
    return NSMakeRect(18.0 + static_cast<CGFloat>(index) * 166.0,
        74.0, 160.0, 674.0);
}

NSRect mixerMainRect()
{
    return NSMakeRect(690.0, 74.0, 372.0, 130.0);
}

NSRect mixerAuxBusRect()
{
    return NSMakeRect(690.0, 216.0, 372.0, 532.0);
}

NSRect mixerInsertTypeRect(uint32_t type)
{
    const NSRect panel = mixerAuxBusRect();
    const uint32_t column = type % 5u;
    const uint32_t row = type / 5u;
    return NSMakeRect(NSMinX(panel) + 16.0
            + static_cast<CGFloat>(column) * 68.0,
        NSMinY(panel) + 36.0 + static_cast<CGFloat>(row) * 32.0,
        64.0, 25.0);
}

NSRect mixerInsertBypassRect()
{
    const NSRect panel = mixerAuxBusRect();
    return NSMakeRect(NSMinX(panel) + 16.0, NSMinY(panel) + 104.0,
        104.0, 25.0);
}

NSRect mixerInsertSwapRect()
{
    const NSRect panel = mixerAuxBusRect();
    return NSMakeRect(NSMinX(panel) + 132.0, NSMinY(panel) + 104.0,
        104.0, 25.0);
}

NSRect mixerInsertCloseRect()
{
    const NSRect panel = mixerAuxBusRect();
    return NSMakeRect(NSMinX(panel) + 248.0, NSMinY(panel) + 104.0,
        108.0, 25.0);
}

NSRect mixerInsertOptionRect()
{
    const NSRect panel = mixerAuxBusRect();
    return NSMakeRect(NSMinX(panel) + 16.0, NSMinY(panel) + 140.0,
        NSWidth(panel) - 32.0, 25.0);
}

CGFloat mixerInsertParameterY(uint32_t parameter)
{
    return NSMinY(mixerAuxBusRect()) + 204.0
        + static_cast<CGFloat>(parameter) * 64.0;
}

NSRect mixerInsertParameterHitRect(uint32_t parameter)
{
    const NSRect panel = mixerAuxBusRect();
    return NSMakeRect(NSMinX(panel) + 12.0,
        mixerInsertParameterY(parameter) + 17.0,
        NSWidth(panel) - 24.0, 28.0);
}

NSRect mixerInsertParameterTrackRect(uint32_t parameter)
{
    const NSRect panel = mixerAuxBusRect();
    return NSMakeRect(NSMinX(panel) + 16.0,
        mixerInsertParameterY(parameter) + 27.0,
        NSWidth(panel) - 32.0, 9.0);
}

void drawMixerInsertParameter(NSString* name, NSString* value,
    CGFloat normalized, uint32_t parameter, NSDictionary* labelAttrs,
    NSDictionary* valueAttrs, const s3g::clap_gui::Style& style)
{
    const NSRect panel = mixerAuxBusRect();
    const CGFloat rowY = mixerInsertParameterY(parameter);
    [name drawAtPoint:NSMakePoint(NSMinX(panel) + 16.0, rowY)
        withAttributes:labelAttrs];
    s3g::clap_gui::drawBoundedRightText(value,
        NSMakeRect(NSMinX(panel) + 150.0, rowY - 1.0,
            NSWidth(panel) - 166.0, 17.0), valueAttrs);

    const NSRect track = mixerInsertParameterTrackRect(parameter);
    [style.strip setFill];
    NSRectFill(track);
    [style.grid setStroke];
    NSFrameRect(track);
    normalized = std::clamp(normalized, static_cast<CGFloat>(0.0),
        static_cast<CGFloat>(1.0));
    NSRect filled = NSInsetRect(track, 1.0, 1.0);
    filled.size.width = std::max<CGFloat>(1.0,
        filled.size.width * normalized);
    [style.fill setFill];
    NSRectFill(filled);
    const CGFloat handleX = std::clamp(
        track.origin.x + track.size.width * normalized - 1.5,
        track.origin.x + 1.0, NSMaxX(track) - 4.0);
    [style.text setFill];
    NSRectFill(NSMakeRect(handleX, track.origin.y - 2.0, 3.0,
        track.size.height + 4.0));
}

NSRect mixerDialRect(uint32_t index, uint32_t dial)
{
    const NSRect strip = mixerStripRect(index);
    return NSMakeRect(NSMinX(strip) + 19.0 + (dial % 2u) * 76.0,
        NSMinY(strip) + 72.0 + (dial / 2u) * 72.0, 50.0, 62.0);
}

NSRect mixerInsertRect(uint32_t index, uint32_t insert)
{
    const NSRect strip = mixerStripRect(index);
    return NSMakeRect(NSMinX(strip) + 84.0,
        NSMinY(strip) + 218.0 + static_cast<CGFloat>(insert) * 32.0,
        66.0, 25.0);
}

CGFloat mixerAuxY(uint32_t index)
{
    return NSMinY(mixerStripRect(index)) + 296.0;
}

NSRect mixerAuxHitRect(uint32_t index)
{
    const NSRect strip = mixerStripRect(index);
    return s3g::clap_gui::mixerStripSliderHitRect(
        NSMinX(strip), NSWidth(strip), mixerAuxY(index));
}

NSRect mixerAuxTrackRect(uint32_t index)
{
    const NSRect strip = mixerStripRect(index);
    return s3g::clap_gui::mixerStripSliderTrackRect(
        NSMinX(strip), NSWidth(strip), mixerAuxY(index));
}

NSRect mixerMuteRect(uint32_t index)
{
    const NSRect strip = mixerStripRect(index);
    return NSMakeRect(NSMinX(strip) + 14.0, NSMinY(strip) + 326.0,
        61.0, 25.0);
}

NSRect mixerSoloRect(uint32_t index)
{
    const NSRect strip = mixerStripRect(index);
    return NSMakeRect(NSMaxX(strip) - 75.0, NSMinY(strip) + 326.0,
        61.0, 25.0);
}

NSRect mixerAuditionRect(uint32_t index)
{
    const NSRect strip = mixerStripRect(index);
    return NSMakeRect(NSMinX(strip) + 14.0, NSMinY(strip) + 360.0,
        NSWidth(strip) - 28.0, 25.0);
}

NSRect mixerFaderRect(uint32_t index)
{
    const NSRect strip = mixerStripRect(index);
    return NSMakeRect(NSMinX(strip) + 24.0, NSMinY(strip) + 425.0,
        60.0, 183.0);
}

NSRect mixerPeakRect(uint32_t index)
{
    const NSRect fader = mixerFaderRect(index);
    return NSMakeRect(NSMinX(mixerStripRect(index)) + 119.0,
        NSMinY(fader), 11.0, NSHeight(fader));
}

CGFloat mixerOutputSliderY()
{
    return NSMinY(mixerMainRect()) + 48.0;
}

NSRect mixerOutputHitRect()
{
    const NSRect panel = mixerMainRect();
    return s3g::clap_gui::mixerStripSliderHitRect(
        NSMinX(panel), NSWidth(panel), mixerOutputSliderY());
}

NSRect mixerOutputTrackRect()
{
    const NSRect panel = mixerMainRect();
    return s3g::clap_gui::mixerStripSliderTrackRect(
        NSMinX(panel), NSWidth(panel), mixerOutputSliderY());
}

NSRect mixerUnityRect()
{
    const NSRect panel = mixerMainRect();
    return NSMakeRect(NSMaxX(panel) - 105.0, NSMinY(panel) + 82.0,
        88.0, 25.0);
}

NSRect mixerBusEnableRect()
{
    const NSRect panel = mixerAuxBusRect();
    return NSMakeRect(NSMinX(panel) + 16.0, NSMinY(panel) + 34.0,
        NSWidth(panel) - 32.0, 25.0);
}

NSRect mixerBusLinkRect()
{
    const NSRect panel = mixerAuxBusRect();
    return NSMakeRect(NSMinX(panel) + 16.0, NSMinY(panel) + 478.0,
        158.0, 25.0);
}

NSRect mixerBusFieldSafeRect()
{
    const NSRect panel = mixerAuxBusRect();
    return NSMakeRect(NSMinX(panel) + 188.0, NSMinY(panel) + 478.0,
        168.0, 25.0);
}

CGFloat mixerBusSliderY(uint32_t row)
{
    return NSMinY(mixerAuxBusRect()) + 92.0
        + static_cast<CGFloat>(row) * 43.0;
}

NSRect mixerBusSliderHitRect(uint32_t row)
{
    const NSRect panel = mixerAuxBusRect();
    return s3g::clap_gui::mixerStripSliderHitRect(
        NSMinX(panel), NSWidth(panel), mixerBusSliderY(row));
}

NSRect mixerBusSliderTrackRect(uint32_t row)
{
    const NSRect panel = mixerAuxBusRect();
    return s3g::clap_gui::mixerStripSliderTrackRect(
        NSMinX(panel), NSWidth(panel), mixerBusSliderY(row));
}

NSString* noteText(uint8_t note)
{
    static NSArray<NSString*>* names = @[
        @"C", @"C#", @"D", @"D#", @"E", @"F",
        @"F#", @"G", @"G#", @"A", @"A#", @"B"
    ];
    return [NSString stringWithFormat:@"%@%d / %u", names[note % 12u],
        static_cast<int>(note / 12u) - 2, static_cast<unsigned>(note)];
}

NSString* shortNoteText(uint8_t note)
{
    static NSArray<NSString*>* names = @[
        @"C", @"C#", @"D", @"D#", @"E", @"F",
        @"F#", @"G", @"G#", @"A", @"A#", @"B"
    ];
    return [NSString stringWithFormat:@"%@%d", names[note % 12u],
        static_cast<int>(note / 12u) - 2];
}

NSString* launchModeText(s3g::breakbeat::LaunchMode mode)
{
    switch (mode) {
    case s3g::breakbeat::LaunchMode::OneShot: return @"ONE SHOT";
    case s3g::breakbeat::LaunchMode::Gate: return @"GATE";
    case s3g::breakbeat::LaunchMode::Thru: return @"THRU";
    case s3g::breakbeat::LaunchMode::Loop: return @"LOOP";
    case s3g::breakbeat::LaunchMode::PingPong: return @"PING PONG";
    }
    return @"ONE SHOT";
}

NSString* triggerModeText(s3g::breakbeat::TriggerMode mode)
{
    switch (mode) {
    case s3g::breakbeat::TriggerMode::Auto: return @"AUTO";
    case s3g::breakbeat::TriggerMode::Gate: return @"GATE";
    case s3g::breakbeat::TriggerMode::OneShot: return @"ONE SHOT";
    case s3g::breakbeat::TriggerMode::Toggle: return @"TOGGLE";
    }
    return @"AUTO";
}

NSString* retriggerModeText(s3g::breakbeat::RetriggerMode mode)
{
    switch (mode) {
    case s3g::breakbeat::RetriggerMode::Layer: return @"LAYER";
    case s3g::breakbeat::RetriggerMode::Restart: return @"RESTART";
    case s3g::breakbeat::RetriggerMode::Ignore: return @"IGNORE";
    }
    return @"RESTART";
}

NSString* voiceModeText(s3g::breakbeat::VoiceMode mode)
{
    switch (mode) {
    case s3g::breakbeat::VoiceMode::Poly: return @"POLY";
    case s3g::breakbeat::VoiceMode::Mono: return @"MONO";
    case s3g::breakbeat::VoiceMode::Legato: return @"LEGATO";
    }
    return @"POLY";
}

NSString* pitchModeText(s3g::breakbeat::PitchMode mode)
{
    switch (mode) {
    case s3g::breakbeat::PitchMode::Rate: return @"RATE";
    case s3g::breakbeat::PitchMode::Stretch: return @"STRETCH";
    case s3g::breakbeat::PitchMode::RateBelowStretchAbove:
        return @"R↓ / S↑";
    }
    return @"RATE";
}

NSString* syncModeText(s3g::breakbeat::SyncMode mode)
{
    return mode == s3g::breakbeat::SyncMode::Host ? @"HOST" : @"FREE";
}

NSString* insertTypeText(InsertType type)
{
    switch (type) {
    case InsertType::Filter: return @"FILTER";
    case InsertType::Degrade: return @"DEGRADE";
    case InsertType::Transient: return @"TRANSIENT";
    case InsertType::Resonator: return @"RESONATOR";
    case InsertType::Erosion: return @"EROSION";
    case InsertType::Shifter: return @"SHIFT";
    case InsertType::Wavefolder: return @"FOLD";
    case InsertType::Repeater: return @"REPEATER";
    case InsertType::TimeMangler: return @"TIME";
    case InsertType::Off: return @"OFF";
    }
    return @"OFF";
}

NSString* insertTypeShortText(InsertType type)
{
    switch (type) {
    case InsertType::Filter: return @"FLT";
    case InsertType::Degrade: return @"DGR";
    case InsertType::Transient: return @"TRN";
    case InsertType::Resonator: return @"RSN";
    case InsertType::Erosion: return @"ERO";
    case InsertType::Shifter: return @"SHF";
    case InsertType::Wavefolder: return @"FLD";
    case InsertType::Repeater: return @"RPT";
    case InsertType::TimeMangler: return @"TIM";
    case InsertType::Off: return @"OFF";
    }
    return @"OFF";
}

NSString* filterModeText(FilterMode mode)
{
    switch (mode) {
    case FilterMode::LowPass: return @"LOW PASS";
    case FilterMode::BandPass: return @"BAND PASS";
    case FilterMode::HighPass: return @"HIGH PASS";
    case FilterMode::Notch: return @"NOTCH";
    }
    return @"LOW PASS";
}

NSString* insertParameterLabel(const InsertSettings& insert,
    uint32_t parameter)
{
    static NSArray<NSString*>* filter = @[
        @"CUTOFF", @"RESONANCE", @"DRIVE", @"MIX"
    ];
    static NSArray<NSString*>* degrade = @[
        @"RATE DIVIDE", @"BIT DEPTH", @"JITTER", @"MIX"
    ];
    static NSArray<NSString*>* transient = @[
        @"ATTACK", @"SUSTAIN", @"GATE", @"MIX"
    ];
    static NSArray<NSString*>* resonator = @[
        @"TUNE", @"FEEDBACK", @"DAMPING", @"AMOUNT"
    ];
    static NSArray<NSString*>* erosion = @[
        @"MOD RATE", @"DEPTH", @"FEEDBACK", @"MIX"
    ];
    static NSArray<NSString*>* shifter = @[
        @"FREQUENCY", @"REGEN", @"COLOR", @"MIX"
    ];
    static NSArray<NSString*>* wavefolder = @[
        @"DRIVE", @"BIAS", @"SHAPE", @"MIX"
    ];
    static NSArray<NSString*>* repeater = @[
        @"BUFFER", @"REPEATS", @"PITCH DECAY", @"MIX"
    ];
    if (parameter >= s3g::breakbeat::kInsertParameterCount) return @"";
    if (insert.type == InsertType::TimeMangler) {
        if (parameter == 0u) return @"WINDOW";
        if (parameter == 1u) return @"PITCH";
        if (parameter == 2u) return insert.variant == 0u ? @"RELEASE"
            : insert.variant == 1u ? @"DECAY" : @"BRAKE";
        return @"MIX";
    }
    switch (insert.type) {
    case InsertType::Filter: return filter[parameter];
    case InsertType::Degrade: return degrade[parameter];
    case InsertType::Transient: return transient[parameter];
    case InsertType::Resonator: return resonator[parameter];
    case InsertType::Erosion: return erosion[parameter];
    case InsertType::Shifter: return shifter[parameter];
    case InsertType::Wavefolder: return wavefolder[parameter];
    case InsertType::Repeater: return repeater[parameter];
    case InsertType::TimeMangler: break;
    case InsertType::Off: return @"UNASSIGNED";
    }
    return @"";
}

NSString* insertParameterValue(const InsertSettings& insert,
    uint32_t parameter)
{
    if (parameter >= insert.values.size()) return @"";
    const float value = insert.values[parameter];
    if ((insert.type == InsertType::Repeater
            || insert.type == InsertType::TimeMangler)
        && parameter == 0u) {
        static NSArray<NSString*>* windows = @[
            @"8 ms", @"16 ms", @"32 ms", @"64 ms",
            @"125 ms", @"250 ms", @"500 ms", @"1000 ms"
        ];
        const NSUInteger index = static_cast<NSUInteger>(std::clamp(
            std::lround(value * 7.0f), 0l, 7l));
        return windows[index];
    }
    if (insert.type == InsertType::Repeater && parameter == 1u) {
        const uint32_t repeats = 1u + static_cast<uint32_t>(std::lround(
            value * 15.0f));
        return [NSString stringWithFormat:@"× %u", repeats];
    }
    if (insert.type == InsertType::Repeater && parameter == 2u) {
        return [NSString stringWithFormat:@"-%.1f st / repeat",
            value * 12.0f];
    }
    if (insert.type == InsertType::TimeMangler && parameter == 1u) {
        const float semitones = (value * 2.0f - 1.0f) * 24.0f;
        return [NSString stringWithFormat:@"%+.1f st", semitones];
    }
    if (insert.type == InsertType::TimeMangler && parameter == 2u) {
        if (insert.variant == 0u) {
            const float milliseconds = 2.0f * std::pow(100.0f, value);
            return [NSString stringWithFormat:@"%.0f ms", milliseconds];
        }
        if (insert.variant == 1u) {
            const float seconds = 0.125f * std::pow(128.0f, value);
            return seconds < 1.0f
                ? [NSString stringWithFormat:@"%.0f ms", seconds * 1000.0f]
                : [NSString stringWithFormat:@"%.2f s", seconds];
        }
        return [NSString stringWithFormat:@"%.0f%%", value * 100.0f];
    }
    if (insert.type == InsertType::Filter && parameter == 0u) {
        const float frequency = 30.0f * std::pow(20000.0f / 30.0f, value);
        return frequency >= 1000.0f
            ? [NSString stringWithFormat:@"%.2f kHz", frequency / 1000.0f]
            : [NSString stringWithFormat:@"%.0f Hz", frequency];
    }
    if (insert.type == InsertType::Filter && parameter == 1u) {
        const float q = 0.5f + 15.5f * value * value;
        return [NSString stringWithFormat:@"Q %.2f", q];
    }
    if (insert.type == InsertType::Degrade && parameter == 0u) {
        const uint32_t period = 1u + static_cast<uint32_t>(std::lround(
            value * value * 95.0f));
        return [NSString stringWithFormat:@"÷ %u", period];
    }
    if (insert.type == InsertType::Degrade && parameter == 1u) {
        const uint32_t bits = 4u + static_cast<uint32_t>(std::lround(
            value * 12.0f));
        return [NSString stringWithFormat:@"%u BIT", bits];
    }
    if (insert.type == InsertType::Transient
        && (parameter == 0u || parameter == 1u))
        return [NSString stringWithFormat:@"%+.0f%%",
            (value * 2.0f - 1.0f) * 100.0f];
    if (insert.type == InsertType::Transient && parameter == 2u) {
        if (value <= 0.002f) return @"OFF";
        return [NSString stringWithFormat:@"%+.1f dB",
            -72.0f + value * 48.0f];
    }
    if (insert.type == InsertType::Resonator && parameter == 0u) {
        const float frequency = 40.0f * std::pow(100.0f, value);
        return frequency >= 1000.0f
            ? [NSString stringWithFormat:@"%.2f kHz", frequency / 1000.0f]
            : [NSString stringWithFormat:@"%.0f Hz", frequency];
    }
    if (insert.type == InsertType::Erosion && parameter == 0u) {
        const float frequency = 10.0f * std::pow(1600.0f, value);
        return frequency >= 1000.0f
            ? [NSString stringWithFormat:@"%.2f kHz", frequency / 1000.0f]
            : [NSString stringWithFormat:@"%.0f Hz", frequency];
    }
    if (insert.type == InsertType::Shifter && parameter == 0u) {
        float frequency = 0.0f;
        if (insert.variant == 0u) {
            const float bipolar = value * 2.0f - 1.0f;
            frequency = std::copysign(bipolar * bipolar * 4000.0f,
                bipolar);
            return [NSString stringWithFormat:@"%+.0f Hz", frequency];
        }
        frequency = 20.0f * std::pow(1000.0f, value);
        return frequency >= 1000.0f
            ? [NSString stringWithFormat:@"%.2f kHz", frequency / 1000.0f]
            : [NSString stringWithFormat:@"%.0f Hz", frequency];
    }
    if (insert.type == InsertType::Wavefolder && parameter == 0u) {
        const float drive = 1.0f + value * value * 31.0f;
        return [NSString stringWithFormat:@"%.1f×", drive];
    }
    if (insert.type == InsertType::Wavefolder && parameter == 1u)
        return [NSString stringWithFormat:@"%+.0f%%",
            (value * 2.0f - 1.0f) * 100.0f];
    if (insert.type == InsertType::Filter && parameter == 2u)
        return [NSString stringWithFormat:@"%.0f%%", value * 100.0f];
    return [NSString stringWithFormat:@"%.0f%%", value * 100.0f];
}

NSString* insertSafetyText(InsertType type)
{
    switch (type) {
    case InsertType::Filter:
        return @"SHARED COEFFICIENTS  //  DRIVE ADDS NONLINEAR COLOR";
    case InsertType::Degrade:
        return @"LOCKED HOLD CLOCK  //  NONLINEAR COLOR PROCESS";
    case InsertType::Transient:
        return @"ONE LINKED DETECTOR CONTROLS EVERY SOURCE CHANNEL";
    case InsertType::Resonator:
        return @"SHARED TUNE AND CLOCK  //  DISCRETE CHANNEL STATE";
    case InsertType::Erosion:
        return @"SHARED MOD CLOCK  //  DISCRETE MULTICHANNEL DELAYS";
    case InsertType::Shifter:
        return @"REGEN GOVERNOR  //  PERIODIC SQUASH + SMOOTH RECOVERY";
    case InsertType::Wavefolder:
        return @"4× SUBSTEP ANTIALIASING  //  NONLINEAR COLOR";
    case InsertType::Repeater:
        return @"TRANSIENT CAPTURE  //  ONE READ/WRITE CLOCK FOR ALL LANES";
    case InsertType::TimeMangler:
        return @"TRANSIENT CAPTURE  //  SHARED FRACTIONAL READ POSITION";
    case InsertType::Off:
        return @"SELECT A DEVICE FOR THIS POST-PLAYBACK INSERT";
    }
    return @"";
}

NSString* slotFilename(const Plugin& instance, uint32_t slot)
{
    if (slot >= instance.samplePaths.size()
        || instance.samplePaths[slot].empty()) return @"EMPTY";
    NSString* path = [NSString stringWithUTF8String:
        instance.samplePaths[slot].c_str()];
    return [path lastPathComponent];
}

NSFont* slicerFont(CGFloat size, NSFontWeight weight = NSFontWeightRegular)
{
    const CGFloat readable = std::round(size * 1.08 * 2.0) / 2.0;
    NSString* name = weight >= NSFontWeightSemibold
        ? @"IBM Plex Mono SemiBold"
        : weight >= NSFontWeightMedium ? @"IBM Plex Mono Medium"
                                       : @"IBM Plex Mono";
    NSFont* font = [NSFont fontWithName:name size:readable];
    return font ? font : [NSFont monospacedSystemFontOfSize:readable
        weight:weight];
}

NSDictionary* slicerTextAttrs(NSColor* color, CGFloat size = 10.0,
    NSFontWeight weight = NSFontWeightRegular)
{
    return @{ NSForegroundColorAttributeName: color,
        NSFontAttributeName: slicerFont(size, weight) };
}

NSDictionary* slicerSoftLabelAttrs()
{
    return slicerTextAttrs(s3g::clap_gui::color(0xa8a8a8), 10.0);
}

void drawButton(NSRect rect, NSString* label, bool active = false)
{
    [s3g::clap_gui::color(active ? 0x2b3730 : 0x303030) setFill];
    NSRectFill(rect);
    [s3g::clap_gui::color(active ? 0x526459 : 0x565656) setStroke];
    NSFrameRect(rect);
    NSDictionary* attrs = slicerTextAttrs(
        s3g::clap_gui::color(active ? 0xb8c0bb : 0xb8b8b8), 10.5);
    const NSSize size = [label sizeWithAttributes:attrs];
    [label drawAtPoint:NSMakePoint(
        rect.origin.x + (rect.size.width - size.width) * 0.5,
        rect.origin.y + (rect.size.height - size.height) * 0.5 - 1.0)
        withAttributes:attrs];
}

void drawCompactMenu(NSRect rect, NSString* text, bool active,
    NSDictionary* attrs, const s3g::clap_gui::Style& style)
{
    [style.strip setFill];
    NSRectFill(rect);
    [(active ? style.accent : style.grid) setStroke];
    NSFrameRect(rect);
    [(active ? style.fill : style.dim) setFill];
    NSRectFill(NSMakeRect(rect.origin.x + 1.0, rect.origin.y + 1.0,
        2.0, rect.size.height - 2.0));
    NSString* shown = s3g::clap_gui::menuDisplayText(text,
        std::max<CGFloat>(0.0, rect.size.width - 26.0), attrs);
    const NSSize size = [shown sizeWithAttributes:attrs];
    [shown drawAtPoint:NSMakePoint(rect.origin.x + 7.0,
        rect.origin.y + (rect.size.height - size.height) * 0.5 - 0.5)
        withAttributes:attrs];
    [@"v" drawAtPoint:NSMakePoint(NSMaxX(rect) - 12.0,
        rect.origin.y + 1.0) withAttributes:attrs];
}

void drawLockedControlBar(NSRect rect, NSString* label)
{
    [s3g::clap_gui::color(0x181818) setFill];
    NSRectFill(rect);
    [s3g::clap_gui::color(0x3c3c3c) setStroke];
    NSFrameRect(rect);
    NSDictionary* attrs = slicerTextAttrs(
        s3g::clap_gui::color(0x777777), 10.5, NSFontWeightMedium);
    const NSSize size = [label sizeWithAttributes:attrs];
    [label drawAtPoint:NSMakePoint(
        rect.origin.x + (rect.size.width - size.width) * 0.5,
        rect.origin.y + (rect.size.height - size.height) * 0.5 - 1.0)
        withAttributes:attrs];
}

void drawCentered(NSRect rect, NSString* label, NSDictionary* attrs)
{
    const NSSize size = [label sizeWithAttributes:attrs];
    [label drawAtPoint:NSMakePoint(
        rect.origin.x + (rect.size.width - size.width) * 0.5,
        rect.origin.y + (rect.size.height - size.height) * 0.5)
        withAttributes:attrs];
}

void drawCenteredTruncatedFilename(NSRect rect, NSString* filename,
    NSDictionary* attrs)
{
    if (!filename) return;
    NSMutableParagraphStyle* paragraph = [[NSMutableParagraphStyle alloc]
        init];
    [paragraph setAlignment:NSTextAlignmentCenter];
    // Retain both the recognizable beginning and extension/end of a long
    // sample name instead of allowing it to spill into adjacent mixer lanes.
    [paragraph setLineBreakMode:NSLineBreakByTruncatingMiddle];
    NSMutableDictionary* bounded = [NSMutableDictionary
        dictionaryWithDictionary:attrs];
    [bounded setObject:paragraph forKey:NSParagraphStyleAttributeName];
    const CGFloat textHeight = [filename sizeWithAttributes:attrs].height;
    rect.origin.y += std::max<CGFloat>(0.0,
        (rect.size.height - textHeight) * 0.5);
    rect.size.height = textHeight + 2.0;
    [filename drawInRect:rect withAttributes:bounded];
}

double gainDb(float gain)
{
    return gain <= 0.001f ? -60.0
        : 20.0 * std::log10(static_cast<double>(gain));
}

} // namespace

@interface S3GBreakbeatSlicerView : NSView <NSDraggingDestination> {
    Plugin* _instance;
    NSTimer* _timer;
    NSInteger _selectedSlice;
    NSInteger _dragMarker;
    NSInteger _dragLoopHandle;
    std::shared_ptr<BankSnapshot> _dragBank;
    CGFloat _zoom;
    uint32_t _visibleStart;
    BOOL _dragViewport;
    CGFloat _viewportGrabOffset;
    double _horizontalPanRemainder;
    uint32_t _page;
    NSInteger _sliceModeSelection;
    NSInteger _envelopeDragIndex;
    NSInteger _mixerDragKind;
    uint32_t _mixerDragSlot;
    NSPoint _mixerDragStartPoint;
    double _mixerDragStartValue;
    NSInteger _insertEditorSlot;
    uint32_t _insertEditorIndex;
    NSTrackingArea* _trackingArea;
    NSInteger _detailMenuKind;
    NSInteger _detailMenuHover;
    uint32_t _detailMenuSlot;
    NSInteger _detailNumericDragIndex;
    NSTextField* _detailNumericFields[kDetailNumericCount];
}
- (instancetype)initWithPlugin:(Plugin*)instance;
- (void)startTimer;
- (void)stopTimer;
- (BOOL)loadDocumentationBreaks;
- (void)setDocumentationPage:(NSUInteger)page;
- (BOOL)runDocumentationMutationFill;
- (void)exportCurrentBreak;
- (void)applySelectedSliceMethod;
- (void)makeEqual:(std::size_t)count;
- (void)makeTransient;
- (void)closeDetailMenu;
- (uint32_t)detailMenuItemCount;
- (NSRect)detailMenuDropdownRect;
- (NSInteger)detailMenuHitAtPoint:(NSPoint)point;
- (void)applyDetailMenuSelection:(NSInteger)selection;
- (void)updateDetailNumericFields;
- (void)updateDetailNumericControl:(uint32_t)index point:(NSPoint)point;
- (void)detailNumericFieldChanged:(id)sender;
@end

@implementation S3GBreakbeatSlicerView

- (instancetype)initWithPlugin:(Plugin*)instance
{
    self = [super initWithFrame:NSMakeRect(0.0, 0.0, kGuiWidth, kGuiHeight)];
    if (self) {
        _instance = instance;
        _selectedSlice = 0;
        _dragMarker = -1;
        _dragLoopHandle = -1;
        _zoom = 1.0;
        _visibleStart = 0u;
        _dragViewport = NO;
        _viewportGrabOffset = 0.0;
        _horizontalPanRemainder = 0.0;
        _page = 0u;
        _sliceModeSelection = 4;
        _envelopeDragIndex = -1;
        _mixerDragKind = 0;
        _mixerDragSlot = 0u;
        _mixerDragStartPoint = NSZeroPoint;
        _mixerDragStartValue = 0.0;
        _insertEditorSlot = -1;
        _insertEditorIndex = 0u;
        _trackingArea = nil;
        _detailMenuKind = kDetailMenuNone;
        _detailMenuHover = -1;
        _detailMenuSlot = 0u;
        _detailNumericDragIndex = kDetailNumericNone;
        [self setWantsLayer:YES];
        [self registerForDraggedTypes:@[ NSPasteboardTypeFileURL ]];
        for (NSInteger index = 0; index < kDetailNumericCount; ++index) {
            NSTextField* field = [[NSTextField alloc]
                initWithFrame:NSZeroRect];
            [field setTag:kDetailNumericFieldTagBase + index];
            [field setTarget:self];
            [field setAction:@selector(detailNumericFieldChanged:)];
            [field setDelegate:(id<NSTextFieldDelegate>)self];
            [field setFormatter:nil];
            s3g::clap_gui::styleNumberTextField(
                field, 10.0, NSTextAlignmentRight);
            [field setHidden:YES];
            [self addSubview:field];
            _detailNumericFields[index] = field;
        }
    }
    return self;
}

- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }

- (void)updateTrackingAreas
{
    if (_trackingArea) [self removeTrackingArea:_trackingArea];
    _trackingArea = [[NSTrackingArea alloc] initWithRect:[self bounds]
        options:NSTrackingMouseMoved | NSTrackingMouseEnteredAndExited
            | NSTrackingActiveInKeyWindow
        owner:self userInfo:nil];
    [self addTrackingArea:_trackingArea];
    [super updateTrackingAreas];
}

- (void)viewDidMoveToWindow
{
    [super viewDidMoveToWindow];
    [[self window] setAcceptsMouseMovedEvents:YES];
}

- (void)closeDetailMenu
{
    _detailMenuKind = kDetailMenuNone;
    _detailMenuHover = -1;
}

- (uint32_t)detailMenuItemCount
{
    switch (_detailMenuKind) {
    case kDetailMenuMidiChannel: return 17u;
    case kDetailMenuSliceMethod: return 5u;
    case kDetailMenuStartNote: {
        const auto* slot = [self selectedSampleSlot];
        if (!slot) return 0u;
        const uint32_t maximum = slot->sliceCount == 0u ? 127u
            : 128u - slot->sliceCount;
        return maximum + 1u;
    }
    case kDetailMenuTrigger: return 4u;
    case kDetailMenuRetrigger:
    case kDetailMenuVoice:
    case kDetailMenuPitch: return 3u;
    case kDetailMenuSync: return 2u;
    case kDetailMenuSliceLaunch: return 5u;
    case kDetailMenuSliceChoke: return 17u;
    default: return 0u;
    }
}

- (NSRect)detailMenuDropdownRect
{
    return detailMenuDropdownRect(_detailMenuKind,
        [self detailMenuItemCount], _detailMenuSlot);
}

- (NSInteger)detailMenuHitAtPoint:(NSPoint)point
{
    const uint32_t count = [self detailMenuItemCount];
    if (detailMenuColumns(_detailMenuKind) > 1u) {
        return s3g::clap_gui::multiColumnDropdownHitIndex(point,
            [self detailMenuDropdownRect], 20.0, count,
            detailMenuColumns(_detailMenuKind));
    }
    return s3g::clap_gui::dropdownHitIndex(point,
        [self detailMenuDropdownRect], 20.0, count);
}

- (void)applyDetailMenuSelection:(NSInteger)selection
{
    if (selection < 0) return;
    if (_detailMenuKind == kDetailMenuMidiChannel) {
        if (selection <= 16 && setSlotMidiChannel(*_instance,
                _detailMenuSlot, static_cast<uint8_t>(selection))) {
            _instance->status = selection == 0
                ? "BREAK MIDI SET TO OMNI" : "BREAK MIDI CHANNEL UPDATED";
        }
    } else if (_detailMenuKind == kDetailMenuSliceMethod) {
        if (selection > 4) return;
        _sliceModeSelection = selection;
        _instance->status = "SLICE METHOD SELECTED - PRESS SLICE";
    } else if (_detailMenuKind == kDetailMenuStartNote) {
        if (selection <= 127 && setSlotRootNote(*_instance,
                [self selectedSlot], static_cast<uint8_t>(selection))) {
            _instance->status = "START NOTE UPDATED - PRESS AUTO MAP";
        }
    } else if (_detailMenuKind >= kDetailMenuTrigger
        && _detailMenuKind <= kDetailMenuSync) {
        auto bank = editableBank(*_instance);
        auto& slot = bank->slots[[self selectedSlot]];
        const char* status = "SLICE PLAYBACK UPDATED";
        if (_detailMenuKind == kDetailMenuTrigger && selection <= 3) {
            slot.triggerMode = static_cast<s3g::breakbeat::TriggerMode>(
                selection);
            status = "MAPPED SLICE TRIGGER UPDATED";
        } else if (_detailMenuKind == kDetailMenuRetrigger
            && selection <= 2) {
            slot.retriggerMode = static_cast<
                s3g::breakbeat::RetriggerMode>(selection);
            status = "MAPPED SLICE RETRIGGER UPDATED";
        } else if (_detailMenuKind == kDetailMenuVoice
            && selection <= 2) {
            slot.voiceMode = static_cast<s3g::breakbeat::VoiceMode>(
                selection);
            status = "MAPPED SLICE VOICE MODE UPDATED";
        } else if (_detailMenuKind == kDetailMenuPitch
            && selection <= 2) {
            slot.pitchMode = static_cast<s3g::breakbeat::PitchMode>(
                selection);
            status = "MAPPED SLICE PITCH MODE UPDATED";
        } else if (_detailMenuKind == kDetailMenuSync
            && selection <= 1) {
            slot.syncMode = static_cast<s3g::breakbeat::SyncMode>(selection);
            status = "MAPPED SLICE TEMPO SYNC UPDATED";
        } else {
            return;
        }
        if (publishBank(*_instance, std::move(bank)))
            _instance->status = status;
    } else if (_detailMenuKind == kDetailMenuSliceLaunch
        || _detailMenuKind == kDetailMenuSliceChoke) {
        auto bank = editableBank(*_instance);
        auto& slot = bank->slots[[self selectedSlot]];
        if (!slotHasCompleteMap(slot) || slot.sliceCount == 0u) return;
        const std::size_t selected = static_cast<std::size_t>(
            std::clamp<NSInteger>(_selectedSlice, 0,
                static_cast<NSInteger>(slot.sliceCount) - 1));
        if (_detailMenuKind == kDetailMenuSliceLaunch && selection <= 4) {
            slot.slices[selected].launchMode
                = static_cast<s3g::breakbeat::LaunchMode>(selection);
            if (publishBank(*_instance, std::move(bank)))
                _instance->status = "SLICE LAUNCH MODE UPDATED";
        } else if (_detailMenuKind == kDetailMenuSliceChoke
            && selection <= 16) {
            slot.slices[selected].chokeGroup
                = static_cast<uint8_t>(selection);
            if (publishBank(*_instance, std::move(bank)))
                _instance->status = "SLICE CHOKE GROUP UPDATED";
        }
    }
    [self setNeedsDisplay:YES];
}

- (BOOL)isEditingDetailField:(NSTextField*)field
{
    NSResponder* first = [[self window] firstResponder];
    NSText* editor = [field currentEditor];
    return first == field || (editor && first == editor);
}

- (void)updateDetailNumericFields
{
    const auto* slot = [self selectedSampleSlot];
    const BOOL visible = _page == 1u && slot != nullptr
        && _detailMenuKind == kDetailMenuNone;
    const bool sliceControlsVisible = visible && slot->asset
        && slotHasCompleteMap(*slot) && slot->sliceCount > 0u;
    const std::size_t selectedSlice = sliceControlsVisible
        ? static_cast<std::size_t>(std::clamp<NSInteger>(_selectedSlice, 0,
            static_cast<NSInteger>(slot->sliceCount) - 1)) : 0u;
    for (uint32_t index = 0u; index < kDetailNumericCount; ++index) {
        NSTextField* field = _detailNumericFields[index];
        [field setFrame:detailNumericFieldRect(index)];
        const bool propertyVisible = !isSlicePropertyNumeric(index)
            || (sliceControlsVisible
                && (index != kDetailNumericSlicePan
                    || slot->asset->channelCount <= 2u));
        [field setHidden:!visible || !propertyVisible];
        if (!visible || !propertyVisible
            || [self isEditingDetailField:field]) continue;
        switch (index) {
        case kDetailNumericPreRoll:
            [field setStringValue:[NSString stringWithFormat:@"%u",
                _instance->transientPreRollMicroseconds]];
            break;
        case kDetailNumericMinimumSlice:
            [field setStringValue:[NSString stringWithFormat:@"%u",
                _instance->minimumTransientSliceMilliseconds]];
            break;
        case kDetailNumericSourceTempo:
            [field setStringValue:[NSString stringWithFormat:@"%.1f",
                slot->sourceTempoBpm]];
            break;
        case kDetailNumericLoopCrossfade:
            [field setStringValue:[NSString stringWithFormat:@"%.1f",
                slot->loopCrossfade * 100.0f]];
            break;
        case kDetailNumericGlide:
            [field setStringValue:[NSString stringWithFormat:@"%.1f",
                slot->glideSeconds * 1000.0f]];
            break;
        case kDetailNumericSliceGain:
            [field setStringValue:[NSString stringWithFormat:@"%.3f",
                slot->slices[selectedSlice].gain]];
            break;
        case kDetailNumericSlicePitch:
            [field setStringValue:[NSString stringWithFormat:@"%+.2f",
                slot->slices[selectedSlice].transposeSemitones]];
            break;
        case kDetailNumericSlicePan:
            [field setStringValue:[NSString stringWithFormat:@"%+.3f",
                slot->slices[selectedSlice].pan]];
            break;
        default: break;
        }
    }
}

- (void)updateDetailNumericControl:(uint32_t)index point:(NSPoint)point
{
    if (index >= kDetailNumericCount) return;
    const NSRect track = detailNumericTrackRect(index);
    const double normalized = std::clamp(static_cast<double>(
        (point.x - NSMinX(track)) / NSWidth(track)), 0.0, 1.0);
    if (index == kDetailNumericPreRoll) {
        _instance->transientPreRollMicroseconds = static_cast<uint32_t>(
            std::llround(normalized * kMaximumTransientPreRollMicroseconds));
        _instance->status = "TRANSIENT PRE-ROLL UPDATED";
    } else if (index == kDetailNumericMinimumSlice) {
        _instance->minimumTransientSliceMilliseconds
            = static_cast<uint32_t>(std::llround(normalized
                * kMaximumMinimumTransientSliceMilliseconds));
        _instance->status = "MINIMUM TRANSIENT SLICE UPDATED";
    } else if (isSlicePropertyNumeric(index)) {
        auto bank = editableBank(*_instance);
        auto& slot = bank->slots[[self selectedSlot]];
        if (!slot.asset || !slotHasCompleteMap(slot)
            || slot.sliceCount == 0u) return;
        const std::size_t selected = static_cast<std::size_t>(
            std::clamp<NSInteger>(_selectedSlice, 0,
                static_cast<NSInteger>(slot.sliceCount) - 1));
        auto& slice = slot.slices[selected];
        if (index == kDetailNumericSliceGain) {
            slice.gain = static_cast<float>(normalized * 4.0);
            _instance->status = "SLICE GAIN UPDATED";
        } else if (index == kDetailNumericSlicePitch) {
            slice.transposeSemitones = static_cast<float>(
                -24.0 + normalized * 48.0);
            _instance->status = "SLICE PITCH UPDATED";
        } else if (slot.asset->channelCount <= 2u) {
            slice.pan = static_cast<float>(-1.0 + normalized * 2.0);
            _instance->status = "SLICE PAN UPDATED";
        } else {
            _instance->status = "PAN IS BYPASSED FOR MULTICHANNEL SOURCES";
            return;
        }
        (void)publishBank(*_instance, std::move(bank), false);
    } else {
        auto bank = editableBank(*_instance);
        auto& slot = bank->slots[[self selectedSlot]];
        if (index == kDetailNumericSourceTempo) {
            slot.sourceTempoBpm = static_cast<float>(20.0
                + normalized * 979.0);
            _instance->status = "MAPPED SLICE SOURCE BPM UPDATED";
        } else if (index == kDetailNumericLoopCrossfade) {
            slot.loopCrossfade = static_cast<float>(normalized * 0.5);
            _instance->status = "MAPPED SLICE LOOP CROSSFADE UPDATED";
        } else {
            slot.glideSeconds = static_cast<float>(normalized * 2.0);
            _instance->status = "MAPPED SLICE GLIDE UPDATED";
        }
        (void)publishBank(*_instance, std::move(bank), false);
    }
    [self updateDetailNumericFields];
    [self setNeedsDisplay:YES];
}

- (void)detailNumericFieldChanged:(id)sender
{
    NSTextField* field = static_cast<NSTextField*>(sender);
    const NSInteger index = [field tag] - kDetailNumericFieldTagBase;
    if (index < 0 || index >= kDetailNumericCount) return;
    const double value = [[field stringValue] doubleValue];
    if (index == kDetailNumericPreRoll) {
        _instance->transientPreRollMicroseconds = static_cast<uint32_t>(
            std::llround(std::clamp(value, 0.0, static_cast<double>(
                kMaximumTransientPreRollMicroseconds))));
        markStateDirty(*_instance);
        _instance->status = "TRANSIENT PRE-ROLL UPDATED";
    } else if (index == kDetailNumericMinimumSlice) {
        _instance->minimumTransientSliceMilliseconds
            = static_cast<uint32_t>(std::llround(std::clamp(value, 0.0,
                static_cast<double>(
                    kMaximumMinimumTransientSliceMilliseconds))));
        markStateDirty(*_instance);
        _instance->status = "MINIMUM TRANSIENT SLICE UPDATED";
    } else if (isSlicePropertyNumeric(static_cast<uint32_t>(index))) {
        auto bank = editableBank(*_instance);
        auto& slot = bank->slots[[self selectedSlot]];
        if (!slot.asset || !slotHasCompleteMap(slot)
            || slot.sliceCount == 0u) return;
        const std::size_t selected = static_cast<std::size_t>(
            std::clamp<NSInteger>(_selectedSlice, 0,
                static_cast<NSInteger>(slot.sliceCount) - 1));
        auto& slice = slot.slices[selected];
        if (index == kDetailNumericSliceGain) {
            slice.gain = static_cast<float>(
                std::clamp(value, 0.0, 4.0));
            _instance->status = "SLICE GAIN UPDATED";
        } else if (index == kDetailNumericSlicePitch) {
            slice.transposeSemitones = static_cast<float>(
                std::clamp(value, -96.0, 96.0));
            _instance->status = "SLICE PITCH UPDATED";
        } else if (slot.asset->channelCount <= 2u) {
            slice.pan = static_cast<float>(
                std::clamp(value, -1.0, 1.0));
            _instance->status = "SLICE PAN UPDATED";
        } else {
            _instance->status = "PAN IS BYPASSED FOR MULTICHANNEL SOURCES";
            [self updateDetailNumericFields];
            return;
        }
        (void)publishBank(*_instance, std::move(bank));
    } else {
        auto bank = editableBank(*_instance);
        auto& slot = bank->slots[[self selectedSlot]];
        if (index == kDetailNumericSourceTempo) {
            slot.sourceTempoBpm = static_cast<float>(
                std::clamp(value, 20.0, 999.0));
            _instance->status = "MAPPED SLICE SOURCE BPM UPDATED";
        } else if (index == kDetailNumericLoopCrossfade) {
            slot.loopCrossfade = static_cast<float>(
                std::clamp(value, 0.0, 50.0) * 0.01);
            _instance->status = "MAPPED SLICE LOOP CROSSFADE UPDATED";
        } else {
            slot.glideSeconds = static_cast<float>(
                std::clamp(value, 0.0, 2000.0) * 0.001);
            _instance->status = "MAPPED SLICE GLIDE UPDATED";
        }
        (void)publishBank(*_instance, std::move(bank));
    }
    [self updateDetailNumericFields];
    [self setNeedsDisplay:YES];
}

- (void)controlTextDidBeginEditing:(NSNotification*)notification
{
    NSTextField* field = static_cast<NSTextField*>([notification object]);
    s3g::clap_gui::styleActiveNumberTextField(field, true);
    s3g::clap_gui::styleNumberTextEditor(field);
}

- (void)controlTextDidChange:(NSNotification*)notification
{
    s3g::clap_gui::styleNumberTextEditor(
        static_cast<NSTextField*>([notification object]));
}

- (void)controlTextDidEndEditing:(NSNotification*)notification
{
    NSTextField* field = static_cast<NSTextField*>([notification object]);
    s3g::clap_gui::styleActiveNumberTextField(field, false);
    [self detailNumericFieldChanged:field];
}

- (BOOL)control:(NSControl*)control textView:(NSTextView*)textView
    doCommandBySelector:(SEL)commandSelector
{
    (void)textView;
    if (commandSelector == @selector(insertNewline:)
        || commandSelector == @selector(insertTab:)) {
        [self detailNumericFieldChanged:control];
        [[self window] makeFirstResponder:self];
        return YES;
    }
    return NO;
}

- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender
{
    NSPasteboard* pasteboard = [sender draggingPasteboard];
    return [pasteboard canReadObjectForClasses:@[ [NSURL class] ]
        options:@{ NSPasteboardURLReadingFileURLsOnlyKey: @YES }]
        ? NSDragOperationCopy : NSDragOperationNone;
}

- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender
{
    NSArray<NSURL*>* urls = [[sender draggingPasteboard]
        readObjectsForClasses:@[ [NSURL class] ]
        options:@{ NSPasteboardURLReadingFileURLsOnlyKey: @YES }];
    if (urls.count == 0u) return NO;
    const NSPoint point = [self convertPoint:[sender draggingLocation]
        fromView:nil];
    uint32_t target = [self selectedSlot];
    for (uint32_t index = 0u;
         index < s3g::breakbeat::kMaximumSampleSlots; ++index) {
        if (NSPointInRect(point, bankRowRect(index))
            || NSPointInRect(point, overviewWaveformRect(index))) {
            target = index;
            break;
        }
    }
    _instance->selectedSlot = target;
    bool queued = false;
    for (NSURL* url in urls) {
        if (target >= s3g::breakbeat::kMaximumSampleSlots) break;
        const char* path = url.fileURL
            ? [[url path] fileSystemRepresentation] : nullptr;
        if (path) {
            queueSampleLoad(*_instance, target, path);
            queued = true;
            ++target;
        }
    }
    if (queued) {
        _selectedSlice = 0;
        _zoom = 1.0;
        _visibleStart = 0u;
        [self setNeedsDisplay:YES];
    }
    return queued ? YES : NO;
}

- (void)startTimer
{
    if (_timer) return;
    __weak S3GBreakbeatSlicerView* weakSelf = self;
    _timer = [NSTimer scheduledTimerWithTimeInterval:1.0 / 30.0
        repeats:YES block:^(NSTimer*) {
            S3GBreakbeatSlicerView* view = weakSelf;
            if (!view) return;
            serviceSampleLoads(*view->_instance);
            [view updateDetailNumericFields];
            [view setNeedsDisplay:YES];
        }];
}

- (void)stopTimer
{
    [_timer invalidate];
    _timer = nil;
}

- (BOOL)loadDocumentationBreaks
{
    constexpr const char* environmentNames[] {
        "S3G_GUI_DOCUMENTATION_SAMPLE_PATH",
        "S3G_GUI_DOCUMENTATION_SAMPLE_PATH_2",
        "S3G_GUI_DOCUMENTATION_SAMPLE_PATH_3",
        "S3G_GUI_DOCUMENTATION_SAMPLE_PATH_4",
    };
    for (uint32_t index = 0u; index < std::size(environmentNames); ++index) {
        const char* path = std::getenv(environmentNames[index]);
        if (!path || !path[0]) return NO;
        queueSampleLoad(*_instance, index, path);
    }

    bool loaded = false;
    for (uint32_t attempt = 0u; attempt < 1000u; ++attempt) {
        serviceSampleLoads(*_instance);
        loaded = _instance->controlBank != nullptr;
        for (uint32_t index = 0u; loaded && index < 4u; ++index) {
            loaded = _instance->controlBank->slots[index].asset != nullptr;
        }
        if (loaded) break;
        [NSThread sleepForTimeInterval:0.002];
    }
    if (!loaded) return NO;

    for (uint32_t index = 0u; index < 4u; ++index) {
        const auto& slot = _instance->controlBank->slots[index];
        const auto analysis = _instance->analyses[index];
        if (!slot.asset || !analysis) return NO;
        const uint32_t zeroRadius = static_cast<uint32_t>(std::lround(
            slot.asset->sampleRate * 0.004));
        const auto slices = s3g::breakbeat::makeTransientSlices(
            *slot.asset, *analysis, 32u, zeroRadius,
            _instance->transientPreRollMicroseconds,
            static_cast<uint32_t>(std::lround(slot.asset->sampleRate
                * _instance->minimumTransientSliceMilliseconds * 0.001)));
        if (slices.empty()
            || !replaceSlotSlices(*_instance, index, slices)
            || !automapSlot(*_instance, index)) return NO;
    }

    auto mixerBank = editableBank(*_instance);
    if (!mixerBank) return NO;
    constexpr std::array<float, 4u> gains {{ 1.00f, 0.88f, 1.08f, 0.94f }};
    constexpr std::array<float, 4u> pans {{ -0.42f, 0.36f, -0.18f, 0.24f }};
    constexpr std::array<float, 4u> sends {{ 0.46f, 0.31f, 0.58f, 0.39f }};
    constexpr std::array<s3g::breakbeat::InsertType, 4u> firstInserts {{
        s3g::breakbeat::InsertType::Filter,
        s3g::breakbeat::InsertType::Transient,
        s3g::breakbeat::InsertType::Degrade,
        s3g::breakbeat::InsertType::Resonator,
    }};
    constexpr std::array<s3g::breakbeat::InsertType, 4u> secondInserts {{
        s3g::breakbeat::InsertType::Wavefolder,
        s3g::breakbeat::InsertType::Off,
        s3g::breakbeat::InsertType::Repeater,
        s3g::breakbeat::InsertType::Erosion,
    }};
    for (uint32_t index = 0u; index < 4u; ++index) {
        auto& slot = mixerBank->slots[index];
        slot.mixerGain = gains[index];
        slot.mixerPan = pans[index];
        slot.mixerLowEqDb = index % 2u == 0u ? 1.8f : -1.2f;
        slot.mixerMidEqDb = index < 2u ? -1.5f : 1.1f;
        slot.mixerHighEqDb = index % 2u == 0u ? 0.9f : 1.6f;
        slot.mixerMidFrequencyHz = 520.0f + 410.0f * index;
        slot.mixerAuxSend = sends[index];
        slot.inserts[0] = s3g::breakbeat::defaultInsertSettings(
            firstInserts[index]);
        slot.inserts[1] = s3g::breakbeat::defaultInsertSettings(
            secondInserts[index]);
    }
    auto& documentedSlice = mixerBank->slots[0u].slices[0u];
    const auto& documentedAsset = *mixerBank->slots[0u].asset;
    const uint32_t zeroRadius = static_cast<uint32_t>(std::lround(
        documentedAsset.sampleRate * 0.004));
    const uint32_t documentedLoopStart
        = s3g::breakbeat::nearestZeroFrame(documentedAsset,
            documentedSlice.startFrame
                + (documentedSlice.endFrame
                    - documentedSlice.startFrame) / 4u,
            zeroRadius);
    const uint32_t documentedLoopEnd
        = s3g::breakbeat::nearestZeroFrame(documentedAsset,
            documentedSlice.startFrame
                + (documentedSlice.endFrame
                    - documentedSlice.startFrame) * 3u / 4u,
            zeroRadius);
    if (documentedLoopStart >= documentedSlice.startFrame
        && documentedLoopStart < documentedLoopEnd
        && documentedLoopEnd <= documentedSlice.endFrame) {
        documentedSlice.launchMode = s3g::breakbeat::LaunchMode::Loop;
        documentedSlice.loopStartFrame = documentedLoopStart;
        documentedSlice.loopEndFrame = documentedLoopEnd;
    }
    mixerBank->auxEnabled = true;
    mixerBank->auxPress = 0.52f;
    mixerBank->auxSnap = 0.24f;
    mixerBank->auxRecovery = 0.41f;
    mixerBank->auxSaturation = 0.28f;
    mixerBank->auxBite = 0.17f;
    mixerBank->auxClip = 0.08f;
    mixerBank->auxTilt = -0.12f;
    mixerBank->auxReturnDb = -7.5f;
    mixerBank->auxLinkMode = s3g::BreakBusLinkMode::Pair;
    if (!publishBank(*_instance, std::move(mixerBank), false)) return NO;
    constexpr std::array<float, 4u> peaks {{ 0.78f, 0.57f, 0.69f, 0.49f }};
    for (std::size_t index = 0u; index < peaks.size(); ++index)
        _instance->slotPeaks[index].store(peaks[index],
            std::memory_order_relaxed);
    _instance->auxActivity.store(0.61f, std::memory_order_relaxed);
    _instance->auxGainReductionDb.store(-3.8f, std::memory_order_relaxed);
    _instance->outputPeak.store(0.82f, std::memory_order_relaxed);
    _instance->selectedSlot = 0u;
    _selectedSlice = 0;
    _zoom = 1.0;
    _visibleStart = 0u;
    _page = 0u;
    _instance->status = "DOCUMENTATION BREAK BANK READY";
    [self setNeedsDisplay:YES];
    [self displayIfNeeded];
    return YES;
}

- (void)setDocumentationPage:(NSUInteger)page
{
    [self closeDetailMenu];
    _page = static_cast<uint32_t>(std::min<NSUInteger>(page, 3u));
    if (_page == 3u && _instance->controlBank
        && (_instance->controlBank->slots[2u].asset
            || _instance->controlBank->slots[3u].asset)) {
        auto buildBank = editableBank(*_instance);
        for (uint32_t index = 2u; index < 4u; ++index) {
            const uint8_t root = buildBank->slots[index].rootNote;
            const uint8_t midi = buildBank->slots[index].midiChannel;
            s3g::breakbeat::clearMappingsForSlot(*buildBank,
                static_cast<uint8_t>(index));
            buildBank->slots[index] = {};
            buildBank->slots[index].rootNote = root;
            buildBank->slots[index].mappedRootNote = root;
            buildBank->slots[index].midiChannel = midi;
            _instance->samplePaths[index].clear();
            _instance->analyses[index].reset();
            resetSlotVariations(*_instance, index);
        }
        (void)publishBank(*_instance, std::move(buildBank), false);
        _instance->selectedSlot = 0u;
        _instance->structuralMutationUses
            = s3g::breakbeat::kDefaultStructuralMutationUses;
    }
    [self updateDetailNumericFields];
    [self setNeedsDisplay:YES];
    [self displayIfNeeded];
}

- (BOOL)runDocumentationMutationFill
{
    if (!_instance->controlBank || !_instance->controlBank->slots[0u].asset) {
        std::fprintf(stderr, "Slicer mutation fill: source unavailable\n");
        return NO;
    }
    _instance->selectedSlot = 0u;
    const uint32_t queued = fillEmptySlotsWithMutations(*_instance, 0u);
    if (queued != 2u) {
        std::fprintf(stderr, "Slicer mutation fill: queued %u, expected 2\n",
            queued);
        return NO;
    }
    for (uint32_t attempt = 0u; attempt < 5000u; ++attempt) {
        serviceSampleLoads(*_instance);
        if (!_instance->pendingMutationSlots[2u]
            && !_instance->pendingMutationSlots[3u]) break;
        [NSThread sleepForTimeInterval:0.002];
    }
    for (uint32_t index = 2u; index < 4u; ++index) {
        const auto& slot = _instance->controlBank->slots[index];
        if (_instance->pendingMutationSlots[index] || !slot.asset
            || slot.sliceCount <= 1u
            || slot.mappedSliceCount != slot.sliceCount
            || _instance->samplePaths[index].rfind(
                "MUTATED BREAK ", 0u) != 0u) {
            std::fprintf(stderr,
                "Slicer mutation fill: slot %u pending=%d asset=%d slices=%u mapped=%u path='%s' status='%s'\n",
                index + 1u, _instance->pendingMutationSlots[index],
                slot.asset != nullptr, static_cast<unsigned>(slot.sliceCount),
                static_cast<unsigned>(slot.mappedSliceCount),
                _instance->samplePaths[index].c_str(),
                _instance->status.c_str());
            return NO;
        }
    }
    return YES;
}

- (const BankSnapshot*)displayBank
{
    return _dragBank ? _dragBank.get() : _instance->controlBank.get();
}

- (uint32_t)selectedSlot
{
    return std::min<uint32_t>(_instance->selectedSlot,
        static_cast<uint32_t>(s3g::breakbeat::kMaximumSampleSlots - 1u));
}

- (const s3g::breakbeat::SampleSlot*)selectedSampleSlot
{
    const auto* bank = [self displayBank];
    return bank ? &bank->slots[[self selectedSlot]] : nullptr;
}

- (uint32_t)visibleFrames:(uint32_t)total
{
    if (total == 0u) return 0u;
    _zoom = std::clamp<CGFloat>(_zoom, 1.0,
        std::max<CGFloat>(1.0, static_cast<CGFloat>(total) / 32.0));
    return std::clamp<uint32_t>(static_cast<uint32_t>(std::ceil(
        static_cast<double>(total) / _zoom)), 1u, total);
}

- (uint32_t)clampedStart:(uint32_t)total
{
    const uint32_t visible = [self visibleFrames:total];
    _visibleStart = std::min(_visibleStart,
        total > visible ? total - visible : 0u);
    return _visibleStart;
}

- (CGFloat)xForFrame:(uint32_t)frame total:(uint32_t)total
{
    const NSRect rect = waveformRect();
    const uint32_t visible = [self visibleFrames:total];
    const uint32_t start = [self clampedStart:total];
    return rect.origin.x + rect.size.width
        * (static_cast<CGFloat>(frame) - static_cast<CGFloat>(start))
        / static_cast<CGFloat>(visible);
}

- (uint32_t)frameForX:(CGFloat)x total:(uint32_t)total
{
    const NSRect rect = waveformRect();
    const uint32_t visible = [self visibleFrames:total];
    const uint32_t start = [self clampedStart:total];
    const CGFloat fraction = std::clamp<CGFloat>((x - rect.origin.x)
        / rect.size.width, 0.0, 1.0);
    return std::min<uint32_t>(total - 1u, start
        + static_cast<uint32_t>(std::llround(fraction * (visible - 1u))));
}

- (NSRect)viewportHandleForTotal:(uint32_t)total
{
    const NSRect track = NSInsetRect(waveformNavigatorRect(), 1.0, 1.0);
    if (total == 0u) return track;
    const uint32_t visible = [self visibleFrames:total];
    const uint32_t start = [self clampedStart:total];
    const CGFloat fractionVisible = static_cast<CGFloat>(visible)
        / static_cast<CGFloat>(total);
    const CGFloat width = std::clamp<CGFloat>(
        track.size.width * fractionVisible, 30.0, track.size.width);
    const CGFloat travel = track.size.width - width;
    const CGFloat position = total > visible
        ? static_cast<CGFloat>(start)
            / static_cast<CGFloat>(total - visible)
        : 0.0;
    return NSMakeRect(track.origin.x + travel * position, track.origin.y,
        width, track.size.height);
}

- (void)setViewportHandleOrigin:(CGFloat)origin total:(uint32_t)total
{
    if (total == 0u) return;
    const NSRect track = NSInsetRect(waveformNavigatorRect(), 1.0, 1.0);
    const uint32_t visible = [self visibleFrames:total];
    const NSRect handle = [self viewportHandleForTotal:total];
    const CGFloat travel = track.size.width - handle.size.width;
    if (travel <= 0.0 || total <= visible) {
        _visibleStart = 0u;
        return;
    }
    const CGFloat position = std::clamp<CGFloat>(
        (origin - track.origin.x) / travel, 0.0, 1.0);
    _visibleStart = static_cast<uint32_t>(std::llround(position
        * static_cast<CGFloat>(total - visible)));
}

- (NSInteger)markerNearPoint:(NSPoint)point
{
    const auto* slot = [self selectedSampleSlot];
    if (!slot || !slot->asset) return -1;
    for (std::size_t index = 1u; index < slot->sliceCount; ++index) {
        const CGFloat x = [self xForFrame:slot->slices[index].startFrame
            total:slot->asset->frameCount()];
        if (std::abs(point.x - x) <= 7.0) return static_cast<NSInteger>(index);
    }
    return -1;
}

- (NSInteger)loopHandleNearPoint:(NSPoint)point
{
    const auto* slot = [self selectedSampleSlot];
    if (!slot || !slot->asset || !slotHasCompleteMap(*slot)
        || _selectedSlice < 0
        || static_cast<std::size_t>(_selectedSlice) >= slot->sliceCount)
        return -1;
    const auto& slice = slot->slices[static_cast<std::size_t>(
        _selectedSlice)];
    if (slice.launchMode != s3g::breakbeat::LaunchMode::Loop
        && slice.launchMode != s3g::breakbeat::LaunchMode::PingPong)
        return -1;
    const uint32_t loopStart = slice.loopStartFrame == 0u
            && slice.loopEndFrame == 0u
        ? slice.startFrame : slice.loopStartFrame;
    const uint32_t loopEnd = slice.loopStartFrame == 0u
            && slice.loopEndFrame == 0u
        ? slice.endFrame : slice.loopEndFrame;
    const CGFloat startX = [self xForFrame:loopStart
        total:slot->asset->frameCount()];
    const CGFloat endX = [self xForFrame:loopEnd
        total:slot->asset->frameCount()];
    const CGFloat startDistance = std::abs(point.x - startX);
    const CGFloat endDistance = std::abs(point.x - endX);
    if (std::min(startDistance, endDistance) > 8.0) return -1;
    return startDistance <= endDistance ? 0 : 1;
}

- (NSInteger)sliceAtFrame:(uint32_t)frame
{
    const auto* slot = [self selectedSampleSlot];
    if (!slot) return -1;
    for (std::size_t index = 0u; index < slot->sliceCount; ++index) {
        if (frame >= slot->slices[index].startFrame
            && frame < slot->slices[index].endFrame)
            return static_cast<NSInteger>(index);
    }
    return -1;
}

- (void)drawWaveformForSlot:(const s3g::breakbeat::SampleSlot&)slot
{
    const NSRect rect = waveformRect();
    [s3g::clap_gui::color(0x111111) setFill];
    NSRectFill(rect);
    [s3g::clap_gui::color(0x555555) setStroke];
    NSFrameRect(rect);
    if (!slot.asset) {
        [@"LOAD A SAMPLE INTO THE SELECTED BANK SLOT"
            drawAtPoint:NSMakePoint(rect.origin.x + 18.0,
                rect.origin.y + 18.0)
                withAttributes:slicerSoftLabelAttrs()];
        return;
    }

    const auto& asset = *slot.asset;
    const uint32_t total = asset.frameCount();
    const uint32_t visible = [self visibleFrames:total];
    const uint32_t start = [self clampedStart:total];
    const uint32_t end = std::min<uint32_t>(total, start + visible);
    const uint32_t pixels = std::max<uint32_t>(1u,
        static_cast<uint32_t>(std::floor(rect.size.width - 2.0)));
    if (_selectedSlice >= 0
        && static_cast<std::size_t>(_selectedSlice) < slot.sliceCount) {
        const auto& selected = slot.slices[
            static_cast<std::size_t>(_selectedSlice)];
        const CGFloat selectedX = [self xForFrame:selected.startFrame
            total:total];
        const CGFloat selectedEndX = [self xForFrame:selected.endFrame
            total:total];
        // Paint selection into the waveform background. Waveform traces are
        // drawn afterward and therefore remain fully legible.
        [s3g::clap_gui::color(0x1b191c) setFill];
        NSRectFill(NSIntersectionRect(rect, NSMakeRect(selectedX,
            rect.origin.y + 1.0,
            std::max<CGFloat>(1.0, selectedEndX - selectedX),
            rect.size.height - 2.0)));
    }
    const auto drawChannel = [&](const std::vector<float>& source,
                                 CGFloat top, CGFloat height,
                                 NSColor* color) {
        NSBezierPath* path = [NSBezierPath bezierPath];
        [path setLineWidth:1.0];
        const CGFloat center = top + height * 0.5;
        if (visible <= pixels) {
            // At sample-level zoom, join the actual sample values. The
            // overview renderer below intentionally uses min/max columns,
            // but those columns look blocky once individual samples fit.
            const CGFloat drawableWidth = rect.size.width - 2.0;
            const CGFloat denominator = static_cast<CGFloat>(
                std::max<uint32_t>(1u, visible - 1u));
            bool began = false;
            for (uint32_t frame = start; frame < end; ++frame) {
                const CGFloat fraction = static_cast<CGFloat>(frame - start)
                    / denominator;
                const CGFloat px = rect.origin.x + 1.0
                    + drawableWidth * fraction;
                const CGFloat py = center - source[frame] * (height * 0.44);
                if (!began) {
                    [path moveToPoint:NSMakePoint(px, py)];
                    began = true;
                } else {
                    [path lineToPoint:NSMakePoint(px, py)];
                }
            }
            [color setStroke];
            [path stroke];
            return;
        }
        for (uint32_t x = 0u; x < pixels; ++x) {
            const uint32_t first = start + static_cast<uint32_t>(
                static_cast<uint64_t>(visible) * x / pixels);
            const uint32_t last = std::min<uint32_t>(end, start
                + static_cast<uint32_t>(static_cast<uint64_t>(visible)
                    * (x + 1u) / pixels) + 1u);
            float minimum = 1.0f;
            float maximum = -1.0f;
            const uint32_t stride = std::max<uint32_t>(1u,
                (last > first ? last - first : 1u) / 32u);
            for (uint32_t frame = first; frame < last; frame += stride) {
                minimum = std::min(minimum, source[frame]);
                maximum = std::max(maximum, source[frame]);
            }
            const CGFloat px = rect.origin.x + 1.0 + x;
            const CGFloat y1 = center - maximum * (height * 0.44);
            const CGFloat y2 = center - minimum * (height * 0.44);
            [path moveToPoint:NSMakePoint(px, y1)];
            [path lineToPoint:NSMakePoint(px, y2)];
        }
        [color setStroke];
        [path stroke];
    };
    const CGFloat usableHeight = rect.size.height - 4.0;
    const CGFloat laneHeight = usableHeight
        / static_cast<CGFloat>(asset.channelCount);
    for (uint32_t channel = 0u; channel < asset.channelCount; ++channel) {
        const CGFloat top = rect.origin.y + 2.0
            + laneHeight * static_cast<CGFloat>(channel);
        drawChannel(asset.channels[channel], top, laneHeight,
            s3g::clap_gui::color(channel % 2u == 0u
                ? 0x747d78 : 0x68706c));
        if (channel != 0u) {
            [s3g::clap_gui::color(0x333333) setFill];
            NSRectFill(NSMakeRect(rect.origin.x + 1.0, top,
                rect.size.width - 2.0, 1.0));
        }
        if (asset.channelCount > 2u) {
            NSString* channelLabel = [NSString stringWithFormat:@"%02u",
                static_cast<unsigned>(channel + 1u)];
            [channelLabel drawAtPoint:NSMakePoint(rect.origin.x + 5.0,
                top + 1.0)
                withAttributes:slicerTextAttrs(
                    s3g::clap_gui::color(0x676767), 8.0)];
        }
    }

    for (std::size_t index = 0u; index < slot.sliceCount; ++index) {
        const auto& slice = slot.slices[index];
        const CGFloat x = [self xForFrame:slice.startFrame total:total];
        if (x < rect.origin.x || x > NSMaxX(rect)) continue;
        [s3g::clap_gui::color(
            static_cast<NSInteger>(index) == _selectedSlice ? 0x756875
                : index == 0u ? 0x8e824f : 0x805b5d)
            setStroke];
        NSBezierPath* marker = [NSBezierPath bezierPath];
        [marker moveToPoint:NSMakePoint(x, rect.origin.y + 1.0)];
        [marker lineToPoint:NSMakePoint(x, NSMaxY(rect) - 1.0)];
        [marker stroke];
    }

    if (slotHasCompleteMap(slot) && _selectedSlice >= 0
        && static_cast<std::size_t>(_selectedSlice) < slot.sliceCount) {
        const auto& selected = slot.slices[
            static_cast<std::size_t>(_selectedSlice)];
        if (selected.launchMode == s3g::breakbeat::LaunchMode::Loop
            || selected.launchMode
                == s3g::breakbeat::LaunchMode::PingPong) {
            const uint32_t loopStart = selected.loopStartFrame == 0u
                    && selected.loopEndFrame == 0u
                ? selected.startFrame : selected.loopStartFrame;
            const uint32_t loopEnd = selected.loopStartFrame == 0u
                    && selected.loopEndFrame == 0u
                ? selected.endFrame : selected.loopEndFrame;
            const std::array<std::pair<uint32_t, NSString*>, 2u> handles {{
                { loopStart, @"LS" }, { loopEnd, @"LE" },
            }};
            for (const auto& handle : handles) {
                const CGFloat x = [self xForFrame:handle.first total:total];
                if (x < rect.origin.x || x > NSMaxX(rect)) continue;
                NSColor* handleColor = s3g::clap_gui::color(0xb5a56c);
                [handleColor setStroke];
                NSBezierPath* line = [NSBezierPath bezierPath];
                [line setLineWidth:1.5];
                [line moveToPoint:NSMakePoint(x, rect.origin.y + 14.0)];
                [line lineToPoint:NSMakePoint(x, NSMaxY(rect) - 1.0)];
                [line stroke];
                NSDictionary* handleAttrs = slicerTextAttrs(
                    s3g::clap_gui::color(0xe1d49c), 7.5,
                    NSFontWeightSemibold);
                const NSSize textSize = [handle.second
                    sizeWithAttributes:handleAttrs];
                const NSRect flagRect = NSMakeRect(
                    std::clamp<CGFloat>(x - textSize.width * 0.5 - 3.0,
                        rect.origin.x + 1.0,
                        NSMaxX(rect) - textSize.width - 7.0),
                    rect.origin.y + 2.0, textSize.width + 6.0,
                    textSize.height + 2.0);
                [s3g::clap_gui::color(0x2c291f) setFill];
                NSRectFill(flagRect);
                [handleColor setStroke];
                NSFrameRect(flagRect);
                [handle.second drawAtPoint:NSMakePoint(
                    flagRect.origin.x + 3.0, flagRect.origin.y)
                    withAttributes:handleAttrs];
            }
        }
    }

    for (std::size_t cursorIndex = 0u;
         cursorIndex < _instance->voicePlayheads.size(); ++cursorIndex) {
        if (_instance->voicePlayheadSlots[cursorIndex].load(
                std::memory_order_relaxed) != [self selectedSlot]) continue;
        const float playhead = _instance->voicePlayheads[cursorIndex].load(
            std::memory_order_relaxed);
        if (playhead < 0.0f) continue;
        const uint32_t frame = std::min<uint32_t>(total - 1u,
            static_cast<uint32_t>(playhead * static_cast<float>(total)));
        const CGFloat x = [self xForFrame:frame total:total];
        if (x >= rect.origin.x && x <= NSMaxX(rect)) {
            NSColor* cursorColor = s3g::clap_gui::color(
                cursorIndex % 2u == 0u ? 0xa9d18e : 0x8fb8cf);
            [cursorColor setStroke];
            NSBezierPath* cursor = [NSBezierPath bezierPath];
            [cursor setLineWidth:2.0];
            [cursor moveToPoint:NSMakePoint(x, rect.origin.y + 1.0)];
            [cursor lineToPoint:NSMakePoint(x, NSMaxY(rect) - 1.0)];
            [cursor stroke];
            const uint8_t key = _instance->voicePlayheadKeys[cursorIndex]
                .load(std::memory_order_relaxed);
            NSString* flag = [NSString stringWithFormat:@"%@/%u",
                shortNoteText(key), static_cast<unsigned>(key)];
            NSDictionary* flagAttrs = slicerTextAttrs(
                s3g::clap_gui::color(0xd0d8d3), 7.5,
                NSFontWeightMedium);
            const NSSize flagSize = [flag sizeWithAttributes:flagAttrs];
            const CGFloat flagX = std::clamp<CGFloat>(x + 3.0,
                rect.origin.x + 2.0,
                NSMaxX(rect) - flagSize.width - 8.0);
            const NSRect flagRect = NSMakeRect(flagX,
                rect.origin.y + 4.0, flagSize.width + 6.0,
                flagSize.height + 3.0);
            [s3g::clap_gui::color(0x202522) setFill];
            NSRectFill(flagRect);
            [cursorColor setStroke];
            NSFrameRect(flagRect);
            [flag drawAtPoint:NSMakePoint(flagX + 3.0,
                flagRect.origin.y + 1.0) withAttributes:flagAttrs];
        }
    }

    const NSRect navigator = waveformNavigatorRect();
    [s3g::clap_gui::color(0x161616) setFill];
    NSRectFill(navigator);
    [s3g::clap_gui::color(0x444444) setStroke];
    NSFrameRect(navigator);
    const NSRect handle = [self viewportHandleForTotal:total];
    [s3g::clap_gui::color(_zoom > 1.0 ? 0x66616a : 0x3d3d3d) setFill];
    NSRectFill(handle);
    [s3g::clap_gui::color(_zoom > 1.0 ? 0x8b8490 : 0x515151) setStroke];
    NSFrameRect(handle);
}

- (void)drawOverviewWaveformForSlot:(const s3g::breakbeat::SampleSlot&)slot
    index:(uint32_t)slotIndex
{
    const NSRect rect = overviewWaveformRect(slotIndex);
    const bool selected = slotIndex == [self selectedSlot];
    [s3g::clap_gui::color(selected ? 0x171b19 : 0x111111) setFill];
    NSRectFill(rect);
    [s3g::clap_gui::color(selected ? 0x59645d : 0x3d3d3d) setStroke];
    NSFrameRect(rect);
    NSDictionary* label = slicerTextAttrs(
        s3g::clap_gui::color(0xa8ada9), 10.0);
    NSString* heading = [NSString stringWithFormat:@"BREAK %u   %@",
        static_cast<unsigned>(slotIndex + 1u),
        slotFilename(*_instance, slotIndex)];
    [heading drawAtPoint:NSMakePoint(rect.origin.x + 8.0,
        rect.origin.y + 5.0) withAttributes:label];
    if (!slot.asset) {
        [@"DROP SAMPLE HERE"
            drawAtPoint:NSMakePoint(rect.origin.x + 8.0,
                rect.origin.y + 42.0)
            withAttributes:slicerSoftLabelAttrs()];
        return;
    }
    const auto& asset = *slot.asset;
    const uint32_t total = asset.frameCount();
    const CGFloat top = rect.origin.y + 23.0;
    const CGFloat height = rect.size.height - 27.0;
    const CGFloat center = top + height * 0.5;
    const uint32_t pixels = std::max<uint32_t>(1u,
        static_cast<uint32_t>(rect.size.width - 2.0));
    NSBezierPath* trace = [NSBezierPath bezierPath];
    [trace setLineWidth:1.0];
    for (uint32_t x = 0u; x < pixels; ++x) {
        const uint32_t first = static_cast<uint32_t>(
            static_cast<uint64_t>(total) * x / pixels);
        const uint32_t last = std::min<uint32_t>(total, std::max(first + 1u,
            static_cast<uint32_t>(static_cast<uint64_t>(total) * (x + 1u)
                / pixels)));
        float minimum = 1.0f;
        float maximum = -1.0f;
        const uint32_t stride = std::max<uint32_t>(1u,
            (last - first) / 24u);
        for (uint32_t channel = 0u; channel < asset.channelCount; ++channel) {
            const auto& source = asset.channels[channel];
            for (uint32_t frame = first; frame < last; frame += stride) {
                minimum = std::min(minimum, source[frame]);
                maximum = std::max(maximum, source[frame]);
            }
        }
        const CGFloat px = rect.origin.x + 1.0 + x;
        [trace moveToPoint:NSMakePoint(px,
            center - maximum * height * 0.42)];
        [trace lineToPoint:NSMakePoint(px,
            center - minimum * height * 0.42)];
    }
    [s3g::clap_gui::color(0x77817b) setStroke];
    [trace stroke];
    for (uint16_t slice = 0u; slice < slot.sliceCount; ++slice) {
        const CGFloat x = rect.origin.x + rect.size.width
            * static_cast<CGFloat>(slot.slices[slice].startFrame)
            / static_cast<CGFloat>(total);
        [s3g::clap_gui::color(slice == 0u ? 0x9b8d56 : 0x865f62)
            setStroke];
        NSBezierPath* marker = [NSBezierPath bezierPath];
        [marker moveToPoint:NSMakePoint(x, top)];
        [marker lineToPoint:NSMakePoint(x, NSMaxY(rect) - 2.0)];
        [marker stroke];
    }
    for (std::size_t cursorIndex = 0u;
         cursorIndex < _instance->voicePlayheads.size(); ++cursorIndex) {
        if (_instance->voicePlayheadSlots[cursorIndex].load(
                std::memory_order_relaxed) != slotIndex) continue;
        const float playhead = _instance->voicePlayheads[cursorIndex].load(
            std::memory_order_relaxed);
        if (playhead < 0.0f) continue;
        const CGFloat x = rect.origin.x + rect.size.width * playhead;
        [s3g::clap_gui::color(cursorIndex % 2u == 0u
            ? 0xa9d18e : 0x8fb8cf) setStroke];
        NSBezierPath* cursor = [NSBezierPath bezierPath];
        [cursor setLineWidth:2.0];
        [cursor moveToPoint:NSMakePoint(x, top)];
        [cursor lineToPoint:NSMakePoint(x, NSMaxY(rect) - 2.0)];
        [cursor stroke];
    }
}

- (void)drawMutateWaveformForSlot:(const s3g::breakbeat::SampleSlot&)slot
{
    const NSRect rect = mutateWaveformRect();
    [s3g::clap_gui::color(0x111111) setFill];
    NSRectFill(rect);
    [s3g::clap_gui::color(0x4f5853) setStroke];
    NSFrameRect(rect);
    NSDictionary* label = slicerTextAttrs(
        s3g::clap_gui::color(0xa8ada9), 10.0);
    NSDictionary* value = slicerTextAttrs(
        s3g::clap_gui::color(0xc6cbc8), 11.0,
        NSFontWeightMedium);
    const uint32_t slotIndex = [self selectedSlot];
    NSString* heading = [NSString stringWithFormat:
        @"BREAK %u  //  MUTATION SOURCE  //  %@",
        static_cast<unsigned>(slotIndex + 1u),
        slotFilename(*_instance, slotIndex)];
    [heading drawAtPoint:NSMakePoint(rect.origin.x + 12.0,
        rect.origin.y + 9.0) withAttributes:value];
    if (!slot.asset) {
        [@"LOAD A BREAK TO USE IT AS A MUTATION SOURCE"
            drawAtPoint:NSMakePoint(rect.origin.x + 12.0,
                rect.origin.y + 54.0)
            withAttributes:slicerSoftLabelAttrs()];
        return;
    }
    const auto& asset = *slot.asset;
    const uint32_t total = asset.frameCount();
    const CGFloat top = rect.origin.y + 36.0;
    const CGFloat height = rect.size.height - 62.0;
    const CGFloat center = top + height * 0.5;
    const uint32_t pixels = std::max<uint32_t>(1u,
        static_cast<uint32_t>(rect.size.width - 24.0));
    NSBezierPath* trace = [NSBezierPath bezierPath];
    [trace setLineWidth:1.0];
    for (uint32_t x = 0u; x < pixels; ++x) {
        const uint32_t first = static_cast<uint32_t>(
            static_cast<uint64_t>(total) * x / pixels);
        const uint32_t last = std::min<uint32_t>(total,
            std::max(first + 1u, static_cast<uint32_t>(
                static_cast<uint64_t>(total) * (x + 1u) / pixels)));
        float minimum = 1.0f;
        float maximum = -1.0f;
        const uint32_t stride = std::max<uint32_t>(1u,
            (last - first) / 24u);
        for (uint32_t channel = 0u; channel < asset.channelCount; ++channel) {
            const auto& samples = asset.channels[channel];
            for (uint32_t frame = first; frame < last; frame += stride) {
                minimum = std::min(minimum, samples[frame]);
                maximum = std::max(maximum, samples[frame]);
            }
        }
        const CGFloat px = rect.origin.x + 12.0 + x;
        [trace moveToPoint:NSMakePoint(px,
            center - maximum * height * 0.43)];
        [trace lineToPoint:NSMakePoint(px,
            center - minimum * height * 0.43)];
    }
    [s3g::clap_gui::color(0x7b8780) setStroke];
    [trace stroke];
    for (uint16_t index = 0u; index < slot.sliceCount; ++index) {
        const auto& slice = slot.slices[index];
        const CGFloat x = rect.origin.x + 12.0
            + static_cast<CGFloat>(pixels) * slice.startFrame / total;
        [s3g::clap_gui::color(slice.reverse ? 0x9a6a72
                                            : 0x8e824f) setStroke];
        NSBezierPath* marker = [NSBezierPath bezierPath];
        [marker moveToPoint:NSMakePoint(x, top)];
        [marker lineToPoint:NSMakePoint(x, top + height)];
        [marker stroke];
    }
    for (std::size_t cursorIndex = 0u;
         cursorIndex < _instance->voicePlayheads.size(); ++cursorIndex) {
        if (_instance->voicePlayheadSlots[cursorIndex].load(
                std::memory_order_relaxed) != slotIndex) continue;
        const float playhead = _instance->voicePlayheads[cursorIndex].load(
            std::memory_order_relaxed);
        if (playhead < 0.0f) continue;
        const CGFloat x = rect.origin.x + 12.0
            + static_cast<CGFloat>(pixels) * playhead;
        NSColor* cursorColor = s3g::clap_gui::color(
            cursorIndex % 2u == 0u ? 0xa9d18e : 0x8fb8cf);
        [cursorColor setStroke];
        NSBezierPath* cursor = [NSBezierPath bezierPath];
        [cursor setLineWidth:2.0];
        [cursor moveToPoint:NSMakePoint(x, top)];
        [cursor lineToPoint:NSMakePoint(x, top + height)];
        [cursor stroke];
        const uint8_t key = _instance->voicePlayheadKeys[cursorIndex].load(
            std::memory_order_relaxed);
        NSString* flag = [NSString stringWithFormat:@"%@/%u",
            shortNoteText(key), static_cast<unsigned>(key)];
        NSDictionary* flagAttrs = slicerTextAttrs(
            s3g::clap_gui::color(0xd0d8d3), 7.5,
            NSFontWeightMedium);
        const NSSize flagSize = [flag sizeWithAttributes:flagAttrs];
        const CGFloat flagX = std::clamp<CGFloat>(x + 3.0,
            rect.origin.x + 2.0,
            NSMaxX(rect) - flagSize.width - 8.0);
        const NSRect flagRect = NSMakeRect(flagX, top + 2.0,
            flagSize.width + 6.0, flagSize.height + 3.0);
        [s3g::clap_gui::color(0x202522) setFill];
        NSRectFill(flagRect);
        [cursorColor setStroke];
        NSFrameRect(flagRect);
        [flag drawAtPoint:NSMakePoint(flagX + 3.0,
            flagRect.origin.y + 1.0) withAttributes:flagAttrs];
    }
    NSString* details = [NSString stringWithFormat:
        @"%u SLICES   %u CH   %.2f S   INSERTS %@ / %@   AUX %.0f%%",
        static_cast<unsigned>(slot.sliceCount),
        static_cast<unsigned>(asset.channelCount),
        static_cast<double>(asset.frameCount()) / asset.sampleRate,
        insertTypeShortText(slot.inserts[0u].type),
        insertTypeShortText(slot.inserts[1u].type),
        slot.mixerAuxSend * 100.0f];
    [details drawAtPoint:NSMakePoint(rect.origin.x + 12.0,
        NSMaxY(rect) - 21.0) withAttributes:label];
}

- (void)drawMutateForBank:(const BankSnapshot&)bank
{
    NSDictionary* label = slicerTextAttrs(
        s3g::clap_gui::color(0xa6aaa7), 10.0);
    NSDictionary* value = slicerTextAttrs(
        s3g::clap_gui::color(0xc6cbc8), 11.0,
        NSFontWeightMedium);
    const uint32_t slotIndex = [self selectedSlot];
    const auto& slot = bank.slots[slotIndex];
    [self drawMutateWaveformForSlot:slot];
    for (uint32_t index = 0u;
         index < s3g::breakbeat::kMaximumSampleSlots; ++index) {
        const NSRect rect = mutateSlotRect(index);
        const bool active = index == slotIndex;
        const auto& candidate = bank.slots[index];
        const bool pending = _instance->pendingMutationSlots[index];
        [s3g::clap_gui::color(active ? 0x292e2b : 0x171717) setFill];
        NSRectFill(rect);
        [s3g::clap_gui::color(active ? 0x6b7a70 : 0x444444) setStroke];
        NSFrameRectWithWidth(rect, active ? 2.0 : 1.0);
        [[NSString stringWithFormat:@"%u", index + 1u]
            drawAtPoint:NSMakePoint(NSMinX(rect) + 12.0,
                NSMinY(rect) + 10.0)
            withAttributes:slicerTextAttrs(
                s3g::clap_gui::color(active ? 0xc9d3cc : 0xa8aaa9),
                16.0, NSFontWeightSemibold)];
        NSString* slotState = pending ? @"BUILDING"
            : candidate.asset ? (active ? @"SOURCE" : @"LOCKED")
                              : @"AVAILABLE";
        [slotState drawAtPoint:NSMakePoint(NSMinX(rect) + 44.0,
            NSMinY(rect) + 12.0) withAttributes:value];
        NSString* filename = pending ? @"RENDERING"
            : candidate.asset ? slotFilename(*_instance, index) : @"EMPTY";
        drawCenteredTruncatedFilename(NSMakeRect(NSMinX(rect) + 12.0,
            NSMinY(rect) + 34.0, NSWidth(rect) - 24.0, 22.0),
            filename, label);
        if (active) {
            [@"SELECTED" drawAtPoint:NSMakePoint(NSMaxX(rect) - 68.0,
                NSMinY(rect) + 12.0)
                withAttributes:slicerTextAttrs(
                    s3g::clap_gui::color(0x9db1a4), 8.5,
                    NSFontWeightSemibold)];
        }
    }

    const uint32_t emptyCount = emptyMutationSlotCount(
        *_instance, slotIndex);
    NSString* fillTitle = emptyCount > 0u
        ? [NSString stringWithFormat:@"FILL %u EMPTY SLOT%@",
            emptyCount, emptyCount == 1u ? @"" : @"S"]
        : @"ALL OTHER SLOTS LOCKED";
    drawButton(mutateActionButtonRect(0u), fillTitle,
        slot.asset && emptyCount > 0u);
    drawButton(mutateActionButtonRect(1u), @"PLAY THROUGH",
        _instance->playthroughSlotTelemetry.load(
            std::memory_order_relaxed) == slotIndex + 1u);
    drawButton(mutateActionButtonRect(2u), @"CLEAR",
        slot.asset || _instance->pendingMutationSlots[slotIndex]);
    drawButton(mutateActionButtonRect(3u), @"EXPORT…",
        slot.asset != nullptr);

    const NSRect panel = mutateUsesPanelRect();
    s3g::clap_gui::Style style;
    s3g::clap_gui::drawPanelFrame(NSMinX(panel), NSMinY(panel),
        NSWidth(panel), NSHeight(panel), style);
    s3g::clap_gui::drawPanelHeader(@"USES", true,
        NSMinX(panel), NSMinY(panel), NSWidth(panel), 24.0, label, style);
    static NSArray<NSString*>* useNames = @[
        @"REARRANGE", @"REPEAT", @"PITCH", @"MIXER FX", @"AUX BUS",
        @"REVERSE"
    ];
    static constexpr std::array<uint8_t, 6u> useBits {{
        s3g::breakbeat::StructuralRearrange,
        s3g::breakbeat::StructuralRepeat,
        s3g::breakbeat::StructuralPitch,
        s3g::breakbeat::StructuralMixerFx,
        s3g::breakbeat::StructuralAuxBus,
        s3g::breakbeat::StructuralReverse,
    }};
    for (uint32_t index = 0u; index < useBits.size(); ++index) {
        drawButton(mutateUseRect(index), useNames[index],
            (_instance->structuralMutationUses & useBits[index]) != 0u);
    }
    [[NSString stringWithFormat:@"NEXT SEED %u",
        _instance->mutationSeed] drawAtPoint:NSMakePoint(
            NSMinX(panel) + 18.0, NSMinY(panel) + 68.0)
        withAttributes:value];
    [@"FX + AUX: PER SLICE" drawAtPoint:NSMakePoint(
        NSMaxX(panel) - 160.0, NSMinY(panel) + 69.0)
        withAttributes:label];
}

- (void)drawKeyboardForSlot:(const s3g::breakbeat::SampleSlot&)slot
{
    const NSRect keyboard = keyboardRect();
    constexpr uint32_t keyCount = 24u;
    const CGFloat width = keyboard.size.width / keyCount;
    NSDictionary* attrs = slicerTextAttrs(
        s3g::clap_gui::color(0xaeb3af), 8.5);
    for (uint32_t index = 0u; index < keyCount; ++index) {
        const uint32_t note = std::min<uint32_t>(127u,
            static_cast<uint32_t>(slot.rootNote) + index);
        const bool black = note % 12u == 1u || note % 12u == 3u
            || note % 12u == 6u || note % 12u == 8u || note % 12u == 10u;
        const bool mapped = index < slot.mappedSliceCount
            && slot.mappedRootNote == slot.rootNote;
        const uint8_t hitChannel = _instance->midiKeyChannels[note].load(
            std::memory_order_relaxed);
        const bool channelReceives = slot.midiChannel == 0u
            || hitChannel == 0u || slot.midiChannel == hitChannel;
        bool voiceActive = false;
        for (std::size_t cursor = 0u;
             cursor < _instance->voicePlayheads.size(); ++cursor) {
            if (_instance->voicePlayheadSlots[cursor].load(
                    std::memory_order_relaxed) == [self selectedSlot]
                && _instance->voicePlayheadKeys[cursor].load(
                    std::memory_order_relaxed) == note
                && _instance->voicePlayheads[cursor].load(
                    std::memory_order_relaxed) >= 0.0f) {
                voiceActive = true;
                break;
            }
        }
        const float hit = channelReceives
            ? _instance->midiKeyActivity[note].load(
                std::memory_order_relaxed) : 0.0f;
        const bool active = mapped && (voiceActive || hit > 0.06f);
        const NSRect key = NSMakeRect(keyboard.origin.x + index * width,
            keyboard.origin.y, width, keyboard.size.height);
        [s3g::clap_gui::color(active ? 0x526b59 : mapped ? 0x26352c
            : black ? 0x171717 : 0x292929) setFill];
        NSRectFill(key);
        [s3g::clap_gui::color(active ? 0xb6c9bb
                                     : mapped ? 0x59705f : 0x4a4a4a)
            setStroke];
        NSFrameRectWithWidth(key, active ? 2.0 : 1.0);
        if (active) {
            [s3g::clap_gui::color(0xb6c9bb) setFill];
            NSRectFill(NSMakeRect(NSMinX(key) + 3.0, NSMinY(key) + 2.0,
                std::max<CGFloat>(2.0,
                    (NSWidth(key) - 6.0) * std::max(hit, 0.35f)), 2.0));
        }
        drawCentered(NSMakeRect(key.origin.x, key.origin.y + 3.0,
            key.size.width, 17.0), shortNoteText(static_cast<uint8_t>(note)),
            attrs);
        NSDictionary* numberAttrs = slicerTextAttrs(
            s3g::clap_gui::color(active ? 0xdce5df
                : mapped ? 0x9fb4a5 : 0x858585), 8.0,
            NSFontWeightMedium);
        drawCentered(NSMakeRect(key.origin.x, key.origin.y + 23.0,
            key.size.width, 17.0),
            [NSString stringWithFormat:@"%u", note], numberAttrs);
    }
}

- (void)drawInsertEditorForBank:(const BankSnapshot&)bank
{
    if (_insertEditorSlot < 0
        || static_cast<std::size_t>(_insertEditorSlot) >= bank.slots.size()
        || _insertEditorIndex >= s3g::breakbeat::kInsertSlotsPerStrip)
        return;
    const uint32_t slotIndex = static_cast<uint32_t>(_insertEditorSlot);
    const auto& insert = bank.slots[slotIndex].inserts[_insertEditorIndex];
    const NSRect panel = mixerAuxBusRect();
    s3g::clap_gui::Style style;
    NSDictionary* label = slicerTextAttrs(
        s3g::clap_gui::color(0x929994), 9.0, NSFontWeightMedium);
    NSDictionary* value = slicerTextAttrs(
        s3g::clap_gui::color(0xc2c7c3), 10.0, NSFontWeightMedium);
    NSString* heading = [NSString stringWithFormat:
        @"BREAK %02u  //  INSERT %u  //  %@", slotIndex + 1u,
        _insertEditorIndex + 1u, insertTypeText(insert.type)];
    s3g::clap_gui::drawPanelFrame(NSMinX(panel), NSMinY(panel),
        NSWidth(panel), NSHeight(panel), style);
    s3g::clap_gui::drawPanelHeader(heading, true, NSMinX(panel),
        NSMinY(panel), NSWidth(panel), 24.0, label, style);

    static NSArray<NSString*>* deviceLabels = @[
        @"OFF", @"FLTR", @"DGRD", @"TRNS",
        @"RSNR", @"ERSN", @"SHIFT", @"FOLD", @"RPT", @"TIME"
    ];
    for (uint32_t type = 0u; type < deviceLabels.count; ++type)
        s3g::clap_gui::drawHeaderButton(mixerInsertTypeRect(type), panel,
            deviceLabels[type], static_cast<uint8_t>(insert.type) == type,
            value, style);
    s3g::clap_gui::drawHeaderButton(mixerInsertBypassRect(), panel,
        @"BYPASS", insert.bypassed, value, style);
    s3g::clap_gui::drawHeaderButton(mixerInsertSwapRect(), panel,
        @"SWAP I1/I2", false, value, style);
    s3g::clap_gui::drawHeaderButton(mixerInsertCloseRect(), panel,
        @"CLOSE", false, value, style);
    NSString* option = insert.type == InsertType::Filter
        ? [NSString stringWithFormat:@"MODE  %@",
            filterModeText(insert.mode)]
        : insert.type == InsertType::Erosion
            ? [NSString stringWithFormat:@"MODE  %@",
                insert.variant == 0u ? @"SINE" : @"NOISE"]
        : insert.type == InsertType::Shifter
            ? [NSString stringWithFormat:@"MODE  %@",
                insert.variant == 0u ? @"FREQUENCY" : @"RING"]
        : insert.type == InsertType::Wavefolder
            ? [NSString stringWithFormat:@"MODE  %@",
                insert.variant == 0u ? @"FOLD" : @"CLIP"]
        : insert.type == InsertType::Repeater
            ? [NSString stringWithFormat:@"MODE  %@",
                insert.variant == 0u ? @"FORWARD"
                    : insert.variant == 1u ? @"REVERSE" : @"ALTERNATE"]
        : insert.type == InsertType::TimeMangler
            ? [NSString stringWithFormat:@"MODE  %@",
                insert.variant == 0u ? @"REVERSE"
                    : insert.variant == 1u ? @"FREEZE" : @"TAPE"]
        : insert.type == InsertType::Off ? @"NO DEVICE ASSIGNED"
        : [NSString stringWithFormat:@"RESET  %@",
            insertTypeText(insert.type)];
    s3g::clap_gui::drawHeaderButton(mixerInsertOptionRect(), panel,
        option, insert.type == InsertType::Filter
            || insert.type == InsertType::Erosion
            || insert.type == InsertType::Shifter
            || insert.type == InsertType::Wavefolder
            || insert.type == InsertType::Repeater
            || insert.type == InsertType::TimeMangler, value, style);

    if (insert.type != InsertType::Off) {
        for (uint32_t parameter = 0u;
             parameter < insert.values.size(); ++parameter) {
            drawMixerInsertParameter(
                insertParameterLabel(insert, parameter),
                insertParameterValue(insert, parameter),
                insert.values[parameter], parameter, label, value, style);
        }
    } else {
        drawCentered(NSMakeRect(NSMinX(panel) + 16.0,
            NSMinY(panel) + 190.0, NSWidth(panel) - 32.0, 24.0),
            @"CHOOSE A POST-PLAYBACK DEVICE ABOVE", label);
    }
    [insertSafetyText(insert.type) drawAtPoint:NSMakePoint(
        NSMinX(panel) + 16.0, NSMinY(panel) + 465.0)
        withAttributes:label];
    [@"POST VOICES  →  I1  →  I2  →  EQ / PAN / FADER  →  AUX"
        drawAtPoint:NSMakePoint(NSMinX(panel) + 16.0,
            NSMinY(panel) + 487.0) withAttributes:label];
}

- (void)drawMixerForBank:(const BankSnapshot&)bank
{
    constexpr std::array<uint32_t, 4u> colors {{
        0x78918c, 0x9a826c, 0x817a99, 0x956f73,
    }};
    s3g::clap_gui::Style style;
    NSDictionary* label = slicerTextAttrs(
        s3g::clap_gui::color(0x929994), 9.0, NSFontWeightMedium);
    NSDictionary* value = slicerTextAttrs(
        s3g::clap_gui::color(0xc2c7c3), 10.0, NSFontWeightMedium);
    static constexpr const char* dialLabels[] {
        "PAN", "LOW", "MID", "FREQ", "HIGH",
    };
    for (uint32_t index = 0u; index < bank.slots.size(); ++index) {
        const auto& slot = bank.slots[index];
        const NSRect strip = mixerStripRect(index);
        const bool selected = index == [self selectedSlot];
        NSColor* accent = s3g::clap_gui::color(
            static_cast<int>(colors[index]));
        s3g::clap_gui::drawPanelFrame(NSMinX(strip), NSMinY(strip),
            NSWidth(strip), NSHeight(strip), style);
        s3g::clap_gui::drawPanelHeader(
            [NSString stringWithFormat:@"%02u  BREAK", index + 1u], selected,
            NSMinX(strip), NSMinY(strip), NSWidth(strip), 24.0, label, style);
        [accent setFill];
        NSRectFill(NSMakeRect(NSMinX(strip), NSMinY(strip),
            NSWidth(strip), 3.0));
        drawCenteredTruncatedFilename(NSMakeRect(NSMinX(strip) + 8.0,
            NSMinY(strip) + 30.0, NSWidth(strip) - 16.0, 16.0),
            slotFilename(*_instance, index), value);
        NSString* midiRoute = slot.midiChannel == 0u ? @"OMNI"
            : [NSString stringWithFormat:@"CH%02u",
                static_cast<unsigned>(slot.midiChannel)];
        NSString* mapping = slot.mappedSliceCount == 0u ? @"UNMAPPED"
            : [NSString stringWithFormat:@"%@+%u",
                shortNoteText(slot.mappedRootNote),
                static_cast<unsigned>(slot.mappedSliceCount)];
        NSString* route = [NSString stringWithFormat:@"%@  //  %@",
            midiRoute, mapping];
        drawCentered(NSMakeRect(NSMinX(strip) + 8.0, NSMinY(strip) + 50.0,
            NSWidth(strip) - 16.0, 15.0), route, label);

        NSString* panText = (!slot.asset || slot.asset->channelCount <= 2u)
            ? (std::abs(slot.mixerPan) < 0.01f ? @"C"
                : [NSString stringWithFormat:@"%c%02d",
                    slot.mixerPan < 0.0f ? 'L' : 'R',
                    static_cast<int>(std::lround(
                        std::abs(slot.mixerPan) * 100.0f))])
            : @"LOCK";
        const std::array<float, 5u> dialValues {{
            slot.mixerPan, slot.mixerLowEqDb, slot.mixerMidEqDb,
            slot.mixerMidFrequencyHz, slot.mixerHighEqDb,
        }};
        const std::array<CGFloat, 5u> dialNorms {{
            static_cast<CGFloat>((slot.mixerPan + 1.0f) * 0.5f),
            static_cast<CGFloat>((slot.mixerLowEqDb + 12.0f) / 24.0f),
            static_cast<CGFloat>((slot.mixerMidEqDb + 12.0f) / 24.0f),
            static_cast<CGFloat>(std::log(slot.mixerMidFrequencyHz / 120.0f)
                / std::log(8000.0f / 120.0f)),
            static_cast<CGFloat>((slot.mixerHighEqDb + 12.0f) / 24.0f),
        }};
        for (uint32_t dial = 0u; dial < 5u; ++dial) {
            NSString* text = nil;
            if (dial == 0u) text = panText;
            else if (dial == 3u) text = dialValues[dial] >= 1000.0f
                ? [NSString stringWithFormat:@"%.1fk",
                    dialValues[dial] / 1000.0f]
                : [NSString stringWithFormat:@"%.0f", dialValues[dial]];
            else text = [NSString stringWithFormat:@"%+.1f",
                dialValues[dial]];
            s3g::clap_gui::drawDial(
                [NSString stringWithUTF8String:dialLabels[dial]], text,
                dialNorms[dial], mixerDialRect(index, dial),
                label, value, style);
        }
        for (uint32_t insertIndex = 0u;
             insertIndex < slot.inserts.size(); ++insertIndex) {
            const auto& insert = slot.inserts[insertIndex];
            NSString* insertLabel = insert.bypassed
                && insert.type != InsertType::Off
                ? [NSString stringWithFormat:@"I%u BYP", insertIndex + 1u]
                : [NSString stringWithFormat:@"I%u %@", insertIndex + 1u,
                    insertTypeShortText(insert.type)];
            const NSRect rect = mixerInsertRect(index, insertIndex);
            s3g::clap_gui::drawHeaderButton(rect, strip, insertLabel,
                insert.type != InsertType::Off && !insert.bypassed,
                value, style);
            if (_insertEditorSlot == static_cast<NSInteger>(index)
                && _insertEditorIndex == insertIndex) {
                [accent setStroke];
                NSBezierPath* selection = [NSBezierPath
                    bezierPathWithRect:NSInsetRect(rect, -1.0, -1.0)];
                selection.lineWidth = 2.0;
                [selection stroke];
            }
        }

        s3g::clap_gui::drawMixerStripSlider(@"AUX",
            [NSString stringWithFormat:@"%.0f%%", slot.mixerAuxSend * 100.0f],
            slot.mixerAuxSend, mixerAuxY(index), NSMinX(strip),
            NSWidth(strip), label, value, style);
        s3g::clap_gui::drawHeaderButton(mixerMuteRect(index), strip, @"MUTE",
            slot.muted, value, style);
        s3g::clap_gui::drawHeaderButton(mixerSoloRect(index), strip, @"SOLO",
            slot.solo, value, style);
        s3g::clap_gui::drawHeaderButton(mixerAuditionRect(index), strip,
            @"AUDITION", false, value, style);

        const NSRect fader = mixerFaderRect(index);
        [@"LVL" drawAtPoint:NSMakePoint(NSMinX(strip) + 16.0,
            NSMinY(fader) - 27.0) withAttributes:label];
        const double levelDb = gainDb(slot.mixerGain);
        const CGFloat levelNorm = std::clamp<CGFloat>(
            static_cast<CGFloat>((levelDb + 60.0) / 66.0), 0.0, 1.0);
        s3g::clap_gui::drawMixerFader(levelNorm, fader, style);
        drawCentered(NSMakeRect(NSMinX(strip) + 10.0, NSMaxY(fader) + 8.0,
            NSWidth(strip) - 20.0, 16.0), slot.mixerGain <= 0.001f
                ? @"-INF dB" : [NSString stringWithFormat:@"%+.1f dB",
                    levelDb], value);
        const float peak = _instance->slotPeaks[index].load(
            std::memory_order_relaxed);
        const float peakDb = 20.0f
            * std::log10(std::max(peak, 0.000001f));
        s3g::clap_gui::drawVerticalVuMeter(std::clamp<CGFloat>(
            (peakDb + 60.0f) / 60.0f, 0.0f, 1.0f),
            mixerPeakRect(index), style);
    }

    const NSRect main = mixerMainRect();
    s3g::clap_gui::drawPanelFrame(NSMinX(main), NSMinY(main),
        NSWidth(main), NSHeight(main), style);
    s3g::clap_gui::drawPanelHeader(@"OUTPUT", true, NSMinX(main),
        NSMinY(main), NSWidth(main), 24.0, label, style);
    const double outputDb = _instance->outputGainDb.load(
        std::memory_order_relaxed);
    s3g::clap_gui::drawMixerStripSlider(@"OUT",
        [NSString stringWithFormat:@"%+.1f dB", outputDb],
        (outputDb + 60.0) / 72.0, mixerOutputSliderY(),
        NSMinX(main), NSWidth(main), label, value, style);
    [[NSString stringWithFormat:@"FIXED %u-CHANNEL PASS-THROUGH",
        static_cast<unsigned>(_instance->outputChannelCount)]
        drawAtPoint:NSMakePoint(NSMinX(main) + 16.0, NSMinY(main) + 88.0)
        withAttributes:value];
    s3g::clap_gui::drawHeaderButton(mixerUnityRect(), main, @"UNITY",
        std::abs(outputDb) < 0.01, value, style);

    if (_insertEditorSlot >= 0
        && static_cast<std::size_t>(_insertEditorSlot) < bank.slots.size()) {
        [self drawInsertEditorForBank:bank];
        return;
    }

    const NSRect bus = mixerAuxBusRect();
    s3g::clap_gui::drawPanelFrame(NSMinX(bus), NSMinY(bus),
        NSWidth(bus), NSHeight(bus), style);
    s3g::clap_gui::drawPanelHeader(@"s3g BREAK BUS", true,
        NSMinX(bus), NSMinY(bus), NSWidth(bus), 24.0, label, style);
    s3g::clap_gui::drawHeaderButton(mixerBusEnableRect(), bus, @"AUX BUS",
        bank.auxEnabled, value, style);
    static constexpr const char* busLabels[] {
        "PRS", "SNP", "RCV", "SAT", "BIT", "CLP", "TLT", "RET",
    };
    const std::array<float, 8u> busValues {{
        bank.auxPress, bank.auxSnap, bank.auxRecovery,
        bank.auxSaturation, bank.auxBite, bank.auxClip,
        bank.auxTilt, bank.auxReturnDb,
    }};
    const std::array<CGFloat, 8u> busNorms {{
        bank.auxPress, (bank.auxSnap + 1.0f) * 0.5f, bank.auxRecovery,
        bank.auxSaturation, bank.auxBite, bank.auxClip,
        (bank.auxTilt + 1.0f) * 0.5f,
        (bank.auxReturnDb + 60.0f) / 72.0f,
    }};
    s3g::clap_gui::Style bypassStyle = style;
    bypassStyle.strip = s3g::clap_gui::color(0x101010);
    bypassStyle.grid = s3g::clap_gui::color(0x303030);
    bypassStyle.text = s3g::clap_gui::color(0x5d625f);
    bypassStyle.fill = s3g::clap_gui::color(0x343735);
    NSDictionary* bypassLabel = slicerTextAttrs(
        s3g::clap_gui::color(0x626662), 9.0, NSFontWeightMedium);
    NSDictionary* bypassValue = slicerTextAttrs(
        s3g::clap_gui::color(0x686c69), 10.0, NSFontWeightMedium);
    for (uint32_t row = 0u; row < busValues.size(); ++row) {
        const bool bypassed = bank.auxFieldSafe
            && row >= 3u && row <= 5u;
        NSString* text = bypassed ? @"BYP" : row == 7u
            ? [NSString stringWithFormat:@"%+.1f", busValues[row]]
            : row == 1u || row == 6u
                ? [NSString stringWithFormat:@"%+.2f", busValues[row]]
                : [NSString stringWithFormat:@"%.0f%%",
                    busValues[row] * 100.0f];
        s3g::clap_gui::drawMixerStripSlider(
            [NSString stringWithUTF8String:busLabels[row]], text,
            busNorms[row], mixerBusSliderY(row), NSMinX(bus), NSWidth(bus),
            bypassed ? bypassLabel : label,
            bypassed ? bypassValue : value,
            bypassed ? bypassStyle : style);
    }
    const float activity = _instance->auxActivity.load(
        std::memory_order_relaxed);
    const float reduction = _instance->auxGainReductionDb.load(
        std::memory_order_relaxed);
    [[NSString stringWithFormat:@"BUS %.0f%%  //  GR %+.1f dB",
        std::min(activity, 1.0f) * 100.0f, reduction]
        drawAtPoint:NSMakePoint(NSMinX(bus) + 16.0, NSMinY(bus) + 438.0)
        withAttributes:value];
    [@"POST-FADER WET RETURN  //  NO LANE REORDERING"
        drawAtPoint:NSMakePoint(NSMinX(bus) + 16.0, NSMinY(bus) + 458.0)
        withAttributes:label];
    NSString* link = bank.auxLinkMode == s3g::BreakBusLinkMode::All
        ? @"LINK ALL" : bank.auxLinkMode == s3g::BreakBusLinkMode::Pair
            ? @"LINK PAIR" : @"LINK FREE";
    s3g::clap_gui::drawHeaderButton(mixerBusLinkRect(), bus, link,
        true, value, style);
    s3g::clap_gui::drawHeaderButton(mixerBusFieldSafeRect(), bus,
        @"FIELD SAFE", bank.auxFieldSafe, value, style);
}

- (void)editSelectedSliceControl:(uint32_t)index
{
    auto bank = editableBank(*_instance);
    auto& slot = bank->slots[[self selectedSlot]];
    if (!slot.asset || slot.sliceCount == 0u) return;
    if (!slotHasCompleteMap(slot)) {
        _instance->status =
            "AUTO MAP BEFORE EDITING SLICE PROPERTIES";
        return;
    }
    const std::size_t sliceIndex = static_cast<std::size_t>(
        std::clamp<NSInteger>(_selectedSlice, 0,
            static_cast<NSInteger>(slot.sliceCount) - 1));
    auto& slice = slot.slices[sliceIndex];
    switch (index) {
    case 0u: slice.gain = std::max(0.0f, slice.gain - 0.05f); break;
    case 1u: slice.gain = std::min(4.0f, slice.gain + 0.05f); break;
    case 2u:
        slice.transposeSemitones = std::max(-96.0f,
            slice.transposeSemitones - 1.0f);
        break;
    case 3u:
        slice.transposeSemitones = std::min(96.0f,
            slice.transposeSemitones + 1.0f);
        break;
    case 4u:
        if (slot.asset->channelCount <= 2u)
            slice.pan = std::max(-1.0f, slice.pan - 0.1f);
        break;
    case 5u:
        if (slot.asset->channelCount <= 2u)
            slice.pan = std::min(1.0f, slice.pan + 0.1f);
        break;
    case 6u: slice.reverse = !slice.reverse; break;
    case 7u: {
        constexpr uint32_t modeCount = 5u;
        const uint32_t next = (static_cast<uint32_t>(slice.launchMode) + 1u)
            % modeCount;
        slice.launchMode = static_cast<s3g::breakbeat::LaunchMode>(next);
        break;
    }
    case 8u:
        slice.chokeGroup = static_cast<uint8_t>(
            std::max(0, static_cast<int>(slice.chokeGroup) - 1));
        break;
    case 9u:
        slice.chokeGroup = static_cast<uint8_t>(
            std::min(16, static_cast<int>(slice.chokeGroup) + 1));
        break;
    default: return;
    }
    const bool multichannelPanBypass = slot.asset->channelCount > 2u
        && (index == 4u || index == 5u);
    if (publishBank(*_instance, std::move(bank)))
        _instance->status = multichannelPanBypass
            ? "PAN IS BYPASSED FOR MULTICHANNEL SOURCES"
            : "SLICE PROPERTY UPDATED";
}

- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    const s3g::clap_gui::Style style = s3g::clap_gui::softTextStyle();
    [style.bg setFill];
    NSRectFill(self.bounds);
    [self updateDetailNumericFields];
    // Match the resolved 17.5 pt header used by "s3g TRACKER". Slicer's
    // shared font helper uses a slightly different readability scale, so a
    // 16 pt design size produces the same rendered title size here.
    NSDictionary* title = slicerTextAttrs(
        s3g::clap_gui::color(0xc8c8c8), 16.0);
    NSDictionary* label = s3g::clap_gui::softLabelAttrs();
    NSDictionary* value = s3g::clap_gui::softValueAttrs();
    [@"s3g SAMPLE SLICER" drawAtPoint:NSMakePoint(18.0, 17.0)
        withAttributes:title];
    NSString* variant = [NSString stringWithFormat:@"%u OUT",
        static_cast<unsigned>(_instance->outputChannelCount)];
    [variant drawAtPoint:NSMakePoint(210.0, 21.0) withAttributes:label];
    NSString* status = [NSString stringWithUTF8String:
        _instance->status.c_str()];
    if (!status) status = @"";
    s3g::clap_gui::drawRightStatus(status, kGuiWidth, 22.0, label);
    NSString* section = _page == 2u ? @"INTERNAL BREAK MIXER"
        : _page == 3u ? @"MUTATE" : @"FOUR BREAK BANK";
    [section drawAtPoint:NSMakePoint(18.0, 51.0) withAttributes:label];
    NSDictionary* compact = s3g::clap_gui::textAttrs(
        s3g::clap_gui::color(0xb8b8b8), 8.5);
    const NSRect navigationBand = NSMakeRect(282.0, 42.0, 780.0, 24.0);
    s3g::clap_gui::drawHeaderButton(editorTabRect(0u), navigationBand,
        @"OVERVIEW", _page == 0u, label, style);
    s3g::clap_gui::drawHeaderButton(editorTabRect(1u), navigationBand,
        @"BREAK EDIT", _page == 1u, label, style);
    s3g::clap_gui::drawHeaderButton(editorTabRect(2u), navigationBand,
        @"MIXER", _page == 2u, label, style);
    s3g::clap_gui::drawHeaderButton(editorTabRect(3u), navigationBand,
        @"MUTATE", _page == 3u, label, style);
    uint64_t projectAudioBytes = 0u;
    if (_instance->controlBank) {
        for (const auto& slot : _instance->controlBank->slots) {
            if (!slot.asset) continue;
            projectAudioBytes += static_cast<uint64_t>(
                slot.asset->channelCount) * slot.asset->frameCount()
                * sizeof(float);
        }
    }
    NSString* projectAudioLabel = nil;
    if (!_instance->embedSamplesInState) {
        projectAudioLabel = @"PROJECT AUDIO: PATHS";
    } else if (projectAudioBytes > kMaximumEmbeddedAudioBytes) {
        projectAudioLabel = @"PROJECT AUDIO: PARTIAL >1 GB";
    } else {
        projectAudioLabel = [NSString stringWithFormat:
            @"PROJECT AUDIO: EMBED %.1f MB",
            static_cast<double>(projectAudioBytes) / (1024.0 * 1024.0)];
    }
    s3g::clap_gui::drawHeaderButton(killAllButtonRect(), navigationBand,
        @"KILL ALL", false, compact, style);
    s3g::clap_gui::drawHeaderButton(embedAudioButtonRect(),
        navigationBand, projectAudioLabel,
        _instance->embedSamplesInState, compact, style);

    const auto* bank = [self displayBank];
    if (!bank) return;
    if (_page != 2u) {
        const NSRect bankPanel = bankPanelRect();
        s3g::clap_gui::drawPanelFrame(NSMinX(bankPanel), NSMinY(bankPanel),
            NSWidth(bankPanel), NSHeight(bankPanel), style);
        s3g::clap_gui::drawPanelHeader(@"BREAK BANK", true,
            NSMinX(bankPanel), NSMinY(bankPanel), NSWidth(bankPanel),
            static_cast<CGFloat>(
                s3g::gui_layout::kStandardMetrics.headerHeight),
            label, style);
    }
    if (_page != 2u) for (uint32_t index = 0u;
         index < s3g::breakbeat::kMaximumSampleSlots; ++index) {
        const NSRect row = bankRowRect(index);
        const bool selected = index == [self selectedSlot];
        [s3g::clap_gui::color(selected ? 0x29282b : 0x191919) setFill];
        NSRectFill(row);
        if (selected) {
            [s3g::clap_gui::color(0x655a68) setFill];
            NSRectFill(NSMakeRect(row.origin.x, row.origin.y, 3.0,
                row.size.height));
        }
        [s3g::clap_gui::color(0x484848) setStroke];
        NSFrameRect(row);
        [[NSString stringWithFormat:@"BREAK %u", index + 1u]
            drawAtPoint:NSMakePoint(row.origin.x + 8.0, row.origin.y + 7.0)
            withAttributes:value];
        NSString* filename = slotFilename(*_instance, index);
        NSRect nameRect = NSMakeRect(row.origin.x + 72.0, row.origin.y + 6.0,
            156.0, 17.0);
        [filename drawInRect:nameRect withAttributes:label];
        const auto& slot = bank->slots[index];
        NSString* meta = slot.asset
            ? [NSString stringWithFormat:
                @"%u SL  %uCH  START %@/%u  MAX %zu",
                slot.sliceCount,
                static_cast<unsigned>(slot.asset->channelCount),
                shortNoteText(slot.rootNote),
                static_cast<unsigned>(slot.rootNote),
                s3g::breakbeat::maximumSlicesForStartNote(slot.rootNote)]
            : @"";
        [meta drawAtPoint:NSMakePoint(row.origin.x + 8.0,
            row.origin.y + 33.0) withAttributes:compact];
        NSString* channel = slot.midiChannel == 0u ? @"MIDI OMNI"
            : [NSString stringWithFormat:@"MIDI CH %u",
                static_cast<unsigned>(slot.midiChannel)];
        drawCompactMenu(slotChannelRect(index), channel, selected, value,
            style);
        const bool mapped = slotHasCompleteMap(slot);
        NSString* mapLabel = mapped
            ? [NSString stringWithFormat:@"MAPPED %@ +%u",
                shortNoteText(slot.mappedRootNote),
                static_cast<unsigned>(slot.mappedSliceCount - 1u)]
            : @"AUTO MAP";
        s3g::clap_gui::drawHeaderButton(slotAutomapRect(index), row,
            mapLabel, mapped, compact, style);
    }

    const auto& slot = bank->slots[[self selectedSlot]];
    if (_page == 2u) {
        [self drawMixerForBank:*bank];
    } else if (_page == 0u) {
        for (uint32_t index = 0u;
             index < s3g::breakbeat::kMaximumSampleSlots; ++index)
            [self drawOverviewWaveformForSlot:bank->slots[index]
                index:index];
        NSString* overviewHelp = @"CLICK A BREAK TO SELECT   DOUBLE-CLICK TO EDIT   DROP AUDIO ON ANY BREAK   AUTO MAP AFTER SLICE CHANGES";
        [overviewHelp drawAtPoint:NSMakePoint(292.0, 620.0)
            withAttributes:label];
    } else if (_page == 3u) {
        [self drawMutateForBank:*bank];
    } else {
        const std::array<std::pair<NSRect, NSString*>, 5u> toolboxes {{
            { sampleToolboxRect(), @"SAMPLE / SLICE" },
            { sliceMapToolboxRect(), @"SLICE / MAP" },
            { playbackToolboxRect(), @"MAPPED SLICE PLAYBACK" },
            { selectedSliceToolboxRect(), @"SELECTED SLICE" },
            { timingToolboxRect(), @"TIMING" },
        }};
        for (const auto& toolbox : toolboxes) {
            s3g::clap_gui::drawPanelFrame(NSMinX(toolbox.first),
                NSMinY(toolbox.first), NSWidth(toolbox.first),
                NSHeight(toolbox.first), style);
            s3g::clap_gui::drawPanelHeader(toolbox.second, true,
                NSMinX(toolbox.first), NSMinY(toolbox.first),
                NSWidth(toolbox.first), static_cast<CGFloat>(
                    s3g::gui_layout::kStandardMetrics.headerHeight),
                label, style);
        }
        s3g::clap_gui::drawHeaderButton(sampleLoadButtonRect(),
            sampleToolboxRect(), @"LOAD", false, label, style);
        s3g::clap_gui::drawHeaderButton(sampleClearButtonRect(),
            sampleToolboxRect(), @"CLEAR", false, label, style);
        s3g::clap_gui::drawHeaderButton(sliceActionButtonRect(),
            sliceMapToolboxRect(), @"SLICE", false, label, style);
        s3g::clap_gui::drawHeaderButton(automapButtonRect(),
            sliceMapToolboxRect(), @"AUTO MAP", slotHasCompleteMap(slot),
            label, style);
        [self drawWaveformForSlot:slot];
        const bool mapped = slotHasCompleteMap(slot);
        static constexpr const char* slicingNames[] {
            "EQUAL 4", "EQUAL 8", "EQUAL 16", "EQUAL 32", "TRANSIENT",
        };
        const NSInteger slicingIndex = std::clamp<NSInteger>(
            _sliceModeSelection, 0, 4);
        s3g::clap_gui::drawProcessorMenu(@"METHOD",
            [NSString stringWithUTF8String:slicingNames[slicingIndex]],
            448.0, NSMinX(sliceMapToolboxRect()),
            NSWidth(sliceMapToolboxRect()), label, value, style);
        s3g::clap_gui::drawProcessorSlider(@"PRE-ROLL US",
            [NSString stringWithFormat:@"%u",
                _instance->transientPreRollMicroseconds],
            static_cast<CGFloat>(_instance->transientPreRollMicroseconds)
                / static_cast<CGFloat>(kMaximumTransientPreRollMicroseconds),
            detailNumericY(kDetailNumericPreRoll),
            NSMinX(sliceMapToolboxRect()), NSWidth(sliceMapToolboxRect()),
            label, value, style);
        s3g::clap_gui::drawProcessorSlider(@"MIN SLICE MS",
            [NSString stringWithFormat:@"%u",
                _instance->minimumTransientSliceMilliseconds],
            static_cast<CGFloat>(
                _instance->minimumTransientSliceMilliseconds)
                / static_cast<CGFloat>(
                    kMaximumMinimumTransientSliceMilliseconds),
            detailNumericY(kDetailNumericMinimumSlice),
            NSMinX(sliceMapToolboxRect()), NSWidth(sliceMapToolboxRect()),
            label, value, style);
        s3g::clap_gui::drawProcessorMenu(@"START NOTE",
            noteText(slot.rootNote), 526.0, NSMinX(sliceMapToolboxRect()),
            NSWidth(sliceMapToolboxRect()), label, value, style);

        s3g::clap_gui::drawProcessorMenu(@"TRIGGER",
            triggerModeText(slot.triggerMode), 448.0,
            NSMinX(playbackToolboxRect()), NSWidth(playbackToolboxRect()),
            label, value, style);
        s3g::clap_gui::drawProcessorMenu(@"RETRIGGER",
            retriggerModeText(slot.retriggerMode), 474.0,
            NSMinX(playbackToolboxRect()), NSWidth(playbackToolboxRect()),
            label, value, style);
        s3g::clap_gui::drawProcessorMenu(@"VOICE MODE",
            voiceModeText(slot.voiceMode), 500.0,
            NSMinX(playbackToolboxRect()), NSWidth(playbackToolboxRect()),
            label, value, style);
        s3g::clap_gui::drawProcessorMenu(@"PITCH MODE",
            pitchModeText(slot.pitchMode), 526.0,
            NSMinX(playbackToolboxRect()), NSWidth(playbackToolboxRect()),
            label, value, style);
        s3g::clap_gui::drawProcessorMenu(@"TEMPO SYNC",
            syncModeText(slot.syncMode), 552.0,
            NSMinX(playbackToolboxRect()), NSWidth(playbackToolboxRect()),
            label, value, style);

        s3g::clap_gui::drawProcessorSlider(@"SAMPLE BPM",
            [NSString stringWithFormat:@"%.1f", slot.sourceTempoBpm],
            static_cast<CGFloat>((slot.sourceTempoBpm - 20.0f) / 979.0f),
            detailNumericY(kDetailNumericSourceTempo),
            NSMinX(timingToolboxRect()), NSWidth(timingToolboxRect()),
            label, value, style);
        s3g::clap_gui::drawProcessorSlider(@"XFADE %",
            [NSString stringWithFormat:@"%.1f",
                slot.loopCrossfade * 100.0f],
            static_cast<CGFloat>(slot.loopCrossfade / 0.5f),
            detailNumericY(kDetailNumericLoopCrossfade),
            NSMinX(timingToolboxRect()), NSWidth(timingToolboxRect()),
            label, value, style);
        s3g::clap_gui::drawProcessorSlider(@"GLIDE MS",
            [NSString stringWithFormat:@"%.1f",
                slot.glideSeconds * 1000.0f],
            static_cast<CGFloat>(slot.glideSeconds / 2.0f),
            detailNumericY(kDetailNumericGlide),
            NSMinX(timingToolboxRect()), NSWidth(timingToolboxRect()),
            label, value, style);

        if (slot.asset && slot.sliceCount > 0u) {
            const double seconds = slot.asset->frameCount()
                / slot.asset->sampleRate;
            NSString* details = [NSString stringWithFormat:
                @"%u CH   %.2f S   %.0f HZ   %u SLICES   START %@   MAX %zu   ZOOM %.1fX",
                static_cast<unsigned>(slot.asset->channelCount), seconds,
                slot.asset->sampleRate, slot.sliceCount,
                noteText(slot.rootNote),
                s3g::breakbeat::maximumSlicesForStartNote(slot.rootNote),
                _zoom];
            [details drawAtPoint:NSMakePoint(294.0, 365.0)
                withAttributes:value];
            [@"DRAG MARKERS / LS / LE   SCROLL ZOOM   SHIFT-SCROLL PAN   DOUBLE-CLICK ADD"
                drawAtPoint:NSMakePoint(294.0, 383.0)
                withAttributes:label];
            const std::size_t selected = static_cast<std::size_t>(
                std::clamp<NSInteger>(_selectedSlice, 0,
                    static_cast<NSInteger>(slot.sliceCount) - 1));
            const auto& slice = slot.slices[selected];
            const NSRect envelopePanel = envelopePanelRect();
            s3g::clap_gui::drawPanelFrame(NSMinX(envelopePanel),
                NSMinY(envelopePanel), NSWidth(envelopePanel),
                NSHeight(envelopePanel), style);
            s3g::clap_gui::drawPanelHeader(@"AMP ENVELOPE", true,
                NSMinX(envelopePanel), NSMinY(envelopePanel),
                NSWidth(envelopePanel), static_cast<CGFloat>(
                    s3g::gui_layout::kStandardMetrics.headerHeight),
                label, style);
            [@"ALL SLICES  //  A D R SCALE TO LENGTH"
                drawAtPoint:NSMakePoint(NSMinX(envelopePanel) + 16.0,
                    NSMinY(envelopePanel) + 31.0)
                withAttributes:slicerTextAttrs(
                    s3g::clap_gui::color(0x888e8a), 8.0,
                    NSFontWeightMedium)];
            const std::array<float, 4u> envelopeValues {{
                slot.envelope.attackProportion,
                slot.envelope.decayProportion,
                slot.envelope.sustain,
                slot.envelope.releaseProportion,
            }};
            static constexpr const char* envelopeLabels[] {
                "A", "D", "S", "R",
            };
            for (uint32_t index = 0u; index < envelopeValues.size(); ++index) {
                s3g::clap_gui::drawMixerStripSlider(
                    [NSString stringWithUTF8String:envelopeLabels[index]],
                    [NSString stringWithFormat:@"%.1f%%",
                        envelopeValues[index] * 100.0f],
                    envelopeValues[index], envelopeSliderY(index),
                    NSMinX(envelopePanel), NSWidth(envelopePanel), label,
                    value, style);
            }
            if (mapped) {
                NSString* selectedStatus = [NSString stringWithFormat:
                    @"%03u / %@ / %@", static_cast<unsigned>(selected),
                    noteText(static_cast<uint8_t>(slot.mappedRootNote
                        + selected)), launchModeText(slice.launchMode)];
                s3g::clap_gui::drawBoundedRightText(selectedStatus,
                    NSMakeRect(500.0, 594.0, 278.0, 14.0), compact);
                static constexpr const char* propertyNames[] {
                    "GAIN", "PITCH", "PAN",
                };
                const std::array<NSString*, 3u> propertyValues {{
                    [NSString stringWithFormat:@"%.3f", slice.gain],
                    [NSString stringWithFormat:@"%+.2f",
                        slice.transposeSemitones],
                    slot.asset->channelCount > 2u ? @"N/A"
                        : [NSString stringWithFormat:@"%+.3f", slice.pan],
                }};
                const std::array<CGFloat, 3u> propertyNormalized {{
                    static_cast<CGFloat>(slice.gain / 4.0f),
                    static_cast<CGFloat>(std::clamp(
                        (slice.transposeSemitones + 24.0f) / 48.0f,
                        0.0f, 1.0f)),
                    static_cast<CGFloat>((slice.pan + 1.0f) * 0.5f),
                }};
                for (uint32_t property = 0u; property < 3u; ++property) {
                    s3g::clap_gui::drawMixerStripSlider(
                        [NSString stringWithUTF8String:
                            propertyNames[property]],
                        propertyValues[property], propertyNormalized[property],
                        kSlicePropertySliderY,
                        slicePropertyGroupX(property),
                        kSlicePropertyGroupWidth, compact, compact, style);
                }
                [@"LAUNCH" drawAtPoint:NSMakePoint(296.0, 650.0)
                    withAttributes:label];
                drawCompactMenu(sliceLaunchMenuRect(),
                    launchModeText(slice.launchMode), false, value, style);
                s3g::clap_gui::drawHeaderButton(sliceReverseButtonRect(),
                    selectedSliceToolboxRect(), @"REVERSE", slice.reverse,
                    compact, style);
                [@"CHOKE" drawAtPoint:NSMakePoint(620.0, 650.0)
                    withAttributes:label];
                drawCompactMenu(sliceChokeMenuRect(),
                    slice.chokeGroup == 0u ? @"OFF"
                        : [NSString stringWithFormat:@"GROUP %u",
                            static_cast<unsigned>(slice.chokeGroup)],
                    slice.chokeGroup != 0u, value, style);
            } else {
                drawLockedControlBar(NSMakeRect(294.0, 620.0, 486.0, 50.0),
                    @"LOCKED — SET START, THEN PRESS AUTO MAP");
            }
            NSString* help = mapped
                ? @"SELECT OR AUDITION A MAPPED NOTE"
                : @"WORKFLOW:  1 SLICE + EDIT MARKERS   →   2 SET START + AUTO MAP   →   3 EDIT / AUDITION";
            [help drawAtPoint:NSMakePoint(282.0, 690.0)
                withAttributes:label];
            [self drawKeyboardForSlot:slot];
        }
    }
    const float peak = _instance->outputPeak.load(std::memory_order_relaxed);
    NSString* outputWidth = [NSString stringWithFormat:@"OUT %u CH FIXED",
        static_cast<unsigned>(_instance->outputChannelCount)];
    NSString* output = [NSString stringWithFormat:
        @"%@ / %+.1f DB   VEL %.0f%%   PER-BREAK MIDI   %@",
        outputWidth,
        _instance->outputGainDb.load(std::memory_order_relaxed),
        _instance->velocitySensitivity.load(std::memory_order_relaxed) * 100.0,
        s3g::clap_gui::peakDbText(peak)];
    [output drawAtPoint:NSMakePoint(292.0, 775.0) withAttributes:label];

    if (_detailMenuKind != kDetailMenuNone) {
        const uint32_t count = [self detailMenuItemCount];
        std::array<NSString*, 128u> items {};
        int selected = -1;
        for (uint32_t index = 0u; index < count; ++index) {
            switch (_detailMenuKind) {
            case kDetailMenuMidiChannel:
                items[index] = index == 0u ? @"OMNI"
                    : [NSString stringWithFormat:@"CHANNEL %u", index];
                break;
            case kDetailMenuSliceMethod:
                items[index] = kSlicerMethodItems[index];
                break;
            case kDetailMenuStartNote:
                items[index] = noteText(static_cast<uint8_t>(index));
                break;
            case kDetailMenuTrigger:
                items[index] = kSlicerTriggerItems[index];
                break;
            case kDetailMenuRetrigger:
                items[index] = kSlicerRetriggerItems[index];
                break;
            case kDetailMenuVoice:
                items[index] = kSlicerVoiceItems[index];
                break;
            case kDetailMenuPitch:
                items[index] = kSlicerPitchItems[index];
                break;
            case kDetailMenuSync:
                items[index] = kSlicerSyncItems[index];
                break;
            case kDetailMenuSliceLaunch:
                items[index] = kSlicerLaunchItems[index];
                break;
            case kDetailMenuSliceChoke:
                items[index] = index == 0u ? @"OFF"
                    : [NSString stringWithFormat:@"GROUP %u", index];
                break;
            default: break;
            }
        }
        if (_detailMenuKind == kDetailMenuMidiChannel
            && _detailMenuSlot < bank->slots.size()) {
            selected = bank->slots[_detailMenuSlot].midiChannel;
        } else if (_detailMenuKind == kDetailMenuSliceMethod) {
            selected = static_cast<int>(_sliceModeSelection);
        } else if (_detailMenuKind == kDetailMenuStartNote) {
            selected = slot.rootNote;
        } else if (_detailMenuKind == kDetailMenuTrigger) {
            selected = static_cast<int>(slot.triggerMode);
        } else if (_detailMenuKind == kDetailMenuRetrigger) {
            selected = static_cast<int>(slot.retriggerMode);
        } else if (_detailMenuKind == kDetailMenuVoice) {
            selected = static_cast<int>(slot.voiceMode);
        } else if (_detailMenuKind == kDetailMenuPitch) {
            selected = static_cast<int>(slot.pitchMode);
        } else if (_detailMenuKind == kDetailMenuSync) {
            selected = static_cast<int>(slot.syncMode);
        } else if ((_detailMenuKind == kDetailMenuSliceLaunch
                || _detailMenuKind == kDetailMenuSliceChoke)
            && slotHasCompleteMap(slot) && slot.sliceCount > 0u) {
            const std::size_t sliceIndex = static_cast<std::size_t>(
                std::clamp<NSInteger>(_selectedSlice, 0,
                    static_cast<NSInteger>(slot.sliceCount) - 1));
            selected = _detailMenuKind == kDetailMenuSliceLaunch
                ? static_cast<int>(slot.slices[sliceIndex].launchMode)
                : static_cast<int>(slot.slices[sliceIndex].chokeGroup);
        }
        const uint32_t columns = detailMenuColumns(_detailMenuKind);
        if (columns > 1u) {
            s3g::clap_gui::drawMultiColumnDropdownMenu(
                [self detailMenuDropdownRect], 20.0, items.data(), count,
                columns, selected, static_cast<int>(_detailMenuHover),
                value, style);
        } else {
            s3g::clap_gui::drawDropdownMenu(
                [self detailMenuDropdownRect], 20.0, items.data(), count,
                selected, static_cast<int>(_detailMenuHover), value, style);
        }
    }
}

- (void)openSamples
{
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    [panel setCanChooseFiles:YES];
    [panel setCanChooseDirectories:NO];
    [panel setAllowsMultipleSelection:YES];
    if ([panel runModal] != NSModalResponseOK) return;
    uint32_t slot = [self selectedSlot];
    for (NSURL* url in [panel URLs]) {
        if (slot >= s3g::breakbeat::kMaximumSampleSlots) break;
        const char* path = [[url path] fileSystemRepresentation];
        if (path) queueSampleLoad(*_instance, slot, path);
        ++slot;
    }
    _selectedSlice = 0;
    _zoom = 1.0;
    _visibleStart = 0u;
    [self setNeedsDisplay:YES];
}

- (void)exportCurrentBreak
{
    const uint32_t slotIndex = [self selectedSlot];
    const auto* bank = [self displayBank];
    if (!bank || !bank->slots[slotIndex].asset) {
        _instance->status = "LOAD A BREAK BEFORE EXPORTING";
        [self setNeedsDisplay:YES];
        return;
    }
    NSString* source = [slotFilename(*_instance, slotIndex)
        stringByDeletingPathExtension];
    if (!source || [source length] == 0u || [source isEqualToString:@"EMPTY"])
        source = [NSString stringWithFormat:@"break-%u", slotIndex + 1u];
    NSString* name = [NSString stringWithFormat:@"%@-rendered.wav",
        source];
    NSSavePanel* panel = [NSSavePanel savePanel];
    [panel setTitle:@"Export Rendered Break"];
    [panel setPrompt:@"Export"];
    [panel setCanCreateDirectories:YES];
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    [panel setAllowedFileTypes:@[ @"wav" ]];
#pragma clang diagnostic pop
    [panel setNameFieldStringValue:name];
    if ([panel runModal] != NSModalResponseOK || ![panel URL]) return;
    const char* path = [[panel URL] fileSystemRepresentation];
    _instance->status = path
            && queueMutationExport(*_instance, slotIndex, path)
        ? "RENDERING BREAK FOR WAV EXPORT"
        : "COULD NOT QUEUE THE WAV EXPORT";
    [self setNeedsDisplay:YES];
}

- (void)clearSelectedSlot
{
    const uint32_t index = [self selectedSlot];
    cancelSampleLoad(*_instance, index);
    auto bank = editableBank(*_instance);
    s3g::breakbeat::clearMappingsForSlot(*bank,
        static_cast<uint8_t>(index));
    const uint8_t root = bank->slots[index].rootNote;
    const uint8_t channel = bank->slots[index].midiChannel;
    const float mixerGain = bank->slots[index].mixerGain;
    const float mixerPan = bank->slots[index].mixerPan;
    const float mixerLowEqDb = bank->slots[index].mixerLowEqDb;
    const float mixerMidEqDb = bank->slots[index].mixerMidEqDb;
    const float mixerHighEqDb = bank->slots[index].mixerHighEqDb;
    const float mixerMidFrequencyHz =
        bank->slots[index].mixerMidFrequencyHz;
    const float mixerAuxSend = bank->slots[index].mixerAuxSend;
    const auto inserts = bank->slots[index].inserts;
    const s3g::breakbeat::Envelope envelope = bank->slots[index].envelope;
    const bool muted = bank->slots[index].muted;
    const bool solo = bank->slots[index].solo;
    bank->slots[index] = {};
    bank->slots[index].rootNote = root;
    bank->slots[index].mappedRootNote = root;
    bank->slots[index].midiChannel = channel;
    bank->slots[index].envelope = envelope;
    bank->slots[index].mixerGain = mixerGain;
    bank->slots[index].mixerPan = mixerPan;
    bank->slots[index].mixerLowEqDb = mixerLowEqDb;
    bank->slots[index].mixerMidEqDb = mixerMidEqDb;
    bank->slots[index].mixerHighEqDb = mixerHighEqDb;
    bank->slots[index].mixerMidFrequencyHz = mixerMidFrequencyHz;
    bank->slots[index].mixerAuxSend = mixerAuxSend;
    bank->slots[index].inserts = inserts;
    bank->slots[index].muted = muted;
    bank->slots[index].solo = solo;
    if (publishBank(*_instance, std::move(bank), false)) {
        _instance->samplePaths[index].clear();
        _instance->analyses[index].reset();
        resetSlotVariations(*_instance, index);
        markStateDirty(*_instance);
        _instance->status = "SLOT CLEARED";
    }
    _selectedSlice = 0;
}

- (void)makeEqual:(std::size_t)count
{
    const auto* slot = [self selectedSampleSlot];
    if (!slot || !slot->asset) return;
    const std::size_t requested = count;
    count = std::min(count,
        s3g::breakbeat::maximumSlicesForStartNote(slot->rootNote));
    if (replaceSlotSlices(*_instance, [self selectedSlot],
            s3g::breakbeat::makeEqualSlices(*slot->asset, count))) {
        _selectedSlice = 0;
        _instance->status = count < requested
            ? "SLICES CAPPED BY START NOTE - PRESS AUTO MAP"
            : "SLICES CHANGED - PRESS AUTO MAP";
    }
}

- (void)applySelectedSliceMethod
{
    const uint32_t selected = [self selectedSlot];
    const auto* slot = [self selectedSampleSlot];
    if (!slot || !slot->asset) {
        _instance->status = "LOAD A BREAK BEFORE SLICING";
        return;
    }
    const NSInteger method = std::clamp<NSInteger>(
        _sliceModeSelection, 0, 4);
    if (method < 4) {
        [self makeEqual:static_cast<std::size_t>(4u << method)];
    } else if (_instance->analyses[selected]) {
        [self makeTransient];
    } else {
        _instance->status = "TRANSIENT ANALYSIS IS NOT AVAILABLE";
    }
}

- (void)makeTransient
{
    const uint32_t selected = [self selectedSlot];
    const auto* slot = [self selectedSampleSlot];
    const auto analysis = _instance->analyses[selected];
    if (!slot || !slot->asset || !analysis) return;
    const uint32_t zeroRadius = static_cast<uint32_t>(std::lround(
        slot->asset->sampleRate * 0.004));
    if (replaceSlotSlices(*_instance, selected,
            s3g::breakbeat::makeTransientSlices(*slot->asset, *analysis,
                s3g::breakbeat::maximumSlicesForStartNote(slot->rootNote),
                zeroRadius,
                _instance->transientPreRollMicroseconds,
                static_cast<uint32_t>(std::lround(slot->asset->sampleRate
                    * _instance->minimumTransientSliceMilliseconds
                    * 0.001))))) {
        _selectedSlice = 0;
        _instance->status = "SLICES CHANGED - PRESS AUTO MAP";
    }
}

- (void)audition
{
    const auto* bank = [self displayBank];
    if (!bank) return;
    const int note = mappedNoteFor(*bank, [self selectedSlot],
        static_cast<uint32_t>(std::max<NSInteger>(0, _selectedSlice)));
    if (note < 0) {
        _instance->status = "SELECTED SLICE IS NOT MAPPED";
        return;
    }
    _instance->pendingAuditionNote.store(static_cast<uint32_t>(note + 1),
        std::memory_order_release);
    _instance->pendingAuditionChannel.store(
        bank->slots[[self selectedSlot]].midiChannel,
        std::memory_order_release);
    requestProcess(*_instance);
}

- (void)updateEnvelopeDragAt:(NSPoint)point
{
    if (_envelopeDragIndex < 0 || _envelopeDragIndex > 3 || !_dragBank)
        return;
    auto& slot = _dragBank->slots[[self selectedSlot]];
    if (!slot.asset || slot.sliceCount == 0u) return;
    auto& envelope = slot.envelope;
    const NSRect track = envelopeSliderTrackRect(
        static_cast<uint32_t>(_envelopeDragIndex));
    float normalized = static_cast<float>(std::clamp(
        (point.x - NSMinX(track)) / NSWidth(track), 0.0, 1.0));
    if (_envelopeDragIndex == 2) {
        envelope.sustain = normalized;
    } else {
        const float otherTiming = _envelopeDragIndex == 0
            ? envelope.decayProportion + envelope.releaseProportion
            : _envelopeDragIndex == 1
                ? envelope.attackProportion + envelope.releaseProportion
                : envelope.attackProportion + envelope.decayProportion;
        normalized = std::min(normalized, std::max(0.0f,
            1.0f - otherTiming));
        if (_envelopeDragIndex == 0)
            envelope.attackProportion = normalized;
        else if (_envelopeDragIndex == 1)
            envelope.decayProportion = normalized;
        else
            envelope.releaseProportion = normalized;
    }
    [self setNeedsDisplay:YES];
}

- (void)updateMixerDragAt:(NSPoint)point
{
    if (_mixerDragKind == 3) {
        const NSRect track = mixerOutputTrackRect();
        const double normalized = std::clamp(
            (point.x - NSMinX(track)) / NSWidth(track), 0.0, 1.0);
        setParam(*_instance, kOutputGainParamId,
            -60.0 + normalized * 72.0);
        requestProcess(*_instance);
        [self setNeedsDisplay:YES];
        return;
    }
    if (_mixerDragKind >= 20 && _mixerDragKind <= 23) {
        if (!_dragBank || _mixerDragSlot >= _dragBank->slots.size()
            || _insertEditorIndex >= s3g::breakbeat::kInsertSlotsPerStrip)
            return;
        const uint32_t parameter = static_cast<uint32_t>(
            _mixerDragKind - 20);
        const NSRect track = mixerInsertParameterTrackRect(parameter);
        auto& insert = _dragBank->slots[_mixerDragSlot]
            .inserts[_insertEditorIndex];
        insert.values[parameter] = static_cast<float>(std::clamp(
            (point.x - NSMinX(track)) / NSWidth(track), 0.0, 1.0));
        (void)publishMixerBank(*_instance, _dragBank, false);
        [self setNeedsDisplay:YES];
        return;
    }
    if (_mixerDragKind >= 9 && _mixerDragKind <= 16) {
        if (!_dragBank) return;
        const uint32_t row = static_cast<uint32_t>(_mixerDragKind - 9);
        if (_dragBank->auxFieldSafe && row >= 3u && row <= 5u) return;
        const NSRect track = mixerBusSliderTrackRect(row);
        const float normalized = static_cast<float>(std::clamp(
            (point.x - NSMinX(track)) / NSWidth(track), 0.0, 1.0));
        switch (row) {
        case 0u: _dragBank->auxPress = normalized; break;
        case 1u: _dragBank->auxSnap = normalized * 2.0f - 1.0f; break;
        case 2u: _dragBank->auxRecovery = normalized; break;
        case 3u: _dragBank->auxSaturation = normalized; break;
        case 4u: _dragBank->auxBite = normalized; break;
        case 5u: _dragBank->auxClip = normalized; break;
        case 6u: _dragBank->auxTilt = normalized * 2.0f - 1.0f; break;
        case 7u: _dragBank->auxReturnDb = -60.0f + normalized * 72.0f; break;
        default: break;
        }
        (void)publishMixerBank(*_instance, _dragBank, false);
        [self setNeedsDisplay:YES];
        return;
    }
    if (!_dragBank || _mixerDragSlot >= _dragBank->slots.size()) return;
    auto& slot = _dragBank->slots[_mixerDragSlot];
    if (_mixerDragKind == 1) {
        const NSRect fader = mixerFaderRect(_mixerDragSlot);
        const CGFloat normalized = std::clamp<CGFloat>(
            (NSMaxY(fader) - point.y) / NSHeight(fader), 0.0, 1.0);
        const double db = -60.0 + static_cast<double>(normalized) * 66.0;
        slot.mixerGain = normalized <= 0.0001
            ? 0.0f : static_cast<float>(std::pow(10.0, db / 20.0));
    } else if (_mixerDragKind == 8) {
        const NSRect track = mixerAuxTrackRect(_mixerDragSlot);
        slot.mixerAuxSend = static_cast<float>(std::clamp(
            (point.x - NSMinX(track)) / NSWidth(track), 0.0, 1.0));
    } else if (_mixerDragKind == 2
        || (_mixerDragKind >= 4 && _mixerDragKind <= 7)) {
        const double delta = (_mixerDragStartPoint.y - point.y)
            + (point.x - _mixerDragStartPoint.x);
        double normalized = 0.0;
        if (_mixerDragKind == 2)
            normalized = (_mixerDragStartValue + 1.0) * 0.5;
        else if (_mixerDragKind == 6)
            normalized = std::log(_mixerDragStartValue / 120.0)
                / std::log(8000.0 / 120.0);
        else normalized = (_mixerDragStartValue + 12.0) / 24.0;
        normalized = std::clamp(normalized + delta / 160.0, 0.0, 1.0);
        if (_mixerDragKind == 2) {
            slot.mixerPan = static_cast<float>(normalized * 2.0 - 1.0);
            if (std::abs(slot.mixerPan) < 0.025f) slot.mixerPan = 0.0f;
        } else if (_mixerDragKind == 4) {
            slot.mixerLowEqDb = static_cast<float>(normalized * 24.0 - 12.0);
        } else if (_mixerDragKind == 5) {
            slot.mixerMidEqDb = static_cast<float>(normalized * 24.0 - 12.0);
        } else if (_mixerDragKind == 6) {
            slot.mixerMidFrequencyHz = static_cast<float>(120.0
                * std::pow(8000.0 / 120.0, normalized));
        } else {
            slot.mixerHighEqDb = static_cast<float>(normalized * 24.0 - 12.0);
        }
    }
    // Publish a post-playback mixer snapshot for every drag update. This is
    // independent of voice/sample state, so an active slice hears the change
    // on the next process block instead of waiting for a retrigger.
    (void)publishMixerBank(*_instance, _dragBank, false);
    [self setNeedsDisplay:YES];
}

- (void)handleMixerMouseDown:(NSPoint)point
{
    for (uint32_t index = 0u;
         index < s3g::breakbeat::kMaximumSampleSlots; ++index) {
        const NSRect strip = mixerStripRect(index);
        if (!NSPointInRect(point, strip)) continue;
        _instance->selectedSlot = index;
        const auto* displayed = [self displayBank];
        if (!displayed) return;
        const auto& slot = displayed->slots[index];
        for (uint32_t insert = 0u;
             insert < s3g::breakbeat::kInsertSlotsPerStrip; ++insert) {
            if (!NSPointInRect(point, mixerInsertRect(index, insert)))
                continue;
            _insertEditorSlot = static_cast<NSInteger>(index);
            _insertEditorIndex = insert;
            _instance->status = "BREAK INSERT EDITOR OPEN";
            [self setNeedsDisplay:YES];
            return;
        }
        _insertEditorSlot = -1;
        if (NSPointInRect(point, mixerFaderRect(index))) {
            _mixerDragKind = 1;
            _mixerDragSlot = index;
            _dragBank = editableBank(*_instance);
            [self updateMixerDragAt:point];
        } else if (NSPointInRect(point, mixerMuteRect(index))) {
            auto bank = editableBank(*_instance);
            bank->slots[index].muted = !bank->slots[index].muted;
            if (publishMixerBank(*_instance, std::move(bank)))
                _instance->status = "BREAK MUTE UPDATED";
        } else if (NSPointInRect(point, mixerSoloRect(index))) {
            auto bank = editableBank(*_instance);
            bank->slots[index].solo = !bank->slots[index].solo;
            if (publishMixerBank(*_instance, std::move(bank)))
                _instance->status = "BREAK SOLO UPDATED";
        } else if (NSPointInRect(point, mixerAuditionRect(index))) {
            _selectedSlice = 0;
            [self audition];
        } else if (NSPointInRect(point, mixerAuxHitRect(index))) {
            _mixerDragKind = 8;
            _mixerDragSlot = index;
            _dragBank = editableBank(*_instance);
            [self updateMixerDragAt:point];
        } else {
            constexpr std::array<NSInteger, 5u> kinds {{ 2, 4, 5, 6, 7 }};
            for (uint32_t dial = 0u; dial < kinds.size(); ++dial) {
                if (!NSPointInRect(point, mixerDialRect(index, dial)))
                    continue;
                if (dial == 0u && slot.asset
                    && slot.asset->channelCount > 2u) {
                    _instance->status
                        = "PAN IS LOCKED FOR MULTICHANNEL BREAKS";
                    break;
                }
                _mixerDragKind = kinds[dial];
                _mixerDragSlot = index;
                _mixerDragStartPoint = point;
                _mixerDragStartValue = dial == 0u ? slot.mixerPan
                    : dial == 1u ? slot.mixerLowEqDb
                    : dial == 2u ? slot.mixerMidEqDb
                    : dial == 3u ? slot.mixerMidFrequencyHz
                                 : slot.mixerHighEqDb;
                _dragBank = editableBank(*_instance);
                break;
            }
        }
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, mixerOutputHitRect())) {
        _mixerDragKind = 3;
        [self updateMixerDragAt:point];
        return;
    }
    if (NSPointInRect(point, mixerUnityRect())) {
        setParam(*_instance, kOutputGainParamId, 0.0);
        markStateDirty(*_instance);
        requestProcess(*_instance);
        _instance->status = "MAIN OUTPUT RETURNED TO UNITY";
        [self setNeedsDisplay:YES];
        return;
    }
    if (_insertEditorSlot >= 0
        && static_cast<std::size_t>(_insertEditorSlot)
            < s3g::breakbeat::kMaximumSampleSlots) {
        const uint32_t slotIndex = static_cast<uint32_t>(_insertEditorSlot);
        for (uint32_t type = 0u;
             type <= static_cast<uint32_t>(InsertType::TimeMangler); ++type) {
            if (!NSPointInRect(point, mixerInsertTypeRect(type))) continue;
            auto bank = editableBank(*_instance);
            bank->slots[slotIndex].inserts[_insertEditorIndex]
                = s3g::breakbeat::defaultInsertSettings(
                    static_cast<InsertType>(type));
            if (publishMixerBank(*_instance, std::move(bank)))
                _instance->status = type == 0u
                    ? "BREAK INSERT CLEARED" : "BREAK INSERT ASSIGNED";
            [self setNeedsDisplay:YES];
            return;
        }
        if (NSPointInRect(point, mixerInsertBypassRect())) {
            auto bank = editableBank(*_instance);
            auto& insert = bank->slots[slotIndex].inserts[_insertEditorIndex];
            if (insert.type != InsertType::Off) {
                insert.bypassed = !insert.bypassed;
                const bool bypassed = insert.bypassed;
                if (publishMixerBank(*_instance, std::move(bank)))
                    _instance->status = bypassed
                        ? "BREAK INSERT BYPASSED"
                        : "BREAK INSERT ENABLED";
            }
            [self setNeedsDisplay:YES];
            return;
        }
        if (NSPointInRect(point, mixerInsertSwapRect())) {
            auto bank = editableBank(*_instance);
            auto& inserts = bank->slots[slotIndex].inserts;
            std::swap(inserts[0u], inserts[1u]);
            _insertEditorIndex = 1u - _insertEditorIndex;
            if (publishMixerBank(*_instance, std::move(bank)))
                _instance->status = "BREAK INSERT ORDER SWAPPED";
            [self setNeedsDisplay:YES];
            return;
        }
        if (NSPointInRect(point, mixerInsertCloseRect())) {
            _insertEditorSlot = -1;
            _instance->status = "BREAK BUS VIEW RESTORED";
            [self setNeedsDisplay:YES];
            return;
        }
        if (NSPointInRect(point, mixerInsertOptionRect())) {
            auto bank = editableBank(*_instance);
            auto& insert = bank->slots[slotIndex].inserts[_insertEditorIndex];
            if (insert.type == InsertType::Filter) {
                const uint8_t next = (static_cast<uint8_t>(insert.mode) + 1u)
                    % 4u;
                insert.mode = static_cast<FilterMode>(next);
                _instance->status = "FILTER MODE UPDATED";
            } else if (insert.type == InsertType::Erosion
                || insert.type == InsertType::Shifter
                || insert.type == InsertType::Wavefolder) {
                insert.variant = insert.variant == 0u ? 1u : 0u;
                _instance->status = "BREAK INSERT MODE UPDATED";
            } else if (insert.type == InsertType::Repeater
                || insert.type == InsertType::TimeMangler) {
                insert.variant = static_cast<uint8_t>(
                    (insert.variant + 1u) % 3u);
                _instance->status = "BUFFER INSERT MODE UPDATED";
            } else if (insert.type != InsertType::Off) {
                insert = s3g::breakbeat::defaultInsertSettings(insert.type);
                _instance->status = "BREAK INSERT RESET";
            }
            (void)publishMixerBank(*_instance, std::move(bank));
            [self setNeedsDisplay:YES];
            return;
        }
        for (uint32_t parameter = 0u;
             parameter < s3g::breakbeat::kInsertParameterCount;
             ++parameter) {
            if (!NSPointInRect(point,
                    mixerInsertParameterHitRect(parameter))) continue;
            const auto* displayed = [self displayBank];
            if (!displayed || displayed->slots[slotIndex]
                    .inserts[_insertEditorIndex].type == InsertType::Off)
                return;
            _mixerDragKind = static_cast<NSInteger>(20u + parameter);
            _mixerDragSlot = slotIndex;
            _dragBank = editableBank(*_instance);
            [self updateMixerDragAt:point];
            return;
        }
        // The editor owns the former Break Bus rectangle while open.
        if (NSPointInRect(point, mixerAuxBusRect())) return;
    }
    if (NSPointInRect(point, mixerBusEnableRect())) {
        auto bank = editableBank(*_instance);
        bank->auxEnabled = !bank->auxEnabled;
        if (publishMixerBank(*_instance, std::move(bank)))
            _instance->status = "AUX BUS UPDATED";
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, mixerBusLinkRect())) {
        auto bank = editableBank(*_instance);
        const uint8_t next = (static_cast<uint8_t>(bank->auxLinkMode) + 1u)
            % 3u;
        bank->auxLinkMode = static_cast<s3g::BreakBusLinkMode>(next);
        if (publishMixerBank(*_instance, std::move(bank)))
            _instance->status = "BREAK BUS LINK MODE UPDATED";
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, mixerBusFieldSafeRect())) {
        auto bank = editableBank(*_instance);
        bank->auxFieldSafe = !bank->auxFieldSafe;
        const bool fieldSafe = bank->auxFieldSafe;
        if (publishMixerBank(*_instance, std::move(bank)))
            _instance->status = fieldSafe
                ? "FIELD SAFE DISABLES NONLINEAR STAGES"
                : "BREAK BUS NONLINEAR STAGES ENABLED";
        [self setNeedsDisplay:YES];
        return;
    }
    for (uint32_t row = 0u; row < 8u; ++row) {
        if (!NSPointInRect(point, mixerBusSliderHitRect(row))) continue;
        const auto* bank = [self displayBank];
        if (bank && bank->auxFieldSafe && row >= 3u && row <= 5u) {
            _instance->status = "SAT / BITE / CLIP BYPASSED IN FIELD SAFE";
            [self setNeedsDisplay:YES];
            return;
        }
        _mixerDragKind = static_cast<NSInteger>(9u + row);
        _dragBank = editableBank(*_instance);
        [self updateMixerDragAt:point];
        return;
    }
}

- (void)mouseDown:(NSEvent*)event
{
    const NSPoint point = [self convertPoint:[event locationInWindow]
        fromView:nil];
    if (_detailMenuKind != kDetailMenuNone) {
        const NSInteger selection = [self detailMenuHitAtPoint:point];
        if (selection >= 0) {
            [self applyDetailMenuSelection:selection];
            [self closeDetailMenu];
            [self updateDetailNumericFields];
            [self setNeedsDisplay:YES];
            return;
        }
        if (!NSPointInRect(point, detailMenuControlRect(
                _detailMenuKind, _detailMenuSlot))) {
            [self closeDetailMenu];
            [self updateDetailNumericFields];
        }
    }
    for (uint32_t index = 0u; index < 4u; ++index) {
        if (NSPointInRect(point, editorTabRect(index))) {
            [self closeDetailMenu];
            _page = index;
            [self updateDetailNumericFields];
            [self setNeedsDisplay:YES];
            return;
        }
    }
    if (NSPointInRect(point, killAllButtonRect())) {
        _instance->pendingKillAll.store(true, std::memory_order_release);
        _instance->status = "ALL SAMPLE PLAYBACK STOPPED";
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, embedAudioButtonRect())) {
        _instance->embedSamplesInState
            = !_instance->embedSamplesInState;
        _instance->status = _instance->embedSamplesInState
            ? "SAMPLES WILL BE EMBEDDED IN PROJECT STATE"
            : "PROJECT STATE WILL REFERENCE SAMPLE PATHS";
        markStateDirty(*_instance);
        [self setNeedsDisplay:YES];
        return;
    }
    if (_page == 2u) {
        [self handleMixerMouseDown:point];
        return;
    }
    for (uint32_t index = 0u;
         index < s3g::breakbeat::kMaximumSampleSlots; ++index) {
        if (NSPointInRect(point, slotChannelRect(index))) {
            _instance->selectedSlot = index;
            _detailMenuKind = kDetailMenuMidiChannel;
            _detailMenuHover = -1;
            _detailMenuSlot = index;
            [self updateDetailNumericFields];
            [self setNeedsDisplay:YES];
            return;
        }
        if (NSPointInRect(point, slotAutomapRect(index))) {
            if (automapSlot(*_instance, index))
                _instance->status = "BREAK SLICES AUTO-MAPPED";
            else
                _instance->status =
                    "LOAD OR REDUCE SLICES BEFORE AUTO MAP";
            [self setNeedsDisplay:YES];
            return;
        }
        if (NSPointInRect(point, bankRowRect(index))) {
            _instance->selectedSlot = index;
            _selectedSlice = 0;
            _zoom = 1.0;
            _visibleStart = 0u;
            if ([event clickCount] >= 2) _page = 1u;
            [self setNeedsDisplay:YES];
            return;
        }
    }
    if (_page == 0u) {
        for (uint32_t index = 0u;
             index < s3g::breakbeat::kMaximumSampleSlots; ++index) {
            if (!NSPointInRect(point, overviewWaveformRect(index))) continue;
            _instance->selectedSlot = index;
            _selectedSlice = 0;
            _zoom = 1.0;
            _visibleStart = 0u;
            if ([event clickCount] >= 2) _page = 1u;
            [self setNeedsDisplay:YES];
            return;
        }
        return;
    }
    if (_page == 3u) {
        const uint32_t slotIndex = [self selectedSlot];
        const auto* bank = [self displayBank];
        if (!bank) return;
        const auto& slot = bank->slots[slotIndex];
        for (uint32_t index = 0u;
             index < s3g::breakbeat::kMaximumSampleSlots; ++index) {
            if (!NSPointInRect(point, mutateSlotRect(index)))
                continue;
            _instance->selectedSlot = index;
            _selectedSlice = 0;
            _zoom = 1.0;
            _visibleStart = 0u;
            _instance->status = bank->slots[index].asset
                ? "BREAK SELECTED AS MUTATION SOURCE"
                : _instance->pendingMutationSlots[index]
                    ? "MUTATION SLOT IS BUILDING"
                    : "EMPTY SLOT SELECTED";
            [self setNeedsDisplay:YES];
            return;
        }
        if (NSPointInRect(point, mutateActionButtonRect(0u))) {
            if (!slot.asset) {
                _instance->status
                    = "SELECT A LOADED BREAK BEFORE FILLING SLOTS";
            } else {
                const uint32_t count = fillEmptySlotsWithMutations(
                    *_instance, slotIndex);
                _instance->status = count > 0u
                    ? "BUILDING STRUCTURAL MUTATIONS IN EMPTY SLOTS"
                    : "ALL OTHER BREAK SLOTS ARE LOCKED";
            }
            [self setNeedsDisplay:YES];
            return;
        }
        if (NSPointInRect(point, mutateActionButtonRect(1u))) {
            if (!slotHasCompleteMap(slot)) {
                _instance->status
                    = "AUTO MAP THE BREAK BEFORE PLAY THROUGH";
            } else {
                _instance->pendingPlaythroughSlot.store(slotIndex + 1u,
                    std::memory_order_release);
                _instance->status
                    = "PLAYING THROUGH MAPPED SLICES";
                requestProcess(*_instance);
            }
            [self setNeedsDisplay:YES];
            return;
        }
        if (NSPointInRect(point, mutateActionButtonRect(2u))) {
            [self clearSelectedSlot];
            [self setNeedsDisplay:YES];
            return;
        }
        if (NSPointInRect(point, mutateActionButtonRect(3u))) {
            [self exportCurrentBreak];
            return;
        }
        static constexpr std::array<uint8_t, 6u> useBits {{
            s3g::breakbeat::StructuralRearrange,
            s3g::breakbeat::StructuralRepeat,
            s3g::breakbeat::StructuralPitch,
            s3g::breakbeat::StructuralMixerFx,
            s3g::breakbeat::StructuralAuxBus,
            s3g::breakbeat::StructuralReverse,
        }};
        for (uint32_t index = 0u; index < useBits.size(); ++index) {
            if (!NSPointInRect(point, mutateUseRect(index)))
                continue;
            _instance->structuralMutationUses ^= useBits[index];
            markStateDirty(*_instance);
            _instance->status = "MUTATE OPTIONS UPDATED";
            [self setNeedsDisplay:YES];
            return;
        }
        return;
    }
    const auto* detailSlot = [self selectedSampleSlot];
    if (!detailSlot) return;
    if (NSPointInRect(point, sampleLoadButtonRect())) {
        [self openSamples];
        return;
    }
    if (NSPointInRect(point, sampleClearButtonRect())) {
        [self clearSelectedSlot];
        return;
    }
    if (NSPointInRect(point, sliceActionButtonRect())) {
        [self applySelectedSliceMethod];
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, automapButtonRect())) {
        if (automapSlot(*_instance, [self selectedSlot]))
            _instance->status
                = "BREAK SLICES AUTO-MAPPED - SLICE EDITING ENABLED";
        else
            _instance->status = "LOAD OR REDUCE SLICES BEFORE AUTO MAP";
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, sliceMethodMenuRect())) {
        _detailMenuKind = kDetailMenuSliceMethod;
        _detailMenuHover = -1;
        _detailMenuSlot = [self selectedSlot];
        [self updateDetailNumericFields];
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, startNoteMenuRect())) {
        _detailMenuKind = kDetailMenuStartNote;
        _detailMenuHover = -1;
        _detailMenuSlot = [self selectedSlot];
        [self updateDetailNumericFields];
        [self setNeedsDisplay:YES];
        return;
    }
    for (uint32_t index = 0u; index < 5u; ++index) {
        if (!NSPointInRect(point, playbackMenuRect(index))) continue;
        _detailMenuKind = kDetailMenuTrigger
            + static_cast<NSInteger>(index);
        _detailMenuHover = -1;
        _detailMenuSlot = [self selectedSlot];
        [self updateDetailNumericFields];
        [self setNeedsDisplay:YES];
        return;
    }
    for (uint32_t index = 0u; index < kDetailNumericCount; ++index) {
        if (!NSPointInRect(point, detailNumericHitRect(index))) continue;
        if ([event clickCount] >= 2) {
            const std::array<double, kDetailNumericCount> defaults {{
                0.0,
                static_cast<double>(kDefaultMinimumTransientSliceMilliseconds)
                    / kMaximumMinimumTransientSliceMilliseconds,
                (120.0 - 20.0) / 979.0,
                0.02 / 0.5,
                0.0,
                0.25,
                0.5,
                0.5,
            }};
            const NSRect track = detailNumericTrackRect(index);
            [self updateDetailNumericControl:index point:NSMakePoint(
                NSMinX(track) + NSWidth(track) * defaults[index],
                NSMidY(track))];
            markStateDirty(*_instance);
        } else {
            _detailNumericDragIndex = static_cast<NSInteger>(index);
            [self updateDetailNumericControl:index point:point];
        }
        return;
    }
    if (NSPointInRect(point, sliceReverseButtonRect())) {
        [self editSelectedSliceControl:6u];
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, sliceLaunchMenuRect())) {
        const auto* selectedSlot = [self selectedSampleSlot];
        if (!selectedSlot || !slotHasCompleteMap(*selectedSlot)
            || selectedSlot->sliceCount == 0u) return;
        _detailMenuKind = kDetailMenuSliceLaunch;
        _detailMenuHover = -1;
        _detailMenuSlot = [self selectedSlot];
        [self updateDetailNumericFields];
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, sliceChokeMenuRect())) {
        const auto* selectedSlot = [self selectedSampleSlot];
        if (!selectedSlot || !slotHasCompleteMap(*selectedSlot)
            || selectedSlot->sliceCount == 0u) return;
        _detailMenuKind = kDetailMenuSliceChoke;
        _detailMenuHover = -1;
        _detailMenuSlot = [self selectedSlot];
        [self updateDetailNumericFields];
        [self setNeedsDisplay:YES];
        return;
    }
    for (uint32_t index = 0u; index < 4u; ++index) {
        if (!NSPointInRect(point, envelopeSliderHitRect(index))) continue;
        const auto* slot = [self selectedSampleSlot];
        if (!slot || !slot->asset || slot->sliceCount == 0u) return;
        _envelopeDragIndex = static_cast<NSInteger>(index);
        _dragBank = editableBank(*_instance);
        [self updateEnvelopeDragAt:point];
        return;
    }
    if (NSPointInRect(point, keyboardRect())) {
        const auto* bank = [self displayBank];
        if (!bank) return;
        const auto& slot = bank->slots[[self selectedSlot]];
        constexpr uint32_t keyCount = 24u;
        const uint32_t index = std::min<uint32_t>(keyCount - 1u,
            static_cast<uint32_t>((point.x - keyboardRect().origin.x)
                / (keyboardRect().size.width / keyCount)));
        if (index < slot.mappedSliceCount
            && slot.mappedRootNote == slot.rootNote) {
            _selectedSlice = static_cast<NSInteger>(index);
            [self audition];
        } else {
            _instance->status = "KEY IS NOT MAPPED - PRESS AUTO MAP";
        }
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, waveformNavigatorRect())) {
        const auto* slot = [self selectedSampleSlot];
        if (!slot || !slot->asset) return;
        const uint32_t total = slot->asset->frameCount();
        const NSRect handle = [self viewportHandleForTotal:total];
        _dragViewport = YES;
        if (NSPointInRect(point, handle)) {
            _viewportGrabOffset = point.x - handle.origin.x;
        } else {
            _viewportGrabOffset = handle.size.width * 0.5;
            [self setViewportHandleOrigin:point.x - _viewportGrabOffset
                total:total];
        }
        [self setNeedsDisplay:YES];
        return;
    }
    if (!NSPointInRect(point, waveformRect())) return;
    const auto* slot = [self selectedSampleSlot];
    if (!slot || !slot->asset) return;
    const NSInteger loopHandle = [self loopHandleNearPoint:point];
    if (loopHandle >= 0) {
        _dragLoopHandle = loopHandle;
        _dragBank = editableBank(*_instance);
        _instance->status = loopHandle == 0
            ? "DRAG LOOP START" : "DRAG LOOP END";
        return;
    }
    const NSInteger marker = [self markerNearPoint:point];
    if (marker > 0) {
        _dragMarker = marker;
        _dragBank = editableBank(*_instance);
        return;
    }
    const uint32_t frame = [self frameForX:point.x
        total:slot->asset->frameCount()];
    if ([event clickCount] >= 2) {
        auto bank = editableBank(*_instance);
        auto& edited = bank->slots[[self selectedSlot]];
        std::size_t count = edited.sliceCount;
        const uint32_t snapped = s3g::breakbeat::nearestZeroFrame(
            *edited.asset, frame, static_cast<uint32_t>(std::lround(
                edited.asset->sampleRate * 0.004)));
        const std::size_t maximum = std::min<std::size_t>(
            edited.slices.size(),
            s3g::breakbeat::maximumSlicesForStartNote(edited.rootNote));
        if (s3g::breakbeat::addSliceMarker(edited.slices.data(), count,
                maximum, snapped)) {
            edited.sliceCount = static_cast<uint16_t>(count);
            edited.mappedSliceCount = 0u;
            publishBank(*_instance, std::move(bank));
            _selectedSlice = [self sliceAtFrame:snapped];
            _instance->status = "MARKER ADDED - PRESS AUTO MAP";
        } else if (count >= maximum) {
            _instance->status = "START NOTE SLICE LIMIT REACHED";
        }
    } else {
        _selectedSlice = [self sliceAtFrame:frame];
    }
    [self setNeedsDisplay:YES];
}

- (void)mouseDragged:(NSEvent*)event
{
    if (_detailNumericDragIndex >= 0) {
        [self updateDetailNumericControl:
            static_cast<uint32_t>(_detailNumericDragIndex)
            point:[self convertPoint:[event locationInWindow] fromView:nil]];
        return;
    }
    if (_envelopeDragIndex >= 0) {
        [self updateEnvelopeDragAt:[self convertPoint:[event locationInWindow]
            fromView:nil]];
        return;
    }
    if (_mixerDragKind != 0) {
        [self updateMixerDragAt:[self convertPoint:[event locationInWindow]
            fromView:nil]];
        return;
    }
    if (_dragViewport) {
        const auto* slot = [self selectedSampleSlot];
        if (!slot || !slot->asset) return;
        const NSPoint point = [self convertPoint:[event locationInWindow]
            fromView:nil];
        [self setViewportHandleOrigin:point.x - _viewportGrabOffset
            total:slot->asset->frameCount()];
        [self setNeedsDisplay:YES];
        return;
    }
    if (_dragLoopHandle >= 0 && _dragBank) {
        const NSPoint point = [self convertPoint:[event locationInWindow]
            fromView:nil];
        auto& slot = _dragBank->slots[[self selectedSlot]];
        if (!slot.asset || _selectedSlice < 0
            || static_cast<std::size_t>(_selectedSlice) >= slot.sliceCount)
            return;
        auto& slice = slot.slices[static_cast<std::size_t>(_selectedSlice)];
        uint32_t loopStart = slice.loopStartFrame == 0u
                && slice.loopEndFrame == 0u
            ? slice.startFrame : slice.loopStartFrame;
        uint32_t loopEnd = slice.loopStartFrame == 0u
                && slice.loopEndFrame == 0u
            ? slice.endFrame : slice.loopEndFrame;
        uint32_t frame = [self frameForX:point.x
            total:slot.asset->frameCount()];
        const CGFloat sliceEndX = [self xForFrame:slice.endFrame
            total:slot.asset->frameCount()];
        if (_dragLoopHandle == 1 && point.x >= sliceEndX - 2.0)
            frame = slice.endFrame;
        else
            frame = s3g::breakbeat::nearestZeroFrame(*slot.asset, frame,
                static_cast<uint32_t>(std::lround(
                    slot.asset->sampleRate * 0.004)));
        if (_dragLoopHandle == 0)
            loopStart = std::clamp(frame, slice.startFrame, loopEnd - 1u);
        else
            loopEnd = std::clamp(frame, loopStart + 1u, slice.endFrame);
        slice.loopStartFrame = loopStart;
        slice.loopEndFrame = loopEnd;
        [self setNeedsDisplay:YES];
        return;
    }
    if (_dragMarker <= 0 || !_dragBank) return;
    const NSPoint point = [self convertPoint:[event locationInWindow]
        fromView:nil];
    auto& slot = _dragBank->slots[[self selectedSlot]];
    if (!slot.asset) return;
    uint32_t frame = [self frameForX:point.x total:slot.asset->frameCount()];
    frame = s3g::breakbeat::nearestZeroFrame(*slot.asset, frame,
        static_cast<uint32_t>(std::lround(slot.asset->sampleRate * 0.004)));
    s3g::breakbeat::moveSliceMarker(slot.slices.data(), slot.sliceCount,
        static_cast<std::size_t>(_dragMarker), frame);
    [self setNeedsDisplay:YES];
}

- (void)mouseUp:(NSEvent*)event
{
    (void)event;
    if (_detailNumericDragIndex >= 0) {
        markStateDirty(*_instance);
        _detailNumericDragIndex = kDetailNumericNone;
        [self updateDetailNumericFields];
        [self setNeedsDisplay:YES];
        return;
    }
    if (_envelopeDragIndex >= 0) {
        if (_dragBank && publishBank(*_instance, _dragBank))
            _instance->status = "BREAK ENVELOPE UPDATED";
        _dragBank.reset();
        _envelopeDragIndex = -1;
        [self setNeedsDisplay:YES];
        return;
    }
    if (_mixerDragKind != 0) {
        if (_mixerDragKind == 3) {
            markStateDirty(*_instance);
            _instance->status = "MAIN OUTPUT GAIN UPDATED";
        } else if (_dragBank && publishMixerBank(*_instance, _dragBank)) {
            _instance->status = _mixerDragKind >= 20
                ? "BREAK INSERT UPDATED"
                : _mixerDragKind >= 9
                ? "AUX BUS PROCESSOR UPDATED"
                : _mixerDragKind == 1 ? "BREAK LEVEL UPDATED"
                : _mixerDragKind == 2 ? "BREAK PAN UPDATED"
                : _mixerDragKind == 8 ? "BREAK AUX SEND UPDATED"
                                       : "BREAK EQ UPDATED";
        }
        _dragBank.reset();
        _mixerDragKind = 0;
        [self setNeedsDisplay:YES];
        return;
    }
    if (_dragLoopHandle >= 0) {
        if (_dragBank && publishBank(*_instance, _dragBank))
            _instance->status = "SLICE LOOP POINTS UPDATED";
        _dragBank.reset();
        _dragLoopHandle = -1;
        [self setNeedsDisplay:YES];
        return;
    }
    if (_dragBank) {
        const auto& slot = _dragBank->slots[[self selectedSlot]];
        const bool mappingPreserved = slot.mappedSliceCount > 0u
            && slot.mappedSliceCount == slot.sliceCount;
        if (publishBank(*_instance, _dragBank))
            _instance->status = mappingPreserved
                ? "MARKER MOVED - MAP PRESERVED" : "MARKER MOVED";
        _dragBank.reset();
    }
    _dragMarker = -1;
    _dragLoopHandle = -1;
    _dragViewport = NO;
}

- (void)rightMouseDown:(NSEvent*)event
{
    const NSPoint point = [self convertPoint:[event locationInWindow]
        fromView:nil];
    if (_page != 1u) return;
    if (!NSPointInRect(point, waveformRect())) return;
    const NSInteger marker = [self markerNearPoint:point];
    if (marker <= 0) return;
    auto bank = editableBank(*_instance);
    auto& slot = bank->slots[[self selectedSlot]];
    std::size_t count = slot.sliceCount;
    if (s3g::breakbeat::deleteSliceMarker(slot.slices.data(), count,
            static_cast<std::size_t>(marker))) {
        slot.sliceCount = static_cast<uint16_t>(count);
        slot.mappedSliceCount = 0u;
        publishBank(*_instance, std::move(bank));
        _selectedSlice = std::min<NSInteger>(_selectedSlice,
            static_cast<NSInteger>(count) - 1);
        _instance->status = "MARKER DELETED - PRESS AUTO MAP";
        [self setNeedsDisplay:YES];
    }
}

- (void)mouseMoved:(NSEvent*)event
{
    if (_detailMenuKind == kDetailMenuNone) return;
    const NSInteger hover = [self detailMenuHitAtPoint:
        [self convertPoint:[event locationInWindow] fromView:nil]];
    if (hover != _detailMenuHover) {
        _detailMenuHover = hover;
        [self setNeedsDisplay:YES];
    }
}

- (void)mouseExited:(NSEvent*)event
{
    (void)event;
    if (_detailMenuHover >= 0) {
        _detailMenuHover = -1;
        [self setNeedsDisplay:YES];
    }
}

- (void)scrollWheel:(NSEvent*)event
{
    if (_page != 1u) return;
    const auto* slot = [self selectedSampleSlot];
    if (!slot || !slot->asset) return;
    const NSPoint point = [self convertPoint:[event locationInWindow]
        fromView:nil];
    if (!NSPointInRect(point, waveformRect())
        && !NSPointInRect(point, waveformNavigatorRect())) return;
    const uint32_t total = slot->asset->frameCount();
    const CGFloat deltaX = [event scrollingDeltaX];
    const CGFloat deltaY = [event scrollingDeltaY];
    const bool horizontal = std::abs(deltaX) > std::abs(deltaY)
        || ([event modifierFlags] & NSEventModifierFlagShift) != 0u;
    if (horizontal && _zoom > 1.0) {
        const uint32_t visible = [self visibleFrames:total];
        const CGFloat gesture = std::abs(deltaX) > 0.0 ? deltaX : deltaY;
        _horizontalPanRemainder += static_cast<double>(gesture)
            * static_cast<double>(visible) / waveformRect().size.width
            * 0.35;
        const int64_t frameDelta = static_cast<int64_t>(
            _horizontalPanRemainder);
        _horizontalPanRemainder -= static_cast<double>(frameDelta);
        const int64_t maximum = total > visible ? total - visible : 0u;
        _visibleStart = static_cast<uint32_t>(std::clamp<int64_t>(
            static_cast<int64_t>(_visibleStart) + frameDelta, 0, maximum));
        [self setNeedsDisplay:YES];
        return;
    }
    if (deltaY == 0.0) return;
    const uint32_t anchor = [self frameForX:point.x total:total];
    const CGFloat previousZoom = _zoom;
    _zoom *= deltaY > 0.0 ? 1.25 : 0.8;
    _zoom = std::clamp<CGFloat>(_zoom, 1.0,
        std::max<CGFloat>(1.0, static_cast<CGFloat>(total) / 32.0));
    if (_zoom != previousZoom) {
        const CGFloat fraction = std::clamp<CGFloat>((point.x
            - waveformRect().origin.x) / waveformRect().size.width,
            0.0, 1.0);
        const uint32_t visible = [self visibleFrames:total];
        const int64_t desired = static_cast<int64_t>(anchor)
            - static_cast<int64_t>(std::llround(fraction * visible));
        _visibleStart = static_cast<uint32_t>(std::clamp<int64_t>(desired,
            0, total > visible ? total - visible : 0u));
    }
    [self setNeedsDisplay:YES];
}

@end

namespace {

bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool floating)
{
    return !floating && api
        && std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0;
}

bool guiGetPreferredApi(const clap_plugin_t*, const char** api,
    bool* floating)
{
    if (!api || !floating) return false;
    *api = CLAP_WINDOW_API_COCOA;
    *floating = false;
    return true;
}

bool guiCreate(const clap_plugin_t* plugin, const char* api, bool floating)
{
    if (!guiIsApiSupported(plugin, api, floating)) return false;
    auto& instance = *self(plugin);
    if (instance.guiView) return true;
    S3GBreakbeatSlicerView* view = [[S3GBreakbeatSlicerView alloc]
        initWithPlugin:&instance];
    if (!view) return false;
    instance.guiView = (__bridge_retained void*)view;
    if (!s3g::clap_gui::createResponsiveViewport(instance.guiViewport, view,
            kGuiWidth, kGuiHeight, 620u, 420u)) {
        void* owned = instance.guiView;
        instance.guiView = nullptr;
        (void)(__bridge_transfer NSView*)owned;
        return false;
    }
    return true;
}

void destroyGui(Plugin& instance)
{
    if (instance.guiView) {
        [(__bridge S3GBreakbeatSlicerView*)instance.guiView stopTimer];
        s3g::clap_gui::destroyResponsiveViewport(instance.guiViewport,
            instance.guiView);
    }
}

void guiDestroy(const clap_plugin_t* plugin)
{
    destroyGui(*self(plugin));
}

bool guiSetScale(const clap_plugin_t*, double) { return true; }

bool guiGetSize(const clap_plugin_t* plugin, uint32_t* width,
    uint32_t* height)
{
    return s3g::clap_gui::getResponsiveViewportSize(
        self(plugin)->guiViewport, kGuiWidth, kGuiHeight, width, height,
        620u, 420u);
}

bool guiCanResize(const clap_plugin_t*) { return true; }

bool guiGetResizeHints(const clap_plugin_t*,
    clap_gui_resize_hints_t* hints)
{
    return s3g::clap_gui::getResponsiveResizeHints(hints);
}

bool guiAdjustSize(const clap_plugin_t* plugin, uint32_t* width,
    uint32_t* height)
{
    return s3g::clap_gui::adjustResponsiveViewportSize(
        self(plugin)->guiViewport, kGuiWidth, kGuiHeight, width, height,
        620u, 420u);
}

bool guiSetSize(const clap_plugin_t* plugin, uint32_t width, uint32_t height)
{
    return s3g::clap_gui::setResponsiveViewportSize(
        self(plugin)->guiViewport, width, height);
}

bool guiSetParent(const clap_plugin_t* plugin,
    const clap_window_t* window)
{
    if (!window || !window->api
        || std::strcmp(window->api, CLAP_WINDOW_API_COCOA) != 0
        || !window->cocoa) return false;
    auto& instance = *self(plugin);
    return s3g::clap_gui::setResponsiveViewportParent(instance.guiViewport,
        (__bridge NSView*)window->cocoa, instance.host);
}

bool guiSetTransient(const clap_plugin_t*, const clap_window_t*)
{
    return false;
}

void guiSuggestTitle(const clap_plugin_t*, const char*) {}

bool guiShow(const clap_plugin_t* plugin)
{
    auto& instance = *self(plugin);
    if (!instance.guiView || !s3g::clap_gui::setResponsiveViewportHidden(
            instance.guiViewport, false)) return false;
    [(__bridge S3GBreakbeatSlicerView*)instance.guiView startTimer];
    return true;
}

bool guiHide(const clap_plugin_t* plugin)
{
    auto& instance = *self(plugin);
    if (!instance.guiView) return false;
    [(__bridge S3GBreakbeatSlicerView*)instance.guiView stopTimer];
    return s3g::clap_gui::setResponsiveViewportHidden(
        instance.guiViewport, true);
}

const clap_plugin_gui_t gui {
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
    if (!id) return nullptr;
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &audioPorts;
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS_CONFIG) == 0)
        return &audioPortsConfig;
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS_CONFIG_INFO) == 0
        || std::strcmp(id, CLAP_EXT_AUDIO_PORTS_CONFIG_INFO_COMPAT) == 0)
        return &audioPortsConfigInfo;
    if (std::strcmp(id, CLAP_EXT_NOTE_PORTS) == 0) return &notePorts;
    if (std::strcmp(id, CLAP_EXT_NOTE_NAME) == 0) return &noteNames;
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) return &params;
    if (std::strcmp(id, CLAP_EXT_STATE) == 0) return &state;
#if defined(__APPLE__)
    if (std::strcmp(id, CLAP_EXT_GUI) == 0) return &gui;
#endif
    return nullptr;
}

const char* const stereoFeatures[] {
    CLAP_PLUGIN_FEATURE_INSTRUMENT,
    CLAP_PLUGIN_FEATURE_SAMPLER,
    CLAP_PLUGIN_FEATURE_STEREO,
    nullptr,
};

const char* const multichannelFeatures[] {
    CLAP_PLUGIN_FEATURE_INSTRUMENT,
    CLAP_PLUGIN_FEATURE_SAMPLER,
    CLAP_PLUGIN_FEATURE_SURROUND,
    CLAP_PLUGIN_FEATURE_AMBISONIC,
    nullptr,
};

const clap_plugin_descriptor_t multichannelDescriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-dsp.breakbeat-slicer",
    "s3g Sample Slicer 16",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.8.3",
    "Four-break fixed 16-output, 1-16 channel sample-locked slicer.",
    multichannelFeatures,
};

const clap_plugin_descriptor_t stereoDescriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-dsp.breakbeat-slicer-stereo",
    "s3g Sample Slicer 2",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.8.3",
    "Four-break fixed stereo slicer for mono and stereo files.",
    stereoFeatures,
};

const clap_plugin_t* createPlugin(const clap_plugin_factory_t*,
    const clap_host_t* host, const char* pluginId)
{
    if (!host || !pluginId) return nullptr;
    const clap_plugin_descriptor_t* selectedDescriptor = nullptr;
    uint32_t outputChannels = 0u;
    clap_id outputConfigId = CLAP_INVALID_ID;
    if (std::strcmp(pluginId, multichannelDescriptor.id) == 0) {
        selectedDescriptor = &multichannelDescriptor;
        outputChannels = 16u;
        outputConfigId = kSixteenChannelOutputConfigId;
    } else if (std::strcmp(pluginId, stereoDescriptor.id) == 0) {
        selectedDescriptor = &stereoDescriptor;
        outputChannels = 2u;
        outputConfigId = kStereoOutputConfigId;
    } else {
        return nullptr;
    }
    auto* instance = new (std::nothrow) Plugin();
    if (!instance) return nullptr;
    instance->host = host;
    instance->outputChannelCount = outputChannels;
    instance->outputConfigId = outputConfigId;
    instance->plugin.desc = selectedDescriptor;
    instance->plugin.plugin_data = instance;
    instance->plugin.init = pluginInit;
    instance->plugin.destroy = pluginDestroy;
    instance->plugin.activate = pluginActivate;
    instance->plugin.deactivate = pluginDeactivate;
    instance->plugin.start_processing = pluginStartProcessing;
    instance->plugin.stop_processing = pluginStopProcessing;
    instance->plugin.reset = pluginReset;
    instance->plugin.process = pluginProcess;
    instance->plugin.get_extension = pluginGetExtension;
    instance->plugin.on_main_thread = pluginOnMainThread;
    return &instance->plugin;
}

uint32_t factoryGetPluginCount(const clap_plugin_factory_t*) { return 2u; }

const clap_plugin_descriptor_t* factoryGetPluginDescriptor(
    const clap_plugin_factory_t*, uint32_t index)
{
    if (index == 0u) return &multichannelDescriptor;
    if (index == 1u) return &stereoDescriptor;
    return nullptr;
}

const clap_plugin_factory_t factory {
    factoryGetPluginCount,
    factoryGetPluginDescriptor,
    createPlugin,
};

bool entryInit(const char*) { return true; }
void entryDeinit() {}

const void* entryGetFactory(const char* factoryId)
{
    return factoryId && std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0
        ? &factory : nullptr;
}

} // namespace

extern "C" const clap_plugin_entry_t clap_entry {
    CLAP_VERSION_INIT,
    entryInit,
    entryDeinit,
    entryGetFactory,
};
