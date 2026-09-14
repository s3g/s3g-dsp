#pragma once
#ifndef NOMINMAX
#define NOMINMAX 1
#endif
#include <windows.h>
#include <clap/clap.h>
#include <clap/ext/draft/transport-control.h>
#include <optional>
#include <string>
#include <vector>
#include <cmath>

namespace s3g::tracker::windows {
inline std::wstring wide(const std::string& text)
{
    if (text.empty()) return {};
    const auto n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        text.data(), int(text.size()), nullptr, 0);
    if (!n) return {};
    std::wstring result(n, 0);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
        int(text.size()), result.data(), n);
    return result;
}
inline std::string utf8(const std::wstring& text)
{
    if (text.empty()) return {};
    const auto n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
        text.data(), int(text.size()), nullptr, 0, nullptr, nullptr);
    if (!n) return {};
    std::string result(n, 0);
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
        int(text.size()), result.data(), n, nullptr, nullptr);
    return result;
}
// REAPER's documented CLAP extension and accelerator ABI:
// https://github.com/justinfrankel/reaper-sdk/blob/main/sdk/reaper_plugin.h
struct ReaperBridge {
    int version;
    HWND mainWindow;
    int (*reg)(const char*, void*);
    void* (*get)(const char*);
};
struct Accelerator {
    int (*translate)(MSG*, Accelerator*) = nullptr;
    bool local = true;
    void* user = nullptr;
};
inline const ReaperBridge* bridge(const clap_host_t* host)
{
    return host && host->get_extension ? static_cast<const ReaperBridge*>(
        host->get_extension(host, "cockos.reaper_extension")) : nullptr;
}
inline std::optional<bool> playing(const clap_host_t* host)
{
    const auto* b = bridge(host);
    const auto fn = b && b->get ? reinterpret_cast<int (*)()>(b->get("GetPlayState")) : nullptr;
    if (!fn) return {};
    const int state = fn();
    return (state & 1) && !(state & 2);
}
inline std::optional<double> tempo(const clap_host_t* host)
{
    const auto* b = bridge(host);
    const auto fn = b && b->get ? reinterpret_cast<double (*)()>(b->get("Master_GetTempo")) : nullptr;
    if (!fn) return {};
    const double bpm = fn();
    return std::isfinite(bpm) && bpm > 0 ? std::optional<double>(bpm) : std::nullopt;
}
inline bool transport(const clap_host_t* host, int action, bool fallbackPlaying)
{
    if (!host || !host->get_extension) return false;
    const auto* control = static_cast<const clap_host_transport_control_t*>(
        host->get_extension(host, CLAP_EXT_TRANSPORT_CONTROL));
    if (control) {
        auto fn = action == 0 ? control->request_toggle_play
            : action == 1 ? control->request_continue : control->request_stop;
        if (!fn && action == 0)
            fn = playing(host).value_or(fallbackPlaying)
                ? control->request_pause : control->request_continue;
        if (fn) { fn(host); return true; }
    }
    const auto* b = bridge(host);
    const char* name = action == 1 ? "OnPlayButton" : action == 2 ? "OnStopButton"
        : playing(host).value_or(fallbackPlaying) ? "OnPauseButton" : "OnPlayButton";
    auto fn = b && b->get ? reinterpret_cast<void (*)()>(b->get(name)) : nullptr;
    if (!fn) return false;
    fn(); return true;
}
} // namespace s3g::tracker::windows
