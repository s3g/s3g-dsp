#import <Cocoa/Cocoa.h>

#import "s3g_tracker_controls.h"
#import "s3g_tracker_phrase_view.h"
#import "s3g_tracker_workspace.h"

#include "s3g_tracker_workspace_layout.h"
#include "s3g_tracker_grid_selection.h"
#include "s3g_gui_layout.h"

#include "s3g/tracker/fx_catalog.h"
#include "s3g/tracker/geometry_edit.h"
#include "s3g/tracker/command.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

@interface NSView (S3GTrackerGridTestAccess)
- (void)laneMidiChannelSelected:(NSMenuItem*)sender;
- (void)beginTrackNameEditingForTrack:(std::size_t)track rect:(NSRect)rect;
- (NSMenu*)sequenceActionMenuForTrack:(std::size_t)track
    row:(std::size_t)row field:(std::size_t)field;
- (void)sequenceActionSelected:(NSMenuItem*)sender;
- (NSMenu*)sequenceConditionMenuForTrack:(std::size_t)track
    row:(std::size_t)row field:(std::size_t)field;
- (NSMenu*)noteMenuForTrack:(std::size_t)track row:(std::size_t)row;
- (NSMenu*)velocityMenuForTrack:(std::size_t)track row:(std::size_t)row;
- (void)sequenceConditionSelected:(NSMenuItem*)sender;
- (NSMenu*)phraseSequenceActionMenuForField:(std::size_t)field
    row:(std::size_t)row;
- (void)phraseSequenceActionSelected:(NSMenuItem*)sender;
- (NSMenu*)phraseSequenceConditionMenuForField:(std::size_t)field
    row:(std::size_t)row;
- (void)phraseSequenceConditionSelected:(NSMenuItem*)sender;
- (BOOL)handleGridKeyEvent:(NSEvent*)event;
- (s3g::tracker::app::GridSelectionRange)effectivePhraseSelection;
- (void)phraseCopy:(id)sender;
- (void)phraseCut:(id)sender;
- (void)phrasePaste:(id)sender;
- (NSString*)displayedPatternId;
- (NSUInteger)displayedLaneCount;
- (NSUInteger)displayedMutedLaneCount;
- (NSUInteger)displayedVisibleRowCount;
- (NSInteger)displayedNoteNumberAtLane:(std::size_t)lane
    row:(std::size_t)row;
- (void)beginGridSelectionAtTrack:(std::size_t)track
    field:(std::size_t)field row:(std::size_t)row page:(std::size_t)page;
- (void)extendGridSelectionToTrack:(std::size_t)track
    field:(std::size_t)field row:(std::size_t)row;
- (BOOL)applyCellText:(NSString*)source toTrack:(s3g::tracker::Track&)track
    row:(std::size_t)row page:(std::size_t)page field:(std::size_t)field;
- (NSString*)cellTextForTrack:(std::size_t)trackIndex
    row:(std::size_t)row page:(std::size_t)page field:(std::size_t)field;
- (NSString*)clipboardTextForTrack:(std::size_t)trackIndex
    row:(std::size_t)row page:(std::size_t)page field:(std::size_t)field;
- (void)trackerCopy:(id)sender;
- (void)trackerCut:(id)sender;
- (void)trackerPaste:(id)sender;
- (void)splitSelectedNoteColumnByPitch:(NSMenuItem*)sender;
- (void)mergeSelectedNoteLanes:(NSMenuItem*)sender;
- (BOOL)extendGridSelectionInColumnToTrack:(std::size_t)track
    field:(std::size_t)field row:(std::size_t)row;
- (s3g::tracker::app::GridSelectionRange)effectiveGridSelection;
- (void)selectWholeRowsFrom:(std::size_t)anchor to:(std::size_t)focus;
- (BOOL)isWholeRowSelected:(std::size_t)row;
- (NSPoint)geometryCenter;
- (CGFloat)ringRadiusForLane:(std::size_t)lane;
- (BOOL)selectedRingLane:(std::size_t*)lane radius:(CGFloat*)radius;
- (NSPoint)geometryPointAtRadius:(CGFloat)radius angle:(CGFloat)angle;
- (NSRect)canvasRect;
- (NSRect)laneCyclePanelRect;
- (NSRect)editPanelRect;
- (NSRect)viewPanelRect;
- (NSRect)laneMenuBoxRect;
- (NSRect)directionMenuBoxRect;
- (NSRect)viewMenuBoxRect;
- (NSRect)linkVelocityLengthToggleRect;
- (NSRect)revealHeaderButtonRect;
- (NSRect)fitBurstGatesHeaderButtonRect;
- (NSRect)burstPreviewHeaderButtonRect;
- (NSRect)pitchPreviewHeaderButtonRect;
- (NSRect)pitchTransposeSliderTrack;
- (NSRect)pitchInvertToggleRect;
- (NSRect)pitchReverseToggleRect;
- (NSString*)pitchSelectedPointFlagText;
- (NSRect)pitchGraphRect;
- (NSRect)pitchIntervalGraphRect;
- (NSPoint)pitchMapPointForAssignmentAtIndex:(std::size_t)index
    original:(BOOL)original;
- (NSPoint)pitchMapPointForAssignmentAtIndex:(std::size_t)index
    original:(BOOL)original interval:(BOOL)interval;
- (void)updatePitchMapPointAtPoint:(NSPoint)point;
- (NSRect)lengthSliderTrack;
- (NSRect)defaultNoteSliderTrack;
- (NSRect)rotateSliderTrack;
- (NSRect)densitySliderTrack;
- (BOOL)handleToolboxClickAtPoint:(NSPoint)point;
- (void)syncToolboxControls;
- (void)openGeometryMenu:(NSInteger)menu;
- (void)applyGeometryMenuSelection:(NSInteger)index;
- (NSInteger)selectedIndexForGeometryMenu:(NSInteger)menu;
- (void)freezePitchPreviewForManualEditing;
- (NSUInteger)allStepsUnderlayNodeCount;
- (NSPoint)rotateHandlePoint;
- (NSPoint)densityHandlePoint;
- (BOOL)revealBeadAtPoint:(NSPoint)point;
- (void)selectLane:(std::size_t)lane;
- (void)selectBurstSlot:(std::size_t)slot;
- (void)openPitchMapFirstRow:(std::size_t)firstRow
    lastRow:(std::size_t)lastRow;
- (void)applyCurrentPitchMap;
- (NSArray<NSString*>*)itemsForGeometryMenu:(NSInteger)menu;
- (NSRect)dropdownRectForGeometryMenu:(NSInteger)menu;
- (NSRect)pitchGraphRect;
- (NSRect)burstMatrixRect;
- (NSRect)burstMatrixRowRect:(std::size_t)row;
- (NSRect)burstMatrixCellRect:(std::size_t)row field:(NSInteger)field;
- (BOOL)beginBurstCanvasGestureAtPoint:(NSPoint)point;
- (void)updateBurstMatrixGestureAtPoint:(NSPoint)point;
- (BOOL)beginSliderGestureAtPoint:(NSPoint)point;
- (void)updateSliderGestureAtPoint:(NSPoint)point;
- (BOOL)beginShapeGestureAtPoint:(NSPoint)point;
- (void)updateShapeGestureAtPoint:(NSPoint)point;
- (void)finishGeometryGesture;
- (NSRect)sliderTrackRect;
- (NSRect)valueTextRect;
- (NSRect)pinnedRectForGridRect:(NSRect)gridRect;
@end

namespace {

int failures = 0;

void check(bool condition, const char* message)
{
    if (condition) return;
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
}

bool near(CGFloat actual, CGFloat expected, CGFloat tolerance = 1.0) noexcept
{
    return std::abs(actual - expected) <= tolerance;
}

NSButton* descendantButton(NSView* root, NSString* title)
{
    for (NSView* view in root.subviews) {
        if ([view isKindOfClass:NSButton.class]
            && [static_cast<NSButton*>(view).title isEqualToString:title])
            return static_cast<NSButton*>(view);
        if (NSButton* match = descendantButton(view, title)) return match;
    }
    return nil;
}

NSEvent* keyEvent(NSWindow* window, NSString* characters,
    unsigned short keyCode, NSEventModifierFlags modifiers)
{
    return [NSEvent keyEventWithType:NSEventTypeKeyDown
        location:NSZeroPoint modifierFlags:modifiers timestamp:0.0
        windowNumber:window.windowNumber context:nil characters:characters
        charactersIgnoringModifiers:characters isARepeat:NO keyCode:keyCode];
}

NSEvent* mouseDownEvent(NSWindow* window, NSPoint location,
    NSInteger clickCount)
{
    return [NSEvent mouseEventWithType:NSEventTypeLeftMouseDown
        location:location modifierFlags:0u timestamp:0.0
        windowNumber:window.windowNumber context:nil eventNumber:1
        clickCount:clickCount pressure:1.0];
}

NSEvent* mouseEvent(NSWindow* window, NSEventType type, NSPoint location,
    NSEventModifierFlags modifiers, NSInteger clickCount = 1)
{
    return [NSEvent mouseEventWithType:type location:location
        modifierFlags:modifiers timestamp:0.0
        windowNumber:window.windowNumber context:nil eventNumber:1
        clickCount:clickCount pressure:1.0];
}

void seedTracks(s3g::tracker::app::TrackerViewState& state)
{
    state.session.pattern.name = "P01";
    state.session.pattern.visibleRows = 64u;
    state.session.pattern.tracks.resize(12u);
    for (std::size_t lane = 0u;
         lane < state.session.pattern.tracks.size(); ++lane) {
        auto& track = state.session.pattern.tracks[lane];
        track.name = "TRACK " + std::to_string(lane + 1u);
        track.notes.resize(64u, s3g::tracker::NoteCell::rest());
        track.velocities.resize(64u,
            s3g::tracker::ValueCell::defaultValue());
        track.noteColumn.length = 64u;
        track.velocityColumn.length = 64u;
    }
    state.session.pattern.tracks[0u].notes[0u]
        = s3g::tracker::NoteCell::withNote(60u);

    auto* first = state.patternBank.findEntry(
        state.patternBank.activePatternId);
    if (!first) return;
    first->pattern = state.session.pattern;
    first->pattern.name = "MAIN";
    s3g::tracker::PatternBankEntry second = *first;
    second.id = "A02";
    second.pattern.name = "BREAK";
    second.pattern.visibleRows = 32u;
    second.pattern.tracks.resize(2u);
    second.pattern.tracks[0u].name = "SONG LEAD";
    second.pattern.tracks[0u].notes[0u]
        = s3g::tracker::NoteCell::withNote(67u);
    second.pattern.tracks[1u].noteColumn.muted = true;
    state.patternBank.entries.push_back(std::move(second));
}

} // namespace

int main()
{
    @autoreleasepool {
        [NSApplication sharedApplication];
        s3g::tracker::app::TrackerViewState state;
        s3g::tracker::app::WorkspaceCallbacks callbacks;
        seedTracks(state);
        state.midiStepInputAvailable = true;
        std::string selectedPattern;
        int addPatternRequests = 0;
        int renamePatternRequests = 0;
        int deletePatternRequests = 0;
        int patternChangeRequests = 0;
        int transportChangeRequests = 0;
        int fillChangeRequests = 0;
        int restartRequests = 0;
        int trackResyncRequests = 0;
        int stepRecordModeRequests = 0;
        int recordTrackRequests = 0;
        int viewPreferenceRequests = 0;
        int trackerRevealRequests = 0;
        int burstPreviewRequests = 0;
        int pitchPreviewRequests = 0;
        int patternPreviewRequests = 0;
        int patternPreviewClearRequests = 0;
        int patternVariantRequests = 0;
        int phraseLibraryExportRequests = 0;
        s3g::tracker::Pattern previewedPattern;
        s3g::tracker::Pattern createdVariant;
        s3g::tracker::BurstDefinition previewedBurst;
        std::vector<s3g::tracker::PitchPreviewEvent> previewedPitch;
        uint8_t previewedChannel = 0u;
        double previewedBpm = 0.0;
        uint32_t previewedTicksPerBeat = 0u;
        std::vector<std::string> commands;
        std::size_t resyncedTrack = s3g::tracker::kMaximumTrackCount;
        callbacks.selectPattern = [&](const std::string& patternId) {
            selectedPattern = patternId;
            (void)state.patternBank.selectPattern(patternId);
        };
        callbacks.addPattern = [&](bool) { ++addPatternRequests; };
        callbacks.renamePattern = [&] { ++renamePatternRequests; };
        callbacks.deletePattern = [&] { ++deletePatternRequests; };
        callbacks.patternChanged = [&] { ++patternChangeRequests; };
        callbacks.transportChanged = [&] { ++transportChangeRequests; };
        callbacks.fillChanged = [&](bool active) {
            ++fillChangeRequests;
            state.fillActive = active;
        };
        callbacks.restartPlayback = [&] { ++restartRequests; };
        callbacks.resyncTrack = [&](std::size_t track) {
            ++trackResyncRequests;
            resyncedTrack = track;
        };
        callbacks.executeCommand = [&](const std::string& command) {
            commands.push_back(command);
        };
        callbacks.midiStepRecordModeChanged = [&](auto mode) {
            ++stepRecordModeRequests;
            state.midiStepRecordMode = mode;
        };
        callbacks.midiRecordTrackChanged = [&](std::size_t track) {
            ++recordTrackRequests;
            state.midiRecordTrack = track;
        };
        callbacks.viewPreferencesChanged = [&] {
            ++viewPreferenceRequests;
        };
        callbacks.showTrackerPage = [&] { ++trackerRevealRequests; };
        callbacks.exportPhraseLibraryAssetPack = [&] {
            ++phraseLibraryExportRequests;
        };
        callbacks.previewBurst = [&](const s3g::tracker::BurstDefinition& burst,
            uint8_t channel, double bpm, uint32_t ticksPerBeat) {
            ++burstPreviewRequests;
            previewedBurst = burst;
            previewedChannel = channel;
            previewedBpm = bpm;
            previewedTicksPerBeat = ticksPerBeat;
        };
        callbacks.previewPitchSequence = [&](
            const std::vector<s3g::tracker::PitchPreviewEvent>& events,
            uint8_t channel, double bpm, uint32_t ticksPerBeat) {
            ++pitchPreviewRequests;
            previewedPitch = events;
            previewedChannel = channel;
            previewedBpm = bpm;
            previewedTicksPerBeat = ticksPerBeat;
        };
        callbacks.previewPattern = [&](const s3g::tracker::Pattern& pattern) {
            ++patternPreviewRequests;
            previewedPattern = pattern;
        };
        callbacks.clearPatternPreview = [&] {
            ++patternPreviewClearRequests;
        };
        callbacks.createPatternVariant = [&](const s3g::tracker::Pattern& pattern) {
            ++patternVariantRequests;
            createdVariant = pattern;
        };

        S3GTrackerWorkspaceController* controller =
            [[S3GTrackerWorkspaceController alloc]
                initWithState:&state callbacks:&callbacks];
        NSWindow* window = [[NSWindow alloc]
            initWithContentRect:NSMakeRect(0.0, 0.0, 1320.0, 840.0)
            styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable)
            backing:NSBackingStoreBuffered defer:NO];
        window.contentMinSize = NSMakeSize(760.0, 620.0);
        window.contentViewController = controller;
        [window makeKeyAndOrderFront:nil];
        [window setContentSize:NSMakeSize(760.0, 620.0)];
        NSView* root = controller.view;
        [root layoutSubtreeIfNeeded];
        check(near(NSWidth(window.contentView.bounds), 760.0)
                && near(NSWidth(window.frame), 760.0),
            "real workspace window should reach its 760-point minimum width");

        NSScrollView* grid = [controller valueForKey:@"gridScroll"];
        NSView* trackerGrid = grid.documentView;
        NSView* rowGutter = [controller valueForKey:@"rowGutterView"];
        check(near(grid.magnification,
                s3g::tracker::app::kTrackerDefaultMagnification, 0.001),
            "tracker lanes should initialize at literal 100 percent zoom");
        NSScrollView* transport = [controller valueForKey:@"transportScroll"];
        S3GTrackerToolboxView* patternPanel = [controller
            valueForKey:@"patternPanel"];
        S3GTrackerToolboxView* transportPanel = [controller
            valueForKey:@"transportPanel"];
        S3GTrackerToolboxView* inputViewPanel = [controller
            valueForKey:@"inputViewPanel"];
        NSStackView* patternPrimaryControls = [controller
            valueForKey:@"patternPrimaryControls"];
        NSStackView* transportPrimaryControls = [controller
            valueForKey:@"transportPrimaryControls"];
        NSStackView* inputPrimaryControls = [controller
            valueForKey:@"inputPrimaryControls"];
        NSPopUpButton* patternPopup = [controller valueForKey:@"patternPopup"];
        NSButton* createPatternButton = [controller
            valueForKey:@"createPatternButton"];
        NSButton* duplicatePatternButton = [controller
            valueForKey:@"duplicatePatternButton"];
        NSButton* renamePatternButton = [controller
            valueForKey:@"renamePatternButton"];
        NSButton* deletePatternButton = [controller
            valueForKey:@"deletePatternButton"];
        NSButton* sequenceColumnsButton = [controller
            valueForKey:@"sequenceColumnsButton"];
        NSButton* trackAddButton = [controller valueForKey:@"trackAddButton"];
        NSButton* trackRemoveButton = [controller
            valueForKey:@"trackRemoveButton"];
        NSButton* undoButton = [controller valueForKey:@"undoButton"];
        NSButton* redoButton = [controller valueForKey:@"redoButton"];
        NSButton* noteDisplayButton = [controller
            valueForKey:@"noteDisplayButton"];
        NSPopUpButton* stepJumpPopup = [controller
            valueForKey:@"stepJumpPopup"];
        NSButton* zoomOutButton = [controller valueForKey:@"zoomOutButton"];
        NSButton* zoomActualButton = [controller
            valueForKey:@"zoomActualButton"];
        NSButton* zoomInButton = [controller valueForKey:@"zoomInButton"];
        NSButton* playButton = [controller valueForKey:@"playButton"];
        NSButton* fillButton = [controller valueForKey:@"fillButton"];
        NSPopUpButton* midiStepRecordPopup = [controller
            valueForKey:@"midiStepRecordPopup"];
        NSPopUpButton* midiRecordTrackPopup = [controller
            valueForKey:@"midiRecordTrackPopup"];
        NSPopUpButton* tempoScalePopup = [controller
            valueForKey:@"tempoScalePopup"];
        S3GTrackerSwingSlider* swingField = [controller
            valueForKey:@"swingField"];
        S3GTrackerPopupButton* gateField = [controller
            valueForKey:@"gateField"];
        S3GTrackerPopupButton* loopStartField = [controller
            valueForKey:@"loopStartField"];
        S3GTrackerPopupButton* loopEndField = [controller
            valueForKey:@"loopEndField"];
        NSButton* restartButton = [controller valueForKey:@"restartButton"];
        NSView* envelope = [controller valueForKey:@"envelopeView"];
        NSView* consoleOutput = [controller consolePageView];
        S3GTrackerToolboxView* consoleToolbox = [controller
            valueForKey:@"consoleOutputPanel"];
        NSTextField* trackerLiveCode = [controller valueForKey:@"consoleInput"];
        NSTextField* consoleLiveCode = [controller
            valueForKey:@"consolePageInput"];
        NSView* geometryPage = [controller geometryPageView];
        NSView* burstPage = [controller burstPageView];
        NSView* phrasePage = [controller phrasePageView];
        NSView* reshapePage = [controller reshapePageView];
        NSView* warpPage = [controller warpPageView];
        id phraseController = [controller valueForKey:@"phraseView"];
        S3GTrackerPopupButton* phraseLength = [phraseController
            valueForKey:@"lengthPopup"];
        S3GTrackerPopupButton* phraseLibrary = [phraseController
            valueForKey:@"libraryPopup"];
        S3GTrackerPopupButton* phrasePreviewChannel = [phraseController
            valueForKey:@"previewChannelPopup"];
        NSView* phraseGrid = [phraseController valueForKey:@"grid"];
        NSPopUpButton* geometryViewMode = [geometryPage
            valueForKey:@"viewModePopup"];
        NSPopUpButton* burstViewMode = [burstPage
            valueForKey:@"viewModePopup"];
        NSPopUpButton* geometryLanePopup = [geometryPage
            valueForKey:@"lanePopup"];
        NSPopUpButton* geometryDirectionPopup = [geometryPage
            valueForKey:@"directionPopup"];
        NSArray<NSButton*>* geometryTools = [geometryPage
            valueForKey:@"toolButtons"];
        NSButton* geometryRotateBack = [geometryPage
            valueForKey:@"rotateBackButton"];
        NSButton* geometryRotateForward = [geometryPage
            valueForKey:@"rotateForwardButton"];
        NSButton* geometryDensityDown = [geometryPage
            valueForKey:@"densityDownButton"];
        NSButton* geometryDensityUp = [geometryPage
            valueForKey:@"densityUpButton"];
        NSButton* geometryReverse = [geometryPage
            valueForKey:@"reverseButton"];
        NSButton* geometryReflect = [geometryPage
            valueForKey:@"reflectButton"];
        NSArray<NSButton*>* geometryMorphButtons = [geometryPage
            valueForKey:@"morphButtons"];
        NSPopUpButton* geometryMorphTarget = [geometryPage
            valueForKey:@"morphTargetPopup"];
        NSButton* geometryReveal = [geometryPage valueForKey:@"revealButton"];

        check(near(NSWidth(grid.frame), NSWidth(root.bounds)),
            "compact tracker should use the full embedded page width");
        check(near(NSHeight(envelope.frame), 111.6),
            "compact AppKit layout should shrink the envelope");
        check(consoleOutput && geometryPage && phrasePage && reshapePage && warpPage
                && consoleOutput != geometryPage
                && geometryPage != phrasePage && phrasePage != reshapePage
                && reshapePage != warpPage,
            "console, geometry, phrase, reshape, and warp modules should expose distinct pages");
        NSWindow* phraseTestWindow = [[NSWindow alloc]
            initWithContentRect:NSMakeRect(0.0, 0.0, 900.0, 660.0)
            styleMask:NSWindowStyleMaskTitled backing:NSBackingStoreBuffered
            defer:NO];
        phrasePage.frame = phraseTestWindow.contentView.bounds;
        phraseTestWindow.contentView = phrasePage;
        [phraseTestWindow makeKeyAndOrderFront:nil];
        [phrasePage layoutSubtreeIfNeeded];
        S3GTrackerToolboxView* phraseEditorPanel = [phraseController
            valueForKey:@"editorPanel"];
        S3GTrackerToolboxView* phraseLibraryPanel = [phraseController
            valueForKey:@"libraryPanel"];
        S3GTrackerToolboxView* phraseAuditionPanel = [phraseController
            valueForKey:@"auditionPanel"];
        S3GTrackerToolboxView* phrasePlacementPanel = [phraseController
            valueForKey:@"placementPanel"];
        NSScrollView* phraseGridScroll = [phraseController
            valueForKey:@"gridScroll"];
        const auto expectedPhraseLayout =
            s3g::gui_layout::trackerGeometryFamilyLayout({
                static_cast<double>(NSWidth(phrasePage.bounds)),
                static_cast<double>(NSHeight(phrasePage.bounds)),
            }, 1u, 7u, false);
        check([phraseEditorPanel.toolboxTitle
                    isEqualToString:
                        @"PHRASE TRACKER  /  PROJECT MIDI PHRASE"]
                && [phraseLibraryPanel.toolboxTitle
                    isEqualToString:@"PHRASE LIBRARY"]
                && [phraseAuditionPanel.toolboxTitle
                    isEqualToString:@"AUDITION"]
                && [phrasePlacementPanel.toolboxTitle
                    isEqualToString:@"TRACKER BRIDGE"]
                && near(NSMinX(phraseEditorPanel.frame),
                    expectedPhraseLayout.fieldPanel.x)
                && near(NSWidth(phraseEditorPanel.frame),
                    expectedPhraseLayout.fieldPanel.width)
                && near(NSMinX(phraseLibraryPanel.frame),
                    expectedPhraseLayout.laneCycle.frame.x)
                && near(NSWidth(phraseLibraryPanel.frame),
                    expectedPhraseLayout.laneCycle.frame.width)
                && near(NSMinY(phraseGridScroll.frame),
                    s3g::gui_layout::kStandardMetrics.headerHeight),
            "Phrases should share the Burst left-workspace and stacked right-inspector geometry");
        NSButton* phraseImportPack = descendantButton(
            phrasePage, @"IMPORT PACK");
        NSButton* phraseExportOne = descendantButton(
            phrasePage, @"EXPORT ONE");
        NSButton* phraseExportAll = descendantButton(
            phrasePage, @"EXPORT ALL");
        NSButton* phraseCopyToLane = descendantButton(
            phrasePage, @"COPY TO LANE");
        const NSRect exportAllFrame = phraseExportAll
            ? [phraseExportAll.superview convertRect:phraseExportAll.frame
                toView:phrasePage] : NSZeroRect;
        const NSRect copyToLaneFrame = phraseCopyToLane
            ? [phraseCopyToLane.superview convertRect:phraseCopyToLane.frame
                toView:phrasePage] : NSZeroRect;
        check(phraseImportPack && phraseExportOne && phraseExportAll
                && NSWidth(phraseImportPack.frame) >= 109.0
                && NSWidth(phraseExportOne.frame) >= 109.0
                && NSWidth(phraseExportAll.frame) >= 109.0
                && std::abs(NSMidY(exportAllFrame)
                    - NSMidY(copyToLaneFrame)) >= 20.0,
            "Phrase pack actions should occupy a separate row with enough width for their complete titles");
        check(phraseLibrary.numberOfItems == 64u
                && phraseLength.numberOfItems == 63u
                && phrasePreviewChannel.numberOfItems == 16u
                && [[phrasePreviewChannel itemAtIndex:0u].title
                    isEqualToString:@"01"]
                && [[phraseLength itemAtIndex:0u].title
                    isEqualToString:@"2 ROWS"]
                && [[phraseLength itemAtIndex:61u].title
                    isEqualToString:@"63 ROWS"]
                && [[phraseLength itemAtIndex:62u].title
                    isEqualToString:@"64 ROWS"],
            "Phrase Library should expose 64 project slots and every even or odd length from 2 through 64");
        [phrasePreviewChannel selectItemAtIndex:9u];
        [phrasePreviewChannel sendAction:phrasePreviewChannel.action
            to:phrasePreviewChannel.target];
        check(state.phraseLibrary.phrases[0u].previewMidiChannel == 10u,
            "Phrase preview MIDI channel should be editable and stored with the phrase");
        [phraseGrid keyDown:keyEvent(phraseTestWindow, @"6", 22u, 0u)];
        NSTextField* phraseCellEditor = [phraseGrid valueForKey:@"editor"];
        check(phraseCellEditor != nil,
            "typing in a Phrase cell should open its standard keyboard editor");
        phraseCellEditor.stringValue = @"60";
        [phraseGrid performSelector:@selector(commitEditor:) withObject:nil];
        check(state.phraseLibrary.phrases[0u].notes[0u].state
                    == s3g::tracker::NoteCellState::Note
                && state.phraseLibrary.phrases[0u].notes[0u].note == 60u,
            "Phrase inline editing should write ordinary tracker cells");
        const NSPoint phraseDoubleClick = [phraseGrid convertPoint:
            NSMakePoint(80.0, 35.0) toView:nil];
        const NSPoint phraseRoundTrip = [phraseGrid convertPoint:
            phraseDoubleClick fromView:nil];
        check(near(phraseRoundTrip.x, 80.0)
                && near(phraseRoundTrip.y, 35.0),
            "Phrase double-click test point should round-trip through window coordinates");
        NSEvent* phraseDoubleDown = mouseDownEvent(phraseTestWindow,
            phraseDoubleClick, 2);
        [phraseGrid mouseDown:phraseDoubleDown];
        phraseCellEditor = [phraseGrid valueForKey:@"editor"];
        check(phraseDoubleDown.clickCount == 2 && phraseCellEditor != nil,
            "double-click should open the Phrase inline field");
        check(phraseCellEditor == nil || (phraseCellEditor.editable
                && phraseCellEditor.selectable),
            "Phrase double-click field should use Tracker's editable/selectable configuration");
        NSTextView* phraseFieldEditor = (NSTextView*)phraseCellEditor.currentEditor;
        check(phraseFieldEditor == nil
                || (phraseFieldEditor.selectedRange.location
                        == phraseCellEditor.stringValue.length
                    && phraseFieldEditor.selectedRange.length == 0u),
            "Phrase double-click should place an unselected caret after the existing value");
        [phraseGrid performSelector:@selector(commitEditor:) withObject:nil];
        state.phraseLibrary.phrases[0u].velocities[0u]
            = s3g::tracker::ValueCell::withValue(0.5f);
        [phraseGrid setValue:@0u forKey:@"selectedRow"];
        [phraseGrid setValue:@1u forKey:@"selectedField"];
        [phraseGrid performSelector:@selector(beginEditing) withObject:nil];
        phraseCellEditor = [phraseGrid valueForKey:@"editor"];
        check([phraseCellEditor.stringValue isEqualToString:@"0.500"],
            "Phrase VOL should display its stored normalized floating-point value");
        phraseCellEditor.stringValue = @"1";
        [phraseGrid performSelector:@selector(commitEditor:) withObject:nil];
        check(std::abs(state.phraseLibrary.phrases[0u].velocities[0u]
                    .normalized - 1.0f) < 0.0001f,
            "Phrase VOL entry should interpret both 0 and 1 as normalized endpoints");
        std::array<float, s3g::tracker::kMaximumNoteVoices>
            phrasePolyphonicVolumes {{
                0.100f, 0.200f, 0.300f, 0.400f,
                0.500f, 0.600f, 0.700f, 0.800f,
            }};
        state.phraseLibrary.phrases[0u].velocities[0u]
            = s3g::tracker::ValueCell::withValues(
                phrasePolyphonicVolumes, phrasePolyphonicVolumes.size());
        [phraseGrid setValue:@0u forKey:@"selectedRow"];
        [phraseGrid setValue:@1u forKey:@"selectedField"];
        [phraseGrid performSelector:@selector(beginEditing) withObject:nil];
        phraseCellEditor = [phraseGrid valueForKey:@"editor"];
        check([phraseCellEditor.stringValue
                    isEqualToString:
                        @"0.100+0.200+0.300+0.400+0.500+0.600+0.700+0.800"]
                && NSWidth(phraseCellEditor.frame) > 300.0
                && NSContainsRect(phraseGrid.visibleRect,
                    phraseCellEditor.frame),
            "double-click editing should expand a polyphonic Phrase VOL cell to reveal every voice inside the visible grid");
        [phraseGrid performSelector:@selector(commitEditor:) withObject:nil];

        auto& editedPhrase = state.phraseLibrary.phrases[0u];
        editedPhrase.notes[0u] = s3g::tracker::NoteCell::withNote(60u);
        editedPhrase.notes[1u] = s3g::tracker::NoteCell::withNote(64u);
        editedPhrase.velocities[0u]
            = s3g::tracker::ValueCell::withValue(0.75f);
        editedPhrase.velocities[1u]
            = s3g::tracker::ValueCell::withValue(0.25f);
        [phraseController performSelector:@selector(exportAllPacksPressed:)
            withObject:nil];
        check(phraseLibraryExportRequests == 1,
            "Phrase EXPORT ALL should dispatch the complete project Phrase Library");
        const int phraseChangesBeforeDuplicate = patternChangeRequests;
        [phraseController performSelector:@selector(duplicatePressed:)
            withObject:nil];
        check(state.selectedPhrase == 1u
                && state.phraseLibrary.phrases[1u].name == "PHRASE COPY"
                && state.phraseLibrary.phrases[1u].notes[0u].note == 60u
                && state.phraseLibrary.phrases[1u].notes[1u].note == 64u
                && state.phraseLibrary.phrases[1u].previewMidiChannel == 10u
                && patternChangeRequests == phraseChangesBeforeDuplicate + 1,
            "Phrase DUP should copy the complete selected Phrase into the first empty slot and select it");
        state.phraseLibrary.phrases[1u] = {};
        state.selectedPhrase = 0u;
        [phraseController reloadModel];
        const NSPoint phraseSelectionStart = [phraseGrid convertPoint:
            NSMakePoint(80.0, 35.0) toView:nil];
        const NSPoint phraseSelectionEnd = [phraseGrid convertPoint:
            NSMakePoint(250.0, 57.0) toView:nil];
        [phraseGrid mouseDown:mouseDownEvent(phraseTestWindow,
            phraseSelectionStart, 1)];
        [phraseGrid mouseDragged:mouseEvent(phraseTestWindow,
            NSEventTypeLeftMouseDragged, phraseSelectionEnd, 0u)];
        [phraseGrid mouseUp:mouseEvent(phraseTestWindow,
            NSEventTypeLeftMouseUp, phraseSelectionEnd, 0u)];
        auto phraseSelection = [phraseGrid effectivePhraseSelection];
        check(phraseSelection.firstField == 0u
                && phraseSelection.lastField == 1u
                && phraseSelection.firstRow == 0u
                && phraseSelection.lastRow == 1u,
            "Phrase drag selection should span the same rectangular cell ranges as Tracker");
        check([phraseGrid handleGridKeyEvent:keyEvent(phraseTestWindow,
                @"x", 7u, NSEventModifierFlagControl)],
            "Control-X should be owned by the Phrase mini tracker");
        check(editedPhrase.notes[0u].state
                    == s3g::tracker::NoteCellState::Rest
                && editedPhrase.notes[1u].state
                    == s3g::tracker::NoteCellState::Rest
                && editedPhrase.velocities[0u].state
                    == s3g::tracker::ValueCellState::Default
                && editedPhrase.velocities[1u].state
                    == s3g::tracker::ValueCellState::Default,
            "Phrase Cut should clear every cell in the selected rectangle");
        const NSPoint phrasePastePoint = [phraseGrid convertPoint:
            NSMakePoint(80.0, 123.0) toView:nil];
        [phraseGrid mouseDown:mouseDownEvent(phraseTestWindow,
            phrasePastePoint, 1)];
        [phraseGrid mouseUp:mouseEvent(phraseTestWindow,
            NSEventTypeLeftMouseUp, phrasePastePoint, 0u)];
        check([phraseGrid handleGridKeyEvent:keyEvent(phraseTestWindow,
                @"v", 9u, NSEventModifierFlagControl)],
            "Control-V should be owned by the Phrase mini tracker");
        check(editedPhrase.notes[4u].state
                    == s3g::tracker::NoteCellState::Note
                && editedPhrase.notes[4u].note == 60u
                && editedPhrase.notes[5u].note == 64u
                && std::abs(editedPhrase.velocities[4u].normalized - 0.75f)
                    < 0.0001f
                && std::abs(editedPhrase.velocities[5u].normalized - 0.25f)
                    < 0.0001f,
            "Phrase Paste should preserve a copied multi-row, multi-column shape and exact cell values");
        const NSPoint phraseShiftEnd = [phraseGrid convertPoint:
            NSMakePoint(250.0, 167.0) toView:nil];
        const NSPoint phraseShiftStart = [phraseGrid convertPoint:
            NSMakePoint(250.0, 123.0) toView:nil];
        [phraseGrid mouseDown:mouseDownEvent(phraseTestWindow,
            phraseShiftStart, 1)];
        [phraseGrid mouseUp:mouseEvent(phraseTestWindow,
            NSEventTypeLeftMouseUp, phraseShiftStart, 0u)];
        [phraseGrid mouseDown:mouseEvent(phraseTestWindow,
            NSEventTypeLeftMouseDown, phraseShiftEnd,
            NSEventModifierFlagShift)];
        phraseSelection = [phraseGrid effectivePhraseSelection];
        check(phraseSelection.firstField == 1u
                && phraseSelection.lastField == 1u
                && phraseSelection.firstRow == 4u
                && phraseSelection.lastRow == 6u,
            "Shift-click should extend a Phrase selection within one column");
        check([phraseGrid handleGridKeyEvent:keyEvent(phraseTestWindow,
                @"", 124u, NSEventModifierFlagControl
                    | NSEventModifierFlagShift)],
            "Control-Shift-arrow should extend Phrase selections from the keyboard");
        phraseSelection = [phraseGrid effectivePhraseSelection];
        check(phraseSelection.lastField == 2u,
            "Phrase keyboard selection should extend across adjacent columns");
        editedPhrase.notes[4u] = s3g::tracker::NoteCell::rest();
        editedPhrase.notes[5u] = s3g::tracker::NoteCell::rest();
        editedPhrase.velocities[4u]
            = s3g::tracker::ValueCell::defaultValue();
        editedPhrase.velocities[5u]
            = s3g::tracker::ValueCell::defaultValue();
        NSMenu* phraseSequenceMenu = [phraseGrid
            phraseSequenceActionMenuForField:2u row:0u];
        NSMenuItem* phraseConditionAction = nil;
        for (NSMenuItem* item in phraseSequenceMenu.itemArray) {
            NSDictionary* represented = item.representedObject;
            if ([represented[@"kind"] isEqualToString:@"action"]) {
                const auto index = [represented[@"action"] unsignedIntegerValue];
                const auto* action = s3g::tracker::sequencerAction(index);
                if (action && action->action
                        == s3g::tracker::SequencerAction::Condition) {
                    phraseConditionAction = item;
                    break;
                }
            }
        }
        NSMenuItem* phraseMidiControl = [phraseSequenceMenu
            itemWithTitle:@"MIDI CONTROL CHANGE"];
        check(phraseSequenceMenu != nil && phraseConditionAction != nil
                && phraseMidiControl.submenu.numberOfItems == 4u
                && phraseMidiControl.submenu.itemArray[0u]
                    .submenu.numberOfItems == 32u,
            "Phrase SEQ cells should expose the Tracker action and MIDI CC context menu");
        [phraseGrid phraseSequenceActionSelected:phraseConditionAction];
        const auto& phraseSeqPair = state.phraseLibrary.phrases[0u]
            .fxPairs[0u];
        check(phraseSeqPair.actions[0u].state
                    == s3g::tracker::FxActionCellState::Sequencer
                && phraseSeqPair.actions[0u].sequencerAction
                    == s3g::tracker::SequencerAction::Condition
                && s3g::tracker::sequencerConditionFromNormalized(
                    phraseSeqPair.values[0u].normalized)
                    == s3g::tracker::SequencerCondition::FirstOf2,
            "choosing CD in a Phrase SEQ menu should write the action and its default condition");
        NSMenu* phraseConditionMenu = [phraseGrid
            phraseSequenceConditionMenuForField:3u row:0u];
        NSMenuItem* phraseRowOdd = nil;
        for (NSMenuItem* item in phraseConditionMenu.itemArray) {
            NSDictionary* represented = item.representedObject;
            const auto index = [represented[@"condition"] unsignedIntegerValue];
            const auto* condition = s3g::tracker::sequencerCondition(index);
            if (condition && condition->condition
                    == s3g::tracker::SequencerCondition::RowOdd) {
                phraseRowOdd = item;
                break;
            }
        }
        [phraseGrid phraseSequenceConditionSelected:phraseRowOdd];
        check(phraseConditionMenu != nil && phraseRowOdd != nil
                && s3g::tracker::sequencerConditionFromNormalized(
                    state.phraseLibrary.phrases[0u].fxPairs[0u]
                        .values[0u].normalized)
                    == s3g::tracker::SequencerCondition::RowOdd,
            "right-clicking a Phrase CD value should expose and store named conditions");
        [phraseGrid setValue:@0u forKey:@"selectedRow"];
        [phraseGrid setValue:@0u forKey:@"selectedField"];
        [phraseGrid keyDown:keyEvent(window, @"\x7f", 51u, 0u)];
        check(state.phraseLibrary.phrases[0u].notes[0u].state
                == s3g::tracker::NoteCellState::Rest,
            "Delete should clear the selected Phrase cell");
        state.phraseLibrary.phrases[0u].notes[0u]
            = s3g::tracker::NoteCell::withNote(60u);
        state.phraseLibrary.phrases[0u].notes[1u]
            = s3g::tracker::NoteCell::withBurst(3u);
        state.phraseLibrary.phrases[0u].notes[2u]
            = s3g::tracker::NoteCell::withNote(64u);
        auto& phraseBurst = state.session.burstLibrary.bursts[3u];
        phraseBurst.name = "Phrase burst";
        phraseBurst.eventCount = 2u;
        phraseBurst.events[0u] = { 0u, 36u, 110u, 35u };
        phraseBurst.events[1u] = { 32768u, 38u, 90u, 30u };
        [phraseController performSelector:@selector(previewPressed:)
            withObject:nil];
        check(pitchPreviewRequests == 1
                && previewedChannel == 10u
                && previewedPitch.size() == 4u
                && previewedPitch[1u].row == 1u
                && previewedPitch[1u].position == 0u
                && previewedPitch[2u].row == 1u
                && previewedPitch[2u].position == 32768u
                && [[phraseController valueForKey:@"previewPlayheadRow"]
                    integerValue] == 0,
            "Phrase Preview should audition project Burst timing and start its visible playhead");
        NSTimer* phrasePreviewTimer = [phraseController
            valueForKey:@"previewTimer"];
        [phrasePreviewTimer fire];
        check([[phraseController valueForKey:@"previewPlayheadRow"]
                integerValue] == 1,
            "Phrase Preview playhead should advance one Tracker row per project tick");
        [phraseController performSelector:@selector(stopPhrasePreview)];
        pitchPreviewRequests = 0;
        previewedPitch.clear();
        const auto previousCaptureNote = state.session.pattern.tracks[0u]
            .notes[10u];
        const auto previousCaptureBurst = state.session.burstLibrary.bursts[7u];
        state.session.burstLibrary.bursts[7u].name = "Captured phrase roll";
        state.session.burstLibrary.bursts[7u].eventCount = 1u;
        state.session.burstLibrary.bursts[7u].events[0u]
            = { 16384u, 40u, 105u, 45u };
        state.session.pattern.tracks[0u].notes[10u]
            = s3g::tracker::NoteCell::withBurst(7u);
        state.selectedPhrase = 1u;
        [phraseController reloadModel];
        const BOOL capturedPhraseBurst = [phraseController captureTrack:0u
            firstRow:10u lastRow:11u];
        check(capturedPhraseBurst
                && state.phraseLibrary.phrases[1u].notes[0u].state
                    == s3g::tracker::NoteCellState::Burst
                && state.session.burstLibrary.bursts[7u].name
                    == "Captured phrase roll",
            "Tracker-selection Phrase capture should retain its project Burst reference");
        state.session.pattern.tracks[0u].notes[10u] = previousCaptureNote;
        state.session.burstLibrary.bursts[7u] = previousCaptureBurst;
        state.selectedPhrase = 0u;
        [phraseController reloadModel];
        phraseTestWindow.contentView = [[NSView alloc] initWithFrame:NSZeroRect];
        [phraseTestWindow orderOut:nil];
        [window makeKeyAndOrderFront:nil];
        [consoleOutput layoutSubtreeIfNeeded];
        check([consoleToolbox isKindOfClass:
                    NSClassFromString(@"S3GTrackerToolboxView")]
                && consoleToolbox.toolboxIndex == 0
                && [consoleToolbox.toolboxTitle
                    isEqualToString:@"CONSOLE / LIVE CODE"]
                && near(NSMinX(consoleToolbox.frame),
                    s3g::gui_layout::kTrackerPageHorizontalInset)
                && near(NSHeight(consoleOutput.bounds)
                        - NSMaxY(consoleToolbox.frame),
                    s3g::gui_layout::kTrackerPageContentTop)
                && near(NSMinY(consoleToolbox.frame),
                    s3g::gui_layout::kTrackerPageBottomInset),
            "Console should use the shared unnumbered toolbox header and page insets");
        check(trackerLiveCode != nil && consoleLiveCode != nil
                && trackerLiveCode != consoleLiveCode
                && [trackerLiveCode.accessibilityLabel
                    isEqualToString:@"Live command input"]
                && [consoleLiveCode.accessibilityLabel
                    isEqualToString:@"Console live command input"]
                && NSMinX(trackerLiveCode.frame) < 100.0,
            "Tracker and detachable Console pages should each expose Live Code entry, with the Tracker field using the compact label inset");
        consoleLiveCode.stringValue = @"aliases";
        [consoleLiveCode sendAction:consoleLiveCode.action
            to:consoleLiveCode.target];
        check(!commands.empty() && commands.back() == "aliases"
                && trackerLiveCode.stringValue.length == 0u
                && consoleLiveCode.stringValue.length == 0u,
            "Console-page Live Code should use the shared command history and executor");
        NSView* envelopePlaybackOverlay = [envelope
            valueForKey:@"playbackOverlay"];
        NSView* geometryPlaybackOverlay = [geometryPage
            valueForKey:@"playbackOverlay"];
        check(envelopePlaybackOverlay.wantsLayer
                && geometryPlaybackOverlay.wantsLayer,
            "animated envelope and geometry marks should use isolated overlays");
        NSArray<NSString*>* geometryMenuItems = [geometryPage
            itemsForGeometryMenu:3];
        check(geometryViewMode.numberOfItems == 8u
                && [[geometryViewMode itemAtIndex:0].title
                    isEqualToString:@"RING FIELD"]
                && [[geometryViewMode itemAtIndex:1].title
                    isEqualToString:@"ACTIVE PULSES"]
                && [[geometryViewMode itemAtIndex:2].title
                    isEqualToString:@"ALL STEPS UNDERLAY"]
                && [[geometryViewMode itemAtIndex:3].title
                    isEqualToString:@"PHASE SPOKES"]
                && [[geometryViewMode itemAtIndex:4].title
                    isEqualToString:@"LANE FOCUS"]
                && [[geometryViewMode itemAtIndex:5].title
                    isEqualToString:@"COMPOSITE RING"]
                && [[geometryViewMode itemAtIndex:6].title
                    isEqualToString:@"BURST EDITOR"]
                && [geometryViewMode itemAtIndex:6].hidden
                && [[geometryViewMode itemAtIndex:7].title
                    isEqualToString:@"PITCH MAP"]
                && geometryMenuItems.count == 7u
                && ![geometryMenuItems containsObject:@"BURST EDITOR"]
                && [[geometryPage valueForKey:@"geometryViewMode"]
                    integerValue] == 0,
            "Geometry should default to Ring Field and exclude the dedicated Burst workspace from its menu");
        burstPage.frame = NSMakeRect(0.0, 0.0, 1320.0, 780.0);
        [burstPage layoutSubtreeIfNeeded];
        check(!burstViewMode.enabled
                && [[burstPage valueForKey:@"burstLibraryOnly"] boolValue]
                && [[burstPage valueForKey:@"geometryViewMode"]
                    integerValue] == 6
                && [burstPage itemsForGeometryMenu:3].count == 0u
                && near(NSMinY([burstPage laneCyclePanelRect]),
                    s3g::gui_layout::kTrackerPageContentTop),
            "the dedicated Bursts page should omit View, fix its mode, and move Burst Library to the shared top inset");
        const NSRect burstLibraryDropdown = [burstPage
            dropdownRectForGeometryMenu:5];
        check([burstPage itemsForGeometryMenu:5].count == 64u
                && NSWidth(burstLibraryDropdown) >= 500.0
                && NSMinY(burstLibraryDropdown) >= 8.0
                && NSMaxY(burstLibraryDropdown)
                    <= NSHeight(burstPage.bounds) - 8.0,
            "the 64-slot Burst Library menu should use a bounded two-column layout");
        auto& matrixBurst = state.session.burstLibrary.bursts[0u];
        matrixBurst.name = "MATRIX TEST";
        matrixBurst.eventCount = 2u;
        matrixBurst.events[0u] = { 0u, 48u, 64u, 70u };
        matrixBurst.events[1u] = { 32768u, 52u, 80u, 75u };
        [burstPage selectBurstSlot:0u];
        NSTextField* burstNameField = [burstPage
            valueForKey:@"burstNameField"];
        NSButton* burstSaveButton = [burstPage
            valueForKey:@"burstSaveButton"];
        burstNameField.stringValue = @"AMEN PUSH";
        [burstSaveButton performClick:nil];
        check(!burstNameField.hidden && !burstSaveButton.hidden
                && [burstSaveButton.title isEqualToString:@"SAVE"]
                && matrixBurst.name == "AMEN PUSH",
            "Burst Library should use a persistent NAME field and explicit SAVE action like Warps");
        const NSRect previewBurstButton = [burstPage
            burstPreviewHeaderButtonRect];
        const BOOL previewed = [burstPage handleToolboxClickAtPoint:
            NSMakePoint(NSMidX(previewBurstButton), NSMidY(previewBurstButton))];
        check(previewed && burstPreviewRequests == 1
                && previewedBurst.name == "AMEN PUSH"
                && previewedChannel == 1u
                && near(previewedBpm, state.session.transport.bpm)
                && previewedTicksPerBeat
                    == state.session.transport.ticksPerBeat,
            "stopped Burst Preview should dispatch the selected phrase on the lane channel at the project clock");
        state.playing = true;
        (void)[burstPage handleToolboxClickAtPoint:
            NSMakePoint(NSMidX(previewBurstButton), NSMidY(previewBurstButton))];
        check(burstPreviewRequests == 1,
            "Burst Preview should remain unavailable while transport is running");
        state.playing = false;
        const NSRect burstMatrix = [burstPage burstMatrixRect];
        const NSRect burstVelocityCell = [burstPage
            burstMatrixCellRect:1u field:2];
        const BOOL beganVelocityEdit = [burstPage
            beginBurstCanvasGestureAtPoint:NSMakePoint(
                NSMidX(burstVelocityCell), NSMidY(burstVelocityCell))];
        [burstPage updateBurstMatrixGestureAtPoint:NSMakePoint(
            NSMaxX(burstVelocityCell) - 6.0, NSMidY(burstVelocityCell))];
        [burstPage finishGeometryGesture];
        const NSRect addRow = [burstPage burstMatrixRowRect:4u];
        const BOOL beganAdd = [burstPage beginBurstCanvasGestureAtPoint:
            NSMakePoint(NSMidX(addRow), NSMidY(addRow))];
        [burstPage finishGeometryGesture];
        check(NSWidth(burstMatrix) >= 360.0,
            "Burst workspace should expose a readable eight-row event matrix");
        check(beganVelocityEdit && matrixBurst.events[1u].velocity > 110u,
            "Burst matrix should directly edit an event value by dragging its cell");
        check(beganAdd && matrixBurst.eventCount == 5u,
            "Burst matrix should grow immediately when an unused event row is selected");
        const NSRect fitGatesButton = [burstPage
            fitBurstGatesHeaderButtonRect];
        const BOOL fitGates = [burstPage handleToolboxClickAtPoint:
            NSMakePoint(NSMidX(fitGatesButton), NSMidY(fitGatesButton))];
        check(fitGates
                && std::all_of(matrixBurst.events.begin(),
                    matrixBurst.events.begin() + matrixBurst.eventCount,
                    [](const s3g::tracker::BurstEvent& event) {
                        return event.gatePercent == 20u;
                    }),
            "Burst Substeps should fit gates between even onsets and the primary row boundary");
        const NSRect placeBurstButton = [burstPage revealHeaderButtonRect];
        const BOOL placedBurst = [burstPage handleToolboxClickAtPoint:
            NSMakePoint(NSMidX(placeBurstButton), NSMidY(placeBurstButton))];
        const BOOL placementFeedback = [[burstPage
            valueForKey:@"burstPlaceFeedbackActive"] boolValue];
        check(placedBurst && placementFeedback
                && state.session.pattern.tracks[0u].notes[0u].state
                    == s3g::tracker::NoteCellState::Burst,
            "placing a Burst should immediately latch visible success feedback");
        state.session.pattern.tracks[0u].notes[0u]
            = s3g::tracker::NoteCell::withNote(60u);
        patternChangeRequests = 0;
        check(geometryTools.count == 4u
                && [geometryTools[0u].title isEqualToString:@"SELECT"]
                && [geometryTools[1u].title isEqualToString:@"PAINT"]
                && [geometryTools[2u].title isEqualToString:@"ERASE"]
                && [geometryTools[3u].title isEqualToString:@"VELOCITY"]
                && geometryRotateBack && geometryRotateForward
                && geometryDensityDown && geometryDensityUp
                && geometryReverse && geometryReflect
                && geometryMorphButtons.count == 4u
                && geometryMorphTarget.numberOfItems == 2u
                && geometryReveal,
            "Geometry workspace should expose direct tools, two morph targets, and four morph amounts");
        const auto phaseBeforeGeometry =
            state.session.pattern.tracks[0u].noteColumn.phase;
        [geometryRotateForward performClick:nil];
        const BOOL rowRotatedForward =
            state.session.pattern.tracks[0u].notes[1u].state
                == s3g::tracker::NoteCellState::Note;
        [geometryRotateBack performClick:nil];
        [geometryReveal performClick:nil];
        check(rowRotatedForward
                && state.session.pattern.tracks[0u].notes[0u].state
                    == s3g::tracker::NoteCellState::Note
                && state.session.pattern.tracks[0u].noteColumn.phase
                    == phaseBeforeGeometry
                && patternChangeRequests == 2
                && trackerRevealRequests == 1,
            "Geometry row rotation should use shared pattern history without changing playback phase, and Reveal should bridge to Tracker");
        patternChangeRequests = 0;
        const auto originalGeometryTrack = state.session.pattern.tracks[0u];
        state.session.selectedTrack = 0u;
        geometryPage.frame = NSMakeRect(0.0, 0.0, 1320.0, 780.0);
        [geometryPage layoutSubtreeIfNeeded];
        const NSPoint geometryCenter = [geometryPage geometryCenter];
        std::size_t selectedRingLane = 0u;
        CGFloat selectedRingRadius = 0.0;
        const BOOL foundSelectedRing = [geometryPage
            selectedRingLane:&selectedRingLane radius:&selectedRingRadius];
        const NSPoint selectedRowBead = [geometryPage
            geometryPointAtRadius:selectedRingRadius
            angle:-static_cast<CGFloat>(M_PI_2)];
        const BOOL revealedBead = [geometryPage
            revealBeadAtPoint:selectedRowBead];
        check(foundSelectedRing && selectedRingLane == 0u && revealedBead
                && trackerRevealRequests == 2,
            "double-click bead targeting should reveal its exact Tracker location");
        const NSRect defaultNoteSlider = [geometryPage
            defaultNoteSliderTrack];
        const NSPoint defaultNote64 = NSMakePoint(
            NSMinX(defaultNoteSlider) + NSWidth(defaultNoteSlider)
                * 64.0 / 127.0,
            NSMidY(defaultNoteSlider));
        const BOOL beganDefaultNoteGesture = [geometryPage
            beginSliderGestureAtPoint:defaultNote64];
        const BOOL defaultNoteStayedPreviewOnly =
            s3g::tracker::laneDefaultNote(state.session, 0u) == 60u;
        [geometryPage finishGeometryGesture];
        check(beganDefaultNoteGesture && defaultNoteStayedPreviewOnly
                && s3g::tracker::laneDefaultNote(state.session, 0u) == 64u
                && state.session.pattern.tracks[0u].notes[0u].note == 64u
                && state.session.pattern.tracks[0u].notes[1u].state
                    == s3g::tracker::NoteCellState::Rest
                && patternChangeRequests == 1,
            "Geometry default-note drag should preview, replace explicit pitches, preserve symbols, and commit once");
        patternChangeRequests = 0;
        const NSPoint rotateHandle = [geometryPage rotateHandlePoint];
        check(std::hypot(rotateHandle.x - selectedRowBead.x,
                    rotateHandle.y - selectedRowBead.y) > 11.0,
            "rotate handle should remain clear of note-bead hit targets");
        const CGFloat rotateRadius = std::hypot(
            rotateHandle.x - geometryCenter.x,
            rotateHandle.y - geometryCenter.y);
        const NSPoint quarterTurn = NSMakePoint(
            geometryCenter.x + rotateRadius, geometryCenter.y);
        const BOOL beganRotateGesture = [geometryPage
            beginShapeGestureAtPoint:rotateHandle];
        [geometryPage updateShapeGestureAtPoint:quarterTurn];
        const BOOL rotationStayedPreviewOnly =
            state.session.pattern.tracks[0u].notes[0u].state
                == s3g::tracker::NoteCellState::Note;
        [geometryPage finishGeometryGesture];
        check(beganRotateGesture && rotationStayedPreviewOnly
                && state.session.pattern.tracks[0u].notes[16u].state
                    == s3g::tracker::NoteCellState::Note
                && state.session.pattern.tracks[0u].noteColumn.phase
                    == phaseBeforeGeometry
                && patternChangeRequests == 1,
            "dragging the rotate handle should preview and commit one quarter-turn of authored rows without moving playback phase");

        state.session.pattern.tracks[0u] = originalGeometryTrack;
        patternChangeRequests = 0;
        const auto densityBeforeGesture = s3g::tracker::geometryHitCount(
            state.session.pattern.tracks[0u]);
        const NSPoint densityHandle = [geometryPage densityHandlePoint];
        const CGFloat densityDx = densityHandle.x - geometryCenter.x;
        const CGFloat densityDy = densityHandle.y - geometryCenter.y;
        const NSPoint densityQuarterTurn = NSMakePoint(
            geometryCenter.x - densityDy,
            geometryCenter.y + densityDx);
        const BOOL beganDensityGesture = [geometryPage
            beginShapeGestureAtPoint:densityHandle];
        [geometryPage updateShapeGestureAtPoint:densityQuarterTurn];
        const BOOL densityStayedPreviewOnly =
            s3g::tracker::geometryHitCount(
                state.session.pattern.tracks[0u]) == densityBeforeGesture;
        [geometryPage finishGeometryGesture];
        check(beganDensityGesture && densityStayedPreviewOnly
                && s3g::tracker::geometryHitCount(
                    state.session.pattern.tracks[0u])
                    == densityBeforeGesture + 16u
                && patternChangeRequests == 1,
            "dragging the density handle should ghost sixteen added hits and commit once");
        state.session.pattern.tracks[0u] = originalGeometryTrack;
        patternChangeRequests = 0;
        [geometryPage syncToolboxControls];
        const NSRect geometryCanvas = [geometryPage canvasRect];
        const NSRect laneCyclePanel = [geometryPage laneCyclePanelRect];
        const NSRect geometryViewPanel = [geometryPage viewPanelRect];
        const NSRect laneMenuBox = [geometryPage laneMenuBoxRect];
        const NSRect directionMenuBox = [geometryPage directionMenuBoxRect];
        check(geometryLanePopup.numberOfItems == 12u
                && [geometryLanePopup.itemArray[0u].title
                    isEqualToString:@"01  TRACK 1"]
                && NSContainsRect(laneCyclePanel,
                    [geometryPage laneMenuBoxRect])
                && NSContainsRect(laneCyclePanel,
                    [geometryPage directionMenuBoxRect])
                && geometryLanePopup.hidden
                && geometryDirectionPopup.hidden
                && geometryViewMode.hidden
                && near(NSMinX(geometryCanvas), 18.0)
                && near(NSMinY(geometryCanvas),
                    s3g::gui_layout::kTrackerPageContentTop)
                && near(NSMinY(geometryViewPanel),
                    s3g::gui_layout::kTrackerPageContentTop)
                && near(NSMinY(laneCyclePanel) - NSMaxY(geometryViewPanel),
                    s3g::gui_layout::kStandardMetrics.panelGap)
                && near(NSMaxY(geometryCanvas),
                    NSHeight(geometryPage.bounds) - 18.0)
                && near(NSMinX(laneCyclePanel) - NSMaxX(geometryCanvas),
                    12.0)
                && near(NSHeight(laneCyclePanel), 210.0),
            "Geometry should lead with View and retain the 12/26 seven-row toolbox contract with custom in-canvas menus");
        const CGFloat expectedGeometryMenuWidth = NSWidth(laneCyclePanel)
            - static_cast<CGFloat>(
                s3g::gui_layout::kStandardMetrics.controlInset)
            - static_cast<CGFloat>(
                s3g::gui_layout::kStandardMetrics.panelRightInset);
        const NSPoint laneMenuFarEdge = NSMakePoint(
            NSMaxX(laneMenuBox) - 2.0, NSMidY(laneMenuBox));
        check(near(NSWidth(laneMenuBox), expectedGeometryMenuWidth)
                && near(NSWidth(directionMenuBox), expectedGeometryMenuWidth)
                && [geometryPage handleToolboxClickAtPoint:laneMenuFarEdge],
            "Geometry menus should open across the complete width of their drawn boxes");
        [geometryPage openGeometryMenu:1];

        warpPage.frame = NSMakeRect(0.0, 0.0, 1320.0, 780.0);
        [warpPage layoutSubtreeIfNeeded];
        id warpController = [warpPage valueForKey:@"layoutOwner"];
        NSView* warpFieldPanel = [warpController valueForKey:@"fieldPanel"];
        NSView* warpLibraryPanel = [warpController valueForKey:@"libraryPanel"];
        NSView* warpStackPanel = [warpController valueForKey:@"stackPanel"];
        NSView* warpTransformPanel = [warpController valueForKey:@"transformPanel"];
        NSView* warpCurve = [warpController valueForKey:@"curveView"];
        S3GTrackerPopupButton* warpLibraryMenu = [warpController
            valueForKey:@"libraryPopup"];
        S3GTrackerPopupButton* warpTransformMenu = [warpController
            valueForKey:@"transformPopup"];
        S3GTrackerPopupButton* warpTypeMenu = [warpController
            valueForKey:@"typePopup"];
        NSTextField* warpCycleSlider = [warpController
            valueForKey:@"cycleField"];
        NSTextField* warpNameField = [warpController
            valueForKey:@"libraryNameField"];
        S3GTrackerActionButton* warpSaveButton = [warpController
            valueForKey:@"saveLibraryButton"];
        S3GTrackerActionButton* warpDeleteButton = [warpController
            valueForKey:@"deleteLibraryButton"];
        S3GTrackerActionButton* warpModeButton = [warpController
            valueForKey:@"warpModeButton"];
        S3GTrackerDragNumberField* warpPrimarySlider = [warpController
            valueForKey:@"primaryField"];
        S3GTrackerDragNumberField* warpRepeatSlider = [warpController
            valueForKey:@"repeatsField"];
        NSArray<NSTextField*>* warpLibraryLabels = [warpController
            valueForKey:@"libraryLabels"];
        NSArray<NSTextField*>* warpStackLabels = [warpController
            valueForKey:@"stackLabels"];
        NSArray<NSTextField*>* warpTransformLabels = [warpController
            valueForKey:@"transformLabels"];
        const auto warpFamily = s3g::gui_layout::trackerWarpFamilyLayout({
            1320.0, 780.0,
        });
        const NSRect warpCycleTrack = [warpCycleSlider sliderTrackRect];
        const NSRect warpCycleValue = [warpCycleSlider valueTextRect];
        check(near(NSMinY(warpFieldPanel.frame),
                    s3g::gui_layout::kTrackerPageContentTop)
                && near(NSMinY(warpLibraryPanel.frame),
                    s3g::gui_layout::kTrackerPageContentTop)
                && near(NSMinY(warpStackPanel.frame)
                    - NSMaxY(warpLibraryPanel.frame), 12.0)
                && near(NSMinY(warpTransformPanel.frame)
                    - NSMaxY(warpStackPanel.frame), 12.0)
                && near(NSHeight(warpStackPanel.frame),
                    s3g::gui_layout::toolboxHeightForRows(5u))
                && warpLibraryMenu.s3gUsesCanvasMenu
                && warpTransformMenu.s3gUsesCanvasMenu
                && warpTypeMenu.s3gUsesCanvasMenu
                && [warpLibraryLabels[0u]
                    isKindOfClass:S3GTrackerSuiteLabel.class]
                && [warpStackLabels[2u]
                    isKindOfClass:S3GTrackerSuiteLabel.class]
                && [warpTransformLabels[0u]
                    isKindOfClass:S3GTrackerSuiteLabel.class]
                && [warpLibraryLabels[0u].font.fontName
                    isEqualToString:warpLibraryMenu.font.fontName]
                && [warpStackLabels[2u].font.fontName
                    isEqualToString:warpTransformMenu.font.fontName]
                && [warpTransformLabels[0u].font.fontName
                    isEqualToString:warpTypeMenu.font.fontName]
                && [warpNameField.font.fontName
                    isEqualToString:warpLibraryMenu.font.fontName]
                && near(NSMidY(warpLibraryLabels[0u].frame),
                    NSMidY(warpLibraryMenu.frame), 0.01)
                && near(NSMidY(warpStackLabels[2u].frame),
                    NSMidY(warpTransformMenu.frame), 0.01)
                && near(NSMidY(warpTransformLabels[0u].frame),
                    NSMidY(warpTypeMenu.frame), 0.01)
                && [warpLibraryPanel isKindOfClass:
                    NSClassFromString(@"S3GTrackerToolboxView")],
            "Warps should use shared toolboxes plus one suite font and vertical center for labels, text entry, and canvas menus");
        check([warpCycleSlider isKindOfClass:
                    NSClassFromString(@"S3GTrackerProcessorSliderField")]
                && near(NSHeight(warpCycleSlider.frame),
                    s3g::gui_layout::kStandardMetrics.hitHeight)
                && near(NSMinY(warpCycleTrack), 9.0)
                && near(NSHeight(warpCycleTrack), 9.0)
                && near(NSWidth(warpCycleTrack),
                    s3g::gui_layout::processorTrackWidth(
                        warpFamily.stack.frame.width))
                && near(NSMinX(warpCycleValue),
                    NSWidth(warpCycleSlider.bounds)
                        - s3g::gui_layout::kStandardMetrics
                            .processorValueWidth)
                && near(NSWidth(warpCycleValue),
                    s3g::gui_layout::kStandardMetrics.processorValueWidth)
                && !warpCycleSlider.editable
                && !warpCycleSlider.selectable
                && !warpCycleSlider.acceptsFirstResponder
                && !warpCycleSlider.drawsBackground
                && near(warpCycleSlider.layer.borderWidth, 0.0),
            "Warps sliders should use the exact shared 9-point track, capped processor width, 24-point hit row, and read-only value display");
        check(![warpNameField isKindOfClass:
                    NSClassFromString(@"S3GTrackerProcessorSliderField")]
                && warpNameField.editable && warpNameField.selectable
                && warpNameField.drawsBackground
                && near(warpNameField.layer.borderWidth, 1.0)
                && near(NSHeight(warpNameField.frame),
                    s3g::gui_layout::kStandardMetrics.hitHeight)
                && warpNameField.alignment == NSTextAlignmentLeft,
            "the Warps library name should be a full-height conventional suite text field, independent of numeric sliders");
        BOOL hasRecallButton = NO;
        for (NSView* view in warpLibraryPanel.subviews) {
            if ([view isKindOfClass:NSButton.class]
                && [static_cast<NSButton*>(view).title
                    isEqualToString:@"RECALL"])
                hasRecallButton = YES;
        }
        state.session.transport.warpCycleTicks = 7u;
        state.session.transport.timingWarp.clear();
        (void)state.session.transport.timingWarp.append(
            s3g::tracker::TimingWarpTransform::exponential(1.75));
        [warpLibraryMenu selectItemAtIndex:3u];
        [warpLibraryMenu sendAction:warpLibraryMenu.action
            to:warpLibraryMenu.target];
        check([warpLibraryMenu.titleOfSelectedItem
                    isEqualToString:@"04  ·  EMPTY"],
            "Warp Library empty slots should use the Burst number/name separator");
        warpNameField.stringValue = @"AUTO LOAD";
        [warpSaveButton performClick:nil];
        check([warpLibraryMenu.titleOfSelectedItem
                    hasPrefix:@"04  ·  AUTO LOAD  ·  "],
            "Warp Library saved slots should use the Burst number/name separator");
        state.session.transport.warpCycleTicks = 2u;
        state.session.transport.timingWarp.clear();
        [warpLibraryMenu selectItemAtIndex:4u];
        [warpLibraryMenu sendAction:warpLibraryMenu.action
            to:warpLibraryMenu.target];
        [warpLibraryMenu selectItemAtIndex:3u];
        [warpLibraryMenu sendAction:warpLibraryMenu.action
            to:warpLibraryMenu.target];
        const auto* autoLoadedWarp = state.session.warpLibrary.entry(3u);
        check(!hasRecallButton
                && [warpSaveButton.title isEqualToString:@"SAVE"]
                && NSMinY(warpSaveButton.frame) <= 4.0
                && [warpDeleteButton.title isEqualToString:@"DELETE SLOT"]
                && warpDeleteButton.enabled
                && autoLoadedWarp && autoLoadedWarp->name == "AUTO LOAD"
                && state.session.transport.warpCycleTicks == 7u
                && state.session.transport.timingWarp.size() == 1u,
            "Warp Library should put SAVE in its header, auto-load occupied menu slots, remove RECALL, and retain explicit slot deletion");
        [warpDeleteButton performClick:nil];
        check(state.session.warpLibrary.entry(3u) == nullptr
                && !warpDeleteButton.enabled,
            "Warp DELETE SLOT should remove only the selected saved slot");
        transportChangeRequests = 0;
        check(warpModeButton.tag == 1
                && [warpModeButton.title
                    isEqualToString:@"WARP PLAYBACK: OFF"]
                && near(warpPrimarySlider.s3gMaximumValue, 16.0)
                && near(warpRepeatSlider.s3gMaximumValue, 16.0),
            "Warps should expose the standard live toggle and cap repeat authoring at sixteen");
        [warpModeButton performClick:nil];
        check(state.session.transport.timingWarpEnabled
                && warpModeButton.state == NSControlStateValueOn
                && [warpModeButton.title
                    isEqualToString:@"WARP PLAYBACK: ON"]
                && transportChangeRequests == 1,
            "the Warps mode button should publish explicit Pattern warp enablement");
        [warpModeButton performClick:nil];
        check(!state.session.transport.timingWarpEnabled
                && transportChangeRequests == 2,
            "the Warps mode button should restore a true playback bypass");
        state.playing = true;
        state.timingWarpPlaybackActive = true;
        state.timingWarpPlaybackFromSong = false;
        state.timingWarpPlaybackTick = 3u;
        state.timingWarpPlaybackCycleTicks = 8u;
        state.timingWarpPlaybackStack = state.session.transport.timingWarp;
        [warpController performSelector:@selector(refreshPlaybackDisplay)];
        check([warpCurve.accessibilityValue
                    isEqualToString:@"Pattern warp playback, step 4 of 8"]
                && warpCurve.needsDisplay,
            "the Warps curve should expose and redraw the active sequence position");
        state.playing = false;
        state.timingWarpPlaybackActive = false;
        [warpController performSelector:@selector(refreshPlaybackDisplay)];
        check([warpCurve.accessibilityValue
                    isEqualToString:@"Warp playback inactive"],
            "the Warps curve playhead should disappear when playback or warp mode is inactive");
        transportChangeRequests = 0;

        auto& reshapeTrack = state.session.pattern.tracks[0u];
        reshapeTrack.velocities[0u] =
            s3g::tracker::ValueCell::withValue(0.35f);
        reshapeTrack.velocities[4u] =
            s3g::tracker::ValueCell::withValue(0.85f);
        auto& reshapePair = reshapeTrack.fxPairs[0u];
        reshapePair.actions.assign(64u,
            s3g::tracker::FxActionCell::empty());
        reshapePair.values.assign(64u,
            s3g::tracker::FxValueCell::previous());
        reshapePair.actionColumn.length = 64u;
        reshapePair.valueColumn.length = 64u;
        reshapePair.actions[0u] = s3g::tracker::FxActionCell::sequencer(
            s3g::tracker::SequencerAction::MicroTime);
        reshapePair.actions[4u] = s3g::tracker::FxActionCell::sequencer(
            s3g::tracker::SequencerAction::MicroTime);
        reshapePair.values[0u] =
            s3g::tracker::FxValueCell::withValue(0.55f);
        reshapePair.values[4u] =
            s3g::tracker::FxValueCell::withValue(0.70f);
        id reshapeController = [reshapePage valueForKey:@"layoutOwner"];
        [reshapeController performSelector:@selector(reloadModel)];
        reshapePage.frame = NSMakeRect(0.0, 0.0, 1320.0, 780.0);
        [reshapePage layoutSubtreeIfNeeded];
        S3GTrackerToolboxView* reshapeProfile = [reshapeController
            valueForKey:@"profilePanel"];
        NSView* reshapeProfileView = [reshapeController
            valueForKey:@"profileView"];
        S3GTrackerToolboxView* reshapeMutation = [reshapeController
            valueForKey:@"mutationPanel"];
        S3GTrackerToolboxView* reshapeTarget = [reshapeController
            valueForKey:@"targetPanel"];
        S3GTrackerToolboxView* reshapeTiming = [reshapeController
            valueForKey:@"timingPanel"];
        S3GTrackerToolboxView* reshapeDynamics = [reshapeController
            valueForKey:@"dynamicsPanel"];
        S3GTrackerPopupButton* reshapePatternMenu = [reshapeController
            valueForKey:@"patternPopup"];
        S3GTrackerPopupButton* reshapeLaneScope = [reshapeController
            valueForKey:@"laneScopePopup"];
        S3GTrackerPopupButton* reshapeCycleMenu = [reshapeController
            valueForKey:@"cyclePopup"];
        S3GTrackerPopupButton* reshapeTimingWrite = [reshapeController
            valueForKey:@"timingWritePopup"];
        S3GTrackerPopupButton* reshapeTimingOutliers = [reshapeController
            valueForKey:@"timingOutlierPopup"];
        S3GTrackerPopupButton* reshapeVelocityWrite = [reshapeController
            valueForKey:@"velocityWritePopup"];
        S3GTrackerPopupButton* reshapeVelocityOutliers =
            [reshapeController valueForKey:@"velocityOutlierPopup"];
        NSArray<NSTextField*>* reshapeTimingLabels = [reshapeController
            valueForKey:@"timingLabels"];
        NSArray<NSTextField*>* reshapeDynamicsLabels = [reshapeController
            valueForKey:@"dynamicsLabels"];
        NSArray<NSTextField*>* reshapeTargetLabels = [reshapeController
            valueForKey:@"targetLabels"];
        NSArray<NSTextField*>* reshapeMutationLeftLabels = [reshapeController
            valueForKey:@"mutationLeftLabels"];
        NSArray<NSTextField*>* reshapeMutationRightLabels = [reshapeController
            valueForKey:@"mutationRightLabels"];
        S3GTrackerProcessorSliderField* reshapeMutationAmount =
            [reshapeController valueForKey:@"mutationAmountField"];
        S3GTrackerProcessorSliderField* reshapeDensity = [reshapeController
            valueForKey:@"densityField"];
        S3GTrackerProcessorSliderField* reshapeSyncopation = [reshapeController
            valueForKey:@"syncopationField"];
        S3GTrackerPopupButton* reshapeDisplacement = [reshapeController
            valueForKey:@"displacementPopup"];
        S3GTrackerProcessorSliderField* reshapeBurstChance =
            [reshapeController valueForKey:@"burstChanceField"];
        S3GTrackerProcessorSliderField* reshapeCycleDrift =
            [reshapeController valueForKey:@"cycleDriftField"];
        S3GTrackerActionButton* reshapeReseed = [reshapeController
            valueForKey:@"reseedButton"];
        NSTextField* reshapeAnchors = [reshapeController
            valueForKey:@"anchorValueLabel"];
        S3GTrackerProcessorSliderField* reshapePocket = [reshapeController
            valueForKey:@"pocketField"];
        S3GTrackerProcessorSliderField* reshapeTighten = [reshapeController
            valueForKey:@"tightenField"];
        S3GTrackerProcessorSliderField* reshapeDepth = [reshapeController
            valueForKey:@"depthField"];
        S3GTrackerActionButton* reshapePreview = [reshapeController
            valueForKey:@"previewButton"];
        S3GTrackerActionButton* reshapeApply = [reshapeController
            valueForKey:@"applyButton"];
        S3GTrackerActionButton* reshapeCreateVariant = [reshapeController
            valueForKey:@"createVariantButton"];
        check([reshapeProfile.toolboxTitle
                    isEqualToString:@"PATTERN PROFILE  /  ORIGINAL → VARIANT"]
                && [reshapeMutation.toolboxTitle
                    isEqualToString:@"RHYTHM MUTATION / STATISTICAL"]
                && [reshapeTarget.toolboxTitle
                    isEqualToString:@"TARGET / ANALYZE"]
                && reshapePatternMenu.s3gUsesCanvasMenu
                && reshapeLaneScope.s3gUsesCanvasMenu
                && reshapeCycleMenu.s3gUsesCanvasMenu
                && reshapeTimingWrite.s3gUsesCanvasMenu
                && reshapeTimingOutliers.s3gUsesCanvasMenu
                && reshapeVelocityWrite.s3gUsesCanvasMenu
                && reshapeVelocityOutliers.s3gUsesCanvasMenu
                && reshapeTiming.toolboxIndex == 0
                && reshapeDynamics.toolboxIndex == 0
                && reshapeMutation.toolboxIndex == 0
                && near(NSHeight(reshapeMutation.frame),
                    s3g::gui_layout::toolboxHeightForRows(4u))
                && near(NSHeight(reshapeTarget.frame),
                    s3g::gui_layout::toolboxHeightForRows(6u))
                && near(NSHeight(reshapeTiming.frame),
                    s3g::gui_layout::toolboxHeightForRows(5u))
                && near(NSHeight(reshapeDynamics.frame),
                    s3g::gui_layout::toolboxHeightForRows(5u))
                && reshapeTimingLabels.count == 5u
                && reshapeDynamicsLabels.count == 5u
                && reshapeTargetLabels.count == 3u
                && reshapeMutationLeftLabels.count == 4u
                && reshapeMutationRightLabels.count == 4u
                && [reshapeTimingLabels[3u]
                    isKindOfClass:S3GTrackerSuiteLabel.class]
                && [reshapeDynamicsLabels[4u]
                    isKindOfClass:S3GTrackerSuiteLabel.class]
                && [reshapeTimingLabels[3u].font.fontName
                    isEqualToString:reshapeTimingWrite.font.fontName]
                && [reshapeDynamicsLabels[4u].font.fontName
                    isEqualToString:reshapeVelocityOutliers.font.fontName]
                && near(NSMidY(reshapeTimingLabels[3u].frame),
                    NSMidY(reshapeTimingWrite.frame), 0.01)
                && near(NSMidY(reshapeDynamicsLabels[4u].frame),
                    NSMidY(reshapeVelocityOutliers.frame), 0.01)
                && [reshapeDepth isKindOfClass:
                    S3GTrackerProcessorSliderField.class]
                && [reshapePocket isKindOfClass:
                    S3GTrackerProcessorSliderField.class]
                && [reshapeMutationAmount isKindOfClass:
                    S3GTrackerProcessorSliderField.class]
                && [reshapeDensity isKindOfClass:
                    S3GTrackerProcessorSliderField.class]
                && [reshapeSyncopation isKindOfClass:
                    S3GTrackerProcessorSliderField.class]
                && [reshapeBurstChance isKindOfClass:
                    S3GTrackerProcessorSliderField.class]
                && [reshapeCycleDrift isKindOfClass:
                    S3GTrackerProcessorSliderField.class]
                && reshapeDisplacement.s3gUsesCanvasMenu
                && [reshapeDisplacement.titleOfSelectedItem
                    isEqualToString:@"1 ROW"]
                && [reshapeReseed.title hasPrefix:@"RESEED · "]
                && [reshapeAnchors.stringValue
                    isEqualToString:@"DOWNBEAT / HIGH CONF"]
                && near(reshapeMutationAmount.doubleValue, 55.0)
                && !reshapeDepth.editable && !reshapeDepth.selectable
                && !reshapeDepth.drawsBackground
                && near(reshapeDepth.layer.borderWidth, 0.0)
                && near(NSHeight(reshapeDepth.frame),
                    s3g::gui_layout::kStandardMetrics.hitHeight)
                && near(NSWidth([reshapeDepth sliderTrackRect]),
                    s3g::gui_layout::processorTrackWidth(
                        reshapeTiming.frame.size.width))
                && [reshapeTimingWrite.titleOfSelectedItem
                    isEqualToString:@"ADD TO ONSETS"]
                && [reshapeProfileView.accessibilityValue
                    containsString:@"hits"]
                && [reshapeProfileView.accessibilityHelp
                    containsString:@"signed microtime in milliseconds"]
                && [reshapeProfileView.accessibilityHelp
                    containsString:@"median velocity per lane"]
                && near(NSMinX(reshapeProfile.frame),
                    s3g::gui_layout::kTrackerPageHorizontalInset)
                && near(NSMinY(reshapeProfile.frame),
                    s3g::gui_layout::kTrackerPageContentTop)
                && near(NSMaxY(reshapeProfile.frame)
                    + s3g::gui_layout::kStandardMetrics.panelGap,
                    NSMinY(reshapeMutation.frame))
                && near(NSMaxY(reshapeMutation.frame),
                    NSHeight(reshapePage.bounds)
                        - s3g::gui_layout::kTrackerPageBottomInset)
                && near(NSMinX(reshapeTarget.frame)
                    - NSMaxX(reshapeProfile.frame), 12.0),
            "Reshape Variations should use shared toolbox rows, menus, sliders, columns, and page gutters");
        [reshapeLaneScope selectItemAtIndex:2u];
        [reshapeLaneScope sendAction:reshapeLaneScope.action
            to:reshapeLaneScope.target];
        check([reshapeLaneScope.s3gDisplayTitle isEqualToString:@"L02 ONLY"]
                && [reshapeLaneScope.itemArray[2u].title
                    hasPrefix:@"● L02"],
            "Reshape lane scope should isolate one lane and visibly mark included lanes");
        [reshapeLaneScope selectItemAtIndex:0u];
        [reshapeLaneScope sendAction:reshapeLaneScope.action
            to:reshapeLaneScope.target];
        check([reshapeLaneScope.s3gDisplayTitle isEqualToString:@"ALL LANES"],
            "Reshape lane scope should restore the complete pattern from the same menu");
        reshapeProfileView.needsDisplay = NO;
        reshapeTighten.doubleValue = 100.0;
        [reshapeTighten sendAction:reshapeTighten.action
            to:reshapeTighten.target];
        check(reshapeProfileView.needsDisplay,
            "a Reshape slider should immediately redraw the HITS/MT/VEL/LANE profile");
        [reshapePreview performClick:nil];
        check(patternPreviewRequests > 0
                && previewedPattern.tracks[0u].notes[0u].note
                    == state.session.pattern.tracks[0u].notes[0u].note
                && near(previewedPattern.tracks[0u].fxPairs[0u]
                    .values[0u].normalized, 0.5, 0.0001),
            "Reshape preview should audition tightened MT at the row grid while preserving authored notes");
        [reshapePreview performClick:nil];
        check(patternPreviewClearRequests == 1,
            "turning Reshape preview off should restore the stored runtime");
        [reshapeCreateVariant performClick:nil];
        check(patternVariantRequests == 1
                && createdVariant.tracks.size()
                    == state.session.pattern.tracks.size()
                && near(createdVariant.tracks[0u].fxPairs[0u]
                    .values[0u].normalized, 0.5, 0.0001)
                && !near(createdVariant.tracks[0u].fxPairs[0u]
                    .values[0u].normalized,
                    state.session.pattern.tracks[0u].fxPairs[0u]
                        .values[0u].normalized, 0.0001),
            "Create Variant should publish a changed pattern while leaving the source available");
        const int reshapeHistoryBefore = patternChangeRequests;
        [reshapeApply performClick:nil];
        check(patternChangeRequests == reshapeHistoryBefore + 1
                && state.session.pattern.tracks[0u].fxPairs[0u]
                    .values[0u].normalized != 0.55f,
            "Reshape Apply should commit the transformed pattern as one history request");
        patternChangeRequests = 0;

        const auto originalSecondGeometryTrack =
            state.session.pattern.tracks[1u];
        [geometryPage openGeometryMenu:1];
        [geometryPage applyGeometryMenuSelection:1u];
        check(state.session.selectedTrack == 1u,
            "the custom Geometry lane menu should update the shared Tracker lane selection");
        [geometryPage openGeometryMenu:2];
        [geometryPage applyGeometryMenuSelection:2u];
        check(state.session.pattern.tracks[1u].noteColumn.direction
                    == s3g::tracker::Direction::Palindrome
                && patternChangeRequests == 1,
            "the repeated direction menu should edit the selected NOTE cycle through shared history");

        const auto sliderPoint = [](NSRect slider, CGFloat normalized) {
            return NSMakePoint(NSMinX(slider) + NSWidth(slider) * normalized,
                NSMidY(slider));
        };
        const NSRect lengthSlider = [geometryPage lengthSliderTrack];
        const BOOL beganLengthSlider = [geometryPage
            beginSliderGestureAtPoint:sliderPoint(lengthSlider,
                std::sqrt((64.0 - 1.0) / 255.0))];
        [geometryPage updateSliderGestureAtPoint:
            sliderPoint(lengthSlider, std::sqrt((32.0 - 1.0) / 255.0))];
        const BOOL lengthStayedPreviewOnly =
            state.session.pattern.tracks[1u].noteColumn.length == 64u;
        [geometryPage finishGeometryGesture];
        check(beganLengthSlider && lengthStayedPreviewOnly
                && state.session.pattern.tracks[1u].noteColumn.length == 32u
                && state.session.pattern.tracks[1u].velocityColumn.length
                    == 32u
                && patternChangeRequests == 2,
            "the repeated length slider should preview and publish one undoable NOTE and linked VOL length edit");

        const NSRect linkToggle = [geometryPage
            linkVelocityLengthToggleRect];
        const BOOL linkWasOn = [[geometryPage
            valueForKey:@"linkVelocityLength"] boolValue];
        (void)[geometryPage handleToolboxClickAtPoint:NSMakePoint(
            NSMidX(linkToggle), NSMidY(linkToggle))];
        const BOOL linkTurnedOff = ![[geometryPage
            valueForKey:@"linkVelocityLength"] boolValue];
        (void)[geometryPage handleToolboxClickAtPoint:NSMakePoint(
            NSMidX(linkToggle), NSMidY(linkToggle))];
        check(linkWasOn && linkTurnedOff && [[geometryPage
                valueForKey:@"linkVelocityLength"] boolValue]
                && patternChangeRequests == 2,
            "Geometry should expose a default-on VOL length link that can be disabled for independent cycles");

        state.session.pattern.tracks[1u].notes[0u]
            = s3g::tracker::NoteCell::withNote(67u);
        const NSRect rotateSlider = [geometryPage rotateSliderTrack];
        const BOOL beganRotateSlider = [geometryPage
            beginSliderGestureAtPoint:sliderPoint(rotateSlider, 0.5)];
        const CGFloat rotateEight = 0.5 + 0.5
            * std::sqrt(8.0 / 31.0);
        [geometryPage updateSliderGestureAtPoint:
            sliderPoint(rotateSlider, rotateEight)];
        const BOOL sliderRotationStayedPreviewOnly =
            state.session.pattern.tracks[1u].notes[0u].state
                == s3g::tracker::NoteCellState::Note;
        [geometryPage finishGeometryGesture];
        check(beganRotateSlider && sliderRotationStayedPreviewOnly
                && state.session.pattern.tracks[1u].notes[8u].state
                    == s3g::tracker::NoteCellState::Note
                && state.session.pattern.tracks[1u].noteColumn.phase == 0u
                && patternChangeRequests == 3,
            "the toolbox rotate slider should share the ring handle's authored-row preview and history path");

        std::fill(state.session.pattern.tracks[1u].notes.begin(),
            state.session.pattern.tracks[1u].notes.end(),
            s3g::tracker::NoteCell::rest());
        const NSRect densitySlider = [geometryPage densitySliderTrack];
        const BOOL beganDensitySlider = [geometryPage
            beginSliderGestureAtPoint:sliderPoint(densitySlider, 0.0)];
        [geometryPage updateSliderGestureAtPoint:
            sliderPoint(densitySlider, 8.0 / 32.0)];
        const BOOL sliderDensityStayedPreviewOnly =
            s3g::tracker::geometryHitCount(
                state.session.pattern.tracks[1u]) == 0u;
        [geometryPage finishGeometryGesture];
        check(beganDensitySlider && sliderDensityStayedPreviewOnly
                && s3g::tracker::geometryHitCount(
                    state.session.pattern.tracks[1u]) == 8u
                && patternChangeRequests == 4,
            "the toolbox density slider should ghost its distribution and commit once");
        [geometryMorphTarget selectItemAtIndex:0];
        [geometryMorphButtons[3u] performClick:nil];
        check(state.session.pattern.tracks[1u].notes[0u].state
                    == s3g::tracker::NoteCellState::Note
                && patternChangeRequests == 5,
            "the expanded morph controls should support a full morph toward the previous visible lane");
        state.session.pattern.tracks[1u] = originalSecondGeometryTrack;
        [geometryPage selectLane:0u];
        [geometryPage syncToolboxControls];
        patternChangeRequests = 0;
        check(NSContainsRect([geometryPage viewPanelRect],
                [geometryPage viewMenuBoxRect]),
            "Geometry view selector should use the shared in-canvas menu slot");
        check([[trackerGrid displayedPatternId] isEqualToString:@"A01"]
                && [trackerGrid displayedLaneCount] == 12u
                && [trackerGrid displayedVisibleRowCount] == 64u
                && [trackerGrid displayedNoteNumberAtLane:0u row:0u] == 60,
            "Tracker should initially present the editor pattern");
        state.playing = true;
        state.songPlaybackActive = true;
        state.songPlaybackPatternId = "A02";
        state.songPlaybackMutedTracks = 1u << 0u;
        [controller refreshPlaybackDisplay];
        [root layoutSubtreeIfNeeded];
        check([[trackerGrid displayedPatternId] isEqualToString:@"A02"]
                && [trackerGrid displayedLaneCount] == 2u
                && [trackerGrid displayedVisibleRowCount] == 32u
                && [trackerGrid displayedNoteNumberAtLane:0u row:0u] == 67
                && [patternPopup.selectedItem.representedObject
                    isEqualToString:@"A02"]
                && !patternPopup.enabled && !trackAddButton.enabled
                && !trackRemoveButton.enabled,
            "Tracker should follow the sounding Song pattern, its dimensions and content, while locking editor mutations");
        state.playing = false;
        state.songPlaybackActive = false;
        state.songPlaybackPatternId.clear();
        state.songPlaybackMutedTracks = 0u;
        [controller refreshPlaybackDisplay];
        [root layoutSubtreeIfNeeded];
        check([[trackerGrid displayedPatternId] isEqualToString:@"A01"]
                && [trackerGrid displayedLaneCount] == 12u
                && [trackerGrid displayedVisibleRowCount] == 64u
                && [patternPopup.selectedItem.representedObject
                    isEqualToString:@"A01"]
                && patternPopup.enabled && trackAddButton.enabled,
            "Tracker should restore the unchanged editor pattern when Song playback stops");
        check([[geometryPage valueForKey:@"displayedPatternId"]
                isEqualToString:@"A01"],
            "Geometry should initially display the editor pattern");
        geometryPage.needsDisplay = NO;
        state.songPlaybackActive = true;
        state.songPlaybackPatternId = "A02";
        state.songPlaybackMutedTracks = 1u << 0u;
        [geometryPage performSelector:@selector(refreshPlaybackDisplay)];
        const CGFloat mutedLaneRadius = [geometryPage ringRadiusForLane:0u];
        check([[geometryPage valueForKey:@"displayedPatternId"]
                    isEqualToString:@"A02"]
                && [[geometryPage valueForKey:@"displayedLaneCount"]
                    unsignedIntegerValue] == 2u
                && [[geometryPage valueForKey:@"displayedMutedLaneCount"]
                    unsignedIntegerValue] == 2u
                && geometryPage.needsDisplay,
            "Geometry should retain every sounding-pattern ring slot while combining Pattern and Song-row mutes");
        [geometryPage displayIfNeeded];
        geometryPage.needsDisplay = NO;
        state.songPlaybackMutedTracks = 0u;
        [geometryPage performSelector:@selector(refreshPlaybackDisplay)];
        const CGFloat unmutedLaneRadius = [geometryPage ringRadiusForLane:0u];
        check([[geometryPage valueForKey:@"displayedLaneCount"]
                    unsignedIntegerValue] == 2u
                && [[geometryPage valueForKey:@"displayedMutedLaneCount"]
                    unsignedIntegerValue] == 1u
                && near(mutedLaneRadius, unmutedLaneRadius, 0.01)
                && geometryPage.needsDisplay,
            "unmuting a Song lane should restore its content without moving that lane's ring radius or removing Pattern-mute placeholders");
        state.songPlaybackActive = false;
        state.songPlaybackPatternId.clear();
        state.songPlaybackMutedTracks = 0u;
        [geometryPage performSelector:@selector(refreshPlaybackDisplay)];
        check([[geometryPage valueForKey:@"displayedPatternId"]
                isEqualToString:@"A01"],
            "Geometry should return to the editor pattern when Song playback stops");
        NSArray<NSString*>* geometryDescriptions = @[
            @"Ring field", @"Active pulses", @"All steps underlay",
            @"Phase spokes", @"Lane focus", @"Composite ring",
            @"Burst editor", @"Pitch map"
        ];
        BOOL geometryModesDispatch = YES;
        for (NSInteger menuIndex = 1; menuIndex < 7; ++menuIndex) {
            const NSInteger mode = menuIndex == 6 ? 7 : menuIndex;
            geometryPlaybackOverlay.needsDisplay = NO;
            [geometryPage openGeometryMenu:3];
            [geometryPage applyGeometryMenuSelection:menuIndex];
            geometryModesDispatch = geometryModesDispatch
                && [[geometryPage valueForKey:@"geometryViewMode"]
                    integerValue] == mode
                && [geometryPage.accessibilityValue
                    isEqualToString:geometryDescriptions[(NSUInteger)mode]]
                && geometryPlaybackOverlay.needsDisplay;
            if (mode == 1)
                geometryModesDispatch = geometryModesDispatch
                    && [geometryPage allStepsUnderlayNodeCount] == 0u;
            if (mode == 2)
                geometryModesDispatch = geometryModesDispatch
                    && [geometryPage allStepsUnderlayNodeCount] > 0u;
            if (mode == 3)
                geometryModesDispatch = geometryModesDispatch
                    && [geometryPage allStepsUnderlayNodeCount] == 0u;
        }
        check(geometryModesDispatch,
            "every Geometry view should dispatch without crossing into the dedicated Bursts page");
        state.session.selectedTrack = 0u;
        auto& pitchTrack = state.session.pattern.tracks[0u];
        pitchTrack.noteColumn.length = std::max<std::size_t>(
            pitchTrack.noteColumn.length, 4u);
        pitchTrack.notes.resize(std::max<std::size_t>(
            pitchTrack.notes.size(), 4u), s3g::tracker::NoteCell::rest());
        pitchTrack.notes[0u] = s3g::tracker::NoteCell::withNote(60u);
        pitchTrack.notes[1u] = s3g::tracker::NoteCell::withNote(61u);
        pitchTrack.notes[2u] = s3g::tracker::NoteCell::withNote(64u);
        pitchTrack.notes[3u] = s3g::tracker::NoteCell::withNote(67u);
        [geometryPage openPitchMapFirstRow:0u lastRow:3u];
        [geometryPage openGeometryMenu:8];
        [geometryPage applyGeometryMenuSelection:0u];
        [geometryPage openGeometryMenu:9];
        [geometryPage applyGeometryMenuSelection:1u];
        [geometryPage openGeometryMenu:10];
        [geometryPage applyGeometryMenuSelection:0u];
        check([[geometryPage pitchSelectedPointFlagText]
                    isEqualToString:@"C-4 · MIDI 060"],
            "Pitch Map should identify the selected breakpoint with note name and MIDI number");
        const NSRect pitchContourPanel = [geometryPage editPanelRect];
        const NSRect pitchViewPanel = [geometryPage viewPanelRect];
        check(NSContainsRect(pitchContourPanel,
                    [geometryPage pitchTransposeSliderTrack])
                && NSContainsRect(pitchContourPanel,
                    [geometryPage pitchInvertToggleRect])
                && NSContainsRect(pitchContourPanel,
                    [geometryPage pitchReverseToggleRect])
                && NSMaxY(pitchViewPanel)
                    < NSMinY([geometryPage laneCyclePanelRect])
                && NSMaxY([geometryPage laneCyclePanelRect])
                    < NSMinY(pitchContourPanel),
            "Geometry View should lead the inspector while Pitch Map transforms remain inside the expanded Contour toolbox");
        const NSRect pitchPreviewButton = [geometryPage
            pitchPreviewHeaderButtonRect];
        const BOOL previewedPitchMap = [geometryPage handleToolboxClickAtPoint:
            NSMakePoint(NSMidX(pitchPreviewButton),
                NSMidY(pitchPreviewButton))];
        check(previewedPitchMap && pitchPreviewRequests == 1
                && previewedPitch.size() == 4u
                && previewedPitch[0u].row == 0u
                && previewedPitch[1u].note == 60u
                && previewedChannel == pitchTrack.midiChannel
                && near(previewedBpm, state.session.transport.bpm)
                && previewedTicksPerBeat
                    == state.session.transport.ticksPerBeat,
            "stopped Pitch Map Preview should dispatch the fitted note contour with row timing and velocity");
        state.playing = true;
        [geometryPage handleToolboxClickAtPoint:NSMakePoint(
            NSMidX(pitchPreviewButton), NSMidY(pitchPreviewButton))];
        check(pitchPreviewRequests == 1,
            "Pitch Map Preview should remain unavailable while transport is running");
        state.playing = false;
        const int changesBeforePitchMap = patternChangeRequests;
        [geometryPage applyCurrentPitchMap];
        check([[geometryPage valueForKey:@"geometryViewMode"] integerValue]
                    == 7
                && [geometryPage itemsForGeometryMenu:9].count == 101u
                && [geometryPage itemsForGeometryMenu:10].count == 7u
                && [[geometryPage itemsForGeometryMenu:10][6u]
                    isEqualToString:@"MANUAL"]
                && NSWidth([geometryPage pitchGraphRect]) > 400.0
                && pitchTrack.notes[1u].note == 60u
                && patternChangeRequests == changesBeforePitchMap + 1,
            "Pitch Map should use the shared scale catalog, selected rows, visual canvas, and one pattern commit");
        [geometryPage handleToolboxClickAtPoint:NSMakePoint(
            NSMidX(pitchPreviewButton), NSMidY(pitchPreviewButton))];
        BOOL appliedPreviewStayedVisible = previewedPitch.size() == 4u
            && [geometryPage selectedIndexForGeometryMenu:10] == 6;
        for (std::size_t index = 0u;
             appliedPreviewStayedVisible && index < previewedPitch.size();
             ++index) {
            appliedPreviewStayedVisible = previewedPitch[index].note
                == pitchTrack.notes[index].note;
        }
        [geometryPage openGeometryMenu:10];
        [geometryPage applyGeometryMenuSelection:5u];
        [geometryPage handleToolboxClickAtPoint:NSMakePoint(
            NSMidX(pitchPreviewButton), NSMidY(pitchPreviewButton))];
        const auto generatedBeforeManualEdit = previewedPitch;
        [geometryPage freezePitchPreviewForManualEditing];
        [geometryPage handleToolboxClickAtPoint:NSMakePoint(
            NSMidX(pitchPreviewButton), NSMidY(pitchPreviewButton))];
        BOOL manualBakeStayedVisible = generatedBeforeManualEdit.size()
            == previewedPitch.size()
            && [geometryPage selectedIndexForGeometryMenu:10] == 6;
        for (std::size_t index = 0u;
             manualBakeStayedVisible && index < previewedPitch.size();
             ++index) {
            manualBakeStayedVisible = generatedBeforeManualEdit[index].note
                == previewedPitch[index].note;
        }
        check(appliedPreviewStayedVisible && manualBakeStayedVisible,
            "Apply and the first manual edit should freeze the exact visible generated contour instead of regenerating it from changed source notes");
        const NSRect contourGraph = [geometryPage pitchGraphRect];
        const NSRect intervalGraph = [geometryPage pitchIntervalGraphRect];
        const NSPoint anchorPoint = [geometryPage
            pitchMapPointForAssignmentAtIndex:0u original:NO interval:YES];
        [geometryPage handleToolboxClickAtPoint:NSMakePoint(
            NSMidX(pitchPreviewButton), NSMidY(pitchPreviewButton))];
        const auto beforeIntervalEdit = previewedPitch;
        state.session.selectedRow = 1u;
        [geometryPage setValue:@1 forKey:@"pitchDragAssignment"];
        [geometryPage setValue:@YES forKey:@"pitchEditingIntervals"];
        const NSPoint intervalPoint = [geometryPage
            pitchMapPointForAssignmentAtIndex:1u original:NO interval:YES];
        [geometryPage updatePitchMapPointAtPoint:NSMakePoint(
            intervalPoint.x, NSMinY(intervalGraph))];
        [geometryPage handleToolboxClickAtPoint:NSMakePoint(
            NSMidX(pitchPreviewButton), NSMidY(pitchPreviewButton))];
        check(NSMaxY(contourGraph) < NSMinY(intervalGraph)
                && near(NSMinX(contourGraph), NSMinX(intervalGraph))
                && near(NSWidth(contourGraph), NSWidth(intervalGraph))
                && near(anchorPoint.y, NSMidY(intervalGraph))
                && beforeIntervalEdit.size() == previewedPitch.size()
                && previewedPitch.size() == 4u
                && previewedPitch[0u].note == beforeIntervalEdit[0u].note
                && previewedPitch[1u].note != beforeIntervalEdit[1u].note
                && previewedPitch[3u].note != beforeIntervalEdit[3u].note,
            "Pitch Map should show Contour and Interval simultaneously while interval editing shifts the selected note plus following phrase in scale degrees");
        const uint8_t manuallyEditedPitch = previewedPitch[1u].note;
        [geometryPage applyCurrentPitchMap];
        check(pitchTrack.notes[1u].note == manuallyEditedPitch
                && [geometryPage selectedIndexForGeometryMenu:10] == 6,
            "a manually moved point should remain effective when the frozen contour is applied");
        state.session.selectedRow = 0u;
        std::array<uint8_t, s3g::tracker::kMaximumNoteVoices>
            pitchChord {};
        pitchChord[0u] = 60u;
        pitchChord[1u] = 63u;
        pitchChord[2u] = 66u;
        pitchTrack.notes[0u] = s3g::tracker::NoteCell::withNotes(
            pitchChord, 3u);
        std::array<float, s3g::tracker::kMaximumNoteVoices>
            pitchChordVelocity {};
        pitchChordVelocity[0u] = 0.9f;
        pitchChordVelocity[1u] = 0.7f;
        pitchChordVelocity[2u] = 0.5f;
        pitchTrack.velocities.resize(std::max<std::size_t>(
            pitchTrack.velocities.size(), 4u),
            s3g::tracker::ValueCell::defaultValue());
        pitchTrack.velocities[0u] = s3g::tracker::ValueCell::withValues(
            pitchChordVelocity, 3u);
        [geometryPage openPitchMapFirstRow:0u lastRow:3u];
        [geometryPage openGeometryMenu:8];
        [geometryPage applyGeometryMenuSelection:0u];
        [geometryPage openGeometryMenu:9];
        [geometryPage applyGeometryMenuSelection:1u];
        [geometryPage openGeometryMenu:10];
        [geometryPage applyGeometryMenuSelection:0u];
        check([[geometryPage pitchSelectedPointFlagText]
                    isEqualToString:@"C-4 · MIDI 060  +  D-4 · MIDI 062  +  F-4 · MIDI 065"],
            "Pitch Map should expose every scale-retargeted voice in the selected chord flag");
        [geometryPage handleToolboxClickAtPoint:NSMakePoint(
            NSMidX(pitchPreviewButton), NSMidY(pitchPreviewButton))];
        check(previewedPitch.size() == 6u
                && previewedPitch[0u].row == 0u
                && previewedPitch[1u].row == 0u
                && previewedPitch[2u].row == 0u
                && previewedPitch[0u].note == 60u
                && previewedPitch[1u].note == 62u
                && previewedPitch[2u].note == 65u
                && previewedPitch[0u].velocity == 114u
                && previewedPitch[1u].velocity == 89u
                && previewedPitch[2u].velocity == 64u,
            "Pitch Map preview should audition simultaneous chord voices with their authored velocities");
        [geometryPage applyCurrentPitchMap];
        check(pitchTrack.notes[0u].noteVoiceCount() == 3u
                && pitchTrack.notes[0u].noteVoice(0u) == 60u
                && pitchTrack.notes[0u].noteVoice(1u) == 62u
                && pitchTrack.notes[0u].noteVoice(2u) == 65u,
            "Pitch Map Apply should commit the complete scale-adjusted chord cell");
        [geometryPage selectLane:0u];
        [geometryPage openGeometryMenu:3];
        [geometryPage applyGeometryMenuSelection:0u];
        [window displayIfNeeded];
        grid.documentView.needsDisplay = NO;
        envelope.needsDisplay = NO;
        envelopePlaybackOverlay.needsDisplay = NO;
        geometryPlaybackOverlay.needsDisplay = NO;
        const double hiddenGeometryAnimationTime = [[geometryPage
            valueForKey:@"lastReadHeadAnimationTime"] doubleValue];
        state.playing = true;
        state.notePlayheads[0u] = 3u;
        state.velocityPlayheads[0u] = 4u;
        [controller refreshPlaybackDisplay];
        check([[grid.documentView
                    valueForKey:@"playbackPresentationPrimed"] boolValue],
            "visible tracker playback should advance its incremental grid presentation state");
        check(near([[geometryPage valueForKey:
                    @"lastReadHeadAnimationTime"] doubleValue],
                hiddenGeometryAnimationTime),
            "hidden geometry should not advance its playback animation");
        state.playing = false;
        [controller refreshPlaybackDisplay];
        check(!state.sequenceColumnsExpanded
                && [sequenceColumnsButton.title isEqualToString:@"EXPAND DETAIL"],
            "tracker should open in compact NOTE/VOL lane mode");
        check(state.showMidiNoteValues
                && [noteDisplayButton.title isEqualToString:@"NOTE: MIDI"]
                && [grid.documentView.accessibilityValue
                    containsString:@"Note, 60"],
            "tracker notes should initially display as decimal MIDI values");
        [noteDisplayButton performClick:nil];
        check(!state.showMidiNoteValues
                && [noteDisplayButton.title isEqualToString:@"NOTE: NAME"]
                && [grid.documentView.accessibilityValue
                    containsString:@"Note, C-4"],
            "note display control should switch to pitch names");
        [noteDisplayButton performClick:nil];
        check(state.showMidiNoteValues
                && [grid.documentView.accessibilityValue
                    containsString:@"Note, 60"],
            "note display control should return to MIDI values without changing the note");
        check(midiStepRecordPopup.enabled
                && midiStepRecordPopup.numberOfItems == 4u
                && [midiStepRecordPopup.selectedItem.title
                    isEqualToString:@"REC OFF"]
                && [[midiStepRecordPopup itemAtIndex:1u].title
                    isEqualToString:@"REC STEP"]
                && [[midiStepRecordPopup itemAtIndex:2u].title
                    isEqualToString:@"REC Q"]
                && [[midiStepRecordPopup itemAtIndex:3u].title
                    isEqualToString:@"REC MT"],
            "MIDI recording should expose STEP plus two live timing modes");
        check(midiRecordTrackPopup.enabled
                && midiRecordTrackPopup.numberOfItems == 12u
                && [midiRecordTrackPopup.selectedItem.title
                    isEqualToString:@"REC L01 · TRACK 1"]
                && [[midiRecordTrackPopup itemAtIndex:1u].title
                    isEqualToString:@"REC L02 · TRACK 2"],
            "MIDI recording should expose an explicit named lane target");
        [midiRecordTrackPopup selectItemAtIndex:2u];
        [midiRecordTrackPopup sendAction:midiRecordTrackPopup.action
            to:midiRecordTrackPopup.target];
        check(state.midiRecordTrack == 2u && recordTrackRequests == 1,
            "REC LANE should arm a fixed lane through the coordinator");
        [midiStepRecordPopup selectItemAtIndex:1u];
        [midiStepRecordPopup sendAction:midiStepRecordPopup.action
            to:midiStepRecordPopup.target];
        [midiStepRecordPopup selectItemAtIndex:2u];
        [midiStepRecordPopup sendAction:midiStepRecordPopup.action
            to:midiStepRecordPopup.target];
        [midiStepRecordPopup selectItemAtIndex:3u];
        [midiStepRecordPopup sendAction:midiStepRecordPopup.action
            to:midiStepRecordPopup.target];
        check(state.midiStepRecordMode
                    == s3g::tracker::MidiStepRecordMode::LiveUnquantized
                && stepRecordModeRequests == 3,
            "STEP, LIVE Q, and LIVE MT should arm the coordinator explicitly");
        const CGFloat initialSequenceX = NSMinX(sequenceColumnsButton.frame);
        const CGFloat initialAddTrackX = NSMinX(trackAddButton.frame);
        const CGFloat initialRemoveTrackX = NSMinX(trackRemoveButton.frame);
        NSStackView* toolboxStack = static_cast<NSStackView*>(
            transport.documentView);
        check(transportPanel.toolboxIndex == 0
                && [transportPanel.toolboxTitle isEqualToString:@"TRANSPORT"]
                && near(NSMinX(transportPanel.frame), 18.0)
                && near(NSMaxX(transportPanel.frame), NSWidth(root.bounds) - 18.0)
                && near(NSMinY(transportPanel.frame), 12.0)
                && NSMaxY(transportPanel.frame) < NSMinY(envelope.frame)
                && patternPanel.toolboxIndex == 0
                && [patternPanel.toolboxTitle isEqualToString:@"PATTERN"]
                && inputViewPanel.toolboxIndex == 0
                && [inputViewPanel.toolboxTitle isEqualToString:@"VIEW"]
                && toolboxStack.arrangedSubviews.count == 2u
                && toolboxStack.arrangedSubviews[0u] == patternPanel
                && toolboxStack.arrangedSubviews[1u] == inputViewPanel,
            "Tracker should keep Pattern and View at the top and place its unnumbered Transport toolbox below the breakpoint editor");
        check(patternPrimaryControls.arrangedSubviews.count == 9u
                && patternPrimaryControls.arrangedSubviews[0u] == patternPopup
                && NSWidth(patternPopup.frame) >= 419.0
                && patternPrimaryControls.arrangedSubviews[1u]
                    == renamePatternButton
                && patternPrimaryControls.arrangedSubviews[2u]
                    == duplicatePatternButton
                && patternPrimaryControls.arrangedSubviews[3u]
                    == createPatternButton
                && patternPrimaryControls.arrangedSubviews[4u]
                    == deletePatternButton
                && patternPrimaryControls.arrangedSubviews[5u]
                    == trackAddButton
                && patternPrimaryControls.arrangedSubviews[6u]
                    == trackRemoveButton
                && patternPrimaryControls.arrangedSubviews[7u] == undoButton
                && patternPrimaryControls.arrangedSubviews[8u] == redoButton
                && [inputPrimaryControls.arrangedSubviews
                    containsObject:sequenceColumnsButton]
                && [inputPrimaryControls.arrangedSubviews
                    containsObject:noteDisplayButton]
                && [inputPrimaryControls.arrangedSubviews
                    containsObject:stepJumpPopup]
                && [inputPrimaryControls.arrangedSubviews
                    containsObject:zoomOutButton]
                && [inputPrimaryControls.arrangedSubviews
                    containsObject:zoomActualButton]
                && [inputPrimaryControls.arrangedSubviews
                    containsObject:zoomInButton]
                && ![inputPrimaryControls.arrangedSubviews
                    containsObject:midiStepRecordPopup]
                && transportPrimaryControls.arrangedSubviews.count == 12u
                && [transportPrimaryControls.arrangedSubviews
                    containsObject:fillButton]
                && [transportPrimaryControls.arrangedSubviews
                    containsObject:tempoScalePopup]
                && [transportPrimaryControls.arrangedSubviews
                    containsObject:swingField]
                && [transportPrimaryControls.arrangedSubviews
                    containsObject:gateField]
                && [transportPrimaryControls.arrangedSubviews
                    containsObject:loopStartField]
                && [transportPrimaryControls.arrangedSubviews
                    containsObject:loopEndField]
                && [transportPrimaryControls.arrangedSubviews
                    containsObject:midiStepRecordPopup]
                && [transportPrimaryControls.arrangedSubviews
                    containsObject:midiRecordTrackPopup],
            "Pattern should lead with a wide long-name menu, while View and bottom Transport controls each occupy one compact row");
        fillButton.state = NSControlStateValueOn;
        [fillButton sendAction:fillButton.action to:fillButton.target];
        check(state.fillActive && fillChangeRequests == 1
                && fillButton.tag == 3,
            "Transport FILL should publish transient active state and use the green performance status rule");
        check([swingField isKindOfClass:S3GTrackerSwingSlider.class]
                && [swingField.s3gLabel isEqualToString:@"SW"]
                && [(S3GTrackerPopupButton*)gateField s3gUsesCanvasMenu]
                && [(S3GTrackerPopupButton*)loopStartField s3gUsesCanvasMenu]
                && [(S3GTrackerPopupButton*)loopEndField s3gUsesCanvasMenu]
                && near(NSMidY(swingField.frame),
                    NSMidY(tempoScalePopup.frame), 0.01)
                && near(NSMidY(gateField.frame),
                    NSMidY(tempoScalePopup.frame), 0.01)
                && near(NSMidY(midiStepRecordPopup.frame),
                    NSMidY(tempoScalePopup.frame), 0.01)
                && near(NSMidY(midiRecordTrackPopup.frame),
                    NSMidY(tempoScalePopup.frame), 0.01),
            "Tracker transport values should use the Song-style Swing slider and suite canvas menus");
        bool everyTransportControlHit = true;
        for (NSView* control in transportPrimaryControls.arrangedSubviews) {
            const NSPoint center = [control convertPoint:NSMakePoint(
                NSMidX(control.bounds), NSMidY(control.bounds)) toView:nil];
            NSView* hit = [window.contentView hitTest:center];
            if (hit != control) {
                everyTransportControlHit = false;
                std::cerr << "transport hit miss: "
                    << NSStringFromClass(control.class).UTF8String << ' '
                    << NSStringFromRect(control.frame).UTF8String
                    << " intercepted by "
                    << (hit ? NSStringFromClass(hit.class).UTF8String : "nil")
                    << '\n';
            }
        }
        check(everyTransportControlHit,
            "every relocated Transport control should own its visible center hit target");
        check([(S3GTrackerPopupButton*)patternPopup s3gUsesCanvasMenu]
                && [(S3GTrackerPopupButton*)tempoScalePopup
                    s3gUsesCanvasMenu]
                && [(S3GTrackerPopupButton*)midiStepRecordPopup
                    s3gUsesCanvasMenu]
                && [(S3GTrackerPopupButton*)midiRecordTrackPopup
                    s3gUsesCanvasMenu]
                && [(S3GTrackerPopupButton*)stepJumpPopup
                    s3gUsesCanvasMenu]
                && [(S3GTrackerActionButton*)playButton
                    s3gUsesSuiteStyle]
                && [(S3GTrackerActionButton*)playButton
                    s3gUsesNeutralTitle]
                && [(S3GTrackerActionButton*)renamePatternButton
                    s3gUsesNeutralTitle]
                && [(S3GTrackerActionButton*)deletePatternButton
                    s3gUsesNeutralTitle],
            "Tracker header menus and actions should use shared controls and one neutral title level");
        check(stepJumpPopup.numberOfItems == 16u
                && [stepJumpPopup.selectedItem.title
                    isEqualToString:@"JUMP 1"],
            "Tracker View should expose a one-through-sixteen row-jump menu defaulting to one");
        const int preferencesBeforeJump = viewPreferenceRequests;
        [stepJumpPopup selectItemAtIndex:2u];
        [stepJumpPopup sendAction:stepJumpPopup.action
            to:stepJumpPopup.target];
        state.session.selectedRow = 5u;
        [grid.documentView keyDown:keyEvent(window, @"", 125u, 0u)];
        const bool jumpedDown = state.session.selectedRow == 8u;
        [grid.documentView keyDown:keyEvent(window, @"", 126u, 0u)];
        const bool jumpedUp = state.session.selectedRow == 5u;
        state.songPlaybackActive = true;
        [grid.documentView keyDown:keyEvent(window, @"", 125u, 0u)];
        const bool jumpedDuringSong = state.session.selectedRow == 8u;
        state.songPlaybackActive = false;
        check(state.trackerRowJump == 3u && jumpedDown && jumpedUp
                && jumpedDuringSong
                && viewPreferenceRequests == preferencesBeforeJump + 1,
            "JUMP 3 should persist as a view preference and move Up/Down by three rows in editing and Song-follow views");
        [stepJumpPopup selectItemAtIndex:0u];
        [stepJumpPopup sendAction:stepJumpPopup.action
            to:stepJumpPopup.target];
        check(state.trackerRowJump == 1u
                && viewPreferenceRequests == preferencesBeforeJump + 2,
            "the row-jump menu should return navigation to one-row steps");
        [sequenceColumnsButton performClick:nil];
        [root layoutSubtreeIfNeeded];
        check(state.sequenceColumnsExpanded
                && [sequenceColumnsButton.title
                    isEqualToString:@"COLLAPSE DETAIL"]
                && near(NSMinX(sequenceColumnsButton.frame), initialSequenceX)
                && near(NSMinX(trackAddButton.frame), initialAddTrackX)
                && near(NSMinX(trackRemoveButton.frame), initialRemoveTrackX),
            "Expand Detail should reveal sequencing and gate columns without moving its direct toolbox controls");
        state.hostBpm = 128.25;
        state.tempoScale = 0.5;
        [controller reloadModel];
        check([tempoScalePopup.selectedItem.title isEqualToString:@"1/2×"],
            "transport should display the persisted musical rate");
        state.playing = true;
        [controller reloadModel];
        check([playButton.title isEqualToString:@"▶"]
                && playButton.state == NSControlStateValueOn
                && playButton.tag == 3,
            "active transport should retain the play glyph and engage its green state");
        state.playing = false;
        [controller reloadModel];
        check([playButton.title isEqualToString:@"▶"]
                && playButton.state == NSControlStateValueOff,
            "paused transport should retain the play glyph and return to gray");
        [tempoScalePopup selectItemAtIndex:4u];
        [tempoScalePopup sendAction:tempoScalePopup.action
            to:tempoScalePopup.target];
        check(std::abs(state.tempoScale - 1.5) < 1.0e-9
                && transportChangeRequests == 1,
            "rate menu should publish the selected musical host-tempo ratio");
        check(NSWidth(grid.documentView.frame) >
                NSWidth(grid.contentView.bounds) + 1000.0
                && grid.hasHorizontalScroller,
            "many tracks should widen the grid document and scroll");
        check(NSHeight(grid.documentView.frame) >
                NSHeight(grid.contentView.bounds)
                && grid.hasVerticalScroller,
            "many rows should heighten the grid document and scroll");
        const NSRect pinnedRowBefore = [rowGutter pinnedRectForGridRect:
            NSMakeRect(0.0, s3g::tracker::app::kTrackerGridHeaderHeight,
                s3g::tracker::app::kTrackerRowNumberWidth,
                s3g::tracker::app::kTrackerGridRowHeight)];
        const CGFloat frozenGutterX = NSMinX(rowGutter.frame);
        [grid.contentView scrollToPoint:NSMakePoint(420.0, 0.0)];
        [grid reflectScrolledClipView:grid.contentView];
        [root layoutSubtreeIfNeeded];
        const NSPoint gutterHitPoint = [rowGutter convertPoint:NSMakePoint(
            5.0, NSMidY(rowGutter.bounds)) toView:nil];
        const NSRect pinnedRowAfter = [rowGutter pinnedRectForGridRect:
            NSMakeRect(0.0, s3g::tracker::app::kTrackerGridHeaderHeight,
                s3g::tracker::app::kTrackerRowNumberWidth,
                s3g::tracker::app::kTrackerGridRowHeight)];
        check(rowGutter.superview == grid
                && near(NSMinX(rowGutter.frame), frozenGutterX)
                && near(NSMinX(rowGutter.frame),
                    NSMinX(grid.contentView.frame))
                && NSWidth(rowGutter.frame) >=
                    s3g::tracker::app::kTrackerRowNumberWidth - 1.0
                && [window.contentView hitTest:gutterHitPoint] == rowGutter,
            "the row-number gutter should remain frozen and interactive while lanes scroll horizontally");
        check(near(NSMinX(pinnedRowBefore), NSMinX(pinnedRowAfter), 0.01)
                && near(NSWidth(pinnedRowBefore), NSWidth(pinnedRowAfter),
                    0.01)
                && near(NSMinX(pinnedRowAfter), 0.0, 0.01),
            "horizontal scrolling should not move or redraw a second copy of the frozen row labels");
        const auto gutterRowPoint = [&](std::size_t row) {
            return [grid.documentView convertPoint:NSMakePoint(4.0,
                s3g::tracker::app::kTrackerGridHeaderHeight
                    + static_cast<CGFloat>(row)
                        * s3g::tracker::app::kTrackerGridRowHeight
                    + 4.0) toView:nil];
        };
        [rowGutter mouseDown:mouseDownEvent(window,
            gutterRowPoint(4u), 1)];
        [rowGutter mouseDragged:mouseDownEvent(window,
            gutterRowPoint(7u), 1)];
        [rowGutter mouseUp:mouseDownEvent(window,
            gutterRowPoint(7u), 1)];
        check(state.session.transport.loopStartRow == 4u
                && state.session.transport.loopEndRow == 8u
                && state.session.selectedRow == 7u,
            "the frozen row gutter should preserve drag-to-select loop behavior");

        auto& rowEditTrack = state.session.pattern.tracks[0u];
        for (std::size_t row = 7u; row <= 10u; ++row) {
            rowEditTrack.notes[row] = s3g::tracker::NoteCell::withNote(
                static_cast<uint8_t>(60u + row));
        }
        [rowGutter mouseDown:mouseEvent(window, NSEventTypeLeftMouseDown,
            gutterRowPoint(10u), NSEventModifierFlagShift)];
        [rowGutter mouseUp:mouseEvent(window, NSEventTypeLeftMouseUp,
            gutterRowPoint(10u), NSEventModifierFlagShift)];
        check([grid.documentView isWholeRowSelected:7u]
                && [grid.documentView isWholeRowSelected:10u]
                && ![grid.documentView isWholeRowSelected:6u]
                && state.session.selectedRow == 10u,
            "Shift-clicking a row number should select the inclusive range from the row anchor");
        NSEvent* rowMenuEvent = mouseEvent(window, NSEventTypeRightMouseDown,
            gutterRowPoint(8u), 0u);
        NSMenu* rowMenu = [rowGutter menuForEvent:rowMenuEvent];
        check(rowMenu.numberOfItems == 10u
                && [rowMenu.itemArray[0u].title
                    isEqualToString:@"INSERT 4 ROWS ABOVE"]
                && [rowMenu.itemArray[1u].title
                    isEqualToString:@"INSERT 4 ROWS BELOW"]
                && [rowMenu.itemArray[2u].title
                    isEqualToString:@"DELETE 4 ROWS"]
                && [rowMenu.itemArray[4u].title
                    isEqualToString:@"COPY 4 ROWS"]
                && !rowMenu.itemArray[5u].enabled
                && [rowMenu.itemArray[7u].title
                    isEqualToString:@"QUANTIZE MT TO ROW GRID"]
                && [rowMenu.itemArray[8u].title
                    isEqualToString:@"HUMANIZE HIT PLACEMENT"]
                && rowMenu.itemArray[8u].submenu.numberOfItems == 3u
                && [rowMenu.itemArray[8u].submenu.itemArray[0u].title
                    isEqualToString:@"10%"]
                && [rowMenu.itemArray[8u].submenu.itemArray[2u].title
                    isEqualToString:@"50%"]
                && [rowMenu.itemArray[9u].title
                    isEqualToString:@"RHYTHM"]
                && rowMenu.itemArray[9u].submenu.numberOfItems == 6u
                && rowMenu.itemArray[9u].submenu.itemArray[4u]
                    .submenu.numberOfItems == 3u
                && rowMenu.itemArray[9u].submenu.itemArray[5u]
                    .submenu.numberOfItems == 3u,
            "the row-number menu should keep structural and pattern-wide rhythm actions without column-owned pitch or velocity transforms");
        [NSApp sendAction:rowMenu.itemArray[4u].action
            to:rowMenu.itemArray[4u].target from:rowMenu.itemArray[4u]];
        rowMenu = [rowGutter menuForEvent:rowMenuEvent];
        const int changesBeforeRowPaste = patternChangeRequests;
        [NSApp sendAction:rowMenu.itemArray[5u].action
            to:rowMenu.itemArray[5u].target from:rowMenu.itemArray[5u]];
        check(state.session.pattern.visibleRows == 68u
                && state.session.pattern.tracks[0u].notes[7u].note == 67u
                && state.session.pattern.tracks[0u].notes[10u].note == 70u
                && patternChangeRequests == changesBeforeRowPaste + 1,
            "pasting copied rows should insert the complete multi-row range once across the pattern");
        rowMenu = [rowGutter menuForEvent:rowMenuEvent];
        [NSApp sendAction:rowMenu.itemArray[2u].action
            to:rowMenu.itemArray[2u].target from:rowMenu.itemArray[2u]];
        check(state.session.pattern.visibleRows == 64u
                && state.session.pattern.tracks[0u].notes[7u].note == 67u
                && state.session.pattern.tracks[0u].notes[10u].note == 70u,
            "deleting a selected row range should close the gap and preserve following pattern data");
        [rowGutter mouseDown:mouseEvent(window, NSEventTypeLeftMouseDown,
            gutterRowPoint(10u), NSEventModifierFlagShift)];
        [rowGutter mouseUp:mouseEvent(window, NSEventTypeLeftMouseUp,
            gutterRowPoint(10u), NSEventModifierFlagShift)];
        rowMenu = [rowGutter menuForEvent:rowMenuEvent];
        [NSApp sendAction:rowMenu.itemArray[0u].action
            to:rowMenu.itemArray[0u].target from:rowMenu.itemArray[0u]];
        check(state.session.pattern.visibleRows == 68u
                && state.session.pattern.tracks[0u].notes[7u].state
                    == s3g::tracker::NoteCellState::Rest
                && state.session.pattern.tracks[0u].notes[11u].note == 67u,
            "inserting from a multi-row selection should create the same number of blank rows above it");
        rowMenu = [rowGutter menuForEvent:rowMenuEvent];
        [NSApp sendAction:rowMenu.itemArray[2u].action
            to:rowMenu.itemArray[2u].target from:rowMenu.itemArray[2u]];
        check(state.session.pattern.visibleRows == 64u
                && state.session.pattern.tracks[0u].notes[7u].note == 67u,
            "deleting the inserted range should restore the original row positions");
        [rowGutter mouseDown:mouseEvent(window, NSEventTypeLeftMouseDown,
            gutterRowPoint(10u), NSEventModifierFlagShift)];
        [rowGutter mouseUp:mouseEvent(window, NSEventTypeLeftMouseUp,
            gutterRowPoint(10u), NSEventModifierFlagShift)];
        rowMenu = [rowGutter menuForEvent:rowMenuEvent];
        [NSApp sendAction:rowMenu.itemArray[1u].action
            to:rowMenu.itemArray[1u].target from:rowMenu.itemArray[1u]];
        check(state.session.pattern.visibleRows == 68u
                && state.session.pattern.tracks[0u].notes[7u].note == 67u
                && state.session.pattern.tracks[0u].notes[10u].note == 70u
                && state.session.pattern.tracks[0u].notes[11u].state
                    == s3g::tracker::NoteCellState::Rest,
            "inserting below a multi-row selection should preserve the selected rows and add the same number after them");
        NSEvent* insertedBelowMenuEvent = mouseEvent(window,
            NSEventTypeRightMouseDown, gutterRowPoint(12u), 0u);
        rowMenu = [rowGutter menuForEvent:insertedBelowMenuEvent];
        [NSApp sendAction:rowMenu.itemArray[2u].action
            to:rowMenu.itemArray[2u].target from:rowMenu.itemArray[2u]];
        check(state.session.pattern.visibleRows == 64u
                && state.session.pattern.tracks[0u].notes[7u].note == 67u,
            "deleting rows inserted below should restore the pattern");

        [grid.documentView selectWholeRowsFrom:7u to:10u];
        auto& rowEditPair = rowEditTrack.fxPairs[0u];
        rowEditPair.actions.resize(64u,
            s3g::tracker::FxActionCell::empty());
        rowEditPair.values.resize(64u,
            s3g::tracker::FxValueCell::previous());
        rowEditPair.actions[8u] = s3g::tracker::FxActionCell::sequencer(
            s3g::tracker::SequencerAction::MicroTime);
        rowEditPair.values[8u]
            = s3g::tracker::FxValueCell::withValue(0.2f);
        rowEditPair.actions[12u] = s3g::tracker::FxActionCell::sequencer(
            s3g::tracker::SequencerAction::MicroTime);
        rowEditPair.values[12u]
            = s3g::tracker::FxValueCell::withValue(0.9f);
        NSEvent* quantizeMenuEvent = mouseEvent(window,
            NSEventTypeRightMouseDown, gutterRowPoint(8u), 0u);
        rowMenu = [rowGutter menuForEvent:quantizeMenuEvent];
        const int changesBeforeQuantize = patternChangeRequests;
        [NSApp sendAction:rowMenu.itemArray[7u].action
            to:rowMenu.itemArray[7u].target from:rowMenu.itemArray[7u]];
        check(near(rowEditPair.values[8u].normalized, 0.5, 0.0001),
            "row quantize should reset MT inside the selected row range");
        check(near(rowEditPair.values[12u].normalized, 0.9, 0.0001),
            "row quantize should preserve MT outside the selected row range");
        check(patternChangeRequests == changesBeforeQuantize + 1,
            "row quantize should commit one edit");

        const int changesBeforeSubmenus = patternChangeRequests;
        state.session.selectedTrack = 0u;
        state.session.selectedField = 0u;
        state.session.selectedRow = 7u;
        [grid.documentView beginGridSelectionAtTrack:0u
            field:0u row:7u page:0u];
        const BOOL shiftExtendedColumn = [grid.documentView
            extendGridSelectionInColumnToTrack:0u field:0u row:10u];
        const auto shiftRange = [grid.documentView effectiveGridSelection];
        const BOOL rejectedCrossColumnShift = ![grid.documentView
            extendGridSelectionInColumnToTrack:0u field:1u row:12u];
        check(shiftExtendedColumn && rejectedCrossColumnShift
                && shiftRange.firstTrack == 0u && shiftRange.lastTrack == 0u
                && shiftRange.firstField == 0u && shiftRange.lastField == 0u
                && shiftRange.firstRow == 7u && shiftRange.lastRow == 10u,
            "Shift-click selection should extend vertically inside the current Tracker column and reject cross-column ranges");
        NSMenu* noteMenu = [grid.documentView noteMenuForTrack:0u row:8u];
        check(noteMenu.numberOfItems == 4u
                && [noteMenu.itemArray[0u].title isEqualToString:@"PITCH"]
                && noteMenu.itemArray[0u].submenu.numberOfItems == 9u
                && [noteMenu.itemArray[1u].title isEqualToString:@"BURST"]
                && [noteMenu.itemArray[3u].title hasPrefix:@"SELECTION"],
            "a NOTE-column selection should own Pitch and Burst context actions");
        NSMenuItem* transposeUp
            = noteMenu.itemArray[0u].submenu.itemArray[0u];
        [NSApp sendAction:transposeUp.action to:transposeUp.target
            from:transposeUp];
        check(rowEditTrack.notes[7u].note == 68u
                && rowEditTrack.notes[10u].note == 71u
                && patternChangeRequests == changesBeforeSubmenus + 1,
            "the NOTE-column Pitch submenu should transpose only the selected lane and rows in one edit");

        rowEditTrack.velocities[8u]
            = s3g::tracker::ValueCell::withValue(0.8f);
        [grid.documentView beginGridSelectionAtTrack:0u
            field:1u row:7u page:0u];
        [grid.documentView extendGridSelectionToTrack:0u
            field:1u row:10u];
        NSMenu* velocityMenu = [grid.documentView
            velocityMenuForTrack:0u row:8u];
        check(velocityMenu.numberOfItems == 10u
                && [velocityMenu.itemArray[9u].title hasPrefix:@"SELECTION"],
            "a VOL-column selection should expose velocity transforms directly");
        NSMenuItem* scaleVelocity = velocityMenu.itemArray[0u];
        [NSApp sendAction:scaleVelocity.action to:scaleVelocity.target
            from:scaleVelocity];
        check(near(rowEditTrack.velocities[8u].normalized, 0.6, 0.0001)
                && patternChangeRequests == changesBeforeSubmenus + 2,
            "the VOL-column menu should scale written values only inside its lane selection");

        [grid.documentView selectWholeRowsFrom:7u to:10u];
        rowMenu = [rowGutter menuForEvent:quantizeMenuEvent];
        NSMenuItem* reverseNotes
            = rowMenu.itemArray[9u].submenu.itemArray[0u];
        [NSApp sendAction:reverseNotes.action to:reverseNotes.target
            from:reverseNotes];
        check(rowEditTrack.notes[7u].note == 71u
                && rowEditTrack.notes[10u].note == 68u
                && patternChangeRequests == changesBeforeSubmenus + 3,
            "the row Rhythm submenu should reverse NOTE cells inside the selected range in one edit");
        [grid.contentView scrollToPoint:NSZeroPoint];
        [grid reflectScrolledClipView:grid.contentView];
        patternChangeRequests = 0;
        const CGFloat laneWidth = (NSWidth(grid.documentView.bounds)
                - s3g::tracker::app::kTrackerRowNumberWidth
                - 11.0 * s3g::tracker::app::kTrackerLaneGutter) / 12.0;
        const CGFloat firstFieldCenterX
            = s3g::tracker::app::kTrackerRowNumberWidth + 3.0
                + (laneWidth - 6.0) * 0.095;
        const CGFloat firstLaneSyncCenterX
            = s3g::tracker::app::kTrackerRowNumberWidth + 3.0
                + (laneWidth - 6.0) - 70.0;
        const auto headerClick = [&](CGFloat y, NSInteger clicks) {
            const NSPoint inWindow = [grid.documentView convertPoint:
                NSMakePoint(firstFieldCenterX, y) toView:nil];
            [grid.documentView mouseDown:mouseDownEvent(
                window, inWindow, clicks)];
        };
        const bool initiallyMuted
            = state.session.pattern.tracks[0u].noteColumn.muted;
        const NSPoint syncInWindow = [grid.documentView convertPoint:
            NSMakePoint(firstLaneSyncCenterX, 12.0) toView:nil];
        [grid.documentView mouseDown:mouseDownEvent(
            window, syncInWindow, 1)];
        check(trackResyncRequests == 1 && resyncedTrack == 0u
                && patternChangeRequests == 0,
            "the lane-header SYNC control should target only that track without editing the pattern");
        headerClick(40.0, 1);
        check(state.session.pattern.tracks[0u].noteColumn.muted
                    == initiallyMuted
                && patternChangeRequests == 0,
            "clicking the dedicated length row must not toggle column mute");
        headerClick(40.0, 2);
        NSTextField* columnHeaderEditor = [grid.documentView
            valueForKey:@"cellEditor"];
        check([columnHeaderEditor.accessibilityLabel
                    isEqualToString:@"Column length and stride"],
            "double-clicking length should open the direct column editor");
        columnHeaderEditor.stringValue = @"24x2";
        [columnHeaderEditor sendAction:columnHeaderEditor.action
            to:columnHeaderEditor.target];
        check(state.session.pattern.tracks[0u].noteColumn.length == 24u
                && state.session.pattern.tracks[0u].noteColumn.stride == 2u
                && patternChangeRequests == 1,
            "length entry should accept compact length x stride notation");
        headerClick(53.0, 2);
        columnHeaderEditor = [grid.documentView valueForKey:@"cellEditor"];
        check([columnHeaderEditor.accessibilityLabel
                    isEqualToString:@"Column read start row"],
            "double-clicking Read should open its direct column editor");
        columnHeaderEditor.stringValue = @"7";
        [columnHeaderEditor sendAction:columnHeaderEditor.action
            to:columnHeaderEditor.target];
        check(state.session.pattern.tracks[0u].noteColumn.phase == 6u
                && patternChangeRequests == 2,
            "Read should store a one-based start row as the column phase");
        patternChangeRequests = 0;
        const auto initialDirection
            = state.session.pattern.tracks[0u].noteColumn.direction;
        headerClick(66.0, 1);
        check(state.session.pattern.tracks[0u].noteColumn.direction
                    != initialDirection
                && state.session.pattern.tracks[0u].noteColumn.muted
                    == initiallyMuted
                && patternChangeRequests == 1,
            "the dedicated direction row should cycle without toggling mute");
        headerClick(66.0, 1);
        headerClick(66.0, 1);
        headerClick(66.0, 1);
        check(state.session.pattern.tracks[0u].noteColumn.direction
                    == initialDirection
                && patternChangeRequests == 4,
            "four direction-row clicks should cycle back to the original mode");
        headerClick(79.0, 1);
        check(state.session.pattern.tracks[0u].noteColumn.muted
                    != initiallyMuted
                && patternChangeRequests == 5,
            "the dedicated MUTE row should be the column mute mouse target");
        headerClick(79.0, 1);
        check(state.session.pattern.tracks[0u].noteColumn.muted
                    == initiallyMuted,
            "the dedicated MUTE row should toggle independently");
        patternChangeRequests = 0;
        check(NSWidth(transport.documentView.frame) >
                NSWidth(transport.contentView.bounds) + 200.0
                && transport.hasHorizontalScroller,
            "pattern, transport, and input/view toolboxes should remain scrollable");
        check(patternPopup.numberOfItems == 2u && patternPopup.enabled
                && patternPopup.target == controller
                && patternPopup.action == @selector(patternSelectionChanged:),
            "compact pattern popup should expose both bank entries");
        check([patternPopup isKindOfClass:S3GTrackerPopupButton.class]
                && patternPopup.menu.font != nil
                && near(patternPopup.intrinsicContentSize.height, 26.0),
            "tracker popups should share centered mono menu typography");
        const double swingBefore = state.session.transport.swing;
        const BOOL swingAdjusted = [swingField adjustByScrollDelta:1.0
            modifierFlags:0u];
        check(swingAdjusted
                && near(state.session.transport.swing,
                    swingBefore + 0.005, 0.0001),
            "toolbar Swing should reuse Song's drag/scroll slider behavior");
        state.patternBank.entries[1u].pattern.visibleRows = 96u;
        [controller reloadModel];
        [gateField selectItemAtIndex:[gateField
            indexOfItemWithRepresentedObject:@100.0]];
        [gateField sendAction:gateField.action to:gateField.target];
        [loopStartField selectItemAtIndex:[loopStartField
            indexOfItemWithRepresentedObject:@4]];
        [loopStartField sendAction:loopStartField.action
            to:loopStartField.target];
        [loopEndField selectItemAtIndex:[loopEndField
            indexOfItemWithRepresentedObject:@12]];
        [loopEndField sendAction:loopEndField.action
            to:loopEndField.target];
        check(near(state.session.gateMilliseconds, 100.0, 0.0001)
                && state.session.transport.loopStartRow == 3u
                && state.session.transport.loopEndRow == 12u
                && [loopEndField indexOfItemWithRepresentedObject:@3] < 0
                && [loopEndField indexOfItemWithRepresentedObject:@96] >= 0
                && [loopEndField indexOfItemWithRepresentedObject:@97] < 0,
            "Gate and conditioned loop boundaries should publish from compact menus, capped by the longest loaded pattern");
        state.patternBank.entries[1u].pattern.visibleRows = 32u;
        [controller reloadModel];
        check([root isKindOfClass:S3GTrackerFocusReleaseView.class]
                && [controller valueForKey:@"toolbar"] != nil,
            "workspace backgrounds should support click-away field release");
        check(restartButton != nil
                && [restartButton.title isEqualToString:@"SYNC ALL"]
                && [restartButton.accessibilityLabel
                    containsString:@"Sync all tracker lanes"],
            "embedded transport should expose a global row-one synchronization control");
        [restartButton sendAction:restartButton.action to:restartButton.target];
        check(restartRequests == 1,
            "SYNC ALL should dispatch an internal scheduler synchronization request");
        check(S3GTrackerThemeRGB(S3GTrackerThemeRole::GridPlayback)
                    == 0x2e412e
                && S3GTrackerThemeRGB(
                    S3GTrackerThemeRole::GridPlaybackAccent) == 0x69826b
                && S3GTrackerThemeRGB(S3GTrackerThemeRole::GridSelection)
                    == 0x303854
                && S3GTrackerThemeRGB(S3GTrackerThemeRole::GridCursor)
                    == 0x4d4d6b,
            "tracker playback and selection cells should retain the v8 semantic palette");
        [patternPopup selectItemAtIndex:1];
        [patternPopup sendAction:patternPopup.action to:patternPopup.target];
        check(selectedPattern == "A02",
            "compact pattern popup should dispatch pattern selection");
        state.songPlaybackActive = true;
        [controller reloadModel];
        check(!patternPopup.enabled && !createPatternButton.enabled
                && !duplicatePatternButton.enabled
                && !renamePatternButton.enabled
                && !deletePatternButton.enabled,
            "active Song playback should lock every pattern-bank control");
        [createPatternButton sendAction:createPatternButton.action
            to:createPatternButton.target];
        [duplicatePatternButton sendAction:duplicatePatternButton.action
            to:duplicatePatternButton.target];
        [renamePatternButton sendAction:renamePatternButton.action
            to:renamePatternButton.target];
        check(addPatternRequests == 0 && renamePatternRequests == 0
                && deletePatternRequests == 0,
            "locked pattern-bank actions should not dispatch callbacks");
        state.songPlaybackActive = false;
        [controller reloadModel];
        check(patternPopup.enabled && createPatternButton.enabled
                && duplicatePatternButton.enabled
                && renamePatternButton.enabled
                && deletePatternButton.enabled,
            "pattern-bank controls should unlock after Song playback");
        const CGFloat toolboxDocumentWidth = NSWidth(
            transport.documentView.frame);
        check(toolboxDocumentWidth > 0.0
                && NSMaxX(inputViewPanel.frame)
                    <= toolboxDocumentWidth + 1.0
                && (toolboxDocumentWidth
                        <= NSWidth(transport.contentView.bounds) + 1.0
                    || transport.hasHorizontalScroller),
            "tracker toolboxes should fit or remain horizontally scrollable");
        check(!grid.hasAmbiguousLayout && !envelope.hasAmbiguousLayout,
            "compact workspace constraints should be unambiguous");

        NSMenuItem* laneChannel = [[NSMenuItem alloc] initWithTitle:@"CH 07"
            action:nil keyEquivalent:@""];
        laneChannel.representedObject = @{ @"track": @3, @"channel": @7 };
        [grid.documentView laneMidiChannelSelected:laneChannel];
        check(state.session.pattern.tracks[3u].midiChannel == 7u
                && state.session.pattern.tracks[2u].midiChannel == 1u,
            "lane channel menu should change only the targeted track");

        [grid.documentView beginTrackNameEditingForTrack:2u
            rect:NSMakeRect(40.0, 3.0, 110.0, 18.0)];
        NSTextField* trackNameEditor = [grid.documentView
            valueForKey:@"cellEditor"];
        trackNameEditor.stringValue = @"BREAKS A";
        [trackNameEditor sendAction:trackNameEditor.action
            to:trackNameEditor.target];
        check(state.session.pattern.tracks[2u].name == "BREAKS A"
                && patternChangeRequests == 2,
            "lane-name editor should commit a renamed track and publish it");

        NSMenu* sequenceMenu = [grid.documentView
            sequenceActionMenuForTrack:0u row:4u field:2u];
        NSMenuItem* flamItem = nil;
        for (NSMenuItem* item in sequenceMenu.itemArray) {
            NSDictionary* represented = item.representedObject;
            if (![represented isKindOfClass:NSDictionary.class]
                || ![represented[@"kind"] isEqualToString:@"action"])
                continue;
            const auto* action = s3g::tracker::sequencerAction(
                [represented[@"action"] unsignedIntegerValue]);
            if (action && action->action
                    == s3g::tracker::SequencerAction::Flam) {
                flamItem = item;
                break;
            }
        }
        NSMenuItem* midiControlItem = nil;
        for (NSMenuItem* item in sequenceMenu.itemArray)
            if ([item.title isEqualToString:@"MIDI CONTROL CHANGE"])
                midiControlItem = item;
        check(sequenceMenu.numberOfItems
                    == static_cast<NSInteger>(
                        s3g::tracker::sequencerActionCount() + 9u)
                && sequenceMenu.font != nil && flamItem != nil
                && [flamItem.title containsString:@"FL"]
                && [flamItem.title containsString:@"FLAM"]
                && midiControlItem.submenu.numberOfItems == 4u
                && midiControlItem.submenu.itemArray[0u]
                    .submenu.numberOfItems == 32u,
            "SEQ context menu should expose every named action and MIDI CC");
        [grid.documentView sequenceActionSelected:flamItem];
        const auto& chosenPair = state.session.pattern.tracks[0u].fxPairs[0u];
        check(chosenPair.actions.size() > 4u
                && chosenPair.actions[4u].state
                    == s3g::tracker::FxActionCellState::Sequencer
                && chosenPair.actions[4u].sequencerAction
                    == s3g::tracker::SequencerAction::Flam
                && chosenPair.values.size() > 4u
                && chosenPair.values[4u].state
                    == s3g::tracker::FxValueCellState::Value
                && near(chosenPair.values[4u].normalized, 0.5)
                && patternChangeRequests == 3,
            "choosing a SEQ action should author it with a visible default value");

        NSMenuItem* conditionActionItem = nil;
        for (NSMenuItem* item in sequenceMenu.itemArray) {
            NSDictionary* represented = item.representedObject;
            if (![represented isKindOfClass:NSDictionary.class]
                || ![represented[@"kind"] isEqualToString:@"action"])
                continue;
            const auto* action = s3g::tracker::sequencerAction(
                [represented[@"action"] unsignedIntegerValue]);
            if (action && action->action
                    == s3g::tracker::SequencerAction::Condition) {
                conditionActionItem = item;
                break;
            }
        }
        conditionActionItem.representedObject = @{
            @"track": @0, @"row": @5, @"field": @2,
            @"kind": @"action",
            @"action": @(static_cast<NSUInteger>(
                s3g::tracker::SequencerAction::Condition)),
        };
        [grid.documentView sequenceActionSelected:conditionActionItem];
        NSMenu* conditionMenu = [grid.documentView
            sequenceConditionMenuForTrack:0u row:5u field:3u];
        NSMenuItem* lastItem = nil;
        for (NSMenuItem* item in conditionMenu.itemArray) {
            NSDictionary* represented = item.representedObject;
            if ([represented[@"condition"] unsignedIntegerValue]
                    == static_cast<NSUInteger>(
                        s3g::tracker::SequencerCondition::Last)) {
                lastItem = item;
                break;
            }
        }
        [grid.documentView sequenceConditionSelected:lastItem];
        const auto& conditionPair = state.session.pattern.tracks[0u]
            .fxPairs[0u];
        check(conditionMenu.numberOfItems
                == static_cast<NSInteger>(
                        s3g::tracker::kSequencerConditionCount + 8u)
                && lastItem != nil
                && s3g::tracker::sequencerConditionFromNormalized(
                    conditionPair.values[5u].normalized)
                    == s3g::tracker::SequencerCondition::Last,
            "CD value cells should expose and author the complete contextual condition menu");

        NSPasteboard* pasteboard = NSPasteboard.generalPasteboard;
        [pasteboard clearContents];
        NSEvent* commandCopy = keyEvent(window, @"c", 8u,
            NSEventModifierFlagCommand);
        const BOOL commandCopyHandled =
            [grid.documentView performKeyEquivalent:commandCopy];
        check(!commandCopyHandled,
            "Command-C should report unhandled for REAPER");
        NSEvent* commandPaste = keyEvent(window, @"v", 9u,
            NSEventModifierFlagCommand);
        check(![grid.documentView performKeyEquivalent:commandPaste],
            "Command-V should report unhandled for REAPER");
        NSEvent* commandUndo = keyEvent(window, @"z", 6u,
            NSEventModifierFlagCommand);
        check(![grid.documentView performKeyEquivalent:commandUndo],
            "Command-Z should remain available to REAPER");

        state.canUndo = true;
        state.canRedo = true;
        [controller reloadModel];
        check(undoButton.enabled && redoButton.enabled,
            "history buttons should reflect coordinator availability");
        [undoButton performClick:nil];
        [redoButton performClick:nil];
        NSEvent* trackerUndo = keyEvent(window, @"z", 6u,
            NSEventModifierFlagControl);
        NSEvent* trackerRedo = keyEvent(window, @"Z", 6u,
            NSEventModifierFlagControl | NSEventModifierFlagShift);
        check([grid.documentView performKeyEquivalent:trackerUndo]
                && [grid.documentView performKeyEquivalent:trackerRedo]
                && commands.size() >= 4u
                && commands[commands.size() - 4u] == "undo"
                && commands[commands.size() - 3u] == "redo"
                && commands[commands.size() - 2u] == "undo"
                && commands[commands.size() - 1u] == "redo",
            "buttons and Control-Z/Control-Shift-Z should request Tracker history");

        const NSInteger clipboardBefore = [[grid.documentView
            valueForKey:@"copiedPasteboardChangeCount"] integerValue];
        NSEvent* trackerCopy = keyEvent(window, @"c", 8u,
            NSEventModifierFlagControl);
        check([grid.documentView performKeyEquivalent:trackerCopy]
                && [[grid.documentView
                    valueForKey:@"copiedPasteboardChangeCount"] integerValue]
                    != clipboardBefore,
            "Control-C should invoke tracker-local copy");

        NSEvent* trackerPaste = keyEvent(window, @"v", 9u,
            NSEventModifierFlagControl);
        const BOOL trackerPasteHandled =
            [grid.documentView performKeyEquivalent:trackerPaste];
        check(trackerPasteHandled,
            "Control-V should report handled by the tracker");

        grid.magnification = 1.0;
        [zoomOutButton performClick:nil];
        const CGFloat zoomedOut = grid.magnification;
        check([zoomActualButton.title isEqualToString:@"86%"],
            "View toolbox should report the current Tracker zoom percentage");
        [zoomInButton performClick:nil];
        check(zoomedOut < 1.0 && grid.magnification > zoomedOut,
            "View toolbox zoom buttons should change Tracker spreadsheet magnification");
        grid.magnification = 1.2;
        [zoomActualButton performClick:nil];
        check(near(grid.magnification, 1.0, 0.001),
            "100% should restore the spreadsheet to 100 percent magnification");
        grid.magnification = 1.2;
        [controller resetTrackerZoom];
        check(near(grid.magnification,
                s3g::tracker::app::kTrackerDefaultMagnification, 0.001),
            "tracker zoom reset should restore literal 100 percent magnification");
        state.songPlaybackActive = true;
        const CGFloat songZoomBefore = grid.magnification;
        NSEvent* trackerZoomIn = keyEvent(window, @"+", 24u,
            NSEventModifierFlagControl);
        check([grid.documentView performKeyEquivalent:trackerZoomIn]
                && grid.magnification > songZoomBefore,
            "Control-plus should zoom directly during Song playback without forwarding recursively through the host");
        NSEvent* trackerZoomReset = keyEvent(window, @"0", 29u,
            NSEventModifierFlagControl);
        check([grid.documentView performKeyEquivalent:trackerZoomReset]
                && near(grid.magnification,
                    s3g::tracker::app::kTrackerDefaultMagnification, 0.001),
            "Control-zero should reset zoom safely during Song playback");
        state.songPlaybackActive = false;

        state.session.selectedTrack = 0u;
        state.session.selectedRow = 1u;
        state.session.selectedField = 0u;
        const int changesBeforeHold = patternChangeRequests;
        [grid.documentView keyDown:keyEvent(window, @"h", 4u, 0u)];
        check(state.session.pattern.tracks[0u].notes[1u].state
                    == s3g::tracker::NoteCellState::Hold
                && state.session.selectedRow == 2u
                && patternChangeRequests == changesBeforeHold + 1,
            "H should write HLD in a NOTE cell and advance one row");

        auto& stackTrack = state.session.pattern.tracks[0u];
        check([grid.documentView applyCellText:@"60+64+67"
                    toTrack:stackTrack row:5u page:0u field:0u]
                && [grid.documentView applyCellText:@"0.866+0.646+0.756"
                    toTrack:stackTrack row:5u page:0u field:1u]
                && [grid.documentView applyCellText:@"MT"
                    toTrack:stackTrack row:5u page:0u field:2u]
                && [grid.documentView applyCellText:@"0.200+0.500+0.800"
                    toTrack:stackTrack row:5u page:0u field:3u]
                && stackTrack.notes[5u].noteVoiceCount() == 3u
                && stackTrack.notes[5u].noteVoice(1u) == 64u
                && stackTrack.velocities[5u].valueVoiceCount() == 3u
                && std::abs(stackTrack.velocities[5u].valueVoice(1u)
                    - 0.646f) < 0.00001f
                && stackTrack.fxPairs[0u].values[5u].valueVoiceCount() == 3u
                && near(stackTrack.fxPairs[0u].values[5u]
                    .valueVoice(2u), 0.8)
                && [[grid.documentView cellTextForTrack:0u row:5u
                    page:0u field:3u] isEqualToString:@"0.200+2"]
                && [[grid.documentView clipboardTextForTrack:0u row:5u
                    page:0u field:3u]
                    isEqualToString:@"0.200+0.500+0.800"],
            "NOTE/VOL/MT inline expressions should create aligned stacks with compact grid and lossless clipboard text");

        std::array<uint8_t, s3g::tracker::kMaximumNoteVoices>
            copiedChord {};
        const bool clipboardExpandedBefore = state.sequenceColumnsExpanded;
        state.sequenceColumnsExpanded = false;
        copiedChord[0u] = 52u;
        copiedChord[1u] = 62u;
        copiedChord[2u] = 64u;
        std::array<float, s3g::tracker::kMaximumNoteVoices>
            copiedVelocities {};
        copiedVelocities[0u] = 0.9f;
        copiedVelocities[1u] = 0.7f;
        copiedVelocities[2u] = 0.5f;
        auto& clipboardLaneA = state.session.pattern.tracks[6u];
        auto& clipboardLaneB = state.session.pattern.tracks[7u];
        clipboardLaneA.notes.resize(64u,
            s3g::tracker::NoteCell::rest());
        clipboardLaneA.velocities.resize(64u,
            s3g::tracker::ValueCell::defaultValue());
        clipboardLaneB.notes.resize(64u,
            s3g::tracker::NoteCell::rest());
        clipboardLaneB.velocities.resize(64u,
            s3g::tracker::ValueCell::defaultValue());
        clipboardLaneA.notes[40u] = s3g::tracker::NoteCell::withNotes(
            copiedChord, 3u);
        clipboardLaneA.velocities[40u]
            = s3g::tracker::ValueCell::withValues(copiedVelocities, 3u);
        clipboardLaneA.notes[41u]
            = s3g::tracker::NoteCell::withNote(53u);
        clipboardLaneA.velocities[41u]
            = s3g::tracker::ValueCell::withValue(0.6f);
        clipboardLaneB.notes[40u]
            = s3g::tracker::NoteCell::withNote(70u);
        clipboardLaneB.velocities[40u]
            = s3g::tracker::ValueCell::withValue(0.8f);
        clipboardLaneB.notes[41u]
            = s3g::tracker::NoteCell::withNote(72u);
        clipboardLaneB.velocities[41u]
            = s3g::tracker::ValueCell::withValue(0.4f);
        [grid.documentView beginGridSelectionAtTrack:6u
            field:0u row:40u page:0u];
        [grid.documentView extendGridSelectionToTrack:7u
            field:1u row:41u];
        const int changesBeforeRectangularCut = patternChangeRequests;
        [grid.documentView trackerCut:nil];
        NSString* rectangularClipboard = [grid.documentView
            valueForKey:@"copiedClipboardText"];
        check([rectangularClipboard hasPrefix:
                    @"52+62+64\t0.900+0.700+0.500\t70\t0.800\n"]
                && state.session.pattern.tracks[6u].notes[40u].state
                    == s3g::tracker::NoteCellState::Rest
                && state.session.pattern.tracks[6u].velocities[41u].state
                    == s3g::tracker::ValueCellState::Default
                && state.session.pattern.tracks[7u].notes[41u].state
                    == s3g::tracker::NoteCellState::Rest
                && patternChangeRequests
                    == changesBeforeRectangularCut + 1,
            "Cut should export lossless chord/velocity text and clear a multi-row, multi-lane column rectangle");
        state.session.selectedTrack = 8u;
        state.session.selectedRow = 50u;
        state.session.selectedField = 0u;
        [grid.documentView beginGridSelectionAtTrack:8u
            field:0u row:50u page:0u];
        [grid.documentView trackerPaste:nil];
        const auto& pastedLaneA = state.session.pattern.tracks[8u];
        const auto& pastedLaneB = state.session.pattern.tracks[9u];
        check(pastedLaneA.notes[50u].noteVoiceCount() == 3u
                && pastedLaneA.notes[50u].noteVoice(0u) == 52u
                && pastedLaneA.notes[50u].noteVoice(1u) == 62u
                && pastedLaneA.notes[50u].noteVoice(2u) == 64u
                && pastedLaneA.velocities[50u].valueVoiceCount() == 3u
                && std::abs(pastedLaneA.velocities[50u].valueVoice(1u)
                    - 0.7f) < 0.00001f
                && pastedLaneA.notes[51u].note == 53u
                && pastedLaneB.notes[50u].note == 70u
                && pastedLaneB.notes[51u].note == 72u,
            "Paste should anchor the copied shape at the selected leftmost cell and restore all rows, columns, lanes, and chord voices");
        state.session.selectedTrack = 8u;
        state.session.selectedRow = 55u;
        state.session.selectedField = 1u;
        [grid.documentView beginGridSelectionAtTrack:8u
            field:1u row:55u page:0u];
        const int changesBeforeIncompatiblePaste = patternChangeRequests;
        [grid.documentView trackerPaste:nil];
        check(patternChangeRequests == changesBeforeIncompatiblePaste
                && (pastedLaneA.velocities.size() <= 55u
                    || pastedLaneA.velocities[55u].state
                        == s3g::tracker::ValueCellState::Default),
            "Paste should reject a NOTE/VOL shape when its left edge is aimed at an incompatible column type");

        const auto patternBeforeSelectionTools = state.session.pattern;
        auto& seriesTrack = state.session.pattern.tracks[4u];
        std::array<uint8_t, s3g::tracker::kMaximumNoteVoices>
            seriesStart {};
        seriesStart[0u] = 60u;
        seriesStart[1u] = 64u;
        std::array<uint8_t, s3g::tracker::kMaximumNoteVoices>
            seriesEnd {};
        seriesEnd[0u] = 66u;
        seriesEnd[1u] = 70u;
        seriesTrack.notes[20u] = s3g::tracker::NoteCell::withNotes(
            seriesStart, 2u);
        seriesTrack.notes[21u] = s3g::tracker::NoteCell::rest();
        seriesTrack.notes[22u] = s3g::tracker::NoteCell::rest();
        seriesTrack.notes[23u] = s3g::tracker::NoteCell::withNotes(
            seriesEnd, 2u);
        [grid.documentView beginGridSelectionAtTrack:4u
            field:0u row:20u page:0u];
        [grid.documentView extendGridSelectionToTrack:4u
            field:0u row:23u];
        NSMenu* seriesMenu = [grid.documentView
            noteMenuForTrack:4u row:21u];
        NSMenuItem* seriesSelection = seriesMenu.itemArray.lastObject;
        NSMenuItem* fillRepeat = seriesSelection.submenu.itemArray[0u];
        NSMenuItem* linearSeries = fillRepeat.submenu.itemArray[2u];
        [NSApp sendAction:linearSeries.action to:linearSeries.target
            from:linearSeries];
        check(state.session.pattern.tracks[4u].notes[21u].noteVoiceCount()
                    == 2u
                && state.session.pattern.tracks[4u].notes[21u].noteVoice(0u)
                    == 62u
                && state.session.pattern.tracks[4u].notes[22u].noteVoice(1u)
                    == 68u,
            "Fill Series should interpolate complete chord voicings between selected endpoints");
        auto& materializeTrack = state.session.pattern.tracks[4u];
        materializeTrack.velocities[20u]
            = s3g::tracker::ValueCell::withValue(0.5f);
        materializeTrack.velocities[21u]
            = s3g::tracker::ValueCell::previous();
        materializeTrack.velocities[22u]
            = s3g::tracker::ValueCell::defaultValue();
        [grid.documentView beginGridSelectionAtTrack:4u
            field:1u row:20u page:0u];
        [grid.documentView extendGridSelectionToTrack:4u
            field:1u row:22u];
        NSMenu* materializeMenu = [grid.documentView
            velocityMenuForTrack:4u row:21u];
        NSMenuItem* materializeSelection = materializeMenu.itemArray.lastObject;
        NSMenuItem* materializeTransform =
            materializeSelection.submenu.itemArray[3u];
        NSMenuItem* materializeValues = [materializeTransform.submenu
            itemWithTitle:@"MATERIALIZE PRV / DEF"];
        [NSApp sendAction:materializeValues.action
            to:materializeValues.target from:materializeValues];
        check(state.session.pattern.tracks[4u].velocities[21u].state
                    == s3g::tracker::ValueCellState::Value
                && near(state.session.pattern.tracks[4u]
                    .velocities[21u].normalized, 0.5)
                && near(state.session.pattern.tracks[4u]
                    .velocities[22u].normalized, 0.787, 0.0001),
            "Materialize should replace VOL Previous and Default cells with their resolved explicit values");
        state.session.pattern = patternBeforeSelectionTools;

        const auto patternBeforeDrumSplit = state.session.pattern;
        const auto defaultsBeforeDrumSplit = state.session.laneDefaultNotes;
        const auto laneCountBeforeDrumSplit = state.session.pattern.tracks.size();
        auto& mixedDrums = state.session.pattern.tracks[6u];
        mixedDrums.name = "LIVE DRUMS";
        mixedDrums.notes[30u] = s3g::tracker::NoteCell::withNote(36u);
        mixedDrums.notes[31u] = s3g::tracker::NoteCell::withNote(38u);
        std::array<uint8_t, s3g::tracker::kMaximumNoteVoices>
            layeredDrums {};
        layeredDrums[0u] = 36u;
        layeredDrums[1u] = 42u;
        mixedDrums.notes[32u] = s3g::tracker::NoteCell::withNotes(
            layeredDrums, 2u);
        std::array<float, s3g::tracker::kMaximumNoteVoices>
            layeredDrumVelocity {};
        layeredDrumVelocity[0u] = 0.8f;
        layeredDrumVelocity[1u] = 0.4f;
        mixedDrums.velocities[32u] = s3g::tracker::ValueCell::withValues(
            layeredDrumVelocity, 2u);
        auto& drumTiming = mixedDrums.fxPairs[0u];
        drumTiming.actionColumn.length = 48u;
        drumTiming.actionColumn.stride = 3u;
        drumTiming.actionColumn.phase = 2u;
        drumTiming.valueColumn.length = 40u;
        drumTiming.valueColumn.direction =
            s3g::tracker::Direction::Palindrome;
        drumTiming.actions[30u]
            = s3g::tracker::FxActionCell::sequencer(
                s3g::tracker::SequencerAction::MicroTime);
        drumTiming.values[30u]
            = s3g::tracker::FxValueCell::withValue(0.25f);
        drumTiming.actions[31u]
            = s3g::tracker::FxActionCell::previous();
        drumTiming.values[31u]
            = s3g::tracker::FxValueCell::previous();
        drumTiming.actions[32u]
            = s3g::tracker::FxActionCell::sequencer(
                s3g::tracker::SequencerAction::Flam);
        drumTiming.values[32u]
            = s3g::tracker::FxValueCell::withValue(0.75f);
        auto& drumAutomation = mixedDrums.fxPairs[1u];
        drumAutomation.actions[31u]
            = s3g::tracker::FxActionCell::midiControlChange(74u);
        drumAutomation.values[31u]
            = s3g::tracker::FxValueCell::withValue(0.6f);
        drumAutomation.actions[32u]
            = s3g::tracker::FxActionCell::sequencer(
                s3g::tracker::SequencerAction::MicroTime);
        std::array<float, s3g::tracker::kMaximumNoteVoices>
            layeredDrumMicroTime {};
        layeredDrumMicroTime[0u] = 0.2f;
        layeredDrumMicroTime[1u] = 0.8f;
        drumAutomation.values[32u]
            = s3g::tracker::FxValueCell::withValues(
                layeredDrumMicroTime, 2u);
        [grid.documentView beginGridSelectionAtTrack:6u
            field:0u row:30u page:0u];
        [grid.documentView extendGridSelectionToTrack:6u
            field:0u row:32u];
        NSMenu* drumSplitMenu = [grid.documentView
            noteMenuForTrack:6u row:31u];
        NSMenuItem* selectionRoot = drumSplitMenu.itemArray.lastObject;
        NSMenuItem* transformRoot = selectionRoot.submenu.itemArray[3u];
        NSMenuItem* separateNotes = [transformRoot.submenu
            itemWithTitle:@"SEPARATE NOTES INTO LANES"];
        [NSApp sendAction:separateNotes.action to:separateNotes.target
            from:separateNotes];
        const auto& separatedSource = state.session.pattern.tracks[6u];
        const auto& separatedSnare = state.session.pattern.tracks[
            laneCountBeforeDrumSplit];
        const auto& separatedHat = state.session.pattern.tracks[
            laneCountBeforeDrumSplit + 1u];
        check(selectionRoot.submenu.numberOfItems == 5u
                && [selectionRoot.submenu.itemArray[0u].title
                    isEqualToString:@"FILL / REPEAT"]
                && [selectionRoot.submenu.itemArray[1u].title
                    isEqualToString:@"CELLS"]
                && [selectionRoot.submenu.itemArray[2u].title
                    isEqualToString:@"PASTE SPECIAL"]
                && [transformRoot.title isEqualToString:@"TRANSFORM"]
                && [selectionRoot.submenu.itemArray[4u].title
                    isEqualToString:@"PHRASE"]
                && separateNotes.enabled
                && state.session.pattern.tracks.size()
                    == laneCountBeforeDrumSplit + 2u
                && separatedSource.notes[30u].note == 36u
                && separatedSource.notes[31u].state
                    == s3g::tracker::NoteCellState::Rest
                && separatedSource.notes[32u].note == 36u
                && separatedSnare.notes[31u].note == 38u
                && separatedHat.notes[32u].note == 42u
                && std::abs(separatedHat.velocities[32u].normalized - 0.4f)
                    < 0.00001f
                && separatedSnare.midiChannel == separatedSource.midiChannel
                && separatedHat.initialInstrumentNodeId
                    == separatedSource.initialInstrumentNodeId
                && separatedSnare.fxPairs[0u].actions[31u].state
                    == s3g::tracker::FxActionCellState::Sequencer
                && separatedSnare.fxPairs[0u].actions[31u].sequencerAction
                    == s3g::tracker::SequencerAction::MicroTime
                && separatedSnare.fxPairs[0u].values[31u].state
                    == s3g::tracker::FxValueCellState::Value
                && near(separatedSnare.fxPairs[0u].values[31u].normalized,
                    0.25)
                && separatedHat.fxPairs[0u].actions[32u].state
                    == s3g::tracker::FxActionCellState::Sequencer
                && separatedHat.fxPairs[0u].actions[32u].sequencerAction
                    == s3g::tracker::SequencerAction::Flam
                && near(separatedHat.fxPairs[0u].values[32u].normalized,
                    0.75)
                && separatedHat.fxPairs[1u].actions[32u].sequencerAction
                    == s3g::tracker::SequencerAction::MicroTime
                && near(separatedHat.fxPairs[1u].values[32u].normalized,
                    0.8)
                && separatedSnare.fxPairs[1u].actions[31u].state
                    == s3g::tracker::FxActionCellState::Empty
                && separatedSnare.fxPairs[0u].actionColumn.length == 48u
                && separatedSnare.fxPairs[0u].actionColumn.stride == 3u
                && separatedSnare.fxPairs[0u].actionColumn.phase == 2u
                && separatedSnare.fxPairs[0u].valueColumn.length == 40u
                && separatedSnare.fxPairs[0u].valueColumn.direction
                    == s3g::tracker::Direction::Palindrome,
            "Separate Notes Into Lanes should split live-recorded drum pitches, velocity voices, and resolved note-local SEQ state while preserving routing and leaving MIDI automation behind");

        auto& mergeTarget = state.session.pattern.tracks[
            laneCountBeforeDrumSplit];
        auto& mergeSource = state.session.pattern.tracks[
            laneCountBeforeDrumSplit + 1u];
        mergeTarget.notes[32u]
            = s3g::tracker::NoteCell::withNote(38u);
        mergeTarget.velocities[31u]
            = s3g::tracker::ValueCell::withValue(0.65f);
        mergeTarget.velocities[32u]
            = s3g::tracker::ValueCell::previous();
        mergeTarget.fxPairs[0u].actions[32u]
            = s3g::tracker::FxActionCell::sequencer(
                s3g::tracker::SequencerAction::MicroTime);
        mergeTarget.fxPairs[0u].values[32u]
            = s3g::tracker::FxValueCell::withValue(0.3f);
        mergeSource.fxPairs[1u].actions[31u]
            = s3g::tracker::FxActionCell::midiControlChange(74u);
        mergeSource.fxPairs[1u].values[31u]
            = s3g::tracker::FxValueCell::withValue(0.6f);
        [grid.documentView beginGridSelectionAtTrack:
            laneCountBeforeDrumSplit field:0u row:31u page:0u];
        [grid.documentView extendGridSelectionToTrack:
            laneCountBeforeDrumSplit + 1u field:0u row:32u];
        NSMenu* drumMergeMenu = [grid.documentView noteMenuForTrack:
            laneCountBeforeDrumSplit row:32u];
        NSMenuItem* mergeSelectionRoot = drumMergeMenu.itemArray.lastObject;
        NSMenuItem* mergeTransformRoot =
            mergeSelectionRoot.submenu.itemArray[3u];
        NSMenuItem* mergeNotes = [mergeTransformRoot.submenu
            itemWithTitle:@"MERGE NOTES INTO ONE LANE"];
        const int changesBeforeMerge = patternChangeRequests;
        [NSApp sendAction:mergeNotes.action to:mergeNotes.target
            from:mergeNotes];
        const auto& merged = state.session.pattern.tracks[
            laneCountBeforeDrumSplit];
        const auto& emptied = state.session.pattern.tracks[
            laneCountBeforeDrumSplit + 1u];
        check([mergeNotes.title isEqualToString:
                    @"MERGE NOTES INTO ONE LANE"]
                && mergeNotes.enabled
                && merged.notes[31u].note == 38u
                && merged.notes[32u].noteVoiceCount() == 2u
                && merged.notes[32u].noteVoice(0u) == 38u
                && merged.notes[32u].noteVoice(1u) == 42u
                && merged.velocities[32u].valueVoiceCount() == 2u
                && near(merged.velocities[32u].valueVoice(0u), 0.65)
                && near(merged.velocities[32u].valueVoice(1u), 0.4)
                && emptied.notes[32u].state
                    == s3g::tracker::NoteCellState::Rest
                && emptied.velocities[32u].state
                    == s3g::tracker::ValueCellState::Default
                && merged.fxPairs[0u].actions[32u].sequencerAction
                    == s3g::tracker::SequencerAction::MicroTime
                && merged.fxPairs[0u].values[32u].valueVoiceCount() == 2u
                && near(merged.fxPairs[0u].values[32u].valueVoice(0u), 0.3)
                && near(merged.fxPairs[0u].values[32u].valueVoice(1u), 0.8)
                && merged.fxPairs[1u].actions[32u].sequencerAction
                    == s3g::tracker::SequencerAction::Flam
                && near(merged.fxPairs[1u].values[32u].normalized, 0.75)
                && emptied.fxPairs[1u].actions[31u].state
                    == s3g::tracker::FxActionCellState::MidiControlChange
                && patternChangeRequests == changesBeforeMerge + 1,
            "Merge Notes Into One Lane should move selected notes into sorted polyphonic cells, align resolved velocity voices, carry compatible SEQ state, and leave MIDI automation in its source lane");

        std::array<uint8_t, s3g::tracker::kMaximumNoteVoices>
            maximumChord {};
        for (std::size_t voice = 0u; voice < maximumChord.size(); ++voice)
            maximumChord[voice] = static_cast<uint8_t>(48u + voice);
        state.session.pattern.tracks[laneCountBeforeDrumSplit].notes[33u]
            = s3g::tracker::NoteCell::withNotes(
                maximumChord, maximumChord.size());
        state.session.pattern.tracks[laneCountBeforeDrumSplit + 1u]
            .notes[33u] = s3g::tracker::NoteCell::withNote(72u);
        [grid.documentView beginGridSelectionAtTrack:
            laneCountBeforeDrumSplit field:0u row:33u page:0u];
        [grid.documentView extendGridSelectionToTrack:
            laneCountBeforeDrumSplit + 1u field:0u row:33u];
        const int changesBeforeOverflowMerge = patternChangeRequests;
        [grid.documentView mergeSelectedNoteLanes:nil];
        check(state.session.pattern.tracks[laneCountBeforeDrumSplit]
                    .notes[33u].noteVoiceCount()
                    == s3g::tracker::kMaximumNoteVoices
                && state.session.pattern.tracks[
                    laneCountBeforeDrumSplit + 1u].notes[33u].note == 72u
                && patternChangeRequests == changesBeforeOverflowMerge,
            "Merge Notes Into One Lane should reject an entire edit when any merged row would exceed eight unique voices");
        state.session.pattern = patternBeforeDrumSplit;
        state.session.laneDefaultNotes = defaultsBeforeDrumSplit;
        state.sequenceColumnsExpanded = clipboardExpandedBefore;

        auto& clearTrack = state.session.pattern.tracks[0u];
        clearTrack.notes[2u] = s3g::tracker::NoteCell::withNote(62u);
        clearTrack.notes[3u] = s3g::tracker::NoteCell::withNote(64u);
        clearTrack.velocities[2u]
            = s3g::tracker::ValueCell::withValue(0.5f);
        clearTrack.velocities[3u]
            = s3g::tracker::ValueCell::withValue(0.75f);
        [grid.documentView beginGridSelectionAtTrack:0u
            field:0u row:2u page:0u];
        [grid.documentView extendGridSelectionToTrack:0u
            field:1u row:3u];
        const int changesBeforeDelete = patternChangeRequests;
        [grid.documentView keyDown:keyEvent(window, @"\x7f", 51u, 0u)];
        const auto& clearedTrack = state.session.pattern.tracks[0u];
        check(clearedTrack.notes[2u].state
                    == s3g::tracker::NoteCellState::Rest
                && clearedTrack.notes[3u].state
                    == s3g::tracker::NoteCellState::Rest
                && clearedTrack.velocities[2u].state
                    == s3g::tracker::ValueCellState::Default
                && clearedTrack.velocities[3u].state
                    == s3g::tracker::ValueCellState::Default
                && patternChangeRequests == changesBeforeDelete + 1,
            "Delete should clear every cell in a rectangular drag selection");

        state.session.selectedTrack = 11u;
        state.session.selectedRow = 63u;
        [controller reloadModel];
        [root layoutSubtreeIfNeeded];
        check(NSMinX(grid.documentVisibleRect) > 1000.0
                && NSMinY(grid.documentVisibleRect) > 500.0,
            "selection navigation should reveal off-screen lanes and rows");

        [window setContentSize:NSMakeSize(1320.0, 840.0)];
        [root layoutSubtreeIfNeeded];
        check(near(NSWidth(window.contentView.bounds), 1320.0)
                && near(NSWidth(grid.frame), 1320.0),
            "spacious AppKit layout should give the tracker the full page");
        check(NSWidth(grid.documentView.frame) >
                NSWidth(grid.contentView.bounds),
            "track count should never force the main window wider");

        const auto clickCanvasMenuItem = ^BOOL(
            NSPopUpButton* popup, NSInteger item) {
            const NSPoint popupPoint = [popup convertPoint:NSMakePoint(
                NSMidX(popup.bounds), NSMidY(popup.bounds)) toView:nil];
            NSView* popupHit = [window.contentView hitTest:popupPoint];
            if (popupHit != popup) return NO;
            [popupHit mouseDown:mouseDownEvent(window, popupPoint, 1)];
            NSView* overlay = [popup valueForKey:@"s3gMenuOverlay"];
            if (!overlay) return NO;
            const NSRect menuRect = [[overlay valueForKey:@"menuRect"]
                rectValue];
            const NSPoint itemPoint = NSMakePoint(
                NSMinX(menuRect) + 10.0,
                NSMinY(menuRect) + 10.5 + 21.0 * item);
            const NSPoint itemInWindow = [overlay convertPoint:itemPoint
                toView:nil];
            NSView* itemHit = [window.contentView hitTest:itemInWindow];
            if (itemHit != overlay) return NO;
            [itemHit mouseDown:mouseDownEvent(window,
                itemInWindow, 1)];
            return [popup valueForKey:@"s3gMenuOverlay"] == nil
                && popup.indexOfSelectedItem == item;
        };
        const BOOL patternMenuClicked = clickCanvasMenuItem(patternPopup, 1);
        const BOOL midiMenuClicked = clickCanvasMenuItem(
            midiStepRecordPopup, 1);
        const BOOL recordLaneMenuClicked = clickCanvasMenuItem(
            midiRecordTrackPopup, 3);
        check(patternMenuClicked && selectedPattern == "A02"
                && midiMenuClicked
                && recordLaneMenuClicked
                && state.midiRecordTrack == 3u
                && state.midiStepRecordMode
                    == s3g::tracker::MidiStepRecordMode::Step,
            "Pattern, MIDI REC, and REC LANE menus should dispatch through real window hit testing");

        [grid.contentView scrollToPoint:NSZeroPoint];
        [grid reflectScrolledClipView:grid.contentView];
        const CGFloat clickLaneWidth = (NSWidth(grid.documentView.bounds)
                - s3g::tracker::app::kTrackerRowNumberWidth
                - 11.0 * s3g::tracker::app::kTrackerLaneGutter) / 12.0;
        const NSPoint secondLaneHeader = [grid.documentView convertPoint:
            NSMakePoint(s3g::tracker::app::kTrackerRowNumberWidth
                    + clickLaneWidth
                    + s3g::tracker::app::kTrackerLaneGutter + 10.0,
                10.0)
            toView:nil];
        NSView* laneHit = [window.contentView hitTest:secondLaneHeader];
        [laneHit mouseDown:mouseDownEvent(window, secondLaneHeader, 1)];
        check(laneHit == grid.documentView
                && state.session.selectedTrack == 1u
                && state.midiRecordTrack == 3u,
            "a real lane-header click should select an editing lane without changing REC LANE");
        state.session.selectedTrack = 0u;
        state.songPlaybackPatternId = "A02";
        state.songPlaybackActive = true;
        laneHit = [window.contentView hitTest:secondLaneHeader];
        [laneHit mouseDown:mouseDownEvent(window, secondLaneHeader, 1)];
        check(laneHit == grid.documentView
                && state.session.selectedTrack == 1u,
            "lane selection should remain available while Tracker follows the sounding Song pattern");
        state.songPlaybackActive = false;
        state.songPlaybackPatternId.clear();

        [[controller valueForKey:@"geometryWindowController"] close];
        [[controller valueForKey:@"reshapeWindowController"] close];
        [[controller valueForKey:@"warpWindowController"] close];
    }

    if (failures == 0) {
        std::cout << "workspace AppKit layout tests passed\n";
        return 0;
    }
    std::cerr << failures << " workspace AppKit assertion(s) failed\n";
    return 1;
}
