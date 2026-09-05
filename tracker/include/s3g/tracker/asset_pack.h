#pragma once

#include "s3g/tracker/project_codec.h"

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

namespace s3g::tracker {

constexpr uint32_t kTrackerAssetPackVersion = 2u;
constexpr const char* kTrackerAssetPackFormat = "s3g-tracker-asset-pack";
constexpr const char* kTrackerAssetPackExtension = ".s3gpack";

// Pack slots are local to the file. Import creates a new named Phrase bank and
// companion Burst bank with fresh stable project IDs, so loading another pack
// never consumes or renumbers an existing bank's Bxx/Pxx slots.
struct TrackerAssetPack {
    std::string name;
    BurstBank burstBank;
    PhraseBank phraseBank;
};

struct AssetPackImportReport {
    std::size_t burstsAdded = 0u;
    std::size_t burstsReused = 0u;
    std::size_t phrasesAdded = 0u;
    std::size_t phrasesReused = 0u;
    AssetBankId burstBankId = kInvalidAssetBankId;
    AssetBankId phraseBankId = kInvalidAssetBankId;
};

TrackerAssetPack makeBurstAssetPack(std::string name,
    const BurstLibrary& source, std::size_t slot);
TrackerAssetPack makeBurstLibraryAssetPack(std::string name,
    const BurstLibrary& source);
TrackerAssetPack makePhraseAssetPack(std::string name,
    const PhraseLibrary& phrases, std::size_t phraseSlot,
    const BurstLibrary& bursts);
TrackerAssetPack makePhraseAssetPack(std::string name,
    const PhraseLibrary& phrases, std::size_t phraseSlot,
    const std::vector<BurstBank>& burstBanks);
TrackerAssetPack makePhraseLibraryAssetPack(std::string name,
    const PhraseLibrary& phrases, const BurstLibrary& bursts);
TrackerAssetPack makePhraseLibraryAssetPack(std::string name,
    const PhraseLibrary& phrases,
    const std::vector<BurstBank>& burstBanks);
TrackerAssetPack makeProjectAssetPack(std::string name,
    const PhraseLibrary& phrases, const BurstLibrary& bursts);

// The destination is replaced only after every dependency has been resolved
// and the bounded project bank capacity has been proven.
ProjectResult importTrackerAssetPack(const TrackerAssetPack& pack,
    ProjectDocument& destination, AssetPackImportReport* report = nullptr);

ProjectResult encodeTrackerAssetPack(const TrackerAssetPack& pack,
    std::string& destination);
ProjectResult decodeTrackerAssetPack(std::string_view source,
    TrackerAssetPack& destination);

} // namespace s3g::tracker
