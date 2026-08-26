#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/note-name.h>
#include <clap/ext/note-ports.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

const void* hostGetExtension(const clap_host_t*, const char*) { return nullptr; }
void hostRequest(const clap_host_t*) {}

std::filesystem::path resolveBinary(const std::filesystem::path& supplied)
{
    if (std::filesystem::is_regular_file(supplied)) return supplied;
#if defined(__APPLE__)
    if (std::filesystem::is_directory(supplied)) {
        const auto macOS = supplied / "Contents" / "MacOS";
        for (const auto& entry : std::filesystem::directory_iterator(macOS))
            if (entry.is_regular_file()) return entry.path();
    }
#endif
    return {};
}

struct EventList {
    std::vector<clap_event_param_value_t> events;
    clap_input_events_t interface {
        this,
        [](const clap_input_events_t* list) -> uint32_t {
            const auto* self = static_cast<const EventList*>(list->ctx);
            return self ? static_cast<uint32_t>(self->events.size()) : 0u;
        },
        [](const clap_input_events_t* list, uint32_t index)
            -> const clap_event_header_t* {
            const auto* self = static_cast<const EventList*>(list->ctx);
            return self && index < self->events.size()
                ? &self->events[index].header : nullptr;
        },
    };

    void add(clap_id id, double value)
    {
        clap_event_param_value_t event {};
        event.header.size = sizeof(event);
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_PARAM_VALUE;
        event.header.flags = CLAP_EVENT_IS_LIVE;
        event.param_id = id;
        event.note_id = -1;
        event.port_index = -1;
        event.channel = -1;
        event.key = -1;
        event.value = value;
        events.push_back(event);
    }
};

struct NoteEventList {
    std::vector<clap_event_note_t> events;
    clap_input_events_t interface {
        this,
        [](const clap_input_events_t* list) -> uint32_t {
            const auto* self = static_cast<const NoteEventList*>(list->ctx);
            return self ? static_cast<uint32_t>(self->events.size()) : 0u;
        },
        [](const clap_input_events_t* list, uint32_t index)
            -> const clap_event_header_t* {
            const auto* self = static_cast<const NoteEventList*>(list->ctx);
            return self && index < self->events.size()
                ? &self->events[index].header : nullptr;
        },
    };

    void add(uint16_t type, int16_t key, double velocity,
             uint32_t time = 0u)
    {
        clap_event_note_t event {};
        event.header.size = sizeof(event);
        event.header.time = time;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = type;
        event.header.flags = CLAP_EVENT_IS_LIVE;
        event.note_id = key;
        event.port_index = 0;
        event.channel = 0;
        event.key = key;
        event.velocity = velocity;
        events.push_back(event);
    }
};

struct StateMemory {
    std::vector<uint8_t> bytes;
    std::size_t offset = 0u;
};

struct LegacyState {
    uint32_t version = 1u;
    float loop1Rate = 1.0f;
    float loop2Rate = 1.0f;
    float crossfade = 0.5f;
    float blend = 0.5f;
    float inputGain = 1.0f;
    float outputGain = 1.0f;
    uint32_t crossfadeMode = 0u;
    uint32_t recordTarget = 1u;
    uint32_t monitorMode = 1u;
};

struct SavedStateV2 {
    uint32_t version = 2u;
    uint32_t headerBytes = sizeof(SavedStateV2);
    float loop1Rate = 1.0f;
    float loop2Rate = 1.0f;
    float crossfade = 0.5f;
    float blend = 0.5f;
    float inputGain = 1.0f;
    float outputGain = 1.0f;
    float overdubFeedback = 0.8f;
    float loop1Start = 0.0f;
    float loop1End = 1.0f;
    float loop2Start = 0.0f;
    float loop2End = 1.0f;
    uint32_t crossfadeMode = 0u;
    uint32_t recordTarget = 1u;
    uint32_t monitorMode = 1u;
    uint32_t playbackModel = 1u;
    uint32_t recordMode = 0u;
    uint32_t loop1Reverse = 0u;
    uint32_t loop2Reverse = 0u;
    uint32_t loop1Join = 0u;
    uint32_t loop2Join = 0u;
    uint32_t playing = 1u;
    double audioSampleRate = 0.0;
    uint32_t loopFrames[2] { 0u, 0u };
};

int64_t writeState(const clap_ostream_t* stream, const void* source,
                   uint64_t bytes)
{
    auto* memory = stream
        ? static_cast<StateMemory*>(stream->ctx) : nullptr;
    if (!memory || !source) return -1;
    const auto* first = static_cast<const uint8_t*>(source);
    memory->bytes.insert(memory->bytes.end(), first, first + bytes);
    return static_cast<int64_t>(bytes);
}

int64_t readState(const clap_istream_t* stream, void* destination,
                  uint64_t bytes)
{
    auto* memory = stream
        ? static_cast<StateMemory*>(stream->ctx) : nullptr;
    if (!memory || !destination || memory->offset + bytes > memory->bytes.size())
        return -1;
    std::memcpy(destination, memory->bytes.data() + memory->offset,
        static_cast<std::size_t>(bytes));
    memory->offset += static_cast<std::size_t>(bytes);
    return static_cast<int64_t>(bytes);
}

float energy(const std::vector<float>& values)
{
    float sum = 0.0f;
    for (float value : values) {
        if (!std::isfinite(value)) return -1.0f;
        sum += value * value;
    }
    return sum;
}

bool processBlock(const clap_plugin_t* plugin,
                  std::vector<float>& inputLeft,
                  std::vector<float>& inputRight,
                  std::vector<float>& outputLeft,
                  std::vector<float>& outputRight,
                  const clap_input_events_t* events)
{
    float* inputChannels[] { inputLeft.data(), inputRight.data() };
    float* outputChannels[] { outputLeft.data(), outputRight.data() };
    clap_audio_buffer_t input {};
    input.data32 = inputChannels;
    input.channel_count = 2u;
    clap_audio_buffer_t output {};
    output.data32 = outputChannels;
    output.channel_count = 2u;
    clap_process_t process {};
    process.steady_time = -1;
    process.frames_count = static_cast<uint32_t>(inputLeft.size());
    process.audio_inputs = &input;
    process.audio_outputs = &output;
    process.audio_inputs_count = 1u;
    process.audio_outputs_count = 1u;
    process.in_events = events;
    return plugin->process(plugin, &process) == CLAP_PROCESS_CONTINUE;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "usage: s3g_crcltr_clap_smoke <bundle-or-binary>\n";
        return 2;
    }
    const auto binary = resolveBinary(argv[1]);
    if (binary.empty()) {
        std::cerr << "Could not resolve CRCLTR plugin binary\n";
        return 1;
    }

    void* library = dlopen(binary.c_str(), RTLD_LOCAL | RTLD_NOW);
    if (!library) {
        std::cerr << "Could not load CRCLTR plugin: " << dlerror() << "\n";
        return 1;
    }
    const auto* entry = static_cast<const clap_plugin_entry_t*>(
        dlsym(library, "clap_entry"));
    bool ok = entry && entry->init(binary.c_str());
    const auto checkpoint = [&](const char* stage) {
        if (!ok) std::cerr << "CRCLTR CLAP smoke stage failed: "
                           << stage << "\n";
    };

    clap_host_t host {};
    host.clap_version = CLAP_VERSION_INIT;
    host.name = "CRCLTR smoke";
    host.vendor = "s3g";
    host.url = "https://github.com/s3g/s3g-dsp";
    host.version = "1";
    host.get_extension = hostGetExtension;
    host.request_restart = hostRequest;
    host.request_process = hostRequest;
    host.request_callback = hostRequest;

    const auto* factory = ok ? static_cast<const clap_plugin_factory_t*>(
        entry->get_factory(CLAP_PLUGIN_FACTORY_ID)) : nullptr;
    const clap_plugin_t* plugin = factory ? factory->create_plugin(
        factory, &host, "org.s3g.s3g-dsp.crcltr") : nullptr;
    ok = ok && plugin && plugin->init(plugin);

    const auto* ports = ok ? static_cast<const clap_plugin_audio_ports_t*>(
        plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS)) : nullptr;
    const auto* params = ok ? static_cast<const clap_plugin_params_t*>(
        plugin->get_extension(plugin, CLAP_EXT_PARAMS)) : nullptr;
    const auto* state = ok ? static_cast<const clap_plugin_state_t*>(
        plugin->get_extension(plugin, CLAP_EXT_STATE)) : nullptr;
    const auto* notePorts = ok ? static_cast<const clap_plugin_note_ports_t*>(
        plugin->get_extension(plugin, CLAP_EXT_NOTE_PORTS)) : nullptr;
    const auto* noteNames = ok ? static_cast<const clap_plugin_note_name_t*>(
        plugin->get_extension(plugin, CLAP_EXT_NOTE_NAME)) : nullptr;
    clap_audio_port_info_t inputPort {};
    clap_audio_port_info_t outputPort {};
    ok = ok && ports && params && state && notePorts && noteNames
        && ports->count(plugin, true) == 1u
        && ports->count(plugin, false) == 1u
        && ports->get(plugin, 0u, true, &inputPort)
        && ports->get(plugin, 0u, false, &outputPort)
        && inputPort.channel_count == 2u
        && outputPort.channel_count == 2u
        && notePorts->count(plugin, true) == 1u
        && notePorts->count(plugin, false) == 0u
        && params->count(plugin) == 25u;
    clap_note_port_info_t notePort {};
    ok = ok && notePorts->get(plugin, 0u, true, &notePort)
        && (notePort.supported_dialects & CLAP_NOTE_DIALECT_MIDI) != 0u
        && noteNames->count(plugin) == 4u;
    constexpr int16_t expectedNoteKeys[] { 36, 37, 38, 39 };
    for (uint32_t index = 0u; ok && index < 4u; ++index) {
        clap_note_name_t name {};
        ok = noteNames->get(plugin, index, &name)
            && name.port == 0 && name.channel == -1
            && name.key == expectedNoteKeys[index];
    }

    bool foundRecord = false;
    bool foundTarget = false;
    bool foundSeamDefault = false;
    bool foundMotion = false;
    bool foundFadeShape = false;
    bool foundClearA = false;
    bool foundClearB = false;
    for (uint32_t index = 0u; ok && index < params->count(plugin); ++index) {
        clap_param_info_t info {};
        ok = params->get_info(plugin, index, &info);
        if (info.id == 8u) foundRecord = std::strcmp(info.name, "Record") == 0;
        if (info.id == 9u) foundTarget = std::strcmp(info.name, "Record Target") == 0;
        if (info.id == 21u) {
            foundSeamDefault = std::strcmp(info.name, "Loop 2 Join") == 0
                && info.default_value == 0.0;
        }
        if (info.id == 3u) {
            foundMotion = std::strcmp(info.name, "Crossfade Motion") == 0
                && info.min_value == 0.0 && info.max_value == 8.0
                && info.default_value == 0.0;
        }
        if (info.id == 23u) {
            foundFadeShape = std::strcmp(info.name, "Fade Shape") == 0
                && info.min_value == 0.0 && info.max_value == 8.0
                && info.default_value == 0.0;
        }
        if (info.id == 24u) {
            foundClearA = std::strcmp(info.name, "Clear Loop A") == 0
                && (info.flags & CLAP_PARAM_IS_STEPPED) != 0u
                && (info.flags & CLAP_PARAM_IS_AUTOMATABLE) == 0u;
        }
        if (info.id == 25u) {
            foundClearB = std::strcmp(info.name, "Clear Loop B") == 0
                && (info.flags & CLAP_PARAM_IS_STEPPED) != 0u
                && (info.flags & CLAP_PARAM_IS_AUTOMATABLE) == 0u;
        }
    }
    double defaultLoop2Join = -1.0;
    ok = ok && foundRecord && foundTarget && foundSeamDefault && foundMotion
        && foundFadeShape && foundClearA && foundClearB
        && params->get_value(plugin, 21u, &defaultLoop2Join)
        && defaultLoop2Join == 0.0;
    checkpoint("parameter surface");
    ok = ok && plugin->activate(plugin, 1000.0, 1u, 512u);
    ok = ok && plugin->start_processing(plugin);

    std::vector<float> inputLeft(400u);
    std::vector<float> inputRight(400u);
    std::vector<float> outputLeft(400u);
    std::vector<float> outputRight(400u);
    for (uint32_t frame = 0u; frame < inputLeft.size(); ++frame) {
        inputLeft[frame] = 0.22f * std::sin(0.031f * static_cast<float>(frame));
        inputRight[frame] = 0.18f * std::cos(0.027f * static_cast<float>(frame));
    }
    EventList recordEvents;
    recordEvents.add(4u, 0.0); // Loop 1 side of the loop crossfade.
    recordEvents.add(23u, 2.0); // Wide A/B fade shape.
    recordEvents.add(5u, 1.0); // Fully wet.
    recordEvents.add(9u, 0.0); // Record Loop 1.
    recordEvents.add(8u, 1.0); // Hold record.
    ok = ok && processBlock(plugin, inputLeft, inputRight,
        outputLeft, outputRight, &recordEvents.interface);

    inputLeft.assign(512u, 0.0f);
    inputRight.assign(512u, 0.0f);
    outputLeft.assign(512u, 0.0f);
    outputRight.assign(512u, 0.0f);
    EventList releaseEvents;
    releaseEvents.add(8u, 0.0);
    ok = ok && processBlock(plugin, inputLeft, inputRight,
        outputLeft, outputRight, &releaseEvents.interface);
    ok = ok && energy(outputLeft) > 0.001f && energy(outputRight) > 0.001f;
    checkpoint("loop A capture");

    // MIDI C2 is a momentary capture gate. The neighboring command notes
    // operate the remaining editor buttons without requiring a CC mapping.
    EventList selectLoopB;
    selectLoopB.add(4u, 1.0);
    selectLoopB.add(9u, 2.0);
    params->flush(plugin, &selectLoopB.interface, nullptr);
    inputLeft.resize(400u);
    inputRight.resize(400u);
    outputLeft.resize(400u);
    outputRight.resize(400u);
    for (uint32_t frame = 0u; frame < inputLeft.size(); ++frame) {
        inputLeft[frame] = 0.20f * std::sin(0.041f * frame);
        inputRight[frame] = 0.16f * std::cos(0.037f * frame);
    }
    NoteEventList midiCaptureOn;
    midiCaptureOn.add(CLAP_EVENT_NOTE_ON, 36, 1.0);
    ok = ok && processBlock(plugin, inputLeft, inputRight,
        outputLeft, outputRight, &midiCaptureOn.interface);
    inputLeft.assign(512u, 0.0f);
    inputRight.assign(512u, 0.0f);
    outputLeft.assign(512u, 0.0f);
    outputRight.assign(512u, 0.0f);
    NoteEventList midiCaptureOff;
    midiCaptureOff.add(CLAP_EVENT_NOTE_OFF, 36, 0.0);
    ok = ok && processBlock(plugin, inputLeft, inputRight,
        outputLeft, outputRight, &midiCaptureOff.interface)
        && energy(outputLeft) > 0.001f && energy(outputRight) > 0.001f;
    checkpoint("MIDI capture");

    double playing = 0.0;
    NoteEventList midiPlayPause;
    midiPlayPause.add(CLAP_EVENT_NOTE_ON, 37, 1.0);
    ok = ok && processBlock(plugin, inputLeft, inputRight,
        outputLeft, outputRight, &midiPlayPause.interface)
        && params->get_value(plugin, 22u, &playing) && playing == 0.0;
    ok = ok && processBlock(plugin, inputLeft, inputRight,
        outputLeft, outputRight, &midiPlayPause.interface)
        && params->get_value(plugin, 22u, &playing) && playing == 1.0;

    EventList extendedCrossfade;
    extendedCrossfade.add(3u, 7.0); // Sample & Hold motion.
    extendedCrossfade.add(23u, 8.0); // Narrow Cut fade.
    params->flush(plugin, &extendedCrossfade.interface, nullptr);

    StateMemory saved;
    clap_ostream_t outputState { &saved, writeState };
    const bool version3Saved = state->save(plugin, &outputState)
        && saved.bytes.size() > 400u * 2u * sizeof(float);
    ok = ok && version3Saved;
    plugin->reset(plugin);
    saved.offset = 0u;
    clap_istream_t inputState { &saved, readState };
    const bool version3Loaded = state->load(plugin, &inputState);
    ok = ok && version3Loaded;
    double restoredShape = 0.0;
    double restoredMotion = 0.0;
    const bool version3Shape = params->get_value(plugin, 23u, &restoredShape)
        && restoredShape == 8.0;
    const bool version3Motion = params->get_value(plugin, 3u, &restoredMotion)
        && restoredMotion == 7.0;
    ok = ok && version3Shape && version3Motion;
    inputLeft.assign(512u, 0.0f);
    inputRight.assign(512u, 0.0f);
    outputLeft.assign(512u, 0.0f);
    outputRight.assign(512u, 0.0f);
    const bool version3Audio = processBlock(plugin, inputLeft, inputRight,
        outputLeft, outputRight, nullptr)
        && energy(outputLeft) > 0.001f && energy(outputRight) > 0.001f;
    ok = ok && version3Audio;
    if (!(version3Saved && version3Loaded && version3Shape && version3Motion
          && version3Audio)) {
        std::cerr << "CRCLTR version 3 state: saved=" << version3Saved
                  << " loaded=" << version3Loaded << " shape="
                  << restoredShape << " motion=" << restoredMotion
                  << " audio=" << version3Audio
                  << " bytes=" << saved.bytes.size() << " offset="
                  << saved.offset << "\n";
    }
    checkpoint("version 3 state round trip");

    // The clear actions erase one embedded capture at a time and always
    // return to their non-latching Ready value.
    EventList clearLoopA;
    clearLoopA.add(3u, 0.0);
    clearLoopA.add(4u, 0.0);
    clearLoopA.add(24u, 1.0);
    clearLoopA.add(24u, 0.0);
    inputLeft.assign(256u, 0.0f);
    inputRight.assign(256u, 0.0f);
    outputLeft.assign(256u, 0.0f);
    outputRight.assign(256u, 0.0f);
    double clearValue = 1.0;
    const bool clearARequested = processBlock(plugin, inputLeft, inputRight,
        outputLeft, outputRight, &clearLoopA.interface)
        && params->get_value(plugin, 24u, &clearValue)
        && clearValue == 0.0;
    const bool clearASilent = processBlock(plugin, inputLeft, inputRight,
        outputLeft, outputRight, nullptr);
    const float clearALeftEnergy = energy(outputLeft);
    const float clearARightEnergy = energy(outputRight);
    const bool clearASettled = clearASilent
        && clearALeftEnergy < 1.0e-20f && clearARightEnergy < 1.0e-20f;
    ok = ok && clearARequested && clearASettled;
    EventList auditionLoopB;
    auditionLoopB.add(4u, 1.0);
    const bool loopBProcessed = processBlock(plugin, inputLeft, inputRight,
        outputLeft, outputRight, &auditionLoopB.interface);
    const float loopBLeftEnergy = energy(outputLeft);
    const float loopBRightEnergy = energy(outputRight);
    const bool loopBRemains = loopBProcessed
        && loopBLeftEnergy > 0.001f && loopBRightEnergy > 0.001f;
    ok = ok && loopBRemains;
    EventList clearLoopB;
    clearLoopB.add(25u, 1.0);
    clearLoopB.add(25u, 0.0);
    clearValue = 1.0;
    const bool clearBProcessed = processBlock(plugin, inputLeft, inputRight,
        outputLeft, outputRight, &clearLoopB.interface);
    const float clearBLeftEnergy = energy(outputLeft);
    const float clearBRightEnergy = energy(outputRight);
    const bool clearBComplete = clearBProcessed
        && clearBLeftEnergy == 0.0f && clearBRightEnergy == 0.0f
        && params->get_value(plugin, 25u, &clearValue)
        && clearValue == 0.0;
    ok = ok && clearBComplete;
    if (!(clearARequested && clearASettled && loopBRemains
          && clearBComplete)) {
        std::cerr << "CRCLTR clear actions: requestA=" << clearARequested
                  << " clearA=" << clearALeftEnergy << ","
                  << clearARightEnergy << " loopB=" << loopBLeftEnergy
                  << "," << loopBRightEnergy << " clearB="
                  << clearBLeftEnergy << "," << clearBRightEnergy
                  << " value=" << clearValue << "\n";
    }
    checkpoint("independent loop clear actions");

    // Version-2 captured-loop states predate Fade Shape and upgrade to the
    // original equal-power law without shifting their embedded audio offset.
    EventList preV2Changes;
    preV2Changes.add(23u, 3.0);
    params->flush(plugin, &preV2Changes.interface, nullptr);
    SavedStateV2 v2;
    v2.crossfade = 0.0f;
    v2.blend = 1.0f;
    v2.audioSampleRate = 1000.0;
    v2.loopFrames[0] = 128u;
    StateMemory v2Memory;
    const auto* v2First = reinterpret_cast<const uint8_t*>(&v2);
    v2Memory.bytes.assign(v2First, v2First + sizeof(v2));
    const std::vector<float> v2Left(v2.loopFrames[0], 0.20f);
    const std::vector<float> v2Right(v2.loopFrames[0], -0.16f);
    const auto appendV2Channel = [&](const std::vector<float>& channel) {
        const auto* first = reinterpret_cast<const uint8_t*>(channel.data());
        v2Memory.bytes.insert(v2Memory.bytes.end(), first,
            first + channel.size() * sizeof(float));
    };
    appendV2Channel(v2Left);
    appendV2Channel(v2Right);
    clap_istream_t v2Input { &v2Memory, readState };
    restoredShape = 3.0;
    ok = ok && state->load(plugin, &v2Input)
        && v2Memory.offset == v2Memory.bytes.size()
        && params->get_value(plugin, 23u, &restoredShape)
        && restoredShape == 0.0;
    inputLeft.assign(256u, 0.0f);
    inputRight.assign(256u, 0.0f);
    outputLeft.assign(256u, 0.0f);
    outputRight.assign(256u, 0.0f);
    ok = ok && processBlock(plugin, inputLeft, inputRight,
        outputLeft, outputRight, nullptr)
        && energy(outputLeft) > 0.001f && energy(outputRight) > 0.001f;
    checkpoint("version 2 state upgrade");

    // Version-1 control-only state keeps its original interpretation.
    EventList preLegacyChanges;
    preLegacyChanges.add(12u, 2.0);
    preLegacyChanges.add(14u, 1.0);
    preLegacyChanges.add(23u, 3.0);
    params->flush(plugin, &preLegacyChanges.interface, nullptr);
    LegacyState legacy;
    legacy.loop1Rate = 1.25f;
    StateMemory legacyMemory;
    const auto* legacyFirst = reinterpret_cast<const uint8_t*>(&legacy);
    legacyMemory.bytes.assign(legacyFirst, legacyFirst + sizeof(legacy));
    clap_istream_t legacyInput { &legacyMemory, readState };
    double restoredRate = 0.0;
    double restoredModel = 1.0;
    double restoredRecordMode = 2.0;
    double restoredReverse = 1.0;
    double restoredLoop2Join = 0.0;
    restoredShape = 3.0;
    ok = ok && state->load(plugin, &legacyInput)
        && legacyMemory.offset == legacyMemory.bytes.size()
        && params->get_value(plugin, 1u, &restoredRate)
        && params->get_value(plugin, 11u, &restoredModel)
        && params->get_value(plugin, 12u, &restoredRecordMode)
        && params->get_value(plugin, 14u, &restoredReverse)
        && params->get_value(plugin, 21u, &restoredLoop2Join)
        && params->get_value(plugin, 23u, &restoredShape)
        && std::abs(restoredRate - 1.25) < 0.0001
        && restoredModel == 0.0
        && restoredRecordMode == 0.0
        && restoredReverse == 0.0
        && restoredLoop2Join == 1.0
        && restoredShape == 0.0;
    checkpoint("version 1 state upgrade");
    inputLeft.assign(256u, 0.0f);
    inputRight.assign(256u, 0.0f);
    outputLeft.assign(256u, 0.0f);
    outputRight.assign(256u, 0.0f);
    ok = ok && processBlock(plugin, inputLeft, inputRight,
        outputLeft, outputRight, nullptr)
        && energy(outputLeft) == 0.0f && energy(outputRight) == 0.0f;

    if (plugin) {
        plugin->stop_processing(plugin);
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
    }
    if (entry) entry->deinit();
    dlclose(library);

    if (!ok) {
        std::cerr << "CRCLTR CLAP smoke test failed\n";
        return 1;
    }
    std::cout << "CRCLTR CLAP smoke test passed\n";
    return 0;
}
