#include <clap/clap.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <iostream>
#include <vector>

namespace {

constexpr uint32_t kFrames = 64u;
constexpr double kSampleRate = 48000.0;

const void* hostGetExtension(const clap_host_t*, const char*) { return nullptr; }
void hostRequest(const clap_host_t*) {}

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

struct AudioBlock {
    std::vector<std::vector<float>> input;
    std::vector<std::vector<float>> output;
    std::vector<float*> inputPointers;
    std::vector<float*> outputPointers;
    clap_audio_buffer_t inputBuffer {};
    clap_audio_buffer_t outputBuffer {};

    explicit AudioBlock(uint32_t channels)
        : input(channels, std::vector<float>(kFrames)),
          output(channels, std::vector<float>(kFrames, -123.0f)),
          inputPointers(channels), outputPointers(channels)
    {
        for (uint32_t channel = 0u; channel < channels; ++channel) {
            inputPointers[channel] = input[channel].data();
            outputPointers[channel] = output[channel].data();
            for (uint32_t frame = 0u; frame < kFrames; ++frame) {
                input[channel][frame] = static_cast<float>(channel + 1u) * 0.01f
                    + static_cast<float>(frame) * 0.00001f;
            }
        }
        inputBuffer.data32 = inputPointers.data();
        inputBuffer.channel_count = channels;
        outputBuffer.data32 = outputPointers.data();
        outputBuffer.channel_count = channels;
    }
};

bool processBlock(const clap_plugin_t* plugin, AudioBlock& audio)
{
    clap_process_t process {};
    process.frames_count = kFrames;
    process.audio_inputs = &audio.inputBuffer;
    process.audio_outputs = &audio.outputBuffer;
    process.audio_inputs_count = 1u;
    process.audio_outputs_count = 1u;
    return plugin->process(plugin, &process) != CLAP_PROCESS_ERROR;
}

bool finiteOutputs(const AudioBlock& audio, uint32_t channels)
{
    for (uint32_t channel = 0u; channel < channels; ++channel) {
        for (float value : audio.output[channel]) {
            if (!std::isfinite(value)) return false;
        }
    }
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 5) {
        std::cerr << "usage: s3g_spectral_spray_clap_routing_smoke "
            << "<bundle-or-binary> <plugin-id> <plugin-channels> <host-channels>\n";
        return 2;
    }
    const uint32_t pluginChannels = static_cast<uint32_t>(std::strtoul(argv[3], nullptr, 10));
    const uint32_t hostChannels = static_cast<uint32_t>(std::strtoul(argv[4], nullptr, 10));
    if (pluginChannels == 0u || hostChannels <= pluginChannels) {
        std::cerr << "host-channels must be greater than plugin-channels\n";
        return 2;
    }

    const auto binary = resolveBinary(argv[1]);
    if (binary.empty()) {
        std::cerr << "Could not resolve Spectral Spray binary\n";
        return 1;
    }
    void* library = dlopen(binary.c_str(), RTLD_LOCAL | RTLD_NOW);
    if (!library) {
        std::cerr << "Could not load Spectral Spray: " << dlerror() << "\n";
        return 1;
    }
    const auto* entry = static_cast<const clap_plugin_entry_t*>(
        dlsym(library, "clap_entry"));
    bool ok = entry && entry->init(binary.c_str());

    clap_host_t host {};
    host.clap_version = CLAP_VERSION_INIT;
    host.name = "Spectral Spray routing smoke";
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
    const bool initialized = ok && plugin && plugin->init(plugin);
    const bool activated = initialized
        && plugin->activate(plugin, kSampleRate, kFrames, kFrames);
    const bool started = activated && plugin->start_processing(plugin);
    ok = started;

    AudioBlock wide(hostChannels);
    if (ok) ok = processBlock(plugin, wide) && finiteOutputs(wide, pluginChannels);
    for (uint32_t channel = pluginChannels; ok && channel < hostChannels; ++channel) {
        for (uint32_t frame = 0u; frame < kFrames; ++frame) {
            if (wide.output[channel][frame] != wide.input[channel][frame]) {
                std::cerr << "Extra lane " << channel
                    << " was not preserved at frame " << frame << "\n";
                ok = false;
                break;
            }
        }
    }

    AudioBlock narrow(hostChannels);
    narrow.outputBuffer.channel_count = std::max(1u, pluginChannels / 2u);
    if (ok) {
        ok = processBlock(plugin, narrow)
            && finiteOutputs(narrow, narrow.outputBuffer.channel_count);
    }

    if (plugin) {
        if (started) plugin->stop_processing(plugin);
        if (activated) plugin->deactivate(plugin);
        plugin->destroy(plugin);
    }
    if (entry) entry->deinit();
    dlclose(library);

    if (!ok) return 1;
    std::cout << "Spectral Spray routing smoke passed: plugin="
        << pluginChannels << "ch host=" << hostChannels << "ch\n";
    return 0;
}
