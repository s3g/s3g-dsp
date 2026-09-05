#include "s3g/tracker/asset_pack.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <iostream>
#include <string>

using namespace s3g::tracker;

namespace {

BurstDefinition burst(std::string name, uint8_t note)
{
    BurstDefinition value;
    value.name = std::move(name);
    value.eventCount = 2u;
    value.events[0u] = { 0u, note, 120u, 40u };
    value.events[1u] = { 32768u, static_cast<uint8_t>(note + 1u), 90u, 35u };
    return value;
}

} // namespace

int main()
{
    ProjectDocument source;
    auto& sourceBursts = source.burstBanks[0u].library;
    auto& sourcePhrases = source.phraseBanks[0u].library;
    sourceBursts.bursts[7u] = burst("Shared ruff", 36u);
    auto& phrase = sourcePhrases.phrases[5u];
    phrase = makeBlankPhrase(5u);
    phrase.name = "Odd break turn";
    phrase.notes[1u] = NoteCell::withBurst(7u);
    phrase.notes[3u] = NoteCell::withNote(42u);

    const auto pack = makePhraseAssetPack("Break Foundations",
        sourcePhrases, 5u, sourceBursts);
    assert(pack.phraseBank.library.phrases[0u].name == "Odd break turn");
    assert(pack.phraseBank.library.phrases[0u].notes[1u].note == 0u);
    assert(pack.burstBank.library.bursts[0u].name == "Shared ruff");

    sourceBursts.bursts[9u] = burst("Second ornament", 42u);
    auto& secondPhrase = sourcePhrases.phrases[12u];
    secondPhrase = makeBlankPhrase(7u);
    secondPhrase.name = "Second phrase";
    secondPhrase.notes[2u] = NoteCell::withBurst(7u);
    secondPhrase.notes[5u] = NoteCell::withBurst(9u);
    const auto libraryPack = makePhraseLibraryAssetPack("Complete library",
        sourcePhrases, sourceBursts);
    assert(libraryPack.phraseBank.library.phrases[0u].name == "Odd break turn");
    assert(libraryPack.phraseBank.library.phrases[1u].name == "Second phrase");
    assert(libraryPack.burstBank.library.bursts[0u].name == "Shared ruff");
    assert(libraryPack.burstBank.library.bursts[1u].name == "Second ornament");
    assert(libraryPack.phraseBank.library.phrases[1u].notes[2u].note == 0u);
    assert(libraryPack.phraseBank.library.phrases[1u].notes[5u].note == 1u);
    std::string libraryEncoded;
    assert(encodeTrackerAssetPack(libraryPack, libraryEncoded).ok());
    TrackerAssetPack libraryDecoded;
    assert(decodeTrackerAssetPack(libraryEncoded, libraryDecoded).ok());
    ProjectDocument libraryDestination;
    AssetPackImportReport libraryReport;
    assert(importTrackerAssetPack(libraryDecoded, libraryDestination,
        &libraryReport).ok());
    assert(libraryReport.phrasesAdded == 2u
        && libraryReport.burstsAdded == 2u);

    BurstBank importedBursts;
    importedBursts.id = 9u;
    importedBursts.name = "IMPORTED BREAKS";
    importedBursts.library.bursts[4u] = burst("Foreign drag", 46u);
    source.burstBanks[0u].library = sourceBursts;
    source.burstBanks.push_back(importedBursts);
    auto crossBankPhrases = sourcePhrases;
    crossBankPhrases.phrases[5u].notes[4u] = NoteCell::withBurst(4u, 9u);
    const auto crossBankPack = makePhraseAssetPack("Cross-bank phrase",
        crossBankPhrases, 5u, source.burstBanks);
    const auto& packedPhrase = crossBankPack.phraseBank.library.phrases[0u];
    assert(crossBankPack.burstBank.library.bursts[0u].name == "Shared ruff");
    assert(crossBankPack.burstBank.library.bursts[1u].name == "Foreign drag");
    assert(packedPhrase.notes[1u].note == 0u
        && packedPhrase.notes[4u].note == 1u);
    assert(packedPhrase.notes[1u].burstBankId == kProjectAssetBankId
        && packedPhrase.notes[4u].burstBankId == kProjectAssetBankId);
    std::string crossBankEncoded;
    assert(encodeTrackerAssetPack(crossBankPack, crossBankEncoded).ok());
    TrackerAssetPack crossBankDecoded;
    assert(decodeTrackerAssetPack(crossBankEncoded, crossBankDecoded).ok());
    ProjectDocument crossBankDestination;
    assert(importTrackerAssetPack(crossBankDecoded,
        crossBankDestination).ok());
    const auto importedBankId = crossBankDestination.burstBanks[1u].id;
    const auto& importedPhrase =
        crossBankDestination.phraseBanks[1u].library.phrases[0u];
    assert(importedPhrase.notes[1u].burstBankId == importedBankId
        && importedPhrase.notes[4u].burstBankId == importedBankId);

    const auto burstBankPack = makeBurstLibraryAssetPack(
        "Imported break collection", importedBursts.library);
    assert(burstBankPack.burstBank.library.bursts[4u].name
        == "Foreign drag");
    assert(burstBankPack.phraseBank.library.phrases[0u].empty());

    std::string encoded;
    const auto packResult = encodeTrackerAssetPack(pack, encoded);
    if (!packResult.ok()) {
        std::cerr << packResult.location << ": " << packResult.message << '\n';
        return 1;
    }
    assert(encoded.find("s3g-tracker-asset-pack") != std::string::npos);
    assert(encoded.find("burst-b01") != std::string::npos);
    assert(encoded.find("burstDependencies") != std::string::npos);

    TrackerAssetPack decoded;
    assert(decodeTrackerAssetPack(encoded, decoded).ok());
    assert(decoded.burstBank.library.bursts[0u].events[1u].note == 37u);
    assert(decoded.phraseBank.library.phrases[0u].notes[1u].state
        == NoteCellState::Burst);

    ProjectDocument destination;
    destination.burstBanks[0u].library.bursts[0u] = burst("Occupied", 60u);
    AssetPackImportReport report;
    assert(importTrackerAssetPack(decoded, destination, &report).ok());
    assert(report.burstsAdded == 1u && report.phrasesAdded == 1u);
    assert(destination.burstBanks.size() == 2u
        && destination.phraseBanks.size() == 2u);
    assert(destination.burstBanks[1u].library.bursts[0u].name == "Shared ruff");
    assert(destination.phraseBanks[1u].library.phrases[0u].notes[1u].note == 0u);
    assert(destination.phraseBanks[1u].library.phrases[0u].notes[1u].burstBankId
        == destination.burstBanks[1u].id);

    report = {};
    assert(importTrackerAssetPack(decoded, destination, &report).ok());
    assert(report.burstsAdded == 1u && report.phrasesAdded == 1u
        && destination.burstBanks.size() == 3u
        && destination.phraseBanks.size() == 3u);

    std::string malformed = encoded;
    const auto dependency = malformed.find("burst-b01",
        malformed.find("burstDependencies"));
    if (dependency == std::string::npos) {
        std::cerr << "encoded dependency ID is missing\n";
        return 1;
    }
    malformed.replace(dependency, std::string("burst-b01").size(),
        "burst-missing");
    TrackerAssetPack rejected;
    assert(decodeTrackerAssetPack(malformed, rejected).code
        == ProjectErrorCode::InconsistentData);

    ProjectDocument full;
    for (std::size_t bank = 1u; bank < kMaximumAssetBanks; ++bank) {
        BurstBank extra = makeProjectBurstBank();
        extra.id = static_cast<AssetBankId>(bank + 1u);
        extra.name = "FULL " + std::to_string(bank);
        full.burstBanks.push_back(std::move(extra));
    }
    std::string before;
    std::string after;
    assert(encodeProjectDocument(full, before).ok());
    assert(importTrackerAssetPack(decoded, full).code
        == ProjectErrorCode::SizeLimitExceeded);
    assert(encodeProjectDocument(full, after).ok() && before == after);

    return 0;
}
