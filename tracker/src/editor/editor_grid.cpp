#include "s3g/tracker/editor_grid.h"
#include "s3g/tracker/fx_catalog.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>

namespace s3g::tracker::editor {
namespace {
    Rect makeRect(double x, double y, double w, double h) { return { x, y, w, h }; }
    std::vector<std::string> split(std::string_view text, char separator)
    {
        std::vector<std::string> result;
        for (;;) {
            auto end = text.find(separator);
            result.push_back(trimCellText(text.substr(0, end)));
            if (end == std::string_view::npos)
                break;
            text.remove_prefix(end + 1u);
        }
        return result;
    }
    std::string lower(std::string text)
    {
        for (auto& c : text)
            if (c >= 'A' && c <= 'Z')
                c += 'a' - 'A';
        return text;
    }
    std::string decimal(double value, unsigned precision, bool fixed = true)
    {
        std::ostringstream out;
        out.imbue(std::locale::classic());
        if (fixed)
            out << std::fixed;
        out << std::setprecision(static_cast<int>(precision)) << value;
        return out.str();
    }
    template <class Cell> std::string valuesText(const Cell& cell, bool compact)
    {
        std::string text = decimal(std::clamp(cell.normalized, 0.f, 1.f), 3);
        if (compact && cell.valueVoiceCount() > 1u)
            return text + "+" + std::to_string(cell.valueVoiceCount() - 1u);
        for (std::size_t voice = 1; voice < cell.valueVoiceCount(); ++voice)
            text += "+" + decimal(std::clamp(cell.valueVoice(voice), 0.f, 1.f), 3);
        return text;
    }
    bool parseValues(
        std::string_view text, std::array<float, kMaximumNoteVoices>& voices, std::size_t& count)
    {
        auto parts = split(text, '+');
        count = parts.size();
        if (!count || count > voices.size())
            return false;
        for (std::size_t i = 0; i < count; ++i)
            if (!app::parseGridNormalizedValue(parts[i], voices[i]))
                return false;
        return true;
    }
}

std::string trimCellText(std::string_view text)
{
    // Cocoa's whitespaceAndNewlineCharacterSet includes Unicode spacing.
    // Compare complete UTF-8 sequences; never trim arbitrary continuation bytes.
    constexpr std::string_view spaces[] = { " ", "\t", "\r", "\n", "\f", "\v", "\u0085", "\u00a0",
        "\u1680", "\u2000", "\u2001", "\u2002", "\u2003", "\u2004", "\u2005", "\u2006", "\u2007",
        "\u2008", "\u2009", "\u200a", "\u2028", "\u2029", "\u202f", "\u205f", "\u3000" };
    bool changed = true;
    while (changed && !text.empty()) {
        changed = false;
        for (auto space : spaces)
            if (text.size() >= space.size() && text.substr(0, space.size()) == space) {
                text.remove_prefix(space.size());
                changed = true;
                break;
            }
    }
    changed = true;
    while (changed && !text.empty()) {
        changed = false;
        for (auto space : spaces)
            if (text.size() >= space.size() && text.substr(text.size() - space.size()) == space) {
                text.remove_suffix(space.size());
                changed = true;
                break;
            }
    }
    return std::string(text);
}

bool parseCellInteger(std::string_view text, int64_t& value)
{
    std::istringstream input(trimCellText(text));
    input.imbue(std::locale::classic());
    int64_t parsed = 0;
    if (!(input >> parsed) || !input.eof())
        return false;
    value = parsed;
    return true;
}

bool parseCellDouble(std::string_view text, double& value)
{
    std::istringstream input(trimCellText(text));
    input.imbue(std::locale::classic());
    double parsed = 0;
    if (!(input >> parsed) || !input.eof() || !std::isfinite(parsed))
        return false;
    value = parsed;
    return true;
}

bool parseColumnLengthAndStride(std::string_view source, std::size_t& length, uint32_t& stride)
{
    auto text = lower(trimCellText(source));
    auto times = text.find("×");
    if (times != std::string::npos)
        text.replace(times, 2, "x");
    std::replace(text.begin(), text.end(), '*', 'x');
    text.erase(std::remove(text.begin(), text.end(), ' '), text.end());
    auto parts = split(text, 'x');
    int64_t l = 0, s = 1;
    if (parts.empty() || parts.size() > 2 || !parseCellInteger(parts[0], l)
        || (parts.size() == 2 && !parseCellInteger(parts[1], s)) || l < 1 || l > 256 || s < 1
        || static_cast<uint64_t>(s) > std::numeric_limits<uint32_t>::max())
        return false;
    length = static_cast<std::size_t>(l);
    stride = static_cast<uint32_t>(s);
    return true;
}

std::string directionMark(Direction direction)
{
    switch (direction) {
    case Direction::Reverse:
        return "<";
    case Direction::Random:
        return "RND";
    case Direction::Palindrome:
        return "<>";
    default:
        return ">";
    }
}

std::string midiNoteName(uint8_t note)
{
    constexpr const char* names[] { "C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-",
        "A#", "B-" };
    return std::string(names[note % 12u]) + std::to_string(int(note) / 12 - 1);
}

std::string noteText(const NoteCell& cell, bool midi, bool compact)
{
    switch (cell.state) {
    case NoteCellState::Note: {
        auto voiceText
            = [midi](uint8_t note) { return midi ? std::to_string(note) : midiNoteName(note); };
        auto text = voiceText(cell.note);
        if (compact && cell.noteVoiceCount() > 1)
            return text + "+" + std::to_string(cell.noteVoiceCount() - 1);
        for (std::size_t voice = 1; voice < cell.noteVoiceCount(); ++voice)
            text += "+" + voiceText(cell.noteVoice(voice));
        return text;
    }
    case NoteCellState::Burst:
        return assetBankToken(cell.burstBankId) + ":" + burstSlotToken(cell.note);
    case NoteCellState::RetriggerPrevious:
        return "RPT";
    case NoteCellState::Kill:
        return "KIL";
    case NoteCellState::Hold:
        return "HLD";
    default:
        return "---";
    }
}

std::string volumeText(const Track& track, std::size_t row, bool compact)
{
    if (row >= track.velocities.size())
        return "DEF";
    const auto& cell = track.velocities[row];
    if (cell.state == ValueCellState::Previous)
        return "PRV";
    if (cell.state == ValueCellState::Default)
        return "DEF";
    return valuesText(cell, compact);
}

std::string fxActionText(const Track& track, std::size_t pair, std::size_t row)
{
    if (pair >= track.fxPairs.size() || row >= track.fxPairs[pair].actions.size())
        return "---";
    const auto& cell = track.fxPairs[pair].actions[row];
    if (cell.state == FxActionCellState::Previous)
        return "PRV";
    if (cell.state == FxActionCellState::Sequencer) {
        auto* action = findSequencerAction(cell.sequencerAction);
        return action ? std::string(action->mnemonic) : "???";
    }
    if (cell.state == FxActionCellState::MidiControlChange)
        return "CC" + std::to_string(cell.midiController);
    return "---";
}

std::string fxValueText(const Track& track, std::size_t pair, std::size_t row, bool compact)
{
    if (pair >= track.fxPairs.size() || row >= track.fxPairs[pair].values.size())
        return "PRV";
    const auto& cell = track.fxPairs[pair].values[row];
    if (cell.state == FxValueCellState::Previous)
        return "PRV";
    const auto& actions = track.fxPairs[pair].actions;
    if (row < actions.size() && actions[row].state == FxActionCellState::Sequencer
        && actions[row].sequencerAction == SequencerAction::Condition) {
        auto* condition = sequencerCondition(
            static_cast<std::size_t>(sequencerConditionFromNormalized(cell.normalized)));
        if (condition)
            return std::string(condition->token);
    }
    return valuesText(cell, compact);
}

std::string gateText(const Track& track, std::size_t row, bool compact)
{
    if (row >= track.gates.size() || !track.gates[row].voiceCount)
        return "DEF";
    std::string text;
    for (std::size_t i = 0; i < track.gates[row].gateVoiceCount(); ++i) {
        if (i)
            text += "+";
        auto voice = track.gates[row].gateVoice(i);
        text += voice.mode == GateVoiceMode::Default
            ? "DEF"
            : voice.mode == GateVoiceMode::Tie ? "TIE"
                                               : decimal(voice.rows, compact ? 2 : 3, false);
    }
    return text;
}

std::string cellText(
    const Track& track, std::size_t row, std::size_t field, bool midi, bool compact)
{
    if (!field)
        return noteText(
            row < track.notes.size() ? track.notes[row] : NoteCell::rest(), midi, compact);
    if (field == 1)
        return volumeText(track, row, compact);
    if (gridFieldIsGate(field))
        return gateText(track, row, compact);
    return gridFieldIsSequenceAction(field)
        ? fxActionText(track, gridSequencePair(field), row)
        : fxValueText(track, gridSequencePair(field), row, compact);
}

bool applyCellText(const TrackerViewState& state, std::string_view source, Track& track,
    std::size_t row, std::size_t field)
{
    if (field >= gridFieldCount(state.sequenceColumnsExpanded) || row >= 256)
        return false;
    const auto text = lower(trimCellText(source));
    GridCell cell = blankTrackerGridCell(field);
    if (!field) {
        if (text.empty() || text == "---" || text == "rest")
            cell = NoteCell::rest();
        else if (text == "rpt" || text == "repeat")
            cell = NoteCell::retriggerPrevious();
        else if (text == "kil" || text == "kill")
            cell = NoteCell::kill();
        else if (text == "hld" || text == "hold" || text == "~")
            cell = NoteCell::hold();
        else {
            std::size_t slot = 0;
            AssetBankId bankId = state.activeBurstBankId;
            if (parseQualifiedBurstToken(text, bankId, slot) || parseBurstSlot(text, slot)) {
                auto* bank = findBurstBank(state.burstBanks, bankId);
                auto* library = bankId == state.activeBurstBankId ? &state.session.burstLibrary
                                                                  : bank ? &bank->library : nullptr;
                if (!library || slot >= library->bursts.size() || library->bursts[slot].empty())
                    return false;
                cell = NoteCell::withBurst(static_cast<uint8_t>(slot), bankId);
            } else {
                auto parts = split(text, '+');
                if (parts.empty() || parts.size() > kMaximumNoteVoices)
                    return false;
                std::array<uint8_t, kMaximumNoteVoices> notes {};
                for (std::size_t i = 0; i < parts.size(); ++i)
                    if (!parseMidiNote(parts[i], notes[i]))
                        return false;
                std::sort(notes.begin(), notes.begin() + parts.size());
                auto count
                    = std::unique(notes.begin(), notes.begin() + parts.size()) - notes.begin();
                cell = NoteCell::withNotes(notes, static_cast<std::size_t>(count));
            }
        }
    } else if (field == 1) {
        if (text.empty() || text == "def" || text == "default")
            cell = ValueCell::defaultValue();
        else if (text == "prv" || text == "previous")
            cell = ValueCell::previous();
        else {
            std::array<float, kMaximumNoteVoices> voices {};
            std::size_t count = 0;
            if (!parseValues(text, voices, count))
                return false;
            cell = ValueCell::withValues(voices, count);
        }
    } else if (gridFieldIsGate(field)) {
        if (text.empty() || text == "def" || text == "default")
            cell = GateCell::defaultValue();
        else {
            auto parts = split(text, '+');
            if (parts.empty() || parts.size() > kMaximumNoteVoices)
                return false;
            std::array<GateVoice, kMaximumNoteVoices> voices {};
            for (std::size_t i = 0; i < parts.size(); ++i) {
                if (parts[i] == "def" || parts[i] == "default")
                    voices[i] = { GateVoiceMode::Default, 1.f };
                else if (parts[i] == "tie" || parts[i] == "t")
                    voices[i] = { GateVoiceMode::Tie, 1.f };
                else {
                    double rows = 0;
                    if (!parseCellDouble(parts[i], rows) || rows < .01 || rows > 64.)
                        return false;
                    voices[i] = { GateVoiceMode::Rows, static_cast<float>(rows) };
                }
            }
            cell = GateCell::withVoices(voices, parts.size());
        }
    } else if (gridFieldIsSequenceAction(field)) {
        if (text.empty() || text == "---" || text == "clear")
            cell = FxActionCell::empty();
        else if (text == "prv" || text == "previous")
            cell = FxActionCell::previous();
        else if (auto* action = findSequencerAction(text))
            cell = FxActionCell::sequencer(action->action);
        else {
            uint8_t controller = 0;
            if (!parseMidiControlChange(text, controller))
                return false;
            cell = FxActionCell::midiControlChange(controller);
        }
    } else {
        auto pairIndex = gridSequencePair(field);
        const auto& pair = track.fxPairs[pairIndex];
        if (text.empty() || text == "prv" || text == "previous")
            cell = FxValueCell::previous();
        else if (text.find('+') != std::string::npos) {
            SequencerAction action = SequencerAction::Count;
            std::array<float, kMaximumNoteVoices> voices {};
            std::size_t count = 0;
            if (!resolvedSequencerAction(track, pairIndex, row, action)
                || action != SequencerAction::MicroTime || !parseValues(text, voices, count))
                return false;
            cell = FxValueCell::withValues(voices, count);
        } else {
            float value = 0;
            bool parsed = false;
            if (row < pair.actions.size() && pair.actions[row].state == FxActionCellState::Sequencer
                && pair.actions[row].sequencerAction == SequencerAction::Condition) {
                if (auto* condition = findSequencerCondition(text)) {
                    value = normalizedFromSequencerCondition(condition->condition);
                    parsed = true;
                }
            } else if (pairContainsMidiControlChange(pair))
                parsed = app::parseGridMidiOrNormalizedValue(text, value);
            else
                parsed = app::parseGridNormalizedValue(text, value);
            if (!parsed)
                return false;
            cell = FxValueCell::withValue(value);
        }
    }
    writeTrackerGridCell(track, field, row, cell);
    return true;
}

const Pattern* playbackFollowPattern(const TrackerViewState* state)
{
    if (!state)
        return nullptr;
    if (state->songPlaybackActive && !state->songPlaybackPatternId.empty()) {
        if (const auto* pattern = state->patternBank.findPattern(state->songPlaybackPatternId))
            return pattern;
    }
    return &state->session.pattern;
}

std::string playbackFollowPatternId(const TrackerViewState* state)
{
    if (!state)
        return {};
    if (state->songPlaybackActive && state->patternBank.findPattern(state->songPlaybackPatternId))
        return state->songPlaybackPatternId;
    return state->patternBank.activePatternId;
}

std::size_t visibleRows(const TrackerViewState* state)
{
    if (!state)
        return 16u;
    return std::clamp<std::size_t>(
        std::max(state->session.pattern.visibleRows, state->session.selectedRow + 1u), 16u, 256u);
}

std::size_t playbackFollowVisibleRows(const TrackerViewState* state)
{
    const auto* pattern = playbackFollowPattern(state);
    if (!pattern)
        return 16u;
    if (!state->songPlaybackActive)
        return visibleRows(state);
    return std::clamp<std::size_t>(pattern->visibleRows, 16u, 256u);
}

std::size_t gridFieldCount(bool sequenceColumnsExpanded) noexcept
{
    return sequenceColumnsExpanded ? 7u : 2u;
}

double gridFieldStartFraction(bool sequenceColumnsExpanded, std::size_t field) noexcept
{
    if (!sequenceColumnsExpanded) {
        constexpr double noteShare = s3g::tracker::app::kTrackerExpandedNoteFraction
            / (s3g::tracker::app::kTrackerExpandedNoteFraction
                + s3g::tracker::app::kTrackerExpandedVolumeFraction);
        constexpr std::array<double, 2u> starts { 0.0, noteShare };
        return starts[std::min<std::size_t>(field, starts.size() - 1u)];
    }
    constexpr std::array<double, 7u> starts {
        0.0,
        s3g::tracker::app::kTrackerExpandedNoteFraction,
        s3g::tracker::app::kTrackerExpandedNoteFraction
            + s3g::tracker::app::kTrackerExpandedVolumeFraction,
        0.49,
        0.61,
        0.74,
        0.87,
    };
    return starts[std::min<std::size_t>(field, starts.size() - 1u)];
}

double gridFieldEndFraction(bool sequenceColumnsExpanded, std::size_t field) noexcept
{
    if (!sequenceColumnsExpanded) {
        constexpr double noteShare = s3g::tracker::app::kTrackerExpandedNoteFraction
            / (s3g::tracker::app::kTrackerExpandedNoteFraction
                + s3g::tracker::app::kTrackerExpandedVolumeFraction);
        constexpr std::array<double, 2u> ends { noteShare, 1.0 };
        return ends[std::min<std::size_t>(field, ends.size() - 1u)];
    }
    constexpr std::array<double, 7u> ends {
        s3g::tracker::app::kTrackerExpandedNoteFraction,
        s3g::tracker::app::kTrackerExpandedNoteFraction
            + s3g::tracker::app::kTrackerExpandedVolumeFraction,
        0.49,
        0.61,
        0.74,
        0.87,
        1.0,
    };
    return ends[std::min<std::size_t>(field, ends.size() - 1u)];
}

Rect gridFieldRect(double laneX, double y, double laneWidth, double height,
    bool sequenceColumnsExpanded, std::size_t field) noexcept
{
    const double start = gridFieldStartFraction(sequenceColumnsExpanded, field);
    const double end = gridFieldEndFraction(sequenceColumnsExpanded, field);
    return makeRect(laneX + laneWidth * start, y, laneWidth * (end - start), height);
}

Rect gridLaneChannelRect(double fieldX, double fieldWidth) noexcept
{
    return makeRect(fieldX + std::max<double>(36.0, fieldWidth - 52.0), 4.0, 52.0, 16.0);
}

Rect gridLaneResyncRect(double fieldX, double fieldWidth) noexcept
{
    return makeRect(fieldX + std::max<double>(0.0, fieldWidth - 84.0), 4.0, 28.0, 16.0);
}

std::size_t gridFieldAtX(double localX, double laneWidth, bool sequenceColumnsExpanded) noexcept
{
    const double fraction = laneWidth > 0.0 ? std::clamp(localX / laneWidth, 0.0, 0.999999) : 0.0;
    const auto count = gridFieldCount(sequenceColumnsExpanded);
    for (std::size_t field = 0u; field < count; ++field) {
        if (fraction < gridFieldEndFraction(sequenceColumnsExpanded, field))
            return field;
    }
    return count - 1u;
}

double gridLaneWidth(bool sequenceColumnsExpanded) noexcept
{
    return static_cast<double>(sequenceColumnsExpanded
            ? s3g::tracker::app::kTrackerLaneExpandedWidth
            : s3g::tracker::app::kTrackerLaneCompactWidth);
}

double gridLaneX(std::size_t lane, double laneWidth) noexcept
{
    return app::kTrackerRowNumberWidth
        + static_cast<double>(lane) * (laneWidth + app::kTrackerLaneGutter);
}

double gridLaneFieldX(std::size_t lane, double laneWidth) noexcept
{
    return gridLaneX(lane, laneWidth) + app::kTrackerLaneInnerPadding;
}

double gridLaneFieldWidth(double laneWidth) noexcept
{
    return std::max<double>(1.0, laneWidth - 2.0 * app::kTrackerLaneInnerPadding);
}

bool gridLaneAtX(double x, std::size_t laneCount, bool sequenceColumnsExpanded, std::size_t& lane,
    double& localFieldX) noexcept
{
    if (laneCount == 0u || x < app::kTrackerRowNumberWidth)
        return false;
    const double laneWidth = gridLaneWidth(sequenceColumnsExpanded);
    const double laneStride = laneWidth + app::kTrackerLaneGutter;
    const double relativeX = x - app::kTrackerRowNumberWidth;
    const auto candidate = static_cast<std::size_t>(relativeX / laneStride);
    if (candidate >= laneCount)
        return false;
    const double withinLane = relativeX - static_cast<double>(candidate) * laneStride;
    if (withinLane > laneWidth)
        return false;

    lane = candidate;
    localFieldX = std::clamp(
        withinLane - app::kTrackerLaneInnerPadding, 0.0, gridLaneFieldWidth(laneWidth));
    return true;
}

ColumnDefinition* columnForField(Track& track, std::size_t page, std::size_t field) noexcept
{
    (void)page;
    field = std::min<std::size_t>(field, 6u);
    if (field == 0u)
        return &track.noteColumn;
    if (field == 1u)
        return &track.velocityColumn;
    if (field == 6u)
        return &track.gateColumn;
    auto& pair = track.fxPairs[(field - 2u) / 2u];
    return ((field - 2u) % 2u) == 0u ? &pair.actionColumn : &pair.valueColumn;
}

bool gridFieldIsGate(std::size_t field) noexcept { return field == 6u; }

bool gridFieldIsSequence(std::size_t field) noexcept { return field >= 2u && field < 6u; }

bool gridFieldIsSequenceAction(std::size_t field) noexcept
{
    return gridFieldIsSequence(field) && ((field - 2u) % 2u) == 0u;
}

std::size_t gridSequencePair(std::size_t field) noexcept
{
    return std::min<std::size_t>((field - 2u) / 2u, s3g::tracker::kFxPairCount - 1u);
}

uint8_t gridClipboardFieldType(std::size_t field) noexcept
{
    if (field == 0u)
        return 0u; // NOTE
    if (field == 1u)
        return 1u; // VOL
    if (gridFieldIsGate(field))
        return 4u; // GATE
    return gridFieldIsSequenceAction(field) ? 2u : 3u; // SEQ / VALUE
}

GridCell trackerGridCellAt(const Track& track, std::size_t field, std::size_t row)
{
    if (field == 0u)
        return row < track.notes.size() ? track.notes[row] : NoteCell::rest();
    if (field == 1u)
        return row < track.velocities.size() ? track.velocities[row] : ValueCell::defaultValue();
    if (gridFieldIsGate(field))
        return row < track.gates.size() ? track.gates[row] : GateCell::defaultValue();
    const auto pair = gridSequencePair(field);
    if (gridFieldIsSequenceAction(field))
        return row < track.fxPairs[pair].actions.size() ? track.fxPairs[pair].actions[row]
                                                        : FxActionCell::empty();
    return row < track.fxPairs[pair].values.size() ? track.fxPairs[pair].values[row]
                                                   : FxValueCell::previous();
}

void writeTrackerGridCell(Track& track, std::size_t field, std::size_t row, const GridCell& cell)
{
    if (field == 0u) {
        if (track.notes.size() <= row)
            track.notes.resize(row + 1u, NoteCell::rest());
        track.notes[row] = std::get<NoteCell>(cell);
        track.noteColumn.length = std::max(track.noteColumn.length, row + 1u);
        return;
    }
    if (field == 1u) {
        if (track.velocities.size() <= row)
            track.velocities.resize(row + 1u, ValueCell::defaultValue());
        track.velocities[row] = std::get<ValueCell>(cell);
        track.velocityColumn.length = std::max(track.velocityColumn.length, row + 1u);
        return;
    }
    if (gridFieldIsGate(field)) {
        if (track.gates.size() <= row)
            track.gates.resize(row + 1u, GateCell::defaultValue());
        track.gates[row] = std::get<GateCell>(cell);
        track.gateColumn.length = std::max(track.gateColumn.length, row + 1u);
        return;
    }
    auto& pair = track.fxPairs[gridSequencePair(field)];
    if (gridFieldIsSequenceAction(field)) {
        if (pair.actions.size() <= row)
            pair.actions.resize(row + 1u, FxActionCell::empty());
        pair.actions[row] = std::get<FxActionCell>(cell);
        pair.actionColumn.length = std::max(pair.actionColumn.length, row + 1u);
    } else {
        if (pair.values.size() <= row)
            pair.values.resize(row + 1u, FxValueCell::previous());
        pair.values[row] = std::get<FxValueCell>(cell);
        pair.valueColumn.length = std::max(pair.valueColumn.length, row + 1u);
    }
}

GridCell blankTrackerGridCell(std::size_t field)
{
    if (field == 0u)
        return NoteCell::rest();
    if (field == 1u)
        return ValueCell::defaultValue();
    if (gridFieldIsGate(field))
        return GateCell::defaultValue();
    if (gridFieldIsSequenceAction(field))
        return FxActionCell::empty();
    return FxValueCell::previous();
}

bool trackerGridCellEmpty(const GridCell& cell) noexcept
{
    if (const auto* note = std::get_if<NoteCell>(&cell))
        return note->state == NoteCellState::Rest;
    if (const auto* value = std::get_if<ValueCell>(&cell))
        return value->state == ValueCellState::Default;
    if (const auto* action = std::get_if<FxActionCell>(&cell))
        return action->state == FxActionCellState::Empty;
    if (const auto* gate = std::get_if<GateCell>(&cell))
        return gate->voiceCount == 0u;
    return std::get<FxValueCell>(cell).state == FxValueCellState::Previous;
}

bool trackerGridCellsEqual(const GridCell& a, const GridCell& b) noexcept
{
    if (a.index() != b.index())
        return false;
    if (const auto* left = std::get_if<NoteCell>(&a)) {
        const auto& right = std::get<NoteCell>(b);
        if (left->state != right.state || left->noteVoiceCount() != right.noteVoiceCount())
            return false;
        if (left->state == NoteCellState::Burst)
            return left->note == right.note && left->burstBankId == right.burstBankId;
        if (left->state != NoteCellState::Note)
            return left->note == right.note;
        for (std::size_t voice = 0u; voice < left->noteVoiceCount(); ++voice)
            if (left->noteVoice(voice) != right.noteVoice(voice))
                return false;
        return true;
    }
    if (const auto* left = std::get_if<ValueCell>(&a)) {
        const auto& right = std::get<ValueCell>(b);
        if (left->state != right.state || left->valueVoiceCount() != right.valueVoiceCount())
            return false;
        if (left->state != ValueCellState::Value)
            return true;
        for (std::size_t voice = 0u; voice < left->valueVoiceCount(); ++voice)
            if (left->valueVoice(voice) != right.valueVoice(voice))
                return false;
        return true;
    }
    if (const auto* left = std::get_if<FxActionCell>(&a)) {
        const auto& right = std::get<FxActionCell>(b);
        return left->state == right.state && left->targetNode == right.targetNode
            && left->parameterId == right.parameterId && left->scope == right.scope
            && left->sequencerAction == right.sequencerAction
            && left->midiController == right.midiController;
    }
    if (const auto* left = std::get_if<GateCell>(&a)) {
        const auto& right = std::get<GateCell>(b);
        if (left->voiceCount != right.voiceCount)
            return false;
        for (std::size_t voice = 0u; voice < left->gateVoiceCount(); ++voice) {
            const auto lv = left->gateVoice(voice);
            const auto rv = right.gateVoice(voice);
            if (lv.mode != rv.mode || lv.rows != rv.rows)
                return false;
        }
        return true;
    }
    const auto& left = std::get<FxValueCell>(a);
    const auto& right = std::get<FxValueCell>(b);
    if (left.state != right.state || left.valueVoiceCount() != right.valueVoiceCount())
        return false;
    if (left.state != FxValueCellState::Value)
        return true;
    for (std::size_t voice = 0u; voice < left.valueVoiceCount(); ++voice)
        if (left.valueVoice(voice) != right.valueVoice(voice))
            return false;
    return true;
}

std::size_t gridPlaybackRow(
    const TrackerViewState* state, std::size_t lane, std::size_t field) noexcept
{
    if (!state || lane >= s3g::tracker::kMaximumTrackCount)
        return 0u;
    if (field == 0u)
        return state->notePlayheads[lane];
    if (field == 1u)
        return state->velocityPlayheads[lane];
    if (gridFieldIsGate(field))
        return state->notePlayheads[lane];
    const auto pair = gridSequencePair(field);
    return gridFieldIsSequenceAction(field) ? state->fxActionPlayheads[lane][pair]
                                            : state->fxValuePlayheads[lane][pair];
}

float resolvedVelocity(const Track& track, std::size_t row)
{
    float value = 0.787f;
    if (track.velocities.empty())
        return value;
    const auto last = std::min(row, track.velocities.size() - 1u);
    for (std::size_t index = 0u; index <= last; ++index) {
        const auto& cell = track.velocities[index];
        if (cell.state == ValueCellState::Value)
            value = std::clamp(cell.normalized, 0.0f, 1.0f);
        else if (cell.state == ValueCellState::Default)
            value = 0.787f;
    }
    return value;
}

ValueCell resolvedVelocityCell(const Track& track, std::size_t row)
{
    ValueCell memory = ValueCell::withValue(0.787f);
    if (track.velocities.empty())
        return memory;
    const auto last = std::min(row, track.velocities.size() - 1u);
    for (std::size_t index = 0u; index <= last; ++index) {
        const auto& cell = track.velocities[index];
        if (cell.state == ValueCellState::Value)
            memory = cell;
        else if (cell.state == ValueCellState::Default)
            memory = ValueCell::withValue(0.787f);
    }
    return memory;
}

float resolvedFxValue(const Track& track, std::size_t pair, std::size_t row)
{
    float value = 0.0f;
    if (pair >= track.fxPairs.size() || track.fxPairs[pair].values.empty())
        return value;
    const auto& values = track.fxPairs[pair].values;
    const auto last = std::min(row, values.size() - 1u);
    for (std::size_t index = 0u; index <= last; ++index) {
        if (values[index].state == FxValueCellState::Value)
            value = std::clamp(values[index].normalized, 0.0f, 1.0f);
    }
    return value;
}

FxValueCell resolvedFxValueCell(const Track& track, std::size_t pair, std::size_t row)
{
    FxValueCell memory = FxValueCell::withValue(0.0f);
    if (pair >= track.fxPairs.size() || track.fxPairs[pair].values.empty())
        return memory;
    const auto& values = track.fxPairs[pair].values;
    const auto last = std::min(row, values.size() - 1u);
    for (std::size_t index = 0u; index <= last; ++index)
        if (values[index].state == FxValueCellState::Value)
            memory = values[index];
    return memory;
}

bool resolvedSequencerAction(
    const Track& track, std::size_t pair, std::size_t row, SequencerAction& resolved) noexcept
{
    if (pair >= track.fxPairs.size())
        return false;
    const auto& actions = track.fxPairs[pair].actions;
    if (row >= actions.size() || actions[row].state == FxActionCellState::Empty)
        return false;
    for (std::size_t index = row + 1u; index-- > 0u;) {
        const auto& action = actions[index];
        if (action.state == FxActionCellState::Empty || action.state == FxActionCellState::Previous)
            continue;
        if (action.state != FxActionCellState::Sequencer)
            return false;
        resolved = action.sequencerAction;
        return resolved != SequencerAction::Count;
    }
    return false;
}

bool pairContainsMidiControlChange(const FxPair& pair) noexcept
{
    return std::any_of(pair.actions.begin(), pair.actions.end(), [](const FxActionCell& cell) {
        return cell.state == FxActionCellState::MidiControlChange;
    });
}

bool noteCellIsActivePulse(const NoteCell& cell) noexcept
{
    return cell.state == NoteCellState::Note || cell.state == NoteCellState::RetriggerPrevious
        || cell.state == NoteCellState::Burst;
}

Direction nextDirection(Direction direction)
{
    switch (direction) {
    case Direction::Forward:
        return Direction::Reverse;
    case Direction::Reverse:
        return Direction::Palindrome;
    case Direction::Palindrome:
        return Direction::Random;
    case Direction::Random:
    default:
        return Direction::Forward;
    }
}

}
