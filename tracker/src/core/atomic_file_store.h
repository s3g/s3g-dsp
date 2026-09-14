#pragma once
#include "s3g/tracker/project_codec.h"
namespace s3g::tracker::file_store {
// Internal, bounded byte I/O shared by the project and asset-pack codecs.
// Call only from a non-realtime thread. Destinations change only on success.
ProjectResult saveBytesAtomically(std::string_view bytes, std::string_view path);
ProjectResult loadBytes(std::string_view path, std::string& destination);
} // namespace s3g::tracker::file_store
