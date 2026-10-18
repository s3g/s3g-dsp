#include <clap/clap.h>
#include <clap/ext/note-ports.h>
#include <clap/ext/params.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace {

constexpr uint32_t kChannels = 64u;
constexpr uint32_t kFrames = 256u;

const void* hostExtension(const clap_host_t*, const char*) { return nullptr; }
void hostRequest(const clap_host_t*) {}

struct Events {
    const clap_event_header_t* event = nullptr;
    clap_input_events_t input {
        this,
        [](const clap_input_events_t* list) -> uint32_t {
            return static_cast<const Events*>(list->ctx)->event ? 1u : 0u;
        },
        [](const clap_input_events_t* list, uint32_t index)
            -> const clap_event_header_t* {
            return index == 0u ? static_cast<const Events*>(list->ctx)->event
                               : nullptr;
        },
    };
};

struct Audio {
    std::array<std::array<float, kFrames>, kChannels> samples {};
    std::array<float*, kChannels> channels {};
    clap_audio_buffer_t output {};

    Audio()
    {
        for (uint32_t index = 0u; index < kChannels; ++index)
            channels[index] = samples[index].data();
        output.data32 = channels.data();
        output.channel_count = kChannels;
    }
};

double render(const clap_plugin_t* plugin, Audio& audio,
    const clap_event_header_t* event = nullptr)
{
    double energy = 0.0;
    Events events;
    events.event = event;
    for (uint32_t block = 0u; block < 32u; ++block) {
        for (auto& channel : audio.samples) channel.fill(0.0f);
        clap_process_t process {};
        process.frames_count = kFrames;
        process.audio_outputs = &audio.output;
        process.audio_outputs_count = 1u;
        process.in_events = block == 0u ? &events.input : nullptr;
        if (plugin->process(plugin, &process) == CLAP_PROCESS_ERROR)
            return -1.0;
        for (const auto& channel : audio.samples) {
            for (float sample : channel) {
                if (!std::isfinite(sample)) return -1.0;
                energy += static_cast<double>(sample) * sample;
            }
        }
    }
    return energy;
}

std::filesystem::path binaryPath(const std::filesystem::path& input)
{
    if (std::filesystem::is_regular_file(input)) return input;
#if defined(__APPLE__)
    const auto directory = input / "Contents" / "MacOS";
    if (std::filesystem::is_directory(directory)) {
        for (const auto& file : std::filesystem::directory_iterator(directory))
            if (file.is_regular_file()) return file.path();
    }
#endif
    return {};
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 3) {
        std::cerr << "usage: s3g_ambi_note_midi_dialect_smoke <CLAP bundle> <plugin ID>\n";
        return 2;
    }
    const auto binary = binaryPath(argv[1]);
    if (binary.empty()) return 1;
#if defined(_WIN32)
    HMODULE library = LoadLibraryW(binary.c_str());
    const auto* entry = library ? reinterpret_cast<const clap_plugin_entry_t*>(
        GetProcAddress(library, "clap_entry")) : nullptr;
#else
    void* library = dlopen(binary.c_str(), RTLD_LOCAL | RTLD_NOW);
    const auto* entry = library ? static_cast<const clap_plugin_entry_t*>(
        dlsym(library, "clap_entry")) : nullptr;
#endif
    if (!entry || !entry->init(nullptr)) {
        std::cerr << "could not load CLAP\n";
        return 1;
    }
    const auto* factory = static_cast<const clap_plugin_factory_t*>(
        entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    clap_host_t host {};
    host.clap_version = CLAP_VERSION_INIT;
    host.name = "s3g Ambi MIDI dialect smoke";
    host.vendor = "s3g";
    host.url = "https://github.com/s3g/s3g-dsp";
    host.version = "1";
    host.get_extension = hostExtension;
    host.request_restart = hostRequest;
    host.request_process = hostRequest;
    host.request_callback = hostRequest;
    const clap_plugin_t* plugin = factory
        ? factory->create_plugin(factory, &host, argv[2]) : nullptr;
    bool ok = plugin && plugin->init(plugin);
    bool activated = false;
    bool started = false;
    if (ok) {
        const auto* ports = static_cast<const clap_plugin_note_ports_t*>(
            plugin->get_extension(plugin, CLAP_EXT_NOTE_PORTS));
        const auto* params = static_cast<const clap_plugin_params_t*>(
            plugin->get_extension(plugin, CLAP_EXT_PARAMS));
        clap_note_port_info_t port {};
        ok = ports && params && ports->count(plugin, true) == 1u
            && ports->get(plugin, 0u, true, &port)
            && (port.supported_dialects & CLAP_NOTE_DIALECT_CLAP) != 0u
            && (port.supported_dialects & CLAP_NOTE_DIALECT_MIDI) != 0u;
        if (ok) activated = plugin->activate(plugin, 48000.0, 1u, kFrames);
        if (activated) started = plugin->start_processing(plugin);
        ok = ok && activated && started;
        if (ok) {
            clap_event_param_value_t mode {};
            mode.header = { sizeof(mode), 0u, CLAP_CORE_EVENT_SPACE_ID,
                CLAP_EVENT_PARAM_VALUE, 0u };
            mode.param_id = 3u;
            mode.note_id = -1;
            mode.port_index = -1;
            mode.channel = -1;
            mode.key = -1;
            mode.value = 1.0; // MIDI rather than the default FREE mode.
            Events events;
            events.event = &mode.header;
            params->flush(plugin, &events.input, nullptr);

            Audio audio;
            plugin->reset(plugin);
            const double silent = render(plugin, audio);

            clap_event_note_t clapNote {};
            clapNote.header = { sizeof(clapNote), 0u, CLAP_CORE_EVENT_SPACE_ID,
                CLAP_EVENT_NOTE_ON, 0u };
            clapNote.note_id = -1;
            clapNote.port_index = 0;
            clapNote.channel = 0;
            clapNote.key = 60;
            clapNote.velocity = 100.0 / 127.0;
            plugin->reset(plugin);
            const double clapEnergy = render(plugin, audio, &clapNote.header);

            clap_event_midi_t midi {};
            midi.header = { sizeof(midi), 0u, CLAP_CORE_EVENT_SPACE_ID,
                CLAP_EVENT_MIDI, 0u };
            midi.port_index = 0;
            midi.data[0] = 0x90u;
            midi.data[1] = 60u;
            midi.data[2] = 100u;
            plugin->reset(plugin);
            const double midiEnergy = render(plugin, audio, &midi.header);

            ok = silent >= 0.0 && silent < 1.0e-10
                && clapEnergy > 1.0e-8 && midiEnergy > 1.0e-8;
            std::cout << argv[2] << ": silent=" << silent
                << " CLAP-note=" << clapEnergy
                << " MIDI-bytes=" << midiEnergy << '\n';
        }
        if (started) plugin->stop_processing(plugin);
        if (activated) plugin->deactivate(plugin);
    }
    if (plugin) plugin->destroy(plugin);
    entry->deinit();
#if defined(_WIN32)
    FreeLibrary(library);
#else
    dlclose(library);
#endif
    if (!ok) std::cerr << "Ambi CLAP MIDI dialect regression\n";
    return ok ? 0 : 1;
}
