#include <clap/clap.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
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
        std::error_code error;
        std::vector<std::filesystem::path> binaries;
        for (const auto& entry : std::filesystem::directory_iterator(macOS, error)) {
            if (entry.is_regular_file()) binaries.push_back(entry.path());
        }
        if (!error && binaries.size() == 1u) return binaries.front();
    }
#endif
    return {};
}

struct MemoryState {
    std::vector<uint8_t> bytes;
    size_t offset = 0u;
};

int64_t stateWrite(const clap_ostream_t* stream,
    const void* source, uint64_t requested)
{
    auto* state = static_cast<MemoryState*>(stream->ctx);
    if (!state || (!source && requested > 0u)
        || requested > 64u * 1024u * 1024u
        || state->bytes.size() > 64u * 1024u * 1024u - requested) {
        return -1;
    }
    if (requested == 0u) return 0;
    const auto* bytes = static_cast<const uint8_t*>(source);
    state->bytes.insert(state->bytes.end(), bytes, bytes + requested);
    return static_cast<int64_t>(requested);
}

int64_t stateRead(const clap_istream_t* stream,
    void* destination, uint64_t requested)
{
    auto* state = static_cast<MemoryState*>(stream->ctx);
    if (!state || (!destination && requested > 0u)) return -1;
    const size_t available = state->offset < state->bytes.size()
        ? state->bytes.size() - state->offset : 0u;
    const size_t count = std::min<size_t>(available, requested);
    if (count > 0u) {
        std::memcpy(destination, state->bytes.data() + state->offset, count);
        state->offset += count;
    }
    return static_cast<int64_t>(count);
}

struct LoadedPlugin {
    void* library = nullptr;
    const clap_plugin_entry_t* entry = nullptr;
    const clap_plugin_t* plugin = nullptr;

    ~LoadedPlugin()
    {
        if (plugin) plugin->destroy(plugin);
        if (entry) entry->deinit();
        if (library) dlclose(library);
    }
};

bool loadPlugin(const std::filesystem::path& supplied,
    const char* pluginId, clap_host_t& host, LoadedPlugin& loaded)
{
    const auto binary = resolveBinary(supplied);
    if (binary.empty()) {
        std::cerr << "Could not resolve CLAP binary from " << supplied << "\n";
        return false;
    }
    loaded.library = dlopen(binary.c_str(), RTLD_LOCAL | RTLD_NOW);
    if (!loaded.library) {
        std::cerr << "Could not load " << binary << ": " << dlerror() << "\n";
        return false;
    }
    loaded.entry = static_cast<const clap_plugin_entry_t*>(
        dlsym(loaded.library, "clap_entry"));
    const auto entryPath = std::filesystem::is_directory(supplied)
        ? supplied : binary;
    if (!loaded.entry || !loaded.entry->init(entryPath.c_str())) {
        std::cerr << "Could not initialize CLAP entry in " << binary << "\n";
        loaded.entry = nullptr;
        return false;
    }
    const auto* factory = static_cast<const clap_plugin_factory_t*>(
        loaded.entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    const clap_plugin_descriptor_t* descriptor = nullptr;
    if (factory) {
        const uint32_t count = factory->get_plugin_count(factory);
        for (uint32_t index = 0u; index < count; ++index) {
            const auto* candidate = factory->get_plugin_descriptor(factory, index);
            if (candidate && candidate->id
                && std::strcmp(candidate->id, pluginId) == 0) {
                descriptor = candidate;
                break;
            }
        }
    }
    if (!descriptor || !clap_version_is_compatible(descriptor->clap_version)) {
        std::cerr << "Missing or incompatible descriptor for " << pluginId << "\n";
        return false;
    }
    loaded.plugin = factory
        ? factory->create_plugin(factory, &host, pluginId) : nullptr;
    if (!loaded.plugin || !loaded.plugin->init(loaded.plugin)) {
        std::cerr << "Could not initialize plugin " << pluginId << "\n";
        return false;
    }
    if (!loaded.plugin->desc || !loaded.plugin->desc->id
        || std::strcmp(loaded.plugin->desc->id, pluginId) != 0
        || !clap_version_is_compatible(loaded.plugin->desc->clap_version)) {
        std::cerr << "Created plugin descriptor does not match " << pluginId << "\n";
        return false;
    }
    return true;
}

bool saveState(const clap_plugin_t* plugin, MemoryState& memory)
{
    const auto* state = static_cast<const clap_plugin_state_t*>(
        plugin->get_extension(plugin, CLAP_EXT_STATE));
    if (!state || !state->save) return false;
    memory.bytes.clear();
    memory.offset = 0u;
    clap_ostream_t stream { &memory, stateWrite };
    return state->save(plugin, &stream) && !memory.bytes.empty();
}

bool loadState(const clap_plugin_t* plugin, MemoryState& memory)
{
    const auto* state = static_cast<const clap_plugin_state_t*>(
        plugin->get_extension(plugin, CLAP_EXT_STATE));
    if (!state || !state->load) return false;
    memory.offset = 0u;
    clap_istream_t stream { &memory, stateRead };
    return state->load(plugin, &stream) && memory.offset == memory.bytes.size();
}

bool parametersAreSane(const clap_plugin_t* plugin)
{
    const auto* params = static_cast<const clap_plugin_params_t*>(
        plugin->get_extension(plugin, CLAP_EXT_PARAMS));
    if (!params) return true;
    const uint32_t count = params->count(plugin);
    for (uint32_t index = 0u; index < count; ++index) {
        clap_param_info_t info {};
        double value = 0.0;
        if (!params->get_info(plugin, index, &info)
            || !params->get_value(plugin, info.id, &value)
            || !std::isfinite(info.min_value)
            || !std::isfinite(info.max_value)
            || info.min_value > info.max_value
            || !std::isfinite(value)
            || value < info.min_value - 1.0e-6
            || value > info.max_value + 1.0e-6) {
            std::cerr << "Invalid restored parameter at index " << index
                      << ": " << value << "\n";
            return false;
        }
    }
    return true;
}

struct ParameterValue {
    clap_id id = CLAP_INVALID_ID;
    double value = 0.0;
};

bool parameterSnapshot(const clap_plugin_t* plugin,
    std::vector<ParameterValue>& snapshot)
{
    snapshot.clear();
    const auto* params = static_cast<const clap_plugin_params_t*>(
        plugin->get_extension(plugin, CLAP_EXT_PARAMS));
    if (!params) return true;
    const uint32_t count = params->count(plugin);
    snapshot.reserve(count);
    for (uint32_t index = 0u; index < count; ++index) {
        clap_param_info_t info {};
        double value = 0.0;
        if (!params->get_info(plugin, index, &info)
            || !params->get_value(plugin, info.id, &value)
            || !std::isfinite(value)) {
            return false;
        }
        snapshot.push_back({ info.id, value });
    }
    std::sort(snapshot.begin(), snapshot.end(), [](const auto& left, const auto& right) {
        return left.id < right.id;
    });
    return std::adjacent_find(snapshot.begin(), snapshot.end(),
        [](const auto& left, const auto& right) { return left.id == right.id; })
        == snapshot.end();
}

bool parameterSnapshotsMatch(const std::vector<ParameterValue>& expected,
    const std::vector<ParameterValue>& actual)
{
    if (expected.size() != actual.size()) {
        std::cerr << "Parameter count changed across migrated reload: "
                  << expected.size() << " -> " << actual.size() << "\n";
        return false;
    }
    for (size_t index = 0u; index < expected.size(); ++index) {
        if (expected[index].id != actual[index].id) {
            std::cerr << "Parameter ID changed across migrated reload at index "
                      << index << "\n";
            return false;
        }
        const double scale = std::max({ 1.0, std::fabs(expected[index].value),
            std::fabs(actual[index].value) });
        if (std::fabs(expected[index].value - actual[index].value)
            > 1.0e-12 * scale) {
            std::cerr << "Parameter " << expected[index].id
                      << " changed across migrated reload: "
                      << expected[index].value << " -> "
                      << actual[index].value << "\n";
            return false;
        }
    }
    return true;
}

bool parameterSnapshotContains(const std::vector<ParameterValue>& expected,
    const std::vector<ParameterValue>& actual)
{
    for (const auto& item : expected) {
        const auto found = std::lower_bound(actual.begin(), actual.end(), item.id,
            [](const auto& candidate, clap_id id) { return candidate.id < id; });
        if (found == actual.end() || found->id != item.id) {
            std::cerr << "Released parameter " << item.id
                      << " is missing from the current plugin\n";
            return false;
        }
        const double scale = std::max({ 1.0, std::fabs(item.value),
            std::fabs(found->value) });
        if (std::fabs(item.value - found->value) > 1.0e-6 * scale) {
            std::cerr << "Released parameter " << item.id << " changed: "
                      << item.value << " -> " << found->value << "\n";
            return false;
        }
    }
    return true;
}

struct ParamEvents {
    std::vector<clap_event_param_value_t> events;
    clap_input_events_t input {
        this,
        [](const clap_input_events_t* list) -> uint32_t {
            const auto* self = static_cast<const ParamEvents*>(list->ctx);
            return self ? static_cast<uint32_t>(self->events.size()) : 0u;
        },
        [](const clap_input_events_t* list, uint32_t index)
            -> const clap_event_header_t* {
            const auto* self = static_cast<const ParamEvents*>(list->ctx);
            return self && index < self->events.size()
                ? &self->events[index].header : nullptr;
        },
    };
};

bool seedFixtureParameters(const clap_plugin_t* plugin)
{
    const auto* params = static_cast<const clap_plugin_params_t*>(
        plugin->get_extension(plugin, CLAP_EXT_PARAMS));
    if (!params || !params->flush) return true;
    ParamEvents list;
    const uint32_t count = params->count(plugin);
    list.events.reserve(count);
    for (uint32_t index = 0u; index < count; ++index) {
        clap_param_info_t info {};
        double current = 0.0;
        if (!params->get_info(plugin, index, &info)
            || !params->get_value(plugin, info.id, &current)
            || !std::isfinite(info.min_value)
            || !std::isfinite(info.max_value)
            || info.min_value > info.max_value
            || !std::isfinite(current)) {
            return false;
        }
        if ((info.flags & CLAP_PARAM_IS_READONLY) != 0u
            || info.min_value == info.max_value
            || current < info.min_value
            || current > info.max_value) {
            continue;
        }
        const uint32_t mixed = info.id * 2654435761u + index * 2246822519u;
        const double fraction = 0.2
            + 0.6 * static_cast<double>(mixed % 10007u) / 10006.0;
        double value = info.min_value
            + fraction * (info.max_value - info.min_value);
        if ((info.flags & CLAP_PARAM_IS_STEPPED) != 0u) value = std::round(value);
        value = std::clamp(value, info.min_value, info.max_value);
        const double tolerance = 1.0e-12 * std::max(1.0, std::fabs(current));
        if (std::fabs(value - current) <= tolerance) {
            value = std::fabs(current - info.min_value)
                    > std::fabs(current - info.max_value)
                ? info.min_value : info.max_value;
        }
        clap_event_param_value_t event {};
        event.header.size = sizeof(event);
        event.header.time = 0u;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_PARAM_VALUE;
        event.header.flags = CLAP_EVENT_IS_LIVE;
        event.param_id = info.id;
        event.note_id = -1;
        event.port_index = -1;
        event.channel = -1;
        event.key = -1;
        event.value = value;
        list.events.push_back(event);
    }
    params->flush(plugin, &list.input, nullptr);
    return true;
}

bool writeParameterFile(const std::filesystem::path& path,
    const std::vector<ParameterValue>& snapshot)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::trunc);
    if (!stream) return false;
    stream << "# CLAP parameter ID\tvalue\n";
    stream << std::setprecision(std::numeric_limits<double>::max_digits10);
    for (const auto& item : snapshot) stream << item.id << '\t' << item.value << '\n';
    return stream.good();
}

bool readParameterFile(const std::filesystem::path& path,
    std::vector<ParameterValue>& snapshot)
{
    snapshot.clear();
    std::ifstream stream(path);
    if (!stream) return false;
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty() || line.front() == '#') continue;
        const auto separator = line.find('\t');
        if (separator == std::string::npos) return false;
        try {
            size_t idEnd = 0u;
            size_t valueEnd = 0u;
            const unsigned long id = std::stoul(line.substr(0u, separator), &idEnd);
            const std::string valueText = line.substr(separator + 1u);
            const double value = std::stod(valueText, &valueEnd);
            if (idEnd != separator || valueEnd != valueText.size()
                || id > std::numeric_limits<clap_id>::max()
                || !std::isfinite(value)) {
                return false;
            }
            snapshot.push_back({ static_cast<clap_id>(id), value });
        } catch (const std::exception&) {
            return false;
        }
    }
    std::sort(snapshot.begin(), snapshot.end(), [](const auto& left, const auto& right) {
        return left.id < right.id;
    });
    return !snapshot.empty()
        && std::adjacent_find(snapshot.begin(), snapshot.end(),
            [](const auto& left, const auto& right) { return left.id == right.id; })
            == snapshot.end();
}

bool readFile(const std::filesystem::path& path, MemoryState& memory)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return false;
    memory.bytes.assign(std::istreambuf_iterator<char>(stream), {});
    return stream.good() || stream.eof();
}

bool writeFile(const std::filesystem::path& path, const MemoryState& memory)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(memory.bytes.data()),
        static_cast<std::streamsize>(memory.bytes.size()));
    return stream.good();
}

} // namespace

int main(int argc, char** argv)
{
    const bool save = argc == 5 && std::strcmp(argv[1], "--save") == 0;
    const bool seed = argc == 6 && std::strcmp(argv[1], "--seed") == 0;
    const bool verify = argc == 6 && std::strcmp(argv[1], "--verify") == 0;
    if (!save && !seed && !verify) {
        std::cerr << "usage: s3g_clap_state_fixture_smoke "
                     "--save <bundle> <plugin-id> <state-file>\n"
                     "       s3g_clap_state_fixture_smoke "
                     "--seed|--verify <bundle> <plugin-id> <state-file> "
                     "<parameter-file>\n";
        return 2;
    }
    clap_host_t host {};
    host.clap_version = CLAP_VERSION_INIT;
    host.name = "s3g state fixture smoke";
    host.vendor = "s3g";
    host.url = "https://github.com/s3g/s3g-dsp";
    host.version = "1";
    host.get_extension = hostGetExtension;
    host.request_restart = hostRequest;
    host.request_process = hostRequest;
    host.request_callback = hostRequest;

    LoadedPlugin loaded;
    if (!loadPlugin(argv[2], argv[3], host, loaded)) return 1;
    const std::filesystem::path fixture = argv[4];
    if (save) {
        MemoryState state;
        if (!saveState(loaded.plugin, state) || !writeFile(fixture, state)) {
            std::cerr << "Could not save state fixture " << fixture << "\n";
            return 1;
        }
        std::cout << "Saved " << state.bytes.size() << " bytes to "
                  << fixture << "\n";
        return 0;
    }

    if (seed) {
        MemoryState state;
        std::vector<ParameterValue> parameters;
        if (!seedFixtureParameters(loaded.plugin)
            || !parameterSnapshot(loaded.plugin, parameters)
            || !saveState(loaded.plugin, state)
            || !writeFile(fixture, state)
            || !writeParameterFile(argv[5], parameters)) {
            std::cerr << "Could not seed released state fixture " << fixture << "\n";
            return 1;
        }
        std::cout << "Seeded " << state.bytes.size() << " bytes and "
                  << parameters.size() << " parameter values for "
                  << fixture.filename() << "\n";
        return 0;
    }

    MemoryState released;
    MemoryState migrated;
    MemoryState migratedAgain;
    std::vector<ParameterValue> releasedParameters;
    std::vector<ParameterValue> migratedParameters;
    std::vector<ParameterValue> expectedParameters;
    if (!readParameterFile(argv[5], expectedParameters)
        || !readFile(fixture, released) || released.bytes.empty()
        || !loadState(loaded.plugin, released)
        || !parametersAreSane(loaded.plugin)
        || !parameterSnapshot(loaded.plugin, releasedParameters)
        || !parameterSnapshotContains(expectedParameters, releasedParameters)
        || !saveState(loaded.plugin, migrated)
        || !loadState(loaded.plugin, migrated)
        || !parametersAreSane(loaded.plugin)
        || !parameterSnapshot(loaded.plugin, migratedParameters)
        || !parameterSnapshotsMatch(releasedParameters, migratedParameters)
        || !saveState(loaded.plugin, migratedAgain)
        || migratedAgain.bytes != migrated.bytes) {
        std::cerr << "Released state fixture did not migrate cleanly: "
                  << fixture << "\n";
        return 1;
    }
    std::cout << "Migrated state fixture " << fixture.filename()
              << " (" << released.bytes.size() << " -> "
              << migrated.bytes.size() << " bytes)\n";
    return 0;
}
