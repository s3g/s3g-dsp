#include "s3g/tracker/asset_pack.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <string>

using namespace s3g::tracker;

namespace {

BurstDefinition makeBurst(std::string name,
    std::initializer_list<BurstEvent> events)
{
    BurstDefinition burst;
    burst.name = std::move(name);
    burst.eventCount = static_cast<uint8_t>(events.size());
    std::copy(events.begin(), events.end(), burst.events.begin());
    return burst;
}

struct DrumHit {
    uint8_t note;
    float velocity;
};

void hits(PhraseDefinition& phrase, std::size_t row,
    std::initializer_list<DrumHit> source)
{
    std::array<uint8_t, kMaximumNoteVoices> notes {};
    std::array<float, kMaximumNoteVoices> velocities {};
    const auto count = std::min<std::size_t>(
        source.size(), kMaximumNoteVoices);
    std::size_t voice = 0u;
    for (const auto& hit : source) {
        if (voice >= count) break;
        notes[voice] = hit.note;
        velocities[voice] = hit.velocity;
        ++voice;
    }
    phrase.notes[row] = NoteCell::withNotes(notes,
        static_cast<uint8_t>(count));
    phrase.velocities[row] = ValueCell::withValues(velocities,
        static_cast<uint8_t>(count));
}

PhraseDefinition phrase(std::string name, std::size_t length)
{
    auto result = makeBlankPhrase(length);
    result.name = std::move(name);
    result.previewMidiChannel = 1u;
    return result;
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
    pack.burstLibrary.bursts[0u] = makeBurst("CLOSED HAT TRIPLET", {
        { 0u, 42u, 112u, 30u }, { 21845u, 42u, 88u, 26u },
        { 43690u, 42u, 101u, 28u },
    });
    pack.burstLibrary.bursts[1u] = makeBurst("SNARE RUFF", {
        { 0u, 38u, 72u, 24u }, { 12288u, 37u, 86u, 20u },
        { 32768u, 38u, 122u, 44u },
    });
    pack.burstLibrary.bursts[2u] = makeBurst("KICK DRAG", {
        { 0u, 36u, 118u, 34u }, { 24576u, 36u, 82u, 28u },
        { 49152u, 36u, 108u, 38u },
    });
    pack.burstLibrary.bursts[3u] = makeBurst("HAT FIVE CUT", {
        { 0u, 42u, 112u, 22u }, { 13107u, 42u, 76u, 20u },
        { 26214u, 46u, 92u, 18u }, { 39321u, 42u, 70u, 20u },
        { 52428u, 42u, 98u, 24u },
    });

    auto boomBap = phrase("BOOM BAP CORE / 16", 16u);
    hits(boomBap, 0u, { { 36u, 0.98f }, { 42u, 0.82f } });
    hits(boomBap, 2u, { { 42u, 0.62f } });
    hits(boomBap, 4u, { { 38u, 0.94f }, { 42u, 0.84f } });
    hits(boomBap, 6u, { { 36u, 0.78f }, { 42u, 0.64f } });
    hits(boomBap, 8u, { { 36u, 0.88f }, { 42u, 0.84f } });
    hits(boomBap, 10u, { { 36u, 0.90f }, { 42u, 0.62f } });
    hits(boomBap, 12u, { { 38u, 1.0f }, { 42u, 0.88f } });
    hits(boomBap, 14u, { { 46u, 0.76f } });
    boomBap.notes[15u] = NoteCell::withBurst(0u);
    pack.phraseLibrary.phrases[0u] = std::move(boomBap);

    auto breakPush = phrase("BREAKBEAT PUSH / 16", 16u);
    hits(breakPush, 0u, { { 36u, 1.0f }, { 42u, 0.86f } });
    hits(breakPush, 2u, { { 42u, 0.63f } });
    hits(breakPush, 3u, { { 36u, 0.76f } });
    hits(breakPush, 4u, { { 38u, 0.96f }, { 42u, 0.82f } });
    hits(breakPush, 6u, { { 36u, 0.84f }, { 42u, 0.65f } });
    breakPush.notes[7u] = NoteCell::withBurst(1u);
    hits(breakPush, 8u, { { 36u, 0.92f }, { 42u, 0.84f } });
    hits(breakPush, 10u, { { 42u, 0.62f } });
    hits(breakPush, 11u, { { 36u, 0.74f } });
    hits(breakPush, 12u, { { 38u, 1.0f }, { 46u, 0.78f } });
    hits(breakPush, 14u, { { 36u, 0.86f }, { 42u, 0.66f } });
    breakPush.notes[15u] = NoteCell::withBurst(3u);
    pack.phraseLibrary.phrases[1u] = std::move(breakPush);

    auto halfTime = phrase("HALF-TIME HAT TURN / 16", 16u);
    hits(halfTime, 0u, { { 36u, 0.98f }, { 42u, 0.82f } });
    hits(halfTime, 2u, { { 42u, 0.60f } });
    hits(halfTime, 4u, { { 36u, 0.72f }, { 42u, 0.78f } });
    hits(halfTime, 6u, { { 46u, 0.68f } });
    hits(halfTime, 8u, { { 38u, 1.0f }, { 42u, 0.88f } });
    hits(halfTime, 10u, { { 36u, 0.82f }, { 42u, 0.62f } });
    hits(halfTime, 12u, { { 37u, 0.68f }, { 42u, 0.80f } });
    hits(halfTime, 14u, { { 41u, 0.76f }, { 46u, 0.72f } });
    halfTime.notes[15u] = NoteCell::withBurst(3u);
    pack.phraseLibrary.phrases[2u] = std::move(halfTime);

    auto oddKit = phrase("ODD KIT CYCLE / 15", 15u);
    hits(oddKit, 0u, { { 36u, 0.98f }, { 42u, 0.84f } });
    hits(oddKit, 2u, { { 42u, 0.62f } });
    hits(oddKit, 4u, { { 38u, 0.94f }, { 42u, 0.82f } });
    hits(oddKit, 6u, { { 36u, 0.76f } });
    hits(oddKit, 7u, { { 42u, 0.66f } });
    hits(oddKit, 9u, { { 36u, 0.88f }, { 46u, 0.74f } });
    hits(oddKit, 11u, { { 38u, 1.0f }, { 42u, 0.84f } });
    hits(oddKit, 13u, { { 41u, 0.78f }, { 42u, 0.62f } });
    oddKit.notes[14u] = NoteCell::withBurst(0u);
    pack.phraseLibrary.phrases[3u] = std::move(oddKit);

    auto tomFill = phrase("FLOOR TOM TURN / 8", 8u);
    hits(tomFill, 0u, { { 36u, 0.92f }, { 42u, 0.78f } });
    hits(tomFill, 2u, { { 41u, 0.72f }, { 42u, 0.62f } });
    hits(tomFill, 4u, { { 38u, 0.94f }, { 46u, 0.74f } });
    hits(tomFill, 5u, { { 41u, 0.82f } });
    hits(tomFill, 6u, { { 37u, 0.68f }, { 41u, 0.96f } });
    tomFill.notes[7u] = NoteCell::withBurst(2u);
    pack.phraseLibrary.phrases[4u] = std::move(tomFill);

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
    if (!importResult.ok() || importReport.burstsAdded != 4u
        || importReport.phrasesAdded != 5u) {
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
