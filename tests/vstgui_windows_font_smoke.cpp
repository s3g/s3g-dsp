#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX 1
#endif
#include <objbase.h>
#endif
#include "s3g_vstgui_foundation.h"
#include "vstgui/lib/platform/platformfactory.h"
#include "vstgui/lib/platform/iplatformfont.h"
#include <algorithm>
#include <cmath>
#include <iostream>

namespace foundation = s3g::portable_gui::foundation;
int main(int argc, char** argv)
{
#if defined(_WIN32)
  // WIC bitmap creation requires an initialized COM apartment. REAPER supplies
  // one for its UI thread; this independent test must supply its own.
  const auto comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  if (FAILED(comResult)) return 2;
  struct ComScope { ~ComScope() { CoUninitialize(); } } comScope;
#endif
    const bool withoutResources = argc == 2 && std::string(argv[1]) == "--without-resources";
    if (!foundation::acquireRuntime()) return 2;
    int failures = 0;
    const auto check = [&](bool ok, const char* message) {
        if (!ok) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
    };
    std::vector<std::string> families;
    VSTGUI::getPlatformFactory().getAllFontFamilies([&](const std::string& family) {
        families.push_back(family);
        return true;
    });
    const auto has = [&](const char* name) {
        return std::find(families.begin(), families.end(), name) != families.end();
    };
    const auto resource = foundation::resourceDirectory() / "Fonts" / "FiraCode-Regular.ttf";
    check(std::filesystem::is_regular_file(resource) != withoutResources,
          "test resource layout matches the requested mode");
    if (!withoutResources) {
        check(has("Fira Code") && has("IBM Plex Mono"), "both private font families registered");
        check(foundation::usingBundledFont(), "bundled Fira survives enumeration of other families");
        check(foundation::selectedFontFamily() == "Fira Code", "bundled Fira selected");
    } else {
        // A system-installed Fira is also usable; otherwise verify the first
        // available fallback, not merely successful construction of any font.
        std::string expected = has("Fira Code") ? "Fira Code" : "";
        if (expected.empty()) {
            for (const char* family : {"Consolas", "Cascadia Mono", "Courier New"})
                if (has(family)) { expected = family; break; }
        }
        check(foundation::selectedFontFamily() == expected, "first available fallback selected");
        check(foundation::usingBundledFont() == has("Fira Code"), "font source reflects availability");
    }
    std::cout << "selected=" << foundation::selectedFontFamily()
              << "; bundled=" << foundation::usingBundledFont()
              << "; resource_mode=" << (withoutResources ? "absent" : "present") << '\n';
    {
        auto font = foundation::makeUiFont(11.0);
        auto platform = font->getPlatformFont();
        check(platform && std::isfinite(platform->getAscent()) && platform->getAscent() > 0,
              "selected font produces usable native metrics");
    }
    const auto selected = foundation::selectedFontFamily();
    check(foundation::acquireRuntime(), "shared runtime acquired twice");
    foundation::releaseRuntime();
    check(foundation::selectedFontFamily() == selected, "font survives another editor closing");
    foundation::releaseRuntime();
    check(foundation::selectedFontFamily().empty(), "last editor releases font selection");
    return failures ? 1 : 0;
}
