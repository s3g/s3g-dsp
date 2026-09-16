#ifndef NOMINMAX
#define NOMINMAX 1
#endif
#include <windows.h>
#include <ole2.h>
#include "vstgui/lib/cdropsource.h"
#include "vstgui/lib/platform/win32/win32dragging.h"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <string>

// Exercise the exact pinned Windows adapter used by GenericTextEdit, without
// changing the user's clipboard. REAPER SendInput covers actual clipboard I/O.
int main()
{
    int failures = 0;
    const auto check = [&](bool ok, const char* message) {
        if (!ok) { std::cerr << message << '\n'; ++failures; }
    };
    const auto run = [&](const std::string& utf8, const std::wstring& expected, bool ansi) {
        auto package = VSTGUI::CDropSource::create(utf8.data(),
            static_cast<uint32_t>(utf8.size()), VSTGUI::IDataPackage::kText);
        auto object = VSTGUI::makeOwned<VSTGUI::Win32DataObject>(package);
        FORMATETC format {static_cast<CLIPFORMAT>(ansi ? CF_TEXT : CF_UNICODETEXT),
            nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        STGMEDIUM medium {};
        const auto result = object->GetData(&format, &medium);
        check(result == S_OK && medium.tymed == TYMED_HGLOBAL, "Windows text data supplied");
        if (result != S_OK) return;
        const auto bytes = GlobalSize(medium.hGlobal);
        const auto* data = GlobalLock(medium.hGlobal);
        check(data != nullptr, "Windows text memory is lockable");
        if (data) {
            if (ansi) {
                const auto* begin = static_cast<const char*>(data);
                const auto* end = std::find(begin, begin + bytes, '\0');
                check(end != begin + bytes && std::string(begin, end) == utf8,
                      "ASCII clipboard retains the complete text and terminator");
            } else {
                const auto* begin = static_cast<const wchar_t*>(data);
                const auto* limit = begin + bytes / sizeof(wchar_t);
                const auto* end = std::find(begin, limit, L'\0');
                check(end != limit && std::wstring(begin, end) == expected,
                      "Unicode clipboard retains the complete text and terminator");
                check(bytes >= (expected.size() + 1) * sizeof(wchar_t),
                      "Unicode allocation includes its terminator");
            }
            GlobalUnlock(medium.hGlobal);
        }
        ReleaseStgMedium(&medium);
    };
    run("len 1 24", L"len 1 24", false);
    run("len 1 24", L"len 1 24", true);
    run(u8"\u00f8-\u8def\u5f84-\U0001f39b", L"\u00f8-\u8def\u5f84-\U0001f39b", false);
    if (!failures) std::cout << "Windows clipboard text/Unicode adapter passed.\n";
    return failures ? 1 : 0;
}
