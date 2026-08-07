#import "s3g_tracker_midi_window.h"

#import "s3g_tracker_controls.h"
#import "s3g_tracker_workspace.h"

#include "s3g/tracker/instrument_rack.h"

#include <algorithm>
#include <string>
#include <utility>

namespace {

using s3g::tracker::app::TrackerViewState;
using s3g::tracker::app::WorkspaceCallbacks;

NSTextField* label(NSString* value, CGFloat size = 9.0,
    NSColor* color = S3GTrackerColor(0x999999))
{
    NSTextField* field = [NSTextField labelWithString:value];
    field.font = S3GTrackerFont(size, NSFontWeightMedium);
    field.textColor = color;
    field.lineBreakMode = NSLineBreakByTruncatingMiddle;
    return field;
}

uint32_t firstActiveMidiNode(const TrackerViewState* state) noexcept
{
    if (!state) return s3g::tracker::kInvalidInstrumentNode;
    for (const auto& instrument : state->instrumentRack.instruments) {
        if (instrument.active
            && instrument.kind == s3g::tracker::InstrumentKind::MidiOut)
            return instrument.nodeId;
    }
    return s3g::tracker::kInvalidInstrumentNode;
}

} // namespace

@interface S3GTrackerMidiRouteView : NSView
@property(nonatomic, copy) NSString* endpointName;
@property(nonatomic) NSInteger channel;
@end

@implementation S3GTrackerMidiRouteView

- (BOOL)isFlipped { return YES; }

- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    [S3GTrackerColor(0x121212) setFill];
    NSRectFill(self.bounds);
    [S3GTrackerColor(0x414141) setStroke];
    NSFrameRect(NSInsetRect(self.bounds, 0.5, 0.5));

    NSArray<NSString*>* titles = @[
        @"TRACKER MIDI EVENTS",
        self.endpointName.length ? self.endpointName : @"NO ENDPOINT",
        [NSString stringWithFormat:@"CHANNEL %ld", self.channel],
    ];
    const CGFloat gap = 24.0;
    const CGFloat boxWidth = (NSWidth(self.bounds) - 48.0 - gap * 2.0) / 3.0;
    const CGFloat top = 34.0;
    NSBezierPath* path = [NSBezierPath bezierPath];
    for (NSInteger index = 0; index < 3; ++index) {
        const NSRect box = NSMakeRect(16.0 + index * (boxWidth + gap),
            top, boxWidth, 44.0);
        [(index == 1
                ? S3GTrackerThemeColor(S3GTrackerThemeRole::Live, 0.12)
                : S3GTrackerThemeColor(S3GTrackerThemeRole::Control)) setFill];
        NSRectFill(box);
        [(index == 1
                ? S3GTrackerThemeColor(S3GTrackerThemeRole::Live)
                : S3GTrackerThemeColor(S3GTrackerThemeRole::BorderStrong))
            setStroke];
        NSFrameRect(NSInsetRect(box, 0.5, 0.5));
        NSMutableParagraphStyle* paragraph = [[NSMutableParagraphStyle alloc] init];
        paragraph.alignment = NSTextAlignmentCenter;
        paragraph.lineBreakMode = NSLineBreakByTruncatingMiddle;
        [titles[index].uppercaseString drawInRect:NSInsetRect(box, 8.0, 14.0)
            withAttributes:@{
                NSForegroundColorAttributeName: index == 1
                    ? S3GTrackerThemeColor(S3GTrackerThemeRole::Live)
                    : S3GTrackerThemeColor(S3GTrackerThemeRole::TextSecondary),
                NSFontAttributeName: S3GTrackerFont(8.0, NSFontWeightMedium),
                NSParagraphStyleAttributeName: paragraph,
            }];
        if (index < 2) {
            [path moveToPoint:NSMakePoint(NSMaxX(box) + 4.0, NSMidY(box))];
            [path lineToPoint:NSMakePoint(NSMaxX(box) + gap - 4.0,
                NSMidY(box))];
        }
    }
    [S3GTrackerColor(0x707070) setStroke];
    path.lineWidth = 1.0;
    [path stroke];
}

@end

@interface S3GTrackerMidiWindowController () <NSWindowDelegate> {
@private
    TrackerViewState* _state;
    WorkspaceCallbacks* _callbacks;
    std::vector<s3g::tracker::MidiDestination> _destinations;
}
@property(nonatomic, strong) S3GTrackerPopupButton* instrumentPopup;
@property(nonatomic, strong) S3GTrackerPopupButton* endpointPopup;
@property(nonatomic, strong) S3GTrackerPopupButton* channelPopup;
@property(nonatomic, strong) S3GTrackerActionButton* auditionButton;
@property(nonatomic, strong) S3GTrackerActionButton* refreshButton;
@property(nonatomic, strong) NSTextField* routeSummary;
@property(nonatomic, strong) S3GTrackerMidiRouteView* routeView;
@end

@implementation S3GTrackerMidiWindowController

- (instancetype)initWithState:(TrackerViewState*)state
    callbacks:(WorkspaceCallbacks*)callbacks
{
    const NSRect frame = NSMakeRect(0.0, 0.0, 680.0, 410.0);
    NSWindow* window = [[NSWindow alloc] initWithContentRect:frame
        styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
            | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable)
        backing:NSBackingStoreBuffered defer:NO];
    self = [super initWithWindow:window];
    if (!self) return self;

    _state = state;
    _callbacks = callbacks;
    window.title = @"s3g Tracker — MIDI Instrument";
    window.minSize = NSMakeSize(580.0, 390.0);
    window.backgroundColor = S3GTrackerColor(0x0c0c0c);
    window.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
    window.tabbingMode = NSWindowTabbingModeDisallowed;
    window.releasedWhenClosed = NO;
    window.delegate = self;
    S3GTrackerRestoreWindowFrame(window, @"S3GTrackerMidiWindow");

    NSView* content = [[NSView alloc] initWithFrame:frame];
    content.wantsLayer = YES;
    content.layer.backgroundColor = S3GTrackerColor(0x0c0c0c).CGColor;
    window.contentView = content;

    NSTextField* title = label(@"MIDI INSTRUMENT ROUTE", 16.0,
        S3GTrackerColor(0xa8a8a8));
    title.frame = NSMakeRect(20.0, 365.0, 440.0, 24.0);
    title.autoresizingMask = NSViewMinYMargin;
    [content addSubview:title];
    NSTextField* subtitle = label(
        @"ONE INDEXED INSTRUMENT OWNS ONE ENDPOINT AND CHANNEL",
        8.0, S3GTrackerColor(0x858585));
    subtitle.frame = NSMakeRect(20.0, 342.0, 600.0, 16.0);
    subtitle.autoresizingMask = NSViewMinYMargin | NSViewWidthSizable;
    [content addSubview:subtitle];

    NSArray<NSString*>* fieldTitles = @[
        @"RACK INSTRUMENT", @"MIDI DEVICE", @"MIDI CHANNEL",
    ];
    for (NSInteger index = 0; index < 3; ++index) {
        NSTextField* fieldLabel = label(fieldTitles[index], 8.0,
            S3GTrackerColor(0x929292));
        fieldLabel.frame = NSMakeRect(20.0 + index * 210.0, 302.0,
            index == 1 ? 190.0 : 170.0, 14.0);
        fieldLabel.autoresizingMask = NSViewMinYMargin;
        [content addSubview:fieldLabel];
    }
    self.instrumentPopup = [[S3GTrackerPopupButton alloc]
        initWithFrame:NSMakeRect(20.0, 263.0, 190.0, 30.0) pullsDown:NO];
    self.endpointPopup = [[S3GTrackerPopupButton alloc]
        initWithFrame:NSMakeRect(230.0, 263.0, 270.0, 30.0) pullsDown:NO];
    self.channelPopup = [[S3GTrackerPopupButton alloc]
        initWithFrame:NSMakeRect(520.0, 263.0, 140.0, 30.0) pullsDown:NO];
    self.instrumentPopup.autoresizingMask = NSViewMinYMargin;
    self.endpointPopup.autoresizingMask = NSViewMinYMargin | NSViewWidthSizable;
    self.channelPopup.autoresizingMask = NSViewMinYMargin | NSViewMinXMargin;
    self.instrumentPopup.target = self;
    self.instrumentPopup.action = @selector(instrumentChanged:);
    self.endpointPopup.target = self;
    self.endpointPopup.action = @selector(endpointChanged:);
    self.channelPopup.target = self;
    self.channelPopup.action = @selector(channelChanged:);
    for (NSInteger channel = 1; channel <= 16; ++channel)
        [self.channelPopup addItemWithTitle:
            [NSString stringWithFormat:@"CHANNEL %ld", channel]];
    [content addSubview:self.instrumentPopup];
    [content addSubview:self.endpointPopup];
    [content addSubview:self.channelPopup];

    self.routeView = [[S3GTrackerMidiRouteView alloc]
        initWithFrame:NSMakeRect(20.0, 104.0, 640.0, 126.0)];
    self.routeView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    [content addSubview:self.routeView];
    self.routeSummary = label(@"", 8.0, S3GTrackerColor(0x8f8f8f));
    self.routeSummary.frame = NSMakeRect(20.0, 68.0, 330.0, 18.0);
    self.routeSummary.autoresizingMask = NSViewWidthSizable | NSViewMaxYMargin;
    [content addSubview:self.routeSummary];
    self.auditionButton = [[S3GTrackerActionButton alloc]
        initWithFrame:NSMakeRect(370.0, 60.0, 135.0, 30.0)];
    self.auditionButton.title = @"AUDITION C4";
    self.auditionButton.target = self;
    self.auditionButton.action = @selector(auditionPressed:);
    self.auditionButton.autoresizingMask = NSViewMinXMargin
        | NSViewMaxYMargin;
    [content addSubview:self.auditionButton];
    self.refreshButton = [[S3GTrackerActionButton alloc]
        initWithFrame:NSMakeRect(520.0, 60.0, 140.0, 30.0)];
    self.refreshButton.title = @"REFRESH DEVICES";
    self.refreshButton.target = self;
    self.refreshButton.action = @selector(refreshPressed:);
    self.refreshButton.autoresizingMask = NSViewMinXMargin | NSViewMaxYMargin;
    [content addSubview:self.refreshButton];
    NSTextField* note = label(
        @"ROUTE EDITS STOP TRANSPORT FIRST SO OLD SCHEDULED NOTES ARE CLEANED ON THEIR ORIGINAL ENDPOINT.",
        6.8, S3GTrackerColor(0x6f6f6f));
    note.frame = NSMakeRect(20.0, 26.0, 640.0, 14.0);
    note.autoresizingMask = NSViewWidthSizable | NSViewMaxYMargin;
    [content addSubview:note];
    [self reloadModel];
    return self;
}

- (uint32_t)selectedNode
{
    if (_state && s3g::tracker::isMidiOutInstrumentNode(
            _state->selectedRackInstrument))
        return _state->selectedRackInstrument;
    return firstActiveMidiNode(_state);
}

- (void)setDestinations:
    (const std::vector<s3g::tracker::MidiDestination>&)destinations
{
    _destinations = destinations;
    [self reloadModel];
}

- (void)reloadModel
{
    if (!_state || !self.instrumentPopup) return;
    const auto selectedNode = [self selectedNode];
    [self.instrumentPopup removeAllItems];
    NSInteger instrumentSelection = -1;
    for (const auto& instrument : _state->instrumentRack.instruments) {
        if (!instrument.active
            || instrument.kind != s3g::tracker::InstrumentKind::MidiOut)
            continue;
        const auto rackIndex = s3g::tracker::rackIndexForNode(
            _state->instrumentRack, instrument.nodeId);
        [self.instrumentPopup addItemWithTitle:[NSString stringWithFormat:
            @"%02lu  MIDI OUT", static_cast<unsigned long>(rackIndex)]];
        self.instrumentPopup.lastItem.representedObject = @(instrument.nodeId);
        if (instrument.nodeId == selectedNode)
            instrumentSelection = self.instrumentPopup.numberOfItems - 1;
    }
    if (instrumentSelection >= 0)
        [self.instrumentPopup selectItemAtIndex:instrumentSelection];

    const auto* route = s3g::tracker::midiInstrumentRoute(
        _state->instrumentRack, selectedNode);
    [self.endpointPopup removeAllItems];
    NSInteger endpointSelection = -1;
    for (std::size_t source = 1u;
        source <= s3g::tracker::kMidiOutRackSlotCount; ++source) {
        [self.endpointPopup addItemWithTitle:[NSString stringWithFormat:
            @"REAPER MIDI BUS %lu  [HOST]", static_cast<unsigned long>(source)]];
        self.endpointPopup.lastItem.representedObject = @{
            @"virtual": @YES, @"value": @(source),
        };
        if (route
            && route->kind == s3g::tracker::MidiInstrumentRouteKind::VirtualSource
            && route->virtualSource == source)
            endpointSelection = self.endpointPopup.numberOfItems - 1;
    }
    if (!_destinations.empty()) [self.endpointPopup.menu addItem:
        [NSMenuItem separatorItem]];
    for (const auto& destination : _destinations) {
        [self.endpointPopup addItemWithTitle:[NSString stringWithUTF8String:
            destination.name.c_str()]];
        self.endpointPopup.lastItem.representedObject = @{
            @"virtual": @NO, @"value": @(destination.id),
        };
        if (route
            && route->kind == s3g::tracker::MidiInstrumentRouteKind::Destination
            && route->destinationId == destination.id)
            endpointSelection = self.endpointPopup.numberOfItems - 1;
    }
    if (route
        && route->kind == s3g::tracker::MidiInstrumentRouteKind::Destination
        && endpointSelection < 0) {
        [self.endpointPopup addItemWithTitle:[NSString stringWithFormat:
            @"DISCONNECTED DESTINATION  [%d]", route->destinationId]];
        self.endpointPopup.lastItem.representedObject = @{
            @"virtual": @NO, @"value": @(route->destinationId),
        };
        endpointSelection = self.endpointPopup.numberOfItems - 1;
    }
    if (endpointSelection >= 0)
        [self.endpointPopup selectItemAtIndex:endpointSelection];
    if (route) [self.channelPopup selectItemAtIndex:route->channel - 1u];
    NSString* endpoint = self.endpointPopup.titleOfSelectedItem;
    if (!endpoint) endpoint = @"—";
    self.routeView.endpointName = endpoint;
    self.routeView.channel = route ? route->channel : 1;
    [self.routeView setNeedsDisplay:YES];
    self.routeSummary.stringValue = route
        ? [NSString stringWithFormat:@"%@  /  CHANNEL %u", endpoint,
            static_cast<unsigned int>(route->channel)]
        : @"NO ACTIVE MIDI INSTRUMENT";
    self.auditionButton.enabled = route != nullptr && !_state->playing;
}

- (void)publishRoute:(s3g::tracker::MidiInstrumentRoute)route
{
    if (!_state) return;
    if (s3g::tracker::setMidiInstrumentRoute(_state->instrumentRack,
            [self selectedNode], route)
        && _callbacks && _callbacks->instrumentRackChanged)
        _callbacks->instrumentRackChanged();
    [self reloadModel];
}

- (void)instrumentChanged:(id)sender
{
    (void)sender;
    NSNumber* node = self.instrumentPopup.selectedItem.representedObject;
    if (!_state || !node) return;
    _state->selectedRackInstrument = node.unsignedIntValue;
    [self reloadModel];
}

- (void)endpointChanged:(id)sender
{
    (void)sender;
    if (!_state) return;
    const auto* route = s3g::tracker::midiInstrumentRoute(
        _state->instrumentRack, [self selectedNode]);
    NSDictionary* value = self.endpointPopup.selectedItem.representedObject;
    if (!route || ![value isKindOfClass:NSDictionary.class]) return;
    auto updated = *route;
    if ([value[@"virtual"] boolValue]) {
        updated.kind = s3g::tracker::MidiInstrumentRouteKind::VirtualSource;
        updated.destinationId = 0;
        updated.virtualSource = static_cast<uint8_t>(
            [value[@"value"] unsignedIntegerValue]);
    } else {
        updated.kind = s3g::tracker::MidiInstrumentRouteKind::Destination;
        updated.destinationId = [value[@"value"] intValue];
    }
    [self publishRoute:updated];
}

- (void)channelChanged:(id)sender
{
    (void)sender;
    if (!_state) return;
    const auto* route = s3g::tracker::midiInstrumentRoute(
        _state->instrumentRack, [self selectedNode]);
    if (!route) return;
    auto updated = *route;
    updated.channel = static_cast<uint8_t>(
        self.channelPopup.indexOfSelectedItem + 1u);
    [self publishRoute:updated];
}

- (void)refreshPressed:(id)sender
{
    (void)sender;
    if (_callbacks && _callbacks->refreshMidiDestinations)
        _callbacks->refreshMidiDestinations();
}

- (void)auditionPressed:(id)sender
{
    (void)sender;
    if (_callbacks && _callbacks->auditionInstrument)
        _callbacks->auditionInstrument([self selectedNode], 60u, 0.82f);
}

- (void)showWindow:(id)sender
{
    [self reloadModel];
    [super showWindow:sender];
    [self.window makeKeyAndOrderFront:sender];
    [NSApp activateIgnoringOtherApps:YES];
}

@end
