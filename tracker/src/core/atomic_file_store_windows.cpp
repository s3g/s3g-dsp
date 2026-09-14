#include "atomic_file_store.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <string>
#include <vector>

namespace s3g::tracker::file_store {
namespace {
ProjectResult failure(ProjectErrorCode code, std::string_view path,
    const char* operation, DWORD error = GetLastError())
{
    return {code, std::string(path), std::string(operation)
        + " (Windows error " + std::to_string(error) + ")"};
}

// Resolve once, then use wide, extended-length absolute paths for every call.
// This also keeps the temporary file beside the destination if cwd changes.
bool nativePath(std::string_view path, std::wstring& destination)
{
    if (path.empty() || path.size() > 131000 || path.find('\0') != path.npos
        || path.back() == '/' || path.back() == '\\') return false;
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        path.data(), static_cast<int>(path.size()), nullptr, 0);
    if (count <= 0 || count > 32700) return false;
    std::wstring wide(static_cast<std::size_t>(count), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.data(),
            static_cast<int>(path.size()), wide.data(), count) != count) return false;
    std::replace(wide.begin(), wide.end(), L'/', L'\\');
    if (wide.rfind(L"\\\\.\\", 0) == 0) return false;
    if (wide.rfind(L"\\\\?\\UNC\\", 0) == 0) wide = L"\\\\" + wide.substr(8);
    else if (wide.rfind(L"\\\\?\\", 0) == 0) {
        if (wide.size() < 7 || wide[5] != L':' || wide[6] != L'\\') return false;
        wide.erase(0, 4);
    }
    DWORD size = GetFullPathNameW(wide.c_str(), 0, nullptr, nullptr);
    if (!size || size > 32700) return false;
    std::vector<wchar_t> buffer(size);
    DWORD length = GetFullPathNameW(wide.c_str(), size, buffer.data(), nullptr);
    if (!length || length >= size) return false;
    std::wstring absolute(buffer.data(), length);
    if (absolute.back() == L'\\') return false;
    // Alternate data streams are not standalone project/pack files.
    if (absolute.find(L':', 2) != std::wstring::npos) return false;
    destination = absolute.rfind(L"\\\\", 0) == 0
        ? L"\\\\?\\UNC\\" + absolute.substr(2) : L"\\\\?\\" + absolute;
    return destination.size() <= 32700;
}

class File {
public:
    HANDLE handle = INVALID_HANDLE_VALUE;
    std::wstring temporary;
    File() = default;
    File(const File&) = delete;
    File& operator=(const File&) = delete;
    ~File() {
        if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
        if (!temporary.empty()) DeleteFileW(temporary.c_str());
    }
    bool close() {
        const auto h = handle;
        handle = INVALID_HANDLE_VALUE;
        return h == INVALID_HANDLE_VALUE || CloseHandle(h) != 0;
    }
    bool create(const std::wstring& target) {
        static std::atomic<uint64_t> serial {0};
        for (unsigned attempt = 0; attempt < 128; ++attempt) {
            auto candidate = target + L".tmp." + std::to_wstring(GetCurrentProcessId())
                + L"-" + std::to_wstring(serial.fetch_add(1, std::memory_order_relaxed));
            handle = CreateFileW(candidate.c_str(), GENERIC_WRITE, 0, nullptr,
                CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (handle != INVALID_HANDLE_VALUE) {
                temporary = std::move(candidate);
                return true;
            }
            const auto error = GetLastError();
            if (error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS) return false;
        }
        SetLastError(ERROR_FILE_EXISTS);
        return false;
    }
};
ProjectResult invalidPath(std::string_view path) {
    return {ProjectErrorCode::InvalidArgument, std::string(path),
        "Tracker path must be a valid UTF-8 file path, not a directory, device or data stream"};
}
} // namespace

ProjectResult saveBytesAtomically(std::string_view bytes, std::string_view path)
{
    std::wstring target;
    if (!nativePath(path, target)) return invalidPath(path);
    if (bytes.size() > kMaximumProjectDocumentBytes)
        return {ProjectErrorCode::SizeLimitExceeded, std::string(path),
            "Tracker file exceeds the 64 MiB limit"};
    File file;
    if (!file.create(target))
        return failure(ProjectErrorCode::IoOpenFailed, path, "create temporary Tracker file");
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const DWORD count = static_cast<DWORD>(std::min<std::size_t>(65536, bytes.size() - offset));
        DWORD written = 0;
        if (!WriteFile(file.handle, bytes.data() + offset, count, &written, nullptr))
            return failure(ProjectErrorCode::IoWriteFailed, path, "write temporary Tracker file");
        if (!written)
            return failure(ProjectErrorCode::IoWriteFailed, path, "write temporary Tracker file",
                ERROR_WRITE_FAULT);
        offset += written;
    }
    if (!FlushFileBuffers(file.handle))
        return failure(ProjectErrorCode::IoSyncFailed, path, "flush temporary Tracker file");
    if (!file.close())
        return failure(ProjectErrorCode::IoWriteFailed, path, "close temporary Tracker file");
    if (!MoveFileExW(file.temporary.c_str(), target.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        return failure(ProjectErrorCode::IoRenameFailed, path, "publish Tracker file");
    file.temporary.clear();
    return {};
}

ProjectResult loadBytes(std::string_view path, std::string& destination)
{
    std::wstring target;
    if (!nativePath(path, target)) return invalidPath(path);
    File file;
    file.handle = CreateFileW(target.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file.handle == INVALID_HANDLE_VALUE)
        return failure(ProjectErrorCode::IoOpenFailed, path, "open Tracker file");
    std::string bytes;
    std::array<char, 16384> buffer {};
    for (;;) {
        DWORD count = 0;
        if (!ReadFile(file.handle, buffer.data(), static_cast<DWORD>(buffer.size()), &count, nullptr))
            return failure(ProjectErrorCode::IoReadFailed, path, "read Tracker file");
        if (!count) break;
        if (count > kMaximumProjectDocumentBytes - bytes.size())
            return {ProjectErrorCode::SizeLimitExceeded, std::string(path),
                "Tracker file exceeds the 64 MiB limit"};
        bytes.append(buffer.data(), count);
    }
    if (!file.close())
        return failure(ProjectErrorCode::IoReadFailed, path, "close Tracker file");
    destination = std::move(bytes);
    return {};
}
} // namespace s3g::tracker::file_store
