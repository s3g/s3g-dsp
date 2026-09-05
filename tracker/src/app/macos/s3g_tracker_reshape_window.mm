#import "s3g_tracker_reshape_window.h"

#import "s3g_tracker_controls.h"
#import "s3g_tracker_workspace.h"

#include "s3g_gui_layout.h"
#define S3G_COCOA_GUI_DRAWING_ONLY 1
#include "s3g_cocoa_gui.h"
#undef S3G_COCOA_GUI_DRAWING_ONLY

#include "s3g/tracker/pattern_reshape.h"

#include <algorithm>
#include <cmath>
#include <string>

using s3g::tracker::PatternReshapeAnalysis;
using s3g::tracker::PatternReshapeResult;
using s3g::tracker::PatternReshapeSettings;
using s3g::tracker::PatternReshapeWriteMode;
using s3g::tracker::reshapePattern;
using s3g::tracker::app::TrackerViewState;
using s3g::tracker::app::WorkspaceCallbacks;

NSString* reshapeString(const std::string& text)
{
    NSString* result = [NSString stringWithUTF8String:text.c_str()];
    return result ? result : @"";
}

NSString* percentString(double value)
{
    return [NSString stringWithFormat:@"%.0f%%", value];
}

PatternReshapeSettings defaultPanelSettings()
{
    PatternReshapeSettings settings;
    settings.microTimingWrite = PatternReshapeWriteMode::FillMissing;
    settings.mutationAmount = 0.55f;
    settings.densityChange = 0.15f;
    settings.syncopation = 0.35f;
    settings.displacementRows = 1u;
    settings.burstChance = 0.20f;
    settings.cycleDrift = 0.20f;
    return settings;
}

@class S3GTrackerReshapeWindowController;

@interface S3GTrackerReshapeRootView : S3GTrackerFocusReleaseView
@property(nonatomic, weak) S3GTrackerReshapeWindowController* layoutOwner;
@end

@interface S3GTrackerPatternProfileView : NSView {
    PatternReshapeAnalysis _beforeAnalysis;
    PatternReshapeAnalysis _afterAnalysis;
}
@property(nonatomic, assign) TrackerViewState* trackerState;
@property(nonatomic) BOOL displaysReshaped;
- (void)setBeforeAnalysis:(const PatternReshapeAnalysis&)before
    afterAnalysis:(const PatternReshapeAnalysis&)after;
@end

@interface S3GTrackerReshapeWindowController ()
    <NSWindowDelegate>
@property(nonatomic, assign) TrackerViewState* trackerState;
@property(nonatomic, assign) WorkspaceCallbacks* trackerCallbacks;
@property(nonatomic, strong) S3GTrackerReshapeRootView* rootView;
@property(nonatomic, strong) S3GTrackerToolboxView* profilePanel;
@property(nonatomic, strong) S3GTrackerToolboxView* mutationPanel;
@property(nonatomic, strong) S3GTrackerToolboxView* targetPanel;
@property(nonatomic, strong) S3GTrackerToolboxView* timingPanel;
@property(nonatomic, strong) S3GTrackerToolboxView* dynamicsPanel;
@property(nonatomic, strong) S3GTrackerToolboxView* commitPanel;
@property(nonatomic, strong) S3GTrackerPatternProfileView* profileView;
@property(nonatomic, strong) S3GTrackerPopupButton* patternPopup;
@property(nonatomic, strong) S3GTrackerPopupButton* laneScopePopup;
@property(nonatomic, strong) S3GTrackerPopupButton* cyclePopup;
@property(nonatomic, strong) S3GTrackerPopupButton* timingWritePopup;
@property(nonatomic, strong) S3GTrackerPopupButton* timingOutlierPopup;
@property(nonatomic, strong) S3GTrackerPopupButton* velocityWritePopup;
@property(nonatomic, strong) S3GTrackerPopupButton* velocityOutlierPopup;
@property(nonatomic, strong) NSArray<NSTextField*>* targetLabels;
@property(nonatomic, strong) NSArray<NSTextField*>* timingLabels;
@property(nonatomic, strong) NSArray<NSTextField*>* dynamicsLabels;
@property(nonatomic, strong) NSArray<NSTextField*>* mutationLeftLabels;
@property(nonatomic, strong) NSArray<NSTextField*>* mutationRightLabels;
@property(nonatomic, strong) S3GTrackerProcessorSliderField* mutationAmountField;
@property(nonatomic, strong) S3GTrackerProcessorSliderField* densityField;
@property(nonatomic, strong) S3GTrackerProcessorSliderField* syncopationField;
@property(nonatomic, strong) S3GTrackerPopupButton* displacementPopup;
@property(nonatomic, strong) S3GTrackerProcessorSliderField* burstChanceField;
@property(nonatomic, strong) S3GTrackerProcessorSliderField* cycleDriftField;
@property(nonatomic, strong) S3GTrackerActionButton* reseedButton;
@property(nonatomic, strong) NSTextField* anchorValueLabel;
@property(nonatomic, strong) S3GTrackerProcessorSliderField* pocketField;
@property(nonatomic, strong) S3GTrackerProcessorSliderField* tightenField;
@property(nonatomic, strong) S3GTrackerProcessorSliderField* depthField;
@property(nonatomic, strong) S3GTrackerProcessorSliderField* rangeField;
@property(nonatomic, strong) S3GTrackerProcessorSliderField* accentField;
@property(nonatomic, strong) S3GTrackerProcessorSliderField* balanceField;
@property(nonatomic, strong) NSTextField* analysisLabel;
@property(nonatomic, strong) NSTextField* resultLabel;
@property(nonatomic, strong) S3GTrackerActionButton* previewButton;
@property(nonatomic, strong) S3GTrackerActionButton* originalButton;
@property(nonatomic, strong) S3GTrackerActionButton* reshapedButton;
@property(nonatomic, strong) S3GTrackerActionButton* resetButton;
@property(nonatomic, strong) S3GTrackerActionButton* applyButton;
@property(nonatomic, strong) S3GTrackerActionButton* createVariantButton;
@property(nonatomic) PatternReshapeSettings settings;
@property(nonatomic) PatternReshapeResult result;
@property(nonatomic) BOOL previewEnabled;
@property(nonatomic) BOOL showingReshaped;
@property(nonatomic, copy) NSString* loadedPatternId;
- (void)layoutReshapeInterface;
@end

@implementation S3GTrackerReshapeRootView
- (BOOL)isFlipped { return YES; }
- (void)layout
{
    [super layout];
    [self.layoutOwner layoutReshapeInterface];
}
@end

@implementation S3GTrackerPatternProfileView

- (BOOL)isFlipped { return YES; }

- (void)setBeforeAnalysis:(const PatternReshapeAnalysis&)before
    afterAnalysis:(const PatternReshapeAnalysis&)after
{
    _beforeAnalysis = before;
    _afterAnalysis = after;
    self.accessibilityValue = [NSString stringWithFormat:
        @"%lu row cycle, %lu hits, %lu timing and %lu velocity values",
        static_cast<unsigned long>(after.cycleRows),
        static_cast<unsigned long>(after.noteEvents),
        static_cast<unsigned long>(after.timingValues),
        static_cast<unsigned long>(after.velocityValues)];
    [self setNeedsDisplay:YES];
}

- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    [S3GTrackerThemeColor(S3GTrackerThemeRole::Workspace) setFill];
    NSRectFill(self.bounds);

    const PatternReshapeAnalysis& shown = self.displaysReshaped
        ? _afterAnalysis : _beforeAnalysis;
    const CGFloat left = 78.0;
    const CGFloat right = 18.0;
    const CGFloat top = 28.0;
    const CGFloat bottom = 34.0;
    const CGFloat width = std::max<CGFloat>(1.0,
        NSWidth(self.bounds) - left - right);
    const CGFloat height = std::max<CGFloat>(1.0,
        NSHeight(self.bounds) - top - bottom);
    const CGFloat bandGap = 8.0;
    const CGFloat bandHeight = std::max<CGFloat>(12.0,
        (height - bandGap * 3.0) / 4.0);
    const NSRect hitsBand = NSMakeRect(left, top, width, bandHeight);
    const NSRect timingBand = NSMakeRect(left,
        NSMaxY(hitsBand) + bandGap, width, bandHeight);
    const NSRect velocityBand = NSMakeRect(left,
        NSMaxY(timingBand) + bandGap, width, bandHeight);
    const NSRect laneBand = NSMakeRect(left,
        NSMaxY(velocityBand) + bandGap, width, bandHeight);
    const std::size_t cycle = std::max<std::size_t>(1u,
        shown.cycleRows);

    [S3GTrackerThemeColor(S3GTrackerThemeRole::Grid, 0.72) setStroke];
    NSBezierPath* grid = [NSBezierPath bezierPath];
    grid.lineWidth = 1.0;
    const std::size_t step = cycle > 32u ? 4u : (cycle > 16u ? 2u : 1u);
    for (std::size_t phase = 0u; phase <= cycle; phase += step) {
        const CGFloat x = left + width * static_cast<CGFloat>(phase)
            / static_cast<CGFloat>(cycle);
        [grid moveToPoint:NSMakePoint(x, top)];
        [grid lineToPoint:NSMakePoint(x, NSMaxY(velocityBand))];
    }
    for (const CGFloat y : {
             NSMaxY(hitsBand) + bandGap * 0.5,
             NSMaxY(timingBand) + bandGap * 0.5,
             NSMaxY(velocityBand) + bandGap * 0.5,
             NSMidY(timingBand),
         }) {
        [grid moveToPoint:NSMakePoint(left, y)];
        [grid lineToPoint:NSMakePoint(left + width, y)];
    }
    [grid stroke];

    NSDictionary* labelStyle = s3g::clap_gui::softLabelAttrs();
    NSDictionary* scaleStyle = s3g::clap_gui::textAttrs(
        s3g::clap_gui::color(0x737a80), 7.0);
    const auto profileMaximumHits = [](const PatternReshapeAnalysis& analysis) {
        return analysis.phaseNoteEvents.empty() ? std::size_t { 0u }
            : *std::max_element(analysis.phaseNoteEvents.begin(),
                analysis.phaseNoteEvents.end());
    };
    const auto maximumHits = std::max(profileMaximumHits(_beforeAnalysis),
        self.displaysReshaped ? profileMaximumHits(_afterAnalysis) : 0u);
    const double mtRange = self.trackerState
        ? std::clamp(self.trackerState->session.transport
                .microTimingRangeMilliseconds, 0.0, 500.0)
        : 25.0;
    const auto drawBandScale = [&](NSString* title, const NSRect band,
                                   NSString* high, NSString* middle,
                                   NSString* low) {
        [title drawAtPoint:NSMakePoint(7.0, NSMinY(band) + 3.0)
            withAttributes:labelStyle];
        const CGFloat scaleX = 42.0;
        const CGFloat scaleWidth = left - scaleX - 5.0;
        s3g::clap_gui::drawBoundedRightText(high,
            NSMakeRect(scaleX, NSMinY(band), scaleWidth, 10.0), scaleStyle);
        if (middle.length > 0u) {
            s3g::clap_gui::drawBoundedRightText(middle,
                NSMakeRect(scaleX, NSMidY(band) - 5.0,
                    scaleWidth, 10.0), scaleStyle);
        }
        s3g::clap_gui::drawBoundedRightText(low,
            NSMakeRect(scaleX, NSMaxY(band) - 10.0,
                scaleWidth, 10.0), scaleStyle);
    };
    drawBandScale(@"HITS", hitsBand,
        [NSString stringWithFormat:@"%lu",
            static_cast<unsigned long>(maximumHits)], @"", @"0");
    drawBandScale(@"MT", timingBand,
        [NSString stringWithFormat:@"+%.0fms", mtRange], @"0",
        [NSString stringWithFormat:@"−%.0fms", mtRange]);
    drawBandScale(@"VEL", velocityBand, @"127", @"", @"0");
    drawBandScale(@"LANE", laneBand, @"127", @"", @"0");
    NSString* cycleText = [NSString stringWithFormat:
        @"%lu ROW CYCLE  ·  %lu PASS%@",
        static_cast<unsigned long>(cycle),
        static_cast<unsigned long>(shown.passes),
        shown.passes == 1u ? @"" : @"ES"];
    [cycleText drawAtPoint:NSMakePoint(left, NSHeight(self.bounds) - 22.0)
        withAttributes:labelStyle];

    const auto drawHits = [&](const PatternReshapeAnalysis& analysis,
                              BOOL transformed, CGFloat alpha) {
        NSBezierPath* line = [NSBezierPath bezierPath];
        NSBezierPath* points = [NSBezierPath bezierPath];
        line.lineWidth = transformed ? 1.8 : 1.0;
        BOOL started = NO;
        for (std::size_t phase = 0u; phase < cycle; ++phase) {
            const CGFloat x = left + width
                * (static_cast<CGFloat>(phase) + 0.5)
                / static_cast<CGFloat>(cycle);
            const auto hits = phase < analysis.phaseNoteEvents.size()
                ? analysis.phaseNoteEvents[phase] : 0u;
            const CGFloat normalized = maximumHits > 0u
                ? static_cast<CGFloat>(hits)
                    / static_cast<CGFloat>(maximumHits)
                : 0.0;
            const CGFloat y = NSMaxY(hitsBand) - 5.0
                - normalized * std::max<CGFloat>(
                    1.0, NSHeight(hitsBand) - 10.0);
            if (!started) {
                [line moveToPoint:NSMakePoint(x, y)];
                started = YES;
            } else {
                [line lineToPoint:NSMakePoint(x, y)];
            }
            [points appendBezierPathWithOvalInRect:
                NSMakeRect(x - 2.3, y - 2.3, 4.6, 4.6)];
        }
        [S3GTrackerThemeColor(transformed
                ? S3GTrackerThemeRole::Note
                : S3GTrackerThemeRole::TextSecondary, alpha) setStroke];
        [line stroke];
        [points stroke];
    };
    drawHits(_beforeAnalysis, NO, self.displaysReshaped ? 0.38 : 0.92);
    if (self.displaysReshaped) drawHits(_afterAnalysis, YES, 1.0);

    const auto drawProfile = [&](const PatternReshapeAnalysis& analysis,
        BOOL transformed, CGFloat alpha) {
        NSColor* timingColor = transformed
            ? S3GTrackerThemeColor(S3GTrackerThemeRole::Live, alpha)
            : S3GTrackerThemeColor(S3GTrackerThemeRole::TextSecondary, alpha);
        NSColor* velocityColor = transformed
            ? S3GTrackerThemeColor(S3GTrackerThemeRole::Value, alpha)
            : S3GTrackerThemeColor(S3GTrackerThemeRole::TextSecondary, alpha);
        NSBezierPath* timing = [NSBezierPath bezierPath];
        NSBezierPath* timingPoints = [NSBezierPath bezierPath];
        NSBezierPath* velocity = [NSBezierPath bezierPath];
        NSBezierPath* velocityPoints = [NSBezierPath bezierPath];
        NSBezierPath* lanes = [NSBezierPath bezierPath];
        NSBezierPath* lanePoints = [NSBezierPath bezierPath];
        timing.lineWidth = transformed ? 1.8 : 1.0;
        velocity.lineWidth = transformed ? 1.8 : 1.0;
        BOOL timingStarted = NO;
        BOOL velocityStarted = NO;
        BOOL laneStarted = NO;
        for (std::size_t phase = 0u; phase < cycle; ++phase) {
            const CGFloat x = left + width
                * (static_cast<CGFloat>(phase) + 0.5)
                / static_cast<CGFloat>(cycle);
            if (phase < analysis.phaseTimingMedian.size()
                && phase < analysis.phaseTimingSupport.size()
                && analysis.phaseTimingSupport[phase] > 0u) {
                const CGFloat value = std::clamp<CGFloat>(
                    analysis.phaseTimingMedian[phase], -1.0, 1.0);
                const CGFloat y = NSMidY(timingBand)
                    - value * (NSHeight(timingBand) * 0.42);
                if (!timingStarted) { [timing moveToPoint:NSMakePoint(x, y)]; timingStarted = YES; }
                else [timing lineToPoint:NSMakePoint(x, y)];
                [timingPoints appendBezierPathWithOvalInRect:NSMakeRect(
                    x - 2.6, y - 2.6, 5.2, 5.2)];
            }
            if (phase < analysis.phaseVelocityMedian.size()
                && phase < analysis.phaseVelocitySupport.size()
                && analysis.phaseVelocitySupport[phase] > 0u) {
                const CGFloat value = std::clamp<CGFloat>(
                    analysis.phaseVelocityMedian[phase], 0.0, 1.0);
                const CGFloat y = NSMinY(velocityBand) + 5.0
                    + (NSHeight(velocityBand) - 10.0) * (1.0 - value);
                if (!velocityStarted) { [velocity moveToPoint:NSMakePoint(x, y)]; velocityStarted = YES; }
                else [velocity lineToPoint:NSMakePoint(x, y)];
                [velocityPoints appendBezierPathWithOvalInRect:NSMakeRect(
                    x - 2.6, y - 2.6, 5.2, 5.2)];
            }
        }
        [timingColor setStroke];
        [timing stroke];
        [timingPoints stroke];
        [velocityColor setStroke];
        [velocity stroke];
        [velocityPoints stroke];
        const std::size_t laneCount = analysis.lanes.size();
        for (std::size_t lane = 0u; lane < laneCount; ++lane) {
            if (analysis.lanes[lane].velocityValues == 0u) continue;
            const CGFloat x = left + width
                * (static_cast<CGFloat>(lane) + 0.5)
                / static_cast<CGFloat>(std::max<std::size_t>(1u,
                    laneCount));
            const CGFloat value = std::clamp<CGFloat>(
                analysis.lanes[lane].velocityMedian, 0.0, 1.0);
            const CGFloat y = NSMinY(laneBand) + 5.0
                + (NSHeight(laneBand) - 10.0) * (1.0 - value);
            if (!laneStarted) {
                [lanes moveToPoint:NSMakePoint(x, y)];
                laneStarted = YES;
            } else {
                [lanes lineToPoint:NSMakePoint(x, y)];
            }
            [lanePoints appendBezierPathWithOvalInRect:
                NSMakeRect(x - 2.6, y - 2.6, 5.2, 5.2)];
        }
        [velocityColor setStroke];
        [lanes stroke];
        [lanePoints stroke];
    };
    drawProfile(_beforeAnalysis, NO, self.displaysReshaped ? 0.38 : 0.92);
    if (self.displaysReshaped)
        drawProfile(_afterAnalysis, YES, 1.0);

    if (self.trackerState && self.trackerState->playing) {
        const std::size_t lane = std::min<std::size_t>(
            self.trackerState->session.selectedTrack,
            self.trackerState->notePlayheads.size() - 1u);
        const std::size_t phase = self.trackerState->notePlayheads[lane]
            % cycle;
        const CGFloat x = left + width
            * static_cast<CGFloat>(phase) / static_cast<CGFloat>(cycle);
        [S3GTrackerThemeColor(S3GTrackerThemeRole::GridPlaybackAccent,
            0.82) setStroke];
        NSBezierPath* cursor = [NSBezierPath bezierPath];
        cursor.lineWidth = 1.0;
        [cursor moveToPoint:NSMakePoint(x, top)];
        [cursor lineToPoint:NSMakePoint(x, NSMaxY(velocityBand))];
        [cursor stroke];
    }

    if (shown.noteEvents == 0u) {
        NSString* empty = @"NO NOTE ONSETS TO ANALYZE";
        const NSSize size = [empty sizeWithAttributes:labelStyle];
        [empty drawAtPoint:NSMakePoint(
            NSMidX(self.bounds) - size.width * 0.5,
            NSMidY(self.bounds) - size.height * 0.5)
            withAttributes:labelStyle];
    } else if (shown.timingValues == 0u && shown.velocityValues == 0u) {
        NSString* writeHint =
            @"WRITE MT / WRITE VEL CAN AUTHOR MISSING VALUES";
        const NSSize size = [writeHint sizeWithAttributes:labelStyle];
        [writeHint drawAtPoint:NSMakePoint(
            NSMaxX(self.bounds) - right - size.width,
            NSHeight(self.bounds) - 22.0)
            withAttributes:labelStyle];
    }
}

@end

@implementation S3GTrackerReshapeWindowController

- (S3GTrackerSuiteLabel*)rowLabel:(NSString*)title panel:(NSView*)panel
{
    S3GTrackerSuiteLabel* label = [[S3GTrackerSuiteLabel alloc]
        initWithFrame:NSZeroRect];
    label.stringValue = title;
    [panel addSubview:label];
    return label;
}

- (S3GTrackerActionButton*)button:(NSString*)title
    action:(SEL)action panel:(NSView*)panel
{
    S3GTrackerActionButton* button = [[S3GTrackerActionButton alloc]
        initWithFrame:NSZeroRect];
    button.s3gUsesSuiteStyle = YES;
    button.s3gUsesNeutralTitle = YES;
    button.title = title;
    button.target = self;
    button.action = action;
    [panel addSubview:button];
    return button;
}

- (S3GTrackerProcessorSliderField*)slider:(SEL)action
    minimum:(double)minimum
    maximum:(double)maximum panel:(NSView*)panel
{
    S3GTrackerProcessorSliderField* slider =
        [[S3GTrackerProcessorSliderField alloc] initWithFrame:NSZeroRect];
    S3GTrackerConfigureProcessorSlider(slider, minimum, maximum, 0u,
        self, action);
    [panel addSubview:slider];
    return slider;
}

- (S3GTrackerPopupButton*)popup:(SEL)action panel:(NSView*)panel
{
    S3GTrackerPopupButton* popup = [[S3GTrackerPopupButton alloc]
        initWithFrame:NSZeroRect pullsDown:NO];
    popup.s3gUsesCanvasMenu = YES;
    popup.target = self;
    popup.action = action;
    [panel addSubview:popup];
    return popup;
}

- (instancetype)initWithState:(TrackerViewState*)state
    callbacks:(WorkspaceCallbacks*)callbacks
{
    NSWindow* window = [[NSWindow alloc] initWithContentRect:
        NSMakeRect(0.0, 0.0, 900.0, 620.0)
        styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
            | NSWindowStyleMaskResizable)
        backing:NSBackingStoreBuffered defer:NO];
    self = [super initWithWindow:window];
    if (!self) return nil;
    self.trackerState = state;
    self.trackerCallbacks = callbacks;
    self.settings = defaultPanelSettings();
    self.showingReshaped = YES;
    window.title = @"s3g Tracker — Pattern Reshape";
    window.releasedWhenClosed = NO;
    window.minSize = NSMakeSize(760.0, 620.0);

    self.rootView = [[S3GTrackerReshapeRootView alloc]
        initWithFrame:window.contentView.bounds];
    self.rootView.layoutOwner = self;
    self.rootView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    self.rootView.wantsLayer = YES;
    self.rootView.layer.backgroundColor = S3GTrackerThemeColor(
        S3GTrackerThemeRole::Canvas).CGColor;
    window.contentView = self.rootView;

    self.profilePanel = [[S3GTrackerToolboxView alloc] initWithFrame:NSZeroRect];
    self.profilePanel.toolboxIndex = 0;
    self.profilePanel.toolboxTitle = @"PATTERN PROFILE  /  ORIGINAL → VARIANT";
    [self.rootView addSubview:self.profilePanel];
    self.mutationPanel = [[S3GTrackerToolboxView alloc] initWithFrame:NSZeroRect];
    self.mutationPanel.toolboxIndex = 0;
    self.mutationPanel.toolboxTitle = @"RHYTHM MUTATION / STATISTICAL";
    [self.rootView addSubview:self.mutationPanel];
    self.targetPanel = [[S3GTrackerToolboxView alloc] initWithFrame:NSZeroRect];
    self.targetPanel.toolboxIndex = 0;
    self.targetPanel.toolboxTitle = @"TARGET / ANALYZE";
    [self.rootView addSubview:self.targetPanel];
    self.timingPanel = [[S3GTrackerToolboxView alloc] initWithFrame:NSZeroRect];
    self.timingPanel.toolboxIndex = 0;
    self.timingPanel.toolboxTitle = @"TIMING / MICROTIME";
    [self.rootView addSubview:self.timingPanel];
    self.dynamicsPanel = [[S3GTrackerToolboxView alloc] initWithFrame:NSZeroRect];
    self.dynamicsPanel.toolboxIndex = 0;
    self.dynamicsPanel.toolboxTitle = @"DYNAMICS / VELOCITY";
    [self.rootView addSubview:self.dynamicsPanel];
    self.commitPanel = [[S3GTrackerToolboxView alloc] initWithFrame:NSZeroRect];
    self.commitPanel.toolboxIndex = 0;
    self.commitPanel.toolboxTitle = @"PREVIEW / APPLY";
    [self.rootView addSubview:self.commitPanel];

    self.profileView = [[S3GTrackerPatternProfileView alloc]
        initWithFrame:NSZeroRect];
    self.profileView.trackerState = state;
    self.profileView.accessibilityElement = YES;
    self.profileView.accessibilityRole = NSAccessibilityGroupRole;
    self.profileView.accessibilityLabel = @"Pattern reshape profile";
    self.profileView.toolTip =
        @"HITS compares original and mutated note-onset counts at each cycle phase. MT shows signed microtime in milliseconds. VEL shows phase velocity and LANE shows median velocity per lane, both on the MIDI 0–127 scale.";
    self.profileView.accessibilityHelp = self.profileView.toolTip;
    [self.profilePanel addSubview:self.profileView];

    self.patternPopup = [self popup:@selector(patternSelected:)
        panel:self.targetPanel];
    self.patternPopup.accessibilityLabel = @"Reshape target pattern";
    self.laneScopePopup = [self popup:@selector(laneScopeSelected:)
        panel:self.targetPanel];
    self.laneScopePopup.accessibilityLabel = @"Reshape included lanes";
    self.laneScopePopup.toolTip =
        @"Choose one lane, then toggle more lanes to reshape a group. ALL LANES restores the complete pattern.";
    self.cyclePopup = [self popup:@selector(cycleSelected:)
        panel:self.targetPanel];
    self.cyclePopup.accessibilityLabel = @"Reshape analysis cycle";
    for (NSString* title in @[@"AUTO", @"4", @"8", @"16", @"32", @"64"])
        [self.cyclePopup addItemWithTitle:title];
    self.analysisLabel = [NSTextField wrappingLabelWithString:@""];
    self.analysisLabel.font = s3g::clap_gui::uiFont(8.5);
    self.analysisLabel.textColor = s3g::clap_gui::color(0x929292);
    self.analysisLabel.maximumNumberOfLines = 3;
    [self.targetPanel addSubview:self.analysisLabel];
    self.targetLabels = @[
        [self rowLabel:@"PATTERN" panel:self.targetPanel],
        [self rowLabel:@"LANES" panel:self.targetPanel],
        [self rowLabel:@"CYCLE" panel:self.targetPanel],
    ];

    self.mutationAmountField = [self slider:@selector(mutationChanged:)
        minimum:0.0 maximum:100.0 panel:self.mutationPanel];
    self.densityField = [self slider:@selector(mutationChanged:)
        minimum:-100.0 maximum:100.0 panel:self.mutationPanel];
    self.syncopationField = [self slider:@selector(mutationChanged:)
        minimum:-100.0 maximum:100.0 panel:self.mutationPanel];
    self.displacementPopup = [self popup:@selector(mutationChanged:)
        panel:self.mutationPanel];
    [self.displacementPopup addItemsWithTitles:@[
        @"OFF", @"1 ROW", @"2 ROWS", @"3 ROWS", @"4 ROWS"
    ]];
    self.burstChanceField = [self slider:@selector(mutationChanged:)
        minimum:0.0 maximum:100.0 panel:self.mutationPanel];
    self.cycleDriftField = [self slider:@selector(mutationChanged:)
        minimum:0.0 maximum:100.0 panel:self.mutationPanel];
    self.reseedButton = [self button:@"RESEED · 0001"
        action:@selector(reseedPressed:) panel:self.mutationPanel];
    self.anchorValueLabel = [NSTextField labelWithString:
        @"DOWNBEAT / HIGH CONF"];
    self.anchorValueLabel.font = s3g::clap_gui::uiFont(8.5);
    self.anchorValueLabel.textColor = S3GTrackerThemeColor(
        S3GTrackerThemeRole::TextSecondary);
    self.anchorValueLabel.lineBreakMode = NSLineBreakByTruncatingTail;
    [self.mutationPanel addSubview:self.anchorValueLabel];
    self.mutationAmountField.accessibilityLabel = @"Mutation amount";
    self.densityField.accessibilityLabel = @"Mutation density change";
    self.syncopationField.accessibilityLabel = @"Mutation syncopation bias";
    self.displacementPopup.accessibilityLabel = @"Maximum hit displacement";
    self.burstChanceField.accessibilityLabel = @"Burst conversion chance";
    self.cycleDriftField.accessibilityLabel = @"Lane cycle drift chance";
    self.reseedButton.accessibilityLabel = @"Generate a new mutation seed";
    self.mutationAmountField.toolTip =
        @"Master probability and depth for note-event mutation. Zero preserves every hit.";
    self.densityField.toolTip =
        @"Negative values remove weaker hits; positive values add hits using each lane's default MIDI note.";
    self.syncopationField.toolTip =
        @"Bias displaced and added hits toward strong rows or offbeats without using a groove template.";
    self.displacementPopup.toolTip =
        @"Maximum number of tracker rows a non-anchor hit may move.";
    self.burstChanceField.toolTip =
        @"Convert suitable non-anchor notes to existing Burst definitions.";
    self.cycleDriftField.toolTip =
        @"Give lanes nearby independent cycle lengths from 4 to 64 rows.";
    self.anchorValueLabel.toolTip =
        @"Row-zero downbeats and statistically strong events are protected automatically.";
    self.mutationLeftLabels = @[
        [self rowLabel:@"AMOUNT" panel:self.mutationPanel],
        [self rowLabel:@"DENSITY" panel:self.mutationPanel],
        [self rowLabel:@"SYNCOPATE" panel:self.mutationPanel],
        [self rowLabel:@"SHIFT MAX" panel:self.mutationPanel],
    ];
    self.mutationRightLabels = @[
        [self rowLabel:@"BURSTS" panel:self.mutationPanel],
        [self rowLabel:@"CYCLE DRIFT" panel:self.mutationPanel],
        [self rowLabel:@"SEED" panel:self.mutationPanel],
        [self rowLabel:@"ANCHORS" panel:self.mutationPanel],
    ];

    self.pocketField = [self slider:@selector(timingChanged:)
        minimum:0.0 maximum:100.0 panel:self.timingPanel];
    self.tightenField = [self slider:@selector(timingChanged:)
        minimum:0.0 maximum:100.0 panel:self.timingPanel];
    self.depthField = [self slider:@selector(timingChanged:)
        minimum:-100.0 maximum:100.0 panel:self.timingPanel];
    self.timingWritePopup = [self popup:@selector(timingChanged:)
        panel:self.timingPanel];
    [self.timingWritePopup addItemsWithTitles:
        @[@"EXISTING ONLY", @"ADD TO ONSETS"]];
    self.timingOutlierPopup = [self popup:@selector(timingChanged:)
        panel:self.timingPanel];
    self.pocketField.accessibilityLabel = @"Reshape timing pocket";
    self.tightenField.accessibilityLabel = @"Reshape timing tighten";
    self.depthField.accessibilityLabel = @"Reshape timing depth";
    self.timingWritePopup.accessibilityLabel = @"Reshape microtiming write mode";
    self.timingOutlierPopup.accessibilityLabel = @"Reshape timing outliers";
    [self.timingOutlierPopup addItemsWithTitles:@[@"OFF", @"SOFT", @"STRONG"]];
    self.timingWritePopup.toolTip =
        @"EXISTING ONLY protects empty SEQ cells; ADD TO ONSETS infers MT from rhythmic and velocity anchors and uses only empty direct SEQ slots.";
    self.pocketField.toolTip =
        @"Blend toward the analyzed phase pocket; this is also the amount used when adding inferred MT.";
    self.tightenField.toolTip =
        @"Pull signed microtime toward the row grid. 0% retains the pocket; 100% quantizes MT to 0 ms.";
    self.depthField.toolTip =
        @"Flatten or exaggerate the analyzed timing contour: -100% is flat and +100% doubles it.";
    self.timingOutlierPopup.toolTip =
        @"Limit timing residuals only; velocity outliers are controlled separately.";
    self.timingLabels = @[
        [self rowLabel:@"POCKET" panel:self.timingPanel],
        [self rowLabel:@"TIGHTEN" panel:self.timingPanel],
        [self rowLabel:@"DEPTH" panel:self.timingPanel],
        [self rowLabel:@"WRITE MT" panel:self.timingPanel],
        [self rowLabel:@"MT OUTLIERS" panel:self.timingPanel],
    ];

    self.rangeField = [self slider:@selector(dynamicsChanged:)
        minimum:0.0 maximum:200.0 panel:self.dynamicsPanel];
    self.accentField = [self slider:@selector(dynamicsChanged:)
        minimum:0.0 maximum:200.0 panel:self.dynamicsPanel];
    self.balanceField = [self slider:@selector(dynamicsChanged:)
        minimum:0.0 maximum:100.0 panel:self.dynamicsPanel];
    self.velocityWritePopup = [self popup:@selector(dynamicsChanged:)
        panel:self.dynamicsPanel];
    [self.velocityWritePopup addItemsWithTitles:
        @[@"EXISTING ONLY", @"FILL DEFAULTS"]];
    self.velocityOutlierPopup = [self popup:@selector(dynamicsChanged:)
        panel:self.dynamicsPanel];
    [self.velocityOutlierPopup addItemsWithTitles:
        @[@"OFF", @"SOFT", @"STRONG"]];
    self.rangeField.accessibilityLabel = @"Reshape velocity range";
    self.accentField.accessibilityLabel = @"Reshape accent depth";
    self.balanceField.accessibilityLabel = @"Reshape lane balance";
    self.velocityWritePopup.accessibilityLabel =
        @"Reshape velocity write mode";
    self.velocityOutlierPopup.accessibilityLabel =
        @"Reshape velocity outliers";
    self.velocityWritePopup.toolTip =
        @"FILL DEFAULTS writes the transformed statistical velocity profile into Default cells; Previous cells remain intentional and protected.";
    self.rangeField.toolTip =
        @"Compress or expand velocity spread around each lane median.";
    self.accentField.toolTip =
        @"Flatten or exaggerate the phase accent profile inferred from velocity and hit density.";
    self.balanceField.toolTip =
        @"Pull separate lane velocity centers toward the pattern-wide median.";
    self.velocityOutlierPopup.toolTip =
        @"Limit velocity residuals only; MT outliers are controlled separately.";
    self.dynamicsLabels = @[
        [self rowLabel:@"RANGE" panel:self.dynamicsPanel],
        [self rowLabel:@"ACCENTS" panel:self.dynamicsPanel],
        [self rowLabel:@"LANE BAL" panel:self.dynamicsPanel],
        [self rowLabel:@"WRITE VEL" panel:self.dynamicsPanel],
        [self rowLabel:@"VEL OUTLIERS" panel:self.dynamicsPanel],
    ];

    self.previewButton = [self button:@"PREVIEW: OFF"
        action:@selector(previewPressed:) panel:self.commitPanel];
    self.previewButton.buttonType = NSButtonTypeToggle;
    self.originalButton = [self button:@"ORIGINAL"
        action:@selector(originalPressed:) panel:self.commitPanel];
    self.reshapedButton = [self button:@"RESHAPED"
        action:@selector(reshapedPressed:) panel:self.commitPanel];
    self.resetButton = [self button:@"RESET"
        action:@selector(resetPressed:) panel:self.commitPanel];
    self.applyButton = [self button:@"APPLY IN PLACE"
        action:@selector(applyPressed:) panel:self.commitPanel];
    self.createVariantButton = [self button:@"CREATE VARIANT"
        action:@selector(createVariantPressed:) panel:self.commitPanel];
    self.createVariantButton.toolTip =
        @"Add the current result to the pattern bank and select it, leaving the source unchanged.";
    self.resultLabel = [NSTextField wrappingLabelWithString:@""];
    self.resultLabel.font = s3g::clap_gui::uiFont(8.5);
    self.resultLabel.textColor = s3g::clap_gui::color(0x929292);
    self.resultLabel.maximumNumberOfLines = 4;
    [self.commitPanel addSubview:self.resultLabel];

    [self reloadModel];
    return self;
}

- (void)layoutReshapeInterface
{
    const CGFloat width = NSWidth(self.rootView.bounds);
    const CGFloat height = NSHeight(self.rootView.bounds);
    const auto family = s3g::gui_layout::trackerReshapeFamilyLayout({
        static_cast<double>(width), static_cast<double>(height),
    });
    const auto cocoaRect = [](const s3g::gui_layout::Rect& rect) {
        return NSMakeRect(static_cast<CGFloat>(rect.x),
            static_cast<CGFloat>(rect.y), static_cast<CGFloat>(rect.width),
            static_cast<CGFloat>(rect.height));
    };
    self.profilePanel.frame = cocoaRect(family.profilePanel);
    self.mutationPanel.frame = cocoaRect(family.mutation.frame);
    self.targetPanel.frame = cocoaRect(family.targetAnalyze.frame);
    self.timingPanel.frame = cocoaRect(family.timing.frame);
    self.dynamicsPanel.frame = cocoaRect(family.dynamics.frame);
    self.commitPanel.frame = cocoaRect(family.previewApply.frame);
    const CGFloat sideWidth = NSWidth(self.targetPanel.bounds);
    const auto& metrics = s3g::gui_layout::kStandardMetrics;
    const CGFloat header = static_cast<CGFloat>(metrics.headerHeight);
    self.profileView.frame = NSMakeRect(1.0, header,
        std::max<CGFloat>(1.0, NSWidth(self.profilePanel.bounds) - 2.0),
        std::max<CGFloat>(1.0, NSHeight(self.profilePanel.bounds) - header - 1.0));

    const CGFloat labelX = static_cast<CGFloat>(metrics.labelInset);
    const CGFloat controlX = static_cast<CGFloat>(metrics.controlInset);
    const CGFloat right = static_cast<CGFloat>(metrics.panelRightInset);
    const CGFloat controlWidth = std::max<CGFloat>(
        80.0, sideWidth - controlX - right);
    const CGFloat row0 = static_cast<CGFloat>(metrics.firstRowOffset);
    const CGFloat row = static_cast<CGFloat>(metrics.rowPitch);
    const auto placeLabels = ^(NSArray<NSTextField*>* labels) {
        for (NSUInteger index = 0u; index < labels.count; ++index)
            labels[index].frame = NSMakeRect(labelX,
                row0 - 1.0 + index * row,
                controlX - labelX - 7.0, 15.0);
    };
    const auto controlRect = ^NSRect(NSUInteger index) {
        return NSMakeRect(controlX, row0 - 1.0 + index * row,
            controlWidth, 15.0);
    };
    const auto sliderRect = ^NSRect(NSUInteger index) {
        return NSMakeRect(controlX, row0 - 8.0 + index * row,
            controlWidth, static_cast<CGFloat>(metrics.hitHeight));
    };
    placeLabels(self.targetLabels);
    self.patternPopup.frame = controlRect(0u);
    self.laneScopePopup.frame = controlRect(1u);
    self.cyclePopup.frame = controlRect(2u);
    self.analysisLabel.frame = NSMakeRect(labelX,
        row0 + row * 3.0 - 2.0,
        std::max<CGFloat>(80.0, sideWidth - labelX - right), 64.0);
    placeLabels(self.timingLabels);
    self.pocketField.frame = sliderRect(0u);
    self.tightenField.frame = sliderRect(1u);
    self.depthField.frame = sliderRect(2u);
    self.timingWritePopup.frame = controlRect(3u);
    self.timingOutlierPopup.frame = controlRect(4u);
    placeLabels(self.dynamicsLabels);
    self.rangeField.frame = sliderRect(0u);
    self.accentField.frame = sliderRect(1u);
    self.balanceField.frame = sliderRect(2u);
    self.velocityWritePopup.frame = controlRect(3u);
    self.velocityOutlierPopup.frame = controlRect(4u);

    const CGFloat mutationGap = static_cast<CGFloat>(metrics.panelGap);
    const CGFloat mutationColumnWidth =
        (NSWidth(self.mutationPanel.bounds) - mutationGap) * 0.5;
    const auto placeMutationColumn = ^(
        NSArray<NSTextField*>* labels, CGFloat originX) {
        for (NSUInteger index = 0u; index < labels.count; ++index) {
            labels[index].frame = NSMakeRect(originX + labelX,
                row0 - 1.0 + index * row,
                controlX - labelX - 7.0, 15.0);
        }
    };
    const auto mutationControlRect = ^NSRect(
        CGFloat originX, NSUInteger index) {
        return NSMakeRect(originX + controlX,
            row0 - 1.0 + index * row,
            std::max<CGFloat>(80.0,
                mutationColumnWidth - controlX - right), 15.0);
    };
    const auto mutationSliderRect = ^NSRect(
        CGFloat originX, NSUInteger index) {
        NSRect frame = mutationControlRect(originX, index);
        frame.origin.y -= 7.0;
        frame.size.height = static_cast<CGFloat>(metrics.hitHeight);
        return frame;
    };
    const CGFloat mutationRightX = mutationColumnWidth + mutationGap;
    placeMutationColumn(self.mutationLeftLabels, 0.0);
    placeMutationColumn(self.mutationRightLabels, mutationRightX);
    self.mutationAmountField.frame = mutationSliderRect(0.0, 0u);
    self.densityField.frame = mutationSliderRect(0.0, 1u);
    self.syncopationField.frame = mutationSliderRect(0.0, 2u);
    self.displacementPopup.frame = mutationControlRect(0.0, 3u);
    self.burstChanceField.frame = mutationSliderRect(mutationRightX, 0u);
    self.cycleDriftField.frame = mutationSliderRect(mutationRightX, 1u);
    self.reseedButton.frame = mutationControlRect(mutationRightX, 2u);
    self.anchorValueLabel.frame = mutationControlRect(mutationRightX, 3u);

    const CGFloat commitWidth = std::max<CGFloat>(80.0,
        NSWidth(self.commitPanel.bounds) - labelX * 2.0);
    self.previewButton.frame = NSMakeRect(labelX, row0 - 1.0,
        commitWidth, 15.0);
    const CGFloat half = (commitWidth - 5.0) * 0.5;
    self.originalButton.frame = NSMakeRect(labelX, row0 - 1.0 + row,
        half, 15.0);
    self.reshapedButton.frame = NSMakeRect(labelX + half + 5.0,
        row0 - 1.0 + row, half, 15.0);
    self.resetButton.frame = NSMakeRect(labelX, row0 - 1.0 + row * 2.0,
        half, 15.0);
    self.applyButton.frame = NSMakeRect(labelX + half + 5.0,
        row0 - 1.0 + row * 2.0, half, 15.0);
    self.createVariantButton.frame = NSMakeRect(labelX,
        row0 - 1.0 + row * 3.0, commitWidth, 15.0);
    self.resultLabel.frame = NSMakeRect(labelX, row0 + row * 4.0 - 2.0,
        commitWidth, std::max<CGFloat>(22.0,
            NSHeight(self.commitPanel.bounds) - row0 - row * 4.0 - 5.0));
}

- (void)refreshResult
{
    if (!self.trackerState) return;
    _settings.laneDefaultNotes = self.trackerState->session.laneDefaultNotes;
    _settings.burstBankId = self.trackerState->activeBurstBankId;
    _result = reshapePattern(self.trackerState->session.pattern,
        self.trackerState->session.burstLibrary, _settings);
    self.profileView.displaysReshaped = self.showingReshaped;
    [self.profileView setBeforeAnalysis:_result.before
        afterAnalysis:_result.after];
    const auto& before = _result.before;
    self.analysisLabel.stringValue = [NSString stringWithFormat:
        @"%lu HITS  ·  %lu MT  ·  %lu VEL\n%lu MT TARGETS  ·  %lu VEL DEFAULTS\nCONFIDENCE %.0f%%",
        static_cast<unsigned long>(before.noteEvents),
        static_cast<unsigned long>(before.timingValues),
        static_cast<unsigned long>(before.velocityValues),
        static_cast<unsigned long>(before.writableTimingOnsets),
        static_cast<unsigned long>(before.defaultVelocityValues),
        before.confidence * 100.0f];
    NSString* skipped = _result.timingSkipped > 0u
        ? [NSString stringWithFormat:@"  ·  %lu MT protected",
            static_cast<unsigned long>(_result.timingSkipped)] : @"";
    NSString* rhythm = _settings.mutationAmount > 0.0f
        ? [NSString stringWithFormat:
            @"HITS +%lu −%lu · MOVE %lu · BURST %lu\nCYCLES %lu CHANGED",
            static_cast<unsigned long>(_result.notesAdded),
            static_cast<unsigned long>(_result.notesRemoved),
            static_cast<unsigned long>(_result.notesMoved),
            static_cast<unsigned long>(_result.burstsCreated),
            static_cast<unsigned long>(_result.cyclesChanged)]
        : @"HITS PRESERVED";
    self.resultLabel.stringValue = [NSString stringWithFormat:
        @"%@\nMT %lu CHANGED / %lu ADDED\nVEL %lu CHANGED / %lu ADDED%@",
        rhythm,
        static_cast<unsigned long>(_result.timingChanged),
        static_cast<unsigned long>(_result.timingCreated),
        static_cast<unsigned long>(_result.velocityChanged),
        static_cast<unsigned long>(_result.velocityCreated), skipped];
    self.applyButton.enabled = _result.changed();
    self.createVariantButton.enabled = _result.changed()
        && self.trackerCallbacks && self.trackerCallbacks->createPatternVariant;
    [self syncPreview];
}

- (void)syncPreview
{
    if (!self.previewEnabled || !self.trackerCallbacks) return;
    if (self.showingReshaped) {
        if (self.trackerCallbacks->previewPattern)
            self.trackerCallbacks->previewPattern(_result.pattern);
    } else if (self.trackerCallbacks->clearPatternPreview) {
        self.trackerCallbacks->clearPatternPreview();
    }
}

- (void)reloadModel
{
    if (!self.trackerState) return;
    NSString* active = reshapeString(
        self.trackerState->patternBank.activePatternId);
    if (self.loadedPatternId && ![self.loadedPatternId isEqualToString:active])
        [self clearPreview];
    self.loadedPatternId = active;
    [self.patternPopup removeAllItems];
    for (const auto& entry : self.trackerState->patternBank.entries) {
        NSString* idText = reshapeString(entry.id);
        NSString* name = reshapeString(entry.pattern.name);
        NSString* title = name.length > 0u
            ? [NSString stringWithFormat:@"%@ · %@", idText, name] : idText;
        [self.patternPopup addItemWithTitle:title];
        self.patternPopup.lastItem.representedObject = idText;
        if (entry.id == self.trackerState->patternBank.activePatternId)
            [self.patternPopup selectItem:self.patternPopup.lastItem];
    }
    const auto laneCount = std::min<std::size_t>(
        self.trackerState->session.pattern.tracks.size(), 32u);
    const uint32_t validLaneMask = laneCount >= 32u
        ? 0xffffffffu
        : (laneCount == 0u ? 0u
            : (uint32_t { 1u } << static_cast<uint32_t>(laneCount)) - 1u);
    if (_settings.laneMask != 0xffffffffu) {
        _settings.laneMask &= validLaneMask;
        if (_settings.laneMask == 0u && validLaneMask != 0u) {
            const auto selected = std::min<std::size_t>(
                self.trackerState->session.selectedTrack, laneCount - 1u);
            _settings.laneMask = uint32_t { 1u }
                << static_cast<uint32_t>(selected);
        }
    }
    [self.laneScopePopup removeAllItems];
    [self.laneScopePopup addItemWithTitle:@"ALL LANES"];
    self.laneScopePopup.lastItem.representedObject = @(-1);
    const bool allLanes = _settings.laneMask == 0xffffffffu
        || (_settings.laneMask & validLaneMask) == validLaneMask;
    if (allLanes) _settings.laneMask = 0xffffffffu;
    std::size_t includedLanes = 0u;
    std::size_t onlyLane = 0u;
    for (std::size_t lane = 0u; lane < laneCount; ++lane) {
        const bool included = allLanes
            || (_settings.laneMask
                & (uint32_t { 1u } << static_cast<uint32_t>(lane))) != 0u;
        if (included) {
            ++includedLanes;
            onlyLane = lane;
        }
        const auto& track = self.trackerState->session.pattern.tracks[lane];
        NSString* name = track.name.empty()
            ? [NSString stringWithFormat:@"LANE %02lu",
                static_cast<unsigned long>(lane + 1u)]
            : reshapeString(track.name);
        [self.laneScopePopup addItemWithTitle:[NSString stringWithFormat:
            @"%@ L%02lu  %@", included ? @"●" : @"○",
            static_cast<unsigned long>(lane + 1u), name]];
        self.laneScopePopup.lastItem.representedObject = @(lane);
    }
    if (allLanes) {
        self.laneScopePopup.s3gDisplayTitle = @"ALL LANES";
    } else if (includedLanes == 1u) {
        self.laneScopePopup.s3gDisplayTitle = [NSString stringWithFormat:
            @"L%02lu ONLY", static_cast<unsigned long>(onlyLane + 1u)];
    } else {
        self.laneScopePopup.s3gDisplayTitle = [NSString stringWithFormat:
            @"%lu LANES", static_cast<unsigned long>(includedLanes)];
    }
    [self.laneScopePopup selectItemAtIndex:0u];
    const std::size_t cycle = _settings.cycleRows;
    [self.cyclePopup selectItemWithTitle:cycle == 0u ? @"AUTO"
        : [NSString stringWithFormat:@"%lu", static_cast<unsigned long>(cycle)]];
    self.pocketField.stringValue = percentString(_settings.pocket * 100.0);
    self.tightenField.stringValue = percentString(_settings.tighten * 100.0);
    self.depthField.stringValue = percentString(_settings.timingDepth * 100.0);
    self.rangeField.stringValue = percentString(_settings.velocityRange * 100.0);
    self.accentField.stringValue = percentString(_settings.accentDepth * 100.0);
    self.balanceField.stringValue = percentString(_settings.laneBalance * 100.0);
    self.mutationAmountField.stringValue = percentString(
        _settings.mutationAmount * 100.0);
    self.densityField.stringValue = percentString(
        _settings.densityChange * 100.0);
    self.syncopationField.stringValue = percentString(
        _settings.syncopation * 100.0);
    self.burstChanceField.stringValue = percentString(
        _settings.burstChance * 100.0);
    self.cycleDriftField.stringValue = percentString(
        _settings.cycleDrift * 100.0);
    [self.displacementPopup selectItemAtIndex:std::min<NSInteger>(
        static_cast<NSInteger>(_settings.displacementRows), 4)];
    self.reseedButton.title = [NSString stringWithFormat:@"RESEED · %04llu",
        static_cast<unsigned long long>(_settings.mutationSeed % 10000u)];
    [self.timingWritePopup selectItemAtIndex:
        _settings.microTimingWrite == PatternReshapeWriteMode::FillMissing
            ? 1 : 0];
    [self.velocityWritePopup selectItemAtIndex:
        _settings.velocityWrite == PatternReshapeWriteMode::FillMissing
            ? 1 : 0];
    [self.timingOutlierPopup selectItemAtIndex:
        _settings.timingOutlierThreshold <= 0.0f
            ? 0 : (_settings.timingOutlierThreshold >= 3.0f ? 1 : 2)];
    [self.velocityOutlierPopup selectItemAtIndex:
        _settings.velocityOutlierThreshold <= 0.0f
            ? 0 : (_settings.velocityOutlierThreshold >= 3.0f ? 1 : 2)];
    self.previewButton.state = self.previewEnabled
        ? NSControlStateValueOn : NSControlStateValueOff;
    self.previewButton.tag = self.previewEnabled ? 1 : 0;
    self.previewButton.title = self.previewEnabled
        ? @"PREVIEW: ON" : @"PREVIEW: OFF";
    self.originalButton.state = self.showingReshaped
        ? NSControlStateValueOff : NSControlStateValueOn;
    self.originalButton.tag = self.showingReshaped ? 0 : 1;
    self.reshapedButton.state = self.showingReshaped
        ? NSControlStateValueOn : NSControlStateValueOff;
    self.reshapedButton.tag = self.showingReshaped ? 1 : 0;
    [self refreshResult];
}

- (void)refreshPlaybackDisplay
{
    [self.profileView setNeedsDisplay:YES];
}

- (void)clearPreview
{
    if (!self.previewEnabled) return;
    self.previewEnabled = NO;
    if (self.trackerCallbacks && self.trackerCallbacks->clearPatternPreview)
        self.trackerCallbacks->clearPatternPreview();
    self.previewButton.state = NSControlStateValueOff;
    self.previewButton.tag = 0;
    self.previewButton.title = @"PREVIEW: OFF";
    [self.previewButton setNeedsDisplay:YES];
}

- (void)patternSelected:(S3GTrackerPopupButton*)sender
{
    NSString* identifier = sender.selectedItem.representedObject;
    if (!identifier || [identifier isEqualToString:self.loadedPatternId]) return;
    [self clearPreview];
    if (self.trackerCallbacks && self.trackerCallbacks->selectPattern)
        self.trackerCallbacks->selectPattern(
            identifier.UTF8String ? identifier.UTF8String : "");
}

- (void)cycleSelected:(S3GTrackerPopupButton*)sender
{
    _settings.cycleRows = [sender.selectedItem.title isEqualToString:@"AUTO"]
        ? 0u : static_cast<std::size_t>(sender.selectedItem.title.integerValue);
    [self refreshResult];
}

- (void)laneScopeSelected:(S3GTrackerPopupButton*)sender
{
    if (!self.trackerState) return;
    const NSInteger selected = sender.selectedItem.representedObject
        ? [sender.selectedItem.representedObject integerValue] : -1;
    if (selected < 0) {
        _settings.laneMask = 0xffffffffu;
        [self reloadModel];
        return;
    }
    const auto lane = static_cast<std::size_t>(selected);
    if (lane >= self.trackerState->session.pattern.tracks.size()
        || lane >= 32u) return;
    const uint32_t bit = uint32_t { 1u } << static_cast<uint32_t>(lane);
    const auto laneCount = std::min<std::size_t>(
        self.trackerState->session.pattern.tracks.size(), 32u);
    const uint32_t validLaneMask = laneCount >= 32u
        ? 0xffffffffu
        : (uint32_t { 1u } << static_cast<uint32_t>(laneCount)) - 1u;
    const bool allLanes = _settings.laneMask == 0xffffffffu
        || (_settings.laneMask & validLaneMask) == validLaneMask;
    if (allLanes) {
        _settings.laneMask = bit;
    } else if ((_settings.laneMask & bit) != 0u) {
        const uint32_t reduced = _settings.laneMask & ~bit;
        if (reduced != 0u) _settings.laneMask = reduced;
    } else {
        _settings.laneMask |= bit;
    }
    [self reloadModel];
}

- (void)timingChanged:(id)sender
{
    (void)sender;
    _settings.pocket = std::clamp<float>(
        self.pocketField.doubleValue / 100.0, 0.0, 1.0);
    _settings.tighten = std::clamp<float>(
        self.tightenField.doubleValue / 100.0, 0.0, 1.0);
    _settings.timingDepth = std::clamp<float>(
        self.depthField.doubleValue / 100.0, -1.0, 1.0);
    _settings.microTimingWrite =
        self.timingWritePopup.indexOfSelectedItem == 1
        ? PatternReshapeWriteMode::FillMissing
        : PatternReshapeWriteMode::ExistingOnly;
    switch (self.timingOutlierPopup.indexOfSelectedItem) {
    case 1: _settings.timingOutlierThreshold = 3.0f; break;
    case 2: _settings.timingOutlierThreshold = 2.0f; break;
    default: _settings.timingOutlierThreshold = 0.0f; break;
    }
    [self reloadModel];
}

- (void)dynamicsChanged:(id)sender
{
    (void)sender;
    _settings.velocityRange = std::clamp<float>(
        self.rangeField.doubleValue / 100.0, 0.0, 2.0);
    _settings.accentDepth = std::clamp<float>(
        self.accentField.doubleValue / 100.0, 0.0, 2.0);
    _settings.laneBalance = std::clamp<float>(
        self.balanceField.doubleValue / 100.0, 0.0, 1.0);
    _settings.velocityWrite =
        self.velocityWritePopup.indexOfSelectedItem == 1
        ? PatternReshapeWriteMode::FillMissing
        : PatternReshapeWriteMode::ExistingOnly;
    switch (self.velocityOutlierPopup.indexOfSelectedItem) {
    case 1: _settings.velocityOutlierThreshold = 3.0f; break;
    case 2: _settings.velocityOutlierThreshold = 2.0f; break;
    default: _settings.velocityOutlierThreshold = 0.0f; break;
    }
    [self reloadModel];
}

- (void)mutationChanged:(id)sender
{
    (void)sender;
    _settings.mutationAmount = std::clamp<float>(
        self.mutationAmountField.doubleValue / 100.0, 0.0, 1.0);
    _settings.densityChange = std::clamp<float>(
        self.densityField.doubleValue / 100.0, -1.0, 1.0);
    _settings.syncopation = std::clamp<float>(
        self.syncopationField.doubleValue / 100.0, -1.0, 1.0);
    _settings.displacementRows = static_cast<uint32_t>(std::clamp<NSInteger>(
        self.displacementPopup.indexOfSelectedItem, 0, 4));
    _settings.burstChance = std::clamp<float>(
        self.burstChanceField.doubleValue / 100.0, 0.0, 1.0);
    _settings.cycleDrift = std::clamp<float>(
        self.cycleDriftField.doubleValue / 100.0, 0.0, 1.0);
    [self reloadModel];
}

- (void)reseedPressed:(id)sender
{
    (void)sender;
    _settings.mutationSeed = _settings.mutationSeed
        * 6364136223846793005ULL + 1442695040888963407ULL;
    if (_settings.mutationSeed == 0u) _settings.mutationSeed = 1u;
    [self reloadModel];
}

- (void)previewPressed:(S3GTrackerActionButton*)sender
{
    self.previewEnabled = !self.previewEnabled;
    if (!self.previewEnabled) {
        if (self.trackerCallbacks && self.trackerCallbacks->clearPatternPreview)
            self.trackerCallbacks->clearPatternPreview();
    } else {
        self.showingReshaped = YES;
    }
    sender.state = self.previewEnabled
        ? NSControlStateValueOn : NSControlStateValueOff;
    sender.tag = self.previewEnabled ? 1 : 0;
    sender.title = self.previewEnabled ? @"PREVIEW: ON" : @"PREVIEW: OFF";
    [sender setNeedsDisplay:YES];
    [self reloadModel];
}

- (void)originalPressed:(id)sender
{
    (void)sender;
    self.showingReshaped = NO;
    [self reloadModel];
}

- (void)reshapedPressed:(id)sender
{
    (void)sender;
    self.showingReshaped = YES;
    [self reloadModel];
}

- (void)resetPressed:(id)sender
{
    (void)sender;
    _settings = defaultPanelSettings();
    [self reloadModel];
}

- (void)applyPressed:(id)sender
{
    (void)sender;
    if (!self.trackerState || !_result.changed()) return;
    self.previewEnabled = NO;
    self.trackerState->session.pattern = _result.pattern;
    self.trackerState->session.burstLibrary = _result.burstLibrary;
    self.trackerState->status = "Pattern reshape applied";
    if (self.trackerCallbacks && self.trackerCallbacks->patternChanged)
        self.trackerCallbacks->patternChanged();
}

- (void)createVariantPressed:(id)sender
{
    (void)sender;
    if (!self.trackerState || !_result.changed()
        || !self.trackerCallbacks
        || !self.trackerCallbacks->createPatternVariant) return;
    [self clearPreview];
    self.trackerCallbacks->createPatternVariant(_result.pattern);
}

@end
