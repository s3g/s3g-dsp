#include "s3g/tracker/asset_pack.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <string>
#include <utility>

using namespace s3g::tracker;

namespace {

constexpr uint8_t kKick = 36u;
constexpr uint8_t kRim = 37u;
constexpr uint8_t kSnare = 38u;
constexpr uint8_t kFloorTom = 41u;
constexpr uint8_t kClosedHat = 42u;
constexpr uint8_t kOpenHat = 46u;
constexpr std::size_t kStarterBurstCount = 16u;

BurstDefinition makeBurst(std::string name,
    std::initializer_list<BurstEvent> events)
{
    BurstDefinition burst;
    burst.name = std::move(name);
    burst.eventCount = static_cast<uint8_t>(events.size());
    std::copy(events.begin(), events.end(), burst.events.begin());
    return burst;
}

struct RowHit {
    std::size_t row;
    float velocity;
};

PhraseDefinition phrase(std::string name, std::size_t length)
{
    auto result = makeBlankPhrase(length);
    result.name = std::move(name);
    result.previewMidiChannel = 1u;
    return result;
}

void addHit(PhraseDefinition& destination, std::size_t row,
    uint8_t note, float velocity)
{
    if (row >= destination.length || row >= destination.notes.size()) return;
    velocity = std::clamp(velocity, 0.0f, 1.0f);
    auto& noteCell = destination.notes[row];
    auto& velocityCell = destination.velocities[row];
    if (noteCell.state == NoteCellState::Rest) {
        noteCell = NoteCell::withNote(note);
        velocityCell = ValueCell::withValue(velocity);
        return;
    }
    if (noteCell.state != NoteCellState::Note) return;
    std::array<std::pair<uint8_t, float>, kMaximumNoteVoices> voices {};
    std::size_t count = noteCell.noteVoiceCount();
    for (std::size_t voice = 0u; voice < count; ++voice)
        voices[voice] = { noteCell.noteVoice(voice),
            velocityCell.state == ValueCellState::Value
                ? velocityCell.valueVoice(voice) : 100.0f / 127.0f };
    for (std::size_t voice = 0u; voice < count; ++voice) {
        if (voices[voice].first != note) continue;
        voices[voice].second = std::max(voices[voice].second, velocity);
        return;
    }
    if (count >= voices.size()) return;
    voices[count++] = { note, velocity };
    std::sort(voices.begin(), voices.begin() + count,
        [](const auto& left, const auto& right) {
            return left.first < right.first;
        });
    std::array<uint8_t, kMaximumNoteVoices> notes {};
    std::array<float, kMaximumNoteVoices> velocities {};
    for (std::size_t voice = 0u; voice < count; ++voice) {
        notes[voice] = voices[voice].first;
        velocities[voice] = voices[voice].second;
    }
    noteCell = NoteCell::withNotes(notes, static_cast<uint8_t>(count));
    velocityCell = ValueCell::withValues(
        velocities, static_cast<uint8_t>(count));
}

float drumMicroTime(uint8_t note, std::size_t row) noexcept
{
    // 0.5 is on-grid. The small note-specific offsets keep layered hits from
    // collapsing onto one timestamp while the row term supplies a restrained
    // long/short pocket suitable for MIDI drums.
    float value = 0.5f;
    if (note == kKick) value -= 0.014f;
    else if (note == kSnare) value += 0.030f;
    else if (note == kRim) value += 0.020f;
    else if (note == kClosedHat) value += 0.010f;
    else if (note == kOpenHat) value += 0.022f;
    else if (note == kFloorTom) value += 0.006f;
    if (row % 4u == 2u) value += 0.026f;
    else if (row % 4u == 3u) value += 0.012f;
    return std::clamp(value, 0.42f, 0.58f);
}

void authorMicroTime(PhraseDefinition& destination)
{
    auto& pair = destination.fxPairs[0u];
    for (std::size_t row = 0u; row < destination.length; ++row) {
        const auto& notes = destination.notes[row];
        if (notes.state != NoteCellState::Note) continue;
        const auto count = notes.noteVoiceCount();
        std::array<float, kMaximumNoteVoices> values {};
        for (std::size_t voice = 0u; voice < count; ++voice)
            values[voice] = drumMicroTime(notes.noteVoice(voice), row);
        pair.actions[row] = FxActionCell::sequencer(
            SequencerAction::MicroTime);
        pair.values[row] = count == 1u
            ? FxValueCell::withValue(values[0u])
            : FxValueCell::withValues(values, count);
    }
}

void addRows(PhraseDefinition& destination, uint8_t note,
    std::initializer_list<RowHit> rows)
{
    for (const auto& hit : rows)
        addHit(destination, hit.row, note, hit.velocity);
}

bool containsRow(std::initializer_list<std::size_t> rows,
    std::size_t candidate)
{
    return std::find(rows.begin(), rows.end(), candidate) != rows.end();
}

void addHatGrid(PhraseDefinition& destination, std::size_t step,
    std::initializer_list<std::size_t> openRows)
{
    if (step == 0u) return;
    for (std::size_t row = 0u; row < destination.length; row += step) {
        const bool open = containsRow(openRows, row);
        const float velocity = open ? 0.76f
            : row % 4u == 0u ? 0.84f
            : row % 2u == 0u ? 0.64f : 0.52f;
        addHit(destination, row, open ? kOpenHat : kClosedHat, velocity);
    }
}

PhraseDefinition groove(std::string name, std::size_t length,
    std::initializer_list<RowHit> kicks,
    std::initializer_list<RowHit> snares,
    std::size_t hatStep = 2u,
    std::initializer_list<std::size_t> openHats = {},
    std::initializer_list<RowHit> rims = {},
    std::initializer_list<RowHit> floorToms = {})
{
    auto result = phrase(std::move(name), length);
    addHatGrid(result, hatStep, openHats);
    addRows(result, kKick, kicks);
    addRows(result, kSnare, snares);
    addRows(result, kRim, rims);
    addRows(result, kFloorTom, floorToms);
    authorMicroTime(result);
    return result;
}

void useBurst(PhraseDefinition& destination, std::size_t row,
    std::size_t burst)
{
    if (row >= destination.length || burst >= kBurstDefinitionCount) return;
    destination.notes[row] = NoteCell::withBurst(
        static_cast<uint8_t>(burst));
    destination.velocities[row] = ValueCell::defaultValue();
    destination.gates[row] = GateCell::defaultValue();
}

PhraseDefinition repeatPhrase(std::string name,
    const PhraseDefinition& source, std::size_t repetitions)
{
    auto result = phrase(std::move(name), source.length * repetitions);
    for (std::size_t repetition = 0u; repetition < repetitions; ++repetition) {
        const auto offset = repetition * source.length;
        for (std::size_t row = 0u; row < source.length; ++row) {
            result.notes[offset + row] = source.notes[row];
            result.velocities[offset + row] = source.velocities[row];
            result.gates[offset + row] = source.gates[row];
            for (std::size_t pair = 0u; pair < result.fxPairs.size(); ++pair) {
                result.fxPairs[pair].actions[offset + row]
                    = source.fxPairs[pair].actions[row];
                result.fxPairs[pair].values[offset + row]
                    = source.fxPairs[pair].values[row];
            }
        }
    }
    return result;
}

void store(TrackerAssetPack& pack, std::size_t slot,
    PhraseDefinition source)
{
    if (slot < pack.phraseBank.library.phrases.size())
        pack.phraseBank.library.phrases[slot] = std::move(source);
}

std::size_t populatedBurstCount(const BurstLibrary& library)
{
    return static_cast<std::size_t>(std::count_if(library.bursts.begin(),
        library.bursts.end(), [](const BurstDefinition& burst) {
            return !burst.empty();
        }));
}

std::size_t populatedPhraseCount(const PhraseLibrary& library)
{
    return static_cast<std::size_t>(std::count_if(library.phrases.begin(),
        library.phrases.end(), [](const PhraseDefinition& value) {
            return !value.empty() || !value.name.empty();
        }));
}

bool validateStarterContent(const TrackerAssetPack& pack)
{
    if (populatedBurstCount(pack.burstBank.library) != kStarterBurstCount
        || populatedPhraseCount(pack.phraseBank.library)
            != kPhraseLibrarySlots) return false;
    std::array<bool, kStarterBurstCount> referenced {};
    std::size_t microTimeRows = 0u;
    std::size_t polyphonicMicroTimeRows = 0u;
    for (const auto& value : pack.phraseBank.library.phrases) {
        if (value.previewMidiChannel != 1u || value.empty()
            || value.name.empty()) return false;
        bool hasPolyphonicRow = false;
        for (const auto& cell : value.notes) {
            hasPolyphonicRow |= cell.noteVoiceCount() > 1u;
            if (cell.state == NoteCellState::Burst
                && cell.note < referenced.size()) referenced[cell.note] = true;
        }
        for (std::size_t row = 0u; row < value.length; ++row) {
            const auto& action = value.fxPairs[0u].actions[row];
            if (action.state != FxActionCellState::Sequencer
                || action.sequencerAction != SequencerAction::MicroTime)
                continue;
            ++microTimeRows;
            if (value.fxPairs[0u].values[row].valueVoiceCount() > 1u)
                ++polyphonicMicroTimeRows;
        }
        if (!hasPolyphonicRow) return false;
    }
    return microTimeRows >= 300u && polyphonicMicroTimeRows >= 250u
        && std::all_of(referenced.begin(), referenced.end(),
        [](bool used) { return used; });
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "usage: generate_starter_pack OUTPUT.s3gpack\n";
        return 2;
    }
    TrackerAssetPack pack;
    pack.name = "S3G DRUM FOUNDATIONS 01";
    pack.burstBank.id = kProjectAssetBankId;
    pack.burstBank.name = "DRUM FOUNDATIONS 01 BURSTS";
    pack.phraseBank.id = kProjectAssetBankId;
    pack.phraseBank.name = "DRUM FOUNDATIONS 01";
    pack.phraseBank.companionBurstBankId = kProjectAssetBankId;
    pack.burstBank.library.bursts[0u] = makeBurst("CLOSED HAT TRIPLET", {
        { 0u, 42u, 112u, 30u }, { 21845u, 42u, 88u, 26u },
        { 43690u, 42u, 101u, 28u },
    });
    pack.burstBank.library.bursts[1u] = makeBurst("SNARE RUFF", {
        { 0u, 38u, 72u, 24u }, { 12288u, 37u, 86u, 20u },
        { 32768u, 38u, 122u, 44u },
    });
    pack.burstBank.library.bursts[2u] = makeBurst("KICK DRAG", {
        { 0u, 36u, 118u, 34u }, { 24576u, 36u, 82u, 28u },
        { 49152u, 36u, 108u, 38u },
    });
    pack.burstBank.library.bursts[3u] = makeBurst("HAT FIVE CUT", {
        { 0u, 42u, 112u, 22u }, { 13107u, 42u, 76u, 20u },
        { 26214u, 46u, 92u, 18u }, { 39321u, 42u, 70u, 20u },
        { 52428u, 42u, 98u, 24u },
    });
    pack.burstBank.library.bursts[4u] = makeBurst("SNARE FLAM", {
        { 0u, 38u, 70u, 22u }, { 9216u, 38u, 124u, 46u },
    });
    pack.burstBank.library.bursts[5u] = makeBurst("SNARE FIVE ROLL", {
        { 0u, 38u, 68u, 20u }, { 14746u, 38u, 78u, 20u },
        { 29491u, 38u, 90u, 22u }, { 44236u, 38u, 104u, 24u },
        { 58981u, 38u, 124u, 32u },
    });
    pack.burstBank.library.bursts[6u] = makeBurst("HAT FOUR RATCHET", {
        { 0u, 42u, 108u, 20u }, { 16384u, 42u, 72u, 18u },
        { 32768u, 42u, 88u, 18u }, { 49152u, 42u, 98u, 20u },
    });
    pack.burstBank.library.bursts[7u] = makeBurst("HAT ACCEL", {
        { 0u, 42u, 70u, 20u }, { 28672u, 42u, 82u, 18u },
        { 47104u, 42u, 96u, 18u }, { 59392u, 42u, 112u, 20u },
    });
    pack.burstBank.library.bursts[8u] = makeBurst("HAT DECEL", {
        { 0u, 42u, 112u, 20u }, { 8192u, 42u, 96u, 18u },
        { 22528u, 42u, 82u, 18u }, { 45056u, 42u, 70u, 20u },
    });
    pack.burstBank.library.bursts[9u] = makeBurst("OPEN CLOSE TURN", {
        { 0u, 46u, 110u, 38u }, { 32768u, 42u, 82u, 24u },
    });
    pack.burstBank.library.bursts[10u] = makeBurst("KICK DOUBLE", {
        { 0u, 36u, 122u, 34u }, { 32768u, 36u, 96u, 32u },
    });
    pack.burstBank.library.bursts[11u] = makeBurst("KICK SNARE PICKUP", {
        { 0u, 36u, 104u, 30u }, { 32768u, 38u, 118u, 40u },
    });
    pack.burstBank.library.bursts[12u] = makeBurst("FLOOR TOM TRIPLET", {
        { 0u, 41u, 82u, 30u }, { 21845u, 41u, 98u, 30u },
        { 43690u, 41u, 118u, 34u },
    });
    pack.burstBank.library.bursts[13u] = makeBurst("TOM SNARE TURN", {
        { 0u, 41u, 86u, 28u }, { 24576u, 37u, 92u, 24u },
        { 49152u, 38u, 124u, 42u },
    });
    pack.burstBank.library.bursts[14u] = makeBurst("RIM DRAG", {
        { 0u, 37u, 62u, 18u }, { 16384u, 37u, 74u, 18u },
        { 36864u, 37u, 88u, 20u }, { 57344u, 37u, 104u, 24u },
    });
    pack.burstBank.library.bursts[15u] = makeBurst("JUNGLE HAT SHUFFLE", {
        { 0u, 42u, 108u, 20u }, { 22528u, 42u, 72u, 18u },
        { 40960u, 46u, 94u, 22u }, { 57344u, 42u, 84u, 18u },
    });

    // 01–08 · Hip-hop and boom-bap foundations.
    auto p01 = groove("BOOM BAP CORE / 16", 16u,
        {{0u,.98f},{6u,.78f},{8u,.88f},{10u,.90f}},
        {{4u,.94f},{12u,1.0f}}, 2u, {14u});
    useBurst(p01, 15u, 0u); store(pack, 0u, std::move(p01));
    auto p02 = groove("BREAKBEAT PUSH / 16", 16u,
        {{0u,1.0f},{3u,.76f},{6u,.84f},{8u,.92f},{11u,.74f},{14u,.86f}},
        {{4u,.96f},{12u,1.0f}}, 2u, {12u});
    useBurst(p02, 7u, 1u); useBurst(p02, 15u, 3u);
    store(pack, 1u, std::move(p02));
    auto p03 = groove("HALF-TIME HAT TURN / 16", 16u,
        {{0u,.98f},{4u,.72f},{10u,.82f}}, {{8u,1.0f}}, 2u, {6u,14u},
        {{12u,.68f}}, {{14u,.76f}});
    useBurst(p03, 15u, 3u); store(pack, 2u, std::move(p03));
    auto p04 = groove("DUSTY KICK POCKET / 16", 16u,
        {{0u,.98f},{5u,.72f},{7u,.84f},{10u,.76f},{14u,.90f}},
        {{4u,.92f},{12u,.97f}}, 2u, {14u}, {{11u,.54f}});
    store(pack, 3u, std::move(p04));
    auto p05 = groove("RIM BACKBEAT / 16", 16u,
        {{0u,.96f},{7u,.82f},{10u,.88f}}, {}, 2u, {14u},
        {{4u,.88f},{12u,.94f}});
    useBurst(p05, 15u, 14u); store(pack, 4u, std::move(p05));
    auto p06 = groove("OPEN HAT ANSWER / 16", 16u,
        {{0u,.98f},{6u,.80f},{9u,.86f},{14u,.76f}},
        {{4u,.95f},{12u,1.0f}}, 2u, {6u,14u}, {{11u,.58f}});
    useBurst(p06, 15u, 9u); store(pack, 5u, std::move(p06));
    auto p07 = groove("FLOOR TOM POCKET / 16", 16u,
        {{0u,.96f},{7u,.82f},{10u,.88f}}, {{4u,.94f},{12u,.98f}},
        2u, {14u}, {}, {{6u,.62f},{15u,.86f}});
    store(pack, 6u, std::move(p07));
    auto p08 = groove("TWO-BAR BOOM BAP / 32", 32u,
        {{0u,.98f},{6u,.78f},{10u,.88f},{16u,.96f},{19u,.72f},
         {22u,.84f},{26u,.90f},{30u,.80f}},
        {{4u,.95f},{12u,1.0f},{20u,.94f},{28u,1.0f}}, 2u,
        {14u,30u}, {{11u,.55f},{27u,.58f}});
    useBurst(p08, 31u, 4u); store(pack, 7u, std::move(p08));

    // 09–16 · Breakbeat and funk variations.
    store(pack, 8u, groove("FUNK BREAK A / 16", 16u,
        {{0u,.98f},{3u,.74f},{7u,.86f},{10u,.82f},{14u,.90f}},
        {{4u,.96f},{12u,.99f}}, 2u, {6u,14u}, {{11u,.54f}}));
    auto p10 = groove("FUNK BREAK B / 16", 16u,
        {{0u,.98f},{2u,.70f},{7u,.84f},{9u,.78f},{14u,.92f}},
        {{4u,.95f},{12u,1.0f}}, 2u, {10u}, {{6u,.52f},{15u,.60f}});
    useBurst(p10, 11u, 1u); store(pack, 9u, std::move(p10));
    store(pack, 10u, groove("SYNC KICK BREAK / 16", 16u,
        {{0u,.99f},{3u,.78f},{6u,.86f},{9u,.74f},{11u,.84f},{14u,.92f}},
        {{4u,.96f},{12u,1.0f}}, 2u, {14u}));
    auto p12 = groove("GHOST SNARE BREAK / 16", 16u,
        {{0u,.98f},{7u,.84f},{10u,.90f}}, {{4u,.96f},{12u,1.0f}},
        2u, {14u}, {{3u,.46f},{6u,.52f},{11u,.58f},{15u,.62f}});
    useBurst(p12, 15u, 1u); store(pack, 11u, std::move(p12));
    auto p13 = groove("OPEN HAT BREAK / 16", 16u,
        {{0u,.98f},{3u,.76f},{7u,.86f},{10u,.84f}},
        {{4u,.96f},{12u,1.0f}}, 2u, {6u,10u,14u});
    useBurst(p13, 15u, 9u); store(pack, 12u, std::move(p13));
    auto p14 = groove("DRAG KICK BREAK / 16", 16u,
        {{0u,.98f},{7u,.84f},{10u,.88f}}, {{4u,.96f},{12u,1.0f}},
        2u, {14u}, {{11u,.55f}});
    useBurst(p14, 14u, 2u); store(pack, 13u, std::move(p14));
    auto p15 = groove("TWO-BAR BREAK TURN / 32", 32u,
        {{0u,.99f},{3u,.74f},{7u,.84f},{10u,.88f},{14u,.78f},
         {16u,.98f},{19u,.76f},{22u,.86f},{25u,.74f},{30u,.92f}},
        {{4u,.96f},{12u,1.0f},{20u,.94f},{28u,1.0f}}, 2u,
        {14u,30u}, {{11u,.52f},{27u,.58f}});
    useBurst(p15, 31u, 13u); store(pack, 14u, std::move(p15));
    auto p16 = groove("BREAK FILL EXIT / 16", 16u,
        {{0u,.98f},{3u,.76f},{7u,.84f}}, {{4u,.96f},{12u,1.0f}},
        2u, {6u}, {{10u,.62f}}, {{11u,.72f},{13u,.84f}});
    useBurst(p16, 14u, 12u); useBurst(p16, 15u, 4u);
    store(pack, 15u, std::move(p16));

    // 17–24 · Half-time and trap-compatible acoustic kit phrases.
    store(pack, 16u, groove("HALF TIME CORE / 16", 16u,
        {{0u,.99f},{5u,.74f},{10u,.88f},{14u,.80f}}, {{8u,1.0f}},
        2u, {6u,14u}, {{12u,.56f}}));
    auto p18 = groove("HALF TIME RIM / 16", 16u,
        {{0u,.98f},{6u,.80f},{11u,.86f}}, {}, 2u, {14u},
        {{8u,.94f},{15u,.58f}});
    useBurst(p18, 7u, 14u); store(pack, 17u, std::move(p18));
    store(pack, 18u, groove("TRAP HAT POCKET / 16", 16u,
        {{0u,.99f},{3u,.72f},{7u,.84f},{10u,.90f},{13u,.76f}},
        {{8u,1.0f}}, 1u, {6u,14u}));
    auto p20 = groove("TRIPLET HAT TURN / 16", 16u,
        {{0u,.98f},{6u,.82f},{10u,.88f}}, {{8u,1.0f}}, 2u, {14u});
    useBurst(p20, 7u, 0u); useBurst(p20, 15u, 0u);
    store(pack, 19u, std::move(p20));
    auto p21 = groove("KICK DOUBLE HALFTIME / 16", 16u,
        {{0u,.99f},{10u,.88f}}, {{8u,1.0f}}, 2u, {14u});
    useBurst(p21, 6u, 10u); useBurst(p21, 14u, 10u);
    store(pack, 20u, std::move(p21));
    auto p22 = groove("OPEN HAT DROP / 16", 16u,
        {{0u,.98f},{7u,.82f},{11u,.88f}}, {{8u,1.0f}}, 2u, {6u,14u});
    useBurst(p22, 15u, 9u); store(pack, 21u, std::move(p22));
    auto p23 = groove("TWO-BAR HALF TIME / 32", 32u,
        {{0u,.99f},{5u,.72f},{10u,.86f},{14u,.80f},{16u,.98f},
         {22u,.78f},{27u,.88f},{30u,.82f}},
        {{8u,1.0f},{24u,1.0f}}, 2u, {14u,30u},
        {{12u,.52f},{28u,.58f}});
    useBurst(p23, 31u, 7u); store(pack, 22u, std::move(p23));
    auto p24 = groove("STOP TIME TURN / 16", 16u,
        {{0u,1.0f},{7u,.82f}}, {{8u,1.0f}}, 2u, {6u},
        {{11u,.58f}}, {{12u,.74f},{14u,.92f}});
    useBurst(p24, 15u, 11u); store(pack, 23u, std::move(p24));

    // 25–32 · Soul, shuffle, and laid-back pockets.
    store(pack, 24u, groove("NEO SOUL POCKET / 16", 16u,
        {{0u,.94f},{7u,.72f},{10u,.82f},{14u,.76f}},
        {{4u,.88f},{12u,.94f}}, 2u, {14u}, {{3u,.42f},{11u,.48f}}));
    auto p26 = groove("LAID BACK RIM / 16", 16u,
        {{0u,.92f},{6u,.70f},{10u,.80f}}, {}, 2u, {14u},
        {{4u,.84f},{12u,.90f},{15u,.50f}});
    useBurst(p26, 11u, 14u); store(pack, 25u, std::move(p26));
    store(pack, 26u, groove("SHUFFLE GHOST / 12", 12u,
        {{0u,.96f},{5u,.72f},{8u,.84f}}, {{3u,.92f},{9u,.98f}},
        2u, {10u}, {{2u,.44f},{7u,.50f}}));
    auto p28 = groove("SIX EIGHT POCKET / 12", 12u,
        {{0u,.98f},{5u,.76f},{8u,.86f}}, {{6u,.98f}}, 2u, {10u},
        {{3u,.50f}}, {{11u,.72f}});
    useBurst(p28, 5u, 0u); store(pack, 27u, std::move(p28));
    store(pack, 28u, groove("FUNK TOM ANSWER / 16", 16u,
        {{0u,.96f},{7u,.80f},{10u,.84f}}, {{4u,.92f},{12u,.98f}},
        2u, {14u}, {{11u,.48f}}, {{6u,.62f},{15u,.82f}}));
    auto p30 = groove("BROKEN SOUL / 16", 16u,
        {{0u,.94f},{3u,.66f},{9u,.78f},{14u,.84f}},
        {{4u,.90f},{12u,.96f}}, 2u, {10u}, {{7u,.48f},{15u,.54f}});
    useBurst(p30, 11u, 8u); store(pack, 29u, std::move(p30));
    auto p31 = groove("TWO-BAR SOUL / 32", 32u,
        {{0u,.94f},{7u,.72f},{10u,.82f},{16u,.92f},{22u,.70f},
         {25u,.78f},{30u,.84f}},
        {{4u,.88f},{12u,.94f},{20u,.90f},{28u,.96f}}, 2u,
        {14u,30u}, {{3u,.42f},{11u,.48f},{19u,.44f},{27u,.52f}});
    useBurst(p31, 31u, 9u); store(pack, 30u, std::move(p31));
    auto p32 = groove("SOUL FILL EXIT / 16", 16u,
        {{0u,.94f},{7u,.74f}}, {{4u,.90f},{12u,.98f}}, 2u, {6u},
        {{10u,.52f}}, {{11u,.68f},{13u,.78f},{14u,.90f}});
    useBurst(p32, 15u, 13u); store(pack, 31u, std::move(p32));

    // 33–40 · Drum-and-bass and jungle foundations.
    store(pack, 32u, groove("DNB TWO STEP / 16", 16u,
        {{0u,1.0f},{7u,.82f},{10u,.92f}}, {{4u,1.0f},{12u,1.0f}},
        2u, {14u}));
    auto p34 = groove("DNB SYNC KICK / 16", 16u,
        {{0u,1.0f},{3u,.76f},{7u,.86f},{10u,.94f},{14u,.82f}},
        {{4u,1.0f},{12u,1.0f}}, 2u, {6u,14u});
    useBurst(p34, 15u, 15u); store(pack, 33u, std::move(p34));
    store(pack, 34u, groove("JUNGLE BREAK A / 16", 16u,
        {{0u,1.0f},{3u,.76f},{6u,.86f},{10u,.92f},{14u,.80f}},
        {{4u,1.0f},{12u,1.0f}}, 2u, {14u}, {{7u,.56f},{11u,.62f}}));
    auto p36 = groove("JUNGLE BREAK B / 16", 16u,
        {{0u,1.0f},{2u,.72f},{7u,.88f},{9u,.80f},{14u,.94f}},
        {{4u,1.0f},{12u,1.0f}}, 2u, {10u}, {{6u,.54f},{15u,.62f}});
    useBurst(p36, 11u, 4u); store(pack, 35u, std::move(p36));
    auto p37 = groove("JUNGLE HAT SHUFFLE / 16", 16u,
        {{0u,1.0f},{3u,.78f},{7u,.86f},{10u,.94f}},
        {{4u,1.0f},{12u,1.0f}}, 2u, {14u});
    useBurst(p37, 2u, 15u); useBurst(p37, 10u, 15u);
    store(pack, 36u, std::move(p37));
    auto p38 = groove("DNB HALF-BAR TURN / 8", 8u,
        {{0u,1.0f},{3u,.78f},{6u,.90f}}, {{4u,1.0f}}, 1u, {6u});
    useBurst(p38, 7u, 6u); store(pack, 37u, std::move(p38));
    auto p39 = groove("TWO-BAR JUNGLE / 32", 32u,
        {{0u,1.0f},{3u,.76f},{7u,.86f},{10u,.92f},{14u,.80f},
         {16u,1.0f},{19u,.78f},{22u,.88f},{25u,.76f},{30u,.94f}},
        {{4u,1.0f},{12u,1.0f},{20u,1.0f},{28u,1.0f}}, 2u,
        {14u,30u}, {{11u,.56f},{27u,.60f}});
    useBurst(p39, 31u, 7u); store(pack, 38u, std::move(p39));
    auto p40 = groove("JUNGLE ROLL EXIT / 16", 16u,
        {{0u,1.0f},{3u,.76f},{7u,.86f},{10u,.92f}},
        {{4u,1.0f},{12u,1.0f}}, 2u, {6u}, {}, {{13u,.76f}});
    useBurst(p40, 14u, 5u); useBurst(p40, 15u, 13u);
    store(pack, 39u, std::move(p40));

    // 41–48 · Odd and polymetric phrase lengths.
    auto p41 = groove("ODD KIT CYCLE / 15", 15u,
        {{0u,.98f},{6u,.76f},{9u,.88f}}, {{4u,.94f},{11u,1.0f}},
        2u, {9u}, {}, {{13u,.78f}});
    useBurst(p41, 14u, 0u); store(pack, 40u, std::move(p41));
    store(pack, 41u, groove("SEVEN STEP BREAK / 7", 7u,
        {{0u,.98f},{5u,.82f}}, {{3u,.96f}}, 2u, {6u}, {}, {{4u,.66f}}));
    auto p43 = groove("NINE STEP POCKET / 9", 9u,
        {{0u,.98f},{6u,.84f}}, {{4u,.96f}}, 2u, {8u}, {{7u,.58f}});
    useBurst(p43, 8u, 9u); store(pack, 42u, std::move(p43));
    store(pack, 43u, groove("ELEVEN STEP BREAK / 11", 11u,
        {{0u,.98f},{3u,.72f},{7u,.86f}}, {{4u,.96f},{9u,.98f}},
        2u, {10u}, {{6u,.52f}}));
    auto p45 = groove("THIRTEEN STEP TURN / 13", 13u,
        {{0u,.98f},{5u,.76f},{9u,.86f}}, {{4u,.94f},{10u,.98f}},
        2u, {12u}, {}, {{11u,.74f}});
    useBurst(p45, 12u, 12u); store(pack, 44u, std::move(p45));
    store(pack, 45u, groove("FIVE STEP LOOP / 5", 5u,
        {{0u,.98f},{3u,.80f}}, {{2u,.96f}}, 1u, {4u}));
    auto p47 = groove("TWELVE STEP SHUFFLE / 12", 12u,
        {{0u,.98f},{5u,.76f},{8u,.86f}}, {{3u,.94f},{9u,.99f}},
        2u, {10u}, {{7u,.52f}});
    useBurst(p47, 11u, 8u); store(pack, 46u, std::move(p47));
    auto p48 = groove("TWENTY-FOUR STEP CYCLE / 24", 24u,
        {{0u,.98f},{6u,.78f},{10u,.86f},{15u,.74f},{19u,.88f},{22u,.82f}},
        {{4u,.95f},{12u,.98f},{20u,1.0f}}, 2u, {10u,22u},
        {{11u,.52f},{18u,.56f}});
    useBurst(p48, 23u, 15u); store(pack, 47u, std::move(p48));

    // 49–56 · Fills and transitions that remain complete kit phrases.
    auto p49 = groove("FLOOR TOM TURN / 8", 8u,
        {{0u,.92f}}, {{4u,.94f}}, 2u, {4u}, {{6u,.68f}},
        {{2u,.72f},{5u,.82f},{6u,.96f}});
    useBurst(p49, 7u, 2u); store(pack, 48u, std::move(p49));
    auto p50 = groove("SNARE TOM CLIMB / 8", 8u,
        {{0u,.94f}}, {{4u,.96f},{6u,.88f}}, 2u, {}, {},
        {{3u,.66f},{5u,.78f}});
    useBurst(p50, 7u, 13u); store(pack, 49u, std::move(p50));
    auto p51 = groove("RIM TO SNARE / 8", 8u,
        {{0u,.94f},{5u,.78f}}, {{4u,.96f}}, 2u, {6u},
        {{3u,.58f},{6u,.74f}});
    useBurst(p51, 7u, 4u); store(pack, 50u, std::move(p51));
    auto p52 = groove("KICK DRAG FILL / 8", 8u,
        {{0u,.96f},{4u,.84f}}, {{6u,.94f}}, 2u, {6u});
    useBurst(p52, 3u, 2u); useBurst(p52, 7u, 11u);
    store(pack, 51u, std::move(p52));
    auto p53 = groove("HAT RATCHET FILL / 8", 8u,
        {{0u,.96f},{5u,.82f}}, {{4u,.98f}}, 2u, {6u});
    useBurst(p53, 3u, 6u); useBurst(p53, 7u, 3u);
    store(pack, 52u, std::move(p53));
    auto p54 = groove("SNARE ROLL EXIT / 8", 8u,
        {{0u,.96f},{5u,.78f}}, {{4u,.98f}}, 2u, {6u});
    useBurst(p54, 6u, 5u); useBurst(p54, 7u, 4u);
    store(pack, 53u, std::move(p54));
    auto p55 = groove("TWO-BEAT PICKUP / 4", 4u,
        {{0u,.94f}}, {{2u,.98f}}, 1u, {3u});
    useBurst(p55, 3u, 11u); store(pack, 54u, std::move(p55));
    auto p56 = groove("FULL BAR FILL / 16", 16u,
        {{0u,.98f},{6u,.80f}}, {{4u,.96f},{12u,1.0f}}, 2u, {6u},
        {{10u,.58f}}, {{11u,.68f},{13u,.78f}});
    useBurst(p56, 14u, 12u); useBurst(p56, 15u, 5u);
    store(pack, 55u, std::move(p56));

    // 57–64 · Arrangement-ready phrases and longer evolving forms.
    store(pack, 56u, groove("SPARSE INTRO / 16", 16u,
        {{0u,.94f},{10u,.80f}}, {}, 4u, {12u}, {{8u,.84f}}));
    auto verseCore = groove("VERSE CORE", 16u,
        {{0u,.98f},{6u,.76f},{10u,.86f},{14u,.80f}},
        {{4u,.94f},{12u,.98f}}, 2u, {14u}, {{11u,.52f}});
    auto p58 = repeatPhrase("VERSE POCKET / 32", verseCore, 2u);
    useBurst(p58, 31u, 0u); store(pack, 57u, std::move(p58));
    auto chorusCore = groove("CHORUS CORE", 16u,
        {{0u,1.0f},{3u,.76f},{7u,.86f},{10u,.92f},{14u,.84f}},
        {{4u,1.0f},{12u,1.0f}}, 2u, {6u,14u});
    auto p59 = repeatPhrase("CHORUS LIFT / 32", chorusCore, 2u);
    useBurst(p59, 31u, 4u); store(pack, 58u, std::move(p59));
    auto p60 = groove("DROP ENTRY / 16", 16u,
        {{0u,1.0f},{7u,.84f},{10u,.94f},{14u,.82f}},
        {{4u,1.0f},{12u,1.0f}}, 2u, {6u,14u});
    useBurst(p60, 15u, 7u); store(pack, 59u, std::move(p60));
    auto p61 = groove("BREAKDOWN RIM / 16", 16u,
        {{0u,.92f},{10u,.78f}}, {}, 4u, {12u},
        {{4u,.82f},{8u,.88f},{12u,.92f}});
    useBurst(p61, 15u, 14u); store(pack, 60u, std::move(p61));
    auto buildCore = groove("BUILD CORE", 16u,
        {{0u,.96f},{7u,.80f},{10u,.88f}}, {{4u,.94f},{12u,.98f}},
        2u, {14u});
    auto p62 = repeatPhrase("FOUR-BAR BUILD / 64", buildCore, 4u);
    useBurst(p62, 31u, 6u); useBurst(p62, 47u, 7u);
    useBurst(p62, 62u, 5u); useBurst(p62, 63u, 3u);
    store(pack, 61u, std::move(p62));
    auto evolveCore = groove("EVOLVE CORE", 16u,
        {{0u,.98f},{3u,.72f},{7u,.84f},{10u,.90f},{14u,.80f}},
        {{4u,.96f},{12u,1.0f}}, 2u, {14u}, {{11u,.52f}});
    auto p63 = repeatPhrase("FOUR-BAR EVOLVE / 64", evolveCore, 4u);
    useBurst(p63, 15u, 8u); useBurst(p63, 31u, 9u);
    useBurst(p63, 47u, 15u); useBurst(p63, 63u, 13u);
    store(pack, 62u, std::move(p63));
    auto p64 = groove("FINAL TURN / 16", 16u,
        {{0u,1.0f},{3u,.76f},{7u,.86f}}, {{4u,1.0f},{12u,1.0f}},
        2u, {6u}, {{10u,.58f}}, {{11u,.70f},{13u,.84f}});
    useBurst(p64, 14u, 12u); useBurst(p64, 15u, 5u);
    store(pack, 63u, std::move(p64));

    if (!validateStarterContent(pack)) {
        std::cerr << "generated starter content failed coverage validation\n";
        return 1;
    }

    std::string encoded;
    const auto result = encodeTrackerAssetPack(pack, encoded);
    if (!result.ok()) {
        std::cerr << result.location << ": " << result.message << '\n';
        return 1;
    }
    TrackerAssetPack decoded;
    const auto decodeResult = decodeTrackerAssetPack(encoded, decoded);
    if (!decodeResult.ok()) {
        std::cerr << "generated pack decode failed at "
                  << decodeResult.location << ": "
                  << decodeResult.message << '\n';
        return 1;
    }
    ProjectDocument importTarget;
    AssetPackImportReport importReport;
    const auto importResult = importTrackerAssetPack(
        decoded, importTarget, &importReport);
    if (!importResult.ok()
        || importReport.burstsAdded != kStarterBurstCount
        || importReport.phrasesAdded != kPhraseLibrarySlots) {
        std::cerr << "generated pack import failed at "
                  << importResult.location << ": "
                  << importResult.message << '\n';
        return 1;
    }
    std::ofstream output(argv[1], std::ios::binary | std::ios::trunc);
    output.write(encoded.data(), static_cast<std::streamsize>(encoded.size()));
    if (!output) {
        std::cerr << "could not write " << argv[1] << '\n';
        return 1;
    }
    return 0;
}
