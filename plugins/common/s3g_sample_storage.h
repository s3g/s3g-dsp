#pragma once

#include <clap/clap.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <system_error>

namespace s3g::sample_storage {

enum class StorageMode : uint8_t {
    Project = 0u,
    Link = 1u,
    Embed = 2u,
};

inline StorageMode sanitizeStorageMode(uint8_t value,
    StorageMode fallback = StorageMode::Project) noexcept
{
    return value <= static_cast<uint8_t>(StorageMode::Embed)
        ? static_cast<StorageMode>(value) : fallback;
}

inline const char* storageModeName(StorageMode mode) noexcept
{
    switch (mode) {
    case StorageMode::Project: return "PROJECT";
    case StorageMode::Link: return "LINK";
    case StorageMode::Embed: return "EMBED";
    }
    return "PROJECT";
}

// REAPER exposes this stable prefix of reaper_plugin_info_t to CLAP plug-ins
// through cockos.reaper_extension. Keep the bridge independent of the REAPER
// SDK so every Sample-family target remains buildable on non-REAPER hosts and
// on every platform supported by CLAP.
struct ReaperHostBridge {
    int callerVersion = 0;
    void* mainWindow = nullptr;
    int (*registerObject)(const char*, void*) = nullptr;
    void* (*getFunction)(const char*) = nullptr;
};

using ClapGetReaperContext = void* (*)(const clap_host_t*, int);
using ReaperEnumProjects = void* (*)(int, char*, int);
using ReaperGetProjectPathEx = void (*)(void*, char*, int);
using ReaperRegisterObject = int (*)(const char*, void*);

struct ReaperContext {
    const clap_host_t* host = nullptr;
    const ReaperHostBridge* bridge = nullptr;
    ClapGetReaperContext getContext = nullptr;
    ReaperEnumProjects enumProjects = nullptr;
    ReaperGetProjectPathEx getProjectPathEx = nullptr;
    ReaperRegisterObject registerObject = nullptr;
    void* project = nullptr;
    void* fxDsp = nullptr;

    bool available() const noexcept
    {
        return host && bridge && getContext && enumProjects
            && getProjectPathEx && project;
    }

    bool canRegisterProjectFiles() const noexcept
    {
        return available() && registerObject && fxDsp;
    }
};

template <typename Function>
inline Function reaperFunction(const ReaperHostBridge* bridge,
    const char* name) noexcept
{
    return bridge && bridge->getFunction && name
        ? reinterpret_cast<Function>(bridge->getFunction(name)) : nullptr;
}

// clap_get_reaper_context was introduced in REAPER 6.80. Selector 3 returns
// the exact ReaProject owning this CLAP instance and selector 4 its FxDsp.
// Function availability, rather than a parsed host-version string, is the safe
// capability check for old REAPER builds and non-REAPER CLAP hosts.
inline ReaperContext reaperContext(const clap_host_t* host) noexcept
{
    ReaperContext context;
    context.host = host;
    if (!host || !host->get_extension) return context;
    context.bridge = static_cast<const ReaperHostBridge*>(
        host->get_extension(host, "cockos.reaper_extension"));
    if (!context.bridge || !context.bridge->getFunction) return context;
    context.getContext = reaperFunction<ClapGetReaperContext>(
        context.bridge, "clap_get_reaper_context");
    context.enumProjects = reaperFunction<ReaperEnumProjects>(
        context.bridge, "EnumProjects");
    context.getProjectPathEx = reaperFunction<ReaperGetProjectPathEx>(
        context.bridge, "GetProjectPathEx");
    context.registerObject = context.bridge->registerObject
        ? context.bridge->registerObject
        : reaperFunction<ReaperRegisterObject>(
            context.bridge, "plugin_register");
    if (context.getContext) {
        context.project = context.getContext(host, 3);
        context.fxDsp = context.getContext(host, 4);
    }
    return context;
}

struct ProjectLocation {
    void* project = nullptr;
    void* fxDsp = nullptr;
    std::string projectFilePath;
    std::string mediaDirectory;
    bool saved = false;

    bool available() const noexcept
    {
        return saved && project && !projectFilePath.empty()
            && !mediaDirectory.empty();
    }
};

inline void setError(std::string* error, const char* message)
{
    if (error) *error = message ? message : "";
}

inline bool queryProjectLocation(const ReaperContext& context,
    ProjectLocation& location, std::string* error = nullptr)
{
    location = {};
    location.project = context.project;
    location.fxDsp = context.fxDsp;
    if (!context.available()) {
        setError(error, "PROJECT STORAGE REQUIRES REAPER 6.80 OR NEWER");
        return false;
    }

    // Do not use EnumProjects(-1): another project tab can be active while an
    // FX editor belongs to this instance's project. Enumerate and pointer-match
    // the selector-3 context so save state and media path are exact.
    constexpr int kMaximumPathBytes = 32768;
    std::array<char, kMaximumPathBytes> projectFile {};
    bool found = false;
    for (int index = 0; index < 100000; ++index) {
        projectFile.fill('\0');
        void* candidate = context.enumProjects(index, projectFile.data(),
            static_cast<int>(projectFile.size()));
        if (!candidate) break;
        if (candidate == context.project) {
            found = true;
            break;
        }
    }
    if (!found) {
        setError(error, "COULD NOT FIND THIS REAPER PROJECT");
        return false;
    }
    if (projectFile[0] == '\0') {
        setError(error, "SAVE THE REAPER PROJECT BEFORE USING PROJECT STORAGE");
        return false;
    }

    std::array<char, kMaximumPathBytes> mediaPath {};
    context.getProjectPathEx(context.project, mediaPath.data(),
        static_cast<int>(mediaPath.size()));
    if (mediaPath[0] == '\0') {
        setError(error, "REAPER DID NOT PROVIDE A PROJECT MEDIA PATH");
        return false;
    }

    std::filesystem::path media(mediaPath.data());
    const std::filesystem::path projectPath(projectFile.data());
    if (media.is_relative()) media = projectPath.parent_path() / media;
    media = media.lexically_normal();
    if (media.empty()) {
        setError(error, "REAPER PROJECT MEDIA PATH IS INVALID");
        return false;
    }
    location.projectFilePath = projectPath.lexically_normal().string();
    location.mediaDirectory = media.string();
    location.saved = true;
    if (error) error->clear();
    return true;
}

inline bool relativePathIsSafe(const std::filesystem::path& path) noexcept
{
    if (path.empty() || path.is_absolute() || path.has_root_path()) return false;
    for (const auto& component : path) {
        if (component == "..") return false;
    }
    return true;
}

inline bool resolveProjectRelativePath(const ProjectLocation& location,
    const std::string& relative, std::string& absolute,
    std::string* error = nullptr)
{
    absolute.clear();
    if (!location.available()) {
        setError(error, "PROJECT MEDIA PATH IS NOT AVAILABLE");
        return false;
    }
    const std::filesystem::path relativePath(relative);
    if (!relativePathIsSafe(relativePath)) {
        setError(error, "PROJECT SAMPLE PATH IS NOT A SAFE RELATIVE PATH");
        return false;
    }
    absolute = (std::filesystem::path(location.mediaDirectory)
        / relativePath).lexically_normal().string();
    if (error) error->clear();
    return true;
}

inline bool resolveProjectRelativePath(const ReaperContext& context,
    const std::string& relative, std::string& absolute,
    std::string* error = nullptr)
{
    ProjectLocation location;
    if (!queryProjectLocation(context, location, error)) {
        absolute.clear();
        return false;
    }
    return resolveProjectRelativePath(location, relative, absolute, error);
}

inline bool makeProjectRelativePath(const ProjectLocation& location,
    const std::string& absolute, std::string& relative,
    std::string* error = nullptr)
{
    relative.clear();
    if (!location.available()) {
        setError(error, "PROJECT MEDIA PATH IS NOT AVAILABLE");
        return false;
    }
    const std::filesystem::path absolutePath(absolute);
    if (absolutePath.empty() || !absolutePath.is_absolute()) {
        setError(error, "PROJECT SAMPLE PATH IS NOT ABSOLUTE");
        return false;
    }
    const auto candidate = absolutePath.lexically_normal().lexically_relative(
        std::filesystem::path(location.mediaDirectory).lexically_normal());
    if (!relativePathIsSafe(candidate)) {
        setError(error, "SAMPLE IS OUTSIDE THE REAPER PROJECT MEDIA PATH");
        return false;
    }
    relative = candidate.generic_string();
    if (error) error->clear();
    return true;
}

inline bool makeProjectRelativePath(const ReaperContext& context,
    const std::string& absolute, std::string& relative,
    std::string* error = nullptr)
{
    ProjectLocation location;
    if (!queryProjectLocation(context, location, error)) {
        relative.clear();
        return false;
    }
    return makeProjectRelativePath(location, absolute, relative, error);
}

namespace detail {

inline uint32_t rotateRight(uint32_t value, uint32_t amount) noexcept
{
    return (value >> amount) | (value << (32u - amount));
}

class Sha256 {
public:
    void update(const uint8_t* bytes, std::size_t count) noexcept
    {
        if (!bytes || count == 0u) return;
        totalBytes_ += count;
        while (count > 0u) {
            const std::size_t amount = std::min(count,
                block_.size() - blockBytes_);
            std::copy_n(bytes, amount, block_.data() + blockBytes_);
            blockBytes_ += amount;
            bytes += amount;
            count -= amount;
            if (blockBytes_ == block_.size()) {
                transform(block_.data());
                blockBytes_ = 0u;
            }
        }
    }

    std::array<uint8_t, 32u> finish() noexcept
    {
        const uint64_t bitCount = totalBytes_ * 8u;
        block_[blockBytes_++] = 0x80u;
        if (blockBytes_ > 56u) {
            std::fill(block_.begin() + blockBytes_, block_.end(), 0u);
            transform(block_.data());
            blockBytes_ = 0u;
        }
        std::fill(block_.begin() + blockBytes_, block_.begin() + 56u, 0u);
        for (std::size_t index = 0u; index < 8u; ++index)
            block_[63u - index] = static_cast<uint8_t>(
                bitCount >> (index * 8u));
        transform(block_.data());
        std::array<uint8_t, 32u> digest {};
        for (std::size_t word = 0u; word < state_.size(); ++word) {
            for (std::size_t byte = 0u; byte < 4u; ++byte)
                digest[word * 4u + byte] = static_cast<uint8_t>(
                    state_[word] >> ((3u - byte) * 8u));
        }
        return digest;
    }

private:
    void transform(const uint8_t* block) noexcept
    {
        static constexpr std::array<uint32_t, 64u> constants {{
            0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
            0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
            0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
            0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
            0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
            0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
            0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
            0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
            0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
            0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
            0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
            0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
            0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
            0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
            0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
            0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
        }};
        std::array<uint32_t, 64u> words {};
        for (std::size_t index = 0u; index < 16u; ++index) {
            words[index] = (static_cast<uint32_t>(block[index * 4u]) << 24u)
                | (static_cast<uint32_t>(block[index * 4u + 1u]) << 16u)
                | (static_cast<uint32_t>(block[index * 4u + 2u]) << 8u)
                | static_cast<uint32_t>(block[index * 4u + 3u]);
        }
        for (std::size_t index = 16u; index < words.size(); ++index) {
            const uint32_t a = words[index - 15u];
            const uint32_t b = words[index - 2u];
            const uint32_t s0 = rotateRight(a, 7u) ^ rotateRight(a, 18u)
                ^ (a >> 3u);
            const uint32_t s1 = rotateRight(b, 17u) ^ rotateRight(b, 19u)
                ^ (b >> 10u);
            words[index] = words[index - 16u] + s0
                + words[index - 7u] + s1;
        }
        uint32_t a = state_[0u];
        uint32_t b = state_[1u];
        uint32_t c = state_[2u];
        uint32_t d = state_[3u];
        uint32_t e = state_[4u];
        uint32_t f = state_[5u];
        uint32_t g = state_[6u];
        uint32_t h = state_[7u];
        for (std::size_t index = 0u; index < words.size(); ++index) {
            const uint32_t sum1 = rotateRight(e, 6u) ^ rotateRight(e, 11u)
                ^ rotateRight(e, 25u);
            const uint32_t choose = (e & f) ^ (~e & g);
            const uint32_t temp1 = h + sum1 + choose + constants[index]
                + words[index];
            const uint32_t sum0 = rotateRight(a, 2u) ^ rotateRight(a, 13u)
                ^ rotateRight(a, 22u);
            const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t temp2 = sum0 + majority;
            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }
        state_[0u] += a;
        state_[1u] += b;
        state_[2u] += c;
        state_[3u] += d;
        state_[4u] += e;
        state_[5u] += f;
        state_[6u] += g;
        state_[7u] += h;
    }

    std::array<uint32_t, 8u> state_ {{
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u,
    }};
    std::array<uint8_t, 64u> block_ {};
    std::size_t blockBytes_ = 0u;
    uint64_t totalBytes_ = 0u;
};

inline bool hashFile(const std::filesystem::path& path,
    std::string& hexadecimal, uint64_t& byteCount, std::string& error)
{
    hexadecimal.clear();
    byteCount = 0u;
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "COULD NOT OPEN SAMPLE FOR PROJECT COPY";
        return false;
    }
    Sha256 hash;
    // Keep worker-thread stack use comfortably below macOS's comparatively
    // small default std::thread stack while retaining efficient sequential I/O.
    std::array<uint8_t, 64u * 1024u> buffer {};
    while (input) {
        input.read(reinterpret_cast<char*>(buffer.data()),
            static_cast<std::streamsize>(buffer.size()));
        const std::streamsize count = input.gcount();
        if (count > 0) {
            const auto amount = static_cast<std::size_t>(count);
            if (amount > std::numeric_limits<uint64_t>::max() - byteCount) {
                error = "SAMPLE IS TOO LARGE FOR PROJECT COPY";
                return false;
            }
            hash.update(buffer.data(), amount);
            byteCount += amount;
        }
    }
    if (!input.eof()) {
        error = "COULD NOT READ SAMPLE FOR PROJECT COPY";
        return false;
    }
    const auto digest = hash.finish();
    std::ostringstream text;
    text << std::hex << std::setfill('0');
    for (const uint8_t byte : digest)
        text << std::setw(2) << static_cast<unsigned>(byte);
    hexadecimal = text.str();
    error.clear();
    return true;
}

inline std::string safeExtension(const std::filesystem::path& path)
{
    std::string extension = path.extension().string();
    if (extension.size() > 16u) return {};
    for (char& character : extension) {
        const auto value = static_cast<unsigned char>(character);
        if (character != '.' && character != '_' && character != '-'
            && !std::isalnum(value)) return {};
        character = static_cast<char>(std::tolower(value));
    }
    return extension;
}

inline std::string recognizableStem(const std::filesystem::path& path)
{
    constexpr std::size_t kMaximumStemBytes = 40u;
    const std::string source = path.stem().string();
    std::string stem;
    stem.reserve(std::min(source.size(), kMaximumStemBytes));
    bool separatorPending = false;
    for (const char character : source) {
        const auto value = static_cast<unsigned char>(character);
        if (std::isalnum(value) || character == '_') {
            if (separatorPending && !stem.empty()
                && stem.size() < kMaximumStemBytes) stem.push_back('-');
            separatorPending = false;
            if (stem.size() < kMaximumStemBytes)
                stem.push_back(static_cast<char>(std::tolower(value)));
        } else {
            separatorPending = !stem.empty();
        }
        if (stem.size() >= kMaximumStemBytes) break;
    }
    while (!stem.empty() && (stem.back() == '-' || stem.back() == '_'))
        stem.pop_back();
    return stem.empty() ? "sample" : stem;
}

inline std::filesystem::path uniqueTemporaryPath(
    const std::filesystem::path& destination)
{
    static std::atomic<uint64_t> serial { 0u };
    const uint64_t tick = static_cast<uint64_t>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count());
    const uint64_t count = serial.fetch_add(1u, std::memory_order_relaxed);
    return destination.parent_path() / ("." + destination.filename().string()
        + ".s3g-" + std::to_string(tick) + "-" + std::to_string(count)
        + ".tmp");
}

} // namespace detail

struct ProjectCopyResult {
    bool success = false;
    std::string absolutePath;
    std::string relativePath;
    std::string contentHash;
    std::string error;
    uint64_t byteCount = 0u;
};

// This function performs filesystem and hashing work only. Query the
// ProjectLocation on the main thread, then call this overload from the sample
// loader worker; it never calls back into the CLAP or REAPER host.
inline ProjectCopyResult copyFileIntoProject(const ProjectLocation& location,
    const std::string& sourcePath)
{
    ProjectCopyResult result;
    if (!location.available()) {
        result.error = "SAVE THE REAPER PROJECT BEFORE USING PROJECT STORAGE";
        return result;
    }
    const std::filesystem::path source(sourcePath);
    std::error_code filesystemError;
    if (source.empty() || !std::filesystem::is_regular_file(
            source, filesystemError)) {
        result.error = "PROJECT COPY SOURCE FILE IS NOT AVAILABLE";
        return result;
    }
    if (!detail::hashFile(source, result.contentHash, result.byteCount,
            result.error)) return result;

    const std::filesystem::path directory
        = std::filesystem::path(location.mediaDirectory) / "s3g Samples";
    std::filesystem::create_directories(directory, filesystemError);
    if (filesystemError) {
        result.error = "COULD NOT CREATE THE PROJECT SAMPLE DIRECTORY";
        return result;
    }

    // A source selected from this project's media directory is already in its
    // final storage domain. Reuse it directly instead of feeding its existing
    // content-addressed filename back through the naming step and creating a
    // second, nested hash-suffixed copy.
    std::string existingRelative;
    const std::string normalizedSource = source.lexically_normal().string();
    if (makeProjectRelativePath(location, normalizedSource,
            existingRelative, nullptr)) {
        result.absolutePath = normalizedSource;
        result.relativePath = existingRelative;
        result.success = true;
        result.error.clear();
        return result;
    }

    const std::string extension = detail::safeExtension(source);
    const std::string readableName = detail::recognizableStem(source) + "-"
        + result.contentHash.substr(0u, 16u) + extension;
    std::filesystem::path destination = directory / readableName;

    const auto pathMatches = [&](const std::filesystem::path& candidate) {
        filesystemError.clear();
        if (!std::filesystem::is_regular_file(candidate, filesystemError)
            || filesystemError) return false;
        std::string existingHash;
        std::string hashError;
        uint64_t existingBytes = 0u;
        return detail::hashFile(candidate, existingHash, existingBytes,
                   hashError)
            && existingHash == result.contentHash
            && existingBytes == result.byteCount;
    };

    // A readable short-hash name is overwhelmingly unique. If an unrelated
    // file already owns that name, retain deterministic content addressing by
    // falling back to the complete digest instead of replacing either file.
    filesystemError.clear();
    bool usingFullHashName = false;
    if (std::filesystem::is_regular_file(destination, filesystemError)
        && !filesystemError && !pathMatches(destination)) {
        destination = directory / (result.contentHash + extension);
        usingFullHashName = true;
    }
    filesystemError.clear();
    if (usingFullHashName
        && std::filesystem::is_regular_file(destination, filesystemError)
        && !filesystemError && !pathMatches(destination)) {
        result.error = "PROJECT SAMPLE HASH DESTINATION IS OCCUPIED";
        return result;
    }

    const auto existingMatches = [&]() { return pathMatches(destination); };

    if (!existingMatches()) {
        filesystemError.clear();
        const auto temporary = detail::uniqueTemporaryPath(destination);
        std::filesystem::copy_file(source, temporary,
            std::filesystem::copy_options::none, filesystemError);
        if (filesystemError) {
            std::error_code cleanupError;
            std::filesystem::remove(temporary, cleanupError);
            result.error = "COULD NOT COPY SAMPLE INTO THE REAPER PROJECT";
            return result;
        }
        std::string temporaryHash;
        std::string hashError;
        uint64_t temporaryBytes = 0u;
        if (!detail::hashFile(temporary, temporaryHash, temporaryBytes,
                hashError)
            || temporaryHash != result.contentHash
            || temporaryBytes != result.byteCount) {
            std::filesystem::remove(temporary, filesystemError);
            result.error = "PROJECT SAMPLE COPY FAILED VERIFICATION";
            return result;
        }
        filesystemError.clear();
        std::filesystem::rename(temporary, destination, filesystemError);
        if (filesystemError) {
            // Another instance may have completed the identical content copy
            // between our existence check and rename. Accept only a verified
            // winner and remove our private temporary file.
            if (!existingMatches()) {
                std::filesystem::remove(temporary, filesystemError);
                result.error = "COULD NOT INSTALL THE PROJECT SAMPLE COPY";
                return result;
            }
            std::filesystem::remove(temporary, filesystemError);
        }
    }

    result.absolutePath = destination.lexically_normal().string();
    if (!makeProjectRelativePath(location, result.absolutePath,
            result.relativePath, &result.error)) return result;
    result.success = true;
    result.error.clear();
    return result;
}

inline ProjectCopyResult copyFileIntoProject(const ReaperContext& context,
    const std::string& sourcePath)
{
    ProjectLocation location;
    ProjectCopyResult result;
    if (!queryProjectLocation(context, location, &result.error)) return result;
    return copyFileIntoProject(location, sourcePath);
}

inline std::size_t utf8PrefixBytes(const std::string& text,
    std::size_t maximum) noexcept
{
    std::size_t bytes = std::min(maximum, text.size());
    while (bytes > 0u && bytes < text.size()
        && (static_cast<unsigned char>(text[bytes]) & 0xc0u) == 0x80u)
        --bytes;
    return bytes;
}

inline std::string abbreviatedPath(const std::string& path,
    std::size_t maximumCharacters = 42u)
{
    if (path.size() <= maximumCharacters || maximumCharacters < 8u)
        return path;
    const std::filesystem::path filesystemPath(path);
    std::string tail = filesystemPath.filename().string();
    const std::string parent = filesystemPath.parent_path().filename().string();
    if (!parent.empty()) tail = parent + "/" + tail;
    if (tail.size() + 4u <= maximumCharacters) return ".../" + tail;
    const std::string filename = filesystemPath.filename().string();
    if (filename.size() + 4u <= maximumCharacters) return ".../" + filename;
    const std::size_t prefix = utf8PrefixBytes(filename,
        maximumCharacters > 7u ? maximumCharacters - 7u : 1u);
    return ".../" + filename.substr(0u, prefix) + "...";
}

inline std::string formatByteCount(uint64_t bytes)
{
    static constexpr std::array<const char*, 5u> suffixes {{
        "B", "KB", "MB", "GB", "TB",
    }};
    double value = static_cast<double>(bytes);
    std::size_t suffix = 0u;
    while (value >= 1024.0 && suffix + 1u < suffixes.size()) {
        value /= 1024.0;
        ++suffix;
    }
    char text[64] {};
    if (suffix == 0u) {
        std::snprintf(text, sizeof(text), "%llu %s",
            static_cast<unsigned long long>(bytes), suffixes[suffix]);
    } else if (value >= 100.0) {
        std::snprintf(text, sizeof(text), "%.0f %s", value,
            suffixes[suffix]);
    } else if (value >= 10.0) {
        std::snprintf(text, sizeof(text), "%.1f %s", value,
            suffixes[suffix]);
    } else {
        std::snprintf(text, sizeof(text), "%.2f %s", value,
            suffixes[suffix]);
    }
    return text;
}

class ProjectFileRegistration {
public:
    using RenameCallback = void (*)(void*, const std::string& absolutePath);

    ProjectFileRegistration() = default;
    ProjectFileRegistration(const ProjectFileRegistration&) = delete;
    ProjectFileRegistration& operator=(const ProjectFileRegistration&) = delete;
    ProjectFileRegistration(ProjectFileRegistration&&) = delete;
    ProjectFileRegistration& operator=(ProjectFileRegistration&&) = delete;

    ~ProjectFileRegistration() { clear(); }

    // REAPER's plugin_register API is a main-thread service. Call reset after a
    // loader-worker ProjectCopyResult has been published back to the main/UI
    // thread. Registration is reference counted; clear()/destruction performs
    // the exactly balanced -file_in_project_ex2 call.
    bool reset(const ReaperContext& context, const std::string& absolutePath,
        void* owner = nullptr, RenameCallback renameCallback = nullptr,
        const char* contextName = "s3g Sample")
    {
        clear();
        if (!context.canRegisterProjectFiles() || absolutePath.empty())
            return false;
        context_ = context;
        project_ = context.project;
        fxDsp_ = context.fxDsp;
        absolutePath_ = absolutePath;
        owner_ = owner;
        renameCallback_ = renameCallback;
        contextName_ = contextName ? contextName : "s3g Sample";
        void* arguments[4u] {
            const_cast<char*>(absolutePath_.c_str()),
            project_,
            this,
            reinterpret_cast<void*>(&projectFileCallback),
        };
        registered_ = context_.registerObject(
            "file_in_project_ex2", arguments) != 0;
        if (!registered_) clearFields();
        return registered_;
    }

    void clear() noexcept
    {
        if (registered_ && context_.registerObject) {
            void* arguments[4u] {
                const_cast<char*>(absolutePath_.c_str()),
                project_,
                this,
                reinterpret_cast<void*>(&projectFileCallback),
            };
            (void)context_.registerObject("-file_in_project_ex2", arguments);
        }
        clearFields();
    }

    bool registered() const noexcept { return registered_; }
    const std::string& absolutePath() const noexcept { return absolutePath_; }

private:
    static intptr_t projectFileCallback(void* userData, int message,
        void* parameter)
    {
        auto* self = static_cast<ProjectFileRegistration*>(userData);
        if (!self) return 0;
        if (message == 0 && parameter) {
            self->absolutePath_ = static_cast<const char*>(parameter);
            if (self->renameCallback_)
                self->renameCallback_(self->owner_, self->absolutePath_);
            return 0;
        }
        if (message == 0x100)
            return reinterpret_cast<intptr_t>("s3g Samples");
        if (message == 0x101)
            return reinterpret_cast<intptr_t>(self->fxDsp_);
        if (message == 0x103)
            return reinterpret_cast<intptr_t>(self->contextName_.c_str());
        return 0;
    }

    void clearFields() noexcept
    {
        registered_ = false;
        context_ = {};
        project_ = nullptr;
        fxDsp_ = nullptr;
        absolutePath_.clear();
        contextName_.clear();
        owner_ = nullptr;
        renameCallback_ = nullptr;
    }

    ReaperContext context_ {};
    void* project_ = nullptr;
    void* fxDsp_ = nullptr;
    std::string absolutePath_;
    std::string contextName_;
    void* owner_ = nullptr;
    RenameCallback renameCallback_ = nullptr;
    bool registered_ = false;
};

} // namespace s3g::sample_storage
