#include "s3g_vstgui_foundation.h"

#include "vstgui/lib/cfileselector.h"
#include "vstgui/lib/cgraphicstransform.h"
#include "vstgui/lib/platform/platformfactory.h"
#include "vstgui/lib/vstguiinit.h"

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <dlfcn.h>
#include "vstgui/lib/platform/platform_macos.h"
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX 1
#endif
#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include "vstgui/lib/platform/win32/win32factory.h"
#include "vstgui/lib/platform/iplatformframe.h"
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cwchar>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <locale>
#include <sstream>
#include <system_error>
#include <utility>

namespace s3g::portable_gui::foundation {
namespace {

using namespace VSTGUI;

constexpr const char* kBundledFontFamily = "Fira Code";
constexpr const char* kBundledFontFilename = "FiraCode-Regular.ttf";

std::atomic<uint32_t> gRuntimeUsers { 0u };
bool gUsingBundledFont = false;
std::string gSelectedFontFamily;
std::filesystem::path gResourceDirectory;

#if defined(__APPLE__)
CFBundleRef gPluginBundle = nullptr;

CFBundleRef findPluginBundle()
{
    Dl_info info {};
    if (dladdr(reinterpret_cast<const void*>(&findPluginBundle), &info) == 0
        || !info.dli_fname) return CFBundleGetMainBundle();
    std::filesystem::path bundlePath(info.dli_fname);
    for (int level = 0; level < 3 && bundlePath.has_parent_path(); ++level)
        bundlePath = bundlePath.parent_path();
    const std::string path = bundlePath.string();
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(nullptr,
        reinterpret_cast<const UInt8*>(path.data()),
        static_cast<CFIndex>(path.size()), true);
    if (!url) return CFBundleGetMainBundle();
    CFBundleRef bundle = CFBundleCreate(nullptr, url);
    CFRelease(url);
    return bundle ? bundle : CFBundleGetMainBundle();
}
#endif

bool fontIsAvailable(const char* requested)
{
    if (!requested || !requested[0]) return false;
    bool available = false;
    VSTGUI::getPlatformFactory().getAllFontFamilies(
        [&](const std::string& family) {
#if defined(_WIN32)
            // Some platform enumerators continue after the callback returns
            // false. Keep a match when later families are reported.
            available = available || family == requested;
#else
            available = family == requested;
#endif
            return !available;
        });
    return available;
}

void chooseFont()
{
    gUsingBundledFont = fontIsAvailable(kBundledFontFamily);
    if (gUsingBundledFont) {
        gSelectedFontFamily = kBundledFontFamily;
        return;
    }
#if defined(__APPLE__)
    constexpr std::array<const char*, 3u> fallbacks {
        "Menlo", "SF Mono", "Monaco"
    };
#elif defined(_WIN32)
    constexpr std::array<const char*, 3u> fallbacks {
        "Consolas", "Cascadia Mono", "Courier New"
    };
#else
    constexpr std::array<const char*, 2u> fallbacks {
        "DejaVu Sans Mono", "monospace"
    };
#endif
    for (const char* fallback : fallbacks) {
        if (fontIsAvailable(fallback)) {
            gSelectedFontFamily = fallback;
            break;
        }
    }
    std::fprintf(stderr,
        "s3g VSTGUI: bundled %s unavailable; using %s\n",
        kBundledFontFilename,
        gSelectedFontFamily.empty() ? "the system font"
                                    : gSelectedFontFamily.c_str());
}

#if defined(_WIN32)
std::wstring utf8ToWide(const char* text)
{
    if (!text || !text[0]) return {};
    const int count = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, nullptr, 0);
    if (count <= 0) return {};
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1,
            result.data(), count) <= 0) return {};
    if (!result.empty() && result.back() == L'\0') result.pop_back();
    return result;
}

std::wstring modeToWide(const char* mode)
{
    std::wstring result;
    if (mode) {
        while (*mode) result.push_back(static_cast<wchar_t>(*mode++));
    }
    return result;
}

std::string wideToUtf8(const wchar_t* text)
{
    if (!text || !text[0]) return {};
    const int length = static_cast<int>(std::wcslen(text));
    const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
        text, length, nullptr, 0, nullptr, nullptr);
    if (count <= 0) return {};
    std::string result(static_cast<std::size_t>(count), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, length,
            result.data(), count, nullptr, nullptr) <= 0) return {};
    return result;
}

std::wstring normalizedExtension(const std::string& extension)
{
    const char* begin = extension.c_str();
    if (begin[0] == '*') ++begin;
    if (begin[0] == '.') ++begin;
    return utf8ToWide(begin);
}

std::vector<std::string> runWindowsFileDialog(CFrame* parent,
    const FileDialogOptions& options, bool multiple = false)
{
    IFileDialog* dialog = nullptr;
    HRESULT result = E_FAIL;
    if (options.save) {
        IFileSaveDialog* saveDialog = nullptr;
        result = CoCreateInstance(CLSID_FileSaveDialog, nullptr,
            CLSCTX_INPROC_SERVER, IID_IFileSaveDialog,
            reinterpret_cast<void**>(&saveDialog));
        dialog = saveDialog;
    } else {
        IFileOpenDialog* openDialog = nullptr;
        result = CoCreateInstance(CLSID_FileOpenDialog, nullptr,
            CLSCTX_INPROC_SERVER, IID_IFileOpenDialog,
            reinterpret_cast<void**>(&openDialog));
        dialog = openDialog;
    }
    if (FAILED(result) || !dialog) {
        if (dialog) dialog->Release();
        return {};
    }

    struct DialogGuard {
        IFileDialog* dialog = nullptr;
        ~DialogGuard() { if (dialog) dialog->Release(); }
    } dialogGuard { dialog };

    DWORD flags = 0u;
    if (SUCCEEDED(dialog->GetOptions(&flags))) {
        flags |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST;
        flags |= options.save ? FOS_OVERWRITEPROMPT : FOS_FILEMUSTEXIST;
        if (options.directory && !options.save) flags |= FOS_PICKFOLDERS;
        if (multiple && !options.save) flags |= FOS_ALLOWMULTISELECT;
        dialog->SetOptions(flags);
    }

    const std::wstring title = utf8ToWide(options.title.c_str());
    if (!title.empty()) dialog->SetTitle(title.c_str());

    const std::wstring extension = normalizedExtension(options.extension);
    std::wstring description;
    std::wstring pattern;
    std::array<COMDLG_FILTERSPEC, 2u> filters {};
    UINT filterCount = 0u;
    if (!extension.empty()) {
        description = utf8ToWide(options.extensionDescription.c_str());
        if (description.empty()) description = extension;
        description += L" (*." + extension + L")";
        pattern = L"*." + extension;
        for (const auto& suffix : options.extensions) {
            const auto extra = normalizedExtension(suffix);
            if (!extra.empty() && extra != extension) pattern += L";*." + extra;
        }
        filters[filterCount++] = { description.c_str(), pattern.c_str() };
    }
    static constexpr wchar_t kAllFilesName[] = L"All files (*.*)";
    static constexpr wchar_t kAllFilesPattern[] = L"*.*";
    filters[filterCount++] = { kAllFilesName, kAllFilesPattern };
    if (!options.directory && SUCCEEDED(dialog->SetFileTypes(filterCount, filters.data())))
        dialog->SetFileTypeIndex(1u);

    if (options.save) {
        if (!extension.empty()) dialog->SetDefaultExtension(extension.c_str());
        const std::wstring defaultName = utf8ToWide(
            options.defaultSaveName.c_str());
        if (!defaultName.empty()) dialog->SetFileName(defaultName.c_str());
    }

    if (!options.initialDirectory.empty()) {
        const std::wstring initialDirectory = options.initialDirectory.native();
        IShellItem* folder = nullptr;
        if (!initialDirectory.empty()
            && SUCCEEDED(SHCreateItemFromParsingName(initialDirectory.c_str(),
                nullptr, IID_IShellItem,
                reinterpret_cast<void**>(&folder))) && folder) {
            dialog->SetDefaultFolder(folder);
            dialog->SetFolder(folder);
            folder->Release();
        }
    }

    HWND owner = nullptr;
    if (parent) {
        if (auto* platformFrame = parent->getPlatformFrame())
            owner = static_cast<HWND>(
                platformFrame->getPlatformRepresentation());
    }
    result = dialog->Show(owner);
    if (FAILED(result)) return {};

    if (multiple && !options.save) {
        IFileOpenDialog* open = nullptr;
        if (FAILED(dialog->QueryInterface(IID_IFileOpenDialog,
            reinterpret_cast<void**>(&open))) || !open) return {};
        IShellItemArray* items = nullptr;
        const HRESULT got = open->GetResults(&items);
        open->Release();
        if (FAILED(got) || !items) return {};
        DWORD count = 0;
        items->GetCount(&count);
        std::vector<std::string> paths;
        for (DWORD i = 0; i < count; ++i) {
            IShellItem* item = nullptr;
            if (FAILED(items->GetItemAt(i, &item)) || !item) continue;
            wchar_t* path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
                auto utf8 = wideToUtf8(path);
                if (!utf8.empty()) paths.push_back(std::move(utf8));
            }
            CoTaskMemFree(path);
            item->Release();
        }
        items->Release();
        return paths;
    }

    IShellItem* item = nullptr;
    if (FAILED(dialog->GetResult(&item)) || !item) return {};
    struct ItemGuard {
        IShellItem* item = nullptr;
        ~ItemGuard() { if (item) item->Release(); }
    } itemGuard { item };

    wchar_t* selectedPath = nullptr;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &selectedPath))
        || !selectedPath) return {};
    const std::string selected = wideToUtf8(selectedPath);
    CoTaskMemFree(selectedPath);
    return selected.empty() ? std::vector<std::string>{}
                            : std::vector<std::string>{selected};
}
#endif

} // namespace

const Palette& palette()
{
    static const Palette value {};
    return value;
}

const FontMetrics& fontMetrics()
{
#if defined(_WIN32)
    static const FontMetrics value { 11.0, 11.5, 9.0, 8.0 };
#else
    static const FontMetrics value {};
#endif
    return value;
}

bool acquireRuntime()
{
    if (gRuntimeUsers.fetch_add(1u, std::memory_order_acq_rel) != 0u)
        return true;
#if defined(__APPLE__)
    gPluginBundle = findPluginBundle();
    VSTGUI::init(gPluginBundle);
    if (CFURLRef resourceUrl = CFBundleCopyResourcesDirectoryURL(
            gPluginBundle)) {
        std::array<UInt8, 4096u> path {};
        if (CFURLGetFileSystemRepresentation(resourceUrl, true,
                path.data(), static_cast<CFIndex>(path.size()))) {
            gResourceDirectory = std::filesystem::u8path(
                reinterpret_cast<const char*>(path.data()));
        }
        CFRelease(resourceUrl);
    }
#elif defined(_WIN32)
    HMODULE module = nullptr;
    const BOOL found = GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
            | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&gRuntimeUsers), &module);
    if (!found || !module) {
        gRuntimeUsers.store(0u, std::memory_order_release);
        return false;
    }
    VSTGUI::init(module);
    std::array<wchar_t, 32768u> modulePath {};
    const DWORD length = GetModuleFileNameW(module, modulePath.data(),
        static_cast<DWORD>(modulePath.size()));
    if (length > 0u && length < modulePath.size()) {
        gResourceDirectory = std::filesystem::path(modulePath.data())
            .parent_path() / L"Resources";
        if (std::filesystem::is_directory(gResourceDirectory)) {
            auto utf8 = pathToUtf8(gResourceDirectory);
            if (!utf8.empty() && utf8.back() != '/' && utf8.back() != '\\')
                utf8.push_back('\\');
            if (const auto* factory =
                    VSTGUI::getPlatformFactory().asWin32Factory()) {
                factory->setResourceBasePath(utf8.c_str());
            }
        }
    }
#else
    gRuntimeUsers.store(0u, std::memory_order_release);
    return false;
#endif
    chooseFont();
    return true;
}

void releaseRuntime()
{
    const uint32_t users = gRuntimeUsers.load(std::memory_order_acquire);
    if (users == 0u) return;
    if (gRuntimeUsers.fetch_sub(1u, std::memory_order_acq_rel) != 1u) return;
    VSTGUI::exit();
#if defined(__APPLE__)
    if (gPluginBundle && gPluginBundle != CFBundleGetMainBundle())
        CFRelease(gPluginBundle);
    gPluginBundle = nullptr;
#endif
    gUsingBundledFont = false;
    gSelectedFontFamily.clear();
    gResourceDirectory.clear();
}

bool usingBundledFont() { return gUsingBundledFont; }
const std::string& selectedFontFamily() { return gSelectedFontFamily; }
const std::filesystem::path& resourceDirectory()
{
    return gResourceDirectory;
}

SharedPointer<CFontDesc> makeUiFont(double size)
{
    if (!gSelectedFontFamily.empty())
        return makeOwned<CFontDesc>(gSelectedFontFamily.c_str(), size);
    if (kSystemFont) {
        auto font = makeOwned<CFontDesc>(*kSystemFont);
        font->setSize(size);
        return font;
    }
    return makeOwned<CFontDesc>("", size);
}

CRect rect(double x, double y, double width, double height)
{
    return CRect(x, y, x + width, y + height);
}

bool contains(const CRect& bounds, const CPoint& point)
{
    return point.x >= bounds.left && point.x <= bounds.right
        && point.y >= bounds.top && point.y <= bounds.bottom;
}

void drawTextLine(CDrawContext& context, const std::string& text,
    double x, double y, double width, CColor textColor,
    CFontRef font, CHoriTxtAlign alignment)
{
    context.setFont(font ? font : kSystemFont);
    context.setFontColor(textColor);
    context.drawString(text.c_str(), rect(x, y - 2.0, width, 15.0),
        alignment, true);
}

void drawTextInRect(CDrawContext& context, const std::string& text,
    const CRect& bounds, CColor textColor, CFontRef font,
    CHoriTxtAlign alignment)
{
    context.saveGlobalState();
    CRect clip;
    context.getClipRect(clip);
    clip.bound(bounds);
    context.setClipRect(clip);
    context.setFont(font ? font : kSystemFont);
    context.setFontColor(textColor);
    context.drawString(text.c_str(), bounds, alignment, true);
    context.restoreGlobalState();
}

std::string pluginTitleText(std::string name)
{
    // Locale-independent; leave UTF-8 bytes, punctuation and channel counts intact.
    for (char& c : name)
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    if (name.compare(0, 3, "S3G") == 0
        && (name.size() == 3 || name[3] == ' '))
        name.replace(0, 3, "s3g");
    return name;
}

void drawPluginTitle(CDrawContext& context, const std::string& name,
    const CRect& bounds, CFontRef titleFont)
{
    drawTextInRect(context, pluginTitleText(name), bounds, palette().text,
        titleFont, kLeftText);
}

std::string sliderValueTextToFit(CDrawContext& context,
    const std::string& value, double maximumWidth, CFontRef font)
{
    context.setFont(font ? font : kSystemFont);
    const auto fits = [&](const std::string& text) {
        return context.getStringWidth(text.c_str()) <= maximumWidth;
    };
    if (maximumWidth <= 0.0 || fits(value)) return value;
    std::istringstream input(value);
    input.imbue(std::locale::classic());
    double number = 0.0;
    if (!(input >> number) || !std::isfinite(number)) return value;
    std::string suffix;
    std::getline(input >> std::ws, suffix);
    const auto last = suffix.find_last_not_of(" \t\r\n");
    suffix = last == std::string::npos ? "" : suffix.substr(0, last + 1);
    for (int precision = 2; precision >= 0; --precision) {
        std::ostringstream output;
        output.imbue(std::locale::classic());
        output << std::fixed << std::setprecision(precision) << number;
        std::string numeric = output.str();
        if (numeric.find('.') != std::string::npos) {
            while (numeric.back() == '0') numeric.pop_back();
            if (numeric.back() == '.') numeric.pop_back();
        }
        const std::string candidate = numeric + (suffix.empty() ? "" : " " + suffix);
        if (fits(candidate)) return candidate;
    }
    std::ostringstream compact;
    compact.imbue(std::locale::classic());
    compact << std::setprecision(2) << number << suffix;
    return fits(compact.str()) ? compact.str() : value;
}

void drawPanel(CDrawContext& context, const CRect& bounds,
    const std::string& title, CFontRef font, double headerHeight,
    double labelInset, double labelTop)
{
    const auto& style = palette();
    context.setFillColor(style.cell);
    context.drawRect(bounds, kDrawFilled);
    context.setFillColor(style.strip);
    context.drawRect(rect(bounds.left, bounds.top, bounds.getWidth(),
        headerHeight), kDrawFilled);
    context.setFillColor(style.accent);
    context.drawRect(rect(bounds.left, bounds.top,
        bounds.getWidth(), 2.0), kDrawFilled);
    drawTextLine(context, title, bounds.left + labelInset,
        bounds.top + labelTop, bounds.getWidth() - labelInset * 2.0,
        style.label, font);
}

void drawButton(CDrawContext& context, const CRect& bounds,
    const std::string& label, CFontRef font, bool active,
    CColor inactiveColor)
{
    const auto& style = palette();
    context.setFillColor(active ? style.buttonActive : inactiveColor);
    context.drawRect(bounds, kDrawFilled);
    drawTextInRect(context, label, bounds, style.label, font, kCenterText);
}

void drawMenuBox(CDrawContext& context, const CRect& bounds,
    const std::string& value, CFontRef font, const Palette& style)
{
    context.setFillColor(style.strip);
    context.drawRect(bounds, kDrawFilled);
    context.setFillColor(style.fill);
    context.drawRect(rect(bounds.left + 1.0, bounds.top + 1.0, 2.0,
        bounds.getHeight() - 2.0), kDrawFilled);
    auto valueBounds = bounds;
    valueBounds.left += 8.0;
    valueBounds.right -= 20.0;
    drawTextInRect(context, value, valueBounds, style.value, font, kLeftText);
    auto disclosureBounds = bounds;
    disclosureBounds.left = disclosureBounds.right - 18.0;
    disclosureBounds.right -= 4.0;
    drawTextInRect(context, "v", disclosureBounds,
        style.value, font, kCenterText);
}

void drawHorizontalSlider(CDrawContext& context, const CRect& track,
    double normalized, double handleTop, double handleHeight, const Palette& style)
{
    normalized = std::clamp(normalized, 0.0, 1.0);
    context.setFillColor(style.strip);
    context.drawRect(track, kDrawFilled);
    context.setFillColor(style.fill);
    context.drawRect(rect(track.left + 1.0, track.top + 1.0,
        std::max(1.0, (track.getWidth() - 2.0) * normalized),
        std::max(1.0, track.getHeight() - 2.0)), kDrawFilled);
    const double handleX = std::clamp(
        track.left + track.getWidth() * normalized - 1.5,
        track.left + 1.0, track.right - 4.0);
    context.setFillColor(style.text);
    context.drawRect(rect(handleX, handleTop, 3.0, handleHeight),
        kDrawFilled);
}

std::string pathToUtf8(const std::filesystem::path& path)
{
#if defined(_WIN32)
    const std::wstring wide = path.native();
    if (wide.empty()) return {};
    const int count = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(),
        static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    if (count <= 0) return {};
    std::string result(static_cast<std::size_t>(count), '\0');
    if (WideCharToMultiByte(CP_UTF8, 0, wide.c_str(),
            static_cast<int>(wide.size()), result.data(), count,
            nullptr, nullptr) <= 0) return {};
    return result;
#else
    return path.u8string();
#endif
}

std::filesystem::path pathFromUtf8(const char* path)
{
    if (!path || !path[0]) return {};
#if defined(_WIN32)
    return std::filesystem::path(utf8ToWide(path));
#else
    return std::filesystem::u8path(path);
#endif
}

std::filesystem::path presetDirectory(const char* pluginName)
{
    std::filesystem::path path;
#if defined(_WIN32)
    std::array<wchar_t, 32768u> profile {};
    const DWORD length = GetEnvironmentVariableW(L"USERPROFILE",
        profile.data(), static_cast<DWORD>(profile.size()));
    if (length == 0u || length >= profile.size()) return {};
    path = std::filesystem::path(profile.data());
#else
    const char* home = std::getenv("HOME");
    if (!home || !home[0]) return {};
    path = std::filesystem::u8path(home);
#endif
    path /= "Music";
    path /= "s3g";
    path /= "Presets";
    if (pluginName && pluginName[0]) path /= pathFromUtf8(pluginName);
    std::error_code error;
    std::filesystem::create_directories(path, error);
    return error ? std::filesystem::path {} : path;
}

std::string runFileDialog(CFrame* parent, const FileDialogOptions& options)
{
#if defined(_WIN32)
    const auto paths = runWindowsFileDialog(parent, options);
    return paths.empty() ? std::string{} : paths.front();
#else
    auto selector = owned(CNewFileSelector::create(parent,
        options.save ? CNewFileSelector::kSelectSaveFile
                     : options.directory ? CNewFileSelector::kSelectDirectory
                                         : CNewFileSelector::kSelectFile));
    if (!selector) return {};
    if (!options.extension.empty()) {
        const CFileExtension extension(
            options.extensionDescription.empty()
                ? options.extension.c_str()
                : options.extensionDescription.c_str(),
            options.extension.c_str());
        selector->addFileExtension(extension);
        selector->setDefaultExtension(extension);
    }
    for (const auto& suffix : options.extensions)
        selector->addFileExtension(CFileExtension(options.extensionDescription.c_str(), suffix.c_str()));
    if (!options.title.empty()) selector->setTitle(options.title.c_str());
    if (options.save && !options.defaultSaveName.empty())
        selector->setDefaultSaveName(options.defaultSaveName.c_str());
    if (!options.initialDirectory.empty()) {
        const std::string utf8 = pathToUtf8(options.initialDirectory);
        if (!utf8.empty()) selector->setInitialDirectory(utf8.c_str());
    }
    if (!selector->runModal() || selector->getNumSelectedFiles() == 0u)
        return {};
    const char* path = selector->getSelectedFile(0u);
    return path ? std::string(path) : std::string {};
#endif
}

std::vector<std::string> runFileDialogs(CFrame* parent, const FileDialogOptions& options)
{
    if (options.save) {
        const auto path = runFileDialog(parent, options);
        return path.empty() ? std::vector<std::string>{} : std::vector<std::string>{path};
    }
#if defined(_WIN32)
    return runWindowsFileDialog(parent, options, true);
#else
    auto selector = owned(CNewFileSelector::create(parent, CNewFileSelector::kSelectFile));
    if (!selector) return {};
    selector->setAllowMultiFileSelection(true);
    if (!options.title.empty()) selector->setTitle(options.title.c_str());
    if (!options.extension.empty()) {
        const CFileExtension extension(options.extensionDescription.c_str(), options.extension.c_str());
        selector->addFileExtension(extension);
        selector->setDefaultExtension(extension);
    }
    for (const auto& suffix : options.extensions)
        selector->addFileExtension(CFileExtension(options.extensionDescription.c_str(), suffix.c_str()));
    if (!options.initialDirectory.empty()) {
        const auto path = pathToUtf8(options.initialDirectory);
        selector->setInitialDirectory(path.c_str());
    }
    if (!selector->runModal()) return {};
    std::vector<std::string> paths;
    for (uint32_t i = 0; i < selector->getNumSelectedFiles(); ++i) {
        const char* path = selector->getSelectedFile(i);
        if (path && path[0]) paths.emplace_back(path);
    }
    return paths;
#endif
}

std::FILE* openFileUtf8(const char* path, const char* mode)
{
    if (!path || !path[0] || !mode || !mode[0]) return nullptr;
#if defined(_WIN32)
    const std::wstring widePath = utf8ToWide(path);
    const std::wstring wideMode = modeToWide(mode);
    return widePath.empty() || wideMode.empty()
        ? nullptr : _wfopen(widePath.c_str(), wideMode.c_str());
#else
    return std::fopen(path, mode);
#endif
}

ParameterEditSession::~ParameterEditSession() { end(); }

void ParameterEditSession::setCallbacks(ParameterEditCallbacks callbacks)
{
    end();
    callbacks_ = callbacks;
}

void ParameterEditSession::begin(uint32_t parameterId)
{
    if (parameterId_ == parameterId) return;
    end();
    parameterId_ = parameterId;
    if (parameterId_ != kNoParameter && callbacks_.begin)
        callbacks_.begin(callbacks_.context, parameterId_);
}

void ParameterEditSession::set(double value)
{
    if (parameterId_ != kNoParameter && callbacks_.set)
        callbacks_.set(callbacks_.context, parameterId_, value);
}

void ParameterEditSession::end()
{
    if (parameterId_ != kNoParameter && callbacks_.end)
        callbacks_.end(callbacks_.context, parameterId_);
    parameterId_ = kNoParameter;
}

void ParameterEditSession::perform(uint32_t parameterId, double value)
{
    begin(parameterId);
    set(value);
    end();
}

EditorHost::EditorHost(uint32_t nativeWidth, uint32_t nativeHeight,
    uint32_t requestedWidth, uint32_t requestedHeight)
    : nativeWidth_(nativeWidth)
    , nativeHeight_(nativeHeight)
    , width_(requestedWidth)
    , height_(requestedHeight)
    , runtimeAcquired_(acquireRuntime())
{
}

EditorHost::~EditorHost()
{
    if (view_) view_->stopRefresh();
    if (frame_) {
        if (opened_) frame_->close();
        else frame_->forget();
    }
    frame_ = nullptr;
    view_ = nullptr;
    if (runtimeAcquired_) releaseRuntime();
}

bool EditorHost::attach(ContentView* contentView)
{
    if (!runtimeAcquired_ || frame_ || !contentView
        || nativeWidth_ == 0u || nativeHeight_ == 0u) return false;
    frame_ = new CFrame(rect(0.0, 0.0, width_, height_), nullptr);
    if (!frame_) {
        contentView->forget();
        return false;
    }
    view_ = contentView;
    if (!frame_->addView(view_)) {
        view_->forget();
        view_ = nullptr;
        return false;
    }
    // VSTGUI's addView()/removeAll(true) contract consumes the creator's
    // reference when the frame removes its children. Keep that reference here;
    // releasing it after addView() would make close() double-release the view.
    updateViewportScale();
    return true;
}

bool EditorHost::setParent(void* nativeParent)
{
    if (!frame_ || !nativeParent || opened_) return false;
#if defined(__APPLE__)
    CocoaFrameConfig config;
    config.flags = CocoaFrameConfig::kNoCALayer;
    opened_ = frame_->open(nativeParent, PlatformType::kNSView, &config);
#elif defined(_WIN32)
    opened_ = frame_->open(nativeParent, PlatformType::kHWND);
#else
    (void)nativeParent;
    opened_ = false;
#endif
    return opened_;
}

bool EditorHost::setSize(uint32_t width, uint32_t height)
{
    if (!frame_ || !frame_->setSize(width, height)) return false;
    width_ = width;
    height_ = height;
    updateViewportScale();
    return true;
}

bool EditorHost::setVisible(bool visible)
{
    if (!frame_ || !view_) return false;
    frame_->setVisible(visible);
    view_->setVisible(visible);
    if (visible) view_->startRefresh();
    else view_->stopRefresh();
    return true;
}

void EditorHost::updateViewportScale()
{
    if (!frame_ || nativeWidth_ == 0u || nativeHeight_ == 0u) return;
    const double scale = std::min(
        static_cast<double>(width_) / static_cast<double>(nativeWidth_),
        static_cast<double>(height_) / static_cast<double>(nativeHeight_));
    frame_->setTransform(CGraphicsTransform().scale(scale, scale));
    if (view_ && view_->hasResponsiveLayout()) {
        const auto bounds = rect(0., 0., width_ / scale, height_ / scale);
        view_->setViewSize(bounds);
        view_->setMouseableArea(bounds);
    }
    frame_->invalid();
}

} // namespace s3g::portable_gui::foundation
