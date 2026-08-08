#include "s3g/tracker/command.h"
#include "s3g/tracker/fx_catalog.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cstdlib>
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
    return { true, effects, std::move(message) };
}

CommandResult failure(std::string message)
{
    return { false, CommandEffect::None, std::move(message) };
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

uint8_t defaultNoteForLane(std::size_t lane)
{
    constexpr std::array<uint8_t, 8u> drumNotes {
        36u, 38u, 42u, 46u, 39u, 45u, 51u, 49u,
    };
    if (lane < drumNotes.size()) return drumNotes[lane];
    return static_cast<uint8_t>(std::min<std::size_t>(127u, 52u + lane));
}

void ensureLaneDefaultNotes(TrackerSession& session)
{
    const auto previousSize = session.laneDefaultNotes.size();
    session.laneDefaultNotes.resize(session.pattern.tracks.size());
    for (std::size_t lane = previousSize;
         lane < session.laneDefaultNotes.size(); ++lane)
        session.laneDefaultNotes[lane] = defaultNoteForLane(lane);
}

uint8_t anchorNote(const TrackerSession& session, std::size_t lane)
{
    const auto& track = session.pattern.tracks[lane];
    const auto found = std::find_if(track.notes.begin(), track.notes.end(),
        [](const NoteCell& cell) {
            return cell.state == NoteCellState::Note;
        });
    if (found != track.notes.end()) return found->note;
    if (lane < session.laneDefaultNotes.size())
        return session.laneDefaultNotes[lane];
    return defaultNoteForLane(lane);
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
        return "No aliases. Use kit superior compact or alias <name> <lane>.";
    std::ostringstream stream;
    stream << "Aliases:";
    for (const auto& [name, lane] : session.aliases)
        stream << " @" << name << '=' << (lane + 1u);
    return stream.str();
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
                || repetitions > TimingWarpStack::kMaximumRepetitions) {
                error = "Warp repeat must be between 1 and 1024.";
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
    stream << "Warp cycle " << session.transport.warpCycleTicks
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
    if (symbol == '*' || symbol == 'o' || symbol == 'O') return 0.70f;
    if (symbol == '.') return 0.55f;
    return static_cast<float>(symbol - '0') * 0.1f;
}

bool isCompactFxPattern(std::string_view token) noexcept
{
    return !token.empty() && std::all_of(token.begin(), token.end(),
        [](char symbol) {
            return symbol == '!' || symbol == '+' || symbol == '*'
                || symbol == 'o' || symbol == 'O' || symbol == '.'
                || symbol == '=' || symbol == '-'
                || (symbol >= '0' && symbol <= '9');
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
    double numeric = 0.0;
    if (!parseFiniteDouble(atom, numeric) || numeric < 0.0
        || numeric > 1.0) {
        error = "FX sequence values must be normalized 0..1, compact symbols, =, or -.";
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
    stream << ". Enter a code in SEQ1/SEQ2 or right-click a SEQ cell.";
    return stream.str();
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
    if (value == "forward" || value == "fwd" || value == "f"
        || value == ">") {
        direction = Direction::Forward;
        canonical = "forward";
        return true;
    }
    if (value == "reverse" || value == "backward" || value == "back"
        || value == "rev" || value == "b" || value == "<") {
        direction = Direction::Reverse;
        canonical = "reverse";
        return true;
    }
    if (value == "random" || value == "rand" || value == "rnd"
        || value == "r" || value == "?") {
        direction = Direction::Random;
        canonical = "random";
        return true;
    }
    if (value == "palindrome" || value == "pal" || value == "pingpong"
        || value == "ping-pong" || value == "p" || value == "<>") {
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
               return value == 'x' || value == 'X' || value == '1'
                   || value == '-' || value == '.' || value == '_'
                   || value == '0';
           });
}

bool maskSymbolIsHit(char value)
{
    return value == 'x' || value == 'X' || value == '1';
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
    if (token.size() <= 1u || token == "==" || token == "--"
        || token == "??")
        return false;
    double numeric = 0.0;
    if (parseFiniteDouble(token, numeric)) return false;
    bool hasSymbol = false;
    for (const auto value : token) {
        const bool valid = value == '!' || value == '+' || value == '*'
            || value == 'o' || value == 'O' || value == '.'
            || value == '-' || value == '_' || value == '='
            || value == '?' || (value >= '0' && value <= '9');
        if (!valid) return false;
        if (!(value >= '0' && value <= '9')) hasSymbol = true;
    }
    return hasSymbol;
}

bool compactVelocityCell(char token, ValueCell& cell, std::string& error)
{
    if (token == '?') {
        error = "Random velocity (?) is not supported by the native value model yet.";
        return false;
    }
    if (token == '-' || token == '_' || token == '=') {
        cell = ValueCell::previous();
        return true;
    }
    if (token == '!') cell = ValueCell::withValue(1.0f);
    else if (token == '+') cell = ValueCell::withValue(0.85f);
    else if (token == '*' || token == 'o' || token == 'O')
        cell = ValueCell::withValue(0.70f);
    else if (token == '.') cell = ValueCell::withValue(0.55f);
    else if (token >= '0' && token <= '9')
        cell = ValueCell::withValue(static_cast<float>(token - '0') / 10.0f);
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
    if (value == "-" || value == "_" || value == "=" || value == "=="
        || value == "hold" || value == "previous" || value == "prev") {
        cell = ValueCell::previous();
        return true;
    }
    if (value == "default") {
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
    if (value == "*" || value == "o") {
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
        || cell.state == NoteCellState::RetriggerPrevious;
}

double nextCommandRandom(TrackerSession& session) noexcept
{
    auto value = session.commandRngState;
    if (value == 0u) value = 0x9e3779b97f4a7c15ull;
    value ^= value >> 12u;
    value ^= value << 25u;
    value ^= value >> 27u;
    session.commandRngState = value;
    const uint64_t bits = value * 2685821657736338717ull;
    return static_cast<double>(bits >> 11u)
        * (1.0 / 9007199254740992.0);
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
        if (tokens.size() >= 2u && tokens[1] == "=") {
            if (tokens.size() != 3u)
                return failure("Usage: @name = <lane|@alias>");
            std::string alias;
            if (!normalizeAliasName(tokens[0], alias))
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
        if (tokens.size() == 1u) {
            std::size_t lane = 0u;
            std::string error;
            if (!parseLane(session, tokens[0], lane, error))
                return failure(std::move(error));
            return success(tokens[0] + " = " + std::to_string(lane + 1u));
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
        return success(timingWarpsText(session));
    }
    if (verb == "warp") {
        if (tokens.size() < 2u) {
            return failure("Usage: warp <clear|cycle|exp|step|eu> ...");
        }
        const auto operation = asciiLower(tokens[1]);
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
                || steps > TimingWarpStack::kMaximumSteps) {
                return failure("Warp step count must be between 1 and 65536.");
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
                || steps > TimingWarpStack::kMaximumSteps) {
                return failure("Warp Euclid requires 1..steps pulses and 1..65536 steps.");
            }
            transform = TimingWarpTransform::euclideanQuantize(
                pulses, steps);
            optionsBegin = 4u;
        } else {
            return failure("Warp type must be exp, step, or eu; use warp clear to reset.");
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

    if (verb == "hit" || verb == "rest" || verb == "repeat"
        || verb == "kill") {
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
            uint32_t note = anchorNote(session, lane);
            if (tokens.size() == 4u
                && (!parseUnsigned(tokens[3], note) || note > 127u))
                return failure("MIDI note must be an integer from 0 to 127.");
            cell = NoteCell::withNote(static_cast<uint8_t>(note));
        } else if (verb == "repeat") {
            cell = NoteCell::retriggerPrevious();
        } else if (verb == "kill") {
            cell = NoteCell::kill();
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
        const auto* sequencerAction = findSequencerAction(tokens[2]);
        if (!sequencerAction)
            return failure("Unknown sequencing action; use actions to list codes.");
        const auto selectedAction = FxActionCell::sequencer(
            sequencerAction->action);
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
                return failure("A sequencing action requires a normalized value.");
            const auto* timingAction = findSequencerAction(tokens[4]);
            if (!timingAction)
                return failure("Unknown sequencing action; use actions to list codes.");
            double value = 0.0;
            if (!parseFiniteDouble(tokens[5], value)
                || value < 0.0 || value > 1.0)
                return failure("FX values must be normalized between 0 and 1.");
            ensureFxStorage(session, target, false, row + 1u);
            target.actions[row] = FxActionCell::sequencer(
                timingAction->action);
            target.values[row] = FxValueCell::withValue(
                static_cast<float>(value));
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
            double value = 0.0;
            if (!parseFiniteDouble(tokens[4], value)
                || value < 0.0 || value > 1.0)
                return failure("FX values must be normalized between 0 and 1.");
            target.values[row] = FxValueCell::withValue(
                static_cast<float>(value));
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
        if (tokens.size() != 3u && tokens.size() != 4u)
            return failure("Usage: " + verb
                + " <lane|@alias> [note|ins|vel|fx1|v1|fx2|v2] <value>");
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
            uint32_t midiNote = 0u;
            if (value == "rest") cell = NoteCell::rest();
            else if (value == "rpt") cell = NoteCell::retriggerPrevious();
            else if (value == "kill") cell = NoteCell::kill();
            else if (parseUnsigned(tokens[3], midiNote) && midiNote <= 127u)
                cell = NoteCell::withNote(static_cast<uint8_t>(midiNote));
            else
                return failure("Note must be MIDI 0..127, rest, rpt, or kill.");
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
            return failure("Mask symbols are x/X/1 for hits and -/./_/0 for rests.");
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
            || steps > kMaximumRows || pulses > steps)
            return failure("Euclid requires 0..steps pulses and 1..256 steps.");
        int64_t rotation = 0;
        if (tokens.size() >= 5u && !parseSigned(tokens[4], rotation))
            return failure("Euclid rotation must be a whole number.");
        Direction direction = session.pattern.tracks[lane].noteColumn.direction;
        std::string canonical;
        if (tokens.size() == 6u
            && !parseDirection(tokens[5], direction, canonical))
            return failure("Unknown Euclid direction; use forward, reverse, random, or palindrome.");

        std::string mask(steps, '-');
        const auto normalized = normalizedRotation(rotation, steps);
        for (std::size_t row = 0u; row < steps; ++row) {
            const bool hit = ((row + 1u) * pulses) / steps
                != (row * pulses) / steps;
            if (hit) mask[(row + normalized) % steps] = 'x';
        }
        applyMask(session, lane, mask);
        if (tokens.size() == 6u)
            session.pattern.tracks[lane].noteColumn.direction = direction;
        return success("Applied Euclid " + std::to_string(pulses) + '/'
                + std::to_string(steps) + " rotate "
                + std::to_string(rotation) + " to "
                + laneLabel(session, lane) + '.',
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

std::string CommandEngine::helpText()
{
    std::ostringstream stream;
    stream << "s3g Tracker console commands\n";
    for (const auto& section : helpSections()) {
        stream << '\n' << section.title << '\n';
        for (const auto& entry : section.entries) {
            stream << "  " << entry.syntax << "\n    "
                   << entry.description << '\n';
        }
    }
    stream
        << "\nTargets are one-based lane numbers or @aliases; rows are one-based.\n"
        << "Columns: note, vol, fx1, v1, fx2, v2. "
        << "Directions: forward (>), reverse (<), palindrome (<>), random (?).\n"
        << "Randomize materializes repeatable values into the targeted VOL column.";
    return stream.str();
}

const std::vector<CommandHelpSection>& CommandEngine::helpSections()
{
    static const std::vector<CommandHelpSection> sections {
        { "ESSENTIALS", {
            { "help  |  ?", "Show this complete command reference.", "help ?" },
            { "demo", "Load the General MIDI tracker demonstration.", "demo" },
            { "play", "Start timestamped playback.", "play" },
            { "stop", "Stop playback and clean up active notes.", "stop" },
            { "panic", "Send MIDI All Notes Off and reset active voices.", "panic" },
        } },
        { "TRANSPORT & TIMING", {
            { "swing <0.50..0.75 | 50..75>", "Set traditional pair swing.", "swing" },
            { "gate <1..5000 ms>", "Set external MIDI note-gate duration.", "gate" },
            { "loop <on|off|toggle>  |  loop [rows] <start> <end>", "Toggle the global loop or set its inclusive one-based row region.", "loop" },
            { "warps", "List the compiled functional timing-warp stack.", "warps" },
            { "warp clear", "Remove every timing transform.", "warp" },
            { "warp cycle <1..16 ticks>", "Set the repeating live-warp cycle.", "" },
            { "warp exp|exponential <power> [options]", "Append an exponential warp.", "" },
            { "warp step|quantize <steps> [options]", "Append a stepped quantizer warp.", "" },
            { "warp eu|euclid <pulses> <steps> [options]", "Append a Euclidean quantizer warp.", "" },
            { "  options: [mix|alpha <0..1>] [segment <begin> <end>] [repeat <count>]", "Options apply to exp, step, and Euclidean timing warps.", "" },
        } },
        { "KITS, TARGETS & SELECTION", {
            { "kit [gm|superior] <compact|basic|toms>", "Configure a named drum map, template, and aliases.", "kit" },
            { "aliases", "List every current @alias binding.", "aliases" },
            { "alias <name> <lane|@alias>", "Bind a case-insensitive alias.", "alias" },
            { "@name = <lane|@alias>", "Compact alias assignment.", "@" },
            { "@name", "Show the lane currently bound to an alias.", "" },
            { "select [lane] <lane|@alias> [row]", "Move tracker selection; lane and row are one-based.", "select" },
            { "name <lane|@alias> <words...>", "Rename a lane.", "name" },
            { "track add [name...]", "Append a lane, up to the 32-track realtime publication limit.", "track" },
            { "track remove <target>", "Remove one lane; at least one remains.", "" },
        } },
        { "RHYTHM & NOTE CELLS", {
            { "hit <target> <row> [MIDI note]", "Write a note using the lane anchor or an explicit pitch.", "hit" },
            { "rest <target> <row>", "Write a NOTE rest.", "rest" },
            { "repeat <target> <row>", "Write a retrigger-previous NOTE cell.", "repeat" },
            { "kill <target> <row>", "Write a NOTE kill cell.", "kill" },
            { "note <target> <row> <0..127|rest|rpt|kill>", "Edit one NOTE cell directly.", "note" },
            { "mask <target> <x---...> [direction]", "Replace the active NOTE mask and optional direction.", "mask" },
            { "eu|e|euclid <target> <pulses> <steps> [rotate] [direction]", "Generate a Euclidean NOTE mask.", "eu e euclid" },
            { "rotate|rot <target> <signed steps>", "Rotate active NOTE cells right; negative values move left.", "rotate rot" },
            { "reverse <target>", "Reverse every active NOTE cell.", "reverse" },
            { "fill <target> <every> [offset]", "Add anchored hits to NOTE rests at a fixed interval.", "fill" },
            { "sieve <target> [note] <modulus> <residue...>", "Build a repeating modular-residue NOTE rhythm.", "sieve" },
            { "density <target> [note] <0..1>", "Stochastically replace the NOTE mask at the requested density.", "density" },
            { "thin <target> [note] <0..1>", "Remove each existing NOTE hit with the requested probability.", "thin" },
            { "rotatehits <target> [note] <steps>", "Rotate active hits and clear non-hit cells.", "rotatehits" },
            { "humanize <target> [note] <0..1>", "Move selected hits one step left or right when the destination is empty.", "humanize" },
        } },
        { "VOLUME", {
            { "vel <target> <row> <0..127>", "Edit one VOL cell using MIDI velocity notation.", "vel" },
            { "velseq|vol <target> <symbols|values...>", "Replace VOL with symbolic, MIDI, or normalized values.", "velseq vol" },
            { "randomize|random|rand <target> [vol] [minimum maximum]", "Materialize random values across the active VOL length.", "randomize random rand" },
        } },
        { "COLUMN MOTION & LANE STATE", {
            { "len|length <target> [column] <1..256>", "Set an independent column length; NOTE is the default.", "len length" },
            { "stride|speed|spd <target> [column] <positive integer>", "Set an independent column stride.", "stride speed spd" },
            { "phase|ph <target> [column] <signed rows>", "Set an independent per-column phase rotation.", "phase ph" },
            { "dir|mode <target> [column] <direction>", "Set forward, reverse, palindrome, or random traversal.", "dir mode" },
            { "mute <target> [column] [on|off|toggle]", "Mute or toggle a lane column; NOTE is the default.", "mute" },
            { "unmute <target|all>", "Unmute one NOTE lane or every NOTE lane.", "unmute" },
            { "solo <target> [target ...]", "Mute every NOTE lane except the listed targets.", "solo" },
        } },
        { "SEQUENCING COLUMNS", {
            { "actions", "List sequencing action codes accepted by SEQ1 and SEQ2.", "actions" },
            { "fx <target> <pair> <row> <clear|previous>", "Clear or recall one FX action cell; pair accepts 1/fx1/f1 or 2/fx2/f2.", "fx" },
            { "fx <target> <pair> <row> <sequencing-action> <0..1>", "Write a sequencing behavior and its normalized amount.", "" },
            { "fxvalue|fxv <target> <pair> <row> <0..1|previous>", "Edit the value paired with an FX action.", "fxvalue fxv" },
            { "fx1|f1|fx2|f2 <target> <action> <sequence>", "Replace a compact FX/value sequence; ! + * o . 0..9 write values, = recalls, and - rests.", "fx1 f1 fx2 f2" },
            { "prob|probability <target> <row> <amount|clear>", "Write PR into the first available FX pair; percentages are accepted.", "prob probability" },
            { "ratchet|retrig|retrigger <target> <row> <amount|clear>", "Write or clear a ratchet action in the first available FX pair.", "ratchet retrig retrigger" },
            { "microtime|micro|delay|flam|stutter <target> <row> <amount|clear>", "Write or clear a timing action.", "microtime micro delay flam stutter" },
            { "skip|offset|repeatprev|accent|ghost|euclidfx <target> <row> <amount|clear>", "Write or clear a note-sequencing action.", "skip offset repeatprev accent ghost euclidfx" },
        } },
        { "ALIAS-FIRST PERFORMANCE SHORTHAND", {
            { "@alias <x---...> [direction]", "Alias-first mask entry.", "" },
            { "@alias vel|v <sequence>", "Alias-first velocity sequence entry.", "" },
            { "@alias <operation> ...", "Move the alias after any lane-targeted operation, for example @h eu 7 16.", "" },
            { "@alias <direction>", "Set NOTE direction with a word or >, <, <>, or ?.", "" },
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
