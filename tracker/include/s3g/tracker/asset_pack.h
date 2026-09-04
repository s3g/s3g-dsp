#pragma once

#include "s3g/tracker/project_codec.h"

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

namespace s3g::tracker {

constexpr uint32_t kTrackerAssetPackVersion = 1u;
constexpr const char* kTrackerAssetPackFormat = "s3g-tracker-asset-pack";
constexpr const char* kTrackerAssetPackExtension = ".s3gpack";

// Pack slots are local to the file. Import always remaps them to the
// destination project's shared libraries, so a pack never depends on which
// Bxx/Pxx slots happen to be free in another composition.
struct TrackerAssetPack {
    std::string name;
    BurstLibrary burstLibrary;
    PhraseLibrary phraseLibrary;
};

struct AssetPackImportReport {
    std::size_t burstsAdded = 0u;
    std::size_t burstsReused = 0u;
    std::size_t phrasesAdded = 0u;
    std::size_t phrasesReused = 0u;
};

TrackerAssetPack makeBurstAssetPack(std::string name,
    const BurstLibrary& source, std::size_t slot);
TrackerAssetPack makePhraseAssetPack(std::string name,
    const PhraseLibrary& phrases, std::size_t phraseSlot,
    const BurstLibrary& bursts);
TrackerAssetPack makePhraseLibraryAssetPack(std::string name,
    const PhraseLibrary& phrases, const BurstLibrary& bursts);
TrackerAssetPack makeProjectAssetPack(std::string name,
    const PhraseLibrary& phrases, const BurstLibrary& bursts);

// The destination is replaced only after every dependency has been resolved
// and sufficient Burst/Phrase capacity has been proven.
ProjectResult importTrackerAssetPack(const TrackerAssetPack& pack,
    ProjectDocument& destination, AssetPackImportReport* report = nullptr);

ProjectResult encodeTrackerAssetPack(const TrackerAssetPack& pack,
    std::string& destination);
ProjectResult decodeTrackerAssetPack(std::string_view source,
    TrackerAssetPack& destination);

} // namespace s3g::tracker
