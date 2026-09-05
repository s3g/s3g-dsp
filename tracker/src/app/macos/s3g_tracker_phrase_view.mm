#import "s3g_tracker_phrase_view.h"
#import "s3g_tracker_controls.h"
#import "s3g_tracker_grid_selection.h"

#include "s3g_gui_layout.h"

#include "s3g/tracker/phrase_library.h"
#include "s3g/tracker/fx_catalog.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <variant>
#include <vector>

namespace {

using s3g::tracker::FxActionCellState;
using s3g::tracker::FxValueCell;
using s3g::tracker::FxValueCellState;
using s3g::tracker::GateCell;
using s3g::tracker::GateVoice;
using s3g::tracker::GateVoiceMode;
using s3g::tracker::NoteCellState;
using s3g::tracker::PhraseDefinition;
using s3g::tracker::PhrasePlacementMode;
using s3g::tracker::PitchPreviewEvent;
using s3g::tracker::ValueCellState;
using s3g::tracker::app::TrackerViewState;
using s3g::tracker::app::WorkspaceCallbacks;

constexpr std::array<CGFloat, 9u> kPhraseGridColumns {{
    0.0, 52.0, 222.0, 302.0, 402.0, 478.0, 578.0, 654.0, 800.0,
}};

NSString* phraseString(const std::string& text)
{
    NSString* value = [NSString stringWithUTF8String:text.c_str()];
    return value ? value : @"";
}

const s3g::tracker::BurstLibrary* phraseBurstLibrary(
    const TrackerViewState* state, s3g::tracker::AssetBankId bankId)
{
    if (!state) return nullptr;
    if (bankId == state->activeBurstBankId)
        return &state->session.burstLibrary;
    const auto* bank = s3g::tracker::findBurstBank(state->burstBanks, bankId);
    return bank ? &bank->library : nullptr;
}

NSString* phraseNoteText(const s3g::tracker::NoteCell& cell)
{
    if (cell.state == NoteCellState::Rest) return @"---";
    if (cell.state == NoteCellState::RetriggerPrevious) return @"RPT";
    if (cell.state == NoteCellState::Hold) return @"HLD";
    if (cell.state == NoteCellState::Kill) return @"KIL";
    if (cell.state == NoteCellState::Burst)
        return [NSString stringWithFormat:@"%s:B%02u",
            s3g::tracker::assetBankToken(cell.burstBankId).c_str(),
            cell.note + 1u];
    NSMutableArray<NSString*>* notes = [NSMutableArray array];
    for (std::size_t voice = 0u; voice < cell.noteVoiceCount(); ++voice)
        [notes addObject:[NSString stringWithFormat:@"%u",
            static_cast<unsigned int>(cell.noteVoice(voice))]];
    return [notes componentsJoinedByString:@"+"];
}

NSString* phraseValueText(const s3g::tracker::ValueCell& cell)
{
    if (cell.state == ValueCellState::Default) return @"DEF";
    if (cell.state == ValueCellState::Previous) return @"PRV";
    NSMutableArray<NSString*>* values = [NSMutableArray array];
    for (std::size_t voice = 0u; voice < cell.valueVoiceCount(); ++voice)
        [values addObject:[NSString stringWithFormat:@"%.3f",
            static_cast<double>(std::clamp(cell.valueVoice(voice),
                0.0f, 1.0f))]];
    return [values componentsJoinedByString:@"+"];
}

NSString* phraseFxText(const s3g::tracker::FxActionCell& cell)
{
    if (cell.state == FxActionCellState::Empty) return @"---";
    if (cell.state == FxActionCellState::Previous) return @"PRV";
    if (cell.state == FxActionCellState::MidiControlChange)
        return [NSString stringWithFormat:@"CC%u", cell.midiController];
    if (cell.state == FxActionCellState::Sequencer) {
        const auto* action = s3g::tracker::findSequencerAction(
            cell.sequencerAction);
        return action ? phraseString(std::string(action->mnemonic)) : @"???";
    }
    return @"---";
}

NSString* phraseFxValueText(const s3g::tracker::FxValueCell& cell)
{
    if (cell.state == s3g::tracker::FxValueCellState::Previous) return @"PRV";
    NSMutableArray<NSString*>* values = [NSMutableArray array];
    for (std::size_t voice = 0u; voice < cell.valueVoiceCount(); ++voice)
        [values addObject:[NSString stringWithFormat:@"%.3f",
            static_cast<double>(std::clamp(cell.valueVoice(voice),
                0.0f, 1.0f))]];
    return [values componentsJoinedByString:@"+"];
}

NSString* phraseGateText(const GateCell& cell)
{
    if (cell.voiceCount == 0u) return @"DEF";
    NSMutableArray<NSString*>* values = [NSMutableArray array];
    for (std::size_t voice = 0u; voice < cell.gateVoiceCount(); ++voice) {
        const auto gate = cell.gateVoice(voice);
        if (gate.mode == GateVoiceMode::Default) [values addObject:@"DEF"];
        else if (gate.mode == GateVoiceMode::Tie) [values addObject:@"TIE"];
        else [values addObject:[NSString stringWithFormat:@"%.3g",
            static_cast<double>(gate.rows)]];
    }
    return [values componentsJoinedByString:@"+"];
}

using PhraseGridCell = std::variant<s3g::tracker::NoteCell,
    s3g::tracker::ValueCell, s3g::tracker::FxActionCell,
    s3g::tracker::FxValueCell, GateCell>;

uint8_t phraseGridFieldType(std::size_t field) noexcept
{
    if (field == 0u) return 0u;
    if (field == 1u) return 1u;
    if (field == 6u) return 4u;
    return (field == 2u || field == 4u) ? 2u : 3u;
}

PhraseGridCell phraseGridCellAt(const PhraseDefinition& phrase,
    std::size_t field, std::size_t row)
{
    if (field == 0u)
        return row < phrase.notes.size() ? phrase.notes[row]
            : s3g::tracker::NoteCell::rest();
    if (field == 1u)
        return row < phrase.velocities.size() ? phrase.velocities[row]
            : s3g::tracker::ValueCell::defaultValue();
    if (field == 6u)
        return row < phrase.gates.size() ? phrase.gates[row]
            : GateCell::defaultValue();
    const auto pair = (field - 2u) / 2u;
    if ((field % 2u) == 0u)
        return row < phrase.fxPairs[pair].actions.size()
            ? phrase.fxPairs[pair].actions[row]
            : s3g::tracker::FxActionCell::empty();
    return row < phrase.fxPairs[pair].values.size()
        ? phrase.fxPairs[pair].values[row] : FxValueCell::previous();
}

void writePhraseGridCell(PhraseDefinition& phrase, std::size_t field,
    std::size_t row, const PhraseGridCell& cell)
{
    if (field == 0u) {
        if (phrase.notes.size() <= row)
            phrase.notes.resize(row + 1u, s3g::tracker::NoteCell::rest());
        phrase.notes[row] = std::get<s3g::tracker::NoteCell>(cell);
        return;
    }
    if (field == 1u) {
        if (phrase.velocities.size() <= row)
            phrase.velocities.resize(row + 1u,
                s3g::tracker::ValueCell::defaultValue());
        phrase.velocities[row] = std::get<s3g::tracker::ValueCell>(cell);
        return;
    }
    if (field == 6u) {
        if (phrase.gates.size() <= row)
            phrase.gates.resize(row + 1u, GateCell::defaultValue());
        phrase.gates[row] = std::get<GateCell>(cell);
        return;
    }
    auto& pair = phrase.fxPairs[(field - 2u) / 2u];
    if ((field % 2u) == 0u) {
        if (pair.actions.size() <= row)
            pair.actions.resize(row + 1u,
                s3g::tracker::FxActionCell::empty());
        pair.actions[row] = std::get<s3g::tracker::FxActionCell>(cell);
    } else {
        if (pair.values.size() <= row)
            pair.values.resize(row + 1u, FxValueCell::previous());
        pair.values[row] = std::get<FxValueCell>(cell);
    }
}

PhraseGridCell blankPhraseGridCell(std::size_t field)
{
    if (field == 0u) return s3g::tracker::NoteCell::rest();
    if (field == 1u) return s3g::tracker::ValueCell::defaultValue();
    if (field == 6u) return GateCell::defaultValue();
    if (field == 2u || field == 4u)
        return s3g::tracker::FxActionCell::empty();
    return FxValueCell::previous();
}

NSString* phraseGridCellText(const PhraseDefinition& phrase,
    std::size_t field, std::size_t row)
{
    if (field == 0u) return phraseNoteText(
        std::get<s3g::tracker::NoteCell>(phraseGridCellAt(phrase, field, row)));
    if (field == 1u) return phraseValueText(
        std::get<s3g::tracker::ValueCell>(phraseGridCellAt(phrase, field, row)));
    if (field == 6u) return phraseGateText(
        std::get<GateCell>(phraseGridCellAt(phrase, field, row)));
    if (field == 2u || field == 4u) return phraseFxText(
        std::get<s3g::tracker::FxActionCell>(
            phraseGridCellAt(phrase, field, row)));
    return phraseFxValueText(std::get<FxValueCell>(
        phraseGridCellAt(phrase, field, row)));
}

bool applyPhraseCellText(NSString* source, PhraseDefinition& phrase,
    const TrackerViewState& state,
    std::size_t row, std::size_t field)
{
    if (row >= phrase.length || field >= 7u) return false;
    NSString* token = source.uppercaseString;
    if (field == 0u) {
        if ([token isEqualToString:@"---"] || token.length == 0u) {
            writePhraseGridCell(phrase, field, row,
                s3g::tracker::NoteCell::rest());
            return true;
        }
        if ([token isEqualToString:@"RPT"]) {
            writePhraseGridCell(phrase, field, row,
                s3g::tracker::NoteCell::retriggerPrevious());
            return true;
        }
        if ([token isEqualToString:@"HLD"]) {
            writePhraseGridCell(phrase, field, row,
                s3g::tracker::NoteCell::hold());
            return true;
        }
        if ([token isEqualToString:@"KIL"]) {
            writePhraseGridCell(phrase, field, row,
                s3g::tracker::NoteCell::kill());
            return true;
        }
        std::size_t burstSlot = 0u;
        auto burstBankId = state.activeBurstBankId;
        const bool qualifiedBurst = s3g::tracker::parseQualifiedBurstToken(
            token.UTF8String, burstBankId, burstSlot);
        if (qualifiedBurst || s3g::tracker::parseBurstSlot(
                token.UTF8String, burstSlot)) {
            const auto* library = phraseBurstLibrary(&state, burstBankId);
            if (!library || burstSlot >= library->bursts.size()
                || library->bursts[burstSlot].empty()) return false;
            writePhraseGridCell(phrase, field, row,
                s3g::tracker::NoteCell::withBurst(
                    static_cast<uint8_t>(burstSlot), burstBankId));
            return true;
        }
        NSArray<NSString*>* parts = [token componentsSeparatedByString:@"+"];
        std::array<uint8_t, s3g::tracker::kMaximumNoteVoices> notes {};
        std::size_t count = 0u;
        for (NSString* part in parts) {
            uint8_t note = 0u;
            if (count >= notes.size()
                || !s3g::tracker::parseMidiNote(part.UTF8String, note))
                return false;
            notes[count++] = note;
        }
        std::sort(notes.begin(), notes.begin() + count);
        if (count == 0u || std::adjacent_find(notes.begin(),
                notes.begin() + count) != notes.begin() + count) return false;
        writePhraseGridCell(phrase, field, row, count == 1u
            ? PhraseGridCell(s3g::tracker::NoteCell::withNote(notes[0u]))
            : PhraseGridCell(s3g::tracker::NoteCell::withNotes(notes, count)));
        return true;
    }
    if (field == 1u) {
        if ([token isEqualToString:@"DEF"] || token.length == 0u) {
            writePhraseGridCell(phrase, field, row,
                s3g::tracker::ValueCell::defaultValue());
            return true;
        }
        if ([token isEqualToString:@"PRV"]) {
            writePhraseGridCell(phrase, field, row,
                s3g::tracker::ValueCell::previous());
            return true;
        }
        NSArray<NSString*>* parts = [token componentsSeparatedByString:@"+"];
        if (parts.count == 0u
            || parts.count > s3g::tracker::kMaximumNoteVoices) return false;
        std::array<float, s3g::tracker::kMaximumNoteVoices> values {};
        for (NSUInteger index = 0u; index < parts.count; ++index) {
            NSString* part = parts[index];
            NSScanner* scanner = [NSScanner scannerWithString:part];
            double value = 0.0;
            if (![scanner scanDouble:&value] || !scanner.isAtEnd
                || !std::isfinite(value)) return false;
            // Phrase VOL is authored and displayed in normalized units. Keep
            // MIDI integers above one as a compatibility input convenience.
            if (value > 1.0) value /= 127.0;
            if (value < 0.0 || value > 1.0) return false;
            values[index] = static_cast<float>(value);
        }
        writePhraseGridCell(phrase, field, row,
            s3g::tracker::ValueCell::withValues(values, parts.count));
        return true;
    }
    if (field == 6u) {
        if ([token isEqualToString:@"DEF"] || token.length == 0u) {
            writePhraseGridCell(phrase, field, row, GateCell::defaultValue());
            return true;
        }
        NSArray<NSString*>* parts = [token componentsSeparatedByString:@"+"];
        if (parts.count == 0u
            || parts.count > s3g::tracker::kMaximumNoteVoices) return false;
        std::array<GateVoice, s3g::tracker::kMaximumNoteVoices> gates {};
        for (NSUInteger index = 0u; index < parts.count; ++index) {
            NSString* part = parts[index];
            if ([part isEqualToString:@"DEF"])
                gates[index] = { GateVoiceMode::Default, 1.0f };
            else if ([part isEqualToString:@"TIE"]
                || [part isEqualToString:@"T"])
                gates[index] = { GateVoiceMode::Tie, 1.0f };
            else {
                NSScanner* scanner = [NSScanner scannerWithString:part];
                double rows = 0.0;
                if (![scanner scanDouble:&rows] || !scanner.isAtEnd
                    || !std::isfinite(rows) || rows < 0.01 || rows > 64.0)
                    return false;
                gates[index] = { GateVoiceMode::Rows,
                    static_cast<float>(rows) };
            }
        }
        writePhraseGridCell(phrase, field, row,
            GateCell::withVoices(gates, parts.count));
        return true;
    }
    if (field == 2u || field == 4u) {
        s3g::tracker::FxActionCell cell;
        if ([token isEqualToString:@"---"] || token.length == 0u)
            cell = s3g::tracker::FxActionCell::empty();
        else if ([token isEqualToString:@"PRV"])
            cell = s3g::tracker::FxActionCell::previous();
        else {
            uint8_t controller = 0u;
            if (s3g::tracker::parseMidiControlChange(token.UTF8String,
                    controller))
                cell = s3g::tracker::FxActionCell::midiControlChange(
                    controller);
            else if (const auto* action = s3g::tracker::findSequencerAction(
                         token.UTF8String))
                cell = s3g::tracker::FxActionCell::sequencer(action->action);
            else return false;
        }
        writePhraseGridCell(phrase, field, row, cell);
        return true;
    }
    if ([token isEqualToString:@"PRV"] || token.length == 0u) {
        writePhraseGridCell(phrase, field, row, FxValueCell::previous());
        return true;
    }
    NSArray<NSString*>* parts = [token componentsSeparatedByString:@"+"];
    if (parts.count == 0u
        || parts.count > s3g::tracker::kMaximumNoteVoices) return false;
    std::array<float, s3g::tracker::kMaximumNoteVoices> values {};
    for (NSUInteger index = 0u; index < parts.count; ++index) {
        NSScanner* scanner = [NSScanner scannerWithString:parts[index]];
        double value = 0.0;
        if (![scanner scanDouble:&value] || !scanner.isAtEnd
            || !std::isfinite(value)) return false;
        if (value > 1.0) value /= 127.0;
        if (value < 0.0 || value > 1.0) return false;
        values[index] = static_cast<float>(value);
    }
    writePhraseGridCell(phrase, field, row,
        FxValueCell::withValues(values, parts.count));
    return true;
}

} // namespace

@class S3GTrackerPhraseView;

@interface S3GTrackerPhraseGridView : NSView <NSTextFieldDelegate> {
@private
    s3g::tracker::app::GridSelection _gridSelection;
    std::vector<PhraseGridCell> _copiedGridCells;
    std::vector<uint8_t> _copiedColumnTypes;
}
@property(nonatomic, weak) S3GTrackerPhraseView* owner;
@property(nonatomic) std::size_t selectedRow;
@property(nonatomic) std::size_t selectedField;
@property(nonatomic, strong) NSTextField* editor;
@property(nonatomic) NSRect editorCellRect;
@property(nonatomic) BOOL selectingGridCells;
@property(nonatomic, copy) NSString* copiedClipboardText;
@property(nonatomic) NSInteger copiedPasteboardChangeCount;
@property(nonatomic) std::size_t copiedRowCount;
@property(nonatomic) std::size_t copiedFieldCount;
- (void)reloadModel;
- (BOOL)handleGridKeyEvent:(NSEvent*)event;
- (void)beginEditingWithInitialText:(NSString*)initialText;
- (NSMenu*)phraseSequenceActionMenuForField:(std::size_t)field
    row:(std::size_t)row;
- (NSMenu*)phraseSequenceConditionMenuForField:(std::size_t)field
    row:(std::size_t)row;
- (void)phraseCopy:(id)sender;
- (void)phraseCut:(id)sender;
- (void)phrasePaste:(id)sender;
- (void)phraseSelectAll:(id)sender;
- (void)resetPhraseSelectionAtCursor;
- (s3g::tracker::app::GridSelectionRange)effectivePhraseSelection;
@end

@interface S3GTrackerPhraseRootView : S3GTrackerFocusReleaseView
    <S3GTrackerPhraseKeyHandling>
@property(nonatomic, weak) S3GTrackerPhraseGridView* phraseGrid;
@property(nonatomic, weak) S3GTrackerPhraseView* layoutOwner;
@end

@interface S3GTrackerPhraseView () <NSTextFieldDelegate>
@property(nonatomic, assign) TrackerViewState* trackerState;
@property(nonatomic, assign) WorkspaceCallbacks* trackerCallbacks;
@property(nonatomic, strong) S3GTrackerPopupButton* libraryPopup;
@property(nonatomic, strong) S3GTrackerPopupButton* bankPopup;
@property(nonatomic, strong) NSTextField* nameField;
@property(nonatomic, strong) S3GTrackerPopupButton* lengthPopup;
@property(nonatomic, strong) S3GTrackerPopupButton* previewChannelPopup;
@property(nonatomic, strong) S3GTrackerPopupButton* modePopup;
@property(nonatomic, strong) S3GTrackerPhraseGridView* grid;
@property(nonatomic, strong) NSScrollView* gridScroll;
@property(nonatomic, strong) S3GTrackerToolboxView* editorPanel;
@property(nonatomic, strong) S3GTrackerToolboxView* libraryPanel;
@property(nonatomic, strong) S3GTrackerToolboxView* auditionPanel;
@property(nonatomic, strong) S3GTrackerToolboxView* placementPanel;
@property(nonatomic, strong) NSTextField* phraseLabel;
@property(nonatomic, strong) NSTextField* bankLabel;
@property(nonatomic, strong) NSTextField* nameLabel;
@property(nonatomic, strong) NSTextField* lengthLabel;
@property(nonatomic, strong) NSTextField* previewChannelLabel;
@property(nonatomic, strong) NSTextField* placementModeLabel;
@property(nonatomic, strong) NSTextField* placementTargetLabel;
@property(nonatomic, strong) NSTextField* placementTargetValue;
@property(nonatomic, strong) NSTextField* statusLabel;
@property(nonatomic, strong) NSButton* saveButton;
@property(nonatomic, strong) NSButton* duplicateButton;
@property(nonatomic, strong) NSButton* deleteButton;
@property(nonatomic, strong) NSButton* importPackButton;
@property(nonatomic, strong) NSButton* exportPackButton;
@property(nonatomic, strong) NSButton* exportAllButton;
@property(nonatomic, strong) NSButton* clearBankButton;
@property(nonatomic, strong) NSButton* deleteBankButton;
@property(nonatomic, strong) NSButton* projectCopyButton;
@property(nonatomic, strong) NSButton* previewButton;
@property(nonatomic, strong) NSButton* placeButton;
@property(nonatomic, strong) NSTimer* previewTimer;
@property(nonatomic) NSInteger previewPlayheadRow;
@property(nonatomic) NSInteger previewLastRow;
@property(nonatomic) std::size_t previewPhraseSlot;
- (PhraseDefinition*)selectedPhrase;
- (void)phraseEdited;
- (void)stopPhrasePreview;
- (void)layoutPhraseInterface;
@end

@implementation S3GTrackerPhraseRootView

- (BOOL)isFlipped { return YES; }

- (void)layout
{
    [super layout];
    [self.layoutOwner layoutPhraseInterface];
}

- (BOOL)s3gHandlePhraseKeyEquivalent:(NSEvent*)event
{
    NSResponder* responder = self.window.firstResponder;
    // REAPER may retain its outer CLAP bridge as first responder even after a
    // custom grid view accepts the mouse click. Text fields and open menus get
    // priority; otherwise the visible Phrase grid owns its tracker-entry keys.
    if ([responder isKindOfClass:NSTextView.class]
        || [NSStringFromClass(responder.class) containsString:@"MenuOverlay"])
        return NO;
    return [self.phraseGrid handleGridKeyEvent:event];
}

- (BOOL)performKeyEquivalent:(NSEvent*)event
{
    if ([self s3gHandlePhraseKeyEquivalent:event]) return YES;
    return [super performKeyEquivalent:event];
}

- (void)keyDown:(NSEvent*)event
{
    if ([self s3gHandlePhraseKeyEquivalent:event]) return;
    [super keyDown:event];
}

@end

@implementation S3GTrackerPhraseGridView

- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }

- (PhraseDefinition*)phrase { return [self.owner selectedPhrase]; }

- (void)reloadModel
{
    PhraseDefinition* phrase = [self phrase];
    const std::size_t rows = phrase ? phrase->length : 16u;
    const CGFloat visibleWidth = self.enclosingScrollView
        ? NSWidth(self.enclosingScrollView.contentView.bounds) : 0.0;
    self.frame = NSMakeRect(0.0, 0.0,
        std::max<CGFloat>(800.0, visibleWidth), 30.0 + rows * 22.0);
    self.selectedRow = std::min(self.selectedRow, rows - 1u);
    if (_gridSelection.active) {
        _gridSelection.focusRow = std::min(_gridSelection.focusRow, rows - 1u);
        _gridSelection.anchorRow = std::min(_gridSelection.anchorRow, rows - 1u);
        _gridSelection.focusField = std::min<std::size_t>(
            _gridSelection.focusField, 6u);
        _gridSelection.anchorField = std::min<std::size_t>(
            _gridSelection.anchorField, 6u);
        _gridSelection.active = !_gridSelection.isSingleCell();
    }
    [self setNeedsDisplay:YES];
}

- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    [S3GTrackerThemeColor(S3GTrackerThemeRole::Workspace) setFill];
    NSRectFill(self.bounds);
    [S3GTrackerThemeColor(S3GTrackerThemeRole::Panel) setFill];
    NSRectFill(NSMakeRect(0.0, 0.0, NSWidth(self.bounds), 30.0));
    [S3GTrackerThemeColor(S3GTrackerThemeRole::Focus) setFill];
    NSRectFill(NSMakeRect(0.0, 0.0, NSWidth(self.bounds), 2.0));
    NSArray<NSString*>* headers = @[@"ROW", @"NOTE", @"VOL", @"SEQ1",
        @"V1", @"SEQ2", @"V2", @"GATE"];
    NSArray<NSColor*>* headerColors = @[
        S3GTrackerThemeColor(S3GTrackerThemeRole::TextMuted),
        S3GTrackerThemeColor(S3GTrackerThemeRole::Note),
        S3GTrackerThemeColor(S3GTrackerThemeRole::Value),
        S3GTrackerThemeColor(S3GTrackerThemeRole::Warning),
        S3GTrackerThemeColor(S3GTrackerThemeRole::Value),
        S3GTrackerThemeColor(S3GTrackerThemeRole::Warning),
        S3GTrackerThemeColor(S3GTrackerThemeRole::Value),
        S3GTrackerThemeColor(S3GTrackerThemeRole::Value),
    ];
    for (NSUInteger field = 0u; field < headers.count; ++field) {
        NSMutableParagraphStyle* paragraph = [[NSMutableParagraphStyle alloc]
            init];
        paragraph.alignment = field == 0u
            ? NSTextAlignmentRight : NSTextAlignmentCenter;
        NSDictionary* header = @{
            NSFontAttributeName: S3GTrackerFont(8.0, NSFontWeightSemibold),
            NSForegroundColorAttributeName: headerColors[field],
            NSParagraphStyleAttributeName: paragraph,
        };
        [headers[field] drawInRect:NSMakeRect(
            kPhraseGridColumns[field] + 6.0, 7.0,
            kPhraseGridColumns[field + 1u]
                - kPhraseGridColumns[field] - 12.0,
            15.0) withAttributes:header];
        if (field > 0u) {
            [S3GTrackerThemeColor(S3GTrackerThemeRole::Grid, 0.70) setFill];
            NSRectFill(NSMakeRect(kPhraseGridColumns[field], 2.0, 1.0,
                28.0));
        }
    }
    PhraseDefinition* phrase = [self phrase];
    if (!phrase) return;
    NSColor* grid = S3GTrackerThemeColor(S3GTrackerThemeRole::Grid, 0.70);
    NSColor* dim = S3GTrackerThemeColor(S3GTrackerThemeRole::TextFaint);
    NSColor* value = S3GTrackerThemeColor(S3GTrackerThemeRole::Value);
    NSColor* warning = S3GTrackerThemeColor(S3GTrackerThemeRole::Warning);
    for (std::size_t row = 0u; row < phrase->length; ++row) {
        const CGFloat y = 30.0 + static_cast<CGFloat>(row) * 22.0;
        NSColor* rowColor = (row % 4u) == 0u
            ? S3GTrackerThemeColor(S3GTrackerThemeRole::Raised)
            : S3GTrackerThemeColor(S3GTrackerThemeRole::Panel);
        [rowColor setFill];
        NSRectFill(NSMakeRect(0.0, y, NSWidth(self.bounds), 22.0));
        NSColor* rowNumberColor = (row % 4u) == 0u
            ? S3GTrackerThemeColor(S3GTrackerThemeRole::Control)
            : S3GTrackerThemeColor(S3GTrackerThemeRole::Raised);
        [rowNumberColor setFill];
        NSRectFill(NSMakeRect(0.0, y, kPhraseGridColumns[1u] - 1.0,
            22.0));
        if (row == self.selectedRow) {
            [S3GTrackerThemeColor(S3GTrackerThemeRole::Focus, 0.11) setFill];
            NSRectFill(NSMakeRect(0.0, y, NSWidth(self.bounds), 22.0));
        } else if ((row % 4u) == 0u) {
            [S3GTrackerThemeColor(S3GTrackerThemeRole::Border, 0.72) setFill];
            NSRectFill(NSMakeRect(0.0, y, NSWidth(self.bounds), 1.0));
        }
        if (static_cast<NSInteger>(row) == self.owner.previewPlayheadRow) {
            for (std::size_t field = 0u; field < 7u; ++field) {
                const NSRect playheadCell = NSMakeRect(
                    kPhraseGridColumns[field + 1u], y,
                    kPhraseGridColumns[field + 2u]
                        - kPhraseGridColumns[field + 1u], 22.0);
                [S3GTrackerThemeColor(
                    S3GTrackerThemeRole::GridPlayback) setFill];
                NSRectFill(NSInsetRect(playheadCell, 1.0, 1.0));
                const bool cursor = row == self.selectedRow
                    && field == self.selectedField;
                if (!cursor) {
                    [S3GTrackerThemeColor(
                        S3GTrackerThemeRole::GridPlaybackAccent) setFill];
                    NSRectFill(NSMakeRect(NSMinX(playheadCell) + 1.0,
                        y + 3.0, 2.0, 16.0));
                }
            }
        }
        if (_gridSelection.active) {
            const auto selection = _gridSelection.range();
            if (row >= selection.firstRow && row <= selection.lastRow) {
                for (std::size_t field = selection.firstField;
                     field <= std::min<std::size_t>(selection.lastField, 6u);
                     ++field) {
                    const NSRect selectedRangeCell = NSMakeRect(
                        kPhraseGridColumns[field + 1u], y,
                        kPhraseGridColumns[field + 2u]
                            - kPhraseGridColumns[field + 1u], 22.0);
                    [S3GTrackerThemeColor(
                        S3GTrackerThemeRole::GridSelection) setFill];
                    NSRectFill(NSInsetRect(selectedRangeCell, 1.0, 1.0));
                }
            }
        }
        if (row == self.selectedRow) {
            const NSRect selectedCell = NSMakeRect(
                kPhraseGridColumns[self.selectedField + 1u], y,
                kPhraseGridColumns[self.selectedField + 2u]
                    - kPhraseGridColumns[self.selectedField + 1u], 22.0);
            [S3GTrackerThemeColor(S3GTrackerThemeRole::GridCursor) setFill];
            NSRectFill(NSInsetRect(selectedCell, 1.0, 1.0));
        }
        NSArray<NSString*>* values = @[
            [NSString stringWithFormat:@"%02lu",
                static_cast<unsigned long>(row + 1u)],
            row < phrase->notes.size() ? phraseNoteText(phrase->notes[row]) : @"---",
            row < phrase->velocities.size()
                ? phraseValueText(phrase->velocities[row]) : @"DEF",
            row < phrase->fxPairs[0u].actions.size()
                ? phraseFxText(phrase->fxPairs[0u].actions[row]) : @"---",
            row < phrase->fxPairs[0u].values.size()
                ? phraseFxValueText(phrase->fxPairs[0u].values[row]) : @"PRV",
            row < phrase->fxPairs[1u].actions.size()
                ? phraseFxText(phrase->fxPairs[1u].actions[row]) : @"---",
            row < phrase->fxPairs[1u].values.size()
                ? phraseFxValueText(phrase->fxPairs[1u].values[row]) : @"PRV",
            row < phrase->gates.size()
                ? phraseGateText(phrase->gates[row]) : @"DEF",
        ];
        const bool noteActive = row < phrase->notes.size()
            && phrase->notes[row].state != NoteCellState::Rest;
        const bool volumeActive = row < phrase->velocities.size()
            && phrase->velocities[row].state == ValueCellState::Value;
        const bool seq1Active = row < phrase->fxPairs[0u].actions.size()
            && phrase->fxPairs[0u].actions[row].state
                != FxActionCellState::Empty;
        const bool seq2Active = row < phrase->fxPairs[1u].actions.size()
            && phrase->fxPairs[1u].actions[row].state
                != FxActionCellState::Empty;
        const bool seq1ValueActive = row < phrase->fxPairs[0u].values.size()
            && phrase->fxPairs[0u].values[row].state
                == s3g::tracker::FxValueCellState::Value;
        const bool seq2ValueActive = row < phrase->fxPairs[1u].values.size()
            && phrase->fxPairs[1u].values[row].state
                == s3g::tracker::FxValueCellState::Value;
        const bool gateActive = row < phrase->gates.size()
            && phrase->gates[row].voiceCount > 0u;
        NSArray<NSColor*>* colors = @[
            row == self.selectedRow
                ? S3GTrackerThemeColor(S3GTrackerThemeRole::Focus)
                : (row % 4u) == 0u
                    ? S3GTrackerThemeColor(S3GTrackerThemeRole::TextMuted)
                    : dim,
            noteActive
                ? S3GTrackerThemeColor(S3GTrackerThemeRole::TextPrimary)
                : dim,
            volumeActive ? value : dim,
            seq1Active ? warning : dim,
            seq1ValueActive ? value : dim,
            seq2Active ? warning : dim,
            seq2ValueActive ? value : dim,
            gateActive ? value : dim,
        ];
        for (NSUInteger field = 0u; field < values.count; ++field) {
            NSMutableParagraphStyle* paragraph = [[NSMutableParagraphStyle alloc]
                init];
            paragraph.alignment = field == 0u || field == 2u
                ? NSTextAlignmentRight : NSTextAlignmentCenter;
            const bool active = field == 1u ? noteActive
                : field == 2u ? volumeActive
                : field == 3u ? seq1Active
                : field == 4u ? seq1ValueActive
                : field == 5u ? seq2Active
                : field == 6u ? seq2ValueActive
                : field == 7u ? gateActive : false;
            NSDictionary* text = @{
                NSFontAttributeName: S3GTrackerFont(field == 0u ? 9.6
                        : field == 2u ? 9.2 : 10.0,
                    field == 0u && row == self.selectedRow
                        ? NSFontWeightSemibold
                        : field == 0u || active
                            ? NSFontWeightMedium : NSFontWeightRegular),
                NSForegroundColorAttributeName: colors[field],
                NSParagraphStyleAttributeName: paragraph,
            };
            [values[field] drawInRect:NSMakeRect(
                kPhraseGridColumns[field] + 6.0, y + 4.0,
                kPhraseGridColumns[field + 1u]
                    - kPhraseGridColumns[field] - 12.0, 15.0)
                withAttributes:text];
            if (field > 0u) {
                [grid setFill];
                NSRectFill(NSMakeRect(kPhraseGridColumns[field], y, 1.0,
                    22.0));
            }
        }
        [grid setFill];
        NSRectFill(NSMakeRect(0.0, y + 21.0, NSWidth(self.bounds), 1.0));
    }
}

- (void)mouseDown:(NSEvent*)event
{
    // Finish the previous cell before moving the selection; commitEditor:
    // writes to the current cursor address.
    if (self.editor) {
        [self commitEditor:nil];
        if (self.editor) return;
    }
    const NSPoint point = [self convertPoint:event.locationInWindow fromView:nil];
    PhraseDefinition* phrase = [self phrase];
    if (!phrase || point.y < 30.0 || point.x < kPhraseGridColumns[1u]
        || point.x >= kPhraseGridColumns.back()) return;
    const auto clickedRow = std::min<std::size_t>(phrase->length - 1u,
        static_cast<std::size_t>((point.y - 30.0) / 22.0));
    std::size_t clickedField = 0u;
    for (std::size_t field = 1u; field < 7u; ++field)
        if (point.x >= kPhraseGridColumns[field + 1u])
            clickedField = field;
    if ((event.modifierFlags & NSEventModifierFlagShift) != 0u
        && clickedField == self.selectedField) {
        const auto anchorRow = _gridSelection.anchorField == clickedField
            ? _gridSelection.anchorRow : self.selectedRow;
        _gridSelection.page = 0u;
        _gridSelection.anchorTrack = _gridSelection.focusTrack = 0u;
        _gridSelection.anchorField = _gridSelection.focusField = clickedField;
        _gridSelection.anchorRow = anchorRow;
        _gridSelection.focusRow = clickedRow;
        _gridSelection.active = !_gridSelection.isSingleCell();
        self.selectedRow = clickedRow;
        self.selectingGridCells = NO;
        [self.window makeFirstResponder:self];
        [self setNeedsDisplay:YES];
        return;
    }
    self.selectedRow = clickedRow;
    self.selectedField = clickedField;
    _gridSelection.page = 0u;
    _gridSelection.anchorTrack = _gridSelection.focusTrack = 0u;
    _gridSelection.anchorField = _gridSelection.focusField = clickedField;
    _gridSelection.anchorRow = _gridSelection.focusRow = clickedRow;
    _gridSelection.active = false;
    self.selectingGridCells = YES;
    [self.window makeFirstResponder:self];
    [self setNeedsDisplay:YES];
    if (event.clickCount >= 2u && self.selectedField < 7u)
        [self beginEditing];
}

- (void)mouseDragged:(NSEvent*)event
{
    PhraseDefinition* phrase = [self phrase];
    if (!phrase || !self.selectingGridCells || self.editor) return;
    const NSPoint point = [self convertPoint:event.locationInWindow
        fromView:nil];
    const CGFloat x = std::clamp(point.x,
        kPhraseGridColumns[1u], kPhraseGridColumns.back() - 1.0);
    const NSInteger rawRow = std::clamp<NSInteger>(static_cast<NSInteger>(
        (point.y - 30.0) / 22.0), 0,
        static_cast<NSInteger>(phrase->length - 1u));
    std::size_t field = 0u;
    for (std::size_t candidate = 1u; candidate < 7u; ++candidate)
        if (x >= kPhraseGridColumns[candidate + 1u]) field = candidate;
    _gridSelection.focusField = field;
    _gridSelection.focusRow = static_cast<std::size_t>(rawRow);
    _gridSelection.active = !_gridSelection.isSingleCell();
    self.selectedField = field;
    self.selectedRow = static_cast<std::size_t>(rawRow);
    [self setNeedsDisplay:YES];
}

- (void)mouseUp:(NSEvent*)event
{
    (void)event;
    self.selectingGridCells = NO;
}

- (NSMenu*)phraseSequenceActionMenuForField:(std::size_t)field
    row:(std::size_t)row
{
    PhraseDefinition* phrase = [self phrase];
    if (!phrase || row >= phrase->length || (field != 2u && field != 4u))
        return nil;
    const auto pairIndex = (field - 2u) / 2u;
    auto& pair = phrase->fxPairs[pairIndex];
    const auto current = row < pair.actions.size()
        ? pair.actions[row] : s3g::tracker::FxActionCell::empty();

    NSMenu* menu = [[NSMenu alloc] initWithTitle:@"SEQUENCING ACTION"];
    menu.autoenablesItems = NO;
    menu.font = S3GTrackerFont(9.5, NSFontWeightMedium);
    NSMenuItem* heading = [[NSMenuItem alloc]
        initWithTitle:@"SEQ ACTION  ·  NORMALIZED VALUE 0.000–1.000"
        action:nil keyEquivalent:@""];
    heading.enabled = NO;
    [menu addItem:heading];
    [menu addItem:NSMenuItem.separatorItem];

    const auto addUtility = [&](NSString* title, NSString* kind,
                                BOOL selected) {
        NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:title
            action:@selector(phraseSequenceActionSelected:)
            keyEquivalent:@""];
        item.target = self;
        item.representedObject = @{
            @"row": @(row), @"field": @(field), @"kind": kind,
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
            phraseString(std::string(action->mnemonic)),
            phraseString(std::string(action->displayName)).uppercaseString,
            phraseString(std::string(action->valueMeaning))];
        NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:title
            action:@selector(phraseSequenceActionSelected:)
            keyEquivalent:@""];
        item.target = self;
        item.representedObject = @{
            @"row": @(row), @"field": @(field), @"kind": @"action",
            @"action": @(index),
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
    ccMenu.autoenablesItems = NO;
    ccMenu.font = menu.font;
    for (NSUInteger group = 0u; group < 4u; ++group) {
        const NSUInteger first = group * 32u;
        NSMenuItem* groupItem = [[NSMenuItem alloc]
            initWithTitle:[NSString stringWithFormat:@"CC%03lu–CC%03lu",
                static_cast<unsigned long>(first),
                static_cast<unsigned long>(first + 31u)]
            action:nil keyEquivalent:@""];
        NSMenu* groupMenu = [[NSMenu alloc] initWithTitle:groupItem.title];
        groupMenu.autoenablesItems = NO;
        groupMenu.font = menu.font;
        for (NSUInteger controller = first;
             controller < first + 32u; ++controller) {
            NSMenuItem* item = [[NSMenuItem alloc]
                initWithTitle:[NSString stringWithFormat:@"CC%03lu",
                    static_cast<unsigned long>(controller)]
                action:@selector(phraseSequenceActionSelected:)
                keyEquivalent:@""];
            item.target = self;
            item.representedObject = @{
                @"row": @(row), @"field": @(field), @"kind": @"cc",
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
    return menu;
}

- (void)phraseSequenceActionSelected:(NSMenuItem*)sender
{
    PhraseDefinition* phrase = [self phrase];
    NSDictionary* value = sender.representedObject;
    if (!phrase || ![value isKindOfClass:NSDictionary.class]) return;
    const auto row = [value[@"row"] unsignedIntegerValue];
    const auto field = [value[@"field"] unsignedIntegerValue];
    NSString* kind = value[@"kind"];
    if (row >= phrase->length || (field != 2u && field != 4u)
        || ![kind isKindOfClass:NSString.class]) return;
    auto& pair = phrase->fxPairs[(field - 2u) / 2u];
    if (pair.actions.size() <= row)
        pair.actions.resize(row + 1u, s3g::tracker::FxActionCell::empty());
    if ([kind isEqualToString:@"clear"]) {
        pair.actions[row] = s3g::tracker::FxActionCell::empty();
    } else if ([kind isEqualToString:@"previous"]) {
        pair.actions[row] = s3g::tracker::FxActionCell::previous();
    } else if ([kind isEqualToString:@"action"]) {
        const auto index = [value[@"action"] unsignedIntegerValue];
        const auto* action = s3g::tracker::sequencerAction(index);
        if (!action) return;
        pair.actions[row] = s3g::tracker::FxActionCell::sequencer(
            action->action);
        if (pair.values.size() <= row)
            pair.values.resize(row + 1u, FxValueCell::previous());
        if (pair.values[row].state == FxValueCellState::Previous) {
            pair.values[row] = FxValueCell::withValue(
                action->action == s3g::tracker::SequencerAction::Condition
                    ? s3g::tracker::normalizedFromSequencerCondition(
                        s3g::tracker::SequencerCondition::FirstOf2)
                    : 0.5f);
        }
        pair.valueColumn.length = std::max(pair.valueColumn.length, row + 1u);
    } else if ([kind isEqualToString:@"cc"]) {
        const auto controller = [value[@"controller"] unsignedIntegerValue];
        if (controller > 127u) return;
        pair.actions[row] = s3g::tracker::FxActionCell::midiControlChange(
            static_cast<uint8_t>(controller));
        if (pair.values.size() <= row)
            pair.values.resize(row + 1u, FxValueCell::previous());
        if (pair.values[row].state == FxValueCellState::Previous)
            pair.values[row] = FxValueCell::withValue(0.5f);
        pair.valueColumn.length = std::max(pair.valueColumn.length, row + 1u);
    } else return;
    pair.actionColumn.length = std::max(pair.actionColumn.length, row + 1u);
    self.selectedRow = row;
    self.selectedField = field;
    [self resetPhraseSelectionAtCursor];
    [self.owner phraseEdited];
    [self reloadModel];
    [self.window makeFirstResponder:self];
}

- (NSMenu*)phraseSequenceConditionMenuForField:(std::size_t)field
    row:(std::size_t)row
{
    PhraseDefinition* phrase = [self phrase];
    if (!phrase || row >= phrase->length || (field != 3u && field != 5u))
        return nil;
    const auto pairIndex = (field - 3u) / 2u;
    const auto& pair = phrase->fxPairs[pairIndex];
    if (row >= pair.actions.size()
        || pair.actions[row].state != FxActionCellState::Sequencer
        || pair.actions[row].sequencerAction
            != s3g::tracker::SequencerAction::Condition) return nil;
    const auto current = row < pair.values.size()
            && pair.values[row].state == FxValueCellState::Value
        ? s3g::tracker::sequencerConditionFromNormalized(
            pair.values[row].normalized)
        : s3g::tracker::SequencerCondition::FirstOf2;
    NSMenu* menu = [[NSMenu alloc] initWithTitle:@"CONDITION"];
    menu.autoenablesItems = NO;
    menu.font = S3GTrackerFont(9.5, NSFontWeightMedium);
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
                s3g::tracker::SequencerCondition::SongFirstOf2))
            [menu addItem:NSMenuItem.separatorItem];
        NSMenuItem* item = [[NSMenuItem alloc]
            initWithTitle:[NSString stringWithFormat:@"%@   %@",
                phraseString(std::string(condition->token)),
                phraseString(std::string(condition->displayName))]
            action:@selector(phraseSequenceConditionSelected:)
            keyEquivalent:@""];
        item.target = self;
        item.representedObject = @{
            @"row": @(row), @"field": @(field), @"condition": @(index),
        };
        item.state = current == condition->condition
            ? NSControlStateValueOn : NSControlStateValueOff;
        [menu addItem:item];
    }
    return menu;
}

- (void)phraseSequenceConditionSelected:(NSMenuItem*)sender
{
    PhraseDefinition* phrase = [self phrase];
    NSDictionary* value = sender.representedObject;
    if (!phrase || ![value isKindOfClass:NSDictionary.class]) return;
    const auto row = [value[@"row"] unsignedIntegerValue];
    const auto field = [value[@"field"] unsignedIntegerValue];
    const auto index = [value[@"condition"] unsignedIntegerValue];
    const auto* condition = s3g::tracker::sequencerCondition(index);
    if (!condition || row >= phrase->length
        || (field != 3u && field != 5u)) return;
    auto& pair = phrase->fxPairs[(field - 3u) / 2u];
    if (pair.values.size() <= row)
        pair.values.resize(row + 1u, FxValueCell::previous());
    pair.values[row] = FxValueCell::withValue(
        s3g::tracker::normalizedFromSequencerCondition(
            condition->condition));
    pair.valueColumn.length = std::max(pair.valueColumn.length, row + 1u);
    self.selectedRow = row;
    self.selectedField = field;
    [self resetPhraseSelectionAtCursor];
    [self.owner phraseEdited];
    [self reloadModel];
    [self.window makeFirstResponder:self];
}

- (void)rightMouseDown:(NSEvent*)event
{
    if (self.editor) {
        [self commitEditor:nil];
        if (self.editor) return;
    }
    const NSPoint point = [self convertPoint:event.locationInWindow
        fromView:nil];
    PhraseDefinition* phrase = [self phrase];
    if (!phrase || point.y < 30.0 || point.x < kPhraseGridColumns[1u]
        || point.x >= kPhraseGridColumns.back()) {
        [super rightMouseDown:event];
        return;
    }
    const auto rawRow = static_cast<std::size_t>((point.y - 30.0) / 22.0);
    if (rawRow >= phrase->length) return;
    std::size_t field = 0u;
    for (std::size_t candidate = 1u; candidate < 7u; ++candidate)
        if (point.x >= kPhraseGridColumns[candidate + 1u])
            field = candidate;
    const bool preserveSelection = _gridSelection.active
        && _gridSelection.containsLinear(0u, 0u, field, rawRow, 7u);
    self.selectedRow = rawRow;
    self.selectedField = field;
    if (!preserveSelection) [self resetPhraseSelectionAtCursor];
    [self.window makeFirstResponder:self];
    [self setNeedsDisplay:YES];
    NSMenu* menu = (field == 2u || field == 4u)
        ? [self phraseSequenceActionMenuForField:field row:rawRow]
        : (field == 3u || field == 5u)
            ? [self phraseSequenceConditionMenuForField:field row:rawRow]
            : nil;
    if (menu)
        [NSMenu popUpContextMenu:menu withEvent:event forView:self];
    else
        [super rightMouseDown:event];
}

- (void)beginEditing
{
    [self beginEditingWithInitialText:nil];
}

- (void)beginEditingWithInitialText:(NSString*)initialText
{
    PhraseDefinition* phrase = [self phrase];
    if (!phrase || self.selectedField >= 7u) return;
    [self.editor removeFromSuperview];
    NSRect editorRect = NSInsetRect(NSMakeRect(
        kPhraseGridColumns[self.selectedField + 1u],
        30.0 + self.selectedRow * 22.0,
        kPhraseGridColumns[self.selectedField + 2u]
            - kPhraseGridColumns[self.selectedField + 1u], 22.0), 1.0, 1.0);
    self.editor = [[NSTextField alloc] initWithFrame:editorRect];
    self.editorCellRect = editorRect;
    // Use the primary Tracker's proven inline NSTextField path verbatim. The
    // TextEditor helper styles an already-active field editor; it is not a
    // substitute for configuring the NSTextField itself.
    S3GTrackerStyleTextField(self.editor, NSTextAlignmentCenter);
    self.editor.font = S3GTrackerFont(11.0, NSFontWeightSemibold);
    self.editor.delegate = self;
    self.editor.target = self;
    self.editor.action = @selector(commitEditor:);
    self.editor.accessibilityLabel = @"Direct tracker cell value";
    if (initialText)
        self.editor.stringValue = initialText;
    else if (self.selectedField == 0u)
        self.editor.stringValue = phraseNoteText(
            phrase->notes[self.selectedRow]);
    else if (self.selectedField == 1u)
        self.editor.stringValue = phraseValueText(
            phrase->velocities[self.selectedRow]);
    else if (self.selectedField == 6u)
        self.editor.stringValue = phraseGateText(
            phrase->gates[self.selectedRow]);
    else {
        const auto pair = (self.selectedField - 2u) / 2u;
        self.editor.stringValue = (self.selectedField % 2u) == 0u
            ? phraseFxText(phrase->fxPairs[pair].actions[self.selectedRow])
            : phraseFxValueText(phrase->fxPairs[pair].values[self.selectedRow]);
    }
    self.editor.frame = S3GTrackerExpandedCellEditorRect(
        self.editorCellRect, self.visibleRect,
        self.editor.stringValue, self.editor.font);
    [self addSubview:self.editor];
    [self.window makeFirstResponder:self.editor];
    S3GTrackerStyleTextEditor(self.editor);
    NSText* fieldEditor = self.editor.currentEditor;
    if ([fieldEditor respondsToSelector:@selector(setSelectedRange:)])
        [(NSTextView*)fieldEditor setSelectedRange:NSMakeRange(
            self.editor.stringValue.length, 0u)];
}

- (void)controlTextDidChange:(NSNotification*)notification
{
    if (notification.object != self.editor) return;
    self.editor.frame = S3GTrackerExpandedCellEditorRect(
        self.editorCellRect, self.visibleRect,
        self.editor.stringValue, self.editor.font);
}

- (void)commitEditor:(id)sender
{
    (void)sender;
    PhraseDefinition* phrase = [self phrase];
    if (!phrase || !self.editor) return;
    NSString* token = self.editor.stringValue.uppercaseString;
    if (self.selectedField == 0u) {
        if ([token isEqualToString:@"---"] || token.length == 0u)
            phrase->notes[self.selectedRow] = s3g::tracker::NoteCell::rest();
        else if ([token isEqualToString:@"RPT"])
            phrase->notes[self.selectedRow]
                = s3g::tracker::NoteCell::retriggerPrevious();
        else if ([token isEqualToString:@"HLD"])
            phrase->notes[self.selectedRow] = s3g::tracker::NoteCell::hold();
        else if ([token isEqualToString:@"KIL"])
            phrase->notes[self.selectedRow] = s3g::tracker::NoteCell::kill();
        else {
            std::size_t burstSlot = 0u;
            auto burstBankId = self.owner.trackerState->activeBurstBankId;
            const bool qualifiedBurst = s3g::tracker::parseQualifiedBurstToken(
                token.UTF8String, burstBankId, burstSlot);
            const bool simpleBurst = !qualifiedBurst
                && s3g::tracker::parseBurstSlot(token.UTF8String, burstSlot);
            if (qualifiedBurst || simpleBurst) {
                const auto* library = phraseBurstLibrary(
                    self.owner.trackerState, burstBankId);
                if (!library || burstSlot >= library->bursts.size()
                    || library->bursts[burstSlot].empty()) {
                    NSBeep();
                    return;
                }
                phrase->notes[self.selectedRow]
                    = s3g::tracker::NoteCell::withBurst(
                        static_cast<uint8_t>(burstSlot), burstBankId);
            } else {
                NSArray<NSString*>* parts = [token componentsSeparatedByString:@"+"];
                std::array<uint8_t, s3g::tracker::kMaximumNoteVoices> notes {};
                std::size_t count = 0u;
                for (NSString* part in parts) {
                    uint8_t note = 0u;
                    if (count >= notes.size()
                        || !s3g::tracker::parseMidiNote(part.UTF8String, note)) {
                        NSBeep();
                        return;
                    }
                    notes[count++] = note;
                }
                std::sort(notes.begin(), notes.begin() + count);
                if (std::adjacent_find(notes.begin(), notes.begin() + count)
                    != notes.begin() + count) { NSBeep(); return; }
                phrase->notes[self.selectedRow] = count == 1u
                    ? s3g::tracker::NoteCell::withNote(notes[0u])
                    : s3g::tracker::NoteCell::withNotes(notes, count);
            }
        }
    } else if (self.selectedField == 1u) {
        if ([token isEqualToString:@"DEF"] || token.length == 0u)
            phrase->velocities[self.selectedRow]
                = s3g::tracker::ValueCell::defaultValue();
        else if ([token isEqualToString:@"PRV"])
            phrase->velocities[self.selectedRow]
                = s3g::tracker::ValueCell::previous();
        else {
            NSArray<NSString*>* parts = [token componentsSeparatedByString:@"+"];
            if (parts.count == 0u
                || parts.count > s3g::tracker::kMaximumNoteVoices) {
                NSBeep(); return;
            }
            std::array<float, s3g::tracker::kMaximumNoteVoices> values {};
            for (NSUInteger index = 0u; index < parts.count; ++index) {
                NSString* part = parts[index];
                NSScanner* scanner = [NSScanner scannerWithString:part];
                double value = 0.0;
                if (![scanner scanDouble:&value] || !scanner.isAtEnd
                    || !std::isfinite(value)) { NSBeep(); return; }
                values[index] = static_cast<float>(
                    value > 1.0 ? value / 127.0 : value);
                if (values[index] < 0.0f || values[index] > 1.0f) {
                    NSBeep(); return;
                }
            }
            phrase->velocities[self.selectedRow]
                = s3g::tracker::ValueCell::withValues(values, parts.count);
        }
    } else if (self.selectedField == 6u) {
        if ([token isEqualToString:@"DEF"] || token.length == 0u) {
            phrase->gates[self.selectedRow] = GateCell::defaultValue();
        } else {
            NSArray<NSString*>* parts = [token componentsSeparatedByString:@"+"];
            if (parts.count == 0u
                || parts.count > s3g::tracker::kMaximumNoteVoices) {
                NSBeep(); return;
            }
            std::array<GateVoice, s3g::tracker::kMaximumNoteVoices> gates {};
            for (NSUInteger index = 0u; index < parts.count; ++index) {
                NSString* part = parts[index];
                if ([part isEqualToString:@"DEF"])
                    gates[index] = { GateVoiceMode::Default, 1.0f };
                else if ([part isEqualToString:@"TIE"]
                    || [part isEqualToString:@"T"])
                    gates[index] = { GateVoiceMode::Tie, 1.0f };
                else {
                    NSScanner* scanner = [NSScanner scannerWithString:part];
                    double rows = 0.0;
                    if (![scanner scanDouble:&rows] || !scanner.isAtEnd
                        || !std::isfinite(rows) || rows < 0.01 || rows > 64.0) {
                        NSBeep(); return;
                    }
                    gates[index] = { GateVoiceMode::Rows,
                        static_cast<float>(rows) };
                }
            }
            phrase->gates[self.selectedRow] = GateCell::withVoices(
                gates, parts.count);
        }
    } else {
        const auto pair = (self.selectedField - 2u) / 2u;
        if ((self.selectedField % 2u) == 0u) {
            auto& cell = phrase->fxPairs[pair].actions[self.selectedRow];
            if ([token isEqualToString:@"---"] || token.length == 0u)
                cell = s3g::tracker::FxActionCell::empty();
            else if ([token isEqualToString:@"PRV"])
                cell = s3g::tracker::FxActionCell::previous();
            else {
                uint8_t controller = 0u;
                if (s3g::tracker::parseMidiControlChange(token.UTF8String,
                        controller))
                    cell = s3g::tracker::FxActionCell::midiControlChange(
                        controller);
                else if (const auto* action = s3g::tracker::findSequencerAction(
                            token.UTF8String))
                    cell = s3g::tracker::FxActionCell::sequencer(action->action);
                else { NSBeep(); return; }
            }
        } else {
            auto& cell = phrase->fxPairs[pair].values[self.selectedRow];
            if ([token isEqualToString:@"PRV"] || token.length == 0u)
                cell = s3g::tracker::FxValueCell::previous();
            else {
                NSArray<NSString*>* parts = [token componentsSeparatedByString:@"+"];
                if (parts.count == 0u
                    || parts.count > s3g::tracker::kMaximumNoteVoices) {
                    NSBeep(); return;
                }
                std::array<float, s3g::tracker::kMaximumNoteVoices> values {};
                for (NSUInteger index = 0u; index < parts.count; ++index) {
                    NSString* part = parts[index];
                    NSScanner* scanner = [NSScanner scannerWithString:part];
                    double value = 0.0;
                    if (![scanner scanDouble:&value] || !scanner.isAtEnd
                        || !std::isfinite(value)) { NSBeep(); return; }
                    if (value > 1.0) value /= 127.0;
                    if (value < 0.0 || value > 1.0) { NSBeep(); return; }
                    values[index] = static_cast<float>(value);
                }
                cell = s3g::tracker::FxValueCell::withValues(
                    values, parts.count);
            }
        }
    }
    [self.editor removeFromSuperview];
    self.editor = nil;
    [self.owner phraseEdited];
    [self reloadModel];
    [self.window makeFirstResponder:self];
}

- (void)controlTextDidEndEditing:(NSNotification*)notification
{
    (void)notification;
    [self commitEditor:nil];
}

- (void)clearSelectedCell
{
    PhraseDefinition* phrase = [self phrase];
    if (!phrase || self.selectedRow >= phrase->length) return;
    if (_gridSelection.active) {
        PhraseDefinition candidate = *phrase;
        const auto range = _gridSelection.range();
        for (std::size_t row = range.firstRow; row <= range.lastRow; ++row)
            for (std::size_t field = range.firstField;
                 field <= range.lastField; ++field)
                writePhraseGridCell(candidate, field, row,
                    blankPhraseGridCell(field));
        *phrase = std::move(candidate);
        [self.owner phraseEdited];
        [self setNeedsDisplay:YES];
        return;
    }
    if (self.selectedField == 0u)
        phrase->notes[self.selectedRow] = s3g::tracker::NoteCell::rest();
    else if (self.selectedField == 1u)
        phrase->velocities[self.selectedRow]
            = s3g::tracker::ValueCell::defaultValue();
    else if (self.selectedField == 6u)
        phrase->gates[self.selectedRow] = GateCell::defaultValue();
    else {
        const auto pair = (self.selectedField - 2u) / 2u;
        if ((self.selectedField % 2u) == 0u)
            phrase->fxPairs[pair].actions[self.selectedRow]
                = s3g::tracker::FxActionCell::empty();
        else
            phrase->fxPairs[pair].values[self.selectedRow]
                = s3g::tracker::FxValueCell::previous();
    }
    [self.owner phraseEdited];
    [self setNeedsDisplay:YES];
}

- (s3g::tracker::app::GridSelectionRange)effectivePhraseSelection
{
    if (_gridSelection.active) return _gridSelection.range();
    s3g::tracker::app::GridSelectionRange range;
    range.page = 0u;
    range.firstTrack = range.lastTrack = 0u;
    range.firstField = range.lastField = self.selectedField;
    range.firstRow = range.lastRow = self.selectedRow;
    return range;
}

- (void)resetPhraseSelectionAtCursor
{
    _gridSelection.page = 0u;
    _gridSelection.anchorTrack = _gridSelection.focusTrack = 0u;
    _gridSelection.anchorField = _gridSelection.focusField = self.selectedField;
    _gridSelection.anchorRow = _gridSelection.focusRow = self.selectedRow;
    _gridSelection.active = false;
    self.selectingGridCells = NO;
}

- (void)phraseSelectAll:(id)sender
{
    (void)sender;
    PhraseDefinition* phrase = [self phrase];
    if (!phrase) return;
    _gridSelection.page = 0u;
    _gridSelection.anchorTrack = _gridSelection.focusTrack = 0u;
    _gridSelection.anchorField = 0u;
    _gridSelection.focusField = 6u;
    _gridSelection.anchorRow = 0u;
    _gridSelection.focusRow = phrase->length - 1u;
    _gridSelection.active = true;
    [self setNeedsDisplay:YES];
}

- (void)phraseCopy:(id)sender
{
    (void)sender;
    PhraseDefinition* phrase = [self phrase];
    if (!phrase) return;
    const auto range = [self effectivePhraseSelection];
    _copiedGridCells.clear();
    _copiedColumnTypes.clear();
    _copiedGridCells.reserve(range.rowCount() * range.fieldCount());
    _copiedColumnTypes.reserve(range.fieldCount());
    NSMutableArray<NSString*>* lines = [NSMutableArray array];
    for (std::size_t row = range.firstRow; row <= range.lastRow; ++row) {
        NSMutableArray<NSString*>* cells = [NSMutableArray array];
        for (std::size_t field = range.firstField;
             field <= range.lastField; ++field) {
            _copiedGridCells.push_back(phraseGridCellAt(*phrase, field, row));
            [cells addObject:phraseGridCellText(*phrase, field, row)];
        }
        [lines addObject:[cells componentsJoinedByString:@"\t"]];
    }
    for (std::size_t field = range.firstField;
         field <= range.lastField; ++field)
        _copiedColumnTypes.push_back(phraseGridFieldType(field));
    NSString* value = [lines componentsJoinedByString:@"\n"];
    NSPasteboard* pasteboard = NSPasteboard.generalPasteboard;
    [pasteboard clearContents];
    [pasteboard setString:value forType:NSPasteboardTypeString];
    self.copiedClipboardText = value;
    self.copiedPasteboardChangeCount = pasteboard.changeCount;
    self.copiedRowCount = range.rowCount();
    self.copiedFieldCount = range.fieldCount();
}

- (void)phraseCut:(id)sender
{
    [self phraseCopy:sender];
    [self clearSelectedCell];
}

- (void)phrasePaste:(id)sender
{
    (void)sender;
    PhraseDefinition* phrase = [self phrase];
    if (!phrase) return;
    NSPasteboard* pasteboard = NSPasteboard.generalPasteboard;
    NSString* value = [pasteboard stringForType:NSPasteboardTypeString];
    if (!value && self.copiedClipboardText
        && self.copiedPasteboardChangeCount == pasteboard.changeCount)
        value = self.copiedClipboardText;
    if (!value) { NSBeep(); return; }
    NSString* normalized = [value stringByReplacingOccurrencesOfString:@"\r\n"
        withString:@"\n"];
    normalized = [normalized stringByReplacingOccurrencesOfString:@"\r"
        withString:@"\n"];
    NSMutableArray<NSString*>* lines = [[normalized
        componentsSeparatedByString:@"\n"] mutableCopy];
    while (lines.count > 1u && lines.lastObject.length == 0u)
        [lines removeLastObject];
    if (lines.count == 0u) { NSBeep(); return; }
    NSMutableArray<NSArray<NSString*>*>* rows = [NSMutableArray array];
    NSUInteger widest = 0u;
    for (NSString* line in lines) {
        NSArray<NSString*>* cells = [line componentsSeparatedByString:@"\t"];
        widest = std::max(widest, cells.count);
        [rows addObject:cells];
    }
    if (widest == 0u) { NSBeep(); return; }
    const auto range = [self effectivePhraseSelection];
    const bool fillSelection = rows.count == 1u && widest == 1u
        && _gridSelection.active;
    const bool structured = self.copiedPasteboardChangeCount
            == pasteboard.changeCount
        && self.copiedRowCount == rows.count
        && self.copiedFieldCount == widest
        && _copiedColumnTypes.size() == widest
        && _copiedGridCells.size() == rows.count * widest;
    if (!fillSelection && (range.firstRow + rows.count > phrase->length
        || range.firstField + widest > 7u)) { NSBeep(); return; }
    PhraseDefinition candidate = *phrase;
    if (fillSelection) {
        const auto sourceType = structured ? _copiedColumnTypes[0u] : 255u;
        for (std::size_t row = range.firstRow; row <= range.lastRow; ++row) {
            for (std::size_t field = range.firstField;
                 field <= range.lastField; ++field) {
                if (structured) {
                    if (sourceType != phraseGridFieldType(field)) {
                        NSBeep(); return;
                    }
                    writePhraseGridCell(candidate, field, row,
                        _copiedGridCells[0u]);
                } else if (!applyPhraseCellText(rows[0u][0u], candidate,
                               *self.owner.trackerState,
                               row, field)) {
                    NSBeep(); return;
                }
            }
        }
    } else {
        if (structured) {
            for (std::size_t offset = 0u; offset < _copiedColumnTypes.size();
                 ++offset)
                if (_copiedColumnTypes[offset]
                    != phraseGridFieldType(range.firstField + offset)) {
                    NSBeep(); return;
                }
        }
        for (NSUInteger rowOffset = 0u; rowOffset < rows.count; ++rowOffset) {
            NSArray<NSString*>* cells = rows[rowOffset];
            if (range.firstField + cells.count > 7u) { NSBeep(); return; }
            for (NSUInteger fieldOffset = 0u; fieldOffset < cells.count;
                 ++fieldOffset) {
                const auto destinationRow = range.firstRow
                    + static_cast<std::size_t>(rowOffset);
                const auto destinationField = range.firstField
                    + static_cast<std::size_t>(fieldOffset);
                if (structured)
                    writePhraseGridCell(candidate, destinationField,
                        destinationRow, _copiedGridCells[
                            static_cast<std::size_t>(rowOffset) * widest
                                + static_cast<std::size_t>(fieldOffset)]);
                else if (!applyPhraseCellText(cells[fieldOffset], candidate,
                               *self.owner.trackerState,
                               destinationRow, destinationField)) {
                    NSBeep(); return;
                }
            }
        }
    }
    *phrase = std::move(candidate);
    [self.owner phraseEdited];
    [self reloadModel];
    [self.window makeFirstResponder:self];
}

- (BOOL)handleGridKeyEvent:(NSEvent*)event
{
    PhraseDefinition* phrase = [self phrase];
    if (!phrase) return NO;
    NSString* key = event.charactersIgnoringModifiers.lowercaseString;
    const auto shortcutModifiers = event.modifierFlags
        & (NSEventModifierFlagCommand | NSEventModifierFlagControl
            | NSEventModifierFlagOption | NSEventModifierFlagShift);
    if (shortcutModifiers == NSEventModifierFlagControl) {
        if ([key isEqualToString:@"a"]) [self phraseSelectAll:nil];
        else if ([key isEqualToString:@"c"]) [self phraseCopy:nil];
        else if ([key isEqualToString:@"x"]) [self phraseCut:nil];
        else if ([key isEqualToString:@"v"]) [self phrasePaste:nil];
        else return NO;
        return YES;
    }
    if (shortcutModifiers == (NSEventModifierFlagControl
            | NSEventModifierFlagShift)
        && (event.keyCode == 123u || event.keyCode == 124u
            || event.keyCode == 125u || event.keyCode == 126u)) {
        if (!_gridSelection.active) {
            _gridSelection.page = 0u;
            _gridSelection.anchorTrack = _gridSelection.focusTrack = 0u;
            _gridSelection.anchorField = _gridSelection.focusField
                = self.selectedField;
            _gridSelection.anchorRow = _gridSelection.focusRow
                = self.selectedRow;
        }
        if (event.keyCode == 123u || event.keyCode == 124u)
            _gridSelection.focusField = event.keyCode == 123u
                ? (_gridSelection.focusField == 0u ? 0u
                    : _gridSelection.focusField - 1u)
                : std::min<std::size_t>(_gridSelection.focusField + 1u, 6u);
        else
            _gridSelection.focusRow = event.keyCode == 126u
                ? (_gridSelection.focusRow == 0u ? 0u
                    : _gridSelection.focusRow - 1u)
                : std::min(_gridSelection.focusRow + 1u, phrase->length - 1u);
        _gridSelection.active = !_gridSelection.isSingleCell();
        self.selectedField = _gridSelection.focusField;
        self.selectedRow = _gridSelection.focusRow;
        [self setNeedsDisplay:YES];
        return YES;
    }
    if (event.keyCode == 36u) {
        [self resetPhraseSelectionAtCursor];
        [self beginEditing];
        return YES;
    }
    if (event.keyCode == 51u || event.keyCode == 117u) {
        [self clearSelectedCell];
        return YES;
    }
    if (event.keyCode == 125u || event.keyCode == 126u) {
        self.selectedRow = event.keyCode == 125u
            ? std::min(self.selectedRow + 1u, phrase->length - 1u)
            : (self.selectedRow == 0u ? 0u : self.selectedRow - 1u);
        [self resetPhraseSelectionAtCursor];
        [self setNeedsDisplay:YES];
        return YES;
    }
    if (event.keyCode == 123u || event.keyCode == 124u
        || event.keyCode == 48u) {
        const bool backwards = event.keyCode == 123u
            || (event.keyCode == 48u
                && (event.modifierFlags & NSEventModifierFlagShift) != 0u);
        self.selectedField = backwards
            ? (self.selectedField == 0u ? 6u : self.selectedField - 1u)
            : (self.selectedField + 1u) % 7u;
        [self resetPhraseSelectionAtCursor];
        [self setNeedsDisplay:YES];
        return YES;
    }
    const auto modifiers = event.modifierFlags
        & (NSEventModifierFlagCommand | NSEventModifierFlagControl
            | NSEventModifierFlagOption);
    if (modifiers == 0u && key.length == 1u) {
        const unichar typed = [key characterAtIndex:0u];
        const bool digit = typed >= '0' && typed <= '9';
        const bool begin = self.selectedField == 0u
            ? digit || (typed >= 'a' && typed <= 'g')
                || typed == 'r' || typed == 'h' || typed == 'k'
                || typed == '-'
            : self.selectedField == 1u
                ? digit || typed == '.' || typed == 'd' || typed == 'p'
            : self.selectedField == 6u
                ? digit || typed == '.' || typed == 'd' || typed == 't'
            : (self.selectedField % 2u) == 0u
                ? (typed >= 'a' && typed <= 'z') || typed == '-'
                : digit || typed == '.' || typed == 'p';
        if (begin) {
            [self resetPhraseSelectionAtCursor];
            [self beginEditingWithInitialText:key];
            return YES;
        }
    }
    return NO;
}

- (BOOL)performKeyEquivalent:(NSEvent*)event
{
    // REAPER/SWELL offers printable keys through key-equivalent dispatch
    // before sending keyDown to embedded AppKit views. Handle each owned key
    // here exactly once so Phrase entry is as dependable as the main Tracker.
    if (self.editor && self.editor.currentEditor == self.window.firstResponder)
        return [super performKeyEquivalent:event];
    if ((event.modifierFlags & NSEventModifierFlagCommand) != 0u)
        return [super performKeyEquivalent:event];
    return [self handleGridKeyEvent:event]
        ? YES : [super performKeyEquivalent:event];
}

- (void)keyDown:(NSEvent*)event
{
    if (![self handleGridKeyEvent:event]) [super keyDown:event];
}

@end

@implementation S3GTrackerPhraseView

- (instancetype)initWithState:(TrackerViewState*)state
    callbacks:(WorkspaceCallbacks*)callbacks
{
    self = [super initWithNibName:nil bundle:nil];
    if (self) {
        self.trackerState = state;
        self.trackerCallbacks = callbacks;
        self.previewPlayheadRow = -1;
        self.previewLastRow = -1;
    }
    return self;
}

- (void)dealloc
{
    [self.previewTimer invalidate];
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

- (NSTextField*)suiteLabel:(NSString*)title panel:(NSView*)panel
{
    S3GTrackerSuiteLabel* label = [[S3GTrackerSuiteLabel alloc]
        initWithFrame:NSZeroRect];
    label.stringValue = title;
    [panel addSubview:label];
    return label;
}

- (void)layoutPhraseInterface
{
    if (!self.isViewLoaded || !self.editorPanel) return;
    const auto family = s3g::gui_layout::trackerGeometryFamilyLayout({
        static_cast<double>(NSWidth(self.view.bounds)),
        static_cast<double>(NSHeight(self.view.bounds)),
    }, 1u, 10u, false);
    const auto cocoaRect = [](const s3g::gui_layout::Rect& rect) {
        return NSMakeRect(static_cast<CGFloat>(rect.x),
            static_cast<CGFloat>(rect.y),
            static_cast<CGFloat>(rect.width),
            static_cast<CGFloat>(rect.height));
    };
    self.editorPanel.frame = cocoaRect(family.fieldPanel);
    self.libraryPanel.frame = cocoaRect(family.laneCycle.frame);
    self.auditionPanel.frame = cocoaRect(family.editShape.frame);
    self.placementPanel.frame = cocoaRect(family.trackerBridge.frame);

    const CGFloat header = static_cast<CGFloat>(
        s3g::gui_layout::kStandardMetrics.headerHeight);
    self.gridScroll.frame = NSMakeRect(1.0, header,
        std::max<CGFloat>(1.0, NSWidth(self.editorPanel.bounds) - 2.0),
        std::max<CGFloat>(1.0,
            NSHeight(self.editorPanel.bounds) - header - 1.0));
    NSRect gridFrame = self.grid.frame;
    gridFrame.size.width = std::max<CGFloat>(800.0,
        NSWidth(self.gridScroll.contentView.bounds));
    self.grid.frame = gridFrame;

    const CGFloat labelX = static_cast<CGFloat>(
        s3g::gui_layout::kStandardMetrics.labelInset);
    const CGFloat controlX = static_cast<CGFloat>(
        s3g::gui_layout::kStandardMetrics.controlInset);
    const CGFloat firstRow = static_cast<CGFloat>(
        s3g::gui_layout::kStandardMetrics.firstRowOffset);
    const CGFloat rowPitch = static_cast<CGFloat>(
        s3g::gui_layout::kStandardMetrics.rowPitch);
    const CGFloat right = static_cast<CGFloat>(
        s3g::gui_layout::kStandardMetrics.panelRightInset);
    const auto layoutLabel = ^(NSTextField* label, NSUInteger row) {
        const CGFloat y = firstRow + static_cast<CGFloat>(row) * rowPitch;
        label.frame = NSMakeRect(labelX, y - 1.0,
            std::max<CGFloat>(20.0, controlX - labelX - 6.0), 15.0);
    };
    const auto controlFrame = ^NSRect(NSView* panel, NSUInteger row) {
        const CGFloat y = firstRow + static_cast<CGFloat>(row) * rowPitch;
        return NSMakeRect(controlX, y - 1.0,
            std::max<CGFloat>(20.0,
                NSWidth(panel.bounds) - controlX - right), 15.0);
    };
    const auto textFrame = ^NSRect(NSView* panel, NSUInteger row) {
        NSRect frame = controlFrame(panel, row);
        frame.origin.y -= 7.0;
        frame.size.height = static_cast<CGFloat>(
            s3g::gui_layout::kStandardMetrics.hitHeight);
        return frame;
    };
    const auto layoutButtons = ^(NSArray<NSButton*>* buttons,
                                  NSView* panel, NSUInteger row) {
        NSRect available = controlFrame(panel, row);
        const CGFloat gap = 4.0;
        const CGFloat width = (NSWidth(available)
                - gap * static_cast<CGFloat>(buttons.count - 1u))
            / static_cast<CGFloat>(std::max<NSUInteger>(1u, buttons.count));
        for (NSUInteger index = 0u; index < buttons.count; ++index)
            buttons[index].frame = NSMakeRect(
                NSMinX(available) + static_cast<CGFloat>(index)
                    * (width + gap), NSMinY(available), width,
                NSHeight(available));
    };

    layoutLabel(self.bankLabel, 0u);
    layoutLabel(self.phraseLabel, 1u);
    layoutLabel(self.nameLabel, 2u);
    layoutLabel(self.lengthLabel, 3u);
    self.bankPopup.frame = controlFrame(self.libraryPanel, 0u);
    self.libraryPopup.frame = controlFrame(self.libraryPanel, 1u);
    self.nameField.frame = textFrame(self.libraryPanel, 2u);
    self.lengthPopup.frame = controlFrame(self.libraryPanel, 3u);
    layoutButtons(@[ self.saveButton, self.duplicateButton,
        self.deleteButton ], self.libraryPanel, 4u);
    self.projectCopyButton.frame = controlFrame(self.libraryPanel, 5u);
    layoutButtons(@[ self.clearBankButton, self.deleteBankButton ],
        self.libraryPanel, 6u);
    self.importPackButton.frame = controlFrame(self.libraryPanel, 7u);
    self.exportPackButton.frame = controlFrame(self.libraryPanel, 8u);
    self.exportAllButton.frame = controlFrame(self.libraryPanel, 9u);

    layoutLabel(self.previewChannelLabel, 0u);
    self.previewChannelPopup.frame = controlFrame(self.auditionPanel, 0u);
    self.previewButton.frame = NSMakeRect(
        NSWidth(self.auditionPanel.bounds) - 90.0, 3.0, 78.0, 15.0);

    layoutLabel(self.placementModeLabel, 0u);
    layoutLabel(self.placementTargetLabel, 1u);
    self.modePopup.frame = controlFrame(self.placementPanel, 0u);
    self.placementTargetValue.frame = controlFrame(
        self.placementPanel, 1u);
    self.statusLabel.frame = NSMakeRect(labelX,
        firstRow + rowPitch * 2.0 - 1.0,
        std::max<CGFloat>(20.0,
            NSWidth(self.placementPanel.bounds) - labelX - right), 30.0);
    self.placeButton.frame = NSMakeRect(
        NSWidth(self.placementPanel.bounds) - 154.0, 3.0, 142.0, 15.0);
}

- (void)loadView
{
    S3GTrackerPhraseRootView* root = [[S3GTrackerPhraseRootView alloc]
        initWithFrame:NSMakeRect(0.0, 0.0, 1320.0, 780.0)];
    root.wantsLayer = YES;
    root.layer.backgroundColor = S3GTrackerThemeColor(
        S3GTrackerThemeRole::Canvas).CGColor;
    root.layoutOwner = self;
    self.view = root;

    self.editorPanel = [[S3GTrackerToolboxView alloc]
        initWithFrame:NSZeroRect];
    self.editorPanel.toolboxTitle =
        @"PHRASE TRACKER  /  PROJECT MIDI PHRASE";
    [root addSubview:self.editorPanel];
    self.libraryPanel = [[S3GTrackerToolboxView alloc]
        initWithFrame:NSZeroRect];
    self.libraryPanel.toolboxTitle = @"PHRASE LIBRARY";
    [root addSubview:self.libraryPanel];
    self.auditionPanel = [[S3GTrackerToolboxView alloc]
        initWithFrame:NSZeroRect];
    self.auditionPanel.toolboxTitle = @"AUDITION";
    [root addSubview:self.auditionPanel];
    self.placementPanel = [[S3GTrackerToolboxView alloc]
        initWithFrame:NSZeroRect];
    self.placementPanel.toolboxTitle = @"TRACKER BRIDGE";
    [root addSubview:self.placementPanel];

    self.libraryPopup = [[S3GTrackerPopupButton alloc]
        initWithFrame:NSZeroRect pullsDown:NO];
    self.libraryPopup.s3gUsesCanvasMenu = YES;
    self.libraryPopup.target = self;
    self.libraryPopup.action = @selector(slotChanged:);
    [self.libraryPanel addSubview:self.libraryPopup];
    self.bankPopup = [[S3GTrackerPopupButton alloc]
        initWithFrame:NSZeroRect pullsDown:NO];
    self.bankPopup.s3gUsesCanvasMenu = YES;
    self.bankPopup.target = self;
    self.bankPopup.action = @selector(bankChanged:);
    [self.libraryPanel addSubview:self.bankPopup];
    self.nameField = [[NSTextField alloc] initWithFrame:NSZeroRect];
    S3GTrackerStyleSuiteTextField(self.nameField, NSTextAlignmentLeft);
    self.nameField.delegate = self;
    [self.libraryPanel addSubview:self.nameField];
    self.lengthPopup = [[S3GTrackerPopupButton alloc]
        initWithFrame:NSZeroRect pullsDown:NO];
    self.lengthPopup.s3gUsesCanvasMenu = YES;
    for (NSInteger length = 2; length <= 64; ++length) {
        [self.lengthPopup addItemWithTitle:[NSString stringWithFormat:
            @"%ld ROWS", static_cast<long>(length)]];
        self.lengthPopup.lastItem.representedObject = @(length);
    }
    self.lengthPopup.target = self;
    self.lengthPopup.action = @selector(lengthChanged:);
    [self.libraryPanel addSubview:self.lengthPopup];
    self.previewChannelPopup = [[S3GTrackerPopupButton alloc]
        initWithFrame:NSZeroRect pullsDown:NO];
    self.previewChannelPopup.s3gUsesCanvasMenu = YES;
    for (NSInteger channel = 1; channel <= 16; ++channel) {
        [self.previewChannelPopup addItemWithTitle:[NSString stringWithFormat:
            @"%02ld", static_cast<long>(channel)]];
        self.previewChannelPopup.lastItem.representedObject = @(channel);
    }
    self.previewChannelPopup.target = self;
    self.previewChannelPopup.action = @selector(previewChannelChanged:);
    self.previewChannelPopup.toolTip = @"MIDI channel used only by Phrase Preview";
    [self.auditionPanel addSubview:self.previewChannelPopup];
    self.saveButton = [self button:@"SAVE" action:@selector(savePressed:)];
    self.duplicateButton = [self button:@"DUP"
        action:@selector(duplicatePressed:)];
    self.deleteButton = [self button:@"DELETE"
        action:@selector(deletePressed:)];
    self.clearBankButton = [self button:@"CLEAR BANK"
        action:@selector(clearBankPressed:)];
    self.deleteBankButton = [self button:@"DELETE BANK"
        action:@selector(deleteBankPressed:)];
    self.projectCopyButton = [self button:@"COPY TO PROJECT"
        action:@selector(copyProjectPressed:)];
    self.previewButton = [self button:@"PREVIEW ▶"
        action:@selector(previewPressed:)];
    for (NSButton* button in @[ self.saveButton, self.duplicateButton,
             self.deleteButton, self.projectCopyButton,
             self.clearBankButton, self.deleteBankButton ])
        [self.libraryPanel addSubview:button];
    [self.auditionPanel addSubview:self.previewButton];

    self.bankLabel = [self suiteLabel:@"BANK" panel:self.libraryPanel];
    self.phraseLabel = [self suiteLabel:@"PHRASE" panel:self.libraryPanel];
    self.nameLabel = [self suiteLabel:@"NAME" panel:self.libraryPanel];
    self.lengthLabel = [self suiteLabel:@"LENGTH" panel:self.libraryPanel];
    self.previewChannelLabel = [self suiteLabel:@"MIDI CH"
        panel:self.auditionPanel];

    self.modePopup = [[S3GTrackerPopupButton alloc]
        initWithFrame:NSZeroRect pullsDown:NO];
    self.modePopup.s3gUsesCanvasMenu = YES;
    [self.modePopup addItemsWithTitles:@[@"REPLACE", @"MERGE EMPTY"]];
    [self.placementPanel addSubview:self.modePopup];
    self.placeButton = [self button:@"COPY TO LANE"
        action:@selector(placePressed:)];
    [self.placementPanel addSubview:self.placeButton];
    self.importPackButton = [self button:@"IMPORT PACK"
        action:@selector(importPackPressed:)];
    self.exportPackButton = [self button:@"EXPORT ONE"
        action:@selector(exportPackPressed:)];
    self.exportAllButton = [self button:@"EXPORT ALL"
        action:@selector(exportAllPacksPressed:)];
    for (NSButton* button in @[ self.importPackButton,
             self.exportPackButton, self.exportAllButton ])
        [self.libraryPanel addSubview:button];
    self.placementModeLabel = [self suiteLabel:@"MODE"
        panel:self.placementPanel];
    self.placementTargetLabel = [self suiteLabel:@"TARGET"
        panel:self.placementPanel];
    self.placementTargetValue = [self suiteLabel:@"—"
        panel:self.placementPanel];
    self.statusLabel = [self suiteLabel:@"" panel:self.placementPanel];
    self.statusLabel.font = S3GTrackerFont(8.0);
    self.statusLabel.textColor = S3GTrackerThemeColor(
        S3GTrackerThemeRole::TextMuted);

    self.grid = [[S3GTrackerPhraseGridView alloc] initWithFrame:NSZeroRect];
    self.grid.owner = self;
    root.phraseGrid = self.grid;
    self.gridScroll = [[NSScrollView alloc] initWithFrame:NSZeroRect];
    self.gridScroll.documentView = self.grid;
    self.gridScroll.hasVerticalScroller = YES;
    self.gridScroll.hasHorizontalScroller = YES;
    self.gridScroll.borderType = NSNoBorder;
    self.gridScroll.drawsBackground = YES;
    self.gridScroll.backgroundColor = S3GTrackerThemeColor(
        S3GTrackerThemeRole::Workspace);
    [self.editorPanel addSubview:self.gridScroll];

    [self layoutPhraseInterface];
    [self reloadModel];
}

- (PhraseDefinition*)selectedPhrase
{
    if (!self.trackerState) return nullptr;
    self.trackerState->selectedPhrase = std::min<std::size_t>(
        self.trackerState->selectedPhrase,
        s3g::tracker::kPhraseLibrarySlots - 1u);
    auto& phrase = self.trackerState->phraseLibrary.phrases[
        self.trackerState->selectedPhrase];
    phrase.length = std::clamp(phrase.length,
        s3g::tracker::kMinimumPhraseRows,
        s3g::tracker::kMaximumPhraseRows);
    phrase.notes.resize(phrase.length, s3g::tracker::NoteCell::rest());
    phrase.velocities.resize(phrase.length,
        s3g::tracker::ValueCell::defaultValue());
    phrase.gates.resize(phrase.length, GateCell::defaultValue());
    for (auto& pair : phrase.fxPairs) {
        pair.actions.resize(phrase.length, s3g::tracker::FxActionCell::empty());
        pair.values.resize(phrase.length,
            s3g::tracker::FxValueCell::previous());
        pair.actionColumn.length = phrase.length;
        pair.valueColumn.length = phrase.length;
    }
    return &phrase;
}

- (void)importPackPressed:(id)sender
{
    (void)sender;
    if (self.trackerCallbacks && self.trackerCallbacks->importAssetPack)
        self.trackerCallbacks->importAssetPack();
}

- (void)bankChanged:(id)sender
{
    (void)sender;
    [self stopPhrasePreview];
    NSNumber* selected = self.bankPopup.selectedItem.representedObject;
    if (selected && self.trackerCallbacks
        && self.trackerCallbacks->selectPhraseBank)
        self.trackerCallbacks->selectPhraseBank(
            static_cast<s3g::tracker::AssetBankId>(selected.unsignedIntValue));
}

- (void)clearBankPressed:(id)sender
{
    (void)sender;
    [self stopPhrasePreview];
    if (self.trackerCallbacks && self.trackerCallbacks->clearPhraseBank)
        self.trackerCallbacks->clearPhraseBank();
}

- (void)deleteBankPressed:(id)sender
{
    (void)sender;
    [self stopPhrasePreview];
    if (self.trackerCallbacks && self.trackerCallbacks->deletePhraseBank)
        self.trackerCallbacks->deletePhraseBank();
}

- (void)copyProjectPressed:(id)sender
{
    (void)sender;
    [self stopPhrasePreview];
    if (!self.trackerState || !self.trackerCallbacks
        || !self.trackerCallbacks->copyPhraseToProject) return;
    const auto destination = self.trackerCallbacks->copyPhraseToProject(
        self.trackerState->selectedPhrase);
    if (destination >= s3g::tracker::kPhraseLibrarySlots) {
        NSBeep();
        return;
    }
    self.trackerState->selectedPhrase = destination;
    [self reloadModel];
}

- (void)exportPackPressed:(id)sender
{
    (void)sender;
    const PhraseDefinition* phrase = [self selectedPhrase];
    if (phrase && (!phrase->empty() || !phrase->name.empty())
        && self.trackerCallbacks
        && self.trackerCallbacks->exportPhraseAssetPack)
        self.trackerCallbacks->exportPhraseAssetPack(
            self.trackerState->selectedPhrase);
    else
        NSBeep();
}

- (void)exportAllPacksPressed:(id)sender
{
    (void)sender;
    bool hasPhrases = false;
    if (self.trackerState)
        for (const auto& phrase : self.trackerState->phraseLibrary.phrases)
            hasPhrases |= !phrase.empty() || !phrase.name.empty();
    if (hasPhrases && self.trackerCallbacks
        && self.trackerCallbacks->exportPhraseLibraryAssetPack)
        self.trackerCallbacks->exportPhraseLibraryAssetPack();
    else
        NSBeep();
}

- (void)reloadModel
{
    if (!self.isViewLoaded || !self.trackerState) return;
    [self.bankPopup removeAllItems];
    for (const auto& bank : self.trackerState->phraseBanks) {
        NSString* title = bank.name.empty() ? @"UNTITLED BANK"
            : phraseString(bank.name);
        [self.bankPopup addItemWithTitle:title];
        self.bankPopup.lastItem.representedObject = @(bank.id);
        if (bank.id == self.trackerState->activePhraseBankId)
            [self.bankPopup selectItem:self.bankPopup.lastItem];
    }
    self.deleteBankButton.enabled = self.trackerState->activePhraseBankId
        != s3g::tracker::kProjectAssetBankId;
    [self.libraryPopup removeAllItems];
    for (std::size_t slot = 0u; slot < s3g::tracker::kPhraseLibrarySlots; ++slot) {
        const auto& phrase = self.trackerState->phraseLibrary.phrases[slot];
        NSString* name = phrase.name.empty() ? @"EMPTY" : phraseString(phrase.name);
        [self.libraryPopup addItemWithTitle:[NSString stringWithFormat:
            @"P%02lu · %@", static_cast<unsigned long>(slot + 1u), name]];
        self.libraryPopup.lastItem.representedObject = @(slot);
    }
    [self.libraryPopup selectItemAtIndex:static_cast<NSInteger>(
        self.trackerState->selectedPhrase)];
    PhraseDefinition* phrase = [self selectedPhrase];
    self.nameField.stringValue = phraseString(phrase->name);
    [self.lengthPopup selectItemWithTitle:[NSString stringWithFormat:
        @"%lu ROWS", static_cast<unsigned long>(phrase->length)]];
    [self.previewChannelPopup selectItemAtIndex:static_cast<NSInteger>(
        std::clamp<int>(phrase->previewMidiChannel, 1, 16) - 1)];
    if (self.trackerState->session.pattern.tracks.empty())
        self.placementTargetValue.stringValue = @"NO LANES";
    else
        self.placementTargetValue.stringValue = [NSString stringWithFormat:
            @"T%02lu  ·  ROW %03lu",
            static_cast<unsigned long>(std::min<std::size_t>(
                self.trackerState->session.selectedTrack,
                self.trackerState->session.pattern.tracks.size() - 1u) + 1u),
            static_cast<unsigned long>(
                self.trackerState->session.selectedRow + 1u)];
    [self.grid reloadModel];
}

- (void)phraseEdited
{
    if (self.trackerCallbacks && self.trackerCallbacks->patternChanged)
        self.trackerCallbacks->patternChanged();
}

- (void)slotChanged:(id)sender
{
    (void)sender;
    [self stopPhrasePreview];
    self.trackerState->selectedPhrase = static_cast<std::size_t>(
        self.libraryPopup.indexOfSelectedItem);
    [self reloadModel];
}

- (void)lengthChanged:(id)sender
{
    (void)sender;
    [self stopPhrasePreview];
    PhraseDefinition* phrase = [self selectedPhrase];
    if (!phrase) return;
    const auto length = static_cast<std::size_t>(
        [self.lengthPopup.selectedItem.representedObject unsignedIntegerValue]);
    phrase->length = std::clamp(length, s3g::tracker::kMinimumPhraseRows,
        s3g::tracker::kMaximumPhraseRows);
    phrase->notes.resize(phrase->length, s3g::tracker::NoteCell::rest());
    phrase->velocities.resize(phrase->length,
        s3g::tracker::ValueCell::defaultValue());
    phrase->gates.resize(phrase->length, GateCell::defaultValue());
    for (auto& pair : phrase->fxPairs) {
        pair.actions.resize(phrase->length, s3g::tracker::FxActionCell::empty());
        pair.values.resize(phrase->length, s3g::tracker::FxValueCell::previous());
        pair.actionColumn.length = phrase->length;
        pair.valueColumn.length = phrase->length;
    }
    [self phraseEdited];
    [self reloadModel];
}

- (void)previewChannelChanged:(id)sender
{
    (void)sender;
    PhraseDefinition* phrase = [self selectedPhrase];
    if (!phrase || self.previewChannelPopup.indexOfSelectedItem < 0) return;
    phrase->previewMidiChannel = static_cast<uint8_t>(
        self.previewChannelPopup.indexOfSelectedItem + 1);
    [self phraseEdited];
}

- (void)savePressed:(id)sender
{
    (void)sender;
    PhraseDefinition* phrase = [self selectedPhrase];
    if (!phrase) return;
    NSData* utf8 = [self.nameField.stringValue dataUsingEncoding:NSUTF8StringEncoding];
    if (utf8.length > s3g::tracker::kMaximumPhraseNameBytes) { NSBeep(); return; }
    const char* utf8Name = self.nameField.stringValue.UTF8String;
    phrase->name = utf8Name ? utf8Name : "";
    [self phraseEdited];
    [self reloadModel];
}

- (void)duplicatePressed:(id)sender
{
    (void)sender;
    [self stopPhrasePreview];
    if (!self.trackerState) return;
    auto& phrases = self.trackerState->phraseLibrary.phrases;
    const auto sourceSlot = std::min<std::size_t>(
        self.trackerState->selectedPhrase, phrases.size() - 1u);
    const auto& source = phrases[sourceSlot];
    if (source.empty() && source.name.empty()) { NSBeep(); return; }
    const auto available = std::find_if(phrases.begin(), phrases.end(),
        [](const PhraseDefinition& phrase) {
            return phrase.empty() && phrase.name.empty();
        });
    if (available == phrases.end()) { NSBeep(); return; }
    const auto destination = static_cast<std::size_t>(
        available - phrases.begin());
    phrases[destination] = source;
    if (phrases[destination].name.empty())
        phrases[destination].name = "PHRASE COPY";
    else if (phrases[destination].name.size() + 5u
        <= s3g::tracker::kMaximumPhraseNameBytes)
        phrases[destination].name += " COPY";
    self.trackerState->selectedPhrase = destination;
    [self phraseEdited];
    [self reloadModel];
}

- (void)deletePressed:(id)sender
{
    (void)sender;
    [self stopPhrasePreview];
    PhraseDefinition* phrase = [self selectedPhrase];
    if (!phrase) return;
    *phrase = s3g::tracker::makeBlankPhrase(16u);
    [self phraseEdited];
    [self reloadModel];
}

- (void)placePressed:(id)sender
{
    (void)sender;
    if (!self.trackerState || self.trackerState->session.pattern.tracks.empty()) return;
    [self placeAtTrack:self.trackerState->session.selectedTrack
        row:self.trackerState->session.selectedRow
        merge:self.modePopup.indexOfSelectedItem == 1];
}

- (BOOL)captureTrack:(std::size_t)track firstRow:(std::size_t)firstRow
    lastRow:(std::size_t)lastRow
{
    if (!self.trackerState || self.trackerState->songPlaybackActive
        || track >= self.trackerState->session.pattern.tracks.size()) return NO;
    PhraseDefinition* phrase = [self selectedPhrase];
    if (!s3g::tracker::capturePhrase(self.trackerState->session.pattern,
            track, firstRow, lastRow, *phrase)) { NSBeep(); return NO; }
    if (phrase->name.empty()) phrase->name = "Captured phrase";
    self.statusLabel.stringValue = [NSString stringWithFormat:
        @"CAPTURED T%02lu · ROWS %03lu–%03lu",
        static_cast<unsigned long>(track + 1u),
        static_cast<unsigned long>(firstRow + 1u),
        static_cast<unsigned long>(lastRow + 1u)];
    [self phraseEdited];
    [self reloadModel];
    return YES;
}

- (BOOL)placeAtTrack:(std::size_t)track row:(std::size_t)row
    merge:(BOOL)merge
{
    if (!self.trackerState || self.trackerState->songPlaybackActive
        || track >= self.trackerState->session.pattern.tracks.size()) return NO;
    PhraseDefinition* phrase = [self selectedPhrase];
    auto candidate = self.trackerState->session.pattern;
    if (!s3g::tracker::placePhrase(candidate, track, *phrase, row,
            merge ? PhrasePlacementMode::MergeIntoEmpty
                  : PhrasePlacementMode::Replace)) { NSBeep(); return NO; }
    candidate.visibleRows = std::max(candidate.visibleRows, row + phrase->length);
    self.trackerState->session.pattern = std::move(candidate);
    self.trackerState->lastPlacedPhrase = self.trackerState->selectedPhrase;
    self.statusLabel.stringValue = [NSString stringWithFormat:
        @"COPIED P%02lu TO T%02lu · ROW %03lu",
        static_cast<unsigned long>(self.trackerState->selectedPhrase + 1u),
        static_cast<unsigned long>(track + 1u),
        static_cast<unsigned long>(row + 1u)];
    [self phraseEdited];
    return YES;
}

- (void)previewPressed:(id)sender
{
    (void)sender;
    PhraseDefinition* phrase = [self selectedPhrase];
    if (!phrase || !self.trackerCallbacks
        || !self.trackerCallbacks->previewPitchSequence) return;
    std::vector<PitchPreviewEvent> events;
    float velocity = 100.0f / 127.0f;
    std::size_t firstAudibleRow = phrase->length;
    for (std::size_t row = 0u; row < phrase->length; ++row) {
        const auto& note = phrase->notes[row];
        const auto* library = note.state == NoteCellState::Burst
            ? phraseBurstLibrary(self.trackerState, note.burstBankId) : nullptr;
        if (note.state == NoteCellState::Note
            || (library && note.note < library->bursts.size()
                && !library->bursts[note.note].empty())) {
            firstAudibleRow = row;
            break;
        }
    }
    for (std::size_t row = 0u; row < phrase->length; ++row) {
        if (row < phrase->velocities.size()) {
            const auto& value = phrase->velocities[row];
            if (value.state == ValueCellState::Value)
                velocity = std::clamp(value.normalized, 0.0f, 1.0f);
        }
        if (row >= phrase->notes.size()
            || (phrase->notes[row].state != NoteCellState::Note
                && phrase->notes[row].state != NoteCellState::Burst)) continue;
        const auto& note = phrase->notes[row];
        if (note.state == NoteCellState::Burst) {
            const auto* library = phraseBurstLibrary(
                self.trackerState, note.burstBankId);
            if (!library || note.note >= library->bursts.size())
                continue;
            const auto& burst = library->bursts[note.note];
            for (std::size_t eventIndex = 0u;
                 eventIndex < burst.eventCount; ++eventIndex) {
                const auto& event = burst.events[eventIndex];
                events.push_back({
                    static_cast<uint16_t>(row - firstAudibleRow),
                    event.note, event.velocity, event.gatePercent,
                    event.position,
                });
            }
            continue;
        }
        for (std::size_t voice = 0u; voice < note.noteVoiceCount(); ++voice) {
            uint8_t gatePercent = 70u;
            if (row < phrase->gates.size()) {
                const auto gate = phrase->gates[row].gateVoice(voice);
                if (gate.mode == GateVoiceMode::Rows)
                    gatePercent = static_cast<uint8_t>(std::clamp(
                        std::lround(gate.rows * 100.0f), 1l, 100l));
                else if (gate.mode == GateVoiceMode::Tie) gatePercent = 100u;
            }
            events.push_back({ static_cast<uint16_t>(row - firstAudibleRow),
                note.noteVoice(voice), static_cast<uint8_t>(std::lround(
                    velocity * 127.0f)), gatePercent });
        }
    }
    if (events.empty()) { NSBeep(); return; }
    const uint8_t channel = std::clamp<uint8_t>(
        phrase->previewMidiChannel, 1u, 16u);
    const double projectBpm = self.trackerState->hostBpm > 0.0
        ? self.trackerState->hostBpm
        : self.trackerState->session.transport.bpm;
    self.trackerCallbacks->previewPitchSequence(events, channel,
        projectBpm,
        self.trackerState->session.transport.ticksPerBeat);
    [self stopPhrasePreview];
    self.previewPhraseSlot = self.trackerState->selectedPhrase;
    self.previewPlayheadRow = static_cast<NSInteger>(firstAudibleRow);
    self.previewLastRow = static_cast<NSInteger>(phrase->length - 1u);
    [self.grid setNeedsDisplay:YES];
    [self.grid scrollRectToVisible:NSMakeRect(0.0,
        30.0 + static_cast<CGFloat>(firstAudibleRow) * 22.0,
        NSWidth(self.grid.bounds), 22.0)];
    const double rowSeconds = 60.0 / (std::max(1.0, projectBpm)
        * static_cast<double>(std::clamp<uint32_t>(
            self.trackerState->session.transport.ticksPerBeat, 1u, 96u)));
    __weak S3GTrackerPhraseView* weakSelf = self;
    self.previewTimer = [NSTimer timerWithTimeInterval:rowSeconds
        repeats:YES block:^(NSTimer* timer) {
            S3GTrackerPhraseView* owner = weakSelf;
            if (!owner || !owner.trackerState
                || owner.trackerState->playing
                || owner.trackerState->selectedPhrase
                    != owner.previewPhraseSlot
                || owner.previewPlayheadRow >= owner.previewLastRow) {
                [timer invalidate];
                if (owner) {
                    owner.previewTimer = nil;
                    owner.previewPlayheadRow = -1;
                    owner.previewLastRow = -1;
                    [owner.grid setNeedsDisplay:YES];
                }
                return;
            }
            ++owner.previewPlayheadRow;
            [owner.grid setNeedsDisplay:YES];
            [owner.grid scrollRectToVisible:NSMakeRect(0.0,
                30.0 + static_cast<CGFloat>(owner.previewPlayheadRow) * 22.0,
                NSWidth(owner.grid.bounds), 22.0)];
        }];
    [[NSRunLoop mainRunLoop] addTimer:self.previewTimer
        forMode:NSRunLoopCommonModes];
}

- (void)stopPhrasePreview
{
    [self.previewTimer invalidate];
    self.previewTimer = nil;
    self.previewPlayheadRow = -1;
    self.previewLastRow = -1;
    [self.grid setNeedsDisplay:YES];
}

@end
