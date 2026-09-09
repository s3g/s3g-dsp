#pragma once

#include "s3g_vstgui_foundation.h"

#include <clap/ext/gui.h>
#include <clap/ext/state.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <utility>

namespace s3g::clap_gui::portable {

struct StateFileWriter {
    std::FILE* file = nullptr;
    clap_ostream_t stream {
        this,
        [](const clap_ostream_t* stream, const void* buffer,
           uint64_t size) -> int64_t {
            auto* writer = static_cast<StateFileWriter*>(stream->ctx);
            if (!writer || !writer->file || (!buffer && size != 0u))
                return -1;
            const std::size_t written = std::fwrite(buffer, 1u,
                static_cast<std::size_t>(size), writer->file);
            return written > 0u || size == 0u
                ? static_cast<int64_t>(written) : -1;
        },
    };
};

struct StateFileReader {
    std::FILE* file = nullptr;
    clap_istream_t stream {
        this,
        [](const clap_istream_t* stream, void* buffer,
           uint64_t size) -> int64_t {
            auto* reader = static_cast<StateFileReader*>(stream->ctx);
            if (!reader || !reader->file || (!buffer && size != 0u))
                return -1;
            const std::size_t read = std::fread(buffer, 1u,
                static_cast<std::size_t>(size), reader->file);
            if (read > 0u || std::feof(reader->file))
                return static_cast<int64_t>(read);
            return -1;
        },
    };
};

inline bool saveStateFile(const clap_plugin_t* plugin,
    const clap_plugin_state_t& state, const char* path)
{
    if (!plugin || !state.save || !path || !path[0]) return false;
    StateFileWriter writer;
    writer.file = s3g::portable_gui::foundation::openFileUtf8(path, "wb");
    if (!writer.file) return false;
    const bool succeeded = state.save(plugin, &writer.stream);
    const bool closed = std::fclose(writer.file) == 0;
    return succeeded && closed;
}

inline bool loadStateFile(const clap_plugin_t* plugin,
    const clap_plugin_state_t& state, const char* path)
{
    if (!plugin || !state.load || !path || !path[0]) return false;
    StateFileReader reader;
    reader.file = s3g::portable_gui::foundation::openFileUtf8(path, "rb");
    if (!reader.file) return false;
    const bool succeeded = state.load(plugin, &reader.stream);
    const bool closed = std::fclose(reader.file) == 0;
    return succeeded && closed;
}

inline const char* windowApi()
{
#if defined(__APPLE__)
    return CLAP_WINDOW_API_COCOA;
#elif defined(_WIN32)
    return CLAP_WINDOW_API_WIN32;
#else
    return "";
#endif
}

inline bool isApiSupported(const char* api, bool floating)
{
    const char* supported = windowApi();
    return !floating && api && supported[0]
        && std::strcmp(api, supported) == 0;
}

inline bool getPreferredApi(const char** api, bool* floating)
{
    if (!api || !floating || !windowApi()[0]) return false;
    *api = windowApi();
    *floating = false;
    return true;
}

inline bool getSize(uint32_t currentWidth, uint32_t currentHeight,
    uint32_t* width, uint32_t* height)
{
    if (!width || !height) return false;
    *width = currentWidth;
    *height = currentHeight;
    return true;
}

inline bool getResizeHints(uint32_t nativeWidth, uint32_t nativeHeight,
    clap_gui_resize_hints_t* hints)
{
    if (!hints || nativeWidth == 0u || nativeHeight == 0u) return false;
    hints->can_resize_horizontally = true;
    hints->can_resize_vertically = true;
    hints->preserve_aspect_ratio = true;
    hints->aspect_ratio_width = nativeWidth;
    hints->aspect_ratio_height = nativeHeight;
    return true;
}

inline bool adjustSize(uint32_t nativeWidth, uint32_t nativeHeight,
    uint32_t* width, uint32_t* height)
{
    if (!width || !height || nativeWidth == 0u || nativeHeight == 0u)
        return false;
    const double scale = std::clamp(std::min(
        static_cast<double>(*width) / static_cast<double>(nativeWidth),
        static_cast<double>(*height) / static_cast<double>(nativeHeight)),
        s3g::portable_gui::foundation::kMinimumEditorScale,
        s3g::portable_gui::foundation::kMaximumEditorScale);
    *width = static_cast<uint32_t>(std::lround(nativeWidth * scale));
    *height = static_cast<uint32_t>(std::lround(nativeHeight * scale));
    return true;
}

inline void* nativeParent(const clap_window_t* window)
{
    if (!window || !window->api
        || std::strcmp(window->api, windowApi()) != 0) return nullptr;
#if defined(__APPLE__)
    return window->cocoa;
#elif defined(_WIN32)
    return window->win32;
#else
    return nullptr;
#endif
}

template <typename Editor, typename Create>
bool create(Editor*& editor, const char* api, bool floating, Create&& factory)
{
    if (!isApiSupported(api, floating)) return false;
    if (editor) return true;
    editor = std::forward<Create>(factory)();
    return editor != nullptr;
}

template <typename Editor, typename Destroy>
void destroy(Editor*& editor, Destroy&& destroyEditor)
{
    if (!editor) return;
    std::forward<Destroy>(destroyEditor)(editor);
    editor = nullptr;
}

template <typename Editor, typename SetSize>
bool setSize(Editor* editor, uint32_t nativeWidth, uint32_t nativeHeight,
    uint32_t& currentWidth, uint32_t& currentHeight,
    uint32_t requestedWidth, uint32_t requestedHeight,
    SetSize&& setEditorSize)
{
    if (!adjustSize(nativeWidth, nativeHeight,
            &requestedWidth, &requestedHeight)) return false;
    if (editor && !std::forward<SetSize>(setEditorSize)(
            editor, requestedWidth, requestedHeight)) return false;
    currentWidth = requestedWidth;
    currentHeight = requestedHeight;
    return true;
}

template <typename Editor, typename SetParent>
bool setParent(Editor* editor, const clap_window_t* window,
    SetParent&& setEditorParent)
{
    void* parent = nativeParent(window);
    return editor && parent
        && std::forward<SetParent>(setEditorParent)(editor, parent);
}

template <typename Editor, typename SetVisible>
bool setVisible(Editor* editor, bool& visible, bool nextVisible,
    SetVisible&& setEditorVisible)
{
    if (!editor || !std::forward<SetVisible>(setEditorVisible)(
            editor, nextVisible)) return false;
    visible = nextVisible;
    return true;
}

template <typename Editor, typename SetVisible>
bool setVisible(Editor* editor, std::atomic<bool>& visible,
    bool nextVisible, SetVisible&& setEditorVisible)
{
    if (!editor || !std::forward<SetVisible>(setEditorVisible)(
            editor, nextVisible)) return false;
    visible.store(nextVisible, std::memory_order_relaxed);
    return true;
}

} // namespace s3g::clap_gui::portable
