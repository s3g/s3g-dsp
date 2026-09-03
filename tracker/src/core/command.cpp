#include "s3g/tracker/command.h"
#include "s3g/tracker/fx_catalog.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <initializer_list>
#include <limits>
#include <sstream>
#include <utility>
#include <vector>

namespace s3g::tracker {
namespace {

constexpr std::size_t kMaximumRows = 256u;
constexpr double kMinimumSwing = 0.5;
constexpr double kMaximumSwing = 0.75;
constexpr double kMinimumGateMilliseconds = 1.0;
constexpr double kMaximumGateMilliseconds = 5000.0;

enum class ColumnTargetKind : uint8_t {
    Note,
    Instrument,
    Velocity,
    FxAction,
    FxValue,
};

struct ColumnTarget {
    ColumnTargetKind kind = ColumnTargetKind::Note;
    std::size_t fxIndex = 0u;
};

struct KitLane {
    const char* token;
    const char* name;
    uint8_t gmNote;
    uint8_t superiorNote;
};

struct InstrumentSlot {
    const char* name;
    uint32_t nodeId;
};

constexpr std::array<InstrumentSlot, 3u>
    kInstrumentSlots {{
        { "kick", 0u },
        { "sampler", kStereoSamplerInstrumentNode },
        { "midi", kMidiOutInstrumentNode },
    }};

CommandResult success(std::string message,
    CommandEffect effects = CommandEffect::None)
{
    return { true, effects, std::move(message), std::nullopt };
}

CommandResult failure(std::string message)
{
    return { false, CommandEffect::None, std::move(message), std::nullopt };
}

std::string asciiLower(std::string_view value)
{
    std::string result(value);
    for (auto& character : result) {
        if (character >= 'A' && character <= 'Z')
            character = static_cast<char>(character - 'A' + 'a');
    }
    return result;
}

std::vector<std::string> tokenize(std::string_view command)
{
    std::vector<std::string> tokens;
    std::size_t cursor = 0u;
    while (cursor < command.size()) {
        while (cursor < command.size()
            && (command[cursor] == ' ' || command[cursor] == '\t'
                || command[cursor] == '\r' || command[cursor] == '\n'))
            ++cursor;
        const auto begin = cursor;
        while (cursor < command.size()
            && command[cursor] != ' ' && command[cursor] != '\t'
            && command[cursor] != '\r' && command[cursor] != '\n')
            ++cursor;
        if (cursor > begin)
            tokens.emplace_back(command.substr(begin, cursor - begin));
    }
    return tokens;
}

template <typename Integer>
bool parseUnsigned(std::string_view token, Integer& output)
{
    static_assert(std::numeric_limits<Integer>::is_integer,
        "parseUnsigned requires an integer type");
    if (token.empty() || token.front() == '-' || token.front() == '+')
        return false;
    uint64_t value = 0u;
    const auto result = std::from_chars(token.data(),
        token.data() + token.size(), value);
    if (result.ec != std::errc() || result.ptr != token.data() + token.size()
        || value > static_cast<uint64_t>(
               std::numeric_limits<Integer>::max()))
        return false;
    output = static_cast<Integer>(value);
    return true;
}

bool parseSigned(std::string_view token, int64_t& output)
{
    if (token.empty()) return false;
    const auto result = std::from_chars(token.data(),
        token.data() + token.size(), output);
    return result.ec == std::errc()
        && result.ptr == token.data() + token.size();
}

bool parseFiniteDouble(std::string_view token, double& output)
{
    if (token.empty()) return false;
    std::string storage(token);
    char* end = nullptr;
    const double value = std::strtod(storage.c_str(), &end);
    if (end != storage.c_str() + storage.size() || !std::isfinite(value))
        return false;
    output = value;
    return true;
}

bool normalizeAliasName(std::string_view token, std::string& output)
{
    if (!token.empty() && token.front() == '@') token.remove_prefix(1u);
    if (token.empty()) return false;
    const auto isLetter = [](char value) {
        return (value >= 'a' && value <= 'z')
            || (value >= 'A' && value <= 'Z');
    };
    const auto isDigit = [](char value) {
        return value >= '0' && value <= '9';
    };
    if (!isLetter(token.front())) return false;
    if (!std::all_of(token.begin(), token.end(), [&](char value) {
            return isLetter(value) || isDigit(value) || value == '_';
        }))
        return false;
    output = asciiLower(token);
    return true;
}

bool isAliasReference(std::string_view token)
{
    return !token.empty() && token.front() == '@';
}

bool parseLane(const TrackerSession& session, std::string_view token,
    std::size_t& zeroBasedLane, std::string& error)
{
    if (isAliasReference(token)) {
        std::string alias;
        if (!normalizeAliasName(token, alias)) {
            error = "Alias names use letters, digits, and underscore and must start with a letter.";
            return false;
        }
        const auto found = session.aliases.find(alias);
        if (found == session.aliases.end()) {
            error = "Unknown lane alias @" + alias + ". Use aliases to inspect the current map.";
            return false;
        }
        if (found->second >= session.pattern.tracks.size()) {
            error = "Alias @" + alias + " points to a lane that no longer exists.";
            return false;
        }
        zeroBasedLane = found->second;
        return true;
    }

    std::size_t lane = 0u;
    if (!parseUnsigned(token, lane) || lane == 0u) {
        error = "Lane must be a positive, one-based number or an @alias.";
        return false;
    }
    if (lane > session.pattern.tracks.size()) {
        std::ostringstream stream;
        stream << "Lane " << lane << " does not exist; this pattern has "
               << session.pattern.tracks.size() << " lane"
               << (session.pattern.tracks.size() == 1u ? "." : "s.");
        error = stream.str();
        return false;
    }
    zeroBasedLane = lane - 1u;
    return true;
}

bool parseRow(std::string_view token, std::size_t& zeroBasedRow,
    std::string& error)
{
    std::size_t row = 0u;
    if (!parseUnsigned(token, row) || row == 0u || row > kMaximumRows) {
        error = "Row must be between 1 and 256.";
        return false;
    }
    zeroBasedRow = row - 1u;
    return true;
}

bool parseInstrumentSlot(std::string_view token, uint32_t& nodeId,
    std::string& canonicalName)
{
    const auto value = asciiLower(token);
    uint32_t numeric = 0u;
    if (parseUnsigned(value, numeric)) {
        if (numeric >= kInstrumentSlots.size()) return false;
        nodeId = kInstrumentSlots[numeric].nodeId;
        canonicalName = kInstrumentSlots[numeric].name;
        return true;
    }

    auto name = value;
    if (name == "bd" || name == "kick" || name == "membrane")
        name = "kick";
    else if (name == "sample" || name == "slice") name = "sampler";
    else if (name == "midiout" || name == "midi_out") name = "midi";
    for (const auto& slot : kInstrumentSlots) {
        if (name != slot.name) continue;
        nodeId = slot.nodeId;
        canonicalName = slot.name;
        return true;
    }
    return false;
}

void ensureNoteStorage(TrackerSession& session, Track& track,
    std::size_t size)
{
    if (track.notes.size() < size) track.notes.resize(size, NoteCell::rest());
    session.pattern.visibleRows = std::max(session.pattern.visibleRows, size);
}

void ensureVelocityStorage(TrackerSession& session, Track& track,
    std::size_t size)
{
    if (track.velocities.size() < size)
        track.velocities.resize(size, ValueCell::defaultValue());
    session.pattern.visibleRows = std::max(session.pattern.visibleRows, size);
}

void ensureInstrumentStorage(TrackerSession& session, Track& track,
    std::size_t size)
{
    if (track.instruments.size() < size)
        track.instruments.resize(size, InstrumentCell::empty());
    session.pattern.visibleRows = std::max(session.pattern.visibleRows, size);
}

void ensureFxStorage(TrackerSession& session, FxPair& pair,
    bool action, std::size_t size)
{
    if (action && pair.actions.size() < size)
        pair.actions.resize(size, FxActionCell::empty());
    if (!action && pair.values.size() < size)
        pair.values.resize(size, FxValueCell::previous());
    session.pattern.visibleRows = std::max(session.pattern.visibleRows, size);
}

void ensureDefaultFxColumns(Track& track, std::size_t rows)
{
    for (auto& pair : track.fxPairs) {
        if (pair.actions.size() < rows)
            pair.actions.resize(rows, FxActionCell::empty());
        if (pair.values.size() < rows)
            pair.values.resize(rows, FxValueCell::previous());
        if (pair.actionColumn.length == 0u)
            pair.actionColumn.length = rows;
        if (pair.valueColumn.length == 0u)
            pair.valueColumn.length = rows;
    }
}

uint8_t fallbackNoteForLane(std::size_t lane)
{
    constexpr std::array<uint8_t, 8u> drumNotes {
        36u, 38u, 42u, 46u, 39u, 45u, 51u, 49u,
    };
    if (lane < drumNotes.size()) return drumNotes[lane];
    return static_cast<uint8_t>(std::min<std::size_t>(127u, 52u + lane));
}

std::string midiNoteNameText(uint8_t note)
{
    constexpr std::array<const char*, 12u> names {
        "C-", "C#", "D-", "D#", "E-", "F-",
        "F#", "G-", "G#", "A-", "A#", "B-",
    };
    return std::string(names[note % 12u])
        + std::to_string(static_cast<int>(note) / 12 - 1);
}

void ensureLaneDefaultNotes(TrackerSession& session)
{
    const auto previousSize = session.laneDefaultNotes.size();
    session.laneDefaultNotes.resize(session.pattern.tracks.size());
    for (std::size_t lane = previousSize;
         lane < session.laneDefaultNotes.size(); ++lane)
        session.laneDefaultNotes[lane] = fallbackNoteForLane(lane);
}

uint8_t anchorNote(const TrackerSession& session, std::size_t lane)
{
    return laneDefaultNote(session, lane);
}

void addAliasesForToken(TrackerSession& session, std::string_view token,
    std::size_t lane)
{
    const auto value = asciiLower(token);
    session.aliases[value] = lane;
    if (value == "kik") {
        session.aliases["k"] = lane;
        session.aliases["kick"] = lane;
        session.aliases["bd"] = lane;
    } else if (value == "snr") {
        session.aliases["s"] = lane;
        session.aliases["sn"] = lane;
        session.aliases["snare"] = lane;
    } else if (value == "chh") {
        session.aliases["h"] = lane;
        session.aliases["hh"] = lane;
        session.aliases["hat"] = lane;
    } else if (value == "ohh") {
        session.aliases["o"] = lane;
        session.aliases["oh"] = lane;
        session.aliases["open"] = lane;
    } else if (value == "lt") {
        session.aliases["low"] = lane;
    } else if (value == "mt") {
        session.aliases["mid"] = lane;
    } else if (value == "ht") {
        session.aliases["high"] = lane;
    } else if (value == "ft") {
        session.aliases["floor"] = lane;
    } else if (value == "cr1") {
        session.aliases["c"] = lane;
        session.aliases["cr"] = lane;
        session.aliases["crash"] = lane;
    } else if (value == "rd1") {
        session.aliases["r"] = lane;
        session.aliases["rd"] = lane;
        session.aliases["ride"] = lane;
    }
}

std::vector<KitLane> kitTemplate(std::string_view name)
{
    if (name == "compact") {
        return {
            { "KIK", "Kick", 36u, 36u },
            { "SNR", "Snare", 38u, 38u },
            { "CHH", "Closed Hat", 42u, 61u },
            { "OHH", "Open Hat", 46u, 46u },
        };
    }
    if (name == "basic") {
        return {
            { "KIK", "Kick", 36u, 36u },
            { "SNR", "Snare", 38u, 38u },
            { "LT", "Low Tom", 45u, 41u },
            { "CHH", "Closed Hat", 42u, 61u },
            { "OHH", "Open Hat", 46u, 46u },
            { "CR1", "Crash", 49u, 49u },
            { "RD1", "Ride", 51u, 51u },
        };
    }
    if (name == "toms") {
        return {
            { "LT", "Low Tom", 45u, 41u },
            { "MT", "Mid Tom", 47u, 45u },
            { "HT", "High Tom", 50u, 48u },
            { "FT", "Floor Tom", 41u, 43u },
        };
    }
    return {};
}

void configureKit(TrackerSession& session, std::string_view mapName,
    std::string_view templateName)
{
    const auto lanes = kitTemplate(templateName);
    const bool superior = mapName == "superior";
    const auto channel = static_cast<uint8_t>(superior ? 1u : 10u);

    while (session.pattern.tracks.size() < lanes.size()) {
        Track track;
        const auto rows = std::max<std::size_t>(session.pattern.visibleRows, 1u);
        track.notes.resize(rows, NoteCell::rest());
        track.velocities.resize(rows, ValueCell::defaultValue());
        track.noteColumn.length = rows;
        track.velocityColumn.length = rows;
        ensureDefaultFxColumns(track, rows);
        session.pattern.tracks.push_back(std::move(track));
    }
    ensureLaneDefaultNotes(session);
    session.aliases.clear();

    for (std::size_t lane = 0u; lane < lanes.size(); ++lane) {
        const auto& kitLane = lanes[lane];
        const auto pitch = superior ? kitLane.superiorNote : kitLane.gmNote;
        auto& track = session.pattern.tracks[lane];
        track.name = kitLane.name;
        track.midiChannel = channel;
        track.initialInstrumentNodeId = kMidiOutInstrumentNode;
        track.destination = EventDestination::Midi;
        track.noteColumn.muted = false;
        ensureDefaultFxColumns(track,
            std::max<std::size_t>(session.pattern.visibleRows, 1u));
        session.laneDefaultNotes[lane] = pitch;
        for (auto& cell : track.notes) {
            if (cell.state == NoteCellState::Note) cell.note = pitch;
        }
        addAliasesForToken(session, kitLane.token, lane);
    }
    for (std::size_t lane = lanes.size();
         lane < session.pattern.tracks.size(); ++lane)
        session.pattern.tracks[lane].noteColumn.muted = true;
}

void loadDemo(TrackerSession& session)
{
    struct DemoLane {
        const char* name;
        const char* token;
        uint8_t note;
        const char* mask;
        std::size_t noteLength;
        std::size_t velocityLength;
        Direction direction;
    };
    constexpr std::array<DemoLane, 8u> lanes {{
        { "Kick", "KIK", 36u, "x---x---x---x---", 15u, 16u,
            Direction::Forward },
        { "Snare", "SNR", 38u, "----x-------x---", 16u, 11u,
            Direction::Forward },
        { "Closed Hat", "CHH", 42u, "x-x-x-x-x-x-x-x-", 13u, 7u,
            Direction::Forward },
        { "Open Hat", "OHH", 46u, "------x-------x-", 7u, 5u,
            Direction::Palindrome },
        { "Slice Track", "SMP", 48u, "x--x--x---x--x--", 11u, 13u,
            Direction::Forward },
        { "Toms", "LT", 45u, "x---x---x-------", 9u, 9u,
            Direction::Reverse },
        { "Ride", "RD1", 51u, "x---x---x---x---", 5u, 4u,
            Direction::Palindrome },
        { "Crash", "CR1", 49u, "x---------------", 16u, 16u,
            Direction::Random },
    }};

    Pattern demo;
    demo.name = "MIDI drum test";
    demo.visibleRows = 16u;
    demo.tracks.reserve(lanes.size());
    for (std::size_t laneIndex = 0u; laneIndex < lanes.size(); ++laneIndex) {
        const auto& source = lanes[laneIndex];
        Track track;
        track.name = source.name;
        track.midiChannel = 10u;
        track.notes.reserve(16u);
        track.velocities.reserve(16u);
        for (std::size_t row = 0u; row < 16u; ++row) {
            track.notes.push_back(source.mask[row] == 'x'
                    ? NoteCell::withNote(source.note)
                    : NoteCell::rest());
            const bool accent = (row % 4u) == 0u;
            track.velocities.push_back(ValueCell::withValue(
                accent ? 1.0f : 0.72f));
        }
        track.noteColumn.length = source.noteLength;
        track.noteColumn.direction = source.direction;
        track.velocityColumn.length = source.velocityLength;
        ensureDefaultFxColumns(track, 16u);
        track.initialInstrumentNodeId = laneIndex == 0u ? 0u
            : laneIndex == 4u ? kStereoSamplerInstrumentNode
                              : kMidiOutInstrumentNode;
        track.destination = destinationForInstrument(
            track.initialInstrumentNodeId, EventDestination::None);
        if (laneIndex == 0u) {
            auto& tune = track.fxPairs[0u];
            tune.actions[0u] = FxActionCell::parameter(3u);
            tune.values[0u] = FxValueCell::withValue(0.28f);
            tune.actions[4u] = FxActionCell::previous();
            tune.values[4u] = FxValueCell::withValue(0.40f);
            tune.actions[8u] = FxActionCell::previous();
            tune.values[8u] = FxValueCell::withValue(0.22f);
            tune.actions[12u] = FxActionCell::previous();
            tune.values[12u] = FxValueCell::withValue(0.34f);
            auto& decay = track.fxPairs[1u];
            decay.actions[0u] = FxActionCell::parameter(6u);
            decay.values[0u] = FxValueCell::withValue(0.22f);
            decay.actions[8u] = FxActionCell::previous();
            decay.values[8u] = FxValueCell::withValue(0.36f);
        }
        demo.tracks.push_back(std::move(track));
    }

    session.pattern = std::move(demo);
    session.transport.bpm = 126.0;
    session.transport.swing = 0.56;
    session.selectedTrack = 0u;
    session.selectedRow = 0u;
    session.aliases.clear();
    session.laneDefaultNotes.clear();
    ensureLaneDefaultNotes(session);
    for (std::size_t lane = 0u; lane < lanes.size(); ++lane) {
        session.laneDefaultNotes[lane] = lanes[lane].note;
        addAliasesForToken(session, lanes[lane].token, lane);
    }
}

std::string laneLabel(const TrackerSession& session, std::size_t lane)
{
    std::ostringstream stream;
    stream << "lane " << (lane + 1u);
    if (!session.pattern.tracks[lane].name.empty())
        stream << " (" << session.pattern.tracks[lane].name << ')';
    return stream.str();
}

std::string aliasesText(const TrackerSession& session)
{
    if (session.aliases.empty())
        return "No aliases. Use autoalias, kit, or alias <name> <lane>.";
    std::vector<std::vector<std::string>> aliasesByLane(
        session.pattern.tracks.size());
    for (const auto& [name, lane] : session.aliases) {
        if (lane < aliasesByLane.size())
            aliasesByLane[lane].push_back(name);
    }
    std::ostringstream stream;
    stream << "Aliases by lane:";
    for (std::size_t lane = 0u; lane < aliasesByLane.size(); ++lane) {
        if (aliasesByLane[lane].empty()) continue;
        stream << "\n  Lane " << (lane + 1u);
        const auto& name = session.pattern.tracks[lane].name;
        if (!name.empty()) stream << " (" << name << ')';
        stream << ':';
        for (const auto& alias : aliasesByLane[lane])
            stream << " @" << alias;
    }
    return stream.str();
}

std::string automaticAliasStem(std::string_view laneName,
    std::size_t lane)
{
    constexpr std::size_t kMaximumAliasBytes = 64u;
    std::string stem;
    stem.reserve(std::min(laneName.size(), kMaximumAliasBytes));
    bool started = false;
    for (char value : laneName) {
        const bool letter = (value >= 'a' && value <= 'z')
            || (value >= 'A' && value <= 'Z');
        const bool digit = value >= '0' && value <= '9';
        if (!started && !letter) continue;
        if (!letter && !digit) continue;
        started = true;
        if (value >= 'A' && value <= 'Z')
            value = static_cast<char>(value - 'A' + 'a');
        stem.push_back(value);
        if (stem.size() == kMaximumAliasBytes) break;
    }
    if (!stem.empty()) return stem;
    return "lane" + std::to_string(lane + 1u);
}

std::map<std::string, std::size_t> automaticAliases(
    const TrackerSession& session)
{
    constexpr std::size_t kMaximumAliasBytes = 64u;
    std::map<std::string, std::size_t> aliases;
    std::vector<std::string> used;
    used.reserve(session.pattern.tracks.size());
    for (std::size_t lane = 0u; lane < session.pattern.tracks.size(); ++lane) {
        const bool named = std::any_of(
            session.pattern.tracks[lane].name.begin(),
            session.pattern.tracks[lane].name.end(), [](char value) {
                return (value >= 'a' && value <= 'z')
                    || (value >= 'A' && value <= 'Z');
            });
        const std::string stem = automaticAliasStem(
            session.pattern.tracks[lane].name, lane);
        std::string alias;
        if (!named) {
            alias = stem;
        } else {
            for (std::size_t length = 1u; length <= stem.size(); ++length) {
                const std::string candidate = stem.substr(0u, length);
                if (std::find(used.begin(), used.end(), candidate)
                        == used.end()) {
                    alias = candidate;
                    break;
                }
            }
        }
        if (alias.empty()
            || std::find(used.begin(), used.end(), alias) != used.end()) {
            const std::string suffix = std::to_string(lane + 1u);
            alias = stem.substr(0u, kMaximumAliasBytes - suffix.size())
                + suffix;
        }
        used.push_back(alias);
        aliases.emplace(std::move(alias), lane);
    }
    return aliases;
}

bool parseColumnTarget(std::string_view token, ColumnTarget& field)
{
    const auto value = asciiLower(token);
    if (value == "note" || value == "notes" || value == "n") {
        field = { ColumnTargetKind::Note, 0u };
        return true;
    }
    if (value == "ins" || value == "inst" || value == "instrument"
        || value == "instruments") {
        field = { ColumnTargetKind::Instrument, 0u };
        return true;
    }
    if (value == "vel" || value == "vol" || value == "velocity"
        || value == "v") {
        field = { ColumnTargetKind::Velocity, 0u };
        return true;
    }
    if (value.size() == 3u && value[0] == 'f' && value[1] == 'x') {
        std::size_t oneBased = 0u;
        if (parseUnsigned(std::string_view(value).substr(2u), oneBased)
            && oneBased >= 1u && oneBased <= kFxPairCount) {
            field = { ColumnTargetKind::FxAction, oneBased - 1u };
            return true;
        }
    }
    if (value.size() == 2u && value[0] == 'v') {
        std::size_t oneBased = 0u;
        if (parseUnsigned(std::string_view(value).substr(1u), oneBased)
            && oneBased >= 1u && oneBased <= kFxPairCount) {
            field = { ColumnTargetKind::FxValue, oneBased - 1u };
            return true;
        }
    }
    return false;
}

std::string columnName(ColumnTarget field)
{
    if (field.kind == ColumnTargetKind::Note) return "NOTE";
    if (field.kind == ColumnTargetKind::Instrument) return "INS";
    if (field.kind == ColumnTargetKind::Velocity) return "VEL";
    return (field.kind == ColumnTargetKind::FxAction ? "FX" : "V")
        + std::to_string(field.fxIndex + 1u);
}

bool parseWarpOptions(const std::vector<std::string>& tokens,
    std::size_t first, TimingWarpOptions& options, std::string& error)
{
    std::size_t index = first;
    while (index < tokens.size()) {
        const auto option = asciiLower(tokens[index]);
        if (option == "mix" || option == "alpha") {
            if (index + 1u >= tokens.size()) {
                error = "Warp mix requires a normalized value from 0 to 1.";
                return false;
            }
            double value = 0.0;
            if (!parseFiniteDouble(tokens[index + 1u], value)
                || value < 0.0 || value > 1.0) {
                error = "Warp mix must be between 0 and 1.";
                return false;
            }
            options.alpha = value;
            index += 2u;
            continue;
        }
        if (option == "segment" || option == "seg") {
            if (index + 2u >= tokens.size()) {
                error = "Warp segment requires <begin> <end>.";
                return false;
            }
            double begin = 0.0;
            double end = 0.0;
            if (!parseFiniteDouble(tokens[index + 1u], begin)
                || !parseFiniteDouble(tokens[index + 2u], end)
                || begin < 0.0 || end > 1.0 || begin >= end) {
                error = "Warp segment must satisfy 0 <= begin < end <= 1.";
                return false;
            }
            options.phaseBegin = begin;
            options.phaseEnd = end;
            index += 3u;
            continue;
        }
        if (option == "repeat" || option == "repeats"
            || option == "reps") {
            if (index + 1u >= tokens.size()) {
                error = "Warp repeat requires a positive subdivision count.";
                return false;
            }
            uint32_t repetitions = 0u;
            if (!parseUnsigned(tokens[index + 1u], repetitions)
                || repetitions == 0u
                || repetitions > kMaximumLiveWarpRepetitions) {
                error = "Warp repeat must be between 1 and "
                    + std::to_string(kMaximumLiveWarpRepetitions) + ".";
                return false;
            }
            options.repetitions = repetitions;
            index += 2u;
            continue;
        }
        error = "Unknown warp option \"" + tokens[index]
            + "\"; use mix, segment, or repeat.";
        return false;
    }
    return true;
}

std::string timingWarpsText(const TrackerSession& session)
{
    const auto& stack = session.transport.timingWarp;
    std::ostringstream stream;
    stream << "Warp mode "
           << (session.transport.timingWarpEnabled ? "ON" : "OFF")
           << "; cycle " << session.transport.warpCycleTicks
           << " ticks; " << stack.size() << " transform";
    if (stack.size() != 1u) stream << 's';
    stream << '.';
    for (std::size_t index = 0u; index < stack.size(); ++index) {
        const auto* transform = stack.transform(index);
        if (!transform) continue;
        stream << " [" << (index + 1u) << ':';
        switch (transform->kind) {
        case TimingWarpKind::Exponential:
            stream << " exp " << transform->exponent;
            break;
        case TimingWarpKind::StepQuantize:
            stream << " step " << transform->steps;
            break;
        case TimingWarpKind::EuclideanQuantize:
            stream << " eu " << transform->pulses << '/'
                   << transform->steps;
            break;
        }
        const auto& options = transform->options;
        stream << " mix " << options.alpha;
        if (options.phaseBegin != 0.0 || options.phaseEnd != 1.0)
            stream << " seg " << options.phaseBegin << '-'
                   << options.phaseEnd;
        if (options.repetitions != 1u)
            stream << " repeat " << options.repetitions;
        stream << ']';
    }
    return stream.str();
}

bool parseWarpLibraryIndex(std::string_view token,
    std::size_t& zeroBased) noexcept
{
    std::size_t oneBased = 0u;
    if (!parseUnsigned(token, oneBased) || oneBased == 0u
        || oneBased > kMaximumTimingWarpLibraryEntries) return false;
    zeroBased = oneBased - 1u;
    return true;
}

std::string warpLibraryText(const TrackerSession& session)
{
    std::ostringstream stream;
    stream << timingWarpsText(session) << " Library "
           << session.warpLibrary.size() << "/"
           << kMaximumTimingWarpLibraryEntries << ':';
    if (session.warpLibrary.size() == 0u) stream << " empty";
    for (std::size_t index = 0u;
         index < kMaximumTimingWarpLibraryEntries; ++index) {
        const auto* entry = session.warpLibrary.entry(index);
        if (!entry) continue;
        stream << " [";
        if (index + 1u < 10u) stream << '0';
        stream << (index + 1u) << ' ';
        if (entry->name.empty()) stream << "UNTITLED";
        else stream << entry->name;
        stream << " · " << entry->cycleTicks << "T · "
               << entry->stack.size() << "X]";
    }
    return stream.str();
}

ColumnDefinition& columnFor(Track& track, ColumnTarget field)
{
    if (field.kind == ColumnTargetKind::Note) return track.noteColumn;
    if (field.kind == ColumnTargetKind::Instrument)
        return track.instrumentColumn;
    if (field.kind == ColumnTargetKind::Velocity)
        return track.velocityColumn;
    return field.kind == ColumnTargetKind::FxAction
        ? track.fxPairs[field.fxIndex].actionColumn
        : track.fxPairs[field.fxIndex].valueColumn;
}

void normalizeColumnPhases(Pattern& pattern) noexcept
{
    const auto normalize = [](ColumnDefinition& column) {
        column.phase = column.length == 0u ? 0u
            : column.phase % column.length;
    };
    for (auto& track : pattern.tracks) {
        normalize(track.noteColumn);
        normalize(track.instrumentColumn);
        normalize(track.velocityColumn);
        for (auto& pair : track.fxPairs) {
            normalize(pair.actionColumn);
            normalize(pair.valueColumn);
        }
    }
}

bool parseFxPair(std::string_view token, std::size_t& pair)
{
    auto value = asciiLower(token);
    if (value.size() == 3u && value[0] == 'f' && value[1] == 'x')
        value.erase(0u, 2u);
    else if (value.size() == 2u && value[0] == 'f')
        value.erase(0u, 1u);
    std::size_t oneBased = 0u;
    if (!parseUnsigned(value, oneBased) || oneBased == 0u
        || oneBased > kFxPairCount) return false;
    pair = oneBased - 1u;
    return true;
}

float compactFxValue(char symbol) noexcept
{
    if (symbol == '!') return 1.0f;
    if (symbol == '+') return 0.85f;
    if (symbol == '*') return 0.70f;
    if (symbol == '.') return 0.55f;
    return 0.0f;
}

bool isCompactFxPattern(std::string_view token) noexcept
{
    return !token.empty() && std::all_of(token.begin(), token.end(),
        [](char symbol) {
            return symbol == '!' || symbol == '+' || symbol == '*'
                || symbol == '.' || symbol == '=' || symbol == '-';
        });
}

struct ParsedFxSequenceCell {
    FxActionCell action;
    FxValueCell value;
};

bool makeFxSequenceCell(std::string_view atom,
    const FxActionCell& selectedAction, ParsedFxSequenceCell& cell,
    std::string& error)
{
    if (atom == "-") {
        cell.action = FxActionCell::empty();
        cell.value = FxValueCell::previous();
        return true;
    }
    if (atom == "=") {
        cell.action = FxActionCell::previous();
        cell.value = FxValueCell::previous();
        return true;
    }
    if (atom.size() == 1u && isCompactFxPattern(atom)) {
        cell.action = selectedAction;
        cell.value = FxValueCell::withValue(compactFxValue(atom.front()));
        return true;
    }
    if (selectedAction.state == FxActionCellState::Sequencer
        && selectedAction.sequencerAction == SequencerAction::Condition) {
        const auto* condition = findSequencerCondition(atom);
        if (!condition) {
            error = "CD values must be 1:2..2:2, 1:4..4:4, 1:8..8:8, FIRST, LAST, FILL, or !FILL.";
            return false;
        }
        cell.action = selectedAction;
        cell.value = FxValueCell::withValue(
            normalizedFromSequencerCondition(condition->condition));
        return true;
    }
    double numeric = 0.0;
    if (selectedAction.state == FxActionCellState::MidiControlChange
        && atom.find('.') == std::string_view::npos
        && atom.find('%') == std::string_view::npos) {
        uint32_t midi = 0u;
        if (!parseUnsigned(atom, midi) || midi > 127u) {
            error = "MIDI CC sequence values must be integers 0..127 or normalized decimals 0..1.";
            return false;
        }
        numeric = static_cast<double>(midi) / 127.0;
    } else if (!parseFiniteDouble(atom, numeric) || numeric < 0.0
        || numeric > 1.0) {
        error = selectedAction.state
                == FxActionCellState::MidiControlChange
            ? "MIDI CC sequence values must be integers 0..127 or normalized decimals 0..1."
            : "FX sequence values must be normalized 0..1, level symbols ! + * ., previous =, or empty -.";
        return false;
    }
    cell.action = selectedAction;
    cell.value = FxValueCell::withValue(static_cast<float>(numeric));
    return true;
}

bool parseFxSequence(const std::vector<std::string>& tokens,
    std::size_t first, const FxActionCell& selectedAction,
    std::vector<ParsedFxSequenceCell>& cells, std::string& error)
{
    if (first >= tokens.size()) {
        error = "FX sequence is empty.";
        return false;
    }
    if (tokens.size() == first + 1u
        && tokens[first].find(',') == std::string::npos) {
        double scalar = 0.0;
        if (tokens[first].find('.') != std::string::npos
            && parseFiniteDouble(tokens[first], scalar)
            && scalar >= 0.0 && scalar <= 1.0) {
            ParsedFxSequenceCell cell;
            if (!makeFxSequenceCell(tokens[first], selectedAction, cell,
                    error)) return false;
            cells.push_back(cell);
            return true;
        }
    }
    if (tokens.size() == first + 1u
        && tokens[first].find(',') == std::string::npos
        && isCompactFxPattern(tokens[first])) {
        if (tokens[first].size() > kMaximumRows) {
            error = "FX sequences may contain at most 256 rows.";
            return false;
        }
        for (char symbol : tokens[first]) {
            ParsedFxSequenceCell cell;
            const char storage[] { symbol, '\0' };
            if (!makeFxSequenceCell(storage, selectedAction, cell, error))
                return false;
            cells.push_back(cell);
        }
        return !cells.empty();
    }

    for (std::size_t index = first; index < tokens.size(); ++index) {
        std::size_t begin = 0u;
        while (begin <= tokens[index].size()) {
            const auto comma = tokens[index].find(',', begin);
            const auto end = comma == std::string::npos
                ? tokens[index].size() : comma;
            if (end == begin) {
                error = "FX lists may not contain empty values.";
                return false;
            }
            ParsedFxSequenceCell cell;
            if (!makeFxSequenceCell(
                    std::string_view(tokens[index]).substr(begin, end - begin),
                    selectedAction, cell, error)) return false;
            cells.push_back(cell);
            if (cells.size() > kMaximumRows) {
                error = "FX sequences may contain at most 256 rows.";
                return false;
            }
            if (comma == std::string::npos) break;
            begin = comma + 1u;
        }
    }
    return !cells.empty();
}

std::string fxActionsText()
{
    std::ostringstream stream;
    stream << "Sequencing actions:";
    for (std::size_t index = 0u; index < sequencerActionCount(); ++index) {
        const auto* action = sequencerAction(index);
        if (!action) continue;
        stream << ' ' << action->mnemonic << '=' << action->displayName;
    }
    stream << " CC0..CC127=MIDI Control Change. Enter a code in SEQ1/SEQ2 or right-click a SEQ cell.";
    return stream.str();
}

bool parseFxActionToken(std::string_view token, FxActionCell& action)
{
    if (const auto* definition = findSequencerAction(token)) {
        action = FxActionCell::sequencer(definition->action);
        return true;
    }
    uint8_t controller = 0u;
    if (!parseMidiControlChange(token, controller)) return false;
    action = FxActionCell::midiControlChange(controller);
    return true;
}

bool parseFxValueForAction(std::string_view token,
    const FxActionCell& action, float& normalized)
{
    if (action.state == FxActionCellState::Sequencer
        && action.sequencerAction == SequencerAction::Condition) {
        const auto* condition = findSequencerCondition(token);
        if (!condition) return false;
        normalized = normalizedFromSequencerCondition(
            condition->condition);
        return true;
    }
    if (action.state != FxActionCellState::MidiControlChange) {
        double value = 0.0;
        if (!parseFiniteDouble(token, value)
            || value < 0.0 || value > 1.0) return false;
        normalized = static_cast<float>(value);
        return true;
    }
    if (token.find('.') != std::string_view::npos) {
        double value = 0.0;
        if (!parseFiniteDouble(token, value)
            || value < 0.0 || value > 1.0) return false;
        normalized = static_cast<float>(value);
        return true;
    }
    uint32_t midi = 0u;
    if (!parseUnsigned(token, midi) || midi > 127u) return false;
    normalized = static_cast<float>(midi) / 127.0f;
    return true;
}

bool parseFxAmount(std::string_view token, float& normalized)
{
    bool percent = !token.empty() && token.back() == '%';
    if (percent) token.remove_suffix(1u);
    double value = 0.0;
    if (!parseFiniteDouble(token, value) || value < 0.0) return false;
    if (percent || value > 1.0) value *= 0.01;
    if (value > 1.0) return false;
    normalized = static_cast<float>(value);
    return true;
}

bool fxCellNamesAction(const FxActionCell& cell,
    SequencerAction action) noexcept
{
    return cell.state == FxActionCellState::Sequencer
        && cell.sequencerAction == action;
}

CommandResult writeSequencerFxCell(TrackerSession& session,
    std::size_t lane, std::size_t row, SequencerAction action,
    std::string_view amountToken, std::string_view displayVerb)
{
    auto& track = session.pattern.tracks[lane];
    const auto clear = asciiLower(amountToken) == "clear";
    if (clear) {
        bool changed = false;
        for (auto& pair : track.fxPairs) {
            if (row >= pair.actions.size()
                || !fxCellNamesAction(pair.actions[row], action)) continue;
            pair.actions[row] = FxActionCell::empty();
            if (row < pair.values.size())
                pair.values[row] = FxValueCell::previous();
            changed = true;
        }
        return success((changed ? "Cleared " : "No ")
                + std::string(displayVerb) + " on "
                + laneLabel(session, lane) + ", row "
                + std::to_string(row + 1u) + '.',
            changed ? CommandEffect::PatternChanged : CommandEffect::None);
    }

    float amount = 0.0f;
    if (!parseFxAmount(amountToken, amount))
        return failure("FX amount must be 0..1, 0..100%, or clear.");

    std::size_t selectedPair = kFxPairCount;
    for (std::size_t pairIndex = 0u; pairIndex < kFxPairCount;
         ++pairIndex) {
        const auto& pair = track.fxPairs[pairIndex];
        if (row >= pair.actions.size()
            || pair.actions[row].state == FxActionCellState::Empty
            || fxCellNamesAction(pair.actions[row], action)) {
            selectedPair = pairIndex;
            break;
        }
    }
    if (selectedPair == kFxPairCount)
        return failure("Both FX cells on that row already hold other actions.");

    auto& pair = track.fxPairs[selectedPair];
    ensureFxStorage(session, pair, true, row + 1u);
    ensureFxStorage(session, pair, false, row + 1u);
    pair.actions[row] = FxActionCell::sequencer(action);
    pair.values[row] = FxValueCell::withValue(amount);
    pair.actionColumn.length = std::max(pair.actionColumn.length, row + 1u);
    pair.valueColumn.length = std::max(pair.valueColumn.length, row + 1u);
    return success("Wrote " + std::string(displayVerb) + " to "
            + laneLabel(session, lane) + " FX"
            + std::to_string(selectedPair + 1u) + ", row "
            + std::to_string(row + 1u) + '.',
        CommandEffect::PatternChanged);
}

bool parseDirection(std::string_view token, Direction& direction,
    std::string& canonical)
{
    const auto value = asciiLower(token);
    if (value == "forward" || value == "fwd" || value == ">") {
        direction = Direction::Forward;
        canonical = "forward";
        return true;
    }
    if (value == "reverse" || value == "backward" || value == "back"
        || value == "rev" || value == "<") {
        direction = Direction::Reverse;
        canonical = "reverse";
        return true;
    }
    if (value == "random" || value == "rand" || value == "rnd") {
        direction = Direction::Random;
        canonical = "random";
        return true;
    }
    if (value == "palindrome" || value == "pal" || value == "pingpong"
        || value == "ping-pong" || value == "<>") {
        direction = Direction::Palindrome;
        canonical = "palindrome";
        return true;
    }
    return false;
}

bool isMaskLiteral(std::string_view token)
{
    return !token.empty()
        && std::all_of(token.begin(), token.end(), [](char value) {
               return value == 'x' || value == 'X' || value == '-';
           });
}

bool maskSymbolIsHit(char value)
{
    return value == 'x' || value == 'X';
}

void applyMask(TrackerSession& session, std::size_t lane,
    std::string_view mask)
{
    auto& track = session.pattern.tracks[lane];
    const auto pitch = anchorNote(session, lane);
    ensureNoteStorage(session, track, mask.size());
    for (std::size_t row = 0u; row < mask.size(); ++row) {
        if (!maskSymbolIsHit(mask[row])) {
            track.notes[row] = NoteCell::rest();
        } else if (track.notes[row].state != NoteCellState::Note) {
            track.notes[row] = NoteCell::withNote(pitch);
        }
    }
    track.noteColumn.length = mask.size();
}

bool isCompactVelocityPattern(std::string_view token)
{
    if (token.size() <= 1u || token == "--")
        return false;
    double numeric = 0.0;
    if (parseFiniteDouble(token, numeric)) return false;
    for (const auto value : token) {
        const bool valid = value == '!' || value == '+' || value == '*'
            || value == '.' || value == '-' || value == '=';
        if (!valid) return false;
    }
    return true;
}

bool compactVelocityCell(char token, ValueCell& cell, std::string& error)
{
    if (token == '-') {
        cell = ValueCell::defaultValue();
        return true;
    }
    if (token == '=') {
        cell = ValueCell::previous();
        return true;
    }
    if (token == '!') cell = ValueCell::withValue(1.0f);
    else if (token == '+') cell = ValueCell::withValue(0.85f);
    else if (token == '*') cell = ValueCell::withValue(0.70f);
    else if (token == '.') cell = ValueCell::withValue(0.55f);
    else {
        error = "Unsupported compact velocity symbol.";
        return false;
    }
    return true;
}

bool parseVelocityCell(std::string_view token, ValueCell& cell,
    std::string& error)
{
    const auto value = asciiLower(token);
    if (value == "?" || value == "??" || value == "rand"
        || value == "random") {
        error = "Random velocity (?) is not supported by the native value model yet.";
        return false;
    }
    if (value == "=" || value == "hold" || value == "previous"
        || value == "prev") {
        cell = ValueCell::previous();
        return true;
    }
    if (value == "-" || value == "default") {
        cell = ValueCell::defaultValue();
        return true;
    }
    if (value == "--" || value == "mute") {
        error = "Muted velocity cannot suppress a note yet; write a NOTE rest or mask rest instead.";
        return false;
    }
    if (value == "!" || value == "full") {
        cell = ValueCell::withValue(1.0f);
        return true;
    }
    if (value == "+" || value == "accent") {
        cell = ValueCell::withValue(0.85f);
        return true;
    }
    if (value == "*") {
        cell = ValueCell::withValue(0.70f);
        return true;
    }
    if (value == "." || value == "mid") {
        cell = ValueCell::withValue(0.55f);
        return true;
    }

    double numeric = 0.0;
    if (!parseFiniteDouble(token, numeric) || numeric < 0.0
        || numeric > 127.0) {
        error = "Velocity values must be symbols, normalized decimals, or MIDI integers 0..127.";
        return false;
    }
    const bool normalized = token.find('.') != std::string_view::npos
        || token.find('e') != std::string_view::npos
        || token.find('E') != std::string_view::npos;
    if (normalized) {
        if (numeric > 1.0) {
            error = "Decimal velocity values must be normalized between 0 and 1.";
            return false;
        }
        cell = ValueCell::withValue(static_cast<float>(numeric));
        return true;
    }
    if (std::floor(numeric) != numeric) {
        error = "MIDI velocity values must be whole numbers 0..127.";
        return false;
    }
    cell = ValueCell::withValue(static_cast<float>(numeric / 127.0));
    return true;
}

bool parseVelocitySequence(const std::vector<std::string>& tokens,
    std::size_t first, std::vector<ValueCell>& cells, std::string& error)
{
    if (first >= tokens.size()) {
        error = "Velocity sequence is empty.";
        return false;
    }

    if (tokens.size() == first + 1u
        && tokens[first].find(',') == std::string::npos
        && isCompactVelocityPattern(tokens[first])) {
        for (const auto symbol : tokens[first]) {
            ValueCell cell;
            if (!compactVelocityCell(symbol, cell, error)) return false;
            cells.push_back(cell);
        }
        return !cells.empty();
    }

    std::vector<std::string> atoms;
    for (std::size_t index = first; index < tokens.size(); ++index) {
        std::size_t begin = 0u;
        while (begin <= tokens[index].size()) {
            const auto comma = tokens[index].find(',', begin);
            const auto end = comma == std::string::npos
                ? tokens[index].size() : comma;
            if (end == begin) {
                error = "Velocity lists may not contain empty values.";
                return false;
            }
            atoms.push_back(tokens[index].substr(begin, end - begin));
            if (comma == std::string::npos) break;
            begin = comma + 1u;
        }
    }
    if (atoms.empty() || atoms.size() > kMaximumRows) {
        error = "Velocity sequences must contain between 1 and 256 values.";
        return false;
    }
    for (const auto& atom : atoms) {
        ValueCell cell;
        if (!parseVelocityCell(atom, cell, error)) return false;
        cells.push_back(cell);
    }
    return true;
}

std::size_t normalizedRotation(int64_t amount, std::size_t length)
{
    const auto signedLength = static_cast<int64_t>(length);
    auto normalized = amount % signedLength;
    if (normalized < 0) normalized += signedLength;
    return static_cast<std::size_t>(normalized);
}

void rotateNotes(TrackerSession& session, std::size_t lane, int64_t amount)
{
    auto& track = session.pattern.tracks[lane];
    const auto length = track.noteColumn.length;
    ensureNoteStorage(session, track, length);
    const auto rotation = normalizedRotation(amount, length);
    const auto source = std::vector<NoteCell>(track.notes.begin(),
        track.notes.begin() + static_cast<std::ptrdiff_t>(length));
    for (std::size_t row = 0u; row < length; ++row)
        track.notes[(row + rotation) % length] = source[row];
}

void reverseNotes(TrackerSession& session, std::size_t lane)
{
    auto& track = session.pattern.tracks[lane];
    const auto length = track.noteColumn.length;
    ensureNoteStorage(session, track, length);
    std::reverse(track.notes.begin(),
        track.notes.begin() + static_cast<std::ptrdiff_t>(length));
}

bool noteCellIsHit(const NoteCell& cell) noexcept
{
    return cell.state == NoteCellState::Note
        || cell.state == NoteCellState::RetriggerPrevious
        || cell.state == NoteCellState::Burst;
}

double nextRandom(uint64_t& state) noexcept
{
    auto value = state;
    if (value == 0u) value = 0x9e3779b97f4a7c15ull;
    value ^= value >> 12u;
    value ^= value << 25u;
    value ^= value >> 27u;
    state = value;
    const uint64_t bits = value * 2685821657736338717ull;
    return static_cast<double>(bits >> 11u)
        * (1.0 / 9007199254740992.0);
}

double nextCommandRandom(TrackerSession& session) noexcept
{
    return nextRandom(session.commandRngState);
}

uint64_t stableSeedState(std::string_view seed) noexcept
{
    uint64_t state = 14695981039346656037ull;
    for (const auto character : seed) {
        state ^= static_cast<uint8_t>(character);
        state *= 1099511628211ull;
    }
    state ^= state >> 30u;
    state *= 0xbf58476d1ce4e5b9ull;
    state ^= state >> 27u;
    state *= 0x94d049bb133111ebull;
    state ^= state >> 31u;
    return state == 0u ? 0x9e3779b97f4a7c15ull : state;
}

std::size_t randomIndex(uint64_t& state, std::size_t count) noexcept
{
    if (count <= 1u) return 0u;
    return std::min(count - 1u,
        static_cast<std::size_t>(nextRandom(state)
            * static_cast<double>(count)));
}

bool randomChance(uint64_t& state, double probability) noexcept
{
    return nextRandom(state) < probability;
}

enum class DrumRole : uint8_t {
    None,
    Kick,
    Snare,
    Hat,
    OpenHat,
    Tom,
    Crash,
    Ride,
};

bool aliasTargetsLane(const TrackerSession& session, std::string_view name,
    std::size_t lane) noexcept
{
    const auto found = session.aliases.find(std::string(name));
    return found != session.aliases.end() && found->second == lane;
}

DrumRole drumRoleForLane(const TrackerSession& session,
    std::size_t lane)
{
    const auto hasAlias = [&](std::string_view name) {
        return aliasTargetsLane(session, name, lane);
    };
    const auto name = asciiLower(session.pattern.tracks[lane].name);
    const auto contains = [&](std::string_view token) {
        return name.find(token) != std::string::npos;
    };

    if (hasAlias("ohh") || hasAlias("open") || contains("open hat"))
        return DrumRole::OpenHat;
    if (hasAlias("kik") || hasAlias("kick") || hasAlias("bd")
        || contains("kick"))
        return DrumRole::Kick;
    if (hasAlias("snr") || hasAlias("snare") || contains("snare"))
        return DrumRole::Snare;
    if (hasAlias("chh") || hasAlias("hat") || contains("closed hat"))
        return DrumRole::Hat;
    if (hasAlias("lt") || hasAlias("mt") || hasAlias("ht")
        || hasAlias("ft") || contains("tom"))
        return DrumRole::Tom;
    if (hasAlias("cr1") || hasAlias("crash") || contains("crash"))
        return DrumRole::Crash;
    if (hasAlias("rd1") || hasAlias("ride") || contains("ride"))
        return DrumRole::Ride;
    return DrumRole::None;
}

constexpr std::array<std::size_t, 8u> kGeneratedLengths {
    5u, 7u, 8u, 9u, 11u, 13u, 16u, 21u,
};

std::array<ColumnDefinition*, 7u> trackColumns(Track& track) noexcept
{
    return { &track.noteColumn, &track.instrumentColumn,
        &track.velocityColumn, &track.fxPairs[0u].actionColumn,
        &track.fxPairs[0u].valueColumn,
        &track.fxPairs[1u].actionColumn,
        &track.fxPairs[1u].valueColumn };
}

Direction generatedDirection(uint64_t& rng) noexcept
{
    const auto choice = randomIndex(rng, 5u);
    if (choice < 3u) return Direction::Forward;
    return choice == 3u ? Direction::Reverse : Direction::Random;
}

void generateColumnStructure(ColumnDefinition& column, double chaos,
    uint64_t& rng) noexcept
{
    column.length = kGeneratedLengths[randomIndex(rng,
        kGeneratedLengths.size())];
    column.stride = randomChance(rng, chaos * 0.35)
        ? static_cast<uint32_t>(2u + randomIndex(rng, 2u)) : 1u;
    column.direction = generatedDirection(rng);
    column.phase = randomIndex(rng, column.length);
}

uint8_t generatedNote(const TrackerSession& session, std::size_t lane,
    DrumRole role, double chaos, uint64_t& rng) noexcept
{
    const auto anchor = lane < session.laneDefaultNotes.size()
        ? session.laneDefaultNotes[lane] : anchorNote(session, lane);
    if (role != DrumRole::None) return anchor;
    if (!randomChance(rng, chaos)) return anchor;
    constexpr std::array<int, 9u> offsets {
        -12, -7, -5, -2, 0, 2, 5, 7, 12,
    };
    const auto shifted = static_cast<int>(anchor)
        + offsets[randomIndex(rng, offsets.size())];
    return static_cast<uint8_t>(std::clamp(shifted, 0, 127));
}

NoteCell generatedNoteCell(const TrackerSession& session, std::size_t lane,
    DrumRole role, double density, double chaos, double symbols,
    uint64_t& rng) noexcept
{
    if (randomChance(rng, symbols * 0.12)) {
        const auto symbol = randomIndex(rng, 3u);
        if (symbol == 0u) return NoteCell::retriggerPrevious();
        if (symbol == 1u) return NoteCell::kill();
        return NoteCell::rest();
    }
    if (!randomChance(rng, density)) return NoteCell::rest();
    return NoteCell::withNote(generatedNote(session, lane, role, chaos, rng));
}

InstrumentCell generatedInstrumentCell(const Track& track, double symbols,
    uint64_t& rng) noexcept
{
    if (randomChance(rng, symbols * 0.12)) {
        return randomChance(rng, 0.5) ? InstrumentCell::previous()
                                      : InstrumentCell::empty();
    }
    if (!randomChance(rng, 0.92)
        || track.initialInstrumentNodeId == kInvalidInstrumentNode)
        return InstrumentCell::empty();
    return InstrumentCell::withInstrument(track.initialInstrumentNodeId);
}

ValueCell generatedVelocityCell(double symbols, uint64_t& rng) noexcept
{
    if (randomChance(rng, symbols)) {
        return randomChance(rng, 0.5) ? ValueCell::previous()
                                      : ValueCell::defaultValue();
    }
    return ValueCell::withValue(static_cast<float>(
        0.25 + nextRandom(rng) * 0.75));
}

FxActionCell randomSupportedFxAction(uint64_t& rng) noexcept
{
    const auto* action = sequencerAction(randomIndex(rng,
        sequencerActionCount()));
    return action ? FxActionCell::sequencer(action->action)
                  : FxActionCell::empty();
}

FxActionCell generatedFxActionCell(double density, double chaos,
    double symbols, uint64_t& rng) noexcept
{
    if (randomChance(rng, symbols * 0.12))
        return FxActionCell::previous();
    if (!randomChance(rng, density * (0.45 + chaos * 0.45)))
        return FxActionCell::empty();
    return randomSupportedFxAction(rng);
}

FxValueCell generatedFxValueCell(double symbols, uint64_t& rng) noexcept
{
    if (randomChance(rng, symbols)) return FxValueCell::previous();
    return FxValueCell::withValue(static_cast<float>(nextRandom(rng)));
}

void generateWholePattern(TrackerSession& session, double density,
    double chaos, double symbols, uint64_t& rng)
{
    const auto existingDefaultCount = session.laneDefaultNotes.size();
    ensureLaneDefaultNotes(session);
    for (std::size_t lane = existingDefaultCount;
         lane < session.pattern.tracks.size(); ++lane) {
        const auto found = std::find_if(
            session.pattern.tracks[lane].notes.begin(),
            session.pattern.tracks[lane].notes.end(),
            [](const NoteCell& cell) {
                return cell.state == NoteCellState::Note;
            });
        if (found != session.pattern.tracks[lane].notes.end())
            session.laneDefaultNotes[lane] = found->note;
    }
    auto rows = std::max<std::size_t>(session.pattern.visibleRows, 1u);
    for (auto& track : session.pattern.tracks) {
        for (auto* column : trackColumns(track)) {
            generateColumnStructure(*column, chaos, rng);
            rows = std::max(rows, column->length);
        }
    }
    session.pattern.visibleRows = rows;

    for (std::size_t lane = 0u; lane < session.pattern.tracks.size(); ++lane) {
        auto& track = session.pattern.tracks[lane];
        const auto role = drumRoleForLane(session, lane);
        track.notes.clear();
        track.notes.reserve(rows);
        track.instruments.clear();
        track.instruments.reserve(rows);
        track.velocities.clear();
        track.velocities.reserve(rows);
        for (auto& pair : track.fxPairs) {
            pair.actions.clear();
            pair.actions.reserve(rows);
            pair.values.clear();
            pair.values.reserve(rows);
        }

        for (std::size_t row = 0u; row < rows; ++row) {
            track.notes.push_back(generatedNoteCell(session, lane, role,
                density, chaos, symbols, rng));
            track.instruments.push_back(generatedInstrumentCell(track,
                symbols, rng));
            track.velocities.push_back(generatedVelocityCell(symbols, rng));
            for (auto& pair : track.fxPairs) {
                pair.actions.push_back(generatedFxActionCell(density, chaos,
                    symbols, rng));
                pair.values.push_back(generatedFxValueCell(symbols, rng));
            }
        }
    }
}

enum class NativeMutationScope : uint8_t {
    All,
    Notes,
    Drums,
    Values,
    Fx,
    Symbols,
    Structure,
    Meta,
};

bool parseMutationScope(std::string_view token,
    NativeMutationScope& scope) noexcept
{
    const auto value = asciiLower(token);
    if (value == "all") scope = NativeMutationScope::All;
    else if (value == "notes" || value == "note" || value == "n"
        || value == "rhythm" || value == "rhythms" || value == "r")
        scope = NativeMutationScope::Notes;
    else if (value == "drums" || value == "drum" || value == "d")
        scope = NativeMutationScope::Drums;
    else if (value == "values" || value == "value" || value == "val"
        || value == "v")
        scope = NativeMutationScope::Values;
    else if (value == "fx" || value == "effect" || value == "effects"
        || value == "f")
        scope = NativeMutationScope::Fx;
    else if (value == "symbols" || value == "symbol" || value == "sym")
        scope = NativeMutationScope::Symbols;
    else if (value == "structure" || value == "struct")
        scope = NativeMutationScope::Structure;
    else if (value == "meta") scope = NativeMutationScope::Meta;
    else return false;
    return true;
}

std::string_view mutationScopeName(NativeMutationScope scope) noexcept
{
    switch (scope) {
    case NativeMutationScope::All: return "all";
    case NativeMutationScope::Notes: return "notes";
    case NativeMutationScope::Drums: return "drums";
    case NativeMutationScope::Values: return "values";
    case NativeMutationScope::Fx: return "fx";
    case NativeMutationScope::Symbols: return "symbols";
    case NativeMutationScope::Structure: return "structure";
    case NativeMutationScope::Meta: return "meta";
    }
    return "all";
}

void mutateColumnStructure(ColumnDefinition& column, uint64_t& rng) noexcept
{
    if (randomChance(rng, 0.55)) {
        column.length = kGeneratedLengths[randomIndex(rng,
            kGeneratedLengths.size())];
    }
    if (randomChance(rng, 0.35)) {
        constexpr std::array<uint32_t, 5u> strides { 1u, 1u, 1u, 2u, 3u };
        column.stride = strides[randomIndex(rng, strides.size())];
    }
    if (randomChance(rng, 0.35)) column.direction = generatedDirection(rng);
    column.phase = column.length == 0u ? 0u : column.phase % column.length;
}

NoteCell mutatedNoteCell(const TrackerSession& session, std::size_t lane,
    DrumRole role, const NoteCell& cell, uint64_t& rng) noexcept
{
    if (randomChance(rng, 0.18)) {
        const auto symbol = randomIndex(rng, 3u);
        if (symbol == 0u) return NoteCell::rest();
        if (symbol == 1u) return NoteCell::retriggerPrevious();
        return NoteCell::kill();
    }
    if (role != DrumRole::None)
        return NoteCell::withNote(anchorNote(session, lane));
    const auto source = cell.state == NoteCellState::Note
        ? cell.note : anchorNote(session, lane);
    constexpr std::array<int, 6u> offsets { -7, -5, -2, 2, 5, 7 };
    const auto shifted = static_cast<int>(source)
        + offsets[randomIndex(rng, offsets.size())];
    return NoteCell::withNote(static_cast<uint8_t>(
        std::clamp(shifted, 0, 127)));
}

ValueCell mutatedValueCell(const ValueCell& cell, uint64_t& rng) noexcept
{
    if (randomChance(rng, 0.22)) {
        return randomChance(rng, 0.5) ? ValueCell::previous()
                                      : ValueCell::defaultValue();
    }
    const auto base = cell.state == ValueCellState::Value
        ? static_cast<double>(cell.normalized) : nextRandom(rng);
    return ValueCell::withValue(static_cast<float>(std::clamp(
        base + (nextRandom(rng) - 0.5) * 0.35, 0.0, 1.0)));
}

FxValueCell mutatedFxValueCell(const FxValueCell& cell,
    uint64_t& rng) noexcept
{
    if (randomChance(rng, 0.22)) return FxValueCell::previous();
    const auto base = cell.state == FxValueCellState::Value
        ? static_cast<double>(cell.normalized) : nextRandom(rng);
    return FxValueCell::withValue(static_cast<float>(std::clamp(
        base + (nextRandom(rng) - 0.5) * 0.35, 0.0, 1.0)));
}

NoteCell mutatedNoteSymbol(const NoteCell& cell, uint64_t& rng) noexcept
{
    const auto symbol = randomIndex(rng, 3u);
    if (symbol == 0u) return NoteCell::retriggerPrevious();
    if (symbol == 1u) return NoteCell::kill();
    return cell;
}

InstrumentCell mutatedInstrumentSymbol(const InstrumentCell& cell,
    uint64_t& rng) noexcept
{
    const auto symbol = randomIndex(rng, 3u);
    if (symbol == 0u) return InstrumentCell::empty();
    if (symbol == 1u) return InstrumentCell::previous();
    return cell;
}

ValueCell mutatedValueSymbol(const ValueCell& cell,
    uint64_t& rng) noexcept
{
    const auto symbol = randomIndex(rng, 3u);
    if (symbol == 0u) return ValueCell::defaultValue();
    if (symbol == 1u) return ValueCell::previous();
    return cell;
}

FxActionCell mutatedFxActionSymbol(const FxActionCell& cell,
    uint64_t& rng) noexcept
{
    const auto symbol = randomIndex(rng, 3u);
    if (symbol == 0u) return FxActionCell::empty();
    if (symbol == 1u) return FxActionCell::previous();
    return cell;
}

FxValueCell mutatedFxValueSymbol(const FxValueCell& cell,
    uint64_t& rng) noexcept
{
    return randomChance(rng, 0.5) ? FxValueCell::previous() : cell;
}

std::size_t mutateWholePattern(TrackerSession& session, double amount,
    NativeMutationScope scope, uint64_t& rng)
{
    ensureLaneDefaultNotes(session);
    std::size_t changed = 0u;
    for (std::size_t lane = 0u; lane < session.pattern.tracks.size(); ++lane) {
        auto& track = session.pattern.tracks[lane];
        const auto role = drumRoleForLane(session, lane);
        if (scope == NativeMutationScope::Drums && role == DrumRole::None)
            continue;

        if (scope == NativeMutationScope::All
            || scope == NativeMutationScope::Structure) {
            for (auto* column : trackColumns(track)) {
                if (!randomChance(rng, amount * 0.35)) continue;
                mutateColumnStructure(*column, rng);
                ++changed;
            }
        }

        ensureNoteStorage(session, track, track.noteColumn.length);
        ensureInstrumentStorage(session, track, track.instrumentColumn.length);
        ensureVelocityStorage(session, track, track.velocityColumn.length);
        for (auto& pair : track.fxPairs) {
            ensureFxStorage(session, pair, true, pair.actionColumn.length);
            ensureFxStorage(session, pair, false, pair.valueColumn.length);
        }

        const bool mutateNotes = scope == NativeMutationScope::All
            || scope == NativeMutationScope::Notes
            || scope == NativeMutationScope::Drums
            || scope == NativeMutationScope::Symbols;
        if (mutateNotes) {
            for (std::size_t row = 0u; row < track.noteColumn.length; ++row) {
                if (!randomChance(rng, amount)) continue;
                track.notes[row] = scope == NativeMutationScope::Symbols
                    ? mutatedNoteSymbol(track.notes[row], rng)
                    : mutatedNoteCell(session, lane, role, track.notes[row], rng);
                ++changed;
            }
        }

        const bool mutateMeta = scope == NativeMutationScope::All
            || scope == NativeMutationScope::Meta
            || scope == NativeMutationScope::Symbols;
        if (mutateMeta) {
            for (std::size_t row = 0u;
                 row < track.instrumentColumn.length; ++row) {
                if (!randomChance(rng, amount)) continue;
                if (scope == NativeMutationScope::Symbols) {
                    track.instruments[row] = mutatedInstrumentSymbol(
                        track.instruments[row], rng);
                } else if (track.initialInstrumentNodeId
                    == kInvalidInstrumentNode) {
                    track.instruments[row] = InstrumentCell::empty();
                } else {
                    track.instruments[row] = InstrumentCell::withInstrument(
                        track.initialInstrumentNodeId);
                }
                ++changed;
            }
        }

        const bool mutateValues = scope == NativeMutationScope::All
            || scope == NativeMutationScope::Values
            || scope == NativeMutationScope::Symbols;
        if (mutateValues) {
            for (std::size_t row = 0u;
                 row < track.velocityColumn.length; ++row) {
                if (!randomChance(rng, amount)) continue;
                track.velocities[row] = scope == NativeMutationScope::Symbols
                    ? mutatedValueSymbol(track.velocities[row], rng)
                    : mutatedValueCell(track.velocities[row], rng);
                ++changed;
            }
        }

        for (auto& pair : track.fxPairs) {
            const bool mutateFx = scope == NativeMutationScope::All
                || scope == NativeMutationScope::Fx
                || scope == NativeMutationScope::Symbols;
            if (mutateFx) {
                for (std::size_t row = 0u;
                     row < pair.actionColumn.length; ++row) {
                    if (!randomChance(rng, amount)) continue;
                    pair.actions[row] = scope == NativeMutationScope::Symbols
                        ? mutatedFxActionSymbol(pair.actions[row], rng)
                        : (randomChance(rng, 0.25)
                            ? FxActionCell::empty()
                            : randomSupportedFxAction(rng));
                    ++changed;
                }
            }
            if (mutateValues) {
                for (std::size_t row = 0u;
                     row < pair.valueColumn.length; ++row) {
                    if (!randomChance(rng, amount)) continue;
                    pair.values[row] = scope == NativeMutationScope::Symbols
                        ? mutatedFxValueSymbol(pair.values[row], rng)
                        : mutatedFxValueCell(pair.values[row], rng);
                    ++changed;
                }
            }
        }
    }
    return changed;
}

void setDrumSteps(std::vector<bool>& pattern,
    std::initializer_list<std::size_t> steps)
{
    for (const auto step : steps)
        pattern[step % pattern.size()] = true;
}

void setDrumEvery(std::vector<bool>& pattern, std::size_t every,
    std::size_t offset)
{
    for (std::size_t row = 0u; row < pattern.size(); ++row) {
        if ((row + every - (offset % every)) % every == 0u)
            pattern[row] = true;
    }
}

void setDrumProbability(std::vector<bool>& pattern, double amount,
    uint64_t& rng)
{
    for (std::size_t row = 0u; row < pattern.size(); ++row)
        pattern[row] = randomChance(rng, amount);
}

void addDrumProbability(std::vector<bool>& pattern, double amount,
    uint64_t& rng)
{
    for (std::size_t row = 0u; row < pattern.size(); ++row) {
        if (!pattern[row] && randomChance(rng, amount)) pattern[row] = true;
    }
}

void thinDrumPattern(std::vector<bool>& pattern, double amount,
    uint64_t& rng)
{
    for (std::size_t row = 0u; row < pattern.size(); ++row) {
        if (pattern[row] && randomChance(rng, amount)) pattern[row] = false;
    }
}

std::size_t drumSceneLength(std::string_view scene, DrumRole role) noexcept
{
    if (scene == "broken") {
        if (role == DrumRole::Hat) return 15u;
        if (role == DrumRole::Tom) return 13u;
        if (role == DrumRole::Ride) return 21u;
    }
    if (scene == "ritual") {
        if (role == DrumRole::Kick) return 9u;
        if (role == DrumRole::Snare) return 11u;
        if (role == DrumRole::Tom) return 13u;
        if (role == DrumRole::Hat || role == DrumRole::OpenHat) return 8u;
    }
    if (scene == "blast" && role == DrumRole::Hat) return 32u;
    return 16u;
}

std::vector<bool> makeDrumScenePattern(std::string_view scene,
    DrumRole role, std::size_t length, uint64_t& rng)
{
    std::vector<bool> pattern(length, false);
    if (scene == "techno") {
        if (role == DrumRole::Kick) setDrumEvery(pattern, 4u, 0u);
        else if (role == DrumRole::Snare) setDrumSteps(pattern, { 4u, 12u });
        else if (role == DrumRole::Hat) setDrumEvery(pattern, 2u, 0u);
        else if (role == DrumRole::OpenHat) setDrumSteps(pattern, { 6u, 14u });
        else if (role == DrumRole::Tom) setDrumProbability(pattern, 0.12, rng);
        else if (role == DrumRole::Crash) setDrumSteps(pattern, { 0u });
        else if (role == DrumRole::Ride) setDrumEvery(pattern, 4u, 2u);
        else setDrumProbability(pattern, 0.18, rng);
        addDrumProbability(pattern,
            role == DrumRole::Kick ? 0.05
            : role == DrumRole::Hat ? 0.08 : 0.04,
            rng);
        if (length > 8u && randomChance(rng, 0.4))
            pattern[length - 1u] = role == DrumRole::Kick
                || role == DrumRole::Tom;
    } else if (scene == "sparse") {
        if (role == DrumRole::Kick) setDrumSteps(pattern, { 0u, 10u });
        else if (role == DrumRole::Snare) setDrumSteps(pattern, { 8u });
        else if (role == DrumRole::Hat) setDrumEvery(pattern, 4u, 2u);
        else if (role == DrumRole::OpenHat) setDrumProbability(pattern, 0.08, rng);
        else if (role == DrumRole::Tom) setDrumProbability(pattern, 0.10, rng);
        else if (role == DrumRole::Crash) setDrumProbability(pattern, 0.06, rng);
        else if (role == DrumRole::Ride) setDrumEvery(pattern, 8u, 4u);
        else setDrumProbability(pattern, 0.10, rng);
        addDrumProbability(pattern, 0.035, rng);
    } else if (scene == "blast") {
        if (role == DrumRole::Kick) setDrumEvery(pattern, 2u, 0u);
        else if (role == DrumRole::Snare) setDrumEvery(pattern, 4u, 2u);
        else if (role == DrumRole::Hat) setDrumEvery(pattern, 1u, 0u);
        else if (role == DrumRole::OpenHat) setDrumEvery(pattern, 8u, 6u);
        else if (role == DrumRole::Tom) setDrumProbability(pattern, 0.28, rng);
        else if (role == DrumRole::Crash) setDrumSteps(pattern, { 0u, 8u });
        else if (role == DrumRole::Ride) setDrumEvery(pattern, 2u, 1u);
        else setDrumProbability(pattern, 0.25, rng);
        thinDrumPattern(pattern, role == DrumRole::Hat ? 0.08 : 0.04, rng);
    } else if (scene == "ritual") {
        if (role == DrumRole::Kick) setDrumSteps(pattern, { 0u, 4u, 7u });
        else if (role == DrumRole::Snare) setDrumSteps(pattern, { 3u, 8u });
        else if (role == DrumRole::Hat) setDrumEvery(pattern, 2u, 0u);
        else if (role == DrumRole::OpenHat) setDrumSteps(pattern, { 5u });
        else if (role == DrumRole::Tom) setDrumSteps(pattern, { 0u, 5u, 8u });
        else if (role == DrumRole::Crash) setDrumSteps(pattern, { 0u });
        else if (role == DrumRole::Ride) setDrumEvery(pattern, 3u, 0u);
        else setDrumProbability(pattern, 0.18, rng);
        addDrumProbability(pattern, 0.04, rng);
    } else {
        if (role == DrumRole::Kick) setDrumSteps(pattern, { 0u, 3u, 7u, 10u });
        else if (role == DrumRole::Snare) setDrumSteps(pattern, { 5u, 11u });
        else if (role == DrumRole::Hat) setDrumEvery(pattern, 3u, 0u);
        else if (role == DrumRole::OpenHat) setDrumSteps(pattern, { 8u });
        else if (role == DrumRole::Tom) setDrumProbability(pattern, 0.22, rng);
        else if (role == DrumRole::Crash) setDrumSteps(pattern, { 0u });
        else if (role == DrumRole::Ride) setDrumEvery(pattern, 5u, 2u);
        else setDrumProbability(pattern, 0.16, rng);
        addDrumProbability(pattern, 0.10, rng);
        thinDrumPattern(pattern, role == DrumRole::Hat ? 0.12 : 0.06, rng);
    }
    return pattern;
}

std::size_t applyDrumScene(TrackerSession& session, std::string_view scene,
    std::string_view seed)
{
    const auto saltedSeed = std::string("drumscene:") + std::string(scene)
        + ':' + std::string(seed);
    auto rng = stableSeedState(saltedSeed);
    auto rows = session.pattern.visibleRows;
    std::size_t used = 0u;
    for (std::size_t lane = 0u; lane < session.pattern.tracks.size(); ++lane) {
        const auto role = drumRoleForLane(session, lane);
        if (role == DrumRole::None) continue;
        rows = std::max(rows, drumSceneLength(scene, role));
        ++used;
    }
    session.pattern.visibleRows = rows;

    for (std::size_t lane = 0u; lane < session.pattern.tracks.size(); ++lane) {
        const auto role = drumRoleForLane(session, lane);
        if (role == DrumRole::None) continue;
        auto& track = session.pattern.tracks[lane];
        const auto length = drumSceneLength(scene, role);
        const auto pattern = makeDrumScenePattern(scene, role, length, rng);
        track.noteColumn.length = length;
        track.noteColumn.stride = 1u;
        track.noteColumn.direction = Direction::Forward;
        track.noteColumn.phase %= length;
        track.notes.resize(rows, NoteCell::rest());
        const auto note = anchorNote(session, lane);
        for (std::size_t row = 0u; row < rows; ++row) {
            track.notes[row] = pattern[row % length]
                ? NoteCell::withNote(note) : NoteCell::rest();
        }
    }
    return used;
}

bool parseOptionalNoteField(const std::vector<std::string>& tokens,
    std::size_t& cursor) noexcept
{
    if (cursor < tokens.size()
        && asciiLower(tokens[cursor]) == "note") {
        ++cursor;
        return true;
    }
    return false;
}

void setBurstTiming(BurstDefinition& burst, std::string_view shape) noexcept
{
    const auto count = static_cast<std::size_t>(burst.eventCount);
    if (count == 0u) return;
    for (std::size_t index = 0u; index < count; ++index) {
        const double phase = static_cast<double>(index)
            / static_cast<double>(count);
        double shaped = phase;
        if (shape == "accelerate")
            shaped = 1.0 - (1.0 - phase) * (1.0 - phase);
        else if (shape == "decelerate") shaped = phase * phase;
        burst.events[index].position = static_cast<uint16_t>(std::clamp<long>(
            std::lround(shaped * 65536.0), 0l, 65535l));
    }
}

std::size_t burstUsageCount(const Pattern& pattern,
    std::size_t slot) noexcept
{
    std::size_t result = 0u;
    for (const auto& track : pattern.tracks)
        result += static_cast<std::size_t>(std::count_if(
            track.notes.begin(), track.notes.end(), [slot](const NoteCell& cell) {
                return cell.state == NoteCellState::Burst
                    && cell.note == slot;
            }));
    return result;
}

bool parseBurstNotes(const std::vector<std::string>& tokens,
    std::size_t first, BurstDefinition& burst, std::string& error)
{
    const auto count = tokens.size() - first;
    if (count == 0u || count > kMaximumBurstEvents) {
        error = "Burst notes require 1..8 MIDI notes or note names.";
        return false;
    }
    for (std::size_t index = 0u; index < count; ++index) {
        uint8_t note = 0u;
        if (!parseMidiNote(tokens[first + index], note)) {
            error = "Burst notes must be MIDI 0..127 or names such as C-2.";
            return false;
        }
        burst.events[index].note = note;
        if (burst.events[index].velocity == 0u)
            burst.events[index].velocity = 127u;
        if (burst.events[index].gatePercent == 0u)
            burst.events[index].gatePercent = 70u;
    }
    burst.eventCount = static_cast<uint8_t>(count);
    setBurstTiming(burst, "even");
    return true;
}

std::string joinWords(const std::vector<std::string>& tokens,
    std::size_t first)
{
    std::ostringstream stream;
    for (std::size_t index = first; index < tokens.size(); ++index) {
        if (index != first) stream << ' ';
        stream << tokens[index];
    }
    return stream.str();
}

bool helpEntryAcceptsVerb(std::string_view accepted,
    std::string_view verb) noexcept
{
    std::size_t begin = 0u;
    while (begin < accepted.size()) {
        while (begin < accepted.size() && accepted[begin] == ' ') ++begin;
        const auto end = accepted.find(' ', begin);
        const auto count = end == std::string_view::npos
            ? accepted.size() - begin : end - begin;
        if (accepted.substr(begin, count) == verb) return true;
        if (end == std::string_view::npos) break;
        begin = end + 1u;
    }
    return false;
}

bool documentedTopLevelVerb(std::string_view verb)
{
    for (const auto& section : CommandEngine::helpSections()) {
        for (const auto& entry : section.entries) {
            if (helpEntryAcceptsVerb(entry.acceptedVerbs, verb)) return true;
        }
    }
    return false;
}

CommandResult executeTokens(TrackerSession& session,
    std::vector<std::string> tokens)
{
    if (tokens.empty()) return failure("Enter a command, or type help.");

    if (isAliasReference(tokens[0])) {
        if (tokens.size() >= 2u && tokens[1] == "=")
            return failure("Alias = shorthand was removed so = always means previous/hold. Use: alias <name> <lane|@alias>.");
        if (tokens.size() == 1u) {
            std::size_t lane = 0u;
            std::string error;
            if (!parseLane(session, tokens[0], lane, error))
                return failure(std::move(error));
            return success(tokens[0] + " -> " + std::to_string(lane + 1u));
        }

        std::vector<std::string> expanded;
        if (isMaskLiteral(tokens[1])) {
            expanded = { "mask", tokens[0], tokens[1] };
            expanded.insert(expanded.end(), tokens.begin() + 2, tokens.end());
        } else {
            Direction ignoredDirection = Direction::Forward;
            std::string ignoredName;
            if (tokens.size() == 2u
                && asciiLower(tokens[1]) != "reverse"
                && parseDirection(tokens[1], ignoredDirection, ignoredName)) {
                expanded = { "dir", tokens[0], tokens[1] };
            } else {
                auto operation = asciiLower(tokens[1]);
                if (operation == "v" || operation == "vel")
                    operation = "velseq";
                expanded.push_back(std::move(operation));
                expanded.push_back(tokens[0]);
                expanded.insert(expanded.end(), tokens.begin() + 2,
                    tokens.end());
            }
        }
        tokens = std::move(expanded);
    }

    const auto verb = asciiLower(tokens[0]);
    // The shared help catalog is also the command registry. This prevents a
    // newly added top-level parser branch from becoming an undocumented live
    // command. Alias-first syntax returns above or expands to a registered
    // canonical verb before reaching this check.
    if (!documentedTopLevelVerb(verb)) {
        return failure("Unknown command \"" + tokens[0]
            + "\". Type help for the command list.");
    }

    if (verb == "help" || verb == "?") {
        if (tokens.size() != 1u) return failure("Usage: help");
        return success(CommandEngine::helpText());
    }
    if (verb == "undo") {
        if (tokens.size() != 1u) return failure("Usage: undo");
        return success("Undo requested.", CommandEffect::UndoRequested);
    }
    if (verb == "redo") {
        if (tokens.size() != 1u) return failure("Usage: redo");
        return success("Redo requested.", CommandEffect::RedoRequested);
    }
    if (verb == "aliases") {
        if (tokens.size() != 1u) return failure("Usage: aliases");
        return success(aliasesText(session));
    }
    if (verb == "alias") {
        if (tokens.size() != 3u)
            return failure("Usage: alias <name> <lane|@alias>");
        std::string alias;
        if (!normalizeAliasName(tokens[1], alias))
            return failure("Alias names must start with a letter and use only letters, digits, or underscore.");
        std::size_t lane = 0u;
        std::string error;
        if (!parseLane(session, tokens[2], lane, error))
            return failure(std::move(error));
        session.aliases[alias] = lane;
        return success("Bound @" + alias + " to "
            + laneLabel(session, lane) + '.',
            CommandEffect::ProjectChanged);
    }
    if (verb == "autoalias") {
        if (tokens.size() != 1u) return failure("Usage: autoalias");
        if (session.pattern.tracks.empty())
            return failure("Autoalias requires at least one lane.");
        auto generated = automaticAliases(session);
        const bool changed = generated != session.aliases;
        session.aliases = std::move(generated);
        return success(std::string(changed ? "Generated" : "Retained")
                + " automatic aliases.\n" + aliasesText(session),
            changed ? CommandEffect::ProjectChanged : CommandEffect::None);
    }
    if (verb == "kit") {
        if (tokens.size() != 2u && tokens.size() != 3u)
            return failure("Usage: kit [gm|superior] <compact|basic|toms>");
        auto mapName = std::string("superior");
        auto templateName = asciiLower(tokens[1]);
        if (tokens.size() == 3u) {
            mapName = asciiLower(tokens[1]);
            templateName = asciiLower(tokens[2]);
        }
        if (mapName != "gm" && mapName != "superior")
            return failure("Kit map must be gm or superior.");
        if (kitTemplate(templateName).empty())
            return failure("Kit template must be compact, basic, or toms.");
        configureKit(session, mapName, templateName);
        const auto templateLanes = kitTemplate(templateName).size();
        const auto retainedLanes = session.pattern.tracks.size() - templateLanes;
        std::ostringstream stream;
        stream << "Configured " << templateName << ' ' << mapName
               << " kit on " << templateLanes << " lanes";
        if (retainedLanes > 0u)
            stream << "; muted " << retainedLanes << " retained lane"
                   << (retainedLanes == 1u ? "" : "s");
        stream << '.';
        return success(stream.str(), CommandEffect::PatternChanged);
    }
    if (verb == "variation" || verb == "vary") {
        if (tokens.size() < 2u) {
            return failure("Usage: variation <generate|generateseed|scene|mutate|drumscene> ... [launch <tick|beat|cycle>]");
        }
        auto generatorEnd = tokens.size();
        PatternVariationLaunch launch = PatternVariationLaunch::None;
        if (tokens.size() >= 4u
            && asciiLower(tokens[tokens.size() - 2u]) == "launch") {
            const auto quantization = asciiLower(tokens.back());
            if (quantization == "tick")
                launch = PatternVariationLaunch::NextTick;
            else if (quantization == "beat")
                launch = PatternVariationLaunch::NextBeat;
            else if (quantization == "cycle"
                || quantization == "pattern")
                launch = PatternVariationLaunch::NextPatternCycle;
            else {
                return failure("Variation launch must be tick, beat, or cycle.");
            }
            generatorEnd -= 2u;
        }
        std::vector<std::string> generatorTokens(tokens.begin() + 1,
            tokens.begin() + static_cast<std::ptrdiff_t>(generatorEnd));
        if (generatorTokens.empty())
            return failure("Variation requires a generation or mutation command.");
        const auto generatorVerb = asciiLower(generatorTokens.front());
        if (generatorVerb != "generate" && generatorVerb != "generateseed"
            && generatorVerb != "scene" && generatorVerb != "mutate"
            && generatorVerb != "drumscene") {
            return failure("Variation accepts generate, generateseed, scene, mutate, or drumscene.");
        }

        TrackerSession generated = session;
        auto generatedResult = executeTokens(generated, generatorTokens);
        if (!generatedResult.ok) return generatedResult;
        if (!generatedResult.hasEffect(CommandEffect::PatternChanged))
            return failure("Variation command did not generate a pattern.");
        normalizeColumnPhases(generated.pattern);
        session.commandRngState = generated.commandRngState;

        PatternVariationRequest request;
        request.generatedSession = std::move(generated);
        request.launch = launch;
        request.sourceCommand = joinWords(generatorTokens, 0u);
        std::ostringstream stream;
        stream << "Prepared bank variation from " << request.sourceCommand;
        if (launch == PatternVariationLaunch::NextTick)
            stream << " for next-tick launch";
        else if (launch == PatternVariationLaunch::NextBeat)
            stream << " for next-beat launch";
        else if (launch == PatternVariationLaunch::NextPatternCycle)
            stream << " for next-cycle launch";
        stream << '.';
        auto result = success(stream.str(), CommandEffect::ProjectChanged);
        result.patternVariation = std::move(request);
        return result;
    }
    if (verb == "generate" || verb == "generateseed") {
        const bool seeded = verb == "generateseed";
        const auto expectedWithoutParameters = seeded ? 2u : 1u;
        const auto expectedWithParameters = seeded ? 5u : 4u;
        if (tokens.size() != expectedWithoutParameters
            && tokens.size() != expectedWithParameters) {
            return failure(seeded
                ? "Usage: generateseed <seed> [density chaos symbols]"
                : "Usage: generate [density chaos symbols]");
        }
        if (session.pattern.tracks.empty())
            return failure("Generation requires at least one lane.");

        double density = 0.45;
        double chaos = 0.50;
        double symbols = 0.18;
        const auto parameterOffset = seeded ? 2u : 1u;
        if (tokens.size() == expectedWithParameters) {
            if (!parseFiniteDouble(tokens[parameterOffset], density)
                || !parseFiniteDouble(tokens[parameterOffset + 1u], chaos)
                || !parseFiniteDouble(tokens[parameterOffset + 2u], symbols)
                || density < 0.0 || density > 1.0
                || chaos < 0.0 || chaos > 1.0
                || symbols < 0.0 || symbols > 1.0) {
                return failure("Generation density, chaos, and symbols must each be normalized between 0 and 1.");
            }
        }

        auto rng = seeded ? stableSeedState(tokens[1u])
                          : session.commandRngState;
        generateWholePattern(session, density, chaos, symbols, rng);
        if (!seeded) session.commandRngState = rng;
        std::ostringstream stream;
        stream << "Generated " << session.pattern.tracks.size()
               << " lanes at density " << density << ", chaos " << chaos
               << ", symbols " << symbols;
        if (seeded) stream << " using seed " << tokens[1u];
        stream << '.';
        return success(stream.str(), CommandEffect::PatternChanged);
    }
    if (verb == "scene") {
        if (tokens.size() != 2u && tokens.size() != 3u)
            return failure("Usage: scene <sparse|balanced|dense|drift|weird> [seed]");
        if (session.pattern.tracks.empty())
            return failure("Generation requires at least one lane.");
        const auto name = asciiLower(tokens[1u]);
        double density = 0.48;
        double chaos = 0.55;
        double symbols = 0.18;
        if (name == "sparse") {
            density = 0.28;
            chaos = 0.25;
            symbols = 0.08;
        } else if (name == "dense") {
            density = 0.72;
            chaos = 0.65;
            symbols = 0.22;
        } else if (name == "weird") {
            density = 0.55;
            chaos = 0.95;
            symbols = 0.35;
        } else if (name == "drift") {
            density = 0.38;
            chaos = 0.75;
            symbols = 0.28;
        } else if (name != "balanced") {
            return failure("Scene must be sparse, balanced, dense, drift, or weird.");
        }
        const auto& seed = tokens.size() == 3u ? tokens[2u] : tokens[1u];
        auto rng = stableSeedState(seed);
        generateWholePattern(session, density, chaos, symbols, rng);
        return success("Generated " + name + " scene using seed " + seed
                + '.',
            CommandEffect::PatternChanged);
    }
    if (verb == "mutate") {
        if (tokens.size() > 3u)
            return failure("Usage: mutate [amount] [all|notes|drums|values|fx|symbols|structure|meta]");
        if (session.pattern.tracks.empty())
            return failure("Mutation requires at least one lane.");
        double amount = 0.12;
        if (tokens.size() >= 2u
            && (!parseFiniteDouble(tokens[1u], amount)
                || amount < 0.0 || amount > 1.0)) {
            return failure("Mutation amount must be normalized between 0 and 1.");
        }
        NativeMutationScope scope = NativeMutationScope::All;
        if (tokens.size() == 3u
            && !parseMutationScope(tokens[2u], scope)) {
            return failure("Mutation scope must be all, notes, drums, values, fx, symbols, structure, or meta.");
        }
        auto rng = session.commandRngState;
        const auto changed = mutateWholePattern(session, amount, scope, rng);
        session.commandRngState = rng;
        std::ostringstream stream;
        stream << "Mutated " << changed << " cells/columns at amount "
               << amount << " in " << mutationScopeName(scope)
               << " scope.";
        return success(stream.str(), CommandEffect::PatternChanged);
    }
    if (verb == "drumscene") {
        if (tokens.size() != 2u && tokens.size() != 3u)
            return failure("Usage: drumscene <techno|broken|sparse|blast|ritual> [seed]");
        const auto name = asciiLower(tokens[1u]);
        if (name != "techno" && name != "broken" && name != "sparse"
            && name != "blast" && name != "ritual") {
            return failure("Drum scene must be techno, broken, sparse, blast, or ritual.");
        }
        ensureLaneDefaultNotes(session);
        const auto& seed = tokens.size() == 3u ? tokens[2u] : tokens[1u];
        const auto used = applyDrumScene(session, name, seed);
        if (used == 0u)
            return failure("Drum scene found no kit lanes. Run kit first or use recognized drum lane names/aliases.");
        std::ostringstream stream;
        stream << "Generated " << name << " drum scene using seed " << seed
               << " across " << used << " lanes.";
        return success(stream.str(), CommandEffect::PatternChanged);
    }
    if (verb == "play") {
        if (tokens.size() != 1u) return failure("Usage: play");
        return success("Playback requested.", CommandEffect::StartPlayback);
    }
    if (verb == "stop") {
        if (tokens.size() != 1u) return failure("Usage: stop");
        return success("Stop requested.", CommandEffect::StopPlayback);
    }
    if (verb == "panic") {
        if (tokens.size() != 1u) return failure("Usage: panic");
        return success("MIDI panic requested.", CommandEffect::Panic);
    }
    if (verb == "demo") {
        if (tokens.size() != 1u) return failure("Usage: demo");
        loadDemo(session);
        return success("Loaded the 8-lane General MIDI drum pattern.",
            CommandEffect::PatternChanged | CommandEffect::TransportChanged
                | CommandEffect::SelectionChanged);
    }
    if (verb == "swing") {
        if (tokens.size() != 2u)
            return failure("Usage: swing <0.50..0.75|50..75>");
        double swing = 0.0;
        if (!parseFiniteDouble(tokens[1], swing))
            return failure("Swing must be 0.50..0.75 or 50..75 percent.");
        if (swing >= 50.0 && swing <= 75.0) swing /= 100.0;
        if (swing < kMinimumSwing || swing > kMaximumSwing)
            return failure("Swing must be 0.50..0.75 or 50..75 percent.");
        session.transport.swing = swing;
        std::ostringstream stream;
        stream << "Swing set to " << std::lround(swing * 100.0) << "%.";
        return success(stream.str(), CommandEffect::TransportChanged);
    }
    if (verb == "loop") {
        if (tokens.size() == 2u) {
            const auto mode = asciiLower(tokens[1]);
            if (mode == "on") session.transport.loopEnabled = true;
            else if (mode == "off") session.transport.loopEnabled = false;
            else if (mode == "toggle")
                session.transport.loopEnabled
                    = !session.transport.loopEnabled;
            else return failure(
                "Usage: loop <on|off|toggle> or loop [rows] <start> <end>");
            return success(session.transport.loopEnabled
                    ? "Global row loop enabled." : "Global row loop disabled.",
                CommandEffect::TransportChanged);
        }
        const std::size_t offset = tokens.size() == 4u
                && asciiLower(tokens[1]) == "rows" ? 1u : 0u;
        if ((offset == 0u && tokens.size() != 3u)
            || (offset == 1u && tokens.size() != 4u))
            return failure(
                "Usage: loop <on|off|toggle> or loop [rows] <start> <end>");
        std::size_t start = 0u;
        std::size_t end = 0u;
        if (!parseUnsigned(tokens[1u + offset], start)
            || !parseUnsigned(tokens[2u + offset], end)
            || start == 0u || end < start || end > kMaximumRows)
            return failure("Loop rows must be one-based: 1 <= start <= end <= 256.");
        session.transport.loopStartRow = static_cast<uint32_t>(start - 1u);
        session.transport.loopEndRow = static_cast<uint32_t>(end);
        std::ostringstream stream;
        stream << "Global loop rows set to " << start << "–" << end << '.';
        return success(stream.str(), CommandEffect::TransportChanged);
    }
    if (verb == "gate") {
        if (tokens.size() != 2u)
            return failure("Usage: gate <1..5000 ms>");
        double milliseconds = 0.0;
        if (!parseFiniteDouble(tokens[1], milliseconds)
            || milliseconds < kMinimumGateMilliseconds
            || milliseconds > kMaximumGateMilliseconds)
            return failure("Gate must be between 1 and 5000 milliseconds.");
        session.gateMilliseconds = milliseconds;
        std::ostringstream stream;
        stream << "MIDI gate set to " << milliseconds << " ms.";
        return success(stream.str(), CommandEffect::OutputChanged);
    }
    if (verb == "warps") {
        if (tokens.size() != 1u) return failure("Usage: warps");
        return success(warpLibraryText(session));
    }
    if (verb == "warp") {
        if (tokens.size() < 2u) {
            return failure("Usage: warp <on|off|toggle|save|load|delete|rename|clear|cycle|exp|step|eu> ...");
        }
        const auto operation = asciiLower(tokens[1]);
        if (operation == "on" || operation == "off"
            || operation == "toggle") {
            if (tokens.size() != 2u)
                return failure("Usage: warp <on|off|toggle>");
            if (operation == "on") session.transport.timingWarpEnabled = true;
            else if (operation == "off")
                session.transport.timingWarpEnabled = false;
            else session.transport.timingWarpEnabled
                = !session.transport.timingWarpEnabled;
            return success(session.transport.timingWarpEnabled
                    ? "Pattern timing-warp playback enabled."
                    : "Pattern timing-warp playback bypassed.",
                CommandEffect::TransportChanged);
        }
        if (operation == "save") {
            std::size_t index = 0u;
            if (tokens.size() < 3u
                || !parseWarpLibraryIndex(tokens[2], index))
                return failure("Usage: warp save <1..64> [name]");
            std::string name = tokens.size() > 3u
                ? joinWords(tokens, 3u) : std::string {};
            if (name.empty()) {
                if (const auto* previous = session.warpLibrary.entry(index))
                    name = previous->name;
            }
            if (name.empty()) name = "WARP " + std::to_string(index + 1u);
            if (!session.warpLibrary.store(index, name,
                    session.transport.warpCycleTicks,
                    session.transport.timingWarp))
                return failure("Warp name is too long or the live cycle is outside 1..16 ticks.");
            return success("Saved current composition to warp "
                    + std::to_string(index + 1u) + " · " + name + '.',
                CommandEffect::ProjectChanged
                    | CommandEffect::TransportChanged);
        }
        if (operation == "load" || operation == "recall"
            || operation == "use") {
            std::size_t index = 0u;
            if (tokens.size() != 3u
                || !parseWarpLibraryIndex(tokens[2], index))
                return failure("Usage: warp load <1..64>");
            const auto* entry = session.warpLibrary.entry(index);
            if (!entry)
                return failure("Warp library slot "
                    + std::to_string(index + 1u) + " is empty.");
            session.transport.warpCycleTicks = entry->cycleTicks;
            session.transport.timingWarp = entry->stack;
            return success("Recalled warp " + std::to_string(index + 1u)
                    + " · " + (entry->name.empty() ? "UNTITLED"
                                                   : entry->name) + '.',
                CommandEffect::TransportChanged);
        }
        if (operation == "delete" || operation == "remove") {
            std::size_t index = 0u;
            if (tokens.size() != 3u
                || !parseWarpLibraryIndex(tokens[2], index))
                return failure("Usage: warp delete <1..64>");
            if (!session.warpLibrary.erase(index))
                return failure("Warp library slot "
                    + std::to_string(index + 1u) + " is already empty.");
            return success("Deleted warp " + std::to_string(index + 1u)
                    + '.', CommandEffect::ProjectChanged
                        | CommandEffect::TransportChanged);
        }
        if (operation == "rename") {
            std::size_t index = 0u;
            if (tokens.size() < 4u
                || !parseWarpLibraryIndex(tokens[2], index))
                return failure("Usage: warp rename <1..64> <name>");
            auto* entry = session.warpLibrary.entry(index);
            if (!entry)
                return failure("Warp library slot "
                    + std::to_string(index + 1u) + " is empty.");
            const std::string name = joinWords(tokens, 3u);
            if (name.size() > kMaximumTimingWarpLibraryNameBytes)
                return failure("Warp names may contain at most 64 UTF-8 bytes.");
            entry->name = name;
            return success("Renamed warp " + std::to_string(index + 1u)
                    + " · " + name + '.', CommandEffect::ProjectChanged);
        }
        if (operation == "clear") {
            if (tokens.size() != 2u) return failure("Usage: warp clear");
            session.transport.timingWarp.clear();
            return success("Cleared the functional timing-warp stack.",
                CommandEffect::TransportChanged);
        }
        if (operation == "cycle") {
            uint32_t ticks = 0u;
            if (tokens.size() != 3u
                || !parseUnsigned(tokens[2], ticks) || ticks == 0u
                || ticks > 16u) {
                return failure("The live warp cycle must be between 1 and 16 ticks.");
            }
            session.transport.warpCycleTicks = ticks;
            return success("Warp cycle set to " + std::to_string(ticks)
                    + " ticks.",
                CommandEffect::TransportChanged);
        }

        if (session.transport.warpCycleTicks > 16u) {
            return failure("Set the live warp cycle to 16 ticks or fewer before adding a transform.");
        }

        TimingWarpTransform transform;
        TimingWarpOptions options;
        std::size_t optionsBegin = 0u;
        if (operation == "exp" || operation == "exponential") {
            double exponent = 0.0;
            if (tokens.size() < 3u
                || !parseFiniteDouble(tokens[2], exponent)
                || exponent < TimingWarpStack::kMinimumExponent
                || exponent > TimingWarpStack::kMaximumExponent) {
                return failure("Warp exponent must be between 0.015625 and 64.");
            }
            transform = TimingWarpTransform::exponential(exponent);
            optionsBegin = 3u;
        } else if (operation == "step" || operation == "quantize") {
            uint32_t steps = 0u;
            if (tokens.size() < 3u || !parseUnsigned(tokens[2], steps)
                || steps == 0u
                || steps > kMaximumLiveWarpSteps) {
                return failure("Warp step count must be between 1 and "
                    + std::to_string(kMaximumLiveWarpSteps) + ".");
            }
            transform = TimingWarpTransform::stepQuantize(steps);
            optionsBegin = 3u;
        } else if (operation == "eu" || operation == "euclid") {
            uint32_t pulses = 0u;
            uint32_t steps = 0u;
            if (tokens.size() < 4u
                || !parseUnsigned(tokens[2], pulses)
                || !parseUnsigned(tokens[3], steps) || pulses == 0u
                || steps == 0u || pulses > steps
                || steps > kMaximumLiveWarpSteps) {
                return failure("Warp Euclid requires 1..steps pulses and 1.."
                    + std::to_string(kMaximumLiveWarpSteps) + " steps.");
            }
            transform = TimingWarpTransform::euclideanQuantize(
                pulses, steps);
            optionsBegin = 4u;
        } else {
            return failure("Warp operation must be on, off, toggle, save, load, delete, rename, exp, step, eu, cycle, or clear.");
        }

        std::string error;
        if (!parseWarpOptions(tokens, optionsBegin, options, error))
            return failure(std::move(error));
        transform.options = options;
        const auto append = session.transport.timingWarp.append(transform);
        if (!append.added())
            return failure("The timing-warp stack is full or the segment is invalid.");
        return success(timingWarpsText(session),
            CommandEffect::TransportChanged);
    }
    if (verb == "select") {
        std::size_t laneToken = 1u;
        if ((tokens.size() == 3u || tokens.size() == 4u)
            && asciiLower(tokens[1]) == "lane")
            laneToken = 2u;
        else if (tokens.size() != 2u && tokens.size() != 3u)
            return failure("Usage: select <lane|@alias> [row]");
        std::size_t lane = 0u;
        std::string error;
        if (!parseLane(session, tokens[laneToken], lane, error))
            return failure(std::move(error));
        auto row = session.selectedRow;
        if (tokens.size() > laneToken + 1u) {
            if (!parseRow(tokens[laneToken + 1u], row, error))
                return failure(std::move(error));
        }
        session.selectedTrack = lane;
        session.selectedRow = row;
        std::ostringstream stream;
        stream << "Selected " << laneLabel(session, lane) << ", row "
               << (session.selectedRow + 1u) << '.';
        return success(stream.str(), CommandEffect::SelectionChanged);
    }

    if (verb == "burst") {
        if (tokens.size() < 2u)
            return failure("Usage: burst <new|duplicate|delete|B01..B32|target> ...");
        const auto operation = asciiLower(tokens[1]);
        if (operation == "new") {
            std::size_t slot = 0u;
            if (tokens.size() < 3u || !parseBurstSlot(tokens[2], slot))
                return failure("Usage: burst new <B01..B32> [name]");
            auto& burst = session.pattern.bursts[slot];
            if (!burst.empty())
                return failure(burstSlotToken(slot) + " is already in use.");
            burst = {};
            burst.name = tokens.size() > 3u ? joinWords(tokens, 3u)
                                            : "BURST " + burstSlotToken(slot);
            burst.eventCount = 4u;
            const uint8_t note = session.pattern.tracks.empty() ? 60u
                : laneDefaultNote(session, std::min(session.selectedTrack,
                      session.pattern.tracks.size() - 1u));
            for (std::size_t index = 0u; index < burst.eventCount; ++index)
                burst.events[index] = { 0u, note, 127u, 70u };
            setBurstTiming(burst, "even");
            return success("Created " + burstSlotToken(slot) + " · "
                    + burst.name + " with four even substeps.",
                CommandEffect::PatternChanged);
        }
        if (operation == "duplicate") {
            std::size_t source = 0u;
            std::size_t destination = 0u;
            if (tokens.size() != 4u || !parseBurstSlot(tokens[2], source)
                || !parseBurstSlot(tokens[3], destination))
                return failure("Usage: burst duplicate <B01..B32> <B01..B32>");
            if (session.pattern.bursts[source].empty())
                return failure(burstSlotToken(source) + " is empty.");
            if (!session.pattern.bursts[destination].empty())
                return failure(burstSlotToken(destination) + " is already in use.");
            session.pattern.bursts[destination]
                = session.pattern.bursts[source];
            session.pattern.bursts[destination].name += " COPY";
            return success("Duplicated " + burstSlotToken(source) + " to "
                    + burstSlotToken(destination) + '.',
                CommandEffect::PatternChanged);
        }
        if (operation == "delete" || operation == "clear") {
            std::size_t slot = 0u;
            if (tokens.size() != 3u || !parseBurstSlot(tokens[2], slot))
                return failure("Usage: burst delete <B01..B32>");
            const auto uses = burstUsageCount(session.pattern, slot);
            if (uses != 0u)
                return failure(burstSlotToken(slot) + " is referenced by "
                    + std::to_string(uses) + " NOTE cell(s); replace them first.");
            session.pattern.bursts[slot] = {};
            return success("Deleted " + burstSlotToken(slot) + '.',
                CommandEffect::PatternChanged);
        }

        std::size_t slot = 0u;
        if (parseBurstSlot(tokens[1], slot)) {
            auto& burst = session.pattern.bursts[slot];
            if (tokens.size() < 3u)
                return failure("Usage: burst " + burstSlotToken(slot)
                    + " <notes|velocity|gate|timing|name|reverse|rotate|usage> ...");
            const auto edit = asciiLower(tokens[2]);
            if (edit != "notes" && burst.empty())
                return failure(burstSlotToken(slot)
                    + " is empty; create it with burst new or burst Bxx notes.");
            if (edit == "notes") {
                std::string error;
                if (!parseBurstNotes(tokens, 3u, burst, error))
                    return failure(std::move(error));
            } else if (edit == "velocity" || edit == "vel") {
                if (tokens.size() != 4u
                    && tokens.size() != 3u + burst.eventCount)
                    return failure("Velocity accepts one value or one per substep.");
                for (std::size_t index = 0u; index < burst.eventCount; ++index) {
                    uint32_t value = 0u;
                    const auto tokenIndex = tokens.size() == 4u
                        ? 3u : 3u + index;
                    if (!parseUnsigned(tokens[tokenIndex], value)
                        || value == 0u || value > 127u)
                        return failure("Burst velocity must be 1..127.");
                    burst.events[index].velocity = static_cast<uint8_t>(value);
                }
            } else if (edit == "gate") {
                if (tokens.size() != 4u
                    && tokens.size() != 3u + burst.eventCount)
                    return failure("Gate accepts one percentage or one per substep.");
                for (std::size_t index = 0u; index < burst.eventCount; ++index) {
                    auto token = std::string_view(tokens[tokens.size() == 4u
                        ? 3u : 3u + index]);
                    if (!token.empty() && token.back() == '%')
                        token.remove_suffix(1u);
                    uint32_t value = 0u;
                    if (!parseUnsigned(token, value)
                        || value == 0u || value > 100u)
                        return failure("Burst gate must be 1..100 percent.");
                    burst.events[index].gatePercent
                        = static_cast<uint8_t>(value);
                }
            } else if (edit == "timing") {
                if (tokens.size() < 4u)
                    return failure("Timing is even, accelerate, decelerate, or a percentage per substep.");
                const auto shape = asciiLower(tokens[3]);
                if (tokens.size() == 4u && (shape == "even"
                        || shape == "accelerate" || shape == "decelerate")) {
                    setBurstTiming(burst, shape);
                } else {
                    if (tokens.size() != 3u + burst.eventCount)
                        return failure("Custom timing needs one 0..99.999 percentage per substep.");
                    uint16_t previous = 0u;
                    for (std::size_t index = 0u; index < burst.eventCount;
                         ++index) {
                        auto token = std::string_view(tokens[3u + index]);
                        if (!token.empty() && token.back() == '%')
                            token.remove_suffix(1u);
                        double value = 0.0;
                        if (!parseFiniteDouble(token, value)
                            || value < 0.0 || value >= 100.0)
                            return failure("Custom burst positions must be 0..<100 percent.");
                        const auto position = static_cast<uint16_t>(
                            std::clamp<long>(std::lround(value * 655.36),
                                0l, 65535l));
                        if (index > 0u && position < previous)
                            return failure("Custom burst positions must be in time order.");
                        burst.events[index].position = position;
                        previous = position;
                    }
                }
            } else if (edit == "name" || edit == "rename") {
                if (tokens.size() < 4u)
                    return failure("Usage: burst B01 name <words...>");
                const auto name = joinWords(tokens, 3u);
                if (name.size() > kMaximumBurstNameBytes)
                    return failure("Burst names may contain at most 64 UTF-8 bytes.");
                burst.name = name;
            } else if (edit == "reverse") {
                if (tokens.size() != 3u)
                    return failure("Usage: burst B01 reverse");
                std::reverse(burst.events.begin(),
                    burst.events.begin() + burst.eventCount);
                setBurstTiming(burst, "even");
            } else if (edit == "rotate") {
                int64_t amount = 0;
                if (tokens.size() != 4u || !parseSigned(tokens[3], amount))
                    return failure("Usage: burst B01 rotate <signed substeps>");
                const auto count = static_cast<std::size_t>(burst.eventCount);
                const auto rotation = normalizedRotation(amount, count);
                std::rotate(burst.events.begin(),
                    burst.events.begin() + static_cast<std::ptrdiff_t>(
                        count - rotation),
                    burst.events.begin() + static_cast<std::ptrdiff_t>(count));
                setBurstTiming(burst, "even");
            } else if (edit == "usage") {
                if (tokens.size() != 3u)
                    return failure("Usage: burst B01 usage");
                return success(burstSlotToken(slot) + " · " + burst.name
                    + " · " + std::to_string(burst.eventCount)
                    + " substeps · "
                    + std::to_string(burstUsageCount(session.pattern, slot))
                    + " NOTE cell use(s).");
            } else {
                return failure("Burst edit must be notes, velocity, gate, timing, name, reverse, rotate, or usage.");
            }
            return success("Updated " + burstSlotToken(slot) + " · "
                    + burst.name + '.', CommandEffect::PatternChanged);
        }

        // Fast authoring form: allocate a recipe, fill it, and place its
        // reference into the requested NOTE cell in one transaction.
        std::size_t lane = 0u;
        std::size_t row = 0u;
        std::string error;
        if (tokens.size() < 5u || !parseLane(session, tokens[1], lane, error)
            || !parseRow(tokens[2], row, error)
            || asciiLower(tokens[3]) != "notes")
            return failure(error.empty()
                ? "Usage: burst <target> <row> notes <1..8 MIDI notes>"
                : std::move(error));
        const auto empty = std::find_if(session.pattern.bursts.begin(),
            session.pattern.bursts.end(), [](const BurstDefinition& burst) {
                return burst.empty();
            });
        if (empty == session.pattern.bursts.end())
            return failure("All 32 Burst slots are in use.");
        slot = static_cast<std::size_t>(empty - session.pattern.bursts.begin());
        BurstDefinition burst;
        burst.name = "QUICK " + burstSlotToken(slot);
        if (!parseBurstNotes(tokens, 4u, burst, error))
            return failure(std::move(error));
        session.pattern.bursts[slot] = burst;
        auto& track = session.pattern.tracks[lane];
        ensureNoteStorage(session, track, row + 1u);
        track.notes[row] = NoteCell::withBurst(static_cast<uint8_t>(slot));
        track.noteColumn.length = std::max(track.noteColumn.length, row + 1u);
        return success("Created " + burstSlotToken(slot) + " and wrote it to "
                + laneLabel(session, lane) + ", row "
                + std::to_string(row + 1u) + '.',
            CommandEffect::PatternChanged);
    }

    if (verb == "pitch" || verb == "defaultnote") {
        if (tokens.size() != 3u)
            return failure("Usage: pitch|defaultnote <lane|@alias> <MIDI note|note name>");
        std::size_t lane = 0u;
        std::string error;
        if (!parseLane(session, tokens[1], lane, error))
            return failure(std::move(error));
        uint8_t note = 0u;
        if (!parseMidiNote(tokens[2], note))
            return failure("Pitch must be MIDI 0..127 or a note name such as C-2 or F#3.");
        std::size_t replaced = 0u;
        (void)setLaneDefaultNote(session, lane, note, &replaced);
        return success("Set " + laneLabel(session, lane)
                + " default pitch to " + midiNoteNameText(note) + " (MIDI "
                + std::to_string(note) + "); replaced "
                + std::to_string(replaced) + " explicit note cell(s).",
            CommandEffect::PatternChanged);
    }

    if (verb == "hit" || verb == "rest" || verb == "repeat"
        || verb == "kill" || verb == "hold") {
        const bool isHit = verb == "hit";
        if ((isHit && tokens.size() != 3u && tokens.size() != 4u)
            || (!isHit && tokens.size() != 3u))
            return failure(isHit
                    ? "Usage: hit <lane|@alias> <row> [MIDI note]"
                    : "Usage: " + verb + " <lane|@alias> <row>");
        std::size_t lane = 0u;
        std::size_t row = 0u;
        std::string error;
        if (!parseLane(session, tokens[1], lane, error))
            return failure(std::move(error));
        if (!parseRow(tokens[2], row, error))
            return failure(std::move(error));
        NoteCell cell;
        if (isHit) {
            uint8_t note = anchorNote(session, lane);
            if (tokens.size() == 4u && !parseMidiNote(tokens[3], note))
                return failure("MIDI note must be 0..127 or a note name such as C-2.");
            cell = NoteCell::withNote(note);
        } else if (verb == "repeat") {
            cell = NoteCell::retriggerPrevious();
        } else if (verb == "kill") {
            cell = NoteCell::kill();
        } else if (verb == "hold") {
            cell = NoteCell::hold();
        } else {
            cell = NoteCell::rest();
        }
        auto& track = session.pattern.tracks[lane];
        ensureNoteStorage(session, track, row + 1u);
        track.notes[row] = cell;
        track.noteColumn.length = std::max(track.noteColumn.length, row + 1u);
        return success(verb + " wrote lane " + std::to_string(lane + 1u)
                + ", row " + std::to_string(row + 1u) + '.',
            CommandEffect::PatternChanged);
    }

    if (verb == "actions") {
        if (tokens.size() != 1u) return failure("Usage: actions");
        return success(fxActionsText());
    }

    if (verb == "fx1" || verb == "f1" || verb == "fx2"
        || verb == "f2") {
        if (tokens.size() < 4u)
            return failure("Usage: fx1|f1|fx2|f2 <target> <action> <sequence>");
        std::size_t lane = 0u;
        std::string error;
        if (!parseLane(session, tokens[1], lane, error))
            return failure(std::move(error));
        std::size_t pairIndex = (verb == "fx2" || verb == "f2") ? 1u : 0u;
        FxActionCell selectedAction;
        if (!parseFxActionToken(tokens[2], selectedAction))
            return failure("Unknown sequencing action or MIDI CC; use actions to list codes.");
        std::vector<ParsedFxSequenceCell> cells;
        if (!parseFxSequence(tokens, 3u, selectedAction, cells, error))
            return failure(std::move(error));
        auto& pair = session.pattern.tracks[lane].fxPairs[pairIndex];
        ensureFxStorage(session, pair, true, cells.size());
        ensureFxStorage(session, pair, false, cells.size());
        for (std::size_t row = 0u; row < cells.size(); ++row) {
            pair.actions[row] = cells[row].action;
            pair.values[row] = cells[row].value;
        }
        pair.actionColumn.length = cells.size();
        pair.valueColumn.length = cells.size();
        return success("Replaced " + laneLabel(session, lane) + " FX"
                + std::to_string(pairIndex + 1u) + " with "
                + std::to_string(cells.size()) + " rows.",
            CommandEffect::PatternChanged);
    }

    if (verb == "prob" || verb == "probability") {
        if (tokens.size() != 4u)
            return failure("Usage: prob|probability <target> <row> <0..1|0..100%|clear>");
        std::size_t lane = 0u;
        std::size_t row = 0u;
        std::string error;
        if (!parseLane(session, tokens[1], lane, error))
            return failure(std::move(error));
        if (!parseRow(tokens[2], row, error))
            return failure(std::move(error));
        return writeSequencerFxCell(session, lane, row,
            SequencerAction::Probability, tokens[3], "probability");
    }

        if (verb == "condition" || verb == "cond") {
            if (tokens.size() != 4u)
                return failure("Usage: condition|cond <target> <row> <1:2..8:8|FIRST|LAST|FILL|!FILL|clear>");
            std::size_t lane = 0u;
            std::size_t row = 0u;
            std::string error;
            if (!parseLane(session, tokens[1], lane, error))
                return failure(std::move(error));
            if (!parseRow(tokens[2], row, error))
                return failure(std::move(error));
            if (asciiLower(tokens[3]) == "clear")
                return writeSequencerFxCell(session, lane, row,
                    SequencerAction::Condition, "clear", "condition");
            const auto* condition = findSequencerCondition(tokens[3]);
            if (!condition)
                return failure("Condition must be 1:2..2:2, 1:4..4:4, 1:8..8:8, FIRST, LAST, FILL, or !FILL.");
            return writeSequencerFxCell(session, lane, row,
                SequencerAction::Condition,
                std::to_string(normalizedFromSequencerCondition(
                    condition->condition)), "condition");
        }

    if (verb == "ratchet" || verb == "retrig" || verb == "retrigger"
        || verb == "microtime" || verb == "micro" || verb == "delay"
        || verb == "flam" || verb == "stutter" || verb == "skip"
        || verb == "offset" || verb == "repeatprev" || verb == "accent"
        || verb == "ghost" || verb == "euclidfx") {
        if (tokens.size() != 4u)
            return failure("Usage: <FX writer> <target> <row> <0..1|clear>");
        SequencerAction action = SequencerAction::Ratchet;
        if (verb == "microtime" || verb == "micro")
            action = SequencerAction::MicroTime;
        else if (verb == "delay") action = SequencerAction::Delay;
        else if (verb == "flam") action = SequencerAction::Flam;
        else if (verb == "stutter") action = SequencerAction::Stutter;
        else if (verb == "skip") action = SequencerAction::Skip;
        else if (verb == "offset") action = SequencerAction::Offset;
        else if (verb == "repeatprev")
            action = SequencerAction::RepeatPrevious;
        else if (verb == "accent") action = SequencerAction::Accent;
        else if (verb == "ghost") action = SequencerAction::Ghost;
        else if (verb == "euclidfx") action = SequencerAction::Euclid;
        std::size_t lane = 0u;
        std::size_t row = 0u;
        std::string error;
        if (!parseLane(session, tokens[1], lane, error))
            return failure(std::move(error));
        if (!parseRow(tokens[2], row, error))
            return failure(std::move(error));
        return writeSequencerFxCell(session, lane, row, action, tokens[3],
            verb);
    }

    if (verb == "fx") {
        if (tokens.size() < 5u || tokens.size() > 6u) {
            return failure("Usage: fx <lane|@alias> <pair> <row> <clear|previous|sequencing-action value>");
        }
        std::size_t lane = 0u;
        std::size_t pair = 0u;
        std::size_t row = 0u;
        std::string error;
        if (!parseLane(session, tokens[1], lane, error))
            return failure(std::move(error));
        if (!parseFxPair(tokens[2], pair))
            return failure("FX pair must be 1, 2, fx1/f1, or fx2/f2.");
        if (!parseRow(tokens[3], row, error))
            return failure(std::move(error));

        auto& target = session.pattern.tracks[lane].fxPairs[pair];
        ensureFxStorage(session, target, true, row + 1u);
        const auto operation = asciiLower(tokens[4]);
        bool wroteValue = false;
        if (operation == "clear" || operation == "empty"
            || operation == "-") {
            if (tokens.size() != 5u)
                return failure("FX clear does not take a value or scope.");
            target.actions[row] = FxActionCell::empty();
        } else if (operation == "previous" || operation == "prev"
            || operation == "=") {
            if (tokens.size() != 5u)
                return failure("FX previous does not take a value or scope.");
            target.actions[row] = FxActionCell::previous();
        } else {
            if (tokens.size() != 6u)
                return failure("A sequencing action requires a value.");
            FxActionCell selectedAction;
            if (!parseFxActionToken(tokens[4], selectedAction))
                return failure("Unknown sequencing action or MIDI CC; use actions to list codes.");
            float value = 0.0f;
            if (!parseFxValueForAction(tokens[5], selectedAction, value))
                return failure(selectedAction.state
                        == FxActionCellState::MidiControlChange
                    ? "MIDI CC values must be integers 0..127 or normalized decimals 0..1."
                    : selectedAction.state == FxActionCellState::Sequencer
                            && selectedAction.sequencerAction
                                == SequencerAction::Condition
                        ? "CD values must be 1:2..2:2, 1:4..4:4, 1:8..8:8, FIRST, LAST, FILL, or !FILL."
                        : "FX values must be normalized between 0 and 1.");
            ensureFxStorage(session, target, false, row + 1u);
            target.actions[row] = selectedAction;
            target.values[row] = FxValueCell::withValue(value);
            wroteValue = true;
        }
        target.actionColumn.length = std::max(
            target.actionColumn.length, row + 1u);
        if (wroteValue) {
            target.valueColumn.length = std::max(
                target.valueColumn.length, row + 1u);
        }
        return success("Updated " + laneLabel(session, lane) + " FX"
                + std::to_string(pair + 1u) + ", row "
                + std::to_string(row + 1u) + '.',
            CommandEffect::PatternChanged);
    }

    if (verb == "interp" || verb == "interpolation") {
        if (tokens.size() != 4u)
            return failure("Usage: interp <target> <v1|v2> <step|linear>");
        std::size_t lane = 0u;
        std::string error;
        if (!parseLane(session, tokens[1], lane, error))
            return failure(std::move(error));
        const auto pairToken = asciiLower(tokens[2]);
        std::size_t pair = 0u;
        if (pairToken == "v1" || pairToken == "fx1"
            || pairToken == "f1" || pairToken == "1") {
            pair = 0u;
        } else if (pairToken == "v2" || pairToken == "fx2"
            || pairToken == "f2" || pairToken == "2") {
            pair = 1u;
        } else {
            return failure("Interpolation column must be V1 or V2.");
        }
        const auto mode = asciiLower(tokens[3]);
        if (mode == "step") {
            session.pattern.tracks[lane].fxPairs[pair].valueInterpolation
                = ValueInterpolation::Step;
        } else if (mode == "linear" || mode == "lin") {
            session.pattern.tracks[lane].fxPairs[pair].valueInterpolation
                = ValueInterpolation::Linear;
        } else {
            return failure("Interpolation mode must be STEP or LINEAR.");
        }
        return success("Set " + laneLabel(session, lane) + " V"
                + std::to_string(pair + 1u) + " interpolation to "
                + (mode == "step" ? "STEP." : "LINEAR."),
            CommandEffect::PatternChanged);
    }

    if (verb == "fxvalue" || verb == "fxv") {
        if (tokens.size() != 5u)
            return failure("Usage: fxvalue <lane|@alias> <pair> <row> <0..1|previous>");
        std::size_t lane = 0u;
        std::size_t pair = 0u;
        std::size_t row = 0u;
        std::string error;
        if (!parseLane(session, tokens[1], lane, error))
            return failure(std::move(error));
        if (!parseFxPair(tokens[2], pair))
            return failure("FX pair must be 1, 2, fx1/f1, or fx2/f2.");
        if (!parseRow(tokens[3], row, error))
            return failure(std::move(error));
        auto& target = session.pattern.tracks[lane].fxPairs[pair];
        ensureFxStorage(session, target, false, row + 1u);
        const auto valueToken = asciiLower(tokens[4]);
        if (valueToken == "previous" || valueToken == "prev"
            || valueToken == "=") {
            target.values[row] = FxValueCell::previous();
        } else {
            const bool midiValueLane = std::any_of(target.actions.begin(),
                target.actions.end(), [](const FxActionCell& action) {
                    return action.state
                        == FxActionCellState::MidiControlChange;
                });
            FxActionCell valueAction = midiValueLane
                ? FxActionCell::midiControlChange(0u)
                : FxActionCell::empty();
            if (row < target.actions.size()
                && target.actions[row].state == FxActionCellState::Sequencer
                && target.actions[row].sequencerAction
                    == SequencerAction::Condition) {
                valueAction = target.actions[row];
            }
            float value = 0.0f;
            if (!parseFxValueForAction(tokens[4], valueAction, value)) {
                return failure(midiValueLane
                    ? "MIDI CC values must be integers 0..127 or normalized decimals 0..1."
                    : valueAction.state == FxActionCellState::Sequencer
                            && valueAction.sequencerAction
                                == SequencerAction::Condition
                        ? "CD values must be 1:2..8:8, FIRST, LAST, FILL, or !FILL."
                        : "FX values must be normalized between 0 and 1.");
            }
            target.values[row] = FxValueCell::withValue(value);
        }
        target.valueColumn.length = std::max(
            target.valueColumn.length, row + 1u);
        return success("Updated " + laneLabel(session, lane) + " V"
                + std::to_string(pair + 1u) + ", row "
                + std::to_string(row + 1u) + '.',
            CommandEffect::PatternChanged);
    }

    if (verb == "len" || verb == "length" || verb == "stride"
        || verb == "speed" || verb == "spd" || verb == "dir"
        || verb == "mode" || verb == "phase" || verb == "ph") {
        const bool isLength = verb == "len" || verb == "length";
        const bool isStride = verb == "stride" || verb == "speed"
            || verb == "spd";
        if ((verb == "phase" || verb == "ph") && tokens.size() == 2u
            && asciiLower(tokens[1]) == "reset") {
            for (auto& track : session.pattern.tracks) {
                track.noteColumn.phase = 0u;
                track.instrumentColumn.phase = 0u;
                track.velocityColumn.phase = 0u;
                for (auto& pair : track.fxPairs) {
                    pair.actionColumn.phase = 0u;
                    pair.valueColumn.phase = 0u;
                }
            }
            return success("Reset every column phase in the current pattern.",
                CommandEffect::PatternChanged);
        }
        if (tokens.size() != 3u && tokens.size() != 4u)
            return failure("Usage: " + verb
                + " <lane|@alias> [note|ins|vel|fx1|v1|fx2|v2] <value>, or phase reset");
        std::size_t lane = 0u;
        std::string error;
        if (!parseLane(session, tokens[1], lane, error))
            return failure(std::move(error));
        ColumnTarget field;
        std::size_t valueIndex = 2u;
        if (tokens.size() == 4u) {
            if (!parseColumnTarget(tokens[2], field))
                return failure("Column field must be NOTE, INS, VEL, FX1, V1, FX2, or V2.");
            valueIndex = 3u;
        }
        auto& track = session.pattern.tracks[lane];
        auto& column = columnFor(track, field);

        if (isLength) {
            std::size_t length = 0u;
            if (!parseUnsigned(tokens[valueIndex], length) || length == 0u
                || length > kMaximumRows)
                return failure("Length must be between 1 and 256 rows.");
            if (field.kind == ColumnTargetKind::Note)
                ensureNoteStorage(session, track, length);
            else if (field.kind == ColumnTargetKind::Instrument)
                ensureInstrumentStorage(session, track, length);
            else if (field.kind == ColumnTargetKind::Velocity)
                ensureVelocityStorage(session, track, length);
            else
                ensureFxStorage(session, track.fxPairs[field.fxIndex],
                    field.kind == ColumnTargetKind::FxAction, length);
            column.length = length;
            return success("Set " + laneLabel(session, lane) + ' '
                    + columnName(field) + " length to "
                    + std::to_string(length) + '.',
                CommandEffect::PatternChanged);
        }
        if (isStride) {
            uint32_t stride = 0u;
            if (!parseUnsigned(tokens[valueIndex], stride) || stride == 0u)
                return failure("Stride must be an integer between 1 and 4294967295.");
            column.stride = stride;
            return success("Set " + laneLabel(session, lane) + ' '
                    + columnName(field) + " stride to "
                    + std::to_string(stride) + '.',
                CommandEffect::PatternChanged);
        }
        if (verb == "phase" || verb == "ph") {
            int64_t phase = 0;
            if (!parseSigned(tokens[valueIndex], phase))
                return failure("Phase must be a whole number of rows.");
            if (column.length == 0u)
                return failure("Cannot phase a zero-length column.");
            if (field.kind == ColumnTargetKind::Note)
                ensureNoteStorage(session, track, column.length);
            else if (field.kind == ColumnTargetKind::Instrument)
                ensureInstrumentStorage(session, track, column.length);
            else if (field.kind == ColumnTargetKind::Velocity)
                ensureVelocityStorage(session, track, column.length);
            else
                ensureFxStorage(session, track.fxPairs[field.fxIndex],
                    field.kind == ColumnTargetKind::FxAction,
                    column.length);
            column.phase = normalizedRotation(phase, column.length);
            return success("Set " + laneLabel(session, lane) + ' '
                    + columnName(field) + " phase to "
                    + std::to_string(column.phase) + '.',
                CommandEffect::PatternChanged);
        }

        Direction direction = Direction::Forward;
        std::string canonical;
        if (!parseDirection(tokens[valueIndex], direction, canonical))
            return failure("Direction must be forward, reverse, palindrome, or random.");
        column.direction = direction;
        return success("Set " + laneLabel(session, lane) + ' '
                + columnName(field) + " direction to " + canonical + '.',
            CommandEffect::PatternChanged);
    }

    if (verb == "mute") {
        if (tokens.size() < 2u || tokens.size() > 4u)
            return failure("Usage: mute <lane|@alias> [note|ins|vel|fx1|v1|fx2|v2] [on|off|toggle]");
        std::size_t lane = 0u;
        std::string error;
        if (!parseLane(session, tokens[1], lane, error))
            return failure(std::move(error));
        ColumnTarget field;
        std::string mode = "toggle";
        if (tokens.size() >= 3u) {
            if (parseColumnTarget(tokens[2], field)) {
                if (tokens.size() == 4u) mode = asciiLower(tokens[3]);
            } else {
                if (tokens.size() == 4u)
                    return failure("Unknown column field for mute.");
                mode = asciiLower(tokens[2]);
            }
        }
        auto& muted = columnFor(session.pattern.tracks[lane], field).muted;
        if (mode == "on") muted = true;
        else if (mode == "off") muted = false;
        else if (mode == "toggle") muted = !muted;
        else return failure("Mute value must be on, off, or toggle.");
        return success(laneLabel(session, lane) + ' ' + columnName(field)
                + (muted ? " muted." : " unmuted."),
            CommandEffect::PatternChanged);
    }
    if (verb == "unmute") {
        if (tokens.size() != 2u)
            return failure("Usage: unmute <lane|@alias|all>");
        if (asciiLower(tokens[1]) == "all") {
            for (auto& track : session.pattern.tracks)
                track.noteColumn.muted = false;
            return success("Unmuted all lanes.", CommandEffect::PatternChanged);
        }
        std::size_t lane = 0u;
        std::string error;
        if (!parseLane(session, tokens[1], lane, error))
            return failure(std::move(error));
        session.pattern.tracks[lane].noteColumn.muted = false;
        return success(laneLabel(session, lane) + " unmuted.",
            CommandEffect::PatternChanged);
    }
    if (verb == "solo") {
        if (tokens.size() < 2u)
            return failure("Usage: solo <lane|@alias> [lane|@alias ...]");
        std::vector<std::size_t> soloLanes;
        for (std::size_t index = 1u; index < tokens.size(); ++index) {
            std::size_t lane = 0u;
            std::string error;
            if (!parseLane(session, tokens[index], lane, error))
                return failure(std::move(error));
            if (std::find(soloLanes.begin(), soloLanes.end(), lane)
                == soloLanes.end())
                soloLanes.push_back(lane);
        }
        for (std::size_t lane = 0u;
             lane < session.pattern.tracks.size(); ++lane) {
            session.pattern.tracks[lane].noteColumn.muted
                = std::find(soloLanes.begin(), soloLanes.end(), lane)
                    == soloLanes.end();
        }
        return success("Soloed " + std::to_string(soloLanes.size())
                + " lane(s).",
            CommandEffect::PatternChanged);
    }
    if (verb == "name") {
        if (tokens.size() < 3u)
            return failure("Usage: name <lane|@alias> <name>");
        std::size_t lane = 0u;
        std::string error;
        if (!parseLane(session, tokens[1], lane, error))
            return failure(std::move(error));
        session.pattern.tracks[lane].name = joinWords(tokens, 2u);
        return success("Named lane " + std::to_string(lane + 1u) + " "
                + session.pattern.tracks[lane].name + '.',
            CommandEffect::PatternChanged);
    }
    if (verb == "track") {
        if (tokens.size() < 2u)
            return failure("Usage: track <add [name...]|remove <target>>");
        const auto operation = asciiLower(tokens[1]);
        if (operation == "add") {
            if (session.pattern.tracks.size() >= kMaximumTrackCount)
                return failure("This build supports up to 32 tracks.");
            const std::size_t lane = session.pattern.tracks.size();
            const std::size_t rows = std::clamp<std::size_t>(
                session.pattern.visibleRows, 1u, kMaximumRows);
            Track track;
            track.name = tokens.size() > 2u ? joinWords(tokens, 2u)
                : "Track " + std::to_string(lane + 1u);
            track.midiChannel = 1u;
            track.initialInstrumentNodeId = kMidiOutInstrumentNode;
            track.destination = EventDestination::Midi;
            track.notes.assign(rows, NoteCell::rest());
            track.instruments.assign(rows, InstrumentCell::empty());
            track.velocities.assign(rows, ValueCell::defaultValue());
            track.noteColumn.length = rows;
            track.instrumentColumn.length = rows;
            track.velocityColumn.length = rows;
            ensureDefaultFxColumns(track, rows);
            session.pattern.tracks.push_back(std::move(track));
            ensureLaneDefaultNotes(session);
            session.selectedTrack = lane;
            return success("Added lane " + std::to_string(lane + 1u)
                    + ".",
                CommandEffect::PatternChanged
                    | CommandEffect::SelectionChanged);
        }
        if (operation == "remove") {
            if (tokens.size() != 3u)
                return failure("Usage: track remove <lane|@alias>");
            if (session.pattern.tracks.size() <= 1u)
                return failure("A pattern must retain at least one track.");
            std::size_t lane = 0u;
            std::string error;
            if (!parseLane(session, tokens[2], lane, error))
                return failure(std::move(error));
            session.pattern.tracks.erase(session.pattern.tracks.begin()
                + static_cast<std::ptrdiff_t>(lane));
            if (lane < session.laneDefaultNotes.size()) {
                session.laneDefaultNotes.erase(
                    session.laneDefaultNotes.begin()
                        + static_cast<std::ptrdiff_t>(lane));
            }
            for (auto it = session.aliases.begin(); it != session.aliases.end();) {
                if (it->second == lane) it = session.aliases.erase(it);
                else {
                    if (it->second > lane) --it->second;
                    ++it;
                }
            }
            session.selectedTrack = std::min(session.selectedTrack,
                session.pattern.tracks.size() - 1u);
            return success("Removed lane " + std::to_string(lane + 1u)
                    + ".",
                CommandEffect::PatternChanged
                    | CommandEffect::SelectionChanged);
        }
        return failure("Track operation must be add or remove.");
    }
    if (verb == "out" || verb == "route")
        return failure("Routing now follows the song instrument index. Use instrument <lane> <0..2|kick|sampler|midi>.");

    if (verb == "instrument" || verb == "inst") {
        if (tokens.size() != 3u && tokens.size() != 4u) {
            return failure("Usage: instrument <lane|@alias> [row] <kick|sampler|midi|0..2|previous|clear>");
        }
        std::size_t lane = 0u;
        std::string error;
        if (!parseLane(session, tokens[1], lane, error))
            return failure(std::move(error));

        if (tokens.size() == 4u) {
            std::size_t row = 0u;
            if (!parseRow(tokens[2], row, error))
                return failure(std::move(error));
            const auto value = asciiLower(tokens[3]);
            InstrumentCell cell;
            std::string description;
            if (value == "clear" || value == "empty" || value == "-") {
                cell = InstrumentCell::empty();
                description = "clear";
            } else if (value == "previous" || value == "prev"
                || value == "prv" || value == "=") {
                cell = InstrumentCell::previous();
                description = "previous";
            } else {
                uint32_t nodeId = kInvalidInstrumentNode;
                if (!parseInstrumentSlot(tokens[3], nodeId, description)) {
                    return failure("Instrument cell must be kick, sampler, midi, a default slot from 0 to 2, previous, or clear; assign added devices from the song index.");
                }
                cell = InstrumentCell::withInstrument(nodeId);
            }
            auto& track = session.pattern.tracks[lane];
            // A first edit should populate the existing default 16-row column,
            // not collapse it to the edited row when playback normalizes the
            // data-backed active length. `len ... ins` remains the deliberate
            // way to choose a shorter polymeter.
            ensureInstrumentStorage(session, track, std::max(
                { std::size_t { 16u }, track.instrumentColumn.length,
                    row + 1u }));
            track.instruments[row] = cell;
            track.instrumentColumn.length = std::max(
                track.instrumentColumn.length, row + 1u);
            return success("Updated " + laneLabel(session, lane)
                    + " INS row " + std::to_string(row + 1u) + " to "
                    + description + '.',
                CommandEffect::PatternChanged);
        }

        uint32_t nodeId = kInvalidInstrumentNode;
        std::string instrumentName;
        if (!parseInstrumentSlot(tokens[2], nodeId, instrumentName)) {
            return failure("Instrument must be kick, sampler, midi, or a default slot from 0 to 2; assign added devices from the song index.");
        }
        session.pattern.tracks[lane].initialInstrumentNodeId = nodeId;
        session.pattern.tracks[lane].destination = destinationForInstrument(
            nodeId, EventDestination::None);
        return success("Set " + laneLabel(session, lane)
                + " initial instrument to "
                + instrumentName + " (rack slot "
                + std::to_string(nodeId) + ").",
            CommandEffect::PatternChanged | CommandEffect::RoutingChanged);
    }

    if (verb == "note" || verb == "vel") {
        if (tokens.size() != 4u)
            return failure("Usage: " + verb
                + " <lane|@alias> <row> <value>");
        std::size_t lane = 0u;
        std::size_t row = 0u;
        std::string error;
        if (!parseLane(session, tokens[1], lane, error))
            return failure(std::move(error));
        if (!parseRow(tokens[2], row, error))
            return failure(std::move(error));

        if (verb == "note") {
            NoteCell cell;
            const auto value = asciiLower(tokens[3]);
            uint8_t midiNote = 0u;
            std::size_t burstSlot = 0u;
            if (value == "rest") cell = NoteCell::rest();
            else if (value == "rpt") cell = NoteCell::retriggerPrevious();
            else if (value == "kill") cell = NoteCell::kill();
            else if (value == "hold" || value == "hld")
                cell = NoteCell::hold();
            else if (parseBurstSlot(tokens[3], burstSlot)) {
                if (session.pattern.bursts[burstSlot].empty())
                    return failure(burstSlotToken(burstSlot)
                        + " is empty; define it before placing a reference.");
                cell = NoteCell::withBurst(
                    static_cast<uint8_t>(burstSlot));
            }
            else if (parseMidiNote(tokens[3], midiNote))
                cell = NoteCell::withNote(midiNote);
            else
                return failure("Note must be MIDI 0..127, a note name, B01..B32, rest, rpt, hold, or kill.");
            auto& track = session.pattern.tracks[lane];
            ensureNoteStorage(session, track, row + 1u);
            track.notes[row] = cell;
            track.noteColumn.length = std::max(track.noteColumn.length,
                row + 1u);
            return success("Updated lane " + std::to_string(lane + 1u)
                    + ", row " + std::to_string(row + 1u) + '.',
                CommandEffect::PatternChanged);
        }

        uint32_t velocity = 0u;
        if (!parseUnsigned(tokens[3], velocity) || velocity > 127u)
            return failure("Velocity must be an integer between 0 and 127.");
        auto& track = session.pattern.tracks[lane];
        ensureVelocityStorage(session, track, row + 1u);
        track.velocities[row] = ValueCell::withValue(
            static_cast<float>(velocity) / 127.0f);
        track.velocityColumn.length = std::max(track.velocityColumn.length,
            row + 1u);
        return success("Set lane " + std::to_string(lane + 1u)
                + ", row " + std::to_string(row + 1u) + " velocity to "
                + std::to_string(velocity) + '.',
            CommandEffect::PatternChanged);
    }

    if (verb == "velseq" || verb == "vol") {
        if (tokens.size() < 3u)
            return failure("Usage: velseq|vol <lane|@alias> <pattern|values...>");
        std::size_t lane = 0u;
        std::string error;
        if (!parseLane(session, tokens[1], lane, error))
            return failure(std::move(error));
        std::vector<ValueCell> cells;
        if (!parseVelocitySequence(tokens, 2u, cells, error))
            return failure(std::move(error));
        if (cells.size() > kMaximumRows)
            return failure("Velocity sequences may contain at most 256 values.");
        auto& track = session.pattern.tracks[lane];
        ensureVelocityStorage(session, track, cells.size());
        std::copy(cells.begin(), cells.end(), track.velocities.begin());
        track.velocityColumn.length = cells.size();
        return success("Applied a " + std::to_string(cells.size())
                + "-step velocity sequence to " + laneLabel(session, lane)
                + '.',
            CommandEffect::PatternChanged);
    }

    if (verb == "random" || verb == "randomize" || verb == "rand") {
        if (tokens.size() < 2u || tokens.size() > 5u)
            return failure("Usage: randomize <lane|@alias> [vel] [minimum maximum]");
        std::size_t lane = 0u;
        std::string error;
        if (!parseLane(session, tokens[1], lane, error))
            return failure(std::move(error));
        std::size_t cursor = 2u;
        if (cursor < tokens.size()) {
            const auto field = asciiLower(tokens[cursor]);
            if (field == "vel" || field == "vol" || field == "velocity"
                || field == "v") {
                ++cursor;
            } else if (!std::isdigit(static_cast<unsigned char>(
                           tokens[cursor].empty() ? '\0'
                                                  : tokens[cursor].front()))) {
                return failure("Randomize currently supports the VEL column; use randomize <target> vel [minimum maximum].");
            }
        }
        uint32_t minimum = 1u;
        uint32_t maximum = 127u;
        if (cursor < tokens.size()) {
            if (cursor + 2u != tokens.size()
                || !parseUnsigned(tokens[cursor], minimum)
                || !parseUnsigned(tokens[cursor + 1u], maximum)
                || minimum > maximum || maximum > 127u) {
                return failure("Random velocity bounds must satisfy 0 <= minimum <= maximum <= 127.");
            }
        }
        auto& track = session.pattern.tracks[lane];
        const auto length = std::clamp<std::size_t>(
            track.velocityColumn.length, 1u, kMaximumRows);
        ensureVelocityStorage(session, track, length);
        const uint32_t span = maximum - minimum + 1u;
        for (std::size_t row = 0u; row < length; ++row) {
            const auto offset = static_cast<uint32_t>(std::min<double>(
                static_cast<double>(span - 1u),
                std::floor(nextCommandRandom(session)
                    * static_cast<double>(span))));
            track.velocities[row] = ValueCell::withValue(
                static_cast<float>(minimum + offset) / 127.0f);
        }
        return success("Randomized " + laneLabel(session, lane)
                + " VEL rows " + std::to_string(minimum) + "–"
                + std::to_string(maximum) + ".",
            CommandEffect::PatternChanged);
    }

    if (verb == "mask") {
        if (tokens.size() != 3u && tokens.size() != 4u)
            return failure("Usage: mask <lane|@alias> <x---...> [direction]");
        std::size_t lane = 0u;
        std::string error;
        if (!parseLane(session, tokens[1], lane, error))
            return failure(std::move(error));
        if (tokens[2].empty() || tokens[2].size() > kMaximumRows)
            return failure("Mask must contain between 1 and 256 steps.");
        if (!isMaskLiteral(tokens[2]))
            return failure("Mask symbols are x/X for hits and - for rests.");
        Direction direction = session.pattern.tracks[lane].noteColumn.direction;
        std::string canonical;
        if (tokens.size() == 4u
            && !parseDirection(tokens[3], direction, canonical))
            return failure("Unknown mask direction; use forward, reverse, random, or palindrome.");
        applyMask(session, lane, tokens[2]);
        if (tokens.size() == 4u)
            session.pattern.tracks[lane].noteColumn.direction = direction;
        return success("Applied a " + std::to_string(tokens[2].size())
                + "-step mask to " + laneLabel(session, lane) + '.',
            CommandEffect::PatternChanged);
    }

    if (verb == "eu" || verb == "e" || verb == "euclid") {
        if (tokens.size() < 4u || tokens.size() > 6u)
            return failure("Usage: eu <lane|@alias> <pulses> <steps> [rotate] [direction]");
        std::size_t lane = 0u;
        std::string error;
        if (!parseLane(session, tokens[1], lane, error))
            return failure(std::move(error));
        std::size_t pulses = 0u;
        std::size_t steps = 0u;
        if (!parseUnsigned(tokens[2], pulses)
            || !parseUnsigned(tokens[3], steps) || steps == 0u
            || steps > kMaximumRows || pulses > steps * 8u)
            return failure("Euclid requires 0..8×steps pulses and 1..256 steps; overfull rhythms use automatic 2..8-way SEQ ratchets.");
        int64_t rotation = 0;
        if (tokens.size() >= 5u && !parseSigned(tokens[4], rotation))
            return failure("Euclid rotation must be a whole number.");
        Direction direction = session.pattern.tracks[lane].noteColumn.direction;
        std::string canonical;
        if (tokens.size() == 6u
            && !parseDirection(tokens[5], direction, canonical))
            return failure("Unknown Euclid direction; use forward, reverse, random, or palindrome.");

        const bool overfull = pulses > steps;
        if (overfull && direction == Direction::Random)
            return failure("Overfull Euclid cannot align NOTE and SEQ ratchets in random direction; use forward, reverse, or palindrome.");

        auto& track = session.pattern.tracks[lane];
        std::size_t ratchetPair = kFxPairCount;
        std::size_t bestExistingRatchets = 0u;
        if (overfull) {
            // One complete SEQ pair owns the generated burst lane so its
            // action/value heads can remain phase-locked to NOTE. Existing
            // RR cells are reusable; unrelated actions are never overwritten.
            for (std::size_t pairIndex = 0u; pairIndex < kFxPairCount;
                 ++pairIndex) {
                const auto& pair = track.fxPairs[pairIndex];
                bool available = true;
                std::size_t existingRatchets = 0u;
                for (std::size_t row = 0u; row < steps; ++row) {
                    if (row >= pair.actions.size()
                        || pair.actions[row].state
                            == FxActionCellState::Empty)
                        continue;
                    if (fxCellNamesAction(pair.actions[row],
                            SequencerAction::Ratchet)) {
                        ++existingRatchets;
                        continue;
                    }
                    available = false;
                    break;
                }
                if (available && (ratchetPair == kFxPairCount
                        || existingRatchets > bestExistingRatchets)) {
                    ratchetPair = pairIndex;
                    bestExistingRatchets = existingRatchets;
                }
            }
            if (ratchetPair == kFxPairCount)
                return failure("Overfull Euclid needs one SEQ pair whose active rows are empty or already RR; clear SEQ1 or SEQ2 first.");
        }

        std::string mask(steps, '-');
        const auto normalized = normalizedRotation(rotation, steps);
        for (std::size_t row = 0u; row < steps; ++row) {
            const bool hit = ((row + 1u) * pulses) / steps
                != (row * pulses) / steps;
            if (hit) mask[(row + normalized) % steps] = 'x';
        }
        applyMask(session, lane, mask);
        if (tokens.size() == 6u)
            track.noteColumn.direction = direction;
        track.noteColumn.phase %= steps;

        // `eu` owns prior automatic RR cells across the generated span. This
        // makes a later conventional Euclid remove stale bursts and prevents
        // a RR in the other pair from overriding the newly generated count.
        for (auto& pair : track.fxPairs) {
            for (std::size_t row = 0u;
                 row < steps && row < pair.actions.size(); ++row) {
                if (!fxCellNamesAction(pair.actions[row],
                        SequencerAction::Ratchet))
                    continue;
                pair.actions[row] = FxActionCell::empty();
                if (row < pair.values.size())
                    pair.values[row] = FxValueCell::previous();
            }
        }

        std::size_t maximumBurst = 1u;
        if (overfull) {
            auto& pair = track.fxPairs[ratchetPair];
            ensureFxStorage(session, pair, true, steps);
            ensureFxStorage(session, pair, false, steps);
            for (std::size_t row = 0u; row < steps; ++row) {
                const std::size_t burst = ((row + 1u) * pulses) / steps
                    - (row * pulses) / steps;
                const auto destination = (row + normalized) % steps;
                maximumBurst = std::max(maximumBurst, burst);
                if (burst <= 1u) continue;
                pair.actions[destination] = FxActionCell::sequencer(
                    SequencerAction::Ratchet);
                pair.values[destination] = FxValueCell::withValue(
                    static_cast<float>(burst - 2u) / 6.0f);
            }
            pair.actionColumn.length = steps;
            pair.valueColumn.length = steps;
            pair.actionColumn.stride = track.noteColumn.stride;
            pair.valueColumn.stride = track.noteColumn.stride;
            pair.actionColumn.phase = track.noteColumn.phase;
            pair.valueColumn.phase = track.noteColumn.phase;
            pair.actionColumn.direction = track.noteColumn.direction;
            pair.valueColumn.direction = track.noteColumn.direction;
            pair.actionColumn.muted = false;
            pair.valueColumn.muted = false;
        }

        std::string message = "Applied Euclid " + std::to_string(pulses) + '/'
                + std::to_string(steps) + " rotate "
                + std::to_string(rotation) + " to "
                + laneLabel(session, lane);
        if (overfull) {
            message += " with aligned SEQ" + std::to_string(ratchetPair + 1u)
                + " ratchets up to " + std::to_string(maximumBurst)
                + " onsets per step";
        }
        message += '.';
        return success(std::move(message),
            CommandEffect::PatternChanged);
    }

    if (verb == "rotate" || verb == "rot") {
        if (tokens.size() != 3u)
            return failure("Usage: rotate <lane|@alias> <steps>");
        std::size_t lane = 0u;
        std::string error;
        if (!parseLane(session, tokens[1], lane, error))
            return failure(std::move(error));
        int64_t amount = 0;
        if (!parseSigned(tokens[2], amount))
            return failure("Rotation must be a whole number.");
        if (session.pattern.tracks[lane].noteColumn.length == 0u)
            return failure("Cannot rotate a NOTE column with zero active length.");
        rotateNotes(session, lane, amount);
        return success("Rotated " + laneLabel(session, lane) + " by "
                + std::to_string(amount) + " step(s).",
            CommandEffect::PatternChanged);
    }
    if (verb == "reverse") {
        if (tokens.size() != 2u)
            return failure("Usage: reverse <lane|@alias>");
        std::size_t lane = 0u;
        std::string error;
        if (!parseLane(session, tokens[1], lane, error))
            return failure(std::move(error));
        reverseNotes(session, lane);
        return success("Reversed " + laneLabel(session, lane) + '.',
            CommandEffect::PatternChanged);
    }
    if (verb == "fill") {
        if (tokens.size() != 3u && tokens.size() != 4u)
            return failure("Usage: fill <lane|@alias> <every> [offset]");
        std::size_t lane = 0u;
        std::string error;
        if (!parseLane(session, tokens[1], lane, error))
            return failure(std::move(error));
        std::size_t every = 0u;
        if (!parseUnsigned(tokens[2], every) || every == 0u
            || every > kMaximumRows)
            return failure("Fill interval must be between 1 and 256.");
        int64_t offset = 0;
        if (tokens.size() == 4u && !parseSigned(tokens[3], offset))
            return failure("Fill offset must be a whole number.");
        auto& track = session.pattern.tracks[lane];
        const auto length = track.noteColumn.length;
        ensureNoteStorage(session, track, length);
        const auto pitch = anchorNote(session, lane);
        const auto signedEvery = static_cast<int64_t>(every);
        auto normalizedOffset = offset % signedEvery;
        if (normalizedOffset < 0) normalizedOffset += signedEvery;
        for (std::size_t row = 0u; row < length; ++row) {
            if (row % every == static_cast<std::size_t>(normalizedOffset)
                && track.notes[row].state == NoteCellState::Rest)
                track.notes[row] = NoteCell::withNote(pitch);
        }
        return success("Filled every " + std::to_string(every)
                + " step(s) on " + laneLabel(session, lane) + '.',
            CommandEffect::PatternChanged);
    }

    if (verb == "sieve") {
        if (tokens.size() < 4u)
            return failure("Usage: sieve <lane|@alias> [note] <modulus> <residue...>");
        std::size_t lane = 0u;
        std::string error;
        if (!parseLane(session, tokens[1], lane, error))
            return failure(std::move(error));
        std::size_t cursor = 2u;
        (void)parseOptionalNoteField(tokens, cursor);
        std::size_t modulus = 0u;
        if (cursor >= tokens.size()
            || !parseUnsigned(tokens[cursor++], modulus)
            || modulus == 0u || modulus > kMaximumRows)
            return failure("Sieve modulus must be between 1 and 256.");
        if (cursor >= tokens.size())
            return failure("Sieve requires at least one residue.");
        std::vector<bool> residues(modulus, false);
        for (; cursor < tokens.size(); ++cursor) {
            int64_t residue = 0;
            if (!parseSigned(tokens[cursor], residue))
                return failure("Sieve residues must be whole numbers.");
            const auto signedModulus = static_cast<int64_t>(modulus);
            auto normalized = residue % signedModulus;
            if (normalized < 0) normalized += signedModulus;
            residues[static_cast<std::size_t>(normalized)] = true;
        }
        std::string mask(modulus, '-');
        for (std::size_t row = 0u; row < modulus; ++row)
            if (residues[row]) mask[row] = 'x';
        applyMask(session, lane, mask);
        return success("Applied modulus " + std::to_string(modulus)
                + " sieve to " + laneLabel(session, lane) + '.',
            CommandEffect::PatternChanged);
    }

    if (verb == "density" || verb == "thin" || verb == "humanize") {
        if (tokens.size() != 3u && tokens.size() != 4u)
            return failure("Usage: " + verb
                + " <lane|@alias> [note] <0..1>");
        std::size_t lane = 0u;
        std::string error;
        if (!parseLane(session, tokens[1], lane, error))
            return failure(std::move(error));
        std::size_t cursor = 2u;
        (void)parseOptionalNoteField(tokens, cursor);
        double amount = 0.0;
        if (cursor + 1u != tokens.size()
            || !parseFiniteDouble(tokens[cursor], amount)
            || amount < 0.0 || amount > 1.0)
            return failure("Amount must be normalized between 0 and 1.");
        auto& track = session.pattern.tracks[lane];
        const auto length = track.noteColumn.length;
        if (length == 0u)
            return failure("Cannot transform a NOTE column with zero active length.");
        ensureNoteStorage(session, track, length);
        const auto pitch = anchorNote(session, lane);
        std::size_t changed = 0u;
        if (verb == "density") {
            for (std::size_t row = 0u; row < length; ++row) {
                if (nextCommandRandom(session) < amount) {
                    if (!noteCellIsHit(track.notes[row]))
                        track.notes[row] = NoteCell::withNote(pitch);
                } else {
                    track.notes[row] = NoteCell::rest();
                }
            }
        } else if (verb == "thin") {
            for (std::size_t row = 0u; row < length; ++row) {
                if (noteCellIsHit(track.notes[row])
                    && nextCommandRandom(session) < amount) {
                    track.notes[row] = NoteCell::rest();
                    ++changed;
                }
            }
        } else {
            const auto source = std::vector<NoteCell>(track.notes.begin(),
                track.notes.begin() + static_cast<std::ptrdiff_t>(length));
            auto next = source;
            for (std::size_t row = 0u; row < length; ++row) {
                if (!noteCellIsHit(source[row])
                    || nextCommandRandom(session) >= amount) continue;
                const int direction = nextCommandRandom(session) < 0.5
                    ? -1 : 1;
                const auto destination = static_cast<std::size_t>((
                    static_cast<int64_t>(row) + direction
                        + static_cast<int64_t>(length))
                    % static_cast<int64_t>(length));
                if (noteCellIsHit(source[destination])) continue;
                next[destination] = source[row];
                next[row] = NoteCell::rest();
                ++changed;
            }
            std::copy(next.begin(), next.end(), track.notes.begin());
        }
        return success(verb + " transformed " + laneLabel(session, lane)
                + (verb == "density" ? "." : " (" + std::to_string(changed)
                    + " changed)."),
            CommandEffect::PatternChanged);
    }

    if (verb == "rotatehits") {
        if (tokens.size() != 3u && tokens.size() != 4u)
            return failure("Usage: rotatehits <lane|@alias> [note] <steps>");
        std::size_t lane = 0u;
        std::string error;
        if (!parseLane(session, tokens[1], lane, error))
            return failure(std::move(error));
        std::size_t cursor = 2u;
        (void)parseOptionalNoteField(tokens, cursor);
        int64_t amount = 0;
        if (cursor + 1u != tokens.size()
            || !parseSigned(tokens[cursor], amount))
            return failure("Hit rotation must be a whole number.");
        auto& track = session.pattern.tracks[lane];
        const auto length = track.noteColumn.length;
        if (length == 0u)
            return failure("Cannot rotate hits in a zero-length NOTE column.");
        ensureNoteStorage(session, track, length);
        const auto source = std::vector<NoteCell>(track.notes.begin(),
            track.notes.begin() + static_cast<std::ptrdiff_t>(length));
        std::fill_n(track.notes.begin(), length, NoteCell::rest());
        const auto rotation = normalizedRotation(amount, length);
        for (std::size_t row = 0u; row < length; ++row) {
            if (noteCellIsHit(source[row]))
                track.notes[(row + rotation) % length] = source[row];
        }
        return success("Rotated hits on " + laneLabel(session, lane)
                + " by " + std::to_string(amount) + " step(s).",
            CommandEffect::PatternChanged);
    }

    return failure("Unknown command \"" + tokens[0]
        + "\". Type help for the command list.");
}

} // namespace

uint8_t laneDefaultNote(const TrackerSession& session,
    std::size_t lane) noexcept
{
    if (lane < session.laneDefaultNotes.size())
        return session.laneDefaultNotes[lane];
    if (lane < session.pattern.tracks.size()) {
        const auto& notes = session.pattern.tracks[lane].notes;
        const auto found = std::find_if(notes.begin(), notes.end(),
            [](const NoteCell& cell) {
                return cell.state == NoteCellState::Note;
            });
        if (found != notes.end()) return found->note;
    }
    return fallbackNoteForLane(lane);
}

bool setLaneDefaultNote(TrackerSession& session, std::size_t lane,
    uint8_t note, std::size_t* replacedNoteCount)
{
    if (replacedNoteCount) *replacedNoteCount = 0u;
    if (lane >= session.pattern.tracks.size()) return false;
    const auto previous = laneDefaultNote(session, lane);
    ensureLaneDefaultNotes(session);
    bool changed = session.laneDefaultNotes[lane] != note;
    session.laneDefaultNotes[lane] = note;
    for (auto& cell : session.pattern.tracks[lane].notes) {
        if (cell.state != NoteCellState::Note) continue;
        if (replacedNoteCount) ++*replacedNoteCount;
        changed |= cell.note != note;
        cell.note = note;
    }
    return changed || previous != note;
}

bool patternVariationLaunchIsDue(PatternVariationLaunch launch,
    uint64_t completedTickIndex, uint64_t completedTransportRow,
    uint32_t ticksPerBeat, std::size_t patternRows) noexcept
{
    if (launch == PatternVariationLaunch::NextTick) return true;
    if (launch == PatternVariationLaunch::NextBeat) {
        return ((completedTickIndex + 1u)
            % std::max<uint32_t>(ticksPerBeat, 1u)) == 0u;
    }
    if (launch == PatternVariationLaunch::NextPatternCycle
        || launch == PatternVariationLaunch::NextSongRow) {
        // A Song runtime supplies its true row boundary. Before Song mode has
        // reached the audio thread, use the current pattern cycle as the
        // nearest meaningful boundary so SELECT QUEUE cannot be stranded.
        return ((completedTransportRow + 1u)
            % std::max<std::size_t>(patternRows, 1u)) == 0u;
    }
    return false;
}

std::string CommandEngine::helpText()
{
    std::ostringstream stream;
    stream << "s3g Tracker console commands\n";
    for (const auto& section : helpSections()) {
        stream << '\n' << section.title << '\n';
        for (const auto& entry : section.entries) {
            stream << "  " << entry.syntax << "\n    "
                   << entry.description << "\n    Example: "
                   << entry.example << '\n';
        }
    }
    stream
        << "\nTargets are one-based lane numbers or @aliases; rows are one-based.\n"
        << "Columns: note, vol, fx1, v1, fx2, v2. "
        << "Directions: forward (>), reverse (<), palindrome (<>), or the word random.\n"
        << "Randomize materializes repeatable values into the targeted VOL column.";
    return stream.str();
}

const std::vector<CommandHelpSection>& CommandEngine::helpSections()
{
    static const std::vector<CommandHelpSection> sections {
        { "ESSENTIALS", {
            { "help  |  ?", "Show this complete command reference.", "help ?", "help" },
            { "undo", "Restore the preceding persistent Tracker project state.", "undo", "undo" },
            { "redo", "Restore the next state after an undo.", "redo", "redo" },
            { "demo", "Load the General MIDI tracker demonstration.", "demo", "demo" },
            { "play", "Ask the host to continue playback from its current position.", "play", "play" },
            { "stop", "Ask the host to stop playback and clean up active notes.", "stop", "stop" },
            { "panic", "Send MIDI All Notes Off and reset active voices.", "panic", "panic" },
        } },
        { "TRANSPORT & TIMING", {
            { "swing <0.50..0.75 | 50..75>", "Set traditional pair swing.", "swing", "swing 58" },
            { "gate <1..5000 ms>", "Set external MIDI note-gate duration.", "gate", "gate 90" },
            { "loop <on|off|toggle>  |  loop [rows] <start> <end>", "Toggle the global loop or set its inclusive one-based row region.", "loop", "loop 1 16" },
            { "warps", "List the current composition and indexed warp library.", "warps", "warps" },
            { "warp on|off|toggle", "Enable or bypass the current Pattern timing-warp composition.", "", "warp on" },
            { "warp clear", "Remove every timing transform.", "warp", "warp clear" },
            { "warp save <1..64> [name]", "Store the current composed stack and cycle in a library slot.", "", "warp save 1 GROOVE" },
            { "warp load|recall|use <1..64>", "Recall a library warp immediately.", "", "warp load 1" },
            { "warp rename <1..64> <name>  |  warp delete <1..64>", "Edit indexed warp-library metadata.", "", "warp rename 1 SWUNG GROOVE" },
            { "warp cycle <1..16 ticks>", "Set the repeating live-warp cycle.", "", "warp cycle 8" },
            { "warp exp|exponential <power> [options]", "Append an exponential warp.", "", "warp exp 1.5 mix 0.7" },
            { "warp step|quantize <1..64 steps> [options]", "Append a stepped quantizer warp.", "", "warp step 4 mix 0.8" },
            { "warp eu|euclid <pulses> <1..64 steps> [options]", "Append a Euclidean quantizer warp.", "", "warp eu 5 8 mix 0.6" },
            { "  options: [mix|alpha <0..1>] [segment <begin> <end>] [repeat <1..16>]", "Options apply to exp, step, and Euclidean timing warps.", "", "warp exp 1.25 segment .25 .75 repeat 2" },
        } },
        { "KITS, TARGETS & SELECTION", {
            { "kit [gm|superior] <compact|basic|toms>", "Configure a named drum map, template, and aliases.", "kit", "kit superior compact" },
            { "aliases", "List current @aliases grouped in lane order.", "aliases", "aliases" },
            { "alias <name> <lane|@alias>", "Bind or reassign a case-insensitive alias.", "alias", "alias hats 3" },
            { "autoalias", "Replace the alias map with the shortest available prefix of each lane name.", "autoalias", "autoalias" },
            { "@name", "Show the lane currently bound to an alias; use the alias command to assign or reassign it.", "@", "@kick" },
            { "select [lane] <lane|@alias> [row]", "Move tracker selection; lane and row are one-based.", "select", "select @kick 5" },
            { "name <lane|@alias> <words...>", "Rename a lane.", "name", "name @kick DEEP KICK" },
            { "track add [name...]", "Append a lane, up to the 32-track realtime publication limit.", "track", "track add PERCUSSION" },
            { "track remove <target>", "Remove one lane; at least one remains.", "", "track remove 4" },
        } },
        { "GENERATION & VARIATION", {
            { "variation|vary <generator...> [launch <tick|beat|cycle>]", "Create a generated bank entry and optionally request a quantized launch.", "variation vary", "variation scene sparse 101 launch beat" },
            { "generate [density chaos symbols]", "Generate every native column using the session random stream.", "generate", "generate 0.5 0.5 0.1" },
            { "generateseed <seed> [density chaos symbols]", "Generate a repeatable whole pattern without consuming the session stream.", "generateseed", "generateseed orchard 1 0.5 0" },
            { "scene <sparse|balanced|dense|drift|weird> [seed]", "Generate a repeatable named whole-pattern scene.", "scene", "scene balanced 733" },
            { "mutate [amount] [all|notes|drums|values|fx|symbols|structure|meta]", "Vary the current pattern within one typed native scope.", "mutate", "mutate 0.25 notes" },
            { "drumscene <techno|broken|sparse|blast|ritual> [seed]", "Generate seeded rhythms for recognized kit lanes.", "drumscene", "drumscene techno 101" },
        } },
        { "PITCH, RHYTHM & NOTE CELLS", {
            { "pitch|defaultnote <target> <MIDI|note name>", "Set the lane's default pitch and replace every explicit NOTE pitch while preserving symbols.", "pitch defaultnote", "pitch @kick C-2" },
            { "hit <target> <row> [MIDI note]", "Write a note using the lane anchor or an explicit pitch.", "hit", "hit @kick 1 36" },
            { "rest <target> <row>", "Write a NOTE rest.", "rest", "rest @kick 2" },
            { "repeat <target> <row>", "Write a retrigger-previous NOTE cell.", "repeat", "repeat @kick 3" },
            { "hold <target> <row>", "Continue the active note without reattacking it.", "hold", "hold @kick 4" },
            { "kill <target> <row>", "Write a NOTE kill cell.", "kill", "kill @kick 4" },
            { "note <target> <row> <0..127|rest|rpt|hold|kill|B01..B32>", "Edit one NOTE cell directly, including a reusable Burst reference.", "note", "note @kick 5 B01" },
            { "burst new <B01..B32> [name]", "Create a four-substep reusable pattern-local Burst.", "burst", "burst new B02 BREAK RUSH" },
            { "burst Bxx notes <1..8 notes>", "Set the independently pitched MIDI events emitted inside one tracker row.", "", "burst B01 notes 48 52 50 55" },
            { "burst Bxx velocity|gate <one|per-step values>", "Set one value for the phrase or one value for every substep.", "", "burst B01 velocity 127 104 82 116" },
            { "burst Bxx timing <even|accelerate|decelerate|positions...>", "Shape sub-row positions; custom positions are percentages in ascending order.", "", "burst B01 timing accelerate" },
            { "burst Bxx name|reverse|rotate|usage ...", "Rename or reshape a Burst and inspect shared NOTE-cell usage.", "", "burst B01 rotate 1" },
            { "burst duplicate <source> <destination>  |  burst delete <Bxx>", "Copy a recipe or delete an unreferenced one.", "", "burst duplicate B01 B02" },
            { "burst <target> <row> notes <1..8 notes>", "Allocate an empty Burst slot and place it in one NOTE cell.", "", "burst @kick 16 notes 48 52 50 55" },
            { "mask <target> <x---...> [direction]", "Replace the active NOTE mask: x/X is a hit and - is a rest.", "mask", "mask @kick x---x--- <>" },
            { "eu|e|euclid <target> <pulses> <steps> [rotate] [direction]", "Generate a Euclidean NOTE mask; pulses above steps automatically use aligned 2–8-way RR cells in an available SEQ pair.", "eu e euclid", "eu @kick 20 16 1 <>" },
            { "rotate|rot <target> <signed steps>", "Rotate active NOTE cells right; negative values move left.", "rotate rot", "rotate @kick -1" },
            { "reverse <target>", "Reverse every active NOTE cell.", "reverse", "reverse @kick" },
            { "fill <target> <every> [offset]", "Add anchored hits to NOTE rests at a fixed interval.", "fill", "fill @kick 4 0" },
            { "sieve <target> [note] <modulus> <residue...>", "Build a repeating modular-residue NOTE rhythm.", "sieve", "sieve @kick note 5 0 2" },
            { "density <target> [note] <0..1>", "Stochastically replace the NOTE mask at the requested density.", "density", "density @kick note 0.65" },
            { "thin <target> [note] <0..1>", "Remove each existing NOTE hit with the requested probability.", "thin", "thin @kick note 0.25" },
            { "rotatehits <target> [note] <steps>", "Rotate active hits and clear non-hit cells.", "rotatehits", "rotatehits @kick note 2" },
            { "humanize <target> [note] <0..1>", "Move selected hits one step left or right when the destination is empty.", "humanize", "humanize @kick note 0.2" },
        } },
        { "VOLUME", {
            { "vel <target> <row> <0..127>", "Edit one VOL cell using MIDI velocity notation.", "vel", "vel @kick 1 110" },
            { "velseq|vol <target> <symbols|values...>", "Replace VOL with compact symbols, MIDI integers, or normalized decimals; see Compact Symbol Reference.", "velseq vol", "vol @kick 110 92 104 76" },
            { "randomize|random|rand <target> [vol] [minimum maximum]", "Materialize random values across the active VOL length.", "randomize random rand", "randomize @kick vol 40 110" },
        } },
        { "COLUMN MOTION & LANE STATE", {
            { "len|length <target> [column] <1..256>", "Set an independent column length; NOTE is the default.", "len length", "len @kick vol 12" },
            { "stride|speed|spd <target> [column] <positive integer>", "Set an independent column stride.", "stride speed spd", "stride @kick note 2" },
            { "phase|ph <target> [column] <signed rows>", "Set an independent per-column phase rotation.", "phase ph", "phase @kick note -1" },
            { "phase|ph reset [bank]", "Clear every NOTE, VOL, and sequencing-column phase in the current pattern; BANK applies it to every saved pattern in the Tracker plug-in.", "", "phase reset" },
            { "dir|mode <target> [column] <direction>", "Set forward, reverse, palindrome, or random traversal.", "dir mode", "dir @kick note <>" },
            { "mute <target> [column] [on|off|toggle]", "Mute or toggle a lane column; NOTE is the default.", "mute", "mute @kick vol toggle" },
            { "unmute <target|all>", "Unmute one NOTE lane or every NOTE lane.", "unmute", "unmute all" },
            { "solo <target> [target ...]", "Mute every NOTE lane except the listed targets.", "solo", "solo @kick @snare" },
        } },
        { "SEQUENCING COLUMNS", {
            { "actions", "List sequencing action codes and CC0..CC127 accepted by SEQ1 and SEQ2.", "actions", "actions" },
            { "fx <target> <pair> <row> <clear|previous>", "Clear or recall one FX action cell; pair accepts 1/fx1/f1 or 2/fx2/f2.", "fx", "fx @kick 1 1 previous" },
            { "fx <target> <pair> <row> <action|CC0..CC127> <value>", "Write a sequencing behavior or MIDI control change. CC values accept 0..127 integers or normalized decimals.", "", "fx @kick 1 5 cc74 96" },
            { "condition|cond <target> <row> <condition|clear>", "Write CD into the first available SEQ pair. Conditions: 1:2..2:2, 1:4..4:4, 1:8..8:8, FIRST, LAST, FILL, !FILL.", "condition cond", "cond @snare 5 2:4" },
            { "fxvalue|fxv <target> <pair> <row> <value|previous>", "Edit a paired value. CC lanes accept 0..127 integers or normalized decimals.", "fxvalue fxv", "fxvalue @kick 1 5 0.8" },
            { "fx1|f1|fx2|f2 <target> <action|CC0..CC127> <sequence>", "Replace a compact FX/value sequence; MIDI CC sequences also accept 0..127 integers.", "fx1 f1 fx2 f2", "fx1 @kick cc74 24 64 96 127" },
            { "interp|interpolation <target> <v1|v2> <step|linear>", "Choose stepped values or bounded between-row MIDI CC interpolation for one value column.", "interp interpolation", "interp @kick v1 linear" },
            { "prob|probability <target> <row> <amount|clear>", "Write PR into the first available FX pair; percentages are accepted.", "prob probability", "prob @kick 5 25%" },
            { "ratchet|retrig|retrigger <target> <row> <amount|clear>", "Write or clear a ratchet action in the first available FX pair.", "ratchet retrig retrigger", "ratchet @kick 6 0.5" },
            { "microtime|micro|delay|flam|stutter <target> <row> <amount|clear>", "Write or clear a timing action.", "microtime micro delay flam stutter", "microtime @kick 7 0.25" },
            { "skip|offset|repeatprev|accent|ghost|euclidfx <target> <row> <amount|clear>", "Write or clear a note-sequencing action.", "skip offset repeatprev accent ghost euclidfx", "skip @kick 8 0.5" },
        } },
        { "ALIAS-FIRST PERFORMANCE SHORTHAND", {
            { "@alias <x---...> [direction]", "Alias-first mask entry.", "", "@kick x---x--- <>" },
            { "@alias vel|v <sequence>", "Alias-first velocity sequence entry.", "", "@kick v 127 96 80 64" },
            { "@alias <operation> ...", "Move the alias after any lane-targeted operation, for example @h eu 7 16.", "", "@kick eu 7 16" },
            { "@alias <direction>", "Set NOTE direction with forward/>, reverse/<, palindrome/<>, or the word random.", "", "@kick >" },
        } },
        { "COMPACT SYMBOL REFERENCE", {
            { "VALUE LEVELS  !  +  *  .", "Only value sequences use these marks: ! = 1.00, + = 0.85, * = 0.70, and . = 0.55 in both VOL and FX.", "", "vol @kick !+*." },
            { "EMPTY / REST / DEFAULT  -", "A standalone - always means no authored event: NOTE rest, VOL default, or empty FX action. Attached to a number, it remains a minus sign.", "", "vol @kick !-+" },
            { "PREVIOUS / HOLD  =", "A standalone = always recalls or holds the previously resolved VOL or FX state; it is not alias assignment.", "", "vol @kick !=+" },
            { "NOTE MASK  x/X  -", "NOTE masks use only x/X for a hit and - for a rest; value marks and digits are rejected.", "", "mask @kick x---x---" },
            { "DIRECTION  >  <  <>  random", "Use > for forward, < for reverse, <> for palindrome, and the word random; ? is reserved for Help.", "", "dir @kick note random" },
            { "GRID NOTE  ---  RPT  HLD  KIL", "Direct NOTE-cell text for rest, retrigger previous, continue the active note, and kill it.", "", "note @kick 1 hold" },
            { "GRID VALUE  DEF  PRV", "Direct VOL/SEQ value text for the default velocity or the previously resolved value; SEQ action PRV recalls its previous action.", "", "vol @kick default previous .5" },
        } },
    };
    return sections;
}

CommandResult CommandEngine::execute(TrackerSession& session,
    std::string_view command)
{
    // Every command runs against a candidate copy. Multi-step shorthand such
    // as kits, solo, masks with direction, and velocity sequences therefore
    // either commits in full or leaves the live session untouched.
    TrackerSession candidate = session;
    auto result = executeTokens(candidate, tokenize(command));
    if (result.ok) {
        if (result.hasEffect(CommandEffect::PatternChanged))
            normalizeColumnPhases(candidate.pattern);
        session = std::move(candidate);
    }
    return result;
}

} // namespace s3g::tracker
