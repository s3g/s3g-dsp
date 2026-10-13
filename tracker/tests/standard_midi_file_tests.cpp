#include "s3g/tracker/standard_midi_file.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace {

using namespace s3g::tracker;

int checks = 0;
int failures = 0;

void check(bool condition, const char* message)
{
    ++checks;
    if (condition) return;
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
}

uint16_t readU16(const std::string& bytes, std::size_t& offset)
{
    const auto first = static_cast<uint8_t>(bytes.at(offset++));
    const auto second = static_cast<uint8_t>(bytes.at(offset++));
    return static_cast<uint16_t>((uint16_t { first } << 8u) | second);
}

uint32_t readU32(const std::string& bytes, std::size_t& offset)
{
    uint32_t value = 0u;
    for (unsigned index = 0u; index < 4u; ++index)
        value = (value << 8u)
            | static_cast<uint8_t>(bytes.at(offset++));
    return value;
}

uint32_t readVlq(const std::string& bytes, std::size_t& offset,
    std::size_t end)
{
    uint32_t value = 0u;
    for (unsigned count = 0u; count < 4u && offset < end; ++count) {
        const auto byte = static_cast<uint8_t>(bytes.at(offset++));
        value = (value << 7u) | (byte & 0x7fu);
        if ((byte & 0x80u) == 0u) return value;
    }
    return value;
}

struct ParsedEvent {
    uint64_t tick = 0u;
    uint8_t status = 0u;
    uint8_t data1 = 0u;
    uint8_t data2 = 0u;
    uint8_t metaType = 0u;
};

struct ParsedMidi {
    uint16_t format = 0u;
    uint16_t division = 0u;
    std::vector<std::vector<ParsedEvent>> tracks;
};

ParsedMidi parse(const std::string& bytes)
{
    ParsedMidi result;
    std::size_t offset = 0u;
    check(bytes.size() >= 14u && bytes.compare(0u, 4u, "MThd") == 0,
        "SMF header signature");
    offset = 4u;
    check(readU32(bytes, offset) == 6u, "SMF header length");
    result.format = readU16(bytes, offset);
    const auto trackCount = readU16(bytes, offset);
    result.division = readU16(bytes, offset);
    for (uint16_t trackIndex = 0u; trackIndex < trackCount; ++trackIndex) {
        check(offset + 8u <= bytes.size()
                && bytes.compare(offset, 4u, "MTrk") == 0,
            "SMF track signature");
        offset += 4u;
        const auto size = readU32(bytes, offset);
        const std::size_t end = offset + size;
        check(end <= bytes.size(), "SMF track length");
        std::vector<ParsedEvent> events;
        uint64_t tick = 0u;
        while (offset < end) {
            tick += readVlq(bytes, offset, end);
            const auto status = static_cast<uint8_t>(bytes.at(offset++));
            if (status == 0xffu) {
                const auto type = static_cast<uint8_t>(bytes.at(offset++));
                const auto length = readVlq(bytes, offset, end);
                events.push_back({ tick, status, 0u, 0u, type });
                offset += length;
                continue;
            }
            ParsedEvent event;
            event.tick = tick;
            event.status = status;
            event.data1 = static_cast<uint8_t>(bytes.at(offset++));
            event.data2 = static_cast<uint8_t>(bytes.at(offset++));
            events.push_back(event);
        }
        result.tracks.push_back(std::move(events));
    }
    check(offset == bytes.size(), "SMF parser consumed file");
    return result;
}

Track track(std::string name, uint8_t channel,
    std::initializer_list<std::optional<uint8_t>> notes)
{
    Track result;
    result.name = std::move(name);
    result.midiChannel = channel;
    for (const auto note : notes)
        result.notes.push_back(note ? NoteCell::withNote(*note)
                                    : NoteCell::rest());
    result.noteColumn.length = result.notes.size();
    result.velocityColumn.length = 1u;
    result.gateColumn.length = 1u;
    result.instrumentColumn.length = 1u;
    for (auto& pair : result.fxPairs) {
        pair.actionColumn.length = 1u;
        pair.valueColumn.length = 1u;
    }
    return result;
}

ProjectDocument patternDocument()
{
    ProjectDocument document;
    document.transport = { 48000.0, 120.0, 4u, 0.5 };
    document.session.gateMilliseconds = 125.0;
    auto& pattern = *document.patternBank.activePattern();
    pattern.name = "Pattern α";
    pattern.visibleRows = 4u;
    pattern.tracks.push_back(track("LEAD", 2u,
        { uint8_t { 60u }, std::nullopt, uint8_t { 64u },
            std::nullopt }));
    return document;
}

void patternExport()
{
    const auto document = patternDocument();
    StandardMidiFileOptions options;
    StandardMidiFileSummary summary;
    std::string bytes;
    const auto result = encodeStandardMidiFile(
        document, options, bytes, &summary);
    check(result.ok(), "active Pattern encodes");
    check(summary.laneTrackCount == 1u && summary.midiEventCount == 4u
            && !summary.renderedSong && summary.durationTicks == 960u,
        "active Pattern summary retains one complete polymetric span");
    const auto midi = parse(bytes);
    check(midi.format == 1u && midi.division == 960u
            && midi.tracks.size() == 2u,
        "type-1 header contains tempo plus lane tracks");
    const auto& lane = midi.tracks[1u];
    const auto noteOn = std::find_if(lane.begin(), lane.end(),
        [](const auto& event) {
            return event.status == 0x91u && event.data1 == 60u;
        });
    check(noteOn != lane.end() && noteOn->tick == 0u,
        "lane channel and first onset are preserved");
    const auto firstOff = std::find_if(lane.begin(), lane.end(),
        [](const auto& event) {
            return event.status == 0x81u && event.data1 == 60u;
        });
    check(firstOff != lane.end() && firstOff->tick == 240u,
        "default Tracker gate is materialized as note-off");
    const auto end = std::find_if(lane.rbegin(), lane.rend(),
        [](const auto& event) { return event.metaType == 0x2fu; });
    check(end != lane.rend() && end->tick == 960u,
        "end-of-track preserves the complete Pattern duration");

    std::string repeat;
    check(encodeStandardMidiFile(document, options, repeat).ok()
            && repeat == bytes,
        "same document and playback seed export deterministically");
}

void songExport()
{
    auto document = patternDocument();
    document.patternBank.entries.front().id = "P0";
    document.patternBank.activePatternId = "P0";
    document.patternBank.entries.front().pattern.tracks.front()
        = track("VOICE", 1u, { uint8_t { 60u } });
    PatternBankEntry second = document.patternBank.entries.front();
    second.id = "P1";
    second.pattern.name = "SLOW";
    second.pattern.tracks.front().notes.front()
        = NoteCell::withNote(72u);
    document.patternBank.entries.push_back(std::move(second));
    document.song.name = "TEMPO SONG";
    document.song.loop = true;
    document.song.rows.clear();
    SongRow first;
    first.patternId = "P0";
    first.durationTicks = 2u;
    first.tempoMultiplier = 1.0;
    document.song.rows.push_back(first);
    SongRow slow = first;
    slow.patternId = "P1";
    slow.tempoMultiplier = 0.5;
    document.song.rows.push_back(slow);

    StandardMidiFileOptions options;
    options.scope = StandardMidiFileScope::Song;
    StandardMidiFileSummary summary;
    std::string bytes;
    const auto result = encodeStandardMidiFile(
        document, options, bytes, &summary);
    check(result.ok(), "Song encodes");
    check(summary.renderedSong && summary.durationTicks == 960u,
        "looping Song exports one finite pass");
    const auto midi = parse(bytes);
    const auto tempoCount = std::count_if(
        midi.tracks.front().begin(), midi.tracks.front().end(),
        [](const auto& event) { return event.metaType == 0x51u; });
    check(tempoCount == 2u,
        "Song tempo-multiplier transition becomes an SMF tempo map");
    check(std::any_of(midi.tracks[1u].begin(), midi.tracks[1u].end(),
            [](const auto& event) {
                return (event.status & 0xf0u) == 0x90u
                    && event.data1 == 72u && event.tick == 480u;
            }),
        "Song pattern transition retains the next Pattern notes");
}

void invalidSongFailsWithoutReplacingDestination()
{
    const auto document = patternDocument();
    StandardMidiFileOptions options;
    options.scope = StandardMidiFileScope::Song;
    std::string bytes = "keep";
    const auto result = encodeStandardMidiFile(document, options, bytes);
    check(!result.ok() && bytes == "keep",
        "empty Song export fails transactionally");
}

} // namespace

int main()
{
    patternExport();
    songExport();
    invalidSongFailsWithoutReplacingDestination();
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
