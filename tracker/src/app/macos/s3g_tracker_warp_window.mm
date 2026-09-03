#import "s3g_tracker_warp_window.h"

#import "s3g_tracker_controls.h"
#import "s3g_tracker_workspace.h"

#include "s3g_gui_layout.h"
#define S3G_COCOA_GUI_DRAWING_ONLY 1
#include "s3g_cocoa_gui.h"
#undef S3G_COCOA_GUI_DRAWING_ONLY

#include <algorithm>
#include <array>
#include <cmath>

namespace {

using s3g::tracker::TimingWarpKind;
using s3g::tracker::TimingWarpOptions;
using s3g::tracker::TimingWarpStack;
using s3g::tracker::TimingWarpTransform;
using s3g::tracker::app::TrackerViewState;
using s3g::tracker::app::WorkspaceCallbacks;

NSTextField* warpLabel(NSString* value, CGFloat size = 9.0)
{
    NSTextField* field = [NSTextField labelWithString:value];
    field.font = s3g::clap_gui::uiFont(size);
    field.textColor = S3GTrackerThemeColor(
        S3GTrackerThemeRole::TextSecondary);
    return field;
}

NSString* transformSummary(const TimingWarpTransform& transform,
    std::size_t index)
{
    switch (transform.kind) {
    case TimingWarpKind::Exponential:
        return [NSString stringWithFormat:@"%02lu  EXP  %.3g",
            static_cast<unsigned long>(index + 1u), transform.exponent];
    case TimingWarpKind::StepQuantize:
        return [NSString stringWithFormat:@"%02lu  STEP  %u",
            static_cast<unsigned long>(index + 1u), transform.steps];
    case TimingWarpKind::EuclideanQuantize:
        return [NSString stringWithFormat:@"%02lu  EUCLID  %u/%u",
            static_cast<unsigned long>(index + 1u), transform.pulses,
            transform.steps];
    }
    return @"—";
}

} // namespace

@class S3GTrackerWarpWindowController;

@interface S3GTrackerWarpRootView : S3GTrackerFocusReleaseView
@property(nonatomic, weak) S3GTrackerWarpWindowController* layoutOwner;
@end

@interface S3GTrackerWarpCurveView : NSView
@property(nonatomic, assign) TrackerViewState* trackerState;
- (void)refreshPlaybackDisplay;
@end

@implementation S3GTrackerWarpRootView

- (BOOL)isFlipped { return YES; }

- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    [S3GTrackerThemeColor(S3GTrackerThemeRole::Workspace) setFill];
    NSRectFill(self.bounds);
}

- (void)layout
{
    [super layout];
    if ([self.layoutOwner respondsToSelector:@selector(layoutWarpInterface)])
        [self.layoutOwner performSelector:@selector(layoutWarpInterface)];
}

@end

@implementation S3GTrackerWarpCurveView

- (BOOL)isFlipped { return YES; }

- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    [S3GTrackerThemeColor(S3GTrackerThemeRole::Workspace) setFill];
    NSRectFill(self.bounds);
    const NSRect graph = NSInsetRect(self.bounds, 30.0, 22.0);
    [S3GTrackerThemeColor(S3GTrackerThemeRole::BorderStrong) setStroke];
    NSFrameRect(graph);
    if (!self.trackerState || NSWidth(graph) <= 1.0
        || NSHeight(graph) <= 1.0) return;

    const auto& transport = self.trackerState->session.transport;
    const bool playbackActive = self.trackerState->playing
        && self.trackerState->timingWarpPlaybackActive;
    const auto& displayedWarp = playbackActive
        ? self.trackerState->timingWarpPlaybackStack
        : transport.timingWarp;
    const bool warpEnabled = playbackActive
        || transport.timingWarpEnabled;
    const auto cycle = std::max<uint32_t>(1u, playbackActive
            ? self.trackerState->timingWarpPlaybackCycleTicks
            : transport.warpCycleTicks);
    for (uint32_t tick = 0u; tick <= cycle; ++tick) {
        const CGFloat x = NSMinX(graph) + NSWidth(graph)
            * static_cast<CGFloat>(tick) / static_cast<CGFloat>(cycle);
        [S3GTrackerThemeColor(S3GTrackerThemeRole::Grid,
            tick == 0u || tick == cycle ? 0.9 : 0.55) setFill];
        NSRectFill(NSMakeRect(x, NSMinY(graph), 1.0, NSHeight(graph)));
    }
    for (uint32_t division = 1u; division < 4u; ++division) {
        const CGFloat y = NSMinY(graph) + NSHeight(graph)
            * static_cast<CGFloat>(division) / 4.0;
        [S3GTrackerThemeColor(S3GTrackerThemeRole::Grid, 0.42) setFill];
        NSRectFill(NSMakeRect(NSMinX(graph), y, NSWidth(graph), 1.0));
    }

    NSBezierPath* identity = [NSBezierPath bezierPath];
    [identity moveToPoint:NSMakePoint(NSMinX(graph), NSMaxY(graph))];
    [identity lineToPoint:NSMakePoint(NSMaxX(graph), NSMinY(graph))];
    identity.lineWidth = 1.0;
    const CGFloat dash[] = { 4.0, 4.0 };
    [identity setLineDash:dash count:2 phase:0.0];
    [S3GTrackerThemeColor(S3GTrackerThemeRole::TextFaint, 0.72) setStroke];
    [identity stroke];

    NSBezierPath* curve = [NSBezierPath bezierPath];
    constexpr std::size_t samples = 256u;
    for (std::size_t index = 0u; index <= samples; ++index) {
        const double input = static_cast<double>(index)
            / static_cast<double>(samples);
        const double output = displayedWarp.map(input);
        const NSPoint point = NSMakePoint(NSMinX(graph)
                + NSWidth(graph) * static_cast<CGFloat>(input),
            NSMaxY(graph) - NSHeight(graph) * static_cast<CGFloat>(output));
        if (index == 0u) [curve moveToPoint:point];
        else [curve lineToPoint:point];
    }
    curve.lineWidth = 2.0;
    [S3GTrackerThemeColor(warpEnabled
            ? S3GTrackerThemeRole::Live : S3GTrackerThemeRole::TextFaint,
        warpEnabled ? 1.0 : 0.55) setStroke];
    [curve stroke];

    double playbackInput = 0.0;
    NSPoint playbackPoint = NSZeroPoint;
    if (playbackActive) {
        const uint64_t cycleTick = self.trackerState->timingWarpPlaybackTick
            % static_cast<uint64_t>(cycle);
        playbackInput = static_cast<double>(cycleTick)
            / static_cast<double>(cycle);
        const double playbackOutput = displayedWarp.map(playbackInput);
        playbackPoint = NSMakePoint(NSMinX(graph)
                + NSWidth(graph) * static_cast<CGFloat>(playbackInput),
            NSMaxY(graph)
                - NSHeight(graph) * static_cast<CGFloat>(playbackOutput));

        NSBezierPath* completed = [NSBezierPath bezierPath];
        constexpr std::size_t progressSamples = 192u;
        for (std::size_t index = 0u; index <= progressSamples; ++index) {
            const double input = playbackInput * static_cast<double>(index)
                / static_cast<double>(progressSamples);
            const double output = displayedWarp.map(input);
            const NSPoint point = NSMakePoint(NSMinX(graph)
                    + NSWidth(graph) * static_cast<CGFloat>(input),
                NSMaxY(graph)
                    - NSHeight(graph) * static_cast<CGFloat>(output));
            if (index == 0u) [completed moveToPoint:point];
            else [completed lineToPoint:point];
        }
        completed.lineWidth = 3.0;
        [S3GTrackerThemeColor(S3GTrackerThemeRole::Live) setStroke];
        [completed stroke];

        [S3GTrackerThemeColor(S3GTrackerThemeRole::Live, 0.22) setFill];
        NSRectFill(NSMakeRect(playbackPoint.x, NSMinY(graph), 1.0,
            NSHeight(graph)));
    }

    if (!warpEnabled) {
        NSBezierPath* bypass = [NSBezierPath bezierPath];
        [bypass moveToPoint:NSMakePoint(NSMinX(graph), NSMaxY(graph))];
        [bypass lineToPoint:NSMakePoint(NSMaxX(graph), NSMinY(graph))];
        bypass.lineWidth = 2.0;
        [S3GTrackerThemeColor(S3GTrackerThemeRole::Live) setStroke];
        [bypass stroke];
    }

    for (uint32_t tick = 0u; tick <= cycle; ++tick) {
        const double input = static_cast<double>(tick)
            / static_cast<double>(cycle);
        const double output = warpEnabled
            ? displayedWarp.map(input) : input;
        const NSPoint point = NSMakePoint(NSMinX(graph)
                + NSWidth(graph) * static_cast<CGFloat>(input),
            NSMaxY(graph) - NSHeight(graph) * static_cast<CGFloat>(output));
        [S3GTrackerThemeColor(S3GTrackerThemeRole::Value) setFill];
        [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(
            point.x - 2.5, point.y - 2.5, 5.0, 5.0)] fill];
    }

    if (playbackActive) {
        [S3GTrackerThemeColor(S3GTrackerThemeRole::Live, 0.2) setFill];
        [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(
            playbackPoint.x - 7.0, playbackPoint.y - 7.0, 14.0, 14.0)]
            fill];
        NSBezierPath* marker = [NSBezierPath bezierPathWithOvalInRect:
            NSMakeRect(playbackPoint.x - 4.0, playbackPoint.y - 4.0,
                8.0, 8.0)];
        [S3GTrackerThemeColor(S3GTrackerThemeRole::Value) setFill];
        [marker fill];
        marker.lineWidth = 1.5;
        [S3GTrackerThemeColor(S3GTrackerThemeRole::Live) setStroke];
        [marker stroke];
    }

    NSDictionary* attributes = @{
        NSForegroundColorAttributeName: S3GTrackerThemeColor(
            S3GTrackerThemeRole::TextMuted),
        NSFontAttributeName: s3g::clap_gui::uiFont(8.5),
    };
    [@"INPUT PHASE" drawAtPoint:NSMakePoint(NSMaxX(graph) - 70.0,
        NSMaxY(graph) + 5.0) withAttributes:attributes];
    NSString* outputLabel = !playbackActive
        ? (warpEnabled ? @"WARPED" : @"OUTPUT · BYPASSED")
        : self.trackerState->timingWarpPlaybackFromSong
            ? @"SONG WARP · PLAYING" : @"WARPED · PLAYING";
    [outputLabel
        drawAtPoint:NSMakePoint(NSMinX(graph), 4.0)
        withAttributes:attributes];
    if (playbackActive) {
        const auto step = self.trackerState->timingWarpPlaybackTick
            % static_cast<uint64_t>(cycle) + 1u;
        NSString* progress = [NSString stringWithFormat:@"STEP %02llu / %02u",
            static_cast<unsigned long long>(step), cycle];
        const NSSize progressSize = [progress sizeWithAttributes:attributes];
        [progress drawAtPoint:NSMakePoint(NSMaxX(graph) - progressSize.width,
            4.0) withAttributes:attributes];
    }
}

- (void)refreshPlaybackDisplay
{
    const auto* state = self.trackerState;
    if (state && state->playing && state->timingWarpPlaybackActive) {
        const uint32_t cycle = std::max<uint32_t>(1u,
            state->timingWarpPlaybackCycleTicks);
        const auto step = state->timingWarpPlaybackTick
            % static_cast<uint64_t>(cycle) + 1u;
        self.accessibilityValue = [NSString stringWithFormat:
            @"%@ warp playback, step %llu of %u",
            state->timingWarpPlaybackFromSong ? @"Song" : @"Pattern",
            static_cast<unsigned long long>(step), cycle];
    } else {
        self.accessibilityValue = @"Warp playback inactive";
    }
    [self setNeedsDisplay:YES];
}

@end

@interface S3GTrackerWarpWindowController ()
    <NSWindowDelegate, NSTextFieldDelegate>
@property(nonatomic, assign) TrackerViewState* trackerState;
@property(nonatomic, assign) WorkspaceCallbacks* trackerCallbacks;
@property(nonatomic, strong) S3GTrackerWarpRootView* rootView;
@property(nonatomic, strong) S3GTrackerToolboxView* fieldPanel;
@property(nonatomic, strong) S3GTrackerToolboxView* libraryPanel;
@property(nonatomic, strong) S3GTrackerToolboxView* stackPanel;
@property(nonatomic, strong) S3GTrackerToolboxView* transformPanel;
@property(nonatomic, strong) S3GTrackerWarpCurveView* curveView;
@property(nonatomic, strong) S3GTrackerPopupButton* libraryPopup;
@property(nonatomic, strong) NSTextField* libraryNameField;
@property(nonatomic, strong) S3GTrackerActionButton* warpModeButton;
@property(nonatomic, strong) NSTextField* cycleField;
@property(nonatomic, strong) S3GTrackerPopupButton* transformPopup;
@property(nonatomic, strong) S3GTrackerPopupButton* typePopup;
@property(nonatomic, strong) NSTextField* primaryLabel;
@property(nonatomic, strong) NSTextField* primaryField;
@property(nonatomic, strong) NSTextField* pulsesLabel;
@property(nonatomic, strong) NSTextField* pulsesField;
@property(nonatomic, strong) NSTextField* mixField;
@property(nonatomic, strong) NSTextField* beginField;
@property(nonatomic, strong) NSTextField* endField;
@property(nonatomic, strong) NSTextField* repeatsField;
@property(nonatomic, strong) NSTextField* statusLabel;
@property(nonatomic, copy) NSArray<NSTextField*>* libraryLabels;
@property(nonatomic, copy) NSArray<NSTextField*>* stackLabels;
@property(nonatomic, copy) NSArray<NSTextField*>* transformLabels;
@property(nonatomic, strong) S3GTrackerActionButton* saveLibraryButton;
@property(nonatomic, strong) S3GTrackerActionButton* deleteLibraryButton;
@property(nonatomic, copy) NSArray<NSButton*>* addButtons;
@property(nonatomic, copy) NSArray<NSButton*>* stackButtons;
@property(nonatomic) NSInteger selectedTransform;
@property(nonatomic) NSInteger selectedLibrarySlot;
@end

@implementation S3GTrackerWarpWindowController

- (NSTextField*)sliderFieldWithAction:(SEL)action
{
    S3GTrackerProcessorSliderField* field =
        [[S3GTrackerProcessorSliderField alloc] initWithFrame:NSZeroRect];
    field.target = self;
    field.action = action;
    return field;
}

- (NSTextField*)nameFieldWithAction:(SEL)action
{
    NSTextField* field = [[NSTextField alloc] initWithFrame:NSZeroRect];
    S3GTrackerStyleSuiteTextField(field, NSTextAlignmentLeft);
    field.delegate = self;
    field.target = self;
    field.action = action;
    field.accessibilityHelp = @"Enter a name, then press Return or SAVE.";
    return field;
}

- (void)configureSliderField:(NSTextField*)field
    minimum:(double)minimum maximum:(double)maximum
    fractionDigits:(NSUInteger)fractionDigits
{
    S3GTrackerProcessorSliderField* slider =
        static_cast<S3GTrackerProcessorSliderField*>(field);
    S3GTrackerConfigureProcessorSlider(slider, minimum, maximum,
        fractionDigits, self, slider.action);
}

- (S3GTrackerActionButton*)warpButton:(NSString*)title
    action:(SEL)action panel:(NSView*)panel
{
    S3GTrackerActionButton* button = [[S3GTrackerActionButton alloc]
        initWithFrame:NSZeroRect];
    button.s3gUsesSuiteStyle = YES;
    button.title = title;
    button.target = self;
    button.action = action;
    [panel addSubview:button];
    return button;
}

- (NSTextField*)warpRowLabel:(NSString*)title panel:(NSView*)panel
{
    S3GTrackerSuiteLabel* label = [[S3GTrackerSuiteLabel alloc]
        initWithFrame:NSZeroRect];
    label.stringValue = title;
    [panel addSubview:label];
    return label;
}

- (void)layoutWarpInterface
{
    if (!self.rootView) return;
    const auto family = s3g::gui_layout::trackerWarpFamilyLayout({
        static_cast<double>(NSWidth(self.rootView.bounds)),
        static_cast<double>(NSHeight(self.rootView.bounds)),
    });
    const auto cocoaRect = [](const s3g::gui_layout::Rect& rect) {
        return NSMakeRect(static_cast<CGFloat>(rect.x),
            static_cast<CGFloat>(rect.y),
            static_cast<CGFloat>(rect.width),
            static_cast<CGFloat>(rect.height));
    };
    self.fieldPanel.frame = cocoaRect(family.fieldPanel);
    self.libraryPanel.frame = cocoaRect(family.library.frame);
    self.stackPanel.frame = cocoaRect(family.stack.frame);
    self.transformPanel.frame = cocoaRect(family.transform.frame);

    const CGFloat header = static_cast<CGFloat>(
        s3g::gui_layout::kStandardMetrics.headerHeight);
    self.curveView.frame = NSMakeRect(1.0, header,
        std::max<CGFloat>(0.0, NSWidth(self.fieldPanel.bounds) - 2.0),
        std::max<CGFloat>(0.0, NSHeight(self.fieldPanel.bounds) - header - 1.0));

    const CGFloat labelX = static_cast<CGFloat>(
        s3g::gui_layout::kStandardMetrics.labelInset);
    const CGFloat controlX = static_cast<CGFloat>(
        s3g::gui_layout::kStandardMetrics.controlInset);
    const CGFloat rowPitch = static_cast<CGFloat>(
        s3g::gui_layout::kStandardMetrics.rowPitch);
    const CGFloat firstRow = static_cast<CGFloat>(
        s3g::gui_layout::kStandardMetrics.firstRowOffset);
    const auto layoutRows = ^(S3GTrackerToolboxView* panel,
        NSArray<NSTextField*>* labels) {
        const CGFloat controlWidth = std::max<CGFloat>(20.0,
            NSWidth(panel.bounds) - controlX - static_cast<CGFloat>(
                s3g::gui_layout::kStandardMetrics.panelRightInset));
        for (NSUInteger row = 0u; row < labels.count; ++row) {
            const CGFloat y = firstRow + static_cast<CGFloat>(row) * rowPitch;
            labels[row].frame = NSMakeRect(labelX, y - 1.0,
                std::max<CGFloat>(20.0, controlX - labelX - 6.0), 15.0);
        }
        return controlWidth;
    };
    const CGFloat libraryWidth = layoutRows(
        self.libraryPanel, self.libraryLabels);
    const CGFloat stackWidth = layoutRows(
        self.stackPanel, self.stackLabels);
    const CGFloat transformWidth = layoutRows(
        self.transformPanel, self.transformLabels);
    const auto controlFrame = ^NSRect(NSUInteger row, CGFloat width) {
        const CGFloat y = firstRow + static_cast<CGFloat>(row) * rowPitch;
        return NSMakeRect(controlX, y - 1.0, width, 15.0);
    };
    const auto sliderFrame = ^NSRect(NSUInteger row, CGFloat width) {
        const CGFloat y = firstRow + static_cast<CGFloat>(row) * rowPitch;
        return NSMakeRect(controlX, y - 8.0, width,
            static_cast<CGFloat>(
                s3g::gui_layout::kStandardMetrics.hitHeight));
    };
    const auto layoutButtons = ^(NSArray<NSButton*>* buttons,
        NSUInteger row, CGFloat width) {
        const CGFloat gap = 4.0;
        const CGFloat buttonWidth = (width
            - gap * static_cast<CGFloat>(buttons.count - 1u))
            / static_cast<CGFloat>(std::max<NSUInteger>(1u, buttons.count));
        for (NSUInteger index = 0u; index < buttons.count; ++index) {
            NSRect frame = controlFrame(row, buttonWidth);
            frame.origin.x += static_cast<CGFloat>(index)
                * (buttonWidth + gap);
            buttons[index].frame = frame;
        }
    };

    self.libraryPopup.frame = controlFrame(0u, libraryWidth);
    self.libraryNameField.frame = sliderFrame(1u, libraryWidth);
    self.saveLibraryButton.frame = NSMakeRect(
        NSWidth(self.libraryPanel.bounds) - 82.0, 3.0, 70.0, 15.0);
    self.deleteLibraryButton.frame = controlFrame(2u, libraryWidth);
    self.warpModeButton.frame = controlFrame(0u, stackWidth);
    self.cycleField.frame = sliderFrame(1u, stackWidth);
    self.transformPopup.frame = controlFrame(2u, stackWidth);
    layoutButtons(self.addButtons, 3u, stackWidth);
    layoutButtons(self.stackButtons, 4u, stackWidth);
    self.typePopup.frame = controlFrame(0u, transformWidth);
    NSArray<NSControl*>* transformSliders = @[
        self.primaryField, self.pulsesField, self.mixField,
        self.beginField, self.endField, self.repeatsField,
    ];
    for (NSUInteger index = 0u; index < transformSliders.count; ++index)
        transformSliders[index].frame = sliderFrame(index + 1u,
            transformWidth);
    self.statusLabel.frame = NSMakeRect(labelX,
        std::max<CGFloat>(firstRow + rowPitch * 7.0 + 2.0,
            NSHeight(self.transformPanel.bounds) - 43.0),
        std::max<CGFloat>(20.0, NSWidth(self.transformPanel.bounds)
            - labelX * 2.0), 35.0);
}

- (instancetype)initWithState:(TrackerViewState*)state
    callbacks:(WorkspaceCallbacks*)callbacks
{
    NSWindow* window = [[NSWindow alloc] initWithContentRect:
        NSMakeRect(0.0, 0.0, 820.0, 580.0)
        styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
            | NSWindowStyleMaskResizable)
        backing:NSBackingStoreBuffered defer:NO];
    self = [super initWithWindow:window];
    if (!self) return nil;
    self.trackerState = state;
    self.trackerCallbacks = callbacks;
    self.selectedTransform = 0;
    self.selectedLibrarySlot = 0;
    window.title = @"s3g Tracker — Functional Timing Warps";
    window.delegate = self;
    window.releasedWhenClosed = NO;
    window.minSize = NSMakeSize(720.0, 530.0);

    self.rootView = [[S3GTrackerWarpRootView alloc]
        initWithFrame:window.contentView.bounds];
    self.rootView.layoutOwner = self;
    self.rootView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    window.contentView = self.rootView;

    self.fieldPanel = [[S3GTrackerToolboxView alloc] initWithFrame:NSZeroRect];
    self.fieldPanel.toolboxTitle = @"WARP FUNCTION  /  INPUT → WARPED PHASE";
    [self.rootView addSubview:self.fieldPanel];
    self.libraryPanel = [[S3GTrackerToolboxView alloc] initWithFrame:NSZeroRect];
    self.libraryPanel.toolboxIndex = 0;
    self.libraryPanel.toolboxTitle = @"WARP LIBRARY";
    [self.rootView addSubview:self.libraryPanel];
    self.stackPanel = [[S3GTrackerToolboxView alloc] initWithFrame:NSZeroRect];
    self.stackPanel.toolboxIndex = 0;
    self.stackPanel.toolboxTitle = @"SERIAL STACK";
    [self.rootView addSubview:self.stackPanel];
    self.transformPanel = [[S3GTrackerToolboxView alloc] initWithFrame:NSZeroRect];
    self.transformPanel.toolboxIndex = 0;
    self.transformPanel.toolboxTitle = @"SELECTED TRANSFORM";
    [self.rootView addSubview:self.transformPanel];

    self.libraryPopup = [[S3GTrackerPopupButton alloc]
        initWithFrame:NSZeroRect pullsDown:NO];
    self.libraryPopup.s3gUsesCanvasMenu = YES;
    self.libraryPopup.target = self;
    self.libraryPopup.action = @selector(librarySlotSelected:);
    [self.libraryPanel addSubview:self.libraryPopup];
    self.libraryNameField = [self nameFieldWithAction:
        @selector(saveLibrarySlot:)];
    [self.libraryPanel addSubview:self.libraryNameField];
    self.saveLibraryButton = [self warpButton:@"SAVE"
        action:@selector(saveLibrarySlot:) panel:self.libraryPanel];
    self.saveLibraryButton.accessibilityLabel = @"Save Warp slot";
    self.saveLibraryButton.toolTip =
        @"Save the current warp stack and NAME to this slot";
    self.deleteLibraryButton = [self warpButton:@"DELETE SLOT"
        action:@selector(deleteLibrarySlot:) panel:self.libraryPanel];
    self.deleteLibraryButton.tag = 2;
    self.deleteLibraryButton.accessibilityLabel = @"Delete Warp slot";
    self.deleteLibraryButton.toolTip =
        @"Delete this saved Warp without changing the current stack";
    self.libraryLabels = @[
        [self warpRowLabel:@"SLOT" panel:self.libraryPanel],
        [self warpRowLabel:@"NAME" panel:self.libraryPanel],
        [self warpRowLabel:@"SLOT EDIT" panel:self.libraryPanel],
    ];

    self.warpModeButton = [self warpButton:@"WARP PLAYBACK: OFF"
        action:@selector(toggleWarpMode:) panel:self.stackPanel];
    self.warpModeButton.buttonType = NSButtonTypeToggle;
    self.warpModeButton.tag = 1;
    self.warpModeButton.accessibilityHelp =
        @"Enable or bypass the current warp stack during Pattern playback.";
    self.cycleField = [self sliderFieldWithAction:@selector(cycleChanged:)];
    [self configureSliderField:self.cycleField minimum:1.0 maximum:16.0
        fractionDigits:0u];
    [self.stackPanel addSubview:self.cycleField];
    self.transformPopup = [[S3GTrackerPopupButton alloc]
        initWithFrame:NSZeroRect pullsDown:NO];
    self.transformPopup.s3gUsesCanvasMenu = YES;
    self.transformPopup.target = self;
    self.transformPopup.action = @selector(transformSelected:);
    [self.stackPanel addSubview:self.transformPopup];
    NSMutableArray<NSButton*>* addButtons = [[NSMutableArray alloc] init];
    for (NSArray* spec in @[
             @[ @"+ EXP", @0 ], @[ @"+ STEP", @1 ], @[ @"+ EUCLID", @2 ] ]) {
        S3GTrackerActionButton* button = [self warpButton:spec[0]
            action:@selector(addTransform:) panel:self.stackPanel];
        button.identifier = [NSString stringWithFormat:@"warp-add-%@", spec[1]];
        [addButtons addObject:button];
    }
    self.addButtons = addButtons;
    S3GTrackerActionButton* remove = [self warpButton:@"REMOVE"
        action:@selector(removeTransform:) panel:self.stackPanel];
    S3GTrackerActionButton* clear = [self warpButton:@"CLEAR"
        action:@selector(clearTransforms:) panel:self.stackPanel];
    clear.tag = 2;
    self.stackButtons = @[ remove, clear ];
    self.stackLabels = @[
        [self warpRowLabel:@"MODE" panel:self.stackPanel],
        [self warpRowLabel:@"CYCLE" panel:self.stackPanel],
        [self warpRowLabel:@"TRANSFORM" panel:self.stackPanel],
        [self warpRowLabel:@"ADD" panel:self.stackPanel],
        [self warpRowLabel:@"EDIT" panel:self.stackPanel],
    ];

    self.curveView = [[S3GTrackerWarpCurveView alloc] initWithFrame:NSZeroRect];
    self.curveView.trackerState = state;
    self.curveView.accessibilityElement = YES;
    self.curveView.accessibilityRole = NSAccessibilityImageRole;
    self.curveView.accessibilityLabel = @"Composite timing warp curve";
    [self.fieldPanel addSubview:self.curveView];

    self.typePopup = [[S3GTrackerPopupButton alloc]
        initWithFrame:NSZeroRect pullsDown:NO];
    self.typePopup.s3gUsesCanvasMenu = YES;
    for (NSArray* spec in @[
             @[ @"EXPONENTIAL", @0 ], @[ @"STEP QUANTIZE", @1 ],
             @[ @"EUCLIDEAN QUANTIZE", @2 ] ]) {
        [self.typePopup addItemWithTitle:spec[0]];
        self.typePopup.lastItem.representedObject = spec[1];
    }
    self.typePopup.target = self;
    self.typePopup.action = @selector(typeChanged:);
    [self.transformPanel addSubview:self.typePopup];
    self.primaryLabel = [self warpRowLabel:@"POWER"
        panel:self.transformPanel];
    self.primaryField = [self sliderFieldWithAction:
        @selector(transformChanged:)];
    [self configureSliderField:self.primaryField minimum:0.1 maximum:16.0
        fractionDigits:3u];
    [self.transformPanel addSubview:self.primaryField];
    self.pulsesLabel = [self warpRowLabel:@"PULSES"
        panel:self.transformPanel];
    self.pulsesField = [self sliderFieldWithAction:
        @selector(transformChanged:)];
    [self configureSliderField:self.pulsesField minimum:1.0
        maximum:static_cast<double>(s3g::tracker::kMaximumLiveWarpSteps)
        fractionDigits:0u];
    [self.transformPanel addSubview:self.pulsesField];
    self.mixField = [self sliderFieldWithAction:
        @selector(transformChanged:)];
    [self configureSliderField:self.mixField minimum:0.0 maximum:1.0
        fractionDigits:2u];
    [self.transformPanel addSubview:self.mixField];
    self.beginField = [self sliderFieldWithAction:
        @selector(transformChanged:)];
    [self configureSliderField:self.beginField minimum:0.0 maximum:1.0
        fractionDigits:2u];
    [self.transformPanel addSubview:self.beginField];
    self.endField = [self sliderFieldWithAction:
        @selector(transformChanged:)];
    [self configureSliderField:self.endField minimum:0.0 maximum:1.0
        fractionDigits:2u];
    [self.transformPanel addSubview:self.endField];
    self.repeatsField = [self sliderFieldWithAction:
        @selector(transformChanged:)];
    [self configureSliderField:self.repeatsField minimum:1.0
        maximum:static_cast<double>(
            s3g::tracker::kMaximumLiveWarpRepetitions)
        fractionDigits:0u];
    [self.transformPanel addSubview:self.repeatsField];
    self.transformLabels = @[
        [self warpRowLabel:@"TYPE" panel:self.transformPanel],
        self.primaryLabel,
        self.pulsesLabel,
        [self warpRowLabel:@"MIX" panel:self.transformPanel],
        [self warpRowLabel:@"SEGMENT START" panel:self.transformPanel],
        [self warpRowLabel:@"SEGMENT END" panel:self.transformPanel],
        [self warpRowLabel:@"REPEAT" panel:self.transformPanel],
    ];

    self.statusLabel = warpLabel(@"SERIAL STACK • INPUT PHASE → WARPED PHASE", 8.5);
    self.statusLabel.textColor = S3GTrackerThemeColor(
        S3GTrackerThemeRole::TextMuted);
    self.statusLabel.maximumNumberOfLines = 2;
    self.statusLabel.lineBreakMode = NSLineBreakByWordWrapping;
    [self.transformPanel addSubview:self.statusLabel];
    [self layoutWarpInterface];
    S3GTrackerRestoreWindowFrame(window, @"S3GTrackerWarpWindow");
    [self reloadModel];
    return self;
}

- (void)publish
{
    if (self.trackerCallbacks && self.trackerCallbacks->transportChanged)
        self.trackerCallbacks->transportChanged();
    [self reloadModel];
}

- (void)controlTextDidBeginEditing:(NSNotification*)notification
{
    if (notification.object == self.libraryNameField)
        S3GTrackerStyleTextEditor(self.libraryNameField);
}

- (void)reloadModel
{
    if (!self.trackerState || !self.window) return;
    const auto& library = self.trackerState->session.warpLibrary;
    [self.libraryPopup removeAllItems];
    for (std::size_t index = 0u;
         index < s3g::tracker::kMaximumTimingWarpLibraryEntries; ++index) {
        const auto* entry = library.entry(index);
        NSString* title = entry
            ? [NSString stringWithFormat:@"%02lu  ·  %@  ·  %uT / %luX",
                static_cast<unsigned long>(index + 1u),
                [NSString stringWithUTF8String:entry->name.c_str()],
                entry->cycleTicks,
                static_cast<unsigned long>(entry->stack.size())]
            : [NSString stringWithFormat:@"%02lu  ·  EMPTY",
                static_cast<unsigned long>(index + 1u)];
        [self.libraryPopup addItemWithTitle:title];
    }
    self.selectedLibrarySlot = std::clamp<NSInteger>(
        self.selectedLibrarySlot, 0,
        static_cast<NSInteger>(
            s3g::tracker::kMaximumTimingWarpLibraryEntries - 1u));
    [self.libraryPopup selectItemAtIndex:self.selectedLibrarySlot];
    const auto* selectedEntry = library.entry(
        static_cast<std::size_t>(self.selectedLibrarySlot));
    self.libraryNameField.stringValue = selectedEntry
        ? [NSString stringWithUTF8String:selectedEntry->name.c_str()] : @"";
    self.saveLibraryButton.enabled = YES;
    self.deleteLibraryButton.enabled = selectedEntry != nullptr;
    const auto& transport = self.trackerState->session.transport;
    self.warpModeButton.state = transport.timingWarpEnabled
        ? NSControlStateValueOn : NSControlStateValueOff;
    self.warpModeButton.title = transport.timingWarpEnabled
        ? @"WARP PLAYBACK: ON" : @"WARP PLAYBACK: OFF";
    [self.warpModeButton setNeedsDisplay:YES];
    self.cycleField.integerValue = transport.warpCycleTicks;
    const auto count = transport.timingWarp.size();
    [self.transformPopup removeAllItems];
    for (std::size_t index = 0u; index < count; ++index) {
        const auto* transform = transport.timingWarp.transform(index);
        if (transform)
            [self.transformPopup addItemWithTitle:transformSummary(
                *transform, index)];
    }
    if (count == 0u) {
        [self.transformPopup addItemWithTitle:@"NO TRANSFORMS"];
        self.selectedTransform = 0;
    } else {
        self.selectedTransform = std::clamp<NSInteger>(
            self.selectedTransform, 0, static_cast<NSInteger>(count - 1u));
        [self.transformPopup selectItemAtIndex:self.selectedTransform];
    }
    const auto* transform = count == 0u ? nullptr
        : transport.timingWarp.transform(
            static_cast<std::size_t>(self.selectedTransform));
    const BOOL enabled = transform != nullptr;
    for (NSControl* control in @[ self.typePopup, self.primaryField,
             self.pulsesField, self.mixField, self.beginField,
             self.endField, self.repeatsField ]) control.enabled = enabled;
    if (transform) {
        [self.typePopup selectItemAtIndex:static_cast<NSInteger>(
            transform->kind)];
        [self configureSliderField:self.primaryField
            minimum:transform->kind == TimingWarpKind::Exponential
                ? 0.1 : 1.0
            maximum:transform->kind == TimingWarpKind::Exponential
                ? 16.0
                : static_cast<double>(
                    s3g::tracker::kMaximumLiveWarpSteps)
            fractionDigits:transform->kind == TimingWarpKind::Exponential
                ? 3u : 0u];
        self.primaryLabel.stringValue = transform->kind
                == TimingWarpKind::Exponential ? @"POWER" : @"STEPS";
        self.primaryField.doubleValue = transform->kind
                == TimingWarpKind::Exponential
            ? transform->exponent : transform->steps;
        const BOOL euclidean = transform->kind
            == TimingWarpKind::EuclideanQuantize;
        self.pulsesLabel.hidden = !euclidean;
        self.pulsesField.hidden = !euclidean;
        if (euclidean) {
            [self configureSliderField:self.pulsesField minimum:1.0
                maximum:static_cast<double>(transform->steps)
                fractionDigits:0u];
        }
        self.pulsesField.integerValue = transform->pulses;
        self.mixField.doubleValue = transform->options.alpha;
        self.beginField.doubleValue = transform->options.phaseBegin;
        self.endField.doubleValue = transform->options.phaseEnd;
        self.repeatsField.integerValue = transform->options.repetitions;
        self.statusLabel.stringValue = [NSString stringWithFormat:
            @"%@ • %lu TRANSFORM%@ • SERIAL LEFT → RIGHT\n%lu SAVED • SONG RECALLS SAVED SLOTS",
            transport.timingWarpEnabled ? @"PLAYBACK ON" : @"BYPASSED",
            static_cast<unsigned long>(count), count == 1u ? @"" : @"S",
            static_cast<unsigned long>(library.size())];
    } else {
        self.primaryLabel.stringValue = @"POWER";
        self.pulsesLabel.hidden = YES;
        self.pulsesField.hidden = YES;
        self.statusLabel.stringValue = [NSString stringWithFormat:
            @"%@ • IDENTITY TIMING • ADD EXP, STEP, OR EUCLID\n%lu SAVED WARP%@",
            transport.timingWarpEnabled ? @"PLAYBACK ON" : @"BYPASSED",
            static_cast<unsigned long>(library.size()),
            library.size() == 1u ? @"" : @"S"];
    }
    [self.curveView refreshPlaybackDisplay];
}

- (void)refreshPlaybackDisplay
{
    [self.curveView refreshPlaybackDisplay];
}

- (void)librarySlotSelected:(id)sender
{
    (void)sender;
    self.selectedLibrarySlot = std::max<NSInteger>(0,
        self.libraryPopup.indexOfSelectedItem);
    if (!self.trackerState) return;
    const auto index = static_cast<std::size_t>(self.selectedLibrarySlot);
    const auto* entry = self.trackerState->session.warpLibrary.entry(index);
    if (!entry) {
        [self reloadModel];
        return;
    }
    self.trackerState->session.transport.warpCycleTicks = entry->cycleTicks;
    self.trackerState->session.transport.timingWarp = entry->stack;
    self.selectedTransform = 0;
    [self publish];
}

- (void)saveLibrarySlot:(id)sender
{
    (void)sender;
    if (!self.trackerState) return;
    NSString* value = [self.libraryNameField.stringValue
        stringByTrimmingCharactersInSet:
            NSCharacterSet.whitespaceAndNewlineCharacterSet];
    const auto index = static_cast<std::size_t>(std::max<NSInteger>(0,
        self.selectedLibrarySlot));
    if (value.length == 0u)
        value = [NSString stringWithFormat:@"WARP %02lu",
            static_cast<unsigned long>(index + 1u)];
    const char* utf8 = value.UTF8String;
    const auto& transport = self.trackerState->session.transport;
    if (!utf8 || !self.trackerState->session.warpLibrary.store(index,
            std::string(utf8), transport.warpCycleTicks,
            transport.timingWarp)) {
        NSBeep();
        [self reloadModel];
        return;
    }
    [self publish];
}

- (void)deleteLibrarySlot:(id)sender
{
    (void)sender;
    if (!self.trackerState) return;
    const auto index = static_cast<std::size_t>(std::max<NSInteger>(0,
        self.selectedLibrarySlot));
    if (!self.trackerState->session.warpLibrary.erase(index)) {
        NSBeep();
        return;
    }
    [self publish];
}

- (void)cycleChanged:(id)sender
{
    (void)sender;
    if (!self.trackerState) return;
    const NSInteger value = self.cycleField.integerValue;
    if (value < 1 || value > 16) {
        NSBeep();
        [self reloadModel];
        return;
    }
    self.trackerState->session.transport.warpCycleTicks
        = static_cast<uint32_t>(value);
    [self publish];
}

- (void)toggleWarpMode:(NSButton*)sender
{
    if (!self.trackerState) return;
    self.trackerState->session.transport.timingWarpEnabled
        = sender.state == NSControlStateValueOn;
    [self publish];
}

- (void)transformSelected:(id)sender
{
    (void)sender;
    self.selectedTransform = std::max<NSInteger>(0,
        self.transformPopup.indexOfSelectedItem);
    [self reloadModel];
}

- (void)addTransform:(NSButton*)sender
{
    if (!self.trackerState) return;
    auto stack = self.trackerState->session.transport.timingWarp;
    TimingWarpTransform transform;
    if ([sender.identifier isEqualToString:@"warp-add-1"])
        transform = TimingWarpTransform::stepQuantize(8u);
    else if ([sender.identifier isEqualToString:@"warp-add-2"])
        transform = TimingWarpTransform::euclideanQuantize(3u, 8u);
    else
        transform = TimingWarpTransform::exponential(2.0);
    const auto result = stack.append(transform);
    if (!result.added()) { NSBeep(); return; }
    self.trackerState->session.transport.timingWarp = stack;
    self.selectedTransform = static_cast<NSInteger>(stack.size() - 1u);
    [self publish];
}

- (void)removeTransform:(id)sender
{
    (void)sender;
    if (!self.trackerState) return;
    const auto& source = self.trackerState->session.transport.timingWarp;
    if (source.empty()) return;
    std::array<TimingWarpTransform,
        TimingWarpStack::kMaximumTransforms> transforms {};
    std::size_t count = 0u;
    for (std::size_t index = 0u; index < source.size(); ++index) {
        if (index == static_cast<std::size_t>(self.selectedTransform)) continue;
        const auto* transform = source.transform(index);
        if (transform) transforms[count++] = *transform;
    }
    TimingWarpStack replacement;
    (void)replacement.compile(transforms.data(), count);
    self.trackerState->session.transport.timingWarp = replacement;
    self.selectedTransform = std::max<NSInteger>(0,
        self.selectedTransform - 1);
    [self publish];
}

- (void)clearTransforms:(id)sender
{
    (void)sender;
    if (!self.trackerState) return;
    self.trackerState->session.transport.timingWarp.clear();
    self.selectedTransform = 0;
    [self publish];
}

- (void)typeChanged:(id)sender
{
    (void)sender;
    [self transformChanged:self.typePopup];
}

- (void)transformChanged:(id)sender
{
    if (!self.trackerState) return;
    const auto& source = self.trackerState->session.transport.timingWarp;
    if (source.empty() || self.selectedTransform < 0
        || static_cast<std::size_t>(self.selectedTransform)
            >= source.size()) return;
    std::array<TimingWarpTransform,
        TimingWarpStack::kMaximumTransforms> transforms {};
    for (std::size_t index = 0u; index < source.size(); ++index) {
        const auto* transform = source.transform(index);
        if (transform) transforms[index] = *transform;
    }
    auto& edited = transforms[static_cast<std::size_t>(
        self.selectedTransform)];
    const auto kind = static_cast<TimingWarpKind>(
        self.typePopup.indexOfSelectedItem);
    TimingWarpOptions options;
    options.alpha = self.mixField.doubleValue;
    options.phaseBegin = self.beginField.doubleValue;
    options.phaseEnd = self.endField.doubleValue;
    const NSInteger repetitions = self.repeatsField.integerValue;
    const double primary = self.primaryField.doubleValue;
    const NSInteger pulses = self.pulsesField.integerValue;
    const bool optionsValid = std::isfinite(options.alpha)
        && options.alpha >= 0.0 && options.alpha <= 1.0
        && std::isfinite(options.phaseBegin)
        && std::isfinite(options.phaseEnd)
        && options.phaseBegin >= 0.0 && options.phaseEnd <= 1.0
        && options.phaseBegin < options.phaseEnd
        && repetitions >= 1
        && repetitions <= static_cast<NSInteger>(
            s3g::tracker::kMaximumLiveWarpRepetitions);
    if (!optionsValid) {
        NSBeep(); [self reloadModel]; return;
    }
    options.repetitions = static_cast<uint32_t>(repetitions);
    if (sender == self.typePopup && kind != edited.kind) {
        if (kind == TimingWarpKind::StepQuantize)
            edited = TimingWarpTransform::stepQuantize(8u, options);
        else if (kind == TimingWarpKind::EuclideanQuantize)
            edited = TimingWarpTransform::euclideanQuantize(3u, 8u, options);
        else edited = TimingWarpTransform::exponential(2.0, options);
    } else if (kind == TimingWarpKind::Exponential) {
        if (!std::isfinite(primary) || primary <= 0.0) {
            NSBeep(); [self reloadModel]; return;
        }
        edited = TimingWarpTransform::exponential(primary, options);
    } else {
        const NSInteger steps = static_cast<NSInteger>(std::llround(primary));
        if (steps < 1 || steps > static_cast<NSInteger>(
                s3g::tracker::kMaximumLiveWarpSteps)
            || (kind == TimingWarpKind::EuclideanQuantize
                && (pulses < 1 || pulses > steps))) {
            NSBeep(); [self reloadModel]; return;
        }
        edited = kind == TimingWarpKind::StepQuantize
            ? TimingWarpTransform::stepQuantize(
                static_cast<uint32_t>(steps), options)
            : TimingWarpTransform::euclideanQuantize(
                static_cast<uint32_t>(pulses),
                static_cast<uint32_t>(steps), options);
    }
    TimingWarpStack replacement;
    const auto report = replacement.compile(transforms.data(), source.size());
    if (report.rejected != 0u) {
        NSBeep(); [self reloadModel]; return;
    }
    self.trackerState->session.transport.timingWarp = replacement;
    [self publish];
}

@end
