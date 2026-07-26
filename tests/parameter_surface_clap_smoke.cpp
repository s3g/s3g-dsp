#include <clap/clap.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>

#include <algorithm>
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
        storage.push_back(event);
    }
};

struct MemoryOutput {
    std::vector<uint8_t> bytes;
};

int64_t streamWrite(
    const clap_ostream_t* stream, const void* source, uint64_t size)
{
    auto* output = static_cast<MemoryOutput*>(stream->ctx);
    if (!output || !source) return -1;
    const auto* bytes = static_cast<const uint8_t*>(source);
    output->bytes.insert(output->bytes.end(), bytes, bytes + size);
    return static_cast<int64_t>(size);
}

struct MemoryInput {
    const std::vector<uint8_t>* bytes = nullptr;
    size_t offset = 0u;
};

int64_t streamRead(
    const clap_istream_t* stream, void* destination, uint64_t requested)
{
    auto* input = static_cast<MemoryInput*>(stream->ctx);
    if (!input || !input->bytes || !destination
        || input->offset >= input->bytes->size()) return 0;
    const size_t count = std::min<size_t>(
        static_cast<size_t>(requested),
        input->bytes->size() - input->offset);
    std::memcpy(destination,
        input->bytes->data() + input->offset, count);
    input->offset += count;
    return static_cast<int64_t>(count);
}

std::filesystem::path resolveBinary(const std::filesystem::path& supplied)
{
    if (std::filesystem::is_regular_file(supplied)) return supplied;
#if defined(__APPLE__)
    if (std::filesystem::is_directory(supplied)) {
        const auto macOS = supplied / "Contents" / "MacOS";
        for (const auto& entry : std::filesystem::directory_iterator(macOS)) {
            if (entry.is_regular_file()) return entry.path();
        }
    }
#endif
    return {};
}

bool approximately(double left, double right)
{
    return std::fabs(left - right) <= 1.0e-6;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 5) {
        std::cerr << "usage: s3g_parameter_surface_clap_smoke "
            << "<bundle-or-binary> <plugin-id> <x-id> <y-id>\n";
        return 2;
    }
    const auto binary = resolveBinary(argv[1]);
    if (binary.empty()) {
        std::cerr << "Could not resolve plugin binary\n";
        return 1;
    }
    const clap_id xId = static_cast<clap_id>(std::stoul(argv[3]));
    const clap_id yId = static_cast<clap_id>(std::stoul(argv[4]));
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
    host.name = "Parameter Surface smoke";
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
    const auto* state = ok ? static_cast<const clap_plugin_state_t*>(
        plugin->get_extension(plugin, CLAP_EXT_STATE)) : nullptr;
    ok = ok && params && state;

    clap_param_info_t xInfo {};
    clap_param_info_t yInfo {};
    bool foundX = false;
    bool foundY = false;
    if (ok) {
        for (uint32_t index = 0u; index < params->count(plugin); ++index) {
            clap_param_info_t info {};
            if (!params->get_info(plugin, index, &info)) continue;
            if (info.id == xId) { xInfo = info; foundX = true; }
            if (info.id == yId) { yInfo = info; foundY = true; }
        }
        const auto valid = [](const clap_param_info_t& info, const char* name) {
            return std::strcmp(info.name, name) == 0
                && std::strcmp(info.module, "Parameter Surface") == 0
                && info.min_value == 0.0 && info.max_value == 1.0
                && info.default_value == 0.5
                && (info.flags & CLAP_PARAM_IS_AUTOMATABLE) != 0u
                && (info.flags & CLAP_PARAM_IS_STEPPED) == 0u;
        };
        ok = foundX && foundY
            && valid(xInfo, "Surface X") && valid(yInfo, "Surface Y");
    }

    const bool pyrosphere = std::strcmp(argv[2],
        "org.s3g.s3g-dsp.ambi-pyrosphere-encoder-64") == 0;
    const bool cryosphere = std::strcmp(argv[2],
        "org.s3g.s3g-dsp.ambi-cryosphere-encoder-64") == 0;
    if (ok && (pyrosphere || cryosphere)) {
        clap_param_info_t rateInfo {};
        bool foundRate = false;
        for (uint32_t index = 0u; index < params->count(plugin); ++index) {
            clap_param_info_t info {};
            if (!params->get_info(plugin, index, &info)) continue;
            if (info.id == 5u) {
                rateInfo = info;
                foundRate = true;
                break;
            }
        }
        char lowText[64] {};
        char middleText[64] {};
        if (pyrosphere) {
            ok = foundRate
                && std::strcmp(rateInfo.name, "Ignition Rate") == 0
                && approximately(rateInfo.min_value, 0.002)
                && approximately(rateInfo.max_value, 12.0)
                && params->value_to_text(plugin, 5u, 0.015,
                    lowText, sizeof(lowText))
                && params->value_to_text(plugin, 5u, 0.5,
                    middleText, sizeof(middleText))
                && std::strstr(lowText, "mHz")
                && std::strstr(middleText, "mHz")
                && std::strcmp(lowText, middleText) != 0;
        } else {
            ok = foundRate
                && std::strcmp(rateInfo.name, "Event Rate") == 0
                && approximately(rateInfo.min_value, 0.0)
                && approximately(rateInfo.max_value, 1.0)
                && params->value_to_text(plugin, 5u, 0.0,
                    lowText, sizeof(lowText))
                && params->value_to_text(plugin, 5u, 0.025,
                    middleText, sizeof(middleText))
                && std::strcmp(lowText, "HOLD") == 0
                && std::strstr(middleText, "0.025x");
        }
    }

    auto flushCursor = [&](double x, double y) {
        EventList events;
        events.add(xId, x);
        events.add(yId, y);
        params->flush(plugin, &events.input, nullptr);
    };
    auto readCursor = [&](double x, double y) {
        double actualX = -1.0;
        double actualY = -1.0;
        return params->get_value(plugin, xId, &actualX)
            && params->get_value(plugin, yId, &actualY)
            && approximately(actualX, x)
            && approximately(actualY, y);
    };

    MemoryOutput saved;
    if (ok) {
        flushCursor(0.2, 0.8);
        clap_ostream_t output { &saved, streamWrite };
        ok = readCursor(0.2, 0.8)
            && state->save(plugin, &output)
            && !saved.bytes.empty();
    }
    if (ok) {
        flushCursor(0.91, 0.09);
        MemoryInput memory { &saved.bytes, 0u };
        clap_istream_t input { &memory, streamRead };
        ok = readCursor(0.91, 0.09)
            && state->load(plugin, &input)
            && readCursor(0.2, 0.8);
    }

    if (plugin) plugin->destroy(plugin);
    if (entry) entry->deinit();
    dlclose(library);
    if (!ok) {
        std::cerr << "Parameter Surface CLAP contract failed for "
            << argv[2] << "\n";
        return 1;
    }
    std::cout << "Parameter Surface CLAP contract passed for "
        << argv[2] << "\n";
    return 0;
}
