#include "s3g/tracker/atomic_project_store.h"
#include "test_directory.h"
#include <fstream>
#include <iostream>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
using namespace s3g::tracker;
namespace fs = std::filesystem;
int main()
{
    int checks = 0, failures = 0;
    auto check = [&](bool ok, const char* label) {
        ++checks;
        if (!ok) { ++failures; std::cerr << label << '\n'; }
    };
    TrackerTestDirectory directory;
    const auto folder = directory.path / fs::u8path(u8"项目 écho 🎛");
    fs::create_directory(folder);
    const auto projectPath = folder / fs::u8path(u8"楽曲 × test.s3gt");
    ProjectDocument project;
    project.patternBank.activePattern()->name = u8"初期 × 🎵";
    check(saveProjectDocumentAtomically(project, projectPath.u8string()).ok(), "Unicode save");
    ProjectDocument loaded;
    check(loadProjectDocument(projectPath.u8string(), loaded).ok()
            && loaded.patternBank.activePattern()->name == project.patternBank.activePattern()->name,
        "Unicode project round trip");
    project.patternBank.activePattern()->name = "Replacement";
    check(saveProjectDocumentAtomically(project, projectPath.u8string()).ok()
            && loadProjectDocument(projectPath.u8string(), loaded).ok()
            && loaded.patternBank.activePattern()->name == "Replacement", "replace existing file");
    auto invalidDocument = project;
    invalidDocument.patternBank.activePatternId = "missing-pattern";
    check(!saveProjectDocumentAtomically(invalidDocument, projectPath.u8string())
            && loadProjectDocument(projectPath.u8string(), loaded).ok()
            && loaded.patternBank.activePattern()->name == "Replacement",
        "encoding failure preserves the previous complete file");

    auto pack = makeBurstLibraryAssetPack(u8"素材 × pack", project.burstBanks.front().library);
    const auto packPath = folder / fs::u8path(u8"素材 🎵.s3gpack");
    TrackerAssetPack restored;
    check(saveTrackerAssetPackAtomically(pack, packPath.u8string()).ok()
            && loadTrackerAssetPack(packPath.u8string(), restored).ok()
            && restored.name == pack.name, "Unicode pack round trip");
    pack.name = "Second pack";
    check(saveTrackerAssetPackAtomically(pack, packPath.u8string()).ok()
            && loadTrackerAssetPack(packPath.u8string(), restored).ok()
            && restored.name == pack.name, "pack replacement");

    const auto blocked = folder / "directory.s3gt";
    fs::create_directory(blocked);
    std::ofstream(blocked / "keep.txt") << "do not change";
    check(saveProjectDocumentAtomically(project, blocked.u8string()).code
            == ProjectErrorCode::IoRenameFailed && fs::exists(blocked / "keep.txt"),
        "failed publication preserves destination directory");
    check(!directory.hasTemporaryFiles(), "failed/successful publication leaves no owned temp");
    for (const auto& path : {std::string {}, std::string("bad\0path", 8), folder.u8string() + "/"})
        check(saveProjectDocumentAtomically(project, path).code == ProjectErrorCode::InvalidArgument,
            "reject invalid path");
    check(loadProjectDocument((folder / "missing").u8string(), loaded).code
            == ProjectErrorCode::IoOpenFailed && loaded.patternBank.activePattern()->name == "Replacement",
        "missing file preserves destination model");
    const auto malformed = folder / "malformed.s3gt";
    std::ofstream(malformed) << "not JSON";
    check(!loadProjectDocument(malformed.u8string(), loaded)
            && loaded.patternBank.activePattern()->name == "Replacement", "malformed project is transactional");
    check(!loadTrackerAssetPack(malformed.u8string(), restored) && restored.name == "Second pack",
        "malformed pack is transactional");
    const auto oversized = folder / "oversized.s3gt";
    { std::ofstream file(oversized, std::ios::binary); file.seekp(kMaximumProjectDocumentBytes); file.put('x'); }
    check(loadProjectDocument(oversized.u8string(), loaded).code == ProjectErrorCode::SizeLimitExceeded
            && loaded.patternBank.activePattern()->name == "Replacement", "bounded project read");
    check(loadTrackerAssetPack(oversized.u8string(), restored).code == ProjectErrorCode::SizeLimitExceeded
            && restored.name == "Second pack", "bounded pack read");

#if defined(_WIN32)
    check(loadProjectDocument(folder.u8string() + "/\xff.s3gt", loaded).code
            == ProjectErrorCode::InvalidArgument, "invalid UTF-8 rejected without ANSI fallback");
    check(saveProjectDocumentAtomically(project, projectPath.u8string() + ":stream").code
            == ProjectErrorCode::InvalidArgument, "alternate data stream rejected");
    HANDLE locked = CreateFileW(projectPath.c_str(), GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    check(locked != INVALID_HANDLE_VALUE, "open deny-delete destination");
    project.patternBank.activePattern()->name = "Must not replace locked file";
    check(saveProjectDocumentAtomically(project, projectPath.u8string()).code
            == ProjectErrorCode::IoRenameFailed, "sharing violation fails without delete fallback");
    if (locked != INVALID_HANDLE_VALUE) CloseHandle(locked);
    check(loadProjectDocument(projectPath.u8string(), loaded).ok()
            && loaded.patternBank.activePattern()->name == "Replacement"
            && !directory.hasTemporaryFiles(), "locked target and temp cleanup preserved");
    check(SetFileAttributesW(projectPath.c_str(), FILE_ATTRIBUTE_READONLY) != 0,
        "mark read-only destination");
    const auto readOnlyResult = saveProjectDocumentAtomically(project, projectPath.u8string());
    SetFileAttributesW(projectPath.c_str(), FILE_ATTRIBUTE_NORMAL);
    check(!readOnlyResult && loadProjectDocument(projectPath.u8string(), loaded).ok()
            && loaded.patternBank.activePattern()->name == "Replacement"
            && !directory.hasTemporaryFiles(), "read-only save preserves old file and cleans temporary");
    auto longFolder = fs::path(L"\\\\?\\" + directory.path.wstring());
    for (int i = 0; i < 8; ++i) {
        longFolder /= fs::path(std::wstring(40, L'x'));
        fs::create_directory(longFolder);
    }
    const auto longPath = longFolder / fs::u8path(u8"長い名前.s3gt");
    check(saveProjectDocumentAtomically(project, longPath.u8string()).ok()
            && loadProjectDocument(longPath.u8string(), loaded).ok(), "extended-length Unicode path");
    // Remove the extended tree through its extended path, before the owner's
    // normal-path cleanup on Windows configurations without long-path opt-in.
    for (int i = 0; i < 8; ++i) longFolder = longFolder.parent_path();
    fs::remove_all(longFolder / fs::path(std::wstring(40, L'x')));
#endif
    check(!directory.hasTemporaryFiles(), "all storage operations clean up owned temporaries");
    std::cout << checks << " file-store checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
