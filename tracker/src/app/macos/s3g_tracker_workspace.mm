#import "s3g_tracker_workspace.h"
#import "s3g_tracker_controls.h"
#import "s3g_tracker_phrase_view.h"
#import "s3g_tracker_reshape_window.h"
#import "s3g_tracker_warp_window.h"
#include "s3g_tracker_grid_input.h"
#include "s3g_tracker_grid_selection.h"
#include "s3g_tracker_workspace_layout.h"
#define S3G_COCOA_GUI_DRAWING_ONLY 1
#include "s3g_cocoa_gui.h"
#undef S3G_COCOA_GUI_DRAWING_ONLY

#include "s3g/tracker/fx_catalog.h"
#include "s3g/tracker/geometry_edit.h"
#include "s3g/tracker/command.h"
#include "s3g/tracker/pitch_map.h"

#include "s3g_musical_scales.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {

using s3g::tracker::Direction;
using s3g::tracker::BurstDefinition;
using s3g::tracker::BurstEvent;
using s3g::tracker::ColumnDefinition;
using s3g::tracker::EventDestination;
using s3g::tracker::FxActionCell;
using s3g::tracker::FxActionCellState;
using s3g::tracker::FxPair;
using s3g::tracker::FxValueCell;
using s3g::tracker::FxValueCellState;
using s3g::tracker::GateCell;
using s3g::tracker::GateVoice;
using s3g::tracker::GateVoiceMode;
using s3g::tracker::InstrumentCell;
using s3g::tracker::InstrumentCellState;
using s3g::tracker::MidiStepRecordMode;
using s3g::tracker::NoteCell;
using s3g::tracker::NoteCellState;
using s3g::tracker::Pattern;
using s3g::tracker::PitchContour;
using s3g::tracker::PitchMapAnalysis;
using s3g::tracker::PitchMapAssignment;
using s3g::tracker::PitchMapResult;
using s3g::tracker::PitchMapSettings;
using s3g::tracker::PitchPreviewEvent;
using s3g::tracker::SequencerAction;
using s3g::tracker::Track;
using s3g::tracker::ValueCell;
using s3g::tracker::ValueCellState;
using s3g::tracker::ValueInterpolation;
using s3g::tracker::burstSlotToken;
using s3g::tracker::fitBurstGatesToRow;
using s3g::tracker::kBurstDefinitionCount;
using s3g::tracker::kMaximumBurstEvents;
using s3g::tracker::kMaximumBurstNameBytes;
using s3g::tracker::app::TrackerViewState;
using s3g::tracker::app::WorkspaceCallbacks;
using s3g::tracker::app::gridClipboardColumn;
namespace layout = s3g::gui_layout;

constexpr CGFloat kGridHeaderHeight = static_cast<CGFloat>(
    s3g::tracker::app::kTrackerGridHeaderHeight);
constexpr CGFloat kGridRowHeight = static_cast<CGFloat>(
    s3g::tracker::app::kTrackerGridRowHeight);
constexpr CGFloat kGridRowNumberWidth =
    s3g::tracker::app::kTrackerRowNumberWidth;
constexpr CGFloat kGridLaneGutter =
    s3g::tracker::app::kTrackerLaneGutter;
constexpr CGFloat kGridLaneInnerPadding =
    s3g::tracker::app::kTrackerLaneInnerPadding;
constexpr CGFloat kGridColumnLabelTop = 21.0;
constexpr CGFloat kGridColumnLabelHeight = 13.0;
constexpr CGFloat kGridColumnLengthTop = 34.0;
constexpr CGFloat kGridColumnLengthHeight = 13.0;
constexpr CGFloat kGridColumnReadStartTop = 47.0;
constexpr CGFloat kGridColumnReadStartHeight = 13.0;
constexpr CGFloat kGridColumnDirectionTop = 60.0;
constexpr CGFloat kGridColumnDirectionHeight = 13.0;
constexpr CGFloat kGridColumnMuteTop = 73.0;
constexpr CGFloat kGridColumnMuteHeight = 13.0;
constexpr std::size_t kGridMaximumRows = 256u;

NSColor* trackerColor(uint32_t rgb, CGFloat alpha = 1.0)
{
    return S3GTrackerColor(rgb, alpha);
}

NSFont* trackerFont(CGFloat size, NSFontWeight weight = NSFontWeightRegular)
{
    return S3GTrackerFont(size, weight);
}

void fillRect(NSRect rect, NSColor* color)
{
    [color setFill];
    NSRectFill(rect);
}

void strokeRect(NSRect rect, NSColor* color, CGFloat width = 1.0)
{
    [color setStroke];
    NSBezierPath* path = [NSBezierPath bezierPathWithRect:rect];
    path.lineWidth = width;
    [path stroke];
}

void drawText(NSString* text, NSRect rect, NSColor* color, CGFloat size,
    NSFontWeight weight = NSFontWeightRegular,
    NSTextAlignment alignment = NSTextAlignmentLeft)
{
    NSMutableParagraphStyle* style = [[NSMutableParagraphStyle alloc] init];
    style.alignment = alignment;
    style.lineBreakMode = NSLineBreakByClipping;
    [text drawInRect:rect withAttributes:@{
        NSForegroundColorAttributeName: color,
        NSFontAttributeName: trackerFont(size, weight),
        NSParagraphStyleAttributeName: style,
    }];
}

void drawCenteredText(NSString* text, NSRect rect, NSColor* color,
    CGFloat size, NSFontWeight weight = NSFontWeightRegular,
    NSTextAlignment alignment = NSTextAlignmentCenter)
{
    NSFont* font = trackerFont(size, weight);
    const CGFloat lineHeight = std::ceil(
        font.ascender - font.descender + font.leading);
    NSMutableParagraphStyle* style = [[NSMutableParagraphStyle alloc] init];
    style.alignment = alignment;
    style.lineBreakMode = NSLineBreakByClipping;
    style.minimumLineHeight = lineHeight;
    style.maximumLineHeight = lineHeight;
    const NSRect lineRect = NSMakeRect(NSMinX(rect),
        std::floor(NSMidY(rect) - lineHeight * 0.5), NSWidth(rect),
        lineHeight);
    [text drawInRect:lineRect withAttributes:@{
        NSForegroundColorAttributeName: color,
        NSFontAttributeName: font,
        NSParagraphStyleAttributeName: style,
    }];
}

void drawTrackerProcessorMenu(NSString* name, NSString* value, CGFloat y,
    CGFloat panelX, CGFloat panelWidth, NSDictionary* labelAttributes,
    NSDictionary* valueAttributes, const s3g::clap_gui::Style& style)
{
    // The shared renderer's historical label baseline sits three points above
    // its menu value. Tracker menus use the same renderer and metrics, but put
    // both strings on the menu-value baseline so their visual middles align.
    s3g::clap_gui::drawProcessorMenu(@"", value, y, panelX, panelWidth,
        labelAttributes, valueAttributes, style);
    [[name uppercaseString] drawAtPoint:NSMakePoint(
        static_cast<CGFloat>(layout::processorLabelX(panelX)), y + 1.0)
        withAttributes:labelAttributes];
}

void drawGeometryReadHead(NSPoint point, CGFloat intensity, bool currentHit,
    bool selected)
{
    intensity = std::clamp<CGFloat>(intensity, 0.0, 1.0);
    if (intensity <= 0.0) return;
    NSColor* yellow = [trackerColor(0xffdf3f)
        colorWithAlphaComponent:intensity * (currentHit ? 1.0 : 0.78)];
    const CGFloat coreRadius = currentHit
        ? (selected ? 5.4 : 4.8)
        : (selected ? 4.6 : 4.0);
    NSBezierPath* outline = [NSBezierPath bezierPathWithOvalInRect:
        NSMakeRect(point.x - coreRadius - 1.0,
            point.y - coreRadius - 1.0,
            (coreRadius + 1.0) * 2.0, (coreRadius + 1.0) * 2.0)];
    [trackerColor(0x161300, currentHit ? 0.88 : 0.58 * intensity) setFill];
    [outline fill];
    NSBezierPath* core = [NSBezierPath bezierPathWithOvalInRect:
        NSMakeRect(point.x - coreRadius, point.y - coreRadius,
            coreRadius * 2.0, coreRadius * 2.0)];
    [yellow setFill];
    [core fill];
    NSBezierPath* center = [NSBezierPath bezierPathWithOvalInRect:
        NSMakeRect(point.x - 1.7, point.y - 1.7, 3.4, 3.4)];
    [[trackerColor(0xffef91) colorWithAlphaComponent:
        currentHit ? 1.0 : intensity * 0.72] setFill];
    [center fill];
}

NSString* nsString(const std::string& value)
{
    NSString* result = [NSString stringWithUTF8String:value.c_str()];
    return result ? result : @"";
}

const Pattern* playbackFollowPattern(const TrackerViewState* state)
{
    if (!state) return nullptr;
    if (state->songPlaybackActive
        && !state->songPlaybackPatternId.empty()) {
        if (const auto* pattern = state->patternBank.findPattern(
                state->songPlaybackPatternId))
            return pattern;
    }
    return &state->session.pattern;
}

std::string playbackFollowPatternId(const TrackerViewState* state)
{
    if (!state) return {};
    if (state->songPlaybackActive
        && state->patternBank.findPattern(state->songPlaybackPatternId))
        return state->songPlaybackPatternId;
    return state->patternBank.activePatternId;
}

std::size_t visibleRows(const TrackerViewState* state)
{
    if (!state) return 16u;
    return std::clamp<std::size_t>(std::max(
        state->session.pattern.visibleRows, state->session.selectedRow + 1u),
        16u, 256u);
}

std::size_t playbackFollowVisibleRows(const TrackerViewState* state)
{
    const auto* pattern = playbackFollowPattern(state);
    if (!pattern) return 16u;
    if (!state->songPlaybackActive) return visibleRows(state);
    return std::clamp<std::size_t>(pattern->visibleRows, 16u, 256u);
}

NSString* directionMark(Direction direction)
{
    switch (direction) {
    case Direction::Reverse: return @"<";
    case Direction::Random: return @"RND";
    case Direction::Palindrome: return @"<>";
    case Direction::Forward:
    default: return @">";
    }
}

Direction nextDirection(Direction direction)
{
    switch (direction) {
    case Direction::Forward: return Direction::Reverse;
    case Direction::Reverse: return Direction::Palindrome;
    case Direction::Palindrome: return Direction::Random;
    case Direction::Random:
    default: return Direction::Forward;
    }
}

NSString* midiNoteName(uint8_t note)
{
    constexpr std::array<const char*, 12u> names {
        "C-", "C#", "D-", "D#", "E-", "F-",
        "F#", "G-", "G#", "A-", "A#", "B-",
    };
    const int octave = static_cast<int>(note) / 12 - 1;
    return [NSString stringWithFormat:@"%s%d", names[note % 12u], octave];
}

NSString* pitchAssignmentText(const PitchMapAssignment& assignment)
{
    NSMutableArray<NSString*>* voices = [NSMutableArray arrayWithCapacity:
        static_cast<NSUInteger>(assignment.voiceCount)];
    for (std::size_t voice = 0u; voice < assignment.voiceCount; ++voice) {
        const auto note = assignment.notes[voice];
        [voices addObject:[NSString stringWithFormat:@"%@ · MIDI %03u",
            midiNoteName(note), static_cast<unsigned int>(note)]];
    }
    return [voices componentsJoinedByString:@"  +  "];
}

NSString* noteVoiceText(uint8_t note, bool showMidiValue)
{
    return showMidiValue
        ? [NSString stringWithFormat:@"%u", static_cast<unsigned int>(note)]
        : midiNoteName(note);
}

NSString* noteText(const NoteCell& cell, bool showMidiValue,
    bool compact = true)
{
    switch (cell.state) {
    case NoteCellState::Note: {
        const auto voices = cell.noteVoiceCount();
        if (compact && voices > 1u)
            return [NSString stringWithFormat:@"%@+%lu",
                noteVoiceText(cell.note, showMidiValue),
                static_cast<unsigned long>(voices - 1u)];
        NSMutableArray<NSString*>* values = [NSMutableArray arrayWithCapacity:
            static_cast<NSUInteger>(voices)];
        for (std::size_t voice = 0u; voice < voices; ++voice)
            [values addObject:noteVoiceText(
                cell.noteVoice(voice), showMidiValue)];
        return [values componentsJoinedByString:@"+"];
    }
    case NoteCellState::Burst:
        return nsString(s3g::tracker::burstSlotToken(cell.note));
    case NoteCellState::RetriggerPrevious: return @"RPT";
    case NoteCellState::Kill: return @"KIL";
    case NoteCellState::Hold: return @"HLD";
    case NoteCellState::Rest:
    default: return @"---";
    }
}

bool parseNoteStack(NSString* source,
    std::array<uint8_t, s3g::tracker::kMaximumNoteVoices>& output,
    std::size_t& count)
{
    NSArray<NSString*>* parts = [source componentsSeparatedByString:@"+"];
    if (parts.count == 0u
        || parts.count > s3g::tracker::kMaximumNoteVoices) return false;
    count = 0u;
    for (NSString* part in parts) {
        NSString* trimmed = [part stringByTrimmingCharactersInSet:
            NSCharacterSet.whitespaceAndNewlineCharacterSet];
        uint8_t note = 0u;
        const char* utf8 = trimmed.UTF8String;
        if (!utf8 || !s3g::tracker::parseMidiNote(utf8, note)) return false;
        output[count++] = note;
    }
    std::sort(output.begin(), output.begin()
        + static_cast<std::ptrdiff_t>(count));
    count = static_cast<std::size_t>(std::unique(output.begin(),
        output.begin() + static_cast<std::ptrdiff_t>(count)) - output.begin());
    return count > 0u;
}

bool parseVelocityStack(NSString* source,
    std::array<float, s3g::tracker::kMaximumNoteVoices>& output,
    std::size_t& count)
{
    NSArray<NSString*>* parts = [source componentsSeparatedByString:@"+"];
    if (parts.count == 0u
        || parts.count > s3g::tracker::kMaximumNoteVoices) return false;
    count = 0u;
    for (NSString* part in parts) {
        NSString* trimmed = [part stringByTrimmingCharactersInSet:
            NSCharacterSet.whitespaceAndNewlineCharacterSet];
        const char* utf8 = trimmed.UTF8String;
        float value = 0.0f;
        if (!utf8 || !s3g::tracker::app::parseGridNormalizedValue(
                std::string_view(utf8), value)) return false;
        output[count++] = value;
    }
    return count > 0u;
}

bool noteCellIsActivePulse(const NoteCell& cell) noexcept
{
    return cell.state == NoteCellState::Note
        || cell.state == NoteCellState::RetriggerPrevious
        || cell.state == NoteCellState::Burst;
}

void setGeometryBurstTiming(BurstDefinition& burst,
    std::string_view shape) noexcept
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

std::size_t geometryBurstUsageCount(const Pattern& pattern,
    std::size_t slot) noexcept
{
    std::size_t count = 0u;
    for (const auto& track : pattern.tracks)
        count += static_cast<std::size_t>(std::count_if(
            track.notes.begin(), track.notes.end(), [slot](const NoteCell& cell) {
                return cell.state == NoteCellState::Burst
                    && cell.note == slot;
            }));
    return count;
}

std::size_t projectBurstUsageCount(const TrackerViewState& state,
    std::size_t slot) noexcept
{
    std::size_t count = 0u;
    for (const auto& entry : state.patternBank.entries) {
        const Pattern* pattern = &entry.pattern;
        if (entry.id == state.patternBank.activePatternId)
            pattern = &state.session.pattern;
        count += geometryBurstUsageCount(*pattern, slot);
    }
    for (const auto& phrase : state.phraseLibrary.phrases) {
        count += static_cast<std::size_t>(std::count_if(
            phrase.notes.begin(), phrase.notes.end(), [&](const NoteCell& cell) {
                return cell.state == NoteCellState::Burst
                    && cell.note == slot;
            }));
    }
    return count;
}

float resolvedVelocity(const Track& track, std::size_t row)
{
    float value = 0.787f;
    if (track.velocities.empty()) return value;
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
    if (track.velocities.empty()) return memory;
    const auto last = std::min(row, track.velocities.size() - 1u);
    for (std::size_t index = 0u; index <= last; ++index) {
        const auto& cell = track.velocities[index];
        if (cell.state == ValueCellState::Value) memory = cell;
        else if (cell.state == ValueCellState::Default)
            memory = ValueCell::withValue(0.787f);
    }
    return memory;
}

float resolvedFxValue(const Track& track, std::size_t pair,
    std::size_t row)
{
    float value = 0.0f;
    if (pair >= track.fxPairs.size()
        || track.fxPairs[pair].values.empty()) return value;
    const auto& values = track.fxPairs[pair].values;
    const auto last = std::min(row, values.size() - 1u);
    for (std::size_t index = 0u; index <= last; ++index) {
        if (values[index].state == FxValueCellState::Value)
            value = std::clamp(values[index].normalized, 0.0f, 1.0f);
    }
    return value;
}

FxValueCell resolvedFxValueCell(const Track& track, std::size_t pair,
    std::size_t row)
{
    FxValueCell memory = FxValueCell::withValue(0.0f);
    if (pair >= track.fxPairs.size()
        || track.fxPairs[pair].values.empty()) return memory;
    const auto& values = track.fxPairs[pair].values;
    const auto last = std::min(row, values.size() - 1u);
    for (std::size_t index = 0u; index <= last; ++index)
        if (values[index].state == FxValueCellState::Value)
            memory = values[index];
    return memory;
}

bool resolvedSequencerAction(const Track& track, std::size_t pair,
    std::size_t row, SequencerAction& resolved) noexcept
{
    if (pair >= track.fxPairs.size()) return false;
    const auto& actions = track.fxPairs[pair].actions;
    if (row >= actions.size()
        || actions[row].state == FxActionCellState::Empty) return false;
    for (std::size_t index = row + 1u; index-- > 0u;) {
        const auto& action = actions[index];
        if (action.state == FxActionCellState::Empty
            || action.state == FxActionCellState::Previous) continue;
        if (action.state != FxActionCellState::Sequencer) return false;
        resolved = action.sequencerAction;
        return resolved != SequencerAction::Count;
    }
    return false;
}

NSString* volumeText(const Track& track, std::size_t row,
    bool compact = true)
{
    if (row >= track.velocities.size()) return @"DEF";
    const auto& cell = track.velocities[row];
    if (cell.state == ValueCellState::Previous) return @"PRV";
    if (cell.state == ValueCellState::Default) return @"DEF";
    const auto voices = cell.valueVoiceCount();
    if (compact && voices > 1u)
        return [NSString stringWithFormat:@"%.3f+%lu", static_cast<double>(
            std::clamp(cell.normalized, 0.0f, 1.0f)),
            static_cast<unsigned long>(voices - 1u)];
    NSMutableArray<NSString*>* values = [NSMutableArray arrayWithCapacity:
        static_cast<NSUInteger>(voices)];
    for (std::size_t voice = 0u; voice < voices; ++voice)
        [values addObject:[NSString stringWithFormat:@"%.3f",
            static_cast<double>(std::clamp(
                cell.valueVoice(voice), 0.0f, 1.0f))]];
    return [values componentsJoinedByString:@"+"];
}

uint32_t laneInitialInstrument(const Track& track) noexcept
{
    return s3g::tracker::isMidiOutInstrumentNode(
        track.initialInstrumentNodeId)
        ? track.initialInstrumentNodeId
        : s3g::tracker::midiOutNodeForRackSlot(0u);
}

NSString* fxActionText(const Track& track, std::size_t pair,
    std::size_t row)
{
    if (pair >= track.fxPairs.size()
        || row >= track.fxPairs[pair].actions.size()) return @"---";
    const auto& cell = track.fxPairs[pair].actions[row];
    if (cell.state == FxActionCellState::Empty) return @"---";
    if (cell.state == FxActionCellState::Previous) return @"PRV";
    if (cell.state == FxActionCellState::Sequencer) {
        const auto* action = s3g::tracker::findSequencerAction(
            cell.sequencerAction);
        return action ? [NSString stringWithUTF8String:
            std::string(action->mnemonic).c_str()] : @"???";
    }
    if (cell.state == FxActionCellState::MidiControlChange) {
        return [NSString stringWithFormat:@"CC%u",
            static_cast<unsigned int>(cell.midiController)];
    }
    // Audio-parameter actions from the former internal-instrument build are
    // intentionally not presented by the MIDI-only tracker.
    return @"---";
}

bool pairContainsMidiControlChange(const FxPair& pair) noexcept
{
    return std::any_of(pair.actions.begin(), pair.actions.end(),
        [](const FxActionCell& cell) {
            return cell.state == FxActionCellState::MidiControlChange;
        });
}

NSString* fxValueText(const Track& track, std::size_t pair,
    std::size_t row, bool compact = true)
{
    if (pair >= track.fxPairs.size()
        || row >= track.fxPairs[pair].values.size()) return @"PRV";
    const auto& cell = track.fxPairs[pair].values[row];
    if (cell.state == FxValueCellState::Previous) return @"PRV";
    const auto& actions = track.fxPairs[pair].actions;
    if (row < actions.size()
        && actions[row].state == FxActionCellState::Sequencer
        && actions[row].sequencerAction == SequencerAction::Condition) {
        const auto condition = s3g::tracker::sequencerConditionFromNormalized(
            cell.normalized);
        const auto* definition = s3g::tracker::sequencerCondition(
            static_cast<std::size_t>(condition));
        if (definition) return nsString(std::string(definition->token));
    }
    if (cell.valueVoiceCount() > 1u) {
        if (compact)
            return [NSString stringWithFormat:@"%.3f+%lu",
                static_cast<double>(std::clamp(
                    cell.normalized, 0.0f, 1.0f)),
                static_cast<unsigned long>(cell.valueVoiceCount() - 1u)];
        NSMutableArray<NSString*>* values = [NSMutableArray
            arrayWithCapacity:cell.valueVoiceCount()];
        for (std::size_t voice = 0u;
             voice < cell.valueVoiceCount(); ++voice) {
            [values addObject:[NSString stringWithFormat:@"%.3f",
                static_cast<double>(std::clamp(
                    cell.valueVoice(voice), 0.0f, 1.0f))]];
        }
        return [values componentsJoinedByString:@"+"];
    }
    return [NSString stringWithFormat:@"%.3f",
        static_cast<double>(
            std::clamp(cell.normalized, 0.0f, 1.0f))];
}

NSString* gateText(const Track& track, std::size_t row, bool compact = true)
{
    if (row >= track.gates.size() || track.gates[row].voiceCount == 0u)
        return @"DEF";
    NSMutableArray<NSString*>* values = [NSMutableArray array];
    const auto& cell = track.gates[row];
    for (std::size_t voice = 0u; voice < cell.gateVoiceCount(); ++voice) {
        const auto gate = cell.gateVoice(voice);
        if (gate.mode == GateVoiceMode::Default) [values addObject:@"DEF"];
        else if (gate.mode == GateVoiceMode::Tie) [values addObject:@"TIE"];
        else [values addObject:[NSString stringWithFormat:
            compact ? @"%.2g" : @"%.3g", static_cast<double>(gate.rows)]];
    }
    return [values componentsJoinedByString:@"+"];
}

std::size_t gridFieldCount(bool sequenceColumnsExpanded) noexcept
{
    return sequenceColumnsExpanded ? 7u : 2u;
}

CGFloat gridFieldStartFraction(bool sequenceColumnsExpanded,
    std::size_t field) noexcept
{
    if (!sequenceColumnsExpanded) {
        constexpr CGFloat noteShare =
            s3g::tracker::app::kTrackerExpandedNoteFraction
            / (s3g::tracker::app::kTrackerExpandedNoteFraction
                + s3g::tracker::app::kTrackerExpandedVolumeFraction);
        constexpr std::array<CGFloat, 2u> starts { 0.0, noteShare };
        return starts[std::min<std::size_t>(
            field, starts.size() - 1u)];
    }
    constexpr std::array<CGFloat, 7u> starts {
        0.0, s3g::tracker::app::kTrackerExpandedNoteFraction,
        s3g::tracker::app::kTrackerExpandedNoteFraction
            + s3g::tracker::app::kTrackerExpandedVolumeFraction,
        0.49, 0.61, 0.74, 0.87,
    };
    return starts[std::min<std::size_t>(field, starts.size() - 1u)];
}

CGFloat gridFieldEndFraction(bool sequenceColumnsExpanded,
    std::size_t field) noexcept
{
    if (!sequenceColumnsExpanded) {
        constexpr CGFloat noteShare =
            s3g::tracker::app::kTrackerExpandedNoteFraction
            / (s3g::tracker::app::kTrackerExpandedNoteFraction
                + s3g::tracker::app::kTrackerExpandedVolumeFraction);
        constexpr std::array<CGFloat, 2u> ends { noteShare, 1.0 };
        return ends[std::min<std::size_t>(field, ends.size() - 1u)];
    }
    constexpr std::array<CGFloat, 7u> ends {
        s3g::tracker::app::kTrackerExpandedNoteFraction,
        s3g::tracker::app::kTrackerExpandedNoteFraction
            + s3g::tracker::app::kTrackerExpandedVolumeFraction,
        0.49, 0.61, 0.74, 0.87, 1.0,
    };
    return ends[std::min<std::size_t>(field, ends.size() - 1u)];
}

NSRect gridFieldRect(CGFloat laneX, CGFloat y, CGFloat laneWidth,
    CGFloat height, bool sequenceColumnsExpanded,
    std::size_t field) noexcept
{
    const CGFloat start = gridFieldStartFraction(
        sequenceColumnsExpanded, field);
    const CGFloat end = gridFieldEndFraction(
        sequenceColumnsExpanded, field);
    return NSMakeRect(laneX + laneWidth * start, y,
        laneWidth * (end - start), height);
}

NSRect gridLaneChannelRect(CGFloat fieldX, CGFloat fieldWidth) noexcept
{
    return NSMakeRect(fieldX + std::max<CGFloat>(36.0, fieldWidth - 52.0),
        4.0, 52.0, 16.0);
}

NSRect gridLaneResyncRect(CGFloat fieldX, CGFloat fieldWidth) noexcept
{
    return NSMakeRect(fieldX + std::max<CGFloat>(0.0, fieldWidth - 84.0),
        4.0, 28.0, 16.0);
}

std::size_t gridFieldAtX(CGFloat localX, CGFloat laneWidth,
    bool sequenceColumnsExpanded) noexcept
{
    const CGFloat fraction = laneWidth > 0.0
        ? std::clamp(localX / laneWidth, 0.0, 0.999999) : 0.0;
    const auto count = gridFieldCount(sequenceColumnsExpanded);
    for (std::size_t field = 0u; field < count; ++field) {
        if (fraction < gridFieldEndFraction(
                sequenceColumnsExpanded, field)) return field;
    }
    return count - 1u;
}

CGFloat gridLaneWidth(bool sequenceColumnsExpanded) noexcept
{
    return static_cast<CGFloat>(sequenceColumnsExpanded
        ? s3g::tracker::app::kTrackerLaneExpandedWidth
        : s3g::tracker::app::kTrackerLaneCompactWidth);
}

CGFloat gridLaneX(std::size_t lane, CGFloat laneWidth) noexcept
{
    return kGridRowNumberWidth + static_cast<CGFloat>(lane)
        * (laneWidth + kGridLaneGutter);
}

CGFloat gridLaneFieldX(std::size_t lane, CGFloat laneWidth) noexcept
{
    return gridLaneX(lane, laneWidth) + kGridLaneInnerPadding;
}

CGFloat gridLaneFieldWidth(CGFloat laneWidth) noexcept
{
    return std::max<CGFloat>(1.0,
        laneWidth - 2.0 * kGridLaneInnerPadding);
}

bool gridLaneAtX(CGFloat x, std::size_t laneCount,
    bool sequenceColumnsExpanded, std::size_t& lane,
    CGFloat& localFieldX) noexcept
{
    if (laneCount == 0u || x < kGridRowNumberWidth) return false;
    const CGFloat laneWidth = gridLaneWidth(sequenceColumnsExpanded);
    const CGFloat laneStride = laneWidth + kGridLaneGutter;
    const CGFloat relativeX = x - kGridRowNumberWidth;
    const auto candidate = static_cast<std::size_t>(relativeX / laneStride);
    if (candidate >= laneCount) return false;
    const CGFloat withinLane = relativeX
        - static_cast<CGFloat>(candidate) * laneStride;
    if (withinLane > laneWidth) return false;

    lane = candidate;
    localFieldX = std::clamp(withinLane - kGridLaneInnerPadding,
        0.0, gridLaneFieldWidth(laneWidth));
    return true;
}

ColumnDefinition* columnForField(Track& track, std::size_t page,
    std::size_t field) noexcept
{
    (void)page;
    field = std::min<std::size_t>(field, 6u);
    if (field == 0u) return &track.noteColumn;
    if (field == 1u) return &track.velocityColumn;
    if (field == 6u) return &track.gateColumn;
    auto& pair = track.fxPairs[(field - 2u) / 2u];
    return ((field - 2u) % 2u) == 0u
        ? &pair.actionColumn : &pair.valueColumn;
}

bool gridFieldIsGate(std::size_t field) noexcept { return field == 6u; }
bool gridFieldIsSequence(std::size_t field) noexcept
{
    return field >= 2u && field < 6u;
}
bool gridFieldIsSequenceAction(std::size_t field) noexcept
{
    return gridFieldIsSequence(field) && ((field - 2u) % 2u) == 0u;
}

std::size_t gridSequencePair(std::size_t field) noexcept;

uint8_t gridClipboardFieldType(std::size_t field) noexcept
{
    if (field == 0u) return 0u; // NOTE
    if (field == 1u) return 1u; // VOL
    if (gridFieldIsGate(field)) return 4u; // GATE
    return gridFieldIsSequenceAction(field) ? 2u : 3u; // SEQ / VALUE
}

using TrackerGridCell = std::variant<NoteCell, ValueCell,
    FxActionCell, FxValueCell, GateCell>;

TrackerGridCell trackerGridCellAt(const Track& track, std::size_t field,
    std::size_t row)
{
    if (field == 0u)
        return row < track.notes.size() ? track.notes[row] : NoteCell::rest();
    if (field == 1u)
        return row < track.velocities.size() ? track.velocities[row]
                                             : ValueCell::defaultValue();
    if (gridFieldIsGate(field))
        return row < track.gates.size() ? track.gates[row]
                                        : GateCell::defaultValue();
    const auto pair = gridSequencePair(field);
    if (gridFieldIsSequenceAction(field))
        return row < track.fxPairs[pair].actions.size()
            ? track.fxPairs[pair].actions[row] : FxActionCell::empty();
    return row < track.fxPairs[pair].values.size()
        ? track.fxPairs[pair].values[row] : FxValueCell::previous();
}

void writeTrackerGridCell(Track& track, std::size_t field,
    std::size_t row, const TrackerGridCell& cell)
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
        track.velocityColumn.length = std::max(
            track.velocityColumn.length, row + 1u);
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

TrackerGridCell blankTrackerGridCell(std::size_t field)
{
    if (field == 0u) return NoteCell::rest();
    if (field == 1u) return ValueCell::defaultValue();
    if (gridFieldIsGate(field)) return GateCell::defaultValue();
    if (gridFieldIsSequenceAction(field)) return FxActionCell::empty();
    return FxValueCell::previous();
}

bool trackerGridCellEmpty(const TrackerGridCell& cell) noexcept
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

bool trackerGridCellsEqual(const TrackerGridCell& a,
    const TrackerGridCell& b) noexcept
{
    if (a.index() != b.index()) return false;
    if (const auto* left = std::get_if<NoteCell>(&a)) {
        const auto& right = std::get<NoteCell>(b);
        if (left->state != right.state
            || left->noteVoiceCount() != right.noteVoiceCount()) return false;
        if (left->state != NoteCellState::Note) return left->note == right.note;
        for (std::size_t voice = 0u; voice < left->noteVoiceCount(); ++voice)
            if (left->noteVoice(voice) != right.noteVoice(voice)) return false;
        return true;
    }
    if (const auto* left = std::get_if<ValueCell>(&a)) {
        const auto& right = std::get<ValueCell>(b);
        if (left->state != right.state
            || left->valueVoiceCount() != right.valueVoiceCount()) return false;
        if (left->state != ValueCellState::Value) return true;
        for (std::size_t voice = 0u; voice < left->valueVoiceCount(); ++voice)
            if (left->valueVoice(voice) != right.valueVoice(voice)) return false;
        return true;
    }
    if (const auto* left = std::get_if<FxActionCell>(&a)) {
        const auto& right = std::get<FxActionCell>(b);
        return left->state == right.state && left->targetNode == right.targetNode
            && left->parameterId == right.parameterId
            && left->scope == right.scope
            && left->sequencerAction == right.sequencerAction
            && left->midiController == right.midiController;
    }
    if (const auto* left = std::get_if<GateCell>(&a)) {
        const auto& right = std::get<GateCell>(b);
        if (left->voiceCount != right.voiceCount) return false;
        for (std::size_t voice = 0u; voice < left->gateVoiceCount(); ++voice) {
            const auto lv = left->gateVoice(voice);
            const auto rv = right.gateVoice(voice);
            if (lv.mode != rv.mode || lv.rows != rv.rows) return false;
        }
        return true;
    }
    const auto& left = std::get<FxValueCell>(a);
    const auto& right = std::get<FxValueCell>(b);
    if (left.state != right.state
        || left.valueVoiceCount() != right.valueVoiceCount()) return false;
    if (left.state != FxValueCellState::Value) return true;
    for (std::size_t voice = 0u; voice < left.valueVoiceCount(); ++voice)
        if (left.valueVoice(voice) != right.valueVoice(voice)) return false;
    return true;
}
std::size_t gridSequencePair(std::size_t field) noexcept
{
    return std::min<std::size_t>((field - 2u) / 2u,
        s3g::tracker::kFxPairCount - 1u);
}

std::size_t gridPlaybackRow(const TrackerViewState* state,
    std::size_t lane, std::size_t field) noexcept
{
    if (!state || lane >= s3g::tracker::kMaximumTrackCount) return 0u;
    if (field == 0u) return state->notePlayheads[lane];
    if (field == 1u) return state->velocityPlayheads[lane];
    if (gridFieldIsGate(field)) return state->notePlayheads[lane];
    const auto pair = gridSequencePair(field);
    return gridFieldIsSequenceAction(field)
        ? state->fxActionPlayheads[lane][pair]
        : state->fxValuePlayheads[lane][pair];
}

constexpr std::array<double, 7u> kTempoScales {
    0.25, 0.5, 2.0 / 3.0, 1.0, 1.5, 2.0, 4.0,
};
constexpr std::array<const char*, 7u> kTempoScaleNames {
    "1/4×", "1/2×", "2/3×", "1×", "3/2×", "2×", "4×",
};
constexpr std::array<double, 20u> kGateMilliseconds {
    1.0, 5.0, 10.0, 15.0, 20.0, 25.0, 30.0, 40.0, 50.0, 60.0,
    75.0, 90.0, 100.0, 125.0, 150.0, 200.0, 250.0, 500.0, 1000.0,
    5000.0,
};

std::size_t nearestTempoScaleIndex(double value) noexcept
{
    std::size_t best = 3u;
    double distance = std::numeric_limits<double>::infinity();
    for (std::size_t index = 0u; index < kTempoScales.size(); ++index) {
        const double candidate = std::abs(value - kTempoScales[index]);
        if (candidate < distance) { distance = candidate; best = index; }
    }
    return best;
}

std::size_t maximumLoadedPatternRows(const TrackerViewState* state) noexcept
{
    if (!state) return 64u;
    std::size_t rows = std::max<std::size_t>(
        state->session.pattern.visibleRows, 1u);
    for (const auto& entry : state->patternBank.entries)
        rows = std::max(rows, entry.pattern.visibleRows);
    return std::clamp<std::size_t>(rows, 1u, kGridMaximumRows);
}

const std::array<uint32_t, 8u> kLaneColors {
    0x78918cu, 0x9a826cu, 0x817a99u, 0x956f73u,
    0x71889au, 0x87916fu, 0x987b6du, 0x748c7bu,
};

constexpr CGFloat kGeometryPlotTop = 58.0;
constexpr CGFloat kGeometryLegendTop = 60.0;
constexpr CGFloat kGeometryNoteValueWidth = 72.0;

const Pattern* geometryPattern(const TrackerViewState* state)
{
    return playbackFollowPattern(state);
}

std::string geometryPatternId(const TrackerViewState* state)
{
    return playbackFollowPatternId(state);
}

struct GeometryLaneSet {
    std::array<std::size_t, s3g::tracker::kMaximumTrackCount> indices {};
    std::size_t count = 0u;
};

GeometryLaneSet geometryLanes(const Pattern* pattern)
{
    GeometryLaneSet result;
    if (!pattern) return result;
    const auto lanes = std::min<std::size_t>(
        s3g::tracker::kMaximumTrackCount,
        pattern->tracks.size());
    for (std::size_t lane = 0u; lane < lanes; ++lane)
        result.indices[result.count++] = lane;
    return result;
}

bool geometryLaneMuted(const TrackerViewState* state, const Pattern* pattern,
    std::size_t lane)
{
    if (!pattern || lane >= pattern->tracks.size()) return true;
    if (pattern->tracks[lane].noteColumn.muted) return true;
    return state && state->songPlaybackActive && lane < 32u
        && (state->songPlaybackMutedTracks
            & (uint32_t { 1u } << lane)) != 0u;
}

GeometryLaneSet visibleGeometryLanes(const Pattern* pattern)
{
    GeometryLaneSet result;
    const auto lanes = geometryLanes(pattern);
    for (std::size_t ordinal = 0u; ordinal < lanes.count; ++ordinal) {
        const auto lane = lanes.indices[ordinal];
        if (!pattern->tracks[lane].noteColumn.muted)
            result.indices[result.count++] = lane;
    }
    return result;
}

GeometryLaneSet visibleGeometryLanes(const TrackerViewState* state)
{
    GeometryLaneSet result;
    const auto* pattern = geometryPattern(state);
    const auto lanes = geometryLanes(pattern);
    for (std::size_t ordinal = 0u; ordinal < lanes.count; ++ordinal) {
        const auto lane = lanes.indices[ordinal];
        if (!geometryLaneMuted(state, pattern, lane))
            result.indices[result.count++] = lane;
    }
    return result;
}

bool viewCanPresentPlayback(NSView* view)
{
    return view && view.window && view.window.visible
        && ![view isHiddenOrHasHiddenAncestor] && !view.window.miniaturized;
}

template <typename Cell>
void insertPatternRowCell(std::vector<Cell>& cells, std::size_t row,
    std::size_t oldRows, std::size_t count, const Cell& blank)
{
    cells.resize(oldRows, blank);
    cells.insert(cells.begin() + static_cast<std::ptrdiff_t>(row),
        count, blank);
}

template <typename Cell>
void deletePatternRowCell(std::vector<Cell>& cells, std::size_t row,
    std::size_t oldRows, std::size_t count, const Cell& blank)
{
    cells.resize(oldRows, blank);
    const auto first = cells.begin() + static_cast<std::ptrdiff_t>(row);
    cells.erase(first, first + static_cast<std::ptrdiff_t>(count));
}

void insertPatternColumnRows(ColumnDefinition& column, std::size_t row,
    std::size_t count)
{
    if (row >= column.length || column.length >= kGridMaximumRows) return;
    if (column.phase >= row) column.phase += count;
    column.length = std::min(kGridMaximumRows, column.length + count);
    column.phase %= column.length;
}

void deletePatternColumnRows(ColumnDefinition& column, std::size_t row,
    std::size_t count)
{
    if (row >= column.length) return;
    const std::size_t removed = std::min(count, column.length - row);
    if (column.phase >= row + removed) column.phase -= removed;
    else if (column.phase >= row) column.phase = row;
    column.length = std::max<std::size_t>(1u, column.length - removed);
    column.phase %= column.length;
}

} // namespace

@class S3GTrackerGridView;
@class S3GTrackerGeometryView;
@class S3GTrackerRowGutterView;

@interface S3GTrackerGridScrollView : NSScrollView
@property(nonatomic, weak) S3GTrackerRowGutterView* frozenRowGutter;
@end

@interface S3GTrackerRowGutterView : NSView
@property(nonatomic, weak) NSScrollView* scrollView;
@property(nonatomic, weak) S3GTrackerGridView* gridView;
@property(nonatomic) BOOL selectingRowRange;
- (instancetype)initWithScrollView:(NSScrollView*)scrollView
    gridView:(S3GTrackerGridView*)gridView;
- (void)refreshFrameAndDisplay;
- (NSRect)pinnedRectForGridRect:(NSRect)gridRect;
@end

typedef NS_ENUM(NSInteger, S3GTrackerGeometryViewMode) {
    S3GTrackerGeometryViewModeRingField = 0,
    S3GTrackerGeometryViewModeActivePulses,
    S3GTrackerGeometryViewModeAllStepsUnderlay,
    S3GTrackerGeometryViewModePhaseSpokes,
    S3GTrackerGeometryViewModeLaneFocus,
    S3GTrackerGeometryViewModeCompositeRing,
    S3GTrackerGeometryViewModeBurst,
    S3GTrackerGeometryViewModePitchMap,
};

typedef NS_ENUM(NSInteger, S3GTrackerGeometryTool) {
    S3GTrackerGeometryToolSelect = 0,
    S3GTrackerGeometryToolPaint,
    S3GTrackerGeometryToolErase,
    S3GTrackerGeometryToolVelocity,
};

typedef NS_ENUM(NSInteger, S3GTrackerGeometryGestureKind) {
    S3GTrackerGeometryGestureNone = 0,
    S3GTrackerGeometryGesturePaint,
    S3GTrackerGeometryGestureErase,
    S3GTrackerGeometryGestureVelocity,
    S3GTrackerGeometryGestureDefaultNote,
    S3GTrackerGeometryGestureLength,
    S3GTrackerGeometryGestureRotate,
    S3GTrackerGeometryGestureDensity,
    S3GTrackerGeometryGestureBurstPosition,
    S3GTrackerGeometryGestureBurstNote,
    S3GTrackerGeometryGestureBurstVelocity,
    S3GTrackerGeometryGestureBurstGate,
    S3GTrackerGeometryGestureBurstMatrixPosition,
    S3GTrackerGeometryGestureBurstMatrixNote,
    S3GTrackerGeometryGestureBurstMatrixVelocity,
    S3GTrackerGeometryGestureBurstMatrixGate,
    S3GTrackerGeometryGestureBurstVelocityPoint,
    S3GTrackerGeometryGesturePitchMinimum,
    S3GTrackerGeometryGesturePitchMaximum,
    S3GTrackerGeometryGesturePitchVariation,
    S3GTrackerGeometryGesturePitchTranspose,
    S3GTrackerGeometryGesturePitchPoint,
};

typedef NS_ENUM(NSInteger, S3GTrackerGeometryMenu) {
    S3GTrackerGeometryMenuNone = 0,
    S3GTrackerGeometryMenuLane,
    S3GTrackerGeometryMenuDirection,
    S3GTrackerGeometryMenuView,
    S3GTrackerGeometryMenuMorphTarget,
    S3GTrackerGeometryMenuBurstSlot,
    S3GTrackerGeometryMenuBurstEvent,
    S3GTrackerGeometryMenuPitchScope,
    S3GTrackerGeometryMenuPitchRoot,
    S3GTrackerGeometryMenuPitchScale,
    S3GTrackerGeometryMenuPitchContour,
    S3GTrackerGeometryMenuPitchLeap,
};
@class S3GTrackerGeometryWindowController;
@class S3GTrackerEnvelopeView;
@class S3GTrackerInstrumentToolboxView;

// Normal clicks latch FILL. Shift-hold supplies a performance momentary mode
// without adding another transport control or changing the suite button look.
@interface S3GTrackerFillButton : S3GTrackerActionButton
@end

@implementation S3GTrackerFillButton

- (instancetype)initWithFrame:(NSRect)frameRect
{
    self = [super initWithFrame:frameRect];
    if (self) self.buttonType = NSButtonTypePushOnPushOff;
    return self;
}

- (void)mouseDown:(NSEvent*)event
{
    if ((event.modifierFlags & NSEventModifierFlagShift) == 0u) {
        [super mouseDown:event];
        return;
    }
    const NSControlStateValue previous = self.state;
    self.state = NSControlStateValueOn;
    self.highlighted = YES;
    [self setNeedsDisplay:YES];
    [NSApp sendAction:self.action to:self.target from:self];
    for (;;) {
        NSEvent* next = [self.window nextEventMatchingMask:
            (NSEventMaskLeftMouseUp | NSEventMaskLeftMouseDragged)
            untilDate:NSDate.distantFuture
            inMode:NSEventTrackingRunLoopMode dequeue:YES];
        if (!next || next.type == NSEventTypeLeftMouseUp) break;
        const NSPoint point = [self convertPoint:next.locationInWindow
            fromView:nil];
        self.highlighted = NSPointInRect(point, self.bounds);
        [self setNeedsDisplay:YES];
    }
    self.highlighted = NO;
    self.state = previous;
    [self setNeedsDisplay:YES];
    [NSApp sendAction:self.action to:self.target from:self];
}

@end

@interface S3GTrackerWorkspaceController () <NSTextFieldDelegate>
@property(nonatomic, assign) TrackerViewState* trackerState;
@property(nonatomic, assign) WorkspaceCallbacks* trackerCallbacks;
@property(nonatomic, strong) NSView* toolbar;
@property(nonatomic, strong) NSScrollView* transportScroll;
@property(nonatomic, strong) NSStackView* transportControls;
@property(nonatomic, strong) S3GTrackerToolboxView* patternPanel;
@property(nonatomic, strong) S3GTrackerToolboxView* transportPanel;
@property(nonatomic, strong) S3GTrackerToolboxView* inputViewPanel;
@property(nonatomic, strong) NSStackView* patternPrimaryControls;
@property(nonatomic, strong) NSStackView* transportPrimaryControls;
@property(nonatomic, strong) NSStackView* inputPrimaryControls;
@property(nonatomic, strong) S3GTrackerGridScrollView* gridScroll;
@property(nonatomic, strong) S3GTrackerGridView* gridView;
@property(nonatomic, strong) S3GTrackerRowGutterView* rowGutterView;
@property(nonatomic, strong) S3GTrackerGeometryView* geometryView;
@property(nonatomic, strong) S3GTrackerGeometryView* burstView;
@property(nonatomic, strong) S3GTrackerGeometryWindowController*
    geometryWindowController;
@property(nonatomic, strong) S3GTrackerReshapeWindowController*
    reshapeWindowController;
@property(nonatomic, strong) S3GTrackerWarpWindowController*
    warpWindowController;
@property(nonatomic, strong) S3GTrackerPhraseView* phraseView;
@property(nonatomic, strong) S3GTrackerEnvelopeView* envelopeView;
@property(nonatomic, strong) NSView* consolePanel;
@property(nonatomic, strong) NSView* consolePageRoot;
@property(nonatomic, strong) S3GTrackerToolboxView* consoleOutputPanel;
@property(nonatomic, strong) NSTextView* consoleOutput;
@property(nonatomic, strong) NSTextField* consoleInput;
@property(nonatomic, strong) NSTextField* consolePageInput;
@property(nonatomic, strong) NSMutableArray<NSString*>* consoleHistory;
@property(nonatomic) NSInteger consoleHistoryIndex;
@property(nonatomic, copy) NSString* consoleDraft;
@property(nonatomic, strong) NSButton* playButton;
@property(nonatomic, strong) NSButton* loopButton;
@property(nonatomic, strong) NSButton* fillButton;
@property(nonatomic, strong) NSButton* restartButton;
@property(nonatomic, strong) NSButton* sequenceColumnsButton;
@property(nonatomic, strong) NSButton* trackAddButton;
@property(nonatomic, strong) NSButton* trackRemoveButton;
@property(nonatomic, strong) NSButton* undoButton;
@property(nonatomic, strong) NSButton* redoButton;
@property(nonatomic, strong) NSButton* noteDisplayButton;
@property(nonatomic, strong) S3GTrackerPopupButton* stepJumpPopup;
@property(nonatomic, strong) NSButton* zoomOutButton;
@property(nonatomic, strong) NSButton* zoomActualButton;
@property(nonatomic, strong) NSButton* zoomInButton;
@property(nonatomic, strong) S3GTrackerPopupButton* midiStepRecordPopup;
@property(nonatomic, strong) S3GTrackerPopupButton* midiRecordTrackPopup;
@property(nonatomic, strong) S3GTrackerPopupButton* patternPopup;
@property(nonatomic, strong) NSButton* createPatternButton;
@property(nonatomic, strong) NSButton* duplicatePatternButton;
@property(nonatomic, strong) NSButton* renamePatternButton;
@property(nonatomic, strong) NSButton* deletePatternButton;
@property(nonatomic, strong) S3GTrackerPopupButton* tempoScalePopup;
@property(nonatomic, strong) S3GTrackerSwingSlider* swingField;
@property(nonatomic, strong) S3GTrackerPopupButton* gateField;
@property(nonatomic, strong) S3GTrackerPopupButton* loopStartField;
@property(nonatomic, strong) S3GTrackerPopupButton* loopEndField;
@property(nonatomic, strong) NSPopUpButton* audioPopup;
@property(nonatomic, strong) NSLayoutConstraint* envelopeHeightConstraint;
- (void)modulePatternChanged;
- (void)moduleTransportChanged;
- (void)moduleSelectionChanged;
- (void)moduleTogglePlayback;
- (void)refreshPlaybackFollowControls;
- (void)undoPressed:(id)sender;
- (void)redoPressed:(id)sender;
- (void)toggleSequenceColumns:(id)sender;
- (void)toggleNoteDisplay:(id)sender;
- (void)stepJumpChanged:(id)sender;
- (void)midiStepRecordModeChanged:(id)sender;
- (void)midiRecordTrackChanged:(id)sender;
- (void)refreshMidiRecordTrackMenu;
- (void)zoomOutPressed:(id)sender;
- (void)zoomActualPressed:(id)sender;
- (void)zoomInPressed:(id)sender;
- (void)moduleFocusConsole;
- (void)tempoScaleChanged:(id)sender;
- (void)loopPressed:(id)sender;
- (void)fillPressed:(id)sender;
- (void)applyWorkspaceMode;
- (void)assignTrackInstrument:(uint32_t)nodeId;
- (void)editRackInstrument:(uint32_t)nodeId;
- (void)addInstrumentKind:(s3g::tracker::InstrumentKind)kind;
- (void)zoomTrackerIn;
- (void)zoomTrackerOut;
- (void)resetTrackerZoom;
- (void)editBurstSlot:(std::size_t)slot;
- (void)openPitchMapFirstRow:(std::size_t)firstRow
    lastRow:(std::size_t)lastRow;
- (void)applyPitchMapContour:(PitchContour)contour
    firstRow:(std::size_t)firstRow lastRow:(std::size_t)lastRow;
- (void)capturePhraseTrack:(std::size_t)track firstRow:(std::size_t)firstRow
    lastRow:(std::size_t)lastRow;
- (void)placeSelectedPhraseTrack:(std::size_t)track row:(std::size_t)row
    merge:(BOOL)merge;
@end

@interface S3GTrackerGridView : NSView <NSTextFieldDelegate> {
    s3g::tracker::app::GridSelection _gridSelection;
    std::array<std::array<std::size_t, 7u>,
        s3g::tracker::kMaximumTrackCount> _presentedPlayheads;
    BOOL _playbackPresentationPrimed;
    BOOL _presentedPlaying;
    std::string _presentedPatternId;
    uint32_t _presentedSongMuteMask;
    s3g::tracker::Pattern _rowClipboard;
    BOOL _hasRowClipboard;
    std::vector<uint8_t> _copiedColumnTypes;
    std::vector<TrackerGridCell> _copiedGridCells;
}
- (instancetype)initWithState:(TrackerViewState*)state
    owner:(S3GTrackerWorkspaceController*)owner;
- (void)scrollSelectionToVisible;
- (void)clearGridSelection;
- (BOOL)clearSelectedGridCells;
- (void)refreshAccessibilityValue;
- (void)refreshPlaybackDisplay;
- (NSString*)displayedPatternId;
- (NSUInteger)displayedLaneCount;
- (NSUInteger)displayedVisibleRowCount;
- (NSInteger)displayedNoteNumberAtLane:(std::size_t)lane
    row:(std::size_t)row;
- (void)beginLoopSelectionAtRow:(NSInteger)row;
- (void)continueLoopSelectionAtRow:(NSInteger)row;
- (void)finishLoopSelection;
- (void)selectWholeRowsFrom:(std::size_t)anchor to:(std::size_t)focus;
- (void)insertPatternRows:(NSMenuItem*)sender;
- (void)insertPatternRowsBelow:(NSMenuItem*)sender;
- (void)deletePatternRows:(NSMenuItem*)sender;
- (void)copyPatternRows:(NSMenuItem*)sender;
- (void)pastePatternRows:(NSMenuItem*)sender;
- (void)quantizePatternRows:(NSMenuItem*)sender;
- (void)humanizePatternRows:(NSMenuItem*)sender;
- (void)reversePatternRows:(NSMenuItem*)sender;
- (void)rotatePatternRows:(NSMenuItem*)sender;
- (void)thinPatternRows:(NSMenuItem*)sender;
- (void)densityPatternRows:(NSMenuItem*)sender;
- (void)transposeSelectedNoteRows:(NSMenuItem*)sender;
- (void)scaleSelectedVelocityRows:(NSMenuItem*)sender;
- (void)randomizeSelectedVelocityRows:(NSMenuItem*)sender;
- (void)fitScaleSelectedNoteRows:(NSMenuItem*)sender;
- (void)generateScaleSelectedNoteRows:(NSMenuItem*)sender;
- (void)openPitchMapSelectedNoteRows:(NSMenuItem*)sender;
- (BOOL)isWholeRowSelected:(std::size_t)row;
- (NSDictionary*)rowActionPayloadForRow:(std::size_t)row;
- (std::size_t)wholeRowSelectionAnchor;
- (BOOL)hasRowClipboard;
- (NSMenu*)sequenceConditionMenuForTrack:(std::size_t)track
    row:(std::size_t)row field:(std::size_t)field;
- (NSMenu*)noteMenuForTrack:(std::size_t)track row:(std::size_t)row;
- (NSMenu*)velocityMenuForTrack:(std::size_t)track row:(std::size_t)row;
- (NSDictionary*)columnActionPayloadForTrack:(std::size_t)track
    field:(std::size_t)field row:(std::size_t)row;
- (void)appendSelectionMenuTo:(NSMenu*)menu;
- (void)fillSelectionFromEdge:(NSMenuItem*)sender;
- (void)fillSelectionSeries:(NSMenuItem*)sender;
- (void)repeatGridSelection:(NSMenuItem*)sender;
- (void)shiftSelectionCells:(NSMenuItem*)sender;
- (void)moveGridSelection:(NSMenuItem*)sender;
- (void)stretchGridSelection:(NSMenuItem*)sender;
- (void)materializeGridSelection:(NSMenuItem*)sender;
- (void)findReplaceGridSelection:(NSMenuItem*)sender;
- (void)swapGridSelectionWithNextLane:(NSMenuItem*)sender;
- (void)showGridSelectionStatistics:(NSMenuItem*)sender;
- (void)splitSelectedNoteColumnByPitch:(NSMenuItem*)sender;
- (void)mergeSelectedNoteLanes:(NSMenuItem*)sender;
- (void)pasteGridSelectionSpecial:(NSMenuItem*)sender;
- (void)captureGridSelectionAsPhrase:(NSMenuItem*)sender;
- (void)placePhraseAtSelection:(NSMenuItem*)sender;
- (void)placePhraseSlotAtSelection:(NSMenuItem*)sender;
- (void)reverseGridSelection:(NSMenuItem*)sender;
- (void)rotateGridSelection:(NSMenuItem*)sender;
- (void)adjustSelectedValues:(NSMenuItem*)sender;
- (void)scaleSelectedValues:(NSMenuItem*)sender;
- (void)quantizeSelectedMicroTime:(NSMenuItem*)sender;
@property(nonatomic, assign) TrackerViewState* trackerState;
@property(nonatomic, weak) S3GTrackerWorkspaceController* owner;
@property(nonatomic, strong) NSTextField* cellEditor;
@property(nonatomic) NSRect cellEditorCellRect;
@property(nonatomic) std::size_t editingTrack;
@property(nonatomic) std::size_t editingRow;
@property(nonatomic) std::size_t editingPage;
@property(nonatomic) std::size_t editingField;
@property(nonatomic) BOOL editingColumnLength;
@property(nonatomic) BOOL editingColumnReadStart;
@property(nonatomic) BOOL editingTrackName;
@property(nonatomic) NSInteger loopAnchorRow;
@property(nonatomic) BOOL selectingLoopRows;
@property(nonatomic) BOOL selectingGridCells;
@property(nonatomic) BOOL selectingWholeRows;
@property(nonatomic) BOOL numericDragCandidate;
@property(nonatomic) BOOL draggingNumericCell;
@property(nonatomic) BOOL numericDragChanged;
@property(nonatomic) NSPoint numericDragOrigin;
@property(nonatomic) float numericDragStartValue;
@property(nonatomic) std::size_t numericDragTrack;
@property(nonatomic) std::size_t numericDragRow;
@property(nonatomic) std::size_t numericDragField;
@property(nonatomic) NSInteger copiedPasteboardChangeCount;
@property(nonatomic, copy) NSString* copiedClipboardText;
@property(nonatomic) std::size_t copiedTrackCount;
@property(nonatomic) std::size_t copiedFieldCount;
@property(nonatomic) std::size_t copiedRowCount;
- (void)beginCellEditingWithInitialText:(NSString*)initialText;
- (void)beginColumnLengthEditingForTrack:(std::size_t)track
    page:(std::size_t)page field:(std::size_t)field rect:(NSRect)rect;
- (void)beginColumnReadStartEditingForTrack:(std::size_t)track
    page:(std::size_t)page field:(std::size_t)field rect:(NSRect)rect;
- (void)beginTrackNameEditingForTrack:(std::size_t)track rect:(NSRect)rect;
- (void)showMidiChannelMenuForTrack:(std::size_t)track event:(NSEvent*)event;
- (NSMenu*)sequenceActionMenuForTrack:(std::size_t)track
    row:(std::size_t)row field:(std::size_t)field;
- (void)sequenceActionSelected:(NSMenuItem*)sender;
- (NSMenu*)burstMenuForTrack:(std::size_t)track row:(std::size_t)row;
- (void)burstMenuSelected:(NSMenuItem*)sender;
@end

@implementation S3GTrackerGridView

- (instancetype)initWithState:(TrackerViewState*)state
    owner:(S3GTrackerWorkspaceController*)owner
{
    self = [super initWithFrame:NSMakeRect(0.0, 0.0, 900.0, 460.0)];
    if (self) {
        self.trackerState = state;
        self.owner = owner;
        self.loopAnchorRow = -1;
        self.copiedPasteboardChangeCount = -1;
        self.accessibilityElement = YES;
        self.accessibilityRole = NSAccessibilityGroupRole;
        self.accessibilityLabel = @"Editable tracker lanes";
        self.accessibilityHelp = @"Compact lanes show NOTE and VOL. During Song playback Tracker follows the sounding pattern and becomes read-only, then returns to the editor pattern when playback stops. Use Expand Detail to reveal SEQ1, V1, SEQ2, V2, and GATE without changing their data. NOTE NAME and NOTE MIDI switch the same stored pitches between names and decimal MIDI values. Each lane header has a SYNC control that restarts that track's NOTE, VOL, sequencing, and gate loops together, plus its own clickable MIDI channel from 1 through 16. Double-click the lane name to rename it. Each visible column header has separate label, length and stride, read start, direction, and MUTE rows. Double-click length to enter forms such as 24x2, or double-click READ to set its one-based starting row. Click DIR to cycle direction or MUTE to toggle that column. Left and right move across visible fields; up and down move by the View toolbox JUMP value. Shift-left and Shift-right move between lanes. Right-click SEQ1 or SEQ2 to choose a sequencing action or MIDI CC, or double-click and type its code. Drag VOL, V1, or V2 vertically to adjust it; Control-drag selects cells instead. GATE accepts DEF, a duration in rows from 0.01 to 64, TIE, or pitch-aligned stacks such as 0.5+TIE+1.25. Drag the row gutter or use Shift-up and Shift-down to select the global loop. Shift-click row numbers to select complete rows, then right-click a selected number for structural edits, MT quantize, humanize, and pattern-wide rhythm transforms. Right-click any selected cell range for Fill, Repeat, cell insert/delete/move, typed Paste Special, stretch/compress, materialize, find/replace, lane swap, and selection statistics. A single selected NOTE column can separate mixed MIDI pitches into routed lanes while preserving matching velocity, per-note gate, and MT voices; parameter and MIDI CC automation stay on the source lane. Select from one lane's NOTE column through another lane's NOTE column to merge pitches, velocities, gates, and per-note MT into polyphonic cells in the leftmost lane. NOTE accepts one pitch or a plus-separated stack such as 60+64+67, plus RPT, HLD, or KIL. VOL accepts one broadcast value or a matching stack such as 0.866+0.646+0.756; shorter stacks repeat their final value. An MT value accepts the same kind of aligned stack, such as 0.200+0.500+0.800; other SEQ actions remain lane-wide. H writes HLD directly. Sequence values accept 0.000 through 1.000; CC value pairs also accept MIDI integers 0 through 127. Delete clears every cell in a drag selection. Control-A, C, X, and V select all, copy, cut, and paste visible tracker cells. Control-Z and Control-Shift-Z undo and redo Tracker edits; Command shortcuts remain available to REAPER.";
    }
    return self;
}

- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }

- (NSString*)displayedPatternId
{
    return nsString(playbackFollowPatternId(self.trackerState));
}

- (NSUInteger)displayedLaneCount
{
    const auto* pattern = playbackFollowPattern(self.trackerState);
    return pattern ? static_cast<NSUInteger>(std::min<std::size_t>(
        s3g::tracker::kMaximumTrackCount, pattern->tracks.size())) : 0u;
}

- (NSUInteger)displayedVisibleRowCount
{
    return static_cast<NSUInteger>(playbackFollowVisibleRows(
        self.trackerState));
}

- (NSInteger)displayedNoteNumberAtLane:(std::size_t)lane
    row:(std::size_t)row
{
    const auto* pattern = playbackFollowPattern(self.trackerState);
    if (!pattern || lane >= pattern->tracks.size()) return -1;
    const auto& notes = pattern->tracks[lane].notes;
    if (row >= notes.size()
        || notes[row].state != NoteCellState::Note) return -1;
    return static_cast<NSInteger>(notes[row].note);
}

- (void)clearGridSelection
{
    _gridSelection.active = false;
    self.selectingWholeRows = NO;
    [self setNeedsDisplay:YES];
}

- (void)selectWholeRowsFrom:(std::size_t)anchor to:(std::size_t)focus
{
    auto* model = self.trackerState;
    if (!model || model->session.pattern.tracks.empty()) return;
    const std::size_t rows = visibleRows(model);
    anchor = std::min(anchor, rows - 1u);
    focus = std::min(focus, rows - 1u);
    _gridSelection.page = 0u;
    _gridSelection.anchorTrack = 0u;
    _gridSelection.focusTrack = model->session.pattern.tracks.size() - 1u;
    _gridSelection.anchorField = 0u;
    _gridSelection.focusField = gridFieldCount(
        model->sequenceColumnsExpanded) - 1u;
    _gridSelection.anchorRow = anchor;
    _gridSelection.focusRow = focus;
    _gridSelection.active = true;
    self.selectingWholeRows = YES;
    model->session.selectedRow = focus;
    [self.owner moduleSelectionChanged];
    [self setNeedsDisplay:YES];
}

- (BOOL)isWholeRowSelected:(std::size_t)row
{
    if (!self.selectingWholeRows || !_gridSelection.active) return NO;
    const auto range = _gridSelection.range();
    return row >= range.firstRow && row <= range.lastRow;
}

- (std::size_t)wholeRowSelectionAnchor
{
    return self.selectingWholeRows ? _gridSelection.anchorRow
                                   : self.trackerState->session.selectedRow;
}

- (BOOL)hasRowClipboard { return _hasRowClipboard; }

- (NSDictionary*)rowActionPayloadForRow:(std::size_t)row
{
    auto* model = self.trackerState;
    if (!model) return @{ @"row": @0, @"count": @0 };
    row = std::min(row, visibleRows(model) - 1u);
    if (self.selectingWholeRows && _gridSelection.active) {
        const auto range = _gridSelection.range();
        if (row >= range.firstRow && row <= range.lastRow) {
            return @{
                @"row": @(range.firstRow),
                @"count": @(range.lastRow - range.firstRow + 1u),
            };
        }
    }
    [self selectWholeRowsFrom:row to:row];
    return @{ @"row": @(row), @"count": @1 };
}

- (NSDictionary*)rowActionPayload:(NSMenuItem*)sender
{
    return [sender.representedObject isKindOfClass:NSDictionary.class]
        ? sender.representedObject : @{};
}

- (void)insertRowsAt:(std::size_t)row count:(std::size_t)requested
    pasteClipboard:(BOOL)pasteClipboard
{
    auto* model = self.trackerState;
    if (!model || model->songPlaybackActive || requested == 0u) return;
    auto& pattern = model->session.pattern;
    const std::size_t oldRows = std::clamp<std::size_t>(
        pattern.visibleRows, 1u, kGridMaximumRows);
    if (oldRows >= kGridMaximumRows) return;
    row = std::min(row, oldRows);
    const std::size_t count = std::min(requested,
        kGridMaximumRows - oldRows);
    for (auto& track : pattern.tracks) {
        insertPatternRowCell(track.notes, row, oldRows, count,
            NoteCell::rest());
        insertPatternRowCell(track.instruments, row, oldRows, count,
            InstrumentCell::empty());
        insertPatternRowCell(track.velocities, row, oldRows, count,
            ValueCell::defaultValue());
        insertPatternRowCell(track.gates, row, oldRows, count,
            GateCell::defaultValue());
        insertPatternColumnRows(track.noteColumn, row, count);
        insertPatternColumnRows(track.instrumentColumn, row, count);
        insertPatternColumnRows(track.velocityColumn, row, count);
        insertPatternColumnRows(track.gateColumn, row, count);
        for (auto& pair : track.fxPairs) {
            insertPatternRowCell(pair.actions, row, oldRows, count,
                FxActionCell::empty());
            insertPatternRowCell(pair.values, row, oldRows, count,
                FxValueCell::previous());
            insertPatternColumnRows(pair.actionColumn, row, count);
            insertPatternColumnRows(pair.valueColumn, row, count);
        }
    }
    if (pasteClipboard && _hasRowClipboard) {
        const std::size_t lanes = std::min(
            pattern.tracks.size(), _rowClipboard.tracks.size());
        for (std::size_t lane = 0u; lane < lanes; ++lane) {
            auto& destination = pattern.tracks[lane];
            const auto& source = _rowClipboard.tracks[lane];
            const auto paste = [row, count](auto& into, const auto& from) {
                const std::size_t cells = std::min(count, from.size());
                for (std::size_t index = 0u; index < cells; ++index)
                    into[row + index] = from[index];
            };
            paste(destination.notes, source.notes);
            paste(destination.instruments, source.instruments);
            paste(destination.velocities, source.velocities);
            paste(destination.gates, source.gates);
            for (std::size_t pair = 0u;
                 pair < destination.fxPairs.size(); ++pair) {
                paste(destination.fxPairs[pair].actions,
                    source.fxPairs[pair].actions);
                paste(destination.fxPairs[pair].values,
                    source.fxPairs[pair].values);
            }
        }
    }
    pattern.visibleRows = oldRows + count;
    auto& transport = model->session.transport;
    if (transport.loopStartRow >= row) transport.loopStartRow += count;
    if (transport.loopEndRow > row) transport.loopEndRow += count;
    model->session.selectedRow = row;
    [self selectWholeRowsFrom:row to:row + count - 1u];
    [self.owner modulePatternChanged];
}

- (void)insertPatternRows:(NSMenuItem*)sender
{
    NSDictionary* payload = [self rowActionPayload:sender];
    [self insertRowsAt:[payload[@"row"] unsignedIntegerValue]
        count:[payload[@"count"] unsignedIntegerValue]
        pasteClipboard:NO];
}

- (void)insertPatternRowsBelow:(NSMenuItem*)sender
{
    NSDictionary* payload = [self rowActionPayload:sender];
    const std::size_t row = [payload[@"row"] unsignedIntegerValue];
    const std::size_t count = [payload[@"count"] unsignedIntegerValue];
    [self insertRowsAt:row + count count:count pasteClipboard:NO];
}

- (void)deletePatternRows:(NSMenuItem*)sender
{
    auto* model = self.trackerState;
    if (!model || model->songPlaybackActive) return;
    auto& pattern = model->session.pattern;
    const std::size_t oldRows = std::clamp<std::size_t>(
        pattern.visibleRows, 1u, kGridMaximumRows);
    if (oldRows <= 16u) return;
    NSDictionary* payload = [self rowActionPayload:sender];
    const std::size_t row = std::min<std::size_t>(
        [payload[@"row"] unsignedIntegerValue], oldRows - 1u);
    const std::size_t count = std::min<std::size_t>({
        [payload[@"count"] unsignedIntegerValue], oldRows - row,
        oldRows - 16u });
    if (count == 0u) return;
    for (auto& track : pattern.tracks) {
        deletePatternRowCell(track.notes, row, oldRows, count,
            NoteCell::rest());
        deletePatternRowCell(track.instruments, row, oldRows, count,
            InstrumentCell::empty());
        deletePatternRowCell(track.velocities, row, oldRows, count,
            ValueCell::defaultValue());
        deletePatternRowCell(track.gates, row, oldRows, count,
            GateCell::defaultValue());
        deletePatternColumnRows(track.noteColumn, row, count);
        deletePatternColumnRows(track.instrumentColumn, row, count);
        deletePatternColumnRows(track.velocityColumn, row, count);
        deletePatternColumnRows(track.gateColumn, row, count);
        for (auto& pair : track.fxPairs) {
            deletePatternRowCell(pair.actions, row, oldRows, count,
                FxActionCell::empty());
            deletePatternRowCell(pair.values, row, oldRows, count,
                FxValueCell::previous());
            deletePatternColumnRows(pair.actionColumn, row, count);
            deletePatternColumnRows(pair.valueColumn, row, count);
        }
    }
    pattern.visibleRows = oldRows - count;
    auto& transport = model->session.transport;
    if (transport.loopStartRow >= row + count)
        transport.loopStartRow -= count;
    else if (transport.loopStartRow >= row)
        transport.loopStartRow = static_cast<uint32_t>(row);
    if (transport.loopEndRow >= row + count)
        transport.loopEndRow -= count;
    else if (transport.loopEndRow > row)
        transport.loopEndRow = static_cast<uint32_t>(row);
    transport.loopStartRow = std::min<uint32_t>(transport.loopStartRow,
        static_cast<uint32_t>(pattern.visibleRows - 1u));
    transport.loopEndRow = std::clamp<uint32_t>(transport.loopEndRow,
        transport.loopStartRow + 1u,
        static_cast<uint32_t>(pattern.visibleRows));
    model->session.selectedRow = std::min(row, pattern.visibleRows - 1u);
    [self clearGridSelection];
    [self.owner modulePatternChanged];
}

- (void)copyPatternRows:(NSMenuItem*)sender
{
    auto* model = self.trackerState;
    if (!model) return;
    NSDictionary* payload = [self rowActionPayload:sender];
    const std::size_t rows = std::max<std::size_t>(
        model->session.pattern.visibleRows, 1u);
    const std::size_t row = std::min<std::size_t>(
        [payload[@"row"] unsignedIntegerValue], rows - 1u);
    const std::size_t count = std::min<std::size_t>(
        [payload[@"count"] unsignedIntegerValue], rows - row);
    if (count == 0u) return;
    _rowClipboard = model->session.pattern;
    _rowClipboard.visibleRows = count;
    const auto copyRange = [row, count, rows](auto& cells, const auto& blank) {
        cells.resize(rows, blank);
        std::vector<std::decay_t<decltype(cells.front())>> copied(
            cells.begin() + static_cast<std::ptrdiff_t>(row),
            cells.begin() + static_cast<std::ptrdiff_t>(row + count));
        cells = std::move(copied);
    };
    for (auto& track : _rowClipboard.tracks) {
        copyRange(track.notes, NoteCell::rest());
        copyRange(track.instruments, InstrumentCell::empty());
        copyRange(track.velocities, ValueCell::defaultValue());
        copyRange(track.gates, GateCell::defaultValue());
        for (auto& pair : track.fxPairs) {
            copyRange(pair.actions, FxActionCell::empty());
            copyRange(pair.values, FxValueCell::previous());
        }
    }
    _hasRowClipboard = YES;
}

- (void)pastePatternRows:(NSMenuItem*)sender
{
    if (!_hasRowClipboard) return;
    NSDictionary* payload = [self rowActionPayload:sender];
    [self insertRowsAt:[payload[@"row"] unsignedIntegerValue]
        count:_rowClipboard.visibleRows pasteClipboard:YES];
}

- (void)quantizePatternRows:(NSMenuItem*)sender
{
    auto* model = self.trackerState;
    if (!model || model->songPlaybackActive) return;
    NSDictionary* payload = [self rowActionPayload:sender];
    const std::size_t row = [payload[@"row"] unsignedIntegerValue];
    const std::size_t count = [payload[@"count"] unsignedIntegerValue];
    if (count == 0u) return;
    if (s3g::tracker::quantizeMicroTimeRows(
            model->session, row, row + count - 1u) > 0u) {
        [self.owner modulePatternChanged];
    }
}

- (void)humanizePatternRows:(NSMenuItem*)sender
{
    auto* model = self.trackerState;
    if (!model || model->songPlaybackActive) return;
    NSDictionary* payload = [self rowActionPayload:sender];
    const std::size_t row = [payload[@"row"] unsignedIntegerValue];
    const std::size_t count = [payload[@"count"] unsignedIntegerValue];
    if (count < 2u) return;
    const double amount = std::clamp(
        static_cast<double>(sender.tag) / 100.0, 0.0, 1.0);
    const uint64_t rngBefore = model->session.commandRngState;
    std::size_t changed = 0u;
    for (std::size_t lane = 0u;
         lane < model->session.pattern.tracks.size(); ++lane) {
        changed += s3g::tracker::humanizeNoteRows(
            model->session, lane, row, row + count - 1u, amount);
    }
    if (changed > 0u || model->session.commandRngState != rngBefore)
        [self.owner modulePatternChanged];
}

- (void)reversePatternRows:(NSMenuItem*)sender
{
    auto* model = self.trackerState;
    if (!model || model->songPlaybackActive) return;
    NSDictionary* payload = [self rowActionPayload:sender];
    const std::size_t row = [payload[@"row"] unsignedIntegerValue];
    const std::size_t count = [payload[@"count"] unsignedIntegerValue];
    if (count < 2u) return;
    std::size_t changed = 0u;
    for (std::size_t lane = 0u;
         lane < model->session.pattern.tracks.size(); ++lane) {
        changed += s3g::tracker::reverseNoteRows(
            model->session, lane, row, row + count - 1u);
    }
    if (changed > 0u) [self.owner modulePatternChanged];
}

- (void)rotatePatternRows:(NSMenuItem*)sender
{
    auto* model = self.trackerState;
    if (!model || model->songPlaybackActive || sender.tag == 0) return;
    NSDictionary* payload = [self rowActionPayload:sender];
    const std::size_t row = [payload[@"row"] unsignedIntegerValue];
    const std::size_t count = [payload[@"count"] unsignedIntegerValue];
    if (count < 2u) return;
    std::size_t changed = 0u;
    for (std::size_t lane = 0u;
         lane < model->session.pattern.tracks.size(); ++lane) {
        changed += s3g::tracker::rotateNoteRows(model->session, lane,
            row, row + count - 1u, static_cast<int64_t>(sender.tag));
    }
    if (changed > 0u) [self.owner modulePatternChanged];
}

- (void)thinPatternRows:(NSMenuItem*)sender
{
    auto* model = self.trackerState;
    if (!model || model->songPlaybackActive) return;
    NSDictionary* payload = [self rowActionPayload:sender];
    const std::size_t row = [payload[@"row"] unsignedIntegerValue];
    const std::size_t count = [payload[@"count"] unsignedIntegerValue];
    if (count == 0u) return;
    const uint64_t rngBefore = model->session.commandRngState;
    std::size_t changed = 0u;
    for (std::size_t lane = 0u;
         lane < model->session.pattern.tracks.size(); ++lane) {
        changed += s3g::tracker::thinNoteRows(model->session, lane,
            row, row + count - 1u,
            static_cast<double>(sender.tag) / 100.0);
    }
    if (changed > 0u || model->session.commandRngState != rngBefore)
        [self.owner modulePatternChanged];
}

- (void)densityPatternRows:(NSMenuItem*)sender
{
    auto* model = self.trackerState;
    if (!model || model->songPlaybackActive) return;
    NSDictionary* payload = [self rowActionPayload:sender];
    const std::size_t row = [payload[@"row"] unsignedIntegerValue];
    const std::size_t count = [payload[@"count"] unsignedIntegerValue];
    if (count == 0u) return;
    const uint64_t rngBefore = model->session.commandRngState;
    std::size_t changed = 0u;
    for (std::size_t lane = 0u;
         lane < model->session.pattern.tracks.size(); ++lane) {
        changed += s3g::tracker::densityNoteRows(model->session, lane,
            row, row + count - 1u,
            static_cast<double>(sender.tag) / 100.0);
    }
    if (changed > 0u || model->session.commandRngState != rngBefore)
        [self.owner modulePatternChanged];
}

- (void)transposeSelectedNoteRows:(NSMenuItem*)sender
{
    auto* model = self.trackerState;
    NSDictionary* payload = [self rowActionPayload:sender];
    if (!model || model->songPlaybackActive || sender.tag == 0
        || ![payload isKindOfClass:NSDictionary.class]) return;
    const auto lane = [payload[@"track"] unsignedIntegerValue];
    const auto row = [payload[@"row"] unsignedIntegerValue];
    const auto count = [payload[@"count"] unsignedIntegerValue];
    const auto field = [payload[@"field"] unsignedIntegerValue];
    if (field != 0u || count == 0u) return;
    if (s3g::tracker::transposeNoteRows(model->session, lane, row,
            row + count - 1u, static_cast<int>(sender.tag)) > 0u)
        [self.owner modulePatternChanged];
}

- (void)scaleSelectedVelocityRows:(NSMenuItem*)sender
{
    auto* model = self.trackerState;
    NSDictionary* payload = [self rowActionPayload:sender];
    if (!model || model->songPlaybackActive || sender.tag <= 0
        || ![payload isKindOfClass:NSDictionary.class]) return;
    const auto lane = [payload[@"track"] unsignedIntegerValue];
    const auto row = [payload[@"row"] unsignedIntegerValue];
    const auto count = [payload[@"count"] unsignedIntegerValue];
    const auto field = [payload[@"field"] unsignedIntegerValue];
    if (field != 1u || count == 0u) return;
    if (s3g::tracker::scaleVelocityRows(model->session, lane, row,
            row + count - 1u,
            static_cast<double>(sender.tag) / 100.0) > 0u)
        [self.owner modulePatternChanged];
}

- (void)randomizeSelectedVelocityRows:(NSMenuItem*)sender
{
    auto* model = self.trackerState;
    NSDictionary* payload = [self rowActionPayload:sender];
    if (!model || model->songPlaybackActive
        || ![payload isKindOfClass:NSDictionary.class]) return;
    const auto lane = [payload[@"track"] unsignedIntegerValue];
    const auto row = [payload[@"row"] unsignedIntegerValue];
    const auto count = [payload[@"count"] unsignedIntegerValue];
    const auto field = [payload[@"field"] unsignedIntegerValue];
    if (field != 1u || count == 0u) return;
    const uint64_t rngBefore = model->session.commandRngState;
    const auto minimum = static_cast<uint8_t>(
        std::clamp<NSInteger>(sender.tag, 0, 127));
    const auto changed = s3g::tracker::randomizeVelocityRows(
        model->session, lane, row, row + count - 1u, minimum, 127u);
    if (changed > 0u || model->session.commandRngState != rngBefore)
        [self.owner modulePatternChanged];
}

- (void)fitScaleSelectedNoteRows:(NSMenuItem*)sender
{
    NSDictionary* payload = [self rowActionPayload:sender];
    if ([payload[@"field"] unsignedIntegerValue] != 0u) return;
    const auto lane = [payload[@"track"] unsignedIntegerValue];
    const auto row = [payload[@"row"] unsignedIntegerValue];
    const auto count = [payload[@"count"] unsignedIntegerValue];
    if (!self.trackerState || count == 0u
        || lane >= self.trackerState->session.pattern.tracks.size()) return;
    self.trackerState->session.selectedTrack = lane;
    [self.owner applyPitchMapContour:PitchContour::Fit
        firstRow:row lastRow:row + count - 1u];
}

- (void)generateScaleSelectedNoteRows:(NSMenuItem*)sender
{
    NSDictionary* payload = [self rowActionPayload:sender];
    if ([payload[@"field"] unsignedIntegerValue] != 0u) return;
    const auto lane = [payload[@"track"] unsignedIntegerValue];
    const auto row = [payload[@"row"] unsignedIntegerValue];
    const auto count = [payload[@"count"] unsignedIntegerValue];
    if (!self.trackerState || count == 0u
        || lane >= self.trackerState->session.pattern.tracks.size()) return;
    self.trackerState->session.selectedTrack = lane;
    [self.owner applyPitchMapContour:PitchContour::VaryExisting
        firstRow:row lastRow:row + count - 1u];
}

- (void)openPitchMapSelectedNoteRows:(NSMenuItem*)sender
{
    NSDictionary* payload = [self rowActionPayload:sender];
    if ([payload[@"field"] unsignedIntegerValue] != 0u) return;
    const auto lane = [payload[@"track"] unsignedIntegerValue];
    const auto row = [payload[@"row"] unsignedIntegerValue];
    const auto count = [payload[@"count"] unsignedIntegerValue];
    if (!self.trackerState || count == 0u
        || lane >= self.trackerState->session.pattern.tracks.size()) return;
    self.trackerState->session.selectedTrack = lane;
    [self.owner openPitchMapFirstRow:row lastRow:row + count - 1u];
}

- (void)beginGridSelectionAtTrack:(std::size_t)track
    field:(std::size_t)field row:(std::size_t)row page:(std::size_t)page
{
    _gridSelection.active = false;
    _gridSelection.page = page;
    _gridSelection.anchorTrack = _gridSelection.focusTrack = track;
    _gridSelection.anchorField = _gridSelection.focusField = field;
    _gridSelection.anchorRow = _gridSelection.focusRow = row;
    self.selectingGridCells = YES;
}

- (void)extendGridSelectionToTrack:(std::size_t)track
    field:(std::size_t)field row:(std::size_t)row
{
    _gridSelection.focusTrack = track;
    _gridSelection.focusField = field;
    _gridSelection.focusRow = row;
    _gridSelection.active = !_gridSelection.isSingleCell();
}

- (BOOL)extendGridSelectionInColumnToTrack:(std::size_t)track
    field:(std::size_t)field row:(std::size_t)row
{
    auto* model = self.trackerState;
    if (!model || model->session.selectedTrack != track
        || model->session.selectedField != field) return NO;
    std::size_t anchor = model->session.selectedRow;
    if (_gridSelection.page == 0u
        && _gridSelection.anchorTrack == track
        && _gridSelection.anchorField == field)
        anchor = _gridSelection.anchorRow;
    [self beginGridSelectionAtTrack:track field:field row:anchor page:0u];
    [self extendGridSelectionToTrack:track field:field row:row];
    model->session.selectedField = field;
    [self selectTrack:track row:row];
    [self setNeedsDisplay:YES];
    return YES;
}

- (s3g::tracker::app::GridSelectionRange)effectiveGridSelection
{
    auto* model = self.trackerState;
    if (_gridSelection.active && model && _gridSelection.page == 0u) {
        const auto candidate = _gridSelection.range();
        if (!model->session.pattern.tracks.empty()
            && candidate.lastTrack < model->session.pattern.tracks.size()
            && candidate.lastField < gridFieldCount(
                model->sequenceColumnsExpanded)
            && candidate.lastRow < visibleRows(model)) return candidate;
        _gridSelection.active = false;
    }
    s3g::tracker::app::GridSelectionRange range;
    if (!model) return range;
    range.page = 0u;
    range.firstTrack = range.lastTrack = model->session.selectedTrack;
    range.firstField = range.lastField = model->session.selectedField;
    range.firstRow = range.lastRow = model->session.selectedRow;
    return range;
}

- (void)refreshAccessibilityValue
{
    auto* model = self.trackerState;
    const auto* pattern = playbackFollowPattern(model);
    if (!model || !pattern || pattern->tracks.empty()) {
        self.accessibilityValue = @"No lanes";
        return;
    }
    const auto& session = model->session;
    const auto lane = std::min(session.selectedTrack,
        pattern->tracks.size() - 1u);
    const auto row = std::min(session.selectedRow,
        playbackFollowVisibleRows(model) - 1u);
    const auto page = 0u;
    const auto field = std::min(session.selectedField,
        gridFieldCount(model->sequenceColumnsExpanded) - 1u);
    const auto& track = pattern->tracks[lane];
    NSString* fieldName = @"Note";
    NSString* fieldValue = @"---";
    if (page == 0u && field == 0u) {
        const NoteCell cell = row < track.notes.size()
            ? track.notes[row] : NoteCell::rest();
        fieldValue = noteText(cell, model->showMidiNoteValues);
    } else if (field == 1u) {
        fieldName = @"Volume";
        fieldValue = volumeText(track, row);
    } else if (gridFieldIsGate(field)) {
        fieldName = @"Gate";
        fieldValue = gateText(track, row, false);
    } else {
        const auto pair = gridSequencePair(field);
        if (gridFieldIsSequenceAction(field)) {
            fieldName = [NSString stringWithFormat:@"Sequence %lu action",
                static_cast<unsigned long>(pair + 1u)];
            fieldValue = fxActionText(track, pair, row);
        } else {
            fieldName = [NSString stringWithFormat:@"Sequence %lu value",
                static_cast<unsigned long>(pair + 1u)];
            fieldValue = fxValueText(track, pair, row);
            fieldValue = [NSString stringWithFormat:@"%@, %@ interpolation",
                fieldValue,
                track.fxPairs[pair].valueInterpolation
                        == ValueInterpolation::Linear
                    ? @"linear" : @"step"];
        }
    }
    self.accessibilityValue = [NSString stringWithFormat:
        @"%@, lane %lu, row %lu, %@, %@",
        model->songPlaybackActive
            ? [NSString stringWithFormat:@"Playing %@",
                [self displayedPatternId]]
            : [NSString stringWithFormat:@"Editing %@",
                [self displayedPatternId]],
        static_cast<unsigned long>(lane + 1u),
        static_cast<unsigned long>(row + 1u), fieldName, fieldValue];
    NSAccessibilityPostNotification(self,
        NSAccessibilityValueChangedNotification);
}

- (void)selectTrack:(std::size_t)track row:(std::size_t)row
{
    if (!self.trackerState) return;
    auto& session = self.trackerState->session;
    if (session.pattern.tracks.empty()) return;
    session.selectedTrack = std::min(track, session.pattern.tracks.size() - 1u);
    session.selectedRow = std::min(row, visibleRows(self.trackerState) - 1u);
    [self.owner moduleSelectionChanged];
}

- (void)setLoopFromAnchor:(std::size_t)anchor row:(std::size_t)row
{
    if (!self.trackerState) return;
    auto& transport = self.trackerState->session.transport;
    transport.loopStartRow = static_cast<uint32_t>(std::min(anchor, row));
    transport.loopEndRow = static_cast<uint32_t>(std::max(anchor, row) + 1u);
    [self.owner moduleTransportChanged];
}

- (void)beginLoopSelectionAtRow:(NSInteger)row
{
    auto* model = self.trackerState;
    if (!model || model->songPlaybackActive
        || model->session.pattern.tracks.empty()
        || row < 0 || row >= static_cast<NSInteger>(visibleRows(model)))
        return;
    [self clearGridSelection];
    self.loopAnchorRow = row;
    self.selectingLoopRows = YES;
    [self setLoopFromAnchor:static_cast<std::size_t>(row)
        row:static_cast<std::size_t>(row)];
    [self selectTrack:model->session.selectedTrack
        row:static_cast<std::size_t>(row)];
    [self.window makeFirstResponder:self];
}

- (void)continueLoopSelectionAtRow:(NSInteger)row
{
    auto* model = self.trackerState;
    if (!model || !self.selectingLoopRows || self.loopAnchorRow < 0)
        return;
    row = std::clamp<NSInteger>(row, 0,
        static_cast<NSInteger>(visibleRows(model) - 1u));
    [self setLoopFromAnchor:static_cast<std::size_t>(self.loopAnchorRow)
        row:static_cast<std::size_t>(row)];
    [self selectTrack:model->session.selectedTrack
        row:static_cast<std::size_t>(row)];
}

- (void)finishLoopSelection
{
    self.selectingLoopRows = NO;
    self.loopAnchorRow = -1;
}

- (void)scrollSelectionToVisible
{
    auto* model = self.trackerState;
    if (!model || model->session.pattern.tracks.empty()) return;
    const auto laneCount = std::min<std::size_t>(
        s3g::tracker::kMaximumTrackCount,
        model->session.pattern.tracks.size());
    const auto lane = std::min(model->session.selectedTrack, laneCount - 1u);
    const auto row = std::min(model->session.selectedRow,
        visibleRows(model) - 1u);
    const CGFloat laneWidth = gridLaneWidth(
        model->sequenceColumnsExpanded);
    [self scrollRectToVisible:NSMakeRect(
        std::max<CGFloat>(0.0,
            gridLaneX(lane, laneWidth) - kGridRowNumberWidth),
        kGridHeaderHeight + static_cast<CGFloat>(row) * kGridRowHeight,
        laneWidth + kGridRowNumberWidth, kGridRowHeight)];
}

- (void)writeCellState:(NoteCellState)state advance:(BOOL)advance
{
    auto* model = self.trackerState;
    if (!model || model->session.pattern.tracks.empty()) return;
    auto& session = model->session;
    const auto lane = std::min(session.selectedTrack,
        session.pattern.tracks.size() - 1u);
    auto& track = session.pattern.tracks[lane];
    const auto row = session.selectedRow;
    if (track.notes.size() <= row) track.notes.resize(row + 1u,
        NoteCell::rest());
    if (state == NoteCellState::Note)
        track.notes[row] = NoteCell::withNote(
            s3g::tracker::laneDefaultNote(session, lane));
    else if (state == NoteCellState::RetriggerPrevious)
        track.notes[row] = NoteCell::retriggerPrevious();
    else if (state == NoteCellState::Kill)
        track.notes[row] = NoteCell::kill();
    else if (state == NoteCellState::Hold)
        track.notes[row] = NoteCell::hold();
    else
        track.notes[row] = NoteCell::rest();
    track.noteColumn.length = std::max(track.noteColumn.length, row + 1u);
    session.pattern.visibleRows = std::max(session.pattern.visibleRows,
        row + 1u);
    if (advance) session.selectedRow = (row + 1u) % visibleRows(model);
    [self.owner modulePatternChanged];
}

- (void)toggleSelectedCell:(BOOL)advance
{
    auto* model = self.trackerState;
    if (!model || model->session.pattern.tracks.empty()) return;
    const auto& session = model->session;
    const auto lane = std::min(session.selectedTrack,
        session.pattern.tracks.size() - 1u);
    const auto& notes = session.pattern.tracks[lane].notes;
    const bool hit = session.selectedRow < notes.size()
        && notes[session.selectedRow].state == NoteCellState::Note;
    [self writeCellState:hit ? NoteCellState::Rest : NoteCellState::Note
        advance:advance];
}

- (void)writeInstrumentState:(InstrumentCellState)state
    nodeId:(uint32_t)nodeId advance:(BOOL)advance
{
    auto* model = self.trackerState;
    if (!model || model->session.pattern.tracks.empty()) return;
    auto& session = model->session;
    const auto lane = std::min(session.selectedTrack,
        session.pattern.tracks.size() - 1u);
    auto& track = session.pattern.tracks[lane];
    const auto row = session.selectedRow;
    const auto storageSize = std::max(track.instrumentColumn.length,
        row + 1u);
    if (track.instruments.size() < storageSize) {
        track.instruments.resize(storageSize, InstrumentCell::empty());
    }
    switch (state) {
    case InstrumentCellState::Instrument:
        if (!s3g::tracker::rackInstrument(model->instrumentRack, nodeId))
            return;
        track.instruments[row] = InstrumentCell::withInstrument(nodeId);
        break;
    case InstrumentCellState::Previous:
        track.instruments[row] = InstrumentCell::previous();
        break;
    case InstrumentCellState::Empty:
    default:
        track.instruments[row] = InstrumentCell::empty();
        break;
    }
    track.instrumentColumn.length = std::max(
        track.instrumentColumn.length, row + 1u);
    session.pattern.visibleRows = std::max(session.pattern.visibleRows,
        row + 1u);
    if (advance) session.selectedRow = (row + 1u) % visibleRows(model);
    [self.owner modulePatternChanged];
}

- (void)writeUsefulInstrument:(BOOL)advance
{
    auto* model = self.trackerState;
    if (!model || model->session.pattern.tracks.empty()) return;
    const auto& session = model->session;
    const auto lane = std::min(session.selectedTrack,
        session.pattern.tracks.size() - 1u);
    const auto nodeId = laneInitialInstrument(session.pattern.tracks[lane]);
    [self writeInstrumentState:InstrumentCellState::Instrument
        nodeId:nodeId advance:advance];
}

- (void)cycleInstrument:(int)delta
{
    auto* model = self.trackerState;
    if (!model || model->session.pattern.tracks.empty()) return;
    const auto& session = model->session;
    const auto lane = std::min(session.selectedTrack,
        session.pattern.tracks.size() - 1u);
    const auto& track = session.pattern.tracks[lane];
    uint32_t authored = laneInitialInstrument(track);
    if (session.selectedRow < track.instruments.size()) {
        const auto& cell = track.instruments[session.selectedRow];
        if (cell.state == InstrumentCellState::Instrument
            && cell.nodeId < s3g::tracker::kInstrumentRackSlotCount) {
            authored = cell.nodeId;
        }
    }
    auto slot = s3g::tracker::midiOutRackSlotIndex(authored);
    if (slot >= s3g::tracker::kMidiOutRackSlotCount) slot = 0u;
    const auto count = static_cast<int>(s3g::tracker::kMidiOutRackSlotCount);
    const auto wrapped = (static_cast<int>(slot) + delta % count + count)
        % count;
    const auto next = s3g::tracker::midiOutNodeForRackSlot(
        static_cast<std::size_t>(wrapped));
    [self writeInstrumentState:InstrumentCellState::Instrument
        nodeId:next advance:NO];
}

- (void)adjustVolume:(float)delta
{
    auto* model = self.trackerState;
    if (!model || model->session.pattern.tracks.empty()) return;
    auto& session = model->session;
    auto& track = session.pattern.tracks[std::min(session.selectedTrack,
        session.pattern.tracks.size() - 1u)];
    const auto row = session.selectedRow;
    if (track.velocities.size() <= row) track.velocities.resize(row + 1u,
        ValueCell::defaultValue());
    const float current = resolvedVelocity(track, row);
    auto& cell = track.velocities[row];
    if (cell.state == ValueCellState::Value
        && cell.valueVoiceCount() > 1u) {
        std::array<float, s3g::tracker::kMaximumNoteVoices> voices {};
        const auto count = cell.valueVoiceCount();
        for (std::size_t voice = 0u; voice < count; ++voice)
            voices[voice] = std::clamp(
                cell.valueVoice(voice) + delta, 0.0f, 1.0f);
        cell = ValueCell::withValues(voices, count);
    } else {
        cell = ValueCell::withValue(std::clamp(
            current + delta, 0.0f, 1.0f));
    }
    track.velocityColumn.length = std::max(track.velocityColumn.length,
        row + 1u);
    session.pattern.visibleRows = std::max(session.pattern.visibleRows,
        row + 1u);
    [self.owner modulePatternChanged];
}

- (void)adjustFxValue:(int)delta
{
    auto* model = self.trackerState;
    if (!model || model->session.pattern.tracks.empty()
        || !gridFieldIsSequence(model->session.selectedField)) return;
    auto& session = model->session;
    auto& track = session.pattern.tracks[std::min(session.selectedTrack,
        session.pattern.tracks.size() - 1u)];
    const auto pairIndex = gridSequencePair(session.selectedField);
    auto& pair = track.fxPairs[pairIndex];
    const auto row = session.selectedRow;
    if (pair.values.size() <= row)
        pair.values.resize(row + 1u, FxValueCell::previous());
    const float current = resolvedFxValue(track,
        pairIndex, row);
    auto& cell = pair.values[row];
    if (cell.state == FxValueCellState::Value
        && cell.valueVoiceCount() > 1u) {
        std::array<float, s3g::tracker::kMaximumNoteVoices> values {};
        const auto count = cell.valueVoiceCount();
        for (std::size_t voice = 0u; voice < count; ++voice) {
            const int scaled = static_cast<int>(std::lround(
                cell.valueVoice(voice) * 100.0f));
            values[voice] = static_cast<float>(
                std::clamp(scaled + delta, 0, 100)) / 100.0f;
        }
        cell = FxValueCell::withValues(values, count);
    } else {
        const int scaled = static_cast<int>(std::lround(current * 100.0f));
        cell = FxValueCell::withValue(
            static_cast<float>(std::clamp(scaled + delta, 0, 100))
                / 100.0f);
    }
    pair.valueColumn.length = std::max(pair.valueColumn.length, row + 1u);
    session.pattern.visibleRows = std::max(session.pattern.visibleRows,
        row + 1u);
    [self.owner modulePatternChanged];
}

- (void)writeFxState:(BOOL)previous clear:(BOOL)clear
{
    auto* model = self.trackerState;
    if (!model || model->session.pattern.tracks.empty()
        || !gridFieldIsSequence(model->session.selectedField)) return;
    auto& session = model->session;
    auto& track = session.pattern.tracks[std::min(session.selectedTrack,
        session.pattern.tracks.size() - 1u)];
    auto& pair = track.fxPairs[gridSequencePair(session.selectedField)];
    const auto row = session.selectedRow;
    if (gridFieldIsSequenceAction(session.selectedField)) {
        if (pair.actions.size() <= row)
            pair.actions.resize(row + 1u, FxActionCell::empty());
        if (clear) pair.actions[row] = FxActionCell::empty();
        else if (previous) pair.actions[row] = FxActionCell::previous();
        else {
            pair.actions[row] = FxActionCell::sequencer(
                s3g::tracker::SequencerAction::Probability);
            if (pair.values.size() <= row)
                pair.values.resize(row + 1u, FxValueCell::previous());
            if (pair.values[row].state == FxValueCellState::Previous)
                pair.values[row] = FxValueCell::withValue(0.5f);
            pair.valueColumn.length = std::max(
                pair.valueColumn.length, row + 1u);
        }
        pair.actionColumn.length = std::max(
            pair.actionColumn.length, row + 1u);
    } else {
        if (pair.values.size() <= row)
            pair.values.resize(row + 1u, FxValueCell::previous());
        if (clear || previous)
            pair.values[row] = FxValueCell::previous();
        else
            pair.values[row] = FxValueCell::withValue(0.5f);
        pair.valueColumn.length = std::max(
            pair.valueColumn.length, row + 1u);
    }
    session.pattern.visibleRows = std::max(session.pattern.visibleRows,
        row + 1u);
    session.selectedRow = (row + 1u) % visibleRows(model);
    [self.owner modulePatternChanged];
}

- (void)showMidiChannelMenuForTrack:(std::size_t)trackIndex
    event:(NSEvent*)event
{
    auto* model = self.trackerState;
    if (!model || trackIndex >= model->session.pattern.tracks.size()) return;
    const auto selected = static_cast<std::size_t>(std::clamp<int>(
        model->session.pattern.tracks[trackIndex].midiChannel, 1, 16));
    NSMenu* menu = [[NSMenu alloc] initWithTitle:@"TRACK MIDI CHANNEL"];
    menu.autoenablesItems = NO;
    for (std::size_t channel = 1u; channel <= 16u; ++channel) {
        NSMenuItem* item = [[NSMenuItem alloc]
            initWithTitle:[NSString stringWithFormat:@"CHANNEL %02lu",
                static_cast<unsigned long>(channel)]
            action:@selector(laneMidiChannelSelected:) keyEquivalent:@""];
        item.target = self;
        item.representedObject = @{
            @"track": @(trackIndex), @"channel": @(channel),
        };
        item.state = channel == selected
            ? NSControlStateValueOn : NSControlStateValueOff;
        [menu addItem:item];
    }
    [NSMenu popUpContextMenu:menu withEvent:event forView:self];
}

- (void)laneMidiChannelSelected:(NSMenuItem*)sender
{
    auto* model = self.trackerState;
    NSDictionary* value = sender.representedObject;
    if (!model || ![value isKindOfClass:NSDictionary.class]) return;
    const auto trackIndex = [value[@"track"] unsignedIntegerValue];
    const auto channel = [value[@"channel"] unsignedIntegerValue];
    if (trackIndex >= model->session.pattern.tracks.size()
        || channel < 1u || channel > 16u) return;
    model->session.pattern.tracks[trackIndex].midiChannel =
        static_cast<uint8_t>(channel);
    model->session.selectedTrack = trackIndex;
    [self.owner modulePatternChanged];
}

- (NSMenu*)sequenceActionMenuForTrack:(std::size_t)trackIndex
    row:(std::size_t)row field:(std::size_t)field
{
    auto* model = self.trackerState;
    if (!model || trackIndex >= model->session.pattern.tracks.size()
        || !gridFieldIsSequenceAction(field)) return nil;
    const auto pairIndex = gridSequencePair(field);
    const auto& pair = model->session.pattern.tracks[trackIndex]
        .fxPairs[pairIndex];
    const FxActionCell current = row < pair.actions.size()
        ? pair.actions[row] : FxActionCell::empty();

    NSMenu* menu = [[NSMenu alloc] initWithTitle:@"SEQUENCING ACTION"];
    menu.autoenablesItems = NO;
    menu.font = trackerFont(9.5, NSFontWeightMedium);
    NSMenuItem* heading = [[NSMenuItem alloc]
        initWithTitle:@"SEQ ACTION  ·  NORMALIZED VALUE 0.000–1.000"
        action:nil keyEquivalent:@""];
    heading.enabled = NO;
    [menu addItem:heading];
    [menu addItem:NSMenuItem.separatorItem];

    const auto addUtility = [&](NSString* title, NSString* kind,
                                BOOL selected) {
        NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:title
            action:@selector(sequenceActionSelected:) keyEquivalent:@""];
        item.target = self;
        item.representedObject = @{
            @"track": @(trackIndex), @"row": @(row), @"field": @(field),
            @"kind": kind,
        };
        item.state = selected ? NSControlStateValueOn
                              : NSControlStateValueOff;
        [menu addItem:item];
    };
    addUtility(@"---   CLEAR", @"clear",
        current.state == FxActionCellState::Empty);
    addUtility(@"PRV   PREVIOUS / RECALL", @"previous",
        current.state == FxActionCellState::Previous);
    [menu addItem:NSMenuItem.separatorItem];

    for (std::size_t index = 0u;
         index < s3g::tracker::sequencerActionCount(); ++index) {
        const auto* action = s3g::tracker::sequencerAction(index);
        if (!action) continue;
        NSString* title = [NSString stringWithFormat:@"%@   %@  ·  %@",
            nsString(std::string(action->mnemonic)),
            nsString(std::string(action->displayName)).uppercaseString,
            nsString(std::string(action->valueMeaning))];
        NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:title
            action:@selector(sequenceActionSelected:) keyEquivalent:@""];
        item.target = self;
        item.representedObject = @{
            @"track": @(trackIndex), @"row": @(row), @"field": @(field),
            @"kind": @"action", @"action": @(index),
        };
        item.state = current.state == FxActionCellState::Sequencer
                && current.sequencerAction == action->action
            ? NSControlStateValueOn : NSControlStateValueOff;
        [menu addItem:item];
    }
    [menu addItem:NSMenuItem.separatorItem];
    NSMenuItem* ccRoot = [[NSMenuItem alloc]
        initWithTitle:@"MIDI CONTROL CHANGE" action:nil keyEquivalent:@""];
    NSMenu* ccMenu = [[NSMenu alloc] initWithTitle:@"MIDI CONTROL CHANGE"];
    for (NSUInteger group = 0u; group < 4u; ++group) {
        const NSUInteger first = group * 32u;
        NSMenuItem* groupItem = [[NSMenuItem alloc]
            initWithTitle:[NSString stringWithFormat:@"CC%03lu–CC%03lu",
                static_cast<unsigned long>(first),
                static_cast<unsigned long>(first + 31u)]
            action:nil keyEquivalent:@""];
        NSMenu* groupMenu = [[NSMenu alloc] initWithTitle:groupItem.title];
        for (NSUInteger controller = first;
             controller < first + 32u; ++controller) {
            NSMenuItem* item = [[NSMenuItem alloc]
                initWithTitle:[NSString stringWithFormat:@"CC%03lu",
                    static_cast<unsigned long>(controller)]
                action:@selector(sequenceActionSelected:)
                keyEquivalent:@""];
            item.target = self;
            item.representedObject = @{
                @"track": @(trackIndex), @"row": @(row),
                @"field": @(field), @"kind": @"cc",
                @"controller": @(controller),
            };
            item.state = current.state
                        == FxActionCellState::MidiControlChange
                    && current.midiController == controller
                ? NSControlStateValueOn : NSControlStateValueOff;
            [groupMenu addItem:item];
        }
        groupItem.submenu = groupMenu;
        [ccMenu addItem:groupItem];
    }
    ccRoot.submenu = ccMenu;
    [menu addItem:ccRoot];
    [self appendSelectionMenuTo:menu];
    return menu;
}

- (void)sequenceActionSelected:(NSMenuItem*)sender
{
    auto* model = self.trackerState;
    NSDictionary* value = sender.representedObject;
    if (!model || model->songPlaybackActive
        || ![value isKindOfClass:NSDictionary.class]) return;
    const auto trackIndex = [value[@"track"] unsignedIntegerValue];
    const auto row = [value[@"row"] unsignedIntegerValue];
    const auto field = [value[@"field"] unsignedIntegerValue];
    NSString* kind = value[@"kind"];
    if (trackIndex >= model->session.pattern.tracks.size()
        || row >= kGridMaximumRows
        || !gridFieldIsSequenceAction(field)
        || ![kind isKindOfClass:NSString.class]) return;

    auto& pair = model->session.pattern.tracks[trackIndex]
        .fxPairs[gridSequencePair(field)];
    if (pair.actions.size() <= row)
        pair.actions.resize(row + 1u, FxActionCell::empty());
    if ([kind isEqualToString:@"clear"]) {
        pair.actions[row] = FxActionCell::empty();
    } else if ([kind isEqualToString:@"previous"]) {
        pair.actions[row] = FxActionCell::previous();
    } else if ([kind isEqualToString:@"action"]) {
        const auto index = [value[@"action"] unsignedIntegerValue];
        const auto* action = s3g::tracker::sequencerAction(index);
        if (!action) return;
        pair.actions[row] = FxActionCell::sequencer(action->action);
        if (pair.values.size() <= row)
            pair.values.resize(row + 1u, FxValueCell::previous());
        if (pair.values[row].state == FxValueCellState::Previous) {
            pair.values[row] = FxValueCell::withValue(
                action->action == SequencerAction::Condition
                    ? s3g::tracker::normalizedFromSequencerCondition(
                        s3g::tracker::SequencerCondition::FirstOf2)
                    : 0.5f);
        }
        pair.valueColumn.length = std::max(
            pair.valueColumn.length, row + 1u);
    } else if ([kind isEqualToString:@"cc"]) {
        const auto controller = [value[@"controller"] unsignedIntegerValue];
        if (controller > 127u) return;
        pair.actions[row] = FxActionCell::midiControlChange(
            static_cast<uint8_t>(controller));
        if (pair.values.size() <= row)
            pair.values.resize(row + 1u, FxValueCell::previous());
        if (pair.values[row].state == FxValueCellState::Previous)
            pair.values[row] = FxValueCell::withValue(0.5f);
        pair.valueColumn.length = std::max(
            pair.valueColumn.length, row + 1u);
    } else return;

    pair.actionColumn.length = std::max(
        pair.actionColumn.length, row + 1u);
    model->session.pattern.visibleRows = std::max(
        model->session.pattern.visibleRows, row + 1u);
    model->session.selectedTrack = trackIndex;
    model->session.selectedRow = row;
    model->session.selectedField = field;
    [self clearGridSelection];
    [self.owner modulePatternChanged];
    [self.window makeFirstResponder:self];
}

- (NSMenu*)sequenceConditionMenuForTrack:(std::size_t)trackIndex
    row:(std::size_t)row field:(std::size_t)field
{
    auto* model = self.trackerState;
    if (!model || trackIndex >= model->session.pattern.tracks.size()
        || !gridFieldIsSequence(field)
        || gridFieldIsSequenceAction(field)) return nil;
    const auto pairIndex = gridSequencePair(field);
    const auto& pair = model->session.pattern.tracks[trackIndex]
        .fxPairs[pairIndex];
    if (row >= pair.actions.size()
        || pair.actions[row].state != FxActionCellState::Sequencer
        || pair.actions[row].sequencerAction
            != SequencerAction::Condition) return nil;
    const auto current = row < pair.values.size()
            && pair.values[row].state == FxValueCellState::Value
        ? s3g::tracker::sequencerConditionFromNormalized(
            pair.values[row].normalized)
        : s3g::tracker::SequencerCondition::FirstOf2;
    NSMenu* menu = [[NSMenu alloc] initWithTitle:@"CONDITION"];
    menu.autoenablesItems = NO;
    menu.font = trackerFont(9.5, NSFontWeightMedium);
    NSMenuItem* heading = [[NSMenuItem alloc]
        initWithTitle:@"CD  ·  PLAY THIS NOTE WHEN"
        action:nil keyEquivalent:@""];
    heading.enabled = NO;
    [menu addItem:heading];
    [menu addItem:NSMenuItem.separatorItem];
    for (std::size_t index = 0u;
         index < s3g::tracker::kSequencerConditionCount; ++index) {
        const auto* condition = s3g::tracker::sequencerCondition(index);
        if (!condition) continue;
        if (index == static_cast<std::size_t>(
                s3g::tracker::SequencerCondition::First)
            || index == static_cast<std::size_t>(
                s3g::tracker::SequencerCondition::Fill)
            || index == static_cast<std::size_t>(
                s3g::tracker::SequencerCondition::SongFirst)
            || index == static_cast<std::size_t>(
                s3g::tracker::SequencerCondition::SongFirstOf2)) {
            [menu addItem:NSMenuItem.separatorItem];
        }
        NSMenuItem* item = [[NSMenuItem alloc]
            initWithTitle:[NSString stringWithFormat:@"%@   %@",
                nsString(std::string(condition->token)),
                nsString(std::string(condition->displayName))]
            action:@selector(sequenceConditionSelected:)
            keyEquivalent:@""];
        item.target = self;
        item.representedObject = @{
            @"track": @(trackIndex), @"row": @(row), @"field": @(field),
            @"condition": @(index),
        };
        item.state = current == condition->condition
            ? NSControlStateValueOn : NSControlStateValueOff;
        [menu addItem:item];
    }
    [self appendSelectionMenuTo:menu];
    return menu;
}

- (void)sequenceConditionSelected:(NSMenuItem*)sender
{
    auto* model = self.trackerState;
    NSDictionary* value = sender.representedObject;
    if (!model || model->songPlaybackActive
        || ![value isKindOfClass:NSDictionary.class]) return;
    const auto trackIndex = [value[@"track"] unsignedIntegerValue];
    const auto row = [value[@"row"] unsignedIntegerValue];
    const auto field = [value[@"field"] unsignedIntegerValue];
    const auto conditionIndex = [value[@"condition"] unsignedIntegerValue];
    const auto* condition = s3g::tracker::sequencerCondition(conditionIndex);
    if (!condition || trackIndex >= model->session.pattern.tracks.size()
        || row >= kGridMaximumRows || !gridFieldIsSequence(field)
        || gridFieldIsSequenceAction(field)) return;
    auto& pair = model->session.pattern.tracks[trackIndex]
        .fxPairs[gridSequencePair(field)];
    if (pair.values.size() <= row)
        pair.values.resize(row + 1u, FxValueCell::previous());
    pair.values[row] = FxValueCell::withValue(
        s3g::tracker::normalizedFromSequencerCondition(
            condition->condition));
    pair.valueColumn.length = std::max(pair.valueColumn.length, row + 1u);
    model->session.selectedTrack = trackIndex;
    model->session.selectedRow = row;
    model->session.selectedField = field;
    [self clearGridSelection];
    [self.owner modulePatternChanged];
    [self.window makeFirstResponder:self];
}

- (NSMenu*)burstMenuForTrack:(std::size_t)trackIndex
    row:(std::size_t)row
{
    auto* model = self.trackerState;
    if (!model || trackIndex >= model->session.pattern.tracks.size())
        return nil;
    auto& pattern = model->session.pattern;
    const auto& bursts = model->session.burstLibrary.bursts;
    const auto firstEmpty = std::find_if(bursts.begin(),
        bursts.end(), [](const BurstDefinition& burst) {
            return burst.empty();
        });
    const BOOL hasEmpty = firstEmpty != bursts.end();
    const auto& track = pattern.tracks[trackIndex];
    const NoteCell current = row < track.notes.size()
        ? track.notes[row] : NoteCell::rest();
    NSMenu* menu = [[NSMenu alloc] initWithTitle:@"BURST"];
    menu.autoenablesItems = NO;
    menu.font = trackerFont(9.5, NSFontWeightMedium);
    const auto add = ^(NSString* title, NSString* kind, BOOL enabled,
                       NSInteger slot, NSDictionary* extra) {
        NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:title
            action:@selector(burstMenuSelected:) keyEquivalent:@""];
        item.target = self;
        NSMutableDictionary* payload = [@{
            @"track": @(trackIndex), @"row": @(row), @"kind": kind,
            @"slot": @(slot),
        } mutableCopy];
        if (extra) [payload addEntriesFromDictionary:extra];
        item.representedObject = payload;
        item.enabled = enabled;
        [menu addItem:item];
    };
    for (const NSUInteger count : { 2u, 3u, 4u, 6u, 8u }) {
        add([NSString stringWithFormat:@"CREATE %lu EVEN SUBSTEPS",
                static_cast<unsigned long>(count)], @"create", hasEmpty,
            -1, @{ @"count": @(count) });
    }

    const auto range = [self effectiveGridSelection];
    const auto fields = gridFieldCount(model->sequenceColumnsExpanded);
    const auto selectedColumn = gridClipboardColumn(trackIndex, 0u, fields);
    const BOOL convertibleSelection = _gridSelection.active
        && _gridSelection.firstColumn(fields) == selectedColumn
        && _gridSelection.lastColumn(fields) == selectedColumn
        && range.rowCount() >= 2u && range.rowCount() <= kMaximumBurstEvents;
    add(@"CREATE FROM SELECTED NOTE ROWS", @"capture",
        hasEmpty && convertibleSelection, -1, @{
            @"firstRow": @(range.firstRow), @"lastRow": @(range.lastRow),
        });
    [menu addItem:NSMenuItem.separatorItem];

    NSMenuItem* useRoot = [[NSMenuItem alloc] initWithTitle:@"USE BURST"
        action:nil keyEquivalent:@""];
    NSMenu* useMenu = [[NSMenu alloc] initWithTitle:@"USE BURST"];
    for (std::size_t slot = 0u; slot < bursts.size(); ++slot) {
        const auto& burst = bursts[slot];
        if (burst.empty()) continue;
        NSString* title = [NSString stringWithFormat:@"%@  ·  %@  ·  %u STEPS",
            nsString(burstSlotToken(slot)), nsString(burst.name),
            static_cast<unsigned int>(burst.eventCount)];
        NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:title
            action:@selector(burstMenuSelected:) keyEquivalent:@""];
        item.target = self;
        item.representedObject = @{
            @"track": @(trackIndex), @"row": @(row), @"kind": @"use",
            @"slot": @(slot),
        };
        item.state = current.state == NoteCellState::Burst
                && current.note == slot
            ? NSControlStateValueOn : NSControlStateValueOff;
        [useMenu addItem:item];
    }
    useRoot.submenu = useMenu;
    useRoot.enabled = useMenu.numberOfItems > 0;
    [menu addItem:useRoot];

    if (current.state == NoteCellState::Burst
        && current.note < bursts.size()
        && !bursts[current.note].empty()) {
        [menu addItem:NSMenuItem.separatorItem];
        add([NSString stringWithFormat:@"EDIT %@ IN BURSTS",
                nsString(burstSlotToken(current.note))], @"edit", YES,
            current.note, nil);
        add(@"DUPLICATE AND EDIT", @"duplicate", hasEmpty,
            current.note, nil);
        add(@"EXPAND TO TRACKER ROWS", @"expand", YES,
            current.note, nil);
        add(@"CONVERT TO FIRST NOTE", @"convert", YES,
            current.note, nil);
    }
    return menu;
}

- (NSDictionary*)columnActionPayloadForTrack:(std::size_t)track
    field:(std::size_t)field row:(std::size_t)row
{
    const auto range = [self effectiveGridSelection];
    const auto fields = self.trackerState
        ? gridFieldCount(self.trackerState->sequenceColumnsExpanded) : 0u;
    const auto selectedColumn = gridClipboardColumn(track, field, fields);
    const bool selectedColumnRange = _gridSelection.active
        && _gridSelection.firstColumn(fields) == selectedColumn
        && _gridSelection.lastColumn(fields) == selectedColumn
        && _gridSelection.containsLinear(0u, track, field, row, fields);
    const auto first = selectedColumnRange ? range.firstRow : row;
    const auto last = selectedColumnRange ? range.lastRow : row;
    return @{
        @"track": @(track), @"field": @(field), @"row": @(first),
        @"count": @(last - first + 1u),
    };
}

- (NSMenu*)noteMenuForTrack:(std::size_t)track row:(std::size_t)row
{
    auto* model = self.trackerState;
    if (!model || track >= model->session.pattern.tracks.size()) return nil;
    const BOOL editable = !model->songPlaybackActive;
    NSDictionary* payload = [self columnActionPayloadForTrack:track
        field:0u row:row];
    NSMenu* menu = [[NSMenu alloc] initWithTitle:@"NOTE"];
    menu.autoenablesItems = NO;
    menu.font = trackerFont(9.5, NSFontWeightMedium);
    const auto add = ^NSMenuItem*(NSMenu* target, NSString* title,
        SEL action, NSInteger tag, BOOL enabled) {
        NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:title
            action:action keyEquivalent:@""];
        item.target = self;
        item.representedObject = payload;
        item.tag = tag;
        item.enabled = enabled;
        [target addItem:item];
        return item;
    };

    NSMenu* pitch = [[NSMenu alloc] initWithTitle:@"PITCH"];
    pitch.autoenablesItems = NO;
    add(pitch, @"UP 1 SEMITONE", @selector(transposeSelectedNoteRows:),
        1, editable);
    add(pitch, @"DOWN 1 SEMITONE", @selector(transposeSelectedNoteRows:),
        -1, editable);
    [pitch addItem:NSMenuItem.separatorItem];
    add(pitch, @"UP 1 OCTAVE", @selector(transposeSelectedNoteRows:),
        12, editable);
    add(pitch, @"DOWN 1 OCTAVE", @selector(transposeSelectedNoteRows:),
        -12, editable);
    [pitch addItem:NSMenuItem.separatorItem];
    add(pitch, @"FIT TO CURRENT SCALE",
        @selector(fitScaleSelectedNoteRows:), 0, editable);
    add(pitch, @"GENERATE CONTOUR",
        @selector(generateScaleSelectedNoteRows:), 0, editable);
    add(pitch, @"OPEN PITCH MAP…",
        @selector(openPitchMapSelectedNoteRows:), 0, YES);
    NSMenuItem* pitchRoot = [[NSMenuItem alloc] initWithTitle:@"PITCH"
        action:nil keyEquivalent:@""];
    pitchRoot.submenu = pitch;
    [menu addItem:pitchRoot];

    NSMenuItem* burstRoot = [[NSMenuItem alloc] initWithTitle:@"BURST"
        action:nil keyEquivalent:@""];
    burstRoot.submenu = [self burstMenuForTrack:track row:row];
    burstRoot.enabled = burstRoot.submenu.numberOfItems > 0u;
    [menu addItem:burstRoot];
    [self appendSelectionMenuTo:menu];
    return menu;
}

- (NSMenu*)velocityMenuForTrack:(std::size_t)track row:(std::size_t)row
{
    auto* model = self.trackerState;
    if (!model || track >= model->session.pattern.tracks.size()) return nil;
    const BOOL editable = !model->songPlaybackActive;
    NSDictionary* payload = [self columnActionPayloadForTrack:track
        field:1u row:row];
    NSMenu* menu = [[NSMenu alloc] initWithTitle:@"VELOCITY"];
    menu.autoenablesItems = NO;
    menu.font = trackerFont(9.5, NSFontWeightMedium);
    const auto add = ^(NSString* title, SEL action, NSInteger tag) {
        NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:title
            action:action keyEquivalent:@""];
        item.target = self;
        item.representedObject = payload;
        item.tag = tag;
        item.enabled = editable;
        [menu addItem:item];
    };
    for (const NSInteger percent : { 75, 90, 110, 125 }) {
        add([NSString stringWithFormat:@"SCALE WRITTEN VALUES %ld%%",
                static_cast<long>(percent)],
            @selector(scaleSelectedVelocityRows:), percent);
    }
    [menu addItem:NSMenuItem.separatorItem];
    for (const NSInteger minimum : { 96, 64, 1 }) {
        add([NSString stringWithFormat:@"RANDOMIZE VALUES %ld–127",
                static_cast<long>(minimum)],
            @selector(randomizeSelectedVelocityRows:), minimum);
    }
    [self appendSelectionMenuTo:menu];
    return menu;
}

- (void)burstMenuSelected:(NSMenuItem*)sender
{
    auto* model = self.trackerState;
    NSDictionary* payload = sender.representedObject;
    if (!model || model->songPlaybackActive
        || ![payload isKindOfClass:NSDictionary.class]) return;
    auto& pattern = model->session.pattern;
    auto& bursts = model->session.burstLibrary.bursts;
    const auto trackIndex = [payload[@"track"] unsignedIntegerValue];
    const auto row = [payload[@"row"] unsignedIntegerValue];
    NSString* kind = payload[@"kind"];
    if (trackIndex >= pattern.tracks.size() || row >= kGridMaximumRows
        || ![kind isKindOfClass:NSString.class]) return;
    auto& track = pattern.tracks[trackIndex];
    const auto findEmpty = [&]() -> std::size_t {
        const auto found = std::find_if(bursts.begin(),
            bursts.end(), [](const BurstDefinition& burst) {
                return burst.empty();
            });
        return found == bursts.end() ? bursts.size()
                                             : static_cast<std::size_t>(
                                                   found - bursts.begin());
    };
    std::size_t slot = [payload[@"slot"] integerValue] < 0
        ? bursts.size()
        : [payload[@"slot"] unsignedIntegerValue];
    if ([kind isEqualToString:@"create"]
        || [kind isEqualToString:@"capture"]) {
        slot = findEmpty();
        if (slot >= bursts.size()) return;
        BurstDefinition burst;
        burst.name = [kind isEqualToString:@"capture"]
            ? "ROW CAPTURE" : "EVEN "
                + std::to_string([payload[@"count"] unsignedIntegerValue]);
        if ([kind isEqualToString:@"create"]) {
            const auto count = std::clamp<NSUInteger>(
                [payload[@"count"] unsignedIntegerValue], 1u,
                kMaximumBurstEvents);
            burst.eventCount = static_cast<uint8_t>(count);
            const auto note = laneDefaultNote(model->session, trackIndex);
            for (std::size_t index = 0u; index < count; ++index) {
                burst.events[index] = {
                    static_cast<uint16_t>(index * 65536u / count),
                    note, 127u, 70u,
                };
            }
        } else {
            const auto first = [payload[@"firstRow"] unsignedIntegerValue];
            const auto last = [payload[@"lastRow"] unsignedIntegerValue];
            const auto span = std::max<std::size_t>(last - first + 1u, 1u);
            for (std::size_t sourceRow = first; sourceRow <= last
                 && burst.eventCount < kMaximumBurstEvents; ++sourceRow) {
                if (sourceRow >= track.notes.size()
                    || track.notes[sourceRow].state != NoteCellState::Note)
                    continue;
                auto& event = burst.events[burst.eventCount++];
                event.position = static_cast<uint16_t>(
                    (sourceRow - first) * 65536u / span);
                event.note = track.notes[sourceRow].note;
                event.velocity = static_cast<uint8_t>(std::clamp<long>(
                    std::lround(resolvedVelocity(track, sourceRow) * 127.0f),
                    1l, 127l));
                event.gatePercent = 70u;
            }
            if (burst.empty()) return;
            if (track.notes.size() <= last)
                track.notes.resize(last + 1u, NoteCell::rest());
            for (std::size_t clear = first; clear <= last; ++clear)
                track.notes[clear] = NoteCell::rest();
        }
        bursts[slot] = burst;
        if (track.notes.size() <= row)
            track.notes.resize(row + 1u, NoteCell::rest());
        track.notes[row] = NoteCell::withBurst(static_cast<uint8_t>(slot));
    } else if ([kind isEqualToString:@"use"] && slot < bursts.size()
        && !bursts[slot].empty()) {
        if (track.notes.size() <= row)
            track.notes.resize(row + 1u, NoteCell::rest());
        track.notes[row] = NoteCell::withBurst(static_cast<uint8_t>(slot));
    } else if ([kind isEqualToString:@"duplicate"]
        && slot < bursts.size()) {
        const auto destination = findEmpty();
        if (destination >= bursts.size()) return;
        bursts[destination] = bursts[slot];
        bursts[destination].name += " COPY";
        [self.owner modulePatternChanged];
        [self.owner editBurstSlot:destination];
        return;
    } else if ([kind isEqualToString:@"edit"]
        && slot < bursts.size()) {
        [self.owner editBurstSlot:slot];
        return;
    } else if ([kind isEqualToString:@"expand"]
        && slot < bursts.size()) {
        const auto burst = bursts[slot];
        const auto required = std::min<std::size_t>(kGridMaximumRows,
            row + burst.eventCount);
        track.notes.resize(std::max(track.notes.size(), required),
            NoteCell::rest());
        track.velocities.resize(std::max(track.velocities.size(), required),
            ValueCell::defaultValue());
        for (std::size_t index = 0u; index < burst.eventCount
             && row + index < required; ++index) {
            track.notes[row + index] = NoteCell::withNote(
                burst.events[index].note);
            track.velocities[row + index] = ValueCell::withValue(
                static_cast<float>(burst.events[index].velocity) / 127.0f);
        }
        pattern.visibleRows = std::max(pattern.visibleRows, required);
        track.noteColumn.length = std::max(track.noteColumn.length, required);
        track.velocityColumn.length = std::max(
            track.velocityColumn.length, required);
    } else if ([kind isEqualToString:@"convert"]
        && slot < bursts.size()
        && !bursts[slot].empty()) {
        track.notes[row] = NoteCell::withNote(
            bursts[slot].events[0u].note);
    } else return;
    track.noteColumn.length = std::max(track.noteColumn.length, row + 1u);
    pattern.visibleRows = std::max(pattern.visibleRows, row + 1u);
    model->session.selectedTrack = trackIndex;
    model->session.selectedRow = row;
    model->session.selectedField = 0u;
    [self clearGridSelection];
    [self.owner modulePatternChanged];
    [self.window makeFirstResponder:self];
}

- (void)rightMouseDown:(NSEvent*)event
{
    auto* model = self.trackerState;
    if (!model || model->session.pattern.tracks.empty()) {
        [super rightMouseDown:event];
        return;
    }
    if (model->songPlaybackActive) {
        [self.window makeFirstResponder:self];
        return;
    }
    const NSPoint point = [self convertPoint:event.locationInWindow
        fromView:nil];
    if (point.y < kGridHeaderHeight) {
        [super rightMouseDown:event];
        return;
    }
    const auto laneCount = std::min<std::size_t>(
        s3g::tracker::kMaximumTrackCount,
        model->session.pattern.tracks.size());
    const CGFloat laneWidth = gridLaneWidth(
        model->sequenceColumnsExpanded);
    const CGFloat fieldWidth = gridLaneFieldWidth(laneWidth);
    std::size_t lane = 0u;
    CGFloat localFieldX = 0.0;
    if (!gridLaneAtX(point.x, laneCount,
            model->sequenceColumnsExpanded, lane, localFieldX)) {
        [super rightMouseDown:event];
        return;
    }
    const NSInteger rawRow = static_cast<NSInteger>(
        (point.y - kGridHeaderHeight) / kGridRowHeight);
    if (rawRow < 0
        || rawRow >= static_cast<NSInteger>(visibleRows(model))) return;
    const auto row = static_cast<std::size_t>(rawRow);
    const auto field = gridFieldAtX(localFieldX, fieldWidth,
        model->sequenceColumnsExpanded);
    const bool conditionValue = gridFieldIsSequence(field)
        && !gridFieldIsSequenceAction(field)
        && [self sequenceConditionMenuForTrack:lane row:row field:field]
            != nil;
    const bool noteField = field == 0u;
    const bool velocityField = field == 1u;
    model->session.selectedField = field;
    [self selectTrack:lane row:row];
    const auto fields = gridFieldCount(model->sequenceColumnsExpanded);
    const bool preserveSelection = _gridSelection.active
        && _gridSelection.containsLinear(0u, lane, field, row, fields);
    if (!preserveSelection) [self clearGridSelection];
    [self.window makeFirstResponder:self];
    NSMenu* menu = noteField ? [self noteMenuForTrack:lane row:row]
        : velocityField ? [self velocityMenuForTrack:lane row:row]
        : conditionValue
            ? [self sequenceConditionMenuForTrack:lane row:row field:field]
        : gridFieldIsSequenceAction(field)
            ? [self sequenceActionMenuForTrack:lane row:row field:field]
        : [[NSMenu alloc] initWithTitle:(
            gridFieldIsGate(field) ? @"GATE" : @"VALUE")];
    if (!noteField && !velocityField && !conditionValue
        && !gridFieldIsSequenceAction(field)) {
        menu.autoenablesItems = NO;
        menu.font = trackerFont(9.5, NSFontWeightMedium);
        [self appendSelectionMenuTo:menu];
    }
    if (menu) [NSMenu popUpContextMenu:menu withEvent:event forView:self];
}

- (void)mouseDown:(NSEvent*)event
{
    auto* model = self.trackerState;
    if (!model || model->session.pattern.tracks.empty()) return;
    if (model->songPlaybackActive) {
        const auto* soundingPattern = playbackFollowPattern(model);
        if (!soundingPattern || soundingPattern->tracks.empty()) return;
        const NSPoint point = [self convertPoint:event.locationInWindow
            fromView:nil];
        const auto laneCount = std::min<std::size_t>(
            s3g::tracker::kMaximumTrackCount,
            soundingPattern->tracks.size());
        std::size_t lane = 0u;
        CGFloat localFieldX = 0.0;
        if (gridLaneAtX(point.x, laneCount,
                model->sequenceColumnsExpanded, lane, localFieldX)) {
            std::size_t row = model->session.selectedRow;
            if (point.y >= kGridHeaderHeight) {
                const NSInteger clickedRow = static_cast<NSInteger>(
                    (point.y - kGridHeaderHeight) / kGridRowHeight);
                if (clickedRow >= 0
                    && clickedRow < static_cast<NSInteger>(
                        playbackFollowVisibleRows(model))) {
                    row = static_cast<std::size_t>(clickedRow);
                }
            }
            model->session.selectedField = gridFieldAtX(
                localFieldX, gridLaneFieldWidth(gridLaneWidth(
                    model->sequenceColumnsExpanded)),
                model->sequenceColumnsExpanded);
            [self selectTrack:lane row:row];
        }
        [self clearGridSelection];
        [self.window makeFirstResponder:self];
        return;
    }
    const NSPoint point = [self convertPoint:event.locationInWindow fromView:nil];
    if (point.x < kGridRowNumberWidth && point.y >= kGridHeaderHeight) {
        const NSInteger row = static_cast<NSInteger>(
            (point.y - kGridHeaderHeight) / kGridRowHeight);
        [self beginLoopSelectionAtRow:row];
        return;
    }
    self.selectingLoopRows = NO;
    self.selectingGridCells = NO;
    self.numericDragCandidate = NO;
    self.draggingNumericCell = NO;
    self.numericDragChanged = NO;
    self.loopAnchorRow = -1;
    const auto laneCount = std::min<std::size_t>(
        s3g::tracker::kMaximumTrackCount,
        model->session.pattern.tracks.size());
    const CGFloat width = gridLaneWidth(
        model->sequenceColumnsExpanded);
    const CGFloat fieldWidth = gridLaneFieldWidth(width);
    std::size_t lane = 0u;
    CGFloat localFieldX = 0.0;
    if (!gridLaneAtX(point.x, laneCount,
            model->sequenceColumnsExpanded, lane, localFieldX)) return;
    if (point.y < kGridHeaderHeight) {
        [self clearGridSelection];
        const CGFloat laneLeft = gridLaneFieldX(lane, width);
        if (NSPointInRect(point,
                gridLaneChannelRect(laneLeft, fieldWidth))) {
            [self selectTrack:lane row:model->session.selectedRow];
            [self showMidiChannelMenuForTrack:lane event:event];
            return;
        }
        if (NSPointInRect(point,
                gridLaneResyncRect(laneLeft, fieldWidth))) {
            [self selectTrack:lane row:model->session.selectedRow];
            if (self.owner.trackerCallbacks
                && self.owner.trackerCallbacks->resyncTrack)
                self.owner.trackerCallbacks->resyncTrack(lane);
            [self.window makeFirstResponder:self];
            return;
        }
        if (event.clickCount >= 2 && point.y < 22.0) {
            [self selectTrack:lane row:model->session.selectedRow];
            [self beginTrackNameEditingForTrack:lane rect:NSMakeRect(
                laneLeft + 4.0, 3.0,
                std::max<CGFloat>(44.0, fieldWidth - 92.0), 18.0)];
            return;
        }
        const auto page = 0u;
        model->session.selectedField = gridFieldAtX(
            localFieldX, fieldWidth, model->sequenceColumnsExpanded);
        [self selectTrack:lane
            row:model->session.selectedRow];
        const NSRect muteRect = gridFieldRect(laneLeft,
            kGridColumnMuteTop, fieldWidth, kGridColumnMuteHeight,
            model->sequenceColumnsExpanded, model->session.selectedField);
        const NSRect directionRect = gridFieldRect(laneLeft,
            kGridColumnDirectionTop, fieldWidth,
            kGridColumnDirectionHeight, model->sequenceColumnsExpanded,
            model->session.selectedField);
        const NSRect readStartRect = gridFieldRect(laneLeft,
            kGridColumnReadStartTop, fieldWidth,
            kGridColumnReadStartHeight, model->sequenceColumnsExpanded,
            model->session.selectedField);
        const NSRect lengthRect = gridFieldRect(laneLeft,
            kGridColumnLengthTop, fieldWidth, kGridColumnLengthHeight,
            model->sequenceColumnsExpanded, model->session.selectedField);
        const NSRect labelRect = gridFieldRect(laneLeft,
            kGridColumnLabelTop, fieldWidth, kGridColumnLabelHeight,
            model->sequenceColumnsExpanded, model->session.selectedField);
        if (NSPointInRect(point, muteRect)) {
            auto& track = model->session.pattern.tracks[
                lane];
            auto& muted = columnForField(track,
                0u,
                model->session.selectedField)->muted;
            muted = !muted;
            [self.owner modulePatternChanged];
        } else if (NSPointInRect(point, directionRect)) {
            auto& track = model->session.pattern.tracks[lane];
            auto& direction = columnForField(track, 0u,
                model->session.selectedField)->direction;
            direction = nextDirection(direction);
            [self.owner modulePatternChanged];
        } else if (gridFieldIsSequence(model->session.selectedField)
            && !gridFieldIsSequenceAction(model->session.selectedField)
            && NSPointInRect(point, labelRect)) {
            auto& pair = model->session.pattern.tracks[lane].fxPairs[
                gridSequencePair(model->session.selectedField)];
            pair.valueInterpolation = pair.valueInterpolation
                    == ValueInterpolation::Step
                ? ValueInterpolation::Linear : ValueInterpolation::Step;
            [self.owner modulePatternChanged];
        } else if (event.clickCount >= 2
            && NSPointInRect(point, lengthRect)) {
            [self beginColumnLengthEditingForTrack:lane page:page
                field:model->session.selectedField
                rect:NSInsetRect(lengthRect, 1.0, 0.0)];
            return;
        } else if (event.clickCount >= 2
            && NSPointInRect(point, readStartRect)) {
            [self beginColumnReadStartEditingForTrack:lane page:page
                field:model->session.selectedField
                rect:NSInsetRect(readStartRect, 1.0, 0.0)];
            return;
        }
        [self.window makeFirstResponder:self];
        return;
    }
    const NSInteger row = static_cast<NSInteger>(
        (point.y - kGridHeaderHeight) / kGridRowHeight);
    if (row < 0 || row >= static_cast<NSInteger>(visibleRows(model))) return;
    const auto page = 0u;
    const auto field = gridFieldAtX(
        localFieldX, fieldWidth, model->sequenceColumnsExpanded);
    if ((event.modifierFlags & NSEventModifierFlagShift) != 0u
        && [self extendGridSelectionInColumnToTrack:lane
            field:field row:static_cast<std::size_t>(row)]) {
        [self.window makeFirstResponder:self];
        return;
    }
    model->session.selectedField = field;
    [self beginGridSelectionAtTrack:lane
        field:field row:static_cast<std::size_t>(row)
        page:page];
    [self selectTrack:lane
        row:static_cast<std::size_t>(row)];
    [self.window makeFirstResponder:self];
    if (event.clickCount >= 2) {
        NSMenu* conditionMenu = [self sequenceConditionMenuForTrack:lane
            row:static_cast<std::size_t>(row) field:field];
        if (conditionMenu)
            [NSMenu popUpContextMenu:conditionMenu withEvent:event
                forView:self];
        else
            [self beginCellEditing];
    } else if ((event.modifierFlags & NSEventModifierFlagControl) == 0u
        && (field == 1u || (gridFieldIsSequence(field)
            && !gridFieldIsSequenceAction(field)))) {
        const auto& track = model->session.pattern.tracks[lane];
        if ([self sequenceConditionMenuForTrack:lane
                row:static_cast<std::size_t>(row) field:field]) return;
        self.numericDragCandidate = YES;
        self.numericDragOrigin = point;
        self.numericDragStartValue = field == 1u
            ? resolvedVelocity(track, static_cast<std::size_t>(row))
            : resolvedFxValue(track, gridSequencePair(field),
                static_cast<std::size_t>(row));
        self.numericDragTrack = lane;
        self.numericDragRow = static_cast<std::size_t>(row);
        self.numericDragField = field;
    }
}

- (void)mouseDragged:(NSEvent*)event
{
    auto* model = self.trackerState;
    if (!model || model->songPlaybackActive) return;
    const NSPoint point = [self convertPoint:event.locationInWindow fromView:nil];
    if (self.numericDragCandidate || self.draggingNumericCell) {
        const CGFloat horizontal = point.x - self.numericDragOrigin.x;
        const CGFloat vertical = self.numericDragOrigin.y - point.y;
        if (!self.draggingNumericCell
            && std::abs(vertical) >= 2.0
            && std::abs(vertical) >= std::abs(horizontal)) {
            self.draggingNumericCell = YES;
            self.selectingGridCells = NO;
            [self clearGridSelection];
        }
        if (self.draggingNumericCell
            && self.numericDragTrack < model->session.pattern.tracks.size()) {
            const float value
                = s3g::tracker::app::normalizedValueFromVerticalDrag(
                    self.numericDragStartValue,
                    static_cast<double>(vertical),
                    (event.modifierFlags & NSEventModifierFlagOption) != 0u,
                    (event.modifierFlags & NSEventModifierFlagShift) != 0u);
            auto& track = model->session.pattern.tracks[
                self.numericDragTrack];
            if (self.numericDragField == 1u) {
                if (track.velocities.size() <= self.numericDragRow)
                    track.velocities.resize(self.numericDragRow + 1u,
                        ValueCell::defaultValue());
                auto& cell = track.velocities[self.numericDragRow];
                if (cell.state == ValueCellState::Value
                    && cell.valueVoiceCount() > 1u) {
                    std::array<float, s3g::tracker::kMaximumNoteVoices> voices {};
                    const float source = std::max(
                        self.numericDragStartValue, 0.00001f);
                    const float factor = value / source;
                    const auto count = cell.valueVoiceCount();
                    for (std::size_t voice = 0u; voice < count; ++voice)
                        voices[voice] = std::clamp(
                            cell.valueVoice(voice) * factor, 0.0f, 1.0f);
                    cell = ValueCell::withValues(voices, count);
                } else {
                    cell = ValueCell::withValue(value);
                }
                track.velocityColumn.length = std::max(
                    track.velocityColumn.length, self.numericDragRow + 1u);
            } else {
                auto& pair = track.fxPairs[
                    gridSequencePair(self.numericDragField)];
                if (pair.values.size() <= self.numericDragRow)
                    pair.values.resize(self.numericDragRow + 1u,
                        FxValueCell::previous());
                auto& cell = pair.values[self.numericDragRow];
                if (cell.state == FxValueCellState::Value
                    && cell.valueVoiceCount() > 1u) {
                    std::array<float, s3g::tracker::kMaximumNoteVoices>
                        voices {};
                    const float delta = value - self.numericDragStartValue;
                    const auto count = cell.valueVoiceCount();
                    for (std::size_t voice = 0u; voice < count; ++voice)
                        voices[voice] = std::clamp(
                            cell.valueVoice(voice) + delta, 0.0f, 1.0f);
                    cell = FxValueCell::withValues(voices, count);
                } else cell = FxValueCell::withValue(value);
                pair.valueColumn.length = std::max(
                    pair.valueColumn.length, self.numericDragRow + 1u);
            }
            model->session.pattern.visibleRows = std::max(
                model->session.pattern.visibleRows,
                self.numericDragRow + 1u);
            model->session.selectedTrack = self.numericDragTrack;
            model->session.selectedRow = self.numericDragRow;
            model->session.selectedField = self.numericDragField;
            self.numericDragChanged = YES;
            [self.owner moduleSelectionChanged];
            return;
        }
    }
    if (!self.selectingLoopRows) {
        if (!self.selectingGridCells) return;
        const auto laneCount = std::min<std::size_t>(
            s3g::tracker::kMaximumTrackCount,
            model->session.pattern.tracks.size());
        const CGFloat width = gridLaneWidth(
            model->sequenceColumnsExpanded);
        const CGFloat fieldWidth = gridLaneFieldWidth(width);
        std::size_t lane = 0u;
        CGFloat localFieldX = 0.0;
        if (!gridLaneAtX(point.x, laneCount,
                model->sequenceColumnsExpanded, lane, localFieldX)) return;
        const NSInteger row = std::clamp<NSInteger>(static_cast<NSInteger>(
            (point.y - kGridHeaderHeight) / kGridRowHeight), 0,
            static_cast<NSInteger>(visibleRows(model) - 1u));
        const auto field = gridFieldAtX(localFieldX, fieldWidth,
            model->sequenceColumnsExpanded);
        [self extendGridSelectionToTrack:lane field:field
            row:static_cast<std::size_t>(row)];
        model->session.selectedField = field;
        [self selectTrack:lane row:static_cast<std::size_t>(row)];
        return;
    }
    const NSInteger row = std::clamp<NSInteger>(static_cast<NSInteger>(
        (point.y - kGridHeaderHeight) / kGridRowHeight), 0,
        static_cast<NSInteger>(visibleRows(model) - 1u));
    [self continueLoopSelectionAtRow:row];
}

- (void)mouseUp:(NSEvent*)event
{
    (void)event;
    const BOOL publishNumericDrag = self.draggingNumericCell
        && self.numericDragChanged;
    [self finishLoopSelection];
    self.selectingGridCells = NO;
    self.numericDragCandidate = NO;
    self.draggingNumericCell = NO;
    self.numericDragChanged = NO;
    if (publishNumericDrag) {
        [self.owner modulePatternChanged];
        return;
    }
}

- (NSString*)editingValue
{
    auto* model = self.trackerState;
    if (!model || self.editingTrack >= model->session.pattern.tracks.size())
        return @"";
    const auto& track = model->session.pattern.tracks[self.editingTrack];
    if (self.editingField == 0u) {
        if (self.editingRow >= track.notes.size()) return @"---";
        const auto& cell = track.notes[self.editingRow];
        return noteText(cell, model->showMidiNoteValues, false);
    }
    if (self.editingField == 1u) {
        if (self.editingRow >= track.velocities.size()) return @"DEF";
        const auto& cell = track.velocities[self.editingRow];
        if (cell.state == ValueCellState::Value)
            return volumeText(track, self.editingRow, false);
        return cell.state == ValueCellState::Previous ? @"PRV" : @"DEF";
    }
    if (gridFieldIsGate(self.editingField))
        return gateText(track, self.editingRow, false);
    const auto pairIndex = gridSequencePair(self.editingField);
    const auto& pair = track.fxPairs[pairIndex];
    if (gridFieldIsSequenceAction(self.editingField)) {
        if (self.editingRow >= pair.actions.size()) return @"";
        return fxActionText(track, pairIndex, self.editingRow);
    }
    if (self.editingRow >= pair.values.size()) return @"PRV";
    return fxValueText(track, pairIndex, self.editingRow, false);
}

- (void)beginCellEditing
{
    [self beginCellEditingWithInitialText:nil];
}

- (void)beginCellEditingWithInitialText:(NSString*)initialText
{
    auto* model = self.trackerState;
    if (!model || model->session.pattern.tracks.empty()) return;
    [self.cellEditor removeFromSuperview];
    const auto laneCount = std::min<std::size_t>(
        s3g::tracker::kMaximumTrackCount,
        model->session.pattern.tracks.size());
    const auto lane = std::min(model->session.selectedTrack, laneCount - 1u);
    const auto row = model->session.selectedRow;
    const auto page = 0u;
    const auto field = std::min(model->session.selectedField,
        gridFieldCount(model->sequenceColumnsExpanded) - 1u);
    const CGFloat laneWidth = gridLaneWidth(
        model->sequenceColumnsExpanded);
    const CGFloat fieldWidth = gridLaneFieldWidth(laneWidth);
    const CGFloat x = gridLaneFieldX(lane, laneWidth);
    NSRect rect = gridFieldRect(x, kGridHeaderHeight
            + static_cast<CGFloat>(row) * kGridRowHeight,
        fieldWidth, kGridRowHeight, model->sequenceColumnsExpanded, field);
    rect = NSInsetRect(rect, 1.0, 1.0);
    self.editingTrack = lane;
    self.editingRow = row;
    self.editingPage = page;
    self.editingField = field;
    self.editingColumnLength = NO;
    self.editingColumnReadStart = NO;
    self.editingTrackName = NO;
    self.cellEditor = [[NSTextField alloc] initWithFrame:rect];
    self.cellEditorCellRect = rect;
    S3GTrackerStyleTextField(self.cellEditor, NSTextAlignmentCenter);
    self.cellEditor.font = trackerFont(11.0, NSFontWeightSemibold);
    self.cellEditor.stringValue = initialText
        ? initialText : [self editingValue];
    self.cellEditor.frame = S3GTrackerExpandedCellEditorRect(
        self.cellEditorCellRect, self.visibleRect,
        self.cellEditor.stringValue, self.cellEditor.font);
    self.cellEditor.delegate = self;
    self.cellEditor.target = self;
    self.cellEditor.action = @selector(commitCellEditing:);
    self.cellEditor.accessibilityLabel = @"Direct tracker cell value";
    [self addSubview:self.cellEditor];
    [self.window makeFirstResponder:self.cellEditor];
    NSText* editor = self.cellEditor.currentEditor;
    if ([editor respondsToSelector:@selector(setSelectedRange:)]) {
        [(NSTextView*)editor setSelectedRange:NSMakeRange(
            self.cellEditor.stringValue.length, 0u)];
    }
}

- (void)controlTextDidChange:(NSNotification*)notification
{
    if (notification.object != self.cellEditor || self.editingColumnLength
        || self.editingColumnReadStart || self.editingTrackName) return;
    self.cellEditor.frame = S3GTrackerExpandedCellEditorRect(
        self.cellEditorCellRect, self.visibleRect,
        self.cellEditor.stringValue, self.cellEditor.font);
}

- (void)beginTrackNameEditingForTrack:(std::size_t)track rect:(NSRect)rect
{
    auto* model = self.trackerState;
    if (!model || track >= model->session.pattern.tracks.size()) return;
    [self.cellEditor removeFromSuperview];
    self.editingTrack = track;
    self.editingRow = model->session.selectedRow;
    self.editingPage = 0u;
    self.editingField = model->session.selectedField;
    self.editingColumnLength = NO;
    self.editingColumnReadStart = NO;
    self.editingTrackName = YES;
    self.cellEditor = [[NSTextField alloc] initWithFrame:rect];
    S3GTrackerStyleTextField(self.cellEditor, NSTextAlignmentLeft);
    self.cellEditor.font = trackerFont(9.5, NSFontWeightSemibold);
    self.cellEditor.stringValue = nsString(
        model->session.pattern.tracks[track].name);
    self.cellEditor.delegate = self;
    self.cellEditor.target = self;
    self.cellEditor.action = @selector(commitCellEditing:);
    self.cellEditor.accessibilityLabel = @"Track name";
    [self addSubview:self.cellEditor];
    [self.window makeFirstResponder:self.cellEditor];
    [self.cellEditor selectText:nil];
}

- (void)beginColumnLengthEditingForTrack:(std::size_t)track
    page:(std::size_t)page field:(std::size_t)field rect:(NSRect)rect
{
    auto* model = self.trackerState;
    if (!model || track >= model->session.pattern.tracks.size()) return;
    [self.cellEditor removeFromSuperview];
    self.editingTrack = track;
    self.editingRow = model->session.selectedRow;
    self.editingPage = page;
    self.editingField = field;
    self.editingColumnLength = YES;
    self.editingColumnReadStart = NO;
    self.editingTrackName = NO;
    auto& trackModel = model->session.pattern.tracks[track];
    const auto* column = columnForField(trackModel, page, field);
    self.cellEditor = [[NSTextField alloc] initWithFrame:rect];
    S3GTrackerStyleTextField(self.cellEditor, NSTextAlignmentCenter);
    self.cellEditor.font = trackerFont(9.5, NSFontWeightSemibold);
    self.cellEditor.stringValue = column->stride == 1u
        ? [NSString stringWithFormat:@"%lu",
            static_cast<unsigned long>(column->length)]
        : [NSString stringWithFormat:@"%lux%u",
            static_cast<unsigned long>(column->length), column->stride];
    self.cellEditor.delegate = self;
    self.cellEditor.target = self;
    self.cellEditor.action = @selector(commitCellEditing:);
    self.cellEditor.accessibilityLabel = @"Column length and stride";
    self.cellEditor.toolTip = @"Enter length or length x stride, for example 24x2";
    [self addSubview:self.cellEditor];
    [self.window makeFirstResponder:self.cellEditor];
    [self.cellEditor selectText:nil];
}

- (void)beginColumnReadStartEditingForTrack:(std::size_t)track
    page:(std::size_t)page field:(std::size_t)field rect:(NSRect)rect
{
    auto* model = self.trackerState;
    if (!model || track >= model->session.pattern.tracks.size()) return;
    [self.cellEditor removeFromSuperview];
    self.editingTrack = track;
    self.editingRow = model->session.selectedRow;
    self.editingPage = page;
    self.editingField = field;
    self.editingColumnLength = NO;
    self.editingColumnReadStart = YES;
    self.editingTrackName = NO;
    auto& trackModel = model->session.pattern.tracks[track];
    const auto* column = columnForField(trackModel, page, field);
    const auto length = std::max<std::size_t>(1u, column->length);
    const auto readStart = column->phase % length + 1u;
    self.cellEditor = [[NSTextField alloc] initWithFrame:rect];
    S3GTrackerStyleTextField(self.cellEditor, NSTextAlignmentCenter);
    self.cellEditor.font = trackerFont(9.5, NSFontWeightSemibold);
    self.cellEditor.integerValue = static_cast<NSInteger>(readStart);
    self.cellEditor.delegate = self;
    self.cellEditor.target = self;
    self.cellEditor.action = @selector(commitCellEditing:);
    self.cellEditor.accessibilityLabel = @"Column read start row";
    self.cellEditor.toolTip = @"One-based first row read by this column";
    [self addSubview:self.cellEditor];
    [self.window makeFirstResponder:self.cellEditor];
    [self.cellEditor selectText:nil];
}

- (BOOL)scanInteger:(NSString*)text result:(NSInteger*)result
{
    NSScanner* scanner = [NSScanner scannerWithString:text];
    NSInteger value = 0;
    if (![scanner scanInteger:&value] || !scanner.isAtEnd) return NO;
    if (result) *result = value;
    return YES;
}

- (BOOL)scanDouble:(NSString*)text result:(double*)result
{
    NSScanner* scanner = [NSScanner scannerWithString:text];
    double value = 0.0;
    if (![scanner scanDouble:&value] || !scanner.isAtEnd
        || !std::isfinite(value)) return NO;
    if (result) *result = value;
    return YES;
}

- (BOOL)scanColumnLengthAndStride:(NSString*)text
    length:(std::size_t*)length stride:(uint32_t*)stride
{
    NSString* normalized = [[text lowercaseString]
        stringByReplacingOccurrencesOfString:@"×" withString:@"x"];
    normalized = [normalized stringByReplacingOccurrencesOfString:@"*"
        withString:@"x"];
    normalized = [normalized stringByReplacingOccurrencesOfString:@" "
        withString:@""];
    NSArray<NSString*>* parts = [normalized componentsSeparatedByString:@"x"];
    if (parts.count < 1u || parts.count > 2u) return NO;
    NSInteger parsedLength = 0;
    NSInteger parsedStride = 1;
    if (parts[0u].length == 0u
        || ![self scanInteger:parts[0u] result:&parsedLength]) return NO;
    if (parts.count == 2u && (parts[1u].length == 0u
            || ![self scanInteger:parts[1u] result:&parsedStride])) return NO;
    if (parsedLength < 1 || parsedLength > 256 || parsedStride < 1
        || static_cast<uint64_t>(parsedStride)
            > std::numeric_limits<uint32_t>::max()) return NO;
    if (length) *length = static_cast<std::size_t>(parsedLength);
    if (stride) *stride = static_cast<uint32_t>(parsedStride);
    return YES;
}

- (BOOL)applyCellText:(NSString*)source toTrack:(Track&)track
    row:(std::size_t)row page:(std::size_t)page field:(std::size_t)field
{
    NSString* text = [source stringByTrimmingCharactersInSet:
        NSCharacterSet.whitespaceAndNewlineCharacterSet];
    NSString* lower = text.lowercaseString;
    auto* model = self.trackerState;
    if (!model || page != 0u
        || field >= gridFieldCount(model->sequenceColumnsExpanded))
        return NO;
    if (field == 0u) {
        if (track.notes.size() <= row)
            track.notes.resize(row + 1u, NoteCell::rest());
        std::size_t burstSlot = 0u;
        const char* noteUtf8 = lower.UTF8String;
        if ([lower isEqualToString:@"---"] || [lower isEqualToString:@"rest"]
            || lower.length == 0u) track.notes[row] = NoteCell::rest();
        else if ([lower isEqualToString:@"rpt"]
            || [lower isEqualToString:@"repeat"])
            track.notes[row] = NoteCell::retriggerPrevious();
        else if ([lower isEqualToString:@"kil"]
            || [lower isEqualToString:@"kill"])
            track.notes[row] = NoteCell::kill();
        else if ([lower isEqualToString:@"hld"]
            || [lower isEqualToString:@"hold"]
            || [lower isEqualToString:@"~"])
            track.notes[row] = NoteCell::hold();
        else if (noteUtf8 && s3g::tracker::parseBurstSlot(noteUtf8,
                burstSlot) && burstSlot < model->session.burstLibrary.bursts.size()
            && !model->session.burstLibrary.bursts[burstSlot].empty())
            track.notes[row] = NoteCell::withBurst(
                static_cast<uint8_t>(burstSlot));
        else {
            std::array<uint8_t, s3g::tracker::kMaximumNoteVoices> voices {};
            std::size_t count = 0u;
            if (!parseNoteStack(lower, voices, count)) return NO;
            track.notes[row] = NoteCell::withNotes(voices, count);
        }
        track.noteColumn.length = std::max(track.noteColumn.length, row + 1u);
        return YES;
    }
    if (field == 1u) {
        if (track.velocities.size() <= row)
            track.velocities.resize(row + 1u, ValueCell::defaultValue());
        if ([lower isEqualToString:@"def"]
            || [lower isEqualToString:@"default"] || lower.length == 0u)
            track.velocities[row] = ValueCell::defaultValue();
        else if ([lower isEqualToString:@"prv"]
            || [lower isEqualToString:@"previous"])
            track.velocities[row] = ValueCell::previous();
        else {
            std::array<float, s3g::tracker::kMaximumNoteVoices> voices {};
            std::size_t count = 0u;
            if (!parseVelocityStack(lower, voices, count)) return NO;
            track.velocities[row] = ValueCell::withValues(voices, count);
        }
        track.velocityColumn.length = std::max(
            track.velocityColumn.length, row + 1u);
        return YES;
    }
    if (gridFieldIsGate(field)) {
        if (track.gates.size() <= row)
            track.gates.resize(row + 1u, GateCell::defaultValue());
        if (lower.length == 0u || [lower isEqualToString:@"def"]
            || [lower isEqualToString:@"default"]) {
            track.gates[row] = GateCell::defaultValue();
        } else {
            NSArray<NSString*>* parts = [lower componentsSeparatedByString:@"+"];
            if (parts.count == 0u
                || parts.count > s3g::tracker::kMaximumNoteVoices) return NO;
            std::array<GateVoice, s3g::tracker::kMaximumNoteVoices> voices {};
            for (NSUInteger voice = 0u; voice < parts.count; ++voice) {
                NSString* part = [parts[voice]
                    stringByTrimmingCharactersInSet:
                        NSCharacterSet.whitespaceAndNewlineCharacterSet];
                if ([part isEqualToString:@"def"]
                    || [part isEqualToString:@"default"]) {
                    voices[voice] = { GateVoiceMode::Default, 1.0f };
                } else if ([part isEqualToString:@"tie"]
                    || [part isEqualToString:@"t"]) {
                    voices[voice] = { GateVoiceMode::Tie, 1.0f };
                } else {
                    double rows = 0.0;
                    if (![self scanDouble:part result:&rows]
                        || !std::isfinite(rows) || rows < 0.01 || rows > 64.0)
                        return NO;
                    voices[voice] = { GateVoiceMode::Rows,
                        static_cast<float>(rows) };
                }
            }
            track.gates[row] = GateCell::withVoices(voices, parts.count);
        }
        track.gateColumn.length = std::max(track.gateColumn.length, row + 1u);
        return YES;
    }
    const auto pairIndex = gridSequencePair(field);
    auto& pair = track.fxPairs[pairIndex];
    if (gridFieldIsSequenceAction(field)) {
        if (pair.actions.size() <= row)
            pair.actions.resize(row + 1u, FxActionCell::empty());
        if (lower.length == 0u || [lower isEqualToString:@"---"]
            || [lower isEqualToString:@"clear"])
            pair.actions[row] = FxActionCell::empty();
        else if ([lower isEqualToString:@"prv"]
            || [lower isEqualToString:@"previous"])
            pair.actions[row] = FxActionCell::previous();
        else {
            const char* utf8 = lower.UTF8String;
            const std::string key(utf8 ? utf8 : "");
            if (const auto* timing
                = s3g::tracker::findSequencerAction(key)) {
                pair.actions[row] = FxActionCell::sequencer(timing->action);
            } else {
                uint8_t controller = 0u;
                if (!s3g::tracker::parseMidiControlChange(key, controller))
                    return NO;
                pair.actions[row] = FxActionCell::midiControlChange(
                    controller);
            }
        }
        pair.actionColumn.length = std::max(
            pair.actionColumn.length, row + 1u);
        return YES;
    }
    if (pair.values.size() <= row)
        pair.values.resize(row + 1u, FxValueCell::previous());
    float value = 0.0f;
    if (lower.length == 0u || [lower isEqualToString:@"prv"]
        || [lower isEqualToString:@"previous"])
        pair.values[row] = FxValueCell::previous();
    else {
        if ([lower containsString:@"+"]) {
            SequencerAction action = SequencerAction::Count;
            std::array<float, s3g::tracker::kMaximumNoteVoices> voices {};
            std::size_t count = 0u;
            if (!resolvedSequencerAction(track, pairIndex, row, action)
                || action != SequencerAction::MicroTime
                || !parseVelocityStack(lower, voices, count)) return NO;
            pair.values[row] = FxValueCell::withValues(voices, count);
            pair.valueColumn.length = std::max(
                pair.valueColumn.length, row + 1u);
            return YES;
        }
        const std::string_view entered(
            lower.UTF8String ? lower.UTF8String : "");
        const bool conditionValue = row < pair.actions.size()
            && pair.actions[row].state == FxActionCellState::Sequencer
            && pair.actions[row].sequencerAction
                == SequencerAction::Condition;
        bool parsed = false;
        if (conditionValue) {
            const auto* condition = s3g::tracker::findSequencerCondition(
                entered);
            if (condition) {
                value = s3g::tracker::normalizedFromSequencerCondition(
                    condition->condition);
                parsed = true;
            }
        } else if (pairContainsMidiControlChange(pair)) {
            parsed = s3g::tracker::app::parseGridMidiOrNormalizedValue(
                entered, value);
        } else {
            parsed = s3g::tracker::app::parseGridNormalizedValue(
                entered, value);
        }
        if (!parsed) return NO;
        pair.values[row] = FxValueCell::withValue(value);
    }
    pair.valueColumn.length = std::max(pair.valueColumn.length, row + 1u);
    return YES;
}

- (void)commitCellEditing:(id)sender
{
    (void)sender;
    auto* model = self.trackerState;
    if (!model || self.editingTrack >= model->session.pattern.tracks.size()) {
        [self.cellEditor removeFromSuperview];
        self.cellEditor = nil;
        self.editingColumnLength = NO;
        self.editingColumnReadStart = NO;
        self.editingTrackName = NO;
        return;
    }
    NSString* text = [self.cellEditor.stringValue
        stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
    NSString* lower = text.lowercaseString;
    auto& originalTrack = model->session.pattern.tracks[self.editingTrack];
    Track candidate = originalTrack;
    auto& track = candidate;
    const auto row = self.editingRow;
    BOOL valid = YES;
    if (self.editingTrackName) {
        const char* utf8 = text.UTF8String;
        if (text.length == 0u || !utf8) {
            NSBeep();
            [self.cellEditor selectText:nil];
            return;
        }
        std::string name(utf8);
        if (name.size() > 64u) name.resize(64u);
        originalTrack.name = std::move(name);
        [self.cellEditor removeFromSuperview];
        self.cellEditor = nil;
        self.editingColumnReadStart = NO;
        self.editingTrackName = NO;
        [self.owner modulePatternChanged];
        [self.window makeFirstResponder:self];
        return;
    }
    if (self.editingColumnLength) {
        std::size_t length = 0u;
        uint32_t stride = 1u;
        if (![self scanColumnLengthAndStride:lower
                length:&length stride:&stride]) {
            NSBeep();
            self.cellEditor.backgroundColor = trackerColor(0x3a2020);
            [self.cellEditor selectText:nil];
            return;
        }
        if (self.editingField == 0u) {
            if (track.notes.size() < length)
                track.notes.resize(length, NoteCell::rest());
        } else if (self.editingField == 1u) {
            if (track.velocities.size() < length)
                track.velocities.resize(length, ValueCell::defaultValue());
        } else if (gridFieldIsGate(self.editingField)) {
            if (track.gates.size() < length)
                track.gates.resize(length, GateCell::defaultValue());
        } else if (gridFieldIsSequenceAction(self.editingField)) {
            auto& actions = track.fxPairs[
                gridSequencePair(self.editingField)].actions;
            if (actions.size() < length)
                actions.resize(length, FxActionCell::empty());
        } else {
            auto& values = track.fxPairs[
                gridSequencePair(self.editingField)].values;
            if (values.size() < length)
                values.resize(length, FxValueCell::previous());
        }
        auto* column = columnForField(track, self.editingPage,
            self.editingField);
        column->length = length;
        column->stride = stride;
        column->phase %= length;
        originalTrack = std::move(candidate);
        model->session.pattern.visibleRows = std::max(
            model->session.pattern.visibleRows, length);
        [self.cellEditor removeFromSuperview];
        self.cellEditor = nil;
        self.editingColumnLength = NO;
        self.editingColumnReadStart = NO;
        self.editingTrackName = NO;
        [self.owner modulePatternChanged];
        [self.window makeFirstResponder:self];
        return;
    }
    if (self.editingColumnReadStart) {
        NSInteger readStart = 0;
        auto* column = columnForField(track, self.editingPage,
            self.editingField);
        const auto length = std::max<std::size_t>(1u, column->length);
        if (![self scanInteger:lower result:&readStart]
            || readStart < 1
            || static_cast<std::size_t>(readStart) > length) {
            NSBeep();
            self.cellEditor.backgroundColor = trackerColor(0x3a2020);
            [self.cellEditor selectText:nil];
            return;
        }
        column->phase = static_cast<std::size_t>(readStart - 1);
        originalTrack = std::move(candidate);
        [self.cellEditor removeFromSuperview];
        self.cellEditor = nil;
        self.editingColumnLength = NO;
        self.editingColumnReadStart = NO;
        self.editingTrackName = NO;
        [self.owner modulePatternChanged];
        [self.window makeFirstResponder:self];
        return;
    }
    valid = [self applyCellText:text toTrack:track row:row
        page:self.editingPage field:self.editingField];
    if (!valid) {
        NSBeep();
        self.cellEditor.backgroundColor = trackerColor(0x3a2020);
        [self.cellEditor selectText:nil];
        return;
    }
    originalTrack = std::move(candidate);
    model->session.pattern.visibleRows = std::max(
        model->session.pattern.visibleRows, row + 1u);
    model->session.selectedRow = std::min(
        row + 1u, visibleRows(model) - 1u);
    [self.cellEditor removeFromSuperview];
    self.cellEditor = nil;
    self.editingColumnLength = NO;
    self.editingColumnReadStart = NO;
    self.editingTrackName = NO;
    [self.owner modulePatternChanged];
    [self.window makeFirstResponder:self];
}

- (BOOL)control:(NSControl*)control textView:(NSTextView*)textView
    doCommandBySelector:(SEL)commandSelector
{
    (void)control;
    (void)textView;
    if (commandSelector == @selector(cancelOperation:)) {
        [self.cellEditor removeFromSuperview];
        self.cellEditor = nil;
        self.editingColumnLength = NO;
        self.editingColumnReadStart = NO;
        self.editingTrackName = NO;
        [self.window makeFirstResponder:self];
        return YES;
    }
    return NO;
}

- (NSString*)cellTextForTrack:(std::size_t)trackIndex
    row:(std::size_t)row page:(std::size_t)page field:(std::size_t)field
{
    auto* model = self.trackerState;
    if (!model || trackIndex >= model->session.pattern.tracks.size()) return @"";
    const auto& track = model->session.pattern.tracks[trackIndex];
    (void)page;
    if (field == 0u)
        return row < track.notes.size()
            ? noteText(track.notes[row], model->showMidiNoteValues) : @"---";
    if (field == 1u) return volumeText(track, row);
    if (gridFieldIsGate(field)) return gateText(track, row);
    const auto pair = gridSequencePair(field);
    return gridFieldIsSequenceAction(field)
        ? fxActionText(track, pair, row)
        : fxValueText(track, pair, row);
}

- (NSString*)clipboardTextForTrack:(std::size_t)trackIndex
    row:(std::size_t)row page:(std::size_t)page field:(std::size_t)field
{
    auto* model = self.trackerState;
    if (!model || trackIndex >= model->session.pattern.tracks.size()) return @"";
    const auto& track = model->session.pattern.tracks[trackIndex];
    (void)page;
    if (field == 0u)
        return row < track.notes.size()
            ? noteText(track.notes[row], model->showMidiNoteValues, false)
            : @"---";
    if (field == 1u) return volumeText(track, row, false);
    if (gridFieldIsGate(field)) return gateText(track, row, false);
    if (gridFieldIsSequenceAction(field))
        return fxActionText(track, gridSequencePair(field), row);
    return fxValueText(track, gridSequencePair(field), row, false);
}

- (NSString*)clearTokenForPage:(std::size_t)page field:(std::size_t)field
{
    (void)page;
    if (field == 0u) return @"---";
    if (field == 1u) return @"DEF";
    if (gridFieldIsGate(field)) return @"DEF";
    return gridFieldIsSequenceAction(field) ? @"---" : @"PRV";
}

- (void)appendSelectionMenuTo:(NSMenu*)menu
{
    auto* model = self.trackerState;
    if (!model || model->session.pattern.tracks.empty() || !menu) return;
    const auto range = [self effectiveGridSelection];
    const auto fields = gridFieldCount(model->sequenceColumnsExpanded);
    const auto firstColumn = _gridSelection.active
        ? _gridSelection.firstColumn(fields)
        : gridClipboardColumn(range.firstTrack, range.firstField, fields);
    const auto lastColumn = _gridSelection.active
        ? _gridSelection.lastColumn(fields) : firstColumn;
    const BOOL editable = !model->songPlaybackActive;
    [menu addItem:NSMenuItem.separatorItem];
    NSMenu* selection = [[NSMenu alloc] initWithTitle:@"SELECTION"];
    selection.autoenablesItems = NO;
    const auto add = ^NSMenuItem*(NSMenu* target, NSString* title,
        SEL action, NSInteger tag, BOOL enabled) {
        NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:title
            action:action keyEquivalent:@""];
        item.target = self;
        item.tag = tag;
        item.enabled = enabled;
        [target addItem:item];
        return item;
    };
    NSMenu* fill = [[NSMenu alloc] initWithTitle:@"FILL"];
    add(fill, @"FILL DOWN", @selector(fillSelectionFromEdge:), 1, editable);
    add(fill, @"FILL UP", @selector(fillSelectionFromEdge:), -1, editable);
    add(fill, @"LINEAR SERIES BETWEEN ENDS",
        @selector(fillSelectionSeries:), 0, editable && range.rowCount() > 1u);
    [fill addItem:NSMenuItem.separatorItem];
    add(fill, @"REPEAT ONCE BELOW", @selector(repeatGridSelection:), 1, editable);
    add(fill, @"REPEAT 2× BELOW", @selector(repeatGridSelection:), 2, editable);
    add(fill, @"REPEAT 4× BELOW", @selector(repeatGridSelection:), 4, editable);
    add(fill, @"REPEAT TO ROW 256", @selector(repeatGridSelection:), -1, editable);
    NSMenuItem* fillRoot = [[NSMenuItem alloc] initWithTitle:@"FILL / REPEAT"
        action:nil keyEquivalent:@""];
    fillRoot.submenu = fill;
    [selection addItem:fillRoot];

    NSMenu* cells = [[NSMenu alloc] initWithTitle:@"CELLS"];
    add(cells, @"INSERT CELLS DOWN", @selector(shiftSelectionCells:), 1, editable);
    add(cells, @"DELETE CELLS UP", @selector(shiftSelectionCells:), -1, editable);
    [cells addItem:NSMenuItem.separatorItem];
    add(cells, @"MOVE UP 1", @selector(moveGridSelection:), -1, editable);
    add(cells, @"MOVE DOWN 1", @selector(moveGridSelection:), 1, editable);
    add(cells, @"MOVE UP BY JUMP", @selector(moveGridSelection:), -100, editable);
    add(cells, @"MOVE DOWN BY JUMP", @selector(moveGridSelection:), 100, editable);
    NSMenuItem* cellsRoot = [[NSMenuItem alloc] initWithTitle:@"CELLS"
        action:nil keyEquivalent:@""];
    cellsRoot.submenu = cells;
    [selection addItem:cellsRoot];

    NSMenu* paste = [[NSMenu alloc] initWithTitle:@"PASTE SPECIAL"];
    const BOOL hasClipboard = !_copiedColumnTypes.empty();
    add(paste, @"REPLACE", @selector(pasteGridSelectionSpecial:), 0,
        editable && hasClipboard);
    add(paste, @"MERGE INTO EMPTY", @selector(pasteGridSelectionSpecial:), 1,
        editable && hasClipboard);
    add(paste, @"RHYTHM ONLY", @selector(pasteGridSelectionSpecial:), 2,
        editable && hasClipboard);
    add(paste, @"NOTES ONLY", @selector(pasteGridSelectionSpecial:), 3,
        editable && hasClipboard);
    add(paste, @"VALUES ONLY", @selector(pasteGridSelectionSpecial:), 4,
        editable && hasClipboard);
    NSMenuItem* pasteRoot = [[NSMenuItem alloc] initWithTitle:@"PASTE SPECIAL"
        action:nil keyEquivalent:@""];
    pasteRoot.submenu = paste;
    [selection addItem:pasteRoot];

    NSMenu* transform = [[NSMenu alloc] initWithTitle:@"TRANSFORM"];
    add(transform, @"REVERSE ROW ORDER", @selector(reverseGridSelection:),
        0, editable && range.rowCount() > 1u);
    add(transform, @"ROTATE UP 1", @selector(rotateGridSelection:), -1,
        editable && range.rowCount() > 1u);
    add(transform, @"ROTATE DOWN 1", @selector(rotateGridSelection:), 1,
        editable && range.rowCount() > 1u);
    [transform addItem:NSMenuItem.separatorItem];
    add(transform, @"COMPRESS TO 50%", @selector(stretchGridSelection:), 50,
        editable && range.rowCount() > 1u);
    add(transform, @"STRETCH TO 200%", @selector(stretchGridSelection:), 200,
        editable && range.rowCount() > 1u);
    add(transform, @"MATERIALIZE PRV / DEF",
        @selector(materializeGridSelection:), 0, editable);
    add(transform, @"REPLACE FIRST VALUE WITH LAST",
        @selector(findReplaceGridSelection:), 0,
        editable && range.rowCount() > 1u);
    add(transform, @"SWAP WITH NEXT LANE",
        @selector(swapGridSelectionWithNextLane:), 0,
        editable && firstColumn / fields == lastColumn / fields
            && lastColumn / fields + 1u < model->session.pattern.tracks.size());
    const BOOL oneNoteColumn = firstColumn == lastColumn
        && firstColumn % fields == 0u;
    add(transform, @"SEPARATE NOTES INTO LANES",
        @selector(splitSelectedNoteColumnByPitch:), 0,
        editable && oneNoteColumn);
    const BOOL multipleNoteLanes = firstColumn % fields == 0u
        && lastColumn % fields == 0u
        && firstColumn / fields < lastColumn / fields;
    add(transform, @"MERGE NOTES INTO ONE LANE",
        @selector(mergeSelectedNoteLanes:), 0,
        editable && multipleNoteLanes);

    BOOL numericOnly = YES;
    BOOL hasSequenceValue = NO;
    for (std::size_t column = firstColumn; column <= lastColumn; ++column) {
        const auto field = column % fields;
        const BOOL numeric = field == 1u
            || (gridFieldIsSequence(field)
                && !gridFieldIsSequenceAction(field));
        numericOnly = numericOnly && numeric;
        hasSequenceValue = hasSequenceValue
            || (gridFieldIsSequence(field)
                && !gridFieldIsSequenceAction(field));
    }
    NSMenu* values = [[NSMenu alloc] initWithTitle:@"VALUE MATH"];
    add(values, @"ADD 5 / 127", @selector(adjustSelectedValues:), 5,
        editable && numericOnly);
    add(values, @"SUBTRACT 5 / 127", @selector(adjustSelectedValues:), -5,
        editable && numericOnly);
    add(values, @"SCALE 80%", @selector(scaleSelectedValues:), 80,
        editable && numericOnly);
    add(values, @"SCALE 120%", @selector(scaleSelectedValues:), 120,
        editable && numericOnly);
    NSMenuItem* valuesRoot = [[NSMenuItem alloc] initWithTitle:@"VALUE MATH"
        action:nil keyEquivalent:@""];
    valuesRoot.submenu = values;
    [transform addItem:valuesRoot];

    NSMenu* timing = [[NSMenu alloc] initWithTitle:@"MICROTIME"];
    add(timing, @"NUDGE EARLIER 1 MS",
        @selector(quantizeSelectedMicroTime:), -1001,
        editable && hasSequenceValue);
    add(timing, @"NUDGE LATER 1 MS",
        @selector(quantizeSelectedMicroTime:), 1001,
        editable && hasSequenceValue);
    [timing addItem:NSMenuItem.separatorItem];
    for (const NSInteger amount : { 25, 50, 75, 100 })
        add(timing, [NSString stringWithFormat:@"QUANTIZE %ld%% TO GRID",
                static_cast<long>(amount)],
            @selector(quantizeSelectedMicroTime:), amount,
            editable && hasSequenceValue);
    NSMenuItem* timingRoot = [[NSMenuItem alloc] initWithTitle:@"MICROTIME"
        action:nil keyEquivalent:@""];
    timingRoot.submenu = timing;
    [transform addItem:timingRoot];
    [transform addItem:NSMenuItem.separatorItem];
    add(transform, @"SELECTION STATISTICS…",
        @selector(showGridSelectionStatistics:), 0, YES);
    NSMenuItem* transformRoot = [[NSMenuItem alloc] initWithTitle:@"TRANSFORM"
        action:nil keyEquivalent:@""];
    transformRoot.submenu = transform;
    [selection addItem:transformRoot];

    NSMenu* phrase = [[NSMenu alloc] initWithTitle:@"PHRASE"];
    const BOOL oneLane = range.firstTrack == range.lastTrack;
    NSString* selectedSlot = [NSString stringWithFormat:@"P%02lu",
        static_cast<unsigned long>(model->selectedPhrase + 1u)];
    add(phrase, [@"CAPTURE SELECTION AS " stringByAppendingString:selectedSlot],
        @selector(captureGridSelectionAsPhrase:), 0,
        editable && oneLane && range.rowCount() >= 2u
            && range.rowCount() <= s3g::tracker::kMaximumPhraseRows);
    [phrase addItem:NSMenuItem.separatorItem];
    NSMenu* phraseLibrary = [[NSMenu alloc] initWithTitle:@"COPY FROM LIBRARY"];
    phraseLibrary.autoenablesItems = NO;
    for (std::size_t slot = 0u; slot < model->phraseLibrary.phrases.size(); ++slot) {
        const auto& definition = model->phraseLibrary.phrases[slot];
        if (definition.empty() && definition.name.empty()) continue;
        NSString* name = definition.name.empty()
            ? @"UNTITLED" : nsString(definition.name);
        NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:[NSString
            stringWithFormat:@"P%02lu · %@ · %lu ROWS",
            static_cast<unsigned long>(slot + 1u), name,
            static_cast<unsigned long>(definition.length)]
            action:@selector(placePhraseSlotAtSelection:) keyEquivalent:@""];
        item.target = self;
        item.tag = static_cast<NSInteger>(slot);
        item.enabled = editable && oneLane;
        [phraseLibrary addItem:item];
    }
    NSMenuItem* phraseLibraryRoot = [[NSMenuItem alloc]
        initWithTitle:@"COPY FROM LIBRARY" action:nil keyEquivalent:@""];
    phraseLibraryRoot.submenu = phraseLibrary;
    phraseLibraryRoot.enabled = phraseLibrary.numberOfItems > 0u;
    [phrase addItem:phraseLibraryRoot];
    add(phrase, [@"COPY " stringByAppendingFormat:@"%@ HERE", selectedSlot],
        @selector(placePhraseAtSelection:), 0, editable && oneLane);
    add(phrase, [@"MERGE " stringByAppendingFormat:@"%@ INTO EMPTY", selectedSlot],
        @selector(placePhraseAtSelection:), 1, editable && oneLane);
    add(phrase, [NSString stringWithFormat:@"REAPPLY LAST P%02lu HERE",
            static_cast<unsigned long>(model->lastPlacedPhrase + 1u)],
        @selector(placePhraseAtSelection:), 2, editable && oneLane);
    NSMenuItem* phraseRoot = [[NSMenuItem alloc] initWithTitle:@"PHRASE"
        action:nil keyEquivalent:@""];
    phraseRoot.submenu = phrase;
    [selection addItem:phraseRoot];

    NSMenuItem* root = [[NSMenuItem alloc] initWithTitle:[NSString
        stringWithFormat:@"SELECTION  ·  %lu ROW%@ × %lu COLUMN%@",
        static_cast<unsigned long>(range.rowCount()),
        range.rowCount() == 1u ? @"" : @"S",
        static_cast<unsigned long>(lastColumn - firstColumn + 1u),
        lastColumn == firstColumn ? @"" : @"S"] action:nil keyEquivalent:@""];
    root.submenu = selection;
    [menu addItem:root];
}

- (void)captureGridSelectionAsPhrase:(NSMenuItem*)sender
{
    (void)sender;
    auto* model = self.trackerState;
    if (!model || model->songPlaybackActive) return;
    const auto range = [self effectiveGridSelection];
    if (range.firstTrack != range.lastTrack) return;
    [self.owner capturePhraseTrack:range.firstTrack
        firstRow:range.firstRow lastRow:range.lastRow];
}

- (void)placePhraseAtSelection:(NSMenuItem*)sender
{
    auto* model = self.trackerState;
    if (!model || model->songPlaybackActive) return;
    const auto range = [self effectiveGridSelection];
    if (range.firstTrack != range.lastTrack) return;
    if (sender.tag == 2)
        model->selectedPhrase = std::min<std::size_t>(model->lastPlacedPhrase,
            s3g::tracker::kPhraseLibrarySlots - 1u);
    [self.owner placeSelectedPhraseTrack:range.firstTrack row:range.firstRow
        merge:sender.tag == 1];
}

- (void)placePhraseSlotAtSelection:(NSMenuItem*)sender
{
    auto* model = self.trackerState;
    if (!model || sender.tag < 0
        || static_cast<std::size_t>(sender.tag)
            >= model->phraseLibrary.phrases.size()) return;
    model->selectedPhrase = static_cast<std::size_t>(sender.tag);
    const auto range = [self effectiveGridSelection];
    [self.owner placeSelectedPhraseTrack:range.firstTrack row:range.firstRow
        merge:NO];
}

- (void)reverseGridSelection:(NSMenuItem*)sender
{
    (void)sender;
    auto* model = self.trackerState;
    if (!model || model->songPlaybackActive) return;
    auto candidate = model->session.pattern;
    const auto range = [self effectiveGridSelection];
    const auto fields = gridFieldCount(model->sequenceColumnsExpanded);
    const auto first = _gridSelection.active
        ? _gridSelection.firstColumn(fields)
        : gridClipboardColumn(range.firstTrack, range.firstField, fields);
    const auto last = _gridSelection.active
        ? _gridSelection.lastColumn(fields) : first;
    for (std::size_t column = first; column <= last; ++column) {
        std::size_t track = 0u, field = 0u;
        s3g::tracker::app::gridAddressForClipboardColumn(
            column, fields, track, field);
        for (std::size_t offset = 0u; offset < range.rowCount() / 2u; ++offset) {
            const auto a = trackerGridCellAt(candidate.tracks[track], field,
                range.firstRow + offset);
            const auto b = trackerGridCellAt(candidate.tracks[track], field,
                range.lastRow - offset);
            writeTrackerGridCell(candidate.tracks[track], field,
                range.firstRow + offset, b);
            writeTrackerGridCell(candidate.tracks[track], field,
                range.lastRow - offset, a);
        }
    }
    model->session.pattern = std::move(candidate);
    [self.owner modulePatternChanged];
}

- (void)rotateGridSelection:(NSMenuItem*)sender
{
    auto* model = self.trackerState;
    if (!model || model->songPlaybackActive || sender.tag == 0) return;
    auto candidate = model->session.pattern;
    const auto range = [self effectiveGridSelection];
    if (range.rowCount() < 2u) return;
    const auto fields = gridFieldCount(model->sequenceColumnsExpanded);
    const auto first = _gridSelection.active
        ? _gridSelection.firstColumn(fields)
        : gridClipboardColumn(range.firstTrack, range.firstField, fields);
    const auto last = _gridSelection.active
        ? _gridSelection.lastColumn(fields) : first;
    for (std::size_t column = first; column <= last; ++column) {
        std::size_t track = 0u, field = 0u;
        s3g::tracker::app::gridAddressForClipboardColumn(
            column, fields, track, field);
        std::vector<TrackerGridCell> cells;
        cells.reserve(range.rowCount());
        for (std::size_t row = range.firstRow; row <= range.lastRow; ++row)
            cells.push_back(trackerGridCellAt(candidate.tracks[track], field, row));
        if (sender.tag < 0)
            std::rotate(cells.begin(), cells.begin() + 1, cells.end());
        else
            std::rotate(cells.rbegin(), cells.rbegin() + 1, cells.rend());
        for (std::size_t offset = 0u; offset < cells.size(); ++offset)
            writeTrackerGridCell(candidate.tracks[track], field,
                range.firstRow + offset, cells[offset]);
    }
    model->session.pattern = std::move(candidate);
    [self.owner modulePatternChanged];
}

- (void)adjustSelectedValues:(NSMenuItem*)sender
{
    auto* model = self.trackerState;
    if (!model || model->songPlaybackActive || sender.tag == 0) return;
    auto candidate = model->session.pattern;
    const auto range = [self effectiveGridSelection];
    const auto fields = gridFieldCount(model->sequenceColumnsExpanded);
    const auto first = _gridSelection.active
        ? _gridSelection.firstColumn(fields)
        : gridClipboardColumn(range.firstTrack, range.firstField, fields);
    const auto last = _gridSelection.active
        ? _gridSelection.lastColumn(fields) : first;
    const float delta = static_cast<float>(sender.tag) / 127.0f;
    BOOL changed = NO;
    for (std::size_t column = first; column <= last; ++column) {
        std::size_t track = 0u, field = 0u;
        s3g::tracker::app::gridAddressForClipboardColumn(
            column, fields, track, field);
        for (std::size_t row = range.firstRow; row <= range.lastRow; ++row) {
            if (field == 1u) {
                auto cell = std::get<ValueCell>(trackerGridCellAt(
                    candidate.tracks[track], field, row));
                if (cell.state != ValueCellState::Value) continue;
                std::array<float, s3g::tracker::kMaximumNoteVoices> voices {};
                for (std::size_t voice = 0u; voice < cell.valueVoiceCount(); ++voice)
                    voices[voice] = std::clamp(cell.valueVoice(voice) + delta,
                        0.0f, 1.0f);
                writeTrackerGridCell(candidate.tracks[track], field, row,
                    ValueCell::withValues(voices, cell.valueVoiceCount()));
                changed = YES;
            } else if (gridFieldIsSequence(field)
                && !gridFieldIsSequenceAction(field)) {
                auto cell = std::get<FxValueCell>(trackerGridCellAt(
                    candidate.tracks[track], field, row));
                if (cell.state != FxValueCellState::Value) continue;
                std::array<float, s3g::tracker::kMaximumNoteVoices> voices {};
                for (std::size_t voice = 0u; voice < cell.valueVoiceCount(); ++voice)
                    voices[voice] = std::clamp(cell.valueVoice(voice) + delta,
                        0.0f, 1.0f);
                writeTrackerGridCell(candidate.tracks[track], field, row,
                    FxValueCell::withValues(voices, cell.valueVoiceCount()));
                changed = YES;
            }
        }
    }
    if (changed) {
        model->session.pattern = std::move(candidate);
        [self.owner modulePatternChanged];
    }
}

- (void)scaleSelectedValues:(NSMenuItem*)sender
{
    auto* model = self.trackerState;
    if (!model || model->songPlaybackActive || sender.tag <= 0) return;
    auto candidate = model->session.pattern;
    const auto range = [self effectiveGridSelection];
    const auto fields = gridFieldCount(model->sequenceColumnsExpanded);
    const auto first = _gridSelection.active
        ? _gridSelection.firstColumn(fields)
        : gridClipboardColumn(range.firstTrack, range.firstField, fields);
    const auto last = _gridSelection.active
        ? _gridSelection.lastColumn(fields) : first;
    const float scale = static_cast<float>(sender.tag) / 100.0f;
    BOOL changed = NO;
    for (std::size_t column = first; column <= last; ++column) {
        std::size_t track = 0u, field = 0u;
        s3g::tracker::app::gridAddressForClipboardColumn(
            column, fields, track, field);
        for (std::size_t row = range.firstRow; row <= range.lastRow; ++row) {
            if (field == 1u) {
                auto cell = std::get<ValueCell>(trackerGridCellAt(
                    candidate.tracks[track], field, row));
                if (cell.state != ValueCellState::Value) continue;
                std::array<float, s3g::tracker::kMaximumNoteVoices> voices {};
                for (std::size_t voice = 0u; voice < cell.valueVoiceCount(); ++voice)
                    voices[voice] = std::clamp(cell.valueVoice(voice) * scale,
                        0.0f, 1.0f);
                writeTrackerGridCell(candidate.tracks[track], field, row,
                    ValueCell::withValues(voices, cell.valueVoiceCount()));
                changed = YES;
            } else if (gridFieldIsSequence(field)
                && !gridFieldIsSequenceAction(field)) {
                auto cell = std::get<FxValueCell>(trackerGridCellAt(
                    candidate.tracks[track], field, row));
                if (cell.state != FxValueCellState::Value) continue;
                std::array<float, s3g::tracker::kMaximumNoteVoices> voices {};
                for (std::size_t voice = 0u; voice < cell.valueVoiceCount(); ++voice)
                    voices[voice] = std::clamp(cell.valueVoice(voice) * scale,
                        0.0f, 1.0f);
                writeTrackerGridCell(candidate.tracks[track], field, row,
                    FxValueCell::withValues(voices, cell.valueVoiceCount()));
                changed = YES;
            }
        }
    }
    if (changed) {
        model->session.pattern = std::move(candidate);
        [self.owner modulePatternChanged];
    }
}

- (void)quantizeSelectedMicroTime:(NSMenuItem*)sender
{
    auto* model = self.trackerState;
    if (!model || model->songPlaybackActive || sender.tag == 0) return;
    auto candidate = model->session.pattern;
    const auto range = [self effectiveGridSelection];
    const auto fields = gridFieldCount(model->sequenceColumnsExpanded);
    const auto first = _gridSelection.active
        ? _gridSelection.firstColumn(fields)
        : gridClipboardColumn(range.firstTrack, range.firstField, fields);
    const auto last = _gridSelection.active
        ? _gridSelection.lastColumn(fields) : first;
    const bool nudge = std::abs(sender.tag) > 1000;
    const float strength = nudge ? 0.0f : std::clamp(
        static_cast<float>(sender.tag) / 100.0f, 0.0f, 1.0f);
    const float rangeMs = static_cast<float>(std::max(1.0,
        model->session.transport.microTimingRangeMilliseconds));
    const float nudgeDelta = nudge
        ? static_cast<float>(sender.tag < 0 ? -1.0 : 1.0)
            / (2.0f * rangeMs)
        : 0.0f;
    BOOL changed = NO;
    for (std::size_t column = first; column <= last; ++column) {
        std::size_t track = 0u, field = 0u;
        s3g::tracker::app::gridAddressForClipboardColumn(
            column, fields, track, field);
        if (!gridFieldIsSequence(field) || gridFieldIsSequenceAction(field))
            continue;
        auto& pair = candidate.tracks[track].fxPairs[gridSequencePair(field)];
        for (std::size_t row = range.firstRow; row <= range.lastRow; ++row) {
            if (row >= pair.values.size()
                || pair.values[row].state != FxValueCellState::Value) continue;
            bool microTime = false;
            for (std::size_t scan = 0u; scan <= row && scan < pair.actions.size(); ++scan) {
                if (pair.actions[scan].state == FxActionCellState::Sequencer)
                    microTime = pair.actions[scan].sequencerAction
                        == SequencerAction::MicroTime;
            }
            if (!microTime) continue;
            auto& cell = pair.values[row];
            std::array<float, s3g::tracker::kMaximumNoteVoices> voices {};
            for (std::size_t voice = 0u; voice < cell.valueVoiceCount(); ++voice)
                voices[voice] = std::clamp(nudge
                        ? cell.valueVoice(voice) + nudgeDelta
                        : cell.valueVoice(voice)
                            + (0.5f - cell.valueVoice(voice)) * strength,
                    0.0f, 1.0f);
            cell = FxValueCell::withValues(voices, cell.valueVoiceCount());
            changed = YES;
        }
    }
    if (changed) {
        model->session.pattern = std::move(candidate);
        [self.owner modulePatternChanged];
    }
}

- (void)fillSelectionFromEdge:(NSMenuItem*)sender
{
    auto* model = self.trackerState;
    if (!model || model->songPlaybackActive) return;
    auto candidate = model->session.pattern;
    const auto range = [self effectiveGridSelection];
    const auto fields = gridFieldCount(model->sequenceColumnsExpanded);
    const auto firstColumn = _gridSelection.active
        ? _gridSelection.firstColumn(fields)
        : gridClipboardColumn(range.firstTrack, range.firstField, fields);
    const auto lastColumn = _gridSelection.active
        ? _gridSelection.lastColumn(fields) : firstColumn;
    const auto sourceRow = sender.tag < 0 ? range.lastRow : range.firstRow;
    for (std::size_t column = firstColumn; column <= lastColumn; ++column) {
        std::size_t track = 0u, field = 0u;
        s3g::tracker::app::gridAddressForClipboardColumn(
            column, fields, track, field);
        const auto source = trackerGridCellAt(candidate.tracks[track],
            field, sourceRow);
        for (std::size_t row = range.firstRow; row <= range.lastRow; ++row)
            writeTrackerGridCell(candidate.tracks[track], field, row, source);
    }
    model->session.pattern = std::move(candidate);
    [self.owner modulePatternChanged];
}

- (void)fillSelectionSeries:(NSMenuItem*)sender
{
    (void)sender;
    auto* model = self.trackerState;
    if (!model || model->songPlaybackActive) return;
    auto candidate = model->session.pattern;
    const auto range = [self effectiveGridSelection];
    if (range.rowCount() < 2u) return;
    const auto fields = gridFieldCount(model->sequenceColumnsExpanded);
    const auto firstColumn = _gridSelection.active
        ? _gridSelection.firstColumn(fields)
        : gridClipboardColumn(range.firstTrack, range.firstField, fields);
    const auto lastColumn = _gridSelection.active
        ? _gridSelection.lastColumn(fields) : firstColumn;
    bool changed = false;
    for (std::size_t column = firstColumn; column <= lastColumn; ++column) {
        std::size_t track = 0u, field = 0u;
        s3g::tracker::app::gridAddressForClipboardColumn(
            column, fields, track, field);
        const auto first = trackerGridCellAt(candidate.tracks[track],
            field, range.firstRow);
        const auto last = trackerGridCellAt(candidate.tracks[track],
            field, range.lastRow);
        for (std::size_t row = range.firstRow + 1u; row < range.lastRow; ++row) {
            const float phase = static_cast<float>(row - range.firstRow)
                / static_cast<float>(range.lastRow - range.firstRow);
            if (field == 0u) {
                const auto& a = std::get<NoteCell>(first);
                const auto& b = std::get<NoteCell>(last);
                if (a.state != NoteCellState::Note
                    || b.state != NoteCellState::Note) continue;
                const auto voices = std::max(a.noteVoiceCount(),
                    b.noteVoiceCount());
                std::array<uint8_t, s3g::tracker::kMaximumNoteVoices> notes {};
                for (std::size_t voice = 0u; voice < voices; ++voice) {
                    const int av = a.noteVoice(std::min(
                        voice, a.noteVoiceCount() - 1u));
                    const int bv = b.noteVoice(std::min(
                        voice, b.noteVoiceCount() - 1u));
                    notes[voice] = static_cast<uint8_t>(std::clamp<int>(
                        static_cast<int>(std::lround(av + (bv - av) * phase)),
                        0, 127));
                }
                std::sort(notes.begin(), notes.begin()
                    + static_cast<std::ptrdiff_t>(voices));
                writeTrackerGridCell(candidate.tracks[track], field, row,
                    NoteCell::withNotes(notes, voices));
                changed = true;
            } else if (field == 1u) {
                const auto& a = std::get<ValueCell>(first);
                const auto& b = std::get<ValueCell>(last);
                if (a.state != ValueCellState::Value
                    || b.state != ValueCellState::Value) continue;
                const auto voices = std::max(a.valueVoiceCount(),
                    b.valueVoiceCount());
                std::array<float, s3g::tracker::kMaximumNoteVoices> values {};
                for (std::size_t voice = 0u; voice < voices; ++voice) {
                    const float av = a.valueVoice(std::min(
                        voice, a.valueVoiceCount() - 1u));
                    const float bv = b.valueVoice(std::min(
                        voice, b.valueVoiceCount() - 1u));
                    values[voice] = std::clamp(av + (bv - av) * phase,
                        0.0f, 1.0f);
                }
                writeTrackerGridCell(candidate.tracks[track], field, row,
                    ValueCell::withValues(values, voices));
                changed = true;
            } else if (gridFieldIsGate(field)) {
                const auto& a = std::get<GateCell>(first);
                const auto& b = std::get<GateCell>(last);
                if (a.voiceCount == 0u || b.voiceCount == 0u) continue;
                const auto voices = std::max(a.gateVoiceCount(),
                    b.gateVoiceCount());
                std::array<GateVoice, s3g::tracker::kMaximumNoteVoices> gates {};
                bool numeric = true;
                for (std::size_t voice = 0u; voice < voices; ++voice) {
                    const auto av = a.gateVoice(std::min(
                        voice, a.gateVoiceCount() - 1u));
                    const auto bv = b.gateVoice(std::min(
                        voice, b.gateVoiceCount() - 1u));
                    if (av.mode != GateVoiceMode::Rows
                        || bv.mode != GateVoiceMode::Rows) {
                        numeric = false; break;
                    }
                    gates[voice] = { GateVoiceMode::Rows,
                        std::clamp(av.rows + (bv.rows - av.rows) * phase,
                            0.01f, 64.0f) };
                }
                if (!numeric) continue;
                writeTrackerGridCell(candidate.tracks[track], field, row,
                    GateCell::withVoices(gates, voices));
                changed = true;
            } else if (!gridFieldIsSequenceAction(field)) {
                const auto& a = std::get<FxValueCell>(first);
                const auto& b = std::get<FxValueCell>(last);
                if (a.state != FxValueCellState::Value
                    || b.state != FxValueCellState::Value) continue;
                const auto voices = std::max(a.valueVoiceCount(),
                    b.valueVoiceCount());
                std::array<float, s3g::tracker::kMaximumNoteVoices> values {};
                for (std::size_t voice = 0u; voice < voices; ++voice) {
                    const float av = a.valueVoice(std::min(
                        voice, a.valueVoiceCount() - 1u));
                    const float bv = b.valueVoice(std::min(
                        voice, b.valueVoiceCount() - 1u));
                    values[voice] = std::clamp(
                        av + (bv - av) * phase, 0.0f, 1.0f);
                }
                writeTrackerGridCell(candidate.tracks[track], field, row,
                    FxValueCell::withValues(values, voices));
                changed = true;
            }
        }
    }
    if (!changed) { NSBeep(); return; }
    model->session.pattern = std::move(candidate);
    [self.owner modulePatternChanged];
}

- (void)repeatGridSelection:(NSMenuItem*)sender
{
    auto* model = self.trackerState;
    if (!model || model->songPlaybackActive) return;
    auto candidate = model->session.pattern;
    const auto range = [self effectiveGridSelection];
    const auto fields = gridFieldCount(model->sequenceColumnsExpanded);
    const auto firstColumn = _gridSelection.active
        ? _gridSelection.firstColumn(fields)
        : gridClipboardColumn(range.firstTrack, range.firstField, fields);
    const auto lastColumn = _gridSelection.active
        ? _gridSelection.lastColumn(fields) : firstColumn;
    const auto rows = range.rowCount();
    std::size_t repeats = sender.tag < 0
        ? (256u - range.lastRow - 1u) / rows
        : static_cast<std::size_t>(sender.tag);
    if (repeats == 0u) return;
    std::vector<TrackerGridCell> source;
    source.reserve(rows * (lastColumn - firstColumn + 1u));
    for (std::size_t row = range.firstRow; row <= range.lastRow; ++row)
        for (std::size_t column = firstColumn; column <= lastColumn; ++column) {
            std::size_t track = 0u, field = 0u;
            s3g::tracker::app::gridAddressForClipboardColumn(
                column, fields, track, field);
            source.push_back(trackerGridCellAt(
                candidate.tracks[track], field, row));
        }
    const auto columns = lastColumn - firstColumn + 1u;
    std::size_t finalRow = range.lastRow;
    for (std::size_t repeat = 0u; repeat < repeats; ++repeat) {
        for (std::size_t rowOffset = 0u; rowOffset < rows; ++rowOffset) {
            const auto row = range.lastRow + 1u + repeat * rows + rowOffset;
            if (row >= 256u) break;
            finalRow = row;
            for (std::size_t columnOffset = 0u;
                 columnOffset < columns; ++columnOffset) {
                std::size_t track = 0u, field = 0u;
                s3g::tracker::app::gridAddressForClipboardColumn(
                    firstColumn + columnOffset, fields, track, field);
                writeTrackerGridCell(candidate.tracks[track], field, row,
                    source[rowOffset * columns + columnOffset]);
            }
        }
    }
    candidate.visibleRows = std::max(candidate.visibleRows, finalRow + 1u);
    model->session.pattern = std::move(candidate);
    [self.owner modulePatternChanged];
}

- (void)shiftSelectionCells:(NSMenuItem*)sender
{
    auto* model = self.trackerState;
    if (!model || model->songPlaybackActive) return;
    auto candidate = model->session.pattern;
    const auto range = [self effectiveGridSelection];
    const auto fields = gridFieldCount(model->sequenceColumnsExpanded);
    const auto firstColumn = _gridSelection.active
        ? _gridSelection.firstColumn(fields)
        : gridClipboardColumn(range.firstTrack, range.firstField, fields);
    const auto lastColumn = _gridSelection.active
        ? _gridSelection.lastColumn(fields) : firstColumn;
    const auto count = range.rowCount();
    const bool insert = sender.tag > 0;
    const auto edit = [=](auto& values, const auto& blank,
                          ColumnDefinition& definition) {
        if (insert && values.size() < range.firstRow)
            values.resize(range.firstRow, blank);
        const auto position = std::min(range.firstRow, values.size());
        if (insert) {
            values.insert(values.begin() + static_cast<std::ptrdiff_t>(position),
                count, blank);
            if (values.size() > 256u) values.resize(256u);
            definition.length = std::min<std::size_t>(
                256u, std::max(definition.length, range.firstRow) + count);
        } else {
            const auto end = std::min(values.size(), range.lastRow + 1u);
            if (position < end)
                values.erase(values.begin() + static_cast<std::ptrdiff_t>(position),
                    values.begin() + static_cast<std::ptrdiff_t>(end));
            const auto removed = range.firstRow < definition.length
                ? std::min(count, definition.length - range.firstRow) : 0u;
            definition.length = std::max<std::size_t>(
                1u, definition.length - removed);
        }
    };
    for (std::size_t column = firstColumn; column <= lastColumn; ++column) {
        std::size_t trackIndex = 0u, field = 0u;
        s3g::tracker::app::gridAddressForClipboardColumn(
            column, fields, trackIndex, field);
        auto& track = candidate.tracks[trackIndex];
        if (field == 0u)
            edit(track.notes, NoteCell::rest(), track.noteColumn);
        else if (field == 1u)
            edit(track.velocities, ValueCell::defaultValue(),
                track.velocityColumn);
        else if (gridFieldIsGate(field))
            edit(track.gates, GateCell::defaultValue(), track.gateColumn);
        else {
            auto& pair = track.fxPairs[gridSequencePair(field)];
            if (gridFieldIsSequenceAction(field))
                edit(pair.actions, FxActionCell::empty(), pair.actionColumn);
            else edit(pair.values, FxValueCell::previous(), pair.valueColumn);
        }
    }
    model->session.pattern = std::move(candidate);
    [self.owner modulePatternChanged];
}

- (void)moveGridSelection:(NSMenuItem*)sender
{
    auto* model = self.trackerState;
    if (!model || model->songPlaybackActive) return;
    int offset = static_cast<int>(sender.tag);
    if (std::abs(offset) == 100) offset = offset < 0
        ? -static_cast<int>(std::clamp<uint32_t>(model->trackerRowJump, 1u, 16u))
        : static_cast<int>(std::clamp<uint32_t>(model->trackerRowJump, 1u, 16u));
    const auto range = [self effectiveGridSelection];
    const int destinationFirst = static_cast<int>(range.firstRow) + offset;
    const int destinationLast = static_cast<int>(range.lastRow) + offset;
    if (destinationFirst < 0 || destinationLast >= 256) { NSBeep(); return; }
    auto candidate = model->session.pattern;
    const auto fields = gridFieldCount(model->sequenceColumnsExpanded);
    const auto firstColumn = _gridSelection.active
        ? _gridSelection.firstColumn(fields)
        : gridClipboardColumn(range.firstTrack, range.firstField, fields);
    const auto lastColumn = _gridSelection.active
        ? _gridSelection.lastColumn(fields) : firstColumn;
    const auto columns = lastColumn - firstColumn + 1u;
    std::vector<TrackerGridCell> cells;
    cells.reserve(range.rowCount() * columns);
    for (std::size_t row = range.firstRow; row <= range.lastRow; ++row)
        for (std::size_t column = firstColumn; column <= lastColumn; ++column) {
            std::size_t track = 0u, field = 0u;
            s3g::tracker::app::gridAddressForClipboardColumn(
                column, fields, track, field);
            cells.push_back(trackerGridCellAt(candidate.tracks[track], field, row));
            writeTrackerGridCell(candidate.tracks[track], field, row,
                blankTrackerGridCell(field));
        }
    for (std::size_t rowOffset = 0u; rowOffset < range.rowCount(); ++rowOffset)
        for (std::size_t columnOffset = 0u; columnOffset < columns;
             ++columnOffset) {
            std::size_t track = 0u, field = 0u;
            s3g::tracker::app::gridAddressForClipboardColumn(
                firstColumn + columnOffset, fields, track, field);
            writeTrackerGridCell(candidate.tracks[track], field,
                static_cast<std::size_t>(destinationFirst) + rowOffset,
                cells[rowOffset * columns + columnOffset]);
        }
    candidate.visibleRows = std::max(candidate.visibleRows,
        static_cast<std::size_t>(destinationLast) + 1u);
    model->session.pattern = std::move(candidate);
    _gridSelection.anchorRow = static_cast<std::size_t>(
        static_cast<int>(_gridSelection.anchorRow) + offset);
    _gridSelection.focusRow = static_cast<std::size_t>(
        static_cast<int>(_gridSelection.focusRow) + offset);
    model->session.selectedRow = static_cast<std::size_t>(destinationFirst);
    [self.owner modulePatternChanged];
}

- (void)stretchGridSelection:(NSMenuItem*)sender
{
    auto* model = self.trackerState;
    if (!model || model->songPlaybackActive) return;
    const auto range = [self effectiveGridSelection];
    if (range.rowCount() < 2u) return;
    const double factor = static_cast<double>(sender.tag) / 100.0;
    const auto destinationRows = std::clamp<std::size_t>(
        static_cast<std::size_t>(std::lround(range.rowCount() * factor)),
        1u, 256u - range.firstRow);
    auto candidate = model->session.pattern;
    const auto fields = gridFieldCount(model->sequenceColumnsExpanded);
    const auto firstColumn = _gridSelection.active
        ? _gridSelection.firstColumn(fields)
        : gridClipboardColumn(range.firstTrack, range.firstField, fields);
    const auto lastColumn = _gridSelection.active
        ? _gridSelection.lastColumn(fields) : firstColumn;
    for (std::size_t column = firstColumn; column <= lastColumn; ++column) {
        std::size_t track = 0u, field = 0u;
        s3g::tracker::app::gridAddressForClipboardColumn(
            column, fields, track, field);
        std::vector<std::pair<std::size_t, TrackerGridCell>> occupied;
        for (std::size_t row = range.firstRow; row <= range.lastRow; ++row) {
            auto cell = trackerGridCellAt(candidate.tracks[track], field, row);
            if (!trackerGridCellEmpty(cell))
                occupied.emplace_back(row - range.firstRow, cell);
            writeTrackerGridCell(candidate.tracks[track], field, row,
                blankTrackerGridCell(field));
        }
        for (const auto& event : occupied) {
            const auto mapped = range.rowCount() <= 1u ? 0u
                : static_cast<std::size_t>(std::lround(
                    static_cast<double>(event.first)
                    * static_cast<double>(destinationRows - 1u)
                    / static_cast<double>(range.rowCount() - 1u)));
            writeTrackerGridCell(candidate.tracks[track], field,
                range.firstRow + mapped, event.second);
        }
    }
    candidate.visibleRows = std::max(candidate.visibleRows,
        range.firstRow + destinationRows);
    model->session.pattern = std::move(candidate);
    _gridSelection.focusRow = range.firstRow + destinationRows - 1u;
    _gridSelection.anchorRow = range.firstRow;
    [self.owner modulePatternChanged];
}

- (void)materializeGridSelection:(NSMenuItem*)sender
{
    (void)sender;
    auto* model = self.trackerState;
    if (!model || model->songPlaybackActive) return;
    auto candidate = model->session.pattern;
    const auto range = [self effectiveGridSelection];
    const auto fields = gridFieldCount(model->sequenceColumnsExpanded);
    const auto firstColumn = _gridSelection.active
        ? _gridSelection.firstColumn(fields)
        : gridClipboardColumn(range.firstTrack, range.firstField, fields);
    const auto lastColumn = _gridSelection.active
        ? _gridSelection.lastColumn(fields) : firstColumn;
    bool changed = false;
    for (std::size_t column = firstColumn; column <= lastColumn; ++column) {
        std::size_t trackIndex = 0u, field = 0u;
        s3g::tracker::app::gridAddressForClipboardColumn(
            column, fields, trackIndex, field);
        auto& track = candidate.tracks[trackIndex];
        TrackerGridCell memory = blankTrackerGridCell(field);
        bool hasMemory = false;
        for (std::size_t row = 0u; row <= range.lastRow; ++row) {
            auto cell = trackerGridCellAt(track, field, row);
            if (field == 0u) {
                auto note = std::get<NoteCell>(cell);
                if (note.state == NoteCellState::Note) {
                    memory = note; hasMemory = true;
                } else if (row >= range.firstRow && hasMemory
                    && (note.state == NoteCellState::RetriggerPrevious
                        || note.state == NoteCellState::Hold)) {
                    writeTrackerGridCell(track, field, row, memory);
                    changed = true;
                }
            } else if (field == 1u) {
                auto value = std::get<ValueCell>(cell);
                if (value.state == ValueCellState::Value) {
                    memory = value; hasMemory = true;
                } else if (value.state == ValueCellState::Default) {
                    memory = ValueCell::withValue(0.787f); hasMemory = true;
                    if (row >= range.firstRow) {
                        writeTrackerGridCell(track, field, row, memory);
                        changed = true;
                    }
                } else if (row >= range.firstRow && hasMemory) {
                    writeTrackerGridCell(track, field, row, memory);
                    changed = true;
                }
            } else if (gridFieldIsGate(field)) {
                // Gate defaults are intentional fallbacks to the transport gate;
                // there is no previous-value state to materialize.
                continue;
            } else if (gridFieldIsSequenceAction(field)) {
                auto action = std::get<FxActionCell>(cell);
                if (action.state != FxActionCellState::Empty
                    && action.state != FxActionCellState::Previous) {
                    memory = action; hasMemory = true;
                } else if (row >= range.firstRow && hasMemory
                    && action.state == FxActionCellState::Previous) {
                    writeTrackerGridCell(track, field, row, memory);
                    changed = true;
                }
            } else {
                auto value = std::get<FxValueCell>(cell);
                if (value.state == FxValueCellState::Value) {
                    memory = value; hasMemory = true;
                } else if (row >= range.firstRow) {
                    if (!hasMemory) memory = FxValueCell::withValue(0.0f);
                    writeTrackerGridCell(track, field, row, memory);
                    changed = true;
                }
            }
        }
    }
    if (!changed) return;
    model->session.pattern = std::move(candidate);
    [self.owner modulePatternChanged];
}

- (void)findReplaceGridSelection:(NSMenuItem*)sender
{
    (void)sender;
    auto* model = self.trackerState;
    if (!model || model->songPlaybackActive) return;
    auto candidate = model->session.pattern;
    const auto range = [self effectiveGridSelection];
    if (range.rowCount() < 2u) return;
    const auto fields = gridFieldCount(model->sequenceColumnsExpanded);
    const auto firstColumn = _gridSelection.active
        ? _gridSelection.firstColumn(fields)
        : gridClipboardColumn(range.firstTrack, range.firstField, fields);
    const auto lastColumn = _gridSelection.active
        ? _gridSelection.lastColumn(fields) : firstColumn;
    std::size_t changed = 0u;
    for (std::size_t column = firstColumn; column <= lastColumn; ++column) {
        std::size_t track = 0u, field = 0u;
        s3g::tracker::app::gridAddressForClipboardColumn(
            column, fields, track, field);
        const auto find = trackerGridCellAt(candidate.tracks[track],
            field, range.firstRow);
        const auto replacement = trackerGridCellAt(candidate.tracks[track],
            field, range.lastRow);
        for (std::size_t row = range.firstRow; row <= range.lastRow; ++row) {
            const auto current = trackerGridCellAt(
                candidate.tracks[track], field, row);
            if (!trackerGridCellsEqual(current, find)
                || trackerGridCellsEqual(current, replacement)) continue;
            writeTrackerGridCell(candidate.tracks[track], field, row,
                replacement);
            ++changed;
        }
    }
    if (changed == 0u) return;
    model->session.pattern = std::move(candidate);
    [self.owner modulePatternChanged];
}

- (void)swapGridSelectionWithNextLane:(NSMenuItem*)sender
{
    (void)sender;
    auto* model = self.trackerState;
    if (!model || model->songPlaybackActive) return;
    auto candidate = model->session.pattern;
    const auto range = [self effectiveGridSelection];
    const auto fields = gridFieldCount(model->sequenceColumnsExpanded);
    const auto firstColumn = _gridSelection.active
        ? _gridSelection.firstColumn(fields)
        : gridClipboardColumn(range.firstTrack, range.firstField, fields);
    const auto lastColumn = _gridSelection.active
        ? _gridSelection.lastColumn(fields) : firstColumn;
    const auto lane = firstColumn / fields;
    if (lastColumn / fields != lane || lane + 1u >= candidate.tracks.size())
        return;
    for (std::size_t row = range.firstRow; row <= range.lastRow; ++row)
        for (std::size_t column = firstColumn; column <= lastColumn; ++column) {
            const auto field = column % fields;
            const auto a = trackerGridCellAt(candidate.tracks[lane], field, row);
            const auto b = trackerGridCellAt(candidate.tracks[lane + 1u], field, row);
            writeTrackerGridCell(candidate.tracks[lane], field, row, b);
            writeTrackerGridCell(candidate.tracks[lane + 1u], field, row, a);
        }
    model->session.pattern = std::move(candidate);
    [self.owner modulePatternChanged];
}

- (void)pasteGridSelectionSpecial:(NSMenuItem*)sender
{
    if (sender.tag == 0) { [self trackerPaste:sender]; return; }
    auto* model = self.trackerState;
    if (!model || model->songPlaybackActive || _copiedGridCells.empty()
        || _copiedColumnTypes.empty() || self.copiedRowCount == 0u) return;
    const auto range = [self effectiveGridSelection];
    const auto fields = gridFieldCount(model->sequenceColumnsExpanded);
    const auto firstColumn = _gridSelection.active
        ? _gridSelection.firstColumn(fields)
        : gridClipboardColumn(range.firstTrack, range.firstField, fields);
    if (firstColumn + _copiedColumnTypes.size()
            > model->session.pattern.tracks.size() * fields
        || range.firstRow + self.copiedRowCount > 256u) {
        NSBeep(); return;
    }
    for (std::size_t offset = 0u; offset < _copiedColumnTypes.size(); ++offset) {
        const auto sourceType = _copiedColumnTypes[offset];
        const bool applies = sender.tag == 3 ? sourceType == 0u
            : sender.tag == 4 ? sourceType == 1u || sourceType == 3u
            : sender.tag == 2 ? sourceType == 0u : true;
        if (applies && sourceType
                != gridClipboardFieldType((firstColumn + offset) % fields)) {
            NSBeep(); return;
        }
    }
    auto candidate = model->session.pattern;
    const auto columns = _copiedColumnTypes.size();
    std::size_t changed = 0u;
    for (std::size_t rowOffset = 0u; rowOffset < self.copiedRowCount;
         ++rowOffset) {
        for (std::size_t columnOffset = 0u; columnOffset < columns;
             ++columnOffset) {
            const auto sourceType = _copiedColumnTypes[columnOffset];
            if ((sender.tag == 3 && sourceType != 0u)
                || (sender.tag == 4 && sourceType != 1u && sourceType != 3u)
                || (sender.tag == 2 && sourceType != 0u)) continue;
            std::size_t track = 0u, field = 0u;
            s3g::tracker::app::gridAddressForClipboardColumn(
                firstColumn + columnOffset, fields, track, field);
            const auto row = range.firstRow + rowOffset;
            const auto current = trackerGridCellAt(candidate.tracks[track],
                field, row);
            if (sender.tag == 1 && !trackerGridCellEmpty(current)) continue;
            TrackerGridCell replacement = _copiedGridCells[
                rowOffset * columns + columnOffset];
            if (sender.tag == 2) {
                const auto& rhythm = std::get<NoteCell>(replacement);
                if (rhythm.state == NoteCellState::Note) {
                    const auto& existing = std::get<NoteCell>(current);
                    replacement = existing.state == NoteCellState::Note
                        ? existing : NoteCell::withNote(
                            laneDefaultNote(model->session, track));
                }
            }
            if (trackerGridCellsEqual(current, replacement)) continue;
            writeTrackerGridCell(candidate.tracks[track], field, row,
                replacement);
            ++changed;
        }
    }
    if (changed == 0u) return;
    candidate.visibleRows = std::max(candidate.visibleRows,
        range.firstRow + self.copiedRowCount);
    model->session.pattern = std::move(candidate);
    [self.owner modulePatternChanged];
}

- (void)showGridSelectionStatistics:(NSMenuItem*)sender
{
    (void)sender;
    auto* model = self.trackerState;
    if (!model || model->session.pattern.tracks.empty()) return;
    const auto range = [self effectiveGridSelection];
    const auto fields = gridFieldCount(model->sequenceColumnsExpanded);
    const auto firstColumn = _gridSelection.active
        ? _gridSelection.firstColumn(fields)
        : gridClipboardColumn(range.firstTrack, range.firstField, fields);
    const auto lastColumn = _gridSelection.active
        ? _gridSelection.lastColumn(fields) : firstColumn;
    std::size_t hits = 0u, voices = 0u, values = 0u, actions = 0u;
    double velocityTotal = 0.0;
    std::size_t velocityCount = 0u;
    uint8_t low = 127u, high = 0u;
    for (std::size_t row = range.firstRow; row <= range.lastRow; ++row)
        for (std::size_t column = firstColumn; column <= lastColumn; ++column) {
            std::size_t track = 0u, field = 0u;
            s3g::tracker::app::gridAddressForClipboardColumn(
                column, fields, track, field);
            const auto cell = trackerGridCellAt(
                model->session.pattern.tracks[track], field, row);
            if (const auto* note = std::get_if<NoteCell>(&cell)) {
                if (note->state == NoteCellState::Note) {
                    ++hits;
                    voices += note->noteVoiceCount();
                    for (std::size_t voice = 0u;
                         voice < note->noteVoiceCount(); ++voice) {
                        low = std::min(low, note->noteVoice(voice));
                        high = std::max(high, note->noteVoice(voice));
                    }
                }
            } else if (const auto* value = std::get_if<ValueCell>(&cell)) {
                if (value->state == ValueCellState::Value) {
                    ++values;
                    for (std::size_t voice = 0u;
                         voice < value->valueVoiceCount(); ++voice) {
                        velocityTotal += value->valueVoice(voice);
                        ++velocityCount;
                    }
                }
            } else if (const auto* action = std::get_if<FxActionCell>(&cell)) {
                if (action->state != FxActionCellState::Empty) ++actions;
            } else if (const auto* gate = std::get_if<GateCell>(&cell)) {
                if (gate->voiceCount > 0u) ++values;
            } else if (std::get<FxValueCell>(cell).state
                    == FxValueCellState::Value) ++values;
        }
    NSString* pitchRange = voices > 0u
        ? [NSString stringWithFormat:@"%@ %03u – %@ %03u",
            midiNoteName(low), low, midiNoteName(high), high] : @"—";
    NSString* detail = [NSString stringWithFormat:
        @"%lu row%@ × %lu column%@\n%lu note onset%@ · %lu voice%@\nPitch range %@\n%lu written value%@ · %lu SEQ action%@%@",
        static_cast<unsigned long>(range.rowCount()),
        range.rowCount() == 1u ? @"" : @"s",
        static_cast<unsigned long>(lastColumn - firstColumn + 1u),
        lastColumn == firstColumn ? @"" : @"s",
        static_cast<unsigned long>(hits), hits == 1u ? @"" : @"s",
        static_cast<unsigned long>(voices), voices == 1u ? @"" : @"s",
        pitchRange, static_cast<unsigned long>(values),
        values == 1u ? @"" : @"s", static_cast<unsigned long>(actions),
        actions == 1u ? @"" : @"s", velocityCount > 0u
            ? [NSString stringWithFormat:@"\nAverage velocity %.1f%%",
                velocityTotal / static_cast<double>(velocityCount) * 100.0]
            : @""];
    NSAlert* alert = [[NSAlert alloc] init];
    alert.messageText = @"Tracker Selection";
    alert.informativeText = detail;
    [alert addButtonWithTitle:@"OK"];
    if (self.window) [alert beginSheetModalForWindow:self.window
        completionHandler:nil];
}

- (void)splitSelectedNoteColumnByPitch:(NSMenuItem*)sender
{
    (void)sender;
    auto* model = self.trackerState;
    if (!model || model->songPlaybackActive) return;
    const auto range = [self effectiveGridSelection];
    const auto fields = gridFieldCount(model->sequenceColumnsExpanded);
    const auto firstColumn = _gridSelection.active
        ? _gridSelection.firstColumn(fields)
        : gridClipboardColumn(range.firstTrack, range.firstField, fields);
    const auto lastColumn = _gridSelection.active
        ? _gridSelection.lastColumn(fields) : firstColumn;
    if (firstColumn != lastColumn || firstColumn % fields != 0u) return;
    const auto sourceLane = firstColumn / fields;
    if (sourceLane >= model->session.pattern.tracks.size()) return;
    std::vector<uint8_t> pitches;
    const auto& source = model->session.pattern.tracks[sourceLane];
    for (std::size_t row = range.firstRow; row <= range.lastRow; ++row) {
        if (row >= source.notes.size()
            || source.notes[row].state != NoteCellState::Note) continue;
        for (std::size_t voice = 0u;
             voice < source.notes[row].noteVoiceCount(); ++voice) {
            const auto note = source.notes[row].noteVoice(voice);
            if (std::find(pitches.begin(), pitches.end(), note) == pitches.end())
                pitches.push_back(note);
        }
    }
    if (pitches.size() < 2u) { NSBeep(); return; }
    if (model->session.pattern.tracks.size() + pitches.size() - 1u
        > s3g::tracker::kMaximumTrackCount) { NSBeep(); return; }
    auto candidate = model->session.pattern;
    const Track original = candidate.tracks[sourceLane];
    std::vector<std::size_t> destinationLanes { sourceLane };
    auto rows = std::max<std::size_t>(candidate.visibleRows,
        std::max({ original.notes.size(), original.instruments.size(),
            original.velocities.size(), original.gates.size(),
            original.noteColumn.length,
            original.instrumentColumn.length,
            original.velocityColumn.length, original.gateColumn.length }));
    for (const auto& pair : original.fxPairs)
        rows = std::max({ rows, pair.actions.size(), pair.values.size(),
            pair.actionColumn.length, pair.valueColumn.length });
    candidate.tracks[sourceLane].notes.resize(rows, NoteCell::rest());
    candidate.tracks[sourceLane].velocities.resize(
        rows, ValueCell::defaultValue());
    candidate.tracks[sourceLane].gates.resize(rows, GateCell::defaultValue());
    for (std::size_t index = 1u; index < pitches.size(); ++index) {
        Track split;
        split.name = (original.name.empty() ? "LANE" : original.name)
            + " · " + std::to_string(pitches[index]);
        split.velocityScale = original.velocityScale;
        split.midiChannel = original.midiChannel;
        split.destination = original.destination;
        split.initialInstrumentNodeId = original.initialInstrumentNodeId;
        split.chokeGroup = original.chokeGroup;
        split.notes.assign(rows, NoteCell::rest());
        split.instruments.assign(rows, InstrumentCell::empty());
        split.velocities.assign(rows, ValueCell::defaultValue());
        split.gates.assign(rows, GateCell::defaultValue());
        split.noteColumn = original.noteColumn;
        split.instrumentColumn = original.instrumentColumn;
        split.velocityColumn = original.velocityColumn;
        split.gateColumn = original.gateColumn;
        for (std::size_t pairIndex = 0u;
             pairIndex < split.fxPairs.size(); ++pairIndex) {
            auto& pair = split.fxPairs[pairIndex];
            const auto& originalPair = original.fxPairs[pairIndex];
            pair.actions.assign(rows, FxActionCell::empty());
            pair.values.assign(rows, FxValueCell::previous());
            pair.actionColumn = originalPair.actionColumn;
            pair.valueColumn = originalPair.valueColumn;
            pair.valueInterpolation = originalPair.valueInterpolation;
        }
        candidate.tracks.push_back(std::move(split));
        destinationLanes.push_back(candidate.tracks.size() - 1u);
    }
    for (std::size_t row = range.firstRow; row <= range.lastRow; ++row) {
        const NoteCell note = row < original.notes.size()
            ? original.notes[row] : NoteCell::rest();
        const ValueCell velocity = row < original.velocities.size()
            ? original.velocities[row] : ValueCell::defaultValue();
        const GateCell gate = row < original.gates.size()
            ? original.gates[row] : GateCell::defaultValue();
        if (note.state != NoteCellState::Note) continue;
        for (const auto lane : destinationLanes) {
            candidate.tracks[lane].notes[row] = NoteCell::rest();
            candidate.tracks[lane].velocities[row] = ValueCell::defaultValue();
            candidate.tracks[lane].gates[row] = GateCell::defaultValue();
        }
        for (std::size_t voice = 0u; voice < note.noteVoiceCount(); ++voice) {
            const auto pitch = note.noteVoice(voice);
            const auto found = std::find(pitches.begin(), pitches.end(), pitch);
            if (found == pitches.end()) continue;
            const auto index = static_cast<std::size_t>(found - pitches.begin());
            const auto lane = destinationLanes[index];
            candidate.tracks[lane].notes[row] = NoteCell::withNote(pitch);
            if (velocity.state == ValueCellState::Value) {
                const auto velocityVoice = std::min<std::size_t>(voice,
                    velocity.valueVoiceCount() - 1u);
                candidate.tracks[lane].velocities[row]
                    = ValueCell::withValue(velocity.valueVoice(velocityVoice));
            } else candidate.tracks[lane].velocities[row] = velocity;
            const auto voiceGate = gate.gateVoice(voice);
            if (voiceGate.mode == GateVoiceMode::Tie)
                candidate.tracks[lane].gates[row] = GateCell::tie();
            else if (voiceGate.mode == GateVoiceMode::Rows)
                candidate.tracks[lane].gates[row]
                    = GateCell::withRows(voiceGate.rows);
        }
        for (std::size_t index = 1u;
             index < destinationLanes.size(); ++index) {
            auto& destination = candidate.tracks[destinationLanes[index]];
            if (destination.notes[row].state != NoteCellState::Note) continue;
            for (std::size_t pairIndex = 0u;
                 pairIndex < original.fxPairs.size(); ++pairIndex) {
                SequencerAction action = SequencerAction::Count;
                if (!resolvedSequencerAction(
                        original, pairIndex, row, action)) continue;
                destination.fxPairs[pairIndex].actions[row]
                    = FxActionCell::sequencer(action);
                float value = resolvedFxValue(original, pairIndex, row);
                if (action == SequencerAction::MicroTime) {
                    const auto resolved = resolvedFxValueCell(
                        original, pairIndex, row);
                    std::size_t sourceVoice = 0u;
                    while (sourceVoice < note.noteVoiceCount()
                        && note.noteVoice(sourceVoice)
                            != destination.notes[row].note) ++sourceVoice;
                    value = resolved.valueVoice(std::min<std::size_t>(
                        sourceVoice, resolved.valueVoiceCount() - 1u));
                }
                destination.fxPairs[pairIndex].values[row]
                    = FxValueCell::withValue(value);
            }
        }
    }
    candidate.tracks[sourceLane].name =
        (original.name.empty() ? "LANE" : original.name)
        + " · " + std::to_string(pitches.front());
    model->session.pattern = std::move(candidate);
    if (model->session.laneDefaultNotes.size() < model->session.pattern.tracks.size())
        model->session.laneDefaultNotes.resize(
            model->session.pattern.tracks.size(), 60u);
    model->session.laneDefaultNotes[sourceLane] = pitches.front();
    for (std::size_t index = 1u; index < pitches.size(); ++index)
        model->session.laneDefaultNotes[destinationLanes[index]] = pitches[index];
    [self clearGridSelection];
    [self.owner modulePatternChanged];
}

- (void)mergeSelectedNoteLanes:(NSMenuItem*)sender
{
    (void)sender;
    auto* model = self.trackerState;
    if (!model || model->songPlaybackActive) return;
    const auto range = [self effectiveGridSelection];
    const auto fields = gridFieldCount(model->sequenceColumnsExpanded);
    const auto firstColumn = _gridSelection.active
        ? _gridSelection.firstColumn(fields)
        : gridClipboardColumn(range.firstTrack, range.firstField, fields);
    const auto lastColumn = _gridSelection.active
        ? _gridSelection.lastColumn(fields) : firstColumn;
    if (firstColumn % fields != 0u || lastColumn % fields != 0u
        || firstColumn / fields >= lastColumn / fields) {
        NSBeep();
        return;
    }
    const auto targetLane = firstColumn / fields;
    const auto lastLane = lastColumn / fields;
    if (lastLane >= model->session.pattern.tracks.size()) return;

    const auto original = model->session.pattern;
    auto candidate = original;
    struct MergedVoice {
        uint8_t note = 0u;
        float velocity = 0.787f;
        float microTime = 0.5f;
        bool hasMicroTime = false;
        GateVoice gate {};
    };
    struct MergedSequence {
        SequencerAction action = SequencerAction::Count;
        float value = 0.0f;
    };
    bool changed = false;
    for (std::size_t row = range.firstRow; row <= range.lastRow; ++row) {
        std::vector<MergedVoice> voices;
        std::array<std::vector<MergedSequence>,
            s3g::tracker::kFxPairCount> sourceSequences;
        for (std::size_t lane = targetLane; lane <= lastLane; ++lane) {
            const auto& track = original.tracks[lane];
            if (row >= track.notes.size()
                || track.notes[row].state != NoteCellState::Note) continue;
            const auto& note = track.notes[row];
            const auto velocity = resolvedVelocityCell(track, row);
            bool hasMicroTime = false;
            FxValueCell microTime = FxValueCell::withValue(0.5f);
            for (std::size_t pairIndex = 0u;
                 pairIndex < track.fxPairs.size(); ++pairIndex) {
                SequencerAction action = SequencerAction::Count;
                if (resolvedSequencerAction(track, pairIndex, row, action)
                    && action == SequencerAction::MicroTime) {
                    hasMicroTime = true;
                    microTime = resolvedFxValueCell(track, pairIndex, row);
                }
            }
            for (std::size_t voice = 0u;
                 voice < note.noteVoiceCount(); ++voice) {
                const auto pitch = note.noteVoice(voice);
                if (std::find_if(voices.begin(), voices.end(),
                        [pitch](const auto& item) {
                            return item.note == pitch;
                        }) != voices.end()) continue;
                const auto velocityVoice = std::min<std::size_t>(voice,
                    velocity.valueVoiceCount() - 1u);
                voices.push_back({ pitch,
                    std::clamp(velocity.valueVoice(velocityVoice),
                        0.0f, 1.0f),
                    std::clamp(microTime.valueVoice(std::min<std::size_t>(
                        voice, microTime.valueVoiceCount() - 1u)),
                        0.0f, 1.0f), hasMicroTime,
                    row < track.gates.size()
                        ? track.gates[row].gateVoice(voice) : GateVoice {} });
            }
            if (lane == targetLane) continue;
            for (std::size_t pairIndex = 0u;
                 pairIndex < track.fxPairs.size(); ++pairIndex) {
                SequencerAction action = SequencerAction::Count;
                if (!resolvedSequencerAction(
                        track, pairIndex, row, action)) continue;
                if (action == SequencerAction::MicroTime) continue;
                sourceSequences[pairIndex].push_back({ action,
                    resolvedFxValue(track, pairIndex, row) });
            }
        }
        if (voices.empty()) continue;
        if (voices.size() > s3g::tracker::kMaximumNoteVoices) {
            NSBeep();
            return;
        }

        std::sort(voices.begin(), voices.end(), [](const auto& a,
                                                    const auto& b) {
            return a.note < b.note;
        });

        auto& target = candidate.tracks[targetLane];
        if (std::any_of(voices.begin(), voices.end(), [](const auto& voice) {
                return voice.hasMicroTime;
            })) {
            std::size_t microTimePair = target.fxPairs.size();
            for (std::size_t pairIndex = 0u;
                 pairIndex < target.fxPairs.size(); ++pairIndex) {
                SequencerAction action = SequencerAction::Count;
                if (resolvedSequencerAction(target, pairIndex, row, action)
                    && action == SequencerAction::MicroTime)
                    microTimePair = pairIndex;
            }
            if (microTimePair >= target.fxPairs.size()) {
                for (std::size_t pairIndex = 0u;
                     pairIndex < target.fxPairs.size(); ++pairIndex) {
                    const auto& actions = target.fxPairs[pairIndex].actions;
                    if (row >= actions.size()
                        || actions[row].state == FxActionCellState::Empty) {
                        microTimePair = pairIndex;
                        break;
                    }
                }
            }
            if (microTimePair >= target.fxPairs.size()) { NSBeep(); return; }
            std::array<float, s3g::tracker::kMaximumNoteVoices> values {};
            for (std::size_t voice = 0u; voice < voices.size(); ++voice)
                values[voice] = voices[voice].hasMicroTime
                    ? voices[voice].microTime : 0.5f;
            auto& pair = target.fxPairs[microTimePair];
            if (pair.actions.size() <= row)
                pair.actions.resize(row + 1u, FxActionCell::empty());
            if (pair.values.size() <= row)
                pair.values.resize(row + 1u, FxValueCell::previous());
            pair.actions[row] = FxActionCell::sequencer(
                SequencerAction::MicroTime);
            pair.values[row] = FxValueCell::withValues(
                values, voices.size());
            pair.actionColumn.length = std::max(
                pair.actionColumn.length, row + 1u);
            pair.valueColumn.length = std::max(
                pair.valueColumn.length, row + 1u);
        }
        for (std::size_t sourcePair = 0u;
             sourcePair < sourceSequences.size(); ++sourcePair) {
            for (const auto& sequence : sourceSequences[sourcePair]) {
                bool alreadyPresent = false;
                bool conflictingValue = false;
                for (std::size_t targetPair = 0u;
                     targetPair < target.fxPairs.size(); ++targetPair) {
                    SequencerAction targetAction = SequencerAction::Count;
                    if (!resolvedSequencerAction(target,
                            targetPair, row, targetAction)
                        || targetAction != sequence.action) continue;
                    const auto targetValue = resolvedFxValue(
                        target, targetPair, row);
                    alreadyPresent = std::abs(targetValue - sequence.value)
                        <= 0.000001f;
                    conflictingValue = !alreadyPresent;
                    break;
                }
                if (alreadyPresent) continue;
                if (conflictingValue) { NSBeep(); return; }
                std::size_t available = target.fxPairs.size();
                for (std::size_t targetPair = 0u;
                     targetPair < target.fxPairs.size(); ++targetPair) {
                    const auto& actions = target.fxPairs[
                        targetPair].actions;
                    if (row >= actions.size()
                        || actions[row].state == FxActionCellState::Empty) {
                        available = targetPair;
                        break;
                    }
                }
                if (available >= target.fxPairs.size()) { NSBeep(); return; }
                auto& pair = target.fxPairs[available];
                if (pair.actions.size() <= row)
                    pair.actions.resize(row + 1u, FxActionCell::empty());
                if (pair.values.size() <= row)
                    pair.values.resize(row + 1u, FxValueCell::previous());
                pair.actions[row] = FxActionCell::sequencer(sequence.action);
                pair.values[row] = FxValueCell::withValue(sequence.value);
                pair.actionColumn.length = std::max(
                    pair.actionColumn.length, row + 1u);
                pair.valueColumn.length = std::max(
                    pair.valueColumn.length, row + 1u);
            }
        }

        std::array<uint8_t, s3g::tracker::kMaximumNoteVoices> notes {};
        std::array<float, s3g::tracker::kMaximumNoteVoices> velocities {};
        std::array<GateVoice, s3g::tracker::kMaximumNoteVoices> gates {};
        bool hasExplicitGate = false;
        for (std::size_t voice = 0u; voice < voices.size(); ++voice) {
            notes[voice] = voices[voice].note;
            velocities[voice] = voices[voice].velocity;
            gates[voice] = voices[voice].gate;
            hasExplicitGate |= gates[voice].mode != GateVoiceMode::Default;
        }
        if (target.notes.size() <= row)
            target.notes.resize(row + 1u, NoteCell::rest());
        if (target.velocities.size() <= row)
            target.velocities.resize(row + 1u, ValueCell::defaultValue());
        if (target.gates.size() <= row)
            target.gates.resize(row + 1u, GateCell::defaultValue());
        target.notes[row] = NoteCell::withNotes(notes, voices.size());
        target.velocities[row]
            = ValueCell::withValues(velocities, voices.size());
        target.gates[row] = hasExplicitGate
            ? GateCell::withVoices(gates, voices.size())
            : GateCell::defaultValue();
        changed = true;
        target.noteColumn.length = std::max(
            target.noteColumn.length, row + 1u);
        target.velocityColumn.length = std::max(
            target.velocityColumn.length, row + 1u);
        target.gateColumn.length = std::max(
            target.gateColumn.length, row + 1u);
        for (std::size_t lane = targetLane + 1u;
             lane <= lastLane; ++lane) {
            const auto& source = original.tracks[lane];
            if (row >= source.notes.size()
                || source.notes[row].state != NoteCellState::Note) continue;
            auto& moved = candidate.tracks[lane];
            moved.notes[row] = NoteCell::rest();
            if (row < moved.velocities.size())
                moved.velocities[row] = ValueCell::defaultValue();
            if (row < moved.gates.size())
                moved.gates[row] = GateCell::defaultValue();
        }
    }
    if (!changed) { NSBeep(); return; }
    model->session.pattern = std::move(candidate);
    model->session.selectedTrack = targetLane;
    model->session.selectedRow = range.firstRow;
    model->session.selectedField = 0u;
    [self clearGridSelection];
    [self.owner modulePatternChanged];
}

- (void)trackerSelectAll:(id)sender
{
    (void)sender;
    auto* model = self.trackerState;
    if (!model || model->session.pattern.tracks.empty()) return;
    const auto page = 0u;
    _gridSelection.active = true;
    _gridSelection.page = page;
    _gridSelection.anchorTrack = 0u;
    _gridSelection.anchorField = 0u;
    _gridSelection.anchorRow = 0u;
    _gridSelection.focusTrack = model->session.pattern.tracks.size() - 1u;
    _gridSelection.focusField = gridFieldCount(
        model->sequenceColumnsExpanded) - 1u;
    _gridSelection.focusRow = visibleRows(model) - 1u;
    [self setNeedsDisplay:YES];
    NSAccessibilityPostNotification(
        self, NSAccessibilitySelectedCellsChangedNotification);
}

- (void)trackerCopy:(id)sender
{
    (void)sender;
    auto* model = self.trackerState;
    if (!model || model->session.pattern.tracks.empty()) return;
    auto range = [self effectiveGridSelection];
    range.lastTrack = std::min(range.lastTrack,
        model->session.pattern.tracks.size() - 1u);
    range.lastField = std::min(range.lastField,
        gridFieldCount(model->sequenceColumnsExpanded) - 1u);
    range.lastRow = std::min(range.lastRow, visibleRows(model) - 1u);
    const auto fields = gridFieldCount(model->sequenceColumnsExpanded);
    const auto firstColumn = _gridSelection.active
        ? _gridSelection.firstColumn(fields)
        : gridClipboardColumn(range.firstTrack, range.firstField, fields);
    const auto lastColumn = _gridSelection.active
        ? _gridSelection.lastColumn(fields) : firstColumn;
    _copiedGridCells.clear();
    _copiedGridCells.reserve(range.rowCount()
        * (lastColumn - firstColumn + 1u));
    NSMutableArray<NSString*>* lines = [[NSMutableArray alloc] init];
    for (std::size_t row = range.firstRow; row <= range.lastRow; ++row) {
        NSMutableArray<NSString*>* cells = [[NSMutableArray alloc] init];
        for (std::size_t column = firstColumn;
             column <= lastColumn; ++column) {
            std::size_t track = 0u;
            std::size_t field = 0u;
            s3g::tracker::app::gridAddressForClipboardColumn(
                column, fields, track, field);
            _copiedGridCells.push_back(trackerGridCellAt(
                model->session.pattern.tracks[track], field, row));
            [cells addObject:[self clipboardTextForTrack:track row:row
                page:range.page field:field]];
        }
        [lines addObject:[cells componentsJoinedByString:@"\t"]];
    }
    NSString* value = [lines componentsJoinedByString:@"\n"];
    NSPasteboard* pasteboard = NSPasteboard.generalPasteboard;
    [pasteboard clearContents];
    [pasteboard setString:value forType:NSPasteboardTypeString];
    self.copiedClipboardText = value;
    self.copiedPasteboardChangeCount = pasteboard.changeCount;
    self.copiedTrackCount = range.trackCount();
    self.copiedFieldCount = range.fieldCount();
    self.copiedRowCount = range.rowCount();
    _copiedColumnTypes.clear();
    _copiedColumnTypes.reserve(lastColumn - firstColumn + 1u);
    for (std::size_t column = firstColumn; column <= lastColumn; ++column)
        _copiedColumnTypes.push_back(gridClipboardFieldType(column % fields));
}

- (void)trackerCut:(id)sender
{
    [self trackerCopy:sender];
    [self clearSelectedGridCells];
}

- (BOOL)clearSelectedGridCells
{
    auto* model = self.trackerState;
    if (!model || model->session.pattern.tracks.empty()) return NO;
    s3g::tracker::Pattern candidate = model->session.pattern;
    auto range = [self effectiveGridSelection];
    range.lastTrack = std::min(range.lastTrack, candidate.tracks.size() - 1u);
    range.lastField = std::min(range.lastField,
        gridFieldCount(model->sequenceColumnsExpanded) - 1u);
    range.lastRow = std::min<std::size_t>(range.lastRow, 255u);
    const auto fields = gridFieldCount(model->sequenceColumnsExpanded);
    const auto firstColumn = _gridSelection.active
        ? _gridSelection.firstColumn(fields)
        : gridClipboardColumn(range.firstTrack, range.firstField, fields);
    const auto lastColumn = _gridSelection.active
        ? _gridSelection.lastColumn(fields) : firstColumn;
    for (std::size_t row = range.firstRow; row <= range.lastRow; ++row) {
        for (std::size_t column = firstColumn;
             column <= lastColumn; ++column) {
            std::size_t track = 0u;
            std::size_t field = 0u;
            s3g::tracker::app::gridAddressForClipboardColumn(
                column, fields, track, field);
            if (![self applyCellText:[self clearTokenForPage:range.page
                    field:field] toTrack:candidate.tracks[track] row:row
                    page:range.page field:field]) {
                NSBeep();
                return NO;
            }
        }
    }
    candidate.visibleRows = std::max(candidate.visibleRows, range.lastRow + 1u);
    model->session.pattern = std::move(candidate);
    [self.owner modulePatternChanged];
    return YES;
}

- (void)trackerPaste:(id)sender
{
    (void)sender;
    NSPasteboard* pasteboard = NSPasteboard.generalPasteboard;
    NSString* value = [pasteboard stringForType:NSPasteboardTypeString];
    if (!value && self.copiedClipboardText
        && self.copiedPasteboardChangeCount == pasteboard.changeCount)
        value = self.copiedClipboardText;
    if (!value) { NSBeep(); return; }
    auto* model = self.trackerState;
    if (!model || model->session.pattern.tracks.empty()) return;
    NSString* normalized = [value stringByReplacingOccurrencesOfString:@"\r\n"
        withString:@"\n"];
    normalized = [normalized stringByReplacingOccurrencesOfString:@"\r"
        withString:@"\n"];
    NSMutableArray<NSString*>* lines = [[normalized
        componentsSeparatedByString:@"\n"] mutableCopy];
    while (lines.count > 1u && lines.lastObject.length == 0u)
        [lines removeLastObject];
    if (lines.count == 0u || lines.count > 256u) { NSBeep(); return; }
    NSMutableArray<NSArray<NSString*>*>* rows = [[NSMutableArray alloc] init];
    NSUInteger widest = 0u;
    for (NSString* line in lines) {
        NSArray<NSString*>* cells = [line componentsSeparatedByString:@"\t"];
        widest = std::max(widest, cells.count);
        [rows addObject:cells];
    }
    if (widest == 0u) { NSBeep(); return; }

    s3g::tracker::Pattern candidate = model->session.pattern;
    const auto range = [self effectiveGridSelection];
    const auto page = range.page;
    const auto fields = gridFieldCount(model->sequenceColumnsExpanded);
    const auto firstColumn = _gridSelection.active
        ? _gridSelection.firstColumn(fields)
        : gridClipboardColumn(range.firstTrack, range.firstField, fields);
    const bool fillSelection = rows.count == 1u && widest == 1u
        && _gridSelection.active;
    const bool shapedInternalPaste = self.copiedPasteboardChangeCount
            == pasteboard.changeCount
        && self.copiedRowCount == rows.count
        && !_copiedColumnTypes.empty()
        && _copiedColumnTypes.size() == widest
        && _copiedGridCells.size() == rows.count * widest;
    const auto maximumColumn = candidate.tracks.size() * fields;
    if (range.firstRow + rows.count > 256u
        || (!fillSelection && firstColumn + widest > maximumColumn)) {
        NSBeep();
        return;
    }
    if (shapedInternalPaste) {
        for (std::size_t offset = 0u; offset < _copiedColumnTypes.size();
             ++offset) {
            if (_copiedColumnTypes[offset]
                != gridClipboardFieldType((firstColumn + offset) % fields)) {
                NSBeep();
                return;
            }
        }
    }
    std::size_t lastRow = range.firstRow;
    if (fillSelection) {
        NSString* cell = rows.firstObject.firstObject;
        const auto selectionLastColumn = _gridSelection.lastColumn(fields);
        for (std::size_t row = range.firstRow;
             row <= std::min<std::size_t>(range.lastRow, 255u); ++row) {
            for (std::size_t column = firstColumn;
                 column <= selectionLastColumn; ++column) {
                std::size_t track = 0u;
                std::size_t field = 0u;
                s3g::tracker::app::gridAddressForClipboardColumn(
                    column, fields, track, field);
                if (![self applyCellText:cell toTrack:candidate.tracks[track]
                        row:row page:page field:field]) {
                    NSBeep();
                    return;
                }
            }
            lastRow = row;
        }
    } else {
        for (NSUInteger rowOffset = 0u; rowOffset < rows.count; ++rowOffset) {
            const auto destinationRow = range.firstRow
                + static_cast<std::size_t>(rowOffset);
            if (destinationRow > 255u) break;
            NSArray<NSString*>* cells = rows[rowOffset];
            for (NSUInteger columnOffset = 0u;
                 columnOffset < cells.count; ++columnOffset) {
                std::size_t track = 0u;
                std::size_t field = 0u;
                const auto column = firstColumn
                    + static_cast<std::size_t>(columnOffset);
                s3g::tracker::app::gridAddressForClipboardColumn(column,
                    fields, track, field);
                if (shapedInternalPaste) {
                    writeTrackerGridCell(candidate.tracks[track], field,
                        destinationRow, _copiedGridCells[
                            static_cast<std::size_t>(rowOffset) * widest
                                + static_cast<std::size_t>(columnOffset)]);
                } else if (![self applyCellText:cells[columnOffset]
                               toTrack:candidate.tracks[track]
                               row:destinationRow page:page field:field]) {
                    NSBeep();
                    return;
                }
            }
            lastRow = destinationRow;
        }
    }
    candidate.visibleRows = std::max(candidate.visibleRows, lastRow + 1u);
    model->session.pattern = std::move(candidate);
    [self.owner modulePatternChanged];
}

- (BOOL)performKeyEquivalent:(NSEvent*)event
{
    const auto modifiers = event.modifierFlags
        & (NSEventModifierFlagCommand | NSEventModifierFlagControl
            | NSEventModifierFlagOption | NSEventModifierFlagShift);
    if ((modifiers & NSEventModifierFlagCommand) != 0u) return NO;
    NSString* key = event.charactersIgnoringModifiers.lowercaseString;
    if (modifiers == NSEventModifierFlagControl) {
        // Do not bounce a key equivalent through keyDown:. In a SWELL host,
        // an unhandled keyDown: is forwarded back through key-equivalent
        // dispatch and recursively re-enters this view. Dispatch the owned
        // shortcuts here so zoom remains safe even during Song playback.
        if (event.keyCode == 24u) {
            [self.owner zoomTrackerIn];
            return YES;
        }
        if (event.keyCode == 27u) {
            [self.owner zoomTrackerOut];
            return YES;
        }
        if (event.keyCode == 29u) {
            [self.owner resetTrackerZoom];
            return YES;
        }
        auto* model = self.trackerState;
        if (!model || model->songPlaybackActive
            || model->session.pattern.tracks.empty()) return NO;
        if ([key isEqualToString:@"z"]) [self.owner undoPressed:nil];
        else if ([key isEqualToString:@"a"]) [self trackerSelectAll:nil];
        else if ([key isEqualToString:@"c"]) [self trackerCopy:nil];
        else if ([key isEqualToString:@"x"]) [self trackerCut:nil];
        else if ([key isEqualToString:@"v"]) [self trackerPaste:nil];
        else return [super performKeyEquivalent:event];
        return YES;
    }
    if (modifiers == (NSEventModifierFlagControl
            | NSEventModifierFlagShift)
        && [key isEqualToString:@"z"]) {
        auto* model = self.trackerState;
        if (!model || model->songPlaybackActive
            || model->session.pattern.tracks.empty()) return NO;
        [self.owner redoPressed:nil];
        return YES;
    }
    return [super performKeyEquivalent:event];
}

- (void)keyDown:(NSEvent*)event
{
    const auto earlyModifiers = event.modifierFlags
        & (NSEventModifierFlagCommand | NSEventModifierFlagControl
            | NSEventModifierFlagOption | NSEventModifierFlagShift);
    if (earlyModifiers == NSEventModifierFlagControl) {
        if (event.keyCode == 24u) {
            [self.owner zoomTrackerIn];
            return;
        }
        if (event.keyCode == 27u) {
            [self.owner zoomTrackerOut];
            return;
        }
        if (event.keyCode == 29u) {
            [self.owner resetTrackerZoom];
            return;
        }
    }
    auto* model = self.trackerState;
    if (!model || model->session.pattern.tracks.empty()) {
        [super keyDown:event];
        return;
    }
    NSString* key = event.charactersIgnoringModifiers.lowercaseString;
    if (model->songPlaybackActive) {
        const auto navigationModifiers = event.modifierFlags
            & (NSEventModifierFlagCommand | NSEventModifierFlagControl
                | NSEventModifierFlagOption | NSEventModifierFlagShift);
        if ([key isEqualToString:@" "]
            && navigationModifiers == 0u) {
            [self.owner moduleTogglePlayback];
            return;
        }
        if ([key isEqualToString:@":"] || [key isEqualToString:@"`"]) {
            [self.owner moduleFocusConsole];
            return;
        }
        auto& session = model->session;
        if (navigationModifiers == 0u
            && (event.keyCode == 123u || event.keyCode == 124u)) {
            const auto fieldCount = gridFieldCount(
                model->sequenceColumnsExpanded);
            session.selectedField = event.keyCode == 123u
                ? (session.selectedField == 0u
                    ? fieldCount - 1u : session.selectedField - 1u)
                : (session.selectedField + 1u) % fieldCount;
            [self.owner moduleSelectionChanged];
            return;
        }
        if (navigationModifiers == 0u
            && (event.keyCode == 125u || event.keyCode == 126u)) {
            const auto rows = playbackFollowVisibleRows(model);
            const auto jump = static_cast<std::size_t>(
                std::clamp<uint32_t>(model->trackerRowJump, 1u, 16u));
            session.selectedRow = event.keyCode == 126u
                ? (session.selectedRow < jump ? 0u
                    : session.selectedRow - jump)
                : std::min(session.selectedRow + jump, rows - 1u);
            [self.owner moduleSelectionChanged];
            return;
        }
        [super keyDown:event];
        return;
    }
    auto& session = model->session;
    const auto shortcutModifiers = event.modifierFlags
        & (NSEventModifierFlagCommand | NSEventModifierFlagControl
            | NSEventModifierFlagOption | NSEventModifierFlagShift);
    if ((shortcutModifiers & NSEventModifierFlagCommand) != 0u) {
        [super keyDown:event];
        return;
    }
    const bool trackerControl = shortcutModifiers
        == NSEventModifierFlagControl;
    const bool trackerControlShift = shortcutModifiers
        == (NSEventModifierFlagControl | NSEventModifierFlagShift);
    const bool trackerControlOption = shortcutModifiers
        == (NSEventModifierFlagControl | NSEventModifierFlagOption);
    const bool trackerControlOptionShift = shortcutModifiers
        == (NSEventModifierFlagControl | NSEventModifierFlagOption
            | NSEventModifierFlagShift);
    if (trackerControl && [key isEqualToString:@"z"]) {
        [self.owner undoPressed:nil];
        return;
    }
    if (trackerControlShift && [key isEqualToString:@"z"]) {
        [self.owner redoPressed:nil];
        return;
    }
    if (trackerControl && [key isEqualToString:@"a"]) {
        [self trackerSelectAll:nil];
        return;
    }
    if (trackerControl && [key isEqualToString:@"c"]) {
        [self trackerCopy:nil];
        return;
    }
    if (trackerControl && [key isEqualToString:@"x"]) {
        [self trackerCut:nil];
        return;
    }
    if (trackerControl && [key isEqualToString:@"v"]) {
        [self trackerPaste:nil];
        return;
    }
    if (trackerControlShift
        && (event.keyCode == 123u || event.keyCode == 124u
            || event.keyCode == 125u || event.keyCode == 126u)) {
        const auto fields = gridFieldCount(model->sequenceColumnsExpanded);
        if (!_gridSelection.active) {
            _gridSelection.page = 0u;
            _gridSelection.anchorTrack = _gridSelection.focusTrack
                = session.selectedTrack;
            _gridSelection.anchorField = _gridSelection.focusField
                = session.selectedField;
            _gridSelection.anchorRow = _gridSelection.focusRow
                = session.selectedRow;
        }
        if (event.keyCode == 123u || event.keyCode == 124u) {
            auto column = gridClipboardColumn(_gridSelection.focusTrack,
                _gridSelection.focusField, fields);
            const auto maximum = model->session.pattern.tracks.size()
                * fields - 1u;
            column = event.keyCode == 123u
                ? (column == 0u ? 0u : column - 1u)
                : std::min(column + 1u, maximum);
            s3g::tracker::app::gridAddressForClipboardColumn(column, fields,
                _gridSelection.focusTrack, _gridSelection.focusField);
        } else {
            _gridSelection.focusRow = event.keyCode == 126u
                ? (_gridSelection.focusRow == 0u
                    ? 0u : _gridSelection.focusRow - 1u)
                : std::min(_gridSelection.focusRow + 1u,
                    visibleRows(model) - 1u);
        }
        _gridSelection.active = true;
        session.selectedTrack = _gridSelection.focusTrack;
        session.selectedField = _gridSelection.focusField;
        session.selectedRow = _gridSelection.focusRow;
        self.selectingWholeRows = NO;
        self.selectingGridCells = YES;
        [self.owner moduleSelectionChanged];
        return;
    }
    if (trackerControlShift && [key isEqualToString:@"p"]) {
        const auto range = [self effectiveGridSelection];
        if (range.firstTrack == range.lastTrack && range.rowCount() >= 2u
            && range.rowCount() <= s3g::tracker::kMaximumPhraseRows)
            [self.owner capturePhraseTrack:range.firstTrack
                firstRow:range.firstRow lastRow:range.lastRow];
        else NSBeep();
        return;
    }
    if (trackerControl && [key isEqualToString:@"p"]) {
        const auto range = [self effectiveGridSelection];
        [self.owner placeSelectedPhraseTrack:range.firstTrack
            row:range.firstRow merge:NO];
        return;
    }
    if (trackerControlOption && key.length == 1u) {
        const unichar digit = [key characterAtIndex:0u];
        if (digit >= '0' && digit <= '9') {
            model->trackerRowJump = digit == '0'
                ? 10u : static_cast<uint32_t>(digit - '0');
            if (self.owner.trackerCallbacks
                && self.owner.trackerCallbacks->viewPreferencesChanged)
                self.owner.trackerCallbacks->viewPreferencesChanged();
            [self.owner reloadModel];
            return;
        }
    }
    if ((trackerControlOption || trackerControlOptionShift)
        && (event.keyCode == 125u || event.keyCode == 126u)) {
        const auto range = [self effectiveGridSelection];
        const auto fields = gridFieldCount(model->sequenceColumnsExpanded);
        const BOOL oneNoteColumn = _gridSelection.active
            ? _gridSelection.firstColumn(fields)
                    == _gridSelection.lastColumn(fields)
                && _gridSelection.firstColumn(fields) % fields == 0u
            : session.selectedField == 0u;
        if (!oneNoteColumn) { NSBeep(); return; }
        NSMenuItem* item = [[NSMenuItem alloc] init];
        item.tag = (event.keyCode == 126u ? 1 : -1)
            * (trackerControlOptionShift ? 12 : 1);
        item.representedObject = [self columnActionPayloadForTrack:
            range.firstTrack field:0u row:range.firstRow];
        [self transposeSelectedNoteRows:item];
        return;
    }
    const auto editingModifiers = event.modifierFlags
        & (NSEventModifierFlagCommand | NSEventModifierFlagControl
            | NSEventModifierFlagOption);
    if (editingModifiers != 0u) {
        [super keyDown:event];
        return;
    }
    if ((event.keyCode == 51 || event.keyCode == 117)
        && _gridSelection.active) {
        [self clearSelectedGridCells];
        return;
    }
    [self clearGridSelection];
    if ([key isEqualToString:@":"] || [key isEqualToString:@"`"]) {
        [self.owner moduleFocusConsole];
        return;
    }
    if ([key isEqualToString:@" "]) {
        if ((event.modifierFlags & NSEventModifierFlagShift) != 0u) {
            [self.owner loopPressed:nil];
            return;
        }
        [self.owner moduleTogglePlayback];
        return;
    }
    if ([key isEqualToString:@"\r"]) {
        [self beginCellEditing];
        return;
    }
    if (event.keyCode == 48) {
        const bool backwards = (event.modifierFlags
            & NSEventModifierFlagShift) != 0u;
        if (backwards) {
            if (session.selectedField > 0u) {
                --session.selectedField;
            } else {
                session.selectedField = gridFieldCount(
                    model->sequenceColumnsExpanded) - 1u;
            }
        } else if (session.selectedField + 1u < gridFieldCount(
                model->sequenceColumnsExpanded)) {
            ++session.selectedField;
        } else {
            session.selectedField = 0u;
        }
        [self.owner moduleSelectionChanged];
        return;
    }
    const bool shift = (event.modifierFlags
        & NSEventModifierFlagShift) != 0u;
    if (event.keyCode == 123 || event.keyCode == 124) {
        if (shift) {
            const auto nextTrack = event.keyCode == 123
                ? (session.selectedTrack == 0u ? 0u
                    : session.selectedTrack - 1u)
                : std::min(session.selectedTrack + 1u,
                    model->session.pattern.tracks.size() - 1u);
            [self selectTrack:nextTrack row:session.selectedRow];
            return;
        }
        if (event.keyCode == 123) {
            if (session.selectedField > 0u) {
                --session.selectedField;
            } else {
                session.selectedField = gridFieldCount(
                    model->sequenceColumnsExpanded) - 1u;
            }
        } else if (session.selectedField + 1u < gridFieldCount(
                model->sequenceColumnsExpanded)) {
            ++session.selectedField;
        } else {
            session.selectedField = 0u;
        }
        [self.owner moduleSelectionChanged];
        return;
    }
    if (event.keyCode == 115 || event.keyCode == 119) {
        [self selectTrack:session.selectedTrack
            row:event.keyCode == 115 ? 0u : visibleRows(model) - 1u];
        return;
    }
    if (event.keyCode == 116 || event.keyCode == 121) {
        const CGFloat visibleHeight = self.enclosingScrollView
            ? NSHeight(self.enclosingScrollView.documentVisibleRect) : 200.0;
        const auto pageRows = std::max<std::size_t>(1u,
            static_cast<std::size_t>(visibleHeight / kGridRowHeight));
        const auto next = event.keyCode == 116
            ? session.selectedRow > pageRows
                ? session.selectedRow - pageRows : 0u
            : std::min(session.selectedRow + pageRows,
                visibleRows(model) - 1u);
        [self selectTrack:session.selectedTrack row:next];
        return;
    }
    if (event.keyCode == 101 || event.keyCode == 109
        || event.keyCode == 103 || event.keyCode == 111) {
        std::size_t numerator = 0u;
        if (event.keyCode == 109) numerator = 1u;
        else if (event.keyCode == 103) numerator = 2u;
        else if (event.keyCode == 111) numerator = 3u;
        const auto row = numerator * (visibleRows(model) - 1u) / 4u;
        [self selectTrack:session.selectedTrack row:row];
        return;
    }
    if (!shift && session.selectedField == 0u && key.length == 1u) {
        const unichar direct = [key characterAtIndex:0u];
        if ((direct >= '0' && direct <= '9')
            || (direct >= 'a' && direct <= 'g')) {
            [self beginCellEditingWithInitialText:key];
            return;
        }
    }
    if (!shift && session.selectedField == 1u && key.length == 1u) {
        const unichar direct = [key characterAtIndex:0u];
        if ((direct >= '0' && direct <= '9') || direct == '.') {
            [self beginCellEditingWithInitialText:key];
            return;
        }
    }
    if (!shift && gridFieldIsSequence(session.selectedField)
        && key.length == 1u) {
        const unichar direct = [key characterAtIndex:0u];
        if ((gridFieldIsSequenceAction(session.selectedField)
                && ((direct >= 'a' && direct <= 'z') || direct == '-'))
            || (!gridFieldIsSequenceAction(session.selectedField)
                && ((direct >= '0' && direct <= '9') || direct == '.'))) {
            [self beginCellEditingWithInitialText:key];
            return;
        }
    }
    if (!shift && gridFieldIsGate(session.selectedField)
        && key.length == 1u) {
        const unichar direct = [key characterAtIndex:0u];
        if ((direct >= '0' && direct <= '9') || direct == '.'
            || direct == 'd' || direct == 't') {
            [self beginCellEditingWithInitialText:key];
            return;
        }
    }
    if (event.keyCode == 125) {
        const auto jump = static_cast<std::size_t>(
            std::clamp<uint32_t>(model->trackerRowJump, 1u, 16u));
        const auto next = std::min(session.selectedRow + jump,
            visibleRows(model) - 1u);
        if (shift) {
            if (self.loopAnchorRow < 0)
                self.loopAnchorRow = static_cast<NSInteger>(session.selectedRow);
            [self setLoopFromAnchor:static_cast<std::size_t>(self.loopAnchorRow)
                row:next];
        } else self.loopAnchorRow = -1;
        [self selectTrack:session.selectedTrack row:next];
        return;
    }
    if (event.keyCode == 126) {
        const auto jump = static_cast<std::size_t>(
            std::clamp<uint32_t>(model->trackerRowJump, 1u, 16u));
        const auto next = session.selectedRow < jump
            ? 0u : session.selectedRow - jump;
        if (shift) {
            if (self.loopAnchorRow < 0)
                self.loopAnchorRow = static_cast<NSInteger>(session.selectedRow);
            [self setLoopFromAnchor:static_cast<std::size_t>(self.loopAnchorRow)
                row:next];
        } else self.loopAnchorRow = -1;
        [self selectTrack:session.selectedTrack row:next];
        return;
    }
    if ([key isEqualToString:@"x"] && session.selectedField == 0u) {
        [self toggleSelectedCell:YES];
        return;
    }
    if (event.keyCode == 51 || event.keyCode == 117) {
        if (session.selectedField == 0u)
            [self writeCellState:NoteCellState::Rest advance:YES];
        else if (session.selectedField == 1u)
            [self adjustVolume:0.0f];
        else if (gridFieldIsGate(session.selectedField)) {
            auto& track = session.pattern.tracks[std::min(
                session.selectedTrack, session.pattern.tracks.size() - 1u)];
            if ([self applyCellText:@"DEF" toTrack:track row:session.selectedRow
                    page:session.selectedPage field:session.selectedField])
                [self.owner modulePatternChanged];
        }
        else
            [self writeFxState:NO clear:YES];
        return;
    }
    if ([key isEqualToString:@"r"]) {
        if (session.selectedField == 0u)
            [self writeCellState:NoteCellState::RetriggerPrevious advance:YES];
        else if (gridFieldIsSequence(session.selectedField))
            [self writeFxState:YES clear:NO];
        else
            [super keyDown:event];
        return;
    }
    if ([key isEqualToString:@"k"]) {
        if (session.selectedField == 0u)
            [self writeCellState:NoteCellState::Kill advance:YES];
        else
            [super keyDown:event];
        return;
    }
    if ([key isEqualToString:@"h"]) {
        if (session.selectedField == 0u)
            [self writeCellState:NoteCellState::Hold advance:YES];
        else
            [super keyDown:event];
        return;
    }
    if ([key isEqualToString:@"["]) {
        if (session.selectedField == 1u)
            [self adjustVolume:-0.05f];
        else if (gridFieldIsSequence(session.selectedField)
            && !gridFieldIsSequenceAction(session.selectedField))
            [self adjustFxValue:-5];
        else [super keyDown:event];
        return;
    }
    if ([key isEqualToString:@"]"]) {
        if (session.selectedField == 1u)
            [self adjustVolume:0.05f];
        else if (gridFieldIsSequence(session.selectedField)
            && !gridFieldIsSequenceAction(session.selectedField))
            [self adjustFxValue:5];
        else [super keyDown:event];
        return;
    }
    if ([key isEqualToString:@"m"]) {
        const auto lane = std::min(session.selectedTrack,
            session.pattern.tracks.size() - 1u);
        session.selectedTrack = lane;
        auto& track = session.pattern.tracks[lane];
        auto& muted = columnForField(track, 0u,
            session.selectedField)->muted;
        muted = !muted;
        [self.owner modulePatternChanged];
        return;
    }
    [super keyDown:event];
}

- (void)flagsChanged:(NSEvent*)event
{
    if ((event.modifierFlags & NSEventModifierFlagShift) == 0u)
        self.loopAnchorRow = -1;
    [super flagsChanged:event];
}

- (void)refreshPlaybackDisplay
{
    auto* model = self.trackerState;
    const auto* pattern = playbackFollowPattern(model);
    if (!model || !pattern || pattern->tracks.empty()) {
        _playbackPresentationPrimed = NO;
        _presentedPlaying = NO;
        _presentedPatternId.clear();
        _presentedSongMuteMask = 0u;
        return;
    }
    const auto patternId = playbackFollowPatternId(model);
    const uint32_t songMuteMask = model->songPlaybackActive
        ? model->songPlaybackMutedTracks : 0u;
    if (_presentedPatternId != patternId
        || _presentedSongMuteMask != songMuteMask) {
        _presentedPatternId = patternId;
        _presentedSongMuteMask = songMuteMask;
        [self setNeedsDisplay:YES];
        [self refreshAccessibilityValue];
    }
    const auto laneCount = std::min<std::size_t>(
        s3g::tracker::kMaximumTrackCount,
        pattern->tracks.size());
    const auto rows = playbackFollowVisibleRows(model);
    const CGFloat laneWidth = gridLaneWidth(
        model->sequenceColumnsExpanded);
    const CGFloat fieldWidth = gridLaneFieldWidth(laneWidth);
    const BOOL playing = model->playing;
    for (std::size_t lane = 0u; lane < laneCount; ++lane) {
        const CGFloat x = gridLaneFieldX(lane, laneWidth);
        for (std::size_t field = 0u;
             field < gridFieldCount(model->sequenceColumnsExpanded);
             ++field) {
            const auto previous = _presentedPlayheads[lane][field];
            const auto current = gridPlaybackRow(model, lane, field);
            if (!_playbackPresentationPrimed
                || _presentedPlaying != playing || previous != current) {
                const auto invalidateRow = [&](std::size_t row) {
                    if (row >= rows) return;
                    const NSRect cell = gridFieldRect(x,
                        kGridHeaderHeight
                            + static_cast<CGFloat>(row) * kGridRowHeight,
                        fieldWidth, kGridRowHeight,
                        model->sequenceColumnsExpanded, field);
                    [self setNeedsDisplayInRect:NSInsetRect(cell, -1.0, -1.0)];
                };
                if (_playbackPresentationPrimed && _presentedPlaying)
                    invalidateRow(previous);
                if (playing) invalidateRow(current);
            }
            _presentedPlayheads[lane][field] = current;
        }
    }
    _playbackPresentationPrimed = YES;
    _presentedPlaying = playing;
}

- (void)drawRect:(NSRect)dirtyRect
{
    auto* model = self.trackerState;
    const auto* pattern = playbackFollowPattern(model);
    fillRect(self.bounds, S3GTrackerThemeColor(
        S3GTrackerThemeRole::Workspace));
    fillRect(NSMakeRect(0.0, 0.0, NSWidth(self.bounds), 2.0),
        S3GTrackerThemeColor(S3GTrackerThemeRole::Focus));
    if (!model || !pattern || pattern->tracks.empty()) {
        drawText(@"NO LANES", NSMakeRect(20.0, 20.0, 200.0, 20.0),
            trackerColor(0x737a80), 10.0);
        return;
    }
    const auto& session = model->session;
    const auto laneCount = std::min<std::size_t>(
        s3g::tracker::kMaximumTrackCount,
        pattern->tracks.size());
    const auto rows = playbackFollowVisibleRows(model);
    const auto selectedLane = std::min(session.selectedTrack,
        laneCount - 1u);
    const auto selectedRow = std::min(session.selectedRow, rows - 1u);
    const CGFloat laneWidth = gridLaneWidth(
        model->sequenceColumnsExpanded);
    const CGFloat fieldWidth = gridLaneFieldWidth(laneWidth);
    NSColor* grid = S3GTrackerThemeColor(S3GTrackerThemeRole::Grid, 0.70);
    NSColor* dim = S3GTrackerThemeColor(S3GTrackerThemeRole::TextFaint);
    NSColor* text = S3GTrackerThemeColor(S3GTrackerThemeRole::TextPrimary);
    NSColor* focus = S3GTrackerThemeColor(S3GTrackerThemeRole::Focus);
    NSColor* note = S3GTrackerThemeColor(S3GTrackerThemeRole::Note);
    NSColor* value = S3GTrackerThemeColor(S3GTrackerThemeRole::Value);

    fillRect(NSMakeRect(0.0, 0.0, NSWidth(self.bounds),
        kGridHeaderHeight), S3GTrackerThemeColor(
            S3GTrackerThemeRole::Panel));
    for (std::size_t row = 0u; row < rows; ++row) {
        const CGFloat y = kGridHeaderHeight
            + static_cast<CGFloat>(row) * kGridRowHeight;
        if (!NSIntersectsRect(dirtyRect, NSMakeRect(
                0.0, y, NSWidth(self.bounds), kGridRowHeight))) continue;
        fillRect(NSMakeRect(0.0, y, NSWidth(self.bounds), kGridRowHeight),
            (row % 4u) == 0u
                ? S3GTrackerThemeColor(S3GTrackerThemeRole::Raised)
                : S3GTrackerThemeColor(S3GTrackerThemeRole::Panel));
        fillRect(NSMakeRect(0.0, y, kGridRowNumberWidth - 1.0,
                kGridRowHeight),
            (row % 4u) == 0u
                ? S3GTrackerThemeColor(S3GTrackerThemeRole::Control)
                : S3GTrackerThemeColor(S3GTrackerThemeRole::Raised));
        fillRect(NSMakeRect(kGridRowNumberWidth - 1.0, y, 1.0,
            kGridRowHeight), grid);
        if (row >= session.transport.loopStartRow
            && row < session.transport.loopEndRow) {
            fillRect(NSMakeRect(0.0, y, NSWidth(self.bounds), kGridRowHeight),
                S3GTrackerThemeColor(S3GTrackerThemeRole::Live,
                    session.transport.loopEnabled ? 0.075 : 0.035));
        }
        if (row == selectedRow) {
            fillRect(NSMakeRect(0.0, y, NSWidth(self.bounds),
                    kGridRowHeight),
                S3GTrackerThemeColor(S3GTrackerThemeRole::Focus, 0.11));
        } else if ((row % 4u) == 0u) {
            fillRect(NSMakeRect(0.0, y, NSWidth(self.bounds), 1.0),
                S3GTrackerThemeColor(S3GTrackerThemeRole::Border, 0.72));
        }
        // The frozen overlay is the sole row-label renderer. A second copy in
        // the scrolling document creates fuzzy overdraw at the origin and
        // exposes moving glyphs during horizontal scrolling.
    }

    if (session.transport.loopStartRow < rows) {
        const CGFloat y = kGridHeaderHeight
            + static_cast<CGFloat>(session.transport.loopStartRow)
                * kGridRowHeight;
        fillRect(NSMakeRect(0.0, y, NSWidth(self.bounds), 2.0),
            S3GTrackerThemeColor(S3GTrackerThemeRole::Live,
                session.transport.loopEnabled ? 0.85 : 0.42));
    }
    if (session.transport.loopEndRow <= rows) {
        const CGFloat y = kGridHeaderHeight
            + static_cast<CGFloat>(session.transport.loopEndRow)
                * kGridRowHeight - 2.0;
        fillRect(NSMakeRect(0.0, y, NSWidth(self.bounds), 2.0),
            S3GTrackerThemeColor(S3GTrackerThemeRole::Live,
                session.transport.loopEnabled ? 0.85 : 0.42));
    }

    const CGFloat laneHeight = kGridHeaderHeight
        + static_cast<CGFloat>(rows) * kGridRowHeight;
    for (std::size_t lane = 0u; lane + 1u < laneCount; ++lane) {
        const CGFloat gutterX = gridLaneX(lane, laneWidth) + laneWidth;
        fillRect(NSMakeRect(gutterX, 0.0, kGridLaneGutter, laneHeight),
            trackerColor(0x090b0c));
    }

    for (std::size_t lane = 0u; lane < laneCount; ++lane) {
        const auto& track = pattern->tracks[lane];
        const bool recordArmed = model->midiStepRecordMode
                != MidiStepRecordMode::Off
            && lane == std::min(model->midiRecordTrack, laneCount - 1u);
        const bool songMuted = model->songPlaybackActive
            && (model->songPlaybackMutedTracks
                & (uint32_t { 1u } << lane)) != 0u;
        const auto page = 0u;
        const auto fieldCount = gridFieldCount(
            model->sequenceColumnsExpanded);
        std::array<const ColumnDefinition*, 7u> columns {{
            &track.noteColumn, &track.velocityColumn,
            &track.fxPairs[0u].actionColumn,
            &track.fxPairs[0u].valueColumn,
            &track.fxPairs[1u].actionColumn,
            &track.fxPairs[1u].valueColumn,
            &track.gateColumn,
        }};
        const CGFloat laneX = gridLaneX(lane, laneWidth);
        const CGFloat x = gridLaneFieldX(lane, laneWidth);
        if (!NSIntersectsRect(dirtyRect, NSMakeRect(
                laneX, 0.0, laneWidth, laneHeight))) continue;
        const auto identityColor = trackerColor(
            kLaneColors[lane % kLaneColors.size()],
            track.noteColumn.muted || songMuted ? 0.35
                : lane == selectedLane ? 1.0 : 0.72);
        bool allMuted = songMuted;
        if (!songMuted) {
            allMuted = true;
            for (std::size_t field = 0u; field < fieldCount; ++field)
                allMuted = allMuted && columns[field]->muted;
        }

        fillRect(NSMakeRect(laneX, 2.0, laneWidth,
                kGridHeaderHeight - 2.0),
            lane == selectedLane
                ? S3GTrackerThemeColor(S3GTrackerThemeRole::Selection)
                : S3GTrackerThemeColor(S3GTrackerThemeRole::Raised));
        fillRect(NSMakeRect(laneX, 2.0, 3.0,
                std::max<CGFloat>(0.0, laneHeight - 2.0)),
            identityColor);
        CGFloat nameInset = 6.0;
        if (recordArmed) {
            const NSRect armedRect = NSMakeRect(x + 5.0, 5.0, 25.0, 13.0);
            fillRect(armedRect, S3GTrackerThemeColor(
                S3GTrackerThemeRole::Danger, 0.24));
            strokeRect(armedRect, S3GTrackerThemeColor(
                S3GTrackerThemeRole::Danger));
            drawCenteredText(@"REC", armedRect,
                S3GTrackerThemeColor(S3GTrackerThemeRole::Danger),
                6.5, NSFontWeightSemibold, NSTextAlignmentCenter);
            nameInset = 34.0;
        }
        drawCenteredText(nsString(track.name.empty()
                ? "LANE " + std::to_string(lane + 1u) : track.name),
            NSMakeRect(x + nameInset, 3.0,
                std::max<CGFloat>(1.0, fieldWidth - 88.0 - nameInset),
                18.0),
            allMuted ? dim : text,
            9.5, NSFontWeightSemibold, NSTextAlignmentLeft);
        const NSRect channelRect = gridLaneChannelRect(x, fieldWidth);
        const NSRect resyncRect = gridLaneResyncRect(x, fieldWidth);
        fillRect(resyncRect, S3GTrackerThemeColor(
            S3GTrackerThemeRole::Control));
        fillRect(channelRect, S3GTrackerThemeColor(
            S3GTrackerThemeRole::Control));
        strokeRect(resyncRect, lane == selectedLane
            ? focus : S3GTrackerThemeColor(S3GTrackerThemeRole::Border));
        strokeRect(channelRect, lane == selectedLane
            ? note : S3GTrackerThemeColor(S3GTrackerThemeRole::Border));
        drawCenteredText(@"SYNC", resyncRect,
            allMuted ? dim : focus, 6.2, NSFontWeightSemibold,
            NSTextAlignmentCenter);
        drawCenteredText([NSString stringWithFormat:@"CH%02u",
                static_cast<unsigned int>(std::clamp<int>(
                    track.midiChannel, 1, 16))],
            NSInsetRect(channelRect, 2.0, 0.0),
            allMuted ? dim : note, 8.0, NSFontWeightSemibold,
            NSTextAlignmentCenter);
        for (std::size_t field = 0u; field < fieldCount; ++field) {
            const auto* column = columns[field];
            const NSRect headerField = gridFieldRect(x,
                kGridColumnLabelTop, fieldWidth,
                kGridHeaderHeight - kGridColumnLabelTop,
                model->sequenceColumnsExpanded, field);
            constexpr std::array<const char*, 7u> labels {
                "NOTE", "VOL", "SEQ1", "V1", "SEQ2", "V2", "GATE",
            };
            NSString* label = [NSString stringWithUTF8String:labels[field]];
            if (gridFieldIsSequence(field)
                && !gridFieldIsSequenceAction(field)) {
                const auto mode = track.fxPairs[gridSequencePair(field)]
                    .valueInterpolation;
                label = [NSString stringWithFormat:@"%@ %@", label,
                    mode == ValueInterpolation::Linear ? @"LIN" : @"STP"];
            }
            NSString* stride = column->stride == 1u ? @""
                : [NSString stringWithFormat:@"×%u", column->stride];
            NSString* state = [NSString stringWithFormat:@"L%lu%@",
                static_cast<unsigned long>(column->length), stride];
            const auto columnLength = std::max<std::size_t>(
                1u, column->length);
            NSString* readStart = [NSString stringWithFormat:@"READ %02lu",
                static_cast<unsigned long>(
                    column->phase % columnLength + 1u)];
            NSString* direction = [NSString stringWithFormat:@"DIR %@",
                directionMark(column->direction)];
            NSColor* fieldColor = field == 0u ? note
                : gridFieldIsSequenceAction(field)
                    ? S3GTrackerThemeColor(S3GTrackerThemeRole::Warning)
                    : value;
            drawCenteredText(label, NSInsetRect(NSMakeRect(
                    NSMinX(headerField), kGridColumnLabelTop,
                    NSWidth(headerField), kGridColumnLabelHeight), 2.0, 0.0),
                column->muted ? dim : fieldColor,
                gridFieldIsSequence(field)
                        && !gridFieldIsSequenceAction(field) ? 6.8 : 7.8,
                NSFontWeightSemibold,
                NSTextAlignmentCenter);
            drawCenteredText(state, NSInsetRect(NSMakeRect(
                    NSMinX(headerField), kGridColumnLengthTop,
                    NSWidth(headerField), kGridColumnLengthHeight), 2.0, 0.0),
                S3GTrackerThemeColor(S3GTrackerThemeRole::TextMuted),
                6.8, NSFontWeightMedium,
                NSTextAlignmentCenter);
            drawCenteredText(readStart, NSInsetRect(NSMakeRect(
                    NSMinX(headerField), kGridColumnReadStartTop,
                    NSWidth(headerField), kGridColumnReadStartHeight),
                    2.0, 0.0),
                column->muted
                    ? S3GTrackerThemeColor(S3GTrackerThemeRole::TextFaint)
                    : S3GTrackerThemeColor(S3GTrackerThemeRole::TextMuted),
                5.9, NSFontWeightMedium,
                NSTextAlignmentCenter);
            const NSRect directionButton = NSInsetRect(NSMakeRect(
                NSMinX(headerField), kGridColumnDirectionTop,
                NSWidth(headerField), kGridColumnDirectionHeight), 2.0, 1.0);
            fillRect(directionButton,
                S3GTrackerThemeColor(S3GTrackerThemeRole::Control));
            strokeRect(directionButton,
                S3GTrackerThemeColor(S3GTrackerThemeRole::Border));
            drawCenteredText(direction, directionButton,
                column->muted
                    ? S3GTrackerThemeColor(S3GTrackerThemeRole::TextFaint)
                    : S3GTrackerThemeColor(S3GTrackerThemeRole::TextSecondary),
                6.4, NSFontWeightMedium, NSTextAlignmentCenter);
            const NSRect muteButton = NSInsetRect(NSMakeRect(
                NSMinX(headerField), kGridColumnMuteTop,
                NSWidth(headerField), kGridColumnMuteHeight), 2.0, 1.0);
            fillRect(muteButton, column->muted
                ? S3GTrackerThemeColor(S3GTrackerThemeRole::Danger, 0.24)
                : S3GTrackerThemeColor(S3GTrackerThemeRole::Control));
            strokeRect(muteButton, column->muted
                ? S3GTrackerThemeColor(S3GTrackerThemeRole::Danger)
                : S3GTrackerThemeColor(S3GTrackerThemeRole::Border));
            drawCenteredText(@"MUTE", muteButton,
                column->muted
                    ? S3GTrackerThemeColor(S3GTrackerThemeRole::Danger)
                    : S3GTrackerThemeColor(S3GTrackerThemeRole::TextMuted),
                6.4, column->muted ? NSFontWeightSemibold
                                   : NSFontWeightMedium,
                NSTextAlignmentCenter);
            if (field > 0u) {
                fillRect(NSMakeRect(NSMinX(headerField),
                    kGridColumnLabelTop + 1.0, 1.0,
                    kGridHeaderHeight - kGridColumnLabelTop - 3.0), grid);
            }
        }
        if (lane == selectedLane)
            fillRect(NSMakeRect(laneX + 1.0, kGridHeaderHeight - 3.0,
                laneWidth - 2.0, 3.0), focus);

        for (std::size_t row = 0u; row < rows; ++row) {
            const CGFloat y = kGridHeaderHeight
                + static_cast<CGFloat>(row) * kGridRowHeight;
            if (!NSIntersectsRect(dirtyRect, NSMakeRect(
                    laneX, y, laneWidth, kGridRowHeight))) continue;
            const bool selected = lane == selectedLane
                && row == selectedRow;
            for (std::size_t field = 0u; field < fieldCount; ++field) {
                const auto* column = columns[field];
                const NSRect fieldRect = gridFieldRect(x, y, fieldWidth,
                    kGridRowHeight, model->sequenceColumnsExpanded, field);
                const auto pairIndex = gridFieldIsSequence(field)
                    ? gridSequencePair(field) : 0u;
                const bool head = model->playing && (field == 0u
                    ? row == model->notePlayheads[lane]
                    : field == 1u
                        ? row == model->velocityPlayheads[lane]
                    : gridFieldIsGate(field)
                        ? row == model->notePlayheads[lane]
                    : gridFieldIsSequenceAction(field)
                        ? row == model->fxActionPlayheads[lane][pairIndex]
                        : row == model->fxValuePlayheads[lane][pairIndex]);
                NSColor* activeColor = field == 0u ? text
                    : gridFieldIsSequenceAction(field)
                        ? S3GTrackerThemeColor(S3GTrackerThemeRole::Warning)
                        : value;
                if (head) {
                    fillRect(NSInsetRect(fieldRect, 1.0, 1.0),
                        S3GTrackerThemeColor(
                            S3GTrackerThemeRole::GridPlayback));
                }
                const bool unavailable = songMuted || column->muted
                    || row >= column->length;
                if (unavailable) {
                    fillRect(NSInsetRect(fieldRect, 1.0, 1.0),
                        trackerColor(0x090b0c, 0.70));
                }
                fillRect(NSMakeRect(NSMaxX(fieldRect) - 1.0,
                    NSMinY(fieldRect), 1.0, NSHeight(fieldRect)), grid);
                fillRect(NSMakeRect(NSMinX(fieldRect),
                    NSMaxY(fieldRect) - 1.0, NSWidth(fieldRect), 1.0),
                    grid);
                const bool inSelection = _gridSelection.active
                    && _gridSelection.containsLinear(page, lane, field,
                        row, fieldCount);
                if (inSelection) {
                    fillRect(NSInsetRect(fieldRect, 1.0, 1.0),
                        S3GTrackerThemeColor(
                            S3GTrackerThemeRole::GridSelection));
                }
                const bool cursor = selected
                    && field == std::min(session.selectedField,
                        fieldCount - 1u);
                if (cursor) {
                    fillRect(NSInsetRect(fieldRect, 1.0, 1.0),
                        S3GTrackerThemeColor(
                            S3GTrackerThemeRole::GridCursor));
                }

                NSString* value = @"---";
                bool active = false;
                if (field == 0u) {
                    const NoteCell note = row < track.notes.size()
                        ? track.notes[row] : NoteCell::rest();
                    value = noteText(note, model->showMidiNoteValues);
                    active = note.state != NoteCellState::Rest;
                } else if (field == 1u) {
                    value = volumeText(track, row);
                    active = row < track.velocities.size()
                        && track.velocities[row].state
                            == ValueCellState::Value;
                } else if (gridFieldIsGate(field)) {
                    value = gateText(track, row);
                    active = row < track.gates.size()
                        && track.gates[row].voiceCount > 0u;
                } else if (gridFieldIsSequenceAction(field)) {
                    value = fxActionText(track, pairIndex, row);
                    active = ![value isEqualToString:@"---"];
                } else {
                    value = fxValueText(track, pairIndex, row);
                    active = row < track.fxPairs[pairIndex].values.size()
                        && track.fxPairs[pairIndex].values[row].state
                            == FxValueCellState::Value;
                }
                drawCenteredText(value, NSInsetRect(fieldRect, 3.0, 1.0),
                    active && !unavailable ? activeColor : dim,
                    field == 1u ? 9.2 : 10.0,
                    active ? NSFontWeightMedium : NSFontWeightRegular,
                    field == 1u
                        ? NSTextAlignmentRight : NSTextAlignmentCenter);
                if (head && !inSelection && !cursor) {
                    fillRect(NSMakeRect(NSMinX(fieldRect) + 1.0, y + 3.0,
                        2.0, kGridRowHeight - 6.0),
                        unavailable ? dim : S3GTrackerThemeColor(
                            S3GTrackerThemeRole::GridPlaybackAccent));
                }
            }
        }
        for (std::size_t field = 0u; field < fieldCount; ++field) {
            const auto* column = columns[field];
            if (column->length > rows) continue;
            const CGFloat lengthY = kGridHeaderHeight
                + static_cast<CGFloat>(column->length) * kGridRowHeight;
            const NSRect fieldRect = gridFieldRect(x, lengthY - 1.0,
                fieldWidth, 2.0, model->sequenceColumnsExpanded, field);
            NSColor* lengthColor = field == 0u ? note
                : gridFieldIsSequenceAction(field)
                    ? S3GTrackerThemeColor(S3GTrackerThemeRole::Warning)
                    : value;
            fillRect(fieldRect, column->muted
                ? dim : lengthColor);
        }
    }
}

@end

@implementation S3GTrackerGridScrollView

- (void)reflectScrolledClipView:(NSClipView*)clipView
{
    [super reflectScrolledClipView:clipView];
    [self.frozenRowGutter refreshFrameAndDisplay];
}

- (void)layout
{
    [super layout];
    [self.frozenRowGutter refreshFrameAndDisplay];
}

@end

@implementation S3GTrackerRowGutterView

- (instancetype)initWithScrollView:(NSScrollView*)scrollView
    gridView:(S3GTrackerGridView*)gridView
{
    self = [super initWithFrame:NSZeroRect];
    if (self) {
        self.scrollView = scrollView;
        self.gridView = gridView;
        self.autoresizingMask = NSViewHeightSizable;
        self.wantsLayer = YES;
        self.layer.opaque = YES;
        self.layer.backgroundColor = S3GTrackerThemeColor(
            S3GTrackerThemeRole::Workspace).CGColor;
        self.accessibilityElement = YES;
        self.accessibilityRole = NSAccessibilityGroupRole;
        self.accessibilityLabel = @"Frozen tracker row numbers";
        self.accessibilityHelp = @"Row numbers stay visible while lanes scroll horizontally. Drag here to set the global loop region.";
    }
    return self;
}

- (BOOL)isFlipped { return YES; }
- (BOOL)isOpaque { return YES; }

- (void)refreshFrameAndDisplay
{
    NSScrollView* scroll = self.scrollView;
    S3GTrackerGridView* grid = self.gridView;
    if (!scroll || !grid || !scroll.contentView) return;
    const NSRect contentFrame = scroll.contentView.frame;
    const NSRect transformed = [grid convertRect:NSMakeRect(
        0.0, 0.0, kGridRowNumberWidth, 1.0) toView:scroll];
    const CGFloat width = std::max<CGFloat>(1.0,
        std::ceil(NSWidth(transformed)));
    self.frame = NSMakeRect(NSMinX(contentFrame), NSMinY(contentFrame),
        width, NSHeight(contentFrame));
    [self setNeedsDisplay:YES];
}

- (void)resetCursorRects
{
    [super resetCursorRects];
    [self addCursorRect:self.bounds cursor:NSCursor.resizeUpDownCursor];
}

- (NSRect)pinnedRectForGridRect:(NSRect)gridRect
{
    NSRect result = [self.gridView convertRect:gridRect toView:self];
    result.origin.x = 0.0;
    result.size.width = NSWidth(self.bounds);
    return result;
}

- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    auto* model = self.gridView.trackerState;
    fillRect(self.bounds, S3GTrackerThemeColor(
        S3GTrackerThemeRole::Workspace));
    if (!model || !playbackFollowPattern(model)) return;
    const CGFloat scale = std::max<CGFloat>(0.01,
        self.scrollView.magnification);
    const NSRect header = [self pinnedRectForGridRect:NSMakeRect(
        0.0, 0.0, kGridRowNumberWidth, kGridHeaderHeight)];
    if (NSIntersectsRect(self.bounds, header)) {
        fillRect(NSIntersectionRect(self.bounds, header),
            S3GTrackerThemeColor(S3GTrackerThemeRole::Panel));
        const NSRect focus = [self pinnedRectForGridRect:NSMakeRect(
            0.0, 0.0, kGridRowNumberWidth, 2.0)];
        fillRect(NSIntersectionRect(self.bounds, focus),
            S3GTrackerThemeColor(S3GTrackerThemeRole::Focus));
        drawCenteredText(@"ROW", NSMakeRect(2.0,
                NSMaxY(header) - 16.0 * scale,
                std::max<CGFloat>(1.0, NSWidth(self.bounds) - 7.0),
                14.0 * scale),
            S3GTrackerThemeColor(S3GTrackerThemeRole::TextMuted),
            7.5 * scale, NSFontWeightSemibold, NSTextAlignmentRight);
    }

    const auto rows = playbackFollowVisibleRows(model);
    const auto selectedRow = std::min(model->session.selectedRow, rows - 1u);
    NSColor* gridColor = S3GTrackerThemeColor(
        S3GTrackerThemeRole::Grid, 0.70);
    for (std::size_t row = 0u; row < rows; ++row) {
        const CGFloat y = kGridHeaderHeight
            + static_cast<CGFloat>(row) * kGridRowHeight;
        const NSRect rowRect = [self pinnedRectForGridRect:NSMakeRect(
            0.0, y, kGridRowNumberWidth, kGridRowHeight)];
        if (!NSIntersectsRect(self.bounds, rowRect)) continue;
        fillRect(rowRect, (row % 4u) == 0u
            ? S3GTrackerThemeColor(S3GTrackerThemeRole::Control)
            : S3GTrackerThemeColor(S3GTrackerThemeRole::Raised));
        fillRect(NSMakeRect(NSMaxX(rowRect) - std::max<CGFloat>(1.0, scale),
                NSMinY(rowRect), std::max<CGFloat>(1.0, scale),
                NSHeight(rowRect)), gridColor);
        if (row >= model->session.transport.loopStartRow
            && row < model->session.transport.loopEndRow) {
            fillRect(rowRect, S3GTrackerThemeColor(
                S3GTrackerThemeRole::Live,
                model->session.transport.loopEnabled ? 0.075 : 0.035));
        }
        if ([self.gridView isWholeRowSelected:row]) {
            fillRect(rowRect, S3GTrackerThemeColor(
                S3GTrackerThemeRole::Selection, 0.52));
        }
        if (row == selectedRow) {
            fillRect(rowRect, S3GTrackerThemeColor(
                S3GTrackerThemeRole::Focus, 0.11));
        } else if ((row % 4u) == 0u) {
            fillRect(NSMakeRect(NSMinX(rowRect), NSMinY(rowRect),
                    NSWidth(rowRect), std::max<CGFloat>(1.0, scale)),
                S3GTrackerThemeColor(S3GTrackerThemeRole::Border, 0.72));
        }
        drawCenteredText([NSString stringWithFormat:@"%02lu",
                static_cast<unsigned long>(row + 1u)],
            NSInsetRect(rowRect, 3.0 * scale, 0.0),
            row == selectedRow
                ? S3GTrackerThemeColor(S3GTrackerThemeRole::Focus)
                : (row % 4u) == 0u
                    ? S3GTrackerThemeColor(S3GTrackerThemeRole::TextMuted)
                    : S3GTrackerThemeColor(S3GTrackerThemeRole::TextFaint),
            9.6 * scale,
            row == selectedRow ? NSFontWeightSemibold : NSFontWeightMedium,
            NSTextAlignmentRight);
    }

    for (const uint32_t boundary : {
             model->session.transport.loopStartRow,
             model->session.transport.loopEndRow }) {
        if (boundary > rows) continue;
        const CGFloat y = kGridHeaderHeight
            + static_cast<CGFloat>(boundary) * kGridRowHeight;
        const NSRect line = [self pinnedRectForGridRect:NSMakeRect(
            0.0, y - (boundary == model->session.transport.loopEndRow
                    ? 2.0 : 0.0),
            kGridRowNumberWidth, 2.0)];
        if (NSIntersectsRect(self.bounds, line)) {
            fillRect(line, S3GTrackerThemeColor(
                S3GTrackerThemeRole::Live,
                model->session.transport.loopEnabled ? 0.85 : 0.42));
        }
    }
}

- (NSInteger)rowForEvent:(NSEvent*)event clamp:(BOOL)clamp
{
    auto* model = self.gridView.trackerState;
    if (!model) return -1;
    const NSPoint point = [self.gridView convertPoint:
        event.locationInWindow fromView:nil];
    const NSInteger rows = static_cast<NSInteger>(visibleRows(model));
    NSInteger row = static_cast<NSInteger>(std::floor(
        (point.y - kGridHeaderHeight) / kGridRowHeight));
    if (clamp) return std::clamp<NSInteger>(row, 0, rows - 1);
    return row >= 0 && row < rows ? row : -1;
}

- (void)mouseDown:(NSEvent*)event
{
    const NSInteger row = [self rowForEvent:event clamp:NO];
    if (row < 0) return;
    if ((event.modifierFlags & NSEventModifierFlagShift) != 0u) {
        const std::size_t anchor = [self.gridView wholeRowSelectionAnchor];
        self.selectingRowRange = YES;
        [self.gridView selectWholeRowsFrom:anchor
            to:static_cast<std::size_t>(row)];
        return;
    }
    self.selectingRowRange = NO;
    [self.gridView clearGridSelection];
    [self.gridView beginLoopSelectionAtRow:row];
}

- (void)mouseDragged:(NSEvent*)event
{
    if (self.selectingRowRange) return;
    [self.gridView continueLoopSelectionAtRow:
        [self rowForEvent:event clamp:YES]];
}

- (void)mouseUp:(NSEvent*)event
{
    (void)event;
    if (!self.selectingRowRange) [self.gridView finishLoopSelection];
    self.selectingRowRange = NO;
}

- (NSMenu*)menuForEvent:(NSEvent*)event
{
    auto* model = self.gridView.trackerState;
    const NSInteger clicked = [self rowForEvent:event clamp:NO];
    if (!model || clicked < 0) return nil;
    NSDictionary* payload = [self.gridView rowActionPayloadForRow:
        static_cast<std::size_t>(clicked)];
    const NSUInteger count = [payload[@"count"] unsignedIntegerValue];
    const BOOL editable = !model->songPlaybackActive;
    NSMenu* menu = [[NSMenu alloc] initWithTitle:@"PATTERN ROWS"];
    menu.autoenablesItems = NO;
    const auto addItem = ^NSMenuItem*(NSString* title, SEL action,
        BOOL enabled) {
        NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:title
            action:action keyEquivalent:@""];
        item.target = self.gridView;
        item.representedObject = payload;
        item.enabled = enabled;
        [menu addItem:item];
        return item;
    };
    NSString* insertTitle = count > 1u
        ? [NSString stringWithFormat:@"INSERT %lu ROWS ABOVE",
            static_cast<unsigned long>(count)]
        : @"INSERT ROW ABOVE";
    NSString* insertBelowTitle = count > 1u
        ? [NSString stringWithFormat:@"INSERT %lu ROWS BELOW",
            static_cast<unsigned long>(count)]
        : @"INSERT ROW BELOW";
    NSString* deleteTitle = count > 1u
        ? [NSString stringWithFormat:@"DELETE %lu ROWS",
            static_cast<unsigned long>(count)]
        : @"DELETE ROW";
    NSString* copyTitle = count > 1u
        ? [NSString stringWithFormat:@"COPY %lu ROWS",
            static_cast<unsigned long>(count)]
        : @"COPY ROW";
    addItem(insertTitle, @selector(insertPatternRows:), editable
        && model->session.pattern.visibleRows < kGridMaximumRows);
    addItem(insertBelowTitle, @selector(insertPatternRowsBelow:), editable
        && model->session.pattern.visibleRows < kGridMaximumRows);
    addItem(deleteTitle, @selector(deletePatternRows:), editable
        && model->session.pattern.visibleRows > 16u);
    [menu addItem:NSMenuItem.separatorItem];
    addItem(copyTitle, @selector(copyPatternRows:), YES);
    addItem(@"PASTE ROWS ABOVE", @selector(pastePatternRows:), editable
        && [self.gridView hasRowClipboard]
        && model->session.pattern.visibleRows < kGridMaximumRows);
    [menu addItem:NSMenuItem.separatorItem];
    NSMenuItem* quantize = addItem(@"QUANTIZE MT TO ROW GRID",
        @selector(quantizePatternRows:), editable);
    quantize.toolTip = @"Reset authored MT microtime in these rows to 0 ms; deliberate Delay, Flam, Ratchet, and Burst timing is preserved.";
    NSMenuItem* humanize = addItem(@"HUMANIZE HIT PLACEMENT", nil,
        editable && count > 1u);
    humanize.toolTip = @"Move note hits one neighboring row without leaving the selected range or overwriting an occupied row.";
    NSMenu* humanizeMenu = [[NSMenu alloc] initWithTitle:
        @"HUMANIZE HIT PLACEMENT"];
    humanizeMenu.autoenablesItems = NO;
    for (const NSInteger strength : { 10, 25, 50 }) {
        NSMenuItem* choice = [[NSMenuItem alloc] initWithTitle:
            [NSString stringWithFormat:@"%ld%%", static_cast<long>(strength)]
            action:@selector(humanizePatternRows:) keyEquivalent:@""];
        choice.target = self.gridView;
        choice.representedObject = payload;
        choice.tag = strength;
        choice.enabled = humanize.enabled;
        [humanizeMenu addItem:choice];
    }
    humanize.submenu = humanizeMenu;

    const auto addSubmenuAction = [&](NSMenu* submenu, NSString* title,
        SEL action, NSInteger tag, BOOL enabled) {
        NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:title
            action:action keyEquivalent:@""];
        item.target = self.gridView;
        item.representedObject = payload;
        item.tag = tag;
        item.enabled = enabled;
        [submenu addItem:item];
        return item;
    };

    NSMenuItem* rhythm = addItem(@"RHYTHM", nil, editable);
    NSMenu* rhythmMenu = [[NSMenu alloc] initWithTitle:@"RHYTHM"];
    rhythmMenu.autoenablesItems = NO;
    addSubmenuAction(rhythmMenu, @"REVERSE NOTES",
        @selector(reversePatternRows:), 0, editable && count > 1u);
    addSubmenuAction(rhythmMenu, @"ROTATE NOTES LEFT 1",
        @selector(rotatePatternRows:), -1, editable && count > 1u);
    addSubmenuAction(rhythmMenu, @"ROTATE NOTES RIGHT 1",
        @selector(rotatePatternRows:), 1, editable && count > 1u);
    [rhythmMenu addItem:NSMenuItem.separatorItem];
    NSMenuItem* thin = addSubmenuAction(rhythmMenu, @"THIN HITS",
        nil, 0, editable);
    NSMenu* thinMenu = [[NSMenu alloc] initWithTitle:@"THIN HITS"];
    thinMenu.autoenablesItems = NO;
    for (const NSInteger amount : { 10, 25, 50 }) {
        addSubmenuAction(thinMenu,
            [NSString stringWithFormat:@"%ld%%",
                static_cast<long>(amount)],
            @selector(thinPatternRows:), amount, editable);
    }
    thin.submenu = thinMenu;
    NSMenuItem* density = addSubmenuAction(rhythmMenu, @"SET HIT DENSITY",
        nil, 0, editable);
    NSMenu* densityMenu = [[NSMenu alloc] initWithTitle:@"SET HIT DENSITY"];
    densityMenu.autoenablesItems = NO;
    for (const NSInteger amount : { 25, 50, 75 }) {
        addSubmenuAction(densityMenu,
            [NSString stringWithFormat:@"%ld%%",
                static_cast<long>(amount)],
            @selector(densityPatternRows:), amount, editable);
    }
    density.submenu = densityMenu;
    rhythm.submenu = rhythmMenu;
    return menu;
}

- (void)scrollWheel:(NSEvent*)event
{
    [self.scrollView scrollWheel:event];
}

@end

@interface S3GTrackerInstrumentToolboxView : NSView {
@private
    std::array<NSRect, s3g::tracker::kInstrumentRackSlotCount>
        _instanceRects;
    std::array<NSRect, s3g::tracker::kInstrumentTypeCount> _addRects;
    NSRect _zoomOutRect;
    NSRect _zoomResetRect;
    NSRect _zoomInRect;
    CGFloat _scrollOffset;
    CGFloat _maximumScrollOffset;
}
@property(nonatomic, assign) TrackerViewState* trackerState;
@property(nonatomic, weak) S3GTrackerWorkspaceController* owner;
- (instancetype)initWithState:(TrackerViewState*)state
    owner:(S3GTrackerWorkspaceController*)owner;
@end

@implementation S3GTrackerInstrumentToolboxView

- (instancetype)initWithState:(TrackerViewState*)state
    owner:(S3GTrackerWorkspaceController*)owner
{
    self = [super initWithFrame:NSZeroRect];
    if (self) {
        self.trackerState = state;
        self.owner = owner;
        self.accessibilityElement = YES;
        self.accessibilityRole = NSAccessibilityGroupRole;
        self.accessibilityLabel = @"Instrument index and library toolbox";
        self.accessibilityHelp = @"Song Instruments are indexed for tracker INS cells. Click an indexed instrument to assign the selected track. Double-click or use its edit icon to open the editor without changing track assignment. Click plus beside an available type to create another instance. Tracker zoom controls are at the bottom.";
    }
    return self;
}

- (BOOL)isFlipped { return YES; }

- (NSRect)editRect:(std::size_t)rackIndex
{
    const NSRect rect = _instanceRects[rackIndex];
    return NSMakeRect(NSMaxX(rect) - 30.0, NSMinY(rect), 30.0,
        NSHeight(rect));
}

- (void)mouseDown:(NSEvent*)event
{
    const NSPoint point = [self convertPoint:event.locationInWindow fromView:nil];
    auto* state = self.trackerState;
    if (!state) return;
    const CGFloat listBottom = NSHeight(self.bounds) - 82.0;
    if (point.y >= 50.0 && point.y < listBottom) {
        for (std::size_t index = 0u;
             index < state->instrumentRack.instruments.size(); ++index) {
            const auto* instrument = s3g::tracker::rackInstrumentAt(
                state->instrumentRack, index);
            if (!instrument || !NSPointInRect(point, _instanceRects[index]))
                continue;
            if ((event.clickCount >= 2
                    || NSPointInRect(point, [self editRect:index]))) {
                [self.owner editRackInstrument:instrument->nodeId];
            } else {
                [self.owner assignTrackInstrument:instrument->nodeId];
            }
            return;
        }
        for (std::size_t index = 0u; index < _addRects.size(); ++index) {
            if (!NSPointInRect(point, _addRects[index])) continue;
            const auto* type = s3g::tracker::instrumentType(index);
            if (type && s3g::tracker::canAddInstrumentInstance(
                    state->instrumentRack, type->kind)) {
                [self.owner addInstrumentKind:type->kind];
            } else NSBeep();
            return;
        }
    }
    if (NSPointInRect(point, _zoomOutRect)) [self.owner zoomTrackerOut];
    else if (NSPointInRect(point, _zoomResetRect))
        [self.owner resetTrackerZoom];
    else if (NSPointInRect(point, _zoomInRect)) [self.owner zoomTrackerIn];
}

- (void)scrollWheel:(NSEvent*)event
{
    if (_maximumScrollOffset <= 0.0) {
        [super scrollWheel:event];
        return;
    }
    _scrollOffset = std::clamp(_scrollOffset - event.scrollingDeltaY,
        0.0, _maximumScrollOffset);
    [self setNeedsDisplay:YES];
    [self.window invalidateCursorRectsForView:self];
}

- (void)resetCursorRects
{
    [super resetCursorRects];
    auto* state = self.trackerState;
    const NSRect listViewport = NSMakeRect(0.0, 50.0,
        NSWidth(self.bounds), std::max<CGFloat>(0.0,
            NSHeight(self.bounds) - 132.0));
    if (state) {
        for (std::size_t index = 0u;
             index < state->instrumentRack.instruments.size(); ++index) {
            const auto* instrument = s3g::tracker::rackInstrumentAt(
                state->instrumentRack, index);
            if (instrument) {
                const NSRect visible = NSIntersectionRect(
                    [self editRect:index], listViewport);
                if (!NSIsEmptyRect(visible))
                    [self addCursorRect:visible
                        cursor:NSCursor.pointingHandCursor];
            }
        }
        for (std::size_t index = 0u; index < _addRects.size(); ++index) {
            const auto* type = s3g::tracker::instrumentType(index);
            if (type && s3g::tracker::canAddInstrumentInstance(
                    state->instrumentRack, type->kind)) {
                const NSRect visible = NSIntersectionRect(
                    _addRects[index], listViewport);
                if (!NSIsEmptyRect(visible))
                    [self addCursorRect:visible
                        cursor:NSCursor.pointingHandCursor];
            }
        }
    }
    [self addCursorRect:_zoomOutRect cursor:NSCursor.pointingHandCursor];
    [self addCursorRect:_zoomResetRect cursor:NSCursor.pointingHandCursor];
    [self addCursorRect:_zoomInRect cursor:NSCursor.pointingHandCursor];
}

- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    fillRect(self.bounds, trackerColor(0x0d0d0d));
    fillRect(NSMakeRect(0.0, 0.0, 1.0, NSHeight(self.bounds)),
        trackerColor(0x404040));
    drawText(@"INSTRUMENTS", NSMakeRect(14.0, 14.0,
        NSWidth(self.bounds) - 28.0, 16.0), trackerColor(0xa8a8a8), 10.0,
        NSFontWeightMedium);
    drawText(@"SONG INDEX", NSMakeRect(14.0, 33.0,
        NSWidth(self.bounds) - 28.0, 12.0), trackerColor(0x777777), 7.0,
        NSFontWeightMedium);
    auto* state = self.trackerState;
    _instanceRects.fill(NSZeroRect);
    _addRects.fill(NSZeroRect);
    if (!state) return;
    uint32_t laneDefault = s3g::tracker::kInvalidInstrumentNode;
    if (!state->session.pattern.tracks.empty()) {
        const auto lane = std::min(state->session.selectedTrack,
            state->session.pattern.tracks.size() - 1u);
        laneDefault = state->session.pattern.tracks[lane]
            .initialInstrumentNodeId;
    }
    const uint32_t selected = state->selectedRackInstrument;
    constexpr CGFloat rowGap = 5.0;
    const CGFloat zoomTop = NSHeight(self.bounds) - 74.0;
    const auto activeCount = s3g::tracker::activeInstrumentCount(
        state->instrumentRack);
    const CGFloat rowHeight = activeCount == 0u ? 47.0 : std::clamp(
        (zoomTop - 52.0 - 161.0) / static_cast<CGFloat>(activeCount)
            - rowGap,
        34.0, 47.0);
    const CGFloat rawLibraryTop = 52.0 + static_cast<CGFloat>(activeCount)
        * (rowHeight + rowGap) + 13.0;
    const CGFloat contentBottom = rawLibraryTop + 20.0
        + static_cast<CGFloat>(_addRects.size()) * 40.0;
    _maximumScrollOffset = std::max<CGFloat>(0.0,
        contentBottom - (zoomTop - 8.0));
    _scrollOffset = std::clamp(_scrollOffset, 0.0, _maximumScrollOffset);
    CGFloat top = 52.0 - _scrollOffset;
    [NSGraphicsContext saveGraphicsState];
    NSRectClip(NSMakeRect(0.0, 50.0, NSWidth(self.bounds),
        std::max<CGFloat>(0.0, zoomTop - 58.0)));
    std::size_t drawn = 0u;
    for (std::size_t index = 0u; index < state->instrumentRack.instruments.size();
         ++index) {
        const auto* instrument = s3g::tracker::rackInstrumentAt(
            state->instrumentRack, index);
        if (!instrument) continue;
        const NSRect rect = NSMakeRect(12.0,
            top + static_cast<CGFloat>(drawn) * (rowHeight + rowGap),
            NSWidth(self.bounds) - 24.0, rowHeight);
        _instanceRects[index] = rect;
        ++drawn;
        const bool active = laneDefault == instrument->nodeId;
        const bool focused = selected == instrument->nodeId;
        fillRect(rect, trackerColor(active ? 0x282828
            : focused ? 0x202020 : 0x151515));
        strokeRect(NSInsetRect(rect, 0.5, 0.5),
            trackerColor(active ? 0xbcbcbc
                : focused ? 0x909090 : 0x454545), active ? 1.5 : 1.0);
        NSString* route = s3g::tracker::instrumentRoutesToMidi(instrument->kind)
            ? @"MIDI" : @"INT";
        drawText([NSString stringWithFormat:@"%02lu  %@",
            static_cast<unsigned long>(index),
            nsString(std::string(instrument->name))],
            NSMakeRect(NSMinX(rect) + 8.0, NSMinY(rect) + 7.0,
                NSWidth(rect) - 40.0, 14.0),
            trackerColor(active ? 0xc8c8c8 : 0xa8a8a8), 8.5,
            NSFontWeightMedium);
        drawText(@"↗", NSInsetRect([self editRect:index], 8.0, 14.0),
            trackerColor(active ? 0xb0b0b0 : 0x707070), 8.0,
            NSFontWeightMedium, NSTextAlignmentRight);
        drawText([NSString stringWithFormat:@"%@%@", route,
                active ? @"  •  TRACK DEFAULT" : @""],
            NSMakeRect(NSMinX(rect) + 8.0, NSMaxY(rect) - 17.0,
                NSWidth(rect) - 16.0, 11.0),
            trackerColor(0x777777), 6.8, NSFontWeightMedium,
            NSTextAlignmentLeft);
    }

    const CGFloat libraryTop = top + static_cast<CGFloat>(drawn)
        * (rowHeight + rowGap) + 13.0;
    drawText(@"AVAILABLE", NSMakeRect(14.0, libraryTop,
        NSWidth(self.bounds) - 28.0, 13.0), trackerColor(0x8f8f8f), 7.2,
        NSFontWeightSemibold);
    for (std::size_t index = 0u; index < _addRects.size(); ++index) {
        const auto* type = s3g::tracker::instrumentType(index);
        if (!type) continue;
        const NSRect rect = NSMakeRect(12.0,
            libraryTop + 20.0 + static_cast<CGFloat>(index) * 40.0,
            NSWidth(self.bounds) - 24.0, 34.0);
        fillRect(rect, trackerColor(0x131313));
        strokeRect(NSInsetRect(rect, 0.5, 0.5), trackerColor(0x3d3d3d));
        drawText(nsString(std::string(type->name)), NSMakeRect(
            NSMinX(rect) + 8.0, NSMinY(rect) + 6.0,
            NSWidth(rect) - 74.0, 12.0), trackerColor(0xa8a8a8), 7.5,
            NSFontWeightMedium);
        const auto count = s3g::tracker::activeInstrumentCount(
            state->instrumentRack, type->kind);
        drawText([NSString stringWithFormat:@"%lu / %lu INSTANCES",
                static_cast<unsigned long>(count),
                static_cast<unsigned long>(type->maximumInstances)],
            NSMakeRect(NSMinX(rect) + 8.0, NSMaxY(rect) - 14.0,
                NSWidth(rect) - 74.0, 9.0), trackerColor(0x656565), 6.2,
            NSFontWeightMedium);
        const bool canAdd = s3g::tracker::canAddInstrumentInstance(
            state->instrumentRack, type->kind);
        _addRects[index] = NSMakeRect(NSMaxX(rect) - 56.0,
            NSMinY(rect) + 5.0, 48.0, 24.0);
        fillRect(_addRects[index], trackerColor(canAdd ? 0x303030 : 0x171717));
        strokeRect(NSInsetRect(_addRects[index], 0.5, 0.5),
            trackerColor(canAdd ? 0x909090 : 0x3b3b3b));
        drawText(canAdd ? @"+ ADD" : @"IN USE", NSInsetRect(
            _addRects[index], 3.0, 7.0),
            trackerColor(canAdd ? 0xd0d0d0 : 0x5d5d5d), 6.5,
            NSFontWeightSemibold, NSTextAlignmentCenter);
    }

    [NSGraphicsContext restoreGraphicsState];
    if (_maximumScrollOffset > 0.0) {
        const CGFloat trackHeight = std::max<CGFloat>(1.0, zoomTop - 58.0);
        const CGFloat thumbHeight = std::max<CGFloat>(28.0,
            trackHeight * trackHeight / (trackHeight + _maximumScrollOffset));
        const CGFloat thumbTravel = std::max<CGFloat>(0.0,
            trackHeight - thumbHeight);
        const CGFloat thumbTop = 50.0 + thumbTravel
            * (_scrollOffset / _maximumScrollOffset);
        fillRect(NSMakeRect(NSWidth(self.bounds) - 3.0, thumbTop,
            2.0, thumbHeight), trackerColor(0x696969));
    }

    drawText(@"TRACKER ZOOM", NSMakeRect(14.0, zoomTop,
        NSWidth(self.bounds) - 28.0, 12.0), trackerColor(0x777777), 7.0,
        NSFontWeightSemibold);
    const CGFloat buttonWidth = (NSWidth(self.bounds) - 32.0) / 3.0;
    _zoomOutRect = NSMakeRect(12.0, zoomTop + 20.0, buttonWidth, 30.0);
    _zoomResetRect = NSMakeRect(NSMaxX(_zoomOutRect) + 4.0,
        zoomTop + 20.0, buttonWidth, 30.0);
    _zoomInRect = NSMakeRect(NSMaxX(_zoomResetRect) + 4.0,
        zoomTop + 20.0, buttonWidth, 30.0);
    const NSInteger percent = self.owner.gridScroll
        ? static_cast<NSInteger>(std::lround(
            self.owner.gridScroll.magnification
                / s3g::tracker::app::kTrackerDefaultMagnification * 100.0))
        : 100;
    const std::array<NSRect, 3u> zoomRects {
        _zoomOutRect, _zoomResetRect, _zoomInRect };
    NSArray<NSString*>* zoomLabels = @[ @"−", [NSString
        stringWithFormat:@"%ld%%", static_cast<long>(percent)], @"+" ];
    for (std::size_t index = 0u; index < zoomRects.size(); ++index) {
        fillRect(zoomRects[index], trackerColor(0x242424));
        strokeRect(NSInsetRect(zoomRects[index], 0.5, 0.5),
            trackerColor(0x666666));
        drawText(zoomLabels[index], NSInsetRect(zoomRects[index], 4.0, 8.0),
            trackerColor(0xc8c8c8), 8.0, NSFontWeightMedium,
            NSTextAlignmentCenter);
    }
    [self.window invalidateCursorRectsForView:self];
}

@end

@class S3GTrackerGeometryView;

@interface S3GTrackerGeometryPlaybackOverlayView : NSView
@property(nonatomic, weak) S3GTrackerGeometryView* geometryView;
@end

@interface S3GTrackerGeometryView : NSView <NSTextFieldDelegate> {
    std::array<CGFloat, s3g::tracker::kMaximumTrackCount>
        _readHeadHaloStrength;
    std::array<std::size_t, s3g::tracker::kMaximumTrackCount>
        _readHeadHaloRows;
    std::array<bool, s3g::tracker::kMaximumTrackCount>
        _documentationHitLanes;
    BOOL _documentationPlaybackSnapshot;
    NSTimeInterval _lastReadHeadAnimationTime;
    std::string _lastDisplayedPatternId;
    uint32_t _lastDisplayedSongMuteMask;
    BOOL _geometryGestureActive;
    BOOL _geometryGestureChanged;
    NSInteger _lastGestureRow;
    std::size_t _gestureLane;
    std::size_t _gestureRow;
    CGFloat _velocityStartRadius;
    float _velocityStartValue;
    S3GTrackerGeometryGestureKind _geometryGestureKind;
    BOOL _geometrySliderGesture;
    std::size_t _gestureOriginalLength;
    std::size_t _gesturePreviewLength;
    uint8_t _gestureOriginalDefaultNote;
    uint8_t _gesturePreviewDefaultNote;
    std::size_t _gestureOriginalPhase;
    std::size_t _gesturePreviewPhase;
    int _gesturePreviewRotation;
    std::size_t _gestureOriginalDensity;
    std::size_t _gesturePreviewDensity;
    CGFloat _gestureLastAngle;
    CGFloat _gestureAccumulatedAngle;
    std::vector<NoteCell> _gestureOriginalNotes;
    std::vector<NoteCell> _gesturePreviewNotes;
    S3GTrackerGeometryMenu _openGeometryMenu;
    NSInteger _geometryMenuHoverIndex;
    NSTrackingArea* _geometryTrackingArea;
    std::size_t _selectedBurstSlot;
    std::size_t _selectedBurstEvent;
    NSInteger _selectedBurstField;
    NSRect _burstGestureRect;
    BOOL _burstPlaceFeedbackActive;
    BOOL _burstPreviewFeedbackActive;
    BOOL _pitchPreviewFeedbackActive;
    PitchMapSettings _pitchSettings;
    PitchMapAnalysis _pitchAnalysis;
    PitchMapResult _pitchPreview;
    std::array<int16_t, 256u> _pitchOverrides;
    std::size_t _pitchFirstRow;
    std::size_t _pitchLastRow;
    BOOL _pitchUseFullCycle;
    BOOL _pitchEditingIntervals;
    NSInteger _pitchDragAssignment;
    NSString* _pitchStatus;
}
- (instancetype)initWithState:(TrackerViewState*)state
    owner:(S3GTrackerWorkspaceController*)owner;
- (void)advancePlaybackAnimation;
- (void)refreshPlaybackDisplay;
- (void)drawPlaybackOverlay;
- (NSInteger)prepareDocumentationPlaybackSnapshot;
- (NSString*)displayedPatternId;
- (NSUInteger)displayedLaneCount;
- (NSUInteger)displayedMutedLaneCount;
- (CGFloat)ringRadiusForLane:(std::size_t)lane;
- (void)selectBurstSlot:(std::size_t)slot;
- (void)syncBurstNameControls;
- (void)saveBurstName:(id)sender;
- (void)openPitchMapFirstRow:(std::size_t)firstRow
    lastRow:(std::size_t)lastRow;
- (void)applyPitchMapContour:(PitchContour)contour
    firstRow:(std::size_t)firstRow lastRow:(std::size_t)lastRow;
@property(nonatomic, assign) TrackerViewState* trackerState;
@property(nonatomic, weak) S3GTrackerWorkspaceController* owner;
@property(nonatomic) CGFloat geometryZoom;
@property(nonatomic) S3GTrackerGeometryViewMode geometryViewMode;
@property(nonatomic) BOOL burstLibraryOnly;
@property(nonatomic) S3GTrackerGeometryTool geometryTool;
@property(nonatomic) BOOL linkVelocityLength;
@property(nonatomic, strong) S3GTrackerPopupButton* lanePopup;
@property(nonatomic, strong) S3GTrackerPopupButton* directionPopup;
@property(nonatomic, strong) S3GTrackerPopupButton* viewModePopup;
@property(nonatomic, strong) S3GTrackerPopupButton* morphTargetPopup;
@property(nonatomic, copy) NSArray<S3GTrackerActionButton*>* toolButtons;
@property(nonatomic, strong) S3GTrackerActionButton* rotateBackButton;
@property(nonatomic, strong) S3GTrackerActionButton* rotateForwardButton;
@property(nonatomic, strong) S3GTrackerActionButton* densityDownButton;
@property(nonatomic, strong) S3GTrackerActionButton* densityUpButton;
@property(nonatomic, strong) S3GTrackerActionButton* reverseButton;
@property(nonatomic, strong) S3GTrackerActionButton* reflectButton;
@property(nonatomic, copy) NSArray<S3GTrackerActionButton*>* morphButtons;
@property(nonatomic, strong) S3GTrackerActionButton* revealButton;
@property(nonatomic, strong) S3GTrackerGeometryPlaybackOverlayView*
    playbackOverlay;
@property(nonatomic, strong) NSTextField* burstNameField;
@property(nonatomic, strong) S3GTrackerActionButton* burstSaveButton;
@end

@implementation S3GTrackerGeometryView

- (instancetype)initWithState:(TrackerViewState*)state
    owner:(S3GTrackerWorkspaceController*)owner
{
    self = [super initWithFrame:NSZeroRect];
    if (self) {
        self.trackerState = state;
        self.owner = owner;
        self.geometryZoom = 1.0;
        self.geometryViewMode = S3GTrackerGeometryViewModeRingField;
        self.geometryTool = S3GTrackerGeometryToolSelect;
        _selectedBurstSlot = 0u;
        _selectedBurstEvent = 0u;
        _selectedBurstField = 1;
        _burstGestureRect = NSZeroRect;
        _burstPlaceFeedbackActive = NO;
        _burstPreviewFeedbackActive = NO;
        _pitchPreviewFeedbackActive = NO;
        _pitchSettings = {};
        _pitchSettings.scale = 2u;
        _pitchSettings.contour = PitchContour::VaryExisting;
        _pitchOverrides.fill(-1);
        _pitchFirstRow = 0u;
        _pitchLastRow = 63u;
        _pitchUseFullCycle = YES;
        _pitchEditingIntervals = NO;
        _pitchDragAssignment = -1;
        _pitchStatus = @"READY TO PREVIEW";
        self.linkVelocityLength = YES;
        _lastGestureRow = -1;
        self.playbackOverlay = [[S3GTrackerGeometryPlaybackOverlayView alloc]
            initWithFrame:self.bounds];
        self.playbackOverlay.geometryView = self;
        self.playbackOverlay.autoresizingMask = NSViewWidthSizable
            | NSViewHeightSizable;
        self.playbackOverlay.accessibilityHidden = YES;
        self.playbackOverlay.wantsLayer = YES;
        [self addSubview:self.playbackOverlay];
        self.viewModePopup = [[S3GTrackerPopupButton alloc]
            initWithFrame:NSMakeRect(48.0, 31.0, 184.0, 24.0)
            pullsDown:NO];
        self.viewModePopup.controlSize = NSControlSizeSmall;
        self.viewModePopup.autoresizingMask = NSViewMaxXMargin;
        [self.viewModePopup addItemsWithTitles:@[
            @"RING FIELD", @"ACTIVE PULSES", @"ALL STEPS UNDERLAY",
            @"PHASE SPOKES", @"LANE FOCUS", @"COMPOSITE RING",
            @"BURST EDITOR", @"PITCH MAP"
        ]];
        self.viewModePopup.target = self;
        self.viewModePopup.action = @selector(viewModeChanged:);
        self.viewModePopup.toolTip =
            @"Choose the editable Ring Field or a diagnostic geometry view";
        self.viewModePopup.accessibilityLabel = @"Geometry view mode";
        self.viewModePopup.identifier = @"geometry-view-popup";
        self.viewModePopup.hidden = YES;
        [self addSubview:self.viewModePopup];

        self.lanePopup = [[S3GTrackerPopupButton alloc]
            initWithFrame:NSZeroRect pullsDown:NO];
        self.lanePopup.controlSize = NSControlSizeSmall;
        self.lanePopup.target = self;
        self.lanePopup.action = @selector(lanePopupChanged:);
        self.lanePopup.identifier = @"geometry-lane-popup";
        self.lanePopup.toolTip = @"Select the Tracker lane edited by this toolbox";
        self.lanePopup.accessibilityLabel = @"Geometry lane";
        self.lanePopup.hidden = YES;
        [self addSubview:self.lanePopup];

        self.directionPopup = [[S3GTrackerPopupButton alloc]
            initWithFrame:NSZeroRect pullsDown:NO];
        self.directionPopup.controlSize = NSControlSizeSmall;
        [self.directionPopup addItemsWithTitles:@[
            @"FORWARD  >", @"REVERSE  <", @"PALINDROME  <>", @"RANDOM"
        ]];
        self.directionPopup.target = self;
        self.directionPopup.action = @selector(directionPopupChanged:);
        self.directionPopup.identifier = @"geometry-direction-popup";
        self.directionPopup.toolTip =
            @"Set the selected lane's NOTE traversal direction";
        self.directionPopup.accessibilityLabel = @"Lane direction";
        self.directionPopup.hidden = YES;
        [self addSubview:self.directionPopup];

        self.morphTargetPopup = [[S3GTrackerPopupButton alloc]
            initWithFrame:NSZeroRect pullsDown:NO];
        [self.morphTargetPopup addItemsWithTitles:@[
            @"PREVIOUS LANE", @"NEXT LANE"
        ]];
        [self.morphTargetPopup selectItemAtIndex:1];
        self.morphTargetPopup.identifier = @"geometry-morph-target-popup";
        self.morphTargetPopup.toolTip =
            @"Choose the neighboring visible lane used as the morph source";
        self.morphTargetPopup.accessibilityLabel = @"Geometry morph target";
        self.morphTargetPopup.hidden = YES;
        [self addSubview:self.morphTargetPopup];

        NSMutableArray<S3GTrackerActionButton*>* tools =
            [[NSMutableArray alloc] init];
        NSArray<NSString*>* toolNames = @[
            @"SELECT", @"PAINT", @"ERASE", @"VELOCITY"
        ];
        NSArray<NSString*>* toolHelp = @[
            @"Select a lane and tracker row",
            @"Paint note hits directly into the pattern",
            @"Erase note hits directly from the pattern",
            @"Drag radially to write tracker velocity values"
        ];
        for (NSUInteger index = 0u; index < toolNames.count; ++index) {
            S3GTrackerActionButton* button = [[S3GTrackerActionButton alloc]
                initWithFrame:NSZeroRect];
            button.title = toolNames[static_cast<NSUInteger>(index)];
            button.target = self;
            button.action = @selector(toolChanged:);
            button.tag = 1;
            button.state = index == 0u ? NSControlStateValueOn
                                      : NSControlStateValueOff;
            button.identifier = [NSString stringWithFormat:
                @"geometry-tool-%ld", static_cast<long>(index)];
            button.toolTip = toolHelp[static_cast<NSUInteger>(index)];
            button.accessibilityLabel = [NSString stringWithFormat:
                @"Geometry %@ tool", toolNames[static_cast<NSUInteger>(index)]];
            button.hidden = YES;
            [self addSubview:button];
            [tools addObject:button];
        }
        self.toolButtons = tools;

        auto addOperation = ^S3GTrackerActionButton*(NSString* title,
            SEL action, NSString* label) {
            S3GTrackerActionButton* button = [[S3GTrackerActionButton alloc]
                initWithFrame:NSZeroRect];
            button.title = title;
            button.target = self;
            button.action = action;
            button.accessibilityLabel = label;
            button.hidden = YES;
            [self addSubview:button];
            return button;
        };
        self.rotateBackButton = addOperation(@"−1", @selector(rotateBack:),
            @"Rotate selected geometry backward one step");
        self.rotateForwardButton = addOperation(@"+1",
            @selector(rotateForward:),
            @"Rotate selected geometry forward one step");
        self.densityDownButton = addOperation(@"− HIT",
            @selector(densityDown:),
            @"Remove one evenly distributed hit");
        self.densityUpButton = addOperation(@"+ HIT",
            @selector(densityUp:),
            @"Add one evenly distributed hit");
        self.reverseButton = addOperation(@"REVERSE", @selector(reverse:),
            @"Reverse selected lane geometry");
        self.reflectButton = addOperation(@"REFLECT", @selector(reflect:),
            @"Reflect selected lane around selected row");
        NSMutableArray<S3GTrackerActionButton*>* morphButtons =
            [[NSMutableArray alloc] init];
        for (NSNumber* amount in @[ @25, @50, @75, @100 ]) {
            S3GTrackerActionButton* button = addOperation(
                [NSString stringWithFormat:@"%@%%", amount],
                @selector(morphAmount:),
                [NSString stringWithFormat:
                    @"Morph selected lane %@ percent toward the selected neighboring lane",
                    amount]);
            button.tag = amount.integerValue;
            [morphButtons addObject:button];
        }
        self.morphButtons = morphButtons;
        self.revealButton = addOperation(@"REVEAL IN TRACKER",
            @selector(revealInTracker:),
            @"Reveal the selected geometry cell in Tracker");
        self.burstNameField = [[NSTextField alloc] initWithFrame:NSZeroRect];
        S3GTrackerStyleSuiteTextField(
            self.burstNameField, NSTextAlignmentLeft);
        self.burstNameField.delegate = self;
        self.burstNameField.target = self;
        self.burstNameField.action = @selector(saveBurstName:);
        self.burstNameField.accessibilityLabel = @"Burst name";
        self.burstNameField.accessibilityHelp =
            @"Enter a Burst name, then press Return or SAVE.";
        self.burstNameField.hidden = YES;
        [self addSubview:self.burstNameField];
        self.burstSaveButton = [[S3GTrackerActionButton alloc]
            initWithFrame:NSZeroRect];
        self.burstSaveButton.s3gUsesSuiteStyle = YES;
        self.burstSaveButton.title = @"SAVE";
        self.burstSaveButton.target = self;
        self.burstSaveButton.action = @selector(saveBurstName:);
        self.burstSaveButton.accessibilityLabel = @"Save Burst name";
        self.burstSaveButton.toolTip =
            @"Save the NAME field to the selected Burst";
        self.burstSaveButton.hidden = YES;
        [self addSubview:self.burstSaveButton];
        _openGeometryMenu = S3GTrackerGeometryMenuNone;
        _geometryMenuHoverIndex = -1;
        self.accessibilityElement = YES;
        self.accessibilityRole = NSAccessibilityGroupRole;
        self.accessibilityLabel = @"Rhythm geometry";
        self.accessibilityHelp = @"Ring Field edits the same lanes and rows as Tracker. The Lane and Cycle toolbox sets lane, default pitch, note length, direction, row rotation, density, and optional linked velocity length. Drag the R diamond to rotate authored rows or the D arc handle for density; edits preview before one undoable commit. Morph can use the previous or next visible lane at 25, 50, 75, or 100 percent. Choose Select, Paint, Erase, or Velocity. Option-drag with Paint temporarily erases, and double-clicking a bead reveals it in Tracker. Burst Editor shows all substeps in a matrix and follows their shared row clock during playback; gate is a percentage of one complete Tracker row measured from each substep onset. Pitch Map fits, generates, or manually shapes selected NOTE cells without changing rests, note symbols, or Burst cells. Absolute Contour and scale-degree Interval graphs appear together and share selection. Interval keeps the first note anchored and shifts the selected note plus the following phrase. Transpose, invert, and reverse remain preview-only until Apply; Preview auditions the result at project BPM while transport is stopped. Space toggles playback.";
    }
    return self;
}

- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }

- (layout::TrackerGeometryFamilyLayout)geometryLayout
{
    return layout::trackerGeometryFamilyLayout({
        static_cast<double>(NSWidth(self.bounds)),
        static_cast<double>(NSHeight(self.bounds)),
    }, self.geometryViewMode == S3GTrackerGeometryViewModePitchMap
        ? 7u : 4u,
        self.geometryViewMode == S3GTrackerGeometryViewModeBurst ? 8u : 7u,
        !self.burstLibraryOnly);
}

- (NSRect)canvasRect
{
    return s3g::clap_gui::cocoaRect([self geometryLayout].fieldPanel);
}

- (NSRect)canvasPlotRect
{
    const NSRect canvas = [self canvasRect];
    return NSMakeRect(NSMinX(canvas) + 1.0,
        NSMinY(canvas) + layout::kStandardMetrics.headerHeight + 1.0,
        std::max<CGFloat>(1.0, NSWidth(canvas) - 2.0),
        std::max<CGFloat>(1.0, NSHeight(canvas)
            - layout::kStandardMetrics.headerHeight - 2.0));
}

- (NSRect)inspectorRect
{
    const auto geometry = [self geometryLayout];
    return NSMakeRect(geometry.inspectorColumn.x,
        geometry.inspectorColumn.top, geometry.inspectorColumn.width,
        NSHeight(self.bounds) - geometry.inspectorColumn.top - 18.0);
}

- (NSRect)laneCyclePanelRect
{
    return s3g::clap_gui::cocoaRect(
        [self geometryLayout].laneCycle.frame);
}

- (NSRect)editPanelRect
{
    return s3g::clap_gui::cocoaRect(
        [self geometryLayout].editShape.frame);
}

- (NSRect)viewPanelRect
{
    return s3g::clap_gui::cocoaRect([self geometryLayout].view.frame);
}

- (NSRect)bridgePanelRect
{
    return s3g::clap_gui::cocoaRect(
        [self geometryLayout].trackerBridge.frame);
}

- (NSRect)geometrySliderTrackForRow:(uint32_t)row
{
    const auto panel = [self geometryLayout].laneCycle;
    const CGFloat panelX = static_cast<CGFloat>(panel.frame.x);
    return NSMakeRect(
        static_cast<CGFloat>(layout::processorControlX(panelX)),
        static_cast<CGFloat>(layout::rowY(panel, row)) + 1.0,
        static_cast<CGFloat>(layout::processorTrackWidth(panel.frame.width)),
        9.0);
}

- (NSRect)lengthSliderTrack
{
    return [self geometrySliderTrackForRow:2u];
}

- (NSRect)defaultNoteSliderTrack
{
    const auto panel = [self geometryLayout].laneCycle;
    const CGFloat panelX = static_cast<CGFloat>(panel.frame.x);
    return NSMakeRect(
        static_cast<CGFloat>(layout::processorControlX(panelX)),
        static_cast<CGFloat>(layout::rowY(panel, 1u)) + 1.0,
        static_cast<CGFloat>(layout::processorTrackWidth(
            panel.frame.width, kGeometryNoteValueWidth)), 9.0);
}

- (NSRect)rotateSliderTrack
{
    return [self geometrySliderTrackForRow:4u];
}

- (NSRect)densitySliderTrack
{
    return [self geometrySliderTrackForRow:5u];
}

- (NSRect)sliderHitRect:(NSRect)track
{
    const auto panel = [self geometryLayout].laneCycle;
    uint32_t row = 1u;
    if (NSEqualRects(track, [self lengthSliderTrack])) row = 2u;
    else if (NSEqualRects(track, [self rotateSliderTrack])) row = 4u;
    else if (NSEqualRects(track, [self densitySliderTrack])) row = 5u;
    return s3g::clap_gui::cocoaRect(layout::sliderHitRect(panel, row));
}

- (NSRect)laneMenuBoxRect
{
    return s3g::clap_gui::cocoaRect(layout::processorMenuBoxRect(
        [self geometryLayout].laneCycle, 0u));
}

- (NSRect)directionMenuBoxRect
{
    return s3g::clap_gui::cocoaRect(layout::processorMenuBoxRect(
        [self geometryLayout].laneCycle, 3u));
}

- (NSRect)viewMenuBoxRect
{
    return s3g::clap_gui::cocoaRect(layout::processorMenuBoxRect(
        [self geometryLayout].view, 0u));
}

- (NSRect)morphTargetMenuBoxRect
{
    return s3g::clap_gui::cocoaRect(layout::processorMenuBoxRect(
        [self geometryLayout].editShape, 2u));
}

- (NSRect)linkVelocityLengthToggleRect
{
    return s3g::clap_gui::cocoaRect(layout::processorMenuBoxRect(
        [self geometryLayout].laneCycle, 6u));
}

- (NSRect)burstSlotMenuBoxRect
{
    return s3g::clap_gui::cocoaRect(layout::processorMenuBoxRect(
        [self geometryLayout].laneCycle, 0u));
}

- (NSRect)burstEventMenuBoxRect
{
    return s3g::clap_gui::cocoaRect(layout::processorMenuBoxRect(
        [self geometryLayout].editShape, 0u));
}

- (NSRect)burstNameBoxRect
{
    return s3g::clap_gui::cocoaRect(layout::processorMenuBoxRect(
        [self geometryLayout].laneCycle, 1u));
}

- (NSRect)pitchScopeMenuBoxRect
{
    return s3g::clap_gui::cocoaRect(layout::processorMenuBoxRect(
        [self geometryLayout].laneCycle, 1u));
}

- (NSRect)pitchRootMenuBoxRect
{
    return s3g::clap_gui::cocoaRect(layout::processorMenuBoxRect(
        [self geometryLayout].laneCycle, 2u));
}

- (NSRect)pitchScaleMenuBoxRect
{
    return s3g::clap_gui::cocoaRect(layout::processorMenuBoxRect(
        [self geometryLayout].laneCycle, 3u));
}

- (NSRect)pitchMinimumSliderTrack
{
    const auto panel = [self geometryLayout].laneCycle;
    return NSMakeRect(static_cast<CGFloat>(layout::processorControlX(
            panel.frame.x)), static_cast<CGFloat>(layout::rowY(panel, 4u))
            + 1.0, static_cast<CGFloat>(layout::processorTrackWidth(
            panel.frame.width, kGeometryNoteValueWidth)), 9.0);
}

- (NSRect)pitchMaximumSliderTrack
{
    NSRect track = [self pitchMinimumSliderTrack];
    track.origin.y = static_cast<CGFloat>(layout::rowY(
        [self geometryLayout].laneCycle, 5u)) + 1.0;
    return track;
}

- (NSRect)pitchContourMenuBoxRect
{
    return s3g::clap_gui::cocoaRect(layout::processorMenuBoxRect(
        [self geometryLayout].editShape, 0u));
}

- (NSRect)pitchLeapMenuBoxRect
{
    return s3g::clap_gui::cocoaRect(layout::processorMenuBoxRect(
        [self geometryLayout].editShape, 1u));
}

- (NSRect)pitchVariationSliderTrack
{
    const auto panel = [self geometryLayout].editShape;
    return NSMakeRect(static_cast<CGFloat>(layout::processorControlX(
            panel.frame.x)), static_cast<CGFloat>(layout::rowY(panel, 2u))
            + 1.0, static_cast<CGFloat>(layout::processorTrackWidth(
            panel.frame.width)), 9.0);
}

- (NSRect)pitchTransposeSliderTrack
{
    const auto panel = [self geometryLayout].editShape;
    return NSMakeRect(static_cast<CGFloat>(layout::processorControlX(
            panel.frame.x)), static_cast<CGFloat>(layout::rowY(panel, 4u))
            + 1.0, static_cast<CGFloat>(layout::processorTrackWidth(
            panel.frame.width)), 9.0);
}

- (NSRect)pitchInvertToggleRect
{
    return s3g::clap_gui::cocoaRect(layout::processorMenuBoxRect(
        [self geometryLayout].editShape, 5u));
}

- (NSRect)pitchReverseToggleRect
{
    return s3g::clap_gui::cocoaRect(layout::processorMenuBoxRect(
        [self geometryLayout].editShape, 6u));
}

- (NSRect)pitchAnchorToggleRect
{
    return s3g::clap_gui::cocoaRect(layout::processorMenuBoxRect(
        [self geometryLayout].editShape, 3u));
}

- (NSRect)pitchAnalyzeHeaderButtonRect
{
    const NSRect header = [self laneCyclePanelRect];
    return NSMakeRect(NSMaxX(header) - 86.0, NSMinY(header) + 3.0,
        74.0, 15.0);
}

- (NSRect)pitchNewSeedHeaderButtonRect
{
    const NSRect header = [self editPanelRect];
    return NSMakeRect(NSMaxX(header) - 88.0, NSMinY(header) + 3.0,
        76.0, 15.0);
}

- (NSRect)pitchPreviewHeaderButtonRect
{
    const NSRect header = [self editPanelRect];
    return NSMakeRect(NSMaxX(header) - 164.0, NSMinY(header) + 3.0,
        70.0, 15.0);
}

- (NSRect)pitchApplyHeaderButtonRect
{
    const NSRect header = [self bridgePanelRect];
    return NSMakeRect(NSMaxX(header) - 76.0, NSMinY(header) + 3.0,
        64.0, 15.0);
}

- (NSRect)pitchGraphRect
{
    const NSRect plot = [self canvasPlotRect];
    const CGFloat top = NSMinY(plot) + 42.0;
    const CGFloat bottom = NSMaxY(plot) - 42.0;
    constexpr CGFloat gap = 62.0;
    const CGFloat available = std::max<CGFloat>(120.0,
        bottom - top - gap);
    const CGFloat height = std::floor(available * 0.58);
    return NSMakeRect(NSMinX(plot) + 54.0, top,
        std::max<CGFloat>(1.0, NSWidth(plot) - 108.0), height);
}

- (NSRect)pitchIntervalGraphRect
{
    const NSRect plot = [self canvasPlotRect];
    const NSRect contour = [self pitchGraphRect];
    constexpr CGFloat gap = 62.0;
    const CGFloat y = NSMaxY(contour) + gap;
    return NSMakeRect(NSMinX(contour), y, NSWidth(contour),
        std::max<CGFloat>(80.0, NSMaxY(plot) - 42.0 - y));
}

- (BOOL)pitchMapNoteMatchesScale:(uint8_t)note
{
    const auto& scale = s3g::musicalScaleDefinition(_pitchSettings.scale);
    const int effectiveRoot = (static_cast<int>(
        _pitchSettings.rootPitchClass) + static_cast<int>(
            _pitchSettings.transposeSemitones) + 120) % 12;
    const uint8_t relative = static_cast<uint8_t>((note + 12u
        - static_cast<uint8_t>(effectiveRoot)) % 12u);
    for (uint32_t degree = 0u; degree < scale.size; ++degree)
        if (static_cast<uint8_t>(scale.semitones[degree]) == relative)
            return YES;
    return NO;
}

- (int)pitchScaleOrdinalForNote:(int)note
{
    note = std::clamp(note, 0, 127);
    int ordinal = 0;
    int nearestOrdinal = 0;
    int nearestDistance = 128;
    for (int candidate = 0; candidate <= 127; ++candidate) {
        if (![self pitchMapNoteMatchesScale:
                static_cast<uint8_t>(candidate)]) continue;
        const int distance = std::abs(candidate - note);
        if (distance < nearestDistance) {
            nearestDistance = distance;
            nearestOrdinal = ordinal;
        }
        if (candidate == note) return ordinal;
        ++ordinal;
    }
    return nearestOrdinal;
}

- (int)pitchScaleNoteForOrdinal:(int)requestedOrdinal
{
    int noteCount = 0;
    for (int note = 0; note <= 127; ++note)
        if ([self pitchMapNoteMatchesScale:static_cast<uint8_t>(note)])
            ++noteCount;
    if (noteCount == 0) return 60;
    requestedOrdinal = std::clamp(requestedOrdinal, 0, noteCount - 1);
    int ordinal = 0;
    for (int note = 0; note <= 127; ++note) {
        if (![self pitchMapNoteMatchesScale:static_cast<uint8_t>(note)])
            continue;
        if (ordinal == requestedOrdinal) return note;
        ++ordinal;
    }
    return 127;
}

- (int)pitchIntervalAtIndex:(std::size_t)index original:(BOOL)original
{
    if (index == 0u || index >= _pitchPreview.assignments.size()) return 0;
    const auto& previous = _pitchPreview.assignments[index - 1u];
    const auto& current = _pitchPreview.assignments[index];
    const int previousNote = original
        ? previous.originalNote : previous.note;
    const int currentNote = original ? current.originalNote : current.note;
    return [self pitchScaleOrdinalForNote:currentNote]
        - [self pitchScaleOrdinalForNote:previousNote];
}

- (int)pitchIntervalExtent
{
    int extent = std::max<int>(4, _pitchSettings.maximumLeapDegrees);
    for (std::size_t index = 1u;
         index < _pitchPreview.assignments.size(); ++index) {
        extent = std::max(extent,
            std::abs([self pitchIntervalAtIndex:index original:NO]));
        extent = std::max(extent,
            std::abs([self pitchIntervalAtIndex:index original:YES]));
    }
    return extent;
}

- (int)pitchMapDisplayMinimum
{
    int minimum = std::min<int>(_pitchSettings.minimumNote,
        std::clamp<int>(static_cast<int>(_pitchSettings.minimumNote)
            + _pitchSettings.transposeSemitones, 0, 127));
    for (const auto& assignment : _pitchPreview.assignments) {
        for (std::size_t voice = 0u; voice < assignment.voiceCount; ++voice) {
            minimum = std::min<int>(minimum, assignment.originalNotes[voice]);
            minimum = std::min<int>(minimum, assignment.notes[voice]);
        }
    }
    return minimum;
}

- (int)pitchMapDisplayMaximum
{
    int maximum = std::max<int>(_pitchSettings.maximumNote,
        std::clamp<int>(static_cast<int>(_pitchSettings.maximumNote)
            + _pitchSettings.transposeSemitones, 0, 127));
    for (const auto& assignment : _pitchPreview.assignments) {
        for (std::size_t voice = 0u; voice < assignment.voiceCount; ++voice) {
            maximum = std::max<int>(maximum, assignment.originalNotes[voice]);
            maximum = std::max<int>(maximum, assignment.notes[voice]);
        }
    }
    return maximum;
}

- (NSPoint)pitchMapPointForAssignment:(const PitchMapAssignment&)assignment
{
    std::size_t first = 0u;
    std::size_t last = 0u;
    [self pitchMapRowsFirst:&first last:&last];
    const NSRect graph = [self pitchGraphRect];
    const CGFloat rowSpan = static_cast<CGFloat>(std::max<std::size_t>(
        1u, last - first));
    const int displayMinimum = [self pitchMapDisplayMinimum];
    const int displayMaximum = [self pitchMapDisplayMaximum];
    const CGFloat pitchSpan = static_cast<CGFloat>(std::max<int>(1,
        displayMaximum - displayMinimum));
    return NSMakePoint(NSMinX(graph)
            + static_cast<CGFloat>(assignment.row - first) / rowSpan
                * NSWidth(graph),
        NSMaxY(graph) - static_cast<CGFloat>(std::clamp(
            static_cast<int>(assignment.note)
                - displayMinimum,
            0, displayMaximum - displayMinimum))
            / pitchSpan * NSHeight(graph));
}

- (NSPoint)pitchMapPointForAssignmentAtIndex:(std::size_t)index
    original:(BOOL)original
{
    return [self pitchMapPointForAssignmentAtIndex:index
        original:original interval:NO];
}

- (NSPoint)pitchMapPointForAssignmentAtIndex:(std::size_t)index
    original:(BOOL)original interval:(BOOL)interval
{
    if (index >= _pitchPreview.assignments.size()) return NSZeroPoint;
    if (!interval) {
        PitchMapAssignment assignment = _pitchPreview.assignments[index];
        if (original) assignment.note = assignment.originalNote;
        return [self pitchMapPointForAssignment:assignment];
    }
    std::size_t first = 0u;
    std::size_t last = 0u;
    [self pitchMapRowsFirst:&first last:&last];
    const NSRect graph = [self pitchIntervalGraphRect];
    const auto row = _pitchPreview.assignments[index].row;
    const CGFloat x = NSMinX(graph)
        + static_cast<CGFloat>(row - first)
            / static_cast<CGFloat>(std::max<std::size_t>(1u, last - first))
            * NSWidth(graph);
    const int extent = [self pitchIntervalExtent];
    const int degreeInterval = [self pitchIntervalAtIndex:index
        original:original];
    const CGFloat usableHalfHeight = std::max<CGFloat>(1.0,
        NSHeight(graph) * 0.5 - 12.0);
    const CGFloat y = NSMidY(graph)
        - static_cast<CGFloat>(degreeInterval)
            / static_cast<CGFloat>(std::max(1, extent)) * usableHalfHeight;
    return NSMakePoint(x, y);
}

- (NSInteger)pitchMapAssignmentAtPoint:(NSPoint)point
{
    [self refreshPitchMapPreview];
    const BOOL interval = NSPointInRect(point, [self pitchIntervalGraphRect]);
    if (!interval && !NSPointInRect(point, [self pitchGraphRect])) return -1;
    NSInteger result = -1;
    CGFloat best = 12.0;
    for (std::size_t index = 0u;
         index < _pitchPreview.assignments.size(); ++index) {
        const NSPoint marker = [self pitchMapPointForAssignmentAtIndex:index
            original:NO interval:interval];
        const CGFloat distance = std::hypot(
            point.x - marker.x, point.y - marker.y);
        if (distance >= best) continue;
        best = distance;
        result = static_cast<NSInteger>(index);
    }
    if (result >= 0) _pitchEditingIntervals = interval;
    return result;
}

- (NSString*)pitchSelectedPointFlagText
{
    auto* model = self.trackerState;
    if (!model) return nil;
    [self refreshPitchMapPreview];
    for (const auto& assignment : _pitchPreview.assignments) {
        if (assignment.row != model->session.selectedRow) continue;
        return pitchAssignmentText(assignment);
    }
    return nil;
}

- (void)updatePitchMapPointAtPoint:(NSPoint)point
{
    if (_pitchDragAssignment < 0
        || static_cast<std::size_t>(_pitchDragAssignment)
            >= _pitchPreview.assignments.size()) return;
    const auto assignmentIndex = static_cast<std::size_t>(
        _pitchDragAssignment);
    if (_pitchEditingIntervals) {
        if (assignmentIndex == 0u) {
            _pitchStatus = @"FIRST NOTE IS THE INTERVAL ANCHOR";
            [self setNeedsDisplay:YES];
            return;
        }
        const NSRect intervalGraph = [self pitchIntervalGraphRect];
        const CGFloat usableHalfHeight = std::max<CGFloat>(1.0,
            NSHeight(intervalGraph) * 0.5 - 12.0);
        const int extent = [self pitchIntervalExtent];
        const int requestedInterval = std::clamp<int>(
            static_cast<int>(std::lround(
                (NSMidY(intervalGraph) - point.y) / usableHalfHeight
                    * static_cast<CGFloat>(extent))),
            -extent, extent);
        const int previousOrdinal = [self pitchScaleOrdinalForNote:
            _pitchPreview.assignments[assignmentIndex - 1u].note];
        const int currentOrdinal = [self pitchScaleOrdinalForNote:
            _pitchPreview.assignments[assignmentIndex].note];
        int ordinalShift = previousOrdinal + requestedInterval
            - currentOrdinal;

        int minimumAllowedOrdinal = 128;
        int maximumAllowedOrdinal = -1;
        for (int note = [self pitchMapDisplayMinimum];
             note <= [self pitchMapDisplayMaximum]; ++note) {
            if (![self pitchMapNoteMatchesScale:static_cast<uint8_t>(note)])
                continue;
            const int ordinal = [self pitchScaleOrdinalForNote:note];
            minimumAllowedOrdinal = std::min(minimumAllowedOrdinal, ordinal);
            maximumAllowedOrdinal = std::max(maximumAllowedOrdinal, ordinal);
        }
        int minimumTailOrdinal = 128;
        int maximumTailOrdinal = -1;
        for (std::size_t index = assignmentIndex;
             index < _pitchPreview.assignments.size(); ++index) {
            const int ordinal = [self pitchScaleOrdinalForNote:
                _pitchPreview.assignments[index].note];
            minimumTailOrdinal = std::min(minimumTailOrdinal, ordinal);
            maximumTailOrdinal = std::max(maximumTailOrdinal, ordinal);
        }
        if (maximumAllowedOrdinal >= minimumAllowedOrdinal
            && maximumTailOrdinal >= minimumTailOrdinal) {
            ordinalShift = std::clamp(ordinalShift,
                minimumAllowedOrdinal - minimumTailOrdinal,
                maximumAllowedOrdinal - maximumTailOrdinal);
        }
        if (ordinalShift != 0) {
            for (std::size_t index = assignmentIndex;
                 index < _pitchPreview.assignments.size(); ++index) {
                const int ordinal = [self pitchScaleOrdinalForNote:
                    _pitchPreview.assignments[index].note];
                const int note = [self pitchScaleNoteForOrdinal:
                    ordinal + ordinalShift];
                const auto row = _pitchPreview.assignments[index].row;
                if (row < _pitchOverrides.size())
                    _pitchOverrides[row] = static_cast<int16_t>(note);
            }
            _geometryGestureChanged = YES;
            [self refreshPitchMapPreview];
        }
        const int resolvedInterval = [self pitchIntervalAtIndex:
            assignmentIndex original:NO];
        _pitchStatus = [NSString stringWithFormat:
            @"INTERVAL %+d DEG · FOLLOWING NOTES SHIFTED", resolvedInterval];
        [self setNeedsDisplay:YES];
        return;
    }
    const NSRect graph = [self pitchGraphRect];
    const CGFloat normalized = std::clamp(
        (NSMaxY(graph) - point.y) / std::max<CGFloat>(1.0, NSHeight(graph)),
        0.0, 1.0);
    const int displayMinimum = [self pitchMapDisplayMinimum];
    const int displayMaximum = [self pitchMapDisplayMaximum];
    const int raw = static_cast<int>(std::lround(
        static_cast<CGFloat>(displayMinimum)
            + normalized * static_cast<CGFloat>(
                displayMaximum - displayMinimum)));
    int nearest = raw;
    int best = 128;
    for (int note = displayMinimum; note <= displayMaximum; ++note) {
        if (![self pitchMapNoteMatchesScale:static_cast<uint8_t>(note)])
            continue;
        const int distance = std::abs(note - raw);
        if (distance >= best) continue;
        best = distance;
        nearest = note;
    }
    if (best == 128) return;
    const auto row = _pitchPreview.assignments[assignmentIndex].row;
    if (row < _pitchOverrides.size())
        _pitchOverrides[row] = static_cast<int16_t>(nearest);
    _pitchStatus = @"POINT OVERRIDE · SCALE SNAPPED";
    [self refreshPitchMapPreview];
    [self setNeedsDisplay:YES];
}

- (NSRect)burstSliderTrackForRow:(uint32_t)row
{
    const auto panel = [self geometryLayout].editShape;
    return NSMakeRect(static_cast<CGFloat>(layout::processorControlX(
            panel.frame.x)), static_cast<CGFloat>(layout::rowY(panel, row))
            + 1.0, static_cast<CGFloat>(layout::processorTrackWidth(
            panel.frame.width, 72.0)), 9.0);
}

- (NSRect)burstActionRectForRow:(uint32_t)row index:(NSUInteger)index
    count:(NSUInteger)count
{
    const auto panel = [self geometryLayout].laneCycle;
    const NSRect controls = s3g::clap_gui::cocoaRect(
        layout::processorMenuBoxRect(panel, row));
    const CGFloat gap = 3.0;
    const CGFloat width = (NSWidth(controls)
        - gap * static_cast<CGFloat>(count - 1u))
        / static_cast<CGFloat>(count);
    return NSMakeRect(NSMinX(controls)
            + static_cast<CGFloat>(index) * (width + gap),
        NSMinY(controls), width, NSHeight(controls));
}

- (NSRect)editToolButtonRect:(NSUInteger)index
{
    const auto panel = [self geometryLayout].editShape;
    const NSRect controls = s3g::clap_gui::cocoaRect(
        layout::processorMenuBoxRect(panel, 0u));
    const CGFloat gap = 3.0;
    const CGFloat width = (NSWidth(controls) - gap * 3.0) * 0.25;
    return NSMakeRect(NSMinX(controls) + (width + gap) * index,
        NSMinY(controls), width, NSHeight(controls));
}

- (NSRect)reverseButtonRect
{
    const auto panel = [self geometryLayout].editShape;
    NSRect controls = s3g::clap_gui::cocoaRect(
        layout::processorMenuBoxRect(panel, 1u));
    controls.size.width = (controls.size.width - 4.0) * 0.5;
    return controls;
}

- (NSRect)reflectButtonRect
{
    NSRect rect = [self reverseButtonRect];
    rect.origin.x = NSMaxX(rect) + 4.0;
    return rect;
}

- (NSRect)morphAmountButtonRect:(NSUInteger)index
{
    const auto panel = [self geometryLayout].editShape;
    const NSRect controls = s3g::clap_gui::cocoaRect(
        layout::processorMenuBoxRect(panel, 3u));
    const CGFloat gap = 3.0;
    const CGFloat width = (NSWidth(controls) - gap * 3.0) * 0.25;
    return NSMakeRect(NSMinX(controls) + (width + gap) * index,
        NSMinY(controls), width, NSHeight(controls));
}

- (NSRect)revealHeaderButtonRect
{
    const NSRect header = [self bridgePanelRect];
    return NSMakeRect(NSMaxX(header) - 154.0, NSMinY(header) + 3.0,
        142.0, 15.0);
}

- (NSRect)fitBurstGatesHeaderButtonRect
{
    const NSRect header = [self editPanelRect];
    return NSMakeRect(NSMaxX(header) - 210.0, NSMinY(header) + 3.0,
        116.0, 15.0);
}

- (NSRect)burstPreviewHeaderButtonRect
{
    const NSRect header = [self editPanelRect];
    return NSMakeRect(NSMaxX(header) - 90.0, NSMinY(header) + 3.0,
        78.0, 15.0);
}

- (NSRect)burstRenameHeaderButtonRect
{
    const NSRect header = [self laneCyclePanelRect];
    return NSMakeRect(NSMaxX(header) - 82.0, NSMinY(header) + 3.0,
        70.0, 15.0);
}

- (void)layout
{
    [super layout];
    NSRect nameFrame = [self burstNameBoxRect];
    nameFrame.origin.y -= 7.0;
    nameFrame.size.height = static_cast<CGFloat>(
        layout::kStandardMetrics.hitHeight);
    self.burstNameField.frame = nameFrame;
    self.burstSaveButton.frame = [self burstRenameHeaderButtonRect];
    [self syncBurstNameControls];
    [self.window invalidateCursorRectsForView:self];
}

- (NSString*)displayedPatternId
{
    return nsString(geometryPatternId(self.trackerState));
}

- (NSUInteger)displayedLaneCount
{
    return static_cast<NSUInteger>(
        geometryLanes(geometryPattern(self.trackerState)).count);
}

- (NSUInteger)displayedMutedLaneCount
{
    const auto* pattern = geometryPattern(self.trackerState);
    const auto lanes = geometryLanes(pattern);
    std::size_t muted = 0u;
    for (std::size_t ordinal = 0u; ordinal < lanes.count; ++ordinal)
        muted += geometryLaneMuted(self.trackerState, pattern,
            lanes.indices[ordinal]) ? 1u : 0u;
    return static_cast<NSUInteger>(muted);
}

- (NSInteger)directionPopupIndex:(Direction)direction
{
    switch (direction) {
    case Direction::Reverse: return 1;
    case Direction::Palindrome: return 2;
    case Direction::Random: return 3;
    case Direction::Forward:
    default: return 0;
    }
}

- (void)syncToolboxControls
{
    auto* model = self.trackerState;
    const auto* pattern = geometryPattern(model);
    const auto visible = visibleGeometryLanes(model);
    BOOL rebuildLanes = self.lanePopup.numberOfItems
        != static_cast<NSInteger>(visible.count);
    for (std::size_t ordinal = 0u; !rebuildLanes
         && ordinal < visible.count; ++ordinal) {
        const auto lane = visible.indices[ordinal];
        NSString* expected = [NSString stringWithFormat:@"%02lu  %@",
            static_cast<unsigned long>(lane + 1u),
            nsString(pattern->tracks[lane].name)];
        rebuildLanes = ![[self.lanePopup itemAtIndex:
            static_cast<NSInteger>(ordinal)].title isEqualToString:expected];
    }
    if (rebuildLanes) {
        [self.lanePopup removeAllItems];
        for (std::size_t ordinal = 0u; ordinal < visible.count; ++ordinal) {
            const auto lane = visible.indices[ordinal];
            [self.lanePopup addItemWithTitle:[NSString stringWithFormat:
                @"%02lu  %@", static_cast<unsigned long>(lane + 1u),
                nsString(pattern->tracks[lane].name)]];
            self.lanePopup.lastItem.tag = static_cast<NSInteger>(lane);
        }
    }
    std::size_t selectedOrdinal = 0u;
    std::size_t selectedLane = visible.count > 0u ? visible.indices[0u] : 0u;
    for (std::size_t ordinal = 0u; ordinal < visible.count; ++ordinal) {
        if (visible.indices[ordinal] == model->session.selectedTrack) {
            selectedOrdinal = ordinal;
            selectedLane = visible.indices[ordinal];
            break;
        }
    }
    if (visible.count > 0u)
        [self.lanePopup selectItemAtIndex:
            static_cast<NSInteger>(selectedOrdinal)];
    if (pattern && selectedLane < pattern->tracks.size())
        [self.directionPopup selectItemAtIndex:[self directionPopupIndex:
            pattern->tracks[selectedLane].noteColumn.direction]];
    const BOOL editable = [self canEditDisplayedPattern];
    self.lanePopup.enabled = editable && visible.count > 0u;
    self.directionPopup.enabled = editable && visible.count > 0u;
    self.morphTargetPopup.enabled = editable && visible.count > 1u;
    [self syncBurstNameControls];
}

- (void)syncBurstNameControls
{
    const BOOL visible = self.geometryViewMode
            == S3GTrackerGeometryViewModeBurst
        && _openGeometryMenu == S3GTrackerGeometryMenuNone;
    self.burstNameField.hidden = !visible;
    self.burstSaveButton.hidden = !visible;
    if (!visible || !self.trackerState) return;
    const auto& burst = self.trackerState->session.burstLibrary.bursts[
        _selectedBurstSlot];
    const BOOL editable = [self canEditDisplayedPattern] && !burst.empty();
    self.burstNameField.enabled = editable;
    self.burstSaveButton.enabled = editable;
    id firstResponder = self.window.firstResponder;
    const BOOL editing = firstResponder == self.burstNameField
        || firstResponder == self.burstNameField.currentEditor;
    if (!editing)
        self.burstNameField.stringValue = burst.empty()
            ? @"" : nsString(burst.name);
}

- (NSArray<NSString*>*)itemsForGeometryMenu:(S3GTrackerGeometryMenu)menu
{
    if (menu == S3GTrackerGeometryMenuView) {
        if (self.burstLibraryOnly) return @[];
        return @[ @"RING FIELD", @"ACTIVE PULSES", @"ALL STEPS UNDERLAY",
            @"PHASE SPOKES", @"LANE FOCUS", @"COMPOSITE RING",
            @"PITCH MAP" ];
    }
    if (menu == S3GTrackerGeometryMenuPitchScope)
        return @[ @"SELECTED ROWS", @"FULL NOTE CYCLE" ];
    if (menu == S3GTrackerGeometryMenuPitchRoot)
        return @[ @"C", @"C#", @"D", @"D#", @"E", @"F",
            @"F#", @"G", @"G#", @"A", @"A#", @"B" ];
    if (menu == S3GTrackerGeometryMenuPitchScale) {
        NSMutableArray<NSString*>* titles = [[NSMutableArray alloc] init];
        for (uint32_t menuIndex = 0u;
             menuIndex < s3g::kMusicalScaleCount; ++menuIndex) {
            const auto scale = s3g::musicalScaleValueForMenuIndex(menuIndex);
            [titles addObject:nsString(
                s3g::musicalScaleDefinition(scale).name)];
        }
        return titles;
    }
    if (menu == S3GTrackerGeometryMenuPitchContour)
        return @[ @"FIT", @"RISE", @"FALL", @"PENDULUM",
            @"RANDOM WALK", @"VARY EXISTING", @"MANUAL" ];
    if (menu == S3GTrackerGeometryMenuPitchLeap)
        return @[ @"1 DEGREE", @"2 DEGREES", @"3 DEGREES",
            @"4 DEGREES", @"5 DEGREES", @"6 DEGREES",
            @"7 DEGREES", @"8 DEGREES" ];
    if (menu == S3GTrackerGeometryMenuBurstSlot) {
        NSMutableArray<NSString*>* titles = [[NSMutableArray alloc] init];
        const auto* model = self.trackerState;
        for (std::size_t slot = 0u; slot < kBurstDefinitionCount; ++slot) {
            const auto& burst = model->session.burstLibrary.bursts[slot];
            [titles addObject:[NSString stringWithFormat:@"%@  ·  %@",
                nsString(burstSlotToken(slot)), burst.empty()
                    ? @"EMPTY" : nsString(burst.name)]];
        }
        return titles;
    }
    if (menu == S3GTrackerGeometryMenuBurstEvent) {
        NSMutableArray<NSString*>* titles = [[NSMutableArray alloc] init];
        const auto& burst = self.trackerState->session.burstLibrary.bursts[
            _selectedBurstSlot];
        for (std::size_t event = 0u; event < burst.eventCount; ++event)
            [titles addObject:[NSString stringWithFormat:
                @"STEP %lu  ·  %@ · %03u", static_cast<unsigned long>(event + 1u),
                midiNoteName(burst.events[event].note),
                static_cast<unsigned int>(burst.events[event].note)]];
        return titles;
    }
    NSPopUpButton* popup = menu == S3GTrackerGeometryMenuLane
        ? self.lanePopup : menu == S3GTrackerGeometryMenuDirection
        ? self.directionPopup : menu == S3GTrackerGeometryMenuMorphTarget
        ? self.morphTargetPopup : self.viewModePopup;
    NSMutableArray<NSString*>* titles = [[NSMutableArray alloc] init];
    for (NSMenuItem* item in popup.itemArray) {
        NSString* title = item.title;
        [titles addObject:title ? title : @""];
    }
    return titles;
}

- (NSInteger)selectedIndexForGeometryMenu:(S3GTrackerGeometryMenu)menu
{
    if (menu == S3GTrackerGeometryMenuPitchScope)
        return _pitchUseFullCycle ? 1 : 0;
    if (menu == S3GTrackerGeometryMenuPitchRoot)
        return static_cast<NSInteger>(_pitchSettings.rootPitchClass % 12u);
    if (menu == S3GTrackerGeometryMenuPitchScale)
        return static_cast<NSInteger>(s3g::musicalScaleMenuIndexForValue(
            _pitchSettings.scale));
    if (menu == S3GTrackerGeometryMenuPitchContour)
        return static_cast<NSInteger>(_pitchSettings.contour);
    if (menu == S3GTrackerGeometryMenuPitchLeap)
        return static_cast<NSInteger>(std::clamp<uint8_t>(
            _pitchSettings.maximumLeapDegrees, 1u, 8u) - 1u);
    if (menu == S3GTrackerGeometryMenuBurstSlot)
        return static_cast<NSInteger>(_selectedBurstSlot);
    if (menu == S3GTrackerGeometryMenuBurstEvent)
        return static_cast<NSInteger>(_selectedBurstEvent);
    if (menu == S3GTrackerGeometryMenuLane)
        return self.lanePopup.indexOfSelectedItem;
    if (menu == S3GTrackerGeometryMenuDirection)
        return self.directionPopup.indexOfSelectedItem;
    if (menu == S3GTrackerGeometryMenuMorphTarget)
        return self.morphTargetPopup.indexOfSelectedItem;
    if (menu == S3GTrackerGeometryMenuView) {
        const NSInteger selected = self.viewModePopup.indexOfSelectedItem;
        return selected == S3GTrackerGeometryViewModePitchMap
            ? selected - 1 : selected;
    }
    return 0;
}

- (NSRect)sourceRectForGeometryMenu:(S3GTrackerGeometryMenu)menu
{
    if (menu == S3GTrackerGeometryMenuPitchScope)
        return [self pitchScopeMenuBoxRect];
    if (menu == S3GTrackerGeometryMenuPitchRoot)
        return [self pitchRootMenuBoxRect];
    if (menu == S3GTrackerGeometryMenuPitchScale)
        return [self pitchScaleMenuBoxRect];
    if (menu == S3GTrackerGeometryMenuPitchContour)
        return [self pitchContourMenuBoxRect];
    if (menu == S3GTrackerGeometryMenuPitchLeap)
        return [self pitchLeapMenuBoxRect];
    if (menu == S3GTrackerGeometryMenuBurstSlot)
        return [self burstSlotMenuBoxRect];
    if (menu == S3GTrackerGeometryMenuBurstEvent)
        return [self burstEventMenuBoxRect];
    if (menu == S3GTrackerGeometryMenuLane) return [self laneMenuBoxRect];
    if (menu == S3GTrackerGeometryMenuDirection)
        return [self directionMenuBoxRect];
    if (menu == S3GTrackerGeometryMenuMorphTarget)
        return [self morphTargetMenuBoxRect];
    return [self viewMenuBoxRect];
}

- (NSRect)dropdownRectForGeometryMenu:(S3GTrackerGeometryMenu)menu
{
    constexpr CGFloat itemHeight = 21.0;
    const NSRect source = [self sourceRectForGeometryMenu:menu];
    const NSUInteger itemCount = [self itemsForGeometryMenu:menu].count;
    const uint32_t columns = menu == S3GTrackerGeometryMenuPitchScale
        ? 4u : menu == S3GTrackerGeometryMenuBurstSlot ? 2u : 1u;
    const CGFloat height = itemHeight * static_cast<CGFloat>(
        (itemCount + columns - 1u) / columns);
    const CGFloat width = columns > 1u
        ? std::min<CGFloat>(menu == S3GTrackerGeometryMenuPitchScale
                ? 760.0 : 600.0,
            NSWidth(self.bounds) - 16.0)
        : NSWidth(source);
    CGFloat y = NSMaxY(source) + 2.0;
    if (y + height > NSHeight(self.bounds) - 8.0)
        y = NSMinY(source) - height - 2.0;
    y = std::clamp<CGFloat>(y, 8.0,
        std::max<CGFloat>(8.0, NSHeight(self.bounds) - height - 8.0));
    CGFloat x = columns > 1u
        ? std::max<CGFloat>(8.0, NSMaxX(source) - width)
        : NSMinX(source);
    return NSMakeRect(x, y, width, height);
}

- (void)openGeometryMenu:(S3GTrackerGeometryMenu)menu
{
    if (menu == S3GTrackerGeometryMenuView && self.burstLibraryOnly) return;
    [self syncToolboxControls];
    _openGeometryMenu = _openGeometryMenu == menu
        ? S3GTrackerGeometryMenuNone : menu;
    _geometryMenuHoverIndex = -1;
    [self syncBurstNameControls];
    [self setNeedsDisplay:YES];
}

- (void)applyGeometryMenuSelection:(NSInteger)index
{
    NSArray<NSString*>* items = [self itemsForGeometryMenu:
        _openGeometryMenu];
    if (index < 0 || index >= static_cast<NSInteger>(items.count)) return;
    if (_openGeometryMenu == S3GTrackerGeometryMenuLane) {
        [self.lanePopup selectItemAtIndex:index];
        [self lanePopupChanged:self.lanePopup];
    } else if (_openGeometryMenu == S3GTrackerGeometryMenuDirection) {
        [self.directionPopup selectItemAtIndex:index];
        [self directionPopupChanged:self.directionPopup];
    } else if (_openGeometryMenu
            == S3GTrackerGeometryMenuMorphTarget) {
        [self.morphTargetPopup selectItemAtIndex:index];
    } else if (_openGeometryMenu == S3GTrackerGeometryMenuView) {
        const NSInteger mode = index >= S3GTrackerGeometryViewModeBurst
            ? index + 1 : index;
        [self.viewModePopup selectItemAtIndex:mode];
        [self viewModeChanged:self.viewModePopup];
    } else if (_openGeometryMenu == S3GTrackerGeometryMenuBurstSlot) {
        _selectedBurstSlot = static_cast<std::size_t>(index);
        _selectedBurstEvent = 0u;
        [self syncBurstNameControls];
    } else if (_openGeometryMenu == S3GTrackerGeometryMenuBurstEvent) {
        _selectedBurstEvent = static_cast<std::size_t>(index);
    } else if (_openGeometryMenu == S3GTrackerGeometryMenuPitchScope) {
        _pitchUseFullCycle = index == 1;
        _pitchOverrides.fill(-1);
    } else if (_openGeometryMenu == S3GTrackerGeometryMenuPitchRoot) {
        _pitchSettings.rootPitchClass = static_cast<uint8_t>(index);
        _pitchOverrides.fill(-1);
    } else if (_openGeometryMenu == S3GTrackerGeometryMenuPitchScale) {
        _pitchSettings.scale = s3g::musicalScaleValueForMenuIndex(
            static_cast<uint32_t>(index));
        _pitchOverrides.fill(-1);
    } else if (_openGeometryMenu == S3GTrackerGeometryMenuPitchContour) {
        _pitchSettings.contour = static_cast<PitchContour>(index);
        _pitchOverrides.fill(-1);
    } else if (_openGeometryMenu == S3GTrackerGeometryMenuPitchLeap) {
        _pitchSettings.maximumLeapDegrees = static_cast<uint8_t>(index + 1);
        _pitchOverrides.fill(-1);
    }
    _openGeometryMenu = S3GTrackerGeometryMenuNone;
    _geometryMenuHoverIndex = -1;
    [self syncBurstNameControls];
    [self setNeedsDisplay:YES];
}

- (void)selectBurstSlot:(std::size_t)slot
{
    _selectedBurstSlot = std::min<std::size_t>(slot,
        kBurstDefinitionCount - 1u);
    _selectedBurstEvent = 0u;
    self.geometryViewMode = S3GTrackerGeometryViewModeBurst;
    [self.viewModePopup selectItemAtIndex:
        S3GTrackerGeometryViewModeBurst];
    [self syncBurstNameControls];
    [self setNeedsDisplay:YES];
}

- (void)pitchMapRowsFirst:(std::size_t*)first last:(std::size_t*)last
{
    auto* model = self.trackerState;
    if (!model || model->session.pattern.tracks.empty()) {
        if (first) *first = 0u;
        if (last) *last = 0u;
        return;
    }
    const auto lane = std::min(model->session.selectedTrack,
        model->session.pattern.tracks.size() - 1u);
    const auto length = std::clamp<std::size_t>(
        model->session.pattern.tracks[lane].noteColumn.length, 1u, 256u);
    const std::size_t resolvedFirst = _pitchUseFullCycle ? 0u
        : std::min(_pitchFirstRow, length - 1u);
    const std::size_t resolvedLast = _pitchUseFullCycle ? length - 1u
        : std::clamp(_pitchLastRow, resolvedFirst, length - 1u);
    if (first) *first = resolvedFirst;
    if (last) *last = resolvedLast;
}

- (void)refreshPitchMapPreview
{
    auto* model = self.trackerState;
    if (!model || model->session.pattern.tracks.empty()) {
        _pitchPreview = {};
        _pitchAnalysis = {};
        return;
    }
    const auto lane = std::min(model->session.selectedTrack,
        model->session.pattern.tracks.size() - 1u);
    std::size_t first = 0u;
    std::size_t last = 0u;
    [self pitchMapRowsFirst:&first last:&last];
    _pitchAnalysis = s3g::tracker::analyzePitchMap(
        model->session.pattern, lane, first, last);
    _pitchPreview = s3g::tracker::previewPitchMap(
        model->session.pattern, lane, first, last, _pitchSettings);
    _pitchPreview.changed = 0u;
    for (auto& assignment : _pitchPreview.assignments) {
        if (assignment.row < _pitchOverrides.size()
            && _pitchOverrides[assignment.row] >= 0)
            assignment.note = static_cast<uint8_t>(
                _pitchOverrides[assignment.row]);
        s3g::tracker::retargetPitchMapVoicing(assignment, _pitchSettings);
        bool changed = false;
        for (std::size_t voice = 0u; voice < assignment.voiceCount; ++voice)
            changed |= assignment.notes[voice]
                != assignment.originalNotes[voice];
        if (changed) ++_pitchPreview.changed;
    }
}

- (void)freezePitchPreviewForManualEditing
{
    [self refreshPitchMapPreview];
    const BOOL generated = _pitchSettings.contour != PitchContour::Manual
        || _pitchSettings.transposeSemitones != 0
        || _pitchSettings.invertScaleDegrees
        || _pitchSettings.reversePitchOrder;
    if (!generated) return;
    const auto frozen = _pitchPreview.assignments;
    _pitchSettings.contour = PitchContour::Manual;
    _pitchSettings.transposeSemitones = 0;
    _pitchSettings.invertScaleDegrees = false;
    _pitchSettings.reversePitchOrder = false;
    _pitchOverrides.fill(-1);
    for (const auto& assignment : frozen) {
        if (assignment.row < _pitchOverrides.size())
            _pitchOverrides[assignment.row] = assignment.note;
    }
    [self refreshPitchMapPreview];
    _pitchStatus = @"GENERATED CONTOUR FROZEN FOR MANUAL EDIT";
}

- (void)analyzePitchMap
{
    [self refreshPitchMapPreview];
    if (_pitchAnalysis.noteCount == 0u) {
        _pitchStatus = @"NO EXPLICIT NOTE PITCHES";
        NSBeep();
        [self setNeedsDisplay:YES];
        return;
    }
    _pitchSettings.rootPitchClass = _pitchAnalysis.rootPitchClass;
    _pitchSettings.scale = _pitchAnalysis.scale;
    _pitchSettings.minimumNote = static_cast<uint8_t>(
        _pitchAnalysis.minimumNote > 12u
            ? _pitchAnalysis.minimumNote - 12u : 0u);
    _pitchSettings.maximumNote = static_cast<uint8_t>(std::min<uint32_t>(
        127u, static_cast<uint32_t>(_pitchAnalysis.maximumNote) + 12u));
    _pitchOverrides.fill(-1);
    const NSInteger percent = static_cast<NSInteger>(std::lround(
        _pitchAnalysis.confidence * 100.0f));
    _pitchStatus = [NSString stringWithFormat:@"ANALYZED · %ld%% CONFIDENCE",
        static_cast<long>(percent)];
    [self refreshPitchMapPreview];
    [self setNeedsDisplay:YES];
}

- (void)openPitchMapFirstRow:(std::size_t)firstRow
    lastRow:(std::size_t)lastRow
{
    auto* model = self.trackerState;
    if (!model || model->session.pattern.tracks.empty()) return;
    const auto lane = std::min(model->session.selectedTrack,
        model->session.pattern.tracks.size() - 1u);
    const auto length = std::clamp<std::size_t>(
        model->session.pattern.tracks[lane].noteColumn.length, 1u, 256u);
    _pitchFirstRow = std::min(firstRow, length - 1u);
    _pitchLastRow = std::clamp(lastRow, _pitchFirstRow, length - 1u);
    _pitchUseFullCycle = _pitchFirstRow == 0u
        && _pitchLastRow + 1u == length;
    _pitchOverrides.fill(-1);
    self.geometryViewMode = S3GTrackerGeometryViewModePitchMap;
    [self.viewModePopup selectItemAtIndex:
        S3GTrackerGeometryViewModePitchMap];
    [self analyzePitchMap];
    [self syncBurstNameControls];
    [self setNeedsDisplay:YES];
    [self.playbackOverlay setNeedsDisplay:YES];
}

- (void)applyCurrentPitchMap
{
    if (![self canEditDisplayedPattern] || !self.trackerState) return;
    [self refreshPitchMapPreview];
    auto& pattern = self.trackerState->session.pattern;
    if (pattern.tracks.empty()) return;
    const auto lane = std::min(self.trackerState->session.selectedTrack,
        pattern.tracks.size() - 1u);
    auto& notes = pattern.tracks[lane].notes;
    std::size_t changed = 0u;
    for (const auto& assignment : _pitchPreview.assignments) {
        if (assignment.row >= notes.size()
            || notes[assignment.row].state != NoteCellState::Note) continue;
        const auto& cell = notes[assignment.row];
        bool rowChanged = cell.noteVoiceCount() != assignment.voiceCount;
        for (std::size_t voice = 0u;
             voice < assignment.voiceCount && !rowChanged; ++voice)
            rowChanged = cell.noteVoice(voice) != assignment.notes[voice];
        if (!rowChanged) continue;
        notes[assignment.row] = NoteCell::withNotes(assignment.notes,
            assignment.voiceCount);
        ++changed;
    }
    _pitchSettings.contour = PitchContour::Manual;
    _pitchSettings.transposeSemitones = 0;
    _pitchSettings.invertScaleDegrees = false;
    _pitchSettings.reversePitchOrder = false;
    _pitchOverrides.fill(-1);
    if (changed > 0u) {
        _pitchStatus = [NSString stringWithFormat:@"APPLIED · %lu NOTES",
            static_cast<unsigned long>(changed)];
        [self.owner modulePatternChanged];
    } else {
        _pitchStatus = @"NO PITCH CHANGES";
        [self setNeedsDisplay:YES];
    }
    [self refreshPitchMapPreview];
}

- (void)applyPitchMapContour:(PitchContour)contour
    firstRow:(std::size_t)firstRow lastRow:(std::size_t)lastRow
{
    if (![self canEditDisplayedPattern] || !self.trackerState
        || self.trackerState->session.pattern.tracks.empty()) return;
    const auto lane = std::min(self.trackerState->session.selectedTrack,
        self.trackerState->session.pattern.tracks.size() - 1u);
    PitchMapSettings settings = _pitchSettings;
    settings.contour = contour;
    const auto evidence = s3g::tracker::analyzePitchMap(
        self.trackerState->session.pattern, lane, firstRow, lastRow);
    if (contour == PitchContour::Fit) {
        settings.minimumNote = 0u;
        settings.maximumNote = 127u;
    } else if (evidence.noteCount > 0u) {
        settings.minimumNote = static_cast<uint8_t>(
            evidence.minimumNote > 12u ? evidence.minimumNote - 12u : 0u);
        settings.maximumNote = static_cast<uint8_t>(std::min<uint32_t>(
            127u, static_cast<uint32_t>(evidence.maximumNote) + 12u));
    }
    const auto changed = s3g::tracker::applyPitchMap(
        self.trackerState->session.pattern, lane, firstRow, lastRow,
        settings);
    if (changed > 0u) [self.owner modulePatternChanged];
}

- (void)initializeBurstAtSlot:(std::size_t)slot
{
    if (!self.trackerState || slot >= kBurstDefinitionCount) return;
    auto& burst = self.trackerState->session.burstLibrary.bursts[slot];
    burst = {};
    burst.name = "BURST " + burstSlotToken(slot);
    burst.eventCount = 4u;
    uint8_t note = 60u;
    if (!self.trackerState->session.pattern.tracks.empty())
        note = laneDefaultNote(self.trackerState->session,
            std::min(self.trackerState->session.selectedTrack,
                self.trackerState->session.pattern.tracks.size() - 1u));
    for (std::size_t index = 0u; index < burst.eventCount; ++index)
        burst.events[index] = {
            static_cast<uint16_t>(index * 65536u / burst.eventCount),
            note, 127u, 70u,
        };
}

- (std::size_t)firstEmptyBurstSlot
{
    const auto& bursts = self.trackerState->session.burstLibrary.bursts;
    const auto found = std::find_if(bursts.begin(), bursts.end(),
        [](const BurstDefinition& burst) { return burst.empty(); });
    return found == bursts.end() ? bursts.size()
                                 : static_cast<std::size_t>(found - bursts.begin());
}

- (void)saveBurstName:(id)sender
{
    (void)sender;
    if (!self.trackerState || ![self canEditDisplayedPattern]) return;
    auto& burst = self.trackerState->session.burstLibrary.bursts[_selectedBurstSlot];
    if (burst.empty()) return;
    NSString* value = [self.burstNameField.stringValue
        stringByTrimmingCharactersInSet:
            NSCharacterSet.whitespaceAndNewlineCharacterSet];
    if (value.length == 0u)
        value = [NSString stringWithFormat:@"BURST %@",
            nsString(burstSlotToken(_selectedBurstSlot))];
    NSData* utf8 = [value dataUsingEncoding:NSUTF8StringEncoding];
    if (!value.UTF8String || utf8.length > kMaximumBurstNameBytes) {
        NSBeep();
        [self syncBurstNameControls];
        return;
    }
    const std::string next(value.UTF8String);
    if (next != burst.name) {
        burst.name = next;
        [self.owner modulePatternChanged];
    }
    self.burstNameField.stringValue = value;
    [self setNeedsDisplay:YES];
}

- (void)controlTextDidBeginEditing:(NSNotification*)notification
{
    if (notification.object == self.burstNameField)
        S3GTrackerStyleTextEditor(self.burstNameField);
}

- (void)clearBurstPlaceFeedback
{
    _burstPlaceFeedbackActive = NO;
    [self setNeedsDisplay:YES];
}

- (void)pulseBurstPlaceFeedback
{
    [NSObject cancelPreviousPerformRequestsWithTarget:self
        selector:@selector(clearBurstPlaceFeedback) object:nil];
    _burstPlaceFeedbackActive = YES;
    [self setNeedsDisplay:YES];
    [self performSelector:@selector(clearBurstPlaceFeedback)
        withObject:nil afterDelay:0.18];
}

- (void)clearBurstPreviewFeedback
{
    _burstPreviewFeedbackActive = NO;
    [self setNeedsDisplay:YES];
}

- (void)pulseBurstPreviewFeedback
{
    [NSObject cancelPreviousPerformRequestsWithTarget:self
        selector:@selector(clearBurstPreviewFeedback) object:nil];
    _burstPreviewFeedbackActive = YES;
    [self setNeedsDisplay:YES];
    [self performSelector:@selector(clearBurstPreviewFeedback)
        withObject:nil afterDelay:0.18];
}

- (void)clearPitchPreviewFeedback
{
    _pitchPreviewFeedbackActive = NO;
    [self setNeedsDisplay:YES];
}

- (void)pulsePitchPreviewFeedback
{
    [NSObject cancelPreviousPerformRequestsWithTarget:self
        selector:@selector(clearPitchPreviewFeedback) object:nil];
    _pitchPreviewFeedbackActive = YES;
    [self setNeedsDisplay:YES];
    [self performSelector:@selector(clearPitchPreviewFeedback)
        withObject:nil afterDelay:0.18];
}

- (BOOL)handleBurstToolboxClickAtPoint:(NSPoint)point
{
    if (NSPointInRect(point, [self burstSlotMenuBoxRect])) {
        [self openGeometryMenu:S3GTrackerGeometryMenuBurstSlot];
        return YES;
    }
    if (NSPointInRect(point, [self burstEventMenuBoxRect])) {
        const auto& burst = self.trackerState->session.burstLibrary.bursts[
            _selectedBurstSlot];
        if (!burst.empty())
            [self openGeometryMenu:S3GTrackerGeometryMenuBurstEvent];
        return YES;
    }
    auto& pattern = self.trackerState->session.pattern;
    auto& burst = self.trackerState->session.burstLibrary.bursts[
        _selectedBurstSlot];
    BOOL changed = NO;
    if (NSPointInRect(point, [self burstPreviewHeaderButtonRect])) {
        if (burst.empty() || self.trackerState->playing) return YES;
        const auto lane = pattern.tracks.empty() ? 0u
            : std::min(self.trackerState->session.selectedTrack,
                pattern.tracks.size() - 1u);
        const uint8_t channel = pattern.tracks.empty() ? 1u
            : pattern.tracks[lane].midiChannel;
        if (self.owner.trackerCallbacks
            && self.owner.trackerCallbacks->previewBurst) {
            const double projectBpm = self.trackerState->hostBpm > 0.0
                ? self.trackerState->hostBpm
                : self.trackerState->session.transport.bpm;
            self.owner.trackerCallbacks->previewBurst(burst, channel,
                projectBpm,
                self.trackerState->session.transport.ticksPerBeat);
            [self pulseBurstPreviewFeedback];
        }
        return YES;
    }
    if (NSPointInRect(point, [self fitBurstGatesHeaderButtonRect])) {
        if (burst.empty() || ![self canEditDisplayedPattern]) return YES;
        const auto previous = burst.events;
        fitBurstGatesToRow(burst);
        changed = !std::equal(previous.begin(),
            previous.begin() + burst.eventCount, burst.events.begin(),
            [](const BurstEvent& left, const BurstEvent& right) {
                return left.gatePercent == right.gatePercent;
            });
        if (changed) [self.owner modulePatternChanged];
        [self setNeedsDisplay:YES];
        return YES;
    }
    for (NSUInteger index = 0u; index < 2u; ++index) {
        if (!NSPointInRect(point,
                [self burstActionRectForRow:2u index:index count:2u]))
            continue;
        if (index == 0u && burst.eventCount > 1u) {
            const auto remove = std::min<std::size_t>(_selectedBurstEvent,
                burst.eventCount - 1u);
            std::move(burst.events.begin()
                    + static_cast<std::ptrdiff_t>(remove + 1u),
                burst.events.begin()
                    + static_cast<std::ptrdiff_t>(burst.eventCount),
                burst.events.begin() + static_cast<std::ptrdiff_t>(remove));
            --burst.eventCount;
            _selectedBurstEvent = std::min<std::size_t>(_selectedBurstEvent,
                burst.eventCount - 1u);
            changed = YES;
        } else if (index == 1u && !burst.empty()
            && burst.eventCount < kMaximumBurstEvents) {
            auto& event = burst.events[burst.eventCount];
            event = burst.events[burst.eventCount - 1u];
            ++burst.eventCount;
            _selectedBurstEvent = burst.eventCount - 1u;
            changed = YES;
        }
        if (changed) {
            setGeometryBurstTiming(burst, "even");
            [self.owner modulePatternChanged];
        }
        [self setNeedsDisplay:YES];
        return YES;
    }
    for (NSUInteger index = 0u; index < 3u; ++index) {
        if (!NSPointInRect(point,
                [self burstActionRectForRow:4u index:index count:3u]))
            continue;
        if (index == 0u) {
            const auto slot = [self firstEmptyBurstSlot];
            if (slot < self.trackerState->session.burstLibrary.bursts.size()) {
                _selectedBurstSlot = slot;
                _selectedBurstEvent = 0u;
                [self initializeBurstAtSlot:slot];
                changed = YES;
            }
        } else if (index == 1u && !burst.empty()) {
            const auto slot = [self firstEmptyBurstSlot];
            if (slot < self.trackerState->session.burstLibrary.bursts.size()) {
                self.trackerState->session.burstLibrary.bursts[slot] = burst;
                self.trackerState->session.burstLibrary.bursts[slot].name += " COPY";
                _selectedBurstSlot = slot;
                _selectedBurstEvent = 0u;
                changed = YES;
            }
        } else if (index == 2u && !burst.empty()
            && projectBurstUsageCount(*self.trackerState,
                _selectedBurstSlot) == 0u) {
            burst = {};
            _selectedBurstEvent = 0u;
            changed = YES;
        }
        if (changed) [self.owner modulePatternChanged];
        [self setNeedsDisplay:YES];
        return YES;
    }
    for (NSUInteger index = 0u; index < 2u; ++index) {
        if (!NSPointInRect(point,
                [self burstActionRectForRow:7u index:index count:2u]))
            continue;
        if (index == 0u && self.owner.trackerCallbacks
            && self.owner.trackerCallbacks->importAssetPack)
            self.owner.trackerCallbacks->importAssetPack();
        else if (index == 1u && !burst.empty()
            && self.owner.trackerCallbacks
            && self.owner.trackerCallbacks->exportBurstAssetPack)
            self.owner.trackerCallbacks->exportBurstAssetPack(
                _selectedBurstSlot);
        return YES;
    }
    if (burst.empty()) return NO;
    for (NSUInteger index = 0u; index < 3u; ++index) {
        if (!NSPointInRect(point,
                [self burstActionRectForRow:5u index:index count:3u]))
            continue;
        const auto shape = index == 0u ? "even"
            : index == 1u ? "accelerate" : "decelerate";
        setGeometryBurstTiming(burst, shape);
        [self.owner modulePatternChanged];
        [self setNeedsDisplay:YES];
        return YES;
    }
    for (NSUInteger index = 0u; index < 3u; ++index) {
        if (!NSPointInRect(point,
                [self burstActionRectForRow:6u index:index count:3u]))
            continue;
        if (index == 0u) {
            std::reverse(burst.events.begin(),
                burst.events.begin() + burst.eventCount);
        } else {
            const auto count = static_cast<std::size_t>(burst.eventCount);
            const auto amount = index == 1u ? count - 1u : 1u;
            std::rotate(burst.events.begin(),
                burst.events.begin() + static_cast<std::ptrdiff_t>(amount),
                burst.events.begin() + static_cast<std::ptrdiff_t>(count));
        }
        setGeometryBurstTiming(burst, "even");
        [self.owner modulePatternChanged];
        [self setNeedsDisplay:YES];
        return YES;
    }
    if (NSPointInRect(point, [self revealHeaderButtonRect])) {
        if (self.trackerState->session.pattern.tracks.empty()) return YES;
        auto& session = self.trackerState->session;
        const auto lane = std::min(session.selectedTrack,
            session.pattern.tracks.size() - 1u);
        auto& track = session.pattern.tracks[lane];
        const auto row = session.selectedRow;
        if (track.notes.size() <= row)
            track.notes.resize(row + 1u, NoteCell::rest());
        track.notes[row] = NoteCell::withBurst(
            static_cast<uint8_t>(_selectedBurstSlot));
        track.noteColumn.length = std::max(track.noteColumn.length, row + 1u);
        [self.owner modulePatternChanged];
        [self pulseBurstPlaceFeedback];
        return YES;
    }
    return NO;
}

- (BOOL)handleToolboxClickAtPoint:(NSPoint)point
{
    if (_openGeometryMenu != S3GTrackerGeometryMenuNone) {
        const auto menu = _openGeometryMenu;
        const NSArray<NSString*>* items = [self itemsForGeometryMenu:menu];
        const uint32_t columns = menu == S3GTrackerGeometryMenuPitchScale
            ? 4u : menu == S3GTrackerGeometryMenuBurstSlot ? 2u : 1u;
        const NSInteger hit = columns > 1u
            ? s3g::clap_gui::multiColumnDropdownHitIndex(point,
                [self dropdownRectForGeometryMenu:menu], 21.0,
                static_cast<uint32_t>(items.count), columns)
            : s3g::clap_gui::dropdownHitIndex(point,
                [self dropdownRectForGeometryMenu:menu], 21.0,
                static_cast<uint32_t>(items.count));
        if (hit >= 0) {
            [self applyGeometryMenuSelection:hit];
            _openGeometryMenu = S3GTrackerGeometryMenuNone;
            _geometryMenuHoverIndex = -1;
            [self setNeedsDisplay:YES];
            return YES;
        }
        _openGeometryMenu = S3GTrackerGeometryMenuNone;
        _geometryMenuHoverIndex = -1;
        [self syncBurstNameControls];
        [self setNeedsDisplay:YES];
    }
    if (!self.burstLibraryOnly
        && NSPointInRect(point, [self viewMenuBoxRect])) {
        [self openGeometryMenu:S3GTrackerGeometryMenuView];
        return YES;
    }
    if (self.geometryViewMode == S3GTrackerGeometryViewModeBurst)
        return [self handleBurstToolboxClickAtPoint:point];
    if (self.geometryViewMode == S3GTrackerGeometryViewModePitchMap) {
        if (NSPointInRect(point, [self laneMenuBoxRect])) {
            [self openGeometryMenu:S3GTrackerGeometryMenuLane];
            return YES;
        }
        if (NSPointInRect(point, [self pitchScopeMenuBoxRect])) {
            [self openGeometryMenu:S3GTrackerGeometryMenuPitchScope];
            return YES;
        }
        if (NSPointInRect(point, [self pitchRootMenuBoxRect])) {
            [self openGeometryMenu:S3GTrackerGeometryMenuPitchRoot];
            return YES;
        }
        if (NSPointInRect(point, [self pitchScaleMenuBoxRect])) {
            [self openGeometryMenu:S3GTrackerGeometryMenuPitchScale];
            return YES;
        }
        if (NSPointInRect(point, [self pitchContourMenuBoxRect])) {
            [self openGeometryMenu:S3GTrackerGeometryMenuPitchContour];
            return YES;
        }
        if (NSPointInRect(point, [self pitchLeapMenuBoxRect])) {
            [self openGeometryMenu:S3GTrackerGeometryMenuPitchLeap];
            return YES;
        }
        if (NSPointInRect(point, [self pitchAnchorToggleRect])) {
            _pitchSettings.preserveEndpoints =
                !_pitchSettings.preserveEndpoints;
            _pitchOverrides.fill(-1);
            [self setNeedsDisplay:YES];
            return YES;
        }
        if (NSPointInRect(point, [self pitchInvertToggleRect])) {
            _pitchSettings.invertScaleDegrees =
                !_pitchSettings.invertScaleDegrees;
            _pitchOverrides.fill(-1);
            _pitchStatus = @"PREVIEW UPDATED · SCALE INVERSION";
            [self setNeedsDisplay:YES];
            return YES;
        }
        if (NSPointInRect(point, [self pitchReverseToggleRect])) {
            _pitchSettings.reversePitchOrder =
                !_pitchSettings.reversePitchOrder;
            _pitchOverrides.fill(-1);
            _pitchStatus = @"PREVIEW UPDATED · PITCH ORDER";
            [self setNeedsDisplay:YES];
            return YES;
        }
        if (NSPointInRect(point, [self pitchAnalyzeHeaderButtonRect])) {
            [self analyzePitchMap];
            return YES;
        }
        if (NSPointInRect(point, [self pitchPreviewHeaderButtonRect])) {
            [self refreshPitchMapPreview];
            if (_pitchPreview.assignments.empty()
                || self.trackerState->playing) return YES;
            const auto& track = self.trackerState->session.pattern.tracks[
                std::min(self.trackerState->session.selectedTrack,
                    self.trackerState->session.pattern.tracks.size() - 1u)];
            std::vector<PitchPreviewEvent> events;
            events.reserve(_pitchPreview.assignments.size()
                * s3g::tracker::kMaximumNoteVoices);
            const auto firstHit = _pitchPreview.assignments.front().row;
            for (const auto& assignment : _pitchPreview.assignments) {
                const auto rowVelocity = resolvedVelocity(
                    track, assignment.row);
                const ValueCell* authoredVelocity =
                    assignment.row < track.velocities.size()
                        && track.velocities[assignment.row].state
                            == ValueCellState::Value
                    ? &track.velocities[assignment.row] : nullptr;
                for (std::size_t voice = 0u;
                     voice < assignment.voiceCount; ++voice) {
                    PitchPreviewEvent event;
                    event.row = static_cast<uint16_t>(std::min<std::size_t>(
                        255u, assignment.row - firstHit));
                    event.note = assignment.notes[voice];
                    const float velocity = authoredVelocity
                        ? authoredVelocity->valueVoice(std::min<std::size_t>(
                            voice, authoredVelocity->valueVoiceCount() - 1u))
                        : rowVelocity;
                    event.velocity = static_cast<uint8_t>(std::clamp<int>(
                        static_cast<int>(std::lround(velocity * 127.0f)),
                        1, 127));
                    event.gatePercent = 70u;
                    events.push_back(event);
                }
            }
            if (self.owner.trackerCallbacks
                && self.owner.trackerCallbacks->previewPitchSequence) {
                const double projectBpm = self.trackerState->hostBpm > 0.0
                    ? self.trackerState->hostBpm
                    : self.trackerState->session.transport.bpm;
                self.owner.trackerCallbacks->previewPitchSequence(events,
                    track.midiChannel, projectBpm,
                    self.trackerState->session.transport.ticksPerBeat);
                _pitchStatus = [NSString stringWithFormat:
                    @"PREVIEW · %lu VOICES @ %.1f BPM",
                    static_cast<unsigned long>(events.size()), projectBpm];
                [self pulsePitchPreviewFeedback];
            }
            return YES;
        }
        if (NSPointInRect(point, [self pitchNewSeedHeaderButtonRect])) {
            _pitchSettings.seed = arc4random();
            _pitchOverrides.fill(-1);
            _pitchStatus = @"NEW DETERMINISTIC VARIATION";
            [self setNeedsDisplay:YES];
            return YES;
        }
        if (NSPointInRect(point, [self pitchApplyHeaderButtonRect])) {
            [self applyCurrentPitchMap];
            return YES;
        }
        return NO;
    }
    const BOOL editable = [self canEditDisplayedPattern];
    if (editable && NSPointInRect(point, [self laneMenuBoxRect])) {
        [self openGeometryMenu:S3GTrackerGeometryMenuLane];
        return YES;
    }
    if (editable && NSPointInRect(point, [self directionMenuBoxRect])) {
        [self openGeometryMenu:S3GTrackerGeometryMenuDirection];
        return YES;
    }
    if (editable && NSPointInRect(point, [self morphTargetMenuBoxRect])) {
        [self openGeometryMenu:S3GTrackerGeometryMenuMorphTarget];
        return YES;
    }
    if (editable && NSPointInRect(point,
            [self linkVelocityLengthToggleRect])) {
        self.linkVelocityLength = !self.linkVelocityLength;
        [self setNeedsDisplay:YES];
        return YES;
    }
    if (NSPointInRect(point, [self revealHeaderButtonRect])) {
        [self revealInTracker:nil];
        return YES;
    }
    for (NSUInteger index = 0u; index < 4u; ++index) {
        if (!NSPointInRect(point, [self editToolButtonRect:index])) continue;
        if (index == 0u || (editable && self.geometryViewMode
                == S3GTrackerGeometryViewModeRingField))
            [self toolChanged:self.toolButtons[index]];
        return YES;
    }
    if (!editable) return NO;
    if (NSPointInRect(point, [self reverseButtonRect])) {
        [self reverse:nil];
        return YES;
    }
    if (NSPointInRect(point, [self reflectButtonRect])) {
        [self reflect:nil];
        return YES;
    }
    for (NSUInteger index = 0u; index < self.morphButtons.count; ++index) {
        if (!NSPointInRect(point, [self morphAmountButtonRect:index]))
            continue;
        [self morphAmount:self.morphButtons[index]];
        return YES;
    }
    return NO;
}

- (void)mouseMoved:(NSEvent*)event
{
    if (_openGeometryMenu == S3GTrackerGeometryMenuNone) return;
    const NSPoint point = [self convertPoint:event.locationInWindow
        fromView:nil];
    const auto count = static_cast<uint32_t>(
        [self itemsForGeometryMenu:_openGeometryMenu].count);
    const uint32_t columns = _openGeometryMenu
            == S3GTrackerGeometryMenuPitchScale
        ? 4u : _openGeometryMenu == S3GTrackerGeometryMenuBurstSlot
            ? 2u : 1u;
    const NSInteger hover = columns > 1u
        ? s3g::clap_gui::multiColumnDropdownHitIndex(point,
            [self dropdownRectForGeometryMenu:_openGeometryMenu], 21.0,
            count, columns)
        : s3g::clap_gui::dropdownHitIndex(point,
            [self dropdownRectForGeometryMenu:_openGeometryMenu], 21.0,
            count);
    if (hover == _geometryMenuHoverIndex) return;
    _geometryMenuHoverIndex = hover;
    [self setNeedsDisplay:YES];
}

- (void)drawOpenGeometryMenu
{
    if (_openGeometryMenu == S3GTrackerGeometryMenuNone) return;
    NSArray<NSString*>* titles = [self itemsForGeometryMenu:
        _openGeometryMenu];
    std::vector<NSString*> items(titles.count, nil);
    const auto count = static_cast<uint32_t>(titles.count);
    for (uint32_t index = 0u; index < count; ++index)
        items[index] = titles[index];
    const auto style = s3g::clap_gui::softTextStyle();
    const uint32_t columns = _openGeometryMenu
            == S3GTrackerGeometryMenuPitchScale
        ? 4u : _openGeometryMenu == S3GTrackerGeometryMenuBurstSlot
            ? 2u : 1u;
    if (columns > 1u) {
        s3g::clap_gui::drawMultiColumnDropdownMenu(
            [self dropdownRectForGeometryMenu:_openGeometryMenu], 21.0,
            items.data(), count, columns,
            static_cast<int>([self selectedIndexForGeometryMenu:
                _openGeometryMenu]),
            static_cast<int>(_geometryMenuHoverIndex),
            s3g::clap_gui::softValueAttrs(), style);
    } else {
        s3g::clap_gui::drawDropdownMenu(
            [self dropdownRectForGeometryMenu:_openGeometryMenu], 21.0,
            items.data(), count,
            static_cast<int>([self selectedIndexForGeometryMenu:
                _openGeometryMenu]),
            static_cast<int>(_geometryMenuHoverIndex),
            s3g::clap_gui::softValueAttrs(), style);
    }
}

- (void)lanePopupChanged:(S3GTrackerPopupButton*)sender
{
    if (![self canEditDisplayedPattern] || !sender.selectedItem) return;
    [self selectLane:static_cast<std::size_t>(sender.selectedItem.tag)];
    if (self.geometryViewMode == S3GTrackerGeometryViewModePitchMap) {
        _pitchOverrides.fill(-1);
        _pitchStatus = @"LANE CHANGED · ANALYZE OR PREVIEW";
    }
    [self setNeedsDisplay:YES];
    [self.playbackOverlay setNeedsDisplay:YES];
}

- (void)directionPopupChanged:(S3GTrackerPopupButton*)sender
{
    Track* track = [self selectedEditableTrack];
    if (!track) return;
    Direction direction = Direction::Forward;
    switch (sender.indexOfSelectedItem) {
    case 1: direction = Direction::Reverse; break;
    case 2: direction = Direction::Palindrome; break;
    case 3: direction = Direction::Random; break;
    default: break;
    }
    const BOOL changed = track->noteColumn.direction != direction;
    track->noteColumn.direction = direction;
    [self commitGeometryChange:changed];
    [self.owner moduleSelectionChanged];
    [self setNeedsDisplay:YES];
}

- (void)viewModeChanged:(S3GTrackerPopupButton*)sender
{
    NSInteger requested = std::clamp<NSInteger>(
        sender.indexOfSelectedItem, 0, 7);
    if (self.burstLibraryOnly)
        requested = S3GTrackerGeometryViewModeBurst;
    else if (requested == S3GTrackerGeometryViewModeBurst)
        requested = S3GTrackerGeometryViewModeRingField;
    [self.viewModePopup selectItemAtIndex:requested];
    self.geometryViewMode = static_cast<S3GTrackerGeometryViewMode>(requested);
    static NSArray<NSString*>* descriptions = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        descriptions = @[ @"Ring field", @"Active pulses",
            @"All steps underlay", @"Phase spokes", @"Lane focus",
            @"Composite ring", @"Burst editor", @"Pitch map" ];
    });
    self.accessibilityValue = descriptions[
        static_cast<NSUInteger>(self.geometryViewMode)];
    if (self.geometryViewMode != S3GTrackerGeometryViewModeRingField
        && self.geometryTool != S3GTrackerGeometryToolSelect)
        [self toolChanged:self.toolButtons[0u]];
    [self syncBurstNameControls];
    [self setNeedsDisplay:YES];
    [self.playbackOverlay setNeedsDisplay:YES];
}

- (void)toolChanged:(S3GTrackerActionButton*)sender
{
    const auto index = [self.toolButtons indexOfObjectIdenticalTo:sender];
    if (index == NSNotFound) return;
    self.geometryTool = static_cast<S3GTrackerGeometryTool>(index);
    for (NSUInteger item = 0u; item < self.toolButtons.count; ++item)
        self.toolButtons[item].state = item == index
            ? NSControlStateValueOn : NSControlStateValueOff;
    [self setNeedsDisplay:YES];
}

- (void)advancePlaybackAnimation
{
    const NSTimeInterval now = NSProcessInfo.processInfo.systemUptime;
    const NSTimeInterval elapsed = _lastReadHeadAnimationTime > 0.0
        ? std::clamp(now - _lastReadHeadAnimationTime, 0.0, 0.25)
        : 1.0 / 60.0;
    _lastReadHeadAnimationTime = now;
    // Roughly 140 ms to halve: the onset stays crisp while its smaller tail
    // remains readable between edge-triggered GUI updates.
    const CGFloat decay = static_cast<CGFloat>(
        std::exp(-elapsed * std::log(2.0) / 0.14));
    auto* model = self.trackerState;
    for (std::size_t lane = 0u;
         lane < _readHeadHaloStrength.size(); ++lane) {
        _readHeadHaloStrength[lane] *= decay;
        if (_readHeadHaloStrength[lane] < 0.012)
            _readHeadHaloStrength[lane] = 0.0;
        if (model && model->playing && model->noteHits[lane]) {
            _readHeadHaloRows[lane] = model->noteHitRows[lane];
            _readHeadHaloStrength[lane] = 1.0;
        }
    }
}

- (void)refreshPlaybackDisplay
{
    const auto currentPatternId = geometryPatternId(self.trackerState);
    const uint32_t currentSongMuteMask = self.trackerState
            && self.trackerState->songPlaybackActive
        ? self.trackerState->songPlaybackMutedTracks : 0u;
    if (_lastDisplayedPatternId != currentPatternId
        || _lastDisplayedSongMuteMask != currentSongMuteMask) {
        _lastDisplayedPatternId = currentPatternId;
        _lastDisplayedSongMuteMask = currentSongMuteMask;
        [self setNeedsDisplay:YES];
    }
    [self advancePlaybackAnimation];
    [self.playbackOverlay setNeedsDisplay:YES];
}

- (NSInteger)prepareDocumentationPlaybackSnapshot
{
    auto* model = self.trackerState;
    if (!model) return 0;
    model->songPlaybackActive = false;
    model->songPlaybackPatternId.clear();
    model->songPlaybackMutedTracks = 0u;
    NSString* captureMode = NSProcessInfo.processInfo.environment[
        @"S3G_TRACKER_GEOMETRY_CAPTURE_MODE"];
    if (captureMode.length > 0u) {
        const NSInteger mode = std::clamp<NSInteger>(
            captureMode.integerValue, 0, 7);
        [self.viewModePopup selectItemAtIndex:mode];
        [self viewModeChanged:self.viewModePopup];
    }
    const auto visible = visibleGeometryLanes(&model->session.pattern);
    model->playing = true;
    std::fill(model->noteHits.begin(), model->noteHits.end(), false);
    _documentationHitLanes.fill(false);
    _documentationPlaybackSnapshot = YES;
    constexpr std::array<double, 6u> playheadPhases {
        0.0, 0.25, 0.50, 0.75, 0.375, 0.875,
    };
    std::size_t activeHits = 0u;
    for (std::size_t ordinal = 0u; ordinal < visible.count; ++ordinal) {
        const auto lane = visible.indices[ordinal];
        auto& track = model->session.pattern.tracks[lane];
        track.noteColumn.length = std::max<std::size_t>(
            track.noteColumn.length, 3u);
        const auto length = std::clamp<std::size_t>(
            track.noteColumn.length, 1u, 256u);
        track.notes.resize(std::max(track.notes.size(), length),
            NoteCell::rest());
        const auto isHit = [&track](std::size_t row) {
            return row < track.notes.size()
                && noteCellIsActivePulse(track.notes[row]);
        };
        std::size_t authoredHits = 0u;
        for (std::size_t row = 0u; row < length; ++row)
            if (isHit(row)) ++authoredHits;
        const std::array<std::size_t, 3u> anchors {
            0u, length / 3u, (length * 2u) / 3u,
        };
        for (const auto row : anchors) {
            if (authoredHits >= 3u) break;
            if (isHit(row)) continue;
            track.notes[row] = NoteCell::withNote(
                s3g::tracker::laneDefaultNote(model->session, lane));
            ++authoredHits;
        }
        for (std::size_t row = 0u;
             row < length && authoredHits < 3u; ++row) {
            if (isHit(row)) continue;
            track.notes[row] = NoteCell::withNote(
                s3g::tracker::laneDefaultNote(model->session, lane));
            ++authoredHits;
        }
        if (activeHits >= 6u) continue;
        std::size_t playheadRow = 0u;
        double nearestDistance = 2.0;
        for (std::size_t row = 0u; row < length; ++row) {
            if (!isHit(row)) continue;
            const double phase = static_cast<double>(row)
                / static_cast<double>(length);
            const double direct = std::abs(
                phase - playheadPhases[activeHits]);
            const double distance = std::min(direct, 1.0 - direct);
            if (distance >= nearestDistance) continue;
            nearestDistance = distance;
            playheadRow = row;
        }
        model->notePlayheads[lane] = playheadRow;
        model->noteHits[lane] = true;
        model->noteHitRows[lane] = playheadRow;
        _readHeadHaloRows[lane] = playheadRow;
        _readHeadHaloStrength[lane] = 1.0;
        _documentationHitLanes[lane] = true;
        ++activeHits;
    }
    [self setNeedsDisplay:YES];
    [self.playbackOverlay setNeedsDisplay:YES];
    return static_cast<NSInteger>(activeHits);
}

- (NSRect)zoomOutRect
{
    const NSRect canvas = [self canvasRect];
    return NSMakeRect(std::max<CGFloat>(NSMinX(canvas) + 180.0,
            NSMaxX(canvas) - 118.0), NSMinY(canvas) + 3.0, 28.0, 15.0);
}

- (NSRect)zoomResetRect
{
    const NSRect previous = [self zoomOutRect];
    return NSMakeRect(NSMaxX(previous) + 2.0,
        NSMinY(previous), 56.0, 15.0);
}

- (NSRect)zoomInRect
{
    const NSRect previous = [self zoomResetRect];
    return NSMakeRect(NSMaxX(previous) + 2.0,
        NSMinY(previous), 26.0, 15.0);
}

- (void)setGeometryZoomAndRedraw:(CGFloat)value
{
    self.geometryZoom = std::clamp(value, 0.65, 1.8);
    [self setNeedsDisplay:YES];
    [self.playbackOverlay setNeedsDisplay:YES];
}

- (void)updateTrackingAreas
{
    [super updateTrackingAreas];
    if (_geometryTrackingArea)
        [self removeTrackingArea:_geometryTrackingArea];
    _geometryTrackingArea = [[NSTrackingArea alloc] initWithRect:self.bounds
        options:(NSTrackingMouseMoved | NSTrackingActiveInKeyWindow
            | NSTrackingInVisibleRect)
        owner:self userInfo:nil];
    [self addTrackingArea:_geometryTrackingArea];
}

- (void)resetCursorRects
{
    [super resetCursorRects];
    [self addCursorRect:[self zoomOutRect] cursor:NSCursor.pointingHandCursor];
    [self addCursorRect:[self zoomResetRect]
        cursor:NSCursor.pointingHandCursor];
    [self addCursorRect:[self zoomInRect] cursor:NSCursor.pointingHandCursor];
    if (!self.burstLibraryOnly)
        [self addCursorRect:[self viewMenuBoxRect]
            cursor:NSCursor.pointingHandCursor];
    if (self.geometryViewMode == S3GTrackerGeometryViewModePitchMap) {
        for (const NSRect rect : { [self laneMenuBoxRect],
                 [self pitchScopeMenuBoxRect], [self pitchRootMenuBoxRect],
                 [self pitchScaleMenuBoxRect], [self pitchContourMenuBoxRect],
                 [self pitchLeapMenuBoxRect], [self pitchAnchorToggleRect],
                 [self pitchInvertToggleRect], [self pitchReverseToggleRect],
                 [self pitchAnalyzeHeaderButtonRect],
                 [self pitchPreviewHeaderButtonRect],
                 [self pitchNewSeedHeaderButtonRect],
                 [self pitchApplyHeaderButtonRect] })
            [self addCursorRect:rect cursor:NSCursor.pointingHandCursor];
        if ([self canEditDisplayedPattern]) {
            [self addCursorRect:s3g::clap_gui::cocoaRect(
                layout::sliderHitRect([self geometryLayout].laneCycle, 4u))
                cursor:NSCursor.resizeLeftRightCursor];
            [self addCursorRect:s3g::clap_gui::cocoaRect(
                layout::sliderHitRect([self geometryLayout].laneCycle, 5u))
                cursor:NSCursor.resizeLeftRightCursor];
            if (_pitchSettings.contour != PitchContour::Fit
                && _pitchSettings.contour != PitchContour::Manual)
                [self addCursorRect:s3g::clap_gui::cocoaRect(
                    layout::sliderHitRect(
                        [self geometryLayout].editShape, 2u))
                    cursor:NSCursor.resizeLeftRightCursor];
            [self addCursorRect:s3g::clap_gui::cocoaRect(
                layout::sliderHitRect([self geometryLayout].editShape, 4u))
                cursor:NSCursor.resizeLeftRightCursor];
            [self addCursorRect:[self pitchGraphRect]
                cursor:NSCursor.openHandCursor];
        }
        return;
    }
    [self addCursorRect:[self revealHeaderButtonRect]
        cursor:NSCursor.pointingHandCursor];
    if (self.geometryViewMode == S3GTrackerGeometryViewModeBurst)
        [self addCursorRect:[self fitBurstGatesHeaderButtonRect]
            cursor:NSCursor.pointingHandCursor];
    if (self.geometryViewMode == S3GTrackerGeometryViewModeBurst) {
        [self addCursorRect:[self burstPreviewHeaderButtonRect]
            cursor:NSCursor.pointingHandCursor];
    }
    for (NSUInteger index = 0u; index < 4u; ++index)
        [self addCursorRect:[self editToolButtonRect:index]
            cursor:NSCursor.pointingHandCursor];
    if (![self canEditDisplayedPattern]) return;
    [self addCursorRect:[self laneMenuBoxRect]
        cursor:NSCursor.pointingHandCursor];
    [self addCursorRect:[self directionMenuBoxRect]
        cursor:NSCursor.pointingHandCursor];
    [self addCursorRect:[self morphTargetMenuBoxRect]
        cursor:NSCursor.pointingHandCursor];
    [self addCursorRect:[self linkVelocityLengthToggleRect]
        cursor:NSCursor.pointingHandCursor];
    [self addCursorRect:[self reverseButtonRect]
        cursor:NSCursor.pointingHandCursor];
    [self addCursorRect:[self reflectButtonRect]
        cursor:NSCursor.pointingHandCursor];
    for (NSUInteger index = 0u; index < self.morphButtons.count; ++index)
        [self addCursorRect:[self morphAmountButtonRect:index]
            cursor:NSCursor.pointingHandCursor];
    [self addCursorRect:[self sliderHitRect:[self defaultNoteSliderTrack]]
        cursor:NSCursor.resizeLeftRightCursor];
    [self addCursorRect:[self sliderHitRect:[self lengthSliderTrack]]
        cursor:NSCursor.resizeLeftRightCursor];
    [self addCursorRect:[self sliderHitRect:[self rotateSliderTrack]]
        cursor:NSCursor.resizeLeftRightCursor];
    [self addCursorRect:[self sliderHitRect:[self densitySliderTrack]]
        cursor:NSCursor.resizeLeftRightCursor];
    if (self.geometryViewMode == S3GTrackerGeometryViewModeRingField) {
        const NSPoint rotate = [self rotateHandlePoint];
        const NSPoint density = [self densityHandlePoint];
        [self addCursorRect:NSMakeRect(rotate.x - 11.0, rotate.y - 11.0,
            22.0, 22.0) cursor:NSCursor.openHandCursor];
        [self addCursorRect:NSMakeRect(density.x - 11.0, density.y - 11.0,
            22.0, 22.0) cursor:NSCursor.openHandCursor];
    }
}

- (BOOL)canEditDisplayedPattern
{
    return self.trackerState && !self.trackerState->songPlaybackActive;
}

- (Track*)selectedEditableTrack
{
    auto* model = self.trackerState;
    if (![self canEditDisplayedPattern]
        || model->session.pattern.tracks.empty()) return nullptr;
    auto lane = std::min(model->session.selectedTrack,
        model->session.pattern.tracks.size() - 1u);
    if (model->session.pattern.tracks[lane].noteColumn.muted) {
        const auto visible = visibleGeometryLanes(&model->session.pattern);
        if (visible.count == 0u) return nullptr;
        lane = visible.indices[0u];
        model->session.selectedTrack = lane;
    }
    return &model->session.pattern.tracks[lane];
}

- (void)commitGeometryChange:(BOOL)changed
{
    if (!changed) return;
    auto* model = self.trackerState;
    if (model) model->session.pattern.visibleRows = std::max(
        model->session.pattern.visibleRows,
        model->session.selectedRow + 1u);
    [self.owner modulePatternChanged];
}

- (void)rotateBack:(id)sender
{
    (void)sender;
    Track* track = [self selectedEditableTrack];
    [self commitGeometryChange:track
        && s3g::tracker::rotateGeometryRows(*track, -1)];
}

- (void)rotateForward:(id)sender
{
    (void)sender;
    Track* track = [self selectedEditableTrack];
    [self commitGeometryChange:track
        && s3g::tracker::rotateGeometryRows(*track, 1)];
}

- (void)densityDown:(id)sender
{
    (void)sender;
    auto* model = self.trackerState;
    Track* track = [self selectedEditableTrack];
    if (!model || !track) return;
    const auto hits = s3g::tracker::geometryHitCount(*track);
    [self commitGeometryChange:s3g::tracker::setGeometryDensity(*track,
        hits > 0u ? hits - 1u : 0u,
        s3g::tracker::laneDefaultNote(model->session,
            model->session.selectedTrack))];
}

- (void)densityUp:(id)sender
{
    (void)sender;
    auto* model = self.trackerState;
    Track* track = [self selectedEditableTrack];
    if (!model || !track) return;
    const auto length = std::clamp<std::size_t>(
        track->noteColumn.length, 1u, 256u);
    const auto hits = s3g::tracker::geometryHitCount(*track);
    [self commitGeometryChange:s3g::tracker::setGeometryDensity(*track,
        std::min(hits + 1u, length),
        s3g::tracker::laneDefaultNote(model->session,
            model->session.selectedTrack))];
}

- (void)reverse:(id)sender
{
    (void)sender;
    Track* track = [self selectedEditableTrack];
    [self commitGeometryChange:track
        && s3g::tracker::reverseGeometry(*track)];
}

- (void)reflect:(id)sender
{
    (void)sender;
    auto* model = self.trackerState;
    Track* track = [self selectedEditableTrack];
    [self commitGeometryChange:model && track
        && s3g::tracker::reflectGeometry(
            *track, model->session.selectedRow)];
}

- (void)morphAmount:(NSButton*)sender
{
    auto* model = self.trackerState;
    Track* track = [self selectedEditableTrack];
    if (!model || !track) return;
    const auto visible = visibleGeometryLanes(&model->session.pattern);
    if (visible.count < 2u) return;
    std::size_t ordinal = 0u;
    for (; ordinal < visible.count; ++ordinal)
        if (visible.indices[ordinal] == model->session.selectedTrack) break;
    if (ordinal == visible.count) return;
    const bool previous = self.morphTargetPopup.indexOfSelectedItem == 0;
    const auto targetOrdinal = previous
        ? (ordinal + visible.count - 1u) % visible.count
        : (ordinal + 1u) % visible.count;
    const auto targetLane = visible.indices[targetOrdinal];
    const Track target = model->session.pattern.tracks[targetLane];
    const float amount = static_cast<float>(std::clamp<NSInteger>(
        sender.tag, 0, 100)) / 100.0f;
    [self commitGeometryChange:s3g::tracker::morphGeometry(*track, target,
        amount, s3g::tracker::laneDefaultNote(model->session,
            model->session.selectedTrack))];
}

- (void)revealInTracker:(id)sender
{
    (void)sender;
    auto* model = self.trackerState;
    if (!model) return;
    model->session.selectedPage = 0u;
    [self.owner moduleSelectionChanged];
    if (self.owner.trackerCallbacks
        && self.owner.trackerCallbacks->showTrackerPage) {
        self.owner.trackerCallbacks->showTrackerPage();
    } else {
        [self.owner showTrackerPage:nil];
        [self.owner focusTracker];
    }
}

- (void)selectLane:(std::size_t)lane row:(std::size_t)row
    field:(std::size_t)field
{
    auto* model = self.trackerState;
    const auto* pattern = geometryPattern(model);
    if (!model || !pattern || pattern->tracks.empty()) return;
    const auto lanes = std::min<std::size_t>(
        s3g::tracker::kMaximumTrackCount,
        pattern->tracks.size());
    model->session.selectedTrack = std::min(lane, lanes - 1u);
    const auto& track = pattern->tracks[model->session.selectedTrack];
    const auto length = std::clamp<std::size_t>(
        track.noteColumn.length, 1u, 256u);
    model->session.selectedRow = row % length;
    model->session.selectedField = std::min<std::size_t>(field, 1u);
    self.accessibilityValue = [NSString stringWithFormat:
        @"Lane %lu, %@, row %lu",
        static_cast<unsigned long>(model->session.selectedTrack + 1u),
        nsString(track.name),
        static_cast<unsigned long>(model->session.selectedRow + 1u)];
    [self.owner moduleSelectionChanged];
}

- (void)selectLane:(std::size_t)lane
{
    auto* model = self.trackerState;
    [self selectLane:lane row:model ? model->session.selectedRow : 0u
        field:model ? model->session.selectedField : 0u];
}

- (NSPoint)geometryCenter
{
    const NSRect plot = [self canvasPlotRect];
    return NSMakePoint(NSMidX(plot), NSMidY(plot));
}

- (CGFloat)geometryMaximumRadius
{
    const NSRect canvas = [self canvasPlotRect];
    return std::max<CGFloat>(30.0,
        std::min(NSWidth(canvas), NSHeight(canvas)) * 0.43)
        * self.geometryZoom;
}

- (CGFloat)ringRadiusForOrdinal:(std::size_t)ordinal
    count:(std::size_t)count
{
    const CGFloat maximum = [self geometryMaximumRadius];
    if (count <= 1u) return maximum * 0.64;
    const CGFloat inner = maximum * 0.22;
    return inner + (maximum - inner)
        * static_cast<CGFloat>(ordinal)
        / static_cast<CGFloat>(count - 1u);
}

- (CGFloat)ringRadiusForLane:(std::size_t)lane
{
    const auto lanes = geometryLanes(geometryPattern(self.trackerState));
    for (std::size_t ordinal = 0u; ordinal < lanes.count; ++ordinal) {
        if (lanes.indices[ordinal] == lane)
            return [self ringRadiusForOrdinal:ordinal count:lanes.count];
    }
    return 0.0;
}

- (BOOL)selectedRingLane:(std::size_t*)lane radius:(CGFloat*)radius
{
    auto* model = self.trackerState;
    const auto* pattern = geometryPattern(model);
    const auto visible = visibleGeometryLanes(model);
    const auto lanes = geometryLanes(pattern);
    if (!model || !pattern || visible.count == 0u) return NO;
    std::size_t selectedLane = visible.indices[0u];
    for (std::size_t ordinal = 0u; ordinal < visible.count; ++ordinal) {
        if (visible.indices[ordinal] == model->session.selectedTrack) {
            selectedLane = visible.indices[ordinal];
            break;
        }
    }
    if (lane) *lane = selectedLane;
    if (radius) {
        *radius = 0.0;
        for (std::size_t ordinal = 0u; ordinal < lanes.count; ++ordinal) {
            if (lanes.indices[ordinal] != selectedLane) continue;
            *radius = [self ringRadiusForOrdinal:ordinal count:lanes.count];
            break;
        }
    }
    return YES;
}

- (CGFloat)geometryAngleForPoint:(NSPoint)point
{
    const NSPoint center = [self geometryCenter];
    return std::atan2(point.y - center.y, point.x - center.x);
}

- (NSPoint)geometryPointAtRadius:(CGFloat)radius angle:(CGFloat)angle
{
    const NSPoint center = [self geometryCenter];
    return NSMakePoint(center.x + std::cos(angle) * radius,
        center.y + std::sin(angle) * radius);
}

- (NSPoint)rotateHandlePoint
{
    std::size_t lane = 0u;
    CGFloat radius = 0.0;
    const auto* pattern = geometryPattern(self.trackerState);
    if (![self selectedRingLane:&lane radius:&radius]
        || !pattern || lane >= pattern->tracks.size()) return NSZeroPoint;
    const auto& track = pattern->tracks[lane];
    const auto length = _geometryGestureActive
            && _geometryGestureKind == S3GTrackerGeometryGestureLength
            && _gestureLane == lane
        ? _gesturePreviewLength : std::clamp<std::size_t>(
            track.noteColumn.length, 1u, 256u);
    const int rotation = _geometryGestureActive
            && _geometryGestureKind == S3GTrackerGeometryGestureRotate
            && _gestureLane == lane
        ? _gesturePreviewRotation : 0;
    const auto signedLength = static_cast<long long>(length);
    const auto displayRow = static_cast<std::size_t>(
        (static_cast<long long>(rotation) % signedLength
            + signedLength) % signedLength);
    const CGFloat angle = -static_cast<CGFloat>(M_PI_2)
        + static_cast<CGFloat>(displayRow) * 2.0 * static_cast<CGFloat>(M_PI)
            / static_cast<CGFloat>(length);
    // Keep the rotate control inside the ring so it never masks a note bead.
    return [self geometryPointAtRadius:std::max<CGFloat>(0.0, radius - 13.0)
        angle:angle];
}

- (NSPoint)densityHandlePoint
{
    std::size_t lane = 0u;
    CGFloat radius = 0.0;
    const auto* pattern = geometryPattern(self.trackerState);
    if (![self selectedRingLane:&lane radius:&radius]
        || !pattern || lane >= pattern->tracks.size()) return NSZeroPoint;
    const auto& track = pattern->tracks[lane];
    const auto length = _geometryGestureActive
            && _geometryGestureKind == S3GTrackerGeometryGestureLength
            && _gestureLane == lane
        ? _gesturePreviewLength : std::clamp<std::size_t>(
            track.noteColumn.length, 1u, 256u);
    std::size_t density = 0u;
    for (std::size_t row = 0u; row < length; ++row) {
        if (row < track.notes.size()
            && noteCellIsActivePulse(track.notes[row]))
            ++density;
    }
    density = _geometryGestureActive
            && _geometryGestureKind == S3GTrackerGeometryGestureDensity
            && _gestureLane == lane
        ? _gesturePreviewDensity : density;
    const CGFloat angle = -static_cast<CGFloat>(M_PI_2)
        + static_cast<CGFloat>(density) * 2.0 * static_cast<CGFloat>(M_PI)
            / static_cast<CGFloat>(length);
    return [self geometryPointAtRadius:radius + 18.0 angle:angle];
}

- (void)prepareGeometryGesture:(S3GTrackerGeometryGestureKind)kind
    lane:(std::size_t)lane
{
    auto& track = self.trackerState->session.pattern.tracks[lane];
    const auto length = std::clamp<std::size_t>(
        track.noteColumn.length, 1u, 256u);
    _geometryGestureActive = YES;
    _geometryGestureChanged = NO;
    _geometryGestureKind = kind;
    _geometrySliderGesture = NO;
    _gestureLane = lane;
    _gestureOriginalDefaultNote = s3g::tracker::laneDefaultNote(
        self.trackerState->session, lane);
    _gesturePreviewDefaultNote = _gestureOriginalDefaultNote;
    _gestureOriginalLength = length;
    _gesturePreviewLength = length;
    _gestureOriginalPhase = track.noteColumn.phase % length;
    _gesturePreviewPhase = _gestureOriginalPhase;
    _gesturePreviewRotation = 0;
    _gestureOriginalDensity = s3g::tracker::geometryHitCount(track);
    _gesturePreviewDensity = _gestureOriginalDensity;
    _gestureOriginalNotes = track.notes;
    _gesturePreviewNotes = track.notes;
}

- (void)updateRotationPreviewForTrack:(const Track&)track
{
    Track preview = track;
    preview.notes = _gestureOriginalNotes;
    _geometryGestureChanged = s3g::tracker::rotateGeometryRows(
        preview, _gesturePreviewRotation);
    _gesturePreviewNotes = _geometryGestureChanged
        ? std::move(preview.notes) : _gestureOriginalNotes;
}

- (void)updateDensityPreviewForTrack:(const Track&)track
{
    _geometryGestureChanged = _gesturePreviewDensity
        != _gestureOriginalDensity;
    if (!_geometryGestureChanged) {
        _gesturePreviewNotes = _gestureOriginalNotes;
        return;
    }
    Track preview = track;
    preview.notes = _gestureOriginalNotes;
    (void)s3g::tracker::setGeometryDensity(preview,
        _gesturePreviewDensity, s3g::tracker::laneDefaultNote(
            self.trackerState->session, _gestureLane));
    _gesturePreviewNotes = std::move(preview.notes);
}

- (BOOL)beginSliderGestureAtPoint:(NSPoint)point
{
    if (![self canEditDisplayedPattern]) return NO;
    if (self.geometryViewMode == S3GTrackerGeometryViewModePitchMap) {
        S3GTrackerGeometryGestureKind kind = S3GTrackerGeometryGestureNone;
        const auto lanePanel = [self geometryLayout].laneCycle;
        const auto contourPanel = [self geometryLayout].editShape;
        if (NSPointInRect(point, s3g::clap_gui::cocoaRect(
                layout::sliderHitRect(lanePanel, 4u))))
            kind = S3GTrackerGeometryGesturePitchMinimum;
        else if (NSPointInRect(point, s3g::clap_gui::cocoaRect(
                layout::sliderHitRect(lanePanel, 5u))))
            kind = S3GTrackerGeometryGesturePitchMaximum;
        else if (NSPointInRect(point, s3g::clap_gui::cocoaRect(
                layout::sliderHitRect(contourPanel, 2u)))
            && _pitchSettings.contour != PitchContour::Fit
            && _pitchSettings.contour != PitchContour::Manual)
            kind = S3GTrackerGeometryGesturePitchVariation;
        else if (NSPointInRect(point, s3g::clap_gui::cocoaRect(
                layout::sliderHitRect(contourPanel, 4u))))
            kind = S3GTrackerGeometryGesturePitchTranspose;
        if (kind == S3GTrackerGeometryGestureNone) return NO;
        _geometryGestureActive = YES;
        _geometryGestureChanged = NO;
        _geometrySliderGesture = YES;
        _geometryGestureKind = kind;
        [self updateSliderGestureAtPoint:point];
        return YES;
    }
    if (self.geometryViewMode == S3GTrackerGeometryViewModeBurst) {
        const auto& burst = self.trackerState->session.burstLibrary.bursts[
            _selectedBurstSlot];
        if (burst.empty()) return NO;
        const std::array<S3GTrackerGeometryGestureKind, 3u> kinds {{
            S3GTrackerGeometryGestureBurstNote,
            S3GTrackerGeometryGestureBurstVelocity,
            S3GTrackerGeometryGestureBurstGate,
        }};
        for (std::size_t index = 0u; index < kinds.size(); ++index) {
            const auto row = static_cast<uint32_t>(index + 1u);
            const NSRect hit = s3g::clap_gui::cocoaRect(
                layout::sliderHitRect([self geometryLayout].editShape, row));
            if (!NSPointInRect(point, hit)) continue;
            _geometryGestureActive = YES;
            _geometryGestureChanged = NO;
            _geometrySliderGesture = YES;
            _geometryGestureKind = kinds[index];
            [self updateSliderGestureAtPoint:point];
            return YES;
        }
        return NO;
    }
    S3GTrackerGeometryGestureKind kind = S3GTrackerGeometryGestureNone;
    if (NSPointInRect(point,
            [self sliderHitRect:[self defaultNoteSliderTrack]]))
        kind = S3GTrackerGeometryGestureDefaultNote;
    else if (NSPointInRect(point, [self sliderHitRect:[self lengthSliderTrack]]))
        kind = S3GTrackerGeometryGestureLength;
    else if (NSPointInRect(point,
            [self sliderHitRect:[self rotateSliderTrack]]))
        kind = S3GTrackerGeometryGestureRotate;
    else if (NSPointInRect(point,
            [self sliderHitRect:[self densitySliderTrack]]))
        kind = S3GTrackerGeometryGestureDensity;
    if (kind == S3GTrackerGeometryGestureNone) return NO;
    std::size_t lane = 0u;
    if (![self selectedRingLane:&lane radius:nullptr]) return NO;
    if (self.trackerState->session.selectedTrack != lane)
        [self selectLane:lane];
    [self prepareGeometryGesture:kind lane:lane];
    _geometrySliderGesture = YES;
    [self updateSliderGestureAtPoint:point];
    return YES;
}

- (void)updateSliderGestureAtPoint:(NSPoint)point
{
    if (!_geometryGestureActive || !_geometrySliderGesture
        || !self.trackerState) return;
    auto& pattern = self.trackerState->session.pattern;
    if (_geometryGestureKind == S3GTrackerGeometryGesturePitchMinimum
        || _geometryGestureKind == S3GTrackerGeometryGesturePitchMaximum
        || _geometryGestureKind == S3GTrackerGeometryGesturePitchVariation
        || _geometryGestureKind == S3GTrackerGeometryGesturePitchTranspose) {
        NSRect slider = _geometryGestureKind
                == S3GTrackerGeometryGesturePitchMinimum
            ? [self pitchMinimumSliderTrack]
            : _geometryGestureKind == S3GTrackerGeometryGesturePitchMaximum
            ? [self pitchMaximumSliderTrack]
            : _geometryGestureKind
                    == S3GTrackerGeometryGesturePitchTranspose
                ? [self pitchTransposeSliderTrack]
            : [self pitchVariationSliderTrack];
        const CGFloat normalized = std::clamp(
            (point.x - NSMinX(slider))
                / std::max<CGFloat>(1.0, NSWidth(slider)), 0.0, 1.0);
        if (_geometryGestureKind == S3GTrackerGeometryGesturePitchMinimum) {
            _pitchSettings.minimumNote = static_cast<uint8_t>(
                std::min<long>(std::lround(normalized * 127.0),
                    _pitchSettings.maximumNote));
        } else if (_geometryGestureKind
                == S3GTrackerGeometryGesturePitchMaximum) {
            _pitchSettings.maximumNote = static_cast<uint8_t>(
                std::max<long>(std::lround(normalized * 127.0),
                    _pitchSettings.minimumNote));
        } else if (_geometryGestureKind
                == S3GTrackerGeometryGesturePitchVariation) {
            _pitchSettings.variation = static_cast<float>(normalized);
        } else {
            _pitchSettings.transposeSemitones = static_cast<int8_t>(
                std::clamp<long>(std::lround(normalized * 48.0) - 24,
                    -24, 24));
        }
        _pitchOverrides.fill(-1);
        _pitchStatus = @"PREVIEW UPDATED";
        [self setNeedsDisplay:YES];
        return;
    }
    if (_geometryGestureKind == S3GTrackerGeometryGestureBurstNote
        || _geometryGestureKind == S3GTrackerGeometryGestureBurstVelocity
        || _geometryGestureKind == S3GTrackerGeometryGestureBurstGate) {
        auto& burst = self.trackerState->session.burstLibrary.bursts[
            _selectedBurstSlot];
        if (burst.empty()) return;
        auto& event = burst.events[std::min<std::size_t>(
            _selectedBurstEvent, burst.eventCount - 1u)];
        const uint32_t row = _geometryGestureKind
                == S3GTrackerGeometryGestureBurstNote
            ? 1u : _geometryGestureKind
                    == S3GTrackerGeometryGestureBurstVelocity ? 2u : 3u;
        const NSRect slider = [self burstSliderTrackForRow:row];
        const CGFloat normalized = std::clamp(
            (point.x - NSMinX(slider))
                / std::max<CGFloat>(1.0, NSWidth(slider)), 0.0, 1.0);
        if (_geometryGestureKind == S3GTrackerGeometryGestureBurstNote) {
            const auto value = static_cast<uint8_t>(std::lround(
                normalized * 127.0));
            _geometryGestureChanged |= event.note != value;
            event.note = value;
        } else if (_geometryGestureKind
                == S3GTrackerGeometryGestureBurstVelocity) {
            const auto value = static_cast<uint8_t>(std::clamp<long>(
                std::lround(normalized * 127.0), 1l, 127l));
            _geometryGestureChanged |= event.velocity != value;
            event.velocity = value;
        } else {
            const auto value = static_cast<uint8_t>(std::clamp<long>(
                std::lround(normalized * 100.0), 1l, 100l));
            _geometryGestureChanged |= event.gatePercent != value;
            event.gatePercent = value;
        }
        [self setNeedsDisplay:YES];
        return;
    }
    if (_gestureLane >= pattern.tracks.size()) return;
    auto& track = pattern.tracks[_gestureLane];
    NSRect slider = _geometryGestureKind
            == S3GTrackerGeometryGestureDefaultNote
        ? [self defaultNoteSliderTrack] : [self lengthSliderTrack];
    if (_geometryGestureKind == S3GTrackerGeometryGestureRotate)
        slider = [self rotateSliderTrack];
    else if (_geometryGestureKind == S3GTrackerGeometryGestureDensity)
        slider = [self densitySliderTrack];
    const CGFloat normalized = std::clamp(
        (point.x - NSMinX(slider)) / std::max<CGFloat>(1.0, NSWidth(slider)),
        0.0, 1.0);
    if (_geometryGestureKind == S3GTrackerGeometryGestureDefaultNote) {
        _gesturePreviewDefaultNote = static_cast<uint8_t>(std::lround(
            normalized * 127.0));
        _geometryGestureChanged = _gesturePreviewDefaultNote
            != _gestureOriginalDefaultNote
            || std::any_of(track.notes.begin(), track.notes.end(),
                [&](const NoteCell& cell) {
                    return cell.state == NoteCellState::Note
                        && cell.note != _gesturePreviewDefaultNote;
                });
    } else if (_geometryGestureKind == S3GTrackerGeometryGestureLength) {
        // A square response gives common short cycles enough physical travel
        // while retaining every legal 1–256-step length.
        _gesturePreviewLength = 1u + static_cast<std::size_t>(std::lround(
            normalized * normalized * 255.0));
        _gesturePreviewPhase = _gestureOriginalPhase % _gesturePreviewLength;
        _geometryGestureChanged = _gesturePreviewLength
            != _gestureOriginalLength
            || (self.linkVelocityLength
                && track.velocityColumn.length != _gesturePreviewLength);
    } else if (_geometryGestureKind == S3GTrackerGeometryGestureRotate) {
        const auto length = std::clamp<std::size_t>(
            track.noteColumn.length, 1u, 256u);
        const CGFloat bipolar = normalized * 2.0 - 1.0;
        const CGFloat shaped = std::copysign(bipolar * bipolar, bipolar);
        _gesturePreviewRotation = length <= 1u ? 0
            : static_cast<int>(std::lround(shaped
                * static_cast<CGFloat>(length - 1u)));
        [self updateRotationPreviewForTrack:track];
    } else if (_geometryGestureKind
            == S3GTrackerGeometryGestureDensity) {
        const auto length = std::clamp<std::size_t>(
            track.noteColumn.length, 1u, 256u);
        _gesturePreviewDensity = static_cast<std::size_t>(std::lround(
            normalized * static_cast<CGFloat>(length)));
        [self updateDensityPreviewForTrack:track];
    }
    [self setNeedsDisplay:YES];
}

- (BOOL)beginShapeGestureAtPoint:(NSPoint)point
{
    if (self.geometryViewMode != S3GTrackerGeometryViewModeRingField
        || ![self canEditDisplayedPattern]) return NO;
    auto* model = self.trackerState;
    std::size_t lane = 0u;
    CGFloat radius = 0.0;
    if (!model || ![self selectedRingLane:&lane radius:&radius]
        || lane >= model->session.pattern.tracks.size()) return NO;
    const NSPoint rotatePoint = [self rotateHandlePoint];
    const NSPoint densityPoint = [self densityHandlePoint];
    const CGFloat rotateDistance = std::hypot(
        point.x - rotatePoint.x, point.y - rotatePoint.y);
    const CGFloat densityDistance = std::hypot(
        point.x - densityPoint.x, point.y - densityPoint.y);
    if (rotateDistance > 11.0 && densityDistance > 11.0) return NO;

    const auto kind = rotateDistance <= densityDistance
        ? S3GTrackerGeometryGestureRotate
        : S3GTrackerGeometryGestureDensity;
    if (model->session.selectedTrack != lane) [self selectLane:lane];
    [self prepareGeometryGesture:kind lane:lane];
    _gestureLastAngle = [self geometryAngleForPoint:point];
    _gestureAccumulatedAngle = 0.0;
    [self setNeedsDisplay:YES];
    return YES;
}

- (void)updateShapeGestureAtPoint:(NSPoint)point
{
    if (!_geometryGestureActive || !self.trackerState
        || (_geometryGestureKind != S3GTrackerGeometryGestureRotate
            && _geometryGestureKind
                != S3GTrackerGeometryGestureDensity)) return;
    auto& pattern = self.trackerState->session.pattern;
    if (_gestureLane >= pattern.tracks.size()) return;
    auto& track = pattern.tracks[_gestureLane];
    const auto length = std::clamp<std::size_t>(
        track.noteColumn.length, 1u, 256u);
    const CGFloat fullCircle = static_cast<CGFloat>(M_PI) * 2.0;
    const CGFloat currentAngle = [self geometryAngleForPoint:point];
    CGFloat delta = currentAngle - _gestureLastAngle;
    if (delta > static_cast<CGFloat>(M_PI)) delta -= fullCircle;
    if (delta < -static_cast<CGFloat>(M_PI)) delta += fullCircle;
    _gestureAccumulatedAngle += delta;
    _gestureLastAngle = currentAngle;
    const auto stepDelta = static_cast<long long>(std::lround(
        _gestureAccumulatedAngle / fullCircle
            * static_cast<CGFloat>(length)));
    if (_geometryGestureKind == S3GTrackerGeometryGestureRotate) {
        _gesturePreviewRotation = static_cast<int>(std::clamp<long long>(
            stepDelta, -static_cast<long long>(length - 1u),
            static_cast<long long>(length - 1u)));
        [self updateRotationPreviewForTrack:track];
    } else {
        const auto density = std::clamp<long long>(
            static_cast<long long>(_gestureOriginalDensity) + stepDelta,
            0ll, static_cast<long long>(length));
        _gesturePreviewDensity = static_cast<std::size_t>(density);
        [self updateDensityPreviewForTrack:track];
    }
    [self setNeedsDisplay:YES];
}

- (void)finishGeometryGesture
{
    if (!_geometryGestureActive) return;
    if (_geometryGestureKind == S3GTrackerGeometryGesturePitchMinimum
        || _geometryGestureKind == S3GTrackerGeometryGesturePitchMaximum
        || _geometryGestureKind == S3GTrackerGeometryGesturePitchVariation
        || _geometryGestureKind == S3GTrackerGeometryGesturePitchTranspose
        || _geometryGestureKind == S3GTrackerGeometryGesturePitchPoint) {
        _geometryGestureActive = NO;
        _geometrySliderGesture = NO;
        _geometryGestureKind = S3GTrackerGeometryGestureNone;
        _geometryGestureChanged = NO;
        _pitchDragAssignment = -1;
        [self setNeedsDisplay:YES];
        return;
    }
    if (_geometryGestureKind == S3GTrackerGeometryGestureBurstPosition
        || _geometryGestureKind == S3GTrackerGeometryGestureBurstNote
        || _geometryGestureKind == S3GTrackerGeometryGestureBurstVelocity
        || _geometryGestureKind == S3GTrackerGeometryGestureBurstGate
        || _geometryGestureKind
            == S3GTrackerGeometryGestureBurstMatrixPosition
        || _geometryGestureKind
            == S3GTrackerGeometryGestureBurstMatrixNote
        || _geometryGestureKind
            == S3GTrackerGeometryGestureBurstMatrixVelocity
        || _geometryGestureKind
            == S3GTrackerGeometryGestureBurstMatrixGate
        || _geometryGestureKind
            == S3GTrackerGeometryGestureBurstVelocityPoint) {
        const BOOL changed = _geometryGestureChanged;
        _geometryGestureActive = NO;
        _geometrySliderGesture = NO;
        _geometryGestureKind = S3GTrackerGeometryGestureNone;
        _geometryGestureChanged = NO;
        if (changed) [self.owner modulePatternChanged];
        [self setNeedsDisplay:YES];
        return;
    }
    auto* model = self.trackerState;
    BOOL changed = _geometryGestureChanged;
    if (model && _gestureLane < model->session.pattern.tracks.size()) {
        auto& track = model->session.pattern.tracks[_gestureLane];
        if (changed && _geometryGestureKind
                == S3GTrackerGeometryGestureDefaultNote) {
            changed = s3g::tracker::setLaneDefaultNote(model->session,
                _gestureLane, _gesturePreviewDefaultNote);
        } else if (changed
            && _geometryGestureKind == S3GTrackerGeometryGestureLength) {
            changed = s3g::tracker::setGeometryNoteLength(track,
                _gesturePreviewLength, self.linkVelocityLength);
            model->session.pattern.visibleRows = std::max(
                model->session.pattern.visibleRows, _gesturePreviewLength);
        } else if (changed
            && _geometryGestureKind == S3GTrackerGeometryGestureRotate) {
            changed = s3g::tracker::rotateGeometryRows(track,
                _gesturePreviewRotation);
        } else if (changed
            && _geometryGestureKind
                == S3GTrackerGeometryGestureDensity) {
            changed = s3g::tracker::setGeometryDensity(track,
                _gesturePreviewDensity,
                s3g::tracker::laneDefaultNote(model->session,
                    _gestureLane));
        }
    }
    _geometryGestureActive = NO;
    _geometrySliderGesture = NO;
    _geometryGestureKind = S3GTrackerGeometryGestureNone;
    _geometryGestureChanged = NO;
    _gestureOriginalNotes.clear();
    _gesturePreviewNotes.clear();
    [self commitGeometryChange:changed];
    [self.owner moduleSelectionChanged];
    [self setNeedsDisplay:YES];
}

- (NSRect)burstMatrixRect
{
    const NSRect plot = [self canvasPlotRect];
    const CGFloat width = std::floor((NSWidth(plot) - 54.0) * 0.64);
    return NSMakeRect(NSMinX(plot) + 18.0, NSMinY(plot) + 34.0,
        std::max<CGFloat>(360.0, width), 254.0);
}

- (NSRect)burstOverviewRect
{
    const NSRect plot = [self canvasPlotRect];
    const NSRect matrix = [self burstMatrixRect];
    return NSMakeRect(NSMaxX(matrix) + 18.0, NSMinY(matrix),
        std::max<CGFloat>(180.0, NSMaxX(plot) - NSMaxX(matrix) - 36.0),
        NSHeight(matrix));
}

- (NSRect)burstBreakpointRect
{
    const NSRect plot = [self canvasPlotRect];
    const NSRect matrix = [self burstMatrixRect];
    const CGFloat top = NSMaxY(matrix) + 38.0;
    return NSMakeRect(NSMinX(plot) + 18.0, top,
        NSWidth(plot) - 36.0,
        std::max<CGFloat>(84.0, NSMaxY(plot) - top - 42.0));
}

- (NSRect)burstRadialPlotRect
{
    const NSRect overview = [self burstOverviewRect];
    return NSInsetRect(NSMakeRect(NSMinX(overview),
        NSMinY(overview) + 19.0, NSWidth(overview),
        NSHeight(overview) - 19.0), 10.0, 8.0);
}

- (NSRect)burstMatrixRowRect:(std::size_t)row
{
    const NSRect matrix = [self burstMatrixRect];
    constexpr CGFloat headerHeight = 22.0;
    constexpr CGFloat rowHeight = 29.0;
    return NSMakeRect(NSMinX(matrix), NSMinY(matrix) + headerHeight
            + static_cast<CGFloat>(row) * rowHeight,
        NSWidth(matrix), rowHeight);
}

- (NSRect)burstMatrixCellRect:(std::size_t)row field:(NSInteger)field
{
    const NSRect rowRect = [self burstMatrixRowRect:row];
    const CGFloat numberWidth = 38.0;
    const CGFloat available = NSWidth(rowRect) - numberWidth;
    const std::array<CGFloat, 5u> edges {{
        NSMinX(rowRect),
        NSMinX(rowRect) + numberWidth,
        NSMinX(rowRect) + numberWidth + available * 0.25,
        NSMinX(rowRect) + numberWidth + available * 0.52,
        NSMinX(rowRect) + numberWidth + available * 0.78,
    }};
    if (field < 0) return NSMakeRect(edges[0], NSMinY(rowRect),
        numberWidth, NSHeight(rowRect));
    const auto index = static_cast<std::size_t>(std::clamp<NSInteger>(
        field, 0, 3));
    const CGFloat right = index == 3u ? NSMaxX(rowRect) : edges[index + 2u];
    return NSMakeRect(edges[index + 1u], NSMinY(rowRect),
        right - edges[index + 1u], NSHeight(rowRect));
}

- (NSInteger)burstMatrixRowAtPoint:(NSPoint)point
{
    const NSRect matrix = [self burstMatrixRect];
    if (!NSPointInRect(point, matrix) || point.y < NSMinY(matrix) + 22.0)
        return -1;
    const NSInteger row = static_cast<NSInteger>(
        (point.y - NSMinY(matrix) - 22.0) / 29.0);
    return row >= 0 && row < static_cast<NSInteger>(kMaximumBurstEvents)
        ? row : -1;
}

- (NSInteger)burstMatrixFieldAtPoint:(NSPoint)point row:(std::size_t)row
{
    for (NSInteger field = 0; field < 4; ++field)
        if (NSPointInRect(point,
                [self burstMatrixCellRect:row field:field])) return field;
    return -1;
}

- (NSInteger)burstBreakpointEventAtPoint:(NSPoint)point
{
    if (!self.trackerState) return -1;
    const auto& burst = self.trackerState->session.burstLibrary.bursts[
        _selectedBurstSlot];
    const NSRect graph = [self burstBreakpointRect];
    if (burst.empty() || !NSPointInRect(point, NSInsetRect(graph, -8.0, -8.0)))
        return -1;
    NSInteger result = -1;
    CGFloat best = 14.0;
    const NSRect inner = NSInsetRect(graph, 12.0, 14.0);
    for (std::size_t index = 0u; index < burst.eventCount; ++index) {
        const auto& authored = burst.events[index];
        const NSPoint marker = NSMakePoint(NSMinX(inner)
                + static_cast<CGFloat>(authored.position) / 65535.0
                    * NSWidth(inner),
            NSMaxY(inner) - static_cast<CGFloat>(authored.velocity - 1u)
                / 126.0 * NSHeight(inner));
        const CGFloat distance = std::hypot(
            point.x - marker.x, point.y - marker.y);
        if (distance >= best) continue;
        best = distance;
        result = static_cast<NSInteger>(index);
    }
    return result;
}

- (void)updateBurstMatrixGestureAtPoint:(NSPoint)point
{
    if (!self.trackerState || _selectedBurstEvent >= kMaximumBurstEvents)
        return;
    auto& burst = self.trackerState->session.burstLibrary.bursts[
        _selectedBurstSlot];
    if (burst.empty() || _selectedBurstEvent >= burst.eventCount) return;
    auto& authored = burst.events[_selectedBurstEvent];
    if (_geometryGestureKind
            == S3GTrackerGeometryGestureBurstVelocityPoint) {
        const NSRect inner = NSInsetRect(_burstGestureRect, 12.0, 14.0);
        const CGFloat x = std::clamp((point.x - NSMinX(inner))
                / std::max<CGFloat>(1.0, NSWidth(inner)), 0.0, 1.0);
        const CGFloat y = std::clamp((NSMaxY(inner) - point.y)
                / std::max<CGFloat>(1.0, NSHeight(inner)), 0.0, 1.0);
        uint16_t position = static_cast<uint16_t>(std::lround(x * 65535.0));
        const uint16_t minimum = _selectedBurstEvent == 0u ? 0u
            : burst.events[_selectedBurstEvent - 1u].position;
        const uint16_t maximum = _selectedBurstEvent + 1u >= burst.eventCount
            ? 65535u : burst.events[_selectedBurstEvent + 1u].position;
        position = std::clamp(position, minimum, maximum);
        const auto velocity = static_cast<uint8_t>(std::clamp<long>(
            std::lround(1.0 + y * 126.0), 1l, 127l));
        _geometryGestureChanged |= authored.position != position
            || authored.velocity != velocity;
        authored.position = position;
        authored.velocity = velocity;
        [self setNeedsDisplay:YES];
        return;
    }
    const CGFloat normalized = std::clamp((point.x - NSMinX(_burstGestureRect))
            / std::max<CGFloat>(1.0, NSWidth(_burstGestureRect)), 0.0, 1.0);
    if (_geometryGestureKind
            == S3GTrackerGeometryGestureBurstMatrixPosition) {
        uint16_t position = static_cast<uint16_t>(std::lround(
            normalized * 65535.0));
        const uint16_t minimum = _selectedBurstEvent == 0u ? 0u
            : burst.events[_selectedBurstEvent - 1u].position;
        const uint16_t maximum = _selectedBurstEvent + 1u >= burst.eventCount
            ? 65535u : burst.events[_selectedBurstEvent + 1u].position;
        position = std::clamp(position, minimum, maximum);
        _geometryGestureChanged |= authored.position != position;
        authored.position = position;
    } else if (_geometryGestureKind
            == S3GTrackerGeometryGestureBurstMatrixNote) {
        const auto value = static_cast<uint8_t>(std::lround(
            normalized * 127.0));
        _geometryGestureChanged |= authored.note != value;
        authored.note = value;
    } else if (_geometryGestureKind
            == S3GTrackerGeometryGestureBurstMatrixVelocity) {
        const auto value = static_cast<uint8_t>(std::clamp<long>(
            std::lround(1.0 + normalized * 126.0), 1l, 127l));
        _geometryGestureChanged |= authored.velocity != value;
        authored.velocity = value;
    } else if (_geometryGestureKind
            == S3GTrackerGeometryGestureBurstMatrixGate) {
        const auto value = static_cast<uint8_t>(std::clamp<long>(
            std::lround(1.0 + normalized * 99.0), 1l, 100l));
        _geometryGestureChanged |= authored.gatePercent != value;
        authored.gatePercent = value;
    }
    [self setNeedsDisplay:YES];
}

- (BOOL)beginBurstCanvasGestureAtPoint:(NSPoint)point
{
    if (!self.trackerState) return NO;
    auto& burst = self.trackerState->session.burstLibrary.bursts[
        _selectedBurstSlot];
    const NSInteger matrixRow = [self burstMatrixRowAtPoint:point];
    if (matrixRow >= 0) {
        const auto row = static_cast<std::size_t>(matrixRow);
        if (row >= burst.eventCount) {
            if (![self canEditDisplayedPattern]) return YES;
            if (burst.empty()) [self initializeBurstAtSlot:_selectedBurstSlot];
            const auto previousCount = static_cast<std::size_t>(burst.eventCount);
            const auto targetCount = row + 1u;
            const BurstEvent seed = previousCount > 0u
                ? burst.events[previousCount - 1u] : BurstEvent {};
            for (std::size_t index = previousCount; index < targetCount;
                 ++index) burst.events[index] = seed;
            burst.eventCount = static_cast<uint8_t>(targetCount);
            setGeometryBurstTiming(burst, "even");
            [self.owner modulePatternChanged];
        }
        _selectedBurstEvent = row;
        const NSInteger field = [self burstMatrixFieldAtPoint:point row:row];
        if (field < 0 || ![self canEditDisplayedPattern]) {
            [self setNeedsDisplay:YES];
            return YES;
        }
        _selectedBurstField = field;
        const std::array<S3GTrackerGeometryGestureKind, 4u> kinds {{
            S3GTrackerGeometryGestureBurstMatrixPosition,
            S3GTrackerGeometryGestureBurstMatrixNote,
            S3GTrackerGeometryGestureBurstMatrixVelocity,
            S3GTrackerGeometryGestureBurstMatrixGate,
        }};
        _burstGestureRect = NSInsetRect(
            [self burstMatrixCellRect:row field:field], 5.0, 0.0);
        _geometryGestureActive = YES;
        _geometryGestureChanged = NO;
        _geometrySliderGesture = NO;
        _geometryGestureKind = kinds[static_cast<std::size_t>(field)];
        [self setNeedsDisplay:YES];
        return YES;
    }
    const NSInteger breakpoint = [self burstBreakpointEventAtPoint:point];
    if (breakpoint >= 0) {
        _selectedBurstEvent = static_cast<std::size_t>(breakpoint);
        _selectedBurstField = 2;
        if ([self canEditDisplayedPattern]) {
            _burstGestureRect = [self burstBreakpointRect];
            _geometryGestureActive = YES;
            _geometryGestureChanged = NO;
            _geometrySliderGesture = NO;
            _geometryGestureKind =
                S3GTrackerGeometryGestureBurstVelocityPoint;
        }
        [self setNeedsDisplay:YES];
        return YES;
    }
    return NO;
}

- (NSInteger)burstEventAtPoint:(NSPoint)point
{
    const auto& burst = self.trackerState->session.burstLibrary.bursts[
        _selectedBurstSlot];
    if (burst.empty() || !NSPointInRect(point, [self burstOverviewRect]))
        return -1;
    const NSRect plot = [self burstRadialPlotRect];
    const NSPoint center = NSMakePoint(NSMidX(plot), NSMidY(plot) - 4.0);
    const CGFloat radius = std::max<CGFloat>(50.0,
        std::min(NSWidth(plot), NSHeight(plot)) * 0.34 * self.geometryZoom);
    CGFloat best = 14.0;
    NSInteger result = -1;
    for (std::size_t index = 0u; index < burst.eventCount; ++index) {
        const auto& event = burst.events[index];
        const CGFloat angle = -static_cast<CGFloat>(M_PI_2)
            + static_cast<CGFloat>(event.position) / 65536.0
                * 2.0 * static_cast<CGFloat>(M_PI);
        const CGFloat eventRadius = radius
            + (static_cast<CGFloat>(event.note) / 127.0 - 0.5) * 30.0;
        const NSPoint marker = NSMakePoint(center.x + std::cos(angle)
                * eventRadius, center.y + std::sin(angle) * eventRadius);
        const CGFloat distance = std::hypot(
            point.x - marker.x, point.y - marker.y);
        if (distance >= best) continue;
        best = distance;
        result = static_cast<NSInteger>(index);
    }
    return result;
}

- (void)updateBurstPositionAtPoint:(NSPoint)point
{
    auto& burst = self.trackerState->session.burstLibrary.bursts[
        _selectedBurstSlot];
    if (burst.empty() || _selectedBurstEvent >= burst.eventCount) return;
    const NSRect plot = [self burstRadialPlotRect];
    const NSPoint center = NSMakePoint(NSMidX(plot), NSMidY(plot) - 4.0);
    CGFloat angle = std::atan2(point.y - center.y, point.x - center.x)
        + static_cast<CGFloat>(M_PI_2);
    const CGFloat fullCircle = 2.0 * static_cast<CGFloat>(M_PI);
    while (angle < 0.0) angle += fullCircle;
    while (angle >= fullCircle) angle -= fullCircle;
    uint16_t position = static_cast<uint16_t>(std::clamp<long>(
        std::lround(angle / fullCircle * 65536.0), 0l, 65535l));
    const uint16_t minimum = _selectedBurstEvent == 0u ? 0u
        : burst.events[_selectedBurstEvent - 1u].position;
    const uint16_t maximum = _selectedBurstEvent + 1u >= burst.eventCount
        ? 65535u : burst.events[_selectedBurstEvent + 1u].position;
    position = std::clamp(position, minimum, maximum);
    auto& authored = burst.events[_selectedBurstEvent].position;
    _geometryGestureChanged |= authored != position;
    authored = position;
    [self setNeedsDisplay:YES];
}

- (BOOL)geometryCellAtPoint:(NSPoint)point lane:(std::size_t*)lane
    row:(std::size_t*)row radius:(CGFloat*)hitRadius
{
    auto* model = self.trackerState;
    const auto* pattern = geometryPattern(model);
    const auto lanes = geometryLanes(pattern);
    const NSRect canvas = [self canvasRect];
    if (!pattern || lanes.count == 0u || !NSPointInRect(point, canvas))
        return NO;
    const NSPoint center = [self geometryCenter];
    const CGFloat distance = std::hypot(
        point.x - center.x, point.y - center.y);
    CGFloat best = std::numeric_limits<CGFloat>::max();
    std::size_t bestOrdinal = 0u;
    bool found = false;
    for (std::size_t ordinal = 0u; ordinal < lanes.count; ++ordinal) {
        if (geometryLaneMuted(model, pattern, lanes.indices[ordinal]))
            continue;
        const CGFloat candidate = [self ringRadiusForOrdinal:ordinal
            count:lanes.count];
        const CGFloat delta = std::abs(distance - candidate);
        if (delta < best) {
            best = delta;
            bestOrdinal = ordinal;
            found = true;
        }
    }
    if (!found) return NO;
    const CGFloat spacing = lanes.count > 1u
        ? std::abs([self ringRadiusForOrdinal:1u count:lanes.count]
            - [self ringRadiusForOrdinal:0u count:lanes.count])
        : 24.0;
    if (best > std::max<CGFloat>(8.0, spacing * 0.44)) return NO;
    const auto selectedLane = lanes.indices[bestOrdinal];
    const auto length = std::clamp<std::size_t>(
        pattern->tracks[selectedLane].noteColumn.length, 1u, 256u);
    CGFloat angle = std::atan2(point.y - center.y, point.x - center.x)
        + static_cast<CGFloat>(M_PI_2);
    const CGFloat fullCircle = static_cast<CGFloat>(M_PI) * 2.0;
    while (angle < 0.0) angle += fullCircle;
    while (angle >= fullCircle) angle -= fullCircle;
    auto selectedRow = static_cast<std::size_t>(std::lround(
        angle / fullCircle * static_cast<CGFloat>(length))) % length;
    if (lane) *lane = selectedLane;
    if (row) *row = selectedRow;
    if (hitRadius) *hitRadius = [self ringRadiusForOrdinal:bestOrdinal
        count:lanes.count];
    return YES;
}

- (BOOL)point:(NSPoint)point isNearBeadOnLane:(std::size_t)lane
    row:(std::size_t)row radius:(CGFloat)radius
{
    const auto* pattern = geometryPattern(self.trackerState);
    if (self.geometryViewMode != S3GTrackerGeometryViewModeRingField
        || !pattern || lane >= pattern->tracks.size()) return NO;
    const auto& track = pattern->tracks[lane];
    const auto length = std::clamp<std::size_t>(
        track.noteColumn.length, 1u, 256u);
    row %= length;
    if (!s3g::tracker::geometryCellIsHit(track, row)) return NO;
    const CGFloat angle = -static_cast<CGFloat>(M_PI_2)
        + static_cast<CGFloat>(row) * 2.0
            * static_cast<CGFloat>(M_PI) / static_cast<CGFloat>(length);
    const CGFloat eventRadius = radius
        + (resolvedVelocity(track, row) - 0.5) * 12.0;
    const NSPoint bead = [self geometryPointAtRadius:eventRadius angle:angle];
    return std::hypot(point.x - bead.x, point.y - bead.y) <= 11.0;
}

- (BOOL)revealBeadAtPoint:(NSPoint)point
{
    std::size_t lane = 0u;
    std::size_t row = 0u;
    CGFloat radius = 0.0;
    if (![self geometryCellAtPoint:point lane:&lane row:&row radius:&radius]
        || ![self point:point isNearBeadOnLane:lane row:row radius:radius])
        return NO;
    [self selectLane:lane row:row field:0u];
    [self revealInTracker:nil];
    return YES;
}

- (void)mouseDown:(NSEvent*)event
{
    auto* model = self.trackerState;
    const auto* pattern = geometryPattern(model);
    if (!model || !pattern || pattern->tracks.empty()) return;
    const NSPoint point = [self convertPoint:event.locationInWindow fromView:nil];
    if ([self handleToolboxClickAtPoint:point]) return;
    if (NSPointInRect(point, [self zoomOutRect])) {
        [self setGeometryZoomAndRedraw:self.geometryZoom / 1.15];
        return;
    }
    if (NSPointInRect(point, [self zoomResetRect])) {
        [self setGeometryZoomAndRedraw:1.0];
        return;
    }
    if (NSPointInRect(point, [self zoomInRect])) {
        [self setGeometryZoomAndRedraw:self.geometryZoom * 1.15];
        return;
    }
    if (self.geometryViewMode == S3GTrackerGeometryViewModePitchMap
        && event.clickCount >= 2) {
        const auto lanePanel = [self geometryLayout].laneCycle;
        const auto contourPanel = [self geometryLayout].editShape;
        if (NSPointInRect(point, s3g::clap_gui::cocoaRect(
                layout::sliderHitRect(lanePanel, 4u)))) {
            _pitchSettings.minimumNote = std::min<uint8_t>(
                36u, _pitchSettings.maximumNote);
            _pitchOverrides.fill(-1);
            [self setNeedsDisplay:YES];
            return;
        }
        if (NSPointInRect(point, s3g::clap_gui::cocoaRect(
                layout::sliderHitRect(lanePanel, 5u)))) {
            _pitchSettings.maximumNote = std::max<uint8_t>(
                84u, _pitchSettings.minimumNote);
            _pitchOverrides.fill(-1);
            [self setNeedsDisplay:YES];
            return;
        }
        if (NSPointInRect(point, s3g::clap_gui::cocoaRect(
                layout::sliderHitRect(contourPanel, 2u)))
            && _pitchSettings.contour != PitchContour::Fit
            && _pitchSettings.contour != PitchContour::Manual) {
            _pitchSettings.variation = 0.5f;
            _pitchOverrides.fill(-1);
            [self setNeedsDisplay:YES];
            return;
        }
        if (NSPointInRect(point, s3g::clap_gui::cocoaRect(
                layout::sliderHitRect(contourPanel, 4u)))) {
            _pitchSettings.transposeSemitones = 0;
            _pitchOverrides.fill(-1);
            _pitchStatus = @"TRANSPOSE RESET";
            [self setNeedsDisplay:YES];
            return;
        }
    }
    if (self.geometryViewMode == S3GTrackerGeometryViewModeBurst
        && [self beginBurstCanvasGestureAtPoint:point]) {
        [self.window makeFirstResponder:self];
        return;
    }
    if ([self beginSliderGestureAtPoint:point]) return;
    if (self.geometryViewMode == S3GTrackerGeometryViewModePitchMap) {
        [self.window makeFirstResponder:self];
        const NSInteger hit = [self pitchMapAssignmentAtPoint:point];
        if (hit >= 0) {
            _pitchDragAssignment = hit;
            const auto& assignment = _pitchPreview.assignments[
                static_cast<std::size_t>(hit)];
            model->session.selectedRow = assignment.row;
            [self.owner moduleSelectionChanged];
            if ([self canEditDisplayedPattern]) {
                _geometryGestureActive = YES;
                _geometryGestureChanged = NO;
                _geometrySliderGesture = NO;
                _geometryGestureKind = S3GTrackerGeometryGesturePitchPoint;
            }
        }
        return;
    }
    if (self.geometryViewMode == S3GTrackerGeometryViewModeBurst) {
        [self.window makeFirstResponder:self];
        const NSInteger hit = [self burstEventAtPoint:point];
        if (hit >= 0) {
            _selectedBurstEvent = static_cast<std::size_t>(hit);
            _geometryGestureActive = YES;
            _geometryGestureChanged = NO;
            _geometrySliderGesture = NO;
            _geometryGestureKind = S3GTrackerGeometryGestureBurstPosition;
            [self setNeedsDisplay:YES];
        }
        return;
    }
    const auto visible = visibleGeometryLanes(model);
    if (visible.count == 0u) return;
    [self.window makeFirstResponder:self];
    if (event.clickCount >= 2 && [self revealBeadAtPoint:point]) return;
    if ([self beginShapeGestureAtPoint:point]) return;
    std::size_t lane = 0u;
    std::size_t row = 0u;
    CGFloat radius = 0.0;
    if (![self geometryCellAtPoint:point lane:&lane row:&row
            radius:&radius]) return;
    const std::size_t field = self.geometryTool
            == S3GTrackerGeometryToolVelocity ? 1u : 0u;
    [self selectLane:lane row:row field:field];
    if (self.geometryViewMode != S3GTrackerGeometryViewModeRingField
        || self.geometryTool == S3GTrackerGeometryToolSelect
        || ![self canEditDisplayedPattern]) return;

    _geometryGestureActive = YES;
    _geometryGestureChanged = NO;
    _geometrySliderGesture = NO;
    _geometryGestureKind = self.geometryTool
            == S3GTrackerGeometryToolVelocity
        ? S3GTrackerGeometryGestureVelocity
        : self.geometryTool == S3GTrackerGeometryToolErase
            || (self.geometryTool == S3GTrackerGeometryToolPaint
                && (event.modifierFlags & NSEventModifierFlagOption) != 0u)
        ? S3GTrackerGeometryGestureErase
        : S3GTrackerGeometryGesturePaint;
    _lastGestureRow = static_cast<NSInteger>(row);
    _gestureLane = lane;
    _gestureRow = row;
    _velocityStartRadius = std::hypot(
        point.x - [self geometryCenter].x,
        point.y - [self geometryCenter].y);
    auto& editable = model->session.pattern.tracks[lane];
    _velocityStartValue = resolvedVelocity(editable, row);
    if (_geometryGestureKind == S3GTrackerGeometryGesturePaint) {
        _geometryGestureChanged = s3g::tracker::setGeometryHit(editable,
            row, true, s3g::tracker::laneDefaultNote(model->session, lane));
    } else if (_geometryGestureKind == S3GTrackerGeometryGestureErase) {
        _geometryGestureChanged = s3g::tracker::setGeometryHit(editable,
            row, false, s3g::tracker::laneDefaultNote(model->session, lane));
    }
    [self setNeedsDisplay:YES];
}

- (void)mouseDragged:(NSEvent*)event
{
    if (!_geometryGestureActive || !self.trackerState) return;
    const NSPoint point = [self convertPoint:event.locationInWindow fromView:nil];
    if (_geometrySliderGesture) {
        [self updateSliderGestureAtPoint:point];
        return;
    }
    if (_geometryGestureKind == S3GTrackerGeometryGesturePitchPoint) {
        [self freezePitchPreviewForManualEditing];
        [self updatePitchMapPointAtPoint:point];
        return;
    }
    if (_geometryGestureKind == S3GTrackerGeometryGestureBurstPosition) {
        [self updateBurstPositionAtPoint:point];
        return;
    }
    if (_geometryGestureKind
            == S3GTrackerGeometryGestureBurstMatrixPosition
        || _geometryGestureKind
            == S3GTrackerGeometryGestureBurstMatrixNote
        || _geometryGestureKind
            == S3GTrackerGeometryGestureBurstMatrixVelocity
        || _geometryGestureKind
            == S3GTrackerGeometryGestureBurstMatrixGate
        || _geometryGestureKind
            == S3GTrackerGeometryGestureBurstVelocityPoint) {
        [self updateBurstMatrixGestureAtPoint:point];
        return;
    }
    if (_geometryGestureKind == S3GTrackerGeometryGestureRotate
        || _geometryGestureKind == S3GTrackerGeometryGestureDensity) {
        [self updateShapeGestureAtPoint:point];
        return;
    }
    auto& pattern = self.trackerState->session.pattern;
    if (_gestureLane >= pattern.tracks.size()) return;
    auto& track = pattern.tracks[_gestureLane];
    if (_geometryGestureKind == S3GTrackerGeometryGestureVelocity) {
        const NSPoint center = [self geometryCenter];
        const CGFloat currentRadius = std::hypot(
            point.x - center.x, point.y - center.y);
        float value = std::clamp(_velocityStartValue
            + static_cast<float>((currentRadius - _velocityStartRadius)
                / 72.0), 0.0f, 1.0f);
        _geometryGestureChanged |= s3g::tracker::setGeometryVelocity(
            track, _gestureRow, value);
        [self setNeedsDisplay:YES];
        return;
    }
    std::size_t lane = 0u;
    std::size_t row = 0u;
    if (![self geometryCellAtPoint:point lane:&lane row:&row
            radius:nullptr]) return;
    if (static_cast<NSInteger>(row) == _lastGestureRow
        && lane == _gestureLane) return;
    _gestureLane = lane;
    _gestureRow = row;
    _lastGestureRow = static_cast<NSInteger>(row);
    if (lane >= pattern.tracks.size()) return;
    auto& destination = pattern.tracks[lane];
    const bool paint = _geometryGestureKind
        == S3GTrackerGeometryGesturePaint;
    _geometryGestureChanged |= s3g::tracker::setGeometryHit(destination,
        row, paint, s3g::tracker::laneDefaultNote(
            self.trackerState->session, lane));
    self.trackerState->session.selectedTrack = lane;
    self.trackerState->session.selectedRow = row;
    [self setNeedsDisplay:YES];
}

- (void)mouseUp:(NSEvent*)event
{
    (void)event;
    [self finishGeometryGesture];
}

- (void)keyDown:(NSEvent*)event
{
    auto* model = self.trackerState;
    const auto* pattern = geometryPattern(model);
    if (!model || !pattern || pattern->tracks.empty()) {
        [super keyDown:event];
        return;
    }
    const auto editingModifiers = event.modifierFlags
        & (NSEventModifierFlagCommand | NSEventModifierFlagControl
            | NSEventModifierFlagOption);
    if (editingModifiers != 0u) {
        [super keyDown:event];
        return;
    }
    NSString* key = event.charactersIgnoringModifiers.lowercaseString;
    if ([key isEqualToString:@" "]) {
        [self.owner moduleTogglePlayback];
        return;
    }
    if (self.geometryViewMode == S3GTrackerGeometryViewModeBurst) {
        auto& burst = model->session.burstLibrary.bursts[_selectedBurstSlot];
        if (!burst.empty()) {
            if (event.keyCode == 48) {
                const bool reverse = (event.modifierFlags
                    & NSEventModifierFlagShift) != 0u;
                _selectedBurstField = reverse
                    ? (_selectedBurstField + 3) % 4
                    : (_selectedBurstField + 1) % 4;
                [self setNeedsDisplay:YES];
                return;
            }
            if (event.keyCode == 125 || event.keyCode == 126) {
                if (event.keyCode == 125)
                    _selectedBurstEvent = std::min<std::size_t>(
                        _selectedBurstEvent + 1u, burst.eventCount - 1u);
                else if (_selectedBurstEvent > 0u) --_selectedBurstEvent;
                [self setNeedsDisplay:YES];
                return;
            }
            if (event.keyCode == 123 || event.keyCode == 124) {
                const int direction = event.keyCode == 123 ? -1 : 1;
                const bool coarse = (event.modifierFlags
                    & NSEventModifierFlagShift) != 0u;
                auto& authored = burst.events[_selectedBurstEvent];
                bool changed = false;
                if (_selectedBurstField == 0) {
                    const int delta = direction * (coarse ? 4096 : 1024);
                    const int minimum = _selectedBurstEvent == 0u ? 0
                        : burst.events[_selectedBurstEvent - 1u].position;
                    const int maximum = _selectedBurstEvent + 1u
                            >= burst.eventCount ? 65535
                        : burst.events[_selectedBurstEvent + 1u].position;
                    const auto value = static_cast<uint16_t>(std::clamp(
                        static_cast<int>(authored.position) + delta,
                        minimum, maximum));
                    changed = value != authored.position;
                    authored.position = value;
                } else if (_selectedBurstField == 1) {
                    const int value = std::clamp(
                        static_cast<int>(authored.note)
                            + direction * (coarse ? 12 : 1), 0, 127);
                    changed = value != authored.note;
                    authored.note = static_cast<uint8_t>(value);
                } else if (_selectedBurstField == 2) {
                    const int value = std::clamp(
                        static_cast<int>(authored.velocity)
                            + direction * (coarse ? 10 : 1), 1, 127);
                    changed = value != authored.velocity;
                    authored.velocity = static_cast<uint8_t>(value);
                } else {
                    const int value = std::clamp(
                        static_cast<int>(authored.gatePercent)
                            + direction * (coarse ? 10 : 1), 1, 100);
                    changed = value != authored.gatePercent;
                    authored.gatePercent = static_cast<uint8_t>(value);
                }
                if (changed) [self.owner modulePatternChanged];
                [self setNeedsDisplay:YES];
                return;
            }
        }
    }
    NSArray<NSString*>* toolKeys = @[ @"s", @"p", @"e", @"v" ];
    const auto toolIndex = [toolKeys indexOfObject:key];
    if (self.geometryViewMode == S3GTrackerGeometryViewModeRingField
        && toolIndex != NSNotFound
        && toolIndex < self.toolButtons.count
        && self.toolButtons[toolIndex].enabled) {
        [self toolChanged:self.toolButtons[toolIndex]];
        return;
    }
    if ([key isEqualToString:@"-"]) {
        [self setGeometryZoomAndRedraw:self.geometryZoom / 1.15];
        return;
    }
    if ([key isEqualToString:@"+"] || [key isEqualToString:@"="]) {
        [self setGeometryZoomAndRedraw:self.geometryZoom * 1.15];
        return;
    }
    const auto visible = visibleGeometryLanes(model);
    if (visible.count == 0u) {
        [super keyDown:event];
        return;
    }
    auto selectedOrdinal = visible.count;
    for (std::size_t ordinal = 0u; ordinal < visible.count; ++ordinal) {
        if (visible.indices[ordinal] == model->session.selectedTrack) {
            selectedOrdinal = ordinal;
            break;
        }
    }
    if (event.keyCode == 123 || event.keyCode == 126) {
        if (selectedOrdinal == visible.count) selectedOrdinal = 0u;
        else if (selectedOrdinal > 0u) --selectedOrdinal;
        [self selectLane:visible.indices[selectedOrdinal]];
        return;
    }
    if (event.keyCode == 124 || event.keyCode == 125) {
        if (selectedOrdinal == visible.count) selectedOrdinal = 0u;
        else selectedOrdinal = std::min(selectedOrdinal + 1u,
            visible.count - 1u);
        [self selectLane:visible.indices[selectedOrdinal]];
        return;
    }
    [super keyDown:event];
}

- (void)drawLegacyGeometry:(NSRect)dirtyRect
{
    (void)dirtyRect;
    fillRect(self.bounds, trackerColor(0x1d1d1d));
    strokeRect(NSInsetRect(self.bounds, 0.5, 0.5), trackerColor(0x565656));
    fillRect(NSMakeRect(0.0, 0.0, NSWidth(self.bounds), 24.0),
        trackerColor(0x131313));
    fillRect(NSMakeRect(0.0, 0.0, NSWidth(self.bounds), 2.0),
        trackerColor(0xb8b8b8));
    const NSRect plotRect = NSMakeRect(8.0, kGeometryPlotTop,
        std::max<CGFloat>(0.0, NSWidth(self.bounds) - 128.0),
        std::max<CGFloat>(0.0,
            NSHeight(self.bounds) - kGeometryPlotTop - 8.0));
    fillRect(plotRect, trackerColor(0x0c0c0c));
    strokeRect(NSInsetRect(plotRect, 0.5, 0.5), trackerColor(0x383838));
    auto* model = self.trackerState;
    const auto* pattern = geometryPattern(model);
    NSString* title = pattern
        ? [NSString stringWithFormat:@"RHYTHM GEOMETRY  /  NOTE  •  %@ · %@",
            [self displayedPatternId], nsString(pattern->name)]
        : @"RHYTHM GEOMETRY  /  NOTE";
    drawText(title, NSMakeRect(8.0, 6.0,
        std::max<CGFloat>(1.0, NSWidth(self.bounds) - 140.0), 16.0),
        trackerColor(0xa8a8a8), 9.5);
    drawText(@"VIEW", NSMakeRect(12.0, 34.0, 32.0, 14.0),
        trackerColor(0x858585), 7.5, NSFontWeightMedium);
    const std::array<NSRect, 3u> zoomRects {{
        [self zoomOutRect], [self zoomResetRect], [self zoomInRect],
    }};
    NSArray<NSString*>* zoomLabels = @[
        @"−", [NSString stringWithFormat:@"%d%%",
            static_cast<int>(std::lround(self.geometryZoom * 100.0))], @"+",
    ];
    for (std::size_t index = 0u; index < zoomRects.size(); ++index) {
        fillRect(zoomRects[index], trackerColor(0x2d3032));
        strokeRect(NSInsetRect(zoomRects[index], 0.5, 0.5),
            trackerColor(0x666b6f));
        drawText(zoomLabels[index], NSInsetRect(zoomRects[index], 2.0, 2.0),
            trackerColor(0xc2c6c8), index == 1u ? 7.5 : 10.0,
            NSFontWeightMedium, NSTextAlignmentCenter);
    }
    if (!model || !pattern || pattern->tracks.empty()) return;
    _lastDisplayedPatternId = geometryPatternId(model);
    _lastDisplayedSongMuteMask = model->songPlaybackActive
        ? model->songPlaybackMutedTracks : 0u;
    const auto visible = visibleGeometryLanes(model);
    if (visible.count == 0u) {
        drawText(@"ALL NOTE LANES MUTED", NSMakeRect(
            NSMinX(plotRect) + 14.0, NSMidY(plotRect) - 7.0,
            NSWidth(plotRect) - 28.0, 14.0), trackerColor(0x6f6f6f),
            9.0, NSFontWeightMedium, NSTextAlignmentCenter);
        return;
    }
    const NSPoint center = [self geometryCenter];
    const CGFloat cx = center.x;
    const CGFloat cy = center.y;
    const CGFloat maximum = [self geometryMaximumRadius];
    std::size_t focusLane = visible.indices[0u];
    for (std::size_t ordinal = 0u; ordinal < visible.count; ++ordinal) {
        if (visible.indices[ordinal] == model->session.selectedTrack) {
            focusLane = visible.indices[ordinal];
            break;
        }
    }
    const bool laneFocusMode = self.geometryViewMode
        == S3GTrackerGeometryViewModeLaneFocus;
    const bool compositeMode = self.geometryViewMode
        == S3GTrackerGeometryViewModeCompositeRing;
    const CGFloat normalizedRadius = maximum * 0.72;
    if (compositeMode) {
        NSBezierPath* referenceRing = [NSBezierPath bezierPathWithOvalInRect:
            NSMakeRect(cx - normalizedRadius, cy - normalizedRadius,
                normalizedRadius * 2.0, normalizedRadius * 2.0)];
        referenceRing.lineWidth = 1.15;
        [trackerColor(0xbfc3c5, 0.42) setStroke];
        [referenceRing stroke];
        fillRect(NSMakeRect(cx - 1.5, cy - normalizedRadius - 1.5,
            3.0, 3.0), trackerColor(0xd0d3d4, 0.72));
    }

    for (std::size_t ordinal = visible.count; ordinal-- > 0u;) {
        const auto lane = visible.indices[ordinal];
        const auto& track = pattern->tracks[lane];
        const auto length = std::clamp<std::size_t>(
            track.noteColumn.length, 1u, 256u);
        const CGFloat regularRadius = [self ringRadiusForOrdinal:ordinal
            count:visible.count];
        const bool selected = lane == focusLane;
        const CGFloat radius = compositeMode
            ? normalizedRadius
            : laneFocusMode && selected ? normalizedRadius : regularRadius;
        const CGFloat alpha = selected ? 1.0
            : laneFocusMode ? 0.14 : compositeMode ? 0.64 : 0.76;
        NSColor* laneColor = trackerColor(
            kLaneColors[lane % kLaneColors.size()], alpha);
        NSColor* legendColor = trackerColor(
            kLaneColors[lane % kLaneColors.size()],
            selected ? 1.0 : laneFocusMode ? 0.42 : 0.76);
        const bool drawAllSteps = self.geometryViewMode
                == S3GTrackerGeometryViewModeAllStepsUnderlay
            || (laneFocusMode && selected);
        if (drawAllSteps) {
            NSBezierPath* allSteps = [NSBezierPath bezierPath];
            NSBezierPath* stepPoints = [NSBezierPath bezierPath];
            for (std::size_t step = 0u; step < length; ++step) {
                const CGFloat angle = -static_cast<CGFloat>(M_PI_2)
                    + static_cast<CGFloat>(step) * 2.0
                        * static_cast<CGFloat>(M_PI)
                        / static_cast<CGFloat>(length);
                const NSPoint point = NSMakePoint(
                    cx + std::cos(angle) * radius,
                    cy + std::sin(angle) * radius);
                if (step == 0u) [allSteps moveToPoint:point];
                else [allSteps lineToPoint:point];
                const CGFloat pointRadius = selected ? 1.5 : 1.2;
                [stepPoints appendBezierPathWithOvalInRect:NSMakeRect(
                    point.x - pointRadius, point.y - pointRadius,
                    pointRadius * 2.0, pointRadius * 2.0)];
            }
            [allSteps closePath];
            allSteps.lineWidth = selected ? 1.6 : 1.3;
            allSteps.lineJoinStyle = NSLineJoinStyleRound;
            NSColor* reference = trackerColor(0xbfc3c5,
                selected ? 0.68 : 0.48);
            [reference setStroke];
            [allSteps stroke];
            [reference setFill];
            [stepPoints fill];
        }
        NSBezierPath* polygon = [NSBezierPath bezierPath];
        bool started = false;
        for (std::size_t step = 0u; step < length; ++step) {
            const CGFloat angle = -static_cast<CGFloat>(M_PI_2)
                + static_cast<CGFloat>(step) * 2.0 * static_cast<CGFloat>(M_PI)
                    / static_cast<CGFloat>(length);
            const CGFloat x = cx + std::cos(angle) * radius;
            const CGFloat y = cy + std::sin(angle) * radius;
            const bool hit = step < track.notes.size()
                && noteCellIsActivePulse(track.notes[step]);
            if (hit) {
                if (!started) { [polygon moveToPoint:NSMakePoint(x, y)]; started = true; }
                else [polygon lineToPoint:NSMakePoint(x, y)];
            }
        }
        if (started) {
            [polygon closePath];
            polygon.lineWidth = selected ? 2.0
                : laneFocusMode ? 0.75 : 1.15;
            polygon.lineJoinStyle = NSLineJoinStyleRound;
            polygon.lineCapStyle = NSLineCapStyleRound;
            [laneColor setStroke];
            [polygon stroke];
        }

        const CGFloat legendY = kGeometryLegendTop
            + static_cast<CGFloat>(ordinal) * 18.0;
        drawText(nsString(track.name), NSMakeRect(NSWidth(self.bounds) - 108.0,
            legendY, 67.0, 14.0), legendColor, 7.5,
            selected ? NSFontWeightBold : NSFontWeightRegular);
        std::size_t hits = 0u;
        for (std::size_t row = 0u; row < length; ++row)
            if (row < track.notes.size()
                && noteCellIsActivePulse(track.notes[row])) ++hits;
        drawText([NSString stringWithFormat:@"%lu/%lu%@",
            static_cast<unsigned long>(hits),
            static_cast<unsigned long>(length),
            directionMark(track.noteColumn.direction)],
            NSMakeRect(NSWidth(self.bounds) - 42.0, legendY, 36.0, 14.0),
            legendColor, 7.5, NSFontWeightRegular, NSTextAlignmentRight);
    }
}

- (NSUInteger)allStepsUnderlayNodeCount
{
    if (self.geometryViewMode
        != S3GTrackerGeometryViewModeAllStepsUnderlay) return 0u;
    const auto* pattern = geometryPattern(self.trackerState);
    const auto visible = visibleGeometryLanes(self.trackerState);
    if (!pattern) return 0u;
    std::size_t count = 0u;
    for (std::size_t ordinal = 0u; ordinal < visible.count; ++ordinal) {
        const auto lane = visible.indices[ordinal];
        if (lane >= pattern->tracks.size()) continue;
        count += std::clamp<std::size_t>(
            pattern->tracks[lane].noteColumn.length, 1u, 256u);
    }
    return static_cast<NSUInteger>(count);
}

- (void)drawBurstWorkspace
{
    auto* model = self.trackerState;
    if (!model) return;
    const auto style = s3g::clap_gui::softTextStyle();
    NSDictionary* labels = s3g::clap_gui::softLabelAttrs();
    NSDictionary* values = s3g::clap_gui::softValueAttrs();
    const auto geometry = [self geometryLayout];
    const NSRect canvas = [self canvasRect];
    auto& pattern = model->session.pattern;
    auto& burst = model->session.burstLibrary.bursts[_selectedBurstSlot];
    _selectedBurstEvent = burst.empty() ? 0u
        : std::min<std::size_t>(_selectedBurstEvent, burst.eventCount - 1u);

    const NSRect matrix = [self burstMatrixRect];
    const NSRect overview = [self burstOverviewRect];
    const NSRect breakpoints = [self burstBreakpointRect];
    fillRect(matrix, S3GTrackerThemeColor(S3GTrackerThemeRole::Canvas, 0.72));
    strokeRect(NSInsetRect(matrix, 0.5, 0.5),
        S3GTrackerThemeColor(S3GTrackerThemeRole::Border));
    const NSArray<NSString*>* matrixHeaders = @[
        @"#", @"TIME", @"NOTE", @"VELOCITY", @"GATE % ROW"
    ];
    const std::array<NSInteger, 5u> headerFields {{ -1, 0, 1, 2, 3 }};
    for (std::size_t index = 0u; index < headerFields.size(); ++index) {
        NSRect header = [self burstMatrixCellRect:0u
            field:headerFields[index]];
        header.origin.y = NSMinY(matrix);
        header.size.height = 22.0;
        drawCenteredText(matrixHeaders[index], NSInsetRect(header, 4.0, 0.0),
            S3GTrackerThemeColor(S3GTrackerThemeRole::TextMuted), 6.8,
            NSFontWeightSemibold, index == 0u
                ? NSTextAlignmentCenter : NSTextAlignmentLeft);
    }
    for (std::size_t row = 0u; row < kMaximumBurstEvents; ++row) {
        const NSRect rowRect = [self burstMatrixRowRect:row];
        const bool active = row < burst.eventCount;
        const bool selected = active && row == _selectedBurstEvent;
        fillRect(rowRect, S3GTrackerThemeColor(selected
                ? S3GTrackerThemeRole::Selection
                : row % 2u == 0u ? S3GTrackerThemeRole::Raised
                                  : S3GTrackerThemeRole::Canvas,
            selected ? 0.40 : 0.66));
        strokeRect(NSInsetRect(rowRect, 0.5, 0.5),
            S3GTrackerThemeColor(S3GTrackerThemeRole::Grid, 0.72));
        drawCenteredText([NSString stringWithFormat:@"%02lu",
                static_cast<unsigned long>(row + 1u)],
            [self burstMatrixCellRect:row field:-1],
            S3GTrackerThemeColor(selected ? S3GTrackerThemeRole::TextPrimary
                                          : S3GTrackerThemeRole::TextFaint),
            7.4, selected ? NSFontWeightBold : NSFontWeightMedium);
        for (NSInteger field = 0; field < 4; ++field) {
            const NSRect cell = [self burstMatrixCellRect:row field:field];
            strokeRect(NSInsetRect(cell, 0.5, 0.5),
                S3GTrackerThemeColor(S3GTrackerThemeRole::Grid, 0.48));
            if (selected && field == _selectedBurstField)
                strokeRect(NSInsetRect(cell, 1.5, 1.5),
                    S3GTrackerThemeColor(S3GTrackerThemeRole::Selection), 1.4);
        }
        if (!active) {
            drawCenteredText(row == burst.eventCount ? @"+ ADD EVENT" : @"—",
                NSMakeRect(NSMinX(rowRect) + 38.0, NSMinY(rowRect),
                    NSWidth(rowRect) - 38.0, NSHeight(rowRect)),
                S3GTrackerThemeColor(S3GTrackerThemeRole::TextFaint), 7.0,
                NSFontWeightMedium);
            continue;
        }
        const auto& authored = burst.events[row];
        const CGFloat time = static_cast<CGFloat>(authored.position)
            / 65535.0;
        const std::array<NSString*, 4u> cellText {{
            [NSString stringWithFormat:@"%05.1f%%", time * 100.0],
            [NSString stringWithFormat:@"%@ · %03u",
                midiNoteName(authored.note), authored.note],
            [NSString stringWithFormat:@"%03u", authored.velocity],
            [NSString stringWithFormat:@"%03u%%", authored.gatePercent],
        }};
        const CGFloat velocityFraction = static_cast<CGFloat>(
            authored.velocity) / 127.0;
        const CGFloat gateFraction = static_cast<CGFloat>(
            authored.gatePercent) / 100.0;
        const CGFloat noteFraction = static_cast<CGFloat>(
            authored.note) / 127.0;
        const std::array<CGFloat, 4u> sliderFractions {{
            time, noteFraction, velocityFraction, gateFraction,
        }};
        for (NSInteger field = 0; field < 4; ++field) {
            const NSRect cell = [self burstMatrixCellRect:row field:field];
            NSColor* color = field == 1
                ? trackerColor(kLaneColors[authored.note % kLaneColors.size()])
                : field == 0
                    ? S3GTrackerThemeColor(S3GTrackerThemeRole::Focus)
                    : field == 2
                        ? S3GTrackerThemeColor(S3GTrackerThemeRole::Value)
                        : S3GTrackerThemeColor(
                            S3GTrackerThemeRole::TextSecondary);
            const NSRect miniTrack = NSMakeRect(NSMinX(cell) + 4.0,
                NSMaxY(cell) - 6.0, NSWidth(cell) - 8.0, 2.5);
            fillRect(miniTrack,
                S3GTrackerThemeColor(S3GTrackerThemeRole::Control, 0.96));
            fillRect(NSMakeRect(NSMinX(miniTrack), NSMinY(miniTrack),
                    std::max<CGFloat>(1.0, NSWidth(miniTrack)
                        * sliderFractions[static_cast<std::size_t>(field)]),
                    NSHeight(miniTrack)),
                [color colorWithAlphaComponent:selected ? 1.0 : 0.88]);
            drawCenteredText(cellText[static_cast<std::size_t>(field)],
                NSInsetRect(cell, 7.0, 0.0), color, 8.0,
                selected ? NSFontWeightSemibold : NSFontWeightMedium,
                NSTextAlignmentLeft);
        }
    }

    fillRect(overview,
        S3GTrackerThemeColor(S3GTrackerThemeRole::Canvas, 0.72));
    strokeRect(NSInsetRect(overview, 0.5, 0.5),
        S3GTrackerThemeColor(S3GTrackerThemeRole::Border));
    drawText(@"RADIAL OVERVIEW", NSMakeRect(NSMinX(overview) + 8.0,
            NSMinY(overview) + 7.0, NSWidth(overview) - 16.0, 11.0),
        S3GTrackerThemeColor(S3GTrackerThemeRole::TextMuted), 6.8,
        NSFontWeightSemibold);
    const NSRect radialPlot = [self burstRadialPlotRect];
    const NSPoint center = NSMakePoint(NSMidX(radialPlot),
        NSMidY(radialPlot));
    const CGFloat radius = std::max<CGFloat>(50.0,
        std::min(NSWidth(radialPlot), NSHeight(radialPlot))
            * 0.34 * self.geometryZoom);
    for (std::size_t index = 0u; index < 16u; ++index) {
        const CGFloat angle = -static_cast<CGFloat>(M_PI_2)
            + static_cast<CGFloat>(index) * 2.0 * static_cast<CGFloat>(M_PI)
                / 16.0;
        NSBezierPath* spoke = [NSBezierPath bezierPath];
        [spoke moveToPoint:NSMakePoint(center.x + std::cos(angle)
                * (radius - 8.0), center.y + std::sin(angle)
                * (radius - 8.0))];
        [spoke lineToPoint:NSMakePoint(center.x + std::cos(angle)
                * (radius + (index % 4u == 0u ? 9.0 : 4.0)),
            center.y + std::sin(angle)
                * (radius + (index % 4u == 0u ? 9.0 : 4.0)))];
        spoke.lineWidth = index % 4u == 0u ? 1.0 : 0.55;
        [S3GTrackerThemeColor(S3GTrackerThemeRole::Grid,
            index % 4u == 0u ? 0.90 : 0.55) setStroke];
        [spoke stroke];
    }
    NSBezierPath* ring = [NSBezierPath bezierPathWithOvalInRect:NSMakeRect(
        center.x - radius, center.y - radius, radius * 2.0, radius * 2.0)];
    ring.lineWidth = 1.2;
    [S3GTrackerThemeColor(S3GTrackerThemeRole::BorderStrong) setStroke];
    [ring stroke];
    if (burst.empty()) {
        drawCenteredText(@"SELECT AN EMPTY MATRIX ROW TO CREATE", radialPlot,
            S3GTrackerThemeColor(S3GTrackerThemeRole::TextFaint), 11.0,
            NSFontWeightMedium);
    } else {
        NSBezierPath* phrase = [NSBezierPath bezierPath];
        for (std::size_t index = 0u; index < burst.eventCount; ++index) {
            const auto& event = burst.events[index];
            const CGFloat phase = static_cast<CGFloat>(event.position)
                / 65536.0;
            const CGFloat angle = -static_cast<CGFloat>(M_PI_2)
                + phase * 2.0 * static_cast<CGFloat>(M_PI);
            const CGFloat eventRadius = radius
                + (static_cast<CGFloat>(event.note) / 127.0 - 0.5) * 30.0;
            const NSPoint point = NSMakePoint(center.x + std::cos(angle)
                    * eventRadius, center.y + std::sin(angle) * eventRadius);
            if (index == 0u) [phrase moveToPoint:point];
            else [phrase lineToPoint:point];
            const CGFloat bead = 3.4
                + static_cast<CGFloat>(event.velocity) / 127.0 * 4.2;
            NSBezierPath* marker = [NSBezierPath bezierPathWithOvalInRect:
                NSMakeRect(point.x - bead, point.y - bead,
                    bead * 2.0, bead * 2.0)];
            NSColor* identity = trackerColor(
                kLaneColors[event.note % kLaneColors.size()], 1.0);
            [S3GTrackerThemeColor(S3GTrackerThemeRole::Canvas, 0.94) setFill];
            [[NSBezierPath bezierPathWithOvalInRect:
                NSInsetRect(marker.bounds, -1.8, -1.8)] fill];
            [identity setFill];
            [marker fill];
            if (index == _selectedBurstEvent) {
                NSBezierPath* halo = [NSBezierPath bezierPathWithOvalInRect:
                    NSInsetRect(marker.bounds, -4.0, -4.0)];
                halo.lineWidth = 1.4;
                [S3GTrackerThemeColor(S3GTrackerThemeRole::TextPrimary)
                    setStroke];
                [halo stroke];
            }
            drawCenteredText([NSString stringWithFormat:@"%lu",
                    static_cast<unsigned long>(index + 1u)],
                NSMakeRect(point.x - 8.0, point.y - 7.0, 16.0, 14.0),
                S3GTrackerThemeColor(S3GTrackerThemeRole::Canvas), 6.4,
                NSFontWeightBold);
        }
        phrase.lineWidth = 1.2;
        [S3GTrackerThemeColor(S3GTrackerThemeRole::TextMuted, 0.65)
            setStroke];
        [phrase stroke];
        drawCenteredText(nsString(burst.name), NSMakeRect(
                NSMinX(radialPlot) + 12.0, NSMaxY(radialPlot) - 22.0,
                NSWidth(radialPlot) - 24.0, 16.0),
            S3GTrackerThemeColor(S3GTrackerThemeRole::TextSecondary), 9.0,
            NSFontWeightSemibold);
    }

    fillRect(breakpoints,
        S3GTrackerThemeColor(S3GTrackerThemeRole::Canvas, 0.72));
    strokeRect(NSInsetRect(breakpoints, 0.5, 0.5),
        S3GTrackerThemeColor(S3GTrackerThemeRole::Border));
    drawText(@"VELOCITY BREAKPOINTS", NSMakeRect(NSMinX(breakpoints) + 9.0,
            NSMinY(breakpoints) + 7.0, 190.0, 11.0),
        S3GTrackerThemeColor(S3GTrackerThemeRole::TextMuted), 6.8,
        NSFontWeightSemibold);
    drawText(@"DRAG POINT = TIME + VELOCITY", NSMakeRect(
            NSMaxX(breakpoints) - 230.0, NSMinY(breakpoints) + 7.0,
            220.0, 11.0),
        S3GTrackerThemeColor(S3GTrackerThemeRole::TextFaint), 6.5,
        NSFontWeightMedium, NSTextAlignmentRight);
    const NSRect graph = NSInsetRect(breakpoints, 12.0, 14.0);
    for (std::size_t division = 0u; division <= 4u; ++division) {
        const CGFloat x = NSMinX(graph) + NSWidth(graph)
            * static_cast<CGFloat>(division) / 4.0;
        NSBezierPath* line = [NSBezierPath bezierPath];
        [line moveToPoint:NSMakePoint(x, NSMinY(graph) + 10.0)];
        [line lineToPoint:NSMakePoint(x, NSMaxY(graph))];
        line.lineWidth = division == 0u || division == 4u ? 1.0 : 0.6;
        [S3GTrackerThemeColor(S3GTrackerThemeRole::Grid, 0.72) setStroke];
        [line stroke];
    }
    for (std::size_t division = 0u; division <= 4u; ++division) {
        const CGFloat y = NSMinY(graph) + 10.0 + (NSHeight(graph) - 10.0)
            * static_cast<CGFloat>(division) / 4.0;
        NSBezierPath* line = [NSBezierPath bezierPath];
        [line moveToPoint:NSMakePoint(NSMinX(graph), y)];
        [line lineToPoint:NSMakePoint(NSMaxX(graph), y)];
        line.lineWidth = 0.6;
        [S3GTrackerThemeColor(S3GTrackerThemeRole::Grid, 0.55) setStroke];
        [line stroke];
    }
    if (!burst.empty()) {
        NSBezierPath* velocityPath = [NSBezierPath bezierPath];
        NSBezierPath* pitchPath = [NSBezierPath bezierPath];
        for (std::size_t index = 0u; index < burst.eventCount; ++index) {
            const auto& authored = burst.events[index];
            const CGFloat x = NSMinX(graph)
                + static_cast<CGFloat>(authored.position) / 65535.0
                    * NSWidth(graph);
            const CGFloat velocityY = NSMaxY(graph)
                - static_cast<CGFloat>(authored.velocity - 1u) / 126.0
                    * NSHeight(graph);
            const CGFloat gateEnd = x
                + static_cast<CGFloat>(authored.gatePercent) / 100.0
                    * NSWidth(graph);
            NSBezierPath* gateSpan = [NSBezierPath bezierPath];
            [gateSpan moveToPoint:NSMakePoint(x, velocityY)];
            [gateSpan lineToPoint:NSMakePoint(
                std::min(NSMaxX(graph), gateEnd), velocityY)];
            gateSpan.lineWidth = 3.0;
            [S3GTrackerThemeColor(S3GTrackerThemeRole::TextSecondary, 0.25)
                setStroke];
            [gateSpan stroke];
            if (gateEnd > NSMaxX(graph)) {
                drawText(@"›", NSMakeRect(NSMaxX(graph) - 7.0,
                        velocityY - 7.0, 8.0, 14.0),
                    S3GTrackerThemeColor(S3GTrackerThemeRole::Warning),
                    8.0, NSFontWeightBold, NSTextAlignmentRight);
            }
            const CGFloat pitchY = NSMaxY(graph)
                - static_cast<CGFloat>(authored.note) / 127.0
                    * NSHeight(graph);
            if (index == 0u) {
                [velocityPath moveToPoint:NSMakePoint(x, velocityY)];
                [pitchPath moveToPoint:NSMakePoint(x, pitchY)];
            } else {
                [velocityPath lineToPoint:NSMakePoint(x, velocityY)];
                [pitchPath lineToPoint:NSMakePoint(x, pitchY)];
            }
        }
        pitchPath.lineWidth = 0.8;
        [S3GTrackerThemeColor(S3GTrackerThemeRole::Note, 0.34) setStroke];
        [pitchPath stroke];
        velocityPath.lineWidth = 1.6;
        [S3GTrackerThemeColor(S3GTrackerThemeRole::Value, 0.82) setStroke];
        [velocityPath stroke];
        for (std::size_t index = 0u; index < burst.eventCount; ++index) {
            const auto& authored = burst.events[index];
            const CGFloat x = NSMinX(graph)
                + static_cast<CGFloat>(authored.position) / 65535.0
                    * NSWidth(graph);
            const CGFloat y = NSMaxY(graph)
                - static_cast<CGFloat>(authored.velocity - 1u) / 126.0
                    * NSHeight(graph);
            const CGFloat radius = index == _selectedBurstEvent ? 5.5 : 4.0;
            NSBezierPath* point = [NSBezierPath bezierPathWithOvalInRect:
                NSMakeRect(x - radius, y - radius, radius * 2.0,
                    radius * 2.0)];
            [S3GTrackerThemeColor(S3GTrackerThemeRole::Canvas) setFill];
            [point fill];
            point.lineWidth = index == _selectedBurstEvent ? 2.0 : 1.2;
            [S3GTrackerThemeColor(index == _selectedBurstEvent
                    ? S3GTrackerThemeRole::TextPrimary
                    : S3GTrackerThemeRole::Value) setStroke];
            [point stroke];
        }
    }
    NSArray<NSString*>* timeLabels = @[ @"0", @"1/4", @"1/2", @"3/4", @"1X" ];
    for (std::size_t index = 0u; index < timeLabels.count; ++index) {
        const CGFloat x = NSMinX(graph) + NSWidth(graph)
            * static_cast<CGFloat>(index) / 4.0;
        drawCenteredText(timeLabels[index], NSMakeRect(x - 16.0,
                NSMaxY(graph) - 4.0, 32.0, 12.0),
            S3GTrackerThemeColor(S3GTrackerThemeRole::TextFaint), 6.2,
            NSFontWeightMedium);
    }
    drawCenteredText(@"DRAG CELLS  •  ↑/↓ EVENT  •  TAB FIELD  •  GATE = % OF ROW  •  PALE TAIL = DURATION  •  › SPILLS",
        NSMakeRect(NSMinX(canvas) + 10.0, NSMaxY(canvas) - 28.0,
            NSWidth(canvas) - 20.0, 12.0),
        S3GTrackerThemeColor(S3GTrackerThemeRole::TextFaint), 6.8,
        NSFontWeightMedium);

    const CGFloat libraryX = geometry.laneCycle.frame.x;
    const CGFloat libraryWidth = geometry.laneCycle.frame.width;
    const auto drawBurstInfo = [&](NSString* name, NSString* value,
                                   const layout::Panel& toolbox,
                                   uint32_t row, NSColor* infoColor) {
        const CGFloat x = static_cast<CGFloat>(toolbox.frame.x);
        const CGFloat width = static_cast<CGFloat>(toolbox.frame.width);
        const CGFloat y = static_cast<CGFloat>(layout::rowY(toolbox, row));
        const CGFloat labelX = static_cast<CGFloat>(
            layout::processorLabelX(x));
        const CGFloat controlX = static_cast<CGFloat>(
            layout::processorControlX(x));
        const CGFloat controlWidth = static_cast<CGFloat>(
            layout::processorMenuWidth(width));
        [[name uppercaseString] drawAtPoint:NSMakePoint(labelX, y - 2.0)
            withAttributes:labels];
        NSColor* color = infoColor ? infoColor
            : S3GTrackerThemeColor(S3GTrackerThemeRole::TextMuted);
        NSDictionary* infoAttrs = @{
            NSForegroundColorAttributeName: color,
            NSFontAttributeName: s3g::clap_gui::uiFont(10.0),
        };
        [color setFill];
        NSRectFill(NSMakeRect(controlX, y, 2.0, 12.0));
        NSString* display = s3g::clap_gui::menuDisplayText(value,
            std::max<CGFloat>(0.0, controlWidth - 12.0), infoAttrs);
        [display drawAtPoint:NSMakePoint(controlX + 8.0, y - 2.0)
            withAttributes:infoAttrs];
    };
    drawTrackerProcessorMenu(@"BURST",
        [NSString stringWithFormat:@"%@  ·  %@",
            nsString(burstSlotToken(_selectedBurstSlot)),
            burst.empty() ? @"EMPTY" : nsString(burst.name)],
        layout::rowY(geometry.laneCycle, 0u), libraryX, libraryWidth,
        labels, values, style);
    drawTrackerProcessorMenu(@"NAME", burst.empty() ? @"—" : nsString(burst.name),
        layout::rowY(geometry.laneCycle, 1u), libraryX, libraryWidth,
        labels, values, style);
    [@"EVENTS" drawAtPoint:NSMakePoint(layout::processorLabelX(libraryX),
        layout::rowY(geometry.laneCycle, 2u) - 2.0) withAttributes:labels];
    for (NSUInteger index = 0u; index < 2u; ++index)
        S3GTrackerDrawSuiteActionButton(
            [self burstActionRectForRow:2u index:index count:2u],
            index == 0u
                ? [NSString stringWithFormat:@"−  %u", burst.eventCount]
                : [NSString stringWithFormat:@"%u  +", burst.eventCount],
            YES, NO, NO, NO, NO, NO, NO, YES);
    drawBurstInfo(@"USAGE", [NSString stringWithFormat:@"%lu NOTE CELLS",
            static_cast<unsigned long>(projectBurstUsageCount(
                *self.trackerState, _selectedBurstSlot))],
        geometry.laneCycle, 3u,
        S3GTrackerThemeColor(S3GTrackerThemeRole::TextMuted));
    const NSArray<NSArray<NSString*>*>* actionRows = @[
        @[ @"NEW", @"DUP", @"DELETE" ],
        @[ @"EVEN", @"ACCEL", @"DECEL" ],
        @[ @"REVERSE", @"ROT <", @"ROT >" ],
        @[ @"IMPORT PACK", @"EXPORT PACK" ],
    ];
    for (NSUInteger rowIndex = 0u; rowIndex < actionRows.count; ++rowIndex) {
        const auto row = static_cast<uint32_t>(4u + rowIndex);
        const auto rowLabels = actionRows[rowIndex];
        for (NSUInteger index = 0u; index < rowLabels.count; ++index) {
            const BOOL danger = rowIndex == 0u && index == 2u;
            S3GTrackerDrawSuiteActionButton(
                [self burstActionRectForRow:row index:index
                    count:rowLabels.count], rowLabels[index],
                YES, NO, NO, NO, NO, NO, danger, YES);
        }
    }

    const CGFloat subX = geometry.editShape.frame.x;
    const CGFloat subWidth = geometry.editShape.frame.width;
    s3g::clap_gui::drawToolboxHeaderActionButton(
        [self fitBurstGatesHeaderButtonRect], [self editPanelRect],
        @"FIT GATES TO ROW", values, style);
    NSDictionary* previewAttrs = self.trackerState->playing || burst.empty()
        ? labels : values;
    const NSRect previewButton = [self burstPreviewHeaderButtonRect];
    s3g::clap_gui::drawToolboxHeaderActionButton(previewButton,
        [self editPanelRect], @"PREVIEW ▶", previewAttrs, style);
    if (_burstPreviewFeedbackActive) {
        fillRect(NSInsetRect(previewButton, 1.0, 1.0),
            S3GTrackerThemeColor(S3GTrackerThemeRole::Success, 0.28));
        strokeRect(NSInsetRect(previewButton, 0.5, 0.5),
            S3GTrackerThemeColor(S3GTrackerThemeRole::Success), 1.25);
        drawCenteredText(@"PLAYING", previewButton,
            S3GTrackerThemeColor(S3GTrackerThemeRole::TextPrimary), 6.7,
            NSFontWeightSemibold);
    }
    NSString* eventTitle = burst.empty() ? @"—" : [NSString stringWithFormat:
        @"STEP %lu OF %u", static_cast<unsigned long>(_selectedBurstEvent + 1u),
        static_cast<unsigned int>(burst.eventCount)];
    drawTrackerProcessorMenu(@"SUBSTEP", eventTitle,
        layout::rowY(geometry.editShape, 0u), subX, subWidth,
        labels, values, style);
    const BurstEvent event = burst.empty() ? BurstEvent {}
        : burst.events[_selectedBurstEvent];
    s3g::clap_gui::drawProcessorSliderWithValueWidth(@"NOTE",
        [NSString stringWithFormat:@"%@ · %03u", midiNoteName(event.note),
            static_cast<unsigned int>(event.note)],
        static_cast<CGFloat>(event.note) / 127.0,
        layout::rowY(geometry.editShape, 1u), subX, subWidth, 72.0,
        labels, values, style);
    s3g::clap_gui::drawProcessorSliderWithValueWidth(@"VELOCITY",
        [NSString stringWithFormat:@"%03u", event.velocity],
        static_cast<CGFloat>(event.velocity) / 127.0,
        layout::rowY(geometry.editShape, 2u), subX, subWidth, 72.0,
        labels, values, style);
    s3g::clap_gui::drawProcessorSliderWithValueWidth(@"GATE / ROW",
        [NSString stringWithFormat:@"%03u%%", event.gatePercent],
        static_cast<CGFloat>(event.gatePercent) / 100.0,
        layout::rowY(geometry.editShape, 3u), subX, subWidth, 72.0,
        labels, values, style);

    if (!self.burstLibraryOnly) {
        NSString* selectedViewTitle = self.viewModePopup.titleOfSelectedItem;
        drawTrackerProcessorMenu(@"MODE",
            selectedViewTitle ? selectedViewTitle : @"BURST EDITOR",
            layout::rowY(geometry.view, 0u), geometry.view.frame.x,
            geometry.view.frame.width, labels, values, style);
    }
    const NSRect placeButton = [self revealHeaderButtonRect];
    s3g::clap_gui::drawToolboxHeaderActionButton(placeButton,
        [self bridgePanelRect], @"PLACE IN TRACKER", values, style);
    if (_burstPlaceFeedbackActive) {
        fillRect(NSInsetRect(placeButton, 1.0, 1.0),
            S3GTrackerThemeColor(S3GTrackerThemeRole::Success, 0.28));
        strokeRect(NSInsetRect(placeButton, 0.5, 0.5),
            S3GTrackerThemeColor(S3GTrackerThemeRole::Success), 1.25);
        drawCenteredText(@"PLACED ✓", placeButton,
            S3GTrackerThemeColor(S3GTrackerThemeRole::TextPrimary), 7.0,
            NSFontWeightSemibold);
    }
    const auto lane = pattern.tracks.empty() ? 0u
        : std::min(model->session.selectedTrack, pattern.tracks.size() - 1u);
    drawBurstInfo(@"TARGET", pattern.tracks.empty() ? @"NO LANES"
            : [NSString stringWithFormat:@"T%02lu · ROW %03lu",
                static_cast<unsigned long>(lane + 1u),
                static_cast<unsigned long>(model->session.selectedRow + 1u)],
        geometry.trackerBridge, 0u,
        S3GTrackerThemeColor(S3GTrackerThemeRole::TextSecondary));
    drawBurstInfo(@"SEQ", @"CD / EN / PR / SK / EU GATE WHOLE BURST",
        geometry.trackerBridge, 1u,
        S3GTrackerThemeColor(S3GTrackerThemeRole::TextMuted));
    drawBurstInfo(@"TIMING", @"MT / DL SHIFT WHOLE BURST",
        geometry.trackerBridge, 2u,
        S3GTrackerThemeColor(S3GTrackerThemeRole::TextMuted));
    drawBurstInfo(@"EXPAND", @"RR / ST / FL / GL DISABLED",
        geometry.trackerBridge, 3u,
        S3GTrackerThemeColor(S3GTrackerThemeRole::Warning));
}

- (void)drawPitchMapWorkspace
{
    auto* model = self.trackerState;
    if (!model || model->session.pattern.tracks.empty()) return;
    [self refreshPitchMapPreview];
    const auto style = s3g::clap_gui::softTextStyle();
    NSDictionary* labels = s3g::clap_gui::softLabelAttrs();
    NSDictionary* values = s3g::clap_gui::softValueAttrs();
    const auto geometry = [self geometryLayout];
    const auto lane = std::min(model->session.selectedTrack,
        model->session.pattern.tracks.size() - 1u);
    std::size_t first = 0u;
    std::size_t last = 0u;
    [self pitchMapRowsFirst:&first last:&last];

    s3g::clap_gui::drawToolboxHeaderActionButton(
        [self pitchAnalyzeHeaderButtonRect], [self laneCyclePanelRect],
        @"ANALYZE", values, style);
    NSString* laneTitle = self.lanePopup.titleOfSelectedItem;
    drawTrackerProcessorMenu(@"LANE", laneTitle ? laneTitle : @"—",
        layout::rowY(geometry.laneCycle, 0u), geometry.laneCycle.frame.x,
        geometry.laneCycle.frame.width, labels, values, style);
    NSString* scope = _pitchUseFullCycle
        ? [NSString stringWithFormat:@"FULL · %03lu ROWS",
            static_cast<unsigned long>(last - first + 1u)]
        : [NSString stringWithFormat:@"%03lu–%03lu",
            static_cast<unsigned long>(first + 1u),
            static_cast<unsigned long>(last + 1u)];
    drawTrackerProcessorMenu(@"ROWS", scope,
        layout::rowY(geometry.laneCycle, 1u), geometry.laneCycle.frame.x,
        geometry.laneCycle.frame.width, labels, values, style);
    static NSArray<NSString*>* roots = @[
        @"C", @"C#", @"D", @"D#", @"E", @"F",
        @"F#", @"G", @"G#", @"A", @"A#", @"B"
    ];
    drawTrackerProcessorMenu(@"ROOT",
        roots[_pitchSettings.rootPitchClass % 12u],
        layout::rowY(geometry.laneCycle, 2u), geometry.laneCycle.frame.x,
        geometry.laneCycle.frame.width, labels, values, style);
    drawTrackerProcessorMenu(@"SCALE", nsString(
            s3g::musicalScaleDefinition(_pitchSettings.scale).name),
        layout::rowY(geometry.laneCycle, 3u), geometry.laneCycle.frame.x,
        geometry.laneCycle.frame.width, labels, values, style);
    s3g::clap_gui::drawProcessorSliderWithValueWidth(@"LOW",
        [NSString stringWithFormat:@"%@ · %03u",
            midiNoteName(_pitchSettings.minimumNote),
            static_cast<unsigned int>(_pitchSettings.minimumNote)],
        static_cast<CGFloat>(_pitchSettings.minimumNote) / 127.0,
        layout::rowY(geometry.laneCycle, 4u), geometry.laneCycle.frame.x,
        geometry.laneCycle.frame.width, kGeometryNoteValueWidth,
        labels, values, style);
    s3g::clap_gui::drawProcessorSliderWithValueWidth(@"HIGH",
        [NSString stringWithFormat:@"%@ · %03u",
            midiNoteName(_pitchSettings.maximumNote),
            static_cast<unsigned int>(_pitchSettings.maximumNote)],
        static_cast<CGFloat>(_pitchSettings.maximumNote) / 127.0,
        layout::rowY(geometry.laneCycle, 5u), geometry.laneCycle.frame.x,
        geometry.laneCycle.frame.width, kGeometryNoteValueWidth,
        labels, values, style);
    const CGFloat evidenceY = static_cast<CGFloat>(
        layout::rowY(geometry.laneCycle, 6u));
    [@"EVIDENCE" drawAtPoint:NSMakePoint(layout::processorLabelX(
        geometry.laneCycle.frame.x), evidenceY - 2.0)
        withAttributes:labels];
    NSString* evidence = _pitchAnalysis.noteCount == 0u ? @"NO NOTE DATA"
        : [NSString stringWithFormat:@"%lu NOTES · %lu PC · %d%%",
            static_cast<unsigned long>(_pitchAnalysis.noteCount),
            static_cast<unsigned long>(_pitchAnalysis.uniquePitchClasses),
            static_cast<int>(std::lround(_pitchAnalysis.confidence * 100.0f))];
    [evidence drawAtPoint:NSMakePoint(layout::processorControlX(
        geometry.laneCycle.frame.x), evidenceY - 2.0)
        withAttributes:values];

    s3g::clap_gui::drawToolboxHeaderActionButton(
        [self pitchNewSeedHeaderButtonRect], [self editPanelRect],
        @"NEW SEED", values, style);
    s3g::clap_gui::drawToolboxHeaderActionButton(
        [self pitchPreviewHeaderButtonRect], [self editPanelRect],
        @"PREVIEW", model->playing ? labels : values, style);
    if (_pitchPreviewFeedbackActive) {
        const NSRect previewButton = [self pitchPreviewHeaderButtonRect];
        fillRect(NSInsetRect(previewButton, 1.0, 1.0),
            S3GTrackerThemeColor(S3GTrackerThemeRole::Success, 0.28));
        strokeRect(NSInsetRect(previewButton, 0.5, 0.5),
            S3GTrackerThemeColor(S3GTrackerThemeRole::Success), 1.25);
        drawCenteredText(@"PLAYING", previewButton,
            S3GTrackerThemeColor(S3GTrackerThemeRole::TextPrimary), 7.0,
            NSFontWeightSemibold);
    }
    drawTrackerProcessorMenu(@"CONTOUR", nsString(pitchContourName(
            _pitchSettings.contour)),
        layout::rowY(geometry.editShape, 0u), geometry.editShape.frame.x,
        geometry.editShape.frame.width, labels, values, style);
    drawTrackerProcessorMenu(@"LEAP",
        [NSString stringWithFormat:@"%u DEGREE%@",
            static_cast<unsigned int>(_pitchSettings.maximumLeapDegrees),
            _pitchSettings.maximumLeapDegrees == 1u ? @"" : @"S"],
        layout::rowY(geometry.editShape, 1u), geometry.editShape.frame.x,
        geometry.editShape.frame.width, labels, values, style);
    const BOOL variationActive = _pitchSettings.contour != PitchContour::Fit
        && _pitchSettings.contour != PitchContour::Manual;
    s3g::clap_gui::drawProcessorSlider(@"VAR",
        variationActive ? [NSString stringWithFormat:@"%03d%%",
            static_cast<int>(std::lround(_pitchSettings.variation * 100.0f))]
            : @"N/A",
        variationActive ? _pitchSettings.variation : 0.0,
        layout::rowY(geometry.editShape, 2u),
        geometry.editShape.frame.x, geometry.editShape.frame.width,
        labels, values, style);
    s3g::clap_gui::drawProcessorToggle(@"ANCHORS",
        _pitchSettings.preserveEndpoints,
        layout::rowY(geometry.editShape, 3u), geometry.editShape.frame.x,
        geometry.editShape.frame.width, labels, values, style);
    s3g::clap_gui::drawProcessorSlider(@"TRANSPOSE",
        [NSString stringWithFormat:@"%+03d ST",
            static_cast<int>(_pitchSettings.transposeSemitones)],
        (static_cast<CGFloat>(_pitchSettings.transposeSemitones) + 24.0)
            / 48.0,
        layout::rowY(geometry.editShape, 4u),
        geometry.editShape.frame.x, geometry.editShape.frame.width,
        labels, values, style);
    s3g::clap_gui::drawProcessorToggle(@"INVERT",
        _pitchSettings.invertScaleDegrees,
        layout::rowY(geometry.editShape, 5u), geometry.editShape.frame.x,
        geometry.editShape.frame.width, labels, values, style);
    s3g::clap_gui::drawProcessorToggle(@"REVERSE",
        _pitchSettings.reversePitchOrder,
        layout::rowY(geometry.editShape, 6u), geometry.editShape.frame.x,
        geometry.editShape.frame.width, labels, values, style);
    drawTrackerProcessorMenu(@"MODE", @"PITCH MAP",
        layout::rowY(geometry.view, 0u), geometry.view.frame.x,
        geometry.view.frame.width, labels, values, style);

    const BOOL editable = [self canEditDisplayedPattern];
    s3g::clap_gui::drawToolboxHeaderActionButton(
        [self pitchApplyHeaderButtonRect], [self bridgePanelRect],
        @"APPLY", editable ? values : labels, style);
    const CGFloat bridgeX = geometry.trackerBridge.frame.x;
    const CGFloat labelX = layout::processorLabelX(bridgeX);
    const CGFloat valueX = layout::processorControlX(bridgeX);
    const auto drawInfo = [&](NSString* name, NSString* value, uint32_t row,
                              NSColor* color) {
        const CGFloat y = layout::rowY(geometry.trackerBridge, row);
        [name drawAtPoint:NSMakePoint(labelX, y - 2.0)
            withAttributes:labels];
        NSDictionary* attrs = @{
            NSForegroundColorAttributeName: color,
            NSFontAttributeName: s3g::clap_gui::uiFont(10.0),
        };
        [value drawAtPoint:NSMakePoint(valueX, y - 2.0)
            withAttributes:attrs];
    };
    drawInfo(@"TARGET", [NSString stringWithFormat:@"T%02lu · ROWS %03lu–%03lu",
        static_cast<unsigned long>(lane + 1u),
        static_cast<unsigned long>(first + 1u),
        static_cast<unsigned long>(last + 1u)], 0u,
        S3GTrackerThemeColor(S3GTrackerThemeRole::TextSecondary));
    drawInfo(@"SOURCE", @"EXPLICIT NOTE CELLS ONLY", 1u,
        S3GTrackerThemeColor(S3GTrackerThemeRole::Note));
    drawInfo(@"PREVIEW", [NSString stringWithFormat:@"%lu HITS · %lu CHANGES",
        static_cast<unsigned long>(_pitchPreview.assignments.size()),
        static_cast<unsigned long>(_pitchPreview.changed)], 2u,
        S3GTrackerThemeColor(S3GTrackerThemeRole::Live));
    drawInfo(@"STATE", _pitchStatus ? _pitchStatus : @"READY", 3u,
        S3GTrackerThemeColor(editable
            ? S3GTrackerThemeRole::TextMuted
            : S3GTrackerThemeRole::Warning));

    const int low = [self pitchMapDisplayMinimum];
    const int high = [self pitchMapDisplayMaximum];
    const CGFloat pitchSpan = static_cast<CGFloat>(std::max(1, high - low));
    if (_pitchPreview.assignments.empty()) {
        for (const NSRect graph : { [self pitchGraphRect],
                 [self pitchIntervalGraphRect] }) {
            fillRect(graph,
                S3GTrackerThemeColor(S3GTrackerThemeRole::Canvas, 0.86));
            strokeRect(NSInsetRect(graph, 0.5, 0.5),
                S3GTrackerThemeColor(S3GTrackerThemeRole::Border));
            drawCenteredText(@"NO EXPLICIT NOTE HITS IN THIS RANGE", graph,
                S3GTrackerThemeColor(S3GTrackerThemeRole::TextFaint), 8.0,
                NSFontWeightMedium);
        }
        return;
    }
    for (NSUInteger graphIndex = 0u; graphIndex < 2u; ++graphIndex) {
        const BOOL drawingInterval = graphIndex == 1u;
        const NSRect graph = drawingInterval
            ? [self pitchIntervalGraphRect] : [self pitchGraphRect];
        fillRect(graph,
            S3GTrackerThemeColor(S3GTrackerThemeRole::Canvas, 0.86));
        strokeRect(NSInsetRect(graph, 0.5, 0.5),
            S3GTrackerThemeColor(S3GTrackerThemeRole::Border));
    if (drawingInterval) {
        const int extent = [self pitchIntervalExtent];
        const CGFloat usableHalfHeight = std::max<CGFloat>(1.0,
            NSHeight(graph) * 0.5 - 12.0);
        int previousDegree = std::numeric_limits<int>::min();
        for (int guideIndex = 0; guideIndex <= 4; ++guideIndex) {
            const int degree = static_cast<int>(std::lround(
                -static_cast<double>(extent)
                    + static_cast<double>(extent * 2 * guideIndex) / 4.0));
            if (degree == previousDegree) continue;
            previousDegree = degree;
            const CGFloat y = NSMidY(graph)
                - static_cast<CGFloat>(degree)
                    / static_cast<CGFloat>(extent) * usableHalfHeight;
            NSBezierPath* guide = [NSBezierPath bezierPath];
            [guide moveToPoint:NSMakePoint(NSMinX(graph), y)];
            [guide lineToPoint:NSMakePoint(NSMaxX(graph), y)];
            guide.lineWidth = degree == 0 ? 1.15 : 0.5;
            [S3GTrackerThemeColor(degree == 0
                    ? S3GTrackerThemeRole::BorderStrong
                    : S3GTrackerThemeRole::Grid,
                degree == 0 ? 0.82 : 0.42) setStroke];
            [guide stroke];
            drawText(degree == 0 ? @"0 DEG" : [NSString stringWithFormat:
                    @"%+d DEG", degree],
                NSMakeRect(NSMinX(graph) - 50.0, y - 6.0, 46.0, 12.0),
                S3GTrackerThemeColor(degree == 0
                    ? S3GTrackerThemeRole::TextSecondary
                    : S3GTrackerThemeRole::TextFaint), 6.4,
                NSFontWeightRegular, NSTextAlignmentRight);
        }
    } else {
        for (int note = low; note <= high; ++note) {
            if (![self pitchMapNoteMatchesScale:static_cast<uint8_t>(note)])
                continue;
            const CGFloat y = NSMaxY(graph)
                - static_cast<CGFloat>(note - low) / pitchSpan
                    * NSHeight(graph);
            const int effectiveRoot = (static_cast<int>(
                _pitchSettings.rootPitchClass) + static_cast<int>(
                    _pitchSettings.transposeSemitones) + 120) % 12;
            const bool root = note % 12 == effectiveRoot;
            NSBezierPath* guide = [NSBezierPath bezierPath];
            [guide moveToPoint:NSMakePoint(NSMinX(graph), y)];
            [guide lineToPoint:NSMakePoint(NSMaxX(graph), y)];
            guide.lineWidth = root ? 0.9 : 0.45;
            [S3GTrackerThemeColor(root
                    ? S3GTrackerThemeRole::BorderStrong
                    : S3GTrackerThemeRole::Grid,
                root ? 0.62 : 0.38) setStroke];
            [guide stroke];
            if (root || note == low || note == high) {
                drawText([NSString stringWithFormat:@"%@ · %03d",
                        midiNoteName(static_cast<uint8_t>(note)), note],
                    NSMakeRect(NSMinX(graph) - 50.0, y - 6.0, 46.0, 12.0),
                    S3GTrackerThemeColor(root
                        ? S3GTrackerThemeRole::TextSecondary
                        : S3GTrackerThemeRole::TextFaint), 6.4,
                    NSFontWeightRegular, NSTextAlignmentRight);
            }
        }
    }
    const std::size_t rowCount = last - first + 1u;
    const std::size_t rowLabelStride = std::max<std::size_t>(
        1u, (rowCount + 15u) / 16u);
    for (std::size_t row = first; row <= last; ++row) {
        const CGFloat x = NSMinX(graph) + static_cast<CGFloat>(row - first)
            / static_cast<CGFloat>(std::max<std::size_t>(1u, last - first))
                * NSWidth(graph);
        const bool selected = row == model->session.selectedRow;
        if (selected)
            fillRect(NSMakeRect(x - 2.0, NSMinY(graph), 4.0,
                NSHeight(graph)), S3GTrackerThemeColor(
                    S3GTrackerThemeRole::Selection, 0.30));
        if ((row - first) % rowLabelStride != 0u && row != last) continue;
        NSBezierPath* guide = [NSBezierPath bezierPath];
        [guide moveToPoint:NSMakePoint(x, NSMinY(graph))];
        [guide lineToPoint:NSMakePoint(x, NSMaxY(graph))];
        guide.lineWidth = row % 4u == 0u ? 0.8 : 0.4;
        [S3GTrackerThemeColor(S3GTrackerThemeRole::Grid,
            row % 4u == 0u ? 0.52 : 0.28) setStroke];
        [guide stroke];
        drawCenteredText([NSString stringWithFormat:@"%03lu",
                static_cast<unsigned long>(row + 1u)],
            NSMakeRect(x - 18.0, NSMaxY(graph) + 8.0, 36.0, 12.0),
            S3GTrackerThemeColor(S3GTrackerThemeRole::TextFaint), 6.4,
            NSFontWeightRegular);
    }
    NSBezierPath* contour = [NSBezierPath bezierPath];
    bool started = false;
    NSColor* laneColor = trackerColor(
        kLaneColors[lane % kLaneColors.size()], 0.92);
    for (std::size_t index = 0u;
         index < _pitchPreview.assignments.size(); ++index) {
        const auto& assignment = _pitchPreview.assignments[index];
        const NSPoint previewPoint = [self
            pitchMapPointForAssignmentAtIndex:index original:NO
            interval:drawingInterval];
        const std::size_t originalVoiceCount = drawingInterval
            ? 1u : assignment.voiceCount;
        for (std::size_t voice = 0u; voice < originalVoiceCount; ++voice) {
            NSPoint originalPoint = NSZeroPoint;
            if (drawingInterval) {
                originalPoint = [self
                    pitchMapPointForAssignmentAtIndex:index original:YES
                    interval:YES];
            } else {
                PitchMapAssignment original = assignment;
                original.note = original.originalNotes[voice];
                originalPoint = [self pitchMapPointForAssignment:original];
            }
            NSBezierPath* originalMarker = [NSBezierPath
                bezierPathWithOvalInRect:NSMakeRect(originalPoint.x - 3.0,
                    originalPoint.y - 3.0, 6.0, 6.0)];
            [S3GTrackerThemeColor(
                S3GTrackerThemeRole::TextFaint, 0.72) setStroke];
            originalMarker.lineWidth = 1.0;
            [originalMarker stroke];
        }
        if (!started) {
            [contour moveToPoint:previewPoint];
            started = true;
        } else [contour lineToPoint:previewPoint];
    }
    contour.lineWidth = 1.35;
    [laneColor setStroke];
    [contour stroke];
    const PitchMapAssignment* selectedAssignment = nullptr;
    NSPoint selectedPoint = NSZeroPoint;
    for (std::size_t index = 0u;
         index < _pitchPreview.assignments.size(); ++index) {
        const auto& assignment = _pitchPreview.assignments[index];
        const NSPoint point = [self
            pitchMapPointForAssignmentAtIndex:index original:NO
            interval:drawingInterval];
        const bool selected = assignment.row == model->session.selectedRow;
        if (!drawingInterval && assignment.voiceCount > 1u) {
            PitchMapAssignment highest = assignment;
            highest.note = assignment.notes[assignment.voiceCount - 1u];
            const NSPoint highestPoint = [self
                pitchMapPointForAssignment:highest];
            NSBezierPath* voicingStem = [NSBezierPath bezierPath];
            [voicingStem moveToPoint:point];
            [voicingStem lineToPoint:highestPoint];
            voicingStem.lineWidth = selected ? 1.2 : 0.8;
            [trackerColor(kLaneColors[lane % kLaneColors.size()],
                selected ? 0.72 : 0.42) setStroke];
            [voicingStem stroke];
            for (std::size_t voice = 1u; voice < assignment.voiceCount;
                 ++voice) {
                PitchMapAssignment voiced = assignment;
                voiced.note = assignment.notes[voice];
                const NSPoint voicedPoint = [self
                    pitchMapPointForAssignment:voiced];
                const CGFloat voicedRadius = selected ? 4.0 : 3.2;
                NSBezierPath* voicedMarker = [NSBezierPath
                    bezierPathWithOvalInRect:NSMakeRect(
                        voicedPoint.x - voicedRadius,
                        voicedPoint.y - voicedRadius,
                        voicedRadius * 2.0, voicedRadius * 2.0)];
                [S3GTrackerThemeColor(S3GTrackerThemeRole::Canvas) setFill];
                [voicedMarker fill];
                voicedMarker.lineWidth = selected ? 1.7 : 1.1;
                [laneColor setStroke];
                [voicedMarker stroke];
                if (assignment.notes[voice]
                    != assignment.originalNotes[voice]) {
                    [laneColor setFill];
                    [[NSBezierPath bezierPathWithOvalInRect:
                        NSInsetRect(voicedMarker.bounds, 1.9, 1.9)] fill];
                }
            }
        }
        const CGFloat radius = selected ? 5.0 : 4.0;
        NSBezierPath* marker = [NSBezierPath bezierPathWithOvalInRect:
            NSMakeRect(point.x - radius, point.y - radius,
                radius * 2.0, radius * 2.0)];
        [S3GTrackerThemeColor(S3GTrackerThemeRole::Canvas) setFill];
        [marker fill];
        marker.lineWidth = selected ? 2.0 : 1.25;
        [laneColor setStroke];
        [marker stroke];
        if (assignment.note != assignment.originalNote) {
            NSBezierPath* center = [NSBezierPath bezierPathWithOvalInRect:
                NSInsetRect(marker.bounds, 2.1, 2.1)];
            [laneColor setFill];
            [center fill];
        }
        if (selected) {
            selectedAssignment = &assignment;
            selectedPoint = point;
        }
    }
    if (selectedAssignment) {
        NSString* flagText = pitchAssignmentText(*selectedAssignment);
        if (drawingInterval) {
            const auto selectedIndex = static_cast<std::size_t>(
                selectedAssignment - _pitchPreview.assignments.data());
            flagText = selectedIndex == 0u
                ? [flagText stringByAppendingString:@" · ANCHOR"]
                : [flagText stringByAppendingFormat:@" · %+d DEG",
                    [self pitchIntervalAtIndex:selectedIndex original:NO]];
        }
        const CGFloat voiceWidth = 104.0 + static_cast<CGFloat>(
            selectedAssignment->voiceCount - 1u) * 112.0;
        const CGFloat flagWidth = std::min<CGFloat>(
            NSWidth(graph) - 8.0, voiceWidth + (drawingInterval ? 54.0 : 0.0));
        constexpr CGFloat flagHeight = 18.0;
        CGFloat flagX = selectedPoint.x + 11.0;
        if (flagX + flagWidth > NSMaxX(graph) - 4.0)
            flagX = selectedPoint.x - flagWidth - 11.0;
        CGFloat flagY = selectedPoint.y - flagHeight - 10.0;
        if (flagY < NSMinY(graph) + 4.0)
            flagY = selectedPoint.y + 10.0;
        flagX = std::clamp(flagX, NSMinX(graph) + 4.0,
            NSMaxX(graph) - flagWidth - 4.0);
        flagY = std::clamp(flagY, NSMinY(graph) + 4.0,
            NSMaxY(graph) - flagHeight - 4.0);
        const NSRect flag = NSMakeRect(
            std::floor(flagX), std::floor(flagY), flagWidth, flagHeight);
        NSBezierPath* leader = [NSBezierPath bezierPath];
        [leader moveToPoint:selectedPoint];
        [leader lineToPoint:NSMakePoint(
            selectedPoint.x < NSMidX(flag) ? NSMinX(flag) : NSMaxX(flag),
            NSMidY(flag))];
        leader.lineWidth = 1.0;
        [laneColor setStroke];
        [leader stroke];
        fillRect(flag, S3GTrackerThemeColor(S3GTrackerThemeRole::Raised,
            0.98));
        strokeRect(NSInsetRect(flag, 0.5, 0.5), laneColor, 1.1);
        drawCenteredText(flagText, NSInsetRect(flag, 5.0, 0.0),
            S3GTrackerThemeColor(S3GTrackerThemeRole::TextPrimary), 7.4,
            NSFontWeightSemibold);
    }
    drawText(!drawingInterval
            ? @"HOLLOW = ORIGINAL  ·  LANE COLOR = PREVIEW  ·  DRAG POINTS TO SCALE DEGREES"
            : @"0 = REPEAT  ·  + / − = SCALE-DEGREE MOTION  ·  DRAG SHIFTS THIS NOTE + FOLLOWING PHRASE",
        NSMakeRect(NSMinX(graph), NSMinY(graph) - 26.0,
            NSWidth(graph), 12.0),
        S3GTrackerThemeColor(S3GTrackerThemeRole::TextFaint), 6.6,
        NSFontWeightMedium, NSTextAlignmentCenter);
    }
}

- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    const auto style = s3g::clap_gui::softTextStyle();
    NSDictionary* labels = s3g::clap_gui::softLabelAttrs();
    NSDictionary* values = s3g::clap_gui::softValueAttrs();
    [style.bg setFill];
    NSRectFill(self.bounds);

    auto* model = self.trackerState;
    const auto* pattern = geometryPattern(model);

    const auto geometry = [self geometryLayout];
    const NSRect canvas = [self canvasRect];
    NSArray<NSString*>* fieldTitles = @[
        @"RING FIELD  /  EDITABLE MIDI GEOMETRY",
        @"ACTIVE PULSES  /  NOTE POLYGONS",
        @"ALL STEPS UNDERLAY  /  ROW LATTICE + PULSES",
        @"PHASE SPOKES  /  PLAYHEAD ALIGNMENT",
        @"LANE FOCUS  /  SELECTED CYCLE",
        @"COMPOSITE RING  /  PHASE COMPARISON",
        @"BURST EDITOR  /  SUB-ROW MIDI PHRASES",
        @"PITCH MAP  /  CONTOUR + SCALE-DEGREE INTERVALS",
    ];
    NSString* fieldTitle = fieldTitles[static_cast<NSUInteger>(
        std::clamp<NSInteger>(self.geometryViewMode, 0, 7))];
    s3g::clap_gui::drawPanelFrame(NSMinX(canvas), NSMinY(canvas),
        NSWidth(canvas), NSHeight(canvas), style);
    s3g::clap_gui::drawPanelHeader(fieldTitle, true, NSMinX(canvas),
        NSMinY(canvas), NSWidth(canvas),
        layout::kStandardMetrics.headerHeight, labels, style);
    const struct {
        const layout::Panel* panel;
        NSString* title;
    } panels[] {
        { &geometry.laneCycle, self.geometryViewMode
                == S3GTrackerGeometryViewModeBurst ? @"BURST LIBRARY"
            : self.geometryViewMode == S3GTrackerGeometryViewModePitchMap
                ? @"PITCH SOURCE" : @"LANE / CYCLE" },
        { &geometry.editShape, self.geometryViewMode
                == S3GTrackerGeometryViewModeBurst ? @"SUBSTEPS"
            : self.geometryViewMode == S3GTrackerGeometryViewModePitchMap
                ? @"CONTOUR" : @"EDIT / SHAPE" },
        { &geometry.view, self.burstLibraryOnly ? nil : @"VIEW" },
        { &geometry.trackerBridge, @"TRACKER BRIDGE" },
    };
    for (const auto& panel : panels) {
        if (!panel.title) continue;
        s3g::clap_gui::drawPanelFrame(*panel.panel, style);
        s3g::clap_gui::drawPanelHeader(panel.title, true,
            *panel.panel, labels, style);
    }
    const std::array<NSRect, 3u> zoomRects {{
        [self zoomOutRect], [self zoomResetRect], [self zoomInRect],
    }};
    NSArray<NSString*>* zoomLabels = @[
        @"−", [NSString stringWithFormat:@"%d%%",
            static_cast<int>(std::lround(self.geometryZoom * 100.0))], @"+",
    ];
    for (std::size_t index = 0u; index < zoomRects.size(); ++index) {
        s3g::clap_gui::drawToolboxHeaderButton(zoomRects[index], canvas,
            zoomLabels[index], index == 1u, labels, style);
    }
    [self syncToolboxControls];
    if (self.geometryViewMode == S3GTrackerGeometryViewModeBurst) {
        [self drawBurstWorkspace];
        [self drawOpenGeometryMenu];
        return;
    }
    if (self.geometryViewMode == S3GTrackerGeometryViewModePitchMap) {
        [self drawPitchMapWorkspace];
        [self drawOpenGeometryMenu];
        return;
    }

    const BOOL editable = [self canEditDisplayedPattern];
    for (NSUInteger index = 0u; index < self.toolButtons.count; ++index)
        self.toolButtons[index].enabled = index == 0u
            || (editable
                && self.geometryViewMode == S3GTrackerGeometryViewModeRingField);
    NSMutableArray<S3GTrackerActionButton*>* mutations =
        [NSMutableArray arrayWithArray:@[
            self.rotateBackButton, self.rotateForwardButton,
            self.densityDownButton, self.densityUpButton,
            self.reverseButton, self.reflectButton,
        ]];
    [mutations addObjectsFromArray:self.morphButtons];
    for (S3GTrackerActionButton* button in mutations)
        button.enabled = editable;

    if (!model || !pattern || pattern->tracks.empty()) {
        drawCenteredText(@"NO PATTERN LANES", [self canvasPlotRect],
            trackerColor(0x8f8f8f), 10.0);
        return;
    }
    _lastDisplayedPatternId = geometryPatternId(model);
    _lastDisplayedSongMuteMask = model->songPlaybackActive
        ? model->songPlaybackMutedTracks : 0u;
    const auto lanes = geometryLanes(pattern);
    const auto visible = visibleGeometryLanes(model);

    std::size_t selectedLane = visible.count > 0u
        ? visible.indices[0u] : std::numeric_limits<std::size_t>::max();
    for (std::size_t ordinal = 0u; ordinal < visible.count; ++ordinal) {
        if (visible.indices[ordinal] == model->session.selectedTrack) {
            selectedLane = visible.indices[ordinal];
            break;
        }
    }
    const NSPoint center = [self geometryCenter];
    const CGFloat maximum = [self geometryMaximumRadius];
    const CGFloat fullCircle = static_cast<CGFloat>(M_PI) * 2.0;
    const bool ringField = self.geometryViewMode
        == S3GTrackerGeometryViewModeRingField;
    const bool allSteps = [self allStepsUnderlayNodeCount] > 0u;
    const bool phaseSpokes = self.geometryViewMode
        == S3GTrackerGeometryViewModePhaseSpokes;
    const bool laneFocus = self.geometryViewMode
        == S3GTrackerGeometryViewModeLaneFocus;
    const bool composite = self.geometryViewMode
        == S3GTrackerGeometryViewModeCompositeRing;

    for (std::size_t ordinal = lanes.count; ordinal-- > 0u;) {
        const auto lane = lanes.indices[ordinal];
        const auto& track = pattern->tracks[lane];
        const bool muted = geometryLaneMuted(model, pattern, lane);
        const bool selected = lane == selectedLane;
        const bool lengthPreview = _geometryGestureActive
            && _geometryGestureKind == S3GTrackerGeometryGestureLength
            && _gestureLane == lane;
        const auto length = lengthPreview ? _gesturePreviewLength
            : std::clamp<std::size_t>(
                track.noteColumn.length, 1u, 256u);
        const bool rotationPreview = _geometryGestureActive
            && _geometryGestureKind == S3GTrackerGeometryGestureRotate
            && _gestureLane == lane;
        const bool densityPreview = _geometryGestureActive
            && _geometryGestureKind == S3GTrackerGeometryGestureDensity
            && _gestureLane == lane;
        const auto displayedPhase = lengthPreview ? _gesturePreviewPhase
            : track.noteColumn.phase % length;
        if (laneFocus && !selected && !muted) continue;
        CGFloat radius = [self ringRadiusForOrdinal:ordinal
            count:lanes.count];
        if (composite) radius = maximum * 0.70;
        if (laneFocus) radius = maximum * 0.70;
        NSColor* identity = trackerColor(
            kLaneColors[lane % kLaneColors.size()],
            selected ? 0.94 : ringField ? 0.42 : 0.66);
        NSColor* pointIdentity = trackerColor(
            kLaneColors[lane % kLaneColors.size()],
            selected ? 1.0 : ringField ? 0.90 : 0.78);
        NSBezierPath* ring = [NSBezierPath bezierPathWithOvalInRect:
            NSMakeRect(center.x - radius, center.y - radius,
                radius * 2.0, radius * 2.0)];
        ring.lineWidth = muted ? 1.15
            : allSteps ? (selected ? 4.8 : 3.2)
            : selected ? 1.35 : 0.72;
        if (muted) {
            const CGFloat dash[] = { 3.0, 4.0 };
            [ring setLineDash:dash count:2 phase:0.0];
            [S3GTrackerThemeColor(S3GTrackerThemeRole::Canvas, 0.94)
                setStroke];
        } else if (allSteps) {
            [[identity colorWithAlphaComponent:selected ? 0.13 : 0.08]
                setStroke];
        } else {
        [S3GTrackerThemeColor(selected
                ? S3GTrackerThemeRole::BorderStrong
                : S3GTrackerThemeRole::Grid,
            ringField ? 0.88 : 0.58) setStroke];
        }
        [ring stroke];
        if (muted) {
            drawCenteredText(@"M", NSMakeRect(center.x + radius - 5.0,
                center.y - 6.0, 10.0, 12.0),
                S3GTrackerThemeColor(S3GTrackerThemeRole::Border, 0.72),
                7.0, NSFontWeightMedium);
            continue;
        }

        NSBezierPath* polygon = [NSBezierPath bezierPath];
        bool polygonStarted = false;
        for (std::size_t row = 0u; row < length; ++row) {
            const CGFloat angle = -static_cast<CGFloat>(M_PI_2)
                + static_cast<CGFloat>(row) * fullCircle
                    / static_cast<CGFloat>(length);
            const CGFloat cosine = std::cos(angle);
            const CGFloat sine = std::sin(angle);
            const bool originalHit = row < track.notes.size()
                && noteCellIsActivePulse(track.notes[row]);
            const bool rotatedHit = rotationPreview
                && row < _gesturePreviewNotes.size()
                && noteCellIsActivePulse(_gesturePreviewNotes[row]);
            const bool previewHit = densityPreview
                && row < _gesturePreviewNotes.size()
                && noteCellIsActivePulse(_gesturePreviewNotes[row]);
            const bool hit = densityPreview
                ? originalHit || previewHit
                : rotationPreview ? rotatedHit : originalHit;
            if (allSteps) {
                const CGFloat nodeRadius = length <= 32u
                    ? (selected ? 2.8 : 2.1)
                    : length <= 64u ? (selected ? 2.0 : 1.55)
                                    : (selected ? 1.35 : 1.0);
                const NSRect nodeRect = NSMakeRect(
                    center.x + cosine * radius - nodeRadius,
                    center.y + sine * radius - nodeRadius,
                    nodeRadius * 2.0, nodeRadius * 2.0);
                [style.bg setFill];
                NSRectFill(nodeRect);
                [[identity colorWithAlphaComponent:selected ? 0.78 : 0.48]
                    setStroke];
                NSFrameRect(NSInsetRect(nodeRect, 0.5, 0.5));
                if (row % 4u == 0u) {
                    NSBezierPath* beatMark = [NSBezierPath bezierPath];
                    [beatMark moveToPoint:NSMakePoint(center.x + cosine
                        * (radius - 7.0), center.y + sine * (radius - 7.0))];
                    [beatMark lineToPoint:NSMakePoint(center.x + cosine
                        * (radius + 7.0), center.y + sine * (radius + 7.0))];
                    beatMark.lineWidth = selected ? 1.2 : 0.8;
                    [[identity colorWithAlphaComponent:selected ? 0.72 : 0.42]
                        setStroke];
                    [beatMark stroke];
                }
            } else if (ringField && (selected || length <= 32u)) {
                const CGFloat tick = selected && row % 4u == 0u ? 4.5 : 2.2;
                NSBezierPath* mark = [NSBezierPath bezierPath];
                [mark moveToPoint:NSMakePoint(center.x + cosine
                    * (radius - tick), center.y + sine * (radius - tick))];
                [mark lineToPoint:NSMakePoint(center.x + cosine
                    * (radius + tick), center.y + sine * (radius + tick))];
                mark.lineWidth = selected ? 0.8 : 0.45;
                [S3GTrackerThemeColor(S3GTrackerThemeRole::Grid,
                    selected ? 0.82 : 0.46) setStroke];
                [mark stroke];
            }
            if (!hit) continue;
            const CGFloat velocity = resolvedVelocity(track, row);
            const CGFloat eventRadius = ringField
                ? radius + (velocity - 0.5) * 12.0 : radius;
            const NSPoint eventPoint = NSMakePoint(
                center.x + cosine * eventRadius,
                center.y + sine * eventRadius);
            if (!ringField) {
                if (!polygonStarted) {
                    [polygon moveToPoint:eventPoint];
                    polygonStarted = true;
                } else [polygon lineToPoint:eventPoint];
            }
            const CGFloat bead = ringField
                ? (selected ? 3.4 : 2.4) + velocity * 2.2
                : allSteps ? (selected ? 3.5 : 2.8) : 2.0;
            NSBezierPath* point = [NSBezierPath bezierPathWithOvalInRect:
                NSMakeRect(eventPoint.x - bead, eventPoint.y - bead,
                    bead * 2.0, bead * 2.0)];
            if (ringField) {
                [S3GTrackerThemeColor(S3GTrackerThemeRole::Canvas, 0.88)
                    setFill];
                [[NSBezierPath bezierPathWithOvalInRect:
                    NSInsetRect(point.bounds, -1.4, -1.4)] fill];
            }
            NSColor* eventColor = rotationPreview
                ? S3GTrackerThemeColor(S3GTrackerThemeRole::Live, 0.92)
                : densityPreview
                    && previewHit && !originalHit
                ? S3GTrackerThemeColor(S3GTrackerThemeRole::Live, 0.82)
                : densityPreview && originalHit && !previewHit
                ? [pointIdentity colorWithAlphaComponent:0.24]
                : pointIdentity;
            [eventColor setFill];
            [point fill];
            if (densityPreview && previewHit != originalHit) {
                NSBezierPath* ghost = [NSBezierPath bezierPathWithOvalInRect:
                    NSInsetRect(point.bounds, -3.0, -3.0)];
                ghost.lineWidth = 1.1;
                [S3GTrackerThemeColor(previewHit
                        ? S3GTrackerThemeRole::Live
                        : S3GTrackerThemeRole::TextFaint,
                    previewHit ? 0.92 : 0.62) setStroke];
                [ghost stroke];
            }
            if (selected && row == model->session.selectedRow % length) {
                NSBezierPath* halo = [NSBezierPath bezierPathWithOvalInRect:
                    NSInsetRect(point.bounds, -3.4, -3.4)];
                halo.lineWidth = 1.2;
                [S3GTrackerThemeColor(S3GTrackerThemeRole::TextPrimary)
                    setStroke];
                [halo stroke];
            }
        }
        if (polygonStarted) {
            [polygon closePath];
            polygon.lineWidth = selected ? 1.8 : 1.0;
            [identity setStroke];
            [polygon stroke];
        }
        if (phaseSpokes || composite) {
            const CGFloat phaseAngle = -static_cast<CGFloat>(M_PI_2)
                + static_cast<CGFloat>(displayedPhase) * fullCircle
                    / static_cast<CGFloat>(length);
            const CGFloat phaseCosine = std::cos(phaseAngle);
            const CGFloat phaseSine = std::sin(phaseAngle);
            NSBezierPath* phaseMark = [NSBezierPath bezierPath];
            const CGFloat inner = phaseSpokes
                ? maximum * 0.10 : radius - 7.0;
            [phaseMark moveToPoint:NSMakePoint(
                center.x + phaseCosine * inner,
                center.y + phaseSine * inner)];
            [phaseMark lineToPoint:NSMakePoint(center.x + phaseCosine
                * (radius + 7.0), center.y + phaseSine
                    * (radius + 7.0))];
            phaseMark.lineWidth = selected ? 1.4 : 0.65;
            [identity setStroke];
            [phaseMark stroke];
        }
        if (ringField && selected) {
            const NSPoint rotateHandle = [self rotateHandlePoint];
            NSBezierPath* diamond = [NSBezierPath bezierPath];
            [diamond moveToPoint:NSMakePoint(
                rotateHandle.x, rotateHandle.y - 7.0)];
            [diamond lineToPoint:NSMakePoint(
                rotateHandle.x + 7.0, rotateHandle.y)];
            [diamond lineToPoint:NSMakePoint(
                rotateHandle.x, rotateHandle.y + 7.0)];
            [diamond lineToPoint:NSMakePoint(
                rotateHandle.x - 7.0, rotateHandle.y)];
            [diamond closePath];
            [S3GTrackerThemeColor(rotationPreview
                    ? S3GTrackerThemeRole::Live
                    : S3GTrackerThemeRole::Raised) setFill];
            [diamond fill];
            diamond.lineWidth = 1.2;
            [S3GTrackerThemeColor(S3GTrackerThemeRole::Live) setStroke];
            [diamond stroke];
            drawCenteredText(@"R", NSMakeRect(rotateHandle.x - 7.0,
                rotateHandle.y - 7.0, 14.0, 14.0),
                S3GTrackerThemeColor(rotationPreview
                    ? S3GTrackerThemeRole::Canvas
                    : S3GTrackerThemeRole::Live), 6.5,
                NSFontWeightBold);

            const auto displayedDensity = densityPreview
                ? _gesturePreviewDensity
                : s3g::tracker::geometryHitCount(track);
            const CGFloat densityRadius = radius + 18.0;
            const CGFloat densityFraction = static_cast<CGFloat>(
                displayedDensity) / static_cast<CGFloat>(length);
            if (displayedDensity > 0u) {
                NSBezierPath* densityArc = [NSBezierPath bezierPath];
                const auto segments = std::max<std::size_t>(2u,
                    static_cast<std::size_t>(std::ceil(
                        densityFraction * 64.0)));
                for (std::size_t segment = 0u; segment <= segments;
                     ++segment) {
                    const CGFloat fraction = densityFraction
                        * static_cast<CGFloat>(segment)
                        / static_cast<CGFloat>(segments);
                    const CGFloat angle = -static_cast<CGFloat>(M_PI_2)
                        + fraction * fullCircle;
                    const NSPoint arcPoint = [self
                        geometryPointAtRadius:densityRadius angle:angle];
                    if (segment == 0u) [densityArc moveToPoint:arcPoint];
                    else [densityArc lineToPoint:arcPoint];
                }
                densityArc.lineWidth = densityPreview ? 2.3 : 1.5;
                densityArc.lineCapStyle = NSLineCapStyleRound;
                [S3GTrackerThemeColor(S3GTrackerThemeRole::Live,
                    densityPreview ? 0.92 : 0.58) setStroke];
                [densityArc stroke];
            }
            const CGFloat densityAngle = -static_cast<CGFloat>(M_PI_2)
                + densityFraction * fullCircle;
            const NSPoint densityHandle = [self
                geometryPointAtRadius:densityRadius angle:densityAngle];
            NSBezierPath* densityKnob = [NSBezierPath bezierPathWithOvalInRect:
                NSMakeRect(densityHandle.x - 6.5,
                    densityHandle.y - 6.5, 13.0, 13.0)];
            [S3GTrackerThemeColor(densityPreview
                    ? S3GTrackerThemeRole::Live
                    : S3GTrackerThemeRole::Raised) setFill];
            [densityKnob fill];
            densityKnob.lineWidth = 1.2;
            [S3GTrackerThemeColor(S3GTrackerThemeRole::Live) setStroke];
            [densityKnob stroke];
            drawCenteredText(@"D", NSMakeRect(densityHandle.x - 6.5,
                densityHandle.y - 6.5, 13.0, 13.0),
                S3GTrackerThemeColor(densityPreview
                    ? S3GTrackerThemeRole::Canvas
                    : S3GTrackerThemeRole::Live), 6.2,
                NSFontWeightBold);
        }
    }

    fillRect(NSMakeRect(center.x - 2.0, center.y - 2.0, 4.0, 4.0),
        S3GTrackerThemeColor(S3GTrackerThemeRole::TextFaint));
    NSString* fieldGuide = ringField
        ? @"DRAG R: ROTATE ROWS  •  DRAG D: DENSITY  •  DOUBLE BEAD: TRACKER  •  M: MUTED  •  ⌥ ERASE"
        : allSteps
        ? @"EVERY TRACKER ROW = HOLLOW CELL  •  FILLED CELLS = ACTIVE PULSES"
        : @"DIAGNOSTIC VIEW  •  SWITCH TO RING FIELD TO EDIT";
    drawText(fieldGuide,
        NSMakeRect(NSMinX(canvas) + 10.0, NSMinY(canvas) + 28.0,
            NSWidth(canvas) - 20.0, 12.0),
        S3GTrackerThemeColor(S3GTrackerThemeRole::TextFaint), 6.8,
        NSFontWeightMedium, NSTextAlignmentCenter);

    if (visible.count == 0u) {
        drawCenteredText(@"ALL NOTE LANES MUTED", NSMakeRect(
            NSMinX(canvas) + 12.0, NSMinY(canvas) + 43.0,
            NSWidth(canvas) - 24.0, 18.0),
            S3GTrackerThemeColor(S3GTrackerThemeRole::TextFaint), 8.0,
            NSFontWeightMedium);
        [self drawOpenGeometryMenu];
        return;
    }

    const auto& selectedTrack = pattern->tracks[selectedLane];
    const bool selectedLengthPreview = _geometryGestureActive
        && _geometryGestureKind == S3GTrackerGeometryGestureLength
        && _gestureLane == selectedLane;
    const auto selectedLength = selectedLengthPreview
        ? _gesturePreviewLength : std::clamp<std::size_t>(
            selectedTrack.noteColumn.length, 1u, 256u);
    const auto selectedRow = model->session.selectedRow % selectedLength;
    const bool selectedRotationPreview = _geometryGestureActive
        && _geometryGestureKind == S3GTrackerGeometryGestureRotate
        && _gestureLane == selectedLane;
    const bool selectedDensityPreview = _geometryGestureActive
        && _geometryGestureKind == S3GTrackerGeometryGestureDensity
        && _gestureLane == selectedLane;
    const int displayedRotation = selectedRotationPreview
        ? _gesturePreviewRotation : 0;
    std::size_t hits = selectedDensityPreview ? _gesturePreviewDensity : 0u;
    if (!selectedDensityPreview) {
        for (std::size_t row = 0u; row < selectedLength; ++row) {
            if (row < selectedTrack.notes.size()
                && noteCellIsActivePulse(selectedTrack.notes[row])) ++hits;
        }
    }

    const CGFloat laneX = static_cast<CGFloat>(geometry.laneCycle.frame.x);
    const CGFloat laneWidth = static_cast<CGFloat>(
        geometry.laneCycle.frame.width);
    NSString* selectedLaneTitle = self.lanePopup.titleOfSelectedItem;
    drawTrackerProcessorMenu(@"LANE",
        selectedLaneTitle ? selectedLaneTitle : @"—",
        static_cast<CGFloat>(layout::rowY(geometry.laneCycle, 0u)),
        laneX, laneWidth, labels, values, style);
    const bool selectedNotePreview = _geometryGestureActive
        && _geometryGestureKind == S3GTrackerGeometryGestureDefaultNote
        && _gestureLane == selectedLane;
    const uint8_t displayedDefaultNote = selectedNotePreview
        ? _gesturePreviewDefaultNote
        : s3g::tracker::laneDefaultNote(model->session, selectedLane);
    s3g::clap_gui::drawProcessorSliderWithValueWidth(@"DEFAULT NOTE",
        [NSString stringWithFormat:@"%@ · %03u%@",
            midiNoteName(displayedDefaultNote),
            static_cast<unsigned int>(displayedDefaultNote),
            selectedNotePreview ? @"*" : @""],
        static_cast<CGFloat>(displayedDefaultNote) / 127.0,
        static_cast<CGFloat>(layout::rowY(geometry.laneCycle, 1u)),
        laneX, laneWidth, kGeometryNoteValueWidth,
        labels, values, style);
    s3g::clap_gui::drawProcessorSlider(@"LENGTH",
        [NSString stringWithFormat:@"%03lu%@",
            static_cast<unsigned long>(selectedLength),
            selectedLengthPreview ? @"*" : @""],
        std::sqrt(static_cast<CGFloat>(selectedLength - 1u) / 255.0),
        static_cast<CGFloat>(layout::rowY(geometry.laneCycle, 2u)),
        laneX, laneWidth, labels, values, style);
    NSString* selectedDirectionTitle =
        self.directionPopup.titleOfSelectedItem;
    drawTrackerProcessorMenu(@"DIRECTION",
        selectedDirectionTitle ? selectedDirectionTitle : @"—",
        static_cast<CGFloat>(layout::rowY(geometry.laneCycle, 3u)),
        laneX, laneWidth, labels, values, style);
    const CGFloat rotationFraction = selectedLength <= 1u ? 0.0
        : static_cast<CGFloat>(displayedRotation)
            / static_cast<CGFloat>(selectedLength - 1u);
    const CGFloat rotationPosition = 0.5 + std::copysign(
        std::sqrt(std::abs(rotationFraction)), rotationFraction) * 0.5;
    s3g::clap_gui::drawProcessorSlider(@"ROTATE ROWS",
        [NSString stringWithFormat:@"%+03d%@", displayedRotation,
            selectedRotationPreview ? @"*" : @""],
        rotationPosition,
        static_cast<CGFloat>(layout::rowY(geometry.laneCycle, 4u)),
        laneX, laneWidth, labels, values, style);
    s3g::clap_gui::drawProcessorSlider(@"DENSITY",
        [NSString stringWithFormat:@"%03lu%@",
            static_cast<unsigned long>(hits),
            selectedDensityPreview ? @"*" : @""],
        static_cast<CGFloat>(hits) / static_cast<CGFloat>(selectedLength),
        static_cast<CGFloat>(layout::rowY(geometry.laneCycle, 5u)),
        laneX, laneWidth, labels, values, style);
    s3g::clap_gui::drawProcessorToggle(@"VOL LINK",
        self.linkVelocityLength,
        static_cast<CGFloat>(layout::rowY(geometry.laneCycle, 6u)),
        laneX, laneWidth, labels, values, style);

    const CGFloat editLabelX = static_cast<CGFloat>(
        layout::processorLabelX(geometry.editShape.frame.x));
    NSArray<NSString*>* editLabels = @[ @"TOOL", @"SHAPE" ];
    for (uint32_t row = 0u; row < 2u; ++row)
        [editLabels[row] drawAtPoint:NSMakePoint(editLabelX,
            static_cast<CGFloat>(layout::rowY(geometry.editShape, row)) - 2.0)
            withAttributes:labels];
    NSArray<NSString*>* toolNames = @[
        @"SEL", @"PNT", @"ERS", @"VEL"
    ];
    for (NSUInteger index = 0u; index < toolNames.count; ++index) {
        S3GTrackerDrawSuiteActionButton([self editToolButtonRect:index],
            toolNames[index], self.toolButtons[index].enabled, NO, NO,
            static_cast<NSUInteger>(self.geometryTool) == index,
            NO, NO, NO, YES);
    }
    S3GTrackerDrawSuiteActionButton([self reverseButtonRect], @"REVERSE",
        self.reverseButton.enabled, NO, NO, NO, NO, NO, NO, YES);
    S3GTrackerDrawSuiteActionButton([self reflectButtonRect], @"REFLECT",
        self.reflectButton.enabled, NO, NO, NO, NO, NO, NO, YES);
    NSString* morphTarget = self.morphTargetPopup.titleOfSelectedItem;
    drawTrackerProcessorMenu(@"MORPH TO",
        morphTarget ? morphTarget : @"NEXT LANE",
        static_cast<CGFloat>(layout::rowY(geometry.editShape, 2u)),
        static_cast<CGFloat>(geometry.editShape.frame.x),
        static_cast<CGFloat>(geometry.editShape.frame.width),
        labels, values, style);
    [@"AMOUNT" drawAtPoint:NSMakePoint(editLabelX,
        static_cast<CGFloat>(layout::rowY(geometry.editShape, 3u)) - 2.0)
        withAttributes:labels];
    NSArray<NSString*>* morphAmounts = @[ @"25", @"50", @"75", @"100" ];
    for (NSUInteger index = 0u; index < morphAmounts.count; ++index) {
        S3GTrackerDrawSuiteActionButton([self morphAmountButtonRect:index],
            morphAmounts[index], self.morphButtons[index].enabled,
            NO, NO, NO, NO, NO, NO, YES);
    }

    const CGFloat viewX = static_cast<CGFloat>(geometry.view.frame.x);
    const CGFloat viewWidth = static_cast<CGFloat>(geometry.view.frame.width);
    NSString* selectedViewTitle = self.viewModePopup.titleOfSelectedItem;
    drawTrackerProcessorMenu(@"MODE",
        selectedViewTitle ? selectedViewTitle : @"RING FIELD",
        static_cast<CGFloat>(layout::rowY(geometry.view, 0u)),
        viewX, viewWidth, labels, values, style);

    const NSRect bridgePanel = [self bridgePanelRect];
    s3g::clap_gui::drawToolboxHeaderActionButton(
        [self revealHeaderButtonRect], bridgePanel, @"REVEAL IN TRACKER",
        values, style);
    NSString* cellName = @"REST";
    if (selectedRow < selectedTrack.notes.size()) {
        const auto& cell = selectedTrack.notes[selectedRow];
        if (cell.state == NoteCellState::Note) {
            cellName = [NSString stringWithFormat:@"%@ · %03u",
                midiNoteName(cell.note),
                static_cast<unsigned int>(cell.note)];
        }
        else if (cell.state == NoteCellState::Burst)
            cellName = nsString(burstSlotToken(cell.note));
        else if (cell.state == NoteCellState::RetriggerPrevious) cellName = @"RTR";
        else if (cell.state == NoteCellState::Hold) cellName = @"HLD";
        else if (cell.state == NoteCellState::Kill) cellName = @"KIL";
    }
    const CGFloat bridgeX = static_cast<CGFloat>(
        geometry.trackerBridge.frame.x);
    const CGFloat bridgeWidth = static_cast<CGFloat>(
        geometry.trackerBridge.frame.width);
    const CGFloat bridgeLabelX = static_cast<CGFloat>(
        layout::processorLabelX(bridgeX));
    const CGFloat bridgeControlX = static_cast<CGFloat>(
        layout::processorControlX(bridgeX));
    const CGFloat bridgeControlWidth = static_cast<CGFloat>(
        layout::processorMenuWidth(bridgeWidth));
    const auto drawBridgeValue = [&](NSString* name, NSString* value,
                                     uint32_t row, NSColor* infoColor) {
        const CGFloat y = static_cast<CGFloat>(layout::rowY(
            geometry.trackerBridge, row));
        [[name uppercaseString] drawAtPoint:NSMakePoint(
            bridgeLabelX, y - 2.0) withAttributes:labels];
        NSColor* color = infoColor ? infoColor
            : S3GTrackerThemeColor(S3GTrackerThemeRole::TextMuted);
        NSDictionary* infoAttrs = @{
            NSForegroundColorAttributeName: color,
            NSFontAttributeName: s3g::clap_gui::uiFont(10.0),
        };
        [color setFill];
        NSRectFill(NSMakeRect(bridgeControlX, y,
            2.0, 12.0));
        NSString* display = s3g::clap_gui::menuDisplayText(value,
            std::max<CGFloat>(0.0, bridgeControlWidth - 12.0),
            infoAttrs);
        [display drawAtPoint:NSMakePoint(bridgeControlX + 8.0,
            y - 2.0) withAttributes:infoAttrs];
    };
    drawBridgeValue(@"LANE", [NSString stringWithFormat:@"T%02lu  %@",
        static_cast<unsigned long>(selectedLane + 1u),
        nsString(selectedTrack.name)], 0u,
        S3GTrackerThemeColor(S3GTrackerThemeRole::TextSecondary));
    drawBridgeValue(@"ROW", [NSString stringWithFormat:@"%03lu  %@",
        static_cast<unsigned long>(selectedRow + 1u), cellName], 1u,
        S3GTrackerThemeColor(S3GTrackerThemeRole::Note));
    drawBridgeValue(@"VELOCITY", [NSString stringWithFormat:@"%03d",
        static_cast<int>(std::lround(
            resolvedVelocity(selectedTrack, selectedRow) * 127.0f))], 2u,
        S3GTrackerThemeColor(S3GTrackerThemeRole::Value));
    drawBridgeValue(@"STATE", editable ? @"SHARED TRACKER SELECTION"
        : @"SONG FOLLOW · EDITING LOCKED", 3u,
        S3GTrackerThemeColor(editable
            ? S3GTrackerThemeRole::TextMuted
            : S3GTrackerThemeRole::Warning));
    [self drawOpenGeometryMenu];
}

- (void)drawBurstPlaybackOverlay
{
    auto* model = self.trackerState;
    const auto* pattern = geometryPattern(model);
    if (!model || !model->playing || !pattern
        || _selectedBurstSlot >= model->session.burstLibrary.bursts.size())
        return;
    const auto& burst = model->session.burstLibrary.bursts[
        _selectedBurstSlot];
    if (burst.empty()) return;
    bool sounding = false;
    const auto laneCount = std::min<std::size_t>(pattern->tracks.size(),
        model->notePlayheads.size());
    for (std::size_t lane = 0u; lane < laneCount; ++lane) {
        if (geometryLaneMuted(model, pattern, lane)) continue;
        const auto& track = pattern->tracks[lane];
        const auto length = std::clamp<std::size_t>(
            track.noteColumn.length, 1u, 256u);
        const auto row = model->notePlayheads[lane] % length;
        if (row >= track.notes.size()) continue;
        const auto& cell = track.notes[row];
        if (cell.state == NoteCellState::Burst
            && cell.note == _selectedBurstSlot) {
            sounding = true;
            break;
        }
    }
    if (!sounding) return;

    const CGFloat phase = std::clamp<CGFloat>(
        model->subrowPlaybackPhase, 0.0, 1.0);
    std::size_t activeEvent = 0u;
    bool eventStarted = false;
    for (std::size_t index = 0u; index < burst.eventCount; ++index) {
        const CGFloat onset = static_cast<CGFloat>(
            burst.events[index].position) / 65535.0;
        if (onset > phase) break;
        activeEvent = index;
        eventStarted = true;
    }
    if (eventStarted) {
        const NSRect row = [self burstMatrixRowRect:activeEvent];
        fillRect(NSInsetRect(row, 1.0, 1.0),
            trackerColor(0xffdf3f, 0.10));
        fillRect(NSMakeRect(NSMinX(row) + 1.0, NSMinY(row) + 2.0,
            3.0, NSHeight(row) - 4.0), trackerColor(0xffdf3f, 0.92));
        strokeRect(NSInsetRect(row, 1.5, 1.5),
            trackerColor(0xffdf3f, 0.55), 1.0);
    }

    const NSRect graph = NSInsetRect([self burstBreakpointRect], 12.0, 14.0);
    const CGFloat graphX = NSMinX(graph) + phase * NSWidth(graph);
    NSBezierPath* cursor = [NSBezierPath bezierPath];
    [cursor moveToPoint:NSMakePoint(graphX, NSMinY(graph) + 10.0)];
    [cursor lineToPoint:NSMakePoint(graphX, NSMaxY(graph))];
    cursor.lineWidth = 1.4;
    [trackerColor(0xffdf3f, 0.88) setStroke];
    [cursor stroke];
    fillRect(NSMakeRect(graphX - 2.0, NSMinY(graph) + 7.0, 4.0, 4.0),
        trackerColor(0xffdf3f, 0.96));

    const NSRect radialPlot = [self burstRadialPlotRect];
    const NSPoint center = NSMakePoint(NSMidX(radialPlot),
        NSMidY(radialPlot));
    const CGFloat radius = std::max<CGFloat>(50.0,
        std::min(NSWidth(radialPlot), NSHeight(radialPlot))
            * 0.34 * self.geometryZoom);
    const CGFloat angle = -static_cast<CGFloat>(M_PI_2)
        + phase * 2.0 * static_cast<CGFloat>(M_PI);
    drawGeometryReadHead(NSMakePoint(center.x + std::cos(angle) * radius,
        center.y + std::sin(angle) * radius), 1.0, true, true);
}

- (void)drawPlaybackOverlay
{
    if (self.geometryViewMode == S3GTrackerGeometryViewModeBurst) {
        [self drawBurstPlaybackOverlay];
        return;
    }
    if (self.geometryViewMode == S3GTrackerGeometryViewModePitchMap) {
        auto* model = self.trackerState;
        if (!model || !model->playing
            || model->session.pattern.tracks.empty()) return;
        const auto lane = std::min(model->session.selectedTrack,
            model->session.pattern.tracks.size() - 1u);
        std::size_t first = 0u;
        std::size_t last = 0u;
        [self pitchMapRowsFirst:&first last:&last];
        const auto row = model->notePlayheads[lane];
        if (row < first || row > last) return;
        for (NSUInteger graphIndex = 0u; graphIndex < 2u; ++graphIndex) {
            const BOOL interval = graphIndex == 1u;
            const NSRect graph = interval
                ? [self pitchIntervalGraphRect] : [self pitchGraphRect];
            const CGFloat x = NSMinX(graph)
                + static_cast<CGFloat>(row - first)
                    / static_cast<CGFloat>(std::max<std::size_t>(
                        1u, last - first)) * NSWidth(graph);
            fillRect(NSMakeRect(x - 1.0, NSMinY(graph), 2.0,
                    NSHeight(graph)), trackerColor(0xffdf3f, 0.62));
            for (std::size_t index = 0u;
                 index < _pitchPreview.assignments.size(); ++index) {
                const auto& assignment = _pitchPreview.assignments[index];
                if (assignment.row != row) continue;
                const NSPoint point = [self
                    pitchMapPointForAssignmentAtIndex:index original:NO
                    interval:interval];
                drawGeometryReadHead(point, 1.0,
                    model->noteHits[lane], true);
                break;
            }
        }
        return;
    }
    auto* model = self.trackerState;
    const auto* pattern = geometryPattern(model);
    if (!model || !pattern || pattern->tracks.empty()) return;
    const auto lanes = geometryLanes(pattern);
    const auto visible = visibleGeometryLanes(model);
    if (visible.count == 0u) return;
    const NSPoint center = [self geometryCenter];
    const CGFloat cx = center.x;
    const CGFloat cy = center.y;
    const CGFloat maximum = [self geometryMaximumRadius];
    std::size_t focusLane = visible.indices[0u];
    for (std::size_t ordinal = 0u; ordinal < visible.count; ++ordinal) {
        if (visible.indices[ordinal] == model->session.selectedTrack) {
            focusLane = visible.indices[ordinal];
            break;
        }
    }
    const bool phaseSpokesMode = self.geometryViewMode
        == S3GTrackerGeometryViewModePhaseSpokes;
    const bool laneFocusMode = self.geometryViewMode
        == S3GTrackerGeometryViewModeLaneFocus;
    const bool compositeMode = self.geometryViewMode
        == S3GTrackerGeometryViewModeCompositeRing;
    const CGFloat normalizedRadius = maximum * 0.72;
    for (std::size_t ordinal = lanes.count; ordinal-- > 0u;) {
        const auto lane = lanes.indices[ordinal];
        if (geometryLaneMuted(model, pattern, lane)) continue;
        const auto& track = pattern->tracks[lane];
        const auto length = std::clamp<std::size_t>(
            track.noteColumn.length, 1u, 256u);
        const CGFloat regularRadius = [self ringRadiusForOrdinal:ordinal
            count:lanes.count];
        const bool selected = lane == focusLane;
        const CGFloat radius = compositeMode
            ? normalizedRadius
            : laneFocusMode && selected ? normalizedRadius : regularRadius;
        if (phaseSpokesMode) {
            const auto phasePosition = model->notePlayheads[lane] % length;
            const CGFloat phaseAngle = -static_cast<CGFloat>(M_PI_2)
                + static_cast<CGFloat>(phasePosition) * 2.0
                    * static_cast<CGFloat>(M_PI)
                    / static_cast<CGFloat>(length);
            const CGFloat cosine = std::cos(phaseAngle);
            const CGFloat sine = std::sin(phaseAngle);
            const CGFloat innerRadius = std::max<CGFloat>(7.0, radius * 0.16);
            NSBezierPath* spoke = [NSBezierPath bezierPath];
            [spoke moveToPoint:NSMakePoint(cx + cosine * innerRadius,
                cy + sine * innerRadius)];
            [spoke lineToPoint:NSMakePoint(cx + cosine * radius,
                cy + sine * radius)];
            spoke.lineWidth = selected ? 1.7 : 1.0;
            NSColor* spokeColor = trackerColor(
                kLaneColors[lane % kLaneColors.size()],
                selected ? 0.92 : 0.48);
            [spokeColor setStroke];
            [spoke stroke];
            const CGFloat markerRadius = selected ? 2.6 : 1.8;
            [spokeColor setFill];
            [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(
                cx + cosine * radius - markerRadius,
                cy + sine * radius - markerRadius,
                markerRadius * 2.0, markerRadius * 2.0)] fill];
        }
        if (laneFocusMode && !selected) continue;
        const bool documentationHit = _documentationPlaybackSnapshot
            && _documentationHitLanes[lane];
        const bool currentHit = documentationHit
            || (model->playing && model->noteHits[lane]);
        const CGFloat haloStrength = currentHit
            ? 1.0 : _readHeadHaloStrength[lane];
        if (haloStrength <= 0.0) continue;
        auto position = (documentationHit
                ? _readHeadHaloRows[lane]
                : currentHit ? model->noteHitRows[lane]
                             : _readHeadHaloRows[lane]) % length;
        if (self.geometryViewMode == S3GTrackerGeometryViewModeRingField)
            position = (position + length
                - track.noteColumn.phase % length) % length;
        const CGFloat angle = -static_cast<CGFloat>(M_PI_2)
            + static_cast<CGFloat>(position) * 2.0
                * static_cast<CGFloat>(M_PI)
                / static_cast<CGFloat>(length);
        const NSPoint point = NSMakePoint(
            cx + std::cos(angle) * radius,
            cy + std::sin(angle) * radius);
        const CGFloat alpha = selected ? 1.0 : compositeMode ? 0.72 : 0.76;
        drawGeometryReadHead(point, haloStrength * alpha,
            currentHit, selected);
    }
}

@end

@implementation S3GTrackerGeometryPlaybackOverlayView

- (BOOL)isFlipped { return YES; }
- (BOOL)isOpaque { return NO; }
- (NSView*)hitTest:(NSPoint)point { (void)point; return nil; }

- (void)drawRect:(NSRect)dirtyRect
{
    if (NSGraphicsContext.currentContext.isDrawingToScreen) {
        CGContextClearRect(NSGraphicsContext.currentContext.CGContext,
            NSRectToCGRect(dirtyRect));
    }
    [self.geometryView drawPlaybackOverlay];
}

@end

@interface S3GTrackerGeometryWindowController : NSWindowController
@property(nonatomic, strong) S3GTrackerGeometryView* geometryView;
- (instancetype)initWithState:(TrackerViewState*)state
    owner:(S3GTrackerWorkspaceController*)owner;
@end

@implementation S3GTrackerGeometryWindowController

- (instancetype)initWithState:(TrackerViewState*)state
    owner:(S3GTrackerWorkspaceController*)owner
{
    const NSRect frame = NSMakeRect(0.0, 0.0, 920.0, 620.0);
    NSWindow* window = [[NSWindow alloc] initWithContentRect:frame
        styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
            | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable)
        backing:NSBackingStoreBuffered defer:NO];
    self = [super initWithWindow:window];
    if (self) {
        window.title = @"s3g Tracker — Rhythm Geometry";
        window.minSize = NSMakeSize(760.0, 520.0);
        window.backgroundColor = trackerColor(0x0c0c0c);
        window.appearance = [NSAppearance
            appearanceNamed:NSAppearanceNameDarkAqua];
        window.releasedWhenClosed = NO;
        window.tabbingMode = NSWindowTabbingModeDisallowed;
        self.geometryView = [[S3GTrackerGeometryView alloc]
            initWithState:state owner:owner];
        self.geometryView.frame = frame;
        self.geometryView.autoresizingMask = NSViewWidthSizable
            | NSViewHeightSizable;
        window.contentView = self.geometryView;
        S3GTrackerRestoreWindowFrame(window, @"S3GTrackerGeometryWindow");
    }
    return self;
}

- (void)showWindow:(id)sender
{
    [super showWindow:sender];
    if (self.window.miniaturized) [self.window deminiaturize:sender];
    [self.window makeKeyAndOrderFront:sender];
    [self.window makeFirstResponder:self.geometryView];
    [NSApp activateIgnoringOtherApps:YES];
}

@end

@class S3GTrackerEnvelopeView;

@interface S3GTrackerEnvelopePlaybackOverlayView : NSView
@property(nonatomic, weak) S3GTrackerEnvelopeView* envelopeView;
@end

@interface S3GTrackerEnvelopeView : NSView
- (instancetype)initWithState:(TrackerViewState*)state
    owner:(S3GTrackerWorkspaceController*)owner;
- (void)refreshPlaybackDisplay;
- (void)drawPlaybackOverlay;
@property(nonatomic, assign) TrackerViewState* trackerState;
@property(nonatomic, weak) S3GTrackerWorkspaceController* owner;
@property(nonatomic, strong) S3GTrackerEnvelopePlaybackOverlayView*
    playbackOverlay;
@property(nonatomic) NSInteger lastPaintedRow;
@property(nonatomic) BOOL paintingEnvelope;
@end

@implementation S3GTrackerEnvelopeView

- (instancetype)initWithState:(TrackerViewState*)state
    owner:(S3GTrackerWorkspaceController*)owner
{
    self = [super initWithFrame:NSZeroRect];
    if (self) {
        self.trackerState = state;
        self.owner = owner;
        self.lastPaintedRow = -1;
        self.playbackOverlay = [[S3GTrackerEnvelopePlaybackOverlayView alloc]
            initWithFrame:self.bounds];
        self.playbackOverlay.envelopeView = self;
        self.playbackOverlay.autoresizingMask = NSViewWidthSizable
            | NSViewHeightSizable;
        self.playbackOverlay.accessibilityHidden = YES;
        self.playbackOverlay.wantsLayer = YES;
        [self addSubview:self.playbackOverlay];
        self.accessibilityElement = YES;
        self.accessibilityRole = NSAccessibilityGroupRole;
        self.accessibilityLabel = @"Volume envelope";
        self.accessibilityHelp = @"Drag in the graph to paint normalized volume. During Song playback the envelope follows the sounding pattern and is read-only. Option-click writes Previous. Cyan breakpoints align with NOTE hits; gray breakpoints have no note. Brackets in the tracker adjust the selected value.";
    }
    return self;
}

- (BOOL)isFlipped { return YES; }

- (void)paintEvent:(NSEvent*)event clear:(BOOL)clear
{
    auto* model = self.trackerState;
    if (!model || model->songPlaybackActive
        || model->session.pattern.tracks.empty()) return;
    auto& session = model->session;
    const auto lane = std::min(session.selectedTrack,
        session.pattern.tracks.size() - 1u);
    auto& track = session.pattern.tracks[lane];
    const auto field = std::min<std::size_t>(session.selectedField, 6u);
    const bool gateField = gridFieldIsGate(field);
    const bool sequenceValue = gridFieldIsSequence(field)
        && !gridFieldIsSequenceAction(field);
    const auto pairIndex = sequenceValue ? gridSequencePair(field) : 0u;
    const auto rows = std::max<std::size_t>(16u,
        std::min<std::size_t>(256u, gateField
                ? track.gateColumn.length : !sequenceValue
                ? track.velocityColumn.length
                : track.fxPairs[pairIndex].valueColumn.length));
    const NSPoint point = [self convertPoint:event.locationInWindow fromView:nil];
    const CGFloat left = 30.0, right = 10.0, top = 34.0, bottom = 22.0;
    const CGFloat width = std::max<CGFloat>(1.0,
        NSWidth(self.bounds) - left - right);
    const CGFloat height = std::max<CGFloat>(1.0,
        NSHeight(self.bounds) - top - bottom);
    if (point.x < left || point.x > left + width || point.y < top
        || point.y > top + height) return;
    const NSInteger row = std::clamp<NSInteger>(static_cast<NSInteger>(
        (point.x - left) / width * static_cast<CGFloat>(rows)), 0,
        static_cast<NSInteger>(rows - 1u));
    const float value = static_cast<float>(std::clamp(
        1.0 - (point.y - top) / height, 0.0, 1.0));
    if (gateField) {
        if (track.gates.size() <= static_cast<std::size_t>(row))
            track.gates.resize(static_cast<std::size_t>(row) + 1u,
                GateCell::defaultValue());
        track.gates[static_cast<std::size_t>(row)] = clear
            ? GateCell::defaultValue()
            : GateCell::withRows(std::max(0.01f, value * 4.0f));
        track.gateColumn.length = std::max(track.gateColumn.length,
            static_cast<std::size_t>(row) + 1u);
    } else if (!sequenceValue) {
        if (track.velocities.size() <= static_cast<std::size_t>(row))
            track.velocities.resize(static_cast<std::size_t>(row) + 1u,
                ValueCell::defaultValue());
        track.velocities[static_cast<std::size_t>(row)] = clear
            ? ValueCell::previous() : ValueCell::withValue(value);
        track.velocityColumn.length = std::max(track.velocityColumn.length,
            static_cast<std::size_t>(row) + 1u);
    } else {
        auto& pair = track.fxPairs[pairIndex];
        if (pair.values.size() <= static_cast<std::size_t>(row))
            pair.values.resize(static_cast<std::size_t>(row) + 1u,
                FxValueCell::previous());
        pair.values[static_cast<std::size_t>(row)] = clear
            ? FxValueCell::previous() : FxValueCell::withValue(value);
        pair.valueColumn.length = std::max(pair.valueColumn.length,
            static_cast<std::size_t>(row) + 1u);
    }
    session.pattern.visibleRows = std::max(session.pattern.visibleRows,
        static_cast<std::size_t>(row) + 1u);
    session.selectedRow = static_cast<std::size_t>(row);
    self.lastPaintedRow = row;
    [self.owner modulePatternChanged];
}

- (void)mouseDown:(NSEvent*)event
{
    [self.window makeFirstResponder:self];
    self.lastPaintedRow = -1;
    if (self.trackerState && self.trackerState->songPlaybackActive) {
        self.paintingEnvelope = NO;
        return;
    }
    const NSPoint point = [self convertPoint:event.locationInWindow fromView:nil];
    const CGFloat left = 30.0, right = 10.0, top = 34.0, bottom = 22.0;
    self.paintingEnvelope = point.x >= left
        && point.x <= NSWidth(self.bounds) - right && point.y >= top
        && point.y <= NSHeight(self.bounds) - bottom;
    if (!self.paintingEnvelope) return;
    [self paintEvent:event clear:(event.modifierFlags
        & NSEventModifierFlagOption) != 0];
}

- (void)mouseDragged:(NSEvent*)event
{
    if (self.paintingEnvelope) [self paintEvent:event clear:NO];
}

- (void)mouseUp:(NSEvent*)event
{
    (void)event;
    self.paintingEnvelope = NO;
}

- (BOOL)acceptsFirstResponder { return YES; }

- (void)refreshPlaybackDisplay
{
    [self.playbackOverlay setNeedsDisplay:YES];
}

- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    fillRect(self.bounds, trackerColor(0x1d1d1d));
    strokeRect(NSInsetRect(self.bounds, 0.5, 0.5), trackerColor(0x565656));
    fillRect(NSMakeRect(0.0, 0.0, NSWidth(self.bounds), 24.0),
        trackerColor(0x131313));
    fillRect(NSMakeRect(0.0, 0.0, NSWidth(self.bounds), 2.0),
        trackerColor(0xb8b8b8));
    auto* model = self.trackerState;
    const auto* pattern = playbackFollowPattern(model);
    if (!model || !pattern || pattern->tracks.empty()) return;
    const auto lane = std::min(model->session.selectedTrack,
        pattern->tracks.size() - 1u);
    const auto& track = pattern->tracks[lane];
    const auto field = std::min<std::size_t>(
        model->session.selectedField, 6u);
    const bool gateField = gridFieldIsGate(field);
    const bool sequenceValue = gridFieldIsSequence(field)
        && !gridFieldIsSequenceAction(field);
    const auto pairIndex = sequenceValue ? gridSequencePair(field) : 0u;
    const auto rows = std::max<std::size_t>(16u,
        std::min<std::size_t>(256u, gateField
                ? track.gateColumn.length : !sequenceValue
                ? track.velocityColumn.length
                : track.fxPairs[pairIndex].valueColumn.length));
    NSString* envelopeName = gateField ? @"GATE (0–4 ROWS)"
        : !sequenceValue ? @"VOLUME"
        : [NSString stringWithFormat:@"SEQUENCE %lu VALUE",
            static_cast<unsigned long>(pairIndex + 1u)];
    drawText([NSString stringWithFormat:@"%@ ENVELOPE  /  T%lu  /  %@%@",
        envelopeName, static_cast<unsigned long>(lane + 1u),
        nsString(playbackFollowPatternId(model)),
        model->songPlaybackActive ? @" PLAYING" : @""], NSMakeRect(8.0, 6.0,
        NSWidth(self.bounds) - 16.0, 16.0),
        trackerColor(0xa8a8a8), 9.5);
    const CGFloat left = 30.0, right = 10.0, top = 34.0, bottom = 22.0;
    const CGFloat width = std::max<CGFloat>(1.0,
        NSWidth(self.bounds) - left - right);
    const CGFloat height = std::max<CGFloat>(1.0,
        NSHeight(self.bounds) - top - bottom);
    fillRect(NSMakeRect(left, top, width, height), trackerColor(0x0c0c0c));
    strokeRect(NSMakeRect(left, top, width, height), trackerColor(0x565656));
    drawText(@"1.0",
        NSMakeRect(2.0, top - 5.0, 25.0, 12.0),
        trackerColor(0x737a80), 7.0, NSFontWeightRegular,
        NSTextAlignmentRight);
    drawText(@"0", NSMakeRect(2.0, top + height - 6.0, 25.0, 12.0),
        trackerColor(0x737a80), 7.0, NSFontWeightRegular,
        NSTextAlignmentRight);
    NSBezierPath* curve = [NSBezierPath bezierPath];
    for (std::size_t row = 0u; row < rows; ++row) {
        const CGFloat x = left + (static_cast<CGFloat>(row) + 0.5)
            * width / static_cast<CGFloat>(rows);
        fillRect(NSMakeRect(x, top, 0.5, height), trackerColor(0x292d30));
        const auto gate = row < track.gates.size()
            ? track.gates[row].gateVoice(0u) : GateVoice {};
        const float value = gateField
            ? (gate.mode == GateVoiceMode::Tie ? 1.0f
                : gate.mode == GateVoiceMode::Rows
                    ? std::clamp(gate.rows / 4.0f, 0.0f, 1.0f) : 0.175f)
            : !sequenceValue ? resolvedVelocity(track, row)
            : resolvedFxValue(track, pairIndex, row);
        const CGFloat y = top + (1.0 - value) * height;
        if (row == 0u) [curve moveToPoint:NSMakePoint(x, y)];
        else [curve lineToPoint:NSMakePoint(x, y)];
    }
    NSColor* curveColor = trackerColor(0xb8b8b8, 0.8);
    [curveColor setStroke];
    curve.lineWidth = 1.2;
    [curve stroke];
    for (std::size_t row = 0u; row < rows; ++row) {
        const bool explicitValue = gateField
            ? row < track.gates.size() && track.gates[row].voiceCount > 0u
            : !sequenceValue
            ? row < track.velocities.size()
                && track.velocities[row].state == ValueCellState::Value
            : row < track.fxPairs[pairIndex].values.size()
                && track.fxPairs[pairIndex].values[row].state
                    == FxValueCellState::Value;
        if (!explicitValue) continue;
        const CGFloat x = left + (static_cast<CGFloat>(row) + 0.5)
            * width / static_cast<CGFloat>(rows);
        const auto gate = row < track.gates.size()
            ? track.gates[row].gateVoice(0u) : GateVoice {};
        const float value = gateField
            ? (gate.mode == GateVoiceMode::Tie ? 1.0f
                : gate.mode == GateVoiceMode::Rows
                    ? std::clamp(gate.rows / 4.0f, 0.0f, 1.0f) : 0.175f)
            : !sequenceValue ? resolvedVelocity(track, row)
            : resolvedFxValue(track, pairIndex, row);
        const CGFloat y = top + (1.0 - value) * height;
        const bool notePresent = row < track.noteColumn.length
            && row < track.notes.size()
            && noteCellIsActivePulse(track.notes[row]);
        const bool selected = row == model->session.selectedRow;
        if (selected) {
            NSBezierPath* selection = [NSBezierPath bezierPathWithOvalInRect:
                NSMakeRect(x - 4.5, y - 4.5, 9.0, 9.0)];
            [S3GTrackerThemeColor(S3GTrackerThemeRole::TextPrimary) setFill];
            [selection fill];
        }
        const CGFloat pointSize = notePresent ? 7.0 : 4.0;
        NSBezierPath* point = [NSBezierPath bezierPathWithOvalInRect:
            NSMakeRect(x - pointSize * 0.5, y - pointSize * 0.5,
                pointSize, pointSize)];
        NSColor* pointColor = notePresent
            ? trackerColor(0x14c7eb) : trackerColor(0x4c4c4c);
        [pointColor setFill];
        [point fill];
    }
    if (model->songPlaybackActive) {
        drawText(@"SONG PLAYBACK FOLLOW  /  READ ONLY",
            NSMakeRect(left, NSHeight(self.bounds) - 17.0, width, 12.0),
            trackerColor(0x737a80), 7.0);
    }
}

- (void)drawPlaybackOverlay
{
    auto* model = self.trackerState;
    const auto* pattern = playbackFollowPattern(model);
    if (!model || !model->playing || !pattern
        || pattern->tracks.empty()) return;
    const auto lane = std::min(model->session.selectedTrack,
        pattern->tracks.size() - 1u);
    const auto& track = pattern->tracks[lane];
    const auto field = std::min<std::size_t>(
        model->session.selectedField, 6u);
    const bool gateField = gridFieldIsGate(field);
    const bool sequenceValue = gridFieldIsSequence(field)
        && !gridFieldIsSequenceAction(field);
    const auto pairIndex = sequenceValue ? gridSequencePair(field) : 0u;
    const auto rows = std::max<std::size_t>(16u,
        std::min<std::size_t>(256u, gateField
                ? track.gateColumn.length : !sequenceValue
                ? track.velocityColumn.length
                : track.fxPairs[pairIndex].valueColumn.length));
    const CGFloat left = 30.0, right = 10.0, top = 34.0, bottom = 22.0;
    const CGFloat width = std::max<CGFloat>(1.0,
        NSWidth(self.bounds) - left - right);
    const CGFloat height = std::max<CGFloat>(1.0,
        NSHeight(self.bounds) - top - bottom);
    const auto playhead = (gateField ? model->notePlayheads[lane]
        : !sequenceValue ? model->velocityPlayheads[lane]
        : model->fxValuePlayheads[lane][pairIndex]) % rows;
    const CGFloat x = left + (static_cast<CGFloat>(playhead) + 0.5)
        * width / static_cast<CGFloat>(rows);
    fillRect(NSMakeRect(x - 1.0, top, 2.0, height),
        trackerColor(0xb8b8b8, 0.7));
}

@end

@implementation S3GTrackerEnvelopePlaybackOverlayView

- (BOOL)isFlipped { return YES; }
- (BOOL)isOpaque { return NO; }
- (NSView*)hitTest:(NSPoint)point { (void)point; return nil; }

- (void)drawRect:(NSRect)dirtyRect
{
    if (NSGraphicsContext.currentContext.isDrawingToScreen) {
        CGContextClearRect(NSGraphicsContext.currentContext.CGContext,
            NSRectToCGRect(dirtyRect));
    }
    [self.envelopeView drawPlaybackOverlay];
}

@end

@implementation S3GTrackerWorkspaceController

- (instancetype)initWithState:(TrackerViewState*)state
    callbacks:(WorkspaceCallbacks*)callbacks
{
    self = [super initWithNibName:nil bundle:nil];
    if (self) {
        self.trackerState = state;
        self.trackerCallbacks = callbacks;
        self.consoleHistory = [NSMutableArray array];
        self.consoleHistoryIndex = 0;
    }
    return self;
}

- (NSTextField*)label:(NSString*)text size:(CGFloat)size color:(NSColor*)color
{
    NSTextField* label = [NSTextField labelWithString:text];
    label.font = trackerFont(size);
    label.textColor = color;
    label.lineBreakMode = NSLineBreakByClipping;
    return label;
}

- (NSTextField*)suiteLabel:(NSString*)text
    alignment:(NSTextAlignment)alignment
{
    S3GTrackerSuiteLabel* label = [[S3GTrackerSuiteLabel alloc]
        initWithFrame:NSZeroRect];
    label.stringValue = text;
    label.alignment = alignment;
    return label;
}

- (NSButton*)button:(NSString*)title action:(SEL)action
{
    S3GTrackerActionButton* button = [[S3GTrackerActionButton alloc]
        initWithFrame:NSZeroRect];
    button.s3gUsesSuiteStyle = YES;
    button.s3gUsesNeutralTitle = YES;
    button.title = title;
    button.target = self;
    button.action = action;
    return button;
}

- (NSScrollView*)horizontalStripForStack:(NSStackView*)stack
{
    NSScrollView* scroll = [[NSScrollView alloc] initWithFrame:NSZeroRect];
    scroll.hasVerticalScroller = NO;
    scroll.hasHorizontalScroller = YES;
    scroll.autohidesScrollers = YES;
    scroll.scrollerStyle = NSScrollerStyleOverlay;
    scroll.scrollerKnobStyle = NSScrollerKnobStyleLight;
    scroll.borderType = NSNoBorder;
    scroll.drawsBackground = NO;
    stack.translatesAutoresizingMaskIntoConstraints = YES;
    scroll.documentView = stack;
    scroll.translatesAutoresizingMaskIntoConstraints = NO;
    return scroll;
}

- (void)resizeHorizontalStrip:(NSStackView*)stack
    inScrollView:(NSScrollView*)scroll
{
    if (!stack || !scroll) return;
    const NSSize fitting = stack.fittingSize;
    const CGFloat viewportWidth = NSWidth(scroll.contentView.bounds);
    const CGFloat viewportHeight = NSHeight(scroll.contentView.bounds);
    const CGFloat documentWidth = static_cast<CGFloat>(
        s3g::tracker::app::scrollingStripDocumentWidth(
            fitting.width, viewportWidth));
    stack.frame = NSMakeRect(0.0, 0.0, documentWidth,
        std::max<CGFloat>(fitting.height, viewportHeight));
}

- (NSTextField*)numberField:(SEL)action minimum:(double)minimum
    maximum:(double)maximum fractionDigits:(NSUInteger)fractionDigits
{
    S3GTrackerDragNumberField* field =
        [[S3GTrackerDragNumberField alloc] initWithFrame:NSZeroRect];
    S3GTrackerStyleSuiteTextField(field, NSTextAlignmentRight);
    field.target = self;
    field.action = action;
    field.delegate = self;
    field.cell.sendsActionOnEndEditing = YES;
    NSNumberFormatter* formatter = [[NSNumberFormatter alloc] init];
    formatter.numberStyle = NSNumberFormatterDecimalStyle;
    formatter.minimum = @(minimum);
    formatter.maximum = @(maximum);
    formatter.minimumFractionDigits = 0u;
    formatter.maximumFractionDigits = fractionDigits;
    formatter.usesGroupingSeparator = NO;
    formatter.lenient = NO;
    field.formatter = formatter;
    field.s3gMinimumValue = minimum;
    field.s3gMaximumValue = maximum;
    field.s3gFractionDigits = fractionDigits;
    const double unit = std::pow(10.0,
        -static_cast<double>(std::min<NSUInteger>(fractionDigits, 9u)));
    field.s3gDragIncrement = std::max(unit, (maximum - minimum) / 240.0);
    field.accessibilityHelp = @"Drag up to increase or down to decrease. Option-drag is fine; Shift-drag is coarse. Click to type an exact value.";
    return field;
}

- (NSStackView*)toolboxRowInPanel:(S3GTrackerToolboxView*)panel
    top:(CGFloat)top
{
    S3GTrackerFocusReleaseStackView* row =
        [[S3GTrackerFocusReleaseStackView alloc] initWithFrame:NSZeroRect];
    row.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    row.alignment = NSLayoutAttributeCenterY;
    row.spacing = 5.0;
    row.translatesAutoresizingMaskIntoConstraints = NO;
    [panel addSubview:row];
    [NSLayoutConstraint activateConstraints:@[
        [row.leadingAnchor constraintEqualToAnchor:panel.leadingAnchor
            constant:8.0],
        [row.trailingAnchor constraintLessThanOrEqualToAnchor:
            panel.trailingAnchor constant:-8.0],
        [row.topAnchor constraintEqualToAnchor:panel.topAnchor constant:top],
    [row.heightAnchor constraintEqualToConstant:22.0],
    ]];
    return row;
}

- (void)constrainToolboxControlHeights:(NSArray<NSStackView*>*)rows
{
    for (NSStackView* row in rows) {
        for (NSView* view in row.arrangedSubviews)
            [view.heightAnchor constraintEqualToConstant:22.0].active = YES;
    }
}

- (void)loadView
{
    S3GTrackerFocusReleaseView* root = [[S3GTrackerFocusReleaseView alloc]
        initWithFrame:NSMakeRect(0.0, 0.0, 1320.0, 840.0)];
    root.wantsLayer = YES;
    root.layer.backgroundColor = S3GTrackerThemeColor(
        S3GTrackerThemeRole::Canvas).CGColor;
    self.view = root;

    self.toolbar = [[S3GTrackerFocusReleaseView alloc]
        initWithFrame:NSZeroRect];
    self.toolbar.wantsLayer = YES;
    self.toolbar.layer.backgroundColor = S3GTrackerThemeColor(
        S3GTrackerThemeRole::Panel).CGColor;
    self.toolbar.translatesAutoresizingMaskIntoConstraints = NO;
    [root addSubview:self.toolbar];

    self.transportControls = [[S3GTrackerFocusReleaseStackView alloc]
        initWithFrame:NSZeroRect];
    NSStackView* toolboxes = self.transportControls;
    toolboxes.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    toolboxes.alignment = NSLayoutAttributeTop;
    toolboxes.spacing = 8.0;
    const CGFloat pageInset = static_cast<CGFloat>(
        s3g::gui_layout::kTrackerPageHorizontalInset);
    toolboxes.edgeInsets = NSEdgeInsetsMake(0.0, pageInset, 0.0, pageInset);
    self.transportScroll = [self horizontalStripForStack:toolboxes];
    self.transportScroll.accessibilityLabel =
        @"Pattern, transport, input, and view toolboxes";
    [self.toolbar addSubview:self.transportScroll];

    self.patternPanel = [[S3GTrackerToolboxView alloc] initWithFrame:NSZeroRect];
    self.patternPanel.toolboxIndex = 0;
    self.patternPanel.toolboxTitle = @"PATTERN";
    self.transportPanel = [[S3GTrackerToolboxView alloc] initWithFrame:NSZeroRect];
    self.transportPanel.toolboxIndex = 0;
    self.transportPanel.toolboxTitle = @"TRANSPORT";
    self.inputViewPanel = [[S3GTrackerToolboxView alloc] initWithFrame:NSZeroRect];
    self.inputViewPanel.toolboxIndex = 0;
    self.inputViewPanel.toolboxTitle = @"VIEW";
    for (S3GTrackerToolboxView* panel in @[
             self.patternPanel, self.inputViewPanel]) {
        [toolboxes addArrangedSubview:panel];
        [panel.heightAnchor constraintEqualToConstant:51.0].active = YES;
    }
    [self.patternPanel.widthAnchor constraintEqualToConstant:874.0].active = YES;
    [self.inputViewPanel.widthAnchor constraintEqualToConstant:402.0].active = YES;
    self.transportPanel.translatesAutoresizingMaskIntoConstraints = NO;
    [root addSubview:self.transportPanel];

    self.patternPrimaryControls = [self toolboxRowInPanel:self.patternPanel
        top:25.0];
    self.transportPrimaryControls = [self toolboxRowInPanel:self.transportPanel
        top:25.0];
    self.inputPrimaryControls = [self toolboxRowInPanel:self.inputViewPanel
        top:25.0];
    NSStackView* patternPrimary = self.patternPrimaryControls;
    NSStackView* transportPrimary = self.transportPrimaryControls;
    NSStackView* inputPrimary = self.inputPrimaryControls;
    patternPrimary.spacing = 8.0;
    transportPrimary.spacing = 3.0;
    inputPrimary.spacing = 8.0;

    self.patternPopup = [[S3GTrackerPopupButton alloc]
        initWithFrame:NSZeroRect pullsDown:NO];
    self.patternPopup.s3gUsesCanvasMenu = YES;
    self.patternPopup.target = self;
    self.patternPopup.action = @selector(patternSelectionChanged:);
    self.patternPopup.accessibilityLabel = @"Active pattern";
    self.patternPopup.toolTip = @"Pattern edits are stored automatically in the REAPER project";
    [self.patternPopup.widthAnchor constraintEqualToConstant:420.0].active = YES;
    [patternPrimary addArrangedSubview:self.patternPopup];
    self.renamePatternButton = [self button:@"NAME"
        action:@selector(renamePatternPressed:)];
    self.renamePatternButton.toolTip = @"Rename the active pattern without changing its stable ID";
    self.renamePatternButton.accessibilityLabel = @"Rename pattern";
    [patternPrimary addArrangedSubview:self.renamePatternButton];
    [self.renamePatternButton.widthAnchor constraintEqualToConstant:50.0].active = YES;
    self.duplicatePatternButton = [self button:@"DUP"
        action:@selector(duplicatePatternPressed:)];
    self.duplicatePatternButton.toolTip = @"Duplicate the active pattern";
    self.duplicatePatternButton.accessibilityLabel = @"Duplicate pattern";
    [patternPrimary addArrangedSubview:self.duplicatePatternButton];
    [self.duplicatePatternButton.widthAnchor constraintEqualToConstant:45.0].active = YES;
    self.createPatternButton = [self button:@"＋"
        action:@selector(newPatternPressed:)];
    self.createPatternButton.toolTip = @"Create a new blank pattern";
    self.createPatternButton.accessibilityLabel = @"New pattern";
    [patternPrimary addArrangedSubview:self.createPatternButton];
    [self.createPatternButton.widthAnchor constraintEqualToConstant:30.0].active = YES;
    self.deletePatternButton = [self button:@"−"
        action:@selector(deletePatternPressed:)];
    self.deletePatternButton.tag = 2;
    self.deletePatternButton.toolTip = @"Delete the active unreferenced pattern";
    self.deletePatternButton.accessibilityLabel = @"Delete pattern";
    [patternPrimary addArrangedSubview:self.deletePatternButton];
    [self.deletePatternButton.widthAnchor constraintEqualToConstant:30.0].active = YES;

    self.playButton = [self button:@"▶" action:@selector(playPressed:)];
    self.playButton.tag = 3;
    self.playButton.toolTip = @"Toggle REAPER play / pause (Space)";
    self.playButton.accessibilityLabel = @"Toggle host play or pause";
    [self.playButton.widthAnchor constraintEqualToConstant:32.0].active = YES;
    [transportPrimary addArrangedSubview:self.playButton];
    self.loopButton = [self button:@"↻" action:@selector(loopPressed:)];
    self.loopButton.tag = 1;
    self.loopButton.toolTip = @"Toggle global row loop (Shift-Space)";
    self.loopButton.accessibilityLabel = @"Loop";
    [self.loopButton.widthAnchor constraintEqualToConstant:32.0].active = YES;
    [transportPrimary addArrangedSubview:self.loopButton];
    S3GTrackerFillButton* fillButton = [[S3GTrackerFillButton alloc]
        initWithFrame:NSZeroRect];
    fillButton.s3gUsesSuiteStyle = YES;
    fillButton.s3gUsesNeutralTitle = YES;
    fillButton.tag = 3;
    fillButton.title = @"FILL";
    fillButton.target = self;
    fillButton.action = @selector(fillPressed:);
    fillButton.toolTip = @"Click to latch FILL; Shift-hold for momentary FILL";
    fillButton.accessibilityLabel = @"Performance fill";
    self.fillButton = fillButton;
    [self.fillButton.widthAnchor constraintEqualToConstant:48.0].active = YES;
    [transportPrimary addArrangedSubview:self.fillButton];
    self.restartButton = [self button:@"SYNC ALL"
        action:@selector(restartPressed:)];
    self.restartButton.toolTip = @"Force every lane and column to row 1, ignoring phase, without stopping REAPER";
    self.restartButton.accessibilityLabel = @"Sync all tracker lanes and columns to row 1";
    [self.restartButton.widthAnchor constraintEqualToConstant:60.0].active = YES;
    [transportPrimary addArrangedSubview:self.restartButton];
    NSButton* panicButton = [self button:@"! PANIC"
        action:@selector(panicPressed:)];
    panicButton.tag = 2;
    panicButton.toolTip = @"Send tracked Note Offs and CC 123 All Notes Off on MIDI channels 1–16";
    panicButton.accessibilityLabel = @"Clear active MIDI notes";
    [panicButton.widthAnchor constraintEqualToConstant:55.0].active = YES;
    [transportPrimary addArrangedSubview:panicButton];
    self.tempoScalePopup = [[S3GTrackerPopupButton alloc]
        initWithFrame:NSZeroRect pullsDown:NO];
    self.tempoScalePopup.s3gUsesCanvasMenu = YES;
    self.tempoScalePopup.target = self;
    self.tempoScalePopup.action = @selector(tempoScaleChanged:);
    self.tempoScalePopup.accessibilityLabel = @"Tracker tempo rate relative to host";
    for (std::size_t index = 0u; index < kTempoScales.size(); ++index) {
        [self.tempoScalePopup addItemWithTitle:
            [NSString stringWithUTF8String:kTempoScaleNames[index]]];
        self.tempoScalePopup.lastItem.representedObject =
            @(kTempoScales[index]);
    }
    [self.tempoScalePopup.widthAnchor constraintEqualToConstant:60.0].active = YES;
    [transportPrimary addArrangedSubview:self.tempoScalePopup];
    self.swingField = [[S3GTrackerSwingSlider alloc] initWithFrame:NSZeroRect];
    self.swingField.s3gLabel = @"SW";
    self.swingField.target = self;
    self.swingField.action = @selector(transportFieldChanged:);
    self.swingField.accessibilityLabel = @"Swing percentage";
    self.swingField.toolTip = @"Drag the short slider or scroll to set global swing from 50 to 75 percent";
    [self.swingField.widthAnchor constraintEqualToConstant:90.0].active = YES;
    [transportPrimary addArrangedSubview:self.swingField];
    self.gateField = [[S3GTrackerPopupButton alloc]
        initWithFrame:NSZeroRect pullsDown:NO];
    self.gateField.s3gUsesCanvasMenu = YES;
    self.gateField.target = self;
    self.gateField.action = @selector(gateFieldChanged:);
    self.gateField.accessibilityLabel = @"MIDI gate in milliseconds";
    [self.gateField.widthAnchor constraintEqualToConstant:62.0].active = YES;
    [transportPrimary addArrangedSubview:self.gateField];

    self.loopStartField = [[S3GTrackerPopupButton alloc]
        initWithFrame:NSZeroRect pullsDown:NO];
    self.loopStartField.s3gUsesCanvasMenu = YES;
    self.loopStartField.target = self;
    self.loopStartField.action = @selector(transportFieldChanged:);
    self.loopStartField.toolTip = @"First loop row";
    self.loopStartField.accessibilityLabel = @"First loop row";
    [self.loopStartField.widthAnchor constraintEqualToConstant:55.0].active = YES;
    [transportPrimary addArrangedSubview:self.loopStartField];
    self.loopEndField = [[S3GTrackerPopupButton alloc]
        initWithFrame:NSZeroRect pullsDown:NO];
    self.loopEndField.s3gUsesCanvasMenu = YES;
    self.loopEndField.target = self;
    self.loopEndField.action = @selector(transportFieldChanged:);
    self.loopEndField.toolTip = @"Last loop row";
    self.loopEndField.accessibilityLabel = @"Last loop row";
    [self.loopEndField.widthAnchor constraintEqualToConstant:55.0].active = YES;
    [transportPrimary addArrangedSubview:self.loopEndField];

    self.sequenceColumnsButton = [self button:@"EXPAND DETAIL"
        action:@selector(toggleSequenceColumns:)];
    [self.sequenceColumnsButton.widthAnchor
        constraintEqualToConstant:94.0].active = YES;
    self.sequenceColumnsButton.toolTip =
        @"Show SEQ1, V1, SEQ2, V2, and GATE in every tracker lane";
    self.sequenceColumnsButton.accessibilityLabel =
        @"Expand tracker sequencing columns";
    [inputPrimary addArrangedSubview:self.sequenceColumnsButton];
    self.trackAddButton = [self button:@"+ TRACK"
        action:@selector(trackAddPressed:)];
    [self.trackAddButton.widthAnchor
        constraintEqualToConstant:70.0].active = YES;
    [patternPrimary addArrangedSubview:self.trackAddButton];
    self.trackRemoveButton = [self button:@"− TRACK"
        action:@selector(trackRemovePressed:)];
    [self.trackRemoveButton.widthAnchor
        constraintEqualToConstant:76.0].active = YES;
    [patternPrimary addArrangedSubview:self.trackRemoveButton];
    self.undoButton = [self button:@"UNDO"
        action:@selector(undoPressed:)];
    [self.undoButton.widthAnchor constraintEqualToConstant:55.0].active = YES;
    self.undoButton.toolTip = @"Undo the last persistent Tracker edit (Control-Z)";
    self.undoButton.accessibilityLabel = @"Undo last Tracker edit";
    [patternPrimary addArrangedSubview:self.undoButton];
    self.redoButton = [self button:@"REDO"
        action:@selector(redoPressed:)];
    [self.redoButton.widthAnchor constraintEqualToConstant:55.0].active = YES;
    self.redoButton.toolTip = @"Redo the last undone Tracker edit (Control-Shift-Z)";
    self.redoButton.accessibilityLabel = @"Redo last Tracker edit";
    [patternPrimary addArrangedSubview:self.redoButton];
    self.noteDisplayButton = [self button:@"NOTE: NAME"
        action:@selector(toggleNoteDisplay:)];
    [self.noteDisplayButton.widthAnchor
        constraintEqualToConstant:88.0].active = YES;
    self.noteDisplayButton.toolTip =
        @"Show NOTE cells as decimal MIDI values";
    self.noteDisplayButton.accessibilityLabel =
        @"Show notes as MIDI values";
    [inputPrimary addArrangedSubview:self.noteDisplayButton];
    self.stepJumpPopup = [[S3GTrackerPopupButton alloc]
        initWithFrame:NSZeroRect pullsDown:NO];
    self.stepJumpPopup.s3gUsesCanvasMenu = YES;
    for (NSInteger jump = 1; jump <= 16; ++jump) {
        [self.stepJumpPopup addItemWithTitle:[NSString stringWithFormat:
            @"JUMP %ld", static_cast<long>(jump)]];
        self.stepJumpPopup.lastItem.representedObject = @(jump);
    }
    self.stepJumpPopup.target = self;
    self.stepJumpPopup.action = @selector(stepJumpChanged:);
    self.stepJumpPopup.accessibilityLabel = @"Tracker row jump";
    self.stepJumpPopup.toolTip = @"Rows moved by Up and Down; use 3 to enter a note every third row";
    [self.stepJumpPopup.widthAnchor
        constraintEqualToConstant:58.0].active = YES;
    [inputPrimary addArrangedSubview:self.stepJumpPopup];
    self.midiStepRecordPopup = [[S3GTrackerPopupButton alloc]
        initWithFrame:NSZeroRect pullsDown:NO];
    self.midiStepRecordPopup.s3gUsesCanvasMenu = YES;
    [self.midiStepRecordPopup addItemsWithTitles:@[
        @"REC OFF", @"REC STEP", @"REC Q", @"REC MT",
    ]];
    self.midiStepRecordPopup.target = self;
    self.midiStepRecordPopup.action = @selector(midiStepRecordModeChanged:);
    self.midiStepRecordPopup.accessibilityLabel = @"MIDI recording mode";
    self.midiStepRecordPopup.toolTip = @"Arm recording to the lane shown beside this menu; STEP advances by View JUMP; live modes follow the written row; LIVE MT preserves each chord voice's timing in an aligned MT stack";
    [self.midiStepRecordPopup.widthAnchor
        constraintEqualToConstant:75.0].active = YES;
    [transportPrimary addArrangedSubview:self.midiStepRecordPopup];
    self.midiRecordTrackPopup = [[S3GTrackerPopupButton alloc]
        initWithFrame:NSZeroRect pullsDown:NO];
    self.midiRecordTrackPopup.s3gUsesCanvasMenu = YES;
    self.midiRecordTrackPopup.target = self;
    self.midiRecordTrackPopup.action = @selector(midiRecordTrackChanged:);
    self.midiRecordTrackPopup.accessibilityLabel = @"MIDI recording lane";
    self.midiRecordTrackPopup.toolTip = @"Choose the fixed lane that receives MIDI recording; editing selection remains independent";
    [self.midiRecordTrackPopup.widthAnchor
        constraintEqualToConstant:50.0].active = YES;
    [transportPrimary addArrangedSubview:self.midiRecordTrackPopup];

    self.zoomOutButton = [self button:@"−" action:@selector(zoomOutPressed:)];
    self.zoomOutButton.accessibilityLabel = @"Zoom Tracker out";
    self.zoomOutButton.toolTip = @"Zoom the Tracker spreadsheet out";
    [self.zoomOutButton.widthAnchor constraintEqualToConstant:26.0].active = YES;
    [inputPrimary addArrangedSubview:self.zoomOutButton];
    self.zoomActualButton = [self button:@"100%"
        action:@selector(zoomActualPressed:)];
    self.zoomActualButton.accessibilityLabel = @"100 percent Tracker zoom";
    self.zoomActualButton.toolTip = @"Show the Tracker spreadsheet at actual 100 percent size";
    [self.zoomActualButton.widthAnchor constraintEqualToConstant:44.0].active = YES;
    [inputPrimary addArrangedSubview:self.zoomActualButton];
    self.zoomInButton = [self button:@"+" action:@selector(zoomInPressed:)];
    self.zoomInButton.accessibilityLabel = @"Zoom Tracker in";
    self.zoomInButton.toolTip = @"Zoom the Tracker spreadsheet in";
    [self.zoomInButton.widthAnchor constraintEqualToConstant:26.0].active = YES;
    [inputPrimary addArrangedSubview:self.zoomInButton];
    [self constrainToolboxControlHeights:@[
        patternPrimary, transportPrimary, inputPrimary,
    ]];

    self.gridView = [[S3GTrackerGridView alloc] initWithState:self.trackerState
        owner:self];
    self.gridScroll = [[S3GTrackerGridScrollView alloc]
        initWithFrame:NSZeroRect];
    self.gridScroll.hasVerticalScroller = YES;
    self.gridScroll.hasHorizontalScroller = YES;
    self.gridScroll.autohidesScrollers = YES;
    self.gridScroll.scrollerStyle = NSScrollerStyleOverlay;
    self.gridScroll.scrollerKnobStyle = NSScrollerKnobStyleLight;
    self.gridScroll.borderType = NSNoBorder;
    self.gridScroll.backgroundColor = S3GTrackerThemeColor(
        S3GTrackerThemeRole::Workspace);
    self.gridScroll.allowsMagnification = YES;
    self.gridScroll.minMagnification = 0.55;
    self.gridScroll.maxMagnification = 1.80;
    self.gridScroll.magnification = static_cast<CGFloat>(
        s3g::tracker::app::kTrackerDefaultMagnification);
    self.gridScroll.documentView = self.gridView;
    self.rowGutterView = [[S3GTrackerRowGutterView alloc]
        initWithScrollView:self.gridScroll gridView:self.gridView];
    self.gridScroll.frozenRowGutter = self.rowGutterView;
    [self.gridScroll addSubview:self.rowGutterView
        positioned:NSWindowAbove relativeTo:self.gridScroll.contentView];
    self.gridScroll.translatesAutoresizingMaskIntoConstraints = NO;
    [root addSubview:self.gridScroll];

    self.geometryWindowController = [[S3GTrackerGeometryWindowController alloc]
        initWithState:self.trackerState owner:self];
    self.geometryView = self.geometryWindowController.geometryView;
    self.burstView = [[S3GTrackerGeometryView alloc]
        initWithState:self.trackerState owner:self];
    self.burstView.burstLibraryOnly = YES;
    self.burstView.geometryViewMode = S3GTrackerGeometryViewModeBurst;
    [self.burstView.viewModePopup selectItemAtIndex:
        S3GTrackerGeometryViewModeBurst];
    self.burstView.accessibilityLabel = @"Burst editor";
    self.burstView.viewModePopup.accessibilityLabel = @"Burst library view";
    self.burstView.viewModePopup.enabled = NO;
    self.geometryView.viewModePopup.itemArray[
        S3GTrackerGeometryViewModeBurst].hidden = YES;
    for (NSUInteger index = 0u;
         index < self.burstView.viewModePopup.itemArray.count; ++index)
        self.burstView.viewModePopup.itemArray[index].hidden
            = index != S3GTrackerGeometryViewModeBurst;
    self.reshapeWindowController = [[S3GTrackerReshapeWindowController alloc]
        initWithState:self.trackerState callbacks:self.trackerCallbacks];
    self.warpWindowController = [[S3GTrackerWarpWindowController alloc]
        initWithState:self.trackerState callbacks:self.trackerCallbacks];
    self.phraseView = [[S3GTrackerPhraseView alloc]
        initWithState:self.trackerState callbacks:self.trackerCallbacks];
    self.envelopeView = [[S3GTrackerEnvelopeView alloc]
        initWithState:self.trackerState owner:self];
    self.envelopeView.translatesAutoresizingMaskIntoConstraints = NO;
    [root addSubview:self.envelopeView];

    self.consolePanel = [[S3GTrackerPanelView alloc] initWithFrame:NSZeroRect];
    self.consolePanel.translatesAutoresizingMaskIntoConstraints = NO;
    [root addSubview:self.consolePanel];
    NSTextField* consoleTitle = [self label:@"LIVE CODE"
        size:8.5 color:trackerColor(0xa8a8a8)];
    consoleTitle.translatesAutoresizingMaskIntoConstraints = NO;
    [self.consolePanel addSubview:consoleTitle];

    NSTextField* prompt = [self label:@":" size:12.0
        color:trackerColor(0xb8b8b8)];
    prompt.translatesAutoresizingMaskIntoConstraints = NO;
    [self.consolePanel addSubview:prompt];
    self.consoleInput = [[NSTextField alloc] initWithFrame:NSZeroRect];
    S3GTrackerStyleTextField(self.consoleInput, NSTextAlignmentLeft);
    if (self.consoleInput.font) {
        self.consoleInput.font = [NSFont fontWithDescriptor:
            self.consoleInput.font.fontDescriptor
            size:self.consoleInput.font.pointSize + 2.0];
    }
    self.consoleInput.placeholderString = @"kit superior compact | @k x---x--- | @h eu 7 16";
    self.consoleInput.accessibilityLabel = @"Live command input";
    self.consoleInput.delegate = self;
    self.consoleInput.target = self;
    self.consoleInput.action = @selector(consoleSubmitted:);
    self.consoleInput.translatesAutoresizingMaskIntoConstraints = NO;
    [self.consolePanel addSubview:self.consoleInput];

    self.consolePageRoot = [[S3GTrackerFocusReleaseView alloc]
        initWithFrame:NSMakeRect(0.0, 0.0, 1320.0, 780.0)];
    self.consolePageRoot.wantsLayer = YES;
    self.consolePageRoot.layer.backgroundColor = S3GTrackerThemeColor(
        S3GTrackerThemeRole::Canvas).CGColor;
    self.consoleOutputPanel = [[S3GTrackerToolboxView alloc]
        initWithFrame:NSZeroRect];
    self.consoleOutputPanel.translatesAutoresizingMaskIntoConstraints = NO;
    self.consoleOutputPanel.accessibilityElement = YES;
    self.consoleOutputPanel.accessibilityRole = NSAccessibilityGroupRole;
    self.consoleOutputPanel.accessibilityLabel = @"Console output page";
    self.consoleOutputPanel.toolboxIndex = 0;
    self.consoleOutputPanel.toolboxTitle = @"CONSOLE / LIVE CODE";
    [self.consolePageRoot addSubview:self.consoleOutputPanel];
    NSTextField* pagePrompt = [self label:@":" size:12.0
        color:trackerColor(0xb8b8b8)];
    pagePrompt.translatesAutoresizingMaskIntoConstraints = NO;
    [self.consoleOutputPanel addSubview:pagePrompt];
    self.consolePageInput = [[NSTextField alloc] initWithFrame:NSZeroRect];
    S3GTrackerStyleTextField(self.consolePageInput, NSTextAlignmentLeft);
    self.consolePageInput.placeholderString =
        @"Live Code remains available when this Console is detached";
    self.consolePageInput.accessibilityLabel = @"Console live command input";
    self.consolePageInput.delegate = self;
    self.consolePageInput.target = self;
    self.consolePageInput.action = @selector(consoleSubmitted:);
    self.consolePageInput.translatesAutoresizingMaskIntoConstraints = NO;
    [self.consoleOutputPanel addSubview:self.consolePageInput];
    NSScrollView* outputScroll = [[NSScrollView alloc] initWithFrame:NSZeroRect];
    outputScroll.hasVerticalScroller = YES;
    outputScroll.autohidesScrollers = YES;
    outputScroll.scrollerStyle = NSScrollerStyleOverlay;
    outputScroll.scrollerKnobStyle = NSScrollerKnobStyleLight;
    outputScroll.borderType = NSNoBorder;
    outputScroll.translatesAutoresizingMaskIntoConstraints = NO;
    self.consoleOutput = [[NSTextView alloc]
        initWithFrame:NSMakeRect(0.0, 0.0, 900.0, 64.0)];
    self.consoleOutput.editable = NO;
    self.consoleOutput.selectable = YES;
    self.consoleOutput.verticallyResizable = YES;
    self.consoleOutput.horizontallyResizable = NO;
    self.consoleOutput.autoresizingMask = NSViewWidthSizable;
    self.consoleOutput.minSize = NSMakeSize(0.0, 0.0);
    self.consoleOutput.maxSize = NSMakeSize(CGFLOAT_MAX, CGFLOAT_MAX);
    self.consoleOutput.textContainer.widthTracksTextView = YES;
    self.consoleOutput.textContainer.containerSize =
        NSMakeSize(CGFLOAT_MAX, CGFLOAT_MAX);
    self.consoleOutput.font = trackerFont(8.5);
    self.consoleOutput.textColor = S3GTrackerThemeColor(
        S3GTrackerThemeRole::TextSecondary);
    self.consoleOutput.backgroundColor = S3GTrackerThemeColor(
        S3GTrackerThemeRole::Canvas);
    self.consoleOutput.textContainerInset = NSMakeSize(5.0, 3.0);
    self.consoleOutput.accessibilityLabel = @"Console printed messages";
    outputScroll.documentView = self.consoleOutput;
    [self.consoleOutputPanel addSubview:outputScroll];

    self.envelopeHeightConstraint =
        [self.envelopeView.heightAnchor constraintEqualToConstant:140.0];

    [NSLayoutConstraint activateConstraints:@[
        [self.toolbar.leadingAnchor constraintEqualToAnchor:root.leadingAnchor],
        [self.toolbar.trailingAnchor constraintEqualToAnchor:root.trailingAnchor],
        [self.toolbar.topAnchor constraintEqualToAnchor:root.topAnchor],
        [self.toolbar.heightAnchor constraintEqualToConstant:
            s3g::tracker::app::kWorkspaceToolbarHeight],
        [self.transportScroll.leadingAnchor constraintEqualToAnchor:self.toolbar.leadingAnchor],
        [self.transportScroll.trailingAnchor constraintEqualToAnchor:self.toolbar.trailingAnchor],
        [self.transportScroll.topAnchor constraintEqualToAnchor:self.toolbar.topAnchor constant:9.0],
        [self.transportScroll.heightAnchor constraintEqualToConstant:51.0],

        [self.consolePanel.leadingAnchor constraintEqualToAnchor:root.leadingAnchor],
        [self.consolePanel.trailingAnchor constraintEqualToAnchor:root.trailingAnchor],
        [self.consolePanel.topAnchor constraintEqualToAnchor:self.toolbar.bottomAnchor constant:1.0],
        [self.consolePanel.heightAnchor constraintEqualToConstant:
            s3g::tracker::app::kWorkspaceConsoleInputHeight],
        [self.gridScroll.leadingAnchor constraintEqualToAnchor:root.leadingAnchor],
        [self.gridScroll.trailingAnchor constraintEqualToAnchor:root.trailingAnchor],
        [self.gridScroll.topAnchor constraintEqualToAnchor:self.consolePanel.bottomAnchor constant:1.0],
        [self.gridScroll.bottomAnchor constraintEqualToAnchor:self.envelopeView.topAnchor constant:-1.0],
        [self.envelopeView.leadingAnchor constraintEqualToAnchor:root.leadingAnchor],
        [self.envelopeView.trailingAnchor constraintEqualToAnchor:root.trailingAnchor],
        [self.envelopeView.bottomAnchor constraintEqualToAnchor:
            self.transportPanel.topAnchor constant:-6.0],
        self.envelopeHeightConstraint,
        [self.transportPanel.leadingAnchor constraintEqualToAnchor:
            root.leadingAnchor constant:18.0],
        [self.transportPanel.trailingAnchor constraintEqualToAnchor:
            root.trailingAnchor constant:-18.0],
        [self.transportPanel.bottomAnchor constraintEqualToAnchor:
            root.bottomAnchor constant:-12.0],
        [self.transportPanel.heightAnchor constraintEqualToConstant:51.0],

        [consoleTitle.leadingAnchor constraintEqualToAnchor:self.consolePanel.leadingAnchor constant:12.0],
        [consoleTitle.centerYAnchor constraintEqualToAnchor:self.consolePanel.centerYAnchor],
        [consoleTitle.widthAnchor constraintEqualToConstant:56.0],
        [prompt.leadingAnchor constraintEqualToAnchor:consoleTitle.trailingAnchor constant:3.0],
        [prompt.centerYAnchor constraintEqualToAnchor:self.consoleInput.centerYAnchor],
        [prompt.widthAnchor constraintEqualToConstant:12.0],
        [self.consoleInput.leadingAnchor constraintEqualToAnchor:prompt.trailingAnchor constant:2.0],
        [self.consoleInput.trailingAnchor constraintEqualToAnchor:self.consolePanel.trailingAnchor constant:-10.0],
        [self.consoleInput.centerYAnchor constraintEqualToAnchor:self.consolePanel.centerYAnchor],
        [self.consoleInput.heightAnchor constraintEqualToConstant:24.0],

        [self.consoleOutputPanel.leadingAnchor constraintEqualToAnchor:
            self.consolePageRoot.leadingAnchor constant:
                s3g::gui_layout::kTrackerPageHorizontalInset],
        [self.consoleOutputPanel.trailingAnchor constraintEqualToAnchor:
            self.consolePageRoot.trailingAnchor constant:
                -s3g::gui_layout::kTrackerPageHorizontalInset],
        [self.consoleOutputPanel.topAnchor constraintEqualToAnchor:
            self.consolePageRoot.topAnchor constant:
                s3g::gui_layout::kTrackerPageContentTop],
        [self.consoleOutputPanel.bottomAnchor constraintEqualToAnchor:
            self.consolePageRoot.bottomAnchor constant:
                -s3g::gui_layout::kTrackerPageBottomInset],
        [pagePrompt.leadingAnchor constraintEqualToAnchor:self.consoleOutputPanel.leadingAnchor constant:12.0],
        [pagePrompt.centerYAnchor constraintEqualToAnchor:self.consolePageInput.centerYAnchor],
        [pagePrompt.widthAnchor constraintEqualToConstant:12.0],
        [self.consolePageInput.leadingAnchor constraintEqualToAnchor:pagePrompt.trailingAnchor constant:2.0],
        [self.consolePageInput.trailingAnchor constraintEqualToAnchor:self.consoleOutputPanel.trailingAnchor constant:-10.0],
        [self.consolePageInput.topAnchor constraintEqualToAnchor:
            self.consoleOutputPanel.topAnchor constant:
                s3g::gui_layout::kStandardMetrics.firstRowOffset - 1.0],
        [self.consolePageInput.heightAnchor constraintEqualToConstant:25.0],
        [outputScroll.leadingAnchor constraintEqualToAnchor:self.consoleOutputPanel.leadingAnchor constant:8.0],
        [outputScroll.trailingAnchor constraintEqualToAnchor:self.consoleOutputPanel.trailingAnchor constant:-8.0],
        [outputScroll.topAnchor constraintEqualToAnchor:self.consolePageInput.bottomAnchor constant:7.0],
        [outputScroll.bottomAnchor constraintEqualToAnchor:self.consoleOutputPanel.bottomAnchor constant:-8.0],
    ]];
    [self appendConsoleMessage:"Native console ready. Try kit superior compact, @k x---x---, or help."
        error:NO];
    [self reloadModel];
}

- (void)viewWillLayout
{
    [super viewWillLayout];
    const auto metrics = s3g::tracker::app::workspaceLayoutMetrics(
        NSWidth(self.view.bounds), NSHeight(self.view.bounds));
    self.envelopeHeightConstraint.constant =
        static_cast<CGFloat>(metrics.envelopeHeight);
}

- (void)viewDidLayout
{
    [super viewDidLayout];
    [self resizeHorizontalStrip:self.transportControls
        inScrollView:self.transportScroll];
    const auto* displayedPattern = playbackFollowPattern(self.trackerState);
    const std::size_t lanes = displayedPattern
        ? std::min<std::size_t>(s3g::tracker::kMaximumTrackCount,
            displayedPattern->tracks.size()) : 0u;
    const CGFloat width = static_cast<CGFloat>(
        s3g::tracker::app::trackerDocumentWidth(lanes,
            NSWidth(self.gridScroll.contentView.bounds),
            self.trackerState
                && self.trackerState->sequenceColumnsExpanded));
    const CGFloat height = kGridHeaderHeight
        + static_cast<CGFloat>(playbackFollowVisibleRows(self.trackerState))
            * kGridRowHeight;
    self.gridView.frame = NSMakeRect(0.0, 0.0, width,
        std::max(height, NSHeight(self.gridScroll.contentView.bounds)));
    [self.rowGutterView refreshFrameAndDisplay];
}

- (void)refreshPlaybackFollowControls
{
    auto* state = self.trackerState;
    if (!state || !self.patternPopup) return;
    NSString* targetPatternId = nsString(playbackFollowPatternId(state));
    NSString* currentPatternId =
        self.patternPopup.selectedItem.representedObject;
    const BOOL patternChanged = ![currentPatternId
        isEqualToString:targetPatternId];
    if (patternChanged) {
        for (NSInteger index = 0;
             index < self.patternPopup.numberOfItems; ++index) {
            NSMenuItem* item = [self.patternPopup itemAtIndex:index];
            if ([item.representedObject isEqualToString:targetPatternId]) {
                [self.patternPopup selectItemAtIndex:index];
                break;
            }
        }
    }

    const BOOL editable = !state->songPlaybackActive;
    const BOOL editStateChanged = self.patternPopup.enabled
        != (self.patternPopup.numberOfItems > 0u && editable);
    self.patternPopup.enabled = self.patternPopup.numberOfItems > 0u
        && editable;
    const bool patternBankCanGrow = state->patternBank.entries.size()
        < s3g::tracker::kMaximumPatternBankEntries;
    self.createPatternButton.enabled = patternBankCanGrow && editable;
    self.duplicatePatternButton.enabled = patternBankCanGrow
        && !state->patternBank.entries.empty() && editable;
    self.renamePatternButton.enabled = !state->patternBank.entries.empty()
        && editable;
    self.deletePatternButton.enabled = state->patternBank.entries.size() > 1u
        && editable;
    self.trackAddButton.enabled = editable;
    self.trackRemoveButton.enabled = editable
        && !state->session.pattern.tracks.empty();
    self.undoButton.enabled = state->canUndo && editable;
    self.redoButton.enabled = state->canRedo && editable;
    self.midiStepRecordPopup.enabled = state->midiStepInputAvailable
        && editable;
    self.midiRecordTrackPopup.enabled = state->midiStepInputAvailable
        && !state->session.pattern.tracks.empty() && editable;

    if (patternChanged || editStateChanged) {
        [self.gridView clearGridSelection];
        [self.gridView setNeedsDisplay:YES];
        [self.envelopeView setNeedsDisplay:YES];
        [self.envelopeView.playbackOverlay setNeedsDisplay:YES];
        [self.view setNeedsLayout:YES];
        [self.gridView refreshAccessibilityValue];
    }
}

- (void)refreshMidiRecordTrackMenu
{
    auto* state = self.trackerState;
    if (!state || !self.midiRecordTrackPopup) return;
    [self.midiRecordTrackPopup removeAllItems];
    const auto& tracks = state->session.pattern.tracks;
    if (tracks.empty()) {
        state->midiRecordTrack = 0u;
        [self.midiRecordTrackPopup addItemWithTitle:@"NO REC LANE"];
        self.midiRecordTrackPopup.lastItem.representedObject = @(0u);
        self.midiRecordTrackPopup.s3gDisplayTitle = @"L—";
        [self.midiRecordTrackPopup setNeedsDisplay:YES];
        return;
    }
    state->midiRecordTrack = std::min(
        state->midiRecordTrack, tracks.size() - 1u);
    for (std::size_t lane = 0u; lane < tracks.size(); ++lane) {
        const std::string fallback = "LANE " + std::to_string(lane + 1u);
        NSString* name = nsString(tracks[lane].name.empty()
            ? fallback : tracks[lane].name);
        [self.midiRecordTrackPopup addItemWithTitle:[NSString
            stringWithFormat:@"REC L%02lu · %@",
            static_cast<unsigned long>(lane + 1u), name]];
        self.midiRecordTrackPopup.lastItem.representedObject = @(lane);
    }
    [self.midiRecordTrackPopup selectItemAtIndex:
        static_cast<NSInteger>(state->midiRecordTrack)];
    self.midiRecordTrackPopup.s3gDisplayTitle = [NSString stringWithFormat:
        @"L%02lu", static_cast<unsigned long>(state->midiRecordTrack + 1u)];
    const auto& armed = tracks[state->midiRecordTrack];
    NSString* armedName = nsString(armed.name.empty()
        ? "LANE " + std::to_string(state->midiRecordTrack + 1u)
        : armed.name);
    self.midiRecordTrackPopup.toolTip = [NSString stringWithFormat:
        @"Recording destination L%02lu · %@; editing selection remains independent",
        static_cast<unsigned long>(state->midiRecordTrack + 1u), armedName];
    [self.midiRecordTrackPopup setNeedsDisplay:YES];
}

- (void)refreshTransportValueMenus
{
    if (!self.trackerState) return;
    const double gate = std::clamp(
        self.trackerState->session.gateMilliseconds, 1.0, 5000.0);
    [self.gateField removeAllItems];
    NSInteger gateSelection = -1;
    for (const double choice : kGateMilliseconds) {
        NSString* title = choice >= 1000.0
            ? [NSString stringWithFormat:@"%.1fs", choice / 1000.0]
            : [NSString stringWithFormat:@"%g MS", choice];
        [self.gateField addItemWithTitle:title];
        self.gateField.lastItem.representedObject = @(choice);
        if (std::abs(choice - gate) < 0.0001)
            gateSelection = self.gateField.numberOfItems - 1;
    }
    if (gateSelection < 0) {
        [self.gateField addItemWithTitle:[NSString stringWithFormat:
            @"%g MS", gate]];
        self.gateField.lastItem.representedObject = @(gate);
        gateSelection = self.gateField.numberOfItems - 1;
    }
    [self.gateField selectItemAtIndex:gateSelection];

    const NSInteger maximumRow = static_cast<NSInteger>(
        maximumLoadedPatternRows(self.trackerState));
    const NSInteger loopStart = std::clamp<NSInteger>(
        static_cast<NSInteger>(
            self.trackerState->session.transport.loopStartRow) + 1,
        1, maximumRow);
    const NSInteger loopEnd = std::clamp<NSInteger>(
        static_cast<NSInteger>(
            self.trackerState->session.transport.loopEndRow),
        loopStart, maximumRow);
    [self.loopStartField removeAllItems];
    for (NSInteger row = 1; row <= loopEnd; ++row) {
        [self.loopStartField addItemWithTitle:[NSString stringWithFormat:
            @"%02ld", static_cast<long>(row)]];
        self.loopStartField.lastItem.representedObject = @(row);
    }
    [self.loopStartField selectItemWithTitle:[NSString stringWithFormat:
        @"%02ld", static_cast<long>(loopStart)]];

    [self.loopEndField removeAllItems];
    for (NSInteger row = loopStart; row <= maximumRow; ++row) {
        [self.loopEndField addItemWithTitle:[NSString stringWithFormat:
            @"%02ld", static_cast<long>(row)]];
        self.loopEndField.lastItem.representedObject = @(row);
    }
    [self.loopEndField selectItemWithTitle:[NSString stringWithFormat:
        @"%02ld", static_cast<long>(loopEnd)]];
}

- (void)reloadModel
{
    auto* state = self.trackerState;
    if (!state || !self.isViewLoaded) return;
    if (state->session.pattern.tracks.empty()) {
        state->session.selectedTrack = 0u;
        state->midiRecordTrack = 0u;
    } else {
        state->session.selectedTrack = std::min(state->session.selectedTrack,
            state->session.pattern.tracks.size() - 1u);
        state->midiRecordTrack = std::min(state->midiRecordTrack,
            state->session.pattern.tracks.size() - 1u);
    }
    state->session.selectedRow = std::min<std::size_t>(
        state->session.selectedRow, 255u);
    state->session.selectedPage = 0u;
    state->session.selectedField = std::min<std::size_t>(
        state->session.selectedField,
        gridFieldCount(state->sequenceColumnsExpanded) - 1u);
    state->tempoScale = kTempoScales[nearestTempoScaleIndex(
        std::isfinite(state->tempoScale) ? state->tempoScale : 1.0)];
    state->trackerRowJump = std::clamp<uint32_t>(
        state->trackerRowJump, 1u, 16u);
    state->mainOutputGain = std::clamp(
        std::isfinite(state->mainOutputGain) ? state->mainOutputGain : 1.0f,
        0.0f, 1.0f);
    state->mixerSelectedStrip = std::min(state->mixerSelectedStrip,
        state->session.pattern.tracks.size());
    [self refreshTransportValueMenus];
    [self refreshMidiRecordTrackMenu];
    [self.patternPopup removeAllItems];
    NSInteger activePattern = -1;
    for (const auto& entry : state->patternBank.entries) {
        NSString* patternId = nsString(entry.id);
        NSString* displayName = nsString(entry.pattern.name);
        NSString* title = displayName.length > 0u
            ? [NSString stringWithFormat:@"%@ · %@", patternId, displayName]
            : patternId;
        [self.patternPopup addItemWithTitle:title];
        self.patternPopup.lastItem.representedObject = patternId;
        if (entry.id == state->patternBank.activePatternId)
            activePattern = self.patternPopup.numberOfItems - 1;
    }
    if (activePattern >= 0)
        [self.patternPopup selectItemAtIndex:activePattern];
    self.patternPopup.enabled = self.patternPopup.numberOfItems > 0u
        && !state->songPlaybackActive;
    const bool patternBankCanGrow = state->patternBank.entries.size()
        < s3g::tracker::kMaximumPatternBankEntries;
    self.createPatternButton.enabled = patternBankCanGrow
        && !state->songPlaybackActive;
    self.duplicatePatternButton.enabled = patternBankCanGrow
        && !state->patternBank.entries.empty()
        && !state->songPlaybackActive;
    self.renamePatternButton.enabled = !state->patternBank.entries.empty()
        && !state->songPlaybackActive;
    self.deletePatternButton.enabled = state->patternBank.entries.size() > 1u
        && !state->songPlaybackActive;
    self.sequenceColumnsButton.title = state->sequenceColumnsExpanded
        ? @"COLLAPSE DETAIL" : @"EXPAND DETAIL";
    self.sequenceColumnsButton.toolTip = state->sequenceColumnsExpanded
        ? @"Hide sequencing columns and keep NOTE and VOL visible"
        : @"Show SEQ1, V1, SEQ2, V2, and GATE in every tracker lane";
    self.sequenceColumnsButton.accessibilityLabel =
        state->sequenceColumnsExpanded
            ? @"Collapse tracker sequencing columns"
            : @"Expand tracker sequencing columns";
    self.noteDisplayButton.title = state->showMidiNoteValues
        ? @"NOTE: MIDI" : @"NOTE: NAME";
    self.noteDisplayButton.toolTip = state->showMidiNoteValues
        ? @"Show NOTE cells as pitch names"
        : @"Show NOTE cells as decimal MIDI values";
    self.noteDisplayButton.accessibilityLabel = state->showMidiNoteValues
        ? @"Show notes as pitch names"
        : @"Show notes as MIDI values";
    [self.stepJumpPopup selectItemAtIndex:static_cast<NSInteger>(
        state->trackerRowJump - 1u)];
    self.midiStepRecordPopup.enabled = state->midiStepInputAvailable;
    self.midiRecordTrackPopup.enabled = state->midiStepInputAvailable
        && !state->session.pattern.tracks.empty()
        && !state->songPlaybackActive;
    [self.midiStepRecordPopup selectItemAtIndex:static_cast<NSInteger>(
        state->midiStepRecordMode)];
    self.midiStepRecordPopup.toolTip = state->midiStepInputAvailable
        ? @"Armed modes record to the fixed REC LANE target; STEP advances by View JUMP; live modes follow the written row; LIVE MT preserves each chord voice's timing in an aligned MT stack"
        : @"This build does not expose a host MIDI input";
    self.undoButton.enabled = state->canUndo;
    self.redoButton.enabled = state->canRedo;
    self.playButton.state = state->playing
        ? NSControlStateValueOn : NSControlStateValueOff;
    self.playButton.title = @"▶";
    self.playButton.accessibilityLabel = state->playing
        ? @"Pause host playback" : @"Start host playback";
    [self.playButton setNeedsDisplay:YES];
    self.loopButton.state = state->session.transport.loopEnabled
        ? NSControlStateValueOn : NSControlStateValueOff;
    [self.loopButton setNeedsDisplay:YES];
    self.fillButton.state = state->fillActive
        ? NSControlStateValueOn : NSControlStateValueOff;
    [self.fillButton setNeedsDisplay:YES];
    [self.tempoScalePopup selectItemAtIndex:static_cast<NSInteger>(
        nearestTempoScaleIndex(state->tempoScale))];
    [self.swingField setSwingValue:
        state->session.transport.swing * 100.0 hasOverride:YES];
    [self refreshPlaybackFollowControls];
    [self.gridView setNeedsDisplay:YES];
    [self.gridView refreshAccessibilityValue];
    [self.rowGutterView refreshFrameAndDisplay];
    [self applyWorkspaceMode];
    [self.geometryView setNeedsDisplay:YES];
    [self.geometryView.playbackOverlay setNeedsDisplay:YES];
    [self.burstView setNeedsDisplay:YES];
    [self.burstView.playbackOverlay setNeedsDisplay:YES];
    [self.reshapeWindowController reloadModel];
    [self.warpWindowController reloadModel];
    [self.phraseView reloadModel];
    [self.envelopeView setNeedsDisplay:YES];
    [self.envelopeView.playbackOverlay setNeedsDisplay:YES];
    [self.view setNeedsLayout:YES];
    [self.gridView scrollSelectionToVisible];
}

- (void)refreshPlaybackDisplay
{
    if (!self.isViewLoaded || !self.trackerState) return;
    [self refreshPlaybackFollowControls];
    if (viewCanPresentPlayback(self.view)) {
        self.playButton.state = self.trackerState->playing
            ? NSControlStateValueOn : NSControlStateValueOff;
        self.playButton.title = @"▶";
        self.playButton.accessibilityLabel = self.trackerState->playing
            ? @"Pause host playback" : @"Start host playback";
        [self.playButton setNeedsDisplay:YES];
        self.loopButton.state
            = self.trackerState->session.transport.loopEnabled
                ? NSControlStateValueOn : NSControlStateValueOff;
        [self.loopButton setNeedsDisplay:YES];
        [self.gridView refreshPlaybackDisplay];
        [self.envelopeView refreshPlaybackDisplay];
    }
    if (viewCanPresentPlayback(self.geometryView))
        [self.geometryView refreshPlaybackDisplay];
    if (viewCanPresentPlayback(self.burstView))
        [self.burstView refreshPlaybackDisplay];
    [self.reshapeWindowController refreshPlaybackDisplay];
    // The Warps content view is reparented into the CLAP page stack, so its
    // original controller window is not a reliable visibility signal. This
    // redraw is small and must run on every display tick for the curve marker.
    [self.warpWindowController refreshPlaybackDisplay];
}

- (void)setMidiDestinations:
    (const std::vector<s3g::tracker::MidiDestination>&)destinations
    selectedTarget:(const s3g::tracker::MidiOutputTarget&)target
{
    (void)destinations;
    (void)target;
}

- (void)setAudioOutputDevices:
    (const std::vector<s3g::tracker::app::AudioOutputDevice>&)devices
    selectedDeviceId:(uint32_t)selectedDeviceId
{
    [self.audioPopup removeAllItems];
    NSInteger selected = -1;
    for (const auto& device : devices) {
        NSString* title = [NSString stringWithFormat:@"%@%@",
            nsString(device.name), device.isDefault ? @"  [DEFAULT]" : @""];
        [self.audioPopup addItemWithTitle:title];
        self.audioPopup.lastItem.representedObject = @(device.id);
        if (device.id == selectedDeviceId)
            selected = self.audioPopup.numberOfItems - 1;
    }
    if (selected >= 0) [self.audioPopup selectItemAtIndex:selected];
    self.audioPopup.enabled = self.audioPopup.numberOfItems > 0;
}

- (void)appendConsoleMessage:(const std::string&)message error:(BOOL)isError
{
    if (!self.consoleOutput) return;
    NSString* line = [NSString stringWithFormat:@"%@%@\n",
        isError ? @"! " : @"> ", nsString(message)];
    NSDictionary* attributes = @{
        NSFontAttributeName: trackerFont(8.5),
        NSForegroundColorAttributeName: isError
            ? S3GTrackerThemeColor(S3GTrackerThemeRole::Danger)
            : S3GTrackerThemeColor(S3GTrackerThemeRole::TextSecondary),
    };
    NSAttributedString* append = [[NSAttributedString alloc]
        initWithString:line attributes:attributes];
    [self.consoleOutput.textStorage appendAttributedString:append];
    if (self.consoleOutput.string.length > 30000u)
        [self.consoleOutput.textStorage deleteCharactersInRange:
            NSMakeRange(0u, 10000u)];
    [self.consoleOutput scrollRangeToVisible:
        NSMakeRange(self.consoleOutput.string.length, 0u)];
}

- (void)focusConsole
{
    [self.view.window makeFirstResponder:self.consoleInput];
}

- (void)focusTracker
{
    [self.view.window makeFirstResponder:self.gridView];
}

- (void)editBurstSlot:(std::size_t)slot
{
    (void)self.view;
    [self.burstView selectBurstSlot:slot];
    if (self.trackerCallbacks && self.trackerCallbacks->showBurstPage)
        self.trackerCallbacks->showBurstPage();
}

- (void)applyPitchMapContour:(PitchContour)contour
    firstRow:(std::size_t)firstRow lastRow:(std::size_t)lastRow
{
    [self.geometryView applyPitchMapContour:contour
        firstRow:firstRow lastRow:lastRow];
}

- (void)openPitchMapFirstRow:(std::size_t)firstRow
    lastRow:(std::size_t)lastRow
{
    (void)self.view;
    [self.geometryView openPitchMapFirstRow:firstRow lastRow:lastRow];
    [self showGeometryWindow:nil];
}

- (void)showGeometryWindow:(id)sender
{
    if (self.trackerCallbacks && self.trackerCallbacks->showGeometryPage) {
        self.trackerCallbacks->showGeometryPage();
        return;
    }
    [self.geometryWindowController showWindow:sender];
    [self.geometryView setNeedsDisplay:YES];
    [self.geometryView.playbackOverlay setNeedsDisplay:YES];
}

- (void)showWarpWindow:(id)sender
{
    if (self.trackerCallbacks && self.trackerCallbacks->showWarpPage) {
        self.trackerCallbacks->showWarpPage();
        return;
    }
    [self.warpWindowController reloadModel];
    [self.warpWindowController showWindow:sender];
}

- (NSView*)geometryPageView
{
    (void)self.view;
    return self.geometryView;
}

- (NSView*)burstPageView
{
    (void)self.view;
    return self.burstView;
}

- (NSView*)reshapePageView
{
    (void)self.view;
    [self.reshapeWindowController reloadModel];
    return self.reshapeWindowController.window.contentView;
}

- (NSView*)phrasePageView
{
    (void)self.view;
    [self.phraseView reloadModel];
    return self.phraseView.view;
}

- (void)capturePhraseTrack:(std::size_t)track firstRow:(std::size_t)firstRow
    lastRow:(std::size_t)lastRow
{
    if ([self.phraseView captureTrack:track firstRow:firstRow lastRow:lastRow]
        && self.trackerCallbacks && self.trackerCallbacks->showPhrasePage)
        self.trackerCallbacks->showPhrasePage();
}

- (void)placeSelectedPhraseTrack:(std::size_t)track row:(std::size_t)row
    merge:(BOOL)merge
{
    [self.phraseView placeAtTrack:track row:row merge:merge];
}

- (NSView*)warpPageView
{
    (void)self.view;
    [self.warpWindowController reloadModel];
    return self.warpWindowController.window.contentView;
}

- (NSView*)consolePageView
{
    (void)self.view;
    return self.consolePageRoot;
}

- (void)applyWorkspaceMode
{
    if (self.trackerState) self.trackerState->mixerPageVisible = false;
    self.gridScroll.hidden = NO;
    self.envelopeView.hidden = NO;
    self.gridScroll.accessibilityHidden = NO;
    self.gridView.accessibilityHidden = NO;
    self.gridView.accessibilityElement = YES;
    self.envelopeView.accessibilityHidden = NO;
    NSAccessibilityPostNotification(
        self.view, NSAccessibilityLayoutChangedNotification);
    if (self.view.window) {
        NSAccessibilityPostNotification(
            self.view.window, NSAccessibilityLayoutChangedNotification);
    }
}

- (void)showMixerPage:(id)sender
{
    [self showTrackerPage:sender];
}

- (void)showTrackerPage:(id)sender
{
    (void)sender;
    if (!self.trackerState) return;
    self.trackerState->mixerPageVisible = false;
    [self applyWorkspaceMode];
    [self.view setNeedsLayout:YES];
    [self.view.window makeFirstResponder:self.gridView];
}

- (void)modulePatternChanged
{
    if (self.trackerCallbacks && self.trackerCallbacks->patternChanged)
        self.trackerCallbacks->patternChanged();
    [self reloadModel];
}

- (void)moduleTransportChanged
{
    if (self.trackerCallbacks && self.trackerCallbacks->transportChanged)
        self.trackerCallbacks->transportChanged();
    [self reloadModel];
}

- (void)moduleSelectionChanged
{
    if (self.trackerState) self.trackerState->session.selectedPage = 0u;
    if (self.trackerCallbacks && self.trackerCallbacks->selectionChanged)
        self.trackerCallbacks->selectionChanged();
    [self.gridView refreshAccessibilityValue];
    [self.gridView setNeedsDisplay:YES];
    [self.rowGutterView setNeedsDisplay:YES];
    [self.geometryView setNeedsDisplay:YES];
    [self.geometryView.playbackOverlay setNeedsDisplay:YES];
    [self.envelopeView setNeedsDisplay:YES];
    [self.envelopeView.playbackOverlay setNeedsDisplay:YES];
    [self.gridView scrollSelectionToVisible];
}

- (void)moduleTogglePlayback
{
    if (self.trackerCallbacks && self.trackerCallbacks->togglePlayback) {
        self.trackerCallbacks->togglePlayback();
    }
}

- (void)toggleSequenceColumns:(id)sender
{
    (void)sender;
    auto* state = self.trackerState;
    if (!state) return;
    state->sequenceColumnsExpanded = !state->sequenceColumnsExpanded;
    if (!state->sequenceColumnsExpanded)
        state->session.selectedField = std::min<std::size_t>(
            state->session.selectedField, 1u);
    [self.gridView clearGridSelection];
    [self reloadModel];
    NSAccessibilityPostNotification(
        self.gridView, NSAccessibilityLayoutChangedNotification);
}

- (void)toggleNoteDisplay:(id)sender
{
    (void)sender;
    auto* state = self.trackerState;
    if (!state) return;
    state->showMidiNoteValues = !state->showMidiNoteValues;
    if (self.trackerCallbacks
        && self.trackerCallbacks->viewPreferencesChanged)
        self.trackerCallbacks->viewPreferencesChanged();
    [self reloadModel];
}

- (void)stepJumpChanged:(id)sender
{
    (void)sender;
    auto* state = self.trackerState;
    if (!state) return;
    NSNumber* selected = self.stepJumpPopup.selectedItem.representedObject;
    state->trackerRowJump = static_cast<uint32_t>(std::clamp<NSInteger>(
        selected ? selected.integerValue : 1, 1, 16));
    if (self.trackerCallbacks
        && self.trackerCallbacks->viewPreferencesChanged)
        self.trackerCallbacks->viewPreferencesChanged();
    [self.gridView refreshAccessibilityValue];
}

- (void)midiStepRecordModeChanged:(id)sender
{
    (void)sender;
    auto* state = self.trackerState;
    if (!state || state->songPlaybackActive
        || !state->midiStepInputAvailable) return;
    state->midiStepRecordMode = static_cast<MidiStepRecordMode>(
        std::clamp<NSInteger>(self.midiStepRecordPopup.indexOfSelectedItem,
            0, 3));
    if (self.trackerCallbacks
        && self.trackerCallbacks->midiStepRecordModeChanged) {
        self.trackerCallbacks->midiStepRecordModeChanged(
            state->midiStepRecordMode);
    }
    const char* mode = state->midiStepRecordMode
            == MidiStepRecordMode::Step
        ? "STEP" : state->midiStepRecordMode
            == MidiStepRecordMode::LiveQuantized
        ? "LIVE Q" : state->midiStepRecordMode
            == MidiStepRecordMode::LiveUnquantized ? "LIVE MT" : "OFF";
    [self appendConsoleMessage:std::string("MIDI recording ") + mode
        error:NO];
    [self reloadModel];
}

- (void)midiRecordTrackChanged:(id)sender
{
    (void)sender;
    auto* state = self.trackerState;
    if (!state || state->songPlaybackActive
        || state->session.pattern.tracks.empty()) return;
    NSNumber* selected = self.midiRecordTrackPopup.selectedItem.representedObject;
    const auto requested = selected
        ? static_cast<std::size_t>(selected.unsignedIntegerValue) : 0u;
    state->midiRecordTrack = std::min(
        requested, state->session.pattern.tracks.size() - 1u);
    if (self.trackerCallbacks
        && self.trackerCallbacks->midiRecordTrackChanged) {
        self.trackerCallbacks->midiRecordTrackChanged(
            state->midiRecordTrack);
    }
    const auto& track = state->session.pattern.tracks[state->midiRecordTrack];
    const std::string name = track.name.empty()
        ? "LANE " + std::to_string(state->midiRecordTrack + 1u)
        : track.name;
    [self appendConsoleMessage:"MIDI record lane L"
        + (state->midiRecordTrack + 1u < 10u ? std::string("0")
                                             : std::string())
        + std::to_string(state->midiRecordTrack + 1u) + " · " + name
        error:NO];
    [self.gridView setNeedsDisplay:YES];
    [self reloadModel];
}

- (void)assignTrackInstrument:(uint32_t)nodeId
{
    auto* state = self.trackerState;
    if (!state || state->songPlaybackActive
        || state->session.pattern.tracks.empty()
        || !s3g::tracker::rackInstrument(
            state->instrumentRack, nodeId)) return;
    state->selectedRackInstrument = nodeId;
    if (nodeId < s3g::tracker::kMembraneRackSlotCount)
        state->instrumentRack.selectedNode = nodeId;
    const auto lane = std::min(state->session.selectedTrack,
        state->session.pattern.tracks.size() - 1u);
    auto& track = state->session.pattern.tracks[lane];
    track.initialInstrumentNodeId = nodeId;
    track.destination = s3g::tracker::destinationForInstrument(
        nodeId, EventDestination::None);
    [self modulePatternChanged];
}

- (void)editRackInstrument:(uint32_t)nodeId
{
    auto* state = self.trackerState;
    const auto* instrument = state
        ? s3g::tracker::rackInstrument(state->instrumentRack, nodeId)
        : nullptr;
    if (!instrument) return;
    state->selectedRackInstrument = nodeId;
    if (nodeId < s3g::tracker::kMembraneRackSlotCount)
        state->instrumentRack.selectedNode = nodeId;
    if (self.trackerCallbacks && self.trackerCallbacks->editRackInstrument)
        self.trackerCallbacks->editRackInstrument(nodeId);
}

- (void)addInstrumentKind:(s3g::tracker::InstrumentKind)kind
{
    auto* state = self.trackerState;
    if (!state) return;
    std::size_t rackIndex = 0u;
    uint32_t nodeId = s3g::tracker::kInvalidInstrumentNode;
    if (!s3g::tracker::addInstrumentInstance(state->instrumentRack, kind,
            &rackIndex, &nodeId)) {
        NSBeep();
        return;
    }
    state->selectedRackInstrument = nodeId;
    if (kind == s3g::tracker::InstrumentKind::MembraneKick)
        state->instrumentRack.selectedNode = nodeId;
    if (self.trackerCallbacks
        && self.trackerCallbacks->instrumentRackChanged) {
        self.trackerCallbacks->instrumentRackChanged();
    }
    const auto* instrument = s3g::tracker::rackInstrumentAt(
        state->instrumentRack, rackIndex);
    [self appendConsoleMessage:instrument
            ? "Added instrument " + std::to_string(rackIndex) + " • "
                + std::string(instrument->name)
            : "Instrument instance added"
        error:NO];
    [self reloadModel];
}

- (void)setTrackerMagnification:(CGFloat)magnification
{
    if (!self.gridScroll) return;
    const CGFloat value = std::clamp(magnification,
        self.gridScroll.minMagnification, self.gridScroll.maxMagnification);
    const NSRect visible = self.gridScroll.documentVisibleRect;
    [self.gridScroll setMagnification:value centeredAtPoint:
        NSMakePoint(NSMidX(visible), NSMidY(visible))];
    self.zoomActualButton.title = [NSString stringWithFormat:@"%ld%%",
        static_cast<long>(std::lround(value * 100.0))];
    [self.rowGutterView refreshFrameAndDisplay];
    [self.gridView scrollSelectionToVisible];
}

- (void)zoomTrackerIn
{
    [self setTrackerMagnification:self.gridScroll.magnification * 1.16];
}

- (void)zoomTrackerOut
{
    [self setTrackerMagnification:self.gridScroll.magnification / 1.16];
}

- (void)zoomOutPressed:(id)sender
{
    (void)sender;
    [self zoomTrackerOut];
}

- (void)zoomActualPressed:(id)sender
{
    (void)sender;
    [self setTrackerMagnification:1.0];
}

- (void)zoomInPressed:(id)sender
{
    (void)sender;
    [self zoomTrackerIn];
}

- (void)resetTrackerZoom
{
    [self setTrackerMagnification:static_cast<CGFloat>(
        s3g::tracker::app::kTrackerDefaultMagnification)];
}

- (void)moduleFocusConsole { [self focusConsole]; }

- (void)tempoScaleChanged:(id)sender
{
    (void)sender;
    if (!self.trackerState) return;
    NSNumber* value = self.tempoScalePopup.selectedItem.representedObject;
    if (!value) return;
    self.trackerState->tempoScale = kTempoScales[nearestTempoScaleIndex(
        value.doubleValue)];
    [self moduleTransportChanged];
}

- (void)mixerPressed:(id)sender
{
    if (self.trackerState && self.trackerState->mixerPageVisible)
        [self showTrackerPage:sender];
    else
        [self showMixerPage:sender];
}

- (void)trackAddPressed:(id)sender
{
    (void)sender;
    if (!self.trackerState || self.trackerState->songPlaybackActive) return;
    if (self.trackerCallbacks && self.trackerCallbacks->executeCommand)
        self.trackerCallbacks->executeCommand("track add");
}

- (void)trackRemovePressed:(id)sender
{
    (void)sender;
    auto* state = self.trackerState;
    if (!state || state->songPlaybackActive
        || state->session.pattern.tracks.empty()) return;
    const auto lane = std::min(state->session.selectedTrack,
        state->session.pattern.tracks.size() - 1u);
    if (self.trackerCallbacks && self.trackerCallbacks->executeCommand)
        self.trackerCallbacks->executeCommand(
            "track remove " + std::to_string(lane + 1u));
}

- (void)undoPressed:(id)sender
{
    (void)sender;
    if (!self.trackerState || self.trackerState->songPlaybackActive) return;
    if (self.trackerCallbacks && self.trackerCallbacks->executeCommand)
        self.trackerCallbacks->executeCommand("undo");
}

- (void)redoPressed:(id)sender
{
    (void)sender;
    if (!self.trackerState || self.trackerState->songPlaybackActive) return;
    if (self.trackerCallbacks && self.trackerCallbacks->executeCommand)
        self.trackerCallbacks->executeCommand("redo");
}

- (void)playPressed:(id)sender
{
    (void)sender;
    [self moduleTogglePlayback];
}

- (void)patternSelectionChanged:(NSPopUpButton*)sender
{
    NSString* patternId = sender.selectedItem.representedObject;
    if (![patternId isKindOfClass:NSString.class]
        || patternId.length == 0u || !self.trackerCallbacks
        || !self.trackerCallbacks->selectPattern) return;
    const char* utf8 = patternId.UTF8String;
    if (utf8) self.trackerCallbacks->selectPattern(utf8);
}

- (void)newPatternPressed:(id)sender
{
    (void)sender;
    if (!self.trackerState || self.trackerState->songPlaybackActive) return;
    if (self.trackerCallbacks && self.trackerCallbacks->addPattern)
        self.trackerCallbacks->addPattern(false);
}

- (void)duplicatePatternPressed:(id)sender
{
    (void)sender;
    if (!self.trackerState || self.trackerState->songPlaybackActive) return;
    if (self.trackerCallbacks && self.trackerCallbacks->addPattern)
        self.trackerCallbacks->addPattern(true);
}

- (void)renamePatternPressed:(id)sender
{
    (void)sender;
    if (!self.trackerState || self.trackerState->songPlaybackActive) return;
    if (self.trackerCallbacks && self.trackerCallbacks->renamePattern)
        self.trackerCallbacks->renamePattern();
}

- (void)deletePatternPressed:(id)sender
{
    (void)sender;
    if (!self.trackerState || self.trackerState->songPlaybackActive) return;
    if (self.trackerCallbacks && self.trackerCallbacks->deletePattern)
        self.trackerCallbacks->deletePattern();
}

- (void)loopPressed:(id)sender
{
    (void)sender;
    if (!self.trackerState) return;
    self.trackerState->session.transport.loopEnabled
        = !self.trackerState->session.transport.loopEnabled;
    if (self.trackerCallbacks && self.trackerCallbacks->transportChanged)
        self.trackerCallbacks->transportChanged();
    [self reloadModel];
}

- (void)fillPressed:(id)sender
{
    if (!self.trackerState) return;
    NSButton* button = [sender isKindOfClass:NSButton.class]
        ? static_cast<NSButton*>(sender) : self.fillButton;
    self.trackerState->fillActive = button.state == NSControlStateValueOn;
    if (self.trackerCallbacks && self.trackerCallbacks->fillChanged)
        self.trackerCallbacks->fillChanged(self.trackerState->fillActive);
    [self.fillButton setNeedsDisplay:YES];
}

- (void)restartPressed:(id)sender
{
    (void)sender;
    if (self.trackerCallbacks && self.trackerCallbacks->restartPlayback)
        self.trackerCallbacks->restartPlayback();
}

- (void)panicPressed:(id)sender
{
    (void)sender;
    if (self.trackerCallbacks && self.trackerCallbacks->panic)
        self.trackerCallbacks->panic();
}

- (void)songPressed:(id)sender
{
    (void)sender;
    if (self.trackerCallbacks && self.trackerCallbacks->showSongWindow)
        self.trackerCallbacks->showSongWindow();
}

- (void)instrumentPressed:(id)sender
{
    (void)sender;
    if (self.trackerCallbacks && self.trackerCallbacks->showInstrumentWindow)
        self.trackerCallbacks->showInstrumentWindow();
}

- (void)helpPressed:(id)sender
{
    (void)sender;
    if (self.trackerCallbacks && self.trackerCallbacks->showConsoleHelp)
        self.trackerCallbacks->showConsoleHelp();
}

- (void)geometryPressed:(id)sender
{
    [self showGeometryWindow:sender];
}

- (void)warpPressed:(id)sender
{
    [self showWarpWindow:sender];
}

- (void)refreshMidiPressed:(id)sender
{
    (void)sender;
    if (self.trackerCallbacks && self.trackerCallbacks->refreshMidiDestinations)
        self.trackerCallbacks->refreshMidiDestinations();
    if (self.trackerCallbacks && self.trackerCallbacks->refreshAudioOutputDevices)
        self.trackerCallbacks->refreshAudioOutputDevices();
}

- (void)audioOutputDeviceChanged:(id)sender
{
    (void)sender;
    if (!self.trackerCallbacks
        || !self.trackerCallbacks->selectAudioOutputDevice) return;
    NSNumber* value = self.audioPopup.selectedItem.representedObject;
    if (value) self.trackerCallbacks->selectAudioOutputDevice(
        static_cast<uint32_t>(value.unsignedIntValue));
}

- (void)transportFieldChanged:(id)sender
{
    if (!self.trackerState) return;
    auto& transport = self.trackerState->session.transport;
    if (sender == self.swingField) {
        transport.swing = std::clamp(
            self.swingField.s3gSwingValue / 100.0, 0.5, 0.75);
    } else if (sender == self.loopStartField) {
        NSNumber* value = self.loopStartField.selectedItem.representedObject;
        if (!value) return;
        const auto maximum = static_cast<NSInteger>(
            maximumLoadedPatternRows(self.trackerState));
        const uint32_t row = static_cast<uint32_t>(std::clamp<NSInteger>(
            value.integerValue, 1, maximum));
        transport.loopStartRow = std::min(row - 1u,
            transport.loopEndRow - 1u);
    } else if (sender == self.loopEndField) {
        NSNumber* value = self.loopEndField.selectedItem.representedObject;
        if (!value) return;
        const auto maximum = static_cast<NSInteger>(
            maximumLoadedPatternRows(self.trackerState));
        const uint32_t row = static_cast<uint32_t>(std::clamp<NSInteger>(
            value.integerValue, 1, maximum));
        transport.loopEndRow = std::max(row,
            transport.loopStartRow + 1u);
    } else {
        return;
    }
    if (self.trackerCallbacks && self.trackerCallbacks->transportChanged)
        self.trackerCallbacks->transportChanged();
    [self reloadModel];
}

- (void)gateFieldChanged:(id)sender
{
    (void)sender;
    if (!self.trackerState) return;
    NSNumber* value = self.gateField.selectedItem.representedObject;
    if (!value) return;
    self.trackerState->session.gateMilliseconds = std::clamp(
        value.doubleValue, 1.0, 5000.0);
    if (self.trackerCallbacks && self.trackerCallbacks->outputChanged)
        self.trackerCallbacks->outputChanged();
    [self reloadModel];
}

- (void)consoleSubmitted:(id)sender
{
    NSTextField* source = [sender isKindOfClass:NSTextField.class]
        ? static_cast<NSTextField*>(sender) : self.consoleInput;
    NSString* input = [source.stringValue
        stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
    if (input.length == 0u) return;
    [self.consoleHistory addObject:input];
    if (self.consoleHistory.count > 100u)
        [self.consoleHistory removeObjectAtIndex:0u];
    self.consoleHistoryIndex = self.consoleHistory.count;
    self.consoleDraft = @"";
    [self appendConsoleMessage:std::string(": ")
        + input.UTF8String error:NO];
    self.consoleInput.stringValue = @"";
    self.consolePageInput.stringValue = @"";
    if (self.trackerCallbacks && self.trackerCallbacks->executeCommand)
        self.trackerCallbacks->executeCommand(input.UTF8String);
}

- (void)controlTextDidChange:(NSNotification*)notification
{
    NSTextField* field = (NSTextField*)notification.object;
    if (field == self.consoleInput)
        self.consolePageInput.stringValue = field.stringValue;
    else if (field == self.consolePageInput)
        self.consoleInput.stringValue = field.stringValue;
}

- (void)controlTextDidBeginEditing:(NSNotification*)notification
{
    NSTextField* field = (NSTextField*)notification.object;
    if ([field isKindOfClass:[NSTextField class]])
        S3GTrackerStyleTextEditor(field);
}

- (BOOL)control:(NSControl*)control textView:(NSTextView*)textView
    doCommandBySelector:(SEL)selector
{
    (void)textView;
    if (control != self.consoleInput && control != self.consolePageInput)
        return NO;
    if (selector == @selector(moveUp:)) {
        if (self.consoleHistory.count > 0u) {
            if (self.consoleHistoryIndex
                == static_cast<NSInteger>(self.consoleHistory.count))
                self.consoleDraft = textView.string;
            self.consoleHistoryIndex = std::max<NSInteger>(0,
                self.consoleHistoryIndex - 1);
            textView.string = self.consoleHistory[
                static_cast<NSUInteger>(self.consoleHistoryIndex)];
            textView.selectedRange = NSMakeRange(textView.string.length, 0u);
            NSTextField* other = control == self.consoleInput
                ? self.consolePageInput : self.consoleInput;
            other.stringValue = textView.string;
        }
        return YES;
    }
    if (selector == @selector(moveDown:)) {
        const NSInteger historyCount = static_cast<NSInteger>(
            self.consoleHistory.count);
        self.consoleHistoryIndex = std::min<NSInteger>(historyCount,
            self.consoleHistoryIndex + 1);
        textView.string = self.consoleHistoryIndex
                < static_cast<NSInteger>(self.consoleHistory.count)
            ? self.consoleHistory[static_cast<NSUInteger>(self.consoleHistoryIndex)]
            : (self.consoleDraft ? self.consoleDraft : @"");
        textView.selectedRange = NSMakeRange(textView.string.length, 0u);
        NSTextField* other = control == self.consoleInput
            ? self.consolePageInput : self.consoleInput;
        other.stringValue = textView.string;
        return YES;
    }
    if (selector == @selector(insertTab:)) {
        constexpr const char* commands[] {
            "help", "undo", "redo", "aliases", "alias", "kit", "play", "stop", "panic",
            "demo", "variation", "vary", "generate", "generateseed", "scene", "mutate",
            "drumscene", "bpm", "swing", "gate", "select", "hit", "rest",
            "repeat", "kill", "note", "vel", "velseq", "vol", "mask",
            "len", "stride", "dir", "mute", "unmute", "solo", "name",
            "eu", "euclid", "rotate", "fill", "reverse", "actions",
            "randomize", "random", "rand",
            "fx", "fxvalue", "interp", "interpolation", "warps", "warp", "out", "route",
            "instrument", "inst",
        };
        NSString* prefix = textView.string.lowercaseString;
        NSMutableArray<NSString*>* matches = [NSMutableArray array];
        for (const auto* candidate : commands) {
            NSString* word = [NSString stringWithUTF8String:candidate];
            if ([word hasPrefix:prefix]) [matches addObject:word];
        }
        if (matches.count == 1u)
            textView.string = [matches[0] stringByAppendingString:@" "];
        else if (matches.count > 1u)
            [self appendConsoleMessage:std::string("matches: ")
                + [[matches componentsJoinedByString:@", "] UTF8String] error:NO];
        textView.selectedRange = NSMakeRange(textView.string.length, 0u);
        NSTextField* other = control == self.consoleInput
            ? self.consolePageInput : self.consoleInput;
        other.stringValue = textView.string;
        return YES;
    }
    if (selector == @selector(cancelOperation:)) {
        [self.view.window makeFirstResponder:self.gridView];
        return YES;
    }
    return NO;
}

@end
