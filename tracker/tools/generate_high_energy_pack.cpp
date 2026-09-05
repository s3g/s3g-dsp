#include "s3g/tracker/asset_pack.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <string>

using namespace s3g::tracker;

namespace {

constexpr uint8_t kKick = 36u;
constexpr uint8_t kRim = 37u;
constexpr uint8_t kSnare = 38u;
constexpr uint8_t kClap = 39u;
constexpr uint8_t kFloorTom = 41u;
constexpr uint8_t kClosedHat = 42u;
constexpr uint8_t kLowTom = 45u;
constexpr uint8_t kOpenHat = 46u;
constexpr uint8_t kHighTom = 50u;
constexpr uint8_t kRide = 51u;
constexpr uint8_t kCowbell = 56u;
constexpr std::size_t kBurstCount = 16u;

enum class Style : uint8_t {
    Techno,
    HardTechno,
    Gabber,
    House,
    Baltimore,
    Singeli,
    EastAfricanClub,
    Hybrid,
};

struct Hit {
    std::size_t row;
    uint8_t note;
    float velocity;
};

BurstDefinition burst(std::string name,
    std::initializer_list<BurstEvent> events)
{
    BurstDefinition result;
    result.name = std::move(name);
    result.eventCount = static_cast<uint8_t>(events.size());
    std::copy(events.begin(), events.end(), result.events.begin());
    return result;
}

PhraseDefinition blankPhrase(std::string name, std::size_t length)
{
    auto result = makeBlankPhrase(length);
    result.name = std::move(name);
    result.previewMidiChannel = 1u;
    return result;
}

void addHit(PhraseDefinition& phrase, std::size_t row,
    uint8_t note, float velocity)
{
    if (row >= phrase.length) return;
    auto& notes = phrase.notes[row];
    auto& velocities = phrase.velocities[row];
    velocity = std::clamp(velocity, 0.0f, 1.0f);
    if (notes.state == NoteCellState::Rest) {
        notes = NoteCell::withNote(note);
        velocities = ValueCell::withValue(velocity);
        return;
    }
    if (notes.state != NoteCellState::Note) return;
    std::array<std::pair<uint8_t, float>, kMaximumNoteVoices> voices {};
    std::size_t count = notes.noteVoiceCount();
    for (std::size_t voice = 0u; voice < count; ++voice) {
        voices[voice] = { notes.noteVoice(voice),
            velocities.state == ValueCellState::Value
                ? velocities.valueVoice(voice) : 0.78f };
        if (voices[voice].first == note) {
            voices[voice].second = std::max(voices[voice].second, velocity);
            return;
        }
    }
    if (count >= voices.size()) return;
    voices[count++] = { note, velocity };
    std::sort(voices.begin(), voices.begin() + count,
        [](const auto& left, const auto& right) {
            return left.first < right.first;
        });
    std::array<uint8_t, kMaximumNoteVoices> noteValues {};
    std::array<float, kMaximumNoteVoices> velocityValues {};
    for (std::size_t voice = 0u; voice < count; ++voice) {
        noteValues[voice] = voices[voice].first;
        velocityValues[voice] = voices[voice].second;
    }
    notes = NoteCell::withNotes(noteValues, count);
    velocities = ValueCell::withValues(velocityValues, count);
}

void addHits(PhraseDefinition& phrase, std::initializer_list<Hit> hits)
{
    for (const auto& hit : hits)
        addHit(phrase, hit.row, hit.note, hit.velocity);
}

float microTime(Style style, uint8_t note, std::size_t row) noexcept
{
    float amount = 0.5f;
    switch (style) {
    case Style::Techno:
        amount += row % 4u == 2u ? 0.006f : 0.0f;
        break;
    case Style::HardTechno:
    case Style::Gabber:
        amount += row % 2u == 0u ? -0.004f : 0.004f;
        break;
    case Style::House:
        amount += row % 2u == 1u ? 0.040f : 0.0f;
        break;
    case Style::Baltimore:
        amount += row % 4u == 2u ? 0.030f
            : row % 4u == 3u ? 0.016f : 0.0f;
        break;
    case Style::Singeli:
        amount += row % 3u == 1u ? -0.020f
            : row % 3u == 2u ? 0.024f : 0.0f;
        break;
    case Style::EastAfricanClub:
        amount += row % 4u == 1u ? -0.026f
            : row % 4u == 3u ? 0.022f : 0.0f;
        break;
    case Style::Hybrid:
        amount += row % 2u == 1u ? 0.018f : -0.006f;
        break;
    }
    if (note == kKick) amount -= 0.006f;
    else if (note == kClap || note == kSnare) amount += 0.010f;
    else if (note == kOpenHat) amount += 0.014f;
    else if (note == kCowbell) amount -= 0.010f;
    return std::clamp(amount, 0.42f, 0.58f);
}

float gateRows(uint8_t note) noexcept
{
    if (note == kOpenHat || note == kRide) return 0.72f;
    if (note == kKick) return 0.42f;
    if (note == kFloorTom || note == kLowTom || note == kHighTom) return 0.34f;
    if (note == kClap || note == kSnare) return 0.28f;
    return 0.18f;
}

void authorExpression(PhraseDefinition& phrase, Style style)
{
    auto& timing = phrase.fxPairs[0u];
    for (std::size_t row = 0u; row < phrase.length; ++row) {
        const auto& notes = phrase.notes[row];
        if (notes.state != NoteCellState::Note) continue;
        const auto count = notes.noteVoiceCount();
        std::array<float, kMaximumNoteVoices> timingValues {};
        std::array<GateVoice, kMaximumNoteVoices> gates {};
        for (std::size_t voice = 0u; voice < count; ++voice) {
            timingValues[voice] = microTime(
                style, notes.noteVoice(voice), row);
            gates[voice] = { GateVoiceMode::Rows,
                gateRows(notes.noteVoice(voice)) };
        }
        timing.actions[row] = FxActionCell::sequencer(
            SequencerAction::MicroTime);
        timing.values[row] = count == 1u
            ? FxValueCell::withValue(timingValues[0u])
            : FxValueCell::withValues(timingValues, count);
        phrase.gates[row] = GateCell::withVoices(gates, count);
    }
}

void addHatLine(PhraseDefinition& phrase, Style style,
    std::size_t variant)
{
    std::size_t step = 2u;
    if (style == Style::Gabber || style == Style::Singeli
        || style == Style::EastAfricanClub) step = 1u;
    if (style == Style::Techno && variant == 0u) step = 4u;
    for (std::size_t row = 0u; row < phrase.length; row += step) {
        bool open = false;
        if (style == Style::House || style == Style::Techno
            || style == Style::HardTechno || style == Style::Gabber)
            open = row % 8u == 6u;
        else if (style == Style::Baltimore)
            open = row % 8u == 7u;
        else
            open = (row + variant) % 11u == 7u;
        const float accent = row % 4u == 0u ? 0.82f
            : row % 2u == 0u ? 0.66f : 0.48f;
        addHit(phrase, row, open ? kOpenHat : kClosedHat,
            open ? 0.78f : accent);
    }
}

void addFourFloor(PhraseDefinition& phrase, std::size_t variant,
    float velocity)
{
    for (std::size_t row = 0u; row < phrase.length; row += 4u) {
        if (variant == 3u && row % 16u == 12u) continue;
        addHit(phrase, row, kKick,
            std::clamp(velocity - (row % 8u == 4u ? 0.05f : 0.0f),
                0.0f, 1.0f));
    }
}

void addBackbeat(PhraseDefinition& phrase, uint8_t note, float velocity)
{
    for (std::size_t base = 0u; base < phrase.length; base += 16u) {
        if (base + 4u < phrase.length) addHit(phrase, base + 4u, note, velocity);
        if (base + 12u < phrase.length)
            addHit(phrase, base + 12u, note, std::min(1.0f, velocity + 0.03f));
    }
}

void placeBurst(PhraseDefinition& phrase, std::size_t row,
    std::size_t slot)
{
    if (row >= phrase.length || slot >= kBurstDefinitionCount) return;
    phrase.notes[row] = NoteCell::withBurst(static_cast<uint8_t>(slot));
    phrase.velocities[row] = ValueCell::defaultValue();
    phrase.gates[row] = GateCell::defaultValue();
    phrase.fxPairs[0u].actions[row] = FxActionCell::empty();
    phrase.fxPairs[0u].values[row] = FxValueCell::previous();
}

PhraseDefinition makeStylePhrase(Style style, std::string name,
    std::size_t length, std::size_t variant, std::size_t burstSlot)
{
    auto phrase = blankPhrase(std::move(name), length);
    addHatLine(phrase, style, variant);
    switch (style) {
    case Style::Techno:
        addFourFloor(phrase, variant, 0.98f);
        addBackbeat(phrase, kClap, 0.82f);
        if (variant % 2u == 1u)
            for (std::size_t row = 3u; row < length; row += 8u)
                addHit(phrase, row, kLowTom, 0.62f);
        if (variant == 2u || variant == 6u)
            for (std::size_t row = 2u; row < length; row += 8u)
                addHit(phrase, row, kRide, 0.54f);
        break;
    case Style::HardTechno:
        addFourFloor(phrase, variant, 1.0f);
        addBackbeat(phrase, variant % 2u == 0u ? kClap : kSnare, 0.94f);
        for (std::size_t row = 2u + variant % 2u; row < length; row += 4u)
            addHit(phrase, row, kKick, 0.70f + 0.04f * (variant % 3u));
        for (std::size_t row = 1u; row < length; row += 4u)
            addHit(phrase, row, kRide, 0.58f);
        break;
    case Style::Gabber:
        addFourFloor(phrase, variant, 1.0f);
        addBackbeat(phrase, kSnare, 0.98f);
        for (std::size_t row = 2u; row < length; row += 4u)
            addHit(phrase, row, kKick, variant % 2u == 0u ? 0.86f : 0.72f);
        if (variant >= 4u)
            for (std::size_t row = 1u; row < length; row += 4u)
                addHit(phrase, row, kKick, 0.64f);
        break;
    case Style::House:
        addFourFloor(phrase, variant, 0.96f);
        addBackbeat(phrase, kClap, 0.92f);
        for (std::size_t row = 2u; row < length; row += 4u)
            addHit(phrase, row, kOpenHat, row % 8u == 2u ? 0.76f : 0.84f);
        if (variant % 2u == 1u)
            for (std::size_t row = 3u; row < length; row += 8u)
                addHit(phrase, row, kRim, 0.48f);
        break;
    case Style::Baltimore:
        for (std::size_t base = 0u; base < length; base += 16u) {
            const std::array<std::size_t, 6u> rows {{ 0u, 3u, 6u, 8u, 11u, 14u }};
            for (std::size_t index = 0u; index < rows.size(); ++index)
                if (base + rows[index] < length)
                    addHit(phrase, base + rows[index], kKick,
                        index % 3u == 0u ? 0.98f : 0.78f);
        }
        addBackbeat(phrase, kClap, 0.96f);
        for (std::size_t row = 1u + variant % 3u; row < length; row += 8u)
            addHit(phrase, row, kRim, 0.58f);
        break;
    case Style::Singeli:
        for (std::size_t row = 0u; row < length; ++row) {
            if ((row + variant) % 3u != 2u)
                addHit(phrase, row, row % 5u == 0u ? kKick : kLowTom,
                    row % 4u == 0u ? 0.96f : 0.68f);
            if ((row * 2u + variant) % 5u == 1u)
                addHit(phrase, row, kRim, 0.76f);
            if ((row + 2u * variant) % 7u == 3u)
                addHit(phrase, row, kCowbell, 0.70f);
        }
        addBackbeat(phrase, kClap, 0.86f);
        break;
    case Style::EastAfricanClub:
        for (std::size_t row = 0u; row < length; ++row) {
            if ((row + variant) % 4u == 0u)
                addHit(phrase, row, kKick, 0.98f);
            if ((row + variant) % 3u == 1u)
                addHit(phrase, row, kFloorTom, 0.76f);
            if ((row + 2u * variant) % 5u == 2u)
                addHit(phrase, row, kHighTom, 0.70f);
            if ((row * 3u + variant) % 7u == 4u)
                addHit(phrase, row, kCowbell, 0.74f);
        }
        addBackbeat(phrase, variant % 2u == 0u ? kClap : kRim, 0.88f);
        break;
    case Style::Hybrid:
        addFourFloor(phrase, variant, 0.99f);
        addBackbeat(phrase, variant % 2u == 0u ? kClap : kSnare, 0.94f);
        for (std::size_t row = 3u; row < length; row += 5u)
            addHit(phrase, row, variant % 2u == 0u ? kLowTom : kCowbell,
                0.66f + 0.03f * static_cast<float>(variant % 3u));
        break;
    }
    // Guarantee that every reusable phrase demonstrates polyphonic drum cells.
    if (phrase.notes[0u].noteVoiceCount() < 2u)
        addHit(phrase, 0u, kClosedHat, 0.76f);
    authorExpression(phrase, style);
    const auto burstRow = length - 1u;
    placeBurst(phrase, burstRow, burstSlot);
    return phrase;
}

void store(TrackerAssetPack& pack, std::size_t slot,
    PhraseDefinition phrase)
{
    pack.phraseBank.library.phrases[slot] = std::move(phrase);
}

bool validate(const TrackerAssetPack& pack)
{
    std::array<bool, kBurstCount> referenced {};
    std::size_t phrases = 0u;
    std::size_t microRows = 0u;
    std::size_t gateRowsAuthored = 0u;
    for (const auto& phrase : pack.phraseBank.library.phrases) {
        if (phrase.empty() || phrase.name.empty()
            || phrase.previewMidiChannel != 1u) return false;
        ++phrases;
        bool polyphonic = false;
        for (std::size_t row = 0u; row < phrase.length; ++row) {
            const auto& note = phrase.notes[row];
            polyphonic |= note.noteVoiceCount() > 1u;
            if (note.state == NoteCellState::Burst && note.note < referenced.size())
                referenced[note.note] = true;
            const auto& action = phrase.fxPairs[0u].actions[row];
            if (action.state == FxActionCellState::Sequencer
                && action.sequencerAction == SequencerAction::MicroTime)
                ++microRows;
            if (phrase.gates[row].voiceCount > 0u) ++gateRowsAuthored;
        }
        if (!polyphonic) return false;
    }
    const auto bursts = static_cast<std::size_t>(std::count_if(
        pack.burstBank.library.bursts.begin(),
        pack.burstBank.library.bursts.end(),
        [](const BurstDefinition& value) { return !value.empty(); }));
    return phrases == kPhraseLibrarySlots && bursts == kBurstCount
        && microRows >= 500u && gateRowsAuthored >= 500u
        && std::all_of(referenced.begin(), referenced.end(),
            [](bool used) { return used; });
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "usage: generate_high_energy_pack OUTPUT.s3gpack\n";
        return 2;
    }
    TrackerAssetPack pack;
    pack.name = "S3G HIGH ENERGY CLUB 01";
    pack.burstBank.id = kProjectAssetBankId;
    pack.burstBank.name = "HIGH ENERGY CLUB 01 BURSTS";
    pack.phraseBank.id = kProjectAssetBankId;
    pack.phraseBank.name = "HIGH ENERGY CLUB 01";
    pack.phraseBank.companionBurstBankId = kProjectAssetBankId;

    auto& bursts = pack.burstBank.library.bursts;
    bursts[0u] = burst("KICK TRIPLET PRESS", {
        {0u,kKick,127u,28u},{21845u,kKick,101u,24u},{43690u,kKick,116u,28u}});
    bursts[1u] = burst("KICK DOUBLE DRIVE", {
        {0u,kKick,127u,34u},{32768u,kKick,108u,30u}});
    bursts[2u] = burst("HAT TRIPLET LIFT", {
        {0u,kClosedHat,112u,18u},{21845u,kClosedHat,78u,16u},
        {43690u,kOpenHat,101u,28u}});
    bursts[3u] = burst("HAT FIVE PRESSURE", {
        {0u,kClosedHat,116u,16u},{13107u,kClosedHat,72u,14u},
        {26214u,kClosedHat,88u,14u},{39321u,kOpenHat,96u,18u},
        {52428u,kClosedHat,106u,16u}});
    bursts[4u] = burst("CLAP FLAM", {
        {0u,kClap,76u,20u},{9216u,kClap,126u,38u}});
    bursts[5u] = burst("SNARE SIX RUSH", {
        {0u,kSnare,68u,14u},{10923u,kSnare,76u,14u},
        {21845u,kSnare,86u,14u},{32768u,kSnare,96u,16u},
        {43690u,kSnare,108u,16u},{54613u,kSnare,126u,20u}});
    bursts[6u] = burst("TOM TRIPLET DROP", {
        {0u,kHighTom,104u,24u},{21845u,kLowTom,112u,26u},
        {43690u,kFloorTom,124u,30u}});
    bursts[7u] = burst("RIM FIVE CHATTER", {
        {0u,kRim,98u,14u},{12288u,kRim,70u,12u},{26624u,kRim,88u,14u},
        {40960u,kRim,78u,12u},{55296u,kRim,112u,16u}});
    bursts[8u] = burst("COWBELL THREE WEAVE", {
        {0u,kCowbell,110u,18u},{24576u,kCowbell,76u,16u},
        {49152u,kCowbell,98u,18u}});
    bursts[9u] = burst("GABBER KICK ACCEL", {
        {0u,kKick,100u,20u},{28672u,kKick,108u,20u},
        {47104u,kKick,118u,22u},{59392u,kKick,127u,24u}});
    bursts[10u] = burst("OPEN CLOSE CHOKE", {
        {0u,kOpenHat,116u,44u},{36864u,kClosedHat,94u,18u}});
    bursts[11u] = burst("BALTIMORE KICK CLAP", {
        {0u,kKick,124u,28u},{21845u,kKick,92u,22u},
        {43690u,kClap,122u,34u}});
    bursts[12u] = burst("SINGELI TOM WEAVE", {
        {0u,kLowTom,106u,20u},{16384u,kHighTom,82u,18u},
        {32768u,kFloorTom,116u,22u},{49152u,kHighTom,94u,18u}});
    bursts[13u] = burst("SINGELI RIM CHATTER", {
        {0u,kRim,112u,14u},{8192u,kRim,72u,12u},{22528u,kClap,96u,16u},
        {40960u,kRim,84u,14u},{57344u,kClap,120u,20u}});
    bursts[14u] = burst("TECHNO HAT ACCEL", {
        {0u,kClosedHat,72u,14u},{28672u,kClosedHat,84u,14u},
        {47104u,kClosedHat,100u,16u},{59392u,kOpenHat,118u,22u}});
    bursts[15u] = burst("RIDE TOM TURN", {
        {0u,kRide,112u,30u},{21845u,kHighTom,88u,20u},
        {43690u,kFloorTom,122u,28u}});

    constexpr std::array<const char*, 64u> names {{
        "TECHNO FOUR LOCK / 16", "TECHNO OFFBEAT ENGINE / 16",
        "TECHNO RIDE PRESS / 16", "TECHNO KICK DROP / 16",
        "TECHNO TWO BAR MOTION / 32", "TECHNO TRIPLET TURN / 12",
        "TECHNO HALF BAR TOOL / 8", "TECHNO PEAK LOOP / 24",
        "HARD TECHNO STOMP / 16", "HARD TECHNO RUMBLE GRID / 16",
        "HARD TECHNO RIDE CHAIN / 16", "HARD TECHNO KICK VOID / 16",
        "HARD TECHNO TWO BAR PUSH / 32", "HARD TECHNO TRIPLET PRESS / 12",
        "HARD TECHNO HALF BAR / 8", "HARD TECHNO PEAK TOOL / 24",
        "GABBER CORE DRIVE / 16", "GABBER DOUBLE KICK / 16",
        "GABBER SNARE WALL / 16", "GABBER KICK GAP / 16",
        "GABBER TWO BAR RUSH / 32", "GABBER TRIPLET POUND / 12",
        "GABBER HALF BAR TURN / 8", "GABBER MAXIMUM GRID / 24",
        "HOUSE DEEP LOCK / 16", "HOUSE OPEN HAT SWING / 16",
        "HOUSE RIM POCKET / 16", "HOUSE KICK DROP / 16",
        "HOUSE TWO BAR WALK / 32", "HOUSE TWELVE STEP SHUFFLE / 12",
        "HOUSE HALF BAR TOOL / 8", "HOUSE PEAK LIFT / 24",
        "BALTIMORE CLUB CORE / 16", "BALTIMORE KICK CUT / 16",
        "BALTIMORE CLAP BREAK / 16", "BALTIMORE RIM ANSWER / 16",
        "BALTIMORE TWO BAR RUN / 32", "BALTIMORE TRIPLET LOOP / 12",
        "BALTIMORE HALF BAR CUT / 8", "BALTIMORE BREAK PRESS / 24",
        "SINGELI INTERLOCK A / 16", "SINGELI INTERLOCK B / 16",
        "SISSO-INSPIRED TOM GRID / 16", "SISSO-INSPIRED RIM GRID / 16",
        "SINGELI TWO BAR FLIGHT / 32", "SINGELI TWELVE STEP / 12",
        "SINGELI HALF BAR RUSH / 8", "SINGELI LONG WEAVE / 24",
        "EAST AFRICAN CLUB WEAVE A / 16", "EAST AFRICAN CLUB WEAVE B / 16",
        "NYEGE-ADJACENT TOM PRESS / 16", "NYEGE-ADJACENT BELL PRESS / 16",
        "HIGH SPEED TWO BAR WEAVE / 32", "HIGH SPEED TWELVE STEP / 12",
        "HIGH SPEED HALF BAR / 8", "HIGH SPEED LONG INTERLOCK / 24",
        "CLUB HYBRID DROP ENTRY / 16", "CLUB HYBRID PRESSURE / 16",
        "CLUB HYBRID TOM TURN / 16", "CLUB HYBRID KICK VOID / 16",
        "CLUB HYBRID TWO BAR BUILD / 32", "CLUB HYBRID TRIPLET EXIT / 12",
        "CLUB HYBRID HALF BAR FILL / 8", "CLUB HYBRID FINAL TURN / 24",
    }};
    constexpr std::array<std::size_t, 8u> lengths {{
        16u, 16u, 16u, 16u, 32u, 12u, 8u, 24u,
    }};
    for (std::size_t slot = 0u; slot < names.size(); ++slot) {
        const auto group = slot / 8u;
        const auto variant = slot % 8u;
        store(pack, slot, makeStylePhrase(static_cast<Style>(group),
            names[slot], lengths[variant], variant, slot % kBurstCount));
    }

    if (!validate(pack)) {
        std::cerr << "generated high-energy pack failed coverage validation\n";
        return 1;
    }
    std::string encoded;
    const auto encodedResult = encodeTrackerAssetPack(pack, encoded);
    if (!encodedResult.ok()) {
        std::cerr << encodedResult.location << ": "
                  << encodedResult.message << '\n';
        return 1;
    }
    TrackerAssetPack decoded;
    const auto decodedResult = decodeTrackerAssetPack(encoded, decoded);
    if (!decodedResult.ok()) {
        std::cerr << decodedResult.location << ": "
                  << decodedResult.message << '\n';
        return 1;
    }
    ProjectDocument blankProject;
    AssetPackImportReport report;
    const auto imported = importTrackerAssetPack(decoded, blankProject, &report);
    if (!imported.ok() || report.burstsAdded != kBurstCount
        || report.phrasesAdded != kPhraseLibrarySlots) {
        std::cerr << "blank-project import failed at " << imported.location
                  << ": " << imported.message << '\n';
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
