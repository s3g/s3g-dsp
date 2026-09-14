#pragma once
#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <string>

// Own only a newly, exclusively created test directory. Never delete a
// caller-supplied path, the system temp directory or a pre-existing folder.
class TrackerTestDirectory {
public:
    TrackerTestDirectory() {
        const auto seed = std::chrono::steady_clock::now().time_since_epoch().count();
        for (unsigned i = 0; i < 100; ++i) {
            auto candidate = std::filesystem::temp_directory_path()
                / ("s3g-tracker-tests-" + std::to_string(seed) + "-" + std::to_string(i));
            if (std::filesystem::create_directory(candidate)) {
                path = std::move(candidate);
                return;
            }
        }
        throw std::runtime_error("could not create an exclusive Tracker test directory");
    }
    TrackerTestDirectory(const TrackerTestDirectory&) = delete;
    TrackerTestDirectory& operator=(const TrackerTestDirectory&) = delete;
    ~TrackerTestDirectory() {
        std::error_code ignored;
        if (!path.empty()) std::filesystem::remove_all(path, ignored);
    }
    bool hasTemporaryFiles() const {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(path))
            if (entry.path().filename().u8string().find(".tmp.") != std::string::npos) return true;
        return false;
    }
    std::filesystem::path path;
};
