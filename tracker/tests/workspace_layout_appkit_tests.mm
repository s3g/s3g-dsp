#import <Cocoa/Cocoa.h>

#import "s3g_tracker_controls.h"
#import "s3g_tracker_workspace.h"

#include "s3g_tracker_workspace_layout.h"
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
- (void)sequenceConditionSelected:(NSMenuItem*)sender;
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
- (void)selectWholeRowsFrom:(std::size_t)anchor to:(std::size_t)focus;
- (BOOL)isWholeRowSelected:(std::size_t)row;
- (NSPoint)geometryCenter;
- (CGFloat)ringRadiusForLane:(std::size_t)lane;
- (BOOL)selectedRingLane:(std::size_t*)lane radius:(CGFloat*)radius;
- (NSPoint)geometryPointAtRadius:(CGFloat)radius angle:(CGFloat)angle;
- (NSRect)canvasRect;
- (NSRect)laneCyclePanelRect;
- (NSRect)viewPanelRect;
- (NSRect)laneMenuBoxRect;
- (NSRect)directionMenuBoxRect;
- (NSRect)viewMenuBoxRect;
- (NSRect)linkVelocityLengthToggleRect;
- (NSRect)revealHeaderButtonRect;
- (NSRect)fitBurstGatesHeaderButtonRect;
- (NSRect)burstPreviewHeaderButtonRect;
- (NSRect)lengthSliderTrack;
- (NSRect)defaultNoteSliderTrack;
- (NSRect)rotateSliderTrack;
- (NSRect)densitySliderTrack;
- (BOOL)handleToolboxClickAtPoint:(NSPoint)point;
- (void)syncToolboxControls;
- (void)openGeometryMenu:(NSInteger)menu;
- (void)applyGeometryMenuSelection:(NSInteger)index;
- (NSUInteger)allStepsUnderlayNodeCount;
- (NSPoint)rotateHandlePoint;
- (NSPoint)densityHandlePoint;
- (BOOL)revealBeadAtPoint:(NSPoint)point;
- (void)selectLane:(std::size_t)lane;
- (void)selectBurstSlot:(std::size_t)slot;
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
        int trackerRevealRequests = 0;
        int burstPreviewRequests = 0;
        int patternPreviewRequests = 0;
        int patternPreviewClearRequests = 0;
        int patternVariantRequests = 0;
        s3g::tracker::Pattern previewedPattern;
        s3g::tracker::Pattern createdVariant;
        s3g::tracker::BurstDefinition previewedBurst;
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
        callbacks.showTrackerPage = [&] { ++trackerRevealRequests; };
        callbacks.previewBurst = [&](const s3g::tracker::BurstDefinition& burst,
            uint8_t channel, double bpm, uint32_t ticksPerBeat) {
            ++burstPreviewRequests;
            previewedBurst = burst;
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
        NSButton* zoomOutButton = [controller valueForKey:@"zoomOutButton"];
        NSButton* zoomActualButton = [controller
            valueForKey:@"zoomActualButton"];
        NSButton* zoomInButton = [controller valueForKey:@"zoomInButton"];
        NSButton* playButton = [controller valueForKey:@"playButton"];
        NSButton* fillButton = [controller valueForKey:@"fillButton"];
        NSPopUpButton* midiStepRecordPopup = [controller
            valueForKey:@"midiStepRecordPopup"];
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
        NSView* reshapePage = [controller reshapePageView];
        NSView* warpPage = [controller warpPageView];
        NSPopUpButton* geometryViewMode = [geometryPage
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
        check(consoleOutput && geometryPage && reshapePage && warpPage
                && consoleOutput != geometryPage
                && geometryPage != reshapePage && reshapePage != warpPage,
            "console, geometry, reshape, and warp modules should expose distinct pages");
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
        check(geometryViewMode.numberOfItems == 7u
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
                && [[geometryPage valueForKey:@"geometryViewMode"]
                    integerValue] == 0,
            "Geometry should default to Ring Field and expose diagnostics plus the Burst workspace");
        auto& matrixBurst = state.session.pattern.bursts[0u];
        matrixBurst.name = "MATRIX TEST";
        matrixBurst.eventCount = 2u;
        matrixBurst.events[0u] = { 0u, 48u, 64u, 70u };
        matrixBurst.events[1u] = { 32768u, 52u, 80u, 75u };
        [geometryPage selectBurstSlot:0u];
        NSTextField* burstNameField = [geometryPage
            valueForKey:@"burstNameField"];
        NSButton* burstSaveButton = [geometryPage
            valueForKey:@"burstSaveButton"];
        burstNameField.stringValue = @"AMEN PUSH";
        [burstSaveButton performClick:nil];
        check(!burstNameField.hidden && !burstSaveButton.hidden
                && [burstSaveButton.title isEqualToString:@"SAVE"]
                && matrixBurst.name == "AMEN PUSH",
            "Burst Library should use a persistent NAME field and explicit SAVE action like Warps");
        const NSRect previewBurstButton = [geometryPage
            burstPreviewHeaderButtonRect];
        const BOOL previewed = [geometryPage handleToolboxClickAtPoint:
            NSMakePoint(NSMidX(previewBurstButton), NSMidY(previewBurstButton))];
        check(previewed && burstPreviewRequests == 1
                && previewedBurst.name == "AMEN PUSH"
                && previewedChannel == 1u
                && near(previewedBpm, state.session.transport.bpm)
                && previewedTicksPerBeat
                    == state.session.transport.ticksPerBeat,
            "stopped Burst Preview should dispatch the selected phrase on the lane channel at the project clock");
        state.playing = true;
        (void)[geometryPage handleToolboxClickAtPoint:
            NSMakePoint(NSMidX(previewBurstButton), NSMidY(previewBurstButton))];
        check(burstPreviewRequests == 1,
            "Burst Preview should remain unavailable while transport is running");
        state.playing = false;
        const NSRect burstMatrix = [geometryPage burstMatrixRect];
        const NSRect burstVelocityCell = [geometryPage
            burstMatrixCellRect:1u field:2];
        const BOOL beganVelocityEdit = [geometryPage
            beginBurstCanvasGestureAtPoint:NSMakePoint(
                NSMidX(burstVelocityCell), NSMidY(burstVelocityCell))];
        [geometryPage updateBurstMatrixGestureAtPoint:NSMakePoint(
            NSMaxX(burstVelocityCell) - 6.0, NSMidY(burstVelocityCell))];
        [geometryPage finishGeometryGesture];
        const NSRect addRow = [geometryPage burstMatrixRowRect:4u];
        const BOOL beganAdd = [geometryPage beginBurstCanvasGestureAtPoint:
            NSMakePoint(NSMidX(addRow), NSMidY(addRow))];
        [geometryPage finishGeometryGesture];
        check(NSWidth(burstMatrix) >= 360.0,
            "Burst workspace should expose a readable eight-row event matrix");
        check(beganVelocityEdit && matrixBurst.events[1u].velocity > 110u,
            "Burst matrix should directly edit an event value by dragging its cell");
        check(beganAdd && matrixBurst.eventCount == 5u,
            "Burst matrix should grow immediately when an unused event row is selected");
        const NSRect fitGatesButton = [geometryPage
            fitBurstGatesHeaderButtonRect];
        const BOOL fitGates = [geometryPage handleToolboxClickAtPoint:
            NSMakePoint(NSMidX(fitGatesButton), NSMidY(fitGatesButton))];
        check(fitGates
                && std::all_of(matrixBurst.events.begin(),
                    matrixBurst.events.begin() + matrixBurst.eventCount,
                    [](const s3g::tracker::BurstEvent& event) {
                        return event.gatePercent == 20u;
                    }),
            "Burst Substeps should fit gates between even onsets and the primary row boundary");
        const NSRect placeBurstButton = [geometryPage revealHeaderButtonRect];
        const BOOL placedBurst = [geometryPage handleToolboxClickAtPoint:
            NSMakePoint(NSMidX(placeBurstButton), NSMidY(placeBurstButton))];
        const BOOL placementFeedback = [[geometryPage
            valueForKey:@"burstPlaceFeedbackActive"] boolValue];
        check(placedBurst && placementFeedback
                && state.session.pattern.tracks[0u].notes[0u].state
                    == s3g::tracker::NoteCellState::Burst,
            "placing a Burst should immediately latch visible success feedback");
        state.session.pattern.tracks[0u].notes[0u]
            = s3g::tracker::NoteCell::withNote(60u);
        [geometryPage setValue:@(0) forKey:@"geometryViewMode"];
        [geometryViewMode selectItemAtIndex:0u];
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
                && near(NSMinY(laneCyclePanel),
                    s3g::gui_layout::kTrackerPageContentTop)
                && near(NSMaxY(geometryCanvas),
                    NSHeight(geometryPage.bounds) - 18.0)
                && near(NSMinX(laneCyclePanel) - NSMaxX(geometryCanvas),
                    12.0)
                && near(NSHeight(laneCyclePanel), 210.0),
            "Geometry should share the Tracker page top and retain the 12/26 seven-row toolbox contract with custom in-canvas menus");
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
                && near(NSHeight(reshapeTiming.frame),
                    s3g::gui_layout::toolboxHeightForRows(5u))
                && near(NSHeight(reshapeDynamics.frame),
                    s3g::gui_layout::toolboxHeightForRows(5u))
                && reshapeTimingLabels.count == 5u
                && reshapeDynamicsLabels.count == 5u
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
            @"Burst editor"
        ];
        BOOL geometryModesDispatch = YES;
        for (NSInteger mode = 1; mode < 7; ++mode) {
            geometryPlaybackOverlay.needsDisplay = NO;
            [geometryPage openGeometryMenu:3];
            [geometryPage applyGeometryMenuSelection:mode];
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
            "every Geometry view should dispatch, with All Steps alone exposing the complete row-node lattice");
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
                && [sequenceColumnsButton.title isEqualToString:@"EXPAND SEQ"],
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
                    containsObject:zoomOutButton]
                && [inputPrimaryControls.arrangedSubviews
                    containsObject:zoomActualButton]
                && [inputPrimaryControls.arrangedSubviews
                    containsObject:zoomInButton]
                && ![inputPrimaryControls.arrangedSubviews
                    containsObject:midiStepRecordPopup]
                && transportPrimaryControls.arrangedSubviews.count == 11u
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
                    containsObject:midiStepRecordPopup],
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
                && [(S3GTrackerActionButton*)playButton
                    s3gUsesSuiteStyle]
                && [(S3GTrackerActionButton*)playButton
                    s3gUsesNeutralTitle]
                && [(S3GTrackerActionButton*)renamePatternButton
                    s3gUsesNeutralTitle]
                && [(S3GTrackerActionButton*)deletePatternButton
                    s3gUsesNeutralTitle],
            "Tracker header menus and actions should use shared controls and one neutral title level");
        [sequenceColumnsButton performClick:nil];
        [root layoutSubtreeIfNeeded];
        check(state.sequenceColumnsExpanded
                && [sequenceColumnsButton.title
                    isEqualToString:@"COLLAPSE SEQ"]
                && near(NSMinX(sequenceColumnsButton.frame), initialSequenceX)
                && near(NSMinX(trackAddButton.frame), initialAddTrackX)
                && near(NSMinX(trackRemoveButton.frame), initialRemoveTrackX),
            "Expand Seq should reveal both sequencing pairs without moving its direct toolbox controls");
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
        check(rowMenu.numberOfItems == 5u
                && [rowMenu.itemArray[0u].title
                    isEqualToString:@"INSERT 4 ROWS ABOVE"]
                && [rowMenu.itemArray[1u].title
                    isEqualToString:@"DELETE 4 ROWS"]
                && [rowMenu.itemArray[3u].title
                    isEqualToString:@"COPY 4 ROWS"]
                && !rowMenu.itemArray[4u].enabled,
            "the row-number menu should apply insert, delete, copy, and paste to the selected row range");
        [NSApp sendAction:rowMenu.itemArray[3u].action
            to:rowMenu.itemArray[3u].target from:rowMenu.itemArray[3u]];
        rowMenu = [rowGutter menuForEvent:rowMenuEvent];
        const int changesBeforeRowPaste = patternChangeRequests;
        [NSApp sendAction:rowMenu.itemArray[4u].action
            to:rowMenu.itemArray[4u].target from:rowMenu.itemArray[4u]];
        check(state.session.pattern.visibleRows == 68u
                && state.session.pattern.tracks[0u].notes[7u].note == 67u
                && state.session.pattern.tracks[0u].notes[10u].note == 70u
                && patternChangeRequests == changesBeforeRowPaste + 1,
            "pasting copied rows should insert the complete multi-row range once across the pattern");
        rowMenu = [rowGutter menuForEvent:rowMenuEvent];
        [NSApp sendAction:rowMenu.itemArray[1u].action
            to:rowMenu.itemArray[1u].target from:rowMenu.itemArray[1u]];
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
        [NSApp sendAction:rowMenu.itemArray[1u].action
            to:rowMenu.itemArray[1u].target from:rowMenu.itemArray[1u]];
        check(state.session.pattern.visibleRows == 64u
                && state.session.pattern.tracks[0u].notes[7u].note == 67u,
            "deleting the inserted range should restore the original row positions");
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
        NSMenuItem* midiControlItem = sequenceMenu.itemArray.lastObject;
        check(sequenceMenu.numberOfItems
                    == static_cast<NSInteger>(
                        s3g::tracker::sequencerActionCount() + 7u)
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
                        s3g::tracker::kSequencerConditionCount + 6u)
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
        check(patternMenuClicked && selectedPattern == "A02"
                && midiMenuClicked
                && state.midiStepRecordMode
                    == s3g::tracker::MidiStepRecordMode::Step,
            "Pattern and MIDI REC menus should open, select, dispatch, and dismiss through real window hit testing");

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
                && state.session.selectedTrack == 1u,
            "a real lane-header click should reach the Tracker grid and select that lane after using canvas menus");
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
