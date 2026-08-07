#pragma once

#include "s3g/tracker/project_document.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace s3g::tracker {

constexpr std::size_t kMaximumProjectDocumentBytes = 64u * 1024u * 1024u;

enum class ProjectErrorCode : uint8_t {
    None,
    InvalidArgument,
    InvalidJson,
    UnsupportedSchemaVersion,
    MissingField,
    TypeMismatch,
    OutOfRange,
    InconsistentData,
    SizeLimitExceeded,
    IoOpenFailed,
    IoReadFailed,
    IoWriteFailed,
    IoSyncFailed,
    IoRenameFailed,
};

struct ProjectResult {
    ProjectErrorCode code = ProjectErrorCode::None;
    // JSON path (codec errors) or filesystem path (storage errors).
    std::string location;
    std::string message;

    constexpr bool ok() const noexcept
    {
        return code == ProjectErrorCode::None;
    }

    constexpr explicit operator bool() const noexcept { return ok(); }
};

// Pure, deterministic UTF-8 JSON codec. Encoding validates before publishing
// output; decoding builds a candidate and only replaces destination after the
// complete document passes validation. Unknown object fields are ignored at
// every level because JSON values are length-delimited by the parser. Unknown
// enum values, incompatible schema versions, duplicate keys, and malformed
// known fields fail closed.
ProjectResult encodeProjectDocument(const ProjectDocument& document,
    std::string& destination);
ProjectResult decodeProjectDocument(std::string_view source,
    ProjectDocument& destination);

} // namespace s3g::tracker
