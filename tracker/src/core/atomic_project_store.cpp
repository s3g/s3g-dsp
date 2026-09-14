#include "s3g/tracker/atomic_project_store.h"
#include "atomic_file_store.h"

namespace s3g::tracker {
ProjectResult saveProjectDocumentAtomically(
    const ProjectDocument& document, std::string_view path)
{
    std::string bytes;
    auto result = encodeProjectDocument(document, bytes);
    return result ? file_store::saveBytesAtomically(bytes, path) : result;
}
ProjectResult loadProjectDocument(std::string_view path, ProjectDocument& destination)
{
    std::string bytes;
    auto result = file_store::loadBytes(path, bytes);
    return result ? decodeProjectDocument(bytes, destination) : result;
}
ProjectResult saveTrackerAssetPackAtomically(
    const TrackerAssetPack& pack, std::string_view path)
{
    std::string bytes;
    auto result = encodeTrackerAssetPack(pack, bytes);
    return result ? file_store::saveBytesAtomically(bytes, path) : result;
}
ProjectResult loadTrackerAssetPack(std::string_view path, TrackerAssetPack& destination)
{
    std::string bytes;
    auto result = file_store::loadBytes(path, bytes);
    return result ? decodeTrackerAssetPack(bytes, destination) : result;
}
} // namespace s3g::tracker
