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
#include "../common/s3g_cocoa_gui.h"
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <vector>

namespace {

using s3g::breakbeat::BankSnapshot;
using s3g::breakbeat::EventKind;
using s3g::breakbeat::FilterMode;
using s3g::breakbeat::InsertSettings;
using s3g::breakbeat::InsertType;
using s3g::breakbeat::MixerSnapshot;
using s3g::breakbeat::RenderEvent;
using s3g::breakbeat::SampleAnalysis;
using s3g::breakbeat::SampleAsset;
using s3g::breakbeat::Slice;

constexpr uint32_t kStateMagic = 0x53423353u; // "S3BS"
constexpr uint32_t kLegacyStateVersion = 9u;
constexpr uint32_t kStateVersion = 10u;
constexpr uint32_t kMaximumTransientPreRollMicroseconds = 20000u;
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
    uint8_t rootNote = 36u;
    uint8_t mappedRootNote = 36u;
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

#if defined(__APPLE__)
struct LoadRequest {
    uint32_t slotIndex = 0u;
    uint64_t generation = 0u;
    std::string path;
};

struct LoadResult {
    uint32_t slotIndex = 0u;
    uint64_t generation = 0u;
    std::string path;
    std::shared_ptr<const SampleAsset> asset;
    std::shared_ptr<const SampleAnalysis> analysis;
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
    std::atomic<uint64_t> auditionCounter { 1u };
    std::atomic<float> outputPeak { 0.0f };
    std::array<std::atomic<float>,
        s3g::breakbeat::kMaximumSampleSlots> slotPlayheads {};
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
    uint32_t selectedSlot = 0u;
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
    if (!slot.asset) return false;
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

bool automapSlot(Plugin& instance, uint32_t slotIndex,
    bool* rootAdjusted = nullptr)
{
    if (rootAdjusted) *rootAdjusted = false;
    if (slotIndex >= s3g::breakbeat::kMaximumSampleSlots) return false;
    auto bank = editableBank(instance);
    auto& slot = bank->slots[slotIndex];
    if (!slot.asset || slot.sliceCount == 0u) return false;
    const uint8_t previousRoot = slot.rootNote;
    if (!s3g::breakbeat::autoMapSlotConsecutively(*bank,
            static_cast<uint8_t>(slotIndex), true)) return false;
    if (rootAdjusted) *rootAdjusted = slot.rootNote != previousRoot;
    return publishBank(instance, std::move(bank));
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
        instance.status = "USE S3G SLICER 16 FOR THIS FILE";
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
    markStateDirty(instance);
    if (sourceChannelCount > 2u) {
        instance.status = "SAMPLE READY - AUTO MAP TO PLAY";
    } else {
        instance.status = "SAMPLE READY - AUTO MAP TO PLAY";
    }
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
                result.slotIndex = request.slotIndex;
                result.generation = request.generation;
                result.path = std::move(request.path);
                try {
                    decodeSampleFile(result.path, result.asset,
                        result.analysis, result.error);
                } catch (...) {
                    result.asset.reset();
                    result.analysis.reset();
                    result.error = "SAMPLE DECODE RAN OUT OF RESOURCES";
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
    const uint64_t generation = ++instance.loadGenerations[slotIndex];
    {
        std::lock_guard<std::mutex> lock(instance.loaderMutex);
        instance.loadRequests.push_back({ slotIndex, generation,
            std::move(path) });
    }
    instance.status = "LOADING AND ANALYZING SAMPLE";
    instance.loaderCondition.notify_one();
}

void cancelSampleLoad(Plugin& instance, uint32_t slotIndex)
{
    if (slotIndex >= instance.loadGenerations.size()) return;
    ++instance.loadGenerations[slotIndex];
}

void cancelAllSampleLoads(Plugin& instance)
{
    for (auto& generation : instance.loadGenerations) ++generation;
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
        if (result.slotIndex >= instance.loadGenerations.size()
            || result.generation
                != instance.loadGenerations[result.slotIndex]) continue;
        if (!result.asset || !result.analysis) {
            instance.status = result.error.empty()
                ? "SAMPLE DECODE FAILED" : result.error;
            continue;
        }
        (void)installDecodedSample(instance, result.slotIndex, result.path,
            std::move(result.asset), std::move(result.analysis));
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

void pluginDestroy(const clap_plugin_t* plugin)
{
#if defined(__APPLE__)
    stopSampleLoader(*self(plugin));
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
    for (auto& playhead : instance.slotPlayheads)
        playhead.store(-1.0f, std::memory_order_relaxed);
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
    for (auto& playhead : instance.slotPlayheads)
        playhead.store(-1.0f, std::memory_order_relaxed);
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
    std::size_t renderEventCount = 0u;
    readInputEvents(instance, process->in_events, frames, renderEventCount);
    std::array<float*, s3g::breakbeat::kMaximumAudioChannels> scratch {};
    for (uint32_t channel = 0u; channel < instance.outputChannelCount;
         ++channel)
        scratch[channel] = instance.scratchChannels[channel].data();
    instance.engine.render(instance.blockEvents.data(), renderEventCount,
        scratch.data(), instance.outputChannelCount, frames);
    for (std::size_t slot = 0u; slot < instance.slotPlayheads.size(); ++slot)
        instance.slotPlayheads[slot].store(
            instance.engine.slotPlayheadNormalized(
                static_cast<uint8_t>(slot)),
            std::memory_order_relaxed);
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
            if (instance.embedSamplesInState
                && bytes <= kMaximumEmbeddedAudioBytes - embeddedBytes) {
                destination.embedded = 1u;
                embeddedBytes += bytes;
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
    return true;
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) return false;
    SavedState saved;
    if (!s3g::clap_state::readAll(stream, &saved, sizeof(saved))
        || saved.magic != kStateMagic
        || (saved.version != kLegacyStateVersion
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
                    = "STATE REQUIRES S3G SLICER 16";
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
                    = "STATE REQUIRES S3G SLICER 16";
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
    if (!bank->valid()
        || !publishBank(instance, std::move(bank), false)) return false;
    instance.selectedSlot = std::min<uint32_t>(saved.selectedSlot,
        static_cast<uint32_t>(s3g::breakbeat::kMaximumSampleSlots - 1u));
    setParam(instance, kOutputGainParamId, saved.outputGainDb);
    setParam(instance, kVelocityParamId, saved.velocitySensitivity);
    instance.embedSamplesInState = saved.embedSamples != 0u;
    instance.transientPreRollMicroseconds
        = saved.version == kStateVersion
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
    return NSMakeRect(18.0, 74.0 + static_cast<CGFloat>(index) * 112.0,
        250.0, 104.0);
}

NSRect slotChannelRect(uint32_t index)
{
    const NSRect row = bankRowRect(index);
    return NSMakeRect(row.origin.x + 9.0, row.origin.y + 70.0,
        86.0, 24.0);
}

NSRect slotAutomapRect(uint32_t index)
{
    const NSRect row = bankRowRect(index);
    return NSMakeRect(row.origin.x + 103.0, row.origin.y + 70.0,
        138.0, 24.0);
}

NSRect overviewWaveformRect(uint32_t index)
{
    return NSMakeRect(292.0, 74.0 + static_cast<CGFloat>(index) * 112.0,
        770.0, 104.0);
}

NSRect waveformRect()
{
    return NSMakeRect(292.0, 74.0, 770.0, 454.0);
}

NSRect waveformNavigatorRect()
{
    return NSMakeRect(292.0, 533.0, 770.0, 10.0);
}

NSRect controlButtonRect(uint32_t index)
{
    constexpr CGFloat widths[] { 72.0, 68.0, 52.0, 52.0, 52.0, 52.0,
        92.0, 54.0, 54.0, 82.0 };
    CGFloat x = 292.0;
    for (uint32_t i = 0u; i < index; ++i) x += widths[i] + 7.0;
    return NSMakeRect(x, 551.0, widths[index], 30.0);
}

NSRect transientPreRollButtonRect(uint32_t index)
{
    constexpr CGFloat widths[] { 32.0, 142.0, 32.0 };
    CGFloat x = 292.0;
    for (uint32_t i = 0u; i < index; ++i) x += widths[i] + 7.0;
    return NSMakeRect(x, 588.0, widths[index], 26.0);
}

NSRect sliceControlButtonRect(uint32_t index)
{
    constexpr CGFloat widths[] { 64.0, 64.0, 72.0, 72.0, 60.0,
        60.0, 72.0, 86.0, 68.0, 68.0 };
    CGFloat x = 292.0;
    for (uint32_t i = 0u; i < index; ++i) x += widths[i] + 7.0;
    return NSMakeRect(x, 646.0, widths[index], 30.0);
}

NSRect editorTabRect(uint32_t index)
{
    return NSMakeRect(292.0 + static_cast<CGFloat>(index) * 112.0,
        42.0, 104.0, 24.0);
}

NSRect embedAudioButtonRect()
{
    return NSMakeRect(858.0, 42.0, 204.0, 24.0);
}

NSRect keyboardRect()
{
    return NSMakeRect(292.0, 706.0, 770.0, 48.0);
}

NSRect envelopePanelRect()
{
    return NSMakeRect(18.0, 530.0, 250.0, 218.0);
}

CGFloat envelopeSliderY(uint32_t index)
{
    return 586.0 + static_cast<CGFloat>(index) * 38.0;
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
        static_cast<int>(note / 12u) - 1, static_cast<unsigned>(note)];
}

NSString* shortNoteText(uint8_t note)
{
    static NSArray<NSString*>* names = @[
        @"C", @"C#", @"D", @"D#", @"E", @"F",
        @"F#", @"G", @"G#", @"A", @"A#", @"B"
    ];
    return [NSString stringWithFormat:@"%@%d", names[note % 12u],
        static_cast<int>(note / 12u) - 1];
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
    std::shared_ptr<BankSnapshot> _dragBank;
    CGFloat _zoom;
    uint32_t _visibleStart;
    BOOL _dragViewport;
    CGFloat _viewportGrabOffset;
    double _horizontalPanRemainder;
    uint32_t _page;
    NSInteger _envelopeDragIndex;
    NSInteger _mixerDragKind;
    uint32_t _mixerDragSlot;
    NSPoint _mixerDragStartPoint;
    double _mixerDragStartValue;
    NSInteger _insertEditorSlot;
    uint32_t _insertEditorIndex;
}
- (instancetype)initWithPlugin:(Plugin*)instance;
- (void)startTimer;
- (void)stopTimer;
- (void)selectMidiChannel:(NSMenuItem*)sender;
- (void)selectTransientPreRoll:(NSMenuItem*)sender;
@end

@implementation S3GBreakbeatSlicerView

- (instancetype)initWithPlugin:(Plugin*)instance
{
    self = [super initWithFrame:NSMakeRect(0.0, 0.0, kGuiWidth, kGuiHeight)];
    if (self) {
        _instance = instance;
        _selectedSlice = 0;
        _dragMarker = -1;
        _zoom = 1.0;
        _visibleStart = 0u;
        _dragViewport = NO;
        _viewportGrabOffset = 0.0;
        _horizontalPanRemainder = 0.0;
        _page = 0u;
        _envelopeDragIndex = -1;
        _mixerDragKind = 0;
        _mixerDragSlot = 0u;
        _mixerDragStartPoint = NSZeroPoint;
        _mixerDragStartValue = 0.0;
        _insertEditorSlot = -1;
        _insertEditorIndex = 0u;
        [self setWantsLayer:YES];
        [self registerForDraggedTypes:@[ NSPasteboardTypeFileURL ]];
    }
    return self;
}

- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }

- (void)selectMidiChannel:(NSMenuItem*)sender
{
    const NSInteger tag = [sender tag];
    if (tag < 0 || tag > 16) return;
    if (setSlotMidiChannel(*_instance, [self selectedSlot],
            static_cast<uint8_t>(tag))) {
        _instance->status = tag == 0
            ? "BREAK MIDI SET TO OMNI" : "BREAK MIDI CHANNEL UPDATED";
        [self setNeedsDisplay:YES];
    }
}

- (void)selectTransientPreRoll:(NSMenuItem*)sender
{
    const NSInteger tag = [sender tag];
    if (tag < 0) return;
    _instance->transientPreRollMicroseconds = std::min<uint32_t>(
        static_cast<uint32_t>(tag),
        kMaximumTransientPreRollMicroseconds);
    markStateDirty(*_instance);
    _instance->status = "TRANSIENT PRE-ROLL UPDATED";
    [self setNeedsDisplay:YES];
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
            [view setNeedsDisplay:YES];
        }];
}

- (void)stopTimer
{
    [_timer invalidate];
    _timer = nil;
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

    const float playhead = _instance->slotPlayheads[[self selectedSlot]].load(
        std::memory_order_relaxed);
    if (playhead >= 0.0f) {
        const uint32_t frame = std::min<uint32_t>(total - 1u,
            static_cast<uint32_t>(playhead * static_cast<float>(total)));
        const CGFloat x = [self xForFrame:frame total:total];
        if (x >= rect.origin.x && x <= NSMaxX(rect)) {
            [s3g::clap_gui::color(0xa9d18e) setStroke];
            NSBezierPath* cursor = [NSBezierPath bezierPath];
            [cursor setLineWidth:2.0];
            [cursor moveToPoint:NSMakePoint(x, rect.origin.y + 1.0)];
            [cursor lineToPoint:NSMakePoint(x, NSMaxY(rect) - 1.0)];
            [cursor stroke];
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
    const float playhead = _instance->slotPlayheads[slotIndex].load(
        std::memory_order_relaxed);
    if (playhead >= 0.0f) {
        const CGFloat x = rect.origin.x + rect.size.width * playhead;
        [s3g::clap_gui::color(0xa9d18e) setStroke];
        NSBezierPath* cursor = [NSBezierPath bezierPath];
        [cursor setLineWidth:2.0];
        [cursor moveToPoint:NSMakePoint(x, top)];
        [cursor lineToPoint:NSMakePoint(x, NSMaxY(rect) - 2.0)];
        [cursor stroke];
    }
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
        const NSRect key = NSMakeRect(keyboard.origin.x + index * width,
            keyboard.origin.y, width, keyboard.size.height);
        [s3g::clap_gui::color(mapped ? 0x26352c
            : black ? 0x171717 : 0x292929) setFill];
        NSRectFill(key);
        [s3g::clap_gui::color(mapped ? 0x59705f : 0x4a4a4a) setStroke];
        NSFrameRect(key);
        drawCentered(NSMakeRect(key.origin.x, key.origin.y + 3.0,
            key.size.width, 17.0), shortNoteText(static_cast<uint8_t>(note)),
            attrs);
        NSDictionary* numberAttrs = slicerTextAttrs(
            s3g::clap_gui::color(mapped ? 0x9fb4a5 : 0x858585), 8.0,
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
    [s3g::clap_gui::color(0x090909) setFill];
    NSRectFill(self.bounds);
    // Match the resolved 17.5 pt header used by "s3g TRACKER". Slicer's
    // shared font helper uses a slightly different readability scale, so a
    // 16 pt design size produces the same rendered title size here.
    NSDictionary* title = slicerTextAttrs(
        s3g::clap_gui::color(0xc8c8c8), 16.0);
    NSDictionary* label = slicerTextAttrs(
        s3g::clap_gui::color(0xa6a6a6), 10.5);
    NSDictionary* value = slicerTextAttrs(
        s3g::clap_gui::color(0xc6c6c6), 11.0);
    [@"s3g SLICER" drawAtPoint:NSMakePoint(18.0, 17.0)
        withAttributes:title];
    NSString* variant = [NSString stringWithFormat:@"%u OUT",
        static_cast<unsigned>(_instance->outputChannelCount)];
    [variant drawAtPoint:NSMakePoint(132.0, 21.0) withAttributes:label];
    NSString* status = [NSString stringWithUTF8String:
        _instance->status.c_str()];
    if (!status) status = @"";
    s3g::clap_gui::drawRightStatus(status, kGuiWidth, 22.0, label);
    [(_page == 2u ? @"INTERNAL BREAK MIXER" : @"FOUR BREAK BANK")
        drawAtPoint:NSMakePoint(18.0, 51.0)
        withAttributes:label];
    drawButton(editorTabRect(0u), @"OVERVIEW", _page == 0u);
    drawButton(editorTabRect(1u), @"BREAK EDIT", _page == 1u);
    drawButton(editorTabRect(2u), @"MIXER", _page == 2u);
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
    drawButton(embedAudioButtonRect(), projectAudioLabel,
        _instance->embedSamplesInState);

    const auto* bank = [self displayBank];
    if (!bank) return;
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
            drawAtPoint:NSMakePoint(row.origin.x + 10.0, row.origin.y + 9.0)
            withAttributes:value];
        NSString* filename = slotFilename(*_instance, index);
        NSRect nameRect = NSMakeRect(row.origin.x + 78.0, row.origin.y + 8.0,
            158.0, 17.0);
        [filename drawInRect:nameRect withAttributes:label];
        const auto& slot = bank->slots[index];
        NSString* meta = slot.asset
            ? [NSString stringWithFormat:@"%u SLICES   %u CH   ROOT %@",
                slot.sliceCount,
                static_cast<unsigned>(slot.asset->channelCount),
                noteText(slot.rootNote)] : @"";
        [meta drawAtPoint:NSMakePoint(row.origin.x + 10.0,
            row.origin.y + 37.0) withAttributes:label];
        NSString* channel = slot.midiChannel == 0u ? @"MIDI OMNI"
            : [NSString stringWithFormat:@"MIDI CH %u",
                static_cast<unsigned>(slot.midiChannel)];
        drawButton(slotChannelRect(index), channel);
        const bool mapped = slot.asset && slot.mappedSliceCount > 0u
            && slot.mappedSliceCount == slot.sliceCount
            && slot.mappedRootNote == slot.rootNote;
        NSString* mapLabel = mapped
            ? [NSString stringWithFormat:@"MAPPED %@ +%u",
                shortNoteText(slot.mappedRootNote),
                static_cast<unsigned>(slot.mappedSliceCount - 1u)]
            : @"AUTO MAP";
        drawButton(slotAutomapRect(index), mapLabel, mapped);
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
    } else {
        [self drawWaveformForSlot:slot];
        drawButton(controlButtonRect(0u), @"LOAD");
        drawButton(controlButtonRect(1u), @"CLEAR");
        drawButton(controlButtonRect(2u), @"EQ 4");
        drawButton(controlButtonRect(3u), @"EQ 8");
        drawButton(controlButtonRect(4u), @"EQ 16");
        drawButton(controlButtonRect(5u), @"EQ 32");
        drawButton(controlButtonRect(6u), @"TRANSIENT");
        drawButton(controlButtonRect(7u), @"ROOT -");
        drawButton(controlButtonRect(8u), @"ROOT +");
        drawButton(controlButtonRect(9u), @"AUDITION");
        drawButton(transientPreRollButtonRect(0u), @"-");
        drawButton(transientPreRollButtonRect(1u),
            [NSString stringWithFormat:@"PRE %u µs",
                _instance->transientPreRollMicroseconds]);
        drawButton(transientPreRollButtonRect(2u), @"+");
        [@"TRANSIENT PRE-ROLL   ±100 µs   OPTION 10   SHIFT 1000"
            drawAtPoint:NSMakePoint(523.0, 595.0)
            withAttributes:label];

        if (slot.asset && slot.sliceCount > 0u) {
            const double seconds = slot.asset->frameCount()
                / slot.asset->sampleRate;
            NSString* details = [NSString stringWithFormat:
                @"%u CH   %.2f S   %.0f HZ   %u SLICES   ROOT %@   ZOOM %.1fX",
                static_cast<unsigned>(slot.asset->channelCount), seconds,
                slot.asset->sampleRate, slot.sliceCount,
                noteText(slot.rootNote), _zoom];
            [details drawAtPoint:NSMakePoint(292.0, 620.0)
                withAttributes:value];
            const std::size_t selected = static_cast<std::size_t>(
                std::clamp<NSInteger>(_selectedSlice, 0,
                    static_cast<NSInteger>(slot.sliceCount) - 1));
            const auto& slice = slot.slices[selected];
            const NSRect envelopePanel = envelopePanelRect();
            s3g::clap_gui::Style envelopeStyle;
            s3g::clap_gui::drawPanelFrame(NSMinX(envelopePanel),
                NSMinY(envelopePanel), NSWidth(envelopePanel),
                NSHeight(envelopePanel), envelopeStyle);
            s3g::clap_gui::drawPanelHeader(@"BREAK ENVELOPE", true,
                NSMinX(envelopePanel), NSMinY(envelopePanel),
                NSWidth(envelopePanel), 24.0, label, envelopeStyle);
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
                    value, envelopeStyle);
            }
            NSString* sliceDetails = [NSString stringWithFormat:
                @"SLICE %03u   GAIN %.2f   PITCH %+.0f ST   PAN %+.1f   %@   CHOKE %u%@",
                static_cast<unsigned>(selected), slice.gain,
                slice.transposeSemitones, slice.pan,
                launchModeText(slice.launchMode),
                static_cast<unsigned>(slice.chokeGroup),
                slice.reverse ? @"   REVERSE" : @""];
            [sliceDetails drawAtPoint:NSMakePoint(292.0, 619.0)
                withAttributes:value];
            drawButton(sliceControlButtonRect(0u), @"GAIN -");
            drawButton(sliceControlButtonRect(1u), @"GAIN +");
            drawButton(sliceControlButtonRect(2u), @"PITCH -");
            drawButton(sliceControlButtonRect(3u), @"PITCH +");
            drawButton(sliceControlButtonRect(4u),
                slot.asset->channelCount > 2u ? @"PAN N/A" : @"PAN -");
            drawButton(sliceControlButtonRect(5u),
                slot.asset->channelCount > 2u ? @"PAN N/A" : @"PAN +");
            drawButton(sliceControlButtonRect(6u), @"REVERSE",
                slice.reverse);
            drawButton(sliceControlButtonRect(7u), @"MODE");
            drawButton(sliceControlButtonRect(8u), @"CHOKE -");
            drawButton(sliceControlButtonRect(9u), @"CHOKE +");
            NSString* help = @"KEYS AUDITION   DOUBLE-CLICK ADD   DRAG MARKER   RIGHT-CLICK DELETE   SCROLL ZOOM";
            [help drawAtPoint:NSMakePoint(292.0, 684.0)
                withAttributes:label];
            [self drawKeyboardForSlot:slot];
        }
    }
    const float peak = _instance->outputPeak.load(std::memory_order_relaxed);
    NSString* outputWidth = [NSString stringWithFormat:@"OUT %u CH FIXED",
        static_cast<unsigned>(_instance->outputChannelCount)];
    NSString* output = [NSString stringWithFormat:
        @"%@ / %+.1f DB   VEL %.0f%%   PER-BREAK MIDI   PK %+.1f DB",
        outputWidth,
        _instance->outputGainDb.load(std::memory_order_relaxed),
        _instance->velocitySensitivity.load(std::memory_order_relaxed) * 100.0,
        20.0 * std::log10(std::max(0.000001f, peak))];
    [output drawAtPoint:NSMakePoint(292.0, 775.0) withAttributes:label];
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
        markStateDirty(*_instance);
        _instance->status = "SLOT CLEARED";
    }
    _selectedSlice = 0;
}

- (void)makeEqual:(std::size_t)count
{
    const auto* slot = [self selectedSampleSlot];
    if (!slot || !slot->asset) return;
    if (replaceSlotSlices(*_instance, [self selectedSlot],
            s3g::breakbeat::makeEqualSlices(*slot->asset, count))) {
        _selectedSlice = 0;
        _instance->status = "SLICES CHANGED - PRESS AUTO MAP";
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
                64u, zeroRadius,
                _instance->transientPreRollMicroseconds))) {
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
    for (uint32_t index = 0u; index < 3u; ++index) {
        if (NSPointInRect(point, editorTabRect(index))) {
            _page = index;
            [self setNeedsDisplay:YES];
            return;
        }
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
            const auto* bank = [self displayBank];
            if (!bank) return;
            _instance->selectedSlot = index;
            NSMenu* menu = [[NSMenu alloc] initWithTitle:@"MIDI CHANNEL"];
            for (NSInteger channel = 0; channel <= 16; ++channel) {
                NSString* itemTitle = channel == 0 ? @"Omni"
                    : [NSString stringWithFormat:@"Channel %ld",
                        static_cast<long>(channel)];
                NSMenuItem* item = [[NSMenuItem alloc]
                    initWithTitle:itemTitle
                    action:@selector(selectMidiChannel:)
                    keyEquivalent:@""];
                [item setTarget:self];
                [item setTag:channel];
                [item setState:bank->slots[index].midiChannel == channel
                    ? NSControlStateValueOn : NSControlStateValueOff];
                [menu addItem:item];
            }
            [menu popUpMenuPositioningItem:nil atLocation:NSMakePoint(
                slotChannelRect(index).origin.x,
                NSMaxY(slotChannelRect(index))) inView:self];
            return;
        }
        if (NSPointInRect(point, slotAutomapRect(index))) {
            bool rootAdjusted = false;
            if (automapSlot(*_instance, index, &rootAdjusted))
                _instance->status = rootAdjusted
                    ? "AUTO-MAPPED; ROOT LOWERED TO FIT ALL SLICES"
                    : "BREAK SLICES AUTO-MAPPED";
            else
                _instance->status = "LOAD A BREAK BEFORE AUTO MAP";
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
    for (uint32_t index = 0u; index < 3u; ++index) {
        if (!NSPointInRect(point, transientPreRollButtonRect(index)))
            continue;
        if (index == 1u) {
            static constexpr std::array<uint32_t, 15u> values {{
                0u, 100u, 250u, 500u, 750u, 1000u, 1500u, 2000u,
                3000u, 4000u, 6000u, 8000u, 10000u, 15000u, 20000u,
            }};
            NSMenu* menu = [[NSMenu alloc]
                initWithTitle:@"TRANSIENT PRE-ROLL"];
            for (const uint32_t value : values) {
                NSString* title = value == 0u ? @"Off (0 µs)"
                    : [NSString stringWithFormat:@"%u µs", value];
                NSMenuItem* item = [[NSMenuItem alloc]
                    initWithTitle:title
                    action:@selector(selectTransientPreRoll:)
                    keyEquivalent:@""];
                [item setTarget:self];
                [item setTag:static_cast<NSInteger>(value)];
                [item setState:_instance->transientPreRollMicroseconds
                        == value
                    ? NSControlStateValueOn : NSControlStateValueOff];
                [menu addItem:item];
            }
            [menu popUpMenuPositioningItem:nil atLocation:NSMakePoint(
                NSMinX(transientPreRollButtonRect(index)),
                NSMaxY(transientPreRollButtonRect(index))) inView:self];
            return;
        }
        const NSEventModifierFlags modifiers = [event modifierFlags];
        const uint32_t step = (modifiers & NSEventModifierFlagShift) != 0u
            ? 1000u
            : (modifiers & NSEventModifierFlagOption) != 0u ? 10u : 100u;
        const int64_t delta = index == 0u
            ? -static_cast<int64_t>(step) : static_cast<int64_t>(step);
        _instance->transientPreRollMicroseconds = static_cast<uint32_t>(
            std::clamp<int64_t>(static_cast<int64_t>(
                    _instance->transientPreRollMicroseconds) + delta,
                0, kMaximumTransientPreRollMicroseconds));
        markStateDirty(*_instance);
        _instance->status = "TRANSIENT PRE-ROLL UPDATED";
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
    for (uint32_t index = 0u; index < 10u; ++index) {
        if (!NSPointInRect(point, controlButtonRect(index))) continue;
        if (index == 0u) [self openSamples];
        else if (index == 1u) [self clearSelectedSlot];
        else if (index >= 2u && index <= 5u)
            [self makeEqual:static_cast<std::size_t>(1u << index)];
        else if (index == 6u) [self makeTransient];
        else if (index == 7u || index == 8u) {
            const auto* slot = [self selectedSampleSlot];
            if (slot && slot->asset) {
                const int delta = index == 7u ? -1 : 1;
                const int maximum = 128 - slot->sliceCount;
                const uint8_t root = static_cast<uint8_t>(std::clamp(
                    static_cast<int>(slot->rootNote) + delta, 0, maximum));
                if (setSlotRootNote(*_instance, [self selectedSlot], root))
                    _instance->status = "ROOT NOTE UPDATED";
            }
        } else if (index == 9u) [self audition];
        [self setNeedsDisplay:YES];
        return;
    }
    for (uint32_t index = 0u; index < 10u; ++index) {
        if (!NSPointInRect(point, sliceControlButtonRect(index))) continue;
        [self editSelectedSliceControl:index];
        [self setNeedsDisplay:YES];
        return;
    }
    if (!NSPointInRect(point, waveformRect())) return;
    const auto* slot = [self selectedSampleSlot];
    if (!slot || !slot->asset) return;
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
        if (s3g::breakbeat::addSliceMarker(edited.slices.data(), count,
                edited.slices.size(), snapped)) {
            edited.sliceCount = static_cast<uint16_t>(count);
            edited.mappedSliceCount = 0u;
            publishBank(*_instance, std::move(bank));
            _selectedSlice = [self sliceAtFrame:snapped];
            _instance->status = "MARKER ADDED - PRESS AUTO MAP";
        }
    } else {
        _selectedSlice = [self sliceAtFrame:frame];
    }
    [self setNeedsDisplay:YES];
}

- (void)mouseDragged:(NSEvent*)event
{
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

void guiDestroy(const clap_plugin_t* plugin)
{
    auto& instance = *self(plugin);
    if (instance.guiView) {
        [(__bridge S3GBreakbeatSlicerView*)instance.guiView stopTimer];
        s3g::clap_gui::destroyResponsiveViewport(instance.guiViewport,
            instance.guiView);
    }
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
    "s3g Slicer",
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
    "s3g Slicer 2",
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
