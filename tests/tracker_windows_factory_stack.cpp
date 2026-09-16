#include <windows.h>
#include <clap/clap.h>
#include <iostream>
#include <string>

namespace {
const void* extension(const clap_host_t*, const char*) { return nullptr; }
void noop(const clap_host_t*) {}

// A host can reach the factory through nested project-recall calls. Keep a
// modest host budget live while testing against a 1 MiB executable stack.
__declspec(noinline) bool createWithHostFrames(const clap_plugin_factory_t* factory)
{
    volatile unsigned char hostFrames[128 * 1024] {};
    clap_host_t host {CLAP_VERSION, nullptr, "Tracker factory stack regression",
        "s3g", "https://s3g.net", "1", extension, noop, noop, noop};
    auto* plugin = factory->create_plugin(factory, &host, "org.s3g.s3g-dsp.tracker");
    if (!plugin) return false;
    const bool initialized = plugin->init(plugin);
    plugin->destroy(plugin);
    return initialized && hostFrames[0] == 0 && hostFrames[sizeof(hostFrames)-1] == 0;
}
}

int wmain(int argc, wchar_t** argv)
{
    if (argc != 2) { std::cerr << "Usage: factory-stack-test plugin.clap\n"; return 2; }
    auto module = LoadLibraryW(argv[1]);
    if (!module) { std::cerr << "LoadLibrary failed: " << GetLastError() << '\n'; return 1; }
    auto* entry = reinterpret_cast<const clap_plugin_entry_t*>(GetProcAddress(module, "clap_entry"));
    const int size = WideCharToMultiByte(CP_UTF8, 0, argv[1], -1, nullptr, 0, nullptr, nullptr);
    std::string path(static_cast<std::size_t>(size), '\0');
    if (!entry || size <= 0 || !WideCharToMultiByte(CP_UTF8, 0, argv[1], -1,
        path.data(), size, nullptr, nullptr) || !entry->init(path.c_str())) {
        std::cerr << "CLAP entry initialization failed\n"; FreeLibrary(module); return 1;
    }
    auto* factory = static_cast<const clap_plugin_factory_t*>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    bool passed = factory && createWithHostFrames(factory);
    entry->deinit();
    FreeLibrary(module);
    std::cout << (passed ? "PASS" : "FAIL") << ": factory initialization with 1 MiB stack and 128 KiB host frames\n";
    return passed ? 0 : 1;
}
