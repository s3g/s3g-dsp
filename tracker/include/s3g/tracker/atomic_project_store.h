#pragma once

#include "s3g/tracker/project_codec.h"

#include <string_view>

namespace s3g::tracker {

// Filesystem I/O is deliberately separate from the pure codec. Save writes a
// same-directory temporary file, fsyncs it, renames it over the destination,
// and fsyncs the parent directory. Any failure before rename leaves the old
// destination intact and removes its temporary file. A reported parent-sync
// failure occurs after complete bytes were atomically published, but means
// crash durability could not be confirmed.
ProjectResult saveProjectDocumentAtomically(
    const ProjectDocument& document, std::string_view path);
ProjectResult loadProjectDocument(std::string_view path,
    ProjectDocument& destination);

} // namespace s3g::tracker
