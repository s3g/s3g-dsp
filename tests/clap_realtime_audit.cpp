#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/note-ports.h>
#include <clap/ext/params.h>
#include <clap/ext/tail.h>

#include "realtime_alloc_probe_api.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr const char* kSchema =
    "org.s3g.s3g-dsp.clap-realtime-audit/v1";

struct Options {
    std::vector<double> sampleRates { 48000.0 };
    std::vector<uint32_t> blockSizes { 64u, 256u };
    uint32_t warmupBlocks = 128u;
    uint32_t iterations = 1000u;
    std::vector<uint32_t> eventBursts { 64u };
    bool distributedAutomation = false;
    bool controlPublicationStress = false;
    bool nimMidiFlood = false;
    bool allocationProbe = false;
    std::filesystem::path jsonPath;
    std::filesystem::path suppliedPath;
    std::string pluginId;
};

[[noreturn]] void usageError(const std::string& message)
{
    throw std::runtime_error(message
        + "\nusage: s3g_clap_realtime_audit [options] "
          "<bundle-or-binary> <plugin-id>\n"
          "  --sample-rates CSV   default: 48000\n"
          "  --blocks CSV         default: 64,256\n"
          "  --warmup N           default: 128\n"
          "  --iterations N       default: 1000\n"
          "  --event-burst N      default: 64; 0 disables automation burst\n"
          "  --event-bursts CSV   automation ladder (for example 1,4,8,16,64)\n"
          "  --distributed-events add a sample-distributed version of each burst\n"
          "  --control-publication-stress\n"
          "                       publish control snapshots from an independent\n"
          "                       thread; only process() calls plugin APIs\n"
          "  --nim-midi-flood     add the NIM E16/BU16 MIDI 1 stress scenario\n"
          "  --allocation-probe  measure callback-thread allocations; requires "
          "an injected probe dylib\n"
          "  --json PATH          write a machine-readable report\n");
}

uint32_t parseUint(const std::string& text, const char* option,
    bool allowZero = false)
{
    size_t consumed = 0u;
    unsigned long value = 0u;
    try {
        value = std::stoul(text, &consumed, 10);
    } catch (...) {
        usageError(std::string(option) + " expects an integer");
    }
    if (consumed != text.size() || value > std::numeric_limits<uint32_t>::max()
        || (!allowZero && value == 0u)) {
        usageError(std::string(option) + " has an invalid value: " + text);
    }
    return static_cast<uint32_t>(value);
}

std::vector<std::string> splitCsv(const std::string& text, const char* option)
{
    std::vector<std::string> fields;
    size_t begin = 0u;
    while (begin <= text.size()) {
        const size_t end = text.find(',', begin);
        const std::string field = text.substr(begin,
            end == std::string::npos ? std::string::npos : end - begin);
        if (field.empty()) usageError(std::string(option) + " contains an empty item");
        fields.push_back(field);
        if (end == std::string::npos) break;
        begin = end + 1u;
    }
    return fields;
}

std::vector<double> parseSampleRates(const std::string& text)
{
    std::vector<double> values;
    for (const auto& field : splitCsv(text, "--sample-rates")) {
        size_t consumed = 0u;
        double value = 0.0;
        try {
            value = std::stod(field, &consumed);
        } catch (...) {
            usageError("--sample-rates expects finite positive numbers");
        }
        if (consumed != field.size() || !std::isfinite(value) || value <= 0.0)
            usageError("--sample-rates has an invalid value: " + field);
        values.push_back(value);
    }
    return values;
}

std::vector<uint32_t> parseBlockSizes(const std::string& text)
{
    std::vector<uint32_t> values;
    for (const auto& field : splitCsv(text, "--blocks")) {
        const uint32_t frames = parseUint(field, "--blocks");
        if (frames > static_cast<uint32_t>(std::numeric_limits<int32_t>::max()))
            usageError("--blocks exceeds the CLAP INT32_MAX frame limit: "
                + field);
        values.push_back(frames);
    }
    return values;
}

std::vector<uint32_t> parseEventBursts(const std::string& text)
{
    std::vector<uint32_t> values;
    for (const auto& field : splitCsv(text, "--event-bursts")) {
        const uint32_t value = parseUint(field, "--event-bursts");
        if (std::find(values.begin(), values.end(), value) == values.end())
            values.push_back(value);
    }
    return values;
}

Options parseOptions(int argc, char** argv)
{
    Options options;
    std::vector<std::string> positional;
    bool sawEventBurst = false;
    bool sawEventBursts = false;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--help" || argument == "-h") {
            std::cout
                << "usage: s3g_clap_realtime_audit [options] "
                   "<bundle-or-binary> <plugin-id>\n"
                << "See --sample-rates, --blocks, --warmup, --iterations, "
                   "--event-burst, --event-bursts, --distributed-events, "
                   "--control-publication-stress, --nim-midi-flood, "
                   "--allocation-probe, and --json.\n";
            std::exit(0);
        }
        const auto requireValue = [&](const char* option) -> std::string {
            if (++index >= argc) usageError(std::string(option) + " needs a value");
            return argv[index];
        };
        if (argument == "--sample-rates") {
            options.sampleRates = parseSampleRates(requireValue("--sample-rates"));
        } else if (argument == "--blocks") {
            options.blockSizes = parseBlockSizes(requireValue("--blocks"));
        } else if (argument == "--warmup") {
            options.warmupBlocks = parseUint(requireValue("--warmup"), "--warmup", true);
        } else if (argument == "--iterations") {
            options.iterations = parseUint(requireValue("--iterations"), "--iterations");
        } else if (argument == "--event-burst") {
            if (sawEventBursts)
                usageError("--event-burst and --event-bursts are mutually exclusive");
            const uint32_t burst = parseUint(
                requireValue("--event-burst"), "--event-burst", true);
            options.eventBursts.clear();
            if (burst != 0u) options.eventBursts.push_back(burst);
            sawEventBurst = true;
        } else if (argument == "--event-bursts") {
            if (sawEventBurst)
                usageError("--event-burst and --event-bursts are mutually exclusive");
            options.eventBursts = parseEventBursts(
                requireValue("--event-bursts"));
            sawEventBursts = true;
        } else if (argument == "--distributed-events") {
            options.distributedAutomation = true;
        } else if (argument == "--control-publication-stress") {
            options.controlPublicationStress = true;
        } else if (argument == "--nim-midi-flood") {
            options.nimMidiFlood = true;
        } else if (argument == "--allocation-probe") {
            options.allocationProbe = true;
        } else if (argument == "--json") {
            options.jsonPath = requireValue("--json");
        } else if (!argument.empty() && argument.front() == '-') {
            usageError("unknown option: " + argument);
        } else {
            positional.push_back(argument);
        }
    }
    if (positional.size() != 2u)
        usageError("expected a bundle or binary path and a plugin identifier");
    options.suppliedPath = std::filesystem::absolute(positional[0]);
    options.pluginId = positional[1];
    return options;
}

class RealtimeAllocationProbe {
public:
    void initialize()
    {
        abiVersion_ = load<AbiVersion>("s3g_rt_alloc_probe_abi_version");
        begin_ = load<Begin>("s3g_rt_alloc_probe_begin");
        end_ = load<End>("s3g_rt_alloc_probe_end");
        read_ = load<Read>("s3g_rt_alloc_probe_read");

        const uint32_t version = abiVersion_();
        if (version != S3G_RT_ALLOC_PROBE_ABI_VERSION) {
            throw std::runtime_error("allocation probe ABI mismatch: audit expects "
                + std::to_string(S3G_RT_ALLOC_PROBE_ABI_VERSION)
                + ", injected dylib provides " + std::to_string(version));
        }

        // Resolve and touch the calling thread's TLS before any measured block.
        // This also verifies the full ABI rather than merely finding its names.
        begin_();
        end_();
        s3g_rt_alloc_probe_counts counts {};
        if (read_(&counts, sizeof(counts)) != 1
            || counts.abi_version != S3G_RT_ALLOC_PROBE_ABI_VERSION
            || counts.struct_size != sizeof(s3g_rt_alloc_probe_counts)) {
            throw std::runtime_error(
                "injected allocation probe returned an incompatible counter record");
        }
        enabled_ = true;
    }

    bool enabled() const noexcept { return enabled_; }
    void begin() const noexcept { begin_(); }
    void end() const noexcept { end_(); }
    bool read(s3g_rt_alloc_probe_counts& counts) const noexcept
    {
        return read_(&counts, sizeof(counts)) == 1;
    }

private:
    using AbiVersion = uint32_t (*)(void);
    using Begin = void (*)(void);
    using End = void (*)(void);
    using Read = int (*)(s3g_rt_alloc_probe_counts*, size_t);

    template <typename Function>
    static Function load(const char* name)
    {
        dlerror();
        void* address = dlsym(RTLD_DEFAULT, name);
        const char* error = dlerror();
        if (error != nullptr || address == nullptr) {
            throw std::runtime_error(std::string("--allocation-probe requires ")
                + "the probe dylib in DYLD_INSERT_LIBRARIES; missing symbol "
                + name + (error ? std::string(": ") + error : std::string()));
        }
        return reinterpret_cast<Function>(address);
    }

    AbiVersion abiVersion_ = nullptr;
    Begin begin_ = nullptr;
    End end_ = nullptr;
    Read read_ = nullptr;
    bool enabled_ = false;
};

std::string jsonEscape(const std::string& value)
{
    std::ostringstream output;
    for (const unsigned char byte : value) {
        switch (byte) {
        case '"': output << "\\\""; break;
        case '\\': output << "\\\\"; break;
        case '\b': output << "\\b"; break;
        case '\f': output << "\\f"; break;
        case '\n': output << "\\n"; break;
        case '\r': output << "\\r"; break;
        case '\t': output << "\\t"; break;
        default:
            if (byte < 0x20u) {
                output << "\\u" << std::hex << std::setw(4)
                       << std::setfill('0') << static_cast<unsigned>(byte)
                       << std::dec << std::setfill(' ');
            } else {
                output << static_cast<char>(byte);
            }
        }
    }
    return output.str();
}

std::string plistExecutableName(const std::filesystem::path& bundle)
{
#if defined(__APPLE__)
    const auto plist = bundle / "Contents" / "Info.plist";
    std::ifstream stream(plist, std::ios::binary);
    if (!stream) return {};
    const std::string contents((std::istreambuf_iterator<char>(stream)),
        std::istreambuf_iterator<char>());
    const std::string key = "<key>CFBundleExecutable</key>";
    const size_t keyAt = contents.find(key);
    if (keyAt == std::string::npos) return {};
    const size_t open = contents.find("<string>", keyAt + key.size());
    const size_t close = open == std::string::npos
        ? std::string::npos : contents.find("</string>", open + 8u);
    if (open == std::string::npos || close == std::string::npos) return {};
    return contents.substr(open + 8u, close - (open + 8u));
#else
    (void)bundle;
    return {};
#endif
}

std::filesystem::path resolveBinary(const std::filesystem::path& supplied)
{
    if (std::filesystem::is_regular_file(supplied)) return supplied;
#if defined(__APPLE__)
    if (std::filesystem::is_directory(supplied)) {
        const auto directory = supplied / "Contents" / "MacOS";
        const auto executableName = plistExecutableName(supplied);
        if (!executableName.empty()) {
            const auto candidate = directory / executableName;
            if (std::filesystem::is_regular_file(candidate)) return candidate;
        }
        std::vector<std::filesystem::path> candidates;
        if (std::filesystem::is_directory(directory)) {
            for (const auto& entry : std::filesystem::directory_iterator(directory)) {
                if (entry.is_regular_file()) candidates.push_back(entry.path());
            }
        }
        if (candidates.size() == 1u) return candidates.front();
    }
#endif
    return {};
}

std::filesystem::path resolveEntryPath(const std::filesystem::path& supplied,
    const std::filesystem::path& binary)
{
#if defined(__APPLE__)
    if (std::filesystem::is_regular_file(supplied)) {
        const auto macOS = binary.parent_path();
        const auto contents = macOS.parent_path();
        const auto bundle = contents.parent_path();
        if (macOS.filename() == "MacOS" && contents.filename() == "Contents"
            && bundle.extension() == ".clap"
            && std::filesystem::is_directory(bundle)) return bundle;
    }
#else
    (void)binary;
#endif
    return supplied;
}

struct HostContext {
    clap_host_t host {};
    clap_host_params_t params {};
    clap_host_audio_ports_t audioPorts {};
    clap_host_note_ports_t notePorts {};
    clap_host_tail_t tail {};
    std::atomic<uint64_t> restartRequests { 0u };
    std::atomic<uint64_t> processRequests { 0u };
    std::atomic<uint64_t> callbackRequests { 0u };
    std::atomic<uint64_t> parameterRescans { 0u };
    std::atomic<uint64_t> parameterClears { 0u };
    std::atomic<uint64_t> flushRequests { 0u };
    std::atomic<uint64_t> portRescans { 0u };
    std::atomic<uint64_t> notePortRescans { 0u };
    std::atomic<uint64_t> tailChanges { 0u };
    std::atomic<bool> callbackPending { false };

    HostContext();
};

HostContext* context(const clap_host_t* host)
{
    return static_cast<HostContext*>(host->host_data);
}

const void* hostGetExtension(const clap_host_t* host, const char* id)
{
    if (!id) return nullptr;
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) return &context(host)->params;
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0)
        return &context(host)->audioPorts;
    if (std::strcmp(id, CLAP_EXT_NOTE_PORTS) == 0)
        return &context(host)->notePorts;
    if (std::strcmp(id, CLAP_EXT_TAIL) == 0) return &context(host)->tail;
    return nullptr;
}

void hostRequestRestart(const clap_host_t* host)
{
    context(host)->restartRequests.fetch_add(1u, std::memory_order_relaxed);
}

void hostRequestProcess(const clap_host_t* host)
{
    context(host)->processRequests.fetch_add(1u, std::memory_order_relaxed);
}

void hostRequestCallback(const clap_host_t* host)
{
    context(host)->callbackRequests.fetch_add(1u, std::memory_order_relaxed);
    context(host)->callbackPending.store(true, std::memory_order_relaxed);
}

void hostParamsRescan(const clap_host_t* host, clap_param_rescan_flags)
{
    context(host)->parameterRescans.fetch_add(1u, std::memory_order_relaxed);
}

void hostParamsClear(const clap_host_t* host, clap_id, clap_param_clear_flags)
{
    context(host)->parameterClears.fetch_add(1u, std::memory_order_relaxed);
}

void hostParamsRequestFlush(const clap_host_t* host)
{
    context(host)->flushRequests.fetch_add(1u, std::memory_order_relaxed);
}

bool hostAudioPortsSupports(const clap_host_t*, uint32_t) { return false; }

void hostAudioPortsRescan(const clap_host_t* host, uint32_t)
{
    context(host)->portRescans.fetch_add(1u, std::memory_order_relaxed);
}

uint32_t hostNotePortsSupportedDialects(const clap_host_t*)
{
    return CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI;
}

void hostNotePortsRescan(const clap_host_t* host, uint32_t)
{
    context(host)->notePortRescans.fetch_add(1u,
        std::memory_order_relaxed);
}

void hostTailChanged(const clap_host_t* host)
{
    context(host)->tailChanges.fetch_add(1u, std::memory_order_relaxed);
}

HostContext::HostContext()
{
    host.clap_version = CLAP_VERSION_INIT;
    host.host_data = this;
    host.name = "s3g CLAP Realtime Audit";
    host.vendor = "s3g";
    host.url = "https://github.com/s3g/s3g-dsp";
    host.version = "1";
    host.get_extension = hostGetExtension;
    host.request_restart = hostRequestRestart;
    host.request_process = hostRequestProcess;
    host.request_callback = hostRequestCallback;
    params.rescan = hostParamsRescan;
    params.clear = hostParamsClear;
    params.request_flush = hostParamsRequestFlush;
    audioPorts.is_rescan_flag_supported = hostAudioPortsSupports;
    audioPorts.rescan = hostAudioPortsRescan;
    notePorts.supported_dialects = hostNotePortsSupportedDialects;
    notePorts.rescan = hostNotePortsRescan;
    tail.changed = hostTailChanged;
}

struct EmptyInputEvents {
    clap_input_events_t events {};

    EmptyInputEvents()
    {
        events.ctx = this;
        events.size = [](const clap_input_events_t*) -> uint32_t { return 0u; };
        events.get = [](const clap_input_events_t*, uint32_t)
            -> const clap_event_header_t* { return nullptr; };
    }
};

union AuditEvent {
    clap_event_header_t header;
    clap_event_param_value_t parameter;
    clap_event_note_t note;
    clap_event_midi_t midi;
};

struct InputEvents {
    std::vector<AuditEvent> storage;
    clap_input_events_t events {};

    InputEvents() { bind(); }
    InputEvents(const InputEvents&) = delete;
    InputEvents& operator=(const InputEvents&) = delete;

    InputEvents(InputEvents&& other) noexcept
        : storage(std::move(other.storage))
    {
        bind();
    }

    InputEvents& operator=(InputEvents&& other) noexcept
    {
        if (this != &other) {
            storage = std::move(other.storage);
            bind();
        }
        return *this;
    }

private:
    void bind()
    {
        events.ctx = this;
        events.size = [](const clap_input_events_t* list) -> uint32_t {
            const auto* self = static_cast<const InputEvents*>(list->ctx);
            return self ? static_cast<uint32_t>(self->storage.size()) : 0u;
        };
        events.get = [](const clap_input_events_t* list, uint32_t index)
            -> const clap_event_header_t* {
            const auto* self = static_cast<const InputEvents*>(list->ctx);
            return self && index < self->storage.size()
                ? &self->storage[index].header : nullptr;
        };
    }
};

struct OutputEvents {
    clap_output_events_t events {};
    uint32_t frames = 0u;
    uint64_t attempted = 0u;
    uint64_t accepted = 0u;
    uint64_t invalid = 0u;
    uint32_t lastTime = 0u;
    bool hasLastTime = false;

    OutputEvents()
    {
        events.ctx = this;
        events.try_push = [](const clap_output_events_t* list,
                              const clap_event_header_t* event) -> bool {
            auto* self = static_cast<OutputEvents*>(list->ctx);
            ++self->attempted;
            const bool valid = event && event->size >= sizeof(clap_event_header_t)
                && (!self->hasLastTime || event->time >= self->lastTime)
                && (self->frames == 0u || event->time < self->frames
                    || event->type == CLAP_EVENT_NOTE_END);
            if (!valid) {
                ++self->invalid;
                return false;
            }
            self->lastTime = event->time;
            self->hasLastTime = true;
            ++self->accepted;
            return true;
        };
    }

    void beginBlock(uint32_t blockFrames)
    {
        frames = blockFrames;
        lastTime = 0u;
        hasLastTime = false;
    }
};

struct PortDescription {
    bool input = false;
    uint32_t index = 0u;
    clap_audio_port_info_t info {};
};

std::vector<PortDescription> queryPorts(const clap_plugin_t* plugin,
    const clap_plugin_audio_ports_t* extension, bool input)
{
    std::vector<PortDescription> result;
    if (!extension) return result;
    const uint32_t count = extension->count(plugin, input);
    result.reserve(count);
    for (uint32_t index = 0u; index < count; ++index) {
        PortDescription description;
        description.input = input;
        description.index = index;
        if (!extension->get(plugin, index, input, &description.info))
            throw std::runtime_error("audio-ports get() failed at index "
                + std::to_string(index));
        result.push_back(description);
    }
    return result;
}

struct PortStorage {
    uint32_t channels = 0u;
    uint32_t maxFrames = 0u;
    std::vector<float> samples;
    std::vector<float*> channelPointers;
};

struct AudioBank {
    std::vector<PortStorage> storage;
    std::vector<clap_audio_buffer_t> buffers;

    AudioBank(const std::vector<PortDescription>& descriptions,
        uint32_t maxFrames, bool input)
    {
        storage.reserve(descriptions.size());
        buffers.resize(descriptions.size());
        for (const auto& description : descriptions) {
            PortStorage port;
            port.channels = description.info.channel_count;
            port.maxFrames = maxFrames;
            port.samples.resize(static_cast<size_t>(port.channels) * maxFrames);
            port.channelPointers.resize(port.channels);
            storage.push_back(std::move(port));
        }
        for (size_t portIndex = 0u; portIndex < storage.size(); ++portIndex) {
            auto& port = storage[portIndex];
            for (uint32_t channel = 0u; channel < port.channels; ++channel) {
                port.channelPointers[channel] = port.samples.data()
                    + static_cast<size_t>(channel) * maxFrames;
                if (input) {
                    for (uint32_t frame = 0u; frame < maxFrames; ++frame) {
                        const double phase = 0.013 * static_cast<double>(frame + 1u)
                            * static_cast<double>(channel + 1u + portIndex * 7u);
                        port.channelPointers[channel][frame] = static_cast<float>(
                            0.075 * std::sin(phase) + 0.025 * std::cos(phase * 0.37));
                    }
                }
            }
            auto& buffer = buffers[portIndex];
            buffer.data32 = port.channelPointers.empty()
                ? nullptr : port.channelPointers.data();
            buffer.data64 = nullptr;
            buffer.channel_count = port.channels;
            buffer.latency = 0u;
            buffer.constant_mask = 0u;
        }
    }

    void clear(uint32_t frames)
    {
        for (auto& port : storage) {
            for (auto* channel : port.channelPointers)
                std::fill(channel, channel + frames, 0.0f);
        }
    }

    bool numericallyHealthy(uint32_t frames) const
    {
        for (const auto& port : storage) {
            for (const auto* channel : port.channelPointers) {
                for (uint32_t frame = 0u; frame < frames; ++frame) {
                    const float value = channel[frame];
                    if (!std::isfinite(value)
                        || std::fpclassify(value) == FP_SUBNORMAL) return false;
                }
            }
        }
        return true;
    }
};

struct ParameterChoice {
    clap_param_info_t info {};
    double original = 0.0;
    double low = 0.0;
    double high = 0.0;
};

std::vector<ParameterChoice> chooseParameters(const clap_plugin_t* plugin,
    const clap_plugin_params_t* params, uint32_t limit)
{
    std::vector<ParameterChoice> choices;
    if (!params || limit == 0u) return choices;
    const uint32_t count = params->count(plugin);
    choices.reserve(std::min(count, limit));
    for (uint32_t index = 0u; index < count && choices.size() < limit; ++index) {
        clap_param_info_t info {};
        if (!params->get_info(plugin, index, &info)) continue;
        if ((info.flags & CLAP_PARAM_IS_AUTOMATABLE) == 0u
            || (info.flags & (CLAP_PARAM_IS_READONLY | CLAP_PARAM_IS_HIDDEN
                | CLAP_PARAM_IS_STEPPED)) != 0u
            || !std::isfinite(info.min_value) || !std::isfinite(info.max_value)
            || info.max_value <= info.min_value) continue;
        double original = info.default_value;
        if (!params->get_value(plugin, info.id, &original)
            || !std::isfinite(original)) original = info.default_value;
        const double range = info.max_value - info.min_value;
        choices.push_back({ info, original,
            info.min_value + 0.25 * range,
            info.min_value + 0.75 * range });
    }
    return choices;
}

InputEvents makeParameterEvents(const std::vector<ParameterChoice>& choices,
    uint32_t limit, bool high, bool original, uint32_t frames,
    bool distributed)
{
    InputEvents events;
    const size_t count = std::min<size_t>(choices.size(), limit);
    events.storage.reserve(count);
    for (size_t index = 0u; index < count; ++index) {
        const auto& choice = choices[index];
        AuditEvent storage {};
        auto& event = storage.parameter;
        event.header.size = sizeof(event);
        event.header.time = distributed && frames > 1u && count > 1u
            ? static_cast<uint32_t>((static_cast<uint64_t>(index)
                  * static_cast<uint64_t>(frames - 1u))
                / static_cast<uint64_t>(count - 1u))
            : 0u;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_PARAM_VALUE;
        event.header.flags = CLAP_EVENT_IS_LIVE;
        event.param_id = choice.info.id;
        event.cookie = choice.info.cookie;
        event.note_id = -1;
        event.port_index = -1;
        event.channel = -1;
        event.key = -1;
        event.value = original ? choice.original : (high ? choice.high : choice.low);
        events.storage.push_back(storage);
    }
    return events;
}

void addMidiEvent(InputEvents& events, uint32_t time, uint8_t status,
    uint8_t dataOne, uint8_t dataTwo)
{
    AuditEvent storage {};
    auto& event = storage.midi;
    event.header.size = sizeof(event);
    event.header.time = time;
    event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    event.header.type = CLAP_EVENT_MIDI;
    event.header.flags = CLAP_EVENT_IS_LIVE;
    event.port_index = 0u;
    event.data[0] = status;
    event.data[1] = dataOne;
    event.data[2] = dataTwo;
    events.storage.push_back(storage);
}

InputEvents makeNimMidiFloodEvents(uint32_t phase)
{
    InputEvents events;
    // One complete BU16 matrix pass per block. Three phases cover native
    // strike velocity, polyphonic pressure, Note Off, every quadrant/channel,
    // and all 64 matrix addresses without depending on a controller profile.
    events.storage.reserve(86u);
    for (uint8_t channel = 0u; channel < 4u; ++channel) {
        for (uint8_t note = 0u; note < 16u; ++note) {
            const uint8_t value = static_cast<uint8_t>(
                1u + ((static_cast<uint32_t>(channel) * 16u + note) * 29u
                    + phase * 31u) % 127u);
            if (phase == 0u)
                addMidiEvent(events, 0u,
                    static_cast<uint8_t>(0x90u | channel), note, value);
            else if (phase == 1u)
                addMidiEvent(events, 0u,
                    static_cast<uint8_t>(0xa0u | channel), note, value);
            else
                addMidiEvent(events, 0u,
                    static_cast<uint8_t>(0x80u | channel), note, 0u);
        }
    }

    // Four complete E16-style 14-bit NRPN turns on MIDI channel 16. These
    // target stable, continuous global IDs and vary both coarse and fine data.
    constexpr std::array<uint16_t, 4u> parameterIds { 5u, 6u, 11u, 12u };
    for (size_t index = 0u; index < parameterIds.size(); ++index) {
        const uint16_t id = parameterIds[index];
        const uint16_t value = static_cast<uint16_t>(
            (phase * 4093u + static_cast<uint32_t>(index) * 3251u) & 0x3fffu);
        addMidiEvent(events, 0u, 0xbfu, 99u,
            static_cast<uint8_t>((id >> 7u) & 0x7fu));
        addMidiEvent(events, 0u, 0xbfu, 98u,
            static_cast<uint8_t>(id & 0x7fu));
        addMidiEvent(events, 0u, 0xbfu, 6u,
            static_cast<uint8_t>((value >> 7u) & 0x7fu));
        addMidiEvent(events, 0u, 0xbfu, 38u,
            static_cast<uint8_t>(value & 0x7fu));
    }

    // Cover the performance-command Note On class and preset Program Change.
    constexpr std::array<uint8_t, 3u> modeNotes { 117u, 119u, 118u };
    addMidiEvent(events, 0u, 0x9fu, modeNotes[phase % modeNotes.size()], 127u);
    addMidiEvent(events, 0u, 0xcfu, static_cast<uint8_t>(phase), 0u);
    return events;
}

InputEvents makeNoteOnEvents()
{
    InputEvents events;
    AuditEvent storage {};
    auto& event = storage.note;
    event.header.size = sizeof(event);
    event.header.time = 0u;
    event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    event.header.type = CLAP_EVENT_NOTE_ON;
    event.header.flags = 0u;
    event.note_id = 1;
    event.port_index = 0;
    event.channel = 0;
    event.key = 60;
    event.velocity = 0.8;
    events.storage.push_back(storage);
    return events;
}

bool hasFeature(const clap_plugin_descriptor_t* descriptor, const char* feature)
{
    if (!descriptor || !descriptor->features) return false;
    for (const char* const* item = descriptor->features; *item; ++item) {
        if (std::strcmp(*item, feature) == 0) return true;
    }
    return false;
}

struct Stats {
    double meanUs = 0.0;
    double p50Us = 0.0;
    double p95Us = 0.0;
    double p99Us = 0.0;
    double maxUs = 0.0;
};

double percentile(const std::vector<double>& sorted, double fraction)
{
    if (sorted.empty()) return 0.0;
    // Nearest-rank keeps a high percentile meaningful for short, explicitly
    // requested diagnostic runs (for example, p99 of two samples is max).
    const double rank = std::ceil(std::clamp(fraction, 0.0, 1.0)
        * static_cast<double>(sorted.size()));
    const size_t index = std::min(sorted.size() - 1u,
        static_cast<size_t>(std::max(1.0, rank)) - 1u);
    return sorted[index];
}

Stats calculateStats(const std::vector<double>& durationsUs)
{
    Stats stats;
    if (durationsUs.empty()) return stats;
    std::vector<double> sorted = durationsUs;
    std::sort(sorted.begin(), sorted.end());
    double sum = 0.0;
    for (const double duration : durationsUs) sum += duration;
    stats.meanUs = sum / static_cast<double>(durationsUs.size());
    stats.p50Us = percentile(sorted, 0.50);
    stats.p95Us = percentile(sorted, 0.95);
    stats.p99Us = percentile(sorted, 0.99);
    stats.maxUs = sorted.back();
    return stats;
}

bool validProcessStatus(clap_process_status status)
{
    return status >= CLAP_PROCESS_CONTINUE && status <= CLAP_PROCESS_SLEEP;
}

class ControlPublicationStress {
public:
    ControlPublicationStress() = default;
    ControlPublicationStress(const ControlPublicationStress&) = delete;
    ControlPublicationStress& operator=(const ControlPublicationStress&) = delete;

    ~ControlPublicationStress() { stop(); }

    void start()
    {
        if (running_.exchange(true, std::memory_order_acq_rel)) return;
        thread_ = std::thread([this] {
            uint64_t value = 0u;
            while (running_.load(std::memory_order_acquire)) {
                published_.store(++value, std::memory_order_release);
                std::this_thread::yield();
            }
        });
        while (published_.load(std::memory_order_acquire) == 0u)
            std::this_thread::yield();
    }

    void stop()
    {
        if (!running_.exchange(false, std::memory_order_acq_rel)) return;
        if (thread_.joinable()) thread_.join();
    }

    uint64_t snapshot() const noexcept
    {
        return published_.load(std::memory_order_acquire);
    }

private:
    std::atomic<bool> running_ { false };
    std::atomic<uint64_t> published_ { 0u };
    std::thread thread_;
};

void addSaturated(uint64_t& destination, uint64_t increment) noexcept
{
    const uint64_t maximum = std::numeric_limits<uint64_t>::max();
    destination = increment > maximum - destination
        ? maximum : destination + increment;
}

uint64_t allocationCalls(const s3g_rt_alloc_probe_counts& counts) noexcept
{
    uint64_t total = 0u;
    addSaturated(total, counts.malloc_calls);
    addSaturated(total, counts.calloc_calls);
    addSaturated(total, counts.realloc_calls);
    addSaturated(total, counts.posix_memalign_calls);
    addSaturated(total, counts.aligned_alloc_calls);
    return total;
}

uint64_t requestedBytes(const s3g_rt_alloc_probe_counts& counts) noexcept
{
    uint64_t total = 0u;
    addSaturated(total, counts.malloc_requested_bytes);
    addSaturated(total, counts.calloc_requested_bytes);
    addSaturated(total, counts.realloc_requested_bytes);
    addSaturated(total, counts.posix_memalign_requested_bytes);
    addSaturated(total, counts.aligned_alloc_requested_bytes);
    return total;
}

struct AllocationStats {
    bool enabled = false;
    uint64_t measuredBlocks = 0u;
    uint64_t blocksWithOperations = 0u;
    uint64_t blocksWithAllocations = 0u;
    uint64_t blocksWithDeallocations = 0u;
    uint64_t maxOperationsPerBlock = 0u;
    uint64_t maxAllocationCallsPerBlock = 0u;
    uint64_t maxDeallocationCallsPerBlock = 0u;
    uint64_t maxRequestedBytesPerBlock = 0u;
    s3g_rt_alloc_probe_counts totals {};

    void add(const s3g_rt_alloc_probe_counts& block) noexcept
    {
        ++measuredBlocks;
        const uint64_t allocations = allocationCalls(block);
        const uint64_t deallocations = block.free_calls;
        uint64_t operations = allocations;
        addSaturated(operations, deallocations);
        const uint64_t bytes = requestedBytes(block);
        if (operations != 0u) ++blocksWithOperations;
        if (allocations != 0u) ++blocksWithAllocations;
        if (deallocations != 0u) ++blocksWithDeallocations;
        maxOperationsPerBlock = std::max(maxOperationsPerBlock, operations);
        maxAllocationCallsPerBlock = std::max(
            maxAllocationCallsPerBlock, allocations);
        maxDeallocationCallsPerBlock = std::max(
            maxDeallocationCallsPerBlock, deallocations);
        maxRequestedBytesPerBlock = std::max(
            maxRequestedBytesPerBlock, bytes);

        addSaturated(totals.malloc_calls, block.malloc_calls);
        addSaturated(totals.malloc_requested_bytes,
            block.malloc_requested_bytes);
        addSaturated(totals.calloc_calls, block.calloc_calls);
        addSaturated(totals.calloc_requested_bytes,
            block.calloc_requested_bytes);
        addSaturated(totals.realloc_calls, block.realloc_calls);
        addSaturated(totals.realloc_requested_bytes,
            block.realloc_requested_bytes);
        addSaturated(totals.free_calls, block.free_calls);
        addSaturated(totals.posix_memalign_calls,
            block.posix_memalign_calls);
        addSaturated(totals.posix_memalign_requested_bytes,
            block.posix_memalign_requested_bytes);
        addSaturated(totals.aligned_alloc_calls,
            block.aligned_alloc_calls);
        addSaturated(totals.aligned_alloc_requested_bytes,
            block.aligned_alloc_requested_bytes);
        addSaturated(totals.allocation_failures,
            block.allocation_failures);
        addSaturated(totals.invalid_alignment_calls,
            block.invalid_alignment_calls);
    }
};

struct ScenarioResult {
    std::string name;
    double sampleRate = 0.0;
    uint32_t blockSize = 0u;
    uint32_t requestedIterations = 0u;
    uint32_t measuredIterations = 0u;
    uint32_t eventCount = 0u;
    Stats timing;
    double deadlineUs = 0.0;
    uint64_t deadlineMisses = 0u;
    std::array<uint64_t, 5u> statuses {};
    uint64_t outputAttempted = 0u;
    uint64_t outputAccepted = 0u;
    uint64_t outputInvalid = 0u;
    std::string eventSource = "none";
    std::string eventTiming = "none";
    bool controlPublicationEnabled = false;
    uint64_t controlPublications = 0u;
    uint64_t controlSnapshotsObserved = 0u;
    AllocationStats allocations;
    bool finiteOutput = true;
    std::string error;
};

void serviceMainThread(HostContext& host, const clap_plugin_t* plugin)
{
    if (!host.callbackPending.exchange(false, std::memory_order_relaxed)) return;
    plugin->on_main_thread(plugin);
}

ScenarioResult runScenario(const std::string& name,
    const clap_plugin_t* plugin, HostContext& host, AudioBank& inputs,
    AudioBank& outputs, uint32_t frames, double sampleRate,
    uint32_t warmupBlocks, uint32_t iterations,
    const clap_input_events_t* preludeEvents,
    const clap_input_events_t* alternatingLow,
    const clap_input_events_t* alternatingHigh, uint32_t eventCount,
    int64_t& steadyTime, RealtimeAllocationProbe* allocationProbe,
    const std::vector<const clap_input_events_t*>* eventCycle = nullptr,
    ControlPublicationStress* controlPublication = nullptr,
    const char* eventSource = "none", const char* eventTiming = "none")
{
    ScenarioResult result;
    result.name = name;
    result.sampleRate = sampleRate;
    result.blockSize = frames;
    result.requestedIterations = iterations;
    result.eventCount = eventCount;
    result.eventSource = eventSource ? eventSource : "none";
    result.eventTiming = eventTiming ? eventTiming : "none";
    result.controlPublicationEnabled = controlPublication != nullptr;
    result.deadlineUs = static_cast<double>(frames) * 1000000.0 / sampleRate;
    result.allocations.enabled = allocationProbe != nullptr
        && allocationProbe->enabled();

    EmptyInputEvents empty;
    OutputEvents outputEvents;
    std::vector<double> durations;
    durations.reserve(iterations);
    uint64_t previousControlSnapshot = controlPublication
        ? controlPublication->snapshot() : 0u;
    const uint64_t firstControlSnapshot = previousControlSnapshot;

    const auto processOne = [&](uint32_t index, bool measure,
                                const clap_input_events_t* explicitEvents) -> bool {
        outputs.clear(frames);
        outputEvents.beginBlock(frames);
        const clap_input_events_t* inputEvents = explicitEvents
            ? explicitEvents : &empty.events;
        if (!explicitEvents && controlPublication
            && alternatingLow && alternatingHigh) {
            const uint64_t snapshot = controlPublication->snapshot();
            if (snapshot != previousControlSnapshot) {
                ++result.controlSnapshotsObserved;
                previousControlSnapshot = snapshot;
            }
            inputEvents = (snapshot & 1u) == 0u
                ? alternatingLow : alternatingHigh;
        } else if (!explicitEvents && eventCycle && !eventCycle->empty()) {
            inputEvents = (*eventCycle)[index % eventCycle->size()];
        } else if (!explicitEvents && alternatingLow && alternatingHigh) {
            inputEvents = (index & 1u) == 0u ? alternatingLow : alternatingHigh;
        }
        clap_process_t process {};
        process.steady_time = steadyTime;
        process.frames_count = frames;
        process.audio_inputs = inputs.buffers.empty() ? nullptr : inputs.buffers.data();
        process.audio_outputs = outputs.buffers.empty() ? nullptr : outputs.buffers.data();
        process.audio_inputs_count = static_cast<uint32_t>(inputs.buffers.size());
        process.audio_outputs_count = static_cast<uint32_t>(outputs.buffers.size());
        process.in_events = inputEvents;
        process.out_events = &outputEvents.events;
        const bool probeThisBlock = measure && allocationProbe != nullptr
            && allocationProbe->enabled();
        if (probeThisBlock) allocationProbe->begin();
        const auto begin = std::chrono::steady_clock::now();
        const clap_process_status status = plugin->process(plugin, &process);
        const auto end = std::chrono::steady_clock::now();
        if (probeThisBlock) allocationProbe->end();
        if (probeThisBlock) {
            s3g_rt_alloc_probe_counts counts {};
            if (!allocationProbe->read(counts)) {
                result.error = "allocation probe could not read callback counters";
                return false;
            }
            result.allocations.add(counts);
        }
        steadyTime += frames;
        if (!validProcessStatus(status)) {
            result.error = "process() returned error or unknown status "
                + std::to_string(status);
            return false;
        }
        ++result.statuses[static_cast<size_t>(status)];
        if (!outputs.numericallyHealthy(frames)) {
            result.finiteOutput = false;
            result.error = "process() produced a non-finite or subnormal output sample";
            return false;
        }
        if (measure) {
            const double duration = std::chrono::duration<double, std::micro>(
                end - begin).count();
            durations.push_back(duration);
            if (duration > result.deadlineUs) ++result.deadlineMisses;
        }
        serviceMainThread(host, plugin);
        return true;
    };

    if (preludeEvents) processOne(0u, false, preludeEvents);
    for (uint32_t index = 0u;
         result.error.empty() && index < warmupBlocks; ++index) {
        if (!processOne(index, false, nullptr)) break;
    }
    if (result.error.empty()) {
        result.statuses.fill(0u);
        const uint64_t attemptedBefore = outputEvents.attempted;
        const uint64_t acceptedBefore = outputEvents.accepted;
        const uint64_t invalidBefore = outputEvents.invalid;
        for (uint32_t index = 0u; index < iterations; ++index) {
            if (!processOne(index, true, nullptr)) break;
        }
        result.outputAttempted = outputEvents.attempted - attemptedBefore;
        result.outputAccepted = outputEvents.accepted - acceptedBefore;
        result.outputInvalid = outputEvents.invalid - invalidBefore;
        if (result.outputInvalid > 0u)
            result.error = "plugin emitted invalid or unsorted output events";
    }
    result.timing = calculateStats(durations);
    result.measuredIterations = static_cast<uint32_t>(durations.size());
    if (controlPublication) {
        const uint64_t finalSnapshot = controlPublication->snapshot();
        result.controlPublications = finalSnapshot >= firstControlSnapshot
            ? finalSnapshot - firstControlSnapshot : 0u;
    }
    return result;
}

void writePortJson(std::ostream& output,
    const std::vector<PortDescription>& ports)
{
    output << '[';
    for (size_t index = 0u; index < ports.size(); ++index) {
        if (index != 0u) output << ',';
        const auto& port = ports[index];
        output << "{\"index\":" << port.index
               << ",\"id\":" << port.info.id
               << ",\"name\":\"" << jsonEscape(port.info.name) << "\""
               << ",\"channels\":" << port.info.channel_count
               << ",\"main\":"
               << ((port.info.flags & CLAP_AUDIO_PORT_IS_MAIN) != 0u
                       ? "true" : "false") << '}';
    }
    output << ']';
}

void writeAllocationJson(std::ostream& output,
    const AllocationStats& allocations)
{
    if (!allocations.enabled) {
        output << "null";
        return;
    }
    const auto& totals = allocations.totals;
    const uint64_t allocationTotal = allocationCalls(totals);
    uint64_t operationTotal = allocationTotal;
    addSaturated(operationTotal, totals.free_calls);
    output << "{\"measured_blocks\":" << allocations.measuredBlocks
           << ",\"blocks_with_operations\":"
           << allocations.blocksWithOperations
           << ",\"blocks_with_allocations\":"
           << allocations.blocksWithAllocations
           << ",\"blocks_with_deallocations\":"
           << allocations.blocksWithDeallocations
           << ",\"totals\":{\"operations\":" << operationTotal
           << ",\"allocation_calls\":" << allocationTotal
           << ",\"deallocation_calls\":" << totals.free_calls
           << ",\"requested_bytes\":" << requestedBytes(totals)
           << ",\"allocation_failures\":"
           << totals.allocation_failures
           << ",\"invalid_alignment_calls\":"
           << totals.invalid_alignment_calls << '}'
           << ",\"max_per_block\":{\"operations\":"
           << allocations.maxOperationsPerBlock
           << ",\"allocation_calls\":"
           << allocations.maxAllocationCallsPerBlock
           << ",\"deallocation_calls\":"
           << allocations.maxDeallocationCallsPerBlock
           << ",\"requested_bytes\":"
           << allocations.maxRequestedBytesPerBlock << '}'
           << ",\"calls_by_api\":{\"malloc\":" << totals.malloc_calls
           << ",\"calloc\":" << totals.calloc_calls
           << ",\"realloc\":" << totals.realloc_calls
           << ",\"free\":" << totals.free_calls
           << ",\"posix_memalign\":" << totals.posix_memalign_calls
           << ",\"aligned_alloc\":" << totals.aligned_alloc_calls << '}'
           << ",\"requested_bytes_by_api\":{\"malloc\":"
           << totals.malloc_requested_bytes
           << ",\"calloc\":" << totals.calloc_requested_bytes
           << ",\"realloc\":" << totals.realloc_requested_bytes
           << ",\"posix_memalign\":"
           << totals.posix_memalign_requested_bytes
           << ",\"aligned_alloc\":"
           << totals.aligned_alloc_requested_bytes << "}}";
}

void writeScenarioJson(std::ostream& output, const ScenarioResult& result)
{
    const auto load = [&](double microseconds) {
        return result.deadlineUs > 0.0 ? microseconds / result.deadlineUs : 0.0;
    };
    output << std::setprecision(10)
           << "{\"name\":\"" << jsonEscape(result.name) << "\""
           << ",\"sample_rate\":" << result.sampleRate
           << ",\"block_size\":" << result.blockSize
           << ",\"requested_iterations\":" << result.requestedIterations
           << ",\"measured_iterations\":" << result.measuredIterations
           << ",\"event_count\":" << result.eventCount
           << ",\"event_source\":\"" << jsonEscape(result.eventSource) << "\""
           << ",\"event_timing\":\"" << jsonEscape(result.eventTiming) << "\""
           << ",\"midi_event_classes\":";
    if (result.eventSource == "midi1-e16-bu16") {
        output << "[\"bu16-note-on\",\"bu16-poly-pressure\","
                  "\"bu16-note-off\",\"e16-nrpn-cc\","
                  "\"e16-command-note-on\",\"e16-program-change\"]";
    } else {
        output << "[]";
    }
    output
           << ",\"deadline_us\":" << result.deadlineUs
           << ",\"timing_us\":{\"mean\":" << result.timing.meanUs
           << ",\"p50\":" << result.timing.p50Us
           << ",\"p95\":" << result.timing.p95Us
           << ",\"p99\":" << result.timing.p99Us
           << ",\"max\":" << result.timing.maxUs << '}'
           << ",\"deadline_load\":{\"mean\":" << load(result.timing.meanUs)
           << ",\"p50\":" << load(result.timing.p50Us)
           << ",\"p95\":" << load(result.timing.p95Us)
           << ",\"p99\":" << load(result.timing.p99Us)
           << ",\"max\":" << load(result.timing.maxUs) << '}'
           << ",\"deadline_misses\":" << result.deadlineMisses
           << ",\"finite_output\":" << (result.finiteOutput ? "true" : "false")
           << ",\"process_statuses\":{\"continue\":"
           << result.statuses[CLAP_PROCESS_CONTINUE]
           << ",\"continue_if_not_quiet\":"
           << result.statuses[CLAP_PROCESS_CONTINUE_IF_NOT_QUIET]
           << ",\"tail\":" << result.statuses[CLAP_PROCESS_TAIL]
           << ",\"sleep\":" << result.statuses[CLAP_PROCESS_SLEEP] << '}'
           << ",\"output_events\":{\"attempted\":" << result.outputAttempted
           << ",\"accepted\":" << result.outputAccepted
           << ",\"invalid\":" << result.outputInvalid << '}'
           << ",\"control_publication\":{\"enabled\":"
           << (result.controlPublicationEnabled ? "true" : "false")
           << ",\"published_snapshots\":" << result.controlPublications
           << ",\"observed_changes\":"
           << result.controlSnapshotsObserved
           << ",\"plugin_api_calls_from_publisher\":0}"
           << ",\"allocation_probe\":";
    writeAllocationJson(output, result.allocations);
    output
           << ",\"error\":";
    if (result.error.empty()) output << "null";
    else output << '"' << jsonEscape(result.error) << '"';
    output << '}';
}

std::string makeReport(const Options& options,
    const std::filesystem::path& binary,
    const std::filesystem::path& entryPath, const clap_plugin_t* plugin,
    const std::vector<PortDescription>& inputs,
    const std::vector<PortDescription>& outputs,
    const std::vector<ScenarioResult>& scenarios, const HostContext& host,
    bool synthNotePrimed)
{
    const auto text = [](const char* value) -> std::string {
        return value ? value : "";
    };
    std::ostringstream report;
    report << "{\n  \"schema\":\"" << kSchema << "\"," 
           << "\n  \"plugin\":{\"id\":\""
           << jsonEscape(text(plugin->desc->id)) << "\",\"name\":\""
           << jsonEscape(text(plugin->desc->name)) << "\",\"version\":\""
           << jsonEscape(text(plugin->desc->version)) << "\",\"supplied_path\":\""
           << jsonEscape(options.suppliedPath.string()) << "\",\"binary\":\""
           << jsonEscape(binary.string()) << "\",\"entry_path\":\""
           << jsonEscape(entryPath.string()) << "\"},"
           << "\n  \"configuration\":{\"warmup_blocks\":"
           << options.warmupBlocks << ",\"iterations\":" << options.iterations
           << ",\"event_burst_limit\":"
           << (options.eventBursts.empty() ? 0u
               : *std::max_element(options.eventBursts.begin(),
                   options.eventBursts.end()))
           << ",\"event_bursts\":[";
    for (size_t index = 0u; index < options.eventBursts.size(); ++index) {
        if (index != 0u) report << ',';
        report << options.eventBursts[index];
    }
    report << "]"
           << ",\"distributed_automation\":"
           << (options.distributedAutomation ? "true" : "false")
           << ",\"control_publication_stress\":"
           << (options.controlPublicationStress ? "true" : "false")
           << ",\"nim_midi_flood\":"
           << (options.nimMidiFlood ? "true" : "false")
           << ",\"allocation_probe\":"
           << (options.allocationProbe ? "true" : "false")
           << ",\"synth_note_primed\":"
           << (synthNotePrimed ? "true" : "false") << "},"
           << "\n  \"ports\":{\"audio_inputs\":";
    writePortJson(report, inputs);
    report << ",\"audio_outputs\":";
    writePortJson(report, outputs);
    report << "},\n  \"scenarios\":[";
    for (size_t index = 0u; index < scenarios.size(); ++index) {
        if (index != 0u) report << ',';
        report << "\n    ";
        writeScenarioJson(report, scenarios[index]);
    }
    report << "\n  ],\n  \"limitations\":["
           << "\"Offline wall-clock process timing cannot prove that an audio "
              "device or host will not underrun.\","
           << "\"The control-publication thread exercises a host-owned atomic "
              "snapshot handoff; it never calls a CLAP or GUI API, so it does "
              "not validate a plug-in's private GUI queue.\","
           << "\"The NIM MIDI flood is synthetic MIDI 1 delivered through the "
              "CLAP event list; it does not include USB driver, CoreMIDI, E16, "
              "or BU16 hardware latency.\"],"
           << "\n  \"host_callbacks\":{\"restart\":"
           << host.restartRequests.load(std::memory_order_relaxed)
           << ",\"process\":"
           << host.processRequests.load(std::memory_order_relaxed)
           << ",\"callback\":"
           << host.callbackRequests.load(std::memory_order_relaxed)
           << ",\"parameter_rescan\":"
           << host.parameterRescans.load(std::memory_order_relaxed)
           << ",\"parameter_clear\":"
           << host.parameterClears.load(std::memory_order_relaxed)
           << ",\"parameter_flush\":"
           << host.flushRequests.load(std::memory_order_relaxed)
           << ",\"audio_ports_rescan\":"
           << host.portRescans.load(std::memory_order_relaxed)
           << ",\"note_ports_rescan\":"
           << host.notePortRescans.load(std::memory_order_relaxed)
           << ",\"tail_changed\":"
           << host.tailChanges.load(std::memory_order_relaxed) << "}\n}\n";
    return report.str();
}

void removeStaleReport(const std::filesystem::path& path)
{
    if (path.empty()) return;
    std::error_code error;
    const bool exists = std::filesystem::exists(path, error);
    if (error) throw std::runtime_error("could not inspect JSON output: "
        + path.string() + ": " + error.message());
    if (!exists) return;
    const bool directory = std::filesystem::is_directory(path, error);
    if (error) throw std::runtime_error("could not inspect JSON output: "
        + path.string() + ": " + error.message());
    if (directory) {
        throw std::runtime_error("JSON output path is a directory: "
            + path.string());
    }
    std::filesystem::remove(path, error);
    if (error) throw std::runtime_error("could not remove stale JSON output: "
        + path.string() + ": " + error.message());
}

void writeReportAtomically(const std::filesystem::path& path,
    const std::string& report)
{
    std::filesystem::path temporary = path;
    temporary += ".tmp";
    removeStaleReport(temporary);
    std::error_code ignored;
    std::ofstream json(temporary, std::ios::binary | std::ios::trunc);
    if (!json) throw std::runtime_error("could not open temporary JSON output: "
        + temporary.string());
    json << report;
    json.close();
    if (!json) {
        std::filesystem::remove(temporary, ignored);
        throw std::runtime_error("could not write JSON output: "
            + path.string());
    }
    std::error_code renameError;
    std::filesystem::rename(temporary, path, renameError);
    if (renameError) {
        std::filesystem::remove(temporary, ignored);
        throw std::runtime_error("could not publish JSON output: "
            + path.string() + ": " + renameError.message());
    }
}

} // namespace

int main(int argc, char** argv)
{
    Options options;
    try {
        options = parseOptions(argc, argv);
    } catch (const std::exception& error) {
        std::cerr << error.what();
        return 2;
    }
    try {
        removeStaleReport(options.jsonPath);
    } catch (const std::exception& error) {
        std::cerr << "CLAP realtime audit failed: " << error.what() << '\n';
        return 1;
    }

    RealtimeAllocationProbe allocationProbe;
    if (options.allocationProbe) {
        try {
            allocationProbe.initialize();
        } catch (const std::exception& error) {
            std::cerr << "CLAP realtime audit failed: " << error.what() << '\n';
            return 1;
        }
    }

    const auto binary = resolveBinary(options.suppliedPath);
    if (binary.empty()) {
        std::cerr << "Could not resolve one CLAP binary from: "
                  << options.suppliedPath << '\n';
        return 1;
    }
    const auto entryPath = resolveEntryPath(options.suppliedPath, binary);

    void* library = dlopen(binary.c_str(), RTLD_LOCAL | RTLD_NOW);
    if (!library) {
        std::cerr << "Could not load CLAP binary: " << dlerror() << '\n';
        return 1;
    }

    const clap_plugin_entry_t* entry = nullptr;
    const clap_plugin_t* plugin = nullptr;
    bool entryInitialized = false;
    bool active = false;
    bool processing = false;
    int exitCode = 0;
    std::vector<PortDescription> inputPorts;
    std::vector<PortDescription> outputPorts;
    std::vector<ScenarioResult> results;
    HostContext host;

    try {
        entry = static_cast<const clap_plugin_entry_t*>(
            dlsym(library, "clap_entry"));
        if (!entry) throw std::runtime_error("CLAP entry symbol is missing");
        if (!clap_version_is_compatible(entry->clap_version))
            throw std::runtime_error("CLAP entry version is incompatible");
        if (!entry->init(entryPath.c_str()))
            throw std::runtime_error("CLAP entry init() failed");
        entryInitialized = true;
        const auto* factory = static_cast<const clap_plugin_factory_t*>(
            entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
        if (!factory) throw std::runtime_error("CLAP plugin factory is missing");
        plugin = factory->create_plugin(factory, &host.host,
            options.pluginId.c_str());
        if (!plugin) throw std::runtime_error("plugin factory could not create requested id");
        if (!plugin->desc
            || !clap_version_is_compatible(plugin->desc->clap_version))
            throw std::runtime_error("plugin descriptor CLAP version is incompatible");
        if (!plugin->init(plugin)) throw std::runtime_error("plugin init() failed");
        if (!plugin->desc || !plugin->desc->id
            || options.pluginId != plugin->desc->id)
            throw std::runtime_error("created plugin descriptor id does not match request");

        const auto* audioPorts = static_cast<const clap_plugin_audio_ports_t*>(
            plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS));
        const auto* params = static_cast<const clap_plugin_params_t*>(
            plugin->get_extension(plugin, CLAP_EXT_PARAMS));
        const auto* notePorts = static_cast<const clap_plugin_note_ports_t*>(
            plugin->get_extension(plugin, CLAP_EXT_NOTE_PORTS));
        inputPorts = queryPorts(plugin, audioPorts, true);
        outputPorts = queryPorts(plugin, audioPorts, false);
        clap_note_port_info_t noteInput {};
        const bool supportsClapNotes = notePorts
            && notePorts->count(plugin, true) > 0u
            && notePorts->get(plugin, 0u, true, &noteInput)
            && (noteInput.supported_dialects & CLAP_NOTE_DIALECT_CLAP) != 0u;
        const bool supportsMidi = notePorts
            && notePorts->count(plugin, true) > 0u
            && notePorts->get(plugin, 0u, true, &noteInput)
            && (noteInput.supported_dialects & CLAP_NOTE_DIALECT_MIDI) != 0u;
        const bool primeSynth = hasFeature(plugin->desc,
                CLAP_PLUGIN_FEATURE_SYNTHESIZER)
            && supportsClapNotes;
        const uint32_t eventBurstLimit = options.eventBursts.empty() ? 0u
            : *std::max_element(options.eventBursts.begin(),
                options.eventBursts.end());
        const auto parameterChoices = chooseParameters(
            plugin, params, eventBurstLimit);
        auto restoreEvents = makeParameterEvents(parameterChoices,
            static_cast<uint32_t>(parameterChoices.size()), false, true,
            1u, false);
        auto noteOnEvents = makeNoteOnEvents();
        OutputEvents flushOutput;

        const uint32_t maxFrames = *std::max_element(
            options.blockSizes.begin(), options.blockSizes.end());
        AudioBank inputs(inputPorts, maxFrames, true);
        AudioBank outputs(outputPorts, maxFrames, false);
        int64_t steadyTime = 0;

        for (const double sampleRate : options.sampleRates) {
            for (const uint32_t frames : options.blockSizes) {
                if (!plugin->activate(plugin, sampleRate, 1u, maxFrames)) {
                    ScenarioResult failure;
                    failure.allocations.enabled = allocationProbe.enabled();
                    failure.name = "activation";
                    failure.sampleRate = sampleRate;
                    failure.blockSize = frames;
                    failure.error = "plugin activate() failed";
                    results.push_back(failure);
                    exitCode = 1;
                    continue;
                }
                active = true;
                plugin->reset(plugin);
                if (!plugin->start_processing(plugin)) {
                    ScenarioResult failure;
                    failure.allocations.enabled = allocationProbe.enabled();
                    failure.name = "start-processing";
                    failure.sampleRate = sampleRate;
                    failure.blockSize = frames;
                    failure.error = "plugin start_processing() failed";
                    results.push_back(failure);
                    plugin->deactivate(plugin);
                    active = false;
                    exitCode = 1;
                    continue;
                }
                processing = true;

                auto baseline = runScenario("baseline", plugin, host,
                    inputs, outputs, frames, sampleRate, options.warmupBlocks,
                    options.iterations,
                    primeSynth ? &noteOnEvents.events : nullptr,
                    nullptr, nullptr, 0u, steadyTime, &allocationProbe);
                bool processingHealthy = baseline.error.empty();
                if (!processingHealthy) exitCode = 1;
                results.push_back(std::move(baseline));

                if (processingHealthy && !parameterChoices.empty()) {
                    std::vector<uint32_t> completedBurstCounts;
                    for (const uint32_t requestedBurst : options.eventBursts) {
                        const uint32_t eventCount = std::min<uint32_t>(
                            requestedBurst,
                            static_cast<uint32_t>(parameterChoices.size()));
                        if (eventCount == 0u
                            || std::find(completedBurstCounts.begin(),
                                completedBurstCounts.end(), eventCount)
                                != completedBurstCounts.end()) continue;
                        completedBurstCounts.push_back(eventCount);
                        const uint32_t timingCount =
                            options.distributedAutomation ? 2u : 1u;
                        for (uint32_t timing = 0u;
                             processingHealthy && timing < timingCount; ++timing) {
                            const bool distributed = timing == 1u;
                            auto lowEvents = makeParameterEvents(parameterChoices,
                                eventCount, false, false, frames, distributed);
                            auto highEvents = makeParameterEvents(parameterChoices,
                                eventCount, true, false, frames, distributed);
                            std::string scenarioName = "automation-burst";
                            if (options.eventBursts.size() != 1u
                                || options.distributedAutomation) {
                                scenarioName += "-" + std::to_string(eventCount)
                                    + (distributed ? "-distributed" : "-same");
                            }
                            auto automation = runScenario(scenarioName, plugin,
                                host, inputs, outputs, frames, sampleRate,
                                options.warmupBlocks, options.iterations,
                                nullptr, &lowEvents.events, &highEvents.events,
                                eventCount, steadyTime, &allocationProbe,
                                nullptr, nullptr, "parameter",
                                distributed ? "distributed" : "same");
                            processingHealthy = automation.error.empty();
                            if (!processingHealthy) exitCode = 1;
                            results.push_back(std::move(automation));
                        }
                    }

                    if (processingHealthy
                        && options.controlPublicationStress
                        && !completedBurstCounts.empty()) {
                        const uint32_t eventCount = completedBurstCounts.back();
                        auto lowEvents = makeParameterEvents(parameterChoices,
                            eventCount, false, false, frames, false);
                        auto highEvents = makeParameterEvents(parameterChoices,
                            eventCount, true, false, frames, false);
                        ControlPublicationStress publication;
                        publication.start();
                        auto control = runScenario(
                            "control-publication-stress", plugin, host,
                            inputs, outputs, frames, sampleRate,
                            options.warmupBlocks, options.iterations,
                            nullptr, &lowEvents.events, &highEvents.events,
                            eventCount, steadyTime, &allocationProbe,
                            nullptr, &publication, "parameter", "same");
                        publication.stop();
                        processingHealthy = control.error.empty();
                        if (!processingHealthy) exitCode = 1;
                        results.push_back(std::move(control));
                    }
                }

                const bool isNim = options.pluginId
                    == "org.s3g.s3g-dsp.no-input-mixer-8ch";
                if (processingHealthy && options.nimMidiFlood && isNim) {
                    if (!supportsMidi) {
                        ScenarioResult failure;
                        failure.allocations.enabled = allocationProbe.enabled();
                        failure.name = "nim-midi-flood";
                        failure.sampleRate = sampleRate;
                        failure.blockSize = frames;
                        failure.eventSource = "midi1";
                        failure.eventTiming = "same";
                        failure.error = "NIM input port does not advertise MIDI 1";
                        results.push_back(std::move(failure));
                        processingHealthy = false;
                        exitCode = 1;
                    } else {
                        auto strikeEvents = makeNimMidiFloodEvents(0u);
                        auto pressureEvents = makeNimMidiFloodEvents(1u);
                        auto releaseEvents = makeNimMidiFloodEvents(2u);
                        const std::vector<const clap_input_events_t*> cycle {
                            &strikeEvents.events,
                            &pressureEvents.events,
                            &releaseEvents.events,
                        };
                        const uint32_t eventCount = static_cast<uint32_t>(
                            strikeEvents.storage.size());
                        auto midi = runScenario("nim-midi-flood", plugin, host,
                            inputs, outputs, frames, sampleRate,
                            options.warmupBlocks, options.iterations,
                            nullptr, nullptr, nullptr, eventCount,
                            steadyTime, &allocationProbe, &cycle, nullptr,
                            "midi1-e16-bu16", "same");
                        processingHealthy = midi.error.empty();
                        if (!processingHealthy) exitCode = 1;
                        results.push_back(std::move(midi));
                    }
                }

                plugin->stop_processing(plugin);
                processing = false;
                if (processingHealthy && params && !restoreEvents.storage.empty()) {
                    flushOutput.beginBlock(0u);
                    params->flush(plugin, &restoreEvents.events,
                        &flushOutput.events);
                }
                plugin->deactivate(plugin);
                active = false;
            }
        }

        for (const auto& result : results) {
            const double maxLoad = result.deadlineUs > 0.0
                ? result.timing.maxUs / result.deadlineUs : 0.0;
            std::cout << (result.error.empty() ? "DATA " : "FAIL ")
                      << result.name << " " << std::fixed << std::setprecision(0)
                      << result.sampleRate << " Hz / " << result.blockSize
                      << " frames: p99 " << std::setprecision(3)
                      << result.timing.p99Us << " us, max "
                      << result.timing.maxUs << " us ("
                      << std::setprecision(1) << maxLoad * 100.0
                      << "% deadline), misses " << result.deadlineMisses;
            if (result.allocations.enabled) {
                const auto& memory = result.allocations;
                const uint64_t allocations = allocationCalls(memory.totals);
                uint64_t operations = allocations;
                addSaturated(operations, memory.totals.free_calls);
                std::cout << ", alloc probe "
                          << memory.blocksWithOperations << '/'
                          << memory.measuredBlocks << " active blocks, "
                          << operations << " ops (" << allocations << " alloc + "
                          << memory.totals.free_calls << " free, "
                          << requestedBytes(memory.totals)
                          << " requested B), max "
                          << memory.maxOperationsPerBlock
                          << " ops/block, failures "
                          << memory.totals.allocation_failures;
            }
            if (!result.error.empty()) std::cout << ", " << result.error;
            std::cout << '\n';
        }

        const std::string report = makeReport(options, binary, entryPath, plugin,
            inputPorts, outputPorts, results, host, primeSynth);
        if (!options.jsonPath.empty())
            writeReportAtomically(options.jsonPath, report);
    } catch (const std::exception& error) {
        std::cerr << "CLAP realtime audit failed: " << error.what() << '\n';
        exitCode = 1;
    }

    if (processing) plugin->stop_processing(plugin);
    if (active) plugin->deactivate(plugin);
    if (plugin) plugin->destroy(plugin);
    if (entryInitialized) entry->deinit();
    dlclose(library);
    return exitCode;
}
