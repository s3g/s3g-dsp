#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
#define NOMINMAX
#include <windows.h>
#include <clap/clap.h>
#include "../plugins/clap_ambi_pyrosphere_encoder/s3g_pyrosphere_windows_float_mode.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>

namespace {
int failures = 0;
void check(bool good, const char* message)
{
    if (!good) { std::fprintf(stderr, "FAIL: %s\n", message); ++failures; }
}
const void* CLAP_ABI extension(const clap_host_t*, const char*) { return nullptr; }
void CLAP_ABI request(const clap_host_t*) {}
uint32_t CLAP_ABI size(const clap_input_events_t*) { return 0; }
const clap_event_header_t* CLAP_ABI get(const clap_input_events_t*, uint32_t) { return nullptr; }
bool CLAP_ABI push(const clap_output_events_t*, const clap_event_header_t*) { return true; }
}

int main(int argc, char** argv)
{
    if (argc != 2) return 2;
    const unsigned saved = _mm_getcsr();
    // Include both flush flags, all rounding modes, and pre-existing status.
    for (unsigned flush : {0u, 0x40u, 0x8000u, 0x8040u}) {
        for (unsigned rounding : {0u, 0x2000u, 0x4000u, 0x6000u}) {
            const unsigned original = 0x1f95u | flush | rounding;
            _mm_setcsr(original);
            {
                s3g::clap_detail::ScopedPyrosphereWindowsFloatMode guard;
                check(_mm_getcsr() == (original | 0x8040u), "only flush flags changed");
                {
                    s3g::clap_detail::ScopedPyrosphereWindowsFloatMode nested;
                    // Add a status bit to prove that both scopes restore it.
                    _mm_setcsr(_mm_getcsr() | 0x20u);
                }
                check(_mm_getcsr() == (original | 0x8040u), "nested restoration");
            }
            check(_mm_getcsr() == original, "complete host state restored");
        }
    }
    _mm_setcsr(saved);
    const auto binary = std::filesystem::u8path(argv[1]);
    HMODULE library = LoadLibraryW(binary.c_str());
    if (!library) return 3;
    const auto* entry = reinterpret_cast<const clap_plugin_entry_t*>(
        GetProcAddress(library, "clap_entry"));
    if (!entry || !entry->init(argv[1])) return 4;
    const auto* factory = static_cast<const clap_plugin_factory_t*>(
        entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    if (!factory || factory->get_plugin_count(factory) != 1) return 5;
    constexpr const char* id = "org.s3g.s3g-dsp.ambi-pyrosphere-encoder-64";
    check(std::strcmp(factory->get_plugin_descriptor(factory, 0)->id, id) == 0,
        "stable CLAP identity");
    clap_host_t host {CLAP_VERSION, nullptr, "s3g Windows test", "s3g", "", "1",
        extension, request, request, request};
    const auto* plugin = factory->create_plugin(factory, &host, id);
    if (!plugin || !plugin->init(plugin)
        || !plugin->activate(plugin, 48000, 512, 512)
        || !plugin->start_processing(plugin)) return 6;
    auto storage = std::make_unique<std::array<std::array<float, 512>, 64>>();
    std::array<float*, 64> pointers {};
    for (unsigned c = 0; c < 64; ++c) pointers[c] = (*storage)[c].data();
    clap_audio_buffer_t output {}; output.data32 = pointers.data(); output.channel_count = 64;
    clap_input_events_t input {nullptr, size, get};
    clap_output_events_t events {nullptr, push};
    clap_process_t process {}; process.frames_count = 512; process.audio_outputs = &output;
    process.audio_outputs_count = 1; process.in_events = &input; process.out_events = &events;
    unsigned denormalBlocks = 0, changedControls = 0, invalidBlocks = 0;
    double peak = 0;
    for (unsigned block = 0; block < 750; ++block) {
        const unsigned original = (block < 375) ? 0x1f80u : 0x9fc0u;
        _mm_setcsr(original);
        process.steady_time = static_cast<int64_t>(block) * 512;
        const auto status = plugin->process(plugin, &process);
        const auto returned = _mm_getcsr();
        _mm_setcsr(saved);
        denormalBlocks += (returned & 2u) != 0;
        changedControls += (returned & ~0x3fu) != original;
        invalidBlocks += status == CLAP_PROCESS_ERROR;
        for (const auto& channel : *storage) for (float sample : channel) {
            if (!std::isfinite(sample)) ++invalidBlocks;
            peak = std::max(peak, std::abs(static_cast<double>(sample)));
        }
    }
    check(denormalBlocks == 0, "DSP must not leak denormal-operand exceptions");
    check(changedControls == 0, "CLAP process preserves host control modes");
    check(invalidBlocks == 0 && peak > 0, "finite, non-silent audio");
    // The zero-output early return also must leave the host control state alone.
    process.audio_outputs_count = 0;
    _mm_setcsr(0x5f80u);
    plugin->process(plugin, &process);
    check((_mm_getcsr() & ~0x3fu) == 0x5f80u, "zero-output host control state");
    _mm_setcsr(saved);
    plugin->stop_processing(plugin); plugin->deactivate(plugin); plugin->destroy(plugin);
    entry->deinit(); FreeLibrary(library); _mm_setcsr(saved);
    std::printf("denormal_blocks=%u changed_controls=%u invalid_blocks=%u peak=%.9g failures=%d\n",
        denormalBlocks, changedControls, invalidBlocks, peak, failures);
    return failures == 0 ? 0 : 1;
}
#else
int main() { return 77; }
#endif
