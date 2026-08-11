#import "s3g_tracker_warp_window.h"

#import "s3g_tracker_controls.h"
#import "s3g_tracker_workspace.h"

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
    field.font = S3GTrackerFont(size, NSFontWeightMedium);
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

@interface S3GTrackerWarpCurveView : NSView
@property(nonatomic, assign) TrackerViewState* trackerState;
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
    const auto cycle = std::max<uint32_t>(1u, transport.warpCycleTicks);
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
        const double output = transport.timingWarp.map(input);
        const NSPoint point = NSMakePoint(NSMinX(graph)
                + NSWidth(graph) * static_cast<CGFloat>(input),
            NSMaxY(graph) - NSHeight(graph) * static_cast<CGFloat>(output));
        if (index == 0u) [curve moveToPoint:point];
        else [curve lineToPoint:point];
    }
    curve.lineWidth = 2.0;
    [S3GTrackerThemeColor(S3GTrackerThemeRole::Live) setStroke];
    [curve stroke];

    for (uint32_t tick = 0u; tick <= cycle; ++tick) {
        const double input = static_cast<double>(tick)
            / static_cast<double>(cycle);
        const double output = transport.timingWarp.map(input);
        const NSPoint point = NSMakePoint(NSMinX(graph)
                + NSWidth(graph) * static_cast<CGFloat>(input),
            NSMaxY(graph) - NSHeight(graph) * static_cast<CGFloat>(output));
        [S3GTrackerThemeColor(S3GTrackerThemeRole::Value) setFill];
        [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(
            point.x - 2.5, point.y - 2.5, 5.0, 5.0)] fill];
    }

    NSDictionary* attributes = @{
        NSForegroundColorAttributeName: S3GTrackerThemeColor(
            S3GTrackerThemeRole::TextMuted),
        NSFontAttributeName: S3GTrackerFont(8.0, NSFontWeightMedium),
    };
    [@"INPUT PHASE" drawAtPoint:NSMakePoint(NSMaxX(graph) - 70.0,
        NSMaxY(graph) + 5.0) withAttributes:attributes];
    [@"WARPED" drawAtPoint:NSMakePoint(NSMinX(graph), 4.0)
        withAttributes:attributes];
}

@end

@interface S3GTrackerWarpWindowController () <NSWindowDelegate>
@property(nonatomic, assign) TrackerViewState* trackerState;
@property(nonatomic, assign) WorkspaceCallbacks* trackerCallbacks;
@property(nonatomic, strong) S3GTrackerWarpCurveView* curveView;
@property(nonatomic, strong) NSPopUpButton* libraryPopup;
@property(nonatomic, strong) NSTextField* libraryNameField;
@property(nonatomic, strong) NSTextField* cycleField;
@property(nonatomic, strong) NSPopUpButton* transformPopup;
@property(nonatomic, strong) NSPopUpButton* typePopup;
@property(nonatomic, strong) NSTextField* primaryLabel;
@property(nonatomic, strong) NSTextField* primaryField;
@property(nonatomic, strong) NSTextField* pulsesLabel;
@property(nonatomic, strong) NSTextField* pulsesField;
@property(nonatomic, strong) NSTextField* mixField;
@property(nonatomic, strong) NSTextField* beginField;
@property(nonatomic, strong) NSTextField* endField;
@property(nonatomic, strong) NSTextField* repeatsField;
@property(nonatomic, strong) NSTextField* statusLabel;
@property(nonatomic) NSInteger selectedTransform;
@property(nonatomic) NSInteger selectedLibrarySlot;
@end

@implementation S3GTrackerWarpWindowController

- (NSTextField*)editorField:(CGFloat)width action:(SEL)action
{
    NSTextField* field = [[NSTextField alloc] initWithFrame:NSZeroRect];
    S3GTrackerStyleTextEditor(field);
    field.target = self;
    field.action = action;
    [field.widthAnchor constraintEqualToConstant:width].active = YES;
    return field;
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

    S3GTrackerPanelView* root = [[S3GTrackerPanelView alloc]
        initWithFrame:window.contentView.bounds];
    root.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    window.contentView = root;

    NSStackView* libraryBar = [[NSStackView alloc] initWithFrame:NSZeroRect];
    libraryBar.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    libraryBar.alignment = NSLayoutAttributeCenterY;
    libraryBar.spacing = 8.0;
    libraryBar.translatesAutoresizingMaskIntoConstraints = NO;
    [root addSubview:libraryBar];
    [libraryBar addArrangedSubview:warpLabel(@"WARP LIBRARY")];
    self.libraryPopup = [[S3GTrackerPopupButton alloc]
        initWithFrame:NSZeroRect pullsDown:NO];
    self.libraryPopup.target = self;
    self.libraryPopup.action = @selector(librarySlotSelected:);
    [self.libraryPopup.widthAnchor constraintEqualToConstant:230.0].active = YES;
    [libraryBar addArrangedSubview:self.libraryPopup];
    [libraryBar addArrangedSubview:warpLabel(@"NAME")];
    self.libraryNameField = [self editorField:190.0
        action:@selector(saveLibrarySlot:)];
    [libraryBar addArrangedSubview:self.libraryNameField];
    for (NSArray* spec in @[
             @[ @"SAVE", @"saveLibrarySlot:" ],
             @[ @"RECALL", @"recallLibrarySlot:" ],
             @[ @"DELETE", @"deleteLibrarySlot:" ] ]) {
        S3GTrackerActionButton* button = [[S3GTrackerActionButton alloc]
            initWithFrame:NSZeroRect];
        button.title = spec[0];
        button.target = self;
        button.action = NSSelectorFromString(spec[1]);
        [libraryBar addArrangedSubview:button];
    }

    NSStackView* toolbar = [[NSStackView alloc] initWithFrame:NSZeroRect];
    toolbar.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    toolbar.alignment = NSLayoutAttributeCenterY;
    toolbar.spacing = 8.0;
    toolbar.translatesAutoresizingMaskIntoConstraints = NO;
    [root addSubview:toolbar];
    [toolbar addArrangedSubview:warpLabel(@"CYCLE TICKS")];
    self.cycleField = [self editorField:48.0 action:@selector(cycleChanged:)];
    [toolbar addArrangedSubview:self.cycleField];
    [toolbar addArrangedSubview:warpLabel(@"TRANSFORM")];
    self.transformPopup = [[S3GTrackerPopupButton alloc]
        initWithFrame:NSZeroRect pullsDown:NO];
    self.transformPopup.target = self;
    self.transformPopup.action = @selector(transformSelected:);
    [self.transformPopup.widthAnchor constraintEqualToConstant:180.0].active = YES;
    [toolbar addArrangedSubview:self.transformPopup];
    for (NSArray* spec in @[
             @[ @"+ EXP", @0 ], @[ @"+ STEP", @1 ], @[ @"+ EUCLID", @2 ] ]) {
        S3GTrackerActionButton* button = [[S3GTrackerActionButton alloc]
            initWithFrame:NSZeroRect];
        button.title = spec[0];
        button.identifier = [NSString stringWithFormat:@"warp-add-%@", spec[1]];
        button.target = self;
        button.action = @selector(addTransform:);
        [toolbar addArrangedSubview:button];
    }
    S3GTrackerActionButton* remove = [[S3GTrackerActionButton alloc]
        initWithFrame:NSZeroRect];
    remove.title = @"REMOVE";
    remove.target = self;
    remove.action = @selector(removeTransform:);
    [toolbar addArrangedSubview:remove];
    S3GTrackerActionButton* clear = [[S3GTrackerActionButton alloc]
        initWithFrame:NSZeroRect];
    clear.title = @"CLEAR";
    clear.tag = 2;
    clear.target = self;
    clear.action = @selector(clearTransforms:);
    [toolbar addArrangedSubview:clear];

    self.curveView = [[S3GTrackerWarpCurveView alloc] initWithFrame:NSZeroRect];
    self.curveView.trackerState = state;
    self.curveView.translatesAutoresizingMaskIntoConstraints = NO;
    self.curveView.accessibilityElement = YES;
    self.curveView.accessibilityRole = NSAccessibilityImageRole;
    self.curveView.accessibilityLabel = @"Composite timing warp curve";
    [root addSubview:self.curveView];

    S3GTrackerPanelView* editor = [[S3GTrackerPanelView alloc]
        initWithFrame:NSZeroRect];
    editor.translatesAutoresizingMaskIntoConstraints = NO;
    [root addSubview:editor];
    NSStackView* rowOne = [[NSStackView alloc] initWithFrame:NSZeroRect];
    rowOne.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    rowOne.alignment = NSLayoutAttributeCenterY;
    rowOne.spacing = 8.0;
    rowOne.translatesAutoresizingMaskIntoConstraints = NO;
    [editor addSubview:rowOne];
    [rowOne addArrangedSubview:warpLabel(@"TYPE")];
    self.typePopup = [[S3GTrackerPopupButton alloc]
        initWithFrame:NSZeroRect pullsDown:NO];
    for (NSArray* spec in @[
             @[ @"EXPONENTIAL", @0 ], @[ @"STEP QUANTIZE", @1 ],
             @[ @"EUCLIDEAN QUANTIZE", @2 ] ]) {
        [self.typePopup addItemWithTitle:spec[0]];
        self.typePopup.lastItem.representedObject = spec[1];
    }
    self.typePopup.target = self;
    self.typePopup.action = @selector(typeChanged:);
    [self.typePopup.widthAnchor constraintEqualToConstant:190.0].active = YES;
    [rowOne addArrangedSubview:self.typePopup];
    self.primaryLabel = warpLabel(@"POWER");
    [rowOne addArrangedSubview:self.primaryLabel];
    self.primaryField = [self editorField:70.0
        action:@selector(transformChanged:)];
    [rowOne addArrangedSubview:self.primaryField];
    self.pulsesLabel = warpLabel(@"PULSES");
    [rowOne addArrangedSubview:self.pulsesLabel];
    self.pulsesField = [self editorField:60.0
        action:@selector(transformChanged:)];
    [rowOne addArrangedSubview:self.pulsesField];

    NSStackView* rowTwo = [[NSStackView alloc] initWithFrame:NSZeroRect];
    rowTwo.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    rowTwo.alignment = NSLayoutAttributeCenterY;
    rowTwo.spacing = 8.0;
    rowTwo.translatesAutoresizingMaskIntoConstraints = NO;
    [editor addSubview:rowTwo];
    [rowTwo addArrangedSubview:warpLabel(@"MIX")];
    self.mixField = [self editorField:64.0 action:@selector(transformChanged:)];
    [rowTwo addArrangedSubview:self.mixField];
    [rowTwo addArrangedSubview:warpLabel(@"SEGMENT START")];
    self.beginField = [self editorField:64.0 action:@selector(transformChanged:)];
    [rowTwo addArrangedSubview:self.beginField];
    [rowTwo addArrangedSubview:warpLabel(@"END")];
    self.endField = [self editorField:64.0 action:@selector(transformChanged:)];
    [rowTwo addArrangedSubview:self.endField];
    [rowTwo addArrangedSubview:warpLabel(@"REPEAT")];
    self.repeatsField = [self editorField:54.0 action:@selector(transformChanged:)];
    [rowTwo addArrangedSubview:self.repeatsField];

    self.statusLabel = warpLabel(@"SERIAL STACK • INPUT PHASE → WARPED PHASE", 8.5);
    self.statusLabel.textColor = S3GTrackerThemeColor(
        S3GTrackerThemeRole::TextMuted);
    self.statusLabel.translatesAutoresizingMaskIntoConstraints = NO;
    [editor addSubview:self.statusLabel];

    [NSLayoutConstraint activateConstraints:@[
        [libraryBar.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:16.0],
        [libraryBar.trailingAnchor constraintLessThanOrEqualToAnchor:root.trailingAnchor constant:-16.0],
        [libraryBar.topAnchor constraintEqualToAnchor:root.topAnchor constant:14.0],
        [toolbar.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:16.0],
        [toolbar.trailingAnchor constraintLessThanOrEqualToAnchor:root.trailingAnchor constant:-16.0],
        [toolbar.topAnchor constraintEqualToAnchor:libraryBar.bottomAnchor constant:10.0],
        [self.curveView.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:16.0],
        [self.curveView.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-16.0],
        [self.curveView.topAnchor constraintEqualToAnchor:toolbar.bottomAnchor constant:14.0],
        [self.curveView.bottomAnchor constraintEqualToAnchor:editor.topAnchor constant:-12.0],
        [editor.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:16.0],
        [editor.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-16.0],
        [editor.bottomAnchor constraintEqualToAnchor:root.bottomAnchor constant:-16.0],
        [editor.heightAnchor constraintEqualToConstant:134.0],
        [rowOne.leadingAnchor constraintEqualToAnchor:editor.leadingAnchor constant:12.0],
        [rowOne.topAnchor constraintEqualToAnchor:editor.topAnchor constant:14.0],
        [rowTwo.leadingAnchor constraintEqualToAnchor:rowOne.leadingAnchor],
        [rowTwo.topAnchor constraintEqualToAnchor:rowOne.bottomAnchor constant:12.0],
        [self.statusLabel.leadingAnchor constraintEqualToAnchor:rowOne.leadingAnchor],
        [self.statusLabel.bottomAnchor constraintEqualToAnchor:editor.bottomAnchor constant:-10.0],
    ]];
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

- (void)reloadModel
{
    if (!self.trackerState || !self.window) return;
    const auto& library = self.trackerState->session.warpLibrary;
    [self.libraryPopup removeAllItems];
    for (std::size_t index = 0u;
         index < s3g::tracker::kMaximumTimingWarpLibraryEntries; ++index) {
        const auto* entry = library.entry(index);
        NSString* title = entry
            ? [NSString stringWithFormat:@"%02lu  %@  ·  %uT / %luX",
                static_cast<unsigned long>(index + 1u),
                [NSString stringWithUTF8String:entry->name.c_str()],
                entry->cycleTicks,
                static_cast<unsigned long>(entry->stack.size())]
            : [NSString stringWithFormat:@"%02lu  EMPTY",
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
    const auto& transport = self.trackerState->session.transport;
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
        self.primaryLabel.stringValue = transform->kind
                == TimingWarpKind::Exponential ? @"POWER" : @"STEPS";
        self.primaryField.doubleValue = transform->kind
                == TimingWarpKind::Exponential
            ? transform->exponent : transform->steps;
        const BOOL euclidean = transform->kind
            == TimingWarpKind::EuclideanQuantize;
        self.pulsesLabel.hidden = !euclidean;
        self.pulsesField.hidden = !euclidean;
        self.pulsesField.integerValue = transform->pulses;
        self.mixField.doubleValue = transform->options.alpha;
        self.beginField.doubleValue = transform->options.phaseBegin;
        self.endField.doubleValue = transform->options.phaseEnd;
        self.repeatsField.integerValue = transform->options.repetitions;
        self.statusLabel.stringValue = [NSString stringWithFormat:
            @"%lu TRANSFORM%@ • SERIAL LEFT → RIGHT • %lu SAVED • WRP RECALLS 01–64",
            static_cast<unsigned long>(count), count == 1u ? @"" : @"S",
            static_cast<unsigned long>(library.size())];
    } else {
        self.primaryLabel.stringValue = @"POWER";
        self.pulsesLabel.hidden = YES;
        self.pulsesField.hidden = YES;
        self.statusLabel.stringValue = [NSString stringWithFormat:
            @"IDENTITY TIMING • ADD EXP, STEP, OR EUCLID • %lu SAVED",
            static_cast<unsigned long>(library.size())];
    }
    [self.curveView setNeedsDisplay:YES];
}

- (void)librarySlotSelected:(id)sender
{
    (void)sender;
    self.selectedLibrarySlot = std::max<NSInteger>(0,
        self.libraryPopup.indexOfSelectedItem);
    [self reloadModel];
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

- (void)recallLibrarySlot:(id)sender
{
    (void)sender;
    if (!self.trackerState) return;
    const auto index = static_cast<std::size_t>(std::max<NSInteger>(0,
        self.selectedLibrarySlot));
    const auto* entry = self.trackerState->session.warpLibrary.entry(index);
    if (!entry) { NSBeep(); return; }
    self.trackerState->session.transport.warpCycleTicks = entry->cycleTicks;
    self.trackerState->session.transport.timingWarp = entry->stack;
    self.selectedTransform = 0;
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
            TimingWarpStack::kMaximumRepetitions);
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
                TimingWarpStack::kMaximumSteps)
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
