#pragma once

#include "s3g/tracker/project_codec.h"
#include "s3g/tracker/asset_pack.h"

#include <string_view>

namespace s3g::tracker {

// Filesystem I/O is deliberately separate from the pure codec. Save writes a
// same-directory temporary file and publishes only complete, flushed bytes.
// POSIX: fsync(file), rename, fsync(parent). Windows: strict UTF-8 -> UTF-16,
// FlushFileBuffers, same-volume MoveFileExW(REPLACE_EXISTING | WRITE_THROUGH).
// No delete-destination or cross-volume copy fallback. Failures before publish
// leave the old destination intact and clean up only our own temporary file.
// A POSIX parent-sync failure is reported after publication. Crash durability
// ultimately depends on the filesystem/device; no network-share guarantee.
// All paths are UTF-8; I/O and codecs are for non-realtime threads only.
ProjectResult saveProjectDocumentAtomically(
    const ProjectDocument& document, std::string_view path);
ProjectResult loadProjectDocument(std::string_view path,
    ProjectDocument& destination);
ProjectResult saveTrackerAssetPackAtomically(
    const TrackerAssetPack& pack, std::string_view path);
ProjectResult loadTrackerAssetPack(std::string_view path,
    TrackerAssetPack& destination);

} // namespace s3g::tracker
