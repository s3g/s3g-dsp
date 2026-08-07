#include "s3g/tracker/atomic_project_store.h"

#include <array>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

namespace s3g::tracker {
namespace {

ProjectResult ioFailure(ProjectErrorCode code, std::string_view path,
    std::string_view operation, int errorNumber)
{
    std::string message(operation);
    message += ": ";
    message += std::strerror(errorNumber);
    return { code, std::string(path), std::move(message) };
}

bool validPath(std::string_view path) noexcept
{
    return !path.empty() && path.size() < static_cast<std::size_t>(PATH_MAX)
        && path.find('\0') == std::string_view::npos && path.back() != '/';
}

std::string parentDirectory(std::string_view path)
{
    const auto separator = path.find_last_of('/');
    if (separator == std::string_view::npos) return ".";
    if (separator == 0u) return "/";
    return std::string(path.substr(0u, separator));
}

class TemporaryFile {
public:
    TemporaryFile() = default;
    TemporaryFile(const TemporaryFile&) = delete;
    TemporaryFile& operator=(const TemporaryFile&) = delete;

    ~TemporaryFile()
    {
        if (descriptor_ >= 0) ::close(descriptor_);
        if (!path_.empty()) ::unlink(path_.c_str());
    }

    int descriptor() const noexcept { return descriptor_; }
    const std::string& path() const noexcept { return path_; }

    bool create(std::string_view target)
    {
        std::string pattern(target);
        pattern += ".tmp.XXXXXX";
        std::vector<char> mutablePattern(pattern.begin(), pattern.end());
        mutablePattern.push_back('\0');
        descriptor_ = ::mkstemp(mutablePattern.data());
        if (descriptor_ < 0) return false;
        path_ = mutablePattern.data();
        const int flags = ::fcntl(descriptor_, F_GETFD);
        if (flags >= 0) (void)::fcntl(descriptor_, F_SETFD, flags | FD_CLOEXEC);
        return true;
    }

    bool closeFile() noexcept
    {
        if (descriptor_ < 0) return true;
        const int descriptor = descriptor_;
        descriptor_ = -1;
        return ::close(descriptor) == 0;
    }

    void published() noexcept { path_.clear(); }

private:
    int descriptor_ = -1;
    std::string path_;
};

bool writeAll(int descriptor, std::string_view bytes, int& errorNumber)
{
    std::size_t offset = 0u;
    while (offset < bytes.size()) {
        const auto remaining = bytes.size() - offset;
        const auto count = ::write(descriptor, bytes.data() + offset,
            remaining);
        if (count < 0) {
            if (errno == EINTR) continue;
            errorNumber = errno;
            return false;
        }
        if (count == 0) {
            errorNumber = EIO;
            return false;
        }
        offset += static_cast<std::size_t>(count);
    }
    return true;
}

ProjectResult syncParent(std::string_view path)
{
    const std::string parent = parentDirectory(path);
#ifdef O_DIRECTORY
    const int descriptor = ::open(parent.c_str(), O_RDONLY | O_DIRECTORY);
#else
    const int descriptor = ::open(parent.c_str(), O_RDONLY);
#endif
    if (descriptor < 0)
        return ioFailure(ProjectErrorCode::IoSyncFailed, parent,
            "open parent directory for sync", errno);
    int syncError = 0;
    while (::fsync(descriptor) != 0) {
        if (errno == EINTR) continue;
        syncError = errno;
        break;
    }
    int closeError = 0;
    if (::close(descriptor) != 0) closeError = errno;
    if (syncError != 0)
        return ioFailure(ProjectErrorCode::IoSyncFailed, parent,
            "sync parent directory", syncError);
    if (closeError != 0)
        return ioFailure(ProjectErrorCode::IoSyncFailed, parent,
            "close parent directory", closeError);
    return {};
}

} // namespace

ProjectResult saveProjectDocumentAtomically(
    const ProjectDocument& document, std::string_view path)
{
    if (!validPath(path))
        return { ProjectErrorCode::InvalidArgument, std::string(path),
            "project path is empty, too long, contains NUL, or names a directory" };

    std::string encoded;
    ProjectResult result = encodeProjectDocument(document, encoded);
    if (!result) return result;

    const std::string target(path);
    TemporaryFile temporary;
    if (!temporary.create(target))
        return ioFailure(ProjectErrorCode::IoOpenFailed, target,
            "create temporary project file", errno);

    // Preserve existing user-visible permission bits when replacing a regular
    // file. A new project remains private (mkstemp creates mode 0600).
    struct stat existing {};
    if (::stat(target.c_str(), &existing) == 0 && S_ISREG(existing.st_mode))
        (void)::fchmod(temporary.descriptor(), existing.st_mode & 0777);

    int ioError = 0;
    if (!writeAll(temporary.descriptor(), encoded, ioError))
        return ioFailure(ProjectErrorCode::IoWriteFailed, target,
            "write temporary project file", ioError);
    while (::fsync(temporary.descriptor()) != 0) {
        if (errno == EINTR) continue;
        return ioFailure(ProjectErrorCode::IoSyncFailed, target,
            "sync temporary project file", errno);
    }
    if (!temporary.closeFile())
        return ioFailure(ProjectErrorCode::IoWriteFailed, target,
            "close temporary project file", errno);
    if (::rename(temporary.path().c_str(), target.c_str()) != 0)
        return ioFailure(ProjectErrorCode::IoRenameFailed, target,
            "atomically publish project file", errno);
    temporary.published();

    // The rename has already atomically published complete bytes. Report a
    // directory-sync failure because crash durability is not guaranteed, but
    // never delete or rewrite the newly published destination here.
    return syncParent(target);
}

ProjectResult loadProjectDocument(std::string_view path,
    ProjectDocument& destination)
{
    if (!validPath(path))
        return { ProjectErrorCode::InvalidArgument, std::string(path),
            "project path is empty, too long, contains NUL, or names a directory" };
    const std::string sourcePath(path);
    const int descriptor = ::open(sourcePath.c_str(), O_RDONLY);
    if (descriptor < 0)
        return ioFailure(ProjectErrorCode::IoOpenFailed, sourcePath,
            "open project file", errno);

    std::string bytes;
    bytes.reserve(16384u);
    std::array<char, 16384u> buffer {};
    ProjectResult result;
    for (;;) {
        const auto count = ::read(descriptor, buffer.data(), buffer.size());
        if (count < 0) {
            if (errno == EINTR) continue;
            result = ioFailure(ProjectErrorCode::IoReadFailed, sourcePath,
                "read project file", errno);
            break;
        }
        if (count == 0) break;
        const auto countSize = static_cast<std::size_t>(count);
        if (countSize > kMaximumProjectDocumentBytes - bytes.size()) {
            result = { ProjectErrorCode::SizeLimitExceeded, sourcePath,
                "project exceeds the 64 MiB file limit" };
            break;
        }
        bytes.append(buffer.data(), countSize);
    }
    const int closeResult = ::close(descriptor);
    if (!result) return result;
    if (closeResult != 0)
        return ioFailure(ProjectErrorCode::IoReadFailed, sourcePath,
            "close project file", errno);
    return decodeProjectDocument(bytes, destination);
}

} // namespace s3g::tracker
