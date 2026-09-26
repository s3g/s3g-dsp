#include "s3g_sample_neon.h"
#include "s3g_neon_midi_bridge.h"
#include "s3g_sample_neon_edit.h"
#include "s3g_sample_cutups_analysis.h"
#include "../common/s3g_clap_gui_param_queue.h"
#include "../common/s3g_clap_state_stream.h"
#include "../common/s3g_sample_file_decode.h"

#if defined(S3G_ENABLE_VSTGUI_SAMPLE_NEON_GUI)
#include "s3g_sample_neon_layout.h"
#include "../common/s3g_clap_vstgui.h"
#include "../common/s3g_vstgui_canvas.h"
#endif

#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/gui.h>
#include <clap/ext/note-name.h>
#include <clap/ext/note-ports.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>

#if defined(__APPLE__)
#import <AVFoundation/AVFoundation.h>
#import <Cocoa/Cocoa.h>
#import <CoreMIDI/CoreMIDI.h>
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <vector>

namespace {

using NeonAction = s3g::controller::reloop_neon::Action;
using NeonActionType = s3g::controller::reloop_neon::ActionType;
using NeonEncoder = s3g::controller::reloop_neon::Encoder;
using NeonMidi = s3g::controller::reloop_neon::MidiMessage;
using NeonMode = s3g::controller::reloop_neon::Mode;
using NeonLamp = s3g::controller::reloop_neon::PadStatusLamp;
using NeonPerformanceState =
    s3g::controller::reloop_neon::PerformanceState;
using NeonUtility = s3g::controller::reloop_neon::UtilityButton;
using s3g::sample::FilterType;
using s3g::sample::RetriggerMode;
using s3g::sample::SampleAsset;
using s3g::sample::SampleNeonEngine;
using s3g::sample::SampleNeonEvent;
using s3g::sample::SampleNeonEventKind;
using s3g::sample::SampleNeonChopMode;
using s3g::sample::SampleNeonMangleCharacter;
using s3g::sample::SampleNeonMotionPath;
using s3g::sample::SampleNeonPlayback;
using s3g::sample::SampleNeonClock;
using s3g::sample::SampleNeonSliceLayout;
using s3g::sample::SampleNeonSliceTriggerMode;
using s3g::sample::SampleNeonSettings;
using s3g::sample::SampleNeonSourceFormat;
using s3g::sample::SampleNeonOutputLayout;
using s3g::sample::TriggerMode;

constexpr uint32_t kStateMagic = 0x4e533353u; // "S3SN"
constexpr uint32_t kStateVersion = 14u;
constexpr std::size_t kMaximumPathBytes = 2048u;
constexpr std::size_t kSliceModeStateBytes =
    s3g::sample::kSampleNeonSlotCount
        * s3g::sample::kSampleNeonSliceCount;
constexpr std::size_t kSliceCountStateBytes =
    s3g::sample::kSampleNeonSlotCount;
constexpr std::size_t kSliceOptionStateBytes = kSliceModeStateBytes;
constexpr uint8_t kSliceRepeatOption = 1u << 0u;
constexpr uint8_t kSliceSyncOption = 1u << 1u;
constexpr uint8_t kSlotRepeatOption = 1u << 0u;
constexpr uint8_t kSlotSyncOption = 1u << 1u;
constexpr uint8_t kSlotVelocityOption = 1u << 2u;
constexpr uint8_t kMotionOption = 1u << 0u;
constexpr uint8_t kGrainsOption = 1u << 2u;
constexpr uint8_t kSequenceOption = 1u << 3u;
constexpr uint8_t kStretchOption = 1u << 4u;
constexpr uint8_t kWavesetsOption = 1u << 5u;
constexpr std::array<uint8_t, 6u> kPlaybackOptions {{0u, kMotionOption, kGrainsOption, kSequenceOption, kStretchOption, kWavesetsOption}};
constexpr std::array<const char*, 6u> kPlaybackNames {{"SAMPLE", "MOTION", "GRAINS", "SLICE SEQUENCE", "STRETCH", "WAVESETS"}};

constexpr uint8_t normalizedStageOptions(uint8_t options) noexcept
{
    // Older sets may combine stages. Commit to Grains first, then Motion;
    // adjacent-cell mixing is intentionally retired.
    return (options & kWavesetsOption) ? kWavesetsOption
        : (options & kStretchOption) ? kStretchOption
        : (options & kSequenceOption) ? kSequenceOption
        : (options & kGrainsOption) ? kGrainsOption
        : (options & kMotionOption) ? kMotionOption : 0u;
}

constexpr std::array<float, 8u> kMotionCycleBeats {{
    0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f, 16.0f, 32.0f,
}};

std::size_t nearestMotionCycleIndex(float beats) noexcept
{
    std::size_t best = 0u;
    float distance = std::abs(beats - kMotionCycleBeats[0u]);
    for (std::size_t index = 1u; index < kMotionCycleBeats.size(); ++index) {
        const float candidate = std::abs(beats - kMotionCycleBeats[index]);
        if (candidate < distance) {
            best = index;
            distance = candidate;
        }
    }
    return best;
}
constexpr std::size_t kFlipPatternCount = 8u;
constexpr std::size_t kFlipMaximumSteps = 16u;
constexpr std::array<double, 6u> kSlicerDomainBeats {{
    2.0, 4.0, 8.0, 16.0, 32.0, 64.0,
}};
constexpr std::array<double, 4u> kSlicerQuantizeBeats {{
    0.125, 0.25, 0.5, 1.0,
}};
constexpr uint32_t kGuiWidth = 1180u;
constexpr uint32_t kGuiHeight = 760u;

constexpr clap_id kOutputLayoutParamId = 1u;
constexpr clap_id kMasterGainParamId = 2u;
constexpr clap_id kGlobalMangleParamId = 3u;
constexpr clap_id kBaseNoteParamId = 4u;
constexpr clap_id kNeonActiveParamId = 5u;
constexpr clap_id kTempoSyncParamId = 6u;
constexpr clap_id kMidiReceiveParamId = 7u;
constexpr std::size_t kGlobalParamCount = 7u;

constexpr clap_id kSlotParamBase = 1000u;
constexpr clap_id kSlotParamStride = 32u;
enum SlotParamOffset : clap_id {
    kSlotGain = 0u,
    kSlotPan,
    kSlotTune,
    kSlotStart,
    kSlotEnd,
    kSlotAttack,
    kSlotRelease,
    kSlotFilterCutoff,
    kSlotFilterResonance,
    kSlotMangle,
    kSlotPressureDepth,
    kSlotCharacter,
    kSlotOutputBus,
    kSlotMute,
    kSlotSolo,
    kSlotTriggerMode,
    kSlotSourceFormat,
    kSlotZeroCross,
    kSlotDirection,
    kSlotParameterCount,
};
constexpr std::size_t kStoredParamCount = kGlobalParamCount
    + s3g::sample::kSampleNeonSlotCount * kSlotParameterCount;

constexpr clap_id slotParamId(std::size_t slot,
    SlotParamOffset offset) noexcept
{
    return kSlotParamBase + static_cast<clap_id>(slot) * kSlotParamStride
        + static_cast<clap_id>(offset);
}

struct ParamDef {
    const char* name;
    const char* module;
    double minimum;
    double maximum;
    double defaultValue;
    bool stepped;
};

constexpr std::array<ParamDef, kGlobalParamCount> kGlobalParamDefs {{
    { "Output Layout", "Output", 0.0, 6.0, 0.0, true },
    { "Master Gain", "Output", -60.0, 12.0, -6.0, false },
    { "Mangle", "Performance", 0.0, 1.0, 0.0, false },
    { "Base Note", "MIDI", 0.0, 96.0, 36.0, true },
    { "Neon Active Owner", "Reloop Neon", 0.0, 1.0, 0.0, true },
    { "Tempo Sync", "Performance", 0.0, 1.0, 0.0, true },
    { "Standard MIDI Receive", "MIDI", 0.0, 16.0, 0.0, true },
}};

constexpr std::array<ParamDef, kSlotParameterCount> kSlotParamDefs {{
    { "Gain", "Mixer", -60.0, 12.0, -6.0, false },
    { "Pan", "Mixer", -1.0, 1.0, 0.0, false },
    { "Tune", "Pitch", -24.0, 24.0, 0.0, false },
    { "Start", "Sample", 0.0, 1.0, 0.0, false },
    { "End", "Sample", 0.0, 1.0, 1.0, false },
    { "Attack", "Envelope", 0.0, 1.0, 0.0, false },
    { "Release", "Envelope", 0.0, 1.0, 0.005, false },
    { "Filter Cutoff", "Filter", 20.0, 20000.0, 20000.0, false },
    { "Filter Resonance", "Filter", 0.0, 1.0, 0.0, false },
    { "Mangle", "Performance", 0.0, 1.0, 0.0, false },
    { "Pressure Depth", "Performance", 0.0, 1.0, 0.75, false },
    { "Character", "Performance", 0.0, 7.0, 0.0, true },
    { "Output Bus", "Mixer", 1.0, 16.0, 1.0, true },
    { "Legacy Mute (unused)", "Legacy", 0.0, 1.0, 0.0, true },
    { "Legacy Solo (unused)", "Legacy", 0.0, 1.0, 0.0, true },
    { "Trigger Mode", "Voice", 0.0, 3.0, 0.0, true },
    { "Source Format", "Sample", 0.0, 1.0, 0.0, true },
    { "Zero Cross", "Sample", 0.0, 1.0, 1.0, true },
    { "Direction", "Voice", 0.0, 3.0, 0.0, true },
}};

constexpr std::array<const char*, 7u> kOutputLayoutNames {{
    "STEREO", "STEREO STEMS", "QUAD", "OCTO",
    "1OA ACN/SN3D", "2OA ACN/SN3D", "3OA ACN/SN3D",
}};

#if defined(__APPLE__)
class DirectNeonOutput {
public:
    enum class Status : uint8_t {
        Off = 0u,
        Searching,
        Connected,
        Error,
    };

    DirectNeonOutput() = default;
    DirectNeonOutput(const DirectNeonOutput&) = delete;
    DirectNeonOutput& operator=(const DirectNeonOutput&) = delete;

    ~DirectNeonOutput() { stop(); }

    void start()
    {
        if (worker_.joinable()) return;
        const char* disabled = std::getenv(
            "S3G_SAMPLE_NEON_DISABLE_DIRECT_MIDI");
        if (disabled && std::strcmp(disabled, "0") != 0) return;
        stopping_.store(false, std::memory_order_release);
        worker_ = std::thread([this] { run(); });
    }

    void stop()
    {
        stopping_.store(true, std::memory_order_release);
        if (worker_.joinable()) worker_.join();
    }

    void publish(bool active,
        const s3g::controller::reloop_neon::LedFrame& frame, bool refresh = false) noexcept
    {
        sequence_.fetch_add(1u, std::memory_order_acq_rel);
        std::size_t index = 0u;
        for (const auto& pad : frame.pads) {
            desired_[index++].store(pad.surface, std::memory_order_relaxed);
            for (const uint8_t value : pad.segments)
                desired_[index++].store(value, std::memory_order_relaxed);
        }
        bank_.store(frame.bank, std::memory_order_relaxed);
        mode_.store(static_cast<uint8_t>(frame.mode),
            std::memory_order_relaxed);
        layer_.store(static_cast<uint8_t>(frame.layer),
            std::memory_order_relaxed);
        active_.store(active, std::memory_order_relaxed);
        if (refresh) refreshRequests_.fetch_add(1u, std::memory_order_relaxed);
        sequence_.fetch_add(1u, std::memory_order_release);
    }

    void setInactive() noexcept
    {
        sequence_.fetch_add(1u, std::memory_order_acq_rel);
        active_.store(false, std::memory_order_relaxed);
        sequence_.fetch_add(1u, std::memory_order_release);
    }

    Status status() const noexcept
    {
        return static_cast<Status>(status_.load(std::memory_order_acquire));
    }

private:
    static std::string endpointName(MIDIEndpointRef endpoint)
    {
        CFStringRef value = nullptr;
        if (MIDIObjectGetStringProperty(endpoint, kMIDIPropertyDisplayName,
                &value) != noErr || !value)
            (void)MIDIObjectGetStringProperty(endpoint, kMIDIPropertyName,
                &value);
        if (!value) return {};
        std::array<char, 512u> buffer {};
        const bool converted = CFStringGetCString(value, buffer.data(),
            static_cast<CFIndex>(buffer.size()), kCFStringEncodingUTF8);
        CFRelease(value);
        return converted ? std::string(buffer.data()) : std::string();
    }

    static bool isNeonEndpoint(std::string name)
    {
        std::transform(name.begin(), name.end(), name.begin(),
            [](unsigned char character) {
                return static_cast<char>(std::toupper(character));
            });
        return name.find("NEON") != std::string::npos;
    }

    static MIDIEndpointRef findDestination()
    {
        for (ItemCount index = 0u; index < MIDIGetNumberOfDestinations();
             ++index) {
            const MIDIEndpointRef endpoint = MIDIGetDestination(index);
            if (endpoint && isNeonEndpoint(endpointName(endpoint)))
                return endpoint;
        }
        return 0u;
    }

    static bool send(MIDIPortRef port, MIDIEndpointRef destination,
        const uint8_t* bytes, std::size_t size)
    {
        if (!port || !destination || !bytes || size == 0u || size > 256u)
            return false;
        std::array<uint8_t, 512u> storage {};
        auto* list = reinterpret_cast<MIDIPacketList*>(storage.data());
        MIDIPacket* packet = MIDIPacketListInit(list);
        packet = MIDIPacketListAdd(list, storage.size(), packet, 0u,
            static_cast<UInt16>(size), bytes);
        return packet && MIDISend(port, destination, list) == noErr;
    }

    bool snapshot(bool& active,
        s3g::controller::reloop_neon::LedFrame& frame,
        uint64_t& version, uint64_t& refreshRequest) const noexcept
    {
        for (int attempt = 0; attempt < 4; ++attempt) {
            const uint64_t before = sequence_.load(std::memory_order_acquire);
            if ((before & 1u) != 0u) continue;
            active = active_.load(std::memory_order_relaxed);
            refreshRequest = refreshRequests_.load(std::memory_order_relaxed);
            frame.bank = bank_.load(std::memory_order_relaxed);
            frame.mode = static_cast<s3g::controller::reloop_neon::Mode>(
                mode_.load(std::memory_order_relaxed));
            frame.layer = static_cast<s3g::controller::reloop_neon::Layer>(
                layer_.load(std::memory_order_relaxed));
            std::size_t index = 0u;
            for (auto& pad : frame.pads) {
                pad.surface = desired_[index++].load(
                    std::memory_order_relaxed);
                for (auto& value : pad.segments)
                    value = desired_[index++].load(
                        std::memory_order_relaxed);
            }
            const uint64_t after = sequence_.load(std::memory_order_acquire);
            if (before == after && (after & 1u) == 0u) {
                version = after;
                return true;
            }
        }
        return false;
    }

    static bool sendFrame(MIDIPortRef port, MIDIEndpointRef destination,
        const s3g::controller::reloop_neon::LedFrame& frame,
        s3g::controller::reloop_neon::LedDiffEncoder& encoder, bool force, bool refreshPads = false)
    {
        std::array<s3g::controller::reloop_neon::MidiMessage,
            s3g::controller::reloop_neon::kMaximumLedMessages> messages {};
        const std::size_t count = encoder.encode(frame, messages.data(),
            messages.size(), force, refreshPads);
        std::array<uint8_t, 3u
            * s3g::controller::reloop_neon::kMaximumLedMessages> bytes {};
        std::size_t byteCount = 0u;
        for (std::size_t index = 0u; index < count; ++index) {
            bytes[byteCount++] = messages[index].status;
            bytes[byteCount++] = messages[index].data1;
            bytes[byteCount++] = messages[index].data2;
        }
        return byteCount == 0u
            || send(port, destination, bytes.data(), byteCount);
    }

    void run()
    {
        MIDIClientRef client = 0u;
        MIDIPortRef port = 0u;
        if (MIDIClientCreate(CFSTR("s3g Sample Neon 32"), nullptr, nullptr,
                &client) != noErr
            || MIDIOutputPortCreate(client, CFSTR("Neon LED Out"),
                &port) != noErr) {
            status_.store(static_cast<uint8_t>(Status::Error),
                std::memory_order_release);
            if (client) MIDIClientDispose(client);
            return;
        }

        MIDIEndpointRef destination = 0u;
        s3g::controller::reloop_neon::LedFrame frame;
        s3g::controller::reloop_neon::LedDiffEncoder encoder;
        s3g::controller::reloop_neon::LedRefreshRetries retries;
        uint64_t appliedVersion = std::numeric_limits<uint64_t>::max();
        uint64_t appliedRefresh = 0u;
        bool sentActive = false;
        auto nextSearch = std::chrono::steady_clock::now();

        while (!stopping_.load(std::memory_order_acquire)) {
            bool active = false;
            uint64_t version = 0u, refreshRequest = 0u;
            if (!snapshot(active, frame, version, refreshRequest)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                continue;
            }
            if (!active) {
                if (sentActive && destination) {
                    auto dark = frame;
                    for (auto& pad : dark.pads) pad = {};
                    (void)sendFrame(port, destination, dark, encoder, true);
                }
                sentActive = false;
                appliedVersion = version;
                status_.store(static_cast<uint8_t>(Status::Off),
                    std::memory_order_release);
                std::this_thread::sleep_for(std::chrono::milliseconds(25));
                continue;
            }

            const auto now = std::chrono::steady_clock::now();
            if (now >= nextSearch) {
                // Discover unplug/reconnect without sending heartbeat MIDI.
                const auto discovered = findDestination();
                nextSearch = now + std::chrono::milliseconds(500);
                if (discovered != destination) {
                    destination = discovered;
                    appliedVersion = std::numeric_limits<uint64_t>::max();
                    sentActive = false;
                }
                if (!destination) status_.store(static_cast<uint8_t>(Status::Searching), std::memory_order_release);
            }
            if (!destination) {
                std::this_thread::sleep_for(std::chrono::milliseconds(25));
                continue;
            }

            const auto nowMs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count());
            const bool refreshPads = retries.update(frame, nowMs, !sentActive || refreshRequest != appliedRefresh);
            appliedRefresh = refreshRequest;
            if (!sentActive) {
                static constexpr auto sysex =
                    s3g::controller::reloop_neon::enableFourDecksSysEx();
                if (!send(port, destination, sysex.data(), sysex.size())
                    || !sendFrame(port, destination, frame, encoder, true)) {
                    destination = 0u;
                    status_.store(static_cast<uint8_t>(Status::Error),
                        std::memory_order_release);
                    continue;
                }
                sentActive = true;
                appliedVersion = version;
                status_.store(static_cast<uint8_t>(Status::Connected),
                    std::memory_order_release);
            } else if (version != appliedVersion || refreshPads) {
                // The hardware can reset lamps after a bank/mode transition.
                // Reassert pads only: repeatedly re-sending bank selection
                // would itself reset the surface and fight physical controls.
                if (!sendFrame(port, destination, frame, encoder, false, refreshPads)) {
                    destination = 0u;
                    sentActive = false;
                    status_.store(static_cast<uint8_t>(Status::Error),
                        std::memory_order_release);
                    continue;
                }
                appliedVersion = version;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(12));
        }

        if (sentActive && destination) {
            for (auto& pad : frame.pads) pad = {};
            (void)sendFrame(port, destination, frame, encoder, true);
        }
        MIDIPortDispose(port);
        MIDIClientDispose(client);
        status_.store(static_cast<uint8_t>(Status::Off),
            std::memory_order_release);
    }

    std::array<std::atomic<uint8_t>,
        s3g::controller::reloop_neon::kPadsPerBank
            * s3g::controller::reloop_neon::kLedValuesPerPad> desired_ {};
    std::atomic<uint64_t> sequence_ { 0u };
    std::atomic<uint64_t> refreshRequests_ { 0u };
    std::atomic<bool> active_ { false };
    std::atomic<uint8_t> bank_ { 0u };
    std::atomic<uint8_t> mode_ { 0u };
    std::atomic<uint8_t> layer_ { 0u };
    std::atomic<bool> stopping_ { false };
    std::atomic<uint8_t> status_ { static_cast<uint8_t>(Status::Off) };
    std::thread worker_;
};
#endif

struct StateHeader {
    uint32_t magic = kStateMagic;
    uint32_t version = kStateVersion;
    uint32_t parameterCount = static_cast<uint32_t>(kStoredParamCount);
    uint32_t pathBytes = static_cast<uint32_t>(kMaximumPathBytes);
};

#if defined(S3G_SAMPLE_FILE_WORKER)
struct LoadRequest {
    uint64_t generation = 0u;
    uint8_t slot = 0u;
    bool dirty = true;
    std::string path;
};

struct LoadResult {
    uint64_t generation = 0u;
    uint8_t slot = 0u;
    bool dirty = true;
    std::string path;
    std::shared_ptr<const SampleAsset> asset;
    s3g::sample::CutupsLaneMetadata analysis {};
    std::string error;
};
#endif

struct CellClipboard;
struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    const clap_host_params_t* hostParams = nullptr;
    const clap_host_state_t* hostState = nullptr;
    std::atomic<bool> stateDirtyPending { false };
    uint32_t outputChannels = s3g::sample::kSampleNeonOutputChannels;
    double sampleRate = 48000.0;
    uint32_t maximumFrames = 0u;
    SampleNeonEngine engine;
    std::array<std::atomic<double>, kStoredParamCount> parameters {};
    s3g::clap_gui::ParamEventQueue<2048u> guiParamEvents {};
    std::atomic_flag guiParamConsumer = ATOMIC_FLAG_INIT;
    std::array<SampleNeonEvent,
        s3g::sample::kSampleNeonMaximumBlockEvents> blockEvents {};
    std::array<std::vector<float>,
        s3g::sample::kSampleNeonOutputChannels> scratch {};
    std::array<const SampleAsset*,
        s3g::sample::kSampleNeonSlotCount> audioAssets {};
    std::array<std::atomic<const SampleAsset*>,
        s3g::sample::kSampleNeonSlotCount> publishedAssets {};
    std::array<std::shared_ptr<const SampleAsset>,
        s3g::sample::kSampleNeonSlotCount> controlAssets {};
    std::vector<std::shared_ptr<const SampleAsset>> retainedAssets;
    std::array<std::shared_ptr<const s3g::sample::WavesetMap>, 32u> controlWavesets {};
    std::array<std::atomic<const s3g::sample::WavesetMap*>, 32u> publishedWavesets {};
    std::array<const SampleAsset*, 32u> waveAnalysisAttempted {};
    std::vector<std::shared_ptr<const s3g::sample::WavesetMap>> retainedWavesets;
    std::array<std::string, s3g::sample::kSampleNeonSlotCount> samplePaths {};
    std::array<std::string, s3g::sample::kSampleNeonSlotCount> statuses {};
    mutable std::mutex statusMutex;
    std::atomic<uint32_t> pendingAuditions { 0u };
    std::atomic<uint32_t> pendingEditAuditions { 0u };
    std::atomic<uint32_t> pendingCellStops { 0u };
    std::atomic<uint32_t> pendingGuiReleases { 0u };
    std::atomic<uint32_t> pendingSliceAudition { 0xffffffffu };
    std::atomic<uint32_t> pendingPerformanceAudition { 0xffffffffu };
    std::atomic<bool> killRequested { false };
    std::atomic<uint32_t> pendingGuiFx { UINT32_MAX };
    enum class CaptureState : uint8_t { Empty, Recording, Ready, Review };
    s3g::sample::SampleNeonRecorder recorder;
    s3g::sample::SamplePlayerEngine capturePreview;
    std::array<std::vector<float>, 16u> previewScratch;
    std::shared_ptr<const SampleAsset> captureAsset;
    std::atomic<const SampleAsset*> publishedCapture { nullptr };
    const SampleAsset* audioCapture = nullptr;
    std::atomic<CaptureState> captureState { CaptureState::Empty };
    std::atomic<int> captureCommand { 0 }; // 1 record, 2 stop, 3 toggle; audio-thread consumed
    std::atomic<bool> captureAudition { false };
    std::atomic<uint32_t> captureFrames { 0u };
    std::atomic<float> capturePlayhead { -1.0f };
    std::atomic<uint8_t> captureBus { 0u }, captureTarget { 0xffu };
    SampleNeonOutputLayout recordingLayout = SampleNeonOutputLayout::Stereo;
    uint8_t recordingBus = 0u;
    std::atomic<uint8_t> captureLayout { 0u };
    std::atomic<double> captureStart { 0.0 }, captureEnd { 1.0 }, captureCursor { 0.0 };
    std::atomic<float> captureZoom { 1.0f };
    std::atomic<bool> captureZeroCross { true };
    std::array<bool, s3g::sample::kSampleNeonSlotCount> embeddedAssets {};
    // Audio thread asks the main-thread service to perform allocating edits.
    std::atomic<uint32_t> pendingEditCommand { 0u };
    std::atomic<bool> replaceChopSource { false };
    // 1: stop requested, 3: stopping, 2: main thread may clear assets/settings.
    std::atomic<uint8_t> resetAllPhase { 0u };
    std::atomic<bool> processing { false };
    std::atomic<uint8_t> editPage { 0u }, cellBank { 0u }, chopBank { 0u };
    std::shared_ptr<CellClipboard> cellClipboard;
    std::string workflowMessage;
    struct HeldPad {
        bool active = false;
        bool pressureOnly = false;
        SampleNeonEvent event;
    };
    std::array<HeldPad, 8u> heldGestures {};
    std::array<std::atomic<float>,
        s3g::sample::kSampleNeonSlotCount> slotPeaks {};
    std::array<std::atomic<bool>, 32u> slotPlaying {};
    std::array<std::atomic<uint8_t>, 4u> lastPlayedCells {};
    uint64_t ledRefreshFrames = 0u; // audio thread only
    std::array<std::array<std::atomic<uint8_t>,
            s3g::sample::kSampleNeonSliceCount>,
        s3g::sample::kSampleNeonSlotCount> sliceTriggers {};
    std::array<std::array<std::atomic<uint8_t>,
            s3g::sample::kSampleNeonSliceCount>,
        s3g::sample::kSampleNeonSlotCount> sliceOptions {};
    std::array<std::atomic<uint8_t>,
        s3g::sample::kSampleNeonSlotCount> sliceCounts {};
    std::array<std::array<std::atomic<double>,
            s3g::sample::kSampleNeonSliceCount + 1u>,
        s3g::sample::kSampleNeonSlotCount> sliceBoundaries {};
    std::array<std::atomic<uint8_t>,
        s3g::sample::kSampleNeonSlotCount> chopModes {};
    std::array<std::atomic<uint8_t>,
        s3g::sample::kSampleNeonSlotCount> chopBeatDivisions {};
    std::array<std::atomic<double>,
        s3g::sample::kSampleNeonSlotCount> sourceBpms {};
    std::array<std::atomic<float>,
        s3g::sample::kSampleNeonSlotCount> transientPreRollMs {};
    std::array<std::atomic<uint8_t>,
        s3g::sample::kSampleNeonSlotCount> transientSliceLimits {};
    std::array<std::atomic<uint8_t>,
        s3g::sample::kSampleNeonSlotCount> slicerDomainIndices {};
    std::array<std::atomic<uint8_t>,
        s3g::sample::kSampleNeonSlotCount> slicerQuantizeIndices {};
    std::array<s3g::sample::CutupsLaneMetadata,
        s3g::sample::kSampleNeonSlotCount> slotAnalyses {};
    std::array<std::atomic<uint8_t>,
        s3g::sample::kSampleNeonSlotCount> slotOptions {};
    std::array<std::atomic<uint8_t>,
        s3g::sample::kSampleNeonSlotCount> chokeGroups {};
    std::array<std::array<std::atomic<double>,
            s3g::sample::kSampleNeonCueCount>,
        s3g::sample::kSampleNeonSlotCount> hotCuePositions {};
    std::array<std::array<std::atomic<uint8_t>,
            s3g::sample::kSampleNeonCueCount>,
        s3g::sample::kSampleNeonSlotCount> hotCueEnabled {};
    std::array<std::array<std::atomic<double>,
            s3g::sample::kSampleNeonSavedLoopCount>,
        s3g::sample::kSampleNeonSlotCount> savedLoopStarts {};
    std::array<std::array<std::atomic<double>,
            s3g::sample::kSampleNeonSavedLoopCount>,
        s3g::sample::kSampleNeonSlotCount> savedLoopEnds {};
    std::array<std::array<std::atomic<uint8_t>,
            s3g::sample::kSampleNeonSavedLoopCount>,
        s3g::sample::kSampleNeonSlotCount> savedLoopEnabled {};
    std::array<std::atomic<double>,
        s3g::sample::kSampleNeonSlotCount> editPositions {};
    // Editor view state: intentionally not serialized with the sound.
    std::atomic<s3g::sample::SampleNeonWaveformDisplay> waveformDisplay {
        s3g::sample::SampleNeonWaveformDisplay::CombinedPeaks };
    std::array<std::atomic<float>,
        s3g::sample::kSampleNeonSlotCount> waveformZooms {};
    std::array<std::atomic<uint8_t>,
        s3g::sample::kSampleNeonSlotCount> textureOptions {};
    std::array<std::atomic<uint8_t>,
        s3g::sample::kSampleNeonSlotCount> motionPaths {};
    std::array<std::atomic<uint8_t>, 32u> playbackClocks {};
    std::array<std::atomic<float>, 32u> motionSeconds {};
    std::array<std::atomic<float>, 32u> shotSeconds {};
    std::array<std::atomic<float>, 32u> techniqueAttackSeconds {}, techniqueReleaseSeconds {};
    std::array<std::atomic<float>, 32u> grainIntervals {};
    std::array<std::array<std::array<std::atomic<float>, 3u>, 8u>, 32u> fxParameters {};
    std::array<std::array<std::array<std::atomic<float>, 4u>, 3u>, 32u> techniqueParameters {};
    std::atomic<uint8_t> fxPair {0u};
    std::array<std::atomic<float>,
        s3g::sample::kSampleNeonSlotCount> motionRates {};
    std::array<std::atomic<float>,
        s3g::sample::kSampleNeonSlotCount> motionLoci {};
    std::array<std::atomic<float>,
        s3g::sample::kSampleNeonSlotCount> motionFields {};
    std::array<std::atomic<float>,
        s3g::sample::kSampleNeonSlotCount> lanePositions {};
    std::array<std::atomic<float>,
        s3g::sample::kSampleNeonSlotCount> laneMotionDepths {};
    std::array<std::atomic<float>,
        s3g::sample::kSampleNeonSlotCount> grainDensities {};
    std::array<std::atomic<float>,
        s3g::sample::kSampleNeonSlotCount> grainSizes {};
    std::array<std::atomic<float>,
        s3g::sample::kSampleNeonSlotCount> grainPositions {};
    std::array<std::atomic<float>,
        s3g::sample::kSampleNeonSlotCount> grainSprays {};
    std::array<std::atomic<float>,
        s3g::sample::kSampleNeonSlotCount> grainPitchSprays {};
    std::array<std::atomic<float>,
        s3g::sample::kSampleNeonSlotCount> grainReverseChances {};
    std::array<std::atomic<float>,
        s3g::sample::kSampleNeonSlotCount> motionPositions {};
    std::array<std::atomic<uint8_t>,
        s3g::sample::kSampleNeonSlotCount> voiceCursorCounts {};
    std::array<std::array<std::atomic<float>, s3g::sample::kMaximumVoices>,
        s3g::sample::kSampleNeonSlotCount> voiceCursorPositions {};
    std::array<std::array<std::atomic<float>, s3g::sample::kMaximumVoices>,
        s3g::sample::kSampleNeonSlotCount> voiceCursorStarts {};
    std::array<std::array<std::atomic<float>, s3g::sample::kMaximumVoices>,
        s3g::sample::kSampleNeonSlotCount> voiceCursorEnds {};
    std::array<std::array<std::atomic<uint8_t>, kFlipMaximumSteps>,
        kFlipPatternCount> flipSteps {};
    std::array<std::array<std::atomic<uint8_t>, kFlipMaximumSteps>,
        kFlipPatternCount> flipVelocities {};
    std::array<std::atomic<uint8_t>, kFlipPatternCount> flipStepCounts {};
    std::atomic<uint8_t> selectedFlipPattern { 0u };
    std::atomic<bool> flipRecording { false };
    std::atomic<bool> flipPlaying { false };
    std::atomic<bool> flipLooping { true };
    uint8_t flipPlaybackStep = 0u;
    double flipFramesUntilStep = 0.0;
    std::atomic<float> outputPeak { 0.0f };
    std::atomic<uint32_t> activeVoices { 0u };
    std::atomic<uint8_t> visibleBank { 0u };
    std::atomic<uint8_t> visibleMode { 0u };
    std::atomic<uint8_t> visibleLayer { 0u };
    std::atomic<uint8_t> visibleSlice { 0u };
    std::atomic<uint8_t> selectedSlot { 0u };
    std::atomic<float> hostTempo { 120.0f };
    std::atomic<double> hostBeatPosition { 0.0 };
    std::atomic<bool> hostBeatValid { false };
    std::atomic<bool> transportPlaying { false };
    NeonPerformanceState neonState {};
    s3g::controller::reloop_neon::PadInputDecoder neonPadInput {};
    std::atomic<bool> resetNeonVelocity {false};
    std::array<uint8_t, s3g::controller::reloop_neon::kPadsPerBank>
        heldSlots {{ 0xffu, 0xffu, 0xffu, 0xffu,
            0xffu, 0xffu, 0xffu, 0xffu }};
    s3g::controller::reloop_neon::LedDiffEncoder ledEncoder;
    std::atomic<bool> sendNeonInitialization { true };
    std::atomic<bool> ledFeedbackDirty { true };
    bool neonWasActive = false;
#if defined(__APPLE__)
    DirectNeonOutput directNeonOutput;
#endif
    bool active = false;
#if defined(S3G_SAMPLE_FILE_WORKER)
    std::mutex loaderMutex;
    std::condition_variable loaderCondition;
    std::deque<LoadRequest> loadRequests;
    std::deque<LoadResult> loadResults;
    std::array<uint64_t, s3g::sample::kSampleNeonSlotCount>
        loadGenerations {};
    std::thread loaderThread;
    bool loaderStopping = false;
#endif
#if defined(S3G_ENABLE_VSTGUI_SAMPLE_NEON_GUI)
    s3g::portable_gui::foundation::EditorHost* portableGuiEditor = nullptr;
    uint32_t portableGuiWidth = kGuiWidth;
    uint32_t portableGuiHeight = kGuiHeight;
    bool portableGuiVisible = false;
#endif
};

Plugin* self(const clap_plugin_t* plugin)
{
    return plugin ? static_cast<Plugin*>(plugin->plugin_data) : nullptr;
}

void regenerateChopLayout(Plugin& instance, std::size_t slot,
    bool dirty) noexcept;
void snapSlotBoundaries(Plugin& instance, std::size_t slot, unsigned trimMask = 3u) noexcept;
void serviceWorkflow(Plugin& instance);

bool parameterLocation(clap_id id, std::size_t& index,
    const ParamDef*& definition, std::size_t* slotOut = nullptr,
    SlotParamOffset* offsetOut = nullptr) noexcept
{
    if (id >= kOutputLayoutParamId && id <= kMidiReceiveParamId) {
        index = static_cast<std::size_t>(id - kOutputLayoutParamId);
        definition = &kGlobalParamDefs[index];
        return true;
    }
    if (id < kSlotParamBase) return false;
    const clap_id relative = id - kSlotParamBase;
    const std::size_t slot = relative / kSlotParamStride;
    const clap_id offset = relative % kSlotParamStride;
    if (slot >= s3g::sample::kSampleNeonSlotCount
        || offset >= kSlotParameterCount) return false;
    index = kGlobalParamCount + slot * kSlotParameterCount + offset;
    definition = &kSlotParamDefs[offset];
    if (slotOut) *slotOut = slot;
    if (offsetOut) *offsetOut = static_cast<SlotParamOffset>(offset);
    return true;
}

clap_id parameterIdAt(std::size_t index) noexcept
{
    if (index < kGlobalParamCount)
        return kOutputLayoutParamId + static_cast<clap_id>(index);
    index -= kGlobalParamCount;
    return slotParamId(index / kSlotParameterCount,
        static_cast<SlotParamOffset>(index % kSlotParameterCount));
}

double clampParam(const ParamDef& definition, double value) noexcept
{
    value = std::isfinite(value) ? value : definition.defaultValue;
    value = std::clamp(value, definition.minimum, definition.maximum);
    return definition.stepped ? std::round(value) : value;
}

double paramValue(const Plugin& instance, clap_id id) noexcept
{
    std::size_t index = 0u;
    const ParamDef* definition = nullptr;
    if (!parameterLocation(id, index, definition)) return 0.0;
    return instance.parameters[index].load(std::memory_order_acquire);
}

void markStateDirty(Plugin& instance) noexcept
{
    // Hardware and bridged controls run on the audio thread. CLAP's state
    // notification is main-thread-only, so coalesce it through the callback.
    if (!instance.stateDirtyPending.exchange(true, std::memory_order_acq_rel)
        && instance.host && instance.host->request_callback)
        instance.host->request_callback(instance.host);
}

void setParam(Plugin& instance, clap_id id, double value,
    bool dirty = false) noexcept
{
    if (instance.resetAllPhase.load()) return;
    std::size_t index = 0u;
    const ParamDef* definition = nullptr;
    if (!parameterLocation(id, index, definition)) return;
    value = clampParam(*definition, value);
    if (id >= kSlotParamBase) {
        const auto slot = (id - kSlotParamBase) / kSlotParamStride;
        const auto offset = (id - kSlotParamBase) % kSlotParamStride;
        if (offset == kSlotMute || offset == kSlotSolo) value = 0.0;
        if (offset == kSlotCharacter && paramValue(instance, slotParamId(slot, kSlotSourceFormat)) >= 0.5
            && !s3g::sample::sampleNeonFxAmbisonicSafe(static_cast<SampleNeonMangleCharacter>(static_cast<unsigned>(value)))) return;
        if (offset == kSlotSourceFormat && value >= 0.5) {
            const auto characterIndex = kGlobalParamCount + slot * kSlotParameterCount + kSlotCharacter;
            if (instance.parameters[characterIndex].load() >= 6.0) {
                instance.parameters[characterIndex].store(0.0);
                instance.parameters[kGlobalParamCount + slot * kSlotParameterCount + kSlotMangle].store(0.0);
            }
            if (normalizedStageOptions(instance.textureOptions[slot].load()) == kWavesetsOption)
                instance.textureOptions[slot].store(0u);
        }
        const auto* asset = instance.publishedAssets[slot].load(std::memory_order_acquire);
        const double gap = asset && asset->frameCount() ? 1.0 / asset->frameCount() : 1.0e-6;
        if (offset == kSlotStart)
            value = std::min(value, std::max(0.0, paramValue(instance, slotParamId(slot, kSlotEnd)) - gap));
        if (offset == kSlotEnd)
            value = std::max(value, std::min(1.0, paramValue(instance, slotParamId(slot, kSlotStart)) + gap));
    }
    if (instance.parameters[index].load(std::memory_order_acquire) == value) return;
    instance.parameters[index].store(value,
        std::memory_order_release);
    if (id >= kSlotParamBase) {
        const auto offset = (id - kSlotParamBase) % kSlotParamStride;
        if (offset == kSlotStart || offset == kSlotEnd || offset == kSlotZeroCross)
            snapSlotBoundaries(instance, (id - kSlotParamBase) / kSlotParamStride,
                offset == kSlotStart ? 1u : offset == kSlotEnd ? 2u : 3u);
    }
    if (dirty) markStateDirty(instance);
}

void requestProcess(Plugin& instance) noexcept
{
    if (instance.host && instance.host->request_process)
        instance.host->request_process(instance.host);
}

void requestGuiParamService(Plugin& instance) noexcept
{
    requestProcess(instance);
    if (instance.hostParams && instance.hostParams->request_flush)
        instance.hostParams->request_flush(instance.host);
}

void queueGuiParamBegin(Plugin& instance, clap_id id)
{
    if (instance.guiParamEvents.push({
            s3g::clap_gui::ParamEventKind::GestureBegin, id, 0.0 }))
        requestGuiParamService(instance);
}

void queueGuiParamValue(Plugin& instance, clap_id id, double value)
{
    std::size_t index = 0u;
    const ParamDef* definition = nullptr;
    if (!parameterLocation(id, index, definition)) return;
    value = clampParam(*definition, value);
    setParam(instance, id, value, true);
    if (!instance.guiParamEvents.push({
            s3g::clap_gui::ParamEventKind::Value, id, paramValue(instance, id) })) return;
    requestGuiParamService(instance);
}

void queueGuiParamEnd(Plugin& instance, clap_id id)
{
    if (instance.guiParamEvents.push({
            s3g::clap_gui::ParamEventKind::GestureEnd, id, 0.0 }))
        requestGuiParamService(instance);
}

void queueGuiParamGesture(Plugin& instance, clap_id id, double value)
{
    std::size_t index = 0u;
    const ParamDef* definition = nullptr;
    if (!parameterLocation(id, index, definition)) return;
    using Kind = s3g::clap_gui::ParamEventKind;
    setParam(instance, id, clampParam(*definition, value), true);
    const std::array<s3g::clap_gui::ParamEvent, 3u> events {{
        { Kind::GestureBegin, id, 0.0 },
        { Kind::Value, id, paramValue(instance, id) },
        { Kind::GestureEnd, id, 0.0 },
    }};
    if (!instance.guiParamEvents.pushBatch(events.data(),
            static_cast<uint32_t>(events.size()))) return;
    requestGuiParamService(instance);
}

void serviceGuiParamEvents(Plugin& instance,
    const clap_output_events_t* output) noexcept
{
    if (instance.guiParamConsumer.test_and_set(std::memory_order_acquire))
        return;
    s3g::clap_gui::serviceParamEvents(instance.guiParamEvents, output,
        [&instance](clap_id id, double value) {
            setParam(instance, id, value);
        });
    instance.guiParamConsumer.clear(std::memory_order_release);
}

SampleNeonSettings settingsSnapshot(const Plugin& instance) noexcept
{
    SampleNeonSettings settings;
    settings.outputLayout = static_cast<SampleNeonOutputLayout>(
        static_cast<uint8_t>(paramValue(instance, kOutputLayoutParamId)));
    settings.masterGainDecibels = static_cast<float>(
        paramValue(instance, kMasterGainParamId));
    settings.globalMangle = static_cast<float>(
        paramValue(instance, kGlobalMangleParamId));
    settings.hostTempoBpm = instance.hostTempo.load(
        std::memory_order_relaxed);
    settings.hostBeatPosition = instance.hostBeatPosition.load(
        std::memory_order_relaxed);
    settings.transportPlaying = instance.transportPlaying.load(
        std::memory_order_relaxed);
    settings.hostBeatValid = instance.hostBeatValid.load(
        std::memory_order_relaxed);
    settings.tempoSync = paramValue(instance, kTempoSyncParamId) >= 0.5;
    for (std::size_t slot = 0u; slot < settings.slots.size(); ++slot) {
        auto& value = settings.slots[slot];
        value.gainDecibels = static_cast<float>(paramValue(instance,
            slotParamId(slot, kSlotGain)));
        value.pan = static_cast<float>(paramValue(instance,
            slotParamId(slot, kSlotPan)));
        value.tuneSemitones = static_cast<float>(paramValue(instance,
            slotParamId(slot, kSlotTune)));
        value.start = paramValue(instance, slotParamId(slot, kSlotStart));
        value.end = paramValue(instance, slotParamId(slot, kSlotEnd));
        value.attackProportion = static_cast<float>(paramValue(instance,
            slotParamId(slot, kSlotAttack)));
        value.releaseProportion = static_cast<float>(paramValue(instance,
            slotParamId(slot, kSlotRelease)));
        value.filterCutoffHz = static_cast<float>(paramValue(instance,
            slotParamId(slot, kSlotFilterCutoff)));
        value.filterResonance = static_cast<float>(paramValue(instance,
            slotParamId(slot, kSlotFilterResonance)));
        value.filterType = value.filterCutoffHz < 19999.0f
            || value.filterResonance > 0.001f
            ? FilterType::LowPass : FilterType::Off;
        value.mangle = static_cast<float>(paramValue(instance,
            slotParamId(slot, kSlotMangle)));
        value.pressureDepth = static_cast<float>(paramValue(instance,
            slotParamId(slot, kSlotPressureDepth)));
        value.character = static_cast<SampleNeonMangleCharacter>(
            static_cast<uint8_t>(std::lround(paramValue(instance,
                slotParamId(slot, kSlotCharacter)))));
        value.outputBus = static_cast<uint8_t>(std::lround(paramValue(
            instance, slotParamId(slot, kSlotOutputBus))) - 1.0);
        value.sourceFormat = static_cast<SampleNeonSourceFormat>(
            static_cast<uint8_t>(paramValue(instance,
                slotParamId(slot, kSlotSourceFormat))));
        const int trigger = std::clamp<int>(static_cast<int>(std::lround(
            paramValue(instance, slotParamId(slot, kSlotTriggerMode)))),
            0, 3);
        value.triggerMode = static_cast<TriggerMode>(trigger);
        value.retriggerMode = RetriggerMode::Restart;
        const uint8_t slotOptions = instance.slotOptions[slot].load(
            std::memory_order_relaxed);
        value.repeat = (slotOptions & kSlotRepeatOption) != 0u;
        value.direction = static_cast<uint8_t>(paramValue(instance, slotParamId(slot, kSlotDirection)));
        value.sync = (slotOptions & kSlotSyncOption) != 0u;
        value.velocityEnabled = (slotOptions & kSlotVelocityOption) != 0u;
        value.chokeGroup = std::min<uint8_t>(4u,
            instance.chokeGroups[slot].load(std::memory_order_relaxed));
        const uint8_t texture = instance.textureOptions[slot].load(
            std::memory_order_relaxed);
        unsigned method = 0u;
        for (unsigned n = 1u; n < kPlaybackOptions.size(); ++n) if (texture == kPlaybackOptions[n]) method = n;
        value.playback = static_cast<SampleNeonPlayback>(method);
        for (unsigned n = 0u; n < 3u; ++n)
            value.fx[n] = instance.fxParameters[slot][static_cast<unsigned>(value.character)][n].load();
        if (method >= 3u) for (unsigned n = 0u; n < 4u; ++n)
            value.technique[n] = instance.techniqueParameters[slot][method - 3u][n].load();
        value.clock = instance.playbackClocks[slot].load() ? SampleNeonClock::Host : SampleNeonClock::Free;
        value.motionCycleSeconds = instance.motionSeconds[slot].load();
        value.shotSeconds = instance.shotSeconds[slot].load();
        value.techniqueAttackSeconds = instance.techniqueAttackSeconds[slot].load();
        value.techniqueReleaseSeconds = instance.techniqueReleaseSeconds[slot].load();
        value.grainIntervalBeats = instance.grainIntervals[slot].load();
        value.motionPath = static_cast<SampleNeonMotionPath>(
            std::min<uint8_t>(3u, instance.motionPaths[slot].load(
                std::memory_order_relaxed)));
        value.motionCycleBeats = kMotionCycleBeats[
            nearestMotionCycleIndex(instance.motionRates[slot].load(
                std::memory_order_relaxed))];
        value.launchPosition = std::clamp(
            instance.editPositions[slot].load(std::memory_order_relaxed),
            0.0, 1.0);
        value.grainDensityHz = std::clamp(
            instance.grainDensities[slot].load(std::memory_order_relaxed),
            1.0f, 80.0f);
        value.grainSizeMs = std::clamp(instance.grainSizes[slot].load(
            std::memory_order_relaxed), 5.0f, 500.0f);
        value.grainPosition = std::clamp(
            instance.grainPositions[slot].load(std::memory_order_relaxed),
            0.0f, 1.0f);
        value.grainSpray = std::clamp(instance.grainSprays[slot].load(
            std::memory_order_relaxed), 0.0f, 1.0f);
        value.grainPitchSpraySemitones = std::clamp(
            instance.grainPitchSprays[slot].load(std::memory_order_relaxed),
            0.0f, 24.0f);
        value.grainReverseChance = std::clamp(
            instance.grainReverseChances[slot].load(
                std::memory_order_relaxed), 0.0f, 1.0f);
        const auto* asset = instance.publishedAssets[slot].load(
            std::memory_order_acquire);
        // Assets were validated on the loader thread, never rescan PCM here.
        value.sourceDurationSeconds = asset
            ? static_cast<double>(asset->frameCount()) / asset->sampleRate
            : 0.0;
        value.sourceTempoBpm = std::clamp(
            instance.sourceBpms[slot].load(std::memory_order_relaxed),
            20.0, 999.0);
        value.slicerDomainStart = std::clamp(
            instance.editPositions[slot].load(std::memory_order_relaxed),
            0.0, 1.0);
        value.slicerDomainBeats = kSlicerDomainBeats[std::min<uint8_t>(5u,
            instance.slicerDomainIndices[slot].load(
                std::memory_order_relaxed))];
        value.slicerQuantizeBeats = kSlicerQuantizeBeats[
            std::min<uint8_t>(3u,
                instance.slicerQuantizeIndices[slot].load(
                    std::memory_order_relaxed))];
        for (std::size_t slice = 0u; slice < value.sliceTriggers.size();
             ++slice) {
            value.sliceTriggers[slice] = static_cast<
                SampleNeonSliceTriggerMode>(std::min<uint8_t>(2u,
                    instance.sliceTriggers[slot][slice].load(
                        std::memory_order_relaxed)));
            const uint8_t options = instance.sliceOptions[slot][slice].load(
                std::memory_order_relaxed);
            value.sliceRepeat[slice] = (options & kSliceRepeatOption) != 0u;
            value.sliceSync[slice] = (options & kSliceSyncOption) != 0u;
        }
        value.sliceCount = std::clamp<uint8_t>(
            instance.sliceCounts[slot].load(std::memory_order_relaxed),
            1u, static_cast<uint8_t>(s3g::sample::kSampleNeonSliceCount));
        value.sliceLayout.sliceCount = value.sliceCount;
        for (std::size_t marker = 0u;
             marker <= value.sliceCount; ++marker)
            value.sliceLayout.boundaries[marker] =
                instance.sliceBoundaries[slot][marker].load(
                    std::memory_order_relaxed);
        if (!value.sliceLayout.valid())
            value.sliceLayout = s3g::sample::equalSampleNeonSliceLayout(
                value.sliceCount);
        for (std::size_t cue = 0u; cue < value.hotCues.size(); ++cue) {
            value.hotCues[cue] = std::clamp(
                instance.hotCuePositions[slot][cue].load(
                    std::memory_order_relaxed), 0.0, 1.0);
            value.hotCueEnabled[cue] = instance.hotCueEnabled[slot][cue].load(
                std::memory_order_relaxed) != 0u;
        }
        for (std::size_t loop = 0u; loop < value.loopStarts.size(); ++loop) {
            const double start = std::clamp(
                instance.savedLoopStarts[slot][loop].load(
                    std::memory_order_relaxed), 0.0, 1.0);
            value.loopStarts[loop] = start;
            value.loopEnds[loop] = std::clamp(
                instance.savedLoopEnds[slot][loop].load(
                    std::memory_order_relaxed), start, 1.0);
            value.loopEnabled[loop] =
                instance.savedLoopEnabled[slot][loop].load(
                    std::memory_order_relaxed) != 0u;
        }
    }
    return settings;
}

std::string sampleDisplayName(const std::string& path)
{
    if (path.empty()) return "NO SAMPLE";
    const auto name = std::filesystem::u8path(path).filename().u8string();
    return name.empty() ? path : name;
}

bool publishAsset(Plugin& instance, std::size_t slot,
    std::shared_ptr<const SampleAsset> asset, std::string path, bool dirty)
{
    if (slot >= instance.controlAssets.size()
        || (asset && (!asset->valid()
            || !s3g::sample::sampleNeonChannelCountSupported(asset->channelCount))))
        return false;
    if (instance.publishedAssets[slot].load() != asset.get()) {
        uint8_t previous = static_cast<uint8_t>(slot);
        instance.lastPlayedCells[slot / 8u].compare_exchange_strong(previous, 0xffu);
    }
    {
        std::lock_guard<std::mutex> lock(instance.statusMutex);
        if (asset) instance.retainedAssets.push_back(asset);
        instance.controlAssets[slot] = std::move(asset);
        instance.samplePaths[slot] = std::move(path);
        instance.statuses[slot] = instance.controlAssets[slot]
            ? (instance.samplePaths[slot].empty() ? std::string("CELL AUDIO")
                : sampleDisplayName(instance.samplePaths[slot])) + " / "
                + std::to_string(instance.controlAssets[slot]->frameCount())
                + " FRAMES"
            : "DROP OR LOAD A SAMPLE (1 / 2 / 4 / 8 / 9 / 16 CH)";
        instance.publishedAssets[slot].store(
            instance.controlAssets[slot].get(), std::memory_order_release);
    }
    instance.ledFeedbackDirty.store(true, std::memory_order_release);
    requestProcess(instance);
    if (dirty) markStateDirty(instance);
    return true;
}

#if defined(S3G_SAMPLE_FILE_WORKER)
bool decodeSampleFile(const std::string& path,
    std::shared_ptr<const SampleAsset>& assetOut, std::string& error)
{
#if defined(__APPLE__)
    @autoreleasepool {
        NSString* nsPath = [NSString stringWithUTF8String:path.c_str()];
        NSError* nsError = nil;
        AVAudioFile* file = nsPath ? [[AVAudioFile alloc]
            initForReading:[NSURL fileURLWithPath:nsPath] error:&nsError]
            : nil;
        if (!file) {
            error = "COULD NOT OPEN SAMPLE";
            return false;
        }
        AVAudioFormat* format = [file processingFormat];
        const AVAudioChannelCount channels = [format channelCount];
        const AVAudioFramePosition sourceFrames = [file length];
        if (!s3g::sample::sampleNeonChannelCountSupported(channels) || sourceFrames < 1
            || static_cast<uint64_t>(sourceFrames)
                > std::numeric_limits<uint32_t>::max()) {
            error = "USE 1, 2, 4, 8, 9 OR 16 CHANNELS; UNDER 2^32 FRAMES";
            return false;
        }
        const auto frames = static_cast<AVAudioFrameCount>(sourceFrames);
        AVAudioPCMBuffer* buffer = [[AVAudioPCMBuffer alloc]
            initWithPCMFormat:format frameCapacity:frames];
        if (!buffer || ![file readIntoBuffer:buffer error:&nsError]
            || [buffer frameLength] == 0u || ![buffer floatChannelData]) {
            error = "SAMPLE DECODE FAILED";
            return false;
        }
        auto asset = std::make_shared<SampleAsset>();
        asset->sampleRate = [format sampleRate];
        asset->channelCount = static_cast<uint8_t>(channels);
        const uint32_t decodedFrames = [buffer frameLength];
        for (AVAudioChannelCount channel = 0u; channel < channels; ++channel)
            asset->channels[channel].assign(
                [buffer floatChannelData][channel],
                [buffer floatChannelData][channel] + decodedFrames);
        if (!asset->valid()) {
            error = "DECODED SAMPLE IS INVALID";
            return false;
        }
        assetOut = std::move(asset);
        error.clear();
        return true;
    }
#else
    if (!s3g::sample_file::decodeWaveFile(path, assetOut, error))
        return false;
    if (assetOut && !s3g::sample::sampleNeonChannelCountSupported(assetOut->channelCount)) {
        assetOut.reset();
        error = "USE 1, 2, 4, 8, 9 OR 16 CHANNELS";
        return false;
    }
    return true;
#endif
}

void loaderMain(Plugin* instance)
{
    for (;;) {
        LoadRequest request;
        {
            std::unique_lock<std::mutex> lock(instance->loaderMutex);
            instance->loaderCondition.wait(lock, [instance] {
                return instance->loaderStopping
                    || !instance->loadRequests.empty();
            });
            if (instance->loaderStopping) return;
            request = std::move(instance->loadRequests.front());
            instance->loadRequests.pop_front();
        }
        LoadResult result;
        result.generation = request.generation;
        result.slot = request.slot;
        result.dirty = request.dirty;
        result.path = std::move(request.path);
        try {
            if (!decodeSampleFile(result.path, result.asset, result.error))
                result.asset.reset();
            else if (result.asset)
                result.analysis = s3g::sample::analyzeCutupsAsset(
                    *result.asset,
                    static_cast<uint32_t>(s3g::sample::kSampleNeonSliceCount),
                    5.0, 1000u, 20.0);
        } catch (...) {
            result.asset.reset();
            result.error = "SAMPLE DECODE EXCEEDED AVAILABLE MEMORY";
        }
        {
            std::lock_guard<std::mutex> lock(instance->loaderMutex);
            instance->loadResults.push_back(std::move(result));
        }
        if (instance->host && instance->host->request_callback)
            instance->host->request_callback(instance->host);
    }
}

bool startLoader(Plugin& instance)
{
    try { instance.loaderThread = std::thread(loaderMain, &instance); }
    catch (...) { return false; }
    return true;
}

void stopLoader(Plugin& instance)
{
    {
        std::lock_guard<std::mutex> lock(instance.loaderMutex);
        instance.loaderStopping = true;
        instance.loadRequests.clear();
    }
    instance.loaderCondition.notify_all();
    if (instance.loaderThread.joinable()) instance.loaderThread.join();
}

void queueSampleLoad(Plugin& instance, std::size_t slot, std::string path,
    bool dirty = true)
{
    if (slot >= s3g::sample::kSampleNeonSlotCount || path.empty()) return;
    LoadRequest request;
    request.generation = ++instance.loadGenerations[slot];
    request.slot = static_cast<uint8_t>(slot);
    request.dirty = dirty;
    request.path = std::move(path);
    {
        std::lock_guard<std::mutex> lock(instance.statusMutex);
        instance.samplePaths[slot] = request.path;
        instance.statuses[slot] = "DECODING...";
    }
    {
        std::lock_guard<std::mutex> lock(instance.loaderMutex);
        instance.loadRequests.erase(std::remove_if(
            instance.loadRequests.begin(), instance.loadRequests.end(),
            [slot](const LoadRequest& pending) {
                return pending.slot == slot;
            }), instance.loadRequests.end());
        instance.loadRequests.push_back(std::move(request));
    }
    instance.loaderCondition.notify_one();
}

void serviceLoads(Plugin& instance)
{
    std::deque<LoadResult> results;
    {
        std::lock_guard<std::mutex> lock(instance.loaderMutex);
        results.swap(instance.loadResults);
    }
    for (auto& result : results) {
        const std::size_t slot = result.slot;
        if (slot >= s3g::sample::kSampleNeonSlotCount
            || result.generation != instance.loadGenerations[slot])
            continue;
        if (!result.asset) {
            std::lock_guard<std::mutex> lock(instance.statusMutex);
            instance.statuses[slot] = result.error.empty()
                ? "SAMPLE DECODE FAILED" : result.error;
            continue;
        }
        {
            std::lock_guard<std::mutex> lock(instance.statusMutex);
            instance.slotAnalyses[slot] = result.analysis;
        }
        if (result.analysis.tempoValid
            && (result.dirty || std::abs(
                instance.sourceBpms[slot].load(std::memory_order_acquire)
                    - 120.0) < 1.0e-9))
            instance.sourceBpms[slot].store(result.analysis.analyzedBpm,
                std::memory_order_release);
        if (result.dirty) {
            // 4 channels default to quad; the user can explicitly select
            // ACN/SN3D for first-order material. Restore never guesses.
            queueGuiParamGesture(instance, slotParamId(slot, kSlotSourceFormat),
                result.asset->channelCount >= 9u ? 1.0 : 0.0);
        }
        (void)publishAsset(instance, slot, std::move(result.asset),
            std::move(result.path), result.dirty);
        instance.embeddedAssets[slot] = false;
        if (result.dirty) snapSlotBoundaries(instance, slot);
        const auto mode = static_cast<SampleNeonChopMode>(
            std::min<uint8_t>(3u, instance.chopModes[slot].load(
                std::memory_order_acquire)));
        if (result.dirty && (mode == SampleNeonChopMode::Transient
            || mode == SampleNeonChopMode::BeatGrid))
            regenerateChopLayout(instance, slot, false);
    }
}
#endif

void clearSample(Plugin& instance, std::size_t slot)
{
    if (slot >= s3g::sample::kSampleNeonSlotCount) return;
#if defined(S3G_SAMPLE_FILE_WORKER)
    ++instance.loadGenerations[slot];
    {
        std::lock_guard<std::mutex> lock(instance.loaderMutex);
        instance.loadRequests.erase(std::remove_if(
            instance.loadRequests.begin(), instance.loadRequests.end(),
            [slot](const LoadRequest& pending) {
                return pending.slot == slot;
            }), instance.loadRequests.end());
    }
#endif
    (void)publishAsset(instance, slot, nullptr, "", true);
    instance.embeddedAssets[slot] = false;
}

void auditionSlot(Plugin& instance, std::size_t slot)
{
    if (slot >= s3g::sample::kSampleNeonSlotCount) return;
    instance.pendingAuditions.fetch_or(
        static_cast<uint32_t>(1u << slot), std::memory_order_release);
    requestProcess(instance);
}

void auditionEditedSlot(Plugin& instance, std::size_t slot)
{
    if (slot >= s3g::sample::kSampleNeonSlotCount) return;
    instance.pendingEditAuditions.fetch_or(static_cast<uint32_t>(1u << slot));
    requestProcess(instance);
}

uint8_t sliceCount(const Plugin& instance, std::size_t slot) noexcept;

void auditionSlice(Plugin& instance, std::size_t slot, uint8_t slice)
{
    if (slot >= s3g::sample::kSampleNeonSlotCount
        || slice >= s3g::sample::kSampleNeonSliceCount
        || slice >= sliceCount(instance, slot)) return;
    instance.pendingSliceAudition.store(
        static_cast<uint32_t>((slot << 8u) | slice),
        std::memory_order_release);
    instance.visibleSlice.store(slice, std::memory_order_relaxed);
    requestProcess(instance);
}

void auditionPerformance(Plugin& instance, std::size_t slot,
    NeonMode mode, uint8_t index, bool alternate = false)
{
    if (slot >= s3g::sample::kSampleNeonSlotCount
        || index >= s3g::sample::kSampleNeonSliceCount) return;
    instance.pendingPerformanceAudition.store(static_cast<uint32_t>(
        (slot << 16u) | (static_cast<uint8_t>(mode) << 8u)
            | index | (alternate ? 0x80u : 0u)),
        std::memory_order_release);
    requestProcess(instance);
}

uint8_t sliceTriggerMode(const Plugin& instance, std::size_t slot,
    std::size_t slice) noexcept
{
    if (slot >= s3g::sample::kSampleNeonSlotCount
        || slice >= s3g::sample::kSampleNeonSliceCount) return 0u;
    return std::min<uint8_t>(2u, instance.sliceTriggers[slot][slice].load(
        std::memory_order_acquire));
}

uint8_t sliceOptions(const Plugin& instance, std::size_t slot,
    std::size_t slice) noexcept
{
    if (slot >= s3g::sample::kSampleNeonSlotCount
        || slice >= s3g::sample::kSampleNeonSliceCount) return 0u;
    return static_cast<uint8_t>(instance.sliceOptions[slot][slice].load(
        std::memory_order_acquire)
        & (kSliceRepeatOption | kSliceSyncOption));
}

bool sliceRepeatEnabled(const Plugin& instance, std::size_t slot,
    std::size_t slice) noexcept
{
    return (sliceOptions(instance, slot, slice) & kSliceRepeatOption) != 0u;
}

bool sliceSyncEnabled(const Plugin& instance, std::size_t slot,
    std::size_t slice) noexcept
{
    return (sliceOptions(instance, slot, slice) & kSliceSyncOption) != 0u;
}

uint8_t sliceCount(const Plugin& instance, std::size_t slot) noexcept
{
    if (slot >= s3g::sample::kSampleNeonSlotCount)
        return static_cast<uint8_t>(s3g::sample::kSampleNeonSliceCount);
    return std::clamp<uint8_t>(instance.sliceCounts[slot].load(
        std::memory_order_acquire), 1u,
        static_cast<uint8_t>(s3g::sample::kSampleNeonSliceCount));
}

SampleNeonSliceLayout sliceLayout(const Plugin& instance,
    std::size_t slot) noexcept
{
    const uint8_t count = sliceCount(instance, slot);
    SampleNeonSliceLayout result;
    result.sliceCount = count;
    for (std::size_t marker = 0u; marker <= count; ++marker)
        result.boundaries[marker] = instance.sliceBoundaries[slot][marker]
            .load(std::memory_order_acquire);
    return result.valid() ? result
        : s3g::sample::equalSampleNeonSliceLayout(count);
}

void storeSliceLayout(Plugin& instance, std::size_t slot,
    const SampleNeonSliceLayout& layout, bool dirty = true) noexcept
{
    if (slot >= s3g::sample::kSampleNeonSlotCount || !layout.valid()) return;
    for (std::size_t marker = 0u; marker <= layout.sliceCount; ++marker)
        instance.sliceBoundaries[slot][marker].store(
            layout.boundaries[marker], std::memory_order_release);
    instance.sliceCounts[slot].store(layout.sliceCount,
        std::memory_order_release);
    snapSlotBoundaries(instance, slot, 0u);
    const uint8_t selected = instance.visibleSlice.load(
        std::memory_order_relaxed);
    if (instance.selectedSlot.load() == slot && selected >= layout.sliceCount)
        instance.visibleSlice.store(static_cast<uint8_t>(
            layout.sliceCount - 1u), std::memory_order_relaxed);
    if (instance.selectedSlot.load() == slot
        && instance.visibleMode.load() == static_cast<uint8_t>(NeonMode::Slicer)
        && instance.visibleBank.load(std::memory_order_relaxed) * 8u
            >= layout.sliceCount)
        instance.visibleBank.store(static_cast<uint8_t>(
            (layout.sliceCount - 1u) / 8u), std::memory_order_relaxed);
    instance.ledFeedbackDirty.store(true, std::memory_order_release);
    if (dirty) markStateDirty(instance);
    requestProcess(instance);
}

void snapSlotBoundaries(Plugin& instance, std::size_t slot, unsigned trimMask) noexcept
{
    const auto* asset = instance.publishedAssets[slot].load(std::memory_order_acquire);
    if (!asset || asset->frameCount() < 2u
        || paramValue(instance, slotParamId(slot, kSlotZeroCross)) < 0.5) return;
    const uint32_t total = asset->frameCount();
    auto frameFor = [total](double position) {
        return static_cast<uint32_t>(std::llround(std::clamp(position, 0.0, 1.0) * total));
    };
    uint32_t start = std::min(total - 1u, frameFor(paramValue(instance, slotParamId(slot, kSlotStart))));
    uint32_t end = std::clamp(frameFor(paramValue(instance, slotParamId(slot, kSlotEnd))), start + 1u, total);
    if (trimMask & 1u) start = s3g::sample::sampleNeonBoundary(*asset, start, 0u, end - 1u);
    if (trimMask & 2u) end = s3g::sample::sampleNeonBoundary(*asset, end, start + 1u, total);
    const auto base = kGlobalParamCount + slot * kSlotParameterCount;
    instance.parameters[base + kSlotStart].store(static_cast<double>(start) / total);
    instance.parameters[base + kSlotEnd].store(static_cast<double>(end) / total);
    auto layout = sliceLayout(instance, slot);
    // There cannot be more nonempty slices than frames.
    if (layout.sliceCount > end - start)
        layout = s3g::sample::equalSampleNeonSliceLayout(end - start);
    uint32_t previous = start;
    for (uint8_t marker = 1u; marker < layout.sliceCount; ++marker) {
        const uint32_t wanted = start + static_cast<uint32_t>(std::llround(
            layout.boundaries[marker] * (end - start)));
        const uint32_t next = start + static_cast<uint32_t>(std::llround(
            layout.boundaries[marker + 1u] * (end - start)));
        const uint32_t upper = std::clamp(next > 0u ? next - 1u : 0u,
            previous + 1u, end - (layout.sliceCount - marker));
        const uint32_t snapped = s3g::sample::sampleNeonBoundary(*asset,
            wanted, previous + 1u, upper);
        layout.boundaries[marker] = static_cast<double>(snapped - start) / (end - start);
        previous = snapped;
    }
    for (uint8_t marker = 0u; marker <= layout.sliceCount; ++marker)
        instance.sliceBoundaries[slot][marker].store(layout.boundaries[marker]);
    instance.sliceCounts[slot].store(layout.sliceCount);
}

constexpr std::array<double, 5u> kChopBeatDivisions {{
    0.25, 0.5, 1.0, 2.0, 4.0,
}};

void regenerateChopLayout(Plugin& instance, std::size_t slot,
    bool dirty = true) noexcept
{
    if (slot >= s3g::sample::kSampleNeonSlotCount) return;
    const auto mode = static_cast<SampleNeonChopMode>(std::min<uint8_t>(
        3u, instance.chopModes[slot].load(std::memory_order_acquire)));
    const uint8_t targetCount = sliceCount(instance, slot);
    SampleNeonSliceLayout layout = sliceLayout(instance, slot);
    if (mode == SampleNeonChopMode::Equal) {
        layout = s3g::sample::equalSampleNeonSliceLayout(targetCount);
    } else if (mode == SampleNeonChopMode::Transient) {
        s3g::sample::CutupsLaneMetadata analysis;
        double duration = 0.0;
        {
            std::lock_guard<std::mutex> lock(instance.statusMutex);
            analysis = instance.slotAnalyses[slot];
            const auto& asset = instance.controlAssets[slot];
            if (asset) duration = asset->frameCount() / asset->sampleRate;
        }
        const double rangeStart = paramValue(instance,
            slotParamId(slot, kSlotStart));
        const double rangeEnd = paramValue(instance,
            slotParamId(slot, kSlotEnd));
        const double range = std::max(1.0e-9, rangeEnd - rangeStart);
        std::array<float, s3g::sample::kSampleNeonSliceCount> starts {};
        std::size_t startCount = 0u;
        starts[startCount++] = 0.0f;
        for (uint32_t index = 0u;
             index < analysis.transientRegions.count
                && startCount < starts.size(); ++index) {
            const double position = analysis.transientRegions.starts[index];
            if (position <= rangeStart || position >= rangeEnd) continue;
            starts[startCount++] = static_cast<float>(
                (position - rangeStart) / range);
        }
        layout = s3g::sample::transientSampleNeonSliceLayout(
            starts.data(), startCount, instance.transientSliceLimits[slot].load(),
            instance.transientPreRollMs[slot].load(), duration * range);
    } else if (mode == SampleNeonChopMode::BeatGrid) {
        std::shared_ptr<const SampleAsset> asset;
        {
            std::lock_guard<std::mutex> lock(instance.statusMutex);
            asset = instance.controlAssets[slot];
        }
        const uint8_t division = std::min<uint8_t>(4u,
            instance.chopBeatDivisions[slot].load(
                std::memory_order_acquire));
        const double selectedRange = std::max(0.0,
            paramValue(instance, slotParamId(slot, kSlotEnd))
                - paramValue(instance, slotParamId(slot, kSlotStart)));
        const double duration = asset
            ? static_cast<double>(asset->frameCount()) / asset->sampleRate
                * selectedRange : 0.0;
        layout = s3g::sample::beatGridSampleNeonSliceLayout(duration,
            instance.sourceBpms[slot].load(std::memory_order_acquire),
            kChopBeatDivisions[division]);
    }
    storeSliceLayout(instance, slot, layout, dirty);
}

void setSliceCount(Plugin& instance, std::size_t slot, int count,
    bool dirty = true) noexcept
{
    if (slot >= s3g::sample::kSampleNeonSlotCount) return;
    const uint8_t bounded = static_cast<uint8_t>(std::clamp(
        count, 1, static_cast<int>(s3g::sample::kSampleNeonSliceCount)));
    instance.sliceCounts[slot].store(bounded, std::memory_order_release);
    instance.transientSliceLimits[slot].store(bounded, std::memory_order_release);
    const auto mode = static_cast<SampleNeonChopMode>(std::min<uint8_t>(
        3u, instance.chopModes[slot].load(std::memory_order_acquire)));
    if (mode == SampleNeonChopMode::Equal
        || mode == SampleNeonChopMode::Transient) {
        regenerateChopLayout(instance, slot, dirty);
        return;
    }
    const uint8_t selected = instance.visibleSlice.load(
        std::memory_order_relaxed);
    if (selected >= bounded)
        instance.visibleSlice.store(static_cast<uint8_t>(bounded - 1u),
            std::memory_order_relaxed);
    if (instance.visibleBank.load(std::memory_order_relaxed) * 8u >= bounded)
        instance.visibleBank.store(static_cast<uint8_t>(
            (bounded - 1u) / 8u), std::memory_order_relaxed);
    instance.ledFeedbackDirty.store(true, std::memory_order_release);
    if (dirty) markStateDirty(instance);
    requestProcess(instance);
}

bool slotOptionEnabled(const Plugin& instance, std::size_t slot,
    uint8_t option) noexcept
{
    return slot < s3g::sample::kSampleNeonSlotCount
        && (instance.slotOptions[slot].load(std::memory_order_acquire)
            & option) != 0u;
}

void setSlotOption(Plugin& instance, std::size_t slot, uint8_t option,
    bool enabled, bool dirty = true) noexcept
{
    if (slot >= s3g::sample::kSampleNeonSlotCount) return;
    auto& target = instance.slotOptions[slot];
    uint8_t previous = target.load(std::memory_order_acquire);
    uint8_t next = previous;
    do {
        next = enabled ? static_cast<uint8_t>(previous | option)
                       : static_cast<uint8_t>(previous & ~option);
    } while (!target.compare_exchange_weak(previous, next,
        std::memory_order_release, std::memory_order_acquire));
    instance.ledFeedbackDirty.store(true, std::memory_order_release);
    if (dirty) markStateDirty(instance);
    requestProcess(instance);
}

void setChopMode(Plugin& instance, std::size_t slot, uint8_t mode,
    bool dirty = true) noexcept
{
    if (slot >= s3g::sample::kSampleNeonSlotCount) return;
    instance.chopModes[slot].store(std::min<uint8_t>(mode, 3u),
        std::memory_order_release);
    regenerateChopLayout(instance, slot, dirty);
}

void setChopBeatDivision(Plugin& instance, std::size_t slot, int division,
    bool dirty = true) noexcept
{
    if (slot >= s3g::sample::kSampleNeonSlotCount) return;
    instance.chopBeatDivisions[slot].store(static_cast<uint8_t>(
        std::clamp(division, 0, 4)), std::memory_order_release);
    instance.chopModes[slot].store(static_cast<uint8_t>(
        SampleNeonChopMode::BeatGrid), std::memory_order_release);
    regenerateChopLayout(instance, slot, dirty);
}

void addLiveSliceMarker(Plugin& instance, std::size_t slot,
    double position) noexcept
{
    auto layout = sliceLayout(instance, slot);
    if (!s3g::sample::addSampleNeonSliceMarker(layout, position)) return;
    instance.chopModes[slot].store(static_cast<uint8_t>(
        SampleNeonChopMode::LiveMark), std::memory_order_release);
    storeSliceLayout(instance, slot, layout);
}

void moveLiveSliceMarker(Plugin& instance, std::size_t slot,
    std::size_t marker, double position) noexcept
{
    auto layout = sliceLayout(instance, slot);
    if (!s3g::sample::moveSampleNeonSliceMarker(
            layout, marker, position)) return;
    instance.chopModes[slot].store(static_cast<uint8_t>(
        SampleNeonChopMode::LiveMark), std::memory_order_release);
    storeSliceLayout(instance, slot, layout);
}

void removeNearestSliceMarker(Plugin& instance, std::size_t slot,
    double position) noexcept
{
    auto layout = sliceLayout(instance, slot);
    if (layout.sliceCount <= 1u) return;
    std::size_t nearest = 1u;
    double nearestDistance = std::abs(layout.boundaries[nearest] - position);
    for (std::size_t marker = 2u; marker < layout.sliceCount; ++marker) {
        const double distance = std::abs(
            layout.boundaries[marker] - position);
        if (distance < nearestDistance) {
            nearest = marker;
            nearestDistance = distance;
        }
    }
    for (std::size_t marker = nearest; marker < layout.sliceCount; ++marker)
        layout.boundaries[marker] = layout.boundaries[marker + 1u];
    --layout.sliceCount;
    layout.boundaries[layout.sliceCount] = 1.0;
    instance.chopModes[slot].store(static_cast<uint8_t>(
        SampleNeonChopMode::LiveMark), std::memory_order_release);
    storeSliceLayout(instance, slot, layout);
}


void setSliceTriggerMode(Plugin& instance, std::size_t slot,
    std::size_t slice, uint8_t mode, bool dirty = true) noexcept
{
    if (slot >= s3g::sample::kSampleNeonSlotCount
        || slice >= s3g::sample::kSampleNeonSliceCount) return;
    instance.sliceTriggers[slot][slice].store(
        std::min<uint8_t>(mode, 2u), std::memory_order_release);
    instance.ledFeedbackDirty.store(true, std::memory_order_release);
    if (dirty) markStateDirty(instance);
    requestProcess(instance);
}

void setSliceOption(Plugin& instance, std::size_t slot,
    std::size_t slice, uint8_t option, bool enabled,
    bool dirty = true) noexcept
{
    if (slot >= s3g::sample::kSampleNeonSlotCount
        || slice >= s3g::sample::kSampleNeonSliceCount
        || (option != kSliceRepeatOption && option != kSliceSyncOption))
        return;
    auto& target = instance.sliceOptions[slot][slice];
    uint8_t previous = target.load(std::memory_order_acquire);
    uint8_t next = previous;
    do {
        next = enabled ? static_cast<uint8_t>(previous | option)
                       : static_cast<uint8_t>(previous & ~option);
    } while (!target.compare_exchange_weak(previous, next,
        std::memory_order_release, std::memory_order_acquire));
    instance.ledFeedbackDirty.store(true, std::memory_order_release);
    if (dirty) markStateDirty(instance);
    requestProcess(instance);
}

#include "s3g_sample_neon_workflow.inc"

uint32_t paramsCount(const clap_plugin_t*)
{
    return static_cast<uint32_t>(kStoredParamCount);
}

bool paramsGetInfo(const clap_plugin_t*, uint32_t index,
    clap_param_info_t* info)
{
    if (!info || index >= kStoredParamCount) return false;
    const clap_id id = parameterIdAt(index);
    std::size_t stored = 0u;
    std::size_t slot = 0u;
    SlotParamOffset offset = kSlotGain;
    const ParamDef* definition = nullptr;
    if (!parameterLocation(id, stored, definition, &slot, &offset))
        return false;
    *info = {};
    info->id = id;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    if (definition->stepped) info->flags |= CLAP_PARAM_IS_STEPPED;
    if (id >= kSlotParamBase && (offset == kSlotMute || offset == kSlotSolo))
        info->flags = CLAP_PARAM_IS_HIDDEN | CLAP_PARAM_IS_READONLY | CLAP_PARAM_IS_STEPPED;
    std::snprintf(info->name, sizeof(info->name), "%s", definition->name);
    if (id >= kSlotParamBase)
        std::snprintf(info->module, sizeof(info->module), "Slot %02zu / %s",
            slot + 1u, definition->module);
    else std::snprintf(info->module, sizeof(info->module), "%s",
        definition->module);
    info->min_value = definition->minimum;
    info->max_value = definition->maximum;
    info->default_value = id >= kSlotParamBase
            && offset == kSlotCharacter
        ? static_cast<double>(slot % 8u)
        : definition->defaultValue;
    return true;
}

bool paramsGetValue(const clap_plugin_t* plugin, clap_id id, double* value)
{
    if (!value) return false;
    std::size_t index = 0u;
    const ParamDef* definition = nullptr;
    if (!parameterLocation(id, index, definition)) return false;
    *value = self(plugin)->parameters[index].load(std::memory_order_acquire);
    return true;
}

const char* characterName(int value) noexcept
{
    return s3g::sample::kSampleNeonFxNames[static_cast<std::size_t>(std::clamp(value, 0, 7))];
}

bool paramsValueToText(const clap_plugin_t*, clap_id id, double value,
    char* display, uint32_t size)
{
    if (!display || size == 0u) return false;
    std::size_t index = 0u;
    std::size_t slot = 0u;
    SlotParamOffset offset = kSlotGain;
    const ParamDef* definition = nullptr;
    if (!parameterLocation(id, index, definition, &slot, &offset))
        return false;
    value = clampParam(*definition, value);
    if (id == kOutputLayoutParamId)
        std::snprintf(display, size, "%s", kOutputLayoutNames[
            static_cast<std::size_t>(std::clamp(value, 0.0, 6.0))]);
    else if (id >= kSlotParamBase && offset == kSlotSourceFormat)
        std::snprintf(display, size, "%s", value >= 0.5
            ? "ACN / SN3D" : "DISCRETE");
    else if (id == kNeonActiveParamId)
        std::snprintf(display, size, "%s", value >= 0.5 ? "OWN" : "OFF");
    else if (id == kTempoSyncParamId || (id >= kSlotParamBase
            && (offset == kSlotMute || offset == kSlotSolo || offset == kSlotZeroCross)))
        std::snprintf(display, size, "%s", value >= 0.5 ? "On" : "Off");
    else if (id == kMidiReceiveParamId)
        std::snprintf(display, size, value < 0.5 ? "Omni" : "Ch %d",
            static_cast<int>(std::lround(value)));
    else if (id >= kSlotParamBase && offset == kSlotCharacter)
        std::snprintf(display, size, "%s",
            characterName(static_cast<int>(std::lround(value))));
    else if (id >= kSlotParamBase && offset == kSlotDirection) {
        constexpr const char* names[] { "Forward", "Reverse", "Ping-pong", "Reverse ping-pong" };
        std::snprintf(display, size, "%s", names[static_cast<unsigned>(std::clamp(value, 0.0, 3.0))]);
    } else if (id >= kSlotParamBase && offset == kSlotTriggerMode) {
        constexpr std::array<const char*, 4u> names {{
            "Auto", "Gate", "One Shot", "Toggle",
        }};
        std::snprintf(display, size, "%s", names[static_cast<std::size_t>(
            std::clamp<int>(static_cast<int>(std::lround(value)), 0, 3))]);
    } else if (id == kMasterGainParamId || (id >= kSlotParamBase && offset == kSlotGain))
        std::snprintf(display, size, "%.1f dB", value);
    else if (id >= kSlotParamBase && offset == kSlotTune)
        std::snprintf(display, size, "%.1f st", value);
    else if (id >= kSlotParamBase && offset == kSlotFilterCutoff)
        std::snprintf(display, size, value >= 999.5 ? "%.1f kHz" : "%.0f Hz",
            value >= 999.5 ? value / 1000.0 : value);
    else if (!definition->stepped && definition->minimum == 0.0 && definition->maximum == 1.0)
        std::snprintf(display, size, "%.1f%%", value * 100.0);
    else if (definition->stepped)
        std::snprintf(display, size, "%d",
            static_cast<int>(std::lround(value)));
    else std::snprintf(display, size, "%.3f", value);
    return true;
}

bool paramsTextToValue(const clap_plugin_t* plugin, clap_id id, const char* text,
    double* value)
{
    if (!text || !value) return false;
    std::size_t index = 0u;
    SlotParamOffset offset = kSlotGain;
    const ParamDef* definition = nullptr;
    if (!parameterLocation(id, index, definition, nullptr, &offset)) return false;
    const auto sameText = [](const char* left, const char* right) {
        while (*left && *right) {
            if (std::tolower(static_cast<unsigned char>(*left++))
                != std::tolower(static_cast<unsigned char>(*right++))) return false;
        }
        return *left == *right;
    };
    if (definition->stepped) {
        char formatted[128] {};
        for (int v = static_cast<int>(definition->minimum);
             v <= static_cast<int>(definition->maximum); ++v) {
            if (paramsValueToText(plugin, id, v, formatted, sizeof(formatted))
                && sameText(text, formatted)) { *value = v; return true; }
        }
    }
    char* end = nullptr;
    double parsed = std::strtod(text, &end);
    if (end == text || !std::isfinite(parsed)) return false;
    while (*end && std::isspace(static_cast<unsigned char>(*end))) ++end;
    if (*end) {
        if (!definition->stepped && definition->minimum == 0.0
            && definition->maximum == 1.0 && sameText(end, "%")) parsed /= 100.0;
        else if (id >= kSlotParamBase && offset == kSlotFilterCutoff && sameText(end, "kHz")) parsed *= 1000.0;
        else if (!((id == kMasterGainParamId || (id >= kSlotParamBase && offset == kSlotGain)) && sameText(end, "dB"))
            && !(id >= kSlotParamBase && offset == kSlotTune && sameText(end, "st"))
            && !(id >= kSlotParamBase && offset == kSlotFilterCutoff && sameText(end, "Hz"))) return false;
    }
    *value = clampParam(*definition, parsed);
    return true;
}

void applyParamInput(Plugin& instance, const clap_input_events_t* input)
{
    if (!input || !input->size || !input->get) return;
    const uint32_t count = input->size(input);
    for (uint32_t index = 0u; index < count; ++index) {
        const auto* header = input->get(input, index);
        if (!header || header->space_id != CLAP_CORE_EVENT_SPACE_ID
            || header->type != CLAP_EVENT_PARAM_VALUE
            || header->size < sizeof(clap_event_param_value_t)) continue;
        const auto* event = reinterpret_cast<
            const clap_event_param_value_t*>(header);
        setParam(instance, event->param_id, event->value);
    }
}

void paramsFlush(const clap_plugin_t* plugin,
    const clap_input_events_t* input, const clap_output_events_t* output)
{
    auto& instance = *self(plugin);
    applyParamInput(instance, input);
    serviceGuiParamEvents(instance, output);
}

const clap_plugin_params_t paramsExtension {
    paramsCount, paramsGetInfo, paramsGetValue,
    paramsValueToText, paramsTextToValue, paramsFlush,
};

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    auto& instance = *self(plugin);
    if (instance.resetAllPhase.load()) return false;
    const StateHeader header;
    if (!s3g::clap_state::writeAll(stream, &header, sizeof(header)))
        return false;
    std::array<double, kStoredParamCount> values {};
    for (std::size_t index = 0u; index < values.size(); ++index)
        values[index] = instance.parameters[index].load(
            std::memory_order_acquire);
    if (!s3g::clap_state::writeAll(stream, values.data(),
            values.size() * sizeof(double))) return false;
    std::lock_guard<std::mutex> lock(instance.statusMutex);
    for (const auto& path : instance.samplePaths) {
        std::array<char, kMaximumPathBytes> stored {};
        std::snprintf(stored.data(), stored.size(), "%s", path.c_str());
        if (!s3g::clap_state::writeAll(stream, stored.data(), stored.size()))
            return false;
    }
    std::array<uint8_t, kSliceModeStateBytes> modes {};
    std::size_t modeIndex = 0u;
    for (std::size_t slot = 0u; slot < instance.sliceTriggers.size(); ++slot)
        for (std::size_t slice = 0u;
             slice < instance.sliceTriggers[slot].size(); ++slice)
            modes[modeIndex++] = sliceTriggerMode(instance, slot, slice);
    if (!s3g::clap_state::writeAll(stream, modes.data(), modes.size()))
        return false;
    std::array<uint8_t, kSliceCountStateBytes> counts {};
    for (std::size_t slot = 0u; slot < counts.size(); ++slot)
        counts[slot] = sliceCount(instance, slot);
    if (!s3g::clap_state::writeAll(stream, counts.data(), counts.size()))
        return false;
    std::array<uint8_t, kSliceOptionStateBytes> options {};
    std::size_t optionIndex = 0u;
    for (std::size_t slot = 0u; slot < instance.sliceOptions.size(); ++slot)
        for (std::size_t slice = 0u;
             slice < instance.sliceOptions[slot].size(); ++slice)
            options[optionIndex++] = sliceOptions(instance, slot, slice);
    if (!s3g::clap_state::writeAll(stream, options.data(), options.size()))
        return false;
    std::array<uint8_t, s3g::sample::kSampleNeonSlotCount> slotOptions {};
    std::array<uint8_t, s3g::sample::kSampleNeonSlotCount> chokeGroups {};
    std::array<uint8_t, s3g::sample::kSampleNeonSlotCount> chopModes {};
    std::array<uint8_t, s3g::sample::kSampleNeonSlotCount> beatDivisions {};
    std::array<double, s3g::sample::kSampleNeonSlotCount> sourceBpms {};
    std::array<double, s3g::sample::kSampleNeonSlotCount
        * (s3g::sample::kSampleNeonSliceCount + 1u)> boundaries {};
    std::array<double, s3g::sample::kSampleNeonSlotCount
        * s3g::sample::kSampleNeonCueCount> cuePositions {};
    std::array<uint8_t, s3g::sample::kSampleNeonSlotCount
        * s3g::sample::kSampleNeonCueCount> cueEnabled {};
    std::array<double, s3g::sample::kSampleNeonSlotCount
        * s3g::sample::kSampleNeonSavedLoopCount> loopStarts {};
    std::array<double, s3g::sample::kSampleNeonSlotCount
        * s3g::sample::kSampleNeonSavedLoopCount> loopEnds {};
    std::array<uint8_t, s3g::sample::kSampleNeonSlotCount
        * s3g::sample::kSampleNeonSavedLoopCount> loopEnabled {};
    std::array<double, s3g::sample::kSampleNeonSlotCount> editPositions {};
    for (std::size_t slot = 0u; slot < slotOptions.size(); ++slot) {
        slotOptions[slot] = instance.slotOptions[slot].load(
            std::memory_order_acquire);
        chokeGroups[slot] = instance.chokeGroups[slot].load(
            std::memory_order_acquire);
        chopModes[slot] = instance.chopModes[slot].load(
            std::memory_order_acquire);
        beatDivisions[slot] = instance.chopBeatDivisions[slot].load(
            std::memory_order_acquire);
        sourceBpms[slot] = instance.sourceBpms[slot].load(
            std::memory_order_acquire);
        editPositions[slot] = instance.editPositions[slot].load(
            std::memory_order_acquire);
        for (std::size_t marker = 0u;
             marker <= s3g::sample::kSampleNeonSliceCount; ++marker)
            boundaries[slot * (s3g::sample::kSampleNeonSliceCount + 1u)
                + marker] = instance.sliceBoundaries[slot][marker].load(
                    std::memory_order_acquire);
        for (std::size_t cue = 0u;
             cue < s3g::sample::kSampleNeonCueCount; ++cue) {
            const std::size_t flat = slot
                * s3g::sample::kSampleNeonCueCount + cue;
            cuePositions[flat] = instance.hotCuePositions[slot][cue].load(
                std::memory_order_acquire);
            cueEnabled[flat] = instance.hotCueEnabled[slot][cue].load(
                std::memory_order_acquire);
        }
        for (std::size_t loop = 0u;
             loop < s3g::sample::kSampleNeonSavedLoopCount; ++loop) {
            const std::size_t flat = slot
                * s3g::sample::kSampleNeonSavedLoopCount + loop;
            loopStarts[flat] = instance.savedLoopStarts[slot][loop].load(
                std::memory_order_acquire);
            loopEnds[flat] = instance.savedLoopEnds[slot][loop].load(
                std::memory_order_acquire);
            loopEnabled[flat] = instance.savedLoopEnabled[slot][loop].load(
                std::memory_order_acquire);
        }
    }
    const auto writeArray = [stream](const auto& values) {
        return s3g::clap_state::writeAll(stream, values.data(),
            values.size() * sizeof(values[0u]));
    };
    if (!writeArray(slotOptions) || !writeArray(chokeGroups)
        || !writeArray(chopModes) || !writeArray(beatDivisions)
        || !writeArray(sourceBpms) || !writeArray(boundaries)
        || !writeArray(cuePositions) || !writeArray(cueEnabled)
        || !writeArray(loopStarts) || !writeArray(loopEnds)
        || !writeArray(loopEnabled) || !writeArray(editPositions))
        return false;
    std::array<uint8_t, kFlipPatternCount * kFlipMaximumSteps> flipSteps {};
    std::array<uint8_t, kFlipPatternCount * kFlipMaximumSteps> flipVelocity {};
    std::array<uint8_t, kFlipPatternCount> flipCounts {};
    for (std::size_t pattern = 0u; pattern < kFlipPatternCount; ++pattern) {
        flipCounts[pattern] = instance.flipStepCounts[pattern].load(
            std::memory_order_acquire);
        for (std::size_t step = 0u; step < kFlipMaximumSteps; ++step) {
            const std::size_t flat = pattern * kFlipMaximumSteps + step;
            flipSteps[flat] = instance.flipSteps[pattern][step].load(
                std::memory_order_acquire);
            flipVelocity[flat] = instance.flipVelocities[pattern][step].load(
                std::memory_order_acquire);
        }
    }
    const std::array<uint8_t, 4u> flipState {{
        instance.selectedFlipPattern.load(std::memory_order_acquire),
        static_cast<uint8_t>(instance.flipRecording.load(
            std::memory_order_acquire) ? 1u : 0u),
        static_cast<uint8_t>(instance.flipPlaying.load(
            std::memory_order_acquire) ? 1u : 0u),
        static_cast<uint8_t>(instance.flipLooping.load(
            std::memory_order_acquire) ? 1u : 0u),
    }};
    if (!writeArray(flipSteps) || !writeArray(flipVelocity)
        || !writeArray(flipCounts) || !writeArray(flipState)) return false;
    std::array<uint8_t, s3g::sample::kSampleNeonSlotCount> domains {};
    std::array<uint8_t, s3g::sample::kSampleNeonSlotCount> quantize {};
    for (std::size_t slot = 0u; slot < domains.size(); ++slot) {
        domains[slot] = instance.slicerDomainIndices[slot].load(
            std::memory_order_acquire);
        quantize[slot] = instance.slicerQuantizeIndices[slot].load(
            std::memory_order_acquire);
    }
    if (!writeArray(domains) || !writeArray(quantize)) return false;
    std::array<uint8_t, s3g::sample::kSampleNeonSlotCount> textures {};
    std::array<uint8_t, s3g::sample::kSampleNeonSlotCount> motionPaths {};
    std::array<float, s3g::sample::kSampleNeonSlotCount> motionRates {};
    std::array<float, s3g::sample::kSampleNeonSlotCount> motionLoci {};
    std::array<float, s3g::sample::kSampleNeonSlotCount> motionFields {};
    std::array<float, s3g::sample::kSampleNeonSlotCount> lanePositions {};
    std::array<float, s3g::sample::kSampleNeonSlotCount> laneDepths {};
    std::array<float, s3g::sample::kSampleNeonSlotCount> grainDensities {};
    std::array<float, s3g::sample::kSampleNeonSlotCount> grainSizes {};
    std::array<float, s3g::sample::kSampleNeonSlotCount> grainPositions {};
    std::array<float, s3g::sample::kSampleNeonSlotCount> grainSprays {};
    std::array<float, s3g::sample::kSampleNeonSlotCount> grainPitches {};
    std::array<float, s3g::sample::kSampleNeonSlotCount> grainReverses {};
    for (std::size_t slot = 0u; slot < textures.size(); ++slot) {
        textures[slot] = normalizedStageOptions(
            instance.textureOptions[slot].load(std::memory_order_acquire));
        motionPaths[slot] = instance.motionPaths[slot].load(
            std::memory_order_acquire);
        motionRates[slot] = instance.motionRates[slot].load(
            std::memory_order_acquire);
        motionLoci[slot] = instance.motionLoci[slot].load(
            std::memory_order_acquire);
        motionFields[slot] = instance.motionFields[slot].load(
            std::memory_order_acquire);
        lanePositions[slot] = instance.lanePositions[slot].load(
            std::memory_order_acquire);
        laneDepths[slot] = instance.laneMotionDepths[slot].load(
            std::memory_order_acquire);
        grainDensities[slot] = instance.grainDensities[slot].load(
            std::memory_order_acquire);
        grainSizes[slot] = instance.grainSizes[slot].load(
            std::memory_order_acquire);
        grainPositions[slot] = instance.grainPositions[slot].load(
            std::memory_order_acquire);
        grainSprays[slot] = instance.grainSprays[slot].load(
            std::memory_order_acquire);
        grainPitches[slot] = instance.grainPitchSprays[slot].load(
            std::memory_order_acquire);
        grainReverses[slot] = instance.grainReverseChances[slot].load(
            std::memory_order_acquire);
    }
    if (!writeArray(textures) || !writeArray(motionPaths)
        || !writeArray(motionRates) || !writeArray(motionLoci)
        || !writeArray(motionFields) || !writeArray(lanePositions)
        || !writeArray(laneDepths) || !writeArray(grainDensities)
        || !writeArray(grainSizes) || !writeArray(grainPositions)
        || !writeArray(grainSprays) || !writeArray(grainPitches)
        || !writeArray(grainReverses)) return false;
    std::array<float, s3g::sample::kSampleNeonSlotCount> preRollMs {};
    std::array<uint8_t, s3g::sample::kSampleNeonSlotCount> transientLimits {};
    for (std::size_t slot = 0u; slot < preRollMs.size(); ++slot) {
        preRollMs[slot] = instance.transientPreRollMs[slot].load();
        transientLimits[slot] = instance.transientSliceLimits[slot].load();
    }
    if (!writeArray(preRollMs) || !writeArray(transientLimits)) return false;
    std::array<uint8_t, 32u> clocks {};
    std::array<float, 32u> motionSeconds {}, shotSeconds {}, grainIntervals {};
    for (std::size_t slot = 0u; slot < 32u; ++slot) {
        clocks[slot] = instance.playbackClocks[slot].load();
        motionSeconds[slot] = instance.motionSeconds[slot].load();
        shotSeconds[slot] = instance.shotSeconds[slot].load();
        grainIntervals[slot] = instance.grainIntervals[slot].load();
    }
    if (!writeArray(clocks) || !writeArray(motionSeconds)
        || !writeArray(shotSeconds) || !writeArray(grainIntervals)) return false;
    std::array<float, 32u * 36u> modern {};
    for (unsigned slot = 0u; slot < 32u; ++slot) {
        for (unsigned effect = 0u; effect < 8u; ++effect)
            for (unsigned n = 0u; n < 3u; ++n) modern[slot * 36u + effect * 3u + n] = instance.fxParameters[slot][effect][n].load();
        for (unsigned method = 0u; method < 3u; ++method)
            for (unsigned n = 0u; n < 4u; ++n) modern[slot * 36u + 24u + method * 4u + n] = instance.techniqueParameters[slot][method][n].load();
    }
    if (!writeArray(modern)) return false;
    std::array<float, 64u> envelopes {};
    for (unsigned slot = 0u; slot < 32u; ++slot) {
        envelopes[slot * 2u] = instance.techniqueAttackSeconds[slot].load();
        envelopes[slot * 2u + 1u] = instance.techniqueReleaseSeconds[slot].load();
    }
    if (!writeArray(envelopes)) return false;
    const std::array<double, 5u> captureSettings {{ instance.captureStart.load(),
        instance.captureEnd.load(), static_cast<double>(instance.captureLayout.load()),
        instance.captureZeroCross.load() ? 1.0 : 0.0, static_cast<double>(instance.captureTarget.load()) }};
    if (!writeArray(captureSettings)) return false;
    std::array<const SampleAsset*, 33u> embedded {};
    for (std::size_t slot = 0u; slot < 32u; ++slot)
        if (instance.embeddedAssets[slot]) embedded[slot] = instance.controlAssets[slot].get();
    embedded[32u] = instance.captureAsset.get();
    return writeEmbeddedAudio(stream, embedded);
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    auto& instance = *self(plugin);
    if (instance.resetAllPhase.load()) return false;
    StateHeader header;
    if (!s3g::clap_state::readAll(stream, &header, sizeof(header))
        || header.magic != kStateMagic
        || (header.version != kStateVersion && header.version != 13u && header.version != 12u && header.version != 11u && header.version != 10u)
        || header.parameterCount != kStoredParamCount
        || header.pathBytes != kMaximumPathBytes) return false;
    std::array<double, kStoredParamCount> values {};
    if (!s3g::clap_state::readAll(stream, values.data(),
            values.size() * sizeof(double))) return false;
    std::array<std::array<char, kMaximumPathBytes>,
        s3g::sample::kSampleNeonSlotCount> paths {};
    for (auto& path : paths)
        if (!s3g::clap_state::readAll(stream, path.data(), path.size()))
            return false;
    std::array<uint8_t, kSliceModeStateBytes> modes {};
    if (!s3g::clap_state::readAll(stream, modes.data(), modes.size()))
        return false;
    std::array<uint8_t, kSliceCountStateBytes> counts {};
    counts.fill(static_cast<uint8_t>(s3g::sample::kSampleNeonSliceCount));
    if (!s3g::clap_state::readAll(stream, counts.data(), counts.size()))
        return false;
    std::array<uint8_t, kSliceOptionStateBytes> options {};
    if (!s3g::clap_state::readAll(stream, options.data(), options.size()))
        return false;
    std::array<uint8_t, s3g::sample::kSampleNeonSlotCount> slotOptions {};
    slotOptions.fill(kSlotVelocityOption);
    std::array<uint8_t, s3g::sample::kSampleNeonSlotCount> chokeGroups {};
    std::array<uint8_t, s3g::sample::kSampleNeonSlotCount> chopModes {};
    chopModes.fill(static_cast<uint8_t>(SampleNeonChopMode::Equal));
    std::array<uint8_t, s3g::sample::kSampleNeonSlotCount> beatDivisions {};
    beatDivisions.fill(2u);
    std::array<double, s3g::sample::kSampleNeonSlotCount> sourceBpms {};
    sourceBpms.fill(120.0);
    std::array<double, s3g::sample::kSampleNeonSlotCount
        * (s3g::sample::kSampleNeonSliceCount + 1u)> boundaries {};
    std::array<double, s3g::sample::kSampleNeonSlotCount
        * s3g::sample::kSampleNeonCueCount> cuePositions {};
    std::array<uint8_t, s3g::sample::kSampleNeonSlotCount
        * s3g::sample::kSampleNeonCueCount> cueEnabled {};
    std::array<double, s3g::sample::kSampleNeonSlotCount
        * s3g::sample::kSampleNeonSavedLoopCount> loopStarts {};
    std::array<double, s3g::sample::kSampleNeonSlotCount
        * s3g::sample::kSampleNeonSavedLoopCount> loopEnds {};
    std::array<uint8_t, s3g::sample::kSampleNeonSlotCount
        * s3g::sample::kSampleNeonSavedLoopCount> loopEnabled {};
    std::array<double, s3g::sample::kSampleNeonSlotCount> editPositions {};
    for (std::size_t slot = 0u;
         slot < s3g::sample::kSampleNeonSlotCount; ++slot) {
        const uint8_t count = std::clamp<uint8_t>(counts[slot], 1u,
            static_cast<uint8_t>(s3g::sample::kSampleNeonSliceCount));
        for (std::size_t marker = 0u; marker <= count; ++marker)
            boundaries[slot * (s3g::sample::kSampleNeonSliceCount + 1u)
                + marker] = static_cast<double>(marker)
                    / static_cast<double>(count);
        for (std::size_t cue = 0u;
             cue < s3g::sample::kSampleNeonCueCount; ++cue)
            cuePositions[slot * s3g::sample::kSampleNeonCueCount + cue]
                = static_cast<double>(cue)
                    / static_cast<double>(s3g::sample::kSampleNeonCueCount);
        for (std::size_t loop = 0u;
             loop < s3g::sample::kSampleNeonSavedLoopCount; ++loop) {
            const std::size_t flat = slot
                * s3g::sample::kSampleNeonSavedLoopCount + loop;
            loopStarts[flat] = static_cast<double>(loop)
                / static_cast<double>(s3g::sample::kSampleNeonSavedLoopCount);
            loopEnds[flat] = std::min(1.0, loopStarts[flat]
                + std::pow(0.5, static_cast<double>(loop)));
        }
    }
    std::array<uint8_t, kFlipPatternCount * kFlipMaximumSteps> flipSteps {};
    flipSteps.fill(0xffu);
    std::array<uint8_t, kFlipPatternCount * kFlipMaximumSteps> flipVelocity {};
    flipVelocity.fill(127u);
    std::array<uint8_t, kFlipPatternCount> flipCounts {};
    std::array<uint8_t, 4u> flipState {{ 0u, 0u, 0u, 1u }};
    std::array<uint8_t, s3g::sample::kSampleNeonSlotCount> domains {};
    domains.fill(2u);
    std::array<uint8_t, s3g::sample::kSampleNeonSlotCount> quantize {};
    quantize.fill(1u);
    std::array<uint8_t, s3g::sample::kSampleNeonSlotCount> textures {};
    std::array<uint8_t, s3g::sample::kSampleNeonSlotCount> motionPaths {};
    std::array<float, s3g::sample::kSampleNeonSlotCount> motionRates {};
    motionRates.fill(8.0f);
    std::array<float, s3g::sample::kSampleNeonSlotCount> motionLoci {};
    motionLoci.fill(0.5f);
    std::array<float, s3g::sample::kSampleNeonSlotCount> motionFields {};
    motionFields.fill(0.5f);
    std::array<float, s3g::sample::kSampleNeonSlotCount> lanePositions {};
    std::array<float, s3g::sample::kSampleNeonSlotCount> laneDepths {};
    std::array<float, s3g::sample::kSampleNeonSlotCount> grainDensities {};
    grainDensities.fill(12.0f);
    std::array<float, s3g::sample::kSampleNeonSlotCount> grainSizes {};
    grainSizes.fill(80.0f);
    std::array<float, s3g::sample::kSampleNeonSlotCount> grainPositions {};
    grainPositions.fill(0.5f);
    std::array<float, s3g::sample::kSampleNeonSlotCount> grainSprays {};
    grainSprays.fill(0.15f);
    std::array<float, s3g::sample::kSampleNeonSlotCount> grainPitches {};
    std::array<float, s3g::sample::kSampleNeonSlotCount> grainReverses {};
    const auto readArray = [stream](auto& target) {
        return s3g::clap_state::readAll(stream, target.data(),
            target.size() * sizeof(target[0u]));
    };
    if ((!readArray(slotOptions) || !readArray(chokeGroups)
            || !readArray(chopModes) || !readArray(beatDivisions)
            || !readArray(sourceBpms) || !readArray(boundaries)
            || !readArray(cuePositions) || !readArray(cueEnabled)
            || !readArray(loopStarts) || !readArray(loopEnds)
            || !readArray(loopEnabled) || !readArray(editPositions)
            || !readArray(flipSteps) || !readArray(flipVelocity)
            || !readArray(flipCounts) || !readArray(flipState)))
        return false;
    if ((!readArray(domains) || !readArray(quantize))) return false;
    if ((!readArray(textures) || !readArray(motionPaths)
            || !readArray(motionRates) || !readArray(motionLoci)
            || !readArray(motionFields) || !readArray(lanePositions)
            || !readArray(laneDepths) || !readArray(grainDensities)
            || !readArray(grainSizes) || !readArray(grainPositions)
            || !readArray(grainSprays) || !readArray(grainPitches)
            || !readArray(grainReverses))) return false;
    std::array<float, s3g::sample::kSampleNeonSlotCount> preRollMs {};
    auto transientLimits = counts;
    if (header.version >= 11u
        && (!readArray(preRollMs) || !readArray(transientLimits))) return false;
    std::array<uint8_t, 32u> clocks {};
    std::array<float, 32u> motionSeconds {}, shotSeconds {}, grainIntervals {};
    motionSeconds.fill(4.0f); shotSeconds.fill(1.0f); grainIntervals.fill(0.25f);
    if (header.version >= 12u && (!readArray(clocks) || !readArray(motionSeconds)
        || !readArray(shotSeconds) || !readArray(grainIntervals))) return false;
    std::array<float, 32u * 36u> modern {};
    for (unsigned slot = 0u; slot < 32u; ++slot) {
        for (unsigned effect = 0u; effect < 8u; ++effect)
            for (unsigned n = 0u; n < 3u; ++n) modern[slot * 36u + effect * 3u + n] = s3g::sample::kSampleNeonFxDefaults[effect][n];
        for (unsigned method = 0u; method < 3u; ++method) {
            modern[slot * 36u + 24u + method * 4u] = 0.35f;
            modern[slot * 36u + 27u + method * 4u] = 1.0f;
        }
    }
    if (header.version >= 13u && !readArray(modern)) return false;
    std::array<float, 64u> envelopes;
    envelopes.fill(0.005f);
    if (header.version >= 14u && !readArray(envelopes)) return false;
    const auto finite = [](const auto& data) {
        return std::all_of(data.begin(), data.end(), [](auto value) { return std::isfinite(value); });
    };
    if (!finite(envelopes) || !finite(modern) || !finite(values) || !finite(sourceBpms) || !finite(boundaries)
        || !finite(editPositions) || !finite(cuePositions)
        || !finite(loopStarts) || !finite(loopEnds) || !finite(motionRates)
        || !finite(motionLoci) || !finite(motionFields) || !finite(lanePositions)
        || !finite(laneDepths) || !finite(grainDensities) || !finite(grainSizes)
        || !finite(grainPositions) || !finite(grainSprays)
        || !finite(grainPitches) || !finite(grainReverses)
        || !finite(preRollMs) || !finite(motionSeconds)
        || !finite(shotSeconds) || !finite(grainIntervals)) return false;
    std::array<double, 5u> captureSettings {};
    std::array<std::shared_ptr<const SampleAsset>, 33u> embedded {};
    if (!readArray(captureSettings) || !finite(captureSettings)
        || captureSettings[0u] < 0.0 || captureSettings[1u] > 1.0
        || captureSettings[0u] >= captureSettings[1u]
        || captureSettings[2u] < 0.0 || captureSettings[2u] > 6.0
        || std::floor(captureSettings[2u]) != captureSettings[2u]
        || (captureSettings[3u] != 0.0 && captureSettings[3u] != 1.0)
        || std::floor(captureSettings[4u]) != captureSettings[4u]
        || (captureSettings[4u] != 255.0 && (captureSettings[4u] < 0.0 || captureSettings[4u] > 31.0))
        || !readEmbeddedAudio(stream, embedded)) return false;
    // An active take owns its buffers until Stop; reject restore atomically.
    if (instance.captureState.load() == Plugin::CaptureState::Recording
        || instance.captureState.load() == Plugin::CaptureState::Ready) return false;
    // Embedded slices are sources in their own right, including after recall.
    // Rebuild their transient analysis before changing the current set.
    std::array<s3g::sample::CutupsLaneMetadata, 32u> embeddedAnalyses {};
    try {
        for (std::size_t slot = 0u; slot < embeddedAnalyses.size(); ++slot)
            if (embedded[slot]) embeddedAnalyses[slot] = s3g::sample::analyzeCutupsAsset(
                *embedded[slot], 32u, 5.0, 1000u, 20.0);
    } catch (...) { return false; }
    if (header.version < 13u) values[kGlobalMangleParamId - kOutputLayoutParamId] = 0.0;
    for (std::size_t index = 0u; index < values.size(); ++index) {
        std::size_t stored = 0u;
        const ParamDef* definition = nullptr;
        parameterLocation(parameterIdAt(index), stored, definition);
        instance.parameters[index].store(clampParam(*definition, values[index]));
    }
    std::size_t modeIndex = 0u;
    for (std::size_t slot = 0u; slot < instance.sliceTriggers.size(); ++slot)
        for (std::size_t slice = 0u;
             slice < instance.sliceTriggers[slot].size(); ++slice) {
            const uint8_t stored = modes[modeIndex];
            uint8_t trigger = std::min<uint8_t>(stored, 2u);
            uint8_t sliceOption = options[modeIndex];
            instance.sliceTriggers[slot][slice].store(trigger,
                std::memory_order_release);
            instance.sliceOptions[slot][slice].store(static_cast<uint8_t>(
                sliceOption & (kSliceRepeatOption | kSliceSyncOption)),
                std::memory_order_release);
            ++modeIndex;
        }
    for (std::size_t slot = 0u; slot < counts.size(); ++slot)
        instance.sliceCounts[slot].store(std::clamp<uint8_t>(
            counts[slot], 1u,
            static_cast<uint8_t>(s3g::sample::kSampleNeonSliceCount)),
            std::memory_order_release);
    for (std::size_t slot = 0u; slot < counts.size(); ++slot) {
        for (unsigned effect = 0u; effect < 8u; ++effect)
            for (unsigned n = 0u; n < 3u; ++n) instance.fxParameters[slot][effect][n].store(std::clamp(modern[slot * 36u + effect * 3u + n], 0.0f, 1.0f));
        for (unsigned method = 0u; method < 3u; ++method)
            for (unsigned n = 0u; n < 4u; ++n) instance.techniqueParameters[slot][method][n].store(std::clamp(modern[slot * 36u + 24u + method * 4u + n], 0.0f, 1.0f));
        if (header.version < 13u) {
            instance.parameters[kGlobalParamCount + slot * kSlotParameterCount + kSlotCharacter].store(0.0);
            instance.parameters[kGlobalParamCount + slot * kSlotParameterCount + kSlotMangle].store(0.0);
        }
        instance.slotOptions[slot].store(static_cast<uint8_t>(
            slotOptions[slot] & (kSlotRepeatOption | kSlotSyncOption
                | kSlotVelocityOption)), std::memory_order_release);
        instance.chokeGroups[slot].store(std::min<uint8_t>(
            chokeGroups[slot], 4u), std::memory_order_release);
        instance.chopModes[slot].store(std::min<uint8_t>(
            chopModes[slot], 3u), std::memory_order_release);
        instance.chopBeatDivisions[slot].store(std::min<uint8_t>(
            beatDivisions[slot], 4u), std::memory_order_release);
        const double sourceBpm = std::isfinite(sourceBpms[slot])
            ? sourceBpms[slot] : 120.0;
        instance.sourceBpms[slot].store(std::clamp(
            sourceBpm, 20.0, 999.0), std::memory_order_release);
        instance.transientPreRollMs[slot].store(std::clamp(preRollMs[slot], 0.0f, 50.0f));
        instance.transientSliceLimits[slot].store(std::clamp<uint8_t>(transientLimits[slot], 1u, 32u));
        instance.playbackClocks[slot].store(std::min<uint8_t>(clocks[slot], 1u));
        instance.motionSeconds[slot].store(std::clamp(motionSeconds[slot], 0.05f, 30.0f));
        instance.shotSeconds[slot].store(std::clamp(shotSeconds[slot], 0.05f, 30.0f));
        instance.techniqueAttackSeconds[slot].store(std::clamp(envelopes[slot * 2u], 0.001f, 10.0f));
        instance.techniqueReleaseSeconds[slot].store(std::clamp(envelopes[slot * 2u + 1u], 0.001f, 10.0f));
        instance.grainIntervals[slot].store(std::clamp(grainIntervals[slot], 0.03125f, 4.0f));
        instance.parameters[kGlobalParamCount + slot * kSlotParameterCount + kSlotMute].store(0.0);
        instance.parameters[kGlobalParamCount + slot * kSlotParameterCount + kSlotSolo].store(0.0);
        uint8_t stageOptions = normalizedStageOptions(textures[slot]);
        if (paramValue(instance, slotParamId(slot, kSlotSourceFormat)) >= 0.5) {
            if (stageOptions == kWavesetsOption) stageOptions = 0u;
            if (paramValue(instance, slotParamId(slot, kSlotCharacter)) >= 6.0) {
                instance.parameters[kGlobalParamCount + slot * kSlotParameterCount + kSlotCharacter].store(0.0);
                instance.parameters[kGlobalParamCount + slot * kSlotParameterCount + kSlotMangle].store(0.0);
            }
        }
        double launchPosition = editPositions[slot];
        instance.editPositions[slot].store(std::clamp(
            launchPosition, 0.0, 1.0), std::memory_order_release);
        instance.slicerDomainIndices[slot].store(std::min<uint8_t>(
            domains[slot], 5u), std::memory_order_release);
        instance.slicerQuantizeIndices[slot].store(std::min<uint8_t>(
            quantize[slot], 3u), std::memory_order_release);
        instance.textureOptions[slot].store(stageOptions,
            std::memory_order_release);
        instance.motionPaths[slot].store(std::min<uint8_t>(
            motionPaths[slot], 3u), std::memory_order_release);
        float motionCycle = std::isfinite(motionRates[slot])
            ? motionRates[slot] : 8.0f;
        instance.motionRates[slot].store(kMotionCycleBeats[
            nearestMotionCycleIndex(motionCycle)],
            std::memory_order_release);
        instance.motionLoci[slot].store(std::clamp(
            std::isfinite(motionLoci[slot]) ? motionLoci[slot] : 0.5f,
            0.0f, 1.0f), std::memory_order_release);
        instance.motionFields[slot].store(std::clamp(
            std::isfinite(motionFields[slot]) ? motionFields[slot] : 0.5f,
            0.0f, 1.0f), std::memory_order_release);
        instance.lanePositions[slot].store(std::clamp(
            std::isfinite(lanePositions[slot]) ? lanePositions[slot] : 0.0f,
            0.0f, 3.0f), std::memory_order_release);
        instance.laneMotionDepths[slot].store(std::clamp(
            std::isfinite(laneDepths[slot]) ? laneDepths[slot] : 0.0f,
            0.0f, 3.0f), std::memory_order_release);
        instance.grainDensities[slot].store(std::clamp(
            std::isfinite(grainDensities[slot])
                ? grainDensities[slot] : 12.0f,
            1.0f, 80.0f), std::memory_order_release);
        instance.grainSizes[slot].store(std::clamp(
            std::isfinite(grainSizes[slot]) ? grainSizes[slot] : 80.0f,
            5.0f, 500.0f), std::memory_order_release);
        instance.grainPositions[slot].store(std::clamp(
            std::isfinite(grainPositions[slot])
                ? grainPositions[slot] : 0.5f,
            0.0f, 1.0f), std::memory_order_release);
        instance.grainSprays[slot].store(std::clamp(
            std::isfinite(grainSprays[slot]) ? grainSprays[slot] : 0.15f,
            0.0f, 1.0f), std::memory_order_release);
        instance.grainPitchSprays[slot].store(std::clamp(
            std::isfinite(grainPitches[slot]) ? grainPitches[slot] : 0.0f,
            0.0f, 24.0f), std::memory_order_release);
        instance.grainReverseChances[slot].store(std::clamp(
            std::isfinite(grainReverses[slot]) ? grainReverses[slot] : 0.0f,
            0.0f, 1.0f), std::memory_order_release);
        for (std::size_t marker = 0u;
             marker <= s3g::sample::kSampleNeonSliceCount; ++marker)
            instance.sliceBoundaries[slot][marker].store(
                boundaries[slot
                    * (s3g::sample::kSampleNeonSliceCount + 1u) + marker],
                std::memory_order_release);
        for (std::size_t cue = 0u;
             cue < s3g::sample::kSampleNeonCueCount; ++cue) {
            const std::size_t flat = slot
                * s3g::sample::kSampleNeonCueCount + cue;
            instance.hotCuePositions[slot][cue].store(std::clamp(
                cuePositions[flat], 0.0, 1.0), std::memory_order_release);
            instance.hotCueEnabled[slot][cue].store(
                cueEnabled[flat] ? 1u : 0u, std::memory_order_release);
        }
        for (std::size_t loop = 0u;
             loop < s3g::sample::kSampleNeonSavedLoopCount; ++loop) {
            const std::size_t flat = slot
                * s3g::sample::kSampleNeonSavedLoopCount + loop;
            const double start = std::clamp(loopStarts[flat], 0.0, 1.0);
            instance.savedLoopStarts[slot][loop].store(start,
                std::memory_order_release);
            instance.savedLoopEnds[slot][loop].store(std::clamp(
                loopEnds[flat], start, 1.0), std::memory_order_release);
            instance.savedLoopEnabled[slot][loop].store(
                loopEnabled[flat] ? 1u : 0u, std::memory_order_release);
        }
    }
    for (std::size_t pattern = 0u; pattern < kFlipPatternCount; ++pattern) {
        instance.flipStepCounts[pattern].store(std::min<uint8_t>(
            flipCounts[pattern], static_cast<uint8_t>(kFlipMaximumSteps)),
            std::memory_order_release);
        for (std::size_t step = 0u; step < kFlipMaximumSteps; ++step) {
            const std::size_t flat = pattern * kFlipMaximumSteps + step;
            instance.flipSteps[pattern][step].store(
                flipSteps[flat] < s3g::sample::kSampleNeonCueCount
                    ? flipSteps[flat] : 0xffu,
                std::memory_order_release);
            instance.flipVelocities[pattern][step].store(
                flipVelocity[flat], std::memory_order_release);
        }
    }
    instance.selectedFlipPattern.store(std::min<uint8_t>(
        flipState[0u], static_cast<uint8_t>(kFlipPatternCount - 1u)),
        std::memory_order_release);
    instance.flipRecording.store(flipState[1u] != 0u,
        std::memory_order_release);
    instance.flipPlaying.store(flipState[2u] != 0u,
        std::memory_order_release);
    instance.flipLooping.store(flipState[3u] != 0u,
        std::memory_order_release);
    for (std::size_t slot = 0u; slot < paths.size(); ++slot) {
        paths[slot].back() = '\0';
#if defined(S3G_SAMPLE_FILE_WORKER)
        ++instance.loadGenerations[slot]; // Also invalidate loads into now-empty cells.
#endif
        (void)publishAsset(instance, slot, nullptr, "", false);
        instance.embeddedAssets[slot] = static_cast<bool>(embedded[slot]);
        {
            std::lock_guard<std::mutex> lock(instance.statusMutex);
            instance.slotAnalyses[slot] = embeddedAnalyses[slot];
        }
        if (embedded[slot]) {
            publishAsset(instance, slot, embedded[slot], "", false);
            continue;
        }
#if defined(S3G_SAMPLE_FILE_WORKER)
        if (paths[slot][0] != '\0')
            queueSampleLoad(instance, slot, paths[slot].data(), false);
#endif
    }
    instance.sendNeonInitialization.store(true, std::memory_order_release);
    instance.ledFeedbackDirty.store(true, std::memory_order_release);
    instance.captureAsset = embedded[32u];
    instance.resetNeonVelocity.store(true, std::memory_order_release);
    if (instance.captureAsset) instance.retainedAssets.push_back(instance.captureAsset);
    instance.publishedCapture.store(instance.captureAsset.get());
    instance.captureState.store(instance.captureAsset ? Plugin::CaptureState::Review : Plugin::CaptureState::Empty);
    instance.captureStart.store(captureSettings[0u]); instance.captureEnd.store(captureSettings[1u]);
    instance.captureLayout.store(static_cast<uint8_t>(captureSettings[2u]));
    instance.captureZeroCross.store(captureSettings[3u] >= 0.5);
    instance.captureTarget.store(static_cast<uint8_t>(captureSettings[4u]));
    instance.captureFrames.store(instance.captureAsset ? instance.captureAsset->frameCount() : 0u);
    instance.captureCommand.store(0);
    requestProcess(instance);
    return true;
}

const clap_plugin_state_t stateExtension { stateSave, stateLoad };

uint32_t audioPortsCount(const clap_plugin_t*, bool isInput)
{
    return isInput ? 0u : 1u;
}

bool audioPortsGet(const clap_plugin_t* plugin, uint32_t index,
    bool isInput, clap_audio_port_info_t* info)
{
    if (!info || isInput || index != 0u) return false;
    const auto& instance = *self(plugin);
    *info = {};
    info->id = 20u;
    std::snprintf(info->name, sizeof(info->name), "%s",
        "Neon 32 Out");
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = instance.outputChannels;
    info->port_type = nullptr;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}

const clap_plugin_audio_ports_t audioPortsExtension {
    audioPortsCount, audioPortsGet,
};

uint32_t notePortsCount(const clap_plugin_t*, bool isInput)
{
#if defined(__APPLE__)
    // LEDs go only to CoreMIDI on Mac. Exposing them as musical output lets
    // host hardware sends duplicate them or feed them back as pad strikes.
    return isInput ? 1u : 0u;
#else
    (void)isInput;
    return 1u;
#endif
}

bool notePortsGet(const clap_plugin_t*, uint32_t index, bool isInput,
    clap_note_port_info_t* info)
{
    if (!info || index != 0u) return false;
#if defined(__APPLE__)
    if (!isInput) return false;
#endif
    *info = {};
    info->id = isInput ? 30u : 31u;
    info->supported_dialects = isInput
        ? CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI
        : CLAP_NOTE_DIALECT_MIDI;
    info->preferred_dialect = CLAP_NOTE_DIALECT_MIDI;
    std::snprintf(info->name, sizeof(info->name), "%s",
        isInput ? "Neon + MIDI In" : "Neon LED Out");
    return true;
}

const clap_plugin_note_ports_t notePortsExtension {
    notePortsCount, notePortsGet,
};

uint32_t noteNameCount(const clap_plugin_t*)
{
    return static_cast<uint32_t>(s3g::sample::kSampleNeonSlotCount);
}

bool noteNameGet(const clap_plugin_t* plugin, uint32_t index,
    clap_note_name_t* info)
{
    if (!info || index >= s3g::sample::kSampleNeonSlotCount) return false;
    *info = {};
    info->port = 0;
    info->channel = -1;
    info->key = static_cast<int16_t>(std::lround(paramValue(*self(plugin),
        kBaseNoteParamId)) + index);
    std::snprintf(info->name, sizeof(info->name), "BANK %c PAD %u",
        static_cast<char>('A' + index / 8u), index % 8u + 1u);
    return true;
}

const clap_plugin_note_name_t noteNameExtension {
    noteNameCount, noteNameGet,
};

bool pushParamOutput(const clap_output_events_t* output, clap_id id,
    double value, uint32_t time) noexcept
{
    if (!output || !output->try_push) return false;
    clap_event_param_value_t event {};
    event.header.size = sizeof(event);
    event.header.time = time;
    event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    event.header.type = CLAP_EVENT_PARAM_VALUE;
    event.header.flags = CLAP_EVENT_IS_LIVE;
    event.param_id = id;
    event.note_id = -1;
    event.port_index = -1;
    event.channel = -1;
    event.key = -1;
    event.value = value;
    return output->try_push(output, &event.header);
}

void setControllerParam(Plugin& instance, clap_id id, double value,
    uint32_t time, const clap_output_events_t* output) noexcept
{
    std::size_t index = 0u;
    const ParamDef* definition = nullptr;
    if (!parameterLocation(id, index, definition)) return;
    value = clampParam(*definition, value);
    setParam(instance, id, value);
    (void)pushParamOutput(output, id, paramValue(instance, id), time);
}

void handleEncoder(Plugin& instance, const NeonAction& action,
    uint32_t time, const clap_output_events_t* output) noexcept
{
    const std::size_t selectedSlot = instance.selectedSlot.load(
        std::memory_order_relaxed);
    const uint8_t visibleMode = instance.visibleMode.load(
        std::memory_order_relaxed);
    const bool slicing = visibleMode == static_cast<uint8_t>(NeonMode::Slicer);
    const bool sampling = visibleMode == static_cast<uint8_t>(NeonMode::Sampler);
    const bool capturing = visibleMode == static_cast<uint8_t>(NeonMode::HotLoop);
    const bool editing = visibleMode == static_cast<uint8_t>(NeonMode::HotCue);
    if (capturing) {
        if (action.type == NeonActionType::EncoderTurn) {
            if (action.encoder == NeonEncoder::Loop)
                instance.captureCursor.store(std::clamp(instance.captureCursor.load()
                    + action.delta * (action.shifted ? 0.001 : 0.01) / instance.captureZoom.load(), 0.0, 1.0));
            else instance.captureZoom.store(std::clamp(instance.captureZoom.load()
                * std::pow(1.12f, static_cast<float>(action.delta)), 1.0f, 32.0f));
        } else if (action.pressed) {
            if (action.encoder == NeonEncoder::Loop)
                setCaptureTrim(instance, action.shifted, instance.captureCursor.load());
            else instance.captureZoom.store(1.0f);
        }
        return;
    }
    if (sampling && action.type == NeonActionType::EncoderTurn) {
        const auto id = action.encoder == NeonEncoder::Loop
            ? slotParamId(selectedSlot, kSlotGain) : kGlobalMangleParamId;
        setControllerParam(instance, id, paramValue(instance, id)
            + action.delta * (action.encoder == NeonEncoder::Loop ? (action.shifted ? 0.1 : 0.5)
                : action.shifted ? 0.001 : 0.01), time, output);
        return;
    }
    if (sampling) return; // PLAY never silently changes trim or markers.
    if (editing && effectiveEditPage(instance) >= 4u) {
        const bool loop = action.encoder == NeonEncoder::Loop;
        if (action.type == NeonActionType::EncoderPush) {
            if (action.pressed && loop) auditionEditedSlot(instance, selectedSlot);
            else if (action.pressed && effectiveEditPage(instance) == 7u) instance.fxPair.fetch_xor(1u);
        } else {
            const auto page = effectiveEditPage(instance);
            double step = action.shifted ? 0.001 : 0.01;
            if ((page == 4u && !loop) || (loop && instance.playbackClocks[selectedSlot].load() && (page == 4u || page == 5u))) step = 1.0 / 7.0;
            if (page == 6u) step = loop ? 1.0 / 31.0 : 1.0 / 15.0;
            if (page == 7u && !instance.fxPair.load()) {
                const auto effect = static_cast<unsigned>(paramValue(instance, slotParamId(selectedSlot, kSlotCharacter)));
                if ((effect == 0u && loop) || (effect == 3u && !loop)) step = 0.5;
            }
            setAdvancedKnob(instance, loop, advancedKnobValue(instance, loop) + action.delta * step);
        }
        return;
    }
    if (editing && effectiveEditPage(instance) >= 2u) {
        const auto page = effectiveEditPage(instance);
        if (action.type == NeonActionType::EncoderPush) {
            if (action.pressed && action.encoder == NeonEncoder::Loop)
                auditionEditedSlot(instance, selectedSlot);
            return; // Preview only; never secretly enable/disable a technique.
        }
        if (page == 2u) {
            if (action.encoder == NeonEncoder::Loop)
                instance.editPositions[selectedSlot].store(std::clamp(instance.editPositions[selectedSlot].load()
                    + action.delta * (action.shifted ? 0.001 : 0.01), 0.0, 1.0));
            else if (instance.playbackClocks[selectedSlot].load()) {
                const auto cycle = nearestMotionCycleIndex(instance.motionRates[selectedSlot].load());
                instance.motionRates[selectedSlot].store(kMotionCycleBeats[static_cast<std::size_t>(
                    std::clamp(static_cast<int>(cycle) + action.delta, 0, 7))]);
            } else instance.motionSeconds[selectedSlot].store(std::clamp(instance.motionSeconds[selectedSlot].load()
                + action.delta * (action.shifted ? 0.01f : 0.1f), 0.05f, 30.0f));
        } else {
            if (action.encoder == NeonEncoder::Trax && instance.playbackClocks[selectedSlot].load()) {
                const auto index = nearestMotionCycleIndex(instance.grainIntervals[selectedSlot].load() * 8.0f);
                instance.grainIntervals[selectedSlot].store(kMotionCycleBeats[static_cast<std::size_t>(
                    std::clamp(static_cast<int>(index) + action.delta, 0, 7))] / 8.0f);
            } else {
                auto& target = action.encoder == NeonEncoder::Loop
                    ? instance.grainSizes[selectedSlot] : instance.grainDensities[selectedSlot];
                target.store(std::clamp(target.load() + action.delta * (action.shifted ? 0.1f : 1.0f),
                    action.encoder == NeonEncoder::Loop ? 5.0f : 1.0f,
                    action.encoder == NeonEncoder::Loop ? 500.0f : 80.0f));
            }
        }
        markStateDirty(instance); requestProcess(instance);
        return;
    }
    if (action.type == NeonActionType::EncoderTurn) {
        if (action.encoder == NeonEncoder::Loop) {
            const double current = instance.editPositions[selectedSlot].load(
                std::memory_order_relaxed);
            const double step = (action.shifted ? 0.001 : 0.01)
                / instance.waveformZooms[selectedSlot].load();
            instance.editPositions[selectedSlot].store(std::clamp(
                current + static_cast<double>(action.delta) * step,
                0.0, 1.0), std::memory_order_release);
            markStateDirty(instance);
            requestProcess(instance);
            return;
        }
        if (action.encoder == NeonEncoder::Trax && !action.shifted) {
            const float current = instance.waveformZooms[selectedSlot].load(
                std::memory_order_relaxed);
            instance.waveformZooms[selectedSlot].store(std::clamp(
                current * std::pow(1.12f, static_cast<float>(action.delta)),
                1.0f, 32.0f), std::memory_order_release);
            requestProcess(instance);
            return;
        }
        setControllerParam(instance, kGlobalMangleParamId,
            paramValue(instance, kGlobalMangleParamId)
                + 0.015 * action.delta, time, output);
    } else if (action.type == NeonActionType::EncoderPush
        && action.pressed) {
        if (action.encoder == NeonEncoder::Loop && slicing) {
            const double cursor = instance.editPositions[selectedSlot].load(
                std::memory_order_relaxed);
            if (action.shifted)
                removeNearestSliceMarker(instance, selectedSlot, cursor);
            else addLiveSliceMarker(instance, selectedSlot, cursor);
            return;
        }
        if (action.encoder == NeonEncoder::Loop && editing) {
            const double start = paramValue(instance,
                slotParamId(selectedSlot, kSlotStart));
            const double end = paramValue(instance,
                slotParamId(selectedSlot, kSlotEnd));
            const double cursor = instance.editPositions[selectedSlot].load(
                std::memory_order_relaxed);
            const double absolute = start + (end - start) * cursor;
            const clap_id id = slotParamId(selectedSlot,
                action.shifted ? kSlotEnd : kSlotStart);
            setControllerParam(instance, id, absolute, time, output);
            instance.editPositions[selectedSlot].store(
                action.shifted ? 1.0 : 0.0, std::memory_order_release);
            markStateDirty(instance);
            requestProcess(instance);
            return;
        }
        if (action.encoder == NeonEncoder::Trax) {
            if (action.shifted) {
                const double start = paramValue(instance,
                    slotParamId(selectedSlot, kSlotStart));
                const double end = paramValue(instance,
                    slotParamId(selectedSlot, kSlotEnd));
                double focusWidth = std::max(1.0e-6, end - start);
                if (!slicing) {
                    instance.editPositions[selectedSlot].store(0.5,
                        std::memory_order_release);
                } else {
                    const auto layout = sliceLayout(instance, selectedSlot);
                    const uint8_t slice = std::min<uint8_t>(
                        instance.visibleSlice.load(std::memory_order_relaxed),
                        static_cast<uint8_t>(layout.sliceCount - 1u));
                    instance.editPositions[selectedSlot].store(
                        0.5 * (layout.boundaries[slice]
                            + layout.boundaries[slice + 1u]),
                        std::memory_order_release);
                    focusWidth *= std::max(1.0e-6,
                        layout.boundaries[slice + 1u]
                            - layout.boundaries[slice]);
                }
                instance.waveformZooms[selectedSlot].store(static_cast<float>(
                    std::clamp(1.0 / focusWidth, 1.0, 32.0)),
                    std::memory_order_release);
            } else {
                instance.waveformZooms[selectedSlot].store(1.0f,
                    std::memory_order_release);
            }
            requestProcess(instance);
        }
    }
}

std::size_t collectEvents(Plugin& instance,
    const clap_input_events_t* input, const clap_output_events_t* output,
    uint32_t frameCount) noexcept
{
    std::size_t count = 0u;
    const auto append = [&](uint32_t frame, SampleNeonEventKind kind,
                            uint64_t noteId, uint8_t slot, NeonMode mode,
                            uint8_t performanceIndex, float value,
                            bool shifted = false, bool reverse = false,
                            bool loopedSlicer = false,
                            bool alternate = false) {
        if (count >= instance.blockEvents.size()
            || slot >= s3g::sample::kSampleNeonSlotCount) return;
        instance.blockEvents[count++] = {
            std::min(frame, frameCount), kind, noteId, slot, mode,
            performanceIndex, value, shifted, reverse, loopedSlicer,
            alternate,
        };
    };

    const auto fx = instance.pendingGuiFx.exchange(UINT32_MAX);
    if (fx != UINT32_MAX)
        append(0u, SampleNeonEventKind::Pressure, 0u, static_cast<uint8_t>(fx >> 1u),
            NeonMode::Sampler, 0u, (fx & 1u) ? 1.0f : 0.0f);

    uint32_t auditions = instance.pendingAuditions.exchange(
        0u, std::memory_order_acq_rel);
    const uint32_t editAuditions = instance.pendingEditAuditions.exchange(0u);
    const uint32_t stops = instance.pendingCellStops.exchange(0u);
    for (uint8_t slot = 0u; slot < s3g::sample::kSampleNeonSlotCount;
        ++slot) {
        if ((editAuditions | stops) & (1u << slot))
            append(0u, SampleNeonEventKind::Choke, 0u, slot, NeonMode::Sampler, 0u, 0.0f);
        if (((auditions | editAuditions) & ~stops & (1u << slot)) != 0u)
            append(0u, SampleNeonEventKind::Trigger,
                0x70000000u + slot, slot, NeonMode::Sampler, 0u, 1.0f);
    }
    const auto releases = instance.pendingGuiReleases.exchange(0u);
    for (uint8_t slot = 0u; slot < s3g::sample::kSampleNeonSlotCount; ++slot)
        if (releases & (1u << slot))
            append(0u, SampleNeonEventKind::Release, 0x70000000u + slot,
                slot, NeonMode::Sampler, 0u, 0.0f);
    const uint32_t sliceAudition = instance.pendingSliceAudition.exchange(
        0xffffffffu, std::memory_order_acq_rel);
    if (sliceAudition != 0xffffffffu) {
        const uint8_t slot = static_cast<uint8_t>(sliceAudition >> 8u);
        const uint8_t slice = static_cast<uint8_t>(sliceAudition & 0xffu);
        append(0u, SampleNeonEventKind::Trigger,
            0x71000000u + static_cast<uint64_t>(slot) * 32u + slice,
            slot, NeonMode::HotCue, slice, 1.0f);
    }
    const uint32_t performanceAudition =
        instance.pendingPerformanceAudition.exchange(
            0xffffffffu, std::memory_order_acq_rel);
    if (performanceAudition != 0xffffffffu) {
        const uint8_t slot = static_cast<uint8_t>(
            performanceAudition >> 16u);
        const auto mode = static_cast<NeonMode>(
            (performanceAudition >> 8u) & 0xffu);
        const uint8_t encodedPerformance = static_cast<uint8_t>(
            performanceAudition & 0xffu);
        const uint8_t performanceIndex = static_cast<uint8_t>(
            encodedPerformance & 0x1fu);
        const bool alternate = (encodedPerformance & 0x80u) != 0u;
        append(0u, SampleNeonEventKind::Trigger,
            0x73000000u + static_cast<uint64_t>(slot) * 8u
                + performanceIndex,
            slot, mode, performanceIndex, 1.0f,
            false, false, false, alternate);
    }
    if (!input || !input->size || !input->get) return count;

    const uint32_t baseNote = static_cast<uint32_t>(std::lround(
        paramValue(instance, kBaseNoteParamId)));
    const int receiveChannel = static_cast<int>(std::lround(
        paramValue(instance, kMidiReceiveParamId)));
    const bool neonActive = paramValue(instance, kNeonActiveParamId) >= 0.5;
    const uint32_t inputCount = input->size(input);
    for (uint32_t index = 0u; index < inputCount; ++index) {
        const auto* header = input->get(input, index);
        if (!header || header->space_id != CLAP_CORE_EVENT_SPACE_ID)
            continue;
        clap_event_midi_t bridgedMidi {};
        bool bridgedControl = false;
        if (header->type == CLAP_EVENT_MIDI_SYSEX
            && header->size >= sizeof(clap_event_midi_sysex_t)) {
            const auto* sysex = reinterpret_cast<const clap_event_midi_sysex_t*>(header);
            s3g::controller::neon_midi::BridgeMessage message;
            if (!neonActive || sysex->port_index != 0u
                || !s3g::controller::neon_midi::decodeBridge(sysex->buffer, sysex->size, message)) continue;
            using BridgeKind = s3g::controller::neon_midi::BridgeKind;
            // Adapter owns the performance bank; CHOP has its own slice bank.
            // No MIDI return path is needed or created.
            instance.cellBank.store(message.bank);
            if (instance.visibleMode.load() != static_cast<uint8_t>(NeonMode::Slicer))
                selectSurfaceBank(instance, message.bank);
            if (message.kind == BridgeKind::Sync) continue;
            if (message.kind == BridgeKind::SelectCell) {
                selectSurface(instance, NeonMode::Sampler, 0u);
                selectSurfaceBank(instance, message.bank);
                instance.selectedSlot.store(message.cell);
                continue; // Musical note already arrived through Tracker.
            }
            if (message.kind == BridgeKind::Pressure) {
                append(header->time, SampleNeonEventKind::Pressure,
                    0x60000000u + message.cell, message.cell, NeonMode::Sampler,
                    0u, static_cast<float>(message.midi.data2) / 127.0f);
                continue;
            }
            bridgedMidi.header = {sizeof(bridgedMidi), header->time,
                CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_MIDI, header->flags};
            bridgedMidi.port_index = 0u;
            bridgedMidi.data[0] = message.midi.status;
            bridgedMidi.data[1] = message.midi.data1;
            bridgedMidi.data[2] = message.midi.data2;
            header = &bridgedMidi.header;
            bridgedControl = true;
        }
        if (header->type == CLAP_EVENT_PARAM_VALUE
            && header->size >= sizeof(clap_event_param_value_t)) {
            const auto* event = reinterpret_cast<
                const clap_event_param_value_t*>(header);
            setParam(instance, event->param_id, event->value);
            continue;
        }
        if ((header->type == CLAP_EVENT_NOTE_ON
                || header->type == CLAP_EVENT_NOTE_OFF
                || header->type == CLAP_EVENT_NOTE_CHOKE)
            && header->size >= sizeof(clap_event_note_t)) {
            const auto* event = reinterpret_cast<const clap_event_note_t*>(
                header);
            if (event->key < static_cast<int16_t>(baseNote)
                || event->key >= static_cast<int16_t>(baseNote
                    + s3g::sample::kSampleNeonSlotCount)
                || event->channel < 0 || event->channel > 15
                || (receiveChannel != 0
                    && receiveChannel != event->channel + 1)) continue;
            const uint8_t slot = static_cast<uint8_t>(
                event->key - static_cast<int16_t>(baseNote));
            const auto kind = header->type == CLAP_EVENT_NOTE_ON
                ? SampleNeonEventKind::Trigger
                : header->type == CLAP_EVENT_NOTE_OFF
                    ? SampleNeonEventKind::Release
                    : SampleNeonEventKind::Choke;
            append(header->time, kind,
                static_cast<uint64_t>(event->note_id < 0
                    ? event->key : event->note_id), slot,
                NeonMode::Sampler, 0u,
                static_cast<float>(event->velocity));
            continue;
        }
        if (header->type == CLAP_EVENT_NOTE_EXPRESSION
            && header->size >= sizeof(clap_event_note_expression_t)) {
            const auto* event = reinterpret_cast<
                const clap_event_note_expression_t*>(header);
            if (event->expression_id == CLAP_NOTE_EXPRESSION_PRESSURE
                && event->key >= static_cast<int16_t>(baseNote)
                && event->key < static_cast<int16_t>(baseNote
                    + s3g::sample::kSampleNeonSlotCount)) {
                append(header->time, SampleNeonEventKind::Pressure,
                    static_cast<uint64_t>(event->note_id < 0
                        ? event->key : event->note_id),
                    static_cast<uint8_t>(event->key
                        - static_cast<int16_t>(baseNote)),
                    NeonMode::Sampler, 0u,
                    static_cast<float>(event->value));
            }
            continue;
        }
        if (header->type != CLAP_EVENT_MIDI
            || header->size < sizeof(clap_event_midi_t)) continue;
        const auto* event = reinterpret_cast<const clap_event_midi_t*>(
            header);
        if (event->port_index != 0u || header->time >= frameCount) continue;
        if (neonActive) {
            if (s3g::controller::reloop_neon::isStatusLedMessage({event->data[0], event->data[1], event->data[2]})) continue;
            const NeonAction action = instance.neonPadInput.process({
                event->data[0u], event->data[1u], event->data[2u] }, header->time);
            if (action) {
                if (action.type == NeonActionType::PadVelocity) continue;
                if (action.type == NeonActionType::EncoderTurn || action.type == NeonActionType::EncoderPush) {
                    handleEncoder(instance, action, header->time, output);
                    continue;
                }
                (void)instance.neonState.apply(action);
                if (action.type == NeonActionType::SelectMode && action.pressed) {
                    selectSurface(instance, action.mode, static_cast<uint8_t>(action.layer));
                    continue;
                }
                if (action.type == NeonActionType::SelectBank && action.pressed) {
                    selectSurfaceBank(instance, action.bank);
                    continue;
                }
                if ((action.type == NeonActionType::Pad || action.type == NeonActionType::PadPressure)
                    && action.pad < 8u) {
                    if (action.mode == NeonMode::HotLoop && action.shifted && action.pad == 0u) {
                        if (action.type == NeonActionType::Pad && action.pressed)
                            capturePadAction(instance, 0u, action.layer == s3g::controller::reloop_neon::Layer::Second, true);
                        continue; // REC pressure/release must not release a musical gesture.
                    }
                    auto& held = instance.heldGestures[action.pad];
                    if (action.type == NeonActionType::PadPressure || !action.pressed) {
                        // Releases belong to the original gesture, not whatever
                        // page/bank happens to be displayed now.
                        if (held.active && count < instance.blockEvents.size()) {
                            auto routed = held.event;
                            routed.frameOffset = header->time;
                            routed.kind = action.type == NeonActionType::PadPressure || held.pressureOnly
                                ? SampleNeonEventKind::Pressure : SampleNeonEventKind::Release;
                            routed.value = action.type == NeonActionType::PadPressure
                                ? static_cast<float>(action.value) / 127.0f : 0.0f;
                            instance.blockEvents[count++] = routed;
                        }
                        if (action.type == NeonActionType::Pad) held = {};
                        continue;
                    }
                    selectSurface(instance, action.mode, static_cast<uint8_t>(action.layer));
                    if (action.bank < 4u) selectSurfaceBank(instance, action.bank);
                    const bool secondary = action.layer == s3g::controller::reloop_neon::Layer::Second;
                    const uint8_t cell = static_cast<uint8_t>(instance.visibleBank.load() * 8u + action.pad);
                    const uint8_t selected = instance.selectedSlot.load();
                    if (action.mode == NeonMode::HotCue && !(secondary && effectiveEditPage(instance) == 7u)) {
                        if (secondary && action.pad == 7u) {
                            append(header->time, SampleNeonEventKind::Choke, 0u,
                                instance.selectedSlot.load(), NeonMode::Sampler, 0u, 0.0f);
                            continue;
                        }
                        if (secondary) editToolAction(instance, action.pad);
                        else instance.selectedSlot.store(cell);
                        const uint8_t previewSlot = secondary ? selected : cell;
                        // Preview through the ordinary cell path: Motion,
                        // Grains, direction and route all apply.
                        append(header->time, SampleNeonEventKind::Choke, 0u,
                            previewSlot, NeonMode::Sampler, 0u, 0.0f);
                        held = {};
                        held.active = true;
                        held.event.frameOffset = header->time;
                        held.event.kind = SampleNeonEventKind::Trigger;
                        held.event.slot = previewSlot;
                        held.event.mode = NeonMode::Sampler;
                        held.event.noteId = 0x72000000u + previewSlot;
                        held.event.value = static_cast<float>(action.value) / 127.0f;
                        if (count < instance.blockEvents.size()) instance.blockEvents[count++] = held.event;
                        continue;
                    }
                    if (action.mode == NeonMode::HotLoop
                        && (!secondary || !instance.publishedAssets[cell].load())) {
                        capturePadAction(instance, action.pad, secondary, action.shifted);
                        continue;
                    }
                    if (action.mode == NeonMode::Slicer && secondary) {
                        chopToolAction(instance, action.pad, action.shifted);
                        continue;
                    }
                    const bool chop = action.mode == NeonMode::Slicer;
                    const bool padFx = secondary && (action.mode == NeonMode::Sampler
                        || (action.mode == NeonMode::HotCue && effectiveEditPage(instance) == 7u));
                    if (padFx && !fxAllowed(instance, selected, action.pad)) continue;
                    if (chop && cell >= sliceCount(instance, selected)) continue;
                    const uint8_t slot = chop || padFx ? selected : cell;
                    if (!chop && !padFx) instance.selectedSlot.store(slot);
                    if (chop) {
                        instance.visibleSlice.store(cell);
                        const auto layout = sliceLayout(instance, slot);
                        instance.editPositions[slot].store(layout.boundaries[cell]);
                    }
                    if (!padFx && (instance.neonState.modeHeld || instance.neonState.repeatHeld
                        || instance.neonState.slipHeld || instance.neonState.syncHeld)) {
                        // Edit flags silently; they belong to the ordinary cell.
                        if (instance.neonState.modeHeld && !chop) {
                            const auto id = slotParamId(slot, kSlotTriggerMode);
                            const int trigger = static_cast<int>(paramValue(instance, id));
                            setControllerParam(instance, id, trigger == 2 ? 3 : trigger == 3 ? 1 : 2,
                                header->time, output);
                        } else if (!chop) {
                            if (normalizedStageOptions(instance.textureOptions[slot].load()) != 0u) {
                                if (instance.neonState.syncHeld) {
                                    if (playbackIndex(instance, slot) != 5u) instance.playbackClocks[slot].fetch_xor(1u);
                                }
                                else {
                                    const auto id = slotParamId(slot, kSlotTriggerMode);
                                    setControllerParam(instance, id, paramValue(instance, id) == 3.0 ? 2.0 : 3.0, header->time, output);
                                }
                                markStateDirty(instance);
                            } else {
                                const uint8_t option = instance.neonState.syncHeld ? kSlotSyncOption : kSlotRepeatOption;
                                setSlotOption(instance, slot, option, !slotOptionEnabled(instance, slot, option), false);
                            }
                        }
                        continue;
                    }
                    held.active = true;
                    held.pressureOnly = padFx;
                    held.event = {};
                    held.event.frameOffset = header->time;
                    held.event.slot = slot;
                    held.event.mode = chop ? NeonMode::HotCue : NeonMode::Sampler; // one-shot CHOP preview
                    held.event.performanceIndex = chop ? cell : 0u;
                    held.event.noteId = 0x4e000000u + static_cast<uint64_t>(slot) * 32u + action.pad;
                    held.event.kind = padFx ? SampleNeonEventKind::Pressure : SampleNeonEventKind::Trigger;
                    held.event.value = static_cast<float>(action.value) / 127.0f;
                    held.event.reverse = instance.neonState.censorHeld;
                    if (padFx) setControllerParam(instance, slotParamId(slot, kSlotCharacter),
                        action.pad, header->time, output);
                    if (count < instance.blockEvents.size()) instance.blockEvents[count++] = held.event;
                }
                continue;
            }
        }

        if (bridgedControl) continue; // Private controls cannot become music.
        const uint8_t status = event->data[0u];
        const uint8_t channel = static_cast<uint8_t>(status & 0x0fu);
        if (receiveChannel != 0 && receiveChannel != channel + 1) continue;
        const uint8_t command = static_cast<uint8_t>(status & 0xf0u);
        const uint8_t key = event->data[1u];
        if (key < baseNote
            || key >= baseNote + s3g::sample::kSampleNeonSlotCount)
            continue;
        const uint8_t slot = static_cast<uint8_t>(key - baseNote);
        if (command == 0x90u && event->data[2u] != 0u)
            append(header->time, SampleNeonEventKind::Trigger,
                0x60000000u + slot, slot, NeonMode::Sampler, 0u,
                static_cast<float>(event->data[2u]) / 127.0f);
        else if (command == 0x80u || command == 0x90u)
            append(header->time, SampleNeonEventKind::Release,
                0x60000000u + slot, slot, NeonMode::Sampler, 0u, 0.0f);
    }
    return count;
}

void pushNeonFeedback(Plugin& instance, const clap_output_events_t* output,
    uint32_t time, bool refreshPads) noexcept
{
    const bool active = paramValue(instance, kNeonActiveParamId) >= 0.5;
    if (!active) {
#if defined(__APPLE__)
        if (instance.neonWasActive) instance.directNeonOutput.setInactive();
#endif
        instance.neonWasActive = false;
        return;
    }
    if (!instance.neonWasActive) {
        instance.neonWasActive = true;
        instance.sendNeonInitialization.store(true,
            std::memory_order_release);
        instance.ledFeedbackDirty.store(true, std::memory_order_release);
    }
    s3g::controller::reloop_neon::LedFrame frame;
    const uint8_t bank = instance.visibleBank.load(std::memory_order_relaxed);
    const uint8_t selected = instance.selectedSlot.load(
        std::memory_order_relaxed);
    const uint8_t mode = instance.visibleMode.load(
        std::memory_order_relaxed);
    const uint8_t layer = instance.visibleLayer.load(
        std::memory_order_relaxed);
    frame.bank = bank;
    frame.mode = static_cast<NeonMode>(std::min<uint8_t>(mode, 3u));
    frame.layer = layer == 0u
        ? s3g::controller::reloop_neon::Layer::First
        : s3g::controller::reloop_neon::Layer::Second;
    const auto secondaryColor = [&](bool active = false) {
        return s3g::controller::reloop_neon::secondaryPadColor(frame.mode, active);
    };
    for (uint8_t pad = 0u; pad < 8u; ++pad) {
        auto& lamps = frame.pads[pad];
        lamps.segments.fill(0u);
        const uint8_t cell = static_cast<uint8_t>(bank * 8u + pad);
        if (mode == static_cast<uint8_t>(NeonMode::HotLoop)) {
            if (layer && !instance.publishedAssets[cell].load()) {
                lamps.surface = instance.captureTarget.load() == cell ? secondaryColor(true) : 0u;
                continue;
            } else if (!layer) {
                const auto state = instance.captureState.load();
                const bool available = pad == 0u ? state == Plugin::CaptureState::Empty || state == Plugin::CaptureState::Recording
                    : pad == 1u ? state == Plugin::CaptureState::Recording
                    : pad == 2u || pad == 3u || pad == 6u || pad == 7u ? state == Plugin::CaptureState::Review : true;
                lamps.surface = !available ? 0u : pad == 0u && state == Plugin::CaptureState::Recording ? 127u : 64u;
                continue;
            }
            // Loaded secondary pads retain their page hue until sounding.
        }
        if (layer && (mode == static_cast<uint8_t>(NeonMode::Sampler)
            || (mode == static_cast<uint8_t>(NeonMode::HotCue) && effectiveEditPage(instance) == 7u))) {
            lamps.surface = !fxAllowed(instance, selected, pad) ? 0u
                : secondaryColor(paramValue(instance, slotParamId(selected, kSlotCharacter)) == pad);
            continue;
        }
        if (mode == static_cast<uint8_t>(NeonMode::HotCue) && layer) {
            const auto playback = normalizedStageOptions(instance.textureOptions[selected].load());
            const auto trigger = paramValue(instance, slotParamId(selected, kSlotTriggerMode));
            const bool chosen = pad == 0u ? effectiveEditPage(instance) == 0u
                : pad == 1u ? playback == 0u : pad == 2u ? playback == kMotionOption
                : pad == 3u ? playback == kGrainsOption
                : pad < 7u ? trigger == (pad == 4u ? 2.0 : pad == 5u ? 1.0 : 3.0) : false;
            lamps.surface = secondaryColor(chosen);
            continue;
        }
        if (mode == static_cast<uint8_t>(NeonMode::Slicer)) {
            const bool loaded = instance.publishedAssets[selected].load() != nullptr;
            if (layer) {
                lamps.surface = loaded ? secondaryColor(pad == 3u
                    && paramValue(instance, slotParamId(selected, kSlotZeroCross)) >= 0.5) : 0u;
            } else if (cell < sliceCount(instance, selected)) {
                lamps.surface = !loaded ? 0u : instance.visibleSlice.load() == cell ? 104u : 48u;
                lamps.segments[s3g::controller::reloop_neon::lampIndex(NeonLamp::OneShot)] = loaded ? 80u : 0u;
            }
            continue;
        }
        const bool fx = mode == static_cast<uint8_t>(NeonMode::Sampler) && layer != 0u;
        const uint8_t slot = fx ? selected : cell;
        const bool loaded = instance.publishedAssets[slot].load() != nullptr;
        const bool highlighted = fx ? paramValue(instance, slotParamId(slot, kSlotCharacter)) == pad
            : instance.lastPlayedCells[bank].load() == slot;
        const bool sounding = instance.slotPlaying[slot].load() || instance.slotPeaks[slot].load() > 0.001f;
        // Hardware palette: loaded red, last played yellow, active white.
        lamps.surface = !loaded ? 0u : layer ? secondaryColor(sounding)
            : sounding ? 127u : highlighted ? 104u : 48u;
        const int trigger = static_cast<int>(paramValue(instance, slotParamId(slot, kSlotTriggerMode)));
        lamps.segments[trigger == 3 ? 1u : trigger == 1 ? 2u : 0u] = loaded ? 80u : 0u;
        const bool generated = normalizedStageOptions(instance.textureOptions[slot].load()) != 0u;
        lamps.segments[3u] = loaded && (generated ? trigger == 3 || trigger == 1
            : slotOptionEnabled(instance, slot, kSlotRepeatOption)
                || paramValue(instance, slotParamId(slot, kSlotDirection)) >= 2.0) ? 127u : 0u;
        lamps.segments[4u] = loaded && (generated ? playbackIndex(instance, slot) != 5u && instance.playbackClocks[slot].load() != 0u
            : slotOptionEnabled(instance, slot, kSlotSyncOption)) ? 127u : 0u;
    }
#if defined(__APPLE__)
    instance.directNeonOutput.publish(true, frame, instance.ledFeedbackDirty.exchange(false, std::memory_order_acq_rel));
    (void)output; (void)time; (void)refreshPads;
    // Never duplicate controller commands into REAPER's musical MIDI graph,
    // even while the device is disconnected or direct MIDI reports an error.
#else

    if (!output || !output->try_push) return;
    if (instance.ledFeedbackDirty.exchange(false,
            std::memory_order_acq_rel))
        instance.ledEncoder.invalidate();
    if (instance.sendNeonInitialization.load(std::memory_order_acquire)) {
        static constexpr auto sysex =
            s3g::controller::reloop_neon::enableFourDecksSysEx();
        clap_event_midi_sysex_t event {};
        event.header.size = sizeof(event);
        event.header.time = time;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_MIDI_SYSEX;
        event.port_index = 0u;
        event.buffer = sysex.data();
        event.size = static_cast<uint32_t>(sysex.size());
        if (output->try_push(output, &event.header))
            instance.sendNeonInitialization.store(false,
                std::memory_order_release);
    }
    std::array<NeonMidi,
        s3g::controller::reloop_neon::kMaximumLedMessages> messages {};
    const std::size_t count = instance.ledEncoder.encode(frame,
        messages.data(), messages.size(), false, refreshPads);
    for (std::size_t index = 0u; index < count; ++index) {
        clap_event_midi_t event {};
        event.header.size = sizeof(event);
        event.header.time = time;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_MIDI;
        event.port_index = 0u;
        event.data[0u] = messages[index].status;
        event.data[1u] = messages[index].data1;
        event.data[2u] = messages[index].data2;
        if (!output->try_push(output, &event.header)) {
            instance.ledFeedbackDirty.store(true,
                std::memory_order_release);
            break;
        }
    }
#endif
}

bool pluginInit(const clap_plugin_t* plugin)
{
    auto& instance = *self(plugin);
    if (instance.host && instance.host->get_extension) {
        instance.hostParams = static_cast<const clap_host_params_t*>(
            instance.host->get_extension(instance.host, CLAP_EXT_PARAMS));
        instance.hostState = static_cast<const clap_host_state_t*>(
            instance.host->get_extension(instance.host, CLAP_EXT_STATE));
    }
#if defined(S3G_SAMPLE_FILE_WORKER)
    if (!startLoader(instance)) return false;
#endif
#if defined(__APPLE__)
    instance.directNeonOutput.start();
#endif
    return true;
}

#if defined(S3G_ENABLE_VSTGUI_SAMPLE_NEON_GUI)
void destroyPortableGui(Plugin& instance);
#endif

void pluginDestroy(const clap_plugin_t* plugin)
{
    auto& instance = *self(plugin);
#if defined(__APPLE__)
    instance.directNeonOutput.stop();
#endif
#if defined(S3G_ENABLE_VSTGUI_SAMPLE_NEON_GUI)
    destroyPortableGui(instance);
#endif
#if defined(S3G_SAMPLE_FILE_WORKER)
    stopLoader(instance);
#endif
    delete &instance;
}

bool pluginActivate(const clap_plugin_t* plugin, double sampleRate,
    uint32_t, uint32_t maximumFrames)
{
    auto& instance = *self(plugin);
    if (!instance.engine.prepare(sampleRate, maximumFrames)) return false;
    instance.sampleRate = sampleRate;
    instance.neonPadInput.prepare(sampleRate);
    instance.maximumFrames = maximumFrames;
    try {
        for (auto& channel : instance.scratch)
            channel.assign(maximumFrames, 0.0f);
        for (auto& channel : instance.previewScratch)
            channel.assign(maximumFrames, 0.0f);
        if (!instance.recorder.prepare(sampleRate)
            || !instance.capturePreview.prepare(sampleRate, 16u)) {
            instance.engine.unprepare();
            return false;
        }
    } catch (...) {
        instance.engine.unprepare();
        return false;
    }
    for (std::size_t slot = 0u; slot < instance.audioAssets.size(); ++slot) {
        instance.audioAssets[slot] = instance.publishedAssets[slot].load(
            std::memory_order_acquire);
        instance.engine.setPreparedAsset(slot, instance.audioAssets[slot]);
    }
    instance.sendNeonInitialization.store(true, std::memory_order_release);
    instance.ledFeedbackDirty.store(true, std::memory_order_release);
    instance.active = true;
    return true;
}

void pluginDeactivate(const clap_plugin_t* plugin)
{
    auto& instance = *self(plugin);
    instance.active = false;
    if (instance.resetAllPhase.load() == 1u) stopSoundForReset(instance);
    if (instance.captureState.load() == Plugin::CaptureState::Recording)
        instance.captureState.store(Plugin::CaptureState::Ready, std::memory_order_release);
    serviceWorkflow(instance);
    instance.capturePreview.unprepare();
    instance.audioCapture = nullptr;
#if defined(__APPLE__)
    instance.directNeonOutput.setInactive();
#endif
    instance.neonWasActive = false;
    instance.engine.unprepare();
    instance.audioAssets.fill(nullptr);
    instance.retainedWavesets.clear();
    for (std::size_t slot = 0u; slot < 32u; ++slot) {
        if (instance.controlWavesets[slot]
            && instance.controlWavesets[slot]->asset.get() != instance.controlAssets[slot].get()) {
            instance.publishedWavesets[slot].store(nullptr);
            instance.controlWavesets[slot].reset(); instance.waveAnalysisAttempted[slot] = nullptr;
        }
        if (instance.controlWavesets[slot]) instance.retainedWavesets.push_back(instance.controlWavesets[slot]);
    }
    for (auto& channel : instance.scratch) channel.clear();
    std::lock_guard<std::mutex> lock(instance.statusMutex);
    instance.retainedAssets.clear();
    for (const auto& asset : instance.controlAssets)
        if (asset) instance.retainedAssets.push_back(asset);
}

bool pluginStartProcessing(const clap_plugin_t* plugin)
{
    auto& p = *self(plugin);
    p.processing.store(p.active);
    return p.active;
}

void pluginStopProcessing(const clap_plugin_t* plugin)
{
    auto& p = *self(plugin);
    p.processing.store(false);
    if (p.resetAllPhase.load() == 1u) {
        stopSoundForReset(p);
        if (p.host && p.host->request_callback) p.host->request_callback(p.host);
    }
}

void pluginReset(const clap_plugin_t* plugin)
{
    auto& instance = *self(plugin);
    instance.engine.reset();
    instance.neonState = {};
    instance.neonPadInput.clear();
    instance.heldSlots.fill(0xffu);
    instance.heldGestures = {};
    instance.capturePreview.reset();
    instance.captureCommand.store(2);
    instance.neonWasActive = false;
    instance.sendNeonInitialization.store(true, std::memory_order_release);
    instance.ledFeedbackDirty.store(true, std::memory_order_release);
}

clap_process_status pluginProcess(const clap_plugin_t* plugin,
    const clap_process_t* process)
{
    if (!process) return CLAP_PROCESS_ERROR;
    auto& instance = *self(plugin);
    if (process->frames_count > instance.maximumFrames
        || process->audio_outputs_count < 1u || !process->audio_outputs)
        return CLAP_PROCESS_ERROR;
    if (instance.resetAllPhase.load(std::memory_order_acquire)) {
        if (instance.resetAllPhase.load() == 1u) {
            stopSoundForReset(instance);
            if (instance.host && instance.host->request_callback) instance.host->request_callback(instance.host);
        }
        auto& audio = process->audio_outputs[0u];
        for (uint32_t channel = 0u; channel < audio.channel_count; ++channel) {
            if (audio.data32 && audio.data32[channel]) std::fill_n(audio.data32[channel], process->frames_count, 0.0f);
            if (audio.data64 && audio.data64[channel]) std::fill_n(audio.data64[channel], process->frames_count, 0.0);
        }
        audio.constant_mask = UINT64_MAX;
        return CLAP_PROCESS_CONTINUE;
    }
    const bool playing = process->transport
        && (process->transport->flags & CLAP_TRANSPORT_IS_PLAYING) != 0u;
    instance.transportPlaying.store(playing, std::memory_order_relaxed);
    const bool beatValid = process->transport
        && (process->transport->flags
            & CLAP_TRANSPORT_HAS_BEATS_TIMELINE) != 0u;
    instance.hostBeatValid.store(beatValid, std::memory_order_relaxed);
    if (beatValid)
        instance.hostBeatPosition.store(
            static_cast<double>(process->transport->song_pos_beats)
                / static_cast<double>(CLAP_BEATTIME_FACTOR),
            std::memory_order_relaxed);
    if (process->transport
        && (process->transport->flags & CLAP_TRANSPORT_HAS_TEMPO) != 0u
        && std::isfinite(process->transport->tempo))
        instance.hostTempo.store(static_cast<float>(std::clamp(
            process->transport->tempo, 20.0, 999.0)),
            std::memory_order_relaxed);
    serviceGuiParamEvents(instance, process->out_events);
    for (std::size_t slot = 0u; slot < instance.audioAssets.size(); ++slot) {
        const auto* asset = instance.publishedAssets[slot].load(
            std::memory_order_acquire);
        if (asset != instance.audioAssets[slot]) {
            instance.audioAssets[slot] = asset;
            instance.engine.setPreparedAsset(slot, asset);
        }
        instance.engine.setPreparedWavesets(slot, instance.publishedWavesets[slot].load(std::memory_order_acquire));
    }
    if (instance.resetNeonVelocity.exchange(false) || paramValue(instance, kNeonActiveParamId) < 0.5)
        instance.neonPadInput.clear();
    std::size_t eventCount = collectEvents(instance, process->in_events,
        process->out_events, process->frames_count);
    instance.neonPadInput.advance(process->frames_count);
    if (instance.killRequested.exchange(false, std::memory_order_acq_rel)) {
        instance.engine.killAll();
        instance.capturePreview.killAll();
        eventCount = 0u;
    }
    std::array<float*, s3g::sample::kSampleNeonOutputChannels> pointers {};
    for (std::size_t channel = 0u; channel < pointers.size(); ++channel)
        pointers[channel] = instance.scratch[channel].data();
    const SampleNeonSettings settings = settingsSnapshot(instance);
    for (std::size_t n = 0u; n < eventCount; ++n) {
        const auto& event = instance.blockEvents[n];
        if (event.kind == SampleNeonEventKind::Trigger && event.slot < 32u
            && event.value > 0.0f && instance.audioAssets[event.slot])
            instance.lastPlayedCells[event.slot / 8u].store(event.slot);
    }
    instance.engine.render(settings, instance.blockEvents.data(), eventCount,
        pointers.data(), instance.outputChannels, process->frames_count);
    int captureCommand = instance.captureCommand.exchange(0);
    if (captureCommand == 3) captureCommand = instance.captureState.load() == Plugin::CaptureState::Recording ? 2 : 1;
    if (captureCommand == 1 && instance.captureState.load() == Plugin::CaptureState::Empty) {
        instance.recordingLayout = settings.outputLayout;
        instance.recordingBus = std::min<uint8_t>(instance.captureBus.load(),
            static_cast<uint8_t>(s3g::sample::sampleNeonBusCount(settings.outputLayout) - 1u));
        instance.recorder.start(s3g::sample::sampleNeonBusWidth(settings.outputLayout));
        instance.captureFrames.store(0u);
        instance.captureState.store(Plugin::CaptureState::Recording);
    }
    if (instance.captureState.load() == Plugin::CaptureState::Recording) {
        const bool stop = captureCommand == 2 || settings.outputLayout != instance.recordingLayout;
        const bool full = !stop && instance.recorder.append(pointers.data(), process->frames_count,
            instance.recordingBus * s3g::sample::sampleNeonBusWidth(instance.recordingLayout));
        instance.captureFrames.store(instance.recorder.count());
        if (stop || full) {
            instance.captureState.store(Plugin::CaptureState::Ready, std::memory_order_release);
            if (instance.host && instance.host->request_callback) instance.host->request_callback(instance.host);
        }
    }
    const auto* capture = instance.publishedCapture.load(std::memory_order_acquire);
    if (capture != instance.audioCapture) {
        instance.audioCapture = capture;
        instance.capturePreview.setPreparedAsset(capture);
    }
    std::array<float*, 16u> previewPointers {};
    for (std::size_t channel = 0u; channel < previewPointers.size(); ++channel)
        previewPointers[channel] = instance.previewScratch[channel].data();
    s3g::sample::PlayerSettings previewSettings;
    previewSettings.start = instance.captureStart.load();
    previewSettings.length = std::max(0.0, instance.captureEnd.load() - previewSettings.start);
    previewSettings.gainDecibels = 0.0f;
    previewSettings.triggerMode = TriggerMode::OneShot;
    s3g::sample::RenderEvent previewEvent;
    previewEvent.key = 60u;
    previewEvent.velocity = 1.0f;
    const bool preview = instance.captureAudition.exchange(false);
    instance.capturePreview.render(previewSettings, preview ? &previewEvent : nullptr,
        preview ? 1u : 0u, previewPointers.data(), 16u, process->frames_count);
    instance.capturePlayhead.store(capture && instance.capturePreview.voiceCursorCount()
        ? instance.capturePreview.voiceCursors()[0u].sourcePositionNormalized : -1.0f);
    // Preview is monitored only through a matching bus layout, never decoded.
    if (capture && static_cast<uint8_t>(settings.outputLayout) == instance.captureLayout.load())
        for (uint8_t channel = 0u; channel < capture->channelCount; ++channel)
            for (uint32_t frame = 0u; frame < process->frames_count; ++frame)
                pointers[channel][frame] += previewPointers[channel][frame];
    for (std::size_t slot = 0u; slot < instance.slotPeaks.size(); ++slot) {
        instance.slotPeaks[slot].store(instance.engine.slotPeak(slot),
            std::memory_order_relaxed);
        instance.slotPlaying[slot].store(instance.engine.slotPlaybackActive(slot), std::memory_order_relaxed);
    }
    for (std::size_t slot = 0u; slot < instance.slotPeaks.size(); ++slot) {
        instance.motionPositions[slot].store(static_cast<float>(
            instance.engine.motionPosition(slot, settings)),
            std::memory_order_relaxed);
        const uint8_t cursorCount = static_cast<uint8_t>(std::min<uint32_t>(
            instance.engine.voiceCursorCount(slot),
            static_cast<uint32_t>(s3g::sample::kMaximumVoices)));
        const auto& cursors = instance.engine.voiceCursors(slot);
        for (uint8_t cursor = 0u; cursor < cursorCount; ++cursor) {
            instance.voiceCursorPositions[slot][cursor].store(
                cursors[cursor].sourcePositionNormalized,
                std::memory_order_relaxed);
            instance.voiceCursorStarts[slot][cursor].store(
                cursors[cursor].sourceStartNormalized,
                std::memory_order_relaxed);
            instance.voiceCursorEnds[slot][cursor].store(
                cursors[cursor].sourceEndNormalized,
                std::memory_order_relaxed);
        }
        instance.voiceCursorCounts[slot].store(cursorCount,
            std::memory_order_release);
    }
    instance.outputPeak.store(instance.engine.outputPeak(),
        std::memory_order_relaxed);
    instance.activeVoices.store(static_cast<uint32_t>(
        instance.engine.activeVoiceCount()), std::memory_order_relaxed);

    auto& output = process->audio_outputs[0u];
    if (output.channel_count < instance.outputChannels)
        return CLAP_PROCESS_ERROR;
    output.constant_mask = 0u;
    for (uint32_t channel = 0u; channel < output.channel_count; ++channel) {
        const float* source = channel < instance.outputChannels
            ? instance.scratch[channel].data() : nullptr;
        if (output.data32 && output.data32[channel]) {
            for (uint32_t frame = 0u; frame < process->frames_count; ++frame)
                output.data32[channel][frame] = source ? source[frame] : 0.0f;
        } else if (output.data64 && output.data64[channel]) {
            for (uint32_t frame = 0u; frame < process->frames_count; ++frame)
                output.data64[channel][frame] = source ? source[frame] : 0.0;
        }
    }
    instance.ledRefreshFrames += process->frames_count;
    const bool refreshPads = instance.ledRefreshFrames >= static_cast<uint64_t>(instance.sampleRate * 0.25);
    if (refreshPads) instance.ledRefreshFrames = 0u;
    pushNeonFeedback(instance, process->out_events,
        process->frames_count == 0u ? 0u : process->frames_count - 1u, refreshPads);
    return CLAP_PROCESS_CONTINUE;
}

void pluginOnMainThread(const clap_plugin_t* plugin)
{
    auto& instance = *self(plugin);
    if (instance.stateDirtyPending.exchange(false, std::memory_order_acq_rel)
        && instance.hostState && instance.hostState->mark_dirty)
        instance.hostState->mark_dirty(instance.host);
    serviceWorkflow(*self(plugin));
#if defined(S3G_SAMPLE_FILE_WORKER)
    serviceLoads(*self(plugin));
#else
    (void)plugin;
#endif
    serviceWavesets(*self(plugin));
}

#if defined(S3G_ENABLE_VSTGUI_SAMPLE_NEON_GUI)
#include "s3g_sample_neon_vstgui.inc"
#include "../common/s3g_clap_canvas_gui.inc"
#endif

const void* pluginGetExtension(const clap_plugin_t*, const char* id)
{
    if (!id) return nullptr;
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0)
        return &audioPortsExtension;
    if (std::strcmp(id, CLAP_EXT_NOTE_PORTS) == 0)
        return &notePortsExtension;
    if (std::strcmp(id, CLAP_EXT_NOTE_NAME) == 0)
        return &noteNameExtension;
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0)
        return &paramsExtension;
    if (std::strcmp(id, CLAP_EXT_STATE) == 0)
        return &stateExtension;
#if defined(S3G_ENABLE_VSTGUI_SAMPLE_NEON_GUI)
    if (std::strcmp(id, CLAP_EXT_GUI) == 0) return &portableGui;
#endif
    return nullptr;
}

const char* const multichannelFeatures[] {
    CLAP_PLUGIN_FEATURE_INSTRUMENT,
    CLAP_PLUGIN_FEATURE_SAMPLER,
    CLAP_PLUGIN_FEATURE_DRUM,
    CLAP_PLUGIN_FEATURE_SURROUND,
    nullptr,
};

const clap_plugin_descriptor_t multichannelDescriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-dsp.sample-neon",
    "s3g Sample Neon 32",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "", "", "0.22.2",
    "Channel-linked Neon sampler: stereo, quad, octo and ACN/SN3D ambisonics, with 32 output channels.",
    multichannelFeatures,
};

void initializeSoundDefaults(Plugin* instance)
{
    for (auto& cell : instance->lastPlayedCells) cell.store(0xffu);
    for (auto& playing : instance->slotPlaying) playing.store(false);
    for (std::size_t index = 0u; index < kStoredParamCount; ++index) {
        const clap_id id = parameterIdAt(index);
        std::size_t stored = 0u;
        std::size_t slot = 0u;
        SlotParamOffset offset = kSlotGain;
        const ParamDef* definition = nullptr;
        (void)parameterLocation(id, stored, definition, &slot, &offset);
        double value = definition->defaultValue;
        if (id >= kSlotParamBase && offset == kSlotCharacter)
            value = static_cast<double>(slot % 8u);
        instance->parameters[index].store(value, std::memory_order_relaxed);
    }
    for (std::size_t slot = 0u; slot < instance->statuses.size(); ++slot) {
        instance->statuses[slot] = "DROP OR LOAD A SAMPLE";
        instance->publishedAssets[slot].store(nullptr,
            std::memory_order_relaxed);
        instance->slotPeaks[slot].store(0.0f, std::memory_order_relaxed);
        for (auto& trigger : instance->sliceTriggers[slot])
            trigger.store(static_cast<uint8_t>(
                SampleNeonSliceTriggerMode::OneShot),
                std::memory_order_relaxed);
        for (auto& options : instance->sliceOptions[slot])
            options.store(0u, std::memory_order_relaxed);
        instance->sliceCounts[slot].store(static_cast<uint8_t>(
            s3g::sample::kSampleNeonSliceCount),
            std::memory_order_relaxed);
        const auto initialLayout =
            s3g::sample::equalSampleNeonSliceLayout(
                s3g::sample::kSampleNeonSliceCount);
        for (std::size_t marker = 0u;
             marker <= s3g::sample::kSampleNeonSliceCount; ++marker)
            instance->sliceBoundaries[slot][marker].store(
                initialLayout.boundaries[marker],
                std::memory_order_relaxed);
        instance->chopModes[slot].store(static_cast<uint8_t>(
            SampleNeonChopMode::Equal), std::memory_order_relaxed);
        instance->chopBeatDivisions[slot].store(2u,
            std::memory_order_relaxed);
        instance->sourceBpms[slot].store(120.0,
            std::memory_order_relaxed);
        instance->transientPreRollMs[slot].store(0.0f, std::memory_order_relaxed);
        instance->transientSliceLimits[slot].store(32u, std::memory_order_relaxed);
        instance->slicerDomainIndices[slot].store(2u,
            std::memory_order_relaxed);
        instance->slicerQuantizeIndices[slot].store(1u,
            std::memory_order_relaxed);
        instance->slotOptions[slot].store(kSlotVelocityOption,
            std::memory_order_relaxed);
        instance->chokeGroups[slot].store(0u,
            std::memory_order_relaxed);
        instance->editPositions[slot].store(0.0,
            std::memory_order_relaxed);
        instance->waveformZooms[slot].store(1.0f,
            std::memory_order_relaxed);
        instance->playbackClocks[slot].store(0u);
        for (unsigned effect = 0u; effect < 8u; ++effect)
            for (unsigned n = 0u; n < 3u; ++n) instance->fxParameters[slot][effect][n].store(s3g::sample::kSampleNeonFxDefaults[effect][n]);
        for (auto& method : instance->techniqueParameters[slot])
            for (unsigned n = 0u; n < 4u; ++n) method[n].store(n == 0u ? 0.35f : n == 3u ? 1.0f : 0.0f);
        instance->motionSeconds[slot].store(4.0f);
        instance->shotSeconds[slot].store(1.0f);
        instance->techniqueAttackSeconds[slot].store(0.005f);
        instance->techniqueReleaseSeconds[slot].store(0.005f);
        instance->grainIntervals[slot].store(0.25f);
        instance->textureOptions[slot].store(0u,
            std::memory_order_relaxed);
        instance->motionPaths[slot].store(static_cast<uint8_t>(
            SampleNeonMotionPath::Forward), std::memory_order_relaxed);
        instance->motionRates[slot].store(8.0f,
            std::memory_order_relaxed);
        instance->motionLoci[slot].store(0.5f,
            std::memory_order_relaxed);
        instance->motionFields[slot].store(0.5f,
            std::memory_order_relaxed);
        instance->lanePositions[slot].store(0.0f,
            std::memory_order_relaxed);
        instance->laneMotionDepths[slot].store(0.0f,
            std::memory_order_relaxed);
        instance->grainDensities[slot].store(12.0f,
            std::memory_order_relaxed);
        instance->grainSizes[slot].store(80.0f,
            std::memory_order_relaxed);
        instance->grainPositions[slot].store(0.5f,
            std::memory_order_relaxed);
        instance->grainSprays[slot].store(0.15f,
            std::memory_order_relaxed);
        instance->grainPitchSprays[slot].store(0.0f,
            std::memory_order_relaxed);
        instance->grainReverseChances[slot].store(0.0f,
            std::memory_order_relaxed);
        instance->motionPositions[slot].store(0.0f,
            std::memory_order_relaxed);
        instance->voiceCursorCounts[slot].store(0u,
            std::memory_order_relaxed);
        for (std::size_t cursor = 0u;
             cursor < s3g::sample::kMaximumVoices; ++cursor) {
            instance->voiceCursorPositions[slot][cursor].store(-1.0f,
                std::memory_order_relaxed);
            instance->voiceCursorStarts[slot][cursor].store(0.0f,
                std::memory_order_relaxed);
            instance->voiceCursorEnds[slot][cursor].store(1.0f,
                std::memory_order_relaxed);
        }
        for (std::size_t cue = 0u;
             cue < s3g::sample::kSampleNeonCueCount; ++cue) {
            instance->hotCuePositions[slot][cue].store(
                static_cast<double>(cue)
                    / static_cast<double>(s3g::sample::kSampleNeonCueCount),
                std::memory_order_relaxed);
            instance->hotCueEnabled[slot][cue].store(0u,
                std::memory_order_relaxed);
        }
        for (std::size_t loop = 0u;
             loop < s3g::sample::kSampleNeonSavedLoopCount; ++loop) {
            const double start = static_cast<double>(loop)
                / static_cast<double>(s3g::sample::kSampleNeonSavedLoopCount);
            instance->savedLoopStarts[slot][loop].store(start,
                std::memory_order_relaxed);
            instance->savedLoopEnds[slot][loop].store(std::min(1.0,
                start + std::pow(0.5, static_cast<double>(loop))),
                std::memory_order_relaxed);
            instance->savedLoopEnabled[slot][loop].store(0u,
                std::memory_order_relaxed);
        }
    }
    for (std::size_t pattern = 0u; pattern < kFlipPatternCount; ++pattern) {
        instance->flipStepCounts[pattern].store(0u,
            std::memory_order_relaxed);
        for (std::size_t step = 0u; step < kFlipMaximumSteps; ++step) {
            instance->flipSteps[pattern][step].store(0xffu,
                std::memory_order_relaxed);
            instance->flipVelocities[pattern][step].store(127u,
                std::memory_order_relaxed);
        }
    }
}

const clap_plugin_t* createPlugin(const clap_plugin_factory_t*,
    const clap_host_t* host, const char* pluginId)
{
    if (!host || !pluginId || std::strcmp(pluginId, multichannelDescriptor.id) != 0) return nullptr;
    const auto* descriptor = &multichannelDescriptor;
    const uint32_t outputChannels = s3g::sample::kSampleNeonOutputChannels;
    auto* instance = new (std::nothrow) Plugin();
    if (!instance) return nullptr;
    initializeSoundDefaults(instance);
    instance->host = host;
    instance->outputChannels = outputChannels;
    instance->plugin.desc = descriptor;
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

uint32_t factoryGetPluginCount(const clap_plugin_factory_t*) { return 1u; }

const clap_plugin_descriptor_t* factoryGetPluginDescriptor(
    const clap_plugin_factory_t*, uint32_t index)
{
    if (index == 0u) return &multichannelDescriptor;
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

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry {
    CLAP_VERSION_INIT,
    entryInit,
    entryDeinit,
    entryGetFactory,
};
