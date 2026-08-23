#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/note-name.h>
#include <clap/ext/note-ports.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>

#include "../plugins/common/s3g_sample_storage.h"

#include <dlfcn.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kStateMagic = 0x50533353u;
constexpr uint32_t kLegacyStateVersion = 1u;
constexpr uint32_t kExpandedStateVersion = 3u;
constexpr uint32_t kPitchStateVersion = 4u;
constexpr uint32_t kPreviousStateVersion = 5u;
constexpr uint32_t kStateVersion = 6u;
constexpr std::size_t kLegacyParamCount = 15u;
constexpr std::size_t kExpandedParamCount = 20u;
constexpr std::size_t kPitchParamCount = 21u;
constexpr std::size_t kParamCount = 28u;
constexpr std::size_t kPathBytes = 1024u;

struct LegacyFixtureState {
    uint32_t magic = kStateMagic;
    uint32_t version = kLegacyStateVersion;
    clap_id outputConfigId = 3002u;
    uint32_t parameterCount = static_cast<uint32_t>(kLegacyParamCount);
    std::array<double, kLegacyParamCount> parameters {{
        0.0, 0.0, 1.0, 0.0, 1.0,
        0.0, 0.0, 60.0,
        0.0, 0.0, 1.0, 50.0,
        0.0, 0.0, 1.0,
    }};
    std::array<char, kPathBytes> path {};
    uint8_t embedded = 1u;
    uint8_t channelCount = 2u;
    uint8_t reserved0 = 0u;
    uint8_t reserved1 = 0u;
    uint32_t frameCount = 64u;
    double sampleRate = 48000.0;
};

struct CurrentFixtureState {
    uint32_t magic = kStateMagic;
    uint32_t version = kStateVersion;
    clap_id outputConfigId = 3002u;
    uint32_t parameterCount = static_cast<uint32_t>(kParamCount);
    std::array<double, kParamCount> parameters {};
    std::array<char, kPathBytes> path {};
    uint8_t embedded = 0u;
    uint8_t channelCount = 0u;
    uint8_t reserved0 = 0u;
    uint8_t reserved1 = 0u;
    uint32_t frameCount = 0u;
    double sampleRate = 0.0;
};

struct ExpandedFixtureState {
    uint32_t magic = kStateMagic;
    uint32_t version = kExpandedStateVersion;
    clap_id outputConfigId = 3002u;
    uint32_t parameterCount = static_cast<uint32_t>(kExpandedParamCount);
    std::array<double, kExpandedParamCount> parameters {};
    std::array<char, kPathBytes> path {};
    uint8_t embedded = 0u;
    uint8_t channelCount = 0u;
    uint8_t reserved0 = 0u;
    uint8_t reserved1 = 0u;
    uint32_t frameCount = 0u;
    double sampleRate = 0.0;
};

struct PitchFixtureState {
    uint32_t magic = kStateMagic;
    uint32_t version = kPitchStateVersion;
    clap_id outputConfigId = 3002u;
    uint32_t parameterCount = static_cast<uint32_t>(kPitchParamCount);
    std::array<double, kPitchParamCount> parameters {};
    std::array<char, kPathBytes> path {};
    uint8_t embedded = 0u;
    uint8_t channelCount = 0u;
    uint8_t reserved0 = 0u;
    uint8_t reserved1 = 0u;
    uint32_t frameCount = 0u;
    double sampleRate = 0.0;
};

struct StateBuffer {
    clap_ostream_t output {};
    clap_istream_t input {};
    std::vector<uint8_t> bytes;
    std::size_t cursor = 0u;

    StateBuffer()
    {
        output.ctx = this;
        output.write = [](const clap_ostream_t* stream, const void* source,
                           uint64_t count) -> int64_t {
            auto* self = static_cast<StateBuffer*>(stream->ctx);
            if (!self || !source) return -1;
            const auto* first = static_cast<const uint8_t*>(source);
            self->bytes.insert(self->bytes.end(), first, first + count);
            return static_cast<int64_t>(count);
        };
        input.ctx = this;
        input.read = [](const clap_istream_t* stream, void* destination,
                        uint64_t count) -> int64_t {
            auto* self = static_cast<StateBuffer*>(stream->ctx);
            if (!self || !destination || self->cursor > self->bytes.size())
                return -1;
            const std::size_t available = self->bytes.size() - self->cursor;
            const std::size_t amount = std::min<std::size_t>(
                static_cast<std::size_t>(count), available);
            if (amount == 0u) return 0;
            std::memcpy(destination, self->bytes.data() + self->cursor,
                amount);
            self->cursor += amount;
            return static_cast<int64_t>(amount);
        };
    }
};

struct NoteEvents {
    clap_event_note_t event {};
    clap_input_events_t input {};

    NoteEvents()
    {
        event.header.size = sizeof(event);
        event.header.time = 1u;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_NOTE_ON;
        event.note_id = 1;
        event.port_index = 0;
        event.channel = 0;
        event.key = 60;
        event.velocity = 1.0;
        input.ctx = this;
        input.size = [](const clap_input_events_t*) { return 1u; };
        input.get = [](const clap_input_events_t* list, uint32_t index)
            -> const clap_event_header_t* {
            const auto* self = static_cast<const NoteEvents*>(list->ctx);
            return index == 0u ? &self->event.header : nullptr;
        };
    }
};

struct ParamEvents {
    clap_event_param_value_t event {};
    clap_input_events_t input {};

    ParamEvents(clap_id id, double value)
    {
        event.header.size = sizeof(event);
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_PARAM_VALUE;
        event.param_id = id;
        event.note_id = -1;
        event.port_index = -1;
        event.channel = -1;
        event.key = -1;
        event.value = value;
        input.ctx = this;
        input.size = [](const clap_input_events_t*) { return 1u; };
        input.get = [](const clap_input_events_t* list, uint32_t index)
            -> const clap_event_header_t* {
            const auto* self = static_cast<const ParamEvents*>(list->ctx);
            return index == 0u ? &self->event.header : nullptr;
        };
    }
};

struct HostContext {
    clap_host_t host {};
};

const void* hostGetExtension(const clap_host_t*, const char*)
{
    return nullptr;
}

void hostRequest(const clap_host_t*) {}

std::filesystem::path resolveBinary(const std::filesystem::path& supplied)
{
    if (std::filesystem::is_regular_file(supplied)) return supplied;
#if defined(__APPLE__)
    const auto macOS = supplied / "Contents" / "MacOS";
    if (std::filesystem::is_directory(macOS)) {
        for (const auto& entry : std::filesystem::directory_iterator(macOS))
            if (entry.is_regular_file()) return entry.path();
    }
#endif
    return {};
}

bool expect(bool condition, const char* message)
{
    if (condition) return true;
    std::fprintf(stderr, "sample player CLAP: %s\n", message);
    return false;
}

struct RegistrationProbe {
    using Callback = intptr_t (*)(void*, int, void*);
    int additions = 0;
    int removals = 0;
    void* userData = nullptr;
    Callback callback = nullptr;
    std::string addedPath;
    std::string removedPath;
};

RegistrationProbe* activeRegistrationProbe = nullptr;

struct ProjectQueryProbe {
    void* firstProject = nullptr;
    void* exactProject = nullptr;
    std::string firstPath;
    std::string exactPath;
    std::string mediaPath;
};

ProjectQueryProbe* activeProjectQueryProbe = nullptr;

int registrationProbe(const char* name, void* opaqueArguments)
{
    if (!activeRegistrationProbe || !name || !opaqueArguments) return 0;
    auto** arguments = static_cast<void**>(opaqueArguments);
    if (std::strcmp(name, "file_in_project_ex2") == 0) {
        ++activeRegistrationProbe->additions;
        activeRegistrationProbe->addedPath = static_cast<const char*>(
            arguments[0u]);
        activeRegistrationProbe->userData = arguments[2u];
        activeRegistrationProbe->callback
            = reinterpret_cast<RegistrationProbe::Callback>(arguments[3u]);
        return 1;
    }
    if (std::strcmp(name, "-file_in_project_ex2") == 0) {
        ++activeRegistrationProbe->removals;
        activeRegistrationProbe->removedPath = static_cast<const char*>(
            arguments[0u]);
        return 1;
    }
    return 0;
}

void* unusedGetContext(const clap_host_t*, int) { return nullptr; }

void* queryEnumProjects(int index, char* path, int capacity)
{
    if (!activeProjectQueryProbe) return nullptr;
    const std::string* selectedPath = nullptr;
    void* project = nullptr;
    if (index == 0) {
        project = activeProjectQueryProbe->firstProject;
        selectedPath = &activeProjectQueryProbe->firstPath;
    } else if (index == 1) {
        project = activeProjectQueryProbe->exactProject;
        selectedPath = &activeProjectQueryProbe->exactPath;
    }
    if (project && path && capacity > 0)
        std::snprintf(path, static_cast<std::size_t>(capacity), "%s",
            selectedPath->c_str());
    return project;
}

void queryGetProjectPathEx(void* project, char* path, int capacity)
{
    if (!activeProjectQueryProbe
        || project != activeProjectQueryProbe->exactProject
        || !path || capacity <= 0) return;
    std::snprintf(path, static_cast<std::size_t>(capacity), "%s",
        activeProjectQueryProbe->mediaPath.c_str());
}

void captureRegisteredRename(void* owner, const std::string& absolutePath)
{
    if (owner) *static_cast<std::string*>(owner) = absolutePath;
}

bool exerciseProjectStorageHelpers()
{
    namespace storage = s3g::sample_storage;
    bool ok = true;
    const auto serial = std::chrono::steady_clock::now()
        .time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path()
        / ("s3g-player-storage-" + std::to_string(serial));
    const auto media = root / "Media";
    const auto source = root / "Sample Kick #1.WAV";
    std::error_code error;
    std::filesystem::create_directories(media, error);
    {
        std::ofstream output(source, std::ios::binary);
        output.write("abc", 3);
    }
    storage::ProjectLocation location;
    location.project = reinterpret_cast<void*>(uintptr_t { 1u });
    location.fxDsp = reinterpret_cast<void*>(uintptr_t { 2u });
    location.projectFilePath = (root / "fixture.rpp").string();
    location.mediaDirectory = media.string();
    location.saved = true;
    const auto copied = storage::copyFileIntoProject(location,
        source.string());
    ok &= expect(copied.success
            && copied.contentHash
                == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9c"
                    "b410ff61f20015ad"
            && std::filesystem::path(copied.relativePath).filename()
                == "sample-kick-1-ba7816bf8f01cfea.wav"
            && copied.byteCount == 3u,
        "project copy did not use verified readable content addressing");
    std::string resolved;
    ok &= expect(storage::resolveProjectRelativePath(location,
            copied.relativePath, resolved)
            && resolved == copied.absolutePath,
        "project-relative sample path did not round-trip");
    const auto repeated = storage::copyFileIntoProject(location,
        source.string());
    ok &= expect(repeated.success
            && repeated.absolutePath == copied.absolutePath,
        "identical project copy was not stable");
    const auto collisionSource = root / "Collision.wav";
    {
        std::ofstream output(collisionSource, std::ios::binary);
        output.write("abc", 3);
    }
    const auto collisionShort = media / "s3g Samples"
        / "collision-ba7816bf8f01cfea.wav";
    {
        std::ofstream output(collisionShort, std::ios::binary);
        output.write("occupied", 8);
    }
    const auto collision = storage::copyFileIntoProject(location,
        collisionSource.string());
    ok &= expect(collision.success
            && std::filesystem::path(collision.absolutePath).filename()
                == copied.contentHash + ".wav",
        "short-hash collision did not fall back to the full digest");
    {
        std::ofstream output(collision.absolutePath,
            std::ios::binary | std::ios::trunc);
        output.write("occupied", 8);
    }
    const auto occupiedFull = storage::copyFileIntoProject(location,
        collisionSource.string());
    ok &= expect(!occupiedFull.success,
        "mismatched full-hash destination was overwritten");

    clap_host_t host {};
    storage::ReaperHostBridge bridge {};
    storage::ReaperContext context;
    context.host = &host;
    context.bridge = &bridge;
    context.getContext = unusedGetContext;
    context.enumProjects = queryEnumProjects;
    context.getProjectPathEx = queryGetProjectPathEx;
    context.registerObject = registrationProbe;
    context.project = location.project;
    context.fxDsp = location.fxDsp;
    ProjectQueryProbe queryProbe;
    queryProbe.firstProject = reinterpret_cast<void*>(uintptr_t { 3u });
    queryProbe.exactProject = context.project;
    queryProbe.firstPath = (root / "other.rpp").string();
    queryProbe.exactPath = location.projectFilePath;
    queryProbe.mediaPath = "Media";
    activeProjectQueryProbe = &queryProbe;
    storage::ProjectLocation queriedLocation;
    std::string queryError;
    ok &= expect(storage::queryProjectLocation(context, queriedLocation,
            &queryError)
            && queriedLocation.projectFilePath == location.projectFilePath
            && queriedLocation.mediaDirectory == media.string(),
        "REAPER project query did not pointer-match this instance or resolve "
        "its media path");
    RegistrationProbe probe;
    activeRegistrationProbe = &probe;
    std::string renamedPath;
    storage::ProjectFileRegistration registration;
    ok &= expect(context.available() && context.canRegisterProjectFiles()
            && registration.reset(context, copied.absolutePath,
                &renamedPath, captureRegisteredRename, "Player Fixture")
            && probe.additions == 1 && probe.callback,
        "project-file registration capability was not accepted");
    const std::string movedPath = (media / "s3g Samples"
        / "moved-sample.wav").string();
    if (probe.callback)
        probe.callback(probe.userData, 0,
            const_cast<char*>(movedPath.c_str()));
    ok &= expect(renamedPath == movedPath
            && registration.absolutePath() == movedPath,
        "project-file rename callback did not publish the new path");
    registration.clear();
    ok &= expect(probe.removals == 1 && probe.removedPath == movedPath,
        "project-file registration was not balanced with the renamed path");
    activeRegistrationProbe = nullptr;
    activeProjectQueryProbe = nullptr;
    std::filesystem::remove_all(root, error);
    return ok;
}

void fillEmbeddedFixture(StateBuffer& state, uint32_t channels,
    clap_id configId)
{
    LegacyFixtureState fixture;
    fixture.outputConfigId = configId;
    fixture.channelCount = static_cast<uint8_t>(channels);
    const auto* header = reinterpret_cast<const uint8_t*>(&fixture);
    state.bytes.insert(state.bytes.end(), header, header + sizeof(fixture));
    for (uint32_t channel = 0u; channel < channels; ++channel) {
        std::array<float, 64u> samples {};
        samples.fill(static_cast<float>(channel + 1u) * 0.1f);
        const auto* bytes = reinterpret_cast<const uint8_t*>(samples.data());
        state.bytes.insert(state.bytes.end(), bytes, bytes + sizeof(samples));
    }
}

bool exerciseDescriptor(const clap_plugin_factory_t* factory,
    const clap_host_t* host, const char* id, const char* expectedName,
    uint32_t channels, clap_id configId)
{
    bool ok = true;
    const clap_plugin_t* plugin = factory->create_plugin(factory, host, id);
    ok &= expect(plugin && plugin->init(plugin), "creation failed");
    if (!plugin) return false;
    ok &= expect(std::strcmp(plugin->desc->name, expectedName) == 0,
        "descriptor name mismatch");
    const auto* ports = static_cast<const clap_plugin_audio_ports_t*>(
        plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS));
    const auto* notePorts = static_cast<const clap_plugin_note_ports_t*>(
        plugin->get_extension(plugin, CLAP_EXT_NOTE_PORTS));
    const auto* noteNames = static_cast<const clap_plugin_note_name_t*>(
        plugin->get_extension(plugin, CLAP_EXT_NOTE_NAME));
    const auto* params = static_cast<const clap_plugin_params_t*>(
        plugin->get_extension(plugin, CLAP_EXT_PARAMS));
    const auto* state = static_cast<const clap_plugin_state_t*>(
        plugin->get_extension(plugin, CLAP_EXT_STATE));
    StateBuffer freshState;
    CurrentFixtureState freshSaved {};
    ok &= expect(state && state->save(plugin, &freshState.output)
            && freshState.bytes.size() == sizeof(freshSaved),
        "fresh Player state failed to save");
    if (freshState.bytes.size() >= sizeof(freshSaved))
        std::memcpy(&freshSaved, freshState.bytes.data(), sizeof(freshSaved));
    ok &= expect(freshSaved.version == kStateVersion
            && freshSaved.reserved0 == static_cast<uint8_t>(
                s3g::sample_storage::StorageMode::Project)
            && freshSaved.embedded == 0u,
        "new Player instances do not default to PROJECT storage");
    clap_audio_port_info_t portInfo {};
    ok &= expect(ports && ports->get(plugin, 0u, false, &portInfo)
            && portInfo.channel_count == channels,
        "fixed output width mismatch");
    ok &= expect(notePorts && notePorts->count(plugin, true) == 1u,
        "note input missing");
    const uint32_t expectedExposedParamCount = static_cast<uint32_t>(
        kParamCount - (channels == 16u ? 1u : 0u));
    ok &= expect(params
            && params->count(plugin) == expectedExposedParamCount,
        "parameter surface mismatch");
    clap_param_info_t attackInfo {};
    clap_param_info_t decayInfo {};
    clap_param_info_t releaseInfo {};
    clap_param_info_t playModeInfo {};
    clap_param_info_t crossfadeInfo {};
    clap_param_info_t filterTypeInfo {};
    clap_param_info_t cutoffInfo {};
    clap_param_info_t pitchModeInfo {};
    clap_param_info_t syncModeInfo {};
    clap_param_info_t sourceTempoInfo {};
    clap_param_info_t triggerModeInfo {};
    clap_param_info_t retriggerModeInfo {};
    clap_param_info_t voiceModeInfo {};
    clap_param_info_t glideInfo {};
    clap_param_info_t midiReceiveInfo {};
    char envelopeText[32] {};
    char modeText[32] {};
    char filterText[32] {};
    char cutoffText[32] {};
    char pitchModeText[32] {};
    char splitPitchModeText[40] {};
    char midiReceiveText[32] {};
    double envelopeValue = -1.0;
    double modeValue = -1.0;
    double filterValue = -1.0;
    double cutoffValue = -1.0;
    double pitchModeValue = -1.0;
    double splitPitchModeValue = -1.0;
    double midiReceiveValue = -1.0;
    const auto findParam = [&](clap_id id, clap_param_info_t& found) {
        if (!params) return false;
        for (uint32_t index = 0u; index < params->count(plugin); ++index) {
            clap_param_info_t candidate {};
            if (params->get_info(plugin, index, &candidate)
                && candidate.id == id) {
                found = candidate;
                return true;
            }
        }
        return false;
    };
    clap_param_info_t panInfo {};
    const bool exposesPan = findParam(14u, panInfo);
    double panValue = -1.0;
    ok &= expect(params && findParam(1u, playModeInfo)
            && findParam(9u, attackInfo)
            && findParam(10u, decayInfo)
            && findParam(12u, releaseInfo)
            && findParam(16u, crossfadeInfo)
            && findParam(17u, filterTypeInfo)
            && findParam(18u, cutoffInfo)
            && findParam(21u, pitchModeInfo)
            && findParam(22u, syncModeInfo)
            && findParam(23u, sourceTempoInfo)
            && findParam(24u, triggerModeInfo)
            && findParam(25u, retriggerModeInfo)
            && findParam(26u, voiceModeInfo)
            && findParam(27u, glideInfo)
            && findParam(28u, midiReceiveInfo)
            && (channels == 2u
                ? exposesPan && params->get_value(plugin, 14u, &panValue)
                : !exposesPan && !params->get_value(
                    plugin, 14u, &panValue))
            && playModeInfo.id == 1u && playModeInfo.max_value == 5.0
            && attackInfo.id == 9u && decayInfo.id == 10u
            && releaseInfo.id == 12u
            && attackInfo.max_value == 1.0
            && decayInfo.max_value == 1.0
            && releaseInfo.max_value == 1.0
            && params->value_to_text(plugin, attackInfo.id, 0.25,
                envelopeText, sizeof(envelopeText))
            && std::strcmp(envelopeText, "25.0 %") == 0
            && params->text_to_value(plugin, releaseInfo.id, "25 %",
                &envelopeValue)
            && std::fabs(envelopeValue - 0.25) < 1.0e-9
            && crossfadeInfo.id == 16u
            && crossfadeInfo.max_value == 0.5
            && std::fabs(crossfadeInfo.default_value - 0.02) < 1.0e-9
            && filterTypeInfo.id == 17u
            && filterTypeInfo.max_value == 4.0
            && cutoffInfo.id == 18u && cutoffInfo.min_value == 20.0
            && cutoffInfo.max_value == 20000.0
            && pitchModeInfo.id == 21u
            && pitchModeInfo.max_value == 2.0
            && pitchModeInfo.default_value == 0.0
            && syncModeInfo.max_value == 1.0
            && syncModeInfo.default_value == 0.0
            && sourceTempoInfo.min_value == 20.0
            && sourceTempoInfo.max_value == 999.0
            && sourceTempoInfo.default_value == 120.0
            && triggerModeInfo.max_value == 3.0
            && triggerModeInfo.default_value == 0.0
            && retriggerModeInfo.max_value == 2.0
            && retriggerModeInfo.default_value == 0.0
            && voiceModeInfo.max_value == 2.0
            && voiceModeInfo.default_value == 0.0
            && glideInfo.max_value == 2000.0
            && glideInfo.default_value == 0.0
            && midiReceiveInfo.max_value == 16.0
            && midiReceiveInfo.default_value == 0.0
            && params->value_to_text(plugin, playModeInfo.id, 4.0,
                modeText, sizeof(modeText))
            && std::strcmp(modeText, "Forward Ping-Pong") == 0
            && params->text_to_value(plugin, playModeInfo.id,
                "Reverse Ping-Pong", &modeValue)
            && std::fabs(modeValue - 5.0) < 1.0e-9
            && params->value_to_text(plugin, filterTypeInfo.id, 2.0,
                filterText, sizeof(filterText))
            && std::strcmp(filterText, "Band Pass") == 0
            && params->text_to_value(plugin, filterTypeInfo.id,
                "High Pass", &filterValue)
            && std::fabs(filterValue - 3.0) < 1.0e-9
            && params->value_to_text(plugin, cutoffInfo.id, 3200.0,
                cutoffText, sizeof(cutoffText))
            && std::strcmp(cutoffText, "3.20 kHz") == 0
            && params->text_to_value(plugin, cutoffInfo.id, cutoffText,
                &cutoffValue)
            && std::fabs(cutoffValue - 3200.0) < 1.0e-9
            && params->value_to_text(plugin, pitchModeInfo.id, 1.0,
                pitchModeText, sizeof(pitchModeText))
            && std::strcmp(pitchModeText, "Stretch") == 0
            && params->text_to_value(plugin, pitchModeInfo.id, "Rate",
                &pitchModeValue)
            && std::fabs(pitchModeValue) < 1.0e-9
            && params->value_to_text(plugin, pitchModeInfo.id, 2.0,
                splitPitchModeText, sizeof(splitPitchModeText))
            && std::strcmp(splitPitchModeText,
                "Rate Below / Stretch Above") == 0
            && params->text_to_value(plugin, pitchModeInfo.id,
                "Rate Below / Stretch Above", &splitPitchModeValue)
            && std::fabs(splitPitchModeValue - 2.0) < 1.0e-9
            && params->value_to_text(plugin, midiReceiveInfo.id, 16.0,
                midiReceiveText, sizeof(midiReceiveText))
            && std::strcmp(midiReceiveText, "Channel 16") == 0
            && params->text_to_value(plugin, midiReceiveInfo.id, "Omni",
                &midiReceiveValue)
            && std::fabs(midiReceiveValue) < 1.0e-9,
        "expanded playback, voice, sync, MIDI, or pitch contract is invalid");

    CurrentFixtureState previousFixture;
    previousFixture.version = kPreviousStateVersion;
    previousFixture.outputConfigId = configId;
    std::snprintf(previousFixture.path.data(), previousFixture.path.size(),
        "%s", "/missing/legacy-linked-sample.wav");
    StateBuffer previousState;
    const auto* previousBytes = reinterpret_cast<const uint8_t*>(
        &previousFixture);
    previousState.bytes.insert(previousState.bytes.end(), previousBytes,
        previousBytes + sizeof(previousFixture));
    ok &= expect(state && state->load(plugin, &previousState.input),
        "version 5 linked state failed to migrate");
    StateBuffer previousRoundTrip;
    CurrentFixtureState previousMigrated {};
    ok &= expect(state && state->save(plugin, &previousRoundTrip.output)
            && previousRoundTrip.bytes.size() == sizeof(previousMigrated),
        "version 5 linked state failed to save after migration");
    if (previousRoundTrip.bytes.size() >= sizeof(previousMigrated))
        std::memcpy(&previousMigrated, previousRoundTrip.bytes.data(),
            sizeof(previousMigrated));
    ok &= expect(previousMigrated.version == kStateVersion
            && previousMigrated.reserved0 == static_cast<uint8_t>(
                s3g::sample_storage::StorageMode::Link)
            && std::strcmp(previousMigrated.path.data(),
                previousFixture.path.data()) == 0,
        "version 5 linked state did not become LINK or preserve its locator");

    ExpandedFixtureState expandedFixture;
    expandedFixture.outputConfigId = configId;
    StateBuffer expandedState;
    const auto* expandedBytes = reinterpret_cast<const uint8_t*>(
        &expandedFixture);
    expandedState.bytes.insert(expandedState.bytes.end(), expandedBytes,
        expandedBytes + sizeof(expandedFixture));
    ok &= expect(state && state->load(plugin, &expandedState.input),
        "version 3 state failed to migrate to Rate pitch mode");
    StateBuffer expandedRoundTrip;
    CurrentFixtureState expandedMigrated {};
    ok &= expect(state && state->save(plugin, &expandedRoundTrip.output)
            && expandedRoundTrip.bytes.size() >= sizeof(expandedMigrated),
        "version 3 state failed to save after migration");
    if (expandedRoundTrip.bytes.size() >= sizeof(expandedMigrated))
        std::memcpy(&expandedMigrated, expandedRoundTrip.bytes.data(),
            sizeof(expandedMigrated));
    ok &= expect(expandedMigrated.version == kStateVersion
            && expandedMigrated.parameterCount == kParamCount
            && expandedMigrated.parameters[20u] == 0.0
            && expandedMigrated.parameters[21u] == 0.0
            && expandedMigrated.parameters[22u] == 120.0
            && expandedMigrated.parameters[23u] == 0.0
            && expandedMigrated.parameters[24u] == 0.0
            && expandedMigrated.parameters[25u] == 0.0
            && expandedMigrated.parameters[26u] == 0.0
            && expandedMigrated.parameters[27u] == 0.0,
        "version 3 state did not receive compatibility playback defaults");

    PitchFixtureState pitchFixture;
    pitchFixture.outputConfigId = configId;
    StateBuffer pitchState;
    const auto* pitchBytes = reinterpret_cast<const uint8_t*>(&pitchFixture);
    pitchState.bytes.insert(pitchState.bytes.end(), pitchBytes,
        pitchBytes + sizeof(pitchFixture));
    ok &= expect(state && state->load(plugin, &pitchState.input),
        "version 4 state failed to migrate");
    StateBuffer pitchRoundTrip;
    CurrentFixtureState pitchMigrated {};
    ok &= expect(state && state->save(plugin, &pitchRoundTrip.output)
            && pitchRoundTrip.bytes.size() >= sizeof(pitchMigrated),
        "version 4 state failed to save after migration");
    if (pitchRoundTrip.bytes.size() >= sizeof(pitchMigrated))
        std::memcpy(&pitchMigrated, pitchRoundTrip.bytes.data(),
            sizeof(pitchMigrated));
    ok &= expect(pitchMigrated.version == kStateVersion
            && pitchMigrated.parameterCount == kParamCount
            && pitchMigrated.parameters[21u] == 0.0
            && pitchMigrated.parameters[22u] == 120.0
            && pitchMigrated.parameters[23u] == 0.0
            && pitchMigrated.parameters[24u] == 0.0
            && pitchMigrated.parameters[25u] == 0.0
            && pitchMigrated.parameters[26u] == 0.0
            && pitchMigrated.parameters[27u] == 0.0,
        "version 4 state did not preserve legacy-default playback behavior");

    StateBuffer fixture;
    fillEmbeddedFixture(fixture, channels, configId);
    ok &= expect(state && state->load(plugin, &fixture.input)
            && noteNames && noteNames->count(plugin) == 1u,
        "embedded sample state failed to load");
    StateBuffer roundTrip;
    ok &= expect(state && state->save(plugin, &roundTrip.output)
            && roundTrip.bytes.size() == sizeof(CurrentFixtureState)
                + static_cast<std::size_t>(channels) * 64u * sizeof(float),
        "embedded sample state failed to round-trip");
    CurrentFixtureState migrated {};
    if (roundTrip.bytes.size() >= sizeof(migrated))
        std::memcpy(&migrated, roundTrip.bytes.data(), sizeof(migrated));
    ok &= expect(migrated.magic == kStateMagic
            && migrated.version == kStateVersion
            && migrated.parameterCount == kParamCount
            && migrated.reserved0 == static_cast<uint8_t>(
                s3g::sample_storage::StorageMode::Embed)
            && migrated.parameters[8u] >= 0.0
            && migrated.parameters[9u] >= 0.0
            && migrated.parameters[11u] >= 0.0
            && migrated.parameters[8u] + migrated.parameters[9u]
                + migrated.parameters[11u] <= 1.000001
            && migrated.parameters[15u] == 0.0
            && migrated.parameters[16u] == 0.0
            && migrated.parameters[17u] == 20000.0
            && migrated.parameters[18u] == 0.0
            && migrated.parameters[19u] == 0.0
            && migrated.parameters[20u] == 0.0
            && migrated.parameters[21u] == 0.0
            && migrated.parameters[22u] == 120.0
            && migrated.parameters[23u] == 0.0
            && migrated.parameters[24u] == 0.0
            && migrated.parameters[25u] == 0.0
            && migrated.parameters[26u] == 0.0
            && migrated.parameters[27u] == 0.0,
        "legacy state did not preserve wraps and playback defaults");

    CurrentFixtureState pathlessProject = migrated;
    pathlessProject.reserved0 = static_cast<uint8_t>(
        s3g::sample_storage::StorageMode::Project);
    pathlessProject.path.fill('\0');
    StateBuffer pathlessProjectState;
    pathlessProjectState.bytes = roundTrip.bytes;
    std::memcpy(pathlessProjectState.bytes.data(), &pathlessProject,
        sizeof(pathlessProject));
    ok &= expect(state && state->load(plugin, &pathlessProjectState.input),
        "pathless PROJECT payload failed to load");
    StateBuffer pathlessProjectSaved;
    CurrentFixtureState pathlessProjectRoundTrip {};
    ok &= expect(state && state->save(plugin, &pathlessProjectSaved.output)
            && pathlessProjectSaved.bytes.size() == roundTrip.bytes.size(),
        "pathless PROJECT did not retain its safety payload");
    if (pathlessProjectSaved.bytes.size()
        >= sizeof(pathlessProjectRoundTrip)) {
        std::memcpy(&pathlessProjectRoundTrip,
            pathlessProjectSaved.bytes.data(),
            sizeof(pathlessProjectRoundTrip));
    }
    ok &= expect(pathlessProjectRoundTrip.reserved0
            == static_cast<uint8_t>(
                s3g::sample_storage::StorageMode::Project)
            && pathlessProjectRoundTrip.embedded == 1u,
        "requested PROJECT was not kept separate from its safety payload");

    CurrentFixtureState pendingProject = pathlessProject;
    std::snprintf(pendingProject.path.data(), pendingProject.path.size(),
        "%s", "/missing/original-project-source.wav");
    StateBuffer pendingProjectState;
    pendingProjectState.bytes = roundTrip.bytes;
    std::memcpy(pendingProjectState.bytes.data(), &pendingProject,
        sizeof(pendingProject));
    ok &= expect(state && state->load(plugin, &pendingProjectState.input),
        "file-backed pending PROJECT payload failed to load");
    StateBuffer pendingProjectSaved;
    CurrentFixtureState pendingProjectRoundTrip {};
    ok &= expect(state && state->save(plugin, &pendingProjectSaved.output)
            && pendingProjectSaved.bytes.size()
                == sizeof(pendingProjectRoundTrip),
        "file-backed pending PROJECT remained embedded");
    if (pendingProjectSaved.bytes.size()
        >= sizeof(pendingProjectRoundTrip)) {
        std::memcpy(&pendingProjectRoundTrip,
            pendingProjectSaved.bytes.data(),
            sizeof(pendingProjectRoundTrip));
    }
    ok &= expect(pendingProjectRoundTrip.reserved0
            == static_cast<uint8_t>(
                s3g::sample_storage::StorageMode::Project)
            && pendingProjectRoundTrip.embedded == 0u
            && std::strcmp(pendingProjectRoundTrip.path.data(),
                pendingProject.path.data()) == 0,
        "pending PROJECT did not preserve its small absolute locator");

    ok &= expect(plugin->activate(plugin, 48000.0, 8u, 16u)
            && plugin->start_processing(plugin),
        "activation failed");

    std::vector<std::array<float, 16u>> rendered(channels);
    std::vector<float*> pointers(channels);
    for (uint32_t channel = 0u; channel < channels; ++channel)
        pointers[channel] = rendered[channel].data();
    clap_audio_buffer_t output {};
    output.data32 = pointers.data();
    output.channel_count = channels;
    output.constant_mask = ~uint64_t { 0u };
    NoteEvents note;
    clap_process_t process {};
    process.frames_count = 16u;
    process.in_events = &note.input;
    process.audio_outputs = &output;
    process.audio_outputs_count = 1u;
    ParamEvents channelTwoOnly(28u, 2.0);
    params->flush(plugin, &channelTwoOnly.input, nullptr);
    ok &= expect(plugin->process(plugin, &process) == CLAP_PROCESS_CONTINUE,
        "MIDI-filtered process call failed");
    ok &= expect(output.constant_mask == 0u,
        "dynamic output retained a host constant-channel mask");
    bool filtered = true;
    for (const auto& channel : rendered) {
        for (float sample : channel) filtered = filtered && sample == 0.0f;
    }
    ok &= expect(filtered,
        "MIDI Receive did not reject a note from another channel");
    plugin->reset(plugin);
    ParamEvents omni(28u, 0.0);
    params->flush(plugin, &omni.input, nullptr);
    ParamEvents forwardLoop(1u, 1.0);
    params->flush(plugin, &forwardLoop.input, nullptr);
    ok &= expect(plugin->process(plugin, &process) == CLAP_PROCESS_CONTINUE,
        "process call failed");
    bool locked = rendered[0u][0u] == 0.0f;
    for (uint32_t channel = 0u; channel < channels; ++channel) {
        locked = locked && std::abs(rendered[channel][1u]
            - static_cast<float>(channel + 1u) * 0.1f) < 1.0e-6f;
    }
    ok &= expect(locked,
        "embedded audio did not render sample-accurately by lane");

    ParamEvents quarterOutput(13u, -12.041199826559248);
    params->flush(plugin, &quarterOutput.input, nullptr);
    ParamEvents hardLeft(14u, -1.0);
    if (channels == 2u) params->flush(plugin, &hardLeft.input, nullptr);
    process.in_events = nullptr;
    ok &= expect(plugin->process(plugin, &process) == CLAP_PROCESS_CONTINUE,
        "post-mix output process call failed");
    bool postOutput = true;
    for (uint32_t channel = 0u; channel < channels; ++channel) {
        const float expected = channels == 2u && channel == 1u
            ? 0.0f : static_cast<float>(channel + 1u) * 0.025f;
        postOutput = postOutput
            && std::abs(rendered[channel][0u] - expected) < 1.0e-6f;
    }
    if (!postOutput) {
        std::fprintf(stderr, "post-mix values:");
        for (uint32_t channel = 0u; channel < channels; ++channel)
            std::fprintf(stderr, " %.7f", rendered[channel][0u]);
        std::fprintf(stderr, "\n");
    }
    ok &= expect(postOutput,
        "Out or stereo Pan did not affect an already-sounding voice");

    ParamEvents unityOutput(13u, 0.0);
    params->flush(plugin, &unityOutput.input, nullptr);
    ParamEvents centerPan(14u, 0.0);
    if (channels == 2u) params->flush(plugin, &centerPan.input, nullptr);
    ParamEvents halfSustain(11u, 0.5);
    params->flush(plugin, &halfSustain.input, nullptr);
    for (uint32_t block = 0u; block < 31u; ++block) {
        ok &= expect(plugin->process(plugin, &process)
                == CLAP_PROCESS_CONTINUE,
            "live Sustain process call failed");
    }
    bool liveSustain = true;
    for (uint32_t channel = 0u; channel < channels; ++channel) {
        const float expected = static_cast<float>(channel + 1u) * 0.05f;
        liveSustain = liveSustain
            && std::abs(rendered[channel][0u] - expected) < 1.0e-5f;
    }
    ok &= expect(liveSustain,
        "Sustain did not update an already-sounding voice");

    ParamEvents shortRelease(12u, 0.1);
    params->flush(plugin, &shortRelease.input, nullptr);
    note.event.header.type = CLAP_EVENT_NOTE_OFF;
    note.event.header.time = 0u;
    note.event.velocity = 0.0;
    process.in_events = &note.input;
    ok &= expect(plugin->process(plugin, &process) == CLAP_PROCESS_CONTINUE,
        "live Release process call failed");
    bool liveRelease = true;
    for (uint32_t channel = 0u; channel < channels; ++channel) {
        liveRelease = liveRelease && rendered[channel][0u] > 0.0f
            && rendered[channel][15u] == 0.0f;
    }
    ok &= expect(liveRelease,
        "Release did not update a held voice before note-off");
    plugin->stop_processing(plugin);
    plugin->deactivate(plugin);
    plugin->destroy(plugin);
    return ok;
}

bool exerciseSupportedSourceWidths(const clap_plugin_factory_t* factory,
    const clap_host_t* host)
{
    bool ok = true;
    for (uint32_t sourceChannels = 1u; sourceChannels <= 16u;
         ++sourceChannels) {
        const clap_plugin_t* plugin = factory->create_plugin(factory, host,
            "org.s3g.s3g-dsp.sample-player-16");
        ok &= expect(plugin && plugin->init(plugin),
            "Player 16 source-width fixture creation failed");
        if (!plugin) return false;

        const auto* state = static_cast<const clap_plugin_state_t*>(
            plugin->get_extension(plugin, CLAP_EXT_STATE));
        StateBuffer fixture;
        fillEmbeddedFixture(fixture, sourceChannels, 3016u);
        ok &= expect(state && state->load(plugin, &fixture.input),
            "Player 16 rejected a supported source width");
        ok &= expect(plugin->activate(plugin, 48000.0, 8u, 16u)
                && plugin->start_processing(plugin),
            "Player 16 source-width fixture activation failed");

        std::vector<std::array<float, 16u>> rendered(sourceChannels);
        std::vector<float*> pointers(sourceChannels);
        for (uint32_t channel = 0u; channel < sourceChannels; ++channel)
            pointers[channel] = rendered[channel].data();
        clap_audio_buffer_t output {};
        output.data32 = pointers.data();
        output.channel_count = sourceChannels;
        output.constant_mask = ~uint64_t { 0u };
        NoteEvents note;
        clap_process_t process {};
        process.frames_count = 16u;
        process.in_events = &note.input;
        process.audio_outputs = &output;
        process.audio_outputs_count = 1u;
        ok &= expect(plugin->process(plugin, &process)
                == CLAP_PROCESS_CONTINUE,
            "Player 16 rejected a connected output prefix wide enough for "
            "its source");
        ok &= expect(output.constant_mask == 0u,
            "Player 16 retained a host constant-channel mask");
        bool lanesPreserved = rendered[0u][0u] == 0.0f;
        for (uint32_t channel = 0u; channel < sourceChannels; ++channel) {
            lanesPreserved = lanesPreserved
                && std::abs(rendered[channel][1u]
                    - static_cast<float>(channel + 1u) * 0.1f) < 1.0e-6f;
        }
        ok &= expect(lanesPreserved,
            "Player 16 did not preserve a supported source width");

        plugin->stop_processing(plugin);
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
    }
    return ok;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::fprintf(stderr,
            "usage: s3g_sample_player_clap_smoke <bundle-or-binary>\n");
        return 2;
    }
    bool ok = exerciseProjectStorageHelpers();
    const auto binary = resolveBinary(argv[1]);
    void* library = !binary.empty()
        ? dlopen(binary.c_str(), RTLD_LOCAL | RTLD_NOW) : nullptr;
    ok &= expect(library != nullptr, library ? "" : dlerror());
    const auto* entry = library ? static_cast<const clap_plugin_entry_t*>(
        dlsym(library, "clap_entry")) : nullptr;
    ok &= expect(entry && entry->init(binary.c_str()),
        "entry initialization failed");

    HostContext context;
    context.host.clap_version = CLAP_VERSION_INIT;
    context.host.name = "s3g sample player smoke";
    context.host.vendor = "s3g";
    context.host.url = "https://github.com/s3g/s3g-dsp";
    context.host.version = "1";
    context.host.get_extension = hostGetExtension;
    context.host.request_restart = hostRequest;
    context.host.request_process = hostRequest;
    context.host.request_callback = hostRequest;
    const auto* factory = entry ? static_cast<const clap_plugin_factory_t*>(
        entry->get_factory(CLAP_PLUGIN_FACTORY_ID)) : nullptr;
    ok &= expect(factory && factory->get_plugin_count(factory) == 2u,
        "expected stereo and 16-channel descriptors");
    if (factory) {
        ok &= exerciseDescriptor(factory, &context.host,
            "org.s3g.s3g-dsp.sample-player", "s3g Sample Player 2",
            2u, 3002u);
        ok &= exerciseDescriptor(factory, &context.host,
            "org.s3g.s3g-dsp.sample-player-16", "s3g Sample Player 16",
            16u, 3016u);
        ok &= exerciseSupportedSourceWidths(factory, &context.host);
    }
    if (entry) entry->deinit();
    if (library) dlclose(library);
    if (!ok) return 1;
    std::puts("s3g Sample Player CLAP smoke: ok");
    return 0;
}
