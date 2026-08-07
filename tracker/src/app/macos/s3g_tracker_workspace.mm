#import "s3g_tracker_workspace.h"
#import "s3g_tracker_controls.h"
#import "s3g_tracker_warp_window.h"
#include "s3g_tracker_grid_input.h"
#include "s3g_tracker_grid_selection.h"
#include "s3g_tracker_workspace_layout.h"

#include "s3g/tracker/fx_catalog.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>

namespace {

using s3g::tracker::Direction;
using s3g::tracker::ColumnDefinition;
using s3g::tracker::EventDestination;
using s3g::tracker::FxActionCell;
using s3g::tracker::FxActionCellState;
using s3g::tracker::FxValueCell;
using s3g::tracker::FxValueCellState;
using s3g::tracker::InstrumentCell;
using s3g::tracker::InstrumentCellState;
using s3g::tracker::NoteCell;
using s3g::tracker::NoteCellState;
using s3g::tracker::ParameterScope;
using s3g::tracker::Track;
using s3g::tracker::ValueCell;
using s3g::tracker::ValueCellState;
using s3g::tracker::app::TrackerViewState;
using s3g::tracker::app::WorkspaceCallbacks;

constexpr CGFloat kGridHeaderHeight = 54.0;
constexpr CGFloat kGridRowHeight = 25.0;
constexpr CGFloat kGridRowNumberWidth =
    s3g::tracker::app::kTrackerRowNumberWidth;
constexpr CGFloat kGridLaneGutter =
    s3g::tracker::app::kTrackerLaneGutter;
constexpr CGFloat kGridLaneInnerPadding = 3.0;

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

NSString* nsString(const std::string& value)
{
    NSString* result = [NSString stringWithUTF8String:value.c_str()];
    return result ? result : @"";
}

std::size_t visibleRows(const TrackerViewState* state)
{
    if (!state) return 16u;
    return std::clamp<std::size_t>(std::max(
        state->session.pattern.visibleRows, state->session.selectedRow + 1u),
        16u, 256u);
}

NSString* directionMark(Direction direction)
{
    switch (direction) {
    case Direction::Reverse: return @"<";
    case Direction::Random: return @"?";
    case Direction::Palindrome: return @"<>";
    case Direction::Forward:
    default: return @">";
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

NSString* noteText(const NoteCell& cell)
{
    switch (cell.state) {
    case NoteCellState::Note: return midiNoteName(cell.note);
    case NoteCellState::RetriggerPrevious: return @"RPT";
    case NoteCellState::Kill: return @"KIL";
    case NoteCellState::Rest:
    default: return @"---";
    }
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

NSString* volumeText(const Track& track, std::size_t row)
{
    if (row >= track.velocities.size()) return @"DEF";
    const auto& cell = track.velocities[row];
    if (cell.state == ValueCellState::Previous) return @"PRV";
    if (cell.state == ValueCellState::Default) return @"DEF";
    return [NSString stringWithFormat:@"%.3f", static_cast<double>(
        std::clamp(cell.normalized, 0.0f, 1.0f))];
}

NSString* indexedInstrumentText(const s3g::tracker::InstrumentRackState* rack,
    uint32_t nodeId)
{
    if (s3g::tracker::isMidiOutInstrumentNode(nodeId)) {
        const auto slot = s3g::tracker::midiOutRackSlotIndex(nodeId);
        return slot < s3g::tracker::kMidiOutRackSlotCount
            ? [NSString stringWithFormat:@"B%02lu",
                static_cast<unsigned long>(slot + 1u)] : @"B??";
    }
    if (!rack) return @"B??";
    const auto index = s3g::tracker::rackIndexForNode(*rack, nodeId);
    return index < rack->instruments.size()
        ? [NSString stringWithFormat:@"B%02lu",
            static_cast<unsigned long>(index + 1u)] : @"B??";
}

NSString* instrumentName(const s3g::tracker::InstrumentRackState* rack,
    uint32_t nodeId)
{
    if (s3g::tracker::isMidiOutInstrumentNode(nodeId)) {
        const auto slot = s3g::tracker::midiOutRackSlotIndex(nodeId);
        return [NSString stringWithFormat:@"REAPER MIDI BUS %lu",
            static_cast<unsigned long>(slot + 1u)];
    }
    const auto* instrument = rack
        ? s3g::tracker::rackInstrument(*rack, nodeId) : nullptr;
    if (!instrument)
        instrument = s3g::tracker::defaultRackInstrument(nodeId);
    return instrument ? nsString(std::string(instrument->name))
                      : @"UNKNOWN INSTRUMENT";
}

NSString* instrumentText(const Track& track, std::size_t row,
    const s3g::tracker::InstrumentRackState* rack)
{
    if (row >= track.instruments.size()) return @"---";
    const auto& cell = track.instruments[row];
    switch (cell.state) {
    case InstrumentCellState::Instrument:
        return indexedInstrumentText(rack, cell.nodeId);
    case InstrumentCellState::Previous: return @"PRV";
    case InstrumentCellState::Empty:
    default: return @"---";
    }
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
    const auto* action = s3g::tracker::findFxParameterAction(
        cell.targetNode, cell.parameterId);
    if (!action) return @"???";
    return [NSString stringWithUTF8String:
        std::string(action->mnemonic).c_str()];
}

NSString* fxValueText(const Track& track, std::size_t pair,
    std::size_t row)
{
    if (pair >= track.fxPairs.size()
        || row >= track.fxPairs[pair].values.size()) return @"PRV";
    const auto& cell = track.fxPairs[pair].values[row];
    if (cell.state == FxValueCellState::Previous) return @"PRV";
    return [NSString stringWithFormat:@"%.3f",
        static_cast<double>(
            std::clamp(cell.normalized, 0.0f, 1.0f))];
}

NSString* gridPageTitle(std::size_t page)
{
    if (page == 1u) return @"FX1 / V1";
    if (page == 2u) return @"FX2 / V2";
    return @"NOTE / BUS / VOL";
}

std::size_t gridFieldCount(std::size_t page) noexcept
{
    return page == 0u ? 3u : 2u;
}

CGFloat gridFieldStartFraction(std::size_t page, std::size_t field) noexcept
{
    if (page != 0u) return field == 0u ? 0.0 : 0.5;
    constexpr std::array<CGFloat, 3u> starts { 0.0, 0.31, 0.59 };
    return starts[std::min<std::size_t>(field, starts.size() - 1u)];
}

CGFloat gridFieldEndFraction(std::size_t page, std::size_t field) noexcept
{
    if (page != 0u) return field == 0u ? 0.5 : 1.0;
    constexpr std::array<CGFloat, 3u> ends { 0.31, 0.59, 1.0 };
    return ends[std::min<std::size_t>(field, ends.size() - 1u)];
}

NSRect gridFieldRect(CGFloat laneX, CGFloat y, CGFloat laneWidth,
    CGFloat height, std::size_t page, std::size_t field) noexcept
{
    const CGFloat start = gridFieldStartFraction(page, field);
    const CGFloat end = gridFieldEndFraction(page, field);
    return NSMakeRect(laneX + laneWidth * start, y,
        laneWidth * (end - start), height);
}

NSRect gridLaneBusRect(CGFloat fieldX, CGFloat fieldWidth) noexcept
{
    return NSMakeRect(fieldX + std::max<CGFloat>(0.0, fieldWidth - 88.0),
        4.0, 38.0, 16.0);
}

NSRect gridLaneChannelRect(CGFloat fieldX, CGFloat fieldWidth) noexcept
{
    return NSMakeRect(fieldX + std::max<CGFloat>(40.0, fieldWidth - 48.0),
        4.0, 48.0, 16.0);
}

std::size_t gridFieldAtX(CGFloat localX, CGFloat laneWidth,
    std::size_t page) noexcept
{
    const CGFloat fraction = laneWidth > 0.0
        ? std::clamp(localX / laneWidth, 0.0, 0.999999) : 0.0;
    const auto count = gridFieldCount(page);
    for (std::size_t field = 0u; field < count; ++field) {
        if (fraction < gridFieldEndFraction(page, field)) return field;
    }
    return count - 1u;
}

CGFloat gridLaneWidth(CGFloat viewWidth, std::size_t laneCount) noexcept
{
    if (laneCount == 0u) return 0.0;
    const CGFloat gutters = static_cast<CGFloat>(laneCount - 1u)
        * kGridLaneGutter;
    return std::max<CGFloat>(1.0,
        (viewWidth - kGridRowNumberWidth - gutters)
            / static_cast<CGFloat>(laneCount));
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

bool gridLaneAtX(CGFloat x, CGFloat viewWidth, std::size_t laneCount,
    std::size_t& lane, CGFloat& localFieldX) noexcept
{
    if (laneCount == 0u || x < kGridRowNumberWidth) return false;
    const CGFloat laneWidth = gridLaneWidth(viewWidth, laneCount);
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
    if (page == 0u) {
        field = std::min<std::size_t>(field, 2u);
        if (field == 0u) return &track.noteColumn;
        if (field == 1u) return &track.instrumentColumn;
        return &track.velocityColumn;
    }
    field = std::min<std::size_t>(field, 1u);
    auto& pair = track.fxPairs[std::min<std::size_t>(page - 1u,
        track.fxPairs.size() - 1u)];
    return field == 0u ? &pair.actionColumn : &pair.valueColumn;
}

uint8_t defaultNoteForLane(const Track& track, std::size_t lane)
{
    for (const auto& cell : track.notes) {
        if (cell.state == NoteCellState::Note) return cell.note;
    }
    constexpr std::array<uint8_t, 8u> notes {
        36u, 38u, 42u, 46u, 39u, 45u, 51u, 49u,
    };
    return lane < notes.size() ? notes[lane] : 60u;
}

const std::array<uint32_t, 8u> kLaneColors {
    0x78918cu, 0x9a826cu, 0x817a99u, 0x956f73u,
    0x71889au, 0x87916fu, 0x987b6du, 0x748c7bu,
};

// The Geometry window deliberately uses the brighter v8 ring palette. The
// tracker and mixer retain their quieter identity accents so this color is
// concentrated where it explains the relationship between pulse polygons.
const std::array<uint32_t, 8u> kGeometryLaneColors {
    0xffc72eu, 0x14c7ebu, 0xf53857u, 0x42db5cu,
    0xb36bffu, 0xff7314u, 0x3d7affu, 0xe6eb38u,
};

struct GeometryLaneSet {
    std::array<std::size_t, s3g::tracker::kMaximumTrackCount> indices {};
    std::size_t count = 0u;
};

GeometryLaneSet visibleGeometryLanes(const TrackerViewState* state)
{
    GeometryLaneSet result;
    if (!state) return result;
    const auto lanes = std::min<std::size_t>(
        s3g::tracker::kMaximumTrackCount,
        state->session.pattern.tracks.size());
    for (std::size_t lane = 0u; lane < lanes; ++lane) {
        if (!state->session.pattern.tracks[lane].noteColumn.muted)
            result.indices[result.count++] = lane;
    }
    return result;
}

} // namespace

@class S3GTrackerGridView;
@class S3GTrackerGeometryView;
@class S3GTrackerGeometryWindowController;
@class S3GTrackerEnvelopeView;
@class S3GTrackerInstrumentToolboxView;

@interface S3GTrackerWorkspaceController () <NSTextFieldDelegate>
@property(nonatomic, assign) TrackerViewState* trackerState;
@property(nonatomic, assign) WorkspaceCallbacks* trackerCallbacks;
@property(nonatomic, strong) NSView* toolbar;
@property(nonatomic, strong) NSScrollView* transportScroll;
@property(nonatomic, strong) NSStackView* transportControls;
@property(nonatomic, strong) NSScrollView* moduleScroll;
@property(nonatomic, strong) NSStackView* moduleControls;
@property(nonatomic, strong) NSScrollView* gridScroll;
@property(nonatomic, strong) S3GTrackerGridView* gridView;
@property(nonatomic, strong) S3GTrackerGeometryView* geometryView;
@property(nonatomic, strong) S3GTrackerGeometryWindowController*
    geometryWindowController;
@property(nonatomic, strong) S3GTrackerWarpWindowController*
    warpWindowController;
@property(nonatomic, strong) S3GTrackerEnvelopeView* envelopeView;
@property(nonatomic, strong) NSView* consolePanel;
@property(nonatomic, strong) NSView* consoleOutputPanel;
@property(nonatomic, strong) NSTextView* consoleOutput;
@property(nonatomic, strong) NSTextField* consoleInput;
@property(nonatomic, strong) NSMutableArray<NSString*>* consoleHistory;
@property(nonatomic) NSInteger consoleHistoryIndex;
@property(nonatomic, copy) NSString* consoleDraft;
@property(nonatomic, strong) NSButton* playButton;
@property(nonatomic, strong) NSButton* loopButton;
@property(nonatomic, strong) NSButton* stopButton;
@property(nonatomic, strong) NSButton* pauseButton;
@property(nonatomic, strong) NSButton* pageButton;
@property(nonatomic, strong) NSPopUpButton* patternPopup;
@property(nonatomic, strong) NSButton* createPatternButton;
@property(nonatomic, strong) NSButton* duplicatePatternButton;
@property(nonatomic, strong) NSButton* renamePatternButton;
@property(nonatomic, strong) NSButton* deletePatternButton;
@property(nonatomic, strong) NSTextField* bpmField;
@property(nonatomic, strong) NSTextField* swingField;
@property(nonatomic, strong) NSTextField* gateField;
@property(nonatomic, strong) NSTextField* loopStartField;
@property(nonatomic, strong) NSTextField* loopEndField;
@property(nonatomic, strong) NSPopUpButton* audioPopup;
@property(nonatomic, strong) NSTextField* routeStatus;
@property(nonatomic, strong) NSTextField* eventStatus;
@property(nonatomic, strong) NSLayoutConstraint* envelopeHeightConstraint;
- (void)modulePatternChanged;
- (void)moduleTransportChanged;
- (void)moduleSelectionChanged;
- (void)moduleTogglePlayback;
- (void)moduleFocusConsole;
- (void)cycleGridPage:(id)sender;
- (void)loopPressed:(id)sender;
- (void)applyWorkspaceMode;
- (void)assignTrackInstrument:(uint32_t)nodeId;
- (void)editRackInstrument:(uint32_t)nodeId;
- (void)addInstrumentKind:(s3g::tracker::InstrumentKind)kind;
- (void)zoomTrackerIn;
- (void)zoomTrackerOut;
- (void)resetTrackerZoom;
@end

@interface S3GTrackerGridView : NSView <NSTextFieldDelegate> {
    s3g::tracker::app::GridSelection _gridSelection;
}
- (instancetype)initWithState:(TrackerViewState*)state
    owner:(S3GTrackerWorkspaceController*)owner;
- (void)scrollSelectionToVisible;
- (void)refreshAccessibilityValue;
@property(nonatomic, assign) TrackerViewState* trackerState;
@property(nonatomic, weak) S3GTrackerWorkspaceController* owner;
@property(nonatomic, strong) NSTextField* cellEditor;
@property(nonatomic) std::size_t editingTrack;
@property(nonatomic) std::size_t editingRow;
@property(nonatomic) std::size_t editingPage;
@property(nonatomic) std::size_t editingField;
@property(nonatomic) BOOL editingColumnLength;
@property(nonatomic) NSInteger loopAnchorRow;
@property(nonatomic) BOOL selectingLoopRows;
@property(nonatomic) BOOL selectingGridCells;
@property(nonatomic) NSInteger copiedPasteboardChangeCount;
@property(nonatomic) std::size_t copiedTrackCount;
@property(nonatomic) std::size_t copiedFieldCount;
@property(nonatomic) std::size_t copiedRowCount;
- (void)beginCellEditingWithInitialText:(NSString*)initialText;
- (void)beginColumnLengthEditingForTrack:(std::size_t)track
    page:(std::size_t)page field:(std::size_t)field rect:(NSRect)rect;
- (void)showMidiBusMenuForTrack:(std::size_t)track event:(NSEvent*)event;
- (void)showMidiChannelMenuForTrack:(std::size_t)track event:(NSEvent*)event;
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
        self.accessibilityHelp = @"Each lane header has its own clickable MIDI bus and channel. Left and right move between fields; up and down move between rows. Shift-left and Shift-right move between lanes. Double-click a column header to edit its independent length. Drag the row gutter or use Shift-up and Shift-down to select the global loop. NOTE accepts a MIDI number or note name, BUS accepts B01 through B08, and VOL accepts 0.000 through 1.000. Control-A, C, X, and V select all, copy, cut, and paste tracker cells; Command shortcuts remain available to REAPER.";
    }
    return self;
}

- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }

- (void)clearGridSelection
{
    _gridSelection.active = false;
    [self setNeedsDisplay:YES];
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

- (s3g::tracker::app::GridSelectionRange)effectiveGridSelection
{
    auto* model = self.trackerState;
    if (_gridSelection.active && model
        && _gridSelection.page == model->session.selectedPage) {
        const auto candidate = _gridSelection.range();
        if (!model->session.pattern.tracks.empty()
            && candidate.lastTrack < model->session.pattern.tracks.size()
            && candidate.lastField < gridFieldCount(candidate.page)
            && candidate.lastRow < visibleRows(model)) return candidate;
        _gridSelection.active = false;
    }
    s3g::tracker::app::GridSelectionRange range;
    if (!model) return range;
    range.page = std::min<std::size_t>(model->session.selectedPage, 2u);
    range.firstTrack = range.lastTrack = model->session.selectedTrack;
    range.firstField = range.lastField = model->session.selectedField;
    range.firstRow = range.lastRow = model->session.selectedRow;
    return range;
}

- (void)refreshAccessibilityValue
{
    auto* model = self.trackerState;
    if (!model || model->session.pattern.tracks.empty()) {
        self.accessibilityValue = @"No lanes";
        return;
    }
    const auto& session = model->session;
    const auto lane = std::min(session.selectedTrack,
        session.pattern.tracks.size() - 1u);
    const auto row = session.selectedRow;
    const auto page = std::min<std::size_t>(session.selectedPage, 2u);
    const auto field = std::min(session.selectedField,
        gridFieldCount(page) - 1u);
    const auto& track = session.pattern.tracks[lane];
    NSString* fieldName = @"Note";
    NSString* fieldValue = @"---";
    if (page == 0u && field == 0u) {
        const NoteCell cell = row < track.notes.size()
            ? track.notes[row] : NoteCell::rest();
        fieldValue = noteText(cell);
    } else if (page == 0u && field == 1u) {
        fieldName = @"MIDI bus";
        NSString* authored = instrumentText(track, row,
            &model->instrumentRack);
        NSString* laneDefault = instrumentName(&model->instrumentRack,
            laneInitialInstrument(track));
        fieldValue = [NSString stringWithFormat:@"%@, lane default %@",
            authored, laneDefault];
    } else if (page == 0u) {
        fieldName = @"Volume";
        fieldValue = volumeText(track, row);
    } else {
        const auto pair = page - 1u;
        if (field == 0u) {
            fieldName = [NSString stringWithFormat:@"FX%lu action",
                static_cast<unsigned long>(page)];
            fieldValue = fxActionText(track, pair, row);
        } else {
            fieldName = [NSString stringWithFormat:@"FX%lu value",
                static_cast<unsigned long>(page)];
            fieldValue = fxValueText(track, pair, row);
        }
    }
    self.accessibilityValue = [NSString stringWithFormat:
        @"Lane %lu, row %lu, %@, %@",
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
    const CGFloat laneWidth = gridLaneWidth(NSWidth(self.bounds), laneCount);
    [self scrollRectToVisible:NSMakeRect(gridLaneX(lane, laneWidth),
        kGridHeaderHeight + static_cast<CGFloat>(row) * kGridRowHeight,
        laneWidth, kGridRowHeight)];
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
        track.notes[row] = NoteCell::withNote(defaultNoteForLane(track, lane));
    else if (state == NoteCellState::RetriggerPrevious)
        track.notes[row] = NoteCell::retriggerPrevious();
    else if (state == NoteCellState::Kill)
        track.notes[row] = NoteCell::kill();
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
    track.velocities[row] = ValueCell::withValue(std::clamp(
        current + delta, 0.0f, 1.0f));
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
        || model->session.selectedPage == 0u) return;
    auto& session = model->session;
    auto& track = session.pattern.tracks[std::min(session.selectedTrack,
        session.pattern.tracks.size() - 1u)];
    auto& pair = track.fxPairs[session.selectedPage - 1u];
    const auto row = session.selectedRow;
    if (pair.values.size() <= row)
        pair.values.resize(row + 1u, FxValueCell::previous());
    const float current = resolvedFxValue(track,
        session.selectedPage - 1u, row);
    const int scaled = static_cast<int>(std::lround(current * 100.0f));
    pair.values[row] = FxValueCell::withValue(
        static_cast<float>(std::clamp(scaled + delta, 0, 100)) / 100.0f);
    pair.valueColumn.length = std::max(pair.valueColumn.length, row + 1u);
    session.pattern.visibleRows = std::max(session.pattern.visibleRows,
        row + 1u);
    [self.owner modulePatternChanged];
}

- (void)writeFxState:(BOOL)previous clear:(BOOL)clear
{
    auto* model = self.trackerState;
    if (!model || model->session.pattern.tracks.empty()
        || model->session.selectedPage == 0u) return;
    auto& session = model->session;
    auto& track = session.pattern.tracks[std::min(session.selectedTrack,
        session.pattern.tracks.size() - 1u)];
    auto& pair = track.fxPairs[session.selectedPage - 1u];
    const auto row = session.selectedRow;
    if (session.selectedField == 0u) {
        if (pair.actions.size() <= row)
            pair.actions.resize(row + 1u, FxActionCell::empty());
        if (clear) pair.actions[row] = FxActionCell::empty();
        else if (previous) pair.actions[row] = FxActionCell::previous();
        else {
            pair.actions[row] = FxActionCell::parameter(
                session.selectedPage == 1u ? 3u : 6u);
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

- (void)showMidiBusMenuForTrack:(std::size_t)trackIndex
    event:(NSEvent*)event
{
    auto* model = self.trackerState;
    if (!model || trackIndex >= model->session.pattern.tracks.size()) return;
    const auto& track = model->session.pattern.tracks[trackIndex];
    auto selected = s3g::tracker::midiOutRackSlotIndex(
        track.initialInstrumentNodeId);
    if (selected >= s3g::tracker::kMidiOutRackSlotCount) selected = 0u;
    NSMenu* menu = [[NSMenu alloc] initWithTitle:@"TRACK MIDI BUS"];
    menu.autoenablesItems = NO;
    for (std::size_t bus = 0u;
         bus < s3g::tracker::kMidiOutRackSlotCount; ++bus) {
        NSMenuItem* item = [[NSMenuItem alloc]
            initWithTitle:[NSString stringWithFormat:@"BUS %02lu",
                static_cast<unsigned long>(bus + 1u)]
            action:@selector(laneMidiBusSelected:) keyEquivalent:@""];
        item.target = self;
        item.representedObject = @{
            @"track": @(trackIndex), @"bus": @(bus),
        };
        item.state = bus == selected
            ? NSControlStateValueOn : NSControlStateValueOff;
        [menu addItem:item];
    }
    [NSMenu popUpContextMenu:menu withEvent:event forView:self];
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

- (void)laneMidiBusSelected:(NSMenuItem*)sender
{
    auto* model = self.trackerState;
    NSDictionary* value = sender.representedObject;
    if (!model || ![value isKindOfClass:NSDictionary.class]) return;
    const auto trackIndex = [value[@"track"] unsignedIntegerValue];
    const auto bus = [value[@"bus"] unsignedIntegerValue];
    if (trackIndex >= model->session.pattern.tracks.size()
        || bus >= s3g::tracker::kMidiOutRackSlotCount) return;
    auto& track = model->session.pattern.tracks[trackIndex];
    track.initialInstrumentNodeId = s3g::tracker::midiOutNodeForRackSlot(bus);
    track.destination = EventDestination::Midi;
    model->selectedRackInstrument = track.initialInstrumentNodeId;
    model->session.selectedTrack = trackIndex;
    [self.owner modulePatternChanged];
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

- (void)mouseDown:(NSEvent*)event
{
    auto* model = self.trackerState;
    if (!model || model->session.pattern.tracks.empty()) return;
    const NSPoint point = [self convertPoint:event.locationInWindow fromView:nil];
    if (point.x < kGridRowNumberWidth && point.y >= kGridHeaderHeight) {
        [self clearGridSelection];
        const NSInteger row = static_cast<NSInteger>(
            (point.y - kGridHeaderHeight) / kGridRowHeight);
        if (row >= 0 && row < static_cast<NSInteger>(visibleRows(model))) {
            self.loopAnchorRow = row;
            self.selectingLoopRows = YES;
            [self setLoopFromAnchor:static_cast<std::size_t>(row)
                row:static_cast<std::size_t>(row)];
            [self selectTrack:model->session.selectedTrack
                row:static_cast<std::size_t>(row)];
            [self.window makeFirstResponder:self];
        }
        return;
    }
    self.selectingLoopRows = NO;
    self.selectingGridCells = NO;
    self.loopAnchorRow = -1;
    const auto laneCount = std::min<std::size_t>(
        s3g::tracker::kMaximumTrackCount,
        model->session.pattern.tracks.size());
    const CGFloat width = gridLaneWidth(NSWidth(self.bounds), laneCount);
    const CGFloat fieldWidth = gridLaneFieldWidth(width);
    std::size_t lane = 0u;
    CGFloat localFieldX = 0.0;
    if (!gridLaneAtX(point.x, NSWidth(self.bounds), laneCount,
            lane, localFieldX)) return;
    if (point.y < kGridHeaderHeight) {
        [self clearGridSelection];
        const CGFloat laneLeft = gridLaneFieldX(lane, width);
        if (NSPointInRect(point,
                gridLaneBusRect(laneLeft, fieldWidth))) {
            [self selectTrack:lane row:model->session.selectedRow];
            [self showMidiBusMenuForTrack:lane event:event];
            return;
        }
        if (NSPointInRect(point,
                gridLaneChannelRect(laneLeft, fieldWidth))) {
            [self selectTrack:lane row:model->session.selectedRow];
            [self showMidiChannelMenuForTrack:lane event:event];
            return;
        }
        const auto page = std::min<std::size_t>(
            model->session.selectedPage, 2u);
        model->session.selectedField = gridFieldAtX(
            localFieldX, fieldWidth, page);
        [self selectTrack:lane
            row:model->session.selectedRow];
        const NSRect fieldRect = gridFieldRect(laneLeft, 0.0, fieldWidth,
            kGridHeaderHeight, page, model->session.selectedField);
        const CGFloat fieldRight = NSMaxX(fieldRect);
        const bool inMuteRow = point.y >= 36.0 && point.y <= 52.0;
        if (inMuteRow && point.x >= fieldRight - 22.0
            && point.x <= fieldRight) {
            auto& track = model->session.pattern.tracks[
                lane];
            auto& muted = columnForField(track,
                model->session.selectedPage,
                model->session.selectedField)->muted;
            muted = !muted;
            [self.owner modulePatternChanged];
        } else if (event.clickCount >= 2) {
            [self beginColumnLengthEditingForTrack:lane page:page
                field:model->session.selectedField
                rect:NSInsetRect(NSMakeRect(NSMinX(fieldRect), 23.0,
                    NSWidth(fieldRect), 19.0), 1.0, 0.0)];
            return;
        }
        [self.window makeFirstResponder:self];
        return;
    }
    const NSInteger row = static_cast<NSInteger>(
        (point.y - kGridHeaderHeight) / kGridRowHeight);
    if (row < 0 || row >= static_cast<NSInteger>(visibleRows(model))) return;
    const auto page = std::min<std::size_t>(
        model->session.selectedPage, 2u);
    model->session.selectedField = gridFieldAtX(
        localFieldX, fieldWidth, page);
    [self beginGridSelectionAtTrack:lane
        field:model->session.selectedField row:static_cast<std::size_t>(row)
        page:page];
    [self selectTrack:lane
        row:static_cast<std::size_t>(row)];
    [self.window makeFirstResponder:self];
    if (event.clickCount >= 2) {
        [self beginCellEditing];
    }
}

- (void)mouseDragged:(NSEvent*)event
{
    auto* model = self.trackerState;
    if (!model) return;
    const NSPoint point = [self convertPoint:event.locationInWindow fromView:nil];
    if (!self.selectingLoopRows) {
        if (!self.selectingGridCells) return;
        const auto laneCount = std::min<std::size_t>(
            s3g::tracker::kMaximumTrackCount,
            model->session.pattern.tracks.size());
        const CGFloat width = gridLaneWidth(NSWidth(self.bounds), laneCount);
        const CGFloat fieldWidth = gridLaneFieldWidth(width);
        std::size_t lane = 0u;
        CGFloat localFieldX = 0.0;
        if (!gridLaneAtX(point.x, NSWidth(self.bounds), laneCount,
                lane, localFieldX)) return;
        const NSInteger row = std::clamp<NSInteger>(static_cast<NSInteger>(
            (point.y - kGridHeaderHeight) / kGridRowHeight), 0,
            static_cast<NSInteger>(visibleRows(model) - 1u));
        const auto page = std::min<std::size_t>(
            model->session.selectedPage, 2u);
        const auto field = gridFieldAtX(localFieldX, fieldWidth, page);
        [self extendGridSelectionToTrack:lane field:field
            row:static_cast<std::size_t>(row)];
        model->session.selectedField = field;
        [self selectTrack:lane row:static_cast<std::size_t>(row)];
        return;
    }
    if (self.loopAnchorRow < 0) return;
    const NSInteger row = std::clamp<NSInteger>(static_cast<NSInteger>(
        (point.y - kGridHeaderHeight) / kGridRowHeight), 0,
        static_cast<NSInteger>(visibleRows(model) - 1u));
    [self setLoopFromAnchor:static_cast<std::size_t>(self.loopAnchorRow)
        row:static_cast<std::size_t>(row)];
    [self selectTrack:model->session.selectedTrack
        row:static_cast<std::size_t>(row)];
}

- (void)mouseUp:(NSEvent*)event
{
    (void)event;
    self.selectingLoopRows = NO;
    self.selectingGridCells = NO;
}

- (NSString*)editingValue
{
    auto* model = self.trackerState;
    if (!model || self.editingTrack >= model->session.pattern.tracks.size())
        return @"";
    const auto& track = model->session.pattern.tracks[self.editingTrack];
    if (self.editingPage == 0u && self.editingField == 0u) {
        if (self.editingRow >= track.notes.size()) return @"---";
        const auto& cell = track.notes[self.editingRow];
        return noteText(cell);
    }
    if (self.editingPage == 0u && self.editingField == 1u) {
        if (self.editingRow >= track.instruments.size()) return @"";
        const auto& cell = track.instruments[self.editingRow];
        if (cell.state == InstrumentCellState::Instrument) {
            const auto slot = s3g::tracker::midiOutRackSlotIndex(
                cell.nodeId);
            if (slot < s3g::tracker::kMidiOutRackSlotCount)
                return [NSString stringWithFormat:@"B%02lu",
                    static_cast<unsigned long>(slot + 1u)];
            return @"";
        }
        return cell.state == InstrumentCellState::Previous ? @"PRV" : @"";
    }
    if (self.editingPage == 0u) {
        if (self.editingRow >= track.velocities.size()) return @"DEF";
        const auto& cell = track.velocities[self.editingRow];
        if (cell.state == ValueCellState::Value)
            return [NSString stringWithFormat:@"%.3f", static_cast<double>(
                std::clamp(cell.normalized, 0.0f, 1.0f))];
        return cell.state == ValueCellState::Previous ? @"PRV" : @"DEF";
    }
    const auto& pair = track.fxPairs[self.editingPage - 1u];
    if (self.editingField == 0u) {
        if (self.editingRow >= pair.actions.size()) return @"";
        return fxActionText(track, self.editingPage - 1u, self.editingRow);
    }
    if (self.editingRow >= pair.values.size()) return @"PRV";
    return fxValueText(track, self.editingPage - 1u, self.editingRow);
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
    const auto page = std::min<std::size_t>(model->session.selectedPage, 2u);
    const auto field = std::min(model->session.selectedField,
        gridFieldCount(page) - 1u);
    const CGFloat laneWidth = gridLaneWidth(NSWidth(self.bounds), laneCount);
    const CGFloat fieldWidth = gridLaneFieldWidth(laneWidth);
    const CGFloat x = gridLaneFieldX(lane, laneWidth);
    NSRect rect = gridFieldRect(x, kGridHeaderHeight
            + static_cast<CGFloat>(row) * kGridRowHeight,
        fieldWidth, kGridRowHeight, page, field);
    rect = NSInsetRect(rect, 1.0, 1.0);
    self.editingTrack = lane;
    self.editingRow = row;
    self.editingPage = page;
    self.editingField = field;
    self.editingColumnLength = NO;
    self.cellEditor = [[NSTextField alloc] initWithFrame:rect];
    S3GTrackerStyleTextField(self.cellEditor, NSTextAlignmentCenter);
    self.cellEditor.font = trackerFont(11.0, NSFontWeightSemibold);
    self.cellEditor.stringValue = initialText
        ? initialText : [self editingValue];
    self.cellEditor.delegate = self;
    self.cellEditor.target = self;
    self.cellEditor.action = @selector(commitCellEditing:);
    self.cellEditor.accessibilityLabel = @"Direct tracker cell value";
    [self addSubview:self.cellEditor];
    [self.window makeFirstResponder:self.cellEditor];
    if (initialText) {
        NSText* editor = self.cellEditor.currentEditor;
        if ([editor respondsToSelector:@selector(setSelectedRange:)]) {
            [(NSTextView*)editor setSelectedRange:NSMakeRange(
                self.cellEditor.stringValue.length, 0u)];
        }
    } else {
        [self.cellEditor selectText:nil];
    }
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
    auto& trackModel = model->session.pattern.tracks[track];
    const auto length = columnForField(trackModel, page, field)->length;
    self.cellEditor = [[NSTextField alloc] initWithFrame:rect];
    S3GTrackerStyleTextField(self.cellEditor, NSTextAlignmentCenter);
    self.cellEditor.font = trackerFont(9.5, NSFontWeightSemibold);
    self.cellEditor.integerValue = static_cast<NSInteger>(length);
    self.cellEditor.delegate = self;
    self.cellEditor.target = self;
    self.cellEditor.action = @selector(commitCellEditing:);
    self.cellEditor.accessibilityLabel = @"Column length in rows";
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

- (BOOL)applyCellText:(NSString*)source toTrack:(Track&)track
    row:(std::size_t)row page:(std::size_t)page field:(std::size_t)field
{
    NSString* text = [source stringByTrimmingCharactersInSet:
        NSCharacterSet.whitespaceAndNewlineCharacterSet];
    NSString* lower = text.lowercaseString;
    auto* model = self.trackerState;
    if (!model || page > 2u || field >= gridFieldCount(page)) return NO;
    if (page == 0u && field == 0u) {
        if (track.notes.size() <= row)
            track.notes.resize(row + 1u, NoteCell::rest());
        uint8_t value = 0u;
        const char* noteUtf8 = lower.UTF8String;
        if ([lower isEqualToString:@"---"] || [lower isEqualToString:@"rest"]
            || lower.length == 0u) track.notes[row] = NoteCell::rest();
        else if ([lower isEqualToString:@"rpt"]
            || [lower isEqualToString:@"repeat"])
            track.notes[row] = NoteCell::retriggerPrevious();
        else if ([lower isEqualToString:@"kil"]
            || [lower isEqualToString:@"kill"])
            track.notes[row] = NoteCell::kill();
        else if (noteUtf8 && s3g::tracker::parseMidiNote(noteUtf8, value))
            track.notes[row] = NoteCell::withNote(value);
        else return NO;
        track.noteColumn.length = std::max(track.noteColumn.length, row + 1u);
        return YES;
    }
    if (page == 0u && field == 1u) {
        if (track.instruments.size() <= row)
            track.instruments.resize(row + 1u, InstrumentCell::empty());
        if (lower.length == 0u || [lower isEqualToString:@"---"]
            || [lower isEqualToString:@"clear"])
            track.instruments[row] = InstrumentCell::empty();
        else if ([lower isEqualToString:@"prv"]
            || [lower isEqualToString:@"previous"])
            track.instruments[row] = InstrumentCell::previous();
        else {
            NSString* digits = [lower hasPrefix:@"b"]
                ? [lower substringFromIndex:1u] : lower;
            NSInteger bus = 0;
            if (![self scanInteger:digits result:&bus]
                || bus < 1
                || bus > static_cast<NSInteger>(
                    s3g::tracker::kMidiOutRackSlotCount)) return NO;
            const auto node = s3g::tracker::midiOutNodeForRackSlot(
                static_cast<std::size_t>(bus - 1));
            if (!s3g::tracker::rackInstrument(
                    model->instrumentRack, node)) return NO;
            track.instruments[row] = InstrumentCell::withInstrument(node);
        }
        track.instrumentColumn.length = std::max(
            track.instrumentColumn.length, row + 1u);
        return YES;
    }
    if (page == 0u) {
        if (track.velocities.size() <= row)
            track.velocities.resize(row + 1u, ValueCell::defaultValue());
        float normalized = 0.0f;
        if ([lower isEqualToString:@"def"]
            || [lower isEqualToString:@"default"] || lower.length == 0u)
            track.velocities[row] = ValueCell::defaultValue();
        else if ([lower isEqualToString:@"prv"]
            || [lower isEqualToString:@"previous"])
            track.velocities[row] = ValueCell::previous();
        else if (s3g::tracker::app::parseGridNormalizedValue(
                std::string_view(lower.UTF8String ? lower.UTF8String : ""),
                normalized))
            track.velocities[row] = ValueCell::withValue(normalized);
        else return NO;
        track.velocityColumn.length = std::max(
            track.velocityColumn.length, row + 1u);
        return YES;
    }
    auto& pair = track.fxPairs[page - 1u];
    if (field == 0u) {
        if (pair.actions.size() <= row)
            pair.actions.resize(row + 1u, FxActionCell::empty());
        if (lower.length == 0u || [lower isEqualToString:@"---"]
            || [lower isEqualToString:@"clear"])
            pair.actions[row] = FxActionCell::empty();
        else if ([lower isEqualToString:@"prv"]
            || [lower isEqualToString:@"previous"])
            pair.actions[row] = FxActionCell::previous();
        else {
            const s3g::tracker::FxParameterActionDefinition* found = nullptr;
            const char* utf8 = lower.UTF8String;
            const std::string key(utf8 ? utf8 : "");
            for (std::size_t index = 0u;
                 index < s3g::tracker::fxParameterActionCount(); ++index) {
                const auto* action = s3g::tracker::fxParameterAction(index);
                if (!action) continue;
                std::string mnemonic(action->mnemonic);
                for (char& character : mnemonic) {
                    if (character >= 'A' && character <= 'Z')
                        character = static_cast<char>(character - 'A' + 'a');
                }
                if (action->stableKey == key || mnemonic == key) {
                    found = action;
                    break;
                }
            }
            if (found) {
                pair.actions[row] = FxActionCell::parameter(
                    found->parameterId, ParameterScope::Global,
                    found->targetNode);
            } else if (const auto* timing
                = s3g::tracker::findSequencerAction(key)) {
                pair.actions[row] = FxActionCell::sequencer(timing->action);
            } else return NO;
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
    else if (s3g::tracker::app::parseGridNormalizedValue(
            std::string_view(lower.UTF8String ? lower.UTF8String : ""), value))
        pair.values[row] = FxValueCell::withValue(value);
    else return NO;
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
    if (self.editingColumnLength) {
        NSInteger lengthValue = 0;
        if (![self scanInteger:lower result:&lengthValue]
            || lengthValue < 1 || lengthValue > 256) {
            NSBeep();
            self.cellEditor.backgroundColor = trackerColor(0x3a2020);
            [self.cellEditor selectText:nil];
            return;
        }
        const auto length = static_cast<std::size_t>(lengthValue);
        if (self.editingPage == 0u && self.editingField == 0u) {
            if (track.notes.size() < length)
                track.notes.resize(length, NoteCell::rest());
        } else if (self.editingPage == 0u && self.editingField == 1u) {
            if (track.instruments.size() < length)
                track.instruments.resize(length, InstrumentCell::empty());
        } else if (self.editingPage == 0u) {
            if (track.velocities.size() < length)
                track.velocities.resize(length, ValueCell::defaultValue());
        } else if (self.editingField == 0u) {
            auto& actions = track.fxPairs[self.editingPage - 1u].actions;
            if (actions.size() < length)
                actions.resize(length, FxActionCell::empty());
        } else {
            auto& values = track.fxPairs[self.editingPage - 1u].values;
            if (values.size() < length)
                values.resize(length, FxValueCell::previous());
        }
        columnForField(track, self.editingPage,
            self.editingField)->length = length;
        originalTrack = std::move(candidate);
        model->session.pattern.visibleRows = std::max(
            model->session.pattern.visibleRows, length);
        [self.cellEditor removeFromSuperview];
        self.cellEditor = nil;
        self.editingColumnLength = NO;
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
    model->session.selectedRow = std::min(row + 1u, visibleRows(model) - 1u);
    [self.cellEditor removeFromSuperview];
    self.cellEditor = nil;
    self.editingColumnLength = NO;
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
    if (page == 0u && field == 0u)
        return row < track.notes.size() ? noteText(track.notes[row]) : @"---";
    if (page == 0u && field == 1u) {
        if (row >= track.instruments.size()) return @"---";
        const auto& cell = track.instruments[row];
        if (cell.state == InstrumentCellState::Previous) return @"PRV";
        if (cell.state != InstrumentCellState::Instrument) return @"---";
        const auto index = s3g::tracker::rackIndexForNode(
            model->instrumentRack, cell.nodeId);
        return index < model->instrumentRack.instruments.size()
            ? [NSString stringWithFormat:@"%lu",
                static_cast<unsigned long>(index)] : @"---";
    }
    if (page == 0u) return volumeText(track, row);
    return field == 0u
        ? fxActionText(track, page - 1u, row)
        : fxValueText(track, page - 1u, row);
}

- (NSString*)clearTokenForPage:(std::size_t)page field:(std::size_t)field
{
    if (page == 0u && field == 0u) return @"---";
    if (page == 0u && field == 1u) return @"---";
    if (page == 0u) return @"DEF";
    return field == 0u ? @"---" : @"PRV";
}

- (void)trackerSelectAll:(id)sender
{
    (void)sender;
    auto* model = self.trackerState;
    if (!model || model->session.pattern.tracks.empty()) return;
    const auto page = std::min<std::size_t>(model->session.selectedPage, 2u);
    _gridSelection.active = true;
    _gridSelection.page = page;
    _gridSelection.anchorTrack = 0u;
    _gridSelection.anchorField = 0u;
    _gridSelection.anchorRow = 0u;
    _gridSelection.focusTrack = model->session.pattern.tracks.size() - 1u;
    _gridSelection.focusField = gridFieldCount(page) - 1u;
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
        gridFieldCount(range.page) - 1u);
    range.lastRow = std::min(range.lastRow, visibleRows(model) - 1u);
    NSMutableArray<NSString*>* lines = [[NSMutableArray alloc] init];
    for (std::size_t row = range.firstRow; row <= range.lastRow; ++row) {
        NSMutableArray<NSString*>* cells = [[NSMutableArray alloc] init];
        for (std::size_t track = range.firstTrack;
             track <= range.lastTrack; ++track) {
            for (std::size_t field = range.firstField;
                 field <= range.lastField; ++field) {
                [cells addObject:[self cellTextForTrack:track row:row
                    page:range.page field:field]];
            }
        }
        [lines addObject:[cells componentsJoinedByString:@"\t"]];
    }
    NSString* value = [lines componentsJoinedByString:@"\n"];
    NSPasteboard* pasteboard = NSPasteboard.generalPasteboard;
    [pasteboard clearContents];
    [pasteboard setString:value forType:NSPasteboardTypeString];
    self.copiedPasteboardChangeCount = pasteboard.changeCount;
    self.copiedTrackCount = range.trackCount();
    self.copiedFieldCount = range.fieldCount();
    self.copiedRowCount = range.rowCount();
}

- (void)trackerCut:(id)sender
{
    [self trackerCopy:sender];
    auto* model = self.trackerState;
    if (!model || model->session.pattern.tracks.empty()) return;
    s3g::tracker::Pattern candidate = model->session.pattern;
    auto range = [self effectiveGridSelection];
    range.lastTrack = std::min(range.lastTrack, candidate.tracks.size() - 1u);
    range.lastField = std::min(range.lastField,
        gridFieldCount(range.page) - 1u);
    range.lastRow = std::min<std::size_t>(range.lastRow, 255u);
    for (std::size_t row = range.firstRow; row <= range.lastRow; ++row) {
        for (std::size_t track = range.firstTrack;
             track <= range.lastTrack; ++track) {
            for (std::size_t field = range.firstField;
                 field <= range.lastField; ++field) {
                if (![self applyCellText:[self clearTokenForPage:range.page
                        field:field] toTrack:candidate.tracks[track] row:row
                        page:range.page field:field]) {
                    NSBeep();
                    return;
                }
            }
        }
    }
    candidate.visibleRows = std::max(candidate.visibleRows, range.lastRow + 1u);
    model->session.pattern = std::move(candidate);
    [self.owner modulePatternChanged];
}

- (void)trackerPaste:(id)sender
{
    (void)sender;
    NSString* value = [NSPasteboard.generalPasteboard
        stringForType:NSPasteboardTypeString];
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
    const auto fields = gridFieldCount(page);
    const bool fillSelection = rows.count == 1u && widest == 1u
        && _gridSelection.active;
    const bool shapedInternalPaste = self.copiedPasteboardChangeCount
            == NSPasteboard.generalPasteboard.changeCount
        && self.copiedTrackCount > 0u && self.copiedFieldCount > 0u
        && self.copiedRowCount == rows.count
        && self.copiedTrackCount * self.copiedFieldCount == widest;
    std::size_t lastRow = range.firstRow;
    if (fillSelection) {
        NSString* cell = rows.firstObject.firstObject;
        for (std::size_t row = range.firstRow;
             row <= std::min<std::size_t>(range.lastRow, 255u); ++row) {
            for (std::size_t track = range.firstTrack;
                 track <= std::min(range.lastTrack,
                    candidate.tracks.size() - 1u); ++track) {
                for (std::size_t field = range.firstField;
                     field <= std::min(range.lastField, fields - 1u); ++field) {
                    if (![self applyCellText:cell toTrack:candidate.tracks[track]
                            row:row page:page field:field]) {
                        NSBeep();
                        return;
                    }
                }
            }
            lastRow = row;
        }
    } else {
        const auto firstColumn = s3g::tracker::app::gridClipboardColumn(
            range.firstTrack, range.firstField, fields);
        const auto maximumColumn = candidate.tracks.size() * fields;
        for (NSUInteger rowOffset = 0u; rowOffset < rows.count; ++rowOffset) {
            const auto destinationRow = range.firstRow
                + static_cast<std::size_t>(rowOffset);
            if (destinationRow > 255u) break;
            NSArray<NSString*>* cells = rows[rowOffset];
            for (NSUInteger columnOffset = 0u;
                 columnOffset < cells.count; ++columnOffset) {
                std::size_t track = 0u;
                std::size_t field = 0u;
                if (shapedInternalPaste) {
                    const auto sourceColumn = static_cast<std::size_t>(
                        columnOffset);
                    track = range.firstTrack
                        + sourceColumn / self.copiedFieldCount;
                    field = range.firstField
                        + sourceColumn % self.copiedFieldCount;
                    if (track >= candidate.tracks.size() || field >= fields)
                        continue;
                } else {
                    const auto column = firstColumn
                        + static_cast<std::size_t>(columnOffset);
                    if (column >= maximumColumn) break;
                    s3g::tracker::app::gridAddressForClipboardColumn(column,
                        fields, track, field);
                }
                if (![self applyCellText:cells[columnOffset]
                        toTrack:candidate.tracks[track] row:destinationRow
                        page:page field:field]) {
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
    if (modifiers == NSEventModifierFlagControl) {
        NSString* key = event.charactersIgnoringModifiers.lowercaseString;
        const bool edit = [key isEqualToString:@"a"]
            || [key isEqualToString:@"c"] || [key isEqualToString:@"x"]
            || [key isEqualToString:@"v"];
        const bool zoom = event.keyCode == 24u || event.keyCode == 27u
            || event.keyCode == 29u;
        const bool page = event.keyCode >= 18u && event.keyCode <= 20u;
        if (edit || zoom || page) {
            [self keyDown:event];
            return YES;
        }
    }
    return [super performKeyEquivalent:event];
}

- (void)keyDown:(NSEvent*)event
{
    auto* model = self.trackerState;
    if (!model || model->session.pattern.tracks.empty()) {
        [super keyDown:event];
        return;
    }
    NSString* key = event.charactersIgnoringModifiers.lowercaseString;
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
    if (trackerControl && event.keyCode == 24) {
        [self.owner zoomTrackerIn];
        return;
    }
    if (trackerControl && event.keyCode == 27) {
        [self.owner zoomTrackerOut];
        return;
    }
    if (trackerControl && event.keyCode == 29) {
        [self.owner resetTrackerZoom];
        return;
    }
    if (trackerControl && event.keyCode >= 18u && event.keyCode <= 20u) {
        session.selectedPage = static_cast<std::size_t>(event.keyCode - 18u);
        session.selectedField = std::min<std::size_t>(
            session.selectedField,
            gridFieldCount(session.selectedPage) - 1u);
        [self.owner moduleSelectionChanged];
        return;
    }
    const auto editingModifiers = event.modifierFlags
        & (NSEventModifierFlagCommand | NSEventModifierFlagControl
            | NSEventModifierFlagOption);
    if (editingModifiers != 0u) {
        [super keyDown:event];
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
                session.selectedPage = (session.selectedPage + 2u) % 3u;
                session.selectedField = gridFieldCount(
                    session.selectedPage) - 1u;
            }
        } else if (session.selectedField + 1u
            < gridFieldCount(session.selectedPage)) {
            ++session.selectedField;
        } else {
            session.selectedPage = (session.selectedPage + 1u) % 3u;
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
                session.selectedPage = (session.selectedPage + 2u) % 3u;
                session.selectedField = gridFieldCount(
                    session.selectedPage) - 1u;
            }
        } else if (session.selectedField + 1u
            < gridFieldCount(session.selectedPage)) {
            ++session.selectedField;
        } else {
            session.selectedPage = (session.selectedPage + 1u) % 3u;
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
    if (session.selectedPage == 0u && session.selectedField == 0u
        && key.length == 1u) {
        const unichar direct = [key characterAtIndex:0u];
        if ((direct >= '0' && direct <= '9')
            || (direct >= 'a' && direct <= 'g')) {
            [self beginCellEditingWithInitialText:key];
            return;
        }
    }
    if (session.selectedPage == 0u && session.selectedField == 1u
        && key.length == 1u) {
        const unichar direct = [key characterAtIndex:0u];
        if (direct >= '0' && direct <= '9') {
            [self beginCellEditingWithInitialText:key];
            return;
        }
    }
    if (session.selectedPage == 0u && session.selectedField == 2u
        && key.length == 1u) {
        const unichar direct = [key characterAtIndex:0u];
        if ((direct >= '0' && direct <= '9') || direct == '.') {
            [self beginCellEditingWithInitialText:key];
            return;
        }
    }
    if (event.keyCode == 125) {
        const auto next = std::min(session.selectedRow + 1u,
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
        const auto next = session.selectedRow == 0u
            ? 0u : session.selectedRow - 1u;
        if (shift) {
            if (self.loopAnchorRow < 0)
                self.loopAnchorRow = static_cast<NSInteger>(session.selectedRow);
            [self setLoopFromAnchor:static_cast<std::size_t>(self.loopAnchorRow)
                row:next];
        } else self.loopAnchorRow = -1;
        [self selectTrack:session.selectedTrack row:next];
        return;
    }
    if (session.selectedPage == 0u && session.selectedField == 1u) {
        if ([key isEqualToString:@"x"]) {
            [self writeUsefulInstrument:YES];
            return;
        }
        if (event.keyCode == 51 || event.keyCode == 117) {
            [self writeInstrumentState:InstrumentCellState::Empty
                nodeId:0u advance:YES];
            return;
        }
        if ([key isEqualToString:@"r"]) {
            [self writeInstrumentState:InstrumentCellState::Previous
                nodeId:0u advance:YES];
            return;
        }
        if ([key isEqualToString:@"["]) {
            [self cycleInstrument:-1];
            return;
        }
        if ([key isEqualToString:@"]"]) {
            [self cycleInstrument:1];
            return;
        }
    }
    if ([key isEqualToString:@"x"] || [key isEqualToString:@"1"]) {
        if (session.selectedPage == 0u) {
            if (session.selectedField == 0u)
                [self toggleSelectedCell:YES];
            else
                [self adjustVolume:0.0f];
        } else {
            [self writeFxState:NO clear:NO];
        }
        return;
    }
    if ([key isEqualToString:@"0"] || event.keyCode == 51
        || event.keyCode == 117) {
        if (session.selectedPage == 0u) {
            if (session.selectedField == 0u)
                [self writeCellState:NoteCellState::Rest advance:YES];
            else
                [self adjustVolume:0.0f];
        } else {
            [self writeFxState:NO clear:YES];
        }
        return;
    }
    if ([key isEqualToString:@"r"]) {
        if (session.selectedPage == 0u && session.selectedField == 0u)
            [self writeCellState:NoteCellState::RetriggerPrevious advance:YES];
        else if (session.selectedPage != 0u)
            [self writeFxState:YES clear:NO];
        else
            [super keyDown:event];
        return;
    }
    if ([key isEqualToString:@"k"]) {
        if (session.selectedPage == 0u && session.selectedField == 0u)
            [self writeCellState:NoteCellState::Kill advance:YES];
        else
            [super keyDown:event];
        return;
    }
    if ([key isEqualToString:@"["]) {
        if (session.selectedPage == 0u)
            [self adjustVolume:-0.05f];
        else
            [self adjustFxValue:-5];
        return;
    }
    if ([key isEqualToString:@"]"]) {
        if (session.selectedPage == 0u)
            [self adjustVolume:0.05f];
        else
            [self adjustFxValue:5];
        return;
    }
    if ([key isEqualToString:@"m"]) {
        const auto lane = std::min(session.selectedTrack,
            session.pattern.tracks.size() - 1u);
        session.selectedTrack = lane;
        auto& track = session.pattern.tracks[lane];
        auto& muted = columnForField(track, session.selectedPage,
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

- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    auto* model = self.trackerState;
    fillRect(self.bounds, S3GTrackerThemeColor(
        S3GTrackerThemeRole::Workspace));
    fillRect(NSMakeRect(0.0, 0.0, NSWidth(self.bounds), 2.0),
        S3GTrackerThemeColor(S3GTrackerThemeRole::Focus));
    if (!model || model->session.pattern.tracks.empty()) {
        drawText(@"NO LANES", NSMakeRect(20.0, 20.0, 200.0, 20.0),
            trackerColor(0x737a80), 10.0);
        return;
    }
    const auto& session = model->session;
    const auto laneCount = std::min<std::size_t>(
        s3g::tracker::kMaximumTrackCount,
        session.pattern.tracks.size());
    const auto rows = visibleRows(model);
    const CGFloat laneWidth = gridLaneWidth(NSWidth(self.bounds), laneCount);
    const CGFloat fieldWidth = gridLaneFieldWidth(laneWidth);
    NSColor* grid = S3GTrackerThemeColor(S3GTrackerThemeRole::Grid);
    NSColor* dim = S3GTrackerThemeColor(S3GTrackerThemeRole::TextFaint);
    NSColor* text = S3GTrackerThemeColor(S3GTrackerThemeRole::TextPrimary);
    NSColor* focus = S3GTrackerThemeColor(S3GTrackerThemeRole::Focus);
    NSColor* note = S3GTrackerThemeColor(S3GTrackerThemeRole::Note);
    NSColor* instrument = S3GTrackerThemeColor(
        S3GTrackerThemeRole::Instrument);
    NSColor* value = S3GTrackerThemeColor(S3GTrackerThemeRole::Value);
    NSColor* playback = S3GTrackerThemeColor(S3GTrackerThemeRole::Live);

    fillRect(NSMakeRect(0.0, 0.0, NSWidth(self.bounds),
        kGridHeaderHeight), S3GTrackerThemeColor(
            S3GTrackerThemeRole::Panel));
    for (std::size_t row = 0u; row < rows; ++row) {
        const CGFloat y = kGridHeaderHeight
            + static_cast<CGFloat>(row) * kGridRowHeight;
        fillRect(NSMakeRect(0.0, y, NSWidth(self.bounds), kGridRowHeight),
            ((row / 4u) & 1u) == 0u
                ? S3GTrackerThemeColor(S3GTrackerThemeRole::Raised)
                : S3GTrackerThemeColor(S3GTrackerThemeRole::Panel));
        if (row >= session.transport.loopStartRow
            && row < session.transport.loopEndRow) {
            fillRect(NSMakeRect(0.0, y, NSWidth(self.bounds), kGridRowHeight),
                S3GTrackerThemeColor(S3GTrackerThemeRole::Live,
                    session.transport.loopEnabled ? 0.075 : 0.035));
        }
        if (row == session.selectedRow) {
            fillRect(NSMakeRect(0.0, y, NSWidth(self.bounds),
                    kGridRowHeight),
                S3GTrackerThemeColor(S3GTrackerThemeRole::Focus, 0.11));
        } else if ((row % 4u) == 0u) {
            fillRect(NSMakeRect(0.0, y, NSWidth(self.bounds), 1.0),
                S3GTrackerThemeColor(S3GTrackerThemeRole::Border, 0.72));
        }
        drawText([NSString stringWithFormat:@"%02lu",
            static_cast<unsigned long>(row + 1u)],
            NSMakeRect(3.0, y + 6.0, kGridRowNumberWidth - 8.0, 15.0),
            row == session.selectedRow ? focus
                : (row % 4u) == 0u
                    ? S3GTrackerThemeColor(S3GTrackerThemeRole::TextMuted)
                    : dim,
            9.6, row == session.selectedRow ? NSFontWeightSemibold
                                             : NSFontWeightMedium,
            NSTextAlignmentRight);
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
        const auto& track = session.pattern.tracks[lane];
        const auto page = std::min<std::size_t>(session.selectedPage, 2u);
        const auto pairIndex = page == 0u ? 0u : page - 1u;
        const auto fieldCount = gridFieldCount(page);
        const auto laneDestination = s3g::tracker::destinationForInstrument(
            track.initialInstrumentNodeId, track.destination);
        const bool fxInactive = page != 0u
            && !routesToInternal(laneDestination);
        std::array<const ColumnDefinition*, 3u> columns {{
            &track.noteColumn, &track.instrumentColumn, &track.velocityColumn,
        }};
        if (page != 0u) {
            columns[0u] = &track.fxPairs[pairIndex].actionColumn;
            columns[1u] = &track.fxPairs[pairIndex].valueColumn;
        }
        const CGFloat laneX = gridLaneX(lane, laneWidth);
        const CGFloat x = gridLaneFieldX(lane, laneWidth);
        const auto identityColor = trackerColor(
            kLaneColors[lane % kLaneColors.size()],
            track.noteColumn.muted ? 0.35
                : lane == session.selectedTrack ? 1.0 : 0.72);
        bool allMuted = true;
        for (std::size_t field = 0u; field < fieldCount; ++field)
            allMuted = allMuted && columns[field]->muted;

        fillRect(NSMakeRect(laneX, 2.0, laneWidth,
                kGridHeaderHeight - 2.0),
            lane == session.selectedTrack
                ? S3GTrackerThemeColor(S3GTrackerThemeRole::Selection)
                : S3GTrackerThemeColor(S3GTrackerThemeRole::Raised));
        fillRect(NSMakeRect(laneX, 2.0, 3.0,
                std::max<CGFloat>(0.0, laneHeight - 2.0)),
            identityColor);
        drawText(nsString(track.name.empty()
                ? "LANE " + std::to_string(lane + 1u) : track.name),
            NSMakeRect(x + 6.0, 5.0,
                std::max<CGFloat>(1.0, fieldWidth - 100.0), 16.0),
            fxInactive || allMuted ? dim : text,
            9.5, NSFontWeightSemibold);
        auto bus = s3g::tracker::midiOutRackSlotIndex(
            track.initialInstrumentNodeId);
        if (bus >= s3g::tracker::kMidiOutRackSlotCount) bus = 0u;
        const NSRect busRect = gridLaneBusRect(x, fieldWidth);
        const NSRect channelRect = gridLaneChannelRect(x, fieldWidth);
        fillRect(busRect, S3GTrackerThemeColor(
            S3GTrackerThemeRole::Control));
        fillRect(channelRect, S3GTrackerThemeColor(
            S3GTrackerThemeRole::Control));
        strokeRect(busRect, lane == session.selectedTrack
            ? instrument : S3GTrackerThemeColor(S3GTrackerThemeRole::Border));
        strokeRect(channelRect, lane == session.selectedTrack
            ? note : S3GTrackerThemeColor(S3GTrackerThemeRole::Border));
        drawText([NSString stringWithFormat:@"B%02lu",
                static_cast<unsigned long>(bus + 1u)],
            NSInsetRect(busRect, 2.0, 1.0),
            allMuted ? dim : instrument, 8.0, NSFontWeightSemibold,
            NSTextAlignmentCenter);
        drawText([NSString stringWithFormat:@"CH%02u",
                static_cast<unsigned int>(std::clamp<int>(
                    track.midiChannel, 1, 16))],
            NSInsetRect(channelRect, 2.0, 1.0),
            allMuted ? dim : note, 8.0, NSFontWeightSemibold,
            NSTextAlignmentCenter);
        for (std::size_t field = 0u; field < fieldCount; ++field) {
            const auto* column = columns[field];
            const NSRect headerField = gridFieldRect(x, 22.0, fieldWidth,
                kGridHeaderHeight - 22.0, page, field);
            NSString* label = nil;
            if (page == 0u) {
                constexpr std::array<const char*, 3u> labels {
                    "N", "BUS", "VOL",
                };
                label = [NSString stringWithUTF8String:labels[field]];
            } else {
                label = [NSString stringWithFormat:field == 0u
                        ? @"F%lu" : @"V%lu",
                    static_cast<unsigned long>(pairIndex + 1u)];
            }
            NSString* state = [NSString stringWithFormat:@"%@%02lu%@%u",
                label, static_cast<unsigned long>(column->length),
                directionMark(column->direction), column->stride];
            NSColor* fieldColor = page == 0u
                ? field == 0u ? note : field == 1u ? instrument : value
                : field == 0u
                    ? S3GTrackerThemeColor(S3GTrackerThemeRole::Warning)
                    : value;
            drawText(state, NSInsetRect(NSMakeRect(NSMinX(headerField),
                    24.0, NSWidth(headerField), 13.0), 2.0, 0.0),
                column->muted || fxInactive ? dim : fieldColor,
                page == 0u ? 7.6 : 8.0, NSFontWeightMedium,
                NSTextAlignmentCenter);
            drawText(@"M", NSMakeRect(NSMaxX(headerField) - 14.0,
                    39.0, 11.0, 11.0),
                column->muted
                    ? S3GTrackerThemeColor(S3GTrackerThemeRole::Danger) : dim,
                7.5, column->muted ? NSFontWeightBold
                                   : NSFontWeightRegular,
                NSTextAlignmentRight);
            if (field > 0u) {
                fillRect(NSMakeRect(NSMinX(headerField), 23.0, 1.0,
                    kGridHeaderHeight - 25.0), grid);
            }
        }
        if (lane == session.selectedTrack)
            fillRect(NSMakeRect(laneX + 1.0, kGridHeaderHeight - 3.0,
                laneWidth - 2.0, 3.0), focus);

        for (std::size_t row = 0u; row < rows; ++row) {
            const CGFloat y = kGridHeaderHeight
                + static_cast<CGFloat>(row) * kGridRowHeight;
            const bool selected = lane == session.selectedTrack
                && row == session.selectedRow;
            for (std::size_t field = 0u; field < fieldCount; ++field) {
                const auto* column = columns[field];
                const NSRect fieldRect = gridFieldRect(x, y, fieldWidth,
                    kGridRowHeight, page, field);
                const bool head = model->playing && (page == 0u
                    ? field == 0u ? row == model->notePlayheads[lane]
                        : field == 1u
                            ? row == model->instrumentPlayheads[lane]
                            : row == model->velocityPlayheads[lane]
                    : field == 0u
                        ? row == model->fxActionPlayheads[lane][pairIndex]
                        : row == model->fxValuePlayheads[lane][pairIndex]);
                NSColor* activeColor = page == 0u
                    ? field == 0u ? text
                        : field == 1u ? instrument : value
                    : field == 0u
                        ? S3GTrackerThemeColor(S3GTrackerThemeRole::Warning)
                        : value;
                NSColor* headColor = playback;
                if (head) {
                    fillRect(NSInsetRect(fieldRect, 1.0, 1.0),
                        S3GTrackerThemeColor(S3GTrackerThemeRole::Live, 0.10));
                }
                const bool unavailable = fxInactive || column->muted
                    || row >= column->length;
                if (unavailable) {
                    fillRect(NSInsetRect(fieldRect, 1.0, 1.0),
                        trackerColor(0x090b0c, 0.70));
                }
                if (_gridSelection.active
                    && _gridSelection.range().contains(page, lane, field,
                        row)) {
                    fillRect(NSInsetRect(fieldRect, 1.0, 1.0),
                        S3GTrackerThemeColor(
                            S3GTrackerThemeRole::Focus, 0.13));
                    strokeRect(NSInsetRect(fieldRect, 2.0, 2.0),
                        S3GTrackerThemeColor(
                            S3GTrackerThemeRole::Focus, 0.55), 1.0);
                }
                if (selected && field == std::min(session.selectedField,
                        fieldCount - 1u)) {
                    fillRect(NSInsetRect(fieldRect, 1.0, 1.0),
                        S3GTrackerThemeColor(S3GTrackerThemeRole::Selection));
                    strokeRect(NSInsetRect(fieldRect, 1.5, 1.5), focus, 1.5);
                }

                NSString* value = @"---";
                bool active = false;
                if (page == 0u && field == 0u) {
                    const NoteCell note = row < track.notes.size()
                        ? track.notes[row] : NoteCell::rest();
                    value = noteText(note);
                    active = note.state != NoteCellState::Rest;
                } else if (page == 0u && field == 1u) {
                    value = instrumentText(track, row,
                        &model->instrumentRack);
                    active = row < track.instruments.size()
                        && track.instruments[row].state
                            != InstrumentCellState::Empty;
                } else if (page == 0u) {
                    value = volumeText(track, row);
                    active = row < track.velocities.size()
                        && track.velocities[row].state
                            == ValueCellState::Value;
                } else if (field == 0u) {
                    value = fxActionText(track, pairIndex, row);
                    active = ![value isEqualToString:@"---"];
                } else {
                    value = fxValueText(track, pairIndex, row);
                    active = row < track.fxPairs[pairIndex].values.size()
                        && track.fxPairs[pairIndex].values[row].state
                            == FxValueCellState::Value;
                }
                drawText(value, NSInsetRect(NSMakeRect(NSMinX(fieldRect),
                        y + 5.0, NSWidth(fieldRect), 16.0), 3.0, 0.0),
                    active && !unavailable ? activeColor : dim,
                    page == 0u && field == 2u ? 8.8 : 9.8,
                    active ? NSFontWeightMedium : NSFontWeightRegular,
                    field == 2u && page == 0u
                        ? NSTextAlignmentRight : NSTextAlignmentCenter);
                if (head) {
                    fillRect(NSMakeRect(NSMinX(fieldRect) + 1.0, y + 3.0,
                        2.0, kGridRowHeight - 6.0),
                        unavailable ? dim : headColor);
                }
                strokeRect(fieldRect, grid);
            }
        }
        for (std::size_t field = 0u; field < fieldCount; ++field) {
            const auto* column = columns[field];
            if (column->length > rows) continue;
            const CGFloat lengthY = kGridHeaderHeight
                + static_cast<CGFloat>(column->length) * kGridRowHeight;
            const NSRect fieldRect = gridFieldRect(x, lengthY - 1.0,
                fieldWidth, 2.0, page, field);
            NSColor* lengthColor = page == 0u
                ? field == 0u ? note : field == 1u ? instrument : value
                : field == 0u
                    ? S3GTrackerThemeColor(S3GTrackerThemeRole::Warning)
                    : value;
            fillRect(fieldRect, column->muted || fxInactive
                ? dim : lengthColor);
        }
        strokeRect(NSInsetRect(NSMakeRect(laneX, 1.0, laneWidth,
                laneHeight - 1.0), 0.75, 0.75),
            lane == session.selectedTrack ? focus : trackerColor(0x3c4144),
            lane == session.selectedTrack ? 1.5 : 1.0);
    }
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
            self.owner.gridScroll.magnification * 100.0)) : 100;
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

@interface S3GTrackerGeometryView : NSView
- (instancetype)initWithState:(TrackerViewState*)state
    owner:(S3GTrackerWorkspaceController*)owner;
@property(nonatomic, assign) TrackerViewState* trackerState;
@property(nonatomic, weak) S3GTrackerWorkspaceController* owner;
@end

@implementation S3GTrackerGeometryView

- (instancetype)initWithState:(TrackerViewState*)state
    owner:(S3GTrackerWorkspaceController*)owner
{
    self = [super initWithFrame:NSZeroRect];
    if (self) {
        self.trackerState = state;
        self.owner = owner;
        self.accessibilityElement = YES;
        self.accessibilityRole = NSAccessibilityGroupRole;
        self.accessibilityLabel = @"Rhythm geometry";
        self.accessibilityHelp = @"Use arrow keys or click a rhythm layer to select a lane; Space toggles playback.";
    }
    return self;
}

- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }

- (void)selectLane:(std::size_t)lane
{
    auto* model = self.trackerState;
    if (!model || model->session.pattern.tracks.empty()) return;
    const auto lanes = std::min<std::size_t>(
        s3g::tracker::kMaximumTrackCount,
        model->session.pattern.tracks.size());
    model->session.selectedTrack = std::min(lane, lanes - 1u);
    self.accessibilityValue = [NSString stringWithFormat:@"Lane %lu, %@",
        static_cast<unsigned long>(model->session.selectedTrack + 1u),
        nsString(model->session.pattern.tracks[
            model->session.selectedTrack].name)];
    [self.owner moduleSelectionChanged];
}

- (void)mouseDown:(NSEvent*)event
{
    auto* model = self.trackerState;
    if (!model || model->session.pattern.tracks.empty()) return;
    const NSPoint point = [self convertPoint:event.locationInWindow fromView:nil];
    const auto visible = visibleGeometryLanes(model);
    if (visible.count == 0u) return;
    std::size_t selectedOrdinal = 0u;
    if (point.x > NSWidth(self.bounds) - 112.0) {
        constexpr CGFloat legendTop = 28.0;
        constexpr CGFloat legendRowHeight = 18.0;
        if (point.y < legendTop
            || point.y >= legendTop
                + static_cast<CGFloat>(visible.count) * legendRowHeight)
            return;
        selectedOrdinal = static_cast<std::size_t>(
            (point.y - legendTop) / legendRowHeight);
    } else {
        if (point.y < 24.0) return;
        const CGFloat cx = std::min(NSWidth(self.bounds) * 0.39,
            NSHeight(self.bounds) * 0.5);
        const CGFloat cy = NSHeight(self.bounds) * 0.55;
        const CGFloat maximum = std::max<CGFloat>(30.0,
            std::min(cx - 8.0, NSHeight(self.bounds) * 0.42));
        const CGFloat spacing = maximum
            / static_cast<CGFloat>(visible.count + 1u);
        const CGFloat distance = std::hypot(point.x - cx, point.y - cy);
        CGFloat best = 1.0e9;
        for (std::size_t ordinal = 0u; ordinal < visible.count; ++ordinal) {
            const CGFloat radius = spacing
                * static_cast<CGFloat>(ordinal + 2u);
            const CGFloat difference = std::abs(distance - radius);
            if (difference < best) {
                best = difference;
                selectedOrdinal = ordinal;
            }
        }
        if (best > std::max<CGFloat>(6.0, spacing * 0.35)) return;
    }
    [self.window makeFirstResponder:self];
    [self selectLane:visible.indices[selectedOrdinal]];
}

- (void)keyDown:(NSEvent*)event
{
    auto* model = self.trackerState;
    if (!model || model->session.pattern.tracks.empty()) {
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

- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    fillRect(self.bounds, trackerColor(0x1d1d1d));
    strokeRect(NSInsetRect(self.bounds, 0.5, 0.5), trackerColor(0x565656));
    fillRect(NSMakeRect(0.0, 0.0, NSWidth(self.bounds), 24.0),
        trackerColor(0x131313));
    fillRect(NSMakeRect(0.0, 0.0, NSWidth(self.bounds), 2.0),
        trackerColor(0xb8b8b8));
    const NSRect plotRect = NSMakeRect(8.0, 30.0,
        std::max<CGFloat>(0.0, NSWidth(self.bounds) - 128.0),
        std::max<CGFloat>(0.0, NSHeight(self.bounds) - 38.0));
    fillRect(plotRect, trackerColor(0x0c0c0c));
    strokeRect(NSInsetRect(plotRect, 0.5, 0.5), trackerColor(0x383838));
    drawText(@"RHYTHM GEOMETRY  /  NOTE", NSMakeRect(8.0, 6.0,
        NSWidth(self.bounds) - 16.0, 16.0), trackerColor(0xa8a8a8), 9.5);
    auto* model = self.trackerState;
    if (!model || model->session.pattern.tracks.empty()) return;
    const auto visible = visibleGeometryLanes(model);
    if (visible.count == 0u) {
        drawText(@"ALL NOTE LANES MUTED", NSMakeRect(
            NSMinX(plotRect) + 14.0, NSMidY(plotRect) - 7.0,
            NSWidth(plotRect) - 28.0, 14.0), trackerColor(0x6f6f6f),
            9.0, NSFontWeightMedium, NSTextAlignmentCenter);
        return;
    }
    const CGFloat cx = std::min(NSWidth(self.bounds) * 0.39,
        NSHeight(self.bounds) * 0.5);
    const CGFloat cy = NSHeight(self.bounds) * 0.55;
    const CGFloat maximum = std::max<CGFloat>(30.0,
        std::min(cx - 8.0, NSHeight(self.bounds) * 0.42));
    const CGFloat spacing = maximum
        / static_cast<CGFloat>(visible.count + 1u);

    for (std::size_t ordinal = visible.count; ordinal-- > 0u;) {
        const auto lane = visible.indices[ordinal];
        const auto& track = model->session.pattern.tracks[lane];
        const auto length = std::clamp<std::size_t>(
            track.noteColumn.length, 1u, 256u);
        const CGFloat radius = spacing
            * static_cast<CGFloat>(ordinal + 2u);
        const bool selected = lane == model->session.selectedTrack;
        const CGFloat alpha = selected ? 1.0 : 0.76;
        NSColor* laneColor = trackerColor(
            kGeometryLaneColors[lane % kGeometryLaneColors.size()], alpha);
        NSBezierPath* polygon = [NSBezierPath bezierPath];
        bool started = false;
        for (std::size_t step = 0u; step < length; ++step) {
            const CGFloat angle = -static_cast<CGFloat>(M_PI_2)
                + static_cast<CGFloat>(step) * 2.0 * static_cast<CGFloat>(M_PI)
                    / static_cast<CGFloat>(length);
            const CGFloat x = cx + std::cos(angle) * radius;
            const CGFloat y = cy + std::sin(angle) * radius;
            const bool hit = step < track.notes.size()
                && (track.notes[step].state == NoteCellState::Note
                    || track.notes[step].state
                        == NoteCellState::RetriggerPrevious);
            if (hit) {
                if (!started) { [polygon moveToPoint:NSMakePoint(x, y)]; started = true; }
                else [polygon lineToPoint:NSMakePoint(x, y)];
            }
        }
        if (started) {
            [polygon closePath];
            polygon.lineWidth = selected ? 1.2 : 0.6;
            [laneColor setStroke];
            [polygon stroke];
        }

        // The polygon shows authored pulse structure without vertex dots. The
        // one yellow point is dynamic and exists only when the sequencer says
        // this lane's current NOTE position emitted a real onset.
        if (model->playing && model->noteHits[lane]) {
            const auto position = model->notePlayheads[lane] % length;
            const CGFloat angle = -static_cast<CGFloat>(M_PI_2)
                + static_cast<CGFloat>(position) * 2.0
                    * static_cast<CGFloat>(M_PI)
                    / static_cast<CGFloat>(length);
            const NSPoint point = NSMakePoint(
                cx + std::cos(angle) * radius,
                cy + std::sin(angle) * radius);
            NSBezierPath* hitPoint = [NSBezierPath bezierPathWithOvalInRect:
                NSMakeRect(point.x - 2.8, point.y - 2.8, 5.6, 5.6)];
            [S3GTrackerThemeColor(S3GTrackerThemeRole::Value, alpha) setFill];
            [hitPoint fill];
        }
        const CGFloat legendY = 32.0
            + static_cast<CGFloat>(ordinal) * 18.0;
        drawText(nsString(track.name), NSMakeRect(NSWidth(self.bounds) - 108.0,
            legendY, 67.0, 14.0), laneColor, 7.5,
            selected ? NSFontWeightBold : NSFontWeightRegular);
        std::size_t hits = 0u;
        for (std::size_t row = 0u; row < length; ++row)
            if (row < track.notes.size()
                && (track.notes[row].state == NoteCellState::Note
                    || track.notes[row].state
                        == NoteCellState::RetriggerPrevious)) ++hits;
        drawText([NSString stringWithFormat:@"%lu/%lu%@",
            static_cast<unsigned long>(hits),
            static_cast<unsigned long>(length),
            directionMark(track.noteColumn.direction)],
            NSMakeRect(NSWidth(self.bounds) - 42.0, legendY, 36.0, 14.0),
            laneColor, 7.5, NSFontWeightRegular, NSTextAlignmentRight);
    }
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
    const NSRect frame = NSMakeRect(0.0, 0.0, 620.0, 560.0);
    NSWindow* window = [[NSWindow alloc] initWithContentRect:frame
        styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
            | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable)
        backing:NSBackingStoreBuffered defer:NO];
    self = [super initWithWindow:window];
    if (self) {
        window.title = @"s3g Tracker — Rhythm Geometry";
        window.minSize = NSMakeSize(480.0, 390.0);
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

@interface S3GTrackerEnvelopeView : NSView
- (instancetype)initWithState:(TrackerViewState*)state
    owner:(S3GTrackerWorkspaceController*)owner;
@property(nonatomic, assign) TrackerViewState* trackerState;
@property(nonatomic, weak) S3GTrackerWorkspaceController* owner;
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
        self.accessibilityElement = YES;
        self.accessibilityRole = NSAccessibilityGroupRole;
        self.accessibilityLabel = @"Volume envelope";
        self.accessibilityHelp = @"Drag in the graph to paint normalized volume. Option-click writes Previous. Brackets in the tracker adjust the selected value.";
    }
    return self;
}

- (BOOL)isFlipped { return YES; }

- (void)paintEvent:(NSEvent*)event clear:(BOOL)clear
{
    auto* model = self.trackerState;
    if (!model || model->session.pattern.tracks.empty()) return;
    auto& session = model->session;
    const auto lane = std::min(session.selectedTrack,
        session.pattern.tracks.size() - 1u);
    auto& track = session.pattern.tracks[lane];
    const auto page = std::min<std::size_t>(session.selectedPage, 2u);
    const auto pairIndex = page == 0u ? 0u : page - 1u;
    const auto rows = std::max<std::size_t>(16u,
        std::min<std::size_t>(256u, page == 0u
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
    if (page == 0u) {
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
    self.lastPaintedRow = -1;
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
    if (!model || model->session.pattern.tracks.empty()) return;
    const auto lane = std::min(model->session.selectedTrack,
        model->session.pattern.tracks.size() - 1u);
    const auto& track = model->session.pattern.tracks[lane];
    const auto page = std::min<std::size_t>(
        model->session.selectedPage, 2u);
    const auto pairIndex = page == 0u ? 0u : page - 1u;
    const bool fxInactive = page != 0u
        && !routesToInternal(s3g::tracker::destinationForInstrument(
            track.initialInstrumentNodeId, track.destination));
    const auto rows = std::max<std::size_t>(16u,
        std::min<std::size_t>(256u, page == 0u
                ? track.velocityColumn.length
                : track.fxPairs[pairIndex].valueColumn.length));
    NSString* envelopeName = page == 0u ? @"VOLUME"
        : [NSString stringWithFormat:@"FX%lu VALUE",
            static_cast<unsigned long>(pairIndex + 1u)];
    drawText([NSString stringWithFormat:@"%@ ENVELOPE  /  T%lu",
        envelopeName, static_cast<unsigned long>(lane + 1u)], NSMakeRect(8.0, 6.0,
        NSWidth(self.bounds) - 16.0, 16.0),
        fxInactive ? trackerColor(0x5d6367) : trackerColor(0xa8a8a8), 9.5);
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
        const float value = page == 0u ? resolvedVelocity(track, row)
            : resolvedFxValue(track, pairIndex, row);
        const CGFloat y = top + (1.0 - value) * height;
        if (row == 0u) [curve moveToPoint:NSMakePoint(x, y)];
        else [curve lineToPoint:NSMakePoint(x, y)];
    }
    NSColor* curveColor = fxInactive ? trackerColor(0x5d6367, 0.65)
                                     : trackerColor(0xb8b8b8, 0.8);
    [curveColor setStroke];
    curve.lineWidth = 1.2;
    [curve stroke];
    for (std::size_t row = 0u; row < rows; ++row) {
        const bool explicitValue = page == 0u
            ? row < track.velocities.size()
                && track.velocities[row].state == ValueCellState::Value
            : row < track.fxPairs[pairIndex].values.size()
                && track.fxPairs[pairIndex].values[row].state
                    == FxValueCellState::Value;
        if (!explicitValue) continue;
        const CGFloat x = left + (static_cast<CGFloat>(row) + 0.5)
            * width / static_cast<CGFloat>(rows);
        const float value = page == 0u ? resolvedVelocity(track, row)
            : resolvedFxValue(track, pairIndex, row);
        const CGFloat y = top + (1.0 - value) * height;
        NSColor* pointColor = fxInactive ? trackerColor(0x5d6367)
            : row == model->session.selectedRow
                ? trackerColor(0xe0e0e0) : trackerColor(0xa8a8a8);
        fillRect(NSMakeRect(x - 2.5, y - 2.5, 5.0, 5.0), pointColor);
    }
    const auto playhead = (page == 0u ? model->velocityPlayheads[lane]
        : model->fxValuePlayheads[lane][pairIndex]) % rows;
    if (model->playing) {
        const CGFloat x = left + (static_cast<CGFloat>(playhead) + 0.5)
            * width / static_cast<CGFloat>(rows);
        fillRect(NSMakeRect(x - 1.0, top, 2.0, height),
            fxInactive ? trackerColor(0x5d6367, 0.5)
                       : trackerColor(0xb8b8b8, 0.7));
    }
    drawText(fxInactive
            ? @"INACTIVE FOR MIDI OUT   EDITS ARE RETAINED"
            : @"CLICK/DRAG: PAINT   OPTION-CLICK: PREVIOUS",
        NSMakeRect(left, NSHeight(self.bounds) - 17.0, width, 12.0),
        trackerColor(0x737a80), 7.0);
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

- (NSButton*)button:(NSString*)title action:(SEL)action
{
    S3GTrackerActionButton* button = [[S3GTrackerActionButton alloc]
        initWithFrame:NSZeroRect];
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
    NSTextField* field = [[NSTextField alloc] initWithFrame:NSZeroRect];
    S3GTrackerStyleTextField(field, NSTextAlignmentRight);
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
    return field;
}

- (void)loadView
{
    NSView* root = [[NSView alloc] initWithFrame:NSMakeRect(0.0, 0.0,
        1320.0, 780.0)];
    root.wantsLayer = YES;
    root.layer.backgroundColor = S3GTrackerThemeColor(
        S3GTrackerThemeRole::Canvas).CGColor;
    self.view = root;

    self.toolbar = [[NSView alloc] initWithFrame:NSZeroRect];
    self.toolbar.wantsLayer = YES;
    self.toolbar.layer.backgroundColor = S3GTrackerThemeColor(
        S3GTrackerThemeRole::Panel).CGColor;
    self.toolbar.translatesAutoresizingMaskIntoConstraints = NO;
    [root addSubview:self.toolbar];

    self.transportControls = [[NSStackView alloc] initWithFrame:NSZeroRect];
    NSStackView* controls = self.transportControls;
    controls.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    controls.alignment = NSLayoutAttributeCenterY;
    controls.spacing = 7.0;
    controls.edgeInsets = NSEdgeInsetsMake(0.0, 14.0, 0.0, 14.0);
    self.transportScroll = [self horizontalStripForStack:controls];
    self.transportScroll.accessibilityLabel = @"Transport and pattern controls";
    [self.toolbar addSubview:self.transportScroll];

    NSStackView* titleStack = [[NSStackView alloc] initWithFrame:NSZeroRect];
    titleStack.orientation = NSUserInterfaceLayoutOrientationVertical;
    titleStack.alignment = NSLayoutAttributeLeading;
    titleStack.spacing = 0.0;
    [titleStack addArrangedSubview:[self label:@"s3g TRACKER" size:15.0
        color:trackerColor(0xa6a6a6)]];
    [titleStack.widthAnchor constraintEqualToConstant:144.0].active = YES;
    [controls addArrangedSubview:titleStack];

    [controls addArrangedSubview:[self label:@"PATTERN" size:9.0
        color:trackerColor(0xa8a8a8)]];
    self.patternPopup = [[S3GTrackerPopupButton alloc]
        initWithFrame:NSZeroRect pullsDown:NO];
    self.patternPopup.target = self;
    self.patternPopup.action = @selector(patternSelectionChanged:);
    self.patternPopup.accessibilityLabel = @"Active pattern";
    [self.patternPopup.widthAnchor constraintGreaterThanOrEqualToConstant:
        156.0].active = YES;
    [controls addArrangedSubview:self.patternPopup];
    self.createPatternButton = [self button:@"＋"
        action:@selector(newPatternPressed:)];
    self.createPatternButton.toolTip = @"Create a new blank pattern";
    self.createPatternButton.accessibilityLabel = @"New pattern";
    [controls addArrangedSubview:self.createPatternButton];
    self.duplicatePatternButton = [self button:@"DUP"
        action:@selector(duplicatePatternPressed:)];
    self.duplicatePatternButton.toolTip = @"Duplicate the active pattern";
    self.duplicatePatternButton.accessibilityLabel = @"Duplicate pattern";
    [controls addArrangedSubview:self.duplicatePatternButton];
    self.renamePatternButton = [self button:@"NAME"
        action:@selector(renamePatternPressed:)];
    self.renamePatternButton.toolTip = @"Rename the active pattern without changing its stable ID";
    self.renamePatternButton.accessibilityLabel = @"Rename pattern";
    [controls addArrangedSubview:self.renamePatternButton];
    self.deletePatternButton = [self button:@"−"
        action:@selector(deletePatternPressed:)];
    self.deletePatternButton.tag = 2;
    self.deletePatternButton.toolTip = @"Delete the active unreferenced pattern";
    self.deletePatternButton.accessibilityLabel = @"Delete pattern";
    [controls addArrangedSubview:self.deletePatternButton];

    self.playButton = [self button:@"▶" action:@selector(playPressed:)];
    self.playButton.tag = 3;
    self.playButton.toolTip = @"Play / pause / resume (Space)";
    self.playButton.accessibilityLabel = @"Play or pause";
    [self.playButton.widthAnchor constraintEqualToConstant:31.0].active = YES;
    [controls addArrangedSubview:self.playButton];
    self.loopButton = [self button:@"↻" action:@selector(loopPressed:)];
    self.loopButton.tag = 1;
    self.loopButton.toolTip = @"Toggle global row loop (Shift-Space)";
    self.loopButton.accessibilityLabel = @"Loop";
    [self.loopButton.widthAnchor constraintEqualToConstant:31.0].active = YES;
    [controls addArrangedSubview:self.loopButton];
    self.stopButton = [self button:@"■" action:@selector(stopPressed:)];
    self.stopButton.toolTip = @"Stop and return to row 1";
    self.stopButton.accessibilityLabel = @"Stop";
    [self.stopButton.widthAnchor constraintEqualToConstant:31.0].active = YES;
    [controls addArrangedSubview:self.stopButton];
    self.pauseButton = [self button:@"Ⅱ" action:@selector(pausePressed:)];
    self.pauseButton.tag = 3;
    self.pauseButton.toolTip = @"Pause / resume at current position (Space)";
    self.pauseButton.accessibilityLabel = @"Pause";
    [self.pauseButton.widthAnchor constraintEqualToConstant:31.0].active = YES;
    [controls addArrangedSubview:self.pauseButton];
    NSButton* panicButton = [self button:@"! PANIC"
        action:@selector(panicPressed:)];
    panicButton.tag = 2;
    [controls addArrangedSubview:panicButton];

    [controls addArrangedSubview:[self label:@"BPM" size:9.0 color:trackerColor(0xa8a8a8)]];
    self.bpmField = [self numberField:@selector(transportFieldChanged:)
        minimum:20.0 maximum:400.0 fractionDigits:2u];
    self.bpmField.accessibilityLabel = @"Tempo in beats per minute";
    [self.bpmField.widthAnchor constraintEqualToConstant:55.0].active = YES;
    [controls addArrangedSubview:self.bpmField];
    [controls addArrangedSubview:[self label:@"SW%" size:9.0 color:trackerColor(0xa8a8a8)]];
    self.swingField = [self numberField:@selector(transportFieldChanged:)
        minimum:50.0 maximum:75.0 fractionDigits:1u];
    self.swingField.accessibilityLabel = @"Swing percentage";
    [self.swingField.widthAnchor constraintEqualToConstant:43.0].active = YES;
    [controls addArrangedSubview:self.swingField];
    [controls addArrangedSubview:[self label:@"GATE" size:9.0 color:trackerColor(0xa8a8a8)]];
    self.gateField = [self numberField:@selector(gateFieldChanged:)
        minimum:1.0 maximum:5000.0 fractionDigits:1u];
    self.gateField.accessibilityLabel = @"MIDI gate in milliseconds";
    [self.gateField.widthAnchor constraintEqualToConstant:48.0].active = YES;
    [controls addArrangedSubview:self.gateField];

    [controls addArrangedSubview:[self label:@"LOOP" size:9.0 color:trackerColor(0xa8a8a8)]];
    self.loopStartField = [self numberField:@selector(transportFieldChanged:)
        minimum:1.0 maximum:256.0 fractionDigits:0u];
    self.loopStartField.toolTip = @"First loop row";
    [self.loopStartField.widthAnchor constraintEqualToConstant:37.0].active = YES;
    [controls addArrangedSubview:self.loopStartField];
    [controls addArrangedSubview:[self label:@"–" size:10.0 color:trackerColor(0x8e8e8e)]];
    self.loopEndField = [self numberField:@selector(transportFieldChanged:)
        minimum:1.0 maximum:256.0 fractionDigits:0u];
    self.loopEndField.toolTip = @"Last loop row";
    [self.loopEndField.widthAnchor constraintEqualToConstant:37.0].active = YES;
    [controls addArrangedSubview:self.loopEndField];

    self.moduleControls = [[NSStackView alloc] initWithFrame:NSZeroRect];
    NSStackView* moduleButtons = self.moduleControls;
    moduleButtons.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    moduleButtons.alignment = NSLayoutAttributeCenterY;
    moduleButtons.spacing = 6.0;
    moduleButtons.edgeInsets = NSEdgeInsetsMake(0.0, 14.0, 0.0, 14.0);
    self.pageButton = [self button:@"NOTE / BUS / VOL"
        action:@selector(cycleGridPage:)];
    self.pageButton.accessibilityLabel = @"Tracker column page";
    [moduleButtons addArrangedSubview:self.pageButton];
    [moduleButtons addArrangedSubview:[self button:@"+ TRACK"
        action:@selector(trackAddPressed:)]];
    [moduleButtons addArrangedSubview:[self button:@"− TRACK"
        action:@selector(trackRemovePressed:)]];
    self.moduleScroll = [self horizontalStripForStack:moduleButtons];
    self.moduleScroll.accessibilityLabel = @"Tracker module controls";
    [self.toolbar addSubview:self.moduleScroll];

    self.routeStatus = [self label:@"MIDI ROUTE" size:8.0
        color:trackerColor(0xa0a0a0)];
    self.routeStatus.lineBreakMode = NSLineBreakByTruncatingMiddle;
    self.routeStatus.toolTip = self.routeStatus.stringValue;
    [self.routeStatus setContentCompressionResistancePriority:1.0
        forOrientation:NSLayoutConstraintOrientationHorizontal];
    self.routeStatus.translatesAutoresizingMaskIntoConstraints = NO;
    [self.toolbar addSubview:self.routeStatus];
    self.eventStatus = [self label:@"NO EVENTS" size:8.0
        color:trackerColor(0x737a80)];
    self.eventStatus.alignment = NSTextAlignmentRight;
    self.eventStatus.lineBreakMode = NSLineBreakByTruncatingTail;
    self.eventStatus.toolTip = self.eventStatus.stringValue;
    [self.eventStatus setContentCompressionResistancePriority:1.0
        forOrientation:NSLayoutConstraintOrientationHorizontal];
    self.eventStatus.translatesAutoresizingMaskIntoConstraints = NO;
    [self.toolbar addSubview:self.eventStatus];

    self.gridView = [[S3GTrackerGridView alloc] initWithState:self.trackerState
        owner:self];
    self.gridScroll = [[NSScrollView alloc] initWithFrame:NSZeroRect];
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
    self.gridScroll.magnification = 1.0;
    self.gridScroll.documentView = self.gridView;
    self.gridScroll.translatesAutoresizingMaskIntoConstraints = NO;
    [root addSubview:self.gridScroll];

    self.geometryWindowController = [[S3GTrackerGeometryWindowController alloc]
        initWithState:self.trackerState owner:self];
    self.geometryView = self.geometryWindowController.geometryView;
    self.warpWindowController = [[S3GTrackerWarpWindowController alloc]
        initWithState:self.trackerState callbacks:self.trackerCallbacks];
    self.envelopeView = [[S3GTrackerEnvelopeView alloc]
        initWithState:self.trackerState owner:self];
    self.envelopeView.translatesAutoresizingMaskIntoConstraints = NO;
    [root addSubview:self.envelopeView];

    self.consolePanel = [[S3GTrackerPanelView alloc] initWithFrame:NSZeroRect];
    self.consolePanel.translatesAutoresizingMaskIntoConstraints = NO;
    [root addSubview:self.consolePanel];
    NSTextField* consoleTitle = [self label:@"LIVE CODE  /  : OR ` TO FOCUS"
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

    self.consoleOutputPanel = [[S3GTrackerPanelView alloc]
        initWithFrame:NSZeroRect];
    self.consoleOutputPanel.translatesAutoresizingMaskIntoConstraints = NO;
    self.consoleOutputPanel.accessibilityElement = YES;
    self.consoleOutputPanel.accessibilityRole = NSAccessibilityGroupRole;
    self.consoleOutputPanel.accessibilityLabel = @"Console output page";
    NSTextField* outputTitle = [self label:@"CONSOLE OUTPUT"
        size:8.5 color:trackerColor(0xa8a8a8)];
    outputTitle.translatesAutoresizingMaskIntoConstraints = NO;
    [self.consoleOutputPanel addSubview:outputTitle];
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
        [self.transportScroll.topAnchor constraintEqualToAnchor:self.toolbar.topAnchor constant:4.0],
        [self.transportScroll.heightAnchor constraintEqualToConstant:37.0],
        [self.routeStatus.leadingAnchor constraintEqualToAnchor:self.toolbar.leadingAnchor constant:14.0],
        [self.routeStatus.trailingAnchor constraintLessThanOrEqualToAnchor:self.toolbar.centerXAnchor constant:-8.0],
        [self.routeStatus.bottomAnchor constraintEqualToAnchor:self.toolbar.bottomAnchor constant:-3.0],
        [self.eventStatus.trailingAnchor constraintEqualToAnchor:self.toolbar.trailingAnchor constant:-14.0],
        [self.eventStatus.leadingAnchor constraintGreaterThanOrEqualToAnchor:self.toolbar.centerXAnchor constant:8.0],
        [self.eventStatus.bottomAnchor constraintEqualToAnchor:self.toolbar.bottomAnchor constant:-3.0],
        [self.eventStatus.leadingAnchor constraintGreaterThanOrEqualToAnchor:self.routeStatus.trailingAnchor constant:16.0],

        [self.moduleScroll.leadingAnchor constraintEqualToAnchor:self.toolbar.leadingAnchor],
        [self.moduleScroll.trailingAnchor constraintEqualToAnchor:self.toolbar.trailingAnchor],
        [self.moduleScroll.topAnchor constraintEqualToAnchor:self.transportScroll.bottomAnchor constant:2.0],
        [self.moduleScroll.heightAnchor constraintEqualToConstant:29.0],

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
        [self.envelopeView.bottomAnchor constraintEqualToAnchor:root.bottomAnchor],
        self.envelopeHeightConstraint,

        [consoleTitle.leadingAnchor constraintEqualToAnchor:self.consolePanel.leadingAnchor constant:12.0],
        [consoleTitle.centerYAnchor constraintEqualToAnchor:self.consolePanel.centerYAnchor],
        [consoleTitle.widthAnchor constraintEqualToConstant:190.0],
        [prompt.leadingAnchor constraintEqualToAnchor:consoleTitle.trailingAnchor constant:3.0],
        [prompt.centerYAnchor constraintEqualToAnchor:self.consoleInput.centerYAnchor],
        [prompt.widthAnchor constraintEqualToConstant:12.0],
        [self.consoleInput.leadingAnchor constraintEqualToAnchor:prompt.trailingAnchor constant:2.0],
        [self.consoleInput.trailingAnchor constraintEqualToAnchor:self.consolePanel.trailingAnchor constant:-10.0],
        [self.consoleInput.centerYAnchor constraintEqualToAnchor:self.consolePanel.centerYAnchor],
        [self.consoleInput.heightAnchor constraintEqualToConstant:24.0],

        [outputTitle.leadingAnchor constraintEqualToAnchor:self.consoleOutputPanel.leadingAnchor constant:12.0],
        [outputTitle.topAnchor constraintEqualToAnchor:self.consoleOutputPanel.topAnchor constant:10.0],
        [outputScroll.leadingAnchor constraintEqualToAnchor:self.consoleOutputPanel.leadingAnchor constant:8.0],
        [outputScroll.trailingAnchor constraintEqualToAnchor:self.consoleOutputPanel.trailingAnchor constant:-8.0],
        [outputScroll.topAnchor constraintEqualToAnchor:outputTitle.bottomAnchor constant:6.0],
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
    [self resizeHorizontalStrip:self.moduleControls
        inScrollView:self.moduleScroll];
    const std::size_t lanes = self.trackerState
        ? std::min<std::size_t>(s3g::tracker::kMaximumTrackCount,
            self.trackerState->session.pattern.tracks.size()) : 0u;
    const CGFloat width = static_cast<CGFloat>(
        s3g::tracker::app::trackerDocumentWidth(lanes,
            NSWidth(self.gridScroll.contentView.bounds)));
    const CGFloat height = kGridHeaderHeight
        + static_cast<CGFloat>(visibleRows(self.trackerState)) * kGridRowHeight;
    self.gridView.frame = NSMakeRect(0.0, 0.0, width,
        std::max(height, NSHeight(self.gridScroll.contentView.bounds)));
}

- (void)refreshStatusMetadata
{
    self.routeStatus.toolTip = self.routeStatus.stringValue;
    self.eventStatus.toolTip = self.eventStatus.stringValue;
    self.routeStatus.accessibilityValue = self.routeStatus.stringValue;
    self.eventStatus.accessibilityValue = self.eventStatus.stringValue;
}

- (void)reloadModel
{
    auto* state = self.trackerState;
    if (!state || !self.isViewLoaded) return;
    if (state->session.pattern.tracks.empty()) {
        state->session.selectedTrack = 0u;
    } else {
        state->session.selectedTrack = std::min(state->session.selectedTrack,
            state->session.pattern.tracks.size() - 1u);
    }
    state->session.selectedRow = std::min<std::size_t>(
        state->session.selectedRow, 255u);
    state->session.selectedPage = std::min<std::size_t>(
        state->session.selectedPage, 2u);
    state->session.selectedField = std::min<std::size_t>(
        state->session.selectedField,
        gridFieldCount(state->session.selectedPage) - 1u);
    state->mainOutputGain = std::clamp(
        std::isfinite(state->mainOutputGain) ? state->mainOutputGain : 1.0f,
        0.0f, 1.0f);
    state->mixerSelectedStrip = std::min(state->mixerSelectedStrip,
        state->session.pattern.tracks.size());
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
    self.pageButton.title = gridPageTitle(state->session.selectedPage);
    self.playButton.state = state->playing
        ? NSControlStateValueOn : NSControlStateValueOff;
    [self.playButton setNeedsDisplay:YES];
    self.loopButton.state = state->session.transport.loopEnabled
        ? NSControlStateValueOn : NSControlStateValueOff;
    self.pauseButton.state = state->paused
        ? NSControlStateValueOn : NSControlStateValueOff;
    [self.loopButton setNeedsDisplay:YES];
    [self.pauseButton setNeedsDisplay:YES];
    self.bpmField.doubleValue = state->session.transport.bpm;
    self.swingField.integerValue = static_cast<NSInteger>(std::lround(
        state->session.transport.swing * 100.0));
    self.gateField.doubleValue = state->session.gateMilliseconds;
    self.loopStartField.integerValue = static_cast<NSInteger>(
        state->session.transport.loopStartRow + 1u);
    self.loopEndField.integerValue = static_cast<NSInteger>(
        state->session.transport.loopEndRow);
    self.routeStatus.stringValue = [NSString stringWithFormat:
        @"HOST: REAPER  •  MIDI: %@  •  WARP %lu/%u  •  %@",
        nsString(state->midiRoute),
        static_cast<unsigned long>(state->session.transport.timingWarp.size()),
        state->session.transport.warpCycleTicks, nsString(state->status)];
    self.eventStatus.stringValue = [NSString stringWithFormat:@"%@  •  SENT %llu  DROP %llu  LATE %llu  CLK %llu",
        nsString(state->lastEvent), state->sentEventCount,
        state->droppedEventCount, state->audioLateEventCount,
        state->audioClockFaultCount];
    [self refreshStatusMetadata];
    [self.gridView setNeedsDisplay:YES];
    [self.gridView refreshAccessibilityValue];
    [self applyWorkspaceMode];
    [self.geometryView setNeedsDisplay:YES];
    [self.warpWindowController reloadModel];
    [self.envelopeView setNeedsDisplay:YES];
    [self.view setNeedsLayout:YES];
    [self.gridView scrollSelectionToVisible];
}

- (void)refreshPlaybackDisplay
{
    if (!self.isViewLoaded || !self.trackerState) return;
    self.playButton.state = self.trackerState->playing
        ? NSControlStateValueOn : NSControlStateValueOff;
    [self.playButton setNeedsDisplay:YES];
    self.loopButton.state = self.trackerState->session.transport.loopEnabled
        ? NSControlStateValueOn : NSControlStateValueOff;
    self.pauseButton.state = self.trackerState->paused
        ? NSControlStateValueOn : NSControlStateValueOff;
    [self.loopButton setNeedsDisplay:YES];
    [self.pauseButton setNeedsDisplay:YES];
    self.routeStatus.stringValue = [NSString stringWithFormat:
        @"HOST: REAPER  •  MIDI: %@  •  WARP %lu/%u  •  %@",
        nsString(self.trackerState->midiRoute),
        static_cast<unsigned long>(
            self.trackerState->session.transport.timingWarp.size()),
        self.trackerState->session.transport.warpCycleTicks,
        nsString(self.trackerState->status)];
    self.eventStatus.stringValue = [NSString stringWithFormat:@"%@  •  SENT %llu  DROP %llu  LATE %llu  CLK %llu",
        nsString(self.trackerState->lastEvent),
        self.trackerState->sentEventCount,
        self.trackerState->droppedEventCount,
        self.trackerState->audioLateEventCount,
        self.trackerState->audioClockFaultCount];
    [self refreshStatusMetadata];
    [self.gridView setNeedsDisplay:YES];
    [self.geometryView setNeedsDisplay:YES];
    [self.envelopeView setNeedsDisplay:YES];
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

- (void)showGeometryWindow:(id)sender
{
    if (self.trackerCallbacks && self.trackerCallbacks->showGeometryPage) {
        self.trackerCallbacks->showGeometryPage();
        return;
    }
    [self.geometryWindowController showWindow:sender];
    [self.geometryView setNeedsDisplay:YES];
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

- (NSView*)warpPageView
{
    (void)self.view;
    [self.warpWindowController reloadModel];
    return self.warpWindowController.window.contentView;
}

- (NSView*)consolePageView
{
    (void)self.view;
    return self.consoleOutputPanel;
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
    if (self.trackerState) {
        self.pageButton.title = gridPageTitle(
            self.trackerState->session.selectedPage);
    }
    [self.gridView refreshAccessibilityValue];
    [self.gridView setNeedsDisplay:YES];
    [self.geometryView setNeedsDisplay:YES];
    [self.envelopeView setNeedsDisplay:YES];
    [self.gridView scrollSelectionToVisible];
}

- (void)moduleTogglePlayback
{
    if (self.trackerState && self.trackerState->playing
        && !self.trackerState->paused) {
        if (self.trackerCallbacks && self.trackerCallbacks->pausePlayback)
            self.trackerCallbacks->pausePlayback();
    } else if (self.trackerCallbacks
        && self.trackerCallbacks->togglePlayback) {
        self.trackerCallbacks->togglePlayback();
    }
}

- (void)assignTrackInstrument:(uint32_t)nodeId
{
    auto* state = self.trackerState;
    if (!state || state->session.pattern.tracks.empty()
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

- (void)resetTrackerZoom
{
    [self setTrackerMagnification:1.0];
}

- (void)moduleFocusConsole { [self focusConsole]; }

- (void)cycleGridPage:(id)sender
{
    (void)sender;
    if (!self.trackerState) return;
    if (self.trackerState->mixerPageVisible) {
        [self showTrackerPage:nil];
        return;
    }
    auto& session = self.trackerState->session;
    session.selectedPage = (session.selectedPage + 1u) % 3u;
    session.selectedField = 0u;
    self.pageButton.title = gridPageTitle(session.selectedPage);
    [self.gridView refreshAccessibilityValue];
    [self.gridView setNeedsDisplay:YES];
    [self.envelopeView setNeedsDisplay:YES];
    [self.view.window makeFirstResponder:self.gridView];
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
    if (self.trackerCallbacks && self.trackerCallbacks->executeCommand)
        self.trackerCallbacks->executeCommand("track add");
}

- (void)trackRemovePressed:(id)sender
{
    (void)sender;
    auto* state = self.trackerState;
    if (!state || state->session.pattern.tracks.empty()) return;
    const auto lane = std::min(state->session.selectedTrack,
        state->session.pattern.tracks.size() - 1u);
    if (self.trackerCallbacks && self.trackerCallbacks->executeCommand)
        self.trackerCallbacks->executeCommand(
            "track remove " + std::to_string(lane + 1u));
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

- (void)pausePressed:(id)sender
{
    (void)sender;
    if (self.trackerCallbacks && self.trackerCallbacks->pausePlayback)
        self.trackerCallbacks->pausePlayback();
}

- (void)stopPressed:(id)sender
{
    (void)sender;
    if (self.trackerCallbacks && self.trackerCallbacks->stopPlayback)
        self.trackerCallbacks->stopPlayback();
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
    (void)sender;
    if (!self.trackerState) return;
    self.trackerState->session.transport.bpm = std::clamp(
        self.bpmField.doubleValue, 20.0, 400.0);
    self.trackerState->session.transport.swing = std::clamp(
        self.swingField.doubleValue / 100.0, 0.5, 0.75);
    auto& transport = self.trackerState->session.transport;
    const uint32_t start = static_cast<uint32_t>(std::clamp<NSInteger>(
        self.loopStartField.integerValue, 1, 256) - 1);
    const uint32_t end = static_cast<uint32_t>(std::clamp<NSInteger>(
        self.loopEndField.integerValue, 1, 256));
    transport.loopStartRow = std::min(start, end - 1u);
    transport.loopEndRow = std::max(end, transport.loopStartRow + 1u);
    if (self.trackerCallbacks && self.trackerCallbacks->transportChanged)
        self.trackerCallbacks->transportChanged();
    [self reloadModel];
}

- (void)gateFieldChanged:(id)sender
{
    (void)sender;
    if (!self.trackerState) return;
    self.trackerState->session.gateMilliseconds = std::clamp(
        self.gateField.doubleValue, 1.0, 5000.0);
    if (self.trackerCallbacks && self.trackerCallbacks->outputChanged)
        self.trackerCallbacks->outputChanged();
    [self reloadModel];
}

- (void)consoleSubmitted:(id)sender
{
    (void)sender;
    NSString* input = [self.consoleInput.stringValue
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
    if (self.trackerCallbacks && self.trackerCallbacks->executeCommand)
        self.trackerCallbacks->executeCommand(input.UTF8String);
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
    if (control != self.consoleInput) return NO;
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
        return YES;
    }
    if (selector == @selector(insertTab:)) {
        constexpr const char* commands[] {
            "help", "aliases", "alias", "kit", "play", "stop", "panic",
            "demo", "bpm", "swing", "gate", "select", "hit", "rest",
            "repeat", "kill", "note", "vel", "velseq", "vol", "mask",
            "len", "stride", "dir", "mute", "unmute", "solo", "name",
            "eu", "euclid", "rotate", "fill", "reverse", "actions",
            "randomize", "random", "rand",
            "fx", "fxvalue", "warps", "warp", "out", "route",
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
        return YES;
    }
    if (selector == @selector(cancelOperation:)) {
        [self.view.window makeFirstResponder:self.gridView];
        return YES;
    }
    return NO;
}

@end
