#include <clap/clap.h>
#include <clap/ext/params.h>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

const void* hostGetExtension(const clap_host_t*, const char*) { return nullptr; }
void hostRequest(const clap_host_t*) {}

struct EventList {
    std::vector<clap_event_param_value_t> storage;
    clap_input_events_t input {
        this,
        [](const clap_input_events_t* list) -> uint32_t {
            const auto* self = static_cast<const EventList*>(list->ctx);
            return self ? static_cast<uint32_t>(self->storage.size()) : 0u;
        },
        [](const clap_input_events_t* list, uint32_t index)
            -> const clap_event_header_t* {
            const auto* self = static_cast<const EventList*>(list->ctx);
            return self && index < self->storage.size()
                ? &self->storage[index].header : nullptr;
        },
    };

    explicit EventList(clap_id id, double value)
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
        storage.push_back(event);
    }
};

std::filesystem::path resolveBinary(const std::filesystem::path& supplied)
{
    if (std::filesystem::is_regular_file(supplied)) return supplied;
#if defined(__APPLE__)
    if (std::filesystem::is_directory(supplied)) {
        const auto directory = supplied / "Contents" / "MacOS";
        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            if (entry.is_regular_file()) return entry.path();
        }
    }
#endif
    return {};
}

bool findParamInfo(const clap_plugin_t* plugin,
                   const clap_plugin_params_t* params,
                   clap_id id,
                   clap_param_info_t& result)
{
    for (uint32_t index = 0u; index < params->count(plugin); ++index) {
        clap_param_info_t info {};
        if (params->get_info(plugin, index, &info) && info.id == id) {
            result = info;
            return true;
        }
    }
    return false;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 6) {
        std::cerr << "usage: s3g_preset_output_preservation_clap_smoke "
            << "<bundle-or-binary> <plugin-id> <output-id> <preset-id> "
            << "<preset-value>\n";
        return 2;
    }

    const auto binary = resolveBinary(argv[1]);
    if (binary.empty()) {
        std::cerr << "Could not resolve plugin binary\n";
        return 1;
    }
    const clap_id outputId = static_cast<clap_id>(std::stoul(argv[3]));
    const clap_id presetId = static_cast<clap_id>(std::stoul(argv[4]));
    const double presetValue = std::stod(argv[5]);

    void* library = dlopen(binary.c_str(), RTLD_LOCAL | RTLD_NOW);
    if (!library) {
        std::cerr << "Could not load plugin: " << dlerror() << "\n";
        return 1;
    }
    const auto* entry = static_cast<const clap_plugin_entry_t*>(
        dlsym(library, "clap_entry"));
    bool ok = entry && entry->init(binary.c_str());

    clap_host_t host {};
    host.clap_version = CLAP_VERSION_INIT;
    host.name = "Preset output preservation smoke";
    host.vendor = "s3g";
    host.url = "https://github.com/s3g/s3g-dsp";
    host.version = "1";
    host.get_extension = hostGetExtension;
    host.request_restart = hostRequest;
    host.request_process = hostRequest;
    host.request_callback = hostRequest;

    const auto* factory = ok ? static_cast<const clap_plugin_factory_t*>(
        entry->get_factory(CLAP_PLUGIN_FACTORY_ID)) : nullptr;
    const clap_plugin_t* plugin = factory
        ? factory->create_plugin(factory, &host, argv[2]) : nullptr;
    ok = ok && plugin && plugin->init(plugin);
    const auto* params = ok ? static_cast<const clap_plugin_params_t*>(
        plugin->get_extension(plugin, CLAP_EXT_PARAMS)) : nullptr;
    ok = ok && params && params->count && params->get_info
        && params->get_value && params->flush;

    clap_param_info_t outputInfo {};
    clap_param_info_t presetInfo {};
    if (ok) {
        ok = findParamInfo(plugin, params, outputId, outputInfo)
            && findParamInfo(plugin, params, presetId, presetInfo)
            && presetValue >= presetInfo.min_value
            && presetValue <= presetInfo.max_value;
    }

    const double preservedValue = ok
        ? outputInfo.min_value
            + (outputInfo.max_value - outputInfo.min_value) * 0.173
        : 0.0;
    if (ok) {
        EventList outputEvent(outputId, preservedValue);
        params->flush(plugin, &outputEvent.input, nullptr);
        double reported = 0.0;
        ok = params->get_value(plugin, outputId, &reported)
            && std::abs(reported - preservedValue) <= 1.0e-5;
    }
    if (ok) {
        EventList presetEvent(presetId, presetValue);
        params->flush(plugin, &presetEvent.input, nullptr);
        double reported = 0.0;
        ok = params->get_value(plugin, outputId, &reported)
            && std::abs(reported - preservedValue) <= 1.0e-5;
        if (!ok) {
            std::cerr << "Preset changed output parameter " << outputId
                << " from " << preservedValue << " to " << reported
                << " for " << argv[2] << "\n";
        }
    }

    if (plugin) plugin->destroy(plugin);
    if (entry && entry->deinit) entry->deinit();
    dlclose(library);
    if (!ok) {
        std::cerr << "Preset/output preservation smoke failed for "
            << argv[2] << "\n";
        return 1;
    }
    std::cout << "Preset/output preservation smoke passed for "
        << argv[2] << "\n";
    return 0;
}
