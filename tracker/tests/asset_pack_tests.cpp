#include "s3g/tracker/asset_pack.h"

#include <cassert>
#include <fstream>
#include <iostream>
#include <sstream>
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
    source.burstLibrary.bursts[7u] = burst("Shared ruff", 36u);
    auto& phrase = source.phraseLibrary.phrases[5u];
    phrase = makeBlankPhrase(5u);
    phrase.name = "Odd break turn";
    phrase.notes[1u] = NoteCell::withBurst(7u);
    phrase.notes[3u] = NoteCell::withNote(42u);

    const auto pack = makePhraseAssetPack("Break Foundations",
        source.phraseLibrary, 5u, source.burstLibrary);
    assert(pack.phraseLibrary.phrases[0u].name == "Odd break turn");
    assert(pack.phraseLibrary.phrases[0u].notes[1u].note == 0u);
    assert(pack.burstLibrary.bursts[0u].name == "Shared ruff");

    source.burstLibrary.bursts[9u] = burst("Second ornament", 42u);
    auto& secondPhrase = source.phraseLibrary.phrases[12u];
    secondPhrase = makeBlankPhrase(7u);
    secondPhrase.name = "Second phrase";
    secondPhrase.notes[2u] = NoteCell::withBurst(7u);
    secondPhrase.notes[5u] = NoteCell::withBurst(9u);
    const auto libraryPack = makePhraseLibraryAssetPack("Complete library",
        source.phraseLibrary, source.burstLibrary);
    assert(libraryPack.phraseLibrary.phrases[0u].name == "Odd break turn");
    assert(libraryPack.phraseLibrary.phrases[1u].name == "Second phrase");
    assert(libraryPack.burstLibrary.bursts[0u].name == "Shared ruff");
    assert(libraryPack.burstLibrary.bursts[1u].name == "Second ornament");
    assert(libraryPack.phraseLibrary.phrases[1u].notes[2u].note == 0u);
    assert(libraryPack.phraseLibrary.phrases[1u].notes[5u].note == 1u);
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
    assert(decoded.burstLibrary.bursts[0u].events[1u].note == 37u);
    assert(decoded.phraseLibrary.phrases[0u].notes[1u].state
        == NoteCellState::Burst);

    ProjectDocument destination;
    destination.burstLibrary.bursts[0u] = burst("Occupied", 60u);
    AssetPackImportReport report;
    assert(importTrackerAssetPack(decoded, destination, &report).ok());
    assert(report.burstsAdded == 1u && report.phrasesAdded == 1u);
    assert(destination.burstLibrary.bursts[1u].name == "Shared ruff");
    assert(destination.phraseLibrary.phrases[0u].notes[1u].note == 1u);

    report = {};
    assert(importTrackerAssetPack(decoded, destination, &report).ok());
    assert(report.burstsReused == 1u && report.phrasesReused == 1u
        && report.burstsAdded == 0u && report.phrasesAdded == 0u);

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
    for (std::size_t slot = 0u; slot < kBurstDefinitionCount; ++slot)
        full.burstLibrary.bursts[slot] = burst(
            "FULL " + std::to_string(slot), 40u);
    std::string before;
    std::string after;
    assert(encodeProjectDocument(full, before).ok());
    assert(importTrackerAssetPack(decoded, full).code
        == ProjectErrorCode::SizeLimitExceeded);
    assert(encodeProjectDocument(full, after).ok() && before == after);

    std::ifstream starter(std::string(S3G_TRACKER_SOURCE_DIR)
        + "/../examples/tracker/packs/s3g-drum-foundations-01.s3gpack",
        std::ios::binary);
    assert(starter.good());
    std::ostringstream starterBytes;
    starterBytes << starter.rdbuf();
    TrackerAssetPack starterPack;
    assert(decodeTrackerAssetPack(starterBytes.str(), starterPack).ok());
    ProjectDocument starterDestination;
    AssetPackImportReport starterReport;
    assert(importTrackerAssetPack(starterPack, starterDestination,
        &starterReport).ok());
    assert(starterReport.burstsAdded == 4u
        && starterReport.phrasesAdded == 5u);
    std::size_t polyphonicRows = 0u;
    for (const auto& starterPhrase : starterPack.phraseLibrary.phrases)
        for (const auto& cell : starterPhrase.notes)
            polyphonicRows += cell.noteVoiceCount() > 1u ? 1u : 0u;
    assert(polyphonicRows == 27u);
    return 0;
}
