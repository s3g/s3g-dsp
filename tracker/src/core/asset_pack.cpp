#include "s3g/tracker/asset_pack.h"

#include <algorithm>
#include <array>
#include <utility>

namespace s3g::tracker {
namespace {

bool burstEqual(const BurstDefinition& left,
    const BurstDefinition& right) noexcept
{
    if (left.name != right.name || left.eventCount != right.eventCount)
        return false;
    for (std::size_t event = 0u; event < left.eventCount; ++event) {
        const auto& a = left.events[event];
        const auto& b = right.events[event];
        if (a.position != b.position || a.note != b.note
            || a.velocity != b.velocity
            || a.gatePercent != b.gatePercent) return false;
    }
    return true;
}

bool phraseEqual(const PhraseDefinition& left,
    const PhraseDefinition& right)
{
    if (left.name != right.name || left.length != right.length
        || left.previewMidiChannel != right.previewMidiChannel
        || left.notes.size() != right.notes.size()
        || left.velocities.size() != right.velocities.size()
        || left.gates.size() != right.gates.size()) return false;
    // Phrase cell types are trivially comparable in meaning but do not all
    // provide operator==. The deterministic project codec is a convenient
    // canonical equality boundary for the small fixed-capacity library.
    PhraseLibrary a;
    PhraseLibrary b;
    a.phrases[0u] = left;
    b.phrases[0u] = right;
    ProjectDocument da;
    ProjectDocument db;
    da.phraseLibrary = std::move(a);
    db.phraseLibrary = std::move(b);
    // Referenced Burst slots are validated by the caller, so definitions are
    // filled with a harmless recipe solely to make canonical encoding legal.
    for (std::size_t slot = 0u; slot < kBurstDefinitionCount; ++slot) {
        bool used = false;
        for (const auto& cell : left.notes)
            used |= cell.state == NoteCellState::Burst && cell.note == slot;
        for (const auto& cell : right.notes)
            used |= cell.state == NoteCellState::Burst && cell.note == slot;
        if (!used) continue;
        BurstDefinition marker;
        marker.name = "COMPARE";
        marker.eventCount = 1u;
        marker.events[0u] = { 0u, 60u, 100u, 50u };
        da.burstLibrary.bursts[slot] = marker;
        db.burstLibrary.bursts[slot] = marker;
    }
    std::string ea;
    std::string eb;
    return encodeProjectDocument(da, ea).ok()
        && encodeProjectDocument(db, eb).ok() && ea == eb;
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

std::size_t firstEmptyPhrase(const PhraseLibrary& library) noexcept
{
    const auto found = std::find_if(library.phrases.begin(),
        library.phrases.end(), [](const PhraseDefinition& phrase) {
            return phrase.empty() && phrase.name.empty();
        });
    return found == library.phrases.end() ? library.phrases.size()
        : static_cast<std::size_t>(found - library.phrases.begin());
}

void appendPhrase(TrackerAssetPack& pack, const PhraseDefinition& source,
    const BurstLibrary& bursts,
    std::array<std::size_t, kBurstDefinitionCount>& burstMap,
    std::size_t destinationPhrase)
{
    PhraseDefinition phrase = source;
    for (auto& cell : phrase.notes) {
        if (cell.state != NoteCellState::Burst) continue;
        const auto sourceSlot = static_cast<std::size_t>(cell.note);
        if (sourceSlot >= bursts.bursts.size()
            || bursts.bursts[sourceSlot].empty()) continue;
        if (burstMap[sourceSlot] >= kBurstDefinitionCount) {
            const auto destinationSlot = firstEmptyBurst(pack.burstLibrary);
            if (destinationSlot >= kBurstDefinitionCount) continue;
            pack.burstLibrary.bursts[destinationSlot]
                = bursts.bursts[sourceSlot];
            burstMap[sourceSlot] = destinationSlot;
        }
        cell.note = static_cast<uint8_t>(burstMap[sourceSlot]);
    }
    pack.phraseLibrary.phrases[destinationPhrase] = std::move(phrase);
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
    TrackerAssetPack pack;
    pack.name = std::move(name);
    if (slot < source.bursts.size() && !source.bursts[slot].empty())
        pack.burstLibrary.bursts[0u] = source.bursts[slot];
    return pack;
}

TrackerAssetPack makePhraseAssetPack(std::string name,
    const PhraseLibrary& phrases, std::size_t phraseSlot,
    const BurstLibrary& bursts)
{
    TrackerAssetPack pack;
    pack.name = std::move(name);
    if (phraseSlot >= phrases.phrases.size()) return pack;
    std::array<std::size_t, kBurstDefinitionCount> burstMap;
    burstMap.fill(kBurstDefinitionCount);
    appendPhrase(pack, phrases.phrases[phraseSlot], bursts, burstMap, 0u);
    return pack;
}

TrackerAssetPack makePhraseLibraryAssetPack(std::string name,
    const PhraseLibrary& phrases, const BurstLibrary& bursts)
{
    TrackerAssetPack pack;
    pack.name = std::move(name);
    std::array<std::size_t, kBurstDefinitionCount> burstMap;
    burstMap.fill(kBurstDefinitionCount);
    std::size_t destination = 0u;
    for (const auto& phrase : phrases.phrases) {
        if (phrase.empty() && phrase.name.empty()) continue;
        appendPhrase(pack, phrase, bursts, burstMap, destination++);
    }
    return pack;
}

TrackerAssetPack makeProjectAssetPack(std::string name,
    const PhraseLibrary& phrases, const BurstLibrary& bursts)
{
    TrackerAssetPack pack;
    pack.name = std::move(name);
    std::array<std::size_t, kBurstDefinitionCount> burstMap;
    burstMap.fill(kBurstDefinitionCount);
    for (std::size_t slot = 0u; slot < bursts.bursts.size(); ++slot) {
        if (bursts.bursts[slot].empty()) continue;
        const auto destination = firstEmptyBurst(pack.burstLibrary);
        if (destination >= kBurstDefinitionCount) break;
        pack.burstLibrary.bursts[destination] = bursts.bursts[slot];
        burstMap[slot] = destination;
    }
    std::size_t phraseDestination = 0u;
    for (const auto& phrase : phrases.phrases) {
        if (phrase.empty() && phrase.name.empty()) continue;
        appendPhrase(pack, phrase, bursts, burstMap, phraseDestination++);
    }
    return pack;
}

ProjectResult importTrackerAssetPack(const TrackerAssetPack& pack,
    ProjectDocument& destination, AssetPackImportReport* report)
{
    ProjectDocument candidate = destination;
    AssetPackImportReport imported;
    std::array<std::size_t, kBurstDefinitionCount> burstMap;
    burstMap.fill(kBurstDefinitionCount);
    for (std::size_t source = 0u;
         source < pack.burstLibrary.bursts.size(); ++source) {
        const auto& burst = pack.burstLibrary.bursts[source];
        if (burst.empty()) continue;
        const auto existing = std::find_if(
            candidate.burstLibrary.bursts.begin(),
            candidate.burstLibrary.bursts.end(),
            [&](const BurstDefinition& other) {
                return !other.empty() && burstEqual(other, burst);
            });
        if (existing != candidate.burstLibrary.bursts.end()) {
            burstMap[source] = static_cast<std::size_t>(
                existing - candidate.burstLibrary.bursts.begin());
            ++imported.burstsReused;
            continue;
        }
        const auto target = firstEmptyBurst(candidate.burstLibrary);
        if (target >= candidate.burstLibrary.bursts.size())
            return packFailure(ProjectErrorCode::SizeLimitExceeded,
                "$.bursts", "project Burst Library has no free slots");
        candidate.burstLibrary.bursts[target] = burst;
        burstMap[source] = target;
        ++imported.burstsAdded;
    }

    for (std::size_t source = 0u;
         source < pack.phraseLibrary.phrases.size(); ++source) {
        const auto& packed = pack.phraseLibrary.phrases[source];
        if (packed.empty() && packed.name.empty()) continue;
        PhraseDefinition phrase = packed;
        for (auto& cell : phrase.notes) {
            if (cell.state != NoteCellState::Burst) continue;
            const auto packedSlot = static_cast<std::size_t>(cell.note);
            if (packedSlot >= burstMap.size()
                || burstMap[packedSlot] >= kBurstDefinitionCount)
                return packFailure(ProjectErrorCode::InconsistentData,
                    "$.phrases", "Phrase references a Burst missing from the pack");
            cell.note = static_cast<uint8_t>(burstMap[packedSlot]);
        }
        const auto existing = std::find_if(
            candidate.phraseLibrary.phrases.begin(),
            candidate.phraseLibrary.phrases.end(),
            [&](const PhraseDefinition& other) {
                return !(other.empty() && other.name.empty())
                    && phraseEqual(other, phrase);
            });
        if (existing != candidate.phraseLibrary.phrases.end()) {
            ++imported.phrasesReused;
            continue;
        }
        const auto target = firstEmptyPhrase(candidate.phraseLibrary);
        if (target >= candidate.phraseLibrary.phrases.size())
            return packFailure(ProjectErrorCode::SizeLimitExceeded,
                "$.phrases", "project Phrase Library has no free slots");
        candidate.phraseLibrary.phrases[target] = std::move(phrase);
        ++imported.phrasesAdded;
    }
    destination = std::move(candidate);
    if (report) *report = imported;
    return {};
}

} // namespace s3g::tracker
