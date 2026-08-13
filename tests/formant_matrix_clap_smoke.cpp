#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/note-ports.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <iostream>
#include <limits>
#include <vector>

namespace {

constexpr const char* kPluginId = "org.s3g.s3g-dsp.formant-matrix";
constexpr const char* kPluginName = "s3g Processor Formant Matrix";
constexpr uint32_t kFrames = 128u;
constexpr uint32_t kBlocks = 96u;
constexpr uint32_t kLongSilentBlocks = 384u;
constexpr uint32_t kDynamicVoiceBlocks = 128u;
constexpr uint32_t kDynamicSilenceBlocks = 384u;
constexpr uint32_t kDynamicBlocks = kDynamicVoiceBlocks
    + kDynamicSilenceBlocks + kDynamicVoiceBlocks;
constexpr uint32_t kSourceExternalBlock = 128u;
constexpr uint32_t kSourceInternalBlock = 512u;
constexpr uint32_t kSourceAutomationBlocks = 640u;
constexpr uint32_t kChannels = 2u;
constexpr double kSampleRate = 48000.0;
constexpr double kInaudibleOutputRms = 1.0e-7;

struct HostContext {
    clap_host_t host {};
    clap_host_params_t params {};
    uint32_t processRequests = 0u;
    uint32_t callbackRequests = 0u;
    uint32_t paramRescans = 0u;
    clap_param_rescan_flags lastRescanFlags = 0u;
};

HostContext* hostContext(const clap_host_t* host)
{
    return static_cast<HostContext*>(host->host_data);
}

const void* hostGetExtension(const clap_host_t* host, const char* id)
{
    if (id && std::strcmp(id, CLAP_EXT_PARAMS) == 0) {
        return &hostContext(host)->params;
    }
    return nullptr;
}

void hostRequestRestart(const clap_host_t*) {}

void hostRequestProcess(const clap_host_t* host)
{
    ++hostContext(host)->processRequests;
}

void hostRequestCallback(const clap_host_t* host)
{
    ++hostContext(host)->callbackRequests;
}

void hostParamsRescan(const clap_host_t* host, clap_param_rescan_flags flags)
{
    auto* context = hostContext(host);
    ++context->paramRescans;
    context->lastRescanFlags = flags;
}

void hostParamsClear(const clap_host_t*, clap_id, clap_param_clear_flags) {}
void hostParamsRequestFlush(const clap_host_t*) {}

void configureHost(HostContext& context, const char* name)
{
    context.host.clap_version = CLAP_VERSION_INIT;
    context.host.host_data = &context;
    context.host.name = name;
    context.host.vendor = "s3g";
    context.host.url = "https://github.com/s3g/s3g-dsp";
    context.host.version = "1";
    context.host.get_extension = hostGetExtension;
    context.host.request_restart = hostRequestRestart;
    context.host.request_process = hostRequestProcess;
    context.host.request_callback = hostRequestCallback;
    context.params.rescan = hostParamsRescan;
    context.params.clear = hostParamsClear;
    context.params.request_flush = hostParamsRequestFlush;
}

bool check(bool condition, const char* message)
{
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}

bool hasFeature(const clap_plugin_descriptor_t* descriptor,
    const char* expected)
{
    if (!descriptor || !descriptor->features) return false;
    for (const char* const* feature = descriptor->features;
         *feature; ++feature) {
        if (std::strcmp(*feature, expected) == 0) return true;
    }
    return false;
}

std::filesystem::path resolveBinary(const std::filesystem::path& supplied)
{
    if (std::filesystem::is_regular_file(supplied)) return supplied;
#if defined(__APPLE__)
    if (std::filesystem::is_directory(supplied)) {
        const auto directory = supplied / "Contents" / "MacOS";
        if (!std::filesystem::is_directory(directory)) return {};
        for (const auto& entry :
             std::filesystem::directory_iterator(directory)) {
            if (entry.is_regular_file()) return entry.path();
        }
    }
#endif
    return {};
}

struct InputEvents {
    std::array<clap_event_param_value_t, 24u> params {};
    clap_event_note_t note {};
    std::array<const clap_event_header_t*, 25u> events {};
    uint32_t paramCount = 0u;
    uint32_t eventCount = 0u;
    clap_input_events_t input {
        this,
        [](const clap_input_events_t* list) -> uint32_t {
            return static_cast<const InputEvents*>(list->ctx)->eventCount;
        },
        [](const clap_input_events_t* list,
            uint32_t index) -> const clap_event_header_t* {
            const auto* source = static_cast<const InputEvents*>(list->ctx);
            return index < source->eventCount
                ? source->events[index] : nullptr;
        },
    };

    void addParam(clap_id id, double value)
    {
        auto& event = params[paramCount++];
        event = {};
        event.header.size = sizeof(event);
        event.header.time = 0u;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_PARAM_VALUE;
        event.param_id = id;
        event.note_id = -1;
        event.port_index = -1;
        event.channel = -1;
        event.key = -1;
        event.value = value;
        events[eventCount++] = &event.header;
    }

    void addNote(uint16_t type, double velocity, int32_t noteId = 73)
    {
        note = {};
        note.header.size = sizeof(note);
        note.header.time = 0u;
        note.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        note.header.type = type;
        note.note_id = noteId;
        note.port_index = 0;
        note.channel = 0;
        note.key = 60;
        note.velocity = velocity;
        events[eventCount++] = &note.header;
    }

    void addHeldNote() { addNote(CLAP_EVENT_NOTE_ON, 0.86); }
};

struct MemoryState {
    std::vector<uint8_t> bytes;
    size_t readOffset = 0u;
    clap_ostream_t output {
        this,
        [](const clap_ostream_t* stream, const void* source,
            uint64_t size) -> int64_t {
            auto* state = static_cast<MemoryState*>(stream->ctx);
            const auto* begin = static_cast<const uint8_t*>(source);
            state->bytes.insert(state->bytes.end(), begin, begin + size);
            return static_cast<int64_t>(size);
        },
    };
    clap_istream_t input {
        this,
        [](const clap_istream_t* stream, void* destination,
            uint64_t size) -> int64_t {
            auto* state = static_cast<MemoryState*>(stream->ctx);
            const size_t available = state->bytes.size() - state->readOffset;
            const size_t count = std::min<size_t>(available,
                static_cast<size_t>(size));
            if (count == 0u) return 0;
            std::memcpy(destination,
                state->bytes.data() + state->readOffset, count);
            state->readOffset += count;
            return static_cast<int64_t>(count);
        },
    };
};

enum class SampleFormat {
    Float32,
    Float64,
};

enum class ModulatorSource : uint32_t {
    ExternalMic = 0u,
    InternalSpeech = 1u,
    Blend = 2u,
};

struct RenderResult {
    bool ok = false;
    bool slept = false;
    std::vector<double> samples;
    double energy = 0.0;
    double onsetEnergy = 0.0;
    double lateEnergy = 0.0;
    double peak = 0.0;
    size_t onsetSampleCount = 0u;
    size_t lateSampleOffset = 0u;
};

enum class RenderSetup {
    ControlledVocoder,
    CreativeExternalMic,
    ClassicMic,
    SourceAutomation,
};

enum class InputPattern {
    Continuous,
    VoicedSilenceVoiced,
    VoicedThenSilence,
    PitchTone,
    PitchToneQuietSustain,
};

float micSample(uint64_t frame, uint32_t channel)
{
    const double time = static_cast<double>(frame) / kSampleRate;
    const double fundamental = channel == 0u ? 137.0 : 181.0;
    uint32_t bits = static_cast<uint32_t>(frame)
        ^ (0x9e3779b9u * (channel + 1u));
    bits ^= bits << 13u;
    bits ^= bits >> 17u;
    bits ^= bits << 5u;
    const double noise = static_cast<double>(bits & 0xffffu)
        / 32767.5 - 1.0;
    return static_cast<float>(
        0.24 * std::sin(6.28318530717958647692 * fundamental * time)
        + 0.15 * std::sin(6.28318530717958647692
            * fundamental * 2.01 * time + 0.3 * channel)
        + 0.08 * noise);
}

float deterministicNoiseSample(uint64_t frame, uint32_t channel,
    float rmsAmplitude)
{
    uint32_t bits = static_cast<uint32_t>(frame)
        ^ (0x85ebca6bu * (channel + 3u));
    bits ^= bits << 13u;
    bits ^= bits >> 17u;
    bits ^= bits << 5u;
    // A Rademacher sequence is deterministic white noise whose finite-window
    // RMS is exactly rmsAmplitude, avoiding calibration tolerance in this
    // threshold regression.
    return (bits & 1u ? rmsAmplitude : -rmsAmplitude);
}

float dbfsToAmplitude(float decibels)
{
    return std::pow(10.0f, decibels / 20.0f);
}

RenderResult render(const clap_plugin_factory_t* factory,
    SampleFormat format, bool provideInput, bool inPlace,
    uint16_t injectedNoteType = 0u,
    ModulatorSource source = ModulatorSource::InternalSpeech,
    bool phraseOff = false, double micGainDb = 0.0,
    bool inputSignal = true, bool destructiveShape = false,
    bool sendHeldNote = true, float inputPolarity = 1.0f,
    bool forceNoiseCarrier = false,
    RenderSetup setup = RenderSetup::ControlledVocoder,
    uint32_t blockCount = kBlocks,
    InputPattern inputPattern = InputPattern::Continuous,
    bool engageFreeze = false, float middleNoiseRms = 0.0f,
    bool automateSource = false, bool voicePitch = false)
{
    RenderResult result;
    HostContext context;
    configureHost(context, "s3g Formant Matrix CLAP smoke");

    const clap_plugin_t* plugin = factory
        ? factory->create_plugin(factory, &context.host, kPluginId) : nullptr;
    if (!plugin || !plugin->init(plugin)) {
        if (plugin) plugin->destroy(plugin);
        return result;
    }

    bool activated = plugin->activate(plugin, kSampleRate, 1u, kFrames);
    bool processing = activated && plugin->start_processing(plugin);
    if (!processing) {
        if (activated) plugin->deactivate(plugin);
        plugin->destroy(plugin);
        return result;
    }

    std::array<std::array<float, kFrames>, kChannels> input32 {};
    std::array<std::array<float, kFrames>, kChannels> output32 {};
    std::array<std::array<double, kFrames>, kChannels> input64 {};
    std::array<std::array<double, kFrames>, kChannels> output64 {};
    std::array<float*, kChannels> inputPointers32 {};
    std::array<float*, kChannels> outputPointers32 {};
    std::array<double*, kChannels> inputPointers64 {};
    std::array<double*, kChannels> outputPointers64 {};
    for (uint32_t channel = 0u; channel < kChannels; ++channel) {
        inputPointers32[channel] = input32[channel].data();
        outputPointers32[channel] = inPlace
            ? input32[channel].data() : output32[channel].data();
        inputPointers64[channel] = input64[channel].data();
        outputPointers64[channel] = inPlace
            ? input64[channel].data() : output64[channel].data();
    }

    clap_audio_buffer_t inputBuffer {};
    clap_audio_buffer_t outputBuffer {};
    inputBuffer.channel_count = kChannels;
    outputBuffer.channel_count = kChannels;
    if (format == SampleFormat::Float32) {
        inputBuffer.data32 = inputPointers32.data();
        outputBuffer.data32 = outputPointers32.data();
    } else {
        inputBuffer.data64 = inputPointers64.data();
        outputBuffer.data64 = outputPointers64.data();
    }

    InputEvents firstEvents;
    if (setup == RenderSetup::ClassicMic) {
        // Exercise the actual quick-start profile, including its routing and
        // bank topology, rather than restating those controls in the test.
        firstEvents.addParam(1u, 14.0); // Profile: Classic Mic
    } else if (setup == RenderSetup::CreativeExternalMic) {
        // Pin the former default-like creative topology independently of the
        // startup profile: a partially dry Hybrid bank with an open floor is
        // the important worst case for carrier leakage and gate hysteresis.
        firstEvents.addParam(65u, 0.90); // Bank Mix
        firstEvents.addParam(66u, 1.0); // Bank Mode: Hybrid
        firstEvents.addParam(94u, 0.08); // Open Level
        firstEvents.addParam(37u, 0.0); // Echo mix
        firstEvents.addParam(96u, 0.0); // No articulation dry path
        firstEvents.addParam(98u,
            static_cast<uint32_t>(ModulatorSource::ExternalMic));
    } else if (setup == RenderSetup::SourceAutomation) {
        // Keep the default Hybrid/Amount/Open bank, but make the internal side
        // use the natural-text phrase so both source-switch endpoints have a
        // stable, intentionally audible articulation.
        firstEvents.addParam(65u, 0.90); // Bank Mix
        firstEvents.addParam(66u, 1.0); // Bank Mode: Hybrid
        firstEvents.addParam(94u, 0.08); // Open Level
        firstEvents.addParam(37u, 0.0); // Echo mix
        firstEvents.addParam(96u, 0.0); // No articulation dry path
        firstEvents.addParam(98u,
            static_cast<uint32_t>(ModulatorSource::InternalSpeech));
        firstEvents.addParam(51u, 5.0); // Phoneme Score: Natural Text
        firstEvents.addParam(54u, 1.0); // Phrase Mode: Loop
    } else {
        // Force a fully wet measured-analysis vocoder. The external mic and
        // internal speech options share the same MIDI-tracked procedural
        // carrier. The note remains held throughout every block.
        firstEvents.addParam(51u, phraseOff ? 0.0 : 5.0); // Phoneme Score
        firstEvents.addParam(54u, 1.0); // Phrase Mode: Loop
        firstEvents.addParam(65u, 1.0); // Bank Amount
        firstEvents.addParam(66u, 0.0); // Bank Mode: Vocoder
        firstEvents.addParam(68u, 1.0); // Internal carrier harmonics
        firstEvents.addParam(70u, 0.0); // Internal carrier noise
        firstEvents.addParam(71u, 0.0); // Measured analysis
        firstEvents.addParam(37u, 0.0); // Echo mix
        firstEvents.addParam(94u, 0.0); // No open-bank floor
        firstEvents.addParam(96u, 0.0); // No articulation dry path
        firstEvents.addParam(98u, static_cast<uint32_t>(source));
        firstEvents.addParam(99u, micGainDb);
        if (forceNoiseCarrier) {
            firstEvents.addParam(67u, 4.0); // Carrier Shape: Noise
            firstEvents.addParam(88u, 1.0); // Voiced / Unvoiced: Noise
        }
        if (destructiveShape) {
            firstEvents.addParam(32u, 24.0); // Fuzz Drive
            firstEvents.addParam(33u, 1.0); // Fuzz Mix
        }
    }
    if (voicePitch) {
        firstEvents.addParam(1099u, 1.0); // Carrier Pitch Source: Voice
        firstEvents.addParam(1100u, 0.0); // Root: C
        firstEvents.addParam(1101u, 0.0); // Scale: Continuous
        firstEvents.addParam(1102u,
            inputPattern == InputPattern::PitchToneQuietSustain
                ? 2000.0 : 350.0); // Pitch Hold / Infinite
        if (inputPattern == InputPattern::PitchToneQuietSustain) {
            // Isolate carrier lifetime from the intentionally level-following
            // wet vocoder VCAs. The External Mic gate still surrounds this
            // rail, so a timeout or host sleep remains directly observable.
            firstEvents.addParam(65u, 0.0); // Bank Mix: carrier monitor
        }
    }
    if (sendHeldNote) firstEvents.addHeldNote();
    InputEvents injectedEvents;
    if (injectedNoteType != 0u) {
        injectedEvents.addNote(injectedNoteType, 0.0, -1);
    }
    InputEvents freezeEvents;
    if (engageFreeze) {
        // Engage a deterministic Continuous capture after the voice has been
        // analyzed for 170 ms; the mic then goes silent another 170 ms later.
        freezeEvents.addParam(84u, 0.0); // Freeze Trigger: Continuous
        freezeEvents.addParam(83u, 1.0); // Envelope Freeze
    }
    InputEvents externalSourceEvents;
    InputEvents internalSourceEvents;
    if (automateSource) {
        externalSourceEvents.addParam(98u,
            static_cast<uint32_t>(ModulatorSource::ExternalMic));
        internalSourceEvents.addParam(98u,
            static_cast<uint32_t>(ModulatorSource::InternalSpeech));
    }

    result.samples.reserve(
        static_cast<size_t>(blockCount) * kFrames * kChannels);
    const uint32_t onsetBlockCount = std::min<uint32_t>(8u, blockCount);
    const uint32_t lateBlock = blockCount * 3u / 4u;
    result.onsetSampleCount = static_cast<size_t>(onsetBlockCount)
        * kFrames * kChannels;
    result.lateSampleOffset = static_cast<size_t>(lateBlock)
        * kFrames * kChannels;
    bool valid = true;
    for (uint32_t block = 0u; valid && block < blockCount; ++block) {
        for (uint32_t channel = 0u; channel < kChannels; ++channel) {
            for (uint32_t frame = 0u; frame < kFrames; ++frame) {
                const bool sourceAutomationSilence = automateSource
                    && block >= kSourceExternalBlock
                    && block < kSourceInternalBlock;
                const bool pitchTone = inputPattern
                        == InputPattern::PitchTone
                    || inputPattern == InputPattern::PitchToneQuietSustain;
                const bool voicedWindow = pitchTone || (inputPattern
                        == InputPattern::Continuous
                        && !sourceAutomationSilence)
                    || (inputPattern != InputPattern::Continuous
                        && block < kDynamicVoiceBlocks)
                    || (inputPattern == InputPattern::VoicedSilenceVoiced
                        && block >= kDynamicVoiceBlocks
                            + kDynamicSilenceBlocks);
                const bool middleWindow = block >= kDynamicVoiceBlocks
                    && block < kDynamicVoiceBlocks + kDynamicSilenceBlocks;
                float input = 0.0f;
                if (provideInput && inputSignal) {
                    const uint64_t absoluteFrame
                        = static_cast<uint64_t>(block) * kFrames + frame;
                    if (pitchTone) {
                        const double time = static_cast<double>(absoluteFrame)
                            / kSampleRate;
                        const bool recoveryAttack = block >= 1800u
                            && block < 1880u;
                        const double amplitude = inputPattern
                                    == InputPattern::PitchToneQuietSustain
                                && block >= 720u && !recoveryAttack
                            ? 0.0015 : 0.32;
                        input = static_cast<float>(amplitude * std::sin(
                            6.28318530717958647692 * 230.0 * time));
                    } else if (middleWindow && middleNoiseRms > 0.0f) {
                        input = deterministicNoiseSample(absoluteFrame,
                            channel, middleNoiseRms) * inputPolarity;
                    } else if (voicedWindow) {
                        input = micSample(absoluteFrame, channel)
                            * inputPolarity;
                    }
                } else if (provideInput && middleWindow
                    && middleNoiseRms > 0.0f) {
                    const uint64_t absoluteFrame
                        = static_cast<uint64_t>(block) * kFrames + frame;
                    input = deterministicNoiseSample(absoluteFrame,
                        channel, middleNoiseRms) * inputPolarity;
                }
                input32[channel][frame] = input;
                input64[channel][frame] = static_cast<double>(input);
                if (!inPlace) {
                    output32[channel][frame]
                        = std::numeric_limits<float>::quiet_NaN();
                    output64[channel][frame]
                        = std::numeric_limits<double>::quiet_NaN();
                }
            }
        }

        clap_process_t process {};
        process.steady_time = static_cast<int64_t>(block) * kFrames;
        process.frames_count = kFrames;
        process.in_events = block == 0u ? &firstEvents.input
            : block == 16u && injectedNoteType != 0u
                ? &injectedEvents.input
            : block == kDynamicVoiceBlocks / 2u && engageFreeze
                ? &freezeEvents.input
            : block == kSourceExternalBlock && automateSource
                ? &externalSourceEvents.input
            : block == kSourceInternalBlock && automateSource
                ? &internalSourceEvents.input : nullptr;
        // Model a host withdrawing the live input buffer long enough for all
        // audible DSP tails to expire, then restoring it. A monitoring effect
        // must keep returning CONTINUE throughout or REAPER can strand it in
        // SLEEP while the hardware interface still shows input.
        const bool hostInputGap = inputPattern
                == InputPattern::PitchToneQuietSustain
            && block >= 1000u && block < 1800u;
        const bool provideBlockInput = provideInput && !hostInputGap;
        process.audio_inputs = provideBlockInput ? &inputBuffer : nullptr;
        process.audio_inputs_count = provideBlockInput ? 1u : 0u;
        process.audio_outputs = &outputBuffer;
        process.audio_outputs_count = 1u;
        const clap_process_status status = plugin->process(plugin, &process);
        valid = status != CLAP_PROCESS_ERROR;
        result.slept = result.slept || status == CLAP_PROCESS_SLEEP;

        for (uint32_t frame = 0u; valid && frame < kFrames; ++frame) {
            for (uint32_t channel = 0u;
                 valid && channel < kChannels; ++channel) {
                const double value = format == SampleFormat::Float32
                    ? static_cast<double>(outputPointers32[channel][frame])
                    : outputPointers64[channel][frame];
                valid = std::isfinite(value) && std::abs(value) <= 1.001;
                if (!valid) break;
                result.samples.push_back(value);
                result.energy += value * value;
                if (block < onsetBlockCount) {
                    result.onsetEnergy += value * value;
                }
                if (block >= lateBlock) result.lateEnergy += value * value;
                result.peak = std::max(result.peak, std::abs(value));
            }
        }
    }

    plugin->stop_processing(plugin);
    plugin->deactivate(plugin);
    plugin->destroy(plugin);
    result.ok = valid && result.samples.size()
        == static_cast<size_t>(blockCount) * kFrames * kChannels;
    return result;
}

double rms(const RenderResult& result)
{
    return result.samples.empty() ? std::numeric_limits<double>::infinity()
        : std::sqrt(result.energy
            / static_cast<double>(result.samples.size()));
}

double onsetRms(const RenderResult& result)
{
    return result.onsetSampleCount == 0u
        ? std::numeric_limits<double>::infinity()
        : std::sqrt(result.onsetEnergy
            / static_cast<double>(result.onsetSampleCount));
}

double lateRms(const RenderResult& result)
{
    const size_t count = result.samples.size() > result.lateSampleOffset
        ? result.samples.size() - result.lateSampleOffset : 0u;
    return count == 0u ? std::numeric_limits<double>::infinity()
        : std::sqrt(result.lateEnergy / static_cast<double>(count));
}

bool inaudible(const RenderResult& result)
{
    // A silent modulator should produce mathematical silence. Retain a tiny
    // floating-point allowance while keeping every window below -120 dBFS
    // peak and roughly -140 dBFS RMS.
    return result.ok && result.peak < 1.0e-6
        && rms(result) < 1.0e-7
        && onsetRms(result) < 1.0e-7
        && lateRms(result) < 1.0e-7;
}

RenderResult renderSilentHeldExternal(
    const clap_plugin_factory_t* factory, RenderSetup setup,
    bool provideInput)
{
    return render(factory, SampleFormat::Float32, provideInput, false, 0u,
        ModulatorSource::ExternalMic, false, 0.0, false, false, true, 1.0f,
        false, setup, kLongSilentBlocks);
}

struct DynamicMicMetrics {
    bool ok = false;
    double firstVoiceRms = 0.0;
    double silentLateRms = 0.0;
    double recoveredVoiceRms = 0.0;
    double closeMaximumStep = 0.0;
    double reopenMaximumStep = 0.0;
};

double windowRms(const RenderResult& result, uint32_t beginBlock,
    uint32_t endBlock)
{
    const size_t begin = std::min(result.samples.size(),
        static_cast<size_t>(beginBlock) * kFrames * kChannels);
    const size_t end = std::min(result.samples.size(),
        static_cast<size_t>(endBlock) * kFrames * kChannels);
    if (begin >= end) return std::numeric_limits<double>::infinity();
    double energy = 0.0;
    for (size_t index = begin; index < end; ++index) {
        energy += result.samples[index] * result.samples[index];
    }
    return std::sqrt(energy / static_cast<double>(end - begin));
}

double windowRmsDifference(const RenderResult& first,
    const RenderResult& second, uint32_t beginBlock, uint32_t endBlock)
{
    if (first.samples.size() != second.samples.size()) {
        return std::numeric_limits<double>::infinity();
    }
    const size_t begin = std::min(first.samples.size(),
        static_cast<size_t>(beginBlock) * kFrames * kChannels);
    const size_t end = std::min(first.samples.size(),
        static_cast<size_t>(endBlock) * kFrames * kChannels);
    if (begin >= end) return std::numeric_limits<double>::infinity();
    double energy = 0.0;
    for (size_t index = begin; index < end; ++index) {
        const double difference = first.samples[index]
            - second.samples[index];
        energy += difference * difference;
    }
    return std::sqrt(energy / static_cast<double>(end - begin));
}

double maximumStepNearBlock(const RenderResult& result, uint32_t block,
    uint32_t radiusFrames = 32u)
{
    if (result.samples.empty()) {
        return std::numeric_limits<double>::infinity();
    }
    const size_t boundary = static_cast<size_t>(block)
        * kFrames * kChannels;
    const size_t radius = static_cast<size_t>(radiusFrames) * kChannels;
    const size_t begin = boundary > radius ? boundary - radius : 1u;
    const size_t end = std::min(result.samples.size(), boundary + radius);
    double maximum = 0.0;
    // Samples are interleaved; compare like channels rather than treating the
    // ordinary stereo difference as a temporal discontinuity.
    for (size_t index = std::max<size_t>(begin, kChannels);
         index < end; ++index) {
        maximum = std::max(maximum,
            std::abs(result.samples[index]
                - result.samples[index - kChannels]));
    }
    return maximum;
}

DynamicMicMetrics dynamicMicMetrics(const RenderResult& result,
    bool expectRecovery)
{
    DynamicMicMetrics metrics;
    metrics.ok = result.ok;
    metrics.firstVoiceRms = windowRms(result,
        kDynamicVoiceBlocks / 2u, kDynamicVoiceBlocks);
    metrics.silentLateRms = windowRms(result,
        kDynamicVoiceBlocks + kDynamicSilenceBlocks - 64u,
        kDynamicVoiceBlocks + kDynamicSilenceBlocks);
    metrics.recoveredVoiceRms = expectRecovery ? windowRms(result,
        kDynamicVoiceBlocks + kDynamicSilenceBlocks + 64u,
        kDynamicBlocks) : 0.0;
    metrics.closeMaximumStep = maximumStepNearBlock(
        result, kDynamicVoiceBlocks);
    metrics.reopenMaximumStep = expectRecovery ? maximumStepNearBlock(
        result, kDynamicVoiceBlocks + kDynamicSilenceBlocks) : 0.0;
    return metrics;
}

RenderResult renderDynamicHeldExternal(
    const clap_plugin_factory_t* factory, bool freeze,
    float middleNoiseRms = 0.0f)
{
    const InputPattern pattern = freeze ? InputPattern::VoicedThenSilence
        : InputPattern::VoicedSilenceVoiced;
    return render(factory, SampleFormat::Float32, true, false, 0u,
        ModulatorSource::ExternalMic, false, 0.0, true, false, true, 1.0f,
        false, RenderSetup::CreativeExternalMic, kDynamicBlocks, pattern,
        freeze, middleNoiseRms);
}

RenderResult renderSourceAutomation(const clap_plugin_factory_t* factory)
{
    return render(factory, SampleFormat::Float32, true, false, 0u,
        ModulatorSource::InternalSpeech, false, 0.0, false, false, true,
        1.0f, false, RenderSetup::SourceAutomation,
        kSourceAutomationBlocks, InputPattern::Continuous, false, 0.0f,
        true);
}

RenderResult renderVoicePitchCarrier(const clap_plugin_factory_t* factory)
{
    // No note event is sent. After acquisition, drop the coherent mic tone
    // below the ordinary anti-drone close threshold for more than two seconds.
    // Infinite Pitch Hold plus periodic voice evidence must keep the carrier
    // alive and the effect must never ask the host to sleep.
    return render(factory, SampleFormat::Float32, true, false, 0u,
        ModulatorSource::ExternalMic, false, 0.0, true, false, false,
        1.0f, false, RenderSetup::ControlledVocoder, 2400u,
        InputPattern::PitchToneQuietSustain, false, 0.0f, false, true);
}

double rmsDifference(const RenderResult& first, const RenderResult& second)
{
    if (first.samples.size() != second.samples.size()
        || first.samples.empty()) return std::numeric_limits<double>::infinity();
    double energy = 0.0;
    for (size_t index = 0u; index < first.samples.size(); ++index) {
        const double difference = first.samples[index] - second.samples[index];
        energy += difference * difference;
    }
    return std::sqrt(energy / static_cast<double>(first.samples.size()));
}

double lateRmsDifference(const RenderResult& first,
    const RenderResult& second)
{
    if (first.samples.size() != second.samples.size()
        || first.samples.empty()) return std::numeric_limits<double>::infinity();
    if (first.lateSampleOffset != second.lateSampleOffset) {
        return std::numeric_limits<double>::infinity();
    }
    const size_t begin = std::min(first.samples.size(),
        first.lateSampleOffset);
    double energy = 0.0;
    for (size_t index = begin; index < first.samples.size(); ++index) {
        const double difference = first.samples[index] - second.samples[index];
        energy += difference * difference;
    }
    return std::sqrt(energy / static_cast<double>(
        std::max<size_t>(1u, first.samples.size() - begin)));
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "usage: formant_matrix_clap_smoke <bundle-or-binary>\n";
        return 2;
    }

    bool ok = true;
    const auto binary = resolveBinary(argv[1]);
    ok &= check(!binary.empty(), "could not resolve CLAP binary");
    void* handle = binary.empty()
        ? nullptr : dlopen(binary.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        if (const char* error = dlerror()) std::cerr << "dlopen: " << error << '\n';
    }
    ok &= check(handle != nullptr, "could not load CLAP binary");
    if (!handle) return 1;

    const auto* entry = static_cast<const clap_plugin_entry_t*>(
        dlsym(handle, "clap_entry"));
    ok &= check(entry && entry->init(nullptr),
        "CLAP entry initialization failed");
    if (!ok) {
        dlclose(handle);
        return 1;
    }
    const auto* factory = static_cast<const clap_plugin_factory_t*>(
        entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    ok &= check(factory && factory->get_plugin_count(factory) == 1u,
        "plugin factory contract failed");
    const auto* descriptor = factory
        ? factory->get_plugin_descriptor(factory, 0u) : nullptr;
    ok &= check(descriptor
            && std::strcmp(descriptor->id, kPluginId) == 0
            && std::strcmp(descriptor->name, kPluginName) == 0
            && std::strcmp(descriptor->version, "5.4.0") == 0,
        "plugin identity failed");
    ok &= check(hasFeature(descriptor, CLAP_PLUGIN_FEATURE_AUDIO_EFFECT)
            && hasFeature(descriptor, CLAP_PLUGIN_FEATURE_FILTER)
            && hasFeature(descriptor, CLAP_PLUGIN_FEATURE_STEREO)
            && !hasFeature(descriptor, CLAP_PLUGIN_FEATURE_INSTRUMENT)
            && !hasFeature(descriptor, CLAP_PLUGIN_FEATURE_SYNTHESIZER),
        "Formant Matrix must be classified as a CLAP effect, not CLAPi");

    HostContext metadataHost;
    configureHost(metadataHost, "s3g Formant Matrix metadata smoke");
    const clap_plugin_t* metadataPlugin = factory
        ? factory->create_plugin(factory, &metadataHost.host, kPluginId)
        : nullptr;
    ok &= check(metadataPlugin && metadataPlugin->init(metadataPlugin),
        "metadata plugin creation failed");
    if (metadataPlugin) {
        const auto* params = static_cast<const clap_plugin_params_t*>(
            metadataPlugin->get_extension(metadataPlugin, CLAP_EXT_PARAMS));
        uint32_t trimCount = 0u;
        uint32_t routeCount = 0u;
        bool hasLayout = false;
        bool hasModulatorSource = false;
        bool hasMicGain = false;
        bool hasMatrixMorph = false;
        bool hasAnalysisSlope = false;
        bool hasPitchSource = false;
        bool hasPitchScaleRoot = false;
        bool hasPitchScale = false;
        bool hasPitchHold = false;
        bool stableIdOrder = true;
        bool defaultsMatchInitialState = true;
        const uint32_t parameterCount = params && params->count
            ? params->count(metadataPlugin) : 0u;
        for (uint32_t index = 0u; index < parameterCount; ++index) {
            clap_param_info_t info {};
            if (!params->get_info(metadataPlugin, index, &info)) continue;
            stableIdOrder &= info.id == index + 1u;
            double initialValue = std::numeric_limits<double>::quiet_NaN();
            const bool defaultMatches = params->get_value(
                    metadataPlugin, info.id, &initialValue)
                && std::abs(initialValue - info.default_value) < 1.0e-6;
            if (!defaultMatches && defaultsMatchInitialState) {
                std::cerr << "first initial/default mismatch: id=" << info.id
                          << " name=" << info.name << " current="
                          << initialValue << " default="
                          << info.default_value << '\n';
            }
            defaultsMatchInitialState &= defaultMatches;
            trimCount += info.id >= 108u && info.id <= 129u;
            routeCount += info.id >= 130u && info.id <= 1097u;
            hasLayout |= info.id == 87u
                && std::strcmp(info.name, "Band Layout") == 0;
            hasModulatorSource |= info.id == 98u
                && std::strcmp(info.name, "Modulator Source") == 0
                && info.min_value == 0.0 && info.max_value == 2.0
                && info.default_value == 0.0
                && (info.flags & CLAP_PARAM_IS_STEPPED) != 0u;
            hasMicGain |= info.id == 99u
                && std::strcmp(info.name, "Mic Gain") == 0
                && info.min_value == -24.0 && info.max_value == 24.0
                && info.default_value == 0.0;
            hasMatrixMorph |= info.id == 107u
                && std::strcmp(info.name, "Matrix A / B") == 0;
            hasAnalysisSlope |= info.id == 1098u
                && std::strcmp(info.name, "Analysis Slope") == 0
                && info.min_value == 0.0 && info.max_value == 1.0
                && info.default_value == 1.0;
            hasPitchSource |= info.id == 1099u
                && std::strcmp(info.name, "Carrier Pitch Source") == 0
                && info.min_value == 0.0 && info.max_value == 1.0
                && info.default_value == 0.0;
            hasPitchScaleRoot |= info.id == 1100u
                && std::strcmp(info.name, "Scale Root") == 0
                && info.min_value == 0.0 && info.max_value == 11.0;
            hasPitchScale |= info.id == 1101u
                && std::strcmp(info.name, "Pitch Scale") == 0
                && info.min_value == 0.0 && info.max_value == 101.0
                && info.default_value == 1.0;
            hasPitchHold |= info.id == 1102u
                && std::strcmp(info.name, "Pitch Hold") == 0
                && info.min_value == 20.0 && info.max_value == 2000.0
                && info.default_value == 350.0;
        }
        ok &= check(parameterCount == 1102u && trimCount == 22u
                && routeCount == 968u && hasLayout && hasModulatorSource
                && hasMicGain && hasMatrixMorph && hasAnalysisSlope
                && hasPitchSource && hasPitchScaleRoot && hasPitchScale
                && hasPitchHold && stableIdOrder
                && defaultsMatchInitialState,
            "expanded 22-band parameter surface failed");
        char display[64] {};
        double parsed = -1.0;
        ok &= check(params && params->value_to_text
                && params->value_to_text(metadataPlugin, 98u, 0.0,
                    display, sizeof(display))
                && std::strcmp(display, "External Mic") == 0
                && params->value_to_text(metadataPlugin, 98u, 1.0,
                    display, sizeof(display))
                && std::strcmp(display, "Internal Speech") == 0
                && params->value_to_text(metadataPlugin, 98u, 2.0,
                    display, sizeof(display))
                && std::strcmp(display, "Blend") == 0
                && params->text_to_value
                && params->text_to_value(metadataPlugin, 98u,
                    "External Mic", &parsed)
                && parsed == 0.0,
            "modulator-source text conversion failed");
        ok &= check(params->value_to_text(metadataPlugin, 1098u, 1.0,
                    display, sizeof(display))
                && std::strcmp(display, "8 Pole") == 0
                && params->value_to_text(metadataPlugin, 1099u, 1.0,
                    display, sizeof(display))
                && std::strcmp(display, "Voice Pitch") == 0
                && params->value_to_text(metadataPlugin, 1100u, 9.0,
                    display, sizeof(display))
                && std::strcmp(display, "A") == 0
                && params->value_to_text(metadataPlugin, 1101u, 2.0,
                    display, sizeof(display))
                && std::strcmp(display, "MAJOR") == 0,
            "analysis/pitch menu text conversion failed");
        parsed = -1.0;
        ok &= check(params->text_to_value(metadataPlugin, 1101u,
                    "WHOLE TONE", &parsed)
                && parsed == 8.0
                && params->value_to_text(metadataPlugin, 1101u, parsed,
                    display, sizeof(display))
                && std::strcmp(display, "WHOLE TONE") == 0,
            "expanded musical-scale conversion failed");
        parsed = -1.0;
        ok &= check(params->value_to_text(metadataPlugin, 1102u, 2000.0,
                    display, sizeof(display))
                && std::strcmp(display, "Infinite") == 0
                && params->text_to_value(metadataPlugin, 1102u,
                    "Infinite", &parsed)
                && parsed == 2000.0,
            "infinite pitch-hold text conversion failed");
        ok &= check(params->value_to_text(metadataPlugin, 1u, 14.0,
                    display, sizeof(display))
                && std::strcmp(display, "Classic Mic") == 0
                && params->value_to_text(metadataPlugin, 1u, 15.0,
                    display, sizeof(display))
                && std::strcmp(display, "Formant Glide") == 0
                && params->value_to_text(metadataPlugin, 1u, 23.0,
                    display, sizeof(display))
                && std::strcmp(display, "Vocal Alloy") == 0
                && params->value_to_text(metadataPlugin, 1u, 24.0,
                    display, sizeof(display))
                && std::strcmp(display, "Custom") == 0,
            "matrix-first profile labels failed");
        double initialProfile = -1.0;
        double initialSource = -1.0;
        double initialAmount = -1.0;
        double initialMode = -1.0;
        double initialOpen = -1.0;
        double initialFreeze = -1.0;
        double initialThru = -1.0;
        ok &= check(params->get_value(metadataPlugin, 1u, &initialProfile)
                && params->get_value(metadataPlugin, 98u, &initialSource)
                && params->get_value(metadataPlugin, 65u, &initialAmount)
                && params->get_value(metadataPlugin, 66u, &initialMode)
                && params->get_value(metadataPlugin, 94u, &initialOpen)
                && params->get_value(metadataPlugin, 83u, &initialFreeze)
                && params->get_value(metadataPlugin, 96u, &initialThru)
                && initialProfile == 14.0 && initialSource == 0.0
                && initialAmount == 1.0 && initialMode == 0.0
                && initialOpen == 0.0 && initialFreeze == 0.0
                && initialThru == 0.0,
            "new instance did not start in the strict Classic Mic topology");

        const auto flushParam = [&](clap_id id, double value) {
            InputEvents events;
            events.addParam(id, value);
            params->flush(metadataPlugin, &events.input, nullptr);
        };
        const auto expectValuesRescan = [&](uint32_t callbackBefore,
                                             uint32_t rescanBefore,
                                             const char* message) {
            bool valid = metadataHost.callbackRequests > callbackBefore;
            metadataPlugin->on_main_thread(metadataPlugin);
            valid = valid && metadataHost.paramRescans == rescanBefore + 1u
                && (metadataHost.lastRescanFlags
                    & CLAP_PARAM_RESCAN_VALUES) != 0u;
            return check(valid, message);
        };

        uint32_t callbackBefore = metadataHost.callbackRequests;
        uint32_t rescanBefore = metadataHost.paramRescans;
        flushParam(1u, 6.0); // multi-parameter factory profile
        ok &= expectValuesRescan(callbackBefore, rescanBefore,
            "factory profile did not request a main-thread value rescan");
        double profile = -1.0;
        ok &= check(params->get_value(metadataPlugin, 1u, &profile)
                && profile == 6.0,
            "factory profile selection was not published");

        const auto* state = static_cast<const clap_plugin_state_t*>(
            metadataPlugin->get_extension(metadataPlugin, CLAP_EXT_STATE));
        MemoryState savedState;
        ok &= check(state && state->save
                && state->save(metadataPlugin, &savedState.output),
            "host rescan state fixture could not be saved");

        callbackBefore = metadataHost.callbackRequests;
        rescanBefore = metadataHost.paramRescans;
        flushParam(99u, 6.0); // implicit Profile -> Custom
        ok &= expectValuesRescan(callbackBefore, rescanBefore,
            "implicit Custom profile did not request a value rescan");
        ok &= check(params->get_value(metadataPlugin, 1u, &profile)
                && profile == 24.0,
            "scalar edit did not publish the Custom profile");

        callbackBefore = metadataHost.callbackRequests;
        rescanBefore = metadataHost.paramRescans;
        savedState.readOffset = 0u;
        ok &= check(state && state->load
                && state->load(metadataPlugin, &savedState.input),
            "host rescan state fixture could not be loaded");
        ok &= expectValuesRescan(callbackBefore, rescanBefore,
            "state load did not request a main-thread value rescan");
        ok &= check(params->get_value(metadataPlugin, 1u, &profile)
                && profile == 6.0,
            "state load did not restore the factory profile value");

        // Version 20 used slot 15 for Custom. Version 21 appends matrix-first
        // profiles at 15--23 and moves Custom to 24 without changing payload
        // width or any routing value.
        MemoryState version20State;
        version20State.bytes = savedState.bytes;
        const uint32_t version20 = 20u;
        const double version20Custom = 15.0;
        if (version20State.bytes.size() >= 8u + sizeof(double)) {
            std::memcpy(version20State.bytes.data(), &version20,
                sizeof(version20));
            std::memcpy(version20State.bytes.data() + 8u,
                &version20Custom, sizeof(version20Custom));
        }
        version20State.readOffset = 0u;
        ok &= check(state->load(metadataPlugin, &version20State.input)
                && params->get_value(metadataPlugin, 1u, &profile)
                && profile == 24.0,
            "v20 Custom profile slot migration failed");

        // Version 18 used IDs 98/99 for external-carrier mix/gain and slot
        // 14 for Custom. Exercise the binary-compatible payload migration so
        // those values cannot silently become mic routing in existing songs.
        MemoryState legacyState;
        legacyState.bytes = savedState.bytes;
        constexpr uint32_t legacySavedParamCount = 1096u;
        constexpr uint32_t currentSavedParamCount = 1101u;
        const size_t legacyParamEnd = 8u
            + static_cast<size_t>(legacySavedParamCount) * sizeof(double);
        const size_t currentParamEnd = 8u
            + static_cast<size_t>(currentSavedParamCount) * sizeof(double);
        if (legacyState.bytes.size() >= currentParamEnd) {
            legacyState.bytes.erase(
                legacyState.bytes.begin() + legacyParamEnd,
                legacyState.bytes.begin() + currentParamEnd);
        }
        const auto writeLegacyParam = [&](clap_id id, double value) {
            const uint32_t savedIndex = id < 25u ? id - 1u : id - 2u;
            const size_t offset = 8u
                + static_cast<size_t>(savedIndex) * sizeof(double);
            if (offset + sizeof(value) > legacyState.bytes.size()) {
                return false;
            }
            std::memcpy(legacyState.bytes.data() + offset,
                &value, sizeof(value));
            return true;
        };
        const uint32_t version18 = 18u;
        const bool legacyFixtureValid = legacyState.bytes.size() >= 8u;
        if (legacyFixtureValid) {
            std::memcpy(legacyState.bytes.data(), &version18,
                sizeof(version18));
        }
        ok &= check(legacyFixtureValid
                && writeLegacyParam(1u, 14.0)
                && writeLegacyParam(98u, 0.73)
                && writeLegacyParam(99u, 12.0),
            "v18 migration fixture construction failed");
        callbackBefore = metadataHost.callbackRequests;
        rescanBefore = metadataHost.paramRescans;
        legacyState.readOffset = 0u;
        ok &= check(state && state->load
                && state->load(metadataPlugin, &legacyState.input),
            "v18 state migration failed to load");
        ok &= expectValuesRescan(callbackBefore, rescanBefore,
            "v18 migration did not request a value rescan");
        double modulatorSource = -1.0;
        double micGain = -99.0;
        ok &= check(params->get_value(metadataPlugin, 1u, &profile)
                && params->get_value(metadataPlugin, 98u,
                    &modulatorSource)
                && params->get_value(metadataPlugin, 99u, &micGain)
                && profile == 24.0 && modulatorSource == 1.0
                && micGain == 0.0,
            "v18 carrier controls were reinterpreted as mic routing");

        // Version 19 already used the mic-modulator topology but predates the
        // appended analyzer/pitch controls. It must load at the old payload
        // width and initialize every new control from its v20 default.
        MemoryState version19State;
        version19State.bytes = legacyState.bytes;
        const uint32_t version19 = 19u;
        std::memcpy(version19State.bytes.data(), &version19,
            sizeof(version19));
        version19State.readOffset = 0u;
        ok &= check(state->load(metadataPlugin, &version19State.input),
            "v19 state migration failed to load");
        double analysisSlope = -1.0;
        double pitchSource = -1.0;
        double scaleRoot = -1.0;
        double pitchScale = -1.0;
        double pitchHold = -1.0;
        ok &= check(params->get_value(metadataPlugin, 1098u, &analysisSlope)
                && params->get_value(metadataPlugin, 1099u, &pitchSource)
                && params->get_value(metadataPlugin, 1100u, &scaleRoot)
                && params->get_value(metadataPlugin, 1101u, &pitchScale)
                && params->get_value(metadataPlugin, 1102u, &pitchHold)
                && analysisSlope == 1.0 && pitchSource == 0.0
                && scaleRoot == 0.0 && pitchScale == 1.0
                && pitchHold == 350.0,
            "v19 did not initialize appended analysis/pitch controls");

        callbackBefore = metadataHost.callbackRequests;
        rescanBefore = metadataHost.paramRescans;
        flushParam(1u, 14.0);
        ok &= expectValuesRescan(callbackBefore, rescanBefore,
            "Classic Mic profile did not request a value rescan");
        double phraseEngine = -1.0;
        double bankMode = -1.0;
        double articulationThru = -1.0;
        ok &= check(params->get_value(metadataPlugin, 1u, &profile)
                && params->get_value(metadataPlugin, 98u,
                    &modulatorSource)
                && params->get_value(metadataPlugin, 99u, &micGain)
                && params->get_value(metadataPlugin, 51u, &phraseEngine)
                && params->get_value(metadataPlugin, 66u, &bankMode)
                && params->get_value(metadataPlugin, 96u,
                    &articulationThru)
                && profile == 14.0 && modulatorSource == 0.0
                && micGain == 0.0 && phraseEngine == 0.0
                && bankMode == 0.0 && articulationThru == 0.0,
            "Classic Mic profile did not select the external mic");

        const auto* audioPorts =
            static_cast<const clap_plugin_audio_ports_t*>(
                metadataPlugin->get_extension(
                    metadataPlugin, CLAP_EXT_AUDIO_PORTS));
        clap_audio_port_info_t input {};
        clap_audio_port_info_t output {};
        ok &= check(audioPorts
                && audioPorts->count(metadataPlugin, true) == 1u
                && audioPorts->count(metadataPlugin, false) == 1u
                && audioPorts->get(metadataPlugin, 0u, true, &input)
                && audioPorts->get(metadataPlugin, 0u, false, &output)
                && input.id == 10u && output.id == 20u
                && input.channel_count == kChannels
                && output.channel_count == kChannels
                && input.port_type && output.port_type
                && std::strcmp(input.port_type, CLAP_PORT_STEREO) == 0
                && std::strcmp(output.port_type, CLAP_PORT_STEREO) == 0
                && (input.flags & CLAP_AUDIO_PORT_IS_MAIN) != 0u
                && (output.flags & CLAP_AUDIO_PORT_IS_MAIN) != 0u
                && (input.flags & CLAP_AUDIO_PORT_SUPPORTS_64BITS) != 0u
                && (output.flags & CLAP_AUDIO_PORT_SUPPORTS_64BITS) != 0u
                && input.in_place_pair == output.id
                && output.in_place_pair == input.id
                && std::strcmp(input.name, "Modulator In") == 0
                && std::strcmp(output.name, "Formant Matrix Out") == 0,
            "stereo main audio-port contract failed");

        const auto* notePorts =
            static_cast<const clap_plugin_note_ports_t*>(
                metadataPlugin->get_extension(
                    metadataPlugin, CLAP_EXT_NOTE_PORTS));
        clap_note_port_info_t note {};
        ok &= check(notePorts
                && notePorts->count(metadataPlugin, true) == 1u
                && notePorts->count(metadataPlugin, false) == 0u
                && notePorts->get(metadataPlugin, 0u, true, &note)
                && (note.supported_dialects & CLAP_NOTE_DIALECT_CLAP) != 0u
                && (note.supported_dialects & CLAP_NOTE_DIALECT_MIDI) != 0u,
            "held-note input-port contract failed");
        metadataPlugin->destroy(metadataPlugin);
    }

    const RenderResult internalAbsent = render(
        factory, SampleFormat::Float32, false, false);
    const RenderResult internalConnected = render(
        factory, SampleFormat::Float32, true, false);
    const RenderResult micAbsent = render(
        factory, SampleFormat::Float32, false, false, 0u,
        ModulatorSource::ExternalMic);
    const RenderResult mic32 = render(
        factory, SampleFormat::Float32, true, false, 0u,
        ModulatorSource::ExternalMic);
    const RenderResult micSilent = render(
        factory, SampleFormat::Float32, true, false, 0u,
        ModulatorSource::ExternalMic, false, 0.0, false);
    const RenderResult creativeMicAbsent = renderSilentHeldExternal(
        factory, RenderSetup::CreativeExternalMic, false);
    const RenderResult creativeMicSilent = renderSilentHeldExternal(
        factory, RenderSetup::CreativeExternalMic, true);
    const RenderResult classicMicAbsent = renderSilentHeldExternal(
        factory, RenderSetup::ClassicMic, false);
    const RenderResult classicMicSilent = renderSilentHeldExternal(
        factory, RenderSetup::ClassicMic, true);
    const RenderResult dynamicMic = renderDynamicHeldExternal(factory, false);
    const RenderResult dynamicFrozenMic = renderDynamicHeldExternal(
        factory, true);
    const RenderResult noiseMinus66 = renderDynamicHeldExternal(
        factory, false, dbfsToAmplitude(-66.0f));
    const RenderResult noiseMinus60 = renderDynamicHeldExternal(
        factory, false, dbfsToAmplitude(-60.0f));
    const RenderResult noiseMinus54 = renderDynamicHeldExternal(
        factory, false, dbfsToAmplitude(-54.0f));
    const RenderResult noiseMinus48 = renderDynamicHeldExternal(
        factory, false, dbfsToAmplitude(-48.0f));
    const RenderResult sourceAutomation = renderSourceAutomation(factory);
    const DynamicMicMetrics dynamic = dynamicMicMetrics(dynamicMic, true);
    const DynamicMicMetrics frozen = dynamicMicMetrics(
        dynamicFrozenMic, false);
    const double frozenCaptureDifference = windowRmsDifference(
        dynamicMic, dynamicFrozenMic,
        kDynamicVoiceBlocks * 3u / 4u, kDynamicVoiceBlocks);
    const double noiseMinus66Rms = windowRms(noiseMinus66,
        kDynamicVoiceBlocks + kDynamicSilenceBlocks - 64u,
        kDynamicVoiceBlocks + kDynamicSilenceBlocks);
    const double noiseMinus60Rms = windowRms(noiseMinus60,
        kDynamicVoiceBlocks + kDynamicSilenceBlocks - 64u,
        kDynamicVoiceBlocks + kDynamicSilenceBlocks);
    const double noiseMinus54Rms = windowRms(noiseMinus54,
        kDynamicVoiceBlocks + kDynamicSilenceBlocks - 64u,
        kDynamicVoiceBlocks + kDynamicSilenceBlocks);
    const double noiseMinus48Rms = windowRms(noiseMinus48,
        kDynamicVoiceBlocks + kDynamicSilenceBlocks - 64u,
        kDynamicVoiceBlocks + kDynamicSilenceBlocks);
    const double sourceInternalBeforeRms = windowRms(sourceAutomation,
        64u, kSourceExternalBlock);
    const double sourceExternalLateRms = windowRms(sourceAutomation,
        kSourceInternalBlock - 16u, kSourceInternalBlock);
    const double sourceInternalAfterRms = windowRms(sourceAutomation,
        kSourceInternalBlock + 64u, kSourceAutomationBlocks);
    const double sourceToExternalStep = maximumStepNearBlock(
        sourceAutomation, kSourceExternalBlock);
    const double sourceToInternalStep = maximumStepNearBlock(
        sourceAutomation, kSourceInternalBlock);
    const RenderResult micSilentFuzz = render(
        factory, SampleFormat::Float32, true, false, 0u,
        ModulatorSource::ExternalMic, false, 0.0, false, true);
    const RenderResult micNoMidi = render(
        factory, SampleFormat::Float32, true, false, 0u,
        ModulatorSource::ExternalMic, false, 0.0, true, false, false);
    const RenderResult micNoMidiNoise = render(
        factory, SampleFormat::Float32, true, false, 0u,
        ModulatorSource::ExternalMic, false, 0.0, true, false, false,
        1.0f, true);
    const RenderResult micInverted = render(
        factory, SampleFormat::Float32, true, false, 0u,
        ModulatorSource::ExternalMic, false, 0.0, true, false, true, -1.0f);
    const RenderResult mic64 = render(
        factory, SampleFormat::Float64, true, false, 0u,
        ModulatorSource::ExternalMic);
    const RenderResult micBoost = render(
        factory, SampleFormat::Float32, true, false, 0u,
        ModulatorSource::ExternalMic, false, 12.0);
    const RenderResult micInPlace32 = render(
        factory, SampleFormat::Float32, true, true, 0u,
        ModulatorSource::ExternalMic);
    const RenderResult micInPlace64 = render(
        factory, SampleFormat::Float64, true, true, 0u,
        ModulatorSource::ExternalMic);
    const RenderResult blend = render(
        factory, SampleFormat::Float32, true, false, 0u,
        ModulatorSource::Blend);
    const RenderResult blendAbsent = render(
        factory, SampleFormat::Float32, false, false, 0u,
        ModulatorSource::Blend);
    const RenderResult voicePitchCarrier = renderVoicePitchCarrier(factory);
    const RenderResult micPhraseOff = render(
        factory, SampleFormat::Float32, true, false, 0u,
        ModulatorSource::ExternalMic, true);
    const RenderResult zeroVelocityClapNoteOn = render(
        factory, SampleFormat::Float32, false, false,
        CLAP_EVENT_NOTE_ON);
    const RenderResult explicitClapNoteOff = render(
        factory, SampleFormat::Float32, false, false,
        CLAP_EVENT_NOTE_OFF);
    const double externalPhraseDifference = lateRmsDifference(
        mic32, micPhraseOff);
    ok &= check(internalAbsent.ok && internalAbsent.energy > 1.0e-8
            && internalAbsent.peak > 1.0e-5,
        "internal speech was silent without a mic input");
    ok &= check(internalConnected.ok
            && rmsDifference(internalAbsent, internalConnected) < 1.0e-6,
        "Internal Speech mode was contaminated by the mic input");
    ok &= check(micAbsent.ok && mic32.ok && mic32.energy > 1.0e-8
            && rmsDifference(micAbsent, mic32) > 1.0e-4,
        "External Mic mode did not analyze the audio input");
    ok &= check(micSilent.ok
            && rmsDifference(micAbsent, micSilent) < 1.0e-7,
        "an explicitly silent mic triggered internal-speech fallback");
    ok &= check(inaudible(micAbsent) && inaudible(micSilent),
        "fully wet Vocoder leaked its held carrier without mic articulation");
    ok &= check(inaudible(creativeMicAbsent)
            && inaudible(creativeMicSilent),
        "External Mic leaked its held carrier with creative Bank Mix/Open/Mode");
    ok &= check(inaudible(classicMicAbsent)
            && inaudible(classicMicSilent),
        "Classic Mic leaked its held carrier without mic articulation");
    ok &= check(dynamic.ok
            && dynamic.firstVoiceRms > 1.0e-3
            && dynamic.silentLateRms < 1.0e-7
            && dynamic.recoveredVoiceRms > 1.0e-3
            && dynamic.recoveredVoiceRms
                > dynamic.firstVoiceRms * 0.20
            && dynamic.closeMaximumStep < 0.045
            && dynamic.reopenMaximumStep < 0.045,
        "held External Mic failed voiced-silence-recovery gating or clicked");
    ok &= check(frozen.ok
            && frozen.firstVoiceRms > 1.0e-3
            && frozenCaptureDifference > frozen.firstVoiceRms * 0.0025
            && frozen.silentLateRms < 1.0e-7
            && frozen.closeMaximumStep < 0.045,
        "External Mic Freeze sustained its captured bank after the mic stopped");
    // -66 through -48 dBFS model interface/room noise below the documented
    // hysteretic opening threshold. Require the resulting output below
    // -140 dBFS RMS, far under a 24-bit converter's practical noise floor;
    // the following recovery window proves real mic signal still reopens it.
    ok &= check(noiseMinus66.ok && noiseMinus60.ok
            && noiseMinus54.ok && noiseMinus48.ok
            && noiseMinus66Rms < kInaudibleOutputRms
            && noiseMinus60Rms < kInaudibleOutputRms
            && noiseMinus54Rms < kInaudibleOutputRms
            && noiseMinus48Rms < kInaudibleOutputRms
            && dynamic.recoveredVoiceRms > 1.0e-3,
        "External Mic noise-floor rejection opened or blocked real-voice recovery");
    ok &= check(sourceAutomation.ok
            && sourceInternalBeforeRms > 1.0e-3
            // Source switching also releases the selected speech analyzer.
            // Require it below -120 dBFS after the long External hold while
            // the dedicated digital-silence/noise tests retain the stricter
            // exact anti-drone floor.
            && sourceExternalLateRms < 1.0e-6
            && sourceInternalAfterRms > 1.0e-3
            && sourceToExternalStep < 0.045
            && sourceToInternalStep < 0.045,
        "Modulator Source automation clicked or failed Internal/External gating");
    // Bank Mix itself is smoothed from the creative topology's 0.90 value when this
    // test automates it at the note boundary, so assess the settled vocoder
    // rather than mistaking that intentional dry/wet transition for leakage.
    ok &= check(micSilentFuzz.ok && micSilentFuzz.lateEnergy < 1.0e-10,
        "post-bank shape effects leaked the carrier under a silent mic");
    ok &= check(micNoMidi.ok && micNoMidi.energy < 1.0e-10
            && micNoMidi.peak < 1.0e-6,
        "external microphone leaked through without a MIDI carrier");
    ok &= check(micNoMidiNoise.ok && micNoMidiNoise.energy < 1.0e-10
            && micNoMidiNoise.peak < 1.0e-6,
        "microphone drove the noise carrier without a MIDI note");
    ok &= check(micInverted.ok
            && lateRmsDifference(mic32, micInverted) < 1.0e-6,
        "external microphone polarity leaked into the vocoder output");
    ok &= check(mic64.ok && mic64.energy > 1.0e-8,
        "64-bit external-mic processing failed");
    ok &= check(micBoost.ok && rmsDifference(mic32, micBoost) > 1.0e-4,
        "Mic Gain did not affect external modulation");
    ok &= check(micInPlace32.ok && micInPlace64.ok,
        "in-place processing failed");
    ok &= check(blend.ok
            && rmsDifference(internalConnected, blend) > 1.0e-4,
        "Blend mode did not add external mic articulation");
    ok &= check(blendAbsent.ok
            && rmsDifference(internalAbsent, blendAbsent) < 1.0e-6,
        "Blend without an input did not fall back to Internal Speech");
    ok &= check(voicePitchCarrier.ok && !voicePitchCarrier.slept
            && voicePitchCarrier.energy > 1.0e-4
            && lateRms(voicePitchCarrier) > 1.0e-3,
        "Voice Pitch did not sustain a no-MIDI carrier from the mic");
    ok &= check(micPhraseOff.ok && micPhraseOff.energy > 1.0e-8
            && micPhraseOff.peak > 1.0e-5,
        "external mic lost the MIDI carrier with the phrase engine off");
    ok &= check(externalPhraseDifference < 1.0e-6,
        "internal phrase metadata contaminated External Mic mode");
    ok &= check(zeroVelocityClapNoteOn.ok && explicitClapNoteOff.ok
            && zeroVelocityClapNoteOn.lateEnergy
                > explicitClapNoteOff.lateEnergy * 8.0
            && rmsDifference(internalAbsent, zeroVelocityClapNoteOn) < 1.0e-6,
        "velocity-zero CLAP note-on was interpreted as a note-off");

    const double externalDifference = rmsDifference(micAbsent, mic32);
    const double precisionDifference = rmsDifference(mic32, mic64);
    const double inPlace32Difference = rmsDifference(mic32, micInPlace32);
    const double inPlace64Difference = rmsDifference(mic64, micInPlace64);
    ok &= check(std::isfinite(precisionDifference)
            && precisionDifference < 1.0e-5,
        "32-bit and 64-bit processing diverged");
    ok &= check(std::isfinite(inPlace32Difference)
            && inPlace32Difference < 1.0e-6
            && std::isfinite(inPlace64Difference)
            && inPlace64Difference < 1.0e-6,
        "in-place and out-of-place processing diverged");

    std::cout << "Dynamic External Mic metrics: voice/silent/recovery RMS "
              << dynamic.firstVoiceRms << '/' << dynamic.silentLateRms
              << '/' << dynamic.recoveredVoiceRms
              << ", close/reopen max step "
              << dynamic.closeMaximumStep << '/'
              << dynamic.reopenMaximumStep
              << ", Freeze voice/silent RMS/max step "
              << frozen.firstVoiceRms << '/' << frozen.silentLateRms
              << '/' << frozen.closeMaximumStep
              << ", captured-vs-live RMS difference "
              << frozenCaptureDifference
              << ", -66/-60/-54/-48 dBFS noise output RMS "
              << noiseMinus66Rms << '/' << noiseMinus60Rms << '/'
              << noiseMinus54Rms << '/' << noiseMinus48Rms
              << ", source internal/external/internal RMS "
              << sourceInternalBeforeRms << '/'
              << sourceExternalLateRms << '/'
              << sourceInternalAfterRms
              << ", source switch max steps "
              << sourceToExternalStep << '/' << sourceToInternalStep << '\n';
    if (!ok) {
        std::cerr << "Formant Matrix render metrics: absent-energy="
            << internalAbsent.energy << " mic-energy=" << mic32.energy
            << " mic-absent-energy=" << micAbsent.energy
            << " creative-silent-rms/onset/late/peak="
            << rms(creativeMicSilent) << '/'
            << onsetRms(creativeMicSilent) << '/'
            << lateRms(creativeMicSilent) << '/'
            << creativeMicSilent.peak
            << " classic-silent-rms/onset/late/peak="
            << rms(classicMicSilent) << '/'
            << onsetRms(classicMicSilent) << '/'
            << lateRms(classicMicSilent) << '/'
            << classicMicSilent.peak
            << " dynamic-voice/silent/recovery/steps="
            << dynamic.firstVoiceRms << '/'
            << dynamic.silentLateRms << '/'
            << dynamic.recoveredVoiceRms << '/'
            << dynamic.closeMaximumStep << '/'
            << dynamic.reopenMaximumStep
            << " freeze-voice/silent/step="
            << frozen.firstVoiceRms << '/'
            << frozen.silentLateRms << '/'
            << frozen.closeMaximumStep << '/'
            << frozenCaptureDifference
            << " noise-66/60/54/48-rms=" << noiseMinus66Rms << '/'
            << noiseMinus60Rms << '/' << noiseMinus54Rms << '/'
            << noiseMinus48Rms
            << " source-rms/steps=" << sourceInternalBeforeRms << '/'
            << sourceExternalLateRms << '/' << sourceInternalAfterRms << '/'
            << sourceToExternalStep << '/' << sourceToInternalStep
            << " voice-pitch rms/late/slept="
            << rms(voicePitchCarrier) << '/'
            << lateRms(voicePitchCarrier) << '/'
            << voicePitchCarrier.slept
            << " mic-diff=" << externalDifference
            << " phrase-diff=" << externalPhraseDifference
            << " silent-fuzz-late=" << micSilentFuzz.lateEnergy
            << " no-midi-energy/peak=" << micNoMidi.energy << '/'
            << micNoMidi.peak
            << " inverted-late-diff="
            << lateRmsDifference(mic32, micInverted)
            << " f32/f64-diff=" << precisionDifference
            << " inplace32-diff=" << inPlace32Difference
            << " inplace64-diff=" << inPlace64Difference << '\n';
    }

    entry->deinit();
    dlclose(handle);
    if (!ok) return 1;
    std::cout << "Formant Matrix CLAP host smoke passed: external mic, "
                 "internal speech, blend, procedural MIDI carrier, f32/f64, "
                 "and in-place paths\n";
    return 0;
}
