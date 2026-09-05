#include "s3g/tracker/asset_pack.h"

#include <algorithm>
#include <array>
#include <utility>

namespace s3g::tracker {
namespace {

TrackerAssetPack blankPack(std::string name)
{
    TrackerAssetPack pack;
    pack.name = std::move(name);
    pack.burstBank.id = kProjectAssetBankId;
    pack.burstBank.name = pack.name.empty() ? "BURSTS" : pack.name + " BURSTS";
    pack.phraseBank.id = kProjectAssetBankId;
    pack.phraseBank.name = pack.name.empty() ? "PHRASES" : pack.name;
    pack.phraseBank.companionBurstBankId = kProjectAssetBankId;
    return pack;
}

std::size_t firstEmptyBurst(const BurstLibrary& library) noexcept
{
    const auto found = std::find_if(library.bursts.begin(),
        library.bursts.end(), [](const BurstDefinition& burst) {
            return burst.empty();
        });
    return found == library.bursts.end() ? library.bursts.size()
        : static_cast<std::size_t>(found - library.bursts.begin());
}

struct BurstMapEntry {
    AssetBankId bankId = kInvalidAssetBankId;
    uint8_t sourceSlot = 0u;
    uint8_t destinationSlot = 0u;
};

void appendPhrase(TrackerAssetPack& pack, const PhraseDefinition& source,
    const std::vector<BurstBank>& burstBanks,
    std::vector<BurstMapEntry>& burstMap,
    std::size_t destinationPhrase)
{
    PhraseDefinition phrase = source;
    for (auto& cell : phrase.notes) {
        if (cell.state != NoteCellState::Burst) continue;
        const auto* sourceBank = findBurstBank(burstBanks, cell.burstBankId);
        const auto sourceSlot = static_cast<std::size_t>(cell.note);
        if (!sourceBank || sourceSlot >= sourceBank->library.bursts.size()
            || sourceBank->library.bursts[sourceSlot].empty()) continue;
        const auto mapped = std::find_if(burstMap.begin(), burstMap.end(),
            [&](const BurstMapEntry& entry) {
                return entry.bankId == cell.burstBankId
                    && entry.sourceSlot == cell.note;
            });
        std::size_t destinationSlot = mapped == burstMap.end()
            ? kBurstDefinitionCount : mapped->destinationSlot;
        if (destinationSlot >= kBurstDefinitionCount) {
            destinationSlot = firstEmptyBurst(pack.burstBank.library);
            if (destinationSlot >= kBurstDefinitionCount) continue;
            pack.burstBank.library.bursts[destinationSlot]
                = sourceBank->library.bursts[sourceSlot];
            burstMap.push_back({ cell.burstBankId, cell.note,
                static_cast<uint8_t>(destinationSlot) });
            cell.note = static_cast<uint8_t>(destinationSlot);
        } else {
            cell.note = static_cast<uint8_t>(destinationSlot);
        }
        cell.burstBankId = pack.burstBank.id;
    }
    pack.phraseBank.library.phrases[destinationPhrase] = std::move(phrase);
}

ProjectResult packFailure(ProjectErrorCode code, std::string location,
    std::string message)
{
    return { code, std::move(location), std::move(message) };
}

} // namespace

TrackerAssetPack makeBurstAssetPack(std::string name,
    const BurstLibrary& source, std::size_t slot)
{
    auto pack = blankPack(std::move(name));
    if (slot < source.bursts.size() && !source.bursts[slot].empty())
        pack.burstBank.library.bursts[0u] = source.bursts[slot];
    return pack;
}

TrackerAssetPack makeBurstLibraryAssetPack(std::string name,
    const BurstLibrary& source)
{
    auto pack = blankPack(std::move(name));
    pack.burstBank.library = source;
    return pack;
}

TrackerAssetPack makePhraseAssetPack(std::string name,
    const PhraseLibrary& phrases, std::size_t phraseSlot,
    const BurstLibrary& bursts)
{
    BurstBank bank = makeProjectBurstBank();
    bank.library = bursts;
    return makePhraseAssetPack(std::move(name), phrases, phraseSlot,
        std::vector<BurstBank> { std::move(bank) });
}

TrackerAssetPack makePhraseAssetPack(std::string name,
    const PhraseLibrary& phrases, std::size_t phraseSlot,
    const std::vector<BurstBank>& burstBanks)
{
    auto pack = blankPack(std::move(name));
    if (phraseSlot >= phrases.phrases.size()) return pack;
    std::vector<BurstMapEntry> burstMap;
    appendPhrase(pack, phrases.phrases[phraseSlot], burstBanks,
        burstMap, 0u);
    return pack;
}

TrackerAssetPack makePhraseLibraryAssetPack(std::string name,
    const PhraseLibrary& phrases, const BurstLibrary& bursts)
{
    BurstBank bank = makeProjectBurstBank();
    bank.library = bursts;
    return makePhraseLibraryAssetPack(std::move(name), phrases,
        std::vector<BurstBank> { std::move(bank) });
}

TrackerAssetPack makePhraseLibraryAssetPack(std::string name,
    const PhraseLibrary& phrases,
    const std::vector<BurstBank>& burstBanks)
{
    auto pack = blankPack(std::move(name));
    std::vector<BurstMapEntry> burstMap;
    std::size_t destination = 0u;
    for (const auto& phrase : phrases.phrases) {
        if (phrase.empty() && phrase.name.empty()) continue;
        appendPhrase(pack, phrase, burstBanks, burstMap, destination++);
    }
    return pack;
}

TrackerAssetPack makeProjectAssetPack(std::string name,
    const PhraseLibrary& phrases, const BurstLibrary& bursts)
{
    return makePhraseLibraryAssetPack(std::move(name), phrases, bursts);
}

ProjectResult importTrackerAssetPack(const TrackerAssetPack& pack,
    ProjectDocument& destination, AssetPackImportReport* report)
{
    if (destination.burstBanks.size() >= kMaximumAssetBanks
        || destination.phraseBanks.size() >= kMaximumAssetBanks)
        return packFailure(ProjectErrorCode::SizeLimitExceeded, "$",
            "project has reached the 64-bank asset limit");

    ProjectDocument candidate = destination;
    AssetPackImportReport imported;
    const AssetBankId burstId = nextAssetBankId(candidate);
    AssetBankId phraseId = burstId + 1u;
    if (phraseId == kInvalidAssetBankId) phraseId = burstId - 1u;

    BurstBank burstBank = pack.burstBank;
    burstBank.id = burstId;
    if (burstBank.name.empty())
        burstBank.name = pack.name.empty() ? "IMPORTED BURSTS"
                                          : pack.name + " BURSTS";
    PhraseBank phraseBank = pack.phraseBank;
    phraseBank.id = phraseId;
    phraseBank.companionBurstBankId = burstId;
    if (phraseBank.name.empty())
        phraseBank.name = pack.name.empty() ? "IMPORTED PHRASES" : pack.name;

    for (auto& phrase : phraseBank.library.phrases) {
        if (phrase.empty() && phrase.name.empty()) continue;
        ++imported.phrasesAdded;
        for (auto& cell : phrase.notes) {
            if (cell.state != NoteCellState::Burst) continue;
            if (cell.note >= burstBank.library.bursts.size()
                || burstBank.library.bursts[cell.note].empty())
                return packFailure(ProjectErrorCode::InconsistentData,
                    "$.phraseBank", "Phrase references a Burst missing from the pack");
            cell.burstBankId = burstId;
        }
    }
    for (const auto& burst : burstBank.library.bursts)
        if (!burst.empty() || !burst.name.empty()) ++imported.burstsAdded;

    candidate.burstBanks.push_back(std::move(burstBank));
    candidate.phraseBanks.push_back(std::move(phraseBank));
    candidate.session.activeBurstBankId = burstId;
    candidate.session.activePhraseBankId = phraseId;
    destination = std::move(candidate);
    imported.burstBankId = burstId;
    imported.phraseBankId = phraseId;
    if (report) *report = imported;
    return {};
}

} // namespace s3g::tracker
