#if !defined(_WIN32)
#error "This smoke test is Windows-only."
#endif

#ifndef NOMINMAX
#define NOMINMAX 1
#endif
#include <windows.h>

#include <clap/clap.h>
#include <clap/ext/gui.h>

#include <cstdint>
#include <cstring>
#include <iostream>

namespace {

bool expect(bool condition, const char* message)
{
    if (!condition) std::cerr << message << '\n';
    return condition;
}

const void* hostGetExtension(const clap_host_t*, const char*) { return nullptr; }
void hostRequestRestart(const clap_host_t*) {}
void hostRequestProcess(const clap_host_t*) {}
void hostRequestCallback(const clap_host_t*) {}

LRESULT CALLBACK parentWindowProc(HWND window, UINT message,
                                  WPARAM wParam, LPARAM lParam)
{
    return DefWindowProcW(window, message, wParam, lParam);
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "usage: s3g_macro_delay_windows_clap_smoke <plugin.clap>\n";
        return 2;
    }

    HMODULE module = LoadLibraryA(argv[1]);
    if (!expect(module != nullptr, "could not load Macro Delay CLAP")) return 1;
    const auto* entry = reinterpret_cast<const clap_plugin_entry_t*>(
        GetProcAddress(module, "clap_entry"));
    bool ok = expect(entry != nullptr, "missing exported clap_entry");
    ok = ok && expect(clap_version_is_compatible(entry->clap_version),
        "incompatible CLAP entry version");
    const bool entryInitialized = ok && entry->init && entry->init(argv[1]);
    ok = ok && expect(entryInitialized,
        "CLAP entry initialization failed");

    clap_host_t host {
        CLAP_VERSION_INIT,
        nullptr,
        "s3g Win32 GUI smoke host",
        "s3g",
        "https://github.com/s3g/s3g-dsp",
        "0.1.0",
        hostGetExtension,
        hostRequestRestart,
        hostRequestProcess,
        hostRequestCallback,
    };

    const auto* factory = ok && entry->get_factory
        ? static_cast<const clap_plugin_factory_t*>(
            entry->get_factory(CLAP_PLUGIN_FACTORY_ID))
        : nullptr;
    ok = ok && expect(factory != nullptr, "missing CLAP plugin factory");
    const clap_plugin_t* plugin = factory
        ? factory->create_plugin(factory, &host,
            "org.s3g.s3g-dsp.macro-delay-8ch")
        : nullptr;
    ok = ok && expect(plugin && plugin->init && plugin->init(plugin),
        "could not create Macro Delay plugin");

    const auto* gui = plugin && plugin->get_extension
        ? static_cast<const clap_plugin_gui_t*>(
            plugin->get_extension(plugin, CLAP_EXT_GUI))
        : nullptr;
    ok = ok && expect(gui
            && gui->is_api_supported(plugin, CLAP_WINDOW_API_WIN32, false),
        "Win32 GUI API is unavailable");
    const bool guiCreated = ok && gui
        && gui->create(plugin, CLAP_WINDOW_API_WIN32, false);
    ok = ok && expect(guiCreated,
        "could not create VSTGUI editor");
    ok = ok && expect(!gui->set_scale(plugin, 1.5),
        "portable editor unexpectedly accepted host-side scaling");

    uint32_t width = 0u;
    uint32_t height = 0u;
    ok = ok && expect(gui->get_size(plugin, &width, &height)
            && width == 760u && height == 496u,
        "unexpected native editor size");
    clap_gui_resize_hints_t hints {};
    ok = ok && expect(gui->can_resize(plugin)
            && gui->get_resize_hints(plugin, &hints)
            && hints.can_resize_horizontally
            && hints.can_resize_vertically
            && hints.preserve_aspect_ratio
            && hints.aspect_ratio_width == 760u
            && hints.aspect_ratio_height == 496u,
        "portable editor resize contract failed");
    uint32_t minimumWidth = 1u;
    uint32_t minimumHeight = 1u;
    ok = ok && expect(gui->adjust_size(
            plugin, &minimumWidth, &minimumHeight)
            && minimumWidth == 494u && minimumHeight == 322u,
        "portable editor minimum size is incorrect");
    uint32_t maximumWidth = 4000u;
    uint32_t maximumHeight = 4000u;
    ok = ok && expect(gui->adjust_size(
            plugin, &maximumWidth, &maximumHeight)
            && maximumWidth == 1520u && maximumHeight == 992u,
        "portable editor maximum size is incorrect");

    const wchar_t* windowClass = L"S3GMacroDelayClapSmokeParent";
    WNDCLASSW windowDefinition {};
    windowDefinition.lpfnWndProc = parentWindowProc;
    windowDefinition.hInstance = GetModuleHandleW(nullptr);
    windowDefinition.lpszClassName = windowClass;
    const ATOM classAtom = RegisterClassW(&windowDefinition);
    ok = ok && expect(classAtom != 0, "could not register Win32 host window");
    HWND parent = classAtom
        ? CreateWindowExW(0, windowClass, L"s3g Macro Delay GUI smoke",
            WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
            static_cast<int>(width), static_cast<int>(height),
            nullptr, nullptr, windowDefinition.hInstance, nullptr)
        : nullptr;
    ok = ok && expect(parent != nullptr, "could not create Win32 host window");

    if (ok) {
        clap_window_t parentWindow {};
        parentWindow.api = CLAP_WINDOW_API_WIN32;
        parentWindow.win32 = parent;
        ok = expect(gui->set_parent(plugin, &parentWindow),
            "could not embed VSTGUI HWND")
            && expect(GetWindow(parent, GW_CHILD) != nullptr,
                "VSTGUI did not create a child HWND")
            && expect(gui->set_size(plugin, 950u, 620u),
                "Win32 editor resize failed")
            && expect(gui->show(plugin), "Win32 editor show failed");
        ShowWindow(parent, SW_SHOWNA);
        UpdateWindow(parent);
        MSG message {};
        while (PeekMessageW(&message, nullptr, 0u, 0u, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        ok = expect(gui->hide(plugin), "Win32 editor hide failed") && ok;
    }

    if (guiCreated) gui->destroy(plugin);
    if (plugin) plugin->destroy(plugin);
    if (parent) DestroyWindow(parent);
    if (classAtom) UnregisterClassW(windowClass, windowDefinition.hInstance);
    if (entryInitialized) entry->deinit();
    FreeLibrary(module);

    if (ok) std::cout << "Macro Delay Win32 CLAP GUI smoke passed\n";
    return ok ? 0 : 1;
}
